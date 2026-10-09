#pragma once
// TAK_PACELOG=<1|path> -- a debug-only per-frame pacing record of the lockstep drain
// (src/client/dev.h; nothing of it exists in a release build, and nothing it does touches
// the sim or the wire). One record per rendered frame:
//   wall ms since the previous frame, ticks the sim finished in it (what the player sees: the
//   worker's simProcessedTick_ delta with the sim thread on, the drained count inline), the
//   sim worker's inbox depth, the jitter buffer's depth, the servo rate, and whether the
//   512-tick fast-forward fired.
// At exit it prints one summary line: ticks-per-frame histogram, zero-tick frames, frames at
// or over twice the nominal tick count (the median), the longest burst and where, p99 frame
// ms, inbox p99 and the fast-forward count. TAK_PACELOG=<path> also writes the records, one
// line each, to <path>. Report-only: tools/pace_check.sh runs it, no gate reads it.
//
// The presentation and worker columns (WE E0.1) follow the original seven on each record line:
//   shown (the tick of the snapshot the frame displays), td (the display time: shown - 1 plus the
//   interpolation fraction the render uses), render-thread simMutex_ wait in us, and the sim
//   worker's (or the inline drain's) time over the ticks it finished since the previous frame:
//   world ms (World::tick + transport-effect capture), capture ms (captureFrame), hash ms (the
//   reported state hash), and the wall and thread-CPU ms of those jobs (wall/CPU > 1.1 is
//   preemption). At exit a second line, PACEWORK, gives the per-tick means and the lock waits.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace tak {

class PaceLog {
public:
    struct Frame {
        double wallMs = 0;
        int ticks = 0, inbox = 0, buffered = 0;
        float rate = 0;
        bool fastForward = false;
        uint32_t tick = 0;   // sim tick reached at the end of the frame
        uint32_t shown = 0;  // tick of the displayed snapshot
        double td = 0;       // display time in ticks (shown - 1 + interpolation fraction)
        double lockWaitUs = 0;
        double worldMs = 0, captureMs = 0, hashMs = 0, jobWallMs = 0, jobCpuMs = 0;
    };

    // "1" (or any value that is not a path): summary only. Anything with a '/' or '.' is a file.
    void open(const char* spec) {
        if (!spec || !*spec || (spec[0] == '0' && !spec[1])) return;
        on_ = true;
        const std::string s = spec;
        if (s.find('/') != std::string::npos || s.find('.') != std::string::npos) f_ = std::fopen(s.c_str(), "w");
    }
    bool on() const { return on_; }

    void frame(const Frame& fr) {
        if (!on_) return;
        frames_.push_back(fr);
        if (f_)
            std::fprintf(f_, "%.3f %d %u %d %d %.3f %d %u %.3f %.1f %.3f %.3f %.3f %.3f %.3f\n", fr.wallMs, fr.ticks,
                         fr.tick, fr.inbox, fr.buffered, double(fr.rate), fr.fastForward ? 1 : 0, fr.shown, fr.td,
                         fr.lockWaitUs, fr.worldMs, fr.captureMs, fr.hashMs, fr.jobWallMs, fr.jobCpuMs);
    }

    struct Summary {
        size_t frames = 0, zeroTick = 0, overTwice = 0, fastForward = 0;
        int nominal = 0, maxBurst = 0, inboxP99 = 0;
        uint32_t burstTick = 0;
        double wallP99 = 0, wallMax = 0;
        std::map<int, size_t> histogram;
    };

    // Frames before `skip` are ignored (the load frames and the initial buffer fill).
    Summary summarize(size_t skip = 0) const {
        Summary s;
        std::vector<int> ticks, inbox;
        std::vector<double> wall;
        for (size_t i = skip; i < frames_.size(); ++i) {
            ticks.push_back(frames_[i].ticks);
            inbox.push_back(frames_[i].inbox);
            wall.push_back(frames_[i].wallMs);
        }
        s.frames = ticks.size();
        if (ticks.empty()) return s;
        auto sorted = ticks;
        std::sort(sorted.begin(), sorted.end());
        s.nominal = sorted[sorted.size() / 2];
        std::sort(inbox.begin(), inbox.end());
        s.inboxP99 = inbox[std::min(inbox.size() - 1, inbox.size() * 99 / 100)];
        std::sort(wall.begin(), wall.end());
        s.wallP99 = wall[std::min(wall.size() - 1, wall.size() * 99 / 100)];
        s.wallMax = wall.back();
        for (size_t i = skip; i < frames_.size(); ++i) {
            const auto& fr = frames_[i];
            ++s.histogram[fr.ticks];
            if (fr.ticks == 0) ++s.zeroTick;
            if (s.nominal > 0 && fr.ticks >= 2 * s.nominal) ++s.overTwice;
            if (fr.fastForward) ++s.fastForward;
            if (fr.ticks > s.maxBurst) { s.maxBurst = fr.ticks; s.burstTick = fr.tick; }
        }
        return s;
    }

    static std::string format(const Summary& s) {
        std::string out = "PACE frames=" + std::to_string(s.frames) + " nominal=" + std::to_string(s.nominal) +
                          " zero_tick=" + std::to_string(s.zeroTick) + " over_2x=" + std::to_string(s.overTwice) +
                          " max_burst=" + std::to_string(s.maxBurst) + "@" + std::to_string(s.burstTick) +
                          " ff512=" + std::to_string(s.fastForward) + " inbox_p99=" + std::to_string(s.inboxP99);
        char b[64];
        std::snprintf(b, sizeof b, " wall_p99=%.2fms wall_max=%.2fms", s.wallP99, s.wallMax);
        out += b;
        out += " hist={";
        bool first = true;
        for (const auto& [k, n] : s.histogram) {
            out += (first ? "" : ", ") + std::to_string(k) + ":" + std::to_string(n);
            first = false;
        }
        return out + "}";
    }

    // Per-tick means of the worker columns and the render-thread lock waits, over frames from `skip`.
    std::string work(size_t skip = 0) const {
        double ticks = 0, world = 0, capture = 0, hash = 0, wall = 0, cpu = 0, waitMax = 0, waitSum = 0;
        size_t waited = 0, frames = 0;
        for (size_t i = skip; i < frames_.size(); ++i) {
            const auto& fr = frames_[i];
            ++frames;
            ticks += fr.ticks; world += fr.worldMs; capture += fr.captureMs; hash += fr.hashMs;
            wall += fr.jobWallMs; cpu += fr.jobCpuMs;
            waitSum += fr.lockWaitUs;
            waitMax = std::max(waitMax, fr.lockWaitUs);
            if (fr.lockWaitUs > 0) ++waited;
        }
        char b[320];
        const double t = std::max(1.0, ticks);
        std::snprintf(b, sizeof b,
                      "PACEWORK ticks=%.0f world_ms=%.3f capture_ms=%.3f hash_ms=%.3f job_wall_ms=%.3f wall_cpu=%.3f"
                      " lock_wait_frames=%zu/%zu lock_wait_us_sum=%.0f lock_wait_us_max=%.0f",
                      ticks, world / t, capture / t, hash / t, wall / t, cpu > 0 ? wall / cpu : 0.0, waited, frames,
                      waitSum, waitMax);
        return b;
    }

    ~PaceLog() {
        if (f_) std::fclose(f_);
        // The first second of frames is the initial buffer fill and the load: not pacing.
        if (on_ && !frames_.empty()) {
            size_t skip = 0;
            double t = 0;
            while (skip < frames_.size() && t < 1000.0) t += frames_[skip++].wallMs;
            std::fprintf(stderr, "%s\n", format(summarize(skip)).c_str());
            std::fprintf(stderr, "%s\n", work(skip).c_str());
        }
    }

private:
    bool on_ = false;
    std::FILE* f_ = nullptr;
    std::vector<Frame> frames_;
};

}  // namespace tak
