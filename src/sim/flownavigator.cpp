#include "flownavigator.h"
#include "cooperativeclearance.h"
#include "flowservice.h"
#include "flowlocal.h"
#include "flowroute.h"
#include "flowmemory.h"
#include "navigationmemory.h"
#include "flowobstacles.h"
#include "flowcost.h"
#include "retailheight.h"
#include "flowsnapshot.h"
#include "sim.h"
#include <algorithm>
#include <bit>
#include <map>
#include <tuple>
#include <utility>

namespace tak::sim {
// This adapter is deliberately separate from PathService. Only explicit
// shared-navigation worlds own it; Retail never allocates these caches or jobs.
struct FlowNavigator::Impl {
    using Cell=flow::Cell;
    using Key=std::tuple<int,int,int,int,int,int,int>;
    struct Profile {
        uint64_t id=0,used=0,leaseUntil=0,retryAt=0,preparingHash=1469598103934665603ull,topologyHash=0;
        unsigned failures=0;
        bool delivered=false;
        const UnitType* type=nullptr;
        int player=0;
        std::shared_ptr<const flow::Topology> topology;
        std::unique_ptr<flow::SnapshotBuilder> builder;
        std::vector<bool> dirty;
        // Cooperative can certify unchanged published tiles while another
        // part of this profile rebuilds. begin() transfers dirty to the job;
        // retain those bits until publication rather than treating them clean.
        std::vector<bool> updating;
        // Proof bits describe only the matching immutable snapshot. Building
        // the next generation must not replace proofs for a usable old prefix.
        std::vector<bool> openPublished,openPreparing;
        uint64_t openPublishedHash=0,openPreparingHash=0;
        struct TileStamp {Key key{};uint64_t revision=0;};
        std::vector<TileStamp> preparing;
        std::vector<uint64_t> tileHashes;

    };
    static_assert(sizeof(Profile::TileStamp)+sizeof(uint64_t)+4<=128,
        "profile tile stamps, hashes and four bitmasks must fit canonical metadata");
    struct Request {
        Key key;
        Fixed x,z;
        uint64_t controller=0;
        int player=0;
        const UnitType* type=nullptr;
        flow::GoalRegion region;
        bool bound=false;
        uint64_t issued=0,retryLocal=0;
        int targetId=0;
        uint8_t firingState=0,firingCount=0;
        uint32_t firingCursor=0;
        std::array<Cell,16> firingSeeds{};
        bool firingDone=false,firingContinuation=false,passagePending=false;
        uint64_t telemetryToken=0;
        void resetFiring() {firingState=0;firingCount=0;firingCursor=0;firingDone=false;}
    };
    static_assert(sizeof(Request)<=384,"review canonical request/traffic memory reservation");
    struct Structure {flow::Obstacles::Stamp stamp;uint64_t seen=0;};
    std::map<int,Structure> structures;
    std::unique_ptr<flow::Obstacles> obstacles;
    bool structuresComplete=true;
    World& world;
    flow::MemoryPlan memory;
    flow::Service service;
    // Keep the original traffic object and call order intact in Flowfield.
    // The third mode owns its independent coordinator and reuses terrain data.
    struct Traffic {
        using Context=flow::Traffic::Context;
        flow::Traffic original;
        std::unique_ptr<cooperative::Traffic> coordinator;
        explicit Traffic(bool enabled) {
            if(enabled)coordinator=std::make_unique<cooperative::Traffic>();
        }
        template<class F> decltype(auto) visit(F&& f) {
            if(coordinator)return f(*coordinator);
            return f(original);
        }
        template<class F> decltype(auto) visit(F&& f) const {
            if(coordinator)return f(std::as_const(*coordinator));
            return f(original);
        }
        void registerMove(const Context& c) {visit([&](auto& t){t.registerMove(c);});}
        int arrivalRadiusSquared(const Context& c) const {return visit([&](const auto& t){return t.arrivalRadiusSquared(c);});}
        bool settled(int id,Cell at) const {return visit([&](const auto& t){return t.settled(id,at);});}
        void refreshSettled(int id,Cell at) {visit([&](auto& t){t.refreshSettled(id,at);});}
        bool nearArrival(const Context& c) const {return visit([&](const auto& t){return t.nearArrival(c);});}
        bool needsArrivalNeighbors(const Context& c) const {return visit([&](const auto& t){return t.needsArrivalNeighbors(c);});}
        void prune(size_t count,const std::function<bool(int,int,uint64_t,Cell,bool)>& valid) {
            visit([&](auto& t){t.prune(count,valid);});
        }
        flow::Traffic::Result update(const Context& c) {return visit([&](auto& t){return t.update(c);});}
        uint64_t checksum() const {return visit([](const auto& t){return t.checksum();});}
        size_t bytes() const {return visit([](const auto& t){return t.bytes();});}
    } traffic;
    std::unique_ptr<cooperative::Passages> passages;
    std::map<Key,Profile> profiles;
    using TileKey=std::pair<Key,size_t>;
    struct CachedTile {std::shared_ptr<const flow::Tile> tile;uint64_t revision=0,used=0,fingerprint=0;bool open=false;};
    std::map<TileKey,CachedTile> tiles;
    std::map<uint64_t,TileKey> tileLru;
    struct Evidence {
        std::array<uint64_t,16> knowledge{};
        uint64_t terrain=0;
        uint16_t any=0,all=0;
        bool dirty=true;
    };
    std::vector<Evidence> evidence;
    uint64_t revision=0,tileClock=0;
    std::shared_ptr<const flow::Tile> unknownTile;
    uint64_t unknownHash=0;

    std::map<int,Request> requests;
    std::map<int,std::unique_ptr<flow::LocalRoute>> local;
    std::map<Key,size_t> subscribers;
    std::map<Key,uint64_t> waiting;
    std::map<uint64_t,Key> admission;
    uint64_t nextAdmission=0;
    Stats counters;
    NavigationTelemetry* telemetry=nullptr;
    uint64_t nextProfile=0,clock=0;
    int requestCursor=0;
    size_t firingRays=0,firingCells=0;
    flow::ProofBudget arrivalBudget;
    // Canonical reservations include simultaneous old/new snapshots and scratch.
    // Admission is independent of pointer sizes and measured allocator usage.
    static constexpr size_t maxRequests=flow::MemoryPlan::maxRequests;
    explicit Impl(World& w):world(w),memory(navigationMemoryPlan(w.pathfindingMode_,w.nav_.width(),w.nav_.height())),
        service(flow::Service::Budget{flow::MemoryPlan::maxDestinations,flow::MemoryPlan::maxFields,
            flow::MemoryPlan::maxBuilders,flow::MemoryPlan::maxRequests,8192,4096}),
        traffic(w.pathfindingMode_==PathfindingMode::Cooperative) {
        if(traffic.coordinator) {
            passages=std::make_unique<cooperative::Passages>();
        }
        updateAlliances();
        evidence.resize(size_t((w.nav_.width()+63)/64)*((w.nav_.height()+63)/64));
    }
    void updateAlliances() {
        if(!traffic.coordinator)return;
        for(int player=0;player<16;++player) {
            uint16_t mask=0;
            for(int other=0;other<16;++other)
                if(world.allied(player,other))mask|=uint16_t(1u<<other);
            traffic.coordinator->setAllianceMask(player,mask);
        }
    }
    Key key(const Unit& u) const {
        const auto nav=world.navIdx_.find(u.type);
        const int index=nav==world.navIdx_.end()?-1-int(u.type->domain):nav->second;
        const int supportClass=(u.type->upright?1:0)|(u.type->floater?2:0)|(u.type->canHover?4:0)|
            (int(uint8_t(u.type->waterline))<<3);
        return {index,u.type->footX,u.type->footZ,u.type->roadMult.v,u.type->waterMult.v,u.player,supportClass};
    }
    static const Order* exitRally(const Unit& other) {
        if(other.orders.empty()||!other.type)return nullptr;
        const size_t end=World::currentLeg(other.orders);const auto& exit=other.orders[end];
        if(!exit.productionExit||end+1>=other.orders.size())return nullptr;
        const int64_t movedX=int64_t(other.x.floorInt())-exit.productionExit->first.floorInt();
        const int64_t movedZ=int64_t(other.z.floorInt())-exit.productionExit->second.floorInt();
        // A populated nearby rally can extend back over the birth position.
        // It must not settle this child before that entire footprint is clear
        // for the next output, regardless of the rally's distance from the site.
        if(std::abs(movedX)<other.type->footX*16 &&
           std::abs(movedZ)<other.type->footZ*16)return nullptr;
        const auto& rally=other.orders[end+1];
        if(!rally.goal||!rally.groundMission||rally.targetId||rally.attackMove||rally.patrol||
           rally.guard||rally.load||rally.unload||rally.buildType||rally.reclaimFeat||
           rally.reclaimArea||rally.manaBuildArea||rally.repairTarget||rally.buildRectangle||rally.park||
           rally.transportPickup||rally.transportUnloadApproach||rally.wait||rally.waitAttack)return nullptr;
        const auto point=rally.missionTarget.value_or(std::pair{rally.x,rally.z});
        const int64_t dx=int64_t(point.first.floorInt())-exit.productionExit->first.floorInt();
        const int64_t dz=int64_t(point.second.floorInt())-exit.productionExit->second.floorInt();
        // Nearby rallies must still clear the producer's independent exit.
        if(std::abs(dx)<std::max(60,(other.type->footX+1)*16)&&
           std::abs(dz)<std::max(60,(other.type->footZ+1)*16))return nullptr;
        return &rally;
    }
    flow::Traffic::Identity identity(const Order& order) const {
        auto target=order.missionTarget.value_or(std::pair{order.x,order.z});
        if(order.guard)if(const auto* guarded=world.unit(order.targetId);guarded&&guarded->alive())
            target={guarded->x,guarded->z};
        const uint32_t kind=uint32_t(order.groundMission)|uint32_t(order.attackMove)<<1|
            uint32_t(order.patrol)<<2|uint32_t(order.guard)<<3|uint32_t(order.load)<<4|
            uint32_t(order.unload)<<5|uint32_t(bool(order.buildType))<<6|
            uint32_t(bool(order.reclaimFeat))<<7|uint32_t(bool(order.repairTarget))<<8|
            uint32_t(bool(order.buildRectangle))<<9|uint32_t(bool(order.park))<<10|
            uint32_t(order.transportUnloadApproach)<<11|uint32_t(order.wait>0)<<12|
            uint32_t(order.waitAttack)<<13|uint32_t(bool(order.targetId))<<14|
            uint32_t(bool(order.reclaimArea))<<15|uint32_t(order.transportPickup)<<16;
        const int targetId=order.targetId?order.targetId:order.repairTarget?order.repairTarget:order.reclaimFeat;
        return {order.controller,{target.first.floorInt()/16,target.second.floorInt()/16},kind,targetId};
    }
    int guardReach(const Unit& escort,const Order& order) const {
        const auto* target=world.unit(order.targetId);
        if(!target||!target->type)return 70;
        // A center-only escort radius can lie wholly inside a large building.
        // Account for both complete footprints and one cell of anchor margin.
        return std::max(70,8*std::max(escort.type->footX+target->type->footX,
            escort.type->footZ+target->type->footZ)+16);
    }
    flow::Traffic::Context trafficContext(Unit& u) const {
        flow::Traffic::Context c;
        if(!u.type||u.type->canFly||u.orders.empty())return c;
        const auto& goal=u.orders[World::currentLeg(u.orders)];
        const auto* rally=exitRally(u);const auto& arrival=rally?*rally:goal;
        const auto intent=identity(arrival);
        c.steeringTarget=Cell{footprintCell(u.orders.front().x,u.type->footX),footprintCell(u.orders.front().z,u.type->footZ)};
        c.id=u.id;c.player=u.player;c.controller=goal.controller;c.tick=world.tickCounter_;
        c.issuedTick=arrival.issuedTick;
        c.position={footprintCell(u.x,u.type->footX),footprintCell(u.z,u.type->footZ)};
        c.target=intent.target;c.missionKind=intent.kind;c.targetId=intent.targetId;
        c.footX=u.type->footX;c.footZ=u.type->footZ;c.blocked=u.bodyBlockStreak;
        c.plainMove=(goal.guard||world.groundMissionOrder(u,true))&&!goal.load&&!goal.unload&&
            !goal.transportUnloadApproach&&!goal.buildRectangle&&!goal.park;
        c.goalReached=c.plainMove&&!goal.guard&&world.groundMissionAccepts(u,arrival);
        if(goal.guard) {
            const int64_t dx=int64_t(c.position.x)-c.target.x,dz=int64_t(c.position.z)-c.target.z;
            c.goalReached=dx*dx+dz*dz<=retailCircleRadiusSquared(guardReach(u,goal));
        }
        return c;
    }
    bool used(const Key& key) const {
        return subscribers.contains(key);
    }
    void release(const Request& request) {
        const auto it=subscribers.find(request.key);
        if(it!=subscribers.end()&&!--it->second) {
            subscribers.erase(it);
            if(const auto wait=waiting.find(request.key);wait!=waiting.end()) {
                admission.erase(wait->second);waiting.erase(wait);
            }
        }
    }
    void cancel(int id,NavigationTelemetry::Cancel reason=NavigationTelemetry::Cancel::Explicit) {
        service.cancel(id);local.erase(id);
        const auto it=requests.find(id);
        if(it!=requests.end()) {
            if(telemetry)telemetry->cancelled(it->second.telemetryToken,reason);
            release(it->second);requests.erase(it);
        }
    }
    void awaitProfile(const Key& key) {
        if(!waiting.contains(key)) {
            const uint64_t ticket=++nextAdmission;
            waiting.emplace(key,ticket);admission.emplace(ticket,key);
        }
    }
    Profile* profile(const Key& key,const Unit& u) {
        if(auto it=profiles.find(key);it!=profiles.end()) {it->second.used=clock;return &it->second;}
        awaitProfile(key);
        // FIFO between movement classes, not request IDs: crowded classes
        // cannot repeatedly take the slot ahead of a rarer movement class.
        if(admission.begin()->second!=key)return nullptr;
        if(profiles.size()==memory.profiles) {
            auto oldest=profiles.end();
            for(auto it=profiles.begin();it!=profiles.end();++it) {
                const auto& p=it->second;
                const bool idle=!used(it->first);
                // Protect the first snapshot until it has published, then
                // reserve a useful service interval. Later edits do not extend
                // this lease indefinitely. A failed first snapshot is evictable.
                if(!idle&&(!p.leaseUntil||clock<p.leaseUntil))continue;
                if(oldest==profiles.end()||
                    (idle&&used(oldest->first))||
                    (idle==!used(oldest->first)&&p.used<oldest->second.used))oldest=it;
            }
            if(oldest==profiles.end())return nullptr;
            service.invalidate(oldest->second.id);
            if(passages)passages->invalidate(oldest->second.id);
            for(auto& [id,r]:requests) {(void)id;if(r.key==oldest->first){r.bound=false;r.resetFiring();}}
            if(used(oldest->first))awaitProfile(oldest->first);
            profiles.erase(oldest);++counters.profileEvictions;
        }
        admission.erase(waiting.at(key));waiting.erase(key);
        const auto& nav=world.navFor(u.type);
        auto [it,inserted]=profiles.try_emplace(key);(void)inserted;
        auto& p=it->second;p.id=++nextProfile;p.used=clock;p.type=u.type;p.player=u.player;
        p.dirty.assign(size_t((nav.width()+63)/64)*((nav.height()+63)/64),true);
        p.preparing.resize(p.dirty.size());p.tileHashes.resize(p.dirty.size());
        return &p;
    }
    void markDirty(int x,int z,int w,int h,uint16_t viewers=0xffff) {
        // Revisions belong to source tiles, not resident profiles: edits made
        // while a profile is evicted must still invalidate its retained tiles.
        const int width=world.nav_.width(),height=world.nav_.height(),tx=(width+63)/64;
        const int64_t x0=std::max<int64_t>(0,x),z0=std::max<int64_t>(0,z);
        const int64_t x1=std::min<int64_t>(width,int64_t(x)+w),z1=std::min<int64_t>(height,int64_t(z)+h);
        if(x1>x0&&z1>z0) {
            ++revision;
            for(int cz=int(z0)/64;cz<=(z1-1)/64;++cz)for(int cx=int(x0)/64;cx<=(x1-1)/64;++cx) {
                auto& e=evidence[size_t(cz)*tx+cx];e.dirty=true;
                if(viewers==0xffff)e.terrain=revision;
                for(unsigned player=0;player<16;++player)if(viewers&(1u<<player))e.knowledge[player]=revision;
            }
        }
        for(auto it=local.begin();it!=local.end();) {
            const auto& request=requests.at(it->first);const auto origin=it->second->origin();
            if(request.player>=0&&request.player<16&&(viewers&(1u<<request.player))&&
                int64_t(x)<origin.x+64&&int64_t(z)<origin.z+64&&int64_t(x)+w>origin.x&&int64_t(z)+h>origin.z)
                it=local.erase(it);
            else ++it;
        }
        for(auto& [key,p]:profiles) {
            (void)key;if(p.player<0||p.player>=16||!(viewers&(1u<<p.player)))continue;
            const auto& nav=world.navFor(p.type);
            flow::SnapshotBuilder::dirtyRectangle(p.dirty,nav.width(),nav.height(),p.type->footX,p.type->footZ,x,z,w,h);
        }
    }
    void updateStructures() {
        structuresComplete=true;
        if(!obstacles)obstacles=std::make_unique<flow::Obstacles>(world.nav_.width(),world.nav_.height(),flow::MemoryPlan::maxStructures);
        for(const auto& u:world.units_) {
            if(!u.alive()||!u.type||!u.type->isStructure()||u.embarked())continue;
            const flow::Obstacles::Stamp stamp{footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ),
                u.type->footX,u.type->footZ,u.type->yardMap,
                size_t(u.id)<world.scriptYardById_.size()&&bool(world.scriptYardById_[size_t(u.id)]),
                u.type->gate&&!u.underConstruction&&u.player>=0&&u.player<8&&world.players_[size_t(u.player)].automaticGates?u.player:-1};
            const auto old=structures.find(u.id);
            const auto edit=obstacles->set(u.id,stamp);
            if(edit==flow::Obstacles::Edit::Full) {structuresComplete=false;continue;}
            if(edit==flow::Obstacles::Edit::Changed) {
                if(old!=structures.end()) {const auto& s=old->second.stamp;markDirty(s.x,s.z,s.w,s.h);}
                markDirty(stamp.x,stamp.z,stamp.w,stamp.h);
            }
            structures[u.id]={stamp,clock};
        }
        for(auto it=structures.begin();it!=structures.end();) {
            if(it->second.seen!=clock) {
                const auto& s=it->second.stamp;markDirty(s.x,s.z,s.w,s.h);
                obstacles->set(it->first,std::nullopt);it=structures.erase(it);
            }else ++it;
        }
        obstacles->step(16384);
    }
    uint16_t sample(const Profile& p,int x,int z) const {return sample(p.type,p.player,x,z);}
    uint16_t sample(const UnitType* type,int player,int x,int z) const {
        const auto& nav=world.navFor(type);
        if(x<0||z<0||x>=nav.width()-1||z>=nav.height()-1)return 0;
        if(player>=0&&player<16&&!world.navigationExplored_.empty()) {
            const int width=world.hW_/2,height=world.hH_/2;
            if(x/2<width&&z/2<height&&
                !(world.navigationExplored_[size_t(z/2)*width+x/2]&(1u<<player)))return 64;
        }
        if(obstacles->blocked(x,z,player))return 0;
        const bool hasFeatures=!world.mapPlacementCells_.empty()&&world.mapPlacementCells_.size()==size_t(world.hW_)*world.hH_;
        if(hasFeatures&&world.mapFeatureGrade(x,z)<7)return 0;
        const int grade=nav.liveTerrainGrade(x,z,true);
        if(grade<0)return 0;
        const int terrain=world.heights_.empty()?world.seaLevel_:retailTerrainHeight(
            int32_t((x*16+8)*65536),int32_t((z*16+8)*65536),world.terW_,world.terH_,
            [&](int cx,int cz){return world.heights_[size_t(cz)*world.terW_+cx];});
        return flow::terrainCost(grade,nav.roadAt(x,z),terrain,world.seaLevel_,
            type->upright,type->floater,type->canHover,type->waterline,type->roadMult.v,type->waterMult.v);
    }
    struct TileEvidence {uint64_t terrain=0,knowledge=0;bool unknown=false,known=false;};
    TileEvidence tileEvidence(const Profile& p,flow::SnapshotBuilder::Rectangle r) {
        TileEvidence out;out.unknown=p.player>=0&&p.player<16&&!world.navigationExplored_.empty();
        out.known=!out.unknown;
        if(r.w<=0||r.h<=0)return out;
        const int tx=(world.nav_.width()+63)/64,fw=world.hW_/2,fh=world.hH_/2;
        const uint16_t mask=out.unknown?uint16_t(1u<<p.player):0;
        if(mask)out.known=true;
        for(int tz=r.z/64;tz<=(r.z+r.h-1)/64;++tz)for(int cx=r.x/64;cx<=(r.x+r.w-1)/64;++cx) {
            auto& e=evidence[size_t(tz)*tx+cx];
            if(e.dirty) {
                e.any=0;e.all=0xffff;
                if(world.navigationExplored_.empty())e.all=0xffff;
                else for(int z=tz*32;z<std::min(fh,(tz+1)*32);++z)
                    for(int x=cx*32;x<std::min(fw,(cx+1)*32);++x) {
                        const auto value=world.navigationExplored_[size_t(z)*fw+x];e.any|=value;e.all&=value;
                    }
                e.dirty=false;
            }
            out.terrain=std::max(out.terrain,e.terrain);
            if(mask) {
                out.knowledge=std::max(out.knowledge,e.knowledge[size_t(p.player)]);
                out.unknown=out.unknown&&!(e.any&mask);
                out.known=out.known&&bool(e.all&mask);
            }
        }
        return out;
    }
    flow::SnapshotBuilder::Rectangle rawRectangle(const Profile& p,size_t tile) const {
        const auto& nav=world.navFor(p.type);const int tx=(nav.width()+63)/64;
        const int x=int(tile%tx)*64-p.type->footX/2,z=int(tile/tx)*64-p.type->footZ/2;
        const int x0=std::max(0,x),z0=std::max(0,z);
        return {x0,z0,std::max(0,std::min(nav.width(),x+64+p.type->footX-1)-x0),
                       std::max(0,std::min(nav.height(),z+64+p.type->footZ-1)-z0)};
    }
    template<bool ProveOpen=false>
    static uint64_t tileFingerprint(const flow::Tile& value,bool* open=nullptr) {
        uint64_t hash=1469598103934665603ull;
        bool all=true;
        for(size_t n=0;n<flow::kTileCells;++n) {
            hash=(hash^(uint32_t(value.cost[n])|(uint32_t(value.component[n])<<16)))*1099511628211ull;
            if constexpr(ProveOpen)all&=value.cost[n]!=0;
        }
        if constexpr(ProveOpen)*open=all;
        hash=(hash^value.componentCount)*1099511628211ull;
        return (hash^value.minimumCost)*1099511628211ull;
    }
    static void prepareOpen(Profile& p,size_t tile,bool open) {
        if(p.openPreparing[tile]==open)return;
        p.openPreparing[tile]=open;
        uint64_t hash=uint64_t(tile+1)*0x9e3779b97f4a7c15ull;
        hash=(hash^(hash>>30))*0xbf58476d1ce4e5b9ull;
        p.openPreparingHash^=hash^(hash>>27);
    }
    std::shared_ptr<const flow::Tile> prepared(Profile& p,size_t tile) {
        const auto rectangle=rawRectangle(p,tile);const auto e=tileEvidence(p,rectangle);
        auto key=this->keyFor(p);
        // Sharing known terrain is safe only when the complete halo is known
        // and it has no owner-specific automatic-gate permissions.
        if(e.known&&!obstacles->playerSensitive(rectangle.x,rectangle.z,rectangle.w,rectangle.h))std::get<5>(key)=-1;
        auto& stamp=p.preparing[tile];stamp={key,std::max(e.terrain,std::get<5>(key)<0?0:e.knowledge)};
        const auto mixStamp=[&](uint64_t value){p.preparingHash=(p.preparingHash^value)*1099511628211ull;};
        mixStamp(tile);mixStamp(stamp.revision);std::apply([&](auto... values){(mixStamp(uint64_t(values)),...);},key);
        const auto& nav=world.navFor(p.type);const int tx=(nav.width()+63)/64;
        const int x=int(tile%tx)*64-p.type->footX/2,z=int(tile/tx)*64-p.type->footZ/2;
        // No source-cell reads, SAT, footprint emission or component flood for
        // an entirely unknown interior halo. Every profile can share this tile.
        if(e.unknown&&x>=0&&z>=0&&x+64+p.type->footX-1<=nav.width()-1&&z+64+p.type->footZ-1<=nav.height()-1) {
            if(!unknownTile) {
                auto value=std::make_shared<flow::Tile>();value->cost.fill(64);value->component.fill(1);
                value->componentCount=1;value->minimumCost=64;unknownHash=tileFingerprint(*value);unknownTile=std::move(value);
            }
            if(passages)prepareOpen(p,tile,true);
            p.tileHashes[tile]=unknownHash;mixStamp(unknownHash);++counters.uniformTiles;return unknownTile;
        }
        const auto found=tiles.find({key,tile});
        if(found==tiles.end()||found->second.revision!=stamp.revision)return {};
        tileLru.erase(found->second.used);found->second.used=++tileClock;tileLru.emplace(tileClock,found->first);
        p.tileHashes[tile]=found->second.fingerprint;mixStamp(found->second.fingerprint);
        if(passages)prepareOpen(p,tile,found->second.open);
        ++counters.tileCacheHits;return found->second.tile;
    }
    Key keyFor(const Profile& p) const {
        const auto nav=world.navIdx_.find(p.type);
        const int index=nav==world.navIdx_.end()?-1-int(p.type->domain):nav->second;
        const int support=(p.type->upright?1:0)|(p.type->floater?2:0)|(p.type->canHover?4:0)|(int(uint8_t(p.type->waterline))<<3);
        return {index,p.type->footX,p.type->footZ,p.type->roadMult.v,p.type->waterMult.v,p.player,support};
    }
    void publishTile(Profile& p,size_t tile,std::shared_ptr<const flow::Tile> value) {
        const auto stamp=p.preparing[tile];const TileKey key{stamp.key,tile};
        if(const auto old=tiles.find(key);old!=tiles.end())tileLru.erase(old->second.used);
        else if(tiles.size()==flow::MemoryPlan::maxCachedTiles) {
            tiles.erase(tileLru.begin()->second);tileLru.erase(tileLru.begin());++counters.tileCacheEvictions;
        }
        uint64_t fingerprint=1469598103934665603ull;
        // Compute once at publication, never by scanning tile payloads in the
        // per-tick World checksum. Reuse it for an unchanged immutable tile.
        const auto old=tiles.find(key);
        bool open=false;
        if(old!=tiles.end()&&old->second.tile==value){fingerprint=old->second.fingerprint;open=old->second.open;}
        else if(passages){fingerprint=tileFingerprint<true>(*value,&open);++counters.cooperativeClearanceRebuilds;}
        else fingerprint=tileFingerprint(*value);
        if(passages)prepareOpen(p,tile,open);
        p.tileHashes[tile]=fingerprint;
        tiles[key]={std::move(value),stamp.revision,++tileClock,fingerprint,open};tileLru.emplace(tileClock,key);++counters.preparedTiles;
    }
    void delivered(const Request& r) {
        const uint64_t age=clock-r.issued;
        counters.deliveredAgeMax=std::max(counters.deliveredAgeMax,age);counters.deliveredAgeTotal+=age;
        ++counters.deliveries;
    }
    void observeDelivery(Request& r,bool failed,size_t points) {
        if(telemetry)telemetry->delivered(r.telemetryToken,failed,points);
        r.telemetryToken=0;
    }
    void begin(Profile& p) {
        const auto& nav=world.navFor(p.type);
        p.preparingHash=1469598103934665603ull;
        flow::SnapshotBuilder::Hooks hooks;
        hooks.prepared=[this,&p](size_t tile){return prepared(p,tile);};
        hooks.uniform=[this,&p](flow::SnapshotBuilder::Rectangle r)->std::optional<uint16_t> {
            // sample() reserves the final row/column as blocked. Certify a
            // uniform raw halo only when it avoids that boundary as well.
            const auto& nav=world.navFor(p.type);
            if(r.x+r.w>=nav.width()||r.z+r.h>=nav.height())return {};
            return tileEvidence(p,r).unknown?std::optional<uint16_t>(64):std::nullopt;
        };
        hooks.publish=[this,&p](size_t tile,std::shared_ptr<const flow::Tile> value){publishTile(p,tile,std::move(value));};
        if(passages) {
            p.updating=p.dirty;
            p.openPreparing=p.openPublished;p.openPreparing.resize(p.dirty.size(),false);
            p.openPreparingHash=p.openPublishedHash;
        }
        p.builder=std::make_unique<flow::SnapshotBuilder>(nav.width(),nav.height(),p.type->footX,p.type->footZ,
            [this,&p](int x,int z){return sample(p,x,z);},p.topology,p.dirty,
            flow::MemoryPlan::topologyLimits,std::move(hooks),world.serialThreads_?0:4);
        std::fill(p.dirty.begin(),p.dirty.end(),false);
    }
    flow::GoalRegion goals(Unit& u,Fixed x,Fixed z) const {
        const auto* mission=world.navigationMissionOrder(u);
        const int offX=u.type->footX/2,offZ=u.type->footZ/2;
        if(mission&&mission->buildRectangle) {
            const auto& r=*mission->buildRectangle;
            return {flow::GoalRegion::Kind::Rectangle,r.minX+offX,r.minZ+offZ,r.maxX+offX,r.maxZ+offZ};
        }
        if(mission&&mission->park&&mission->park->ring) {
            const auto& r=*mission->park->ring;
            return {flow::GoalRegion::Kind::Ring,r.x+offX,r.z+offZ,0,0,r.outerSquared,
                r.innerRadius>0?uint64_t(r.innerRadius)*uint64_t(r.innerRadius):0};
        }
        if(!mission&&!u.orders.empty()) {
            const auto& leg=u.orders[World::currentLeg(u.orders)];
            const auto* target=world.unit(leg.repairTarget?leg.repairTarget:leg.targetId);
            if(target&&target->type) {
                // These jobs stop at interaction range, not inside the target
                // footprint. Retail's search could supply a closest-approach
                // route to a blocked point; a reverse field must seed the
                // reachable interaction area explicitly.
                float reach=0;uint64_t innerSquared=0;
                if(leg.guard)reach=float(guardReach(u,leg));
                else if(leg.repairTarget) {
                    const float half=8.f*float(std::max(target->type->footX,target->type->footZ));
                    reach=std::max(half+40.f,u.type->buildDist>0?u.type->buildDist+half:0.f)-16.f;
                }else if(leg.targetId) {
                    const int slot=u.type->weapons.empty()?0:std::clamp(u.weaponSlot,0,int(u.type->weapons.size())-1);
                    const Weapon* selected=u.type->weapons.empty()?&u.type->weapon:&u.type->weapons[size_t(slot)];
                    if(!u.type->weaponSwitching&&u.type->weapons.size()>1)
                        selected=&*std::max_element(u.type->weapons.begin(),u.type->weapons.end(),
                            [](const Weapon& a,const Weapon& b){return a.range<b.range;});
                    if(selected->melee) {
                        // The actual melee gate is footprint-box adjacency.
                        // Its inscribed circle stays inside both axis limits.
                        reach=8.f*float(std::min(u.type->footX+target->type->footX,
                                               u.type->footZ+target->type->footZ))+7.f;
                    }else {
                        const float range=!u.type->weaponSwitching&&u.type->weapons.size()>1
                            ?u.type->maxRange():selected->range;
                        const float pad=target->type->isStructure()
                            ?8.f*float(std::max(target->type->footX,target->type->footZ))+24.f:0.f;
                        reach=std::max(0.f,range+pad-16.f);
                        if(selected->minRange>0) {
                            const uint64_t minimum=uint64_t(selected->minRange)+16;
                            innerSquared=minimum*minimum;
                        }
                    }
                }
                const int cells=int(std::clamp(reach/16.f,0.f,46340.f));
                return {innerSquared?flow::GoalRegion::Kind::Ring:flow::GoalRegion::Kind::Circle,
                    footprintCell(x,u.type->footX),footprintCell(z,u.type->footZ),0,0,
                    leg.guard?retailCircleRadiusSquared(guardReach(u,leg)):cells*cells,innerSquared};
            }
        }
        // Fight/patrol wrap the same ground move but are deliberately excluded
        // from the retail plain-move controller. Retain their arrival area.
        const auto* move=mission;
        if(!move&&!u.orders.empty()) {
            const auto& leg=u.orders[World::currentLeg(u.orders)];
            if(leg.groundMission&&!leg.targetId&&(leg.attackMove||leg.patrol))move=&leg;
        }
        const int radius=move?std::bit_cast<int32_t>(move->missionRadius+4u):0;
        return {flow::GoalRegion::Kind::Circle,footprintCell(x,u.type->footX),footprintCell(z,u.type->footZ),0,0,
            retailCircleRadiusSquared(radius)};
    }
    void advanceProfiles() {
        size_t remaining=65536;
        // Round-robin among profiles, with fixed total work independent of CPUs.
        if(profiles.empty())return;
        size_t active=0;for(const auto& [key,p]:profiles){(void)key;active+=bool(p.builder);}
        auto it=profiles.begin();std::advance(it,size_t(clock%profiles.size()));
        for(size_t n=0;n<profiles.size()&&active<flow::MemoryPlan::maxSnapshots;++n) {
            auto& p=it->second;
            if(!p.builder&&clock>=p.retryAt&&std::any_of(p.dirty.begin(),p.dirty.end(),[](bool d){return d;})){begin(p);++active;}
            if(++it==profiles.end())it=profiles.begin();
        }
        // Split the fixed budget among active jobs, not idle profile metadata.
        // This also gives tile workers useful batches instead of tiny futures.
        for(size_t n=0;n<profiles.size()&&remaining;++n) {
            auto& p=it->second;
            if(p.builder) {
                const size_t quantum=std::max<size_t>(1,remaining/active);--active;
                const size_t work=p.builder->step(quantum);
                remaining-=work;counters.snapshotWork+=work;
                if(p.builder->done()) {
                    const auto topology=p.builder->finish();
                    if(topology!=p.topology) {
                        service.invalidate(p.id);
                        if(passages)passages->invalidate(p.id);
                        for(auto& [id,r]:requests) { (void)id;if(r.key==it->first){r.bound=false;r.resetFiring();} }
                        p.topology=topology;p.topologyHash=p.builder->checksum();
                        if(passages){p.openPublished=std::move(p.openPreparing);p.openPublishedHash=p.openPreparingHash;}
                        for(uint64_t hash:p.tileHashes)p.topologyHash=(p.topologyHash^hash)*1099511628211ull;
                    }
                    p.builder.reset();p.updating.clear();p.openPreparing.clear();p.openPreparingHash=0;p.failures=0;p.retryAt=0;
                    // A fragmented graph may need much more than 256 ticks to
                    // seed/integrate its first destination. Give the first
                    // global delivery a larger, still bounded opportunity.
                    if(!p.leaseUntil)p.leaseUntil=clock+4096;
                }else if(p.builder->failed()) {
                    // Release failed graph/snapshot storage. Retain all edits,
                    // and retry at deterministic bounded intervals so a later
                    // terrain/yard change can recover a previously over-limit
                    // graph without spinning every tick or pinning this slot.
                    p.builder.reset();p.updating.clear();p.openPreparing.clear();p.openPreparingHash=0;++counters.snapshotFailures;std::fill(p.dirty.begin(),p.dirty.end(),true);
                    p.failures=std::min(p.failures+1,6u);
                    p.retryAt=clock+(uint64_t(60)<<p.failures);
                    if(!p.leaseUntil)p.leaseUntil=clock;
                }
            }
            if(++it==profiles.end())it=profiles.begin();
        }
    }
    bool valid(Unit* u,const Request& r) const {
        if(!u||!u->alive()||u->type!=r.type||u->player!=r.player||u->orders.empty())return false;
        // Fight/patrol wrappers may change mission flags after order() has
        // enqueued navigation. The current leg identity remains authoritative.
        return u->orders[World::currentLeg(u->orders)].controller==r.controller &&
            u->orders[World::currentLeg(u->orders)].targetId==r.targetId;
    }
    void served(Profile& p) {
        if(!p.delivered) {p.delivered=true;p.leaseUntil=clock+256;}
    }
    // Charge the worst-case Bresenham length before each exact combat ray.
    // Ray/candidate quotas are shared across requests and independent of CPUs.
    bool firingRay(const Unit& from,const Unit& to,bool& clear) {
        const size_t work=size_t(std::max(std::abs(from.x.floorInt()/16-to.x.floorInt()/16),
            std::abs(from.z.floorInt()/16-to.z.floorInt()/16)))+1;
        if(!firingRays||work>firingCells)return false;
        --firingRays;firingCells-=work;++counters.firingRays;counters.firingCells+=work;
        clear=world.combatLineOfSight(from,to);return true;
    }
    // Classification needs no snapshot: cold/evicted combat requests must
    // not publish a distance-only local route before checking firing LOS.
    bool classifyFiring(Request& r,Unit& u) {
        const auto* target=world.unit(r.targetId);
        if(!r.firingState) {
            const auto& leg=u.orders[World::currentLeg(u.orders)];
            const int slot=u.type->weapons.empty()?0:std::clamp(u.weaponSlot,0,int(u.type->weapons.size())-1);
            const auto& weapon=u.type->weapons.empty()?u.type->weapon:u.type->weapons[size_t(slot)];
            if(!target||!target->type||leg.guard||leg.repairTarget||u.type->canFly||target->type->canFly||
               weapon.melee||u.type->lobs()||u.type->maxRange()<=64) {
                r.firingState=1;return true;
            }
            const int64_t dx=int64_t(u.x.floorInt())-target->x.floorInt(),dz=int64_t(u.z.floorInt())-target->z.floorInt();
            const int64_t reach=int64_t(u.type->maxRange())+32+
                (target->type->isStructure()?8*std::max(target->type->footX,target->type->footZ)+24:0);
            if(!r.firingContinuation&&dx*dx+dz*dz>reach*reach){r.firingState=1;return true;}
            bool clear=false;if(!firingRay(u,*target,clear))return false;
            r.firingState=clear?1:2;
        }
        return true;
    }
    bool prepareFiring(Request& r,Profile& p,Unit& u) {
        if(!classifyFiring(r,u))return false;
        if(r.firingState==1||r.bound)return true;
        const auto* target=world.unit(r.targetId);
        if(!target||!target->type)return false;
        auto [lo,hi]=r.region.bounds();lo.x=std::max(0,lo.x);lo.z=std::max(0,lo.z);
        hi.x=std::min(p.topology->width-1,hi.x);hi.z=std::min(p.topology->height-1,hi.z);
        const uint32_t width=uint32_t(std::max(0,hi.x-lo.x+1));
        const uint32_t count=width*uint32_t(std::max(0,hi.z-lo.z+1));
        int low=0,high=46340;while(low<high){const int mid=low+(high-low+1)/2;if(int64_t(mid)*mid<=r.region.outerSquared)low=mid;else high=mid-1;}
        const int radius=low;
        const std::array<Cell,8> around{{{radius,0},{0,radius},{-radius,0},{0,-radius},
            {radius*181/256,radius*181/256},{-radius*181/256,radius*181/256},
            {-radius*181/256,-radius*181/256},{radius*181/256,-radius*181/256}}};
        // Minimal stack probe: combatLineOfSight reads only position/type.
        // Seed batches avoid per-request dynamic memory or whole-region rays.
        Unit candidate;candidate.type=u.type;
        while(r.firingCount<r.firingSeeds.size()&&!r.firingDone) {
            if(r.firingCursor>=8+count){r.firingDone=true;break;}
            // Try a coarse ring first. If none of its valid seeds is reachable,
            // resume the exhaustive bounded scan; narrow firing gaps are not
            // permanently missed by the directional sampling.
            if(r.firingCursor==8&&r.firingCount)return true;
            if(!firingCells)return false;
            Cell at;
            if(r.firingCursor<8) {at=around[r.firingCursor];at.x+=r.region.x;at.z+=r.region.z;}
            else {const uint32_t n=r.firingCursor-8;at={lo.x+int(n%width),lo.z+int(n/width)};}
            if(!r.region.contains(at)||!p.topology->cost(at)){--firingCells;++counters.firingCells;++r.firingCursor;continue;}
            candidate.x=footprintWaypoint(at.x,u.type->footX);candidate.z=footprintWaypoint(at.z,u.type->footZ);
            bool clear=false;if(!firingRay(candidate,*target,clear))return false;
            ++r.firingCursor;
            if(clear)r.firingSeeds[r.firingCount++]=at;
        }
        return r.firingCount||r.firingDone;
    }
    bool deliver(int id,Request& r,Profile& p,Unit& u) {
        if(!p.topology)return false;
        // Incomplete obstacle installation cannot certify even a local prefix.
        if(passages&&(!structuresComplete||!obstacles->settled())) {
            r.passagePending=true;return false;
        }
        const bool snapshotFresh=!passages||(!p.builder&&
            !std::any_of(p.dirty.begin(),p.dirty.end(),[](bool d){return d;}));
        const auto freshTile=[&](int tile) {
            if(snapshotFresh)return true;
            // Out-of-map cells remain blocked independently of any rebuild.
            return tile<0||(!p.dirty[size_t(tile)]&&
                (p.updating.empty()||!p.updating[size_t(tile)]));
        };
        const auto fresh=[&](Cell cell){return snapshotFresh||freshTile(p.topology->tileAt(cell));};
        Cell at{footprintCell(u.x,u.type->footX),footprintCell(u.z,u.type->footZ)};
        if(!fresh(at)){r.passagePending=true;return false;}
        if(!prepareFiring(r,p,u))return false;
        if(r.firingState==2&&!r.firingCount&&r.firingDone) {
            if(!snapshotFresh){r.passagePending=true;return false;}
            observeDelivery(r,true,0);
            world.deliverSearchRoute(id,{},r.x,r.z,true,false,false,false);
            delivered(r);++counters.failures;served(p);return true;
        }
        if(!r.bound)r.bound=r.firingState==2
            ?service.bind(id,p.id,p.topology,std::vector<Cell>(r.firingSeeds.begin(),r.firingSeeds.begin()+r.firingCount))
            :service.bindRegion(id,p.id,p.topology,r.region);
        if(!r.bound)return false;
        std::vector<PathCell> route;route.reserve(64);route.push_back({at.x,at.z});
        if(!p.topology->cost(at)) {
            // This separate recovery path does not screen a complete open
            // prefix, so retain the full freshness requirement for admission.
            if(!snapshotFresh){r.passagePending=true;return false;}
            // Live collision can stop between cell anchors as a newly revealed
            // structure enters the footprint. Escape to a nearby legal
            // anchor; a blocked start is not proof that the goal is unreachable.
            // Cardinal-only recovery cannot cut a blocked diagonal corner, and
            // normal live placement still judges every movement step.
            std::vector<std::pair<int64_t,Cell>> exits;
            bool staticExit=false;
            for(const Cell delta:std::array<Cell,4>{{{0,-1},{-1,0},{1,0},{0,1}}}) {
                // Only the immediately adjacent anchor can recover an
                // in-between-cell stop. Searching farther for a free anchor
                // jumped across solid terrain and retried impossible escapes
                // forever (notably a ship boxed into a shallow pocket).
                const Cell next{at.x+delta.x,at.z+delta.z};
                if(!p.topology->cost(next))continue;
                staticExit=true;
                if(world.cellScore(u.type,next.x,next.z,id)<4)continue;
                const int64_t dx=footprintWaypoint(next.x,u.type->footX).floorInt()-u.x.floorInt();
                const int64_t dz=footprintWaypoint(next.z,u.type->footZ).floorInt()-u.z.floorInt();
                exits.emplace_back(dx*dx+dz*dz,next);
            }
            std::stable_sort(exits.begin(),exits.end(),[](const auto& a,const auto& b){return a.first<b.first;});
            for(const auto& [distance,next]:exits) {
                (void)distance;
                const auto sample=service.sample(id,next);
                if(sample.status==flow::Service::Status::Pending)return false;
                if(sample.status!=flow::Service::Status::Ready&&sample.status!=flow::Service::Status::Arrived)continue;
                route.push_back({next.x,next.z});
                observeDelivery(r,false,route.size());
                world.deliverSearchRoute(id,route,r.x,r.z,false,false,false,true);
                if(auto* goal=world.navigationMissionOrder(u);goal&&goal->controller==r.controller)
                    (goal->load||goal->transportUnloadApproach?goal->transportMission:goal->mission).pending|=0x1000;
                u.routeStamp=std::bit_cast<int32_t>(world.tickCounter_);delivered(r);served(p);
                return true;
            }
            if(!staticExit&&structuresComplete&&obstacles->settled()&&!p.builder&&
                !std::any_of(p.dirty.begin(),p.dirty.end(),[](bool d){return d;})) {
                // With an up-to-date static snapshot and no adjacent legal
                // anchor there is no physical route out. Mobile occupancy
                // alone never reaches this branch: it waits for a free exit.
                observeDelivery(r,true,0);
                world.deliverSearchRoute(id,{},r.x,r.z,true,false,false,false);
                if(auto* goal=world.navigationMissionOrder(u);goal&&goal->controller==r.controller)
                    (goal->load||goal->transportUnloadApproach?goal->transportMission:goal->mission).pending|=0x2000;
                u.routeStamp=std::bit_cast<int32_t>(world.tickCounter_);
                delivered(r);++counters.failures;served(p);return true;
            }
            return false;
        }
        bool arrived=false,failed=false;
        const auto corridor=traffic.coordinator?traffic.coordinator->corridorDirection(trafficContext(u)):
            std::optional<Cell>{};
        const auto lane=traffic.coordinator?traffic.coordinator->corridorAim(trafficContext(u)):
            std::optional<Cell>{};
        std::optional<cooperative::Traffic::Passage> passage;
        // Bound both field sampling and the output independently of route length.
        for(size_t n=0;n<63;++n) {
            const auto next=traffic.coordinator
                ?service.sampleCooperative(id,at,corridor.value_or(Cell{r.region.x-at.x,r.region.z-at.z}),lane)
                :service.sample(id,at,Cell{r.region.x,r.region.z});
            if(next.status==flow::Service::Status::Arrived) {arrived=true;break;}
            if(next.status==flow::Service::Status::Unreachable) {
                if(r.firingState==2&&!r.firingDone){service.cancel(id);r.bound=false;r.firingCount=0;return false;}
                failed=true;break;
            }
            if(next.status!=flow::Service::Status::Ready)break;
            if(!fresh(next.next)||(at.x!=next.next.x&&at.z!=next.next.z&&
                (!fresh({at.x,next.next.z})||!fresh({next.next.x,at.z})))) {
                r.passagePending=true;return false;
            }
            if(passages&&!passage) {
                const Cell delta{(next.next.x>at.x)-(next.next.x<at.x),(next.next.z>at.z)-(next.next.z<at.z)};
                const std::array<Cell,2> directions{{{delta.x,0},{0,delta.z}}};
                for(const Cell direction:directions) {
                    if(direction==Cell{})continue;
                    // Prove open ground with contiguous rays; sampling only
                    // their endpoints could jump over thin separating walls.
                    // Use the detector's full supported width so small peers
                    // also register strips needed by larger bodies.
                    const auto screened=cooperative::Clearance::screen(*p.topology,next.next,bool(direction.z),
                        freshTile,[&](int tile){return p.openPublished[size_t(tile)];},
                        counters.cooperativePassageScreeningCells,counters.cooperativeClearanceHits);
                    if(screened==cooperative::Clearance::Result::Stale){r.passagePending=true;return false;}
                    if(screened==cooperative::Clearance::Result::Open)continue;
                    // Only a fully current topology may supply a cached strip
                    // descriptor. Unrelated edits can delay narrow admission,
                    // but cannot stop a prefix proved open on unchanged tiles.
                    if(!snapshotFresh){r.passagePending=true;return false;}
                    const auto result=passages->probe(world.tickCounter_,p.id,p.topologyHash,*p.topology,
                        u.type->footX,u.type->footZ,next.next,direction);
                    if(result.status==cooperative::Passages::Status::Pending) {
                        r.passagePending=true;return false;
                    }
                    if(result.status==cooperative::Passages::Status::Found) {
                        passage=result.passage;
                        if(!traffic.coordinator->setPassage(id,passage)) {
                            // A proved strip still needs admission to the
                            // bounded gate table. Wait rather than publishing
                            // an uncoordinated route when that table is full.
                            r.passagePending=true;return false;
                        }
                        break;
                    }
                }
            }
            at=next.next;
            // The retail mover consumes direction-change corners, not every
            // 16px cell on a straight run. Keep the same sampled polyline and
            // fixed 63-cell work budget, without extra steering stops/turns.
            flow::appendRouteCorner(route,PathCell{at.x,at.z});
            if(passage) {
                const int64_t dx=int64_t(passage->last.x)-passage->first.x;
                const int64_t dz=int64_t(passage->last.z)-passage->first.z;
                const int64_t passed=(int64_t(at.x)-passage->last.x)*dx+(int64_t(at.z)-passage->last.z)*dz;
                if(passed>int64_t(std::max(u.type->footX,u.type->footZ))*(std::abs(dx)+std::abs(dz)))break;
            }
        }
        if(route.size()==1&&!arrived&&!failed)return false;
        if(passages&&!passage)traffic.coordinator->setPassage(id,{});
        // Never report stale topology as a permanent mission failure.
        if(failed&&(!structuresComplete||!obstacles->settled()||p.builder||std::any_of(p.dirty.begin(),p.dirty.end(),[](bool d){return d;})))return false;
        if(failed&&route.size()==1)route.clear();
        observeDelivery(r,failed,route.size());
        world.deliverSearchRoute(id,route,r.x,r.z,failed,false,false,r.firingState==2);
        if(auto* goal=world.navigationMissionOrder(u);goal&&goal->controller==r.controller)
            (goal->load||goal->transportUnloadApproach?goal->transportMission:goal->mission).pending|=failed?0x2000:0x1000;
        u.routeStamp=std::bit_cast<int32_t>(world.tickCounter_);
        delivered(r);if(failed)++counters.failures;served(p);
        return true;
    }
    void advanceLocal() {
        if(!structuresComplete||!obstacles->settled())return;
        size_t budget=8192,left=local.size();
        for(auto& [id,job]:local) {
            (void)id;const size_t work=job->step(std::max<size_t>(1,budget/left));
            budget-=work;--left;counters.localWork+=work;
        }
    }
    bool fallback(int id,Request& r,Unit& u) {
        if(!structuresComplete||!obstacles->settled())return false;
        auto it=local.find(id);
        if(it==local.end()) {
            if(local.size()>=flow::MemoryPlan::maxLocalJobs||clock<r.retryLocal)return false;
            const auto& nav=world.navFor(u.type);
            const Cell start{footprintCell(u.x,u.type->footX),footprintCell(u.z,u.type->footZ)};
            const Cell target{footprintCell(r.x,u.type->footX),footprintCell(r.z,u.type->footZ)};
            // A local window may finish a nearby mission, but must never
            // substitute a greedy boundary prefix for global maze guidance.
            // Skip jobs that cannot possibly contain any acceptable endpoint.
            const int width=std::min(nav.width(),flow::kTileSize);
            const int height=std::min(nav.height(),flow::kTileSize);
            const Cell origin{std::clamp(start.x-width/2,0,nav.width()-width),
                              std::clamp(start.z-height/2,0,nav.height()-height)};
            const auto [lo,hi]=r.region.bounds();
            if(hi.x<origin.x||hi.z<origin.z||lo.x>=origin.x+width||lo.z>=origin.z+height)
                return false;
            local.emplace(id,std::make_unique<flow::LocalRoute>(nav.width(),nav.height(),u.type->footX,u.type->footZ,
                start,target,r.region,[this,type=u.type,player=u.player](int x,int z){return sample(type,player,x,z);}));
            return false;
        }
        if(!it->second->done())return false;
        const Cell current{footprintCell(u.x,u.type->footX),footprintCell(u.z,u.type->footZ)};
        if(!it->second->reachesGoal()||current!=it->second->start()) {
            local.erase(it);r.retryLocal=clock+30;return false;
        }
        std::vector<PathCell> route;route.reserve(it->second->route().size());
        for(Cell c:it->second->route())route.push_back({c.x,c.z});
        observeDelivery(r,false,route.size());
        world.deliverSearchRoute(id,route,r.x,r.z,false,false,false,true);
        if(auto* goal=world.navigationMissionOrder(u);goal&&goal->controller==r.controller)
            (goal->load||goal->transportUnloadApproach?goal->transportMission:goal->mission).pending|=0x1000;
        u.routeStamp=std::bit_cast<int32_t>(world.tickCounter_);
        ++counters.localDeliveries;delivered(r);local.erase(it);return true;
    }
    void tick() {
        if(!memory.supported)return;
        updateAlliances();
        ++clock;firingRays=64;firingCells=8192;arrivalBudget.tick();updateStructures();
        traffic.prune(64,[&](int id,int player,uint64_t controller,Cell position,bool settled) {
            auto* u=world.unit(id);
            if(!u||!u->alive()||u->embarked()||u->player!=player||!u->type)return false;
            const Cell current{footprintCell(u->x,u->type->footX),footprintCell(u->z,u->type->footZ)};
            if(u->orders.empty()) {
                if(settled&&position!=current)traffic.refreshSettled(id,current);
                return settled;
            }
            const auto& goal=u->orders[World::currentLeg(u->orders)];
            const bool valid=goal.controller?goal.controller==controller:
                (goal.guard||(settled&&goal.groundMission&&(goal.mission.pending&0x500)));
            if(valid&&settled&&position!=current&&(goal.guard||!goal.controller))
                traffic.refreshSettled(id,current);
            return valid;
        });
        if(structuresComplete&&obstacles->settled())advanceProfiles();
        if(passages) {
            // Descriptor work progresses independently of how often a unit's
            // request is revisited. Retain no old topology in the sparse cache.
            std::array<const Profile*,flow::MemoryPlan::maxProfiles> fresh{};
            size_t count=0;
            if(structuresComplete&&obstacles->settled())for(const auto& [key,p]:profiles) {
                (void)key;
                if(p.topology&&!p.builder&&!std::any_of(p.dirty.begin(),p.dirty.end(),[](bool d){return d;}))
                    fresh[count++]=&p;
            }
            passages->advance(world.tickCounter_,[&fresh](uint64_t id,uint64_t generation)->const flow::Topology* {
                for(const auto* p:fresh)if(p&&p->id==id&&p->topologyHash==generation)return p->topology.get();
                return nullptr;
            });
        }
        advanceLocal();
        // Selection does not depend on worker availability or elapsed time.
        service.tick(world.serialThreads_?0:4);
        auto it=requests.upper_bound(requestCursor);
        const size_t count=std::min<size_t>(requests.size(),256);
        for(size_t n=0;n<count&&!requests.empty();++n) {
            if(it==requests.end())it=requests.begin();
            const int id=it->first;requestCursor=id;auto& r=it->second;auto* u=world.unit(id);
            bool remove=!valid(u,r);
            if(remove&&telemetry)telemetry->cancelled(r.telemetryToken,NavigationTelemetry::Cancel::Stale);
            if(!remove) {
                auto* p=profile(r.key,*u);
                const bool classified=classifyFiring(r,*u);
                if(classified&&p)remove=deliver(id,r,*p,*u);
                // Cooperative waits for fair shared-profile admission. A local
                // shortcut would bypass passage discovery and entry permits.
                if(!passages&&!remove&&classified&&r.firingState==1&&clock-r.issued>=64&&(!p||!p->topology||!r.bound))remove=fallback(id,r,*u);
            }
            if(remove) {service.cancel(id);local.erase(id);release(r);it=requests.erase(it);}else ++it;
            if(!firingRays||!firingCells)break;
        }
        counters.fieldWork=service.work();
    }
};
FlowNavigator::FlowNavigator(World& world):impl_(std::make_unique<Impl>(world)) {}
FlowNavigator::~FlowNavigator() {
    if(impl_->telemetry)for(const auto& [id,r]:impl_->requests) {
        (void)id;impl_->telemetry->cancelled(r.telemetryToken,NavigationTelemetry::Cancel::Cleared);
    }
}
void FlowNavigator::setTelemetry(NavigationTelemetry* observer) {
    if(observer!=impl_->telemetry&&!impl_->requests.empty())throw std::logic_error("telemetry requires an empty request queue");
    impl_->telemetry=observer;
}
bool FlowNavigator::request(Unit& u,Fixed x,Fixed z) {
    auto& p=*impl_;
    if(!p.memory.supported)return false;
    if(!p.requests.contains(u.id)&&p.requests.size()>=Impl::maxRequests)return false;
    const auto* mission=u.orders.empty()?nullptr:&u.orders[World::currentLeg(u.orders)];
    // Fight/patrol keep the original click in missionTarget just like plain
    // moves. Their installed prefix endpoint must not become the next goal.
    if(mission&&mission->groundMission&&mission->missionTarget) {
        x=mission->missionTarget->first;z=mission->missionTarget->second;
    }
    const auto key=p.key(u);
    auto goals=p.goals(u,x,z);
    const auto controller=mission?mission->controller:0;
    if(auto it=p.requests.find(u.id);it!=p.requests.end()&&it->second.key==key&&it->second.x==x&&it->second.z==z&&it->second.controller==controller&&it->second.targetId==(mission?mission->targetId:0)&&it->second.region==goals)return true;
    p.cancel(u.id,NavigationTelemetry::Cancel::Replaced);++p.subscribers[key];
    p.requests[u.id]={key,x,z,controller,u.player,u.type,std::move(goals),false,p.clock,0};
    // The third backend must prove clearance and coordinate passage entry
    // before consuming either a new raw goal or an older installed prefix.
    p.requests[u.id].passagePending=bool(p.passages);
    p.requests[u.id].targetId=mission?mission->targetId:0;
    // Preserve an ongoing firing-position detour across bounded prefixes, even
    // when it temporarily leads away from weapon range. Every continuation is
    // still checked against the exact LOS rule before using point seeds.
    p.requests[u.id].firingContinuation=u.routeDetour;
    p.requests[u.id].telemetryToken=p.telemetry?p.telemetry->requested(u.id):0;
    u.routeStamp=std::bit_cast<int32_t>(p.world.tickCounter_);
    u.routeCrowded=u.routeTraffic=u.routeFailed=u.routeDetour=false;
    if(!u.orders.empty()&&World::currentLeg(u.orders)==0)u.orders.front().navigationExhausted=true;
    p.traffic.registerMove(p.trafficContext(u));
    ++p.counters.requests;
    return true;
}
bool FlowNavigator::pending(int unit) const {return impl_->requests.contains(unit);}
bool FlowNavigator::settled(const Unit& u) const {
    if(!u.type)return false;
    const flow::Cell at{footprintCell(u.x,u.type->footX),footprintCell(u.z,u.type->footZ)};
    if(u.alive()&&!u.embarked()&&u.orders.empty())impl_->traffic.refreshSettled(u.id,at);
    return impl_->traffic.settled(u.id,at);
}
bool FlowNavigator::routeBlocked(const Unit& u) const {
    if(u.orders.empty()||!u.type)return false;
    const auto found=impl_->profiles.find(impl_->key(u));
    if(found==impl_->profiles.end()||!found->second.topology) {
        // Eviction removes cached costs, not the installed route. Repeated
        // physical refusal must still notice a static wall beside that route;
        // otherwise an off-segment mover can remain stuck without requesting
        // re-admission. Check only the immediate step, using the same footprint
        // and exploration rules as snapshots, and exclude mobile occupancy.
        if(u.bodyBlockStreak<2||!impl_->structuresComplete||!impl_->obstacles||
           !impl_->obstacles->settled())return false;
        const auto& next=u.orders.front();
        if(!next.hasSegment)return false; // untouched factory exit
        const flow::Cell at{footprintCell(u.x,u.type->footX),footprintCell(u.z,u.type->footZ)};
        const flow::Cell end{footprintCell(next.x,u.type->footX),footprintCell(next.z,u.type->footZ)};
        if(next.goal&&!next.groundMission&&(next.targetId||next.repairTarget)) {
            if(auto* live=impl_->world.unit(u.id);live&&impl_->goals(*live,next.x,next.z).contains(at))return false;
        }
        const int dx=(end.x>at.x)-(end.x<at.x),dz=(end.z>at.z)-(end.z<at.z);
        if(!dx&&!dz)return false;
        const auto free=[&](flow::Cell cell) {
            for(int z=0;z<u.type->footZ;++z)for(int x=0;x<u.type->footX;++x)
                if(!impl_->sample(u.type,u.player,cell.x-u.type->footX/2+x,
                                 cell.z-u.type->footZ/2+z))return false;
            return true;
        };
        return !free({at.x+dx,at.z+dz}) || (dx&&dz&&
            (!free({at.x+dx,at.z})||!free({at.x,at.z+dz})));
    }
    const auto& topo=*found->second.topology;
    // Straight runs are stored as corner endpoints. Check the next cell
    // on the stored segment as well as the endpoint so a newly built wall
    // inside a compressed run still triggers replanning before crossing it.
    const auto& next=u.orders.front();
    const flow::Cell at{footprintCell(u.x,u.type->footX),footprintCell(u.z,u.type->footZ)};
    const flow::Cell end{footprintCell(next.x,u.type->footX),footprintCell(next.z,u.type->footZ)};
    const bool interaction=next.goal&&!next.groundMission&&(next.targetId||next.repairTarget);
    if(interaction) {
        // A blocked target centre is intentional once the unit is already in
        // its interaction area. The owning action supplies the exact stop.
        if(auto* live=impl_->world.unit(u.id);live&&impl_->goals(*live,next.x,next.z).contains(at))return false;
    }
    // Action routes retain their distant target after a bounded prefix.
    // If that raw final approach hits static terrain, request the next prefix;
    // mobile avoidance must not be responsible for completing the route.
    if(interaction&&u.bodyBlockStreak>=2&&topo.cost(at)) {
        const int sx=(end.x>at.x)-(end.x<at.x),sz=(end.z>at.z)-(end.z<at.z);
        if(!topo.cost({at.x+sx,at.z+sz})||(sx&&sz&&
           (!topo.cost({at.x+sx,at.z})||!topo.cost({at.x,at.z+sz}))))return true;
    }
    if(next.hasSegment&&topo.cost(at)) {
        const flow::Cell start{footprintCell(next.segmentX,u.type->footX),
                               footprintCell(next.segmentZ,u.type->footZ)};
        const int64_t dx=int64_t(end.x)-start.x,dz=int64_t(end.z)-start.z;
        const int64_t length=std::max(std::abs(dx),std::abs(dz));
        const int64_t squared=dx*dx+dz*dz;
        const int64_t projection=(int64_t(at.x)-start.x)*dx+(int64_t(at.z)-start.z)*dz;
        // Physical collision or local avoidance can leave a mover beside or
        // beyond its stored segment.
        // Reanchor only after repeated refusal when its direct return crosses
        // static terrain; merely projecting onto the old line misses that wall.
        // Raw factory exits and blocked-start recovery retain their own rules.
        if(u.bodyBlockStreak>=2&&length&&length<=63&&
           (dx==0||dz==0||std::abs(dx)==std::abs(dz))&&
           (projection<0||projection>squared||
            (int64_t(at.x)-start.x)*dz!=(int64_t(at.z)-start.z)*dx)) {
            const int sx=(end.x>at.x)-(end.x<at.x),sz=(end.z>at.z)-(end.z<at.z);
            const flow::Cell ahead{at.x+sx,at.z+sz};
            if(!topo.cost(ahead)||(sx&&sz&&
               (!topo.cost({at.x+sx,at.z})||!topo.cost({at.x,at.z+sz}))))return true;
        }
        if(length&&length<=63&&(dx==0||dz==0||std::abs(dx)==std::abs(dz))&&projection<squared) {
            const int64_t step=std::clamp(projection*length/squared+1,int64_t(1),length);
            const auto point=[&](int64_t k) {return flow::Cell{int(start.x+dx*k/length),int(start.z+dz*k/length)};};
            const auto before=point(step-1),after=point(step);
            if(!topo.cost(after))return true;
            if(before.x!=after.x&&before.z!=after.z&&
                (!topo.cost({before.x,after.z})||!topo.cost({after.x,before.z})))return true;
        }
    }
    // Corner pruning can expose the final interaction centre long before the
    // approach completes. Unlike a route corner, that centre may be occupied.
    return !interaction&&!topo.cost(end);
}
flow::Traffic::Result FlowNavigator::traffic(Unit& u) {
    auto& p=*impl_;auto& w=p.world;
    if(!u.type||u.type->canFly||u.orders.empty())return {};
    if(p.traffic.coordinator) {
        const auto request=p.requests.find(u.id);
        if(request!=p.requests.end()&&request->second.passagePending) {
            flow::Traffic::Result result;result.wait=true;return result;
        }
    }
    const auto& goal=u.orders[World::currentLeg(u.orders)];
    const auto* rally=Impl::exitRally(u);const auto& arrival=rally?*rally:goal;
    auto c=p.trafficContext(u);
    // Completed controllers can brake for several ticks before their native
    // wrapper retires. Refresh before update(), not just the 64-record prune,
    // so a large cohort does not forget members crossing a cell while coasting.
    if(goal.guard||(!goal.controller&&goal.groundMission&&(goal.mission.pending&0x500)))
        p.traffic.refreshSettled(u.id,c.position);
    const auto trafficIdle=[&](const Unit& other) {
        if(other.orders.empty())return true;
        // A parked escort retains Guard forever. It is still a standing body
        // for local avoidance and can anchor arrival contact for its peers.
        const auto& order=other.orders[World::currentLeg(other.orders)];
        return order.guard&&p.traffic.settled(other.id,
            {footprintCell(other.x,other.type->footX),footprintCell(other.z,other.type->footZ)});
    };
    const auto lookup=[&](int id)->std::optional<flow::Traffic::Neighbor> {
        const auto* other=w.unit(id);
        if(!other||!other->alive()||other->embarked()||!other->type||
           (other->type->canFly&&other->flightGroundMode!=1))return {};
        auto result=flow::Traffic::Neighbor{id,other->player,
            {footprintCell(other->x,other->type->footX),footprintCell(other->z,other->type->footZ)},
            other->type->footX,other->type->footZ,trafficIdle(*other)};
        result.mobile=!other->type->isStructure();
        if(!other->orders.empty()) {
            const auto legIndex=World::currentLeg(other->orders);
            const auto& leg=other->orders[legIndex];
            const auto* next=Impl::exitRally(*other);
            result.identity=p.identity(next?*next:leg);result.identity->controller=leg.controller;
            result.steeringTarget=flow::Cell{footprintCell(other->orders.front().x,other->type->footX),
                footprintCell(other->orders.front().z,other->type->footZ)};
            if(p.traffic.coordinator&&result.steeringTarget==result.position) {
                // An off-centre body can still be approaching a corner in its
                // current cell. Its next distinct corner describes which lane
                // a yielding peer must leave clear. Do not cross a queued order
                // or change the physical mover's current steering waypoint.
                for(size_t i=1;i<=std::min<size_t>(legIndex,63);++i) {
                    const flow::Cell at{footprintCell(other->orders[i].x,other->type->footX),
                        footprintCell(other->orders[i].z,other->type->footZ)};
                    if(at!=result.position){result.steeringTarget=at;break;}
                }
            }
        }
        return result;
    };
    c.lookup=std::cref(lookup);
    const bool arrivalNeighbors=p.traffic.needsArrivalNeighbors(c);
    const bool stalledArrival=c.blocked<2&&arrivalNeighbors;
    if(c.blocked>=2||stalledArrival) {
        const auto& next=u.orders.front();
        const int nx=footprintCell(next.x,u.type->footX)-c.position.x,nz=footprintCell(next.z,u.type->footZ)-c.position.z;
        const int sx=(nx>0)-(nx<0),sz=(nz>0)-(nz<0);
        // A no-progress orbit may stay just short of physical refusal. Look
        // only a footprint stride along its installed leg, never past a turn.
        const int reach=stalledArrival?std::min(std::clamp(std::max(c.footX,c.footZ)+1,2,16),
            std::max(std::abs(nx),std::abs(nz))):1;
        size_t examined=0,buckets=0;
        for(int step=1;step<=reach;++step) {
          // Physical diagonal movement also needs both cardinal side
          // footprints. A clear diagonal endpoint can hide the body that
          // refused the first subcell step, leaving avoidance without a cause.
          const flow::Cell ahead{c.position.x+sx*step,c.position.z+sz*step};
          const std::array<flow::Cell,3> anchors{{ahead,{ahead.x,ahead.z-sz},{ahead.x-sx,ahead.z}}};
          const int anchorCount=sx&&sz?3:1;
          for(int anchor=0;anchor<anchorCount;++anchor) {
            const int x0=anchors[size_t(anchor)].x-c.footX/2,z0=anchors[size_t(anchor)].z-c.footZ/2;
            int blocker=0;
            for(int z=std::max(0,z0);z<std::min(w.occH_,z0+c.footZ);++z)
                for(int x=std::max(0,x0);x<std::min(w.occW_,x0+c.footX);++x) {
                    const int id=w.occ_[size_t(z)*w.occW_+x];
                    if(id&&id!=u.id&&(!blocker||id<blocker))blocker=id;
                }
            // The coarse occupancy grid omits flying types, but authoritative
            // ground collision includes them while landed. Query that same body
            // index for grounded flyers so an idle scout cannot permanently block
            // a ground army without ever becoming a local-avoidance obstacle.
            if(!blocker) {
                if(!w.bodyIndexValid_)w.rebuildBodyIndex();
                const int x1=x0+c.footX,z1=z0+c.footZ;
                const int tx0=std::max(0,x0)/8,tz0=std::max(0,z0)/8;
                const int tx1=(std::min(w.hW_,x1)+7)/8,tz1=(std::min(w.hH_,z1)+7)/8;
                for(int tz=tz0;tz<tz1&&examined<256&&buckets<128;++tz)
                    for(int tx=tx0;tx<tx1&&examined<256&&buckets<128;++tx) {
                        ++buckets;
                        const auto& entries=w.bodyTiles_[size_t(tz)*w.bodyTilesW_+tx];
                        for(size_t i=0;i<entries.size()&&examined<256;++i) {
                            ++examined;
                            const size_t offset=(i+uint32_t(u.id)+c.tick/8)%entries.size();
                            const auto& other=w.units_[size_t(entries[offset])];
                            if(other.id==u.id||!other.alive()||other.embarked()||!other.type||
                               !other.type->canFly||other.flightGroundMode!=1)continue;
                            const int ox=footprintOrigin(other.x,other.type->footX);
                            const int oz=footprintOrigin(other.z,other.type->footZ);
                            if(ox<x1&&x0<ox+other.type->footX&&oz<z1&&z0<oz+other.type->footZ&&
                               (!blocker||other.id<blocker))blocker=other.id;
                        }
                    }
            }
            if(blocker) {
                c.obstruction=c.lookup(blocker);
                c.obstructionFriendly=c.obstruction&&w.allied(u.player,c.obstruction->player);
                break;
            }
          }
          if(c.obstruction)break;
        }
    }
    std::array<flow::Traffic::Neighbor,64> neighbors;size_t count=0,examined=0;
    // Use this destination's actual population. Far-away armies avoid an
    // irrelevant neighbor scan.
    if(arrivalNeighbors) {
        if(!w.bodyIndexValid_)w.rebuildBodyIndex();
        // The body index stamps complete footprints. Querying around our own
        // rectangle therefore sees a touching giant whose center is far away.
        const int x0=std::max(0,c.position.x-c.footX/2-2),z0=std::max(0,c.position.z-c.footZ/2-2);
        const int x1=std::min(w.hW_,c.position.x-c.footX/2+c.footX+2);
        const int z1=std::min(w.hH_,c.position.z-c.footZ/2+c.footZ+2);
        const int tx0=x0/8,tz0=z0/8,tx1=(x1+7)/8,tz1=(z1+7)/8;
        const int cx=c.position.x/8,cz=c.position.z/8;
        const int radius=std::max({cx-tx0,tx1-cx-1,cz-tz0,tz1-cz-1});
        size_t buckets=0;
        for(int ring=0;ring<=radius&&examined<256&&count<neighbors.size()&&buckets<128;++ring)
          for(int oz=-ring;oz<=ring&&examined<256&&count<neighbors.size()&&buckets<128;++oz)
            for(int ox=-ring;ox<=ring&&examined<256&&count<neighbors.size()&&buckets<128;++ox) {
                if(std::max(std::abs(ox),std::abs(oz))!=ring)continue;
                const int x=cx+ox,z=cz+oz;
                if(x<tx0||z<tz0||x>=tx1||z>=tz1)continue;
                ++buckets;
                const auto& bucket=w.bodyTiles_[size_t(z)*w.bodyTilesW_+x];
                for(size_t offset=0;offset<bucket.size()&&examined<256&&count<neighbors.size();++offset) {
                    const size_t index=(offset+uint32_t(u.id)+c.tick/8)%bucket.size();
                    ++examined;const auto& n=w.units_[size_t(bucket[index])];
                    if(n.id==u.id||!n.alive()||n.embarked()||!n.type||n.type->canFly||!trafficIdle(n))continue;
                    bool duplicate=false;
                    for(size_t prior=0;prior<count;++prior)if(neighbors[prior].id==n.id){duplicate=true;break;}
                    if(duplicate)continue;
                    neighbors[count++]={n.id,n.player,{footprintCell(n.x,n.type->footX),footprintCell(n.z,n.type->footZ)},
                        n.type->footX,n.type->footZ,true};
                }
            }
    }
    c.neighbors=std::span(neighbors.data(),count);
    const auto terrainFree=[&](flow::Cell at) {
        // Incomplete static evidence must not invent a narrow corridor. Live
        // movement remains collision-checked while obstacle stamps finish.
        if(!p.structuresComplete||!p.obstacles||!p.obstacles->settled())return true;
        const int x0=at.x-c.footX/2,z0=at.z-c.footZ/2;
        for(int z=z0;z<z0+c.footZ;++z)for(int x=x0;x<x0+c.footX;++x)
            if(!p.sample(u.type,u.player,x,z))return false;
        return true;
    };
    c.terrainFree=std::cref(terrainFree);
    c.free=[&](flow::Cell at){
        const int x0=at.x-u.type->footX/2,z0=at.z-u.type->footZ/2;
        if(x0<0||z0<0||x0+u.type->footX>w.occW_||z0+u.type->footZ>w.occH_)return false;
        for(int z=z0;z<z0+u.type->footZ;++z)for(int x=x0;x<x0+u.type->footX;++x) {
            const int owner=w.occ_[size_t(z)*w.occW_+x];
            if(owner&&owner!=u.id)return false;
        }
        return w.cellScore(u.type,at.x,at.z,u.id)>=4;
    };
    bool arrivalDeferred=false;c.arrivalDeferred=&arrivalDeferred;
    const auto arrivalProof=[&](flow::Cell at,bool contact) {
        arrivalDeferred=false;
        if(!p.arrivalBudget.accepts(u.id)){arrivalDeferred=true;return false;}
        const auto profile=p.profiles.find(p.key(u));
        if(!p.structuresComplete||!p.obstacles||!p.obstacles->settled()) {
            arrivalDeferred=true;return false;
        }
        const auto* profileData=profile==p.profiles.end()?nullptr:&profile->second;
        const auto* topology=profileData&&!profileData->builder?profileData->topology.get():nullptr;
        const auto known=[&](flow::Cell cell) {
            if(cell.x<0||cell.z<0||cell.x>=w.nav_.width()||cell.z>=w.nav_.height())return false;
            if(u.player<0||u.player>=16||w.navigationExplored_.empty())return true;
            const int width=w.hW_/2,height=w.hH_/2;
            return cell.x/2<width&&cell.z/2<height&&
                (w.navigationExplored_[size_t(cell.z/2)*width+cell.x/2]&(1u<<u.player));
        };
        const auto knownFree=[&](flow::Cell cell) {
            if(!p.arrivalBudget.spend(u.id,1)){arrivalDeferred=true;return false;}
            ++p.counters.arrivalCells;
            // Optimistic unknown terrain is navigation guidance, never proof
            // that an arrival slot connects to the commanded destination.
            if(!known(cell))return false;
            const int x0=cell.x-c.footX/2,z0=cell.z-c.footZ/2;
            const int x1=x0+c.footX-1,z1=z0+c.footZ-1;
            if(x0<0||z0<0||x1>=w.nav_.width()||z1>=w.nav_.height())return false;
            if(u.player>=0&&u.player<16&&!w.navigationExplored_.empty()) {
                // A known centre does not certify a large footprint straddling
                // the discovery boundary. Entirely known source tiles take
                // the cheap path; partial tiles check only touched fog cells.
                const uint16_t mask=uint16_t(1u<<u.player);
                const int tilesX=(w.nav_.width()+63)/64;
                bool all=true;
                for(int z=z0/64;z<=z1/64;++z)for(int x=x0/64;x<=x1/64;++x) {
                    const auto& evidence=p.evidence[size_t(z)*tilesX+x];
                    all&=!evidence.dirty&&bool(evidence.all&mask);
                }
                if(!all) {
                    const int width=w.hW_/2,height=w.hH_/2;
                    const size_t cells=size_t(x1/2-x0/2+1)*size_t(z1/2-z0/2+1);
                    if(!p.arrivalBudget.spend(u.id,cells)){arrivalDeferred=true;return false;}
                    p.counters.arrivalCells+=cells;
                    if(x1/2>=width||z1/2>=height)return false;
                    for(int z=z0/2;z<=z1/2;++z)for(int x=x0/2;x<=x1/2;++x)
                        if(!(w.navigationExplored_[size_t(z)*width+x]&mask))return false;
                }
            }
            if(topology) {
                const int tile=topology->tileAt(cell);
                if(tile<0)return false;
                if(!profileData->dirty[size_t(tile)])return bool(topology->cost(cell));
            }
            // Cache eviction or discovery must not strand an already-arrived
            // group. A shared raw-cell quota bounds exact local proof while
            // its footprint snapshot is unavailable; no new field is needed.
            const size_t cells=size_t(c.footX)*c.footZ;
            if(!p.arrivalBudget.spend(u.id,cells-1)){arrivalDeferred=true;return false;}
            p.counters.arrivalCells+=cells-1;
            for(int z=0;z<c.footZ;++z)for(int x=0;x<c.footX;++x) {
                const flow::Cell raw{cell.x-c.footX/2+x,cell.z-c.footZ/2+z};
                if(!known(raw)||!p.sample(u.type,u.player,raw.x,raw.z))return false;
            }
            return true;
        };
        // A settled neighbor supplies the remainder of the connection, but
        // not permission to cross unknown terrain or an unfinished snapshot.
        // Its occupied footprint is valid here: only terrain is being proved.
        if(contact)return flow::directRoute(c.position,at,64,knownFree,
            [&](flow::Cell cell){return cell==at;});
        auto target=arrival.missionTarget.value_or(std::pair{arrival.x,arrival.z});
        if(arrival.guard)if(const auto* guarded=w.unit(arrival.targetId);guarded&&guarded->alive())
            target={guarded->x,guarded->z};
        const flow::GoalRegion region{flow::GoalRegion::Kind::Circle,
            footprintCell(target.first,u.type->footX),footprintCell(target.second,u.type->footZ),0,0,
            retailCircleRadiusSquared(arrival.guard?p.guardReach(u,arrival):
                std::bit_cast<int32_t>(arrival.missionRadius+4u))};
        const flow::Cell center{region.x,region.z};
        // A short approach and straight goal connection, never a global search.
        // The shared 8192-cell quota also bounds huge destination formations.
        // Body checks apply to the approach; arrived peers may occupy the
        // remaining terrain connection between that slot and the goal area.
        return flow::directRoute(c.position,at,64,
            [&](flow::Cell cell){return knownFree(cell)&&c.free(cell);},
            [&](flow::Cell cell){return cell==at;})&&
            flow::directRoute(at,center,512,knownFree,[&](flow::Cell cell){return region.contains(cell);});
    };
    const auto arrivalReachable=[&](flow::Cell at){return arrivalProof(at,false);};
    const auto contactReachable=[&](flow::Cell at){return arrivalProof(at,true);};
    // These callbacks live through update(); reference wrappers avoid a heap
    // allocation per moving unit/tick for captures larger than std::function's
    // small-object storage.
    c.arrivalReachable=std::cref(arrivalReachable);
    c.contactReachable=std::cref(contactReachable);
    if(p.traffic.coordinator&&c.obstructionFriendly&&c.blocked>=2) {
        c.cooperativeOpenTerrain=false;
        const auto profile=p.profiles.find(p.key(u));
        if(p.structuresComplete&&p.obstacles&&p.obstacles->settled()&&profile!=p.profiles.end()) {
            const auto& data=profile->second;
            if(data.topology) {
                const int tile=data.topology->tileAt(c.position);
                // Scope following and moving-peer retries to an open published
                // footprint tile. Dirty/updating include its source footprint
                // halo; no per-mover terrain scan is needed here. Unknown costs
                // remain optimistic, not proof of known ground.
                if(tile>=0&&size_t(tile)<data.openPublished.size()&&size_t(tile)<data.dirty.size()&&
                   !data.dirty[size_t(tile)]&&(data.updating.empty()||
                    (size_t(tile)<data.updating.size()&&!data.updating[size_t(tile)])))
                    c.cooperativeOpenTerrain=data.openPublished[size_t(tile)];
            }
        }
    }
    auto result=p.traffic.update(c);
    result.settledRally=rally&&result.settled;
    return result;
}
void FlowNavigator::cancel(int unit) {
    impl_->cancel(unit);
    if(impl_->traffic.coordinator)impl_->traffic.coordinator->cancel(unit);
}
void FlowNavigator::tick() {impl_->tick();}
bool FlowNavigator::allowFollowerStep(int id,flow::Cell from,flow::Cell to) const {
    return impl_->traffic.coordinator&&impl_->traffic.coordinator->allowFollowerStep(id,from,to);
}
void FlowNavigator::dirty(int x,int z,int w,int h,uint16_t viewers) {
    impl_->markDirty(x,z,w,h,viewers);
}
uint64_t FlowNavigator::checksum() const {
    uint64_t h=1469598103934665603ull;
    const auto mix=[&](uint64_t v){h=(h^v)*1099511628211ull;};
    mix(impl_->traffic.checksum());mix(impl_->clock);mix(impl_->nextProfile);mix(impl_->requestCursor);
    if(impl_->passages)mix(impl_->passages->checksum());
    mix(impl_->service.checksum());mix(impl_->service.publishedHash());mix(impl_->service.work());mix(impl_->counters.snapshotWork);
    mix(impl_->revision);mix(impl_->tileClock);
    for(const auto& [key,tile]:impl_->tiles) {
        std::apply([&](auto... values){(mix(uint64_t(values)),...);},key.first);
        mix(key.second);mix(tile.revision);mix(tile.used);mix(tile.fingerprint);
        if(impl_->passages)mix(tile.open);
    }
    for(const auto& e:impl_->evidence) {mix(e.terrain);mix(e.any);mix(e.all);mix(e.dirty);for(auto value:e.knowledge)mix(value);}
    mix(impl_->nextAdmission);mix(impl_->counters.firingRays);mix(impl_->counters.firingCells);
    mix(impl_->counters.arrivalCells);
    mix(impl_->arrivalBudget.limit);mix(impl_->arrivalBudget.remaining);
    mix(impl_->arrivalBudget.after);mix(impl_->arrivalBudget.last);mix(impl_->arrivalBudget.exhausted);
    for(const auto& [ticket,key]:impl_->admission) {
        mix(ticket);std::apply([&](auto... values){(mix(uint64_t(values)),...);},key);
    }
    mix(impl_->structuresComplete);mix(impl_->counters.localWork);mix(impl_->counters.localDeliveries);
    if(impl_->obstacles)mix(impl_->obstacles->work());
    for(const auto& [key,p]:impl_->profiles) {
        std::apply([&](auto... values){(mix(uint64_t(values)),...);},key);mix(p.id);mix(p.used);mix(p.leaseUntil);mix(p.retryAt);mix(p.failures);mix(p.delivered);mix(p.topology!=nullptr);mix(p.topologyHash);mix(p.builder!=nullptr);
        if(p.builder){mix(p.preparingHash);mix(p.builder->checksum());}
        for(bool dirty:p.dirty)mix(dirty);
        if(impl_->passages){mix(p.updating.size());for(bool updating:p.updating)mix(updating);
            mix(p.openPublished.size());mix(p.openPublishedHash);mix(p.openPreparing.size());mix(p.openPreparingHash);}
    }
    for(const auto& [id,r]:impl_->requests) {
        std::apply([&](auto... values){(mix(uint64_t(values)),...);},r.key);
        mix(r.targetId);mix(r.firingState);mix(r.firingCount);mix(r.firingCursor);mix(r.firingDone);mix(r.firingContinuation);
        if(impl_->passages)mix(r.passagePending);
        for(size_t n=0;n<r.firingCount;++n){mix(r.firingSeeds[n].x);mix(r.firingSeeds[n].z);}
        mix(id);mix(r.x.v);mix(r.z.v);mix(r.controller);mix(r.player);mix(r.bound);mix(r.issued);mix(r.retryLocal);
        mix(uint8_t(r.region.kind));mix(r.region.x);mix(r.region.z);mix(r.region.maxX);mix(r.region.maxZ);
        mix(r.region.outerSquared);mix(r.region.innerWorldSquared);
    }
    for(const auto& [id,job]:impl_->local) {mix(id);mix(job->done());mix(job->start().x);mix(job->start().z);}
    return h;
}
FlowNavigator::Stats FlowNavigator::stats() const {
    auto stats=impl_->counters;stats.profiles=impl_->profiles.size();stats.pending=impl_->requests.size();
    if(impl_->traffic.coordinator) {
        const auto value=impl_->traffic.coordinator->stats();
        stats.cooperativeProbes=value.probes;stats.cooperativeSearches=value.searches;
        stats.cooperativeRoutes=value.routes;stats.cooperativeWaits=value.waits;
        stats.cooperativeConflicts=value.conflicts;stats.cooperativeRecords=value.records;
        stats.cooperativeReservations=value.reservations;stats.cooperativeBytes=value.bytes;
        stats.cooperativeCompleteFailures=value.completeFailures;stats.cooperativeDeferredSearches=value.deferredSearches;
        stats.cooperativeRetrySkips=value.retrySkips;
    }
    if(impl_->passages) {
        const auto value=impl_->passages->stats();
        stats.cooperativePassageProbes=value.probes;stats.cooperativePassageHits=value.hits;
        stats.cooperativePassages=value.entries;
    }
    stats.cachedTiles=impl_->tiles.size();
    std::vector<uint64_t> ages;ages.reserve(impl_->requests.size());
    for(const auto& [id,r]:impl_->requests){(void)id;ages.push_back(impl_->clock-r.issued);}
    if(!ages.empty()) {std::sort(ages.begin(),ages.end());stats.pendingAgeMax=ages.back();stats.pendingAgeP95=ages[(ages.size()-1)*95/100];}
    stats.fields=impl_->service.fields();stats.bytes=sizeof(Impl)+impl_->service.bytes()+impl_->traffic.bytes();
    if(impl_->passages)stats.bytes+=impl_->passages->bytes();
    stats.bytes+=impl_->requests.size()*(sizeof(Impl::Request)+64)+
        impl_->profiles.size()*(sizeof(Impl::Key)+sizeof(Impl::Profile)+64)+
        impl_->subscribers.size()*(sizeof(Impl::Key)+sizeof(size_t)+64)+
        (impl_->waiting.size()+impl_->admission.size())*(sizeof(Impl::Key)+sizeof(uint64_t)+64);
    stats.bytes+=impl_->evidence.capacity()*sizeof(Impl::Evidence)+impl_->tiles.size()*(flow::MemoryPlan::tileBytes+256);
    if(impl_->unknownTile)stats.bytes+=flow::MemoryPlan::tileBytes;
    if(impl_->obstacles)stats.bytes+=impl_->obstacles->bytes()+impl_->structures.size()*(sizeof(Impl::Structure)+64);
    for(const auto& [key,p]:impl_->profiles) {(void)key;if(p.topology)stats.bytes+=p.topology->bytes();if(p.builder)stats.bytes+=p.builder->bytes();stats.bytes+=p.preparing.capacity()*sizeof(Impl::Profile::TileStamp)+p.tileHashes.capacity()*sizeof(uint64_t)+(p.dirty.capacity()+7)/8+(p.updating.capacity()+7)/8;
        if(impl_->passages){stats.cooperativeClearanceEntries+=p.openPublished.size();stats.cooperativeClearanceBytes+=(p.openPublished.capacity()+7)/8+(p.openPreparing.capacity()+7)/8;}
    }
    stats.bytes+=stats.cooperativeClearanceBytes;
    for(const auto& [id,job]:impl_->local) {(void)id;stats.bytes+=job->bytes()+64;}
    return stats;
}
}
