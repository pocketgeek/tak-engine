#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tak::video {
struct StreamConfig {
    int width = 1280, height = 720, fps = 30, bitrateKbps = 6000;
    // Empty means automatic hardware selection with a CPU fallback.
    std::string encoder;
    std::string key; // Memory only. Never persisted or logged.
};
struct StreamStatus {
    bool active = false;
    std::string state = "OFF", encoder;
    uint64_t frames = 0, dropped = 0, replaced = 0, bytes = 0;
    int width = 0, height = 0, fps = 30;
};
// One application-owned encoder. All codec/network operations run on its worker.
// Video/audio producers never wait for that worker or allocate on the audio thread.
class Stream {
public:
    Stream();
    ~Stream();
    bool start(const StreamConfig&);
    // Local FLV integration test/recording sink, never exposed as a network URL.
    bool startRecording(const StreamConfig&, const std::string& path);
#ifdef TAK_STREAM_TESTING
    void testDelayOnce(int milliseconds);
    bool startTestEndpoint(const StreamConfig&, const std::string& url, const std::string& caFile);
#endif
    void stop(); // nonblocking; destruction joins after interrupting network I/O
    bool active() const;
    StreamStatus status() const;
    bool video(const uint8_t* rgba, int width, int height, int pitch);
    // Transfer packed RGBA ownership, returning a reusable buffer to the producer.
    bool video(std::vector<uint8_t>& rgba, int width, int height);
    void audio(const int16_t* samples, int frames, int channels); // 11025 Hz
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
