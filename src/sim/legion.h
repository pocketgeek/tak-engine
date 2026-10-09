#pragma once
// Legion: group-planned, clearance-exact ground navigation (PathfindingMode 4).
// Design and guarantees: docs/legion-pathfinding.md.
#include "sim/fixed.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

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

// Is this mover "shared" under Legion: does a Move click give it the clicked
// point itself? Legion plans surface movers with footprints 1..8 (ground units
// and boats) and packs one shared destination into arrival slots; flyers and
// oversize bodies keep their offset from the selection centroid (clamped to
// +-60 px per axis). The client's right-click (gameview_hud.cpp) and
// World::order()'s convoy test (sim/convoy.h) both call this, so the two
// cannot drift.
constexpr bool legionSharedClick(bool canFly,int footX,int footZ) {
    return !canFly&&footX>=1&&footZ>=1&&footX<=8&&footZ<=8;
}

class LegionNavigator {
public:
    // Deterministic work/outcome counters. Observation only: never hashed and
    // never read by a movement decision. Legion runs on the sim thread (move()
    // in the serial unit loop), and each work counter is summed in a local and
    // added once per call, so serial and --workers runs count identically.
    struct Stats {
        uint64_t planeBuilds=0,planeRefreshes=0,planeRelabels=0,fieldWork=0,fieldsBuilt=0,fieldEvictions=0,fieldsShared=0;
        uint64_t groups=0,registrations=0,moves=0,holds=0,slides=0,passScans=0,passScansSkipped=0;
        uint64_t arrivals=0,contactArrivals=0,trapped=0,escapes=0;
        uint64_t detours=0,detourCells=0;
        uint64_t lifts=0;   // lift requests to idle landed flyers (see World::requestLegionLift)
        // Per LegionMission: legs (unit, order) Legion took on, arrivals it
        // raised (0x500) and failed approaches it handed back (0x200).
        uint64_t missionLegs[16]={},missionArrivals[16]={},missionFailures[16]={};
        // ---- hot-path work --------------------------------------------------
        // Straight-line probes (sweep/trace calls) and the origins they
        // stepped; long-held rest re-checks (ring signatures); slot search
        // cells (slotsFor windows, formationCell rings, re-choice BFS).
        uint64_t lineSweeps=0,heldRechecks=0,slotSearchCells=0;
        uint64_t traceCells=0;
        uint64_t passScanCells=0;    // cells passAhead's oncoming scan read
        // Iterations of every loop over all groups (tick's scheduler,
        // staticChanged, eviction, awareness, plane eviction) and of
        // sharedField's group x field scan.
        uint64_t groupLoopIters=0,shareScanIters=0;
        uint64_t awarePairs=0;       // (group, mover) pairs awareScan judged
        // Refreshes (`next`) dropped before done; refreshes installed.
        uint64_t refreshDiscards=0,refreshCompleted=0;
        // ---- scheduler (T7 I1) ---------------------------------------------
        uint64_t schedGroupVisits=0;      // groups tick()'s three scheduler loops visited
        uint64_t sharedfieldFullScans=0;  // sharedField calls that scanned the group list
        uint64_t softownerLookups=0;      // linear lookups in the settled-arrival owner list
        // fieldWork by who it was spent for: first builds of area groups
        // (a goal shared by 2+ members) and of the rest, refreshes of groups
        // with a member that moved last tick and of groups with none. The
        // four always sum to fieldWork.
        uint64_t fieldWorkFirstSlot=0,fieldWorkFirstSolo=0,fieldWorkRefreshMoving=0,fieldWorkRefreshIdle=0;
        uint64_t fieldsStartedByKind[16]={};   // new (unshared) field builds per LegionMission
        uint64_t refreshDeferred=0;   // stale groups left waiting: refresh allowance spent, nothing to share
        uint64_t blockedRerequests=0; // demand re-requests by blocked members (0 until demand refresh exists)
        uint64_t liftMembersWalked=0,liftMembersSkipped=0;   // liftFlyers: steps walked / box-rejected
        uint64_t waitingMemberTicks=0;   // member updates spent waiting for a field
        uint64_t demandResumes=0,fieldsPaused=0;   // paused builds (0 until paused builds exist)
        uint64_t stillUnitsProcessed=0;  // bodies scanStill sampled
        // ---- W4 step 0 instruments (observation only, never hashed) ----------
        // Gauges (running maxima, not per-tick work): the most bodies one
        // residue class (id % 30) of a scanStill pass holds (B2 stripes the scan
        // by this residue; target <= 1.1x the mean), and the longest run of
        // consecutive ticks whose field quota was spent to zero.
        uint64_t stillPerResidueMax=0,quotaPegRunMax=0;
        // Map entries serviceYields walks (anchors, approachDone, parts,
        // yielding) plus the ids prune validates (B3 drives this to the
        // backstop cursor alone); soft-hash recomputations checked by
        // TAK_LEGION_VERIFY (0 until B2); refreshes left unstarted for want of
        // demand (0 until B1).
        uint64_t anchorWalkIters=0,softHashVerifyTicks=0,refreshSuppressed=0;
        // ---- army throughput (T1 I4) ---------------------------------------
        uint64_t crowdWindowRingCells=0;   // the settle rule's queue ring (settle-chain cells) and the holder rule's ring
        uint64_t crowdSettleVisits=0;      // units walked to rebuild the factory exit lanes
        uint64_t rechoiceBfsCells=0;       // the settle rule's slot re-choice (rechoose)
        uint64_t formationRingCells=0;     // formationCell
        uint64_t moveCallsByState[8]={};   // move() per member state (0 none .. 5 trapped)
        uint64_t pivotPartIds=0;           // pivotAim's per-part centroid pass (ids of points of 2+ parts)
        uint64_t joinIterations=0;         // registerMove's group-join scan
        // ---- completions (T1) ------------------------------------------------
        // Completed legs farther than the destination area (formation limit,
        // else the packed disc of the goal's members) plus 20 cells from the
        // click with no settled arrival of the same order within 3 bodies
        // (mid-route), or merely outside that area; and every completion's
        // distance from the click in cells, log2 buckets: [0]: <1, [i]:
        // [2^(i-1), 2^i), [9]: >= 256.
        uint64_t midrouteCompletions=0,outsideAreaCompletions=0;
        uint64_t completionDist[10]={};
        uint64_t completionDistSum=0,completionDistMax=0;
        size_t bytes=0;
        // Live container sizes (observation only).
        size_t liveGroups=0,liveMembers=0,livePoints=0,liveFields=0;
    };
    // Every counter as (snake_case name, value), in declaration order;
    // arrays expand as name_<index>. crowdbench's legion_* keys are these
    // names with the prefix.
    template<class F> static void forEachStat(const Stats& s,F&& f) {
        auto array=[&](const char* name,const uint64_t* v,size_t n) {
            for(size_t i=0;i<n;++i)f((std::string(name)+"_"+std::to_string(i)).c_str(),v[i]);
        };
        f("plane_builds",s.planeBuilds);f("plane_refreshes",s.planeRefreshes);f("plane_relabels",s.planeRelabels);
        f("field_work",s.fieldWork);f("fields_built",s.fieldsBuilt);f("field_evictions",s.fieldEvictions);f("fields_shared",s.fieldsShared);
        f("groups",s.groups);f("registrations",s.registrations);f("moves",s.moves);f("holds",s.holds);f("slides",s.slides);
        f("pass_scans",s.passScans);f("pass_scans_skipped",s.passScansSkipped);
        f("arrivals",s.arrivals);f("contact_arrivals",s.contactArrivals);f("trapped",s.trapped);f("escapes",s.escapes);
        f("detours",s.detours);f("detour_cells",s.detourCells);f("lifts",s.lifts);
        array("mission_legs",s.missionLegs,16);array("mission_arrivals",s.missionArrivals,16);array("mission_failures",s.missionFailures,16);
        f("line_sweeps",s.lineSweeps);f("held_rechecks",s.heldRechecks);f("slot_search_cells",s.slotSearchCells);
        f("trace_cells",s.traceCells);f("pass_scan_cells",s.passScanCells);
        f("group_loop_iters",s.groupLoopIters);f("share_scan_iters",s.shareScanIters);f("aware_pairs",s.awarePairs);
        f("refresh_discards",s.refreshDiscards);f("refresh_completed",s.refreshCompleted);
        f("sched_group_visits",s.schedGroupVisits);f("sharedfield_full_scans",s.sharedfieldFullScans);f("softowner_lookups",s.softownerLookups);
        f("field_work_first_slot",s.fieldWorkFirstSlot);f("field_work_first_solo",s.fieldWorkFirstSolo);
        f("field_work_refresh_moving",s.fieldWorkRefreshMoving);f("field_work_refresh_idle",s.fieldWorkRefreshIdle);
        array("fields_started_by_kind",s.fieldsStartedByKind,16);
        f("refresh_deferred",s.refreshDeferred);f("blocked_rerequests",s.blockedRerequests);
        f("lift_members_walked",s.liftMembersWalked);f("lift_members_skipped",s.liftMembersSkipped);
        f("waiting_member_ticks",s.waitingMemberTicks);f("demand_resumes",s.demandResumes);f("fields_paused",s.fieldsPaused);
        f("still_units_processed",s.stillUnitsProcessed);
        f("still_per_residue_max",s.stillPerResidueMax);f("quota_peg_run_max",s.quotaPegRunMax);
        f("anchor_walk_iters",s.anchorWalkIters);f("soft_hash_verify_ticks",s.softHashVerifyTicks);f("refresh_suppressed",s.refreshSuppressed);
        f("crowd_window_ring_cells",s.crowdWindowRingCells);f("crowd_settle_visits",s.crowdSettleVisits);
        f("rechoice_bfs_cells",s.rechoiceBfsCells);f("formation_ring_cells",s.formationRingCells);f("pivot_part_ids",s.pivotPartIds);
        array("move_calls_by_state",s.moveCallsByState,8);f("join_iterations",s.joinIterations);
        f("midroute_completions",s.midrouteCompletions);f("outside_area_completions",s.outsideAreaCompletions);
        array("completion_dist",s.completionDist,10);f("completion_dist_sum",s.completionDistSum);f("completion_dist_max",s.completionDistMax);
        f("bytes",uint64_t(s.bytes));
        f("live_groups",uint64_t(s.liveGroups));f("live_members",uint64_t(s.liveMembers));
        f("live_points",uint64_t(s.livePoints));f("live_fields",uint64_t(s.liveFields));
    }
    // I2 slot-shape probe: the shape of a formation when its slots were
    // assigned (assignFormation), in the approach frame (centroid -> click).
    // Observation only, never hashed. Distances in pixels.
    struct SlotShape {
        bool valid=false;
        uint32_t tick=0;
        int members=0,slots=0;            // members placed; members that got a slot
        int64_t rmsAlong=0,rmsAcross=0;   // the members' spread about their centroid
        int64_t alongMin=0,alongMax=0,acrossMin=0,acrossMax=0;   // slot centres about the click
        int64_t limit=0;                  // the area's radius
        int frontage=0;                   // distinct across-cells among the 25% of slots nearest the click
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
    // Observation hook (read-only, never hashed): cells left in the unit's committed local detour route
    // (0: none, or not a Legion member). MV-06's route-follower crawl samples read it.
    int routeLength(int id) const;
    // Test hook: the unit's group field potential at an origin (-1 if none).
    int fieldPotential(int id,int originX,int originZ) const;
    // Test hook: the slot shape of the unit's formation (valid=false if it
    // has none), and of the formation assigned last.
    SlotShape slotShape(int id) const;
    SlotShape lastSlotShape() const;
    // Test hook (process-wide; the settlelatency probe): a long-held body's
    // full re-evaluation stride (default 2). Changes behaviour and hashes.
    static void setRestStrideForTest(int stride);
    // Debug builds (NDEBUG undefined): check derived indexes and the Stats
    // after every tick() (aborts on a mismatch). A no-op in release. A debug
    // build starts with it on when TAK_LEGION_VERIFY is set (the only
    // environment read, debug-only and never hashed). Process-wide.
    static void setVerify(bool on);
    // Is the verify hook on? (Always false in release.) World's convoy table
    // (sim/convoy.h) runs its own checks under the same hook.
    static bool verifying();
    // Print the scheduler counters (an "LPROBE" line on stderr) every
    // `ticks` ticks; 0 off. Process-wide; tools and debug clients set it.
    static void setProbe(uint32_t ticks);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
