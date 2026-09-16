// pathfallback_test -- the bounded A* fallback must stay, and must earn its place.
//
// Retail's search is a bug algorithm (see src/sim/pathsearch.h): it marches at the
// goal and traces an obstacle's outline when blocked. That is faithful and it is
// fine on open ground, but in a serpentine it SUCCEEDS while returning a route
// several times longer than the grid allows -- the unit walks a bit, re-asks, and
// gets another long one. World::kDetoursBeforeAStar exists for exactly that: after
// a couple of excessively long routes running, a unit switches to the bounded
// planner.
//
// This is OURS, not retail's, and it was nearly deleted twice on that basis. It is
// kept deliberately (docs/retail-engine.md, "Two tests on how close our movement
// now is"), and the measurements there are the reason:
//
//     serpentine        with fallback   without
//     arrival                   12/12     12/12
//     t50                        72.8      226.
//     travel vs straight        x1.89     x6.19
//
// Note the arrival column. Deleting the fallback does NOT fail any correctness
// test -- every unit still gets there -- so nothing in the suite noticed, and only
// a benchmark did. That is what this test is for: it asserts the PATH QUALITY the
// fallback buys, so removing it turns a test red instead of quietly tripling how
// long a maze takes.
//
// The bound is against the OPTIMAL path, not the straight line. In a serpentine the
// straight line is available to nobody, so comparing to it would overstate the
// fault and make the threshold meaningless.

#include "sim/sim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace tak::sim;

static int g_fail = 0;
static void check(bool cond, const std::string& what, const std::string& detail = {}) {
    std::printf("  %-52s %s%s%s\n", what.c_str(), cond ? "ok" : "FAIL",
                detail.empty() ? "" : " -- ", detail.c_str());
    if (!cond) ++g_fail;
}

static UnitType soldier() {
    UnitType t{};
    t.name = "sol"; t.id = "sol";
    t.maxVel = Fixed::fromFloat(70.0f / 30.0f);
    t.turnRate = 10000;
    t.maxHp = 100; t.canMove = true;
    t.footX = 2; t.footZ = 2;
    return t;
}

// Shortest legal path on the nav grid, 8-way with 10/14 integer costs -- the yardstick.
// Integer throughout so the answer cannot drift between builds.
static float shortestPath(const World& w, const UnitType* t,
                          float sx, float sz, float gx, float gz) {
    const NavGrid& g = w.navFor(t);
    if (g.empty()) return 0;
    const int W = g.width(), H = g.height();
    const int foot = std::max(1, std::max(t->footX, t->footZ));
    const int s = int(sx) / 16, sZ = int(sz) / 16;
    const int e = int(gx) / 16, eZ = int(gz) / 16;
    const int kInf = 1 << 29;
    std::vector<int> dist(size_t(W) * size_t(H), kInf);
    std::vector<uint8_t> done(size_t(W) * size_t(H), 0);
    auto idx = [&](int x, int z) { return size_t(z) * size_t(W) + size_t(x); };
    dist[idx(s, sZ)] = 0;
    for (;;) {
        int best = -1, bestD = kInf;
        for (size_t i = 0; i < dist.size(); ++i)
            if (!done[i] && dist[i] < bestD) { bestD = dist[i]; best = int(i); }
        if (best < 0) break;
        done[size_t(best)] = 1;
        const int cx = best % W, cz = best / W;
        if (cx == e && cz == eZ) return float(bestD) * 1.6f;   // cost 10 == 16px
        static const int dx8[8] = {0, 1, 0, -1, 1, 1, -1, -1};
        static const int dz8[8] = {-1, 0, 1, 0, -1, 1, 1, -1};
        for (int k = 0; k < 8; ++k) {
            const int nx = cx + dx8[k], nz = cz + dz8[k];
            if (nx < 0 || nz < 0 || nx >= W || nz >= H) continue;
            if (!g.fits(nx, nz, foot)) continue;
            const int nd = bestD + (k < 4 ? 10 : 14);
            if (nd < dist[idx(nx, nz)]) dist[idx(nx, nz)] = nd;
        }
    }
    return 0;
}

int main() {
    std::printf("pathfallback_test -- the bounded planner behind the retail tracer\n");

    const int W = 200, H = 200;
    World w;
    w.setVisPlayer(-1);
    w.setTerrain(std::vector<uint8_t>(size_t(W) * size_t(H), 100), W, H, /*seaLevel=*/20);
    // Horizontal baffles with alternating gaps -- the tracer has to wind through,
    // which is the shape that makes it return long routes over and over.
    for (int row = 0; row < 6; ++row) {
        const int z = 40 + row * 20;
        const int gapAt = (row % 2) ? 20 : 170;
        for (int x = 10; x < 190; ++x)
            if (x < gapAt || x > gapAt + 5) w.blockCells(x, z, 1, 1, true);
    }
    w.setPathService(true);

    UnitType s = soldier();
    const float sx = 500.0f, sz = 300.0f, gx = 1600.0f, gz = 2600.0f;
    const int id = w.spawn(&s, sx, sz, 0, 0);
    w.order(id, gx, gz, false);

    const float optimal = shortestPath(w, &s, sx, sz, gx, gz);
    check(optimal > 0, "the maze has a legal route at all",
          std::to_string(int(optimal)) + "px optimal");

    float travelled = 0;
    float px = sx, pz = sz;
    int arriveTick = -1;
    const int kTicks = 30 * 300;   // 300s of sim; the fallback path needs ~73s
    for (int i = 0; i < kTicks && arriveTick < 0; ++i) {
        w.tick(1.0f / 30.0f);
        const Unit* u = w.unit(id);
        if (!u || !u->alive()) break;
        const float cx = u->x.toFloat(), cz = u->z.toFloat();
        travelled += std::sqrt((cx - px) * (cx - px) + (cz - pz) * (cz - pz));
        px = cx; pz = cz;
        const float dx = cx - gx, dz = cz - gz;
        if (std::sqrt(dx * dx + dz * dz) < 60.0f) arriveTick = i;
    }

    check(arriveTick >= 0, "the unit gets through the serpentine",
          arriveTick >= 0 ? (std::to_string(arriveTick / 30) + "s") : "never arrived");

    const float ratio = optimal > 0 ? travelled / optimal : 0.0f;
    char buf[128];
    std::snprintf(buf, sizeof buf, "%.0fpx travelled for a %.0fpx optimum = x%.2f",
                  travelled, optimal, double(ratio));
    // WHERE THE BOUND COMES FROM, both halves measured on this exact maze:
    //
    //     kDetoursBeforeAStar = 2 (shipped)   4160px / 3546 = x1.17, arrives 62s
    //     kDetoursBeforeAStar unreachable    15466px / 3546 = x4.36, arrives 224s
    //
    // x2 sits between them with room on both sides: it fails if the escalation stops
    // happening, and does not flap on ordinary drift. Note the unit ARRIVES either
    // way -- an arrival-only assertion would pass with the fallback deleted, which is
    // how this went unnoticed until a benchmark measured the path length.
    check(ratio > 0 && ratio < 2.0f,
          "...without wandering several times the optimal path", buf);

    std::printf(g_fail ? "pathfallback_test: %d FAILURE(S)\n" : "pathfallback_test: all passed\n",
                g_fail);
    return g_fail ? 1 : 0;
}
