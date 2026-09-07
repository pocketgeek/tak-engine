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

// Must precede SDL.h: on Windows this pulls in winsock2 (with WIN32_LEAN_AND_MEAN)
// before SDL's <windows.h> would otherwise pull the incompatible winsock v1.
#include "net/netcompat.h"

#include "cob/vm.h"
#include "crt/crt.h"
#include "gaf/gaf.h"
#include "gui/gui.h"
#include "hpi/hpi.h"
#include "net/client.h"
#include "net/lockstep.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"
#include "tdf/tdf.h"
#include "tdo/tdo.h"
#include "terrain/terrain.h"
#include "tnt/tnt.h"
#include "util/png.h"
#include "version.h"
#include "viewer/options.h"
#include "viewer/settings.h"
#include "viewer/mainmenu.h"
#include "viewer/menumusic.h"

// Keep our own main() on every platform (don't let SDL redefine it to SDL_main /
// pull in SDL2main + a WinMain); we call SDL_SetMainReady() in main() instead. This
// also keeps takview usable as a console/headless tool on Windows.
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
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

// Single-player auto-launches a local takserver (AIs run only on the server).
// Sockets come from net/netcompat.h (included first, before SDL). Process control
// is the one genuinely platform-specific bit: fork/exec on POSIX, CreateProcess on
// Windows.
#ifdef _WIN32
  #include <windows.h>
#else
  #include <csignal>
  #include <sys/wait.h>
  #include <unistd.h>
#endif

namespace {
// A local takserver spawned for single-player; killed when the client exits.
bool gLocalServerUp = false;
#ifdef _WIN32
PROCESS_INFORMATION gLocalProc{};
void killLocalServer() {
    if (gLocalServerUp) {
        TerminateProcess(gLocalProc.hProcess, 0);
        CloseHandle(gLocalProc.hProcess);
        CloseHandle(gLocalProc.hThread);
        gLocalServerUp = false;
    }
}
#else
pid_t gLocalPid = 0;
void killLocalServer() {
    if (gLocalPid > 0) { kill(gLocalPid, SIGTERM); waitpid(gLocalPid, nullptr, 0); gLocalPid = 0; }
    gLocalServerUp = false;
}
#endif

// Pick a free loopback TCP port by binding to 0 and reading the assignment.
int pickFreePort() {
    tak::net::netStartup();
    int fd = int(socket(AF_INET, SOCK_STREAM, 0));
    if (fd < 0) return 0;
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) {
        socklen_t len = sizeof a;
        if (getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len) == 0) port = ntohs(a.sin_port);
    }
    tak::net::sockClose(fd);
    return port;
}

// Launch a takserver for a private single-player game. Returns true on success.
bool spawnLocalServer(const std::string& serverBin, const std::string& dataRoot, int port) {
#ifdef _WIN32
    std::string cmd = "\"" + serverBin + ".exe\" --port " + std::to_string(port) +
                      " --data \"" + dataRoot + "\"";
    STARTUPINFOA si{}; si.cb = sizeof si;
    std::vector<char> mut(cmd.begin(), cmd.end()); mut.push_back('\0');
    if (!CreateProcessA(nullptr, mut.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &si, &gLocalProc))
        return false;
    gLocalServerUp = true;
    return true;
#else
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        std::string ps = std::to_string(port);
        execl(serverBin.c_str(), serverBin.c_str(), "--port", ps.c_str(),
              "--data", dataRoot.c_str(), static_cast<char*>(nullptr));
        _exit(127);   // exec failed
    }
    gLocalPid = pid;
    gLocalServerUp = true;
    return true;
#endif
}
}  // namespace

namespace {

float gTilt = 0.72f;
// Default windowed size (used when not fullscreen -- the default IS fullscreen; see
// Settings::fullscreen). 1920x1080 for modern displays. The 4:3 title menu
// letterboxes within it; the lobby scales to fit + centres itself (kLobbyW/kLobbyH),
// so both stay fully visible at this or any other size/aspect.
constexpr int kWinW = 1920, kWinH = 1080;

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
    MapView(SDL_Renderer* ren, const tak::hpi::Vfs& vfs, const std::string& mapPath)
        : ren_(ren), map_(tak::tnt::Map::load(vfs.read(mapPath), mapPath)), comp_(vfs) {}

    void input(const SDL_Event& e) {
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
    void setZoomSpeed(float m) { zoomSpeed_ = std::clamp(m, 0.25f, 4.0f); }
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
    float zoomSpeed_ = 1.0f;   // wheel-zoom sensitivity exponent (Options)
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
    const tak::hpi::Vfs* vfs_ = nullptr;   // runtime read-path (owned by main)

    SoundBank() = default;
    // Owns an SDL audio device + the buffers its callback reads; never copy it.
    SoundBank(const SoundBank&) = delete;
    SoundBank& operator=(const SoundBank&) = delete;
    ~SoundBank() {
        // Stop SDL's audio callback thread BEFORE our buffers (music_/voices_) are
        // destroyed. SDL_CloseAudioDevice blocks until the callback returns and
        // won't call it again, so mixThunk can't fire on freed state -- this is the
        // return-to-menu teardown crash (GameView, and thus SoundBank, is freed).
        if (dev_) { SDL_CloseAudioDevice(dev_); dev_ = 0; }
    }

    void init(const tak::hpi::Vfs& vfs) {
        vfs_ = &vfs;
        // Index the sounds/ namespace by stem (user overrides already win via the
        // VFS). A missing sounds dir must NOT skip audio init (music shares the
        // device); just index whatever's there.
        for (const std::string& path : vfs.list("sounds")) {
            std::filesystem::path fp(path);
            std::string ext = fp.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext != ".wav") continue;
            std::string stem = fp.stem().string();
            std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);
            index_[stem] = path;
        }

        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
        // The process-wide detected layout (see tak::detectOutputChannels) -- shared
        // with the Options per-speaker sliders so they always match what we mix into,
        // and it reveals surround even when the default sink advertises stereo.
        int chans = tak::detectOutputChannels();
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
        std::fprintf(stderr, "audio: %d output channels%s%s\n", chan_,
                     chan_ >= 4 ? " (surround: front/rear enabled)" : "",
                     (chan_ == 6 || chan_ == 8) ? ", LFE subwoofer driven" : "");
        if (dev_) SDL_PauseAudioDevice(dev_, 0);
    }

    // (User sound overrides -- e.g. overrides/click.hpi replacing the faction
    // order tones -- now arrive through the VFS's overrides layer, which wins the
    // sounds/ namespace automatically, so no separate override loader is needed.)

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
        // Re-pan every still-playing positional sound so it tracks its world source as
        // the camera pans/zooms (matters for anything longer than a blip).
        if (dev_) {
            SDL_LockAudioDevice(dev_);
            for (auto& c : channels_)
                if (c.positional && c.data && c.pos < c.data->size()) repan(c);
            SDL_UnlockAudioDevice(dev_);
        }
    }

    // Non-positional (UI, music-adjacent) — centred across all speakers.
    void play(const std::string& name) { playAt(name, 0.0f, 0.0f); }

    // Positional: pan by the source's world position relative to the listener.
    // Left/right from x; front(up)/rear(down) from z on surround setups. The sound is
    // tagged positional, so setListener re-pans it every frame as the camera moves --
    // it tracks its world source for its whole duration, not just at trigger time.
    void playWorld(const std::string& name, float x, float z) {
        float pan = std::clamp((x - listenX_) / listenHW_, -1.0f, 1.0f);
        float depth = std::clamp((z - listenZ_) / listenHH_, -1.0f, 1.0f);
        playAt(name, pan, depth, true, x, z);
    }

    // Play the synthesised 10-second disco loop as a positional SFX from (x,z) -- the
    // Shift+D dance-floor track. Generated in code (no shipped asset) at the mixer's
    // 11025 Hz mono and cached under "disco", so it pans/fades like any unit sound.
    void discoAt(float x, float z) {
        if (!cache_.count("disco")) buildDisco();
        playWorld("disco", x, z);
    }

    // Likewise for the Shift+H headbang: a synthesised 10s heavy-metal track (distorted
    // power-chord chugs, double-bass kick, snare + crash), played positionally.
    void metalAt(float x, float z) {
        if (!cache_.count("metal")) buildMetal();
        playWorld("metal", x, z);
    }

    // Move the world source of any currently-playing copies of `name` (e.g. keep the
    // disco pinned to a monarch that walks off). setListener re-pans from the new point.
    void repositionWorld(const std::string& name, float x, float z) {
        if (!dev_) return;
        std::string n = name;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        auto it = cache_.find(n);
        if (it == cache_.end()) return;
        const std::vector<int16_t>* data = &it->second;
        SDL_LockAudioDevice(dev_);
        for (auto& c : channels_)
            if (c.data == data) { c.wx = x; c.wz = z; repan(c); }
        SDL_UnlockAudioDevice(dev_);
    }

  private:
    void buildDisco() {
        constexpr float PI = 3.14159265358979f;
        const int SR = 11025;
        const float BPM = 120.0f, beat = 60.0f / BPM, eighth = beat * 0.5f;
        const int N = int(SR * 10.0f);
        std::vector<float> buf(size_t(N) + size_t(SR), 0.0f);
        std::mt19937 rng(1234567u);
        auto rnd = [&] { return float(rng()) / float(std::mt19937::max()) * 2.0f - 1.0f; };
        auto place = [&](const std::vector<float>& s, float start) {
            size_t i = size_t(start * SR);
            for (size_t k = 0; k < s.size() && i + k < buf.size(); ++k) buf[i + k] += s[k];
        };
        auto sawv = [](float f, float t) { float p = f * t; return 2.0f * (p - std::floor(0.5f + p)); };
        auto n2f = [](float semi) { return 110.0f * std::pow(2.0f, semi / 12.0f); };
        auto kick = [&] {
            int n = int(SR * 0.20f); std::vector<float> s(size_t(n), 0.0f); double ph = 0;
            for (int i = 0; i < n; ++i) { float t = i / float(SR);
                ph += 2.0 * PI * (110.0f * std::exp(-t * 42.0f) + 46.0f) / SR;
                s[size_t(i)] = float(std::sin(ph)) * std::exp(-t * 16.0f); } return s; };
        auto clap = [&] {
            int n = int(SR * 0.22f); std::vector<float> s(size_t(n), 0.0f); float mx = 1e-6f;
            for (int i = 0; i < n; ++i) { float t = i / float(SR); float amp = 0.6f * std::exp(-t * 22.0f);
                for (float off : {0.0f, 0.010f, 0.020f}) { int so = int(off * SR);
                    if (i >= so) amp += std::exp(-((i - so) / float(SR)) * 90.0f); }
                s[size_t(i)] = rnd() * amp; mx = std::max(mx, std::fabs(s[size_t(i)])); }
            for (auto& v : s) v *= 0.5f / mx;
            return s; };
        auto hat = [&](bool open) {
            int n = int(SR * (open ? 0.28f : 0.045f)); std::vector<float> s(size_t(n), 0.0f), nz(size_t(n), 0.0f);
            for (auto& v : nz) v = rnd();
            for (int i = 0; i < n; ++i) { float t = i / float(SR); float sm = 0; int c = 0;
                for (int k = -3; k <= 2; ++k) { int j = i + k; if (j >= 0 && j < n) { sm += nz[size_t(j)]; ++c; } }
                sm /= std::max(1, c);
                s[size_t(i)] = (open ? 0.22f : 0.28f) * (nz[size_t(i)] - sm) * std::exp(-t * (open ? 11.0f : 70.0f)); }
            return s; };
        auto bass = [&](float f, float L) {
            int n = int(SR * L); std::vector<float> s(size_t(n), 0.0f); float acc = 0;
            for (int i = 0; i < n; ++i) { float t = i / float(SR); float x = sawv(f, t) + 0.5f * sawv(f * 0.5f, t);
                acc += (0.06f + 0.25f * std::exp(-t * 12.0f)) * (x - acc);
                s[size_t(i)] = 0.55f * acc * std::min(1.0f, t * 200.0f) * std::exp(-t * 3.0f); } return s; };
        auto stab = [&](const std::vector<float>& fr, float L) {
            int n = int(SR * L); std::vector<float> s(size_t(n), 0.0f), o(size_t(n), 0.0f);
            for (int i = 0; i < n; ++i) { float t = i / float(SR); float v = 0;
                for (float f : fr) v += sawv(f - 0.6f, t) + sawv(f + 0.6f, t);
                s[size_t(i)] = v / float(fr.size() * 2); }
            for (int i = 0; i < n; ++i) { float t = i / float(SR); float sm = 0; int c = 0;
                for (int k = -1; k <= 1; ++k) { int j = i + k; if (j >= 0 && j < n) { sm += s[size_t(j)]; ++c; } }
                sm /= std::max(1, c);
                o[size_t(i)] = (0.7f * s[size_t(i)] + 0.3f * (s[size_t(i)] - sm)) * 0.5f * std::exp(-t * 9.0f); }
            return o; };
        auto lead = [&](float f, float L) {
            int n = int(SR * L); std::vector<float> s(size_t(n), 0.0f); double ph = 0;
            for (int i = 0; i < n; ++i) { float t = i / float(SR);
                ph += 2.0 * PI * f * (1.0f + 0.006f * std::sin(2 * PI * 6 * t)) / SR;
                s[size_t(i)] = 0.28f * (float(std::sin(ph)) + 0.3f * float(std::sin(2 * ph))) *
                               std::min(1.0f, t * 40.0f) * std::min(1.0f, (L - t) * 30.0f); } return s; };

        struct Bar { const char* name; float root; std::vector<float> tones, melo; };
        auto n = [&](float s) { return n2f(s + 12); };
        std::vector<Bar> prog = {
            {"Am", 0, {n(0), n(3), n(7)},   {n(0), n(3), n(7)}},
            {"F", -4, {n(-4), n(0), n(3)},  {n(-4), n(0), n(3)}},
            {"C",  3, {n(3), n(7), n(10)},  {n(3), n(7), n(10)}},
            {"G", -2, {n(-2), n(2), n(5)},  {n(-2), n(2), n(5)}},
            {"Am", 0, {n(0), n(3), n(7)},   {n(0), n(3), n(7)}},
        };
        for (size_t b = 0; b < prog.size(); ++b) {
            float b0 = float(b) * 4 * beat;
            for (int k = 0; k < 4; ++k) place(kick(), b0 + k * beat);
            place(clap(), b0 + beat); place(clap(), b0 + 3 * beat);
            for (int k = 0; k < 8; ++k) place(hat(k == 7), b0 + k * eighth);
            float r = n2f(prog[b].root);
            for (int k = 0; k < 8; ++k) place(bass((k % 2) ? r * 2 : r, eighth * 0.95f), b0 + k * eighth);
            for (int k : {1, 3, 5, 7}) place(stab(prog[b].tones, eighth * 1.2f), b0 + k * eighth);
            for (size_t j = 0; j < prog[b].melo.size(); ++j)
                place(lead(prog[b].melo[j], beat * 0.9f), b0 + (2 + float(j) * 0.66f) * beat);
        }
        float mx = 1e-6f;
        for (int i = 0; i < N; ++i) mx = std::max(mx, std::fabs(buf[size_t(i)]));
        float g = 1.4f / mx;
        std::vector<int16_t> pcm(size_t(N), 0);
        for (int i = 0; i < N; ++i)
            pcm[size_t(i)] = int16_t(std::clamp(std::tanh(buf[size_t(i)] * g) * 0.9f, -1.0f, 1.0f) * 32767);
        cache_["disco"] = std::move(pcm);
        index_["disco"] = "disco";
    }

    void buildMetal() {
        constexpr float PI = 3.14159265358979f;
        const int SR = 11025;
        const float BPM = 152.0f, beat = 60.0f / BPM, six = beat / 4;
        const int N = int(SR * 10.0f);
        std::vector<float> buf(size_t(N) + size_t(SR), 0.0f);
        std::mt19937 rng(99887766u);
        auto rnd = [&] { return float(rng()) / float(std::mt19937::max()) * 2.0f - 1.0f; };
        auto place = [&](const std::vector<float>& s, float start) {
            size_t i = size_t(start * SR);
            for (size_t k = 0; k < s.size() && i + k < buf.size(); ++k) buf[i + k] += s[k];
        };
        auto sawv = [](float f, float t) { float p = f * t; return 2.0f * (p - std::floor(0.5f + p)); };
        auto n2f = [](float semi) { return 110.0f * std::pow(2.0f, semi / 12.0f); };
        // Distorted power chord (root + fifth + octave), palm-muted or ringing.
        auto chug = [&](float root, float L, bool mute) {
            int n = int(SR * L); std::vector<float> s(size_t(n), 0.0f);
            float f5 = root * std::pow(2.0f, 7.0f / 12.0f), f8 = root * 2.0f, lp = 0.0f;
            for (int i = 0; i < n; ++i) { float t = i / float(SR);
                // Detuned double-tracked rhythm guitars = a fatter wall of chug.
                float x = sawv(root, t) + sawv(root + 0.35f, t)
                        + sawv(f5, t) + sawv(f5 + 0.35f, t) + 0.7f * sawv(f8, t);
                x = std::tanh(x * 5.5f);                              // heavy distortion
                lp += 0.5f * (x - lp);                                // tame the fizz
                float amp = mute ? std::exp(-t * 26.0f)
                                 : std::min(1.0f, t * 400.0f) * std::exp(-t * 2.5f);
                s[size_t(i)] = 0.42f * lp * amp; }
            return s; };
        auto kick = [&] {
            int n = int(SR * 0.12f); std::vector<float> s(size_t(n), 0.0f); double ph = 0;
            for (int i = 0; i < n; ++i) { float t = i / float(SR);
                ph += 2.0 * PI * (150.0f * std::exp(-t * 55.0f) + 50.0f) / SR;
                float click = t < 0.004f ? (1.0f - t / 0.004f) : 0.0f;
                s[size_t(i)] = float(std::sin(ph)) * std::exp(-t * 22.0f) + 0.5f * click; }
            return s; };
        auto snare = [&] {
            int n = int(SR * 0.18f); std::vector<float> s(size_t(n), 0.0f); double ph = 0;
            for (int i = 0; i < n; ++i) { float t = i / float(SR);
                ph += 2.0 * PI * 180.0f / SR;
                s[size_t(i)] = (0.7f * rnd() + 0.4f * float(std::sin(ph))) * std::exp(-t * 26.0f); }
            return s; };
        auto crash = [&] {
            int n = int(SR * 0.7f); std::vector<float> s(size_t(n), 0.0f);
            for (int i = 0; i < n; ++i) { float t = i / float(SR);
                s[size_t(i)] = 0.3f * (rnd() - 0.5f * rnd()) * std::exp(-t * 4.0f); }
            return s; };
        // Distorted twin-lead guitar (detuned, with vibrato) -- the soaring melody line.
        auto lead = [&](float freq, float L) {
            int n = int(SR * L); std::vector<float> s(size_t(n), 0.0f);
            for (int i = 0; i < n; ++i) { float t = i / float(SR);
                float vib = 1.0f + 0.012f * std::sin(2 * PI * 5.5f * t);
                float x = sawv(freq * vib, t) + sawv(freq * vib * 1.006f, t);
                x = std::tanh(x * 4.0f);
                float amp = std::min(1.0f, t * 90.0f) * std::min(1.0f, (L - t) * 45.0f);
                s[size_t(i)] = 0.20f * x * amp; }
            return s; };
        // E natural minor scale (E F# G A B C D), semitones from A2 (E2 = -5); deg 0 = E.
        auto scale = [&](int deg) {
            static const int st[7] = {-5, -3, -2, 0, 2, 3, 5};
            int o = 0; while (deg < 0) { deg += 7; --o; } while (deg >= 7) { deg -= 7; ++o; }
            return n2f(float(st[deg]) + 12.0f * o);
        };
        auto twin = [&](int deg, float start, float L) {   // melody + a diatonic 3rd above
            place(lead(scale(deg), L), start);
            place(lead(scale(deg + 2), L), start);
        };
        // Riff: root per bar (E-heavy with movement), galloping chugs + gallop kick.
        float roots[6] = {n2f(-5), n2f(-5), n2f(-2), n2f(0), n2f(-5), n2f(3)};   // E E G A E C
        // Twin-lead melody (upper octave, deg 7 = E4), two half-notes per bar.
        int melody[6][2] = {{7, 9}, {11, 10}, {13, 11}, {14, 12}, {11, 9}, {7, 9}};
        for (int b = 0; b < 6; ++b) {
            float b0 = float(b) * 4 * beat;
            place(crash(), b0);
            for (int bt = 0; bt < 4; ++bt) {
                float t0 = b0 + bt * beat;
                for (float off : {0.0f, 2 * six, 3 * six}) {   // dum da-da gallop
                    place(chug(roots[b], (off == 0.0f ? beat * 0.5f : six * 0.9f), true), t0 + off);
                    place(kick(), t0 + off);
                }
            }
            place(snare(), b0 + beat);
            place(snare(), b0 + 3 * beat);
            twin(melody[b][0], b0, beat * 2 * 0.92f);              // soaring lead over the riff
            twin(melody[b][1], b0 + 2 * beat, beat * 2 * 0.92f);
        }
        float mx = 1e-6f;
        for (int i = 0; i < N; ++i) mx = std::max(mx, std::fabs(buf[size_t(i)]));
        float g = 1.5f / mx;
        std::vector<int16_t> pcm(size_t(N), 0);
        for (int i = 0; i < N; ++i)
            pcm[size_t(i)] = int16_t(std::clamp(std::tanh(buf[size_t(i)] * g) * 0.92f, -1.0f, 1.0f) * 32767);
        cache_["metal"] = std::move(pcm);
        index_["metal"] = "metal";
    }

  public:

    void playAt(const std::string& name, float pan, float depth,
                bool positional = false, float wx = 0, float wz = 0) {
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
                c.positional = positional;
                c.wx = wx;
                c.wz = wz;
                break;
            }
        SDL_UnlockAudioDevice(dev_);
    }

private:
    struct Channel {
        const std::vector<int16_t>* data = nullptr;
        size_t pos = 0;
        float pan = 0, depth = 0;   // -1..+1 : left..right, front..rear
        float wx = 0, wz = 0;       // world emission point (for positional re-panning)
        bool positional = false;    // true = re-pan every frame from (wx,wz)
    };
    // Recompute a channel's pan/depth from its world point and the current listener.
    void repan(Channel& c) {
        c.pan = std::clamp((c.wx - listenX_) / listenHW_, -1.0f, 1.0f);
        c.depth = std::clamp((c.wz - listenZ_) / listenHH_, -1.0f, 1.0f);
    }

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
        if (!vfs_) return nullptr;
        std::vector<uint8_t> bytes;
        try { bytes = vfs_->read(path); } catch (const std::exception&) { return nullptr; }
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
        int lfe = (ch == 6 || ch == 8) ? 3 : -1;   // 5.1/7.1 LFE (subwoofer) channel
        if (lfe >= 0) lfeMono_.assign(size_t(frames), 0);   // full-range mono for the sub
        auto add = [&](int f, int ci, int v) {
            int idx = f * ch + ci;
            out[idx] = int16_t(std::clamp(out[idx] + v, -32768, 32767));
        };
        // Background music bed (quieter than SFX), spread across the main speakers --
        // NOT the LFE, which gets the low-passed sub feed instead.
        for (int f = 0; f < frames; ++f) {
            if (musicPos_ >= music_.size()) { musicDone_ = true; break; }
            int m = (music_[musicPos_++] * musicVol_) / 256;
            int mc = m / (ch > 2 ? 2 : 1);   // don't get louder with more speakers
            for (int ci = 0; ci < ch; ++ci)
                if (ci != lfe) add(f, ci, mc);
            if (lfe >= 0) lfeMono_[size_t(f)] += m;
        }
        // Positional SFX: pan each into the speaker layout.
        float g[8];
        for (auto& c : channels_) {
            if (!c.data) continue;
            channelGains(c.pan, c.depth, g);
            for (int f = 0; f < frames && c.pos < c.data->size(); ++f, ++c.pos) {
                int s = (*c.data)[c.pos] / 2 * sfxVol_ / 256;
                for (int ci = 0; ci < ch; ++ci)
                    if (g[ci] != 0.0f) add(f, ci, int(s * g[ci]));
                if (lfe >= 0) lfeMono_[size_t(f)] += s;
            }
        }
        // Subwoofer: one-pole low-pass of the mono mix (~110 Hz cutoff at 11025 Hz),
        // fed into the LFE channel so a 5.1/7.1 rig drives the sub directly.
        if (lfe >= 0) {
            constexpr float kAlpha = 0.06f;   // 1/(1 + fs/(2*pi*fc)), fc ~= 110 Hz
            constexpr float kGain = 0.7f;     // sub level (headroom for stacked bass)
            for (int f = 0; f < frames; ++f) {
                lpfState_ += kAlpha * (float(lfeMono_[size_t(f)]) - lpfState_);
                add(f, lfe, int(lpfState_ * kGain));
            }
        }
        // Final trim: master volume, then each output channel's own gain (LFE
        // included) -- applied after everything is summed so one control scales the
        // whole mix. Both default to unity (masterVol_=256, chanGain_=1), so this is
        // an exact no-op at default settings.
        for (int f = 0; f < frames; ++f)
            for (int ci = 0; ci < ch; ++ci) {
                int idx = f * ch + ci;
                int v = int(out[idx] * masterVol_ / 256 * chanGain_[ci]);
                out[idx] = int16_t(std::clamp(v, -32768, 32767));
            }
    }

public:
    // Begin playing a shuffled playlist of the given track numbers (a
    // faction's tracks, per sidedata.tdf). Empty = all 20. `dataRoot` is
    // the extracted data dir; music may live there or in the game install.
    void startMusic(const tak::hpi::Vfs& vfs, const std::vector<int>& tracks) {
        vfs_ = &vfs;
        std::vector<int> want = tracks;
        if (want.empty())
            for (int i = 1; i <= 20; ++i) want.push_back(i);
        for (int n : want) {
            std::string path = "music/track" + std::to_string(n) + ".wav";
            if (vfs.has(path)) playlist_.push_back(path);
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
    void setMasterVolume(int v) { masterVol_ = std::clamp(v, 0, 256); }
    void setSfxVolume(int v) { sfxVol_ = std::clamp(v, 0, 256); }
    void setChannelGain(int i, float g) { if (i >= 0 && i < 8) chanGain_[i] = std::clamp(g, 0.0f, 1.0f); }

    // For the Options screen: how many output channels the device gave us, and a
    // human label for each (matching channelGains()'s per-count speaker layout).
    int channelCount() const { return std::clamp(chan_, 1, 8); }
    const char* channelRole(int i) const {
        switch (chan_) {
            case 2: { static const char* r[] = {"LEFT", "RIGHT"}; return i < 2 ? r[i] : ""; }
            case 4: { static const char* r[] = {"FRONT L", "FRONT R", "REAR L", "REAR R"}; return i < 4 ? r[i] : ""; }
            case 6: { static const char* r[] = {"FRONT L", "FRONT R", "CENTER", "SUB", "REAR L", "REAR R"}; return i < 6 ? r[i] : ""; }
            case 8: { static const char* r[] = {"FRONT L", "FRONT R", "CENTER", "SUB", "REAR L", "REAR R", "SIDE L", "SIDE R"}; return i < 8 ? r[i] : ""; }
            default: return "MONO";
        }
    }

private:
    void loadTrack(size_t idx) {
        SDL_AudioSpec spec{};
        Uint8* buf = nullptr;
        Uint32 len = 0;
        std::vector<uint8_t> raw;
        if (vfs_) { try { raw = vfs_->read(playlist_[idx]); } catch (const std::exception&) {} }
        SDL_RWops* rw = raw.empty() ? nullptr : SDL_RWFromConstMem(raw.data(), int(raw.size()));
        if (!rw || !SDL_LoadWAV_RW(rw, 1, &spec, &buf, &len)) {
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
    int musicVol_ = 90;   // out of 256 (BGM)
    int masterVol_ = 256; // out of 256, global gain over the whole mix
    int sfxVol_ = 256;    // out of 256, sound effects
    float chanGain_[8] = {1, 1, 1, 1, 1, 1, 1, 1};   // per-output-channel trim 0..1
    bool musicDone_ = false;
    SDL_AudioDeviceID dev_ = 0;
    SDL_AudioSpec spec_{};
    int chan_ = 1;              // output channel count (2=stereo, 4/6/8=surround)
    bool verbose_ = false;
    // Subwoofer (LFE) feed: a one-pole low-pass of the full mono mix, driven into the
    // 5.1/7.1 LFE channel. lpfState_ persists across callbacks (the filter's memory).
    float lpfState_ = 0;
    std::vector<int> lfeMono_;   // per-callback mono accumulator (reused, no hot alloc)
};

// Sound classes: gamedata/soundclasses/*.tdf map a class to event ->
// candidate WAV names.
class SoundClasses {
public:
    void load(const tak::hpi::Vfs& vfs) {
        try {
            for (const std::string& path : vfs.list("gamedata/soundclasses")) {
                if (std::filesystem::path(path).extension() != ".tdf") continue;
                try {
                    auto sb = vfs.read(path);
                    auto root = tak::tdf::parseText(std::string(sb.begin(), sb.end()), path);
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
    Font(SDL_Renderer* ren, const tak::hpi::Vfs& vfs, const std::string& gafPath) {
        std::filesystem::path pcx = gafPath;
        pcx.replace_extension(".pcx");
        auto pal = tak::gaf::Palette::fromBytes(vfs.read(pcx.generic_string()),
                                                pcx.generic_string());
        auto seqs = tak::gaf::load(vfs.read(gafPath), pal, -1, gafPath);
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

// RAII: force the render scale to 1:1 for an off-screen bake, restoring it on exit.
// The whole-frame AA path (main) leaves SDL_RenderSetScale at Sx during draw(); any
// atlas/impostor/icon bake must render at 1:1 or its contents come out scaled.
struct AaScaleReset {
    SDL_Renderer* r; float sx, sy;
    explicit AaScaleReset(SDL_Renderer* rr) : r(rr) {
        SDL_RenderGetScale(r, &sx, &sy);
        SDL_RenderSetScale(r, 1.0f, 1.0f);
    }
    ~AaScaleReset() { SDL_RenderSetScale(r, sx, sy); }
};

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

    // Player start positions from the current map's sibling .ota, in world pixels,
    // ordered StartPos1, StartPos2, …. Delegates to the shared sim helper so the
    // client and the server's referee derive identical positions.
    std::vector<std::pair<float, float>> parseStartPositions() const {
        return tak::sim::parseStartPositions(vfs_, mapPath_);
    }

    GameView(SDL_Renderer* ren, tak::hpi::Vfs vfs, const std::string& mapPath,
             const std::string& installRoot, tak::hpi::OverridePolicy policy,
             bool demo, bool scenario, bool mission,
             bool bare, const std::string& side = "ara", const std::string& aiSide = "tar",
             bool crusades = false)
        // (side_ initialized below before loadPanel uses it; vfs_ must precede
        //  mapView_ in the member list so the Compositor can borrow it)
        : ren_(ren), vfs_(std::move(vfs)), mapView_(ren, vfs_, mapPath),
          installRoot_(installRoot), policy_(policy),
          mapPath_(mapPath), crusades_(crusades), side_(side), aiSide_(aiSide) {
        // Unit registry: MOVEINFO + units + canbuild (+ Crusades overlay first).
        // The VFS merges base + Iron Plague + community data into one namespace,
        // precedence resolved by the retail newest-date rule.
        tak::sim::setupRegistry(registry_, vfs_, crusades_);
        if (crusades_)
            std::fprintf(stderr, "balance: Crusades (unitscb/canbuildcb)%s\n",
                         vfs_.list("unitscb").empty() ? " -- NOT FOUND" : "");
        // God economy timing (gamedata/gods.tdf). TAK_GODTIME overrides the
        // appear time (seconds) for testing; otherwise use AppearTimeMin minutes.
        try {
            auto g = vtdf("gamedata/gods.tdf");
            if (const auto* tm = g.child("TIMING")) {
                float appear = float(tm->numberOr("AppearTimeMin", 30.0)) * 60.0f;
                if (const char* e = getenv("TAK_GODTIME")) appear = std::stof(e);
                world_.enableGods(appear);
            }
        } catch (const std::exception&) {}
        loadTextures();
        mapView_.setZoom(0.9f);
        try {
            hudFont_ = Font(ren_, vfs_, "fonts/bodfontbody.gaf");
            bigFont_ = Font(ren_, vfs_, "fonts/font48.gaf");
            // A plain, legible font for the HUD stat readouts.
            try { statFont_ = Font(ren_, vfs_, "fonts/b_times new roman (100b).gaf"); }
            catch (const std::exception&) {
                try { statFont_ = Font(ren_, vfs_, "fonts/ig_times new roman (100).gaf"); }
                catch (const std::exception&) {}
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "font load: %s\n", e.what());
        }
        loadOrderButtons();
        loadBuildFx();
        sounds_.init(vfs_);
        soundClasses_.load(vfs_);   // music is started per-state by manageMusic()
        loadPanel(side_);
        loadGui(side_);

        if (mission) {
            world_.setTerrain(mapView_.map().heights, mapView_.map().width,
                              mapView_.map().height, mapView_.map().seaLevel);
            try {
                auto ota = vtdf(mapSibling(".ota"));
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
            loadFeatures();
            // Mission scripts: run the authentic COB event handlers.
            try {
                std::string cobPath = mapSibling(".cob");
                auto roster = vtdf(mapSibling(".tdf"));
                for (const auto& name : roster.childOrder) {
                    std::string n = name;
                    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                    missionRoster_.push_back(n);
                    if (n == "verat" || n == "araat" || n == "tarat" || n == "zonat")
                        missionTowerIdx_ = int(missionRoster_.size()) - 1;
                }
                missionVm_ = std::make_unique<tak::cob::Vm>(tak::cob::load(vread(cobPath), cobPath));
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
                std::vector<uint8_t> tb;
                if (vhas(mapSibling(".txt"))) tb = vread(mapSibling(".txt"));
                std::istringstream bf(std::string(tb.begin(), tb.end()));
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
            std::string crtPath = mapSibling(".crt");
            auto placements = vhas(crtPath) ? tak::crt::load(vread(crtPath))
                                            : std::vector<tak::crt::Placement>{};
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
            loadFeatures();

            // Skirmish victory rule from the map's trigger section: a scoring
            // unit type, a scoring region, and a time limit.
            auto trig = vhas(crtPath) ? tak::crt::loadTriggers(vread(crtPath))
                                      : tak::crt::Triggers{};
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
        auto starts = parseStartPositions();
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
            for (auto& u : world_.units()) {
                if (!u.type || u.type->canMove) continue;
                tak::sim::blockFootprint(world_.nav(), *u.type, u.x, u.z, true);
            }
            return;
        }
        if (!bare) {
        const FactionKit& pk = kit(side);
        const FactionKit& ak = kit(aiSide);
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
        }
        }

        for (auto& u : world_.units()) {
            if (!u.type || u.type->canMove) continue;
            tak::sim::blockFootprint(world_.nav(), *u.type, u.x, u.z, true);
        }
    }

    // True while the multiplayer lobby is showing (before the game world exists).
    // Play the menu theme (track15) while in the lobby / not yet in a game, and the
    // faction playlist once the match starts. Called every frame; only (re)starts the
    // playlist on a state change so it doesn't restart the current track.
    int musicMode_ = 0;   // 0 = none yet, 1 = lobby, 2 = in-game
    bool discoWas_[8] = {};       // per-player disco state, to fire the track once on start
    bool headbangWas_[8] = {};    // ...and the headbang state

    // On the rising edge of a player's disco (Shift+D), play the 10s disco loop as a
    // positional SFX from each of that player's dancing monarchs (enemies only if in
    // view). It runs alongside the faction music -- a dance-floor track from the unit.
    void discoSound() {
        for (int p = 0; p < 8 && p < world_.numPlayers(); ++p) {
            bool on = world_.discoActive(p);
            if (on) {
                // Centroid of this player's (visible) dancing monarchs.
                float cx = 0, cz = 0; int n = 0;
                for (const auto& u : world_.units()) {
                    if (u.player != p || !u.alive() || !isMonarchType(u.type)) continue;
                    if (!alliedToLocal(p) && !noFog_ && !world_.cellVisible(u.x, u.z)) continue;
                    cx += u.x; cz += u.z; ++n;
                }
                if (n) {
                    cx /= n; cz /= n;
                    if (!discoWas_[p]) sounds_.discoAt(cx, cz);               // start the track
                    else sounds_.repositionWorld("disco", cx, cz);           // keep it panned
                }
            }
            discoWas_[p] = on;
        }
    }

    // Same as discoSound but for the Shift+H headbang -> the heavy-metal track.
    void headbangSound() {
        for (int p = 0; p < 8 && p < world_.numPlayers(); ++p) {
            bool on = world_.headbangActive(p);
            if (on) {
                float cx = 0, cz = 0; int n = 0;
                for (const auto& u : world_.units()) {
                    if (u.player != p || !u.alive() || !isMonarchType(u.type)) continue;
                    if (!alliedToLocal(p) && !noFog_ && !world_.cellVisible(u.x, u.z)) continue;
                    cx += u.x; cz += u.z; ++n;
                }
                if (n) {
                    cx /= n; cz /= n;
                    if (!headbangWas_[p]) sounds_.metalAt(cx, cz);
                    else sounds_.repositionWorld("metal", cx, cz);
                }
            }
            headbangWas_[p] = on;
        }
    }

    void manageMusic() {
        int want = inLobbyPhase() ? 1 : 2;
        if (want == musicMode_) return;
        musicMode_ = want;
        // The menu-launched front-end owns the lobby BGM (so it plays continuously
        // from the menu into the lobby); suppress ours there, but still switch to
        // faction music once the game proper begins.
        if (want == 1) { if (!externalLobbyMusic_) sounds_.startMusic(vfs_, {15}); }
        else sounds_.startMusic(vfs_, factionMusicTracks(side_));
    }

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
    // Single-player from the menu: it's a private local game, so open the lobby on
    // the Create screen (the browser is empty by design) and mark it single-player
    // (labels change, no password / no game browser).
    void setSinglePlayer() { lobbyScreen_ = LobbyScreen::Create; singlePlayer_ = true; }
    // Return-to-menu request: a lobby/in-game action sets this; main()'s outer loop
    // tears the session down and re-shows the front-end menu.
    void requestMenu() { menuRequested_ = true; }
    bool menuRequested() const { return menuRequested_; }
    bool quitRequested() const { return quitRequested_; }   // in-game QUIT -> exit app
    // Menu-launched sessions: the front-end owns the lobby BGM (see manageMusic).
    void setExternalLobbyMusic() { externalLobbyMusic_ = true; }
    // Menu-launched sessions can return to the front-end, so the in-game menu
    // offers MAIN MENU; a direct CLI game only offers RESUME/QUIT.
    void setCanReturnToMenu() { canReturnToMenu_ = true; }

    // Apply local Options (audio / camera / UI scale) live -- at startup and
    // whenever the in-game Options screen changes a value. Never touches the sim.
    void applySettings(const tak::Settings& s) {
        sounds_.setMasterVolume(s.masterVol);
        sounds_.setMusicVolume(s.bgmVol);
        sounds_.setSfxVolume(s.sfxVol);
        for (int i = 0; i < sounds_.channelCount(); ++i) sounds_.setChannelGain(i, s.chanGain[i]);
        mapView_.setZoomSpeed(s.mouseZoomSpeed);
        edgeScrollSpeed_ = s.edgeScrollSpeed;
        edgeScrollOn_ = s.edgeScroll;
        uiScale_ = s.uiScale;
    }

    // main()'s live settings, so the in-game Options screen can edit + persist them.
    void setSettings(tak::Settings* s) { settings_ = s; }

    // Open the in-game Options overlay (from the Esc menu). onChange applies audio,
    // camera, UI scale and window state live; the host saves on close.
    void openOptions() {
        if (!settings_) return;
        options_ = std::make_unique<tak::OptionsScreen>(ren_, *settings_,
            [this] {
                applySettings(*settings_);
                if (SDL_Window* w = SDL_RenderGetWindow(ren_))
                    SDL_SetWindowFullscreen(w, settings_->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                SDL_RenderSetVSync(ren_, settings_->vsync ? 1 : 0);
            },
            [this] { saveSettings(*settings_); },
            sounds_.channelCount());
    }
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
        int n = int(parseStartPositions().size());
        return uint8_t(std::clamp(n < 2 ? 2 : n, 2, tak::net::kMaxSlots));
    }

    void input(const SDL_Event& e, int winW, int winH) {
        winW_ = winW;
        winH_ = winH;
        if (inLobbyPhase()) { lobbyInput(e, winW, winH); return; }
        // Options overlay (opened from the Esc menu) takes all input while up.
        if (options_) {
            if (options_->input(e, winW, winH)) options_.reset();   // BACK / Esc (SAVE is explicit)
            return;
        }
        // In-game exit menu (opened with Esc). While it's up, all game input is
        // swallowed and only its own buttons / Esc respond. The sim keeps running
        // underneath -- a true pause isn't possible in the lockstep MP model (even
        // local single-player runs on a private server). Buttons fire on release;
        // hit-rects come from the last draw (see the overlay in draw()).
        if (exitMenu_) {
            if (e.type == SDL_MOUSEMOTION) { mouseX_ = float(e.motion.x); mouseY_ = float(e.motion.y); }
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) exitMenu_ = false;
            else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                float mx = float(e.button.x), my = float(e.button.y);
                for (auto& [r, action] : exitHots_)
                    if (mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h) { action(); break; }
            }
            return;
        }
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
            // Escape cancels a pending placement/order first; a stray selection is
            // cleared; otherwise it opens the in-game menu. Once the game is over it
            // returns to the front-end menu directly.
            if (outcome_ != 0) { menuRequested_ = true; }
            else if (placing_ || pendingCmd_) { placing_ = nullptr; pendingCmd_ = 0; }
            else if (!selection_.empty()) selection_.clear();
            else exitMenu_ = true;
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
                   buildIconClick(float(e.button.x), float(e.button.y),
                                  e.button.button == SDL_BUTTON_LEFT,
                                  e.button.button == SDL_BUTTON_RIGHT)) {
            // conjure/build icon (bottom-left, above the bar) handled
        } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                   e.button.y > winH - barH()) {
            // bottom bar: build icons already handled above; swallow the rest so a stray
            // click on the bar chrome doesn't deselect / order into the world
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   (gui_.gadgets.empty()
                        ? orderColumnClick(float(e.button.x), float(e.button.y), winW)
                        : guiClick(float(e.button.x), float(e.button.y)))) {
            // command-panel button handled
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   minimapArmedOrder(float(e.button.x), float(e.button.y), winW, winH)) {
            // An armed order (F/M/A/P/G) + minimap click issues that order at the
            // clicked location, instead of moving the camera there.
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   minimapClick(float(e.button.x), float(e.button.y), winW, winH)) {
            draggingMinimap_ = true;   // camera follows the drag until release
            trackSel_ = false;
        } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                   e.button.x > mapViewW(winW) && e.button.y < winH - barH()) {
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
            // Placement pick: a building takes the flat cell under the cursor (so the
            // green/red square sits under the mouse and the flat-rendered building
            // lands there); a conjured unit uses the height-aware pick so it drops on
            // the elevated cell drawn under the cursor, not the low cell behind.
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
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT &&
                   reclaimDrag_) {
            reclaimDrag_ = false;
            bool queue = (SDL_GetModState() & KMOD_SHIFT) != 0;
            float ex = float(e.button.x), ey = float(e.button.y), ewx, ewz;
            pickWorld(ex, ey, ewx, ewz);
            // A tiny drag was really a click -> the ordinary contextual order.
            if (std::fabs(ex - rdSx0_) < 6.0f && std::fabs(ey - rdSy0_) < 6.0f)
                rightClickOrder(ewx, ewz, queue);
            else
                issueReclaimBox(rdX0_, rdZ0_, ewx, ewz, queue);
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT &&
                   placing_) {
            placing_ = nullptr;
            buildDrag_ = false;
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   !spectating_) {   // a spectator watches only -- no unit selection
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
            // A mobile reclaimer selected: arm a right-drag "clear this area" box and
            // defer the normal order to button-up, so a plain click still works.
            if (haveReclaimer()) {
                reclaimDrag_ = true;
                rdX0_ = wx; rdZ0_ = wz;
                rdSx0_ = float(e.button.x); rdSy0_ = float(e.button.y);
                return;
            }
            rightClickOrder(wx, wz, (SDL_GetModState() & KMOD_SHIFT) != 0);
            return;
        }
    }

    // Contextual right-click order, extracted so the reclaim right-drag can defer to
    // it on a plain click: transport unload/load, assist/guard, attack, or move.
    void rightClickOrder(float wx, float wz, bool queue) {
        if (selection_.empty()) return;
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
            // Right-clicking a friendly unit is contextual, never a move:
            //  - a conjure-in-progress: selected builders that can build that type
            //    resume/assist it (revives a decaying site); other units do nothing.
            //  - any other friendly unit: selected units that can attack guard it;
            //    the rest do nothing.
            // Either case consumes the click (no move fallthrough).
            {
                int siteId = -1;   float bestSite = 1e18f;
                int allyId = -1;   float bestAlly = 22.0f * 22.0f;
                for (auto& u : world_.units()) {
                    if (!u.alive() || u.embarked() || !u.type || !first ||
                        !world_.allied(u.player, first->player)) continue;
                    float dx = u.x - wx, dz = u.z - wz, d = dx * dx + dz * dz;
                    if (u.underConstruction) {
                        // Any allied conjure (yours or a teammate's) can be revived.
                        float r = 20.0f + 8.0f * float(std::max(u.type->footX, u.type->footZ));
                        if (d < r * r && d < bestSite) { bestSite = d; siteId = u.id; }
                    } else if (d < bestAlly) { bestAlly = d; allyId = u.id; }
                }
                if (siteId >= 0) {
                    const auto* st = world_.unit(siteId);
                    bool any = false;
                    for (int id : selection_) {
                        const auto* bu = world_.unit(id);
                        if (!bu || !bu->type || !bu->type->isBuilder) continue;
                        const auto& menu = registry_.buildable(bu->type->id);
                        if (std::find(menu.begin(), menu.end(), st->type->id) == menu.end())
                            continue;
                        tak::net::Command c;
                        c.kind = tak::net::Cmd::Assist;
                        c.unitId = id; c.targetId = siteId; c.queue = queue;
                        issue(c);
                        any = true;
                    }
                    if (any) voice(selection_.front(), "move");
                    return;   // non-builders / can't-build-it: nothing happens
                }
                if (allyId >= 0) {
                    bool any = false;
                    for (int id : selection_) {
                        const auto* gu = world_.unit(id);
                        if (!gu || !gu->type || gu->type->weapon.damage <= 0) continue;
                        tak::net::Command c;
                        c.kind = tak::net::Cmd::Guard;
                        c.unitId = id; c.targetId = allyId; c.queue = queue;
                        issue(c);
                        any = true;
                    }
                    if (any) voice(selection_.front(), "move");
                    return;   // non-attackers: nothing happens
                }
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
            // A reclaimer clicking directly on a reclaimable feature (with no enemy
            // there) reclaims just that one -- retail's single Reclaim.
            if (enemy < 0 && haveReclaimer()) {
                int fid = -1; float bestF = 1e18f;
                for (const auto& f : world_.features()) {
                    if (!f.alive) continue;
                    float dx = f.x - wx, dz = f.z - wz, d = dx * dx + dz * dz;
                    float r = 18.0f + 8.0f * float(std::max(f.fx, f.fz));
                    if (d < r * r && d < bestF) { bestF = d; fid = f.id; }
                }
                if (fid >= 0) {
                    int builderId = firstReclaimer();
                    tak::net::Command c;
                    c.kind = tak::net::Cmd::Reclaim;
                    c.unitId = builderId;
                    c.targetId = fid;
                    c.queue = uint8_t(queue ? 1 : 0);
                    issue(c);
                    voice(builderId, "move");
                    return;
                }
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

    // A mobile, reclaim-capable builder of ours is selected (drives the right-drag).
    bool haveReclaimer() {
        for (int id : selection_)
            if (const auto* u = world_.unit(id))
                if (u->alive() && u->type && u->type->isBuilder && u->type->canMove &&
                    u->type->canReclaim && u->player == localPlayer_)
                    return true;
        return false;
    }
    int firstReclaimer() {
        for (int id : selection_)
            if (const auto* u = world_.unit(id))
                if (u->alive() && u->type && u->type->isBuilder && u->type->canMove &&
                    u->type->canReclaim && u->player == localPlayer_)
                    return id;
        return 0;
    }
    // Right-drag "clear this area": order the builder to reclaim every reclaimable
    // feature in the box, nearest-first (approximating retail's greedy re-scan).
    void issueReclaimBox(float x0, float z0, float x1, float z1, bool queue) {
        int builderId = firstReclaimer();
        const auto* b = world_.unit(builderId);
        if (!b) return;
        float minx = std::min(x0, x1), maxx = std::max(x0, x1);
        float minz = std::min(z0, z1), maxz = std::max(z0, z1);
        std::vector<std::pair<float, int>> targets;
        for (const auto& f : world_.features()) {
            if (!f.alive || f.x < minx || f.x > maxx || f.z < minz || f.z > maxz) continue;
            float dx = f.x - b->x, dz = f.z - b->z;
            targets.push_back({dx * dx + dz * dz, f.id});
        }
        std::sort(targets.begin(), targets.end());
        bool first = true;
        for (auto& [d, fid] : targets) {
            tak::net::Command c;
            c.kind = tak::net::Cmd::Reclaim;
            c.unitId = builderId;
            c.targetId = fid;
            c.queue = uint8_t((first && !queue) ? 0 : 1);   // first clears, rest append
            issue(c);
            first = false;
        }
        if (!targets.empty()) {
            notice_ = "RECLAIM " + std::to_string(targets.size());
            noticeTimer_ = 2;
            voice(builderId, "move");
        }
    }

    // Attach the multiplayer client. The game world is set up later, from the
    // server's GameStarting (startMpGame), not from the constructor.
    void setMpClient(tak::net::MpClient* mp) {
        mp_ = mp;
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
        // The game's balance AND override tier are the ROOM's (lobby choices), which
        // may differ from how this client was launched. Remount to the room's tier
        // (so gameplay overrides -- or their absence -- match the referee) and
        // rebuild the registry, so our world and hash agree with the server's
        // referee. remountPolicy already rebuilds the registry for the current
        // crusades setting; handle a crusades-only change separately.
        remountPolicy(room.opts.overridePolicy);
        if ((room.opts.crusades != 0) != crusades_) {
            crusades_ = room.opts.crusades != 0;
            registry_ = tak::sim::TypeRegistry{};
            tak::sim::setupRegistry(registry_, vfs_, crusades_);
        }
        tak::sim::MatchConfig cfg;
        cfg.vfs = &vfs_;
        cfg.mapPath = mapPath_;
        cfg.gods = room.opts.gods != 0;
        cfg.unitCap = room.opts.unitCap;
        cfg.slots.resize(size_t(maxSlot + 1));
        for (int i = 0; i <= maxSlot; ++i) {
            const auto& s = room.slots[i];
            cfg.slots[size_t(i)] = {s.type == 1 || s.type == 2, s.faction % 5, s.team};
            colorSlot_[i & 7] = s.color % 10;
            playerAi_[i & 7] = (s.type == 2);
            playerName_[i & 7] = !s.name.empty()
                                     ? s.name   // human name, or the AI's random name
                                     : s.type == 2 ? ("AI " + std::to_string(i + 1))
                                                   : ("Player " + std::to_string(i + 1));
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
        loadGui(side_);
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
        auto simTick = [&] {
            for (const auto& c : bd.cmds) apply(c);
            for (const auto& e : bd.events) applyEvent(e);
            update(1.0f / 30.0f);
            // Spectators observe only -- they seat no player and report no hash.
            if (netTick_ % 30 == 0 && !mp_->isSpectator())
                mp_->sendHash(netTick_, world_.stateHash());
            ++netTick_;
            ++drained;
        };
        if (netDelay_ == -2) {   // one-time init from the env
            // The adaptive jitter buffer is ON by default: it only ever reduces
            // stalls and is pure pacing (byte-identical sim). TAK_NET_DELAY overrides
            // -- "0" disables it (drain every bundle immediately), a positive integer
            // pins a fixed reserve depth, "auto" (or unset) self-sizes to the link.
            const char* e = std::getenv("TAK_NET_DELAY");
            if (!e || std::string(e) == "auto") { netAuto_ = true; netDelay_ = 3; mp_->enableRttProbe(); }
            else netDelay_ = std::max(0, std::atoi(e));
        }
        if (netAuto_) {
            // Size the buffer to cover the measured bundle-arrival jitter, with an
            // RTT-scaled floor, clamped. Recomputed each frame so it tracks the link.
            int kJit = int(std::ceil(mp_->arrivalJitterMs() / (1000.0f / 30.0f)));
            int kRtt = int(std::ceil(mp_->rttMs() / 60.0f));   // gentle RTT floor
            netDelay_ = std::clamp(2 + std::max(kJit, kRtt), 2, 16);
        }
        if (netDelay_ <= 0) {
            // Default: drain to the newest delivered bundle every frame.
            while (outcome_ == 0 && drained < 512 && mp_->takeBundle(netTick_, bd)) simTick();
            // Stall metric: 0 ticks played this frame while future bundles ARE
            // buffered means the one we need is late (head-of-line block) -- a stall.
            ++netBenchFrames_;
            if (drained == 0 && mp_->bufferedBundles() > 0) ++netBenchStalls_;
        } else {
            // Jitter buffer: fill an initial reserve of netDelay_ bundles, then pace
            // the sim on the wall clock at ~30 Hz, staying that many bundles behind
            // the newest received. A late bundle is covered from the reserve; only a
            // gap deeper than the reserve stalls. Same bundles, same order -> the sim
            // and every hash are byte-identical, so this is a pure pacing change.
            uint64_t now = SDL_GetTicks64();
            float dt = netStepMs_ ? std::min(0.25f, float(now - netStepMs_) / 1000.0f) : 0.0f;
            netStepMs_ = now;
            int buffered = int(mp_->bufferedBundles());
            if (!netBufReady_) {
                if (buffered < netDelay_) return true;   // still filling the reserve
                netBufReady_ = true; netAccum_ = 0;
            }
            // Adaptive playout: run the sim clock slightly fast/slow to servo the
            // buffer depth to the target (netDelay_). Too deep -> play a touch faster
            // (drain toward target, shed latency); too shallow -> slower (rebuild the
            // reserve). Holds the added latency tight at ~netDelay_ ticks.
            float err = float(buffered - netDelay_);
            // Base playout tracks the game speed (server ticks that much faster/slower).
            float sp = std::max(1, int(mp_->gameSpeed())) / 10.0f;
            float rate = 30.0f * sp * std::clamp(1.0f + 0.06f * err, 0.7f, 1.3f);
            netAccum_ += dt * rate;            // accumulates fractional TICKS now
            int budget = int(netAccum_);
            netAccum_ -= float(budget);
            // A deep backlog (rejoin replay, or the client fell behind) is NOT jitter
            // -- fast-forward it back down to the target reserve instead of pacing.
            if (buffered > netDelay_ + 60) budget = 512;
            budget = std::min(budget, 512);
            while (outcome_ == 0 && drained < budget && mp_->takeBundle(netTick_, bd)) simTick();
            // Stall metric: the wall clock wanted more ticks than we could play
            // because the next bundle isn't buffered yet (jitter exceeded the
            // reserve). One count per starved frame.
            ++netBenchFrames_;
            if (drained < budget && !mp_->haveBundle(netTick_)) ++netBenchStalls_;
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
    void startReplay(tak::sim::MatchConfig cfg,
                     std::vector<tak::net::Bundle> bundles) {
        // Read through this view's own VFS (we own it), not a caller pointer.
        cfg.vfs = &vfs_;
        cfg.mapPath = mapPath_;
        auto spots = tak::sim::setupMatch(world_, registry_, cfg);
        replayBundles_ = std::move(bundles);
        replayMode_ = true;
        noFog_ = true;             // a spectator sees the whole map
        localPlayer_ = 0;
        world_.setVisPlayer(0);
        side_ = "ara";
        loadPanel(side_);
        loadGui(side_);
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
        if (st == S::Lobby && (autoMode == 1 || autoMode == 4 || autoMode == 7)) {
            tak::net::GameOptions o; o.crusades = crusades ? 1 : 0;
            o.overridePolicy = uint8_t(policy_);   // room tier = this host's launch tier
            // TAK_SPEED: set the game speed in tenths (10 = 1x) for headless timing
            // tests -- re-cadences the server without touching the (deterministic) sim.
            if (const char* sp = std::getenv("TAK_SPEED")) o.speed = uint8_t(std::clamp(std::atoi(sp), 1, 40));
            // TAK_MP_WATCH: host creates the game as a spectator (no slot) so every
            // slot can be an AI -- an all-AI game to watch.
            bool watch = autoMode == 1 && std::getenv("TAK_MP_WATCH");
            // Mode 7 is interactive SINGLE-PLAYER: a private game (hidden from the
            // browser) with one server-run AI opponent.
            bool priv = autoMode == 7;
            mp_->createGame(priv ? "Single Player" : "headless", "", mapId, o,
                            mpCapacity(), watch, priv);
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
            o.overridePolicy = uint8_t(policy_);   // room tier = this host's launch tier
                mp_->createGame(mapId, "", mapId, o, mpCapacity());
            }
            if (!mpFirstListMs_) mpFirstListMs_ = SDL_GetTicks64();
        } else if (st == S::InRoom && autoMode && !mpReadied_) {
            const auto& r = mp_->room();
            // Host-spectator (TAK_MP_WATCH): seat AIs in the LOW slots (0..N-1) and
            // don't seat self -- an all-AI game the host just watches.
            if (autoMode == 1 && r.mySlot < 0 && std::getenv("TAK_MP_WATCH")) {
                const char* ai = std::getenv("TAK_MP_AIS");
                int nAi = std::clamp(ai ? std::atoi(ai) : 2, 2, int(tak::net::kMaxSlots));
                for (int k = 0; k < nAi; ++k)
                    mp_->setSlot(k, 2, uint8_t(k % 5), uint8_t(k), uint8_t(k), 1);
                mpReadied_ = true;
            } else if (autoMode == 7 && r.mySlot >= 0) {
                // Single-player: seat self UNREADY and hand off to the interactive
                // Room, where the player adds one or more AI opponents, then readies
                // up and starts.
                mp_->setSlot(r.mySlot, 1, facIdx(side_), uint8_t(r.mySlot),
                             uint8_t(r.mySlot), 0);
                // Headless test hook: TAK_SP_AIS=N seats N AI opponents and starts
                // immediately (the interactive path leaves this to the player).
                if (const char* na = std::getenv("TAK_SP_AIS")) {
                    // The auto-start hook must ready-up the host (interactive SP now
                    // seats unready, which would otherwise block startGame()).
                    mp_->setSlot(r.mySlot, 1, facIdx(side_), uint8_t(r.mySlot),
                                 uint8_t(r.mySlot), 1);
                    int n = std::clamp(std::atoi(na), 1, int(tak::net::kMaxSlots) - 1);
                    for (int k = 0; k < n && k + 1 < int(tak::net::kMaxSlots); ++k) {
                        int slot = k + 1;
                        mp_->setSlot(slot, 2, uint8_t((facIdx(aiSide_) + k) % 5),
                                     uint8_t(slot), uint8_t(slot), 1);
                    }
                    mp_->startGame();
                    mpStarted_ = true;
                }
                mpReadied_ = true;
            } else if (r.mySlot >= 0) {
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
            // (mode 7 single-player does NOT auto-start: the player adds AIs and
            //  clicks START in the Room.)
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
                mp_->reportLoaded(gameDataHash());
                writeResume(mp_->gameId(), mp_->resumeToken());
            }
            mpSetupDone_ = true;
            std::fprintf(stderr, spec ? "spectating -- replaying to catch up...\n"
                                      : "rejoined -- replaying to catch up...\n");
            mpStep();   // drain the replay this frame
        } else if (mp_->starting() && !mpSetupDone_) {
            startMpGame(mp_->startRoom(), mp_->startSeed());
            if (mp_->isSpectator()) {
                // Host-spectator (create-as-spectator): watch-only, no fog, no slot,
                // no resume ticket, and nothing to report loaded.
                spectating_ = true;
                noFog_ = true;
            } else {
                mp_->reportLoaded(gameDataHash());
                writeResume(mp_->gameId(), mp_->resumeToken());   // reconnect ticket
            }
            mpSetupDone_ = true;
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
    void selectOnly(int id) { if (spectating_) return; selection_.clear(); selection_.push_back(id); }
    const std::string& netError() const { return netError_; }

    void setFollow(float zoom) { follow_ = true; mapView_.setZoom(zoom); }

    void amphibDemo() {
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
        int id = spawn("araarch", 900, 1000, 0, 0);
        selection_ = {id};
        for (int i = 0; i < 8; i++) voice(id, "move");
    }
    void faceTest() {
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
        float cx = mapView_.map().blocksX * 16.0f, cz = mapView_.map().blocksY * 16.0f;
        int a = spawn("araarch", cx - 40, cz, 1.57f, 0);
        int e = spawn("tararch", cx + 200, cz, -1.57f, 1);
        world_.attack(a, e, false);
        mapView_.setOffset(cx - 640 / mapView_.zoom(), cz - 400 / mapView_.zoom());
    }
    void lodeTest() {
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
        if (winW_ <= 0 || winH_ <= 0 || !edgeScrollOn_) return;
        if (mouseX_ < 0 || mouseX_ > winW_ || mouseY_ < 0 || mouseY_ > winH_) return;
        const float margin = 24.0f, panPx = 2000.0f * edgeScrollSpeed_;   // px/s at zoom 1
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

    // Real-time per-RENDER-frame work: camera and audio that must stay smooth
    // regardless of the deterministic sim. In a net game the sim (update()) only
    // advances when a server bundle arrives, so anything lag-sensitive that isn't
    // "game state" belongs here, not in update() -- panning, zoom-follow, edge
    // scroll, camera shake, and music keep running even while the sim is stalled
    // waiting on the network. Called every frame in SP and net alike.
    void cameraFrame(float dt) {
        sounds_.pollMusic();
        if (inLobbyPhase()) return;
        edgeScroll(dt, std::max(mapView_.zoom(), 1e-3f));   // cursor-at-edge pan (real time)
        if (trackSel_ && !centerOnSelection()) trackSel_ = false;   // follow selection
        if (shakeTime_ > 0) shakeTime_ = std::max(0.0f, shakeTime_ - dt);   // shake decay
    }
    void update(float dt) {
        if (paused_) return;   // freeze the sim; input/render/camera keep running
        // Game speed: scale game time (sim, effects, AI, animation all follow dt).
        dt *= speedMult();
        double _sim0 = double(SDL_GetPerformanceCounter());
        // Advance the sim in sub-steps capped at 1/30s so fast speeds (or a laggy
        // frame) can't move a unit far enough to tunnel a wall; effects/AI below use
        // the full scaled dt (they only interpolate, so a big step is harmless).
        for (float rem = dt, guard = 0; rem > 1e-5f && guard < 16; ++guard) {
            float step = std::min(rem, 1.0f / 30.0f);
            world_.tick(step);
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
        // Kick off the summon fade-in/shimmer for anything just conjured from a
        // building (the producer flags justBuilt for that one tick); then age the
        // active effects and drop finished or dead ones. Cosmetic, viewer-only.
        for (auto& u : world_.units())
            if (u.justBuilt && !birthFx_.count(u.justBuilt)) birthFx_[u.justBuilt] = 0.0f;
        for (auto it = birthFx_.begin(); it != birthFx_.end();) {
            const tak::sim::Unit* bu = world_.unit(it->first);
            it->second += dt;
            if (it->second >= kBirthFxDur || !bu || !bu->alive()) it = birthFx_.erase(it);
            else ++it;
        }
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
        // (T-tracking / edge-scroll / shake now run per-frame in cameraFrame, so
        // the camera stays smooth when the net sim stalls.)
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
        for (auto& [id, a] : anims_) {
            if (a.vm) vmTick_.push_back(a.vm.get());
            a.fireT += dt; a.smokeT += dt;   // age since last emit (fire fades if it stops)
        }
        pool_.parallelFor(vmTick_.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) vmTick_[i]->tick(dt);
        });
        // Drain emit-sfx the VMs stashed (fire/smoke from FireControl-style loops),
        // now serially on the main thread, into the world-space effect system.
        for (auto& [id, a] : anims_) {
            if (a.pendingSfx.empty()) continue;
            const auto* u = world_.unit(id);
            if (u && u->type && (noFog_ || world_.cellVisible(u->x, u->z)))
                for (auto& [piece, sfx] : a.pendingSfx) emitSfx(*u, a, piece, sfx);
            a.pendingSfx.clear();
        }
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
        manageMusic();
        discoSound();     // fire the disco track from a monarch when its player starts dancing
        headbangSound();  // ...and the metal track on headbang
        if (inLobbyPhase()) {
            // The lobby always lays out at its design size (kLobbyW x kLobbyH logical)
            // and is scaled to fit + centred in the window -- so it shows fully at any
            // window size or aspect (never clustered top-left, never clipped). Renders
            // large for legible text; lbHot/lobbyInput undo the scale + centre offset.
            lobbyScale_ = std::min(winW / kLobbyW, winH / kLobbyH);
            float lw = winW / lobbyScale_, lh = winH / lobbyScale_;   // logical window
            lobbyOffX_ = std::floor((lw - kLobbyW) * 0.5f);
            lobbyOffY_ = std::floor((lh - kLobbyH) * 0.5f);
            // Darker surround + a thin frame so the centred lobby reads as a panel.
            // (SDL_RenderClear ignores the viewport, so the ground-clear lives here,
            // not in drawLobby.) Drawn in pixel space (scale 1, no viewport).
            SDL_SetRenderDrawColor(ren_, 8, 9, 13, 255);
            SDL_RenderClear(ren_);
            float px = lobbyOffX_ * lobbyScale_, py = lobbyOffY_ * lobbyScale_;
            float pw = kLobbyW * lobbyScale_, ph = kLobbyH * lobbyScale_;
            SDL_FRect panelBg{px, py, pw, ph};
            SDL_SetRenderDrawColor(ren_, 16, 18, 26, 255);
            SDL_RenderFillRectF(ren_, &panelBg);
            SDL_FRect frame{px - 2, py - 2, pw + 4, ph + 4};
            SDL_SetRenderDrawColor(ren_, 60, 66, 90, 255);
            SDL_RenderDrawRectF(ren_, &frame);
            // Content: scaled + viewport-offset so it draws inside the centred panel.
            SDL_Rect vp{int(lobbyOffX_), int(lobbyOffY_), int(kLobbyW), int(kLobbyH)};
            SDL_RenderSetScale(ren_, lobbyScale_, lobbyScale_);
            SDL_RenderSetViewport(ren_, &vp);
            drawLobby(int(kLobbyW), int(kLobbyH));
            SDL_RenderSetViewport(ren_, nullptr);
            SDL_RenderSetScale(ren_, 1.0f, 1.0f);
            return;
        }
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
            if (!world_.featureAliveAt(f.x, f.z)) continue;   // reclaimed away by a builder
            int cx = int(f.x) / 16, cz = int(f.z) / 16;
            if (!noFog_ && !vis.empty() && (cx < 0 || cz < 0 || cx >= vw ||
                                 vis[size_t(cz) * vw + cx] == 0))
                continue;   // unexplored
            // Cull on the LIFTED position (features lift onto the relief like units).
            float sx = (f.x - mapView_.offX()) * zm0 - terrainLiftX(f.x, f.z) * zm0;
            float sy = (f.z - mapView_.offY()) * zm0 - terrainLift(f.x, f.z) * zm0;
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
            // Cull on the LIFTED anchor (where the unit is actually drawn), else a unit
            // lifted onto the screen from just below the edge on high ground vanishes.
            float sx = (u.x - mapView_.offX()) * zm0 - uLiftX(u) * zm0;
            float sy = (u.z - mapView_.offY()) * zm0 - uLiftY(u) * zm0;
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
        bool builtGlow = false;
        for (const auto* u : visUnits_) {
            atlasFor(colorSlot_[u->player & 7]);
            if (!u->underConstruction)
                if (auto it = anims_.find(u->id);
                    it != anims_.end() && it->second.usesGlow)
                    builtGlow = true;
        }
        // Cycle lodestone/mana/fire crystal frames -- but only once a built glow-unit
        // is on screen, so a still-conjuring lodestone stays dark until it's finished.
        animateGlowTextures(builtGlow);
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
            return occluded || conjuring || dancing(u) || headbanging(u);   // draw their glow
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
                // Lift the decal onto the terrain relief just like a unit, so a mana
                // deposit sits at the height its heightmap claims (and lodestones/units
                // built on it line up) instead of the decal being flat.
                float lfx = terrainLiftX(f.x, f.z) * zm0, lfy = terrainLift(f.x, f.z) * zm0;
                if (f.shadow) {
                    SDL_FRect sd{(f.x - mapView_.offX() - float(f.sxoff)) * zm0 - lfx,
                                 (f.z - mapView_.offY() - float(f.syoff)) * zm0 - lfy,
                                 float(f.sw) * zm0, float(f.sh) * zm0};
                    SDL_RenderCopyF(ren_, f.shadow, nullptr, &sd);
                }
                SDL_FRect dst{(f.x - mapView_.offX() - float(f.xoff)) * zm0 - lfx,
                              (f.z - mapView_.offY() - float(f.yoff)) * zm0 - lfy,
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
        drawUnitFx();

        drawFog();
        if (buildDrag_ && placing_) {
            float mx, mz;
            pickWorld(mouseX_, mouseY_, mx, mz);
            for (auto& [x, z] : buildLinePositions(bdX0_, bdZ0_, mx, mz))
                drawGhostAt(placing_, x, z, !world_.canPlace(placing_, x, z));
        } else if (placing_) {
            drawGhost();
        }
        if (reclaimDrag_) {
            // Screen-space "clear this area" box, with a marker on every reclaimable
            // feature it currently catches.
            float zm = mapView_.zoom();
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            float x0 = std::min(rdSx0_, mouseX_), y0 = std::min(rdSy0_, mouseY_);
            float x1 = std::max(rdSx0_, mouseX_), y1 = std::max(rdSy0_, mouseY_);
            SDL_FRect box{x0, y0, x1 - x0, y1 - y0};
            SDL_SetRenderDrawColor(ren_, 255, 170, 40, 40);
            SDL_RenderFillRectF(ren_, &box);
            SDL_SetRenderDrawColor(ren_, 255, 190, 70, 220);
            SDL_RenderDrawRectF(ren_, &box);
            float mx, mz;
            pickWorld(mouseX_, mouseY_, mx, mz);
            float minx = std::min(rdX0_, mx), maxx = std::max(rdX0_, mx);
            float minz = std::min(rdZ0_, mz), maxz = std::max(rdZ0_, mz);
            SDL_SetRenderDrawColor(ren_, 255, 210, 90, 230);
            for (const auto& f : world_.features()) {
                if (!f.alive || f.x < minx || f.x > maxx || f.z < minz || f.z > maxz) continue;
                float fsx = (f.x - mapView_.offX()) * zm - terrainLiftX(f.x, f.z) * zm;
                float fsy = (f.z - mapView_.offY()) * zm - terrainLift(f.x, f.z) * zm;
                SDL_FRect m{fsx - 4, fsy - 4, 8, 8};
                SDL_RenderDrawRectF(ren_, &m);
            }
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
                // Size the brackets to the unit's footprint (world half-extent =
                // foot cells * 8) so a building is boxed at its real size, not a single
                // cell; a small floor keeps mobile units at the old marker size.
                float halfX = std::max(std::max(u.type->footX, 1) * 8.0f, 11.0f);
                float halfZ = std::max(std::max(u.type->footZ, 1) * 8.0f, 8.0f);
                float rx = halfX * zms, ry = halfZ * zms;
                if (rx < 9.0f) {
                    // Tiny on screen (a whole army zoomed out): one small marker
                    // quad instead of eight bracket segments -- 8x less geometry.
                    float s = std::max(2.0f, rx * 0.6f);
                    pushQuad(shadowBatch_, cx - s, cy - s * 0.65f, 2 * s, 2 * s * 0.65f, grn);
                    continue;
                }
                float Lx = std::max(3.0f, rx * 0.4f), Ly = std::max(3.0f, ry * 0.4f);
                float th = std::max(1.0f, 1.2f * zms);
                for (int sx = -1; sx <= 1; sx += 2)
                    for (int sy = -1; sy <= 1; sy += 2) {
                        float px = cx + sx * rx, py = cy + sy * ry;
                        pushQuad(shadowBatch_, std::min(px, px - sx * Lx), py - th * 0.5f,
                                 Lx, th, grn);
                        pushQuad(shadowBatch_, px - th * 0.5f,
                                 std::min(py, py - sy * Ly), th, Ly, grn);
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
                if (!t || !t->alive() || !t->type) continue;
                SDL_FPoint p = unitScreen(*t);   // includes flyer altitude
                float cx = p.x, cy = p.y + 12.0f * zms;   // undo unitScreen's body bias
                if (cx < -40 || cx > mvw + 40 || cy < -40 || cy > winH + 40) continue;
                // Match the green selection brackets: sized to the target's footprint.
                float halfX = std::max(std::max(t->type->footX, 1) * 8.0f, 11.0f);
                float halfZ = std::max(std::max(t->type->footZ, 1) * 8.0f, 8.0f);
                float rx = halfX * zms, ry = halfZ * zms;
                float Lx = std::max(3.0f, rx * 0.4f), Ly = std::max(3.0f, ry * 0.4f);
                float th = std::max(1.0f, 1.2f * zms);
                for (int sx = -1; sx <= 1; sx += 2)
                    for (int sy = -1; sy <= 1; sy += 2) {
                        float px = cx + sx * rx, py = cy + sy * ry;
                        pushQuad(shadowBatch_, std::min(px, px - sx * Lx), py - th * 0.5f,
                                 Lx, th, red);
                        pushQuad(shadowBatch_, px - th * 0.5f,
                                 std::min(py, py - sy * Ly), th, Ly, red);
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
        SDL_FRect panelStrip{float(mvw), 0, float(winW - mvw), float(winH) - barH()};
        SDL_RenderFillRectF(ren_, &panelStrip);
        drawMinimap(winW, winH);
        renderGui(winW, winH);

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
            // Dev net-status readout (TAK_NETDEBUG): off by default -- it sat over the
            // bottom-right mana panel. The BEHIND-BY lag warning below always shows.
            static const bool kNetDebug = std::getenv("TAK_NETDEBUG") != nullptr;
            if (kNetDebug) {
                char nb[64];
                std::snprintf(nb, sizeof nb, "NET P%d  TICK %u", localPlayer_ + 1, netTick_);
                hudFont_.draw(ren_, nb, 12, float(winH) - barH() - 16, 1.4f,
                              {140, 200, 255, 255});
            }
            // "Behind by N s": how far this client's view lags the live game --
            // the depth of received-but-unplayed bundles (30 Hz). The adaptive
            // buffer keeps only a tiny intended reserve (~netDelay_ ticks, <0.2 s),
            // so surface this only once the lag is clearly abnormal: the machine
            // can't keep up, or a link stall is draining faster than it refills.
            // Escalates amber -> red toward the ~10 s server reconnect threshold.
            float behindSec = float(mp_->bufferedBundles()) / float(tak::net::kServerHz);
            if (behindSec > 0.75f && outcome_ == 0 && !paused_) {
                char bb[48];
                std::snprintf(bb, sizeof bb, "BEHIND BY %.1fs", behindSec);
                float tw = float(hudFont_.width(bb, 1.8f));
                float t = std::min(1.0f, behindSec / 10.0f);
                SDL_Color col{255, uint8_t(210 - int(150 * t)), uint8_t(90 - int(60 * t)), 255};
                hudFont_.draw(ren_, bb, (float(winW) - tw) / 2, 78, 1.8f, col);
            }
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
                              : pendingCmd_ == 'c' ? "RECLAIM: CLICK FEATURE"
                              : pendingCmd_ == 'r' ? "REPAIR: CLICK DAMAGED UNIT"
                              : pendingCmd_ == 'l' ? "LOAD: CLICK UNIT TO CARRY"
                              : pendingCmd_ == 'u' ? "UNLOAD: CLICK DESTINATION"
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
            const char* hint = "PRESS ESC FOR MENU";
            blockText(hint, (winW - blockWidth(hint, 2.0f)) / 2, float(winH) / 2 + 74, 2.0f,
                      {220, 220, 230, 255});
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
            float x = 14, y = float(winH) - barH() - 14;
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

        // In-game exit menu overlay -- drawn last so it sits above the HUD. Screen
        // space (like the game-over banner); hit-rects are rebuilt here each frame
        // and consumed by input() (see the exitMenu_ branch).
        if (exitMenu_) {
            exitHots_.clear();
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_FRect dim{0, 0, float(winW), float(winH)};
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 150);
            SDL_RenderFillRectF(ren_, &dim);

            const float bw = 280, bh = 50, gap = 14, pad = 32, titlePx = 3.0f;
            const int nBtn = canReturnToMenu_ ? 4 : 3;
            const float titleH = 7 * titlePx + 24;
            const float pw = bw + pad * 2;
            const float ph = pad * 2 + titleH + nBtn * bh + (nBtn - 1) * gap;
            const float px0 = (winW - pw) / 2, py0 = (winH - ph) / 2;

            SDL_FRect panel{px0, py0, pw, ph};
            SDL_SetRenderDrawColor(ren_, 26, 28, 36, 240);
            SDL_RenderFillRectF(ren_, &panel);
            SDL_SetRenderDrawColor(ren_, 120, 130, 160, 255);
            SDL_RenderDrawRectF(ren_, &panel);

            const char* title = "GAME MENU";
            blockText(title, px0 + (pw - blockWidth(title, titlePx)) / 2, py0 + pad, titlePx,
                      {235, 225, 180, 255});

            float bx = px0 + pad, by = py0 + pad + titleH;
            auto btn = [&](const std::string& label, std::function<void()> action) {
                SDL_FRect r{bx, by, bw, bh};
                bool hot = mouseX_ >= r.x && mouseX_ <= r.x + r.w &&
                           mouseY_ >= r.y && mouseY_ <= r.y + r.h;
                SDL_SetRenderDrawColor(ren_, hot ? 90 : 60, hot ? 110 : 66, hot ? 150 : 86, 255);
                SDL_RenderFillRectF(ren_, &r);
                SDL_SetRenderDrawColor(ren_, hot ? 180 : 90, hot ? 200 : 100, hot ? 240 : 130, 255);
                SDL_RenderDrawRectF(ren_, &r);
                float tpx = 2.5f, tw = blockWidth(label, tpx);
                blockText(label, bx + (bw - tw) / 2, by + (bh - 7 * tpx) / 2, tpx,
                          {228, 232, 242, 255});
                exitHots_.push_back({r, std::move(action)});
                by += bh + gap;
            };
            btn("RESUME", [this] { exitMenu_ = false; });
            btn("OPTIONS", [this] { exitMenu_ = false; openOptions(); });
            if (canReturnToMenu_) btn("MAIN MENU", [this] { menuRequested_ = true; });
            btn("QUIT", [this] { quitRequested_ = true; });
        }
        if (options_) options_->render(winW, winH);   // topmost of all
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
        return rowW + 24 + panelW();
    }

private:
    struct Visual {
        tak::tdo::Model model;
    };
    struct EffectAnim;   // defined below; Anim only needs the pointer type
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
        // emit-sfx (piece, sfxType) captured off the worker thread; drained on the
        // main thread after the parallel VM tick (SDL/effects_ are main-thread only).
        std::vector<std::pair<int, int32_t>> pendingSfx;
        // Continuous ambient fire/smoke: retail runs one persistent emitter per unit,
        // so we draw ONE looping flame/smoke, kept alive while the emit-loop re-fires
        // (fireT/smokeT = seconds since the last emit of each). Smooth, not per-emit.
        const EffectAnim* fireFx = nullptr;
        const EffectAnim* smokeFx = nullptr;
        float fireT = 1e9f, smokeT = 1e9f;
        float fireLift = 0, smokeLift = 0;   // screen lift of the emitting piece
        bool usesGlow = false;               // model has an animated glow texture
    };

    // Turn a COB emit-sfx (piece, packed type) into a one-shot world-space effect at
    // the unit. The Sacred Fire's FireControl loop re-emits every ~0.5s, so the short
    // flame/smoke puffs stack into a continuous flicker (as retail's persistent
    // particle emitter does). Cosmetic; not hashed.
    void emitSfx(const tak::sim::Unit& u, Anim& a, int piece, int32_t sfx) {
        const char* anim = sfxAnimFor(sfx);
        if (!anim) return;
        const EffectAnim* ea = effectFor(anim);
        if (!ea || ea->frames.empty()) return;
        // Refresh this unit's persistent flame/smoke; drawUnitFx cycles it smoothly.
        float lift = pieceLift(u, a, piece);
        if (anim[0] == 's') { a.smokeFx = ea; a.smokeT = 0; a.smokeLift = lift; }
        else                { a.fireFx = ea;  a.fireT = 0;  a.fireLift = lift; }
    }
    // Map a packed COB sfx code to the retail effect GAF sequence. (KINGDOMS.icd
    // emitSfx @0x50da20: the 0x100 bit flags the extended emitter family, low bits
    // pick the effect -- 4/5/6 = damage-flame small/med/large from anims/flames.gaf,
    // 1/2/3 = smoke/steam from anims/smoke.gaf.)
    static const char* sfxAnimFor(int32_t sfx) {
        int low = sfx & 0xFF;
        if (sfx & 0x100) {
            if (low == 6) return "flames:flame large";
            if (low == 5) return "flames:flame medium";
            if (low == 4) return "flames:flame small";
            if (low >= 1 && low <= 3) return "smoke:smoke01";
            return nullptr;
        }
        return (low == 0 || low == 1) ? "flames:flame large" : nullptr;
    }
    // Accumulated model-Y (height above the unit's ground origin) of a named piece,
    // for lifting the effect onto it. Ground-level pieces (the Sacred Fire's root)
    // give 0; a smokestack piece gives its height.
    static bool findPieceY(const tak::tdo::Object& o, const std::string& name,
                           float acc, float& out) {
        float y = acc + o.y;
        std::string on = o.name;
        std::transform(on.begin(), on.end(), on.begin(), ::tolower);
        if (on == name) { out = y; return true; }
        for (const auto& c : o.children)
            if (findPieceY(c, name, y, out)) return true;
        return false;
    }
    float pieceLift(const tak::sim::Unit& u, const Anim& a, int piece) {
        if (piece < 0 || size_t(piece) >= a.pieceNames.size() || !u.type) return 0.0f;
        auto vt = visuals_.find(u.type->id);
        if (vt == visuals_.end()) return 0.0f;
        float out = 0.0f;
        findPieceY(vt->second.model.root, a.pieceNames[size_t(piece)], 0.0f, out);
        return std::max(0.0f, out);
    }

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
                visuals_[vm] = {tak::tdo::load(vread("objects3d/" + vm + ".3do"))};
            } catch (const std::exception&) { return; }   // no promoted mesh: keep base
        }
        it->second = vm;   // draw the promoted mesh from now on
    }

    void registerUnit(const tak::sim::Unit& u) {
        const std::string& typeId = u.type->id;
        if (!visuals_.count(typeId)) {
            try {
                visuals_[typeId] = {tak::tdo::load(vread("objects3d/" + typeId + ".3do"))};
            } catch (const std::exception& e) {
                std::fprintf(stderr, "no model for %s: %s\n", typeId.c_str(), e.what());
                return;
            }
        }
        Anim a;
        try {
            std::string cobPath = "scripts/" + typeId + ".cob";
            auto cobFile = tak::cob::load(vread(cobPath), cobPath);
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
                    case 16:                                           // BUILD_PERCENT_LEFT
                    case 17: return su->underConstruction              // (scripts push 17; the
                                 ? int32_t(100 - su->hp / su->type->maxHp * 100)  // Create wait-
                                 : 0;                                  // loops on it, so ambient
                                                                       // anims/emit-sfx hold off
                                                                       // until the building is up)
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
            } else if (isStructure(u.type)) {
                // Buildings: run the COB constructor so ambient loops start (e.g. the
                // Keep's Create kicks off its flag/smoke scripts, the Sacred Fire's
                // its FireControl flicker). Detect via isStructure (maxVel<=0), NOT
                // !canMove -- the Keep and friends set canmove=1 with no velocity, so
                // the old !canMove test skipped them and they never animated.
                a.vm->start("Create");
            }
        } catch (const std::exception&) { /* unit stays unanimated */ }
        // Flag units whose model uses an animated glow texture (lodestone/mana/crystal)
        // so the glow only cycles once built -- held static while still conjuring.
        if (auto vt = visuals_.find(typeId); vt != visuals_.end())
            for (const auto& tn : vt->second.model.textures()) {
                std::string t = tn;
                std::transform(t.begin(), t.end(), t.begin(), ::tolower);
                if (animatedTex_.count(t)) { a.usesGlow = true; break; }
            }
        if (a.vm) {
            Anim& st = anims_[u.id] = std::move(a);
            // The VM is ticked on the worker pool, so emit-sfx only stashes into this
            // unit's own buffer (std::map nodes are pointer-stable); the main thread
            // drains it into effects_ after the parallel tick.
            st.vm->onEmitSfx = [buf = &st.pendingSfx](int piece, int32_t sfx) {
                buf->push_back({piece, sfx});
            };
        }
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

    void loadTextures() {
        // Faction texture banks use their own palettes (palettes/<side>_textures.pcx).
        // The VFS merges base + Iron Plague (cre) texture GAFs into one namespace.
        std::map<std::string, tak::gaf::Palette> pals;
        for (const char* side : {"ara", "tar", "ver", "zon", "aid", "cre"}) {
            std::string pp = std::string("palettes/") + side + "_textures.pcx";
            try { if (vfs_.has(pp)) pals[side] = tak::gaf::Palette::fromBytes(vread(pp), pp); }
            catch (const std::exception&) {}
        }
        if (!pals.count("ara")) return;   // no palettes available
        for (const std::string& path : vfs_.list("textures")) {
            if (std::filesystem::path(path).extension() != ".gaf") continue;
            std::string stem = std::filesystem::path(path).stem().string();
            std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);
            const auto* pal = &pals.at("ara");
            auto pit = pals.find(stem.substr(0, 3));
            if (pit != pals.end()) pal = &pit->second;
            try {
                for (auto& seq : tak::gaf::load(vread(path), *pal, 5, path)) {
                    if (seq.frames.empty()) continue;
                    std::string name = seq.name;
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    if (textures_.count(name)) continue;
                    // 10-frame sequences are EITHER per-player colours (insignia --
                    // frames span distinct hues) OR an animated glow (lodestone/mana/
                    // sacred-fire crystal -- frames share a hue, a moving sparkle).
                    // Keep all 10 for both; classify by hue spread so the glow cycles
                    // over time (animatedTex_) while insignia stay picked-by-player.
                    size_t n = seq.frames.size() == 10 ? 10 : 1;
                    // The animated glows (lodestone/mana/sacred-fire/crystal crystals)
                    // are the mana/lodestone/crystal-named textures whose 10 frames
                    // pulse ONE hue over time. The "*logo*" textures (incl. the
                    // lodestone side-panel logos) are per-PLAYER-colour -- their 10
                    // frames are the 10 player colours, picked by slot, NOT animated.
                    if (n == 10 && name.find("logo") == std::string::npos) {
                        for (const char* g :
                             {"lode", "mana", "sacred", "crystal", "lightning", "stone"})
                            if (name.find(g) != std::string::npos) {
                                animatedTex_.insert(name);
                                break;
                            }
                    }
                    // Sample the actual 10 player-colour RGBs once, from a logo/
                    // insignia texture (mostly pure player colour), so the HUD and
                    // minimap can match whatever colour a player renders in.
                    if (!sampledColors_ && n == 10 && !animatedTex_.count(name) &&
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
        try {
            visuals_[typeId] = {tak::tdo::load(vread("objects3d/" + typeId + ".3do"))};
            return &visuals_[typeId].model;
        } catch (const std::exception&) {}
        return nullptr;
    }

    // Draw a translucent, faintly blue ghost of a building where it will be
    // built later (a queued or not-yet-started site).
    void drawGhostAt(const tak::sim::UnitType* type, float x, float z,
                     bool invalid = false) {
        const tak::tdo::Model* model = ghostModel(type->id);
        if (!model) return;
        tris_.clear();
        SDL_Texture* atlas = atlasFor(colorSlot_[localPlayer_ & 7]);
        collect(tris_, atlas, model->root, Xform{}, nullptr, 0.0f, localPlayer_);
        std::stable_sort(tris_.begin(), tris_.end(),
                  [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
        float zm = mapView_.zoom();
        // The ghost lifts onto the relief exactly like the finished unit/building will.
        float ax = (x - mapView_.offX()) * zm - terrainLiftX(x, z) * zm;
        float ay = (z - mapView_.offY()) * zm - terrainLift(x, z) * zm;
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
                if (invalid) {
                    // Can't build here: wash the ghost red instead of the box.
                    v.color.a = 150;
                    v.color.r = Uint8(std::min(255, int(v.color.r * 0.6f) + 110));
                    v.color.g = Uint8(v.color.g * 0.30f);
                    v.color.b = Uint8(v.color.b * 0.30f);
                } else {
                    v.color.a = 130;
                    v.color.r = Uint8(v.color.r * 0.55f);   // shift toward blue
                    v.color.g = Uint8(v.color.g * 0.8f);
                }
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
    std::set<std::string> animatedTex_;    // multi-frame glow textures (cycle over time)

    // Re-render the current frame of each animated glow texture (lodestone/mana/
    // sacred-fire crystal) into its rect in every built atlas, so the glow cycles
    // over time instead of showing a single frame baked at atlas-build time. `live`
    // (a built glow-unit is on screen) advances it; otherwise it holds frame 0 so a
    // still-conjuring lodestone doesn't glow until it's finished.
    void animateGlowTextures(bool live) {
        AaScaleReset _sr(ren_);   // bakes render at 1:1 even when whole-frame AA is on
        if (animatedTex_.empty()) return;
        int frame = live ? int(animClock_ * 4.0f) : 0;   // ~4 fps
        SDL_Texture* prev = SDL_GetRenderTarget(ren_);
        bool onAny = false;
        for (SDL_Texture* atlas : atlasTex_) {
            if (!atlas) continue;
            SDL_SetRenderTarget(ren_, atlas);
            onAny = true;
            for (const auto& name : animatedTex_) {
                auto rit = atlasRect_.find(name);
                auto tit = textures_.find(name);
                if (rit == atlasRect_.end() || tit == textures_.end() ||
                    tit->second.size() < 2)
                    continue;
                SDL_Texture* f = tit->second[size_t(frame) % tit->second.size()];
                SDL_Rect r = rit->second;
                SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(ren_, 0, 0, 0, 0);
                SDL_RenderFillRect(ren_, &r);          // clear the region (transparent)
                SDL_BlendMode fb;
                SDL_GetTextureBlendMode(f, &fb);
                SDL_SetTextureBlendMode(f, SDL_BLENDMODE_NONE);
                SDL_RenderCopy(ren_, f, nullptr, &r);  // overwrite with this frame's RGBA
                SDL_SetTextureBlendMode(f, fb);
            }
        }
        if (onAny) SDL_SetRenderTarget(ren_, prev);
    }

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
        AaScaleReset _sr(ren_);
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
        AaScaleReset _sr(ren_);
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
        AaScaleReset _sr(ren_);
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
        std::string cobPath = "scripts/" + typeId + ".cob";
        try {
            auto cobFile = tak::cob::load(vread(cobPath), cobPath);
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
        AaScaleReset _sr(ren_);
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
    // A monarch (the five hero units) -- the only thing that disco-dances.
    static bool isMonarchType(const tak::sim::UnitType* t) {
        if (!t) return false;
        for (int i = 0; i < 5; ++i) if (t->id == tak::sim::kMonarchs[i]) return true;
        return false;
    }
    // Fully-saturated hue wheel -> RGB, hue in [0,1). Drives the disco tint & floor.
    static SDL_Color discoHue(float h) {
        h = h - std::floor(h);
        float r = std::fabs(h * 6.0f - 3.0f) - 1.0f;
        float g = 2.0f - std::fabs(h * 6.0f - 2.0f);
        float b = 2.0f - std::fabs(h * 6.0f - 4.0f);
        auto cl = [](float v) { return Uint8(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
        return SDL_Color{cl(r), cl(g), cl(b), 255};
    }
    // Is this unit currently disco-dancing (a monarch whose player hit Shift+D)?
    bool dancing(const tak::sim::Unit& u) const {
        return isMonarchType(u.type) && world_.discoActive(u.player);
    }
    // ...or headbanging to heavy metal (a monarch whose player hit Shift+H)?
    bool headbanging(const tak::sim::Unit& u) const {
        return isMonarchType(u.type) && world_.headbangActive(u.player);
    }

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
        // Disco emote: a dancing monarch spins, bobs and hue-cycles. Local wall-time
        // (animClock_) drives the smooth motion; world_.discoActive() (a synced sim
        // timer) gates it. Pure client-side eye-candy -- nothing here is hashed.
        bool disco = dancing(u);
        float discoBob = 0.0f, discoMix = 0.0f;
        SDL_Color discoCol{};
        if (disco) {
            float t = animClock_;
            facing += t * 6.2831853f;                                // ~1 rev/sec
            discoBob = std::fabs(std::sin(t * 8.0f)) * 11.0f * zm;    // bounce, px
            discoCol = discoHue(t * 0.8f);                           // body tint hue
            discoMix = 0.5f;
        }
        // Headbang emote (Shift+H): no spin -- a sharp downward nod synced to the metal
        // beat (~152 BPM), the body whipping side to side and flashing red on each bang.
        if (headbanging(u)) {
            float ph = animClock_ * 2.533f * 6.2831853f;             // ~152 bangs/min
            float bang = std::pow(std::max(0.0f, std::sin(ph)), 2.0f);
            discoBob = -bang * 16.0f * zm;                           // dip DOWN (nod)
            facing += std::sin(ph) * 0.55f;                          // hair-whip
            discoCol = SDL_Color{210, 40, 40, 255};                 // deep metal red
            discoMix = bang * 0.5f;                                  // flash on the bang
            disco = true;                                            // reuse the tint path
        }
        bool mirror = false;
        SDL_Texture* atlas = (slot >= 0 && size_t(slot) < atlasTex_.size())
                                 ? atlasTex_[size_t(slot)] : nullptr;
        collect(scratch, atlas, vt->second.model.root, base, anim, facing, u.player, mirror);
        std::stable_sort(scratch.begin(), scratch.end(),
                  [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
        g.ax = ax; g.ay = ay;
        g.alt = anim ? anim->altitude : 0.0f;
        g.occY = wallOcclusionY(u.x, u.z);
        // "Materialising" = the summon fade-in/shimmer: either a site still conjuring
        // (progress = HP fraction) or a unit just summoned from a building (progress =
        // its viewer-only birth ramp). Both fade alpha in and glow while p < 1.
        float birthP = birthProgress(u.id);
        bool conjuring = u.type && (u.underConstruction || birthP < 1.0f);
        float p = !u.type ? 1.0f
                  : u.underConstruction ? std::clamp(u.hp / u.type->maxHp, 0.0f, 1.0f)
                                        : birthP;
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
                v.position.y = v.position.y * zm + ay - discoBob;   // disco bounce (0 otherwise)
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
                if (disco) {   // blend the body toward the cycling disco hue
                    v.color.r = Uint8(int(v.color.r) + int((int(discoCol.r) - int(v.color.r)) * discoMix));
                    v.color.g = Uint8(int(v.color.g) + int((int(discoCol.g) - int(v.color.g)) * discoMix));
                    v.color.b = Uint8(int(v.color.b) + int((int(discoCol.b) - int(v.color.b)) * discoMix));
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
        // Disco dance floor: a pulsing, hue-cycling glow disc under a dancing monarch.
        if (dancing(u)) {
            float t = animClock_;
            SDL_Color dc = discoHue(t * 0.8f + 0.5f);   // offset from the body tint
            float rad = (float(std::max(u.type->footX, u.type->footZ)) * 12.0f + 22.0f)
                        * (0.85f + 0.15f * std::sin(t * 8.0f)) * zm;   // pulse with the bob
            const int N = 24;
            std::vector<SDL_Vertex> fan;
            fan.reserve(N * 3);
            SDL_Vertex ctr{{ax, ay}, {dc.r, dc.g, dc.b, 150}, {0, 0}};
            auto rim = [&](float a) {
                return SDL_Vertex{{ax + std::cos(a) * rad, ay + std::sin(a) * rad * 0.5f},
                                  {dc.r, dc.g, dc.b, 0}, {0, 0}};
            };
            for (int i = 0; i < N; ++i) {
                fan.push_back(ctr);
                fan.push_back(rim(float(i) / N * 6.2831853f));
                fan.push_back(rim(float(i + 1) / N * 6.2831853f));
            }
            SDL_BlendMode pbm;
            SDL_GetRenderDrawBlendMode(ren_, &pbm);
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_ADD);
            SDL_RenderGeometry(ren_, nullptr, fan.data(), int(fan.size()), nullptr, 0);
            SDL_SetRenderDrawBlendMode(ren_, pbm);
        }
        // Headbang: a red mosh-pit glow that flares on each downbeat.
        if (headbanging(u)) {
            float ph = animClock_ * 2.533f * 6.2831853f;
            float bang = std::pow(std::max(0.0f, std::sin(ph)), 2.0f);
            SDL_Color dc{Uint8(150 + 90 * bang), Uint8(20 + 20 * bang), 20, 255};
            float rad = (float(std::max(u.type->footX, u.type->footZ)) * 12.0f + 22.0f)
                        * (0.75f + 0.45f * bang) * zm;
            const int N = 24;
            std::vector<SDL_Vertex> fan;
            fan.reserve(N * 3);
            SDL_Vertex ctr{{ax, ay}, {dc.r, dc.g, dc.b, Uint8(120 + 100 * bang)}, {0, 0}};
            auto rim = [&](float a) {
                return SDL_Vertex{{ax + std::cos(a) * rad, ay + std::sin(a) * rad * 0.5f},
                                  {dc.r, dc.g, dc.b, 0}, {0, 0}};
            };
            for (int i = 0; i < N; ++i) {
                fan.push_back(ctr);
                fan.push_back(rim(float(i) / N * 6.2831853f));
                fan.push_back(rim(float(i + 1) / N * 6.2831853f));
            }
            SDL_BlendMode pbm;
            SDL_GetRenderDrawBlendMode(ren_, &pbm);
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_ADD);
            SDL_RenderGeometry(ren_, nullptr, fan.data(), int(fan.size()), nullptr, 0);
            SDL_SetRenderDrawBlendMode(ren_, pbm);
        }
        // Submit the pre-built, depth-sorted vertex runs -- one SDL_RenderGeometry
        // per texture (usually 1 per unit). The veterancy/conjure colour tint was
        // already baked into the vertices on the worker pool.
        bool conjuring = u.type && (u.underConstruction || birthProgress(u.id) < 1.0f);
        int off = 0;
        for (const auto& r : g.runs) {
            SDL_RenderGeometry(ren_, r.first, g.verts.data() + off, r.second, nullptr, 0);
            off += r.second;
        }

        // Conjure effect: sprinkle the faction's build/summon sparkle over the
        // footprint while the unit materialises (a conjuring site, or a unit freshly
        // summoned from a building), each staggered so they twinkle out of sync.
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
    // vfs_ is declared BEFORE mapView_ so its Compositor can borrow it; it is owned
    // here (not a reference) so a multiplayer client can REMOUNT to the room's
    // override tier at game start -- move-assigning vfs_ keeps every borrowed
    // pointer (MapView's Compositor) valid because the object itself is reused.
    tak::hpi::Vfs vfs_;          // retail-root read-path (the only way we read files)
    MapView mapView_;
    std::string installRoot_;    // retail install dir (for remounting to a new tier)
    tak::hpi::OverridePolicy policy_ = tak::hpi::OverridePolicy::Full;
    std::string mapPath_;        // VFS path to the map .tnt (start positions, siblings)
    // VFS read helpers -- the engine's only game-file access.
    std::vector<uint8_t> vread(const std::string& p) const { return vfs_.read(p); }
    bool vhas(const std::string& p) const { return vfs_.has(p); }
    tak::tdf::Node vtdf(const std::string& p) const {
        auto b = vfs_.read(p);
        return tak::tdf::parseText(std::string(b.begin(), b.end()), p);
    }
    // A map's sibling scenario file (map.tnt -> map.ota/.cob/.tdf/.txt/.crt).
    std::string mapSibling(const char* ext) const {
        std::filesystem::path p = mapPath_; p.replace_extension(ext);
        return p.generic_string();
    }
    // Remount the data set to a multiplayer room's override tier and rebuild the
    // gameplay registry, so this client's sim reads the same gameplay data the
    // referee does. Cosmetics already loaded stay (harmless local display).
    void remountPolicy(uint8_t p) {
        auto pol = tak::hpi::OverridePolicy(p <= 2 ? p : 2);
        if (pol == policy_ || installRoot_.empty()) return;
        policy_ = pol;
        vfs_ = tak::hpi::mountRetailRoot(installRoot_, pol);
        registry_ = tak::sim::TypeRegistry{};
        tak::sim::setupRegistry(registry_, vfs_, crusades_);
    }
    // Fingerprint of the gameplay data THIS client will feed its sim (current tier).
    uint64_t gameDataHash() const { return tak::hpi::gameplayHash(vfs_); }
public:
    uint8_t overridePolicy() const { return uint8_t(policy_); }
private:
    bool crusades_ = false; // which balance registry_ currently holds
    std::string side_ = "ara";
    std::string aiSide_ = "tar";   // single-player: the AI opponent's faction
    // Faction name -> wire index (0 ara, 1 tar, 2 ver, 3 zon, 4 cre).
    static uint8_t facIdx(const std::string& s) {
        const char* n[5] = {"ara", "tar", "ver", "zon", "cre"};
        for (uint8_t i = 0; i < 5; ++i) if (s == n[i]) return i;
        return 0;
    }
    tak::sim::TypeRegistry registry_;
    tak::sim::World world_;
    // Hash maps (not std::map): these are looked up per unit per frame in the
    // serial anim loop and the parallel projection, and tree traversals were a
    // measurable slice of the update cost at thousands of units.
    std::unordered_map<std::string, Visual> visuals_;
    std::unordered_map<int, std::string> unitType_;
    std::unordered_map<int, Anim> anims_;
    // Summon fade-in: unit id -> age (s) since it was conjured from a building. Purely
    // cosmetic and viewer-only (driven by the sim's non-hashed justBuilt hook); it
    // fades the unit in and sprinkles the faction shimmer for kBirthFxDur. Updated on
    // the main thread each frame, then read-only during the parallel geometry build.
    std::unordered_map<int, float> birthFx_;
    static constexpr float kBirthFxDur = 0.9f;
    float birthProgress(int id) const {
        auto it = birthFx_.find(id);
        return it == birthFx_.end() ? 1.0f
                                    : std::clamp(it->second / kBirthFxDur, 0.0f, 1.0f);
    }
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
    bool reclaimDrag_ = false;        // right-drag box: a builder clears the area
    float rdX0_ = 0, rdZ0_ = 0;       // reclaim-drag start (world)
    float rdSx0_ = 0, rdSy0_ = 0;     // reclaim-drag start (screen; click-vs-drag test)
    bool draggingMinimap_ = false;
    float dragX0_ = 0, dragY0_ = 0, dragX1_ = 0, dragY1_ = 0;
    char pendingCmd_ = 0;   // armed order awaiting a click: 'f' fight-move,
                            // 'm' move, 'a' attack, 'p' patrol, 'g' guard
    std::map<int, std::vector<int>> groups_;   // control groups 0-9
    bool paused_ = false;
    bool exitMenu_ = false;          // in-game exit overlay (Esc) is open
    bool canReturnToMenu_ = false;   // launched from the front-end -> offer MAIN MENU
    std::vector<std::pair<SDL_FRect, std::function<void()>>> exitHots_;   // overlay hit-rects (screen space)
    // ---- Options (local display/input; see viewer/settings.h) ----
    float edgeScrollSpeed_ = 1.0f;
    bool  edgeScrollOn_ = true;
    float uiScale_ = 1.0f;
    tak::Settings* settings_ = nullptr;               // main()'s settings (for the in-game Options)
    std::unique_ptr<tak::OptionsScreen> options_;     // in-game Options overlay
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
    // Client-side jitter/receive buffer (ON by default; TAK_NET_DELAY overrides:
    // 0 = off, K = fixed depth, auto/unset = self-sizing). The sim is paced on the
    // wall clock at 30 Hz and kept ~netDelay_ bundles behind the newest received,
    // so brief server->client jitter is covered from the reserve instead of
    // stalling. Costs ~netDelay_*33ms of input latency (auto keeps that minimal).
    int netDelay_ = -2;          // -2 = read TAK_NET_DELAY once; then 0 = off, else depth
    bool netAuto_ = false;       // auto (default): size the buffer to the link
    bool netBufReady_ = false;   // built the initial reserve
    float netAccum_ = 0;         // wall-clock tick accumulator (seconds)
    uint64_t netStepMs_ = 0;     // last mpStep wall time
    long netBenchFrames_ = 0, netBenchStalls_ = 0;   // jitter-buffer stall metric
public:
    long netBenchFrames() const { return netBenchFrames_; }
    long netBenchStalls() const { return netBenchStalls_; }
    void netEnableRttProbe() { if (mp_) mp_->enableRttProbe(); }
    float netRttMs() const { return mp_ ? mp_->rttMs() : 0.0f; }
    int netDelay() const { return netDelay_; }
private:
    bool replayMode_ = false;                     // playing a recorded .takrep
    std::vector<tak::net::Bundle> replayBundles_;
    size_t replayTick_ = 0;
    float replayAccum_ = 0;
    bool mpReadied_ = false, mpStarted_ = false, mpSetupDone_ = false;
    // interactive lobby UI state
    enum class LobbyScreen { Browser, Create } lobbyScreen_ = LobbyScreen::Browser;
    bool menuRequested_ = false;   // set by a MAIN MENU action -> main() returns to the front-end
    bool quitRequested_ = false;   // set by the in-game QUIT button -> main() exits the app
    bool singlePlayer_ = false;    // menu single-player: private local game (SP-flavoured lobby)
    bool externalLobbyMusic_ = false;   // front-end owns the lobby BGM -> suppress ours
    int lbField_ = 0;   // active text field: 1=createName 2=createPass 3=joinPass 4=chat
    float lobbyScale_ = 2.0f;               // lobby fit scale (set in render)
    float lobbyOffX_ = 0, lobbyOffY_ = 0;   // lobby centre offset (logical units; set in render)
    std::string createName_ = "game", createPass_, joinPass_, chatDraft_;
    bool createCrusades_ = false, createGods_ = false;
    std::vector<std::pair<std::string, std::string>> mapList_;  // {name, tnt path}, cached
    // Create-screen map picker: a scrollable list box. mapScroll_ is the index of the
    // first visible row; the rest is geometry cached each frame for wheel + scrollbar
    // drag handling in lobbyInput (all in panel-local logical coords).
    int mapScroll_ = 0;
    bool mapPrefApplied_ = false;      // adopted settings_->lastMap once this session
    bool mapDrag_ = false;             // dragging the scrollbar thumb
    SDL_FRect mapListRect_{};          // the list box (rows area) -- wheel target
    SDL_FRect mapThumbRect_{};         // the scrollbar thumb -- drag grab
    int mapVisRows_ = 0, mapTotalRows_ = 0;   // for clamping + thumb drag math
    // Selected-map preview (the .tnt's embedded minimap), rebuilt on selection.
    SDL_Texture* mapPreviewTex_ = nullptr;
    std::string mapPreviewFor_;        // mapPath_ the current preview was built for
    int mapPreviewW_ = 0, mapPreviewH_ = 0;
    std::string mapPreviewDims_;       // "W x H" cell size, shown under the preview
    std::vector<uint8_t> mapPalRgba_;  // palettes/palette.pal, cached (256*4)
    uint8_t createOverride_ = 1;   // create-dialog override tier (default cosmetic)
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
    // A "structure" (building) for render/build purposes = one that can't actually
    // move. NOTE: the FBI `canmove` flag is unreliable -- some buildings (the Keep,
    // arakeep) set canmove=1 with NO velocity -- so key off maxVel, not type->canMove.
    static bool isStructure(const tak::sim::UnitType* t) { return !t || t->maxVel <= 0.0f; }
    // EVERYTHING on the map lifts onto the terrain relief by the same rule -- mobile
    // units, buildings, AND the feature decals (mana deposits, trees) -- so a mana
    // deposit sits at the height its heightmap claims and a lodestone/units built on
    // it stack right on top instead of the decal being flat while units float above.
    float uLiftY(const tak::sim::Unit& u) { return terrainLift(u.x, u.z); }
    float uLiftX(const tak::sim::Unit& u) { return terrainLiftX(u.x, u.z); }

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
    // Retail GUI-driven HUD: the parsed .gui and, parallel to gui_.gadgets, the
    // loaded state-art textures for each gadget (imgs[0]=normal,1=hover,2=grayed).
    tak::gui::Gui gui_;
    std::vector<std::vector<SDL_Texture*>> guiTex_;
    std::map<std::string, SDL_Texture*> icons_;
    std::map<std::pair<std::string, int>, SDL_Texture*> modelIcons_;  // model-rendered fallback icons
    std::vector<std::pair<SDL_FRect, const tak::sim::UnitType*>> iconRects_;
    static constexpr int kMiniSizeBase = 180;
    int miniSize() const { return int(kMiniSizeBase * uiScale_); }   // UI-scale (Options)
    // Right-side UI strip (minimap + command panel). The map view is kept to the
    // left of it so the panel never draws over the world.
    int panelW() const { return miniSize() + int(20 * uiScale_); }   // fallback width (no GUI loaded)

    // Scale from the retail 640x480 GUI space to screen pixels. Authored at 640x480;
    // we scale by height so the panel art keeps its aspect (winH/480 is true retail
    // scale -- the /700 divisor keeps the HUD from dominating high-res displays while
    // holding retail proportions and button alignment).
    float guiS() const { return uiScale_ * (winH_ > 0 ? winH_ : 480) / 700.0f; }
    // Width of the right-hand command-panel strip (the retail UnitMenu is 128 wide in
    // 640-space); never narrower than the minimap.
    int cmdPanelW() const {
        if (gui_.gadgets.empty()) return panelW();
        return std::max(miniSize() + 12, int(128 * guiS()) + 8);
    }
    int mapViewW(int winW) const { return std::max(64, winW - cmdPanelW()); }

    // Screen rect for a command-panel gadget (x >= 512 in 640-space): the whole
    // command panel is anchored to the bottom-right corner, so the ButtonPanel art
    // and its buttons share one transform and stay aligned at any scale.
    SDL_FRect guiCmdRect(const tak::gui::Gadget& g) const {
        float s = guiS();
        // 640-space y=480 (screen bottom in retail) maps just above our info bar so
        // the command panel and the existing bottom bar don't overlap.
        float baseY = winH_ - barH();
        return {winW_ - (640 - g.x) * s, baseY - (480 - g.y) * s, g.w * s, g.h * s};
    }

    // Screen rect for a bottom-bar gadget (the retail InfoPanel occupies 640-space
    // y 431..480). The whole bar layout is scaled uniformly to our barH()-tall bar and
    // anchored bottom-left, so the unit-info block clusters at the left while the bar
    // chrome stretches to fill the width.
    SDL_FRect guiBarRect(const tak::gui::Gadget& g) const {
        float vs = float(barH()) / 49.0f;   // 49-tall retail bar -> barH() px
        float barTop = winH_ - barH();
        return {g.x * vs, barTop + (g.y - 431) * vs, g.w * vs, g.h * vs};
    }

    SDL_FRect minimapRect(int winW, int winH) const {
        (void)winH;
        float aspect = float(mapView_.map().blocksY) / float(mapView_.map().blocksX);
        return {float(winW) - miniSize() - 10, 10, float(miniSize()), float(miniSize()) * aspect};
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
                       (winH - barH()) / zm / mapH * r.h};
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
                           wz - (winH - int(barH())) / zm / 2);
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
        if (cmd == 'c') {   // clear/reclaim: reclaim the feature under the cursor
            if (!haveReclaimer()) return;
            int fid = -1; float bestF = 1e18f;
            for (const auto& f : world_.features()) {
                if (!f.alive) continue;
                float dx = f.x - wx, dz = f.z - wz, d = dx * dx + dz * dz;
                float r = 18.0f + 8.0f * float(std::max(f.fx, f.fz));
                if (d < r * r && d < bestF) { bestF = d; fid = f.id; }
            }
            if (fid < 0) return;
            int builderId = firstReclaimer();
            tak::net::Command c;
            c.kind = tak::net::Cmd::Reclaim;
            c.unitId = builderId;
            c.targetId = fid;
            c.queue = queue ? 1 : 0;
            issue(c);
            voice(builderId, "move");
            return;
        }
        if (cmd == 'r') {   // repair: heal the damaged friendly under the cursor
            int builderId = -1;
            for (int id : selection_) {
                const auto* u = world_.unit(id);
                if (u && u->type && u->type->isBuilder && u->type->canMove &&
                    u->player == localPlayer_) { builderId = id; break; }
            }
            if (builderId < 0) return;
            int tid = -1; float best = 28.0f * 28.0f;
            for (auto& u : world_.units()) {
                if (!u.alive() || u.embarked() || u.id == builderId || !u.type) continue;
                if (!world_.allied(u.player, localPlayer_)) continue;
                if (u.underConstruction || u.hp >= u.type->maxHp) continue;   // only damaged
                float dx = u.x - wx, dz = u.z - wz, d = dx * dx + dz * dz;
                if (d < best) { best = d; tid = u.id; }
            }
            if (tid < 0) return;
            tak::net::Command c;
            c.kind = tak::net::Cmd::Repair;
            c.unitId = builderId;
            c.targetId = tid;
            c.queue = queue ? 1 : 0;
            issue(c);
            voice(builderId, "move");
            return;
        }
        if (cmd == 'u') {   // unload: selected transport(s) sail to (wx,wz), disembark
            bool any = false;
            for (int id : selection_) {
                const auto* u = world_.unit(id);
                if (!u || !u->type || !u->type->canTransport || u->cargo.empty()) continue;
                tak::net::Command c;
                c.kind = tak::net::Cmd::Unload;
                c.unitId = id;
                c.x = wx;
                c.z = wz;
                issue(c);
                any = true;
            }
            if (any) voice(selection_.front(), "move");
            return;
        }
        if (cmd == 'l') {   // load: the friendly unit under the cursor boards a transport
            int transportId = -1;
            for (int id : selection_) {
                const auto* u = world_.unit(id);
                if (u && u->type && u->type->canTransport &&
                    int(u->cargo.size()) < u->type->transportCap) { transportId = id; break; }
            }
            const auto* t = transportId >= 0 ? world_.unit(transportId) : nullptr;
            if (!t) return;
            int pid = -1; float best = 24.0f * 24.0f;
            for (auto& u : world_.units()) {
                if (!u.alive() || u.embarked() || u.id == transportId || !u.type) continue;
                if (u.player != t->player || u.type->canTransport) continue;
                float dx = u.x - wx, dz = u.z - wz, d = dx * dx + dz * dz;
                if (d < best) { best = d; pid = u.id; }
            }
            if (pid < 0) return;
            tak::net::Command c;
            c.kind = tak::net::Cmd::Load;
            c.unitId = pid;
            c.targetId = transportId;
            issue(c);
            voice(transportId, "move");
            return;
        }
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
                for (auto& sq : tak::gaf::load(vread("anims/shadows.gaf"), *pal, -1,
                                               "anims/shadows.gaf")) {
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
            for (const std::string& path : vfs_.list("features")) {
                if (std::filesystem::path(path).extension() != ".tdf") continue;
                try {
                    auto root = vtdf(path);
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
                std::string pp = "palettes/" + cand;
                return &featurePals_
                            .emplace(world, tak::gaf::Palette::fromBytes(vread(pp), pp))
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
                for (auto& sq : tak::gaf::load(vread("anims/" + f + ".gaf"), *pal, -1,
                                               "anims/" + f + ".gaf")) {
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
        try {
            auto sd = vtdf("gamedata/sidedata.tdf");
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
        return out;
    }

    void loadPanel(const std::string& side) {
        std::string base = "anims/" + side + "ingame";
        try {
            auto pal = tak::gaf::Palette::fromBytes(vread(base + ".pcx"), base + ".pcx");
            for (auto& sq : tak::gaf::load(vread(base + ".gaf"), pal, -1, base + ".gaf")) {
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

    // Palette for a GUI GAF: the sibling anims/<gaf>.pcx if it exists (per-faction
    // panels ship one), else the global palettes/guipal.pal used by gui.gaf.
    tak::gaf::Palette guiPalette(const std::string& gaf) {
        std::string pp = "anims/" + gaf + ".pcx";
        try {
            return tak::gaf::Palette::fromBytes(vread(pp), pp);
        } catch (const std::exception&) {}
        try {
            return tak::gaf::Palette::fromBytes(vread("palettes/guipal.pal"),
                                                "palettes/guipal.pal");
        } catch (const std::exception&) {}
        return {};
    }

    // Load one GAF sequence frame (anims/<gaf>, sequence seq, frame idx) to a texture.
    SDL_Texture* loadGuiFrame(const std::string& gaf, const std::string& seq, int frame) {
        if (gaf.empty() || seq.empty()) return nullptr;
        std::string gp = "anims/" + gaf;
        if (gp.size() < 4 || gp.substr(gp.size() - 4) != ".gaf") gp += ".gaf";
        try {
            auto pal = guiPalette(gaf.size() >= 4 && gaf.substr(gaf.size() - 4) == ".gaf"
                                      ? gaf.substr(0, gaf.size() - 4)
                                      : gaf);
            for (auto& sq : tak::gaf::load(vread(gp), pal, -1, gp)) {
                if (sq.name != seq) continue;
                if (frame < 0 || size_t(frame) >= sq.frames.size()) frame = 0;
                if (sq.frames.empty()) return nullptr;
                auto& f = sq.frames[size_t(frame)];
                if (f.width == 0 || f.height == 0) return nullptr;
                SDL_Texture* t = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                                   SDL_TEXTUREACCESS_STATIC, f.width,
                                                   f.height);
                SDL_UpdateTexture(t, nullptr, f.rgba.data(), f.width * 4);
                SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                return t;
            }
        } catch (const std::exception&) {}
        return nullptr;
    }

    // Parse the faction in-game .gui and load every gadget's state art. side is the
    // 3-letter faction ("ara"/"tar"/"ver"/"zon"/"cre"); the file is guis/<side>ingame.gui.
    void loadGui(const std::string& side) {
        for (auto& v : guiTex_)
            for (auto* t : v)
                if (t) SDL_DestroyTexture(t);
        guiTex_.clear();
        gui_ = {};
        std::string path = "guis/" + side + "ingame.gui";
        try {
            gui_ = tak::gui::parse(vread(path), path);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "loadGui: %s: %s\n", path.c_str(), e.what());
            return;
        }
        guiTex_.resize(gui_.gadgets.size());
        for (size_t i = 0; i < gui_.gadgets.size(); ++i) {
            const auto& g = gui_.gadgets[i];
            for (const auto& im : g.imgs)
                guiTex_[i].push_back(loadGuiFrame(im.gaf, im.seq, im.frame));
        }
        if (std::getenv("TAK_GUIDEBUG")) {
            std::fprintf(stderr, "== %s: %zu gadgets ==\n", path.c_str(),
                         gui_.gadgets.size());
            for (size_t i = 0; i < gui_.gadgets.size(); ++i) {
                const auto& g = gui_.gadgets[i];
                std::fprintf(stderr, "  [%2zu] t%-2d %-18s (%3d,%3d %3dx%3d)", i, g.type,
                             g.name.c_str(), g.x, g.y, g.w, g.h);
                for (size_t k = 0; k < g.imgs.size(); ++k)
                    std::fprintf(stderr, " %s:%s#%d%s", g.imgs[k].gaf.c_str(),
                                 g.imgs[k].seq.c_str(), g.imgs[k].frame,
                                 guiTex_[i][k] ? "" : "(!)");
                std::fprintf(stderr, "\n");
            }
        }
    }

    static constexpr int kBarHBase = 72;
    int barH() const { return int(kBarHBase * uiScale_); }   // UI-scale (Options)

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
                auto pal = tak::gaf::Palette::fromBytes(vread("palettes/ara_textures.pcx"),
                                                        "palettes/ara_textures.pcx");
                std::string tp = "anims/" + std::string(file) + "_4444.taf";
                auto seqs = tak::gaf::load(vread(tp), pal, -1, tp);
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
                std::string pp = "anims/" + std::string(gaf) + ".pcx";
                std::string gp = "anims/" + std::string(gaf) + ".gaf";
                auto pal = tak::gaf::Palette::fromBytes(vread(pp), pp);
                for (auto& sq : tak::gaf::load(vread(gp), pal, -1, gp)) {
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
        float y = float(winH) - barH() - bw - 8;
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
            char lbl[16];
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

    // ---- Retail GUI-driven HUD ----------------------------------------------
    // The command panel (right strip) and its order buttons are laid out from the
    // faction .gui (see loadGui). Rects are anchored bottom-right and share the
    // guiCmdRect transform so the ButtonPanel art and its buttons stay aligned.

    int guiIdx(const char* name) const {
        for (size_t i = 0; i < gui_.gadgets.size(); ++i)
            if (gui_.gadgets[i].name == name) return int(i);
        return -1;
    }

    // Leftmost gadget with this name. Several bar gadgets appear twice -- the primary
    // single-unit group (UnitInfo1, x~114) and the second-unit group (UnitInfo2,
    // x~386); the primary is always the left one, the conjure-target the right one.
    int guiIdxLeft(const char* name) const {
        int best = -1;
        for (size_t i = 0; i < gui_.gadgets.size(); ++i)
            if (gui_.gadgets[i].name == name &&
                (best < 0 || gui_.gadgets[i].x < gui_.gadgets[size_t(best)].x))
                best = int(i);
        return best;
    }
    int guiIdxRight(const char* name) const {
        int best = -1;
        for (size_t i = 0; i < gui_.gadgets.size(); ++i)
            if (gui_.gadgets[i].name == name &&
                (best < 0 || gui_.gadgets[i].x > gui_.gadgets[size_t(best)].x))
                best = int(i);
        return best;
    }

    // (gadget index, command char) for each command button to show for the current
    // selection. cmd: 'm'/'a'/'p'/'g' arm pendingCmd_; 's' = Stop (immediate);
    // '1'/'2'/'3' = weapon slot; 'O'/'D'/'H' = stance offensive/defensive/passive;
    // 'K'/'k' = cloak on/off; 'N'/'F' = active on/off (all immediate toggles).
    std::vector<std::pair<int, char>> guiActiveButtons() const {
        std::vector<std::pair<int, char>> out;
        if (gui_.gadgets.empty() || selection_.empty()) return out;
        const tak::sim::Unit* front = world_.unit(selection_.front());
        if (!front || !front->type) return out;
        auto add = [&](const char* nm, char c) {
            int i = guiIdx(nm);
            if (i >= 0) out.push_back({i, c});
        };
        bool mobile = front->type->maxVel > 0;        // inverse of isStructure()
        bool armed = !front->type->weapons.empty();
        if (mobile) { add("MOVE", 'm'); add("PATROL", 'p'); add("GUARD", 'g'); }
        if (armed) add("ATTACK", 'a');
        add("STOP", 's');
        // Context orders: reclaim (a mobile reclaiming builder), and transport
        // load/unload. CLEAR and UNLOAD share a .gui slot (599,213) but a unit is
        // never both a reclaimer and a transport, so only one shows.
        bool builder = false, reclaimer = false;
        for (int id : selection_) {
            const auto* u = world_.unit(id);
            if (!u || !u->alive() || !u->type || !u->type->isBuilder ||
                !u->type->canMove || u->player != localPlayer_)
                continue;
            builder = true;
            if (u->type->canReclaim) reclaimer = true;
        }
        if (builder) add("HEAL", 'r');       // repair a damaged friendly
        if (reclaimer) add("CLEAR", 'c');
        if (front->type->canTransport) {
            if (int(front->cargo.size()) < front->type->transportCap) add("LOAD", 'l');
            if (!front->cargo.empty()) add("UNLOAD", 'u');
        }
        // Combat stance radio (any mobile armed unit): offensive/defensive/passive.
        if (mobile && armed) {
            add("Offensive", 'O');
            add("Defensive", 'D');
            add("Passive", 'H');
        }
        // Cloak toggle (cloakers) OR power on/off (onOffable) -- they share the 303
        // row, and a unit has at most one of the two capabilities.
        if (front->type->canCloak) {
            add("Uncloaked", 'k');
            add("Cloaked", 'K');
        } else if (front->type->onOffable) {
            add("Inactive", 'F');
            add("Active", 'N');
        }
        int nw = int(front->type->weapons.size());
        if (nw > 1) {
            add("PrimaryWeapon", '1');
            add("SecondaryWeapon", '2');
            if (nw > 2) add("SpecialWeapon", '3');
        }
        return out;
    }

    std::vector<std::pair<SDL_FRect, char>> guiBtnRects_;   // hit list, filled by renderGui

    // Draw the command panel chrome + buttons + idle crystal ball. Falls back to the
    // old vertical order column if no .gui loaded.
    void renderGui(int winW, int winH) {
        if (gui_.gadgets.empty()) { drawOrderColumn(winW, winH); return; }
        guiBtnRects_.clear();
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);

        // Command panel background. ButtonPanel frame 0 is the idle dragon medallion;
        // frame 1 is the button-slot panel shown while a unit is selected (retail swaps
        // the medallion out for the order grid).
        int mi = guiIdx("UnitMenu");
        if (mi >= 0 && !guiTex_[mi].empty()) {
            int pf = !selection_.empty() && guiTex_[mi].size() > 1 && guiTex_[mi][1] ? 1 : 0;
            SDL_Texture* pt = guiTex_[mi][size_t(pf)] ? guiTex_[mi][size_t(pf)]
                                                      : guiTex_[mi][0];
            if (pt) {
                SDL_FRect r = guiCmdRect(gui_.gadgets[mi]);
                SDL_RenderCopyF(ren_, pt, nullptr, &r);
            }
        }

        const tak::sim::Unit* front =
            !selection_.empty() ? world_.unit(selection_.front()) : nullptr;
        for (auto [idx, cmd] : guiActiveButtons()) {
            const auto& g = gui_.gadgets[idx];
            auto& tex = guiTex_[idx];
            SDL_FRect r = guiCmdRect(g);
            guiBtnRects_.push_back({r, cmd});
            bool hot = mouseX_ >= r.x && mouseX_ <= r.x + r.w && mouseY_ >= r.y &&
                       mouseY_ <= r.y + r.h;
            bool active = false;
            if (cmd >= '1' && cmd <= '3')
                active = front && front->weaponSlot == (cmd - '1');
            else if (cmd == 'O') active = front && front->stance == 0;
            else if (cmd == 'D') active = front && front->stance == 1;
            else if (cmd == 'H') active = front && front->stance == 2;
            else if (cmd == 'K') active = front && front->cloakOn;
            else if (cmd == 'k') active = front && !front->cloakOn;
            else if (cmd == 'N') active = front && front->active;
            else if (cmd == 'F') active = front && !front->active;
            else if (cmd != 's')
                active = pendingCmd_ == cmd;
            // Frame semantics differ by button family (both list imgs = frames 0,1,2):
            //  - order buttons: 0 = empty, 1 = glyph normal, 2 = glyph hilite;
            //  - radio/toggle buttons (stance/cloak/active): 1 = SELECTED (gold),
            //    2 = normal (dim). So the lit face swaps between the two.
            bool toggle = cmd == 'O' || cmd == 'D' || cmd == 'H' || cmd == 'K' ||
                          cmd == 'k' || cmd == 'N' || cmd == 'F';
            auto tx = [&](int i) -> SDL_Texture* {
                return i >= 0 && i < int(tex.size()) ? tex[size_t(i)] : nullptr;
            };
            // Weapon slots composite the weapon's own icon (anims/weaponpic) -- the
            // WPrimaryButton GAF is just an empty recess. The pic already includes the
            // frame, so it replaces the slot art.
            if (cmd >= '1' && cmd <= '3' && front) {
                int slot = cmd - '1';
                SDL_Texture* wt = slot < int(front->type->weapons.size())
                    ? weaponIcon(front->type->weapons[size_t(slot)].name, active || hot)
                    : nullptr;
                if (wt) SDL_RenderCopyF(ren_, wt, nullptr, &r);
                else {
                    char n[2] = {cmd, 0};
                    float px = std::max(1.4f, r.h / 18.0f), tw = blockWidth(n, px);
                    blockText(n, r.x + (r.w - tw) * 0.5f, r.y + r.h * 0.28f, px,
                              {200, 190, 160, 255});
                }
            } else {
                SDL_Texture* lit = toggle ? (tx(1) ? tx(1) : tx(2)) : (tx(2) ? tx(2) : tx(1));
                SDL_Texture* dim = toggle ? (tx(2) ? tx(2) : tx(1))
                                          : (tx(1) ? tx(1) : tx(0));
                SDL_Texture* t = (active || hot) ? lit : dim;
                if (t) SDL_RenderCopyF(ren_, t, nullptr, &r);
                if (active && (!t || t == dim)) {   // emphasise when there's no lit face
                    SDL_SetRenderDrawColor(ren_, 255, 220, 90, 255);
                    SDL_RenderDrawRectF(ren_, &r);
                }
            }
            if (hot && !g.cmd.empty()) {
                float px = 1.6f, tw = blockWidth(g.cmd.c_str(), px);
                SDL_SetRenderDrawColor(ren_, 0, 0, 0, 210);
                SDL_FRect tb{r.x - tw - 14, r.y + r.h * 0.3f, tw + 10, 20};
                SDL_RenderFillRectF(ren_, &tb);
                blockText(g.cmd.c_str(), r.x - tw - 9, r.y + r.h * 0.3f + 3, px,
                          {235, 225, 180, 255});
            }
        }

        // Mana panel at the command-panel foot: the orb is a MANA BULB (its 24 frames
        // are liquid-fill levels, picked by mana fraction); "MANA X/Y" sits in a box
        // above it, with +income to the orb's left and -expenditure to its right.
        int cb = guiIdx("CrystalBall");
        if (cb >= 0) {
            const auto& tm = world_.player(localPlayer_);
            float cap = std::max(tm.storage, 100.0f);
            SDL_FRect orb = guiCmdRect(gui_.gadgets[cb]);
            if (!guiTex_[cb].empty()) {
                int nf = int(guiTex_[cb].size());
                float frac = std::clamp(tm.mana / cap, 0.0f, 1.0f);
                int fr = std::clamp(int(frac * float(nf - 1) + 0.5f), 0, nf - 1);
                if (guiTex_[cb][size_t(fr)])
                    SDL_RenderCopyF(ren_, guiTex_[cb][size_t(fr)], nullptr, &orb);
            }
            // "MANA" over "X/Y", both centred (H and V) in the panel's black HelpText
            // recess above the orb.
            int pmi = guiIdx("UnitMenu"), hti = guiIdx("HelpText");
            SDL_FRect panel = pmi >= 0 ? guiCmdRect(gui_.gadgets[pmi]) : orb;
            SDL_FRect mbox = hti >= 0 ? guiCmdRect(gui_.gadgets[hti])
                                      : SDL_FRect{panel.x + 6, orb.y - 60, panel.w - 12, 52};
            SDL_SetRenderDrawColor(ren_, 8, 8, 8, 235);
            SDL_RenderFillRectF(ren_, &mbox);
            SDL_SetRenderDrawColor(ren_, 70, 62, 44, 255);
            SDL_RenderDrawRectF(ren_, &mbox);
            char nums[32];
            std::snprintf(nums, sizeof nums, "%d/%d", int(tm.mana), int(cap));
            float px = std::max(1.4f, mbox.h / 20.0f);
            float gap = 3, lineH = 7 * px;
            // Shrink to fit both lines within the recess (H and V).
            while (px > 1.0f && (2 * lineH + gap > mbox.h - 4 ||
                                 blockWidth(nums, px) > mbox.w - 6)) {
                px -= 0.1f; lineH = 7 * px;
            }
            float y0 = mbox.y + (mbox.h - (2 * lineH + gap)) * 0.5f;
            SDL_Color mc{200, 215, 255, 255};
            float w1 = blockWidth("MANA", px), w2 = blockWidth(nums, px);
            blockText("MANA", mbox.x + (mbox.w - w1) * 0.5f, y0, px, mc);
            blockText(nums, mbox.x + (mbox.w - w2) * 0.5f, y0 + lineH + gap, px, mc);
            // +income / -expenditure (conjure + repair drain, computed here) flanking orb.
            float expend = 0;
            for (const auto& un : world_.units()) {
                if (un.player != localPlayer_ || !un.alive() || !un.type) continue;
                if (un.buildSiteId)
                    if (const auto* st = world_.unit(un.buildSiteId);
                        st && st->type && st->underConstruction) {
                        float total = st->type->buildTime / std::max(un.type->workerTime, 0.01f);
                        expend += st->type->buildCost / std::max(total, 0.01f);
                    }
                if (un.repairId)
                    if (const auto* t2 = world_.unit(un.repairId);
                        t2 && t2->type && t2->hp < t2->type->maxHp) {
                        float total = t2->type->buildTime / std::max(un.type->workerTime, 0.01f);
                        expend += t2->type->buildCost / std::max(total, 0.01f);
                    }
            }
            float ipx = std::max(1.5f, orb.h / 24.0f);
            char inb[16], outb[16];
            std::snprintf(inb, sizeof inb, "+%d", int(tm.income + 0.5f));
            std::snprintf(outb, sizeof outb, "-%d", int(expend + 0.5f));
            float iy = orb.y + orb.h * 0.5f - 3.5f * ipx;
            blockText(inb, orb.x - blockWidth(inb, ipx) - 5, iy, ipx, {150, 225, 150, 255});
            blockText(outb, orb.x + orb.w + 5, iy, ipx, {230, 160, 150, 255});
        }
    }

    // Returns true if the click hit (and was handled by) a command-panel button.
    bool guiClick(float mx, float my) {
        for (auto& [r, cmd] : guiBtnRects_) {
            if (mx < r.x || mx > r.x + r.w || my < r.y || my > r.y + r.h) continue;
            if (cmd == 's') {
                for (int id : selection_) {
                    tak::net::Command c;
                    c.kind = tak::net::Cmd::Stop;
                    c.unitId = id;
                    issue(c);
                }
            } else if (cmd >= '1' && cmd <= '3') {
                selectWeapon(cmd - '1');
            } else if (cmd == 'O' || cmd == 'D' || cmd == 'H') {
                int st = cmd == 'O' ? 0 : cmd == 'D' ? 1 : 2;
                issuePerUnit(tak::net::Cmd::Stance, st);
            } else if (cmd == 'K' || cmd == 'k') {
                issuePerUnit(tak::net::Cmd::Cloak, cmd == 'K' ? 1 : 0);
            } else if (cmd == 'N' || cmd == 'F') {
                issuePerUnit(tak::net::Cmd::SetActive, cmd == 'N' ? 1 : 0);
            } else {
                pendingCmd_ = cmd;
            }
            return true;
        }
        return false;
    }

    // Returns true if a click hit (and was handled by) a conjure/build icon. The icons
    // sit above the info bar (not inside it), so this is hit-tested independently of the
    // bottom-bar region -- placement arms for structures/mobile conjurers, else it
    // trains/unqueues at a building (Ctrl toggles infinite, Shift/Ctrl+Shift = 5/10).
    bool buildIconClick(float mx, float my, bool lmb, bool rmb) {
        if (!lmb && !rmb) return false;
        for (const auto& [r, bt] : iconRects_) {
            if (mx < r.x || mx > r.x + r.w || my < r.y || my > r.y + r.h) continue;
            const auto* b = selectedBuilder();
            if (!b || !bt) return true;   // consume the click even if it can't act
            uint16_t mod = SDL_GetModState();
            bool ctrl = (mod & KMOD_CTRL) != 0, shift = (mod & KMOD_SHIFT) != 0;
            if (isStructure(bt) || !isStructure(b->type)) {
                if (lmb) placing_ = bt;   // manual placement (buildings / mobile conjurers)
                return true;
            }
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
            return true;
        }
        return false;
    }

    // Issue a per-unit toggle command (targetId = value) to every selected own unit.
    void issuePerUnit(tak::net::Cmd kind, int value) {
        for (int id : selection_) {
            const auto* u = world_.unit(id);
            if (!u || u->player != localPlayer_) continue;
            tak::net::Command c;
            c.kind = kind;
            c.unitId = id;
            c.targetId = value;
            issue(c);
        }
    }

    // Draw a thin fill gauge (HP/mana) at a bar gadget's .gui position.
    void drawGauge(const char* name, float frac, SDL_Color c) {
        int gi = guiIdxLeft(name);
        if (gi < 0) return;
        SDL_FRect r = guiBarRect(gui_.gadgets[gi]);
        if (r.h < 5) { r.y -= (5 - r.h); r.h = 5; }   // the retail gauge is 2px -- lift for legibility
        frac = std::clamp(frac, 0.0f, 1.0f);
        SDL_SetRenderDrawColor(ren_, 8, 8, 8, 230);
        SDL_RenderFillRectF(ren_, &r);
        SDL_FRect f{r.x, r.y, r.w * frac, r.h};
        SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, 255);
        SDL_RenderFillRectF(ren_, &f);
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 200);
        SDL_RenderDrawRectF(ren_, &r);
    }

    // Retail bottom InfoPanel bar: chrome (InfoPanel + EndCap) plus the selected
    // unit's portrait/name/HP/mana at the .gui positions. Returns false (so drawPanel
    // keeps its own chrome) when no .gui is loaded. The build menu + mana readout stay
    // in drawPanel and draw on top.
    bool drawGuiInfoBar(int winW, int winH) {
        if (gui_.gadgets.empty()) return false;
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        float barTop = float(winH - barH());
        // Chrome: InfoPanel stretched across the width, EndCap at the left.
        int bi = guiIdx("BottomBar");
        if (bi >= 0 && !guiTex_[bi].empty() && guiTex_[bi][0]) {
            SDL_FRect r{0, barTop, float(winW), float(barH())};
            SDL_RenderCopyF(ren_, guiTex_[bi][0], nullptr, &r);
        } else {
            SDL_SetRenderDrawColor(ren_, 42, 38, 34, 255);
            SDL_FRect bar{0, barTop, float(winW), float(barH())};
            SDL_RenderFillRectF(ren_, &bar);
        }
        int ei = guiIdx("BottomEnd");
        if (ei >= 0 && !guiTex_[ei].empty() && guiTex_[ei][0]) {
            SDL_FRect r = guiBarRect(gui_.gadgets[ei]);
            SDL_RenderCopyF(ren_, guiTex_[ei][0], nullptr, &r);
        }
        // Unit info as a FIXED, always-present group centred in the bar (like retail):
        // UnitInfo1 (selected unit: portrait + name + HP/mana bars), ActionText
        // (status), and UnitInfo2 (the conjured target: name + progress bar). Both info
        // blocks always render -- empty bars when absent -- so the layout never shifts.
        {
            float vs = float(barH()) / 49.0f;
            float groupW = (512.0f - 59.0f) * vs;   // UnitInfo1.x .. UnitInfo2 right edge
            float off = (float(winW) - groupW) * 0.5f - 59.0f * vs;
            auto place = [&](int gi) {
                SDL_FRect r = guiBarRect(gui_.gadgets[gi]);
                r.x += off;
                return r;
            };
            auto bar = [&](int gi, float frac, SDL_Color c) {
                if (gi < 0) return;
                SDL_FRect r = place(gi);
                if (r.h < 5) { r.y -= (5 - r.h) * 0.5f; r.h = 5; }
                drawBar(r.x, r.y, r.w, r.h, frac, c);
            };
            const tak::sim::Unit* u =
                selection_.empty() ? nullptr : world_.unit(selection_.front());
            if (u && (!u->alive() || !u->type)) u = nullptr;
            float tpx = std::max(2.0f, float(barH()) / 24.0f);

            // The conjured target (site under construction, or the head of a build queue).
            std::string tName; float tProg = 0;
            if (u) {
                if (u->buildSiteId) {
                    if (const auto* s = world_.unit(u->buildSiteId); s && s->type) {
                        tName = s->type->name;
                        tProg = s->hp / std::max(1.0f, s->type->maxHp);
                    }
                } else if (!u->buildQueue.empty() && u->buildQueue.front()) {
                    tName = u->buildQueue.front()->name;
                    tProg = u->buildProgress / std::max(0.01f, u->buildQueue.front()->buildTime);
                }
            }

            // --- UnitInfo1: selected unit ---
            int ii = guiIdx("UnitImage");
            if (ii >= 0) {
                SDL_FRect pr = place(ii);
                SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
                SDL_RenderFillRectF(ren_, &pr);
                if (u) {
                    SDL_Texture* ic = iconFor(u->type->id);
                    if (!ic) ic = modelIconTex(u->type->id, colorSlot_[localPlayer_ & 7],
                                               u->type->maxVel > 0);
                    if (ic) SDL_RenderCopyF(ren_, ic, nullptr, &pr);
                }
                SDL_SetRenderDrawColor(ren_, 96, 84, 60, 255);
                SDL_RenderDrawRectF(ren_, &pr);
                if (u && u->veteran > 0) {
                    int xi = guiIdxLeft("Experience");
                    int tier = u->veteran >= 7 ? 2 : u->veteran >= 4 ? 1 : 0;
                    if (xi >= 0 && tier < int(guiTex_[xi].size()) && guiTex_[xi][size_t(tier)]) {
                        SDL_FRect cr{pr.x + 1, pr.y + pr.h - 22 * vs - 1, 11 * vs, 22 * vs};
                        SDL_RenderCopyF(ren_, guiTex_[xi][size_t(tier)], nullptr, &cr);
                    }
                }
            }
            if (int t1 = guiIdxLeft("UnitText"); u && t1 >= 0) {
                SDL_FRect nr = place(t1);
                blockText(u->type->name, nr.x, nr.y, tpx, {236, 226, 192, 255});
            }
            bar(guiIdxLeft("HealthBar"), u ? u->hp / std::max(1.0f, u->type->maxHp) : 0.0f,
                {210, 70, 60, 255});
            bar(guiIdxLeft("ManaBar"),
                (u && u->type->maxMana > 0) ? u->mana / u->type->maxMana : 0.0f,
                {90, 150, 255, 255});

            // --- ActionText: status (or +N MORE for a multi-selection) ---
            if (int ai = guiIdx("ActionText"); u && ai >= 0) {
                SDL_FRect ar = place(ai);
                std::string st = selection_.size() > 1
                    ? "+" + std::to_string(selection_.size() - 1) + " MORE"
                    : std::string(unitStatusText(u));
                blockText(st, ar.x, ar.y, tpx, {170, 205, 255, 255});
            }

            // --- UnitInfo2: the conjured target ---
            if (int t2 = guiIdxRight("UnitText"); !tName.empty() && t2 >= 0) {
                SDL_FRect nr = place(t2);
                std::transform(tName.begin(), tName.end(), tName.begin(), ::toupper);
                blockText(tName, nr.x, nr.y, tpx, {236, 226, 192, 255});
            }
            bar(guiIdxRight("HealthBar"), std::clamp(tProg, 0.0f, 1.0f), {210, 70, 60, 255});
            bar(guiIdxRight("ManaBar"), 0.0f, {90, 150, 255, 255});
        }
        return true;
    }

    // A one-word description of what the selected unit is doing, for the command
    // panel's HelpText recess.
    const char* unitStatusText(const tak::sim::Unit* u) const {
        if (!u || !u->type) return "";
        if (u->stonedFor > 0) return "PETRIFIED";
        if (u->frozenFor > 0) return "FROZEN";
        if (u->paralyzedFor > 0) return "PARALYZED";
        if (u->underConstruction) return "UNDER CONSTRUCTION";
        if (!u->active) return "INACTIVE";
        if (u->repairId != 0) return "REPAIRING";
        if (u->reclaimId != 0 || !u->reclaimQueue.empty()) return "RECLAIMING";
        if (u->buildSiteId != 0 || !u->buildQueue.empty()) return "CONJURING";
        if (!u->orders.empty()) {
            const auto& o = u->orders.front();
            if (o.guard) return "GUARDING";
            if (o.patrol) return "PATROLLING";
            if (o.load) return "BOARDING";
            if (o.unload) return "UNLOADING";
            if (o.targetId != 0) return "ATTACKING";
            if (o.attackMove) return "ADVANCING";
            return "MOVING";
        }
        if (u->cloaked) return "CLOAKED";
        return "STANDBY";   // retail's idle label
    }

    // What a builder is currently conjuring/building, for the info bar (else "").
    std::string conjureTargetName(const tak::sim::Unit* u) const {
        if (!u || !u->type) return {};
        if (u->buildSiteId != 0)
            if (const auto* site = world_.unit(u->buildSiteId); site && site->type)
                return site->type->name;
        if (!u->buildQueue.empty() && u->buildQueue.front())
            return u->buildQueue.front()->name;
        return {};
    }

    // A simple filled bar (HP/mana) at explicit pixel coords.
    void drawBar(float x, float y, float w, float h, float frac, SDL_Color c) {
        frac = std::clamp(frac, 0.0f, 1.0f);
        SDL_FRect r{x, y, w, h};
        SDL_SetRenderDrawColor(ren_, 8, 8, 8, 235);
        SDL_RenderFillRectF(ren_, &r);
        SDL_FRect f{x, y, w * frac, h};
        SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, 255);
        SDL_RenderFillRectF(ren_, &f);
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 200);
        SDL_RenderDrawRectF(ren_, &r);
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

    std::map<std::string, SDL_Texture*> weaponIcons_;
    // Weapon-slot icon: anims/weaponpic/<name>{sb,sbh}.jpg (name lowercased, spaces
    // stripped; sb = normal, sbh = selected/gold), falling back to the default_* pics
    // for weapons that ship no icon. These 32x32 JPGs are the full button (icon +
    // recessed frame), so they replace the empty WPrimaryButton recess.
    SDL_Texture* weaponIcon(const std::string& wname, bool selected) {
        std::string base;
        for (char c : wname)
            if (c != ' ') base += char(std::tolower((unsigned char)c));
        std::string key = base + (selected ? "#s" : "#n");
        if (auto it = weaponIcons_.find(key); it != weaponIcons_.end()) return it->second;
        SDL_Texture* tex = nullptr;
        const char* suf = selected ? "sbh" : "sb";
        std::string paths[2] = {"anims/weaponpic/" + base + suf + ".jpg",
                                selected ? "anims/weaponpic/default_selected.jpg"
                                         : "anims/weaponpic/default_up.jpg"};
        for (const auto& path : paths) {
            try {
                auto img = tak::jpeg::load(vread(path));
                tex = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_STATIC, img.width, img.height);
                SDL_UpdateTexture(tex, nullptr, img.rgba.data(), img.width * 4);
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                break;
            } catch (const std::exception&) {}
        }
        weaponIcons_[key] = tex;
        return tex;
    }

    SDL_Texture* iconFor(const std::string& typeId) {
        auto it = icons_.find(typeId);
        if (it != icons_.end()) return it->second;
        SDL_Texture* tex = nullptr;
        try {
            auto img = tak::jpeg::load(vread("anims/buildpic/" + typeId + ".jpg"));
            tex = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STATIC, img.width, img.height);
            SDL_UpdateTexture(tex, nullptr, img.rgba.data(), img.width * 4);
        } catch (const std::exception&) {}
        icons_[typeId] = tex;
        return tex;
    }

    // Fallback build icon: render the unit's 3D model into a small cached texture,
    // for buildables that ship no anims/buildpic. Retail never drew a build pic for
    // the Zhon trapdoor spider (zonspide) -- a base-game creature the Crusades
    // balance made buildable -- so without this its slot would be an empty box.
    SDL_Texture* modelIconTex(const std::string& id, int slot, bool canMove) {
        AaScaleReset _sr(ren_);
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
            if (mp_) {   // lockstep: only the host may re-cadence, and only if unlocked
                if (!mp_->room().opts.speedUnlock) {
                    notice_ = "SPEED LOCKED (host can unlock in the lobby)";
                    noticeTimer_ = 2; return true;
                }
                if (mp_->room().hostId != mp_->myClientId()) {
                    notice_ = "ONLY THE HOST CAN CHANGE SPEED";
                    noticeTimer_ = 2; return true;
                }
                auto o = mp_->room().opts;
                int ns = std::clamp(int(mp_->gameSpeed()) + (up ? 5 : -5), 5, 40);
                if (ns != o.speed) { o.speed = uint8_t(ns); mp_->setGameOptions(o); }
                char nb[24]; std::snprintf(nb, sizeof nb, "GAME SPEED %.1fx", ns / 10.0f);
                notice_ = nb; noticeTimer_ = 2;
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
        // One emote at a time -- don't even send the command while a disco or headbang
        // is already running (the sim enforces this too, deterministically). The busy
        // notice reflects what you're ACTUALLY doing, not the key you pressed.
        bool emoting = world_.discoActive(localPlayer_) || world_.headbangActive(localPlayer_);
        const char* busy = world_.discoActive(localPlayer_) ? "ALREADY GROOVING" : "ALREADY ROCKING";
        if (key == SDLK_d && shift) {             // DISCO! your monarchs boogie 10s
            if (emoting) { notice_ = busy; noticeTimer_ = 2; return true; }
            // Purely cosmetic, but routed through the lockstep command path so every
            // peer sees your monarchs dance (the sim just runs a per-player timer).
            tak::net::Command c;
            c.kind = tak::net::Cmd::Disco;
            issue(c);                             // issue() stamps c.player = localPlayer_
            notice_ = "DISCO TIME";
            noticeTimer_ = 2;
            return true;
        }
        if (key == SDLK_h && shift) {             // HEADBANG! your monarchs mosh 10s
            if (emoting) { notice_ = busy; noticeTimer_ = 2; return true; }
            tak::net::Command c;
            c.kind = tak::net::Cmd::Headbang;
            issue(c);
            notice_ = "HEADBANG!!";
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
        if (digit >= 0 && !spectating_) {   // control groups: watch-only can't select
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
        if (spectating_) return;   // watch-only
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
        return sx >= 0 && sy >= 0 && sx <= float(winW_) && sy <= float(winH_) - barH();
    }

    // Cycle the selection to the next owned unit (single-select stepping).
    void cycleNextUnit() {
        if (spectating_) return;   // watch-only
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
    // Lobby design size (logical). The lobby always lays out at exactly this size and
    // is scaled to fit + centred in the window (lobbyScale_ / lobbyOffX_/Y_), so it
    // shows fully at any window size or aspect. Hit-tests undo the same transform.
    static constexpr float kLobbyW = 960.0f;
    static constexpr float kLobbyH = 540.0f;
    bool lbHot(const SDL_FRect& r) const {
        float mx = mouseX_ / lobbyScale_ - lobbyOffX_, my = mouseY_ / lobbyScale_ - lobbyOffY_;
        return mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h;
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
        // Shrink the label if it would overflow the button (keeps long captions like
        // "OVERRIDES: COSMETIC" inside their box). 8px total horizontal padding.
        float px = 2.0f;
        float fit = (w - 8.0f) / std::max<size_t>(1, label.size()) / 6.0f;
        if (fit < px) px = std::max(fit, 1.0f);
        float tw = blockWidth(label, px);
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
        // Map-picker geometry is only live while the create screen is shown; clear it
        // so a stale thumb/list rect can't grab clicks or wheel on the other screens.
        mapListRect_ = mapThumbRect_ = SDL_FRect{0, 0, 0, 0};
        // absorb any new chat
        if (mp_) for (auto& m : mp_->takeChat()) chatLog_.push_back(m);
        // The ground + centred panel frame are painted by the caller (the viewport is
        // already offset to this panel); RenderClear would ignore the viewport.
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        float cx = winW / 2.0f;
        const char* title = singlePlayer_ ? "SINGLE PLAYER VS AI" : "TA:KINGDOMS  MULTIPLAYER";
        blockText(title, cx - blockWidth(title, 2.6f) / 2, 24, 2.6f, {210, 200, 150, 255});
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
        lbBtn(winW - 140.0f, winH - 40.0f, 120, 30, "BACK", true, [this] { menuRequested_ = true; });
    }

    // Build the selected map's preview texture from the .tnt's embedded minimap
    // (8-bit indexed -> RGBA via palettes/palette.pal). Rebuilt only when the
    // selection changes; a bad/missing tnt just leaves no preview.
    void buildMapPreview(const std::string& tntPath) {
        if (mapPreviewTex_) { SDL_DestroyTexture(mapPreviewTex_); mapPreviewTex_ = nullptr; }
        mapPreviewFor_ = tntPath;
        mapPreviewW_ = mapPreviewH_ = 0;
        mapPreviewDims_.clear();
        if (tntPath.empty()) return;
        tak::tnt::Map m;
        try { m = tak::tnt::Map::load(vfs_.read(tntPath), tntPath); } catch (...) { return; }
        if (m.minimap.empty() || m.minimapW <= 0 || m.minimapH <= 0) return;
        if (mapPalRgba_.empty()) {   // cache the standard game palette once
            try {
                auto pal = tak::gaf::Palette::fromBytes(vfs_.read("palettes/palette.pal"), "palette.pal");
                mapPalRgba_.assign(&pal.rgba[0][0], &pal.rgba[0][0] + 256 * 4);
            } catch (...) { return; }
        }
        int w = m.minimapW, h = m.minimapH;
        std::vector<uint8_t> rgba(size_t(w) * h * 4);
        for (size_t i = 0; i < size_t(w) * h; ++i) {
            const uint8_t* c = &mapPalRgba_[size_t(m.minimap[i]) * 4];
            rgba[i * 4 + 0] = c[0]; rgba[i * 4 + 1] = c[1]; rgba[i * 4 + 2] = c[2]; rgba[i * 4 + 3] = 255;
        }
        mapPreviewTex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
        if (!mapPreviewTex_) return;
        SDL_UpdateTexture(mapPreviewTex_, nullptr, rgba.data(), w * 4);
        SDL_SetTextureScaleMode(mapPreviewTex_, SDL_ScaleModeLinear);
        mapPreviewW_ = w; mapPreviewH_ = h;
        mapPreviewDims_ = std::to_string(m.width) + " X " + std::to_string(m.height);
    }

    void drawCreate(int winW, int winH) {
        (void)winW; (void)winH;
        float x = 80, y = 90;
        blockText(singlePlayer_ ? "SINGLE PLAYER VS AI" : "CREATE GAME", x, y, 2.2f,
                  {200, 205, 220, 255}); y += 40;
        // A private single-player game needs no name or password.
        if (!singlePlayer_) {
            lbField(x, y, 260, "GAME NAME", createName_, 1); y += 46;
            lbField(x, y, 260, "PASSWORD (optional)", createPass_, 2); y += 46;
        }
        blockText(std::string("MAP: ") + mpMapId_, x, y, 1.8f, {180, 185, 195, 255}); y += 30;
        lbBtn(x, y, 170, 26, createCrusades_ ? "CRUSADES: ON" : "CRUSADES: OFF", true,
              [this] { createCrusades_ = !createCrusades_; }); y += 34;
        lbBtn(x, y, 170, 26, createGods_ ? "GODS: ON" : "GODS: OFF", true,
              [this] { createGods_ = !createGods_; }); y += 34;
        // Override tier for the game: NONE (pure retail) / COSMETIC (art & sound
        // may differ) / FULL (gameplay overrides allowed but every player must
        // have the same ones). The host's own launch tier caps it (you can't offer
        // FULL if you didn't mount your gameplay overrides).
        static const char* kTier[] = {"NONE", "COSMETIC", "FULL"};
        lbBtn(x, y, 240, 26, std::string("OVERRIDES: ") + kTier[createOverride_ & 3], true,
              [this] { createOverride_ = uint8_t((createOverride_ + 1) % 3); }); y += 44;
        lbBtn(x, y, 120, 30, "CREATE", !createName_.empty(), [this] {
            tak::net::GameOptions o; o.crusades = createCrusades_ ? 1 : 0; o.gods = createGods_ ? 1 : 0;
            o.overridePolicy = createOverride_;
            mp_->createGame(createName_, createPass_, mpMapId_, o, mpCapacity());
            lobbyScreen_ = LobbyScreen::Browser;
        });
        if (!singlePlayer_)   // a private single-player game has no browser to go back to
            lbBtn(x + 132, y, 110, 30, "BROWSER", true, [this] { lobbyScreen_ = LobbyScreen::Browser; });
        // Back button at the bottom-left, where back buttons live.
        lbBtn(x, kLobbyH - 40, 120, 30, "BACK", true, [this] { menuRequested_ = true; });

        // Map picker (right column): a scrollable list box. Selecting sets both the
        // wire id (mpMapId_ = bare .tnt stem) and mapPath_, so mpCapacity() recomputes
        // the chosen map's start-position count.
        if (mapList_.empty()) mapList_ = tak::hpi::listMaps(vfs_);
        // Adopt the remembered map once (persisted across launches), and scroll to it.
        if (!mapPrefApplied_ && settings_ && !settings_->lastMap.empty()) {
            for (int i = 0; i < int(mapList_.size()); ++i)
                if (mapList_[size_t(i)].first == settings_->lastMap) {
                    mpMapId_ = mapList_[size_t(i)].first;
                    mapPath_ = mapList_[size_t(i)].second;
                    mapScroll_ = std::max(0, i - 3);
                    break;
                }
            mapPrefApplied_ = true;
        }
        float lx = 400, hy = 90;
        blockText("SELECT MAP", lx, hy, 1.8f, {200, 205, 220, 255});
        const float boxX = lx, boxY = hy + 24, boxW = 340, boxH = 366, rowH = 24, sbW = 12;
        const int total = int(mapList_.size());
        mapVisRows_ = int(boxH / rowH);
        mapTotalRows_ = total;
        clampMapScroll();
        mapListRect_ = {boxX, boxY, boxW, boxH};
        // box background + border
        SDL_FRect box{boxX, boxY, boxW, boxH};
        SDL_SetRenderDrawColor(ren_, 24, 26, 34, 255); SDL_RenderFillRectF(ren_, &box);
        SDL_SetRenderDrawColor(ren_, 70, 76, 96, 255); SDL_RenderDrawRectF(ren_, &box);
        const float rowW = boxW - sbW - 4;
        const int maxCh = int((rowW - 12) / 12);   // chars that fit at px 2.0
        for (int r = 0; r < mapVisRows_; ++r) {
            int i = mapScroll_ + r;
            if (i >= total) break;
            const std::string& nm = mapList_[size_t(i)].first;
            const std::string& pth = mapList_[size_t(i)].second;
            bool sel = (nm == mpMapId_);
            SDL_FRect row{boxX + 2, boxY + r * rowH, rowW, rowH};
            bool hot = lbHot(row);
            SDL_Color rc = sel ? SDL_Color{44, 78, 44, 255}
                         : hot ? SDL_Color{40, 46, 62, 255} : SDL_Color{24, 26, 34, 255};
            SDL_SetRenderDrawColor(ren_, rc.r, rc.g, rc.b, 255); SDL_RenderFillRectF(ren_, &row);
            blockText(nm.size() > size_t(maxCh) ? nm.substr(0, size_t(maxCh)) : nm,
                      boxX + 8, row.y + (rowH - 14) / 2, 2.0f,
                      sel ? SDL_Color{200, 240, 200, 255} : SDL_Color{220, 225, 235, 255});
            lobbyHots_.push_back({row, [this, nm, pth] {
                mpMapId_ = nm; mapPath_ = pth;
                if (settings_) { settings_->lastMap = nm; saveSettings(*settings_); }  // remember it
            }});
        }
        // Scrollbar: track + a proportional, draggable thumb (also wheel-scrollable).
        if (total > mapVisRows_) {
            float trackX = boxX + boxW - sbW;
            SDL_FRect track{trackX, boxY, sbW, boxH};
            SDL_SetRenderDrawColor(ren_, 30, 32, 42, 255); SDL_RenderFillRectF(ren_, &track);
            float thumbH = std::max(24.0f, boxH * mapVisRows_ / total);
            float thumbY = boxY + (boxH - thumbH) * mapScroll_ / float(total - mapVisRows_);
            mapThumbRect_ = {trackX, thumbY, sbW, thumbH};
            SDL_SetRenderDrawColor(ren_, mapDrag_ ? 130 : 90, mapDrag_ ? 150 : 100,
                                   mapDrag_ ? 190 : 130, 255);
            SDL_RenderFillRectF(ren_, &mapThumbRect_);
        } else {
            mapThumbRect_ = {0, 0, 0, 0};
        }

        // Preview of the selected map (its embedded minimap), right of the list.
        if (mapPreviewFor_ != mapPath_) buildMapPreview(mapPath_);
        const float pvx = boxX + boxW + 12, pvy = boxY, pvW = 192, pvH = 192;
        blockText("PREVIEW", pvx, hy, 1.8f, {200, 205, 220, 255});
        SDL_FRect pbox{pvx, pvy, pvW, pvH};
        SDL_SetRenderDrawColor(ren_, 18, 20, 28, 255); SDL_RenderFillRectF(ren_, &pbox);
        if (mapPreviewTex_ && mapPreviewW_ > 0) {
            float sc = std::min(pvW / float(mapPreviewW_), pvH / float(mapPreviewH_));
            float iw = mapPreviewW_ * sc, ih = mapPreviewH_ * sc;
            SDL_FRect dst{pvx + (pvW - iw) / 2, pvy + (pvH - ih) / 2, iw, ih};
            SDL_RenderCopyF(ren_, mapPreviewTex_, nullptr, &dst);
        } else {
            blockText("NO PREVIEW", pvx + 34, pvy + pvH / 2 - 7, 1.6f, {120, 125, 140, 255});
        }
        SDL_SetRenderDrawColor(ren_, 70, 76, 96, 255); SDL_RenderDrawRectF(ren_, &pbox);
        if (!mapPreviewDims_.empty())
            blockText(mapPreviewDims_, pvx, pvy + pvH + 8, 1.6f, {160, 165, 180, 255});
    }

    void drawRoom(int winW, int winH) {
        const auto& room = mp_->room();
        float x = 40, y = 78;
        blockText(room.name, x, y, 2.2f, {210, 210, 220, 255});
        blockText(std::string("MAP  ") + room.mapId, x + winW - 320, y + 4, 1.8f, {180, 185, 195, 255});
        // Override tier for this game (joiners adopt it; FULL needs matching gameplay files).
        static const char* kTier[] = {"NONE", "COSMETIC", "FULL"};
        blockText(std::string("OVERRIDES  ") + kTier[room.opts.overridePolicy & 3],
                  x + winW - 320, y + 22, 1.5f, {150, 175, 150, 255});
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
                // Host cycles an empty slot OPEN -> AI -> CLOSED. AI opponents are
                // seated (and run) on the server; this is how you set up multi-AI
                // games (including single-player vs several AIs).
                SDL_FRect tb{x + 34, y + 6, 60, 18};
                lobbyHots_.push_back({tb, [this, i, t = s.type] {
                    uint8_t nt = t == 0 ? 2 : (t == 2 ? 3 : 0);
                    const auto& s2 = mpRoom().slots[i];
                    mp_->setSlot(i, nt, s2.faction, s2.color, s2.team, 0); }});
            }
            if (s.type == 1) blockText(s.name, x + 100, y + 8, 1.8f, {225, 228, 236, 255});
            else if (s.type == 2)
                blockText(s.name.empty() ? "Computer" : s.name, x + 100, y + 8, 1.8f,
                          {210, 200, 150, 255});
            // faction / color / team edit: your own row, or (host) any AI row.
            bool canEdit = mine || (host && s.type == 2);
            blockText(factionName(s.faction), x + 260, y + 8, 1.6f, {200, 205, 215, 255});
            if (canEdit) { SDL_FRect fb{x + 260, y + 6, 90, 18};
                lobbyHots_.push_back({fb, [this, i] { const auto& s2 = mpRoom().slots[i];
                    mp_->setSlot(i, s2.type, (s2.faction + 1) % 5, s2.color, s2.team, s2.ready); }}); }
            colorSwatch(x + 360, y + 5, 20, s.color, canEdit ? std::function<void()>([this, i] {
                const auto& s2 = mpRoom().slots[i];
                mp_->setSlot(i, s2.type, s2.faction, (s2.color + 1) % 10, s2.team, s2.ready); }) : nullptr);
            char tm[8]; std::snprintf(tm, sizeof tm, "T%d", s.team + 1);
            blockText(tm, x + 392, y + 8, 1.8f, {200, 205, 215, 255});
            if (canEdit) { SDL_FRect teb{x + 392, y + 6, 34, 18};
                lobbyHots_.push_back({teb, [this, i] { const auto& s2 = mpRoom().slots[i];
                    mp_->setSlot(i, s2.type, s2.faction, s2.color, uint8_t((s2.team + 1) % tak::net::kMaxSlots), s2.ready); }}); }
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
        // The game starts at normal speed; the host can allow it to be changed
        // in-game, and the host's -/+ keys then re-cadence the match live.
        y += 40;
        if (host) {
            std::string ub = std::string("ALLOW SPEED CHANGE IN-GAME: ") + (room.opts.speedUnlock ? "ON" : "OFF");
            lbBtn(x, y, 420, 26, ub, true, [this] {
                auto o = mpRoom().opts; o.speedUnlock = o.speedUnlock ? 0 : 1;
                mp_->setGameOptions(o); });
        }
        // Per-player unit cap (host cycles 250/500/1000/2000/5000; everyone sees it).
        y += 34;
        char cb[40]; std::snprintf(cb, sizeof cb, "UNIT CAP  %d", int(room.opts.unitCap));
        blockText(cb, x, y + 6, 2.0f, {205, 210, 225, 255});
        if (host) {
            lbBtn(x + 220, y, 90, 26, "CHANGE", true, [this] {
                static const uint16_t seq[] = {250, 500, 1000, 2000, 5000};
                auto o = mpRoom().opts; int idx = 3;   // default 2000
                for (int k = 0; k < 5; ++k) if (seq[k] == o.unitCap) idx = k;
                o.unitCap = seq[(idx + 1) % 5];
                mp_->setGameOptions(o); });
        }
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

    // A panel-local logical point, undoing the lobby's fit-scale + centre offset.
    void lobbyMouse(float& mx, float& my) const {
        mx = mouseX_ / lobbyScale_ - lobbyOffX_;
        my = mouseY_ / lobbyScale_ - lobbyOffY_;
    }
    static bool ptIn(const SDL_FRect& r, float x, float y) {
        return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
    }
    void clampMapScroll() {
        mapScroll_ = std::clamp(mapScroll_, 0, std::max(0, mapTotalRows_ - mapVisRows_));
    }
    // Set the map scroll from the current thumb-drag mouse position: map the cursor's
    // y within the list box to a first-visible-row index.
    void setMapScrollFromThumb() {
        if (mapListRect_.h <= 0 || mapTotalRows_ <= mapVisRows_) return;
        float mx, my; lobbyMouse(mx, my);
        float frac = (my - mapListRect_.y) / mapListRect_.h;   // 0..1 down the box
        mapScroll_ = int(std::lround(frac * mapTotalRows_ - mapVisRows_ * 0.5f));
        clampMapScroll();
    }
    void lobbyInput(const SDL_Event& e, int winW, int winH) {
        (void)winW; (void)winH;
        if (e.type == SDL_MOUSEMOTION) {
            mouseX_ = float(e.motion.x); mouseY_ = float(e.motion.y);
            if (mapDrag_) setMapScrollFromThumb();   // dragging the map scrollbar
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            mapDrag_ = false;
        } else if (e.type == SDL_MOUSEWHEEL) {
            float mx, my; lobbyMouse(mx, my);
            if (ptIn(mapListRect_, mx, my)) {   // scroll the map list under the cursor
                mapScroll_ -= e.wheel.y;
                clampMapScroll();
            }
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            mouseX_ = float(e.button.x); mouseY_ = float(e.button.y);
            lbField_ = 0; SDL_StopTextInput();
            float mx, my; lobbyMouse(mx, my);
            if (ptIn(mapThumbRect_, mx, my)) { mapDrag_ = true; return; }   // grab the thumb
            for (auto& [r, action] : lobbyHots_)
                if (ptIn(r, mx, my)) { action(); break; }
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
                if (playerAi_[t & 7]) s = "AI - " + s;   // computer opponents: "AI - <name>"
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
        float y0 = float(winH) - barH() - 52;
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
        // Bottom bar: the retail InfoPanel chrome + unit info when a .gui is loaded,
        // else our own stone strip. The build menu + mana readout below draw on top.
        bool guiBar = drawGuiInfoBar(winW, winH);
        SDL_FRect bar{0, float(winH - barH()), float(winW), float(barH())};
        if (!guiBar) {
            if (botTex_) {
                for (int x = 0; x < winW; x += botW_) {
                    SDL_Rect dst{x, winH - barH(), botW_, barH()};
                    SDL_RenderCopy(ren_, botTex_, nullptr, &dst);
                }
            } else if (panelTex_) {
                for (int x = 0; x < winW; x += panelW_) {
                    SDL_Rect src{0, 40, panelW_, barH()};
                    SDL_Rect dst{x, winH - barH(), panelW_, barH()};
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
        }
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);

        char buf[96];
        SDL_Color fc = factionColor();
        // A solid faction-coloured panel with a dark frame — black text on top.
        auto shade = [&](float x, float w) {
            SDL_FRect z{x, bar.y + 4, w, barH() - 8.0f};
            SDL_SetRenderDrawColor(ren_, fc.r, fc.g, fc.b, 255);
            SDL_RenderFillRectF(ren_, &z);
            SDL_SetRenderDrawColor(ren_, 20, 18, 16, 255);
            SDL_RenderDrawRectF(ren_, &z);
        };

        // Bottom-LEFT: portrait + stats for the selected unit. Skipped when the retail
        // InfoPanel bar already drew the unit info (drawGuiInfoBar).
        if (!guiBar && !selection_.empty() && statFont_.ok()) {
            const auto* u = world_.unit(selection_.front());
            if (u && u->alive() && u->type) {
                float px = 8;
                shade(px, 288);
                SDL_Texture* ic = iconFor(u->type->id);
                if (ic) {
                    SDL_FRect pr{px + 4, bar.y + 8, 56, barH() - 20.0f};
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

        // Bottom-LEFT conjure menu: clickable build icons for the selected builder,
        // in a horizontal row just above the info bar's left end -- where retail draws
        // it (not the old centred strip over the map).
        iconRects_.clear();
        const auto* b = selectedBuilder();
        if (b) {
            const auto& menu = registry_.buildable(b->type->id);
            int n = int(menu.size());
            float iconSz = float(barH()) - 10.0f;
            float gap = 6.0f;
            float rowW = n > 0 ? (n - 1) * (iconSz + gap) + iconSz : 0;
            float x0 = 10;                          // left-aligned
            float iconY = bar.y - iconSz - 5;       // sit just above the bar
            float x = x0;
            // A recessed container behind the row so the conjure menu reads as one HUD
            // strip (matching the InfoPanel bar's dark inset + bronze frame).
            if (n > 0 && guiBar) {
                SDL_FRect box{x0 - 5, iconY - 5, rowW + 10, iconSz + 10};
                SDL_SetRenderDrawColor(ren_, 14, 12, 10, 225);
                SDL_RenderFillRectF(ren_, &box);
                SDL_SetRenderDrawColor(ren_, 96, 84, 60, 255);
                SDL_RenderDrawRectF(ren_, &box);
            }
            for (int i = 0; i < n; ++i) {
                const auto* bt = registry_.find(menu[size_t(i)]);
                if (!bt) continue;
                SDL_FRect r{x, iconY, iconSz, iconSz};
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
                    float tipx = std::clamp(r.x + iconSz / 2 - tw / 2, 6.0f, winW - tw - 6);
                    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 210);
                    SDL_FRect tb{tipx - 6, iconY - 28, tw + 12, 26};
                    SDL_RenderFillRectF(ren_, &tb);
                    blockText(tip, tipx, iconY - 24, px, {255, 240, 190, 255});
                }
                iconRects_.push_back({r, bt});
                x += iconSz + gap;
            }
            if (!b->buildQueue.empty()) {
                char q[64];
                std::snprintf(q, sizeof q, "TRAINING %s (%zu)",
                              b->buildQueue.front()->name.c_str(),
                              b->buildQueue.size());
                blockText(q, x0, iconY - 24, 1.8f, {160, 210, 255, 255});
            }
        }

        // Bottom-RIGHT: mana -- only on our own bar. The retail GUI bar draws the mana
        // readout at the command-panel foot around the orb (renderGui) instead.
        if (!guiBar) {
            auto& tm = world_.player(localPlayer_);
            float manaX = float(winW) - 192;
            shade(manaX - 8, 200);
            SDL_Color txt{0, 0, 0, 255};
            blockText("MANA", manaX, bar.y + 9, 2.0f, txt);
            std::snprintf(buf, sizeof buf, "%d/%d", int(tm.mana),
                          int(std::max(tm.storage, 100.0f)));
            blockText(buf, manaX, bar.y + 30, 2.3f, txt);
            std::snprintf(buf, sizeof buf, "+%d/SEC", int(tm.income));
            blockText(buf, manaX, bar.y + 52, 1.8f, txt);
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
        // Full footprint width plus a one-cell gap: edge-to-edge spacing lets integer
        // cell rounding of the un-aligned drag tip adjacent sites into a shared
        // footprint cell, which makes canPlace reject every other one.
        float sp = (std::max({placing_->footX, placing_->footZ, 1}) + 1) * 16.0f;
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
        // Resolve the elevated cell drawn under the cursor. Draw a translucent ghost
        // of the thing being placed, lifted onto the relief exactly like the finished
        // unit/building -- a plain ghost where it can go, red-washed where it can't.
        float wx, wz;
        pickWorld(mouseX_, mouseY_, wx, wz);
        bool ok = world_.canPlace(placing_, wx, wz);
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        drawGhostAt(placing_, wx, wz, !ok);
        // Small name tag above the ghost so the player still sees what's queued.
        float cxp = (wx - mapView_.offX()) * zm - terrainLiftX(wx, wz) * zm;
        float czp = (wz - mapView_.offY()) * zm - terrainLift(wx, wz) * zm;
        float hh = float(placing_->footZ) * 8 * zm;
        float px = 1.8f;
        float tw = blockWidth(placing_->name, px);
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 190);
        SDL_FRect tb{cxp - tw / 2 - 4, czp - hh - 24, tw + 8, 20};
        SDL_RenderFillRectF(ren_, &tb);
        blockText(placing_->name, cxp - tw / 2, czp - hh - 20, px, {230, 230, 200, 255});
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
            auto root = vtdf("gamedata/explosions/explosions.tdf");
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
        // "file:sequence" targets a specific GAF sequence (e.g. "flames:flame large");
        // a bare name uses the file of that name and its like-named (or first) sequence.
        std::string file = animName, seqWant = animName;
        if (auto c = animName.find(':'); c != std::string::npos) {
            file = animName.substr(0, c);
            seqWant = animName.substr(c + 1);
        }
        for (const std::string suf : {"_4444.taf", "_1555.taf", ".taf", ".gaf"}) {
            if (!ea.frames.empty()) break;
            try {
                std::string ap = "anims/" + file + suf;
                auto seqs = tak::gaf::load(vread(ap), pal ? *pal : tak::gaf::Palette{}, -1, ap);
                const tak::gaf::Sequence* seq = nullptr;
                for (auto& s : seqs) {
                    if (s.frames.empty()) continue;
                    if (!seq) seq = &s;
                    std::string sn = s.name;
                    std::transform(sn.begin(), sn.end(), sn.begin(), ::tolower);
                    if (sn == seqWant) { seq = &s; break; }
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

    // Persistent per-unit ambient fire/smoke (emit-sfx). One looping flame/smoke per
    // unit, cycled off the continuous animClock so it flows smoothly (no restart/gaps
    // like re-spawned one-shots), kept alive while the unit's emit-loop keeps firing.
    void drawUnitFx() {
        float zm = mapView_.zoom();
        const float kLinger = 0.8f;   // seconds after the last emit to keep drawing
        for (auto& [id, a] : anims_) {
            bool fire = a.fireFx && a.fireT < kLinger;
            bool smk = a.smokeFx && a.smokeT < kLinger;
            if (!fire && !smk) continue;
            const auto* u = world_.unit(id);
            if (!u || !u->type) continue;
            if (!noFog_ && !world_.cellVisible(u->x, u->z)) continue;
            auto draw = [&](const EffectAnim* ea, float lift, float fps) {
                int nf = int(ea->frames.size());
                int fi = int(animClock_ * fps + float(id) * 0.37f) % nf;
                if (fi < 0) fi += nf;
                const EFrame& f = ea->frames[size_t(fi)];
                float sx = (u->x - mapView_.offX()) * zm - f.ax * zm - terrainLiftX(u->x, u->z) * zm;
                float sy = (u->z - mapView_.offY()) * zm - f.ay * zm - terrainLift(u->x, u->z) * zm
                           - lift * zm;
                SDL_FRect dst{sx, sy, f.w * zm, f.h * zm};
                SDL_RenderCopyF(ren_, f.tex, nullptr, &dst);
            };
            if (smk) draw(a.smokeFx, a.smokeLift, 12.0f);
            if (fire) draw(a.fireFx, a.fireLift, 18.0f);   // flame over its smoke
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
    std::set<int> corpsed_;
    float animClock_ = 0;
    float trigTimer_ = 0;
};

// A parsed .takrep: enough to rebuild the world and replay it.
struct ReplayFile {
    std::string mapId;
    bool crusades = false;
    uint8_t overridePolicy = 1;   // override tier the recorded game ran under
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
    uint32_t fmt = r.u32();        // format version
    r.u32();                       // protocol version
    out.mapId = r.str();
    uint8_t crusades = r.u8(); uint8_t gods = r.u8(); r.u8();
    if (fmt >= 2) out.overridePolicy = r.u8();   // override tier the game ran under
    out.cfg.unitCap = 0;                          // fmt<3 replays ran without a unit cap
    if (fmt >= 3) out.cfg.unitCap = uint16_t(r.u32());
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
    SDL_SetMainReady();   // we defined SDL_MAIN_HANDLED; tell SDL our main is ready
    if (argc >= 2 && (!std::strcmp(argv[1], "--version") || !std::strcmp(argv[1], "-v"))) {
        std::printf("takview (TAK engine) %s\n", tak::kVersion);
        return 0;
    }
    if (argc >= 2 && (!std::strcmp(argv[1], "--help") || !std::strcmp(argv[1], "-h"))) {
        std::printf(
            "usage: takview [mode] --data <retail-install-dir> [options]\n"
            "  With no mode (or only flags), launches the front-end MENU.\n"
            "  modes: menu | game <map> | map <map> | replay <file.takrep> | model <file.3do>\n"
            "    game single-player: no --server -> auto-hosts a private game vs a server AI.\n"
            "    game multiplayer:   add --server host [--serverport N] [--name X].\n"
            "  common: [--side X --aiside Y] [--overrides none|cosmetic|full] [--shot out.png]\n"
            "  <retail-install-dir> holds the shipped *.hpi plus Maps/ Music/ overrides/.\n");
        return 0;
    }
    // The first positional arg is the launch mode only if it's a known keyword;
    // otherwise the default is the front-end menu, so `takview --data <dir>` (or even
    // bare `takview`) just opens it -- no need to type "menu".
    std::string mode = "menu";
    int argStart = 1;
    if (argc >= 2) {
        std::string a1 = argv[1];
        if (a1 == "menu" || a1 == "game" || a1 == "map" || a1 == "replay" || a1 == "model") {
            mode = a1;
            argStart = 2;
        }
    }
    std::string shot, cobPath, anim, joinAddr, side = "ara", aiSide = "tar";
    std::string serverHost, playerName, dataRoot, overridesArg;
    int serverPort = 7677, mpHeadless = 0;
    int hostPort = 0, joinPort = 0, winW = kWinW, winH = kWinH, maxFps = 60;
    int playerColor = -1, aiColor = -1;   // --color / --aicolor slot overrides
    float startTime = 0, followZoom = 0;
    bool demo = false, trace = false,
         scenario = false, navy = false, amphib = false, missionFlag = false,
         nofog = false, doLook = false,
         keytest = false, selonly = false;
    std::string lodeUnitName;
    bool firetest = false, facetest = false, noVsync = false;
    // Debug/test harness flags (--march, --testbuild, --soundtest, ...): set from
    // argv but read only inside the #ifndef NDEBUG blocks below, so they are unused
    // in release builds.
    [[maybe_unused]] float marchX = 0, marchZ = 0;
    [[maybe_unused]] bool doMarch = false, testbuild = false, misstest = false,
        creon = false, hilltest = false, guardtest = false, lodetest = false,
        soundtest = false;
    bool crusades = false;
    float lookX = 0, lookZ = 0;
    std::vector<std::string> args;
    for (int i = argStart; i < argc; ++i) {
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

        else if (a == "--data" && i + 1 < argc) dataRoot = argv[++i];
        else if (a == "--overrides" && i + 1 < argc) overridesArg = argv[++i];
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

    // The runtime data set: a retail install directory (root *.hpi + Maps/ +
    // Music/ + overrides/). This is the ONLY way the engine reads game files. It
    // must outlive the views (GameView/MapView hold a reference), so it lives here
    // at function scope for the whole render loop, and is built BEFORE connecting
    // so the Hello can carry this install's gameplay-data fingerprint.
    tak::hpi::OverridePolicy pol = tak::hpi::OverridePolicy::Full;
    if (overridesArg == "none") pol = tak::hpi::OverridePolicy::None;
    else if (overridesArg == "cosmetic") pol = tak::hpi::OverridePolicy::Cosmetic;
    tak::hpi::Vfs vfs;
    if (!dataRoot.empty()) vfs = tak::hpi::mountRetailRoot(dataRoot, pol);

    // The engine is client-server only: every real game runs on a server, and AIs
    // run ONLY on the server. Local dev/test harnesses (which free-run the sim with
    // no server) are DEBUG-only. A release build has none of them.
    bool localHarness = false;
#ifndef NDEBUG
    localHarness = demo || scenario || missionFlag || navy || amphib || firetest ||
                   facetest || hilltest || guardtest || lodetest || keytest ||
                   soundtest || misstest || creon || testbuild ||
                   (std::getenv("TAK_FFA") != nullptr);
#endif
    // Create the window + renderer up front so the front-end menu can drive the
    // single-player / multiplayer setup that follows it.
    if (shot.empty()) SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    // Persisted Options (audio/camera/display prefs). CLI flags still win where they
    // apply; the file is the source of truth for anything not passed on the CLI.
    tak::Settings settings = tak::loadSettings();
    if (maxFps != 60) settings.maxFps = maxFps;          // --maxfps (if given) wins the file
    bool vsyncOn = settings.vsync && !noVsync;            // --novsync forces off
    std::string winTitle = std::string("takview ") + tak::kVersion;
    SDL_Window* win = SDL_CreateWindow(winTitle.c_str(), SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, winW, winH,
                                       SDL_WINDOW_RESIZABLE);
    if (win && settings.fullscreen)
        SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
    Uint32 renFlags = SDL_RENDERER_SOFTWARE;
    if (shot.empty()) renFlags = noVsync ? SDL_RENDERER_ACCELERATED
                                         : SDL_RENDERER_PRESENTVSYNC;
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, renFlags);
    if (!ren) {
        std::fprintf(stderr, "renderer failed: %s\n", SDL_GetError());
        return 1;
    }
    if (shot.empty()) SDL_RenderSetVSync(ren, vsyncOn ? 1 : 0);

    // ---- outer session loop: menu -> game -> menu (menu launches only) ----------
    // After a menu-launched session ends (a MAIN MENU button or post-game Escape),
    // loop back to the front-end and let the player pick again. Non-menu launches
    // (direct game/map/replay, headless) run one pass and break. The window/renderer
    // and the menu's vfs outlive each session.
    const bool fromMenu = (mode == "menu");
    const std::string launchMode = mode;
    const std::string launchServerHost = serverHost;
    const int launchServerPort = serverPort;
    const std::vector<std::string> launchArgs = args;
    bool quitApp = false;
    SDL_Texture* aaTex = nullptr;   // whole-frame supersampling target (Options AA); reused
    int aaW = 0, aaH = 0;
    tak::MenuMusic menuMusic;   // persists across menu -> lobby so the track doesn't restart
    menuMusic.setVolume(settings.masterVol, settings.bgmVol);
    for (;;) {
    if (fromMenu) { mode = launchMode; serverHost = launchServerHost;
                    serverPort = launchServerPort; args = launchArgs;
                    menuMusic.start(vfs, 15); }   // front-end BGM (idempotent; loops into the lobby)

    // main-loop lobby driver: 0 = UI-driven lobby (browse/join/host), 7 = auto SP.
    int mpAutoMode = 0;
    bool menuInteractive = false;   // menu single-player -> interactive lobby, not auto-play

    // Front-end: the retail three-door main menu. Its choice drives the setup below
    // (single-player -> local server + lobby; multiplayer -> connect + browser).
    if (mode == "menu") {
        if (dataRoot.empty()) { std::fprintf(stderr, "menu: needs --data <retail-install-dir>\n"); return 1; }
        std::string menuServer;
        tak::MainMenu::Choice choice;
        {
            tak::MainMenu menu(ren, vfs, dataRoot);
            choice = menu.run(shot, &menuServer, &menuMusic, &settings);
        }
        if (!shot.empty()) { SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit(); return 0; }
        if (choice != tak::MainMenu::Choice::SinglePlayer &&
            choice != tak::MainMenu::Choice::Multiplayer) {
            quitApp = true; break;   // exit / campaign / options -> leave the app
        }
        mode = "game";
        if (args.empty()) args.push_back("athri cay");   // TODO: map picker (SP battle menu)
        if (choice == tak::MainMenu::Choice::Multiplayer) {
            std::string sv = menuServer.empty() ? std::string("127.0.0.1") : menuServer;
            auto colon = sv.find(':');   // accept host:port
            if (colon != std::string::npos) {
                int p = std::atoi(sv.substr(colon + 1).c_str());
                if (p > 0) serverPort = p;
                sv = sv.substr(0, colon);
            }
            serverHost = sv.empty() ? std::string("127.0.0.1") : sv;
        } else {
            menuInteractive = true;   // single-player: local server, but stop in the lobby
        }
    }

    if (mode == "game" && serverHost.empty() && !mpHeadless && !localHarness) {
        // Single-player: auto-launch a private local server and play a 1-v-AI game
        // on it (the AI runs server-side). Not visible to other players.
        int p = pickFreePort();
        std::string serverBin =
            (std::filesystem::path(argv[0]).parent_path() / "takserver").string();
        if (p <= 0 || !spawnLocalServer(serverBin, dataRoot, p)) {
            std::fprintf(stderr, "single-player: could not launch a local server (%s)\n",
                         serverBin.c_str());
            return 1;
        }
        static bool atexitOnce = [] { std::atexit(killLocalServer); return true; }();
        (void)atexitOnce;   // register the safety-net teardown once (the loop kills it per session)
        serverHost = "127.0.0.1"; serverPort = p;
        mpAutoMode = menuInteractive ? 0 : 7;   // menu SP stops in the lobby; CLI SP auto-plays
        std::fprintf(stderr, "single-player: local server on port %d%s\n", p,
                     menuInteractive ? " (lobby)" : "");
    }

    // Connect to the multiplayer server, if requested.
    std::unique_ptr<tak::net::MpClient> mp;
    if (!serverHost.empty()) {
        mp = std::make_unique<tak::net::MpClient>();
        if (playerName.empty()) playerName = settings.playerName;
        if (playerName.empty()) playerName = "player";
        // Hello carries the PURE-RETAIL gameplay fingerprint (no overrides), so the
        // base game files are checked regardless of anyone's tier; the room's tier
        // and its gameplay overrides are agreed later, at load.
        if (!dataRoot.empty())
            mp->setDataHash(tak::hpi::gameplayHash(
                tak::hpi::mountRetailRoot(dataRoot, tak::hpi::OverridePolicy::None)));
        // A freshly-spawned local server takes a couple seconds to mount + load, so
        // retry the connect while it comes up.
        bool ok = false;
        for (int attempt = 0; attempt < (gLocalServerUp ? 60 : 1) && !ok; ++attempt) {
            ok = mp->connect(serverHost, uint16_t(serverPort), playerName);
            if (!ok && gLocalServerUp) std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        if (!ok) {
            std::fprintf(stderr, "server: %s\n", mp->error().c_str());
            killLocalServer();
            return 1;
        }
        std::printf("connected to %s:%d as '%s'\n", serverHost.c_str(), serverPort, playerName.c_str());
    }


    std::unique_ptr<MapView> mapView;
    std::unique_ptr<ModelView> modelView;
    std::unique_ptr<GameView> gameView;
    try {
        if (mode == "replay" && !args.empty() && !dataRoot.empty()) {
            // takview replay <file.takrep> --data <retail-root>
            ReplayFile rf;
            if (!loadReplayFile(args[0], rf)) {
                std::fprintf(stderr, "replay: cannot read %s\n", args[0].c_str());
                return 1;
            }
            std::string mapPath = tak::hpi::findMap(vfs, rf.mapId);
            if (mapPath.empty()) { std::fprintf(stderr, "replay: map '%s' not found\n", rf.mapId.c_str()); return 1; }
            // Replay under the tier the game was recorded at (startReplay rebinds the vfs).
            auto rpol = tak::hpi::OverridePolicy(rf.overridePolicy <= 2 ? rf.overridePolicy : 2);
            if (rpol != pol) vfs = tak::hpi::mountRetailRoot(dataRoot, rpol);
            gameView = std::make_unique<GameView>(ren, std::move(vfs), mapPath, dataRoot, rpol,
                                                  false, false, false, /*bare=*/true, "ara", "tar",
                                                  rf.crusades);
            std::fprintf(stderr, "replay: %s -- map '%s', %zu ticks%s\n", args[0].c_str(),
                         rf.mapId.c_str(), rf.bundles.size(), rf.crusades ? " (Crusades)" : "");
            gameView->startReplay(rf.cfg, std::move(rf.bundles));
        } else if (mode == "map" && !args.empty() && !dataRoot.empty()) {
            std::string mapPath = tak::hpi::findMap(vfs, args[0]);
            if (mapPath.empty()) { std::fprintf(stderr, "map '%s' not found\n", args[0].c_str()); return 1; }
            mapView = std::make_unique<MapView>(ren, vfs, mapPath);
        } else if (mode == "game" && !args.empty() && !dataRoot.empty()) {
            std::string mapPath = tak::hpi::findMap(vfs, args[0]);
            if (mapPath.empty()) { std::fprintf(stderr, "map '%s' not found in %s\n", args[0].c_str(), dataRoot.c_str()); return 1; }
            // A multiplayer client builds the world from the server's GameStarting
            // later, so it constructs "bare" (no single-player 2-monarch spawn).
            // When looping back to the menu, keep this function's vfs alive for the
            // next session (+ its findMap); hand the game its own fresh mount.
            gameView = std::make_unique<GameView>(ren,
                                                  fromMenu ? tak::hpi::mountRetailRoot(dataRoot, pol) : std::move(vfs),
                                                  mapPath, dataRoot, pol, demo,
                                                  scenario, missionFlag,
                                                  navy || amphib || firetest || facetest || mp,
                                                  side, aiSide, crusades);
            gameView->applySettings(settings);   // audio / camera / UI-scale prefs
            gameView->setSettings(&settings);     // in-game Options edits + persists these
            if (mp) {
                gameView->setMpClient(mp.get());
                gameView->setMpMapId(args[0]);
                if (fromMenu) { gameView->setExternalLobbyMusic();  // front-end owns the lobby BGM
                                gameView->setCanReturnToMenu(); }   // in-game menu can return to it
                if (menuInteractive) gameView->setSinglePlayer();   // menu SP: SP-flavoured lobby, Create-first
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
            if (trace) gameView->setTrace(true);
            if (nofog) gameView->noFog_ = true;
            if (doLook) gameView->lookAt(lookX, lookZ);
#ifndef NDEBUG
            // Local dev/test harnesses spawn units / issue orders / fast-forward the
            // LOCAL sim -- debug builds only, and never for a server-driven game
            // (localHarness is false whenever a server is involved).
            if (localHarness) {
                if (doMarch) gameView->marchTo(marchX, marchZ);
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
                if (amphib) gameView->amphibDemo();
                if (startTime > 0) gameView->advance(startTime);
            }
#endif
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
        // Jitter benchmark: run the client loop at a FIXED 60 fps (so the stall
        // metric is frame-rate-consistent) and enable the RTT probe. Otherwise the
        // usual tight poll loop.
        bool bench = std::getenv("TAK_NETBENCH") != nullptr;
        if (bench) gameView->netEnableRttProbe();
        while (gameView->mpAutoStep(mpHeadless, mapId, crusades)) {
            if (int(gameView->netTick()) >= limitTicks) break;
            SDL_Delay(bench ? 16 : 2);   // ~60 fps for the benchmark
        }
        std::fprintf(stderr, "mp-headless done: tick=%u hash=%016llx units=%zu err=%s\n",
                     gameView->netTick(), (unsigned long long)gameView->worldHashPublic(),
                     gameView->aliveUnits(),
                     gameView->netError().empty() ? "none" : gameView->netError().c_str());
        if (bench) {
            long f = gameView->netBenchFrames(), s = gameView->netBenchStalls();
            std::fprintf(stderr, "NETBENCH delay=%d rtt=%.0fms frames=%ld stalls=%ld (%.1f%%)\n",
                         gameView->netDelay(), gameView->netRttMs(), f, s,
                         f ? 100.0 * double(s) / double(f) : 0.0);
        }
        mp->disconnect();
        return gameView->netError().empty() ? 0 : 1;
    }

    uint64_t last = SDL_GetPerformanceCounter();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            // The window-manager close button (title-bar X) fires SDL_QUIT; we
            // deliberately IGNORE it so it can't yank the player out of a game. Quit
            // only through real paths: the menu's Exit door, the in-game QUIT button
            // (quitRequested_ below), or Escape in the asset viewers.
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && !gameView) {
                running = false; quitApp = true;   // asset-viewer Esc -> quit the app
            }
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
            // world<->screen math) works in output pixels. Map between them with
            // SDL_RenderWindowToLogical, which uses SDL's INTERNAL window<->drawable
            // mapping -- reliable even on Wayland fractional scaling, where the size
            // getters report window==drawable yet pointer events are in a smaller
            // logical space (SDL_GetWindowSize-based rescaling was a no-op there).
            if (e.type == SDL_MOUSEMOTION) {
                float lx, ly, lx0, ly0;
                SDL_RenderWindowToLogical(ren, e.motion.x, e.motion.y, &lx, &ly);
                SDL_RenderWindowToLogical(ren, e.motion.x - e.motion.xrel,
                                          e.motion.y - e.motion.yrel, &lx0, &ly0);
                e.motion.x = int(lx); e.motion.y = int(ly);
                e.motion.xrel = int(lx - lx0); e.motion.yrel = int(ly - ly0);
            } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                float lx, ly;
                SDL_RenderWindowToLogical(ren, e.button.x, e.button.y, &lx, &ly);
                e.button.x = int(lx); e.button.y = int(ly);
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
                std::snprintf(title, sizeof title, "takview %s  |  %.0f fps",
                              tak::kVersion, float(fpsFrames) / fpsAcc);
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
                    int pick = -1, any = -1;
                    for (auto& u : gameView->worldRef().units()) {
                        if (!u.alive() || u.player != 0 || !u.type) continue;
                        any = u.id;
                        if (u.type->isBuilder) pick = u.id;
                    }
                    if (pick < 0) pick = any;   // fall back to any own unit
                    if (pick >= 0) {
                        gameView->selectOnly(pick);
                        std::fprintf(stderr, "KEYTEST select unit %d (of %zu units)\n",
                                     pick, gameView->worldRef().units().size());
                        // --selonly: hold this selection for the shot (no map clicks,
                        // which would deselect). Otherwise continue the order test.
                        if (keytestSelectOnly) { std::printf("KEYTEST done\n"); ktPhase = -1; }
                        else ktPhase = 1;
                    }
                    // else: units not synced yet -- retry next frame (stay in phase 0)
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
        // Whole-frame supersampling AA (Options): render the game to an oversized
        // target with SDL_RenderSetScale, then downscale it onto the window with
        // linear filtering. The scale only affects OUTPUT pixels -- framing, fixed-px
        // HUD and input all stay in 1x logical space, so nothing else has to change.
        // VRAM-safe: the target is allocated once and reused; if it can't be created
        // (VRAM pressure) we just fall back to no AA this frame. Baking runs at 1x
        // BEFORE the scale is set (the lazy atlas/impostor bakes reset the scale
        // themselves too, see their SetRenderTarget sites).
        float aaS = (settings.antiAlias == 4) ? 2.0f : (settings.antiAlias == 2) ? 1.4142f : 1.0f;
        // Cap the supersample target a safe margin below the GPU's texture/render
        // limit. A render target AT the max texture size misbehaves (renders/samples
        // short, then gets stretched to the window -- squeezing everything leftward,
        // the AA pointer drift). SDL can't report the true render limit and a readback
        // probe proved unreliable, so just stay 1/8 below the reported/assumed max. At
        // the very widest windows this trims 4X's supersample a touch -- imperceptible.
        static int aaMaxDim = 0;
        if (aaMaxDim == 0) {
            SDL_RendererInfo ri;
            int mx = (SDL_GetRendererInfo(ren, &ri) == 0)
                         ? std::min(ri.max_texture_width, ri.max_texture_height) : 0;
            if (mx <= 0 || mx > 16384) mx = 16384;
            aaMaxDim = mx - mx / 8;
            std::fprintf(stderr, "AA: max supersample dim %d (GPU reports %d)\n", aaMaxDim, mx);
        }
        if (aaS > 1.0f && w > 0 && h > 0)
            aaS = std::min(aaS, std::min(float(aaMaxDim) / w, float(aaMaxDim) / h));
        bool aaOn = false;
        if (gameView && aaS > 1.0f && !gameView->inLobbyPhase()) {
            int tw = int(w * aaS), th = int(h * aaS);
            if (!aaTex || aaW != tw || aaH != th) {
                if (aaTex) SDL_DestroyTexture(aaTex);
                aaTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888,
                                          SDL_TEXTUREACCESS_TARGET, tw, th);
                if (aaTex) { SDL_SetTextureScaleMode(aaTex, SDL_ScaleModeLinear); aaW = tw; aaH = th; }
                else { aaW = aaH = 0; std::fprintf(stderr, "AA: %dx%d target alloc failed; AA off\n", tw, th); }
            }
            if (aaTex) aaOn = true;
        }
        // One-line diagnostic whenever the AA level changes, so it's clear on real
        // hardware whether supersampling actually engaged (or fell back on alloc).
        static int aaLoggedLevel = -99;
        if (gameView && !gameView->inLobbyPhase() && settings.antiAlias != aaLoggedLevel) {
            aaLoggedLevel = settings.antiAlias;
            if (settings.antiAlias == 0)
                std::fprintf(stderr, "AA: off\n");
            else if (aaOn)
                std::fprintf(stderr, "AA: %dX active -- %dx%d supersample target\n",
                             settings.antiAlias, int(w * aaS), int(h * aaS));
            else
                std::fprintf(stderr, "AA: %dX requested but INACTIVE (alloc failed?): %s\n",
                             settings.antiAlias, SDL_GetError());
        }
        // Create textures before the render pass (mid-pass creation glitches
        // the whole frame on some backends). Prepare/bake at 1x, then set the scale.
        if (mapView) mapView->ensureChunks(w, h);
        if (gameView) gameView->prepare(w, h);
        if (aaOn) { SDL_SetRenderTarget(ren, aaTex); SDL_RenderSetScale(ren, aaS, aaS); }
        SDL_SetRenderDrawColor(ren, 18, 18, 26, 255);
        SDL_RenderClear(ren);
        // Optional per-phase profiler (TAK_PROF=1): prints where each frame's
        // wall-clock goes, once a second, so a stall can be localised on real
        // hardware that the headless software renderer can't show.
        static const bool prof = getenv("TAK_PROF") != nullptr;
        static double pUpd = 0, pDraw = 0, pPres = 0, pAcc = 0;
        static int pFrames = 0;
        auto pnow = [] { return double(SDL_GetPerformanceCounter()) /
                         double(SDL_GetPerformanceFrequency()) * 1000.0; };
        double t0 = prof ? pnow() : 0;
        if (mapView) mapView->draw(w, h);
        if (modelView) modelView->draw(w, h, dt);
        double t1 = prof ? pnow() : 0;
        if (gameView) {
            // Real-time camera/audio every frame, BEFORE the sim step -- so pan,
            // edge-scroll, follow, shake and music stay smooth even when a net
            // game's sim is stalled waiting on a bundle (the deferred netAccum-style
            // decoupling this comment used to promise).
            gameView->cameraFrame(dt);
            if (gameView->replayMode()) {
                gameView->replayStep(dt);   // play back a recorded .takrep
            } else if (gameView->isNet()) {
                // Server-sequenced lockstep: one mpAutoStep pumps the connection,
                // advances the lobby (auto-matchmaking for now -- a lobby UI is
                // follow-on), and simulates every delivered tick. (void)netAccum.
                (void)netAccum;
                // TAK_MPAUTO=N overrides the lobby driver (0 = UI-driven, the
                // default; 1 = auto-host; 2 = auto-join) -- handy for screenshots.
                static int autoOv = std::getenv("TAK_MPAUTO")
                                        ? std::atoi(std::getenv("TAK_MPAUTO")) : mpAutoMode;
                gameView->mpAutoStep(autoOv, serverMapId, crusades);
            } else {
                gameView->update(dt);
            }
            double t2 = prof ? pnow() : 0;
            gameView->draw(w, h);
            double t3 = prof ? pnow() : 0;
            if (prof) { pUpd += t2 - t1; pDraw += t3 - t2; }
            // Feed the whole real frame time (dt = last frame's total incl. present)
            // to the sprite auto-tuner, so a GPU-bound full-model crowd triggers it.
            gameView->autoTuneSprites(dt * 1000.0f);
        }
        double t4 = prof ? pnow() : 0;
        if (aaOn) {   // downscale the supersampled frame onto the window
            SDL_RenderSetScale(ren, 1.0f, 1.0f);
            SDL_SetRenderTarget(ren, nullptr);
            SDL_SetTextureScaleMode(aaTex, SDL_ScaleModeLinear);   // ensure a smooth downscale
            // Blit to an EXPLICIT full-drawable rect. A nullptr dst resolves to the
            // renderer's logical size, which on some backends (Wayland) is smaller
            // than the real drawable -- squeezing the frame leftward so the cursor
            // drifts. Use the actual output size so it fills the whole window.
            int bw = 0, bh = 0;
            SDL_GetRendererOutputSize(ren, &bw, &bh);
            SDL_Rect dst{0, 0, bw, bh};
            SDL_RenderCopy(ren, aaTex, nullptr, &dst);
        }
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
                pUpd = pDraw = pPres = pAcc = 0; pFrames = 0;
            }
        }

        if (settings.maxFps > 0) {
            static uint64_t prevPresent = 0;
            uint64_t nowp = SDL_GetPerformanceCounter();
            double target = 1.0 / settings.maxFps;   // live via the Options slider
            double elapsed = prevPresent ? double(nowp - prevPresent) /
                                               double(SDL_GetPerformanceFrequency())
                                         : target;
            if (elapsed < target)
                SDL_Delay(uint32_t((target - elapsed) * 1000.0));
            prevPresent = SDL_GetPerformanceCounter();
        }

        // Front-end BGM continues through the lobby (same track, no restart), then
        // stops once the game proper starts so GameView's faction music takes over.
        if (fromMenu) {
            if (gameView && !gameView->inLobbyPhase()) menuMusic.stop();
            else menuMusic.poll();
        }
        // A MAIN MENU button or post-game Escape ends the session; the outer loop
        // then tears it down and re-shows the menu (quitApp stays false).
        if (gameView && gameView->menuRequested()) running = false;
        // The in-game QUIT button exits the whole app.
        if (gameView && gameView->quitRequested()) { running = false; quitApp = true; }

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
    // Session ended: tear down any single-player local server, then either loop back
    // to the menu or exit the app.
    killLocalServer();
    mp.reset();
    if (quitApp || !fromMenu) break;
    }  // ---- end outer session loop ----

    if (aaTex) SDL_DestroyTexture(aaTex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
