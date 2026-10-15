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
    flips("textureFilter 2", [](Settings& s) { s.textureFilter = 2; });
    flips("buildBarAlign",   [](Settings& s) { s.buildBarAlign = (s.buildBarAlign + 1) % 3; });
    flips("buildBarScale",   [](Settings& s) { s.buildBarScale += 0.25f; });
    flips("treeSway",        [](Settings& s) { s.treeSway = !s.treeSway; });
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
    flips("gameCreate.crusades", [](Settings& s) { s.gameCreate.crusades = !s.gameCreate.crusades; });
    flips("gameCreate.doubleSight", [](Settings& s) { s.gameCreate.doubleSight = !s.gameCreate.doubleSight; });
    flips("gameCreate.speedUnlock", [](Settings& s) { s.gameCreate.speedUnlock = !s.gameCreate.speedUnlock; });
    flips("gameCreate.monarchExpendable", [](Settings& s) { s.gameCreate.monarchExpendable = !s.gameCreate.monarchExpendable; });
    flips("gameCreate.randomStarts", [](Settings& s) { s.gameCreate.randomStarts = (s.gameCreate.randomStarts + 1) % 3; });
    flips("gameCreate.randomStarts 2", [](Settings& s) { s.gameCreate.randomStarts = 2; });
    flips("gameCreate.allowSpectators", [](Settings& s) { s.gameCreate.allowSpectators = !s.gameCreate.allowSpectators; });
    flips("gameCreate.generated", [](Settings& s) { s.gameCreate.generated = !s.gameCreate.generated; });
    flips("gameCreate.unitCap", [](Settings& s) { ++s.gameCreate.unitCap; });
    flips("gameCreate.fog", [](Settings& s) { ++s.gameCreate.fog; });
    flips("gameCreate.overrides", [](Settings& s) { ++s.gameCreate.overrides; });
    flips("gameCreate.mapSort", [](Settings& s) { ++s.gameCreate.mapSort; });
    flips("gameCreate.mapSortDir", [](Settings& s) { ++s.gameCreate.mapSortDir; });
    flips("gameCreate.name", [](Settings& s) { s.gameCreate.name = "test"; });
    flips("gameCreate.generator", [](Settings& s) { s.gameCreate.generator = "test"; });
    flips("hostOverridePacks", [](Settings& s){s.hostOverridePacks={"Pack A"};});
    flips("cosmeticOverridePacks", [](Settings& s){s.cosmeticOverridePacks={"Pack B"};});
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
        cur.lastMap = "Inner Circle";
        cur.gameCreate.crusades = true;
        cur.gameCreate.generator = "saved recipe";
        cur.hotkeys["selectAll"] = "ctrl+a";
        // Preferences that must be RESET.
        cur.unitShadows = false;
        cur.smoothArt = true;
        cur.videoDeblock = true;
        cur.masterVol = 7;
        cur.uiScale = 1.75f;
        cur.textureFilter = 2;
        cur.tacticalDots = true;
        cur.renderer = "opengles2";
        cur.gpuMemoryLimit = 768;

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
        keep("lastMap",           d.lastMap == cur.lastMap);
        keep("hotkeys",           d.hotkeys == cur.hotkeys);

        const Settings fresh{};
        reset("unitShadows",      d.unitShadows == fresh.unitShadows);
        reset("smoothArt",        d.smoothArt == fresh.smoothArt);
        reset("videoDeblock",     d.videoDeblock == fresh.videoDeblock);
        reset("masterVol",        d.masterVol == fresh.masterVol);
        reset("uiScale",          d.uiScale == fresh.uiScale);
        reset("textureFilter",    d.textureFilter == fresh.textureFilter && d.textureFilter == 0);
        reset("tacticalDots",     d.tacticalDots == fresh.tacticalDots && !d.tacticalDots);
        reset("renderer",         d.renderer == fresh.renderer && d.renderer.empty());
        reset("gpuMemoryLimit",   d.gpuMemoryLimit == fresh.gpuMemoryLimit && d.gpuMemoryLimit == 0);

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
        check("tf: default Off", Settings{}.textureFilter == 0 && resolveTextureFilter(-1, -1, -1, -1) == 0);
        check("tf: new key wins", resolveTextureFilter(2, 0, 0, 0) == 2 && resolveTextureFilter(0, 1, 1, 1) == 0);
        check("tf: new key clamps", resolveTextureFilter(9, -1, -1, -1) == 2);
        check("tf: bilinear 0 -> Off", resolveTextureFilter(-1, 0, 1, 1) == 0);
        check("tf: bilinear 1 -> Units", resolveTextureFilter(-1, 1, -1, -1) == 1);
        check("tf: old zoom smooth", resolveTextureFilter(-1, -1, 0, 1) == 1 && resolveTextureFilter(-1, -1, 1, 0) == 0);
        check("tf: 0.7.24 bilinear", resolveTextureFilter(-1, -1, 1, -1) == 1 && resolveTextureFilter(-1, -1, 0, -1) == 0);
    }

    // GPU MEMORY LIMIT values (parseGpuMemoryLimit / snap / write / label). AUTO is the
    // default and what any unreadable value falls back to; a hand-edited size snaps to
    // the nearest listed choice so the Options dropdown always shows what is in effect.
    {
        auto check = [&](const char* what, bool ok) {
            std::printf("  %-22s %s\n", what, ok ? "ok" : "FAIL");
            if (!ok) ++g_fail;
        };
        using tak::parseGpuMemoryLimit; using tak::gpuMemoryLimitValue; using tak::gpuMemoryLimitLabel;
        check("gpu: default Auto", Settings{}.gpuMemoryLimit == 0);
        check("gpu: words", parseGpuMemoryLimit("auto") == 0 && parseGpuMemoryLimit("AUTO") == 0 &&
                            parseGpuMemoryLimit("unlimited") == -1 && parseGpuMemoryLimit("Unlimited") == -1);
        check("gpu: listed sizes", parseGpuMemoryLimit("512") == 512 && parseGpuMemoryLimit("1536") == 1536 &&
                                   parseGpuMemoryLimit("4096") == 4096);
        check("gpu: snaps sizes", parseGpuMemoryLimit("900") == 1024 && parseGpuMemoryLimit("100") == 512 &&
                                  parseGpuMemoryLimit("99999") == 4096 && parseGpuMemoryLimit("1300") == 1536);
        check("gpu: junk -> Auto", parseGpuMemoryLimit("") == 0 && parseGpuMemoryLimit("lots") == 0 &&
                                   parseGpuMemoryLimit("1 GB") == 0 && parseGpuMemoryLimit("-7") == 0 &&
                                   parseGpuMemoryLimit("0") == 0);
        check("gpu: -1 -> Unlimited", parseGpuMemoryLimit("-1") == -1);
        bool roundTrip = true;
        for (int mib : tak::kGpuMemoryLimits) roundTrip = roundTrip && parseGpuMemoryLimit(gpuMemoryLimitValue(mib)) == mib;
        check("gpu: every choice round-trips", roundTrip);
        check("gpu: written forms", gpuMemoryLimitValue(0) == "auto" && gpuMemoryLimitValue(-1) == "unlimited" &&
                                    gpuMemoryLimitValue(768) == "768");
        check("gpu: labels", gpuMemoryLimitLabel(0) == "Auto (1.25 GB)" && gpuMemoryLimitLabel(768) == "768 MB" &&
                             gpuMemoryLimitLabel(1536) == "1.5 GB" && gpuMemoryLimitLabel(3072) == "3 GB" &&
                             gpuMemoryLimitLabel(-1) == "Unlimited");
        check("gpu: AUTO is 1.25 GiB", tak::kGpuMemoryAutoMiB == 1280);
    }

    std::printf(g_fail ? "settings_test: %d FAILURE(S)\n" : "settings_test: all passed\n",
                g_fail);
    return g_fail ? 1 : 0;
}
