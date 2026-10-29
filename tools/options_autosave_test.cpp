// options_autosave_test -- Options, Voice Chat and Controls save every change as it is
// made; there is no SAVE button. Driven with real SDL events on a software renderer,
// with the hosts' save callback (saveSettings) writing a private settings file
// (setSettingsPath), and the file read back after each step:
//   - a toggle, a dropdown pick and a rebound key are in the file at once;
//   - a slider writes once, when it is let go -- not per drag step;
//   - the player name writes when its field loses focus (Enter, or the screen closing),
//     not per keystroke;
//   - DEFAULTS asks first (RESET TO DEFAULTS? YES / NO): NO changes nothing, YES resets
//     and saves -- in Options and in Controls.

#include "client/hotkeys.h"
#include "client/hotkeysscreen.h"
#include "client/options.h"
#include "client/settings.h"

#include <SDL.h>

#include <cstdio>
#include <filesystem>
#include <random>
#include <string>

namespace fs = std::filesystem;
using namespace tak;

static int g_fail = 0, g_checks = 0;
static void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

static constexpr int W = 1280, H = 720;

template <typename Screen>
static bool send(Screen& s, Uint32 type, float x, float y, Uint32 state = 0) {
    SDL_Event e{};
    e.type = type;
    if (type == SDL_MOUSEMOTION) { e.motion.x = int(x); e.motion.y = int(y); e.motion.state = state; }
    else { e.button.button = SDL_BUTTON_LEFT; e.button.x = int(x); e.button.y = int(y); }
    const bool leave = s.input(e, W, H);
    s.render(W, H);
    return leave;
}
template <typename Screen>
static bool click(Screen& s, const SDL_FRect& r) {
    const float x = r.x + r.w / 2, y = r.y + r.h / 2;
    const bool leave = send(s, SDL_MOUSEBUTTONDOWN, x, y);
    send(s, SDL_MOUSEBUTTONUP, x, y);
    return leave;
}
template <typename Screen>
static bool key(Screen& s, SDL_Keycode k) {
    SDL_Event e{}; e.type = SDL_KEYDOWN; e.key.keysym.sym = k;
    const bool leave = s.input(e, W, H);
    s.render(W, H);
    return leave;
}

// Scroll Options until the row is in the middle of the panel; nullptr if there is none.
static const SDL_FRect* bring(OptionsScreen& o, const char* label) {
    o.render(W, H);
    const SDL_FRect* r = o.rowRect(label);
    for (int i = 0; i < 80 && r && (r->y + r->h / 2 > H * 0.6f || r->y < H * 0.25f); ++i) {
        SDL_Event e{}; e.type = SDL_MOUSEWHEEL; e.wheel.y = r->y < H * 0.25f ? 1 : -1;
        o.input(e, W, H); o.render(W, H);
        r = o.rowRect(label);
    }
    check(r != nullptr, std::string("Options has a ") + label + " row");
    return r;
}

static void testOptions(SDL_Renderer* ren) {
    std::printf("Options\n");
    Settings s = loadSettings();
    int saves = 0, changes = 0;
    auto save = [&] { ++saves; check(saveSettings(s), "the host's save writes the file"); };
    {
        OptionsScreen o(ren, s, [&] { ++changes; }, save, 2);
        o.render(W, H);

        // A toggle: in the file at once.
        const bool shadows = s.unitShadows;
        if (const SDL_FRect* r = bring(o, "SHADOWS")) click(o, *r);
        check(s.unitShadows != shadows && changes == 1, "SHADOWS toggles live");
        check(saves == 1 && loadSettings().unitShadows == s.unitShadows, "a toggle is saved at once, no SAVE");

        // A slider: live on every drag step, saved once when let go.
        if (const SDL_FRect* r = bring(o, "MASTER VOLUME")) {
            const int before = loadSettings().masterVol;
            const float y = r->y + r->h / 2;
            send(o, SDL_MOUSEBUTTONDOWN, r->x + r->w * 0.20f, y);
            send(o, SDL_MOUSEMOTION, r->x + r->w * 0.35f, y, SDL_BUTTON_LMASK);
            send(o, SDL_MOUSEMOTION, r->x + r->w * 0.50f, y, SDL_BUTTON_LMASK);
            check(s.masterVol >= 124 && s.masterVol <= 132 && changes >= 4, "MASTER VOLUME follows the drag live");
            check(saves == 1 && loadSettings().masterVol == before, "a slider is not saved while it is held");
            send(o, SDL_MOUSEBUTTONUP, r->x + r->w * 0.50f, y);
            check(saves == 2 && loadSettings().masterVol == s.masterVol, "a slider is saved once, when let go");
            send(o, SDL_MOUSEMOTION, r->x + r->w * 0.70f, y, 0);   // no button: not a drag
            check(saves == 2 && s.masterVol <= 132, "moving the mouse after the release changes nothing");
        }

        // A dropdown pick: saved at once.
        if (const SDL_FRect* r = bring(o, "GPU MEMORY LIMIT")) {
            click(o, SDL_FRect{r->x + r->w - 20, r->y, 10, r->h});
            const SDL_FRect item = o.dropItemRect(0);
            check(item.w > 0, "the GPU MEMORY LIMIT list opens");
            send(o, SDL_MOUSEBUTTONDOWN, item.x + item.w / 2, item.y + item.h / 2);
            check(s.gpuMemoryLimit == kGpuMemoryLimits[0], "the pick applies");
            check(saves == 3 && loadSettings().gpuMemoryLimit == kGpuMemoryLimits[0], "a dropdown pick is saved at once");
        }

        // The player name: typed live, saved when its field loses focus.
        if (const SDL_FRect* r = bring(o, "PLAYER NAME")) {
            click(o, *r);
            check(o.editingText(), "clicking the name field focuses it");
            SDL_Event t{}; t.type = SDL_TEXTINPUT; SDL_strlcpy(t.text.text, "Aldric", sizeof t.text.text);
            o.input(t, W, H);
            check(s.playerName == "Aldric", "the name is typed");
            check(saves == 3 && loadSettings().playerName != "Aldric", "the name is not saved per keystroke");
            key(o, SDLK_RETURN);
            check(!o.editingText() && saves == 4 && loadSettings().playerName == "Aldric", "Enter saves the name");
            click(o, *r);
            SDL_Event t2{}; t2.type = SDL_TEXTINPUT; SDL_strlcpy(t2.text.text, "Brand", sizeof t2.text.text);
            o.input(t2, W, H);
            check(saves == 4 && loadSettings().playerName == "Aldric", "the second name waits for the focus to go");
        }
    }   // the screen closes with the name still focused
    check(saves == 5 && loadSettings().playerName == "Brand", "closing the screen saves the name being typed");

    // DEFAULTS asks first.
    saves = 0;
    {
        OptionsScreen o(ren, s, [&] { ++changes; }, save, 2);
        o.render(W, H);
        const Settings before = s;
        check(!(s == preferenceDefaults(s)), "the profile is off its defaults");
        click(o, o.defaultsRect());
        check(s == before && saves == 0, "DEFAULTS alone changes nothing");
        const SDL_FRect no = o.defaultsNoRect(), yes = o.defaultsYesRect();
        check(no.w > 0 && yes.w > 0, "DEFAULTS asks RESET TO DEFAULTS? YES / NO");
        check(!click(o, no) && s == before && saves == 0 && o.defaultsYesRect().w == 0, "NO keeps everything");
        click(o, o.defaultsRect());
        key(o, SDLK_ESCAPE);
        check(s == before && saves == 0 && o.defaultsYesRect().w == 0, "Esc answers NO, and stays in Options");
        click(o, o.defaultsRect());
        click(o, o.defaultsYesRect());
        check(s == preferenceDefaults(before) && s.playerName == "Brand", "YES resets, keeping the player name");
        check(saves == 1 && loadSettings() == s, "YES saves the defaults");
        check(click(o, o.defaultsRect()) == false && o.defaultsYesRect().w == 0, "at defaults, DEFAULTS does nothing");
    }
}

static void testVoice(SDL_Renderer* ren) {
    std::printf("VOICE CHAT\n");
    Settings s = loadSettings();
    int saves = 0;
    OptionsScreen o(ren, s, [] {}, [&] { ++saves; check(saveSettings(s), "the host's save writes the file"); },
                    VoicePageHooks{});
    o.render(W, H);
    if (const SDL_FRect* r = o.rowRect("VOICE CHAT")) click(o, *r);
    check(s.voiceEnabled && saves == 1 && loadSettings().voiceEnabled, "the VOICE CHAT switch is saved at once");
    click(o, o.defaultsRect());
    check(s.voiceEnabled && saves == 1, "the page's DEFAULTS asks first");
    click(o, o.defaultsYesRect());
    check(!s.voiceEnabled && saves == 2 && !loadSettings().voiceEnabled, "YES resets the voice settings and saves");
}

static void testControls(SDL_Renderer* ren) {
    std::printf("CONTROLS\n");
    Settings s = loadSettings();
    s.hotkeys.clear();
    check(saveSettings(s), "start from the default keys");
    int saves = 0, changes = 0;
    HotkeysScreen hk(ren, s, [&] { ++changes; }, [&] { ++saves; check(saveSettings(s), "the host's save writes the file"); });
    hk.render(W, H);
    const Act act = hotkeyDefs().front().act;
    const SDL_FRect row = hk.rowRect(act);
    check(row.w > 0, "CONTROLS has a row for the first action");
    click(hk, row);   // arm
    key(hk, SDLK_F7);
    check(effectiveChord(s.hotkeys, act) == KeyChord{SDLK_F7, 0} && changes == 1, "a key press rebinds the row");
    check(saves == 1 && loadSettings().hotkeys == s.hotkeys && !s.hotkeys.empty(), "a rebind is saved at once, no SAVE");

    const auto bound = s.hotkeys;
    click(hk, hk.defaultsRect());
    check(s.hotkeys == bound && saves == 1, "DEFAULTS alone changes nothing");
    check(hk.defaultsNoRect().w > 0, "DEFAULTS asks RESET TO DEFAULTS? YES / NO");
    click(hk, hk.defaultsNoRect());
    check(s.hotkeys == bound && saves == 1 && loadSettings().hotkeys == bound, "NO keeps the keys");
    click(hk, hk.defaultsRect());
    click(hk, hk.defaultsYesRect());
    check(s.hotkeys.empty() && saves == 2 && loadSettings().hotkeys.empty(), "YES resets the keys and saves");
}

int main() {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::printf("SDL_Init: %s\n", SDL_GetError()); return 1; }
    const fs::path dir = fs::temp_directory_path() / ("options_autosave_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    setSettingsPath((dir / "settings.ini").string());
    SDL_Window* win = SDL_CreateWindow("autosave", 0, 0, W, H, SDL_WINDOW_HIDDEN);
    SDL_Renderer* ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : nullptr;
    if (!ren) { std::printf("renderer: %s\n", SDL_GetError()); return 1; }
    testOptions(ren);
    testVoice(ren);
    testControls(ren);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    setSettingsPath("");
    std::error_code ec;
    fs::remove_all(dir, ec);
    SDL_Quit();
    std::printf("options_autosave_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
