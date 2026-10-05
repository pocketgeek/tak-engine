#include "cooperative_test_common.h"
#include "sim/flownavigator.h"

using namespace cooperative_test;
namespace tak::sim {
struct RetailReplayProbe {
    static void rebuildBodies(World& world) {
        world.bodyIndexValid_=false;world.rebuildGrid();world.rebuildOccupancy();
    }
    static flow::Traffic::Result traffic(World& world,int id) {
        return world.flow_->traffic(*world.unit(id));
    }
    static void dirty(World& world,int x,int z) {world.flow_->dirty(x,z,1,1);}
};
}
namespace {
struct Fixture {
    UnitType type=mover();
    World world;
    std::vector<int> ids;
    explicit Fixture(int count=1,bool clutter=false,int startX=512) {
        setup(world,PathfindingMode::Cooperative);
        if(clutter) {
            std::vector<uint16_t> features(256*256,0xffff);features[80*256+80]=0;
            world.blockCells(80,80,1,1,true);
            world.setMapPlacementFeatures(features,{{"follower-scope-wall",1,1,true,true,false,0}});
        }
        type.maxVel=Fixed::raw(1);
        for(int i=0;i<count;++i) {
            const int id=world.spawn(&type,float(startX+32*i),1024,std::nullopt,0);ids.push_back(id);
            world.order(id,3008,1024,false);world.order(id,3008,1600,true);world.setSquad(id,-1);
        }
        known(world);
        for(int tick=0;tick<300&&(world.flowStats().deliveries<size_t(count)||world.flowStats().pending);++tick)
            world.tick(1.f/30);
        require(world.flowStats().deliveries>=size_t(count)&&!world.flowStats().pending,
            "follower fixture did not register a current terrain controller");
        type.maxVel=Fixed::raw(117964);
        for(size_t i=0;i<ids.size();++i) {
            auto& u=*world.unit(ids[i]);
            const auto active=u.orders[World::currentLeg(u.orders)],queued=u.orders.back();
            require(active.controller!=queued.controller,"fixture lost the queued command");
            u.orders={active,queued};u.x=Fixed::fromFloat(float(startX)+7.75f+32*float(i));u.z=Fixed::fromInt(1024);
            auto& first=u.orders.front();first.hasSegment=true;first.segmentX=u.x;first.segmentZ=u.z;
            u.heading=Bam(16384);u.turnReqBam=0;u.speed=u.baseSpeed=type.maxVel;
            u.groundMovementMode=0;u.groundSpeedMode=0;u.groundPitch=u.groundRoll=0;u.bodyBlockStreak=0;
        }
        RetailReplayProbe::rebuildBodies(world);legal(world,ids);
    }
};
void actualVacancyAndMissionOwnership() {
    Fixture f(6);
    struct Before {Fixed x,z;uint64_t active,queued;size_t orders;};
    std::vector<Before> before;
    for(int id:f.ids) {const auto& u=*f.world.unit(id);before.push_back({u.x,u.z,u.orders.front().controller,u.orders.back().controller,u.orders.size()});}
    for(size_t i=0;i+1<f.ids.size();++i) {
        auto& u=*f.world.unit(f.ids[i]);
        f.world.followGroundLeader(u,f.type.maxVel);
        require(u.x==before[i].x&&u.z==before[i].z,"saving a conditional displacement moved before its leader");
    }
    auto& leader=*f.world.unit(f.ids.back());f.world.commitGroundStep(leader,Fixed::fromInt(2),Fixed());
    const auto counts=f.world.cooperativeMovementStats();f.world.flushGroundFollowers();
    const auto after=f.world.cooperativeMovementStats();
    require(after.moved-counts.moved==f.ids.size()-1,"real leader vacancy did not propagate through the packed line");
    require(after.attempted-counts.attempted==f.ids.size()-1,"packed followers performed more than one retry");
    for(size_t i=0;i<f.ids.size();++i) {
        const auto& u=*f.world.unit(f.ids[i]);
        require(u.x>before[i].x&&u.x-before[i].x<=Fixed::fromInt(2)&&u.z==before[i].z,
            "follower exceeded its one saved native displacement");
        require(u.orders.size()==before[i].orders&&u.orders.front().controller==before[i].active&&
            u.orders.back().controller==before[i].queued&&u.orders.back().z==Fixed::fromInt(1600)&&u.squad==-1,
            "dependency movement changed active/queued command ownership or formation");
    }
    legal(f.world,f.ids);
    const auto x=f.world.unit(f.ids.front())->x;f.world.flushGroundFollowers();
    require(f.world.unit(f.ids.front())->x==x,"a second flush repeated the saved movement");
    std::printf("FOLLOWERS moved=%llu probes=%llu hash=%016llx\n",
        (unsigned long long)(after.moved-counts.moved),(unsigned long long)(after.probes-counts.probes),
        (unsigned long long)f.world.stateHash());
}
enum class Change {Stop,Order,Death,Freeze,Capture,Position};
void invalidation(Change change) {
    Fixture f;auto& u=*f.world.unit(f.ids.front());
    const auto before=f.world.cooperativeMovementStats();
    f.world.followGroundLeader(u,f.type.maxVel);
    switch(change) {
    case Change::Stop:f.world.stop(u.id);break;
    case Change::Order:f.world.order(u.id,1024,2048,false);break;
    case Change::Death:u.hp=Fixed();break;
    case Change::Freeze:u.frozenFor=90;break;
    case Change::Capture:u.player=1;break;
    case Change::Position:u.x-=Fixed::fromInt(1);RetailReplayProbe::rebuildBodies(f.world);break;
    }
    const auto x=u.x,z=u.z;f.world.flushGroundFollowers();
    const auto after=f.world.cooperativeMovementStats();
    require(u.x==x&&u.z==z&&after.moved==before.moved&&after.invalid==before.invalid+1,
        "late lifecycle/ownership change did not invalidate the saved step");
    f.world.flushGroundFollowers();
    require(f.world.cooperativeMovementStats().invalid==after.invalid,"invalid attempt survived flush");
}
void stationaryLeaderDoesNotPromiseSpace() {
    Fixture f(2);auto& follower=*f.world.unit(f.ids.front());
    const auto x=follower.x,z=follower.z;const auto before=f.world.cooperativeMovementStats();
    f.world.followGroundLeader(follower,f.type.maxVel);f.world.flushGroundFollowers();
    require(follower.x==x&&follower.z==z&&f.world.cooperativeMovementStats().moved==before.moved,
        "a stationary leader was treated as a future vacancy");
    legal(f.world,f.ids);
}
void followerEligibilityUsesPublishedOpenTile() {
    // An interior tile avoids the blocked footprint anchors at the map edge.
    // The optional distant feature is in this tile, outside the actual queue.
    for(bool clutter:{false,true}) {
        Fixture f(2,clutter,1024);auto& u=*f.world.unit(f.ids.front());u.bodyBlockStreak=2;
        auto result=RetailReplayProbe::traffic(f.world,u.id);
        require(result.wait&&!result.detour&&!result.settled,
            "follower scope fixture did not reach ordinary same-way queue admission");
        require((result.followLeader!=0)==!clutter,
            "conditional follower eligibility ignored its published open tile");
        if(!clutter) {
            // A raw edit just beyond the anchor tile's negative edge dirties this
            // footprint tile through its halo even before publication starts.
            RetailReplayProbe::dirty(f.world,63,80);
            result=RetailReplayProbe::traffic(f.world,u.id);
            require(result.wait&&!result.followLeader,
                "dirty footprint-halo evidence retained conditional following");
        }
    }
}
void oversizedLegacyFootprintWaits() {
    auto type=mover();type.footX=type.footZ=16;type.maxVel=Fixed::raw(1);
    World world;world.setVisPlayer(-1);world.setSerialThreads(true);world.setPathService(true);
    world.setPathfindingMode(PathfindingMode::Cooperative);
    // Deliberately omit the placement plane. This harness can only prove
    // footprints up to15 cells, so the16-cell body must not enter the batch.
    world.setTerrain(std::vector<uint8_t>(256*256,100),256,256,64);
    const int id=world.spawn(&type,512,1024,std::nullopt,0);
    known(world);world.order(id,3008,1024,false);
    for(int tick=0;tick<2000&&(world.flowStats().deliveries==0||world.flowStats().pending);++tick)
        world.tick(1.f/30);
    require(world.flowStats().deliveries>0&&!world.flowStats().pending,
        "oversized legacy fixture did not register its terrain controller");
    require(world.mapPlacementCells().empty(),"legacy footprint fixture unexpectedly has a placement plane");
    auto& u=*world.unit(id);type.maxVel=Fixed::raw(117964);
    u.heading=Bam(16384);u.turnReqBam=0;u.speed=u.baseSpeed=type.maxVel;
    const auto x=u.x,z=u.z;const auto before=world.cooperativeMovementStats();
    world.followGroundLeader(u,type.maxVel);world.flushGroundFollowers();
    const auto after=world.cooperativeMovementStats();
    require(u.x==x&&u.z==z,"an unsupported legacy footprint moved through a truncated proof");
    require(after.queued==before.queued&&after.attempted==before.attempted&&after.moved==before.moved,
        "an unsupported legacy footprint queued a follower attempt");
}
}
int main() {try {
    actualVacancyAndMissionOwnership();stationaryLeaderDoesNotPromiseSpace();
    followerEligibilityUsesPublishedOpenTile();
    for(auto change:{Change::Stop,Change::Order,Change::Death,Change::Freeze,Change::Capture,Change::Position})invalidation(change);
    oversizedLegacyFootprintWaits();
    std::puts("PASS Cooperative follower vacancy propagation, legal footprints, mission ownership and lifecycle");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}}
