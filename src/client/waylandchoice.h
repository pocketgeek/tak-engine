// waylandchoice -- Linux: native Wayland window (real pixels) or X11 through XWayland.
//
// The shipped Linux build links a static SDL2, which picks its x11 driver whenever an X
// server is reachable -- under a Wayland desktop that is XWayland. At a display scale
// above 100% most compositors (GNOME, Sway and other wlroots ones, Hyprland, KDE with
// "Legacy applications: scaled by the system") draw such a window at 1x and stretch it,
// which is blurry. Native Wayland with SDL_WINDOW_ALLOW_HIGHDPI gets the real pixel size
// instead (fractional scales included: the renderer output is window points x scale),
// and the window then speaks points exactly as a macOS Retina window does
// (client/hidpi.h).
//
// KDE Plasma is the exception that is already crisp: with "Legacy applications (X11):
// apply scaling themselves" (Plasma's default; kwinrc [Xwayland] XwaylandClientsScale
// absent or true) kwin gives X11 apps the real pixel size -- XWayland's screen is the
// output's native size. Auto keeps X11 there, so a player whose KDE desktop already
// shows the game crisply sees no change; "scaled by the system" gets native Wayland.
//
// GNOME (Mutter) draws no title bars for Wayland windows: SDL2 draws them with libdecor,
// loaded at run time. Without libdecor-0.so.0 a GNOME window would have no title bar, so
// Auto keeps X11 there (On still means Wayland).
//
// Options -> Display -> NATIVE WAYLAND (Settings::nativeWayland): Auto, On, Off. Applied
// before SDL_Init (restart required). SDL_VIDEODRIVER in the environment always wins.
// Pure decision code here; main.cpp and Cartographer apply it.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#if defined(__linux__)
#include <dlfcn.h>
#endif

namespace tak::wayland {

enum Setting : int { kAuto = 0, kOn = 1, kOff = 2 };

inline const char* settingLabel(int s) {
    return s == kOn ? "ON" : s == kOff ? "OFF" : "AUTO";
}

// What the session looks like, gathered from the environment (and kwinrc on KDE).
struct Session {
    bool wayland = false;          // a Wayland compositor to talk to (WAYLAND_DISPLAY / session type)
    bool driverForced = false;     // SDL_VIDEODRIVER set by the user (or a test): leave SDL alone
    bool kde = false;              // XDG_CURRENT_DESKTOP lists KDE
    bool kdeX11RealPixels = true;  // kwinrc [Xwayland] XwaylandClientsScale (default true)
    bool gnome = false;            // XDG_CURRENT_DESKTOP lists GNOME (no server-side title bars)
    bool libdecor = true;          // libdecor-0.so.0 loads (SDL2 draws GNOME title bars with it)
};

// kwinrc [Xwayland] XwaylandClientsScale: Plasma's "Legacy applications (X11)" choice.
// true (the default, also when the key or file is absent) = "apply scaling themselves":
// X11 windows get real pixels. false = "scaled by the system": stretched, blurry.
inline bool kwinXwaylandClientsScale(std::string_view kwinrc) {
    std::istringstream in{std::string(kwinrc)};
    std::string line;
    bool inGroup = false, clientsScale = true;
    const auto trim = [](std::string s) {
        size_t b = 0, e = s.size();
        while (b < e && std::isspace(uint8_t(s[b]))) ++b;
        while (e > b && std::isspace(uint8_t(s[e - 1]))) --e;
        return s.substr(b, e - b);
    };
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') { inGroup = line == "[Xwayland]"; continue; }
        if (!inGroup) continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos || trim(line.substr(0, eq)) != "XwaylandClientsScale") continue;
        std::string v = trim(line.substr(eq + 1));
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
        clientsScale = !(v == "false" || v == "0" || v == "off" || v == "no");
    }
    return clientsScale;
}

// Does XDG_CURRENT_DESKTOP (a colon-separated list) name this desktop (upper case)?
inline bool desktopIs(std::string_view desktops, std::string_view name) {
    while (!desktops.empty()) {
        const size_t c = desktops.find(':');
        std::string item(desktops.substr(0, c));
        std::transform(item.begin(), item.end(), item.begin(), [](unsigned char ch) { return char(std::toupper(ch)); });
        if (item == name) return true;
        desktops = c == std::string_view::npos ? std::string_view() : desktops.substr(c + 1);
    }
    return false;
}
inline bool desktopIsKde(std::string_view desktops) { return desktopIs(desktops, "KDE"); }

// The decision. Off never, On whenever a Wayland compositor is there, Auto likewise
// except on KDE when X11 apps already get real pixels (Plasma's default), and on GNOME
// when libdecor is missing (the window would have no title bar).
inline bool wantNative(int setting, const Session& s) {
    if (setting == kOff || s.driverForced || !s.wayland) return false;
    if (setting == kOn) return true;
    if (s.gnome && !s.libdecor) return false;
    return !(s.kde && s.kdeX11RealPixels);
}

inline Session probeSession() {
    Session s;
    const auto env = [](const char* k) -> std::string_view {
        const char* v = std::getenv(k);
        return v ? std::string_view(v) : std::string_view();
    };
    s.wayland = !env("WAYLAND_DISPLAY").empty() || env("XDG_SESSION_TYPE") == "wayland";
    s.driverForced = !env("SDL_VIDEODRIVER").empty();
    s.kde = desktopIsKde(env("XDG_CURRENT_DESKTOP"));
    s.gnome = desktopIs(env("XDG_CURRENT_DESKTOP"), "GNOME");
#if defined(__linux__)
    if (s.gnome && s.wayland) {
        void* lib = dlopen("libdecor-0.so.0", RTLD_LAZY | RTLD_LOCAL);
        s.libdecor = lib != nullptr;
        if (lib) dlclose(lib);
    }
#endif
    if (s.kde) {
        std::string dir(env("XDG_CONFIG_HOME"));
        if (dir.empty() && !env("HOME").empty()) dir = std::string(env("HOME")) + "/.config";
        if (!dir.empty()) {
            std::ifstream f(dir + "/kwinrc", std::ios::binary);
            if (f) {
                std::ostringstream text;
                text << f.rdbuf();
                s.kdeX11RealPixels = kwinXwaylandClientsScale(text.str());
            }
        }
    }
    return s;
}

// What this process started with (set once before SDL_Init), so the Options row can say
// RESTART REQUIRED and the HiDPI code knows the window speaks points.
struct Startup {
    int setting = kAuto;    // Settings::nativeWayland as read at startup
    bool requested = false; // wantNative() said yes, so SDL was asked for "wayland,x11"
    bool native = false;    // SDL's video driver really is wayland
    Session session;        // probeSession() as seen at startup (what an X11 window gets)
};
inline Startup& startup() { static Startup s; return s; }

// ---- Keeping UI Scale / Cursor Size the same physical size across a switch ----------
//
// Which window a session got decides what a size setting means:
//   x11-real       X11 at real pixels (an X11 desktop, or KDE Plasma's default "apply
//                  scaling themselves"): window points = renderer pixels = screen pixels.
//   x11-stretched  X11 that the Wayland desktop draws at 1x and enlarges by its scale S
//                  (GNOME, wlroots, Hyprland, KDE "scaled by the system"; also SDL's
//                  wayland driver forced without HiDPI): one renderer pixel = S screen
//                  pixels, window points = renderer pixels.
//   wayland        native Wayland (SDL_WINDOW_ALLOW_HIGHDPI): window points x S =
//                  renderer pixels = screen pixels.
// The game sizes its HUD and cursor in RENDERER PIXELS (client/hidpi.h), so its UI Scale
// and Cursor Size are in screen pixels in x11-real and wayland, and in points (S screen
// pixels each) only in x11-stretched. Cartographer scales by its UI SCALE times the
// backing scale (window points), so its scale and window size are in screen pixels only
// in x11-real and in points otherwise.
//
// A value keeps its physical size across a switch by converting between those units:
// x S going from points to pixels, / S from pixels to points, unchanged when the unit is
// the same -- so KDE's x11-real <-> wayland leaves the game's settings alone, GNOME's
// x11-stretched -> wayland multiplies them by S, and switching back divides again. The
// previous session's mode is remembered (Settings::displayMode, Cartographer's
// preferences), so each start converts at most once per switch and never compounds. S
// is the native Wayland scale: measured now when this session is native, else the last
// one remembered (an X11 window cannot see the compositor's scale); unknown = 1 = no
// change.
enum Mode : int { kModeUnknown = 0, kModeX11Real = 1, kModeX11Stretched = 2, kModeWayland = 3 };

inline const char* modeName(int m) {
    return m == kModeX11Real ? "x11-real" : m == kModeX11Stretched ? "x11-stretched" : m == kModeWayland ? "wayland" : "";
}
inline int parseMode(std::string_view v) {
    return v == "x11-real" ? kModeX11Real : v == "x11-stretched" ? kModeX11Stretched : v == "wayland" ? kModeWayland : kModeUnknown;
}

// What an X11 window gets in this session.
inline int x11Mode(const Session& s) {
    return s.wayland && !(s.kde && s.kdeX11RealPixels) ? kModeX11Stretched : kModeX11Real;
}

// This session's mode from SDL's video driver; unknown for anything else (dummy, kmsdrm).
inline int sessionMode(std::string_view driver, bool native, const Session& s) {
    if (driver == "wayland") return native ? kModeWayland : kModeX11Stretched;   // no HiDPI: stretched too
    if (driver == "x11") return x11Mode(s);
    return kModeUnknown;
}

enum class Units { GamePixels, CartographerPoints };
inline bool inPoints(int mode, Units u) {
    return u == Units::GamePixels ? mode == kModeX11Stretched : mode != kModeX11Real;
}

// The remembered mode and native scale (0 = none seen yet).
struct DisplayHistory {
    int mode = kModeUnknown;
    float scale = 0.0f;
};

// Records this session in `h` and returns the factor for the size settings (1 = leave
// them). `assumedPrev` stands in for a history that does not exist yet: the mode the
// settings were last chosen in (for a file from before this was recorded, the X11 window
// of this session -- every earlier Linux build ran X11).
inline float advanceDisplay(DisplayHistory& h, int now, float nowScale, int assumedPrev, Units u) {
    if (now == kModeUnknown) return 1.0f;
    const int prev = h.mode != kModeUnknown ? h.mode : assumedPrev;
    if (now == kModeWayland && nowScale > 0.0f) h.scale = nowScale;
    const float S = h.scale;
    h.mode = now;
    if (prev == kModeUnknown || inPoints(prev, u) == inPoints(now, u) || !(S > 1.01f)) return 1.0f;
    return inPoints(prev, u) ? S : 1.0f / S;
}

}  // namespace tak::wayland
