#pragma once

// User-adjustable Options, persisted to a per-user config file. These are ALL
// local display / input / audio preferences -- none of them feed the deterministic
// sim (World::stateHash) or the net GameOptions, so they may differ per client and
// change live. Keep it that way: never include this from src/sim, src/net, or
// src/server. (Net-synced, hashed choices -- game speed, unit cap, crusades, FFA --
// live in GameOptions/MatchConfig, not here.)

#include <map>
#include <string>

namespace tak {

struct Settings {
    // ---- display / window ----
    bool  fullscreen = true;       // borderless-desktop fullscreen (default on)
    bool  vsync      = true;
    int   maxFps     = 60;         // frame cap when vsync is off; clamp 30..480
    float uiScale    = 1.0f;       // in-game HUD scale; 0.75..2.0 (1.0 = 100%)
    int   antiAlias  = 0;          // scene supersampling: 0=off, 2=on (2x)
    bool  lod        = true;       // distant-unit impostors (perf); on by default
    int   spriteMode = 0;          // unit sprites: 0=auto, 1=on, 2=off

    // ---- audio (0..256, matching SoundBank's internal scale) ----
    int   masterVol  = 256;        // global gain over everything
    int   bgmVol      = 90;        // background music (SoundBank music + MenuMusic)
    int   sfxVol      = 256;       // unit / world / UI sound effects
    float chanGain[8] = {1, 1, 1, 1, 1, 1, 1, 1};   // per-output-speaker trim, 0..1

    // ---- camera / input ----
    float mouseZoomSpeed  = 1.0f;  // wheel-zoom sensitivity; 0.25..4.0
    float edgeScrollSpeed = 1.0f;  // edge-scroll rate;       0.25..4.0
    bool  edgeScroll      = true;  // pan when the cursor is at a screen edge
    int   cursorScale     = 4;     // custom mouse-cursor size multiplier; 1..8 (4 = default)

    // ---- misc ----
    std::string playerName;        // default name for multiplayer
    std::string lastMap;           // last map picked in the create/SP lobby (remembered)

    // ---- campaign progress ----
    // Per-campaign count of missions COMPLETED (campaign id -> n). Mission n is the
    // next one to play / the highest unlocked; a fresh campaign is absent (== 0).
    // Keyed by Campaign::id (the lowercased camps/*.tdf stem). See src/campaign.
    std::map<std::string, int> campaignDone;
    int campaignProgress(const std::string& id) const {
        auto it = campaignDone.find(id);
        return it == campaignDone.end() ? 0 : it->second;
    }

    friend bool operator==(const Settings& a, const Settings& b) {
        for (int i = 0; i < 8; ++i) if (a.chanGain[i] != b.chanGain[i]) return false;
        return a.fullscreen == b.fullscreen && a.vsync == b.vsync && a.maxFps == b.maxFps
            && a.uiScale == b.uiScale && a.antiAlias == b.antiAlias
            && a.lod == b.lod && a.spriteMode == b.spriteMode
            && a.masterVol == b.masterVol && a.bgmVol == b.bgmVol && a.sfxVol == b.sfxVol
            && a.mouseZoomSpeed == b.mouseZoomSpeed && a.edgeScrollSpeed == b.edgeScrollSpeed
            && a.edgeScroll == b.edgeScroll && a.cursorScale == b.cursorScale
            && a.playerName == b.playerName && a.lastMap == b.lastMap
            && a.campaignDone == b.campaignDone;
    }
    friend bool operator!=(const Settings& a, const Settings& b) { return !(a == b); }
};

// The config file path (SDL_GetPrefPath based). Empty only if SDL can't provide one.
std::string settingsPath();

// Read the config file; returns defaults for anything missing/corrupt (never throws).
Settings loadSettings();

// Atomically write the config file. Returns false on I/O failure.
bool saveSettings(const Settings&);

}  // namespace tak
