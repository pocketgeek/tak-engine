#include "viewer/mainmenu.h"

#include "gaf/gaf.h"
#include "gui/gui.h"
#include "util/png.h"
#include "video/bink.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace tak {

namespace {

// Per-door hover video state: hold the idle clip (`*4`), play hover-in (`*5`) once,
// loop (`*6`) while hovered, then play hover-out (`*7`) back to idle.
enum class DoorState { Idle, In, Loop, Out };

struct Door {
    std::string name;                 // gui gadget id
    SDL_Rect rect{};                  // 640x480 layout space
    std::string vbase;                // video basename: "machine"/"girl"/"knight"
    MainMenu::Choice action = MainMenu::Choice::None;

    SDL_Texture* gaf = nullptr;       // static fallback (GAF state-0 art)
    video::BinkVideo vid;             // current clip
    SDL_Texture* vtex = nullptr;      // streaming frame texture (video size)
    int vw = 0, vh = 0;
    double fps = 30.0, accum = 0.0;
    DoorState state = DoorState::Idle;
    bool videoOk = false;
    bool hover = false;
    std::vector<uint8_t> rgba;
};

struct Button {
    std::string name;
    SDL_Rect rect{};
    SDL_Texture* tex[3] = {nullptr, nullptr, nullptr};   // normal / hover / pressed
    MainMenu::Choice action = MainMenu::Choice::None;
    bool hover = false;
};

}  // namespace

struct MainMenu::Impl {
    SDL_Renderer* ren;
    const hpi::Vfs& vfs;
    std::string install;

    gui::Gui gui;
    SDL_Texture* bg = nullptr;
    std::vector<Door> doors;
    std::vector<Button> buttons;
    std::unordered_map<std::string, std::string> bikByLower;   // lowercased name -> path

    Impl(SDL_Renderer* r, const hpi::Vfs& v, std::string in)
        : ren(r), vfs(v), install(std::move(in)) {}
    ~Impl() {
        if (bg) SDL_DestroyTexture(bg);
        for (auto& d : doors) { if (d.gaf) SDL_DestroyTexture(d.gaf);
                                if (d.vtex) SDL_DestroyTexture(d.vtex); }
        for (auto& b : buttons) for (auto* t : b.tex) if (t) SDL_DestroyTexture(t);
    }

    // ---- asset loading --------------------------------------------------------

    gaf::Palette palette(const std::string& gaf) {
        std::string pp = "anims/" + gaf + ".pcx";
        try { return gaf::Palette::fromBytes(vfs.read(pp), pp); } catch (...) {}
        try { return gaf::Palette::fromBytes(vfs.read("palettes/guipal.pal"),
                                             "palettes/guipal.pal"); } catch (...) {}
        return {};
    }

    // Turn a GAF sequence/frame into an SDL texture (same pipeline as the in-game HUD).
    SDL_Texture* gafTex(const std::string& gafName, const std::string& seq, int frame) {
        if (gafName.empty() || seq.empty()) return nullptr;
        std::string base = gafName;
        if (base.size() >= 4 && base.substr(base.size() - 4) == ".gaf")
            base = base.substr(0, base.size() - 4);
        std::string gp = "anims/" + base + ".gaf";
        try {
            auto pal = palette(base);
            for (auto& sq : gaf::load(vfs.read(gp), pal, -1, gp)) {
                if (sq.name != seq) continue;
                if (frame < 0 || size_t(frame) >= sq.frames.size()) frame = 0;
                if (sq.frames.empty()) return nullptr;
                auto& f = sq.frames[size_t(frame)];
                if (f.width == 0 || f.height == 0) return nullptr;
                SDL_Texture* t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                                   SDL_TEXTUREACCESS_STATIC, f.width, f.height);
                SDL_UpdateTexture(t, nullptr, f.rgba.data(), f.width * 4);
                SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                return t;
            }
        } catch (...) {}
        return nullptr;
    }

    // Index the loose door videos under <install>/Movies/Gui by lowercased name so a
    // "machine5.bik" / "GIRL5.BIK" mix resolves on a case-sensitive filesystem.
    void indexVideos() {
        std::error_code ec;
        fs::path dir = fs::path(install) / "Movies" / "Gui";
        for (auto& e : fs::directory_iterator(dir, ec)) {
            if (!e.is_regular_file()) continue;
            std::string n = e.path().filename().string();
            std::string low = n;
            std::transform(low.begin(), low.end(), low.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            bikByLower[low] = e.path().string();
        }
    }

    std::string findBik(const std::string& base, int n) const {
        auto it = bikByLower.find(base + std::to_string(n) + ".bik");
        return it == bikByLower.end() ? std::string() : it->second;
    }

    // ---- door video state machine --------------------------------------------

    void setDoorTex(Door& d) {
        if (d.vw <= 0 || d.vh <= 0 || d.rgba.empty()) return;
        if (!d.vtex) {
            d.vtex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888,
                                       SDL_TEXTUREACCESS_STREAMING, d.vw, d.vh);
            SDL_SetTextureBlendMode(d.vtex, SDL_BLENDMODE_BLEND);
        }
        SDL_UpdateTexture(d.vtex, nullptr, d.rgba.data(), d.vw * 4);
    }

    // Open clip <base><n>.bik and show its first frame; returns false if unavailable.
    bool startClip(Door& d, int n, DoorState st) {
        std::string path = findBik(d.vbase, n);
        if (path.empty()) return false;
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
        if (!d.vid.open(std::move(bytes))) return false;
        d.vw = d.vid.width(); d.vh = d.vid.height();
        d.fps = d.vid.fps() > 1.0 ? d.vid.fps() : 30.0;
        d.accum = 0.0;
        d.state = st;
        if (d.vid.nextFrame(d.rgba)) { setDoorTex(d); return true; }
        return false;
    }

    void updateDoor(Door& d, double dt) {
        if (!d.videoOk) return;
        if (d.hover && (d.state == DoorState::Idle || d.state == DoorState::Out))
            startClip(d, 5, DoorState::In);
        else if (!d.hover && (d.state == DoorState::In || d.state == DoorState::Loop))
            startClip(d, 7, DoorState::Out);

        if (d.state == DoorState::Idle) return;   // hold the idle frame
        d.accum += dt;
        double spf = 1.0 / std::max(1.0, d.fps);
        int guard = 0;
        while (d.accum >= spf && guard++ < 8) {
            d.accum -= spf;
            if (d.vid.nextFrame(d.rgba)) { setDoorTex(d); continue; }
            // clip ended -> advance the state machine
            if (d.state == DoorState::In) startClip(d, 6, DoorState::Loop);
            else if (d.state == DoorState::Loop) { d.vid.rewind();
                if (d.vid.nextFrame(d.rgba)) setDoorTex(d); }
            else if (d.state == DoorState::Out) { startClip(d, 4, DoorState::Idle); break; }
        }
    }

    // ---- setup ----------------------------------------------------------------

    void load() {
        try { gui = gui::parse(vfs.read("guis/mainmenu.gui"), "guis/mainmenu.gui"); }
        catch (...) {}
        indexVideos();

        // Background: the root gadget's first image (MainScreen.gaf/MainBG).
        if (!gui.gadgets.empty() && !gui.gadgets[0].imgs.empty()) {
            auto& im = gui.gadgets[0].imgs[0];
            bg = gafTex(im.gaf, im.seq, im.frame);
            // The background is the opaque base layer -- ignore any palette-index-0
            // "transparency" in the map art so it doesn't punch through to black.
            if (bg) SDL_SetTextureBlendMode(bg, SDL_BLENDMODE_NONE);
        }

        struct DoorSpec { const char* gadget; const char* vbase; Choice act; };
        const DoorSpec specs[] = {
            {"PlayComputer", "machine", Choice::SinglePlayer},
            {"PlayStory",    "girl",    Choice::Campaign},
            {"PlayPlayer",   "knight",  Choice::Multiplayer},
        };
        for (auto& s : specs) {
            const gui::Gadget* g = gui.find(s.gadget);
            if (!g) continue;
            Door d;
            d.name = s.gadget;
            d.rect = {g->x, g->y, g->w, g->h};
            d.vbase = s.vbase;
            d.action = s.act;
            if (!g->imgs.empty()) d.gaf = gafTex(g->imgs[0].gaf, g->imgs[0].seq, g->imgs[0].frame);
            doors.push_back(std::move(d));
        }
        // Open each door's idle clip so it rests on the animated idle frame.
        for (auto& d : doors) d.videoOk = video::BinkVideo::available() && startClip(d, 4, DoorState::Idle);

        struct BtnSpec { const char* gadget; Choice act; };
        const BtnSpec btns[] = {{"Options", Choice::Options}, {"Exit", Choice::Exit}};
        for (auto& b : btns) {
            const gui::Gadget* g = gui.find(b.gadget);
            if (!g) continue;
            Button bt;
            bt.name = b.gadget;
            bt.rect = {g->x, g->y, g->w, g->h};
            bt.action = b.act;
            for (int i = 0; i < 3 && i < int(g->imgs.size()); ++i)
                bt.tex[i] = gafTex(g->imgs[size_t(i)].gaf, g->imgs[size_t(i)].seq, g->imgs[size_t(i)].frame);
            buttons.push_back(std::move(bt));
        }
    }

    // ---- render ---------------------------------------------------------------

    // Uniform 640x480 -> window scale with letterbox centering.
    void layout(int winW, int winH, float& scale, float& offX, float& offY) const {
        scale = std::min(winW / 640.0f, winH / 480.0f);
        offX = (winW - 640.0f * scale) * 0.5f;
        offY = (winH - 480.0f * scale) * 0.5f;
    }
    SDL_FRect toScreen(const SDL_Rect& r, float s, float ox, float oy) const {
        return {ox + r.x * s, oy + r.y * s, r.w * s, r.h * s};
    }

    void render(int winW, int winH) {
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        float s, ox, oy; layout(winW, winH, s, ox, oy);
        if (bg) { SDL_FRect r{ox, oy, 640 * s, 480 * s}; SDL_RenderCopyF(ren, bg, nullptr, &r); }
        for (auto& d : doors) {
            SDL_Texture* t = (d.videoOk && d.vtex) ? d.vtex : d.gaf;
            if (t) { SDL_FRect r = toScreen(d.rect, s, ox, oy); SDL_RenderCopyF(ren, t, nullptr, &r); }
        }
        for (auto& b : buttons) {
            SDL_Texture* t = b.hover && b.tex[1] ? b.tex[1] : b.tex[0];
            if (!t) continue;
            // The button art is bigger than its gui hotspot rect and is authored to
            // exactly cover MainBG's button-footprint box; draw it at native size
            // from the gadget origin, not stretched to the (smaller) hotspot rect.
            int tw = 0, th = 0; SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
            SDL_Rect nat{b.rect.x, b.rect.y, tw, th};
            SDL_FRect r = toScreen(nat, s, ox, oy);
            SDL_RenderCopyF(ren, t, nullptr, &r);
        }
        SDL_RenderPresent(ren);
    }

    void screenshot(int winW, int winH, const std::string& path) {
        std::vector<uint8_t> px(size_t(winW) * size_t(winH) * 4);
        if (SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ABGR8888, px.data(), winW * 4) == 0)
            png::write(path, winW, winH, px);
    }

    // ---- input ----------------------------------------------------------------

    void updateHover(int mx, int my, int winW, int winH) {
        float s, ox, oy; layout(winW, winH, s, ox, oy);
        float gx = (mx - ox) / s, gy = (my - oy) / s;   // to 640x480 space
        auto in = [&](const SDL_Rect& r) {
            return gx >= r.x && gx < r.x + r.w && gy >= r.y && gy < r.y + r.h;
        };
        for (auto& d : doors) d.hover = in(d.rect);
        for (auto& b : buttons) b.hover = in(b.rect);
    }

    Choice clicked() const {
        for (auto& d : doors) if (d.hover) return d.action;
        for (auto& b : buttons) if (b.hover) return b.action;
        return Choice::None;
    }
};

MainMenu::MainMenu(SDL_Renderer* ren, const hpi::Vfs& vfs, std::string install)
    : d_(new Impl(ren, vfs, std::move(install))) { d_->load(); }
MainMenu::~MainMenu() { delete d_; }

MainMenu::Choice MainMenu::run(const std::string& shotPath) {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(d_->ren, &w, &h);

    if (!shotPath.empty()) {
        for (auto& dr : d_->doors) d_->updateDoor(dr, 0.0);
        d_->render(w, h);
        d_->screenshot(w, h, shotPath);
        return Choice::None;
    }

    Uint64 prev = SDL_GetPerformanceCounter();
    const double freq = double(SDL_GetPerformanceFrequency());
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return Choice::Exit;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) return Choice::Exit;
            if (e.type == SDL_MOUSEMOTION)
                d_->updateHover(e.motion.x, e.motion.y, w, h);
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                d_->updateHover(e.button.x, e.button.y, w, h);
                Choice c = d_->clicked();
                if (c != Choice::None && c != Choice::Campaign) return c;   // campaign: no-op for now
            }
        }
        SDL_GetRendererOutputSize(d_->ren, &w, &h);
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = double(now - prev) / freq;
        prev = now;
        for (auto& dr : d_->doors) d_->updateDoor(dr, dt);
        d_->render(w, h);
        SDL_Delay(1);
    }
}

}  // namespace tak
