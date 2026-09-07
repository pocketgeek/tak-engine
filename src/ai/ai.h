#pragma once

// The skirmish AI, extracted from the SDL viewer so it can also run headless on
// the multiplayer server. A Controller reads a const World and EMITS net::Commands
// through a sink instead of mutating the world directly -- so the same AI drives
// a player whether the commands are applied in-process (single-player) or fed into
// the server's command sequencer (server-hosted AI). See docs/multiplayer-design.md.
//
// The AI runs ONLY on the server (the referee), never on clients: its *decisions*
// are server-local, and only the net::Commands it emits are relayed and applied by
// every peer -- so lockstep never depends on the AI being reproducible. We still
// keep it RNG-driven so a --mpai run is repeatable for testing.

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "net/lockstep.h"
#include "sim/sim.h"

namespace tak::ai {

// The retail AI profile (ai/default.txt): per-unit build weight and hard limit.
// weight = probability share in the weighted-random build pick; limit = hard cap
// (-1 = unlimited). Missing weight => the AI never builds that unit.
struct Profile {
    std::unordered_map<std::string, int> weight, limit;
};

// Parse ai/default.txt from the runtime VFS (base + IP merged). One file covers
// every faction. Never throws; a missing file yields an empty profile (the AI
// then builds nothing).
Profile loadProfile(const tak::hpi::Vfs& vfs);

// Opponent skill, loosely modelled on retail's easy/normal/hard. It scales HOW the
// AI plays (economy pace, army size before it commits, aggression, reaction rate)
// rather than cheating its economy -- so all three respect the same rules the human
// does. See paramsFor().
enum class Difficulty : uint8_t { Easy = 0, Normal = 1, Hard = 2 };

// Behaviour knobs derived from Difficulty (paramsFor).
struct DiffParams {
    int  thinkPeriod;      // sim ticks between decisions (30 = 1 Hz); lower = faster reactions
    int  waveSize;         // idle fighters gathered before an attack wave commits
    int  econRich;         // army build-weight multiplier when mana is plentiful
    int  producersPerThink;// how many idle producers act each think (economy/APM pace)
    int  limitScale;       // percent applied to the profile's unit limits (100 = as shipped)
    bool scout;            // send an early lone scout toward an enemy start
};
DiffParams paramsFor(Difficulty d);

// Sink for the commands a Controller decides to issue this tick. Offline this
// applies them immediately; on the server it queues them into the tick sequencer.
using CommandSink = std::function<void(const tak::net::Command&)>;

// One AI brain, driving a single player.
class Controller {
public:
    // enemyStarts: the start positions of the players this AI is NOT allied with, so
    // it can march on a base under fog before it has actually spotted enemy units.
    Controller(int player, const tak::sim::TypeRegistry& registry,
               const Profile& profile, uint32_t seed,
               Difficulty difficulty = Difficulty::Normal,
               std::vector<std::pair<float, float>> enemyStarts = {});

    // Evaluate the AI for sim tick `simTick`. Does nothing off its think cadence.
    // Reads `world` (never mutates it) and emits any orders through `sink`.
    void tick(const tak::sim::World& world, uint32_t simTick, const CommandSink& sink);

private:
    // --- deterministic RNG (retail-style LCG) --------------------------------
    int rand(int n) {
        rng_ = rng_ * 1103515245u + 12345u;
        return n > 0 ? int((rng_ >> 16) % uint32_t(n)) : 0;
    }

    // --- decision helpers (all read-only over the world) ---------------------
    int   countOf(const tak::sim::World&, const std::string& id) const;
    float manaRatio(const tak::sim::World&) const;
    const tak::sim::UnitType* weightedPick(const tak::sim::World&,
                                           const tak::sim::Unit& producer, int econFactor);
    void produce(const tak::sim::World&, const tak::sim::Unit& producer,
                 const tak::sim::UnitType* pick, const CommandSink&);
    bool placeSite(const tak::sim::World&, const tak::sim::UnitType*, float nx, float nz,
                   float& outX, float& outZ) const;
    // Fog of war for the AI: an enemy is targeted only when one of the AI's own units
    // is within its sight/radar. For direction (before anything is spotted) the AI
    // falls back to the known enemy start positions.
    bool nearestVisibleEnemy(const tak::sim::World&, float cx, float cz,
                             const tak::sim::UnitType* atype, float& tx, float& tz) const;
    bool nearestEnemyStart(float cx, float cz, float& tx, float& tz) const;
    void sendWaves(const tak::sim::World&, const CommandSink&);

    static bool waveFree(const tak::sim::Unit& u) {
        return u.orders.empty() ||
               (u.orders.front().targetId == 0 && !u.orders.front().attackMove);
    }
    void emit(const CommandSink& sink, tak::net::Cmd kind, int unitId,
              const std::string& type, float x, float z) const;

    int player_;
    const tak::sim::TypeRegistry& registry_;
    const Profile& profile_;
    uint32_t rng_;
    Difficulty diff_;
    DiffParams dp_;
    std::vector<std::pair<float, float>> enemyStarts_;
    bool scouted_ = false;    // one-shot early scout sent
    bool committed_ = false;  // has launched its first wave -> keep the pressure on
};

}  // namespace tak::ai
