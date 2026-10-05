#include "cooperative.h"
#include <algorithm>
#include <array>
#include <cstdlib>

namespace tak::sim::cooperative {
namespace {
using Cell=flow::Cell;
constexpr std::array<Cell,8> directions{{{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}}};
int sign(int n) {return (n>0)-(n<0);}
int bucket(int n) {return n>=0?n/16:(n-15)/16;}
struct Bounds {int x0,z0,x1,z1;};
Bounds bounds(Cell a,Cell b,int x,int z) {
    return {std::min(a.x,b.x)-x/2,std::min(a.z,b.z)-z/2,
            std::max(a.x,b.x)-x/2+x-1,std::max(a.z,b.z)-z/2+z-1};
}
bool overlaps(Bounds a,Bounds b) {return a.x0<=b.x1&&b.x0<=a.x1&&a.z0<=b.z1&&b.z0<=a.z1;}
Bounds passageBounds(Traffic::Passage p) {
    const int width=p.extentWidth?p.extentWidth:p.width;
    if(p.first.x!=p.last.x) {
        const int low=p.first.z-width/2;
        return {std::min(p.first.x,p.last.x),low,std::max(p.first.x,p.last.x)-1,low+width-1};
    }
    const int low=p.first.x-width/2;
    return {low,std::min(p.first.z,p.last.z),low+width-1,std::max(p.first.z,p.last.z)-1};
}
int root(int64_t value) {int lo=0,hi=65536;while(lo<hi){const int mid=lo+(hi-lo+1)/2;if(int64_t(mid)*mid<=value)lo=mid;else hi=mid-1;}return lo;}
}
void Traffic::beginTick(uint32_t now) {
    if(started_&&tick_==now)return;
    started_=true;tick_=now;probeRemaining_=8192;conflictRemaining_=8192;admitted_.clear();
    // A spent allowance advances from the last caller that actually did work,
    // not the end of its admission window. Advancing both cursors separately
    // can repeatedly serve the first member of each 64-caller window while
    // starving every other member. Unspent windows still rotate normally.
    const int resumeAfter=exhausted_?chargedLast_:workCursor_;
    chargedAfter_=exhausted_?chargedLast_:0;chargedLast_=chargedAfter_;exhausted_=false;
    auto it=pending_.upper_bound(resumeAfter);
    if(it==pending_.end()&&!pending_.empty()) {
        it=pending_.begin();chargedAfter_=chargedLast_=0;
    }
    for(size_t n=0,limit=std::min<size_t>(64,pending_.size());n<limit;++n) {
        if(it==pending_.end())it=pending_.begin();
        workCursor_=*it;admitted_.insert(*it);it=pending_.erase(it);
    }
}
bool Traffic::spend(int id,size_t weight,bool footprint,bool* deferred) {
    if(id<=chargedAfter_){if(deferred)*deferred=true;return false;}
    auto& remaining=footprint?probeRemaining_:conflictRemaining_;
    if(weight>remaining){exhausted_=true;if(deferred)*deferred=true;return false;}
    remaining-=weight;chargedLast_=std::max(chargedLast_,id);
    if(!remaining)exhausted_=true;
    if(footprint)totals_.probes+=weight;
    return true;
}
void Traffic::release(int id,Record& r) {
    if(!r.claim)return;
    const auto b=bounds(r.claimFrom,r.claimTo,r.footX,r.footZ);
    for(int z=bucket(b.z0);z<=bucket(b.z1);++z)for(int x=bucket(b.x0);x<=bucket(b.x1);++x) {
        auto at=claims_.find({x,z,r.coordination});if(at==claims_.end())continue;
        links_-=at->second.erase(id);if(at->second.empty())claims_.erase(at);
    }
    r.claim=false;
}
void Traffic::erase(std::map<int,Record>::iterator it) {
    const int id=it->first;const auto& r=it->second;
    release(id,it->second);
    releasePassage(it->second);
    const Group key{r.player,r.identity.target.x,r.identity.target.z,r.identity.targetId};
    if(auto group=groups_.find(key);group!=groups_.end()) {
        group->second.x-=r.start.x;group->second.z-=r.start.z;
        if(!--group->second.count)groups_.erase(group);
    }
    admitted_.erase(id);pending_.erase(id);records_.erase(it);
}
void Traffic::cancel(int id) {
    const auto it=records_.find(id);if(it!=records_.end())erase(it);
}
uint16_t Traffic::coordinationMask(int player) const {
    if(player<0||player>=int(alliances_.size()))return 0;
    return alliances_[size_t(player)]?alliances_[size_t(player)]:uint16_t(1u<<player);
}
void Traffic::setAllianceMask(int player,uint16_t mask) {
    if(player<0||player>=int(alliances_.size()))return;
    // Old claims become invisible immediately. Active members refresh their
    // records on update; the bounded prune cursor retires inactive records.
    alliances_[size_t(player)]=uint16_t(mask|(1u<<player));
}
void Traffic::releasePassage(Record& r) {
    if(!r.passage)return;
    const auto& p=*r.passage;
    const auto found=passages_.find({p.first.x,p.first.z,p.last.x,p.last.z,r.coordination});
    if(found!=passages_.end()) {
        auto& gate=found->second;
        if(r.passagePermit&&gate.active)--gate.active;
        if(r.passageWaiting&&gate.waiting[r.passageDirection>0])--gate.waiting[r.passageDirection>0];
        if(gate.members&&!--gate.members)passages_.erase(found);
    }
    r.passage.reset();r.passageDirection=0;r.passagePermit=r.passageWaiting=r.passageStale=false;
}
bool Traffic::setPassage(int id,std::optional<Passage> passage) {
    const auto found=records_.find(id);if(found==records_.end())return false;
    auto& r=found->second;int8_t direction=1;
    if(!passage&&r.passage&&overlaps(bounds(r.position,r.position,r.footX,r.footZ),passageBounds(*r.passage))) {
        // A changed route cannot give the opposite stream permission to enter
        // while this body still physically occupies the old narrow strip.
        r.passageStale=true;return true;
    }
    if(passage&&std::pair{passage->last.x,passage->last.z}<std::pair{passage->first.x,passage->first.z}) {
        std::swap(passage->first,passage->last);direction=-1;
    }
    if(passage&&r.passage&&r.passage->first==passage->first&&r.passage->last==passage->last&&r.passageDirection==direction) {
        r.passageStale=false;
        r.passage->extentWidth=std::max(r.passage->extentWidth,passage->extentWidth);
        const auto gate=passages_.find({passage->first.x,passage->first.z,passage->last.x,passage->last.z,r.coordination});
        if(gate!=passages_.end())gate->second.width=std::min(gate->second.width,passage->width);
        return true;
    }
    if(!passage||passage->first==passage->last||!passage->width) {
        releasePassage(r);return true;
    }
    const PassageKey key{passage->first.x,passage->first.z,passage->last.x,passage->last.z,r.coordination};
    if(!passages_.contains(key)) {
        // Check admission before releasing an old occupied strip. A rejected
        // replacement must not let the opposite stream enter behind this body.
        // Replacing a sole member still reuses its existing gate allocation.
        size_t removed=0;
        if(r.passage) {
            const auto& old=*r.passage;
            const auto gate=passages_.find({old.first.x,old.first.z,old.last.x,old.last.z,r.coordination});
            removed=gate!=passages_.end()&&gate->second.members==1;
        }
        if(passages_.size()-removed>=4096||logicalBytes()-removed*128+128>=memoryLimit)return false;
    }
    releasePassage(r);
    auto [it,inserted]=passages_.try_emplace(key);auto& gate=it->second;
    gate.width=inserted?passage->width:std::min(gate.width,passage->width);
    gate.maxFoot=std::max<uint16_t>(gate.maxFoot,uint16_t(std::clamp(std::max(r.footX,r.footZ),1,64)));++gate.members;
    r.passage=passage;r.passageDirection=direction;return true;
}
bool Traffic::passageWait(const Context& c,Record& r) {
    if(!r.passage)return false;
    const auto p=*r.passage;
    const auto found=passages_.find({p.first.x,p.first.z,p.last.x,p.last.z,r.coordination});
    if(found==passages_.end())return true;
    auto& gate=found->second;
    const Bounds body=bounds(c.position,c.position,r.footX,r.footZ),strip=passageBounds(p);
    if(r.passageStale&&!overlaps(body,strip)){releasePassage(r);return false;}
    const bool transverse=p.first.x!=p.last.x
        ?body.z0<=strip.z1&&strip.z0<=body.z1:body.x0<=strip.x1&&strip.x0<=body.x1;
    if(!transverse) {
        // A detour outside the mouth no longer occupies or owns the strip.
        // Keep the descriptor so a later return must request admission again.
        if(r.passagePermit&&gate.active)--gate.active;
        if(r.passageWaiting&&gate.waiting[r.passageDirection>0])--gate.waiting[r.passageDirection>0];
        r.passagePermit=r.passageWaiting=false;return false;
    }
    // Continuous bodies need a spare raw cell between opposing lanes. An
    // exactly packed pair of discrete footprints cannot safely turn abreast.
    if(gate.width>=2*gate.maxFoot+1)return false;
    const Cell from=r.passageDirection>0?p.first:p.last,to=r.passageDirection>0?p.last:p.first;
    const int64_t dx=int64_t(to.x)-from.x,dz=int64_t(to.z)-from.z,length2=dx*dx+dz*dz;
    const int length=std::max(1,root(length2));
    const int64_t progress=(int64_t(c.position.x)-from.x)*dx+(int64_t(c.position.z)-from.z)*dz;
    if(progress>length2+int64_t(std::max(r.footX,r.footZ))*length) {releasePassage(r);return false;}
    if(progress < -int64_t(12)*length)return false;
    if(r.passagePermit)return false;
    const size_t own=r.passageDirection>0,other=!own;
    if(!r.passageWaiting){++gate.waiting[own];r.passageWaiting=true;}
    if(!gate.active) {
        if(!gate.direction||!gate.waiting[gate.direction>0])gate.direction=r.passageDirection;
        else if(gate.batch&&gate.waiting[gate.direction<0])gate.direction=int8_t(-gate.direction);
        gate.batch=0;gate.since=c.tick;
    }
    if(gate.direction!=r.passageDirection)return true;
    if(gate.waiting[other]&&(gate.batch>=8||c.tick-gate.since>=120u))return true;
    --gate.waiting[own];r.passageWaiting=false;r.passagePermit=true;++gate.active;++gate.batch;
    return false;
}
Traffic::Record* Traffic::remember(const Context& c) {
    // Match terrain snapshots' footprint limit before any callback or bounded
    // geometry arithmetic. Invalid external contexts cannot underpay a larger
    // physical probe or retain an earlier valid body's movement claims.
    if(c.footX<1||c.footX>64||c.footZ<1||c.footZ>64){cancel(c.id);return nullptr;}
    const flow::Traffic::Identity identity{c.controller,c.target,c.missionKind,c.targetId};
    auto it=records_.find(c.id);
    if(it!=records_.end()) {
        auto& r=it->second;
        if(r.identity.target!=c.target||r.identity.kind!=c.missionKind||r.identity.targetId!=c.targetId||
           r.issued!=c.issuedTick||r.player!=c.player||r.coordination!=coordinationMask(c.player)||r.footX!=c.footX||r.footZ!=c.footZ) {
            erase(it);it=records_.end();
        } else {
            if(r.identity.controller!=c.controller)r.retryPolicy=0;
            r.identity.controller=c.controller;
        }
    }
    if(it==records_.end()) {
        if(records_.size()>=maxRecords||logicalBytes()+65536>=memoryLimit)return nullptr;
        Record r;r.identity=identity;r.player=c.player;r.coordination=coordinationMask(c.player);r.footX=c.footX;r.footZ=c.footZ;
        r.issued=c.issuedTick;r.start=r.position=c.position;r.seen=r.progress=c.tick;
        it=records_.emplace(c.id,r).first;
        auto& group=groups_[{c.player,c.target.x,c.target.z,c.targetId}];
        group.x+=c.position.x;group.z+=c.position.z;++group.count;
    }
    auto& r=it->second;
    if(r.position!=c.position){r.progress=c.tick;r.position=c.position;r.retryPolicy=0;}
    r.seen=c.tick;return &r;
}
void Traffic::registerMove(const Context& c) {
    if(remember(c)&&c.plainMove)arrivals_.registerMove(c);
}
std::optional<Traffic::Cell> Traffic::corridorDirection(const Context& c) const {
    const auto it=groups_.find({c.player,c.target.x,c.target.z,c.targetId});
    if(it==groups_.end()||!it->second.count)return {};
    const auto& g=it->second;
    return Cell{int(int64_t(c.target.x)-g.x/int64_t(g.count)),int(int64_t(c.target.z)-g.z/int64_t(g.count))};
}
std::optional<Traffic::Cell> Traffic::corridorAim(const Context& c) const {
    const auto member=records_.find(c.id);
    const auto group=groups_.find({c.player,c.target.x,c.target.z,c.targetId});
    if(member==records_.end()||group==groups_.end()||group->second.count<2)return {};
    const auto& g=group->second;
    const int64_t cx=g.x/int64_t(g.count),cz=g.z/int64_t(g.count);
    const int64_t dx=int64_t(c.target.x)-cx,dz=int64_t(c.target.z)-cz,norm=dx*dx+dz*dz;
    if(!norm)return c.target;
    const int64_t lateral=(member->second.start.x-cx)*-dz+(member->second.start.z-cz)*dx;
    int64_t x=-dz*lateral/norm,z=dx*lateral/norm;
    const int radius=root(arrivals_.arrivalRadiusSquared(c));
    const int length=root(x*x+z*z);
    if(length>radius&&length){x=x*radius/length;z=z*radius/length;}
    return Cell{c.target.x+int(x),c.target.z+int(z)};
}
bool Traffic::conflict(int id,const Record& r,Cell from,Cell to,bool* deferred) {
    const auto b=bounds(from,to,r.footX,r.footZ);
    size_t examined=0;
    for(int z=bucket(b.z0);z<=bucket(b.z1);++z)for(int x=bucket(b.x0);x<=bucket(b.x1);++x) {
        const auto at=claims_.find({x,z,r.coordination});if(at==claims_.end())continue;
        for(int other:at->second) {
            if(other==id)continue;
            if(++examined>128){if(deferred)*deferred=true;return true;}
            if(!spend(id,1,false,deferred))return true;
            const auto peer=records_.find(other);if(peer==records_.end())continue;
            const auto& p=peer->second;
            if(!p.claim||p.until<=tick_||p.coordination!=coordinationMask(p.player))continue;
            if(overlaps(b,bounds(p.claimFrom,p.claimTo,p.footX,p.footZ))) {
                ++totals_.conflicts;return true;
            }
        }
    }
    return false;
}
bool Traffic::reserve(int id,Record& r,Cell from,Cell to,uint32_t until) {
    if(conflict(id,r,from,to))return false;
    const auto b=bounds(from,to,r.footX,r.footZ);
    const size_t links=size_t(bucket(b.x1)-bucket(b.x0)+1)*size_t(bucket(b.z1)-bucket(b.z0)+1);
    if(links_+links>maxLinks||claims_.size()+links>maxBuckets||logicalBytes()+links*192>=memoryLimit)return false;
    release(id,r);r.claimFrom=from;r.claimTo=to;r.claim=true;r.until=until;
    for(int z=bucket(b.z0);z<=bucket(b.z1);++z)for(int x=bucket(b.x0);x<=bucket(b.x1);++x)
        links_+=claims_[{x,z,r.coordination}].insert(id).second;
    return true;
}
bool Traffic::allowFollowerStep(int id,Cell from,Cell to) const {
    const auto owner=records_.find(id);
    if(owner==records_.end())return false;
    const auto& r=owner->second;
    if(r.claim||r.count||r.yielding||r.arrivalRoute||r.passage||
       r.coordination!=coordinationMask(r.player))return false;
    const auto b=bounds(from,to,r.footX,r.footZ);
    size_t examined=0;
    for(int z=bucket(b.z0);z<=bucket(b.z1);++z)for(int x=bucket(b.x0);x<=bucket(b.x1);++x) {
        const auto at=claims_.find({x,z,r.coordination});if(at==claims_.end())continue;
        for(int other:at->second) {
            if(other==id)continue;
            if(++examined>128)return false;
            const auto peer=records_.find(other);if(peer==records_.end())continue;
            const auto& p=peer->second;
            if(p.claim&&p.until>tick_&&p.coordination==coordinationMask(p.player)&&
               overlaps(b,bounds(p.claimFrom,p.claimTo,p.footX,p.footZ)))return false;
        }
    }
    return true;
}
bool Traffic::legal(const Context& c,Record& r,Cell from,Cell to,bool* deferred) {
    if(c.localDeferred&&*c.localDeferred){if(deferred)*deferred=true;return false;}
    const size_t weight=size_t(std::clamp(c.footX,1,64))*size_t(std::clamp(c.footZ,1,64));
    if(!spend(c.id,weight,true,deferred))return false;
    const bool clear=c.free&&c.free(to);
    if(c.localDeferred&&*c.localDeferred){if(deferred)*deferred=true;return false;}
    return clear&&!conflict(c.id,r,from,to,deferred);
}
Traffic::Plan Traffic::plan(const Context& c,Record& r,bool yield) {
    constexpr int radius=12,width=radius*2+1,cells=width*width;
    constexpr size_t visits=384;
    if(c.id<=chargedAfter_||!c.free){++totals_.deferredSearches;return Plan::Deferred;}
    ++totals_.searches;
    const Cell aim=c.steeringTarget.value_or(c.target);
    const Cell f{sign(aim.x-c.position.x),sign(aim.z-c.position.z)};
    if(f==Cell{}){++totals_.completeFailures;return Plan::NoRoute;}
    bool deferred=false;
    // Opposing streams both use their own right-hand shoulder. A yielding
    // member commits to its shoulder until the priority member has passed.
    const Cell side{-f.z,f.x};
    const int norm=f.x*f.x+f.z*f.z;
    const int total=(aim.x-c.position.x)*f.x+(aim.z-c.position.z)*f.z;
    const int stride=std::clamp(std::max(c.footX,c.footZ)+1,2,6);
    const int required=std::min(total,stride*norm);
    const auto yieldsRoom=[&](Cell at) {
        if(!c.obstruction)return true;
        const auto& peer=*c.obstruction;Cell next=peer.position;
        if(peer.steeringTarget) {
            next.x+=sign(peer.steeringTarget->x-peer.position.x);
            next.z+=sign(peer.steeringTarget->z-peer.position.z);
        }
        // Clearing one immediate step can merely retreat into the same lane.
        // Project its one-step sweep until it has passed every candidate in
        // this local window. Intersect translation intervals on both axes so
        // diagonal motion remains a strip rather than one large rectangle.
        const auto body=bounds(at,at,c.footX,c.footZ);
        const auto sweep=bounds(peer.position,next,peer.footX,peer.footZ);
        int first=0,last=radius+std::max(std::abs(c.position.x-peer.position.x),std::abs(c.position.z-peer.position.z))+
            std::max(c.footX,c.footZ)+std::max(peer.footX,peer.footZ);
        const auto interval=[&](int b0,int b1,int p0,int p1,int direction) {
            if(!direction)return b0<=p1&&p0<=b1;
            if(direction>0){first=std::max(first,b0-p1);last=std::min(last,b1-p0);}
            else {first=std::max(first,p0-b1);last=std::min(last,p1-b0);}
            return first<=last;
        };
        return !interval(body.x0,body.x1,sweep.x0,sweep.x1,next.x-peer.position.x)||
               !interval(body.z0,body.z1,sweep.z0,sweep.z1,next.z-peer.position.z);
    };
    std::array<int16_t,cells> parent;parent.fill(-1);
    std::array<uint8_t,cells> known{};
    std::array<uint16_t,cells> queue{};
    const auto index=[&](Cell at){return (at.z-c.position.z+radius)*width+at.x-c.position.x+radius;};
    const auto point=[&](int i){return Cell{c.position.x+i%width-radius,c.position.z+i/width-radius};};
    const auto available=[&](Cell at) {
        if(std::abs(at.x-c.position.x)>radius||std::abs(at.z-c.position.z)>radius)return false;
        auto& value=known[size_t(index(at))];
        if(!value)value=legal(c,r,at,at,&deferred)?2:1;
        return value==2;
    };
    const auto connected=[&](Cell at,int progress) {
        if(yield)return true;
        const Cell rejoin=progress==total?aim:Cell{c.position.x+f.x*(progress/norm),c.position.z+f.z*(progress/norm)};
        if(!c.terrainFree)return at==rejoin;
        if(std::max(std::abs(rejoin.x-at.x),std::abs(rejoin.z-at.z))>radius)deferred=true;
        return flow::directRoute(at,rejoin,radius,[&](Cell cell) {
            const size_t weight=size_t(std::clamp(c.footX,1,64))*size_t(std::clamp(c.footZ,1,64));
            if(!spend(c.id,weight,true,&deferred))return false;
            const bool clear=c.terrainFree(cell);
            if(c.localDeferred&&*c.localDeferred){deferred=true;return false;}
            return clear;
        },[&](Cell cell){return cell==rejoin;});
    };
    std::array<uint8_t,8> order{};for(unsigned i=0;i<8;++i)order[i]=uint8_t(i);
    const auto preference=[&](Cell d) {
        const int forward=d.x*f.x+d.z*f.z,lateral=d.x*side.x+d.z*side.z;
        return yield?lateral*8+forward:forward*8+lateral;
    };
    // Eight insertion-sort entries keep the entire planner scratch on stack.
    for(size_t i=1;i<order.size();++i)for(size_t j=i;j&&preference(directions[order[j]])>preference(directions[order[j-1]]);--j)
        std::swap(order[j],order[j-1]);
    const int origin=index(c.position);parent[size_t(origin)]=int16_t(origin);queue[0]=uint16_t(origin);
    size_t head=0,tail=1;int found=-1,opposite=-1,fallback=-1;
    const bool stalledFallback=!yield&&c.tick-r.progress>=180u&&c.obstruction&&
        c.obstructionFriendly&&!c.obstruction->idle;
    while(head<tail&&head<visits&&probeRemaining_&&!(c.localDeferred&&*c.localDeferred)) {
        const int current=queue[head++];const Cell at=point(current);
        const int progress=(at.x-c.position.x)*f.x+(at.z-c.position.z)*f.z;
        const int lateral=(at.x-c.position.x)*side.x+(at.z-c.position.z)*side.z;
        // Terrain can close the prescribed right shoulder. Remember a proved
        // left exit in the same bounded search so that a legal opposing pair
        // cannot wait forever for a shoulder that does not exist.
        if(yield&&opposite<0&&lateral<=-norm&&yieldsRoom(at))opposite=current;
        // A stationary same-way queue can fill the only forward footprint.
        // Retain a lane-clearing shoulder from this search as a last resort;
        // it must never preempt an available connected forward exit or move
        // aside for a parked body that has no movement to unblock.
        if(stalledFallback&&fallback<0&&std::abs(lateral)>=norm&&yieldsRoom(at))fallback=current;
        if(current!=origin&&(yield?lateral>=norm&&yieldsRoom(at):(progress>=required&&progress<=total))&&connected(at,progress)) {found=current;break;}
        for(uint8_t d:order) {
            const Cell v=directions[d],next{at.x+v.x,at.z+v.z};
            if(std::abs(next.x-c.position.x)>radius||std::abs(next.z-c.position.z)>radius)continue;
            const int n=index(next);if(parent[size_t(n)]>=0||!available(next))continue;
            if(v.x&&v.z&&(!available({at.x+v.x,at.z})||!available({at.x,at.z+v.z})))continue;
            parent[size_t(n)]=int16_t(current);queue[tail++]=uint16_t(n);
        }
    }
    if(c.localDeferred&&*c.localDeferred){++totals_.deferredSearches;return Plan::Deferred;}
    if(found<0)found=opposite;
    if(found<0&&fallback>=0){found=fallback;yield=true;}
    if(found<0) {
        if(deferred||head<tail){++totals_.deferredSearches;return Plan::Deferred;}
        ++totals_.completeFailures;return Plan::NoRoute;
    }
    std::array<uint8_t,48> reverse{};size_t count=0;
    for(int at=found;at!=origin;at=parent[size_t(at)]) {
        if(count==reverse.size()){++totals_.deferredSearches;return Plan::Deferred;}
        const Cell a=point(parent[size_t(at)]),b=point(at),delta{b.x-a.x,b.z-a.z};
        for(unsigned d=0;d<8;++d)if(directions[d]==delta){reverse[count++]=uint8_t(d);break;}
    }
    release(c.id,r);r.steps={};r.count=uint8_t(count);r.next=0;r.origin=c.position;r.aim=aim;r.direction=f;
    r.yielding=yield;r.arrivalRoute=false;r.waiting=c.tick;r.until=c.tick+180;
    for(size_t i=0;i<count;++i)r.steps[i]=reverse[count-i-1];
    r.retryPolicy=0;++totals_.routes;return Plan::Ready;
}
Traffic::Result Traffic::follow(const Context& c,Record& r) {
    Result out;out.arrivalApproach=r.arrivalRoute;
    if(c.tick-std::max(r.progress,r.waiting)>180u) {release(c.id,r);r.count=r.next=0;r.yielding=r.arrivalRoute=false;out.repath=true;return out;}
    if(r.claim&&c.position==r.claimTo) {
        const int steps=std::max(std::abs(r.claimTo.x-r.claimFrom.x),std::abs(r.claimTo.z-r.claimFrom.z));
        r.next=uint8_t(std::min<int>(r.count,r.next+steps));r.origin=r.claimTo;release(c.id,r);
    }
    if(r.next>=r.count) {
        // The passing interval starts after reaching the shoulder. Keep it
        // even after the nearby obstruction disappears: that is the opening
        // the other member needs in order to pass.
        if(r.count&&r.yielding)r.waiting=c.tick;
        r.count=r.next=0;r.arrivalRoute=false;
        if(r.yielding&&c.tick-r.waiting<120u) {
            out.wait=true;++totals_.waits;return out;
        }
        r.yielding=false;out.repath=true;return out;
    }
    if(r.claim) {
        if(r.until<=c.tick){release(c.id,r);r.count=r.next=0;r.yielding=r.arrivalRoute=false;out.repath=true;return out;}
        out.detour=r.claimTo;return out;
    }
    const Cell d=directions[r.steps[r.next]];Cell at=r.origin;
    unsigned n=0;
    while(r.next+n<r.count&&n<4&&directions[r.steps[r.next+n]]==d) {
        const Cell next{at.x+d.x,at.z+d.z};
        if(!legal(c,r,at,next)||(d.x&&d.z&&(!legal(c,r,at,{next.x,at.z})||!legal(c,r,at,{at.x,next.z})))) {
            // Another member may have entered the future route. Retain the
            // current lane until a bounded retry can repair the reservation.
            if(!(c.localDeferred&&*c.localDeferred)&&c.blocked>=2&&admitted_.erase(c.id)){r.count=r.next=0;r.yielding=r.arrivalRoute=false;release(c.id,r);}
            pending_.insert(c.id);out.wait=true;++totals_.waits;return out;
        }
        at=next;++n;
    }
    if(!reserve(c.id,r,r.origin,at,c.tick+90)) {pending_.insert(c.id);out.wait=true;++totals_.waits;return out;}
    out.detour=at;return out;
}
bool Traffic::updateUnblocked(const Context& c) {
    if(!c.plainMove||c.goalReached||c.blocked>=2||c.obstruction||!c.neighbors.empty()||
       c.arrivalReachable||c.contactReachable||(c.localDeferred&&*c.localDeferred)||!claims_.empty())return false;
    const auto it=records_.find(c.id);if(it==records_.end())return false;
    auto& r=it->second;
    if(r.identity!=flow::Traffic::Identity{c.controller,c.target,c.missionKind,c.targetId}||
       r.issued!=c.issuedTick||r.player!=c.player||r.coordination!=coordinationMask(c.player)||
       r.footX!=c.footX||r.footZ!=c.footZ||
       r.claim||r.count||r.yielding||r.arrivalRoute||r.passage||arrivals_.nearArrival(c))return false;
    // These coordinators have independent admission tables. Update arrivals
    // first so a decline is entirely observational: the adapter may still exit
    // early on a body-query budget or density fallback without calling update.
    if(!arrivals_.updateUnblocked(c))return false;
    beginTick(c.tick);
    if(r.position!=c.position){r.progress=c.tick;r.position=c.position;r.retryPolicy=0;}
    r.seen=c.tick;return true;
}
Traffic::Result Traffic::update(const Context& c) {
    beginTick(c.tick);
    Record* record=remember(c);if(!record){Result out;out.wait=true;return out;}
    auto& r=*record;
    bool opposing=false;
    if(c.obstruction&&c.obstructionFriendly&&!c.obstruction->idle&&c.obstruction->steeringTarget) {
        const Cell aim=c.steeringTarget.value_or(c.target);
        const int dx=sign(aim.x-c.position.x),dz=sign(aim.z-c.position.z);
        const int ox=sign(c.obstruction->steeringTarget->x-c.obstruction->position.x);
        const int oz=sign(c.obstruction->steeringTarget->z-c.obstruction->position.z);
        opposing=dx*ox+dz*oz<0;
    }
    bool arriving=c.goalReached;
    if(!arriving&&!opposing&&arrivals_.nearArrival(c)) {
        const int radius=root(arrivals_.arrivalRadiusSquared(c))+64;
        const int64_t dx=int64_t(c.position.x)-c.target.x,dz=int64_t(c.position.z)-c.target.z;
        arriving=dx*dx+dz*dz<=int64_t(radius)*radius;
    }
    Context arrival=c;
    if(!arriving) {arrival.blocked=0;arrival.obstruction.reset();arrival.neighbors={};arrival.arrivalReachable={};arrival.contactReachable={};}
    Result out=arrivals_.update(arrival);
    if(c.localDeferred&&*c.localDeferred){out.wait=true;out.repath=false;return out;}
    if(out.settled){release(c.id,r);releasePassage(r);r.count=r.next=0;r.yielding=r.arrivalRoute=false;return out;}
    const auto owned=[approaching=out.arrivalApproach](Result result) {
        result.arrivalApproach|=approaching;return result;
    };
    if(r.arrivalRoute&&(!out.arrivalApproach||out.detour!=r.aim)) {
        release(c.id,r);r.count=r.next=0;r.yielding=r.arrivalRoute=false;
    }
    if(passageWait(c,r)) {
        if(r.passage) {
            const auto p=*r.passage;
            const Cell entry=r.passageDirection>0?p.first:p.last,exit=r.passageDirection>0?p.last:p.first;
            const Cell axis{sign(exit.x-entry.x),sign(exit.z-entry.z)};
            const int progress=(c.position.x-entry.x)*axis.x+(c.position.z-entry.z)*axis.z;
            const Cell retreatAim{entry.x-axis.x*12,entry.z-axis.z*12};
            // A short terrain prefix can first identify a gate at its mouth.
            // Waiting there blocks the admitted stream's exit. Move back to
            // the queue line using the same bounded, reserved local planner.
            if(progress>-12) {
                if(r.count&&r.aim==retreatAim)return owned(follow(c,r));
                release(c.id,r);r.count=r.next=0;r.yielding=r.arrivalRoute=false;pending_.insert(c.id);
                if(admitted_.erase(c.id)) {
                    Context retreat=c;retreat.steeringTarget=retreatAim;
                    const auto free=[&](Cell at){return (at.x-entry.x)*axis.x+(at.z-entry.z)*axis.z<=progress&&c.free(at);};
                    retreat.free=std::cref(free);
                    if(plan(retreat,r,false)==Plan::Ready)return owned(follow(retreat,r));
                }
            }
        }
        out=owned({});out.wait=true;++totals_.waits;return out;
    }
    if(r.arrivalRoute&&out.wait)return out;
    if(r.count||r.yielding)return owned(follow(c,r));
    if(out.arrivalApproach&&out.detour&&c.blocked>=2&&!out.wait) {
        // A raster proof to the reserved slot does not make the straight ray
        // from an off-centre body legal. Repair an actual refusal with short
        // footprint-proved legs while retaining the slot's arrival ownership.
        pending_.insert(c.id);
        if(admitted_.erase(c.id)) {
            Context approach=c;approach.steeringTarget=out.detour;
            if(plan(approach,r,false)==Plan::Ready){r.arrivalRoute=true;return owned(follow(approach,r));}
        }
        out.detour.reset();out.wait=true;++totals_.waits;return out;
    }
    if(!claims_.empty()&&(!c.obstruction||!c.obstructionFriendly||c.blocked<2||(arriving&&out.detour))) {
        const Cell aim=out.detour.value_or(c.steeringTarget.value_or(c.target));
        const Cell next{c.position.x+sign(aim.x-c.position.x),c.position.z+sign(aim.z-c.position.z)};
        if(conflict(c.id,r,c.position,next)){out=owned({});out.wait=true;++totals_.waits;return out;}
    }
    if(arriving&&(out.detour||out.wait))return out;
    if(!c.obstruction||!c.obstructionFriendly||c.blocked<2) {
        release(c.id,r);r.yielding=false;return out;
    }
    pending_.insert(c.id);
    const auto waiting=[&] {
        out.wait=true;++totals_.waits;
        if(c.cooperativeOpenTerrain&&!arriving&&!opposing&&!r.passage&&!c.obstruction->idle&&c.obstruction->steeringTarget) {
            const Cell aim=c.steeringTarget.value_or(c.target),peer=*c.obstruction->steeringTarget;
            const int dot=sign(aim.x-c.position.x)*sign(peer.x-c.obstruction->position.x)+
                          sign(aim.z-c.position.z)*sign(peer.z-c.obstruction->position.z);
            if(dot>0)out.followLeader=c.obstruction->id;
        }
        return out;
    };
    if(!admitted_.erase(c.id))return waiting();
    const Cell retryAim=c.steeringTarget.value_or(c.target);
    const auto& peer=*c.obstruction;
    uint8_t retryPolicy=uint8_t(0x80|(opposing?1:0)|(peer.idle?2:0)|
        (c.tick-r.progress>=180u?64:0));
    if(peer.steeringTarget)retryPolicy|=uint8_t((sign(peer.steeringTarget->x-peer.position.x)+1)<<2|
                                               (sign(peer.steeringTarget->z-peer.position.z)+1)<<4);
    // Repeat less often on open terrain or behind an unchanged idle body.
    // Constrained moving queues retain immediate admitted retries.
    // This is retry scheduling, never a retained claim about live occupancy:
    // unrelated bodies, terrain or reservations can wake it after at most
    // eight ticks plus ordinary admission. Obvious context changes wake now.
    if((c.cooperativeOpenTerrain||peer.idle)&&!opposing&&!arriving&&!r.passage&&r.retryPolicy==retryPolicy&&r.retryAim==retryAim&&r.retryPeerId==peer.id&&
       r.retryPeer==peer.position&&c.tick-r.retryTick<8u) {
        ++totals_.retrySkips;return waiting();
    }
    r.retryPolicy=0;
    const Plan planned=plan(c,r,opposing);
    if(planned==Plan::Ready)return owned(follow(c,r));
    if(c.localDeferred&&*c.localDeferred)return waiting();
    if((c.cooperativeOpenTerrain||peer.idle)&&planned==Plan::NoRoute&&!opposing&&!arriving&&!r.passage) {
        r.retryPolicy=retryPolicy;r.retryAim=retryAim;r.retryPeerId=peer.id;
        r.retryPeer=peer.position;r.retryTick=c.tick;
    }
    // A bounded local failure cannot freeze an installed terrain prefix for
    // the rest of the match. Reanchor infrequently, after genuine stationary
    // time; work-quota exhaustion remains a deferred attempt.
    if(c.id>chargedAfter_&&probeRemaining_>=size_t(std::max(1,c.footX))*size_t(std::max(1,c.footZ))&&
       conflictRemaining_&&c.tick-r.progress>=180u&&c.tick>=r.until) {
        r.until=c.tick+180;out.repath=true;return out;
    }
    return waiting();
}
void Traffic::prune(size_t budget,const std::function<bool(int,int,uint64_t,Cell,bool)>& valid) {
    arrivals_.prune(budget,valid);
    auto it=records_.upper_bound(pruneCursor_);
    for(size_t n=0,limit=std::min(budget,records_.size());n<limit&&!records_.empty();++n) {
        if(it==records_.end())it=records_.begin();
        auto at=it++;pruneCursor_=at->first;
        const auto& r=at->second;
        if(r.coordination!=coordinationMask(r.player)||!valid(at->first,r.player,r.identity.controller,r.position,arrivals_.settled(at->first,r.position)))erase(at);
        else if(at->second.claim&&at->second.until<=tick_)release(at->first,at->second);
    }
}
size_t Traffic::bytes() const {
    return sizeof(*this)+arrivals_.bytes()+records_.size()*(sizeof(Record)+64)+groups_.size()*(sizeof(Group)+sizeof(Population)+64)+
        passages_.size()*(sizeof(PassageKey)+sizeof(Gate)+64)+claims_.size()*112+links_*48+(pending_.size()+admitted_.size())*48;
}
size_t Traffic::logicalBytes() const {
    // These admission charges are protocol constants, independent of sizeof,
    // allocator layout or host pointer width. Existing arrival state has its
    // own reservation in the shared request budget and its own fixed caps.
    static_assert(sizeof(Record)+64<=320&&sizeof(Group)+sizeof(Population)+64<=128);
    static_assert(sizeof(PassageKey)+sizeof(Gate)+64<=128&&sizeof(Traffic)<=2048);
    return 2048+records_.size()*320+groups_.size()*128+passages_.size()*128+
        claims_.size()*128+links_*64+(pending_.size()+admitted_.size())*64;
}
Traffic::Stats Traffic::stats() const {
    auto result=totals_;result.records=records_.size();result.reservations=links_;result.bytes=bytes();return result;
}
uint64_t Traffic::checksum() const {
    uint64_t h=1469598103934665603ull;
    const auto mix=[&](uint64_t n){for(int i=0;i<8;++i){h^=uint8_t(n);h*=1099511628211ull;n>>=8;}};
    const auto cell=[&](Cell c){mix(c.x);mix(c.z);};
    mix(arrivals_.checksum());mix(tick_);mix(started_);mix(pruneCursor_);mix(workCursor_);mix(probeRemaining_);mix(conflictRemaining_);mix(links_);
    mix(chargedAfter_);mix(chargedLast_);mix(exhausted_);
    mix(totals_.probes);mix(totals_.searches);mix(totals_.routes);mix(totals_.waits);mix(totals_.conflicts);
    mix(totals_.completeFailures);mix(totals_.deferredSearches);mix(totals_.retrySkips);
    for(const auto& [id,r]:records_) {
        mix(id);mix(r.identity.controller);cell(r.identity.target);mix(r.identity.kind);mix(r.identity.targetId);
        mix(r.player);mix(r.coordination);mix(r.footX);mix(r.footZ);mix(r.issued);mix(r.seen);mix(r.until);mix(r.progress);mix(r.waiting);
        cell(r.position);cell(r.start);cell(r.origin);cell(r.aim);cell(r.direction);cell(r.claimFrom);cell(r.claimTo);
        mix(r.count);mix(r.next);mix(r.claim);mix(r.yielding);mix(r.arrivalRoute);for(uint8_t d:r.steps)mix(d);
        mix(bool(r.passage));if(r.passage){cell(r.passage->first);cell(r.passage->last);mix(r.passage->width);mix(r.passage->extentWidth);}
        mix(r.passageDirection);mix(r.passagePermit);mix(r.passageWaiting);mix(r.passageStale);
        cell(r.retryAim);cell(r.retryPeer);mix(r.retryTick);mix(r.retryPeerId);mix(r.retryPolicy);
    }
    for(const auto& [key,g]:groups_){mix(std::get<0>(key));mix(std::get<1>(key));mix(std::get<2>(key));mix(std::get<3>(key));mix(g.x);mix(g.z);mix(g.count);}
    for(const auto& [key,ids]:claims_){mix(std::get<0>(key));mix(std::get<1>(key));mix(std::get<2>(key));for(int id:ids)mix(id);mix(0);}
    for(const auto& [key,g]:passages_){mix(std::get<0>(key));mix(std::get<1>(key));mix(std::get<2>(key));mix(std::get<3>(key));mix(std::get<4>(key));mix(g.width);mix(g.maxFoot);mix(g.direction);mix(g.members);mix(g.active);mix(g.batch);mix(g.since);mix(g.waiting[0]);mix(g.waiting[1]);}
    for(uint16_t mask:alliances_)mix(mask);
    for(int id:pending_)mix(uint64_t(id)*2);
    for(int id:admitted_)mix(uint64_t(id)*2+1);
    return h;
}
}
