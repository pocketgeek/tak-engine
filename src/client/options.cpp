#include "client/options.h"

#include "client/uitext.h"
#include "client/chatbox.h"   // wrap
#include "client/cursorpath.h"
#include "client/hidpi.h"
#include "client/datafolder.h"
#include "client/gpuvram.h"
#include "client/hotkeys.h"
#include "client/renderdriver.h"
#include "client/waylandchoice.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <memory>

namespace tak {

namespace {
// A folder path elided at its START (elidePathStart's rule: prefer a cut on a
// separator) to the most characters that fit `maxW` in the UI font.
std::string fitPath(const std::string& path, float px, float maxW) {
    for (size_t n = path.size(); n > 3; --n) {
        std::string t = elidePathStart(path, n);
        if (uiTextWidth(t, px) <= maxW) return t;
    }
    return "...";
}
}  // namespace

namespace {

// The user-chosen output device NAME ("" = system default). Set once at startup from
// Settings::audioDevice (validated), and cleared back to system default if a chosen
// device turns out to be missing or fails to open. Process-global so SoundBank and the
// menu/briefing audio all open the same device.
std::string g_audioDevice;
int g_channelCache = -1;   // detectOutputChannels() memo; -1 = recompute

// HONEST channel-layout snapshot, captured ONCE at startup before ANY audio stream opens
// (see initAudioCaps). We cannot query this live: PipeWire/PulseAudio collapse a 5.1
// sink's ADVERTISED layout to stereo while a stereo stream (e.g. the menu music) is
// playing on it, so SDL_GetDefaultAudioInfo/SDL_GetAudioDeviceSpec report 2 mid-menu even
// for a real 5.1 device. The startup snapshot (no streams yet) is the only moment they
// tell the truth, so detectOutputChannels reads from here, not from a live query.
std::map<std::string, int> g_devCaps;   // device name -> advertised channels (1..8)
int g_defaultCaps = 0;                  // system-default advertised channels (0 = unknown)
bool g_capsReady = false;

// Index of an output device by name (-1 if absent), for SDL_GetAudioDeviceSpec.
int deviceIndex(const char* name) {
    if (!name) return -1;
    int n = SDL_GetNumAudioDevices(0);
    for (int i = 0; i < n; ++i)
        if (const char* dn = SDL_GetAudioDeviceName(i, 0); dn && !std::strcmp(dn, name)) return i;
    return -1;
}

// Speaker labels match channelGains()'s per-count speaker layout.
const char* role(int count, int i) {
    switch (count) {
        case 2: { static const char* r[] = {"LEFT", "RIGHT"}; return i < 2 ? r[i] : ""; }
        case 4: { static const char* r[] = {"FRONT L", "FRONT R", "REAR L", "REAR R"}; return i < 4 ? r[i] : ""; }
        case 6: { static const char* r[] = {"FRONT L", "FRONT R", "CENTER", "SUB", "REAR L", "REAR R"}; return i < 6 ? r[i] : ""; }
        case 8: { static const char* r[] = {"FRONT L", "FRONT R", "CENTER", "SUB", "REAR L", "REAR R", "SIDE L", "SIDE R"}; return i < 8 ? r[i] : ""; }
        default: return "MONO";
    }
}

std::string pctOf(float v, float full) { return std::to_string(int(v * 100 / full + 0.5f)) + "%"; }
std::string timesFmt(float v) { char b[16]; std::snprintf(b, sizeof b, "%.2fX", v); return b; }

}  // namespace

// Snapshot every output device's HONEST channel layout, ONCE, before any stream opens.
// MUST be called at startup (see main()) ahead of the menu music / door videos -- once a
// stereo stream is live on the default 5.1 sink, PipeWire reports it as 2-channel and the
// snapshot would be wrong. Idempotent.
void initAudioCaps() {
    if (g_capsReady) return;
    g_capsReady = true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
    int n = SDL_GetNumAudioDevices(0);
    SDL_AudioSpec spec{};
    if (n != 0 && SDL_GetDefaultAudioInfo(nullptr, &spec, 0) == 0 && spec.channels >= 1)
        g_defaultCaps = std::clamp(int(spec.channels), 1, 8);
    for (int i = 0; i < n; ++i)
        if (const char* dn = SDL_GetAudioDeviceName(i, 0)) {
            SDL_AudioSpec ds{};
            if (SDL_GetAudioDeviceSpec(i, 0, &ds) == 0 && ds.channels >= 1)
                g_devCaps[dn] = std::clamp(int(ds.channels), 1, 8);
        }
    std::fprintf(stderr, "audio: capability snapshot -- default=%d, %d device(s)\n",
                 g_defaultCaps, int(g_devCaps.size()));
}

// The one true output-channel count for this process (of the chosen device, or the
// system default), so SoundBank and the Options per-speaker sliders always agree.
// Computed once and cached; recomputed after a device change. Reads the startup snapshot
// (g_devCaps / g_defaultCaps) rather than a live query, which under-reports surround
// while the menu music holds the sink in stereo.
int detectOutputChannels() {
    if (g_channelCache >= 0) return g_channelCache;
    initAudioCaps();   // no-op if already snapshotted at startup (the normal path)
    int adv = 0;
    if (g_audioDevice.empty()) {
        adv = g_defaultCaps;
    } else if (auto it = g_devCaps.find(g_audioDevice); it != g_devCaps.end()) {
        adv = it->second;
    } else if (int idx = deviceIndex(g_audioDevice.c_str()); idx >= 0) {
        // Chosen device wasn't in the startup snapshot (hot-plugged since) -- best-effort
        // live spec; may read low if a stereo stream is currently on it.
        SDL_AudioSpec spec{};
        if (SDL_GetAudioDeviceSpec(idx, 0, &spec) == 0 && spec.channels >= 1)
            adv = std::clamp(int(spec.channels), 1, 8);
    }
    if (adv <= 0) adv = 2;   // unknown -> safe stereo (never fabricate surround)
    g_channelCache = std::clamp(adv, 1, 8);
    std::fprintf(stderr, "audio: %d output channels on %s\n", g_channelCache,
                 g_audioDevice.empty() ? "system default" : g_audioDevice.c_str());
    return g_channelCache;
}

std::vector<std::string> listAudioDevices(int iscapture) {
    std::vector<std::string> out;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return out;
    int n = SDL_GetNumAudioDevices(iscapture);
    for (int i = 0; i < n; ++i)
        if (const char* dn = SDL_GetAudioDeviceName(i, iscapture)) out.emplace_back(dn);
    return out;
}

void setAudioDevice(const std::string& name) {
    // Ensure the audio subsystem is up (and the device snapshot taken) BEFORE validating:
    // at startup this runs right after SDL_Init(VIDEO), so without this SDL_GetNumAudioDevices
    // would see zero devices and EVERY saved device would falsely read as "not present".
    initAudioCaps();
    std::string prev = g_audioDevice;
    g_audioDevice.clear();
    if (!name.empty()) {
        if (deviceIndex(name.c_str()) >= 0) g_audioDevice = name;
        else std::fprintf(stderr, "audio: chosen device '%s' not present -- using system default\n",
                          name.c_str());
    }
    if (g_audioDevice != prev) g_channelCache = -1;  // recompute on next detectOutputChannels
}

const std::string& currentAudioDevice() { return g_audioDevice; }

SDL_AudioDeviceID openAudioDevice(int iscapture, const SDL_AudioSpec* want,
                                  SDL_AudioSpec* got, int allowed) {
    // WASAPI retries an absent endpoint for up to eight seconds per open. Menu
    // music, clicks and movies would each pay that delay on a soundless system.
    // Recheck on every request so plugging in/selecting a device can recover.
    if (SDL_GetNumAudioDevices(iscapture) == 0) {
        SDL_SetError("No active audio device");
        return 0;
    }
    if (!g_audioDevice.empty()) {
        SDL_AudioDeviceID d = SDL_OpenAudioDevice(g_audioDevice.c_str(), iscapture, want, got, allowed);
        if (d) return d;
        // Missing / inaccessible at open time -> permanently fall back to system default.
        std::fprintf(stderr, "audio: device '%s' failed to open -- falling back to system default\n",
                     g_audioDevice.c_str());
        g_audioDevice.clear();
        g_channelCache = -1;
    }
    return SDL_OpenAudioDevice(nullptr, iscapture, want, got, allowed);
}

OptionsScreen::OptionsScreen(SDL_Renderer* ren, Settings& s, std::function<void()> onChange,
                             std::function<void()> onSave, int audioChannels,
                             std::function<void()> onHotkeys, std::function<void()> onAudioDevice,
                             DataFolderRow dataFolder)
    : ren_(ren), s_(s), onChange_(std::move(onChange)), onSave_(std::move(onSave)),
      onHotkeys_(std::move(onHotkeys)), onAudioDevice_(std::move(onAudioDevice)),
      folder_(std::move(dataFolder)) {
    build(audioChannels > 0 ? audioChannels : detectOutputChannels());
}

OptionsScreen::OptionsScreen(SDL_Renderer* ren, Settings& s, std::function<void()> onChange,
                             std::function<void()> onSave, VoicePageHooks voice,
                             std::function<void()> onHotkeys)
    : ren_(ren), s_(s), onChange_(std::move(onChange)), onSave_(std::move(onSave)),
      onHotkeys_(std::move(onHotkeys)), page_(OptionsPage::Voice), voice_(std::move(voice)) {
    build(0);
    if (voice_.monitor) voice_.monitor(true);   // the MIC LEVEL meter runs while this is open
}

// Rebuild the speaker sliders (etc.) after a device change. Deferred to input()/render()
// so it never runs while a caller still holds a reference into ctls_.
void OptionsScreen::applyPendingRebuild() {
    if (!pendingRebuild_) return;
    pendingRebuild_ = false;
    openDrop_ = -1; drag_ = -1; dropScroll_ = 0;   // transient control state -> invalid after rebuild
    build(detectOutputChannels());
}

// VOICE CHAT: a page of its own, separate from the game's audio. One switch, then the
// microphone (device, volume, a live level meter), the voice output (SAME AS GAME OUTPUT,
// the system default or any device, and ONE volume whatever its channel count), and how
// you talk: TALK MODE push to talk (the default; the key lives in CONTROLS with the other
// bindings) or voice activation at ACTIVATION LEVEL. With voice chat off every row but
// the switch is greyed and ignores input.
void OptionsScreen::buildVoice() {
    refreshDevices();
    auto section = [&](const char* label) { ctls_.push_back({Control::Section, label, 0, 0, {}, {}, {}, {}, {}}); };
    auto on = [this] { return s_.voiceEnabled; };
    auto ptt = [this] { return s_.voiceEnabled && !s_.voiceActivation; };
    auto activation = [this] { return s_.voiceEnabled && s_.voiceActivation; };

    section("GENERAL");
    ctls_.push_back({Control::Toggle, "VOICE CHAT", 0, 1, [this] { return s_.voiceEnabled ? 1.0f : 0.0f; },
                     [this](float v) { s_.voiceEnabled = v > 0.5f; }, {}, {}, {}});
    ctls_.back().help = [] {
        return std::vector<std::string>{
            "TALK TO THE OTHER PLAYERS, BY PUSH TO TALK OR VOICE ACTIVATION. OFF BY DEFAULT.",
            "WHO HEARS YOU IS SET PER PLAYER IN THE DIPLOMACY SCREEN'S VOICE COLUMN, LIKE CHAT."};
    };
    ctls_.push_back({Control::Dropdown, "TALK MODE", 0, 1, [this] { return s_.voiceActivation ? 1.0f : 0.0f; },
                     [this](float idx) { s_.voiceActivation = idx > 0.5f; }, {}, {},
                     [] { return std::vector<std::string>{"Push to Talk", "Voice Activation"}; }});
    ctls_.back().enabled = on;
    ctls_.back().help = [] {
        return std::vector<std::string>{
            "PUSH TO TALK (THE DEFAULT) SENDS WHILE YOU HOLD THE KEY.",
            "VOICE ACTIVATION SENDS BY ITSELF WHILE THE MIC LEVEL REACHES THE ACTIVATION LEVEL."};
    };

    ctls_.push_back({Control::Button, "PUSH TO TALK", 0, 0, {}, {}, {}, [this] { if (onHotkeys_) onHotkeys_(); }, {}});
    ctls_.back().enabled = ptt;
    ctls_.back().note = [this] {
        const KeyChord c = effectiveChord(s_.hotkeys, Act::PushToTalk);
        std::string t = chordName(c);
        for (char& ch : t) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        return t;
    };
    ctls_.back().help = [] {
        return std::vector<std::string>{
            "HOLD IT TO TALK. CHANGE IT IN CONTROLS (CLICK HERE). NEVER WHILE TYPING IN A TEXT BOX."};
    };
    section("MICROPHONE");
    // A saved device that is not plugged in stays in the list (so the choice still shows)
    // and is noted; the engine meanwhile uses the system default.
    auto micValues = [this] {
        auto v = capSnapshot_;
        if (std::find(v.begin(), v.end(), s_.voiceMicDevice) == v.end()) v.push_back(s_.voiceMicDevice);
        return v;
    };
    ctls_.push_back({Control::Dropdown, "MIC DEVICE", 0, 1,
        [this, micValues] {
            auto v = micValues();
            return float(std::find(v.begin(), v.end(), s_.voiceMicDevice) - v.begin());
        },
        [this, micValues](float idx) {
            auto v = micValues();
            const size_t i = size_t(std::max(0.0f, idx));
            if (i < v.size()) s_.voiceMicDevice = v[i];
        }, {}, {},
        [micValues] {
            std::vector<std::string> labels;
            for (auto& v : micValues()) labels.push_back(v.empty() ? "System Default" : v);
            return labels;
        }});
    ctls_.back().enabled = on;
    ctls_.back().note = [this] {
        if (!s_.voiceMicDevice.empty() &&
            std::find(capSnapshot_.begin(), capSnapshot_.end(), s_.voiceMicDevice) == capSnapshot_.end())
            return std::string("NOT FOUND - USING DEFAULT");
        return voice_.micNote ? voice_.micNote() : std::string();
    };
    ctls_.push_back({Control::Slider, "MIC VOLUME", 0, 100, [this] { return float(s_.voiceMicVolume); },
                     [this](float v) { s_.voiceMicVolume = std::clamp(int(v + 0.5f), 0, 100); },
                     [](float v) { return std::to_string(int(v + 0.5f)); }, {}, {}});
    ctls_.back().enabled = on;
    ctls_.push_back({Control::Meter, "MIC LEVEL", 0, 1, [this] { return voice_.micLevel ? voice_.micLevel() : 0.0f; },
                     {}, {}, {}, {}});
    ctls_.back().enabled = on;
    ctls_.back().help = [] {
        return std::vector<std::string>{"WHAT THE MICROPHONE HEARS NOW, AFTER MIC VOLUME. SPEAK TO TEST IT.",
                                        "IN VOICE ACTIVATION THE TICK IS THE ACTIVATION LEVEL."};
    };
    ctls_.back().mark = [this] { return s_.voiceActivation ? float(s_.voiceActivationLevel) / 100.0f : -1.0f; };
    ctls_.push_back({Control::Slider, "ACTIVATION LEVEL", 0, 100, [this] { return float(s_.voiceActivationLevel); },
                     [this](float v) { s_.voiceActivationLevel = std::clamp(int(v + 0.5f), 0, 100); },
                     [](float v) { return std::to_string(int(v + 0.5f)); }, {}, {}});
    ctls_.back().enabled = activation;
    ctls_.back().help = [] {
        return std::vector<std::string>{
            "VOICE ACTIVATION: YOU ARE SENT WHILE THE MIC LEVEL REACHES THIS (THE TICK ON THE METER).",
            "RAISE IT IF NOISE SETS IT OFF, LOWER IT IF YOUR WORDS ARE CUT. 0 SENDS ALL THE TIME."};
    };

    section("OUTPUT");
    // [0] = SAME AS GAME OUTPUT (follows the game's SOUND DEVICE, the default),
    // [1] = System Default, then the output devices A-Z.
    auto outValues = [this] {
        auto v = devSnapshot_;   // [0] = "" = system default
        if (std::find(v.begin(), v.end(), s_.voiceOutputDevice) == v.end()) v.push_back(s_.voiceOutputDevice);
        return v;
    };
    ctls_.push_back({Control::Dropdown, "OUTPUT DEVICE", 0, 1,
        [this, outValues] {
            if (s_.voiceOutputSameAsGame) return 0.0f;
            auto v = outValues();
            return float(1 + (std::find(v.begin(), v.end(), s_.voiceOutputDevice) - v.begin()));
        },
        [this, outValues](float idx) {
            const int i = std::max(0, int(idx));
            if (i == 0) { s_.voiceOutputSameAsGame = true; return; }
            auto v = outValues();
            if (size_t(i - 1) >= v.size()) return;
            s_.voiceOutputSameAsGame = false;
            s_.voiceOutputDevice = v[size_t(i - 1)];
        }, {}, {},
        [outValues] {
            std::vector<std::string> labels{"Same as Game Output"};
            for (auto& v : outValues()) labels.push_back(v.empty() ? "System Default" : v);
            return labels;
        }});
    ctls_.back().enabled = on;
    ctls_.back().note = [this] {
        if (!s_.voiceOutputSameAsGame && !s_.voiceOutputDevice.empty() &&
            std::find(devSnapshot_.begin(), devSnapshot_.end(), s_.voiceOutputDevice) == devSnapshot_.end())
            return std::string("NOT FOUND - USING DEFAULT");
        return voice_.outNote ? voice_.outNote() : std::string();
    };
    ctls_.push_back({Control::Slider, "OUTPUT VOLUME", 0, 100, [this] { return float(s_.voiceOutputVolume); },
                     [this](float v) { s_.voiceOutputVolume = std::clamp(int(v + 0.5f), 0, 100); },
                     [](float v) { return std::to_string(int(v + 0.5f)); }, {}, {}});
    ctls_.back().enabled = on;
    ctls_.back().help = [] {
        return std::vector<std::string>{"ONE VOLUME FOR EVERY SPEAKER, STEREO OR SURROUND. SEPARATE FROM THE GAME'S VOLUMES."};
    };
    voiceLabelW_ = 0;
    for (const auto& c : ctls_)
        if (c.kind == Control::Dropdown || c.kind == Control::Meter) voiceLabelW_ = std::max(voiceLabelW_, uiTextWidth(c.label, 2.0f));
}

void OptionsScreen::build(int channels) {
    ctls_.clear();   // rebuildable: a live device switch re-runs this with a new channel count
    channels_ = channels;
    if (page_ == OptionsPage::Voice) { buildVoice(); return; }
    std::fprintf(stderr, "Options: %d speaker sliders\n", channels);
    auto section = [&](const char* label) { ctls_.push_back({Control::Section, label, 0, 0, {}, {}, {}, {}, {}}); };
    auto slider = [&](const char* label, float lo, float hi, std::function<float()> get,
                      std::function<void(float)> set, std::function<std::string(float)> fmt) {
        ctls_.push_back({Control::Slider, label, lo, hi, std::move(get), std::move(set), std::move(fmt), {}, {}});
    };
    auto toggle = [&](const char* label, std::function<float()> get, std::function<void(float)> set) {
        ctls_.push_back({Control::Toggle, label, 0, 1, std::move(get), std::move(set), {}, {}, {}});
    };
    // A dropdown: `options()` lists the display choices, get() is the selected index,
    // set(index) applies the choice. Rendered closed as a chip + arrow; clicking opens
    // a pop-up list.
    auto dropdown = [&](const char* label, std::function<std::vector<std::string>()> options,
                        std::function<float()> get, std::function<void(float)> set) {
        ctls_.push_back({Control::Dropdown, label, 0, 1, std::move(get), std::move(set),
                         {}, {}, std::move(options)});
    };

    section("PLAYER (NEW GAMES)");
    ctls_.push_back({Control::PlayerName, "PLAYER NAME", 0, 0, {}, {}, {}, {}, {}});

    // GAME DATA: the retail install this launch reads, shared with Cartographer. A
    // change is saved at once and applied by restarting (client/datafolder.h); the row
    // is greyed wherever that is unsafe or would not take.
    section("GAME DATA");
    ctls_.push_back({Control::Folder, "DATA FOLDER", 0, 0, {}, {}, {}, {}, {}});
    ctls_.back().enabled = [this] { return folder_.locked.empty(); };
    ctls_.back().help = [this] {
        std::vector<std::string> out = {"IN USE: " + folder_.inUse};
        if (dataFolderNeedsRestart(s_.dataDir, folder_.inUse)) out.push_back("> AFTER A RESTART: " + s_.dataDir);
        out.push_back(folder_.locked.empty()
            ? "THE TOTAL ANNIHILATION: KINGDOMS INSTALL THE GAME READS. CARTOGRAPHER USES THE SAME ONE. "
              "CLICK TO CHOOSE ANOTHER. THE GAME RESTARTS TO LOAD IT."
            : folder_.locked);
        return out;
    };

    section("AUDIO");
    // Output device: "System Default" plus every current output device. On startup a
    // saved-but-missing device auto-falls-back to system (see setAudioDevice); here the
    // user picks one from the list.
    refreshDevices();                                // one A-Z snapshot; re-taken on each open
    auto deviceValues = [this] { return devSnapshot_; };   // stored values ("" = system default)
    dropdown("SOUND DEVICE",
        [deviceValues] {                             // display labels
            std::vector<std::string> labels;
            for (auto& v : deviceValues()) labels.push_back(v.empty() ? "System Default" : v);
            return labels;
        },
        [this, deviceValues] {                       // selected index (0 = system default)
            auto v = deviceValues();
            for (size_t k = 0; k < v.size(); ++k) if (v[k] == s_.audioDevice) return float(k);
            return 0.0f;
        },
        [this, deviceValues](float idx) {            // apply
            auto v = deviceValues();
            size_t i = size_t(idx < 0 ? 0 : idx);
            std::string chosen = i < v.size() ? v[i] : std::string();
            if (chosen == s_.audioDevice) return;    // same device -> nothing to switch
            s_.audioDevice = chosen;
            setAudioDevice(s_.audioDevice);          // resets the channel-count cache
            pendingRebuild_ = true;                  // re-lay the SPEAKER sliders for the new count
            if (onAudioDevice_) onAudioDevice_();     // host re-opens its streams on the new device
            if (onChange_) onChange_();               // re-apply volumes to the re-opened device
        });
    slider("MASTER VOLUME", 0, 256, [&] { return float(s_.masterVol); },
           [&](float v) { s_.masterVol = int(v + 0.5f); }, [](float v) { return pctOf(v, 256); });
    slider("MUSIC VOLUME", 0, 256, [&] { return float(s_.bgmVol); },
           [&](float v) { s_.bgmVol = int(v + 0.5f); }, [](float v) { return pctOf(v, 256); });
    slider("SOUND EFFECTS", 0, 256, [&] { return float(s_.sfxVol); },
           [&](float v) { s_.sfxVol = int(v + 0.5f); }, [](float v) { return pctOf(v, 256); });
    for (int i = 0; i < channels && i < 8; ++i) {
        std::string label = std::string("SPEAKER ") + role(channels, i);
        slider(label.c_str(), 0, 1, [this, i] { return s_.chanGain[i]; },
               [this, i](float v) { s_.chanGain[i] = v; }, [](float v) { return pctOf(v, 1); });
    }
    // Voice chat keeps its own page and settings; this row is the way there.
    if (showVoice_) {
        ctls_.push_back({Control::Button, "VOICE CHAT", 0, 0, {}, {}, {}, [this] { voiceRequested_ = true; }, {}});
        ctls_.back().note = [this] { return std::string(s_.voiceEnabled ? "ON - OPEN" : "OFF - OPEN"); };
    }

    // DISPLAY: the window / output device itself.
    section("DISPLAY");
    toggle("FULLSCREEN", [&] { return s_.fullscreen ? 1.0f : 0.0f; },
           [&](float v) { s_.fullscreen = v > 0.5f; });
    toggle("VSYNC", [&] { return s_.vsync ? 1.0f : 0.0f; }, [&](float v) { s_.vsync = v > 0.5f; });
    // 30..240, and the stretch past 240 is UNLIMITED (stored as 0).
    slider("MAX FPS", 30, 250, [&] { return s_.maxFps > 0 ? float(s_.maxFps) : 250.0f; },
           [&](float v) { s_.maxFps = v > 240.5f ? 0 : int(v + 0.5f); },
           [](float v) { return v > 240.5f ? std::string("UNLIMITED") : std::to_string(int(v + 0.5f)); });
    ctls_.back().enabled = [&] { return !s_.vsync; };   // vsync paces frames; the cap does nothing
#if defined(__linux__)
    // NATIVE WAYLAND: a Wayland window in real pixels instead of X11 through XWayland,
    // which most Wayland desktops stretch (blurry) above 100% scale. AUTO = on in a
    // Wayland session, except GNOME without libdecor (which draws the title bar there).
    // Applied at
    // startup only (client/waylandchoice.h).
    dropdown("NATIVE WAYLAND",
        [] { return std::vector<std::string>{"Auto", "On", "Off"}; },
        [this] { return float(std::clamp(s_.nativeWayland, 0, 2)); },
        [this](float idx) { s_.nativeWayland = std::clamp(int(idx + 0.5f), 0, 2); });
    ctls_.back().note = [this] {
        return s_.nativeWayland != wayland::startup().setting ? std::string("RESTART REQUIRED") : std::string();
    };
    ctls_.back().help = [this] {
        const auto& wl = wayland::startup();
        std::string now = "NOW: ";
        if (wl.native) {
            char buf[48];
            std::snprintf(buf, sizeof buf, "NATIVE WAYLAND, %.2fX DISPLAY SCALE", hidpi::scale(ren_));
            now += buf;
        } else if (wl.requested) now += "X11 (WAYLAND COULD NOT START)";
        else now += "X11";
        return std::vector<std::string>{now,
            "AUTO: NATIVE WAYLAND ON A WAYLAND DESKTOP, EXCEPT GNOME WITHOUT LIBDECOR (NO TITLE BAR). "
            "ON: WAYLAND WHENEVER IT IS THERE. OFF: ALWAYS X11. RESTART TO APPLY."};
    };
#endif

    // GRAPHICS. TEXTURE FILTERING's UNITS level is retail's video option (BiLinearFilter).
    section("GRAPHICS");
    // RENDERER: AUTO (Windows: OpenGL, else Direct3D 11, else Direct3D 9; elsewhere SDL's
    // own pick; shown as "AUTO (OPENGL)" etc. while it is running) or any driver this
    // SDL build reports. Applied at startup only, so the row says RESTART REQUIRED when
    // the pick differs from what is running (or UNAVAILABLE when startup already had to
    // fall back from it).
    {
        auto values = std::make_shared<std::vector<std::string>>(1, std::string());
        for (auto& id : renderdriver::available()) values->push_back(id);
        if (std::find(values->begin(), values->end(), s_.renderer) == values->end())
            values->push_back(s_.renderer);          // a saved id this build lacks
        dropdown("RENDERER",
            [values] {
                std::vector<std::string> labels;
                const auto& a = renderdriver::active();
                for (auto& v : *values)
                    labels.push_back(v.empty() ? renderdriver::autoLabel(a.inEffect.empty(), a.created)
                                               : renderdriver::label(v));
                return labels;
            },
            [this, values] {
                auto it = std::find(values->begin(), values->end(), s_.renderer);
                return it == values->end() ? 0.0f : float(it - values->begin());
            },
            [this, values](float idx) {
                size_t i = size_t(idx < 0 ? 0 : idx);
                if (i < values->size()) s_.renderer = (*values)[i];
            });
        ctls_.back().note = [this] {
            const auto& a = renderdriver::active();
            if (!a.failed.empty() && s_.renderer == a.failed) return std::string("UNAVAILABLE");
            return s_.renderer != a.inEffect ? std::string("RESTART REQUIRED") : std::string();
        };
    }
#if defined(_WIN32) || defined(__APPLE__)
    // Windows scales DPI-unaware programs by the display scale setting (a blurry stretch);
    // macOS does the same to a window that does not ask for its Retina backing (2x).
    // ON opts out at startup: real pixels, HUD sized via UI SCALE instead.
#ifdef _WIN32
    const char* dpiLabel = "IGNORE WINDOWS DISPLAY SCALING";
#else
    const char* dpiLabel = "NATIVE RETINA RESOLUTION";
#endif
    toggle(dpiLabel, [&] { return s_.ignoreDpiScaling ? 1.0f : 0.0f; },
           [&](float v) { s_.ignoreDpiScaling = v > 0.5f; });
    ctls_.back().note = [this] {
        return s_.ignoreDpiScaling != dpiIgnoredAtStartup() ? std::string("RESTART REQUIRED") : std::string();
    };
#endif
    // Three stops, game area only (client/texturefilter.h); applied live like the rest.
    slider("TEXTURE FILTERING", 0, 2, [&] { return float(s_.textureFilter); },
           [&](float v) { s_.textureFilter = std::clamp(int(v + 0.5f), 0, 2); },
           [](float v) { int l = int(v + 0.5f); return std::string(l >= 2 ? "EVERYTHING" : l >= 1 ? "UNITS" : "OFF"); });
    ctls_.back().help = [&] {
        const char* lines[] = {
            "OFF: NOTHING IS SMOOTHED, AS IN THE ORIGINAL GAME.",
            "UNITS: SMOOTHS UNIT TEXTURES AND UNIT SHADOWS (THE ORIGINAL GAME'S BILINEAR FILTER).",
            "EVERYTHING: UNITS, PLUS TERRAIN, TREES, ROCKS AND EFFECTS ONCE ZOOMED IN PAST 1.5X."};
        std::vector<std::string> out;
        for (int i = 0; i < 3; ++i) out.push_back(std::string(i == s_.textureFilter ? "> " : "  ") + lines[i]);
        return out;
    };
    // Projected unit shadows. Retail's Glide path casts these, so ON is the faithful
    // setting -- but it is the largest single cost in a crowded frame (measured ~3.2ms
    // of a ~12ms draw at ~1180 visible units), which is worth a switch on a slow machine.
    toggle("SHADOWS", [&] { return s_.unitShadows ? 1.0f : 0.0f; },
           [&](float v) { s_.unitShadows = v > 0.5f; });

    toggle("SMOOTH GUI ART", [&] { return s_.smoothArt ? 1.0f : 0.0f; },
           [&](float v) { s_.smoothArt = v > 0.5f; });
    toggle("SMOOTH MOVIES", [&] { return s_.videoDeblock ? 1.0f : 0.0f; },
           [&](float v) { s_.videoDeblock = v > 0.5f; });
    toggle("TREES SWAY IN WIND", [&] { return s_.treeSway ? 1.0f : 0.0f; },
           [&](float v) { s_.treeSway = v > 0.5f; });
    // Zoomed far out, draw every unit as a minimap-style player-coloured dot instead
    // of its model (not in retail). Fog and selection behave exactly as for models.
    toggle("TACTICAL DOTS", [&] { return s_.tacticalDots ? 1.0f : 0.0f; },
           [&](float v) { s_.tacticalDots = v > 0.5f; });
    // When dots take over, as a position along the zoom-out range: 0% = only fully
    // zoomed out, 100% = from normal size outwards (client/tacticaldots.h).
    slider("TACTICAL DOTS ZOOM", 0, 100, [&] { return float(s_.tacticalDotsZoom); },
           [&](float v) { s_.tacticalDotsZoom = std::clamp(int(v + 0.5f), 0, 100); },
           [](float v) { return pctOf(v, 100); });
    ctls_.back().enabled = [&] { return s_.tacticalDots; };   // only meaningful with dots on
    // Camera shake on heavy impacts and mission quakes; off unless asked for.
    toggle("SCREEN SHAKE", [&] { return s_.screenShake ? 1.0f : 0.0f; },
           [&](float v) { s_.screenShake = v > 0.5f; });
    // OS-tracked cursor: the pointer keeps moving smoothly even when a heavy frame
    // stalls the render loop (the retail Direct3D "hardware cursor" option).
    toggle("HARDWARE CURSOR", [&] { return s_.hardwareCursor ? 1.0f : 0.0f; },
           [&](float v) { s_.hardwareCursor = v > 0.5f; });
    // Which path actually runs: the OS cursor and its size, or the platform's refusal.
    ctls_.back().note = [this] { return hardwareCursorNote(s_.hardwareCursor, hardwareCursorStatus()); };
    // Glide units between the 30Hz sim ticks instead of stepping them -- smooth motion
    // above 30fps, at the cost of ~one tick (33ms) of visual latency.
    toggle("SMOOTH MOTION", [&] { return s_.smoothMotion ? 1.0f : 0.0f; },
           [&](float v) { s_.smoothMotion = v > 0.5f; });
    // GPU MEMORY LIMIT: the ceiling on the game's own texture/buffer memory (gpuvram.h),
    // applied live by applyRuntimeSettings. The note flags a temporary post-failure
    // tightening below the chosen limit.
    dropdown("GPU MEMORY LIMIT",
        [] {
            std::vector<std::string> labels;
            for (int mib : kGpuMemoryLimits) labels.push_back(gpuMemoryLimitLabel(mib));
            return labels;
        },
        [this] {
            const int cur = snapGpuMemoryLimit(s_.gpuMemoryLimit);
            for (size_t i = 0; i < std::size(kGpuMemoryLimits); ++i)
                if (kGpuMemoryLimits[i] == cur) return float(i);
            return 0.0f;
        },
        [this](float idx) {
            const size_t i = size_t(std::clamp(int(idx), 0, int(std::size(kGpuMemoryLimits)) - 1));
            s_.gpuMemoryLimit = kGpuMemoryLimits[i];
        });
    ctls_.back().note = [] { return gpuvram::tightened() ? std::string("LOWERED FOR NOW") : std::string(); };
    ctls_.back().help = [] {
        std::vector<std::string> out = {
            "THE MOST VIDEO MEMORY THE GAME'S OWN TEXTURES MAY USE. 1.25 GB IS THE DEFAULT. TAKES EFFECT AT ONCE.",
            "LOWER: LESS OF THE MAP IS KEPT READY OFF-SCREEN, SO AFTER A PAN OR A MINIMAP JUMP THE "
            "TERRAIN CAN LOOK BLURRY FOR A MOMENT BEFORE IT SHARPENS, AND BIG BATTLES MAY DRAW A "
            "LITTLE SLOWER WITH SLIGHTLY FLATTER BUILDING SHADING. UNITS ARE NEVER HIDDEN.",
            "HIGHER OR UNLIMITED: ONLY HELPS IF YOUR CARD HAS MEMORY TO SPARE. NOTHING CHANGES "
            "UNTIL THE GAME ACTUALLY NEEDS MORE."};
        char used[96];
        if (gpuvram::cap() == gpuvram::kUnlimited)
            std::snprintf(used, sizeof used, "IN USE NOW: %zu MB.", gpuvram::bytes() >> 20);
        else
            std::snprintf(used, sizeof used, "IN USE NOW: %zu MB OF %zu MB%s.", gpuvram::bytes() >> 20,
                          gpuvram::cap() >> 20, gpuvram::tightened() ? " (LOWERED AFTER A GPU ERROR)" : "");
        out.push_back(used);
        return out;
    };

    // INTERFACE: on-screen HUD -- its size, and the game overlays.
    section("INTERFACE");
    // UI FONT: the face of every plain interface text -- menus, rooms, Options, HUD
    // readouts, briefings, the scoreboard. MODERN is Noto Sans Bold (the chat box's
    // face); CLASSIC is the block font and retail text faces the game always had. The
    // retail art lettering (big messages, Unit Info labels, the doors) stays either way.
    slider("UI FONT", 0, 1, [&] { return float(s_.uiFont); },
           [&](float v) { s_.uiFont = std::clamp(int(v + 0.5f), 0, 1); },
           [](float v) { return std::string(v >= 0.5f ? "CLASSIC" : "MODERN"); });
    slider("F4 SCORECARD SCALE", 0.5f, 2.0f, [&] { return s_.scorecardScale; },
           [&](float v) { s_.scorecardScale = v; }, [](float v) { return pctOf(v, 1); });
    slider("UI SCALE", 0.5f, 2.0f, [&] { return s_.uiScale; },
           [&](float v) { s_.uiScale = v; }, [](float v) { return pctOf(v, 1); });
    slider("HEALTH BARS", 0, 2, [&] { return float(s_.healthBars); },
           [&](float v) { s_.healthBars = std::clamp(int(v + 0.5f), 0, 2); },
           [](float v) { int l = int(v + 0.5f); return std::string(l >= 2 ? "ALWAYS" : l >= 1 ? "DAMAGED" : "OFF"); });
    // The right-hand strip between the minimap and the command panel is dead black
    // space at every resolution. This fills it with a live readout. It is sized to the
    // gap rather than assuming one: on a short window, or at a large UI SCALE, there is
    // room for only the first row or two, and for none at all below that -- so the panel
    // drops rows from the bottom of its priority list and disappears entirely rather
    // than overlapping the command panel.
    toggle("STATS PANEL", [&] { return s_.statsPanel ? 1.0f : 0.0f; },
           [&](float v) { s_.statsPanel = v > 0.5f; });
    // Where the in-game conjure/build icon row sits along the bottom of the screen.
    slider("BUILD MENU", 0, 2, [&] { return float(s_.buildBarAlign); },
           [&](float v) { s_.buildBarAlign = std::clamp(int(v + 0.5f), 0, 2); },
           [](float v) { int l = int(v + 0.5f); return std::string(l >= 2 ? "RIGHT" : l >= 1 ? "CENTER" : "LEFT"); });
    // Extra scale for the build icon row, applied ON TOP of UI SCALE (same range).
    slider("BUILD MENU SCALE", 0.75f, 4.0f, [&] { return s_.buildBarScale; },
           [&](float v) { s_.buildBarScale = v; }, [](float v) { return pctOf(v, 1); });
    // The in-game chat box: where it sits over the play area, and its size ON TOP of
    // UI SCALE, in 10% steps (client/chatbox.h).
    slider("CHAT POSITION", 0, 1, [&] { return float(s_.chatAlign); },
           [&](float v) { s_.chatAlign = std::clamp(int(v + 0.5f), 0, 1); },
           [](float v) { return std::string(v >= 0.5f ? "CENTERED" : "LEFT"); });
    slider("CHAT SCALE", 0.5f, 3.0f, [&] { return s_.chatScale; },
           [&](float v) { s_.chatScale = std::clamp(std::round(v * 10) / 10, 0.5f, 3.0f); },
           [](float v) { return pctOf(std::round(v * 10) / 10, 1); });

    section("CAMERA");
    slider("MOUSE ZOOM SPEED", 0.25f, 4.0f, [&] { return s_.mouseZoomSpeed; },
           [&](float v) { s_.mouseZoomSpeed = v; }, [](float v) { return timesFmt(v); });
    toggle("EDGE SCROLLING", [&] { return s_.edgeScroll ? 1.0f : 0.0f; },
           [&](float v) { s_.edgeScroll = v > 0.5f; });
    slider("EDGE SCROLL SPEED", 0.25f, 4.0f, [&] { return s_.edgeScrollSpeed; },
           [&](float v) { s_.edgeScrollSpeed = v; }, [](float v) { return timesFmt(v); });
    // Mouse-cursor size as an 8-stop slider: 1X..8X.
    slider("CURSOR SIZE", 1, 8, [&] { return float(s_.cursorScale); },
           [&](float v) { s_.cursorScale = int(v + 0.5f); },
           [](float v) { return std::to_string(int(v + 0.5f)) + "X"; });

    // (Hotkey rebinding moved OUT of Options: it's now a CONTROLS entry in the settings
    // menu -- the in-game Esc menu and the title screen's menu button. See mainmenu.cpp
    // and the in-game GAME MENU. onHotkeys_ is retained for compatibility but unused here.)
    // SAVE / BACK are drawn as a fixed footer (see layout()/render()), not list rows.
}

void OptionsScreen::layout(int winW, int winH) {
    u_ = std::clamp(std::min(winW / 1280.0f, winH / 720.0f), 1.0f, 3.0f);
    auto rowH = [&](Control::Kind k) {
        return k == Control::Section ? 30 * u_ : k == Control::Folder ? 56 * u_ : 36 * u_;
    };
    float panelW = std::min(660 * u_, winW * 0.72f);
    float titleH = 3.4f * 7 * u_ + 26 * u_;
    float footerH = 52 * u_;    // SAVE / BACK buttons, fixed at the panel bottom
    contentH_ = 0;
    for (auto& c : ctls_) contentH_ += rowH(c.kind);
    float viewH = std::min(contentH_, winH * 0.92f - titleH - footerH);
    float panelH = titleH + viewH + footerH;
    panel_ = {(winW - panelW) / 2, (winH - panelH) / 2, panelW, panelH};

    float maxScroll = std::max(0.0f, contentH_ - viewH);
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll);

    float vy = panel_.y + titleH;    // viewport top
    float y = vy - scroll_;
    for (auto& c : ctls_) {
        float h = rowH(c.kind);
        c.row = {panel_.x + 26 * u_, y, panelW - 52 * u_, h};
        y += h;
    }
    float bw = 150 * u_, bh = 32 * u_, gap = 18 * u_;
    float total = 3 * bw + 2 * gap;
    float x0 = panel_.x + (panel_.w - total) / 2;
    float by = panel_.y + panel_.h - footerH + (footerH - bh) / 2;
    defaultsRect_ = {x0, by, bw, bh};
    saveRect_ = {x0 + bw + gap, by, bw, bh};
    backRect_ = {x0 + 2 * (bw + gap), by, bw, bh};
}

void OptionsScreen::commit(Control& c, float mx) {
    float frac = std::clamp((mx - c.row.x) / std::max(1.0f, c.row.w), 0.0f, 1.0f);
    float v = c.lo + frac * (c.hi - c.lo);
    if (c.set) c.set(v);
    dirty_ = true;
    if (onChange_) onChange_();
}

void OptionsScreen::refreshDevices() {
    auto devs = listAudioDevices();
    std::sort(devs.begin(), devs.end(),                  // stable A-Z (case-insensitive)
              [](const std::string& a, const std::string& b) {
                  return std::lexicographical_compare(
                      a.begin(), a.end(), b.begin(), b.end(),
                      [](unsigned char x, unsigned char y) {
                          return std::tolower(x) < std::tolower(y);
                      });
              });
    devSnapshot_.assign(1, std::string());               // [0] = "" = system default (always first)
    for (auto& d : devs) devSnapshot_.push_back(d);
    if (page_ != OptionsPage::Voice) return;
    auto caps = listAudioDevices(1);
    std::sort(caps.begin(), caps.end(), [](const std::string& a, const std::string& b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
            [](unsigned char x, unsigned char y) { return std::tolower(x) < std::tolower(y); });
    });
    capSnapshot_.assign(1, std::string());
    for (auto& d : caps) capSnapshot_.push_back(d);
}

void OptionsScreen::dropViewport(const Control& c, int nOpts, float& y0, float& itemH,
                                 float& viewH) {
    itemH = 24 * u_;
    float footerH = 52 * u_, titleH = 3.4f * 7 * u_ + 26 * u_;
    float chipTop = c.row.y + 5 * u_, chipBot = chipTop + 24 * u_;
    float maxBottom = panel_.y + panel_.h - footerH - 6 * u_;   // keep clear of the footer
    float minTop = panel_.y + titleH + 6 * u_;                  // ...and of the title
    float fullH = itemH * float(std::max(nOpts, 1));
    // Below the closed chip when the whole list fits there; otherwise ABOVE it if that
    // side has more room (a chip low in the panel would hide its last options under the
    // footer). Whole rows only, so no option is ever shown half cut off.
    float below = maxBottom - chipBot, above = chipTop - minTop;
    bool up = fullH > below && above > below;
    float room = std::max(itemH, std::floor((up ? above : below) / itemH) * itemH);
    viewH = std::min(room, fullH);                       // at least one row, at most the list
    y0 = up ? chipTop - viewH : chipBot;
    float maxScroll = std::max(0.0f, fullH - viewH);
    dropScroll_ = std::clamp(dropScroll_, 0.0f, maxScroll);
}

void OptionsScreen::appendPlayerName(const std::string& text) {
    std::string value = nameSelectAll_ ? std::string() : s_.playerName;
    for (unsigned char c : text) if (c >= 32 && c != 127) value += char(c);
    char name[33];
    SDL_utf8strlcpy(name, value.c_str(), sizeof(name));
    if (s_.playerName != name) { s_.playerName = name; dirty_ = true; }
    nameSelectAll_ = false;
}

bool OptionsScreen::input(const SDL_Event& e, int winW, int winH) {
    applyPendingRebuild();   // fold in a pending device-change rebuild before this event
    layout(winW, winH);
    pollFolder();
    if (restartPrompt_) return restartPromptInput(e);
    // The folder dialog is up (in its own window): nothing here may change underneath it.
    if (folderPick_.busy() && (e.type == SDL_KEYDOWN || e.type == SDL_MOUSEBUTTONDOWN ||
                               e.type == SDL_MOUSEWHEEL || e.type == SDL_TEXTINPUT)) return false;
    if (nameEditing_) {
        if (e.type == SDL_TEXTINPUT) { appendPlayerName(e.text.text); return false; }
        if (e.type == SDL_KEYDOWN) {
            const auto key = e.key.keysym.sym;
            const bool shortcut = (e.key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0;
            if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_TAB) {
                nameEditing_ = false; SDL_StopTextInput();
            } else if (shortcut && key == SDLK_a) nameSelectAll_ = true;
            else if (shortcut && key == SDLK_v) {
                if (char* text = SDL_GetClipboardText()) { appendPlayerName(text); SDL_free(text); }
            } else if (key == SDLK_BACKSPACE || (key == SDLK_DELETE && nameSelectAll_)) {
                if (nameSelectAll_) { s_.playerName.clear(); nameSelectAll_ = false; dirty_ = true; }
                else if (!s_.playerName.empty()) {
                    size_t pos = s_.playerName.size()-1;
                    while (pos && (static_cast<unsigned char>(s_.playerName[pos]) & 0xc0) == 0x80) --pos;
                    s_.playerName.resize(pos); dirty_ = true;
                }
            }
            return false;
        }
    }
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        if (openDrop_ >= 0) { openDrop_ = -1; return false; }   // close the dropdown, not the screen
        return true;
    }
    if (e.type == SDL_MOUSEWHEEL) {
        // While a dropdown is open, the wheel scrolls ITS list, not the panel underneath --
        // otherwise the panel slides and the pop-up runs out from under the cursor.
        if (openDrop_ >= 0 && openDrop_ < int(ctls_.size()) &&
            ctls_[size_t(openDrop_)].kind == Control::Dropdown) {
            const Control& c = ctls_[size_t(openDrop_)];
            int n = c.options ? int(c.options().size()) : 0;
            float y0, itemH, viewH;
            dropScroll_ -= e.wheel.y * 42 * u_;
            dropViewport(c, n, y0, itemH, viewH);        // clamps dropScroll_
            return false;
        }
        scroll_ -= e.wheel.y * 42 * u_;
        return false;
    }

    auto in = [](const SDL_FRect& r, float mx, float my) {
        return mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h;
    };
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) {
        if (openDrop_ >= 0) { openDrop_ = -1; return false; }
        const float mx = float(e.button.x), my = float(e.button.y);
        const float titleH = 3.4f * 7 * u_ + 26 * u_, footerH = 52 * u_;
        if (my < panel_.y + titleH || my > panel_.y + panel_.h - footerH) return false;
        for (auto& c : ctls_) {
            if (!in(c.row, mx, my) || c.kind != Control::Toggle) continue;
            if (c.enabled && !c.enabled()) break;   // greyed out: ignore
            c.set(c.get() > 0.5f ? 0.0f : 1.0f);
            dirty_ = true;
            if (onChange_) onChange_();
            break;
        }
        return false;
    }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        if (nameEditing_) { nameEditing_ = false; SDL_StopTextInput(); }
        float bmx = float(e.button.x), bmy = float(e.button.y);
        // An open dropdown grabs the click: an option selects + closes; the dropdown's
        // own chip toggles it shut; anywhere else just closes it (then falls through so
        // the click still lands on whatever is under it).
        if (openDrop_ >= 0 && openDrop_ < int(ctls_.size()) &&
            ctls_[size_t(openDrop_)].kind == Control::Dropdown) {
            Control& c = ctls_[size_t(openDrop_)];
            auto opts = c.options ? c.options() : std::vector<std::string>{};
            float cw = dropWidth(c), y0, itemH, viewH;
            dropViewport(c, int(opts.size()), y0, itemH, viewH);
            float x = c.row.x + c.row.w - cw;
            // Only clicks inside the scrolled viewport count (items scroll under a clip).
            if (bmx >= x && bmx <= x + cw && bmy >= y0 && bmy <= y0 + viewH) {
                int i = int((bmy - y0 + dropScroll_) / itemH);
                if (i >= 0 && i < int(opts.size())) {
                    if (c.set) c.set(float(i));
                    dirty_ = true; openDrop_ = -1;
                    return false;
                }
            }
            openDrop_ = -1;
            if (in(c.row, bmx, bmy)) return false;   // clicked its own chip -> just close
        }
        float mx = float(e.button.x), my = float(e.button.y);
        if (in(backRect_, mx, my)) return true;                     // close, no save
        if (in(saveRect_, mx, my)) {                                // persist (if dirty)
            if (dirty_ && onSave_) { onSave_(); dirty_ = false; saved_ = true; }
            return false;
        }
        if (in(defaultsRect_, mx, my)) {                            // reset to defaults
            if (atDefaults()) return false;         // already default -> disabled, ignore
            // One definition per page, shared with atDefaults().
            s_ = page_ == OptionsPage::Voice ? tak::voiceDefaults(s_) : tak::preferenceDefaults(s_);
            dirty_ = true;
            if (onChange_) onChange_();
            return false;
        }
        float titleH = 3.4f * 7 * u_ + 26 * u_, footerH = 52 * u_;  // scrollable viewport
        if (my < panel_.y + titleH || my > panel_.y + panel_.h - footerH) return false;
        for (size_t i = 0; i < ctls_.size(); ++i) {
            Control& c = ctls_[i];
            if (!in(c.row, mx, my)) continue;
            if (c.enabled && !c.enabled() && c.kind != Control::Folder) return false;   // greyed out: ignore
            if (c.kind == Control::PlayerName) {
                nameEditing_ = true; nameSelectAll_ = true; SDL_StartTextInput();
                SDL_Rect caret=hidpi::toPoints(ren_,{int(c.row.x),int(c.row.y),int(c.row.w),int(c.row.h)});
                SDL_SetTextInputRect(&caret);
                return false;
            }
            if (c.kind == Control::Toggle) {
                c.set(c.get() > 0.5f ? 0.0f : 1.0f); dirty_ = true; if (onChange_) onChange_(); return false;
            }
            if (c.kind == Control::Slider) { drag_ = int(i); commit(c, mx); return false; }
            if (c.kind == Control::Button) {
                if (c.action) c.action();
                return voiceRequested_;   // VOICE CHAT: leave, so the host opens that page
            }
            if (c.kind == Control::Folder) { clickFolder(); return false; }
            if (c.kind == Control::Dropdown) { refreshDevices(); openDrop_ = int(i); dropScroll_ = 0; return false; }   // fresh A-Z list
        }
        return false;
    }
    if (e.type == SDL_MOUSEMOTION) { hoverX_ = float(e.motion.x); hoverY_ = float(e.motion.y); }
    if (e.type == SDL_MOUSEMOTION && drag_ >= 0 && (e.motion.state & SDL_BUTTON_LMASK)) {
        layout(winW, winH);   // rows may have shifted if window resized mid-drag
        if (drag_ < int(ctls_.size()) && (!ctls_[size_t(drag_)].enabled || ctls_[size_t(drag_)].enabled()))
            commit(ctls_[size_t(drag_)], float(e.motion.x));
        return false;
    }
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) drag_ = -1;
    return false;
}

void OptionsScreen::drawHelp(const Control& c) {
    // Word-wrapped to the row width (measured in the UI font, so it fits in either
    // style); continuation lines are indented. Below the row, or above it when the
    // footer is in the way. Lines starting "> " mark the current choice.
    const float px = 1.4f * u_, pad = 6 * u_, lineH = 10 * px;
    const float maxW = c.row.w - 2 * pad, indent = uiTextWidth("    ", px);
    auto width = [px](const std::string& t) { return uiTextWidth(t, px); };
    std::vector<std::pair<std::string, bool>> lines;
    for (const std::string& text : c.help()) {
        const bool mark = text.rfind("> ", 0) == 0;
        const auto rows = chatbox::wrap(text, maxW, maxW - indent, width);
        for (size_t i = 0; i < rows.size(); ++i) lines.push_back({(i ? "    " : "") + rows[i], mark});
    }
    const float h = float(lines.size()) * lineH + 2 * pad - (lineH - 7 * px);
    const float footerTop = panel_.y + panel_.h - 52 * u_;
    float y = c.row.y + c.row.h + 2 * u_;
    if (y + h > footerTop) y = c.row.y - h - 2 * u_;
    SDL_FRect box{c.row.x, y, c.row.w, h};
    SDL_SetRenderDrawColor(ren_, 14, 16, 24, 250); SDL_RenderFillRectF(ren_, &box);
    SDL_SetRenderDrawColor(ren_, 150, 160, 195, 255); SDL_RenderDrawRectF(ren_, &box);
    for (size_t i = 0; i < lines.size(); ++i)
        uiText(ren_, lines[i].first, box.x + pad, box.y + pad + float(i) * lineH, px,
                      lines[i].second ? SDL_Color{235, 225, 180, 255} : SDL_Color{185, 192, 210, 255});
}

void OptionsScreen::render(int winW, int winH) {
    applyPendingRebuild();   // in case the change came between input() and this frame
    layout(winW, winH);
    pollFolder();            // a folder pick finishes while the player is not touching anything
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);

    // Dim the whole screen, then the panel.
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 175);
    SDL_FRect dim{0, 0, float(winW), float(winH)}; SDL_RenderFillRectF(ren_, &dim);
    SDL_SetRenderDrawColor(ren_, 24, 26, 34, 245); SDL_RenderFillRectF(ren_, &panel_);
    SDL_SetRenderDrawColor(ren_, 120, 130, 160, 255); SDL_RenderDrawRectF(ren_, &panel_);

    const char* title = page_ == OptionsPage::Voice ? "VOICE CHAT" : "OPTIONS";
    float tpx = 3.4f * u_;
    uiText(ren_, title, panel_.x + (panel_.w - uiTextWidth(title, tpx)) / 2,
                  panel_.y + 13 * u_, tpx, {235, 225, 180, 255});

    // Clip the scrollable region to between the title and the footer so rows don't
    // spill onto the footer buttons.
    float titleH = 3.4f * 7 * u_ + 26 * u_, footerH = 52 * u_;
    SDL_Rect clip{int(panel_.x), int(panel_.y + titleH), int(panel_.w),
                  int(panel_.h - titleH - footerH)};
    SDL_RenderSetClipRect(ren_, &clip);

    float fpx = 2.0f * u_;
    for (auto& c : ctls_) {
        if (c.row.y + c.row.h < clip.y || c.row.y > clip.y + clip.h) continue;   // off-screen
        if (c.kind == Control::Section) {
            // Caps end 7 * 2.2 = 15.4 below y, the divider 3 px above the row's bottom:
            // y + 9 leaves a clear gap (descenders included) above it.
            uiText(ren_, c.label, c.row.x, c.row.y + 9 * u_, 2.2f * u_, {150, 200, 235, 255});
            SDL_SetRenderDrawColor(ren_, 70, 78, 96, 255);
            SDL_FRect ln{c.row.x, c.row.y + c.row.h - 3 * u_, c.row.w, 1.5f * u_};
            SDL_RenderFillRectF(ren_, &ln);
        } else if (c.kind == Control::Toggle) {
            bool on = c.get() > 0.5f;
            const bool live = !c.enabled || c.enabled();   // greyed when its master switch is off
            uiText(ren_, c.label, c.row.x, c.row.y + 12 * u_, fpx,
                          live ? SDL_Color{225, 230, 240, 255} : SDL_Color{105, 110, 122, 255});
            SDL_FRect pill{c.row.x + c.row.w - 78 * u_, c.row.y + 7 * u_, 78 * u_, 20 * u_};
            if (live) SDL_SetRenderDrawColor(ren_, on ? 60 : 44, on ? 120 : 48, on ? 90 : 58, 255);
            else SDL_SetRenderDrawColor(ren_, 34, 37, 45, 255);
            SDL_RenderFillRectF(ren_, &pill);
            if (live) SDL_SetRenderDrawColor(ren_, 130, 140, 170, 255);
            else SDL_SetRenderDrawColor(ren_, 70, 76, 92, 255);
            SDL_RenderDrawRectF(ren_, &pill);
            if (c.note && live) {   // e.g. HARDWARE CURSOR's "OS CURSOR 64X100": left of the pill
                const std::string n = c.note();
                const float npx = 1.3f * u_, nx = pill.x - 10 * u_ - uiTextWidth(n, npx);
                if (!n.empty() && nx > c.row.x + uiTextWidth(c.label, fpx) + 8 * u_)
                    uiText(ren_, n, nx, pill.y + (pill.h - 7 * npx) / 2, npx,
                           n.starts_with("UNAVAILABLE") ? SDL_Color{240, 190, 90, 255}
                                                        : SDL_Color{150, 160, 175, 255});
            }
            const char* t = on ? "ON" : "OFF";
            uiText(ren_, t, pill.x + (pill.w - uiTextWidth(t, 2.0f * u_)) / 2,
                          pill.y + (pill.h - 7 * 2.0f * u_) / 2, 2.0f * u_,
                          !live ? SDL_Color{105, 110, 122, 255}
                          : on ? SDL_Color{210, 240, 215, 255} : SDL_Color{170, 175, 185, 255});
        } else if (c.kind == Control::Meter) {
            // A live level bar (the VOICE CHAT microphone): green, amber near the top.
            const bool live = !c.enabled || c.enabled();
            uiText(ren_, c.label, c.row.x, c.row.y + 12 * u_, fpx,
                          live ? SDL_Color{225, 230, 240, 255} : SDL_Color{105, 110, 122, 255});
            const float cw = dropWidth(c);
            SDL_FRect track{c.row.x + c.row.w - cw, c.row.y + 11 * u_, cw, 12 * u_};
            SDL_SetRenderDrawColor(ren_, live ? 40 : 30, live ? 44 : 33, live ? 58 : 42, 255);
            SDL_RenderFillRectF(ren_, &track);
            if (live) {
                const float level = std::clamp(c.get ? c.get() : 0.0f, 0.0f, 1.0f);
                SDL_FRect fill{track.x, track.y, track.w * level, track.h};
                if (level > 0.9f) SDL_SetRenderDrawColor(ren_, 225, 120, 80, 255);
                else if (level > 0.6f) SDL_SetRenderDrawColor(ren_, 225, 195, 90, 255);
                else SDL_SetRenderDrawColor(ren_, 90, 190, 120, 255);
                SDL_RenderFillRectF(ren_, &fill);
            }
            SDL_SetRenderDrawColor(ren_, live ? 130 : 70, live ? 140 : 76, live ? 170 : 92, 255);
            SDL_RenderDrawRectF(ren_, &track);
            if (const float m = c.mark ? c.mark() : -1.0f; live && m >= 0) {
                const float tx = track.x + track.w * std::clamp(m, 0.0f, 1.0f);
                SDL_FRect tick{tx - 1 * u_, track.y - 3 * u_, 2 * u_, track.h + 6 * u_};
                SDL_SetRenderDrawColor(ren_, 235, 240, 250, 255);
                SDL_RenderFillRectF(ren_, &tick);
            }
        } else if (c.kind == Control::PlayerName) {
            uiText(ren_, c.label, c.row.x, c.row.y + 12*u_, fpx, {225,230,240,255});
            const float cw = dropWidth(c);
            SDL_FRect field{c.row.x+c.row.w-cw,c.row.y+5*u_,cw,24*u_};
            SDL_SetRenderDrawColor(ren_,nameEditing_ && nameSelectAll_ ? 65 : 35,55,80,255);
            SDL_RenderFillRectF(ren_, &field);
            SDL_SetRenderDrawColor(ren_,nameEditing_ ? 220 : 130,180,170,255);
            SDL_RenderDrawRectF(ren_, &field);
            std::string text = s_.playerName.empty() && !nameEditing_ ? "player" : s_.playerName;
            if (nameEditing_ && !nameSelectAll_) text += "_";
            const float px = std::min(1.6f*u_, (field.w-12*u_)/std::max(1.0f,uiTextWidth(text,1)));
            uiText(ren_,text,field.x+6*u_,field.y+(field.h-7*px)/2,px,{225,230,240,255});
        } else if (c.kind == Control::Folder) {
            drawFolderRow(c);
        } else if (c.kind == Control::Button) {
            const bool live = !c.enabled || c.enabled();
            uiText(ren_, c.label, c.row.x, c.row.y + 12 * u_, fpx,
                          live ? SDL_Color{225, 230, 240, 255} : SDL_Color{105, 110, 122, 255});
            const std::string t = c.note ? c.note() : std::string("OPEN >");
            const float tw = uiTextWidth(t, 2.0f * u_);
            const float bw = std::max(96 * u_, tw + 20 * u_);
            SDL_FRect chip{c.row.x + c.row.w - bw, c.row.y + 5 * u_, bw, 24 * u_};
            SDL_SetRenderDrawColor(ren_, live ? 52 : 34, live ? 60 : 38, live ? 82 : 46, 255);
            SDL_RenderFillRectF(ren_, &chip);
            SDL_SetRenderDrawColor(ren_, live ? 130 : 70, live ? 140 : 76, live ? 170 : 92, 255);
            SDL_RenderDrawRectF(ren_, &chip);
            uiText(ren_, t, chip.x + (chip.w - tw) / 2,
                          chip.y + (chip.h - 7 * 2.0f * u_) / 2, 2.0f * u_,
                          live ? SDL_Color{215, 225, 245, 255} : SDL_Color{110, 115, 125, 255});
        } else if (c.kind == Control::Dropdown) {
            // Closed state: label on the left, a wide chip with the current value and a
            // downward arrow on the right. The open pop-up list is drawn in a second
            // pass below (un-clipped, on top of everything).
            const bool live = !c.enabled || c.enabled();
            uiText(ren_, c.label, c.row.x, c.row.y + 12 * u_, fpx,
                          live ? SDL_Color{225, 230, 240, 255} : SDL_Color{105, 110, 122, 255});
            float cw = dropWidth(c);
            SDL_FRect chip{c.row.x + c.row.w - cw, c.row.y + 5 * u_, cw, 24 * u_};

            SDL_SetRenderDrawColor(ren_, live ? 52 : 34, live ? 60 : 38, live ? 82 : 46, 255);
            SDL_RenderFillRectF(ren_, &chip);
            SDL_SetRenderDrawColor(ren_, live ? 130 : 70, live ? 140 : 76, live ? 170 : 92, 255);
            SDL_RenderDrawRectF(ren_, &chip);
            auto opts = c.options ? c.options() : std::vector<std::string>{};
            int sel = int(c.get ? c.get() : 0.0f);
            std::string t = (sel >= 0 && sel < int(opts.size())) ? opts[size_t(sel)] : std::string();
            float dpx = 1.6f * u_;
            t = uiTextFit(t, dpx, chip.w - 30 * u_);   // leave room for the arrow
            uiText(ren_, t, chip.x + 6 * u_, chip.y + (chip.h - 7 * dpx) / 2, dpx,
                          live ? SDL_Color{215, 225, 245, 255} : SDL_Color{110, 115, 125, 255});
            if (c.note && live) {   // e.g. RESTART REQUIRED: small, amber, right-aligned before the caret
                std::string n = c.note();
                float npx = 1.3f * u_, nw = uiTextWidth(n, npx);
                float nx = chip.x + chip.w - 26 * u_ - nw;
                if (!n.empty() && nx > chip.x + 6 * u_ + uiTextWidth(t, dpx) + 8 * u_)
                    uiText(ren_, n, nx, chip.y + (chip.h - 7 * npx) / 2, npx,
                                  {240, 190, 90, 255});
            }
            // Down-caret: a filled triangle at the chip's right edge (a real glyph, not "V").
            float aw = 9 * u_, ah = 5 * u_;
            float cx = chip.x + chip.w - 13 * u_, ty = chip.y + (chip.h - ah) / 2;
            SDL_SetRenderDrawColor(ren_, 170, 180, 210, 255);
            for (float r = 0; r < ah; r += 1) {
                float half = (aw / 2) * (1 - r / ah);
                SDL_FRect span{cx - half, ty + r, 2 * half, 1};
                SDL_RenderFillRectF(ren_, &span);
            }
        } else if (c.kind == Control::Slider) {
            float val = c.get();
            const bool on = !c.enabled || c.enabled();   // greyed out when its toggle is off
            // y + 2: the caps end 2u above the handle's top (y + 18), descenders short of it.
            uiText(ren_, c.label, c.row.x, c.row.y + 2 * u_, fpx,
                          on ? SDL_Color{225, 230, 240, 255} : SDL_Color{105, 110, 122, 255});
            if (c.fmt) {
                std::string vs = c.fmt(val);
                uiText(ren_, vs, c.row.x + c.row.w - uiTextWidth(vs, fpx),
                              c.row.y + 2 * u_, fpx,
                              on ? SDL_Color{150, 195, 235, 255} : SDL_Color{90, 100, 115, 255});
            }
            // track + fill + handle
            float ty = c.row.y + 22 * u_, th = 7 * u_;
            SDL_FRect track{c.row.x, ty, c.row.w, th};
            SDL_SetRenderDrawColor(ren_, 48, 52, 66, 255); SDL_RenderFillRectF(ren_, &track);
            float frac = std::clamp((val - c.lo) / std::max(0.0001f, c.hi - c.lo), 0.0f, 1.0f);
            SDL_FRect fill{c.row.x, ty, c.row.w * frac, th};
            if (on) SDL_SetRenderDrawColor(ren_, 80, 140, 225, 255);
            else SDL_SetRenderDrawColor(ren_, 70, 76, 92, 255);
            SDL_RenderFillRectF(ren_, &fill);
            SDL_FRect handle{c.row.x + c.row.w * frac - 4 * u_, ty - 4 * u_, 8 * u_, th + 8 * u_};
            if (on) SDL_SetRenderDrawColor(ren_, 205, 215, 240, 255);
            else SDL_SetRenderDrawColor(ren_, 95, 100, 115, 255);
            SDL_RenderFillRectF(ren_, &handle);
        }
    }
    SDL_RenderSetClipRect(ren_, nullptr);

    // Help for the row under the pointer (or the slider being dragged), unless a
    // dropdown's list is open over it.
    if (openDrop_ < 0) {
        const bool inView = hoverY_ >= float(clip.y) && hoverY_ <= float(clip.y + clip.h);
        for (size_t i = 0; i < ctls_.size(); ++i) {
            const Control& c = ctls_[i];
            const bool hot = drag_ == int(i) || (inView && hoverX_ >= c.row.x && hoverX_ <= c.row.x + c.row.w &&
                                                 hoverY_ >= c.row.y && hoverY_ <= c.row.y + c.row.h);
            if (hot && c.help) { drawHelp(c); break; }
        }
    }

    // Scrollbar hint (viewport is the panel minus the title and the footer).
    float viewH = panel_.h - titleH - footerH;
    if (contentH_ > viewH) {
        float th = viewH * viewH / contentH_;
        float ty = panel_.y + titleH + (scroll_ / (contentH_ - viewH)) * (viewH - th);
        SDL_SetRenderDrawColor(ren_, 110, 120, 150, 220);
        SDL_FRect bar{panel_.x + panel_.w - 5 * u_, ty, 3 * u_, th}; SDL_RenderFillRectF(ren_, &bar);
    }

    // Footer buttons. BACK is always active; SAVE only when there are unsaved changes
    // (dirty_), and DEFAULTS only when the settings aren't already at their defaults --
    // so each button's enabled state shows whether it would do anything.
    auto button = [&](const SDL_FRect& r, const char* label, bool on) {
        SDL_SetRenderDrawColor(ren_, on ? 60 : 34, on ? 66 : 38, on ? 86 : 46, 255);
        SDL_RenderFillRectF(ren_, &r);
        SDL_SetRenderDrawColor(ren_, on ? 130 : 70, on ? 140 : 76, on ? 170 : 92, 255);
        SDL_RenderDrawRectF(ren_, &r);
        float bpx = 2.2f * u_;
        uiText(ren_, label, r.x + (r.w - uiTextWidth(label, bpx)) / 2,
                      r.y + (r.h - 7 * bpx) / 2, bpx,
                      on ? SDL_Color{228, 232, 242, 255} : SDL_Color{110, 115, 125, 255});
    };
    button(defaultsRect_, "DEFAULTS", !atDefaults());   // disabled when already default
    button(saveRect_, "SAVE", dirty_);
    button(backRect_, "BACK", true);

    // Open dropdown pop-up: drawn LAST so it sits above every control and the footer.
    if (openDrop_ >= 0 && openDrop_ < int(ctls_.size()) &&
        ctls_[size_t(openDrop_)].kind == Control::Dropdown) {
        const Control& c = ctls_[size_t(openDrop_)];
        auto opts = c.options ? c.options() : std::vector<std::string>{};
        int sel = int(c.get ? c.get() : 0.0f);
        float cw = dropWidth(c), dpx = 1.6f * u_, y0, itemH, viewH;
        dropViewport(c, int(opts.size()), y0, itemH, viewH);
        float x = c.row.x + c.row.w - cw;
        SDL_FRect bg{x, y0, cw, viewH};
        SDL_SetRenderDrawColor(ren_, 30, 34, 46, 255); SDL_RenderFillRectF(ren_, &bg);
        SDL_SetRenderDrawColor(ren_, 140, 150, 185, 255); SDL_RenderDrawRectF(ren_, &bg);
        // Clip the rows to the viewport so a list taller than the panel scrolls under it.
        SDL_Rect clip{int(x), int(y0), int(cw) + 1, int(viewH) + 1};
        SDL_RenderSetClipRect(ren_, &clip);
        for (size_t i = 0; i < opts.size(); ++i) {
            float iy = y0 + float(i) * itemH - dropScroll_;
            if (iy + itemH < y0 || iy > y0 + viewH) continue;   // fully scrolled out
            SDL_FRect it{x, iy, cw, itemH};
            if (int(i) == sel) { SDL_SetRenderDrawColor(ren_, 52, 74, 96, 255); SDL_RenderFillRectF(ren_, &it); }
            const std::string t = uiTextFit(opts[i], dpx, cw - 16 * u_);
            uiText(ren_, t, it.x + 6 * u_, it.y + (itemH - 7 * dpx) / 2, dpx,
                          int(i) == sel ? SDL_Color{215, 235, 245, 255} : SDL_Color{200, 205, 220, 255});
        }
        // A list longer than its pop-up gets a scrollbar thumb, so the rows past the
        // edge (reached with the wheel) are visibly there.
        float fullH = itemH * float(opts.size());
        if (fullH > viewH + 0.5f) {
            float th = std::max(12 * u_, viewH * viewH / fullH);
            SDL_FRect thumb{x + cw - 6 * u_, y0 + (dropScroll_ / (fullH - viewH)) * (viewH - th), 4 * u_, th};
            SDL_SetRenderDrawColor(ren_, 140, 150, 185, 255); SDL_RenderFillRectF(ren_, &thumb);
        }
        SDL_RenderSetClipRect(ren_, nullptr);
    }
    drawRestartPrompt(winW, winH);   // over everything, the open dropdown included
}

// ---- GAME DATA FOLDER ----------------------------------------------------------------

void OptionsScreen::clickFolder() {
    if (!folder_.locked.empty() || folderPick_.busy()) return;
    if (!folder_.picker && !haveDirPicker()) { folderNote_ = FolderNote::NoPicker; return; }
    folderNote_ = FolderNote::None;
    folderPick_.start(folder_.picker, s_.dataDir.empty() ? folder_.inUse : s_.dataDir, s_.dataDir);
}

void OptionsScreen::pollFolder() {
    auto r = folderPick_.poll();
    if (!r) return;
    switch (r->outcome) {
        case DataFolderPick::Cancelled: break;
        case DataFolderPick::Unchanged:    // the saved folder again: re-offer a pending restart
            restartPrompt_ = dataFolderNeedsRestart(s_.dataDir, folder_.inUse);
            break;
        case DataFolderPick::Invalid:      // the setting is untouched
            folderNote_ = FolderNote::Invalid;
            folderNoteText_ = r->reason;
            break;
        case DataFolderPick::Accepted:
            // Written straight away, and ONLY these keys (not the rest of this screen's
            // unsaved edits): the folder is configuration, and Cartographer reads it too.
            if (!saveDataFolderSetting(r->folder, r->manifest)) {
                folderNote_ = FolderNote::SaveFailed;
                folderNoteText_ = r->folder;
                break;
            }
            s_.dataDir = r->folder;
            s_.dataManifest = r->manifest;
            folderNote_ = FolderNote::Saved;
            restartPrompt_ = dataFolderNeedsRestart(s_.dataDir, folder_.inUse);
            std::fprintf(stderr, "options: data folder set to %s%s\n", s_.dataDir.c_str(),
                         restartPrompt_ ? " (restart required)" : "");
            break;
    }
}

std::string OptionsScreen::folderStatusText() const {
    if (folderPick_.busy()) return "CHOOSING A FOLDER...";
    if (!folder_.locked.empty()) return folder_.locked;
    switch (folderNote_) {
        case FolderNote::NoPicker:
            return "NO FOLDER PICKER: INSTALL KDIALOG OR ZENITY, OR START THE GAME WITH --DATA.";
        case FolderNote::Invalid: return "NOT A GAME FOLDER (" + folderNoteText_ + "). KEPT THE CURRENT ONE.";
        case FolderNote::SaveFailed: return "COULD NOT SAVE THE SETTING. KEPT THE CURRENT ONE.";
        default: break;
    }
    if (dataFolderNeedsRestart(s_.dataDir, folder_.inUse)) return "RESTART TO USE: " + s_.dataDir;
    return "SHARED WITH CARTOGRAPHER. A NEW FOLDER IS LOADED BY RESTARTING THE GAME.";
}

void OptionsScreen::drawFolderRow(const Control& c) {
    const bool on = folder_.locked.empty();
    const float fpx = 2.0f * u_, dpx = 1.6f * u_, npx = 1.3f * u_;
    uiText(ren_, c.label, c.row.x, c.row.y + 12 * u_, fpx,
                  on ? SDL_Color{225, 230, 240, 255} : SDL_Color{105, 110, 122, 255});
    // CHANGE button at the right; the folder in use in a chip to its left, elided at
    // its START (the end of a path is the part that tells installs apart).
    const float bw = 96 * u_, gap = 8 * u_;
    SDL_FRect btn{c.row.x + c.row.w - bw, c.row.y + 5 * u_, bw, 24 * u_};
    const float cw = std::max(60 * u_, dropWidth(c) - bw - gap);
    SDL_FRect chip{btn.x - gap - cw, c.row.y + 5 * u_, cw, 24 * u_};
    SDL_SetRenderDrawColor(ren_, on ? 35 : 30, on ? 40 : 33, on ? 56 : 42, 255); SDL_RenderFillRectF(ren_, &chip);
    SDL_SetRenderDrawColor(ren_, on ? 110 : 70, on ? 120 : 76, on ? 150 : 92, 255); SDL_RenderDrawRectF(ren_, &chip);
    uiText(ren_, fitPath(folder_.inUse, dpx, chip.w - 12 * u_), chip.x + 6 * u_, chip.y + (chip.h - 7 * dpx) / 2,
                  dpx, on ? SDL_Color{215, 225, 245, 255} : SDL_Color{120, 126, 140, 255});
    const bool busy = folderPick_.busy();
    SDL_SetRenderDrawColor(ren_, on && !busy ? 52 : 34, on && !busy ? 60 : 38, on && !busy ? 82 : 46, 255);
    SDL_RenderFillRectF(ren_, &btn);
    SDL_SetRenderDrawColor(ren_, on ? 130 : 70, on ? 140 : 76, on ? 170 : 92, 255); SDL_RenderDrawRectF(ren_, &btn);
    const char* t = "CHANGE";
    uiText(ren_, t, btn.x + (btn.w - uiTextWidth(t, fpx)) / 2, btn.y + (btn.h - 7 * fpx) / 2, fpx,
                  on && !busy ? SDL_Color{215, 225, 245, 255} : SDL_Color{110, 115, 125, 255});
    // Status line under it: why it is greyed, what the last pick did, or a pending restart.
    std::string note = folderStatusText();
    SDL_Color col{130, 140, 160, 255};
    if (!on) col = {150, 155, 170, 255};
    else if (busy) col = {240, 190, 90, 255};
    else if (folderNote_ == FolderNote::Invalid || folderNote_ == FolderNote::SaveFailed ||
             folderNote_ == FolderNote::NoPicker) col = {240, 130, 120, 255};
    else if (dataFolderNeedsRestart(s_.dataDir, folder_.inUse)) {
        col = {240, 190, 90, 255};
        const std::string head = "RESTART TO USE: ";
        note = head + fitPath(s_.dataDir, npx, c.row.w - uiTextWidth(head, npx));
    }
    note = uiTextFit(note, npx, c.row.w);
    uiText(ren_, note, c.row.x, c.row.y + 38 * u_, npx, col);
}

void OptionsScreen::drawRestartPrompt(int winW, int winH) {
    if (!restartPrompt_) return;
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 150);
    SDL_FRect dim{0, 0, float(winW), float(winH)}; SDL_RenderFillRectF(ren_, &dim);
    const float w = std::min(panel_.w - 40 * u_, 560 * u_), lpx = 1.6f * u_, tpx = 2.4f * u_;
    std::vector<std::string> lines = {"THE GAME WILL USE", "", "AFTER IT RESTARTS. CARTOGRAPHER USES IT TOO."};
    lines[1] = fitPath(s_.dataDir, lpx, w - 40 * u_);
    if (dirty_) lines.push_back("YOUR UNSAVED OPTIONS ARE SAVED FIRST.");
    const float h = 40 * u_ + float(lines.size()) * 14 * u_ + 64 * u_;
    SDL_FRect box{panel_.x + (panel_.w - w) / 2, panel_.y + (panel_.h - h) / 2, w, h};
    SDL_SetRenderDrawColor(ren_, 30, 33, 44, 252); SDL_RenderFillRectF(ren_, &box);
    SDL_SetRenderDrawColor(ren_, 170, 180, 210, 255); SDL_RenderDrawRectF(ren_, &box);
    const char* title = "GAME DATA FOLDER CHANGED";
    uiText(ren_, title, box.x + (box.w - uiTextWidth(title, tpx)) / 2, box.y + 14 * u_, tpx,
                  {235, 225, 180, 255});
    for (size_t i = 0; i < lines.size(); ++i)
        uiText(ren_, lines[i], box.x + (box.w - uiTextWidth(lines[i], lpx)) / 2,
                      box.y + 40 * u_ + float(i) * 14 * u_, lpx,
                      i == 1 ? SDL_Color{150, 200, 235, 255} : SDL_Color{205, 210, 222, 255});
    const float bw = 170 * u_, bh = 32 * u_, gap = 18 * u_, by = box.y + box.h - bh - 14 * u_;
    restartNowRect_ = {box.x + (box.w - 2 * bw - gap) / 2, by, bw, bh};
    restartLaterRect_ = {restartNowRect_.x + bw + gap, by, bw, bh};
    for (int i = 0; i < 2; ++i) {
        const SDL_FRect& r = i ? restartLaterRect_ : restartNowRect_;
        SDL_SetRenderDrawColor(ren_, i ? 60 : 62, i ? 66 : 92, i ? 86 : 80, 255); SDL_RenderFillRectF(ren_, &r);
        SDL_SetRenderDrawColor(ren_, 130, 140, 170, 255); SDL_RenderDrawRectF(ren_, &r);
        const char* label = i ? "LATER" : "RESTART NOW";
        const float bpx = 2.2f * u_;
        uiText(ren_, label, r.x + (r.w - uiTextWidth(label, bpx)) / 2, r.y + (r.h - 7 * bpx) / 2, bpx,
                      {228, 232, 242, 255});
    }
}

bool OptionsScreen::restartPromptInput(const SDL_Event& e) {
    auto in = [](const SDL_FRect& r, float x, float y) { return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h; };
    bool now = false, later = false;
    if (e.type == SDL_KEYDOWN) {
        now = e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER;
        later = e.key.keysym.sym == SDLK_ESCAPE;
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        now = in(restartNowRect_, float(e.button.x), float(e.button.y));
        later = in(restartLaterRect_, float(e.button.x), float(e.button.y));
    }
    if (later) restartPrompt_ = false;   // the row keeps saying RESTART TO USE
    if (!now) return false;
    restartPrompt_ = false;
    if (dirty_ && onSave_) { onSave_(); dirty_ = false; saved_ = true; }   // as the prompt said
    restartRequested_ = true;
    return true;
}

}  // namespace tak
