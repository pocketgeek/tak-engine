#pragma once

// The replay transport: the speed steps, the bar's geometry, the timeline's x <-> tick
// mapping and the keyframe bookkeeping behind rewinding. No SDL and no GameView, so
// replaybar_test pins all of it. Display and seek logic only: what a replay computes
// is decided by the recording, whichever way it is reached.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace tak::replaybar {

inline constexpr int kTicksPerSecond = 30;

// Playback speeds. The table is the replay's own: a live game keeps retail's -9..+10.
inline constexpr std::array<float, 11> kSpeeds = {0.25f, 0.5f, 1, 2, 4, 8, 16, 32, 64, 128, 256};
inline constexpr int kNormalSpeed = 2;   // index of 1x
inline constexpr float kFullEffectsSpeed = 8;   // above this: no sound, display time held here

inline int stepSpeed(int index, int delta) {
    return std::clamp(index + delta, 0, int(kSpeeds.size()) - 1);
}
inline float speedAt(int index) { return kSpeeds[size_t(std::clamp(index, 0, int(kSpeeds.size()) - 1))]; }

// The wall-clock time one frame may spend simulating. Generous when the viewer has asked
// for speed over smoothness, and while a jump is running.
inline double frameBudgetMs(float speed, bool jumping) {
    return jumping || speed > kFullEffectsSpeed ? 40.0 : 14.0;
}

// "8X", "0.25X"; with the rate actually reached once it falls clearly short of the
// request ("64X (27X)", "1X (0.8X)"). `real` <= 0: not measured yet.
inline std::string speedText(float speed, float real) {
    const auto one = [](float value) {
        char text[24];
        if (value >= 1) std::snprintf(text, sizeof text, "%.0fX", double(value));
        else std::snprintf(text, sizeof text, "%gX", double(value));
        return std::string(text);
    };
    std::string out = one(speed);
    if (real > 0 && real < speed * 0.9f)
        out += " (" + one(real >= 1 ? std::round(real) : std::max(0.1f, std::round(real * 10) / 10)) + ")";
    return out;
}

inline std::string timeText(size_t tick) {
    const size_t seconds = tick / kTicksPerSecond;
    char text[32];
    std::snprintf(text, sizeof text, "%zu:%02zu", seconds / 60, seconds % 60);
    return text;
}

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool hit(float px, float py) const { return px >= x && py >= y && px <= x + w && py <= y + h; }
};

// Row 1 is the timeline; row 2 holds << play >>, - speed +, then the time at the right.
struct Layout {
    Rect box, timeline, track;
    Rect back, play, forward, slower, speed, faster;
    float textPx = 1, timeRight = 0;
};

// The bar at the top centre of the map view: nothing else lives there, and a click
// target must not move when a selection brings up the build row or the panel.
inline Layout layout(float mapViewW, float uiScale, float top) {
    constexpr float kRowUnits = 430;   // what row 2 wants at scale 1, its time included
    Layout l;
    const float s = uiScale;
    const float w = std::min(760 * s, std::max(mapViewW - 32, 64.0f));
    const float pad = 8 * s, timelineH = 18 * s, trackH = 8 * s, rowGap = 4 * s;
    // Row 2 shrinks with a bar too narrow for it; the timeline's hit area never does.
    const float r = s * std::min(1.0f, w / (kRowUnits * s));
    const float rowH = 26 * r, button = 34 * r, step = 26 * r, label = 92 * r, gap = 4 * r;
    l.box = {(mapViewW - w) / 2, top, w, pad + timelineH + rowGap + rowH + pad};
    l.timeline = {l.box.x + pad, top + pad, w - 2 * pad, timelineH};
    l.track = {l.timeline.x, l.timeline.y + (timelineH - trackH) / 2, l.timeline.w, trackH};
    const float y = l.timeline.y + timelineH + rowGap;
    float x = l.box.x + pad;
    const auto place = [&](Rect& rect, float width, float after) {
        rect = {x, y, width, rowH};
        x += width + after;
    };
    place(l.back, button, gap);
    place(l.play, button, gap);
    place(l.forward, button, 12 * r);
    place(l.slower, step, gap);
    place(l.speed, label, gap);
    place(l.faster, step, 0);
    l.textPx = 1.5f * r;
    l.timeRight = l.box.x + w - pad;
    return l;
}

inline float tickToX(const Layout& l, size_t tick, size_t length) {
    const float fraction = length ? std::min(1.0f, float(double(tick) / double(length))) : 0;
    return l.track.x + l.track.w * fraction;
}
inline size_t xToTick(const Layout& l, float x, size_t length) {
    const float fraction = l.track.w > 0 ? std::clamp((x - l.track.x) / l.track.w, 0.0f, 1.0f) : 0;
    return size_t(std::llround(double(fraction) * double(length)));
}

// ---- keyframes ------------------------------------------------------------------

inline constexpr uint32_t kKeyframeTicks = 900;        // one every 30 s of game
inline constexpr uint32_t kStepTicks = 30 * kTicksPerSecond;      // << >> [ ]
inline constexpr uint32_t kLongStepTicks = 300 * kTicksPerSecond;   // Shift+[ Shift+]
inline constexpr uint32_t kSnapTicks = 15 * kTicksPerSecond;
inline constexpr uint64_t kKeyframeCeilingBytes = uint64_t(4) << 30;

// What keyframes may hold in all: a quarter of the machine, at most 4 GB.
inline uint64_t keyframeBudget(uint64_t systemBytes) {
    return std::min(systemBytes / 4, kKeyframeCeilingBytes);
}

// The index of the latest keyframe at or before `tick`. `ticks` ascends from 0.
inline size_t keyframeAt(const std::vector<uint32_t>& ticks, uint32_t tick) {
    const auto after = std::upper_bound(ticks.begin(), ticks.end(), tick);
    return after == ticks.begin() ? 0 : size_t(after - ticks.begin()) - 1;
}

// Where << lands: on a keyframe within kSnapTicks of the wanted tick when one is behind
// the playhead (the jump is then a copy, however heavy the game), else on the tick.
inline uint32_t snapRewind(const std::vector<uint32_t>& ticks, uint32_t want, uint32_t playhead) {
    uint32_t best = want, bestOff = kSnapTicks + 1;
    for (const uint32_t t : ticks) {
        if (t >= playhead) break;
        const uint32_t off = t > want ? t - want : want - t;
        if (off < bestOff) { bestOff = off; best = t; }
    }
    return best;
}

// Where a press of << or >> aims. It stacks on the jump in hand (`running`, the playhead
// if there is none), but >> never reaches further than a long step past the playhead: a
// heavy game runs a jump far slower than keys can ask for one. A running jump is not
// shortened, however far a timeline click sent it.
inline size_t nudgeTarget(size_t playhead, size_t running, int64_t ticks, size_t length) {
    const int64_t from = int64_t(running);
    const int64_t want = std::clamp(from + ticks, int64_t(0), int64_t(length));
    if (ticks <= 0) return size_t(want);
    return size_t(std::max(from, std::min(want, int64_t(playhead) + int64_t(kLongStepTicks))));
}

// Which keyframe to give up when the budget is full: the interior one with the smallest
// gap / min(age, 2 * length / capacity), `gap` being the hole dropping it would leave and
// `age` its distance from the playhead. Near the playhead keyframes stay dense; far from
// it they thin to an even spread. The first (tick 0) and the last are never dropped.
// ticks.size() when there is nothing to drop.
inline size_t thinChoice(const std::vector<uint32_t>& ticks, uint32_t playhead, size_t length, size_t capacity) {
    if (ticks.size() < 3) return ticks.size();
    const double reach = 2.0 * double(length) / double(std::max<size_t>(capacity, 1));
    size_t pick = ticks.size();
    double best = 0;
    for (size_t i = 1; i + 1 < ticks.size(); ++i) {
        const double gap = double(ticks[i + 1]) - double(ticks[i - 1]);
        const double age = std::abs(double(ticks[i]) - double(playhead));
        const double score = gap / std::max(1.0, std::min(age, reach));
        if (pick == ticks.size() || score < best) { best = score; pick = i; }
    }
    return pick;
}

// The first recorded check still ahead of a playhead standing at `tick`: a check taken
// on tick T stands for the state after T + 1 ticks, the moment playback compares it.
template <class Check>
size_t checkAfter(const std::vector<Check>& checks, size_t tick) {
    return size_t(std::upper_bound(checks.begin(), checks.end(), tick, [](size_t at, const Check& c) {
        return at < size_t(c.tick) + 1;
    }) - checks.begin());
}

}  // namespace tak::replaybar
