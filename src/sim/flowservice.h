#pragma once
#include "flowfield.h"
#include <map>
#include <deque>
#include <span>
#include <tuple>

namespace tak::sim::flow {
// Match-owned sharing/cache policy. Logical work quotas and eviction order do
// not depend on frame rate, hardware concurrency or worker completion timing.
class Service {
public:
    struct Budget {
        size_t destinations=32,fields=512,builders=16,bindings=16384;
        size_t destinationWork=8192,fieldWork=4096;
    };
    enum class Status { Pending,Ready,Arrived,Unreachable,Capacity };
    struct Sample {Status status=Status::Pending;Cell next;};
    Service();
    explicit Service(Budget budget);
    // Snapshot identity is supplied by the deterministic World profile cache.
    // The cache owns profile memory independently of this service's field cap.
    bool bind(int unit,uint64_t profile,std::shared_ptr<const Topology> topology,std::vector<Cell> goals);
    bool bindRegion(int unit,uint64_t profile,std::shared_ptr<const Topology> topology,GoalRegion region);
    void cancel(int unit);
    void invalidate(uint64_t profile);
    // An optional distant aim selects a cardinal alternative for predominantly
    // cardinal travel only when it has exactly the same integrated cost. Broad
    // fronts retain lanes without collapsing diagonal fronts into a seam,
    // changing fields, permitting cycles, or adding route work.
    Sample sample(int unit,Cell from,std::optional<Cell> aim={});
    // Cooperative keeps a shared group direction and bounded lateral lanes.
    // Every choice still descends the field. Existing Flowfield sampling and
    // its cache access order are intact.
    Sample sampleCooperative(int unit,Cell from,Cell direction,std::optional<Cell> laneTarget={});
    // workers=0 executes inline. Worker counts affect scheduling only, never
    // chosen work or publication: all selected jobs join before tick returns.
    void tick(unsigned workers=0);
    size_t fields() const {return fields_.size();}
    size_t destinations() const {return destinations_.size();}
    size_t builders() const {return builders_.size();}
    size_t bindings() const {return bindings_.size();}
    size_t bytes() const;
    uint64_t publishedHash() const {return publishedHash_;}
    uint64_t work() const {return work_;}
    uint64_t checksum() const;
private:
    struct Key {
        uint64_t profile=0;
        std::vector<Cell> goals;
        std::optional<GoalRegion> region;
        bool operator<(const Key& other) const;
        uint64_t checksum() const;
    };
    struct Group {
        Key key;
        std::shared_ptr<Destination> destination;
        uint64_t used=0,keyHash=0;
        size_t users=0;
    };
    struct Cached {Field field;uint64_t used=0,fingerprint=0;};
    using FieldKey=std::pair<uint64_t,int>;
    Budget budget_;
    uint64_t clock_=0,nextGroup_=0,publishedHash_=0,work_=0;
    std::map<int,uint64_t> bindings_;
    std::map<Key,uint64_t> keys_;
    std::map<uint64_t,Group> destinations_;
    std::map<FieldKey,Cached> fields_;
    std::map<FieldKey,std::shared_ptr<FieldBuilder>> builders_;
    std::deque<FieldKey> fieldQueue_;
    std::deque<uint64_t> destinationQueue_;
    bool bindImpl(int,uint64_t,std::shared_ptr<const Topology>,std::vector<Cell>,std::optional<GoalRegion>);
    bool evictGroup();
    void eraseGroup(uint64_t id);
};
}
