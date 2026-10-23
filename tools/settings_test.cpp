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

#include <cstdio>
#include <string>

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
    flips("dpiMigrated",     [](Settings& s) { s.dpiMigrated = !s.dpiMigrated; });
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
    flips("chanGain[0]",     [](Settings& s) { s.chanGain[0] = 0.5f; });
    flips("chanGain[7]",     [](Settings& s) { s.chanGain[7] = 0.5f; });
    flips("audioDevice",     [](Settings& s) { s.audioDevice = "some-device"; });
    flips("voiceEnabled",    [](Settings& s) { s.voiceEnabled = !s.voiceEnabled; });
    flips("voiceMicDevice",  [](Settings& s) { s.voiceMicDevice = "a-mic"; });
    flips("voiceOutputSameAsGame", [](Settings& s) { s.voiceOutputSameAsGame = !s.voiceOutputSameAsGame; });
    flips("voiceOutputDevice", [](Settings& s) { s.voiceOutputDevice = "headset"; });
    flips("voiceMicVolume",  [](Settings& s) { s.voiceMicVolume -= 11; });
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
    flips("streamLocalCopy", [](Settings& s) { s.streamLocalCopy = !s.streamLocalCopy; });
    flips("gameCreate.crusades", [](Settings& s) { s.gameCreate.crusades = !s.gameCreate.crusades; });
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
    flips("gameCreate.gameMods", [](Settings& s) { s.gameCreate.gameMods = !s.gameCreate.gameMods; });
    flips("gameCreate.mapSort", [](Settings& s) { ++s.gameCreate.mapSort; });
    flips("gameCreate.mapSortDir", [](Settings& s) { ++s.gameCreate.mapSortDir; });
    flips("gameCreate.name", [](Settings& s) { s.gameCreate.name = "test"; });
    flips("gameCreate.generator", [](Settings& s) { s.gameCreate.generator = "test"; });
    flips("gameModPacks", [](Settings& s){s.gameModPacks={"Pack A"};});
    flips("cosmeticModPacks", [](Settings& s){s.cosmeticModPacks={"Pack B"};});
    flips("lastMap",         [](Settings& s) { s.lastMap = "Inner Circle"; });
    flips("campaignAi",      [](Settings& s) { s.campaignAi = 2; });
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
        cur.lastMap = "Inner Circle";
        cur.campaignAi = 2;   // NORMAL: away from the CLASSIC default
        cur.gameCreate.crusades = true;
        cur.gameCreate.generator = "saved recipe";
        cur.hotkeys["selectAll"] = "ctrl+a";
        // Voice chat has its own screen and DEFAULTS: the Options reset leaves it alone.
        cur.voiceEnabled = true;
        cur.voiceMicDevice = "a-mic";
        cur.voiceOutputSameAsGame = false;
        cur.voiceOutputDevice = "headset";
        cur.voiceMicVolume = 40;
        cur.voiceOutputVolume = 60;
        // Preferences that must be RESET.
        cur.unitShadows = false;
        cur.smoothArt = false;
        cur.videoDeblock = false;
        cur.masterVol = 7;
        cur.uiScale = 1.75f;
        cur.textureFilter = 0;
        cur.tacticalDots = false;
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
        keep("dataDir",           d.dataDir == cur.dataDir);
        keep("dataManifest",      d.dataManifest == cur.dataManifest);
        keep("playerName",        d.playerName == cur.playerName);
        keep("accountName",       d.accountName == cur.accountName);
        keep("streamRememberKey", d.streamRememberKey);
        keep("streamLocalCopy",   d.streamLocalCopy);
        keep("lastMap",           d.lastMap == cur.lastMap);
        keep("campaign opponent", d.campaignAi == 2);
        keep("hotkeys",           d.hotkeys == cur.hotkeys);
        keep("voice chat",        d.voiceEnabled && d.voiceMicDevice == "a-mic" && !d.voiceOutputSameAsGame &&
                                  d.voiceOutputDevice == "headset" && d.voiceMicVolume == 40 &&
                                  d.voiceOutputVolume == 60);

        // ...and the VOICE CHAT screen's DEFAULTS resets exactly the voice fields: voice
        // off (the default), system devices, full volumes; everything else untouched.
        const Settings v = tak::voiceDefaults(cur);
        const Settings fresh0{};
        reset("voice (own page)", !v.voiceEnabled && !fresh0.voiceEnabled && v.voiceMicDevice.empty() &&
                                  v.voiceOutputSameAsGame && v.voiceOutputDevice.empty() &&
                                  v.voiceMicVolume == 100 && v.voiceOutputVolume == 100);
        keep("non-voice on voice DEFAULTS", v.masterVol == cur.masterVol && v.uiScale == cur.uiScale &&
                                  v.hotkeys == cur.hotkeys && v.campaignCompleted == cur.campaignCompleted);

        const Settings fresh{};
        reset("unitShadows",      d.unitShadows == fresh.unitShadows);
        reset("smoothArt",        d.smoothArt == fresh.smoothArt);
        reset("videoDeblock",     d.videoDeblock == fresh.videoDeblock);
        reset("masterVol",        d.masterVol == fresh.masterVol);
        reset("uiScale",          d.uiScale == fresh.uiScale);
        reset("textureFilter",    d.textureFilter == fresh.textureFilter && d.textureFilter == 2);
        reset("tacticalDots",     d.tacticalDots == fresh.tacticalDots && d.tacticalDots);
        reset("screenShake",      d.screenShake == fresh.screenShake && !d.screenShake);
        reset("tacticalDotsZoom", d.tacticalDotsZoom == fresh.tacticalDotsZoom && d.tacticalDotsZoom == 0);
        reset("renderer",         d.renderer == fresh.renderer && d.renderer.empty());
        reset("gpuMemoryLimit",   d.gpuMemoryLimit == fresh.gpuMemoryLimit && d.gpuMemoryLimit == 1280);
        reset("uiFont",           d.uiFont == fresh.uiFont && d.uiFont == 0);

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

    // DPI migration: once, folds the OS scale into uiScale/cursorScale (clamped), and
    // only when the option is on.
    {
        auto check = [&](const char* what, bool ok) {
            std::printf("  %-22s %s\n", what, ok ? "ok" : "FAIL");
            if (!ok) ++g_fail;
        };
        Settings s{};
        check("dpi: changed", tak::applyDpiMigration(s, 1.5f));
        check("dpi: ui 1.5", s.uiScale == 1.5f && s.cursorScale == 2 && s.dpiMigrated);
        check("dpi: only once", !tak::applyDpiMigration(s, 2.0f) && s.uiScale == 1.5f);
        Settings c{}; c.uiScale = 1.5f; c.cursorScale = 6;
        tak::applyDpiMigration(c, 3.0f);
        check("dpi: clamped", c.uiScale == 2.0f && c.cursorScale == 8);
        Settings o{}; o.ignoreDpiScaling = false;
        tak::applyDpiMigration(o, 1.5f);
        check("dpi: off untouched", o.uiScale == 1.0f && o.cursorScale == 1 && o.dpiMigrated);
        Settings h{};
        tak::applyDpiMigration(h, 1.0f);
        check("dpi: 100% untouched", h.uiScale == 1.0f && h.cursorScale == 1);
        check("dpi: defaults keep flag", tak::preferenceDefaults(s).dpiMigrated);
        // macOS Retina: points vs pixels. Every non-Retina case is an identity.
        check("dpi: backing 1x", tak::backingScale(1280, 1280) == 1.0f && tak::backingScale(0, 0) == 1.0f);
        check("dpi: backing 2x", tak::backingScale(1280, 2560) == 2.0f);
        check("dpi: points 1x", tak::pixelsToPoints(1201, 1.0f) == 1201);
        check("dpi: points 2x up", tak::pixelsToPoints(1201, 2.0f) == 601 && tak::pixelsToPoints(480, 2.0f) == 240);
        check("dpi: hw cursor 1x", tak::hardwareCursorScale(3, 1.0f) == 3 && tak::hardwareCursorScale(0, 1.0f) == 1);
        check("dpi: hw cursor 2x", tak::hardwareCursorScale(2, 2.0f) == 1 && tak::hardwareCursorScale(4, 2.0f) == 2 &&
                                   tak::hardwareCursorScale(1, 2.0f) == 1);
    }

    // Texture Filtering migration (resolveTextureFilter; -1 = key absent). The new key
    // wins; a file from before it maps the old Bilinear Filtering toggle onto Off/Units
    // (Units is exactly what that toggle smoothed), then the older 0.7.24 keys.
    {
        auto check = [&](const char* what, bool ok) {
            std::printf("  %-22s %s\n", what, ok ? "ok" : "FAIL");
            if (!ok) ++g_fail;
        };
        using tak::resolveTextureFilter;
        check("tf: default Everything", Settings{}.textureFilter == 2 && resolveTextureFilter(-1, -1, -1, -1) == 2);
        check("tf: new key wins", resolveTextureFilter(2, 0, 0, 0) == 2 && resolveTextureFilter(0, 1, 1, 1) == 0);
        check("tf: new key clamps", resolveTextureFilter(9, -1, -1, -1) == 2);
        check("tf: bilinear 0 -> Off", resolveTextureFilter(-1, 0, 1, 1) == 0);
        check("tf: bilinear 1 -> Units", resolveTextureFilter(-1, 1, -1, -1) == 1);
        check("tf: old zoom smooth", resolveTextureFilter(-1, -1, 0, 1) == 1 && resolveTextureFilter(-1, -1, 1, 0) == 0);
        check("tf: 0.7.24 bilinear", resolveTextureFilter(-1, -1, 1, -1) == 1 && resolveTextureFilter(-1, -1, 0, -1) == 0);
    }

    // GPU MEMORY LIMIT values (parseGpuMemoryLimit / snap / write / label). 1280 MiB is the
    // default and what any unreadable value falls back to -- as is "auto" (and a stored 0),
    // the pre-0.8.8 default -- and a hand-edited size snaps to the nearest listed choice so
    // the Options dropdown always shows what is in effect.
    {
        auto check = [&](const char* what, bool ok) {
            std::printf("  %-22s %s\n", what, ok ? "ok" : "FAIL");
            if (!ok) ++g_fail;
        };
        using tak::parseGpuMemoryLimit; using tak::gpuMemoryLimitValue; using tak::gpuMemoryLimitLabel;
        check("gpu: default 1280", Settings{}.gpuMemoryLimit == 1280 && tak::kGpuMemoryDefaultMiB == 1280);
        check("gpu: old auto -> 1280", parseGpuMemoryLimit("auto") == 1280 && parseGpuMemoryLimit("AUTO") == 1280 &&
                                       parseGpuMemoryLimit("0") == 1280 && tak::snapGpuMemoryLimit(0) == 1280);
        check("gpu: unlimited", parseGpuMemoryLimit("unlimited") == -1 && parseGpuMemoryLimit("Unlimited") == -1 &&
                                parseGpuMemoryLimit("-1") == -1);
        check("gpu: listed sizes", parseGpuMemoryLimit("512") == 512 && parseGpuMemoryLimit("1280") == 1280 &&
                                   parseGpuMemoryLimit("1536") == 1536 && parseGpuMemoryLimit("4096") == 4096);
        check("gpu: snaps sizes", parseGpuMemoryLimit("900") == 1024 && parseGpuMemoryLimit("100") == 512 &&
                                  parseGpuMemoryLimit("99999") == 4096 && parseGpuMemoryLimit("1300") == 1280 &&
                                  parseGpuMemoryLimit("1450") == 1536);
        check("gpu: junk -> 1280", parseGpuMemoryLimit("") == 1280 && parseGpuMemoryLimit("lots") == 1280 &&
                                   parseGpuMemoryLimit("1 GB") == 1280 && parseGpuMemoryLimit("-7") == 1280);
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

    // Campaign OPPONENT AI: CLASSIC (6) by default -- the struct, and a settings file
    // with no campaignAi key (which keeps the struct default) or an unknown value;
    // "normal" still reads NORMAL (2), and both written forms round-trip.
    {
        auto check = [&](const char* what, bool ok) {
            std::printf("  %-22s %s\n", what, ok ? "ok" : "FAIL");
            if (!ok) ++g_fail;
        };
        using tak::parseCampaignAi; using tak::campaignAiValue;
        check("campaignAi: default 6", Settings{}.campaignAi == 6);
        check("campaignAi: values", parseCampaignAi("normal") == 2 && parseCampaignAi("classic") == 6);
        check("campaignAi: junk -> 6", parseCampaignAi("") == 6 && parseCampaignAi("hard") == 6 &&
                                       parseCampaignAi("Normal ") == 6);
        check("campaignAi: written", std::string(campaignAiValue(2)) == "normal" &&
                                     std::string(campaignAiValue(6)) == "classic");
        check("campaignAi: round-trip", parseCampaignAi(campaignAiValue(2)) == 2 &&
                                        parseCampaignAi(campaignAiValue(6)) == 6);
    }

    std::printf(g_fail ? "settings_test: %d FAILURE(S)\n" : "settings_test: all passed\n",
                g_fail);
    return g_fail ? 1 : 0;
}
