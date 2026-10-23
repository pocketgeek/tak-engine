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
// priorities recomputed on rand(30)==0 of each >= 7-tick window. P3: retail's
// AI manager (ai/classicsquads.h) -- the 100 squad slots the constructor builds
// and rolls, the scheduler (the assignment countdown, then every squad whose
// deadline has come), the dying-unit cleanup and the assignment pass: the
// Monarch into base 1 anchored on its start, every other finished unit into the
// nearest base with retail's standing orders (a Stance 0 command), and the
// campaign mass attack. P4a: the base think's member loop (ai/classicsquads.h)
// with retail's factory planner behind it (ai/classicproduction.h): a ready
// factory in a base draws from its menu with the brain's weights and queues one
// unit at a time with a Train command. P4b: a ready mobile builder of a base
// (the Monarch, its builders, Zhon's and Creon's conjurers) goes to retail's
// constructor planner (ai/classicproduction.h) with the Monarch's gates, and
// what it picks is placed by retail's placers (ai/classicplacement.h) on the
// block grids the build manager draws as it is built, and ordered with a Build
// command. Combat members and the defence planner (P5) are not planned yet, and
// nothing after the member loop runs yet.

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ai/classicplacement.h"
#include "ai/classicprofile.h"
#include "ai/classicsquads.h"
#include "ai/classictypes.h"
#include "net/lockstep.h"
#include "sim/retailrng.h"

namespace tak::sim { class World; class TypeRegistry; class NavGrid; }

namespace tak::ai {

class ClassicBrain {
public:
    // `roomSeed` is the seed the server hands every AI of the game; mixing in the
    // player keeps two Classic AIs of one game from mirroring each other.
    // Retail creates the build manager at game start with every type reset to
    // 100 / unlimited (0x4102f0 -> 0x410670); the profile file comes after.
    // Retail builds the AI manager first (0x4f6660: 0x40fed0, its squads and
    // their rolls), then the build manager. `largestEnemyPopulation` is what the
    // ground wave roll reads as E; at a skirmish start every player has at most a
    // Monarch, so any value under 10 gives retail's E = 10.
    ClassicBrain(int player, uint32_t roomSeed, const tak::sim::TypeRegistry& registry,
                 int32_t largestEnemyPopulation = 0);
    ClassicBrain(int player, uint32_t roomSeed);   // an empty catalogue (tests)

    // The player's start location (player+0xec and its record in 0x62dbdc):
    // start record `index` is the 16-pixel cell holding (x, z) pixels. Base 1 is
    // anchored on it by the first assignment pass. Without one base 1 is not
    // anchored and its centre is its members' centroid.
    void setStart(int index, float x, float z);
    // A campaign mission's `massattack` for this player, in ticks (0 = none;
    // classicMassAttackTicks parses the .ota keyword), and the scenario mode the
    // first assignment pass reads (1 = a campaign mission with preplaced units).
    void setMassAttack(uint32_t ticks) { squads_.massAttack = ticks; }
    void setScenarioMode(int32_t mode) { scenarioMode_ = mode; }
    const ClassicSquads& squads() const { return squads_; }
    // A unit's squad slot (retail unit +0xc8): 0 unassigned, -1 dropped while dying.
    int32_t squadOf(int unitId) const {
        const auto it = squadOf_.find(unitId);
        return it == squadOf_.end() ? 0 : it->second;
    }

    // Retail's profile loader (0x4f6860): runs the map's profile script over the
    // reset tables.
    void useProfile(const ClassicProfileScript& script);
    const ClassicCatalogue& catalogue() const { return catalogue_; }
    const ClassicProfile& profile() const { return profile_; }
    const std::vector<ClassicTypeInfo>& typeInfo() const { return info_; }
    // A builder type's menu as retail loads it (0x4c1d35, only for FBI builder
    // types): its canbuild entries in [Menu] Priority order, as catalogue types.
    const std::vector<uint16_t>& menu(size_t type) const {
        static const std::vector<uint16_t> none;
        return type < menu_.size() ? menu_[type] : none;
    }
    // Train commands the factory planner has issued (tests and the debug dump).
    uint32_t factoryQueues() const { return queues_; }
    // Build commands the constructor planner has issued, and how many of them
    // were lodestones (tests).
    uint32_t builds() const { return builds_; }
    uint32_t lodestoneBuilds() const { return lodestoneBuilds_; }
    // The block grids the build manager drew as it was built (0x4110f0).
    const ClassicBlockGrids& blockGrids() const { return grids_; }
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
    // 0x410810's store fill: stored / capacity, or 0 when the capacity is at most
    // 0.01 (a double compare).
    static float storeFill(float stored, float capacity) {
        return double(capacity) > 0.01 ? stored / capacity : 0.0f;
    }

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
    // 0x40fe00 -> 0x40fd80, retail's AI scheduler: the assignment countdown, then
    // each squad whose deadline has come (the shared sim templates).
    void scheduler(const tak::sim::World& world, uint32_t simTick,
                   const std::function<void(const tak::net::Command&)>& sink);
    // The player's units as the manager reads them, in unit order, with their
    // squads; members that left the world (or the player) are dropped first.
    std::vector<ClassicSquadUnit> squadUnits(const tak::sim::World& world);
    // 0x411a90, retail's per-player bookkeeping: the >= 7-tick window (where the
    // world rebuilds its visibility lists) draws rand(30), and 0 recomputes the
    // priorities from the cache as the previous tick left it; then the recount.
    void bookkeeping(const tak::sim::World& world, uint32_t simTick);
    // 0x40b320's member loop for base `slot` (after its reschedule), and the
    // factory planner 0x40dcb0 for one of its members. Retail's orders take
    // effect at once (0x429810 / 0x4d78a0 put the mission on the unit), so a
    // planner that orders a member updates its first mission in `units`: later
    // members of the same think read it (the idle-factory and busy-deposit tests).
    void baseThink(const tak::sim::World& world, int slot, std::vector<ClassicSquadUnit>& units,
                   const std::function<void(const tak::net::Command&)>& sink);
    void planFactory(const tak::sim::World& world, ClassicSquadUnit& unit,
                     const std::function<void(const tak::net::Command&)>& sink);
    // 0x40c890 for units[i], a ready mobile builder of base `slot`.
    void planConstructor(const tak::sim::World& world, int slot, std::vector<ClassicSquadUnit>& units,
                         size_t i, bool threat, const std::function<void(const tak::net::Command&)>& sink);
    // The map's sacred-site records (0x62dbcc) and which feature-plane types are
    // sacred, read from the World once.
    void readSites(const tak::sim::World& world);
    bool sacredCell(const tak::sim::World& world, int x, int z) const;
    // The navigator grid's static bit (what 0x4e1570's mode 1 reads) for a
    // `footX` x `footZ` mover on `nav`: 1 where the grid build set it at load.
    const std::vector<uint8_t>& staticBlocked(const tak::sim::World& world, const tak::sim::NavGrid& nav,
                                              int footX, int footZ);
    // The owner's unit count as retail keeps it (player +0xe8): its live units.
    uint16_t population(const tak::sim::World& world) const;
    void recount(const tak::sim::World& world);
    void recomputePriorities(const tak::sim::World& world);
    // 0x4102f0's pass at the manager's creation (from the registry constructor).
    void creationPass();
#ifndef NDEBUG
    void debugDump(uint32_t simTick);
    bool dumped_ = false;
#endif

    int player_;
    uint32_t seed_;
    ClassicCatalogue catalogue_;
    ClassicProfile profile_;
    std::vector<ClassicTypeInfo> info_;
    std::vector<std::vector<uint16_t>> menu_;
    uint32_t queues_ = 0, builds_ = 0, lodestoneBuilds_ = 0;
    ClassicBlockGrids grids_;
    bool sitesRead_ = false;
    std::vector<std::optional<ClassicCell>> sites_;
    std::vector<bool> sacredTypes_;
    std::vector<std::pair<ClassicCell, ClassicCell>> sacredRects_;   // without a feature plane
    struct StaticPlane { const tak::sim::NavGrid* nav; int footX, footZ; std::vector<uint8_t> blocked; };
    std::vector<StaticPlane> staticPlanes_;
    std::unordered_map<const tak::sim::UnitType*, uint16_t> index_;
    std::vector<int16_t> count_, completed_;
    std::vector<uint8_t> priority_;
    tak::sim::RetailPlayerCacheClock clock_{true, 0};
    uint32_t refreshes_ = 0;
    ClassicSquads squads_;
    std::unordered_map<int, int32_t> squadOf_;
    int32_t startIndex_ = -1;
    int32_t scenarioMode_ = 2;   // retail's skirmish map mode
};

}  // namespace tak::ai
