#pragma once

// The Classic AI level (Difficulty::Classic): a port of the retail TA:Kingdoms
// skirmish AI, behaviour reverse-engineered from the retail binary and described in
// docs/classic-ai-plan.md. It keeps retail's own decisions, retail's AI bugs
// included, and has no difficulty knobs and no income cheat.
//
// Like every AI it runs ONLY on the server: it reads a const World and emits
// net::Commands, so its seat's sim config is exactly a Normal seat's
// (matchConfigFor), and nothing it holds is lockstep or hashed state.
//
// P0 (plumbing): the brain owns its random stream and is wired into
// ai::Controller, but tick() does nothing yet -- a Classic Monarch idles.

#include <cstdint>
#include <functional>

#include "net/lockstep.h"
#include "sim/retailrng.h"

namespace tak::sim { class World; }

namespace tak::ai {

class ClassicBrain {
public:
    // `roomSeed` is the seed the server hands every AI of the game; mixing in the
    // player keeps two Classic AIs of one game from mirroring each other.
    ClassicBrain(int player, uint32_t roomSeed);

    // Runs every sim tick (the brain keeps retail's own clocks, not a think period).
    void tick(const tak::sim::World& world, uint32_t simTick,
              const std::function<void(const tak::net::Command&)>& sink);

    // Retail's game RNG (Park-Miller, 0x535cc0): a value in [0,n); n < 2 returns 0
    // without a draw. A private, server-local stream; the draw order inside each
    // ported routine follows retail's.
    int rand(int n) { return int(tak::sim::retailRandom(seed_, n)); }
    uint32_t seed() const { return seed_; }
    int player() const { return player_; }

    static constexpr uint32_t seedFor(int player, uint32_t roomSeed) {
        return tak::sim::retailSeed(roomSeed ^ (0x9e3779b9u * uint32_t(player + 1)));
    }

private:
    int player_;
    uint32_t seed_;
};

}  // namespace tak::ai
