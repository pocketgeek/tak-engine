// settings_test -- every Settings field must be visible to operator==.
//
// atDefaults() IS that comparison (options.h builds a default Settings and compares),
// so a field left out of it makes the DEFAULTS button go dead for anyone who changed
// only that field: the UI believes they are already at defaults. That is not
// hypothetical -- it shipped when `unitShadows` was added and the comparison was not
// updated, and `dataDir`/`dataManifest` were missing from it too.
//
// The failure mode is silent and the fix is one line, so the only real defence is a
// test that fails when a new field is added without touching the comparison. Each case
// below flips exactly one field away from its default and asserts the comparison
// notices. Adding a field to Settings without adding it here leaves it unguarded, so
// keep the list in step with the struct -- that is the point of the test.
//
// "Every field" includes the CONTAINERS. The first version of this file said it covered
// every field and quietly skipped knownServers, hotkeys and campaignCompleted, which is
// how DEFAULTS-wipes-campaign-progress got past it.

#include "client/settings.h"
#include "client/waylandchoice.h"

#include <cstdio>
#include <string>
#ifdef TAK_SETTINGS_FILE_TEST
#include <filesystem>
#include <fstream>
#include <random>
#endif

using tak::Settings;

static int g_fail = 0;

// Flip one field via a lambda, then assert the comparison sees a difference.
template <typename F>
static void flips(const char* field, F mutate) {
    Settings a{}, b{};
    mutate(b);
    const bool noticed = !(a == b);
    std::printf("  %-22s %s\n", field, noticed ? "ok" : "FAIL (invisible to operator==)");
    if (!noticed) ++g_fail;
    // And the reverse, so a comparison that is accidentally one-directional is caught.
    if (noticed && (b == a)) {
        std::printf("  %-22s FAIL (asymmetric)\n", field);
        ++g_fail;
    }
}

int main() {
    std::printf("settings_test: every field visible to operator==\n");

    // display / window
    flips("fullscreen",      [](Settings& s) { s.fullscreen = !s.fullscreen; });
    flips("vsync",           [](Settings& s) { s.vsync = !s.vsync; });
    flips("maxFps",          [](Settings& s) { s.maxFps += 17; });
    flips("renderer",        [](Settings& s) { s.renderer = "software"; });
    flips("ignoreDpiScaling",[](Settings& s) { s.ignoreDpiScaling = !s.ignoreDpiScaling; });
    flips("nativeWayland",   [](Settings& s) { s.nativeWayland = 2; });
    flips("displayMode",     [](Settings& s) { s.displayMode = 3; });
    flips("displayScale",    [](Settings& s) { s.displayScale = 1.45f; });
    flips("displayX11Width", [](Settings& s) { s.displayX11Width = 7680; });
    flips("displayLogicalW", [](Settings& s) { s.displayLogicalWidth = 5297; });
    flips("uiScale",         [](Settings& s) { s.uiScale += 0.25f; });
    flips("textureFilter",   [](Settings& s) { s.textureFilter = (s.textureFilter + 1) % 3; });
    flips("textureFilter 0", [](Settings& s) { s.textureFilter = 0; });
    flips("buildBarAlign",   [](Settings& s) { s.buildBarAlign = (s.buildBarAlign + 1) % 3; });
    flips("buildBarScale",   [](Settings& s) { s.buildBarScale += 0.25f; });
    flips("chatScale",       [](Settings& s) { s.chatScale += 0.5f; });
    flips("chatAlign",       [](Settings& s) { s.chatAlign = 0; });
    flips("uiFont",          [](Settings& s) { s.uiFont = 1; });
    flips("treeSway",        [](Settings& s) { s.treeSway = !s.treeSway; });
    flips("screenShake",    [](Settings& s) { s.screenShake = !s.screenShake; });
    flips("tacticalDots",    [](Settings& s) { s.tacticalDots = !s.tacticalDots; });
    flips("mouseZoom",       [](Settings& s) { s.mouseZoom = !s.mouseZoom; });
    flips("hiddenUnits",     [](Settings& s) { s.hiddenUnits = 1; });
    flips("hiddenUnits 0",   [](Settings& s) { s.hiddenUnits = 0; });
    flips("hiddenFadeOpacity", [](Settings& s) { s.hiddenFadeOpacity = 40; });
    flips("unitShadows",     [](Settings& s) { s.unitShadows = !s.unitShadows; });
    flips("gpuMemoryLimit",  [](Settings& s) { s.gpuMemoryLimit = 2048; });
    flips("gpuMemoryLimit -1", [](Settings& s) { s.gpuMemoryLimit = -1; });
    flips("smoothArt",       [](Settings& s) { s.smoothArt = !s.smoothArt; });
    flips("videoDeblock",    [](Settings& s) { s.videoDeblock = !s.videoDeblock; });
    flips("healthBars",      [](Settings& s) { s.healthBars = (s.healthBars + 1) % 3; });
    flips("statsPanel",      [](Settings& s) { s.statsPanel = !s.statsPanel; });

    // audio
    flips("masterVol",       [](Settings& s) { s.masterVol -= 11; });
    flips("bgmVol",          [](Settings& s) { s.bgmVol -= 11; });
    flips("sfxVol",          [](Settings& s) { s.sfxVol -= 11; });
    flips("soundVoices",     [](Settings& s) { s.soundVoices = 64; });
    flips("evenSoundLevels", [](Settings& s) { s.evenSoundLevels = !s.evenSoundLevels; });
    flips("chanGain[0]",     [](Settings& s) { s.chanGain[0] = 0.5f; });
    flips("chanGain[7]",     [](Settings& s) { s.chanGain[7] = 0.5f; });
    flips("audioDevice",     [](Settings& s) { s.audioDevice = "some-device"; });
    flips("customMusicTracks", [](Settings& s) { s.customMusicTracks[3] = "/music/a.flac"; });
    flips("voiceEnabled",    [](Settings& s) { s.voiceEnabled = !s.voiceEnabled; });
    flips("voiceMicDevice",  [](Settings& s) { s.voiceMicDevice = "a-mic"; });
    flips("voiceOutputSameAsGame", [](Settings& s) { s.voiceOutputSameAsGame = !s.voiceOutputSameAsGame; });
    flips("voiceOutputDevice", [](Settings& s) { s.voiceOutputDevice = "headset"; });
    flips("voiceMicVolume",  [](Settings& s) { s.voiceMicVolume -= 11; });
    flips("voiceMicEnabled", [](Settings& s) { s.voiceMicEnabled = !s.voiceMicEnabled; });
    flips("idleBuilders",    [](Settings& s) { s.idleBuilders = 3; });
    flips("idleBuildersScale", [](Settings& s) { s.idleBuildersScale = 1.5f; });
    flips("voiceOutputVolume", [](Settings& s) { s.voiceOutputVolume -= 11; });

    // camera / input
    flips("mouseZoomSpeed",  [](Settings& s) { s.mouseZoomSpeed += 0.5f; });
    flips("edgeScrollSpeed", [](Settings& s) { s.edgeScrollSpeed += 0.5f; });
    flips("edgeScroll",      [](Settings& s) { s.edgeScroll = !s.edgeScroll; });
    flips("cursorScale",     [](Settings& s) { s.cursorScale += 1; });
    flips("hardwareCursor",  [](Settings& s) { s.hardwareCursor = !s.hardwareCursor; });
    flips("smoothMotion",    [](Settings& s) { s.smoothMotion = !s.smoothMotion; });

    // identity / session
    flips("playerName",      [](Settings& s) { s.playerName = "someone"; });
    flips("accountName",     [](Settings& s) { s.accountName = "someone"; });
    flips("streamRememberKey", [](Settings& s) { s.streamRememberKey = !s.streamRememberKey; });
    flips("streamBitrate", [](Settings& s) { s.streamBitrate = 20000; });
    flips("streamResolution", [](Settings& s) { s.streamResolution = 1; });
    flips("streamFps", [](Settings& s) { s.streamFps = 30; });
    flips("streamCpuEncoder", [](Settings& s) { s.streamCpuEncoder = true; });
    flips("streamLocalCopy", [](Settings& s) { s.streamLocalCopy = !s.streamLocalCopy; });
    flips("gameCreate.crusades", [](Settings& s) { s.gameCreate.crusades = !s.gameCreate.crusades; });
    flips("gameCreate.pathfindingMode", [](Settings& s) { s.gameCreate.pathfindingMode = tak::sim::PathfindingMode::Legion; });
    flips("benchmarkPathfinding", [](Settings& s) { s.benchmarkPathfinding = tak::sim::PathfindingMode::Legion; });
    flips("benchmarkSpeed",  [](Settings& s) { s.benchmarkSpeed = 0; });
    flips("gameCreate.doubleSight", [](Settings& s) { s.gameCreate.doubleSight = !s.gameCreate.doubleSight; });
    flips("gameCreate.speedUnlock", [](Settings& s) { s.gameCreate.speedUnlock = !s.gameCreate.speedUnlock; });
    flips("gameCreate.monarchExpendable", [](Settings& s) { s.gameCreate.monarchExpendable = !s.gameCreate.monarchExpendable; });
    flips("gameCreate.randomStarts", [](Settings& s) { s.gameCreate.randomStarts = (s.gameCreate.randomStarts + 1) % 3; });
    flips("gameCreate.randomStarts 2", [](Settings& s) { s.gameCreate.randomStarts = 2; });
    flips("gameCreate.allowSpectators", [](Settings& s) { s.gameCreate.allowSpectators = !s.gameCreate.allowSpectators; });
    flips("gameCreate.powerCodes", [](Settings& s) { s.gameCreate.powerCodes = !s.gameCreate.powerCodes; });
    flips("gameCreate.generated", [](Settings& s) { s.gameCreate.generated = !s.gameCreate.generated; });
    flips("gameCreate.unitCap", [](Settings& s) { ++s.gameCreate.unitCap; });
    flips("gameCreate.fog", [](Settings& s) { ++s.gameCreate.fog; });
    flips("gameCreate.mapSort", [](Settings& s) { ++s.gameCreate.mapSort; });
    flips("gameCreate.mapSortDir", [](Settings& s) { ++s.gameCreate.mapSortDir; });
    flips("gameCreate.name", [](Settings& s) { s.gameCreate.name = "test"; });
    flips("gameCreate.generator", [](Settings& s) { s.gameCreate.generator = "test"; });
    flips("gameModPacks", [](Settings& s){s.gameModPacks={"Pack A"};});
    flips("cosmeticModPacks", [](Settings& s){s.cosmeticModPacks={"Pack B"};});
    flips("lastMap",         [](Settings& s) { s.lastMap = "Inner Circle"; });
    flips("dataDir",         [](Settings& s) { s.dataDir = "/somewhere/else"; });
    flips("dataManifest",    [](Settings& s) { s.dataManifest = "deadbeef"; });

    // Container fields. The first version of this test skipped these while its header
    // claimed to cover every field -- a coverage claim is worth no more than the cases
    // behind it, and these three are exactly where DEFAULTS was destroying real user
    // data (campaign progress and the server list).
    flips("knownServers",    [](Settings& s) { s.knownServers.push_back("host:7777"); });
    flips("hotkeys",         [](Settings& s) { s.hotkeys["selectAll"] = "ctrl+a"; });
    flips("campaignCompleted", [](Settings& s) { s.campaignCompleted["aramon"].insert(3); });
    // ...and a second entry in the same container, so a comparison that only checks
    // emptiness rather than contents is caught too.
    {
        Settings a{}, b{};
        a.campaignCompleted["aramon"].insert(1);
        b.campaignCompleted["aramon"].insert(2);
        const bool noticed = !(a == b);
        std::printf("  %-22s %s\n", "campaign contents",
                    noticed ? "ok" : "FAIL (compares size/emptiness only)");
        if (!noticed) ++g_fail;
    }
    {
        Settings a{}, b{};
        a.knownServers.push_back("one:1");
        b.knownServers.push_back("two:2");
        const bool noticed = !(a == b);
        std::printf("  %-22s %s\n", "server contents",
                    noticed ? "ok" : "FAIL (compares size/emptiness only)");
        if (!noticed) ++g_fail;
    }

    // ---- what DEFAULTS does ----
    //
    // operator== coverage alone would NOT have caught the bug this section exists for:
    // DEFAULTS was erasing campaign progress and the remembered server list, and a
    // comparison test cannot see that, because the erasure happened in the reset handler.
    // preferenceDefaults() is now the single definition of the reset, so it can be tested
    // directly.
    {
        std::printf("what DEFAULTS preserves vs resets:\n");
        Settings cur{};
        // Records and configuration that must SURVIVE a reset.
        cur.campaignCompleted["aramon"].insert(4);
        cur.knownServers.push_back("friend:7777");
        cur.dataDir = "/games/kingdoms";
        cur.dataManifest = "abc123";
        cur.playerName = "curtis";
        cur.accountName = "curtis";
        cur.streamRememberKey = true;
        cur.streamLocalCopy = true;
        cur.streamBitrate = 45000;
        cur.streamResolution = 2;
        cur.streamFps = 30;
        cur.streamCpuEncoder = true;
        cur.lastMap = "Inner Circle";
        cur.gameCreate.crusades = true;
        cur.gameCreate.generator = "saved recipe";
        cur.benchmarkPathfinding = tak::sim::PathfindingMode::Legion;
        cur.benchmarkSpeed = 0;
        cur.hotkeys["selectAll"] = "ctrl+a";
        // Voice chat has its own screen and DEFAULTS: the Options reset leaves it alone.
        cur.voiceEnabled = true;
        cur.voiceMicDevice = "a-mic";
        cur.voiceOutputSameAsGame = false;
        cur.voiceOutputDevice = "headset";
        cur.voiceMicVolume = 40;
        cur.voiceMicEnabled = false;
        cur.voiceOutputVolume = 60;
        // Preferences that must be RESET.
        cur.unitShadows = false;
        cur.smoothArt = false;
        cur.videoDeblock = false;
        cur.masterVol = 7;
        cur.uiScale = 1.75f;
        cur.textureFilter = 0;
        cur.tacticalDots = false;
        cur.hiddenUnits = 0;
        cur.hiddenFadeOpacity = 20;
        cur.screenShake = true;
        cur.renderer = "opengles2";
        cur.gpuMemoryLimit = 768;
        cur.uiFont = 1;

        const Settings d = tak::preferenceDefaults(cur);
        if (!(d.gameCreate == cur.gameCreate)) ++g_fail;
        auto keep = [&](const char* what, bool ok) {
            std::printf("  keeps %-18s %s\n", what, ok ? "ok" : "FAIL (destroyed by DEFAULTS)");
            if (!ok) ++g_fail;
        };
        auto reset = [&](const char* what, bool ok) {
            std::printf("  resets %-17s %s\n", what, ok ? "ok" : "FAIL (not reset)");
            if (!ok) ++g_fail;
        };
        keep("campaign progress", d.campaignCompleted == cur.campaignCompleted &&
                                  d.missionCompleted("aramon", 4));
        keep("known servers",     d.knownServers == cur.knownServers);
        keep("benchmarkPathfinding", d.benchmarkPathfinding == tak::sim::PathfindingMode::Legion);
        keep("benchmarkSpeed",    d.benchmarkSpeed == 0);
        keep("dataDir",           d.dataDir == cur.dataDir);
        keep("dataManifest",      d.dataManifest == cur.dataManifest);
        keep("playerName",        d.playerName == cur.playerName);
        keep("accountName",       d.accountName == cur.accountName);
        keep("streamRememberKey", d.streamRememberKey);
        keep("streamLocalCopy",   d.streamLocalCopy);
        keep("streamBitrate",     d.streamBitrate == 45000);
        keep("streamResolution",  d.streamResolution == 2);
        keep("streamFps",         d.streamFps == 30);
        keep("streamCpuEncoder",  d.streamCpuEncoder);
        keep("lastMap",           d.lastMap == cur.lastMap);
        keep("hotkeys",           d.hotkeys == cur.hotkeys);
        keep("voice chat",        d.voiceEnabled && d.voiceMicDevice == "a-mic" && !d.voiceOutputSameAsGame &&
                                  d.voiceOutputDevice == "headset" && d.voiceMicVolume == 40 &&
                                  !d.voiceMicEnabled && d.voiceOutputVolume == 60);

        // ...and the VOICE CHAT screen's DEFAULTS resets exactly the voice fields: voice
        // off (the default), system devices, full volumes; everything else untouched.
        const Settings v = tak::voiceDefaults(cur);
        const Settings fresh0{};
        reset("voice (own page)", !v.voiceEnabled && !fresh0.voiceEnabled && v.voiceMicDevice.empty() &&
                                  v.voiceOutputSameAsGame && v.voiceOutputDevice.empty() &&
                                  v.voiceMicVolume == 100 && v.voiceMicEnabled && v.voiceOutputVolume == 100);
        keep("non-voice on voice DEFAULTS", v.masterVol == cur.masterVol && v.uiScale == cur.uiScale &&
                                  v.hotkeys == cur.hotkeys && v.campaignCompleted == cur.campaignCompleted);

        const Settings fresh{};
        reset("unitShadows",      d.unitShadows == fresh.unitShadows);
        reset("smoothArt",        d.smoothArt == fresh.smoothArt);
        reset("videoDeblock",     d.videoDeblock == fresh.videoDeblock);
        keep("audio (own page)",  d.masterVol == cur.masterVol);
        // ...and the AUDIO screen's DEFAULTS resets the sound fields and puts every track back
        // to its original music, keeping everything else.
        Settings withMusic = cur;
        withMusic.customMusicTracks[15] = "/music/menu.mp3";
        withMusic.audioDevice = "speakers";
        const Settings a = tak::audioDefaults(withMusic);
        reset("audio (own page)", a.masterVol == fresh.masterVol && a.customMusicTracks.empty() && a.audioDevice.empty());
        keep("non-audio on audio DEFAULTS", a.uiScale == cur.uiScale && a.voiceEnabled == cur.voiceEnabled);
        keep("music on Options DEFAULTS", tak::preferenceDefaults(withMusic).customMusicTracks == withMusic.customMusicTracks);
        reset("uiScale",          d.uiScale == fresh.uiScale);
        reset("textureFilter",    d.textureFilter == fresh.textureFilter && d.textureFilter == 2);
        reset("tacticalDots",     d.tacticalDots == fresh.tacticalDots && d.tacticalDots);
        reset("hiddenUnits",      d.hiddenUnits == fresh.hiddenUnits && d.hiddenUnits == 2);
        reset("hiddenFadeOpacity", d.hiddenFadeOpacity == fresh.hiddenFadeOpacity && d.hiddenFadeOpacity == 50);
        reset("screenShake",      d.screenShake == fresh.screenShake && !d.screenShake);
        reset("tacticalDotsZoom", d.tacticalDotsZoom == fresh.tacticalDotsZoom && d.tacticalDotsZoom == 0);
        reset("renderer",         d.renderer == fresh.renderer && d.renderer.empty());
        reset("gpuMemoryLimit",   d.gpuMemoryLimit == fresh.gpuMemoryLimit && d.gpuMemoryLimit == 1280);
        reset("uiFont",           d.uiFont == fresh.uiFont && d.uiFont == 0);

        // The GRAPHICS, UI and GAME pages' DEFAULTS together reset exactly what the old
        // single Options DEFAULTS did: a field on no page would make this differ.
        {
            Settings all = cur;
            all.fullscreen = !all.fullscreen; all.vsync = !all.vsync; all.maxFps = 77; all.nativeWayland = 2;
            all.ignoreDpiScaling = !all.ignoreDpiScaling; all.smoothArt = !all.smoothArt; all.treeSway = !all.treeSway;
            all.smoothMotion = !all.smoothMotion; all.scorecardScale = 1.5f; all.healthBars = 2; all.statsPanel = !all.statsPanel;
            all.buildBarAlign = 2; all.buildBarScale = 2.5f; all.idleBuilders = 3; all.idleBuildersScale = 2.0f;
            all.chatAlign = 1; all.chatScale = 2.0f; all.hardwareCursor = !all.hardwareCursor; all.cursorScale = 5;
            all.mouseZoomSpeed = 2.0f; all.mouseZoom = !all.mouseZoom; all.edgeScroll = !all.edgeScroll; all.edgeScrollSpeed = 3.0f;
            const Settings pages = tak::gameDefaults(tak::uiDefaults(tak::graphicsDefaults(all)));
            const bool same = pages == tak::preferenceDefaults(all);
            std::printf("  %-23s %s\n", "pages => full reset", same ? "ok" : "FAIL (a field is on no Options page)");
            if (!same) ++g_fail;
            const bool own = tak::graphicsDefaults(all).uiScale == all.uiScale && tak::uiDefaults(all).unitShadows == all.unitShadows &&
                             tak::gameDefaults(all).uiScale == all.uiScale && tak::gameDefaults(all).playerName == all.playerName &&
                             tak::gameDefaults(all).mouseZoom == all.mouseZoom && tak::uiDefaults(all).mouseZoom != all.mouseZoom;
            std::printf("  %-23s %s\n", "each page its own", own ? "ok" : "FAIL");
            if (!own) ++g_fail;
        }

        // And the two questions must agree: after a reset we ARE at defaults, which is
        // the invariant that broke when the preserve list and atDefaults() diverged.
        const bool agree = (d == tak::preferenceDefaults(d));
        std::printf("  %-23s %s\n", "reset => atDefaults",
                    agree ? "ok" : "FAIL (reset does not settle at defaults)");
        if (!agree) ++g_fail;
    }

    // CONTROL: two untouched defaults must compare EQUAL. Without this the whole file
    // would still pass if operator== were simply `return false`.
    {
        Settings a{}, b{};
        const bool same = (a == b);
        std::printf("  %-22s %s\n", "(control) equal", same ? "ok" : "FAIL");
        if (!same) ++g_fail;
    }

    // First run (applyFirstRunScale): a fresh profile starts UI Scale and Cursor Size at
    // the display's scale (clamped), on every platform; an existing profile keeps its own.
    {
        auto check = [&](const char* what, bool ok) {
            std::printf("  %-22s %s\n", what, ok ? "ok" : "FAIL");
            if (!ok) ++g_fail;
        };
        Settings w{};   // Windows, a 150% primary display: DPI 144 / 96
        check("first: windows dpi", tak::applyFirstRunScale(w, true, 144.0f / 96.0f) &&
                                    w.uiScale == 1.5f && w.cursorScale == 2);
        Settings m{};   // macOS Retina: the backing scale, drawable / window
        check("first: mac backing", tak::applyFirstRunScale(m, true, tak::backingScale(1440, 2880)) &&
                                    m.uiScale == 2.0f && m.cursorScale == 2);
        Settings c{}; c.uiScale = 1.5f; c.cursorScale = 6;
        tak::applyFirstRunScale(c, true, 3.0f);
        check("first: clamped", c.uiScale == 2.0f && c.cursorScale == 8);
        Settings h{};
        check("first: 100% untouched", !tak::applyFirstRunScale(h, true, 1.0f) && h.uiScale == 1.0f &&
                                       h.cursorScale == 1);
        Settings e{}; e.uiScale = 1.25f; e.cursorScale = 3;
        check("first: existing kept", !tak::applyFirstRunScale(e, false, 2.0f) && e.uiScale == 1.25f &&
                                      e.cursorScale == 3);
        // macOS Retina: points vs pixels. Every non-Retina case is an identity.
        check("dpi: backing 1x", tak::backingScale(1280, 1280) == 1.0f && tak::backingScale(0, 0) == 1.0f);
        check("dpi: backing 2x", tak::backingScale(1280, 2560) == 2.0f);
        check("dpi: points 1x", tak::pixelsToPoints(1201, 1.0f) == 1201);
        check("dpi: points 2x up", tak::pixelsToPoints(1201, 2.0f) == 601 && tak::pixelsToPoints(480, 2.0f) == 240);
        check("dpi: hw cursor 1x", tak::hardwareCursorScale(3, 1.0f) == 3 && tak::hardwareCursorScale(0, 1.0f) == 1);
        check("dpi: hw cursor 2x", tak::hardwareCursorScale(2, 2.0f) == 1 && tak::hardwareCursorScale(4, 2.0f) == 2 &&
                                   tak::hardwareCursorScale(1, 2.0f) == 1);
        // Linux native Wayland at a fractional desktop scale (e.g. 150%, 1.45 measured on KDE).
        check("dpi: backing 1.5x", tak::backingScale(1280, 1920) == 1.5f);
        check("dpi: points 1.5x up", tak::pixelsToPoints(1201, 1.5f) == 801 && tak::pixelsToPoints(480, 1.5f) == 320);
        check("dpi: hw cursor 1.5x", tak::hardwareCursorScale(2, 1.5f) == 1 && tak::hardwareCursorScale(3, 1.5f) == 2);
    }

    // NATIVE WAYLAND decision (client/waylandchoice.h).
    {
        auto check = [&](const char* what, bool ok) {
            std::printf("  %-22s %s\n", what, ok ? "ok" : "FAIL");
            if (!ok) ++g_fail;
        };
        namespace wl = tak::wayland;
        check("wl: kwinrc default", wl::kwinXwaylandClientsScale("") &&
                                    wl::kwinXwaylandClientsScale("[Xwayland]\nScale=1.45\nXwaylandEisNoPromptApps=x\n"));
        check("wl: kwinrc system", !wl::kwinXwaylandClientsScale("[General]\nX=1\n[Xwayland]\nScale=2\nXwaylandClientsScale=false\n"));
        check("wl: kwinrc crlf", !wl::kwinXwaylandClientsScale("[Xwayland]\r\n XwaylandClientsScale = False \r\n"));
        check("wl: kwinrc true", wl::kwinXwaylandClientsScale("[Xwayland]\nXwaylandClientsScale=true\n"));
        check("wl: kwinrc group", wl::kwinXwaylandClientsScale("[Other]\nXwaylandClientsScale=false\n"));
        check("wl: kde list", wl::desktopIsKde("KDE") && wl::desktopIsKde("ubuntu:GNOME:kde") && !wl::desktopIsKde("GNOME") &&
                              !wl::desktopIsKde("") && !wl::desktopIsKde("KDEX"));
        wl::Session gnome; gnome.wayland = true;
        wl::Session x11; x11.wayland = false;
        wl::Session forced = gnome; forced.driverForced = true;
        wl::Session kdeSelf = gnome; kdeSelf.kde = true;
        wl::Session kdeSystem = kdeSelf; kdeSystem.kdeX11RealPixels = false;
        // The Auto matrix: every Wayland session is native except GNOME without libdecor.
        check("wl: auto gnome", wl::wantNative(wl::kAuto, gnome));
        check("wl: auto kde default", wl::wantNative(wl::kAuto, kdeSelf));   // Plasma's default: now native too
        check("wl: auto kde system", wl::wantNative(wl::kAuto, kdeSystem));
        check("wl: on kde self", wl::wantNative(wl::kOn, kdeSelf));
        check("wl: off", !wl::wantNative(wl::kOff, gnome) && !wl::wantNative(wl::kOff, kdeSystem));
        check("wl: no wayland", !wl::wantNative(wl::kOn, x11) && !wl::wantNative(wl::kAuto, x11));
        check("wl: env wins", !wl::wantNative(wl::kOn, forced) && !wl::wantNative(wl::kAuto, forced));
        check("wl: gnome list", wl::desktopIs("ubuntu:GNOME", "GNOME") && !wl::desktopIs("KDE", "GNOME"));
        wl::Session gnomeBare = gnome; gnomeBare.gnome = true; gnomeBare.libdecor = false;
        wl::Session swayBare = gnome; swayBare.libdecor = false;   // server-side title bars
        wl::Session gnomeDecor = gnomeBare; gnomeDecor.libdecor = true;
        check("wl: gnome no libdecor", !wl::wantNative(wl::kAuto, gnomeBare) && wl::wantNative(wl::kOn, gnomeBare));
        check("wl: gnome libdecor", wl::wantNative(wl::kAuto, gnomeDecor));
        check("wl: other no libdecor", wl::wantNative(wl::kAuto, swayBare));
        wl::Session kdeForced = kdeSelf; kdeForced.driverForced = true;
        check("wl: env wins on kde", !wl::wantNative(wl::kAuto, kdeForced) && !wl::wantNative(wl::kOn, kdeForced));
        check("wl: default auto", Settings{}.nativeWayland == wl::kAuto);

        // UI Scale / Cursor Size across window-kind switches (advanceDisplay). One
        // simulated start: what the game does with the settings at startup. The numbers are
        // the user's 57" (7680 x 2160 at 1.45: 5297 logical) and a 2560-wide GNOME laptop
        // at 1.5 (1707 logical). An X11 start under Wayland measures the X11 desktop width
        // (`x11W`); a native start the scale and the logical width. `fresh`: no settings
        // file when the game started, so the first-run rule applies first, as in main.cpp.
        auto start = [&](Settings& s, int now, float scale, int x11W, int logicalW,
                         int fallback = wl::kModeX11Real, bool fresh = false) {
            wl::Observation o{now, scale, x11W, logicalW};
            tak::applyFirstRunScale(s, fresh, wl::firstRunScale(o));
            wl::DisplayHistory h{s.displayMode, s.displayScale, s.displayX11Width, s.displayLogicalWidth};
            const float f = wl::advanceDisplay(h, o, wl::Units::GamePixels, fallback);
            tak::rescaleUi(s, f);
            s.displayMode = h.mode; s.displayScale = h.scale;
            s.displayX11Width = h.x11Width; s.displayLogicalWidth = h.logicalWidth;
            return f;
        };
        auto near = [](float a, float b) { return std::abs(a - b) < 0.001f; };
        const int R = wl::kModeX11Real, X = wl::kModeX11Stretched, W = wl::kModeWayland, U = wl::kModeX11;
        {   // The measurement: real pixels give an X11 desktop S times the logical one.
            check("x11: kde real", wl::classifyX11(7680, 5297, 1.45f) == R);
            check("x11: stretched", wl::classifyX11(5297, 5297, 1.45f) == X && wl::classifyX11(1707, 1707, 1.5f) == X);
            check("x11: kde max scale", wl::classifyX11(2 * 3627, 3627, 1.25f) == R);   // 2 outputs, X11 at the larger 2.0
            check("x11: unknown", wl::classifyX11(0, 5297, 1.45f) == U && wl::classifyX11(7680, 0, 1.45f) == U &&
                                  wl::classifyX11(7680, 7680, 1.0f) == U);
            wl::Session kdeSys = kdeSystem, kde = kdeSelf;
            check("x11: fallback", wl::fallbackX11Kind(kde) == R && wl::fallbackX11Kind(kdeSys) == X &&
                                   wl::fallbackX11Kind(gnome) == R);
        }
        {   // First run on Linux: a fresh profile in a native Wayland window starts at the
            // compositor's scale on every desktop -- KDE (whose X11 windows get real pixels)
            // and GNOME (whose X11 windows are stretched) alike. Nothing converts on that
            // start: there is no earlier window.
            Settings k;
            check("first: kde native S", start(k, W, 1.45f, 0, 5297, R, true) == 1.0f && near(k.uiScale, 1.45f) &&
                                         k.cursorScale == 1 && k.displayMode == W && near(k.displayScale, 1.45f));
            Settings g;
            check("first: gnome native S", start(g, W, 1.5f, 0, 1707, R, true) == 1.0f && near(g.uiScale, 1.5f) &&
                                           g.cursorScale == 2 && g.displayMode == W);
            // The next start is not fresh: nothing compounds.
            check("first: once", start(k, W, 1.45f, 0, 5297) == 1.0f && near(k.uiScale, 1.45f) &&
                                 start(g, W, 1.5f, 0, 1707) == 1.0f && near(g.uiScale, 1.5f));
            // Then switching NATIVE WAYLAND off converts as usual: KDE's real pixels keep the
            // sizes, GNOME's stretched X11 window divides by S.
            start(k, U, 0, 7680, 0);
            check("first: kde then x11", near(k.uiScale, 1.45f) && k.displayMode == R);
            start(g, U, 0, 1707, 0);
            check("first: gnome then x11", near(g.uiScale, 1.0f) && g.cursorScale == 1 && g.displayMode == X);
            // X11, plain or through XWayland: the defaults stay.
            Settings x, p;
            check("first: xwayland 1", start(x, U, 0, 1707, 0, R, true) == 1.0f && x.uiScale == 1.0f &&
                                       x.cursorScale == 1 && x.displayMode == U);
            check("first: plain x11 1", start(p, R, 0, 0, 0, R, true) == 1.0f && p.uiScale == 1.0f && p.displayMode == R);
            // A native start whose scale never settled keeps the defaults and records nothing.
            Settings u;
            check("first: unsettled", start(u, W, 0.0f, 0, 1707, R, true) == 1.0f && u.uiScale == 1.0f &&
                                      u.displayMode == 0);
        }
        {   // An existing profile is never rescaled on start: with no window recorded yet
            // this start is recorded and the player's sizes stay.
            Settings e; e.uiScale = 1.25f; e.cursorScale = 4;
            check("existing: native kept", start(e, W, 1.45f, 0, 5297) == 1.0f && e.uiScale == 1.25f &&
                                           e.cursorScale == 4 && e.displayMode == W);
            Settings x; x.uiScale = 0.75f; x.cursorScale = 2;
            check("existing: x11 kept", start(x, U, 0, 1707, 0) == 1.0f && x.uiScale == 0.75f &&
                                        x.cursorScale == 2 && x.displayMode == U);
        }
        {   // An X11 history recorded by an X11 start is measured by the next native start.
            Settings s; s.uiScale = 1.0f; s.cursorScale = 2;
            start(s, U, 0, 1707, 0);
            check("x11 rec: unmeasured", s.displayMode == U && s.displayX11Width == 1707 && s.uiScale == 1.0f);
            start(s, W, 1.5f, 0, 1707);
            check("x11 rec: stretched xS", near(s.uiScale, 1.5f) && s.cursorScale == 3);
            start(s, U, 0, 1707, 0);   // and back: measured stretched now, / S
            check("x11 rec: back /S", near(s.uiScale, 1.0f) && s.cursorScale == 2 && s.displayMode == X);
            start(s, W, 1.5f, 0, 1707);
            check("x11 rec: no compounding", near(s.uiScale, 1.5f) && s.cursorScale == 3);
            Settings k; k.uiScale = 2.0f; k.cursorScale = 4;
            start(k, U, 0, 7680, 0);
            start(k, W, 1.45f, 0, 5297);
            check("x11 rec: kde same", k.uiScale == 2.0f && k.cursorScale == 4);
        }
        {   // Widths from different monitors (docked / undocked between the sessions) or a
            // mixed-scale real-pixel X11 desktop give a ratio neither kind has: not measured.
            check("x11: other monitor", wl::classifyX11(2560, 5297, 1.45f) == U);
            check("x11: mixed real", wl::classifyX11(1920 + 3840, 3840, 2.0f) == U);
            // Native on the 57", then Off on the KDE laptop alone (real pixels, 2560 wide):
            // read as stretched this would shrink everything by 1.45.
            Settings s; s.uiScale = 1.5f; s.cursorScale = 4;
            s.displayMode = W; s.displayScale = 1.45f; s.displayLogicalWidth = 5297;
            check("dock: kde x11 same", start(s, U, 0, 2560, 0) == 1.0f && s.uiScale == 1.5f && s.displayMode == R);
            check("dock: back same", start(s, W, 1.45f, 0, 5297) == 1.0f && s.uiScale == 1.5f && s.cursorScale == 4);
        }
        {   // An X11 start that cannot measure itself against a known earlier window (native
            // on a 2560-wide external monitor, then X11 on the 1707-wide laptop alone) uses the
            // fallback and remembers it, so the next native start does not convert a second time.
            Settings s; s.uiScale = 1.0f; s.cursorScale = 2; s.displayMode = W; s.displayScale = 1.5f;
            s.displayLogicalWidth = 2560;
            check("guess: x11 left", start(s, U, 0, 1707, 0) == 1.0f && s.displayMode == R && s.uiScale == 1.0f);
            check("guess: no compounding", start(s, W, 1.5f, 0, 2560) == 1.0f && s.uiScale == 1.0f &&
                                           s.cursorScale == 2);
        }
        {   // The scale must be settled: an unsettled native start (0) records and changes nothing,
            // so the next start still converts.
            Settings g; g.uiScale = 1.0f; g.cursorScale = 2;
            start(g, U, 0, 1707, 0);
            check("settle: none", start(g, W, 0.0f, 0, 1707) == 1.0f && g.displayMode == U && g.displayScale == 0.0f &&
                                  g.uiScale == 1.0f);
            start(g, W, 1.5f, 0, 1707);
            check("settle: next start", near(g.uiScale, 1.5f) && g.displayMode == W);
        }
        {   // Changing the desktop scale while native: sizes are screen pixels, unchanged.
            Settings s; s.uiScale = 1.5f; s.cursorScale = 3; s.displayMode = W; s.displayScale = 1.5f;
            s.displayLogicalWidth = 1707; s.displayX11Width = 1707;
            const float f = start(s, W, 2.0f, 0, 1280);
            check("kind: wl scale change", f == 1.0f && s.uiScale == 1.5f && s.cursorScale == 3 && s.displayScale == 2.0f);
            start(s, U, 0, 1280, 0);   // back to stretched X11 now uses the latest scale
            check("kind: latest scale", near(s.uiScale, 0.75f) && s.cursorScale == 2 && s.displayMode == X);
        }
        {   // Never native: X11 starts only record; with no scale known nothing converts.
            Settings x; x.uiScale = 1.0f;
            check("kind: x11 only none", start(x, U, 0, 1707, 0) == 1.0f && start(x, U, 0, 1707, 0) == 1.0f &&
                                        x.uiScale == 1.0f && x.displayMode == U);
            Settings p;   // a plain X11 desktop is always real pixels
            check("kind: plain x11", start(p, R, 0, 0, 0) == 1.0f && p.displayMode == R);
            Settings u;
            check("kind: unknown driver", start(u, wl::kModeUnknown, 0, 0, 0) == 1.0f && u.displayMode == 0);
            Settings one; one.cursorScale = 2;
            start(one, W, 1.0f, 0, 2560);   // 100% desktop: nothing to convert
            check("kind: scale 1 none", one.uiScale == 1.0f && one.cursorScale == 2);
        }
        {   // Cartographer sizes in window points: only the real-pixel X11 window differs.
            auto cart = [](wl::DisplayHistory& h, int now, float scale, int x11W, int logicalW) {
                return wl::advanceDisplay(h, wl::Observation{now, scale, x11W, logicalW},
                                          wl::Units::CartographerPoints, wl::kModeX11Real);
            };
            wl::DisplayHistory h;
            check("cart: kde first", cart(h, U, 0, 7680, 0) == 1.0f && h.mode == U);
            check("cart: kde x11->wl /S", near(cart(h, W, 1.45f, 0, 5297), 1.0f / 1.45f));
            check("cart: wl scale same", cart(h, W, 2.0f, 0, 3840) == 1.0f);
            check("cart: kde wl->x11 xS", near(cart(h, U, 0, 7680, 0), 2.0f) && h.mode == R);
            wl::DisplayHistory g;
            check("cart: gnome same", cart(g, U, 0, 1707, 0) == 1.0f && cart(g, W, 1.5f, 0, 1707) == 1.0f &&
                                      cart(g, U, 0, 1707, 0) == 1.0f && g.mode == X);
        }
        {   // The window kind from SDL's driver and the session.
            wl::Session kde = kdeSelf, plain;
            check("mode: drivers", wl::sessionMode("x11", false, kde) == U && wl::sessionMode("x11", false, gnome) == U &&
                                   wl::sessionMode("x11", false, plain) == R &&
                                   wl::sessionMode("wayland", true, kde) == W && wl::sessionMode("wayland", false, gnome) == X &&
                                   wl::sessionMode("dummy", false, gnome) == wl::kModeUnknown);
            check("mode: names", wl::parseMode(wl::modeName(R)) == R && wl::parseMode(wl::modeName(X)) == X &&
                                 wl::parseMode(wl::modeName(W)) == W && wl::parseMode(wl::modeName(U)) == U &&
                                 wl::parseMode("junk") == wl::kModeUnknown);
        }
        {   // Clamped at the range ends: 2.0 cannot grow, so the way back is not exact.
            Settings s; s.uiScale = 2.0f; s.cursorScale = 8;
            tak::rescaleUi(s, 1.45f);
            check("kind: clamped", s.uiScale == 2.0f && s.cursorScale == 8);
            check("kind: factor 1 no-op", !tak::rescaleUi(s, 1.0f));
        }
    }

    // GPU MEMORY LIMIT values (parseGpuMemoryLimit / snap / write / label). 1280 MiB is the
    // default and what any unreadable value falls back to, and a hand-edited size snaps to
    // the nearest listed choice so the Options dropdown always shows what is in effect.
    {
        auto check = [&](const char* what, bool ok) {
            std::printf("  %-22s %s\n", what, ok ? "ok" : "FAIL");
            if (!ok) ++g_fail;
        };
        using tak::parseGpuMemoryLimit; using tak::gpuMemoryLimitValue; using tak::gpuMemoryLimitLabel;
        check("gpu: default 1280", Settings{}.gpuMemoryLimit == 1280 && tak::kGpuMemoryDefaultMiB == 1280);
        check("gpu: unlimited", parseGpuMemoryLimit("unlimited") == -1 && parseGpuMemoryLimit("Unlimited") == -1 &&
                                parseGpuMemoryLimit("-1") == -1);
        check("gpu: listed sizes", parseGpuMemoryLimit("512") == 512 && parseGpuMemoryLimit("1280") == 1280 &&
                                   parseGpuMemoryLimit("1536") == 1536 && parseGpuMemoryLimit("4096") == 4096);
        check("gpu: snaps sizes", parseGpuMemoryLimit("900") == 1024 && parseGpuMemoryLimit("100") == 512 &&
                                  parseGpuMemoryLimit("99999") == 4096 && parseGpuMemoryLimit("1300") == 1280 &&
                                  parseGpuMemoryLimit("1450") == 1536);
        check("gpu: junk -> 1280", parseGpuMemoryLimit("") == 1280 && parseGpuMemoryLimit("lots") == 1280 &&
                                   parseGpuMemoryLimit("1 GB") == 1280 && parseGpuMemoryLimit("-7") == 1280 &&
                                   parseGpuMemoryLimit("0") == 1280);
        bool roundTrip = true, listed = false;
        for (int mib : tak::kGpuMemoryLimits) {
            roundTrip = roundTrip && parseGpuMemoryLimit(gpuMemoryLimitValue(mib)) == mib;
            listed = listed || mib == tak::kGpuMemoryDefaultMiB;
        }
        check("gpu: every choice round-trips", roundTrip);
        check("gpu: default is listed", listed);
        check("gpu: written forms", gpuMemoryLimitValue(1280) == "1280" && gpuMemoryLimitValue(0) == "1280" &&
                                    gpuMemoryLimitValue(-1) == "unlimited" && gpuMemoryLimitValue(768) == "768");
        check("gpu: labels", gpuMemoryLimitLabel(1280) == "1.25 GB (default)" && gpuMemoryLimitLabel(768) == "768 MB" &&
                             gpuMemoryLimitLabel(1024) == "1 GB" && gpuMemoryLimitLabel(1536) == "1.5 GB" &&
                             gpuMemoryLimitLabel(3072) == "3 GB" && gpuMemoryLimitLabel(-1) == "Unlimited");
        bool noAuto = true;
        for (int mib : tak::kGpuMemoryLimits)
            noAuto = noAuto && gpuMemoryLimitLabel(mib).find("Auto") == std::string::npos;
        check("gpu: no Auto entry", noAuto);
    }

#ifdef TAK_SETTINGS_FILE_TEST
    // A fresh profile is one with no settings file: loadSettings says whether it found one
    // (the first-run rule keys on it), and an empty file is a profile all the same.
    {
        auto check = [&](const char* what, bool ok) {
            std::printf("  %-22s %s\n", what, ok ? "ok" : "FAIL");
            if (!ok) ++g_fail;
        };
        const auto path = std::filesystem::temp_directory_path() /
                          ("settings_test_" + std::to_string(std::random_device{}()) + ".ini");
        tak::setSettingsPath(path.string());
        bool found = true;
        const Settings none = tak::loadSettings(&found);
        check("fresh: no file", !found && none == Settings{});
        { std::ofstream(path) << "uiScale = 1.5\n"; }
        check("fresh: file found", tak::loadSettings(&found).uiScale == 1.5f && found);
        { std::ofstream(path, std::ios::trunc); }
        check("fresh: empty file found", tak::loadSettings(&found) == Settings{} && found);
        // Campaign progress is campaigndone.<id> = the completed mission indices; a
        // campaign.<id> count is not a key the game reads.
        { std::ofstream(path) << "campaigndone.aramon = 0,2\ncampaign.taros = 3\n"; }
        const Settings camp = tak::loadSettings();
        check("campaign: done read", camp.campaignCompleted.size() == 1 &&
                                     camp.campaignCompleted.at("aramon") == std::set<int>{0, 2});
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    // SOUND VOICES: 128 by default (retail's pool is 32), saved on its own key, clamped to
    // 8..256; EVEN OUT SOUND LEVELS on by default.
    {
        auto check = [&](const char* what, bool ok) {
            std::printf("  %-22s %s\n", what, ok ? "ok" : "FAIL");
            if (!ok) ++g_fail;
        };
        const auto path = std::filesystem::temp_directory_path() /
                          ("settings_test_voices_" + std::to_string(std::random_device{}()) + ".ini");
        tak::setSettingsPath(path.string());
        auto voicesFrom = [&](const std::string& body) {
            { std::ofstream(path) << body; }
            return tak::loadSettings().soundVoices;
        };
        check("voices: default 128", tak::Settings{}.soundVoices == 128 && voicesFrom("masterVol = 7\n") == 128);
        Settings saved;
        saved.soundVoices = 96;
        check("voices: save", tak::saveSettings(saved));
        check("voices: 96 kept", tak::loadSettings().soundVoices == 96);
        check("voices: 2 -> 8", voicesFrom("soundVoices = 2\n") == 8);
        check("voices: 999 -> 256", voicesFrom("soundVoices = 999\n") == 256);
        check("voices: 256 kept", voicesFrom("soundVoices = 256\n") == 256);
        check("even: default on", tak::Settings{}.evenSoundLevels);
        saved.evenSoundLevels = false;
        check("even: save", tak::saveSettings(saved));
        check("even: off kept", !tak::loadSettings().evenSoundLevels);
        // CUSTOM MUSIC: each track's file (any path: spaces, quotes, UTF-8); none by default.
        check("music: default original", tak::Settings{}.customMusicTracks.empty());
        saved.customMusicTracks[15] = "/home/a b/It's \xc3\xa9t\xc3\xa9.mp3";
        saved.customMusicTracks[2] = "C:\\Music\\theme.flac";
        check("music: save", tak::saveSettings(saved));
        const Settings back = tak::loadSettings();
        check("music: kept", back.customMusicTracks == saved.customMusicTracks);
        check("music: bad hex ignored", [&] {
            { std::ofstream(path) << "customMusic.track4 = zz\ncustomMusic.track0 = 41\n"; }
            return tak::loadSettings().customMusicTracks.empty();
        }());
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    // The Benchmark's PATHFINDING pick round-trips through the file on its own key, leaves
    // the game-create pick alone, and anything but a valid mode byte loads as Retail.
    {
        using Mode = tak::sim::PathfindingMode;
        auto check = [&](const char* what, bool ok) {
            std::printf("  %-22s %s\n", what, ok ? "ok" : "FAIL");
            if (!ok) ++g_fail;
        };
        const auto path = std::filesystem::temp_directory_path() /
                          ("settings_test_bench_" + std::to_string(std::random_device{}()) + ".ini");
        tak::setSettingsPath(path.string());
        auto modeFrom = [&](const std::string& body) {
            { std::ofstream(path) << body; }
            return tak::loadSettings().benchmarkPathfinding;
        };
        check("benchPath: default", tak::Settings{}.benchmarkPathfinding == Mode::Retail);
        check("benchSpeed: default +10", tak::Settings{}.benchmarkSpeed == 10);
        { { std::ofstream(path) << "benchmarkSpeed = 0\n"; } check("benchSpeed: 0 is NORMAL", tak::loadSettings().benchmarkSpeed == 0); }
        { { std::ofstream(path) << "benchmarkSpeed = 7\n"; } check("benchSpeed: 7 -> +10", tak::loadSettings().benchmarkSpeed == 10); }
        { { std::ofstream(path) << "benchmarkSpeed = 100\n"; } check("benchSpeed: 100 is +100", tak::loadSettings().benchmarkSpeed == 100); }
        { { std::ofstream(path) << "benchmarkSpeed = 999\n"; } check("benchSpeed: 999 -> +100", tak::loadSettings().benchmarkSpeed == 100); }
        { { std::ofstream(path) << "benchmarkSpeed = -3\n"; } check("benchSpeed: -3 -> NORMAL", tak::loadSettings().benchmarkSpeed == 0); }
        {   // up to 64 remembered servers load, most recent first; the rest are dropped
            std::ofstream o(path);
            o << "knownServers = ";
            for (int i = 0; i < 70; ++i) o << (i ? "," : "") << "host" << i << ":7677";
            o << "\n";
        }
        {
            const auto ks = tak::loadSettings().knownServers;
            check("servers: 64 of 70 kept", ks.size() == Settings::kMaxKnownServers && ks.front() == "host0:7677"
                                            && ks.back() == "host63:7677");
        }
        check("benchPath: absent", modeFrom("masterVol = 7\n") == Mode::Retail);
        Settings saved;
        saved.benchmarkPathfinding = Mode::Legion;
        check("benchPath: save", tak::saveSettings(saved));
        const Settings loaded = tak::loadSettings();
        check("benchPath: Legion kept", loaded.benchmarkPathfinding == Mode::Legion);
        check("benchPath: lobby alone", loaded.gameCreate.pathfindingMode == Mode::Retail);
        check("benchPath: 4 is Legion", modeFrom("benchmarkPathfinding = 4\n") == Mode::Legion);
        check("benchPath: 0 is Retail", modeFrom("benchmarkPathfinding = 0\n") == Mode::Retail);
        check("benchPath: 3 -> Retail", modeFrom("benchmarkPathfinding = 3\n") == Mode::Retail);
        check("benchPath: 44 -> Retail", modeFrom("benchmarkPathfinding = 44\n") == Mode::Retail);
        check("benchPath: junk -> Retail", modeFrom("benchmarkPathfinding = legion\n") == Mode::Retail);
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    // MAX FPS: 30..240, and 0 means unlimited; a cap outside the range clamps to it, and
    // unlimited survives a save.
    {
        auto fpsFrom = [&](const std::string& body) {
            const auto path = std::filesystem::temp_directory_path() /
                              ("settings_test_fps_" + std::to_string(std::random_device{}()) + ".ini");
            { std::ofstream(path) << body; }
            tak::setSettingsPath(path.string());
            const int v = tak::loadSettings().maxFps;
            std::error_code ec;
            std::filesystem::remove(path, ec);
            return v;
        };
        auto check = [&](const char* what, bool ok) {
            std::printf("  %-22s %s\n", what, ok ? "ok" : "FAIL");
            if (!ok) ++g_fail;
        };
        check("maxFps: 144 kept", fpsFrom("maxFps = 144\n") == 144);
        check("maxFps: 10 -> 30", fpsFrom("maxFps = 10\n") == 30);
        check("maxFps: 0 unlimited", fpsFrom("maxFps = 0\n") == 0);
        check("maxFps: 480 -> 240", fpsFrom("maxFps = 480\n") == 240);
        check("maxFps: -5 unlimited", fpsFrom("maxFps = -5\n") == 0);
        Settings w{};
        w.maxFps = 0;
        check("maxFps: 0 round trip", fpsFrom(tak::serializeSettings(w)) == 0);
    }
    // The streaming panel's BITRATE: saved, reloaded, and invalid values snap to 6000.
    {
        auto loadFrom = [&](const std::string& body) {
            const auto path = std::filesystem::temp_directory_path() /
                              ("settings_test_br_" + std::to_string(std::random_device{}()) + ".ini");
            { std::ofstream(path) << body; }
            tak::setSettingsPath(path.string());
            const Settings v = tak::loadSettings();
            std::error_code ec;
            std::filesystem::remove(path, ec);
            return v;
        };
        auto bitrateFrom = [&](const std::string& b) { return loadFrom(b).streamBitrate; };
        auto check = [&](const char* what, bool ok) {
            std::printf("  %-22s %s\n", what, ok ? "ok" : "FAIL");
            if (!ok) ++g_fail;
        };
        Settings w{};
        w.streamBitrate = 45000;
        check("bitrate: round trip", bitrateFrom(tak::serializeSettings(w)) == 45000);
        check("bitrate: absent=6000", bitrateFrom("masterVol = 7\n") == 6000);
        check("bitrate: 7777 snaps", bitrateFrom("streamBitrate = 7777\n") == 6000);
        check("bitrate: 0 snaps", bitrateFrom("streamBitrate = 0\n") == 6000);
        check("bitrate: junk snaps", bitrateFrom("streamBitrate = fast\n") == 6000);
        check("bitrate: 80000 ok", bitrateFrom("streamBitrate = 80000\n") == 80000);
        // YouTube's figures (the panel's cap and RECOMMENDED) are kept too.
        check("bitrate: 17000 ok", bitrateFrom("streamBitrate = 17000\n") == 17000);
        check("bitrate: 35000 ok", bitrateFrom("streamBitrate = 35000\n") == 35000);
        // Resolution, frame rate and encoder: round trip, then invalid values snap to default.
        Settings w2{};
        w2.streamResolution = 1; w2.streamFps = 30; w2.streamCpuEncoder = true;
        const Settings r2 = loadFrom(tak::serializeSettings(w2));
        check("panel: res round trip", r2.streamResolution == 1);
        check("panel: fps round trip", r2.streamFps == 30);
        check("panel: enc round trip", r2.streamCpuEncoder);
        const Settings d0 = loadFrom("masterVol = 7\n");
        check("panel: absent=defaults", d0.streamResolution == 5 && d0.streamFps == 60 && !d0.streamCpuEncoder);
        check("res: 4 ok", loadFrom("streamResolution = 4\n").streamResolution == 4);
        check("res: 6 snaps", loadFrom("streamResolution = 6\n").streamResolution == 5);
        check("res: -1 snaps", loadFrom("streamResolution = -1\n").streamResolution == 5);
        check("res: junk snaps", loadFrom("streamResolution = big\n").streamResolution == 5);
        // NATIVE WAYLAND: saved as auto/on/off, Auto when absent or unknown.
        Settings nw{};
        nw.nativeWayland = 2;
        check("wayland: off trip", loadFrom(tak::serializeSettings(nw)).nativeWayland == 2);
        nw.nativeWayland = 1;
        check("wayland: on trip", loadFrom(tak::serializeSettings(nw)).nativeWayland == 1);
        check("wayland: absent auto", loadFrom("masterVol = 7\n").nativeWayland == 0);
        check("wayland: junk auto", loadFrom("nativeWayland = maybe\n").nativeWayland == 0);
        Settings dm{};
        dm.displayMode = 3; dm.displayScale = 1.45f; dm.displayX11Width = 7680; dm.displayLogicalWidth = 5297;
        check("display: trip", loadFrom(tak::serializeSettings(dm)).displayMode == 3 &&
                               std::abs(loadFrom(tak::serializeSettings(dm)).displayScale - 1.45f) < 0.001f);
        check("display: widths", loadFrom(tak::serializeSettings(dm)).displayX11Width == 7680 &&
                                 loadFrom(tak::serializeSettings(dm)).displayLogicalWidth == 5297 &&
                                 tak::preferenceDefaults(dm).displayX11Width == 7680 &&
                                 loadFrom("displayMode = x11\ndisplayX11Width = -3\n").displayMode == tak::wayland::kModeX11 &&
                                 loadFrom("displayX11Width = -3\n").displayX11Width == 0);
        check("display: absent", loadFrom("masterVol = 7\n").displayMode == 0 && loadFrom("masterVol = 7\n").displayScale == 0.0f);
        check("display: junk", loadFrom("displayMode = x\ndisplayScale = 99\n").displayMode == 0 &&
                               loadFrom("displayMode = x\ndisplayScale = 99\n").displayScale == 0.0f);
        check("display: kept by defaults", tak::preferenceDefaults(dm).displayMode == 3 &&
                                           tak::preferenceDefaults(dm).displayScale == 1.45f);
        // HIDDEN UNITS: off | dots | fade; no key and an unknown value load as FADE, the
        // default.
        check("hidden: default 2", Settings{}.hiddenUnits == 2);
        check("hidden: absent", loadFrom("masterVol = 7\n").hiddenUnits == 2);
        check("hidden: off", loadFrom("hiddenUnits = off\n").hiddenUnits == 0);
        check("hidden: dots", loadFrom("hiddenUnits = dots\n").hiddenUnits == 1);
        check("hidden: fade", loadFrom("hiddenUnits = fade\n").hiddenUnits == 2);
        check("hidden: shadow reads as fade", loadFrom("hiddenUnits = shadow\n").hiddenUnits == 2);
        check("hidden: outline reads as fade", loadFrom("hiddenUnits = outline\n").hiddenUnits == 2);
        check("hidden: 2 and 3 read as fade", loadFrom("hiddenUnits = 2\n").hiddenUnits == 2 &&
                                              loadFrom("hiddenUnits = 3\n").hiddenUnits == 2);
        check("hidden: junk", loadFrom("hiddenUnits = maybe\n").hiddenUnits == 2);
        for (int mode = 0; mode < 3; ++mode) {
            Settings hs{};
            hs.hiddenUnits = mode;
            check("hidden: round trip", loadFrom(tak::serializeSettings(hs)).hiddenUnits == mode);
        }
        check("hidden: saved as name", tak::serializeSettings(Settings{}).find("hiddenUnits = fade") != std::string::npos);
        // FADE OPACITY: percent 0..100, clamped; absent = 50.
        check("fade opacity: default 50", Settings{}.hiddenFadeOpacity == 50);
        check("fade opacity: absent", loadFrom("masterVol = 7\n").hiddenFadeOpacity == 50);
        check("fade opacity: read", loadFrom("hiddenFadeOpacity = 30\n").hiddenFadeOpacity == 30);
        check("fade opacity: clamped high", loadFrom("hiddenFadeOpacity = 250\n").hiddenFadeOpacity == 100);
        check("fade opacity: clamped low", loadFrom("hiddenFadeOpacity = -5\n").hiddenFadeOpacity == 0);
        for (int pct : {0, 1, 42, 65, 99, 100}) {
            Settings hs{};
            hs.hiddenFadeOpacity = pct;
            check("fade opacity: round trip", loadFrom(tak::serializeSettings(hs)).hiddenFadeOpacity == pct);
        }
        check("fps: 30 ok", loadFrom("streamFps = 30\n").streamFps == 30);
        check("fps: 45 snaps", loadFrom("streamFps = 45\n").streamFps == 60);
        check("fps: junk snaps", loadFrom("streamFps = x\n").streamFps == 60);
    }
#endif

    std::printf(g_fail ? "settings_test: %d FAILURE(S)\n" : "settings_test: all passed\n",
                g_fail);
    return g_fail ? 1 : 0;
}
