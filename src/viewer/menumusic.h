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
        // Pre-scale to the game's music level (SDL_MIX_MAXVOLUME=128; ~45/128 ≈ 90/256,
        // GameView's SoundBank musicVol_) so the menu isn't louder than the game.
        pcm_.assign(len, 0);
        SDL_MixAudioFormat(pcm_.data(), buf, wav.format, len, 45);
        SDL_FreeWAV(buf);
        SDL_AudioSpec want = wav, have{};
        want.callback = nullptr;   // queue-driven
        dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (!dev_) { pcm_.clear(); return; }
        track_ = track;
        SDL_QueueAudio(dev_, pcm_.data(), Uint32(pcm_.size()));
        SDL_PauseAudioDevice(dev_, 0);
    }

    // Re-queue before the buffer drains so the track loops seamlessly. Call/frame.
    void poll() {
        if (dev_ && !pcm_.empty() && SDL_GetQueuedAudioSize(dev_) < Uint32(pcm_.size() / 2))
            SDL_QueueAudio(dev_, pcm_.data(), Uint32(pcm_.size()));
    }

    void stop() {
        if (dev_) { SDL_CloseAudioDevice(dev_); dev_ = 0; }
        pcm_.clear();
        track_ = -1;
    }

    bool playing() const { return dev_ != 0; }

private:
    SDL_AudioDeviceID dev_ = 0;
    std::vector<uint8_t> pcm_;
    int track_ = -1;
};

}  // namespace tak
