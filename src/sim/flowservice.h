#pragma once
#include "flowfield.h"
#include <array>
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
        // Share detailed fields of tiles at least one tile away from every
        // goal tile between destinations whose fields are provably identical.
        bool shareDistant=true;
        // Keep finished shared fields across a topology publication when their
        // immutable source tile is unchanged. Their content is a function of
        // that tile, the exit mask and the bias box, all part of the lookup.
        bool retainShared=true;
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
    // next: the newly published topology of the same profile, if any. Without
    // it (eviction) every field of the profile is discarded.
    void invalidate(uint64_t profile,std::shared_ptr<const Topology> next={});
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
    // Diagnostics only: never read by sim decisions and not part of checksum().
    struct Counters {
        uint64_t destinations=0,destinationWork=0,fieldsBuilt=0,fieldWork=0;
        uint64_t invalidations=0,invalidatedDestinations=0,invalidatedFields=0,invalidatedBuilders=0,invalidatedBindings=0;
        uint64_t evictedGroups=0,evictedFields=0,sharedFieldsBuilt=0,sharedResolutions=0,sharedReuses=0,retainedFields=0;
    };
    const Counters& counters() const {return counters_;}
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
    struct Cached {Field field;uint64_t used=0,fingerprint=0;std::shared_ptr<const Tile> source;};
    using FieldKey=std::pair<uint64_t,int>;
    // A detailed field identity. Private fields belong to one destination
    // group (group!=0). A shared field (group==0) is keyed by everything its
    // content depends on: profile generation, tile, exact exit-seed mask and
    // the tile-aligned bias box. Destinations agreeing on all of these build
    // byte-identical fields, so any of them may build it once for all.
    struct FieldId {
        uint64_t group=0,profile=0;
        int tile=0;
        Cell low{},high{};
        Destination::Exits exits{};
        bool operator<(const FieldId& other) const;
        uint64_t checksum() const;
    };
    Budget budget_;
    Counters counters_;
    uint64_t clock_=0,nextGroup_=0,publishedHash_=0,work_=0;
    std::map<int,uint64_t> bindings_;
    std::map<Key,uint64_t> keys_;
    std::map<uint64_t,Group> destinations_;
    std::map<FieldId,Cached> fields_;
    std::map<FieldId,std::shared_ptr<FieldBuilder>> builders_;
    std::map<FieldKey,FieldId> resolved_; // per group/tile memo, erased with its group
    std::deque<FieldId> fieldQueue_;
    std::deque<uint64_t> destinationQueue_;
    bool bindImpl(int,uint64_t,std::shared_ptr<const Topology>,std::vector<Cell>,std::optional<GoalRegion>);
    bool evictGroup();
    const FieldId& resolve(uint64_t group,const Group& value,int tile);
    void eraseGroup(uint64_t id);
};
}
