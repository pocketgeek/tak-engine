#include "legion.h"
#include "sim.h"
#include "footprint.h"
#include "retailplacement.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <tuple>
#include <vector>

// Legion navigation. See docs/legion-pathfinding.md for the design.
//
// Everything below is integer arithmetic on the footprint-ORIGIN lattice: a
// unit of footprint fx*fz whose centre is at x occupies origin column
// footprintOrigin(x,fx). That is the coordinate the mover's legality test
// (World::mobilePlacement / commitGroundStep) takes, so planning on it makes
// "legal for the planner" and "legal for the mover" the same predicate.

namespace tak::sim {
namespace {
constexpr uint16_t kUnreached=0xffff;
constexpr uint16_t kOrthogonal=5,kDiagonal=7;   // 7/5 = 1.4 ~ sqrt(2)
// Relaxations per tick, all fields: about 1.5 ms (4.4 ns each, measured on
// the jank replay), a third of a tick at 8x game speed (4.17 ms). A field
// still steers the bodies its frontier has passed while it builds.
constexpr uint64_t kFieldQuota=384'000;
// Of which refreshes (rebuilds of a stale field whose group still steers by
// the finished one) take at most this much per tick: after a static change
// in a battle a whole-map refresh otherwise ran the full quota for a dozen
// ticks, a spike over the 8x budget each time.
constexpr uint64_t kRefreshQuota=kFieldQuota/4;
// A stale field is refreshed only for an active group (see active): a member
// moving, or one blocked -- no progress for this many ticks outside its
// destination area (Retail's blocked re-request) -- or an explicit demand.
constexpr uint32_t kBlockedRetry=120;
// A body held this many ticks (past every early reaction:
// yields, first detours) is fully re-evaluated only every kRestStride ticks,
// staggered by unit id, unless something around it changed (heldRest).
constexpr uint32_t kRestAfter=60;
constexpr uint32_t kRestStride=2;
// Process-wide hooks (see legion.h): the rest stride for the settlelatency
// probe, TAK_LEGION_VERIFY and the LPROBE line. Set by tools before a run.
// A debug build also starts with the verify hook on when TAK_LEGION_VERIFY
// is set (no tool wires it yet: legion_identity.sh --verify, crowdbench and
// the world tests only set the variable). Observation only, never hashed.
uint32_t gRestStride=kRestStride;
#ifndef NDEBUG
bool gVerify=std::getenv("TAK_LEGION_VERIFY")!=nullptr;
#else
bool gVerify=false;
#endif
#ifndef NDEBUG
// ... and the LPROBE line every TAK_LPROBE ticks.
uint32_t gProbe=std::getenv("TAK_LPROBE")?uint32_t(std::strtoul(std::getenv("TAK_LPROBE"),nullptr,10)):0;
#else
uint32_t gProbe=0;
#endif
constexpr size_t kMaxFields=48,kMaxPlanes=24;     // field budget: kMaxFields whole maps of cells
constexpr size_t kMaxFieldCount=1024;             // live fields (and in-progress rebuilds), any size
constexpr uint32_t kFieldTenure=300;          // ticks a field is safe from eviction
constexpr uint32_t kNoTick=~0u;                // an unset tick stamp
constexpr uint32_t kTrappedRetire=9000;        // ticks (5 min) a trapped order waits for terrain to open
// Ticks an approach member's order (an unreachable goal) is kept once the body
// reaches its approach point, the nearest reachable spot: none, the order drops
// there (user decision 2026-10-08, PLAN W2 AR-11). Timed from arrival, so the
// walk to the point is never cut short; the walk's own limit is the
// kTrappedRetire age test in move().
constexpr uint32_t kApproachRetire=0;
// Ticks without gaining on its point after which an approach member pressed
// against bodies of its own selection that already stopped nearer that point
// (arrived there, or settled behind them) has reached the nearest spot IT can
// reach: its order drops where it stands (AR-11, see approachSettled). A body
// that still gains, in a moving queue or on a long walk, restarts the window
// each time. 30 and 45 left the bodies still to settle pressed against the
// settled ones longer than Retail does (contact_settled_permille unreach-200
// 47 / 56 against Retail 45, mazeapproach 3 / 5 against 2: the Retail floor);
// 15 gives 26 and 2.
constexpr uint32_t kApproachSettle=12;
constexpr uint32_t kApproachLook=4;            // ticks between a held approach member's looks (staggered by id)
constexpr int kClusterCells=16;
constexpr int kFieldMargin=32;                 // bounded field window margin (cells), at least
constexpr int kHeadingCells=8;                 // pending-field heading probe (cells)
constexpr size_t kGroupSeeds=256;              // distinct goal origins per group field
constexpr uint32_t kCrowdWindow=45;            // no-progress window for "close enough" settling at a crowd
// The settle rule (see settleWindow, PLAN 3.1 T1-B): a body queued back to
// its destination's settled crowd and pressed against it settles where it
// stands once its field potential is within its bound `settleP`, which
// starts at the destination area's bound plus kSettleSlack bodies and grows
// by Retail's foot*32 px (kSettleGrow bodies) per window. At most
// kRechoices re-choices of a free slot per member come first (none while it
// stands on its own slot).
constexpr int kSettleSlack=2,kSettleGrow=2;
constexpr uint8_t kRechoices=3;
constexpr int kSettleRing=64;                  // cells the queue test may visit per window
constexpr int kDetourCells=12;                 // local detour search radius                // group goals linked within this
constexpr uint32_t kPassHold=300;              // ticks a passing body keeps its new lane
constexpr int kLaneSpan=8;                    // passage lane grid: strips narrower than this (origins)
constexpr int kPassCells=6;                    // oncoming-traffic look-ahead (cells)
constexpr int kPartCells=16;                   // lane parting through idle bodies: look-ahead (cells)
constexpr uint8_t kMaxParts=3;                 // parting steps per idle body
constexpr int64_t kOncomingCos2=14;            // 100*cos^2(112 deg): goal directions this far apart are oncoming
constexpr int kLineCells=160;                  // direct-line probe reach
constexpr int kFormationLineCells=640;         // ... for a member with a formation slot
// Pinwheel (see pivotAim): a formation member of a command at least
// kPivotMembers strong rounds a wall end on its own concentric arc, looking
// kPivotChain descent cells ahead. Its arc radius is capped at 1.5 bodies
// per sqrt(members) and kPivotMaxCells; below kPivotMinBodies bodies it
// hugs (the inner file, as before).
constexpr int kPivotMembers=16;
constexpr int kUplinkPart=64;                  // orders the server applies per client per tick (net kCmdCapPerTick)
constexpr size_t kSlotsPerTick=32;             // formation slots a point hands out per tick (A2)
constexpr uint64_t kHandOutCells=131072;       // ... and the ring cells their searches may walk in that tick
constexpr int kPivotChain=48;
constexpr int kPivotMaxCells=32;
constexpr int kPivotMinBodies=2;
constexpr int kPivotSweeps=48;                 // line probes per pivot aim (bounded work)
constexpr int kPivotAhead=4;                   // an arc aim is at least this many descent cells ahead
constexpr int kPivotNear=32;                   // no pinwheel within this many cells of the destination
constexpr int kPivotLead=6;                    // pursuit distance along the arc (cells)
constexpr int kGoalSearchCells=24;             // blocked click -> nearest legal
constexpr int kApproachRegion=256;             // origins a region needs before an unreachable goal is approached
// Moving mission goals (chase, guard) re-seed their field at most once per
// this many ticks, on a shared tick grid so every chaser of one target that
// re-seeds in the same window shares one group and field, and only once the
// target has left the seeded origin by kReseedCells (Chebyshev).
constexpr uint32_t kReseedTicks=16;
constexpr int kReseedCells=2;
// A reach kind's (attack, guard) held peer of the same target is settled to
// the bodies behind it once held this many ticks (see blockedBySettled).
constexpr uint32_t kReachPeerHeld=30;
// The reach ring (see buildRing): members are grouped by reach in buckets of
// this many px; the ring holds at most kRingCells spots.
constexpr int kRingBucket=32;
constexpr size_t kRingCells=256;
// Members a reach group needs when its first field starts to plan to a ring
// (PLAN 3.4; its exit raises it to 8 if the battles' field quota pegs).
constexpr size_t kRingMembers=2;
// A production exit without headway for this many crowd windows, its
// birthplace clear, is done (twice as many hand it over to a rally).
constexpr uint8_t kExitWindows=1;
// Static bodies (see scanStill): a ground body that has not moved over
// kStillScans consecutive scans kStillScan ticks apart, and is not a Legion
// member on its way, is a soft obstacle. A field charges kSoftFactor times
// the step cost to enter an origin it covers (kSoftNear within a cell of
// one), so a group plans round a standing block of bodies (idle, settled,
// building, any player) instead of walking into it and waiting at its
// face; direct lines refuse to cross one.
constexpr uint32_t kStillScan=30;
constexpr uint16_t kStillScans=2;
constexpr uint32_t kSoftFactor=4;
constexpr uint32_t kSoftNear=3;                // ... within a cell of one
// Group awareness (see awareScan): every kAwareScan ticks each moving
// formation of at least kAwareMembers is a mover: its centroid, spread and
// the corridor it sweeps over the next kAwareAhead scans. A group whose way
// ahead (kAwareChain descent cells from its centroid) meets a mover it may
// see plans round it as a whole (its next field charges the corridor), and
// keeps doing so until the mover has been off its way for kAwareKeep scans.
// Formations of one player ordered within kConvoyTicks to points within
// kConvoyCells are one selection and never plan round each other.
constexpr uint32_t kAwareScan=30;
constexpr int kAwareMembers=8,kAwareAhead=3,kAwareChain=48,kAwareKeep=2;
constexpr uint32_t kConvoyTicks=90;
constexpr int kConvoyCells=32;
constexpr std::array<std::array<int,2>,8> kDirections{{
    {1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1}}};

uint64_t mix(uint64_t h,uint64_t v) {
    h^=v+0x9e3779b97f4a7c15ull+(h<<6)+(h>>2);
    h=(h^(h>>30))*0xbf58476d1ce4e5b9ull;
    return h^(h>>27);
}
int64_t isqrtFloor(uint64_t n) {return int64_t(isqrt64(n));}
}

struct LegionNavigator::Impl {
    // Static legality of every footprint origin for one mobility class.
    struct Plane {
        int maxDepth=0,minDepth=0,maxSlope=0,maxWaterSlope=0,footX=1,footZ=1;
        bool legacy=false;
        uint64_t epoch=0;   // World placement epoch + structure signature
        std::vector<uint8_t> legal;
        // Static connected component of every legal origin (-1 if illegal),
        // under the same step rule the field and the mover use.
        std::vector<int32_t> comp;
        // Per component: origin count and bounding box {minX,minZ,maxX,maxZ}.
        std::vector<int> compSize;
        std::vector<std::array<int,4>> compBox;
        uint64_t lastUse=0;
        // 1x1 cell legality (terrain, features, structure yards) the origin
        // plane is the footprint-rectangle test of; kept so a static change
        // recomputes only its rectangle (see refreshPlanes).
        std::vector<uint8_t> cell;
        int liveComps=0;    // labels with compSize>0 (dead labels are reused never)
        // Background first build (prebuildStep): 0 idle, 1 cells, 2 row
        // runs, 3 column runs, 4 labels; epoch stays ~0 until it completes.
        int stage=0,cursor=0,nextLabel=0;
        size_t head=0;
        std::vector<int> queue;
        std::vector<uint16_t> run;
        std::vector<int> column;
        std::vector<std::array<int,4>> pending;   // static changes during the build
        std::array<int,4> box{};                  // box of the component being flooded
    };
    // Running totals of the live fields (every Field lives in some group's
    // field or next): startField's cap test reads them instead of walking
    // every group, which cost O(groups) per call and O(groups^2) per tick
    // with many groups waiting at the cap. Derived, never hashed.
    struct FieldTotals {size_t fields=0,cells=0;};
    // Integer distance field from a group's goal origins, built with a
    // bounded bucket queue; resumable across ticks under the work quota.
    // A field covers only the bounding box of its seeds' static component
    // (every origin it can ever reach): a group penned in a small region
    // costs that region, not the whole map. Cells outside read kUnreached.
    struct Field {
        Field()=default;
        Field(const Field&)=delete;
        Field& operator=(const Field&)=delete;
        ~Field() {if(totals) {--totals->fields;totals->cells-=potential.size();}}
        FieldTotals* totals=nullptr;      // set once potential is sized (startField)
        int plane=-1;
        uint64_t epoch=0;
        uint64_t serial=0;                // unique per field built (identifies it to the aim memo)
        int W=0,x0=0,z0=0,fw=0,fh=0;      // map width; window origin and size (cells)
        std::vector<uint16_t> potential;  // fw*fh, row-major within the window
        bool inside(int x,int z) const {return x>=x0&&z>=z0&&x<x0+fw&&z<z0+fh;}
        size_t local(int x,int z) const {return size_t(z-z0)*size_t(fw)+size_t(x-x0);}
        uint16_t at(size_t cell) const {
            const int x=int(cell%size_t(W)),z=int(cell/size_t(W));
            return inside(x,z)?potential[local(x,z)]:kUnreached;
        }
        // Buckets keyed by potential + heuristic (see advance); the key of a
        // relaxed neighbour is at most (kSoftFactor+1)*kDiagonal above the
        // popped one, plus a mover corridor's charge (see Corridor).
        std::array<std::vector<int>,128> buckets;
        // Movers this field plans round (see awareScan): corridor segments
        // (px) and radius; a step inside one costs kSoftFactor times, within
        // two cells of its edge kSoftNear times. With a soft obstacle on top
        // a key rises by at most (2*kSoftFactor-1)*kDiagonal plus the
        // heuristic's kDiagonal, under the 128 buckets.
        struct Corridor {int64_t x0=0,z0=0,x1=0,z1=0,r=0;};
        std::vector<Corridor> avoid;
        uint32_t corridorCharge(int x,int z,int fx,int fz,uint32_t base) const {
            uint32_t extra=0;
            const int64_t px=int64_t(x)*16+fx*8,pz=int64_t(z)*16+fz*8;
            for(const auto& c:avoid) {
                const int64_t d2=segDist2(px,pz,c);
                if(d2<=c.r*c.r)return base*(kSoftFactor-1);
                if(d2<=(c.r+32)*(c.r+32))extra=base*(kSoftNear-1);
            }
            return extra;
        }
        static int64_t segDist2(int64_t px,int64_t pz,const Corridor& c) {
            const int64_t vx=c.x1-c.x0,vz=c.z1-c.z0,wx=px-c.x0,wz=pz-c.z0;
            const int64_t len2=vx*vx+vz*vz;
            int64_t qx=c.x0,qz=c.z0;
            if(len2>0) {
                const int64_t t=std::clamp<int64_t>(wx*vx+wz*vz,0,len2);
                qx=c.x0+vx*t/len2;qz=c.z0+vz*t/len2;
            }
            return (px-qx)*(px-qx)+(pz-qz)*(pz-qz);
        }
        uint32_t current=0;
        int tx=0,tz=0;bool aimed=false;    // heuristic target: the group's bodies
        std::vector<std::pair<uint32_t,int>> seedKeys;size_t seedNext=0;   // seeds by key, enqueued in order
        uint32_t heuristic(int x,int z) const {
            if(!aimed)return 0;
            const int dx=std::abs(x-tx),dz=std::abs(z-tz);
            return uint32_t(kOrthogonal*std::max(dx,dz)+(kDiagonal-kOrthogonal)*std::min(dx,dz));
        }
        size_t queued=0;
        bool done=false;
        // The window is a margin around the group's bodies and seeds, not
        // its whole static component: a body outside it (or cut off from
        // the seeds inside it) widens the group's next field.
        bool bounded=false;
        uint64_t work=0;
        // The build is A* toward the group's bodies with a consistent
        // (octile) heuristic, run to exhaustion: the finished potentials are
        // the exact distances, the same as plain Dijkstra, but the frontier
        // reaches the bodies first. A cell whose potential + heuristic is at
        // most the current key can never improve again, so a half-built
        // field already steers the bodies it has reached (descent only
        // visits lower potentials, which always end at a seed).
        // The command (player, issue tick) the field serves: its own
        // settled arrivals are its destination crowd, never soft obstacles.
        uint64_t command=~0ull;int softCounts=-1;
        bool ownArrivals=true;   // a settled arrival of `command` stood soft when the build started
        bool liftAllied=true;    // a liftable flyer of an ally of `command` stood soft then
        bool softened=false;   // some step was charged as a soft obstacle
        std::vector<int> seeds;   // the group's seeds when the build started
        uint64_t seedKey=0;       // seedsKey(seeds): its route-key index entry (see routeIndex)
        uint32_t started=0;       // tick the build started
        bool settled(size_t cell,uint16_t v) const {
            if(done)return v!=kUnreached;
            return v!=kUnreached&&uint32_t(v)+heuristic(int(cell%size_t(W)),int(cell/size_t(W)))<=current;
        }
    };
    struct Group {
        int id=0,player=0,plane=-1;
        uint32_t issuedTick=0;
        int minX=0,minZ=0,maxX=0,maxZ=0;
        std::vector<int> seeds;          // sorted unique goal origins (cell index)
        std::map<int,int> sharing;       // goal origin -> member count
        std::map<int,int> peak;          // goal origin -> most members it ever had (area size)
        int members=0;
        // A seed cell standing for the static component of every seed. Raw
        // component labels are renumbered on every plane rebuild, so the
        // region is compared live through this cell (see groupComp).
        int compCell=-1;
        // Bounding box of the members' origins when they joined, and whether
        // a bounded field already failed to cover a member (fields of this
        // group then span the whole component).
        int bodyMinX=0,bodyMinZ=0,bodyMaxX=-1,bodyMaxZ=-1;
        bool full=false;
        uint32_t waited=~0u-1;           // last tick a member stood still waiting for the field
        bool approach=false;             // seeds are approach points of unreachable goals
        // The command its members belong to (player, the point's issue
        // tick): their own settled arrivals are never soft obstacles to it.
        // A wanderer's stroll (wildlife, villagers milling about inside a
        // crowd) plans on terrain alone: soft=false.
        uint64_t command=~0ull;bool soft=true;
        LegionMission kind=LegionMission::Move;   // members' mission kind (never mixed)
        // Fields are shared between groups with the same plane and seeds
        // (see sharedField). A field is never written once done.
        std::shared_ptr<Field> field;    // the field members steer by (done or building)
        // After a static change the finished field keeps steering (the mover
        // re-proves every step) while its replacement builds in `next`.
        std::shared_ptr<Field> next;
        bool stale=false;
        uint64_t lastUse=0;
        uint32_t built=0;
        // Packed arrival slots for goals several members share, inside-out
        // by field potential; claimed on approach so the area fills from
        // the point outward and nobody has to cross a settled body.
        struct Slots {std::vector<int> cells;std::vector<uint8_t> taken;bool built=false,stale=false;uint16_t reach=0;};
        std::map<int,Slots> slots;
        // The destination area's bound per goal (see areaBound): (field
        // serial, (members, bound)). A function of the field, never hashed.
        std::map<int,std::pair<uint64_t,std::pair<int,uint32_t>>> areaBounds;
        // Group awareness (see awareScan): the movers (by command) its
        // fields plan round, their corridors as planned, and the scans each
        // has been off its way.
        std::vector<uint64_t> avoidCmd;std::vector<Field::Corridor> avoidSeg;std::vector<uint8_t> avoidOff;
        // Demand-driven refresh (see active): the last tick a member started
        // its update Moving, the last tick a member was blocked (no progress
        // for kBlockedRetry ticks outside its destination area), and the
        // explicit demand of a re-plan that is not a static change (aware
        // re-plan; cleared when a replacement swaps in). Decision state.
        // demand is folded into checksum with a tag when set (C27). The two
        // stamps are not: each is a function of the last two ticks' member
        // updates, whose inputs are hashed, and no path rebuilds Legion from
        // a snapshot (a rejoining client replays the bundle log from tick 0
        // and recomputes them; checked by a mid-game rejoin at W4).
        uint32_t movingTick=~0u-8,blockedTick=~0u-8;
        bool demand=false;
        // Stats only (never hashed): a stale refresh was suppressed while
        // the group was inactive (demandResumes counts its resumption).
        bool suppressed=false;
        // The scheduler lists the group is on and its byBuilt key (see
        // listGroup). Derived, never hashed.
        bool onBuilding=false,onNeedField=false,onStaleDone=false,onByBuilt=false;
        uint32_t byBuiltKey=0;
        std::array<uint64_t,2> routeKeys{};uint8_t routeCount=0;   // its routeIndex entries
        // ---- the reach ring (AR-06 part B, PLAN 3.4) ------------------------
        // A reach kind's group (attack, guard) is keyed by its target, the
        // target's origin when the group was seeded (centre) and the members'
        // reach bucket (reachPx / kRingBucket), not by an issue tick. Hashed
        // when set (C27). reachIds: its members, ascending (derived from
        // `members`, never hashed).
        int reachTarget=0,reachBucket=-1,reachCentre=-1;
        // MV-05 (see softBlocks): the last soft re-plan (hashed when set),
        // and this stripe cycle's Moving/Holding members: their origin sums
        // and the origin with the highest potential (derived from the
        // members' updates of the cycle, as movingTick is; never hashed).
        uint32_t softReplanTick=kNoTick;
        uint32_t scanCycle=~0u;int64_t scanSumX=0,scanSumZ=0;int scanCount=0,scanTail=-1;uint16_t scanTailPot=0;
        std::vector<int> reachIds;
        // Two or more members: arrival spots round the target at weapon reach.
        // cells: band 0 (centre distance R-1.5 .. R-0.5 bodies, in the
        // members' static component and in line of sight of the target: the
        // field's seeds) then the outer waiting bands, each in pseudo-angle
        // order from the approach bearing, footprints never overlapping;
        // owner: the member holding each (0 free). tried: built (or found
        // without a band-0 spot: the group keeps its point seed) once.
        struct Ring {
            bool tried=false,assigned=false;
            int R=0,band0=0;int64_t tx=0,tz=0,ax=0,az=0;   // px: target centre, approach axis
            std::vector<int> cells;std::vector<uint8_t> band;std::vector<int32_t> owner;
        } ring;
    };
    // Engaged (AR-06, PLAN 3.4): tickCombat braked this attacker in reach (or a
    // guard within 70 px of its ward) this tick, so its move() did not run.
    // Set by engaged() from the unit's own combat update, in the serial unit
    // loop; move() replaces it with the state its next update reaches. An
    // engaged body is settled and still for local steering (bodies behind
    // walk round it), never yields or parts, and is not a soft obstacle.
    enum State : uint8_t {None=0,Moving=1,Holding=2,Waiting=3,Arrived=4,Trapped=5,Engaged=6};
    // ---- mission goals ------------------------------------------------
    // Legion is the route provider for the goal of any supported ground
    // mission; the mission's own handler keeps its semantics (when to fire,
    // when in range, when to build, retirement events). A kind's Policy says
    // how its goal is served. See docs/legion-pathfinding.md "Mission goals".
    using Kind=LegionMission;
    struct Policy {
        // Legion arrival raises the native arrival event (0x500 on the leg's
        // mission). Otherwise the owner (combat, guard) ends the approach and
        // Legion only ever holds at the goal.
        bool completes=true;
        // The goal follows a unit: re-seeded every kReseedTicks once the
        // target has moved kReseedCells.
        bool moving=false;
        // Members of one command sent to one point share a destination area
        // (formation slots, per-goal packed slots).
        bool area=true;
        // The leg's native goal predicate (World::groundMissionAccepts) also
        // completes it, exactly as the native mover would.
        bool nativeAccept=false;
        // Group identity: the order's issue tick (one command), or the
        // leg's controller (every patrol lap gets its own field: the legs of
        // one patrol share an issue tick, and a field seeded at both ends
        // would pull a body toward the wrong end). The destination area is
        // still shared by the command's issue tick.
        bool perController=false;
        // The body leaves its goal at once (a patrol waypoint): arrival
        // releases its cells instead of standing on them as an anchor.
        bool passThrough=false;
        // The mission's own goal geometry (build rectangle, transport
        // circle, park ring: see accepts) is the only arrival. Legion
        // reaching its seed, or settling against a crowd, outside it raises
        // the native failed-approach event (0x200) and the mission handler
        // decides (build within reach, re-roll the park ring, retry the
        // pickup), exactly as after a failed native search.
        bool exact=false;
        // The arrival event belongs to the leg's transport mission (boarding
        // and surface-unload approaches), as in the native mover.
        bool transport=false;
        // The leg has no movement controller (repair, reclaim: the work
        // handler polls reach itself); the member key is the leg's target.
        bool keyed=false;
        // Never shares a group/field: each body's goal is its own (a teammate's
        // seed is not on this body's rectangle or within its reach).
        bool solo=false;
        // A generated production exit point: a body without headway and
        // with its whole birthplace clear is done (never blocks the lane).
        bool exitClear=false;
    };
    static Policy policy(Kind k) {
        Policy p;
        switch(k) {
        case Kind::Move:break;
        case Kind::Fight:p.nativeAccept=true;break;
        case Kind::Patrol:p.nativeAccept=true;p.perController=true;p.passThrough=true;break;
        case Kind::Attack:case Kind::Guard:p.completes=false;p.moving=true;p.area=false;break;
        case Kind::Build:p.nativeAccept=true;p.exact=true;p.area=false;p.solo=true;break;
        case Kind::Repair:case Kind::Reclaim:p.completes=false;p.keyed=true;p.area=false;p.solo=true;break;
        case Kind::Load:case Kind::Unload:p.nativeAccept=true;p.exact=true;p.transport=true;p.area=false;p.solo=true;break;
        case Kind::Park:p.nativeAccept=true;p.exact=true;p.area=false;p.solo=true;break;
        case Kind::Exit:p.area=false;p.solo=true;p.exitClear=true;break;
        default:break;
        }
        return p;
    }
    struct Point;
    struct Member {
        uint64_t controller=0;
        int group=0,goal=-1;              // goal origin cell index
        int lineCell=-1;                  // origin cell the line probe ran from
        bool line=false;
        State state=None;
        uint16_t best=kUnreached;         // best potential reached
        // Game-clock timers (AR-05, PLAN 3.1 T): the tick this body stopped
        // taking steps (kNoTick while it steps; Waiting does not end a hold)
        // and the tick its progress last improved. Thresholds read
        // now - stamp, so a rested body (heldRest) ages as fast as an
        // awake one. holdUpdates counts hold updates since the last step:
        // only the detour, yield, part and side-step back-offs read it.
        uint32_t heldSince=kNoTick,stallTick=0,progress=0xffffffffu;
        uint32_t holdUpdates=0;
        // The tick of this body's last move() call (rested or not). Within a
        // tick it tells a peer already updated from one still to come (see
        // peerStalledFor). Not hashed: it is only ever compared with the
        // current tick, which no earlier stamp equals, so a rejoining client
        // reads it the same.
        uint32_t movedTick=kNoTick;
        uint32_t trappedSince=0;uint64_t trappedEpoch=0;
        // The order's goal is statically unreachable from this body's region:
        // `goal` is the nearest reachable point to it, walked to and held
        // (order kept) until a static change re-resolves the real goal.
        bool approach=false;uint32_t approachSince=0;uint64_t approachEpoch=0;
        // Approach members only (0 otherwise): the least distance still to
        // go to the approach point so far, and the tick it last improved.
        uint32_t gainBest=0,gainTick=0;
        int real=-1;                      // the unreachable goal origin an approach member stands in for
        int requested=-1;                 // the goal origin the order named
        int slot=-1;                      // claimed slot index (shared goals)
        int detour=-1;                    // committed side-step cell
        uint32_t detourTicks=0;
        bool detourFace=true;             // side-step turns the body (keep-right) or not (shuffle)
        bool detourPass=false;            // side-step is a lane-discipline pass (moves at travel speed)
        uint32_t passUntil=0;             // tick until which a passing body keeps its new lane
        int8_t passRX=0,passRZ=0;         // the side it moved over to
        std::tuple<int,uint32_t,int32_t,int32_t> point{}; // player, command (convoy) tick, requested point
        // The 64-unit part of the click this member's order came in (the
        // order's own issuedTick; the point's tick is the convoy's, A2). The
        // pinwheel ranks a member within its part (C33). Hashed when it
        // differs from the point's tick.
        uint32_t part=0;
        Point* pt=nullptr;         // points[point]: alive while this member holds its ref
        std::vector<int> route;           // committed local detour around still bodies
        // Pinwheel (see pivotAim): the member's distance off a wall end it
        // rounds (cells; 0 not yet measured, -1 hugs as the inner file), and
        // the arc cell it aims at from origin pivotCell.
        int16_t pivotR=0;int pivotCell=-1,pivotAim=-1;
        uint32_t routeTicks=0,nextDetour=8,detourCount=0;
        int64_t detourBest=-1;            // progress when the last local detour was planned
        // "Close enough": the start of the current no-progress window and the
        // pixel distance to the requested point then (see settleWindow), and
        // the start of the run of still windows that ends there (kNoTick:
        // the last window was not still; read by production exits).
        uint32_t windowTick=0;int64_t windowDist=-1;uint32_t stillSince=kNoTick;
        // The settle rule (settleWindow): the potential bound (0 not yet
        // set), whether the last window found this body queued back to its
        // destination's settled crowd, and the slot re-choices it has made.
        // Hashed only when set (PLAN 3.0, C27).
        uint32_t settleP=0;bool queued=false;uint8_t rechoices=0;
        // Long-held throttle (see heldRest): what the last full update of a
        // held body saw -- its position, hit points, the static epoch and
        // the occupants of every origin around it. Hashed.
        int32_t restX=0,restZ=0,restHp=0;uint64_t restEpoch=0,restRing=0;bool rest=false;
        // Mission goal (kind, and for moving goals the origin and tick the
        // field was last seeded at). Only hashed for non-Move kinds.
        Kind kind=Kind::Move;
        int seedX=0,seedZ=0;uint32_t seededAt=0;
        // Memo of the field-descent aim (see aimCell): a pure function of
        // the exact position, goal, footprint, the finished field and the
        // static plane, so a held body (same inputs) reuses it. Derived:
        // never hashed, and a peer without it computes the same cell.
        struct Aim {
            int32_t x=0,z=0;int goal=-1;const UnitType* type=nullptr;
            uint64_t field=0,epoch=~0ull;int cell=-1;
        } aim;
    };

    World& w;
    std::vector<Plane> planes;
    // Declared before `groups`: the fields it owns update these as they die.
    FieldTotals fieldTotals;
    // startField found no field to evict at (tick, fieldsDone): until a
    // field finishes or the tick moves on, no group can become a victim
    // (see startField), so the next caller skips the O(groups) scan.
    uint64_t fieldsDone=0;
    uint32_t noVictimTick=~0u;uint64_t noVictimDone=~0ull;
    std::map<int,Group> groups;
    // Scheduler work lists (see tick), ascending ids:
    //   buildingIds  a field or refresh is building ((field && !done) ||
    //                next); a group whose shared field another group
    //                finished stays listed until tick next visits it;
    //   waitedIds    a member waited for a field (setWaited); dropped by
    //                tick once that is two ticks old;
    //   needFieldIds no field and no refresh;
    //   staleDoneIds a stale field (done or not: tick checks) and no
    //                refresh.
    // byBuilt: the groups with a field by (built, id), the eviction order.
    // Functions of each group's own field, next, stale, waited and built:
    // every write of those is followed by listGroup (waited: setWaited),
    // and TAK_LEGION_VERIFY checks each list against a full scan. Never
    // hashed; a peer replaying the same commands lists the same groups.
    std::set<int> buildingIds,waitedIds,needFieldIds,staleDoneIds;
    std::set<std::pair<uint32_t,int>> byBuilt;
    // Route-key index for sharedField: (group plane, seedsKey of a field's
    // seeds) -> ascending ids of the groups whose field or next has those
    // seeds. Every group sharedField can accept is listed under the key of
    // the asking group's seeds (a hash collision only adds candidates),
    // so a walk of that entry in id order finds the same first field as a
    // walk of every group. Maintained by listGroup; never hashed.
    std::map<std::pair<int,uint64_t>,std::set<int>> routeIndex;
    // LPROBE only (never hashed, never read by the sim): the four work
    // lists' sizes and the live groups, summed over the ticks.
    uint64_t probeListSum=0,probeGroupSum=0;
    // LPROBE only: wall time (ms, summed over the ticks) of syncStatic,
    // stampGrounded and liftFlyers. Measured only while the probe is on.
    double probeSyncMs=0,probeStampMs=0,probeLiftMs=0;
    static uint64_t seedsKey(const std::vector<int>& seeds) {
        uint64_t h=mix(0x7365656473ull,seeds.size());
        for(int c:seeds)h=mix(h,uint32_t(c));
        return h;
    }
    void listRoutes(Group& g) {
        std::array<uint64_t,2> want{};uint8_t n=0;
        for(const Field* f:{g.field.get(),g.next.get()})
            if(f&&!(n==1&&want[0]==f->seedKey))want[n++]=f->seedKey;
        if(n==g.routeCount&&std::is_permutation(want.begin(),want.begin()+n,g.routeKeys.begin()))return;
        unlistRoutes(g);
        for(uint8_t i=0;i<n;++i)routeIndex[{g.plane,want[i]}].insert(g.id);
        g.routeKeys=want;g.routeCount=n;
    }
    void unlistRoutes(Group& g) {
        for(uint8_t i=0;i<g.routeCount;++i) {
            const auto at=routeIndex.find({g.plane,g.routeKeys[i]});
            if(at==routeIndex.end())continue;
            at->second.erase(g.id);
            if(at->second.empty())routeIndex.erase(at);
        }
        g.routeCount=0;
    }
    static bool wantsBuilding(const Group& g) {return (g.field&&!g.field->done)||g.next;}
    static bool wantsNeedField(const Group& g) {return !g.field&&!g.next;}
    static bool wantsStaleDone(const Group& g) {return g.field&&g.stale&&!g.next;}
    // B1, demand-driven refresh: does anybody steer by this group's field?
    // A member started its update Moving or was blocked in the last two
    // ticks (a resting body updates at least every kRestStride ticks), or a
    // re-plan that is not a static change asked for it (demand). A stale
    // field of an inactive group keeps steering and stays shareable; its
    // refresh starts the tick the group becomes active.
    bool active(const Group& g) const {
        const uint32_t now=w.tickCounter_;
        return now-g.movingTick<=2||now-g.blockedTick<=2||g.demand;
    }
    void listGroup(Group& g) {
        auto put=[&](std::set<int>& ids,bool& on,bool want) {
            if(want==on)return;
            if(want)ids.insert(g.id);else ids.erase(g.id);
            on=want;
        };
        put(buildingIds,g.onBuilding,wantsBuilding(g));
        put(needFieldIds,g.onNeedField,wantsNeedField(g));
        put(staleDoneIds,g.onStaleDone,wantsStaleDone(g));
        const bool built=g.field!=nullptr;
        if(g.onByBuilt&&(!built||g.byBuiltKey!=g.built)) {byBuilt.erase({g.byBuiltKey,g.id});g.onByBuilt=false;}
        if(built&&!g.onByBuilt) {byBuilt.insert({g.built,g.id});g.byBuiltKey=g.built;g.onByBuilt=true;}
        listRoutes(g);
    }
    void unlistGroup(Group& g) {
        unlistRoutes(g);
        if(g.onBuilding)buildingIds.erase(g.id);
        if(g.onNeedField)needFieldIds.erase(g.id);
        if(g.onStaleDone)staleDoneIds.erase(g.id);
        if(g.onByBuilt)byBuilt.erase({g.byBuiltKey,g.id});
        waitedIds.erase(g.id);
    }
#ifndef NDEBUG
    [[noreturn]] void verifyFail(const char* what) const {
        std::fprintf(stderr,"TAK_LEGION_VERIFY tick %u: %s\n",w.tickCounter_,what);
        std::abort();
    }
#endif
    void setWaited(Group& g) {g.waited=w.tickCounter_;waitedIds.insert(g.id);}
    // The first id in `ids` above `id` (0: none; group ids start at 1).
    static int after(const std::set<int>& ids,int id) {
        const auto at=ids.upper_bound(id);
        return at==ids.end()?0:*at;
    }
    std::map<int,Member> members;
    // Unit id -> its node in `members` (std::map nodes are stable): the
    // per-update lookups (own member, bodies in the lane ahead) without a
    // tree walk. `members` keeps the ordered iteration.
    std::vector<Member*> memberIndex;
    Member* member(int id) const {
        return id>=0&&size_t(id)<memberIndex.size()?memberIndex[size_t(id)]:nullptr;
    }
    void putMember(int id,const Member& m) {
        auto& slot=members[id];slot=m;
        if(size_t(id)>=memberIndex.size())memberIndex.resize(size_t(id)+1,nullptr);
        memberIndex[size_t(id)]=&slot;
        markPass(id);
        if(slot.pt) {
            auto& ids=slot.pt->ids;
            const auto at=std::lower_bound(ids.begin(),ids.end(),id);
            if(at==ids.end()||*at!=id) {ids.insert(at,id);++slot.pt->partRefs[slot.part];}
        }
    }
    // Settled Legion arrivals: the goal origin each one completed on, and how
    // many times it has stepped aside since. A settled body may yield one
    // cell (never farther than one cell from that goal origin, so it stays
    // inside its destination area) to open a lane for a same-player member
    // whose own goal it walls in. Capped per body: no endless shuffling.
    struct Anchor {int goal=-1;uint8_t yields=0;std::tuple<int,uint32_t,int32_t,int32_t> point{};};
    std::map<int,Anchor> anchors;
    // Settled arrivals per destination (player, requested point): the crowd
    // a settling body's reach is sized by (see settleReach). A function of
    // `anchors`, kept with it by setAnchor/dropAnchor; never hashed.
    std::map<std::tuple<int,int32_t,int32_t>,int> anchorsAt;
    static std::tuple<int,int32_t,int32_t> destination(const std::tuple<int,uint32_t,int32_t,int32_t>& p) {
        return {std::get<0>(p),std::get<2>(p),std::get<3>(p)};
    }
    void setAnchor(int id,const Anchor& a) {
        dropAnchor(id);
        anchors[id]=a;markAnchor(id,true);++anchorsAt[destination(a.point)];
    }
    // ---- event-driven erasure (T7 B3) -----------------------------------
    // A settled body stays an anchor while it is alive and idle, or stands
    // on the leg it completed (that leg lingers until World retires it).
    // Parted bodies, approach-done bodies and yield steps need an idle body.
    // These used to be walked whole every tick; now an entry is checked when
    // its unit's orders change (World::noteOrders: the order helpers, leg
    // retirement, the mission dispatchers, the death edge), on registerMove,
    // and by prune's bounded cursor as the backstop.
    static bool settledBody(const Unit* u) {
        return u&&u->alive()&&(u->orders.empty()||(u->orders[World::currentLeg(u->orders)].mission.pending&0x500));
    }
    static bool idleBody(const Unit* u) {return u&&u->alive()&&u->orders.empty();}
    // Unit id -> it may have an entry in approachDone, parts or yielding (a
    // hint: set on insert, cleared when a check finds none). Lets the orders
    // event skip the tree lookups for the units in none of the maps.
    std::vector<uint8_t> watchMark;
    void markWatch(int id) {
        if(id<0)return;
        if(size_t(id)>=watchMark.size())watchMark.resize(size_t(id)+1,0);
        watchMark[size_t(id)]=1;
    }
    // Erase this unit's stale entries (anchors through dropAnchor, so
    // anchorsAt stays a count of live settled bodies).
    void validate(int id) {
        const bool watched=id>=0&&size_t(id)<watchMark.size()&&watchMark[size_t(id)];
        if(!watched&&!isAnchor(id))return;
        const Unit* u=w.unit(id);
        if(isAnchor(id)&&!settledBody(u)) {yielding.erase(id);dropAnchor(id);}
        if(!watched||idleBody(u))return;
        approachDone.erase(id);parts.erase(id);yielding.erase(id);
        watchMark[size_t(id)]=0;
    }
    std::map<int,Anchor>::iterator dropAnchor(std::map<int,Anchor>::iterator it) {
        const auto d=anchorsAt.find(destination(it->second.point));
        if(d!=anchorsAt.end()&&--d->second<=0)anchorsAt.erase(d);
        markAnchor(it->first,false);
        return anchors.erase(it);
    }
    void dropAnchor(int id) {
        if(const auto it=anchors.find(id);it!=anchors.end())dropAnchor(it);
        else markAnchor(id,false);
    }
    // Unit id -> whether it has an entry in `anchors` (a dense mirror): the
    // yield and crowd checks reject a non-anchor body without a tree walk.
    std::vector<uint8_t> anchorMark;
    bool isAnchor(int id) const {return id>=0&&size_t(id)<anchorMark.size()&&anchorMark[size_t(id)];}
    void markAnchor(int id,bool on) {
        if(id<0)return;
        if(size_t(id)>=anchorMark.size()) {if(!on)return;anchorMark.resize(size_t(id)+1,0);}
        anchorMark[size_t(id)]=on;
    }
    // Committed yield steps in progress: unit id -> target origin cell.
    struct Yield {int cell=-1;uint32_t ticks=0;};
    std::map<int,Yield> yielding;
    static constexpr uint8_t kMaxYields=3;
    // ---- static bodies (soft obstacles) ---------------------------------
    // The route field is static terrain only, so a standing block of bodies
    // sits right on every member's shortest way and each one walks into it
    // and then has to wait or detour locally (the local detour cannot see
    // round a block wider than its window). Each ground body is sampled
    // every kStillScan ticks, on its own residue (id % kStillScan == tick %
    // kStillScan: the scan is striped, one slice of the bodies a tick, never
    // the whole population at once); one at the same exact position on
    // kStillScans consecutive samples is still. A still body that is not a
    // Legion member, or is a settled Legion arrival, stamps its cells into
    // `soft` (any player: idle, building, guarding, an enemy's). Moving
    // crowds never qualify: every Legion member on its way (moving, held,
    // waiting, trapped behind a gate) is excluded, and anything else must
    // not have moved at all between two samples. A body that sets off as a
    // Legion member stops counting at once (registerMove lifts its stamp);
    // one ordered off otherwise stops at its next sample. A field already
    // built keeps its route: no re-planning when the block starts to move.
    // Per unit id: its last sample (hashed in id order, as the whole-scan
    // `stills` list was) and the stamp it holds. The stamp is derived from
    // the sample, the body's type and the kind/owner folded into softHash.
    struct SoftBody {
        int32_t x=0,z=0;uint16_t scans=0;bool present=false;   // the sample
        bool stamped=false;uint8_t kind=0;int player=0;          // the stamp
        int ox=0,oz=0,fx=0,fz=0;uint64_t owner=~0ull;
    };
    std::vector<SoftBody> softBodies;    // by unit id
    std::vector<int32_t> soft;           // per cell: the lowest id of the stamps covering it (0 none)
    std::vector<uint8_t> softKind;       // per cell: 0 none, 1 soft to all, 2 a settled arrival, 3 a liftable flyer (see softCell)
    size_t softCellCount=0;              // cells set in `soft`
    // Cells two or more stamps cover: every covering id, ascending (`soft`
    // holds the first). Rare (bodies do not overlap), so a map.
    std::map<int,std::vector<int>> softStack;
    // Per unit id: the command (player, issue tick) a settled Legion arrival
    // belongs to, else ~0: a field never treats its own arrivals as soft.
    std::vector<uint64_t> softOwner;
    // The commands softOwner holds, each with its number of owned ids:
    // "does any settled arrival belong to this command" is one lookup
    // (Stats::softownerLookups counts the linear scans it replaced: none).
    // Kept with softOwner by stamp/unstamp; a function of it, never hashed.
    std::map<uint64_t,uint32_t> softOwnerCount;
    bool ownsArrivals(uint64_t command) const {
        // ~0 marks an id without an owner: present whenever any id is owned.
        return command==~0ull?!softOwnerCount.empty():softOwnerCount.count(command)>0;
    }
    // softHash: an order-independent wrapping sum of one term per stamped
    // cell (cell, id, kind) and one per owned id (id, command), kept by
    // stamp/unstamp; softHashFull() recomputes it from the stamps
    // (TAK_LEGION_VERIFY checks the two every tick). softSerial counts the
    // stamp changes.
    uint64_t softSerial=0,softHash=0;
    static uint64_t softTerm(size_t c,int id,uint8_t kind) {return mix(mix(0x736f6674ull,uint64_t(c)<<32|uint32_t(id)),kind);}
    static uint64_t ownerTerm(int id,uint64_t owner) {return mix(mix(0x6f776e6572ull,uint64_t(uint32_t(id))),owner);}
    std::map<int,uint32_t> liftCells;    // player -> kind 3 cells it owns
    std::vector<int> liftSoftPlayers;    // owners of the kind 3 cells (liftable flyers), sorted unique
    // Landed flyers (mode 1) stand on the ground: retail stamps them into
    // its ground grid (5066f0, re-stamped by 51b370/4dafd2 on every mode or
    // cell change), so mobilePlacement refuses a ground step into one. But
    // World::occ_ leaves every flyer out, so Legion, which reads occ_ for
    // who stands where (holds, detours, parting, passing), saw free ground
    // there and pressed into them. `grounded` overlays them on occ_ (see
    // occAt); rebuilt each tick from hashed unit state, so never hashed.
    std::vector<int32_t> grounded;       // per cell: a landed flyer covering it (0 none)
    std::vector<int> groundedCells,groundedIds;   // cells set; landed flyer ids, ascending
    // Flyers lifted to let a group pass (Unit::legionLift): the spot each
    // one left, so the members still on their way keep it up. Rebuilt each
    // tick with `grounded`; never hashed.
    std::vector<int32_t> liftHome;
    std::vector<int> liftHomeCells,liftedIds;
    int32_t occAt(size_t c) const {const int32_t o=w.occ_[c];return o||groundedCells.empty()?o:grounded[c];}
    // passAhead's oncoming-sector mask (A7): per kPassBlock x kPassBlock
    // block of cells, the 16-sector directions (sector16) to their goals
    // of the members that may stand there, each widened by a sector either
    // way. Rebuilt at the end of tick() from every member whose goal is at
    // least 2*kPassCells-1 cells off (a superset of passAhead's peer filter,
    // which also asks state, orders and more than 2*kPassCells), over its
    // footprint widened by a cell; and OR'ed again for a member whenever its
    // goal is set and after each of its updates, so within a tick a block
    // keeps every direction any member standing in it has had. passAhead
    // skips its lane scan when no block the scan reads holds a direction
    // that can be oncoming (passOpposed): the scan would find no oncoming
    // body. Never hashed.
    static constexpr int kPassBlock=8;
    std::vector<uint16_t> passMask;
    std::vector<int> passMaskList;   // non-zero entries of passMask
    int passMaskW=0;
    // 16 sectors of 22.5 degrees from +x toward +z; each an interval of
    // directions (boundaries at tan 22.5 ~ 0.414214).
    static int sector16(int64_t x,int64_t z) {
        const int64_t ax=std::abs(x),az=std::abs(z);
        const int q=az*1000000<ax*414214?0:az<=ax?1:ax*1000000>=az*414214?2:3;
        return x>=0?(z>=0?q:15-q):(z>=0?7-q:8+q);
    }
    static uint16_t widenSector(uint16_t bits) {
        return uint16_t(bits|uint16_t(bits<<1)|uint16_t(bits>>15)|uint16_t(bits>>1)|uint16_t(bits<<15));
    }
    // Per travel octant ((dx+1)*3+dz+1): the sectors of every direction
    // passAhead counts as oncoming to it (a lattice of directions out to 64
    // cells, widened by a sector, so no direction's sector is missed).
    static const std::array<uint16_t,9>& passOpposed() {
        static const std::array<uint16_t,9> table=[] {
            std::array<uint16_t,9> t{};
            for(int dx=-1;dx<=1;++dx)for(int dz=-1;dz<=1;++dz) {
                if(!dx&&!dz)continue;
                uint16_t bits=0;
                for(int64_t z=-64;z<=64;++z)for(int64_t x=-64;x<=64;++x) {
                    if(!x&&!z)continue;
                    const int64_t dot=x*dx+z*dz;
                    if(dot<0&&100*dot*dot>kOncomingCos2*(x*x+z*z)*(dx*dx+dz*dz))bits|=uint16_t(1u<<sector16(x,z));
                }
                t[size_t((dx+1)*3+dz+1)]=widenSector(bits);
            }
            return t;
        }();
        return table;
    }
    void markPass(const Unit& u,const Member& m) {
        if(passMask.empty()||m.goal<0||!u.type)return;
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        const int64_t odx=m.goal%W-ox,odz=m.goal/W-oz;
        if(std::max(std::abs(odx),std::abs(odz))<2*kPassCells-1)return;
        const uint16_t bits=widenSector(uint16_t(1u<<sector16(odx,odz)));
        const int x0=std::max(ox-1,0),z0=std::max(oz-1,0),x1=std::min(ox+fx,w.occW_-1),z1=std::min(oz+fz,w.occH_-1);
        if(x0>x1||z0>z1)return;
        for(int bz=z0/kPassBlock;bz<=z1/kPassBlock;++bz)for(int bx=x0/kPassBlock;bx<=x1/kPassBlock;++bx) {
            uint16_t& b=passMask[size_t(bz)*passMaskW+bx];
            if(!b)passMaskList.push_back(bz*passMaskW+bx);
            b|=bits;
        }
    }
    void markPass(int id) {
        const Member* m=member(id);const Unit* u=m?w.unit(id):nullptr;
        if(u)markPass(*u,*m);
    }
    void buildPassMask() {
        if(w.occW_<=0||w.occH_<=0) {passMask.clear();passMaskList.clear();passMaskW=0;return;}
        passMaskW=(w.occW_+kPassBlock-1)/kPassBlock;
        const size_t n=size_t(passMaskW)*size_t((w.occH_+kPassBlock-1)/kPassBlock);
        if(passMask.size()!=n) {passMask.assign(n,0);passMaskList.clear();}
        for(int b:passMaskList)passMask[size_t(b)]=0;
        passMaskList.clear();
        for(const auto& [id,m]:members)if(m.goal>=0)if(const Unit* u=w.unit(id))markPass(*u,m);
    }
    // The ids of every flyer and every structure in World::units_ order,
    // so stampGrounded, syncStatic and computeCells walk those alone, in the
    // order the all-unit walk visited them. A unit's type never changes;
    // World::spawn only appends to units_, and compactRetiredUnits (the
    // only removal) keeps the order, so the lists follow units_ by reading
    // the units appended since the last call, and are rebuilt whenever the
    // entry they ended at moved (a compaction). They keep dead units until
    // then: every walk tests alive() as before. Never hashed.
    std::vector<int> flyerIds,structureIds;
    size_t listedUnits=0;int listedTail=0;
    void syncUnitLists() {
        const auto& units=w.units_;
        size_t from=listedUnits;
        if(from>units.size()||(from&&units[from-1].id!=listedTail)) {from=0;flyerIds.clear();structureIds.clear();}
        for(size_t i=from;i<units.size();++i) {
            const Unit& u=units[i];
            if(!u.type)continue;
            if(u.type->canFly)flyerIds.push_back(u.id);
            if(u.type->isStructure())structureIds.push_back(u.id);
        }
        listedUnits=units.size();listedTail=units.empty()?0:units.back().id;
    }
    // liftFlyers' gate: per kLiftBucket x kLiftBucket block of cells, how
    // many grounded and lift-home cells it holds (a cell in both counts
    // twice). Rebuilt with them by stampGrounded; never hashed.
    static constexpr int kLiftBucket=16;
    std::vector<uint16_t> liftBuckets;
    std::vector<int> liftBucketList;   // non-zero entries of liftBuckets
    int liftBucketW=0;
    void fillLiftBuckets() {
        liftBucketW=(w.occW_+kLiftBucket-1)/kLiftBucket;
        const size_t n=size_t(liftBucketW)*size_t((w.occH_+kLiftBucket-1)/kLiftBucket);
        if(liftBuckets.size()!=n)liftBuckets.assign(n,0);
        else for(int b:liftBucketList)liftBuckets[size_t(b)]=0;
        liftBucketList.clear();
        for(const auto* cells:{&groundedCells,&liftHomeCells})for(int c:*cells) {
            const int b=(c/w.occW_)/kLiftBucket*liftBucketW+(c%w.occW_)/kLiftBucket;
            if(!liftBuckets[size_t(b)]++)liftBucketList.push_back(b);
        }
    }
    // Does any grounded or lift-home cell lie in a bucket the inclusive cell
    // rectangle [x0,x1]x[z0,z1] touches? (False means none lies in it.)
    bool liftBucketsHit(int x0,int z0,int x1,int z1) const {
        x0=std::max(x0,0);z0=std::max(z0,0);x1=std::min(x1,w.occW_-1);z1=std::min(z1,w.occH_-1);
        if(x0>x1||z0>z1||liftBucketList.empty())return false;
        for(int bz=z0/kLiftBucket;bz<=z1/kLiftBucket;++bz)for(int bx=x0/kLiftBucket;bx<=x1/kLiftBucket;++bx)
            if(liftBuckets[size_t(bz)*liftBucketW+bx])return true;
        return false;
    }
    void stampGrounded() {
        if(w.occW_<=0)return;
        const size_t n=size_t(w.occW_)*w.occH_;
        if(grounded.size()!=n) {grounded.assign(n,0);groundedCells.clear();}
        for(int c:groundedCells)grounded[size_t(c)]=0;
        groundedCells.clear();
        std::vector<int> ids;
        if(liftHome.size()!=n) {liftHome.assign(n,0);liftHomeCells.clear();}
        for(int c:liftHomeCells)liftHome[size_t(c)]=0;
        liftHomeCells.clear();liftedIds.clear();
        // Only flyers are lifted (World::legionLiftable) or land.
        syncUnitLists();
        for(const int flyer:flyerIds) {
            const Unit* found=w.unit(flyer);
            if(!found)continue;
            const Unit& u=*found;
            if(u.legionLift&&u.alive()&&u.type) {
                liftedIds.push_back(u.id);
                const int fx=u.type->footX,fz=u.type->footZ;
                const int ox=footprintOrigin(u.legionLiftX,fx),oz=footprintOrigin(u.legionLiftZ,fz);
                for(int j=0;j<fz;++j)for(int i=0;i<fx;++i) {
                    const int cx=ox+i,cz=oz+j;
                    if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
                    int32_t& o=liftHome[size_t(cz)*w.occW_+cx];
                    if(!o) {o=u.id;liftHomeCells.push_back(cz*w.occW_+cx);}
                }
            }
            if(!u.alive()||u.embarked()||!u.type||!u.type->canFly||u.flightGroundMode!=1)continue;
            ids.push_back(u.id);
            const int fx=u.type->footX,fz=u.type->footZ;
            const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
            for(int j=0;j<fz;++j)for(int i=0;i<fx;++i) {
                const int cx=ox+i,cz=oz+j;
                if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
                int32_t& o=grounded[size_t(cz)*w.occW_+cx];
                if(!o) {o=u.id;groundedCells.push_back(cz*w.occW_+cx);}
            }
        }
        std::sort(ids.begin(),ids.end());
        fillLiftBuckets();
        // A flyer that took off is no soft obstacle any more (as a body
        // setting off as a member, see registerMove): clear it at once,
        // not at the next scan.
        for(int id:groundedIds)if(!std::binary_search(ids.begin(),ids.end(),id))unstamp(id);
        groundedIds.swap(ids);
    }
    static uint64_t commandKey(int player,uint32_t issue) {return uint64_t(uint32_t(player))<<32|issue;}
    static constexpr uint64_t kNoSoft=~0ull-1;   // a field / line that ignores soft obstacles
    // Is soft cell c an obstacle to `command`? Kind 1 stands for every
    // field; kind 2 (a settled Legion arrival) not for its own command.
    bool softCell(size_t c,uint64_t command) const {
        const uint8_t k=softKind[c];
        if(k==3) {
            // An idle landed flyer lifts for an allied group (see liftFlyers):
            // no obstacle to its fields, still one to anyone else's.
            const Unit* f=w.unit(soft[c]);
            return !f||!w.allied(f->player,int(uint32_t(command>>32)));
        }
        if(k!=2)return k!=0;
        const int32_t o=soft[c];
        return !(size_t(o)<softOwner.size()&&softOwner[size_t(o)]==command);
    }
    // Does a footprint at origin (x,z) cover a soft body other than the
    // command's own arrivals? `counts` (the footprint's window counts, see
    // SoftCounts; -1 none) answers without a walk unless a settled arrival
    // is near: none covered, or only bodies soft to every command.
    bool softAt(int x,int z,int fx,int fz,uint64_t command,int counts=-1) const {
        if(!softCellCount||command==kNoSoft)return false;
        if(counts>=0&&x>=0&&z>=0&&x<w.occW_&&z<w.occH_) {
            const uint32_t k=softCounts[size_t(counts)].at[size_t(z)*w.occW_+x];
            if(!(k&kCoverCount))return false;
            if(!(k&kArrivalCount))return true;
        }
        for(int j=0;j<fz;++j)for(int i=0;i<fx;++i) {
            const int cx=x+i,cz=z+j;
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
            if(softCell(size_t(cz)*w.occW_+cx,command))return true;
        }
        return false;
    }
    // The field's charge for entering origin (x,z): 2 covers a soft body,
    // 1 passes within a cell of one (a stream keeps a lane off the block's
    // face instead of filing along it), 0 clear.
    int softLevel(int x,int z,int fx,int fz,uint64_t command,int counts,bool own=true,bool lift=true) const {
        if(!softCellCount||command==kNoSoft)return 0;
        // The field asks this for every improving relaxation: answered from
        // the footprint's window counts, walking the window only near a
        // settled arrival (whose command matters).
        if(counts>=0) {
            const uint32_t k=softCounts[size_t(counts)].at[size_t(z)*w.occW_+x];
            if(!k)return 0;
            // No settled arrival of this command stands anywhere, and no
            // liftable flyer of an ally: every counted cell is an obstacle
            // to it.
            if(!((own&&(k&kArrivalCount))||(lift&&(k&kLiftCount))))return k&kCoverCount?2:1;
        }
        int level=0;
        for(int j=-1;j<=fz;++j)for(int i=-1;i<=fx;++i) {
            const int cx=x+i,cz=z+j;
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
            if(!softCell(size_t(cz)*w.occW_+cx,command))continue;
            if(i>=0&&j>=0&&i<fx&&j<fz)return 2;
            level=1;
        }
        return level;
    }
    // Per footprint class of a plane fields are built on, one word per
    // origin (one load per relaxation): byte 0 the soft cells (either kind)
    // its footprint covers, byte 1 those in the footprint grown by one
    // cell, byte 2 the settled-arrival cells among the latter, byte 3 the
    // liftable flyers' cells among them (a window holds at most 49 cells).
    // Derived from `soft`/`softKind` and kept up to date cell by cell,
    // never hashed.
    struct SoftCounts {int fx=0,fz=0;std::vector<uint32_t> at;};
    static constexpr uint32_t kCoverCount=0xff,kArrivalCount=0xff0000,kLiftCount=0xff000000;
    std::vector<SoftCounts> softCounts;
    void countCell(SoftCounts& k,int c,uint8_t kind,int delta) {
        const int W=w.occW_,H=w.occH_,cx=c%W,cz=c/W;
        for(int oz=std::max(0,cz-k.fz);oz<=std::min(H-1,cz+1);++oz)for(int ox=std::max(0,cx-k.fx);ox<=std::min(W-1,cx+1);++ox) {
            uint32_t& word=k.at[size_t(oz)*W+ox];
            if(kind==2)word=uint32_t(int64_t(word)+(int64_t(delta)<<16));
            if(kind==3)word=uint32_t(int64_t(word)+(int64_t(delta)<<24));
            word=uint32_t(int64_t(word)+(int64_t(delta)<<8));
            if(ox>cx-k.fx&&ox<=cx&&oz>cz-k.fz&&oz<=cz)word=uint32_t(int64_t(word)+delta);
        }
    }
    void countAll(int c,uint8_t kind,int delta) {for(auto& k:softCounts)countCell(k,c,kind,delta);}
    int softCountsFor(int fx,int fz) {
        if(w.occW_<=0)return -1;
        for(size_t i=0;i<softCounts.size();++i)if(softCounts[i].fx==fx&&softCounts[i].fz==fz)return int(i);
        SoftCounts k;k.fx=fx;k.fz=fz;
        const size_t n=size_t(w.occW_)*w.occH_;
        k.at.assign(n,0);
        if(softCellCount)for(size_t c=0;c<soft.size();++c)if(softKind[c])countCell(k,int(c),softKind[c],1);
        softCounts.push_back(std::move(k));
        return int(softCounts.size()-1);
    }
    template<class F> void forStampCells(const SoftBody& b,F&& fn) const {
        for(int j=0;j<b.fz;++j)for(int i=0;i<b.fx;++i) {
            const int cx=b.ox+i,cz=b.oz+j;
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
            fn(size_t(cz)*w.occW_+cx);
        }
    }
    void liftCount(int player,int delta) {
        auto& n=liftCells[player];
        n=uint32_t(int64_t(n)+delta);
        if(n&&(delta<0||n>1))return;
        if(!n)liftCells.erase(player);
        liftSoftPlayers.clear();
        for(const auto& [p,cells]:liftCells)liftSoftPlayers.push_back(p);
    }
    // Cell c's covering stamp becomes `id` of `kind` (0, 0: none). The
    // window counts follow the kind (only a kind change moves them).
    void cellSet(size_t c,int32_t id,uint8_t kind) {
        const uint8_t k0=softKind[c];const int32_t o=soft[c];
        if(k0!=kind) {
            if(k0)countAll(int(c),k0,-1);
            if(kind)countAll(int(c),kind,1);
        }
        if(k0==3)liftCount(softBodies[size_t(o)].player,-1);
        if(kind==3)liftCount(softBodies[size_t(id)].player,1);
        if(!o&&id)++softCellCount;
        else if(o&&!id)--softCellCount;
        soft[c]=id;softKind[c]=kind;
    }
    void stamp(int id) {
        SoftBody& b=softBodies[size_t(id)];
        b.stamped=true;
        forStampCells(b,[&](size_t c) {
            softHash+=softTerm(c,id,b.kind);
            if(!soft[c]) {cellSet(c,id,b.kind);return;}
            auto& v=softStack[int(c)];
            if(v.empty())v.push_back(soft[c]);
            v.insert(std::lower_bound(v.begin(),v.end(),id),id);
            if(v.front()==id)cellSet(c,id,b.kind);
        });
        if(b.owner!=~0ull) {
            if(size_t(id)>=softOwner.size())softOwner.resize(size_t(id)+1,~0ull);
            softOwner[size_t(id)]=b.owner;++softOwnerCount[b.owner];
            softHash+=ownerTerm(id,b.owner);
        }
        ++softSerial;
    }
    // Lifts id's stamp (none: nothing to do); its sample stays.
    void unstamp(int id) {
        if(id<=0||size_t(id)>=softBodies.size()||!softBodies[size_t(id)].stamped)return;
        SoftBody& b=softBodies[size_t(id)];
        forStampCells(b,[&](size_t c) {
            softHash-=softTerm(c,id,b.kind);
            const auto it=softStack.find(int(c));
            if(it==softStack.end()) {cellSet(c,0,0);return;}
            auto& v=it->second;
            v.erase(std::lower_bound(v.begin(),v.end(),id));
            const int first=v.front();
            if(v.size()==1)softStack.erase(it);
            if(soft[c]==id)cellSet(c,first,softBodies[size_t(first)].kind);
        });
        if(b.owner!=~0ull) {
            softOwner[size_t(id)]=~0ull;
            if(auto it=softOwnerCount.find(b.owner);--it->second==0)softOwnerCount.erase(it);
            softHash-=ownerTerm(id,b.owner);
        }
        b.stamped=false;
        ++softSerial;
    }
    // softHash from scratch: every stamp's terms (TAK_LEGION_VERIFY, C31).
    uint64_t softHashFull() const {
        uint64_t h=0;
        for(size_t id=0;id<softBodies.size();++id) {
            const SoftBody& b=softBodies[id];
            if(!b.stamped)continue;
            forStampCells(b,[&](size_t c) {h+=softTerm(c,int(id),b.kind);});
            if(b.owner!=~0ull)h+=ownerTerm(int(id),b.owner);
        }
        return h;
    }
    // The unit with id `id`, through World's id -> slot table.
    const Unit* bodyUnit(size_t id) const {
        if(id>=w.unitSlotById_.size())return nullptr;
        const int32_t slot=w.unitSlotById_[id];
        if(slot<0)return nullptr;
        if(size_t(slot)<w.units_.size()&&w.units_[size_t(slot)].id==int(id))return &w.units_[size_t(slot)];
        return w.unit(int(id));
    }
    // A reach member standing for its owner (AR-06): engaged, or held on its
    // own ring spot (a waiting band). Soft to other commands only.
    bool reachStill(const Unit& u,const Member& m) const {
        if(m.state==Engaged)return true;
        if(m.state!=Holding||m.slot<0||policy(m.kind).completes||!u.type)return false;
        return footprintOrigin(u.z,u.type->footZ)*width()+footprintOrigin(u.x,u.type->footX)==m.goal;
    }
    // ---- MV-05: a block that forms on a planned way ---------------------
    // Cells scanStill made soft over the last kStillScan ticks (one stripe
    // cycle; never hashed: a function of the stamps, which are). Every
    // kStillScan ticks they are binned into kSoftTile-cell tiles; tiles with
    // at least kSoftTileDense added cells are dense, 8-connected dense tiles
    // form a block, and a block whose box spans kSoftBlockCells or more
    // re-plans (once per kSoftReplanCooldown ticks) every active group whose
    // descent chain from its members' centroid or its tail (the member
    // with the highest potential) meets the block on cells soft to it
    // (PLAN 3.4, C21). Blocks leaving never re-plan.
    static constexpr int kSoftTile=16,kSoftTileDense=64,kSoftBlockCells=20,kSoftChain=48;
    static constexpr uint32_t kSoftReplanCooldown=300;
    // A block stays on the list kSoftBlockKeep ticks after it formed: a
    // group still far from it (its chain looks kSoftChain cells ahead) meets
    // it in a later cycle. Derived from the stamps' history; never hashed.
    static constexpr uint32_t kSoftBlockKeep=1800;
    std::vector<int> softAdded;
    std::vector<std::pair<uint32_t,std::array<int,4>>> softBlockList;
    void softBlocks() {
        const uint32_t now=w.tickCounter_;
        softBlockList.erase(std::remove_if(softBlockList.begin(),softBlockList.end(),
            [&](const auto& b) {return now-b.first>kSoftBlockKeep;}),softBlockList.end());
        newBlocks();
        if(softBlockList.empty())return;
        replanForBlocks();
    }
    void newBlocks() {
        if(softAdded.empty())return;
        std::sort(softAdded.begin(),softAdded.end());
        softAdded.erase(std::unique(softAdded.begin(),softAdded.end()),softAdded.end());
        const int W=w.occW_;
        if(W<=0) {softAdded.clear();return;}
        const int TW=(W+kSoftTile-1)/kSoftTile;
        std::map<int,std::array<int,5>> tiles;   // tile -> count, box
        for(const int c:softAdded) {
            const int x=c%W,z=c/W,t=(z/kSoftTile)*TW+x/kSoftTile;
            auto [it,fresh]=tiles.try_emplace(t,std::array<int,5>{0,x,z,x,z});
            auto& v=it->second;++v[0];
            v[1]=std::min(v[1],x);v[2]=std::min(v[2],z);v[3]=std::max(v[3],x);v[4]=std::max(v[4],z);
        }
        softAdded.clear();
        std::vector<int> dense;
        for(const auto& [t,v]:tiles)if(v[0]>=kSoftTileDense)dense.push_back(t);
        if(dense.empty())return;
        // Union-find over the dense tiles, in tile order.
        std::map<int,int> parent;
        for(const int t:dense)parent[t]=t;
        std::function<int(int)> root=[&](int t) {int& p=parent[t];return p==t?t:p=root(p);};
        for(const int t:dense)for(const int d:{1,TW-1,TW,TW+1}) {
            const int o=t+d;
            if(!parent.count(o)||(d!=TW&&std::abs(o%TW-t%TW)>1))continue;
            const int a=root(t),b=root(o);
            if(a!=b)parent[std::max(a,b)]=std::min(a,b);
        }
        std::map<int,std::array<int,4>> blocks;
        for(const int t:dense) {
            const auto& v=tiles[t];
            auto [it,fresh]=blocks.try_emplace(root(t),std::array<int,4>{v[1],v[2],v[3],v[4]});
            auto& b=it->second;
            b={std::min(b[0],v[1]),std::min(b[1],v[2]),std::max(b[2],v[3]),std::max(b[3],v[4])};
        }
        for(const auto& [r,b]:blocks)if(std::max(b[2]-b[0],b[3]-b[1])+1>=kSoftBlockCells)softBlockList.push_back({w.tickCounter_,b});
    }
    void replanForBlocks() {
        const uint32_t now=w.tickCounter_;
        for(auto& [id,g]:groups) {
            ++stats.groupLoopIters;
            if(!g.soft||!g.field||!g.field->done||g.next||g.seeds.empty()||g.scanCount==0||g.scanCycle!=now/kStillScan)continue;
            if(g.softReplanTick!=kNoTick&&now-g.softReplanTick<kSoftReplanCooldown)continue;
            const Field& f=*g.field;
            const Plane& p=plane(g.plane);
            const int fx=p.footX,fz=p.footZ,foot=std::max(fx,fz);
            const uint64_t command=g.command;
            const int counts=softCountsFor(fx,fz);
            const uint32_t nearSeed=uint32_t(2*foot*kOrthogonal);
            bool hit=false;
            const int Wp=width();
            const int centroid=int(g.scanSumZ/g.scanCount)*Wp+int(g.scanSumX/g.scanCount);
            for(const int start:{centroid,g.scanTail}) {
                if(start<0||hit)continue;
                int cell=start;
                for(int k=0;k<kSoftChain&&!hit;++k) {
                    const uint16_t v=f.at(size_t(cell));
                    if(v==kUnreached||v<nearSeed)break;
                    const int x=cell%Wp,z=cell/Wp;
                    for(const auto& [formed,b]:softBlockList)
                        if(formed>=g.built&&x+fx>b[0]&&x<=b[2]&&z+fz>b[1]&&z<=b[3]&&softAt(x,z,fx,fz,command,counts)) {hit=true;break;}
                    const int next=descend(p,f,x,z,g.seeds.front());
                    if(next<0)break;
                    cell=next;
                }
            }
            if(!hit)continue;
            g.softReplanTick=now;g.stale=true;g.demand=true;
            listGroup(g);
            ++stats.softReplans;
        }
    }
    void scanStill() {
        if(w.occW_<=0)return;
        const size_t n=size_t(w.occW_)*w.occH_;
        if(soft.size()!=n) {
            soft.assign(n,0);softKind.assign(n,0);softCellCount=0;softStack.clear();softCounts.clear();
            softOwner.clear();softOwnerCount.clear();liftCells.clear();liftSoftPlayers.clear();softHash=0;
            for(auto& b:softBodies) {b.stamped=false;b.owner=~0ull;}
        }
        // This tick's slice: the ids on its residue (ids start at 1).
        const uint32_t r=w.tickCounter_%kStillScan;
        const size_t bound=std::max(softBodies.size(),w.unitSlotById_.size());
        uint64_t processed=0;
        for(size_t id=r?r:kStillScan;id<bound;id+=kStillScan) {
            const Unit* found=bodyUnit(id);
            const Member* m=found?member(found->id):nullptr;
            if(!found||!found->alive()||found->embarked()||!found->type||(found->type->canFly&&found->flightGroundMode!=1)||
               found->type->isStructure()||(m&&m->state!=Arrived&&!reachStill(*found,*m))) {
                if(id<softBodies.size()&&softBodies[id].present) {
                    unstamp(int(id));
                    softBodies[id].present=false;softBodies[id].scans=0;
                }
                continue;
            }
            const Unit& u=*found;
            ++processed;
            if(id>=softBodies.size())softBodies.resize(id+1);
            SoftBody& b=softBodies[id];
            const bool same=b.present&&b.x==u.x.v&&b.z==u.z.v;
            b.scans=same?uint16_t(std::min<int>(b.scans+1,kStillScans)):0;
            b.present=true;b.x=u.x.v;b.z=u.z.v;
            if(b.scans<kStillScans) {unstamp(int(id));continue;}
            const int fx=u.type->footX,fz=u.type->footZ;
            const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
            uint64_t owner=~0ull;
            if(isAnchor(u.id)) {
                const auto a=anchors.find(u.id);
                owner=commandKey(std::get<0>(a->second.point),std::get<1>(a->second.point));
            }
            // An engaged attacker (or one held on its ring spot) is soft to
            // other commands only (a passing group plans round a firing
            // ring, PLAN 3.4 residual risk), never to its own attack.
            else if(m) {
                const auto g=groups.find(m->group);
                owner=g!=groups.end()?g->second.command:commandKey(u.player,std::get<1>(m->point));
            }
            const uint8_t kind=u.type->canFly&&w.legionLiftable(u,u.player)?3:owner!=~0ull?2:1;
            if(b.stamped&&b.ox==ox&&b.oz==oz&&b.fx==fx&&b.fz==fz&&b.kind==kind&&b.owner==owner&&b.player==u.player)continue;
            unstamp(int(id));
            b.ox=ox;b.oz=oz;b.fx=fx;b.fz=fz;b.kind=kind;b.owner=owner;b.player=u.player;
            // MV-05: cells a body standing still makes soft that were not
            // (a liftable flyer and an engaged or ring-held member never).
            if(kind!=3&&!m)forStampCells(b,[&](size_t c) {if(!soft[c])softAdded.push_back(int(c));});
            stamp(int(id));
        }
        stats.stillUnitsProcessed+=processed;
        stats.stillPerResidueMax=std::max<uint64_t>(stats.stillPerResidueMax,processed);
    }
#ifndef NDEBUG
    // TAK_LEGION_VERIFY (C31): the incremental soft state against the
    // stamps. The hash every tick; the grid, owner and window counts once a
    // scan period.
    template<class Fail> void verifySoft(Fail&& fail) {
        ++stats.softHashVerifyTicks;
        if(softHashFull()!=softHash)fail("softHash differs from its recompute over the stamps");
        if(w.tickCounter_%kStillScan!=0||soft.empty())return;
        std::vector<int32_t> first(soft.size(),0);
        std::map<int,std::vector<int>> stack;
        std::map<uint64_t,uint32_t> owners;
        for(size_t id=0;id<softBodies.size();++id) {
            const SoftBody& b=softBodies[id];
            if(!b.stamped)continue;
            if(!b.present||b.scans<kStillScans)fail("a stamp without a still sample");
            forStampCells(b,[&](size_t c) {
                if(!first[c])first[c]=int32_t(id);
                else {auto& v=stack[int(c)];if(v.empty())v.push_back(first[c]);v.push_back(int(id));}
            });
            if(b.owner!=~0ull) {
                ++owners[b.owner];
                if(size_t(id)>=softOwner.size()||softOwner[id]!=b.owner)fail("softOwner differs from the stamp");
            }
        }
        if(first!=soft)fail("soft differs from the stamps");
        if(stack!=softStack)fail("softStack differs from the stamps");
        if(owners!=softOwnerCount)fail("softOwnerCount differs from the stamps");
        size_t cells=0;
        std::map<int,uint32_t> lift;
        for(size_t c=0;c<soft.size();++c) {
            const uint8_t kind=soft[c]?softBodies[size_t(soft[c])].kind:0;
            if(softKind[c]!=kind)fail("softKind differs from the covering stamp");
            cells+=soft[c]!=0;
            if(kind==3)++lift[softBodies[size_t(soft[c])].player];
        }
        if(cells!=softCellCount)fail("softCellCount differs from the grid");
        if(lift!=liftCells)fail("liftCells differs from the grid");
        for(const auto& k:softCounts) {
            SoftCounts fresh;fresh.fx=k.fx;fresh.fz=k.fz;fresh.at.assign(soft.size(),0);
            for(size_t c=0;c<soft.size();++c)if(softKind[c])countCell(fresh,int(c),softKind[c],1);
            if(fresh.at!=k.at)fail("soft window counts differ from a recount");
        }
    }
#endif
    // Cells claimed by arrival slots of every group sent to one point in one
    // command (mixed footprints form one group per class but share the area).
    // A shared point's members get their slots all at once, by formation:
    // each member's offset from the group's centroid, scaled into the
    // packed area around the point (see assignFormation).
    struct Point {
        int refs=0;std::set<int> cells;
        // Ids of the members whose entry names this point, ascending (the
        // order `members` iterates them in): assignFormation and the pivot
        // centroid read these instead of scanning every member. Derived,
        // never hashed.
        std::vector<int> ids;
        bool assigned=false,tried=false;
        int64_t centreX=0,centreZ=0,scaleNum=1,scaleDen=1,limit=0;   // px
        // The members' live centroid (px) on tick liveTick: derived from
        // positions on demand (see pivotAim), so never hashed.
        int64_t liveX=0,liveZ=0;uint32_t liveTick=~0u;
        int64_t awareX=0,awareZ=0;bool awareSeen=false;   // centroid (px) at the last awareScan (hashed)
        SlotShape shape;   // I2 probe, set by assignFormation (never hashed)
        // Members per 64-unit part (Member::part) over `ids`: the pinwheel's
        // per-part cap and threshold (C33). Derived, never hashed.
        std::map<uint32_t,int> partRefs;
        // The formation was taken while the convoy was still open, by a first
        // part under one uplink tick (see formationSlot); derived, never hashed.
        bool provisional=false;
        // Per-part live centroid (px; sum x, sum z, count) on tick partTick,
        // for a point of several parts (see pivotAim). Derived, never hashed.
        std::map<uint32_t,std::array<int64_t,3>> partLive;uint32_t partTick=~0u;
        // Deferred slot assignment (A2): members still waiting for a slot,
        // front first, handed out kSlotsPerTick a tick (hashed while not
        // empty); handTick is the tick of the last hand-out (a per-tick
        // guard, never hashed).
        std::vector<int> queue;uint32_t handTick=~0u;
    };
    std::map<std::tuple<int,uint32_t,int32_t,int32_t>,Point> points;
    void slotCells(const Member& m,int fx,int fz,bool claim) {
        auto& cells=(m.pt?*m.pt:points[m.point]).cells;
        const int W=width(),x=m.goal%W,z=m.goal/W;
        for(int j=0;j<fz;++j)for(int i=0;i<fx;++i) {
            const int c=(z+j)*W+x+i;
            if(claim)cells.insert(c);else cells.erase(c);
        }
    }
    bool slotFree(const Member& m,int cell,int fx,int fz) const {
        const Point* pt=m.pt;
        if(!pt) {const auto found=points.find(m.point);if(found==points.end())return true;pt=&found->second;}
        const int W=width(),x=cell%W,z=cell/W;
        for(int j=0;j<fz;++j)for(int i=0;i<fx;++i)if(pt->cells.count((z+j)*W+x+i))return false;
        return true;
    }
    int nextGroup=1;
    uint64_t structureSignature=0,epoch=0,lastWorldEpoch=~0ull;
    uint64_t fieldSerial=0;   // fields built so far (Field::serial)
    // Plane rebuild work (cells) not yet charged to the per-tick quota.
    uint64_t planeDebt=0;
    int pruneCursor=0;
    uint64_t quotaPegRun=0;   // consecutive ticks the field quota ran out (Stats::quotaPegRunMax)
    // Goal resolution per (plane, requested origin, body region) on the
    // current static epoch: a whole selection clicking one far point (or a
    // re-resolution after a change) searches once, not once per body. Pure
    // function of the plane, so a cache, never hashed; dropped on any epoch
    // change or plane reuse.
    std::map<std::tuple<int,int,int,int>,std::pair<int,int>> resolved;
    uint64_t resolvedEpoch=~0ull;
    // Observation only (see Stats); mutable so const helpers count work.
    mutable Stats stats;
    // Ring cells formationCell has walked (the hand-out's per-tick budget
    // reads it; Stats is observation only). Derived, never hashed.
    mutable uint64_t ringCells=0;
    SlotShape lastShape;   // the last assignFormation's (I2 probe)
#ifndef NDEBUG
    Stats verified;        // the Stats at the previous verifyIndexes
#endif
    explicit Impl(World& world):w(world) {}

    int width() const {return w.hW_;}
    int height() const {return w.hH_;}
    // W5 step 0: the claims invariant over every point (LegionNavigator::ClaimsAudit).
    ClaimsAudit claimsAudit() const {
        ClaimsAudit a;
        const int W=width();
        std::map<const Point*,std::map<int,int>> claimed;   // point -> cell -> members claiming it
        for(const auto& [id,m]:members) {
            if(m.slot<0)continue;
            const Unit* u=w.unit(id);
            if(!u||!u->type)continue;
            const Point* pt=m.pt;
            if(!pt) {const auto found=points.find(m.point);if(found!=points.end())pt=&found->second;}
            ++a.members;
            if(!pt) {++a.dangling;continue;}
            auto& cells=claimed[pt];
            const int x=m.goal%W,z=m.goal/W;
            for(int j=0;j<u->type->footZ;++j)for(int i=0;i<u->type->footX;++i) {
                const int c=(z+j)*W+x+i;
                if(++cells[c]>1)++a.overlaps;
                if(!pt->cells.count(c))++a.missing;
            }
        }
        // Reach rings (AR-06 B): every spot's owner holds it as its slot, and
        // every ring member owns its spot.
        for(const auto& [gid,g]:groups) {
            if(g.ring.band0==0)continue;
            for(size_t i=0;i<g.ring.owner.size();++i)if(const int o=g.ring.owner[i]) {
                const Member* mm=member(o);
                if(!mm||mm->group!=gid||mm->slot!=int(i)||mm->goal!=g.ring.cells[i])++a.dangling;
            }
        }
        for(const auto& [id,m]:members) {
            const auto g=groups.find(m.group);
            if(g!=groups.end()&&g->second.ring.band0>0&&m.slot>=0&&
               (size_t(m.slot)>=g->second.ring.owner.size()||g->second.ring.owner[size_t(m.slot)]!=id))++a.missing;
        }
        for(const auto& [key,pt]:points) {
            const auto found=claimed.find(&pt);
            for(const int c:pt.cells)
                if(found==claimed.end()||!found->second.count(c))++a.orphans;
        }
        return a;
    }
    bool placementPlane() const {
        return !w.mapPlacementCells_.empty()&&w.mapPlacementCells_.size()==size_t(w.hW_)*w.hH_;
    }

    // ---- static plane -------------------------------------------------
    int planeFor(const UnitType& t) {
        const bool legacy=!placementPlane();
        const int maxDepth=t.canFly||t.domain!=UnitType::Domain::Ground?10000:(t.maxWaterDepth>0?t.maxWaterDepth:20);
        const int minDepth=t.domain==UnitType::Domain::Water?(t.minWaterDepth>0?t.minWaterDepth:13):-10000;
        for(size_t i=0;i<planes.size();++i) {
            const auto& p=planes[i];
            if(p.legacy==legacy&&p.footX==t.footX&&p.footZ==t.footZ&&(legacy||
               (p.maxDepth==maxDepth&&p.minDepth==minDepth&&p.maxSlope==t.maxSlope&&p.maxWaterSlope==t.maxWaterSlope))) {
                return int(i);
            }
        }
        if(planes.size()>=kMaxPlanes) {
            // Evict the least recently used plane no live group refers to.
            int victim=-1;uint64_t iters=0;
            for(size_t i=0;i<planes.size();++i) {
                bool used=false;
                for(const auto& [id,g]:groups) {++iters;if(g.plane==int(i)) {used=true;break;}}
                if(!used&&(victim<0||planes[i].lastUse<planes[size_t(victim)].lastUse))victim=int(i);
            }
            stats.groupLoopIters+=iters;
            if(victim>=0) {
                resolved.clear();
                planes[size_t(victim)]=Plane{};
                auto& p=planes[size_t(victim)];
                p.legacy=legacy;p.footX=t.footX;p.footZ=t.footZ;p.maxDepth=maxDepth;p.minDepth=minDepth;
                p.maxSlope=t.maxSlope;p.maxWaterSlope=t.maxWaterSlope;p.epoch=~0ull;
                return victim;
            }
        }
        Plane p;p.legacy=legacy;p.footX=t.footX;p.footZ=t.footZ;p.maxDepth=maxDepth;p.minDepth=minDepth;
        p.maxSlope=t.maxSlope;p.maxWaterSlope=t.maxWaterSlope;p.epoch=~0ull;
        planes.push_back(std::move(p));
        return int(planes.size()-1);
    }
    // Same cell rules as World::mobilePlacement with the entity slot empty;
    // retailMobilePlacement's per-cell loop makes a footprint legal exactly
    // when every covered cell is (plus the footprint's own bounds test).
    void buildPlane(Plane& p) {
        const int W=width(),H=height();
        p.legal.assign(size_t(std::max(0,W))*std::max(0,H),0);
        ++stats.planeBuilds;
        if(W<=0||H<=0)return;
        if(p.legacy) {
            const auto* grid=legacyGrid(p);
            if(!grid||grid->empty())return;
            const int foot=std::clamp(std::max(p.footX,p.footZ),1,15);
            for(int z=0;z<H;++z)for(int x=0;x<W;++x)
                p.legal[size_t(z)*W+x]=grid->fits(x+foot/2,z+foot/2,foot);
            return;
        }
        p.cell.assign(size_t(W)*H,0);
        auto& cell=p.cell;
        computeCells(p,0,0,W-1,H-1);
        // Separable rectangle test: run of legal cells to the right >= footX
        // on footZ consecutive rows, and the footprint's bounds (x+fx<W).
        std::vector<uint16_t> run(size_t(W)*H,0);
        for(int z=0;z<H;++z) {
            int r=0;
            for(int x=W-1;x>=0;--x) {
                r=cell[size_t(z)*W+x]?std::min(r+1,0xffff):0;
                run[size_t(z)*W+x]=uint16_t(r);
            }
        }
        for(int x=0;x<W;++x) {
            int r=0;
            for(int z=H-1;z>=0;--z) {
                r=run[size_t(z)*W+x]>=p.footX?r+1:0;
                p.legal[size_t(z)*W+x]=r>=p.footZ&&x+p.footX<W&&z+p.footZ<H;
            }
        }
    }
    // 1x1 cell legality over the inclusive cell rectangle [x0,x1]x[z0,z1]:
    // the mover's per-cell rule with the entity slot empty, then structures
    // stamped with their yard maps by the mover's rule
    // (World::mobilePlacement): '.' is open, a closable 'c' yard is open
    // while the script holds it open. Returns the cells visited (work).
    // Live structures in unit order, gathered once per syncStatic: a refresh
    // recomputes a few small rectangles on every plane, and walking every
    // unit (10k+ in a battle) per rectangle per plane cost 5-10 ms per plane
    // for a 36-cell change. Null outside syncStatic (callers walk units).
    const std::vector<const Unit*>* structureList=nullptr;
    uint64_t computeCells(Plane& p,int x0,int z0,int x1,int z1) {
        const int W=width(),H=height();
        x0=std::max(x0,0);z0=std::max(z0,0);x1=std::min(x1,W-1);z1=std::min(z1,H-1);
        if(x0>x1||z0>z1)return 0;
        auto cellAt=[&](int cx,int cz) {
            const auto& source=w.mapPlacementCells_[size_t(cz)*W+cx];
            RetailPlacementCell result;
            result.low=source.low;
            result.high=std::max({w.heights_[size_t(cz)*W+cx],w.heights_[size_t(cz)*W+cx+1],
                                 w.heights_[size_t(cz+1)*W+cx],w.heights_[size_t(cz+1)*W+cx+1]});
            result.feature=source.feature;
            if(source.feature<0xfffa||source.feature==0xfffe) {
                const int ox=cx-(source.feature==0xfffe?source.backX:0);
                const int oz=cz-(source.feature==0xfffe?source.backZ:0);
                const auto index=w.mapPlacementCells_[size_t(oz)*W+ox].feature;
                const bool blocking=index<w.mapPlacementTypes_.size()&&w.mapPlacementTypes_[index].blocking;
                result.feature=blocking?0:0xffff;
            }
            return result;
        };
        for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x)
            p.cell[size_t(z)*W+x]=x+1<W&&z+1<H&&retailMobilePlacement(x,z,1,1,W,H,w.seaLevel_,p.maxDepth,p.minDepth,
                p.maxSlope,p.maxWaterSlope,0,false,1,cellAt,[](uint16_t){return 0x20u;},
                [](uint16_t){return RetailPlacementEntity{};});
        uint64_t work=uint64_t(x1-x0+1)*uint64_t(z1-z0+1);
        auto stampStructure=[&](const Unit& u) {
            if(!u.alive()||u.embarked()||!u.type||!u.type->isStructure())return;
            const int ux=footprintOrigin(u.x,u.type->footX),uz=footprintOrigin(u.z,u.type->footZ);
            if(ux>x1||uz>z1||ux+u.type->footX<=x0||uz+u.type->footZ<=z0)return;
            const bool opened=yardOpen(u.id);
            for(int j=0;j<u.type->footZ;++j)for(int i=0;i<u.type->footX;++i) {
                const int cx=ux+i,cz=uz+j;
                if(cx<x0||cz<z0||cx>x1||cz>z1)continue;
                if(!u.type->yardMap.empty()) {
                    const char yard=u.type->yardMap[size_t(j)*u.type->footX+i];
                    if(yard=='.'||(opened&&(yard=='c'||yard=='C')))continue;
                }
                p.cell[size_t(cz)*W+cx]=0;++work;
            }
        };
        if(structureList)for(const Unit* u:*structureList)stampStructure(*u);
        else {
            syncUnitLists();
            for(const int id:structureIds)if(const Unit* u=w.unit(id))stampStructure(*u);
        }
        return work;
    }
    // (World::unitScript: the dense mirror of unitScripts_, kept by every
    // insert and erase of it.)
    bool yardOpen(int id) const {
        const auto* script=w.unitScript(id);
        return script&&script->yardOpen;
    }
    // Label static components by flood fill in cell order (deterministic).
    void labelPlane(Plane& p) {
        const int W=width(),H=height();
        p.comp.assign(p.legal.size(),-1);
        p.compSize.clear();p.compBox.clear();
        std::vector<int> queue;int next=0;
        for(int start=0;start<W*H;++start) {
            if(!p.legal[size_t(start)]||p.comp[size_t(start)]>=0)continue;
            p.comp[size_t(start)]=next;queue.assign(1,start);
            for(size_t head=0;head<queue.size();++head) {
                const int x=queue[head]%W,z=queue[head]/W;
                for(const auto& d:kDirections) {
                    if(!step(p,x,z,d[0],d[1]))continue;
                    const int c=(z+d[1])*W+x+d[0];
                    if(p.comp[size_t(c)]<0) {p.comp[size_t(c)]=next;queue.push_back(c);}
                }
            }
            std::array<int,4> box{W,H,-1,-1};
            for(int c:queue) {
                box[0]=std::min(box[0],c%W);box[1]=std::min(box[1],c/W);
                box[2]=std::max(box[2],c%W);box[3]=std::max(box[3],c/W);
            }
            p.compSize.push_back(int(queue.size()));p.compBox.push_back(box);
            ++next;
        }
        p.liveComps=next;
    }
    // ---- incremental static change ------------------------------------
    // A static change (feature placed/removed, corpse, structure built or
    // gone, yard opened/closed) touches a few cells. Recompute the cell layer
    // there, the origins whose footprint covers it, and repair the component
    // labels locally; whole-map rebuilds were 10 ms spikes several times a
    // tick in a battle. Exactly equal to buildPlane+labelPlane up to label
    // numbering (labels are only ever compared, sized and boxed); the
    // legion_planeincremental test checks that after random changes.
    // Returns the work done (cells), 0 if no origin changed legality.
    // Changes far apart are refreshed one cluster at a time, as if each had
    // arrived alone: relabel works in a window round the bounding box of
    // everything it is given, and a corpse in one corner plus a building in
    // another made that window most of the map -- a whole-map flood per
    // plane (8-11 ms each, 20-43 ms per tick on Ulasem with 5 planes) for a
    // few dozen changed cells. Clusters are rectangles whose boxes, grown by
    // the relabel margin plus the footprint, overlap; cluster order is the
    // order of each cluster's first rectangle.
    uint64_t refreshPlane(Plane& p,const std::vector<std::array<int,4>>& rects,bool& changedAny,std::array<int,4>* box=nullptr) {
        const int grow=kRelabelMargin+std::max(p.footX,p.footZ)+1;
        std::vector<int> cluster(rects.size());
        for(size_t i=0;i<rects.size();++i)cluster[i]=int(i);
        auto root=[&](int i) {while(cluster[size_t(i)]!=i)i=cluster[size_t(i)]=cluster[size_t(cluster[size_t(i)])];return i;};
        for(size_t i=0;i<rects.size();++i)for(size_t j=i+1;j<rects.size();++j) {
            const auto& a=rects[i];const auto& b=rects[j];
            if(a[0]-grow<b[0]+b[2]+grow&&b[0]-grow<a[0]+a[2]+grow&&a[1]-grow<b[1]+b[3]+grow&&b[1]-grow<a[1]+a[3]+grow) {
                const int ra=root(int(i)),rb=root(int(j));
                if(ra!=rb)cluster[size_t(std::max(ra,rb))]=std::min(ra,rb);
            }
        }
        uint64_t work=0;
        if(box)*box={0,0,-1,-1};
        std::vector<std::array<int,4>> part;
        for(size_t i=0;i<rects.size();++i) {
            if(root(int(i))!=int(i))continue;
            part.clear();
            for(size_t j=i;j<rects.size();++j)if(root(int(j))==int(i))part.push_back(rects[j]);
            std::array<int,4> b{0,0,-1,-1};
            work+=refreshCluster(p,part,changedAny,&b);
            if(box&&b[2]>=0)*box=(*box)[2]<0?b:std::array<int,4>{std::min((*box)[0],b[0]),std::min((*box)[1],b[1]),
                std::max((*box)[2],b[2]),std::max((*box)[3],b[3])};
        }
        return work;
    }
    uint64_t refreshCluster(Plane& p,const std::vector<std::array<int,4>>& rects,bool& changedAny,std::array<int,4>* box) {
        const int W=width(),H=height();
        uint64_t work=0;
        for(const auto& r:rects)work+=computeCells(p,r[0],r[1],r[0]+r[2]-1,r[1]+r[3]-1);
        std::vector<int> added,removed;
        int bx0=W,bz0=H,bx1=-1,bz1=-1;
        for(const auto& r:rects) {
            const int x0=std::max(0,r[0]-p.footX+1),z0=std::max(0,r[1]-p.footZ+1);
            const int x1=std::min(W-1,r[0]+r[2]-1),z1=std::min(H-1,r[1]+r[3]-1);
            for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x) {
                bool ok=x+p.footX<W&&z+p.footZ<H;
                for(int j=0;ok&&j<p.footZ;++j)for(int i=0;ok&&i<p.footX;++i)ok=p.cell[size_t(z+j)*W+x+i]!=0;
                work+=uint64_t(p.footX)*p.footZ;
                const size_t c=size_t(z)*W+x;
                if(bool(p.legal[c])==ok)continue;
                p.legal[c]=ok;(ok?added:removed).push_back(int(c));
                bx0=std::min(bx0,x);bz0=std::min(bz0,z);bx1=std::max(bx1,x);bz1=std::max(bz1,z);
            }
        }
        if(added.empty()&&removed.empty())return work;
        changedAny=true;
        if(box)*box={bx0,bz0,bx1,bz1};
        return work+relabel(p,added,removed,bx0,bz0,bx1,bz1);
    }
    static constexpr int kRelabelMargin=8;   // relabel's local window margin (cells)
    void emptyBox(std::array<int,4>& b) const {b={width(),height(),-1,-1};}
    // Scratch marks for splitSearch (a cache, never state).
    std::vector<uint32_t> seenGen;std::vector<int> seenBy;uint32_t markGen=0;
    // Interleaved searches of the current plane from `reps` (one cell each),
    // one cell per search per round. Searches that meet are one component.
    // A class of searches that runs dry has visited a whole component cut
    // off from the rest: it takes a fresh label (sizes, boxes and `touched`
    // kept exact). Stops once at most one class is still open; that one
    // keeps its labels. ~0 if the work passes a whole map (caller relabels).
    uint64_t splitSearch(Plane& p,const std::vector<int>& reps,std::set<int>& touched) {
        const int W=width();const size_t n=p.legal.size();
        if(seenGen.size()!=n) {seenGen.assign(n,0);seenBy.assign(n,0);markGen=0;}
        if(++markGen==0) {std::fill(seenGen.begin(),seenGen.end(),0);markGen=1;}
        const int k=int(reps.size());
        std::vector<std::vector<int>> q(static_cast<size_t>(k));std::vector<size_t> head(static_cast<size_t>(k),0);
        std::vector<int> parent(static_cast<size_t>(k));std::vector<char> done(static_cast<size_t>(k),0);
        for(int i=0;i<k;++i) {parent[size_t(i)]=i;q[size_t(i)].push_back(reps[size_t(i)]);
            seenGen[size_t(reps[size_t(i)])]=markGen;seenBy[size_t(reps[size_t(i)])]=i;}
        auto find=[&](int i) {while(parent[size_t(i)]!=i)i=parent[size_t(i)]=parent[size_t(parent[size_t(i)])];return i;};
        uint64_t work=0;
        for(;;) {
            int open=0;
            for(int i=0;i<k;++i)if(find(i)==i&&!done[size_t(i)])++open;
            if(open<=1)break;
            for(int i=0;i<k;++i) {
                if(done[size_t(find(i))]||head[size_t(i)]>=q[size_t(i)].size())continue;
                const int c=q[size_t(i)][head[size_t(i)]++],x=c%W,z=c/W;
                ++work;
                for(const auto& d:kDirections) {
                    if(!step(p,x,z,d[0],d[1]))continue;
                    const size_t nc=size_t((z+d[1])*W+x+d[0]);
                    if(seenGen[nc]!=markGen) {seenGen[nc]=markGen;seenBy[nc]=i;q[size_t(i)].push_back(int(nc));continue;}
                    const int a=find(i),b=find(seenBy[nc]);
                    if(a!=b)parent[size_t(std::max(a,b))]=std::min(a,b);
                }
            }
            if(work>n)return ~0ull;
            for(int r=0;r<k;++r) {
                if(find(r)!=r||done[size_t(r)])continue;
                bool dry=true;
                for(int i=0;i<k&&dry;++i)if(find(i)==r&&head[size_t(i)]<q[size_t(i)].size())dry=false;
                if(!dry)continue;
                done[size_t(r)]=1;
                const int label=int(p.compSize.size());
                p.compSize.push_back(0);p.compBox.emplace_back();emptyBox(p.compBox.back());++p.liveComps;
                auto& b=p.compBox.back();
                for(int i=0;i<k;++i) {
                    if(find(i)!=r)continue;
                    for(int c:q[size_t(i)]) {
                        const int old=p.comp[size_t(c)];
                        if(old>=0) {--p.compSize[size_t(old)];touched.insert(old);}
                        p.comp[size_t(c)]=label;++p.compSize[size_t(label)];
                        b={std::min(b[0],c%W),std::min(b[1],c/W),std::max(b[2],c%W),std::max(b[3],c/W)};
                    }
                    work+=q[size_t(i)].size();
                }
            }
        }
        return work;
    }
    // Shrink a component's box to its cells after removals (exact).
    uint64_t shrinkBox(Plane& p,int label) {
        const int W=width();
        auto& b=p.compBox[size_t(label)];
        if(p.compSize[size_t(label)]<=0) {emptyBox(b);return 0;}
        uint64_t work=0;
        auto row=[&](int z) {for(int x=b[0];x<=b[2];++x) {++work;if(p.comp[size_t(z)*W+x]==label)return true;}return false;};
        auto col=[&](int x) {for(int z=b[1];z<=b[3];++z) {++work;if(p.comp[size_t(z)*W+x]==label)return true;}return false;};
        while(!row(b[1]))++b[1];
        while(!row(b[3]))--b[3];
        while(!col(b[0]))++b[0];
        while(!col(b[2]))--b[2];
        return work;
    }
    uint64_t relabel(Plane& p,const std::vector<int>& added,const std::vector<int>& removed,int bx0,int bz0,int bx1,int bz1) {
        const int W=width(),H=height();
        uint64_t work=0;
        std::set<int> touched;   // labels that lost cells (box may shrink)
        for(int c:removed) {
            const int l=p.comp[size_t(c)];
            p.comp[size_t(c)]=-1;
            if(l>=0) {--p.compSize[size_t(l)];touched.insert(l);}
        }
        // Local window: the change's bounding box plus a margin. Every edge
        // the change added or removed joins two legal cells within one cell
        // of a changed origin (N), so local connectivity decides merges
        // exactly and proves "no split" when each old label's N-cells meet.
        const int wx0=std::max(0,bx0-kRelabelMargin),wz0=std::max(0,bz0-kRelabelMargin);
        const int wx1=std::min(W-1,bx1+kRelabelMargin),wz1=std::min(H-1,bz1+kRelabelMargin);
        const int ww=wx1-wx0+1,wh=wz1-wz0+1;
        std::vector<int> group(size_t(ww)*wh,-1);
        auto local=[&](int c) {return size_t(c/W-wz0)*ww+size_t(c%W-wx0);};
        std::vector<int> near;
        auto addNear=[&](int c) {
            const int x=c%W,z=c/W;
            for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx)
                if(legal(p,x+dx,z+dz))near.push_back((z+dz)*W+x+dx);
        };
        for(int c:added)addNear(c);
        for(int c:removed)addNear(c);
        std::sort(near.begin(),near.end());near.erase(std::unique(near.begin(),near.end()),near.end());
        // Flood local groups from N in cell order; each group's old labels.
        std::vector<std::vector<int>> labels;   // per group, sorted unique
        std::vector<int> queue;
        for(int start:near) {
            if(group[local(start)]>=0)continue;
            const int id=int(labels.size());
            labels.emplace_back();
            group[local(start)]=id;queue.assign(1,start);
            for(size_t head=0;head<queue.size();++head) {
                const int c=queue[head],x=c%W,z=c/W;
                if(p.comp[size_t(c)]>=0)labels.back().push_back(p.comp[size_t(c)]);
                for(const auto& d:kDirections) {
                    const int nx=x+d[0],nz=z+d[1];
                    if(nx<wx0||nz<wz0||nx>wx1||nz>wz1||!step(p,x,z,d[0],d[1]))continue;
                    auto& g=group[local(nz*W+nx)];
                    if(g<0) {g=id;queue.push_back(nz*W+nx);}
                }
            }
            work+=queue.size()*8;
            auto& l=labels.back();
            std::sort(l.begin(),l.end());l.erase(std::unique(l.begin(),l.end()),l.end());
        }
        // No split if every old label's N-cells lie in one local group. A
        // label whose N-cells lie in several may have split (a wall closing
        // a passage) or join beyond the window: settle it with one search
        // per local group (splitSearch), bounded by the cut-off side or the
        // way around, not the map.
        std::map<int,std::vector<int>> reps;   // label -> one N-cell per local group
        {
            std::map<int,std::set<int>> seen;
            for(int c:near) {
                const int l=p.comp[size_t(c)];
                if(l>=0&&seen[l].insert(group[local(c)]).second)reps[l].push_back(c);
            }
        }
        bool split=false;
        for(const auto& [l,cells]:reps) {
            if(cells.size()<2)continue;
            const uint64_t spent=splitSearch(p,cells,touched);
            if(spent==~0ull) {labelPlane(p);++stats.planeRelabels;return work+uint64_t(W)*H*2;}
            work+=spent;split=true;
        }
        if(split) {
            for(auto& l:labels)l.clear();
            for(int z=wz0;z<=wz1;++z)for(int x=wx0;x<=wx1;++x) {
                const int g=group[local(z*W+x)],c=p.comp[size_t(z)*W+x];
                if(g>=0&&c>=0)labels[size_t(g)].push_back(c);
            }
            for(auto& l:labels) {std::sort(l.begin(),l.end());l.erase(std::unique(l.begin(),l.end()),l.end());}
        }
        // Groups sharing a label are one component: union the labels of
        // every group, merge each class into its largest label (ties: lowest),
        // and a group with no label is a new component of added cells only.
        std::map<int,int> parent;
        auto find=[&](int l) {
            int r=l;
            while(parent.at(r)!=r)r=parent.at(r);
            while(parent.at(l)!=r) {const int n=parent.at(l);parent[l]=r;l=n;}
            return r;
        };
        for(const auto& l:labels)for(int c:l)parent.try_emplace(c,c);
        for(const auto& l:labels)for(size_t i=1;i<l.size();++i) {
            const int a=find(l[0]),b=find(l[i]);
            if(a!=b)parent[std::max(a,b)]=std::min(a,b);
        }
        std::map<int,int> target;   // class root -> surviving label
        for(const auto& [c,unused]:parent) {
            const int r=find(c);
            const auto [it,fresh]=target.try_emplace(r,c);
            if(!fresh&&p.compSize[size_t(c)]>p.compSize[size_t(it->second)])it->second=c;
        }
        for(const auto& [c,unused]:parent) {
            const int t=target.at(find(c));
            if(c==t)continue;
            auto& tb=p.compBox[size_t(t)];
            const auto b=p.compBox[size_t(c)];
            for(int z=b[1];z<=b[3];++z)for(int x=b[0];x<=b[2];++x)
                if(p.comp[size_t(z)*W+x]==c)p.comp[size_t(z)*W+x]=t;
            if(b[2]>=0)work+=uint64_t(b[2]-b[0]+1)*uint64_t(b[3]-b[1]+1);
            p.compSize[size_t(t)]+=p.compSize[size_t(c)];p.compSize[size_t(c)]=0;
            if(b[2]>=0)tb={std::min(tb[0],b[0]),std::min(tb[1],b[1]),std::max(tb[2],b[2]),std::max(tb[3],b[3])};
            if(b[2]>=0)--p.liveComps;
            emptyBox(p.compBox[size_t(c)]);
            if(touched.erase(c))touched.insert(t);
        }
        for(auto& l:labels) {
            if(!l.empty()) {l.assign(1,target.at(find(l[0])));continue;}
            l.assign(1,int(p.compSize.size()));
            p.compSize.push_back(0);p.compBox.emplace_back();emptyBox(p.compBox.back());
            ++p.liveComps;
        }
        for(int c:added) {
            if(p.comp[size_t(c)]>=0)continue;   // already in a component splitSearch labelled
            const int id=group[local(c)];
            const int target=labels[size_t(id)][0];
            p.comp[size_t(c)]=target;++p.compSize[size_t(target)];
            auto& b=p.compBox[size_t(target)];
            const int x=c%W,z=c/W;
            b={std::min(b[0],x),std::min(b[1],z),std::max(b[2],x),std::max(b[3],z)};
        }
        for(int l:touched) {
            if(p.compSize[size_t(l)]<=0&&p.compBox[size_t(l)][2]>=0)--p.liveComps;
            work+=shrinkBox(p,l);
        }
        // Dead labels are never reused; renumber once they dominate.
        if(p.compSize.size()>4*size_t(std::max(p.liveComps,1))+1024) {
            labelPlane(p);++stats.planeRelabels;work+=uint64_t(W)*H*2;
        }
        (void)H;
        return work;
    }
    int compAt(const Plane& p,int cell) const {
        return cell>=0&&size_t(cell)<p.comp.size()?p.comp[size_t(cell)]:-1;
    }
    // A group's static component on the CURRENT labelling: its seed cell's,
    // or (that cell now covered) the first seed still legal.
    int groupComp(const Group& g,const Plane& p) const {
        if(const int c=compAt(p,g.compCell);c>=0)return c;
        for(int s:g.seeds)if(const int c=compAt(p,s);c>=0)return c;
        return -1;
    }
    const NavGrid* legacyGrid(const Plane& p) const {
        for(const auto& u:w.units_)if(u.type&&u.type->footX==p.footX&&u.type->footZ==p.footZ&&!u.type->canFly&&
            u.type->domain==UnitType::Domain::Ground)
            return &w.navFor(u.type);
        return nullptr;
    }
    uint64_t worldEpoch() const {
        uint64_t e=mix(w.placementEpoch_,structureSignature);
        if(!placementPlane())for(const auto& g:w.navClasses_)e=mix(e,g.version());
        if(!placementPlane())e=mix(e,w.nav_.version());
        return e;
    }
    Plane& plane(int index) {
        auto& p=planes[size_t(index)];
        if(p.epoch!=epoch) {
            // Only REbuilds (static churn) are charged to the field quota. A
            // class's first build is one-time setup; charging it delayed the
            // first fields by a tick, which changed how the order's members
            // split into groups (a started field takes no new seeds) and cost
            // jagged 2000 about 30% of its arrivals (seeds 0/7/42).
            const bool first=p.epoch==~0ull;
            p.epoch=epoch;buildPlane(p);labelPlane(p);clearStage(p);
            // The carried debt is clamped to half a tick's quota: under
            // constant churn (corpses, burning features) every epoch rebuilds
            // every plane in use, and an unbounded debt starved all field
            // work, so new groups never got a field.
            if(!first)planeDebt=std::min(planeDebt+2*uint64_t(width())*height(),kFieldQuota/2);
        }
        p.lastUse=w.tickCounter_;
        return p;
    }
    bool legal(const Plane& p,int x,int z) const {
        return x>=0&&z>=0&&x<width()&&z<height()&&p.legal[size_t(z)*width()+x];
    }
    // Diagonal steps must clear both orthogonal neighbours (no corner cut).
    bool step(const Plane& p,int x,int z,int dx,int dz) const {
        if(!legal(p,x+dx,z+dz))return false;
        return !dx||!dz||(legal(p,x+dx,z)&&legal(p,x,z+dz));
    }

    // ---- groups and fields -------------------------------------------
    static bool plainMove(const Order& front) {
        return !front.targetId&&!front.load&&!front.unload&&!front.buildType&&!front.reclaimFeat&&
            !front.reclaimArea&&!front.manaBuildArea&&!front.repairTarget&&!front.wait&&!front.waitAttack&&
            !front.attackMove&&!front.patrol&&!front.guard&&!front.autoTarget&&!front.landing&&!front.park&&
            !front.buildRectangle&&!front.flightGoal&&!front.transportPickup&&!front.transportUnloadApproach&&
            !front.transportUnloadReleasePending&&!front.transportUnloadTransferDeferred&&!front.transportPassenger&&
            !front.productionExit;
    }
    // A leg Legion routes, apart from the combat flags its mission kind
    // allows (fight/patrol) and the target it chases (attack/guard).
    static bool routedLeg(const Order& o,bool combat,bool chase) {
        if(!chase&&(o.targetId||o.guard||o.autoTarget))return false;
        if(!combat&&(o.attackMove||o.patrol))return false;
        return !o.load&&!o.unload&&!o.buildType&&!o.reclaimFeat&&
            !o.reclaimArea&&!o.manaBuildArea&&!o.repairTarget&&!o.wait&&!o.waitAttack&&
            !o.landing&&!o.park&&!o.buildRectangle&&!o.flightGoal&&!o.transportPickup&&
            !o.transportUnloadApproach&&!o.transportUnloadReleasePending&&
            !o.transportUnloadTransferDeferred&&!o.transportPassenger&&!o.productionExit;
    }
    // A work/logistics leg: no flags but the ones its kind owns.
    enum Work : uint32_t {WBuild=1,WRepair=2,WReclaim=4,WLoad=8,WPickup=16,WUnloadApproach=32,WPark=64,WExit=128};
    static bool workLeg(const Order& o,uint32_t own) {
        if(bool(o.buildType)!=bool(own&WBuild)||bool(o.buildRectangle)!=bool(own&WBuild))return false;
        if(bool(o.repairTarget)!=bool(own&WRepair)||bool(o.reclaimFeat)!=bool(own&WReclaim))return false;
        if(o.load!=bool(own&(WLoad|WPickup))||o.transportPickup!=bool(own&WPickup))return false;
        if(o.transportUnloadApproach!=bool(own&WUnloadApproach)||bool(o.park)!=bool(own&WPark))return false;
        if(bool(o.productionExit)!=bool(own&WExit))return false;
        if(o.targetId&&!(own&(WLoad|WPickup)))return false;
        return !o.unload&&!o.attackMove&&!o.patrol&&!o.guard&&!o.autoTarget&&!o.reclaimArea&&!o.manaBuildArea&&
            !o.wait&&!o.waitAttack&&!o.landing&&!o.flightGoal&&!o.transportUnloadReleasePending&&
            !o.transportUnloadTransferDeferred&&!o.transportPassenger;
    }
    // The work/logistics kind of a leg (None if it is not one).
    static Kind workKind(const Order& leg) {
        if(leg.groundMission) {
            if(workLeg(leg,WPark))return leg.park->ring?Kind::Park:Kind::None;
            if(workLeg(leg,WUnloadApproach))return Kind::Unload;
            if(workLeg(leg,WExit))return Kind::Exit;
            return Kind::None;
        }
        if(workLeg(leg,WBuild))return Kind::Build;
        if(workLeg(leg,WRepair))return Kind::Repair;
        if(workLeg(leg,WReclaim))return Kind::Reclaim;
        if(leg.targetId&&(workLeg(leg,WLoad)||workLeg(leg,WPickup)))return Kind::Load;
        return Kind::None;
    }
    // The mission's own arrival geometry for an exact kind.
    static bool accepts(const Unit& u,const Order& leg,Kind kind) {
        if(kind==Kind::Build)
            return leg.buildRectangle->accepts(footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ));
        return World::groundMissionAccepts(u,leg);
    }
    // The mobility classes Legion plans for: surface movers with a 1..8
    // footprint. Ground units always; boats (Domain::Water) and hovercraft
    // (Domain::Hover) when the map has a placement plane, where the plane is
    // built from the mover's own predicate with the type's water-depth and
    // slope limits (planeFor), so a domain is just another mobility class.
    // Legacy terrain-only worlds key their nav-grid plane by footprint
    // alone, which cannot tell a boat from a ground unit: those stay Retail.
    // Non-square footprints also need the placement plane. Flyers are native.
    bool routedType(const UnitType& t) const {
        if(t.canFly||t.isStructure()||t.footX<1||t.footZ<1||t.footX>8||t.footZ>8)return false;
        if(t.domain!=UnitType::Domain::Ground&&!placementPlane())return false;
        return placementPlane()||t.footX==t.footZ;
    }
    // The mission goal of the unit's current leg, or None. Move keeps its
    // original rule exactly (plain Move legs and plain corners ahead of it).
    // The other kinds accept route corners (non-goal legs) ahead of the goal,
    // which a native route delivered before the leg became Legion's.
    Kind kindOf(const Unit& u) const {
        if(!u.alive()||u.embarked()||u.underConstruction||!u.type||u.orders.empty())return Kind::None;
        if(!routedType(*u.type))return Kind::None;
        if(width()<2||height()<2)return Kind::None;
        const size_t current=World::currentLeg(u.orders);
        const auto& leg=u.orders[current];
        if(!leg.goal)return Kind::None;
        auto corners=[&](bool combat,bool chase) {
            for(size_t i=0;i<current;++i)if(u.orders[i].goal||!routedLeg(u.orders[i],combat,chase))return false;
            return true;
        };
        if(leg.groundMission) {
            bool plain=true;
            for(size_t i=0;i<=current&&plain;++i)plain=plainMove(u.orders[i]);
            if(plain)return Kind::Move;
            if((leg.attackMove||leg.patrol)&&routedLeg(leg,true,false)&&corners(true,false))
                return leg.patrol?Kind::Patrol:Kind::Fight;
            if(const Kind work=workKind(leg);work!=Kind::None&&corners(false,false))return work;
            return Kind::None;
        }
        if(const Kind work=workKind(leg);work!=Kind::None&&corners(false,false))return work;
        // Attack and guard: tickCombat owns the front order's target and
        // writes its position into the leg every tick.
        const auto& front=u.orders.front();
        if(front.targetId&&leg.targetId==front.targetId&&front.guard==leg.guard&&
           routedLeg(leg,false,true)&&corners(false,true)&&u.type->canMove)
            return leg.guard?Kind::Guard:Kind::Attack;
        return Kind::None;
    }
    bool supports(const Unit& u) const {return kindOf(u)!=Kind::None;}
    std::pair<Fixed,Fixed> target(const Order& leg) const {
        return leg.missionTarget.value_or(std::pair{leg.x,leg.z});
    }
    // A leg's identity for a member: its controller, or for a moving goal
    // (no controller) the chased unit.
    static uint64_t legKey(const Order& leg,Kind kind) {
        if(policy(kind).moving)return 0xC000000000000000ull|uint64_t(uint8_t(kind))<<48|uint32_t(leg.targetId);
        // A repair or reclaim leg: its target and order (no controller).
        if(policy(kind).keyed)return 0xA000000000000000ull|uint64_t(uint8_t(kind))<<48|
            uint64_t(uint16_t(leg.issuedTick))<<32|uint32_t(kind==Kind::Repair?leg.repairTarget:leg.reclaimFeat);
        return leg.controller;
    }
    // Nearest statically legal origin to a requested one. Past the near
    // search radius, like Retail, settle for the nearest origin the unit can
    // actually reach (its own static component); -1 only if there is none.
    int nearestLegal(const Plane& p,int x,int z,int reach=-1) const {
        if(legal(p,x,z))return z*width()+x;
        int best=-1;int64_t bestD=0;
        const int far=reach>=0?std::max(width(),height()):kGoalSearchCells;
        for(int r=1;r<=far&&best<0;++r)
            for(int dz=-r;dz<=r;++dz)for(int dx=-r;dx<=r;dx+=(dz==-r||dz==r||dx==r)?1:2*r) {
                if(!legal(p,x+dx,z+dz))continue;
                if(r>kGoalSearchCells&&compAt(p,(z+dz)*width()+x+dx)!=reach)continue;
                const int64_t d=int64_t(dx)*dx+int64_t(dz)*dz;
                const int index=(z+dz)*width()+x+dx;
                if(best<0||d<bestD||(d==bestD&&index<best)) {best=index;bestD=d;}
            }
        return best;
    }
    // Nearest origin of component `comp` to (x,z): minimum squared distance,
    // ties to the lower cell index. Square rings outward, clipped to the
    // component's bounding box and starting at its Chebyshev distance; stops
    // once no farther ring can beat the best found (exact, bounded work).
    int nearestReachable(const Plane& p,int x,int z,int comp) const {
        if(comp<0||size_t(comp)>=p.compBox.size())return -1;
        const auto& b=p.compBox[size_t(comp)];
        const int W=width();
        const int r0=std::max({0,b[0]-x,x-b[2],b[1]-z,z-b[3]});
        const int far=std::max({x-b[0],b[2]-x,z-b[1],b[3]-z});
        int best=-1;int64_t bestD=0;
        for(int r=r0;r<=far;++r) {
            if(best>=0&&int64_t(r)*r>bestD)break;
            auto visit=[&](int cx,int cz) {
                const int index=cz*W+cx;
                if(p.comp[size_t(index)]!=comp)return;
                const int64_t d=int64_t(cx-x)*(cx-x)+int64_t(cz-z)*(cz-z);
                if(best<0||d<bestD||(d==bestD&&index<best)) {best=index;bestD=d;}
            };
            const int z0=std::max(z-r,b[1]),z1=std::min(z+r,b[3]);
            for(int cz=z0;cz<=z1;++cz) {
                if(std::abs(cz-z)==r) {
                    for(int cx=std::max(x-r,b[0]);cx<=std::min(x+r,b[2]);++cx)visit(cx,cz);
                } else {
                    if(x-r>=b[0]&&x-r<=b[2])visit(x-r,cz);
                    if(r&&x+r>=b[0]&&x+r<=b[2])visit(x+r,cz);
                }
            }
        }
        return best;
    }
    void leave(int id) {
        auto found=members.find(id);
        if(found==members.end())return;
        if(const auto* u=w.unit(id);u&&u->type&&found->second.slot>=0&&found->second.state!=Arrived)
            slotCells(found->second,u->type->footX,u->type->footZ,false);
        if(auto point=points.find(found->second.point);point!=points.end()) {
            auto& ids=point->second.ids;
            if(const auto at=std::lower_bound(ids.begin(),ids.end(),id);at!=ids.end()&&*at==id) {
                ids.erase(at);
                auto& parts=point->second.partRefs;
                if(auto part=parts.find(found->second.part);part!=parts.end()&&--part->second<=0)parts.erase(part);
            }
            if(--point->second.refs<=0)points.erase(point);
        }
        auto group=groups.find(found->second.group);
        if(group!=groups.end()) {
            const auto& m=found->second;
            // A member that completes keeps its slot (it stands there); one
            // cancelled or re-ordered frees it for the rest of the group.
            if(m.slot>=0&&m.state!=Arrived) {
                auto slots=group->second.slots.find(m.requested);
                if(slots!=group->second.slots.end()&&size_t(m.slot)<slots->second.taken.size())
                    slots->second.taken[size_t(m.slot)]=0;
            }
            if(auto shared=group->second.sharing.find(m.requested);shared!=group->second.sharing.end()&&
               --shared->second<=0)group->second.sharing.erase(shared);
            ringRelease(group->second,id,m);
            if(auto& ids=group->second.reachIds;!ids.empty())
                if(const auto at=std::lower_bound(ids.begin(),ids.end(),id);at!=ids.end()&&*at==id)ids.erase(at);
            if(--group->second.members<=0) {unlistGroup(group->second);groups.erase(group);}
        }
        if(size_t(id)<memberIndex.size())memberIndex[size_t(id)]=nullptr;
        members.erase(found);
    }
    void registerMove(Unit& u) {
        // A re-registration of the same order keeps its approach clock: the
        // 5-minute retire counts from the first time this goal was found
        // unreachable, not from the latest static change.
        int priorReal=-1;uint64_t priorController=0;uint32_t priorSince=0;
        std::tuple<int,uint32_t,int32_t,int32_t> pinned{};bool pin=false;
        uint64_t priorKey=0;Kind priorKind=Kind::None;bool hadPrior=false;
        if(const auto prior=members.find(u.id);prior!=members.end()) {
            priorKey=prior->second.controller;priorKind=prior->second.kind;hadPrior=true;
            if(prior->second.approach) {
                priorReal=prior->second.real;priorController=prior->second.controller;priorSince=prior->second.approachSince;
            }
            // Hold the member's point across its own re-registration: the
            // last member leaving would erase it (its formation and the
            // cells arrived bodies claimed), and the re-registered member
            // would aim at the occupied centre with no area to settle in.
            if(const auto pt=points.find(prior->second.point);pt!=points.end()) {pinned=prior->second.point;pin=true;++pt->second.refs;}
        }
        auto unpin=[&] {
            if(!pin)return;
            pin=false;
            if(auto pt=points.find(pinned);pt!=points.end()&&--pt->second.refs<=0)points.erase(pt);
        };
        leave(u.id);
        dropAnchor(u.id);yielding.erase(u.id);approachDone.erase(u.id);parts.erase(u.id);
        if(size_t(u.id)<watchMark.size())watchMark[size_t(u.id)]=0;
        // A body setting off is no soft obstacle any more (its own group's
        // first field is built right now, before the next scan).
        // Its sample stays: still and settled again, it stamps at its next one.
        unstamp(u.id);
        const Kind kind=kindOf(u);
        if(kind==Kind::None) {unpin();return;}
        const Policy rule=policy(kind);
        ++stats.registrations;
        const auto& leg=u.orders[World::currentLeg(u.orders)];
        if(!hadPrior||priorKey!=legKey(leg,kind)||priorKind!=kind)++stats.missionLegs[size_t(kind)];
        const int planeIndex=planeFor(*u.type);
        auto& p=plane(planeIndex);
        const auto [tx,tz]=target(leg);
        int gx=footprintOrigin(tx,u.type->footX),gz=footprintOrigin(tz,u.type->footZ);
        const int sx=footprintOrigin(u.x,u.type->footX),sz=footprintOrigin(u.z,u.type->footZ);
        const int reach=legal(p,sx,sz)?compAt(p,sz*width()+sx):-1;
        if(kind==Kind::Build) {
            // The build rectangle's own approach cell (as requestPath asks
            // the controller); if the static plane rejects it, the nearest
            // perimeter origin this body can stand on and reach. Arrival is
            // the rectangle itself (accepts), never a point beside it.
            const auto& rect=*leg.buildRectangle;
            const auto [cx,cz]=rect.navigationCell(sx,sz);
            gx=cx;gz=cz;
            if(!legal(p,cx,cz)) {
                int best=-1;int64_t bestD=0;
                rect.enumerate([&](int16_t x,int16_t z) {
                    if(!legal(p,x,z)||(reach>=0&&compAt(p,z*width()+x)!=reach))return;
                    const int64_t d=int64_t(x-cx)*(x-cx)+int64_t(z-cz)*(z-cz);
                    const int index=z*width()+x;
                    if(best<0||d<bestD||(d==bestD&&index<best)) {best=index;bestD=d;}
                });
                if(best>=0) {gx=best%width();gz=best/width();}
            }
        }
        Member m;m.controller=legKey(leg,kind);m.state=Waiting;m.windowTick=w.tickCounter_;m.stallTick=w.tickCounter_;
        m.kind=kind;m.seedX=gx;m.seedZ=gz;m.seededAt=w.tickCounter_;
        // The command a member belongs to: the order's convoy (one click,
        // however many ticks the uplink split it over; its issue tick when
        // unstamped); every patrol lap on its own; moving goals on the
        // shared re-seed grid.
        const uint32_t command=leg.convoyTick!=ConvoyTable::kNone?leg.convoyTick:leg.issuedTick;
        const uint32_t issue=rule.moving?w.tickCounter_-w.tickCounter_%kReseedTicks:
            rule.perController?uint32_t(leg.controller):command;
        if(resolvedEpoch!=epoch||resolved.size()>=4096) {resolved.clear();resolvedEpoch=epoch;}
        auto [cached,fresh]=resolved.try_emplace({planeIndex,gx,gz,reach},-1,-1);
        if(fresh) {
            cached->second.first=nearestLegal(p,gx,gz,reach);
            const int goal=cached->second.first;
            if(reach>=0&&goal>=0&&compAt(p,goal)!=reach&&p.compSize[size_t(reach)]>=kApproachRegion)
                cached->second.second=nearestReachable(p,gx,gz,reach);
        }
        m.goal=cached->second.first;
        // A goal outside this body's static region, while the region still
        // leaves room to move: walk to the region's nearest point to it and
        // hold there, order kept, as Retail does (a small sealed pocket holds
        // where it stands). A static change re-resolves the real goal.
        if(reach>=0&&m.goal>=0&&compAt(p,m.goal)!=reach&&p.compSize[size_t(reach)]>=kApproachRegion) {
            const int near=cached->second.second;
            if(near>=0) {
                m.real=m.goal;m.goal=near;m.approach=true;m.approachEpoch=epoch;
                m.approachSince=priorReal==m.real&&priorController==m.controller?priorSince:w.tickCounter_;
                m.gainBest=0xffffffffu;m.gainTick=w.tickCounter_;
            }
        }
        m.requested=m.goal;
        // A kind without a shared destination area gets a point of its own
        // (keyed by the unit), so no formation or area logic joins it.
        m.point={rule.area?u.player:-1-u.id,rule.area?command:issue,tx.v,tz.v};m.part=leg.issuedTick;
        {auto& pt=points[m.point];++pt.refs;m.pt=&pt;}
        unpin();
        if(m.goal<0) {putMember(u.id,m);return;}   // trapped on first move
        const int x=m.goal%width(),z=m.goal/width();
        const int goalComp=compAt(p,m.goal);
        // A reach kind joins its target's group (see Group::reachTarget).
        const bool ringKind=kind==Kind::Attack||kind==Kind::Guard;
        const int ringTarget=ringKind?leg.targetId:0;
        const int ringBucket=ringKind?reachPx(u,w.unit(ringTarget),kind==Kind::Guard).first/kRingBucket:-1;
        Group* joined=nullptr;
        uint64_t joins=0;
        for(auto& [id,g]:groups) {
            if(rule.solo)break;
            ++joins;
            if(g.player!=u.player||g.plane!=planeIndex||g.kind!=kind)continue;
            if(ringKind?g.reachTarget!=ringTarget||g.reachBucket!=ringBucket:g.issuedTick!=issue)continue;
            // Seeds in different static components never share a field: a
            // member whose goal it cannot reach would descend to a
            // teammate's seed and hold there forever instead of retiring.
            if(groupComp(g,p)!=goalComp)continue;
            // Approach points never share a field with real goals: a member
            // would descend to a stand-in point beside an obstacle instead
            // of its own goal behind it.
            if(g.approach!=m.approach)continue;
            if(x<g.minX-kClusterCells||x>g.maxX+kClusterCells||z<g.minZ-kClusterCells||z>g.maxZ+kClusterCells)continue;
            // (A reach group's seeds are its ring once built: it serves the
            // target's origin it was seeded at.)
            const bool seeded=ringKind?g.reachCentre==m.goal:std::binary_search(g.seeds.begin(),g.seeds.end(),m.goal);
            // A field in progress or complete is never re-seeded.
            if(g.field&&!seeded)continue;
            // A field is "distance to the nearest seed of anyone": over a
            // whole army's goal lattice it pulls every body to the lattice's
            // near edge, where they jam and then thread between settled goals.
            // Groups of at most kGroupSeeds goals keep each field aimed at its
            // own block of the destination.
            if(!seeded&&g.seeds.size()>=kGroupSeeds)continue;
            joined=&g;break;
        }
        stats.joinIterations+=joins;stats.groupLoopIters+=joins;
        if(!joined) {
            Group g;g.id=nextGroup++;g.player=u.player;g.plane=planeIndex;g.issuedTick=issue;g.compCell=m.goal;g.approach=m.approach;g.kind=kind;
            g.command=commandKey(u.player,std::get<1>(m.point));g.soft=!u.type->wanders;
            g.minX=g.maxX=x;g.minZ=g.maxZ=z;
            if(ringKind) {g.reachTarget=ringTarget;g.reachBucket=ringBucket;g.reachCentre=m.goal;}
            joined=&groups.emplace(g.id,std::move(g)).first->second;
            listGroup(*joined);
            ++stats.groups;
        }
        auto& g=*joined;
        if(g.ring.band0==0&&!std::binary_search(g.seeds.begin(),g.seeds.end(),m.goal))
            g.seeds.insert(std::upper_bound(g.seeds.begin(),g.seeds.end(),m.goal),m.goal);
        if(ringKind)g.reachIds.insert(std::upper_bound(g.reachIds.begin(),g.reachIds.end(),u.id),u.id);
        g.minX=std::min(g.minX,x);g.maxX=std::max(g.maxX,x);g.minZ=std::min(g.minZ,z);g.maxZ=std::max(g.maxZ,z);
        if(g.bodyMaxX<g.bodyMinX) {g.bodyMinX=g.bodyMaxX=sx;g.bodyMinZ=g.bodyMaxZ=sz;}
        else {g.bodyMinX=std::min(g.bodyMinX,sx);g.bodyMaxX=std::max(g.bodyMaxX,sx);g.bodyMinZ=std::min(g.bodyMinZ,sz);g.bodyMaxZ=std::max(g.bodyMaxZ,sz);}
        {const int n=++g.sharing[m.goal];int& top=g.peak[m.goal];top=std::max(top,n);}++g.members;g.lastUse=w.tickCounter_;
        m.group=g.id;
        putMember(u.id,m);
    }
    size_t liveFields() const {return fieldTotals.fields;}
    size_t liveFieldCells() const {return fieldTotals.cells;}
    // The window a new field for `g` needs: the union of its seeds' static
    // component boxes (the whole map if a seed is unlabelled).
    std::array<int,4> fieldWindow(const Group& g,bool whole=false) const {
        const auto& p=planes[size_t(g.plane)];
        std::array<int,4> box{width(),height(),-1,-1};
        for(int s:g.seeds) {
            const int c=compAt(p,s);
            if(c<0||size_t(c)>=p.compBox.size())return {0,0,width()-1,height()-1};
            const auto& b=p.compBox[size_t(c)];
            box={std::min(box[0],b[0]),std::min(box[1],b[1]),std::max(box[2],b[2]),std::max(box[3],b[3])};
        }
        if(box[2]<box[0])return {0,0,width()-1,height()-1};
        if(whole||g.full||g.bodyMaxX<g.bodyMinX)return box;
        // Bound it to the seeds and bodies plus a margin of half their
        // span (at least kFieldMargin): the shortest way rarely leaves it,
        // and a group crossing a corner of a big map no longer pays for the
        // whole map. A member it fails to cover widens the next build.
        const int x0=std::min(g.minX,g.bodyMinX),z0=std::min(g.minZ,g.bodyMinZ);
        const int x1=std::max(g.maxX,g.bodyMaxX),z1=std::max(g.maxZ,g.bodyMaxZ);
        int margin=std::max(kFieldMargin,std::max(x1-x0,z1-z0)/2);
        // A formation strong enough to pinwheel (see pivotAim) takes its
        // outer arcs up to kPivotMaxCells off the shortest way.
        for(const auto& [seed,count]:g.sharing)if(count>=kPivotMembers) {margin+=kPivotMaxCells;break;}
        const std::array<int,4> bounded{std::max(box[0],x0-margin),std::max(box[1],z0-margin),
                                        std::min(box[2],x1+margin),std::min(box[3],z1+margin)};
        return bounded;
    }
    // Another group's field that serves this group as well as its own
    // build would: the same plane at the current static epoch, the same
    // seeds, a window holding this group's (a larger window only frees
    // paths a bounded one cuts off), the same soft rule (the same command
    // unless neither has settled arrivals of its own), and started within
    // the last soft scan period (soft bodies are sampled that coarsely
    // anyway, and a build already sees them change while it runs).
    // Finished, its potentials are the exact distances on that window;
    // still building, it is the build this one would be, under way.
    // Commands sent to one point at different ticks (AI squads, repeated
    // orders) otherwise each built the same whole-map field, and rebuilt it
    // after every static change in a battle: a full quota for seconds.
    // A finished field first, then one in progress, each in group order.
    std::shared_ptr<Field> sharedField(const Group& g,const std::array<int,4>& box,uint64_t command,bool own) const {
        std::shared_ptr<Field> hit;
        uint64_t iters=0;
        if(const auto found=routeIndex.find({g.plane,seedsKey(g.seeds)});found!=routeIndex.end()) {
            for(int pass=0;pass<2&&!hit;++pass)for(int id:found->second) {
                ++iters;
                if(sharesWith(g,groups.find(id)->second,pass,box,command,own,hit))break;
            }
        }
        stats.shareScanIters+=iters;
#ifndef NDEBUG
        if(gVerify) {
            std::shared_ptr<Field> scan;
            for(int pass=0;pass<2&&!scan;++pass)for(const auto& [id,o]:groups)if(sharesWith(g,o,pass,box,command,own,scan))break;
            if(scan!=hit)verifyFail("route-key index finds another shared field than the full scan");
        }
#endif
        return hit;
    }
    // sharedField's test of one group: its field, then its next.
    bool sharesWith(const Group& g,const Group& o,int pass,const std::array<int,4>& box,uint64_t command,bool own,std::shared_ptr<Field>& hit) const {
        if(&o==&g||o.plane!=g.plane)return false;
        for(const auto* d:{&o.field,&o.next}) {
            const Field* f=d->get();
            if(!f||f->done!=(pass==0)||f->epoch!=epoch||w.tickCounter_-f->started>kStillScan)continue;
            if(f->x0>box[0]||f->z0>box[1]||f->x0+f->fw-1<box[2]||f->z0+f->fh-1<box[3])continue;
            if((f->command==kNoSoft)!=(command==kNoSoft)||f->ownArrivals!=own||(own&&f->command!=command))continue;
            if(f->seeds!=g.seeds)continue;
            hit=*d;
            return true;
        }
        return false;
    }
    // At the cap a new field may only displace one that has served its
    // group for a while (oldest build first); otherwise the group waits for
    // a slot. Evicting the least recently used field every tick thrashed:
    // all live groups use theirs every tick.
    bool startField(Group& g,bool shareOnly=false) {
        // A reach group of two or more members plans to its ring (AR-06).
        if(!g.field&&!g.ring.tried&&g.reachIds.size()>=kRingMembers)buildRing(g);
        const auto box=fieldWindow(g);
        const size_t cells=size_t(box[2]-box[0]+1)*size_t(box[3]-box[1]+1);
        {
            const uint64_t command=g.soft?g.command:kNoSoft;
            const bool own=ownsArrivals(command);
            if(auto shared=sharedField(g,box,command,own)) {
                g.built=w.tickCounter_;++stats.fieldsShared;
                if(!shared->done)(g.field?g.next:g.field)=std::move(shared);
                else if(g.field) {
                    if(g.next&&!g.next->done)++stats.refreshDiscards;
                    ++stats.refreshCompleted;
                    g.field=std::move(shared);g.next.reset();g.stale=false;g.demand=false;restaleSlots(g);
                }
                else g.field=std::move(shared);
                listGroup(g);
                return true;
            }
        }
        if(shareOnly)return false;
        const size_t budget=kMaxFields*size_t(width())*size_t(height());
        while(liveFields()>=kMaxFieldCount||liveFieldCells()+cells>budget) {
            // A victim is a group whose finished field is past its tenure
            // and its own (dropping a shared one frees nothing).
            // Within one tick that set only grows when a field finishes
            // (fieldsDone): evictions and new builds only shrink it.
            if(noVictimTick==w.tickCounter_&&noVictimDone==fieldsDone)return false;
            // The oldest build first (ties: the lowest id), so the walk in
            // byBuilt order stops at the first victim or at tenure.
            Group* victim=nullptr;
            uint64_t iters=0;
            for(const auto& [built,id]:byBuilt) {
                ++iters;
                if(w.tickCounter_-built<kFieldTenure)break;
                Group& o=groups.find(id)->second;
                if(o.field->done&&o.field.use_count()==1) {victim=&o;break;}
            }
            stats.groupLoopIters+=iters;
#ifndef NDEBUG
            if(gVerify) {
                Group* scan=nullptr;
                for(auto& [id,o]:groups)
                    if(o.field&&o.field->done&&o.field.use_count()==1&&w.tickCounter_-o.built>=kFieldTenure&&(!scan||o.built<scan->built))scan=&o;
                if(scan!=victim)verifyFail("byBuilt eviction victim differs from the full scan");
            }
#endif
            if(!victim) {noVictimTick=w.tickCounter_;noVictimDone=fieldsDone;return false;}
            if(victim->next&&!victim->next->done)++stats.refreshDiscards;
            victim->field.reset();victim->next.reset();victim->stale=false;victim->demand=false;++stats.fieldEvictions;
            listGroup(*victim);
        }
        g.built=w.tickCounter_;
        auto f=std::make_shared<Field>();
        f->seeds=g.seeds;f->seedKey=seedsKey(f->seeds);f->started=w.tickCounter_;
        f->plane=g.plane;f->epoch=epoch;f->serial=++fieldSerial;f->command=g.soft?g.command:kNoSoft;f->softCounts=softCountsFor(planes[size_t(g.plane)].footX,planes[size_t(g.plane)].footZ);
        f->avoid=g.avoidSeg;
        f->ownArrivals=ownsArrivals(f->command);
        ++stats.fieldsStartedByKind[size_t(g.kind)&15];
        f->liftAllied=std::any_of(liftSoftPlayers.begin(),liftSoftPlayers.end(),
            [&](int owner) {return w.allied(owner,int(uint32_t(f->command>>32)));});
        f->W=width();f->x0=box[0];f->z0=box[1];f->fw=box[2]-box[0]+1;f->fh=box[3]-box[1]+1;
        f->potential.assign(cells,kUnreached);
        f->totals=&fieldTotals;++fieldTotals.fields;fieldTotals.cells+=cells;
        {const auto whole=fieldWindow(g,true);f->bounded=whole!=box;}
        if(g.bodyMaxX>=g.bodyMinX) {f->aimed=true;f->tx=(g.bodyMinX+g.bodyMaxX)/2;f->tz=(g.bodyMinZ+g.bodyMaxZ)/2;}
        for(int s:g.seeds) {
            if(!f->inside(s%f->W,s/f->W))continue;
            f->potential[f->local(s%f->W,s/f->W)]=0;f->seedKeys.push_back({f->heuristic(s%f->W,s/f->W),s});
        }
        std::sort(f->seedKeys.begin(),f->seedKeys.end());
        f->current=f->seedKeys.empty()?0:f->seedKeys.front().first;
        if(g.field)g.next=std::move(f);else g.field=std::move(f);
        listGroup(g);
        return true;
    }
    // Dial's algorithm on key = potential + heuristic (A*; consistent, so
    // a key never drops below its parent's and rises by at most
    // (kSoftFactor+1)*kDiagonal plus a corridor charge: 128 circular
    // buckets suffice). Seeds enter
    // in key order.
    uint64_t advance(Field& f,uint64_t budget) {
        const auto& p=planes[size_t(f.plane)];
        const int W=width();uint64_t spent=0;
        while((f.queued||f.seedNext<f.seedKeys.size())&&spent<budget) {
            if(!f.queued&&f.seedKeys[f.seedNext].first>f.current)f.current=f.seedKeys[f.seedNext].first;
            while(f.seedNext<f.seedKeys.size()&&f.seedKeys[f.seedNext].first==f.current) {
                f.buckets[f.current&127].push_back(f.seedKeys[f.seedNext].second);++f.queued;++f.seedNext;
            }
            auto& bucket=f.buckets[f.current&127];
            if(bucket.empty()) {++f.current;continue;}
            const int cell=bucket.back();bucket.pop_back();--f.queued;
            const int x=cell%W,z=cell/W;
            const uint32_t g=f.at(size_t(cell));
            if(g+f.heuristic(x,z)!=f.current)continue;   // stale entry
            for(const auto& d:kDirections) {
                ++spent;
                if(!step(p,x,z,d[0],d[1]))continue;
                const uint32_t base=d[0]&&d[1]?kDiagonal:kOrthogonal;
                // Outside the window only after a static change mid-build
                // (the field is then stale and rebuilt on the new plane).
                if(!f.inside(x+d[0],z+d[1]))continue;
                auto& slot=f.potential[f.local(x+d[0],z+d[1])];
                if(g+base>=slot)continue;   // no charge can improve it
                const int level=softLevel(x+d[0],z+d[1],p.footX,p.footZ,f.command,f.softCounts,f.ownArrivals,f.liftAllied);
                f.softened|=level>0;
                const uint32_t next=g+(level==2?base*kSoftFactor:level==1?base*kSoftNear:base)+
                    (f.avoid.empty()?0:f.corridorCharge(x+d[0],z+d[1],p.footX,p.footZ,base));
                if(next>=kUnreached)continue;   // saturated: beyond the field's range
                if(next<slot) {
                    slot=uint16_t(next);
                    f.buckets[(next+f.heuristic(x+d[0],z+d[1]))&127].push_back((z+d[1])*W+x+d[0]);++f.queued;
                }
            }
        }
        if(!f.queued&&f.seedNext>=f.seedKeys.size()) {
            f.done=true;++fieldsDone;for(auto& b:f.buckets)std::vector<int>().swap(b);std::vector<std::pair<uint32_t,int>>().swap(f.seedKeys);
        }
        f.work+=spent;
        return spent;
    }
    // Structure footprints as last stamped into the planes (id order).
    struct Stamp {
        int x=0,z=0,fx=0,fz=0;bool open=false;
        bool operator==(const Stamp&) const=default;
    };
    // Ascending ids; `nowStamps` and `structurePtrs` are syncStatic's
    // scratch, kept to reuse their storage.
    std::vector<std::pair<int,Stamp>> stamps,nowStamps;
    std::vector<const Unit*> structurePtrs;
    // Bring the static planes up to date with the world. On a placement map
    // only the changed rectangles are recomputed (refreshPlane), and a new
    // static epoch starts only when some built plane's legality actually
    // changed: a non-blocking corpse, a feature swapped for one of the same
    // blocking, or a structure moving nothing leave every plane, field and
    // slot valid. Legacy (nav-grid) worlds keep the whole-epoch rebuild.
    void syncStatic() {
        if(!placementPlane()) {
            // Structures are bodies the mover always refuses: fold their layout
            // into the static epoch so the plane follows construction/death.
            uint64_t sig=0x6c6567696f6e;
            syncUnitLists();
            for(const int id:structureIds) {
                const Unit* found=w.unit(id);
                if(!found)continue;
                const Unit& u=*found;
                if(!u.alive()||u.embarked()||!u.type||!u.type->isStructure())continue;
                sig=mix(sig,uint64_t(u.id));sig=mix(sig,uint64_t(uint32_t(u.x.v))<<32|uint32_t(u.z.v));
                sig=mix(sig,yardOpen(u.id));
            }
            structureSignature=sig;
            const uint64_t e=worldEpoch();
            if(e!=lastWorldEpoch) {lastWorldEpoch=e;staticChanged();}
            return;
        }
        std::vector<std::array<int,4>> rects;
        auto& now=nowStamps;
        auto& structures=structurePtrs;
        now.clear();structures.clear();
        struct ListScope {const std::vector<const Unit*>*& list;~ListScope(){list=nullptr;}} listScope{structureList};
        syncUnitLists();
        for(const int id:structureIds) {
            const Unit* found=w.unit(id);
            if(!found)continue;
            const Unit& u=*found;
            if(!u.alive()||u.embarked()||!u.type||!u.type->isStructure())continue;
            structures.push_back(&u);
            now.push_back({u.id,{footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ),
                                 u.type->footX,u.type->footZ,yardOpen(u.id)}});
        }
        // Unit ids ascend along units_ except in restored retail fixtures.
        if(!std::is_sorted(now.begin(),now.end(),[](const auto& x,const auto& y){return x.first<y.first;}))
            std::sort(now.begin(),now.end(),[](const auto& x,const auto& y){return x.first<y.first;});
        auto a=stamps.begin();auto b=now.begin();
        auto rect=[&](const Stamp& t) {rects.push_back({t.x,t.z,t.fx,t.fz});};
        while(a!=stamps.end()||b!=now.end()) {
            if(b==now.end()||(a!=stamps.end()&&a->first<b->first)) {rect(a->second);++a;}
            else if(a==stamps.end()||b->first<a->first) {rect(b->second);++b;}
            else {if(!(a->second==b->second)) {rect(a->second);rect(b->second);}++a;++b;}
        }
        stamps.swap(now);
        const bool all=w.placementDirtyAll_;
        rects.insert(rects.end(),w.placementDirty_.begin(),w.placementDirty_.end());
        w.placementDirty_.clear();w.placementDirtyAll_=false;
        bool changed=false;
        if(lastWorldEpoch==~0ull) {lastWorldEpoch=0;changed=true;}   // the first epoch, as before
        structureList=&structures;
        if(all||!rects.empty()) {
            std::vector<Plane*> refreshed;
            touch.assign(planes.size(),{0,0,-1,-1});
            for(auto& p:planes) {
                // A background build keeps going on what it has read and
                // applies the changes incrementally once complete (exact:
                // every cell read before a change is in a pending rectangle).
                if(p.stage>0) {
                    if(all||p.pending.size()+rects.size()>4096) {clearStage(p);startBuild(p);continue;}
                    p.pending.insert(p.pending.end(),rects.begin(),rects.end());
                    continue;
                }
                // Unbuilt planes build fresh; stale ones rebuild on use.
                if(p.legacy||p.epoch==~0ull||p.epoch!=epoch)continue;
                if(all) {changed=true;touch[size_t(&p-planes.data())]={0,0,width()-1,height()-1};continue;}
                bool any=false;
                const uint64_t work=refreshPlane(p,rects,any,&touch[size_t(&p-planes.data())]);
                planeDebt=std::min(planeDebt+work,kFieldQuota/2);
                if(any) {changed=true;++stats.planeRefreshes;}
                refreshed.push_back(&p);
            }
            if(changed)for(auto* p:refreshed)p->epoch=epoch+1;
        }
        if(changed)staticChanged(touch.empty()?nullptr:&touch);
        touch.clear();
    }
    // Per plane, the bounding box of origins whose legality changed in this
    // sync ({0,0,-1,-1}: none).
    std::vector<std::array<int,4>> touch;
    // Does a static change box (on the group's plane) reach the group's
    // fields? A field covers its seeds' component box; a change farther
    // than one cell from every cell of that box cannot alter any step
    // inside it (a step reads its target and the two orthogonal cells), nor
    // merge another region into it.
    bool fieldTouched(const Group& g,const std::array<int,4>& b) const {
        if(b[2]<0)return false;
        for(const Field* f:{g.field.get(),g.next.get()}) {
            if(!f)continue;
            if(b[0]<=f->x0+f->fw&&b[2]>=f->x0-1&&b[1]<=f->z0+f->fh&&b[3]>=f->z0-1)return true;
        }
        return false;
    }
    void staticChanged(const std::vector<std::array<int,4>>* changes=nullptr) {
        {
            ++epoch;
            // Terrain changed. A finished field keeps steering its group
            // (a stale potential can only misdirect, never make a step legal:
            // the plane and commitGroundStep decide legality) until its
            // replacement on the new plane is done. A group's FIRST field,
            // half built, keeps building on the new plane (marked stale, so
            // a clean rebuild follows): restarting it on every change meant
            // a group ordered during constant churn (corpses every tick)
            // never got any field. Arrival slots were proven on the old
            // plane: they are rebuilt (see restaleSlots). A refresh under way
            // keeps building too, and another follows once it is installed
            // (see finish): refreshes run on a reduced allowance, and
            // restarted on every change in a battle they never finished.
            // Only the groups a change reaches (all, without per-plane boxes).
            stats.groupLoopIters+=groups.size();
            for(auto& [id,g]:groups) {
                if(changes&&(g.plane<0||size_t(g.plane)>=changes->size()||!fieldTouched(g,(*changes)[size_t(g.plane)])))continue;
                g.stale=g.field!=nullptr;
                listGroup(g);
                restaleSlots(g);
            }
        }
    }
    // ---- background first builds ---------------------------------------
    // A class's first plane build is a whole-map pass (15 ms on 768x768,
    // charged to nobody). Build planes for classes already on the map ahead
    // of their first order, a bounded number of cells per tick; the result
    // is identical to buildPlane+labelPlane (same order, same labels), so
    // this only moves work earlier: plane() still builds synchronously if a
    // plane is needed before its background build finished.
    static constexpr uint64_t kPrebuildCells=24576;   // cells of work per tick (~1 ms)
    static constexpr size_t kPrebuildPlanes=12;       // never crowd the plane budget
    void clearStage(Plane& p) {
        p.stage=0;p.cursor=0;p.nextLabel=0;p.head=0;
        std::vector<int>().swap(p.queue);std::vector<uint16_t>().swap(p.run);std::vector<int>().swap(p.column);
        std::vector<std::array<int,4>>().swap(p.pending);
    }
    void startBuild(Plane& p) {
        const size_t n=size_t(width())*height();
        p.cell.assign(n,0);p.legal.assign(n,0);p.comp.assign(n,-1);
        p.compSize.clear();p.compBox.clear();p.liveComps=0;
        p.run.assign(n,0);p.column.assign(size_t(width()),0);
        p.stage=1;p.cursor=0;
    }
    int findPlane(const UnitType& t) const {
        const int maxDepth=t.canFly||t.domain!=UnitType::Domain::Ground?10000:(t.maxWaterDepth>0?t.maxWaterDepth:20);
        const int minDepth=t.domain==UnitType::Domain::Water?(t.minWaterDepth>0?t.minWaterDepth:13):-10000;
        for(size_t i=0;i<planes.size();++i) {
            const auto& p=planes[i];
            if(!p.legacy&&p.footX==t.footX&&p.footZ==t.footZ&&p.maxDepth==maxDepth&&p.minDepth==minDepth&&
               p.maxSlope==t.maxSlope&&p.maxWaterSlope==t.maxWaterSlope)return int(i);
        }
        return -1;
    }
    void prebuildStep() {
        if(!placementPlane()||width()<2||height()<2)return;
        Plane* p=nullptr;
        for(auto& q:planes)if(q.stage>0) {p=&q;break;}
        if(!p) {
            if(planes.size()>=kPrebuildPlanes||w.tickCounter_%30!=0)return;
            for(const auto& u:w.units_) {
                if(!u.alive()||u.embarked()||!u.type)continue;
                const auto& t=*u.type;
                if(!routedType(t)||findPlane(t)>=0)continue;
                p=&planes[size_t(planeFor(t))];
                break;
            }
            if(!p)return;
            startBuild(*p);
        }
        advanceBuild(*p,kPrebuildCells);
    }
    void advanceBuild(Plane& p,uint64_t budget) {
        const int W=width(),H=height();
        while(budget>0&&p.stage>0) {
            if(p.stage==1) {   // cell layer, rows ascending
                const int rows=std::max(1,int(budget/uint64_t(W)));
                const int z1=std::min(H,p.cursor+rows);
                computeCells(p,0,p.cursor,W-1,z1-1);
                budget-=std::min(budget,uint64_t(z1-p.cursor)*W);
                p.cursor=z1;
                if(p.cursor>=H) {p.stage=2;p.cursor=0;}
            } else if(p.stage==2) {   // runs of legal cells to the right
                const int rows=std::max(1,int(budget/uint64_t(W)));
                const int z1=std::min(H,p.cursor+rows);
                for(int z=p.cursor;z<z1;++z) {
                    int r=0;
                    for(int x=W-1;x>=0;--x) {
                        r=p.cell[size_t(z)*W+x]?std::min(r+1,0xffff):0;
                        p.run[size_t(z)*W+x]=uint16_t(r);
                    }
                }
                budget-=std::min(budget,uint64_t(z1-p.cursor)*W);
                p.cursor=z1;
                if(p.cursor>=H) {p.stage=3;p.cursor=H-1;std::fill(p.column.begin(),p.column.end(),0);}
            } else if(p.stage==3) {   // footZ consecutive rows, rows descending
                const int rows=std::max(1,int(budget/uint64_t(W)));
                const int z1=std::max(-1,p.cursor-rows);
                for(int z=p.cursor;z>z1;--z)for(int x=0;x<W;++x) {
                    int& r=p.column[size_t(x)];
                    r=p.run[size_t(z)*W+x]>=p.footX?r+1:0;
                    p.legal[size_t(z)*W+x]=r>=p.footZ&&x+p.footX<W&&z+p.footZ<H;
                }
                budget-=std::min(budget,uint64_t(p.cursor-z1)*W);
                p.cursor=z1;
                if(p.cursor<0) {p.stage=4;p.cursor=0;p.head=0;p.queue.clear();std::vector<uint16_t>().swap(p.run);}
            } else {   // labelPlane's flood, resumable
                if(p.head>=p.queue.size()) {
                    if(!p.queue.empty()) {
                        p.compSize.push_back(int(p.queue.size()));p.compBox.push_back(p.box);
                        ++p.nextLabel;p.queue.clear();p.head=0;
                    }
                    while(budget>0&&p.cursor<W*H&&(!p.legal[size_t(p.cursor)]||p.comp[size_t(p.cursor)]>=0)) {++p.cursor;--budget;}
                    if(p.cursor>=W*H) {
                        p.liveComps=p.nextLabel;p.epoch=epoch;++stats.planeBuilds;
                        const auto pending=std::move(p.pending);
                        clearStage(p);
                        bool changed=false;
                        if(!pending.empty())refreshPlane(p,pending,changed);
                        break;
                    }
                    if(!budget)break;
                    p.comp[size_t(p.cursor)]=p.nextLabel;p.queue.assign(1,p.cursor);
                    p.box={p.cursor%W,p.cursor/W,p.cursor%W,p.cursor/W};
                }
                for(;p.head<p.queue.size()&&budget>0;++p.head) {
                    const int x=p.queue[p.head]%W,z=p.queue[p.head]/W;
                    for(const auto& d:kDirections) {
                        if(!step(p,x,z,d[0],d[1]))continue;
                        const int c=(z+d[1])*W+x+d[0];
                        if(p.comp[size_t(c)]<0) {
                            p.comp[size_t(c)]=p.nextLabel;p.queue.push_back(c);
                            p.box={std::min(p.box[0],x+d[0]),std::min(p.box[1],z+d[1]),std::max(p.box[2],x+d[0]),std::max(p.box[3],z+d[1])};
                        }
                    }
                    budget-=std::min<uint64_t>(budget,4);
                }
            }
        }
    }
    // Idle landed flyers make way (World::requestLegionLift): every
    // kLiftStride ticks (staggered by id) a ground member on its way looks
    // kLiftCells cells ahead along its planned steps -- its committed
    // detour route, else the descent of its group's field, else the
    // straight line to its goal -- and asks every allied idle landed flyer
    // whose footprint those steps cover to lift, and every flyer already
    // lifted from them to stay up. Bounded and in member (id) order.
    static constexpr uint32_t kLiftStride=4;
    static constexpr int kLiftCells=12;
    static constexpr int kLiftClear=6;   // cells round a lifted flyer: no member on its way there or bound there
    std::vector<int32_t> liftGoal;       // liftFlyers scratch: per goal origin, a member bound there (0 none)
    std::vector<int> liftGoalCells;
    void liftFlyers() {
        if(groundedIds.empty()&&liftHomeCells.empty())return;
        const int W=width();
        // A lifted flyer stays up while an allied member within kLiftClear
        // cells of it, or bound for a goal there, is moving or was held
        // only briefly (a long-held jam is not passing through): it lands
        // only into a settled area, not in front of the stragglers of a
        // group that is still coming in (it would have to lift again).
        if(!liftedIds.empty()&&w.tickCounter_%kLiftStride==0) {
            const size_t n=size_t(w.occW_)*w.occH_;
            if(liftGoal.size()!=n)liftGoal.assign(n,0);
            for(int c:liftGoalCells)liftGoal[size_t(c)]=0;
            liftGoalCells.clear();
            for(const auto& [id,m]:members) {
                if(m.state==Arrived||m.state==Trapped||m.goal<0||size_t(m.goal)>=n||liftGoal[size_t(m.goal)])continue;
                liftGoal[size_t(m.goal)]=id;liftGoalCells.push_back(m.goal);
            }
            for(int id:liftedIds) {
                Unit* flyer=w.unit(id);
                if(!flyer||!flyer->legionLift)continue;
                const int fx=flyer->type->footX,fz=flyer->type->footZ;
                const int ox=footprintOrigin(flyer->x,fx),oz=footprintOrigin(flyer->z,fz);
                bool busy=false;
                for(int cz=std::max(0,oz-kLiftClear);cz<std::min(w.occH_,oz+fz+kLiftClear)&&!busy;++cz)
                    for(int cx=std::max(0,ox-kLiftClear);cx<std::min(w.occW_,ox+fx+kLiftClear)&&!busy;++cx) {
                        const size_t c=size_t(cz)*w.occW_+cx;
                        for(const int32_t o:{w.occ_[c],liftGoal[c]}) {
                            if(!o)continue;
                            const Member* m=member(o);
                            const Unit* b=m?w.unit(o):nullptr;
                            // Its own squad settling round it is not traffic passing through.
                            if(b&&b->squad&&b->squad==flyer->squad)continue;
                            if(b&&(m->state==Moving||((m->state==Holding||m->state==Waiting)&&heldFor(*m)<kRestAfter))&&w.allied(flyer->player,b->player)) {busy=true;break;}
                        }
                    }
                if(busy&&w.legionLiftable(*flyer,flyer->player)) {w.requestLegionLift(*flyer);++stats.lifts;}
            }
        }
        // Bounding box of every cell a request can hit, and the exact gate
        // below it: each planned step moves at most one cell, so every origin
        // the walk visits lies within kLiftCells cells of the body's origin
        // or of a cell of its committed route (whatever cell it joins the
        // route at). A member whose footprint over that box reaches no
        // bucket holding a grounded or lift-home cell (see liftBuckets) can
        // ask no flyer to lift and is skipped without walking its steps.
        int bx0=W,bz0=w.occH_,bx1=-1,bz1=-1;
        for(const auto* cells:{&groundedCells,&liftHomeCells})for(int c:*cells) {
            bx0=std::min(bx0,c%W);bx1=std::max(bx1,c%W);bz0=std::min(bz0,c/W);bz1=std::max(bz1,c/W);
        }
        // The member's planned steps (detour route, else its group field's
        // descent, else the straight line to its goal), kLiftCells ahead;
        // asks every liftable flyer its footprint covers on the way to lift.
        // `dry` (TAK_LEGION_VERIFY) only reports whether the walk covers
        // any grounded or lift-home cell, and asks nothing.
        auto walk=[&](Unit* u,const Member& m,const Group& g,bool dry) {
            const auto& p=planes[size_t(g.plane)];
            const Field* f=g.field&&g.field->done?g.field.get():nullptr;
            const int fx=u->type->footX,fz=u->type->footZ;
            int x=footprintOrigin(u->x,fx),z=footprintOrigin(u->z,fz);
            const int gx=m.goal%W,gz=m.goal/W;
            size_t r=0;
            for(int k=0;k<=kLiftCells;++k) {
                for(int j=0;j<fz;++j)for(int i=0;i<fx;++i) {
                    const int cx=x+i,cz=z+j;
                    if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
                    const size_t c=size_t(cz)*w.occW_+cx;
                    int32_t o=groundedCells.empty()?0:grounded[c];
                    if(!o&&!liftHomeCells.empty())o=liftHome[c];
                    if(!o)continue;
                    if(dry)return true;
                    Unit* flyer=w.unit(o);
                    // A flyer of this member's own squad (a mixed formation that
                    // has just landed where the formation is settling) stays down:
                    // the member settles beside it rather than driving it back up.
                    if(flyer&&u->squad&&u->squad==flyer->squad)continue;
                    if(flyer&&w.legionLiftable(*flyer,u->player)) {w.requestLegionLift(*flyer);++stats.lifts;}
                }
                if(k==kLiftCells||(x==gx&&z==gz))break;
                // The next planned cell.
                int nx=x,nz=z;
                while(r<m.route.size()&&m.route[r]==z*W+x)++r;
                if(r<m.route.size()) {nx=m.route[r]%W;nz=m.route[r]/W;++r;}
                else if(f&&f->inside(x,z)&&f->at(size_t(z*W+x))!=kUnreached) {
                    uint32_t best=f->at(size_t(z*W+x));
                    for(const auto& d:kDirections) {
                        if(!step(p,x,z,d[0],d[1])||!f->inside(x+d[0],z+d[1]))continue;
                        const uint32_t v=f->at(size_t((z+d[1])*W+x+d[0]));
                        if(v<best) {best=v;nx=x+d[0];nz=z+d[1];}
                    }
                } else {
                    nx=x+(gx>x)-(gx<x);nz=z+(gz>z)-(gz<z);
                }
                if(nx==x&&nz==z)break;
                x=nx;z=nz;
            }
            return false;
        };
        uint64_t walked=0,skipped=0;
        for(auto& [id,m]:members) {
            if((uint32_t(id)+w.tickCounter_)%kLiftStride)continue;
            if(m.state==Arrived||m.state==Trapped||m.goal<0)continue;
            Unit* u=w.unit(id);
            if(!u||!u->alive()||!u->type||u->type->canFly||u->embarked())continue;
            {
                int x0=footprintOrigin(u->x,u->type->footX),z0=footprintOrigin(u->z,u->type->footZ),x1=x0,z1=z0;
                for(const int c:m.route) {x0=std::min(x0,c%W);x1=std::max(x1,c%W);z0=std::min(z0,c/W);z1=std::max(z1,c/W);}
                x0-=kLiftCells;z0-=kLiftCells;x1+=kLiftCells+u->type->footX-1;z1+=kLiftCells+u->type->footZ-1;
                if((m.route.empty()&&(x0>bx1||z0>bz1||x1<bx0||z1<bz0))||!liftBucketsHit(x0,z0,x1,z1)) {
#ifndef NDEBUG
                    if(gVerify) {
                        const auto group=groups.find(m.group);
                        if(group!=groups.end()&&walk(u,m,group->second,true))
                            verifyFail("lift gate skipped a member whose steps cover a grounded or lift-home cell");
                    }
#endif
                    ++skipped;continue;
                }
            }
            const auto group=groups.find(m.group);
            if(group==groups.end())continue;
            ++walked;
            walk(u,m,group->second,false);
        }
        stats.liftMembersWalked+=walked;stats.liftMembersSkipped+=skipped;
    }
    void tick() {
        if(gProbe) {
            using Clock=std::chrono::steady_clock;
            auto ms=[](Clock::time_point a,Clock::time_point b) {return std::chrono::duration<double,std::milli>(b-a).count();};
            const auto t0=Clock::now();
            syncStatic();
            const auto t1=Clock::now();
            stampGrounded();
            const auto t2=Clock::now();
            scanStill();
            const auto t3=Clock::now();
            liftFlyers();
            const auto t4=Clock::now();
            probeSyncMs+=ms(t0,t1);probeStampMs+=ms(t1,t2);probeLiftMs+=ms(t3,t4);
        } else {
            syncStatic();
            stampGrounded();
            scanStill();
            liftFlyers();
        }
        // The stripe cycle's last tick: the soft cells it added, as blocks.
        if(w.tickCounter_%kStillScan==kStillScan-1)softBlocks();
        awareScan();
        prebuildStep();
        prune();
        // Yields run even while no Legion member remains (a committed yield
        // must finish or time out, never freeze until the next order).
        serviceYields();
        // Plane rebuilds and labelling are charged against the same
        // deterministic quota as field relaxations (debt carries over).
        // At most half the quota pays plane debt in one tick, so field work
        // always progresses (the rest of the debt carries over).
        uint64_t budget=kFieldQuota,charged=0;
        auto settle=[&] {
            const uint64_t c=std::min({budget,planeDebt,kFieldQuota/2-charged});
            budget-=c;planeDebt-=c;charged+=c;
        };
        settle();
        uint64_t visits=0;
        probeListSum+=waitedIds.size()+buildingIds.size()+needFieldIds.size()+staleDoneIds.size();
        probeGroupSum+=groups.size();
        auto finish=[&](Group& g,Field& f,uint64_t spent) {
            budget-=std::min(budget,spent);stats.fieldWork+=spent;
            if(spent) {
                // Who the work was for (Stats only): a refresh is the
                // replacement building in `next`; a group has an area when
                // some goal is shared (more members than distinct goals).
                if(g.next.get()==&f)
                    (w.tickCounter_-g.movingTick<=2?stats.fieldWorkRefreshMoving:stats.fieldWorkRefreshIdle)+=spent;
                else
                    (policy(g.kind).area&&!g.approach&&g.members>int(g.sharing.size())?stats.fieldWorkFirstSlot:stats.fieldWorkFirstSolo)+=spent;
            }
            if(f.done) {
                ++stats.fieldsBuilt;
                if(g.next) {g.field=std::move(g.next);++fieldsDone;g.stale=g.field->epoch!=epoch;g.demand=false;restaleSlots(g);++stats.refreshCompleted;}
                listGroup(g);
            }
        };
        // Groups with a member standing still for want of a field come
        // first (their field starts, or its frontier advances toward them),
        // then first fields whose members all steer already, then refreshes
        // (members still steer by the finished stale field).
        // Each loop walks its work list (see buildingIds) in id order with
        // the predicate of a walk over every group: a list holds every
        // group the predicate accepts, including one a step of the loop
        // makes eligible (an eviction), so the same groups are served in
        // the same order.
        for(int id=after(waitedIds,0);id;id=after(waitedIds,id)) {
            if(budget==0)break;
            ++visits;
            Group& g=groups.find(id)->second;
            if(w.tickCounter_-g.waited>1) {waitedIds.erase(id);continue;}
            if(g.next||(g.field&&g.field->done))continue;
            plane(g.plane);settle();
            if(budget==0)break;
            if(!g.field&&!startField(g))continue;
            if(!g.field->done)finish(g,*g.field,advance(*g.field,budget));   // (done: shared)
        }
        uint64_t refresh=kRefreshQuota;
        for(int pass=0;pass<2;++pass)for(int id=after(buildingIds,0);id;id=after(buildingIds,id)) {
            if(budget==0)break;
            ++visits;
            Group& g=groups.find(id)->second;
            Field* f=pass==1?g.next.get():g.field&&!g.field->done?g.field.get():nullptr;
            if(!f) {if(!wantsBuilding(g))listGroup(g);continue;}   // a shared build another group finished
            if(f->done) {finish(g,*f,0);continue;}   // a refresh another group finished
            if(pass==1&&refresh==0)continue;
            plane(g.plane);settle();
            if(budget==0)break;
            const uint64_t spent=advance(*f,pass==1?std::min(budget,refresh):budget);
            if(pass==1)refresh-=std::min(refresh,spent);
            finish(g,*f,spent);
        }
        // Groups that need a field start in id order: first those with none
        // (they cannot steer at all), then stale refreshes. Served first,
        // refreshes of older groups under constant churn starved a new
        // group forever.
        for(int pass=0;pass<2;++pass) {
            const std::set<int>& ids=pass==0?needFieldIds:staleDoneIds;
            for(int id=after(ids,0);id;id=after(ids,id)) {
                if(budget==0)break;
                ++visits;
                Group& g=groups.find(id)->second;
                if((g.field&&(!g.stale||!g.field->done))||g.next)continue;
                if((pass==0)!=(g.field==nullptr))continue;
                // B1: nobody steers by an inactive group's stale field; it
                // stays listed and refreshes the tick the group is active
                // (refresh suppressed / demand resumes count the visits). A
                // refresh already under way keeps building whatever its group
                // does (pausing it measured worse: crowdtrap 200x1 arrived
                // settled -4% on every seed).
                if(pass==1&&!active(g)) {++stats.refreshSuppressed;g.suppressed=true;continue;}
                if(pass==1&&g.suppressed) {++stats.demandResumes;g.suppressed=false;}
                plane(g.plane);settle();
                if(budget==0)break;
                // With the refresh allowance spent, a stale group only takes a
                // field another group already has (see sharedField).
                const bool shareOnly=pass==1&&refresh==0;
                if(!startField(g,shareOnly)) {if(shareOnly) {++stats.refreshDeferred;continue;}break;}
                Field& f=g.next?*g.next:*g.field;
                if(f.done)continue;   // shared
                const uint64_t spent=advance(f,pass==1?std::min(budget,refresh):budget);
                if(pass==1)refresh-=std::min(refresh,spent);
                finish(g,f,spent);
            }
        }
        stats.schedGroupVisits+=visits;stats.groupLoopIters+=visits;
        // The longest run of ticks whose whole field quota went to work.
        quotaPegRun=budget==0?quotaPegRun+1:0;
        stats.quotaPegRunMax=std::max(stats.quotaPegRunMax,quotaPegRun);
        buildPassMask();
#ifndef NDEBUG
        if(gVerify)verifyIndexes();
#endif
        if(gProbe&&w.tickCounter_%gProbe==0)probeLine();
    }
    // TAK_LPROBE: the scheduler counters (cumulative) on one stderr line.
    void probeLine() const {
        const Stats& s=stats;
        std::fprintf(stderr,"LPROBE tick=%u units=%zu groups=%zu members=%zu sched_group_visits=%llu sharedfield_full_scans=%llu softowner_lookups=%llu"
            " field_work=%llu first_slot=%llu first_solo=%llu refresh_moving=%llu refresh_idle=%llu refresh_deferred=%llu blocked_rerequests=%llu"
            " lifts=%llu lift_members_walked=%llu lift_members_skipped=%llu waiting_member_ticks=%llu demand_resumes=%llu refresh_suppressed=%llu fields_paused=%llu"
            " still_units_processed=%llu still_per_residue_max=%llu quota_peg_run_max=%llu anchor_walk_iters=%llu share_scan_iters=%llu list_sizes=%llu group_ticks=%llu sync_ms=%.3f stamp_ms=%.3f lift_ms=%.3f\n",
            w.tickCounter_,w.units_.size(),groups.size(),members.size(),(unsigned long long)s.schedGroupVisits,(unsigned long long)s.sharedfieldFullScans,
            (unsigned long long)s.softownerLookups,(unsigned long long)s.fieldWork,(unsigned long long)s.fieldWorkFirstSlot,
            (unsigned long long)s.fieldWorkFirstSolo,(unsigned long long)s.fieldWorkRefreshMoving,(unsigned long long)s.fieldWorkRefreshIdle,
            (unsigned long long)s.refreshDeferred,(unsigned long long)s.blockedRerequests,(unsigned long long)s.lifts,
            (unsigned long long)s.liftMembersWalked,(unsigned long long)s.liftMembersSkipped,(unsigned long long)s.waitingMemberTicks,
            (unsigned long long)s.demandResumes,(unsigned long long)s.refreshSuppressed,(unsigned long long)s.fieldsPaused,(unsigned long long)s.stillUnitsProcessed,
            (unsigned long long)s.stillPerResidueMax,(unsigned long long)s.quotaPegRunMax,(unsigned long long)s.anchorWalkIters,
            (unsigned long long)s.shareScanIters,(unsigned long long)probeListSum,(unsigned long long)probeGroupSum,
            probeSyncMs,probeStampMs,probeLiftMs);
    }
#ifndef NDEBUG
    // TAK_LEGION_VERIFY: every derived index and work list against a full
    // scan (the W1 steps add theirs here), and the Stats' own invariants.
    void verifyIndexes() {
        const Stats& s=stats;
        auto fail=[&](const char* what) {
            std::fprintf(stderr,"TAK_LEGION_VERIFY tick %u: %s\n",w.tickCounter_,what);
            std::abort();
        };
        if(s.fieldWorkFirstSlot+s.fieldWorkFirstSolo+s.fieldWorkRefreshMoving+s.fieldWorkRefreshIdle!=s.fieldWork)
            fail("field work classes do not sum to fieldWork");
        uint64_t dist=0;
        for(uint64_t n:s.completionDist)dist+=n;
        if(dist!=s.arrivals)fail("completion distance histogram does not sum to arrivals");
        if(s.midrouteCompletions>s.outsideAreaCompletions||s.outsideAreaCompletions>s.arrivals)
            fail("mid-route completions exceed outside-area completions or arrivals");
        {
            // anchorsAt against a count of every anchor (settleReach).
            std::map<std::tuple<int,int32_t,int32_t>,int> count;
            for(const auto& [id,a]:anchors) {
                ++count[destination(a.point)];
                if(!isAnchor(id))fail("an anchor is not marked");
            }
            if(count!=anchorsAt)fail("anchorsAt differs from a count of the anchors");
        }
        if(s.completionDistMax*s.arrivals<s.completionDistSum)fail("completion distance sum exceeds max x arrivals");
        if(s.schedGroupVisits>s.groupLoopIters)fail("scheduler visits exceed group loop iterations");
        uint64_t calls=0;
        for(uint64_t n:s.moveCallsByState)calls+=n;
        if(calls<s.moves)fail("move calls by state below moves");
        if(s.formationRingCells+s.rechoiceBfsCells>s.slotSearchCells)
            fail("slot search cells below the formation ring and re-choice cells");
        if(s.fieldsPaused)fail("counters of mechanisms that do not exist yet are non-zero");
        if(s.demandResumes>s.refreshSuppressed)fail("more demand resumes than suppressed refreshes");
        verifySoft(fail);
        // The scheduler lists against a scan of every group.
        {
            size_t building=0,need=0,staleDone=0,built=0;
            for(const auto& [id,g]:groups) {
                if(g.id!=id)fail("group id differs from its key");
                if(wantsBuilding(g)&&!g.onBuilding)fail("a building group is off buildingIds");
                if(g.onBuilding&&!g.field&&!g.next)fail("a group without field or refresh is on buildingIds");
                if(g.onNeedField!=wantsNeedField(g))fail("needFieldIds flag differs from the group");
                if(g.onStaleDone!=wantsStaleDone(g))fail("staleDoneIds flag differs from the group");
                if(g.onByBuilt!=(g.field!=nullptr)||(g.onByBuilt&&g.byBuiltKey!=g.built))fail("byBuilt entry differs from the group");
                if(g.onBuilding!=(buildingIds.count(id)>0)||g.onNeedField!=(needFieldIds.count(id)>0)||
                   g.onStaleDone!=(staleDoneIds.count(id)>0)||(g.onByBuilt&&!byBuilt.count({g.byBuiltKey,id})))
                    fail("a list flag differs from its list");
                if(w.tickCounter_-g.waited<=1&&!waitedIds.count(id))fail("a group that waited is off waitedIds");
                building+=g.onBuilding;need+=g.onNeedField;staleDone+=g.onStaleDone;built+=g.onByBuilt;
            }
            if(building!=buildingIds.size()||need!=needFieldIds.size()||staleDone!=staleDoneIds.size()||built!=byBuilt.size())
                fail("a scheduler list holds a group that is not listed");
            for(int id:waitedIds)if(!groups.count(id))fail("waitedIds holds a dead group");
            std::map<std::pair<int,uint64_t>,std::set<int>> routes;
            for(const auto& [id,g]:groups)for(const Field* f:{g.field.get(),g.next.get()}) {
                if(!f)continue;
                if(f->seedKey!=seedsKey(f->seeds))fail("a field's seed key differs from its seeds");
                routes[{g.plane,f->seedKey}].insert(id);
            }
            if(routes!=routeIndex)fail("routeIndex differs from the groups' fields");
        }
        // The flyer and structure lists against a walk of every unit (A5),
        // and the dense yard lookup against the script map.
        {
            std::vector<int> flyers,structures;
            for(const auto& u:w.units_) {
                if(u.legionLift&&!(u.type&&u.type->canFly))fail("a unit that is no flyer is lifted");
                if(!u.type)continue;
                if(u.type->canFly)flyers.push_back(u.id);
                if(u.type->isStructure()) {
                    structures.push_back(u.id);
                    const auto script=w.unitScripts_.find(u.id);
                    if(yardOpen(u.id)!=(script!=w.unitScripts_.end()&&script->second.yardOpen))
                        fail("the dense yard lookup differs from the script map");
                }
            }
            syncUnitLists();
            if(flyers!=flyerIds||structures!=structureIds)
                fail("flyerIds or structureIds differ from a walk of the units");
            for(size_t i=1;i<stamps.size();++i)if(stamps[i-1].first>=stamps[i].first)fail("stamps are not in ascending id order");
        }
        // liftFlyers' buckets against a scan of the grounded and lift-home
        // cells (A4).
        if(w.occW_>0&&grounded.size()==size_t(w.occW_)*w.occH_) {
            std::vector<uint16_t> count(liftBuckets.size(),0);
            for(size_t c=0;c<grounded.size();++c) {
                const size_t b=size_t(int(c)/w.occW_/kLiftBucket*liftBucketW+int(c)%w.occW_/kLiftBucket);
                if(b>=count.size())fail("liftBuckets do not cover the map");
                count[b]+=uint16_t((grounded[c]!=0)+(liftHome.size()==grounded.size()&&liftHome[c]!=0));
            }
            if(count!=liftBuckets)fail("liftBuckets differ from a scan of the grounded and lift-home cells");
            size_t listed=0;
            for(size_t b=0;b<count.size();++b)listed+=count[b]!=0;
            if(listed!=liftBucketList.size())fail("liftBucketList differs from the non-zero liftBuckets");
            for(int b:liftBucketList)if(!liftBuckets[size_t(b)])fail("liftBucketList holds an empty bucket");
        }
        // Counters only grow (the sizes are filled in by stats() alone).
        std::vector<uint64_t> before;
        forEachStat(verified,[&](const char*,uint64_t v) {before.push_back(v);});
        size_t i=0;
        forEachStat(s,[&](const char* name,uint64_t now) {
            if(now<before[i++]) {
                std::fprintf(stderr,"TAK_LEGION_VERIFY tick %u: counter %s decreased\n",w.tickCounter_,name);
                std::abort();
            }
        });
        verified=s;
    }
#endif
    // Group awareness: moving formations plan round each other as wholes.
    // Same-player and allied movers always; an enemy mover only within the
    // sight (World::sightDistance, from hashed unit types and positions) of
    // some member of the group, and never by a group whose mission engages
    // enemies (fight, attack, guard, patrol). Two groups meeting head on each
    // keep right: the corridor a group plans round is shifted to its left by
    // half the corridor's radius. Deterministic: integer, ordered, on a fixed
    // cadence; the plans are hashed.
    void awareScan() {
        if(w.tickCounter_%kAwareScan!=kAwareScan/2)return;
        struct Mover {uint64_t command;int player;uint32_t issue;int64_t px,pz,cx,cz,ax,az,r,dx,dz;};
        std::vector<Mover> movers;
        for(auto& [key,pt]:points) {
            if(std::get<0>(key)<0)continue;
            int64_t sx=0,sz=0,n=0;
            for(const int id:pt.ids) {
                const Member* mm=member(id);const Unit* v=w.unit(id);
                if(!mm||!v||(mm->state!=Moving&&mm->state!=Holding))continue;
                sx+=v->x.v>>16;sz+=v->z.v>>16;++n;
            }
            if(n<kAwareMembers) {pt.awareSeen=false;continue;}
            const int64_t cx=sx/n,cz=sz/n;
            int64_t ss=0;
            for(const int id:pt.ids) {
                const Member* mm=member(id);const Unit* v=w.unit(id);
                if(!mm||!v||(mm->state!=Moving&&mm->state!=Holding))continue;
                const int64_t ox=(v->x.v>>16)-cx,oz=(v->z.v>>16)-cz;ss+=ox*ox+oz*oz;
            }
            const int64_t dx=pt.awareSeen?cx-pt.awareX:0,dz=pt.awareSeen?cz-pt.awareZ:0;
            pt.awareX=cx;pt.awareZ=cz;pt.awareSeen=true;
            if(dx*dx+dz*dz<16*16)continue;   // standing: the soft-obstacle scan sees still bodies
            const int64_t r=isqrtFloor(uint64_t(ss/n))*3/2+16;
            movers.push_back({commandKey(std::get<0>(key),std::get<1>(key)),std::get<0>(key),std::get<1>(key),int64_t(std::get<2>(key))>>16,int64_t(std::get<3>(key))>>16,cx,cz,cx+dx*kAwareAhead,cz+dz*kAwareAhead,r,dx,dz});
        }
        // Each group's centroid, spread and the sight of its members.
        struct Acc {int64_t sx=0,sz=0,n=0,sight=0;};
        std::map<int,Acc> acc;
        for(const auto& [id,m]:members) {
            if(m.state!=Moving&&m.state!=Holding)continue;
            const Unit* v=w.unit(id);if(!v||!v->type)continue;
            auto& a=acc[m.group];a.sx+=v->x.v>>16;a.sz+=v->z.v>>16;++a.n;a.sight=std::max<int64_t>(a.sight,w.sightDistance(*v->type));
        }
        const int W=width();
        uint64_t pairs=0;
        stats.groupLoopIters+=groups.size();
        for(auto& [id,g]:groups) {
            const auto a=acc.find(id);
            std::vector<uint64_t> want;std::vector<Field::Corridor> seg;
            if(a!=acc.end()&&a->second.n>=kAwareMembers&&g.field&&g.field->done&&!movers.empty()) {
                const auto& p=planes[size_t(g.plane)];
                const int64_t cx=a->second.sx/a->second.n,cz=a->second.sz/a->second.n;
                int c=nearestLegal(p,int(cx/16),int(cz/16));
                if(c>=0&&g.field->at(size_t(c))!=kUnreached) {
                    std::vector<int> chain{c};
                    for(int k=0;k<kAwareChain;++k) {const int next=descend(p,*g.field,chain.back()%W,chain.back()/W,chain.back());if(next<0)break;chain.push_back(next);}
                    const int64_t tx=(chain.back()%W)*16-cx,tz=(chain.back()/W)*16-cz;
                    const bool engages=g.kind==Kind::Fight||g.kind==Kind::Attack||g.kind==Kind::Guard||g.kind==Kind::Patrol;
                    pairs+=movers.size();
                    for(const auto& mv:movers) {
                        if(mv.command==g.command)continue;
                        // One selection sent as several groups (one player,
                        // within kConvoyTicks, to points within kConvoyCells)
                        // never plans round itself, and a
                        // mover going the same way (within 60 degrees) is
                        // followed, not planned round.
                        const int64_t gcx=int64_t(g.minX+g.maxX)*8,gcz=int64_t(g.minZ+g.maxZ)*8;
                        if(mv.player==g.player&&(mv.issue>g.issuedTick?mv.issue-g.issuedTick:g.issuedTick-mv.issue)<=kConvoyTicks&&
                           std::abs(mv.px-gcx)<=kConvoyCells*16&&std::abs(mv.pz-gcz)<=kConvoyCells*16)continue;
                        {
                            const int64_t dot=tx*mv.dx+tz*mv.dz;
                            if(dot>0&&4*dot*dot>=(tx*tx+tz*tz)*(mv.dx*mv.dx+mv.dz*mv.dz))continue;
                        }
                        const bool friendly=mv.player==g.player||w.allied(g.player,mv.player);
                        if(!friendly) {
                            if(engages)continue;
                            const int64_t ex=mv.cx-cx,ez=mv.cz-cz,see=a->second.sight+mv.r;
                            if(ex*ex+ez*ez>see*see)continue;
                        }
                        const bool headOn=tx*mv.dx+tz*mv.dz<0;
                        // Crossing ways: only one group gives way, the one
                        // with the larger command key (player, then issue
                        // tick); head on, both keep right.
                        if(!headOn&&mv.command>g.command)continue;
                        // Already in among each other: too late to plan round
                        // (a late swerve only reverses bodies).
                        const bool known=std::find(g.avoidCmd.begin(),g.avoidCmd.end(),mv.command)!=g.avoidCmd.end();
                        {
                            const int64_t ex=mv.cx-cx,ez=mv.cz-cz;
                            if(!known&&ex*ex+ez*ez<4*mv.r*mv.r)continue;
                        }
                        Field::Corridor cor{mv.cx,mv.cz,mv.ax,mv.az,mv.r};
                        bool meets=false;
                        for(size_t k=0;k<chain.size()&&!meets;k+=4)
                            meets=Field::segDist2(int64_t(chain[k]%W)*16+8,int64_t(chain[k]/W)*16+8,cor)<=(cor.r+32)*(cor.r+32);
                        if(!meets)continue;
                        // Head on: keep right (plan round a corridor shifted left).
                        if(headOn) {
                            const int64_t tl=std::max<int64_t>(1,isqrtFloor(uint64_t(tx*tx+tz*tz)));
                            const int64_t lx=tz*cor.r/(2*tl),lz=-tx*cor.r/(2*tl);
                            cor.x0+=lx;cor.x1+=lx;cor.z0+=lz;cor.z1+=lz;
                        }
                        want.push_back(mv.command);seg.push_back(cor);
                    }
                }
            }
            // Hysteresis: a mover leaves the plan only after kAwareKeep
            // scans off the way; a new one re-plans at once.
            bool replan=false;
            std::vector<uint64_t> cmd;std::vector<Field::Corridor> keep;std::vector<uint8_t> off;
            for(size_t i=0;i<g.avoidCmd.size();++i) {
                const auto at=std::find(want.begin(),want.end(),g.avoidCmd[i]);
                if(at!=want.end()) {cmd.push_back(g.avoidCmd[i]);keep.push_back(g.avoidSeg[i]);off.push_back(0);continue;}
                if(g.avoidOff[i]+1<kAwareKeep) {cmd.push_back(g.avoidCmd[i]);keep.push_back(g.avoidSeg[i]);off.push_back(uint8_t(g.avoidOff[i]+1));continue;}
                replan=true;
            }
            for(size_t i=0;i<want.size();++i)
                if(std::find(g.avoidCmd.begin(),g.avoidCmd.end(),want[i])==g.avoidCmd.end()) {
                    cmd.push_back(want[i]);keep.push_back(seg[i]);off.push_back(0);replan=true;
                }
            if(replan) {
                // New corridors replace the planned ones (the movers moved on).
                for(size_t i=0;i<cmd.size();++i)
                    for(size_t j=0;j<want.size();++j)if(want[j]==cmd[i])keep[i]=seg[j];
            }
            g.avoidCmd.swap(cmd);g.avoidSeg.swap(keep);g.avoidOff.swap(off);
            if(replan&&g.field&&g.field->done) {
                if(g.next&&!g.next->done)++stats.refreshDiscards;
                g.next.reset();g.stale=true;g.demand=true;   // not a static change: refresh even if idle (C6)
                listGroup(g);
            }
        }
        stats.awarePairs+=pairs;
    }
    // Members whose unit died, embarked, lost its orders or left Legion's
    // plain-move domain (no more move() calls) are dropped here, a bounded
    // number per tick in id order, so their groups, slots and point claims
    // are freed. The death edge also cancels directly.
    void prune() {
        constexpr size_t kPrunePerTick=256;
        if(members.empty()&&anchors.empty()&&approachDone.empty()&&parts.empty()) {pruneCursor=0;return;}
        // The backstop (B3): one cursor in id order over the members and the
        // anchors, approachDone and parts maps, kPrunePerTick distinct ids a
        // tick, wrapping once. Their entries are erased by the orders event
        // and registerMove; this catches whatever no event reported. With no
        // records it is exactly the members-only cursor it replaced.
        auto im=members.lower_bound(pruneCursor);auto ia=anchors.lower_bound(pruneCursor);
        auto ic=approachDone.lower_bound(pruneCursor);auto ip=parts.lower_bound(pruneCursor);
        auto head=[&] {
            int id=INT_MAX;
            if(im!=members.end())id=std::min(id,im->first);
            if(ia!=anchors.end())id=std::min(id,ia->first);
            if(ic!=approachDone.end())id=std::min(id,ic->first);
            if(ip!=parts.end())id=std::min(id,ip->first);
            return id;
        };
        // (id, 1 member | 2 record) in visit order.
        std::vector<std::pair<int,int>> ids;
        const int start=pruneCursor;bool wrapped=false;
        int at=head();
        while(ids.size()<kPrunePerTick) {
            if(at==INT_MAX) {
                if(wrapped||start==0)break;
                wrapped=true;
                im=members.begin();ia=anchors.begin();ic=approachDone.begin();ip=parts.begin();
                at=head();
                if(at==INT_MAX)break;
            }
            if(wrapped&&at>=start)break;
            int what=0;
            if(im!=members.end()&&im->first==at) {what|=1;++im;}
            if(ia!=anchors.end()&&ia->first==at) {what|=2;++ia;}
            if(ic!=approachDone.end()&&ic->first==at) {what|=2;++ic;}
            if(ip!=parts.end()&&ip->first==at) {what|=2;++ip;}
            ids.push_back({at,what});
            at=head();
        }
        pruneCursor=at==INT_MAX?0:at;
        stats.anchorWalkIters+=ids.size();
        for(const auto& [id,what]:ids) {
            if(what&1) {
                const Unit* u=w.unit(id);
                if(!(u&&u->alive()&&!u->embarked()&&!u->orders.empty()&&supports(*u)))leave(id);
            }
            if(what&2) {markWatch(id);validate(id);}
        }
    }
    // Yield steps advance one update per tick in id order: straight
    // toward the target cell, no turning, then a full stop.
    void serviceYields() {
        for(auto it=yielding.begin();it!=yielding.end();) {
            Unit* u=w.unit(it->first);
            if(!u||!u->alive()||!u->orders.empty()) {it=yielding.erase(it);continue;}
            const int W=width(),fx=u->type->footX,fz=u->type->footZ;
            const Fixed tx=centre(it->second.cell%W,fx),tz=centre(it->second.cell/W,fz);
            const int64_t dx=int64_t(tx.v)-u->x.v,dz=int64_t(tz.v)-u->z.v;
            const int64_t length=isqrtFloor(uint64_t(dx*dx+dz*dz));
            if(length==0||++it->second.ticks>45) {u->speed=Fixed();u->turnReqBam=0;it=yielding.erase(it);continue;}
            const int64_t travel=std::min<int64_t>(std::max<int64_t>(1,retailGroundSpeedCap(u->type->maxVel*w.groundTerrainMultiplier(*u),u->groundPitch,0).v),length);
            const Fixed sx=Fixed::raw(int32_t(dx*travel/length)),sz=Fixed::raw(int32_t(dz*travel/length));
            const Fixed bx=u->x,bz=u->z;
            w.commitGroundStep(*u,sx,sz,true);
            u->speed=Fixed();u->turnReqBam=0;
            if(u->x==bx&&u->z==bz) {it=yielding.erase(it);continue;}
            ++stats.slides;++it;
        }
    }
    // Open a lane through settled arrivals: first the cell this member
    // wants, else any neighbouring cell nearer its own goal (in a packed
    // lattice the way through runs BETWEEN goals, where two rows can part).
    // A side cell becomes a committed shuffle while its blockers yield.
    bool yieldLane(const Unit& u,Member& m,const Plane& p,int nx,int nz) {
        if(requestYield(u,nx,nz))return true;
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        const int gx=m.goal%W,gz=m.goal/W;
        const int64_t here=int64_t(gx-ox)*(gx-ox)+int64_t(gz-oz)*(gz-oz);
        std::array<std::pair<int64_t,int>,8> options{};int count=0;
        for(int k=0;k<8;++k) {
            const auto& d=kDirections[size_t(k)];
            const int cx=ox+d[0],cz=oz+d[1];
            if((cx==nx&&cz==nz)||!step(p,ox,oz,d[0],d[1]))continue;
            const int64_t v=int64_t(gx-cx)*(gx-cx)+int64_t(gz-cz)*(gz-cz);
            if(v<here)options[size_t(count++)]={v,k};
        }
        std::sort(options.begin(),options.begin()+count);
        for(int i=0;i<count;++i) {
            const auto& d=kDirections[size_t(options[size_t(i)].second)];
            if(!requestYield(u,ox+d[0],oz+d[1]))continue;
            m.detour=(oz+d[1])*W+ox+d[0];m.detourTicks=0;m.detourFace=false;m.detourPass=false;
            return true;
        }
        return false;
    }
    // Idle bodies of the same player that never moved for Legion (no anchor:
    // units standing where they were left) part a lane for a held member:
    // each body in the member's way, along its axis of travel up to the
    // first free footprint past them (at most kPartCells), steps one or two
    // cells across the lane, away from its centre, without turning. The
    // lane may be the member's own or one cell to either side (the member
    // then shuffles over first): in a lattice of bodies with one-cell gaps
    // that lines it up with a gap, so one-cell steps open it. Every body
    // must find a legal target clear of the lane, the member and the other
    // targets, or none moves. A body never steps back the way it last
    // parted (to and fro reads as spinning), and parts at most kMaxParts
    // times. Opposing columns 250x8 at 50% moving, where idle bodies stand
    // between the movers and their goals: crossed 680 -> 894 and settled
    // 328 -> 644 (Retail 1000 / 475), means of seeds 0/7/42.
    struct Part {uint8_t count=0;int8_t sx=0,sz=0;};
    std::map<int,Part> parts;
    // Bodies whose approach order (an unreachable goal) dropped, idle since:
    // unit id -> the member's point (player, issue tick, requested point).
    // Forgotten on a new order or death. Read by approachSettled.
    using PointKey=std::tuple<int,uint32_t,int32_t,int32_t>;
    std::map<int,PointKey> approachDone;
    // One selection: the same command (player and issue tick), or one click
    // the uplink landed as several commands -- the convoy rule of the
    // awareness scan: one player, within kConvoyTicks, to points within
    // kConvoyCells.
    static bool oneSelection(const PointKey& a,const PointKey& b) {
        if(std::get<0>(a)!=std::get<0>(b))return false;
        if(std::get<1>(a)==std::get<1>(b))return true;
        const uint32_t ia=std::get<1>(a),ib=std::get<1>(b);
        return (ia>ib?ia-ib:ib-ia)<=kConvoyTicks&&
            std::abs(int64_t(std::get<2>(a))-std::get<2>(b))<=int64_t(kConvoyCells*16)<<16&&
            std::abs(int64_t(std::get<3>(a))-std::get<3>(b))<=int64_t(kConvoyCells*16)<<16;
    }
    bool partable(const Unit& u,int id) const {
        const Unit* b=w.unit(id);
        if(!b||!b->alive()||b->player!=u.player||!b->orders.empty()||b->speed!=Fixed()||!b->type||b->type->isStructure()||b->type->canFly)return false;
        if(anchors.count(id)||yielding.count(id))return false;
        const auto part=parts.find(id);
        return part==parts.end()||part->second.count<kMaxParts;
    }
    bool partLane(const Unit& u,Member& m,const Plane& p,int ox,int oz,int nx,int nz) {
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
        int dx=std::clamp(nx-ox,-1,1),dz=std::clamp(nz-oz,-1,1);
        if(dx&&dz) {
            // A diagonal step: part along the axis the goal lies farther along.
            const int gx=m.goal%W-ox,gz=m.goal/W-oz;
            if(std::abs(gx)>=std::abs(gz))dz=0;else dx=0;
        }
        if(!dx&&!dz)return false;
        const int px=dz?1:0,pz=dx?1:0;          // across the lane
        const int along=dx?fx:fz,across=dx?fz:fx;
        for(int off:{0,1,-1}) {
            const int sx=ox+off*px,sz=oz+off*pz;
            // The bodies in the lane, up to the first free footprint past them.
            std::array<int,16> ids{};int count=0,last=-1,end=-1;bool blocked=false;
            for(int k=1;k<=kPartCells&&!blocked;++k) {
                const int lx=sx+k*dx,lz=sz+k*dz;
                if(!legal(p,lx,lz)) {blocked=true;break;}
                bool any=false;
                for(int j=0;j<fz&&!blocked;++j)for(int i=0;i<fx&&!blocked;++i) {
                    const int cx=lx+i,cz=lz+j;
                    if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
                    const int32_t o=occAt(size_t(cz)*w.occW_+cx);
                    if(!o||o==u.id)continue;
                    any=true;
                    bool seen=false;for(int q=0;q<count;++q)seen|=ids[size_t(q)]==o;
                    if(seen)continue;
                    if(count==16||!partable(u,o)) {blocked=true;break;}
                    ids[size_t(count++)]=o;
                }
                if(blocked)break;
                if(any)last=k;
                else if(count&&k>=last+along) {end=k;break;}
            }
            if(blocked||!count||end<0)continue;
            // (Checked after the scan: most lanes fail on a moving body at once.)
            if(off&&(!step(p,ox,oz,off*px,off*pz)||!stepFree(u,ox,oz,sx,sz)))continue;
            std::sort(ids.begin(),ids.begin()+count);
            const int l0=dx?sz:sx;
            std::array<int,16> cells{},feetX{},feetZ{};bool ok=true;
            for(int q=0;q<count&&ok;++q) {
                const Unit& b=*w.unit(ids[size_t(q)]);
                const int bfx=b.type->footX,bfz=b.type->footZ;
                const int bx=footprintOrigin(b.x,bfx),bz=footprintOrigin(b.z,bfz);
                const int b0=dx?bz:bx,bw=dx?bfz:bfx;
                const int side=2*b0+bw>=2*l0+across?1:-1;
                const auto part=parts.find(ids[size_t(q)]);
                int best=-1;
                for(int shift:{side,-side,2*side,-2*side}) {
                    if(b0+shift<l0+across&&l0<b0+shift+bw)continue;   // still in the lane
                    if(part!=parts.end()&&part->second.sx*shift*px+part->second.sz*shift*pz<0)continue;
                    const int tx=bx+shift*px,tz=bz+shift*pz;
                    if(tx<ox+fx&&ox<tx+bfx&&tz<oz+fz&&oz<tz+bfz)continue;
                    bool reserved=false;
                    for(int r=0;r<q&&!reserved;++r) {
                        const int rx=cells[size_t(r)]%W,rz=cells[size_t(r)]/W;
                        reserved=tx<rx+feetX[size_t(r)]&&rx<tx+bfx&&tz<rz+feetZ[size_t(r)]&&rz<tz+bfz;
                    }
                    if(reserved||!yieldFree(b,tx,tz))continue;
                    if(std::abs(shift)==2&&!yieldFree(b,bx+shift/2*px,bz+shift/2*pz))continue;
                    best=tz*W+tx;break;
                }
                if(best<0)ok=false;
                else {cells[size_t(q)]=best;feetX[size_t(q)]=bfx;feetZ[size_t(q)]=bfz;}
            }
            if(!ok)continue;
            for(int q=0;q<count;++q) {
                const Unit& b=*w.unit(ids[size_t(q)]);
                const int bx=footprintOrigin(b.x,b.type->footX),bz=footprintOrigin(b.z,b.type->footZ);
                auto& part=parts[ids[size_t(q)]];++part.count;markWatch(ids[size_t(q)]);
                part.sx=int8_t(std::clamp(cells[size_t(q)]%W-bx,-1,1));part.sz=int8_t(std::clamp(cells[size_t(q)]/W-bz,-1,1));
                yielding[ids[size_t(q)]]=Yield{cells[size_t(q)],0};
            }
            if(off) {m.detour=sz*W+sx;m.detourTicks=0;m.detourFace=false;m.detourPass=false;}
            return true;
        }
        return false;
    }
    // The cells this member wants are held by settled same-player arrivals:
    // ask each of them to step one cell clear of that footprint, staying
    // within one cell of its own goal origin. All blockers must be able to
    // yield, or none is asked. Returns whether a yield was committed.
    // Body legality of a yield step (World::mobilePlacement needs a loaded
    // placement plane; legacy terrain-only worlds use the nav grid, as
    // bodiesFree does).
    bool yieldFree(const Unit& b,int x,int z) const {
        if(placementPlane())return w.mobilePlacement(b,x,z,false);
        const int foot=std::clamp(std::max(b.type->footX,b.type->footZ),1,15);
        return w.cellFree(centre(x,b.type->footX),centre(z,b.type->footZ),b.id,foot);
    }
    bool requestYield(const Unit& u,int nx,int nz) {
        const int fx=u.type->footX,fz=u.type->footZ,W=width();
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        std::array<int,16> ids{};int count=0;
        for(int j=0;j<fz;++j)for(int i=0;i<fx;++i) {
            const int cx=nx+i,cz=nz+j;
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
            const int32_t o=occAt(size_t(cz)*w.occW_+cx);
            if(!o||o==u.id)continue;
            bool seen=false;for(int k=0;k<count;++k)seen|=ids[size_t(k)]==o;
            if(seen)continue;
            if(count==16)return false;
            ids[size_t(count++)]=o;
        }
        if(!count)return false;
        // Every blocker must be a settled arrival (checked below with the
        // rest); most bodies in a crowd are not, so reject them first.
        for(int k=0;k<count;++k)if(!isAnchor(ids[size_t(k)]))return false;
        std::sort(ids.begin(),ids.begin()+count);
        std::array<int,16> cells{};
        std::array<int,16> feetX{},feetZ{};
        for(int k=0;k<count;++k) {
            const int id=ids[size_t(k)];
            const Unit* b=w.unit(id);
            auto anchor=anchors.find(id);
            if(!b||!b->alive()||b->player!=u.player||!b->orders.empty()||b->speed!=Fixed()||
               anchor==anchors.end()||yielding.count(id)||anchor->second.yields>=kMaxYields)return false;
            const int bfx=b->type->footX,bfz=b->type->footZ;
            const int bx=footprintOrigin(b->x,bfx),bz=footprintOrigin(b->z,bfz);
            const int gx=anchor->second.goal%W,gz=anchor->second.goal/W;
            // A formation arrival stays inside its point's area and off the
            // cells other members claimed there (its own footprint excepted).
            const auto pt=points.find(anchor->second.point);
            const bool formation=pt!=points.end()&&pt->second.assigned&&pt->second.limit>0;
            int best=-1;int64_t bestD=0;
            for(const auto& d:kDirections) {
                const int cx=bx+d[0],cz=bz+d[1];
                if(std::abs(cx-gx)>1||std::abs(cz-gz)>1)continue;
                // Clear of the member's wanted footprint and its current one.
                auto overlaps=[&](int ax,int az){return cx<ax+fx&&ax<cx+bfx&&cz<az+fz&&az<cz+bfz;};
                if(overlaps(nx,nz)||overlaps(ox,oz))continue;
                // Never onto a target an earlier blocker of this request took.
                bool reserved=false;
                for(int q=0;q<k&&!reserved;++q) {
                    const int qx=cells[size_t(q)]%W,qz=cells[size_t(q)]/W;
                    reserved=cx<qx+feetX[size_t(q)]&&qx<cx+bfx&&cz<qz+feetZ[size_t(q)]&&qz<cz+bfz;
                }
                if(reserved)continue;
                if(formation) {
                    const int64_t px=int64_t(std::get<2>(anchor->second.point))>>16,pz=int64_t(std::get<3>(anchor->second.point))>>16;
                    const int64_t tx=int64_t(cx)*16+bfx*8-px,tz=int64_t(cz)*16+bfz*8-pz;
                    if(tx*tx+tz*tz>pt->second.limit*pt->second.limit)continue;
                    bool claimed=false;
                    for(int j=0;j<bfz&&!claimed;++j)for(int i=0;i<bfx&&!claimed;++i) {
                        const int qx=cx+i,qz=cz+j;
                        if(qx>=bx&&qx<bx+bfx&&qz>=bz&&qz<bz+bfz)continue;
                        claimed=pt->second.cells.count(qz*W+qx)>0;
                    }
                    if(claimed)continue;
                }
                if(!yieldFree(*b,cx,cz))continue;
                if(d[0]&&d[1]&&(!yieldFree(*b,cx,bz)||!yieldFree(*b,bx,cz)))continue;
                const int64_t dd=int64_t(cx-gx)*(cx-gx)+int64_t(cz-gz)*(cz-gz);
                if(best<0||dd<bestD) {best=cz*W+cx;bestD=dd;}
            }
            if(best<0)return false;
            cells[size_t(k)]=best;feetX[size_t(k)]=bfx;feetZ[size_t(k)]=bfz;
        }
        for(int k=0;k<count;++k) {
            ++anchors[ids[size_t(k)]].yields;
            yielding[ids[size_t(k)]]=Yield{cells[size_t(k)],0};markWatch(ids[size_t(k)]);
        }
        return true;
    }

    // ---- movement -------------------------------------------------------
    static Fixed centre(int origin,int foot) {return Fixed::fromInt(origin*16+foot*8);}
    // Aim point for a step into an ADJACENT origin cell: move only along the
    // axes whose origin changes (to the new cell's centre line) and keep the
    // other coordinate. Aiming at the cell centre would pull the body back
    // along the unchanged axis -- a visible back-and-forth in a crowd.
    std::pair<Fixed,Fixed> stepAim(const Unit& u,int cell) const {
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        const int nx=cell%W,nz=cell/W;
        return {nx!=ox?centre(nx,fx):u.x,nz!=oz?centre(nz,fz):u.z};
    }
    // Every origin the body passes through moving in a straight line from
    // (x0,z0) to (x1,z1) must be legal; an exact corner crossing must clear
    // both side cells. Raw 16.16 coordinates shifted to origin space.
    bool sweep(const Plane& p,const Unit& u,Fixed x0,Fixed z0,Fixed x1,Fixed z1) const {
        return trace(u,x0,z0,x1,z1,[&](int x,int z) {return this->legal(p,x,z);});
    }
    template<class Ok>
    bool trace(const Unit& u,Fixed x0,Fixed z0,Fixed x1,Fixed z1,const Ok& legal) const {
        const int64_t bx=int64_t(u.type->footX-1)*8*Fixed::kOne,bz=int64_t(u.type->footZ-1)*8*Fixed::kOne;
        const int64_t ax=int64_t(x0.v)-bx,az=int64_t(z0.v)-bz,ex=int64_t(x1.v)-bx,ez=int64_t(z1.v)-bz;
        int cx=int(ax>>20),cz=int(az>>20);const int tx=int(ex>>20),tz=int(ez>>20);
        ++stats.lineSweeps;
        uint64_t guard=0;
        struct Flush {uint64_t& to;uint64_t& n;~Flush() {to+=n+1;}} flush{stats.traceCells,guard};
        if(!legal(cx,cz))return false;
        const int64_t dx=ex-ax,dz=ez-az;
        const int sx=dx>0?1:-1,sz=dz>0?1:-1;
        for(;(cx!=tx||cz!=tz)&&guard<4*kFormationLineCells+8;++guard) {
            const bool canX=cx!=tx,canZ=cz!=tz;
            int64_t nx=0,nz=0;   // distance to the next boundary along each axis
            if(canX)nx=sx>0?(int64_t(cx+1)<<20)-ax:ax-(int64_t(cx)<<20);
            if(canZ)nz=sz>0?(int64_t(cz+1)<<20)-az:az-(int64_t(cz)<<20);
            // Compare nx/|dx| with nz/|dz| without dividing.
            int order=0;   // -1 x first, 1 z first, 0 corner
            if(!canZ)order=-1;else if(!canX)order=1;
            else {
                const __int128 lx=__int128(nx)*(dz<0?-dz:dz),lz=__int128(nz)*(dx<0?-dx:dx);
                order=lx<lz?-1:lx>lz?1:0;
            }
            if(order==0) {
                if(!legal(cx+sx,cz)||!legal(cx,cz+sz)||!legal(cx+sx,cz+sz))return false;
                cx+=sx;cz+=sz;
            } else if(order<0) {cx+=sx;if(!legal(cx,cz))return false;}
            else {cz+=sz;if(!legal(cx,cz))return false;}
        }
        return cx==tx&&cz==tz;
    }
    // Mobile bodies at an origin (static legality is checked separately).
    bool bodiesFree(const Unit& u,int x,int z) const {
        const int fx=u.type->footX,fz=u.type->footZ;
        if(w.occW_>0) {
            for(int j=0;j<fz;++j)for(int i=0;i<fx;++i) {
                const int cx=x+i,cz=z+j;
                if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
                const int32_t o=occAt(size_t(cz)*w.occW_+cx);
                if(o&&o!=u.id)return false;
            }
        }
        if(placementPlane())return w.mobilePlacement(u,x,z,false);
        const int foot=std::clamp(std::max(fx,fz),1,15);
        return w.cellFree(centre(x,fx),centre(z,fz),u.id,foot);
    }
    bool stepFree(const Unit& u,int ox,int oz,int nx,int nz) const {
        if(nx==ox&&nz==oz)return true;
        if(!bodiesFree(u,nx,nz))return false;
        return nx==ox||nz==oz||(bodiesFree(u,nx,oz)&&bodiesFree(u,ox,nz));
    }
    void hold(Unit& u,Member& m) {
        if(m.state!=Holding)m.nextDetour=8;
        // Holding is a full stop with a constant heading: no creeping, no
        // re-aiming. The body wakes when a candidate cell frees (rechecked
        // against occupancy each update), not on a timer.
        u.speed=Fixed();u.turnReqBam=0;
        if(m.state!=Holding)++stats.holds;
        if(m.heldSince==kNoTick)m.heldSince=w.tickCounter_;
        m.state=Holding;++m.holdUpdates;
    }
    // Ticks since the body stopped stepping (0 while it steps), and since
    // its progress last improved: the hold and stall clocks.
    // Both read as the body's own update sees them: heldFor at its start
    // (holds before this tick), stalledFor after its progress test.
    uint32_t heldFor(const Member& m) const {return m.heldSince==kNoTick||m.heldSince>=w.tickCounter_?0:w.tickCounter_-m.heldSince;}
    uint32_t stalledFor(const Member& m) const {return w.tickCounter_-m.stallTick;}
    // Another member's stall clock as its last update left it: one tick
    // less while its update of this tick is still to come. (The counters
    // read a peer that way, and so does every rest stride: a rested peer's
    // move() still runs, so the reading never depends on the stride.)
    uint32_t peerStalledFor(const Member& peer) const {
        const uint32_t stall=stalledFor(peer);
        return peer.movedTick==w.tickCounter_||stall==0?stall:stall-1;
    }
    // The stall clock stands still while the body walks a committed route
    // or side-step (it measures no progress on the way the body is NOT
    // being steered round): the stamp moves with the tick. Without this a
    // body finishing a long way round arrived "stalled" and settled where
    // the route left it (gap6: one body short of the area, order never
    // complete in 3 of 5 offsets). Resting bodies walk neither.
    void holdStall(Member& m) const {m.stallTick=std::min(m.stallTick+1,w.tickCounter_);}
    void complete(Unit& u,Member& m,bool contact) {
        // The nearest reachable point to an unreachable goal is not a
        // completion: the body stops there and its order is retired after
        // kApproachRetire (see trapped), as Retail drops an unreachable goal.
        if(m.approach) {trapped(u,m);return;}
        // A goal its owner ends (combat in range, guard within reach) is
        // never declared reached by Legion: the body holds there, order kept.
        if(!policy(m.kind).completes) {hold(u,m);return;}
        auto& leg=u.orders[World::currentLeg(u.orders)];
        const Policy rule=policy(m.kind);
        u.speed=Fixed();u.turnReqBam=0;
        // An exact goal is reached only on its own geometry; anywhere else
        // the approach failed and the mission handler decides (0x200).
        const uint32_t event=rule.exact&&!accepts(u,leg,m.kind)?0x200u:0x500u;
        (rule.transport?leg.transportMission:leg.mission).pending|=event;
        ++(event==0x500u?stats.missionArrivals:stats.missionFailures)[size_t(m.kind)];
        leg.controller=0;leg.navigationExhausted=true;
        if(uint32_t(u.routeStamp)<=w.tickCounter_-6u)u.routeStamp=0;
        ++stats.arrivals;if(contact)++stats.contactArrivals;
        completionStats(u,m);
        if(policy(m.kind).passThrough) {leave(u.id);return;}
        m.state=Arrived;
        setAnchor(u.id,Anchor{m.goal,0,m.point});
        leave(u.id);
    }
    // Where a leg completed, from its click (Stats only): the distance
    // histogram, outside the destination area, and mid-route (well outside
    // it with no settled body of the same order near).
    void completionStats(const Unit& u,const Member& m) const {
        const int64_t dx=(int64_t(u.x.v)-std::get<2>(m.point))>>16,dz=(int64_t(u.z.v)-std::get<3>(m.point))>>16;
        const int64_t dist=isqrtFloor(uint64_t(dx*dx+dz*dz)),cells=dist/16;
        int bucket=0;
        for(int64_t c=cells;c>0&&bucket<9;c>>=1)++bucket;
        ++stats.completionDist[bucket];stats.completionDistSum+=uint64_t(cells);
        stats.completionDistMax=std::max(stats.completionDistMax,uint64_t(cells));
        const int fx=u.type->footX,fz=u.type->footZ,foot=std::max(fx,fz);
        const int64_t body=int64_t(foot)*16;
        int64_t area=body;
        if(m.pt&&m.pt->assigned&&m.pt->limit>0)area=m.pt->limit;
        else if(const auto g=groups.find(m.group);g!=groups.end()) {
            const auto peak=g->second.peak.find(m.requested);
            const int64_t count=peak==g->second.peak.end()?1:peak->second;
            if(count>1)area=body+body*isqrtFloor(uint64_t(count)*100000000/31416)/100;
        }
        if(dist<=area)return;
        ++stats.outsideAreaCompletions;
        if(dist<=area+20*16)return;
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz),reach=3*foot;
        for(int z=std::max(0,oz-reach);z<std::min(w.occH_,oz+fz+reach);++z)
            for(int x=std::max(0,ox-reach);x<std::min(w.occW_,ox+fx+reach);++x) {
                const int32_t o=occAt(size_t(z)*w.occW_+x);
                if(!o||o==u.id||!isAnchor(o))continue;
                const auto a=anchors.find(o);
                if(a!=anchors.end()&&std::get<0>(a->second.point)==std::get<0>(m.point)&&std::get<1>(a->second.point)==std::get<1>(m.point))return;
            }
        ++stats.midrouteCompletions;
    }
    void trapped(Unit& u,Member& m) {
        // No legal route exists for this footprint: stop at once (zero speed,
        // constant heading, no probing). The order is kept for a grace
        // period so a gate opening or a wall coming down (a new static
        // epoch) resumes it; after that the leg is retired as Retail retires
        // an unreachable goal. Later queued legs proceed. An approach member
        // got here by reaching its nearest reachable spot (its approach
        // point) or by outliving the walk's age limit: its grace,
        // kApproachRetire, is timed from that arrival, never from the order,
        // so a long walk is not cut short. The update that stops the body
        // only stamps the clock; the retire test runs from the next update
        // on (with kApproachRetire = 0 the order drops one update after
        // arrival, and the body is seen standing at its point in between).
        u.speed=Fixed();u.turnReqBam=0;
        if(m.state!=Trapped) {
            m.state=Trapped;m.trappedSince=w.tickCounter_;m.trappedEpoch=epoch;++stats.trapped;
            return;
        }
        if(w.tickCounter_-m.trappedSince<(m.approach?kApproachRetire:kTrappedRetire))return;
        if(m.approach) {approachDone[u.id]=m.point;markWatch(u.id);}
        leave(u.id);
        w.dropLeg(u);
        u.routeStamp=-1;
    }
    // A body that starts on an origin the static plane rejects (inside a
    // yard, or on a cell a structure now covers) leaves it the native way.
    void escape(Unit& u,Fixed maximum,const Order& leg) {
        ++stats.escapes;
        const auto [tx,tz]=target(leg);
        const RetailSteeringPoint start{Fixed::fromInt(u.x.floorInt()),Fixed::fromInt(u.z.floorInt())},end{tx,tz};
        const Bam heading=u.heading;const Fixed x=u.x,z=u.z;
        const auto d=w.steerGround(u,start,end,end,maximum);
        w.commitGroundStep(u,d.s,d.c);
        // A refused escape step is a stop, not a turn in place.
        if(u.x==x&&u.z==z) {u.heading=heading;u.speed=Fixed();u.turnReqBam=0;}
    }
    // Passage lanes: in a strip narrower than kLaneSpan origins across (a
    // door, a bridge), footprint-wide lanes anchored on the low wall pack the
    // most files side by side (a 6-cell door holds three 2-cell lanes, but
    // only two when the files drift off that grid). True when (x,z) is in
    // such a strip on either axis and off its lane grid.
    bool offLane(const Plane& p,int x,int z,int fx,int fz) const {
        auto bounds=[&](int cx,int cz,int dx,int dz,int& lo,int& hi)->bool {
            lo=0;hi=0;
            if(!legal(p,cx,cz))return false;
            while(lo<kLaneSpan&&legal(p,cx-dx*(lo+1),cz-dz*(lo+1)))++lo;
            if(lo>=kLaneSpan)return false;
            while(lo+hi<kLaneSpan&&legal(p,cx+dx*(hi+1),cz+dz*(hi+1)))++hi;
            return lo+hi<kLaneSpan;
        };
        auto across=[&](int dx,int dz,int foot)->bool {
            int lo,hi;
            // A strip only one footprint wide is a single lane.
            if(!bounds(x,z,dx,dz,lo,hi)||lo+hi<foot||lo%foot==0)return false;
            // Only a straight-walled section (the same strip one cell along
            // it, either way) has lanes: on ragged walls the grid shifts
            // every cell and keeping to it only zig-zags bodies into them.
            int l,h;
            const int ax=x+dz,az=z+dx;   // one cell along the strip
            return (bounds(ax,az,dx,dz,l,h)&&l==lo&&h==hi)||
                   (bounds(2*x-ax,2*z-az,dx,dz,l,h)&&l==lo&&h==hi);
        };
        return across(0,1,fz)||across(1,0,fx);
    }
    // Steepest legal descent, measured per unit of path length (an
    // orthogonal drop of 5 and a diagonal drop of 7 are equally steep).
    // Ties -- common on open ground, where a region field is "distance to the
    // nearest goal of anyone" -- go to the neighbour nearest this member's
    // OWN goal, then direction order. Plain first-found tie breaking pulled
    // bodies toward other members' goals and folded formations into a file.
    // Potential strictly decreases, so no step can cycle.
    int descend(const Plane& p,const Field& f,int x,int z,int goal,int fx=0,int fz=0) const {
        const int W=width(),gx=goal%W,gz=goal/W;
        const uint16_t here=f.at(size_t(z)*W+x);
        int best=-1;int64_t bestSlope=0,bestD=0;
        std::array<std::pair<int,int64_t>,8> down{};int count=0;
        for(const auto& d:kDirections) {
            if(!step(p,x,z,d[0],d[1]))continue;
            const int cell=(z+d[1])*W+x+d[0];
            const uint16_t v=f.at(size_t(cell));
            if(v>=here)continue;
            const int64_t slope=int64_t(here-v)*(d[0]&&d[1]?kOrthogonal:kDiagonal);
            const int64_t dx=gx-x-d[0],dz=gz-z-d[1],dd=dx*dx+dz*dz;
            down[size_t(count++)]={cell,slope};
            if(best<0||slope>bestSlope||(slope==bestSlope&&dd<bestD)) {best=cell;bestSlope=slope;bestD=dd;}
        }
        // In a passage, keep to its lane grid: a descending cell on a lane
        // beats a steeper one off it (best stays first among equals).
        if(fx>0&&best>=0&&offLane(p,best%W,best/W,fx,fz)) {
            int lane=-1;int64_t laneSlope=0;
            for(int i=0;i<count;++i) {
                const auto [cell,slope]=down[size_t(i)];
                if(cell==best||(lane>=0&&slope<=laneSlope))continue;
                if(!offLane(p,cell%W,cell/W,fx,fz)) {lane=cell;laneSlope=slope;}
            }
            if(lane>=0)return lane;   // still strictly descending
        }
        return best;
    }
    // Rebuild a group's arrival slots on its current plane and field,
    // around the cells members and arrivals already hold (those claims live
    // in `points`, so a rebuilt slot is never handed out twice).
    static void restaleSlots(Group& g) {
        for(auto& [seed,slot]:g.slots) {slot.built=false;slot.stale=true;slot.cells.clear();slot.taken.clear();}
    }
    Group::Slots& slotsFor(const Member& m,Group& g,const Plane& p,int seed,int count,int fx,int fz) {
        auto& s=g.slots[seed];
        if(s.built)return s;
        s.built=true;
        const Field& f=*g.field;
        const int W=width(),H=height(),sx=seed%W,sz=seed/W;
        const int foot=std::max(fx,fz);
        for(int radius=int(isqrtFloor(uint64_t(count)))*foot+2*foot+4,attempt=0;attempt<4;++attempt,radius*=2) {
            const int x0=std::max(0,sx-radius),z0=std::max(0,sz-radius);
            const int x1=std::min(W-1,sx+radius),z1=std::min(H-1,sz+radius);
            std::vector<std::pair<uint16_t,int>> order;
            stats.slotSearchCells+=uint64_t(x1-x0+1)*uint64_t(z1-z0+1);
            for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x) {
                const int cell=z*W+x;
                if(legal(p,x,z)&&f.at(size_t(cell))!=kUnreached&&(!s.stale||slotFree(m,cell,fx,fz)))
                    order.push_back({f.at(size_t(cell)),cell});
            }
            std::sort(order.begin(),order.end());
            const int bw=x1-x0+1+fx,bh=z1-z0+1+fz;
            std::vector<uint8_t> used(size_t(bw)*bh,0);
            s.cells.clear();
            for(const auto& [potential,cell]:order) {
                const int x=cell%W-x0,z=cell/W-z0;
                bool clear=true;
                for(int j=0;j<fz&&clear;++j)for(int i=0;i<fx&&clear;++i)clear=!used[size_t(z+j)*bw+x+i];
                if(!clear)continue;
                for(int j=0;j<fz;++j)for(int i=0;i<fx;++i)used[size_t(z+j)*bw+x+i]=1;
                s.cells.push_back(cell);s.reach=potential;
                if(int(s.cells.size())>=count)break;
            }
            if(int(s.cells.size())>=count)break;
        }
        s.taken.assign(s.cells.size(),0);
        return s;
    }
    // Members sharing one point claim the innermost free slot once they are
    // close, so arrivals pack from the point outward.
    // Nearest origin to a target centre (px) that this body may take as its
    // slot: statically legal, in the group's component, its footprint clear
    // of every claimed cell of the point, and its centre inside the point's
    // packed area. -1 if none within the search window.
    int formationCell(const Unit& u,const Plane& p,int comp,const Point& pt,int64_t px,int64_t pz,int64_t tx,int64_t tz) const {
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
        const int sx=footprintOrigin(Fixed::fromInt(int32_t(tx)),fx),sz=footprintOrigin(Fixed::fromInt(int32_t(tz)),fz);
        auto test=[&](int dx,int dz,int& best,int64_t& bestD) {
            const int x=sx+dx,z=sz+dz;
            if(!legal(p,x,z)||compAt(p,z*W+x)!=comp)return;
            const int64_t cx=int64_t(x)*16+fx*8,cz=int64_t(z)*16+fz*8;
            if((cx-px)*(cx-px)+(cz-pz)*(cz-pz)>pt.limit*pt.limit)return;
            const int64_t d=(cx-tx)*(cx-tx)+(cz-tz)*(cz-tz);
            if(best>=0&&(d>bestD||(d==bestD&&z*W+x>best)))return;
            bool clear=true;
            for(int j=0;j<fz&&clear;++j)for(int i=0;i<fx&&clear;++i)clear=!pt.cells.count((z+j)*W+x+i);
            if(clear) {best=z*W+x;bestD=d;}
        };
        int best=-1;int64_t bestD=0;
        uint64_t cells=0;
        for(int r=0;r<=48;++r) {
            if(best>=0&&int64_t(r-1)*16*int64_t(r-1)*16>bestD)break;
            // The ring at Chebyshev distance r alone, in the (dz,dx) order of
            // a walk of the whole square: the top row, each middle row's two
            // ends, the bottom row.
            cells+=r?8*uint64_t(r):1;
            for(int dz=-r;dz<=r;++dz) {
                const int stride=dz==-r||dz==r?1:2*r;
                for(int dx=-r;dx<=r;dx+=stride)test(dx,dz,best,bestD);
            }
        }
        stats.formationRingCells+=cells;stats.slotSearchCells+=cells;ringCells+=cells;
#ifndef NDEBUG
        if(gVerify) {
            // The walk of every square cell the ring search replaced (A6).
            int square=-1;int64_t squareD=0;
            for(int r=0;r<=48;++r) {
                if(square>=0&&int64_t(r-1)*16*int64_t(r-1)*16>squareD)break;
                for(int dz=-r;dz<=r;++dz)for(int dx=-r;dx<=r;++dx)
                    if(std::max(std::abs(dx),std::abs(dz))==r)test(dx,dz,square,squareD);
            }
            if(square!=best||squareD!=bestD)verifyFail("formationCell's ring search differs from the square walk");
        }
#endif
        return best;
    }
    bool formationMember(const Member& m) const {
        if(m.pt)return m.pt->assigned&&m.pt->limit>0;
        const auto found=points.find(m.point);
        return found!=points.end()&&found->second.assigned&&found->second.limit>0;
    }
    // Every member of the point gives its formation slot back (its goal is
    // the click again) and the point is assigned afresh.
    void releaseFormation(Point& pt,const std::tuple<int,uint32_t,int32_t,int32_t>& key) {
        for(const int id:pt.ids) {
            Member* mm=member(id);
            const Unit* v=w.unit(id);
            if(!mm||mm->point!=key||!v||!v->type)continue;
            if(mm->slot>=0) {
                slotCells(*mm,v->type->footX,v->type->footZ,false);
                mm->goal=mm->requested;mm->lineCell=-1;markPass(*v,*mm);
            }
            if(mm->slot>=0||mm->slot==-2||mm->slot==-3)mm->slot=-1;
        }
        pt.queue.clear();pt.cells.clear();pt.assigned=false;pt.tried=false;
    }
    void takeFormation(Member& m,const Unit& u,int cell) {
        m.goal=cell;m.slot=0;m.lineCell=-1;markPass(u,m);
        slotCells(m,u.type->footX,u.type->footZ,true);
    }
    // The pinwheel's strength for a member: its 64-unit part's members
    // (C33); the point's refs while the point is a single part, as before
    // A2 merged a click's parts into one point.
    int partRefs(const Member& m) const {
        if(!m.pt)return 0;
        if(m.pt->partRefs.size()<=1)return m.pt->refs;
        const auto found=m.pt->partRefs.find(m.part);
        return found==m.pt->partRefs.end()?0:found->second;
    }
    // Every member sent to one point in one command gets its slot by
    // formation, once its convoy has closed (or at once for a first part
    // under one uplink tick, see formationSlot), kSlotsPerTick a tick: its
    // offset from the members' centroid, scaled so the formation's spread matches the packed disc that many bodies occupy,
    // placed around the point. Members are served front first (farthest
    // along the centroid->point direction), so the front of the crowd takes
    // the far side of the area and nobody has to cross a settled body.
    void assignFormation(Point& pt,const std::tuple<int,uint32_t,int32_t,int32_t>& key) {
        const int64_t px=int64_t(std::get<2>(key))>>16,pz=int64_t(std::get<3>(key))>>16;
        std::vector<std::pair<int,Member*>> list;
        int64_t sumX=0,sumZ=0,area=0,areaGap=0,firstSide=0,minSide=8;bool mixed=false;
        for(const int id:pt.ids) {
            Member& mm=*memberIndex[size_t(id)];
            // Approach members stand in for an unreachable click: they take
            // no slot and do not size the area.
            if(mm.point!=key||mm.goal<0||mm.slot>=0||mm.approach)continue;
            const Unit* v=w.unit(id);
            if(!v||!v->type||groups.find(mm.group)==groups.end())continue;
            list.push_back({id,&mm});
            sumX+=v->x.v>>16;sumZ+=v->z.v>>16;
            const int64_t side=std::max(v->type->footX,v->type->footZ);area+=side*side*256;
            areaGap+=(side+1)*(side+1)*256;
            if(!firstSide)firstSide=side;
            minSide=std::min(minSide,side);
            mixed|=side!=firstSide;
        }
        // Bodies of different sizes never tile: give each a cell of clearance.
        if(mixed)area=areaGap;
        // Assigned only once two members can take part; until then the point
        // is retried (every 16 ticks, a deterministic bound on the scan).
        if(list.size()<2)return;
        pt.assigned=true;
        const int64_t n=int64_t(list.size());
        pt.centreX=sumX/n;pt.centreZ=sumZ/n;
        const int64_t packed=isqrtFloor(uint64_t(area)*10000/31416);
        // The area: the packed disc plus a margin of three of the smallest
        // bodies (greedy formation packing leaves holes inside the disc).
        pt.limit=packed+48*minSide-8;
        int64_t spread=0;
        for(const auto& [id,mm]:list) {
            const Unit* v=w.unit(id);
            const int64_t ox=(v->x.v>>16)-pt.centreX,oz=(v->z.v>>16)-pt.centreZ;
            spread+=ox*ox+oz*oz;
        }
        // RMS radius of a uniform disc of radius a is a/sqrt(2).
        const int64_t rms2=isqrtFloor(uint64_t(2*spread/n));
        if(rms2>packed) {pt.scaleNum=packed;pt.scaleDen=rms2;} else {pt.scaleNum=pt.scaleDen=1;}
        const int64_t ax=px-pt.centreX,az=pz-pt.centreZ;
        std::vector<std::tuple<int64_t,int,Member*>> order;
        for(const auto& [id,mm]:list) {
            const Unit* v=w.unit(id);
            const int64_t ox=(v->x.v>>16)-pt.centreX,oz=(v->z.v>>16)-pt.centreZ;
            order.push_back({-(ox*ax+oz*az),id,mm});
        }
        std::sort(order.begin(),order.end(),[](const auto& a,const auto& b) {
            return std::get<0>(a)!=std::get<0>(b)?std::get<0>(a)<std::get<0>(b):std::get<1>(a)<std::get<1>(b);});
        // At most kSlotsPerTick slots now, front first; the rest queue in
        // that order (slot -3) and are handed out on the next ticks (see
        // handOut), steering by the shared field meanwhile.
        pt.handTick=w.tickCounter_;
        size_t given=0;
        for(const auto& [key2,id,mm]:order) {
            if(given>=kSlotsPerTick) {mm->slot=-3;pt.queue.push_back(id);continue;}
            ++given;
            const Unit* v=w.unit(id);
            const Group& gg=groups.find(mm->group)->second;
            const Plane& pp=plane(gg.plane);
            const int64_t ox=(v->x.v>>16)-pt.centreX,oz=(v->z.v>>16)-pt.centreZ;
            const int cell=formationCell(*v,pp,groupComp(gg,pp),pt,px,pz,px+ox*pt.scaleNum/pt.scaleDen,pz+oz*pt.scaleNum/pt.scaleDen);
            if(cell>=0)takeFormation(*mm,*v,cell);
        }
        slotShape(pt,list,ax,az,px,pz);
    }
    // The next kSlotsPerTick queued members of an assigned point take their
    // formation slots (their current offset, mapped as a later joiner's),
    // once per tick. A member that left, re-registered or died is dropped.
    // A large formation's later joiners search far through claimed cells
    // (a 120-body dead-end room: 8000 ring cells a member, 268k a tick at
    // 32 slots), so the hand-out also stops, after its first slot, once
    // the tick's searches have walked kHandOutCells ring cells; the rest
    // wait a tick, steering by the shared field as before.
    void handOut(Point& pt,const std::tuple<int,uint32_t,int32_t,int32_t>& key) {
        pt.handTick=w.tickCounter_;
        const int64_t px=int64_t(std::get<2>(key))>>16,pz=int64_t(std::get<3>(key))>>16;
        size_t at=0,given=0;
        const uint64_t ring0=ringCells;
        for(;at<pt.queue.size()&&given<kSlotsPerTick&&(!given||ringCells-ring0<kHandOutCells);++at) {
            Member* mm=member(pt.queue[at]);const Unit* v=w.unit(pt.queue[at]);
            if(!mm||mm->point!=key||mm->slot!=-3)continue;
            const auto group=groups.find(mm->group);
            if(!v||!v->type||group==groups.end()) {mm->slot=-1;continue;}
            ++given;
            const Plane& pp=plane(group->second.plane);
            const int64_t ox=(v->x.v>>16)-pt.centreX,oz=(v->z.v>>16)-pt.centreZ;
            const int cell=formationCell(*v,pp,groupComp(group->second,pp),pt,px,pz,px+ox*pt.scaleNum/pt.scaleDen,pz+oz*pt.scaleNum/pt.scaleDen);
            if(cell>=0)takeFormation(*mm,*v,cell);else mm->slot=-2;
        }
        pt.queue.erase(pt.queue.begin(),pt.queue.begin()+std::ptrdiff_t(at));
    }
    // Can the point's command still grow? A click's orders arrive over
    // several ticks (the 64-per-tick uplink); its convoy stays joinable for
    // up to kGapTicks after its last order (sim/convoy.h). Slots wait for it
    // to close, so the formation is sized and ranked over the whole click.
    bool commandOpen(const std::tuple<int,uint32_t,int32_t,int32_t>& key) const {
        return std::get<0>(key)>=0&&w.convoys().joinable(std::get<0>(key),std::get<1>(key),w.tickCounter_);
    }
    // I2 slot-shape probe (observation only, never hashed): the members'
    // spread and their slots' layout in the approach frame (along: centroid
    // -> click; across: its right), and the frontage of the slots nearest
    // the click.
    void slotShape(Point& pt,const std::vector<std::pair<int,Member*>>& list,int64_t ax,int64_t az,int64_t px,int64_t pz) {
        const int W=width();
        int64_t len=isqrtFloor(uint64_t(ax*ax+az*az));
        if(!len) {ax=1;az=0;len=1;}
        SlotShape s;s.valid=true;s.tick=w.tickCounter_;s.members=int(list.size());s.limit=pt.limit;
        uint64_t along2=0,across2=0;
        std::vector<std::tuple<int64_t,int64_t,int64_t>> slots;   // distance^2 to the click, along, across
        for(const auto& [id,mm]:list) {
            const Unit* v=w.unit(id);
            const int64_t ox=(v->x.v>>16)-pt.centreX,oz=(v->z.v>>16)-pt.centreZ;
            const int64_t along=(ox*ax+oz*az)/len,across=(ox*az-oz*ax)/len;
            along2+=uint64_t(along*along);across2+=uint64_t(across*across);
            if(mm->slot<0||mm->goal<0)continue;
            const int64_t cx=int64_t(mm->goal%W)*16+v->type->footX*8-px,cz=int64_t(mm->goal/W)*16+v->type->footZ*8-pz;
            slots.push_back({cx*cx+cz*cz,(cx*ax+cz*az)/len,(cx*az-cz*ax)/len});
        }
        s.rmsAlong=isqrtFloor(along2/uint64_t(list.size()));s.rmsAcross=isqrtFloor(across2/uint64_t(list.size()));
        s.slots=int(slots.size());
        if(!slots.empty()) {
            s.alongMin=s.alongMax=std::get<1>(slots[0]);s.acrossMin=s.acrossMax=std::get<2>(slots[0]);
            for(const auto& [d,along,across]:slots) {
                s.alongMin=std::min(s.alongMin,along);s.alongMax=std::max(s.alongMax,along);
                s.acrossMin=std::min(s.acrossMin,across);s.acrossMax=std::max(s.acrossMax,across);
            }
            std::sort(slots.begin(),slots.end());
            std::set<int64_t> columns;
            for(size_t i=0;i<(slots.size()+3)/4;++i) {
                const int64_t across=std::get<2>(slots[i]);
                columns.insert(across>=0?across/16:-((15-across)/16));
            }
            s.frontage=int(columns.size());
        }
        pt.shape=s;lastShape=s;
    }
    // Shared points: formation slots (assigned once for the whole point; a
    // later joiner maps its own offset the same way). A member walled off
    // from its slot re-chooses one only through the settle rule (rechoose).
    bool formationSlot(const Unit& u,Member& m,const Group& g,const Plane& p) {
        Point* cached=m.pt;
        if(!cached) {auto found=points.find(m.point);cached=found==points.end()?nullptr:&found->second;}
        // Once assigned, the point's formation stays in force for its last
        // member too (refs 1): it can still re-choose a walled-off slot.
        if(!cached||(cached->refs<2&&!cached->assigned))return false;
        auto& pt=*cached;
        // A2 waits for the convoy to close, so a click the uplink splits over
        // several ticks forms one formation. A first part under one uplink
        // tick (kUplinkPart) is the whole click as far as the server can
        // tell: it takes its formation at once, provisionally, and if another
        // part joins the point before the convoy closes the formation is
        // rebuilt over every part then. (Waiting the convoy's 9-plus ticks
        // without slots walked small groups -- the flyer fixtures' 40,
        // doorplug's 60 -- the first stretch by the shared field alone,
        // bunched on the line to the click.)
        if(pt.provisional&&!commandOpen(m.point)) {
            pt.provisional=false;
            if(pt.partRefs.size()>1)releaseFormation(pt,m.point);
        }
        const bool early=!pt.assigned&&pt.partRefs.size()<=1&&pt.refs<kUplinkPart&&commandOpen(m.point);
        if(!pt.assigned&&(!pt.tried||w.tickCounter_%16==0)&&(early||!commandOpen(m.point))) {
            pt.tried=true;assignFormation(pt,m.point);
            pt.provisional=early&&pt.assigned;
        }
        if(!pt.assigned||pt.limit<=0)return true;
        if(!pt.queue.empty()&&pt.handTick!=w.tickCounter_)handOut(pt,m.point);
        if(m.slot==-3)return true;   // queued for its slot: steers by the shared field
        const int64_t px=int64_t(std::get<2>(m.point))>>16,pz=int64_t(std::get<3>(m.point))>>16;
        const int64_t ux=u.x.v>>16,uz=u.z.v>>16;
        // A slot walled off by bodies that settled first, or none left for
        // this member (-2: it walks to the point), is re-chosen only by the
        // settle rule (rechoose), once the body is queued and pressed.
        // A held member re-aims its own lane every 20 held ticks (the
        // bodies round it have moved since it last swept it; restDue wakes
        // a resting body for it). Its slot stays. This is the lane sweep the
        // every-20-held re-choice did on the way, where no area cell is in
        // its reach: without it a held army stops spreading round the
        // bodies in front of it (battle-field 2x500: 454 of A's 500 died
        // instead of 376, and Legion work rose 15%).
        if(m.slot>=0&&laneDue(m))m.lineCell=-1;
        if(m.slot>=0||m.slot==-2)return true;
        const int64_t ox=ux-pt.centreX,oz=uz-pt.centreZ;
        const int cell=formationCell(u,p,groupComp(g,p),pt,px,pz,px+ox*pt.scaleNum/pt.scaleDen,pz+oz*pt.scaleNum/pt.scaleDen);
        if(cell>=0)takeFormation(m,u,cell);else m.slot=-2;
        return true;
    }

    // ---- the reach ring (AR-06 part B) ------------------------------------
    // The centre distance (px) at which tickCombat brakes this attacker in
    // reach of `t` (its selected weapon, or the longest when every weapon
    // fires; a structure target padded by its half extent, as tickCombat
    // pads it; melee at footprint contact), or a guard's 70 px; and whether
    // shooting from there needs a line of sight. 0: no reach (no weapon).
    // The same float expressions as the hashed combat path, read as int px.
    std::pair<int,bool> reachPx(const Unit& u,const Unit* t,bool guard) const {
        if(guard)return {70,false};
        if(!t||!t->type||!u.type||u.type->weapons.empty())return {0,false};
        const int slot=std::clamp(u.weaponSlot,0,int(u.type->weapons.size())-1);
        const bool all=!u.type->weaponSwitching&&u.type->weapons.size()>1;
        const Weapon& sel=u.type->weapons[size_t(slot)];
        if(sel.melee)return {8*(std::max(u.type->footX,u.type->footZ)+std::max(t->type->footX,t->type->footZ))+8,false};
        const float best=all?u.type->maxRange():sel.range;
        const float pad=t->type->maxVel<=Fixed()?8.0f*float(std::max(t->type->footX,t->type->footZ))+24.0f:0.0f;
        const bool los=best>64.0f&&!t->type->canFly&&!u.type->lobs();
        return {int(best+pad),los};
    }
    // Monotonic integer pseudo-angle of (along, across) in [-2^17, 2^17):
    // 0 along +along, counterclockwise positive (a diamond angle, no atan2).
    static int64_t pseudoAngle(int64_t along,int64_t across) {
        constexpr int64_t Q=65536;
        if(!along&&!across)return 0;
        int64_t a;
        if(across>=0)a=along>=0?Q*across/(along+across):Q+Q*(-along)/(-along+across);
        else a=along<0?2*Q+Q*(-across)/(-along-across):3*Q+Q*along/(along-across);
        return a>=2*Q?a-4*Q:a;
    }
    bool ringMember(const Group& g,const Member& m) const {return g.ring.band0>0&&m.slot>=0&&size_t(m.slot)<g.ring.cells.size();}
    void ringRelease(Group& g,int id,const Member& m) {
        if(!ringMember(g,m))return;
        if(g.ring.owner[size_t(m.slot)]==id)g.ring.owner[size_t(m.slot)]=0;
    }
    // Build the ring of a reach group of two or more members (once): band 0
    // becomes the group's seeds. Without a band-0 spot (none in reach and
    // line of sight in the members' component) the group keeps its point
    // seed. Returns whether the seeds changed.
    bool buildRing(Group& g) {
        auto& r=g.ring;
        r.tried=true;
        if(g.approach||g.reachIds.size()<kRingMembers||g.reachCentre<0||g.kind==Kind::Guard)return false;   // a guard's 70 px is a stance round its ward: no ring
        const Unit* t=w.unit(g.reachTarget);
        if(!t||!t->alive()||!t->type)return false;
        int R=INT_MAX;bool los=false,naval=false;
        for(const int id:g.reachIds)if(const Unit* u=w.unit(id);u&&u->type) {
            const auto [px,l]=reachPx(*u,t,g.kind==Kind::Guard);
            if(px<R) {R=px;los=l;naval=u->type->domain==UnitType::Domain::Water||t->type->domain==UnitType::Domain::Water;}
        }
        if(R==INT_MAX||R<=0)return false;
        const Plane& p=planes[size_t(g.plane)];
        const int W=width(),H=height(),fx=p.footX,fz=p.footZ,foot=std::max(fx,fz),body=foot*16;
        const int comp=compAt(p,g.reachCentre);
        if(comp<0)return false;
        const int64_t tx=t->x.v>>16,tz=t->z.v>>16;
        // The approach bearing: from the target to the members' bodies.
        int64_t ax=int64_t(g.bodyMinX+g.bodyMaxX)*8+fx*8-tx,az=int64_t(g.bodyMinZ+g.bodyMaxZ)*8+fz*8-tz;
        if(!ax&&!az)ax=-1;
        const int64_t touch=8*(std::max(t->type->footX,t->type->footZ)+foot);
        const size_t want=std::min(kRingCells,std::max<size_t>(2*g.reachIds.size(),g.reachIds.size()+8));
        const NavGrid& sight=naval?w.navalSight_:w.nav_;   // as combatLineOfSight
        std::vector<int> cells;std::vector<uint8_t> band;
        std::set<int> used;
        uint64_t scanned=0;
        for(int b=0;b<8&&cells.size()<want;++b) {
            // Band 0 lies half a body to a body and a half inside reach
            // (combat brakes a body the moment it is in reach, so bodies on
            // their way to it stop round the reach edge, leaving room
            // between them for the ones behind); band b > 0 waits b bodies
            // farther out.
            const int64_t lo=std::max<int64_t>(touch,int64_t(R)-3*body/2+int64_t(b)*body),hi=int64_t(R)-body/2+int64_t(b)*body;
            if(hi<=lo)continue;
            const int span=int(hi/16)+foot+1;
            const int cx=int(tx/16),cz=int(tz/16);
            std::vector<std::pair<int64_t,int>> order;
            for(int z=std::max(0,cz-span);z<=std::min(H-1,cz+span);++z)for(int x=std::max(0,cx-span);x<=std::min(W-1,cx+span);++x) {
                ++scanned;
                const int64_t dx=int64_t(x)*16+fx*8-tx,dz=int64_t(z)*16+fz*8-tz,d2=dx*dx+dz*dz;
                if(d2<lo*lo||d2>=hi*hi||!legal(p,x,z)||compAt(p,z*W+x)!=comp)continue;
                if(b==0&&los&&!sight.losBetween(float(x*16+fx*8),float(z*16+fz*8),t->x.toFloat(),t->z.toFloat(),foot/2,
                                                std::max(t->type->footX,t->type->footZ)/2))continue;
                order.push_back({pseudoAngle(dx*ax+dz*az,dx*az-dz*ax),z*W+x});
            }
            std::sort(order.begin(),order.end());
            for(const auto& [angle,cell]:order) {
                if(cells.size()>=kRingCells)break;
                const int x=cell%W,z=cell/W;
                bool clear=true;
                for(int j=0;j<fz&&clear;++j)for(int i=0;i<fx&&clear;++i)clear=!used.count((z+j)*W+x+i);
                if(!clear)continue;
                for(int j=0;j<fz;++j)for(int i=0;i<fx;++i)used.insert((z+j)*W+x+i);
                cells.push_back(cell);band.push_back(uint8_t(b));
            }
            if(b==0&&cells.empty())break;
        }
        stats.slotSearchCells+=scanned;
        int band0=0;for(const uint8_t b:band)band0+=b==0;
        if(!band0)return false;
        r.R=R;r.band0=band0;r.tx=tx;r.tz=tz;r.ax=ax;r.az=az;
        r.cells=std::move(cells);r.band=std::move(band);r.owner.assign(r.cells.size(),0);
        stats.reachSlotsBuilt+=r.cells.size();
        g.seeds.assign(r.cells.begin(),r.cells.begin()+band0);
        std::sort(g.seeds.begin(),g.seeds.end());
        for(const int c:g.seeds) {
            g.minX=std::min(g.minX,c%W);g.maxX=std::max(g.maxX,c%W);g.minZ=std::min(g.minZ,c/W);g.maxZ=std::max(g.maxZ,c/W);
        }
        return true;
    }
    // A member's angle round the target, in the ring's approach frame.
    int64_t ringAngle(const Group& g,const Unit& u) const {
        const int64_t dx=(u.x.v>>16)-g.ring.tx,dz=(u.z.v>>16)-g.ring.tz;
        return pseudoAngle(dx*g.ring.ax+dz*g.ring.az,dx*g.ring.az-dz*g.ring.ax);
    }
    void ringTake(Group& g,const Unit& u,Member& m,int slot) {
        g.ring.owner[size_t(slot)]=u.id;
        m.slot=slot;m.goal=g.ring.cells[size_t(slot)];m.lineCell=-1;markPass(u,m);
        slotCells(m,u.type->footX,u.type->footZ,true);
    }
    bool ringCovered(const Group& g,const Plane& p,int slot,int self) const {
        const int c=g.ring.cells[size_t(slot)],W=width();
        for(int j=0;j<p.footZ;++j)for(int i=0;i<p.footX;++i) {
            const int cx=c%W+i,cz=c/W+j;
            if(cx>=w.occW_||cz>=w.occH_)continue;
            const int32_t o=occAt(size_t(cz)*w.occW_+cx);
            if(o&&o!=self)return true;
        }
        return false;
    }
    // A spot nobody holds, statically legal and reached by the field, and
    // (for a re-choice) not covered by a body standing there now (an
    // engaged one that let its own spot go).
    bool ringFree(const Group& g,const Plane& p,int slot,int self=0) const {
        const int c=g.ring.cells[size_t(slot)],W=width();
        if(g.ring.owner[size_t(slot)]||!legal(p,c%W,c/W)||!g.field||g.field->at(size_t(c))==kUnreached)return false;
        return !self||!ringCovered(g,p,slot,self);
    }
    // The first claim hands every member a spot at once: the members nearest
    // the target take band 0, the next band 1 and so on; within a band the
    // spots nearest the approach bearing are used, and members and spots
    // are matched in angular order, so lanes to the spots do not cross.
    void assignRing(Group& g,const Plane& p) {
        auto& r=g.ring;
        r.assigned=true;
        std::vector<std::tuple<int64_t,int,const Unit*,Member*>> list;   // distance^2, id
        for(const int id:g.reachIds) {
            Member* mm=member(id);const Unit* v=w.unit(id);
            if(!mm||!v||!v->type||mm->group!=g.id||mm->slot>=0||mm->state==Engaged)continue;
            const int64_t dx=(v->x.v>>16)-r.tx,dz=(v->z.v>>16)-r.tz;
            list.push_back({dx*dx+dz*dz,id,v,mm});
        }
        std::sort(list.begin(),list.end(),[](const auto& a,const auto& b) {return std::tie(std::get<0>(a),std::get<1>(a))<std::tie(std::get<0>(b),std::get<1>(b));});
        size_t next=0;
        for(int b=0;b<8&&next<list.size();++b) {
            std::vector<std::pair<int64_t,int>> spots;   // |angle|, slot
            for(size_t i=0;i<r.cells.size();++i)if(r.band[i]==b&&ringFree(g,p,int(i))) {
                const int64_t dx=int64_t(r.cells[i]%width())*16+p.footX*8-r.tx,dz=int64_t(r.cells[i]/width())*16+p.footZ*8-r.tz;
                spots.push_back({std::abs(pseudoAngle(dx*r.ax+dz*r.az,dx*r.az-dz*r.ax)),int(i)});
            }
            std::sort(spots.begin(),spots.end());
            const size_t n=std::min(spots.size(),list.size()-next);
            if(!n)continue;
            std::vector<int> chosen;for(size_t i=0;i<n;++i)chosen.push_back(spots[i].second);
            std::sort(chosen.begin(),chosen.end());   // band order is pseudo-angle order
            std::vector<std::pair<int64_t,size_t>> who;
            for(size_t i=next;i<next+n;++i)who.push_back({ringAngle(g,*std::get<2>(list[i])),i});
            std::sort(who.begin(),who.end());
            for(size_t i=0;i<n;++i) {
                const auto& [d,id,v,mm]=list[who[i].second];
                ringTake(g,*v,*mm,chosen[i]);
            }
            next+=n;
        }
    }
    // A reach member's spot: the bulk assignment on the group's first claim,
    // else (a late joiner, or one whose spot was let go) the free spot of
    // the lowest band nearest to it.
    void ringClaim(const Unit& u,Member& m,Group& g,const Plane& p) {
        if(m.approach)return;
        // (A group that grows past one member after its first field keeps its
        // point seed: re-planning it to a ring then cost the battles a
        // refresh per target, measured +13-28% total Legion work.)
        if(g.ring.band0==0||m.slot>=0||m.state==Engaged)return;
        if(!g.field||!g.field->done||g.field->seedKey!=seedsKey(g.seeds))return;
        if(!g.ring.assigned) {assignRing(g,p);return;}
        ringNearest(u,m,g,p,255);
    }
    // The nearest free spot in a lower band, or in the same band nearer this
    // body than its own (any, if another body now stands on its own: an
    // engaged one stopped there); ties: the lower index.
    bool ringRechoose(const Unit& u,Member& m,Group& g,const Plane& p) {
        const int W=width();
        const int64_t ux=u.x.v>>16,uz=u.z.v>>16;
        auto dist=[&](size_t i) {
            const int c=g.ring.cells[i];
            const int64_t dx=int64_t(c%W)*16+u.type->footX*8-ux,dz=int64_t(c/W)*16+u.type->footZ*8-uz;
            return dx*dx+dz*dz;
        };
        const int curBand=g.ring.band[size_t(m.slot)];
        const int64_t curD=ringCovered(g,p,m.slot,u.id)?INT64_MAX:dist(size_t(m.slot));
        int best=-1;int bestBand=0;int64_t bestD=0;
        for(size_t i=0;i<g.ring.cells.size();++i) {
            if(g.ring.band[i]>curBand||!ringFree(g,p,int(i),u.id))continue;
            const int64_t d=dist(i);
            if(g.ring.band[i]==curBand&&d>=curD)continue;
            if(best<0||g.ring.band[i]<bestBand||(g.ring.band[i]==bestBand&&d<bestD)) {best=int(i);bestBand=g.ring.band[i];bestD=d;}
        }
        if(best<0)return false;
        ringRelease(g,u.id,m);slotCells(m,u.type->footX,u.type->footZ,false);m.slot=-1;
        ringTake(g,u,m,best);
        return true;
    }
    bool ringNearest(const Unit& u,Member& m,Group& g,const Plane& p,int below) {
        const int W=width();
        const int64_t ux=u.x.v>>16,uz=u.z.v>>16;
        int best=-1;int bestBand=0;int64_t bestD=0;
        for(size_t i=0;i<g.ring.cells.size();++i) {
            if(g.ring.band[i]>=below||!ringFree(g,p,int(i),u.id))continue;
            const int c=g.ring.cells[i];
            const int64_t dx=int64_t(c%W)*16+u.type->footX*8-ux,dz=int64_t(c/W)*16+u.type->footZ*8-uz,d=dx*dx+dz*dz;
            if(best<0||g.ring.band[i]<bestBand||(g.ring.band[i]==bestBand&&d<bestD)) {best=int(i);bestBand=g.ring.band[i];bestD=d;}
        }
        if(best<0)return false;
        if(m.slot>=0) {ringRelease(g,u.id,m);slotCells(m,u.type->footX,u.type->footZ,false);m.slot=-1;}
        ringTake(g,u,m,best);
        return true;
    }
    void claimSlot(const Unit& u,Member& m,Group& g,const Plane& p,int here) {
        if(m.requested<0)return;
        if(!g.reachIds.empty()) {ringClaim(u,m,g,p);return;}
        // An approach point is a stand-in, not a destination area: bodies
        // queue up to it in the order they come (keeping their formation,
        // so the deepest goals lead when the way opens) and hold on contact.
        if(m.approach)return;
        // Kinds without a destination area (patrol laps, chases) never claim
        // slots: a chaser's goal is a unit, ended by its owner, not a spot.
        if(!policy(m.kind).area)return;
        if(formationSlot(u,m,g,p))return;
        // A claimed slot walled off by bodies that settled first is
        // re-chosen only by the settle rule (rechoose).
        if(m.slot>=0)return;
        const auto sharing=g.sharing.find(m.requested);
        if(sharing==g.sharing.end()||sharing->second<2)return;
        auto& s=slotsFor(m,g,p,m.requested,sharing->second,u.type->footX,u.type->footZ);
        const uint16_t potential=g.field->at(size_t(here));
        const int foot=std::max(u.type->footX,u.type->footZ);
        if(potential==kUnreached||potential>uint32_t(s.reach)+uint32_t(12*kOrthogonal*foot))return;
        const int W=width(),ux=here%W,uz=here/W;
        // Back-to-front: claim the free slot farthest along this member's
        // own approach direction (ties: nearest the approach axis). Every
        // later arrival then finds the cells between it and its slot still
        // empty, so nobody has to cross a settled body.
        const int seedX=m.requested%W,seedZ=m.requested/W;
        const int64_t ax=seedX-ux,az=seedZ-uz;
        int best=-1;int64_t bestScore=0,bestSide=0;
        for(size_t i=0;i<s.cells.size();++i) {
            if(s.taken[i]||!slotFree(m,s.cells[i],u.type->footX,u.type->footZ))continue;
            // A slot a corpse, feature or structure now covers (or cut off)
            // is never claimed: claiming it would re-register this member
            // and claim it again, forever.
            if(!legal(p,s.cells[i]%W,s.cells[i]/W)||g.field->at(size_t(s.cells[i]))==kUnreached)continue;
            const int64_t cx=s.cells[i]%W,cz=s.cells[i]/W;
            const int64_t score=(cx-seedX)*ax+(cz-seedZ)*az;
            const int64_t side=std::abs((cx-seedX)*az-(cz-seedZ)*ax);
            if(best<0||score>bestScore||(score==bestScore&&side<bestSide)) {best=int(i);bestScore=score;bestSide=side;}
        }
        if(best<0)return;
        s.taken[size_t(best)]=1;m.slot=best;m.goal=s.cells[size_t(best)];m.lineCell=-1;markPass(u,m);
        slotCells(m,u.type->footX,u.type->footZ,true);
    }
    // An approach member's stand-in still holds after a static change: the
    // body and its approach point share a region, the point is still legal,
    // and the real goal is still legal and outside that region.
    bool approachValid(const Unit& u,const Member& m) {
        auto group=groups.find(m.group);
        if(group==groups.end()||m.goal<0||m.real<0)return false;
        const auto& p=plane(group->second.plane);
        const int W=width(),ox=footprintOrigin(u.x,u.type->footX),oz=footprintOrigin(u.z,u.type->footZ);
        if(!legal(p,ox,oz))return false;
        const int reach=compAt(p,oz*W+ox);
        return reach>=0&&compAt(p,m.goal)==reach&&compAt(p,m.real)>=0&&compAt(p,m.real)!=reach;
    }

    // ---- re-seeding in place (AR-07) ------------------------------------
    // A moving goal's group follows its target: once the target's origin
    // has left the group's seed by max(kReseedCells, min(D/8, 8)) cells (D:
    // this member's Chebyshev distance to it), the group re-seeds in place:
    // its seeds (or its ring, translated) move, a refresh builds the new
    // field (the old one steers meanwhile; demand makes the group active),
    // and every member is reseated as it next moves -- its group, slot,
    // hold and stall clocks, detour back-off, pass state and approach clock
    // carried (C8), its route and progress reset. Returns false when the
    // member must register afresh (its group is not a reach group, or the
    // target is gone).
    bool reseed(Unit& u,Member& m,const Order& leg) {
        const auto [tx,tz]=target(leg);
        const int gx=footprintOrigin(tx,u.type->footX),gz=footprintOrigin(tz,u.type->footZ);
        auto group=groups.find(m.group);
        // Not a reach group (an approach member's, or one still waiting to
        // join): a fresh registration, as before, once the target has left
        // the seeded origin by kReseedCells.
        if(group==groups.end()||!group->second.reachTarget||m.approach) {
            if(std::max(std::abs(gx-m.seedX),std::abs(gz-m.seedZ))>=kReseedCells)return false;
            m.seededAt=w.tickCounter_;return true;
        }
        Group& g=group->second;
        const int W=width();
        const auto& p=plane(g.plane);
        // Measured on the target's own origin, as the seed was taken (a
        // structure's lies inside its yard).
        const int ox=footprintOrigin(u.x,u.type->footX),oz=footprintOrigin(u.z,u.type->footZ);
        const int D=std::max(std::abs(gx-ox),std::abs(gz-oz));
        const int moved=std::max(std::abs(gx-m.seedX),std::abs(gz-m.seedZ));
        if(moved<std::max(kReseedCells,std::min(D/8,8))) {m.seededAt=w.tickCounter_;return true;}
        const int region=legal(p,ox,oz)?compAt(p,oz*W+ox):-1;
        const int centre=nearestLegal(p,gx,gz,region);
        if(centre<0||compAt(p,centre)!=compAt(p,g.reachCentre)||(region>=0&&compAt(p,centre)!=region))return false;
        if(g.reachCentre!=centre) {
            const int cx=g.reachCentre%W,cz=g.reachCentre/W;
            if(std::max(std::abs(centre%W-cx),std::abs(centre/W-cz))>=std::max(kReseedCells,std::min(D/8,8)))
                reseedGroup(g,p,centre,centre%W-cx,centre/W-cz);
        }
        if(m.requested!=g.reachCentre)reseat(u,m,g,p);
        m.seedX=gx;m.seedZ=gz;m.seededAt=w.tickCounter_;
        return true;
    }
    void reseedGroup(Group& g,const Plane& p,int centre,int dx,int dz) {
        const int W=width(),H=height();
        ++stats.reseedsInPlace;
        g.reachCentre=centre;
        auto& r=g.ring;
        if(r.band0>0) {
            const int comp=compAt(p,centre);
            int band0=0;
            for(size_t i=0;i<r.cells.size();++i) {
                const int x=r.cells[i]%W+dx,z=r.cells[i]/W+dz;
                // A spot that leaves the map keeps its index, unclaimable.
                r.cells[i]=x>=0&&z>=0&&x<W&&z<H?z*W+x:r.cells[i];
                if(r.band[i]==0&&legal(p,x,z)&&compAt(p,z*W+x)==comp)++band0;
            }
            r.tx+=int64_t(dx)*16;r.tz+=int64_t(dz)*16;
            g.seeds.clear();
            for(size_t i=0;i<r.cells.size();++i)
                if(r.band[i]==0&&legal(p,r.cells[i]%W,r.cells[i]/W)&&compAt(p,r.cells[i])==comp)g.seeds.push_back(r.cells[i]);
            if(g.seeds.empty())g.seeds.push_back(centre);
            std::sort(g.seeds.begin(),g.seeds.end());
            g.seeds.erase(std::unique(g.seeds.begin(),g.seeds.end()),g.seeds.end());
        } else g.seeds.assign(1,centre);
        g.minX=g.maxX=centre%W;g.minZ=g.maxZ=centre/W;
        for(const int c:g.seeds) {g.minX=std::min(g.minX,c%W);g.maxX=std::max(g.maxX,c%W);g.minZ=std::min(g.minZ,c/W);g.maxZ=std::max(g.maxZ,c/W);}
        int n=0;for(const auto& [seed,count]:g.sharing)n+=count;
        int top=0;for(const auto& [seed,count]:g.peak)top=std::max(top,count);
        g.sharing.clear();g.peak.clear();
        if(n)g.sharing[centre]=n;
        g.peak[centre]=std::max(top,n);
        g.compCell=centre;
        if(g.next) {if(!g.next->done)++stats.refreshDiscards;g.next.reset();}
        g.stale=true;g.demand=true;g.areaBounds.clear();
        listGroup(g);
    }
    // The member follows its group's re-seed: new goal (its ring spot,
    // translated with the ring, or the new seed), its timers kept.
    void reseat(const Unit& u,Member& m,Group& g,const Plane& p) {
        const int W=width();
        if(m.slot>=0)slotCells(m,u.type->footX,u.type->footZ,false);
        m.requested=g.reachCentre;
        if(ringMember(g,m)&&g.ring.owner[size_t(m.slot)]==u.id) {
            const int c=g.ring.cells[size_t(m.slot)];
            if(legal(p,c%W,c/W)&&compAt(p,c)==compAt(p,g.reachCentre)) {m.goal=c;slotCells(m,u.type->footX,u.type->footZ,true);}
            else {ringRelease(g,u.id,m);m.slot=-1;m.goal=m.requested;}
        } else {m.slot=-1;m.goal=m.requested;}
        m.lineCell=-1;m.line=false;m.route.clear();m.routeTicks=0;
        m.progress=0xffffffffu;m.rechoices=0;m.pivotCell=-1;m.pivotAim=-1;
        markPass(u,m);
    }
    // An illegal goal (AR-07): a slot member gives its slot back and claims
    // another (claimSlot); any other member takes the nearest legal origin
    // of its own region round the goal, in its group. False: none left.
    bool regoal(const Unit& u,Member& m,Group& g,const Plane& p,int ox,int oz) {
        const int W=width();
        const int region=compAt(p,oz*W+ox);
        if(region<0)return false;
        if(m.slot>=0) {
            slotCells(m,u.type->footX,u.type->footZ,false);
            if(ringMember(g,m))ringRelease(g,u.id,m);
            else if(const auto slots=g.slots.find(m.requested);slots!=g.slots.end()&&size_t(m.slot)<slots->second.taken.size())
                slots->second.taken[size_t(m.slot)]=0;
            m.slot=-1;m.goal=m.requested;m.lineCell=-1;
            if(legal(p,m.goal%W,m.goal/W)) {markPass(u,m);return true;}
        }
        const int near=nearestLegal(p,m.goal%W,m.goal/W,region);
        if(near<0||compAt(p,near)!=region)return false;
        m.goal=near;m.lineCell=-1;m.line=false;m.route.clear();m.progress=0xffffffffu;
        markPass(u,m);
        return true;
    }
    // tickCombat braked this unit in reach (or within 70 px of its guard
    // target): its move() does not run this tick. Its member is Engaged until
    // its next update (AR-06 part A). Called from the unit's own combat
    // update in the serial unit loop, so every peer moved later this tick
    // reads it, as in any run.
    void engaged(const Unit& u) {
        if(!supports(u))return;
        ++stats.engagedNow;
        Member* m=member(u.id);
        if(!m)return;
        // The claim follows the engaged body: a ring spot it stopped short of
        // is let go for the members still on their way (AR-06 B).
        if(m->slot>=0)if(const auto g=groups.find(m->group);g!=groups.end()&&ringMember(g->second,*m)) {
            const int here=footprintOrigin(u.z,u.type->footZ)*width()+footprintOrigin(u.x,u.type->footX);
            if(here!=m->goal) {
                ringRelease(g->second,u.id,*m);slotCells(*m,u.type->footX,u.type->footZ,false);
                m->slot=-1;m->goal=m->requested;m->lineCell=-1;markPass(u,*m);
            }
        }
        m->state=Engaged;
    }
    // The member whose contact arrival this update already refused.
    const Member* contactRefused=nullptr;
    // The member this update steers on its pinwheel arc, and the arc cell it
    // aims at: blocked, it flows round toward that cell, not down the field
    // (the field's lower cells lie inward, back toward the wall end).
    const Member* pivoting=nullptr;int pivotTarget=-1;
    // Occupants of every origin cell touching the body's footprint (its
    // own cells excluded): a blocker leaving or a new body arriving changes it.
    uint64_t ringSignature(const Unit& u) const {
        const int fx=u.type->footX,fz=u.type->footZ;
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        uint64_t h=0x72696e67;
        for(int z=oz-1;z<=oz+fz;++z)for(int x=ox-1;x<=ox+fx;++x) {
            if(x>=ox&&x<ox+fx&&z>=oz&&z<oz+fz)continue;
            if(x<0||z<0||x>=w.occW_||z>=w.occH_)continue;
            h=mix(h,uint64_t(uint32_t(occAt(size_t(z)*w.occW_+x))));
        }
        return h;
    }
    // A long-held body (kRestAfter ticks without a step, no committed
    // detour or route) skips its full update on all but one tick in
    // kRestStride (staggered by id), as long as nothing it reacts to has
    // changed since its last full update: it was not pushed or hurt, the
    // static epoch is the same and no body arrived at or left any cell
    // around it. Any change wakes it at once. A CPU throttle only: the hold
    // and stall clocks are tick stamps, so resting never slows them.
    //
    // A skipped update stands for the hold the full update would have
    // reached with nothing changed (it counts toward holdUpdates as that
    // hold would), and the body is woken on every tick where a clock it
    // reads crosses a threshold: its crowd window (the settle rule and its
    // slot re-choice), contactArrival's stall threshold, the hold-update back-offs, an
    // approach look and the end of a lane pass. So the rest stride changes
    // only when a body sees changes outside its ring, never when its own
    // timers fire (AR-05: the stride used to shift every timed decision of
    // a rested body by up to stride-1 ticks, and the shifts compounded).
    bool heldRest(const Unit& u,Member& m) {
        if(m.state!=Holding||heldFor(m)<kRestAfter||m.detour>=0||!m.route.empty()||w.occW_<=0) {m.rest=false;return false;}
        const uint64_t ring=ringSignature(u);
        ++stats.heldRechecks;
        const bool same=m.rest&&m.restX==u.x.v&&m.restZ==u.z.v&&m.restHp==u.hp.v&&m.restEpoch==epoch&&m.restRing==ring;
        if(same&&(uint32_t(u.id)+w.tickCounter_)%gRestStride!=0&&!restDue(u,m)) {++m.holdUpdates;return true;}
        m.rest=true;m.restX=u.x.v;m.restZ=u.z.v;m.restHp=u.hp.v;m.restEpoch=epoch;m.restRing=ring;
        return false;
    }
    // A held formation member re-sweeps its lane on these ticks (see
    // formationSlot).
    bool laneDue(const Member& m) const {
        if(m.state!=Holding||m.slot<0)return false;
        const uint32_t held=heldFor(m);
        return held>=20&&held%20==0;
    }
    // Does a timed reader of this held body decide on this tick? (See heldRest.)
    bool restDue(const Unit& u,const Member& m) const {
        const uint32_t now=w.tickCounter_,stall=stalledFor(m),n=m.holdUpdates;
        if(now-m.windowTick>=kCrowdWindow||stall==20||laneDue(m))return true;
        if(n==6||n==12||n==60||n==m.nextDetour)return true;
        if(m.approach&&(uint32_t(u.id)+now)%kApproachLook==0)return true;
        return m.passUntil==now;
    }
    // A member holding to give way to crossing traffic waits on purpose and
    // is never "blocked" for B1's re-request (C16). Always false until W7
    // adds the give-way hold; every blocked reader tests it.
    static bool giveWayHolder(const Member&) {return false;}
    void move(Unit& u,Fixed maximum) {
        contactRefused=nullptr;pivoting=nullptr;
        Member* found=member(u.id);
        const auto& leg=u.orders[World::currentLeg(u.orders)];
        const Kind kind=kindOf(u);
        const Policy rule=policy(kind);
        const uint64_t key=legKey(leg,kind);
        if(!rule.moving&&!rule.keyed&&((rule.transport?leg.transportMission:leg.mission).pending&0x500||!leg.controller))
            {w.brakeGround(u);return;}
        // A moving goal re-seeds on the shared tick grid once its unit has
        // left the seeded origin (bounded, deterministic cadence): the whole
        // group in place (AR-07, see reseed), its members keeping their
        // timers; a member of a group already re-seeded is reseated.
        if(found&&rule.moving&&found->controller==key&&found->kind==kind&&
           w.tickCounter_/kReseedTicks!=found->seededAt/kReseedTicks&&!reseed(u,*found,leg)) {
            registerMove(u);found=member(u.id);
            if(!found) {w.brakeGround(u);return;}
        }
        // A new controller, or terrain that changed since this body was
        // found trapped, means a fresh registration (new goal resolution).
        // An approach member re-resolves only when its own reachability
        // changed (see approachValid), not on every unrelated static change:
        // re-registering reset its walk, detours and timers each time.
        if(found&&found->controller==key&&found->approach&&
           (found->approachEpoch!=epoch||(found->state==Trapped&&found->trappedEpoch!=epoch))&&
           approachValid(u,*found)) {
            found->approachEpoch=epoch;
            if(found->state==Trapped)found->trappedEpoch=epoch;
        }
        if(!found||found->controller!=key||found->kind!=kind||
           (found->state==Trapped&&found->trappedEpoch!=epoch)||
           (found->approach&&found->approachEpoch!=epoch)) {
            registerMove(u);found=member(u.id);
            if(!found) {w.brakeGround(u);return;}
        }
        auto& m=*found;m.movedTick=w.tickCounter_;
        ++stats.moveCallsByState[size_t(m.state)&7];
        // Out of the owner's brake again (the target left reach or died): no
        // soft obstacle any more, at once (as a body setting off).
        if(m.state==Engaged)unstamp(u.id);
        if(heldRest(u,m)) {u.speed=Fixed();u.turnReqBam=0;return;}
        ++stats.moves;
        // The native goal predicate (the circle the native mover would test)
        // also ends the leg, with the same event.
        if(rule.nativeAccept&&!m.approach&&(rule.exact?accepts(u,leg,kind):World::groundMissionAccepts(u,leg))) {complete(u,m,false);return;}
        // A production exit without headway and with its birthplace already
        // clear is done: the point was generated, and pressing on would
        // block the factory lane behind it. Still on its birthplace, a body
        // with a rally behind the exit hands over to it after a second
        // window: it walks off the lane with the rally instead of holding
        // there against the crowd.
        // (Still ticks are those the last window proved: windowTick - stillSince.)
        const uint32_t stillTicks=m.stillSince==kNoTick?0:m.windowTick-m.stillSince;
        if(rule.exitClear&&stillTicks>=kExitWindows*kCrowdWindow&&leg.productionExit) {
            const int64_t dx=int64_t(u.x.floorInt())-leg.productionExit->first.floorInt();
            const int64_t dz=int64_t(u.z.floorInt())-leg.productionExit->second.floorInt();
            const bool clear=std::abs(dx)>=u.type->footX*16||std::abs(dz)>=u.type->footZ*16;
            const bool rally=World::currentLeg(u.orders)+1<u.orders.size();
            if(clear||(rally&&stillTicks>=2*kExitWindows*kCrowdWindow)) {complete(u,m,true);return;}
        }
        if(m.goal<0) {trapped(u,m);return;}
        // Stopped at its approach point (retired by trapped), or still
        // walking to it kTrappedRetire after the order (the walk's age limit).
        if(m.approach&&(m.state==Trapped||w.tickCounter_-m.approachSince>=kTrappedRetire)) {trapped(u,m);return;}
        auto group=groups.find(m.group);
        if(group==groups.end()) {registerMove(u);w.brakeGround(u);return;}
        auto& g=group->second;g.lastUse=w.tickCounter_;
        if(m.state==Moving)g.movingTick=w.tickCounter_;
        const auto& p=plane(g.plane);
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        if(!legal(p,ox,oz)) {escape(u,maximum,leg);return;}
        // A goal origin can stop being legal (a building went up on it): a
        // slot member gives its slot back and claims another; any other
        // member takes the nearest legal origin of its region, in its group
        // (AR-07: no new group or field); a fresh registration only when
        // none is left in the region.
        if(!legal(p,m.goal%W,m.goal/W)&&!regoal(u,m,g,p,ox,oz)) {registerMove(u);w.brakeGround(u);return;}
        const int here=oz*W+ox;
        const Field* f=g.field&&g.field->done?g.field.get():nullptr;
        if(f&&(m.state==Moving||m.state==Holding)) {
            // MV-05's probe of the group (see softBlocks).
            const uint32_t cycle=w.tickCounter_/kStillScan;
            if(g.scanCycle!=cycle) {g.scanCycle=cycle;g.scanSumX=g.scanSumZ=0;g.scanCount=0;g.scanTail=-1;g.scanTailPot=0;}
            g.scanSumX+=ox;g.scanSumZ+=oz;++g.scanCount;
            if(const uint16_t v=f->at(size_t(here));v!=kUnreached&&(g.scanTail<0||v>g.scanTailPot)) {g.scanTail=here;g.scanTailPot=v;}
        }
        if(f)claimSlot(u,m,g,p,here);
        // A first field still building already steers a body its frontier
        // has passed (see Field::settled): no standing still for the rest
        // of the build. Slots wait for the finished field.
        else if(g.field&&g.field->settled(size_t(here),g.field->at(size_t(here))))f=g.field.get();
        // A bounded field that cannot reach this body (it walked or joined
        // outside the window, or the way out leaves it): widen the group's
        // field to the whole component and wait for it, never trapped. It
        // restarts as a first field (members already passed by its frontier
        // steer by it half built), not as a refresh: a refresh would keep
        // steering this body by the old field, which cannot reach it. (A
        // refresh does finish under constant static churn, about every 85
        // ticks since the quarter-quota allowance, kRefreshQuota.)
        if(g.field&&g.field->bounded&&!g.full&&(!g.field->inside(ox,oz)||(g.field->done&&g.field->at(size_t(here))==kUnreached))) {
            if(g.next&&!g.next->done)++stats.refreshDiscards;
            g.full=true;g.field.reset();g.next.reset();g.stale=false;g.demand=false;restaleSlots(g);listGroup(g);
            m.state=Waiting;++stats.waitingMemberTicks;w.brakeGround(u);return;
        }
        // The holder rule (approachSettled): checked before routes and
        // side-steps, which a body held behind its own army otherwise walks
        // for ever without gaining. Gain is measured here, every update, as
        // the progress below measures it, so a route that gains keeps the
        // body going. Only a held body, or one walking a way round still
        // bodies, looks for its stopped selection: a free walk (which in a
        // maze can gain nothing for a long stretch) never does. The look is
        // staggered by unit id over kApproachLook ticks (the ring walk is
        // the cost; a held crowd would otherwise pay it every tick).
        // Gain is one measure, the field potential: where the field does not
        // give one (not built yet, or this cell outside it) the window starts
        // over, so switching between two scales can neither fake a gain nor
        // hide one (it mixed potential x64 with squared cells).
        if(m.approach) {
            const uint16_t left=f?f->at(size_t(here)):kUnreached;
            if(left==kUnreached) {m.gainBest=0xffffffffu;m.gainTick=w.tickCounter_;}
            else if(left<m.gainBest) {m.gainBest=left;m.gainTick=w.tickCounter_;}
            else if(w.tickCounter_-m.gainTick>=kApproachSettle&&(m.state==Holding||!m.route.empty()||m.detour>=0)&&
                    (uint32_t(u.id)+w.tickCounter_)%kApproachLook==0&&approachSettled(u,m)) {complete(u,m,true);return;}
        }
        // Close enough: a body that gained less than half a body on its
        // point over the last window, queued back to the settled crowd
        // already standing there and pressed against it, settles where it
        // is (see settleWindow). Checked before detours and shuffles, which
        // otherwise run forever.
        if(w.tickCounter_-m.windowTick>=kCrowdWindow) {
            const int64_t dx=(int64_t(u.x.v)-std::get<2>(m.point))>>16,dz=(int64_t(u.z.v)-std::get<3>(m.point))>>16;
            const int64_t dist=isqrtFloor(uint64_t(dx*dx+dz*dz));
            const int64_t body=int64_t(std::max(fx,fz))*16;
            const bool still=m.windowDist>=0&&m.windowDist-dist<body/2;
            if(!still)m.stillSince=kNoTick;
            else if(m.stillSince==kNoTick)m.stillSince=m.windowTick;
            m.windowTick=w.tickCounter_;m.windowDist=dist;
            if(!still)m.queued=false;
            else if(settleWindow(u,m,g,p,ox,oz,dist)) {complete(u,m,true);return;}
            // A reach member held a whole window re-chooses a free ring spot
            // nearer the front (C2: kRechoices times, reset by a re-seed).
            if(ringMember(g,m)&&m.state==Holding&&heldFor(m)>=kCrowdWindow&&m.rechoices<kRechoices&&ringRechoose(u,m,g,p))++m.rechoices;
        }
        if(!m.route.empty()) {
            while(!m.route.empty()&&m.route.front()==here)m.route.erase(m.route.begin());
            if(m.route.empty()||++m.routeTicks>240||(m.state==Holding&&heldFor(m)>=30)) {m.route.clear();m.lineCell=-1;}
            else {
                // Route cells are adjacent and were proven clear of still
                // bodies cell by cell: follow them one at a time (a pulled
                // string could clip a body the search went around).
                const int aim=m.route.front();
                // A short way around still bodies is walked without turning
                // the body (a crowd shuffle), not as a U-turn and back.
                // (Facing it while walking measured as spin in shared-goal
                // crowds: 0 -> 10-22k unit-ticks in sharedgoal 2000.)
                const auto [ax,az]=stepAim(u,aim);
                holdStall(m);
                drive(u,m,p,nullptr,maximum,ax,az,false,false,false);
                return;
            }
        }
        if(m.detour>=0) {
            // A committed side-step runs to completion (or a short timeout):
            // no re-deciding every update, so no back-and-forth.
            if(here==m.detour||++m.detourTicks>45||!legal(p,m.detour%W,m.detour/W)) {m.detour=-1;m.lineCell=-1;}
            else {
                // Lateral shuffles keep facing the route: no heading thrash.
                // Side-steps do not turn the body either (measured: turning
                // 90 degrees and back in a jam reads as spinning); only a
                // pass committed with the lane ahead clear faces its step. A
                // lane-discipline pass keeps travel speed; the facing cap
                // that made non-turning keep-right steps crawl (and
                // gridlocked opposing columns) does not apply to it.
                const auto [ax,az]=stepAim(u,m.detour);
                holdStall(m);
                drive(u,m,p,nullptr,maximum,ax,az,false,false,m.detourFace,m.detourPass);
                return;
            }
        }
        const int goalX=m.goal%W,goalZ=m.goal/W;
        const Fixed gx=centre(goalX,fx),gz=centre(goalZ,fz);
        // Progress is the integer distance still to go: field potential, or
        // squared cells to the own goal once that is in a straight line.
        {
            const uint32_t left=uint32_t(std::min<int64_t>(0xfffffff,
                f&&f->at(size_t(here))!=kUnreached&&f->at(size_t(here))>0
                    ? int64_t(f->at(size_t(here)))*64
                    : (int64_t(goalX-ox)*(goalX-ox)+int64_t(goalZ-oz)*(goalZ-oz))));
            if(left<m.progress) {m.progress=left;m.stallTick=w.tickCounter_;m.detourCount=0;}
            if(stalledFor(m)>=20&&contactArrival(u,m)) {complete(u,m,true);return;}
            // B1: a member blocked kBlockedRetry ticks outside its destination
            // area, and not arrived by contact just above, makes its group
            // active (see active): Retail's blocked re-request. Blocked means
            // a re-plan could help: no finished field, or no step down the
            // field from here that is legal and clear of soft bodies (a
            // static change cut the way, or bodies standing still close it,
            // which a refresh plans round). A body held only by its own
            // command's bodies or by movers on a free way down is not: a
            // refresh would give it the same way (guards queued at an idle
            // friend hold like this for good: staticidle).
            if(stalledFor(m)>=kBlockedRetry&&!giveWayHolder(m)&&
               (!f||!f->done||f->at(size_t(here))==kUnreached||
                (!freeDescent(u,g,p,*f,ox,oz)&&f->at(size_t(here))>areaBound(g,m,fx,fz,int64_t(std::max(fx,fz))*16)))) {
                if(w.tickCounter_-g.blockedTick>2)++stats.blockedRerequests;
                g.blockedTick=w.tickCounter_;
            }
            // Nothing contactArrival reads changes before drive asks again
            // in this update (no step was taken, no goal or slot changes).
            contactRefused=&m;
        }
        Fixed aimX=gx,aimZ=gz;
        bool direct=false;
        if(here==m.goal)direct=true;
        else {
            if(m.lineCell!=here) {
                m.lineCell=here;
                // A formation member walks its own straight lane from farther out:
                // descending the shared field first funnels the crowd into a file.
                const int reach=m.slot>=0&&formationMember(m)?kFormationLineCells:kLineCells;
                // A line across a soft block (bodies standing still, not
                // this command's own arrivals) is refused: the field plans
                // round it.
                const uint64_t command=g.soft?g.command:kNoSoft;
                const int counts=!softCellCount?-1:softCountsFor(fx,fz);
                m.line=std::max(std::abs(goalX-ox),std::abs(goalZ-oz))<=reach&&sweep(p,u,u.x,u.z,gx,gz)&&
                    (!softCellCount||trace(u,u.x,u.z,gx,gz,[&](int cx,int cz) {return !softAt(cx,cz,fx,fz,command,counts);}))&&
                    // ... and so is a line through a mover the group plans round.
                    (g.avoidSeg.empty()||trace(u,u.x,u.z,gx,gz,[&](int cx,int cz) {
                        const int64_t px=int64_t(cx)*16+fx*8,pz=int64_t(cz)*16+fz*8;
                        for(const auto& c:g.avoidSeg)if(Field::segDist2(px,pz,c)<=c.r*c.r)return false;
                        return true;}));
            }
            direct=m.line;
        }
        // A ring member near its own spot walks to it: band 0 is all one
        // potential, and an outer spot lies up the field from band 0.
        if(!direct&&f&&f->done&&ringMember(g,m)) {
            const uint16_t ph=f->at(size_t(here)),pg=f->at(size_t(m.goal));
            if(ph!=kUnreached&&pg!=kUnreached&&uint32_t(ph)<=uint32_t(pg)+uint32_t(3*std::max(fx,fz)*kOrthogonal))direct=true;
        }
        if(!direct) {
            if(!f||(f->at(size_t(here))==kUnreached&&f->bounded)) {
                setWaited(g);
                // No field reaches this body yet: head toward the goal along
                // a short proven straight segment (kHeadingCells) instead of
                // standing still; the field takes over as soon as its
                // frontier passes here. Blocked: wait.
                const int dx=goalX-ox,dz=goalZ-oz,span=std::max(std::abs(dx),std::abs(dz));
                const int hx=ox+dx*kHeadingCells/span,hz=oz+dz*kHeadingCells/span;
                if(span>kHeadingCells&&sweep(p,u,u.x,u.z,centre(hx,fx),centre(hz,fz))) {
                    drive(u,m,p,nullptr,maximum,centre(hx,fx),centre(hz,fz),false,false);
                    return;
                }
                m.state=Waiting;++stats.waitingMemberTicks;w.brakeGround(u);return;
            }
            const uint16_t potential=f->at(size_t(here));
            if(potential==kUnreached) {trapped(u,m);return;}
            int cell=aimCell(u,m,p,*f,ox,oz);
            if(cell<0) {hold(u,m);return;}
            if(f->done&&m.pivotR>=0&&m.slot>=0&&m.pt&&partRefs(m)>=kPivotMembers&&formationMember(m)) {
                if(m.pivotCell!=here) {m.pivotCell=here;m.pivotAim=pivotAim(u,m,p,*f,ox,oz);}
                if(m.pivotAim>=0) {cell=m.pivotAim;pivoting=&m;pivotTarget=cell;}
            }
            aimX=centre(cell%W,fx);aimZ=centre(cell/W,fz);
        }
        if(here!=m.goal&&passAhead(u,m,p,f,maximum,ox,oz,aimX,aimZ,direct))return;
        drive(u,m,p,f,maximum,aimX,aimZ,here==m.goal,direct);
    }
    // The cell a field follower at origin (ox,oz) aims at; -1 when no
    // legal neighbour is nearer its goal (it holds). String-pull along the
    // descent chain: the farthest of the next few cells reachable in a
    // straight legal line. Every input is fixed while the body stands still
    // on one field and plane (the field is finished, the plane changes only
    // with the static epoch), so a held body reuses its last answer: the
    // descent and line sweeps were the largest share of a held update.
    int aimCell(const Unit& u,Member& m,const Plane& p,const Field& f,int ox,int oz) {
        auto& a=m.aim;
        const bool memo=f.done&&f.serial;
        if(memo&&a.x==u.x.v&&a.z==u.z.v&&a.goal==m.goal&&a.type==u.type&&a.field==f.serial&&a.epoch==epoch)
            return a.cell;
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
        const int goalX=m.goal%W,goalZ=m.goal/W;
        int cell=descend(p,f,ox,oz,m.goal,fx,fz);
        if(cell<0) {
            // Inside the goal region the field is flat (potential 0) but
            // this member's own goal is not in line: walk its seed set by
            // re-entering the goal's neighbourhood through the nearest
            // legal neighbour that reduces octile distance to the goal.
            int best=-1;int64_t bestD=int64_t(goalX-ox)*(goalX-ox)+int64_t(goalZ-oz)*(goalZ-oz);
            for(const auto& d:kDirections) {
                if(!step(p,ox,oz,d[0],d[1]))continue;
                const int64_t dd=int64_t(goalX-ox-d[0])*(goalX-ox-d[0])+int64_t(goalZ-oz-d[1])*(goalZ-oz-d[1]);
                if(dd<bestD) {bestD=dd;best=(oz+d[1])*W+ox+d[0];}
            }
            cell=best;
        } else {
            int chain=cell;
            for(int k=0;k<3;++k) {
                const int next=descend(p,f,chain%W,chain/W,m.goal,fx,fz);
                if(next<0)break;
                if(!sweep(p,u,u.x,u.z,centre(next%W,fx),centre(next/W,fz)))break;
                chain=next;
            }
            cell=chain;
        }
        if(memo)a={u.x.v,u.z.v,m.goal,u.type,f.serial,epoch,cell};
        return cell;
    }
    // Pinwheel: a formation keeps its width round the end of a wall. The
    // field's shortest ways past a convex wall end all touch its tip, so
    // members descending it fold into one file there. Instead, when a turn
    // of its descent chain beside a wall comes into its look-ahead, a member
    // takes a rank: its side offset in the group across the way to the turn,
    // counted from the inner side (the inner file hugs as before; outer files
    // get wider radii, so lanes never cross). Its descent chain beside the
    // wall, offset outward by that radius perpendicular to the chain, is a
    // concentric arc round the wall end (and a parallel lane along the walls
    // either side of it); the member pursues it, aiming at the first arc
    // point at least kPivotLead cells away that is in a straight legal line.
    // Where the free width beside the wall is narrower than the radius the
    // arc shrinks cell by cell to what is free, so a gap folds the outer
    // files inward, down to the field's own passage lanes and single file.
    // Bounded work; recomputed when the body changes origin.
    int pivotAim(const Unit& u,Member& m,const Plane& p,const Field& f,int ox,int oz) {
        const int W=width(),fx=u.type->footX,fz=u.type->footZ,foot=std::max(fx,fz);
        const int here=oz*W+ox;
        const uint16_t level=f.at(size_t(here));
        if(level==kUnreached)return -1;
        std::array<int,kPivotChain+1> chain{};int n=0;chain[size_t(n++)]=here;
        while(n<=kPivotChain) {
            const int next=descend(p,f,chain[size_t(n-1)]%W,chain[size_t(n-1)]/W,m.goal,fx,fz);
            if(next<0)break;
            chain[size_t(n++)]=next;
        }
        // Outward (away from illegal origins) vector at a chain cell; zero
        // when no illegal origin is within a cell.
        auto wall=[&](int c,int& vx,int& vz) {
            const int x=c%W,z=c/W;vx=vz=0;
            for(const auto& d:kDirections)if(!legal(p,x+d[0],z+d[1])) {vx-=d[0];vz-=d[1];}
            return vx||vz||!legal(p,x+1,z)||!legal(p,x,z+1)||!legal(p,x-1,z)||!legal(p,x,z-1);
        };
        int touch=-1;
        for(int i=0;i<n&&touch<0;++i) {int vx,vz;if(wall(chain[size_t(i)],vx,vz))touch=i;}
        if(touch<0)return -1;
        // A passage on the way (a strip narrower than kLaneSpan across, as
        // the passage lanes use) ends the look-ahead: there the field and its
        // lane grid steer, so a gap beside a wall end is taken single file.
        auto narrow=[&](int c) {
            const int x=c%W,z=c/W;
            for(const auto& d:std::array<std::array<int,2>,2>{{{1,0},{0,1}}}) {
                int run=1;
                for(int k=1;k<kLaneSpan&&legal(p,x+d[0]*k,z+d[1]*k);++k)++run;
                for(int k=1;run<kLaneSpan&&legal(p,x-d[0]*k,z-d[1]*k);++k)++run;
                if(run<kLaneSpan)return true;
            }
            return false;
        };
        // Nor near the destination: there the formation's slots and the
        // area logic place the bodies (a rock among the slots is no wall
        // end to wheel round).
        for(int i=touch;i<n;++i)if(narrow(chain[size_t(i)])||f.at(size_t(chain[size_t(i)]))<kPivotNear*kOrthogonal) {n=i;break;}
        if(touch>=n)return -1;
        if(m.pivotR==0) {
            // Taken once, as the wall end comes into reach: a turn of the
            // chain (30 degrees or more between 6-cell chords) beside the
            // wall.
            int turn=-1;int64_t side=0;
            for(int i=touch;i+12<n&&turn<0;i+=2) {
                const int64_t ax=chain[size_t(i+6)]%W-chain[size_t(i)]%W,az=chain[size_t(i+6)]/W-chain[size_t(i)]/W;
                const int64_t bx=chain[size_t(i+12)]%W-chain[size_t(i+6)]%W,bz=chain[size_t(i+12)]/W-chain[size_t(i+6)]/W;
                const int64_t cross=ax*bz-az*bx;
                if(cross*cross*4>=(ax*ax+az*az)*(bx*bx+bz*bz)) {turn=i+6;side=cross>0?1:-1;}
            }
            const int cap=std::min<int>(kPivotMaxCells,int(isqrtFloor(uint64_t(std::max(partRefs(m),0))))*foot*3/2);
            if(turn<0)return -1;
            // Rank: the member's side offset from its part's live centroid
            // across the way to the turn, measured from the inner (turn)
            // side; the centre file keeps half the part's packed width.
            auto& pt=*m.pt;
            int64_t liveX,liveZ;
            if(pt.partRefs.size()<=1) {
                if(pt.liveTick!=w.tickCounter_) {
                    int64_t sx=0,sz=0,count=0;
                    for(const int id:pt.ids)if(const Unit* v=w.unit(id)) {sx+=v->x.v>>16;sz+=v->z.v>>16;++count;}
                    pt.liveTick=w.tickCounter_;pt.liveX=count?sx/count:0;pt.liveZ=count?sz/count:0;
                }
                liveX=pt.liveX;liveZ=pt.liveZ;
            } else {
                // A point of several parts (one click split by the uplink,
                // A2): each part ranks against its own centroid (C33), so
                // the merged selection is never ranked as one. One pass
                // over the point's ids per tick, counted as pivot work.
                if(pt.partTick!=w.tickCounter_) {
                    pt.partTick=w.tickCounter_;pt.partLive.clear();
                    for(const int id:pt.ids) {
                        const Member* mm=member(id);const Unit* v=w.unit(id);
                        if(!mm||!v)continue;
                        auto& a=pt.partLive[mm->part];a[0]+=v->x.v>>16;a[1]+=v->z.v>>16;++a[2];
                    }
                    stats.pivotPartIds+=pt.ids.size();
                }
                const auto found=pt.partLive.find(m.part);
                const int64_t count=found==pt.partLive.end()?0:found->second[2];
                liveX=count?found->second[0]/count:0;liveZ=count?found->second[1]/count:0;
            }
            const int64_t tx=int64_t(chain[size_t(turn)]%W)*16+fx*8-liveX,tz=int64_t(chain[size_t(turn)]/W)*16+fz*8-liveZ;
            const int64_t tl=isqrtFloor(uint64_t(tx*tx+tz*tz));
            const int64_t lat=tl?(tx*((u.z.v>>16)-liveZ)-tz*((u.x.v>>16)-liveX))/tl:0;   // px, + right of the way
            const int rank=int(std::clamp<int64_t>(cap/2-side*lat/16,0,cap));
            m.pivotR=int16_t(rank>=kPivotMinBodies*foot?rank:-1);
            if(m.pivotR<0)return -1;
        }
        const int64_t R=m.pivotR;
        int sweeps=0;
        // Pursuit along the arc: the first offset point at least
        // kPivotLead cells from the body's centre cell (a farther one would
        // cut the arc's chord through the inner files).
        const int64_t mx=u.x.v>>20,mz=u.z.v>>20;
        for(int j=std::max(touch,kPivotAhead);j<n;++j) {
            const int c=chain[size_t(j)];
            int vx,vz;
            if(!wall(c,vx,vz))continue;
            const int a=chain[size_t(std::max(j-3,0))],b=chain[size_t(std::min(j+3,n-1))];
            const int64_t tx=b%W-a%W,tz=b/W-a/W;
            int64_t nx=-tz,nz=tx;
            const int64_t len=isqrtFloor(uint64_t(nx*nx+nz*nz));
            if(!len)continue;
            if(nx*vx+nz*vz<0) {nx=-nx;nz=-nz;}
            if(nx*vx+nz*vz==0)continue;
            const int cx=c%W,cz=c/W;
            // The free width beside the wall here (inside the field's
            // window): the arc shrinks to it. The offset is a straight legal
            // segment from the chain cell, so never a pocket behind a wall.
            // Round the end the arc is a level set of the field (every point
            // on it is as far from the tip), so the aim is not required to
            // be lower than here: its chain cell is ahead.
            auto open=[&](int64_t k) {
                const int x=cx+int(nx*k/len),z=cz+int(nz*k/len);
                return legal(p,x,z)&&f.at(size_t(z)*W+x)!=kUnreached;
            };
            int64_t r=0;
            while(r<R&&open(r+1))++r;
            if(r<int64_t(kPivotMinBodies)*foot)continue;
            const int qx=cx+int(nx*r/len),qz=cz+int(nz*r/len);
            if((qx-mx)*(qx-mx)+(qz-mz)*(qz-mz)<int64_t(kPivotLead)*kPivotLead&&j+1<n)continue;
            // Only an arc point ahead along the arc (never one beside or
            // behind the body: walking back to it reads as reversing).
            if((qx-mx)*tx+(qz-mz)*tz<=0)continue;
            if(++sweeps>kPivotSweeps)return -1;
            if(sweep(p,u,u.x,u.z,centre(qx,fx),centre(qz,fz)))return qz*W+qx;
        }
        return -1;
    }
    // Lane discipline for opposing traffic: a mover that sees an oncoming
    // Legion mover in its own swept lane a few cells ahead moves over one
    // cell to its right (the same side sidestep() uses) BEFORE contact, as a
    // committed forward-right step, so both streams keep moving and form
    // lanes instead of meeting nose to nose and holding. It repeats while
    // the lane ahead still holds oncoming bodies; same-direction bodies
    // ahead are followed, never passed. Returns whether a step was committed.
    bool passAhead(Unit& u,Member& m,const Plane& p,const Field* f,Fixed maximum,int ox,int oz,Fixed aimX,Fixed aimZ,bool direct) {
        const int64_t ax=int64_t(aimX.v)-u.x.v,az=int64_t(aimZ.v)-u.z.v;
        const int64_t aax=std::abs(ax),aaz=std::abs(az);
        if(!aax&&!aaz)return false;
        // Travel octant (tan 22.5 ~ 0.414 ~ 2/5).
        const int dx=aax*5>=aaz*2?(ax>0?1:-1):0,dz=aaz*5>=aax*2?(az>0?1:-1):0;
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
        // Inside the destination area bodies pack into their slots; passing
        // there only displaces them.
        if(std::max(std::abs(m.goal%W-ox),std::abs(m.goal/W-oz))<=2*kPassCells)return false;
        const int rx=-dz,rz=dx;
        const uint16_t here=f?f->at(size_t(oz*W+ox)):kUnreached;
        const std::array<std::array<int,2>,2> options{{{std::clamp(dx+rx,-1,1),std::clamp(dz+rz,-1,1)},{rx,rz}}};
        // The lane scan only decides between the two outcomes below. When
        // neither can commit a step -- no pass cell is open (a body in a
        // queue or a packed block) and no committed lane is held -- the
        // verdict is "no step" whatever the scan finds, so it is skipped.
        // Every test here is a necessary condition of the step it guards
        // (body occupancy only: stepFree's placement check comes after it).
        auto occupied=[&](int x,int z) {
            for(int j=0;j<fz;++j)for(int i=0;i<fx;++i) {
                const int cx=x+i,cz=z+j;
                if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
                const int32_t o=occAt(size_t(cz)*w.occW_+cx);
                if(o&&o!=u.id)return true;
            }
            return false;
        };
        auto mayStep=[&](int sx,int sz) {
            if(!step(p,ox,oz,sx,sz))return false;
            if(w.occW_<=0)return true;
            return !occupied(ox+sx,oz+sz)&&(!sx||!sz||(!occupied(ox+sx,oz)&&!occupied(ox,oz+sz)));
        };
        auto passOpen=[&](const std::array<int,2>& d) {
            if(!d[0]&&!d[1])return false;
            const int cell=(oz+d[1])*W+ox+d[0];
            if(f&&(f->at(size_t(cell))==kUnreached||(!direct&&f->at(size_t(cell))>uint32_t(here)+kDiagonal)))return false;
            return mayStep(d[0],d[1]);
        };
        const bool laneHeld=direct&&m.passUntil>w.tickCounter_;
        if(!passOpen(options[0])&&!passOpen(options[1])&&!(laneHeld&&mayStep(dx,dz))) {++stats.passScansSkipped;return false;}
        // No block the scan reads holds a member direction that can be
        // oncoming (see passMask): the scan would find no oncoming body.
        bool opposed=passMask.empty();
        if(!opposed) {
            const uint16_t want=passOpposed()[size_t((dx+1)*3+dz+1)];
            const int x0=std::max(ox+std::min(dx,kPassCells*dx),0),x1=std::min(ox+std::max(dx,kPassCells*dx)+fx-1,w.occW_-1);
            const int z0=std::max(oz+std::min(dz,kPassCells*dz),0),z1=std::min(oz+std::max(dz,kPassCells*dz)+fz-1,w.occH_-1);
            for(int bz=z0/kPassBlock;bz<=z1/kPassBlock&&!opposed&&x0<=x1;++bz)for(int bx=x0/kPassBlock;bx<=x1/kPassBlock;++bx)
                if(passMask[size_t(bz)*passMaskW+bx]&want) {opposed=true;break;}
        }
        bool oncoming=false;
        // A body spans several scanned cells; the verdict on it depends only
        // on the body, so each one is judged once (the scan's hot cost was
        // the repeated member lookups).
        std::array<int32_t,8> seen{};size_t seenCount=0;
        uint64_t scanned=0;
#ifndef NDEBUG
        // TAK_LEGION_VERIFY: a skipped scan is run anyway and must find no
        // oncoming body.
        const bool scan=opposed||gVerify;
#else
        const bool scan=opposed;
#endif
        if(opposed)++stats.passScans;else ++stats.passScansSkipped;
        if(scan)for(int k=1;k<=kPassCells&&!oncoming;++k)for(int j=0;j<fz&&!oncoming;++j)for(int i=0;i<fx&&!oncoming;++i) {
            ++scanned;
            const int cx=ox+k*dx+i,cz=oz+k*dz+j;
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
            const int32_t o=occAt(size_t(cz)*w.occW_+cx);
            if(!o||o==u.id)continue;
            if(std::find(seen.begin(),seen.begin()+seenCount,o)!=seen.begin()+seenCount)continue;
            if(seenCount<seen.size())seen[seenCount++]=o;
            const Member* peer=member(o);
            if(!peer||peer->state==Arrived||peer->state==Trapped||peer->state==Engaged)continue;
            const Unit* other=w.unit(o);
            if(!other||other->orders.empty()||peer->goal<0)continue;
            // Oncoming by intent, not heading: the other body's way to its
            // own goal points back at this one by more than ~112 degrees. A
            // body that has not turned yet (fresh or reversed order) is not
            // oncoming traffic.
            const int64_t odx=peer->goal%W-footprintOrigin(other->x,other->type->footX);
            const int64_t odz=peer->goal/W-footprintOrigin(other->z,other->type->footZ);
            if(std::max(std::abs(odx),std::abs(odz))<=2*kPassCells)continue;
            const int64_t dot=odx*dx+odz*dz;
            oncoming=dot<0&&100*dot*dot>kOncomingCos2*(odx*odx+odz*odz)*(dx*dx+dz*dz);
        }
#ifndef NDEBUG
        if(!opposed&&oncoming)verifyFail("passAhead's oncoming mask skipped a scan that finds an oncoming body");
        if(!opposed)scanned=0;
#endif
        stats.passScanCells+=scanned;
        if(!oncoming) {
            // Committed pass: for a while after moving over, keep to the new
            // lane (straight ahead) instead of edging back toward the goal
            // line, which would cross the lane just left and read as a
            // left-right shuffle in dense traffic.
            if(!direct||m.passUntil<=w.tickCounter_)return false;
            if(!step(p,ox,oz,dx,dz)||!stepFree(u,ox,oz,ox+dx,oz+dz))return false;
            const auto [cx,cz]=stepAim(u,(oz+dz)*W+ox+dx);
            drive(u,m,p,f,maximum,cx,cz,false,true);
            return true;
        }
        for(const auto& d:options) {
            if(!d[0]&&!d[1])continue;
            if(!step(p,ox,oz,d[0],d[1]))continue;
            const int cell=(oz+d[1])*W+ox+d[0];
            // Never onto an unreachable cell; a field follower also may not
            // climb more than a diagonal step of potential.
            if(f&&(f->at(size_t(cell))==kUnreached||(!direct&&f->at(size_t(cell))>uint32_t(here)+kDiagonal)))continue;
            if(!stepFree(u,ox,oz,ox+d[0],oz+d[1]))continue;
            ++stats.slides;
            // A pass made with the lane ahead nearly clear is a committed
            // manoeuvre around oncoming traffic: the body faces it (unturned
            // it read as sliding sideways). Inside a dense opposing block it
            // is a one-cell dodge and stays unturned: turning there and back
            // reads as spinning.
            int busy=0;
            for(int k=1;k<=3;++k)for(int j=-1;j<=fz;++j)for(int i=-1;i<=fx;++i) {
                const int bx=ox+k*dx+i,bz=oz+k*dz+j;
                if(bx<0||bz<0||bx>=w.occW_||bz>=w.occH_)continue;
                const int32_t o=occAt(size_t(bz)*w.occW_+bx);
                if(o&&o!=u.id)++busy;
            }
            const bool face=busy<=fx*fz;
            m.detour=cell;m.detourTicks=0;m.detourFace=face;m.detourPass=true;m.lineCell=-1;
            m.passUntil=w.tickCounter_+kPassHold;m.passRX=int8_t(rx);m.passRZ=int8_t(rz);
            const auto [cx,cz]=stepAim(u,cell);
            drive(u,m,p,nullptr,maximum,cx,cz,false,false,face,true);
            return true;
        }
        return false;
    }
    void drive(Unit& u,Member& m,const Plane& p,const Field* f,Fixed maximum,Fixed aimX,Fixed aimZ,bool final,
               bool towardGoal=false,bool face=true,bool pass=false) {
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        int64_t dx=int64_t(aimX.v)-u.x.v,dz=int64_t(aimZ.v)-u.z.v;
        int64_t length=isqrtFloor(uint64_t(dx*dx+dz*dz));
        if(final&&length<=Fixed::kOne/2) {
            // Exactly on the goal centre (sub-pixel): this leg is complete.
            complete(u,m,false);return;
        }
        // Heading: turn toward the travel direction at the authored rate.
        // Motion follows the legal direction exactly; speed is limited while
        // the body still faces away from it, so turning never sweeps the
        // footprint off the proved line (no orbiting, no wall pushing).
        const Fixed multiplier=w.groundTerrainMultiplier(u);
        const auto wanted=retailHeadingToPort(uint16_t(retailDirection(u.x-aimX,u.z-aimZ).v));
        const int32_t diff=retailTurnRequest(wanted,u.heading);
        const int32_t turn=std::max(int32_t(1),int32_t(int64_t(uint16_t(std::max(u.type->turnRate,u.type->turnInPlaceRate)))*multiplier.v/65536));
        Fixed cap=retailGroundSpeedCap(maximum*multiplier,u.groundPitch,0);
        const int32_t facing=std::abs(diff);
        // A lane-discipline pass slides one cell over without turning (turning
        // there read as spinning in a jam); it keeps travel speed.
        if(pass) {}
        else if(facing>12288)cap=Fixed::raw(cap.v/8);
        else if(facing>4096)cap=Fixed::raw(cap.v/2);
        Fixed speed=fxMin(cap,u.speed+u.type->accel*multiplier);
        if(u.speed>cap)speed=fxMax(cap,u.speed-u.type->brake*multiplier);
        // A work/logistics leg never creeps a body that cannot accelerate (a
        // released passenger stands on its landing point, as natively).
        if(speed<=Fixed()&&(m.kind<=Kind::Guard||u.type->accel>Fixed()))speed=Fixed::raw(std::max(1,cap.v/8));
        // A leader must be slower than this body and at least half its cap,
        // so none exists unless this body is above half its cap (a held or
        // starting body never is): the scan below is skipped then.
        if(length>0&&int64_t(speed.v)*2>int64_t(cap.v)) {
            // Speed-matched following: keep station behind a slower body of
            // the same player moving the same way (heading within 45 deg) a
            // cell or two ahead -- match its speed, at most halving this
            // body's -- instead of running up to it and stopping dead (the
            // stop-go of a mixed-speed column). A leader below half of
            // this body's cap is creeping, not travelling: matching it would
            // chain a whole queue down to a crawl, so it is not followed.
            const int64_t adx=std::abs(dx),adz=std::abs(dz);
            const int sdx=adx*5>=adz*2?(dx>0?1:-1):0,sdz=adz*5>=adx*2?(dz>0?1:-1):0;
            const Unit* lead=nullptr;
            for(int k=1;k<=2&&!lead;++k)for(int j=0;j<fz&&!lead;++j)for(int i=0;i<fx&&!lead;++i) {
                const int cx=ox+k*sdx+i,cz=oz+k*sdz+j;
                if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
                const int32_t o=occAt(size_t(cz)*w.occW_+cx);
                if(!o||o==u.id)continue;
                const Unit* other=w.unit(o);
                if(other&&other->player==u.player&&other->speed>Fixed()&&other->speed<speed&&int64_t(other->speed.v)*2>=cap.v&&
                   std::abs(retailTurnRequest(other->heading,u.heading))<8192)lead=other;
            }
            if(lead)speed=fxMax(lead->speed,Fixed::raw(std::max(1,speed.v/2)));
        }
        int64_t travel=std::min<int64_t>(speed.v,length);
        auto proposal=[&](int64_t ax,int64_t az,int64_t len,int64_t along) {
            return std::pair{Fixed::raw(int32_t(len?ax*along/len:0)),Fixed::raw(int32_t(len?az*along/len:0))};
        };
        auto [sx,sz]=proposal(dx,dz,length,travel);
        int nx=footprintOrigin(u.x+sx,fx),nz=footprintOrigin(u.z+sz,fz);
        // A step that changes both origins at once must clear both side
        // origins (the mover's corner rule). Near a wall's end a proven line
        // can pass the corner a fraction of a pixel away: it crosses one axis
        // first, then the other, through a legal side cell, but a whole
        // update's step jumps both at once onto the illegal one. That step
        // is blocked (as by a body; on a legacy plane the mover refuses it);
        // if nothing else frees the body, it crosses only the legal axis
        // (the other coordinate stops at its origin's edge), as the line
        // does, instead of holding there forever.
        const bool corner=nx!=ox&&nz!=oz&&!step(p,ox,oz,nx-ox,nz-oz);
        const Fixed sx0=sx,sz0=sz;const int nx0=nx,nz0=nz;
        auto cornerStep=[&]()->bool {
            auto edge=[](Fixed at,Fixed by,int origin,int foot) {
                const int64_t offset=int64_t(foot-1)*8*Fixed::kOne;
                const int64_t lo=(int64_t(origin)<<20)+offset,hi=lo+(int64_t(1)<<20)-1;
                return Fixed::raw(int32_t(std::clamp<int64_t>(int64_t(at.v)+by.v,lo,hi)-at.v));
            };
            Fixed cx=sx0,cz=sz0;int kx=nx0,kz=nz0;
            if(legal(p,ox,nz0)) {cx=edge(u.x,sx0,ox,fx);kx=ox;}
            else if(legal(p,nx0,oz)) {cz=edge(u.z,sz0,oz,fz);kz=oz;}
            else return false;
            if(!stepFree(u,ox,oz,kx,kz))return false;
            sx=cx;sz=cz;nx=kx;nz=kz;return true;
        };
        if(corner||!stepFree(u,ox,oz,nx,nz)) {
            // Blocked by a body. Flow around it through any other free cell
            // that is strictly closer to the goal (field descent), committing
            // to that neighbour for this update; otherwise hold still.
            bool moved=false;
            if(f) {
                // Direct-line members measure progress toward their own goal
                // cell; field followers by the group potential.
                const int goalX=m.goal%W,goalZ=m.goal/W;
                const bool arc=pivoting==&m&&!towardGoal;
                const int arcX=pivotTarget%W,arcZ=pivotTarget/W;
                auto metric=[&](int x,int z)->int64_t {
                    if(arc)return int64_t(arcX-x)*(arcX-x)+int64_t(arcZ-z)*(arcZ-z);
                    if(towardGoal)return int64_t(goalX-x)*(goalX-x)+int64_t(goalZ-z)*(goalZ-z);
                    return f->at(size_t(z*W+x));
                };
                const int64_t here=metric(ox,oz);
                std::array<std::pair<int64_t,int>,8> options{};int count=0;
                for(int k=0;k<8;++k) {
                    const auto& d=kDirections[size_t(k)];
                    if(!step(p,ox,oz,d[0],d[1]))continue;
                    if(f->at(size_t((oz+d[1])*W+ox+d[0]))==kUnreached)continue;
                    // A body that just moved over for oncoming traffic does
                    // not edge back across the lane it left.
                    if(m.passUntil>w.tickCounter_&&d[0]*m.passRX+d[1]*m.passRZ<0)continue;
                    const int64_t v=metric(ox+d[0],oz+d[1]);
                    if(v<here)options[size_t(count++)]={v,k};
                }
                std::sort(options.begin(),options.begin()+count);
                for(int i=0;i<count&&!moved;++i) {
                    const auto& d=kDirections[size_t(options[size_t(i)].second)];
                    if(!stepFree(u,ox,oz,ox+d[0],oz+d[1]))continue;
                    const auto [cx,cz]=stepAim(u,(oz+d[1])*W+ox+d[0]);
                    dx=int64_t(cx.v)-u.x.v;dz=int64_t(cz.v)-u.z.v;
                    length=isqrtFloor(uint64_t(dx*dx+dz*dz));
                    travel=std::min<int64_t>(speed.v,length);
                    std::tie(sx,sz)=proposal(dx,dz,length,travel);
                    nx=footprintOrigin(u.x+sx,fx);nz=footprintOrigin(u.z+sz,fz);
                    if(stepFree(u,ox,oz,nx,nz)) {
                        // Commit to finishing this step into the cell
                        // (hysteresis): no re-deciding mid-cell, and the body
                        // keeps facing its route while it shuffles.
                        moved=true;++stats.slides;face=false;
                        m.detour=(oz+d[1])*W+ox+d[0];m.detourTicks=0;m.detourFace=false;m.detourPass=false;
                    }
                }
            }
            if(!moved&&corner)moved=cornerStep();
            if(!moved) {
                if(contactRefused!=&m&&contactArrival(u,m)) {complete(u,m,true);return;}
                if(f&&m.detour<0)sidestep(u,m,p,*f,ox,oz,nx,nz);
                // A body walled in by STILL bodies (settled arrivals, a held
                // queue, idle units) plans a short committed detour around
                // them; moving traffic is waited for, not planned around.
                // A formation member held a long time is in a standing jam of
                // crossing lanes (nobody ahead will move first): it plans too.
                // Blocked by settled bodies again with no progress since the
                // last detour: ask them to yield FIRST. A detour away from
                // them is dropped when blocked and the body shuffles back,
                // every ~250-400 ticks, never holding long enough to ask (a
                // livelock).
                const bool settled=f&&m.detour<0&&m.route.empty()&&blockedBySettled(u,m,nx,nz);
                if(settled&&int64_t(m.progress)==m.detourBest&&m.holdUpdates>=6&&yieldLane(u,m,p,nx,nz)) {hold(u,m);restartHold(m);return;}
                if(f&&m.detour<0&&m.route.empty()&&m.holdUpdates>=m.nextDetour&&
                   (settled||(m.holdUpdates>=60&&formationMember(m)))) {
                    // Back off geometrically after each attempt: a crowd that
                    // stays jammed stops re-planning instead of shuffling.
                    const uint32_t wait=std::min<uint32_t>(30u<<std::min<uint32_t>(m.detourCount,4u),480u);
                    ++m.detourCount;
                    if(localDetour(u,m,p,f,towardGoal)) {m.nextDetour=wait;u.speed=Fixed();return;}
                    m.nextDetour=m.holdUpdates+wait;
                }
                if(f&&m.detour<0&&m.route.empty()&&m.holdUpdates>=12&&yieldLane(u,m,p,nx,nz)) {hold(u,m);restartHold(m);return;}
                if(f&&m.detour<0&&m.route.empty()&&m.holdUpdates>=12&&partLane(u,m,p,ox,oz,nx,nz)) {hold(u,m);restartHold(m);return;}
                hold(u,m);return;
            }
        }
        u.speed=speed;
        const Fixed beforeX=u.x,beforeZ=u.z;
        w.commitGroundStep(u,sx,sz,true);
        // The heading only turns with an actual step: a refused step is a
        // hold, and a held body keeps a constant heading.
        if(u.x==beforeX&&u.z==beforeZ) {hold(u,m);return;}
        // A small dead band keeps the body from twitching left and right as
        // its string-pulled aim shifts by a cell in a crowd; motion is exact
        // regardless, and a real course change (> ~5.6 deg) still turns.
        if(face&&std::abs(diff)>1024)u.heading=u.heading+Bam(std::clamp(diff,-turn,turn));
        u.turnReqBam=diff;
        // A reach member leaving its ring spot is no soft obstacle any more.
        if(m.state==Holding&&!policy(m.kind).completes)unstamp(u.id);
        m.state=Moving;m.heldSince=kNoTick;m.holdUpdates=0;
    }
    // A hold that starts over (a detour planned, a lane asked for): the
    // clocks run from the next tick, as a hold begun by the next update
    // would (heldFor reads 0 until then).
    void restartHold(Member& m) const {m.heldSince=w.tickCounter_+1;m.holdUpdates=0;}
    // Is the cell this body wants held by a body that will not move on its
    // own (idle, arrived, engaged in combat, or not a Legion mover)? A queue
    // of members waiting for each other is not: it drains by itself and must
    // not be re-planned. An attack or guard queue never drains (its owner
    // ends the approach only in reach), so for a reach kind a peer of the
    // same command held kReachPeerHeld ticks counts as settled too (AR-06,
    // C8): the queue walks round it toward the target.
    bool blockedBySettled(const Unit& u,const Member& m,int nx,int nz) const {
        const bool reach=!policy(m.kind).completes;
        for(int j=0;j<u.type->footZ;++j)for(int i=0;i<u.type->footX;++i) {
            const int cx=nx+i,cz=nz+j;
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
            const int32_t o=occAt(size_t(cz)*w.occW_+cx);
            if(!o||o==u.id)continue;
            const Member* peer=member(o);
            if(!peer||peer->state==Arrived||peer->state==Trapped||peer->state==Engaged)return true;
            if(reach&&peer->state==Holding&&peer->controller==m.controller&&heldFor(*peer)>=kReachPeerHeld)
                if(const Unit* other=w.unit(o);other&&other->player==u.player)return true;
        }
        return false;
    }
    static int z0goal(int local,int S,int R,int ox,int oz,int W) {return (oz+local/S-R)*W+ox+local%S-R;}
    // Bounded breadth-first search (window of radius kDetourCells) over
    // statically legal origins whose footprint touches no STILL body. The
    // target is the own goal if inside, else the reachable cell that most
    // reduces the distance still to go. The route is committed.
    bool localDetour(const Unit& u,Member& m,const Plane& p,const Field* f,bool towardGoal) {
        constexpr int R=kDetourCells,S=2*R+1;
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        const int goalX=m.goal%W,goalZ=m.goal/W;
        auto metric=[&](int x,int z)->int64_t {
            if(!towardGoal&&f) {
                const uint16_t v=f->at(size_t(z*W+x));
                return v==kUnreached?INT64_MAX:int64_t(v)*64;
            }
            return int64_t(goalX-x)*(goalX-x)+int64_t(goalZ-z)*(goalZ-z);
        };
        auto still=[&](int cx,int cz) {
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)return false;
            const int32_t o=occAt(size_t(cz)*w.occW_+cx);
            if(!o||o==u.id)return false;
            const Unit* other=w.unit(o);
            if(!other||other->speed!=Fixed())return false;
            if(other->orders.empty())return true;
            const Member* peer=member(o);
            return !peer||peer->state==Holding||peer->state==Arrived||peer->state==Trapped||peer->state==Engaged;
        };
        auto open=[&](int x,int z) {
            if(!legal(p,x,z))return false;
            for(int j=0;j<fz;++j)for(int i=0;i<fx;++i)if(still(x+i,z+j))return false;
            return true;
        };
        std::vector<int16_t> parent(size_t(S)*S,-1);
        std::vector<int> queue;queue.reserve(size_t(S)*S);
        const int start=R*S+R;parent[size_t(start)]=int16_t(start);queue.push_back(start);
        const int64_t current=metric(ox,oz);
        int best=-1;int64_t bestMetric=current;
        for(size_t head=0;head<queue.size();++head) {
            const int local=queue[head];
            const int lx=local%S,lz=local/S,x=ox+lx-R,z=oz+lz-R;
            if(z*W+x==m.goal) {best=local;break;}
            const int64_t v=local==start?current:metric(x,z);
            if(v<bestMetric) {bestMetric=v;best=local;}
            for(const auto& d:kDirections) {
                const int nlx=lx+d[0],nlz=lz+d[1];
                if(nlx<0||nlz<0||nlx>=S||nlz>=S)continue;
                const int next=nlz*S+nlx;
                if(parent[size_t(next)]>=0)continue;
                if(!step(p,x,z,d[0],d[1])||!open(x+d[0],z+d[1]))continue;
                if(d[0]&&d[1]&&(!open(x+d[0],z)||!open(x,z+d[1])))continue;
                parent[size_t(next)]=int16_t(local);queue.push_back(next);
            }
        }
        stats.detourCells+=queue.size();
        if(best<0||best==start)return false;
        // Only a real gain (about two cells) is worth committing to.
        if(z0goal(best,S,R,ox,oz,W)!=m.goal) {
            const bool field=!towardGoal&&f;
            const int64_t gain=field?current-bestMetric
                :int64_t(isqrtFloor(uint64_t(current)))-int64_t(isqrtFloor(uint64_t(bestMetric)));
            if(gain<(field?int64_t(2*kOrthogonal)*64:2))return false;
        }
        std::vector<int> path;
        for(int c=best;c!=start;c=parent[size_t(c)])path.push_back((oz+c/S-R)*W+ox+c%S-R);
        std::reverse(path.begin(),path.end());
        m.route=std::move(path);m.routeTicks=0;++stats.detours;m.detourBest=int64_t(m.progress);
        m.state=Holding;restartHold(m);  // stopped this update; the route starts next
        return true;
    }
    // Opposing traffic: after a short hold, both bodies commit to a lateral
    // step to their own right (keep-right), so head-on pairs pass instead
    // of pushing. Same-direction queues only side-step after a long hold.
    void sidestep(const Unit& u,Member& m,const Plane& p,const Field& f,int ox,int oz,int nx,int nz) {
        if(m.holdUpdates<6)return;
        const int W=width();
        int blocker=0;
        const int fx=u.type->footX,fz=u.type->footZ;
        for(int j=0;j<fz&&!blocker;++j)for(int i=0;i<fx&&!blocker;++i) {
            const int cx=nx+i,cz=nz+j;
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
            const int32_t o=occAt(size_t(cz)*w.occW_+cx);
            if(o&&o!=u.id)blocker=o;
        }
        const Unit* other=blocker?w.unit(blocker):nullptr;
        bool opposing=other&&!other->orders.empty()&&
            std::abs(retailTurnRequest(other->heading,u.heading))>16384;
        // Opposing by intent too: the blocker's way to its own goal points
        // back against this body's, and neither is inside its destination
        // area (there bodies head for their own slots in every direction;
        // as passAhead). A same-way body that merely faces elsewhere is
        // queued behind, not passed: a sideways step there makes no
        // progress and read as sliding.
        if(opposing) {
            const auto peer=members.find(blocker);
            if(peer==members.end()||peer->second.goal<0)opposing=false;
            else {
                const int64_t odx=peer->second.goal%W-footprintOrigin(other->x,other->type->footX);
                const int64_t odz=peer->second.goal/W-footprintOrigin(other->z,other->type->footZ);
                const int64_t mdx=m.goal%W-ox,mdz=m.goal/W-oz;
                opposing=odx*mdx+odz*mdz<0&&std::max(std::abs(mdx),std::abs(mdz))>2*kPassCells&&
                    std::max(std::abs(odx),std::abs(odz))>2*kPassCells;
            }
        }
        // Same-direction queues never side-step: they drain by themselves.
        // (Purposeful side-steps for them cost opposing-column throughput.)
        if(!opposing)return;
        int dx=nx-ox,dz=nz-oz;
        if(!dx&&!dz)return;
        dx=std::clamp(dx,-1,1);dz=std::clamp(dz,-1,1);
        const uint16_t here=f.at(size_t(oz*W+ox));
        const std::array<std::array<int,2>,2> sides{{{-dz,dx},{dz,-dx}}};
        for(int k=0;k<1;++k) {
            const int sx=sides[size_t(k)][0],sz=sides[size_t(k)][1];
            if(!step(p,ox,oz,sx,sz))continue;
            const int cell=(oz+sz)*W+ox+sx;
            if(f.at(size_t(cell))>uint32_t(here)+kDiagonal)continue;
            if(!stepFree(u,ox,oz,ox+sx,oz+sz))continue;
            m.detour=cell;m.detourTicks=0;m.detourFace=false;m.detourPass=false;return;
        }
    }
    // "Close enough" settling (the user's rule: late bodies get close to the
    // crowd already standing at their destination and are done). A body
    // that stopped gaining on its point settles where it stands when
    //  - it touches (within one body) a same-player body that already
    //    settled for the same destination (a Legion arrival whose point lies
    //    within two bodies of this one's, or an idle body) and stands nearer
    //    that point;
    //  - it is within the crowd's reach: four times the packed-disc radius
    //    of the arrivals settled there (plus two bodies) from the point
    //    (crowds pressed against terrain spread well past a disc);
    //  - the way to the destination area is short: its group-field potential
    //    is at most 1.4x the straight distance plus three bodies (never
    //    across a wall or terrain from the crowd);
    //  - it is not standing in a same-player factory's exit lane.
    // Its own footprint is legal (checked by the caller). A body passing
    // through a crowd on its way elsewhere is far from its own point and
    // touches nobody settled for it, so it never settles here.
    // Returns 0 (not here), 1 (touching only this same order's arrivals),
    // 2 (touching an earlier order's arrival or an idle body) or 3 (beyond
    // the reach, touching an arrival of this destination).
    // The settle rule (PLAN 3.1 T1-B), run once per crowd window by a body
    // that gained less than half a body on its point over the window.
    //  - Queued: a body touching it (the ring kSettleRing bounds) and nearer
    //    the point is a settled arrival of this destination, an idle body of
    //    the player, or a held member of this destination that is itself
    //    queued; or (a shared point) it stands inside the destination area
    //    itself. A chain that can only start at the destination's own
    //    crowd, so a jam on the way never queues.
    //  - A settled crowd of an earlier order, or idle bodies, already
    //    standing there: one window (within the crowd's reach).
    //  - Otherwise (destination areas only) a queued body pressed against
    //    the crowd (no free neighbour nearer the point, or no gain for the
    //    window) first re-chooses a
    //    free slot it can still reach (rechoose, kRechoices times), then
    //    settles where it stands once its potential is within settleP. The
    //    bound starts at the destination area's (areaBound) plus
    //    kSettleSlack bodies and grows by kSettleGrow bodies per window, but
    //    no completion is farther from the click than twice the crowd's
    //    reach (the bound the old far rule allowed, C29) unless the body is
    //    one row behind a settled arrival of its own command: the cap is
    //    measured along a queue contiguous with the settled crowd.
    // Every settle is connected (the field's way in is not much longer
    // than the straight line: no settling behind a wall) and outside a
    // factory's exit lane.
    bool settleWindow(const Unit& u,Member& m,Group& g,const Plane& p,int ox,int oz,int64_t dist) {
        m.queued=false;
        // A reach kind (attack, guard) never settles: its owner ends the
        // approach, in reach (AR-06).
        if(m.approach||m.goal<0||!policy(m.kind).completes)return false;
        const Field* f=g.field&&g.field->done?g.field.get():nullptr;
        if(!f)return false;
        const int W=width(),fx=u.type->footX,fz=u.type->footZ,foot=std::max(fx,fz);
        const int64_t body=int64_t(foot)*16;
        const uint16_t potential=f->at(size_t(oz*W+ox));
        if(potential==kUnreached)return false;
        const int64_t px=int64_t(std::get<2>(m.point)),pz=int64_t(std::get<3>(m.point));
        const bool area=policy(m.kind).area,waypoint=policy(m.kind).passThrough;
        auto sameDestination=[&](const std::tuple<int,uint32_t,int32_t,int32_t>& q) {
            if(std::get<0>(q)!=u.player)return false;
            const int64_t ax=(int64_t(std::get<2>(q))-px)>>16,az=(int64_t(std::get<3>(q))-pz)>>16;
            return ax*ax+az*az<=4*body*body;
        };
        auto nearer=[&](const Unit& other) {
            const int qx=footprintOrigin(other.x,fx),qz=footprintOrigin(other.z,fz);
            if(qx>=0&&qz>=0&&qx<W&&qz<height()) {
                const uint16_t q=f->at(size_t(qz*W+qx));
                if(q!=kUnreached)return q<potential;
            }
            const int64_t dx=(int64_t(other.x.v)-px)>>16,dz=(int64_t(other.z.v)-pz)>>16;
            return dx*dx+dz*dz<dist*dist;
        };
        // Nearer the point: by the field's potential at the other body's
        // origin where the field reaches it, else by pixel distance.
        bool queued=false,foreign=false;uint16_t front=kUnreached;
        const int r=foot<=5?2:1;
        uint64_t ring=0;
        for(int j=-r;j<fz+r&&!foreign&&ring<uint64_t(kSettleRing);++j)for(int i=-r;i<fx+r&&!foreign&&ring<uint64_t(kSettleRing);++i) {
            if(i>=0&&i<fx&&j>=0&&j<fz)continue;
            ++ring;
            const int cx=ox+i,cz=oz+j;
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
            const int32_t o=occAt(size_t(cz)*w.occW_+cx);
            if(!o||o==u.id)continue;
            const Unit* other=w.unit(o);
            if(!other||other->player!=u.player||!other->type||other->type->isStructure()||!nearer(*other))continue;
            if(isAnchor(o)) {
                const auto a=anchors.find(o);
                if(!sameDestination(a->second.point))continue;
                // A settled crowd of another order: one window. Its own
                // order's crowd at this very point is the destination area
                // it settles into (a lattice of per-unit points is not one).
                if(std::get<1>(a->second.point)!=std::get<1>(m.point))foreign=true;
                else if(area&&destination(a->second.point)==destination(m.point)) {
                    queued=true;
                    // The settled row in front (its potential): the queue
                    // behind it may settle one row further back (C29 below).
                    const int qx=footprintOrigin(other->x,fx),qz=footprintOrigin(other->z,fz);
                    if(qx>=0&&qz>=0&&qx<W&&qz<height())
                        if(const uint16_t q=f->at(size_t(qz*W+qx));q!=kUnreached&&(front==kUnreached||q>front))front=q;
                }
            } else if(other->orders.empty())foreign=true;
            else if(area&&!queued) {
                // A waypoint (patrol) arrival never anchors, so its crowd has
                // no settled chain start: there a held member of the same
                // destination nearer the waypoint is the queue itself.
                const Member* peer=member(o);
                queued=peer&&(peer->queued||waypoint)&&peer->state==Holding&&destination(peer->point)==destination(m.point);
            }
        }
        stats.crowdWindowRingCells+=ring;
        // The chain's first link: a shared point's member standing inside
        // the destination area itself is at the crowd.
        if(area&&!queued&&!foreign&&shared(g,m)&&potential<=areaBound(g,m,fx,fz,body))queued=true;
        // A distinct goal's destination area is a body plus 4 px round its
        // point (Retail's goal radius + 4): a member still for a window
        // inside it has arrived, whoever stands on the goal itself (a lattice
        // of per-unit points otherwise wedges bodies one step off their
        // goals for good; the 600-tick wait used to end that).
        if(area&&!shared(g,m)&&dist<=body+4&&!inExitLane(u,body))return true;
        if(!queued&&!foreign)return false;
        // Soft obstacles (an idle crowd standing there) charge kSoftFactor
        // per step in a softened field.
        const int64_t factor=f->softened?kSoftFactor:1;
        auto connected=[&](int64_t d) {return int64_t(potential)<=factor*((d*kDiagonal)/16+int64_t(3*foot*kOrthogonal));};
        const int64_t reach=settleReach(m,body);
        if(foreign&&dist<=reach&&connected(dist)&&!inExitLane(u,body))return true;
        if(!area)return false;
        m.queued=true;
        // Pressed: no free neighbour nearer the point. A body with one free
        // that gained nothing for a whole window all the same (its own
        // steering, toward its slot, takes no step, or steps to and fro)
        // re-chooses its slot, and once its re-choices are spent it counts
        // as pressed.
        const bool press=pressed(u,p,*f,ox,oz,potential);
        if(!press&&stalledFor(m)<kCrowdWindow)return false;
        // A body standing on its own slot keeps it: the re-choice is for a
        // slot walled off by bodies that settled first. Re-choosing from the
        // slot itself took the lowest-potential free cell anywhere in the
        // window and walked the body across the front of its own formation
        // (liftflyers' open run: 8 cells sideways); it settles here instead.
        if(m.rechoices<kRechoices&&m.goal!=oz*W+ox) {
            ++m.rechoices;
            if(rechoose(u,m,g,p,*f,potential)||!press)return false;
        }
        const uint32_t grow=uint32_t(kSettleGrow*foot*kOrthogonal),slack=uint32_t(kSettleSlack*foot*kOrthogonal);
        // C29, measured along the queue: no completion farther from the click
        // than twice the crowd's reach, unless the body touches a settled
        // arrival of its own command nearer the point -- then the settled
        // crowd itself reaches back to it, and it may settle within one row
        // (kSettleSlack bodies of potential) behind that arrival. A queue
        // settles back from the crowd row by row and only while it is
        // contiguous with it (a dead-end corridor's 120-body queue is longer
        // than any straight-line cap); a jam that is not connected to the
        // settled crowd by settled bodies never gains that reach.
        const bool behind=front!=kUnreached&&uint32_t(potential)<=uint32_t(front)+slack;
        uint32_t cap=uint32_t(std::min<int64_t>(0xfffe,factor*((2*reach*kDiagonal)/16+int64_t(3*foot*kOrthogonal))));
        if(behind)cap=std::max(cap,std::min<uint32_t>(0xfffe,uint32_t(front)+slack));
        if(!m.settleP)m.settleP=std::min(cap,areaBound(g,m,fx,fz,body)+slack);
        if(potential<=m.settleP&&(dist<=2*reach||behind)&&connected(dist)&&!inExitLane(u,body))return true;
        m.settleP=std::min(cap,m.settleP+grow);
        return false;
    }
    // A destination several members share (or a formation slot's).
    bool shared(const Group& g,const Member& m) const {
        if(m.slot!=-1)return true;
        const auto peak=g.peak.find(m.requested);
        if(peak!=g.peak.end()&&peak->second>1)return true;
        const Point* pt=m.pt;
        if(!pt) {const auto found=points.find(m.point);pt=found==points.end()?nullptr:&found->second;}
        return pt&&pt->assigned&&pt->limit>0;
    }
    // A step from (ox,oz) down the field (a neighbour origin with lower
    // potential) that is legal on the plane and not onto a soft obstacle of
    // the group's command, whoever moves through it now (see move's B1 test:
    // a body held only on such a way is held by bodies a re-plan does not
    // plan round -- its own, or movers -- and a refresh would not help it).
    bool freeDescent(const Unit& u,const Group& g,const Plane& p,const Field& f,int ox,int oz) {
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
        const uint16_t potential=f.at(size_t(oz*W+ox));
        const uint64_t command=g.soft?g.command:kNoSoft;
        const int counts=!softCellCount||command==kNoSoft?-1:softCountsFor(fx,fz);
        for(const auto& d:kDirections) {
            if(!step(p,ox,oz,d[0],d[1]))continue;
            const uint16_t v=f.at(size_t((oz+d[1])*W+ox+d[0]));
            if(v!=kUnreached&&v<potential&&!softAt(ox+d[0],oz+d[1],fx,fz,command,counts))return true;
        }
        return false;
    }
    // Pressed: no neighbour origin nearer the point (lower potential) that
    // the body could step into now.
    bool pressed(const Unit& u,const Plane& p,const Field& f,int ox,int oz,uint16_t potential) const {
        const int W=width();
        for(const auto& d:kDirections) {
            if(!step(p,ox,oz,d[0],d[1]))continue;
            const uint16_t v=f.at(size_t((oz+d[1])*W+ox+d[0]));
            if(v==kUnreached||v>=potential)continue;
            if(stepFree(u,ox,oz,ox+d[0],oz+d[1]))return false;
        }
        return true;
    }
    // The reach of the crowd settled at this member's destination: four
    // times the packed disc of everyone settled there (and this body), plus
    // two bodies (the crowdSettle reach at b8a4110, its count kept by
    // anchorsAt instead of a walk of every anchor).
    int64_t settleReach(const Member& m,int64_t body) const {
        const auto at=anchorsAt.find(destination(m.point));
        const int64_t count=1+(at==anchorsAt.end()?0:at->second);
        return 4*body*isqrtFloor(uint64_t(count)*100000000/31416)/100+2*body;
    }
    // The destination area's bound: the potential of the last of the
    // ceil(1.25n) footprints nearest the point by field potential (n the
    // members the point was given to), packed as slotsFor packs slots, so
    // it follows walls (a half-disc at a wall, a strip in a dead end). A
    // distinct goal's is a body plus 4 px (Retail's radius + 4). Cached per
    // goal and field: a function of the finished field, never hashed.
    uint32_t areaBound(Group& g,const Member& m,int fx,int fz,int64_t body) {
        const auto peak=g.peak.find(m.requested);
        const int n=peak==g.peak.end()?1:peak->second;
        if(n<=1||m.requested<0)return uint32_t((body+4)*kOrthogonal/16);
        const Field& f=*g.field;
        auto& cached=g.areaBounds[m.requested];
        if(cached.first==f.serial&&cached.second.first==n)return cached.second.second;
        const int count=(5*n+3)/4;
        const int W=width(),H=height(),sx=m.requested%W,sz=m.requested/W,foot=std::max(fx,fz);
        uint32_t bound=0;
        for(int radius=int(isqrtFloor(uint64_t(count)))*foot+2*foot+4,attempt=0;attempt<4;++attempt,radius*=2) {
            const int x0=std::max(0,sx-radius),z0=std::max(0,sz-radius);
            const int x1=std::min(W-1,sx+radius),z1=std::min(H-1,sz+radius);
            std::vector<std::pair<uint16_t,int>> order;
            stats.slotSearchCells+=uint64_t(x1-x0+1)*uint64_t(z1-z0+1);
            const Plane& p=plane(g.plane);
            for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x)
                if(legal(p,x,z)&&f.at(size_t(z*W+x))!=kUnreached)order.push_back({f.at(size_t(z*W+x)),z*W+x});
            std::sort(order.begin(),order.end());
            const int bw=x1-x0+1+fx,bh=z1-z0+1+fz;
            std::vector<uint8_t> used(size_t(bw)*bh,0);
            int packed=0;
            for(const auto& [v,cell]:order) {
                const int x=cell%W-x0,z=cell/W-z0;
                bool clear=true;
                for(int j=0;j<fz&&clear;++j)for(int i=0;i<fx&&clear;++i)clear=!used[size_t(z+j)*bw+x+i];
                if(!clear)continue;
                for(int j=0;j<fz;++j)for(int i=0;i<fx;++i)used[size_t(z+j)*bw+x+i]=1;
                bound=v;
                if(++packed>=count)break;
            }
            if(packed>=count)break;
        }
        cached={f.serial,{n,bound}};
        return bound;
    }
    // Re-choice (C2, C28): the free slot of this member's destination it can
    // still reach past the bodies standing now (breadth-first over legal
    // origins no other body covers, window of 24 cells) with the lowest
    // potential below its own, ties to the lowest cell index, then the
    // lowest slot index. Formation points take any free cell of the area;
    // packed points their free packed slots. Returns whether it took one.
    bool rechoose(const Unit& u,Member& m,Group& g,const Plane& p,const Field& f,uint16_t potential) {
        if(m.requested<0||m.approach)return false;
        Point* pt=m.pt;
        if(!pt) {auto found=points.find(m.point);pt=found==points.end()?nullptr:&found->second;}
        const bool formation=pt&&pt->assigned&&pt->limit>0;
        const int fx=u.type->footX,fz=u.type->footZ;
        Group::Slots* slots=nullptr;
        if(!formation) {
            const auto sharing=g.sharing.find(m.requested);
            if(sharing==g.sharing.end()||sharing->second<2)return false;
            slots=&slotsFor(m,g,p,m.requested,sharing->second,fx,fz);
        }
        constexpr int R=24,S=2*R+1;
        const int W=width();
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        auto open=[&](int x,int z) {
            if(!legal(p,x,z))return false;
            for(int j=0;j<fz;++j)for(int i=0;i<fx;++i) {
                const int cx=x+i,cz=z+j;
                if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
                const int32_t o=occAt(size_t(cz)*w.occW_+cx);
                if(o&&o!=u.id)return false;
            }
            return true;
        };
        // The search never climbs more than a row (kSettleSlack bodies of
        // potential) above the body: a free slot reachable only by walking
        // back round the crowd is no re-choice, and that walk is what made
        // the search cost a whole 49x49 window per call.
        const uint32_t climb=uint32_t(potential)+uint32_t(kSettleSlack*std::max(fx,fz)*kOrthogonal);
        std::vector<uint8_t> seen(size_t(S)*S,0);
        std::vector<int> queue;queue.reserve(size_t(S)*S);
        seen[size_t(R*S+R)]=1;queue.push_back(R*S+R);
        for(size_t head=0;head<queue.size();++head) {
            const int local=queue[head],lx=local%S,lz=local/S,x=ox+lx-R,z=oz+lz-R;
            for(const auto& dd:kDirections) {
                const int nlx=lx+dd[0],nlz=lz+dd[1];
                if(nlx<0||nlz<0||nlx>=S||nlz>=S||seen[size_t(nlz*S+nlx)])continue;
                if(!step(p,x,z,dd[0],dd[1]))continue;
                if(const uint16_t v=f.at(size_t((z+dd[1])*W+x+dd[0]));v==kUnreached||v>climb||!open(x+dd[0],z+dd[1]))continue;
                if(dd[0]&&dd[1]&&(!open(x+dd[0],z)||!open(x,z+dd[1])))continue;
                seen[size_t(nlz*S+nlx)]=1;queue.push_back(nlz*S+nlx);
            }
        }
        stats.rechoiceBfsCells+=queue.size();stats.slotSearchCells+=queue.size();
        auto reached=[&](int cell) {
            const int lx=cell%W-ox+R,lz=cell/W-oz+R;
            return lx>=0&&lz>=0&&lx<S&&lz<S&&seen[size_t(lz*S+lx)];
        };
        int best=-1,bestSlot=-1;uint16_t bestV=potential;
        if(formation) {
            // This member's own cells are free to it.
            if(m.slot>=0)slotCells(m,fx,fz,false);
            const int64_t px=int64_t(std::get<2>(m.point))>>16,pz=int64_t(std::get<3>(m.point))>>16;
            for(const int local:queue) {
                const int x=ox+local%S-R,z=oz+local/S-R,cell=z*W+x;
                const uint16_t v=f.at(size_t(cell));
                if(v==kUnreached||v>bestV||(v==bestV&&(best<0||cell>best)))continue;
                if(v==potential)continue;
                const int64_t cx=int64_t(x)*16+fx*8,cz=int64_t(z)*16+fz*8;
                if((cx-px)*(cx-px)+(cz-pz)*(cz-pz)>pt->limit*pt->limit)continue;
                bool clear=true;
                for(int j=0;j<fz&&clear;++j)for(int i=0;i<fx&&clear;++i)clear=!pt->cells.count((z+j)*W+x+i);
                if(clear) {best=cell;bestV=v;}
            }
            if(best<0||best==m.goal) {if(m.slot>=0)slotCells(m,fx,fz,true);return false;}
            m.goal=best;m.slot=0;m.lineCell=-1;markPass(u,m);
            slotCells(m,fx,fz,true);
            return true;
        }
        for(size_t i=0;i<slots->cells.size();++i) {
            const int cell=slots->cells[i];
            if(slots->taken[i]||int(i)==m.slot)continue;
            const uint16_t v=f.at(size_t(cell));
            if(v==kUnreached||v>=potential)continue;
            if(best>=0&&(v>bestV||(v==bestV&&cell>=best)))continue;
            if(!legal(p,cell%W,cell/W)||!reached(cell)||!slotFree(m,cell,fx,fz))continue;
            best=cell;bestSlot=int(i);bestV=v;
        }
        if(best<0)return false;
        if(m.slot>=0) {
            if(size_t(m.slot)<slots->taken.size())slots->taken[size_t(m.slot)]=0;
            slotCells(m,fx,fz,false);
        }
        slots->taken[size_t(bestSlot)]=1;m.slot=bestSlot;m.goal=best;m.lineCell=-1;markPass(u,m);
        slotCells(m,fx,fz,true);
        return true;
    }
    // Factory exit lanes (from the factory's centre to past where its output
    // is put down), per static epoch: a settle rule never settles a body in
    // one of its own player's. Derived from the structures, never hashed.
    struct ExitLane {int id=0;int64_t x=0,z=0,hx=0,hz=0;};
    std::vector<ExitLane> exitLanes;uint64_t exitLanesEpoch=~0ull;
    bool inExitLane(const Unit& u,int64_t body) {
        if(exitLanesEpoch!=epoch) {
            exitLanes.clear();exitLanesEpoch=epoch;
            for(const auto& s:w.units_)if(s.alive()&&s.type&&s.type->producesUnits())
                exitLanes.push_back({s.id,s.x.v>>16,s.z.v>>16,int64_t(s.type->footX)*8,int64_t(s.type->footZ)*8+60});
            stats.crowdSettleVisits+=w.units_.size();
        }
        const int64_t ux=u.x.v>>16,uz=u.z.v>>16;
        for(const auto& l:exitLanes) {
            if(ux<l.x-l.hx-body||ux>l.x+l.hx+body||uz<l.z||uz>l.z+l.hz+body)continue;
            const Unit* s=w.unit(l.id);
            if(s&&s->alive()&&s->player==u.player)return true;
        }
        return false;
    }
    // The holder rule (AR-11, PLAN section 0 row 11: an unreachable order
    // drops as soon as the unit reaches its nearest reachable spot). An
    // approach member that has gained nothing on its point for
    // kApproachSettle ticks and touches (within one body) a body of its own
    // selection (oneSelection) that already stopped nearer its approach
    // point -- one that arrived there (Trapped, its order about to drop) or
    // whose order already dropped (approachDone) -- stands at the nearest
    // spot it can reach: the way on is filled by its own army, which will
    // not move again. A body still gaining on its point never settles, and a
    // body of another selection, or one of its own still walking, never
    // settles it, so neither a moving queue nor a long walk is cut short.
    bool approachSettled(const Unit& u,const Member& m) const {
        const int W=width(),fx=u.type->footX,fz=u.type->footZ,foot=std::max(fx,fz);
        const int64_t px=centre(m.goal%W,fx).v,pz=centre(m.goal/W,fz).v;
        const int64_t dx=(int64_t(u.x.v)-px)>>16,dz=(int64_t(u.z.v)-pz)>>16;
        const int64_t mine=dx*dx+dz*dz;
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        uint64_t ring=0;bool found=false;
        for(int j=-foot;j<fz+foot&&!found;++j)for(int i=-foot;i<fx+foot&&!found;++i) {
            ++ring;
            const int cx=ox+i,cz=oz+j;
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
            const int32_t o=occAt(size_t(cz)*w.occW_+cx);
            if(!o||o==u.id)continue;
            const Unit* other=w.unit(o);
            if(!other||other->player!=u.player)continue;
            bool own=false;
            if(const auto d=approachDone.find(o);d!=approachDone.end())own=other->orders.empty()&&oneSelection(d->second,m.point);
            else if(const Member* peer=member(o))own=peer->approach&&peer->state==Trapped&&oneSelection(peer->point,m.point);
            if(!own)continue;
            const int64_t qx=(int64_t(other->x.v)-px)>>16,qz=(int64_t(other->z.v)-pz)>>16;
            found=qx*qx+qz*qz<mine;
        }
        stats.crowdWindowRingCells+=ring;
        return found;
    }
    // A body pressed against settled bodies on its own distinct goal has
    // arrived: the goal is taken and the cells nearer it are filled. A
    // shared point's members (several sent there, or a formation slot)
    // settle by the settle rule alone (settleWindow).
    bool contactArrival(const Unit& u,const Member& m) const {
        if(stalledFor(m)<20||m.approach||m.slot!=-1||!policy(m.kind).completes)return false;
        auto group=groups.find(m.group);
        if(group==groups.end())return false;
        const auto sharing=group->second.peak.find(m.requested);
        if(sharing!=group->second.peak.end()&&sharing->second>1)return false;
        if(const Point* pt=m.pt?m.pt:[&]()->const Point* {const auto f=points.find(m.point);return f==points.end()?nullptr:&f->second;}();
           pt&&pt->assigned&&pt->limit>0)return false;
        const int body=std::max(u.type->footX,u.type->footZ)*16;
        const int W=width();
        const Fixed gx=centre(m.goal%W,u.type->footX),gz=centre(m.goal/W,u.type->footZ);
        const int64_t dx=(int64_t(u.x.v)-gx.v)>>16,dz=(int64_t(u.z.v)-gz.v)>>16;
        if(dx*dx+dz*dz>int64_t(body)*body)return false;
        // A distinct goal is only "full" if another body stands on it;
        // otherwise settling short could plug the lane a neighbour needs.
        bool taken=false;
        const int gx0=m.goal%W,gz0=m.goal/W;
        for(int j=0;j<u.type->footZ&&!taken;++j)for(int i=0;i<u.type->footX&&!taken;++i) {
            const int cx=gx0+i,cz=gz0+j;
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
            const int32_t o=occAt(size_t(cz)*w.occW_+cx);
            taken=o&&o!=u.id;
        }
        if(!taken)return false;
        // Only settled neighbours (idle, or already arrived) make it full.
        const int fx=u.type->footX,fz=u.type->footZ;
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        for(int j=-1;j<=fz;++j)for(int i=-1;i<=fx;++i) {
            const int cx=ox+i,cz=oz+j;
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
            const int32_t o=occAt(size_t(cz)*w.occW_+cx);
            if(!o||o==u.id)continue;
            const Unit* other=w.unit(o);
            if(!other||other->player!=u.player)continue;
            if(other->orders.empty()||other->orders[World::currentLeg(other->orders)].mission.pending&0x500)return true;
        }
        return false;
    }
    uint64_t checksum() const {
        uint64_t h=mix(0x4c4547494f4eull,epoch);
        h=mix(h,planeDebt);h=mix(h,uint64_t(pruneCursor));
        h=mix(h,uint64_t(nextGroup));
        for(const auto& [id,g]:groups) {
            h=mix(h,uint64_t(id));h=mix(h,uint64_t(g.player));h=mix(h,g.issuedTick);
            h=mix(h,uint64_t(uint32_t(g.minX))<<32|uint32_t(g.minZ));h=mix(h,uint64_t(uint32_t(g.maxX))<<32|uint32_t(g.maxZ));
            h=mix(h,uint64_t(uint32_t(g.compCell)));h=mix(h,g.stale);h=mix(h,g.approach);h=mix(h,g.command);h=mix(h,g.soft);
            if(g.kind!=Kind::Move)h=mix(h,uint64_t(g.kind));
            for(const auto& [seed,count]:g.sharing) {h=mix(h,uint64_t(seed));h=mix(h,uint64_t(count));}
            if(g.next) {h=mix(h,g.next->work);h=mix(h,g.next->done);h=mix(h,g.next->epoch);h=mix(h,g.next->started);}
            h=mix(h,uint64_t(g.members));h=mix(h,uint64_t(g.plane));
            for(int s:g.seeds)h=mix(h,uint64_t(s));
            for(const auto& [seed,n]:g.peak) {h=mix(h,uint64_t(seed));h=mix(h,uint64_t(n));}
            h=mix(h,g.lastUse);
            h=mix(h,g.built);
            if(g.field) {h=mix(h,g.field->work);h=mix(h,g.field->done);h=mix(h,g.field->epoch);h=mix(h,g.field->current);h=mix(h,g.field->bounded);h=mix(h,g.field->started);}
            for(size_t i=0;i<g.avoidCmd.size();++i) {
                h=mix(h,g.avoidCmd[i]);h=mix(h,g.avoidOff[i]);const auto& c=g.avoidSeg[i];
                h=mix(h,uint64_t(c.x0)^uint64_t(c.z0)<<20^uint64_t(c.x1)<<40);h=mix(h,uint64_t(c.z1)^uint64_t(c.r)<<32);
            }
            h=mix(h,uint64_t(uint32_t(g.bodyMinX))<<32|uint32_t(g.bodyMinZ));h=mix(h,uint64_t(uint32_t(g.bodyMaxX))<<32|uint32_t(g.bodyMaxZ));h=mix(h,g.full);h=mix(h,g.waited);
            if(g.demand)h=mix(h,0x64656d616e64ull);
            if(g.softReplanTick!=kNoTick) {h=mix(h,0x7265706c616eull);h=mix(h,g.softReplanTick);}
            if(g.reachTarget) {h=mix(h,0x7265616368ull);h=mix(h,uint64_t(uint32_t(g.reachTarget))<<32|uint32_t(g.reachBucket));h=mix(h,uint64_t(uint32_t(g.reachCentre)));}
            if(g.ring.tried) {
                h=mix(h,0x72696e67ull);h=mix(h,uint64_t(g.ring.assigned)|uint64_t(uint32_t(g.ring.R))<<8);h=mix(h,uint64_t(g.ring.band0));
                for(size_t i=0;i<g.ring.cells.size();++i)h=mix(h,uint64_t(uint32_t(g.ring.cells[i]))<<32|uint64_t(uint32_t(g.ring.owner[i]))<<4|g.ring.band[i]);
            }
            for(const auto& [seed,slot]:g.slots) {
                h=mix(h,uint64_t(seed));h=mix(h,slot.built);h=mix(h,slot.reach);h=mix(h,slot.stale);
                for(size_t i=0;i<slot.cells.size();++i)h=mix(h,uint64_t(slot.cells[i])<<1|slot.taken[i]);
            }
        }
        for(const auto& [key,point]:points) {
            h=mix(h,uint64_t(std::get<0>(key)));h=mix(h,std::get<1>(key));
            h=mix(h,uint32_t(std::get<2>(key)));h=mix(h,uint32_t(std::get<3>(key)));h=mix(h,uint64_t(point.refs));h=mix(h,point.tried);
            if(point.assigned) {h=mix(h,uint64_t(point.centreX));h=mix(h,uint64_t(point.centreZ));
                h=mix(h,uint64_t(point.scaleNum));h=mix(h,uint64_t(point.scaleDen));h=mix(h,uint64_t(point.limit));}
            for(int c:point.cells)h=mix(h,uint64_t(c));
            if(point.awareSeen) {h=mix(h,uint64_t(point.awareX));h=mix(h,uint64_t(point.awareZ));}
            if(!point.queue.empty()) {h=mix(h,0x71756575ull);for(const int id:point.queue)h=mix(h,uint64_t(id));}
        }
        for(const auto& [id,a]:anchors) {
            h=mix(h,uint64_t(id));h=mix(h,uint64_t(a.goal));h=mix(h,a.yields);
            h=mix(h,uint64_t(std::get<0>(a.point)));h=mix(h,std::get<1>(a.point));
            h=mix(h,uint32_t(std::get<2>(a.point)));h=mix(h,uint32_t(std::get<3>(a.point)));
        }
        for(const auto& [id,y]:yielding) {h=mix(h,uint64_t(id));h=mix(h,uint64_t(y.cell));h=mix(h,y.ticks);}
        h=mix(h,softSerial);h=mix(h,softHash);
        for(size_t id=0;id<softBodies.size();++id)if(const SoftBody& st=softBodies[id];st.present) {
            h=mix(h,uint64_t(id));h=mix(h,uint64_t(uint32_t(st.x))<<32|uint32_t(st.z));h=mix(h,st.scans);
        }
        for(const auto& [id,c]:approachDone) {
            h=mix(h,0x61646f6e65ull);h=mix(h,uint64_t(id));h=mix(h,uint64_t(uint32_t(std::get<0>(c)))<<32|std::get<1>(c));
            h=mix(h,uint64_t(uint32_t(std::get<2>(c)))<<32|uint32_t(std::get<3>(c)));
        }
        for(const auto& [id,n]:parts) {h=mix(h,uint64_t(id));h=mix(h,n.count);h=mix(h,uint64_t(uint8_t(n.sx))|uint64_t(uint8_t(n.sz))<<8);}
        for(const auto& [id,m]:members) {
            h=mix(h,uint64_t(id));h=mix(h,m.controller);h=mix(h,uint64_t(m.group));h=mix(h,uint64_t(m.goal));
            h=mix(h,uint64_t(m.lineCell));h=mix(h,m.line);h=mix(h,m.state);h=mix(h,m.best);
            h=mix(h,m.heldSince);h=mix(h,m.stallTick);h=mix(h,m.holdUpdates);h=mix(h,m.progress);h=mix(h,uint64_t(m.requested));
            h=mix(h,m.rest);if(m.rest) {h=mix(h,uint32_t(m.restX));h=mix(h,uint32_t(m.restZ));h=mix(h,uint32_t(m.restHp));h=mix(h,m.restEpoch);h=mix(h,m.restRing);}
            h=mix(h,uint64_t(m.slot));h=mix(h,uint64_t(m.detour));h=mix(h,m.detourTicks);h=mix(h,m.detourFace);h=mix(h,m.detourPass);h=mix(h,m.passUntil);h=mix(h,uint64_t(m.detourBest));h=mix(h,uint64_t(uint8_t(m.passRX))|uint64_t(uint8_t(m.passRZ))<<8);
            h=mix(h,m.trappedSince);h=mix(h,m.trappedEpoch);h=mix(h,m.windowTick);h=mix(h,uint64_t(m.windowDist));h=mix(h,m.stillSince);
            if(m.settleP||m.queued||m.rechoices) {h=mix(h,0x736574746c65ull);h=mix(h,m.settleP);h=mix(h,uint64_t(m.queued)|uint64_t(m.rechoices)<<8);}
            h=mix(h,m.approach);h=mix(h,m.approachSince);h=mix(h,m.approachEpoch);h=mix(h,uint64_t(m.real));
            if(m.gainTick) {h=mix(h,0x6761696eull);h=mix(h,m.gainTick);h=mix(h,m.gainBest);}
            h=mix(h,uint64_t(std::get<0>(m.point)));h=mix(h,std::get<1>(m.point));
            h=mix(h,uint32_t(std::get<2>(m.point)));h=mix(h,uint32_t(std::get<3>(m.point)));
            if(std::get<0>(m.point)>=0&&m.part!=std::get<1>(m.point)) {h=mix(h,0x70617274ull);h=mix(h,m.part);}
            if(m.pivotR||m.pivotCell>=0) {h=mix(h,uint64_t(uint16_t(m.pivotR)));h=mix(h,uint64_t(uint32_t(m.pivotCell))<<32|uint32_t(m.pivotAim));}
            h=mix(h,m.routeTicks);h=mix(h,m.nextDetour);h=mix(h,m.detourCount);for(int c:m.route)h=mix(h,uint64_t(c));
            if(m.kind!=Kind::Move) {
                h=mix(h,uint64_t(m.kind));h=mix(h,uint64_t(uint32_t(m.seedX))<<32|uint32_t(m.seedZ));h=mix(h,m.seededAt);
            }
        }
        return h;
    }
};

LegionNavigator::LegionNavigator(World& w):impl_(std::make_unique<Impl>(w)) {}
LegionNavigator::~LegionNavigator()=default;
bool LegionNavigator::supports(const Unit& u) const {return impl_->supports(u);}
LegionMission LegionNavigator::mission(const Unit& u) const {return impl_->kindOf(u);}
void LegionNavigator::registerMove(Unit& u) {impl_->registerMove(u);}
void LegionNavigator::cancel(int id) {impl_->leave(id);}
void LegionNavigator::ordersChanged(int id) {impl_->validate(id);}
void LegionNavigator::tick() {impl_->tick();}
void LegionNavigator::move(Unit& u,Fixed maximum) {impl_->move(u,maximum);impl_->markPass(u.id);}
uint64_t LegionNavigator::checksum() const {return impl_->checksum();}
LegionNavigator::Stats LegionNavigator::stats() const {
    auto s=impl_->stats;
    s.bytes=0;
    for(const auto& p:impl_->planes)s.bytes+=p.legal.capacity();
    for(const auto& [id,g]:impl_->groups) {
        s.bytes+=g.seeds.capacity()*sizeof(int);
        if(g.field) {
            s.bytes+=g.field->potential.capacity()*sizeof(uint16_t);
            for(const auto& b:g.field->buckets)s.bytes+=b.capacity()*sizeof(int);
        }
    }
    s.bytes+=impl_->members.size()*(sizeof(Impl::Member)+48);
    s.liveGroups=impl_->groups.size();s.liveMembers=impl_->members.size();
    s.livePoints=impl_->points.size();s.liveFields=impl_->liveFields();
    return s;
}
bool LegionNavigator::staticLegal(const Unit& u,int x,int z) {
    if(!u.type)return false;
    impl_->syncStatic();
    const int index=impl_->planeFor(*u.type);
    return impl_->legal(impl_->plane(index),x,z);
}
bool LegionNavigator::planeMatchesRebuild(const Unit& u) {
    if(!u.type)return false;
    impl_->syncStatic();
    const int index=impl_->planeFor(*u.type);
    const auto& p=impl_->plane(index);
    Impl::Plane fresh=p;
    impl_->buildPlane(fresh);impl_->labelPlane(fresh);
    if(fresh.legal!=p.legal)return false;
    // Same partition up to numbering, and the same size and box per part.
    std::map<int,int> to,from;
    for(size_t c=0;c<p.comp.size();++c) {
        const int a=p.comp[c],b=fresh.comp[c];
        if((a<0)!=(b<0))return false;
        if(a<0)continue;
        if(to.try_emplace(a,b).first->second!=b||from.try_emplace(b,a).first->second!=a)return false;
    }
    for(const auto& [a,b]:to)
        if(p.compSize[size_t(a)]!=fresh.compSize[size_t(b)]||p.compBox[size_t(a)]!=fresh.compBox[size_t(b)])return false;
    int live=0;
    for(int n:p.compSize)live+=n>0;
    return live==int(to.size())&&live==p.liveComps&&fresh.liveComps==int(to.size());
}
int LegionNavigator::unitState(int id) const {
    const auto found=impl_->members.find(id);
    return found==impl_->members.end()?0:int(found->second.state);
}
int LegionNavigator::recordsForTest(int id) const {
    return (impl_->anchors.count(id)?1:0)|(impl_->approachDone.count(id)?2:0)|(impl_->parts.count(id)?4:0)|(impl_->yielding.count(id)?8:0);
}
int LegionNavigator::unitGroup(int id) const {
    const auto found=impl_->members.find(id);
    return found==impl_->members.end()?0:found->second.group;
}
void LegionNavigator::noteEngaged(const Unit& u) {impl_->engaged(u);}
LegionNavigator::ClaimsAudit LegionNavigator::claimsAudit() const {return impl_->claimsAudit();}
int LegionNavigator::routeLength(int id) const {
    const auto found=impl_->members.find(id);
    return found==impl_->members.end()?0:int(found->second.route.size());
}
}
namespace tak::sim {
int LegionNavigator::fieldPotential(int id,int x,int z) const {
    const auto found=impl_->members.find(id);
    if(found==impl_->members.end())return -1;
    const auto group=impl_->groups.find(found->second.group);
    if(group==impl_->groups.end()||!group->second.field||!group->second.field->done)return -1;
    if(x<0||z<0||x>=impl_->width()||z>=impl_->height())return -1;
    return group->second.field->at(size_t(z)*impl_->width()+x);
}
LegionNavigator::SlotShape LegionNavigator::slotShape(int id) const {
    const auto* m=impl_->member(id);
    return m&&m->pt?m->pt->shape:SlotShape{};
}
LegionNavigator::SlotShape LegionNavigator::lastSlotShape() const {return impl_->lastShape;}
void LegionNavigator::setRestStrideForTest(int stride) {gRestStride=uint32_t(std::max(stride,1));}
void LegionNavigator::setVerify(bool on) {gVerify=on;}
bool LegionNavigator::verifying() {return gVerify;}
void LegionNavigator::setProbe(uint32_t ticks) {gProbe=ticks;}
}
