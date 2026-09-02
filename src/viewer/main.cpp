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
#include "gaf/gaf.h"
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
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

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

    void draw(int winW, int winH) {
        int mapW = map_.blocksX * 32, mapH = map_.blocksY * 32;
        offX_ = std::clamp(offX_, 0.0f, std::max(0.0f, mapW - winW / zoom_));
        offY_ = std::clamp(offY_, 0.0f, std::max(0.0f, mapH - winH / zoom_));

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
        std::sort(tris_.begin(), tris_.end(),
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
                for (auto& seq : tak::gaf::load(e.path(), pal)) {
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

    const std::vector<int16_t>* load(const std::string& key, const std::string& path) {
        auto it = cache_.find(key);
        if (it != cache_.end()) return &it->second;
        SDL_AudioSpec spec{};
        Uint8* buf = nullptr;
        Uint32 len = 0;
        if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &len)) return nullptr;
        SDL_AudioCVT cvt;
        if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS,
                              1, 11025) < 0) {
            SDL_FreeWAV(buf);
            return nullptr;
        }
        std::vector<uint8_t> work(size_t(len) * size_t(std::max(cvt.len_mult, 1)));
        std::memcpy(work.data(), buf, len);
        SDL_FreeWAV(buf);
        cvt.buf = work.data();
        cvt.len = int(len);
        if (cvt.needed && SDL_ConvertAudio(&cvt) != 0) return nullptr;
        size_t outBytes = cvt.needed ? size_t(cvt.len_cvt) : len;
        std::vector<int16_t> out(outBytes / 2);
        std::memcpy(out.data(), work.data(), out.size() * 2);
        return &cache_.emplace(key, std::move(out)).first->second;
    }

    static void mixThunk(void* ud, Uint8* stream, int len) {
        static_cast<SoundBank*>(ud)->mix(reinterpret_cast<int16_t*>(stream), len / 2);
    }

    void mix(int16_t* out, int n) {
        std::memset(out, 0, size_t(n) * 2);
        for (auto& c : channels_) {
            if (!c.data) continue;
            for (int i = 0; i < n && c.pos < c.data->size(); ++i, ++c.pos) {
                int v = out[i] + (*c.data)[c.pos] / 2;
                out[i] = int16_t(std::clamp(v, -32768, 32767));
            }
        }
    }

    std::map<std::string, std::string> index_;
    std::map<std::string, std::vector<int16_t>> cache_;
    Channel channels_[8];
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
    GameView(SDL_Renderer* ren, const std::string& tntPath, const std::string& terrainDir,
             const std::string& dataRoot, bool demo)
        : ren_(ren), mapView_(ren, tntPath, terrainDir), dataRoot_(dataRoot) {
        registry_.loadDir(dataRoot_ + "/units");
        registry_.loadBuildTree(dataRoot_ + "/canbuild");
        loadTextures(dataRoot_ + "/textures", dataRoot_ + "/palettes/ara_textures.pcx");
        mapView_.setZoom(0.9f);

        world_.setNav(tak::sim::NavGrid(mapView_.map().heights, mapView_.map().width,
                                        mapView_.map().height));
        float cx = mapView_.map().blocksX * 16.0f, cz = mapView_.map().blocksY * 16.0f;
        mapView_.setOffset(cx - 640 / 0.9f + 110, cz - 400 / 0.9f + 20);
        const char* aramon[] = {"araarch", "araarch", "araarch", "arasword",
                                "arasword", "araarch"};
        const char* taros[] = {"tararch", "tararch", "tardemon", "tararch",
                               "tardemon", "tararch"};
        std::vector<int> teamA, teamB;
        int i = 0;
        for (const char* t : aramon) {
            int id = spawn(t, cx - 160 + float(i % 2) * 26, cz - 60 + float(i / 2) * 30, 1.57f, 0);
            if (id >= 0) teamA.push_back(id);
            ++i;
        }
        i = 0;
        for (const char* t : taros) {
            int id = spawn(t, cx + 160 + float(i % 2) * 26, cz - 60 + float(i / 2) * 30,
                           -1.57f, 1);
            if (id >= 0) teamB.push_back(id);
            ++i;
        }
        // Bases: player Keep + Lodestones west, AI Abyss + Lodestones east.
        keepId_ = spawn("arakeep", cx - 260, cz + 30, 3.14159f, 0);
        spawn("aralode", cx - 340, cz - 30, 3.14159f, 0);
        spawn("aralode", cx - 180, cz - 30, 3.14159f, 0);
        aiKeepId_ = spawn("tardung", cx + 300, cz + 30, 3.14159f, 1);
        spawn("tarlode", cx + 380, cz - 30, 3.14159f, 1);
        spawn("tarlode", cx + 220, cz - 30, 3.14159f, 1);
        world_.team(1).mana = 800;

        try {
            hudFont_ = Font(ren_, dataRoot_ + "/fonts/bodfontbody.gaf");
            bigFont_ = Font(ren_, dataRoot_ + "/fonts/font48.gaf");
        } catch (const std::exception& e) {
            std::fprintf(stderr, "font load: %s\n", e.what());
        }
        sounds_.init(dataRoot_ + "/../english/Sounds", false);
        soundClasses_.load(dataRoot_ + "/gamedata/soundclasses");
        for (auto& u : world_.units()) {
            if (!u.type || u.type->canMove) continue;
            world_.nav().block(int(u.x) / 16 - u.type->footX / 2,
                               int(u.z) / 16 - u.type->footZ / 2,
                               u.type->footX, u.type->footZ, true);
        }
        if (demo) {
            for (size_t k = 0; k < teamA.size(); ++k)
                world_.attack(teamA[k], teamB[k % teamB.size()], false);
            for (size_t k = 0; k < teamB.size(); ++k)
                world_.attack(teamB[k], teamA[k % teamA.size()], false);
            for (int k = 0; k < 4; ++k)
                world_.train(keepId_, registry_.find("araarch"));
            demoAi_ = true;
        }
    }

    void input(const SDL_Event& e, int winW, int winH) {
        (void)winW; (void)winH;
        float zm = mapView_.zoom();
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym >= SDLK_1 &&
            e.key.keysym.sym <= SDLK_6 && !selection_.empty()) {
            const auto* b = world_.unit(selection_.front());
            if (b && b->type && b->type->isBuilder) {
                const auto& menu = registry_.buildable(b->type->id);
                size_t slot = size_t(e.key.keysym.sym - SDLK_1);
                if (slot < menu.size())
                    world_.train(b->id, registry_.find(menu[slot]));
            }
        } else if (e.type == SDL_MOUSEWHEEL || e.type == SDL_KEYDOWN) {
            mapView_.input(e);
        } else if (e.type == SDL_MOUSEMOTION) {
            if (e.motion.state & SDL_BUTTON_MMASK)   // middle-drag scrolls
                mapView_.setOffset(mapView_.offX() - e.motion.xrel / zm,
                                   mapView_.offY() - e.motion.yrel / zm);
            if (dragging_) { dragX1_ = float(e.motion.x); dragY1_ = float(e.motion.y); }
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            dragging_ = true;
            dragX0_ = dragX1_ = float(e.button.x);
            dragY0_ = dragY1_ = float(e.button.y);
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
                    if (u.alive() && u.team == 0 && u.x >= x0 && u.x <= x1 &&
                        u.z >= z0 && u.z <= z1)
                        selection_.push_back(u.id);
            }
        } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                   e.button.button == SDL_BUTTON_RIGHT && !selection_.empty()) {
            float wx = mapView_.offX() + e.button.x / zm;
            float wz = mapView_.offY() + e.button.y / zm;
            bool queue = (SDL_GetModState() & KMOD_SHIFT) != 0;
            // Clicking near an enemy = attack; else formation move.
            int enemy = -1;
            float best = 20 * 20;
            const auto* first = world_.unit(selection_.front());
            for (auto& u : world_.units()) {
                if (!u.alive() || !first || u.team == first->team) continue;
                float dx = u.x - wx, dz = u.z - wz;
                if (dx * dx + dz * dz < best) { best = dx * dx + dz * dz; enemy = u.id; }
            }
            if (enemy >= 0) {
                for (int id : selection_) world_.attack(id, enemy, queue);
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
                    float ox = std::clamp(u->x - cx, -60.0f, 60.0f);
                    float oz = std::clamp(u->z - cz, -60.0f, 60.0f);
                    world_.order(id, wx + ox, wz + oz, queue);
                }
            }
        }
    }

    void setFollow(float zoom) { follow_ = true; mapView_.setZoom(zoom); }

    void marchTo(float dx, float dz) {
        float cx = mapView_.map().blocksX * 16.0f, cz = mapView_.map().blocksY * 16.0f;
        for (auto& u : world_.units())
            if (u.team == 0) world_.order(u.id, cx + dx, cz + dz, false);
    }

    void update(float dt) {
        world_.tick(dt);
        for (auto& u : world_.units())
            if (u.type && u.alive() && !unitType_.count(u.id)) registerUnit(u);
        tickAi(dt);

        // Victory check: a side with no living units loses.
        if (outcome_ == 0) {
            int alive[2] = {0, 0};
            for (auto& u : world_.units())
                if (u.alive() && u.team < 2) ++alive[u.team];
            if (alive[1] == 0) outcome_ = 1;
            else if (alive[0] == 0) outcome_ = -1;
        }
        if (follow_ && !world_.units().empty()) {
            float cx = 0, cz = 0;
            for (auto& u : world_.units()) { cx += u.x; cz += u.z; }
            cx /= float(world_.units().size());
            cz /= float(world_.units().size());
            mapView_.setOffset(cx - 640 / mapView_.zoom(), cz - 400 / mapView_.zoom());
        }
        for (auto& u : world_.units()) {
            if (u.justFired && u.type) {
                if (u.type->weapon.melee)
                    sounds_.play("ahitfl0" + std::to_string(1 + (salt_++ % 3)));
                else
                    sounds_.play("bow2");
            }
            auto it = anims_.find(u.id);
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
                }
                a.vm->tick(dt);
                continue;
            }
            bool m = u.moving();
            if (m != a.walking) {
                a.walking = m;
                a.vm->reset();
                a.vm->setStatic(0, m ? 1 : 0);
                if (m) { a.vm->start("walk_legs") || a.vm->start("walk"); }
                else { a.vm->start("restore_legs") || a.vm->start("restore_x"); }
            }
            a.vm->tick(dt);
        }
    }

    void draw(int winW, int winH) {
        mapView_.draw(winW, winH);
        std::vector<const tak::sim::Unit*> order;
        for (auto& u : world_.units())
            if (u.deadFor < 4.0f) order.push_back(&u);   // corpses linger briefly
        std::sort(order.begin(), order.end(),
                  [](auto* a, auto* b) { return a->z < b->z; });
        for (const auto* u : order) drawUnit(*u);

        // Projectiles: short bright streaks.
        float zm = mapView_.zoom();
        SDL_SetRenderDrawColor(ren_, 255, 235, 140, 255);
        for (const auto& p : world_.projectiles()) {
            float sx = (p.x - mapView_.offX()) * zm;
            float sy = (p.z - mapView_.offY()) * zm - 8 * zm;
            SDL_RenderDrawLineF(ren_, sx, sy, sx - p.vx * 0.03f * zm,
                                sy - p.vz * 0.03f * zm);
        }

        for (int id : selection_) {
            const auto* u = world_.unit(id);
            if (!u || !u->alive()) continue;
            drawRing(u->x, u->z, 14);
            for (const auto& o : u->orders)
                if (o.targetId == 0) drawRing(o.x, o.z, 4);
        }

        // Health bars for damaged or selected units.
        for (const auto& u : world_.units()) {
            if (!u.alive() || !u.type) continue;
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

        // Team-0 mana bar + HUD text, top left.
        {
            auto& tm = world_.team(0);
            float cap = std::max(tm.storage, 100.0f);
            SDL_FRect bg{10, 10, 180, 12};
            SDL_SetRenderDrawColor(ren_, 20, 20, 30, 230);
            SDL_RenderFillRectF(ren_, &bg);
            SDL_FRect fg{12, 12, 176 * std::clamp(tm.mana / cap, 0.0f, 1.0f), 8};
            SDL_SetRenderDrawColor(ren_, 80, 200, 255, 255);
            SDL_RenderFillRectF(ren_, &fg);
            if (hudFont_.ok()) {
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
        if (aiTimer_ > 0 || outcome_ != 0) return;
        aiTimer_ = 1.0f;

        runAi(1, aiKeepId_, std::array<const char*, 4>{"tararch", "tartb", "tararch",
                                                       "tarbeak"});
        // In demo mode team 0 is AI-driven too: a full war plays itself out.
        if (demoAi_)
            runAi(0, keepId_, std::array<const char*, 4>{"araarch", "arasword", "araarch",
                                                         "araknigh"});
    }

    void runAi(int team, int keepId, std::array<const char*, 4> cycle) {
        auto* keep = world_.unit(keepId);
        if (keep && keep->alive() && keep->buildQueue.empty())
            world_.train(keepId, registry_.find(cycle[size_t(aiTrained_++) % cycle.size()]));

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
    };

    void registerUnit(const tak::sim::Unit& u) {
        const std::string& typeId = u.type->id;
        if (!visuals_.count(typeId)) {
            try {
                visuals_[typeId] = {tak::tdo::load(dataRoot_ + "/objects3d/" + typeId + ".3do")};
            } catch (const std::exception& e) {
                std::fprintf(stderr, "no model for %s: %s\n", typeId.c_str(), e.what());
                return;
            }
        }
        Anim a;
        try {
            auto cobFile = tak::cob::load(dataRoot_ + "/scripts/" + typeId + ".cob");
            for (const auto& p : cobFile.pieces) {
                std::string n = p;
                std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                a.pieceNames.push_back(n);
            }
            a.vm = std::make_unique<tak::cob::Vm>(std::move(cobFile));
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
                    auto& f = seq.frames[0];
                    if (f.width == 0 || f.height == 0) continue;
                    std::string name = seq.name;
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    if (textures_.count(name)) continue;
                    SDL_Texture* t = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                                       SDL_TEXTUREACCESS_STATIC,
                                                       f.width, f.height);
                    SDL_UpdateTexture(t, nullptr, f.rgba.data(), f.width * 4);
                    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                    textures_[name] = t;
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

    void drawUnit(const tak::sim::Unit& u) {
        auto vt = visuals_.find(unitType_.at(u.id));
        if (vt == visuals_.end()) return;
        const Anim* anim = nullptr;
        auto at = anims_.find(u.id);
        if (at != anims_.end()) anim = &at->second;

        tris_.clear();
        collect(vt->second.model.root, Xform{}, anim, u.heading);
        std::sort(tris_.begin(), tris_.end(),
                  [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
        float zm = mapView_.zoom();
        float ax = (u.x - mapView_.offX()) * zm;
        float ay = (u.z - mapView_.offY()) * zm;
        for (auto& t : tris_) {
            SDL_Vertex v[3];
            for (int i = 0; i < 3; ++i) {
                v[i] = t.v[i];
                v[i].position.x = v[i].position.x * zm + ax;
                v[i].position.y = v[i].position.y * zm + ay;
            }
            SDL_RenderGeometry(ren_, t.tex, v, 3, nullptr, 0);
        }
    }

    // Project model triangles relative to the unit anchor: yaw by heading,
    // fixed tilt so models read against TAK's painted top-down terrain.
    void collect(const tak::tdo::Object& o, const Xform& parent, const Anim* anim,
                 float heading) {
        static const float kNoRot[3] = {0, 0, 0};
        const tak::cob::PieceState* ps = pieceFor(anim, o.name);
        if (ps && !ps->visible) return;
        Xform xf = parent.then(o.x + (ps ? ps->move[0] : 0),
                               o.y + (ps ? ps->move[1] : 0),
                               o.z + (ps ? ps->move[2] : 0),
                               ps ? ps->rot : kNoRot);
        // Ground-plate reference pieces (AraGP, araground, ...) are never drawn.
        std::string oname = o.name;
        std::transform(oname.begin(), oname.end(), oname.begin(), ::tolower);
        bool groundPlate = oname.size() >= 2 &&
                           (oname.substr(oname.size() - 2) == "gp" ||
                            oname.find("ground") != std::string::npos ||
                            oname.find("gpoly") != std::string::npos);
        const float tilt = 1.05f;   // ~60 degrees down
        float cy = std::cos(heading + 3.14159f), sy = std::sin(heading + 3.14159f);
        float ct = std::cos(tilt), st = std::sin(tilt);
        for (const auto& p : o.primitives) {
            if (groundPlate) break;
            if (p.indices.size() < 3) continue;
            SDL_Texture* tex = nullptr;
            if (!p.texture.empty()) {
                std::string name = p.texture;
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                auto it = textures_.find(name);
                if (it != textures_.end()) tex = it->second;
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
                    float ry = w[1] * ct - rz * st;
                    depth += rz * ct + w[1] * st;
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
        for (const auto& c : o.children) collect(c, xf, anim, heading);
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
    tak::sim::TypeRegistry registry_;
    tak::sim::World world_;
    std::map<std::string, Visual> visuals_;
    std::map<int, std::string> unitType_;
    std::map<int, Anim> anims_;
    std::map<std::string, SDL_Texture*> textures_;
    std::vector<Tri> tris_;
    std::vector<int> selection_;
    bool dragging_ = false;
    float dragX0_ = 0, dragY0_ = 0, dragX1_ = 0, dragY1_ = 0;
    bool follow_ = false;
    bool trace_ = false;
    int keepId_ = -1, aiKeepId_ = -1;
    int aiTrained_ = 0;
    float aiTimer_ = 0;
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
    bool demoAi_ = false;
    Font hudFont_, bigFont_;
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
    std::string shot, cobPath, anim;
    float startTime = 0, followZoom = 0, marchX = 0, marchZ = 0;
    bool demo = false, doMarch = false, trace = false;
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "--cob" && i + 1 < argc) cobPath = argv[++i];
        else if (a == "--anim" && i + 1 < argc) anim = argv[++i];
        else if (a == "--time" && i + 1 < argc) startTime = std::stof(argv[++i]);
        else if (a == "--demo") demo = true;
        else if (a == "--trace") trace = true;
        else if (a == "--follow" && i + 1 < argc) followZoom = std::stof(argv[++i]);
        else if (a == "--march" && i + 2 < argc) {
            marchX = std::stof(argv[++i]);
            marchZ = std::stof(argv[++i]);
            doMarch = true;
        }
        else args.push_back(a);
    }
    if (!shot.empty()) SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("takview", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, kWinW, kWinH,
                                       SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren = SDL_CreateRenderer(
        win, -1, shot.empty() ? SDL_RENDERER_PRESENTVSYNC : SDL_RENDERER_SOFTWARE);
    if (!ren) {
        std::fprintf(stderr, "renderer failed: %s\n", SDL_GetError());
        return 1;
    }

    std::unique_ptr<MapView> mapView;
    std::unique_ptr<ModelView> modelView;
    std::unique_ptr<GameView> gameView;
    try {
        if (mode == "map" && args.size() >= 2) {
            mapView = std::make_unique<MapView>(ren, args[0], args[1]);
        } else if (mode == "game" && args.size() >= 3) {
            gameView = std::make_unique<GameView>(ren, args[0], args[1], args[2], demo);
            if (followZoom > 0) gameView->setFollow(followZoom);
            if (doMarch) gameView->marchTo(marchX, marchZ);
            if (trace) gameView->setTrace(true);
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
    uint64_t last = SDL_GetPerformanceCounter();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT ||
                (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
                running = false;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_s)
                screenshot(ren, kWinW, kWinH, "takview_shot.png");
            int ww, wh;
            SDL_GetRendererOutputSize(ren, &ww, &wh);
            if (mapView) mapView->input(e);
            if (modelView) modelView->input(e);
            if (gameView) gameView->input(e, ww, wh);
        }
        uint64_t now = SDL_GetPerformanceCounter();
        float dt = float(now - last) / float(SDL_GetPerformanceFrequency());
        last = now;

        int w, h;
        SDL_GetRendererOutputSize(ren, &w, &h);
        SDL_SetRenderDrawColor(ren, 18, 18, 26, 255);
        SDL_RenderClear(ren);
        if (mapView) mapView->draw(w, h);
        if (modelView) modelView->draw(w, h, dt);
        if (gameView) { gameView->update(dt); gameView->draw(w, h); }
        SDL_RenderPresent(ren);

        if (!shot.empty()) {
            // Render a few frames so lazy content settles, then capture.
            static int frames = 0;
            if (++frames >= 3) {
                screenshot(ren, w, h, shot);
                running = false;
            }
        }
    }
    SDL_Quit();
    return 0;
}
