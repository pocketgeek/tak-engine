// Text driver for tools/re/check_corpse_sink.py: runs the engine's water
// grave (512ee0's predicate, src/sim/retailheight.h) and sinking
// (src/sim/retailfeaturesink.h) on maps the checker also loads into the
// original routines. Built by the checker.
//
// In: W H sea gravity isfeature x y z, then W*H heights, then the tick count.
// Out: one line per tick, "y vy fading alpha-bits state" (state 0 moving,
// 1 settled, 2 removed), ending at the first tick that is not moving.
#include "sim/retailfeaturesink.h"
#include "sim/retailheight.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <iostream>
#include <vector>

using namespace tak::sim;

int main() {
    int w, h, sea, gravity, isFeature, ticks;
    int32_t x, y, z;
    while (std::cin >> w >> h >> sea >> gravity >> isFeature >> x >> y >> z) {
        std::vector<int> heights(size_t(w) * size_t(h));
        for (auto& v : heights) std::cin >> v;
        std::cin >> ticks;
        auto at = [&](int cx, int cz) {
            return heights[size_t(std::min(cz, h - 1)) * size_t(w) + size_t(std::min(cx, w - 1))];
        };
        const bool water = retailTerrainHeight(x, z, w, h, at) <= sea;
        RetailFeatureSink f{x, y, z, 0, water && !isFeature ? kRetailSinkVelocity : 0, 0};
        auto ground = [&](int32_t px, int32_t pz) {
            return retailCellMidHeight(px, pz, w, h, [&](int cx, int cz) {
                const int a = at(cx, cz), b = at(cx + 1, cz), c = at(cx, cz + 1), d = at(cx + 1, cz + 1);
                return std::pair{std::min({a, b, c, d}), std::max({a, b, c, d})};
            });
        };
        for (int t = 0; t < ticks; ++t) {
            const auto r = retailFeatureSinkStep(f, ground, uint8_t(sea), gravity);
            std::printf("%d %d %d %u %d\n", f.y, f.vy, int(f.fading), std::bit_cast<uint32_t>(f.alpha), int(r));
            if (r != RetailSinkResult::Moving) break;
        }
        std::printf("end\n");
        std::fflush(stdout);
    }
}
