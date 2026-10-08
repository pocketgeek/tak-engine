#include "legion.h"
#include "sim.h"
#include "footprint.h"
#include "retailplacement.h"
#include <algorithm>
#include <array>
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
// A body held this many updates in a row (past every early reaction:
// yields, first detours) is fully re-evaluated only every kRestStride ticks,
// staggered by unit id, unless something around it changed (heldRest).
constexpr uint32_t kRestAfter=60;
constexpr uint32_t kRestStride=2;
constexpr size_t kMaxFields=48,kMaxPlanes=24;     // field budget: kMaxFields whole maps of cells
constexpr size_t kMaxFieldCount=1024;             // live fields (and in-progress rebuilds), any size
constexpr uint32_t kFieldTenure=300;          // ticks a field is safe from eviction
constexpr uint32_t kTrappedRetire=9000;        // ticks (5 min) a trapped order waits for terrain to open
constexpr int kClusterCells=16;
constexpr int kFieldMargin=32;                 // bounded field window margin (cells), at least
constexpr int kHeadingCells=8;                 // pending-field heading probe (cells)
constexpr size_t kGroupSeeds=256;              // distinct goal origins per group field
constexpr uint32_t kAreaSettle=300;            // ticks a shared-point member may stand still in the area before it settles there
constexpr uint32_t kFarSettle=1800;            // still ticks before a body pressed against its crowd beyond the crowd's reach settles there
constexpr uint32_t kCrowdWindow=45;            // no-progress window for "close enough" settling at a crowd
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
        // popped one.
        std::array<std::vector<int>,64> buckets;
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
        bool softened=false;   // some step was charged as a soft obstacle
        std::vector<int> seeds;   // the group's seeds when the build started
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
    };
    enum State : uint8_t {None=0,Moving=1,Holding=2,Waiting=3,Arrived=4,Trapped=5};
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
        uint32_t held=0,stalled=0,progress=0xffffffffu;
        uint32_t trappedSince=0;uint64_t trappedEpoch=0;
        // The order's goal is statically unreachable from this body's region:
        // `goal` is the nearest reachable point to it, walked to and held
        // (order kept) until a static change re-resolves the real goal.
        bool approach=false;uint32_t approachSince=0;uint64_t approachEpoch=0;
        int real=-1;                      // the unreachable goal origin an approach member stands in for
        int requested=-1;                 // the goal origin the order named
        int slot=-1;                      // claimed slot index (shared goals)
        int detour=-1;                    // committed side-step cell
        uint32_t detourTicks=0;
        bool detourFace=true;             // side-step turns the body (keep-right) or not (shuffle)
        bool detourPass=false;            // side-step is a lane-discipline pass (moves at travel speed)
        uint32_t passUntil=0;             // tick until which a passing body keeps its new lane
        int8_t passRX=0,passRZ=0;         // the side it moved over to
        std::tuple<int,uint32_t,int32_t,int32_t> point{}; // player, issue tick, requested point
        Point* pt=nullptr;         // points[point]: alive while this member holds its ref
        std::vector<int> route;           // committed local detour around still bodies
        // Pinwheel (see pivotAim): the member's distance off a wall end it
        // rounds (cells; 0 not yet measured, -1 hugs as the inner file), and
        // the arc cell it aims at from origin pivotCell.
        int16_t pivotR=0;int pivotCell=-1,pivotAim=-1;
        uint32_t routeTicks=0,nextDetour=8,detourCount=0;
        int64_t detourBest=-1;            // progress when the last local detour was planned
        // "Close enough": the start of the current no-progress window and the
        // pixel distance to the requested point then (see crowdSettle).
        uint32_t windowTick=0;int64_t windowDist=-1;uint8_t stillWindows=0;
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
        if(slot.pt) {
            auto& ids=slot.pt->ids;
            const auto at=std::lower_bound(ids.begin(),ids.end(),id);
            if(at==ids.end()||*at!=id)ids.insert(at,id);
        }
    }
    // Settled Legion arrivals: the goal origin each one completed on, and how
    // many times it has stepped aside since. A settled body may yield one
    // cell (never farther than one cell from that goal origin, so it stays
    // inside its destination area) to open a lane for a same-player member
    // whose own goal it walls in. Capped per body: no endless shuffling.
    struct Anchor {int goal=-1;uint8_t yields=0;std::tuple<int,uint32_t,int32_t,int32_t> point{};};
    std::map<int,Anchor> anchors;
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
    // round a block wider than its window). Every kStillScan ticks the
    // ground bodies are sampled; one at the same exact position on
    // kStillScans consecutive scans is still. A still body that is not a
    // Legion member, or is a settled Legion arrival, stamps its cells into
    // `soft` (any player: idle, building, guarding, an enemy's). Moving
    // crowds never qualify: every Legion member on its way (moving, held,
    // waiting, trapped behind a gate) is excluded, and anything else must
    // not have moved at all for a whole scan. A body that sets off as a
    // Legion member stops counting at once (registerMove clears its cells);
    // one ordered off otherwise stops at the next scan. A field already
    // built keeps its route: no re-planning when the block starts to move.
    struct Still {int32_t x=0,z=0;uint16_t scans=0;};
    std::vector<std::pair<int,Still>> stills;   // unit id -> last sampled position, ascending ids
    std::vector<int32_t> soft;           // per cell: a soft body covering it (0 none)
    std::vector<uint8_t> softKind;       // per cell: 0 none, 1 soft to all, 2 a settled arrival (see softCell)
    std::vector<int> softCells;          // cells set in `soft`, ascending
    std::vector<uint8_t> softPrior;      // scanStill scratch: a cell's kind before the rescan (all 0 between scans)
    // Per unit id: the command (player, issue tick) a settled Legion arrival
    // belongs to, else ~0: a field never treats its own arrivals as soft.
    std::vector<uint64_t> softOwner;
    uint64_t softSerial=0,softHash=0;    // bumped whenever `soft` changes; its content hash
    // Landed flyers (mode 1) stand on the ground: retail stamps them into
    // its ground grid (5066f0, re-stamped by 51b370/4dafd2 on every mode or
    // cell change), so mobilePlacement refuses a ground step into one. But
    // World::occ_ leaves every flyer out, so Legion, which reads occ_ for
    // who stands where (holds, detours, parting, passing), saw free ground
    // there and pressed into them. `grounded` overlays them on occ_ (see
    // occAt); rebuilt each tick from hashed unit state, so never hashed.
    std::vector<int32_t> grounded;       // per cell: a landed flyer covering it (0 none)
    std::vector<int> groundedCells,groundedIds;   // cells set; landed flyer ids, ascending
    int32_t occAt(size_t c) const {const int32_t o=w.occ_[c];return o||groundedCells.empty()?o:grounded[c];}
    void stampGrounded() {
        if(w.occW_<=0)return;
        const size_t n=size_t(w.occW_)*w.occH_;
        if(grounded.size()!=n) {grounded.assign(n,0);groundedCells.clear();}
        for(int c:groundedCells)grounded[size_t(c)]=0;
        groundedCells.clear();
        std::vector<int> ids;
        for(const auto& u:w.units_) {
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
        // A flyer that took off is no soft obstacle any more (as a body
        // setting off as a member, see registerMove): clear it at once,
        // not at the next scan.
        if(!softCells.empty())for(int id:groundedIds)if(!std::binary_search(ids.begin(),ids.end(),id))
            for(int c:softCells)if(soft[size_t(c)]==id&&softKind[size_t(c)]) {countAll(c,softKind[size_t(c)],-1);softKind[size_t(c)]=0;}
        groundedIds.swap(ids);
    }
    static uint64_t commandKey(int player,uint32_t issue) {return uint64_t(uint32_t(player))<<32|issue;}
    static constexpr uint64_t kNoSoft=~0ull-1;   // a field / line that ignores soft obstacles
    // Is soft cell c an obstacle to `command`? Kind 1 stands for every
    // field; kind 2 (a settled Legion arrival) not for its own command.
    bool softCell(size_t c,uint64_t command) const {
        const uint8_t k=softKind[c];
        if(k!=2)return k!=0;
        const int32_t o=soft[c];
        return !(size_t(o)<softOwner.size()&&softOwner[size_t(o)]==command);
    }
    // Does a footprint at origin (x,z) cover a soft body other than the
    // command's own arrivals? `counts` (the footprint's window counts, see
    // SoftCounts; -1 none) answers without a walk unless a settled arrival
    // is near: none covered, or only bodies soft to every command.
    bool softAt(int x,int z,int fx,int fz,uint64_t command,int counts=-1) const {
        if(softCells.empty()||command==kNoSoft)return false;
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
    int softLevel(int x,int z,int fx,int fz,uint64_t command,int counts,bool own=true) const {
        if(softCells.empty()||command==kNoSoft)return 0;
        // The field asks this for every improving relaxation: answered from
        // the footprint's window counts, walking the window only near a
        // settled arrival (whose command matters).
        if(counts>=0) {
            const uint32_t k=softCounts[size_t(counts)].at[size_t(z)*w.occW_+x];
            if(!k)return 0;
            // No settled arrival of this command stands anywhere: every
            // counted cell is an obstacle to it.
            if(!own||!(k&kArrivalCount))return k&kCoverCount?2:1;
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
    // cell, byte 2 the settled-arrival cells among the latter (a window
    // holds at most 49 cells). Derived from `soft`/`softKind` and kept up
    // to date cell by cell, never hashed.
    struct SoftCounts {int fx=0,fz=0;std::vector<uint32_t> at;};
    static constexpr uint32_t kCoverCount=0xff,kArrivalCount=0xff0000;
    std::vector<SoftCounts> softCounts;
    void countCell(SoftCounts& k,int c,uint8_t kind,int delta) {
        const int W=w.occW_,H=w.occH_,cx=c%W,cz=c/W;
        for(int oz=std::max(0,cz-k.fz);oz<=std::min(H-1,cz+1);++oz)for(int ox=std::max(0,cx-k.fx);ox<=std::min(W-1,cx+1);++ox) {
            uint32_t& word=k.at[size_t(oz)*W+ox];
            if(kind==2)word=uint32_t(int64_t(word)+(int64_t(delta)<<16));
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
        for(int c:softCells)if(softKind[size_t(c)])countCell(k,c,softKind[size_t(c)],1);
        softCounts.push_back(std::move(k));
        return int(softCounts.size()-1);
    }
    void scanStill() {
        if(w.tickCounter_%kStillScan!=0||w.occW_<=0)return;
        std::vector<std::pair<int,Still>> next;next.reserve(stills.size()+16);
        std::vector<int> cells;
        std::vector<uint64_t> owner;
        const size_t n=size_t(w.occW_)*w.occH_;
        if(soft.size()!=n) {
            soft.assign(n,0);softKind.assign(n,0);softCells.clear();softCounts.clear();
        }
        std::vector<std::pair<int,int32_t>> stamps;
        for(const auto& u:w.units_) {
            if(!u.alive()||u.embarked()||!u.type||(u.type->canFly&&u.flightGroundMode!=1)||u.type->isStructure())continue;
            const Member* m=member(u.id);
            if(m&&m->state!=Arrived)continue;
            Still s;s.x=u.x.v;s.z=u.z.v;
            const auto old=std::lower_bound(stills.begin(),stills.end(),u.id,[](const auto& e,int id) {return e.first<id;});
            if(old!=stills.end()&&old->first==u.id&&old->second.x==s.x&&old->second.z==s.z)
                s.scans=uint16_t(std::min<int>(old->second.scans+1,kStillScans));
            next.push_back({u.id,s});
            if(s.scans<kStillScans)continue;
            const int fx=u.type->footX,fz=u.type->footZ;
            const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
            for(int j=0;j<fz;++j)for(int i=0;i<fx;++i) {
                const int cx=ox+i,cz=oz+j;
                if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
                stamps.push_back({cz*w.occW_+cx,u.id});
            }
            if(isAnchor(u.id)) {
                const auto a=anchors.find(u.id);
                if(size_t(u.id)>=owner.size())owner.resize(size_t(u.id)+1,~0ull);
                owner[size_t(u.id)]=commandKey(std::get<0>(a->second.point),std::get<1>(a->second.point));
            }
        }
        std::sort(next.begin(),next.end(),[](const auto& a,const auto& b) {return a.first<b.first;});
        stills.swap(next);
        std::sort(stamps.begin(),stamps.end());
        // The window counts depend on each cell's kind alone, and their
        // updates are additive: a cell soft before and after with the same
        // kind would be removed and added back unchanged. Only cells whose
        // kind changed are counted (in a still crowd that is almost none;
        // re-counting every cell cost ~5-8 ms per scan at ~10k bodies).
        if(softPrior.size()!=n)softPrior.assign(n,0);
        for(int c:softCells) {
            softPrior[size_t(c)]=softKind[size_t(c)];
            soft[size_t(c)]=0;softKind[size_t(c)]=0;
        }
        uint64_t h=0x736f6674;
        for(const auto& [c,id]:stamps)if(!soft[size_t(c)]) {
            soft[size_t(c)]=id;softKind[size_t(c)]=size_t(id)<owner.size()&&owner[size_t(id)]!=~0ull?2:1;
            if(const uint8_t prior=softPrior[size_t(c)];prior!=softKind[size_t(c)]) {
                if(prior)countAll(c,prior,-1);
                countAll(c,softKind[size_t(c)],1);
            }
            softPrior[size_t(c)]=0;
            cells.push_back(c);h=mix(h,uint64_t(c)<<32|uint32_t(id));
        }
        for(int c:softCells)if(const uint8_t prior=softPrior[size_t(c)]) {
            countAll(c,prior,-1);softPrior[size_t(c)]=0;
        }
        for(size_t i=0;i<owner.size();++i)if(owner[i]!=~0ull)h=mix(mix(h,i),owner[i]);
        const bool changed=h!=softHash;
        softHash=h;
        softCells.swap(cells);softOwner.swap(owner);
        if(changed)++softSerial;
    }
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
    // Goal resolution per (plane, requested origin, body region) on the
    // current static epoch: a whole selection clicking one far point (or a
    // re-resolution after a change) searches once, not once per body. Pure
    // function of the plane, so a cache, never hashed; dropped on any epoch
    // change or plane reuse.
    std::map<std::tuple<int,int,int,int>,std::pair<int,int>> resolved;
    uint64_t resolvedEpoch=~0ull;
    Stats stats;
    explicit Impl(World& world):w(world) {}

    int width() const {return w.hW_;}
    int height() const {return w.hH_;}
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
            int victim=-1;
            for(size_t i=0;i<planes.size();++i) {
                bool used=false;
                for(const auto& [id,g]:groups)if(g.plane==int(i)) {used=true;break;}
                if(!used&&(victim<0||planes[i].lastUse<planes[size_t(victim)].lastUse))victim=int(i);
            }
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
        else for(const auto& u:w.units_)stampStructure(u);
        return work;
    }
    bool yardOpen(int id) const {
        const auto script=w.unitScripts_.find(id);
        return script!=w.unitScripts_.end()&&script->second.yardOpen;
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
            !o.transportUnloadTransferDeferred&&!o.transportPassenger&&!o.patrolRepair;
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
            if(const auto at=std::lower_bound(ids.begin(),ids.end(),id);at!=ids.end()&&*at==id)ids.erase(at);
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
            if(--group->second.members<=0)groups.erase(group);
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
        anchors.erase(u.id);markAnchor(u.id,false);yielding.erase(u.id);
        // A body setting off is no soft obstacle any more (its own group's
        // first field is built right now, before the next scan).
        if(!softCells.empty()&&u.type) {
            const int ox=footprintOrigin(u.x,u.type->footX),oz=footprintOrigin(u.z,u.type->footZ);
            for(int j=0;j<u.type->footZ;++j)for(int i=0;i<u.type->footX;++i) {
                const int cx=ox+i,cz=oz+j;
                if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
                const size_t c=size_t(cz)*w.occW_+cx;
                if(soft[c]==u.id&&softKind[c]) {countAll(int(c),softKind[c],-1);softKind[c]=0;}
            }
        }
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
        Member m;m.controller=legKey(leg,kind);m.state=Waiting;m.windowTick=w.tickCounter_;
        m.kind=kind;m.seedX=gx;m.seedZ=gz;m.seededAt=w.tickCounter_;
        // The command a member belongs to: the order's issue tick; every
        // patrol lap on its own; moving goals on the shared re-seed grid.
        const uint32_t issue=rule.moving?w.tickCounter_-w.tickCounter_%kReseedTicks:
            rule.perController?uint32_t(leg.controller):leg.issuedTick;
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
            }
        }
        m.requested=m.goal;
        // A kind without a shared destination area gets a point of its own
        // (keyed by the unit), so no formation or area logic joins it.
        m.point={rule.area?u.player:-1-u.id,rule.area?leg.issuedTick:issue,tx.v,tz.v};
        {auto& pt=points[m.point];++pt.refs;m.pt=&pt;}
        unpin();
        if(m.goal<0) {putMember(u.id,m);return;}   // trapped on first move
        const int x=m.goal%width(),z=m.goal/width();
        const int goalComp=compAt(p,m.goal);
        Group* joined=nullptr;
        for(auto& [id,g]:groups) {
            if(rule.solo)break;
            if(g.player!=u.player||g.issuedTick!=issue||g.plane!=planeIndex||g.kind!=kind)continue;
            // Seeds in different static components never share a field: a
            // member whose goal it cannot reach would descend to a
            // teammate's seed and hold there forever instead of retiring.
            if(groupComp(g,p)!=goalComp)continue;
            // Approach points never share a field with real goals: a member
            // would descend to a stand-in point beside an obstacle instead
            // of its own goal behind it.
            if(g.approach!=m.approach)continue;
            if(x<g.minX-kClusterCells||x>g.maxX+kClusterCells||z<g.minZ-kClusterCells||z>g.maxZ+kClusterCells)continue;
            const bool seeded=std::binary_search(g.seeds.begin(),g.seeds.end(),m.goal);
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
        if(!joined) {
            Group g;g.id=nextGroup++;g.player=u.player;g.plane=planeIndex;g.issuedTick=issue;g.compCell=m.goal;g.approach=m.approach;g.kind=kind;
            g.command=commandKey(u.player,std::get<1>(m.point));g.soft=!u.type->wanders;
            g.minX=g.maxX=x;g.minZ=g.maxZ=z;
            joined=&groups.emplace(g.id,std::move(g)).first->second;
            ++stats.groups;
        }
        auto& g=*joined;
        if(!std::binary_search(g.seeds.begin(),g.seeds.end(),m.goal))
            g.seeds.insert(std::upper_bound(g.seeds.begin(),g.seeds.end(),m.goal),m.goal);
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
        for(int pass=0;pass<2;++pass)for(const auto& [id,o]:groups) {
            if(&o==&g||o.plane!=g.plane)continue;
            for(const auto* d:{&o.field,&o.next}) {
                const Field* f=d->get();
                if(!f||f->done!=(pass==0)||f->epoch!=epoch||w.tickCounter_-f->started>kStillScan)continue;
                if(f->x0>box[0]||f->z0>box[1]||f->x0+f->fw-1<box[2]||f->z0+f->fh-1<box[3])continue;
                if((f->command==kNoSoft)!=(command==kNoSoft)||f->ownArrivals!=own||(own&&f->command!=command))continue;
                if(f->seeds!=g.seeds)continue;
                return *d;
            }
        }
        return nullptr;
    }
    // At the cap a new field may only displace one that has served its
    // group for a while (oldest build first); otherwise the group waits for
    // a slot. Evicting the least recently used field every tick thrashed:
    // all live groups use theirs every tick.
    bool startField(Group& g) {
        const auto box=fieldWindow(g);
        const size_t cells=size_t(box[2]-box[0]+1)*size_t(box[3]-box[1]+1);
        {
            const uint64_t command=g.soft?g.command:kNoSoft;
            const bool own=std::find(softOwner.begin(),softOwner.end(),command)!=softOwner.end();
            if(auto shared=sharedField(g,box,command,own)) {
                g.built=w.tickCounter_;++stats.fieldsShared;
                if(!shared->done)(g.field?g.next:g.field)=std::move(shared);
                else if(g.field) {g.field=std::move(shared);g.next.reset();g.stale=false;restaleSlots(g);}
                else g.field=std::move(shared);
                return true;
            }
        }
        const size_t budget=kMaxFields*size_t(width())*size_t(height());
        while(liveFields()>=kMaxFieldCount||liveFieldCells()+cells>budget) {
            // A victim is a group whose finished field is past its tenure
            // and its own (dropping a shared one frees nothing).
            // Within one tick that set only grows when a field finishes
            // (fieldsDone): evictions and new builds only shrink it.
            if(noVictimTick==w.tickCounter_&&noVictimDone==fieldsDone)return false;
            Group* victim=nullptr;
            for(auto& [id,o]:groups)
                if(o.field&&o.field->done&&o.field.use_count()==1&&w.tickCounter_-o.built>=kFieldTenure&&(!victim||o.built<victim->built))victim=&o;
            if(!victim) {noVictimTick=w.tickCounter_;noVictimDone=fieldsDone;return false;}
            victim->field.reset();victim->next.reset();victim->stale=false;++stats.fieldEvictions;
        }
        g.built=w.tickCounter_;
        auto f=std::make_shared<Field>();
        f->seeds=g.seeds;f->started=w.tickCounter_;
        f->plane=g.plane;f->epoch=epoch;f->serial=++fieldSerial;f->command=g.soft?g.command:kNoSoft;f->softCounts=softCountsFor(planes[size_t(g.plane)].footX,planes[size_t(g.plane)].footZ);
        f->ownArrivals=std::find(softOwner.begin(),softOwner.end(),f->command)!=softOwner.end();
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
        return true;
    }
    // Dial's algorithm on key = potential + heuristic (A*; consistent, so
    // a key never drops below its parent's and rises by at most
    // (kSoftFactor+1)*kDiagonal: 64 circular buckets suffice). Seeds enter
    // in key order.
    uint64_t advance(Field& f,uint64_t budget) {
        const auto& p=planes[size_t(f.plane)];
        const int W=width();uint64_t spent=0;
        while((f.queued||f.seedNext<f.seedKeys.size())&&spent<budget) {
            if(!f.queued&&f.seedKeys[f.seedNext].first>f.current)f.current=f.seedKeys[f.seedNext].first;
            while(f.seedNext<f.seedKeys.size()&&f.seedKeys[f.seedNext].first==f.current) {
                f.buckets[f.current&63].push_back(f.seedKeys[f.seedNext].second);++f.queued;++f.seedNext;
            }
            auto& bucket=f.buckets[f.current&63];
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
                const int level=softLevel(x+d[0],z+d[1],p.footX,p.footZ,f.command,f.softCounts,f.ownArrivals);
                f.softened|=level>0;
                const uint32_t next=g+(level==2?base*kSoftFactor:level==1?base*kSoftNear:base);
                if(next>=kUnreached)continue;   // saturated: beyond the field's range
                if(next<slot) {
                    slot=uint16_t(next);
                    f.buckets[(next+f.heuristic(x+d[0],z+d[1]))&63].push_back((z+d[1])*W+x+d[0]);++f.queued;
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
    std::map<int,Stamp> stamps;
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
            for(const auto& u:w.units_) {
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
        std::map<int,Stamp> now;
        std::vector<const Unit*> structures;
        struct ListScope {const std::vector<const Unit*>*& list;~ListScope(){list=nullptr;}} listScope{structureList};
        for(const auto& u:w.units_) {
            if(!u.alive()||u.embarked()||!u.type||!u.type->isStructure())continue;
            structures.push_back(&u);
            now[u.id]={footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ),
                       u.type->footX,u.type->footZ,yardOpen(u.id)};
        }
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
            // plane: they are rebuilt (see restaleSlots).
            // Only the groups a change reaches (all, without per-plane boxes).
            for(auto& [id,g]:groups) {
                if(changes&&(g.plane<0||size_t(g.plane)>=changes->size()||!fieldTouched(g,(*changes)[size_t(g.plane)])))continue;
                g.next.reset();
                g.stale=g.field!=nullptr;
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
    void tick() {
        syncStatic();
        stampGrounded();
        scanStill();
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
        auto finish=[&](Group& g,Field& f,uint64_t spent) {
            budget-=std::min(budget,spent);stats.fieldWork+=spent;
            if(f.done) {++stats.fieldsBuilt;if(g.next) {g.field=std::move(g.next);++fieldsDone;g.stale=false;restaleSlots(g);}}
        };
        // Groups with a member standing still for want of a field come
        // first (their field starts, or its frontier advances toward them),
        // then first fields whose members all steer already, then refreshes
        // (members still steer by the finished stale field).
        for(auto& [id,g]:groups) {
            if(budget==0)break;
            if(w.tickCounter_-g.waited>1||g.next||(g.field&&g.field->done))continue;
            plane(g.plane);settle();
            if(budget==0)break;
            if(!g.field&&!startField(g))continue;
            if(!g.field->done)finish(g,*g.field,advance(*g.field,budget));   // (done: shared)
        }
        for(int pass=0;pass<2;++pass)for(auto& [id,g]:groups) {
            if(budget==0)break;
            Field* f=pass==1?g.next.get():g.field&&!g.field->done?g.field.get():nullptr;
            if(!f)continue;
            plane(g.plane);settle();
            if(budget==0)break;
            finish(g,*f,advance(*f,budget));
        }
        // Groups that need a field start in id order: first those with none
        // (they cannot steer at all), then stale refreshes. Under constant
        // churn a refresh restarts every tick and never finishes; served
        // first, refreshes of older groups starved a new group forever.
        for(int pass=0;pass<2;++pass)for(auto& [id,g]:groups) {
            if(budget==0)break;
            if((g.field&&(!g.stale||!g.field->done))||g.next)continue;
            if((pass==0)!=(g.field==nullptr))continue;
            plane(g.plane);settle();
            if(budget==0)break;
            if(!startField(g))break;
            Field& f=g.next?*g.next:*g.field;
            if(!f.done)finish(g,f,advance(f,budget));   // (done: shared)
        }
    }
    // Members whose unit died, embarked, lost its orders or left Legion's
    // plain-move domain (no more move() calls) are dropped here, a bounded
    // number per tick in id order, so their groups, slots and point claims
    // are freed. The death edge also cancels directly.
    void prune() {
        constexpr size_t kPrunePerTick=256;
        if(members.empty()) {pruneCursor=0;return;}
        std::vector<int> ids;
        auto it=members.lower_bound(pruneCursor);
        for(size_t n=0;n<kPrunePerTick&&n<members.size();++n) {
            if(it==members.end())it=members.begin();
            ids.push_back(it->first);++it;
        }
        pruneCursor=it==members.end()?0:it->first;
        for(int id:ids) {
            const Unit* u=w.unit(id);
            if(u&&u->alive()&&!u->embarked()&&!u->orders.empty()&&supports(*u))continue;
            leave(id);
        }
    }
    // A settled body that is no longer idle (new order, death) forgets its
    // anchor. Yield steps advance one update per tick in id order: straight
    // toward the target cell, no turning, then a full stop.
    void serviceYields() {
        for(auto it=anchors.begin();it!=anchors.end();) {
            // (The completed leg itself lingers until World retires it.)
            const Unit* u=w.unit(it->first);
            if(!u||!u->alive()||(!u->orders.empty()&&!(u->orders[World::currentLeg(u->orders)].mission.pending&0x500)))
                {yielding.erase(it->first);markAnchor(it->first,false);it=anchors.erase(it);}
            else ++it;
        }
        // A parted body that gets an order (or dies) forgets its partings.
        for(auto it=parts.begin();it!=parts.end();) {
            const Unit* u=w.unit(it->first);
            if(!u||!u->alive()||!u->orders.empty())it=parts.erase(it);else ++it;
        }
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
                auto& part=parts[ids[size_t(q)]];++part.count;
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
            yielding[ids[size_t(k)]]=Yield{cells[size_t(k)],0};
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
        if(!legal(cx,cz))return false;
        const int64_t dx=ex-ax,dz=ez-az;
        const int sx=dx>0?1:-1,sz=dz>0?1:-1;
        for(int guard=0;(cx!=tx||cz!=tz)&&guard<4*kFormationLineCells+8;++guard) {
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
        m.state=Holding;++m.held;
    }
    void complete(Unit& u,Member& m,bool contact) {
        // The nearest reachable point to an unreachable goal is held, not
        // completed: the order waits there for the terrain to open.
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
        if(policy(m.kind).passThrough) {leave(u.id);return;}
        m.state=Arrived;
        anchors[u.id]=Anchor{m.goal,0,m.point};markAnchor(u.id,true);
        leave(u.id);
    }
    void trapped(Unit& u,Member& m) {
        // No legal route exists for this footprint: stop at once (zero speed,
        // constant heading, no probing). The order is kept for a grace
        // period so a gate opening or a wall coming down (a new static
        // epoch) resumes it; after that the leg is retired as Retail retires
        // an unreachable goal. Later queued legs proceed.
        if(m.state!=Trapped) {
            m.state=Trapped;m.trappedSince=m.approach?m.approachSince:w.tickCounter_;m.trappedEpoch=epoch;++stats.trapped;
        }
        u.speed=Fixed();u.turnReqBam=0;
        if(w.tickCounter_-m.trappedSince<kTrappedRetire)return;
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
        int best=-1;int64_t bestD=0;
        for(int r=0;r<=48;++r) {
            if(best>=0&&int64_t(r-1)*16*int64_t(r-1)*16>bestD)break;
            for(int dz=-r;dz<=r;++dz)for(int dx=-r;dx<=r;++dx) {
                if(std::max(std::abs(dx),std::abs(dz))!=r)continue;
                const int x=sx+dx,z=sz+dz;
                if(!legal(p,x,z)||compAt(p,z*W+x)!=comp)continue;
                const int64_t cx=int64_t(x)*16+fx*8,cz=int64_t(z)*16+fz*8;
                if((cx-px)*(cx-px)+(cz-pz)*(cz-pz)>pt.limit*pt.limit)continue;
                const int64_t d=(cx-tx)*(cx-tx)+(cz-tz)*(cz-tz);
                if(best>=0&&(d>bestD||(d==bestD&&z*W+x>best)))continue;
                bool clear=true;
                for(int j=0;j<fz&&clear;++j)for(int i=0;i<fx&&clear;++i)clear=!pt.cells.count((z+j)*W+x+i);
                if(clear) {best=z*W+x;bestD=d;}
            }
        }
        return best;
    }
    // A member walled off from its slot: breadth-first over legal origins
    // whose footprint no other body covers (window of 24 cells), and take the
    // reached free slot cell nearest the point. Reached means a way in
    // exists past the bodies standing now.
    int reachableFormationCell(const Unit& u,const Plane& p,const Point& pt,int64_t px,int64_t pz) const {
        constexpr int R=24,S=2*R+1;
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
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
        std::vector<uint8_t> seen(size_t(S)*S,0);
        std::vector<int> queue;queue.reserve(size_t(S)*S);
        seen[size_t(R*S+R)]=1;queue.push_back(R*S+R);
        int best=-1;int64_t bestD=0;
        for(size_t head=0;head<queue.size();++head) {
            const int local=queue[head],lx=local%S,lz=local/S,x=ox+lx-R,z=oz+lz-R;
            const int64_t cx=int64_t(x)*16+fx*8,cz=int64_t(z)*16+fz*8;
            const int64_t d=(cx-px)*(cx-px)+(cz-pz)*(cz-pz);
            if(d<=pt.limit*pt.limit&&(best<0||d<bestD)) {
                bool clear=true;
                for(int j=0;j<fz&&clear;++j)for(int i=0;i<fx&&clear;++i)clear=!pt.cells.count((z+j)*W+x+i);
                if(clear) {best=z*W+x;bestD=d;}
            }
            for(const auto& dd:kDirections) {
                const int nlx=lx+dd[0],nlz=lz+dd[1];
                if(nlx<0||nlz<0||nlx>=S||nlz>=S||seen[size_t(nlz*S+nlx)])continue;
                if(!step(p,x,z,dd[0],dd[1])||!open(x+dd[0],z+dd[1]))continue;
                if(dd[0]&&dd[1]&&(!open(x+dd[0],z)||!open(x,z+dd[1])))continue;
                seen[size_t(nlz*S+nlx)]=1;queue.push_back(nlz*S+nlx);
            }
        }
        return best;
    }
    bool formationMember(const Member& m) const {
        if(m.pt)return m.pt->assigned&&m.pt->limit>0;
        const auto found=points.find(m.point);
        return found!=points.end()&&found->second.assigned&&found->second.limit>0;
    }
    void takeFormation(Member& m,const Unit& u,int cell) {
        m.goal=cell;m.slot=0;m.lineCell=-1;
        slotCells(m,u.type->footX,u.type->footZ,true);
    }
    // Every member sent to one point in one command gets its slot at once,
    // by formation: its offset from the members' centroid, scaled so the
    // formation's spread matches the packed disc that many bodies occupy,
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
        for(const auto& [key2,id,mm]:order) {
            const Unit* v=w.unit(id);
            const Group& gg=groups.find(mm->group)->second;
            const Plane& pp=plane(gg.plane);
            const int64_t ox=(v->x.v>>16)-pt.centreX,oz=(v->z.v>>16)-pt.centreZ;
            const int cell=formationCell(*v,pp,groupComp(gg,pp),pt,px,pz,px+ox*pt.scaleNum/pt.scaleDen,pz+oz*pt.scaleNum/pt.scaleDen);
            if(cell>=0)takeFormation(*mm,*v,cell);
        }
    }
    // Shared points: formation slots (assigned once for the whole point; a
    // later joiner maps its own offset the same way). A member walled off
    // from its slot re-chooses the free cell nearest itself every 20 held
    // updates, inside the same area.
    bool formationSlot(const Unit& u,Member& m,const Group& g,const Plane& p) {
        Point* cached=m.pt;
        if(!cached) {auto found=points.find(m.point);cached=found==points.end()?nullptr:&found->second;}
        // Once assigned, the point's formation stays in force for its last
        // member too (refs 1): it can still re-choose a walled-off slot.
        if(!cached||(cached->refs<2&&!cached->assigned))return false;
        auto& pt=*cached;
        if(!pt.assigned&&(!pt.tried||w.tickCounter_%16==0)) {pt.tried=true;assignFormation(pt,m.point);}
        if(!pt.assigned||pt.limit<=0)return true;
        const int64_t px=int64_t(std::get<2>(m.point))>>16,pz=int64_t(std::get<3>(m.point))>>16;
        const int64_t ux=u.x.v>>16,uz=u.z.v>>16;
        if(m.slot>=0) {
            if(m.state!=Holding||m.held<20||m.held%20)return true;
            slotCells(m,u.type->footX,u.type->footZ,false);
            const int cell=reachableFormationCell(u,p,pt,px,pz);
            if(cell>=0)m.goal=cell;
            slotCells(m,u.type->footX,u.type->footZ,true);m.lineCell=-1;
            return true;
        }
        // No free cell was left for this member: it walks to the point and
        // looks again (by reachability) only while it is held.
        if(m.slot==-2) {
            if(m.state!=Holding||m.held<20||m.held%20)return true;
            const int cell=reachableFormationCell(u,p,pt,px,pz);
            if(cell>=0)takeFormation(m,u,cell);
            return true;
        }
        const int64_t ox=ux-pt.centreX,oz=uz-pt.centreZ;
        const int cell=formationCell(u,p,groupComp(g,p),pt,px,pz,px+ox*pt.scaleNum/pt.scaleDen,pz+oz*pt.scaleNum/pt.scaleDen);
        if(cell>=0)takeFormation(m,u,cell);else m.slot=-2;
        return true;
    }
    void claimSlot(const Unit& u,Member& m,Group& g,const Plane& p,int here) {
        if(m.requested<0)return;
        // An approach point is a stand-in, not a destination area: bodies
        // queue up to it in the order they come (keeping their formation,
        // so the deepest goals lead when the way opens) and hold on contact.
        if(m.approach)return;
        // Kinds without a destination area (patrol laps, chases) never claim
        // slots: a chaser's goal is a unit, ended by its owner, not a spot.
        if(!policy(m.kind).area)return;
        if(formationSlot(u,m,g,p))return;
        bool nearest=false;
        if(m.slot>=0) {
            // A claimed slot walled off by bodies that settled first is
            // re-chosen every 20 held updates: take the nearest free slot,
            // which fills the area from the side this member stands on.
            if(m.state!=Holding||m.held<20||m.held%20)return;
            auto found=g.slots.find(m.requested);
            if(found==g.slots.end())return;
            if(size_t(m.slot)<found->second.taken.size())found->second.taken[size_t(m.slot)]=0;
            slotCells(m,u.type->footX,u.type->footZ,false);
            m.slot=-1;nearest=true;
        }
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
        // empty, so nobody has to cross a settled body. A member re-claiming
        // after being walled off takes the nearest free slot instead.
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
            int64_t score,side;
            if(nearest) {score=-((cx-ux)*(cx-ux)+(cz-uz)*(cz-uz));side=0;}
            else {
                score=(cx-seedX)*ax+(cz-seedZ)*az;
                side=std::abs((cx-seedX)*az-(cz-seedZ)*ax);
            }
            if(best<0||score>bestScore||(score==bestScore&&side<bestSide)) {best=int(i);bestScore=score;bestSide=side;}
        }
        if(best<0)return;
        s.taken[size_t(best)]=1;m.slot=best;m.goal=s.cells[size_t(best)];m.lineCell=-1;
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
    // A long-held body (kRestAfter updates without a step, no committed
    // detour or route) skips its full update on all but one tick in
    // kRestStride (staggered by id), as long as nothing it reacts to has
    // changed since its last full update: it was not pushed or hurt, the
    // static epoch is the same and no body arrived at or left any cell
    // around it. Any change wakes it at once. A skipped update is a hold
    // that does not count toward the held timers (they count updates).
    bool heldRest(const Unit& u,Member& m) {
        if(m.state!=Holding||m.held<kRestAfter||m.detour>=0||!m.route.empty()||w.occW_<=0) {m.rest=false;return false;}
        const uint64_t ring=ringSignature(u);
        const bool same=m.rest&&m.restX==u.x.v&&m.restZ==u.z.v&&m.restHp==u.hp.v&&m.restEpoch==epoch&&m.restRing==ring;
        if(same&&(uint32_t(u.id)+w.tickCounter_)%kRestStride!=0)return true;
        m.rest=true;m.restX=u.x.v;m.restZ=u.z.v;m.restHp=u.hp.v;m.restEpoch=epoch;m.restRing=ring;
        return false;
    }
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
        // left the seeded origin (bounded, deterministic cadence).
        if(found&&rule.moving&&found->controller==key&&
           w.tickCounter_/kReseedTicks!=found->seededAt/kReseedTicks) {
            const auto [tx,tz]=target(leg);
            const int gx=footprintOrigin(tx,u.type->footX),gz=footprintOrigin(tz,u.type->footZ);
            if(std::max(std::abs(gx-found->seedX),std::abs(gz-found->seedZ))>=kReseedCells) {
                registerMove(u);found=member(u.id);
                if(!found) {w.brakeGround(u);return;}
            }
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
        auto& m=*found;
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
        if(rule.exitClear&&m.stillWindows>=kExitWindows&&leg.productionExit) {
            const int64_t dx=int64_t(u.x.floorInt())-leg.productionExit->first.floorInt();
            const int64_t dz=int64_t(u.z.floorInt())-leg.productionExit->second.floorInt();
            const bool clear=std::abs(dx)>=u.type->footX*16||std::abs(dz)>=u.type->footZ*16;
            const bool rally=World::currentLeg(u.orders)+1<u.orders.size();
            if(clear||(rally&&m.stillWindows>=2*kExitWindows)) {complete(u,m,true);return;}
        }
        if(m.goal<0) {trapped(u,m);return;}
        // Holding at the approach point, or out of grace while walking to it.
        if(m.approach&&(m.state==Trapped||w.tickCounter_-m.approachSince>=kTrappedRetire)) {trapped(u,m);return;}
        auto group=groups.find(m.group);
        if(group==groups.end()) {registerMove(u);w.brakeGround(u);return;}
        auto& g=group->second;g.lastUse=w.tickCounter_;
        const auto& p=plane(g.plane);
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        if(!legal(p,ox,oz)) {escape(u,maximum,leg);return;}
        // A goal origin can stop being legal (a building went up on it).
        if(!legal(p,m.goal%W,m.goal/W)) {registerMove(u);w.brakeGround(u);return;}
        const int here=oz*W+ox;
        const Field* f=g.field&&g.field->done?g.field.get():nullptr;
        if(f)claimSlot(u,m,g,p,here);
        // A first field still building already steers a body its frontier
        // has passed (see Field::settled): no standing still for the rest
        // of the build. Slots wait for the finished field.
        else if(g.field&&g.field->settled(size_t(here),g.field->at(size_t(here))))f=g.field.get();
        // A bounded field that cannot reach this body (it walked or joined
        // outside the window, or the way out leaves it): widen the group's
        // field to the whole component and wait for it, never trapped. It
        // restarts as a first field (members already passed by its frontier
        // steer by it half built), not as a refresh: refreshes restart on
        // every static change and never finish under constant churn.
        if(g.field&&g.field->bounded&&!g.full&&(!g.field->inside(ox,oz)||(g.field->done&&g.field->at(size_t(here))==kUnreached))) {
            g.full=true;g.field.reset();g.next.reset();g.stale=false;restaleSlots(g);
            m.state=Waiting;w.brakeGround(u);return;
        }
        // Close enough: a body that gained less than half a body on its
        // point over the last window, pressed against the settled crowd
        // already standing there, settles where it is (see crowdSettle).
        // Checked before detours and shuffles, which otherwise run forever.
        if(w.tickCounter_-m.windowTick>=kCrowdWindow) {
            const int64_t dx=(int64_t(u.x.v)-std::get<2>(m.point))>>16,dz=(int64_t(u.z.v)-std::get<3>(m.point))>>16;
            const int64_t dist=isqrtFloor(uint64_t(dx*dx+dz*dz));
            const int64_t body=int64_t(std::max(fx,fz))*16;
            const bool still=m.windowDist>=0&&m.windowDist-dist<body/2;
            m.windowTick=w.tickCounter_;m.windowDist=dist;
            m.stillWindows=still?uint8_t(std::min(m.stillWindows+1,255)):uint8_t(0);
            if(m.stillWindows>0) {
                // A crowd of an earlier order (or idle bodies) already stands
                // there: one window. A crowd of this same order is still
                // forming, and the area logic (contactArrival) brings its
                // late members in: only after twice the in-area wait.
                const int crowd=crowdSettle(u,m,g,ox,oz,dist);
                // Inside a formation's area (and its two-body margin) that
                // logic decides alone; this only ends the wait outside it,
                // where a late member otherwise never settles. (Settling
                // there too cost sharedgoal 200 eight arrivals, seeds 0/7/42.)
                const bool outside=!(m.pt&&m.pt->assigned&&m.pt->limit>0)||dist>m.pt->limit+2*body;
                const uint32_t still=uint32_t(m.stillWindows)*kCrowdWindow;
                if(crowd==2||(crowd==1&&outside&&still>=2*kAreaSettle)||(crowd==3&&outside&&still>=kFarSettle)) {complete(u,m,true);return;}
            }
        }
        if(!m.route.empty()) {
            while(!m.route.empty()&&m.route.front()==here)m.route.erase(m.route.begin());
            if(m.route.empty()||++m.routeTicks>240||(m.state==Holding&&m.held>=30)) {m.route.clear();m.lineCell=-1;}
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
            if(left<m.progress) {m.progress=left;m.stalled=0;m.detourCount=0;} else ++m.stalled;
            if(m.stalled>=20&&contactArrival(u,m)) {complete(u,m,true);return;}
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
                const int counts=softCells.empty()?-1:softCountsFor(fx,fz);
                m.line=std::max(std::abs(goalX-ox),std::abs(goalZ-oz))<=reach&&sweep(p,u,u.x,u.z,gx,gz)&&
                    (softCells.empty()||trace(u,u.x,u.z,gx,gz,[&](int cx,int cz) {return !softAt(cx,cz,fx,fz,command,counts);}));
            }
            direct=m.line;
        }
        if(!direct) {
            if(!f||(f->at(size_t(here))==kUnreached&&f->bounded)) {
                g.waited=w.tickCounter_;
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
                m.state=Waiting;w.brakeGround(u);return;
            }
            const uint16_t potential=f->at(size_t(here));
            if(potential==kUnreached) {trapped(u,m);return;}
            int cell=aimCell(u,m,p,*f,ox,oz);
            if(cell<0) {hold(u,m);return;}
            if(f->done&&m.pivotR>=0&&m.slot>=0&&m.pt&&m.pt->refs>=kPivotMembers&&formationMember(m)) {
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
            const int cap=std::min<int>(kPivotMaxCells,int(isqrtFloor(uint64_t(std::max(m.pt->refs,0))))*foot*3/2);
            if(turn<0)return -1;
            // Rank: the member's side offset from the group's live centroid
            // across the way to the turn, measured from the inner (turn)
            // side; the centre file keeps half the group's packed width.
            auto& pt=*m.pt;
            if(pt.liveTick!=w.tickCounter_) {
                int64_t sx=0,sz=0,count=0;
                for(const int id:pt.ids)if(const Unit* v=w.unit(id)) {sx+=v->x.v>>16;sz+=v->z.v>>16;++count;}
                pt.liveTick=w.tickCounter_;pt.liveX=count?sx/count:0;pt.liveZ=count?sz/count:0;
            }
            const int64_t tx=int64_t(chain[size_t(turn)]%W)*16+fx*8-pt.liveX,tz=int64_t(chain[size_t(turn)]/W)*16+fz*8-pt.liveZ;
            const int64_t tl=isqrtFloor(uint64_t(tx*tx+tz*tz));
            const int64_t lat=tl?(tx*((u.z.v>>16)-pt.liveZ)-tz*((u.x.v>>16)-pt.liveX))/tl:0;   // px, + right of the way
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
        ++stats.passScans;
        bool oncoming=false;
        // A body spans several scanned cells; the verdict on it depends only
        // on the body, so each one is judged once (the scan's hot cost was
        // the repeated member lookups).
        std::array<int32_t,8> seen{};size_t seenCount=0;
        for(int k=1;k<=kPassCells&&!oncoming;++k)for(int j=0;j<fz&&!oncoming;++j)for(int i=0;i<fx&&!oncoming;++i) {
            const int cx=ox+k*dx+i,cz=oz+k*dz+j;
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
            const int32_t o=occAt(size_t(cz)*w.occW_+cx);
            if(!o||o==u.id)continue;
            if(std::find(seen.begin(),seen.begin()+seenCount,o)!=seen.begin()+seenCount)continue;
            if(seenCount<seen.size())seen[seenCount++]=o;
            const Member* peer=member(o);
            if(!peer||peer->state==Arrived||peer->state==Trapped)continue;
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
                const bool settled=f&&m.detour<0&&m.route.empty()&&blockedBySettled(u,nx,nz);
                if(settled&&int64_t(m.progress)==m.detourBest&&m.held>=6&&yieldLane(u,m,p,nx,nz)) {hold(u,m);m.held=0;return;}
                if(f&&m.detour<0&&m.route.empty()&&m.held>=m.nextDetour&&
                   (settled||(m.held>=60&&formationMember(m)))) {
                    // Back off geometrically after each attempt: a crowd that
                    // stays jammed stops re-planning instead of shuffling.
                    const uint32_t wait=std::min<uint32_t>(30u<<std::min<uint32_t>(m.detourCount,4u),480u);
                    ++m.detourCount;
                    if(localDetour(u,m,p,f,towardGoal)) {m.nextDetour=wait;u.speed=Fixed();return;}
                    m.nextDetour=m.held+wait;
                }
                if(f&&m.detour<0&&m.route.empty()&&m.held>=12&&yieldLane(u,m,p,nx,nz)) {hold(u,m);m.held=0;return;}
                if(f&&m.detour<0&&m.route.empty()&&m.held>=12&&partLane(u,m,p,ox,oz,nx,nz)) {hold(u,m);m.held=0;return;}
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
        m.state=Moving;m.held=0;
    }
    // Is the cell this body wants held by a body that will not move on its
    // own (idle, arrived, or not a Legion mover)? A queue of members waiting
    // for each other is not: it drains by itself and must not be re-planned.
    bool blockedBySettled(const Unit& u,int nx,int nz) const {
        for(int j=0;j<u.type->footZ;++j)for(int i=0;i<u.type->footX;++i) {
            const int cx=nx+i,cz=nz+j;
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
            const int32_t o=occAt(size_t(cz)*w.occW_+cx);
            if(!o||o==u.id)continue;
            const Member* peer=member(o);
            if(!peer||peer->state==Arrived||peer->state==Trapped)return true;
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
            return !peer||peer->state==Holding||peer->state==Arrived||peer->state==Trapped;
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
        m.state=Holding;m.held=0;  // stopped this update; the route starts next
        return true;
    }
    // Opposing traffic: after a short hold, both bodies commit to a lateral
    // step to their own right (keep-right), so head-on pairs pass instead
    // of pushing. Same-direction queues only side-step after a long hold.
    void sidestep(const Unit& u,Member& m,const Plane& p,const Field& f,int ox,int oz,int nx,int nz) {
        if(m.held<6)return;
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
    int crowdSettle(const Unit& u,const Member& m,const Group& g,int ox,int oz,int64_t dist) const {
        if(m.approach||m.goal<0)return 0;
        const int fx=u.type->footX,fz=u.type->footZ,foot=std::max(fx,fz);
        const int64_t body=int64_t(foot)*16;
        const int64_t px=int64_t(std::get<2>(m.point)),pz=int64_t(std::get<3>(m.point));
        auto sameDestination=[&](const Anchor& a) {
            if(std::get<0>(a.point)!=u.player)return false;
            const int64_t ax=(int64_t(std::get<2>(a.point))-px)>>16,az=(int64_t(std::get<3>(a.point))-pz)>>16;
            return ax*ax+az*az<=4*body*body;
        };
        // Touching: a settled body of this destination within one body,
        // nearer the point than this one.
        int touching=0;bool anchored=false;
        for(int j=-foot;j<fz+foot&&(touching<2||!anchored);++j)for(int i=-foot;i<fx+foot&&(touching<2||!anchored);++i) {
            const int cx=ox+i,cz=oz+j;
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
            const int32_t o=occAt(size_t(cz)*w.occW_+cx);
            if(!o||o==u.id)continue;
            const Unit* other=w.unit(o);
            if(!other||other->player!=u.player||!other->type||other->type->isStructure())continue;
            // Settled for this destination: a Legion arrival there, or an
            // idle body (standing nearer the point, so inside its crowd).
            const auto a=isAnchor(o)?anchors.find(o):anchors.end();
            if(a!=anchors.end()?!sameDestination(a->second):!other->orders.empty())continue;
            const int64_t qx=(int64_t(other->x.v)-px)>>16,qz=(int64_t(other->z.v)-pz)>>16;
            if(qx*qx+qz*qz>=dist*dist)continue;
            // Same order: same player and issue tick (a group's members may
            // each name their own point of one lattice).
            const bool own=a!=anchors.end()&&std::get<1>(a->second.point)==std::get<1>(m.point);
            touching=std::max(touching,own?1:2);
            anchored=anchored||a!=anchors.end();
        }
        if(!touching)return 0;
        // Reach: four times the packed disc of everyone settled there (and
        // this body), plus two bodies.
        int64_t count=1;
        for(const auto& [id,a]:anchors)if(sameDestination(a))++count;
        const int64_t reach=4*body*isqrtFloor(uint64_t(count)*100000000/31416)/100+2*body;
        // Out to twice that, a body touching a settled ARRIVAL of this
        // destination (a link of the crowd itself, not merely an idle body)
        // also counts, after a much longer wait (3): a crowd stretched along
        // the way in (arrivals from one side settle on its near face) left
        // its late arrivals pressed against it, orders held forever, standing
        // exactly where a settled body would stand.
        const bool far=dist>reach;
        if(far&&(!anchored||dist>2*reach))return 0;
        // Connected: the field's way to the destination area is not much
        // longer than the straight line (no settling behind a wall).
        const Field* f=g.field&&g.field->done?g.field.get():nullptr;
        if(!f)return 0;
        const uint16_t potential=f->at(size_t(oz*width()+ox));
        // Soft obstacles (an idle crowd standing there) charge kSoftFactor
        // per step in a softened field.
        const int64_t factor=f->softened?kSoftFactor:1;
        if(potential==kUnreached||int64_t(potential)>factor*((dist*kDiagonal)/16+int64_t(3*foot*kOrthogonal)))return 0;
        // Never in a same-player factory's exit lane: from the factory's
        // centre to past where its output is put down.
        const int64_t ux=u.x.v>>16,uz=u.z.v>>16;
        for(const auto& s:w.units_) {
            if(!s.alive()||s.player!=u.player||!s.type||!s.type->producesUnits())continue;
            const int64_t sx=s.x.v>>16,sz=s.z.v>>16;
            const int64_t half=int64_t(s.type->footX)*8+body;
            if(ux>=sx-half&&ux<=sx+half&&uz>=sz&&uz<=sz+int64_t(s.type->footZ)*8+60+body)return 0;
        }
        return far?3:touching;
    }
    // A body pressed against settled bodies inside its goal's area has
    // arrived: the destination is an area, and the cells nearer the point
    // are taken. Distinct-goal members use one body width; members sharing a
    // point use the packed disc that many bodies of this size occupy.
    bool contactArrival(const Unit& u,const Member& m) const {
        if(m.stalled<20)return false;
        auto group=groups.find(m.group);
        if(group==groups.end())return false;
        // Shared points are counted by the requested point; a member's own
        // goal is its claimed slot once it has one.
        // The area is sized by everyone the point was given to, including
        // members that already settled there.
        const auto sharing=group->second.peak.find(m.requested);
        const int count=sharing==group->second.peak.end()?1:sharing->second;
        const int body=std::max(u.type->footX,u.type->footZ)*16;
        // Packed disc of `count` bodies: body*sqrt(count/pi), plus a body.
        const int64_t radius=count>1?int64_t(body)+int64_t(body)*isqrtFloor(uint64_t(count)*100000000/31416)/100:body;
        const int W=width();
        const int point=count>1?m.requested:m.goal;
        const Fixed gx=centre(point%W,u.type->footX),gz=centre(point/W,u.type->footZ);
        int64_t dx=(int64_t(u.x.v)-gx.v)>>16,dz=(int64_t(u.z.v)-gz.v)>>16;
        // A member of a shared point held still for ten seconds within one
        // body of the packed disc is at the destination area: its slot is
        // gone (covered, or walled off by bodies that settled first) and
        // waiting longer cannot make one.
        bool formation=false;
        if(const Point* pt=m.pt?m.pt:[&]()->const Point* {const auto f=points.find(m.point);return f==points.end()?nullptr:&f->second;}();
           !m.approach&&(count>1||m.slot!=-1)&&pt&&pt->assigned&&pt->limit>0) {
            // Formation slots: the area is the whole point's (every class
            // sent there), measured from the requested point itself.
            formation=true;
            dx=(int64_t(u.x.v)>>16)-(int64_t(std::get<2>(m.point))>>16);
            dz=(int64_t(u.z.v)>>16)-(int64_t(std::get<3>(m.point))>>16);
            const int64_t limit=pt->limit;
            // Walled out at the ring edge by settled bodies: after twice the
            // in-area stand-still, within two bodies of the area, it settles
            // (bounded: a formation member never waits forever). At the
            // in-area wait (300) it settled bodies a re-choice would still
            // have brought in: sharedgoal 200 lost 4 arrivals (seeds 0/7/42).
            if(dx*dx+dz*dz>limit*limit)
                return m.stalled>=2*kAreaSettle&&dx*dx+dz*dz<=(limit+2*body)*(limit+2*body);
            if(m.stalled>=kAreaSettle)return true;
        } else {
            if(count>1&&m.stalled>=kAreaSettle&&dx*dx+dz*dz<=(radius+body)*(radius+body))return true;
            if(dx*dx+dz*dz>radius*radius)return false;
        }
        if(count==1&&!formation) {
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
        }
        // Only settled neighbours (idle, or already arrived) make an area full.
        const int fx=u.type->footX,fz=u.type->footZ;
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        bool settled=false;
        for(int j=-1;j<=fz&&!settled;++j)for(int i=-1;i<=fx&&!settled;++i) {
            const int cx=ox+i,cz=oz+j;
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
            const int32_t o=occAt(size_t(cz)*w.occW_+cx);
            if(!o||o==u.id)continue;
            const Unit* other=w.unit(o);
            if(!other||other->player!=u.player)continue;
            if(other->orders.empty()||other->orders[World::currentLeg(other->orders)].mission.pending&0x500)settled=true;
            // Members of a shared point that are themselves pressed still
            // inside the area count as its filled part.
            else if(count>1||formation) {
                const Member* peer=member(o);
                settled=peer&&peer->group==m.group&&peer->stalled>=20;
            }
        }
        return settled;
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
            h=mix(h,uint64_t(uint32_t(g.bodyMinX))<<32|uint32_t(g.bodyMinZ));h=mix(h,uint64_t(uint32_t(g.bodyMaxX))<<32|uint32_t(g.bodyMaxZ));h=mix(h,g.full);h=mix(h,g.waited);
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
        }
        for(const auto& [id,a]:anchors) {
            h=mix(h,uint64_t(id));h=mix(h,uint64_t(a.goal));h=mix(h,a.yields);
            h=mix(h,uint64_t(std::get<0>(a.point)));h=mix(h,std::get<1>(a.point));
            h=mix(h,uint32_t(std::get<2>(a.point)));h=mix(h,uint32_t(std::get<3>(a.point)));
        }
        for(const auto& [id,y]:yielding) {h=mix(h,uint64_t(id));h=mix(h,uint64_t(y.cell));h=mix(h,y.ticks);}
        h=mix(h,softSerial);h=mix(h,softHash);
        for(const auto& [id,st]:stills) {h=mix(h,uint64_t(id));h=mix(h,uint64_t(uint32_t(st.x))<<32|uint32_t(st.z));h=mix(h,st.scans);}
        for(const auto& [id,n]:parts) {h=mix(h,uint64_t(id));h=mix(h,n.count);h=mix(h,uint64_t(uint8_t(n.sx))|uint64_t(uint8_t(n.sz))<<8);}
        for(const auto& [id,m]:members) {
            h=mix(h,uint64_t(id));h=mix(h,m.controller);h=mix(h,uint64_t(m.group));h=mix(h,uint64_t(m.goal));
            h=mix(h,uint64_t(m.lineCell));h=mix(h,m.line);h=mix(h,m.state);h=mix(h,m.best);
            h=mix(h,m.held);h=mix(h,m.stalled);h=mix(h,m.progress);h=mix(h,uint64_t(m.requested));
            h=mix(h,m.rest);if(m.rest) {h=mix(h,uint32_t(m.restX));h=mix(h,uint32_t(m.restZ));h=mix(h,uint32_t(m.restHp));h=mix(h,m.restEpoch);h=mix(h,m.restRing);}
            h=mix(h,uint64_t(m.slot));h=mix(h,uint64_t(m.detour));h=mix(h,m.detourTicks);h=mix(h,m.detourFace);h=mix(h,m.detourPass);h=mix(h,m.passUntil);h=mix(h,uint64_t(m.detourBest));h=mix(h,uint64_t(uint8_t(m.passRX))|uint64_t(uint8_t(m.passRZ))<<8);
            h=mix(h,m.trappedSince);h=mix(h,m.trappedEpoch);h=mix(h,m.windowTick);h=mix(h,uint64_t(m.windowDist));h=mix(h,m.stillWindows);
            h=mix(h,m.approach);h=mix(h,m.approachSince);h=mix(h,m.approachEpoch);h=mix(h,uint64_t(m.real));
            h=mix(h,uint64_t(std::get<0>(m.point)));h=mix(h,std::get<1>(m.point));
            h=mix(h,uint32_t(std::get<2>(m.point)));h=mix(h,uint32_t(std::get<3>(m.point)));
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
void LegionNavigator::tick() {impl_->tick();}
void LegionNavigator::move(Unit& u,Fixed maximum) {impl_->move(u,maximum);}
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
int LegionNavigator::unitGroup(int id) const {
    const auto found=impl_->members.find(id);
    return found==impl_->members.end()?0:found->second.group;
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
}
