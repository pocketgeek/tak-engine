#pragma once
#include "flowtraffic.h"
#include <array>
#include <map>
#include <set>

namespace tak::sim::cooperative {
// Sparse, short-horizon movement reservations. Immutable terrain routing stays
// in FlowNavigator; this coordinator never allocates a second map-sized field.
class Traffic {
public:
    using Context=flow::Traffic::Context;
    using Result=flow::Traffic::Result;
    using Cell=flow::Cell;
    struct Stats {
        uint64_t probes=0,searches=0,routes=0,waits=0,conflicts=0;
        uint64_t completeFailures=0,deferredSearches=0,retrySkips=0;
        size_t records=0,reservations=0,bytes=0;
    };
    // Additional owned storage. The existing request reservation already
    // accounts for arrivals_' independently capped Flow arrival policy.
    static constexpr size_t memoryLimit=16*1024*1024;
    // Endpoints follow this member's direction of travel. Width is the raw
    // terrain opening, not the count of legal footprint-centre anchors.
    struct Passage {Cell first,last;uint16_t width=0,extentWidth=0;};
    Result update(const Context&);
    bool allowFollowerStep(int id,Cell from,Cell to) const;
    void cancel(int id);
    void cancelUnsettledArrival(int id) {arrivals_.cancelUnsettled(id);}
    // False leaves the previous descriptor intact. The route owner must defer
    // entry until a rejected new descriptor can be admitted.
    bool setPassage(int id,std::optional<Passage>);
    void setAllianceMask(int player,uint16_t mask);
    void registerMove(const Context&);
    int arrivalRadiusSquared(const Context& c) const {return arrivals_.arrivalRadiusSquared(c);}
    bool settled(int id,Cell at) const {return arrivals_.settled(id,at);}
    void refreshSettled(int id,Cell at) {arrivals_.refreshSettled(id,at);}
    bool nearArrival(const Context& c) const {return arrivals_.nearArrival(c);}
    bool needsArrivalNeighbors(const Context& c) const {return arrivals_.needsArrivalNeighbors(c);}
    std::optional<Cell> corridorDirection(const Context&) const;
    std::optional<Cell> corridorAim(const Context&) const;
    void prune(size_t,const std::function<bool(int,int,uint64_t,Cell,bool)>&);
    uint64_t checksum() const;
    size_t bytes() const;
    Stats stats() const;
private:
    struct Record {
        flow::Traffic::Identity identity;
        int player=0,footX=1,footZ=1;
        uint16_t coordination=0;
        uint32_t issued=0,seen=0,until=0,progress=0,waiting=0;
        Cell position,start,origin,aim,direction,claimFrom,claimTo;
        std::array<uint8_t,48> steps{};
        uint8_t count=0,next=0;
        bool claim=false,yielding=false,arrivalRoute=false;
        std::optional<Passage> passage;
        int8_t passageDirection=0;
        bool passagePermit=false,passageWaiting=false,passageStale=false;
        Cell retryAim,retryPeer;
        uint32_t retryTick=0;
        int retryPeerId=0;
        uint8_t retryPolicy=0;
    };
    static_assert(sizeof(Record)<=256);
    static constexpr size_t maxRecords=16384,maxLinks=32768,maxBuckets=8192;
    flow::Traffic arrivals_;
    std::map<int,Record> records_;
    using Group=std::tuple<int,int,int,int>;
    struct Population {int64_t x=0,z=0;size_t count=0;};
    std::map<Group,Population> groups_;
    using PassageKey=std::tuple<int,int,int,int,uint16_t>;
    struct Gate {uint16_t width=0,maxFoot=0;int8_t direction=0;uint16_t members=0,active=0,batch=0;uint32_t since=0;std::array<uint16_t,2> waiting{};};
    std::map<PassageKey,Gate> passages_;
    using Bucket=std::tuple<int,int,uint16_t>;
    std::map<Bucket,std::set<int>> claims_;
    size_t links_=0,probeRemaining_=0,conflictRemaining_=0;
    int pruneCursor_=0,workCursor_=0;
    int chargedAfter_=0,chargedLast_=0;
    bool exhausted_=false;
    uint32_t tick_=0;
    bool started_=false;
    std::set<int> pending_,admitted_;
    Stats totals_;
    std::array<uint16_t,16> alliances_{};
    void beginTick(uint32_t);
    size_t logicalBytes() const;
    uint16_t coordinationMask(int player) const;
    bool spend(int id,size_t weight,bool footprint,bool* deferred=nullptr);
    void releasePassage(Record&);
    bool passageWait(const Context&,Record&);
    Record* remember(const Context&);
    void release(int,Record&);
    void erase(std::map<int,Record>::iterator);
    bool conflict(int,const Record&,Cell,Cell,bool* deferred=nullptr);
    bool reserve(int,Record&,Cell,Cell,uint32_t);
    bool legal(const Context&,Record&,Cell,Cell,bool* deferred=nullptr);
    enum class Plan {Ready,NoRoute,Deferred};
    Plan plan(const Context&,Record&,bool yield);
    Result follow(const Context&,Record&);
};
}
