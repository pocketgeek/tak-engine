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
// (ai/classicprofile.h). P2: per-type facts (ai/classictypes.h) and retail's
// bookkeeping clock -- the per-tick unit count cache and the dynamic build
// priorities recomputed on rand(30)==0 of each >= 7-tick window. tick() still
// issues nothing -- a Classic Monarch idles.

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "ai/classicprofile.h"
#include "ai/classictypes.h"
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
    const std::vector<ClassicTypeInfo>& typeInfo() const { return info_; }
    // Retail's per-type cache (mgr+0x85 / +0x95, recounted every tick by 0x4119d0):
    // the player's live units of each type, and how many of those are finished.
    const std::vector<int16_t>& counts() const { return count_; }
    const std::vector<int16_t>& completed() const { return completed_; }
    // Retail's dynamic build priority per type (mgr+0x69, 0x410810).
    const std::vector<uint8_t>& priorities() const { return priority_; }
    uint32_t priorityRefreshes() const { return refreshes_; }
    // The draw weight of a type for this player (0x412c90): 0 at its limit, else
    // pref * priority / 100.
    int buildWeight(size_t type) const;

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
    // 0x411a90, retail's per-player bookkeeping: the >= 7-tick window (where the
    // world rebuilds its visibility lists) draws rand(30), and 0 recomputes the
    // priorities from the cache as the previous tick left it; then the recount.
    void bookkeeping(const tak::sim::World& world, uint32_t simTick);
    void recount(const tak::sim::World& world);
    void recomputePriorities(const tak::sim::World& world, bool atCreation);
#ifndef NDEBUG
    void debugDump(uint32_t simTick);
    bool dumped_ = false;
#endif

    int player_;
    uint32_t seed_;
    ClassicCatalogue catalogue_;
    ClassicProfile profile_;
    std::vector<ClassicTypeInfo> info_;
    std::unordered_map<const tak::sim::UnitType*, uint16_t> index_;
    std::vector<int16_t> count_, completed_;
    std::vector<uint8_t> priority_;
    tak::sim::RetailPlayerCacheClock clock_{true, 0};
    bool created_ = false;
    uint32_t refreshes_ = 0;
};

}  // namespace tak::ai
