// The replay transport's pure parts: bar geometry, the timeline's mapping, the speed
// steps, keyframe choice and thinning, and finding the next recorded check after a
// jump; and the watcher's rule table.
#include "client/alloccount.h"
#include "client/replaybar.h"
#include "client/watchonly.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace rb = tak::replaybar;

static int failures = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
}

static void geometry() {
    for (const float scale : {1.0f, 1.5f, 2.0f})
        for (const float mapW : {640.0f, 860.0f, 1280.0f, 3400.0f}) {
            const auto l = rb::layout(mapW, scale, 8);
            const std::string at = "scale " + std::to_string(scale) + " map " + std::to_string(mapW);
            check(l.box.w <= 760 * scale && l.box.w <= mapW - 32, at + ": bar too wide");
            check(l.box.x >= 16 && l.box.x + l.box.w <= mapW - 16, at + ": bar leaves the map view");
            check(std::abs((l.box.x + l.box.w / 2) - mapW / 2) < 0.51f, at + ": not centred");
            check(l.timeline.h >= 18 * scale, at + ": timeline hit area under 18 px at scale");
            check(l.track.y >= l.timeline.y && l.track.y + l.track.h <= l.timeline.y + l.timeline.h,
                  at + ": track outside its hit area");
            const rb::Rect* row[] = {&l.back, &l.play, &l.forward, &l.slower, &l.speed, &l.faster};
            float right = l.box.x;
            for (const auto* r : row) {
                check(r->x >= right, at + ": row 2 overlaps");
                check(r->y >= l.timeline.y + l.timeline.h, at + ": row 2 runs into the timeline");
                check(r->y + r->h <= l.box.y + l.box.h, at + ": row 2 leaves the bar");
                right = r->x + r->w;
            }
            // Room left for "123:45 / 123:45", a block-font cell being 6 px wide.
            check(l.timeRight - right >= 15 * 6 * l.textPx * 0.9f, at + ": no room for the time");
            // What the label may fill stops short of the last control (a longer one is drawn smaller).
            check(rb::timeRoom(l) > 0 && rb::timeRoom(l) < l.timeRight - right, at + ": the time's room runs into the controls");
        }
}

static void mapping() {
    const auto l = rb::layout(1280, 1, 8);
    for (const size_t length : {size_t(0), size_t(1), size_t(30), size_t(18002), size_t(216000)}) {
        check(rb::tickToX(l, 0, length) == l.track.x, "tick 0 is the track's left end");
        if (length) check(std::abs(rb::tickToX(l, length, length) - (l.track.x + l.track.w)) < 0.01f,
                          "the last tick is the track's right end");
        for (size_t tick = 0; tick <= length; tick += length / 7 + 1) {
            const size_t back = rb::xToTick(l, rb::tickToX(l, tick, length), length);
            const double tolerance = double(length) / double(l.track.w) + 1;
            check(std::abs(double(back) - double(tick)) <= tolerance, "x -> tick round trip");
        }
    }
    check(rb::xToTick(l, l.track.x - 500, 1000) == 0, "left of the track clamps to the start");
    check(rb::xToTick(l, l.track.x + l.track.w + 500, 1000) == 1000, "right of the track clamps to the end");
    check(rb::tickToX(l, 5000, 1000) <= l.track.x + l.track.w, "past the end clamps");
}

static void speeds() {
    check(rb::speedAt(rb::kNormalSpeed) == 1, "the default is 1x");
    check(rb::kSpeeds.back() == 256, "256x tops the table");
    int at = rb::kNormalSpeed;
    for (int i = 0; i < 40; ++i) at = rb::stepSpeed(at, +1);
    check(at == int(rb::kSpeeds.size()) - 1, "stepping up clamps at the top");
    for (int i = 0; i < 40; ++i) at = rb::stepSpeed(at, -1);
    check(at == 0, "stepping down clamps at the bottom");
    check(rb::frameBudgetMs(8, false) == 14 && rb::frameBudgetMs(16, false) == 40 &&
          rb::frameBudgetMs(1, true) == 40, "frame budgets");
    check(rb::speedText(64, 0) == "64X", "speed label");
    check(rb::speedText(0.25f, 0) == "0.25X", "slow speed label");
    check(rb::speedText(64, 27.4f) == "64X (27X)", "a shortfall shows the real rate");
    check(rb::speedText(64, 60) == "64X", "within 10% shows the request alone");
    check(rb::speedText(1, 0.75343f) == "1X (0.8X)", "under 1x the real rate is one decimal");
    check(rb::speedText(1, 0.01f) == "1X (0.1X)", "and never reads zero");
    for (const float speed : rb::kSpeeds)
        for (const float real : {0.01f, 0.25f, 0.76f, 0.96f, 1.4f, 27.4f, 240.0f})
            check(rb::speedText(speed, real).size() <= 12, "a speed label stays short");
    check(rb::timeText(30 * 210) == "3:30" && rb::timeText(30 * 1382) == "23:02", "time text");
    // From an hour on the label has hours (an 18 hour replay read "1080:34").
    check(rb::timeText(30 * 3599) == "59:59" && rb::timeText(30 * 3600) == "1:00:00", "hours start at 60 minutes");
    check(rb::timeText(1945042) == "18:00:34", "18 hours read as hours: " + rb::timeText(1945042));
    check(rb::timeText(30 * 210, 1945042) == "0:03:30" && rb::timeText(0, 30 * 3600) == "0:00:00",
          "every time of a replay that passes an hour reads the same way");
    check(rb::timeText(30 * 210, 30 * 3599) == "3:30", "a replay under an hour keeps m:ss");
    check(rb::timeText(30 * 360000 + 29) == "100:00:00", "past 99 hours");
}

static void keyframes() {
    const std::vector<uint32_t> ticks = {0, 900, 1800, 2700};
    check(rb::keyframeAt(ticks, 0) == 0 && rb::keyframeAt(ticks, 899) == 0, "before the second keyframe");
    check(rb::keyframeAt(ticks, 900) == 1, "on a keyframe");
    check(rb::keyframeAt(ticks, 100000) == 3, "past the last");
    // << snaps to a keyframe in reach, never one at or past the playhead.
    check(rb::snapRewind(ticks, 1700, 2300) == 1800, "<< snaps to the nearest keyframe");
    check(rb::snapRewind(ticks, 2000, 2400) == 1800, "<< snaps back too");
    check(rb::snapRewind(ticks, 1400, 1800) == 1400, "no keyframe within 15 s: the tick itself");
    check(rb::snapRewind(ticks, 2600, 2700) == 2600, "the keyframe under the playhead is no rewind");

    check(rb::keyframeBudget(uint64_t(8) << 30) == uint64_t(2) << 30, "a quarter of 8 GB");
    check(rb::keyframeBudget(uint64_t(64) << 30) == uint64_t(4) << 30, "the 4 GB ceiling");

    // << >> stack on the jump in hand; >> no further than a long step past the playhead.
    const int64_t step = rb::kStepTicks;
    const size_t reach = 1000 + rb::kLongStepTicks;
    check(rb::nudgeTarget(1000, 1000, step, 100000) == 1900, ">> from the playhead");
    check(rb::nudgeTarget(1000, 1900, step, 100000) == 2800, ">> stacks on the jump in hand");
    check(rb::nudgeTarget(1000, reach - 1, step, 100000) == reach, ">> stops a long step ahead");
    check(rb::nudgeTarget(1000, reach, step, 100000) == reach, "and stays there");
    check(rb::nudgeTarget(1000, 50000, step, 100000) == 50000, "a timeline jump further off is not shortened");
    check(rb::nudgeTarget(1000, 1000, rb::kLongStepTicks, 100000) == reach, "a long step is allowed");
    check(rb::nudgeTarget(99900, 99900, step, 100000) == 100000, ">> stops at the end");
    check(rb::nudgeTarget(1000, 1900, -step, 100000) == 1000, "<< takes off the jump in hand");
    check(rb::nudgeTarget(300, 300, -step, 100000) == 0, "<< stops at the start");

    // Thinning: fill a two hour game with far too many keyframes, playing all the way.
    const size_t length = 216000, capacity = 40;
    std::vector<uint32_t> kept = {0};
    for (uint32_t t = rb::kKeyframeTicks; t < length; t += rb::kKeyframeTicks) {
        kept.push_back(t);
        while (kept.size() > capacity) {
            const size_t drop = rb::thinChoice(kept, t);
            check(drop > 0 && drop + 1 < kept.size(), "the ends are never dropped");
            kept.erase(kept.begin() + std::ptrdiff_t(drop));
        }
    }
    check(kept.front() == 0, "tick 0 stays");
    uint32_t widest = 0, nearest = ~0u;
    for (size_t i = 1; i < kept.size(); ++i) {
        widest = std::max(widest, kept[i] - kept[i - 1]);
        if (kept[i] + 20 * 1800 >= kept.back()) nearest = std::min(nearest, kept[i] - kept[i - 1]);
    }
    check(widest <= 3 * length / capacity, "far from the playhead the keyframes spread evenly");
    check(nearest == rb::kKeyframeTicks, "near the playhead they stay dense");
    check(rb::thinChoice({0, 900}, 900) == 2, "nothing to drop");
}

// Straight playback with room for `capacity` keyframes: one laid every 30 s of game, thinned
// to fit. Returns the widest gap between neighbours, as a fraction of what was played, at
// its worst over the whole run; `kept` is what is held at the end.
template <class Thin>
static double widestGap(size_t capacity, uint32_t steps, const Thin& thin, std::vector<uint32_t>& kept) {
    kept = {0};
    double worst = 0;
    for (uint32_t s = 1; s <= steps; ++s) {
        const uint32_t t = s * rb::kKeyframeTicks;
        kept.push_back(t);
        while (kept.size() > capacity) {
            const size_t drop = thin(kept, t);
            if (drop == 0 || drop + 1 >= kept.size()) { check(false, "thinning gives up an end, or nothing"); return 1; }
            kept.erase(kept.begin() + std::ptrdiff_t(drop));
        }
        uint32_t widest = 0;
        for (size_t i = 1; i < kept.size(); ++i) widest = std::max(widest, kept[i] - kept[i - 1]);
        check(widest <= rb::thinGapBound(t, capacity), "a gap past the bound: capacity " + std::to_string(capacity) +
              " tick " + std::to_string(t) + " gap " + std::to_string(widest));
        if (s >= 4 * capacity) worst = std::max(worst, double(widest) / double(t));
    }
    return worst;
}

// The property the rewind stands on: however long the replay and however few keyframes
// fit, they stay spread over everything played. (They used to bunch at the playhead: on
// an 18 hour replay with room for four, a jump to hour 17 ran from tick 0.)
static void spread() {
    const auto thin = [](const std::vector<uint32_t>& ticks, uint32_t playhead) { return rb::thinChoice(ticks, playhead); };
    const uint32_t eighteenHours = 1945042 / rb::kKeyframeTicks;
    for (const size_t capacity : {size_t(3), size_t(4), size_t(5), size_t(8), size_t(10), size_t(13), size_t(17), size_t(40)})
        for (const uint32_t steps : {uint32_t(120), uint32_t(400), eighteenHours}) {
            std::vector<uint32_t> kept;
            const double worst = widestGap(capacity, steps, thin, kept);
            const std::string at = "capacity " + std::to_string(capacity) + ", " + std::to_string(steps) + " captures";
            if (steps >= 4 * capacity)
                check(worst <= 2.0 / double(capacity - 1) + 1e-9, at + ": widest gap " + std::to_string(worst) + " of the played length");
            check(kept.size() == std::min<size_t>(capacity, steps + 1) && kept.front() == 0 &&
                  kept.back() == steps * rb::kKeyframeTicks, at + ": tick 0 and the newest are held, and the room is used");
            // A jump back to any time restores the keyframe behind it and re-simulates the rest.
            uint64_t farthest = 0;
            for (uint32_t target = 0; target <= kept.back(); target += 450)
                farthest = std::max<uint64_t>(farthest, target - kept[rb::keyframeAt(kept, target)]);
            check(farthest <= rb::thinGapBound(kept.back(), capacity), at + ": a jump re-simulates " + std::to_string(farthest) + " ticks");
        }
    // The 18 hour replay with the room the default budget gives it: no jump runs more than a quarter of it.
    {
        std::vector<uint32_t> kept;
        const double worst = widestGap(9, eighteenHours, thin, kept);
        check(worst <= 0.25 + 1e-9, "nine keyframes over 18 hours: widest gap " + std::to_string(worst));
        // Near the playhead a keyframe is close: the newest, and the one before it within five minutes more often than not.
        check(kept.back() - kept[kept.size() - 2] <= rb::thinGapBound(kept.back(), 9), "the newest two");
    }
    // Rewinding and playing on thins round the playhead and keeps the spread too.
    {
        const size_t capacity = 8;
        std::vector<uint32_t> kept;
        widestGap(capacity, 400, thin, kept);
        const uint32_t end = kept.back();
        for (uint32_t t = 100 * rb::kKeyframeTicks; t <= 160 * rb::kKeyframeTicks; t += rb::kKeyframeTicks) {
            if (std::binary_search(kept.begin(), kept.end(), t)) continue;
            kept.insert(std::lower_bound(kept.begin(), kept.end(), t), t);
            while (kept.size() > capacity) {
                const size_t drop = rb::thinChoice(kept, t);
                check(drop > 0 && drop + 1 < kept.size(), "the ends stay while a rewound stretch plays");
                kept.erase(kept.begin() + std::ptrdiff_t(drop));
            }
            uint32_t widest = 0;
            for (size_t i = 1; i < kept.size(); ++i) widest = std::max(widest, kept[i] - kept[i - 1]);
            check(widest <= rb::thinGapBound(end, capacity), "a rewound stretch keeps the spread");
        }
        check(kept.back() == end, "what was played furthest stays reachable");
    }
}

// Making room for the next keyframe: capture goes on whatever fits.
static void room() {
    const uint64_t mb = uint64_t(1) << 20;
    // Room for all: nothing is given up.
    auto plan = rb::thinPlan({0, 900, 1800}, {80 * mb, 400 * mb, 400 * mb}, 4096 * mb, 400 * mb, 2700, 2700);
    check(plan.fits && plan.drop.empty(), "room for all");
    // Full: one goes, never tick 0, and the one being laid counts as the newest.
    plan = rb::thinPlan({0, 900, 1800, 2700}, {80 * mb, 400 * mb, 400 * mb, 400 * mb}, 1300 * mb, 400 * mb, 3600, 3600);
    check(plan.fits && plan.drop.size() == 1 && plan.drop[0] != 0, "one is given up for the next");
    // Room for tick 0 and one more (an 8 GB machine on a 2048x2048 map used to end capture here for good):
    // the one held makes way for the newer one, every time.
    {
        std::vector<uint32_t> ticks = {0};
        std::vector<uint64_t> bytes = {240 * mb};
        const uint64_t budget = 2048 * mb, size = 1100 * mb;
        for (uint32_t t = rb::kKeyframeTicks; t <= 40 * rb::kKeyframeTicks; t += rb::kKeyframeTicks) {
            const auto p = rb::thinPlan(ticks, bytes, budget, size, t, t);
            check(p.fits, "tick 0 and one more fit: tick " + std::to_string(t));
            for (auto it = p.drop.rbegin(); it != p.drop.rend(); ++it) {   // ascending when one at a time
                check(*it != 0, "tick 0 is never given up");
                ticks.erase(ticks.begin() + std::ptrdiff_t(*it)); bytes.erase(bytes.begin() + std::ptrdiff_t(*it));
            }
            ticks.push_back(t); bytes.push_back(size);
            uint64_t held = 0; for (const uint64_t b : bytes) held += b;
            check(held <= budget, "the budget holds");
        }
        check(ticks.size() == 2 && ticks.back() == 40 * rb::kKeyframeTicks, "the keyframe kept follows the playhead");
    }
    // Not even one fits beside tick 0: nothing is given up for a capture that cannot happen.
    plan = rb::thinPlan({0, 900}, {240 * mb, 300 * mb}, 1024 * mb, 1100 * mb, 1800, 1800);
    check(!plan.fits && plan.drop.empty(), "no room to make: the keyframes held stay");
    // The one just laid came out larger than the budget has room for: the pass after gives up others first.
    plan = rb::thinPlan({0, 900, 1800, 2700}, {80 * mb, 400 * mb, 400 * mb, 700 * mb}, 1300 * mb, 0, 2700, 2700);
    check(plan.fits && plan.drop.size() == 1 && plan.drop[0] != 0 && plan.drop[0] != 3, "a larger one makes its own room");
    // After a rewind a keyframe is laid between two held ones; when it is the one thinning would
    // drop again, it is not laid at all.
    plan = rb::thinPlan({0, 90000}, {80 * mb, 400 * mb}, 600 * mb, 400 * mb, 45000, 45000);
    check(!plan.fits && plan.drop.empty(), "a keyframe that would go at once is not laid");
    // Several at once, when the newest is much larger than those held.
    plan = rb::thinPlan({0, 900, 1800, 2700, 3600}, {80 * mb, 100 * mb, 100 * mb, 100 * mb, 100 * mb}, 600 * mb, 400 * mb, 4500, 4500);
    check(plan.fits && plan.drop.size() == 3, "as many go as the next one needs");
    for (const size_t i : plan.drop) check(i >= 1 && i <= 4, "tick 0 stays through a larger thinning");
}

struct Check { uint32_t tick; };
static void checks() {
    const std::vector<Check> list = {{29}, {59}, {89}};   // the state after ticks 30, 60, 90
    check(rb::checkAfter(list, 0) == 0, "from the start, the first check is next");
    check(rb::checkAfter(list, 29) == 0, "one tick before a check: it is next");
    check(rb::checkAfter(list, 30) == 1, "standing on a check's state: the next one");
    check(rb::checkAfter(list, 31) == 1, "just past");
    check(rb::checkAfter(list, 90) == 3, "after the last");
    check(rb::checkAfter(std::vector<Check>{}, 90) == 0, "a spectator recording has none");
}

static void rules() {
    using tak::Act;
    using tak::client::watchAllows;
    for (const Act act : {Act::Move, Act::Attack, Act::FightMove, Act::Patrol, Act::Guard, Act::Stop, Act::Heal,
                          Act::Load, Act::Unload, Act::ClearOrders, Act::CycleWeapon, Act::ToggleCloak,
                          Act::ToggleGate, Act::SelfDestruct, Act::GiveUnits, Act::Disco, Act::Headbang,
                          Act::MakePoster})
        check(!watchAllows(act), "an order or an order's input is blocked: " + std::to_string(int(act)));
    for (const Act act : {Act::NextUnit, Act::TrackSelection, Act::SelectAll, Act::SelectMonarch, Act::SelectFlying,
                          Act::ToggleCounts, Act::UnitInfo, Act::FullScreenRadar, Act::RetailZoom, Act::Count})
        check(watchAllows(act), "a look is allowed: " + std::to_string(int(act)));
    const auto owner = [](std::vector<int> v) { return tak::client::boxOwner(v); };
    check(owner({}) == -1, "an empty box has no owner");
    check(owner({3, 3, 1}) == 3, "the most units win");
    check(owner({3, 1}) == 1, "a tie goes to the lower slot");
    check(owner({8, 8, 2}) == 8, "neutral can win a box");
    check(tak::client::watchMayPace(true, true), "the host of a private game paces it");
    check(!tak::client::watchMayPace(false, true), "not a multiplayer room, even a host");
    check(!tak::client::watchMayPace(true, false) && !tak::client::watchMayPace(false, false), "nor a guest");
}

// The keyframe budget stands on this, so every route a World::clone allocates by.
struct alignas(64) Aligned { char bytes[128]; };
// Clang at -O2 may drop an allocation nothing reads; storing each pointer here keeps them.
static const void* volatile allocationSink = nullptr;
static void allocations() {
    std::vector<char> before(1000);
    uint64_t counted = 0;
    {
        tak::client::AllocCount count;
        std::vector<char> plain(1 << 20);
        const auto array = std::make_unique<int[]>(1000);
        std::vector<Aligned> aligned(100);
        const std::vector<Aligned> copy = aligned;
        allocationSink = plain.data(); allocationSink = array.get();
        allocationSink = aligned.data(); allocationSink = copy.data();
        std::thread([] { std::vector<char> elsewhere(1 << 24); allocationSink = elsewhere.data(); }).join();
        counted = count.bytes();
        const uint64_t want = (1 << 20) + 1000 * sizeof(int) + 2 * 100 * sizeof(Aligned);
        check(counted >= want, "plain, array and over-aligned allocations are all counted");
        check(counted < want + (1 << 16), "and another thread's are not: " + std::to_string(counted));
    }
    std::vector<char> after(1 << 20);
    check(counted < uint64_t(1) << 21, "nothing is counted after the scope");
    uint64_t fresh = 1;
    {
        tak::client::AllocCount count;
        fresh = count.bytes();
    }
    check(fresh == 0, "a new count starts at zero");
}

int main() {
    geometry(); mapping(); speeds(); keyframes(); spread(); room(); checks(); rules(); allocations();
    if (failures) { std::fprintf(stderr, "replaybar: %d failures\n", failures); return 1; }
    std::puts("replaybar passed");
    return 0;
}
