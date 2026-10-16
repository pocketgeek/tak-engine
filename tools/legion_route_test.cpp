// legion_route_test -- the route-query interface (src/sim/legion_route.h, Legion 10,000 stage 5): what a RouteView
// answers on a building, paused, complete, bounded and stale route. Unknown is never Unreachable; a tentative
// estimate is never a known distance; coverage and progress follow their stated rules.
#include "sim/legion_route.h"
#include <cstdio>
#include <cstdlib>

using namespace tak::sim;

namespace {
int failures = 0;
#define CHECK(c)                                                                 \
    do {                                                                         \
        if (!(c)) {                                                              \
            std::fprintf(stderr, "legion_route_test:%d: FAILED %s\n", __LINE__, #c); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

// A 10 x 10 map; the route's window is x 2..7, z 2..7. Seed at (2,2); potentials are 5 per orthogonal step along
// x then z (a stand-in: the view does not care how they were made). The heuristic aims at (7,7).
RouteGrid grid() {
    RouteGrid g;
    g.W = 10; g.x0 = 2; g.z0 = 2; g.fw = 6; g.fh = 6;
    g.potential.assign(36, route::kNone);
    for (int z = 0; z < 6; ++z)
        for (int x = 0; x < 6; ++x) g.potential[size_t(z * 6 + x)] = uint32_t(5 * (x + z));
    g.aimed = true; g.tx = 7; g.tz = 7;
    g.serial = 41; g.epoch = 9; g.work = 100;
    return g;
}
size_t cell(int x, int z) { return size_t(z * 10 + x); }
}  // namespace

int main() {
    {   // A complete, unbounded route on the current geometry.
        RouteGrid g = grid();
        g.done = true;
        g.potential[size_t(3 * 6 + 3)] = route::kNone;   // (5,5): not reached
        const RouteView v(g);
        CHECK(v.complete() && v.usable() && !v.paused());
        CHECK(v.estimate(cell(4, 4)) == 20 && v.known(cell(4, 4)) == 20 && v.settled(cell(4, 4)));
        CHECK(v.reach(cell(4, 4), 9) == Reach::Reachable);
        CHECK(v.reach(cell(5, 5), 9) == Reach::Unreachable);   // proven: complete, spans the component, fresh
        CHECK(v.reach(cell(0, 0), 9) == Reach::Unreachable);   // outside the component's window
        CHECK(v.reach(cell(5, 5), 10) == Reach::Unknown);      // built on older geometry: unknown, not unreachable
        CHECK(v.covered(cell(4, 4), 1000000));                 // a complete route covers every reached cell
        CHECK(!v.covered(cell(5, 5), 0));
        const auto pr = v.progress(cell(4, 4));
        CHECK(pr && pr->value == 20 && pr->metric == 41);
        CHECK(!v.progress(cell(5, 5)));
        CHECK(v.metric() == 41 && v.geometryEpoch() == 9 && v.version() == 100);
    }
    {   // A complete but bounded route: an unreached cell is unknown (the window may cut the way).
        RouteGrid g = grid();
        g.done = true; g.bounded = true;
        g.potential[size_t(3 * 6 + 3)] = route::kNone;
        const RouteView v(g);
        CHECK(v.reach(cell(5, 5), 9) == Reach::Unknown);
        CHECK(v.reach(cell(0, 0), 9) == Reach::Unknown);
    }
    {   // A build in progress: current key 40. key(x,z) = 5(x-2 + z-2) + h, h = octile to (7,7).
        RouteGrid g = grid();
        g.current = 40;
        const RouteView v(g);
        CHECK(!v.complete() && !v.usable() && !v.paused());
        // (2,2): potential 0, h = 5*5 + 2*5 = 35 -> key 35 <= 40: settled.
        CHECK(v.settled(cell(2, 2)) && v.known(cell(2, 2)) == 0);
        // (4,4): potential 20, h = 3*5 + 2*3 = 21 -> key 41 > 40: reached, not settled.
        CHECK(v.estimate(cell(4, 4)) == 20 && !v.settled(cell(4, 4)) && v.known(cell(4, 4)) == route::kNone);
        CHECK(v.reach(cell(4, 4), 9) == Reach::Reachable);      // a finite estimate is a real way
        CHECK(!v.progress(cell(4, 4)));                          // progress only from settled cells
        // (2,2) has 5 of key to spare.
        CHECK(v.covered(cell(2, 2), 5) && !v.covered(cell(2, 2), 6));
        // Unreached in a building route: unknown, whatever the window.
        g.potential[size_t(3 * 6 + 3)] = route::kNone;
        CHECK(RouteView(g).reach(cell(5, 5), 9) == Reach::Unknown);
    }
    {   // Paused: usable, still not complete; nothing unsettled becomes known.
        RouteGrid g = grid();
        g.current = 40; g.covered = true;
        const RouteView v(g);
        CHECK(v.usable() && v.paused() && !v.complete());
        CHECK(v.known(cell(4, 4)) == route::kNone);
        g.potential[size_t(3 * 6 + 3)] = route::kNone;
        CHECK(RouteView(g).reach(cell(5, 5), 9) == Reach::Unknown);
    }
    {   // An empty view: no state queries crash, nothing is usable.
        const RouteView v;
        CHECK(!v && !v.complete() && !v.usable() && !v.paused());
    }
    if (failures) {
        std::fprintf(stderr, "legion_route_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("legion_route_test: ok\n");
    return 0;
}
