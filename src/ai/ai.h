#pragma once

// The skirmish AI, extracted from the SDL viewer so it can also run headless on
// the multiplayer server. A Controller reads a const World and EMITS net::Commands
// through a sink instead of mutating the world directly -- so the same AI drives
// a player whether the commands are applied in-process (single-player) or fed into
// the server's command sequencer (server-hosted AI). See docs/multiplayer-design.md.

#include <functional>
#include <string>
#include <unordered_map>

#include "net/lockstep.h"
#include "sim/sim.h"

namespace tak::ai {

// The retail AI profile (ai/default.txt): per-unit build weight and hard limit.
// weight = probability share in the weighted-random build pick; limit = hard cap
// (-1 = unlimited). Missing weight => the AI never builds that unit.
struct Profile {
    std::unordered_map<std::string, int> weight, limit;
};

// Parse ai/default.txt from the data root (falling back to the IP data root).
// One file covers every faction. Never throws; a missing file yields an empty
// profile (the AI then builds nothing).
Profile loadProfile(const std::string& dataRoot, const std::string& ipRoot = "");
// Same, reading ai/default.txt from the runtime VFS (base + IP merged).
Profile loadProfile(const tak::hpi::Vfs& vfs);

// Sink for the commands a Controller decides to issue this tick. Offline this
// applies them immediately; on the server it queues them into the tick sequencer.
using CommandSink = std::function<void(const tak::net::Command&)>;

// One AI brain, driving a single player. Deterministic: given the same seed and
// the same sequence of (world, simTick) observations it emits the same commands,
// so it stays in lockstep across peers.
class Controller {
public:
    Controller(int player, const tak::sim::TypeRegistry& registry,
               const Profile& profile, uint32_t seed);

    // Evaluate the AI for sim tick `simTick`. Does nothing off its ~1 Hz cadence.
    // Reads `world` (never mutates it) and emits any orders through `sink`.
    void tick(const tak::sim::World& world, uint32_t simTick, const CommandSink& sink);

    int player() const { return player_; }

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
    bool nearestReachableEnemy(const tak::sim::World&, float cx, float cz,
                               const tak::sim::UnitType* atype, float& tx, float& tz) const;
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
};

}  // namespace tak::ai
