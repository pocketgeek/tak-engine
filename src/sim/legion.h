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

// The ground missions whose goal Legion routes (docs/legion-pathfinding.md
// "Mission goals"). None: the leg is Retail's (native search and mover).
// Build..Park are the work/logistics approaches: build sites, repair and
// reclaim targets, transport boarding/drop circles, production exits and
// PARK rings.
enum class LegionMission : uint8_t {None=0,Move,Fight,Patrol,Attack,Guard,
    Build,Repair,Reclaim,Load,Unload,Exit,Park};

class LegionNavigator {
public:
    // Deterministic work/outcome counters. Observation only: never hashed and
    // never read by a movement decision.
    struct Stats {
        uint64_t planeBuilds=0,planeRefreshes=0,planeRelabels=0,fieldWork=0,fieldsBuilt=0,fieldEvictions=0;
        uint64_t groups=0,registrations=0,moves=0,holds=0,slides=0,passScans=0,passScansSkipped=0;
        uint64_t arrivals=0,contactArrivals=0,trapped=0,escapes=0;
        uint64_t detours=0,detourCells=0;
        uint64_t lifts=0;   // lift requests to idle landed flyers (see World::requestLegionLift)
        // Per LegionMission: legs (unit, order) Legion took on, arrivals it
        // raised (0x500) and failed approaches it handed back (0x200).
        uint64_t missionLegs[16]={},missionArrivals[16]={},missionFailures[16]={};
        size_t bytes=0;
        // Live container sizes (observation only).
        size_t liveGroups=0,liveMembers=0,livePoints=0,liveFields=0;
    };
    explicit LegionNavigator(World&);
    ~LegionNavigator();
    // Ground units whose current leg is a mission Legion routes (see
    // mission()); everything else stays Retail.
    bool supports(const Unit&) const;
    // Which mission goal Legion would route for this unit (None: Retail).
    LegionMission mission(const Unit&) const;
    // A supported leg became current (order, queued leg, controller reset).
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
    // Test hook: does the unit's (incrementally maintained) plane equal a
    // fresh whole-map build: legality, component partition, sizes, boxes?
    bool planeMatchesRebuild(const Unit&);
    // Test hook: the unit's movement state (0 none, 1 moving, 2 holding,
    // 3 waiting for its field) and its group's identity.
    int unitState(int id) const;
    int unitGroup(int id) const;
    // Test hook: the unit's group field potential at an origin (-1 if none).
    int fieldPotential(int id,int originX,int originZ) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
