#include "client/mapview.h"

#include "client/gpuvram.h"
#include "hpi/hpi.h"           // tak::hpi::Vfs::read (ctor / reload)
#include "tnt/mapgen.h"        // "~gen1~" random-map ids -> procedural map

#include <algorithm>
#include <cmath>

tak::tnt::Map MapView::genOrLoad(const tak::hpi::Vfs& vfs, const std::string& mapPath) {
    if (tak::mapgen::isGeneratedMapId(mapPath))
        return tak::mapgen::generate(tak::mapgen::decodeMapId(mapPath)).map;
    return tak::tnt::Map::load(vfs.read(mapPath), mapPath);
}

MapView::MapView(SDL_Renderer* ren, const tak::hpi::Vfs& vfs, const std::string& mapPath)
    : ren_(ren), map_(genOrLoad(vfs, mapPath)), comp_(vfs) {
    chunkWorker_ = std::thread([this] { chunkWorkerLoop(); });
}

MapView::~MapView() {
    {
        std::lock_guard<std::mutex> lk(chunkMu_);
        chunkStop_ = true;
    }
    chunkCv_.notify_all();
    if (chunkWorker_.joinable()) chunkWorker_.join();
    if (waterMask_) gpuvram::destroy(waterMask_);
    if (caustic_) gpuvram::destroy(caustic_);
    if (waterRT_) gpuvram::destroy(waterRT_);
}

void MapView::reload(const tak::hpi::Vfs& vfs, const std::string& mapPath) {
    // Quiesce the chunk worker first: it reads map_, which is about to be swapped.
    {
        std::unique_lock<std::mutex> lk(chunkMu_);
        chunkQueue_.clear();
        chunkCv_.wait(lk, [this] { return !chunkBusy_; });
        chunkDone_.clear();      // stale composites of the OLD map
        chunkPending_.clear();
    }
    for (auto& [k, t] : chunks_) if (t) gpuvram::destroy(t);
    chunks_.clear();
    map_ = genOrLoad(vfs, mapPath);
    if (waterMask_) { gpuvram::destroy(waterMask_); waterMask_ = nullptr; }
    waterChecked_ = false; hasWater_ = false;   // rebuild the mask for the new map
}

void MapView::input(const SDL_Event& e) {
    if (e.type == SDL_MOUSEMOTION && (e.motion.state & SDL_BUTTON_LMASK)) {
        offX_ -= e.motion.xrel / zoom_;
        offY_ -= e.motion.yrel / zoom_;
    } else if (e.type == SDL_MOUSEWHEEL) {
        // Half the old per-notch step (1.25/0.8): 1.25^0.5 in, its reciprocal out.
        // zoomSpeed_ is an exponent so in/out stay reciprocal and 1.0 == the base.
        float f = std::pow(e.wheel.y > 0 ? 1.118f : 0.894f, zoomSpeed_);
        zoom_ = std::clamp(zoom_ * f, 0.05f, 4.0f);
    } else if (e.type == SDL_KEYDOWN) {
        float step = 200 / zoom_;
        switch (e.key.keysym.sym) {
            case SDLK_LEFT: offX_ -= step; break;
            case SDLK_RIGHT: offX_ += step; break;
            case SDLK_UP: offY_ -= step; break;
            case SDLK_DOWN: offY_ += step; break;
            case SDLK_EQUALS: case SDLK_PLUS: zoom_ = std::min(zoom_ * 1.25f, 4.0f); break;
            case SDLK_MINUS: zoom_ = std::max(zoom_ * 0.8f, 0.05f); break;
        }
    }
}

float MapView::minZoom(int winW, int winH) const {
    int mapW = map_.blocksX * 32, mapH = map_.blocksY * 32;
    return (mapW > 0 && mapH > 0) ? std::max(float(winW) / mapW, float(winH) / mapH) : 0.05f;
}

void MapView::clampZoom(int winW, int winH) { zoom_ = std::max(zoom_, minZoom(winW, winH)); }

void MapView::clampOffset(int winW, int winH) {
    int mapW = map_.blocksX * 32, mapH = map_.blocksY * 32;
    // Don't allow zooming out past the point where the map fills the
    // window in one dimension — otherwise the view runs off the map edges.
    if (mapW > 0 && mapH > 0) {
        float mz = std::max(float(winW) / mapW, float(winH) / mapH);
        if (zoom_ < mz) zoom_ = mz;
    }
    float maxX = mapW - winW / zoom_, maxY = mapH - winH / zoom_;
    offX_ = maxX <= 0 ? maxX / 2 : std::clamp(offX_, 0.0f, maxX);
    offY_ = maxY <= 0 ? maxY / 2 : std::clamp(offY_, 0.0f, maxY);
}

void MapView::ensureChunks(int winW, int winH) {
    clampOffset(winW, winH);
    uploadReadyChunks();   // adopt whatever the worker finished since last frame
    int c0x = int(offX_) / kChunk, c0y = int(offY_) / kChunk;
    int c1x = int(offX_ + winW / zoom_) / kChunk, c1y = int(offY_ + winH / zoom_) / kChunk;
    // Queue visible chunks first, then a one-chunk prefetch ring so scrolling
    // usually meets terrain that is already composited. The worker JPEG-decodes
    // + composites off-thread; missing chunks draw as nothing for a frame or
    // two instead of freezing the main thread (a big cold view used to stall
    // 200-400ms right as the world appeared).
    for (int cy = c0y; cy <= c1y; ++cy)
        for (int cx = c0x; cx <= c1x; ++cx)
            requestChunk(cx, cy);
    for (int cy = c0y - 1; cy <= c1y + 1; ++cy)
        for (int cx = c0x - 1; cx <= c1x + 1; ++cx)
            if (cy < c0y || cy > c1y || cx < c0x || cx > c1x)
                requestChunk(cx, cy);
    // Evict chunks well outside the visible+prefetch ring so terrain VRAM tracks the
    // working set, not the pan history (part of the GPU cap). A 3-chunk margin keeps
    // normal panning from thrashing; evicted chunks recomposite off-thread on return.
    const int M = 3;
    for (auto it = chunks_.begin(); it != chunks_.end(); ) {
        int cx = it->first.first, cy = it->first.second;
        if (cx >= c0x - 1 - M && cx <= c1x + 1 + M && cy >= c0y - 1 - M && cy <= c1y + 1 + M) { ++it; continue; }
        gpuvram::destroy(it->second);
        { std::lock_guard<std::mutex> lk(chunkMu_); chunkPending_.erase(it->first); }
        it = chunks_.erase(it);
    }
}

void MapView::finishChunks() {
    {
        std::unique_lock<std::mutex> lk(chunkMu_);
        chunkCv_.wait(lk, [this] { return chunkQueue_.empty() && !chunkBusy_; });
    }
    // Screenshot path: upload EVERYTHING now (loop past the per-frame upload budget).
    for (;;) {
        uploadReadyChunks();
        std::lock_guard<std::mutex> lk(chunkMu_);
        if (chunkDone_.empty()) break;
    }
}

void MapView::draw(int winW, int winH) {
    clampOffset(winW, winH);

    // Underlay first: stretch the overview across the whole map's screen rect. Chunks
    // draw on top at full detail; gaps between them fall back to this instead of black.
    if (underlay_) {
        int mapW = map_.blocksX * 32, mapH = map_.blocksY * 32;   // full map in world px
        int ux0 = int(std::lround((0 - offX_) * zoom_)), uy0 = int(std::lround((0 - offY_) * zoom_));
        int ux1 = int(std::lround((mapW - offX_) * zoom_)), uy1 = int(std::lround((mapH - offY_) * zoom_));
        SDL_Rect udst{ux0, uy0, ux1 - ux0, uy1 - uy0};
        SDL_RenderCopy(ren_, underlay_, nullptr, &udst);
    }

    int c0x = int(offX_) / kChunk, c0y = int(offY_) / kChunk;
    int c1x = int(offX_ + winW / zoom_) / kChunk, c1y = int(offY_ + winH / zoom_) / kChunk;
    for (int cy = c0y; cy <= c1y; ++cy)
        for (int cx = c0x; cx <= c1x; ++cx) {
            auto it = chunks_.find(std::make_pair(cx, cy));
            SDL_Texture* t = it != chunks_.end() ? it->second : nullptr;
            if (!t) continue;   // still compositing: the underlay shows through
            // Integer-rounded edges so adjacent chunks always abut.
            int x0 = int(std::lround((cx * kChunk - offX_) * zoom_));
            int y0 = int(std::lround((cy * kChunk - offY_) * zoom_));
            int x1 = int(std::lround(((cx + 1) * kChunk - offX_) * zoom_));
            int y1 = int(std::lround(((cy + 1) * kChunk - offY_) * zoom_));
            SDL_Rect dst{x0, y0, x1 - x0, y1 - y0};
            SDL_RenderCopy(ren_, t, nullptr, &dst);
        }

    drawWater(winW, winH);   // animated caustic over below-sea cells
}

void MapView::buildWaterMask() {
    waterChecked_ = true;
    hasWater_ = false;
    if (waterMask_) { gpuvram::destroy(waterMask_); waterMask_ = nullptr; }
    const int W = map_.width, H = map_.height, sea = map_.seaLevel;
    if (W <= 0 || H <= 0 || int(map_.heights.size()) < W * H) return;
    auto land = [&](int x, int z) {
        if (x < 0 || z < 0 || x >= W || z >= H) return false;
        return map_.heights[size_t(z) * W + x] >= sea;
    };
    std::vector<uint8_t> px(size_t(W) * H * 4, 0);
    for (int z = 0; z < H; ++z)
        for (int x = 0; x < W; ++x) {
            uint8_t s = 0;
            if (!land(x, z)) {                   // a water cell
                hasWater_ = true;
                int h = map_.heights[size_t(z) * W + x];
                int base = 108 + std::clamp(sea - h, 0, 60) * 42 / 60;   // 108..150 open water
                // Surf: the caustic runs BRIGHT right at the waterline and fades a few
                // cells out, so the scrolling caustic reads as foam rolling on the
                // shore. Nearest land within 3 cells (Chebyshev) sets the boost.
                int near = 9;
                for (int dz = -3; dz <= 3 && near > 1; ++dz)
                    for (int dx = -3; dx <= 3; ++dx)
                        if (land(x + dx, z + dz)) { near = std::min(near, std::max(std::abs(dx), std::abs(dz))); }
                int foam = near <= 3 ? (4 - near) * 46 : 0;   // 138 / 92 / 46 at 1 / 2 / 3 cells
                s = uint8_t(std::clamp(base + foam, 0, 255));
            }
            size_t i = (size_t(z) * W + x) * 4;
            px[i] = px[i + 1] = px[i + 2] = s; px[i + 3] = 255;
        }
    if (!hasWater_) return;
    waterMask_ = gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, W, H);
    if (!waterMask_) { hasWater_ = false; return; }
    SDL_UpdateTexture(waterMask_, nullptr, px.data(), W * 4);
    SDL_SetTextureScaleMode(waterMask_, SDL_ScaleModeLinear);   // smooth shorelines
}

void MapView::buildCaustic() {
    const int N = 256;   // seamless: every grating uses an integer frequency, period N
    std::vector<uint8_t> px(size_t(N) * N * 4);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            float u = float(x) / N * 6.2831853f, v = float(y) / N * 6.2831853f;
            float a = std::sin(u * 2 + std::cos(v * 3)) * 0.5f
                    + std::sin(v * 2 + std::cos(u * 3)) * 0.5f
                    + std::sin((u + v) * 3) * 0.35f;
            a = a * 0.5f + 0.5f;   // 0..1
            a = a * a;             // sharpen into veins
            int b = std::clamp(int(a * 190.0f), 0, 255);   // modest glint, not blinding
            size_t i = (size_t(y) * N + x) * 4;   // cyan-white glint
            px[i] = uint8_t(b * 52 / 100); px[i + 1] = uint8_t(b * 78 / 100);
            px[i + 2] = uint8_t(b);        px[i + 3] = 255;
        }
    caustic_ = gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, N, N);
    if (!caustic_) return;
    SDL_UpdateTexture(caustic_, nullptr, px.data(), N * 4);
    SDL_SetTextureScaleMode(caustic_, SDL_ScaleModeLinear);
}

void MapView::drawWater(int winW, int winH) {
    if (!waterChecked_) buildWaterMask();
    if (!hasWater_) return;                       // fully-dry map: nothing to animate
    if (!caustic_) buildCaustic();
    if (!caustic_ || !waterMask_) return;

    const int down = 2;                           // composite at half res (cheap, smooth)
    int rw = std::max(1, winW / down), rh = std::max(1, winH / down);
    if (!waterRT_ || waterRTw_ != rw || waterRTh_ != rh) {
        if (waterRT_) gpuvram::destroy(waterRT_);
        waterRT_ = gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, rw, rh);
        waterRTw_ = rw; waterRTh_ = rh;
        if (waterRT_) SDL_SetTextureScaleMode(waterRT_, SDL_ScaleModeLinear);
    }
    if (!waterRT_) return;

    SDL_Texture* prev = SDL_GetRenderTarget(ren_);
    SDL_SetRenderTarget(ren_, waterRT_);
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 0);
    SDL_RenderClear(ren_);

    const float z = zoom_ / down;                 // world px -> RT px
    const int mapW = map_.blocksX * 32, mapH = map_.blocksY * 32;   // full map, world px
    SDL_Rect mdst{int(std::lround((0 - offX_) * z)), int(std::lround((0 - offY_) * z)),
                  0, 0};
    mdst.w = int(std::lround((mapW - offX_) * z)) - mdst.x;
    mdst.h = int(std::lround((mapH - offY_) * z)) - mdst.y;
    SDL_RenderSetClipRect(ren_, &mdst);           // keep glints inside the map rect

    const float t = SDL_GetTicks() / 1000.0f;     // wall clock (display only)
    // Two caustic layers scroll opposite ways -> shifting interference = ripples.
    // Anchored to the world (pan offset) so glints track terrain, plus a slow drift.
    auto layer = [&](float wx, float wy, int tile, SDL_BlendMode bm) {
        SDL_SetTextureBlendMode(caustic_, bm);
        // Integer step so tiles abut exactly (no sub-pixel seam lines in the water).
        int ox = int(std::floor(std::fmod(wx, float(tile)))); if (ox > 0) ox -= tile;
        int oy = int(std::floor(std::fmod(wy, float(tile)))); if (oy > 0) oy -= tile;
        for (int y = oy; y < rh; y += tile)
            for (int x = ox; x < rw; x += tile) {
                SDL_Rect d{x, y, tile, tile};
                SDL_RenderCopy(ren_, caustic_, nullptr, &d);
            }
    };
    layer(-offX_ * z + t * 13, -offY_ * z + t * 8,  128,  SDL_BLENDMODE_NONE);
    layer(-offX_ * z - t * 9,  -offY_ * z + t * 11, 192,  SDL_BLENDMODE_ADD);

    // Confine the glints to water: MOD the whole-map mask over the caustic (RGB *=
    // strength -> zero on land). Stretch it to the same rect the terrain occupies.
    SDL_SetTextureBlendMode(waterMask_, SDL_BLENDMODE_MOD);
    SDL_RenderCopy(ren_, waterMask_, nullptr, &mdst);

    SDL_RenderSetClipRect(ren_, nullptr);
    SDL_SetRenderTarget(ren_, prev);
    SDL_SetTextureBlendMode(waterRT_, SDL_BLENDMODE_ADD);
    SDL_Rect full{0, 0, winW, winH};
    SDL_RenderCopy(ren_, waterRT_, nullptr, &full);
}

void MapView::setBilinear(bool b) {
    bilinear_ = b;
    for (auto& [k, t] : chunks_)
        if (t) SDL_SetTextureScaleMode(t, b ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
}

void MapView::setZoomSpeed(float m) { zoomSpeed_ = std::clamp(m, 0.25f, 4.0f); }

void MapView::requestChunk(int cx, int cy) {
    int bx0 = cx * kChunk / 32, by0 = cy * kChunk / 32;
    if (bx0 >= map_.blocksX || by0 >= map_.blocksY || cx < 0 || cy < 0) return;
    auto key = std::make_pair(cx, cy);
    if (chunks_.count(key)) return;
    std::lock_guard<std::mutex> lk(chunkMu_);
    if (!chunkPending_.insert(key).second) return;   // already queued/in flight
    chunkQueue_.push_back(key);
    chunkCv_.notify_one();
}

void MapView::uploadReadyChunks() {
    std::vector<DoneChunk> done;
    {
        std::lock_guard<std::mutex> lk(chunkMu_);
        done.swap(chunkDone_);
    }
    // Budget GPU uploads per frame: each chunk is a 512x512 (~1MB) texture create +
    // upload, and edge-scrolling finishes a whole prefetch row at once -- uploading
    // them all in one frame is a hitch that repeats every time you cross a chunk
    // boundary. Upload a few now; the leftovers wait (still marked pending, still
    // ahead of the view thanks to the one-chunk prefetch ring) for the next frames.
    constexpr size_t kUploadBudget = 2;
    size_t n = 0;
    std::vector<DoneChunk> deferred;
    for (auto& d : done) {
        auto key = std::make_pair(d.cx, d.cy);
        if (chunks_.count(key)) {   // already uploaded on an earlier frame
            std::lock_guard<std::mutex> lk(chunkMu_); chunkPending_.erase(key); continue;
        }
        if (n >= kUploadBudget) { deferred.push_back(std::move(d)); continue; }
        // Respect the GPU budget/backoff so terrain can never start the exhaustion
        // storm; a deferred chunk stays pending and uploads once there's room.
        if (gpuvram::blocked() || !gpuvram::wouldFit(size_t(kChunk) * kChunk * 4)) {
            deferred.push_back(std::move(d)); continue;
        }
        SDL_Texture* t = gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_STATIC, kChunk, kChunk);
        if (!t) { gpuvram::noteFail(); deferred.push_back(std::move(d)); continue; }
        SDL_UpdateTexture(t, nullptr, d.buf.data(), kChunk * 4);
        if (bilinear_) SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
        chunks_[key] = t;
        { std::lock_guard<std::mutex> lk(chunkMu_); chunkPending_.erase(key); }
        ++n;
    }
    if (!deferred.empty()) {   // carry the rest to the next frame (still pending)
        std::lock_guard<std::mutex> lk(chunkMu_);
        for (auto& d : deferred) chunkDone_.push_back(std::move(d));
    }
}

void MapView::chunkWorkerLoop() {
    std::unique_lock<std::mutex> lk(chunkMu_);
    for (;;) {
        chunkCv_.wait(lk, [this] { return chunkStop_ || !chunkQueue_.empty(); });
        if (chunkStop_) return;
        auto [cx, cy] = chunkQueue_.front();
        chunkQueue_.pop_front();
        chunkBusy_ = true;
        lk.unlock();
        DoneChunk d{cx, cy, std::vector<uint8_t>(size_t(kChunk) * kChunk * 4, 0)};
        int bx0 = cx * kChunk / 32, by0 = cy * kChunk / 32;
        int nb = kChunk / 32;
        for (int y = 0; y < nb; ++y)
            for (int x = 0; x < nb; ++x) {
                int bx = bx0 + x, by = by0 + y;
                if (bx >= map_.blocksX || by >= map_.blocksY) continue;
                comp_.renderBlock(map_, bx, by, d.buf, kChunk, x * 32, y * 32);
            }
        lk.lock();
        chunkBusy_ = false;
        chunkDone_.push_back(std::move(d));
        chunkCv_.notify_all();   // reload()/finishChunks() may be waiting
    }
}
