#pragma once
#include "flowfield.h"
#include "flowroute.h"
#include "trafficindex.h"
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <set>
#include <tuple>

namespace tak::sim::flow {
// Flow-only local traffic state. Terrain fields remain independent of transient
// bodies; this bounded layer retains short detours and completed group arrivals.
class Traffic {
public:
    struct Identity {
        uint64_t controller=0;
        Cell target;
        uint32_t kind=0;
        int targetId=0;
        bool operator==(const Identity&) const=default;
    };
    struct Neighbor {
        int id=0,player=0;
        Cell position;
        int footX=1,footZ=1;
        bool idle=false;
        std::optional<Identity> identity{};
        std::optional<Cell> steeringTarget{};
        bool mobile=true;
    };
    struct Context {
        int id=0,player=0;
        uint64_t controller=0;
        uint32_t tick=0;
        uint32_t issuedTick=0;
        Cell position,target;
        int footX=1,footZ=1,blocked=0;
        std::optional<Cell> steeringTarget;
        bool plainMove=false,goalReached=false;
        uint32_t missionKind=0;
        int targetId=0;
        std::span<const Neighbor> neighbors;
        std::optional<Neighbor> obstruction;
        bool obstructionFriendly=false;
        std::function<std::optional<Neighbor>(int)> lookup;
        // Must test the whole footprint, including stationary and moving bodies.
        std::function<bool(Cell)> free,terrainFree;
        // A slot must have a clear footprint route from the mover and a local
        // terrain connection to the mission area. Occupancy is checked here.
        std::function<bool(Cell)> arrivalReachable;
        // Strict known-terrain connection to an already arrived neighbor.
        // Unlike avoidance's terrainFree, incomplete evidence cannot pass.
        std::function<bool(Cell)> contactReachable;
        // Optional callback status: exhausted shared proof work defers the
        // candidate; it does not invalidate an already committed slot.
        bool* arrivalDeferred=nullptr;
        // Optional live-query status. Once set during this update, unknown
        // body/terrain results must not invalidate retained routes or settle.
        bool* localDeferred=nullptr;
        // Cooperative-only optimization scope. The adapter supplies a fresh
        // wholly-open published footprint tile; pure callers default to open.
        bool cooperativeOpenTerrain=true;
    };
    struct Result {bool settled=false;std::optional<Cell> detour;bool wait=false,repath=false,settledRally=false,arrivalApproach=false;int followLeader=0;};
    Result update(const Context&);
    // Complete only an ordinary unblocked update with no retained local work.
    // A declined call leaves all state untouched, including work admission.
    bool updateUnblocked(const Context&);
    void registerMove(const Context&);
    // Opt-in lifecycle cleanup for an adapter that distinguishes completed
    // contact anchors from cancelled, uncompleted arrival reservations.
    void cancelUnsettled(int id);
    int arrivalRadiusSquared(const Context&) const;
    bool settled(int id,Cell position) const;
    void refreshSettled(int id,Cell position);
    bool nearArrival(const Context&) const;
    bool needsArrivalNeighbors(const Context&) const;
    // Called with a fixed per-tick budget, independently of wall-clock time.
    void prune(size_t budget,const std::function<bool(int,int,uint64_t,Cell,bool)>& valid);
    uint64_t checksum() const;
    size_t bytes() const;
private:
    using Group=std::tuple<int,int,int,int>;
    struct Record {
        Group group;
        uint64_t controller=0;
        uint32_t missionKind=0;
        int targetId=0;
        uint32_t seen=0,detourUntil=0;
        Cell position;
        std::optional<Cell> detour,continuation;
        int64_t detourDistance=0;
        bool longDetour=false;
        Cell bypassSide,bypassForward;
        std::optional<Cell> arrivalSlot;
        uint32_t slotUntil=0;
        int64_t slotDistance=0;
        int footX=1,footZ=1;
        bool plain=false,settled=false;
        uint32_t progressTick=0;
        uint32_t issuedTick=0;
        int64_t bestDistance=0;
        uint32_t blockedSince=0,yieldUntil=0;
        int yieldTo=0;
        Identity yieldIdentity;
        Group yieldGroup;
        Cell yieldDirection,yieldOrigin;
        // A bounded local route, packed without per-unit allocations.
        std::array<uint64_t,2> escapeSteps{};
        uint8_t escapeCount=0;
        bool escaping=false;
    };
    static_assert(sizeof(Record)<=256,"traffic records must fit the shared memory reservation");
    int pruneCursor_=0,workCursor_=0;
    uint32_t workTick_=0;
    std::set<int> waiting_,admitted_;
    std::map<int,Record> records_;
    struct Population {size_t members=0;uint64_t area=0,settledArea=0;};
    std::map<Group,Population> groups_;
    // Pointer cache over records_/groups_ nodes; plain records keep their
    // group node alive, so its population pointer is valid with the record.
    struct Slot {Record* record=nullptr;Population* group=nullptr;};
    IdIndex<Slot> index_;
    const Slot* slot(int id) const {return index_.find(id);}
    const Population* population(const Context&) const;
    Record* lookup(int id);
    const Record* lookup(int id) const;
    using Bucket=std::pair<int,int>;
    std::map<Bucket,std::set<int>> reservations_;
    size_t reservationLinks_=0;
    ProofBudget escapeBudget_{4096};
    enum class EscapeResult { Route, Blocked, Deferred, Unavailable };
    EscapeResult escape(const Context&,Record&);
    static void advanceEscape(Record&);
    static void clearEscape(Record&);
    Record& remember(const Context&);
    void beginTick(uint32_t);
    void releaseSlot(int id,Record&);
    bool reserveSlot(int id,Record&,Cell);
    bool slotFree(int id,const Record&,Cell) const;
    void erase(std::map<int,Record>::iterator);
};
}
