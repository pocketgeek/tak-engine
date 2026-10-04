#pragma once
#include "flowfield.h"
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
    };
    struct Result {bool settled=false;std::optional<Cell> detour;bool wait=false,repath=false;};
    Result update(const Context&);
    // Called with a fixed per-tick budget, independently of wall-clock time.
    void prune(size_t budget,const std::function<bool(int,int,uint64_t,Cell,bool)>& valid);
    uint64_t checksum() const;
    size_t bytes() const;
private:
    using Group=std::tuple<int,int,int>;
    struct Record {
        Group group;
        uint64_t controller=0;
        uint32_t missionKind=0;
        int targetId=0;
        uint32_t seen=0,detourUntil=0;
        Cell position;
        std::optional<Cell> detour,continuation;
        bool plain=false,settled=false;
        uint32_t blockedSince=0,yieldUntil=0;
        int yieldTo=0;
        Identity yieldIdentity;
        Group yieldGroup;
        Cell yieldDirection,yieldOrigin;
    };
    static_assert(sizeof(Record)<=256,"traffic records must fit the shared memory reservation");
    int pruneCursor_=0,workCursor_=0;
    uint32_t workTick_=0;
    std::set<int> waiting_,admitted_;
    std::map<int,Record> records_;
    std::map<Group,size_t> groups_;
    void erase(std::map<int,Record>::iterator);
};
}
