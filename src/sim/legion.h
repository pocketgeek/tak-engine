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
        // ---- W6 step 0 instruments (observation only, never hashed) ----------
        // relifts: a lift episode that begins within 600 ticks of the same flyer
        // landing from its previous one (bobbing); counted from W6 step 0.
        // The rest are declared here for the W6 steps that add the behaviour they
        // count and stay 0 until then: capHits (a lift episode ended by the
        // 1800-tick cap, step 6), stationReleasesA / stationReleasesB (formation
        // flyers released from their station by the stall / static-centroid
        // rule, step 5), goArounds (touchdown backstop go-arounds, step 2),
        // stationOverflow (a 5th distinct click in one squad that got no station,
        // step 3).
        uint64_t relifts=0,capHits=0,stationReleasesA=0,stationReleasesB=0,goArounds=0,stationOverflow=0;
        // W6 FL-01: acquireTarget polls by lifted flyers (a declared work class,
        // PLAN 3.0: at most lifted flyers / 8 per tick).
        uint64_t liftTargetPolls=0;
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
        // Stale refreshes resumed once their group is active again (B1, see
        // refreshSuppressed); first builds paused once they cover their
        // members and paused builds resumed for a member's demand (C1).
        uint64_t demandResumes=0,fieldsPaused=0,pausedResumes=0;
        uint64_t stillUnitsProcessed=0;  // bodies scanStill sampled
        // ---- W4 step 0 instruments (observation only, never hashed) ----------
        // Gauges (running maxima, not per-tick work): the most bodies one
        // residue class (id % 30) of a scanStill pass holds (B2 stripes the scan
        // by this residue; bound: per-tick max <= 10% of the old 30th-tick
        // spike, lead ruling W4 (k)), and the longest run of
        // consecutive ticks whose field quota was spent to zero.
        uint64_t stillPerResidueMax=0,quotaPegRunMax=0;
        // Ids prune's backstop cursor validates (members plus the anchors,
        // approachDone and parts records; at most 256 a tick since B3, which
        // erases those records by event instead of a whole walk); soft-hash recomputations checked by
        // TAK_LEGION_VERIFY (B2: one a tick while it is on); stale refreshes
        // left unstarted for want of demand (B1: one per inactive group visit).
        uint64_t anchorWalkIters=0,softHashVerifyTicks=0,refreshSuppressed=0;
        // ---- army throughput (T1 I4) ---------------------------------------
        uint64_t crowdWindowRingCells=0;   // the settle rule's queue ring (settle-chain cells) and the holder rule's ring
        uint64_t crowdSettleVisits=0;      // units walked to rebuild the factory exit lanes
        uint64_t rechoiceBfsCells=0;       // the settle rule's slot re-choice (rechoose)
        uint64_t formationRingCells=0;     // formationCell
        uint64_t moveCallsByState[8]={};   // move() per member state (0 none .. 5 trapped, 6 engaged)
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
        // ---- W5 step 0 instruments (observation only, never hashed) ----------
        // engagedNow is cumulative like every counter (the verify hook demands it): the
        // member-ticks tickCombat braked an in-reach ground attacker or a guard within 70 px
        // (the bodies W5's Engaged flag will mark), so its per-tick delta IS the number
        // engaged now and its total the engaged member-ticks. The other four count
        // mechanisms W5 builds and read 0 until their step exists: reach-ring slots handed
        // out (step 1), stale-block re-plans MV-05 triggered (step 3), chase re-seeds that
        // kept the group (step 2), brisk-tier steps taken unturned at cap/2 (step 4).
        uint64_t engagedNow=0,reachSlotsBuilt=0,softReplans=0,reseedsInPlace=0,briskSteps=0;
        // ---- W7 step 0 instruments (observation only, never hashed) ----------
        // parts: bodies a parting shifted aside (one per body per part).
        // awareEncounters: (group, mover) corridors newly planned round by awareScan.
        // awareReplans: awareScan decisions that asked a group for a new field.
        // awareBuilds: fresh (unshared) field builds that carry a mover corridor; per
        // encounter that is awareBuilds / awareEncounters.
        // awareLatencyMax/Sum/N: ticks from an awareScan replan to the install of a
        // field started after it (the encounter's plan latency); max is a running
        // maximum, not work.
        // giveWayTicks / giveWayTimeouts / giveWayStarts: member-ticks in a give-way hold, holds
        // that ended on their timeout and holds begun. Declared for W7 step 6 and 0 until then;
        // under user decision 5 W7 builds no hold, so they must stay 0 after W7 too.
        // awareWork: cells of the descent chains awareScan walked plus its corridor tests (the
        // pair count is awarePairs); the step-6 detection samples add to it.
        uint64_t parts=0,awareEncounters=0,awareReplans=0,awareBuilds=0,awareWork=0;
        uint64_t awareLatencyMax=0,awareLatencySum=0,awareLatencyN=0;
        uint64_t giveWayTicks=0,giveWayTimeouts=0,giveWayStarts=0;
        // ---- W8 step 0 instruments (observation only, never hashed) ----------
        // pivotCalls: pivotAim evaluations (one per origin change of a pinwheel-eligible member);
        // pivotWork: the cells and probes they touch (descent chain cells, the wall scan up to the first
        // touch, the narrow-strip tests, the arc's free-width probes and one per sweep started);
        // pivotSweeps: the line sweeps they started. laneWork / laneClipped are the S1 corner lanes' own
        // counters (vertex-search and arc cells; arcs clamped to the group's field window) and stay 0
        // until W8 step 3 builds the lanes: a crowdbench row with pivotWork and laneWork both 0 is
        // required to stay hash-identical through W8 step 3.
        uint64_t pivotCalls=0,pivotWork=0,pivotSweeps=0,laneWork=0,laneClipped=0;
        // W8 step 1 (the vertex probe, unused by steering): vertexCalls counts probes of
        // pinwheel-eligible members far from their destination (one per origin change), split
        // by outcome: a vertex found, the whole 128-cell chain visible, the vertex within 32
        // cells of the destination, or nothing past the body's own cell visible; vertexWork
        // is the chain cells, line probes and clearance-ring cells they touch.
        uint64_t vertexCalls=0,vertexFound=0,vertexVisible=0,vertexNear=0,vertexBlocked=0,vertexWork=0;
        // W8 step 2 (the passage gate): members that committed to a gate point, and releases
        // (crossed the gate line, came within 2 cells of the point, or the point turned illegal).
        // The gate's search and aim work is counted in laneWork.
        uint64_t gateCommits=0,gateReleases=0;
        // W8 step 5 (S4 pass release, dropped; the counter stays): hold updates in which the pass filter
        // kept a body from stepping back across the lane it left and nothing else moved it.
        uint64_t passFilterHolds=0;
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
        f("relifts",s.relifts);f("cap_hits",s.capHits);f("station_releases_a",s.stationReleasesA);f("station_releases_b",s.stationReleasesB);
        f("go_arounds",s.goArounds);f("station_overflow",s.stationOverflow);f("lift_target_polls",s.liftTargetPolls);
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
        f("waiting_member_ticks",s.waitingMemberTicks);f("demand_resumes",s.demandResumes);f("fields_paused",s.fieldsPaused);f("paused_resumes",s.pausedResumes);
        f("still_units_processed",s.stillUnitsProcessed);
        f("still_per_residue_max",s.stillPerResidueMax);f("quota_peg_run_max",s.quotaPegRunMax);
        f("anchor_walk_iters",s.anchorWalkIters);f("soft_hash_verify_ticks",s.softHashVerifyTicks);f("refresh_suppressed",s.refreshSuppressed);
        f("crowd_window_ring_cells",s.crowdWindowRingCells);f("crowd_settle_visits",s.crowdSettleVisits);
        f("rechoice_bfs_cells",s.rechoiceBfsCells);f("formation_ring_cells",s.formationRingCells);f("pivot_part_ids",s.pivotPartIds);
        array("move_calls_by_state",s.moveCallsByState,8);f("join_iterations",s.joinIterations);
        f("midroute_completions",s.midrouteCompletions);f("outside_area_completions",s.outsideAreaCompletions);
        array("completion_dist",s.completionDist,10);f("completion_dist_sum",s.completionDistSum);f("completion_dist_max",s.completionDistMax);
        f("engaged_now",s.engagedNow);f("reach_slots_built",s.reachSlotsBuilt);f("soft_replans",s.softReplans);
        f("reseeds_in_place",s.reseedsInPlace);f("brisk_steps",s.briskSteps);
        f("parts",s.parts);f("aware_encounters",s.awareEncounters);f("aware_replans",s.awareReplans);f("aware_builds",s.awareBuilds);f("aware_work",s.awareWork);
        f("aware_latency_max",s.awareLatencyMax);f("aware_latency_sum",s.awareLatencySum);f("aware_latency_n",s.awareLatencyN);
        f("giveway_ticks",s.giveWayTicks);f("giveway_timeouts",s.giveWayTimeouts);f("giveway_starts",s.giveWayStarts);
        f("pivot_calls",s.pivotCalls);f("pivot_work",s.pivotWork);f("pivot_sweeps",s.pivotSweeps);
        f("lane_work",s.laneWork);f("lane_clipped",s.laneClipped);
        f("vertex_calls",s.vertexCalls);f("vertex_found",s.vertexFound);f("vertex_visible",s.vertexVisible);
        f("vertex_near",s.vertexNear);f("vertex_blocked",s.vertexBlocked);f("vertex_work",s.vertexWork);
        f("gate_commits",s.gateCommits);f("gate_releases",s.gateReleases);
        f("pass_filter_holds",s.passFilterHolds);
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
    // The unit's orders changed (a leg gained or retired, a mission step, its
    // death): drop the settled-arrival, parting and yield records it no
    // longer qualifies for. World::noteOrders calls it.
    void ordersChanged(int id);
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
    // Test hook (W8 step 1, read-only): nearObstacle (static memo plus the per-query
    // soft part) against an exhaustive search on `queries` random origins of the
    // unit's plane, for no command, every command with settled arrivals and one
    // without; then every member's vertex probe against an exhaustive scan of
    // its chain. Returns the mismatches; `report` gets the counts.
    int laneGeometryCheck(const Unit&,int queries,uint64_t seed,std::string* report);
    // Test hook: the unit's movement state (0 none, 1 moving, 2 holding,
    // 3 waiting for its field, 4 arrived, 5 trapped, 6 engaged) and its group's identity.
    int unitState(int id) const;
    int unitGroup(int id) const;
    // Test hook: the unit's settled-arrival records, as bits: 1 anchor,
    // 2 approach done, 4 parted, 8 yielding.
    int recordsForTest(int id) const;
    // Observation hook (read-only, never hashed): cells left in the unit's committed local detour route
    // (0: none, or not a Legion member). MV-06's route-follower crawl samples read it.
    int routeLength(int id) const;
    // tickCombat braked this unit in reach (an attacker) or within 70 px of its guard
    // target: counted into Stats::engagedNow when Legion routes it, and its member is
    // Engaged (settled and still to local steering) until its next move() (AR-06).
    void noteEngaged(const Unit&);
    // W5 step 0 (the claims invariant, read-only, never hashed): audit every member's
    // claimed arrival slot against its point's claimed cells. `overlaps` counts cells two
    // members of a point both claim, `missing` claimed footprint cells the point does not
    // hold (a lost claim), `dangling` members that hold a slot on a point that is gone,
    // `orphans` point cells no member's slot covers (report only: a settled body that left
    // the navigator keeps its cells by design). overlaps + missing + dangling must be 0.
    struct ClaimsAudit {int members=0,overlaps=0,missing=0,dangling=0,orphans=0;};
    ClaimsAudit claimsAudit() const;
    // W6 (PLAN 3.6): is the unit a Legion member still making headway, its
    // headway clock (the last tick it reached a new best potential on its
    // group's field) under `limit` ticks?
    // Moving, Holding and Waiting members count; Arrived, Trapped and
    // non-members do not. The flyer rules read it with kLiftStall (lift area)
    // and kStationStall (formation stations). Reads hashed member state only.
    static constexpr uint32_t kLiftStall=120,kStationStall=450;
    bool advancing(int id,uint32_t limit) const;
    // W6 (PLAN 3.5 rejoin): the destination point (raw Fixed) of the unit's
    // settled-arrival record and how many settled bodies share it; false if
    // it has none.
    bool arrivalPoint(int id,int32_t& x,int32_t& z,int& count) const;
    // W6 behaviour counters World's flyer rules raise (Stats, observation only).
    enum class FlyerEvent : uint8_t {StationOverflow,ReleaseA,ReleaseB,CapHit,GoAround,TargetPoll};
    void noteFlyerEvent(FlyerEvent);
    // Test hook: the unit's group field potential at an origin (-1 if none).
    int fieldPotential(int id,int originX,int originZ) const;
    // Test hook: the slot shape of the unit's formation (valid=false if it
    // has none), and of the formation assigned last.
    SlotShape slotShape(int id) const;
    SlotShape lastSlotShape() const;
    // Test hook (process-wide; the settlelatency probe): a long-held body's
    // full re-evaluation stride (default 2). Changes behaviour and hashes.
    static void setRestStrideForTest(int stride);
    // Test hook (process-wide; the awareness fixtures' OFF arm): awareScan does
    // nothing while set. Changes behaviour and hashes when set; default off.
    static void setAwareOffForTest(bool off);
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
