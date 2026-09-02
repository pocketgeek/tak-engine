// takview — interactive TAK asset viewer.
//
//   takview map <map.tnt> <terrain-dir>       scrollable terrain (drag/arrows,
//                                             +/- zoom, S = screenshot)
//   takview model <file.3do> [textures-dir palette.pcx]
//                                             rotating textured model
//                                             (drag to rotate, wheel zoom)
//   ... --shot <out.png>                      render one frame headless
//
// Textures dir = extracted data/textures; palette = faction palette PCX
// (e.g. palettes/ara_textures.pcx from sidedata.tdf).

#include "cob/vm.h"
#include "crt/crt.h"
#include "gaf/gaf.h"
#include "hpi/hpi.h"
#include "net/lockstep.h"
#include "sim/sim.h"
#include "tdf/tdf.h"
#include "tdo/tdo.h"
#include "terrain/terrain.h"
#include "tnt/tnt.h"
#include "util/png.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

float gTilt = 0.72f;
constexpr int kWinW = 1280, kWinH = 800;

void screenshot(SDL_Renderer* ren, int w, int h, const std::string& path) {
    std::vector<uint8_t> px(size_t(w) * h * 4);
    if (SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32, px.data(), w * 4) == 0) {
        tak::png::write(path, w, h, px);
        std::printf("screenshot: %s\n", path.c_str());
    }
}

// ---------------------------------------------------------------- map mode

class MapView {
public:
    MapView(SDL_Renderer* ren, const std::string& tntPath, const std::string& terrainDir)
        : ren_(ren), map_(tak::tnt::Map::load(tntPath)), comp_(terrainDir) {}

    void input(const SDL_Event& e) {
        if (e.type == SDL_MOUSEMOTION && (e.motion.state & SDL_BUTTON_LMASK)) {
            offX_ -= e.motion.xrel / zoom_;
            offY_ -= e.motion.yrel / zoom_;
        } else if (e.type == SDL_MOUSEWHEEL) {
            zoom_ = std::clamp(zoom_ * (e.wheel.y > 0 ? 1.25f : 0.8f), 0.05f, 4.0f);
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

    // Create any terrain chunk textures that will be visible this frame.
    // Called BEFORE the render pass so texture creation never interleaves
    // with draw calls (which glitches the whole frame on some backends).
    // Keep the camera on the map. When the view is wider/taller than the map
    // (small map, or a maximized window), center it instead of pinning it to
    // the top-left corner — pinning makes zoom-to-cursor appear to drift toward
    // (0,0).
    void clampOffset(int winW, int winH) {
        int mapW = map_.blocksX * 32, mapH = map_.blocksY * 32;
        // Don't allow zooming out past the point where the map fills the
        // window in one dimension — otherwise the view runs off the map edges.
        if (mapW > 0 && mapH > 0) {
            float minZoom = std::max(float(winW) / mapW, float(winH) / mapH);
            if (zoom_ < minZoom) zoom_ = minZoom;
        }
        float maxX = mapW - winW / zoom_, maxY = mapH - winH / zoom_;
        offX_ = maxX <= 0 ? maxX / 2 : std::clamp(offX_, 0.0f, maxX);
        offY_ = maxY <= 0 ? maxY / 2 : std::clamp(offY_, 0.0f, maxY);
    }

    void ensureChunks(int winW, int winH) {
        clampOffset(winW, winH);
        int c0x = int(offX_) / kChunk, c0y = int(offY_) / kChunk;
        int c1x = int(offX_ + winW / zoom_) / kChunk, c1y = int(offY_ + winH / zoom_) / kChunk;
        for (int cy = c0y; cy <= c1y; ++cy)
            for (int cx = c0x; cx <= c1x; ++cx)
                chunk(cx, cy);
    }

    void draw(int winW, int winH) {
        clampOffset(winW, winH);

        int c0x = int(offX_) / kChunk, c0y = int(offY_) / kChunk;
        int c1x = int(offX_ + winW / zoom_) / kChunk, c1y = int(offY_ + winH / zoom_) / kChunk;
        for (int cy = c0y; cy <= c1y; ++cy)
            for (int cx = c0x; cx <= c1x; ++cx) {
                SDL_Texture* t = chunk(cx, cy);
                if (!t) continue;
                // Integer-rounded edges so adjacent chunks always abut.
                int x0 = int(std::lround((cx * kChunk - offX_) * zoom_));
                int y0 = int(std::lround((cy * kChunk - offY_) * zoom_));
                int x1 = int(std::lround(((cx + 1) * kChunk - offX_) * zoom_));
                int y1 = int(std::lround(((cy + 1) * kChunk - offY_) * zoom_));
                SDL_Rect dst{x0, y0, x1 - x0, y1 - y0};
                SDL_RenderCopy(ren_, t, nullptr, &dst);
            }
    }

    float offX() const { return offX_; }
    float offY() const { return offY_; }
    float zoom() const { return zoom_; }
    tak::terrain::Compositor& compositor() { return comp_; }
    void setZoom(float z) { zoom_ = z; }
    void setOffset(float x, float y) { offX_ = x; offY_ = y; }
    const tak::tnt::Map& map() const { return map_; }

private:
    static constexpr int kChunk = 512;

    SDL_Texture* chunk(int cx, int cy) {
        int bx0 = cx * kChunk / 32, by0 = cy * kChunk / 32;
        if (bx0 >= map_.blocksX || by0 >= map_.blocksY || cx < 0 || cy < 0) return nullptr;
        auto key = std::make_pair(cx, cy);
        auto it = chunks_.find(key);
        if (it != chunks_.end()) return it->second;

        std::vector<uint8_t> buf(size_t(kChunk) * kChunk * 4, 0);
        int nb = kChunk / 32;
        for (int y = 0; y < nb; ++y)
            for (int x = 0; x < nb; ++x) {
                int bx = bx0 + x, by = by0 + y;
                if (bx >= map_.blocksX || by >= map_.blocksY) continue;
                comp_.renderBlock(map_, bx, by, buf, kChunk, x * 32, y * 32);
            }
        SDL_Texture* t = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_STATIC, kChunk, kChunk);
        SDL_UpdateTexture(t, nullptr, buf.data(), kChunk * 4);
        chunks_[key] = t;
        return t;
    }

    SDL_Renderer* ren_;
    tak::tnt::Map map_;
    tak::terrain::Compositor comp_;
    std::map<std::pair<int, int>, SDL_Texture*> chunks_;
    float offX_ = 0, offY_ = 0, zoom_ = 0.35f;
};

// -------------------------------------------------------------- model mode

struct Tri {
    SDL_Vertex v[3];
    SDL_Texture* tex;
    float depth;
};

// Column-major-ish 3x3 rotation + translation, composed down the piece tree.
struct Xform {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float t[3] = {0, 0, 0};

    Xform then(float ox, float oy, float oz, const float rot[3]) const {
        // Local = T(offset) * Ry * Rx * Rz
        float cx = std::cos(rot[0]), sx = std::sin(rot[0]);
        float cy = std::cos(rot[1]), sy = std::sin(rot[1]);
        float cz = std::cos(rot[2]), sz = std::sin(rot[2]);
        float r[9] = {
            cy * cz + sy * sx * sz, -cy * sz + sy * sx * cz, sy * cx,
            cx * sz, cx * cz, -sx,
            -sy * cz + cy * sx * sz, sy * sz + cy * sx * cz, cy * cx,
        };
        Xform out;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                out.m[i * 3 + j] = 0;
                for (int k = 0; k < 3; ++k)
                    out.m[i * 3 + j] += m[i * 3 + k] * r[k * 3 + j];
            }
        out.t[0] = t[0] + m[0] * ox + m[1] * oy + m[2] * oz;
        out.t[1] = t[1] + m[3] * ox + m[4] * oy + m[5] * oz;
        out.t[2] = t[2] + m[6] * ox + m[7] * oy + m[8] * oz;
        return out;
    }

    void apply(float x, float y, float z, float out[3]) const {
        out[0] = t[0] + m[0] * x + m[1] * y + m[2] * z;
        out[1] = t[1] + m[3] * x + m[4] * y + m[5] * z;
        out[2] = t[2] + m[6] * x + m[7] * y + m[8] * z;
    }
};

class ModelView {
public:
    ModelView(SDL_Renderer* ren, const std::string& path, const std::string& texDir,
              const std::string& palettePath, const std::string& cobPath,
              const std::string& anim)
        : ren_(ren), model_(tak::tdo::load(path)) {
        if (!texDir.empty() && !palettePath.empty()) loadTextures(texDir, palettePath);
        if (!cobPath.empty() && !anim.empty()) {
            vm_ = std::make_unique<tak::cob::Vm>(tak::cob::load(cobPath));
            vm_->setStatic(0, 1);   // convention: static 0 = "is walking" flag
            if (!vm_->start(anim))
                std::fprintf(stderr, "no script '%s' in %s\n", anim.c_str(),
                             cobPath.c_str());
            // Map piece numbers to lowercase object names.
            for (const auto& p : vm_->file().pieces) {
                std::string n = p;
                std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                pieceNames_.push_back(n);
            }
        }
    }

    void input(const SDL_Event& e) {
        if (e.type == SDL_MOUSEMOTION && (e.motion.state & SDL_BUTTON_LMASK)) {
            yaw_ += e.motion.xrel * 0.01f;
            pitch_ = std::clamp(pitch_ + e.motion.yrel * 0.01f, -1.4f, 1.4f);
            spin_ = false;
        } else if (e.type == SDL_MOUSEWHEEL) {
            zoom_ *= e.wheel.y > 0 ? 1.15f : 0.87f;
        }
    }

    void draw(int winW, int winH, float dt) {
        if (spin_) yaw_ += dt * 0.8f;
        if (vm_) vm_->tick(dt);

        tris_.clear();
        walk(model_.root, Xform{});
        if (tris_.empty()) return;

        // Center and fit every frame (cheap, and stays correct as it spins).
        float lox = 1e9f, hix = -1e9f, loy = 1e9f, hiy = -1e9f;
        for (auto& t : tris_)
            for (auto& v : t.v) {
                lox = std::min(lox, v.position.x); hix = std::max(hix, v.position.x);
                loy = std::min(loy, v.position.y); hiy = std::max(hiy, v.position.y);
            }
        if (!fitted_) {
            fit_ = 0.8f * std::min(winW, winH) /
                   std::max({hix - lox, hiy - loy, 1e-3f});
            fitted_ = true;
        }
        std::stable_sort(tris_.begin(), tris_.end(),
                  [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
        float s = fit_ * zoom_;
        float cx = (lox + hix) / 2, cy = (loy + hiy) / 2;
        for (auto& t : tris_) {
            SDL_Vertex v[3];
            for (int i = 0; i < 3; ++i) {
                v[i] = t.v[i];
                v[i].position.x = (v[i].position.x - cx) * s + winW / 2.0f;
                v[i].position.y = (v[i].position.y - cy) * s + winH / 2.0f;
            }
            SDL_RenderGeometry(ren_, t.tex, v, 3, nullptr, 0);
        }
    }

private:
    void loadTextures(const std::string& texDir, const std::string& palettePath) {
        auto pal = tak::gaf::Palette::load(palettePath);
        for (const auto& e : std::filesystem::directory_iterator(texDir)) {
            if (e.path().extension() != ".gaf") continue;
            try {
                for (auto& seq : tak::gaf::load(e.path(), pal, 5)) {
                    if (seq.frames.empty()) continue;
                    auto& f = seq.frames[0];
                    if (f.width == 0 || f.height == 0) continue;
                    std::string name = seq.name;
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    if (textures_.count(name)) continue;
                    SDL_Texture* t = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                                       SDL_TEXTUREACCESS_STATIC,
                                                       f.width, f.height);
                    SDL_UpdateTexture(t, nullptr, f.rgba.data(), f.width * 4);
                    textures_[name] = t;
                }
            } catch (const std::exception&) { /* skip odd banks */ }
        }
        std::printf("loaded %zu textures\n", textures_.size());
    }

    void project(float x, float y, float z, SDL_FPoint& out, float& depth) const {
        float cx = std::cos(yaw_), sx = std::sin(yaw_);
        float rx = x * cx + z * sx;
        float rz = -x * sx + z * cx;
        float cy = std::cos(pitch_), sy = std::sin(pitch_);
        float ry = y * cy - rz * sy;
        depth = rz * cy + y * sy;
        out = {rx, -ry};
    }

    const tak::cob::PieceState* pieceFor(const std::string& objName) const {
        if (!vm_) return nullptr;
        std::string n = objName;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        for (size_t i = 0; i < pieceNames_.size(); ++i)
            if (pieceNames_[i] == n) return &vm_->pieces()[i];
        return nullptr;
    }

    void walk(const tak::tdo::Object& o, const Xform& parent) {
        static const float kNoRot[3] = {0, 0, 0};
        const tak::cob::PieceState* ps = pieceFor(o.name);
        if (ps && !ps->visible) return;
        Xform xf = parent.then(o.x + (ps ? ps->move[0] : 0),
                               o.y + (ps ? ps->move[1] : 0),
                               o.z + (ps ? ps->move[2] : 0),
                               ps ? ps->rot : kNoRot);
        for (const auto& p : o.primitives) {
            if (p.indices.size() < 3) continue;
            SDL_Texture* tex = nullptr;
            if (!p.texture.empty()) {
                std::string name = p.texture;
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                auto it = textures_.find(name);
                if (it != textures_.end()) tex = it->second;
            }
            // Fan-triangulate; quads get proper corner UVs.
            for (size_t i = 1; i + 1 < p.indices.size(); ++i) {
                size_t idx[3] = {0, i, i + 1};
                Tri tri{};
                tri.tex = tex;
                float depth = 0;
                for (int k = 0; k < 3; ++k) {
                    size_t vi = size_t(p.indices[idx[k]]) * 3;
                    if (vi + 2 >= o.vertices.size()) { tri.tex = nullptr; break; }
                    float w[3], d;
                    xf.apply(o.vertices[vi], o.vertices[vi + 1], o.vertices[vi + 2], w);
                    project(w[0], w[1], w[2], tri.v[k].position, d);
                    depth += d;
                    static const SDL_FPoint uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                    tri.v[k].tex_coord = uv[idx[k] & 3];
                    tri.v[k].color = tex ? SDL_Color{255, 255, 255, 255}
                                         : SDL_Color{170, 170, 180, 255};
                }
                tri.depth = depth / 3;
                tris_.push_back(tri);
            }
        }
        for (const auto& c : o.children) walk(c, xf);
    }

public:
    void advance(float seconds) {
        if (!vm_) return;
        for (float t = 0; t < seconds; t += 1.0f / 30.0f) vm_->tick(1.0f / 30.0f);
    }

private:
    SDL_Renderer* ren_;
    tak::tdo::Model model_;
    std::unique_ptr<tak::cob::Vm> vm_;
    std::vector<std::string> pieceNames_;
    std::map<std::string, SDL_Texture*> textures_;
    std::vector<Tri> tris_;
    float yaw_ = 0.7f, pitch_ = 0.4f, zoom_ = 1.0f, fit_ = 1.0f;
    bool spin_ = true, fitted_ = false;
};

// Minimal 8-channel WAV mixer over an SDL audio device (the game's WAVs are
// 11025 Hz 8-bit mono). Failing to open audio is non-fatal: play() no-ops.
class SoundBank {
public:
    void init(const std::string& soundsDir, bool verbose) {
        verbose_ = verbose;
        try {
            for (const auto& e : std::filesystem::directory_iterator(soundsDir)) {
                std::string stem = e.path().stem().string();
                std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);
                index_[stem] = e.path().string();
            }
        } catch (const std::exception&) { return; }

        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
        SDL_AudioSpec want{};
        want.freq = 11025;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 1024;
        want.callback = &SoundBank::mixThunk;
        want.userdata = this;
        dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &spec_, 0);
        if (dev_) SDL_PauseAudioDevice(dev_, 0);
    }

    // Override built-in sounds with WAVs from an HPI archive (e.g. a
    // click.hpi that replaces the faction order tones). Decoded straight
    // into the cache so they win over the on-disk originals.
    void loadHpiOverrides(const std::filesystem::path& hpiPath) {
        if (!std::filesystem::exists(hpiPath)) return;
        try {
            tak::hpi::Archive ar(hpiPath);
            int n = 0;
            for (const auto& e : ar.entries()) {
                if (e.isDirectory) continue;
                std::string ext = std::filesystem::path(e.path).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext != ".wav") continue;
                std::string stem = std::filesystem::path(e.path).stem().string();
                std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);
                auto bytes = ar.read(e);
                if (auto pcm = decodeWav(bytes.data(), bytes.size())) {
                    cache_[stem] = std::move(*pcm);
                    ++n;
                }
            }
            std::fprintf(stderr, "sound overrides: %d from %s\n", n,
                         hpiPath.string().c_str());
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "sound override load: %s\n", ex.what());
        }
    }

    bool has(const std::string& name) const {
        std::string n = name;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        return index_.count(n) != 0;
    }

    void setVerbose(bool v) { verbose_ = v; }

    void play(const std::string& name) {
        std::string n = name;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        auto it = index_.find(n);
        if (it == index_.end()) return;
        if (verbose_) std::printf("SND %s\n", n.c_str());
        const auto* samples = load(n, it->second);
        if (!samples || !dev_) return;
        SDL_LockAudioDevice(dev_);
        for (auto& c : channels_)
            if (c.pos >= (c.data ? c.data->size() : 0)) {
                c.data = samples;
                c.pos = 0;
                break;
            }
        SDL_UnlockAudioDevice(dev_);
    }

private:
    struct Channel {
        const std::vector<int16_t>* data = nullptr;
        size_t pos = 0;
    };

    // Decode a WAV (from memory) to the mixer's 11025 Hz mono S16 format.
    std::optional<std::vector<int16_t>> decodeWav(const uint8_t* data, size_t size) {
        SDL_AudioSpec spec{};
        Uint8* buf = nullptr;
        Uint32 len = 0;
        SDL_RWops* rw = SDL_RWFromConstMem(data, int(size));
        if (!rw || !SDL_LoadWAV_RW(rw, 1, &spec, &buf, &len)) return std::nullopt;
        SDL_AudioCVT cvt;
        if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS,
                              1, 11025) < 0) {
            SDL_FreeWAV(buf);
            return std::nullopt;
        }
        std::vector<uint8_t> work(size_t(len) * size_t(std::max(cvt.len_mult, 1)));
        std::memcpy(work.data(), buf, len);
        SDL_FreeWAV(buf);
        cvt.buf = work.data();
        cvt.len = int(len);
        if (cvt.needed && SDL_ConvertAudio(&cvt) != 0) return std::nullopt;
        size_t outBytes = cvt.needed ? size_t(cvt.len_cvt) : len;
        std::vector<int16_t> out(outBytes / 2);
        std::memcpy(out.data(), work.data(), out.size() * 2);
        return out;
    }

    const std::vector<int16_t>* load(const std::string& key, const std::string& path) {
        auto it = cache_.find(key);
        if (it != cache_.end()) return &it->second;
        std::ifstream f(path, std::ios::binary);
        if (!f) return nullptr;
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
        auto pcm = decodeWav(bytes.data(), bytes.size());
        if (!pcm) return nullptr;
        return &cache_.emplace(key, std::move(*pcm)).first->second;
    }

    static void mixThunk(void* ud, Uint8* stream, int len) {
        static_cast<SoundBank*>(ud)->mix(reinterpret_cast<int16_t*>(stream), len / 2);
    }

    void mix(int16_t* out, int n) {
        std::memset(out, 0, size_t(n) * 2);
        // Background music bed (quieter than SFX).
        for (int i = 0; i < n; ++i) {
            if (musicPos_ >= music_.size()) { musicDone_ = true; break; }
            int v = out[i] + (music_[musicPos_++] * musicVol_) / 256;
            out[i] = int16_t(std::clamp(v, -32768, 32767));
        }
        for (auto& c : channels_) {
            if (!c.data) continue;
            for (int i = 0; i < n && c.pos < c.data->size(); ++i, ++c.pos) {
                int v = out[i] + (*c.data)[c.pos] / 2;
                out[i] = int16_t(std::clamp(v, -32768, 32767));
            }
        }
    }

public:
    // Begin playing a shuffled playlist of the given track numbers (a
    // faction's tracks, per sidedata.tdf). Empty = all 20. `dataRoot` is
    // the extracted data dir; music may live there or in the game install.
    void startMusic(const std::string& dataRoot, const std::vector<int>& tracks) {
        const std::string cands[] = {
            dataRoot + "/../Music", dataRoot + "/../music",
            dataRoot + "/../../game/Music", dataRoot + "/../../game/music",
            dataRoot + "/Music",
        };
        std::string dir;
        for (const auto& c : cands)
            if (std::filesystem::is_directory(c)) { dir = c; break; }
        if (dir.empty()) {
            std::fprintf(stderr, "music: no Music/ directory found\n");
            return;
        }
        std::vector<int> want = tracks;
        if (want.empty())
            for (int i = 1; i <= 20; ++i) want.push_back(i);
        for (int n : want) {
            std::string path = dir + "/track" + std::to_string(n) + ".wav";
            if (std::filesystem::exists(path)) playlist_.push_back(path);
        }
        std::fprintf(stderr, "music: %zu faction tracks, audio=%s\n", playlist_.size(),
                     dev_ ? "yes" : "NO DEVICE");
        if (!playlist_.empty() && dev_) {
            std::shuffle(playlist_.begin(), playlist_.end(),
                         std::mt19937{std::random_device{}()});
            loadTrack(0);
        }
    }

    // Advance to the next track when the current one finishes (call per frame).
    void pollMusic() {
        if (musicDone_ && !playlist_.empty()) {
            musicDone_ = false;
            loadTrack((musicTrack_ + 1) % playlist_.size());
        }
    }

    void setMusicVolume(int v) { musicVol_ = std::clamp(v, 0, 256); }

private:
    void loadTrack(size_t idx) {
        SDL_AudioSpec spec{};
        Uint8* buf = nullptr;
        Uint32 len = 0;
        if (!SDL_LoadWAV(playlist_[idx].c_str(), &spec, &buf, &len)) {
            std::fprintf(stderr, "music: LoadWAV failed: %s\n", SDL_GetError());
            return;
        }
        SDL_AudioCVT cvt;
        if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS,
                              1, 11025) < 0) {
            std::fprintf(stderr, "music: BuildAudioCVT failed: %s\n", SDL_GetError());
            SDL_FreeWAV(buf);
            return;
        }
        std::vector<uint8_t> work(size_t(len) * size_t(std::max(cvt.len_mult, 1)));
        std::memcpy(work.data(), buf, len);
        SDL_FreeWAV(buf);
        cvt.buf = work.data();
        cvt.len = int(len);
        if (cvt.needed && SDL_ConvertAudio(&cvt) != 0) {
            std::fprintf(stderr, "music: ConvertAudio failed: %s\n", SDL_GetError());
            return;
        }
        size_t outBytes = cvt.needed ? size_t(cvt.len_cvt) : len;
        std::vector<int16_t> pcm(outBytes / 2);
        std::memcpy(pcm.data(), work.data(), pcm.size() * 2);
        SDL_LockAudioDevice(dev_);
        music_ = std::move(pcm);
        musicPos_ = 0;
        musicTrack_ = idx;
        musicDone_ = false;
        SDL_UnlockAudioDevice(dev_);
        std::fprintf(stderr, "music: now playing %s\n", playlist_[idx].c_str());
    }

    std::map<std::string, std::string> index_;
    std::map<std::string, std::vector<int16_t>> cache_;
    Channel channels_[8];
    std::vector<std::string> playlist_;
    std::vector<int16_t> music_;
    size_t musicPos_ = 0, musicTrack_ = 0;
    int musicVol_ = 90;   // out of 256
    bool musicDone_ = false;
    SDL_AudioDeviceID dev_ = 0;
    SDL_AudioSpec spec_{};
    bool verbose_ = false;
};

// Sound classes: gamedata/soundclasses/*.tdf map a class to event ->
// candidate WAV names.
class SoundClasses {
public:
    void load(const std::string& dir) {
        try {
            for (const auto& e : std::filesystem::directory_iterator(dir)) {
                if (e.path().extension() != ".tdf") continue;
                try {
                    auto root = tak::tdf::parse(e.path());
                    for (const auto& clsName : root.childOrder) {
                        auto& cls = classes_[clsName];
                        const auto& node = root.children.at(clsName);
                        for (const auto& evName : node.childOrder) {
                            auto& list = cls[evName];
                            for (const auto& [wav, weight] : node.children.at(evName).values)
                                list.push_back(wav);
                        }
                    }
                } catch (const std::exception&) {}
            }
        } catch (const std::exception&) {}
    }

    const std::string* pick(const std::string& cls, const std::string& event,
                            uint32_t salt) const {
        auto ci = classes_.find(cls);
        if (ci == classes_.end()) return nullptr;
        auto ei = ci->second.find(event);
        if (ei == ci->second.end() || ei->second.empty()) return nullptr;
        return &ei->second[salt % ei->second.size()];
    }

private:
    std::map<std::string, std::map<std::string, std::vector<std::string>>> classes_;
};

// TAK GAF bitmap font: 256 glyph frames, one per codepoint, palette from
// the same-stem 1x1 PCX.
class Font {
public:
    Font() = default;
    Font(SDL_Renderer* ren, const std::filesystem::path& gafPath) {
        auto pcx = gafPath;
        pcx.replace_extension(".pcx");
        auto pal = tak::gaf::Palette::load(pcx);
        auto seqs = tak::gaf::load(gafPath, pal);
        if (seqs.empty()) return;
        auto& frames = seqs[0].frames;
        for (size_t i = 0; i < frames.size() && i < 256; ++i) {
            auto& f = frames[i];
            Glyph g;
            g.w = f.width;
            g.h = f.height;
            g.yoff = f.yoff;
            if (f.width > 0 && f.height > 0) {
                g.tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                          SDL_TEXTUREACCESS_STATIC, f.width, f.height);
                SDL_UpdateTexture(g.tex, nullptr, f.rgba.data(), f.width * 4);
                SDL_SetTextureBlendMode(g.tex, SDL_BLENDMODE_BLEND);
            }
            glyphs_[i] = g;
        }
        ok_ = true;
    }

    bool ok() const { return ok_; }

    int width(const std::string& text, float scale = 1) const {
        float x = 0;
        for (unsigned char c : text) x += advance(glyphs_[c]) * scale;
        return int(x);
    }

    void draw(SDL_Renderer* ren, const std::string& text, float x, float y,
              float scale = 1, SDL_Color tint = {255, 255, 255, 255}) const {
        for (unsigned char c : text) {
            const Glyph& g = glyphs_[c];
            if (g.tex) {
                SDL_SetTextureColorMod(g.tex, tint.r, tint.g, tint.b);
                SDL_FRect dst{x, y - g.yoff * scale, g.w * scale, g.h * scale};
                SDL_RenderCopyF(ren, g.tex, nullptr, &dst);
            }
            x += advance(g) * scale;
        }
    }

private:
    struct Glyph {
        SDL_Texture* tex = nullptr;
        int w = 0, h = 0, yoff = 0;
    };
    static float advance(const Glyph& g) { return g.w > 0 ? float(g.w + 2) : 4.0f; }
    Glyph glyphs_[256] = {};
    bool ok_ = false;
};

// --------------------------------------------------------------- game mode

// Units simulated on a real map: left-click select, right-click move order
// (shift queues waypoints), arrows scroll, wheel zoom.
class GameView {
public:
    struct FactionKit {
        const char* monarch;   // hero commander the faction starts with
        const char* keep;      // production building the monarch builds first
        const char* lode;
        const char* builder;
        const char* squad[4];
    };
    static const FactionKit& kit(const std::string& side) {
        static const std::map<std::string, FactionKit> kits = {
            {"ara", {"araking", "arakeep", "aralode", "arabuild",
                     {"araarch", "araarch", "arasword", "arasword"}}},
            {"tar", {"tarnecro", "tardung", "tarlode", "tarnecro",
                     {"tararch", "tararch", "tardemon", "tartb"}}},
            {"ver", {"vermage", "verkeep", "verlode", "verliege",
                     {"verarch", "verarch", "versword", "versword"}}},
            {"zon", {"zonhunt", "", "zonlode", "zonhand",
                     {"zongob", "zonter", "zontroll", "zonbat"}}},
            {"cre", {"cresage", "creacad", "crelode", "cremech",
                     {"creauto", "creauto", "crebeas", "creshoc"}}},
        };
        auto it = kits.find(side);
        return it != kits.end() ? it->second : kits.at("ara");
    }

    // Player start positions from the map's sibling .ota, in world pixels,
    // ordered StartPos1, StartPos2, …. OTA coordinates are in 16px cells.
    static std::vector<std::pair<float, float>> parseStartPositions(
        const std::string& tntPath) {
        std::vector<std::pair<float, float>> out;
        std::filesystem::path ota = tntPath;
        ota.replace_extension(".ota");
        if (!std::filesystem::exists(ota)) return out;
        try {
            auto root = tak::tdf::parse(ota);
            const auto* gh = root.child("globalheader");
            const auto* md = gh ? gh->child("map data") : nullptr;
            const auto* sp = md ? md->child("specials") : nullptr;
            if (!sp) return out;
            std::map<int, std::pair<float, float>> byIndex;
            for (const auto& name : sp->childOrder) {
                const auto* s = sp->child(name);
                if (!s) continue;
                std::string what = s->valueOr("specialwhat", "");
                if (what.rfind("StartPos", 0) != 0 &&
                    what.rfind("startpos", 0) != 0)
                    continue;
                int n = std::atoi(what.c_str() + 8);
                if (n <= 0) continue;
                byIndex[n] = {float(s->numberOr("xpos", 0) * 16),
                              float(s->numberOr("zpos", 0) * 16)};
            }
            for (auto& [n, pos] : byIndex) out.push_back(pos);
        } catch (const std::exception&) {}
        return out;
    }

    GameView(SDL_Renderer* ren, const std::string& tntPath, const std::string& terrainDir,
             const std::string& dataRoot, bool demo, bool scenario, bool mission,
             bool bare, const std::string& side = "ara", const std::string& aiSide = "tar")
        // (side_ initialized below before loadPanel uses it)
        : ren_(ren), mapView_(ren, tntPath, terrainDir), dataRoot_(dataRoot),
          side_(side) {
        registry_.loadDir(dataRoot_ + "/units");
        registry_.loadBuildTree(dataRoot_ + "/canbuild");
        ipRoot_ = dataRoot_ + "/../IPData";
        if (std::filesystem::exists(ipRoot_ + "/units")) {
            registry_.loadDir(ipRoot_ + "/units");
            if (std::filesystem::exists(ipRoot_ + "/canbuild"))
                registry_.loadBuildTree(ipRoot_ + "/canbuild");
        } else {
            ipRoot_.clear();
        }
        loadTextures(dataRoot_ + "/textures", dataRoot_ + "/palettes/ara_textures.pcx");
        if (!ipRoot_.empty() && std::filesystem::exists(ipRoot_ + "/textures"))
            loadTextures(ipRoot_ + "/textures", ipRoot_ + "/palettes/cre_textures.pcx");
        mapView_.setZoom(0.9f);
        try {
            hudFont_ = Font(ren_, dataRoot_ + "/fonts/bodfontbody.gaf");
            bigFont_ = Font(ren_, dataRoot_ + "/fonts/font48.gaf");
        } catch (const std::exception& e) {
            std::fprintf(stderr, "font load: %s\n", e.what());
        }
        loadOrderButtons();
        loadBuildFx();
        sounds_.init(dataRoot_ + "/../english/Sounds", false);
        // Bundled sound override (repo overrides/), then any user override
        // archives dropped next to the data or at the working dir.
        for (const std::string cand : {std::string("overrides/click.hpi"),
                                       dataRoot_ + "/../../overrides/click.hpi",
                                       dataRoot_ + "/click.hpi",
                                       std::string("click.hpi")})
            if (std::filesystem::exists(cand)) { sounds_.loadHpiOverrides(cand); break; }
        sounds_.startMusic(dataRoot_, factionMusicTracks(side_));
        soundClasses_.load(dataRoot_ + "/gamedata/soundclasses");
        loadPanel(side_);

        if (mission) {
            world_.setTerrain(mapView_.map().heights, mapView_.map().width,
                              mapView_.map().height, mapView_.map().seaLevel);
            std::filesystem::path otaPath = tntPath;
            otaPath.replace_extension(".ota");
            try {
                auto ota = tak::tdf::parse(otaPath);
                const auto* gh = ota.child("globalheader");
                const auto* md = gh ? gh->child("map data") : nullptr;
                const auto* units = md ? md->child("units") : nullptr;
                std::printf("mission: %s\n",
                            gh ? gh->valueOr("missiondescription", "").c_str() : "");
                int n = 0;
                float cx = 0, cz = 0;
                int pc = 0;
                if (units)
                    for (const auto& key : units->childOrder) {
                        const auto& u = units->children.at(key);
                        std::string id = u.valueOr("unitname", "");
                        std::transform(id.begin(), id.end(), id.begin(), ::tolower);
                        int player = int(u.numberOr("player", 1));
                        float x = float(u.numberOr("xpos", 0)) * 16 + 8;
                        float z = float(u.numberOr("zpos", 0)) * 16 + 8;
                        int team = std::clamp(player - 1, 0, 3);
                        int uid = spawn(id, x, z, 3.14159f, team);
                        if (uid >= 0) {
                            ++n;
                            float hpp = float(u.numberOr("healthpercentage", 100));
                            if (auto* su = world_.unit(uid)) {
                                su->hp *= hpp / 100.0f;
                                if (su->type->canMove &&
                                    su->type->domain ==
                                        tak::sim::UnitType::Domain::Ground)
                                    reinfPool_[team].push_back(id);
                            }
                            if (team == 0) { cx += x; cz += z; ++pc; }
                        }
                    }
                std::printf("mission: %d units spawned\n", n);
                if (pc) mapView_.setOffset(cx / float(pc) - 640 / 0.9f,
                                           cz / float(pc) - 400 / 0.9f);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "mission load: %s\n", e.what());
            }
            aiEnabled_ = false;   // no wave AI; guards fight via auto-acquire
            loadFeatures();
            // Mission scripts: run the authentic COB event handlers.
            try {
                std::filesystem::path cobPath = tntPath;
                cobPath.replace_extension(".cob");
                std::filesystem::path tdfPath = tntPath;
                tdfPath.replace_extension(".tdf");
                auto roster = tak::tdf::parse(tdfPath);
                for (const auto& name : roster.childOrder) {
                    std::string n = name;
                    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                    missionRoster_.push_back(n);
                    if (n == "verat" || n == "araat" || n == "tarat" || n == "zonat")
                        missionTowerIdx_ = int(missionRoster_.size()) - 1;
                }
                missionVm_ = std::make_unique<tak::cob::Vm>(tak::cob::load(cobPath));
                missionVm_->onMapCommand = [this](int sub, const std::vector<int32_t>& a)
                    -> int32_t { return mapCommand(sub, a); };
                missionVm_->onGet = [this](int32_t valId, const std::vector<int32_t>& a)
                    -> int32_t {
                    if (valId == 30 && !a.empty()) {
                        int idx = rosterIndexOf(a[0]);
                        if (trace_) {
                            static int lg = 0;
                            if (lg++ < 8)
                                std::printf("GET30 unit=%d -> roster %d (tower=%d)\n",
                                            a[0], idx, missionTowerIdx_);
                        }
                        return idx;
                    }
                    return 0;
                };
                missionVm_->onSetUnitValue = [this](int32_t valId, int32_t value) {
                    if (valId == 2 && value == 1 && outcome_ == 0) outcome_ = 1;
                };
                missionVm_->start("Start");
                std::printf("mission scripts: running\n");
                std::filesystem::path txtPath = tntPath;
                txtPath.replace_extension(".txt");
                std::ifstream bf(txtPath);
                std::string line;
                while (std::getline(bf, line) && briefing_.size() < 8) {
                    std::string clean;
                    for (char c : line)
                        if (uint8_t(c) >= 32 && uint8_t(c) < 127) clean += c;
                    while (!clean.empty() && clean.back() == ' ') clean.pop_back();
                    if (clean.empty()) continue;
                    // Wrap to ~54 chars per line for the panel.
                    std::string cur = "- ";
                    std::istringstream ws(clean);
                    std::string word;
                    while (ws >> word) {
                        if (cur.size() + word.size() > 42) {
                            briefing_.push_back(cur);
                            cur = "  ";
                        }
                        cur += word + " ";
                    }
                    if (cur.size() > 2) briefing_.push_back(cur);
                }
                if (briefing_.size() > 10) briefing_.resize(10);
                briefTimer_ = 30;
            } catch (const std::exception& e) {
                std::fprintf(stderr, "mission cob: %s\n", e.what());
            }
            return;
        }

        if (scenario) {
            world_.setTerrain(mapView_.map().heights, mapView_.map().width,
                              mapView_.map().height, mapView_.map().seaLevel);
            std::filesystem::path crtPath = tntPath;
            crtPath.replace_extension(".crt");
            if (!std::filesystem::exists(crtPath)) crtPath.replace_extension(".CRT");
            auto placements = tak::crt::load(crtPath);
            std::printf("scenario: %zu placements\n", placements.size());
            float cx = 0, cz = 0;
            int n = 0;
            for (const auto& p : placements) {
                std::string id = p.name;
                std::transform(id.begin(), id.end(), id.begin(), ::tolower);
                int team = std::clamp(p.player, 0, 3);
                if (spawn(id, p.x, p.z, 3.14159f, team) >= 0 && team == 0) {
                    cx += p.x; cz += p.z; ++n;
                }
            }
            if (n) mapView_.setOffset(cx / float(n) - 640 / 0.9f,
                                      cz / float(n) - 400 / 0.9f);
            aiEnabled_ = false;   // placements only; auto-acquire still fights
            loadFeatures();

            // Skirmish victory rule from the map's trigger section: a scoring
            // unit type, a scoring region, and a time limit.
            auto trig = tak::crt::loadTriggers(crtPath);
            // Scoring rule comes from an op-13 record (score-count of a unit
            // type in a region); the time limit is the first op-1 timer that
            // follows it. Maps without op 13 (pure last-alive arenas) get no
            // scoring rule.
            bool sawScore = false;
            for (const auto& rec : trig.records) {
                if (!sawScore && rec.op() == 13 && rec.slots.size() == 2) {
                    std::string lo = rec.slots[0];
                    std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
                    scenUnit_ = registry_.find(lo);
                    if (!scenUnit_) continue;
                    bool found = false;
                    for (const auto& r : trig.regions)
                        if (r.name == rec.slots[1]) {
                            scenRegion_ = r;
                            found = true;
                        }
                    if (!found) {   // e.g. 'Anywhere': the whole map
                        scenRegion_.name = rec.slots[1];
                        scenRegion_.x1 = 0;
                        scenRegion_.z1 = 0;
                        scenRegion_.x2 = mapView_.map().width;
                        scenRegion_.z2 = mapView_.map().height;
                    }
                    sawScore = true;
                } else if (sawScore && scenTime_ <= 0 && rec.op() == 1 &&
                           rec.slots.size() == 1 &&
                           rec.slots[0].find_first_not_of("0123456789") ==
                               std::string::npos) {
                    int v = std::atoi(rec.slots[0].c_str());
                    if (v >= 60 && v <= 7200) scenTime_ = float(v);
                }
            }
            if (scenUnit_ && scenTime_ > 0 && !scenRegion_.name.empty())
                std::printf("scenario rule: most %s in '%s' after %.0fs\n",
                            scenUnit_->name.c_str(), scenRegion_.name.c_str(),
                            scenTime_);

            // Trigger-record rules: timed spawns (op 1 sets the time
            // context, following op 7s spawn then), maintain-count
            // respawns (op 16 then op 7s), and timed player messages.
            auto regionOf = [&](const std::string& nm) -> const tak::crt::Region* {
                for (const auto& r : trig.regions)
                    if (r.name == nm) return &r;
                return nullptr;
            };
            float timeCtx = 0;
            int maintainN = 0;
            std::string maintainType, maintainRegion;
            for (const auto& rec : trig.records) {
                int op = rec.op();
                if (op == 1 && rec.slots.size() == 1 &&
                    rec.slots[0].find_first_not_of("0123456789") == std::string::npos) {
                    timeCtx = float(std::atoi(rec.slots[0].c_str()));
                    maintainN = 0;
                } else if (op == 16 && rec.slots.size() == 3) {
                    maintainN = std::atoi(rec.slots[0].c_str());
                    maintainType = rec.slots[1];
                    maintainRegion = rec.slots[2];
                } else if (op == 7 && rec.slots.size() == 2) {
                    std::string ty = rec.slots[0];
                    std::transform(ty.begin(), ty.end(), ty.begin(), ::tolower);
                    const auto* rg = regionOf(rec.slots[1]);
                    if (!registry_.find(ty) || !rg) continue;
                    SpawnRule sr;
                    sr.type = ty;
                    sr.x = float(rg->x1 + rg->x2) * 8;
                    sr.z = float(rg->z1 + rg->z2) * 8;
                    // Spawns into a "Player N" zone belong to that player.
                    sr.team = 3;
                    if (rec.slots[1].size() >= 8 &&
                        (rec.slots[1][0] == 'P' || rec.slots[1][0] == 'p'))
                        sr.team = std::clamp(rec.slots[1].back() - '1', 0, 3);
                    if (maintainN > 0) {
                        sr.maintainCount = maintainN;
                        std::string mt = maintainType;
                        std::transform(mt.begin(), mt.end(), mt.begin(), ::tolower);
                        sr.maintainType = mt;
                        if (const auto* mr = regionOf(maintainRegion)) sr.maintainRect = *mr;
                        else {
                            sr.maintainRect.x1 = 0;
                            sr.maintainRect.z1 = 0;
                            sr.maintainRect.x2 = mapView_.map().width;
                            sr.maintainRect.z2 = mapView_.map().height;
                        }
                    } else {
                        sr.atTime = timeCtx;
                    }
                    spawnRules_.push_back(std::move(sr));
                } else if (rec.slots.size() == 2 &&
                           rec.slots[0].rfind("Player", 0) == 0 &&
                           rec.slots[1].size() > 10) {
                    messages_.push_back({timeCtx, rec.slots[1]});
                }
            }
            if (!spawnRules_.empty())
                std::printf("scenario: %zu trigger spawn rules, %zu messages\n",
                            spawnRules_.size(), messages_.size());
            return;
        }

        world_.setTerrain(mapView_.map().heights, mapView_.map().width,
                          mapView_.map().height, mapView_.map().seaLevel);
        loadFeatures();
        float cx = mapView_.map().blocksX * 16.0f, cz = mapView_.map().blocksY * 16.0f;

        // Each side starts with only its Monarch, dropped on the map's real
        // start positions (from the .ota). Pick the two furthest-apart spots
        // so the player and the AI begin on opposite sides.
        auto starts = parseStartPositions(tntPath);
        float px = cx - 260, pz = cz + 30;   // fallbacks near map center
        float ax = cx + 300, az = cz + 30;
        if (starts.size() >= 2) {
            size_t bi = 0, bj = 1;
            float bestD = -1;
            for (size_t i = 0; i < starts.size(); ++i)
                for (size_t j = i + 1; j < starts.size(); ++j) {
                    float dx = starts[i].first - starts[j].first;
                    float dz = starts[i].second - starts[j].second;
                    if (dx * dx + dz * dz > bestD) { bestD = dx * dx + dz * dz; bi = i; bj = j; }
                }
            px = starts[bi].first;  pz = starts[bi].second;
            ax = starts[bj].first;  az = starts[bj].second;
        } else if (starts.size() == 1) {
            px = starts[0].first; pz = starts[0].second;
        }
        // Camera opens on the player's Monarch.
        mapView_.setOffset(px - 640 / 0.9f, pz - 400 / 0.9f);
        if (!bare) {
        const FactionKit& pk = kit(side);
        const FactionKit& ak = kit(aiSide);
        aiCycle_ = {ak.squad[0], ak.squad[1], ak.squad[2], ak.squad[3]};
        aiKeepType_ = ak.keep;
        aiLodeType_ = ak.lode;
        aiBuilderType_ = ak.builder;
        // Monarchs face one another.
        float pFace = std::atan2(ax - px, az - pz);
        float aFace = std::atan2(px - ax, pz - az);
        playerMonarchId_ = spawn(pk.monarch, px, pz, pFace, 0);
        builderId_ = playerMonarchId_;
        aiMonarchId_ = spawn(ak.monarch, ax, az, aFace, 1);
        // Enough mogrium to bootstrap the opening: a handful of lodestones for
        // income and the start of a keep, without being able to skip economy
        // and rush one to completion.
        world_.team(0).mana = 2800;
        world_.team(1).mana = 2800;
        if (demo) {
            // Showcase: skip the slow build-up and pit two ready armies at the
            // start positions against each other.
            std::vector<int> teamA, teamB;
            for (int i = 0; i < 6; ++i) {
                int a = spawn(pk.squad[i % 4], px + float(i % 2) * 26,
                              pz - 60 + float(i / 2) * 30, pFace, 0);
                int b = spawn(ak.squad[i % 4], ax + float(i % 2) * 26,
                              az - 60 + float(i / 2) * 30, aFace, 1);
                if (a >= 0) teamA.push_back(a);
                if (b >= 0) teamB.push_back(b);
            }
            if (pk.keep[0]) keepId_ = spawn(pk.keep, px, pz + 60, pFace, 0);
            if (ak.keep[0]) aiKeepId_ = spawn(ak.keep, ax, az + 60, aFace, 1);
            if (!teamA.empty() && !teamB.empty()) {
                for (size_t k = 0; k < teamA.size(); ++k)
                    world_.attack(teamA[k], teamB[k % teamB.size()], false);
                for (size_t k = 0; k < teamB.size(); ++k)
                    world_.attack(teamB[k], teamA[k % teamA.size()], false);
            }
            demoAi_ = true;
        }
        }

        for (auto& u : world_.units()) {
            if (!u.type || u.type->canMove) continue;
            tak::sim::blockFootprint(world_.nav(), *u.type, u.x, u.z, true);
        }
    }

    void input(const SDL_Event& e, int winW, int winH) {
        winW_ = winW;
        winH_ = winH;
        float zm = mapView_.zoom();
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE &&
            (placing_ || pendingCmd_)) {
            placing_ = nullptr;
            pendingCmd_ = 0;
        } else if (e.type == SDL_KEYDOWN && handleKey(e.key.keysym.sym,
                                                       SDL_GetModState())) {
            // handled by the hotkey dispatcher
        } else if (e.type == SDL_MOUSEWHEEL) {
            // Zoom toward the cursor: keep the world point under the mouse fixed.
            float wx = mapView_.offX() + mouseX_ / mapView_.zoom();
            float wz = mapView_.offY() + mouseY_ / mapView_.zoom();
            mapView_.input(e);   // applies the clamped zoom step
            float nz = mapView_.zoom();
            mapView_.setOffset(wx - mouseX_ / nz, wz - mouseY_ / nz);
        } else if (e.type == SDL_KEYDOWN) {
            mapView_.input(e);
        } else if (e.type == SDL_MOUSEMOTION) {
            mouseX_ = float(e.motion.x);
            mouseY_ = float(e.motion.y);
            if (draggingMinimap_) {
                minimapClick(mouseX_, mouseY_, winW, winH);
            } else if (e.motion.state & SDL_BUTTON_MMASK) {   // middle-drag scrolls
                mapView_.setOffset(mapView_.offX() - e.motion.xrel / zm,
                                   mapView_.offY() - e.motion.yrel / zm);
            }
            if (dragging_) { dragX1_ = float(e.motion.x); dragY1_ = float(e.motion.y); }
        } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                   e.button.y > winH - kBarH) {
            // bottom bar: build-icon clicks; everything else is swallowed
            if (e.button.button == SDL_BUTTON_LEFT) {
                for (const auto& [r, bt] : iconRects_) {
                    if (e.button.x < r.x || e.button.x > r.x + r.w ||
                        e.button.y < r.y || e.button.y > r.y + r.h)
                        continue;
                    const auto* b = selectedBuilder();
                    if (!b || !bt) break;
                    if (b->type->canMove) {
                        placing_ = bt;
                    } else {
                        tak::net::Command c;
                        c.kind = tak::net::Cmd::Train;
                        c.unitId = b->id;
                        std::snprintf(c.type, sizeof c.type, "%s", bt->id.c_str());
                        issue(c);
                    }
                    break;
                }
            }
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   orderColumnClick(float(e.button.x), float(e.button.y), winW)) {
            // order button handled
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   minimapClick(float(e.button.x), float(e.button.y), winW, winH)) {
            draggingMinimap_ = true;   // camera follows the drag until release
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   pendingCmd_) {
            float wx = mapView_.offX() + e.button.x / zm;
            float wz = mapView_.offY() + e.button.y / zm;
            bool queue = (SDL_GetModState() & KMOD_SHIFT) != 0;
            if (pendingCmd_ == 'g') {
                // Guard: needs a friendly unit under the click.
                int buddy = -1;
                float best = 24 * 24;
                for (auto& u : world_.units()) {
                    if (!u.alive() || u.team != localTeam_) continue;
                    float dx = u.x - wx, dz = u.z - wz;
                    if (dx * dx + dz * dz < best) { best = dx * dx + dz * dz; buddy = u.id; }
                }
                if (buddy >= 0) {
                    for (int id : selection_) {
                        if (id == buddy) continue;
                        tak::net::Command c;
                        c.kind = tak::net::Cmd::Guard;
                        c.unitId = id;
                        c.targetId = buddy;
                        c.queue = queue;
                        issue(c);
                    }
                    voice(selection_.front(), "guard");
                }
                pendingCmd_ = 0;
                return;
            }
            // 'a' (attack) targets an enemy under the click if there is one,
            // otherwise falls through to attack-move on the ground.
            int enemy = -1;
            if (pendingCmd_ == 'a') {
                const auto* first = world_.unit(selection_.front());
                float best = 20 * 20;
                for (auto& u : world_.units()) {
                    if (!u.alive() || u.embarked() || !first || u.team == first->team)
                        continue;
                    float dx = u.x - wx, dz = u.z - wz;
                    if (dx * dx + dz * dz < best) { best = dx * dx + dz * dz; enemy = u.id; }
                }
            }
            for (int id : selection_) {
                tak::net::Command c;
                if (enemy >= 0) {
                    c.kind = tak::net::Cmd::Attack;
                    c.targetId = enemy;
                } else {
                    c.kind = (pendingCmd_ == 'f' || pendingCmd_ == 'a')
                                 ? tak::net::Cmd::AttackMove
                             : pendingCmd_ == 'p' ? tak::net::Cmd::Patrol
                                                  : tak::net::Cmd::Move;
                    c.x = wx;
                    c.z = wz;
                }
                c.unitId = id;
                c.queue = queue;
                issue(c);
            }
            voice(selection_.front(),
                  (pendingCmd_ == 'a' || pendingCmd_ == 'f') ? "attack"
                  : pendingCmd_ == 'p'                       ? "patrol"
                                                             : "move");
            pendingCmd_ = 0;
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   placing_) {
            float wx = mapView_.offX() + e.button.x / zm;
            float wz = mapView_.offY() + e.button.y / zm;
            if (SDL_GetModState() & KMOD_SHIFT) {
                // Shift: begin a drag — a whole line of these gets queued on
                // release (a single shift-click is just a zero-length line).
                buildDrag_ = true;
                bdX0_ = wx;
                bdZ0_ = wz;
            } else if (!selection_.empty() && world_.canPlace(placing_, wx, wz)) {
                tak::net::Command c;
                c.kind = tak::net::Cmd::Build;
                c.unitId = selectedBuilder() ? selectedBuilder()->id : selection_.front();
                c.x = wx;
                c.z = wz;
                std::snprintf(c.type, sizeof c.type, "%s", placing_->id.c_str());
                issue(c);
                placing_ = nullptr;
            }
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT &&
                   buildDrag_) {
            buildDrag_ = false;
            placeBuildLine(bdX0_, bdZ0_, mapView_.offX() + e.button.x / zm,
                           mapView_.offY() + e.button.y / zm);
            if (!(SDL_GetModState() & KMOD_SHIFT)) placing_ = nullptr;
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT &&
                   placing_) {
            placing_ = nullptr;
            buildDrag_ = false;
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            dragging_ = true;
            dragX0_ = dragX1_ = float(e.button.x);
            dragY0_ = dragY1_ = float(e.button.y);
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT &&
                   draggingMinimap_) {
            draggingMinimap_ = false;
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            dragging_ = false;
            float x0 = mapView_.offX() + std::min(dragX0_, dragX1_) / zm;
            float x1 = mapView_.offX() + std::max(dragX0_, dragX1_) / zm;
            float z0 = mapView_.offY() + std::min(dragY0_, dragY1_) / zm;
            float z1 = mapView_.offY() + std::max(dragY0_, dragY1_) / zm;
            bool isClick = (x1 - x0) < 6 && (z1 - z0) < 6;
            selection_.clear();
            if (isClick) {
                float wx = (x0 + x1) / 2, wz = (z0 + z1) / 2;
                int hit = -1;
                float best = 24 * 24;
                for (auto& u : world_.units()) {
                    if (!u.alive()) continue;
                    float dx = u.x - wx, dz = u.z - wz;
                    if (dx * dx + dz * dz < best) { best = dx * dx + dz * dz; hit = u.id; }
                }
                if (hit >= 0) {
                    selection_.push_back(hit);
                    voice(hit, "select");
                }
            } else {
                for (auto& u : world_.units())
                    if (u.alive() && u.team == localTeam_ && u.x >= x0 && u.x <= x1 &&
                        u.z >= z0 && u.z <= z1)
                        selection_.push_back(u.id);
            }
        } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                   e.button.button == SDL_BUTTON_RIGHT && !selection_.empty()) {
            float wx = mapView_.offX() + e.button.x / zm;
            float wz = mapView_.offY() + e.button.y / zm;
            bool queue = (SDL_GetModState() & KMOD_SHIFT) != 0;
            const auto* first = world_.unit(selection_.front());
            // Selected transport with cargo: right-click = sail + disembark.
            if (first && first->type && first->type->canTransport &&
                !first->cargo.empty()) {
                tak::net::Command c;
                c.kind = tak::net::Cmd::Unload;
                c.unitId = first->id;
                c.x = wx;
                c.z = wz;
                issue(c);
                return;
            }
            // Clicking a friendly transport = board it.
            int friendlyTransport = -1;
            float bestT = 24 * 24;
            for (auto& u : world_.units()) {
                if (!u.alive() || !first || u.team != first->team || !u.type ||
                    !u.type->canTransport)
                    continue;
                float dx = u.x - wx, dz = u.z - wz;
                if (dx * dx + dz * dz < bestT) { bestT = dx * dx + dz * dz; friendlyTransport = u.id; }
            }
            if (friendlyTransport >= 0) {
                for (int id : selection_) {
                    tak::net::Command c;
                    c.kind = tak::net::Cmd::Load;
                    c.unitId = id;
                    c.targetId = friendlyTransport;
                    issue(c);
                }
                return;
            }
            // Clicking near an enemy = attack; else formation move.
            int enemy = -1;
            float best = 20 * 20;
            for (auto& u : world_.units()) {
                if (!u.alive() || u.embarked() || !first || u.team == first->team) continue;
                float dx = u.x - wx, dz = u.z - wz;
                if (dx * dx + dz * dz < best) { best = dx * dx + dz * dz; enemy = u.id; }
            }
            if (enemy >= 0) {
                for (int id : selection_) {
                    tak::net::Command c;
                    c.kind = tak::net::Cmd::Attack;
                    c.unitId = id;
                    c.targetId = enemy;
                    c.queue = queue;
                    issue(c);
                }
                voice(selection_.front(), "attack");
            } else {
                voice(selection_.front(), "move");
                float cx = 0, cz = 0;
                int n = 0;
                for (int id : selection_)
                    if (const auto* u = world_.unit(id)) { cx += u->x; cz += u->z; ++n; }
                if (n) { cx /= float(n); cz /= float(n); }
                for (int id : selection_) {
                    const auto* u = world_.unit(id);
                    if (!u) continue;
                    tak::net::Command c;
                    c.kind = tak::net::Cmd::Move;
                    c.unitId = id;
                    c.x = wx + std::clamp(u->x - cx, -60.0f, 60.0f);
                    c.z = wz + std::clamp(u->z - cz, -60.0f, 60.0f);
                    c.queue = queue;
                    issue(c);
                }
            }
        }
    }

    void setNet(tak::net::Session* net) {
        net_ = net;
        localTeam_ = net->localPlayer();
        world_.setVisTeam(localTeam_);
        aiEnabled_ = false;   // both sides are human in a net game
    }

    // Route a command: apply immediately offline, schedule via net online.
    void issue(tak::net::Command c) {
        c.player = uint8_t(localTeam_);
        if (net_) net_->issue(c);
        else apply(c);
    }

    void apply(const tak::net::Command& c) {
        using tak::net::Cmd;
        // Only allow commanding units the issuing player owns.
        auto owns = [&](int id) {
            const auto* u = world_.unit(id);
            return u && u->team == int(c.player);
        };
        // A fresh redirect order (not a shift-queued one) also abandons any
        // pending build queue, so its ghosts don't linger.
        auto redirect = [&] { if (!c.queue) world_.cancelBuilds(c.unitId); };
        switch (c.kind) {
            case Cmd::Move:
                if (owns(c.unitId)) { redirect(); world_.order(c.unitId, c.x, c.z, c.queue); }
                break;
            case Cmd::Attack:
                if (owns(c.unitId)) { redirect(); world_.attack(c.unitId, c.targetId, c.queue); }
                break;
            case Cmd::AttackMove:
                if (owns(c.unitId)) { redirect(); world_.attackMove(c.unitId, c.x, c.z, c.queue); }
                break;
            case Cmd::Patrol:
                if (owns(c.unitId)) { world_.cancelBuilds(c.unitId); world_.patrol(c.unitId, c.x, c.z); }
                break;
            case Cmd::Stop:
                if (owns(c.unitId)) { world_.cancelBuilds(c.unitId); world_.stop(c.unitId); }
                break;
            case Cmd::Train:
                if (owns(c.unitId)) world_.train(c.unitId, registry_.find(c.type));
                break;
            case Cmd::Build:
                if (owns(c.unitId))
                    world_.queueBuild(c.unitId, registry_.find(c.type), c.x, c.z,
                                      c.queue);
                break;
            case Cmd::Guard:
                if (owns(c.unitId)) world_.guard(c.unitId, c.targetId, c.queue);
                break;
            case Cmd::Load:
                if (owns(c.unitId)) world_.loadInto(c.unitId, c.targetId);
                break;
            case Cmd::Unload:
                if (owns(c.unitId)) world_.unloadAt(c.unitId, c.x, c.z);
                break;
        }
    }

    // One lockstep step: returns false while stalled waiting for the peer.
    bool netStep() {
        std::vector<tak::net::Command> cmds;
        if (!net_->exchange(netTick_, cmds, 2000)) {
            netError_ = net_->error();
            return false;
        }
        for (const auto& c : cmds) apply(c);
        update(1.0f / 30.0f);
        if (netTick_ % tak::net::kHashInterval == 0) {
            uint64_t h = world_.stateHash();
            std::printf("HASH %u %016llx\n", netTick_, (unsigned long long)h);
            if (!net_->checkHash(netTick_, h)) netError_ = net_->error();
        }
        ++netTick_;
        return true;
    }

    uint32_t netTick() const { return netTick_; }
    tak::sim::World& worldRef() { return world_; }
    void selectOnly(int id) { selection_.clear(); selection_.push_back(id); }
    size_t menuSize(const std::string& id) { return registry_.buildable(id).size(); }
    bool hasIP() const { return !ipRoot_.empty(); }
    const std::string& netError() const { return netError_; }
    bool isNet() const { return net_ != nullptr; }

    void setFollow(float zoom) { follow_ = true; mapView_.setZoom(zoom); }

    void amphibDemo() {
        aiEnabled_ = false;
        amphib_ = true;
        const auto* shipType = registry_.find("vertrans");
        const auto& ground = world_.nav();
        const auto& water = world_.navFor(shipType);
        float cx = float(mapView_.map().width) * 8, cz = float(mapView_.map().height) * 8;

        // Walk outward from the island center along a direction: last land
        // cell with deep water a bit beyond = a beach; return both spots.
        auto findBeach = [&](float ax, float az, float* bx, float* bz, float* wx2,
                             float* wz2) {
            for (float r = 0; r < 4000; r += 16) {
                int gx = int(cx + ax * r) / 16, gz = int(cz + az * r) / 16;
                if (!ground.walkable(gx, gz)) {
                    for (float rw = r + 48; rw < r + 400; rw += 16) {
                        int wxc = int(cx + ax * rw) / 16, wzc = int(cz + az * rw) / 16;
                        if (water.walkable(wxc, wzc)) {
                            *bx = cx + ax * (r - 32);
                            *bz = cz + az * (r - 32);
                            *wx2 = cx + ax * rw;
                            *wz2 = cz + az * rw;
                            return true;
                        }
                    }
                }
            }
            return false;
        };
        float bax, baz, wax, waz, bbx, bbz, wbx, wbz;
        if (!findBeach(-0.9f, 0.44f, &bax, &baz, &wax, &waz) ||
            !findBeach(0.44f, -0.9f, &bbx, &bbz, &wbx, &wbz)) {
            std::printf("amphib: no beaches found\n");
            return;
        }
        std::printf("amphib: embark beach (%.0f,%.0f) landing (%.0f,%.0f)\n", bax, baz,
                    bbx, bbz);
        amphibLandX_ = bbx;
        amphibLandZ_ = bbz;
        amphibSeaX_ = wbx;
        amphibSeaZ_ = wbz;

        transportId_ = spawn("vertrans", wax, waz, 0, 0);
        const char* squad[] = {"araarch", "araarch", "arasword", "arasword"};
        int i = 0;
        for (const char* t : squad) {
            int id = spawn(t, bax + float(i % 2) * 24 - 12, baz + float(i / 2) * 24 - 12,
                           0, 0);
            if (id >= 0) {
                world_.loadInto(id, transportId_);
                ++amphibSquad_;
            }
            ++i;
        }
    }

    void navyDemo() {
        aiEnabled_ = false;
        struct S { const char* t; float x, z; int team; };
        const S fleet[] = {
            {"verflag", 1150, 1250, 0}, {"verman", 1080, 1150, 0},
            {"verman", 1220, 1130, 0},  {"verharp", 1020, 1260, 0},
            {"vertre", 1100, 1360, 0},
            {"npcbotl", 1750, 1500, 1}, {"npcbotl", 1830, 1600, 1},
            {"monpiran", 1700, 1400, 1}, {"monpiran", 1780, 1420, 1},
            {"monpiran", 1650, 1500, 1},
        };
        std::vector<int> a, b;
        for (const auto& sp : fleet) {
            int id = spawn(sp.t, sp.x, sp.z, sp.team == 0 ? 1.57f : -1.57f, sp.team);
            if (id >= 0) (sp.team == 0 ? a : b).push_back(id);
        }
        for (size_t i = 0; i < a.size(); ++i)
            world_.attack(a[i], b[i % b.size()], false);
        for (size_t i = 0; i < b.size(); ++i)
            world_.attack(b[i], a[i % a.size()], false);
        mapView_.setOffset(1500 - 640 / 0.9f, 1380 - 400 / 0.9f);
    }

    void hillTest() {
        // Team 0 gets 2 scoring units in the region, team 1 gets 1.
        if (!scenUnit_ || scenRegion_.name.empty()) return;
        float cx = float(scenRegion_.x1 + scenRegion_.x2) * 8;
        float cz = float(scenRegion_.z1 + scenRegion_.z2) * 8;
        spawn(scenUnit_->id, cx - 20, cz, 0, 0);
        spawn(scenUnit_->id, cx + 20, cz, 0, 0);
        spawn(scenUnit_->id, cx, cz + 30, 0, 1);
        scenClock_ = scenTime_ - 12;   // fast-forward the timer for testing
    }

    void creonDemo() {
        aiEnabled_ = false;
        float cx = mapView_.map().blocksX * 16.0f, cz = mapView_.map().blocksY * 16.0f;
        const char* squad[] = {"cregod",  "creiron", "creauto", "creauto",
                               "crebeas", "cregatl", "creshoc", "credrag"};
        int i = 0;
        for (const char* t : squad) {
            spawn(t, cx - 100 + float(i % 4) * 60, cz - 40 + float(i / 4) * 70, 1.57f, 0);
            ++i;
        }
        mapView_.setOffset(cx - 640 / 0.9f, cz - 400 / 0.9f);
    }

    void missionTest() {
        // Plant 4 Watch Towers + escorts inside mission06's forest-edge
        // region (cells 147,119-269,210) to exercise the victory script.
        for (int i = 0; i < 4; ++i)
            spawn("verat", 3300 + float(i % 2) * 60, 2600 + float(i / 2) * 60, 0, 0);
        spawn("versword", 3260, 2700, 0, 0);
        mapView_.setOffset(3300 - 640 / 0.9f, 2620 - 400 / 0.9f);
    }

    void testBuild() {
        aiEnabled_ = false;
        const auto* keep = world_.unit(keepId_);
        if (!keep) return;
        const auto* lode = registry_.find("aralode");
        // Probe outward from the keep for the first legal site.
        for (float r = 90; r < 400; r += 24) {
            for (float a = 0; a < 6.28f; a += 0.5f) {
                float x = keep->x + std::cos(a) * r, z = keep->z + std::sin(a) * r;
                if (world_.canPlace(lode, x, z)) {
                    int id = world_.startBuild(builderId_, lode, x, z);
                    std::printf("testbuild: site id %d at %.0f,%.0f\n", id, x, z);
                    return;
                }
            }
        }
        std::printf("testbuild: no site found\n");
    }

    void lookAt(float x, float z) {
        mapView_.setOffset(x - 640 / mapView_.zoom(), z - 400 / mapView_.zoom());
    }

    std::string lodeUnit;
    void soundTest() {
        aiEnabled_ = false;
        int id = spawn("araarch", 900, 1000, 0, 0);
        selection_ = {id};
        for (int i = 0; i < 8; i++) voice(id, "move");
    }
    void faceTest() {
        aiEnabled_ = false;
        float cx = mapView_.map().blocksX * 16.0f, cz = mapView_.map().blocksY * 16.0f;
        struct D { float dx, dz; };
        float ddx[4]={0,400,0,-400}, ddz[4]={-400,0,400,0};
        for (int i=0;i<4;i++){
            int id=spawn("araarch", cx+ddx[i]*0.15f, cz+ddz[i]*0.15f, 0, 0);
            world_.order(id, cx+ddx[i], cz+ddz[i], false);
        }
        mapView_.setOffset(cx - 640 / mapView_.zoom(), cz - 400 / mapView_.zoom());
    }
    void fireTest() {
        aiEnabled_ = false;
        float cx = mapView_.map().blocksX * 16.0f, cz = mapView_.map().blocksY * 16.0f;
        int a = spawn("araarch", cx - 40, cz, 1.57f, 0);
        int e = spawn("tararch", cx + 200, cz, -1.57f, 1);
        world_.attack(a, e, false);
        mapView_.setOffset(cx - 640 / mapView_.zoom(), cz - 400 / mapView_.zoom());
    }
    void lodeTest() {
        aiEnabled_ = false;
        float cx = mapView_.map().blocksX * 16.0f, cz = mapView_.map().blocksY * 16.0f;
        spawn(lodeUnit.empty()?"zonlode":lodeUnit, cx, cz, 3.14159f, 0);
        mapView_.setOffset(cx - 640 / mapView_.zoom(), cz - 400 / mapView_.zoom());
    }

    void guardTest() {
        // Squad guards the first unit; the first unit marches east alone.
        int leader = -1;
        for (auto& u : world_.units())
            if (u.alive() && u.team == 0 && u.type && u.type->canMove) {
                if (leader < 0) leader = u.id;
                else world_.guard(u.id, leader, false);
            }
        if (leader >= 0) world_.order(leader, 1500, 950, false);
    }

    void marchTo(float dx, float dz) {
        float cx = mapView_.map().blocksX * 16.0f, cz = mapView_.map().blocksY * 16.0f;
        for (auto& u : world_.units())
            if (u.team == localTeam_ && u.type && u.type->canMove) {
                tak::net::Command c;
                c.kind = tak::net::Cmd::AttackMove;
                c.unitId = u.id;
                c.x = cx + dx;
                c.z = cz + dz;
                issue(c);
            }
    }

    void update(float dt) {
        sounds_.pollMusic();
        if (paused_) return;   // freeze the sim; input/render keep running
        world_.tick(dt);
        for (auto& u : world_.units())
            if (u.type && u.alive() && !unitType_.count(u.id)) registerUnit(u);
        tickAi(dt);
        if (briefTimer_ > 0) briefTimer_ -= dt;
        animClock_ += dt;
        if (!spawnRules_.empty() || !messages_.empty()) scenClock2_ += dt;
        for (auto& sr : spawnRules_) {
            if (sr.atTime >= 0) {
                if (!sr.done && scenClock2_ >= sr.atTime) {
                    sr.done = true;
                    spawn(sr.type, sr.x, sr.z, 0, sr.team);
                    if (hudFont_.ok()) { notice_ = "A POWER AWAKENS"; noticeTimer_ = 5; }
                }
            } else if (sr.maintainCount > 0) {
                sr.cooldown -= dt;
                if (sr.cooldown > 0) continue;
                sr.cooldown = 5;
                int have = 0;
                for (auto& u : world_.units()) {
                    if (!u.alive() || !u.type || u.type->id != sr.maintainType) continue;
                    int cx = int(u.x) / 16, cz = int(u.z) / 16;
                    if (cx >= sr.maintainRect.x1 && cz >= sr.maintainRect.z1 &&
                        cx <= sr.maintainRect.x2 && cz <= sr.maintainRect.z2)
                        ++have;
                }
                if (have < sr.maintainCount) spawn(sr.type, sr.x, sr.z, 0, sr.team);
            }
        }
        for (auto& m : messages_) {
            if (m.first >= 0 && scenClock2_ >= m.first) {
                notice_ = m.second;
                noticeTimer_ = 8;
                m.first = -1;
            }
        }
        if (scenUnit_ && scenTime_ > 0 && outcome_ == 0) {
            scenClock_ += dt;
            if (scenClock_ >= scenTime_) {
                int counts[4] = {0, 0, 0, 0};
                for (auto& u : world_.units()) {
                    if (!u.alive() || !u.type || u.type != scenUnit_) continue;
                    int cx = int(u.x) / 16, cz = int(u.z) / 16;
                    if (cx >= scenRegion_.x1 && cz >= scenRegion_.z1 &&
                        cx <= scenRegion_.x2 && cz <= scenRegion_.z2 && u.team < 4)
                        ++counts[u.team];
                }
                int best = 0;
                for (int i = 1; i < 4; ++i)
                    if (counts[i] > counts[best]) best = i;
                bool tie = false;
                for (int i = 0; i < 4; ++i)
                    if (i != best && counts[i] == counts[best]) tie = true;
                std::printf("scenario result: %d %d %d %d -> %s\n", counts[0],
                            counts[1], counts[2], counts[3],
                            tie ? "tie" : (best == localTeam_ ? "win" : "loss"));
                outcome_ = (!tie && best == localTeam_) ? 1 : -1;
            }
        }
        for (auto& u : world_.units()) {
            if (u.alive() || u.deadFor < 4.0f || corpsed_.count(u.id)) continue;
            corpsed_.insert(u.id);
            if (u.type && !u.type->corpse.empty())
                addFeature(u.type->corpse, u.x, u.z, false);
        }
        if (noticeTimer_ > 0) noticeTimer_ -= dt;
        if (missionVm_) {
            missionVm_->tick(dt);
            // Engine sweep: armed regions fire TriggerHit per player unit
            // inside. One-shot story triggers disarm themselves in-script;
            // viccheck re-arms its counting region every loop.
            trigTimer_ -= dt;
            if (trigTimer_ <= 0) {
                trigTimer_ = 0.3f;
                for (auto& [rid, r] : regions_) {
                    if (!r.armed) continue;
                    for (auto& u : world_.units()) {
                        if (!u.alive() || u.embarked() || u.team != 0) continue;
                        int cx = int(u.x) / 16, cz = int(u.z) / 16;
                        bool inside = r.rect
                            ? (cx >= r.a && cz >= r.b && cx <= r.c && cz <= r.d)
                            : ((cx - r.a) * (cx - r.a) + (cz - r.b) * (cz - r.b) <=
                               r.c * r.c);
                        if (inside && missionVm_->threadCount() < 200)
                            missionVm_->start("TriggerHit", {rid, u.id, 0});
                    }
                }
            }
            if (trace_) {
                static float dbg = 0;
                dbg += dt;
                if (dbg > 2) {
                    dbg = 0;
                    std::printf("MSTAT s0=%d threads=%zu pcs:",
                                missionVm_->getStatic(0), missionVm_->threadCount());
                    std::map<uint32_t, int> hist;
                    for (auto pc : missionVm_->threadPcs()) ++hist[pc];
                    for (auto& [pc, n] : hist) std::printf(" %u x%d", pc, n);
                    std::printf("\n");
                }
            }
            for (auto& u : world_.units()) {
                if (u.team != 0) continue;
                if (u.justBuilt) missionVm_->start("UnitCreated", {u.justBuilt, 0});
                bool wasBuilding = building_.count(u.id) != 0;
                if (u.underConstruction) building_.insert(u.id);
                else if (wasBuilding) {
                    building_.erase(u.id);
                    missionVm_->start("UnitCreated", {u.id, 0});
                }
            }
        }
        if (amphib_) {
            auto* t = world_.unit(transportId_);
            if (t && t->alive()) {
                if (amphibPhase_ == 0 && int(t->cargo.size()) >= amphibSquad_) {
                    world_.unloadAt(transportId_, amphibSeaX_, amphibSeaZ_);
                    amphibPhase_ = 1;
                } else if (amphibPhase_ == 1 && t->cargo.empty()) {
                    for (auto& u : world_.units())
                        if (u.alive() && !u.embarked() && u.team == 0 && u.type &&
                            u.type->canMove && !u.type->canTransport)
                            world_.order(u.id, amphibLandX_, amphibLandZ_, false);
                    amphibPhase_ = 2;
                }
            }
        }

        // Victory check: a side with no living units loses. Only armed
        // once both sides have fielded units (staged demos may not).
        if (outcome_ == 0) {
            int alive[2] = {0, 0};
            for (auto& u : world_.units())
                if (u.alive() && u.team < 2) ++alive[u.team];
            sawTeam_[0] |= alive[0] > 0;
            sawTeam_[1] |= alive[1] > 0;
            if (sawTeam_[0] && sawTeam_[1]) {
                int other = localTeam_ == 0 ? 1 : 0;
                if (alive[other] == 0) outcome_ = 1;
                else if (alive[localTeam_] == 0) outcome_ = -1;
            }
        }
        if (follow_ && !world_.units().empty()) {
            // Track moving friendly units; fall back to everyone.
            float cx = 0, cz = 0;
            int n = 0;
            for (auto& u : world_.units())
                if (u.alive() && u.team == 0 && u.type && u.type->canMove &&
                    u.moving()) { cx += u.x; cz += u.z; ++n; }
            if (!n)
                for (auto& u : world_.units())
                    if (u.alive()) { cx += u.x; cz += u.z; ++n; }
            if (n)
                mapView_.setOffset(cx / float(n) - 640 / mapView_.zoom(),
                                   cz / float(n) - 400 / mapView_.zoom());
        }
        for (auto& u : world_.units()) {
            auto it = anims_.find(u.id);
            if (u.justFired && u.type) {
                if (u.type->weapon.melee)
                    sounds_.play("ahitfl0" + std::to_string(1 + (salt_++ % 3)));
                else
                    sounds_.play("bow2");
                // Play the unit's own firing animation while standing. Flyers
                // keep their continuous flight threads running, so don't reset.
                if (it != anims_.end() && !u.walking()) {
                    auto& fa = it->second;
                    if (!fa.flying) { fa.vm->reset(); fa.vm->setStatic(0, 0); }
                    fa.vm->start("FireWeapon") || fa.vm->start("attack1") ||
                        fa.vm->start("fire") || fa.vm->start("MeleeAttack");
                    fa.walking = false;
                    fa.firing = true;
                }
            }
            if (it == anims_.end()) continue;
            auto& a = it->second;
            if (!u.alive()) {
                if (!a.dying) {
                    a.dying = true;
                    a.vm->reset();
                    a.vm->setStatic(0, 0);
                    a.vm->start("death") || a.vm->start("Dying") || a.vm->start("Killed");
                    const std::string& id = u.type->id;
                    if (sounds_.has(id + "die1")) sounds_.play(id + "die1");
                    else if (sounds_.has(id + "die2")) sounds_.play(id + "die2");
                    if (missionVm_ && u.team == 0)
                        missionVm_->start("UnitDestroyed", {u.id});
                }
                a.vm->tick(dt);
                continue;
            }
            if (a.flying) {
                // Take off when moving, settle back to the ground when idle.
                float cruise = u.type ? u.type->cruiseAlt : 0.0f;
                float target = (u.walking() || !u.orders.empty()) ? cruise : 0.0f;
                float step = std::max(cruise, 1.0f) / 0.7f * dt;   // ~0.7s to cruise
                a.altitude += std::clamp(target - a.altitude, -step, step);

                // Only run the flight animation while she's actually off the
                // ground; on landing, restore the standing pose.
                bool air = a.altitude > std::max(cruise, 1.0f) * 0.3f;
                if (air) {
                    if (!a.airborne) {
                        a.airborne = true;
                        a.vm->reset();
                        a.vm->setStatic(8, 1);   // gate that "fly" animates on
                        a.vm->start("fly");
                    } else if (a.vm->threadCount() == 0) {
                        a.vm->start("fly");       // keep the beat looping
                    }
                } else if (a.airborne) {
                    a.airborne = false;
                    a.vm->reset();
                    a.vm->setStatic(8, 0);
                    a.vm->start("restore_x");     // fold back to a standing pose
                }
            } else {
                bool m = u.walking();
                if (m != a.walking) {
                    a.walking = m;
                    a.vm->reset();
                    a.vm->setStatic(0, m ? 1 : 0);
                    if (m) { a.vm->start("walk_legs") || a.vm->start("walk"); }
                    else { a.vm->start("restore_legs") || a.vm->start("restore_x"); }
                    a.firing = false;
                } else if (m && a.vm->threadCount() == 0) {
                    // The walk script is single-pass; the engine re-invokes it
                    // each cycle while the unit keeps moving.
                    a.vm->start("walk_legs") || a.vm->start("walk");
                }
            }
            if (u.type && !u.type->canMove) {   // buildings: yard/production anims
                bool busy = !u.buildQueue.empty();
                if (busy != a.producing) {
                    a.producing = busy;
                    a.vm->reset();
                    if (busy) {
                        a.vm->start("startbuild") || a.vm->start("OpenYard") ||
                            a.vm->start("Activate");
                    } else {
                        a.vm->start("stopbuild") || a.vm->start("CloseYard") ||
                            a.vm->start("Deactivate");
                    }
                }
            }
            // The FlightControl machine is timed for real time; over-ticking it
            // makes the wing keyframes snap past each other (crossing, not
            // flapping), so flyers run at 1x like everything else.
            a.vm->tick(dt);
        }
    }

    // Create textures (terrain chunks, minimap) before the render pass.
    void prepare(int winW, int winH) {
        mapView_.ensureChunks(winW, winH);
        if (!miniTex_) buildMinimap();
    }

    void draw(int winW, int winH) {
        mapView_.draw(winW, winH);
        float zm0 = mapView_.zoom();

        // Painter list: features and units together, sorted by map z.
        struct Item { float z; const tak::sim::Unit* u; const FeatureInst* f; };
        std::vector<Item> items;
        const auto& vis = world_.visibility();
        int vw = world_.visW();
        for (const auto& f : features_) {
            int cx = int(f.x) / 16, cz = int(f.z) / 16;
            if (!noFog_ && !vis.empty() && (cx < 0 || cz < 0 || cx >= vw ||
                                 vis[size_t(cz) * vw + cx] == 0))
                continue;   // unexplored
            float sx = (f.x - mapView_.offX()) * zm0, sy = (f.z - mapView_.offY()) * zm0;
            if (sx < -200 || sy < -200 || sx > winW + 200 || sy > winH + 200) continue;
            // Mana deposit markers are flat ground decals a lodestone is built
            // on top of, so bias their sort key back to keep them painted
            // under the building rather than over it.
            float key = f.mana ? f.z - 24.0f : f.z;
            items.push_back({key, nullptr, &f});
        }
        for (auto& u : world_.units()) {
            if (u.deadFor >= 4.0f || u.embarked()) continue;
            if (!noFog_ && u.team != localTeam_ && !world_.cellVisible(u.x, u.z)) continue;
            items.push_back({u.z, &u, nullptr});
        }
        std::stable_sort(items.begin(), items.end(),
                  [](const Item& a, const Item& b) { return a.z < b.z; });
        for (const auto& it : items) {
            if (it.f) {
                const auto& f = *it.f;
                if (f.shadow) {
                    SDL_FRect sd{(f.x - mapView_.offX() - float(f.sxoff)) * zm0,
                                 (f.z - mapView_.offY() - float(f.syoff)) * zm0,
                                 float(f.sw) * zm0, float(f.sh) * zm0};
                    SDL_RenderCopyF(ren_, f.shadow, nullptr, &sd);
                }
                SDL_FRect dst{(f.x - mapView_.offX() - float(f.xoff)) * zm0,
                              (f.z - mapView_.offY() - float(f.yoff)) * zm0,
                              float(f.w) * zm0, float(f.h) * zm0};
                SDL_Texture* tex = f.tex;
                if (f.frames && f.frames->size() > 1)
                    tex = (*f.frames)[(size_t(animClock_ * 8) + size_t(f.seed)) %
                                      f.frames->size()];
                SDL_RenderCopyF(ren_, tex, nullptr, &dst);
            } else {
                // Soft shadow blob under mobile units.
                const auto& u = *it.u;
                if (u.alive() && u.type && u.type->canMove && !u.type->canFly) {
                    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 70);
                    float sx = (u.x - mapView_.offX()) * zm0;
                    float sy = (u.z - mapView_.offY()) * zm0 + 2 * zm0;
                    SDL_FRect sh{sx - 7 * zm0, sy - 2.5f * zm0, 14 * zm0, 5 * zm0};
                    SDL_RenderFillRectF(ren_, &sh);
                }
                drawUnit(u);
            }
        }

        // Ghosts of the local player's queued (shift) build orders.
        for (const auto& u : world_.units())
            if (u.alive() && u.team == localTeam_)
                for (const auto& bo : u.buildOrders)
                    if (bo.type) drawGhostAt(bo.type, bo.x, bo.z);

        // Projectiles: short bright streaks (only where visible).
        float zm = mapView_.zoom();
        SDL_SetRenderDrawColor(ren_, 255, 235, 140, 255);
        for (const auto& p : world_.projectiles()) {
            if (!world_.cellVisible(p.x, p.z)) continue;
            // Ballistic arc: peak height scales with flight time.
            float t = std::clamp(p.age / std::max(p.flight, 0.05f), 0.0f, 1.0f);
            float peak = std::min(40.0f, p.flight * 28.0f);
            float h = 8 + 4 * peak * t * (1 - t);
            float sx = (p.x - mapView_.offX()) * zm;
            float sy = (p.z - mapView_.offY()) * zm - h * zm;
            SDL_RenderDrawLineF(ren_, sx, sy, sx - p.vx * 0.035f * zm,
                                sy - p.vz * 0.035f * zm + (t < 0.5f ? 2.5f : -2.5f) * zm);
        }

        drawFog();
        if (buildDrag_ && placing_) {
            float mx = mapView_.offX() + mouseX_ / mapView_.zoom();
            float mz = mapView_.offY() + mouseY_ / mapView_.zoom();
            for (auto& [x, z] : buildLinePositions(bdX0_, bdZ0_, mx, mz))
                drawGhostAt(placing_, x, z);
        } else if (placing_) {
            drawGhost();
        }
        drawMinimap(winW, winH);
        drawOrderColumn(winW, winH);

        for (int id : selection_) {
            const auto* u = world_.unit(id);
            if (!u || !u->alive()) continue;
            drawBrackets(u->x, u->z, 11);
            for (const auto& o : u->orders)
                if (o.targetId == 0) drawRing(o.x, o.z, 4);
        }

        // Health bars for damaged or selected units.
        for (const auto& u : world_.units()) {
            if (!u.alive() || u.embarked() || !u.type) continue;
            if (u.underConstruction && !u.buildBegun) continue;   // ghost: no bar
            if (u.team != localTeam_ && !world_.cellVisible(u.x, u.z)) continue;
            bool sel = std::find(selection_.begin(), selection_.end(), u.id) !=
                       selection_.end();
            float frac = std::clamp(u.hp / u.type->maxHp, 0.0f, 1.0f);
            if (!sel && frac >= 1.0f) continue;
            float bw = 26 * zm, bh = std::max(2.0f, 3 * zm);
            float bx = (u.x - mapView_.offX()) * zm - bw / 2;
            float by = (u.z - mapView_.offY()) * zm - 30 * zm;
            SDL_FRect bg{bx - 1, by - 1, bw + 2, bh + 2};
            SDL_SetRenderDrawColor(ren_, 10, 10, 10, 220);
            SDL_RenderFillRectF(ren_, &bg);
            SDL_FRect fg{bx, by, bw * frac, bh};
            SDL_SetRenderDrawColor(ren_, uint8_t(230 * (1 - frac) + 40 * frac),
                                   uint8_t(200 * frac + 40 * (1 - frac)), 40, 255);
            SDL_RenderFillRectF(ren_, &fg);
        }

        // Production progress above busy buildings.
        for (const auto& u : world_.units()) {
            if (!u.alive() || u.buildQueue.empty() || !u.type) continue;
            if (u.team != localTeam_ && !world_.cellVisible(u.x, u.z)) continue;
            float total = u.buildQueue.front()->buildTime /
                          std::max(u.type->workerTime, 0.01f);
            float frac = std::clamp(u.buildProgress / total, 0.0f, 1.0f);
            float bw = 40 * zm, bh = std::max(3.0f, 4 * zm);
            float bx = (u.x - mapView_.offX()) * zm - bw / 2;
            float by = (u.z - mapView_.offY()) * zm - float(u.type->footZ) * 8 * zm - 14 * zm;
            SDL_FRect bg{bx - 1, by - 1, bw + 2, bh + 2};
            SDL_SetRenderDrawColor(ren_, 10, 10, 10, 220);
            SDL_RenderFillRectF(ren_, &bg);
            SDL_FRect fg{bx, by, bw * frac, bh};
            SDL_SetRenderDrawColor(ren_, 90, 170, 255, 255);
            SDL_RenderFillRectF(ren_, &fg);
        }

        // Team mana bar top left (legacy; only without the bottom bar).
        if (!panelTex_) {
            auto& tm = world_.team(localTeam_);
            float cap = std::max(tm.storage, 100.0f);
            SDL_FRect bg{10, 10, 180, 12};
            SDL_SetRenderDrawColor(ren_, 20, 20, 30, 230);
            SDL_RenderFillRectF(ren_, &bg);
            SDL_FRect fg{12, 12, 176 * std::clamp(tm.mana / cap, 0.0f, 1.0f), 8};
            SDL_SetRenderDrawColor(ren_, 80, 200, 255, 255);
            SDL_RenderFillRectF(ren_, &fg);
            if (hudFont_.ok() && !panelTex_) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "MANA %d/%d  +%d", int(tm.mana), int(cap),
                              int(tm.income));
                hudFont_.draw(ren_, buf, 198, 21, 1.5f, {170, 225, 255, 255});
                if (!selection_.empty()) {
                    const auto* u = world_.unit(selection_.front());
                    if (u && u->alive() && u->type) {
                        std::snprintf(buf, sizeof buf, "%s  %d/%d", u->type->name.c_str(),
                                      int(u->hp), int(u->type->maxHp));
                        hudFont_.draw(ren_, buf, 12, 40, 1.5f, {220, 220, 190, 255});
                        if (u->type->isBuilder) {
                            const auto& menu = registry_.buildable(u->type->id);
                            std::string m;
                            for (size_t i = 0; i < menu.size() && i < 6; ++i) {
                                const auto* bt = registry_.find(menu[i]);
                                m += std::to_string(i + 1) + ":" +
                                     (bt ? bt->name : menu[i]) + "  ";
                            }
                            if (!m.empty())
                                hudFont_.draw(ren_, m, 12, 58, 1.5f, {180, 200, 170, 255});
                            if (!u->buildQueue.empty()) {
                                std::snprintf(buf, sizeof buf, "TRAINING %s (%zu queued)",
                                              u->buildQueue.front()->name.c_str(),
                                              u->buildQueue.size());
                                hudFont_.draw(ren_, buf, 12, 76, 1.5f, {150, 200, 255, 255});
                            }
                        }
                    }
                }
            }
        }

        drawPanel(winW, winH);

        // Mission briefing (first 30s) and event notices.
        if (briefTimer_ > 0 && hudFont_.ok()) {
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_FRect bg{float(winW) - 560, 8, 552,
                         14.0f + 16.0f * float(briefing_.size())};
            SDL_SetRenderDrawColor(ren_, 10, 10, 20, 170);
            SDL_RenderFillRectF(ren_, &bg);
            float y = 24;
            for (const auto& l : briefing_) {
                hudFont_.draw(ren_, l, bg.x + 8, y, 1.4f, {220, 215, 180, 255});
                y += 16;
            }
        }
        if (net_ && hudFont_.ok()) {
            char nb[64];
            std::snprintf(nb, sizeof nb, "NET P%d  TICK %u", localTeam_ + 1, netTick_);
            hudFont_.draw(ren_, nb, float(winW) - 200, float(winH) - 14, 1.4f,
                          {140, 200, 255, 255});
            if (!netError_.empty()) {
                std::string msg = "NETWORK: " + netError_;
                float tw = float(hudFont_.width(msg, 2.0f));
                hudFont_.draw(ren_, msg, (float(winW) - tw) / 2, 150, 2.0f,
                              {255, 120, 100, 255});
            }
        }
        if (scenUnit_ && scenTime_ > 0 && outcome_ == 0 && hudFont_.ok()) {
            char sb[96];
            int rem = int(scenTime_ - scenClock_);
            std::snprintf(sb, sizeof sb, "%s IN %s: %d:%02d", scenUnit_->name.c_str(),
                          scenRegion_.name.c_str(), rem / 60, rem % 60);
            hudFont_.draw(ren_, sb, 12, 46, 1.5f, {255, 220, 140, 255});
        }
        if (pendingCmd_ && hudFont_.ok()) {
            const char* msg = pendingCmd_ == 'a'   ? "ATTACK: CLICK TARGET"
                              : pendingCmd_ == 'f' ? "FIGHT-MOVE: CLICK DESTINATION"
                              : pendingCmd_ == 'p' ? "PATROL: CLICK WAYPOINT"
                              : pendingCmd_ == 'g' ? "GUARD: CLICK FRIENDLY UNIT"
                                                   : "MOVE: CLICK DESTINATION";
            hudFont_.draw(ren_, msg, 12, 100, 1.6f, {255, 200, 120, 255});
        }
        if (noticeTimer_ > 0 && hudFont_.ok() && !notice_.empty()) {
            float tw = float(hudFont_.width(notice_, 2.5f));
            hudFont_.draw(ren_, notice_, (float(winW) - tw) / 2, 120, 2.5f,
                          {255, 230, 120, 255});
        }
        if (paused_ && bigFont_.ok()) {
            const char* msg = "PAUSED";
            float tw = float(bigFont_.width(msg, 1.2f));
            bigFont_.draw(ren_, msg, (winW - tw) / 2, 60, 1.2f, {255, 230, 120, 255});
        }

        // Victory / defeat banner.
        if (outcome_ != 0 && bigFont_.ok()) {
            const char* msg = outcome_ > 0 ? "VICTORY" : "DEFEAT";
            SDL_Color col = outcome_ > 0 ? SDL_Color{255, 220, 90, 255}
                                         : SDL_Color{255, 90, 70, 255};
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_FRect shade{0, float(winH) / 2 - 60, float(winW), 120};
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 150);
            SDL_RenderFillRectF(ren_, &shade);
            float tw = float(bigFont_.width(msg, 1.5f));
            bigFont_.draw(ren_, msg, (winW - tw) / 2, float(winH) / 2 + 24, 1.5f, col);
        }

        if (dragging_) {
            SDL_FRect r{std::min(dragX0_, dragX1_), std::min(dragY0_, dragY1_),
                        std::abs(dragX1_ - dragX0_), std::abs(dragY1_ - dragY0_)};
            SDL_SetRenderDrawColor(ren_, 120, 255, 150, 200);
            SDL_RenderDrawRectF(ren_, &r);
        }
    }

    void advance(float seconds) {
        float printed = 0;
        for (float t = 0; t < seconds; t += 1.0f / 30.0f) {
            update(1.0f / 30.0f);
            if (trace_ && t >= printed) {
                printed += 0.5f;
                for (auto& u : world_.units())
                    if (u.team == 0 && u.alive())
                        std::printf("TRACE %.1f %d %.1f %.1f\n", t, u.id, u.x, u.z);
            }
        }
    }

    void setTrace(bool on) { trace_ = on; sounds_.setVerbose(on); }

private:
    // Simple wave AI: keep the production queue full, and when enough idle
    // fighters have gathered, throw them at the nearest player unit.
    void tickAi(float dt) {
        aiTimer_ -= dt;
        if (!aiEnabled_ || aiTimer_ > 0 || outcome_ != 0) return;
        aiTimer_ = 1.0f;

        if (demoAi_) {
            // Showcase: both pre-built armies simply fight.
            runAi(1, aiKeepId_, aiCycle_);
            runAi(0, keepId_, std::array<std::string, 4>{"araarch", "arasword",
                                                         "araarch", "araknigh"});
        } else {
            // Skirmish: the AI Monarch bootstraps a base and produces.
            runBuildAi(1, aiMonarchId_, aiKeepId_, aiKeepType_, aiLodeType_,
                       aiCycle_);
        }
    }

    // Start a building for the AI: lodestones go on the nearest free mana
    // deposit (when the map has any), everything else probes outward from the
    // builder. Returns the new building's id, or 0.
    int aiPlace(int builderId, const tak::sim::UnitType* t, float nx, float nz) {
        if (!t) return 0;
        if (t->onMana && world_.hasManaSpots()) {
            float bestD = 1e18f, bx = 0, bz = 0;
            bool found = false;
            for (const auto& [sx, sz] : manaSpots_) {
                if (!world_.canPlace(t, sx, sz)) continue;   // taken or blocked
                float dx = sx - nx, dz = sz - nz, d = dx * dx + dz * dz;
                if (d < bestD) { bestD = d; bx = sx; bz = sz; found = true; }
            }
            return found ? world_.startBuild(builderId, t, bx, bz) : 0;
        }
        for (float r = 70; r < 340; r += 30)
            for (float a = 0; a < 6.28f; a += 0.5f) {
                float x = nx + std::cos(a) * r, z = nz + std::sin(a) * r;
                if (world_.canPlace(t, x, z)) return world_.startBuild(builderId, t, x, z);
            }
        return 0;
    }

    // Bootstrapping AI: the Monarch raises lodestones for income, then its
    // keep, then the keep trains a rolling army that attacks in waves.
    void runBuildAi(int team, int monarchId, int& keepId,
                    const std::string& keepType, const std::string& lodeType,
                    const std::array<std::string, 4>& cycle) {
        // Keepless factions (Zhon): the Monarch raises Beast Handlers and the
        // Handlers summon the fighting creatures — a two-tier build order.
        if (keepType.empty()) { runSummonAi(team, monarchId, lodeType, cycle); return; }

        if (auto* k = world_.unit(keepId); !k || !k->alive()) keepId = -1;

        auto* m = world_.unit(monarchId);
        bool monIdle = m && m->alive() && m->type && m->type->isBuilder &&
                       m->orders.empty() && m->buildSiteId == 0;
        if (monIdle) {
            // Lay down enough lodestones that their income covers the keep's
            // mogrium drain (buildCost*workerTime/buildTime) before committing
            // to it — otherwise the keep starves and crawls. Cheaper keeps
            // (Aramon) need ~3 lodes, pricier ones (Taros/Creon) more.
            const auto* kt = keepType.empty() ? nullptr : registry_.find(keepType);
            const auto* lt = lodeType.empty() ? nullptr : registry_.find(lodeType);
            int needLodes = 3;
            if (kt && m->type) {
                float drain = kt->buildCost * m->type->workerTime /
                              std::max(kt->buildTime, 1.0f);
                float perLode = lt ? std::max(lt->income, 1.0f) : 10.0f;
                needLodes = std::clamp(
                    int(std::ceil((drain - m->type->income) / perLode)), 3, 8);
            }
            // Priority: economy lodes, then the keep, then a small buffer of
            // extra lodes to fund production.
            const tak::sim::UnitType* want = nullptr;
            if (lt && aiLodes_ < needLodes) want = lt;
            else if (kt && keepId < 0) want = kt;
            else if (lt && aiLodes_ < needLodes + 2) want = lt;
            int b = aiPlace(monarchId, want, m->x, m->z);
            if (b > 0) {
                if (want->id == keepType) keepId = b;
                else ++aiLodes_;
            }
        }

        auto* keep = world_.unit(keepId);
        if (keep && keep->alive() && !keep->underConstruction &&
            keep->buildQueue.empty())
            world_.train(keepId,
                         registry_.find(cycle[size_t(aiTrained_++) % cycle.size()]));

        sendWaves(team);
    }

    // Zhon-style production: no keep. The Monarch builds a couple of
    // lodestones then Beast Handlers; each idle Handler summons the next
    // creature in the cycle (a mobile builder producing a mobile unit).
    void runSummonAi(int team, int monarchId, const std::string& lodeType,
                     const std::array<std::string, 4>& cycle) {
        const auto* lt = lodeType.empty() ? nullptr : registry_.find(lodeType);
        const auto* ht =
            aiBuilderType_.empty() ? nullptr : registry_.find(aiBuilderType_);

        auto* m = world_.unit(monarchId);
        bool monIdle = m && m->alive() && m->type && m->type->isBuilder &&
                       m->orders.empty() && m->buildSiteId == 0;
        if (monIdle) {
            // A summoning Handler drains ~25 mogrium/s but a lodestone only
            // yields 10, so economy has to stay well ahead of Handler count.
            // Interleave: ~3-4 lodes per Handler.
            const tak::sim::UnitType* want = nullptr;
            if (lt && aiLodes_ < 3) want = lt;                 // seed economy
            else if (ht && aiHandlers_ < 1) want = ht;         // first Handler
            else if (lt && aiLodes_ < 5) want = lt;            // grow income
            else if (ht && aiHandlers_ < 2) want = ht;         // second Handler
            else if (lt && aiLodes_ < 7) want = lt;            // grow income
            else if (ht && aiHandlers_ < 3) want = ht;         // third Handler
            int b = aiPlace(monarchId, want, m->x, m->z);
            if (b > 0) { if (want == ht) ++aiHandlers_; else ++aiLodes_; }
        }

        // One idle Beast Handler summons the next creature in the cycle.
        int handler = 0;
        for (auto& u : world_.units())
            if (u.alive() && u.team == team && u.type && u.type->isBuilder &&
                u.id != monarchId && u.orders.empty() && u.buildSiteId == 0) {
                handler = u.id;
                break;
            }
        if (handler) {
            const tak::sim::UnitType* ct = nullptr;
            for (int k = 0; k < 4 && !ct; ++k) {   // skip builders in the cycle
                const auto* c = registry_.find(cycle[size_t(aiTrained_++) % cycle.size()]);
                if (c && c->canMove && !c->isBuilder) ct = c;
            }
            if (const auto* h = ct ? world_.unit(handler) : nullptr) {
                float hx = h->x, hz = h->z;
                for (float r = 40; r < 170; r += 20)
                    for (float a = 0; a < 6.28f; a += 0.6f) {
                        float x = hx + std::cos(a) * r, z = hz + std::sin(a) * r;
                        if (!world_.canPlace(ct, x, z)) continue;
                        world_.startBuild(handler, ct, x, z);
                        r = 1e9f;   // done
                        break;
                    }
            }
        }

        sendWaves(team);
    }

    // Once a strike force has gathered, send the (non-builder) fighters at the
    // nearest enemy. Ids are collected first so world_.attack never mutates a
    // container being iterated.
    void sendWaves(int team) {
        std::vector<int> idle;
        for (auto& u : world_.units())
            if (u.alive() && u.team == team && u.type && u.type->canMove &&
                !u.type->isBuilder && u.orders.empty())
                idle.push_back(u.id);
        if (idle.size() < 4) return;
        for (int id : idle) {
            const auto* me = world_.unit(id);
            if (!me) continue;
            int best = 0;
            float bestD = 1e18f;
            for (auto& e : world_.units()) {
                if (!e.alive() || e.team == team) continue;
                float dx = e.x - me->x, dz = e.z - me->z;
                if (dx * dx + dz * dz < bestD) { bestD = dx * dx + dz * dz; best = e.id; }
            }
            if (best) world_.attack(id, best, false);
        }
    }

    void runAi(int team, int keepId, const std::array<std::string, 4>& cycle) {
        auto* keep = world_.unit(keepId);
        if (keep && keep->alive() && keep->buildQueue.empty())
            world_.train(keepId,
                         registry_.find(cycle[size_t(aiTrained_++) % cycle.size()]));

        std::vector<int> idle;
        for (auto& u : world_.units())
            if (u.alive() && u.team == team && u.type && u.type->canMove &&
                u.orders.empty())
                idle.push_back(u.id);
        if (idle.size() >= 4) {
            for (int id : idle) {
                const auto* me = world_.unit(id);
                int best = 0;
                float bestD = 1e18f;
                for (auto& e : world_.units()) {
                    if (!e.alive() || e.team == team) continue;
                    float dx = e.x - me->x, dz = e.z - me->z;
                    if (dx * dx + dz * dz < bestD) { bestD = dx * dx + dz * dz; best = e.id; }
                }
                if (best) world_.attack(id, best, false);
            }
        }
    }

    struct Visual {
        tak::tdo::Model model;
    };
    struct Anim {
        std::unique_ptr<tak::cob::Vm> vm;
        std::vector<std::string> pieceNames;
        bool walking = false;
        bool dying = false;
        bool producing = false;
        bool firing = false;
        bool flying = false;
        bool airborne = false;   // true while the flight animation should run
        float altitude = 0;      // flyers: 0 grounded, rising to cruiseAlt in flight
    };

    void registerUnit(const tak::sim::Unit& u) {
        const std::string& typeId = u.type->id;
        if (!visuals_.count(typeId)) {
            try {
                visuals_[typeId] = {tak::tdo::load(dataRoot_ + "/objects3d/" + typeId + ".3do")};
            } catch (const std::exception&) {
                try {
                    visuals_[typeId] = {
                        tak::tdo::load(ipRoot_ + "/objects3d/" + typeId + ".3do")};
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "no model for %s: %s\n", typeId.c_str(),
                                 e.what());
                    return;
                }
            }
        }
        Anim a;
        try {
            std::string cobPath = dataRoot_ + "/scripts/" + typeId + ".cob";
            if (!std::filesystem::exists(cobPath) && !ipRoot_.empty())
                cobPath = ipRoot_ + "/scripts/" + typeId + ".cob";
            auto cobFile = tak::cob::load(cobPath);
            for (const auto& p : cobFile.pieces) {
                std::string n = p;
                std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                a.pieceNames.push_back(n);
            }
            a.vm = std::make_unique<tak::cob::Vm>(std::move(cobFile));
            // TA COB unit-state queries answered from the sim.
            int unitId = u.id;
            a.vm->onGet = [this, unitId](int32_t valId,
                                         const std::vector<int32_t>&) -> int32_t {
                const auto* su = world_.unit(unitId);
                if (!su || !su->type) return 0;
                switch (valId) {
                    case 0:  return su->buildQueue.empty() ? 0 : 1;   // ACTIVATION
                    case 3:  return int32_t(su->hp / su->type->maxHp * 100);  // HEALTH
                    case 5:  return su->moving() ? 1 : 0;             // BUSY
                    case 8:  {                                        // UNIT_XZ
                        int32_t x = int32_t(su->x) & 0xFFFF;
                        int32_t z = int32_t(su->z) & 0xFFFF;
                        return (x << 16) | z;
                    }
                    case 16: return su->underConstruction              // BUILD_PCT_LEFT
                                 ? int32_t(100 - su->hp / su->type->maxHp * 100)
                                 : 0;
                    default: return 0;
                }
            };
            // Flyers deploy their wings and start flapping at spawn via their
            // flight scripts; without these they sit in the landed rest pose
            // (which also reads as facing the wrong way).
            if (u.type && u.type->canFly)
                a.flying = true;   // starts grounded; the update loop flies her
        } catch (const std::exception&) { /* unit stays unanimated */ }
        if (a.vm) anims_[u.id] = std::move(a);
        unitType_[u.id] = typeId;
    }

    int spawn(const std::string& typeId, float x, float z, float heading, int team) {
        const auto* type = registry_.find(typeId);
        if (!type) return -1;
        int id = world_.spawn(type, x, z, heading, team);
        if (const auto* u = world_.unit(id)) registerUnit(*u);
        if (!unitType_.count(id)) return -1;
        return id;
    }

    void loadTextures(const std::string& texDir, const std::string& defaultPalette) {
        // Faction texture banks use their own palettes (sidedata.tdf).
        std::map<std::string, tak::gaf::Palette> pals;
        pals["ara"] = tak::gaf::Palette::load(defaultPalette);
        for (const char* side : {"tar", "ver", "zon", "aid"}) {
            try {
                pals[side] = tak::gaf::Palette::load(
                    dataRoot_ + "/palettes/" + std::string(side) + "_textures.pcx");
            } catch (const std::exception&) {}
        }
        if (!ipRoot_.empty()) {
            try {
                pals["cre"] = tak::gaf::Palette::load(ipRoot_ +
                                                      "/palettes/cre_textures.pcx");
            } catch (const std::exception&) {}
        }
        for (const auto& e : std::filesystem::directory_iterator(texDir)) {
            if (e.path().extension() != ".gaf") continue;
            std::string stem = e.path().stem().string();
            std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);
            const auto* pal = &pals.at("ara");
            auto pit = pals.find(stem.substr(0, 3));
            if (pit != pals.end()) pal = &pit->second;
            try {
                for (auto& seq : tak::gaf::load(e.path(), *pal, 5)) {
                    if (seq.frames.empty()) continue;
                    std::string name = seq.name;
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    if (textures_.count(name)) continue;
                    // 10-frame sequences are per-player team colors.
                    size_t n = seq.frames.size() == 10 ? 10 : 1;
                    std::vector<SDL_Texture*> frames;
                    for (size_t i = 0; i < n; ++i) {
                        auto& f = seq.frames[i];
                        if (f.width == 0 || f.height == 0) break;
                        SDL_Texture* t = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                                           SDL_TEXTUREACCESS_STATIC,
                                                           f.width, f.height);
                        SDL_UpdateTexture(t, nullptr, f.rgba.data(), f.width * 4);
                        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                        frames.push_back(t);
                    }
                    if (!frames.empty()) textures_[name] = std::move(frames);
                }
            } catch (const std::exception&) {}
        }
    }

    const tak::cob::PieceState* pieceFor(const Anim* a, const std::string& objName) const {
        if (!a || !a->vm) return nullptr;
        std::string n = objName;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        for (size_t i = 0; i < a->pieceNames.size(); ++i)
            if (a->pieceNames[i] == n) return &a->vm->pieces()[i];
        return nullptr;
    }

    // The 3DO model for a type, loading it on demand (a queued build may have
    // no live unit of that type yet).
    const tak::tdo::Model* ghostModel(const std::string& typeId) {
        auto it = visuals_.find(typeId);
        if (it != visuals_.end()) return &it->second.model;
        for (const std::string& root : {dataRoot_, ipRoot_}) {
            if (root.empty()) continue;
            try {
                visuals_[typeId] = {tak::tdo::load(root + "/objects3d/" + typeId + ".3do")};
                return &visuals_[typeId].model;
            } catch (const std::exception&) {}
        }
        return nullptr;
    }

    // Draw a translucent, faintly blue ghost of a building where it will be
    // built later (a queued or not-yet-started site).
    void drawGhostAt(const tak::sim::UnitType* type, float x, float z) {
        const tak::tdo::Model* model = ghostModel(type->id);
        if (!model) return;
        tris_.clear();
        collect(model->root, Xform{}, nullptr, 0.0f, localTeam_);
        std::stable_sort(tris_.begin(), tris_.end(),
                  [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
        float zm = mapView_.zoom();
        float ax = (x - mapView_.offX()) * zm, ay = (z - mapView_.offY()) * zm;
        for (auto& t : tris_) {
            SDL_Vertex v[3];
            for (int i = 0; i < 3; ++i) {
                v[i] = t.v[i];
                v[i].position.x = v[i].position.x * zm + ax;
                v[i].position.y = v[i].position.y * zm + ay;
                v[i].color.a = 130;
                v[i].color.r = Uint8(v[i].color.r * 0.55f);   // shift toward blue
                v[i].color.g = Uint8(v[i].color.g * 0.8f);
            }
            SDL_RenderGeometry(ren_, t.tex, v, 3, nullptr, 0);
        }
    }

    void drawUnit(const tak::sim::Unit& u) {
        // A placed-but-not-yet-started site shows as a faint ghost until the
        // builder arrives and it begins conjuring for real.
        if (u.underConstruction && !u.buildBegun) {
            if (u.type) drawGhostAt(u.type, u.x, u.z);
            return;
        }
        auto vt = visuals_.find(unitType_.at(u.id));
        if (vt == visuals_.end()) return;
        const Anim* anim = nullptr;
        auto at = anims_.find(u.id);
        if (at != anims_.end()) anim = &at->second;

        tris_.clear();
        Xform base;
        if (u.type && u.type->canFly && u.type->cruiseAlt > 0)
            base.t[1] = anim ? anim->altitude : u.type->cruiseAlt;   // cruise height
        // Buildings are stationary; their billboards are authored facing the
        // camera (yaw 0). Mobile units turn to face their heading; the +pi
        // aligns the models' authored forward axis with the movement dir.
        // Buildings are stationary (camera-facing billboards, yaw 0). Mobile
        // units face their heading; the render z-axis is inverted vs the sim
        // (billboard flip), so the facing yaw negates the heading.
        // Buildings are stationary (camera-facing billboards, yaw 0). Mobile
        // units face their heading directly: the model's forward axis (+z)
        // maps to (sin yaw, cos yaw), matching the sim's movement vector.
        float facing = (u.type && u.type->canMove) ? u.heading : 0.0f;
        // Flyer models are built mirrored across the N-S axis relative to the
        // ground units, so they read correct N/S but backward E/W. Negating
        // the heading reflects E<->W (and diagonals) while leaving N/S.
        // Flyer models read backward vs the ground units, and the running fly
        // pose adds a further 180° body turn, so face them at -heading + pi.
        // Verified against a ground unit moving in each direction.
        if (u.type && u.type->canFly) facing = 3.14159265f - facing;
        collect(vt->second.model.root, base, anim, facing, u.team);
        std::stable_sort(tris_.begin(), tris_.end(),
                  [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
        float zm = mapView_.zoom();
        float ax = (u.x - mapView_.offX()) * zm;
        float ay = (u.z - mapView_.offY()) * zm;
        // Conjuring: while a unit is built/summoned it fades in from ~0 to 100%
        // (its hp fills 5%->100%) with a shimmering cyan pulse that fades out
        // as it finishes materialising.
        bool conjuring = u.underConstruction && u.type;
        float p = conjuring ? std::clamp(u.hp / u.type->maxHp, 0.0f, 1.0f) : 1.0f;
        Uint8 alpha = Uint8(p * 255.0f);
        for (auto& t : tris_) {
            SDL_Vertex v[3];
            for (int i = 0; i < 3; ++i) {
                v[i] = t.v[i];
                v[i].position.x = v[i].position.x * zm + ax;
                v[i].position.y = v[i].position.y * zm + ay;
                if (conjuring) {
                    float pulse = 0.5f + 0.5f * std::sin(animClock_ * 7.0f +
                                                         v[i].position.y * 0.03f);
                    float glow = (1.0f - p) * pulse;   // 0 once fully formed
                    v[i].color.a = alpha;
                    v[i].color.r = Uint8(v[i].color.r * (1.0f - 0.75f * glow));
                    v[i].color.g = Uint8(v[i].color.g * (1.0f - 0.25f * glow));
                    // blue channel left high, so the shimmer reads cyan
                }
            }
            SDL_RenderGeometry(ren_, t.tex, v, 3, nullptr, 0);
        }

        // Conjure effect: sprinkle the faction's build/summon sparkle over the
        // footprint while the unit materialises, each staggered so they twinkle
        // out of sync.
        if (conjuring) {
            std::string side = u.type->side;
            std::transform(side.begin(), side.end(), side.begin(), ::tolower);
            auto fit = buildFx_.find(side);
            if (fit != buildFx_.end() && !fit->second.empty()) {
                auto& frames = fit->second;
                int fw, fh;
                SDL_QueryTexture(frames[0], nullptr, nullptr, &fw, &fh);
                float tw = float(fw) * zm, th = float(fh) * zm;
                float fpw = std::max(u.type->footX, 1) * 16.0f * zm;
                float fph = std::max(u.type->footZ, 1) * 16.0f * zm;
                int nx = std::clamp(int(fpw / tw + 0.5f), 1, 5);
                int nz = std::clamp(int(fph / th + 0.5f), 1, 5);
                float x0 = ax - fpw * 0.5f, y0 = ay - fph * 0.6f;
                int base = int(animClock_ * 12);
                for (int gz = 0; gz < nz; ++gz)
                    for (int gx = 0; gx < nx; ++gx) {
                        SDL_Texture* fx = frames[size_t(base + gx * 3 + gz * 5) %
                                                 frames.size()];
                        SDL_FRect d{x0 + (gx + 0.5f) * fpw / nx - tw * 0.5f,
                                    y0 + (gz + 0.5f) * fph / nz - th * 0.5f, tw, th};
                        SDL_RenderCopyF(ren_, fx, nullptr, &d);
                    }
            }
        }
    }

    // Project model triangles relative to the unit anchor: yaw by heading,
    // fixed tilt so models read against TAK's painted top-down terrain.
    void collect(const tak::tdo::Object& o, const Xform& parent, const Anim* anim,
                 float heading, int team) {
        static const float kNoRot[3] = {0, 0, 0};
        const tak::cob::PieceState* ps = pieceFor(anim, o.name);
        if (ps && !ps->visible) return;
        Xform xf = parent.then(o.x + (ps ? ps->move[0] : 0),
                               o.y + (ps ? ps->move[1] : 0),
                               o.z + (ps ? ps->move[2] : 0),
                               ps ? ps->rot : kNoRot);
        // Hidden pieces: ground-reference plates (AraGP, araground, zonnull,
        // zon_gpoly, ...) and deactivated-state duplicates (*off), which the
        // game shows only via activation scripts we don't run.
        std::string oname = o.name;
        std::transform(oname.begin(), oname.end(), oname.begin(), ::tolower);
        auto ends = [&](const char* suf) {
            size_t n = std::strlen(suf);
            return oname.size() >= n && oname.compare(oname.size() - n, n, suf) == 0;
        };
        bool groundPlate = ends("gp") || ends("null") || ends("off") ||
                           oname.find("ground") != std::string::npos ||
                           oname.find("gpoly") != std::string::npos ||
                           oname.find("gpoint") != std::string::npos;
        const float tilt = gTilt;
        float cy = std::cos(heading), sy = std::sin(heading);
        float ct = std::cos(tilt), st = std::sin(tilt);
        for (const auto& p : o.primitives) {
            if (groundPlate) break;
            if (p.indices.size() < 3) continue;
            SDL_Texture* tex = nullptr;
            if (!p.texture.empty()) {
                std::string name = p.texture;
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                auto it = textures_.find(name);
                if (it != textures_.end() && !it->second.empty())
                    tex = it->second[size_t(team) < it->second.size() ? size_t(team)
                                                                      : 0];
            }
            for (size_t i = 1; i + 1 < p.indices.size(); ++i) {
                size_t idx[3] = {0, i, i + 1};
                Tri tri{};
                tri.tex = tex;
                float depth = 0;
                bool ok = true;
                for (int k = 0; k < 3; ++k) {
                    size_t vi = size_t(p.indices[idx[k]]) * 3;
                    if (vi + 2 >= o.vertices.size()) { ok = false; break; }
                    float w[3];
                    xf.apply(o.vertices[vi], o.vertices[vi + 1], o.vertices[vi + 2], w);
                    float rx = w[0] * cy + w[2] * sy;
                    float rz = -w[0] * sy + w[2] * cy;
                    // TAK billboards lean back (+y and +z together); moving
                    // away (+z) reads upward on screen, adding to height.
                    float ry = w[1] * ct + rz * st;
                    depth += rz * ct - w[1] * st;
                    tri.v[k].position = {rx, -ry};
                    static const SDL_FPoint uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                    tri.v[k].tex_coord = uv[idx[k] & 3];
                    tri.v[k].color = tex ? SDL_Color{255, 255, 255, 255}
                                         : SDL_Color{170, 170, 180, 255};
                }
                if (!ok) continue;
                tri.depth = depth / 3;
                tris_.push_back(tri);
            }
        }
        for (const auto& c : o.children) collect(c, xf, anim, heading, team);
    }

    void drawBrackets(float wx, float wz, float r) {
        // TAK-style selection: four green corner chevrons, slightly flattened.
        float zm = mapView_.zoom();
        float cx = (wx - mapView_.offX()) * zm, cy = (wz - mapView_.offY()) * zm;
        float rx = r * zm, ry = r * 0.65f * zm, L = r * 0.45f * zm;
        SDL_SetRenderDrawColor(ren_, 70, 240, 90, 255);
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2) {
                float px = cx + sx * rx, py = cy + sy * ry;
                SDL_RenderDrawLineF(ren_, px, py, px - sx * L, py);
                SDL_RenderDrawLineF(ren_, px, py, px, py - sy * L * 0.65f);
            }
    }

    void drawRing(float wx, float wz, float r) {
        float zm = mapView_.zoom();
        SDL_SetRenderDrawColor(ren_, 90, 255, 120, 255);
        SDL_FPoint pts[25];
        for (int i = 0; i <= 24; ++i) {
            float a = float(i) / 24 * 2 * 3.14159f;
            pts[i] = {(wx + std::cos(a) * r - mapView_.offX()) * zm,
                      (wz + std::sin(a) * r * 0.7f - mapView_.offY()) * zm};
        }
        SDL_RenderDrawLinesF(ren_, pts, 25);
    }

    SDL_Renderer* ren_;
    MapView mapView_;
    std::string dataRoot_;
    std::string ipRoot_;
    std::string side_ = "ara";
    tak::sim::TypeRegistry registry_;
    tak::sim::World world_;
    std::map<std::string, Visual> visuals_;
    std::map<int, std::string> unitType_;
    std::map<int, Anim> anims_;
    std::map<std::string, std::vector<SDL_Texture*>> textures_;
    std::vector<Tri> tris_;
    std::vector<int> selection_;
    bool dragging_ = false;
    bool buildDrag_ = false;          // shift-drag placing a line of buildings
    float bdX0_ = 0, bdZ0_ = 0;       // build-drag start (world)
    bool draggingMinimap_ = false;
    float dragX0_ = 0, dragY0_ = 0, dragX1_ = 0, dragY1_ = 0;
    char pendingCmd_ = 0;   // armed order awaiting a click: 'f' fight-move,
                            // 'm' move, 'a' attack, 'p' patrol, 'g' guard
    std::map<int, std::vector<int>> groups_;   // control groups 0-9
    bool paused_ = false;
    int winW_ = 0, winH_ = 0;   // last-known window size (for centering/culling)
    tak::net::Session* net_ = nullptr;
    int localTeam_ = 0;
    uint32_t netTick_ = 0;
    std::string netError_;
    bool follow_ = false;
    bool trace_ = false;
public:
    bool noFog_ = false;
private:
    struct SpawnRule {
        std::string type;
        float x = 0, z = 0;
        float atTime = -1;        // >= 0: spawn once at this time
        int maintainCount = 0;    // > 0: respawn while count(maintainType) < N
        std::string maintainType;
        tak::crt::Region maintainRect;
        int team = 3;
        bool done = false;
        float cooldown = 0;
    };
    std::vector<SpawnRule> spawnRules_;
    std::vector<std::pair<float, std::string>> messages_;
    const tak::sim::UnitType* scenUnit_ = nullptr;
    tak::crt::Region scenRegion_;
    float scenTime_ = 0, scenClock_ = 0, scenClock2_ = 0;
    int keepId_ = -1, aiKeepId_ = -1, builderId_ = -1;
    int playerMonarchId_ = -1, aiMonarchId_ = -1;
    std::string aiKeepType_;    // production building the AI Monarch builds
    std::string aiLodeType_;    // AI lodestone type (economy)
    std::string aiBuilderType_; // keepless (Zhon) AI: the Handler the Monarch builds
    int aiLodes_ = 0;           // lodestones the AI has queued so far
    int aiHandlers_ = 0;        // Beast Handlers the AI has queued so far
    const tak::sim::UnitType* placing_ = nullptr;
    float mouseX_ = 0, mouseY_ = 0;
    SDL_Texture* fogTex_ = nullptr;
    SDL_Texture* miniTex_ = nullptr;
    SDL_Texture* panelTex_ = nullptr;
    int panelW_ = 0, panelH_ = 0;
    SDL_Texture* botTex_ = nullptr;
    int botW_ = 0, botH_ = 0;
    std::map<std::string, SDL_Texture*> icons_;
    std::vector<std::pair<SDL_FRect, const tak::sim::UnitType*>> iconRects_;
    int aiTrained_ = 0;
    std::array<std::string, 4> aiCycle_ = {"tararch", "tartb", "tararch", "tarbeak"};
    float aiTimer_ = 0;
    static constexpr int kMiniSize = 180;

    SDL_FRect minimapRect(int winW, int winH) const {
        (void)winH;
        float aspect = float(mapView_.map().blocksY) / float(mapView_.map().blocksX);
        return {float(winW) - kMiniSize - 10, 10, kMiniSize, kMiniSize * aspect};
    }

    void buildMinimap() {
        int bw = mapView_.map().blocksX, bh = mapView_.map().blocksY;
        std::vector<uint8_t> pix(size_t(bw) * bh * 4);
        std::vector<uint8_t> block(32 * 32 * 4);
        for (int bz = 0; bz < bh; ++bz)
            for (int bx = 0; bx < bw; ++bx) {
                mapView_.compositor().renderBlock(mapView_.map(), bx, bz, block, 32, 0, 0);
                uint32_t r = 0, g = 0, b = 0;
                for (size_t i = 0; i < block.size(); i += 4) {
                    r += block[i]; g += block[i + 1]; b += block[i + 2];
                }
                size_t n = block.size() / 4;
                uint8_t* p = &pix[(size_t(bz) * bw + bx) * 4];
                p[0] = uint8_t(r / n); p[1] = uint8_t(g / n); p[2] = uint8_t(b / n);
                p[3] = 255;
            }
        miniTex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STATIC, bw, bh);
        SDL_UpdateTexture(miniTex_, nullptr, pix.data(), bw * 4);
        SDL_SetTextureScaleMode(miniTex_, SDL_ScaleModeLinear);
    }

    void drawMinimap(int winW, int winH) {
        (void)winW;
        if (!miniTex_) buildMinimap();
        SDL_FRect r = minimapRect(winW, winH);
        SDL_FRect frame{r.x - 2, r.y - 2, r.w + 4, r.h + 4};
        SDL_SetRenderDrawColor(ren_, 30, 30, 40, 255);
        SDL_RenderFillRectF(ren_, &frame);
        SDL_RenderCopyF(ren_, miniTex_, nullptr, &r);
        if (fogTex_) SDL_RenderCopyF(ren_, fogTex_, nullptr, &r);

        float mapW = float(mapView_.map().blocksX) * 32;
        float mapH = float(mapView_.map().blocksY) * 32;
        auto toMini = [&](float wx, float wz) {
            return SDL_FPoint{r.x + wx / mapW * r.w, r.y + wz / mapH * r.h};
        };
        for (const auto& u : world_.units()) {
            if (!u.alive() || u.embarked() || !u.type) continue;
            if (u.team != localTeam_ && !world_.cellVisible(u.x, u.z)) continue;
            SDL_FPoint p = toMini(u.x, u.z);
            SDL_FRect dot{p.x - 1.5f, p.y - 1.5f, 3, 3};
            if (u.team == localTeam_) SDL_SetRenderDrawColor(ren_, 90, 160, 255, 255);
            else SDL_SetRenderDrawColor(ren_, 255, 80, 60, 255);
            SDL_RenderFillRectF(ren_, &dot);
        }
        // Camera view rectangle.
        float zm = mapView_.zoom();
        SDL_FPoint a = toMini(mapView_.offX(), mapView_.offY());
        SDL_FRect view{a.x, a.y, winW / zm / mapW * r.w,
                       (winH - kBarH) / zm / mapH * r.h};
        SDL_SetRenderDrawColor(ren_, 240, 240, 240, 200);
        SDL_RenderDrawRectF(ren_, &view);
    }

    // Returns true if the click was inside the minimap (and moved the camera).
    bool minimapClick(float mx, float my, int winW, int winH) {
        SDL_FRect r = minimapRect(winW, winH);
        if (mx < r.x || my < r.y || mx > r.x + r.w || my > r.y + r.h) return false;
        float mapW = float(mapView_.map().blocksX) * 32;
        float mapH = float(mapView_.map().blocksY) * 32;
        float wx = (mx - r.x) / r.w * mapW, wz = (my - r.y) / r.h * mapH;
        float zm = mapView_.zoom();
        mapView_.setOffset(wx - winW / zm / 2, wz - winH / zm / 2);
        return true;
    }

    struct FeatureInst {
        SDL_Texture* tex = nullptr;
        const std::vector<SDL_Texture*>* frames = nullptr;
        int seed = 0;
        SDL_Texture* shadow = nullptr;
        int w = 0, h = 0, xoff = 0, yoff = 0;
        int sw = 0, sh = 0, sxoff = 0, syoff = 0;
        float x = 0, z = 0;
        bool mana = false;   // a Sacred Stone mana deposit (lodestone spot)
    };
    std::vector<FeatureInst> features_;
    std::vector<std::pair<float, float>> manaSpots_;   // Sacred Stone deposits

    struct FeatArt {
        SDL_Texture* tex = nullptr;
        SDL_Texture* shadow = nullptr;
        int w = 0, h = 0, xoff = 0, yoff = 0;
        int sw = 0, sh = 0, sxoff = 0, syoff = 0;
        std::vector<SDL_Texture*> frames;   // >1 entries when animating
    };
    std::map<std::string, tak::tdf::Node> featureDefs_;
    std::map<std::string, tak::gaf::Palette> featurePals_;
    std::map<std::string, FeatArt> featureArt_;

    void loadFeatureDefs() {
        if (!featureDefs_.empty()) return;
        try {
            for (const auto& e : std::filesystem::recursive_directory_iterator(
                     dataRoot_ + "/features")) {
                if (e.path().extension() != ".tdf") continue;
                try {
                    auto root = tak::tdf::parse(e.path());
                    for (const auto& n : root.childOrder) {
                        std::string k = n;
                        std::transform(k.begin(), k.end(), k.begin(), ::tolower);
                        featureDefs_[k] = root.children.at(n);
                    }
                } catch (const std::exception&) {}
            }
        } catch (const std::exception&) {}
    }

    const tak::gaf::Palette* featurePalette(std::string world) {
        std::transform(world.begin(), world.end(), world.begin(), ::tolower);
        auto it = featurePals_.find(world);
        if (it != featurePals_.end()) return &it->second;
        const std::string cands[] = {world + "_features.pcx", world + ".pcx",
                                     std::string("aramon_features.pcx")};
        for (const std::string& cand : cands) {
            try {
                return &featurePals_
                            .emplace(world, tak::gaf::Palette::load(
                                                dataRoot_ + "/palettes/" + cand))
                            .first->second;
            } catch (const std::exception&) {}
        }
        return nullptr;
    }

    FeatArt* featureArtFor(const tak::tdf::Node& def) {
        std::string file = def.valueOr("filename", "");
        std::string seq = def.valueOr("seqname", "");
        std::string seqShad = def.valueOr("seqnameshad", "");
        std::string key = file + "|" + seq;
        auto it = featureArt_.find(key);
        if (it != featureArt_.end()) return it->second.tex ? &it->second : nullptr;
        FeatArt a{};
        const auto* pal = featurePalette(def.valueOr("world", "aramon"));
        if (pal) {
            try {
                std::string f = file;
                std::transform(f.begin(), f.end(), f.begin(), ::tolower);
                auto ieq = [](const std::string& x, const std::string& y) {
                    if (x.size() != y.size()) return false;
                    for (size_t i = 0; i < x.size(); ++i)
                        if (std::tolower(x[i]) != std::tolower(y[i])) return false;
                    return true;
                };
                for (auto& sq : tak::gaf::load(dataRoot_ + "/anims/" + f + ".gaf", *pal)) {
                    if (sq.frames.empty() || sq.frames[0].width == 0) continue;
                    auto& fr = sq.frames[0];
                    if (ieq(sq.name, seq)) {
                        bool animate = def.numberOr("animating", 0) != 0 ||
                                       def.numberOr("animatable", 0) != 0;
                        size_t nf = animate ? sq.frames.size() : 1;
                        for (size_t fi = 0; fi < nf; ++fi) {
                            auto& ff = sq.frames[fi];
                            if (ff.width == 0 || ff.height != fr.height ||
                                ff.width != fr.width)
                                continue;   // keep uniform dimensions only
                            SDL_Texture* t = SDL_CreateTexture(
                                ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                ff.width, ff.height);
                            SDL_UpdateTexture(t, nullptr, ff.rgba.data(), ff.width * 4);
                            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                            a.frames.push_back(t);
                        }
                        if (!a.frames.empty()) {
                            a.tex = a.frames[0];
                            a.w = fr.width; a.h = fr.height;
                            a.xoff = fr.xoff; a.yoff = fr.yoff;
                        }
                    } else if (!seqShad.empty() && ieq(sq.name, seqShad)) {
                        // Shadow: silhouette drawn as translucent black.
                        std::vector<uint8_t> px = fr.rgba;
                        for (size_t i = 0; i + 3 < px.size(); i += 4) {
                            px[i] = px[i + 1] = px[i + 2] = 0;
                            px[i + 3] = px[i + 3] ? 90 : 0;
                        }
                        a.shadow = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                                     SDL_TEXTUREACCESS_STATIC, fr.width,
                                                     fr.height);
                        SDL_UpdateTexture(a.shadow, nullptr, px.data(), fr.width * 4);
                        SDL_SetTextureBlendMode(a.shadow, SDL_BLENDMODE_BLEND);
                        a.sw = fr.width; a.sh = fr.height;
                        a.sxoff = fr.xoff; a.syoff = fr.yoff;
                    }
                }
            } catch (const std::exception&) {}
        }
        featureArt_[key] = a;
        return featureArt_[key].tex ? &featureArt_[key] : nullptr;
    }

    // Place one feature instance by definition name; returns success.
    bool addFeature(const std::string& rawName, float x, float z, bool blockNav) {
        loadFeatureDefs();
        std::string key = rawName;
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        auto di = featureDefs_.find(key);
        if (di == featureDefs_.end()) return false;
        FeatArt* a = featureArtFor(di->second);
        if (!a) return false;
        FeatureInst inst;
        inst.tex = a->tex;
        inst.frames = &a->frames;
        inst.seed = int(features_.size() * 7);
        inst.shadow = a->shadow;
        inst.w = a->w; inst.h = a->h; inst.xoff = a->xoff; inst.yoff = a->yoff;
        inst.sw = a->sw; inst.sh = a->sh; inst.sxoff = a->sxoff; inst.syoff = a->syoff;
        inst.x = x;
        inst.z = z;
        // Mana deposits ("Sacred Stone", category=Mana) are the spots you build
        // lodestones ON, so they must stay buildable (walkable) — never block
        // the nav grid for them, or canPlace rejects the deposit itself.
        std::string cat = di->second.valueOr("category", "");
        std::transform(cat.begin(), cat.end(), cat.begin(), ::tolower);
        inst.mana = (cat == "mana");
        features_.push_back(inst);
        if (blockNav && !inst.mana) {
            int fx = int(di->second.numberOr("footprintx", 1));
            int fz = int(di->second.numberOr("footprintz", 1));
            world_.nav().block(int(x) / 16 - fx / 2, int(z) / 16 - fz / 2, fx, fz, true);
        }
        return true;
    }

    void loadFeatures() {
        const auto& names = mapView_.map().featureNames;
        if (names.empty()) return;
        loadFeatureDefs();
        const auto& map = mapView_.map();
        int placed = 0;
        for (int cz = 0; cz < map.height; ++cz)
            for (int cx = 0; cx < map.width; ++cx) {
                uint16_t v = map.features[size_t(cz) * map.width + cx];
                if (v >= names.size()) continue;
                if (addFeature(names[v], float(cx) * 16 + 8, float(cz) * 16 + 8, true))
                    ++placed;
            }
        std::printf("features: %d placed\n", placed);
        // Register the Sacred Stone deposits so lodestones can only build on
        // them (and the AI knows where to put them).
        manaSpots_.clear();
        for (const auto& f : features_)
            if (f.mana) manaSpots_.push_back({f.x, f.z});
        world_.setManaSpots(manaSpots_);
    }

    // Track numbers for a side from gamedata/sidedata.tdf (falls back to IP).
    std::vector<int> factionMusicTracks(const std::string& side) {
        std::string want = side;   // "ara" -> match "ARAMON" etc.
        std::transform(want.begin(), want.end(), want.begin(), ::toupper);
        const std::string prefixes[] = {
            std::string("ARA=ARAMON"), "TAR=TAROS", "VER=VERUNA", "ZON=ZHON",
            "CRE=CREON"};
        std::string full;
        for (const auto& m : prefixes)
            if (m.substr(0, 3) == want) full = m.substr(4);
        std::vector<int> out;
        for (const std::string root : {dataRoot_, ipRoot_}) {
            if (root.empty()) continue;
            try {
                auto sd = tak::tdf::parse(root + "/gamedata/sidedata.tdf");
                for (const auto& key : sd.childOrder) {
                    const auto& sec = sd.children.at(key);
                    std::string nm = sec.valueOr("name", "");
                    std::transform(nm.begin(), nm.end(), nm.begin(), ::toupper);
                    if (nm != full) continue;
                    std::istringstream ts(sec.valueOr("musictracks", ""));
                    int t;
                    while (ts >> t) out.push_back(t);
                    if (!out.empty()) return out;
                }
            } catch (const std::exception&) {}
        }
        return out;
    }

    void loadPanel(const std::string& side) {
        std::string base = dataRoot_ + "/anims/" + side + "ingame";
        if (!std::filesystem::exists(base + ".gaf") && !ipRoot_.empty())
            base = ipRoot_ + "/anims/" + side + "ingame";
        try {
            auto pal = tak::gaf::Palette::load(base + ".pcx");
            for (auto& sq : tak::gaf::load(base + ".gaf", pal)) {
                if (sq.frames.empty()) continue;
                auto& f = sq.frames[0];
                if (sq.name == "AidPanel" || sq.name == "MainPanel") {
                    panelTex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                                  SDL_TEXTUREACCESS_STATIC, f.width,
                                                  f.height);
                    SDL_UpdateTexture(panelTex_, nullptr, f.rgba.data(), f.width * 4);
                    panelW_ = f.width;
                    panelH_ = f.height;
                } else if (sq.name == "AidBotPanel" || sq.name == "BottomPanel") {
                    botTex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                                SDL_TEXTUREACCESS_STATIC, f.width,
                                                f.height);
                    SDL_UpdateTexture(botTex_, nullptr, f.rgba.data(), f.width * 4);
                    botW_ = f.width;
                    botH_ = f.height;
                }
            }
        } catch (const std::exception&) {}
    }

    static constexpr int kBarH = 72;

    struct OrderBtn {
        SDL_Texture* frames[3] = {nullptr, nullptr, nullptr};   // normal/hover/armed
        int w = 0, h = 0;
        char cmd = 0;        // 'm','a','p'; 0 = stop (instant)
        const char* label;
    };
    std::vector<OrderBtn> orderBtns_;

    // The per-faction conjure/build effect animation (TAF), keyed by side.
    std::map<std::string, std::vector<SDL_Texture*>> buildFx_;

    void loadBuildFx() {
        static const std::pair<const char*, const char*> maps[] = {
            {"ara", "aramonbuild"}, {"tar", "tarosbuild"},
            {"ver", "verunabuild"}, {"zon", "zhonbuild"}, {"cre", "zhonbuild"},
        };
        for (auto& [side, file] : maps) {
            try {
                // TAF frames are raw ARGB; the palette arg is ignored for them.
                auto pal = tak::gaf::Palette::load(dataRoot_ +
                                                   "/palettes/ara_textures.pcx");
                auto seqs = tak::gaf::load(
                    dataRoot_ + "/anims/" + std::string(file) + "_4444.taf", pal);
                if (seqs.empty()) continue;
                auto& frames = buildFx_[side];
                for (auto& fr : seqs[0].frames) {
                    if (fr.width == 0) continue;
                    SDL_Texture* t = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                                       SDL_TEXTUREACCESS_STATIC,
                                                       fr.width, fr.height);
                    SDL_UpdateTexture(t, nullptr, fr.rgba.data(), fr.width * 4);
                    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
                    frames.push_back(t);
                }
            } catch (const std::exception&) {}
        }
    }

    void loadOrderButtons() {
        auto grab = [&](const char* gaf, const char* seq, char cmd,
                        const char* label, int f0 = 0, int f1 = 1, int f2 = 2) {
            try {
                auto pal = tak::gaf::Palette::load(dataRoot_ + "/anims/" +
                                                   std::string(gaf) + ".pcx");
                for (auto& sq : tak::gaf::load(
                         dataRoot_ + "/anims/" + std::string(gaf) + ".gaf", pal)) {
                    if (sq.name != seq || sq.frames.size() < 3) continue;
                    OrderBtn b;
                    b.cmd = cmd;
                    b.label = label;
                    int idx[3] = {f0, f1, f2};
                    for (int i = 0; i < 3; ++i) {
                        auto& f = sq.frames[size_t(idx[i])];
                        if (f.width == 0) continue;
                        b.frames[i] = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                                        SDL_TEXTUREACCESS_STATIC,
                                                        f.width, f.height);
                        SDL_UpdateTexture(b.frames[i], nullptr, f.rgba.data(),
                                          f.width * 4);
                        SDL_SetTextureBlendMode(b.frames[i], SDL_BLENDMODE_BLEND);
                        b.w = f.width;
                        b.h = f.height;
                    }
                    if (b.frames[0]) orderBtns_.push_back(b);
                }
            } catch (const std::exception&) {}
        };
        grab("actionbuttons", "MoveButton", 'm', "MOVE");
        grab("actionbuttons", "AttackButton", 'a', "ATTACK");
        grab("actionbuttons", "PatrolButton", 'p', "PATROL");
        grab("actionbuttons", "GuardButton", 'g', "GUARD");
        // Fight-move (Keys.TDF LOWER_F) reuses the Attack glyph, tinted.
        grab("actionbuttons", "AttackButton", 'f', "FIGHT-MOVE");
        grab("igcommonbuttons", "StopButton", 0, "STOP", 1, 2, 3);
    }

    SDL_FRect orderBtnRect(size_t i, int winW) const {
        return {float(winW) - 54, 220 + float(i) * 52, 44, 44};
    }

    void drawOrderColumn(int winW, int winH) {
        (void)winH;
        if (orderBtns_.empty() || selection_.empty()) return;
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        SDL_FRect col{float(winW) - 60, 210, 56,
                      float(orderBtns_.size()) * 52 + 12};
        SDL_SetRenderDrawColor(ren_, 20, 18, 16, 170);
        SDL_RenderFillRectF(ren_, &col);
        SDL_SetRenderDrawColor(ren_, 120, 105, 80, 255);
        SDL_RenderDrawRectF(ren_, &col);
        for (size_t i = 0; i < orderBtns_.size(); ++i) {
            const auto& b = orderBtns_[i];
            SDL_FRect r = orderBtnRect(i, winW);
            bool hot = mouseX_ >= r.x && mouseX_ <= r.x + r.w && mouseY_ >= r.y &&
                       mouseY_ <= r.y + r.h;
            bool armed = b.cmd && pendingCmd_ == b.cmd;
            SDL_Texture* t = armed && b.frames[2] ? b.frames[2]
                             : hot && b.frames[1] ? b.frames[1]
                                                  : b.frames[0];
            SDL_RenderCopyF(ren_, t, nullptr, &r);
            if (armed) {
                SDL_SetRenderDrawColor(ren_, 255, 220, 90, 255);
                SDL_RenderDrawRectF(ren_, &r);
            }
            if (hot && hudFont_.ok()) {
                float tw = float(hudFont_.width(b.label, 1.3f));
                hudFont_.draw(ren_, b.label, r.x - tw - 8, r.y + 26, 1.3f,
                              {235, 225, 180, 255});
            }
        }
    }

    // Returns true if the click hit (and was handled by) the order column.
    bool orderColumnClick(float mx, float my, int winW) {
        if (orderBtns_.empty() || selection_.empty()) return false;
        for (size_t i = 0; i < orderBtns_.size(); ++i) {
            SDL_FRect r = orderBtnRect(i, winW);
            if (mx < r.x || mx > r.x + r.w || my < r.y || my > r.y + r.h) continue;
            const auto& b = orderBtns_[i];
            if (b.cmd) {
                pendingCmd_ = b.cmd;
            } else {
                for (int id : selection_) {
                    tak::net::Command c;
                    c.kind = tak::net::Cmd::Stop;
                    c.unitId = id;
                    issue(c);
                }
            }
            return true;
        }
        return false;
    }

    SDL_Texture* iconFor(const std::string& typeId) {
        auto it = icons_.find(typeId);
        if (it != icons_.end()) return it->second;
        SDL_Texture* tex = nullptr;
        for (const std::string root : {dataRoot_, ipRoot_}) {
            if (root.empty()) continue;
            try {
                auto img = tak::jpeg::load(root + "/anims/buildpic/" + typeId + ".jpg");
                tex = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_STATIC, img.width,
                                        img.height);
                SDL_UpdateTexture(tex, nullptr, img.rgba.data(), img.width * 4);
                break;
            } catch (const std::exception&) {}
        }
        icons_[typeId] = tex;
        return tex;
    }

    // The selected builder (any builder in the selection).
    const tak::sim::Unit* selectedBuilder() {
        for (int id : selection_) {
            const auto* u = world_.unit(id);
            if (u && u->alive() && u->type && u->type->isBuilder) return u;
        }
        return nullptr;
    }

    // Keys.TDF-derived hotkeys. Returns true when the key was consumed.
    bool handleKey(SDL_Keycode key, uint16_t mod) {
        bool ctrl = (mod & KMOD_CTRL) != 0;
        bool shift = (mod & KMOD_SHIFT) != 0;

        // Pause toggle works without a selection.
        if (key == SDLK_PAUSE) { paused_ = !paused_; return true; }

        // Control groups on the number row: plain digit recalls, CTRL assigns,
        // CTRL+SHIFT appends the current selection. Digit 0 is group 10.
        int digit = -1;
        if (key >= SDLK_0 && key <= SDLK_9) digit = int(key - SDLK_0);
        if (digit >= 0) {
            int g = digit == 0 ? 10 : digit;
            if (ctrl && shift) {   // add selection to the group
                auto& grp = groups_[g];
                for (int id : selection_)
                    if (std::find(grp.begin(), grp.end(), id) == grp.end())
                        grp.push_back(id);
            } else if (ctrl) {     // (re)assign the group
                groups_[g] = selection_;
            } else {               // recall, dropping dead members
                selection_.clear();
                for (int id : groups_[g])
                    if (const auto* u = world_.unit(id); u && u->alive())
                        selection_.push_back(id);
                if (!selection_.empty()) {
                    centerOn(selection_.front());
                    voice(selection_.front(), "select");
                }
            }
            return true;
        }

        // Selection commands (modifier-based, no armed order).
        if (ctrl && key == SDLK_a) { selectOwned([](const tak::sim::Unit&){ return true; });
                                     return true; }
        if (ctrl && key == SDLK_z) {   // all of the currently-selected type
            const auto* first = selection_.empty() ? nullptr
                                                    : world_.unit(selection_.front());
            const auto* t = first ? first->type : nullptr;
            if (t) selectOwned([t](const tak::sim::Unit& u){ return u.type == t; });
            return true;
        }
        if (ctrl && key == SDLK_u) {   // everything visible on screen
            selectOwned([this](const tak::sim::Unit& u){ return onScreen(u); });
            return true;
        }
        if (ctrl) return false;   // other CTRL combos fall through to the map view

        // Order commands need at least one selected unit.
        if (selection_.empty()) return false;
        switch (key) {
            case SDLK_f: pendingCmd_ = 'f'; return true;   // fight-move
            case SDLK_m: pendingCmd_ = 'm'; return true;   // move
            case SDLK_a: pendingCmd_ = 'a'; return true;   // attack
            case SDLK_p: pendingCmd_ = 'p'; return true;   // patrol
            case SDLK_g: pendingCmd_ = 'g'; return true;   // guard
            case SDLK_s:                                   // stop (immediate)
                for (int id : selection_) {
                    tak::net::Command c;
                    c.kind = tak::net::Cmd::Stop;
                    c.unitId = id;
                    issue(c);
                }
                pendingCmd_ = 0;
                return true;
            case SDLK_n: cycleNextUnit(); return true;     // next unit
            default: return false;
        }
    }

    // Replace the selection with every owned, living unit matching `pred`.
    template <class Pred>
    void selectOwned(Pred pred) {
        selection_.clear();
        for (auto& u : world_.units())
            if (u.alive() && u.team == localTeam_ && u.type && pred(u))
                selection_.push_back(u.id);
        if (!selection_.empty()) voice(selection_.front(), "select");
    }

    bool onScreen(const tak::sim::Unit& u) const {
        float sx = (u.x - mapView_.offX()) * mapView_.zoom();
        float sy = (u.z - mapView_.offY()) * mapView_.zoom();
        return sx >= 0 && sy >= 0 && sx <= float(winW_) && sy <= float(winH_) - kBarH;
    }

    // Cycle the selection to the next owned unit (single-select stepping).
    void cycleNextUnit() {
        std::vector<int> owned;
        for (auto& u : world_.units())
            if (u.alive() && u.team == localTeam_ && u.type && u.type->canMove)
                owned.push_back(u.id);
        if (owned.empty()) return;
        int cur = selection_.empty() ? -1 : selection_.front();
        auto it = std::find(owned.begin(), owned.end(), cur);
        int next = (it == owned.end() || it + 1 == owned.end())
                       ? owned.front()
                       : *(it + 1);
        selection_ = {next};
        centerOn(next);
        voice(next, "select");
    }

    void centerOn(int id) {
        const auto* u = world_.unit(id);
        if (!u) return;
        mapView_.setOffset(u->x - (winW_ / 2.0f) / mapView_.zoom(),
                           u->z - (winH_ / 2.0f) / mapView_.zoom());
    }

    SDL_Color factionColor() const {
        if (side_ == "ara") return {70, 130, 240, 255};   // Aramon blue
        if (side_ == "tar") return {205, 65, 60, 255};    // Taros red
        if (side_ == "ver") return {60, 195, 195, 255};   // Veruna teal
        if (side_ == "zon") return {120, 205, 80, 255};   // Zhon green
        if (side_ == "cre") return {230, 145, 50, 255};   // Creon orange
        return {180, 170, 140, 255};
    }

    // HUD text with a full dark outline so it reads over any panel.
    void hudText(const std::string& s, float x, float y, float scale, SDL_Color c) {
        static const int o[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0},
                                    {1, 0},   {-1, 1}, {0, 1},  {1, 1}};
        for (auto& d : o)
            hudFont_.draw(ren_, s, x + d[0] * 1.3f, y + d[1] * 1.3f, scale,
                          {0, 0, 0, 220});
        hudFont_.draw(ren_, s, x, y, scale, c);
    }

    void drawPanel(int winW, int winH) {
        // Bottom bar: stone strip across the full width.
        SDL_FRect bar{0, float(winH - kBarH), float(winW), float(kBarH)};
        if (botTex_) {
            for (int x = 0; x < winW; x += botW_) {
                SDL_Rect dst{x, winH - kBarH, botW_, kBarH};
                SDL_RenderCopy(ren_, botTex_, nullptr, &dst);
            }
        } else if (panelTex_) {
            for (int x = 0; x < winW; x += panelW_) {
                SDL_Rect src{0, 40, panelW_, kBarH};
                SDL_Rect dst{x, winH - kBarH, panelW_, kBarH};
                SDL_RenderCopy(ren_, panelTex_, &src, &dst);
            }
        } else {
            SDL_SetRenderDrawColor(ren_, 42, 38, 34, 255);
            SDL_RenderFillRectF(ren_, &bar);
        }
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 90);
        SDL_RenderFillRectF(ren_, &bar);
        SDL_SetRenderDrawColor(ren_, 120, 105, 80, 255);
        SDL_RenderDrawLineF(ren_, 0, bar.y, float(winW), bar.y);

        char buf[96];
        SDL_Color fc = factionColor();
        auto shade = [&](float x, float w) {
            SDL_FRect z{x, bar.y + 4, w, kBarH - 8.0f};
            SDL_SetRenderDrawColor(ren_, fc.r / 4, fc.g / 4, fc.b / 4, 225);
            SDL_RenderFillRectF(ren_, &z);   // dark faction-tinted panel
            // double faction-coloured border for a clear frame
            SDL_SetRenderDrawColor(ren_, fc.r, fc.g, fc.b, 245);
            SDL_RenderDrawRectF(ren_, &z);
            SDL_FRect z2{z.x + 1, z.y + 1, z.w - 2, z.h - 2};
            SDL_SetRenderDrawColor(ren_, fc.r / 2, fc.g / 2, fc.b / 2, 200);
            SDL_RenderDrawRectF(ren_, &z2);
        };

        // Bottom-LEFT: portrait + stats for the selected unit.
        if (!selection_.empty() && hudFont_.ok()) {
            const auto* u = world_.unit(selection_.front());
            if (u && u->alive() && u->type) {
                float px = 8;
                shade(px, 248);
                SDL_Texture* ic = iconFor(u->type->id);
                if (ic) {
                    SDL_FRect pr{px + 4, bar.y + 8, 56, kBarH - 20.0f};
                    SDL_RenderCopyF(ren_, ic, nullptr, &pr);
                    SDL_SetRenderDrawColor(ren_, fc.r, fc.g, fc.b, 255);
                    SDL_RenderDrawRectF(ren_, &pr);
                }
                float tx = px + 70;
                hudText(u->type->name, tx, bar.y + 18, 1.7f, {255, 245, 215, 255});
                std::snprintf(buf, sizeof buf, "HP %d/%d", int(u->hp),
                              int(u->type->maxHp));
                hudText(buf, tx, bar.y + 42, 1.4f, {180, 245, 175, 255});
                if (selection_.size() > 1) {
                    std::snprintf(buf, sizeof buf, "+%zu MORE",
                                  selection_.size() - 1);
                    hudText(buf, tx, bar.y + 60, 1.3f, {210, 210, 195, 255});
                }
            }
        }

        // Bottom-CENTER: clickable build icons for the selected builder.
        iconRects_.clear();
        const auto* b = selectedBuilder();
        if (b) {
            const auto& menu = registry_.buildable(b->type->id);
            int n = std::min<int>(int(menu.size()), 10);
            float rowW = n > 0 ? n * 66.0f - 6.0f : 0;
            float x = (float(winW) - rowW) * 0.5f;
            for (int i = 0; i < n; ++i) {
                const auto* bt = registry_.find(menu[size_t(i)]);
                if (!bt) continue;
                SDL_FRect r{x, bar.y + 6, 60, kBarH - 16.0f};
                SDL_SetRenderDrawColor(ren_, 20, 18, 14, 235);
                SDL_FRect rb{r.x - 1, r.y - 1, r.w + 2, r.h + 2};
                SDL_RenderFillRectF(ren_, &rb);
                SDL_Texture* ic = iconFor(bt->id);
                if (ic) SDL_RenderCopyF(ren_, ic, nullptr, &r);
                else {
                    SDL_SetRenderDrawColor(ren_, 60, 55, 50, 255);
                    SDL_RenderFillRectF(ren_, &r);
                }
                bool hot = mouseX_ >= r.x && mouseX_ <= r.x + r.w &&
                           mouseY_ >= r.y && mouseY_ <= r.y + r.h;
                SDL_SetRenderDrawColor(ren_, hot ? 255 : 110, hot ? 230 : 100,
                                       hot ? 120 : 70, 255);
                SDL_RenderDrawRectF(ren_, &r);
                if (hot && hudFont_.ok()) {
                    char tip[80];
                    std::snprintf(tip, sizeof tip, "%s  %d MOGRIUM", bt->name.c_str(),
                                  int(bt->buildCost));
                    float tw = float(hudFont_.width(tip, 1.5f));
                    float tipx = std::clamp(r.x + 30 - tw / 2, 4.0f, winW - tw - 4);
                    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 200);
                    SDL_FRect tb{tipx - 4, bar.y - 30, tw + 8, 24};
                    SDL_RenderFillRectF(ren_, &tb);
                    hudText(tip, tipx, bar.y - 12, 1.5f, {255, 240, 190, 255});
                }
                iconRects_.push_back({r, bt});
                x += 66;
            }
            if (!b->buildQueue.empty() && hudFont_.ok()) {
                char q[64];
                std::snprintf(q, sizeof q, "TRAINING %s (%zu)",
                              b->buildQueue.front()->name.c_str(),
                              b->buildQueue.size());
                float qw = float(hudFont_.width(q, 1.4f));
                hudText(q, (float(winW) - qw) * 0.5f, bar.y - 28, 1.4f,
                        {160, 210, 255, 255});
            }
        }

        // Bottom-RIGHT: mogrium.
        if (hudFont_.ok()) {
            auto& tm = world_.team(localTeam_);
            float manaX = float(winW) - 176;
            shade(manaX - 8, 184);
            hudText("MOGRIUM", manaX, bar.y + 16, 1.3f, {150, 215, 255, 255});
            std::snprintf(buf, sizeof buf, "%d / %d", int(tm.mana),
                          int(std::max(tm.storage, 100.0f)));
            hudText(buf, manaX, bar.y + 40, 1.6f, {215, 238, 255, 255});
            std::snprintf(buf, sizeof buf, "+%d / sec", int(tm.income));
            hudText(buf, manaX, bar.y + 62, 1.3f, {170, 245, 190, 255});
        }
    }

    void drawFog() {
        if (noFog_) return;
        const auto& vis = world_.visibility();
        if (vis.empty()) return;
        int w = world_.visW(), h = world_.visH();
        if (!fogTex_) {
            fogTex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_STREAMING, w, h);
            SDL_SetTextureBlendMode(fogTex_, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(fogTex_, SDL_ScaleModeLinear);
        }
        void* px = nullptr;
        int pitch = 0;
        if (SDL_LockTexture(fogTex_, nullptr, &px, &pitch) == 0) {
            for (int z = 0; z < h; ++z) {
                uint32_t* row = reinterpret_cast<uint32_t*>(
                    static_cast<uint8_t*>(px) + size_t(z) * size_t(pitch));
                for (int x = 0; x < w; ++x) {
                    uint8_t v = vis[size_t(z) * w + x];
                    uint8_t a = v == 2 ? 0 : (v == 1 ? 110 : 235);
                    row[x] = uint32_t(a) << 24;   // black with alpha (RGBA32 LE)
                }
            }
            SDL_UnlockTexture(fogTex_);
        }
        float zm = mapView_.zoom();
        SDL_FRect dst{-mapView_.offX() * zm, -mapView_.offY() * zm,
                      float(w) * 16 * zm, float(h) * 16 * zm};
        SDL_RenderCopyF(ren_, fogTex_, nullptr, &dst);
    }

    // Positions along a build-drag line, spaced by the building's footprint.
    std::vector<std::pair<float, float>> buildLinePositions(
        float x0, float z0, float x1, float z1) const {
        std::vector<std::pair<float, float>> out;
        if (!placing_) return out;
        float sp = std::max({placing_->footX, placing_->footZ, 1}) * 16.0f;
        float dx = x1 - x0, dz = z1 - z0, len = std::sqrt(dx * dx + dz * dz);
        int n = int(len / sp);
        float ux = len > 1e-3f ? dx / len : 0, uz = len > 1e-3f ? dz / len : 0;
        for (int i = 0; i <= n; ++i)
            out.push_back({x0 + ux * sp * i, z0 + uz * sp * i});
        return out;
    }

    // Queue a whole line of the current building from a shift-drag.
    void placeBuildLine(float x0, float z0, float x1, float z1) {
        if (!placing_ || selection_.empty()) return;
        int builderId = selectedBuilder() ? selectedBuilder()->id : selection_.front();
        for (auto& [x, z] : buildLinePositions(x0, z0, x1, z1)) {
            if (!world_.canPlace(placing_, x, z)) continue;
            tak::net::Command c;
            c.kind = tak::net::Cmd::Build;
            c.unitId = builderId;
            c.x = x;
            c.z = z;
            c.queue = true;   // all queued; the first starts if the builder is free
            std::snprintf(c.type, sizeof c.type, "%s", placing_->id.c_str());
            issue(c);
        }
    }

    void drawGhost() {
        float zm = mapView_.zoom();
        float wx = mapView_.offX() + mouseX_ / zm;
        float wz = mapView_.offY() + mouseY_ / zm;
        bool ok = world_.canPlace(placing_, wx, wz);
        float hw = float(placing_->footX) * 8 * zm, hh = float(placing_->footZ) * 8 * zm;
        SDL_FRect r{(wx - mapView_.offX()) * zm - hw, (wz - mapView_.offY()) * zm - hh,
                    hw * 2, hh * 2};
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren_, ok ? 90 : 230, ok ? 220 : 60, 70, 90);
        SDL_RenderFillRectF(ren_, &r);
        SDL_SetRenderDrawColor(ren_, ok ? 120 : 255, ok ? 255 : 80, 90, 220);
        SDL_RenderDrawRectF(ren_, &r);
        if (hudFont_.ok())
            hudFont_.draw(ren_, placing_->name, r.x, r.y - 4, 1.5f,
                          {230, 230, 200, 255});
    }

    int rosterIndexOf(int unitId) {
        auto* u = world_.unit(unitId);
        if (!u || !u->type) return -1;
        for (size_t i = 0; i < missionRoster_.size(); ++i)
            if (missionRoster_[i] == u->type->id) return int(i);
        return -1;
    }

    int32_t mapCommand(int sub, const std::vector<int32_t>& a) {
        switch (sub) {
            case 0:   // define (and arm) region: rect or circle, cells
                if (a.size() == 5) regions_[a[0]] = {a[1], a[2], a[3], a[4], true, true};
                else if (a.size() == 4)
                    regions_[a[0]] = {a[1], a[2], a[3], 0, false, true};
                return 0;
            case 1:   // disarm region (one-shot triggers disarm themselves)
                if (!a.empty()) {
                    auto it = regions_.find(a[0]);
                    if (it != regions_.end()) it->second.armed = false;
                }
                return 0;
            case 2: {   // nearest unit of player a[0] to cell (a[1],a[2])
                if (a.size() < 3) return 0;
                int team = std::clamp(a[0] - 1, 0, 3);
                float wx = float(a[1]) * 16 + 8, wz = float(a[2]) * 16 + 8;
                int best = 0;
                float bestD = 1e18f;
                for (auto& u : world_.units()) {
                    if (!u.alive() || u.team != team) continue;
                    float dx = u.x - wx, dz = u.z - wz;
                    if (dx * dx + dz * dz < bestD) { bestD = dx * dx + dz * dz; best = u.id; }
                }
                return best;
            }
            case 4: {   // HEURISTIC: spawn a reinforcement for player a[0]
                if (a.size() < 3) return 0;
                int team = std::clamp(a[0] - 1, 0, 3);
                auto& pool = reinfPool_[team];
                if (pool.empty()) return 0;
                const std::string& type = pool[size_t(reinfIdx_++) % pool.size()];
                float wx = float(a[1]) * 16 + 8, wz = float(a[2]) * 16 + 8;
                int id = spawn(type, wx + float(reinfIdx_ % 3) * 18,
                               wz + float(reinfIdx_ % 2) * 18, 3.14159f, team);
                if (id >= 0 && team == 0 && hudFont_.ok()) notice_ = "REINFORCEMENTS!";
                if (trace_) std::printf("SPAWN4 %s team%d at %d,%d -> id %d\n",
                                        type.c_str(), team, a[1], a[2], id);
                if (id >= 0) noticeTimer_ = 6;
                return id;
            }
            case 3: case 5: {   // HEURISTIC: activate spawned unit - join force
                if (a.empty()) return 0;
                const auto* u = world_.unit(a[0]);
                if (!u) return 0;
                float bx = 0, bz = 0;
                int n = 0;
                for (auto& o : world_.units())
                    if (o.alive() && o.team == u->team && o.id != u->id && o.type &&
                        o.type->canMove) { bx += o.x; bz += o.z; ++n; }
                if (n) world_.attackMove(a[0], bx / float(n), bz / float(n), false);
                return 0;
            }
            case 8: case 9: case 12: case 13: case 14:
                if (a.empty()) return missionTowerIdx_;   // type-constant heuristic
                return 0;
            default:
                return 0;
        }
    }

    void voice(int unitId, const std::string& event) {
        const auto* u = world_.unit(unitId);
        if (!u || !u->type || u->type->soundClass.empty()) return;
        if (const auto* wav = soundClasses_.pick(u->type->soundClass, event, salt_++))
            sounds_.play(*wav);
    }

    SoundBank sounds_;
    SoundClasses soundClasses_;
    uint32_t salt_ = 0;
    int outcome_ = 0;   // 0 = playing, 1 = victory, -1 = defeat
    bool sawTeam_[2] = {false, false};
    bool demoAi_ = false;
    bool aiEnabled_ = true;
    bool amphib_ = false;
    int amphibPhase_ = 0, amphibSquad_ = 0, transportId_ = -1;
    float amphibLandX_ = 0, amphibLandZ_ = 0, amphibSeaX_ = 0, amphibSeaZ_ = 0;
    Font hudFont_, bigFont_;

    struct Region { int a, b, c, d; bool rect; bool armed; };
    std::unique_ptr<tak::cob::Vm> missionVm_;
    std::vector<std::string> missionRoster_;
    std::map<int, Region> regions_;
    int missionTowerIdx_ = -1;
    std::set<int> building_;
    std::map<int, std::vector<std::string>> reinfPool_;
    int reinfIdx_ = 0;
    std::vector<std::string> briefing_;
    float briefTimer_ = 0;
    std::string notice_;
    float noticeTimer_ = 0;
    std::set<std::pair<int, int>> inside_;
    std::set<int> corpsed_;
    float animClock_ = 0;
    float trigTimer_ = 0;
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: takview map <map.tnt> <terrain-dir> [--shot out.png]\n"
                     "       takview model <file.3do> [textures-dir palette.pcx] "
                     "[--cob f.cob --anim script] [--shot out.png]\n"
                     "       takview game <map.tnt> <terrain-dir> <data-root> "
                     "[--demo] [--time s] [--shot out.png]\n");
        return 2;
    }
    std::string mode = argv[1];
    std::string shot, cobPath, anim, joinAddr, side = "ara", aiSide = "tar";
    int hostPort = 0, joinPort = 0, winW = kWinW, winH = kWinH, maxFps = 60;
    float startTime = 0, followZoom = 0, marchX = 0, marchZ = 0;
    bool demo = false, doMarch = false, trace = false, testbuild = false,
         scenario = false, navy = false, amphib = false, missionFlag = false,
         misstest = false, nofog = false, doLook = false, creon = false,
         hilltest = false, keytest = false, guardtest = false, selonly = false,
         lodetest = false;
    std::string lodeUnitName;
    bool firetest = false, facetest = false, soundtest = false;
    float lookX = 0, lookZ = 0;
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "--cob" && i + 1 < argc) cobPath = argv[++i];
        else if (a == "--anim" && i + 1 < argc) anim = argv[++i];
        else if (a == "--time" && i + 1 < argc) startTime = std::stof(argv[++i]);
        else if (a == "--demo") demo = true;
        else if (a == "--trace") trace = true;
        else if (a == "--testbuild") testbuild = true;
        else if (a == "--scenario") scenario = true;
        else if (a == "--navy") navy = true;
        else if (a == "--amphib") amphib = true;
        else if (a == "--mission") missionFlag = true;
        else if (a == "--misstest") misstest = true;
        else if (a == "--creon") creon = true;
        else if (a == "--hilltest") hilltest = true;
        else if (a == "--side" && i + 1 < argc) side = argv[++i];
        else if (a == "--aiside" && i + 1 < argc) aiSide = argv[++i];
        else if (a == "--keytest") keytest = true;
        else if (a == "--guardtest") guardtest = true;
        else if (a == "--lodetest") lodetest = true;
        else if (a == "--firetest") firetest = true;
        else if (a == "--facetest") facetest = true;
        else if (a == "--soundtest") soundtest = true;

        else if (a == "--tilt" && i + 1 < argc) gTilt = std::stof(argv[++i]);
        else if (a == "--winsize" && i + 2 < argc) {
            winW = std::atoi(argv[++i]);
            winH = std::atoi(argv[++i]);
        }
        else if (a == "--maxfps" && i + 1 < argc) maxFps = std::atoi(argv[++i]);


        else if (a == "--lodeunit" && i + 1 < argc) lodeUnitName = argv[++i];
        else if (a == "--selonly") selonly = true;

        else if (a == "--host" && i + 1 < argc) hostPort = std::atoi(argv[++i]);
        else if (a == "--join" && i + 2 < argc) {
            joinAddr = argv[++i];
            joinPort = std::atoi(argv[++i]);
        }
        else if (a == "--nofog") nofog = true;
        else if (a == "--look" && i + 2 < argc) {
            lookX = std::stof(argv[++i]);
            lookZ = std::stof(argv[++i]);
            doLook = true;
        }
        else if (a == "--follow" && i + 1 < argc) followZoom = std::stof(argv[++i]);
        else if (a == "--march" && i + 2 < argc) {
            marchX = std::stof(argv[++i]);
            marchZ = std::stof(argv[++i]);
            doMarch = true;
        }
        else args.push_back(a);
    }
    if (!shot.empty()) SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");

    std::unique_ptr<tak::net::Session> net;
    if (hostPort || joinPort) {
        net = std::make_unique<tak::net::Session>();
        bool ok = hostPort ? (std::printf("hosting on port %d, waiting for peer...\n",
                                          hostPort),
                              net->host(uint16_t(hostPort), 60))
                           : net->join(joinAddr, uint16_t(joinPort));
        if (!ok) {
            std::fprintf(stderr, "net: %s\n", net->error().c_str());
            return 1;
        }
        std::printf("net: connected as player %d\n", net->localPlayer() + 1);
    }

    if (shot.empty()) SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("takview", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, winW, winH,
                                       SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren = SDL_CreateRenderer(
        win, -1, shot.empty() ? SDL_RENDERER_PRESENTVSYNC : SDL_RENDERER_SOFTWARE);
    if (!ren) {
        std::fprintf(stderr, "renderer failed: %s\n", SDL_GetError());
        return 1;
    }
    if (shot.empty()) SDL_RenderSetVSync(ren, 1);   // explicit vsync

    std::unique_ptr<MapView> mapView;
    std::unique_ptr<ModelView> modelView;
    std::unique_ptr<GameView> gameView;
    try {
        if (mode == "map" && args.size() >= 2) {
            mapView = std::make_unique<MapView>(ren, args[0], args[1]);
        } else if (mode == "game" && args.size() >= 3) {
            gameView = std::make_unique<GameView>(ren, args[0], args[1], args[2], demo,
                                                  scenario, missionFlag,
                                                  navy || amphib || firetest || facetest,
                                                  side, aiSide);
            if (net) gameView->setNet(net.get());
            if (followZoom > 0) gameView->setFollow(followZoom);
            if (doMarch) gameView->marchTo(marchX, marchZ);
            if (trace) gameView->setTrace(true);
            if (testbuild) gameView->testBuild();
            if (navy) gameView->navyDemo();
            if (misstest) gameView->missionTest();
            if (creon) gameView->creonDemo();
            if (hilltest) gameView->hillTest();
            if (guardtest) gameView->guardTest();
            if (lodetest) { gameView->lodeUnit = lodeUnitName; gameView->lodeTest(); }
            if (firetest) gameView->fireTest();
            if (facetest) gameView->faceTest();
            if (soundtest) { gameView->setTrace(true); gameView->soundTest(); }

            if (nofog) gameView->noFog_ = true;
            if (doLook) gameView->lookAt(lookX, lookZ);
            if (amphib) gameView->amphibDemo();
            if (startTime > 0) gameView->advance(startTime);
        } else if (mode == "model" && !args.empty()) {
            modelView = std::make_unique<ModelView>(ren, args[0],
                                                    args.size() > 1 ? args[1] : "",
                                                    args.size() > 2 ? args[2] : "",
                                                    cobPath, anim);
            if (startTime > 0) modelView->advance(startTime);
        } else {
            std::fprintf(stderr, "bad arguments\n");
            return 2;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    bool running = true;
    float netAccum = 0;
    int ktPhase = keytest ? 0 : -1;
    float ktClock = 0;
    bool keytestSelectOnly = selonly;

    uint64_t last = SDL_GetPerformanceCounter();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT ||
                (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
                running = false;
            // 'S' grabs a screenshot in the asset viewers; in game it is the
            // Stop hotkey (Keys.TDF LOWER_S), handled by GameView::input.
            if (!gameView && e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_s)
                screenshot(ren, kWinW, kWinH, "takview_shot.png");
            int ww, wh;
            SDL_GetRendererOutputSize(ren, &ww, &wh);
            // Mouse events arrive in window points; the renderer (and all our
            // world<->screen math) works in output pixels. When those differ
            // — e.g. a maximized window on a scaled display — rescale so
            // zoom-to-cursor and clicks land where the pointer actually is.
            int wpw = 0, wph = 0;
            SDL_GetWindowSize(win, &wpw, &wph);
            if (wpw > 0 && wph > 0 && (wpw != ww || wph != wh)) {
                double sx = double(ww) / wpw, sy = double(wh) / wph;
                if (e.type == SDL_MOUSEMOTION) {
                    e.motion.x = int(e.motion.x * sx); e.motion.y = int(e.motion.y * sy);
                    e.motion.xrel = int(e.motion.xrel * sx);
                    e.motion.yrel = int(e.motion.yrel * sy);
                } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                    e.button.x = int(e.button.x * sx); e.button.y = int(e.button.y * sy);
                }
            }
            if (mapView) mapView->input(e);
            if (modelView) modelView->input(e);
            if (gameView) gameView->input(e, ww, wh);
        }
        uint64_t now = SDL_GetPerformanceCounter();
        float dt = float(now - last) / float(SDL_GetPerformanceFrequency());
        last = now;
        if (ktPhase >= 0) {
            ktClock += dt;
            auto click = [&](int x, int y, uint8_t btn) {
                SDL_Event ev{};
                ev.type = SDL_MOUSEBUTTONDOWN;
                ev.button.button = btn;
                ev.button.x = x;
                ev.button.y = y;
                SDL_PushEvent(&ev);
                ev.type = SDL_MOUSEBUTTONUP;
                SDL_PushEvent(&ev);
            };
            auto key = [&](SDL_Keycode k) {
                SDL_Event ev{};
                ev.type = SDL_KEYDOWN;
                ev.key.keysym.sym = k;
                SDL_PushEvent(&ev);
            };
            auto motion = [&](int x, int y) {
                SDL_Event ev{};
                ev.type = SDL_MOUSEMOTION;
                ev.motion.x = x;
                ev.motion.y = y;
                SDL_PushEvent(&ev);
            };
            if (ktPhase == 0 && ktClock > 0.3f) {
                {
                    int pick = -1;
                    for (auto& u : gameView->worldRef().units())
                        if (u.alive() && u.team == 0 && u.type && u.type->isBuilder)
                            pick = u.id;
                    if (pick >= 0) gameView->selectOnly(pick);
                    ktPhase = keytestSelectOnly ? 4 : 1;
                }
            }

            else if (ktPhase == 1 && ktClock > 0.6f) { key(SDLK_f); ktPhase = 2; }
            else if (ktPhase == 2 && ktClock > 0.9f) { motion(400, 453); ktPhase = 3; }
            else if (ktPhase == 3 && ktClock > 1.2f) { click(400, 453, SDL_BUTTON_LEFT); ktPhase = 4; }
            else if (ktPhase == 4 && ktClock > 1.5f) { click(253, 453, SDL_BUTTON_LEFT); motion(1250, 245); ktPhase = 5; }
            else if (ktPhase == 5 && ktClock > 1.9f) {
                std::printf("KEYTEST done\n");
                ktPhase = -1;
            }
        }

        int w, h;
        SDL_GetRendererOutputSize(ren, &w, &h);
        // Create textures before the render pass (mid-pass creation glitches
        // the whole frame on some backends).
        if (mapView) mapView->ensureChunks(w, h);
        if (gameView) gameView->prepare(w, h);
        SDL_SetRenderDrawColor(ren, 18, 18, 26, 255);
        SDL_RenderClear(ren);
        if (mapView) mapView->draw(w, h);
        if (modelView) modelView->draw(w, h, dt);
        if (gameView) {
            if (gameView->isNet()) {
                netAccum += dt;
                // Catch up at most a few steps per frame; block briefly on peer.
                int steps = 0;
                while (netAccum >= 1.0f / 30.0f && steps < 4 &&
                       gameView->netError().empty()) {
                    if (gameView->netStep()) {
                        netAccum -= 1.0f / 30.0f;
                        ++steps;
                    } else {
                        break;   // stalled or errored; keep rendering
                    }
                }
                if (netAccum > 0.5f) netAccum = 0.5f;
            } else {
                gameView->update(dt);
            }
            gameView->draw(w, h);
        }
        SDL_RenderPresent(ren);

        if (maxFps > 0) {
            static uint64_t prevPresent = 0;
            uint64_t nowp = SDL_GetPerformanceCounter();
            double target = 1.0 / maxFps;
            double elapsed = prevPresent ? double(nowp - prevPresent) /
                                               double(SDL_GetPerformanceFrequency())
                                         : target;
            if (elapsed < target)
                SDL_Delay(uint32_t((target - elapsed) * 1000.0));
            prevPresent = SDL_GetPerformanceCounter();
        }

        if (!shot.empty()) {
            // Render a few frames so lazy content settles, then capture.
            static int frames = 0;
            if (++frames >= 3 && ktPhase < 0) {
                screenshot(ren, w, h, shot);
                running = false;
            }
        }
    }
    SDL_Quit();
    return 0;
}
