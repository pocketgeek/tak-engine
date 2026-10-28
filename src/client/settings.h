#pragma once

// User-adjustable Options, persisted to a per-user config file. These are ALL
// local display / input / audio preferences, plus defaults for creating a game.
// GameCreate only seeds a new room: the authoritative rules live in
// GameOptions/MatchConfig and never read these preferences during a match.
// Never include this header from src/sim, src/net, or src/server.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <charconv>
#include <cstdlib>
#include "sim/pathmode.h"
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace tak {

// Completion index for a campaign's alternate-ending branch (it has no numbered slot).
inline constexpr int kAltMission = -2;

struct Settings {
    // ---- display / window ----
    bool  fullscreen = true;       // borderless-desktop fullscreen (default on)
    bool  vsync      = true;
    int   maxFps     = 60;         // frame cap when vsync is off: 30..240, 0 = unlimited
    std::string renderer;          // SDL render driver id ("opengl", "software", ...);
                                   // "" = AUTO, SDL's own choice. Applied at startup
                                   // only (restart required); see client/renderdriver.h.
    // Windows only (ignored elsewhere): opt the process out of Windows display scaling so
    // the window is real pixels instead of a bitmap-stretched (blurry) low-res surface.
    // Applied at startup only (restart required); see applyDpiMigration below.
    bool  ignoreDpiScaling = true;
    bool  dpiMigrated = false;     // one-time: uiScale/cursorScale already adapted to the OS scale
    // Linux only (ignored elsewhere): 0 = Auto, 1 = On, 2 = Off -- open a native Wayland
    // window (real pixels at any display scale) instead of X11 through XWayland. Applied
    // at startup only (restart required); see client/waylandchoice.h.
    int   nativeWayland = 0;
    // Linux bookkeeping, not preferences: the window the last session got
    // (tak::wayland::Mode: 0 none yet, 1 x11-real, 2 x11-stretched, 3 wayland, 4 x11 not yet
    // measured), the last native Wayland scale seen (0 = none), and what tells the two X11
    // kinds apart: the X11 desktop width an X11 window got and the Wayland logical desktop
    // width a native one saw (0 = none). A NATIVE WAYLAND switch then converts UI Scale and
    // Cursor Size once, in the right direction (tak::wayland::advanceDisplay).
    int   displayMode = 0;
    float displayScale = 0.0f;
    int   displayX11Width = 0;
    int   displayLogicalWidth = 0;
    float scorecardScale = 1.0f;   // independent F4 scorecard scale; 0.5..2.0
    float uiScale    = 1.0f;       // in-game HUD scale; 0.5..2.0 (1.0 = 100%)
    // TEXTURE FILTERING for the game area (client/texturefilter.h): 0 = Off (retail's
    // default, all nearest), 1 = Units (retail's BiLinearFilter: unit model textures
    // and unit shadows linear), 2 = Everything (Units, plus terrain, features and
    // effects once zoomed in past 1.5x; a non-retail extension). The interface is
    // never filtered by this; see Smooth GUI Art. Persisted as textureFilter.
    int   textureFilter = 2;
    int   buildBarAlign = 1;       // conjure/build icon row: 0=left, 1=center, 2=right
    float buildBarScale = 1.0f;    // extra scale on the build icon row, ON TOP of uiScale; 0.75..4.0
    // The in-game chat box (client/chatbox.h): its size, ON TOP of uiScale (0.5..3.0), and
    // where it sits over the play area: 0 = left, 1 = centered.
    float chatScale = 1.0f;
    int   chatAlign = 1;
    // UI FONT (client/uitext.h): the face of the interface's plain text. 0 = MODERN (Noto
    // Sans Bold, the default), 1 = CLASSIC (the 5x7 block font and the retail text
    // faces). Retail art lettering is the same either way. Applied live. Persisted as
    // uiFont = modern | classic; absent (a settings file from before it) = MODERN.
    int   uiFont = 0;
    bool  treeSway   = true;       // animated tree and shadow sway; default on
    bool  tacticalDots = true;     // zoomed far out, draw units as minimap-style dots
    int   tacticalDotsZoom = 0;    // when: 0 = only fully zoomed out .. 100 = from normal size
                                   // instead of models (non-retail extra; default on)
    // HIDDEN UNITS (client/hiddenunits.h; a non-retail extra): a unit behind a tree, rock or
    // building is shown anyway -- 0 = OFF (as retail), 1 = DOTS (its tactical dot on top
    // when its centre is covered), 2 = SHADOW (the cover turns see-through over the unit),
    // 3 = FADE (a tree, rock or building over a unit fades out whole; the default).
    // Persisted as hiddenUnits = off | dots | shadow | fade; absent (a settings
    // file from before it), "outline" (an older mode) or unknown = FADE.
    int   hiddenUnits = 3;
    // FADE OPACITY: how visible a faded cover stays, percent -- 100 = no fade, 0 = gone.
    // Persisted as hiddenFadeOpacity = 0..100 (clamped); absent = 65.
    int   hiddenFadeOpacity = 65;
    bool  screenShake = false;     // camera shake on heavy impacts / mission quakes; default off
    bool  videoDeblock = true;     // deblock the Bink clips before upload. They are
                                   // 640x360 and get stretched ~12x on a wide display,
                                   // which magnifies every 8x8 compression seam into a
                                   // visible line. ~0.3ms per frame against a 66ms
                                   // budget at 15fps; nothing scales with window size.
    bool  smoothArt  = true;       // edge-directed 2x upscale of the STATIC art (GUI,
                                   // panels, backdrops, cursors) when it is built, so
                                   // 1999 sprites do not show blocky stair-steps when
                                   // magnified. Costs load time and VRAM, nothing per
                                   // frame. Changing it reloads the current interface
                                   // textures immediately; no restart required.
    // GPU MEMORY LIMIT (client/gpuvram.h): the ceiling on the video memory the game's own
    // textures and buffers may use, in MiB: one of kGpuMemoryLimits, 1280 (1.25 GiB) by
    // default, or -1 = UNLIMITED (only each subsystem's own ceiling applies). Applied live.
    // Persisted as gpuMemoryLimit = <MiB> | unlimited (an older file's "auto" reads as 1280).
    int   gpuMemoryLimit = 1280;
    bool  unitShadows = true;      // projected unit shadows (retail Glide casts them; the
                                   // single largest cost in a crowded frame, so it is worth
                                   // being able to turn off on a slow machine)
    int   healthBars = 1;          // unit health bars: 0=off, 1=only when damaged, 2=always
    bool  statsPanel = true;       // fill the dead space under the minimap with a live
                                   // readout (fps, ping, unit counts, memory). The strip
                                   // between the minimap and the command panel is plain
                                   // black at every resolution; how MUCH of it there is
                                   // depends on window height and UI scale, so the panel
                                   // shows only as many rows as actually fit and draws
                                   // nothing at all when the gap is too small.

    // ---- audio (0..256, matching SoundBank's internal scale) ----
    int   masterVol  = 256;        // global gain over everything
    int   bgmVol      = 128;       // background music (SoundBank music + MenuMusic); 50%
    int   sfxVol      = 256;       // unit / world / UI sound effects
    float chanGain[8] = {1, 1, 1, 1, 1, 1, 1, 1};   // per-output-speaker trim, 0..1
    std::string audioDevice;       // output device NAME; "" = system default. Falls back
                                   // to system default if the saved name is gone at startup.

    // ---- voice chat (client/voice.h; separate from the game's audio above) ----
    // Off by default. The push-to-talk key is an ordinary rebindable action (CONTROLS,
    // Act::PushToTalk, Space), stored with the other hotkeys.
    bool  voiceEnabled = false;
    // TALK MODE: push to talk (the default), or voice activation -- the microphone sends
    // by itself while its level (after MIC VOLUME) reaches voiceActivationLevel.
    bool  voiceActivation = false;
    int   voiceActivationLevel = 15;   // 0..100, on the MIC LEVEL meter's scale
    std::string voiceMicDevice;    // capture device NAME; "" = system default
    bool  voiceOutputSameAsGame = true;   // play voice on whatever device the game uses
    std::string voiceOutputDevice; // when not same as game: device NAME, "" = system default
    int   voiceMicVolume = 100;    // microphone gain, 0..100 (100 = as captured)
    int   voiceOutputVolume = 100; // voice playback volume, 0..100, one value for all speakers

    // ---- camera / input ----
    float mouseZoomSpeed  = 1.0f;  // wheel-zoom sensitivity; 0.25..4.0
    float edgeScrollSpeed = 1.0f;  // edge-scroll rate;       0.25..4.0
    bool  edgeScroll      = true;  // pan when the cursor is at a screen edge
    int   cursorScale     = 1;     // custom mouse-cursor size multiplier; 1..8 (1 = retail size)
    bool  hardwareCursor  = true; // OS-tracked cursor: stays smooth when the game hitches
    bool  smoothMotion    = true;  // interpolate unit motion between 30Hz sim ticks (glide, not step)

    // ---- misc ----
    std::string playerName;        // default name for new local / unauthenticated multiplayer games
    std::string accountName;       // last multiplayer account signed in with. The
                                   // PASSWORD is deliberately not here and is never
                                   // written to disk -- see src/net/auth.h.
    // YouTube streaming REMEMBER KEY (client/streamkey.h): only whether the stream key is
    // kept in the OS credential store. The key itself is never in this file.
    bool streamRememberKey = false;
    // The streaming panel's SAVE A LOCAL COPY: also write the stream to videoDir().
    bool streamLocalCopy = false;
    // The streaming panel's BITRATE (Kbps). Always one of kStreamBitrates; anything else
    // read from the file snaps to the default.
    static constexpr int kStreamBitrates[] = {3000, 6000, 9000, 12000, 20000, 30000, 45000, 60000, 80000};
    static constexpr int kStreamBitrateDefault = 6000;
    int streamBitrate = kStreamBitrateDefault;
    // The panel's RESOLUTION choice, as its index: 0-3 = 720p/1080p/1440p/4K, 4 = FULL window,
    // 5 = MAX 3840 (the default); anything else read from the file snaps to the default.
    static constexpr int kStreamResolutionDefault = 5;
    int streamResolution = kStreamResolutionDefault;
    // FRAME RATE: 30 or 60 (default 60).
    int streamFps = 60;
    // ENCODER: false = AUTOMATIC (default), true = CPU (libx264).
    bool streamCpuEncoder = false;
    struct GameCreate {
        bool crusades = false, doubleSight = false, speedUnlock = false;
        bool monarchExpendable = false;
        bool allowSpectators = true;   // multiplayer ALLOW SPECTATORS (on, as every room was before it)
        bool powerCodes = false;       // POWER CODES (net::GameOptions::powerCodes), off as in retail
        bool generated = false;
        sim::PathfindingMode pathfindingMode = sim::PathfindingMode::Retail;
        int unitCap = 2000, fog = 1;
        // START LOCATIONS, net::GameOptions::randomStarts: 0 FIXED, 1 RANDOM, 2 BY SLOT.
        // Saved under the same key it had as a RANDOM on/off toggle, so 0 and 1 keep
        // their meaning.
        int randomStarts = 0;
        int mapSort = 0, mapSortDir = 1;
        std::string name = "game", generator;
        friend bool operator==(const GameCreate&, const GameCreate&) = default;
    } gameCreate;
    // Ticked mod packs, by folder name: mods/game/<pack> (applied in every non-campaign
    // room this player hosts, sent to every player) and mods/cosmetic/<pack> (every
    // player's own, never sent anywhere). Saved as mods.gamePack / mods.cosmeticPack.
    // A name an always-enabled pack (mods/<kind>/always_enabled/) also uses is kept
    // but has no effect while that pack is there. (The 0.8.13 gameCreate.gameMods
    // key, its multiplayer GAME MODS toggle, is gone: skipped on load, never saved.)
    std::vector<std::string> gameModPacks, cosmeticModPacks;
    std::string lastMap;           // last map picked in the create/SP lobby (remembered)
    // Servers that connected successfully (most recent first, capped at 8). The
    // menu's CONNECT dropdown lists these under the default server.
    std::vector<std::string> knownServers;

    // ---- data / install location ----
    std::string dataDir;           // retail install folder (picked at first run, changeable in
                                   // Options and in Cartographer's Preferences, which share it;
                                   // re-checked at launch). See client/datafolder.h.
    std::string dataManifest;      // authenticity digest of the root HPIs when last validated
                                   // (hpi::rootManifest); a mismatch re-runs validation

    // ---- hotkeys ----
    // Rebindable in-game key chords, by action id (see src/client/hotkeys). Only
    // bindings that DIFFER from the factory default are stored ("NONE" = an explicit
    // unbind); anything absent uses the default, so new defaults propagate.
    std::map<std::string, std::string> hotkeys;

    // ---- campaign progress ----
    // Which missions the player has COMPLETED, per campaign (id -> set of 0-based
    // mission indices; the alternate ending is kAltMission). Keyed by Campaign::id
    // (the lowercased camps/*.tdf stem). Missions are NEVER locked -- this only records
    // what's been beaten, for the DONE marker and the "play next" hint. See src/campaign.
    std::map<std::string, std::set<int>> campaignCompleted;
    bool missionCompleted(const std::string& id, int mission) const {
        auto it = campaignCompleted.find(id);
        return it != campaignCompleted.end() && it->second.count(mission) > 0;
    }

    // EVERY field above belongs here -- this is a complete comparison of two Settings,
    // with no field exempt. Which fields DEFAULTS should ignore is a separate question,
    // and it is answered where it belongs, in Options::atDefaults(), by copying the
    // exempt ones across before comparing.
    //
    // Leaving a field out here instead makes the DEFAULTS button go dead for anyone who
    // changed only that field, since the UI then believes they are already at defaults.
    // That shipped when unitShadows was added, and dataDir/dataManifest had the same
    // hole. tools/settings_test.cpp flips each field in turn and fails if this does not
    // notice, so the next addition is caught rather than shipped.
    friend bool operator==(const Settings& a, const Settings& b) {
        for (int i = 0; i < 8; ++i) if (a.chanGain[i] != b.chanGain[i]) return false;
        return a.fullscreen == b.fullscreen && a.vsync == b.vsync && a.maxFps == b.maxFps
            && a.scorecardScale == b.scorecardScale && a.uiScale == b.uiScale && a.textureFilter == b.textureFilter
            && a.buildBarAlign == b.buildBarAlign && a.buildBarScale == b.buildBarScale
            && a.chatScale == b.chatScale && a.chatAlign == b.chatAlign && a.uiFont == b.uiFont
            && a.treeSway == b.treeSway && a.tacticalDots == b.tacticalDots
            && a.tacticalDotsZoom == b.tacticalDotsZoom && a.hiddenUnits == b.hiddenUnits
            && a.hiddenFadeOpacity == b.hiddenFadeOpacity
            && a.screenShake == b.screenShake
            && a.unitShadows == b.unitShadows && a.smoothArt == b.smoothArt
            && a.gpuMemoryLimit == b.gpuMemoryLimit
            && a.videoDeblock == b.videoDeblock
            && a.healthBars == b.healthBars && a.statsPanel == b.statsPanel
            && a.masterVol == b.masterVol && a.bgmVol == b.bgmVol && a.sfxVol == b.sfxVol
            && a.mouseZoomSpeed == b.mouseZoomSpeed && a.edgeScrollSpeed == b.edgeScrollSpeed
            && a.edgeScroll == b.edgeScroll && a.cursorScale == b.cursorScale
            && a.hardwareCursor == b.hardwareCursor && a.smoothMotion == b.smoothMotion
            && a.playerName == b.playerName && a.accountName == b.accountName
            && a.streamRememberKey == b.streamRememberKey && a.streamLocalCopy == b.streamLocalCopy
            && a.streamBitrate == b.streamBitrate
            && a.streamResolution == b.streamResolution && a.streamFps == b.streamFps
            && a.streamCpuEncoder == b.streamCpuEncoder
            && a.gameModPacks == b.gameModPacks && a.cosmeticModPacks == b.cosmeticModPacks
            && a.lastMap == b.lastMap && a.gameCreate == b.gameCreate
            && a.dataDir == b.dataDir && a.dataManifest == b.dataManifest
            && a.knownServers == b.knownServers
            && a.audioDevice == b.audioDevice && a.renderer == b.renderer
            && a.voiceEnabled == b.voiceEnabled && a.voiceMicDevice == b.voiceMicDevice
            && a.voiceActivation == b.voiceActivation && a.voiceActivationLevel == b.voiceActivationLevel
            && a.voiceOutputSameAsGame == b.voiceOutputSameAsGame && a.voiceOutputDevice == b.voiceOutputDevice
            && a.voiceMicVolume == b.voiceMicVolume && a.voiceOutputVolume == b.voiceOutputVolume
            && a.ignoreDpiScaling == b.ignoreDpiScaling && a.dpiMigrated == b.dpiMigrated
            && a.nativeWayland == b.nativeWayland
            && a.displayMode == b.displayMode && a.displayScale == b.displayScale
            && a.displayX11Width == b.displayX11Width && a.displayLogicalWidth == b.displayLogicalWidth
            && a.hotkeys == b.hotkeys
            && a.campaignCompleted == b.campaignCompleted;
    }
    friend bool operator!=(const Settings& a, const Settings& b) { return !(a == b); }
};

// The config file path: settings.ini in the per-user folder (util/prefpath.h). Empty only
// if there is no such folder.
std::string settingsPath();
// Where replays are recorded and listed: settingsPath()'s folder, ending in a path
// separator. Empty when there is no per-user folder.
std::string replayDir();
// Where the streaming panel's local copies go: replayDir() + "videos" and a separator
// (created when the first copy starts). Empty when there is no per-user folder.
std::string videoDir();
// Point settingsPath() at another file for the rest of the process ("" = back to the
// default). Cartographer's test profile uses it so a UI test of the shared GAME DATA
// FOLDER setting can never read or write the player's real game settings -- in any
// build type, which the debug-only TAK_SETTINGS_FILE hook cannot promise.
void setSettingsPath(std::string path);

// Read the config file; returns defaults for anything missing/corrupt (never throws).
Settings loadSettings();

// Atomically write the config file. Returns false on I/O failure.
//
// The data folder is the one setting another PROGRAM writes too: Cartographer reads
// and changes the game's dataDir (client/datafolder.h). So a save from a process that
// has not itself changed dataDir/dataManifest since it last read or wrote the file
// keeps whatever the file holds for them NOW, instead of writing back its stale copy --
// otherwise changing the folder in Cartographer while the game is open would be undone
// by the game's next save of anything (an Options SAVE, a won campaign mission).
bool saveSettings(const Settings&);
// A launch-only game-mod selection (takclient --game-mod, Cartographer's TEST IN GAME):
// this run's Settings::gameModPacks holds `packs`, the room's mods for the test. While a
// save still holds exactly them, the file keeps the player's own ticks (as the file has
// them then); once the player changes the ticks, the new ticks are saved as usual and
// the launch selection is over. An empty list is a selection too (a --game-mod pack that
// is already always enabled ticks nothing for the run); nullopt ends it.
void setLaunchGameModPacks(std::optional<std::vector<std::string>> packs);
// Change ONLY the data folder in the file (read, set dataDir + dataManifest, write):
// what Options and Cartographer's Preferences call on a valid pick, so unsaved edits
// on the open Options screen are not persisted along with it.
bool saveDataFolderSetting(const std::string& folder, const std::string& manifest);
// The exact text saveSettings writes (tests inspect it).
std::string serializeSettings(const Settings&);

// What DEFAULTS means, defined ONCE.
//
// A fresh Settings, with the fields that are not preferences carried across:
//   * RECORDS of what the player has done -- campaign progress, remembered servers.
//     Resetting an options screen must never delete those.
//   * CONFIGURATION -- the data root and its manifest. Resetting preferences must not
//     send someone back to the data-dir picker.
//   * Identity and things with their own reset UI -- name, account, last map, hotkeys,
//     and the streaming panel's Remember Key (a stored key sits behind that flag; only
//     unticking it there deletes the key, so DEFAULTS must not orphan it) and its Save
//     A Local Copy, Resolution, Frame Rate, Bitrate and Encoder, which also live only on
//     that panel.
//
// This used to be two separate lists: one inline in the DEFAULTS click handler, one in
// Options::atDefaults(). They have to agree, and three times they did not -- dataDir was
// wiped by the reset, then campaignCompleted and knownServers were wiped too, each time
// because a field was added to one list and not the other. One function, both callers.
inline Settings preferenceDefaults(const Settings& cur) {
    Settings d;
    d.playerName = cur.playerName;
    d.accountName = cur.accountName;
    d.streamRememberKey = cur.streamRememberKey;
    d.streamLocalCopy = cur.streamLocalCopy;
    d.streamBitrate = cur.streamBitrate;
    d.streamResolution = cur.streamResolution;
    d.streamFps = cur.streamFps;
    d.streamCpuEncoder = cur.streamCpuEncoder;
    d.lastMap = cur.lastMap;
    d.gameCreate = cur.gameCreate;
    d.gameModPacks=cur.gameModPacks;d.cosmeticModPacks=cur.cosmeticModPacks;
    d.hotkeys = cur.hotkeys;                      // reset from the Hotkeys screen
    d.dataDir = cur.dataDir;
    d.dataManifest = cur.dataManifest;
    d.campaignCompleted = cur.campaignCompleted;
    d.knownServers = cur.knownServers;
    d.dpiMigrated = cur.dpiMigrated;              // bookkeeping, not a preference
    d.displayMode = cur.displayMode;              // likewise
    d.displayScale = cur.displayScale;
    d.displayX11Width = cur.displayX11Width;
    d.displayLogicalWidth = cur.displayLogicalWidth;
    // Voice chat has its own screen and its own DEFAULTS (voiceDefaults below).
    d.voiceEnabled = cur.voiceEnabled;
    d.voiceActivation = cur.voiceActivation;
    d.voiceActivationLevel = cur.voiceActivationLevel;
    d.voiceMicDevice = cur.voiceMicDevice;
    d.voiceOutputSameAsGame = cur.voiceOutputSameAsGame;
    d.voiceOutputDevice = cur.voiceOutputDevice;
    d.voiceMicVolume = cur.voiceMicVolume;
    d.voiceOutputVolume = cur.voiceOutputVolume;
    return d;
}

// What the VOICE CHAT screen's DEFAULTS means: the current settings with only the voice
// fields reset (voice off, push to talk, system devices, full volumes). The push-to-talk key is reset
// from CONTROLS, like every other binding.
inline Settings voiceDefaults(const Settings& cur) {
    Settings d = cur;
    const Settings fresh;
    d.voiceEnabled = fresh.voiceEnabled;
    d.voiceActivation = fresh.voiceActivation;
    d.voiceActivationLevel = fresh.voiceActivationLevel;
    d.voiceMicDevice = fresh.voiceMicDevice;
    d.voiceOutputSameAsGame = fresh.voiceOutputSameAsGame;
    d.voiceOutputDevice = fresh.voiceOutputDevice;
    d.voiceMicVolume = fresh.voiceMicVolume;
    d.voiceOutputVolume = fresh.voiceOutputVolume;
    return d;
}

// The Texture Filtering level a settings file asks for, from the keys it holds (-1 =
// absent). textureFilter wins; a file written before it existed falls back to the
// 0.7.25-0.7.28 bilinearFilter toggle (0 = Off, 1 = Units, which is exactly what that
// toggle smoothed), then to 0.7.24's bilinear toggle or the unreleased Zoom Smoothing
// choice whose Smooth mode was the same filter. Nothing at all = the default (Everything).
inline int resolveTextureFilter(int textureFilter, int bilinearFilter, int legacyBilinear,
                                int legacyZoom) {
    if (textureFilter >= 0) return std::clamp(textureFilter, 0, 2);
    if (bilinearFilter >= 0) return bilinearFilter ? 1 : 0;
    if (legacyZoom >= 0) return legacyZoom == 1 ? 1 : 0;
    if (legacyBilinear >= 0) return legacyBilinear == 1 ? 1 : 0;
    return Settings{}.textureFilter;
}

// GPU MEMORY LIMIT choices, in Options order (MiB; -1 = UNLIMITED). 1280 is the default.
inline constexpr int kGpuMemoryLimits[] = {512, 768, 1024, 1280, 1536, 2048, 3072, 4096, -1};
inline constexpr int kGpuMemoryDefaultMiB = 1280;   // gpuvram::kDefaultLimit

// Any stored value onto a listed choice: -1 stays; a number of MiB becomes the nearest
// listed size (a hand-edited 900 -> 1024, 100 -> 512); 0 (what AUTO was stored as before
// 0.8.8) and other negatives are the default.
inline int snapGpuMemoryLimit(int mib) {
    if (mib == -1) return mib;
    if (mib <= 0) return kGpuMemoryDefaultMiB;
    int best = 512;
    for (int c : kGpuMemoryLimits)
        if (c > 0 && std::abs(c - mib) < std::abs(best - mib)) best = c;
    return best;
}
// The settings-file value: "unlimited" = -1, a whole number of MiB = snapped; "auto" (the
// pre-0.8.8 default) or anything unreadable = the default. Case-insensitive.
inline int parseGpuMemoryLimit(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (v == "unlimited") return -1;
    int mib = 0;
    const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), mib);
    if (v.empty() || ec != std::errc() || end != v.data() + v.size()) return kGpuMemoryDefaultMiB;
    return snapGpuMemoryLimit(mib);
}
inline std::string gpuMemoryLimitValue(int mib) {
    mib = snapGpuMemoryLimit(mib);
    return mib < 0 ? "unlimited" : std::to_string(mib);
}
// "512 MB", "1 GB", "1.25 GB (default)", "1.5 GB", "Unlimited".
inline std::string gpuMemoryLimitLabel(int mib) {
    mib = snapGpuMemoryLimit(mib);
    if (mib < 0) return "Unlimited";
    const std::string suffix = mib == kGpuMemoryDefaultMiB ? " (default)" : "";
    if (mib < 1024) return std::to_string(mib) + " MB" + suffix;
    std::string frac = std::to_string(100 + mib % 1024 * 100 / 1024).substr(1);   // hundredths
    while (!frac.empty() && frac.back() == '0') frac.pop_back();
    return std::to_string(mib / 1024) + (frac.empty() ? "" : "." + frac) + " GB" + suffix;
}

// What the running process started with (set once in main before SDL_Init), so the
// Options row can say RESTART REQUIRED when the saved choice differs.
inline bool& dpiIgnoredAtStartup() { static bool v = false; return v; }

// First run with DPI-unaware scaling switched off (Windows, macOS): the window used to be
// stretched by the OS scale, so keep the HUD/cursor the same apparent size by folding the
// scale into uiScale/cursorScale (clamped to their ranges). Runs once (dpiMigrated);
// returns true if the settings changed and should be saved. osScale = display DPI / 96 on
// Windows, the Retina backing scale (drawable / window, normally 2) on macOS.
inline bool rescaleUi(Settings& s, float factor);
inline bool applyDpiMigration(Settings& s, float osScale) {
    if (s.dpiMigrated) return false;
    s.dpiMigrated = true;
    if (s.ignoreDpiScaling && osScale > 1.01f) rescaleUi(s, osScale);
    return true;
}

// UI Scale and Cursor Size times `factor`, clamped to their ranges (so a value at the edge
// of a range does not come back exactly after the inverse factor). Returns true when the
// factor is not 1.
inline bool rescaleUi(Settings& s, float factor) {
    if (!(factor > 0.0f) || std::abs(factor - 1.0f) < 0.005f) return false;
    s.uiScale = std::clamp(s.uiScale * factor, 0.5f, 2.0f);
    s.cursorScale = std::clamp(int(float(s.cursorScale) * factor + 0.5f), 1, 8);
    return true;
}

// macOS Retina and Linux native Wayland (client/hidpi.h). With SDL_WINDOW_ALLOW_HIGHDPI
// the window is measured in points and the renderer in pixels; backingScale = pixels per
// point, possibly fractional on Wayland (1 when they agree, which is every Windows and
// Linux X11 window here, so all of these are then identities).
inline float backingScale(int windowW, int outputW) {
    return windowW > 0 && outputW > windowW ? float(outputW) / float(windowW) : 1.0f;
}
// A pixel extent (e.g. the HUD's minimum width) as window points, rounded up so the
// window never ends up a pixel short of it.
inline int pixelsToPoints(int px, float backing) {
    if (backing <= 1.0f) return px;
    return int(float(px) / backing + 0.999f);
}
// SDL2's Cocoa cursor is sized in POINTS (one surface pixel per point), unlike the
// software cursor drawn in renderer pixels. Divide CURSOR SIZE by the backing scale so
// both paths show the cursor at the same physical size.
inline int hardwareCursorScale(int cursorScale, float backing) {
    if (backing <= 1.0f) return std::max(1, cursorScale);
    return std::max(1, int(float(cursorScale) / backing + 0.5f));
}

}  // namespace tak
