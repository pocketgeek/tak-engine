#pragma once

// A tiny looping background-music player for the front-end. main() keeps ONE of
// these alive across the menu -> lobby transition so the track doesn't restart at
// the handoff, and streams it at the game's music level so it matches once in game.
// Both the menu (MainMenu) and main()'s game loop poll it; GameView suppresses its
// own lobby music while this is playing (see externalLobbyMusic_).

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "hpi/hpi.h"

namespace tak {

class MenuMusic {
public:
    ~MenuMusic() { stop(); }

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
        SDL_AudioSpec want = wav, have{};
        want.callback = nullptr;   // queue-driven
        dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (!dev_) { src_.clear(); return; }
        track_ = track;
        rescale();                 // build pcm_ at the current volume + queue it
        SDL_PauseAudioDevice(dev_, 0);
    }

    // BGM + master volume on the SoundBank 0..256 scale; applies live if playing.
    void setVolume(int master, int bgm) {
        master_ = master; bgm_ = bgm;
        if (dev_) { SDL_ClearQueuedAudio(dev_); rescale(); }
    }

    // Re-queue before the buffer drains so the track loops seamlessly. Call/frame.
    void poll() {
        if (dev_ && !pcm_.empty() && SDL_GetQueuedAudioSize(dev_) < Uint32(pcm_.size() / 2))
            SDL_QueueAudio(dev_, pcm_.data(), Uint32(pcm_.size()));
    }

    void stop() {
        if (dev_) { SDL_CloseAudioDevice(dev_); dev_ = 0; }
        pcm_.clear();
        src_.clear();
        track_ = -1;
    }

    bool playing() const { return dev_ != 0; }

private:
    // Rebuild the volume-scaled loop buffer from the retained source and queue it.
    void rescale() {
        if (!dev_ || src_.empty()) return;
        int vol = 128 * bgm_ / 256 * master_ / 256;   // SDL_MIX_MAXVOLUME=128; 90/256 -> 45
        pcm_.assign(src_.size(), 0);
        SDL_MixAudioFormat(pcm_.data(), src_.data(), fmt_, Uint32(src_.size()), vol);
        SDL_QueueAudio(dev_, pcm_.data(), Uint32(pcm_.size()));
    }

    SDL_AudioDeviceID dev_ = 0;
    std::vector<uint8_t> pcm_;    // volume-scaled loop buffer (re-queued each poll)
    std::vector<uint8_t> src_;    // unscaled source, kept so volume can re-apply live
    SDL_AudioFormat fmt_ = 0;
    int master_ = 256, bgm_ = 90;
    int track_ = -1;
};

}  // namespace tak
