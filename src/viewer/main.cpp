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
#include "ai/ai.h"
#include "net/client.h"
#include "net/lockstep.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"
#include "tdf/tdf.h"
#include "tdo/tdo.h"
#include "terrain/terrain.h"
#include "tnt/tnt.h"
#include "util/png.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
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
    // The smallest zoom at which the map still fills the window in one dimension
    // (zooming out past this would run the view off the map edges).
    float minZoom(int winW, int winH) const {
        int mapW = map_.blocksX * 32, mapH = map_.blocksY * 32;
        return (mapW > 0 && mapH > 0) ? std::max(float(winW) / mapW, float(winH) / mapH) : 0.05f;
    }
    // Apply just the min-zoom floor (no offset change). The zoom-to-cursor step
    // must call this before recomputing the offset, else it recentres using a
    // below-floor zoom and the next clampOffset snaps the view to the map corner.
    void clampZoom(int winW, int winH) { zoom_ = std::max(zoom_, minZoom(winW, winH)); }

    void clampOffset(int winW, int winH) {
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

// COB piece rotations for Xform::then(), with the piece YAW negated to match the
// retail engine. Retail composes every piece as Ry(-y)*Rx(+x)*Rz(+z): the y
// negation is a literal `fchs` at 0x4eea98 in KINGDOMS.icd's piece-matrix call
// site -- the SAME negation it applies to the root heading (0x4ee6a9), which we
// already reproduce as facing = -u.heading and which is verified correct. Our
// piece Ry and the root yaw reduce to the identical matrix, so pieces must carry
// the same negation. Without it, every scripted y-axis TURN/SPIN played mirrored
// (janky walks, garbled wing strokes). The negation lives HERE, at the
// script->composer boundary, so then() stays a generic rotation utility and the
// COB VM's script-space state (WAIT_TURN, shortest-path) is untouched.
// See docs/model-rendering-plan.md.
inline const float* scriptRot(const tak::cob::PieceState* ps, float (&tmp)[3]) {
    static const float kZero[3] = {0, 0, 0};
    if (!ps) return kZero;
    // COB piece angles compose with BOTH pitch (X) and yaw (Y) negated: the models
    // are authored front=-z/right=-x (a mirrored basis), so scripted X- and Y-turns
    // play mirrored unless negated here. Verified in-game: negating X fixes walker
    // leg-swing direction ("feet backwards") and flyer body-roll (was flying supine,
    // "back to the ground"); Z passes through. Retail applies the same via `fchs`
    // at the piece-matrix call site (0x4eea98).
    tmp[0] = -ps->rot[0];
    tmp[1] = -ps->rot[1];
    tmp[2] = ps->rot[2];
    return tmp;
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
        const tak::cob::PieceState* ps = pieceFor(o.name);
        if (ps && !ps->visible) return;
        float rr[3];
        Xform xf = parent.then(o.x + (ps ? ps->move[0] : 0),
                               o.y + (ps ? ps->move[1] : 0),
                               o.z + (ps ? ps->move[2] : 0),
                               scriptRot(ps, rr));
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
        // A missing sounds dir must NOT skip audio init (music uses the same
        // device); just index whatever's there.
        try {
            for (const auto& e : std::filesystem::directory_iterator(soundsDir)) {
                std::string stem = e.path().stem().string();
                std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);
                index_[stem] = e.path().string();
            }
        } catch (const std::exception&) { /* no sounds dir -- music still plays */ }

        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
        // Auto-detect the device's channel layout so positional audio can pan
        // across stereo (L/R) or surround (adds front/rear) speakers.
        int chans = 2;
        SDL_AudioSpec def{};
        if (SDL_GetDefaultAudioInfo(nullptr, &def, 0) == 0 && def.channels >= 2)
            chans = std::min<int>(def.channels, 8);
        SDL_AudioSpec want{};
        want.freq = 11025;
        want.format = AUDIO_S16SYS;
        want.channels = Uint8(chans);
        want.samples = 1024;
        want.callback = &SoundBank::mixThunk;
        want.userdata = this;
        dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &spec_,
                                   SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
        chan_ = dev_ ? (spec_.channels ? spec_.channels : 2) : 1;
        std::fprintf(stderr, "audio: %d output channels%s\n", chan_,
                     chan_ >= 4 ? " (surround: front/rear enabled)" : "");
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

    // The listener (camera) frame in world coords, so positional sounds pan by
    // where the source sits on screen. halfW/halfH are half the visible extent.
    void setListener(float cx, float cz, float halfW, float halfH) {
        listenX_ = cx; listenZ_ = cz;
        listenHW_ = std::max(halfW, 1.0f); listenHH_ = std::max(halfH, 1.0f);
    }

    // Non-positional (UI, music-adjacent) — centred across all speakers.
    void play(const std::string& name) { playAt(name, 0.0f, 0.0f); }

    // Positional: pan by the source's world position relative to the listener.
    // Left/right from x; front(up)/rear(down) from z on surround setups.
    void playWorld(const std::string& name, float x, float z) {
        float pan = std::clamp((x - listenX_) / listenHW_, -1.0f, 1.0f);
        float depth = std::clamp((z - listenZ_) / listenHH_, -1.0f, 1.0f);
        playAt(name, pan, depth);
    }

    void playAt(const std::string& name, float pan, float depth) {
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
                c.pan = pan;
                c.depth = depth;
                break;
            }
        SDL_UnlockAudioDevice(dev_);
    }

private:
    struct Channel {
        const std::vector<int16_t>* data = nullptr;
        size_t pos = 0;
        float pan = 0, depth = 0;   // -1..+1 : left..right, front..rear
    };

    // Per-output-channel gains for a source at (pan, depth). Equal-power pan
    // left/right; on surround layouts also crossfade front/rear by depth.
    void channelGains(float pan, float depth, float* g) const {
        float lg = std::sqrt(std::clamp((1 - pan) * 0.5f, 0.0f, 1.0f));
        float rg = std::sqrt(std::clamp((1 + pan) * 0.5f, 0.0f, 1.0f));
        float fg = std::sqrt(std::clamp((1 - depth) * 0.5f, 0.0f, 1.0f));
        float bg = std::sqrt(std::clamp((1 + depth) * 0.5f, 0.0f, 1.0f));
        for (int i = 0; i < chan_; ++i) g[i] = 0;
        float cg = (1.0f - std::fabs(pan)) * 0.7f;   // centre channel content
        switch (chan_) {
            case 1: g[0] = 1.0f; break;
            case 2: g[0] = lg; g[1] = rg; break;                       // FL FR
            case 4: g[0]=lg*fg; g[1]=rg*fg; g[2]=lg*bg; g[3]=rg*bg; break;  // FL FR BL BR
            case 6:  // FL FR FC LFE BL BR
                g[0]=lg*fg; g[1]=rg*fg; g[2]=cg*fg; g[3]=0; g[4]=lg*bg; g[5]=rg*bg; break;
            case 8:  // FL FR FC LFE BL BR SL SR
                g[0]=lg*fg; g[1]=rg*fg; g[2]=cg*fg; g[3]=0;
                g[4]=lg*bg; g[5]=rg*bg; g[6]=lg*0.7f; g[7]=rg*0.7f; break;
            default: g[0]=lg; if (chan_>1) g[1]=rg; break;
        }
    }
    float listenX_ = 0, listenZ_ = 0, listenHW_ = 1, listenHH_ = 1;

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
        int ch = std::max(chan_, 1);
        int frames = n / ch;
        auto add = [&](int f, int ci, int v) {
            int idx = f * ch + ci;
            out[idx] = int16_t(std::clamp(out[idx] + v, -32768, 32767));
        };
        // Background music bed (quieter than SFX), spread across all speakers.
        for (int f = 0; f < frames; ++f) {
            if (musicPos_ >= music_.size()) { musicDone_ = true; break; }
            int m = (music_[musicPos_++] * musicVol_) / 256;
            int mc = m / (ch > 2 ? 2 : 1);   // don't get louder with more speakers
            for (int ci = 0; ci < ch; ++ci) add(f, ci, mc);
        }
        // Positional SFX: pan each into the speaker layout.
        float g[8];
        for (auto& c : channels_) {
            if (!c.data) continue;
            channelGains(c.pan, c.depth, g);
            for (int f = 0; f < frames && c.pos < c.data->size(); ++f, ++c.pos) {
                int s = (*c.data)[c.pos] / 2;
                for (int ci = 0; ci < ch; ++ci)
                    if (g[ci] != 0.0f) add(f, ci, int(s * g[ci]));
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
    int chan_ = 1;              // output channel count (2=stereo, 4/6/8=surround)
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
            // Some fonts have a visible space glyph (a dot) — never draw it.
            if (g.tex && c != ' ') {
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

// ------------------------------------------------------------- thread pool

// Persistent worker pool for data-parallel loops (e.g. per-unit animation VM
// ticks). parallelFor splits [0,count) into chunks pulled off an atomic counter
// and blocks until all are done; the calling thread participates as one worker.
class ThreadPool {
public:
    ThreadPool() {
        unsigned n = std::thread::hardware_concurrency();
        n_ = n ? n : 1;
        for (unsigned i = 1; i < n_; ++i)
            workers_.emplace_back([this] { workerLoop(); });
    }
    ~ThreadPool() {
        { std::lock_guard<std::mutex> lk(m_); stop_ = true; }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <class F>
    void parallelFor(size_t count, F&& f) {
        if (count == 0) return;
        if (n_ == 1 || count < 32) { f(size_t(0), count); return; }  // not worth it
        std::function<void(size_t, size_t)> fn =
            [&f](size_t b, size_t e) { f(b, e); };
        {
            std::lock_guard<std::mutex> lk(m_);
            fn_ = &fn;
            count_ = count;
            next_.store(0, std::memory_order_relaxed);
            active_ = n_;
            ++gen_;
        }
        cv_.notify_all();
        runChunks();
        std::unique_lock<std::mutex> lk(m_);
        doneCv_.wait(lk, [this] { return active_ == 0; });
        fn_ = nullptr;
    }

private:
    void runChunks() {
        constexpr size_t kChunk = 8;
        for (;;) {
            size_t b = next_.fetch_add(kChunk, std::memory_order_relaxed);
            if (b >= count_) break;
            size_t e = std::min(b + kChunk, count_);
            (*fn_)(b, e);
        }
        std::lock_guard<std::mutex> lk(m_);
        if (--active_ == 0) doneCv_.notify_one();
    }
    void workerLoop() {
        unsigned seen = 0;
        for (;;) {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [this, &seen] { return stop_ || gen_ != seen; });
            if (stop_) return;
            seen = gen_;
            lk.unlock();
            runChunks();
        }
    }
    unsigned n_ = 1;
    std::vector<std::thread> workers_;
    std::mutex m_;
    std::condition_variable cv_, doneCv_;
    std::function<void(size_t, size_t)>* fn_ = nullptr;
    size_t count_ = 0;
    std::atomic<size_t> next_{0};
    unsigned active_ = 0, gen_ = 0;
    bool stop_ = false;
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
             bool bare, const std::string& side = "ara", const std::string& aiSide = "tar",
             bool crusades = false)
        // (side_ initialized below before loadPanel uses it)
        : ren_(ren), mapView_(ren, tntPath, terrainDir), dataRoot_(dataRoot),
          side_(side), tntPath_(tntPath), crusades_(crusades) {
        registry_.loadMoveInfo(dataRoot_ + "/gamedata/moveinfo.tdf");
        // God economy timing (gamedata/Gods.tdf). TAK_GODTIME overrides the
        // appear time (seconds) for testing; otherwise use AppearTimeMin minutes.
        try {
            auto g = tak::tdf::parse(dataRoot_ + "/gamedata/gods.tdf");
            if (const auto* tm = g.child("TIMING")) {
                float appear = float(tm->numberOr("AppearTimeMin", 30.0)) * 60.0f;
                if (const char* e = getenv("TAK_GODTIME")) appear = std::stof(e);
                world_.enableGods(appear);
            }
        } catch (const std::exception&) {}
        // Crusades balance: the final patch shipped alternate unit stats and
        // build menus (units/->UnitsCB, canbuild/->CanBuildCB in the retail
        // engine) used for ranked "Darien Crusades" play. Load the CB dirs FIRST
        // so their versions win (loadDir/loadBuildTree are first-definition-wins);
        // the base dirs then fill in units the CB set left unchanged.
        if (crusades) {
            std::string ucb = dataRoot_ + "/unitscb", ccb = dataRoot_ + "/canbuildcb";
            if (std::filesystem::is_directory(ucb)) registry_.loadDir(ucb);
            if (std::filesystem::is_directory(ccb)) registry_.loadBuildTree(ccb);
            std::fprintf(stderr, "balance: Crusades (unitscb/canbuildcb)%s\n",
                         std::filesystem::is_directory(ucb) ? "" : " -- NOT FOUND");
        }
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
            // A plain, legible font for the HUD stat readouts.
            try { statFont_ = Font(ren_, dataRoot_ +
                                   "/fonts/b_times new roman (100b).gaf"); }
            catch (const std::exception&) {
                try { statFont_ = Font(ren_, dataRoot_ +
                                       "/fonts/ig_times new roman (100).gaf"); }
                catch (const std::exception&) {}
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "font load: %s\n", e.what());
        }
        loadOrderButtons();
        loadBuildFx();
        // Sounds live at <data>/sounds in a merged tree, or ../english/Sounds in
        // the old per-archive layout.
        std::string soundsDir = dataRoot_ + "/sounds";
        if (!std::filesystem::exists(soundsDir))
            soundsDir = dataRoot_ + "/../english/Sounds";
        sounds_.init(soundsDir, false);
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
                        int playerSlot = int(u.numberOr("player", 1));
                        float x = float(u.numberOr("xpos", 0)) * 16 + 8;
                        float z = float(u.numberOr("zpos", 0)) * 16 + 8;
                        int player = std::clamp(playerSlot - 1, 0, 3);
                        int uid = spawn(id, x, z, 3.14159f, player);
                        if (uid >= 0) {
                            ++n;
                            float hpp = float(u.numberOr("healthpercentage", 100));
                            if (auto* su = world_.unit(uid)) {
                                su->hp *= hpp / 100.0f;
                                if (su->type->canMove &&
                                    su->type->domain ==
                                        tak::sim::UnitType::Domain::Ground)
                                    reinfPool_[player].push_back(id);
                            }
                            if (player == 0) { cx += x; cz += z; ++pc; }
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
                int player = std::clamp(p.player, 0, 3);
                if (spawn(id, p.x, p.z, 3.14159f, player) >= 0 && player == 0) {
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
                    sr.player = 3;
                    if (rec.slots[1].size() >= 8 &&
                        (rec.slots[1][0] == 'P' || rec.slots[1][0] == 'p'))
                        sr.player = std::clamp(rec.slots[1].back() - '1', 0, 3);
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
        // Dev harness: TAK_FFA=N or TAK_FFA=N,t0.t1.t2... sets up an N-player
        // game (each on its own team unless a team list is given), one monarch +
        // a small army per player at N start positions, all AI-driven. Verifies
        // the 8-player / team / shared-vision / win-condition paths before the
        // real lobby exists. (multiplayer M1)
        if (const char* ff = std::getenv("TAK_FFA")) {
            int n = std::atoi(ff);
            n = std::clamp(n, 2, tak::sim::kMaxPlayers);
            std::vector<int> teams(size_t(n), 0);
            for (int i = 0; i < n; ++i) teams[size_t(i)] = i;   // default: FFA
            if (const char* comma = std::strchr(ff, ',')) {     // optional team list
                std::string ts(comma + 1);
                int i = 0;
                for (size_t p = 0; p < ts.size() && i < n; ) {
                    size_t dot = ts.find('.', p);
                    teams[size_t(i++)] = std::atoi(ts.substr(p, dot - p).c_str());
                    if (dot == std::string::npos) break;
                    p = dot + 1;
                }
            }
            world_.setPlayerCount(n);
            for (int i = 0; i < n; ++i) world_.setTeam(i, teams[size_t(i)]);
            // Spread players across the N most mutually-distant start positions
            // (fall back to a ring around centre when the map has too few).
            std::vector<std::pair<float, float>> spots = starts;
            while (int(spots.size()) < n) {
                float ang = float(spots.size()) / float(n) * 6.2831853f;
                spots.push_back({cx + std::cos(ang) * 300, cz + std::sin(ang) * 300});
            }
            const char* sides[5] = {"ara", "tar", "ver", "zon", "cre"};
            for (int i = 0; i < n; ++i) {
                std::string fkSide = sides[i % 5];
                const FactionKit& fk = kit(fkSide);
                float mx = spots[size_t(i)].first, mz = spots[size_t(i)].second;
                int mon = spawn(fk.monarch, mx, mz, 0, i);
                if (i == 0) { playerMonarchId_ = mon; builderId_ = mon; }
                for (int s = 0; s < 4; ++s)
                    spawn(fk.squad[s % 4], mx + (s % 2) * 26 - 13, mz - 50 + (s / 2) * 26, 0, i);
                world_.player(i).mana = 2800;
            }
            ffaPlayers_ = n;
            demoAi_ = true;   // (harness) all players AI-driven
            for (auto& u : world_.units()) {
                if (!u.type || u.type->canMove) continue;
                tak::sim::blockFootprint(world_.nav(), *u.type, u.x, u.z, true);
            }
            return;
        }
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
        world_.player(0).mana = 2800;
        world_.player(1).mana = 2800;
        if (demo) {
            // Showcase: skip the slow build-up and pit two ready armies at the
            // start positions against each other.
            std::vector<int> playerA, playerB;
            for (int i = 0; i < 6; ++i) {
                int a = spawn(pk.squad[i % 4], px + float(i % 2) * 26,
                              pz - 60 + float(i / 2) * 30, pFace, 0);
                int b = spawn(ak.squad[i % 4], ax + float(i % 2) * 26,
                              az - 60 + float(i / 2) * 30, aFace, 1);
                if (a >= 0) playerA.push_back(a);
                if (b >= 0) playerB.push_back(b);
            }
            if (pk.keep[0]) keepId_ = spawn(pk.keep, px, pz + 60, pFace, 0);
            if (ak.keep[0]) aiKeepId_ = spawn(ak.keep, ax, az + 60, aFace, 1);
            if (!playerA.empty() && !playerB.empty()) {
                for (size_t k = 0; k < playerA.size(); ++k)
                    world_.attack(playerA[k], playerB[k % playerB.size()], false);
                for (size_t k = 0; k < playerB.size(); ++k)
                    world_.attack(playerB[k], playerA[k % playerA.size()], false);
            }
            demoAi_ = true;
        }
        }

        for (auto& u : world_.units()) {
            if (!u.type || u.type->canMove) continue;
            tak::sim::blockFootprint(world_.nav(), *u.type, u.x, u.z, true);
        }
    }

    // True while the multiplayer lobby is showing (before the game world exists).
    bool inLobbyPhase() const {
        if (!mp_ || mpSetupDone_) return false;
        auto s = mp_->state();
        return s == tak::net::MpClient::State::Connecting ||
               s == tak::net::MpClient::State::Lobby ||
               s == tak::net::MpClient::State::InRoom ||
               s == tak::net::MpClient::State::Done;
    }
    void setMpMapId(const std::string& id) { mpMapId_ = id; }
    void setResumePath(const std::string& p) { mpResumePath_ = p; }
    // Persist / read the resume ticket (gameId + rotating token) so a killed
    // client can rejoin its held slot on restart.
    void writeResume(uint32_t gid, uint64_t tok) const {
        if (mpResumePath_.empty()) return;
        if (FILE* f = std::fopen(mpResumePath_.c_str(), "w")) {
            std::fprintf(f, "%u %llu\n", gid, (unsigned long long)tok);
            std::fclose(f);
        }
    }
    bool readResume(uint32_t& gid, uint64_t& tok) const {
        if (mpResumePath_.empty()) return false;
        FILE* f = std::fopen(mpResumePath_.c_str(), "r");
        if (!f) return false;
        unsigned long long t = 0;
        bool ok = std::fscanf(f, "%u %llu", &gid, &t) == 2;
        std::fclose(f);
        tok = t;
        return ok;
    }
    // The map's player capacity = its start-position count (2..8). The server has
    // no map data, so a creating client tells it how many slots the map supports.
    uint8_t mpCapacity() const {
        int n = int(parseStartPositions(tntPath_).size());
        return uint8_t(std::clamp(n < 2 ? 2 : n, 2, tak::net::kMaxSlots));
    }

    void input(const SDL_Event& e, int winW, int winH) {
        winW_ = winW;
        winH_ = winH;
        if (inLobbyPhase()) { lobbyInput(e, winW, winH); return; }
        // In-game chat capture. While composing, keyboard goes to the draft;
        // mouse events still fall through so the camera stays usable.
        if (chatTyping_) {
            if (e.type == SDL_TEXTINPUT) {
                if (chatDraft_.size() < 200) chatDraft_ += e.text.text;
                return;
            }
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                    if (mp_ && !chatDraft_.empty()) mp_->chat(chatDraft_);
                    chatDraft_.clear(); chatTyping_ = false; SDL_StopTextInput();
                } else if (k == SDLK_ESCAPE) {
                    chatDraft_.clear(); chatTyping_ = false; SDL_StopTextInput();
                } else if (k == SDLK_BACKSPACE && !chatDraft_.empty()) {
                    while (!chatDraft_.empty() && (chatDraft_.back() & 0xC0) == 0x80)
                        chatDraft_.pop_back();          // drop a UTF-8 continuation
                    if (!chatDraft_.empty()) chatDraft_.pop_back();
                }
                return;
            }
        }
        // Enter opens the chat composer (net games only -- there's no one to
        // talk to offline or in a recording).
        if (mp_ && e.type == SDL_KEYDOWN &&
            (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER)) {
            chatTyping_ = true; chatDraft_.clear(); SDL_StartTextInput();
            return;
        }
        float zm = mapView_.zoom();
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
            // Escape cancels the pending placement/order, else clears the current
            // selection. It does NOT quit the game (the main loop no longer treats
            // Escape as quit while a game is running).
            if (placing_ || pendingCmd_) { placing_ = nullptr; pendingCmd_ = 0; }
            else selection_.clear();
        } else if (e.type == SDL_KEYDOWN && handleKey(e.key.keysym.sym,
                                                       SDL_GetModState())) {
            // handled by the hotkey dispatcher
        } else if (e.type == SDL_MOUSEWHEEL) {
            // Zoom toward the cursor: keep the world point under the mouse fixed.
            float wx = mapView_.offX() + mouseX_ / mapView_.zoom();
            float wz = mapView_.offY() + mouseY_ / mapView_.zoom();
            mapView_.input(e);                  // applies the zoom step
            mapView_.clampZoom(winW, winH);     // ...then the map-fills-window floor,
            float nz = mapView_.zoom();         // so the recentre below uses the real zoom
            mapView_.setOffset(wx - mouseX_ / nz, wz - mouseY_ / nz);
        } else if (e.type == SDL_KEYDOWN) {
            // Arrow-key panning takes the camera off the tracked selection.
            switch (e.key.keysym.sym) {
                case SDLK_LEFT: case SDLK_RIGHT: case SDLK_UP: case SDLK_DOWN:
                    trackSel_ = false; break;
                default: break;
            }
            mapView_.input(e);
        } else if (e.type == SDL_MOUSEMOTION) {
            mouseX_ = float(e.motion.x);
            mouseY_ = float(e.motion.y);
            if (draggingMinimap_) {
                minimapClick(mouseX_, mouseY_, winW, winH);
            } else if (e.motion.state & SDL_BUTTON_MMASK) {   // middle-drag scrolls
                trackSel_ = false;
                mapView_.setOffset(mapView_.offX() - e.motion.xrel / zm,
                                   mapView_.offY() - e.motion.yrel / zm);
            }
            if (dragging_) { dragX1_ = float(e.motion.x); dragY1_ = float(e.motion.y); }
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   colorPickerClick(float(e.button.x), float(e.button.y))) {
            // colour picker swatch handled
        } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                   e.button.y > winH - kBarH) {
            // bottom bar: build-icon clicks; everything else is swallowed
            bool lmb = e.button.button == SDL_BUTTON_LEFT;
            bool rmb = e.button.button == SDL_BUTTON_RIGHT;
            if (lmb || rmb) {
                for (const auto& [r, bt] : iconRects_) {
                    if (e.button.x < r.x || e.button.x > r.x + r.w ||
                        e.button.y < r.y || e.button.y > r.y + r.h)
                        continue;
                    const auto* b = selectedBuilder();
                    if (!b || !bt) break;
                    uint16_t mod = SDL_GetModState();
                    bool ctrl = (mod & KMOD_CTRL) != 0, shift = (mod & KMOD_SHIFT) != 0;
                    // Placement (a manual click) is for STRUCTURES -- you place
                    // buildings/lodestones -- and for MOBILE builders conjuring units
                    // (e.g. Zhon's beast handlers): left-click arms placement, no queue.
                    if (isStructure(bt) || !isStructure(b->type)) {
                        if (lmb) placing_ = bt;
                        break;
                    }
                    // A BUILDING conjuring a mobile unit uses a build QUEUE: left adds,
                    // right removes; 1 by default, 5 with Shift, 10 with Ctrl+Shift.
                    // Ctrl+left (no Shift) toggles infinite production.
                    tak::net::Command c;
                    c.unitId = b->id;
                    std::snprintf(c.type, sizeof c.type, "%s", bt->id.c_str());
                    if (lmb && ctrl && !shift) {
                        c.kind = tak::net::Cmd::RepeatTrain;
                    } else {
                        c.kind = lmb ? tak::net::Cmd::Train : tak::net::Cmd::Unqueue;
                        c.targetId = (ctrl && shift) ? 10 : shift ? 5 : 1;
                    }
                    issue(c);
                    break;
                }
            }
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   orderColumnClick(float(e.button.x), float(e.button.y), winW)) {
            // order button handled
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   minimapArmedOrder(float(e.button.x), float(e.button.y), winW, winH)) {
            // An armed order (F/M/A/P/G) + minimap click issues that order at the
            // clicked location, instead of moving the camera there.
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   minimapClick(float(e.button.x), float(e.button.y), winW, winH)) {
            draggingMinimap_ = true;   // camera follows the drag until release
            trackSel_ = false;
        } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                   e.button.x > mapViewW(winW) && e.button.y < winH - kBarH) {
            // Right-hand panel press. A right-click on the minimap orders the
            // selection to that world point; every other panel press is swallowed
            // so it can't start a box-select or drop an order on the map. (Only the
            // PRESS -- releases still fall through to end a drag/box-select.)
            if (e.button.button == SDL_BUTTON_RIGHT &&
                minimapOrder(float(e.button.x), float(e.button.y), winW, winH,
                             (SDL_GetModState() & KMOD_SHIFT) != 0))
                trackSel_ = false;
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   pendingCmd_) {
            float wx, wz;
            pickWorld(float(e.button.x), float(e.button.y), wx, wz);
            issueArmedOrder(pendingCmd_, wx, wz,
                            (SDL_GetModState() & KMOD_SHIFT) != 0, /*precise=*/true);
            pendingCmd_ = 0;
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   placing_) {
            // Height-aware placement: resolve the click to the cell drawn under the
            // cursor (pickWorld inverts the terrain lift), so conjuring/building onto
            // elevated ground drops the unit where clicked, not on the low cell behind.
            float wx, wz;
            pickWorld(float(e.button.x), float(e.button.y), wx, wz);
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
            float ewx, ewz;
            pickWorld(float(e.button.x), float(e.button.y), ewx, ewz);
            placeBuildLine(bdX0_, bdZ0_, ewx, ewz);
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
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT &&
                   dragging_) {
            // Only complete a box/click-select if the press actually began on
            // the map. A release after an icon click or a build placement must
            // NOT clear the selection using stale drag coordinates.
            dragging_ = false;
            // Screen-space marquee: a unit is boxed by where it's DRAWN (terrain lift
            // + flyer altitude), not its flat ground cell -- else a lifted/airborne
            // unit (e.g. the flying Monarch) escapes a box drawn around its sprite.
            float sx0 = std::min(dragX0_, dragX1_), sx1 = std::max(dragX0_, dragX1_);
            float sy0 = std::min(dragY0_, dragY1_), sy1 = std::max(dragY0_, dragY1_);
            bool isClick = (sx1 - sx0) < 6 && (sy1 - sy0) < 6;
            selection_.clear();
            if (isClick) {
                // Pick the unit nearest the cursor in SCREEN space (matching the
                // render lift + flyer altitude), so a unit on a lifted wall top or a
                // Monarch cruising overhead is selected where it's drawn.
                float ccx = (dragX0_ + dragX1_) / 2, ccy = (dragY0_ + dragY1_) / 2;
                int hit = -1;
                float best = 30.0f * 30.0f;
                for (auto& u : world_.units()) {
                    if (!u.alive() || u.underConstruction) continue;  // not-yet-built: unselectable
                    SDL_FPoint p = unitScreen(u);
                    float dx = p.x - ccx, dy = p.y - ccy;
                    if (dx * dx + dy * dy < best) { best = dx * dx + dy * dy; hit = u.id; }
                }
                if (hit >= 0) {
                    selection_.push_back(hit);
                    voice(hit, "select");
                }
            } else {
                for (auto& u : world_.units())
                    if (u.alive() && u.player == localPlayer_ && !u.underConstruction) {
                        SDL_FPoint p = unitScreen(u);
                        if (p.x >= sx0 && p.x <= sx1 && p.y >= sy0 && p.y <= sy1)
                            selection_.push_back(u.id);
                    }
            }
        } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                   e.button.button == SDL_BUTTON_RIGHT && !selection_.empty()) {
            float wx, wz;
            pickWorld(float(e.button.x), float(e.button.y), wx, wz);
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
                if (!u.alive() || !first || u.player != first->player || !u.type ||
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
            // Clicking near an enemy = attack; else formation move. (Allies are
            // not enemies -- clicking one falls through to a move, not an attack.)
            int enemy = -1;
            float best = 20 * 20;
            for (auto& u : world_.units()) {
                if (!u.alive() || u.embarked() || !first ||
                    world_.allied(u.player, first->player)) continue;
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

    // Attach the multiplayer client. The game world is set up later, from the
    // server's GameStarting (startMpGame), not from the constructor.
    void setMpClient(tak::net::MpClient* mp) {
        mp_ = mp;
        aiEnabled_ = false;   // the server owns any AI; clients are all human
    }
    bool isNet() const { return mp_ != nullptr; }

    // Route a command: offline it applies immediately; in a net game it is queued
    // for the server, which stamps ownership and sequences it into a tick bundle.
    void issue(tak::net::Command c) {
        if (replayMode_ || spectating_) return;   // watch-only: can't order units
        c.player = uint8_t(localPlayer_);
        if (mp_) outbox_.push_back(c);
        else apply(c);
    }

    // Set up the world for a multiplayer match from the server's final slot
    // table: one player per used slot (sim player index == slot), teams/colours
    // per slot, a monarch spawned at a start position each, seeded starting mana.
    void startMpGame(const tak::net::RoomView& room, uint32_t seed) {
        (void)seed;
        int maxSlot = 0;
        for (int i = 0; i < tak::net::kMaxSlots; ++i)
            if (room.slots[i].type == 1 || room.slots[i].type == 2) maxSlot = i;
        // Build the world through the SHARED setup so the server's referee sim and
        // every client produce a bit-identical world (and hash). Client-only bits
        // (colours, camera, local player, panel) stay here.
        // The game's balance is the ROOM's (a lobby choice), which may differ
        // from how this client was launched. Rebuild the registry to match so our
        // world -- and hash -- agree with the server's referee. (Crusades only
        // re-tunes stats/build trees; models/textures are shared and id-keyed, so
        // they need no reload.)
        if ((room.opts.crusades != 0) != crusades_) {
            registry_ = tak::sim::TypeRegistry{};
            tak::sim::setupRegistry(registry_, dataRoot_, room.opts.crusades != 0);
            crusades_ = room.opts.crusades != 0;
        }
        tak::sim::MatchConfig cfg;
        cfg.tntPath = tntPath_;
        cfg.dataRoot = dataRoot_;
        cfg.gods = room.opts.gods != 0;
        cfg.slots.resize(size_t(maxSlot + 1));
        for (int i = 0; i <= maxSlot; ++i) {
            const auto& s = room.slots[i];
            cfg.slots[size_t(i)] = {s.type == 1 || s.type == 2, s.faction % 5, s.team};
            colorSlot_[i & 7] = s.color % 10;
            playerAi_[i & 7] = (s.type == 2);
            playerName_[i & 7] = s.type == 2 ? ("AI " + std::to_string(i + 1))
                                 : s.name.empty() ? ("Player " + std::to_string(i + 1))
                                                  : s.name;
        }
        auto spots = tak::sim::setupMatch(world_, registry_, cfg);
        // client-only presentation
        localPlayer_ = room.mySlot < 0 ? 0 : room.mySlot;
        world_.setVisPlayer(localPlayer_);
        for (auto& u : world_.units())
            if (u.player == localPlayer_ && u.type) { playerMonarchId_ = u.id; builderId_ = u.id; break; }
        const char* sides[5] = {"ara", "tar", "ver", "zon", "cre"};
        side_ = sides[room.slots[localPlayer_].faction % 5];
        loadPanel(side_);
        if (!spots.empty())
            mapView_.setOffset(spots[0].first - 640 / 0.9f, spots[0].second - 400 / 0.9f);
    }

    // One networked frame: pump the connection, send this frame's local orders,
    // and simulate every tick the server has delivered a bundle for. Returns
    // false when the game/connection ends (see netError()).
    bool mpStep() {
        if (!mp_->poll()) { netError_ = mp_->error().empty() ? "disconnected" : mp_->error(); return false; }
        if (mp_->desynced()) { netError_ = mp_->desyncReason(); return false; }
        if (!outbox_.empty()) { mp_->sendCommands(outbox_); outbox_.clear(); }
        // Simulate every delivered tick, but cap per frame so a big catch-up
        // (rejoin replay) stays responsive rather than freezing for seconds.
        tak::net::Bundle bd;
        int drained = 0;
        while (outcome_ == 0 && drained < 512 && mp_->takeBundle(netTick_, bd)) {
            for (const auto& c : bd.cmds) apply(c);
            for (const auto& e : bd.events) applyEvent(e);
            update(1.0f / 30.0f);
            // Spectators observe only -- they seat no player and report no hash.
            if (netTick_ % 30 == 0 && !mp_->isSpectator())
                mp_->sendHash(netTick_, world_.stateHash());
            ++netTick_;
            ++drained;
        }
        // "Machine too slow" guard: if the backlog stays deep for a sustained
        // stretch, this client can't process ticks as fast as they arrive and
        // will never catch up -- fail clearly instead of falling ever further
        // behind (or reconnect-looping).
        uint64_t now = SDL_GetTicks64();
        if (mp_->bufferedBundles() > 900) {
            if (!mpSlowSinceMs_) mpSlowSinceMs_ = now;
            else if (now - mpSlowSinceMs_ > 5000) {
                netError_ = "this machine can't keep up with the game speed";
                return false;
            }
        } else {
            mpSlowSinceMs_ = 0;
        }
        return true;
    }

    // ---- replay playback (.takrep) ----------------------------------------
    // Build the world from a recorded match config and feed it the bundle log.
    void startReplay(const tak::sim::MatchConfig& cfg,
                     std::vector<tak::net::Bundle> bundles) {
        auto spots = tak::sim::setupMatch(world_, registry_, cfg);
        replayBundles_ = std::move(bundles);
        replayMode_ = true;
        noFog_ = true;             // a spectator sees the whole map
        localPlayer_ = 0;
        world_.setVisPlayer(0);
        side_ = "ara";
        loadPanel(side_);
        if (!spots.empty())
            mapView_.setOffset(spots[0].first - 640 / 0.9f, spots[0].second - 400 / 0.9f);
    }
    bool replayMode() const { return replayMode_; }
    // Advance playback by `dt` (real seconds), scaled by the game-speed control;
    // Pause freezes it. Applies each recorded bundle then ticks the world.
    void replayStep(float dt) {
        if (paused_ || replayTick_ >= replayBundles_.size()) return;
        replayAccum_ += dt * speedMult();
        int guard = 0;
        while (replayAccum_ >= 1.0f / 30.0f && replayTick_ < replayBundles_.size() && guard < 64) {
            const auto& bd = replayBundles_[replayTick_];
            for (const auto& c : bd.cmds) apply(c);
            for (const auto& e : bd.events) applyEvent(e);
            world_.tick(1.0f / 30.0f);
            ++replayTick_;
            replayAccum_ -= 1.0f / 30.0f;
            ++guard;
        }
    }
    size_t replayTick() const { return replayTick_; }
    size_t replayLength() const { return replayBundles_.size(); }

    uint64_t worldHashPublic() const { return world_.stateHash(); }
    size_t aliveUnits() const {
        size_t n = 0; for (auto& u : world_.units()) if (u.alive() && u.type) ++n; return n;
    }

    // Drive one iteration of the multiplayer lobby + game. autoMode: 0 = don't
    // auto-drive the lobby (a real UI will), 1 = auto-host (create + start at 2+
    // ready), 2 = auto-join the first game. Returns false when the session ends.
    // (M3 uses the auto modes; the interactive lobby UI is follow-on work.)
    bool mpAutoStep(int autoMode, const std::string& mapId, bool crusades) {
        using S = tak::net::MpClient::State;
        if (!mp_->poll()) { netError_ = mp_->error(); return false; }
        S st = mp_->state();
        if (st == S::Done) { if (netError_.empty()) netError_ = mp_->error(); return false; }
        if (st == S::Lobby && (autoMode == 1 || autoMode == 4)) {
            tak::net::GameOptions o; o.crusades = crusades ? 1 : 0;
            mp_->createGame("headless", "", mapId, o, mpCapacity());
        } else if (st == S::Lobby && autoMode == 5) {
            // Rejoin: read the resume ticket the original session saved and
            // reconnect to the held slot.
            uint32_t gid = 0; uint64_t tok = 0;
            if (readResume(gid, tok) && gid) { mp_->rejoin(gid, tok); mpReadied_ = true; }
        } else if (st == S::Lobby && autoMode == 6) {
            // Spectate: poll the list and watch the first running game.
            if (SDL_GetTicks64() - mpListMs_ > 300) { mp_->listGames(); mpListMs_ = SDL_GetTicks64(); }
            for (const auto& g : mp_->games())
                if (g.running) { mp_->spectate(g.id, ""); break; }
        } else if (st == S::Lobby && (autoMode == 2 || autoMode == 3)) {
            // Poll the game list; join the first, or (mode 3) create if none appear.
            if (SDL_GetTicks64() - mpListMs_ > 300) { mp_->listGames(); mpListMs_ = SDL_GetTicks64(); }
            if (!mp_->games().empty()) mp_->joinGame(mp_->games().front().id, "");
            else if (autoMode == 3 && mpListMs_ && SDL_GetTicks64() - mpFirstListMs_ > 800) {
                tak::net::GameOptions o; o.crusades = crusades ? 1 : 0;
                mp_->createGame(mapId, "", mapId, o, mpCapacity());
            }
            if (!mpFirstListMs_) mpFirstListMs_ = SDL_GetTicks64();
        } else if (st == S::InRoom && autoMode && !mpReadied_) {
            const auto& r = mp_->room();
            if (r.mySlot >= 0) {
                mp_->setSlot(r.mySlot, 1, uint8_t(r.mySlot % 5), uint8_t(r.mySlot),
                             uint8_t(r.mySlot), 1);
                // autoMode 4 (AI-game host): also seat one AI opponent in slot 1.
                if (autoMode == 4 && r.mySlot == 0)
                    mp_->setSlot(1, 2, 1, 1, 1, 1);   // AI, tar, colour 1, team 1
                // Host stress harness: TAK_MP_AIS=N seats N server AIs in the TOP
                // slots, leaving the low slots for human joiners.
                if (autoMode == 1 && r.mySlot == 0)
                    if (const char* ai = std::getenv("TAK_MP_AIS")) {
                        int nAi = std::clamp(std::atoi(ai), 0, tak::net::kMaxSlots - 1);
                        for (int k = 0; k < nAi; ++k) {
                            int slot = tak::net::kMaxSlots - 1 - k;
                            mp_->setSlot(slot, 2, uint8_t(slot % 5), uint8_t(slot),
                                         uint8_t(slot), 1);   // AI, distinct colour/team
                        }
                    }
                mpReadied_ = true;
            }
        } else if (st == S::InRoom && (autoMode == 1 || autoMode == 3 || autoMode == 4) &&
                   !mpStarted_ &&
                   mp_->room().hostId == mp_->myClientId()) {
            int ready = 0;
            for (int i = 0; i < tak::net::kMaxSlots; ++i) {
                const auto& s = mp_->room().slots[i];
                if ((s.type == 1 && s.ready) || s.type == 2) ++ready;   // human-ready or AI
            }
            // Default: start with any 2 ready. TAK_MP_WAIT=N holds for a full lobby.
            static const int wantReady = [] {
                const char* w = std::getenv("TAK_MP_WAIT"); return w ? std::atoi(w) : 2;
            }();
            if (ready >= wantReady) { mp_->startGame(); mpStarted_ = true; }
        } else if (mp_->isRejoin()) {
            // Rejoin OR spectate (checked BEFORE the state branches -- the replayed
            // bundles may already have flipped the state to InGame): reset the sim
            // and replay from tick 0. The server has queued the whole bundle log
            // after the GameStarting; mpStep drains it, fast-forwarding to now.
            bool spec = mp_->isSpectator();
            mp_->clearRejoin();
            world_.resetForReplay();
            netTick_ = 0; outcome_ = 0; netError_.clear();
            startMpGame(mp_->startRoom(), mp_->startSeed());
            if (spec) {
                spectating_ = true;   // watch-only: no fog, no control, no resume
                noFog_ = true;
            } else {
                mp_->reportLoaded();
                writeResume(mp_->gameId(), mp_->resumeToken());
            }
            mpSetupDone_ = true;
            std::fprintf(stderr, spec ? "spectating -- replaying to catch up...\n"
                                      : "rejoined -- replaying to catch up...\n");
            mpStep();   // drain the replay this frame
        } else if (mp_->starting() && !mpSetupDone_) {
            startMpGame(mp_->startRoom(), mp_->startSeed());
            mp_->reportLoaded();
            mpSetupDone_ = true;
            writeResume(mp_->gameId(), mp_->resumeToken());   // ticket for reconnect
        } else if (st == S::InGame) {
            return mpStep();
        }
        return true;
    }

    void applyEvent(const tak::net::Event& e) { tak::sim::applyEvent(world_, e); }

    // Apply one command (shared with the server's referee sim, so both mutate
    // the world identically).
    void apply(const tak::net::Command& c) { tak::sim::applyCommand(world_, registry_, c); }

    // One lockstep step: returns false while stalled waiting for the peer.
    uint32_t netTick() const { return netTick_; }
    tak::sim::World& worldRef() { return world_; }
    void selectOnly(int id) { selection_.clear(); selection_.push_back(id); }
    size_t menuSize(const std::string& id) { return registry_.buildable(id).size(); }
    bool hasIP() const { return !ipRoot_.empty(); }
    const std::string& netError() const { return netError_; }

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
        struct S { const char* t; float x, z; int player; };
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
            int id = spawn(sp.t, sp.x, sp.z, sp.player == 0 ? 1.57f : -1.57f, sp.player);
            if (id >= 0) (sp.player == 0 ? a : b).push_back(id);
        }
        for (size_t i = 0; i < a.size(); ++i)
            world_.attack(a[i], b[i % b.size()], false);
        for (size_t i = 0; i < b.size(); ++i)
            world_.attack(b[i], a[i % a.size()], false);
        mapView_.setOffset(1500 - 640 / 0.9f, 1380 - 400 / 0.9f);
    }

    void hillTest() {
        // Player 0 gets 2 scoring units in the region, player 1 gets 1.
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
        // araarch (correct, +h) vs zonhand, both walking east, arrows on.
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
            if (u.alive() && u.player == 0 && u.type && u.type->canMove) {
                if (leader < 0) leader = u.id;
                else world_.guard(u.id, leader, false);
            }
        if (leader >= 0) world_.order(leader, 1500, 950, false);
    }

    void marchTo(float dx, float dz) {
        float cx = mapView_.map().blocksX * 16.0f, cz = mapView_.map().blocksY * 16.0f;
        for (auto& u : world_.units())
            if (u.player == localPlayer_ && u.type && u.type->canMove) {
                tak::net::Command c;
                c.kind = tak::net::Cmd::AttackMove;
                c.unitId = u.id;
                c.x = cx + dx;
                c.z = cz + dz;
                issue(c);
            }
    }

    // Smoothed render FPS for the F4 overlay (set from the main loop each frame).
    void setFps(float f) { fps_ = fps_ > 0 ? fps_ * 0.9f + f * 0.1f : f; }

    // Choose which player-colour variant a player's units render in.
    void setPlayerColor(int player, int slot) {
        if (player >= 0 && player < 8 && slot >= 0) colorSlot_[player] = slot;
    }

    // Mouse edge scrolling: pan the camera while the cursor rests in the margin
    // at a window edge — including the very bottom of the screen (the HUD panel
    // never sits at the extreme edge, so this doesn't fight the build icons).
    void edgeScroll(float dt, float zm) {
        if (winW_ <= 0 || winH_ <= 0) return;
        if (mouseX_ < 0 || mouseX_ > winW_ || mouseY_ < 0 || mouseY_ > winH_) return;
        const float margin = 24.0f, panPx = 4000.0f;   // px/s at zoom 1
        // Trigger at the real window edges (incl. the far right, past the panel),
        // so the player pushes to the screen edge to scroll -- not to the map edge.
        float sx = 0, sz = 0;
        if (mouseX_ < margin) sx = -1;
        else if (mouseX_ > winW_ - margin) sx = 1;
        if (mouseY_ < margin) sz = -1;
        else if (mouseY_ > winH_ - margin) sz = 1;
        if (sx == 0 && sz == 0) return;
        follow_ = false;   // the player is driving the camera now
        trackSel_ = false;
        mapView_.setOffset(mapView_.offX() + sx * panPx * dt / zm,
                           mapView_.offY() + sz * panPx * dt / zm);
    }

    void update(float dt) {
        sounds_.pollMusic();
        float zm = std::max(mapView_.zoom(), 1e-3f);
        edgeScroll(dt, zm);    // pan when the cursor rests near a screen edge (real time)
        if (paused_) return;   // freeze the sim; input/render keep running
        // Game speed: scale game time (sim, effects, AI, animation all follow dt).
        // edgeScroll above already ran on the real dt, so the camera stays real-time.
        dt *= speedMult();
        double _sim0 = double(SDL_GetPerformanceCounter());
        // Advance the sim in sub-steps capped at 1/30s so fast speeds (or a laggy
        // frame) can't move a unit far enough to tunnel a wall; effects/AI below use
        // the full scaled dt (they only interpolate, so a big step is harmless).
        for (float rem = dt, guard = 0; rem > 1e-5f && guard < 16; ++guard) {
            float step = std::min(rem, 1.0f / 30.0f);
            world_.tick(step);
            runAi();   // AI evaluated per SIM TICK -> deterministic ~1 Hz cadence
            rem -= step;
        }
        profSimMs_ += (double(SDL_GetPerformanceCounter()) - _sim0)
                      / (double(SDL_GetPerformanceFrequency()) / 1000.0);
        // Weapon impacts this tick: play each weapon's soundhitclass, picking the
        // material-specific variant from the struck unit's bodytype (flesh/armor/..).
        for (const auto& h : world_.hits()) {
            if (h.weapon && !h.weapon->soundHit.empty()) {
                const std::string& body = h.target ? h.target->bodyType : std::string("default");
                const std::string* wav = soundClasses_.pick(h.weapon->soundHit, body, salt_++);
                if (!wav) wav = soundClasses_.pick(h.weapon->soundHit, "default", salt_++);
                if (wav) sounds_.playWorld(*wav, h.x, h.z);
            }
            // Impact visual: play the weapon's real GAF/TAF explosion effect
            // (water variant over water); fall back to procedural particles when
            // the class or its art is unavailable.
            if (h.weapon) {
                const std::string& cls = (world_.isWater(h.x, h.z) &&
                                          !h.weapon->waterExplosionClass.empty())
                                             ? h.weapon->waterExplosionClass
                                             : h.weapon->explosionClass;
                // Lift the blast onto an airborne target (shooting down a flyer).
                float tAlt = flyerAltAt(h.x, h.z) * 0.8f;
                if (!spawnEffect(cls, h.x, h.z, tAlt)) spawnImpact(*h.weapon, h.x, h.z, tAlt);
            }
            // Weapon area-effect: expanding shockwave rings (radiusart, staggered
            // by ringdelay) and ground fire (firestarter) at the impact.
            if (h.weapon) {
                float maxR = std::max(h.weapon->aoe * 0.5f, 48.0f);
                for (int i = 0; i < h.weapon->ringCount && i < 3; ++i)
                    if (!h.weapon->radiusArt[i].empty())
                        spawnRing(h.weapon->radiusArt[i], h.x, h.z,
                                  float(i) * h.weapon->ringDelay, h.weapon->ringDur,
                                  h.weapon->spriteCount, maxR);
                if (h.weapon->fireStarter && !world_.isWater(h.x, h.z))
                    spawnEffectAnim("flame", h.x, h.z, 0.0f, 0.0f, 5);   // fire lingers
                // Camera shake for heavy impacts you can actually see.
                if (h.weapon->shakeMag > 0 && world_.cellVisible(h.x, h.z))
                    triggerShake(h.weapon->shakeMag, h.weapon->shakeDur);
            }
            if (h.target && h.target->bodyType == "flesh")
                spawnBurst(h.x, h.z, 5, h.target->blood[0], h.target->blood[1],
                           h.target->blood[2], 26, 1.8f, 0);
        }
        world_.clearHits();
        updateParticles(dt);
        updateEffects(dt);
        updateRings(dt);
        if (shakeTime_ > 0) shakeTime_ = std::max(0.0f, shakeTime_ - dt);
        // God economy: once a player's favour fills after the appear time, its
        // faction's god manifests among its forces.
        if (world_.godsEnabled())
            for (int t = 0; t < world_.numPlayers(); ++t)
                if (world_.godReady(t)) summonGod(t);
        if (getenv("TAK_STUCKSTAT")) {   // crowd-jam diagnostic
            static float acc = 0; acc += dt;
            if (acc >= 2.0f) {
                acc = 0;
                int moving = 0, stalled = 0, ordered = 0;
                for (auto& u : world_.units()) {
                    if (!u.alive() || !u.type || !u.type->canMove || u.type->canFly ||
                        u.orders.empty() || u.orders.front().targetId != 0) continue;
                    ordered++;
                    if (u.speed > 3.0f) moving++; else stalled++;
                }
                std::printf("stuckstat t=%.0f ordered=%d moving=%d stalled=%d\n",
                            animClock_, ordered, moving, stalled);
                std::fflush(stdout);
            }
        }
        for (auto& u : world_.units())
            if (u.type && u.alive() && !unitType_.count(u.id)) registerUnit(u);
        if (briefTimer_ > 0) briefTimer_ -= dt;
        animClock_ += dt;
        if (!spawnRules_.empty() || !messages_.empty()) scenClock2_ += dt;
        for (auto& sr : spawnRules_) {
            if (sr.atTime >= 0) {
                if (!sr.done && scenClock2_ >= sr.atTime) {
                    sr.done = true;
                    spawn(sr.type, sr.x, sr.z, 0, sr.player);
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
                if (have < sr.maintainCount) spawn(sr.type, sr.x, sr.z, 0, sr.player);
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
                        cx <= scenRegion_.x2 && cz <= scenRegion_.z2 && u.player < 4)
                        ++counts[u.player];
                }
                int best = 0;
                for (int i = 1; i < 4; ++i)
                    if (counts[i] > counts[best]) best = i;
                bool tie = false;
                for (int i = 0; i < 4; ++i)
                    if (i != best && counts[i] == counts[best]) tie = true;
                std::printf("scenario result: %d %d %d %d -> %s\n", counts[0],
                            counts[1], counts[2], counts[3],
                            tie ? "tie" : (best == localPlayer_ ? "win" : "loss"));
                outcome_ = (!tie && best == localPlayer_) ? 1 : -1;
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
                        if (!u.alive() || u.embarked() || u.player != 0) continue;
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
                if (u.player != 0) continue;
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
                        if (u.alive() && !u.embarked() && u.player == 0 && u.type &&
                            u.type->canMove && !u.type->canTransport)
                            world_.order(u.id, amphibLandX_, amphibLandZ_, false);
                    amphibPhase_ = 2;
                }
            }
        }

        // Victory check: last team standing. The sim computes winningTeam() and
        // per-player defeated flags each tick (deterministic across peers); the
        // viewer just maps that to this player's win/lose banner. Only armed once
        // at least two teams have fielded units (staged demos may field one).
        if (outcome_ == 0) {
            int teamsSeen = 0;
            for (int t = 0; t < world_.numPlayers(); ++t) {
                bool any = false;
                for (auto& u : world_.units())
                    if (u.alive() && u.type && world_.player(u.player).team == t) { any = true; break; }
                if (any) sawTeam_[t] = true;
                if (sawTeam_[t]) ++teamsSeen;
            }
            int win = world_.winningTeam();
            if (teamsSeen >= 2 && win >= 0) {
                outcome_ = (win == world_.player(localPlayer_).team) ? 1 : -1;
            } else if (world_.player(localPlayer_).defeated && teamsSeen >= 2) {
                // My whole team may still be alive via allies; only lose when the
                // sim says my team is gone, but a solo (FFA) defeat ends my game.
                bool teamAlive = false;
                for (int p = 0; p < world_.numPlayers(); ++p)
                    if (world_.player(p).team == world_.player(localPlayer_).team &&
                        !world_.player(p).defeated) { teamAlive = true; break; }
                if (!teamAlive) outcome_ = -1;
            }
        }
        // T-tracking: keep the camera on the selection; drop out if it's all gone.
        if (trackSel_ && !centerOnSelection()) trackSel_ = false;
        if (follow_ && !world_.units().empty()) {
            // Track moving friendly units; fall back to everyone.
            float cx = 0, cz = 0;
            int n = 0;
            for (auto& u : world_.units())
                if (u.alive() && u.player == 0 && u.type && u.type->canMove &&
                    u.moving()) { cx += u.x; cz += u.z; ++n; }
            if (!n)
                for (auto& u : world_.units())
                    if (u.alive()) { cx += u.x; cz += u.z; ++n; }
            if (n)
                mapView_.setOffset(cx / float(n) - 640 / mapView_.zoom(),
                                   cz / float(n) - 400 / mapView_.zoom());
        }
        for (auto& u : world_.units()) {
            if (u.alive()) maybeSwapVeteranModel(u);
            auto it = anims_.find(u.id);
            if (u.justFired && u.type) {
                using Fx = tak::sim::WeaponFx;
                const auto& w = u.type->weapon;
                if (w.melee)
                    sounds_.playWorld("ahitfl0" + std::to_string(1 + (salt_++ % 3)), u.x, u.z);
                else if (w.fx == Fx::Fire)
                    sounds_.playWorld(sounds_.has("firedrag") ? "firedrag" : "fireflsh", u.x, u.z);
                else if (w.fx == Fx::Lightning)
                    sounds_.playWorld("lightng" + std::to_string(1 + (salt_++ % 3)), u.x, u.z);
                else
                    sounds_.playWorld("bow2", u.x, u.z);
                // Muzzle flash: a quick bright puff at the weapon, just ahead of
                // the unit along its facing (skip melee swings).
                if (!w.melee) {
                    float fx = u.x + std::sin(u.heading) * 11.0f;
                    float fz = u.z + std::cos(u.heading) * 11.0f;
                    Uint8 mr = 255, mg = 235, mb = 150;   // arrow/generic = warm
                    if (w.fx == Fx::Lightning) { mr = 200; mg = 225; mb = 255; }
                    else if (w.fx == Fx::Fire) { mr = 255; mg = 150; mb = 60; }
                    spawnBurst(fx, fz, 4, mr, mg, mb, 14, 1.4f, 0);
                }
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
                    if (sounds_.has(id + "die1")) sounds_.playWorld(id + "die1", u.x, u.z);
                    else if (sounds_.has(id + "die2")) sounds_.playWorld(id + "die2", u.x, u.z);
                    // Death effect: a real GAF explosion sized to the unit (bigger
                    // footprint => bigger blast), plus blood particles for flesh.
                    int foot = std::max(u.type->footX, u.type->footZ);
                    const char* deathCls = foot >= 3 ? "large explosion"
                                         : foot == 2 ? "medium explosion"
                                                     : "small explosion";
                    float dAlt = unitAltById(u.id) * 0.8f;   // a flyer explodes mid-air
                    spawnEffect(deathCls, u.x, u.z, dAlt);
                    if (u.type->bodyType == "flesh") {
                        spawnEffect("blood explosion", u.x, u.z, dAlt);
                        spawnBurst(u.x, u.z, 14, u.type->blood[0], u.type->blood[1],
                                   u.type->blood[2], 40, 2.2f, 0, dAlt);
                    } else
                        spawnBurst(u.x, u.z, 10, 110, 100, 90, 30, 2.4f, 1, dAlt);
                    if (missionVm_ && u.player == 0)
                        missionVm_->start("UnitDestroyed", {u.id});
                }
                continue;   // VM advanced in the parallel pass below
            }
            if (a.flying) {
                // Take off when moving, settle back to the ground when idle.
                float cruise = u.type ? u.type->cruiseAlt : 0.0f;
                // Flyers cruise while doing anything — moving, or hovering to
                // conjure — and touch down when idle, playing the `land` script
                // for a proper folded-wing landed pose (not the wings-spread
                // rest "T-pose").
                bool busy = u.walking() || !u.orders.empty() ||
                            u.buildSiteId != 0 || !u.buildOrders.empty();
                float target = busy ? cruise : 0.0f;
                float step = std::max(cruise, 1.0f) / 0.7f * dt;   // ~0.7s to cruise
                a.altitude += std::clamp(target - a.altitude, -step, step);

                // Run the flight animation whenever she is airborne (always, in
                // practice, since idle only settles to a low hover).
                bool air = a.altitude > std::max(cruise, 1.0f) * 0.3f;
                // The flyer `fly` script gates its whole body/wing animation on
                // a static whose index differs per unit (zonhunt=8, zongod and
                // zonharp=7); a.flyGate is read from the bytecode. Setting the
                // wrong index leaves `fly` inert — the unit sits in its wings-
                // spread rest pose (a T-pose) and never picks up the fly-pose
                // body turn, so it reads static and backward.
                if (air) {
                    if (!a.airborne) {
                        a.airborne = true;
                        a.vm->reset();
                        a.vm->setStatic(a.flyGate, 1);   // gate that "fly" animates on
                        a.vm->start("fly");
                    } else if (a.vm->threadCount() == 0) {
                        a.vm->start("fly");       // keep the beat looping
                    }
                } else if (a.airborne) {
                    a.airborne = false;
                    a.vm->reset();
                    a.vm->setStatic(a.flyGate, 0);
                    a.vm->start("land") || a.vm->start("restore_x");   // landed pose
                }
            } else {
                bool m = u.walking();
                if (m != a.walking) {
                    a.walking = m;
                    a.vm->reset();
                    a.vm->setStatic(0, m ? 1 : 0);
                    if (m) { a.vm->start("walk") || a.vm->start("walk_legs"); }
                    else { a.vm->start("restore_x") || a.vm->start("restore_legs"); }
                    a.firing = false;
                } else if (m && a.vm->threadCount() == 0) {
                    // The walk script is single-pass; the engine re-invokes it
                    // each cycle while the unit keeps moving.
                    a.vm->start("walk") || a.vm->start("walk_legs");
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
            // (The VM itself is advanced in the parallel pass below.)
        }

        // Advance every unit's animation VM in parallel. Each VM is independent:
        // it only reads sim state through onGet (no writes) and its EMIT_SFX/
        // PLAY_SOUND opcodes are no-ops here, so ticking off the main thread is
        // safe. The state transitions above (sounds, script starts) stayed
        // serial. Flyer VMs still advance at 1x real time.
        vmTick_.clear();
        for (auto& [id, a] : anims_)
            if (a.vm) vmTick_.push_back(a.vm.get());
        pool_.parallelFor(vmTick_.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) vmTick_[i]->tick(dt);
        });
    }

    // Create textures (terrain chunks, minimap) before the render pass.
    void prepare(int winW, int winH) {
        mapView_.ensureChunks(mapViewW(winW), winH);
        if (!miniTex_) buildMinimap();
        // Listener = the camera view (the map viewport), so positional sounds pan by
        // where the source sits on SCREEN, not by its absolute map position. This
        // runs every frame for SP, net, AND spectator -- update() is skipped on the
        // net/spectator path, so the listener can't live there.
        float zm = std::max(mapView_.zoom(), 1e-3f);
        float halfW = (mapViewW(winW) / 2.0f) / zm, halfH = (winH / 2.0f) / zm;
        sounds_.setListener(mapView_.offX() + halfW, mapView_.offY() + halfH, halfW, halfH);
    }

    // Fetch and reset the per-draw sub-phase timers (for TAK_PROF).
    void takeProf(double& projMs, double& submitMs, double& simMs, long& lod, long& full) {
        projMs = profProjMs_; submitMs = profSubmitMs_; simMs = profSimMs_;
        lod = lodDrawn_; full = fullDrawn_;
        profProjMs_ = 0; profSubmitMs_ = 0; profSimMs_ = 0; lodDrawn_ = 0; fullDrawn_ = 0;
    }

    void draw(int winW, int winH) {
        if (inLobbyPhase()) { drawLobby(winW, winH); return; }
        // Pull any chat that arrived and age the overlay on a real wall clock, so
        // it fades even while the game is paused or catching up on ticks.
        if (mp_) {
            for (auto& m : mp_->takeChat()) gameChat_.push_back({m.first, m.second, 0});
            uint64_t nowMs = SDL_GetTicks64();
            float cdt = chatLastMs_ ? (nowMs - chatLastMs_) / 1000.0f : 0;
            chatLastMs_ = nowMs;
            if (!chatTyping_) for (auto& g : gameChat_) g.age += cdt;
            if (gameChat_.size() > 16) gameChat_.erase(gameChat_.begin(), gameChat_.end() - 16);
        }
        // Camera shake: nudge the map offset by a decaying oscillation for this
        // frame, so the whole world jolts; the offset is restored at the end so
        // the camera and UI stay put. shakemagnitude ~3 => a few px of jolt.
        float shakeBaseX = mapView_.offX(), shakeBaseY = mapView_.offY();
        bool shaking = shakeTime_ > 0 && shakeDur_ > 0;
        if (shaking) {
            float decay = shakeTime_ / shakeDur_;
            float amp = shakeMag_ * 3.0f * decay;    // pixels
            float dx = amp * std::sin(animClock_ * 91.0f);
            float dz = amp * std::cos(animClock_ * 73.0f);
            float zm = std::max(mapView_.zoom(), 1e-3f);
            mapView_.setOffset(shakeBaseX + dx / zm, shakeBaseY + dz / zm);
        }
        // Everything world-space (map, units, effects, bars) is clipped to the map
        // viewport so it never bleeds under the right-hand panel.
        int mvw = mapViewW(winW);
        SDL_Rect worldClip{0, 0, mvw, winH};
        SDL_RenderSetClipRect(ren_, &worldClip);
        mapView_.draw(mvw, winH);
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
            // Unregistered (e.g. a type whose model failed to load): not drawable,
            // and every render path does unitType_.at(u.id) -- skip it here so none
            // of them throw (a throw in the parallel projection aborts the process).
            if (!unitType_.count(u.id)) continue;
            if (!noFog_ && !alliedToLocal(u.player) && !world_.cellVisible(u.x, u.z)) continue;
            // Frustum cull: only units whose anchor falls in (or just outside) the
            // map viewport are projected and drawn. The margin is generous and
            // asymmetric -- models extend well above their anchor, so a unit above
            // the top edge can still show its lower body. Without this, every
            // fog-visible unit was drawn regardless of camera position, so the
            // frame rate didn't improve when the crowd scrolled off screen.
            float sx = (u.x - mapView_.offX()) * zm0, sy = (u.z - mapView_.offY()) * zm0;
            if (sx < -160 || sx > mvw + 160 || sy < -260 || sy > winH + 120) continue;
            items.push_back({u.z, &u, nullptr});
        }
        std::stable_sort(items.begin(), items.end(),
                  [](const Item& a, const Item& b) { return a.z < b.z; });

        // Project every visible unit's model in parallel before drawing. This is
        // the heavy per-frame CPU work (matrix-transforming each unit's model tree
        // and building its vertex buffer); the render thread then only submits the
        // finished geometry, one texture-batched draw call per unit. Without this
        // the whole frame is single-threaded and pegs one core at large unit counts.
        visUnits_.clear();
        geomIndex_.clear();
        for (const auto& it : items)
            if (it.u) { geomIndex_[it.u->id] = int(visUnits_.size());
                        visUnits_.push_back(it.u); }
        if (geomPool_.size() < visUnits_.size()) geomPool_.resize(visUnits_.size());
        // Build the texture atlas for every colour slot in view (main thread; the
        // parallel pass below only reads the finished atlas pointers).
        for (const auto* u : visUnits_) atlasFor(colorSlot_[u->player & 7]);
        // Ensure an impostor sprite exists for every visible model when zoomed out
        // enough that LOD may kick in (main thread; the parallel pass only reads it).
        // Budgeted: a few NEW bakes per frame, so a first zoom-out over a mixed army
        // spreads its render-target allocations/captures across frames instead of
        // bursting them all into one already-slow frame (units not yet baked just
        // draw as full models for a few more frames).
        if (lodEnabled_ && mapView_.zoom() < kLodZoomGate) {
            int budget = 2;
            for (const auto* u : visUnits_) {
                if (!u->type) continue;
                auto key = std::make_pair(unitType_.at(u->id), colorSlot_[u->player & 7]);
                if (impostors_.count(key)) continue;
                if (budget-- <= 0) break;
                ensureImpostor(key.first, key.second, u->type->canMove);
            }
        }
        // Bake sprite sheets for visible models when sprite mode is on -- one NEW
        // set per frame (each is kSprFacings x kSprFrames render-target captures).
        if (spritesEnabled_) {
            for (const auto* u : visUnits_) {
                if (!u->type) continue;
                auto key = std::make_pair(unitType_.at(u->id), colorSlot_[u->player & 7]);
                if (sprites_.count(key)) continue;
                bakeSprites(key.first, key.second, u->type->canMove, u->type->canFly);
                break;
            }
        }
        double _pt0 = double(SDL_GetPerformanceCounter());
        pool_.parallelFor(visUnits_.size(), [this](size_t b, size_t e) {
            thread_local std::vector<Tri> scratch;
            for (size_t i = b; i < e; ++i)
                buildUnitGeom(*visUnits_[i], geomPool_[i], scratch);
        });
        double _ptFreq = double(SDL_GetPerformanceFrequency()) / 1000.0;
        profProjMs_ += (double(SDL_GetPerformanceCounter()) - _pt0) / _ptFreq;
        // Tally impostor vs full-model draws this frame (for TAK_PROF).
        for (size_t _i = 0; _i < visUnits_.size(); ++_i) {
            const auto& _g = geomPool_[_i];
            if (_g.runs.empty()) continue;
            if (impAtlas_ && _g.runs[0].first == impAtlas_) ++lodDrawn_;
            else ++fullDrawn_;
        }
        double _st0 = double(SDL_GetPerformanceCounter());

        // Is this unit drawn whole by drawUnit (needs clip rects / interleaved
        // effects) rather than folded into the shared batches?
        auto special = [&](const tak::sim::Unit& u, const UnitGeom& g) {
            bool occluded = !g.canFly && g.occY < g.ay - 2.0f;
            bool conjuring = u.underConstruction && u.type;
            return occluded || conjuring;
        };

        // Pass 1: every normal unit's ground shadows, batched. Soft blobs go into
        // one untextured triangle batch; FBI shadow sprites are batched per shadow
        // texture. Drawn first so all shadows sit under all bodies. (Special units
        // draw their own shadow inside drawUnit in pass 2.)
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        shadowBatch_.clear();
        for (const auto& it : items) {
            if (!it.u) continue;
            const auto& u = *it.u;
            // Soft blob batches for every mobile ground unit, special or not (a
            // special unit's own body still draws whole in pass 2).
            if (u.alive() && u.type && u.type->canMove && !u.type->canFly) {
                float sx = (u.x - mapView_.offX()) * zm0 - terrainLiftX(u.x, u.z) * zm0;
                float sy = (u.z - mapView_.offY()) * zm0 + 2 * zm0
                           - terrainLift(u.x, u.z) * zm0;
                pushQuad(shadowBatch_, sx - 7 * zm0, sy - 2.5f * zm0,
                         14 * zm0, 5 * zm0, SDL_Color{0, 0, 0, 70});
            }
        }
        if (!shadowBatch_.empty())
            SDL_RenderGeometry(ren_, nullptr, shadowBatch_.data(),
                               int(shadowBatch_.size()), nullptr, 0);
        {   // FBI shadow sprites, batched by shadow texture (flush on change).
            unitBatch_.clear();
            SDL_Texture* st = nullptr;
            auto flush = [&] {
                if (!unitBatch_.empty())
                    SDL_RenderGeometry(ren_, st, unitBatch_.data(),
                                       int(unitBatch_.size()), nullptr, 0);
                unitBatch_.clear();
            };
            for (const auto& it : items) {
                if (!it.u) continue;
                const auto& u = *it.u;
                auto git = geomIndex_.find(u.id);
                if (git == geomIndex_.end()) continue;
                const UnitGeom& g = geomPool_[size_t(git->second)];
                if (special(u, g) || !u.type || u.underConstruction) continue;
                // Impostor-sized units are too small for a ground shadow to read.
                if (impAtlas_ && !g.runs.empty() && g.runs[0].first == impAtlas_) continue;
                const ShadowTex* sh = shadowFor(u.type->shadowArt);
                if (!sh) continue;
                if (sh->tex != st) { flush(); st = sh->tex; }
                float sox = (6.0f + g.alt * 0.5f) * zm0, soy = (3.0f + g.alt * 0.25f) * zm0;
                pushQuad(unitBatch_, g.ax - sh->xoff * zm0 + sox,
                         g.ay - sh->yoff * zm0 + soy, sh->w * zm0, sh->h * zm0,
                         SDL_Color{255, 255, 255, 255});
            }
            flush();
        }

        // Pass 2: bodies (feature sprites + unit models) in depth order. Unit
        // models are accumulated into one batch and flushed only when the texture
        // changes or a feature/special unit interrupts the run -- so a crowd of one
        // unit type collapses to a handful of draw calls instead of one per unit.
        // Plan the draw order serially (no vertex copies), scatter the vertex
        // copies across the worker pool, then replay the draws. This spreads the
        // ~1.5M-vertex body assembly that used to peg one core, while keeping the
        // exact painter order (segments broken by features / special units).
        copyTasks_.clear();
        drawOps_.clear();
        int destOff = 0;
        SDL_Texture* segTex = nullptr;
        int segStart = 0, segCount = 0;
        auto closeSeg = [&] {
            if (segCount > 0) {
                drawOps_.push_back({nullptr, nullptr, segTex, segStart, segCount});
                segCount = 0;
            }
            // Force the next run to re-anchor segStart to the current destOff. Without
            // this, a unit whose texture matches segTex but follows a feature/special
            // unit (which closed the segment) keeps the PREVIOUS segment's segStart and
            // gets drawn from the wrong vertices -- so it renders an earlier unit's body
            // and vanishes from its own spot (camera-dependent, as depth order shifts).
            segTex = nullptr;
        };
        for (const auto& it : items) {
            if (it.f) {
                closeSeg();
                drawOps_.push_back({nullptr, it.f, nullptr, 0, 0});
            } else {
                const auto& u = *it.u;
                auto git = geomIndex_.find(u.id);
                if (git == geomIndex_.end()) continue;
                const UnitGeom& g = geomPool_[size_t(git->second)];
                if (special(u, g)) {
                    closeSeg();
                    drawOps_.push_back({&u, nullptr, nullptr, 0, 0});
                    continue;
                }
                int src = 0;
                for (const auto& r : g.runs) {
                    if (r.first != segTex) { closeSeg(); segTex = r.first; segStart = destOff; }
                    copyTasks_.push_back({git->second, src, r.second, destOff});
                    destOff += r.second; segCount += r.second; src += r.second;
                }
            }
        }
        closeSeg();
        bodyVerts_.resize(size_t(destOff));
        pool_.parallelFor(copyTasks_.size(), [this](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const CopyTask& t = copyTasks_[i];
                const auto& gv = geomPool_[size_t(t.geom)].verts;
                std::copy(gv.begin() + t.src, gv.begin() + t.src + t.count,
                          bodyVerts_.begin() + t.dst);
            }
        });
        for (const DrawOp& op : drawOps_) {
            if (op.f) {
                const auto& f = *op.f;
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
            } else if (op.u) {
                drawUnit(*op.u);
            } else if (op.count > 0) {
                SDL_RenderGeometry(ren_, op.tex, bodyVerts_.data() + op.start,
                                   op.count, nullptr, 0);
            }
        }
        profSubmitMs_ += (double(SDL_GetPerformanceCounter()) - _st0) / _ptFreq;

        // Ghosts of the local player's queued (shift) build orders.
        for (const auto& u : world_.units())
            if (u.alive() && u.player == localPlayer_)
                for (const auto& bo : u.buildOrders)
                    if (bo.type) drawGhostAt(bo.type, bo.x, bo.z);

        // Projectiles: drawn per weapon family (only where visible).
        float zm = mapView_.zoom();
        for (const auto& p : world_.projectiles()) {
            if (!world_.cellVisible(p.x, p.z)) continue;
            float t = std::clamp(p.age / std::max(p.flight, 0.05f), 0.0f, 1.0f);
            // Flyer shots: lift the whole trajectory by the altitude interpolated
            // from the firing unit down to the target (0.8x, matching the sprite
            // lift), so a drake's breath leaves its mouth and arcs to the ground.
            float palt = (unitAltById(p.fromId) * (1 - t) + unitAltById(p.targetId) * t)
                         * 0.8f * zm;
            if (p.fx == tak::sim::WeaponFx::Lightning) {
                // Flat, fast, jagged blue-white bolt from source toward target.
                float sx = (p.x - mapView_.offX()) * zm - terrainLiftX(p.x, p.z) * zm;
                float sy = (p.z - mapView_.offY()) * zm - 12 * zm - terrainLift(p.x, p.z) * zm
                           - palt;
                float len = 22.0f;
                float bx = -p.vx, bz = -p.vz;
                float bl = std::max(std::sqrt(bx * bx + bz * bz), 1e-3f);
                bx /= bl; bz /= bl;
                float px = sx, py = sy;
                SDL_SetRenderDrawColor(ren_, 210, 230, 255, 255);
                for (int s = 1; s <= 4; ++s) {
                    float d = len * zm * s / 4.0f;
                    float jitter = ((s * 1327 + int(p.age * 900)) % 7 - 3) * 1.6f * zm;
                    float nx = sx + bx * d - bz * jitter;
                    float ny = sy + bz * d + bx * jitter - 12 * zm * s / 4.0f;
                    SDL_RenderDrawLineF(ren_, px, py, nx, ny);
                    px = nx; py = ny;
                }
            } else if (p.fx == tak::sim::WeaponFx::Fire) {
                // Flame breath: a short stream of flickering orange/yellow puffs
                // trailing behind the leading tip, not a single fireball.
                float bx = -p.vx, bz = -p.vz;
                float bl = std::max(std::sqrt(bx * bx + bz * bz), 1e-3f);
                bx /= bl; bz /= bl;
                SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
                for (int s = 0; s < 5; ++s) {
                    float back = s * 6.0f;   // world px behind the tip
                    float wob = ((s * 811 + int(p.age * 1000)) % 5 - 2) * 2.0f;
                    float fx = p.x + bx * back - bz * wob;
                    float fz = p.z + bz * back + bx * wob;
                    float sx = (fx - mapView_.offX()) * zm - terrainLiftX(fx, fz) * zm;
                    float sy = (fz - mapView_.offY()) * zm - 12 * zm - terrainLift(fx, fz) * zm
                               - palt;
                    float r = (4.0f - s * 0.6f) * zm;   // shrinks toward the tail
                    Uint8 aA = Uint8(200 - s * 30);
                    // outer orange
                    SDL_SetRenderDrawColor(ren_, 230, 90, 25, aA);
                    SDL_FRect o{sx - r, sy - r, 2 * r, 2 * r};
                    SDL_RenderFillRectF(ren_, &o);
                    // hot yellow core at the leading puffs
                    if (s < 2) {
                        SDL_SetRenderDrawColor(ren_, 255, 220, 110, aA);
                        SDL_FRect c{sx - r * 0.45f, sy - r * 0.45f, r * 0.9f, r * 0.9f};
                        SDL_RenderFillRectF(ren_, &c);
                    }
                }
            } else {
                // Arrow/bolt/cannonball: a yellow streak. Ballistic weapons (FBI
                // type = Ballistic) lob a high arc scaled by flight time; other
                // shots (line-of-sight bolts) fly nearly flat.
                bool bal = p.wsrc && p.wsrc->ballistic;
                float peak = bal ? std::min(95.0f, p.flight * 55.0f)
                                 : std::min(18.0f, p.flight * 12.0f);
                float h = 8 + 4 * peak * t * (1 - t);
                float sx = (p.x - mapView_.offX()) * zm - terrainLiftX(p.x, p.z) * zm;
                float sy = (p.z - mapView_.offY()) * zm - h * zm - terrainLift(p.x, p.z) * zm
                           - palt;
                SDL_SetRenderDrawColor(ren_, 255, 235, 140, 255);
                SDL_RenderDrawLineF(ren_, sx, sy, sx - p.vx * 0.035f * zm,
                                    sy - p.vz * 0.035f * zm + (t < 0.5f ? 2.5f : -2.5f) * zm);
            }
        }
        drawParticles();
        drawEffects();

        drawFog();
        if (buildDrag_ && placing_) {
            float mx, mz;
            pickWorld(mouseX_, mouseY_, mx, mz);
            for (auto& [x, z] : buildLinePositions(bdX0_, bdZ0_, mx, mz))
                drawGhostAt(placing_, x, z);
        } else if (placing_) {
            drawGhost();
        }

        // Selection membership as a hash set: the old code did world_.unit(id) (a
        // linear scan) per selected unit and std::find(selection_) per world unit
        // -- both O(n^2) once a big army was selected, which tanked the frame.
        selSet_.clear();
        selSet_.insert(selection_.begin(), selection_.end());
        // Selection brackets: iterate units once, batched into a single draw
        // (viewport-culled, thin green quads).
        shadowBatch_.clear();
        if (!selSet_.empty()) {
            const SDL_Color grn{70, 240, 90, 255};
            float zms = mapView_.zoom();
            for (const auto& u : world_.units()) {
                if (!u.alive() || !selSet_.count(u.id)) continue;
                float cx = (u.x - mapView_.offX()) * zms - uLiftX(u) * zms;
                float cy = (u.z - mapView_.offY()) * zms - uLiftY(u) * zms;
                if (cx < -40 || cx > mvw + 40 || cy < -40 || cy > winH + 40) continue;
                float rr = 11.0f, rx = rr * zms, ry = rr * 0.65f * zms;
                if (rx < 9.0f) {
                    // Tiny on screen (a whole army zoomed out): one small marker
                    // quad instead of eight bracket segments -- 8x less geometry.
                    float s = std::max(2.0f, rx * 0.6f);
                    pushQuad(shadowBatch_, cx - s, cy - s * 0.65f, 2 * s, 2 * s * 0.65f, grn);
                    continue;
                }
                float L = rr * 0.45f * zms, th = std::max(1.0f, 1.2f * zms);
                for (int sx = -1; sx <= 1; sx += 2)
                    for (int sy = -1; sy <= 1; sy += 2) {
                        float px = cx + sx * rx, py = cy + sy * ry;
                        pushQuad(shadowBatch_, std::min(px, px - sx * L), py - th * 0.5f,
                                 L, th, grn);
                        pushQuad(shadowBatch_, px - th * 0.5f,
                                 std::min(py, py - sy * L * 0.65f), th, L * 0.65f, grn);
                    }
                for (const auto& o : u.orders)   // move-order rings (few)
                    if (o.targetId == 0) drawRing(o.x, o.z, 4);
            }
            // Attack-target indicator: RED brackets on any enemy a selected unit is
            // ordered to attack, so you can see what you've told them to hit.
            const SDL_Color red{245, 70, 60, 255};
            std::unordered_set<int> targets;
            for (int sid : selection_)
                if (const auto* su = world_.unit(sid))
                    for (const auto& o : su->orders)
                        if (o.targetId > 0) targets.insert(o.targetId);
            for (int tid : targets) {
                const auto* t = world_.unit(tid);
                if (!t || !t->alive()) continue;
                SDL_FPoint p = unitScreen(*t);   // includes flyer altitude
                float cx = p.x, cy = p.y + 12.0f * zms;   // undo unitScreen's body bias
                if (cx < -40 || cx > mvw + 40 || cy < -40 || cy > winH + 40) continue;
                float rr = 11.0f, rx = rr * zms, ry = rr * 0.65f * zms;
                float L = std::max(3.0f, rr * 0.45f * zms), th = std::max(1.0f, 1.2f * zms);
                for (int sx = -1; sx <= 1; sx += 2)
                    for (int sy = -1; sy <= 1; sy += 2) {
                        float px = cx + sx * rx, py = cy + sy * ry;
                        pushQuad(shadowBatch_, std::min(px, px - sx * L), py - th * 0.5f,
                                 L, th, red);
                        pushQuad(shadowBatch_, px - th * 0.5f,
                                 std::min(py, py - sy * L * 0.65f), th, L * 0.65f, red);
                    }
            }
            if (!shadowBatch_.empty()) {
                SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
                SDL_RenderGeometry(ren_, nullptr, shadowBatch_.data(),
                                   int(shadowBatch_.size()), nullptr, 0);
            }
        }

        // Health bars for damaged or selected units -- viewport-culled and batched
        // into one draw call (each was two state-changing FillRects, so a damaged
        // crowd used to break the render batch thousands of times a frame).
        shadowBatch_.clear();
        for (const auto& u : world_.units()) {
            if (!u.alive() || u.embarked() || !u.type) continue;
            if (u.underConstruction && !u.buildBegun) continue;   // ghost: no bar
            if (!alliedToLocal(u.player) && !world_.cellVisible(u.x, u.z)) continue;
            float frac = std::clamp(u.hp / u.type->maxHp, 0.0f, 1.0f);
            // Only damaged units show a health bar -- a unit at full HP never does,
            // selected or not.
            if (frac >= 1.0f) continue;
            float bw = 26 * zm, bh = std::max(2.0f, 3 * zm);
            float bx = (u.x - mapView_.offX()) * zm - bw / 2 - uLiftX(u) * zm;
            float by = (u.z - mapView_.offY()) * zm - 30 * zm - uLiftY(u) * zm;
            if (bx < -40 || bx > mvw + 40 || by < -40 || by > winH + 40) continue;
            pushQuad(shadowBatch_, bx - 1, by - 1, bw + 2, bh + 2,
                     SDL_Color{10, 10, 10, 220});
            pushQuad(shadowBatch_, bx, by, bw * frac, bh,
                     SDL_Color{uint8_t(230 * (1 - frac) + 40 * frac),
                               uint8_t(200 * frac + 40 * (1 - frac)), 40, 255});
        }
        if (!shadowBatch_.empty()) {
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_RenderGeometry(ren_, nullptr, shadowBatch_.data(),
                               int(shadowBatch_.size()), nullptr, 0);
        }

        // Production progress above busy buildings.
        for (const auto& u : world_.units()) {
            if (!u.alive() || u.buildQueue.empty() || !u.type) continue;
            if (!alliedToLocal(u.player) && !world_.cellVisible(u.x, u.z)) continue;
            float total = u.buildQueue.front()->buildTime /
                          std::max(u.type->workerTime, 0.01f);
            float frac = std::clamp(u.buildProgress / total, 0.0f, 1.0f);
            float bw = 40 * zm, bh = std::max(3.0f, 4 * zm);
            float bx = (u.x - mapView_.offX()) * zm - bw / 2 - uLiftX(u) * zm;
            float by = (u.z - mapView_.offY()) * zm - float(u.type->footZ) * 8 * zm - 14 * zm
                       - uLiftY(u) * zm;
            SDL_FRect bg{bx - 1, by - 1, bw + 2, bh + 2};
            SDL_SetRenderDrawColor(ren_, 10, 10, 10, 220);
            SDL_RenderFillRectF(ren_, &bg);
            SDL_FRect fg{bx, by, bw * frac, bh};
            SDL_SetRenderDrawColor(ren_, 90, 170, 255, 255);
            SDL_RenderFillRectF(ren_, &fg);
        }

        // Player mana bar top left (legacy; only without the bottom bar).
        if (!panelTex_) {
            auto& tm = world_.player(localPlayer_);
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

        // Restore the un-shaken camera so the HUD/panel stays rock-steady.
        if (shaking) mapView_.setOffset(shakeBaseX, shakeBaseY);

        // Done with world-space: drop the clip and draw the right-hand panel and
        // its minimap + order column on a solid strip (never over the map).
        SDL_RenderSetClipRect(ren_, nullptr);
        SDL_SetRenderDrawColor(ren_, 16, 14, 12, 255);
        SDL_FRect panelStrip{float(mvw), 0, float(winW - mvw), float(winH) - kBarH};
        SDL_RenderFillRectF(ren_, &panelStrip);
        drawMinimap(winW, winH);
        drawOrderColumn(winW, winH);

        drawPanel(winW, winH);
        if (showCounts_) drawUnitCounts(winW);
        if (showHDebug_) drawHDebug();
        if (showColorPicker_) drawColorPicker(winW, winH);

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
        if (mp_ && hudFont_.ok()) {
            char nb[64];
            std::snprintf(nb, sizeof nb, "NET P%d  TICK %u", localPlayer_ + 1, netTick_);
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

        // Victory / defeat banner. A spectator/replay viewer isn't a participant,
        // so it names the winner instead of "VICTORY"/"DEFEAT".
        if (outcome_ != 0 && bigFont_.ok()) {
            std::string msg;
            SDL_Color col{255, 220, 90, 255};
            if (spectating_ || replayMode_) {
                int wt = world_.winningTeam();
                msg = wt >= 0 ? "TEAM " + std::to_string(wt + 1) + " WINS" : "GAME OVER";
            } else {
                msg = outcome_ > 0 ? "VICTORY" : "DEFEAT";
                if (outcome_ < 0) col = {255, 90, 70, 255};
            }
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

        // Spectator badge: a live watcher can pan/zoom but issues no orders. Use the
        // crisp 5x7 block font (the scaled GAF hudFont smeared/overlapped here).
        if (spectating_) {
            const char* m = "SPECTATING";
            float px = 3.0f;
            float tw = blockWidth(m, px), th = 7 * px;
            float bx = (winW - tw) / 2, by = 24;
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_FRect bg{bx - 14, by - 8, tw + 28, th + 16};
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 150);
            SDL_RenderFillRectF(ren_, &bg);
            blockText(m, bx, by, px, {235, 175, 110, 255});
        }

        // Replay scrubber: elapsed/total time and a progress bar above the HUD.
        if (replayMode_ && hudFont_.ok()) {
            int cur = int(replayTick_) / 30, tot = int(replayLength()) / 30;
            char sb[64];
            std::snprintf(sb, sizeof sb, "REPLAY  %d:%02d / %d:%02d%s",
                          cur / 60, cur % 60, tot / 60, tot % 60,
                          paused_ ? "  PAUSED" : "");
            float bw = 360, bx = (winW - bw) / 2, by = float(winH) - 118;
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_FRect bg{bx - 8, by - 22, bw + 16, 44};
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 150);
            SDL_RenderFillRectF(ren_, &bg);
            SDL_FRect track{bx, by + 6, bw, 6};
            SDL_SetRenderDrawColor(ren_, 90, 95, 110, 220);
            SDL_RenderFillRectF(ren_, &track);
            float frac = replayLength() ? float(replayTick_) / float(replayLength()) : 0;
            SDL_FRect fill{bx, by + 6, bw * frac, 6};
            SDL_SetRenderDrawColor(ren_, 235, 205, 110, 255);
            SDL_RenderFillRectF(ren_, &fill);
            hudFont_.draw(ren_, sb, bx, by - 14, 1.6f, {235, 230, 210, 255});
        }

        // In-game chat: recent lines bottom-left, plus a composer while typing.
        if (mp_ && hudFont_.ok() && (chatTyping_ || !gameChat_.empty())) {
            const float kFade = 10.0f;   // seconds a line stays up when not typing
            // Gather what will be drawn (bottom-up) so we can back it with one
            // translucent strip -- readable over bright terrain.
            std::vector<std::pair<std::string, SDL_Color>> lines;
            if (chatTyping_)
                lines.push_back({"SAY> " + chatDraft_ + "_", {255, 245, 180, 255}});
            int shown = 0;
            for (auto it = gameChat_.rbegin(); it != gameChat_.rend() && shown < 6; ++it) {
                if (!chatTyping_ && it->age > kFade) continue;
                lines.push_back({it->who + ": " + it->text, {225, 228, 236, 255}});
                ++shown;
            }
            float x = 14, y = float(winH) - kBarH - 14;
            float wMax = 0;
            for (auto& l : lines) wMax = std::max(wMax, float(hudFont_.width(l.first, 1.7f)));
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_FRect bg{x - 8, y - float(lines.size()) * 21 + 4,
                         std::min(wMax + 16, float(winW) - x), float(lines.size()) * 21 + 6};
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 140);
            SDL_RenderFillRectF(ren_, &bg);
            for (auto& l : lines) {           // lines[0] is the composer / newest
                float sc = (&l == &lines.front() && chatTyping_) ? 1.8f : 1.6f;
                hudFont_.draw(ren_, l.first, x, y, sc, l.second);
                y -= 21;
            }
        }
    }

    void advance(float seconds) {
        float printed = 0;
        for (float t = 0; t < seconds; t += 1.0f / 30.0f) {
            update(1.0f / 30.0f);
            if (trace_ && t >= printed) {
                printed += 0.5f;
                for (auto& u : world_.units())
                    if (u.player == 0 && u.alive())
                        std::printf("TRACE %.1f %d %.1f %.1f\n", t, u.id, u.x, u.z);
            }
        }
    }

    void setTrace(bool on) { trace_ = on; sounds_.setVerbose(on); }

    // Smallest window that still fits the widest build-icon row at full size
    // (icons are 60px on a 66px pitch, centred over the map viewport with a small
    // margin, beside the fixed right-hand panel). Enforced in main() so the icons
    // never have to shrink and the last builder tier is never clipped off.
    int minWindowWidth() const {
        int n = int(registry_.maxBuildMenu());
        int rowW = n > 0 ? (n - 1) * 66 + 60 : 0;
        return rowW + 24 + kPanelW;
    }

private:
    // The skirmish AI now lives in src/ai (tak::ai::Controller), so it can also
    // run headless on the multiplayer server. Here it is driven in-process: one
    // Controller per AI player, evaluated every SIM TICK (not render frame) so its
    // ~1 Hz decision cadence is deterministic, emitting net::Commands through a
    // sink that feeds the normal apply() path -- exactly as a human's orders do.
    void ensureAi() {
        if (aiReady_) return;
        aiReady_ = true;
        aiProfile_ = tak::ai::loadProfile(dataRoot_, ipRoot_);
        // Which players are AI: the FFA harness makes 0..N-1 AI; otherwise the
        // opponent (player 1), plus player 0 in the showcase demo.
        std::vector<int> aiPlayers;
        if (ffaPlayers_ > 0)
            for (int p = 0; p < ffaPlayers_; ++p) aiPlayers.push_back(p);
        else {
            aiPlayers.push_back(1);
            if (demoAi_) aiPlayers.push_back(0);
        }
        // Reserve so populating never reallocates (Controller holds reference
        // members; moving is safe, but reserving avoids the churn entirely) and
        // the vector stays fixed for the rest of the game.
        aiControllers_.reserve(aiPlayers.size());
        for (int p : aiPlayers)
            if (p < world_.numPlayers())
                aiControllers_.emplace_back(p, registry_, aiProfile_, kAiSeed);
    }

    // Run every AI controller for the current sim tick. Called once per world
    // tick from the sub-step loop (so the per-30-tick cadence is exact), never in
    // a networked game (there the server owns the AI; aiEnabled_ is false).
    void runAi() {
        if (!aiEnabled_ || outcome_ != 0) return;
        ensureAi();   // builds a Controller per AI player once (none if there are none)
        auto sink = [this](const tak::net::Command& c) { apply(c); };
        uint32_t simTick = world_.tickCount();
        for (auto& c : aiControllers_) c.tick(world_, simTick, sink);
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
        int flyGate = 8;         // static index that this unit's `fly` gates on
    };

    // The `fly` script's first instruction is a PUSH_STATIC that gates the
    // whole animation; different flyers use different indices (zonhunt=8,
    // zongod/zonharp=7). Read it straight from the bytecode.
    static int flyGateOf(const tak::cob::Vm& vm) {
        const auto& f = vm.file();
        int si = f.scriptIndex("fly");
        if (si < 0) return 8;
        uint32_t e = f.scripts[size_t(si)].entry;
        if (e + 1 < f.code.size() && f.code[e] == 0x10021004)   // PUSH_STATIC
            return int(f.code[e + 1]);
        return 8;
    }

    // At max veterancy, a unit with a `veteranmodel` swaps its mesh for the
    // fancier promoted 3DO (same piece structure, so the COB/anim carries over).
    void maybeSwapVeteranModel(const tak::sim::Unit& u) {
        if (!u.type || u.veteran < 10 || u.type->veteranModel.empty()) return;
        const std::string& vm = u.type->veteranModel;
        auto it = unitType_.find(u.id);
        if (it == unitType_.end() || it->second == vm) return;   // not drawn yet / done
        if (!visuals_.count(vm)) {
            try {
                visuals_[vm] = {tak::tdo::load(dataRoot_ + "/objects3d/" + vm + ".3do")};
            } catch (const std::exception&) {
                try { visuals_[vm] = {tak::tdo::load(ipRoot_ + "/objects3d/" + vm + ".3do")}; }
                catch (const std::exception&) { return; }   // no promoted mesh: keep base
            }
        }
        it->second = vm;   // draw the promoted mesh from now on
    }

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
            if (u.type && u.type->canFly) {
                a.flying = true;   // starts grounded; the update loop flies her
                a.flyGate = flyGateOf(*a.vm);
                // Start in the folded landed pose, not the wings-spread rest
                // pose, so a flyer that spawns idle and never takes off (e.g. the
                // Monarch at game start) doesn't sit in a T-pose.
                a.vm->start("land");
            } else if (u.type && !u.type->canMove) {
                // Buildings: run the COB constructor so ambient loops start
                // (e.g. the Sacred Fire's Create kicks off its FireControl
                // flicker). Harmless for buildings without one.
                a.vm->start("Create");
            }
        } catch (const std::exception&) { /* unit stays unanimated */ }
        if (a.vm) anims_[u.id] = std::move(a);
        unitType_[u.id] = typeId;
    }

    // Manifest player `t`'s faction god at its army's centre (once favour fills).
    void summonGod(int t) {
        float cx = 0, cz = 0; int n = 0; std::string side;
        for (const auto& u : world_.units())
            if (u.alive() && u.player == t && u.type && !u.underConstruction) {
                cx += u.x; cz += u.z; ++n;
                if (side.empty() && !u.type->side.empty()) side = u.type->side;
            }
        world_.player(t).godSummoned = true;   // mark handled regardless
        if (!n || side.empty()) return;
        std::transform(side.begin(), side.end(), side.begin(), ::tolower);
        const auto* god = registry_.find(side + "god");
        if (!god) return;
        int id = spawn(side + "god", cx / n, cz / n, 3.14159f, t);
        (void)id;
        if (t == localPlayer_ && hudFont_.ok()) { notice_ = "YOUR GOD HAS ANSWERED"; noticeTimer_ = 6; }
        else if (hudFont_.ok()) { notice_ = "AN ENEMY GOD RISES"; noticeTimer_ = 6; }
    }

    int spawn(const std::string& typeId, float x, float z, float heading, int player) {
        const auto* type = registry_.find(typeId);
        if (!type) return -1;
        int id = world_.spawn(type, x, z, heading, player);
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
                    // 10-frame sequences are per-player colours.
                    size_t n = seq.frames.size() == 10 ? 10 : 1;
                    // Sample the actual 10 player-colour RGBs once, from a logo/
                    // insignia texture (mostly pure player colour), so the HUD and
                    // minimap can match whatever colour a player renders in.
                    if (!sampledColors_ && n == 10 &&
                        name.find("logo") != std::string::npos) {
                        for (size_t i = 0; i < 10; ++i) {
                            const auto& f = seq.frames[i];
                            // Saturation-weighted average: the pure player-colour
                            // pixels dominate, grey shading/outlines contribute little.
                            double r = 0, g = 0, b = 0, wsum = 0;
                            for (size_t k = 0; k + 3 < f.rgba.size(); k += 4) {
                                if (f.rgba[k + 3] < 128) continue;
                                int R = f.rgba[k], G = f.rgba[k + 1], B = f.rgba[k + 2];
                                int mx = std::max({R, G, B}), mn = std::min({R, G, B});
                                if (mx < 45) continue;                 // skip outlines
                                double w = double(mx - mn) + 4.0;      // ~saturation
                                w *= w;                                // emphasise colour
                                r += R * w; g += G * w; b += B * w; wsum += w;
                            }
                            if (wsum > 0) {
                                // Brighten a touch so a swatch reads clearly.
                                auto up = [](double v) {
                                    return Uint8(std::min(255.0, v * 1.25));
                                };
                                playerColors_[i] = {up(r / wsum), up(g / wsum),
                                                    up(b / wsum), 255};
                            }
                        }
                        sampledColors_ = true;
                    }
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
        SDL_Texture* atlas = atlasFor(colorSlot_[localPlayer_ & 7]);
        collect(tris_, atlas, model->root, Xform{}, nullptr, 0.0f, localPlayer_);
        std::stable_sort(tris_.begin(), tris_.end(),
                  [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
        float zm = mapView_.zoom();
        // A building ghost sits on its footprint like the finished building -- never
        // lift it onto the terrain relief (see uLiftY: a deposit's raised heightmap
        // would float the ghost off its spot).
        bool lift = !isStructure(type);
        float ax = (x - mapView_.offX()) * zm - (lift ? terrainLiftX(x, z) : 0.0f) * zm;
        float ay = (z - mapView_.offY()) * zm - (lift ? terrainLift(x, z) : 0.0f) * zm;
        // Batch by texture (flush on change), like a live unit.
        triBatch_.clear();
        SDL_Texture* cur = nullptr;
        auto flush = [&] {
            if (!triBatch_.empty())
                SDL_RenderGeometry(ren_, cur, triBatch_.data(),
                                   int(triBatch_.size()), nullptr, 0);
            triBatch_.clear();
        };
        for (auto& t : tris_) {
            if (t.tex != cur) { flush(); cur = t.tex; }
            for (int i = 0; i < 3; ++i) {
                SDL_Vertex v = t.v[i];
                v.position.x = v.position.x * zm + ax;
                v.position.y = v.position.y * zm + ay;
                v.color.a = 130;
                v.color.r = Uint8(v.color.r * 0.55f);   // shift toward blue
                v.color.g = Uint8(v.color.g * 0.8f);
                triBatch_.push_back(v);
            }
        }
        flush();
    }

    // Per-unit screen-space geometry, built in parallel each frame (the expensive
    // model projection) so the single render thread only submits draw calls.
    struct UnitGeom {
        std::vector<SDL_Vertex> verts;                  // transformed, coloured
        std::vector<std::pair<SDL_Texture*, int>> runs; // (texture, vertex count)
        float ax = 0, ay = 0, occY = 0, alt = 0;
        bool canFly = false;
    };
    std::vector<const tak::sim::Unit*> visUnits_;
    std::vector<SDL_Vertex> unitBatch_, shadowBatch_;   // cross-unit render batches
    // Body pass assembled in parallel: plan offsets serially, scatter the vertex
    // copies across the pool, then replay the draw ops. Keeps depth order exact.
    std::vector<SDL_Vertex> bodyVerts_;
    struct FeatureInst;   // defined below; DrawOp only needs the pointer type
    struct CopyTask { int geom, src, count, dst; };
    struct DrawOp { const tak::sim::Unit* u; const FeatureInst* f;
                    SDL_Texture* tex; int start, count; };   // seg if u&&f both null
    std::vector<CopyTask> copyTasks_;
    std::vector<DrawOp> drawOps_;
    double profProjMs_ = 0, profSubmitMs_ = 0, profSimMs_ = 0;   // TAK_PROF sub-phase timers
    long lodDrawn_ = 0, fullDrawn_ = 0;                 // impostor vs full-model counts

    // Texture atlas: every unit texture packed into one big texture per player-
    // colour slot, so a whole model (and a whole crowd of one player) shares a
    // single texture and collapses to a handful of draw calls. Depth-sorted
    // multi-texture models otherwise force ~one draw call per triangle.
    std::unordered_map<std::string, SDL_Rect> atlasRect_;  // name -> content rect
    int atlasW_ = 0, atlasH_ = 0;
    std::vector<SDL_Texture*> atlasTex_;   // per colour slot; nullptr until built
    bool atlasLaidOut_ = false;

    // Level of detail: a unit smaller than kLodPx on screen is drawn as a single
    // billboard quad sampling a pre-rendered impostor sprite (8 facings, cached
    // per model+colour, packed into impAtlas_) instead of its full ~200-triangle
    // model. That cuts the per-frame vertex count ~100x for a zoomed-out crowd --
    // the thing that pins the render thread and the GPU at thousands of units.
    static constexpr int kFacings = 16;  // facings for both impostors and sprites
    struct Impostor {
        SDL_Rect rect[kFacings];   // where each facing sits in impAtlas_
        SDL_FRect bbox[kFacings];  // model's screen bbox at zoom 1 (offset from anchor)
        bool ready = false;
    };
    std::map<std::pair<std::string, int>, Impostor> impostors_;  // (model, slot)
    std::map<std::string, float> modelH_;    // model projected height (px @ zoom 1)
    SDL_Texture* impAtlas_ = nullptr;
    int impAtlasDim_ = 4096, impCurX_ = 0, impCurY_ = 0, impShelfH_ = 0;
    // After a failed GPU texture allocation (VRAM pressure), pause every bake /
    // atlas creation path for a few seconds instead of retrying next frame. The
    // bake paths run per visible unit per frame, so an un-cached failure becomes
    // a driver-flooding allocation storm: observed on an 8GB card driving a
    // 7680x2160 desktop as ~6k/sec nvidia-drm NVKMS GEM allocation errors that
    // starved the compositor itself ("Failed to start frame" = whole-screen
    // flicker). Units render as full 3D models during the pause -- correct, just
    // less cheap -- and baking resumes automatically once the pause lapses.
    uint32_t gpuAllocBackoffUntil_ = 0;
    bool gpuAllocBlocked() const {
        return gpuAllocBackoffUntil_ != 0 &&
               SDL_GetTicks() < gpuAllocBackoffUntil_;
    }
    void noteGpuAllocFail() {
        if (!gpuAllocBlocked())
            std::fprintf(stderr, "gpu: texture allocation failed (VRAM pressure?) "
                                 "-- pausing atlas/sprite baking for 3s\n");
        gpuAllocBackoffUntil_ = SDL_GetTicks() + 3000;
    }
    static constexpr float kImpScale = 2.0f;    // impostor render supersampling

    // Sprite sheets: the locomotion animation (walk / fly) baked to a grid of
    // frames x 8 facings per model+colour, so a unit draws as one animated quad
    // instead of a live model -- the classic-RTS way to run thousands cheaply.
    // Full-3D is kept for attack/death/build poses (rare, few at a time).
    static constexpr int kSprFrames = 8;    // locomotion-cycle frames baked
    static constexpr int kSprFacings = kFacings;  // 16 => ~22.5deg turn granularity
    struct SpriteSet {
        SDL_Rect rect[kSprFacings][kSprFrames];
        SDL_FRect bbox[kSprFacings][kSprFrames];
        SDL_Texture* page = nullptr;   // which sprite-atlas page holds this set
        int frames = 1;      // 1 for static (buildings), kSprFrames for movers
        float period = 0.9f; // real locomotion cycle length (s) the frames span
        bool ready = false;
    };
    std::map<std::pair<std::string, int>, SpriteSet> sprites_;
    std::vector<SDL_Texture*> sprPages_;   // 4096 atlas pages (multi-page: scales,
    int sprAtlasDim_ = 4096;               // and 4096 targets work everywhere)
    int sprCurX_ = 0, sprCurY_ = 0, sprShelfH_ = 0;
    // Sprite mode: AUTO (default) turns sprite sheets on only while the frame can't
    // hold 60fps, off again once the crowd clears -- so units keep full 3D detail
    // until the scene actually needs the cheaper representation. F10 cycles
    // AUTO -> ON -> OFF. spritesEnabled_ is the effective state auto-tune sets.
    enum SpriteMode { SPR_AUTO, SPR_ON, SPR_OFF };
    int spriteMode_ = SPR_AUTO;
    bool spritesEnabled_ = false;  // effective state (managed by autoTuneSprites)
    float frameEma_ = 12.0f;       // smoothed real frame time ms (drives auto sprites)
public:
    // Called once per frame with the whole frame's wall time (ms) -- the real cost
    // INCLUDING the GPU present, since a big full-model crowd is GPU-bound and that
    // cost never shows in CPU submit time (measuring update+draw alone missed it and
    // the auto-switch never fired). Under the fps cap a kept-up frame reads ~16.6ms,
    // so the on-threshold sits just above it; below-cap frames mean we're losing 60.
    void autoTuneSprites(float frameMs) {
        frameEma_ = frameEma_ * 0.85f + frameMs * 0.15f;
        if (spriteMode_ == SPR_ON)  { spritesEnabled_ = true;  return; }
        if (spriteMode_ == SPR_OFF) {
            spritesEnabled_ = false;
            if (!sprPages_.empty()) freeSpritePages();
            return;
        }
        // AUTO. Sprites help only a real crowd, so gate both on frames slower than
        // ~55fps (18ms) AND enough on-screen units -- that keeps a startup/asset
        // hitch with few units from latching them on. A kept-up frame reads ~16.6ms
        // (cap) or faster, so it can't reveal how much headroom a light scene has;
        // turn sprites back off by crowd size (wide 64-on / 32-off gap = no flapping).
        if (!spritesEnabled_) {
            if (frameEma_ > 18.0f && visUnits_.size() >= 64) spritesEnabled_ = true;
        } else if (visUnits_.size() < 32) {
            spritesEnabled_ = false;
            freeSpritePages();   // return the 64MB/page VRAM while sprites are off
        }
    }
private:

    // Allocate a fresh cleared sprite-atlas page. Returns false if it can't.
    bool newSprPage() {
        if (gpuAllocBlocked()) return false;   // don't retry a failed 64MB alloc per frame
        SDL_Texture* t = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_TARGET, sprAtlasDim_, sprAtlasDim_);
        if (!t) { noteGpuAllocFail(); return false; }
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
        SDL_Texture* p0 = SDL_GetRenderTarget(ren_);
        SDL_SetRenderTarget(ren_, t);
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 0);
        SDL_RenderClear(ren_);
        SDL_SetRenderTarget(ren_, p0);
        sprPages_.push_back(t);
        sprCurX_ = sprCurY_ = sprShelfH_ = 0;
        return true;
    }
public:
    // SDL_RENDER_TARGETS_RESET / _DEVICE_RESET: on a driver or device reset, every
    // TEXTUREACCESS_TARGET texture silently loses its pixels while its handle stays
    // valid -- so units baked/skinned into them render fully transparent (they
    // "disappear"), the classic intermittent-vanish some GPUs show under load or on
    // a focus/resolution change. SDL fires this event exactly then; we drop every
    // render-target-backed cache so the pre-pass re-bakes them cleanly next frame.
    // Surface-backed caches (shadows, build FX) keep their pixels and are untouched.
    void invalidateRenderTargets() {
        freeSpritePages();
        for (SDL_Texture* t : atlasTex_) if (t) SDL_DestroyTexture(t);
        atlasTex_.clear();
        impostors_.clear();
        if (impAtlas_) { SDL_DestroyTexture(impAtlas_); impAtlas_ = nullptr; }
        impCurX_ = impCurY_ = impShelfH_ = 0;
    }
private:
    // Release the sprite-sheet pages (they're 64MB of VRAM each). Called when
    // sprite mode turns off -- on a card shared with a huge desktop the memory
    // matters more than the rebake cost, which is budgeted anyway -- and on a
    // render-target reset, where the pixels are gone regardless.
    // NOTE: the auto-tune call site runs between draw() (which queues batched
    // SDL_RenderGeometry commands referencing these pages) and RenderPresent.
    // That is safe because SDL_DestroyTexture flushes pending render commands
    // that reference the texture (SDL >= 2.0.10) -- if draws ever bypass SDL's
    // command queue, move the auto-tune free to before update() instead.
    void freeSpritePages() {
        for (SDL_Texture* t : sprPages_) if (t) SDL_DestroyTexture(t);
        sprPages_.clear();
        sprites_.clear();
        sprCurX_ = sprCurY_ = sprShelfH_ = 0;
    }
public:
private:
    bool lodEnabled_ = true;    // distant impostors on by default; F8 toggles
    float lodPx_ = 64.0f;                        // model shorter than this -> impostor
    static constexpr float kLodZoomGate = 0.5f;  // LOD only when really zoomed out
                                                 // (zoom below this); full 3D otherwise

    static int facingIndex(float heading, int n) {
        int k = int(std::lround(heading / (2.0f * 3.14159265f) * float(n)));
        k %= n; if (k < 0) k += n;
        return k;
    }

    // Append two triangles for an axis-aligned quad (shared by shadow blobs and
    // shadow sprites; uv is ignored when the batch is drawn untextured).
    static void pushQuad(std::vector<SDL_Vertex>& b, float x, float y, float w,
                         float h, SDL_Color c) {
        SDL_Vertex tl{{x, y}, c, {0, 0}}, tr{{x + w, y}, c, {1, 0}},
                   br{{x + w, y + h}, c, {1, 1}}, bl{{x, y + h}, c, {0, 1}};
        b.push_back(tl); b.push_back(tr); b.push_back(br);
        b.push_back(tl); b.push_back(br); b.push_back(bl);
    }
    // Textured quad with explicit UV corners (for impostor billboards).
    static void pushQuadUV(std::vector<SDL_Vertex>& b, float x, float y, float w,
                           float h, float u0, float v0, float u1, float v1,
                           SDL_Color c) {
        SDL_Vertex tl{{x, y}, c, {u0, v0}}, tr{{x + w, y}, c, {u1, v0}},
                   br{{x + w, y + h}, c, {u1, v1}}, bl{{x, y + h}, c, {u0, v1}};
        b.push_back(tl); b.push_back(tr); b.push_back(br);
        b.push_back(tl); b.push_back(br); b.push_back(bl);
    }

    // Shelf-pack every loaded unit texture into a single atlas layout (rects are
    // shared across colour slots -- only the pixels differ). Called once, lazily.
    void buildAtlasLayout() {
        atlasLaidOut_ = true;
        struct Item { const std::string* name; int w, h; };
        std::vector<Item> items;
        items.reserve(textures_.size());
        for (auto& [name, frames] : textures_) {
            if (frames.empty()) continue;
            int w = 0, h = 0;
            SDL_QueryTexture(frames[0], nullptr, nullptr, &w, &h);
            if (w <= 0 || h <= 0 || w > 512 || h > 512) continue;   // skip oddities
            items.push_back({&name, w, h});
        }
        // Tallest first packs tightest.
        std::sort(items.begin(), items.end(),
                  [](const Item& a, const Item& b) { return a.h > b.h; });
        const int pad = 2, W = 2048;
        int x = pad, y = pad, shelfH = 0;
        for (const auto& it : items) {
            if (x + it.w + pad > W) { x = pad; y += shelfH + pad; shelfH = 0; }
            atlasRect_[*it.name] = SDL_Rect{x, y, it.w, it.h};
            x += it.w + pad;
            shelfH = std::max(shelfH, it.h);
        }
        atlasW_ = W;
        atlasH_ = y + shelfH + pad;
    }

    // Build (or return cached) the atlas texture for one colour slot by blitting
    // each texture's slot variant into its packed rect. Main thread only (render
    // target), so it must run before the parallel geometry pass.
    SDL_Texture* atlasFor(int slot) {
        if (slot < 0) slot = 0;
        if (!atlasLaidOut_) buildAtlasLayout();
        if (atlasW_ <= 0) return nullptr;
        if (int(atlasTex_.size()) <= slot) atlasTex_.resize(size_t(slot) + 1, nullptr);
        if (atlasTex_[slot]) return atlasTex_[slot];
        if (gpuAllocBlocked()) return nullptr;   // don't retry a failed alloc per frame
        SDL_Texture* atlas = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                               SDL_TEXTUREACCESS_TARGET, atlasW_, atlasH_);
        if (!atlas) { noteGpuAllocFail(); return nullptr; }
        SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);
        SDL_Texture* prev = SDL_GetRenderTarget(ren_);
        SDL_SetRenderTarget(ren_, atlas);
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 0);
        SDL_RenderClear(ren_);
        for (const auto& [name, r] : atlasRect_) {
            auto it = textures_.find(name);
            if (it == textures_.end() || it->second.empty()) continue;
            size_t ci = size_t(slot) < it->second.size() ? size_t(slot) : 0;
            SDL_Rect dst = r;
            SDL_RenderCopy(ren_, it->second[ci], nullptr, &dst);
        }
        SDL_SetRenderTarget(ren_, prev);
        SDL_SetTextureScaleMode(atlas, SDL_ScaleModeNearest);   // no atlas edge bleed
        atlasTex_[slot] = atlas;
        return atlas;
    }

    // Render a model's 8 facings into the impostor atlas once and cache the rects
    // + per-facing bounding box. Main thread only (render target); must run before
    // the parallel geometry pass reads it.
    void ensureImpostor(const std::string& modelKey, int slot, bool canMove) {
        auto key = std::make_pair(modelKey, slot);
        if (impostors_.count(key)) return;
        auto vt = visuals_.find(modelKey);
        if (vt == visuals_.end()) return;
        SDL_Texture* atlas = atlasFor(slot);
        if (!atlas) return;   // slot atlas unavailable (alloc backoff): bake later
        if (!impAtlas_) {
            if (gpuAllocBlocked()) return;   // don't retry a failed 64MB alloc per frame
            impAtlas_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                          SDL_TEXTUREACCESS_TARGET, impAtlasDim_, impAtlasDim_);
            if (!impAtlas_) { noteGpuAllocFail(); return; }
            SDL_SetTextureBlendMode(impAtlas_, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(impAtlas_, SDL_ScaleModeLinear);
            SDL_Texture* p0 = SDL_GetRenderTarget(ren_);
            SDL_SetRenderTarget(ren_, impAtlas_);
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 0);
            SDL_RenderClear(ren_);
            SDL_SetRenderTarget(ren_, p0);
        }
        Impostor imp{};   // zero-init: never cache uninitialized facing rects
        bool full = false;
        std::vector<Tri> scratch;
        SDL_Texture* prev = SDL_GetRenderTarget(ren_);
        SDL_SetRenderTarget(ren_, impAtlas_);
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        float maxH = 1.0f;
        for (int k = 0; k < kFacings; ++k) {
            float heading = float(k) / float(kFacings) * 2.0f * 3.14159265f;
            float facing = canMove ? -heading : 0.0f;
            scratch.clear();
            collect(scratch, atlas, vt->second.model.root, Xform{}, nullptr, facing, 0, false);
            std::stable_sort(scratch.begin(), scratch.end(),
                      [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
            float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
            for (auto& t : scratch)
                for (int i = 0; i < 3; ++i) {
                    minX = std::min(minX, t.v[i].position.x);
                    minY = std::min(minY, t.v[i].position.y);
                    maxX = std::max(maxX, t.v[i].position.x);
                    maxY = std::max(maxY, t.v[i].position.y);
                }
            if (scratch.empty()) { minX = minY = 0; maxX = maxY = 1; }
            // Render at kImpScale x native resolution so an impostor stays crisp
            // when a big crowd is viewed close and the sprite is upscaled.
            const float S = kImpScale;
            const int pad = 2;
            int w = std::clamp(int(std::ceil((maxX - minX) * S)) + 2 * pad, 2, 400);
            int h = std::clamp(int(std::ceil((maxY - minY) * S)) + 2 * pad, 2, 400);
            if (impCurX_ + w > impAtlasDim_) { impCurX_ = 0; impCurY_ += impShelfH_ + 1; impShelfH_ = 0; }
            if (impCurY_ + h > impAtlasDim_) { full = true; break; }   // atlas full
            int rx = impCurX_, ry = impCurY_;
            for (auto& t : scratch) {
                SDL_Vertex v[3];
                for (int i = 0; i < 3; ++i) {
                    v[i] = t.v[i];
                    v[i].position.x = (t.v[i].position.x - minX) * S + float(rx + pad);
                    v[i].position.y = (t.v[i].position.y - minY) * S + float(ry + pad);
                }
                SDL_RenderGeometry(ren_, t.tex, v, 3, nullptr, 0);
            }
            imp.rect[k] = SDL_Rect{rx, ry, w, h};
            // bbox drives on-screen placement, so it stays in native units: the
            // 2x cell (incl. pad) maps back to w/S x h/S native pixels.
            imp.bbox[k] = SDL_FRect{minX - pad / S, minY - pad / S, w / S, h / S};
            impCurX_ += w + 1;
            impShelfH_ = std::max(impShelfH_, h);
            maxH = std::max(maxH, maxY - minY);
        }
        SDL_SetRenderTarget(ren_, prev);
        // Ready only when every facing baked. On atlas-full, the cached not-ready
        // entry keeps the unit on its full 3D model (and stops per-frame retries)
        // instead of drawing garbage rects for the unbaked facings.
        imp.ready = !full;
        impostors_[key] = imp;
        modelH_[modelKey] = maxH;
    }

    // A standalone COB VM for a type (no live unit), for baking sprites. onGet
    // answers "healthy and moving" so locomotion scripts animate.
    std::unique_ptr<tak::cob::Vm> loadTypeVm(const std::string& typeId,
                                             std::vector<std::string>& names) {
        std::string cobPath = dataRoot_ + "/scripts/" + typeId + ".cob";
        if (!std::filesystem::exists(cobPath) && !ipRoot_.empty())
            cobPath = ipRoot_ + "/scripts/" + typeId + ".cob";
        try {
            auto cobFile = tak::cob::load(cobPath);
            names.clear();
            for (const auto& p : cobFile.pieces) {
                std::string n = p;
                std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                names.push_back(n);
            }
            auto vm = std::make_unique<tak::cob::Vm>(std::move(cobFile));
            vm->onGet = [](int32_t v, const std::vector<int32_t>&) -> int32_t {
                switch (v) { case 3: return 100; case 5: return 1; default: return 0; }
            };
            return vm;
        } catch (const std::exception&) { return nullptr; }
    }

    // Bake a model's locomotion cycle (walk / fly) into the sprite atlas: kSprFrames
    // poses x 8 facings, at 2x native. Main thread only (render target); one-time
    // per model+colour. A reserved not-ready entry is left if there's no COB so we
    // don't retry every frame (that unit just keeps using its full model).
    void bakeSprites(const std::string& typeId, int slot, bool canMove, bool canFly) {
        auto key = std::make_pair(typeId, slot);
        if (sprites_.count(key)) return;
        // During an allocation backoff, don't reserve the key yet -- so the bake
        // happens properly once VRAM pressure clears, instead of never.
        if (gpuAllocBlocked()) return;
        sprites_[key] = SpriteSet{};   // reserve (not ready)
        auto vt = visuals_.find(typeId);
        if (vt == visuals_.end()) return;
        std::vector<std::string> names;
        auto vm = loadTypeVm(typeId, names);
        if (!vm) return;
        Anim tmp;
        tmp.pieceNames = names;
        tmp.vm = std::move(vm);
        tmp.flyGate = flyGateOf(*tmp.vm);
        bool animated = canMove || canFly;
        // (Re)start the locomotion animation from the top -- used before each bake
        // attempt so a retry on a fresh page re-captures the same frames.
        auto initAnim = [&] {
            tmp.vm->reset();
            if (canFly) { tmp.vm->setStatic(tmp.flyGate, 1); tmp.vm->start("fly");
                          for (int s = 0; s < 8; ++s) tmp.vm->tick(1.0f / 30); }
            else if (canMove) {
                // Ground walk scripts gate their leg motion on static 0 (the "moving"
                // flag the live anim loop sets); without it walk_legs no-ops and every
                // baked frame is the same standing pose. Set it, exactly as the live
                // update loop does, so the bake captures a real walk cycle.
                tmp.vm->setStatic(0, 1);
                tmp.vm->start("walk") || tmp.vm->start("walk_legs");
            }
            else {
                // Static buildings: run the COB constructor exactly as registerUnit
                // does for the live unit, then let it settle, so the bake reflects the
                // same default piece visibility -- e.g. the Death Totem's Create hides
                // its vetskull* pieces (veterancy skulls a fresh totem hasn't earned),
                // which the sprite otherwise left visible while the 3D model hid them.
                tmp.vm->start("Create");
                for (int s = 0; s < 6; ++s) tmp.vm->tick(1.0f / 30);
            }
        };
        // Advance the bake VM one step. A ground walk script is single-pass, so (like
        // the live loop) re-invoke it once its thread ends, keeping the legs cycling
        // across the whole bake window instead of freezing after the first pass.
        auto stepVm = [&](float dt) {
            tmp.vm->tick(dt);
            if (canMove && !canFly && tmp.vm->threadCount() == 0)
                tmp.vm->start("walk") || tmp.vm->start("walk_legs");
        };
        SDL_Texture* atlas = atlasFor(slot);
        // A page-allocation failure is transient (VRAM pressure) -- un-reserve the
        // key so this type rebakes after the backoff, unlike the permanent
        // no-COB/no-model reservations above.
        if (sprPages_.empty() && !newSprPage()) { sprites_.erase(key); return; }
        SpriteSet ss;
        ss.frames = animated ? kSprFrames : 1;
        std::vector<Tri> scratch;
        SDL_Texture* prev = SDL_GetRenderTarget(ren_);
        const float S = kImpScale, kPi = 3.14159265f;
        // Estimate the real locomotion cycle length so the baked frames span one
        // actual cycle (not a guessed 0.9s). TA locomotion scripts play once then
        // hold, so the cycle = how long the pose keeps changing; if it never
        // settles (a truly looping script) or settles instantly, fall back to 0.9s.
        auto sig = [&] {
            double s = 0;
            for (const auto& pc : tmp.vm->pieces())
                s += std::sin(pc.rot[0]) + std::sin(pc.rot[1]) + std::sin(pc.rot[2])
                   + pc.move[0] + pc.move[1] + pc.move[2];
            return float(s);
        };
        float period = 0.9f;
        if (animated) {
            initAnim();
            const float dt = 1.0f / 60.0f;
            float prev = sig();
            int stable = 0;
            for (float t = dt; t < 2.5f; t += dt) {
                tmp.vm->tick(dt);   // no re-invoke: let one walk pass settle = the cycle
                float s = sig();
                if (std::fabs(s - prev) < 1e-4f) {
                    if (++stable >= 6 && t - 6 * dt > 0.25f) { period = t - 6 * dt; break; }
                } else stable = 0;
                prev = s;
            }
            period = std::clamp(period, 0.3f, 2.0f);
        }
        bool atlasFull = false, pageAllocFailed = false;
        SDL_Texture* target = sprPages_.back();
        // Capture the current VM pose at facing fi into a packed cell of `target`.
        auto capture = [&](int fi, SDL_Rect& outR, SDL_FRect& outB) {
            float heading = float(fi) / kSprFacings * 2.0f * kPi;
            // Every mover, flyers included, faces -heading: with the piece X/Y
            // negation the fly pose no longer needs a per-flyer 180 (retail has no
            // flyer facing branch, root matrix 0x4ee620 identical for all units).
            float facing = (canFly || canMove) ? -heading : 0.0f;
            scratch.clear();
            collect(scratch, atlas, vt->second.model.root, Xform{}, &tmp, facing, 0, false);
            std::stable_sort(scratch.begin(), scratch.end(),
                      [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
            float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
            for (auto& t : scratch)
                for (int i = 0; i < 3; ++i) {
                    minX = std::min(minX, t.v[i].position.x);
                    minY = std::min(minY, t.v[i].position.y);
                    maxX = std::max(maxX, t.v[i].position.x);
                    maxY = std::max(maxY, t.v[i].position.y);
                }
            if (scratch.empty()) { minX = minY = 0; maxX = maxY = 1; }
            const int pad = 2;
            int w = std::clamp(int(std::ceil((maxX - minX) * S)) + 2 * pad, 2, 400);
            int h = std::clamp(int(std::ceil((maxY - minY) * S)) + 2 * pad, 2, 400);
            if (sprCurX_ + w > sprAtlasDim_) { sprCurX_ = 0; sprCurY_ += sprShelfH_ + 1; sprShelfH_ = 0; }
            if (sprCurY_ + h > sprAtlasDim_) { atlasFull = true; return; }
            int rx = sprCurX_, ry = sprCurY_;
            for (auto& t : scratch) {
                SDL_Vertex v[3];
                for (int i = 0; i < 3; ++i) {
                    v[i] = t.v[i];
                    v[i].position.x = (t.v[i].position.x - minX) * S + float(rx + pad);
                    v[i].position.y = (t.v[i].position.y - minY) * S + float(ry + pad);
                }
                SDL_RenderGeometry(ren_, t.tex, v, 3, nullptr, 0);
            }
            outR = SDL_Rect{rx, ry, w, h};
            outB = SDL_FRect{minX - pad / S, minY - pad / S, w / S, h / S};
            sprCurX_ += w + 1;
            sprShelfH_ = std::max(sprShelfH_, h);
        };
        // Bake all frames into the current page; if it overflows, start a fresh
        // page and re-bake from the top (at most one retry -- a type that can't fit
        // an empty page is left not-ready and just uses its full model).
        for (int attempt = 0; attempt < 2; ++attempt) {
            initAnim();
            target = sprPages_.back();
            SDL_SetRenderTarget(ren_, target);
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            atlasFull = false;
            for (int k = 0; k < ss.frames && !atlasFull; ++k) {
                if (k > 0) for (int s = 0; s < 4; ++s) stepVm(period / ss.frames / 4);
                for (int fi = 0; fi < kSprFacings && !atlasFull; ++fi)
                    capture(fi, ss.rect[fi][k], ss.bbox[fi][k]);
            }
            if (!atlasFull) { ss.page = target; break; }
            if (attempt == 0 && !newSprPage()) { pageAllocFailed = true; break; }
        }
        SDL_SetRenderTarget(ren_, prev);
        // Transient VRAM failure: un-reserve so the type rebakes after the backoff.
        if (pageAllocFailed) { sprites_.erase(key); return; }
        if (atlasFull || !ss.page) return;   // doesn't fit a fresh page -> full model
        ss.period = period;
        ss.ready = true;
        sprites_[key] = ss;
    }

    // Project + transform one unit's model into screen-space, coloured vertex runs.
    // No SDL calls and only reads shared state (models/textures/heightmap/anim), so
    // it is safe to run for many units at once on the worker pool. drawUnit() then
    // just submits g.runs. `scratch` is a reusable per-thread triangle buffer.
    void buildUnitGeom(const tak::sim::Unit& u, UnitGeom& g, std::vector<Tri>& scratch) {
        g.verts.clear();
        g.runs.clear();
        g.canFly = u.type && u.type->canFly;
        if (u.underConstruction && !u.buildBegun) return;   // ghost drawn serially
        auto ut = unitType_.find(u.id);   // defensive: a throw here would abort
        if (ut == unitType_.end()) return;
        auto vt = visuals_.find(ut->second);
        if (vt == visuals_.end()) return;
        const Anim* anim = nullptr;
        auto at = anims_.find(u.id);
        if (at != anims_.end()) anim = &at->second;

        float zm = mapView_.zoom();
        int slot = colorSlot_[u.player & 7];
        float ax = (u.x - mapView_.offX()) * zm - uLiftX(u) * zm;
        float ay = (u.z - mapView_.offY()) * zm - uLiftY(u) * zm;
        // Sprite sheet: draw a moving/idle unit as one animated quad from the baked
        // locomotion cycle. Attack/death poses keep the full 3D model (rare).
        if (spritesEnabled_) {
            auto sit = sprites_.find(std::make_pair(vt->first, slot));
            // A grounded/idle unit shows a static frame (no flap); it only cycles
            // the animation while airborne (flyers) or moving (ground). Attack/death
            // poses keep the full 3D model.
            bool grounded = (u.type && u.type->canFly) && !(anim && anim->airborne);
            bool special = anim && (anim->dying || anim->firing);
            if (sit != sprites_.end() && sit->second.ready && !special) {
                const SpriteSet& ss = sit->second;
                int fi = facingIndex((u.type && u.type->canMove) ? u.heading : 0.0f,
                                     kSprFacings);
                int frame = 0;
                if (ss.frames > 1 && !grounded) {
                    bool moving = (u.type && u.type->canFly) || u.moving();
                    if (moving) {
                        float rate = float(ss.frames) / std::max(0.05f, ss.period);
                        frame = (int(animClock_ * rate) + u.id) % ss.frames;
                    }
                }
                const SDL_Rect& r = ss.rect[fi][frame];
                const SDL_FRect& bb = ss.bbox[fi][frame];
                if (r.w > 2 && r.h > 2) {   // else falls to full model
                    float alt = g.canFly ? (anim ? anim->altitude : u.type->cruiseAlt) : 0.0f;
                    float qx = ax + bb.x * zm;
                    float qy = ay + bb.y * zm - alt * 0.8f * zm;
                    float inv = 1.0f / float(sprAtlasDim_);
                    pushQuadUV(g.verts, qx, qy, bb.w * zm, bb.h * zm,
                               float(r.x) * inv, float(r.y) * inv,
                               float(r.x + r.w) * inv, float(r.y + r.h) * inv,
                               SDL_Color{255, 255, 255, 255});
                    g.runs.push_back({ss.page, 6});
                    g.ax = ax; g.ay = ay; g.alt = alt;
                    g.occY = wallOcclusionY(u.x, u.z);
                    return;
                }
            }
        }
        // Level of detail: only when really zoomed out (zoom below the gate), draw a
        // unit small enough on screen (< lodPx_ tall) as a cached impostor billboard
        // instead of its model. At normal/close zoom every unit keeps full 3D.
        if (lodEnabled_ && zm < kLodZoomGate) {
            auto hit = modelH_.find(vt->first);
            auto iit = impostors_.find(std::make_pair(vt->first, slot));
            if (hit != modelH_.end() && iit != impostors_.end() && iit->second.ready
                && hit->second * zm < lodPx_) {
                const Impostor& imp = iit->second;
                int f = facingIndex((u.type && u.type->canMove) ? u.heading : 0.0f, kFacings);
                const SDL_Rect& r = imp.rect[f];
                const SDL_FRect& bb = imp.bbox[f];
                float alt = g.canFly ? (anim ? anim->altitude : u.type->cruiseAlt) : 0.0f;
                float qx = ax + bb.x * zm;
                float qy = ay + bb.y * zm - alt * 0.8f * zm;   // ~lift a flyer's sprite
                float inv = 1.0f / float(impAtlasDim_);
                pushQuadUV(g.verts, qx, qy, bb.w * zm, bb.h * zm,
                           float(r.x) * inv, float(r.y) * inv,
                           float(r.x + r.w) * inv, float(r.y + r.h) * inv,
                           SDL_Color{255, 255, 255, 255});
                g.runs.push_back({impAtlas_, 6});
                g.ax = ax; g.ay = ay; g.alt = alt;
                g.occY = wallOcclusionY(u.x, u.z);
                return;
            }
        }

        scratch.clear();
        Xform base;
        if (u.type && u.type->canFly && u.type->cruiseAlt > 0)
            base.t[1] = anim ? anim->altitude : u.type->cruiseAlt;
        // Flyers face -heading exactly like ground movers (no flyer facing branch).
        float facing = (u.type && (u.type->canMove || u.type->canFly)) ? -u.heading : 0.0f;
        bool mirror = false;
        SDL_Texture* atlas = (slot >= 0 && size_t(slot) < atlasTex_.size())
                                 ? atlasTex_[size_t(slot)] : nullptr;
        collect(scratch, atlas, vt->second.model.root, base, anim, facing, u.player, mirror);
        std::stable_sort(scratch.begin(), scratch.end(),
                  [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
        g.ax = ax; g.ay = ay;
        g.alt = anim ? anim->altitude : 0.0f;
        g.occY = wallOcclusionY(u.x, u.z);
        bool conjuring = u.underConstruction && u.type;
        float p = conjuring ? std::clamp(u.hp / u.type->maxHp, 0.0f, 1.0f) : 1.0f;
        Uint8 alpha = Uint8(p * 255.0f);
        float vetGold = (!conjuring && u.veteran >= 4)
                            ? float(std::min(u.veteran, 10) - 3) / 7.0f * 0.5f : 0.0f;
        SDL_Texture* cur = nullptr;
        int runStart = 0;
        for (auto& t : scratch) {
            if (t.tex != cur) {
                if (int(g.verts.size()) > runStart)
                    g.runs.push_back({cur, int(g.verts.size()) - runStart});
                cur = t.tex;
                runStart = int(g.verts.size());
            }
            for (int i = 0; i < 3; ++i) {
                SDL_Vertex v = t.v[i];
                v.position.x = v.position.x * zm + ax;
                v.position.y = v.position.y * zm + ay;
                if (vetGold > 0) {
                    v.color.r = Uint8(v.color.r + (255 - v.color.r) * vetGold);
                    v.color.g = Uint8(v.color.g + (200 - v.color.g) * vetGold * 0.85f);
                    v.color.b = Uint8(v.color.b * (1.0f - vetGold * 0.7f));
                }
                if (conjuring) {
                    float pulse = 0.5f + 0.5f * std::sin(animClock_ * 7.0f +
                                                         v.position.y * 0.03f);
                    float glow = (1.0f - p) * pulse;
                    v.color.a = alpha;
                    v.color.r = Uint8(v.color.r * (1.0f - 0.75f * glow));
                    v.color.g = Uint8(v.color.g * (1.0f - 0.25f * glow));
                }
                g.verts.push_back(v);
            }
        }
        if (int(g.verts.size()) > runStart)
            g.runs.push_back({cur, int(g.verts.size()) - runStart});
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

        // The model projection (collect + sort + screen transform + colour) was
        // done for every visible unit in parallel on the worker pool this frame;
        // here we just look up the result and submit its draw calls.
        auto git = geomIndex_.find(u.id);
        if (git == geomIndex_.end()) return;
        UnitGeom& g = geomPool_[size_t(git->second)];
        float zm = mapView_.zoom();
        float ax = g.ax, ay = g.ay;
        // Terrain occlusion: if a wall between the unit and the camera projects its
        // top above the unit's feet, clip the model to that line and re-draw the
        // hidden part as a faint player-tinted silhouette showing through the wall.
        // Flyers ride above the terrain, so a wall never hides them.
        float occY = g.occY;
        bool occluded = !g.canFly && occY < ay - 2.0f;
        int outW = 0, outH = 0;
        SDL_bool hadClip = SDL_FALSE;
        SDL_Rect prevClip{};
        if (occluded) {
            SDL_GetRendererOutputSize(ren_, &outW, &outH);
            hadClip = SDL_RenderIsClipEnabled(ren_);
            if (hadClip) SDL_RenderGetClipRect(ren_, &prevClip);
            int line = std::clamp(int(occY), 0, outH);
            SDL_Rect top{0, 0, outW, line};   // only pixels above the wall top show
            SDL_RenderSetClipRect(ren_, &top);
        }
        // Ground shadow (FBI shadowart, from shadows.gaf): drawn under the model
        // at the unit's ground point, nudged for the sun; a flyer's shadow sits
        // further out and stays on the ground while the model rides its altitude.
        if (u.type && !u.underConstruction) {
            if (const ShadowTex* sh = shadowFor(u.type->shadowArt)) {
                float alt = anim ? anim->altitude : 0.0f;
                float sox = (6.0f + alt * 0.5f) * zm, soy = (3.0f + alt * 0.25f) * zm;
                SDL_FRect dst{ax - sh->xoff * zm + sox, ay - sh->yoff * zm + soy,
                              sh->w * zm, sh->h * zm};
                SDL_RenderCopyF(ren_, sh->tex, nullptr, &dst);
            }
        }
        // Submit the pre-built, depth-sorted vertex runs -- one SDL_RenderGeometry
        // per texture (usually 1 per unit). The veterancy/conjure colour tint was
        // already baked into the vertices on the worker pool.
        bool conjuring = u.underConstruction && u.type;
        int off = 0;
        for (const auto& r : g.runs) {
            SDL_RenderGeometry(ren_, r.first, g.verts.data() + off, r.second, nullptr, 0);
            off += r.second;
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

        // Occluded: re-draw the hidden lower part as a faint, flat player-coloured
        // silhouette through the wall, so a unit behind cover is never fully lost.
        if (occluded) {
            int line = std::clamp(int(occY), 0, outH);
            SDL_Rect bot{0, line, outW, std::max(0, outH - line)};
            SDL_RenderSetClipRect(ren_, &bot);
            SDL_Color tc = playerColor(u.player);
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            // The silhouette is flat, untextured and single-colour, so the whole
            // model collapses to ONE draw call: re-tint every vertex and submit.
            triBatch_.clear();
            for (const SDL_Vertex& sv : g.verts) {
                SDL_Vertex v = sv;
                v.color = SDL_Color{tc.r, tc.g, tc.b, 70};
                triBatch_.push_back(v);
            }
            if (!triBatch_.empty())
                SDL_RenderGeometry(ren_, nullptr, triBatch_.data(),
                                   int(triBatch_.size()), nullptr, 0);
            if (hadClip) SDL_RenderSetClipRect(ren_, &prevClip);
            else SDL_RenderSetClipRect(ren_, nullptr);
        }
    }

    // Project model triangles relative to the unit anchor: yaw by heading,
    // fixed tilt so models read against TAK's painted top-down terrain.
    void collect(std::vector<Tri>& out, SDL_Texture* atlas, const tak::tdo::Object& o,
                 const Xform& parent, const Anim* anim, float heading, int player,
                 bool mirror = false, bool isRoot = true) {
        const tak::cob::PieceState* ps = pieceFor(anim, o.name);
        if (ps && !ps->visible) return;
        float rr[3];
        Xform xf = parent.then(o.x + (ps ? ps->move[0] : 0),
                               o.y + (ps ? ps->move[1] : 0),
                               o.z + (ps ? ps->move[2] : 0),
                               scriptRot(ps, rr));
        // Hidden pieces: ground-reference plates and deactivated-state
        // duplicates (*off), which the game shows only via activation scripts
        // we don't run. The model ROOT is always the flat base plate (AraGP,
        // zonnull, or just the unit name like zontrain/zonharpy1) with the real
        // model in its children, so skip its own primitives unconditionally.
        std::string oname = o.name;
        std::transform(oname.begin(), oname.end(), oname.begin(), ::tolower);
        auto ends = [&](const char* suf) {
            size_t n = std::strlen(suf);
            return oname.size() >= n && oname.compare(oname.size() - n, n, suf) == 0;
        };
        bool groundPlate = isRoot || ends("gp") || ends("null") || ends("off") ||
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
            const SDL_Rect* arect = nullptr;
            if (!p.texture.empty()) {
                std::string name = p.texture;
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                auto rit = atlasRect_.find(name);
                if (atlas && rit != atlasRect_.end()) {
                    tex = atlas;             // whole model shares one atlas texture
                    arect = &rit->second;
                } else {
                    // Fallback for any texture not packed into the atlas.
                    auto it = textures_.find(name);
                    if (it != textures_.end() && !it->second.empty()) {
                        // Each texture carries one variant per player colour; a
                        // player's slot is remappable (--color / --aicolor).
                        size_t ci = size_t(colorSlot_[player & 7]);
                        tex = it->second[ci < it->second.size() ? ci : 0];
                    }
                }
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
                    float wx = mirror ? -w[0] : w[0];   // un-mirror Zhon models on X
                    float rx = wx * cy + w[2] * sy;
                    float rz = -wx * sy + w[2] * cy;
                    // TAK billboards lean back (+y and +z together); moving
                    // away (+z) reads upward on screen, adding to height.
                    float ry = w[1] * ct + rz * st;
                    depth += rz * ct - w[1] * st;
                    tri.v[k].position = {rx, -ry};
                    static const SDL_FPoint uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                    SDL_FPoint c = uv[idx[k] & 3];
                    tri.v[k].tex_coord = arect
                        ? SDL_FPoint{(float(arect->x) + c.x * float(arect->w)) / float(atlasW_),
                                     (float(arect->y) + c.y * float(arect->h)) / float(atlasH_)}
                        : c;
                    tri.v[k].color = tex ? SDL_Color{255, 255, 255, 255}
                                         : SDL_Color{170, 170, 180, 255};
                }
                if (!ok) continue;
                tri.depth = depth / 3;
                out.push_back(tri);
            }
        }
        for (const auto& c : o.children)
            collect(out, atlas, c, xf, anim, heading, player, mirror, false);
    }

    void drawRing(float wx, float wz, float r) {
        float zm = mapView_.zoom();
        float lift = terrainLift(wx, wz) * zm;
        float liftX = terrainLiftX(wx, wz) * zm;
        SDL_SetRenderDrawColor(ren_, 90, 255, 120, 255);
        SDL_FPoint pts[25];
        for (int i = 0; i <= 24; ++i) {
            float a = float(i) / 24 * 2 * 3.14159f;
            pts[i] = {(wx + std::cos(a) * r - mapView_.offX()) * zm - liftX,
                      (wz + std::sin(a) * r * 0.7f - mapView_.offY()) * zm - lift};
        }
        SDL_RenderDrawLinesF(ren_, pts, 25);
    }

    SDL_Renderer* ren_;
    MapView mapView_;
    std::string dataRoot_;
    std::string ipRoot_;
    std::string tntPath_;   // map path (for MP start-position setup)
    bool crusades_ = false; // which balance registry_ currently holds
    std::string side_ = "ara";
    tak::sim::TypeRegistry registry_;
    tak::sim::World world_;
    // Hash maps (not std::map): these are looked up per unit per frame in the
    // serial anim loop and the parallel projection, and tree traversals were a
    // measurable slice of the update cost at thousands of units.
    std::unordered_map<std::string, Visual> visuals_;
    std::unordered_map<int, std::string> unitType_;
    std::unordered_map<int, Anim> anims_;
    std::map<std::string, std::vector<SDL_Texture*>> textures_;
    std::vector<Tri> tris_;
    std::vector<SDL_Vertex> triBatch_;   // reused per-unit vertex batch
    std::vector<UnitGeom> geomPool_;              // reused across frames (keeps capacity)
    std::unordered_map<int, int> geomIndex_;     // unit id -> slot in geomPool_
    std::vector<int> selection_;
    std::unordered_set<int> selSet_;   // rebuilt each draw for O(1) membership
    bool dragging_ = false;
    bool buildDrag_ = false;          // shift-drag placing a line of buildings
    float bdX0_ = 0, bdZ0_ = 0;       // build-drag start (world)
    bool draggingMinimap_ = false;
    float dragX0_ = 0, dragY0_ = 0, dragX1_ = 0, dragY1_ = 0;
    char pendingCmd_ = 0;   // armed order awaiting a click: 'f' fight-move,
                            // 'm' move, 'a' attack, 'p' patrol, 'g' guard
    std::map<int, std::vector<int>> groups_;   // control groups 0-9
    bool paused_ = false;
    int gameSpeed_ = 0;         // -10..+10 game-speed level (+/- keys); 0 = normal
    // 10^(level/10): +10 = 10x, 0 = 1x, -10 = 0.1x.
    // Game-speed multiplier. Forced to 1x in a networked game: the peers advance
    // the sim in lockstep at a fixed step, so scaling one peer's dt would desync.
    float speedMult() const {
        return mp_ ? 1.0f : std::pow(10.0f, float(gameSpeed_) / 10.0f);
    }
    bool showCounts_ = false;   // F4: per-faction live unit counts
    bool spectating_ = false;   // watching a live net game (no control, no fog)
    std::string playerName_[8];   // net games: display name per player (from lobby)
    bool playerAi_[8] = {};       // net games: which players are server-run AI
    bool showColorPicker_ = false;   // F6: pick the player colour
    bool showHDebug_ = false;   // F7: terrain-height / lift diagnostic overlay
    std::vector<std::pair<SDL_FRect, int>> colorRects_;   // picker swatch hit boxes
    float fps_ = 0;             // smoothed render FPS, shown on the F4 overlay
    int winW_ = 0, winH_ = 0;   // last-known window size (for centering/culling)
    tak::net::MpClient* mp_ = nullptr;
    std::vector<tak::net::Command> outbox_;   // local orders to send to the server
    uint64_t mpListMs_ = 0, mpFirstListMs_ = 0;   // auto-join: ListGames timing
    uint64_t mpSlowSinceMs_ = 0;                  // when the replay backlog went deep
    bool replayMode_ = false;                     // playing a recorded .takrep
    std::vector<tak::net::Bundle> replayBundles_;
    size_t replayTick_ = 0;
    float replayAccum_ = 0;
    bool mpReadied_ = false, mpStarted_ = false, mpSetupDone_ = false;
    // interactive lobby UI state
    enum class LobbyScreen { Browser, Create, Room } lobbyScreen_ = LobbyScreen::Browser;
    int lbField_ = 0;   // active text field: 1=createName 2=createPass 3=joinPass 4=chat
    std::string createName_ = "game", createPass_, joinPass_, chatDraft_;
    bool createCrusades_ = false, createGods_ = false;
    std::string mpMapId_;   // set from the launched map basename
    std::string mpResumePath_;   // where the resume ticket is saved (for reconnect)
    std::vector<std::pair<std::string, std::string>> chatLog_;
    // In-game chat: press Enter to compose, lines fade after a while. Kept apart
    // from the lobby chatLog_ so the in-game overlay and lobby panel don't mix.
    bool chatTyping_ = false;
    struct GameChat { std::string who, text; float age = 0; };
    std::vector<GameChat> gameChat_;
    uint64_t chatLastMs_ = 0;
    std::vector<std::pair<SDL_FRect, std::function<void()>>> lobbyHots_;
    int localPlayer_ = 0;
    // Player-colour slot per player (which colour variant of each unit texture to
    // use); defaults to the player index. Overridable via --color / --aicolor and
    // the in-game picker.
    int colorSlot_[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    // The 10 player-colour RGBs (sampled from a player-coloured logo texture at
    // load; the fallback below is a sensible distinct palette). Used so the HUD
    // and minimap match whatever slot a player renders in.
    SDL_Color playerColors_[10] = {
        {70, 130, 240, 255}, {215, 60, 55, 255}, {70, 185, 90, 255},
        {235, 205, 55, 255}, {225, 225, 225, 255}, {80, 200, 205, 255},
        {160, 95, 205, 255}, {230, 145, 50, 255}, {230, 120, 180, 255},
        {120, 120, 130, 255}};
    bool sampledColors_ = false;
    // RGB a player's units render in (its slot's player colour). The argument is
    // the sim ownership index (Unit::player) -- one player per slot in skirmish.
    SDL_Color playerColor(int player) const {
        int s = (player >= 0 && player < 8) ? colorSlot_[player] : 0;
        return playerColors_[(s >= 0 && s < 10) ? s : 0];
    }

    // Height-aware 2.5D: the world-pixel lift for a point, from the terrain height
    // under it, so a unit (and its shadow/health bar/selection/effects) sits on
    // the elevation the tile art shows instead of the flat grid cell. Zero at the
    // map's ground level; water and flat ground are unaffected.
    int heightRef_ = -1;                          // map ground level (modal height)
    float kHeightScale_ = 1.1f;                    // screen-Y lift per height unit (N, also occlusion + picking)
    float kHeightScaleX_ = 0.0f;                   // screen-X lift per height unit (+X = west). Off by default:
                                                   // a large value makes the diagonal picking march overshoot
                                                   // thin N-S walls. Opt in / tune small via TAK_HSCALEX.
    int kOccScan_ = 12;                            // cells to scan south for a wall
    // Height above ground at a world point, bilinearly sampled so lifts ramp
    // smoothly across a slope. Lazily initialises the modal ground reference and
    // reads the tunable scales / debug flag from the environment.
    float heightAbove(float wx, float wz) {
        const auto& m = mapView_.map();
        if (m.heights.empty() || m.width <= 0) return 0.0f;
        if (heightRef_ < 0) {
            long hist[256] = {0};
            for (uint8_t v : m.heights) hist[v]++;
            int best = 0;
            for (int i = 1; i < 256; ++i) if (hist[i] > hist[best]) best = i;
            heightRef_ = best;
            if (const char* e = getenv("TAK_HSCALE")) kHeightScale_ = std::stof(e);
            if (const char* e = getenv("TAK_HSCALEX")) kHeightScaleX_ = std::stof(e);
            if (getenv("TAK_HDEBUG")) showHDebug_ = true;
        }
        float gx = (wx - 8.0f) / 16.0f, gz = (wz - 8.0f) / 16.0f;
        int x0 = std::clamp(int(std::floor(gx)), 0, m.width - 1);
        int z0 = std::clamp(int(std::floor(gz)), 0, m.height - 1);
        int x1 = std::min(x0 + 1, m.width - 1), z1 = std::min(z0 + 1, m.height - 1);
        float fx = std::clamp(gx - float(x0), 0.0f, 1.0f);
        float fz = std::clamp(gz - float(z0), 0.0f, 1.0f);
        auto H = [&](int x, int z) { return float(m.heights[size_t(z) * m.width + x]); };
        float h = H(x0, z0) * (1 - fx) * (1 - fz) + H(x1, z0) * fx * (1 - fz) +
                  H(x0, z1) * (1 - fx) * fz + H(x1, z1) * fx * fz;
        return std::max(0.0f, h - float(heightRef_));
    }
    // Screen-space displacement of a world point's surface from its flat grid cell,
    // baked into the tile art by the tilted 2.5D view: up (Y) AND sideways (X).
    float terrainLift(float wx, float wz) { return heightAbove(wx, wz) * kHeightScale_; }
    float terrainLiftX(float wx, float wz) { return heightAbove(wx, wz) * kHeightScaleX_; }
    // The terrain-relief lift is for MOBILE units standing on painted slopes. A
    // building sits flat on its footprint, so it must never lift -- otherwise a
    // structure on a cell with a raised heightmap (e.g. a mana deposit, whose
    // heightmap runs 60-200 while the sand around it is 0) floats far off its
    // base decal (which, being a feature, is drawn unlifted).
    // A "structure" (building) for render/build purposes = one that can't actually
    // move. NOTE: the FBI `canmove` flag is unreliable -- some buildings (the Keep,
    // arakeep) set canmove=1 with NO velocity -- so key off maxVel, not type->canMove.
    static bool isStructure(const tak::sim::UnitType* t) { return !t || t->maxVel <= 0.0f; }
    float uLiftY(const tak::sim::Unit& u) { return isStructure(u.type) ? 0.0f : terrainLift(u.x, u.z); }
    float uLiftX(const tak::sim::Unit& u) { return isStructure(u.type) ? 0.0f : terrainLiftX(u.x, u.z); }

    // A unit's current render altitude (flyers rise to cruiseAlt; 0 for ground
    // units or units with no live anim). Used to lift a flyer's projectiles/effects
    // so they leave/strike at the altitude the unit is drawn, not the ground.
    float unitAltById(int id) const {
        auto it = anims_.find(id);
        return it != anims_.end() ? it->second.altitude : 0.0f;
    }
    // Render altitude of an airborne unit at ~this world point, else 0. Used to lift
    // an impact blast onto a flyer (the hit record only carries the impact point).
    float flyerAltAt(float x, float z) const {
        float best = 24.0f * 24.0f, alt = 0.0f;
        for (const auto& u : world_.units()) {
            if (!u.alive() || !u.type || !u.type->canFly) continue;
            float dx = u.x - x, dz = u.z - z, d = dx * dx + dz * dz;
            if (d < best) { best = d; alt = unitAltById(u.id); }
        }
        return alt;
    }

    // Screen position of a unit's drawn body centre, matching the render lift:
    // terrain relief (uLift*) plus, for a flyer, its cruise altitude (the sprite is
    // raised by alt*0.8*zoom, same factor as drawUnit). Used for height-correct
    // marquee/click selection so a lifted or airborne unit is picked where it's SEEN,
    // not at its flat ground cell.
    SDL_FPoint unitScreen(const tak::sim::Unit& u) {
        float zm = mapView_.zoom();
        float alt = 0.0f;
        if (u.type && u.type->canFly) {
            auto it = anims_.find(u.id);
            alt = (it != anims_.end()) ? it->second.altitude : u.type->cruiseAlt;
        }
        return {(u.x - mapView_.offX()) * zm - uLiftX(u) * zm,
                (u.z - mapView_.offY()) * zm - uLiftY(u) * zm - alt * 0.8f * zm - 12.0f * zm};
    }

    // Height-aware picking: invert the render lift so a click on elevated terrain
    // (a wall/plateau top, drawn lifted UP on screen) resolves to the cell whose
    // *lifted* position is under the cursor, not the flat cell the raw screen->world
    // map would give (which lands on the low ground behind the wall). Returns the
    // FRONT-MOST surface (largest z) that projects to the click, like a depth pick.
    void pickWorld(float sx, float sy, float& wx, float& wz) {
        float zm = mapView_.zoom();
        // Flat (no-lift) world position of the click.
        float cwx = mapView_.offX() + sx / zm;
        float cwz = mapView_.offY() + sy / zm;
        wx = cwx; wz = cwz;
        const auto& m = mapView_.map();
        if (m.heights.empty()) return;
        heightAbove(cwx, cwz);   // init ref/scales
        // A surface cell of height h renders at flat position (wx - h*scaleX,
        // wz - h*scaleY), so a point that projects to this click satisfies
        // (wx, wz) = (cwx + h*scaleX, cwz + h*scaleY) with h = heightAbove(wx, wz).
        // March h outward (both axes move together along the tilt) and take the
        // front-most self-consistent surface -- the largest h where the candidate's
        // actual height drops through the assumed h. Handles the X/Z coupling so a
        // click on a wall top resolves to that top, not the ground behind it.
        float maxH = std::max(0.0f, float(255 - (heightRef_ < 0 ? 0 : heightRef_)));
        float bestH = 0.0f;
        float prevDiff = heightAbove(cwx, cwz);   // actualH - 0 at h = 0
        const float step = 1.0f;
        for (float h = step; h <= maxH + 2; h += step) {
            float ax = cwx + h * kHeightScaleX_;
            float az = cwz + h * kHeightScale_;
            float diff = heightAbove(ax, az) - h;
            // Front-most self-consistent surface: interpolate the zero-crossing of
            // (actualHeight - h) so we land ON the surface, not past its far edge.
            if (prevDiff > 0 && diff <= 0)
                bestH = (h - step) + step * prevDiff / (prevDiff - diff);
            prevDiff = diff;
        }
        wx = std::clamp(cwx + bestH * kHeightScaleX_, 0.0f, float(m.width * 16 - 1));
        wz = std::clamp(cwz + bestH * kHeightScale_, 0.0f, float(m.height * 16 - 1));
    }

    // Terrain occlusion: a wall's baked-relief art projects up-and-north over the
    // flat ground behind it, but terrain is painted before units, so a unit on
    // that ground would draw ON the wall. Find the screen-Y of the projected top
    // surface of the tallest wall BETWEEN this unit and the camera (i.e. to its
    // south, larger z). Units are then clipped to above that line and the hidden
    // part re-drawn as a faint silhouette. Returns a huge value when nothing
    // occludes the unit. kHeightScale_/heightRef_ are lazily set by terrainLift.
    float wallOcclusionY(float wx, float wz) {
        const auto& m = mapView_.map();
        if (m.heights.empty()) return 1e9f;
        terrainLift(wx, wz);   // ensure heightRef_/kHeightScale_ are initialised
        float zm = mapView_.zoom();
        int cx = std::clamp(int(wx) / 16, 0, m.width - 1);
        int cz0 = std::clamp(int(wz) / 16, 0, m.height - 1);
        int hUnit = m.heights[size_t(cz0) * m.width + cx];
        float best = 1e9f;
        for (int dz = 1; dz <= kOccScan_; ++dz) {
            int cz = cz0 + dz;
            if (cz >= m.height) break;
            int h = m.heights[size_t(cz) * m.width + cx];
            if (h <= hUnit + 24) continue;   // not a wall relative to this unit
            // The wall's painted top projects north of its heightmap footprint by
            // ~height*kHeightScale_ (the art leans it up-and-back) -- the same scale
            // the unit lift and picking use, so occlusion, seating and clicks agree.
            float proj = std::max(0.0f, float(h - heightRef_) * kHeightScale_);
            float wy = (float(cz) * 16 + 8 - mapView_.offY()) * zm - proj * zm;
            if (wy < best) best = wy;
        }
        return best;
    }
    uint32_t netTick_ = 0;
    std::string netError_;
    bool follow_ = false;
    bool trackSel_ = false;   // T: keep the camera centred on the selection
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
        int player = 3;
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
    std::vector<int> aiKeeps_;  // all the AI's keeps (it builds several + expands)
    // Server-portable skirmish AI (src/ai). aiProfile_ is the parsed
    // ai/default.txt weight/limit table; one Controller drives each AI player,
    // built lazily on first use. kAiSeed is fixed so a match is reproducible
    // (the real lobby will supply a per-game seed in multiplayer M3).
    tak::ai::Profile aiProfile_;
    std::vector<tak::ai::Controller> aiControllers_;
    bool aiReady_ = false;
    static constexpr uint32_t kAiSeed = 0x1234567u;
    const tak::sim::UnitType* placing_ = nullptr;
    float mouseX_ = -1, mouseY_ = -1;   // -1 until the first real mouse motion, so
                                        // edge-scroll can't fire from a (0,0) default
                                        // cursor on launch (before the mouse moves)
    SDL_Texture* fogTex_ = nullptr;
    SDL_Texture* miniTex_ = nullptr;
    SDL_Texture* panelTex_ = nullptr;
    int panelW_ = 0, panelH_ = 0;
    SDL_Texture* botTex_ = nullptr;
    int botW_ = 0, botH_ = 0;
    std::map<std::string, SDL_Texture*> icons_;
    std::map<std::pair<std::string, int>, SDL_Texture*> modelIcons_;  // model-rendered fallback icons
    std::vector<std::pair<SDL_FRect, const tak::sim::UnitType*>> iconRects_;
    int aiTrained_ = 0;
    std::array<std::string, 4> aiCycle_ = {"tararch", "tartb", "tararch", "tarbeak"};
    static constexpr int kMiniSize = 180;
    // Right-side UI strip (minimap + order/weapon buttons). The map view is kept
    // to the left of it so the panel never draws over the world.
    static constexpr int kPanelW = kMiniSize + 20;
    int mapViewW(int winW) const { return std::max(64, winW - kPanelW); }

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
        // All unit dots batched into one draw call (per-unit FillRect + colour
        // set was thousands of state changes a frame at large unit counts).
        shadowBatch_.clear();
        for (const auto& u : world_.units()) {
            if (!u.alive() || u.embarked() || !u.type) continue;
            if (!alliedToLocal(u.player) && !world_.cellVisible(u.x, u.z)) continue;
            SDL_FPoint p = toMini(u.x, u.z);
            SDL_Color tc = playerColor(u.player);
            pushQuad(shadowBatch_, p.x - 1.5f, p.y - 1.5f, 3, 3, tc);
        }
        if (!shadowBatch_.empty())
            SDL_RenderGeometry(ren_, nullptr, shadowBatch_.data(),
                               int(shadowBatch_.size()), nullptr, 0);
        // Camera view rectangle.
        float zm = mapView_.zoom();
        SDL_FPoint a = toMini(mapView_.offX(), mapView_.offY());
        SDL_FRect view{a.x, a.y, winW / zm / mapW * r.w,
                       (winH - kBarH) / zm / mapH * r.h};
        SDL_SetRenderDrawColor(ren_, 240, 240, 240, 200);
        SDL_RenderDrawRectF(ren_, &view);
    }

    // Returns true if the click was inside the minimap (and moved the camera).
    // Map a minimap-space click to world coords; false if outside the minimap.
    bool minimapToWorld(float mx, float my, int winW, int winH, float& wx, float& wz) {
        SDL_FRect r = minimapRect(winW, winH);
        if (mx < r.x || my < r.y || mx > r.x + r.w || my > r.y + r.h) return false;
        wx = (mx - r.x) / r.w * float(mapView_.map().blocksX) * 32;
        wz = (my - r.y) / r.h * float(mapView_.map().blocksY) * 32;
        return true;
    }

    bool minimapClick(float mx, float my, int winW, int winH) {
        float wx, wz;
        if (!minimapToWorld(mx, my, winW, winH, wx, wz)) return false;
        float zm = mapView_.zoom();   // centre the clicked point in the map viewport
        mapView_.setOffset(wx - mapViewW(winW) / zm / 2,
                           wz - (winH - int(kBarH)) / zm / 2);
        return true;
    }

    // Right-click on the minimap: order the selection to that world point.
    bool minimapOrder(float mx, float my, int winW, int winH, bool queue) {
        float wx, wz;
        if (selection_.empty() || !minimapToWorld(mx, my, winW, winH, wx, wz)) return false;
        for (int id : selection_) {
            const auto* u = world_.unit(id);
            if (!u || u->player != localPlayer_) continue;
            tak::net::Command c;
            c.kind = tak::net::Cmd::Move;
            c.unitId = id;
            c.x = wx;
            c.z = wz;
            c.queue = queue ? 1 : 0;
            issue(c);
        }
        if (const auto* u = world_.unit(selection_.front()); u && u->player == localPlayer_)
            voice(selection_.front(), "move");
        return true;
    }

    // Issue the armed order (pendingCmd_) to the selection at world (wx,wz). On the
    // main map (`precise`) 'a' targets an enemy under the cursor and 'g' the friendly
    // under it; from the minimap (coarse) 'a' falls back to attack-move and 'g' to the
    // nearest friendly. Shared by the map click and the minimap click.
    void issueArmedOrder(char cmd, float wx, float wz, bool queue, bool precise) {
        if (selection_.empty()) return;
        if (cmd == 'g') {   // guard: needs a friendly unit
            int buddy = -1;
            float best = precise ? 24.0f : 96.0f;   // generous radius on the minimap
            best *= best;
            for (auto& u : world_.units()) {
                if (!u.alive() || u.player != localPlayer_) continue;
                float dx = u.x - wx, dz = u.z - wz, d = dx * dx + dz * dz;
                if (d < best) { best = d; buddy = u.id; }
            }
            if (buddy < 0) return;
            for (int id : selection_) {
                if (id == buddy) continue;
                tak::net::Command c;
                c.kind = tak::net::Cmd::Guard;
                c.unitId = id;
                c.targetId = buddy;
                c.queue = queue ? 1 : 0;
                issue(c);
            }
            voice(selection_.front(), "guard");
            return;
        }
        // 'a' (attack) targets an enemy under a precise click; otherwise (and always
        // on the minimap) it is an attack-move to the ground point.
        int enemy = -1;
        if (cmd == 'a' && precise) {
            const auto* first = world_.unit(selection_.front());
            float best = 20 * 20;
            for (auto& u : world_.units()) {
                if (!u.alive() || u.embarked() || !first ||
                    world_.allied(u.player, first->player))
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
                c.kind = (cmd == 'f' || cmd == 'a') ? tak::net::Cmd::AttackMove
                         : cmd == 'p'               ? tak::net::Cmd::Patrol
                                                    : tak::net::Cmd::Move;
                c.x = wx;
                c.z = wz;
            }
            c.unitId = id;
            c.queue = queue ? 1 : 0;
            issue(c);
        }
        voice(selection_.front(),
              (cmd == 'a' || cmd == 'f') ? "attack" : cmd == 'p' ? "patrol" : "move");
    }

    // Left-click on the minimap while an order is armed (e.g. F): issue it at the
    // minimap location (F + minimap = fight-move there). Returns true if handled.
    bool minimapArmedOrder(float mx, float my, int winW, int winH) {
        if (!pendingCmd_) return false;
        float wx, wz;
        if (!minimapToWorld(mx, my, winW, winH, wx, wz)) return false;
        issueArmedOrder(pendingCmd_, wx, wz, (SDL_GetModState() & KMOD_SHIFT) != 0, false);
        pendingCmd_ = 0;
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
        bool mana = false;   // category=mana (deposit cluster: kept walkable/buildable)
        bool glowy = false;  // the animated "Sacred Stone" centre -- the actual
                             // buildable spot; category=mana + animating=1. The
                             // static "Standing Stones" (animating=0) are decoration.
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

    // Unit ground shadows: the sprites from data/anims/shadows.gaf named by each
    // unit's FBI `shadowart`, recoloured to translucent black. Loaded once.
    struct ShadowTex { SDL_Texture* tex = nullptr; int w = 0, h = 0, xoff = 0, yoff = 0; };
    std::map<std::string, ShadowTex> shadowTex_;
    bool shadowsLoaded_ = false;
    const ShadowTex* shadowFor(const std::string& art) {
        if (art.empty()) return nullptr;
        if (!shadowsLoaded_) {
            shadowsLoaded_ = true;
            const auto* pal = featurePalette("aramon");
            if (pal) try {
                for (auto& sq : tak::gaf::load(dataRoot_ + "/anims/shadows.gaf", *pal)) {
                    if (sq.frames.empty() || sq.frames[0].width == 0) continue;
                    auto& fr = sq.frames[0];
                    std::vector<uint8_t> px = fr.rgba;   // silhouette -> translucent black
                    for (size_t i = 0; i + 3 < px.size(); i += 4) {
                        px[i] = px[i + 1] = px[i + 2] = 0;
                        px[i + 3] = px[i + 3] ? 90 : 0;
                    }
                    SDL_Texture* t = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                                       SDL_TEXTUREACCESS_STATIC,
                                                       fr.width, fr.height);
                    SDL_UpdateTexture(t, nullptr, px.data(), fr.width * 4);
                    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                    std::string name = sq.name;
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    shadowTex_[name] = {t, fr.width, fr.height, fr.xoff, fr.yoff};
                }
            } catch (const std::exception&) {}
        }
        auto it = shadowTex_.find(art);
        return it != shadowTex_.end() ? &it->second : nullptr;
    }

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
        // The buildable spot is the animated Sacred Stone centre; the static
        // Standing Stones sharing the category are just ruins around it.
        inst.glowy = inst.mana && di->second.numberOr("animating", 0) != 0;
        features_.push_back(inst);
        // Retail nav-blocking: ordinary obstacle features AND the static Standing
        // Stones (blocking=1) block; only the glowy Sacred Stone centre stays
        // walkable, so a lodestone can build on it. (Standing Stones default to
        // blocking; the centre has no blocking field, so it never blocks.)
        bool blocks = !inst.glowy && (!inst.mana || di->second.numberOr("blocking", 0) != 0);
        if (blockNav && blocks) {
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
        // them (and the AI knows where to put them). The buildable spot is the
        // GLOWING centre (animated Sacred Stone) -- NOT the ring of static
        // Standing Stones around it, which share category=mana but are just
        // ruins; including them pulled the spot off-centre. One deposit can have
        // a couple of adjacent glowy stones, so still cluster (union-find, 60px
        // link) and keep ONE spot per cluster. Fallback: a deposit with no glowy
        // centre at all (odd data) uses its category=mana features instead.
        std::vector<std::pair<float, float>> raw;
        for (const auto& f : features_)
            if (f.glowy) raw.push_back({f.x, f.z});
        if (raw.empty())
            for (const auto& f : features_)
                if (f.mana) raw.push_back({f.x, f.z});
        std::vector<int> par(raw.size());
        for (size_t i = 0; i < par.size(); ++i) par[i] = int(i);
        std::function<int(int)> find = [&](int a) {
            while (par[size_t(a)] != a) { par[size_t(a)] = par[size_t(par[size_t(a)])]; a = par[size_t(a)]; }
            return a;
        };
        const float link2 = 60.0f * 60.0f;
        for (size_t i = 0; i < raw.size(); ++i)
            for (size_t j = i + 1; j < raw.size(); ++j) {
                float dx = raw[i].first - raw[j].first, dz = raw[i].second - raw[j].second;
                if (dx * dx + dz * dz < link2) par[size_t(find(int(i)))] = find(int(j));
            }
        std::map<int, std::pair<std::pair<double, double>, int>> acc;   // root -> (sum, count)
        for (size_t i = 0; i < raw.size(); ++i) {
            auto& a = acc[find(int(i))];
            a.first.first += raw[i].first; a.first.second += raw[i].second; ++a.second;
        }
        manaSpots_.clear();
        for (auto& [root, a] : acc)
            manaSpots_.push_back({float(a.first.first / a.second),
                                  float(a.first.second / a.second)});
        world_.setManaSpots(manaSpots_);
        // Standing Stones block nav, but a large one can reach the glowy centre;
        // keep every deposit buildable by carving the 2x2 lodestone footprint
        // clear at each spot (canPlace tests exactly these cells).
        for (const auto& [sx, sz] : manaSpots_)
            world_.nav().block(int(sx) / 16 - 1, int(sz) / 16 - 1, 2, 2, false);
        std::printf("mana deposits: %zu (from %zu features)\n",
                    manaSpots_.size(), raw.size());
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
            if (hot) {
                float px = 1.8f;
                float tw = blockWidth(b.label, px);
                SDL_SetRenderDrawColor(ren_, 0, 0, 0, 210);
                SDL_FRect tb{r.x - tw - 14, r.y + 14, tw + 10, 22};
                SDL_RenderFillRectF(ren_, &tb);
                blockText(b.label, r.x - tw - 9, r.y + 18, px, {235, 225, 180, 255});
            }
        }
        drawWeaponButtons(winW, winH);
    }

    // The front selected unit, if it has more than one weapon (worth a picker).
    const tak::sim::Unit* multiWeaponSel() {
        if (selection_.empty()) return nullptr;
        const auto* u = world_.unit(selection_.front());
        if (u && u->alive() && u->type && u->type->weapons.size() > 1) return u;
        return nullptr;
    }

    std::vector<SDL_FRect> weaponRects_;
    // A small row of weapon-select buttons under the order column: retail fires
    // only the active weapon; click (or press W to cycle) to pick another.
    void drawWeaponButtons(int winW, int winH) {
        weaponRects_.clear();
        const auto* u = multiWeaponSel();
        if (!u) return;
        int n = int(u->type->weapons.size());
        const float bw = 26, gap = 4;
        // A row just above the HUD bar, right-aligned to the order column's right
        // edge (the row can be wider than the 56px column, so don't centre it or it
        // runs off the screen edge).
        float y = float(winH) - kBarH - bw - 8;
        float row = n * bw + (n - 1) * gap;
        float x0 = float(winW) - 4 - row;
        for (int i = 0; i < n; ++i) {
            SDL_FRect r{x0 + i * (bw + gap), y, bw, bw};
            weaponRects_.push_back(r);
            bool active = u->weaponSlot == i;
            bool hot = mouseX_ >= r.x && mouseX_ <= r.x + r.w && mouseY_ >= r.y &&
                       mouseY_ <= r.y + r.h;
            SDL_SetRenderDrawColor(ren_, active ? 90 : 34, active ? 70 : 30,
                                   active ? 30 : 26, 235);
            SDL_RenderFillRectF(ren_, &r);
            SDL_SetRenderDrawColor(ren_, active ? 255 : (hot ? 200 : 120),
                                   active ? 220 : (hot ? 180 : 105),
                                   active ? 90 : 80, 255);
            SDL_RenderDrawRectF(ren_, &r);
            char lbl[4];
            std::snprintf(lbl, sizeof lbl, "%d", i + 1);
            blockText(lbl, r.x + 8, r.y + 6, 2.0f,
                      active ? SDL_Color{255, 240, 180, 255} : SDL_Color{205, 195, 165, 255});
            if (hot && !u->type->weapons[size_t(i)].name.empty()) {
                const std::string& nm = u->type->weapons[size_t(i)].name;
                float px = 1.6f, tw = blockWidth(nm.c_str(), px);
                SDL_SetRenderDrawColor(ren_, 0, 0, 0, 210);
                SDL_FRect tb{r.x - tw - 14, r.y + 4, tw + 10, 20};
                SDL_RenderFillRectF(ren_, &tb);
                blockText(nm.c_str(), r.x - tw - 9, r.y + 7, px, {235, 225, 180, 255});
            }
        }
    }

    // Issue SetWeapon(slot) for every selected unit that has that slot.
    void selectWeapon(int slot) {
        for (int id : selection_) {
            const auto* u = world_.unit(id);
            if (!u || !u->type || int(u->type->weapons.size()) <= slot) continue;
            tak::net::Command c;
            c.kind = tak::net::Cmd::SetWeapon;
            c.unitId = id;
            c.targetId = slot;
            issue(c);
        }
    }

    bool weaponButtonClick(float mx, float my) {
        for (size_t i = 0; i < weaponRects_.size(); ++i) {
            const auto& r = weaponRects_[i];
            if (mx < r.x || mx > r.x + r.w || my < r.y || my > r.y + r.h) continue;
            selectWeapon(int(i));
            return true;
        }
        return false;
    }

    // Returns true if the click hit (and was handled by) the order column.
    bool orderColumnClick(float mx, float my, int winW) {
        if (weaponButtonClick(mx, my)) return true;
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

    // Fallback build icon: render the unit's 3D model into a small cached texture,
    // for buildables that ship no anims/buildpic. Retail never drew a build pic for
    // the Zhon trapdoor spider (zonspide) -- a base-game creature the Crusades
    // balance made buildable -- so without this its slot would be an empty box.
    SDL_Texture* modelIconTex(const std::string& id, int slot, bool canMove) {
        auto keyp = std::make_pair(id, slot);
        if (auto it = modelIcons_.find(keyp); it != modelIcons_.end()) return it->second;
        if (gpuAllocBlocked()) return nullptr;   // retry after the alloc backoff lapses
        modelIcons_[keyp] = nullptr;   // cache the attempt (success or failure) up front
        if (!ghostModel(id)) return nullptr;
        auto vt = visuals_.find(id);
        if (vt == visuals_.end()) return nullptr;
        SDL_Texture* atlas = atlasFor(slot);
        std::vector<Tri> scratch;
        float facing = canMove ? -0.6f : 0.0f;   // slight 3/4 turn reads as a portrait
        collect(scratch, atlas, vt->second.model.root, Xform{}, nullptr, facing, 0, false);
        if (scratch.empty()) return nullptr;
        std::stable_sort(scratch.begin(), scratch.end(),
                         [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        for (auto& t : scratch)
            for (int i = 0; i < 3; ++i) {
                minX = std::min(minX, t.v[i].position.x);
                minY = std::min(minY, t.v[i].position.y);
                maxX = std::max(maxX, t.v[i].position.x);
                maxY = std::max(maxY, t.v[i].position.y);
            }
        const int ICON = 64;
        SDL_Texture* tgt = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                             SDL_TEXTUREACCESS_TARGET, ICON, ICON);
        // Transient VRAM failure: un-cache so the icon renders after the backoff
        // (the up-front nullptr stays only for permanent no-model failures).
        if (!tgt) { noteGpuAllocFail(); modelIcons_.erase(keyp); return nullptr; }
        SDL_SetTextureBlendMode(tgt, SDL_BLENDMODE_BLEND);
        SDL_Texture* prev = SDL_GetRenderTarget(ren_);
        SDL_SetRenderTarget(ren_, tgt);
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 0);
        SDL_RenderClear(ren_);
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        float span = std::max({maxX - minX, maxY - minY, 1.0f});
        float s = 0.82f * float(ICON) / span;
        float ox = float(ICON) * 0.5f - (minX + maxX) * 0.5f * s;
        float oy = float(ICON) * 0.5f - (minY + maxY) * 0.5f * s;
        for (auto& t : scratch) {
            SDL_Vertex v[3];
            for (int i = 0; i < 3; ++i) {
                v[i] = t.v[i];
                v[i].position.x = t.v[i].position.x * s + ox;
                v[i].position.y = t.v[i].position.y * s + oy;
            }
            SDL_RenderGeometry(ren_, t.tex, v, 3, nullptr, 0);
        }
        SDL_SetRenderTarget(ren_, prev);
        modelIcons_[keyp] = tgt;
        return tgt;
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
        // +/- (and keypad +/-) step game speed over -10..+10 (0 = normal).
        if (key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS ||
            key == SDLK_MINUS || key == SDLK_KP_MINUS) {
            bool up = (key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS);
            if (mp_) {   // lockstep peers must share one clock
                notice_ = "GAME SPEED LOCKED IN NET GAMES";
                noticeTimer_ = 2;
                return true;
            }
            gameSpeed_ = std::clamp(gameSpeed_ + (up ? 1 : -1), -10, 10);
            notice_ = "GAME SPEED " + std::string(gameSpeed_ > 0 ? "+" : "") +
                      std::to_string(gameSpeed_);
            noticeTimer_ = 2;
            return true;
        }
        if (key == SDLK_F4) { showCounts_ = !showCounts_; return true; }
        if (key == SDLK_F6) { showColorPicker_ = !showColorPicker_; return true; }
        if (key == SDLK_F7) { showHDebug_ = !showHDebug_; return true; }
        if (key == SDLK_F8) {                     // toggle LOD impostors (A/B perf)
            lodEnabled_ = !lodEnabled_;
            notice_ = lodEnabled_ ? "LOD ON" : "LOD OFF";
            noticeTimer_ = 2;
            return true;
        }
        if (key == SDLK_F10 && shift) {           // force-rebuild the baked atlases
            invalidateRenderTargets();            // (recovers a GPU render-target reset)
            notice_ = "REBUILT SPRITE ATLASES";
            noticeTimer_ = 2;
            return true;
        }
        if (key == SDLK_F10) {                    // cycle sprite mode AUTO->ON->OFF
            spriteMode_ = (spriteMode_ + 1) % 3;
            notice_ = spriteMode_ == SPR_AUTO ? "SPRITES AUTO"
                    : spriteMode_ == SPR_ON   ? "SPRITES ON" : "SPRITES OFF";
            noticeTimer_ = 2;
            return true;
        }
        if (key == SDLK_d && ctrl) {              // self-destruct the selected unit(s)
            // Through the command path (Cmd::Destroy), not a direct hp write --
            // a local mutation would silently desync a networked game.
            int n = 0;
            for (int id : selection_)
                if (auto* su = world_.unit(id))
                    if (su->alive() && su->player == localPlayer_) {
                        tak::net::Command c;
                        c.kind = tak::net::Cmd::Destroy;
                        c.unitId = id;
                        issue(c);
                        ++n;
                    }
            notice_ = "DESTRUCT " + std::to_string(n);
            noticeTimer_ = 2;
            return true;
        }
        // [ and ] tune the LOD size threshold live (raise it to make impostors
        // engage at larger on-screen sizes / less zoom-out).
        if (key == SDLK_LEFTBRACKET || key == SDLK_RIGHTBRACKET) {
            lodPx_ = std::clamp(lodPx_ + (key == SDLK_RIGHTBRACKET ? 16.0f : -16.0f),
                                16.0f, 400.0f);
            notice_ = "LOD THRESHOLD " + std::to_string(int(lodPx_)) + "px";
            noticeTimer_ = 2;
            return true;
        }
        // F9/F11 stress spawns mutate the world outside the command path, so
        // they are dev-only: in a networked game they would instantly desync.
        if ((key == SDLK_F9 || key == SDLK_F11) && mp_) {
            notice_ = "STRESS SPAWN DISABLED IN NET GAMES";
            noticeTimer_ = 2;
            return true;
        }
        // F9: stress test -- spawn 500 Zhon drakes across the current view.
        if (key == SDLK_F9) {
            float zm = std::max(mapView_.zoom(), 1e-3f);
            float cx = mapView_.offX() + (winW_ / 2.0f) / zm;
            float cz = mapView_.offY() + (winH_ / 2.0f) / zm;
            const int nx = 25, nz = 20;   // 25 * 20 = 500
            int made = 0;
            for (int j = 0; j < nz; ++j)
                for (int i = 0; i < nx; ++i) {
                    float x = cx + (i - (nx - 1) * 0.5f) * 24.0f;
                    float z = cz + (j - (nz - 1) * 0.5f) * 22.0f;
                    if (spawn("zondrake", x, z, 0.0f, localPlayer_) >= 0) ++made;
                }
            notice_ = "SPAWNED " + std::to_string(made) + " DRAKES";
            noticeTimer_ = 3;
            return true;
        }
        // F11: stress test -- spawn 500 Zhon trolls across the current view.
        if (key == SDLK_F11) {
            float zm = std::max(mapView_.zoom(), 1e-3f);
            float cx = mapView_.offX() + (winW_ / 2.0f) / zm;
            float cz = mapView_.offY() + (winH_ / 2.0f) / zm;
            const int nx = 25, nz = 20;
            int made = 0;
            for (int j = 0; j < nz; ++j)
                for (int i = 0; i < nx; ++i) {
                    float x = cx + (i - (nx - 1) * 0.5f) * 20.0f;
                    float z = cz + (j - (nz - 1) * 0.5f) * 18.0f;
                    if (spawn("zontroll", x, z, 0.0f, localPlayer_) >= 0) ++made;
                }
            notice_ = "SPAWNED " + std::to_string(made) + " TROLLS";
            noticeTimer_ = 3;
            return true;
        }

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
            case SDLK_w:                                   // cycle active weapon
                if (const auto* u = multiWeaponSel()) {
                    int n = int(u->type->weapons.size());
                    selectWeapon((u->weaponSlot + 1) % n);
                }
                pendingCmd_ = 0;
                return true;
            case SDLK_s:                                   // stop (immediate)
                for (int id : selection_) {
                    tak::net::Command c;
                    c.kind = tak::net::Cmd::Stop;
                    c.unitId = id;
                    issue(c);
                }
                pendingCmd_ = 0;
                return true;
            case SDLK_t:                                   // track/untrack selection
                trackSel_ = !trackSel_;
                if (trackSel_) centerOnSelection();
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
            if (u.alive() && u.player == localPlayer_ && u.type && !u.underConstruction &&
                pred(u))
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
            if (u.alive() && u.player == localPlayer_ && u.type && u.type->canMove &&
                !u.underConstruction)
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

    // Centre the camera on the average position of the live selected units.
    // Returns false if nothing in the selection is still alive.
    bool centerOnSelection() {
        float cx = 0, cz = 0;
        int n = 0;
        for (int id : selection_) {
            const auto* u = world_.unit(id);
            if (u && u->alive()) { cx += u->x; cz += u->z; ++n; }
        }
        if (!n) return false;
        mapView_.setOffset(cx / n - (winW_ / 2.0f) / mapView_.zoom(),
                           cz / n - (winH_ / 2.0f) / mapView_.zoom());
        return true;
    }

    // The player's HUD accent — follows their chosen player colour, not faction.
    SDL_Color factionColor() const { return playerColor(localPlayer_); }

    // Is this player on the local player's team? (Allies share vision, so their
    // units render/appear on the minimap through fog just like your own.)
    bool alliedToLocal(int player) const { return world_.allied(player, localPlayer_); }

    // A built-in 5x7 pixel font (uppercase, digits, a few symbols), drawn as
    // solid blocks — unmistakably legible at any size, unlike the game's small
    // decorative fonts. Each glyph is 5 columns; bit 0 of a column is the top.
    static const uint8_t* glyph5x7(char c) {
        static const std::map<char, std::array<uint8_t, 5>> F = {
            {'0',{0x3E,0x51,0x49,0x45,0x3E}}, {'1',{0x00,0x42,0x7F,0x40,0x00}},
            {'2',{0x42,0x61,0x51,0x49,0x46}}, {'3',{0x21,0x41,0x45,0x4B,0x31}},
            {'4',{0x18,0x14,0x12,0x7F,0x10}}, {'5',{0x27,0x45,0x45,0x45,0x39}},
            {'6',{0x3C,0x4A,0x49,0x49,0x30}}, {'7',{0x01,0x71,0x09,0x05,0x03}},
            {'8',{0x36,0x49,0x49,0x49,0x36}}, {'9',{0x06,0x49,0x49,0x29,0x1E}},
            {'A',{0x7E,0x11,0x11,0x11,0x7E}}, {'B',{0x7F,0x49,0x49,0x49,0x36}},
            {'C',{0x3E,0x41,0x41,0x41,0x22}}, {'D',{0x7F,0x41,0x41,0x22,0x1C}},
            {'E',{0x7F,0x49,0x49,0x49,0x41}}, {'F',{0x7F,0x09,0x09,0x09,0x01}},
            {'G',{0x3E,0x41,0x49,0x49,0x7A}}, {'H',{0x7F,0x08,0x08,0x08,0x7F}},
            {'I',{0x00,0x41,0x7F,0x41,0x00}}, {'J',{0x20,0x40,0x41,0x3F,0x01}},
            {'K',{0x7F,0x08,0x14,0x22,0x41}}, {'L',{0x7F,0x40,0x40,0x40,0x40}},
            {'M',{0x7F,0x02,0x0C,0x02,0x7F}}, {'N',{0x7F,0x04,0x08,0x10,0x7F}},
            {'O',{0x3E,0x41,0x41,0x41,0x3E}}, {'P',{0x7F,0x09,0x09,0x09,0x06}},
            {'Q',{0x3E,0x41,0x51,0x21,0x5E}}, {'R',{0x7F,0x09,0x19,0x29,0x46}},
            {'S',{0x46,0x49,0x49,0x49,0x31}}, {'T',{0x01,0x01,0x7F,0x01,0x01}},
            {'U',{0x3F,0x40,0x40,0x40,0x3F}}, {'V',{0x1F,0x20,0x40,0x20,0x1F}},
            {'W',{0x7F,0x20,0x18,0x20,0x7F}}, {'X',{0x63,0x14,0x08,0x14,0x63}},
            {'Y',{0x07,0x08,0x70,0x08,0x07}}, {'Z',{0x61,0x51,0x49,0x45,0x43}},
            {'/',{0x20,0x10,0x08,0x04,0x02}}, {'+',{0x08,0x08,0x3E,0x08,0x08}},
            {'-',{0x08,0x08,0x08,0x08,0x08}}, {':',{0x00,0x36,0x36,0x00,0x00}},
            {'.',{0x00,0x60,0x60,0x00,0x00}}, {'%',{0x63,0x13,0x08,0x64,0x63}},
        };
        auto it = F.find(c);
        return it == F.end() ? nullptr : it->second.data();
    }

    // Draw text in the built-in block font. `px` is the size of one font pixel.
    void blockText(const std::string& s, float x, float y, float px, SDL_Color c) {
        SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, c.a);
        float cx = x;
        for (char ch : s) {
            char u = char(std::toupper((unsigned char)ch));
            const uint8_t* cols = glyph5x7(u);
            if (!cols) { cx += 4 * px; continue; }   // space / unknown
            for (int col = 0; col < 5; ++col)
                for (int row = 0; row < 7; ++row)
                    if (cols[col] & (1 << row)) {
                        SDL_FRect r{cx + col * px, y + row * px, px, px};
                        SDL_RenderFillRectF(ren_, &r);
                    }
            cx += 6 * px;
        }
    }
    float blockWidth(const std::string& s, float px) const { return s.size() * 6 * px; }

    // ==================== interactive multiplayer lobby ====================

    static const char* factionName(int f) {
        static const char* n[5] = {"ARAMON", "TAROS", "VERUNA", "ZHON", "CREON"};
        return n[f % 5];
    }
    bool lbHot(const SDL_FRect& r) const {
        return mouseX_ >= r.x && mouseX_ <= r.x + r.w && mouseY_ >= r.y && mouseY_ <= r.y + r.h;
    }
    // A clickable button: panel + centered label; registers its action.
    void lbBtn(float x, float y, float w, float h, const std::string& label, bool enabled,
               std::function<void()> action, SDL_Color base = {60, 66, 86, 255}) {
        SDL_FRect r{x, y, w, h};
        bool hot = enabled && lbHot(r);
        SDL_Color c = enabled ? (hot ? SDL_Color{90, 110, 150, 255} : base)
                              : SDL_Color{40, 42, 50, 255};
        SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, 255);
        SDL_RenderFillRectF(ren_, &r);
        SDL_SetRenderDrawColor(ren_, hot ? 180 : 90, hot ? 200 : 100, hot ? 240 : 130, 255);
        SDL_RenderDrawRectF(ren_, &r);
        float px = 2.0f, tw = blockWidth(label, px);
        blockText(label, x + (w - tw) / 2, y + (h - 7 * px) / 2, px,
                  enabled ? SDL_Color{225, 230, 240, 255} : SDL_Color{110, 115, 125, 255});
        if (enabled && action) lobbyHots_.push_back({r, std::move(action)});
    }
    // A text-input field: label + box; clicking activates it (id != 0).
    void lbField(float x, float y, float w, const std::string& label,
                 const std::string& value, int id) {
        blockText(label, x, y, 1.6f, {150, 155, 170, 255});
        SDL_FRect r{x, y + 14, w, 22};
        bool active = (lbField_ == id);
        SDL_SetRenderDrawColor(ren_, 24, 26, 34, 255);
        SDL_RenderFillRectF(ren_, &r);
        SDL_SetRenderDrawColor(ren_, active ? 200 : 90, active ? 210 : 100, active ? 130 : 130, 255);
        SDL_RenderDrawRectF(ren_, &r);
        std::string shown = value + (active ? "_" : "");
        blockText(shown, x + 6, y + 20, 1.8f, {225, 230, 240, 255});
        lobbyHots_.push_back({r, [this, id] { lbField_ = id; SDL_StartTextInput(); }});
    }
    void colorSwatch(float x, float y, float s, int color, std::function<void()> action) {
        SDL_FRect r{x, y, s, s};
        SDL_Color c = playerColors_[color % 10];
        SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, 255);
        SDL_RenderFillRectF(ren_, &r);
        SDL_SetRenderDrawColor(ren_, lbHot(r) ? 255 : 30, lbHot(r) ? 255 : 30, 30, 255);
        SDL_RenderDrawRectF(ren_, &r);
        if (action) lobbyHots_.push_back({r, std::move(action)});
    }

    void drawLobby(int winW, int winH) {
        lobbyHots_.clear();
        // absorb any new chat
        if (mp_) for (auto& m : mp_->takeChat()) chatLog_.push_back(m);
        // ground
        SDL_SetRenderDrawColor(ren_, 16, 18, 26, 255);
        SDL_RenderClear(ren_);
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        float cx = winW / 2.0f;
        blockText("TA:KINGDOMS  MULTIPLAYER", cx - blockWidth("TA:KINGDOMS  MULTIPLAYER", 2.6f) / 2,
                  24, 2.6f, {210, 200, 150, 255});
        if (!mp_) return;
        auto st = mp_->state();
        if (st == tak::net::MpClient::State::Connecting) {
            blockText("connecting to server...", cx - 120, winH / 2.0f, 2.0f, {200, 200, 210, 255});
            return;
        }
        if (st == tak::net::MpClient::State::Done) {
            std::string m = "disconnected: " + (mp_->error().empty() ? std::string("server closed") : mp_->error());
            blockText(m, cx - blockWidth(m, 2.0f) / 2, winH / 2.0f, 2.0f, {255, 130, 110, 255});
            return;
        }
        if (st == tak::net::MpClient::State::InRoom) drawRoom(winW, winH);
        else if (lobbyScreen_ == LobbyScreen::Create) drawCreate(winW, winH);
        else drawBrowser(winW, winH);
    }

    void drawBrowser(int winW, int winH) {
        (void)winH;
        // keep the list fresh without needing a manual refresh
        if (SDL_GetTicks64() - mpListMs_ > 1000) { mp_->listGames(); mpListMs_ = SDL_GetTicks64(); }
        float x = 60, y = 80, w = winW - 120.0f;
        blockText("GAMES", x, y, 2.2f, {200, 205, 220, 255});
        lbBtn(x + w - 200, y - 6, 95, 26, "REFRESH", true, [this] { mp_->listGames(); });
        lbBtn(x + w - 100, y - 6, 100, 26, "CREATE", true,
              [this] { lobbyScreen_ = LobbyScreen::Create; });
        y += 34;
        // column header
        blockText("NAME", x + 8, y, 1.6f, {130, 135, 150, 255});
        blockText("MAP", x + 220, y, 1.6f, {130, 135, 150, 255});
        blockText("PLAYERS", x + w - 200, y, 1.6f, {130, 135, 150, 255});
        y += 20;
        const auto& games = mp_->games();
        if (games.empty())
            blockText("no games -- create one, or refresh", x + 8, y + 10, 1.8f, {150, 150, 160, 255});
        for (const auto& g : games) {
            SDL_FRect row{x, y, w, 30};
            bool hot = lbHot(row) && !g.running;
            SDL_SetRenderDrawColor(ren_, hot ? 46 : 30, hot ? 52 : 34, hot ? 72 : 44, 255);
            SDL_RenderFillRectF(ren_, &row);
            blockText(g.name, x + 8, y + 8, 1.8f, {225, 228, 236, 255});
            blockText(g.mapId, x + 220, y + 8, 1.6f, {180, 185, 195, 255});
            char pc[32]; std::snprintf(pc, sizeof pc, "%d/%d", g.players, g.capacity);
            blockText(pc, x + w - 200, y + 8, 1.8f, {200, 205, 215, 255});
            if (g.passworded)
                blockText("LOCK", x + w - (g.running ? 232 : 120), y + 8, 1.6f, {230, 200, 120, 255});
            if (g.running) {
                // A running game can't be joined, but it can be watched live.
                blockText("LIVE", x + w - 158, y + 8, 1.6f, {230, 140, 120, 255});
                lbBtn(x + w - 96, y + 3, 92, 24, "WATCH", true,
                      [this, id = g.id] { mp_->spectate(id, joinPass_); });
            } else {
                lobbyHots_.push_back({row, [this, id = g.id] { mp_->joinGame(id, joinPass_); }});
            }
            y += 34;
        }
        // password entry for locked games
        lbField(x, winH - 70.0f, 200, "PASSWORD (for locked games)", joinPass_, 3);
        lbBtn(winW - 120.0f, winH - 44.0f, 100, 28, "QUIT", true, [this] { mp_->disconnect(); });
    }

    void drawCreate(int winW, int winH) {
        (void)winH;
        float x = 80, y = 90;
        blockText("CREATE GAME", x, y, 2.2f, {200, 205, 220, 255}); y += 40;
        lbField(x, y, 260, "GAME NAME", createName_, 1); y += 46;
        lbField(x, y, 260, "PASSWORD (optional)", createPass_, 2); y += 46;
        blockText(std::string("MAP: ") + mpMapId_, x, y, 1.8f, {180, 185, 195, 255}); y += 30;
        lbBtn(x, y, 150, 26, createCrusades_ ? "CRUSADES: ON" : "CRUSADES: OFF", true,
              [this] { createCrusades_ = !createCrusades_; }); y += 34;
        lbBtn(x, y, 150, 26, createGods_ ? "GODS: ON" : "GODS: OFF", true,
              [this] { createGods_ = !createGods_; }); y += 44;
        lbBtn(x, y, 120, 30, "CREATE", !createName_.empty(), [this] {
            tak::net::GameOptions o; o.crusades = createCrusades_ ? 1 : 0; o.gods = createGods_ ? 1 : 0;
            mp_->createGame(createName_, createPass_, mpMapId_, o, mpCapacity());
            lobbyScreen_ = LobbyScreen::Browser;
        });
        lbBtn(x + 132, y, 120, 30, "CANCEL", true, [this] { lobbyScreen_ = LobbyScreen::Browser; });
    }

    void drawRoom(int winW, int winH) {
        const auto& room = mp_->room();
        float x = 40, y = 78;
        blockText(room.name, x, y, 2.2f, {210, 210, 220, 255});
        blockText(std::string("MAP  ") + room.mapId, x + winW - 320, y + 4, 1.8f, {180, 185, 195, 255});
        y += 34;
        bool host = (room.hostId == mp_->myClientId());
        // slot table
        const char* typeName[4] = {"OPEN", "HUMAN", "AI", "CLOSED"};
        for (int i = 0; i < tak::net::kMaxSlots; ++i) {
            const auto& s = room.slots[i];
            bool mine = (i == room.mySlot);
            SDL_FRect row{x, y, winW - 320.0f, 30};
            SDL_SetRenderDrawColor(ren_, mine ? 40 : 26, mine ? 48 : 30, mine ? 66 : 40, 255);
            SDL_RenderFillRectF(ren_, &row);
            char sn[8]; std::snprintf(sn, sizeof sn, "%d", i + 1);
            blockText(sn, x + 8, y + 8, 1.8f, {150, 155, 170, 255});
            // type (host may cycle open<->closed on empty slots)
            SDL_Color tcol = s.type == 1 ? SDL_Color{200, 230, 200, 255}
                           : s.type == 2 ? SDL_Color{230, 220, 150, 255}
                                         : SDL_Color{130, 135, 150, 255};
            blockText(typeName[s.type % 4], x + 34, y + 8, 1.6f, tcol);
            if (host && s.type != 1) {
                SDL_FRect tb{x + 34, y + 6, 60, 18};
                lobbyHots_.push_back({tb, [this, i, t = s.type] {
                    // cycle OPEN(0) <-> CLOSED(3) (AI is M4)
                    mp_->setSlot(i, t == 0 ? 3 : 0, mpRoom().slots[i].faction,
                                 mpRoom().slots[i].color, mpRoom().slots[i].team, 0); }});
            }
            if (s.type == 1) blockText(s.name, x + 100, y + 8, 1.8f, {225, 228, 236, 255});
            // faction / color / team / ready are editable on your own row.
            bool canEdit = mine;
            blockText(factionName(s.faction), x + 260, y + 8, 1.6f, {200, 205, 215, 255});
            if (canEdit) { SDL_FRect fb{x + 260, y + 6, 90, 18};
                lobbyHots_.push_back({fb, [this, i] { const auto& s2 = mpRoom().slots[i];
                    mp_->setSlot(i, 1, (s2.faction + 1) % 5, s2.color, s2.team, s2.ready); }}); }
            colorSwatch(x + 360, y + 5, 20, s.color, canEdit ? std::function<void()>([this, i] {
                const auto& s2 = mpRoom().slots[i];
                mp_->setSlot(i, 1, s2.faction, (s2.color + 1) % 10, s2.team, s2.ready); }) : nullptr);
            char tm[8]; std::snprintf(tm, sizeof tm, "T%d", s.team + 1);
            blockText(tm, x + 392, y + 8, 1.8f, {200, 205, 215, 255});
            if (canEdit) { SDL_FRect teb{x + 392, y + 6, 34, 18};
                lobbyHots_.push_back({teb, [this, i] { const auto& s2 = mpRoom().slots[i];
                    mp_->setSlot(i, 1, s2.faction, s2.color, uint8_t((s2.team + 1) % tak::net::kMaxSlots), s2.ready); }}); }
            if (s.type == 1) {
                SDL_Color rc = s.ready ? SDL_Color{130, 230, 140, 255} : SDL_Color{120, 125, 135, 255};
                blockText(s.ready ? "READY" : "NOT READY", x + 440, y + 8, 1.6f, rc);
            }
            // host kick button for other humans
            if (host && s.type == 1 && !mine) {
                lbBtn(x + row.w - 54, y + 3, 50, 22, "KICK", true, [this, i] { mp_->kick(i); });
            }
            y += 34;
        }
        y += 10;
        // controls
        bool iAmReady = room.mySlot >= 0 && room.slots[room.mySlot].ready;
        lbBtn(x, y, 130, 30, iAmReady ? "UNREADY" : "READY", room.mySlot >= 0, [this, iAmReady] {
            const auto& s = mpRoom().slots[mpRoom().mySlot];
            mp_->setSlot(mpRoom().mySlot, 1, s.faction, s.color, s.team, iAmReady ? 0 : 1); });
        // start (host): enabled when >=2 used slots and all humans ready and colors unique
        bool canStart = host && startValid(room);
        lbBtn(x + 142, y, 130, 30, "START", canStart, [this] { mp_->startGame(); },
              {70, 110, 70, 255});
        lbBtn(x + 284, y, 120, 30, "LEAVE", true, [this] {
            mp_->leaveGame(); lobbyScreen_ = LobbyScreen::Browser;
            mpReadied_ = false; mpStarted_ = false; });
        // chat panel on the right
        float chx = winW - 300.0f, chy = 78, chw = 280;
        SDL_SetRenderDrawColor(ren_, 22, 24, 32, 255);
        SDL_FRect cp{chx, chy, chw, winH - 150.0f}; SDL_RenderFillRectF(ren_, &cp);
        blockText("CHAT", chx + 8, chy + 6, 1.8f, {160, 165, 180, 255});
        float ly = chy + cp.h - 20;
        for (auto it = chatLog_.rbegin(); it != chatLog_.rend() && ly > chy + 26; ++it) {
            std::string line = it->first + ": " + it->second;
            if (line.size() > 40) line = line.substr(0, 40);
            blockText(line, chx + 8, ly, 1.4f, {200, 205, 215, 255});
            ly -= 16;
        }
        lbField(chx, winH - 66.0f, chw - 70, "SAY", chatDraft_, 4);
        lbBtn(chx + chw - 62, winH - 52.0f, 56, 24, "SEND", !chatDraft_.empty(), [this] {
            mp_->chat(chatDraft_); chatDraft_.clear(); });
    }

    static bool startValid(const tak::net::RoomView& room) {
        int used = 0; bool color[10] = {};
        for (int i = 0; i < tak::net::kMaxSlots; ++i) {
            const auto& s = room.slots[i];
            if (s.type != 1 && s.type != 2) continue;
            ++used;
            if (s.type == 1 && !s.ready) return false;
            if (s.color < 10) { if (color[s.color]) return false; color[s.color] = true; }
        }
        return used >= 2;
    }
    const tak::net::RoomView& mpRoom() const { return mp_->room(); }

    void lobbyInput(const SDL_Event& e, int winW, int winH) {
        (void)winW; (void)winH;
        if (e.type == SDL_MOUSEMOTION) { mouseX_ = float(e.motion.x); mouseY_ = float(e.motion.y); }
        else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            mouseX_ = float(e.button.x); mouseY_ = float(e.button.y);
            lbField_ = 0; SDL_StopTextInput();
            for (auto& [r, action] : lobbyHots_)
                if (mouseX_ >= r.x && mouseX_ <= r.x + r.w && mouseY_ >= r.y && mouseY_ <= r.y + r.h) {
                    action(); break;
                }
        } else if (e.type == SDL_TEXTINPUT && lbField_) {
            std::string* f = lbFieldBuf();
            if (f && f->size() < 24) *f += e.text.text;
        } else if (e.type == SDL_KEYDOWN && lbField_) {
            if (e.key.keysym.sym == SDLK_BACKSPACE) { std::string* f = lbFieldBuf(); if (f && !f->empty()) f->pop_back(); }
            else if (e.key.keysym.sym == SDLK_RETURN) {
                if (lbField_ == 4 && !chatDraft_.empty()) { mp_->chat(chatDraft_); chatDraft_.clear(); }
                lbField_ = 0; SDL_StopTextInput();
            } else if (e.key.keysym.sym == SDLK_ESCAPE) { lbField_ = 0; SDL_StopTextInput(); }
        }
    }
    std::string* lbFieldBuf() {
        switch (lbField_) {
            case 1: return &createName_; case 2: return &createPass_;
            case 3: return &joinPass_; case 4: return &chatDraft_;
            default: return nullptr;
        }
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

    static SDL_Color sideColor(const std::string& side) {
        std::string l = side;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        if (l == "ara") return {70, 130, 240, 255};
        if (l == "tar") return {205, 65, 60, 255};
        if (l == "ver") return {70, 185, 90, 255};
        if (l == "zon") return {235, 205, 55, 255};
        if (l == "cre") return {230, 145, 50, 255};
        return {210, 210, 210, 255};
    }

    // F4: per-faction live unit counts, top-left.
    // F7 diagnostic: tint elevated cells, and for each unit show its cell height,
    // computed lift, its RAW (unlifted) foot position (magenta dot) vs its LIFTED
    // foot position (cyan dot). Lets us see whether a unit that looks "on the wall"
    // is actually on a high heightmap cell or on flat ground beside painted relief.
    void drawHDebug() {
        const auto& m = mapView_.map();
        if (m.heights.empty()) return;
        float zm = mapView_.zoom();
        // Tint cells whose height is well above ground (candidate "wall" cells).
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        int cx0 = std::max(0, int(mapView_.offX()) / 16 - 1);
        int cz0 = std::max(0, int(mapView_.offY()) / 16 - 1);
        int cx1 = std::min(m.width, cx0 + int(1000 / zm / 16) + 3);
        int cz1 = std::min(m.height, cz0 + int(1000 / zm / 16) + 3);
        for (int cz = cz0; cz < cz1; ++cz)
            for (int cx = cx0; cx < cx1; ++cx) {
                int h = m.heights[size_t(cz) * m.width + cx];
                int d = h - (heightRef_ < 0 ? 0 : heightRef_);
                if (d < 8) continue;
                Uint8 a = Uint8(std::min(150, 30 + d));
                SDL_SetRenderDrawColor(ren_, 220, 40, 40, a);
                SDL_FRect r{(cx * 16.0f - mapView_.offX()) * zm,
                            (cz * 16.0f - mapView_.offY()) * zm, 16 * zm, 16 * zm};
                SDL_RenderFillRectF(ren_, &r);
            }
        char buf[64];
        for (const auto& u : world_.units()) {
            if (!u.alive() || !u.type) continue;
            if (!alliedToLocal(u.player) && !world_.cellVisible(u.x, u.z) && !noFog_) continue;
            float sx = (u.x - mapView_.offX()) * zm;
            float rawY = (u.z - mapView_.offY()) * zm;
            float lift = terrainLift(u.x, u.z);
            float liftX = terrainLiftX(u.x, u.z) * zm;
            float liftY = rawY - lift * zm;
            // raw foot (magenta) and lifted foot (cyan, incl. sideways tilt)
            SDL_SetRenderDrawColor(ren_, 255, 0, 255, 255);
            SDL_FRect rr{sx - 2, rawY - 2, 4, 4};
            SDL_RenderFillRectF(ren_, &rr);
            SDL_SetRenderDrawColor(ren_, 0, 255, 255, 255);
            SDL_FRect lr{sx - liftX - 2, liftY - 2, 4, 4};
            SDL_RenderFillRectF(ren_, &lr);
            SDL_SetRenderDrawColor(ren_, 255, 0, 255, 200);
            SDL_RenderDrawLineF(ren_, sx, rawY, sx, liftY);
            int cx = std::clamp(int(u.x) / 16, 0, m.width - 1);
            int cz = std::clamp(int(u.z) / 16, 0, m.height - 1);
            int h = m.heights[size_t(cz) * m.width + cx];
            std::snprintf(buf, sizeof buf, "h%d L%d", h, int(lift + 0.5f));
            blockText(buf, sx + 5, liftY - 30, 1.4f, SDL_Color{255, 255, 120, 255});
            // Computed occlusion clip line (green): units are clipped above this.
            float occ = wallOcclusionY(u.x, u.z);
            if (occ < rawY) {
                SDL_SetRenderDrawColor(ren_, 40, 255, 40, 255);
                SDL_RenderDrawLineF(ren_, sx - 30, occ, sx + 30, occ);
            }
        }
    }

    void drawUnitCounts(int winW) {
        int cnt[tak::sim::kMaxPlayers] = {};
        std::string sd[tak::sim::kMaxPlayers];
        int np = world_.numPlayers();
        for (const auto& u : world_.units()) {
            if (!u.alive() || !u.type) continue;
            int t = u.player;
            if (t < 0 || t >= np) continue;
            ++cnt[t];
            if (sd[t].empty()) sd[t] = u.type->side;
        }
        // In a net game (or replay) this is a full scoreboard: every player is
        // listed with their name, team, and defeat status. Offline it stays the
        // compact "who has units" readout.
        bool board = mp_ || replayMode_;
        // Are there real alliances (a team with 2+ members)? If so, show a team tag.
        bool teams = false;
        { int tc[tak::sim::kMaxPlayers] = {};
          for (int t = 0; t < np; ++t) tc[world_.player(t).team % tak::sim::kMaxPlayers]++;
          for (int t = 0; t < tak::sim::kMaxPlayers; ++t) if (tc[t] > 1) teams = true; }
        int rows = 0;
        for (int t = 0; t < np; ++t) if (board || cnt[t] > 0) ++rows;
        const float px = 2.4f, lh = 7 * px + 9, x = 12;
        float y = 12;
        const float nameX = x + (teams ? 40 : 0);
        const float panelW = (board || teams) ? 340.0f : 270.0f;
        const float colUnits = x + panelW - 128, colKills = x + panelW - 50;
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 175);
        SDL_FRect bg{x - 7, y - 7, panelW, (rows + 1) * lh + 8};
        SDL_RenderFillRectF(ren_, &bg);
        char buf[64];
        // FPS on the left; UNITS / KILLS column headers on the right.
        std::snprintf(buf, sizeof buf, "FPS %d", int(fps_ + 0.5f));
        blockText(buf, x, y, px, SDL_Color{190, 190, 195, 255});
        blockText("UNITS", colUnits, y + 3, 1.8f, SDL_Color{150, 150, 155, 255});
        blockText("KILLS", colKills, y + 3, 1.8f, SDL_Color{150, 150, 155, 255});
        y += lh;
        for (int t = 0; t < np; ++t) {
            if (!board && cnt[t] == 0) continue;
            bool dead = world_.player(t).defeated;
            SDL_Color c = playerColor(t);
            if (dead) { c.r /= 2; c.g /= 2; c.b /= 2; }   // dim a knocked-out player
            if (teams) {   // small team tag, e.g. "T2"
                std::snprintf(buf, sizeof buf, "T%d", world_.player(t).team + 1);
                blockText(buf, x, y, 1.8f, dead ? SDL_Color{110, 110, 115, 255}
                                                : SDL_Color{170, 175, 185, 255});
            }
            // Label: the player's name in a net game, else the faction. A faction
            // can repeat with >2 players, so the offline label carries a P# prefix.
            std::string s;
            if (mp_ && !playerName_[t & 7].empty()) {
                s = playerName_[t & 7];
                if (s.size() > 12) s = s.substr(0, 12);
            } else {
                s = sd[t].empty() ? std::string("--") : sd[t];
                std::transform(s.begin(), s.end(), s.begin(), ::toupper);
                if (np > 2) s = "P" + std::to_string(t + 1) + " " + s;
            }
            blockText(s, nameX, y, px, c);
            std::snprintf(buf, sizeof buf, "%d", cnt[t]);
            blockText(buf, colUnits, y, px, c);
            std::snprintf(buf, sizeof buf, "%d", world_.player(t).kills);
            blockText(buf, colKills, y, px, c);
            if (dead) blockText("OUT", nameX + blockWidth(s, px) + 8, y, 1.7f,
                                SDL_Color{210, 90, 70, 255});
            y += lh;
        }
        (void)winW;
    }

    // In-game player-colour picker (F6): a row of swatches; click one to recolour
    // your units, HUD and minimap. Swatch rects are cached for click hit-testing.
    void drawColorPicker(int winW, int winH) {
        colorRects_.clear();
        const float sw = 30, gap = 6, pad = 10;
        float rowW = 10 * sw + 9 * gap;
        float x0 = (winW - rowW) / 2.0f;
        float y0 = float(winH) - kBarH - 52;
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 190);
        SDL_FRect bg{x0 - pad, y0 - 22, rowW + 2 * pad, sw + 34};
        SDL_RenderFillRectF(ren_, &bg);
        blockText("PLAYER COLOR", x0, y0 - 18, 1.8f, SDL_Color{210, 210, 215, 255});
        for (int i = 0; i < 10; ++i) {
            SDL_FRect r{x0 + i * (sw + gap), y0, sw, sw};
            SDL_Color c = playerColors_[i];
            SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, 255);
            SDL_RenderFillRectF(ren_, &r);
            // Frame; the current selection gets a bright, thick border.
            bool cur = colorSlot_[localPlayer_] == i;
            SDL_SetRenderDrawColor(ren_, cur ? 255 : 20, cur ? 255 : 18,
                                   cur ? 255 : 16, 255);
            SDL_RenderDrawRectF(ren_, &r);
            if (cur) {
                SDL_FRect r2{r.x - 2, r.y - 2, r.w + 4, r.h + 4};
                SDL_RenderDrawRectF(ren_, &r2);
            }
            colorRects_.push_back({r, i});
        }
    }
    // Handle a click on the colour picker; returns true if it consumed the click.
    bool colorPickerClick(float mx, float my) {
        if (!showColorPicker_) return false;
        for (const auto& [r, slot] : colorRects_)
            if (mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h) {
                colorSlot_[localPlayer_] = slot;
                return true;
            }
        return false;
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
        // A solid faction-coloured panel with a dark frame — black text on top.
        auto shade = [&](float x, float w) {
            SDL_FRect z{x, bar.y + 4, w, kBarH - 8.0f};
            SDL_SetRenderDrawColor(ren_, fc.r, fc.g, fc.b, 255);
            SDL_RenderFillRectF(ren_, &z);
            SDL_SetRenderDrawColor(ren_, 20, 18, 16, 255);
            SDL_RenderDrawRectF(ren_, &z);
        };

        // Bottom-LEFT: portrait + stats for the selected unit.
        if (!selection_.empty() && statFont_.ok()) {
            const auto* u = world_.unit(selection_.front());
            if (u && u->alive() && u->type) {
                float px = 8;
                shade(px, 288);
                SDL_Texture* ic = iconFor(u->type->id);
                if (ic) {
                    SDL_FRect pr{px + 4, bar.y + 8, 56, kBarH - 20.0f};
                    SDL_RenderCopyF(ren_, ic, nullptr, &pr);
                    SDL_SetRenderDrawColor(ren_, 20, 18, 16, 255);
                    SDL_RenderDrawRectF(ren_, &pr);
                }
                float tx = px + 70;
                SDL_Color blk{0, 0, 0, 255};
                blockText(u->type->name, tx, bar.y + 9, 2.6f, blk);
                std::snprintf(buf, sizeof buf, "HP %d/%d", int(u->hp),
                              int(u->type->maxHp));
                blockText(buf, tx, bar.y + 32, 2.3f, blk);
                if (selection_.size() > 1) {
                    std::snprintf(buf, sizeof buf, "+%zu MORE",
                                  selection_.size() - 1);
                    blockText(buf, tx, bar.y + 52, 1.8f, blk);
                }
            }
        }

        // Bottom-CENTER: clickable build icons for the selected builder.
        iconRects_.clear();
        const auto* b = selectedBuilder();
        if (b) {
            const auto& menu = registry_.buildable(b->type->id);
            // Show the WHOLE menu at full size -- never truncate, never shrink.
            // Some builders (the Zhon Beast Tamer under the Crusades balance) list
            // 13 buildables, and the higher-tier builder sorts last, so a fixed cap
            // would hide it and make that tier unreachable. The window has a minimum
            // width (from registry_.maxBuildMenu(), set in main) so the full-size row
            // always fits the map viewport -- centred here over the map, clear of
            // the right-hand panel.
            int n = int(menu.size());
            float rowW = n > 0 ? (n - 1) * 66.0f + 60.0f : 0;
            float x = (float(mapViewW(winW)) - rowW) * 0.5f;
            for (int i = 0; i < n; ++i) {
                const auto* bt = registry_.find(menu[size_t(i)]);
                if (!bt) continue;
                SDL_FRect r{x, bar.y + 6, 60, kBarH - 16.0f};
                SDL_SetRenderDrawColor(ren_, 20, 18, 14, 235);
                SDL_FRect rb{r.x - 1, r.y - 1, r.w + 2, r.h + 2};
                SDL_RenderFillRectF(ren_, &rb);
                SDL_Texture* ic = iconFor(bt->id);
                if (!ic) ic = modelIconTex(bt->id, colorSlot_[localPlayer_ & 7], bt->canMove);
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
                // Infinite-build marker: bright +++ over the repeating unit's icon.
                if (b->repeatType == bt) {
                    float px = 2.8f;
                    float pw = blockWidth("+++", px);
                    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 180);
                    SDL_FRect pb{r.x + (r.w - pw) / 2 - 3, r.y + 4, pw + 6, 22};
                    SDL_RenderFillRectF(ren_, &pb);
                    blockText("+++", r.x + (r.w - pw) / 2, r.y + 7, px, {120, 255, 130, 255});
                }
                // Queued-count badge (bottom-right of the icon): how many are queued.
                if (int qc = world_.queuedCount(b->id, bt)) {
                    char num[8];
                    std::snprintf(num, sizeof num, "%d", qc);
                    float px = 2.2f, nw = blockWidth(num, px);
                    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 205);
                    SDL_FRect nb{r.x + r.w - nw - 7, r.y + r.h - 21, nw + 7, 20};
                    SDL_RenderFillRectF(ren_, &nb);
                    blockText(num, r.x + r.w - nw - 4, r.y + r.h - 18, px, {255, 235, 140, 255});
                }
                if (hot) {
                    char tip[80];
                    std::snprintf(tip, sizeof tip, "%s  %d MANA", bt->name.c_str(),
                                  int(bt->buildCost));
                    float px = 2.0f;
                    float tw = blockWidth(tip, px);
                    float tipx = std::clamp(r.x + 30 - tw / 2, 6.0f, winW - tw - 6);
                    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 210);
                    SDL_FRect tb{tipx - 6, bar.y - 34, tw + 12, 26};
                    SDL_RenderFillRectF(ren_, &tb);
                    blockText(tip, tipx, bar.y - 30, px, {255, 240, 190, 255});
                }
                iconRects_.push_back({r, bt});
                x += 66;
            }
            if (!b->buildQueue.empty()) {
                char q[64];
                std::snprintf(q, sizeof q, "TRAINING %s (%zu)",
                              b->buildQueue.front()->name.c_str(),
                              b->buildQueue.size());
                float px = 1.8f;
                float qw = blockWidth(q, px);
                blockText(q, (float(winW) - qw) * 0.5f, bar.y - 30, px,
                          {160, 210, 255, 255});
            }
        }

        // Bottom-RIGHT: mana.
        {
            auto& tm = world_.player(localPlayer_);
            float manaX = float(winW) - 192;
            shade(manaX - 8, 200);
            SDL_Color blk{0, 0, 0, 255};
            blockText("MANA", manaX, bar.y + 9, 2.0f, blk);
            std::snprintf(buf, sizeof buf, "%d/%d", int(tm.mana),
                          int(std::max(tm.storage, 100.0f)));
            blockText(buf, manaX, bar.y + 30, 2.3f, blk);
            std::snprintf(buf, sizeof buf, "+%d/SEC", int(tm.income));
            blockText(buf, manaX, bar.y + 52, 1.8f, blk);
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
        // Lift the fog to sit on the terrain relief, exactly like units do, so the
        // cleared area follows a unit up a hill instead of staying at ground level.
        // Draw it as a grid of quads lifted per-cell (the terrain art itself is a
        // flat mosaic, but units are drawn lifted onto it -- the fog must match).
        heightAbove(0.0f, 0.0f);   // ensure heightRef_/scales are initialised
        float maxLy = float(255 - std::max(heightRef_, 0)) * kHeightScale_;
        float maxLx = float(255 - std::max(heightRef_, 0)) * kHeightScaleX_;
        float ox = mapView_.offX(), oy = mapView_.offY();
        int gx0 = std::clamp(int(ox / 16) - 1, 0, w);
        int gx1 = std::clamp(int((ox + winW_ / zm + maxLx) / 16) + 2, 0, w);
        int gz0 = std::clamp(int(oy / 16) - 1, 0, h);
        int gz1 = std::clamp(int((oy + winH_ / zm + maxLy) / 16) + 2, 0, h);
        auto vert = [&](int gx, int gz) {
            float wx = float(gx) * 16.0f, wz = float(gz) * 16.0f;
            SDL_Vertex v;
            v.position = {(wx - ox) * zm - terrainLiftX(wx, wz) * zm,
                          (wz - oy) * zm - terrainLift(wx, wz) * zm};
            v.tex_coord = {float(gx) / float(w), float(gz) / float(h)};
            v.color = {255, 255, 255, 255};
            return v;
        };
        fogVerts_.clear();
        for (int gz = gz0; gz < gz1; ++gz)
            for (int gx = gx0; gx < gx1; ++gx) {
                SDL_Vertex a = vert(gx, gz), b = vert(gx + 1, gz),
                           c = vert(gx + 1, gz + 1), d = vert(gx, gz + 1);
                fogVerts_.push_back(a); fogVerts_.push_back(b); fogVerts_.push_back(c);
                fogVerts_.push_back(a); fogVerts_.push_back(c); fogVerts_.push_back(d);
            }
        if (!fogVerts_.empty())
            SDL_RenderGeometry(ren_, fogTex_, fogVerts_.data(), int(fogVerts_.size()),
                               nullptr, 0);
    }
    std::vector<SDL_Vertex> fogVerts_;

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
        // Match the height-aware placement: pick the cell drawn under the cursor and
        // draw the footprint at its lifted screen position, so the preview sits where
        // the unit will actually land on elevated ground.
        float wx, wz;
        pickWorld(mouseX_, mouseY_, wx, wz);
        bool ok = world_.canPlace(placing_, wx, wz);
        bool lift = !isStructure(placing_);
        float hw = float(placing_->footX) * 8 * zm, hh = float(placing_->footZ) * 8 * zm;
        float cxp = (wx - mapView_.offX()) * zm - (lift ? terrainLiftX(wx, wz) : 0.0f) * zm;
        float czp = (wz - mapView_.offY()) * zm - (lift ? terrainLift(wx, wz) : 0.0f) * zm;
        SDL_FRect r{cxp - hw, czp - hh, hw * 2, hh * 2};
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren_, ok ? 90 : 230, ok ? 220 : 60, 70, 90);
        SDL_RenderFillRectF(ren_, &r);
        SDL_SetRenderDrawColor(ren_, ok ? 120 : 255, ok ? 255 : 80, 90, 220);
        SDL_RenderDrawRectF(ren_, &r);
        {
            float px = 1.8f;
            float tw = blockWidth(placing_->name, px);
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 190);
            SDL_FRect tb{r.x - 4, r.y - 22, tw + 8, 20};
            SDL_RenderFillRectF(ren_, &tb);
            blockText(placing_->name, r.x, r.y - 18, px, {230, 230, 200, 255});
        }
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
                int player = std::clamp(a[0] - 1, 0, 3);
                float wx = float(a[1]) * 16 + 8, wz = float(a[2]) * 16 + 8;
                int best = 0;
                float bestD = 1e18f;
                for (auto& u : world_.units()) {
                    if (!u.alive() || u.player != player) continue;
                    float dx = u.x - wx, dz = u.z - wz;
                    if (dx * dx + dz * dz < bestD) { bestD = dx * dx + dz * dz; best = u.id; }
                }
                return best;
            }
            case 4: {   // HEURISTIC: spawn a reinforcement for player a[0]
                if (a.size() < 3) return 0;
                int player = std::clamp(a[0] - 1, 0, 3);
                auto& pool = reinfPool_[player];
                if (pool.empty()) return 0;
                const std::string& type = pool[size_t(reinfIdx_++) % pool.size()];
                float wx = float(a[1]) * 16 + 8, wz = float(a[2]) * 16 + 8;
                int id = spawn(type, wx + float(reinfIdx_ % 3) * 18,
                               wz + float(reinfIdx_ % 2) * 18, 3.14159f, player);
                if (id >= 0 && player == 0 && hudFont_.ok()) notice_ = "REINFORCEMENTS!";
                if (trace_) std::printf("SPAWN4 %s player%d at %d,%d -> id %d\n",
                                        type.c_str(), player, a[1], a[2], id);
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
                    if (o.alive() && o.player == u->player && o.id != u->id && o.type &&
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
            sounds_.playWorld(*wav, u->x, u->z);
    }

    // --- GAF/TAF impact effects (gamedata/explosions -> data/anims/*.taf) ------
    struct EFrame { SDL_Texture* tex = nullptr; int w = 0, h = 0, ax = 0, ay = 0; };
    struct EffectAnim { std::vector<EFrame> frames; };
    std::map<std::string, std::vector<std::string>> explosionClasses_;  // class -> anim names
    std::map<std::string, EffectAnim> effectAnims_;                     // anim name -> frames
    bool explosionsLoaded_ = false;
    struct EffectInst {
        const EffectAnim* anim = nullptr;
        float x = 0, z = 0, age = 0;
        float delay = 0;   // seconds before it starts playing
        float dur = 0;     // seconds for one playthrough (0 => use kEffectFps)
        int loops = 1;     // how many times to repeat (ground fire loops)
        float alt = 0;     // extra screen lift (impact on an airborne target)
    };
    std::vector<EffectInst> effects_;
    static constexpr float kEffectFps = 20.0f;

    // Per-loop playback length of an effect instance, in seconds.
    static float effLoopLen(const EffectInst& e) {
        return e.dur > 0 ? e.dur : float(e.anim->frames.size()) / kEffectFps;
    }
    // Play a named effect anim at (x,z), optionally delayed / stretched / looped.
    // `alt` lifts it on screen (impact on an airborne target).
    void spawnEffectAnim(const std::string& anim, float x, float z,
                         float delay = 0, float dur = 0, int loops = 1, float alt = 0) {
        const EffectAnim* ea = effectFor(anim);
        if (ea) effects_.push_back({ea, x, z, 0.0f, delay, dur, loops, alt});
    }

    void loadExplosionClasses() {
        if (explosionsLoaded_) return;
        explosionsLoaded_ = true;
        try {
            auto root = tak::tdf::parse(dataRoot_ +
                                        "/gamedata/explosions/explosions.tdf");
            for (const auto& cls : root.childOrder) {
                const auto& node = root.children.at(cls);
                auto& list = explosionClasses_[cls];   // cls is already lowercased
                for (const auto& vn : node.childOrder) {
                    const auto& v = node.children.at(vn);
                    std::string a = v.valueOr("anim", v.valueOr("gaf", ""));
                    if (!a.empty()) {
                        std::transform(a.begin(), a.end(), a.begin(), ::tolower);
                        list.push_back(a);
                    }
                }
            }
        } catch (const std::exception&) {}
    }
    // Load a named effect animation from its TAF/GAF (truecolor _4444 preferred).
    const EffectAnim* effectFor(const std::string& animName) {
        auto it = effectAnims_.find(animName);
        if (it != effectAnims_.end())
            return it->second.frames.empty() ? nullptr : &it->second;
        EffectAnim ea;
        const auto* pal = featurePalette("aramon");   // ignored for truecolor TAF
        for (const std::string suf : {"_4444.taf", "_1555.taf", ".taf", ".gaf"}) {
            if (!ea.frames.empty()) break;
            try {
                auto seqs = tak::gaf::load(dataRoot_ + "/anims/" + animName + suf,
                                           pal ? *pal : tak::gaf::Palette{});
                const tak::gaf::Sequence* seq = nullptr;
                for (auto& s : seqs) {
                    if (s.frames.empty()) continue;
                    if (!seq) seq = &s;
                    std::string sn = s.name;
                    std::transform(sn.begin(), sn.end(), sn.begin(), ::tolower);
                    if (sn == animName) { seq = &s; break; }
                }
                if (!seq) continue;
                for (auto& fr : seq->frames) {
                    if (fr.width == 0 || fr.height == 0) continue;
                    SDL_Texture* t = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                                       SDL_TEXTUREACCESS_STATIC,
                                                       fr.width, fr.height);
                    SDL_UpdateTexture(t, nullptr, fr.rgba.data(), fr.width * 4);
                    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);   // fiery glow
                    ea.frames.push_back({t, fr.width, fr.height, fr.xoff, fr.yoff});
                }
            } catch (const std::exception&) {}
        }
        auto& stored = effectAnims_[animName];
        stored = std::move(ea);
        return stored.frames.empty() ? nullptr : &stored;
    }
    // Play the named explosion class (a random variant) at (x,z). Returns false
    // if the class/art is unavailable (caller then falls back to particles).
    bool spawnEffect(const std::string& cls, float x, float z, float alt = 0) {
        if (cls.empty()) return false;
        loadExplosionClasses();
        auto it = explosionClasses_.find(cls);
        if (it == explosionClasses_.end() || it->second.empty()) return false;
        const std::string& anim = it->second[salt_++ % it->second.size()];
        const EffectAnim* ea = effectFor(anim);
        if (!ea) return false;
        effects_.push_back({ea, x, z, 0.0f, 0.0f, 0.0f, 1, alt});
        static const bool kLog = getenv("TAK_FXLOG") != nullptr;
        if (kLog) std::fprintf(stderr, "t=%.2f effect '%s' anim '%s' (%zu frames)\n",
                               animClock_, cls.c_str(), anim.c_str(), ea->frames.size());
        return true;
    }
    void updateEffects(float dt) {
        for (auto& e : effects_) e.age += dt;
        std::erase_if(effects_, [](const EffectInst& e) {
            if (!e.anim || e.anim->frames.empty()) return true;
            float local = e.age - e.delay;
            return local >= effLoopLen(e) * float(std::max(e.loops, 1));
        });
    }
    // A shockwave ring: `sprites` copies of an effect anim arranged around a
    // circle that expands from the centre to maxR over `dur` (TAK radiusart).
    struct RingFx {
        const EffectAnim* anim = nullptr;
        float x = 0, z = 0, age = 0, delay = 0, dur = 1.2f, maxR = 120;
        int sprites = 24;
    };
    // Camera shake (weapon shakemagnitude/shakeduration on heavy impacts).
    float shakeTime_ = 0, shakeDur_ = 0, shakeMag_ = 0;
    void triggerShake(float mag, float dur) {
        if (mag <= 0 || dur <= 0) return;
        // Let a stronger/longer quake override a fading one.
        if (mag * dur >= shakeMag_ * shakeTime_) {
            shakeMag_ = mag; shakeDur_ = dur; shakeTime_ = dur;
        }
    }

    std::vector<RingFx> rings_;
    void spawnRing(const std::string& anim, float x, float z, float delay,
                   float dur, int sprites, float maxR) {
        const EffectAnim* ea = effectFor(anim);
        if (ea) rings_.push_back({ea, x, z, 0.0f, delay, dur, maxR,
                                  std::clamp(sprites, 6, 48)});
    }
    void updateRings(float dt) {
        for (auto& r : rings_) r.age += dt;
        std::erase_if(rings_, [](const RingFx& r) { return r.age - r.delay >= r.dur; });
    }
    void drawRings() {
        float zm = mapView_.zoom();
        for (const auto& r : rings_) {
            float local = r.age - r.delay;
            if (local < 0 || !r.anim || r.anim->frames.empty()) continue;
            if (!world_.cellVisible(r.x, r.z)) continue;
            float t = std::clamp(local / std::max(r.dur, 1e-3f), 0.0f, 1.0f);
            float radius = r.maxR * t;
            int nf = int(r.anim->frames.size());
            const EFrame& f = r.anim->frames[size_t(std::clamp(int(t * nf), 0, nf - 1))];
            for (int k = 0; k < r.sprites; ++k) {
                float a = 6.2831853f * float(k) / float(r.sprites);
                float wx = r.x + std::cos(a) * radius, wz = r.z + std::sin(a) * radius;
                float sx = (wx - mapView_.offX()) * zm - f.ax * zm - terrainLiftX(r.x, r.z) * zm;
                float sy = (wz - mapView_.offY()) * zm - f.ay * zm - terrainLift(r.x, r.z) * zm;
                SDL_FRect dst{sx, sy, f.w * zm, f.h * zm};
                SDL_RenderCopyF(ren_, f.tex, nullptr, &dst);
            }
        }
    }

    void drawEffects() {
        drawRings();
        float zm = mapView_.zoom();
        for (const auto& e : effects_) {
            if (!e.anim || e.anim->frames.empty()) continue;
            float local = e.age - e.delay;
            if (local < 0) continue;                // still waiting to start
            if (!world_.cellVisible(e.x, e.z)) continue;
            float per = effLoopLen(e);
            float within = local - std::floor(local / per) * per;   // into this loop
            int nf = int(e.anim->frames.size());
            int fi = std::clamp(int(within / per * float(nf)), 0, nf - 1);
            const EFrame& f = e.anim->frames[size_t(fi)];
            float sx = (e.x - mapView_.offX()) * zm - f.ax * zm - terrainLiftX(e.x, e.z) * zm;
            float sy = (e.z - mapView_.offY()) * zm - f.ay * zm - terrainLift(e.x, e.z) * zm
                       - e.alt * zm;
            SDL_FRect dst{sx, sy, f.w * zm, f.h * zm};
            SDL_RenderCopyF(ren_, f.tex, nullptr, &dst);
        }
    }

    // Procedural particle for impact explosions, flame, and blood spray.
    struct Particle {
        float x = 0, z = 0, vx = 0, vz = 0, alt = 0, valt = 0;
        float life = 0, maxLife = 1, size = 3;
        Uint8 r = 255, g = 200, b = 80;
        int kind = 0;   // 0 spark/blood (gravity), 1 smoke (rises, fades)
    };
    std::vector<Particle> particles_;
    // Spawn a burst of `n` particles at (x,z) with a colour and speed spread.
    void spawnBurst(float x, float z, int n, Uint8 r, Uint8 g, Uint8 b,
                    float spread, float sizeMax, int kind, float baseAlt = 0) {
        for (int i = 0; i < n; ++i) {
            Particle p;
            p.x = x; p.z = z;
            float ang = float(salt_++ % 628) / 100.0f;
            float sp = spread * (0.3f + float(salt_++ % 100) / 100.0f);
            p.vx = std::sin(ang) * sp;
            p.vz = std::cos(ang) * sp;
            p.alt = 4 + baseAlt;
            p.valt = kind == 1 ? 18.0f : (30.0f + float(salt_++ % 40));
            p.maxLife = p.life = 0.35f + float(salt_++ % 50) / 100.0f;
            p.size = 1.5f + float(salt_++ % 100) / 100.0f * sizeMax;
            p.r = r; p.g = g; p.b = b; p.kind = kind;
            particles_.push_back(p);
        }
    }
    // An impact effect scaled to the weapon: fire/lightning tinted, aoe-sized.
    void spawnImpact(const tak::sim::Weapon& w, float x, float z, float baseAlt = 0) {
        using Fx = tak::sim::WeaponFx;
        float sc = 1.0f + std::min(w.aoe, 200.0f) / 40.0f;
        int n = int(6 + std::min(w.aoe, 200.0f) / 6);
        if (w.fx == Fx::Fire) {
            spawnBurst(x, z, n, 240, 130, 40, 34 * sc, 2.4f * sc, 0, baseAlt);
            spawnBurst(x, z, n / 2, 90, 80, 80, 20 * sc, 3.0f * sc, 1, baseAlt);   // smoke
        } else if (w.fx == Fx::Lightning) {
            spawnBurst(x, z, n, 200, 225, 255, 40 * sc, 2.0f * sc, 0, baseAlt);
        } else {
            spawnBurst(x, z, n, 210, 200, 170, 26 * sc, 2.0f * sc, 0, baseAlt);   // dust
            spawnBurst(x, z, n / 3, 110, 100, 90, 16 * sc, 2.6f * sc, 1, baseAlt);
        }
    }
    void updateParticles(float dt) {
        static const bool kLog = getenv("TAK_FXLOG") != nullptr;
        if (kLog && !particles_.empty()) {
            static size_t peak = 0;
            if (particles_.size() > peak) {
                peak = particles_.size();
                std::fprintf(stderr, "particles: %zu live (peak)\n", peak);
            }
        }
        for (auto& p : particles_) {
            p.life -= dt;
            p.x += p.vx * dt; p.z += p.vz * dt;
            p.alt += p.valt * dt;
            p.valt -= (p.kind == 1 ? 6.0f : 90.0f) * dt;   // smoke floats, sparks fall
            p.vx *= 0.92f; p.vz *= 0.92f;
        }
        std::erase_if(particles_, [](const Particle& p) { return p.life <= 0; });
    }
    void drawParticles() {
        float zm = mapView_.zoom();
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        for (const auto& p : particles_) {
            if (!world_.cellVisible(p.x, p.z)) continue;
            float t = std::clamp(p.life / std::max(p.maxLife, 1e-3f), 0.0f, 1.0f);
            float sx = (p.x - mapView_.offX()) * zm - terrainLiftX(p.x, p.z) * zm;
            float sy = (p.z - mapView_.offY()) * zm - p.alt * zm - terrainLift(p.x, p.z) * zm;
            float r = p.size * zm * (p.kind == 1 ? (1.4f - t) : t);
            Uint8 a = Uint8(std::clamp(t * 255.0f, 0.0f, 255.0f));
            SDL_SetRenderDrawColor(ren_, p.r, p.g, p.b, a);
            SDL_FRect rc{sx - r, sy - r, 2 * r, 2 * r};
            SDL_RenderFillRectF(ren_, &rc);
        }
    }

    SoundBank sounds_;
    ThreadPool pool_;                       // for parallel per-unit VM ticks
    std::vector<tak::cob::Vm*> vmTick_;     // scratch list for the parallel pass
    SoundClasses soundClasses_;
    uint32_t salt_ = 0;
    int outcome_ = 0;   // 0 = playing, 1 = victory, -1 = defeat
    bool sawTeam_[tak::sim::kMaxPlayers] = {};   // teams that have ever fielded a unit
    bool demoAi_ = false;
    bool aiEnabled_ = true;
    // Dev-only N-player free-for-all / teams harness (TAK_FFA=N[,teams]); the
    // real lobby (multiplayer M3) replaces it. When >0, an AI Controller drives
    // every player, not just player 1.
    int ffaPlayers_ = 0;
    bool amphib_ = false;
    int amphibPhase_ = 0, amphibSquad_ = 0, transportId_ = -1;
    float amphibLandX_ = 0, amphibLandZ_ = 0, amphibSeaX_ = 0, amphibSeaZ_ = 0;
    Font hudFont_, bigFont_, statFont_;

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

// A parsed .takrep: enough to rebuild the world and replay it.
struct ReplayFile {
    std::string mapId;
    bool crusades = false;
    tak::sim::MatchConfig cfg;
    std::vector<tak::net::Bundle> bundles;
};

// Load a .takrep (header + tick bundles). Returns false on a malformed file.
static bool loadReplayFile(const std::string& path, ReplayFile& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> d(size_t(n < 0 ? 0 : n));
    if (!d.empty() && std::fread(d.data(), 1, d.size(), f) != d.size()) { std::fclose(f); return false; }
    std::fclose(f);
    if (d.size() < 4 || d[0] != 'T' || d[1] != 'A' || d[2] != 'K' || d[3] != 'R') return false;
    tak::net::Reader r(d.data() + 4, d.size() - 4);
    r.u32();                       // format version
    r.u32();                       // protocol version
    out.mapId = r.str();
    uint8_t crusades = r.u8(); uint8_t gods = r.u8(); r.u8();
    r.u32();                       // seed (setupMatch derives its own timing)
    uint8_t nslots = r.u8();
    out.crusades = crusades != 0;
    out.cfg.gods = gods != 0;
    out.cfg.slots.resize(nslots);
    int maxUsed = 0;
    for (int i = 0; i < nslots; ++i) {
        uint8_t type = r.u8(), faction = r.u8(); r.u8(); uint8_t team = r.u8();
        bool used = (type == 1 || type == 2);
        out.cfg.slots[size_t(i)] = {used, faction % 5, team};
        if (used) maxUsed = i;
    }
    // The game used setPlayerCount(maxUsedSlot+1); match it exactly (empty trailing
    // players would otherwise enter the state hash and diverge from the recording).
    out.cfg.slots.resize(size_t(maxUsed + 1));
    uint32_t nticks = r.u32();
    for (uint32_t t = 0; t < nticks && r.ok; ++t) {
        uint32_t len = r.u32();
        if (!r.avail(len)) return false;
        tak::net::Reader br(r.p, len);
        r.p += len;
        br.u32();                  // tick index (implicit = t)
        tak::net::Bundle bd;
        uint32_t nc = br.u32();
        for (uint32_t i = 0; i < nc && br.ok; ++i) bd.cmds.push_back(br.cmd());
        uint32_t ne = br.u32();
        for (uint32_t i = 0; i < ne && br.ok; ++i) {
            tak::net::Event e; e.kind = tak::net::Event::Kind(br.u8()); e.player = br.u8();
            bd.events.push_back(e);
        }
        out.bundles.push_back(std::move(bd));
    }
    return true;
}

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
    std::string serverHost, playerName;
    int serverPort = 7677, mpHeadless = 0;
    int hostPort = 0, joinPort = 0, winW = kWinW, winH = kWinH, maxFps = 60;
    int playerColor = -1, aiColor = -1;   // --color / --aicolor slot overrides
    float startTime = 0, followZoom = 0, marchX = 0, marchZ = 0;
    bool demo = false, doMarch = false, trace = false, testbuild = false,
         scenario = false, navy = false, amphib = false, missionFlag = false,
         misstest = false, nofog = false, doLook = false, creon = false,
         hilltest = false, keytest = false, guardtest = false, selonly = false,
         lodetest = false;
    std::string lodeUnitName;
    bool firetest = false, facetest = false, soundtest = false, noVsync = false;
    bool crusades = false;
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
        else if (a == "--color" && i + 1 < argc) playerColor = std::atoi(argv[++i]);
        else if (a == "--aicolor" && i + 1 < argc) aiColor = std::atoi(argv[++i]);
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
        else if (a == "--novsync") noVsync = true;
        else if (a == "--crusades") crusades = true;


        else if (a == "--lodeunit" && i + 1 < argc) lodeUnitName = argv[++i];
        else if (a == "--selonly") selonly = true;

        else if (a == "--server" && i + 1 < argc) serverHost = argv[++i];
        else if (a == "--serverport" && i + 1 < argc) serverPort = std::atoi(argv[++i]);
        else if (a == "--name" && i + 1 < argc) playerName = argv[++i];
        // Headless multiplayer test drivers (auto-play through the server).
        else if (a == "--mphost") mpHeadless = 1;   // create a game, start it, play
        else if (a == "--mpjoin") mpHeadless = 2;   // join the first game, play
        else if (a == "--mpai") mpHeadless = 4;     // host vs one server-run AI
        else if (a == "--mprejoin") mpHeadless = 5; // rejoin a held slot (resume ticket)
        else if (a == "--mpspectate") mpHeadless = 6; // watch the first running game
        else if (a == "--nofog") nofog = true;
        else if (a == "--cheat") tak::sim::gInstantBuild = true;
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
    if (!shot.empty() || mpHeadless) SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    (void)hostPort; (void)joinPort; (void)joinAddr;   // --host/--join retired (see --server)

    // Connect to the multiplayer server, if requested.
    std::unique_ptr<tak::net::MpClient> mp;
    if (!serverHost.empty()) {
        mp = std::make_unique<tak::net::MpClient>();
        if (playerName.empty()) playerName = "player";
        if (!mp->connect(serverHost, uint16_t(serverPort), playerName)) {
            std::fprintf(stderr, "server: %s\n", mp->error().c_str());
            return 1;
        }
        std::printf("connected to %s:%d as '%s'\n", serverHost.c_str(), serverPort, playerName.c_str());
    }


    if (shot.empty()) SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("takview", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, winW, winH,
                                       SDL_WINDOW_RESIZABLE);
    Uint32 renFlags = SDL_RENDERER_SOFTWARE;
    if (shot.empty()) renFlags = noVsync ? SDL_RENDERER_ACCELERATED
                                         : SDL_RENDERER_PRESENTVSYNC;
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, renFlags);
    if (!ren) {
        std::fprintf(stderr, "renderer failed: %s\n", SDL_GetError());
        return 1;
    }
    // Vsync avoids tearing but, with double buffering, halves the frame rate the
    // moment a frame overruns one refresh even when there's headroom. --novsync
    // unlocks it (useful to see true throughput / for high-refresh displays).
    if (shot.empty()) SDL_RenderSetVSync(ren, noVsync ? 0 : 1);

    std::unique_ptr<MapView> mapView;
    std::unique_ptr<ModelView> modelView;
    std::unique_ptr<GameView> gameView;
    try {
        if (mode == "replay" && args.size() >= 3) {
            // takview replay <file.takrep> <terrain-dir> <data-root>
            ReplayFile rf;
            if (!loadReplayFile(args[0], rf)) {
                std::fprintf(stderr, "replay: cannot read %s\n", args[0].c_str());
                return 1;
            }
            std::string tnt = args[2] + "/maps/" + rf.mapId + ".tnt";
            rf.cfg.tntPath = tnt;
            rf.cfg.dataRoot = args[2];
            gameView = std::make_unique<GameView>(ren, tnt, args[1], args[2], false, false,
                                                  false, /*bare=*/true, "ara", "tar", rf.crusades);
            std::fprintf(stderr, "replay: %s -- map '%s', %zu ticks%s\n", args[0].c_str(),
                         rf.mapId.c_str(), rf.bundles.size(), rf.crusades ? " (Crusades)" : "");
            gameView->startReplay(rf.cfg, std::move(rf.bundles));
        } else if (mode == "map" && args.size() >= 2) {
            mapView = std::make_unique<MapView>(ren, args[0], args[1]);
        } else if (mode == "game" && args.size() >= 3) {
            // A multiplayer client builds the world from the server's GameStarting
            // later, so it constructs "bare" (no single-player 2-monarch spawn).
            gameView = std::make_unique<GameView>(ren, args[0], args[1], args[2], demo,
                                                  scenario, missionFlag,
                                                  navy || amphib || firetest || facetest || mp,
                                                  side, aiSide, crusades);
            if (mp) {
                gameView->setMpClient(mp.get());
                gameView->setMpMapId(std::filesystem::path(args[0]).stem().string());
                if (const char* rp = std::getenv("TAK_RESUME")) gameView->setResumePath(rp);
            }
            // Never let the window shrink below what the widest build-icon row
            // needs (full-size icons), and grow it now if it opened smaller.
            {
                int minW = gameView->minWindowWidth();
                SDL_SetWindowMinimumSize(win, minW, 480);
                int cw, ch;
                SDL_GetWindowSize(win, &cw, &ch);
                if (cw < minW) SDL_SetWindowSize(win, minW, ch);
            }
            if (playerColor >= 0) gameView->setPlayerColor(0, playerColor);
            if (aiColor >= 0) gameView->setPlayerColor(1, aiColor);
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
    std::string serverMapId = args.empty() ? "" : std::filesystem::path(args[0]).stem().string();
    int ktPhase = keytest ? 0 : -1;
    float ktClock = 0;
    bool keytestSelectOnly = selonly;

    // Headless multiplayer test driver: auto-run the lobby + game loop against
    // takserver and print periodic hashes. Proves the server-sequenced lockstep
    // end to end without any SDL UI. (--mphost creates+starts, --mpjoin joins.)
    // Headless replay verify: play the whole recording and print the final hash.
    if (gameView && gameView->replayMode() && std::getenv("TAK_REPLAY_VERIFY")) {
        while (gameView->replayTick() < gameView->replayLength())
            gameView->replayStep(10.0f);   // guard caps to 64 ticks/call
        std::fprintf(stderr, "replay done: tick=%zu hash=%016llx units=%zu\n",
                     gameView->replayTick(), (unsigned long long)gameView->worldHashPublic(),
                     gameView->aliveUnits());
        return 0;
    }
    if (gameView && mp && mpHeadless) {
        std::string mapId = std::filesystem::path(args[0]).stem().string();
        int limitTicks = int((startTime > 0 ? startTime : 60) * 30);
        while (gameView->mpAutoStep(mpHeadless, mapId, crusades)) {
            if (int(gameView->netTick()) >= limitTicks) break;
            SDL_Delay(2);
        }
        std::fprintf(stderr, "mp-headless done: tick=%u hash=%016llx units=%zu err=%s\n",
                     gameView->netTick(), (unsigned long long)gameView->worldHashPublic(),
                     gameView->aliveUnits(),
                     gameView->netError().empty() ? "none" : gameView->netError().c_str());
        mp->disconnect();
        return gameView->netError().empty() ? 0 : 1;
    }

    uint64_t last = SDL_GetPerformanceCounter();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            // Escape quits the asset viewers, but in a running game it deselects
            // (handled by GameView::input) rather than exiting.
            if (e.type == SDL_QUIT ||
                (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && !gameView))
                running = false;
            // The GPU lost every render-target texture's contents (device/driver
            // reset). Rebuild the baked atlases so sprites don't blink out.
            if ((e.type == SDL_RENDER_TARGETS_RESET ||
                 e.type == SDL_RENDER_DEVICE_RESET) && gameView)
                gameView->invalidateRenderTargets();
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
        // FPS readout in the window title (updated ~4x/sec).
        {
            static float fpsAcc = 0; static int fpsFrames = 0;
            fpsAcc += dt; ++fpsFrames;
            if (fpsAcc >= 0.25f) {
                char title[64];
                std::snprintf(title, sizeof title, "takview  |  %.0f fps",
                              float(fpsFrames) / fpsAcc);
                SDL_SetWindowTitle(win, title);
                fpsAcc = 0; fpsFrames = 0;
            }
        }
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
                        if (u.alive() && u.player == 0 && u.type && u.type->isBuilder)
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

        if (gameView && dt > 0) gameView->setFps(1.0f / dt);
        int w, h;
        SDL_GetRendererOutputSize(ren, &w, &h);
        // Create textures before the render pass (mid-pass creation glitches
        // the whole frame on some backends).
        if (mapView) mapView->ensureChunks(w, h);
        if (gameView) gameView->prepare(w, h);
        SDL_SetRenderDrawColor(ren, 18, 18, 26, 255);
        SDL_RenderClear(ren);
        // Optional per-phase profiler (TAK_PROF=1): prints where each frame's
        // wall-clock goes, once a second, so a stall can be localised on real
        // hardware that the headless software renderer can't show.
        static const bool prof = getenv("TAK_PROF") != nullptr;
        static double pTer = 0, pUpd = 0, pDraw = 0, pPres = 0, pAcc = 0;
        static int pFrames = 0;
        auto pnow = [] { return double(SDL_GetPerformanceCounter()) /
                         double(SDL_GetPerformanceFrequency()) * 1000.0; };
        double t0 = prof ? pnow() : 0;
        if (mapView) mapView->draw(w, h);
        if (modelView) modelView->draw(w, h, dt);
        double t1 = prof ? pnow() : 0;
        if (gameView) {
            if (gameView->replayMode()) {
                gameView->replayStep(dt);   // play back a recorded .takrep
            } else if (gameView->isNet()) {
                // Server-sequenced lockstep: one mpAutoStep pumps the connection,
                // advances the lobby (auto-matchmaking for now -- a lobby UI is
                // follow-on), and simulates every delivered tick. (void)netAccum.
                (void)netAccum;
                // TAK_MPAUTO=N overrides the lobby driver (0 = UI-driven, the
                // default; 1 = auto-host; 2 = auto-join) -- handy for screenshots.
                static int autoOv = std::getenv("TAK_MPAUTO") ? std::atoi(std::getenv("TAK_MPAUTO")) : 0;
                gameView->mpAutoStep(autoOv, serverMapId, crusades);
            } else {
                gameView->update(dt);
            }
            double t2 = prof ? pnow() : 0;
            gameView->draw(w, h);
            double t3 = prof ? pnow() : 0;
            if (prof) { pTer += t1 - t0; pUpd += t2 - t1; pDraw += t3 - t2; }
            // Feed the whole real frame time (dt = last frame's total incl. present)
            // to the sprite auto-tuner, so a GPU-bound full-model crowd triggers it.
            gameView->autoTuneSprites(dt * 1000.0f);
        }
        double t4 = prof ? pnow() : 0;
        SDL_RenderPresent(ren);
        if (prof) {
            double t5 = pnow();
            pPres += t5 - t4;
            pAcc += t5 - t0; ++pFrames;
            if (pAcc >= 1000.0) {
                double proj = 0, submit = 0, sim = 0; long lod = 0, full = 0;
                if (gameView) gameView->takeProf(proj, submit, sim, lod, full);
                std::printf("PROF fps=%.0f | update=%.1f [sim=%.1f] draw=%.1f "
                            "[proj=%.1f submit=%.1f other=%.1f] present=%.1f | "
                            "impostor=%ld full=%ld\n",
                            pFrames * 1000.0 / pAcc, pUpd / pFrames, sim / pFrames,
                            pDraw / pFrames, proj / pFrames, submit / pFrames,
                            (pDraw - proj - submit) / pFrames, pPres / pFrames,
                            lod / std::max(1, pFrames), full / std::max(1, pFrames));
                std::fflush(stdout);
                pTer = pUpd = pDraw = pPres = pAcc = 0; pFrames = 0;
            }
        }

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
            // Render a few frames so lazy content settles, then capture. For content
            // that settles asynchronously (a network spectator building its world),
            // TAK_SHOT_MS waits that many wall-clock ms before capturing instead.
            static const char* shotMsEnv = std::getenv("TAK_SHOT_MS");
            static uint64_t shotT0 = SDL_GetTicks64();
            static int frames = 0;
            bool ready = shotMsEnv ? (SDL_GetTicks64() - shotT0 >= uint64_t(std::atoi(shotMsEnv)))
                                   : (++frames >= 3);
            if (ready && ktPhase < 0) {
                screenshot(ren, w, h, shot);
                running = false;
            }
        }
    }
    SDL_Quit();
    return 0;
}
