#include "flowtraffic.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
namespace tak::sim::flow {
namespace {
int64_t distance(Cell a,Cell b) {const int64_t x=int64_t(a.x)-b.x,z=int64_t(a.z)-b.z;return x*x+z*z;}
int sign(int n) {return (n>0)-(n<0);}
int root(int64_t n) {int lo=0,hi=65536;while(lo<hi){const int mid=lo+(hi-lo+1)/2;if(int64_t(mid)*mid<=n)lo=mid;else hi=mid-1;}return lo;}
constexpr std::array<Cell,8> escapeDirections{{{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}}};
uint64_t footprintArea(int x,int z) {return uint64_t(std::clamp(x,1,64)+1)*uint64_t(std::clamp(z,1,64)+1);}
bool localQuery(const Traffic::Context& c,const std::function<bool(Cell)>& query,Cell at) {
    if(c.localDeferred&&*c.localDeferred)return false;
    const bool clear=query(at);
    return clear&&!(c.localDeferred&&*c.localDeferred);
}
}
void Traffic::releaseSlot(int id,Record& r) {
    if(!r.arrivalSlot)return;
    const int x0=r.arrivalSlot->x-r.footX/2,z0=r.arrivalSlot->z-r.footZ/2;
    for(int z=z0/8;z<=(z0+r.footZ-1)/8;++z)for(int x=x0/8;x<=(x0+r.footX-1)/8;++x) {
        auto bucket=reservations_.find({x,z});
        if(bucket!=reservations_.end()){reservationLinks_-=bucket->second.erase(id);if(bucket->second.empty())reservations_.erase(bucket);}
    }
    r.arrivalSlot.reset();
}
bool Traffic::slotFree(int id,const Record& r,Cell at) const {
    if(r.footX>64||r.footZ>64)return false;
    const int x0=at.x-r.footX/2,z0=at.z-r.footZ/2;
    if(x0<0||z0<0)return false;
    size_t examined=0;
    for(int z=z0/8;z<=(z0+r.footZ-1)/8;++z)for(int x=x0/8;x<=(x0+r.footX-1)/8;++x) {
        const auto bucket=reservations_.find({x,z});if(bucket==reservations_.end())continue;
        for(int other:bucket->second) {
            if(other==id)continue;
            // Overfull buckets reject candidates conservatively. A crowded
            // destination cannot turn slot selection into an unbounded scan.
            if(++examined>128)return false;
            const auto& peer=records_.at(other);const Cell p=*peer.arrivalSlot;
            const int px=p.x-peer.footX/2,pz=p.z-peer.footZ/2;
            if(x0<px+peer.footX&&px<x0+r.footX&&z0<pz+peer.footZ&&pz<z0+r.footZ)return false;
        }
    }
    return true;
}
bool Traffic::reserveSlot(int id,Record& r,Cell at) {
    const int x0=at.x-r.footX/2,z0=at.z-r.footZ/2;
    const size_t links=size_t((x0+r.footX-1)/8-x0/8+1)*size_t((z0+r.footZ-1)/8-z0/8+1);
    if(reservationLinks_+links>65536)return false;
    size_t added=0;
    for(int z=z0/8;z<=(z0+r.footZ-1)/8;++z)for(int x=x0/8;x<=(x0+r.footX-1)/8;++x)
        added+=!reservations_.contains({x,z});
    if(reservations_.size()+added>16384)return false;
    releaseSlot(id,r);r.arrivalSlot=at;
    for(int z=z0/8;z<=(z0+r.footZ-1)/8;++z)for(int x=x0/8;x<=(x0+r.footX-1)/8;++x)
        reservationLinks_+=reservations_[{x,z}].insert(id).second;
    return true;
}
void Traffic::erase(std::map<int,Record>::iterator it) {
    releaseSlot(it->first,it->second);
    if(it->second.plain) {auto group=groups_.find(it->second.group);if(group!=groups_.end()) {
        group->second.area-=footprintArea(it->second.footX,it->second.footZ);
        if(it->second.settled)group->second.settledArea-=footprintArea(it->second.footX,it->second.footZ);
        if(!--group->second.members)groups_.erase(group);
    }}
    waiting_.erase(it->first);admitted_.erase(it->first);index_.clear(it->first);records_.erase(it);
    if(records_.empty())index_.release();
}
Traffic::Record* Traffic::lookup(int id) {
    if(const auto* s=index_.find(id))return s->record;
    if(IdIndex<Slot>::covers(id))return nullptr;
    const auto it=records_.find(id);return it==records_.end()?nullptr:&it->second;
}
const Traffic::Record* Traffic::lookup(int id) const {
    if(const auto* s=index_.find(id))return s->record;
    if(IdIndex<Slot>::covers(id))return nullptr;
    const auto it=records_.find(id);return it==records_.end()?nullptr:&it->second;
}
const Traffic::Population* Traffic::population(const Context& c) const {
    // A plain member's cached population is its own group's map node.
    const Group key{c.player,c.target.x,c.target.z,c.targetId};
    if(const auto* s=slot(c.id);s&&s->group&&s->record->group==key)return s->group;
    const auto group=groups_.find(key);return group==groups_.end()?nullptr:&group->second;
}
int Traffic::arrivalRadiusSquared(const Context& c) const {
    const auto* group=population(c);
    return !group||group->members<2?0:int(std::min<uint64_t>(512*512,group->area));
}
bool Traffic::settled(int id,Cell position) const {
    const auto* r=lookup(id);return r&&r->settled&&r->position==position;
}
void Traffic::refreshSettled(int id,Cell position) {
    if(auto* r=lookup(id);r&&r->settled)r->position=position;
}
bool Traffic::nearArrival(const Context& c) const {
    const int64_t spacing=std::max(c.footX,c.footZ)+1;
    const auto* group=population(c);
    const int64_t members=!group?1:int64_t(group->members)+1;
    const int64_t area=!group?spacing*spacing:int64_t(group->area);
    const int64_t ordinary=std::min<int64_t>(64,members*spacing);
    // Geometric area plus the ordinary constrained-queue window is enough for
    // movers making progress. A linear population radius made every refusal
    // in a large travelling army scan destination neighbors across the map.
    int64_t radius2=std::max({64*spacing*spacing,ordinary*ordinary,2*area+2*spacing*spacing});
    if(const auto* record=lookup(c.id);record&&c.tick-record->progressTick>=300u)
        radius2=std::max(radius2,members*members*spacing*spacing);
    return distance(c.position,c.target)<=radius2;
}
bool Traffic::needsArrivalNeighbors(const Context& c) const {
    if(!c.plainMove)return false;
    const auto* group=population(c);
    if(!group||!group->settledArea)return false;
    if(c.blocked<2) {
        const auto* r=lookup(c.id);
        if(!r||!r->plain||r->issuedTick!=c.issuedTick||
           r->group!=Group{c.player,c.target.x,c.target.z,c.targetId}||c.tick-r->progressTick<300u)return false;
    }
    return nearArrival(c);
}
Traffic::Record& Traffic::remember(const Context& c) {
    const Group group{c.player,c.target.x,c.target.z,c.targetId};
    Record* existing=lookup(c.id);
    if(existing&&(existing->controller!=c.controller||existing->group!=group||existing->plain!=c.plainMove||
       existing->footX!=c.footX||existing->footZ!=c.footZ||
       existing->missionKind!=c.missionKind||existing->targetId!=c.targetId||existing->issuedTick!=c.issuedTick)) {
        const auto& r=*existing;
        if(c.plainMove&&r.plain&&r.group==group&&r.issuedTick==c.issuedTick&&r.footX==c.footX&&r.footZ==c.footZ&&
           r.missionKind==c.missionKind&&r.targetId==c.targetId) {
            // Internal controller retries retain the issued mission. Keep its
            // progress instead of resetting the watchdog each time.
            existing->controller=c.controller;existing->yieldTo=0;
        } else {erase(records_.find(c.id));existing=nullptr;}
    }
    if(!existing) {
        // Unit caps already bound normal matches. Retain deterministic behavior
        // in synthetic/oversubscribed callers without growing the cache forever.
        if(records_.size()==16384) {
            auto victim=records_.upper_bound(pruneCursor_);
            if(victim==records_.end())victim=records_.begin();
            pruneCursor_=victim->first;erase(victim);
        }
        Record r;r.group=group;r.controller=c.controller;r.plain=c.plainMove;r.missionKind=c.missionKind;r.targetId=c.targetId;
        r.issuedTick=c.issuedTick;
        r.footX=c.footX;r.footZ=c.footZ;
        if(c.plainMove){r.progressTick=c.tick;r.bestDistance=distance(c.position,c.target);}
        existing=&records_.emplace(c.id,r).first->second;
        Population* population=nullptr;
        if(c.plainMove){population=&groups_[group];++population->members;population->area+=footprintArea(c.footX,c.footZ);}
        index_.set(c.id,{existing,population});
    }
    auto& record=*existing;
    if(record.settled&&record.position!=c.position) {
        if(record.plain)groups_.at(record.group).settledArea-=footprintArea(record.footX,record.footZ);
        record.settled=false;
    }
    record.seen=c.tick;record.position=c.position;
    return record;
}
void Traffic::registerMove(const Context& c) {if(c.plainMove)remember(c);}
void Traffic::cancelUnsettled(int id) {
    const auto it=records_.find(id);
    if(it!=records_.end()&&!it->second.settled)erase(it);
}
void Traffic::beginTick(uint32_t tick) {
    if(tick!=workTick_) {
        workTick_=tick;admitted_.clear();escapeBudget_.tick();
        auto job=waiting_.upper_bound(workCursor_);
        const size_t count=std::min<size_t>(256,waiting_.size());
        for(size_t n=0;n<count;++n) {
            if(job==waiting_.end())job=waiting_.begin();
            workCursor_=*job;admitted_.insert(*job);job=waiting_.erase(job);
        }
    }
}
bool Traffic::updateUnblocked(const Context& c) {
    if(!c.plainMove||c.goalReached||c.blocked>=2||c.obstruction||!c.neighbors.empty()||
       c.arrivalReachable||c.contactReachable||(c.localDeferred&&*c.localDeferred))return false;
    Record* const record=lookup(c.id);if(!record)return false;
    auto& r=*record;
    if(r.controller!=c.controller||r.group!=Group{c.player,c.target.x,c.target.z,c.targetId}||
       r.plain!=c.plainMove||r.footX!=c.footX||r.footZ!=c.footZ||r.missionKind!=c.missionKind||
       r.targetId!=c.targetId||r.issuedTick!=c.issuedTick||
       r.settled||r.arrivalSlot||r.detour||r.continuation||r.yieldTo||r.escaping||
       r.bypassSide.x||r.bypassSide.z)return false;
    beginTick(c.tick);
    r.seen=c.tick;r.position=c.position;
    const int64_t spacing=std::max(c.footX,c.footZ)+1;
    const int64_t distance2=distance(c.position,c.target);
    if(distance2+spacing*spacing<=r.bestDistance){r.bestDistance=distance2;r.progressTick=c.tick;}
    r.blockedSince=0;
    waiting_.erase(c.id);
    return true;
}
Traffic::Result Traffic::update(const Context& c) {
    Result out;
    const auto localDeferred=[&] {return c.localDeferred&&*c.localDeferred;};
    beginTick(c.tick);
    auto& record=remember(c);const Group group=record.group;
    const auto defer=[&] {
        Result result;result.wait=true;
        result.detour=record.arrivalSlot?record.arrivalSlot:record.detour;
        result.arrivalApproach=bool(record.arrivalSlot);
        waiting_.insert(c.id);return result;
    };
    if(localDeferred())return defer();
    bool admitted=false;
    const auto admit=[&] {
        if(!admitted)admitted=admitted_.erase(c.id)!=0;
        return admitted;
    };
    const int64_t spacing=std::max(c.footX,c.footZ)+1;
    const int64_t distance2=distance(c.position,c.target);
    if(c.plainMove&&distance2+spacing*spacing<=record.bestDistance) {
        record.bestDistance=distance2;record.progressTick=c.tick;
    }
    // Local avoidance can keep making physical steps without getting closer
    // to a filled destination. Retain actual mission progress across detours.
    const bool stalled=c.plainMove&&c.tick-record.progressTick>=300u;
    if(c.blocked>=2) {if(!record.blockedSince)record.blockedSince=c.tick;}
    else if(!record.yieldTo)record.blockedSince=0;
    if(record.yieldTo) {
        const auto other=records_.find(record.yieldTo);
        const auto live=c.lookup?c.lookup(record.yieldTo):std::optional<Neighbor>{};
        const int clearance=std::max(c.footX,c.footZ)+1;
        const bool passed=live&&int64_t(live->position.x-c.position.x)*record.yieldDirection.x+
            int64_t(live->position.z-c.position.z)*record.yieldDirection.z < -clearance;
        if(!live||live->idle||other==records_.end()||!live->identity||*live->identity!=record.yieldIdentity||
           other->second.controller!=record.yieldIdentity.controller||other->second.group!=record.yieldGroup||live->player!=std::get<0>(record.yieldGroup)||
           c.tick>=record.yieldUntil||passed) {
            record.yieldTo=0;record.detour.reset();record.continuation.reset();clearEscape(record);record.longDetour=false;record.bypassSide={};out.repath=!record.yieldTo;
        }
    }
    const auto settle=[&] {
        if(localDeferred()){out=defer();return;}
        releaseSlot(c.id,record);
        if(!record.settled)groups_.at(record.group).settledArea+=footprintArea(record.footX,record.footZ);
        record.settled=true;record.yieldTo=0;record.detour.reset();record.continuation.reset();clearEscape(record);record.longDetour=false;record.bypassSide={};
        waiting_.erase(c.id);admitted_.erase(c.id);out.settled=true;
    };
    const auto proofDeferred=[&] {return localDeferred()||(c.arrivalDeferred&&*c.arrivalDeferred);};
    std::optional<bool> standingConnection;
    const auto standingConnected=[&] {
        if(standingConnection)return *standingConnection;
        const bool connected=c.arrivalReachable(c.position);
        if(!proofDeferred())standingConnection=connected;
        return connected;
    };
    // Persistent Guard orders keep their destination-area parking place until
    // their target, owner, command, or standing cell changes.
    if(c.plainMove&&record.settled) {out.settled=true;return out;}
    if(c.plainMove&&c.goalReached&&!record.arrivalSlot) {settle();return out;}
    bool checkedCurrent=false;
    if(c.plainMove&&stalled&&c.arrivalReachable&&distance2<=arrivalRadiusSquared(c)) {
        // Congestion can keep a perfectly legal arrival walking around without
        // ever producing a body refusal. After genuine mission progress stops,
        // its current place in the destination area is a useful final slot.
        waiting_.insert(c.id);
        if(admit()) {
            waiting_.erase(c.id);
            // A live standing body outranks a prospective reservation. The
            // reservation's owner will choose another slot if it is occupied.
            if(localQuery(c,c.free,c.position)) {checkedCurrent=true;if(standingConnected()) {settle();return out;}}
        }
    }
    if(record.arrivalSlot) {
        const int64_t remaining=distance(c.position,*record.arrivalSlot);
        if(remaining<record.slotDistance) {record.slotDistance=remaining;record.slotUntil=c.tick+180;}
        if(!checkedCurrent&&remaining<=2&&distance2<=arrivalRadiusSquared(c)&&localQuery(c,c.free,c.position)&&c.arrivalReachable) {
            waiting_.insert(c.id);
            if(admit()) {
                waiting_.erase(c.id);
                if(standingConnected()) {settle();return out;}
            } else {out.detour=record.arrivalSlot;out.arrivalApproach=out.wait=true;return out;}
        }
        if(proofDeferred()) {out.detour=record.arrivalSlot;out.arrivalApproach=out.wait=true;return out;}
        const bool invalid=c.blocked>=2&&(!localQuery(c,c.free,*record.arrivalSlot)||!c.arrivalReachable||!c.arrivalReachable(*record.arrivalSlot));
        if(proofDeferred()) {out.detour=record.arrivalSlot;out.arrivalApproach=out.wait=true;return out;}
        if(c.tick>=record.slotUntil||invalid) {
            releaseSlot(c.id,record);out.repath=true;
        }else {out.detour=record.arrivalSlot;out.arrivalApproach=true;return out;}
    }
    if(c.plainMove&&(c.blocked>=2||stalled)) {
        // Only grow the arrival area enough to hold this actual destination
        // group. Contact propagation cannot settle a long convoy far away.
        const int64_t members=int64_t(groups_.at(group).members);
        const int64_t radius2=c.arrivalReachable&&members>1?arrivalRadiusSquared(c):
            std::max<int64_t>(64*spacing*spacing,(members+std::min<int64_t>(members,16))*spacing*spacing);
        const int64_t ordinaryRadius=std::min<int64_t>(64,members*spacing);
        const int64_t queueRadius=stalled?members*spacing:ordinaryRadius;
        const bool extended=distance2>std::max(radius2,ordinaryRadius*ordinaryRadius);
        std::optional<bool> currentFree;
        const auto standingFree=[&] {
            if(!currentFree)currentFree=c.free&&localQuery(c,c.free,c.position);
            return *currentFree;
        };
        if(distance2<=std::max(radius2,queueRadius*queueRadius)&&(!extended||standingFree()))for(const auto& n:c.neighbors) {
            if(!n.id||n.id==c.id||!n.idle||n.player!=c.player)continue;
            const auto anchor=records_.find(n.id);
            if(anchor==records_.end()||!anchor->second.settled||anchor->second.group!=group||
               anchor->second.position!=n.position)continue;
            const int dx=std::abs(c.position.x-n.position.x),dz=std::abs(c.position.z-n.position.z);
            if(dx<=(c.footX+n.footX+1)/2+1&&dz<=(c.footZ+n.footZ+1)/2+1) {
                if(c.arrivalReachable) {
                    if(!standingFree()||!c.terrainFree)continue;
                    if(c.contactReachable&&!c.contactReachable(n.position))continue;
                    // Large groups can extend beyond a single local proof ray.
                    // Contact with a settled member supplies the connection,
                    // but a wall between nearby footprints must not do so.
                    const int steps=std::max(dx,dz);Cell before=c.position;bool connected=true;
                    for(int step=1;!c.contactReachable&&step<=steps&&connected;++step) {
                        const Cell at{c.position.x+(n.position.x-c.position.x)*step/steps,
                                      c.position.z+(n.position.z-c.position.z)*step/steps};
                        connected=localQuery(c,c.terrainFree,at)&&(at.x==before.x||at.z==before.z||
                            (localQuery(c,c.terrainFree,{at.x,before.z})&&localQuery(c,c.terrainFree,{before.x,at.z})));
                        before=at;
                    }
                    if(!connected)continue;
                }
                // Touching arrived footprints need a small packing margin
                // around the destination area. Apply the same fixed bound to
                // every center, so contact cannot extend an unbounded chain.
                const int64_t contactRadius=root(radius2)+2*spacing;
                const bool contactShell=c.arrivalReachable&&c.contactReachable&&distance2<=contactRadius*contactRadius;
                if(distance2>radius2&&distance(n.position,c.target)>radius2&&!contactShell) {
                    // A terrain bottleneck can force an arrival formation into
                    // a queue. Extend only actual same-goal contact, bounded by
                    // its population and ordinarily a 64-cell window. Open-ground
                    // formations may finish one touching footprint beyond the
                    // area boundary, anchored by a body inside it. Requiring
                    // every centre inside leaves that last row circling the
                    // stopped formation. The no-progress fallback below admits
                    // longer queues only on a currently free full footprint.
                    const auto constrained=[&](Cell at) {
                        if(!c.terrainFree)return false;
                        std::array<bool,4> blocked{};
                        constexpr std::array<Cell,4> directions{{{1,0},{-1,0},{0,1},{0,-1}}};
                        for(size_t side=0;side<directions.size();++side)
                            for(int step=1;step<=spacing;++step) {
                                const auto direction=directions[side];
                                if(!localQuery(c,c.terrainFree,{at.x+direction.x*step,at.z+direction.z*step})) {
                                    blocked[side]=true;break;
                                }
                            }
                        return (blocked[0]&&blocked[1])||(blocked[2]&&blocked[3]);
                    };
                    if(!constrained(c.position)&&!constrained(n.position)&&
                       !(!c.arrivalReachable&&stalled&&standingFree()))continue;
                }
                settle();return out;
            }
        }
    }
    if(localDeferred())return defer();
    // Moving bodies can occupy a retained local leg after it was proved.
    // Recheck only on refusal and retain the leg when its shared quota defers.
    if(record.escaping&&record.detour&&c.blocked>=2) {
        bool deferred=false;
        const bool clear=directRoute(c.position,*record.detour,42,[&](Cell at) {
            if(!escapeBudget_.spend(c.id,size_t(std::max(1,c.footX))*size_t(std::max(1,c.footZ)))){deferred=true;return false;}
            return localQuery(c,c.free,at);
        },[&](Cell at){return at==*record.detour;});
        if(localDeferred())return defer();
        if(deferred){out.detour=record.detour;out.wait=true;return out;}
        if(!clear){record.detour.reset();record.continuation.reset();clearEscape(record);}
    }
    if(record.detour) {
        if(record.longDetour||record.escaping) {
            const int64_t remaining=distance(c.position,*record.detour);
            if(remaining<record.detourDistance){record.detourDistance=remaining;record.detourUntil=c.tick+300;}
        }
        if(distance(c.position,*record.detour)==0) {
            if(record.escaping&&record.escapeCount) {
                advanceEscape(record);record.detourDistance=distance(c.position,*record.detour);
                record.detourUntil=c.tick+300;out.detour=record.detour;return out;
            }
            const bool continuationFree=record.continuation&&localQuery(c,c.free,*record.continuation);
            if(localDeferred())return defer();
            clearEscape(record);
            record.detour=record.continuation;record.continuation.reset();
            if(record.detour&&continuationFree) {
                record.detourDistance=distance(c.position,*record.detour);
                record.detourUntil=c.tick+(record.longDetour?300:360);out.detour=record.detour;return out;
            }
            record.detour.reset();record.longDetour=false;out.repath=!record.yieldTo&&!record.bypassSide.x&&!record.bypassSide.z;
        } else {
            const bool invalid=c.tick>=record.detourUntil||(c.blocked>=2&&!localQuery(c,c.free,*record.detour));
            if(localDeferred())return defer();
            if(invalid) {
                record.detour.reset();record.continuation.reset();clearEscape(record);record.longDetour=false;record.bypassSide={};out.repath=!record.yieldTo;
            } else {out.detour=record.detour;return out;}
        }
    }
    // Local detours resolve transient mobile occupancy. Static clipping belongs
    // to the installed terrain route; steering away from it can cause orbits.
    const int areaSquared=c.plainMove&&c.arrivalReachable&&distance2<=528*528?arrivalRadiusSquared(c):0;
    const int areaRadius=areaSquared?root(areaSquared):0;
    if(record.bypassSide.x||record.bypassSide.z) {
        const int radius=root(arrivalRadiusSquared(c));
        if(distance2<=arrivalRadiusSquared(c)||distance2>int64_t(radius+128)*(radius+128))record.bypassSide={};
    }
    bool continuingBypass=record.bypassSide.x||record.bypassSide.z;
    const bool areaCandidate=c.plainMove&&areaSquared&&c.arrivalReachable&&
        distance2<=int64_t(areaRadius+16)*(areaRadius+16);
    const auto parked=c.obstruction?records_.find(c.obstruction->id):records_.end();
    // Turning beside a parked footprint need not produce a collision refusal.
    // A stalled mover can use the bounded forward blocker query to commit to
    // going around its own arrived group, instead of endlessly circling it.
    // An approaching member can obscure the parked row behind it. Requiring
    // the first blocker itself to be idle traps the dense waiting fringe.
    bool filledArrival=stalled&&c.plainMove&&c.arrivalReachable&&c.obstruction&&c.obstruction->mobile&&
        parked!=records_.end()&&parked->second.plain&&parked->second.group==group&&groups_.at(group).settledArea&&
        (!parked->second.settled||parked->second.position==c.obstruction->position)&&
        distance2<=int64_t(root(arrivalRadiusSquared(c))+64)*(root(arrivalRadiusSquared(c))+64);
    if(((!filledArrival&&c.blocked<2)||!c.obstruction||!c.obstruction->mobile)&&!record.yieldTo&&!areaCandidate&&!continuingBypass) {waiting_.erase(c.id);return out;}
    waiting_.insert(c.id);
    // Admission bounds detour search, not normal collision-checked following.
    // Braking here latches dense convoys long after the blocker has moved.
    if(!admit()) {out.wait=bool(record.yieldTo)||continuingBypass;return out;}
    waiting_.erase(c.id);
    if(filledArrival&&!continuingBypass&&c.arrivalReachable) {
        // A nearby destination can lie across terrain that the installed route
        // still needs to go around. Only a proved connection permits a bypass
        // steered toward that destination instead of the next terrain corner.
        // Retained legal legs preserve that established connection; a new
        // straight ray can fail behind an isolated feature after moving aside.
        // Final settlement still requires its own current connection proof.
        const bool connected=standingConnected();
        if(proofDeferred()){waiting_.insert(c.id);out.wait=true;return out;}
        if(!connected) {
            filledArrival=continuingBypass=false;record.bypassSide={};
        }
    }
    if(areaCandidate&&!record.yieldTo) {
        // Keep the global field shared. Only the final approach chooses a free
        // standing footprint inside the group's compact destination area.
        // A few inward/lateral choices distribute simultaneous arrivals instead
        // of assigning every unit the clicked centre or the same nearest edge.
        const int length=std::max(1,root(distance2));
        const int fx=sign(c.target.x-c.position.x),fz=sign(c.target.z-c.position.z);
        const int stride=std::clamp(int(spacing),2,16);
        // Reservations prevent overlap but do not mean the area filled yet.
        // Counting en-route reservations made later waves park on the near rim
        // while the intended interior positions were still empty.
        const int filledRadius=root(int64_t(groups_.at(group).settledArea)+int64_t(footprintArea(c.footX,c.footZ)));
        const int edge=std::max(0,std::min({areaRadius,length,filledRadius})-2*stride);
        const Cell entry{c.target.x+int(int64_t(c.position.x-c.target.x)*edge/length),
                         c.target.z+int(int64_t(c.position.z-c.target.z)*edge/length)};
        const int first=int(uint32_t(c.id)*2654435761u%7);
        for(int depth=0;depth<3&&!proofDeferred();++depth)for(int n=0;n<7&&!proofDeferred();++n) {
            const int side=((first+n)%7)-3;
            const Cell at{entry.x-fz*side*stride+fx*depth*stride,
                          entry.z+fx*side*stride+fz*depth*stride};
            if(distance(at,c.target)>areaSquared||distance(at,c.position)>64*64)continue;
            if(!slotFree(c.id,record,at)||!localQuery(c,c.free,at)||!c.arrivalReachable(at))continue;
            if(at==c.position) {settle();return out;}
            if(reserveSlot(c.id,record,at)) {
                record.detour.reset();record.continuation.reset();clearEscape(record);record.longDetour=false;record.bypassSide={};record.slotUntil=c.tick+180;
                record.slotDistance=distance(c.position,at);
                out.detour=at;out.arrivalApproach=true;return out;
            }
        }
        if(localDeferred())return defer();
        if(proofDeferred()) {waiting_.insert(c.id);return out;}
        // The no-progress arrival above can use a legal current position.
        // Doing so immediately here would park the next wave across the entry
        // before the reserved interior slots had actually been occupied.
        if(!continuingBypass&&((!filledArrival&&c.blocked<2)||!c.obstruction||!c.obstruction->mobile))return out;
    }
    // Maze routes can initially lead away from the mission destination.
    // Local avoidance follows that installed route, not a straight-line goal.
    const auto aim=c.steeringTarget.value_or(c.target);
    const int dx=sign(aim.x-c.position.x),dz=sign(aim.z-c.position.z);
    if(!dx&&!dz)return out;
    const int step=std::clamp(std::max(c.footX,c.footZ)+1,2,16);
    const auto clear=[&](Cell dir,int length,bool terrainOnly=false) {
        const auto& test=terrainOnly?c.terrainFree:c.free;
        const int sx=sign(dir.x),sz=sign(dir.z);
        for(int n=1;n<=length;++n) {
            const Cell at{c.position.x+sx*n,c.position.z+sz*n};
            if(!localQuery(c,test,at)||(sx&&sz&&(!localQuery(c,test,{at.x-sx,at.z})||!localQuery(c,test,{at.x,at.z-sz}))))return false;
        }
        return true;
    };
    if(!record.yieldTo&&c.obstructionFriendly&&c.obstruction&&!c.obstruction->idle) {
        const auto other=records_.find(c.obstruction->id);
        if(other!=records_.end()) {
            const auto& peer=other->second;
            const auto peerAim=c.obstruction->steeringTarget.value_or(Cell{std::get<1>(peer.group),std::get<2>(peer.group)});
            const int px=sign(peerAim.x-c.obstruction->position.x),pz=sign(peerAim.z-c.obstruction->position.z);
            const Identity peerIdentity{peer.controller,{std::get<1>(peer.group),std::get<2>(peer.group)},peer.missionKind,peer.targetId};
            if(dx*px+dz*pz<0&&c.obstruction->identity&&*c.obstruction->identity==peerIdentity) {
                // The peer has committed to retreat. Keep following with
                // normal collision checks so it can actually let us pass;
                // braking the winner can leave both units waiting forever.
                if(peer.yieldTo==c.id) {return out;}
                if(!peer.yieldTo&&c.terrainFree&&!clear({-dz,dx},step,true)&&!clear({dz,-dx},step,true)) {
                    if(localDeferred())return defer();
                    const uint32_t age=c.tick-record.blockedSince;
                    const uint32_t peerAge=peer.blockedSince?c.tick-peer.blockedSince:0;
                    // Priority is not an agreement from the other unit: its
                    // side of this corner may have a different escape shape.
                    // Until it yields, continue ordinary avoidance below.
                    if(age<peerAge||(age==peerAge&&c.id>other->first)) {
                        record.yieldTo=other->first;record.yieldIdentity=peerIdentity;record.yieldGroup=peer.group;
                        record.yieldDirection={dx,dz};record.yieldOrigin=c.position;record.yieldUntil=c.tick+4096;
                    }
                }
            }
        }
    }
    if(localDeferred())return defer();
    if(record.yieldTo) {
        const auto forward=record.yieldDirection;
        const int lateral=(c.position.x-record.yieldOrigin.x)*-forward.z+(c.position.z-record.yieldOrigin.z)*forward.x;
        // A nominal side offset is insufficient around a bend. Park only
        // once our footprint clears the winner's live next anchor.
        if(std::abs(lateral)>=step) {
            const auto peer=c.lookup?c.lookup(record.yieldTo):std::optional<Neighbor>{};
            bool blocking=false;
            if(peer&&peer->steeringTarget) {
                const auto end=*peer->steeringTarget;
                const int px=peer->position.x+sign(end.x-peer->position.x)-peer->footX/2;
                const int pz=peer->position.z+sign(end.z-peer->position.z)-peer->footZ/2;
                const int x=c.position.x-c.footX/2,z=c.position.z-c.footZ/2;
                blocking=x<px+peer->footX&&px<x+c.footX&&z<pz+peer->footZ&&pz<z+c.footZ;
            }
            if(!blocking) {out.wait=true;return out;}
        }
        // Retain side-step preference, then move away from the live peer.
        // Diagonal backing also needs cardinal alternatives at L corners.
        const auto peer=c.lookup?c.lookup(record.yieldTo):std::optional<Neighbor>{};
        const Cell away=peer?Cell{sign(c.position.x-peer->position.x),sign(c.position.z-peer->position.z)}:Cell{-forward.x,-forward.z};
        const Cell choices[]={{-forward.z,forward.x},{forward.z,-forward.x},{away.x,away.z},{away.x,0},{0,away.z},{-forward.x,-forward.z},{-forward.x,0},{0,-forward.z}};
        // Full stride first; a single legal cell still permits gradual exit.
        for(int distance:{step,1})for(const auto dir:choices)if((dir.x||dir.z)&&clear(dir,distance)) {
            record.detour=Cell{c.position.x+dir.x*distance,c.position.z+dir.z*distance};
            record.continuation.reset();record.detourUntil=c.tick+180;out.detour=record.detour;return out;
        }
        if(localDeferred())return defer();
        out.wait=true;return out;
    }
    // A backward pocket is useful only as part of a route around the crowd.
    // After repeated loops, retain the complete bounded escape before asking
    // the terrain field again; a closed occupied pocket waits for room.
    // A proved arrival flank retains priority across these short route corners.
    if(!filledArrival&&!continuingBypass&&stalled&&c.blocked>=2&&c.obstructionFriendly&&c.obstruction&&c.obstruction->mobile) {
        const auto found=escape(c,record);
        if(found==EscapeResult::Route){out.repath=false;out.detour=record.detour;return out;}
        if(found!=EscapeResult::Unavailable) {
            out.wait=true;
            if(found==EscapeResult::Deferred)waiting_.insert(c.id);
            return out;
        }
    }
    if(filledArrival||continuingBypass) {
        // A wide stopped army may have no clear L-shaped route within one
        // local window. Retain its chosen side across bounded lateral legs,
        // then turn inward when an ingress leg opens. No new field is needed.
        // This is a bypass of the final arrival area, not of a maze waypoint.
        // Keep its basis independent of short alternating field-route corners.
        Cell forward=continuingBypass?record.bypassForward:Cell{sign(c.target.x-c.position.x),sign(c.target.z-c.position.z)};
        const int outerRadius=root(arrivalRadiusSquared(c))+128;
        const int64_t outerSquared=int64_t(outerRadius)*outerRadius;
        if(!continuingBypass) {
            const int ax=std::abs(c.target.x-c.position.x),az=std::abs(c.target.z-c.position.z);
            if(ax>2*az)forward.z=0;else if(az>2*ax)forward.x=0;
        }
        const auto begin=[&](Cell at,std::optional<Cell> continuation,Cell side) {
            record.bypassSide=side;record.bypassForward=forward;
            record.detour=at;record.continuation=continuation;
            record.longDetour=true;record.detourDistance=distance(c.position,at);
            record.detourUntil=c.tick+300;out.detour=at;
        };
        if(continuingBypass) {
            const int remaining=(c.target.x-c.position.x)*forward.x+(c.target.z-c.position.z)*forward.z;
            const int length=std::min(64,remaining/std::max(1,forward.x*forward.x+forward.z*forward.z));
            const Cell at{c.position.x+forward.x*length,c.position.z+forward.z*length};
            if(length>0&&distance(at,c.target)<=outerSquared&&clear(forward,length)) {
                begin(at,{},record.bypassSide);return out;
            }
        }
        std::optional<Cell> fallback;Cell fallbackSide;int fallbackLength=0;
        const std::array<Cell,2> sides{{{-forward.z,forward.x},{forward.z,-forward.x}}};
        for(int choice=0;choice<(continuingBypass?1:2);++choice) {
            const Cell side=continuingBypass?record.bypassSide:sides[size_t(choice)];
            int checked=0;bool lateral=true;
            // Retain the side even through a one-cell pocket. Falling back to
            // an uncommitted sidestep here recreates the original micro-loop.
            for(int length:std::array<int,7>{{1,step,step*2,step*4,24,48,64}}) {
                length=std::min(64,length);if(length<=checked)continue;
                for(int n=checked+1;n<=length;++n) {
                    const Cell at{c.position.x+side.x*n,c.position.z+side.z*n};
                    if(!localQuery(c,c.free,at)||(side.x&&side.z&&
                       (!localQuery(c,c.free,{at.x-side.x,at.z})||!localQuery(c,c.free,{at.x,at.z-side.z})))) {lateral=false;break;}
                }
                if(!lateral)break;
                checked=length;
                const Cell corner{c.position.x+side.x*length,c.position.z+side.z*length};
                if(distance(corner,c.target)>outerSquared)continue;
                if(length>fallbackLength){fallback=corner;fallbackSide=side;fallbackLength=length;}
                const int forwardLength=std::min(64,std::max(24,length));bool ingress=true;
                const Cell onward{corner.x+forward.x*forwardLength,corner.z+forward.z*forwardLength};
                if(distance(onward,c.target)>outerSquared)continue;
                for(int n=1;n<=forwardLength;++n) {
                    const Cell at{corner.x+forward.x*n,corner.z+forward.z*n};
                    if(!localQuery(c,c.free,at)||(forward.x&&forward.z&&
                       (!localQuery(c,c.free,{at.x-forward.x,at.z})||!localQuery(c,c.free,{at.x,at.z-forward.z})))) {ingress=false;break;}
                }
                if(ingress) {
                    begin(corner,onward,side);return out;
                }
            }
        }
        if(localDeferred())return defer();
        if(fallback) {begin(*fallback,{},fallbackSide);return out;}
        // A feature or neighboring footprint can interrupt the chosen flank.
        // Retain a complete short route around it before resuming the long
        // side, using the same shared footprint-work budget as local traffic.
        bool allBlocked=true;
        for(int choice=0;choice<(continuingBypass?1:2);++choice) {
            const Cell side=continuingBypass?record.bypassSide:sides[size_t(choice)];
            const auto localFree=[&](Cell at){return distance(at,c.target)<=outerSquared&&localQuery(c,c.free,at);};
            auto local=c;local.free=std::cref(localFree);
            local.steeringTarget=Cell{c.position.x+side.x*16,c.position.z+side.z*16};
            const auto found=escape(local,record);
            if(found==EscapeResult::Route) {
                record.bypassSide=side;record.bypassForward=forward;
                out.repath=false;out.detour=record.detour;return out;
            }
            if(found==EscapeResult::Deferred){waiting_.insert(c.id);out.wait=true;return out;}
            allBlocked&=found==EscapeResult::Blocked;
        }
        if(allBlocked){out.wait=true;return out;}
        if(localDeferred())return defer();
        record.bypassSide={};
    }
    if(c.obstruction&&c.obstruction->idle) {
        // Ordinary parked-body avoidance keeps its short two-leg search.
        for(const Cell side:std::array<Cell,2>{{{-dz,dx},{dz,-dx}}})
            for(int multiple=1;multiple<=4;++multiple) {
                if(multiple>1&&step*(multiple-1)>=16)break;
                const int length=std::min(16,step*multiple);
                if(!clear(side,length))break;
                const Cell corner{c.position.x+side.x*length,c.position.z+side.z*length};
                bool forward=true;
                const int forwardLength=std::min(16,step*3);
                for(int n=1;n<=forwardLength;++n) {
                    const Cell at{corner.x+dx*n,corner.z+dz*n};
                    if(!localQuery(c,c.free,at)||(dx&&dz&&
                       (!localQuery(c,c.free,{at.x-dx,at.z})||!localQuery(c,c.free,{at.x,at.z-dz})))) {forward=false;break;}
                }
                if(!forward)continue;
                record.detour=corner;record.continuation=Cell{corner.x+dx*forwardLength,corner.z+dz*forwardLength};
                record.detourUntil=c.tick+360;out.detour=corner;return out;
            }
    }
    // Prefer the right side of travel for both opposing streams. Search fixed
    // candidate directions, never random or wall-clock-based decisions.
    const Cell dirs[]={{dx-dz,dz+dx},{-dz,dx},{dx,dz},{dz,-dx},{dx+dz,dz-dx},{-dx-dz,-dz+dx},{-dx+dz,-dz-dx},{-dx,-dz}};
    // Dense armies can leave a legal one-cell pocket while every full stride
    // is occupied. Use it after the normal candidates, just as yielding does.
    // Admission and the fixed direction list still bound collision probes.
    for(int stride:{step,1})for(const auto dir:dirs) {
        const Cell aim{c.position.x+sign(dir.x)*stride,c.position.z+sign(dir.z)*stride};
        if(!localQuery(c,c.free,aim))continue;
        const int sx=sign(dir.x),sz=sign(dir.z);
        if(!clear(dir,stride))continue;
        std::optional<Cell> continuation;
        if(sx*dx+sz*dz==0) {
            bool forward=true;
            for(int n=1;n<=step*2;++n) {
                const Cell at{aim.x+dx*n,aim.z+dz*n};
                if(!localQuery(c,c.free,at)||(dx&&dz&&
                   (!localQuery(c,c.free,{at.x-dx,at.z})||!localQuery(c,c.free,{at.x,at.z-dz})))) {forward=false;break;}
            }
            if(forward)continuation=Cell{aim.x+dx*step*2,aim.z+dz*step*2};
        }
        if(localDeferred())return defer();
        record.detour=aim;record.continuation=continuation;
        record.detourUntil=c.tick+180;out.detour=aim;return out;
    }
    // No alternative was free. Retry the installed route so current body
    // movement can clear a previous refusal without another admission.
    if(localDeferred())return defer();
    return out;
}
void Traffic::clearEscape(Record& record) {record.escaping=false;record.escapeCount=0;record.escapeSteps={};}
void Traffic::advanceEscape(Record& record) {
    if(!record.detour||!record.escapeCount)return;
    const unsigned direction=unsigned(record.escapeSteps[0]&7);
    do {
        record.detour->x+=escapeDirections[direction].x;
        record.detour->z+=escapeDirections[direction].z;
        record.escapeSteps[0]=(record.escapeSteps[0]>>3)|((record.escapeSteps[1]&7)<<60);
        record.escapeSteps[1]>>=3;--record.escapeCount;
    } while(record.escapeCount&&(record.escapeSteps[0]&7)==direction);
}
Traffic::EscapeResult Traffic::escape(const Context& c,Record& record) {
    constexpr int radius=16,width=2*radius+1,cells=width*width;
    const size_t weight=size_t(std::max(1,c.footX))*size_t(std::max(1,c.footZ));
    // Whole-footprint queries share a fixed cell-work allowance. Large
    // footprints and searches outside this window retain ordinary avoidance.
    const size_t queryLimit=std::min<size_t>(512,escapeBudget_.limit/weight);
    if(queryLimit<32)return EscapeResult::Unavailable;
    if(!escapeBudget_.accepts(c.id))return EscapeResult::Deferred;
    std::array<uint8_t,cells> known{};
    std::array<int16_t,cells> parents;parents.fill(-1);
    std::array<uint16_t,cells> queue{};
    size_t queries=0,head=0,tail=0;bool deferred=false,limited=false,boundary=false;
    const auto index=[&](Cell at) {return (at.z-c.position.z+radius)*width+at.x-c.position.x+radius;};
    const auto point=[&](int i){return Cell{c.position.x+i%width-radius,c.position.z+i/width-radius};};
    const auto free=[&](Cell at) {
        if(std::abs(at.x-c.position.x)>radius||std::abs(at.z-c.position.z)>radius)return false;
        auto& cached=known[size_t(index(at))];
        if(cached)return cached==2;
        if(queries==queryLimit){limited=true;return false;}
        if(!escapeBudget_.spend(c.id,weight)){deferred=true;return false;}
        ++queries;cached=localQuery(c,c.free,at)?2:1;
        if(c.localDeferred&&*c.localDeferred){deferred=true;return false;}
        return cached==2;
    };
    const Cell aim=c.steeringTarget.value_or(c.target);
    const int dx=sign(aim.x-c.position.x),dz=sign(aim.z-c.position.z);
    if(!dx&&!dz)return EscapeResult::Blocked;
    const int norm=dx*dx+dz*dz;
    const int total=(aim.x-c.position.x)*dx+(aim.z-c.position.z)*dz;
    const int stride=std::clamp(std::max(c.footX,c.footZ)+1,2,4);
    const int required=std::min(total,stride*2*norm);
    const auto exit=[&](Cell at) {
        const int progress=(at.x-c.position.x)*dx+(at.z-c.position.z)*dz;
        if(progress<required||progress>total)return false;
        if(progress==total) {
            // A free anchor beside a mobile-occupied corner can reanchor the
            // terrain route. Stay on its entry plane and prove its static
            // connection; never walk beyond an unseen terrain turn.
            if(!c.terrainFree)return at==aim;
            return directRoute(at,aim,radius,[&](Cell cell) {
                if(queries==queryLimit){limited=true;return false;}
                ++queries;
                if(!escapeBudget_.spend(c.id,weight)){deferred=true;return false;}
                const bool clear=localQuery(c,c.terrainFree,cell);
                if(c.localDeferred&&*c.localDeferred){deferred=true;return false;}
                return clear;
            },[&](Cell cell){return cell==aim;});
        }
        const int length=std::min(stride*2,(total-progress)/norm);
        return length&&directRoute(at,Cell{at.x+dx*length,at.z+dz*length},radius,free,
            [&](Cell cell){return cell==Cell{at.x+dx*length,at.z+dz*length};});
    };
    const int origin=index(c.position);parents[size_t(origin)]=int16_t(origin);queue[tail++]=uint16_t(origin);
    int found=-1;
    while(head<tail&&head<queryLimit&&!deferred&&!limited) {
        const int current=queue[head++];const Cell at=point(current);
        boundary|=std::max(std::abs(at.x-c.position.x),std::abs(at.z-c.position.z))==radius;
        if(current!=origin&&exit(at)){found=current;break;}
        for(const Cell direction:escapeDirections) {
            const Cell next{at.x+direction.x,at.z+direction.z};
            if(std::abs(next.x-c.position.x)>radius||std::abs(next.z-c.position.z)>radius)continue;
            const int n=index(next);
            if(parents[size_t(n)]>=0||!free(next))continue;
            if(direction.x&&direction.z&&(!free({next.x,at.z})||!free({at.x,next.z})))continue;
            parents[size_t(n)]=int16_t(current);queue[tail++]=uint16_t(n);
            if(deferred||limited)break;
        }
    }
    if(found<0) {
        if(deferred)return EscapeResult::Deferred;
        return limited||head==queryLimit||boundary?EscapeResult::Unavailable:EscapeResult::Blocked;
    }
    // Publish only a complete escape. Two words hold 21 three-bit directions
    // each; the follower exposes their direction changes as steering corners.
    std::array<uint8_t,42> reversed{};size_t count=0;
    for(int at=found;at!=origin;at=parents[size_t(at)]) {
        if(count==reversed.size())return EscapeResult::Unavailable;
        const Cell a=point(parents[size_t(at)]),b=point(at);
        const Cell delta{b.x-a.x,b.z-a.z};
        for(unsigned direction=0;direction<escapeDirections.size();++direction)
            if(delta==escapeDirections[direction]){reversed[count++]=uint8_t(direction);break;}
    }
    record.escapeSteps={};record.escapeCount=uint8_t(count);record.escaping=true;
    for(size_t i=0;i<count;++i)record.escapeSteps[i/21]|=uint64_t(reversed[count-i-1])<<(3*(i%21));
    record.detour=c.position;record.continuation.reset();record.longDetour=false;record.bypassSide={};
    advanceEscape(record);record.detourUntil=c.tick+300;
    record.detourDistance=distance(c.position,*record.detour);
    return EscapeResult::Route;
}
void Traffic::prune(size_t budget,const std::function<bool(int,int,uint64_t,Cell,bool)>& valid) {
    budget=std::min(budget,records_.size());
    auto it=records_.upper_bound(pruneCursor_);
    for(size_t n=0;n<budget&&!records_.empty();++n) {
        if(it==records_.end())it=records_.begin();
        auto at=it++;pruneCursor_=at->first;
        const auto& r=at->second;
        if(!valid(at->first,std::get<0>(r.group),r.controller,r.position,r.settled))erase(at);
    }
}
uint64_t Traffic::checksum() const {
    uint64_t h=1469598103934665603ull;
    const auto mix=[&](uint64_t n){for(int i=0;i<8;++i){h^=uint8_t(n);h*=1099511628211ull;n>>=8;}};
    mix(pruneCursor_);mix(workCursor_);mix(workTick_);
    mix(escapeBudget_.limit);mix(escapeBudget_.remaining);mix(escapeBudget_.after);mix(escapeBudget_.last);mix(escapeBudget_.exhausted);
    for(int id:waiting_)mix(uint64_t(id)*2);
    for(int id:admitted_)mix(uint64_t(id)*2+1);
    for(const auto& [id,r]:records_) {mix(id);mix(std::get<0>(r.group));mix(std::get<1>(r.group));mix(std::get<2>(r.group));mix(std::get<3>(r.group));mix(r.controller);mix(r.missionKind);mix(r.targetId);mix(r.seen);mix(r.detourUntil);mix(r.position.x);mix(r.position.z);mix(r.plain);mix(r.settled);mix(r.blockedSince);mix(r.yieldTo);mix(r.yieldIdentity.controller);mix(r.yieldIdentity.target.x);mix(r.yieldIdentity.target.z);mix(r.yieldIdentity.kind);mix(r.yieldIdentity.targetId);mix(r.yieldUntil);
        mix(r.footX);mix(r.footZ);mix(r.longDetour);if(r.longDetour)mix(r.detourDistance);
        mix(r.bypassSide.x);mix(r.bypassSide.z);if(r.bypassSide.x||r.bypassSide.z){mix(r.bypassForward.x);mix(r.bypassForward.z);}
        mix(bool(r.arrivalSlot));if(r.arrivalSlot){mix(r.arrivalSlot->x);mix(r.arrivalSlot->z);mix(r.slotUntil);mix(r.slotDistance);}
        if(r.plain){mix(r.issuedTick);mix(r.progressTick);mix(uint64_t(r.bestDistance));}
        mix(std::get<0>(r.yieldGroup));mix(std::get<1>(r.yieldGroup));mix(std::get<2>(r.yieldGroup));mix(std::get<3>(r.yieldGroup));
        if(r.escaping){mix(0x455343415045ull);mix(r.escapeSteps[0]);mix(r.escapeSteps[1]);mix(r.escapeCount);mix(r.detourDistance);}
        mix(r.yieldDirection.x);mix(r.yieldDirection.z);mix(r.yieldOrigin.x);mix(r.yieldOrigin.z);mix(bool(r.detour));if(r.detour){mix(r.detour->x);mix(r.detour->z);}mix(bool(r.continuation));if(r.continuation){mix(r.continuation->x);mix(r.continuation->z);}}
    return h;
}
size_t Traffic::bytes() const {return index_.bytes()+records_.size()*(sizeof(Record)+64)+groups_.size()*(sizeof(Group)+sizeof(Population)+48)+(waiting_.size()+admitted_.size())*48+reservations_.size()*112+reservationLinks_*48;}
}
