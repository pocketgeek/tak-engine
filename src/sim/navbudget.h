#pragma once
// navbudget.h -- navigation work and byte accounting, the per-tick budgets and the deterministic
// service policy of Legion's scheduler (Legion 10,000 stage 4, docs/legion-10000-progress.md 3.2).
//
// Everything here counts in deterministic units -- cells touched, relaxations, registrations,
// entries -- never wall time, worker order or machine memory, so a policy that reads these numbers
// decides the same on every peer. Wall time and heap are measured by the tools, never here.
//
// The service policy (navDrr, navPromote, NavAdmit) is header-only so that navbudget_test drives
// exactly the code the navigator's scheduler runs.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tak::sim {

// Work classes. Each counts its own deterministic unit (see NavBudget for what one unit is).
enum class NavWork : uint8_t {
    FieldRelax,     // field relaxations (neighbour checks; Stats::fieldWork)
    FieldInit,      // field potential cells initialised when a build starts
    PlaneBuild,     // first (or re-) builds of a footprint-legality plane: advanceBuild's cell units
    PlaneRelabel,   // whole-plane relabels and synchronous rebuilds (cells x 2, as charged to planeDebt)
    Registration,   // registerMove: a base cost plus join candidates plus goal-resolution cells
    Awareness,      // awareScan: members gathered, candidate pairs, conflict samples, descent cells
    SlotSearch,     // destination assignment: formation / packed slot / re-choice cells
    RingBuild,      // reach-ring construction cells (buildRing)
    Connectivity,   // incremental plane refresh after a static change (refreshPlane)
    TileBuild,      // stage 6 (sparse tiles); 0 until then
    RouteSearch,    // stage 6 (hierarchical search); 0 until then
    Count
};
inline constexpr const char* kNavWorkNames[size_t(NavWork::Count)] = {
    "field_relax", "field_init", "plane_build", "plane_relabel", "registration", "awareness",
    "slot_search", "ring_build", "connectivity", "tile_build", "route_search"};

// Service classes of the field scheduler, highest priority first.
enum class NavClass : uint8_t {
    Waiting,        // a member stands still for want of this field (or a paused build it waits on)
    FirstRoute,     // a group's first field; its members steer by direct lines / the partial build
    AwareRepair,    // a refresh asked for by group awareness (a crossing planned round)
    StaleRefresh,   // a refresh after a static change; the members steer by the finished stale field
    Count
};
inline constexpr const char* kNavClassNames[size_t(NavClass::Count)] = {
    "waiting", "first_route", "aware_repair", "stale_refresh"};

// Deterministic bytes: element counts times fixed element sizes (the constants below, not sizeof, so
// every platform and standard library reports the same number). Capacity slack, allocator headers and
// fragmentation are NOT here: the tools measure the process heap separately (SCALE_WALL heap_kb).
struct NavBytes {
    uint64_t planes = 0, fields = 0, tiles = 0, portals = 0, queues = 0, scratch = 0, metadata = 0, inflight = 0;
    uint64_t total() const { return planes + fields + tiles + portals + queues + scratch + metadata + inflight; }
};
// Fixed element sizes for NavBytes (bytes). A tree node is charged kNode on top of its payload.
struct NavSize {
    static constexpr uint64_t kNode = 32;          // one std::map / std::set node's links and colour
    static constexpr uint64_t kCell32 = 4, kCell16 = 2, kCell8 = 1, kMemo = 8, kBox = 16, kKeyed = 8;
    static constexpr uint64_t kGroup = 768;        // a Group with its scalar fields (vectors charged apart)
    static constexpr uint64_t kField = 1200;       // a Field: scalars, its 128 bucket headers
    static constexpr uint64_t kMember = 640;       // a Member (route charged apart)
    static constexpr uint64_t kPoint = 320;        // a Point (cells, ids, queue charged apart)
    static constexpr uint64_t kCorridor = 104;     // a Field::Corridor
    static constexpr uint64_t kMover = 160;        // an awareness snapshot mover
};

// Observation-only accounting (LegionNavigator::accounting): never hashed, never read by a decision.
// `total` is cumulative per class; `tickMax` the most any one navigator tick window (one tick() to the
// next, so command-time work counts toward the tick it lands before) spent per class; `tickAll` the
// most of every class together in one window; `bytes` the current NavBytes and `bytesPeak` its largest
// total so far (sampled when accounting() is read).
struct NavAccounting {
    std::array<uint64_t, size_t(NavWork::Count)> total{}, tickMax{};
    uint64_t tickAll = 0;
    NavBytes bytes;
    uint64_t bytesPeak = 0;
    // The scheduler's service record (stage 4): the longest a field job went without a slice of work
    // (ticks, by its base class), the longest a start request waited (ticks from its request to its
    // field starting, by class), the jobs promoted by age, capacity refusals the fit scan stepped over,
    // reservations (a refused request old enough that nothing may overtake it), and registrations that
    // waited for the budget or for their plane (and the longest such wait, ticks).
    std::array<uint64_t, size_t(NavClass::Count)> serviceGapMax{}, requestAgeMax{};
    uint64_t promotions = 0, fitSkips = 0, reservations = 0;
    uint64_t regDeferred = 0, regWaitMax = 0, planeWaits = 0;
    // Synchronous stages that ran over their budget unit (reported, never hidden): whole-plane
    // relabels/rebuilds done in one call, the largest such call (units), and the largest single
    // field initialisation (cells).
    uint64_t syncPlaneCalls = 0, syncPlaneMax = 0, initMax = 0;
};

// Per-tick budgets (constants: the same on every peer). Units and the reference cost per unit
// (Intel Core Ultra 9 275HX, build-o2) are recorded in docs/legion-10000-progress.md, section 7.4.
struct NavBudget {
    // Field service: relaxations + initialisation + ring cells per tick. A quarter of it is the most
    // the refresh classes (AwareRepair + StaleRefresh) may take, so first routes keep most of it.
    static constexpr uint64_t kFieldQuota = 384'000;
    static constexpr uint64_t kRefreshQuota = kFieldQuota / 4;
    // One field cell initialised costs 1/kInitDiv of a relaxation unit (a 4-byte fill against a
    // neighbour test with soft and corridor reads).
    static constexpr uint64_t kInitDiv = 16;
    // Deficit round robin: the slice a job may spend per visit (a multiple of the paused-build
    // slice, so a paused build stops at the same points however its work is cut).
    static constexpr int64_t kSlice = 32768;
    // Age promotion: a job (or start request) that has gone this many ticks without service moves up
    // one class, so no class starves another for longer than (classes - 1) x kAgeStep.
    static constexpr uint32_t kAgeStep = 150;
    // Bounded fit scan: start requests refused for capacity that the admission steps over per tick,
    // and the age at which a refused request reserves (nothing behind it may start until it does).
    static constexpr int kFitScan = 8;
    static constexpr uint32_t kReserveAge = 150;
    // Field memory: deterministic bytes (potentials + in-flight frontiers) every live field may use
    // together. A fixed allowance, not a number of whole maps, with a floor of two whole-map fields
    // so that no single request can be larger than the allowance on any map.
    static constexpr uint64_t kFieldBytes = uint64_t(128) << 20;
    // Registration: units per tick (kRegBase per registration plus its join candidates and its goal
    // resolution cells). Registrations requested between ticks (orders) wait in a FIFO drained at
    // the start of the next navigator tick; one requested during a tick runs at once while the budget
    // lasts and the FIFO holds nothing it could overtake.
    static constexpr uint64_t kRegBase = 64;
    static constexpr uint64_t kRegBudget = 4096 * kRegBase;
    // Connectivity: first plane builds a registration waits for (advanceBuild units per tick).
    static constexpr uint64_t kPlaneBuildBudget = 1'048'576;
    // Awareness: candidate pairs + conflict samples + descent cells per tick; a scan that needs more
    // resumes next tick on the same snapshot.
    static constexpr uint64_t kAwareBudget = 262'144;
};

// The class a job serves in after `age` ticks without service (age promotion).
constexpr NavClass navPromote(NavClass base, uint32_t age) {
    const int c = int(base) - int(age / NavBudget::kAgeStep);
    return NavClass(c < 0 ? 0 : c);
}

// One visit's outcome: work spent, and whether the job stays in this tick's ring (it still has work
// and could use more). A visit that spends nothing must return keep = false.
struct NavVisit { uint64_t spent = 0; bool keep = false; };

// Deficit round robin over `ring` (jobs in ascending key order) from the first key above `cursor`,
// until `budget` (and `cap`, a second limit the same work also spends, e.g. the refresh allowance;
// nullptr: none) is spent or no job stays. Each visit adds kSlice to the job's deficit (capped at
// 2 kSlice) and lets it spend up to that deficit; `cursor` becomes the key of the last job served.
// Job: { uint64_t key; int64_t* deficit; ... }. serve(Job&, allowance) -> NavVisit.
template <class Job, class Serve>
void navDrr(std::vector<Job>& ring, uint64_t& cursor, uint64_t& budget, uint64_t* cap, Serve&& serve) {
    if (ring.empty()) return;
    size_t start = 0;
    while (start < ring.size() && ring[start].key <= cursor) ++start;
    if (start == ring.size()) start = 0;
    std::rotate(ring.begin(), ring.begin() + std::ptrdiff_t(start), ring.end());
    while (budget > 0 && (!cap || *cap > 0) && !ring.empty()) {
        size_t kept = 0;
        for (size_t i = 0; i < ring.size(); ++i) {
            Job& j = ring[i];
            if (budget == 0 || (cap && *cap == 0)) { ring[kept++] = j; continue; }
            int64_t& d = *j.deficit;
            d = std::min<int64_t>(d + NavBudget::kSlice, 2 * NavBudget::kSlice);
            if (d <= 0) { ring[kept++] = j; continue; }
            uint64_t allow = std::min<uint64_t>(uint64_t(d), budget);
            if (cap) allow = std::min(allow, *cap);
            const NavVisit v = serve(j, allow);
            budget -= std::min(budget, v.spent);
            if (cap) *cap -= std::min(*cap, v.spent);
            d -= int64_t(v.spent);
            if (v.spent) cursor = j.key;
            if (v.keep && v.spent) ring[kept++] = j;
            else d = 0;   // left the ring: an idle job carries no credit (DRR)
        }
        ring.resize(kept);
    }
}

// The bounded fit scan of one tick's admission: start requests are tried oldest first; one refused
// for capacity is stepped over, at most kFitScan times a tick, unless it has been refused for
// kReserveAge ticks -- then it reserves: nothing after it may start this tick, so the capacity that
// frees next goes to it. `blockedSince` is the request's own (hashed) stamp, kNever when clear.
struct NavAdmit {
    static constexpr uint32_t kNever = ~0u;
    int skips = 0;
    bool stopped = false;      // the scan ended this tick (reservation or the bound)
    bool reserved = false;     // ... because a request reserved
    // A refusal; true when the scan must stop.
    bool refuse(uint32_t& blockedSince, uint32_t now) {
        if (blockedSince == kNever) blockedSince = now;
        if (now - blockedSince >= NavBudget::kReserveAge) { stopped = reserved = true; return true; }
        if (++skips >= NavBudget::kFitScan) { stopped = true; return true; }
        return false;
    }
    static void admitted(uint32_t& blockedSince) { blockedSince = kNever; }
};

}  // namespace tak::sim
