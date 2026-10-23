#include "ai/classicbrain.h"

#include "sim/sim.h"

namespace tak::ai {

ClassicBrain::ClassicBrain(int player, uint32_t roomSeed)
    : player_(player), seed_(seedFor(player, roomSeed)), profile_(catalogue_.size()) {}

ClassicBrain::ClassicBrain(int player, uint32_t roomSeed, const tak::sim::TypeRegistry& registry)
    : player_(player), seed_(seedFor(player, roomSeed)),
      catalogue_(ClassicCatalogue::from(registry)), profile_(catalogue_.size()) {}

void ClassicBrain::useProfile(const ClassicProfileScript& script) {
    profile_.reset(catalogue_.size());   // as retail's ReloadAIProfiles does first
    profile_.execute(script.text, catalogue_);
    profile_.path = script.path;
}

void ClassicBrain::tick(const tak::sim::World&, uint32_t,
                        const std::function<void(const tak::net::Command&)>&) {
    // P0: nothing yet. The ported scheduler (retail 0x40fe00 -> 0x40fd80) lands here.
}

}  // namespace tak::ai
