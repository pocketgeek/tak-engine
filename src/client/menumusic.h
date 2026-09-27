#pragma once

// A tiny looping background-music player for the front-end. main() keeps ONE of
// these alive across the menu -> lobby transition so the track doesn't restart at
// the handoff, and streams it at the game's music level so it matches once in game.
// Both the menu (MainMenu) and main()'s game loop poll it; GameView suppresses its
// own lobby music while this is playing (see externalLobbyMusic_).

#include <SDL.h>

#include <atomic>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "hpi/hpi.h"
#include "client/options.h"   // tak::openAudioDevice (routes to the chosen device)

namespace tak {

class MenuMusic {
public:
    ~MenuMusic() { stop(); }
    MenuMusic() = default;
    MenuMusic(const MenuMusic&) = delete;
    MenuMusic& operator=(const MenuMusic&) = delete;

    // The stream mixer accepts 11025 Hz signed PCM. Tap the volume-scaled
    // device buffer, converting the menu WAV's native format/rate as needed.
    void setAudioTap(void* context, void (*tap)(void*, const int16_t*, int, int)) {
        if (dev_) SDL_LockAudioDevice(dev_);
        tapContext_ = context; tap_ = tap;
        resetTapConverter();
        if (dev_) SDL_UnlockAudioDevice(dev_);
    }

    // Play music/track<n>.wav on a loop. Idempotent: if this track is already
    // playing, does nothing (so it does NOT restart across menu -> lobby).
    void start(const hpi::Vfs& vfs, int track) {
        if (dev_ && track_ == track) return;
        stop();
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
        std::vector<uint8_t> raw;
        try { raw = vfs.read("music/track" + std::to_string(track) + ".wav"); } catch (...) { return; }
        SDL_RWops* rw = raw.empty() ? nullptr : SDL_RWFromConstMem(raw.data(), int(raw.size()));
        SDL_AudioSpec wav{}; Uint8* buf = nullptr; Uint32 len = 0;
        if (!rw || !SDL_LoadWAV_RW(rw, 1, &wav, &buf, &len)) return;
        // Keep the unscaled source so the BGM/master volume can be re-applied live.
        src_.assign(buf, buf + len);
        fmt_ = wav.format;
        SDL_FreeWAV(buf);
        // Callback-driven, NOT queue-driven: the audio thread pulls chunks itself, so a
        // long synchronous load on the main thread (map + texture atlases when a
        // game starts) can't starve the queue and stutter the BGM the way a poll()
        // did. Matches SoundBank's model.
        SDL_AudioSpec want = wav, have{};
        want.samples = 2048;             // ~46ms device buffer at 44.1kHz
        want.callback = &MenuMusic::mixThunk;
        want.userdata = this;
        pos_ = 0;                        // set before the device unpauses (callback reads it)
        openWant_ = want;                // kept so reopen() can re-open on a new device
        dev_ = tak::openAudioDevice(0, &want, &have, 0);   // flags 0 => have == want
        if (!dev_) { src_.clear(); return; }
        silence_ = have.silence;
        track_ = track;
        resetTapConverter();
        playing_.store(true);
        SDL_PauseAudioDevice(dev_, 0);   // the callback starts pulling
    }

    // Re-open the current track on the now-current output device (Options device switch),
    // continuing from the same play position so the music doesn't restart. No-op if nothing
    // is playing -- the next start() then simply opens on the new device.
    void reopen() {
        if (!dev_ || src_.empty()) return;
        SDL_CloseAudioDevice(dev_);      // stops + joins the callback thread; pos_ is preserved
        dev_ = 0;
        playing_.store(false);
        SDL_AudioSpec have{};
        dev_ = tak::openAudioDevice(0, &openWant_, &have, 0);
        if (!dev_) { src_.clear(); track_ = -1; return; }
        silence_ = have.silence;
        resetTapConverter();
        playing_.store(true);
        SDL_PauseAudioDevice(dev_, 0);
    }

    // BGM + master volume on the SoundBank 0..256 scale. Applies live WITHOUT
    // restarting: the callback simply reads the new gain, so dragging a volume slider
    // doesn't jump the track back to its start.
    void setVolume(int master, int bgm) {
        master_.store(master, std::memory_order_relaxed);
        bgm_.store(bgm, std::memory_order_relaxed);
    }

    // Callback-driven now, so there is nothing to pump each frame. Kept so the existing
    // per-frame call sites stay valid.
    void poll() {}

    void stop() {
        // CloseAudioDevice stops + joins the callback thread, so it's safe to clear the
        // source it reads only after this returns.
        if (dev_) { SDL_CloseAudioDevice(dev_); dev_ = 0; }
        playing_.store(false);
        if (tapConverter_) { SDL_FreeAudioStream(tapConverter_); tapConverter_ = nullptr; }
        src_.clear();
        pos_ = 0;
        track_ = -1;
    }

    bool playing() const { return playing_.load(); }

private:
    friend struct MenuMusicTestAccess;
    void resetTapConverter() {
        if (tapConverter_) SDL_FreeAudioStream(tapConverter_);
        tapConverter_ = tap_ && !src_.empty()
            ? SDL_NewAudioStream(fmt_, openWant_.channels, openWant_.freq,
                                 AUDIO_S16SYS, 2, 11025) : nullptr;
    }
    static void SDLCALL mixThunk(void* userdata, Uint8* stream, int len) {
        static_cast<MenuMusic*>(userdata)->fill(stream, len);
    }

    // Fill one device buffer by mixing the volume-scaled source at pos_, wrapping at the
    // end for a seamless loop. Runs on the AUDIO thread. Offsets stay frame-aligned
    // because the source length and every mix step are.
    void fill(Uint8* stream, int len) {
        if (src_.empty()) { SDL_memset(stream, silence_, size_t(len)); return; }
        int vol = 128 * bgm_.load(std::memory_order_relaxed) / 256
                      * master_.load(std::memory_order_relaxed) / 256;   // SDL_MIX_MAXVOLUME=128
        SDL_memset(stream, silence_, size_t(len));   // silence -> MixAudioFormat scales onto it
        size_t need = size_t(len);
        Uint8* out = stream;
        while (need > 0) {
            size_t avail = src_.size() - pos_;
            size_t take = std::min(need, avail);
            SDL_MixAudioFormat(out, src_.data() + pos_, fmt_, Uint32(take), vol);
            pos_ += take;
            if (pos_ >= src_.size()) pos_ = 0;       // loop
            out += take;
            need -= take;
        }
        if (tap_ && tapConverter_ && SDL_AudioStreamPut(tapConverter_, stream, len) == 0) {
            std::array<int16_t, 2048> pcm;
            int bytes;
            while ((bytes = SDL_AudioStreamGet(tapConverter_, pcm.data(), int(sizeof pcm))) > 0)
                tap_(tapContext_, pcm.data(), bytes / (2 * int(sizeof(int16_t))), 2);
        }
    }

    void* tapContext_ = nullptr;
    void (*tap_)(void*, const int16_t*, int, int) = nullptr;
    SDL_AudioStream* tapConverter_ = nullptr;
    std::atomic<bool> playing_{false}; // also read by the game's streaming audio callback
    SDL_AudioDeviceID dev_ = 0;
    SDL_AudioSpec openWant_{};    // spec used to open dev_, kept so reopen() can switch devices
    std::vector<uint8_t> src_;    // unscaled source, kept so volume can re-apply live
    SDL_AudioFormat fmt_ = 0;
    Uint8 silence_ = 0;          // device silence byte (have.silence)
    size_t pos_ = 0;             // play cursor into src_ (bytes, frame-aligned); audio thread only
    std::atomic<int> master_{256}, bgm_{128};   // read by the audio thread, set from the main thread
    int track_ = -1;
};

}  // namespace tak
