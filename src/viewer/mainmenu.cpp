#include "viewer/mainmenu.h"

#include "gaf/gaf.h"
#include "gui/gui.h"
#include "util/png.h"
#include "version.h"
#include "video/bink.h"
#include "viewer/menumusic.h"
#include "viewer/options.h"
#include "viewer/settings.h"

#include <memory>

#include <algorithm>
#include <array>
#include <cstdlib>
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
    std::string sound;                // click sound (gui states, e.g. "skirmish.wav")
    std::string tip;                  // help caption (gui cmd, e.g. "Play the Machine")
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
    std::string sound;                                   // click sound (gui states)
    std::string tip;                                     // help caption (gui cmd)
    MainMenu::Choice action = MainMenu::Choice::None;
    bool hover = false;
};

// A menu gadget's click sound is the first ".wav" entry in its gui state strings
// (the others are "Default" or empty). Case-insensitive on the extension.
inline std::string clickSound(const gui::Gadget& g) {
    for (const auto& s : g.states) {
        if (s.size() < 4) continue;
        std::string ext = s.substr(s.size() - 4);
        for (char& c : ext) c = char(std::tolower((unsigned char)c));
        if (ext == ".wav") return s;
    }
    return {};
}

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

    // Click SFX: one queue-driven device (all menu click WAVs share a format,
    // u8/11025/mono), and each sound's volume-scaled PCM keyed by filename. This is
    // separate from the shared MenuMusic device so a click mixes over the music.
    SDL_AudioDeviceID sfxDev_ = 0;
    std::unordered_map<std::string, std::vector<uint8_t>> sfxPcm_;

    // The Options overlay, opened from the Options button (see run()).
    std::unique_ptr<OptionsScreen> options_;

    Impl(SDL_Renderer* r, const hpi::Vfs& v, std::string in)
        : ren(r), vfs(v), install(std::move(in)) {}
    ~Impl() {
        if (bg) SDL_DestroyTexture(bg);
        for (auto& d : doors) { if (d.gaf) SDL_DestroyTexture(d.gaf);
                                if (d.vtex) SDL_DestroyTexture(d.vtex); }
        for (auto& b : buttons) for (auto* t : b.tex) if (t) SDL_DestroyTexture(t);
        if (sfxDev_) SDL_CloseAudioDevice(sfxDev_);
    }

    // ---- click SFX ------------------------------------------------------------

    // Load one click WAV from sounds/ and cache its PCM. Opens the shared SFX
    // device from the first WAV (all four share u8/11025/mono, so no conversion).
    void loadSfx(const std::string& name) {
        if (name.empty() || sfxPcm_.count(name)) return;
        std::vector<uint8_t> raw;
        try { raw = vfs.read("sounds/" + name); } catch (...) { return; }
        if (raw.empty()) return;
        SDL_RWops* rw = SDL_RWFromConstMem(raw.data(), int(raw.size()));
        SDL_AudioSpec spec{}; Uint8* buf = nullptr; Uint32 len = 0;
        if (!rw || !SDL_LoadWAV_RW(rw, 1, &spec, &buf, &len)) return;
        if (!sfxDev_) {
            SDL_InitSubSystem(SDL_INIT_AUDIO);
            SDL_AudioSpec want = spec, have{};
            want.callback = nullptr;   // queue-driven
            // Small buffer so a queued click plays with minimal latency. The whole
            // click is queued at once (never a partial fill), so a short device
            // period can't underrun it. ~128/11025 ~= 12ms vs ~46ms at 512.
            want.samples = 128;
            sfxDev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
            if (sfxDev_) SDL_PauseAudioDevice(sfxDev_, 0);
        }
        // Scale to a UI level that sits above the (soft, ~45/128) menu music.
        // These WAVs are unsigned 8-bit (silence == 128), so scale around 128.
        std::vector<uint8_t> pcm(len);
        for (Uint32 i = 0; i < len; ++i) {
            int s = 128 + (int(buf[i]) - 128) * 100 / 128;
            pcm[i] = uint8_t(s < 0 ? 0 : s > 255 ? 255 : s);
        }
        SDL_FreeWAV(buf);
        sfxPcm_[name] = std::move(pcm);
    }

    void playSfx(const std::string& name) {
        if (!sfxDev_ || name.empty()) return;
        auto it = sfxPcm_.find(name);
        if (it == sfxPcm_.end()) return;
        SDL_ClearQueuedAudio(sfxDev_);   // retrigger cleanly on rapid clicks
        SDL_QueueAudio(sfxDev_, it->second.data(), Uint32(it->second.size()));
    }

    // Play the click sound of whatever door/button is currently hovered.
    void playHoveredSound() {
        for (auto& d : doors) if (d.hover) { playSfx(d.sound); return; }
        for (auto& b : buttons) if (b.hover) { playSfx(b.sound); return; }
    }

    // A click that transitions away destroys this MainMenu (closing the SFX
    // device), so let the short click finish first -- keep rendering so the menu
    // doesn't freeze. Bounded well above the longest click (~140ms).
    void flushSfx(int winW, int winH) {
        if (!sfxDev_) return;
        for (int i = 0; i < 30 && SDL_GetQueuedAudioSize(sfxDev_) > 0; ++i) {
            render(winW, winH);
            SDL_RenderPresent(ren);
            SDL_Delay(10);
        }
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
            // Bink frames are opaque, but the decoder's alpha can come out < 255;
            // BLENDMODE_BLEND then blends the clip DARKER over the background (a visible
            // colour shift where the snort video overlays the static "K"). Ignore the
            // alpha (opaque) so the clip renders at its true decoded colour. Linear
            // filtering smooths these low-res clips (~124px) when scaled to the window.
            SDL_SetTextureBlendMode(d.vtex, SDL_BLENDMODE_NONE);
            SDL_SetTextureScaleMode(d.vtex, SDL_ScaleModeLinear);
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
            if (bg) {
                SDL_SetTextureBlendMode(bg, SDL_BLENDMODE_NONE);
                // Match the background to the Bink clips it sits behind: they render a
                // touch darker (mostly green), so the palette-bright bg would otherwise
                // seam against the video (the dragon in the "K"). Measured video/bg
                // per-channel scale, pixel-aligned.
                SDL_SetTextureColorMod(bg, 249, 243, 248);
            }
        }

        struct DoorSpec { const char* gadget; const char* vbase; Choice act; };
        const DoorSpec specs[] = {
            {"PlayComputer", "machine", Choice::SinglePlayer},
            {"PlayStory",    "girl",    Choice::Campaign},
            {"PlayPlayer",   "knight",  Choice::Multiplayer},
            // The dragon in the title's "K": hover snorts (snort4-7), no click action.
            {"Credits",      "snort",   Choice::None},
        };
        for (auto& s : specs) {
            const gui::Gadget* g = gui.find(s.gadget);
            if (!g) continue;
            Door d;
            d.name = s.gadget;
            d.rect = {g->x, g->y, g->w, g->h};
            d.vbase = s.vbase;
            d.action = s.act;
            d.sound = s.act == Choice::None ? "" : clickSound(*g);   // hover-only: no click sound
            d.tip = g->cmd;   // gui cmd doubles as the hover help caption
            loadSfx(d.sound);
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
            bt.sound = clickSound(*g);
            bt.tip = g->cmd;   // gui cmd doubles as the hover help caption
            if (b.act == Choice::Exit) bt.tip = "Exit";   // retail's cmd is "Exit to Windows"
            loadSfx(bt.sound);
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
            // The dragon (Choice::None) is part of the static background art, so when
            // it's idle just let the background show through (a perfect match); overlay
            // its snort video only while it's actually snorting (hover). The clip itself
            // is colour-matched to the bg (see setDoorTex) so the transition is seamless
            // and the fire hides any residual. Doors always play (they fill a bg hole).
            bool idleDragon = d.action == MainMenu::Choice::None && d.state == DoorState::Idle;
            if (d.videoOk && d.vtex && !idleDragon) {
                // Like the buttons, the door video is authored bigger than its gui
                // hotspot and anchored at the gadget origin -- draw it at native size,
                // NOT stretched to the (smaller) hotspot. Stretching squished it badly:
                // knight is 221x250 vs a 161-wide hotspot, and its 221px width is
                // authored to run from x=419 to the 640px screen edge.
                SDL_Rect nat{d.rect.x, d.rect.y, d.vw, d.vh};
                SDL_FRect r = toScreen(nat, s, ox, oy);
                SDL_RenderCopyF(ren, d.vtex, nullptr, &r);
            } else if (d.gaf && !idleDragon) {
                SDL_FRect r = toScreen(d.rect, s, ox, oy);
                SDL_RenderCopyF(ren, d.gaf, nullptr, &r);
            }
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

        // Bottom-centre placards, in the retail gui's 640x480 coordinates:
        // the "Version" static (174,408,295,14) shows the build version, and its
        // "HelpText" sibling (172,441,296,31) shows the hovered gadget's caption --
        // a fixed help/status line, the retail convention, not a floating tooltip.
        // (blockText covers A-Z/0-9/.:- ; spaces render as a gap.)
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        auto placard = [&](const std::string& text, int dx, int dw, int dy, float dpx, SDL_Color c) {
            float tw = float(text.size()) * 6 * dpx;                        // design-space width
            shadowText(text, ox + (dx + (dw - tw) / 2) * s, oy + dy * s, dpx * s, c);
        };
        placard(std::string("VERSION ") + tak::kVersion, 174, 295, 409, 2.0f, {185, 180, 160, 220});
        const std::string* tip = nullptr;
        for (auto& d : doors) if (d.hover && !d.tip.empty()) { tip = &d.tip; break; }
        if (!tip) for (auto& b : buttons) if (b.hover && !b.tip.empty()) { tip = &b.tip; break; }
        if (tip) placard(*tip, 172, 296, 448, 2.5f, {240, 238, 245, 235});
    }

    // ---- minimal block font + multiplayer server-select overlay ---------------
    bool serverSelect = false;
    std::string serverText = "127.0.0.1";

    static const uint8_t* glyph5x7(char c) {
        static const std::unordered_map<char, std::array<uint8_t, 5>> F = {
            {'0',{0x3E,0x51,0x49,0x45,0x3E}},{'1',{0x00,0x42,0x7F,0x40,0x00}},
            {'2',{0x42,0x61,0x51,0x49,0x46}},{'3',{0x21,0x41,0x45,0x4B,0x31}},
            {'4',{0x18,0x14,0x12,0x7F,0x10}},{'5',{0x27,0x45,0x45,0x45,0x39}},
            {'6',{0x3C,0x4A,0x49,0x49,0x30}},{'7',{0x01,0x71,0x09,0x05,0x03}},
            {'8',{0x36,0x49,0x49,0x49,0x36}},{'9',{0x06,0x49,0x49,0x29,0x1E}},
            {'A',{0x7E,0x11,0x11,0x11,0x7E}},{'B',{0x7F,0x49,0x49,0x49,0x36}},
            {'C',{0x3E,0x41,0x41,0x41,0x22}},{'D',{0x7F,0x41,0x41,0x22,0x1C}},
            {'E',{0x7F,0x49,0x49,0x49,0x41}},{'F',{0x7F,0x09,0x09,0x09,0x01}},
            {'G',{0x3E,0x41,0x49,0x49,0x7A}},{'H',{0x7F,0x08,0x08,0x08,0x7F}},
            {'I',{0x00,0x41,0x7F,0x41,0x00}},{'J',{0x20,0x40,0x41,0x3F,0x01}},
            {'K',{0x7F,0x08,0x14,0x22,0x41}},{'L',{0x7F,0x40,0x40,0x40,0x40}},
            {'M',{0x7F,0x02,0x0C,0x02,0x7F}},{'N',{0x7F,0x04,0x08,0x10,0x7F}},
            {'O',{0x3E,0x41,0x41,0x41,0x3E}},{'P',{0x7F,0x09,0x09,0x09,0x06}},
            {'Q',{0x3E,0x41,0x51,0x21,0x5E}},{'R',{0x7F,0x09,0x19,0x29,0x46}},
            {'S',{0x46,0x49,0x49,0x49,0x31}},{'T',{0x01,0x01,0x7F,0x01,0x01}},
            {'U',{0x3F,0x40,0x40,0x40,0x3F}},{'V',{0x1F,0x20,0x40,0x20,0x1F}},
            {'W',{0x7F,0x20,0x18,0x20,0x7F}},{'X',{0x63,0x14,0x08,0x14,0x63}},
            {'Y',{0x07,0x08,0x70,0x08,0x07}},{'Z',{0x61,0x51,0x49,0x45,0x43}},
            {'.',{0x00,0x60,0x60,0x00,0x00}},{':',{0x00,0x36,0x36,0x00,0x00}},
            {'-',{0x08,0x08,0x08,0x08,0x08}},
        };
        auto it = F.find(c);
        return it == F.end() ? nullptr : it->second.data();
    }
    void blockText(const std::string& str, float x, float y, float px, SDL_Color c) {
        SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
        float cx = x;
        for (char ch : str) {
            const uint8_t* cols = glyph5x7(char(std::toupper((unsigned char)ch)));
            if (!cols) { cx += 4 * px; continue; }
            for (int col = 0; col < 5; ++col)
                for (int row = 0; row < 7; ++row)
                    if (cols[col] & (1 << row)) {
                        SDL_FRect r{cx + col * px, y + row * px, px, px};
                        SDL_RenderFillRectF(ren, &r);
                    }
            cx += 6 * px;
        }
    }
    // Block text with a 1px dark drop-shadow so it stays legible over any menu art.
    void shadowText(const std::string& s, float x, float y, float px, SDL_Color c) {
        float o = std::max(1.0f, px * 0.4f);
        blockText(s, x + o, y + o, px, {0, 0, 0, 170});
        blockText(s, x, y, px, c);
    }

    void renderServerSelect(int winW, int winH) {
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 160);
        SDL_FRect dim{0, 0, float(winW), float(winH)}; SDL_RenderFillRectF(ren, &dim);
        float pw = 560, ph = 210, x0 = (winW - pw) / 2, y0 = (winH - ph) / 2;
        SDL_SetRenderDrawColor(ren, 28, 30, 40, 245);
        SDL_FRect panel{x0, y0, pw, ph}; SDL_RenderFillRectF(ren, &panel);
        SDL_SetRenderDrawColor(ren, 150, 150, 175, 255); SDL_RenderDrawRectF(ren, &panel);
        blockText("CONNECT TO SERVER", x0 + 30, y0 + 24, 3.0f, {210, 205, 160, 255});
        SDL_SetRenderDrawColor(ren, 16, 18, 26, 255);
        SDL_FRect box{x0 + 30, y0 + 78, pw - 60, 46}; SDL_RenderFillRectF(ren, &box);
        SDL_SetRenderDrawColor(ren, 120, 140, 180, 255); SDL_RenderDrawRectF(ren, &box);
        blockText(serverText, box.x + 12, box.y + 14, 3.0f, {230, 235, 245, 255});
        // blinking caret
        if ((SDL_GetTicks() / 500) % 2 == 0) {
            float cx = box.x + 12 + float(serverText.size()) * 6 * 3.0f;
            SDL_SetRenderDrawColor(ren, 230, 235, 245, 255);
            SDL_FRect car{cx, box.y + 12, 3, 22}; SDL_RenderFillRectF(ren, &car);
        }
        blockText("ENTER - CONNECT     ESC - BACK", x0 + 30, y0 + 156, 2.0f, {150, 155, 175, 255});
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

MainMenu::Choice MainMenu::run(const std::string& shotPath, std::string* serverOut,
                               MenuMusic* music, Settings* settings) {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(d_->ren, &w, &h);

    if (!shotPath.empty()) {
        for (auto& dr : d_->doors) d_->updateDoor(dr, 0.0);
        d_->render(w, h);
        d_->screenshot(w, h, shotPath);
        return Choice::None;
    }

    // Drop any mouse events queued by the previous screen. Returning here from the
    // lobby's MAIN MENU button (which fires on mouse-DOWN) leaves its matching
    // mouse-UP in the queue; without this it would land as a phantom click on a
    // door and snap straight back into the game.
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP);

    Uint64 prev = SDL_GetPerformanceCounter();
    const double freq = double(SDL_GetPerformanceFrequency());
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) continue;   // ignore the WM close button; use the Exit door

            if (d_->serverSelect) {   // multiplayer: typing a server address
                if (e.type == SDL_TEXTINPUT) {
                    for (const char* p = e.text.text; *p; ++p) {
                        unsigned char ch = (unsigned char)*p;
                        if (d_->serverText.size() < 64 &&
                            (std::isalnum(ch) || ch == '.' || ch == ':' || ch == '-'))
                            d_->serverText += char(ch);
                    }
                } else if (e.type == SDL_KEYDOWN) {
                    SDL_Keycode k = e.key.keysym.sym;
                    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                        SDL_StopTextInput();
                        if (serverOut) *serverOut = d_->serverText;
                        return Choice::Multiplayer;
                    }
                    if (k == SDLK_ESCAPE) { d_->serverSelect = false; SDL_StopTextInput(); }
                    if (k == SDLK_BACKSPACE && !d_->serverText.empty()) d_->serverText.pop_back();
                }
                continue;   // swallow everything else while typing
            }

            if (d_->options_) {   // Options overlay is up: route everything to it
                if (d_->options_->input(e, w, h)) d_->options_.reset();   // BACK / Esc (SAVE is explicit)
                continue;
            }

            // Esc does nothing at the title screen -- quitting is only via the Exit
            // door (the WM close button is ignored too). Esc never exits the game.
            if (e.type == SDL_MOUSEMOTION)
                d_->updateHover(e.motion.x, e.motion.y, w, h);
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                // Doors/buttons act on the PRESS: the sound and the action both fire
                // here, so the menu feels immediate.
                d_->updateHover(e.button.x, e.button.y, w, h);
                d_->playHoveredSound();
                Choice c = d_->clicked();
                if (c != Choice::None) {
                    // We acted on the press -- drop this click's matching release so
                    // it can't land as a phantom click on the overlay/lobby/game we're
                    // about to switch to (the mirror of the entry flush above).
                    SDL_PumpEvents();
                    SDL_FlushEvents(SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP);
                }
                if (c == Choice::Multiplayer) { d_->serverSelect = true; SDL_StartTextInput(); }
                else if (c == Choice::Options && settings) {
                    // Open the Options overlay in place (rather than exiting). onChange
                    // applies audio + window live; SAVE persists (BACK does not).
                    SDL_Renderer* ren = d_->ren;
                    d_->options_ = std::make_unique<OptionsScreen>(ren, *settings,
                        [ren, music, settings] {
                            if (music) music->setVolume(settings->masterVol, settings->bgmVol);
                            if (SDL_Window* wnd = SDL_RenderGetWindow(ren))
                                SDL_SetWindowFullscreen(wnd, settings->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                            SDL_RenderSetVSync(ren, settings->vsync ? 1 : 0);
                        },
                        [settings] { saveSettings(*settings); });
                }
                else if (c != Choice::None && c != Choice::Campaign) {
                    d_->flushSfx(w, h);   // let the click sound finish before we tear down
                    return c;
                }
            }
        }
        SDL_GetRendererOutputSize(d_->ren, &w, &h);
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = double(now - prev) / freq;
        prev = now;
        for (auto& dr : d_->doors) d_->updateDoor(dr, dt);
        if (music) music->poll();
        d_->render(w, h);
        if (d_->serverSelect) d_->renderServerSelect(w, h);
        if (d_->options_) d_->options_->render(w, h);
        SDL_RenderPresent(d_->ren);
        SDL_Delay(1);
    }
}

}  // namespace tak
