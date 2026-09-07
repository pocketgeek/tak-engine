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
        int frame = have.channels * (SDL_AUDIO_BITSIZE(have.format) / 8);
        if (frame <= 0) frame = 1;
        frameSize_ = frame;
        bytesPerSec_ = have.freq * frame;
        pos_ = 0;
        track_ = track;
        poll();                    // prime the queue at the current volume
        SDL_PauseAudioDevice(dev_, 0);
    }

    // BGM + master volume on the SoundBank 0..256 scale. Applies live WITHOUT
    // restarting: the new gain simply takes effect on the chunks queued from here on
    // (see poll), so dragging a volume slider doesn't clear the queue or jump the
    // track back to its start. A no-op when the volume hasn't actually changed.
    void setVolume(int master, int bgm) {
        master_ = master; bgm_ = bgm;
    }

    // Keep the device fed by streaming small volume-scaled chunks from a play cursor,
    // looping seamlessly at the end of the source. Called once per frame.
    void poll() {
        if (!dev_ || src_.empty()) return;
        Uint32 low = Uint32(bytesPerSec_ / 4);        // keep ~0.25s buffered
        size_t chunk = size_t(bytesPerSec_ / 20);     // ~50ms per queued chunk
        chunk -= chunk % size_t(frameSize_);          // frame-align
        if (chunk == 0) chunk = size_t(frameSize_);
        for (int guard = 0; guard < 64 && SDL_GetQueuedAudioSize(dev_) < low; ++guard)
            queueChunk(chunk);
    }

    void stop() {
        if (dev_) { SDL_CloseAudioDevice(dev_); dev_ = 0; }
        src_.clear();
        pos_ = 0;
        track_ = -1;
    }

    bool playing() const { return dev_ != 0; }

private:
    // Mix one volume-scaled chunk from the source at pos_ (wrapping at the end for a
    // seamless loop) and queue it. All offsets stay frame-aligned because the source
    // length and chunk size are.
    void queueChunk(size_t chunk) {
        int vol = 128 * bgm_ / 256 * master_ / 256;   // SDL_MIX_MAXVOLUME=128; 90/256 -> 45
        std::vector<uint8_t> out;
        out.reserve(chunk);
        size_t need = chunk;
        while (need > 0) {
            size_t avail = src_.size() - pos_;
            size_t take = std::min(need, avail);
            size_t base = out.size();
            out.resize(base + take, 0);                // silence -> MixAudioFormat scales into it
            SDL_MixAudioFormat(out.data() + base, src_.data() + pos_, fmt_, Uint32(take), vol);
            pos_ += take;
            if (pos_ >= src_.size()) pos_ = 0;         // loop
            need -= take;
        }
        SDL_QueueAudio(dev_, out.data(), Uint32(out.size()));
    }

    SDL_AudioDeviceID dev_ = 0;
    std::vector<uint8_t> src_;    // unscaled source, kept so volume can re-apply live
    SDL_AudioFormat fmt_ = 0;
    size_t pos_ = 0;              // play cursor into src_ (bytes, frame-aligned)
    int frameSize_ = 1;          // bytes per sample frame (channels * bytes/sample)
    int bytesPerSec_ = 44100;    // for buffering thresholds
    int master_ = 256, bgm_ = 90;
    int track_ = -1;
};

}  // namespace tak
