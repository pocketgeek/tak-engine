// chatbox_test -- the in-game chat box (client/chatbox.h) and its two Options keys.
//
// Settings: CHAT SCALE (chatScale, 0.5..3.0, default 1.0) and CHAT POSITION (chatAlign,
// 0 left / 1 centered, default centered) survive a save and load, a file without them
// gets the defaults, and out-of-range values are clamped. Layout: word wrap, the box's
// placement left/centred over the play area, lifting over the build row, and fading.

#include "client/chatbox.h"
#include "client/settings.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace cb = tak::chatbox;

static int g_fail = 0;
static void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++g_fail;
}

int main() {
    std::printf("chatbox_test\n");

    // ---- settings ----
    namespace fs = std::filesystem;
    std::mt19937_64 rng(std::random_device{}());
    const fs::path dir = fs::temp_directory_path() / ("tak-chatbox-" + std::to_string(rng()));
    fs::create_directories(dir);
    const fs::path file = dir / "settings.ini";
    tak::setSettingsPath(file.string());
    {
        tak::Settings d;
        check(d.chatScale == 1.0f && d.chatAlign == cb::Centered, "defaults: 100%, centered");
        std::ofstream(file) << "uiScale = 1.5\n";
        const auto old = tak::loadSettings();
        check(old.chatScale == 1.0f && old.chatAlign == cb::Centered, "a file without the keys loads the defaults");

        tak::Settings s;
        s.chatScale = 2.5f;
        s.chatAlign = cb::Left;
        check(tak::saveSettings(s), "save");
        const auto back = tak::loadSettings();
        check(back.chatScale == 2.5f && back.chatAlign == cb::Left, "chatScale / chatAlign round-trip");
        std::ifstream in(file);
        const std::string text((std::istreambuf_iterator<char>(in)), {});
        check(text.find("chatScale = 2.5\n") != std::string::npos &&
              text.find("chatAlign = 0\n") != std::string::npos, "written under chatScale / chatAlign");

        std::ofstream(file) << "chatScale = 9\nchatAlign = 7\n";
        auto hi = tak::loadSettings();
        check(hi.chatScale == 3.0f && hi.chatAlign == 1, "too high: clamped to 300% / centered");
        std::ofstream(file) << "chatScale = 0.1\nchatAlign = -3\n";
        auto lo = tak::loadSettings();
        check(lo.chatScale == 0.5f && lo.chatAlign == 0, "too low: clamped to 50% / left");

        tak::Settings a, b;
        b.chatScale = 2.0f;
        check(!(a == b), "chatScale visible to operator== (DEFAULTS button)");
        b = a; b.chatAlign = cb::Left;
        check(!(a == b), "chatAlign visible to operator==");
        tak::Settings cur; cur.chatScale = 0.5f; cur.chatAlign = cb::Left;
        const auto reset = tak::preferenceDefaults(cur);
        check(reset.chatScale == 1.0f && reset.chatAlign == cb::Centered, "DEFAULTS resets both");
    }
    tak::setSettingsPath("");
    std::error_code ec;
    fs::remove_all(dir, ec);

    // ---- wrap: a fixed 10 px per byte ----
    auto w10 = [](const std::string& t) { return float(t.size()) * 10; };
    {
        auto r = cb::wrap("hello world", 200, 200, w10);
        check(r.size() == 1 && r[0] == "hello world", "a short line stays one row");
        r = cb::wrap("aaa bbb ccc ddd", 75, 75, w10);
        check(r.size() == 2 && r[0] == "aaa bbb" && r[1] == "ccc ddd", "wraps between words, no trailing space");
        r = cb::wrap("aaa bbb ccc", 35, 75, w10);
        check(r.size() == 2 && r[0] == "aaa" && r[1] == "bbb ccc", "the first row has its own (narrower) width");
        r = cb::wrap("abcdefghijkl", 50, 50, w10);
        check(r.size() == 3 && r[0] == "abcde" && r[2] == "kl", "an over-long word breaks between characters");
        r = cb::wrap("\xc3\xa9\xc3\xa9\xc3\xa9", 25, 25, w10);   // three 2-byte characters
        check(r.size() == 3 && r[0] == "\xc3\xa9", "never splits a UTF-8 character");
        r = cb::wrap("abcdefgh", 30, 100, w10);
        check(r.size() == 2 && r[0].empty() && r[1] == "abcdefgh",
              "a word too wide for the first row but not a continuation row moves down whole");
        r = cb::wrap("hi abcdefgh", 30, 100, w10);
        check(r.size() == 2 && r[0] == "hi" && r[1] == "abcdefgh", "...also after a short first word");
        r = cb::wrap("abcdefghijkl", 30, 100, w10);
        check(r.size() == 2 && r[0] == "abc" && r[1] == "defghijkl", "a word wider than any row still breaks after the name");
        r = cb::wrap("", 50, 50, w10);
        check(r.size() == 1 && r[0].empty(), "empty text is one empty row");
    }

    // ---- place ----
    {
        const cb::Rect none{};
        auto c = cb::place(cb::Centered, 400, 100, 1000, 700, 0, 10, none);
        check(c.x == 300 && c.y == 590 && c.w == 400, "centered over the play area, above the bar");
        auto l = cb::place(cb::Left, 400, 100, 1000, 700, 0, 10, none);
        check(l.x == 10 && l.y == 590, "left: at the margin");
        auto wide = cb::place(cb::Centered, 5000, 100, 1000, 700, 0, 10, none);
        check(wide.x == 10 && wide.w == 980, "never wider than the play area");
        auto lifted = cb::place(cb::Centered, 400, 100, 1000, 700, 0, 10, {350, 640, 300, 60});
        check(lifted.y + lifted.h <= 630, "lifts over an overlapping build row");
        auto clear = cb::place(cb::Left, 200, 100, 1000, 700, 0, 10, {600, 640, 300, 60});
        check(clear.y == 590, "a build row elsewhere does not move it");
        auto tall = cb::place(cb::Left, 200, 900, 1000, 700, 50, 10, none);
        check(tall.y == 50, "never above the top limit");
    }

    // ---- fade ----
    check(cb::lineAlpha(0) == 1 && cb::lineAlpha(cb::kHoldSeconds) == 1, "a new line is fully shown");
    const float mid = cb::lineAlpha(cb::kHoldSeconds + cb::kFadeSeconds / 2);
    check(mid > 0.4f && mid < 0.6f, "then fades");
    check(cb::lineAlpha(cb::kHoldSeconds + cb::kFadeSeconds + 1) == 0, "and is gone");

    std::printf(g_fail ? "chatbox_test: %d FAILED\n" : "chatbox_test: all ok\n", g_fail);
    return g_fail ? 1 : 0;
}
