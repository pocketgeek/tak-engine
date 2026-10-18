// voice_test -- voice chat, client side (client/voice.h), without a microphone, speakers
// or a network:
//
//  - the Opus codec round trip on a synthetic tone (level kept, packets small);
//  - the jitter buffer: buffering, in-order play, a dropped frame rebuilt from FEC, a late
//    frame waited out, END, 16-bit sequence wrap, a burst trimmed;
//  - the push-to-talk rules: never while typing, never leaking a held key across a focus
//    change, and the Space default with its modifier rules;
//  - the settings: voice chat OFF by default, a round trip through the file;
//  - the engine on SDL's DISK audio driver: a synthetic tone in the capture file, captured
//    only while talking, sent as START..END frames, played back (looped in as a remote
//    talker) at the output volume, mic volume applied, a missing device falling back;
//  - the VOICE CHAT page: every row but the switch greyed and inert while off.
#include "client/hotkeys.h"
#include "client/options.h"
#include "client/settings.h"
#include "client/voice.h"
#include "client/voicecodec.h"
#include "client/voicejitter.h"
#include "client/voiceptt.h"
#include "net/protocol.h"

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace tak;

static int g_fail = 0, g_checks = 0;
static void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

static std::vector<float> tone(int samples, float amp, float hz = 440.0f, int offset = 0) {
    std::vector<float> v(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i)
        v[size_t(i)] = amp * std::sin(2.0f * 3.14159265f * hz * float(i + offset) / float(voice::kRate));
    return v;
}
static float rms(const float* p, size_t n) {
    double s = 0;
    for (size_t i = 0; i < n; ++i) s += double(p[i]) * p[i];
    return n ? float(std::sqrt(s / double(n))) : 0.0f;
}

static void testCodec() {
    std::printf("codec\n");
    voice::Encoder enc;
    voice::Decoder dec;
    check(enc.ok() && dec.ok(), "opus encoder/decoder create");
    const auto pcm = tone(voice::kFrame * 50, 0.5f);
    std::vector<float> out;
    size_t bytes = 0, biggest = 0;
    std::vector<std::vector<uint8_t>> packets;
    for (int f = 0; f < 50; ++f) {
        auto pkt = enc.encode(pcm.data() + size_t(f) * voice::kFrame);
        check(!pkt.empty() && pkt.size() <= net::kVoiceMaxOpus, "packet within the wire cap");
        bytes += pkt.size(); biggest = std::max(biggest, pkt.size());
        packets.push_back(pkt);
        float frame[voice::kFrame];
        check(dec.decode(pkt.data(), pkt.size(), frame), "decode");
        out.insert(out.end(), frame, frame + voice::kFrame);
    }
    // Skip the codec's start-up; the tone's level comes through within a few dB.
    const float in = rms(pcm.data() + 10 * voice::kFrame, 40 * voice::kFrame);
    const float got = rms(out.data() + 10 * voice::kFrame, 40 * voice::kFrame);
    check(got > in * 0.7f && got < in * 1.3f, "decoded level " + std::to_string(got) + " vs " + std::to_string(in));
    const double kbps = double(bytes) * 8 / 1.0 / 1000.0;   // 50 frames = 1 s
    check(kbps > 8 && kbps < 40, "bitrate near 24 kbit/s: " + std::to_string(kbps));
    // FEC and concealment produce a full frame.
    float frame[voice::kFrame];
    check(dec.decodeFec(packets[20].data(), packets[20].size(), frame), "FEC decode");
    check(dec.conceal(frame), "concealment");
    check(!dec.decode(nullptr, 0, frame), "an empty packet is refused");
    std::printf("  %zu bytes/s, largest packet %zu\n", bytes, biggest);
}

static void testJitter() {
    std::printf("jitter buffer\n");
    using voice::JitterBuffer; using voice::Playout;
    auto pkt = [](uint8_t b) { return std::vector<uint8_t>{b}; };
    {
        JitterBuffer jb;
        uint64_t t = 1000;
        jb.push(100, voice::kFlagStart, pkt(1), t);
        check(jb.next().kind == Playout::Idle, "buffers before the target depth");
        jb.push(101, 0, pkt(2), t += 20);
        Playout p = jb.next();
        check(p.kind == Playout::Decode && (*p.data)[0] == 1, "plays the first frame at the target");
        p = jb.next();
        check(p.kind == Playout::Decode && (*p.data)[0] == 2, "then the second, in order");
        // 102 never arrives (the server dropped it); 103 does: rebuilt from 103's FEC.
        jb.push(103, 0, pkt(4), t += 40);
        jb.push(104, 0, pkt(5), t += 20);
        p = jb.next();
        check(p.kind == Playout::Fec && (*p.data)[0] == 4, "a dropped frame comes from the next packet's FEC");
        p = jb.next();
        check(p.kind == Playout::Decode && (*p.data)[0] == 4, "and the next packet still plays");
        p = jb.next();
        check(p.kind == Playout::Decode && (*p.data)[0] == 5, "...followed by its successor");
        // Nothing queued: late, not lost -- conceal without skipping it.
        p = jb.next();
        check(p.kind == Playout::Conceal, "an empty queue conceals");
        jb.push(105, 0, pkt(6), t += 60);
        p = jb.next();
        check(p.kind == Playout::Decode && (*p.data)[0] == 6, "the late frame still plays when it arrives");
        jb.push(106, voice::kFlagEnd, {}, t += 20);
        p = jb.next();
        check(p.kind == Playout::Idle && jb.idle(), "END stops the talker cleanly");
        check(jb.stats().fec == 1 && jb.stats().concealed == 1, "stats count FEC and concealment");
    }
    {   // 16-bit wrap.
        JitterBuffer jb;
        uint64_t t = 0;
        uint16_t s = 65533;
        jb.push(s, voice::kFlagStart, pkt(0), t);
        for (int i = 1; i < 6; ++i) jb.push(uint16_t(s + i), 0, pkt(uint8_t(i)), t += 20);
        bool order = true;
        for (int i = 0; i < 6; ++i) { auto p = jb.next(); order = order && p.kind == Playout::Decode && (*p.data)[0] == i; }
        check(order, "sequence numbers wrap at 65535");
    }
    {   // A burst far deeper than the buffer keeps only the newest.
        JitterBuffer jb;
        jb.push(0, voice::kFlagStart, pkt(0), 0);
        for (int i = 1; i < 40; ++i) jb.push(uint16_t(i), 0, pkt(uint8_t(i)), 0);
        auto p = jb.next();
        check(p.kind == Playout::Decode && (*p.data)[0] >= 40 - JitterBuffer::kMaxTarget && jb.stats().trimmed > 0,
              "a burst is trimmed to the newest frames");
    }
    {   // A late frame from before a new START is ignored.
        JitterBuffer jb;
        jb.push(500, voice::kFlagStart, pkt(1), 0);
        jb.push(499, 0, pkt(9), 0);
        jb.push(501, 0, pkt(2), 0);
        auto p = jb.next();
        check(p.kind == Playout::Decode && (*p.data)[0] == 1 && jb.stats().late == 1, "pre-START frames are dropped");
    }
}

static void testPtt() {
    std::printf("push to talk\n");
    voice::PushToTalk p;
    check(!p.press(false, true, true) && !p.talking(), "a press while typing is text, not voice (not consumed)");
    check(!p.press(false, false, false) && !p.talking(), "no talking where voice is not allowed");
    check(p.press(false, true, false) && p.talking(), "a press starts talking");
    check(p.press(true, true, false) && p.talking(), "key repeat is consumed while talking");
    p.frame(true, true, true, true);
    check(!p.talking(), "a text field taking focus mid-press ends the transmission");
    check(!p.press(true, true, false) && !p.talking(), "and a repeat does not restart it");
    check(!p.release(), "its release then ends nothing");
    p.press(false, true, false); p.frame(true, false, false, true);
    check(!p.talking(), "losing window focus ends it");
    p.press(false, true, false); p.frame(true, false, true, false);
    check(!p.talking(), "a release we never saw ends it");
    p.press(false, true, false); p.frame(false, false, true, true);
    check(!p.talking(), "leaving the room / switching voice off ends it");
    p.press(false, true, false);
    check(p.release() && !p.talking(), "the release ends it");

    // The default binding and its matching rules.
    Hotkeys hk; hk.load({});
    const KeyChord c = hk.chord(Act::PushToTalk);
    check(c.key == SDLK_SPACE && c.mod == 0, "Space is the default push-to-talk key");
    check(hk.pushToTalkPress(SDLK_SPACE, 0) && hk.pushToTalkPress(SDLK_SPACE, KMOD_LSHIFT),
          "pressed with extra modifiers it still talks");
    check(!hk.pushToTalkPress(SDLK_a, 0) && hk.pushToTalkRelease(SDLK_SPACE), "other keys do not; the release matches");
    std::map<std::string, std::string> o;
    setChordOverride(o, Act::Stop, KeyChord{SDLK_SPACE, HK_SHIFT});
    hk.load(o);
    check(hk.pushToTalkPress(SDLK_SPACE, 0) && !hk.pushToTalkPress(SDLK_SPACE, KMOD_LSHIFT),
          "a chord bound exactly to another action is that action");
    o.clear();
    setChordOverride(o, Act::PushToTalk, KeyChord{SDLK_v, HK_CTRL});
    check(o.count("voice.ptt") && o["voice.ptt"] == chordName(KeyChord{SDLK_v, HK_CTRL}), "rebinding persists as voice.ptt");
    hk.load(o);
    check(!hk.pushToTalkPress(SDLK_SPACE, 0) && !hk.pushToTalkPress(SDLK_v, 0) && hk.pushToTalkPress(SDLK_v, KMOD_LCTRL),
          "the rebound key replaces Space and needs its modifier");
    setChordOverride(o, Act::PushToTalk, KeyChord{SDLK_SPACE, 0});
    check(o.empty(), "back to Space stores nothing");
}

static void testSettings(const fs::path& dir) {
    std::printf("settings\n");
    const Settings d;
    check(!d.voiceEnabled && d.voiceOutputSameAsGame && d.voiceMicVolume == 100 && d.voiceOutputVolume == 100 &&
          d.voiceMicDevice.empty() && d.voiceOutputDevice.empty(), "voice chat is OFF by default");
    const fs::path file = dir / "settings.ini";
    setSettingsPath(file.string());
    {   // a file from before voice chat: still off.
        std::ofstream f(file); f << "masterVol = 200\n";
    }
    Settings old = loadSettings();
    check(!old.voiceEnabled && old.masterVol == 200, "an older settings file loads with voice off");
    Settings s;
    s.voiceEnabled = true; s.voiceMicDevice = "USB Mic"; s.voiceOutputSameAsGame = false;
    s.voiceOutputDevice = "Headset"; s.voiceMicVolume = 35; s.voiceOutputVolume = 70;
    s.hotkeys["voice.ptt"] = "Ctrl+V";
    check(saveSettings(s), "save");
    const Settings back = loadSettings();
    check(back == s, "voice settings round-trip through the file");
    check(serializeSettings(Settings{}).find("voiceEnabled = 0") != std::string::npos, "the file says voiceEnabled = 0");
    { std::ofstream f(file); f << "voiceMicVolume = 900\nvoiceOutputVolume = -4\n"; }
    const Settings clamp = loadSettings();
    check(clamp.voiceMicVolume == 100 && clamp.voiceOutputVolume == 0, "volumes clamp to 0..100");
    setSettingsPath("");
}

// The disk driver reads the "microphone" from SDL_DISKAUDIOFILEIN and writes the output
// to SDL_DISKAUDIOFILE, in real time: a synthetic tone in, the played voice out.
static void testEngine(const fs::path& dir) {
    std::printf("engine (SDL disk audio)\n");
    const fs::path in = dir / "mic.raw", out = dir / "speaker.raw";
    {
        const auto pcm = tone(voice::kRate * 12, 0.5f);
        std::ofstream f(in, std::ios::binary);
        f.write(reinterpret_cast<const char*>(pcm.data()), std::streamsize(pcm.size() * sizeof(float)));
    }
    SDL_setenv("SDL_AUDIODRIVER", "disk", 1);
    SDL_setenv("SDL_DISKAUDIOFILEIN", in.string().c_str(), 1);
    SDL_setenv("SDL_DISKAUDIOFILE", out.string().c_str(), 1);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { check(false, std::string("disk audio: ") + SDL_GetError()); return; }
    check(std::string(SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "") == "disk", "disk driver");

    VoiceEngine v;
    Settings s;
    std::vector<VoiceEngine::Frame> sent;
    auto send = [&](const VoiceEngine::Frame& f) { sent.push_back(f); };
    auto pump = [&](int ms, bool room, bool talk) {
        const uint64_t until = SDL_GetTicks64() + uint64_t(ms);
        while (SDL_GetTicks64() < until) { v.configure(s, ""); v.update(room, talk, send); SDL_Delay(5); }
    };

    // Off (the default): nothing opens, nothing is sent, whatever the key does.
    pump(100, true, true);
    check(sent.empty() && v.micState() == VoiceEngine::Device::Off && v.outState() == VoiceEngine::Device::Off,
          "voice off opens no device and sends nothing");

    s.voiceEnabled = true;
    pump(150, true, false);
    check(v.micState() == VoiceEngine::Device::Ok && v.outState() == VoiceEngine::Device::Ok, "on, in a room: both open");
    check(sent.empty(), "nothing is sent while the key is up");

    pump(1000, true, true);
    const size_t talked = sent.size();
    check(talked >= 30 && talked <= 70, "about 50 frames in a second of talking: " + std::to_string(talked));
    check(!sent.empty() && (sent.front().flags & net::kVoiceStart) && !(sent.back().flags & net::kVoiceEnd),
          "the first frame is a START");
    bool seqOk = true;
    for (size_t i = 1; i < sent.size(); ++i) seqOk = seqOk && sent[i].seq == uint16_t(sent[i - 1].seq + 1);
    check(seqOk, "sequence numbers count up");
    check(v.transmitting() && v.micLevel() > 0.4f && v.micLevel() < 0.6f,
          "mic level follows the tone: " + std::to_string(v.micLevel()));
    pump(60, true, false);
    check(sent.size() == talked + 1 && (sent.back().flags & net::kVoiceEnd) && sent.back().opus.empty() && !v.transmitting(),
          "releasing sends one empty END frame");

    // Mic volume 50 halves the level.
    s.voiceMicVolume = 50;
    sent.clear();
    pump(400, true, true);
    check(v.micLevel() > 0.2f && v.micLevel() < 0.3f, "mic volume 50 halves the level: " + std::to_string(v.micLevel()));
    pump(60, true, false);
    s.voiceMicVolume = 100;

    // Loop the frames back as a remote talker and measure what the "speaker" played.
    auto playback = [&](int volume) {
        s.voiceOutputVolume = volume;
        sent.clear();
        pump(800, true, true);
        pump(60, true, false);
        std::vector<VoiceEngine::Frame> frames = sent;
        const auto before = fs::exists(out) ? fs::file_size(out) : 0;
        // Delivered at the talker's own pace, one frame every 20 ms, as the server relays it.
        for (size_t k = 0; k < frames.size(); ++k) {
            const auto& f = frames[k];
            VoiceEngine::Incoming i; i.talker = 7; i.slot = 2; i.seq = f.seq; i.flags = f.flags; i.opus = f.opus;
            if (k == 0) i.name = "Remote";
            v.push(i);
            if (k == 3) check(!v.speaking().empty() && v.speaking()[0].name == "Remote" && v.speaking()[0].slot == 2,
                              "the remote talker shows as speaking");
            pump(20, true, false);
        }
        pump(500, true, false);
        std::vector<float> got;
        {
            std::ifstream f(out, std::ios::binary);
            f.seekg(std::streamoff(before));
            float x;
            while (f.read(reinterpret_cast<char*>(&x), sizeof x)) got.push_back(x);
        }
        float peak = 0;
        for (float x : got) peak = std::max(peak, std::fabs(x));
        return peak;
    };
    const float full = playback(100);
    const float half = playback(50);
    check(full > 0.35f && full < 0.75f, "the remote talker plays at full volume: peak " + std::to_string(full));
    check(half > full * 0.35f && half < full * 0.65f, "output volume 50 halves it: " + std::to_string(half));
    check(v.stats().decoded > 20, "frames were decoded: " + std::to_string(v.stats().decoded));

    // Out of the room: no devices, and no talkers.
    pump(60, false, true);
    check(v.micState() == VoiceEngine::Device::Off && v.outState() == VoiceEngine::Device::Off && v.speaking().empty(),
          "leaving the room closes both devices");
    // The VOICE CHAT screen's meter: the mic runs, nothing is sent even with the key down.
    sent.clear();
    v.setMonitoring(true);
    pump(300, false, true);
    check(v.micState() == VoiceEngine::Device::Ok && v.micLevel() > 0.4f && sent.empty(),
          "monitoring meters the mic without sending");
    v.setMonitoring(false);
    pump(30, false, false);
    check(v.micState() == VoiceEngine::Device::Off, "and closes it again");

    // A chosen device that is not there falls back to the system default, and says so.
    s.voiceMicDevice = "No Such Microphone";
    s.voiceOutputSameAsGame = false; s.voiceOutputDevice = "No Such Speaker";
    pump(100, true, false);
    check(v.micState() == VoiceEngine::Device::NotFound && v.outState() == VoiceEngine::Device::NotFound,
          "missing devices fall back to the default");
    check(VoiceEngine::note(v.micState()) == "NOT FOUND - USING DEFAULT", "...and the screen says so");
    sent.clear();
    pump(300, true, true);
    check(!sent.empty(), "still talking on the fallback device");
    pump(60, true, false);

    // Switched off mid-game: everything closes at once.
    s.voiceEnabled = false;
    sent.clear();
    pump(60, true, true);
    check(v.micState() == VoiceEngine::Device::Off && v.outState() == VoiceEngine::Device::Off && sent.empty(),
          "switching voice off closes everything");
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

// The VOICE CHAT page on a software renderer.
static void testPage() {
    std::printf("VOICE CHAT page\n");
    SDL_Window* win = SDL_CreateWindow("voice", 0, 0, 1280, 720, SDL_WINDOW_HIDDEN);
    SDL_Renderer* ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : nullptr;
    if (!ren) { check(false, std::string("renderer: ") + SDL_GetError()); return; }
    Settings s;
    int changes = 0, saves = 0, hotkeys = 0, monitorOn = 0, monitorOff = 0;
    VoicePageHooks hooks;
    hooks.micLevel = [] { return 0.5f; };
    hooks.micNote = [] { return std::string(); };
    hooks.outNote = [] { return std::string(); };
    hooks.monitor = [&](bool on) { (on ? monitorOn : monitorOff)++; };
    const int W = 1280, H = 720;
    {
        OptionsScreen page(ren, s, [&] { ++changes; }, [&] { ++saves; }, hooks, [&] { ++hotkeys; });
        check(page.page() == OptionsPage::Voice && monitorOn == 1, "the page opens and starts the meter");
        page.render(W, H);
        auto click = [&](const char* label, float frac) {
            const SDL_FRect* r = page.rowRect(label);
            check(r != nullptr, std::string("row ") + label);
            if (!r) return;
            SDL_Event e{}; e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT;
            e.button.x = int(r->x + r->w * frac); e.button.y = int(r->y + r->h / 2);
            page.input(e, W, H);
            SDL_Event u{}; u.type = SDL_MOUSEBUTTONUP; u.button.button = SDL_BUTTON_LEFT;
            u.button.x = e.button.x; u.button.y = e.button.y;
            page.input(u, W, H);
            page.render(W, H);
        };
        const char* rows[] = {"MIC DEVICE", "MIC VOLUME", "MIC LEVEL", "OUTPUT DEVICE", "OUTPUT VOLUME", "PUSH TO TALK"};
        check(page.rowEnabled("VOICE CHAT"), "the switch is live while off");
        for (const char* r : rows) check(!page.rowEnabled(r), std::string(r) + " is greyed while voice chat is off");
        // Greyed rows ignore input.
        click("MIC VOLUME", 0.25f);
        click("OUTPUT VOLUME", 0.25f);
        click("PUSH TO TALK", 0.98f);
        click("OUTPUT DEVICE", 0.9f);
        check(s.voiceMicVolume == 100 && s.voiceOutputVolume == 100 && hotkeys == 0 && changes == 0,
              "greyed rows ignore clicks");
        // Switch on: every row comes alive.
        click("VOICE CHAT", 0.95f);
        check(s.voiceEnabled && changes == 1, "the switch turns voice chat on");
        for (const char* r : rows) check(page.rowEnabled(r), std::string(r) + " is live once on");
        click("MIC VOLUME", 0.25f);
        check(s.voiceMicVolume >= 23 && s.voiceMicVolume <= 27, "MIC VOLUME 0..100: " + std::to_string(s.voiceMicVolume));
        click("OUTPUT VOLUME", 0.75f);
        check(s.voiceOutputVolume >= 73 && s.voiceOutputVolume <= 77, "OUTPUT VOLUME 0..100");
        click("PUSH TO TALK", 0.98f);
        check(hotkeys == 1, "PUSH TO TALK opens CONTROLS");
        // VOICE OUTPUT: open the list, pick System Default (index 1), not same as game.
        const SDL_FRect* out = page.rowRect("OUTPUT DEVICE");
        click("OUTPUT DEVICE", 0.9f);
        {
            const SDL_FRect item = page.dropItemRect(1);   // [0] SAME AS GAME, [1] System Default
            check(item.w > 0 && out != nullptr, "the OUTPUT DEVICE list opens");
            SDL_Event e{}; e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT;
            e.button.x = int(item.x + item.w / 2); e.button.y = int(item.y + item.h / 2);
            page.input(e, W, H);
            page.render(W, H);
        }
        check(!s.voiceOutputSameAsGame && s.voiceOutputDevice.empty(),
              "OUTPUT DEVICE can leave SAME AS GAME for the system default");
        check(s.masterVol == 256, "the page touches nothing but voice");
    }
    check(monitorOff == 1, "closing the page stops the meter");
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
}

int main() {
    SDL_SetMainReady();
    const fs::path dir = fs::temp_directory_path() / ("tak-voice-test-" + std::to_string(SDL_GetPerformanceCounter()));
    fs::create_directories(dir);
    testCodec();
    testJitter();
    testPtt();
    testSettings(dir);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::printf("SDL_Init: %s\n", SDL_GetError()); return 1; }
    testEngine(dir);
    testPage();
    SDL_Quit();
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::printf("voice_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
