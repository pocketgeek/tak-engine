#include "ai/classicbrain.h"

#include "sim/sim.h"

namespace tak::ai {

ClassicBrain::ClassicBrain(int player, uint32_t roomSeed)
    : player_(player), seed_(seedFor(player, roomSeed)) {}

void ClassicBrain::tick(const tak::sim::World&, uint32_t,
                        const std::function<void(const tak::net::Command&)>&) {
    // P0: nothing yet. The ported scheduler (retail 0x40fe00 -> 0x40fd80) lands here.
}

}  // namespace tak::ai
