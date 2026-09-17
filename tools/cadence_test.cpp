// cadence_test -- retail's repath cadence ladder (icd 0x4e51f3-0x4e5491).
//
// Two behaviours the ladder defines, each porting a traced branch:
//
//   1. A unit walled off by PARKED bodies keeps its order and keeps GENTLY
//      probing. Its searches fail with no crowd evidence (parked bodies grade
//      0/2, never 5), so the failure report sets no bits (0x414450's report
//      branch, emulated) and the PLAIN branch governs: elapsed >= 120 ticks,
//      then a 1-in-120 roll per frame (0x4e545b). Expected pace: a few asks a
//      minute -- not silence (a real bug we shipped briefly: mapping failure to
//      quiet stranded long hauls), and not a hammer (the 2s retry we once had).
//
//   2. A healthy route on a long march re-asks on the same tail, which is how a
//      trip longer than one search's visit limit chains route by route.
//
// Synthetic world, synthetic types: the ladder reads halfCellTicks (default 6,
// a standard walker), floater and maxwaterdepth.

#include "sim/sim.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace tak::sim;

static int g_fail = 0;
static void check(bool cond, const char* what, const std::string& detail = {}) {
    std::printf("  %-62s %s%s%s\n", what, cond ? "ok" : "FAIL",
                detail.empty() ? "" : " -- ", detail.c_str());
    if (!cond) ++g_fail;
}

static UnitType walkerType() {
    UnitType t{};
    t.name = "walker"; t.id = "walker";
    t.maxVel = Fixed::fromFloat(48.0f / 30.0f);
    t.turnRate = 10000;
    t.maxHp = 100; t.canMove = true;
    t.footX = 2; t.footZ = 2;
    return t;
}

static World* makeWorld(int W, int H) {
    World* w = new World();
    w->setVisPlayer(-1);
    w->setTerrain(std::vector<uint8_t>(size_t(W) * size_t(H), 100), W, H, 20);
    w->setPathService(true);
    return w;
}

int main() {
    std::printf("cadence_test\n");

    // ---- 1. walled off by parked bodies: gentle probing, no churn, no rest ----
    {
        World& w = *makeWorld(160, 160);
        UnitType wt = walkerType();
        std::vector<int> wall;
        for (int d = -3; d <= 3; ++d) {
            for (int ring = 0; ring < 2; ++ring) {
                const float r = 96.0f + 32.0f * float(ring);
                wall.push_back(w.spawn(&wt, 1600.0f + float(d) * 32.0f, 1600.0f - r, 0, 1));
                wall.push_back(w.spawn(&wt, 1600.0f + float(d) * 32.0f, 1600.0f + r, 0, 1));
                wall.push_back(w.spawn(&wt, 1600.0f - r, 1600.0f + float(d) * 32.0f, 0, 1));
                wall.push_back(w.spawn(&wt, 1600.0f + r, 1600.0f + float(d) * 32.0f, 0, 1));
            }
        }
        const int u = w.spawn(&wt, 600, 1600, 0, 0);
        for (int i = 0; i < 60; ++i) w.tick(1.0f / 30.0f);
        w.order(u, 1600, 1600, false);
        for (int i = 0; i < 30 * 30; ++i) w.tick(1.0f / 30.0f);   // walk in, settle
        const Unit* p = w.unit(u);
        const float rx = p->x.toFloat(), rz = p->z.toFloat();
        const uint64_t asksAtRest = w.pathStats().requests();
        for (int i = 0; i < 30 * 60; ++i) w.tick(1.0f / 30.0f);   // a minute at the wall
        const uint64_t probes = w.pathStats().requests() - asksAtRest;
        // The 1-in-120 tail past a 120-tick threshold re-arms after every ask:
        // expected ~7 asks a minute, wide variance. 1..30 is the honest band --
        // zero is the failure-means-quiet bug, and anything like the old 2s
        // retry (30/min) means a hammer came back.
        check(probes >= 1 && probes <= 30,
              "a walled-off unit probes gently on the 1-in-120 tail",
              std::to_string(probes) + " asks in 60s");
        check(p && !p->orders.empty(),
              "...keeps its order (retail's: the wall might move)",
              p ? std::to_string(p->orders.size()) + " orders" : "dead");
        const float drift = std::max(std::fabs(p->x.toFloat() - rx),
                                     std::fabs(p->z.toFloat() - rz));
        check(drift < 24.0f,
              "...and stands its ground between probes",
              "drifted " + std::to_string(int(drift)) + "px in 60s");
        delete &w;
    }

    // ---- 2. a long healthy march re-anchors on the same tail ----
    {
        World& w = *makeWorld(160, 160);
        UnitType wt = walkerType();
        const int u = w.spawn(&wt, 300, 300, 0, 0);
        for (int i = 0; i < 60; ++i) w.tick(1.0f / 30.0f);
        const uint64_t asks0 = w.pathStats().requests();
        w.order(u, 2300, 2300, false);   // ~2800px, ~58s at this speed
        for (int i = 0; i < 30 * 40; ++i) w.tick(1.0f / 30.0f);
        const uint64_t asks = w.pathStats().requests() - asks0;
        check(asks >= 3,
              "a long healthy march re-asks on the 1-in-120 tail",
              std::to_string(asks) + " asks in 40s");
        delete &w;
    }

    std::printf(g_fail ? "cadence_test: %d FAILURE(S)\n" : "cadence_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
