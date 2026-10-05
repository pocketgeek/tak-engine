#pragma once
// Legion: group-planned, clearance-exact ground navigation (PathfindingMode 4).
// Design and guarantees: docs/legion-pathfinding.md.
#include "sim/fixed.h"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace tak::sim {
class World;
struct Unit;

class LegionNavigator {
public:
    // Deterministic work/outcome counters. Observation only: never hashed and
    // never read by a movement decision.
    struct Stats {
        uint64_t planeBuilds=0,fieldWork=0,fieldsBuilt=0,fieldEvictions=0;
        uint64_t groups=0,registrations=0,moves=0,holds=0,slides=0;
        uint64_t arrivals=0,contactArrivals=0,trapped=0,escapes=0;
        uint64_t detours=0,detourCells=0;
        size_t bytes=0;
    };
    explicit LegionNavigator(World&);
    ~LegionNavigator();
    // Plain ground Move legs of ground units; everything else stays Retail.
    bool supports(const Unit&) const;
    // A Move leg became current (order, queued leg, controller reset).
    void registerMove(Unit&);
    void cancel(int id);
    // Start-of-tick service: static plane freshness and field work quota.
    void tick();
    // One movement update for a supported unit, replacing Retail steering.
    void move(Unit&,Fixed maximum);
    uint64_t checksum() const;
    Stats stats() const;
    // Test hook: is a footprint origin legal on the static plane Legion plans
    // on (terrain, features, structures; mobile bodies excluded)?
    bool staticLegal(const Unit&,int originX,int originZ);
    // Test hook: the unit's movement state (0 none, 1 moving, 2 holding,
    // 3 waiting for its field) and its group's identity.
    int unitState(int id) const;
    int unitGroup(int id) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
