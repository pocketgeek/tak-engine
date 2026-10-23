#pragma once

// The Classic level's production choice (docs/classic-ai-plan.md §1.3, §1.6):
// retail's weighted menu draw 0x412d00 over a builder type's canbuild menu, and
// the factory planner 0x40dcb0 that queues its pick.
//
// Both are thin layers over the shared sim templates, which are what the
// emulation oracles check: sim::retailAiBuildWeight (0x412c90, check_ai_build_weight),
// sim::retailAiChooseBuild (0x412d00, check_ai_build_choice) and
// sim::retailAiPlanFactory (0x40dcb0, check_ai_factory_plan). The layer itself --
// the menu walk with the brain's own tables, the allocation the planner gates on,
// and the base think's dispatch to it -- is checked as one by
// tools/re/check_ai_factory_think.py (classic_ai_test --factory-think), with
// retail's own 0x40b320, 0x40dcb0, 0x412d00, 0x412c90, 0x409890 and 0x410de0
// running and the allocation written by retail's economy step 0x51d837.
//
// Server-side only, like the rest of the brain.

#include <cstdint>
#include <vector>

#include "sim/retailai.h"

namespace tak::ai {

// 0x412d00(player, unit, special): one pass over the builder type's menu (retail
// type +0x132, in canbuild [Menu] Priority order). `weight(t)` is 0x412c90 for
// catalogue type t (0 at its limit, else pref * priority / 100), `lodestone(t)`
// its draw class (yardmap[0] & 0x80) and `sameSide(t)` whether its side string
// equals the builder's (retail's strcmp of type +0x8a). Each kept entry -- weight
// > 0, of the requested class -- costs one rand(total) (none for the first, as
// rand(n < 2) does not draw); a winner from another side gives 0.
template<class Weight, class Lodestone, class SameSide, class Random>
uint16_t classicMenuDraw(const std::vector<uint16_t>& menu, bool special, Weight weight, Lodestone lodestone,
                         SameSide sameSide, Random random) {
    std::vector<sim::RetailAiBuildChoice> choices;
    choices.reserve(menu.size());
    for (uint16_t t : menu) choices.push_back({t, int32_t(weight(t)), bool(lodestone(t)), bool(sameSide(t))});
    return sim::retailAiChooseBuild(choices, special, random);
}

// 0x40dcb0, the factory planner, for a ready factory member of a base: nothing
// below allocation 0.7/3 or while the factory has a BuildingBuild mission (one
// unit at a time); else the ordinary draw over its `menu` (special 0, the
// callables as for classicMenuDraw), and under allocation 0.7 an item costing
// over 500 is deferred when rand(int((cost-500)/300+2)) != 0 and the owner's unit
// count is under rand(30)+30. Otherwise `queue(t)` is retail's
// 0x429810(type, factory, 1): one BuildingBuild of one unit.
//
// `allocation` is the owner's construction allocation (resource object +8, which
// the economy step 0x51d837 sets to min(1, stored / this tick's demand); not the
// store fill). `population` is the owner's unit count (player +0xe8).
template<class Weight, class Lodestone, class SameSide, class Cost, class Random, class Queue>
bool classicPlanFactory(float allocation, bool producing, uint16_t population, const std::vector<uint16_t>& menu,
                        Weight weight, Lodestone lodestone, SameSide sameSide, Cost cost, Random random,
                        Queue queue) {
    return sim::retailAiPlanFactory(allocation, producing, population,
        [&] { return classicMenuDraw(menu, false, weight, lodestone, sameSide, random); }, cost, random, queue);
}

}  // namespace tak::ai
