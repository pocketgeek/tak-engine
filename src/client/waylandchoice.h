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
// Options -> Display -> NATIVE WAYLAND (Settings::nativeWayland): Auto, On, Off. Applied
// before SDL_Init (restart required). SDL_VIDEODRIVER in the environment always wins.
// Pure decision code here; main.cpp and Cartographer apply it.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

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

// Does XDG_CURRENT_DESKTOP (a colon-separated list) name KDE?
inline bool desktopIsKde(std::string_view desktops) {
    while (!desktops.empty()) {
        const size_t c = desktops.find(':');
        std::string item(desktops.substr(0, c));
        std::transform(item.begin(), item.end(), item.begin(), [](unsigned char ch) { return char(std::toupper(ch)); });
        if (item == "KDE") return true;
        desktops = c == std::string_view::npos ? std::string_view() : desktops.substr(c + 1);
    }
    return false;
}

// The decision. Off never, On whenever a Wayland compositor is there, Auto likewise
// except on KDE when X11 apps already get real pixels (Plasma's default).
inline bool wantNative(int setting, const Session& s) {
    if (setting == kOff || s.driverForced || !s.wayland) return false;
    if (setting == kOn) return true;
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
};
inline Startup& startup() { static Startup s; return s; }

}  // namespace tak::wayland
