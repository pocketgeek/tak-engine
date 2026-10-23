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
// ai::Controller. P1: it holds retail's unit catalogue and the baked AI profile
// (ai/classicprofile.h). tick() still issues nothing -- a Classic Monarch idles.

#include <cstdint>
#include <functional>

#include "ai/classicprofile.h"
#include "net/lockstep.h"
#include "sim/retailrng.h"

namespace tak::sim { class World; class TypeRegistry; }

namespace tak::ai {

class ClassicBrain {
public:
    // `roomSeed` is the seed the server hands every AI of the game; mixing in the
    // player keeps two Classic AIs of one game from mirroring each other.
    // Retail creates the build manager at game start with every type reset to
    // 100 / unlimited (0x4102f0 -> 0x410670); the profile file comes after.
    ClassicBrain(int player, uint32_t roomSeed, const tak::sim::TypeRegistry& registry);
    ClassicBrain(int player, uint32_t roomSeed);   // an empty catalogue (tests)

    // Retail's profile loader (0x4f6860): runs the map's profile script over the
    // reset tables.
    void useProfile(const ClassicProfileScript& script);
    const ClassicCatalogue& catalogue() const { return catalogue_; }
    const ClassicProfile& profile() const { return profile_; }

    // Runs every sim tick (the brain keeps retail's own clocks, not a think period).
    void tick(const tak::sim::World& world, uint32_t simTick,
              const std::function<void(const tak::net::Command&)>& sink);

    // Retail's game RNG (Park-Miller, 0x535cc0): a value in [0,n); n < 2 returns 0
    // without a draw. A private, server-local stream; the draw order inside each
    // ported routine follows retail's.
    int rand(int n) { return int(tak::sim::retailRandom(seed_, n)); }
    uint32_t seed() const { return seed_; }
    int player() const { return player_; }

    // A deliberate divergence from retail's seeding (0x535d30), which can hand
    // out a seed that is a multiple of 2^31-1: Park-Miller maps such a seed (and
    // 0) to 0x7fffffff, which then never moves again, so every draw of the game
    // would repeat. retailSeed's |1 already rules out 0; the one odd multiple,
    // 0x7fffffff itself, is moved to a neighbouring odd seed.
    static constexpr uint32_t seedFor(int player, uint32_t roomSeed) {
        const uint32_t s = tak::sim::retailSeed(roomSeed ^ (0x9e3779b9u * uint32_t(player + 1)));
        return s % 0x7fffffffu == 0 ? s ^ 2u : s;
    }

private:
    int player_;
    uint32_t seed_;
    ClassicCatalogue catalogue_;
    ClassicProfile profile_;
};

}  // namespace tak::ai
