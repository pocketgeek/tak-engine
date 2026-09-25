#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace tak {

// Select the cursor frame from its authored GAF delays. Cursor clocks run in wall time
// so the pointer keeps animating while simulation is paused; each GAF duration is in
// 30 Hz ticks, matching the native sprite clock. The template also lets the timing
// rule be regression-tested without SDL textures.
template<class Frames>
size_t cursorFrameAt(const Frames& frames, uint64_t elapsedMilliseconds) {
    if (frames.empty()) return 0;

    uint64_t cycleTicks = 0;
    for (const auto& frame : frames)
        cycleTicks += std::max<uint16_t>(1, frame.delayTicks);
    if (cycleTicks == 0) return 0;

    uint64_t tick = (elapsedMilliseconds * 30 / 1000) % cycleTicks;
    for (size_t i = 0; i < frames.size(); ++i) {
        const uint64_t duration = std::max<uint16_t>(1, frames[i].delayTicks);
        if (tick < duration) return i;
        tick -= duration;
    }
    return frames.size() - 1;
}

// Native 0x4d5930 selects end-of-order cursor markers from the global game tick.
// Its frame interval is twice the first GAF frame's delay, and it does not reset
// when an order is issued. The marker API uses the first frame duration even if
// later frames carry different authored delays.
inline size_t cursorOrderMarkerFrameAt(size_t frameCount, uint16_t firstFrameDelayTicks,
                                      uint64_t gameTick) {
    if (!frameCount) return 0;
    const uint64_t interval = 2 * uint64_t(std::max<uint16_t>(1, firstFrameDelayTicks));
    return size_t((gameTick / interval) % frameCount);
}

} // namespace tak
