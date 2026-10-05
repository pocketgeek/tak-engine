#include "retailplusnavigator.h"
#include "sim.h"
#include "footprint.h"
#include <chrono>

namespace tak::sim {
namespace {
using ProfileClock=std::chrono::steady_clock;
uint64_t profileElapsed(ProfileClock::time_point begin) {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(ProfileClock::now()-begin).count());
}
}
struct RetailPlusNavigator::Impl {
    using Cell=flow::Cell;
    World& world;
    retailplus::Traffic policy;
    flow::ProofBudget proof;
    flow::ProofBudget bodies{32768};
    uint64_t proofCells=0,bodyEntries=0,bodyDeferrals=0;
    uint64_t contextNanoseconds=0,setupNanoseconds=0,policyNanoseconds=0,maintenanceNanoseconds=0;
    uint64_t fastUpdates=0,fullUpdates=0;
    explicit Impl(World& w):world(w) {}
    bool supports(const Unit& u) const {
        if(!u.alive()||u.embarked()||u.underConstruction||!u.type||u.orders.empty())return false;
        if(!retailplus::Traffic::supports(*u.type,u.orders[World::currentLeg(u.orders)]))return false;
        const auto& front=u.orders.front();
        return !front.targetId&&!front.load&&!front.unload&&!front.buildType&&!front.reclaimFeat&&
            !front.reclaimArea&&!front.manaBuildArea&&!front.repairTarget&&!front.wait&&!front.waitAttack&&
            !front.attackMove&&!front.patrol&&!front.guard&&!front.autoTarget&&!front.landing&&!front.park&&
            !front.buildRectangle&&!front.flightGoal&&!front.transportPickup&&!front.transportUnloadApproach&&
            !front.transportUnloadReleasePending&&!front.transportUnloadTransferDeferred&&!front.transportPassenger;
    }
    flow::Traffic::Context context(const Unit& u) const {
        flow::Traffic::Context c;
        // All callers have already checked supports, including neighbor lookup.
        const auto& goal=u.orders[World::currentLeg(u.orders)];
        const auto target=goal.missionTarget.value_or(std::pair{goal.x,goal.z});
        c.id=u.id;c.player=u.player;c.controller=goal.controller;
        c.tick=world.tickCounter_;c.issuedTick=goal.issuedTick;
        c.position={footprintCell(u.x,u.type->footX),footprintCell(u.z,u.type->footZ)};
        c.target={target.first.floorInt()/16,target.second.floorInt()/16};
        c.footX=u.type->footX;c.footZ=u.type->footZ;c.blocked=u.bodyBlockStreak;
        c.steeringTarget=Cell{footprintCell(u.orders.front().x,c.footX),footprintCell(u.orders.front().z,c.footZ)};
        c.plainMove=true;c.missionKind=1;
        c.goalReached=world.groundMissionAccepts(u,goal);
        c.cooperativeOpenTerrain=false;
        return c;
    }
};

RetailPlusNavigator::RetailPlusNavigator(World& world):impl_(std::make_unique<Impl>(world)) {}
RetailPlusNavigator::~RetailPlusNavigator()=default;
bool RetailPlusNavigator::supports(const Unit& u) const {return impl_->supports(u);}
void RetailPlusNavigator::registerMove(Unit& u) {
    if(supports(u))impl_->policy.registerMove(impl_->context(u));
    else impl_->policy.cancel(u.id);
}
void RetailPlusNavigator::cancel(int id) {impl_->policy.cancel(id);}
void RetailPlusNavigator::tick() {
    auto& p=*impl_;auto& w=p.world;
    const bool profile=w.paths_.profiling();
    const auto start=profile?ProfileClock::now():ProfileClock::time_point{};
    p.proof.tick();p.bodies.tick();
    for(int player=0;player<w.numPlayers();++player) {
        uint16_t mask=0;
        for(int other=0;other<w.numPlayers();++other)if(w.allied(player,other))mask|=uint16_t(1u<<other);
        p.policy.setAllianceMask(player,mask);
    }
    p.policy.prune(64,[&](int id,int owner,uint64_t controller,flow::Cell at,bool settled) {
        const auto* u=w.unit(id);
        if(!u||!u->alive()||u->embarked()||!u->type||u->player!=owner)return false;
        const flow::Cell current{footprintCell(u->x,u->type->footX),footprintCell(u->z,u->type->footZ)};
        if(u->orders.empty())return settled&&current==at;
        if(!p.supports(*u))return false;
        const auto& goal=u->orders[World::currentLeg(u->orders)];
        return goal.controller==controller||(settled&&!goal.controller&&current==at&&(goal.mission.pending&0x500));
    });
    if(profile)p.maintenanceNanoseconds+=profileElapsed(start);
}

flow::Traffic::Result RetailPlusNavigator::traffic(Unit& u) {return traffic(u,supports(u));}
flow::Traffic::Result RetailPlusNavigator::traffic(Unit& u,bool supported) {
    auto& p=*impl_;auto& w=p.world;
    const bool profile=w.paths_.profiling();
    const auto start=profile?ProfileClock::now():ProfileClock::time_point{};
    if(!supported) {p.policy.cancel(u.id);return {};}
    auto c=p.context(u);
    const auto& goal=u.orders[World::currentLeg(u.orders)];
    if(!goal.controller&&(goal.mission.pending&0x500))p.policy.refreshSettled(u.id,c.position);
    // Preserve both coordinators' progress, identities, groups and work cursors
    // before skipping the callback-rich path. Arrival and retained claims always
    // take the full path; no openness or collision clearance is assumed here.
    // Here c has no neighbors, obstruction or proof callbacks yet, so those
    // guards in updateUnblocked are vacuous. Safety rests on two real ones:
    // blocked>=2 declines (the full path's obstruction query), and nearArrival
    // declines (needsArrivalNeighbors requires it, and update() only supplies
    // arrival callbacks to arrivals_ when goalReached or nearArrival holds).
    // Away from both, the full path never consults what is skipped here.
    const bool unblocked=p.policy.updateUnblocked(c);
    if(profile)p.contextNanoseconds+=profileElapsed(start);
    // Unhashed observation counts, kept whether or not clocks are enabled.
    if(unblocked){++p.fastUpdates;return {};}
    ++p.fullUpdates;
    const auto setupStart=profile?ProfileClock::now():ProfileClock::time_point{};
    // Include early density/deferred exits in setup timing as well.
    struct SetupProfile {
        bool enabled;ProfileClock::time_point start;uint64_t& elapsed;
        ~SetupProfile(){if(enabled)elapsed+=profileElapsed(start);}
    } setupProfile{profile,setupStart,p.setupNanoseconds};
    bool localDeferred=false,arrivalDeferred=false,queryTooDense=false;
    c.localDeferred=&localDeferred;c.arrivalDeferred=&arrivalDeferred;
    const auto defer=[&] {
        if(!localDeferred)++p.bodyDeferrals;
        localDeferred=arrivalDeferred=true;return false;
    };
    const auto bodyQuery=[&](int x,int z,int width,int height) {
        if(localDeferred)return false;
        if(width<=0||height<=0||x<0||z<0||x+width>w.hW_||z+height>w.hH_)return false;
        // Never fall back to the all-unit scan. The existing index is shared
        // with physical movement and rebuilt at most once on first use.
        if(!w.bodyIndexEnabled_){queryTooDense=true;return defer();}
        if(!p.bodies.accepts(u.id))return defer();
        if(!w.bodyIndexValid_)w.rebuildBodyIndex();
        size_t entries=0;
        for(int tz=z/8;tz<(z+height+7)/8;++tz)for(int tx=x/8;tx<(x+width+7)/8;++tx) {
            // Charge this preflight and the subsequent query's bucket visit.
            if(!p.bodies.spend(u.id,2))return defer();
            p.bodyEntries+=2;
            entries+=w.bodyTiles_[size_t(tz)*w.bodyTilesW_+tx].size();
            if(entries>256){queryTooDense=true;return defer();}
        }
        if(!p.bodies.spend(u.id,entries))return defer();
        p.bodyEntries+=entries;return true;
    };
    const auto wait=[] {
        flow::Traffic::Result result;result.wait=result.arrivalApproach=true;
        return result; // Preserve mission/slot ownership while evidence waits.
    };
    const auto unavailable=[&] {
        // Raw spatial buckets include airborne units. An overfull bucket may
        // stay overfull forever, even with a completely clear ground route.
        // Drop optional local claims and retain native movement/collision.
        if(queryTooDense) {
            p.policy.cancel(u.id);flow::Traffic::Result result;
            // Native fallback still needs a legal standing body before goal
            // retirement. Use its ordinary authoritative placement query once
            // at geometric arrival, including when optional indexing is off.
            if(c.goalReached) {
                const int x=footprintOrigin(u.x,c.footX),z=footprintOrigin(u.z,c.footZ);
                const bool clear=!w.mapPlacementCells_.empty()?w.mobilePlacement(u,x,z,false):
                    w.navFor(u.type).fits(c.position.x,c.position.z,std::max(c.footX,c.footZ))&&
                    w.cellFree(u.x,u.z,u.id,std::max(c.footX,c.footZ));
                result.arrivalApproach=!clear;
            }
            return result;
        }
        return wait();
    };
    const auto lookup=[&](int id)->std::optional<flow::Traffic::Neighbor> {
        const auto* other=w.unit(id);
        if(!other||!other->type||!other->alive()||other->embarked()||
            (other->type->canFly&&other->flightGroundMode!=1))return {};
        flow::Traffic::Neighbor n{id,other->player,
            {footprintCell(other->x,other->type->footX),footprintCell(other->z,other->type->footZ)},
            other->type->footX,other->type->footZ,other->orders.empty()};
        n.mobile=!other->type->isStructure();
        if(p.supports(*other)) {
            const auto context=p.context(*other);
            n.identity=flow::Traffic::Identity{context.controller,context.target,context.missionKind,0};
            n.steeringTarget=context.steeringTarget;
            if(n.steeringTarget==n.position)for(size_t i=1;i<=std::min<size_t>(World::currentLeg(other->orders),63);++i) {
                const flow::Cell at{footprintCell(other->orders[i].x,n.footX),footprintCell(other->orders[i].z,n.footZ)};
                if(at!=n.position){n.steeringTarget=at;break;}
            }
        }
        return n;
    };
    c.lookup=std::cref(lookup);
    const bool arrivalNeighbors=p.policy.needsArrivalNeighbors(c);
    if(c.blocked>=2||arrivalNeighbors) {
        // Full body-index query includes landed flyers and structure yards.
        const int dx=c.steeringTarget->x-c.position.x,dz=c.steeringTarget->z-c.position.z;
        const int sx=(dx>0)-(dx<0),sz=(dz>0)-(dz<0);
        const int reach=arrivalNeighbors?std::min(std::clamp(std::max(c.footX,c.footZ)+1,2,16),std::max(std::abs(dx),std::abs(dz))):1;
        for(int step=1;step<=reach&&!c.obstruction;++step) {
            const flow::Cell ahead{c.position.x+sx*step,c.position.z+sz*step};
            const std::array<flow::Cell,3> cells{{ahead,{ahead.x,ahead.z-sz},{ahead.x-sx,ahead.z}}};
            for(int side=0;side<(sx&&sz?3:1)&&!c.obstruction;++side) {
                const int x0=cells[size_t(side)].x-c.footX/2,z0=cells[size_t(side)].z-c.footZ/2;
                if(x0<0||z0<0||x0+c.footX>w.hW_||z0+c.footZ>w.hH_)continue;
                // The occupancy grid already names the ground mover standing
                // there. Reading it costs no index work; only an empty grid
                // footprint still needs the full query for landed flyers,
                // structure yards and other bodies the grid does not record.
                if(w.occW_==w.hW_&&w.occH_==w.hH_) {
                    int owner=0;
                    for(int z=z0;z<z0+c.footZ;++z)for(int x=x0;x<x0+c.footX;++x) {
                        const int id=w.occ_[size_t(z)*w.occW_+x];
                        if(id&&id!=u.id&&(!owner||id<owner))owner=id;
                    }
                    // Accept the named mover only while its current footprint
                    // still covers the queried rectangle; otherwise fall
                    // through to the authoritative query below.
                    if(owner)if(auto n=lookup(owner)) {
                        const int nx=n->position.x-n->footX/2,nz=n->position.z-n->footZ/2;
                        if(nx<x0+c.footX&&x0<nx+n->footX&&nz<z0+c.footZ&&z0<nz+n->footZ){c.obstruction=n;continue;}
                    }
                }
                if(!bodyQuery(x0,z0,c.footX,c.footZ))return unavailable();
                const auto bodies=w.searchBodyRect(x0,z0,c.footX,c.footZ);
                int id=0;
                for(const auto* body:bodies.cells)if(body&&body->id!=u.id&&(!id||body->id<id))id=body->id;
                if(id)c.obstruction=lookup(id);
            }
        }
        c.obstructionFriendly=c.obstruction&&w.allied(u.player,c.obstruction->player);
    }
    std::array<flow::Traffic::Neighbor,64> neighbors;size_t count=0;
    if(arrivalNeighbors) {
        // Complete neighboring footprints from the existing spatial index;
        // bounded radius and output, no all-unit vector per mover.
        const int x0=std::max(0,c.position.x-c.footX/2-2),z0=std::max(0,c.position.z-c.footZ/2-2);
        const int x1=std::min(w.hW_,c.position.x-c.footX/2+c.footX+2),z1=std::min(w.hH_,c.position.z-c.footZ/2+c.footZ+2);
        if(x1<=x0||z1<=z0||!bodyQuery(x0,z0,x1-x0,z1-z0))return unavailable();
        const auto bodies=w.searchBodyRect(x0,z0,std::max(0,x1-x0),std::max(0,z1-z0));
        for(const auto* body:bodies.cells) {
            if(!body||body->id==u.id||!body->orders.empty()||count==neighbors.size())continue;
            bool duplicate=false;for(size_t i=0;i<count;++i)if(neighbors[i].id==body->id){duplicate=true;break;}
            if(!duplicate)if(auto n=lookup(body->id))neighbors[count++]=*n;
        }
    }
    c.neighbors=std::span(neighbors.data(),count);
    const auto place=[&](flow::Cell at,bool bodies) {
        if(localDeferred)return false;
        if(!w.mapPlacementCells_.empty()) {
            const int x=at.x-c.footX/2,z=at.z-c.footZ/2;
            if(x<0||z<0||x+c.footX>=w.hW_||z+c.footZ>=w.hH_)return false;
            if(!bodyQuery(x,z,c.footX,c.footZ))return false;
            return w.mobilePlacement(u,x,z,!bodies);
        }
        // Legacy terrain fixtures have no loaded feature plane; keep their
        // existing conservative square-footprint placement rule.
        const int foot=std::max(c.footX,c.footZ);
        if(foot>15)return false;
        const auto& nav=w.navFor(u.type);
        return nav.fits(at.x,at.z,foot)&&(!bodies||w.cellFree(footprintWaypoint(at.x,c.footX),footprintWaypoint(at.z,c.footZ),u.id,foot));
    };
    const auto free=[&](flow::Cell at){return place(at,true);};
    const auto terrainFree=[&](flow::Cell at){return place(at,false);};
    c.free=std::cref(free);c.terrainFree=std::cref(terrainFree);
    const auto knownFree=[&](flow::Cell at) {
        const size_t weight=size_t(c.footX)*c.footZ;
        if(!p.proof.spend(u.id,weight)){arrivalDeferred=true;return false;}
        p.proofCells+=weight;
        const int x0=at.x-c.footX/2,z0=at.z-c.footZ/2;
        if(x0<0||z0<0||x0+c.footX>=w.hW_||z0+c.footZ>=w.hH_)return false;
        if(u.player>=0&&u.player<16&&!w.navigationExplored_.empty()) {
            const int width=w.hW_/2,height=w.hH_/2;
            for(int z=z0/2;z<=(z0+c.footZ-1)/2;++z)for(int x=x0/2;x<=(x0+c.footX-1)/2;++x)
                if(x>=width||z>=height||!(w.navigationExplored_[size_t(z)*width+x]&(1u<<u.player)))return false;
        }
        return terrainFree(at);
    };
    const auto proof=[&](flow::Cell at,bool contact) {
        arrivalDeferred=localDeferred;
        if(localDeferred)return false;
        const auto target=goal.missionTarget.value_or(std::pair{goal.x,goal.z});
        const flow::Cell center{footprintCell(target.first,c.footX),footprintCell(target.second,c.footZ)};
        // The shared traffic table stores total standing area, not a circle's
        // squared radius. Keep Retail+ arrivals compact instead of extending
        // a contact chain down the approaching column. Use half the allocated
        // square's side as the radius, with a small local maneuvering minimum.
        // The table already includes one cell of spacing around every member;
        // actual standing footprints and terrain connections remain mandatory.
        const int groupArea=p.policy.arrivalRadiusSquared(c);
        if(groupArea) {
            const auto standing=contact?c.position:at;
            const int64_t dx=int64_t(standing.x)-center.x,dz=int64_t(standing.z)-center.z;
            const int minimumRadius=2*std::max(c.footX,c.footZ)+4;
            const int compactArea=std::max((groupArea+3)/4,std::min(groupArea,minimumRadius*minimumRadius));
            if(dx*dx+dz*dz>compactArea)return false;
        }
        // A proof that cannot fit even a fresh tick must not defer forever.
        // Decline this optional arrival shortcut; native routing remains live.
        const uint64_t maximumWork=contact?
            retailplus::Traffic::contactProofCost(c.position,at,c.footX,c.footZ):
            retailplus::Traffic::arrivalProofCost(c.position,at,center,c.footX,c.footZ);
        if(maximumWork>p.proof.limit)return false;
        if(!p.proof.accepts(u.id)){arrivalDeferred=true;return false;}
        if(contact)return flow::directRoute(c.position,at,64,knownFree,[&](flow::Cell cell){return cell==at;});
        const RetailCircleGoal region{center.x,center.z,std::bit_cast<int32_t>(goal.missionRadius+4u),
            retailCircleRadiusSquared(std::bit_cast<int32_t>(goal.missionRadius+4u))};
        return flow::directRoute(c.position,at,64,[&](flow::Cell cell){
            if(!knownFree(cell))return false;
            const size_t weight=size_t(c.footX)*c.footZ;
            if(!p.proof.spend(u.id,weight)){arrivalDeferred=true;return false;}
            p.proofCells+=weight;return free(cell);
        },
            [&](flow::Cell cell){return cell==at;})&&
            flow::directRoute(at,center,512,knownFree,[&](flow::Cell cell){return region.accepts(cell.x,cell.z);});
    };
    const auto arrival=[&](flow::Cell at){return proof(at,false);};
    const auto contact=[&](flow::Cell at){return proof(at,true);};
    c.arrivalReachable=std::cref(arrival);c.contactReachable=std::cref(contact);
    if(profile){p.setupNanoseconds+=profileElapsed(setupStart);setupProfile.enabled=false;}
    const auto policyStart=profile?ProfileClock::now():ProfileClock::time_point{};
    auto result=p.policy.update(c);
    if(profile)p.policyNanoseconds+=profileElapsed(policyStart);
    if(queryTooDense)return unavailable();
    if(localDeferred) {
        // Unknown standing/route evidence must also defer the native mission
        // and corner handlers, even when there is no reserved arrival slot.
        result.wait=result.arrivalApproach=true;result.repath=result.settled=false;
    }
    return result;
}

uint64_t RetailPlusNavigator::checksum() const {
    auto h=impl_->policy.checksum();
    const auto mix=[&](uint64_t value){h^=value;h*=1099511628211ull;};
    mix(impl_->proof.limit);mix(impl_->proof.remaining);mix(impl_->proof.after);mix(impl_->proof.last);mix(impl_->proof.exhausted);
    mix(impl_->bodies.limit);mix(impl_->bodies.remaining);mix(impl_->bodies.after);mix(impl_->bodies.last);mix(impl_->bodies.exhausted);
    return h;
}
RetailPlusNavigator::Stats RetailPlusNavigator::stats() const {
    return {impl_->policy.stats(),impl_->proofCells,sizeof(*this)+sizeof(Impl)-sizeof(impl_->policy)+impl_->policy.bytes(),
        impl_->bodyEntries,impl_->bodyDeferrals,impl_->contextNanoseconds,impl_->setupNanoseconds,
        impl_->policyNanoseconds,impl_->maintenanceNanoseconds,impl_->fastUpdates,impl_->fullUpdates};
}
}
