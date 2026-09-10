#include "client/mapview.h"

#include "client/gpuvram.h"
#include "hpi/hpi.h"           // tak::hpi::Vfs::read (ctor / reload)
#include "tnt/mapgen.h"        // "~gen1~" random-map ids -> procedural map

#include <algorithm>
#include <cmath>

tak::tnt::Map MapView::genOrLoad(const tak::hpi::Vfs& vfs, const std::string& mapPath) {
    if (tak::mapgen::isGeneratedMapId(mapPath))
        return tak::mapgen::generate(tak::mapgen::decodeMapId(mapPath), vfs).map;
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
