#include "flowservice.h"
#include "flowworkers.h"
#include <algorithm>
#include <functional>
#include <future>

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
    std::erase_if(fields_,[=](const auto& item){return item.first.first==id;});
    std::erase_if(builders_,[=](const auto& item){return item.first.first==id;});
    std::erase_if(fieldQueue_,[=](const auto& key){return key.first==id;});
}
bool Service::evictGroup() {
    auto oldest=destinations_.end();
    for(auto it=destinations_.begin();it!=destinations_.end();++it)
        if(!it->second.users && (oldest==destinations_.end()||it->second.used<oldest->second.used))oldest=it;
    if(oldest==destinations_.end())return false;
    eraseGroup(oldest->first);return true;
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
        destinations_.emplace(id,Group{key,std::move(destination),++clock_,key.checksum(),0});
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
void Service::invalidate(uint64_t profile) {
    for(auto it=bindings_.begin();it!=bindings_.end();) {
        if(destinations_.at(it->second).key.profile==profile)it=bindings_.erase(it);else ++it;
    }
    for(auto it=destinations_.begin();it!=destinations_.end();) {
        if(it->second.key.profile==profile) {const auto id=it->first;++it;eraseGroup(id);}else ++it;
    }
}
Service::Sample Service::sample(int unit,Cell from) {
    const auto bound=bindings_.find(unit);if(bound==bindings_.end())return {Status::Capacity,{}};
    auto& group=destinations_.at(bound->second);group.used=++clock_;
    if(!group.destination->reachable(from))
        return group.destination->done()?Sample{Status::Unreachable,{}}:Sample{};
    const auto& topo=group.destination->topology();
    if(!group.destination->tileReady(topo.tileAt(from)))return {};
    const FieldKey key{bound->second,topo.tileAt(from)};
    const auto cached=fields_.find(key);
    if(cached!=fields_.end()) {
        cached->second.used=clock_;Cell next;
        if(cached->second.field.next(from,next))return {Status::Ready,next};
        const auto direction=cached->second.field.direction[size_t(from.z%kTileSize)*kTileSize+from.x%kTileSize];
        return {direction==8?Status::Arrived:Status::Unreachable,from};
    }
    if(!builders_.contains(key)) {
        if(builders_.size()>=budget_.builders)return {};
        builders_.emplace(key,std::make_shared<FieldBuilder>(group.destination,key.second));fieldQueue_.push_back(key);
    }
    return {};
}
void Service::tick(unsigned workerCount) {
    ++clock_;
    size_t remaining=budget_.destinationWork;
    while(remaining&&!destinationQueue_.empty()) {
        const auto id=destinationQueue_.front();destinationQueue_.pop_front();
        auto& d=*destinations_.at(id).destination;
        const size_t spent=d.step(std::min<size_t>(remaining,512));remaining-=spent;work_+=spent;
        if(!d.done())destinationQueue_.push_back(id);
    }
    // The selection and work assignment are identical with 0, 1 or 4 workers.
    // Join ALL futures even if one reports an exception, so clear/destruction
    // cannot race an outstanding job. No tasks hold World/Unit pointers.
    struct Task {FieldKey key;std::shared_ptr<FieldBuilder> builder;size_t quantum;std::future<size_t> future;};
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
            try {if(!workerCount || tasks[i].future.valid())
                work_+=workerCount?tasks[i].future.get():tasks[i].builder->step(tasks[i].quantum); }
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
            fields_.erase(oldest);
        }
        const auto& field=task.builder->field();
        const uint64_t fingerprint=field.hash();
        publishedHash_=publishedHash_*1099511628211ull ^ fingerprint ^ task.key.first ^ uint64_t(task.key.second);
        fields_.emplace(task.key,Cached{field,clock_,fingerprint});builders_.erase(task.key);
    }
}
uint64_t Service::checksum() const {
    uint64_t h=1469598103934665603ull;
    const auto mix=[&](uint64_t value){h=(h^value)*1099511628211ull;};
    mix(clock_);mix(nextGroup_);mix(publishedHash_);mix(work_);
    mix(budget_.destinations);mix(budget_.fields);mix(budget_.builders);mix(budget_.bindings);
    mix(budget_.destinationWork);mix(budget_.fieldWork);
    mix(bindings_.size());for(const auto& [unit,group]:bindings_){mix(unit);mix(group);}
    mix(destinations_.size());for(const auto& [id,group]:destinations_) {
        mix(id);mix(group.keyHash);mix(group.used);mix(group.users);mix(group.destination->checksum());
    }
    mix(fields_.size());for(const auto& [key,cached]:fields_) {
        mix(key.first);mix(key.second);mix(cached.used);mix(cached.fingerprint);
    }
    mix(builders_.size());for(const auto& [key,builder]:builders_) {
        mix(key.first);mix(key.second);mix(builder->checksum());
    }
    mix(fieldQueue_.size());for(const auto& key:fieldQueue_){mix(key.first);mix(key.second);}
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
    return total+(fieldQueue_.size()*sizeof(FieldKey)+destinationQueue_.size()*sizeof(uint64_t))*2;
}
}
