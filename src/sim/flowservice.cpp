#include "flowservice.h"
#include "cooperativecorridor.h"
#include "flowworkers.h"
#include <algorithm>
#include <functional>
#include <future>
#include <tuple>

namespace tak::sim::flow {
Service::Service():Service(Budget{}) {}
Service::Service(Budget budget):budget_(budget) {
    if(!budget.destinations||!budget.fields||!budget.builders||!budget.bindings||!budget.destinationWork||!budget.fieldWork)
        throw std::invalid_argument("zero flow service budget");
}
uint64_t Service::Key::checksum() const {
    uint64_t h=1469598103934665603ull;
    const auto mix=[&](uint64_t value){h=(h^value)*1099511628211ull;};
    mix(profile);mix(bool(region));mix(goals.size());
    for(Cell cell:goals){mix(cell.x);mix(cell.z);}
    if(region){mix(uint64_t(region->kind));mix(region->x);mix(region->z);mix(region->maxX);mix(region->maxZ);
        mix(uint32_t(region->outerSquared));mix(region->innerWorldSquared);}
    return h;
}
bool Service::FieldId::operator<(const FieldId& other) const {
    return std::tie(group,profile,tile,low.x,low.z,high.x,high.z,exits)<
        std::tie(other.group,other.profile,other.tile,other.low.x,other.low.z,other.high.x,other.high.z,other.exits);
}
uint64_t Service::FieldId::checksum() const {
    uint64_t h=1469598103934665603ull;
    const auto mix=[&](uint64_t value){h=(h^value)*1099511628211ull;};
    mix(group);mix(profile);mix(uint32_t(tile));mix(uint32_t(low.x));mix(uint32_t(low.z));mix(uint32_t(high.x));mix(uint32_t(high.z));
    for(uint64_t word:exits)mix(word);
    return h;
}
const Service::FieldId& Service::resolve(uint64_t group,const Group& value,int tile) {
    const FieldKey key{group,tile};
    if(const auto found=resolved_.find(key);found!=resolved_.end())return found->second;
    FieldId id{group,0,tile};
    const auto& topo=value.destination->topology();
    auto [low,high]=value.destination->goalBox();
    low={std::max(0,low.x),std::max(0,low.z)};high={std::min(topo.width-1,high.x),std::min(topo.height-1,high.z)};
    const int tx=tile%topo.tilesX,tz=tile/topo.tilesX;
    // Keep exact per-destination bias on the goal tiles and their neighbours.
    // Farther away the bias only breaks ties between equally short component
    // routes: aim at the enclosing goal tiles, so nearby goals share fields.
    // Sharing the neighbour ring too (with a 16-, 8- or 64-cell box) roughly
    // doubled 1000-unit arrivals but broke formation re-spreading after a
    // narrow passage and left a queued-group straggler; it is not used.
    const int gx0=low.x/kTileSize,gz0=low.z/kTileSize,gx1=high.x/kTileSize,gz1=high.z/kTileSize;
    const int ring=std::max({gx0-tx,tx-gx1,gz0-tz,tz-gz1,0});
    if(budget_.shareDistant&&low.x<=high.x&&low.z<=high.z&&ring>=2) {
        id.group=0;id.profile=value.key.profile;
        id.low={gx0*kTileSize,gz0*kTileSize};
        id.high={gx1*kTileSize+kTileSize-1,gz1*kTileSize+kTileSize-1};
        id.exits=value.destination->exits(tile);
        ++counters_.sharedResolutions;
        counters_.sharedReuses+=fields_.contains(id)||builders_.contains(id);
    }
    return resolved_.emplace(key,id).first->second;
}
bool Service::Key::operator<(const Key& other) const {
    if(profile!=other.profile)return profile<other.profile;
    if(region!=other.region)return region<other.region;
    return std::lexicographical_compare(goals.begin(),goals.end(),other.goals.begin(),other.goals.end(),
        [](Cell a,Cell b){return a.z!=b.z?a.z<b.z:a.x<b.x;});
}
void Service::eraseGroup(uint64_t id) {
    const auto it=destinations_.find(id);if(it==destinations_.end())return;
    keys_.erase(it->second.key);destinations_.erase(it);
    std::erase(destinationQueue_,id);
    std::erase_if(fields_,[=](const auto& item){return item.first.group==id;});
    std::erase_if(builders_,[=](const auto& item){return item.first.group==id;});
    std::erase_if(fieldQueue_,[=](const auto& key){return key.group==id;});
    std::erase_if(resolved_,[=](const auto& item){return item.first.first==id;});
}
bool Service::evictGroup() {
    auto oldest=destinations_.end();
    for(auto it=destinations_.begin();it!=destinations_.end();++it)
        if(!it->second.users && (oldest==destinations_.end()||it->second.used<oldest->second.used))oldest=it;
    if(oldest==destinations_.end())return false;
    ++counters_.evictedGroups;eraseGroup(oldest->first);return true;
}
bool Service::bind(int unit,uint64_t profile,std::shared_ptr<const Topology> topology,std::vector<Cell> goals) {
    return bindImpl(unit,profile,std::move(topology),std::move(goals),std::nullopt);
}
bool Service::bindRegion(int unit,uint64_t profile,std::shared_ptr<const Topology> topology,GoalRegion region) {
    return bindImpl(unit,profile,std::move(topology),{},region);
}
bool Service::bindImpl(int unit,uint64_t profile,std::shared_ptr<const Topology> topology,std::vector<Cell> goals,std::optional<GoalRegion> region) {
    if(!topology||(!region&&goals.empty())||goals.size()>4096)return false;
    if(!bindings_.contains(unit)&&bindings_.size()>=budget_.bindings)return false;
    std::sort(goals.begin(),goals.end(),[](Cell a,Cell b){return a.z!=b.z?a.z<b.z:a.x<b.x;});
    goals.erase(std::unique(goals.begin(),goals.end()),goals.end());
    Key key{profile,std::move(goals),region};
    auto found=keys_.find(key);
    if(found==keys_.end()) {
        if(destinations_.size()>=budget_.destinations&&!evictGroup()) {
            // Replacing the sole subscriber of a group must not require an
            // extra destination slot. Other active subscribers remain protected.
            const auto current=bindings_.find(unit);
            if(current==bindings_.end()||destinations_.at(current->second).users!=1)return false;
            const auto old=current->second;bindings_.erase(current);eraseGroup(old);
        }
        const uint64_t id=++nextGroup_;
        auto destination=region?std::make_shared<Destination>(std::move(topology),*region):
            std::make_shared<Destination>(std::move(topology),key.goals);
        destinations_.emplace(id,Group{key,std::move(destination),++clock_,key.checksum(),0});++counters_.destinations;
        found=keys_.emplace(std::move(key),id).first;destinationQueue_.push_back(id);
    }
    const uint64_t group=found->second;
    if(topology && &destinations_.at(group).destination->topology()!=topology.get())
        throw std::logic_error("flow profile identity reused without invalidation");
    if(const auto current=bindings_.find(unit);current!=bindings_.end()&&current->second==group)return true;
    cancel(unit);bindings_[unit]=group;++destinations_.at(group).users;return true;
}
void Service::cancel(int unit) {
    const auto it=bindings_.find(unit);if(it==bindings_.end())return;
    auto& group=destinations_.at(it->second);--group.users;group.used=++clock_;bindings_.erase(it);
}
void Service::invalidate(uint64_t profile,std::shared_ptr<const Topology> next) {
    ++counters_.invalidations;
    for(auto it=bindings_.begin();it!=bindings_.end();) {
        if(destinations_.at(it->second).key.profile==profile){it=bindings_.erase(it);++counters_.invalidatedBindings;}else ++it;
    }
    for(auto it=destinations_.begin();it!=destinations_.end();) {
        if(it->second.key.profile==profile) {
            const auto id=it->first;++it;++counters_.invalidatedDestinations;
            for(const auto& [key,cached]:fields_){(void)cached;counters_.invalidatedFields+=key.group==id;}
            for(const auto& [key,builder]:builders_){(void)builder;counters_.invalidatedBuilders+=key.group==id;}
            eraseGroup(id);
        }else ++it;
    }
    // Shared fields belong to their profile. A finished one survives only when
    // the new generation still holds the identical immutable source tile:
    // lookups then recompute the exit mask against the new destinations, so a
    // changed route anywhere selects a different key instead of stale content.
    // In-progress builders reference the old generation and are discarded.
    const auto stale=[=](const FieldId& key){return !key.group&&key.profile==profile;};
    const auto keep=[&](const FieldId& key,const Cached& cached) {
        return budget_.retainShared&&next&&cached.source&&key.tile>=0&&size_t(key.tile)<next->tiles.size()&&
            next->tiles[size_t(key.tile)]==cached.source;
    };
    for(const auto& [key,cached]:fields_)if(stale(key)) {
        if(keep(key,cached))++counters_.retainedFields;else ++counters_.invalidatedFields;
    }
    for(const auto& [key,builder]:builders_){(void)builder;counters_.invalidatedBuilders+=stale(key);}
    std::erase_if(fields_,[&](const auto& item){return stale(item.first)&&!keep(item.first,item.second);});
    std::erase_if(builders_,[&](const auto& item){return stale(item.first);});
    std::erase_if(fieldQueue_,stale);
}
Service::Sample Service::sample(int unit,Cell from,std::optional<Cell> aim) {
    const auto bound=bindings_.find(unit);if(bound==bindings_.end())return {Status::Capacity,{}};
    auto& group=destinations_.at(bound->second);group.used=++clock_;
    if(!group.destination->reachable(from))
        return group.destination->done()?Sample{Status::Unreachable,{}}:Sample{};
    const auto& topo=group.destination->topology();
    const int tile=topo.tileAt(from);
    if(!group.destination->tileReady(tile))return {};
    const FieldId key=resolve(bound->second,group,tile);
    auto cached=fields_.find(key);
    // A shared key names a profile, not a topology generation: correctness
    // rests on every topology change passing through invalidate(). Verify
    // the immutable source tile anyway, so a missed invalidation rebuilds
    // this field instead of reusing another generation's content.
    if(cached!=fields_.end()&&!key.group&&cached->second.source!=topo.tiles[size_t(tile)]) {
        fields_.erase(cached);cached=fields_.end();
    }
    if(cached!=fields_.end()) {
        cached->second.used=clock_;Cell next;
        if(cached->second.field.next(from,next)) {
            if(aim) {
                const int64_t dx=int64_t(aim->x)-from.x,dz=int64_t(aim->z)-from.z;
                // Favor a cardinal lane only for predominantly cardinal
                // travel. Near a diagonal, opposite sides of a dense group
                // would otherwise turn toward the same diagonal seam and
                // block each other despite having equally good parallel paths.
                const Cell straight=std::abs(dx)>2*std::abs(dz)?Cell{from.x+(dx>0?1:-1),from.z}:
                    std::abs(dz)>2*std::abs(dx)?Cell{from.x,from.z+(dz>0?1:-1)}:next;
                if(straight!=next&&topo.tileAt(straight)==tile&&topo.cost(straight)) {
                    const auto& field=cached->second.field;
                    const auto remaining=field.distance[size_t(straight.z%kTileSize)*kTileSize+straight.x%kTileSize];
                    const auto current=field.distance[size_t(from.z%kTileSize)*kTileSize+from.x%kTileSize];
                    if(uint64_t(remaining)+uint64_t(topo.cost(from))*1024==current)next=straight;
                }
            }
            return {Status::Ready,next};
        }
        const auto direction=cached->second.field.direction[size_t(from.z%kTileSize)*kTileSize+from.x%kTileSize];
        return {direction==8?Status::Arrived:Status::Unreachable,from};
    }
    if(!builders_.contains(key)) {
        if(builders_.size()>=budget_.builders)return {};
        // A shared builder holds only its key and the topology generation,
        // never the destination that first asked for it: that group may be
        // evicted while other destinations still wait for this field.
        builders_.emplace(key,key.group?std::make_shared<FieldBuilder>(group.destination,tile):
            std::make_shared<FieldBuilder>(group.destination->topologyPointer(),tile,key.exits,std::pair{key.low,key.high}));
        fieldQueue_.push_back(key);
    }
    return {};
}
Service::Sample Service::sampleCooperative(int unit,Cell from,Cell direction,std::optional<Cell> laneTarget) {
    auto result=sample(unit,from);
    if(result.status!=Status::Ready)return result;
    const auto& binding=bindings_.at(unit);
    const auto& group=destinations_.at(binding);
    const auto& topology=group.destination->topology();
    const auto resolved=resolved_.find({binding,topology.tileAt(from)});
    const auto found=resolved==resolved_.end()?fields_.end():fields_.find(resolved->second);
    if(found!=fields_.end())result.next=cooperative::corridorStep(topology,found->second.field,from,result.next,direction,laneTarget);
    return result;
}
void Service::tick(unsigned workerCount) {
    ++clock_;
    size_t remaining=budget_.destinationWork;
    while(remaining&&!destinationQueue_.empty()) {
        const auto id=destinationQueue_.front();destinationQueue_.pop_front();
        auto& d=*destinations_.at(id).destination;
        const size_t spent=d.step(std::min<size_t>(remaining,512));remaining-=spent;work_+=spent;counters_.destinationWork+=spent;
        if(!d.done())destinationQueue_.push_back(id);
    }
    // The selection and work assignment are identical with 0, 1 or 4 workers.
    // Join ALL futures even if one reports an exception, so clear/destruction
    // cannot race an outstanding job. No tasks hold World/Unit pointers.
    struct Task {FieldId key;std::shared_ptr<FieldBuilder> builder;size_t quantum;std::future<size_t> future;};
    std::vector<Task> tasks;tasks.reserve(4);
    remaining=budget_.fieldWork;
    const size_t count=std::min<size_t>(4,fieldQueue_.size());
    for(size_t i=0;i<count&&remaining;++i) {
        const auto key=fieldQueue_.front();fieldQueue_.pop_front();
        const size_t quantum=std::min<size_t>(remaining,std::max<size_t>(1,budget_.fieldWork/count));remaining-=quantum;
        tasks.push_back({key,builders_.at(key),quantum,{}});
    }
    std::exception_ptr error;
    // workerCount caps simultaneous submissions, not the logical work schedule.
    for(size_t base=0;base<tasks.size();) {
        const size_t end=std::min(tasks.size(),base+std::max(1u,std::min(workerCount,4u)));
        for(size_t i=base;i<end;++i)if(workerCount) {
            try {
                const auto builder=tasks[i].builder;const auto quantum=tasks[i].quantum;
                tasks[i].future=submitWork([builder,quantum]{return builder->step(quantum);});
            } catch(...) {if(!error)error=std::current_exception();}
        }
        for(size_t i=base;i<end;++i) {
            try {if(!workerCount || tasks[i].future.valid()) {
                const size_t spent=workerCount?tasks[i].future.get():tasks[i].builder->step(tasks[i].quantum);
                work_+=spent;counters_.fieldWork+=spent;} }
            catch(...) {if(!error)error=std::current_exception();}
        }
        base=end;
    }
    if(error)std::rethrow_exception(error);
    for(auto& task:tasks) {
        if(!task.builder->done()) {fieldQueue_.push_back(task.key);continue;}
        if(fields_.size()>=budget_.fields) {
            auto oldest=fields_.begin();
            for(auto it=fields_.begin();it!=fields_.end();++it)if(it->second.used<oldest->second.used)oldest=it;
            fields_.erase(oldest);++counters_.evictedFields;
        }
        const auto& field=task.builder->field();
        const uint64_t fingerprint=field.hash();
        publishedHash_=publishedHash_*1099511628211ull ^ fingerprint ^ task.key.checksum();
        fields_.emplace(task.key,Cached{field,clock_,fingerprint,task.key.group?nullptr:
            task.builder->topology().tiles[size_t(task.key.tile)]});
        builders_.erase(task.key);++counters_.fieldsBuilt;
        counters_.sharedFieldsBuilt+=!task.key.group;
    }
}
uint64_t Service::checksum() const {
    uint64_t h=1469598103934665603ull;
    const auto mix=[&](uint64_t value){h=(h^value)*1099511628211ull;};
    mix(clock_);mix(nextGroup_);mix(publishedHash_);mix(work_);
    mix(budget_.destinations);mix(budget_.fields);mix(budget_.builders);mix(budget_.bindings);
    mix(budget_.destinationWork);mix(budget_.fieldWork);mix(budget_.shareDistant);mix(budget_.retainShared);
    mix(bindings_.size());for(const auto& [unit,group]:bindings_){mix(unit);mix(group);}
    mix(destinations_.size());for(const auto& [id,group]:destinations_) {
        mix(id);mix(group.keyHash);mix(group.used);mix(group.users);mix(group.destination->checksum());
    }
    mix(fields_.size());for(const auto& [key,cached]:fields_) {
        mix(key.checksum());mix(cached.used);mix(cached.fingerprint);
    }
    mix(builders_.size());for(const auto& [key,builder]:builders_) {
        mix(key.checksum());mix(builder->checksum());
    }
    mix(resolved_.size());for(const auto& [key,id]:resolved_){mix(key.first);mix(uint32_t(key.second));mix(id.checksum());}
    mix(fieldQueue_.size());for(const auto& key:fieldQueue_)mix(key.checksum());
    mix(destinationQueue_.size());for(auto id:destinationQueue_)mix(id);
    return h;
}
size_t Service::bytes() const {
    // Conservative payload plus node allowance; topology memory is owned and
    // capped by the World profile cache, not multiplied by subscriber count.
    size_t total=sizeof(*this)+bindings_.size()*64+fields_.size()*(sizeof(Cached)+64)+builders_.size()*(sizeof(FieldBuilder)+96);
    for(const auto& [id,group]:destinations_) {
        (void)id;total+=sizeof(Group)+192+group.key.goals.capacity()*sizeof(Cell)*2+group.destination->bytes();
    }
    total+=resolved_.size()*(sizeof(FieldKey)+sizeof(FieldId)+64);
    return total+(fieldQueue_.size()*sizeof(FieldId)+destinationQueue_.size()*sizeof(uint64_t))*2;
}
}
