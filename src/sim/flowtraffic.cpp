#include "flowtraffic.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
namespace tak::sim::flow {
namespace {
int64_t distance(Cell a,Cell b) {const int64_t x=int64_t(a.x)-b.x,z=int64_t(a.z)-b.z;return x*x+z*z;}
int sign(int n) {return (n>0)-(n<0);}
}
void Traffic::erase(std::map<int,Record>::iterator it) {
    if(it->second.plain) {auto group=groups_.find(it->second.group);if(group!=groups_.end()&&!--group->second)groups_.erase(group);}
    waiting_.erase(it->first);admitted_.erase(it->first);records_.erase(it);
}
Traffic::Result Traffic::update(const Context& c) {
    Result out;
    if(c.tick!=workTick_) {
        workTick_=c.tick;admitted_.clear();
        auto job=waiting_.upper_bound(workCursor_);
        const size_t count=std::min<size_t>(256,waiting_.size());
        for(size_t n=0;n<count;++n) {
            if(job==waiting_.end())job=waiting_.begin();
            workCursor_=*job;admitted_.insert(*job);job=waiting_.erase(job);
        }
    }
    const Group group{c.player,c.target.x,c.target.z};
    auto it=records_.find(c.id);
    if(it!=records_.end()&&(it->second.controller!=c.controller||it->second.group!=group||it->second.plain!=c.plainMove||
       it->second.missionKind!=c.missionKind||it->second.targetId!=c.targetId)) {
        erase(it);it=records_.end();
    }
    if(it==records_.end()) {
        // Unit caps already bound normal matches. Retain deterministic behavior
        // in synthetic/oversubscribed callers without growing the cache forever.
        if(records_.size()==16384) {
            auto victim=records_.upper_bound(pruneCursor_);
            if(victim==records_.end())victim=records_.begin();
            pruneCursor_=victim->first;erase(victim);
        }
        Record r;r.group=group;r.controller=c.controller;r.plain=c.plainMove;r.missionKind=c.missionKind;r.targetId=c.targetId;
        it=records_.emplace(c.id,r).first;
        if(c.plainMove)++groups_[group];
    }
    auto& record=it->second;record.seen=c.tick;record.position=c.position;
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
            record.yieldTo=0;record.detour.reset();record.continuation.reset();out.repath=!record.yieldTo;
        }
    }
    if(c.plainMove&&c.goalReached) {record.settled=true;record.yieldTo=0;record.detour.reset();record.continuation.reset();waiting_.erase(c.id);admitted_.erase(c.id);out.settled=true;return out;}
    if(c.plainMove&&c.blocked>=2) {
        const int spacing=std::max(c.footX,c.footZ)+1;
        // Only grow the arrival area enough to hold this actual destination
        // group. Contact propagation cannot settle a long convoy far away.
        const int64_t members=int64_t(groups_.at(group));
        const int64_t radius2=std::max<int64_t>(64*spacing*spacing,(members+std::min<int64_t>(members,16))*spacing*spacing);
        const int64_t distance2=distance(c.position,c.target);
        const int64_t queueRadius=std::min<int64_t>(64,members*spacing);
        if(distance2<=std::max(radius2,queueRadius*queueRadius))for(const auto& n:c.neighbors) {
            if(!n.id||n.id==c.id||!n.idle||n.player!=c.player)continue;
            const auto anchor=records_.find(n.id);
            if(anchor==records_.end()||!anchor->second.settled||anchor->second.group!=group||
               anchor->second.position!=n.position)continue;
            const int dx=std::abs(c.position.x-n.position.x),dz=std::abs(c.position.z-n.position.z);
            if(dx<=(c.footX+n.footX+1)/2+1&&dz<=(c.footZ+n.footZ+1)/2+1) {
                if(distance2>radius2&&distance(n.position,c.target)>radius2) {
                    // A terrain bottleneck can force an arrival formation into
                    // a queue. Extend only actual same-goal contact, bounded by
                    // its population and a 64-cell local window. Open-ground
                    // formations may finish one touching footprint beyond the
                    // area boundary, anchored by a body inside it. Requiring
                    // every centre inside leaves that last row circling the
                    // stopped formation; outside anchors cannot propagate it.
                    const auto constrained=[&](Cell at) {
                        if(!c.terrainFree)return false;
                        for(const Cell direction:std::array<Cell,4>{{{1,0},{-1,0},{0,1},{0,-1}}})
                            for(int step=1;step<=spacing;++step)
                                if(!c.terrainFree({at.x+direction.x*step,at.z+direction.z*step}))return true;
                        return false;
                    };
                    if(!constrained(c.position)&&!constrained(n.position))continue;
                }
                record.settled=true;record.yieldTo=0;record.detour.reset();record.continuation.reset();waiting_.erase(c.id);admitted_.erase(c.id);out.settled=true;return out;
            }
        }
    }
    if(record.detour) {
        if(distance(c.position,*record.detour)==0) {
            record.detour=record.continuation;record.continuation.reset();
            if(record.detour&&c.free(*record.detour)) {
                record.detourUntil=c.tick+360;out.detour=record.detour;return out;
            }
            record.detour.reset();out.repath=!record.yieldTo;
        } else if(c.tick>=record.detourUntil||(c.blocked>=2&&!c.free(*record.detour))) {
            record.detour.reset();record.continuation.reset();out.repath=!record.yieldTo;
        } else {out.detour=record.detour;return out;}
    }
    // Local detours resolve transient mobile occupancy. Static clipping belongs
    // to the installed terrain route; steering away from it can cause orbits.
    if((c.blocked<2||!c.obstruction||!c.obstruction->mobile)&&!record.yieldTo) {waiting_.erase(c.id);return out;}
    waiting_.insert(c.id);
    // Admission bounds detour search, not normal collision-checked following.
    // Braking here latches dense convoys long after the blocker has moved.
    if(!admitted_.erase(c.id)) {out.wait=bool(record.yieldTo);return out;}
    waiting_.erase(c.id);
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
            if(!test(at)||(sx&&sz&&(!test({at.x-sx,at.z})||!test({at.x,at.z-sz}))))return false;
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
        out.wait=true;return out;
    }
    if(c.obstruction&&c.obstruction->idle) {
        // A short sidestep followed immediately by returning to an occupied
        // corner makes the mover orbit a parked formation. Commit to a clear
        // lateral leg and a forward continuation before rejoining the field.
        // Check both legs against whole-footprint terrain/body collision; maze
        // turns and narrow passages fall through to the short escape below.
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
                    if(!c.free(at)||(dx&&dz&&
                       (!c.free({at.x-dx,at.z})||!c.free({at.x,at.z-dz})))) {forward=false;break;}
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
        if(!c.free(aim))continue;
        const int sx=sign(dir.x),sz=sign(dir.z);
        if(!clear(dir,stride))continue;
        record.detour=aim;record.continuation.reset();
        if(sx*dx+sz*dz==0) {
            bool forward=true;
            for(int n=1;n<=step*2;++n)if(!c.free({aim.x+dx*n,aim.z+dz*n})) {forward=false;break;}
            if(forward)record.continuation=Cell{aim.x+dx*step*2,aim.z+dz*step*2};
        }
        record.detourUntil=c.tick+180;out.detour=aim;return out;
    }
    // No alternative was free. Retry the installed route so current body
    // movement can clear a previous refusal without another admission.
    return out;
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
    for(int id:waiting_)mix(uint64_t(id)*2);
    for(int id:admitted_)mix(uint64_t(id)*2+1);
    for(const auto& [id,r]:records_) {mix(id);mix(std::get<0>(r.group));mix(std::get<1>(r.group));mix(std::get<2>(r.group));mix(r.controller);mix(r.missionKind);mix(r.targetId);mix(r.seen);mix(r.detourUntil);mix(r.position.x);mix(r.position.z);mix(r.plain);mix(r.settled);mix(r.blockedSince);mix(r.yieldTo);mix(r.yieldIdentity.controller);mix(r.yieldIdentity.target.x);mix(r.yieldIdentity.target.z);mix(r.yieldIdentity.kind);mix(r.yieldIdentity.targetId);mix(r.yieldUntil);
        mix(std::get<0>(r.yieldGroup));mix(std::get<1>(r.yieldGroup));mix(std::get<2>(r.yieldGroup));
        mix(r.yieldDirection.x);mix(r.yieldDirection.z);mix(r.yieldOrigin.x);mix(r.yieldOrigin.z);mix(bool(r.detour));if(r.detour){mix(r.detour->x);mix(r.detour->z);}mix(bool(r.continuation));if(r.continuation){mix(r.continuation->x);mix(r.continuation->z);}}
    return h;
}
size_t Traffic::bytes() const {return records_.size()*(sizeof(Record)+64)+groups_.size()*(sizeof(Group)+sizeof(size_t)+48)+(waiting_.size()+admitted_.size())*48;}
}
