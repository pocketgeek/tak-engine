// Point missions must outlive partial routes and produced units must inherit
// real movement controllers, including their occupied-destination handling.
#include "sim/sim.h"
#include "sim/matchsetup.h"
#include "hpi/hpi.h"
#include "tnt/mapgen.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
using namespace tak::sim;
namespace {
int failures=0;
void check(bool ok,const char* message) {
    std::printf("%s %s\n",ok?"PASS":"FAIL",message);failures+=!ok;
}
UnitType soldier() {
    UnitType t{};t.id=t.name="order-test";t.maxVel=Fixed::fromFloat(70.f/30);
    t.turnRate=10000;t.maxHp=100;t.canMove=true;t.footX=t.footZ=2;
    t.sight=4096;t.buildTime=1;return t;
}
void setup(World& w,PathfindingMode mode,bool serial=true) {
    w.setVisPlayer(-1);w.setSerialThreads(serial);w.setPathfindingMode(mode);
    w.setTerrain(std::vector<uint8_t>(128*128,100),128,128,20);w.setPathService(true);
}
void ticks(World& w,int count) {for(int n=0;n<count;++n)w.tick(1.f/30);}
void partial(PathfindingMode mode,bool patrol,bool clipped) {
    World w;setup(w,mode);auto type=soldier();const int id=w.spawn(&type,160,320,0,0);
    if(patrol) {w.patrolTo(id,1600,320,false);w.patrolTo(id,160,320,true);}
    else {w.attackMove(id,1600,320,false);w.attackMove(id,1600,1600,true);}
    w.setPathService(false);
    auto& state=w.unit(id)->orders.front().mission;
    // Exercise the active mission without its stage-zero direct-segment reset.
    state.stage=patrol?3:2;state.waitMask=0x2700;state.deadline=0xffffffffu;
    std::vector<PathCell> route{{10,20},{30,20}};
    if(clipped) {route.clear();for(int x=10;x<80;++x)route.push_back({x,20});route.push_back({100,20});}
    w.deliverSearchRoute(id,route,Fixed::fromInt(1600),Fixed::fromInt(320),!clipped,false,!clipped,false);
    // The injected route's first point is a corner; the final installed point
    // is the controller's prefix endpoint. Stand there to expose false arrival.
    const auto end=w.unit(id)->orders[World::currentLeg(w.unit(id)->orders)];
    auto* u=w.unit(id);u->x=end.x;u->z=end.z;ticks(w,1);
    check(!u->orders.empty()&&u->orders[World::currentLeg(u->orders)].missionTarget==
        std::pair{Fixed::fromInt(1600),Fixed::fromInt(320)},
        patrol?"patrol retains its requested point at a partial/clipped endpoint":"fight-move retains its requested point at a partial/clipped endpoint");
    check(u->orders.size()>=2&&u->orders.back().missionTarget.value_or(std::pair{u->orders.back().x,u->orders.back().z})==
        (patrol?std::pair{Fixed::fromInt(160),Fixed::fromInt(320)}:
                 std::pair{Fixed::fromInt(1600),Fixed::fromInt(1600)}),"partial delivery preserves the next queued point");
}
uint64_t group(PathfindingMode mode,bool fight,bool serial) {
    World w;setup(w,mode,serial);auto type=soldier();std::vector<int> ids;
    for(int i=0;i<24;++i) {
        const int id=w.spawn(&type,float(160+i%4*48),float(400+i/4*48),0,0);ids.push_back(id);
        if(fight)w.attackMove(id,1600,640,false);else w.order(id,1600,640,false);
    }
    ticks(w,18000);int arrived=0;
    for(int id:ids) {const auto& u=*w.unit(id);arrived+=u.orders.empty();
        if(!u.orders.empty())std::printf("group mode=%d fight=%d id=%d at=%.1f,%.1f radius=%u orders=%zu\n",
            int(mode),fight,id,u.x.toFloat(),u.z.toFloat(),u.orders.back().missionRadius,u.orders.size());}
    check(arrived==int(ids.size()),"every member of a converging move/fight-move group finishes");
    return w.stateHash();
}
void patrolLaps(PathfindingMode mode) {
    World w;setup(w,mode);auto type=soldier();
    // A terrain detour must survive several rounds of route installation and
    // patrol rotation; endpoint coordinates must never become the next mission.
    w.nav().block(38,0,3,49,true);
    const int id=w.spawn(&type,320,640,0,0);w.patrol(id,1280,640);
    int laps=0;bool outbound=false;
    for(int n=0;n<18000;++n) {w.tick(1.f/30);const auto& u=*w.unit(id);
        if(u.x>Fixed::fromInt(1200))outbound=true;
        if(outbound&&u.x<Fixed::fromInt(400)){++laps;outbound=false;}
    }
    check(laps>=3&&!w.unit(id)->orders.empty(),"patrol completes repeated outbound/return laps around terrain");
    for(const auto& order:w.unit(id)->orders)if(order.goal)
        check(order.missionTarget==std::pair{Fixed::fromInt(320),Fixed::fromInt(640)}||
              order.missionTarget==std::pair{Fixed::fromInt(1280),Fixed::fromInt(640)},"patrol endpoints stay canonical after repeated laps");
}
uint64_t production(bool mobile,bool rally,bool serial) {
    World w;setup(w,PathfindingMode::Flowfield,serial);auto type=soldier(),producer=soldier();
    producer.id=producer.name="producer-test";producer.isBuilder=true;producer.workerTime=1000;
    producer.footX=6;producer.footZ=8;if(!mobile)producer.maxVel=Fixed();
    const int id=w.spawn(&producer,600,600,0,0);w.player(0).mana=1e9;
    w.setRepeat(id,&type);if(rally)w.order(id,1600,640,false);
    bool stopped=false,controllerSeen=false;
    for(int n=0;n<18000;++n) {
        w.tick(1.f/30);
        for(const auto& u:w.units())if(u.id!=id&&!u.underConstruction&&!u.orders.empty()) {
            const auto& goal=u.orders.back();
            if(!rally||goal.x!=Fixed::fromInt(1600))continue;
            controllerSeen|=goal.groundMission&&goal.controller;
        }
        if(!stopped&&w.units().size()>=25) {w.stop(id);stopped=true;}
    }
    int built=0,idle=0;
    for(const auto& u:w.units())if(u.id!=id&&u.alive()&&!u.underConstruction){++built;idle+=u.orders.empty();
        if(!u.orders.empty())std::printf("production mobile=%d rally=%d id=%d at=%.1f,%.1f orders=%zu\n",mobile,rally,u.id,u.x.toFloat(),u.z.toFloat(),u.orders.size());}
    check(stopped&&built>=24,"infinite queue produced the requested observation cohort");
    check(!rally||controllerSeen,"produced rally points receive a ground controller");
    check(idle==built,"all completed outputs stop after their exit/rally move");
    return w.stateHash();
}
void mobileFightRally() {
    World w;setup(w,PathfindingMode::Flowfield);auto type=soldier(),builder=soldier();builder.isBuilder=true;
    const int id=w.spawn(&builder,320,640,0,0);w.setRepeat(id,&type);w.attackMove(id,1600,640,false);
    check(w.unit(id)->orders.empty()&&w.unit(id)->rally.size()==1&&w.unit(id)->rally.front().attackMove,
        "infinite mobile producer retains the fight-move rally flag");
}
void combatResume(PathfindingMode mode,bool patrol) {
    World w;setup(w,mode);auto type=soldier(),enemy=soldier();enemy.maxVel=Fixed();
    Weapon weapon;weapon.name="order-test-weapon";weapon.range=120;weapon.damage=100;
    weapon.reload=0.1f;weapon.aimTol=32767;type.weapon=weapon;type.weapons.push_back(weapon);
    const int id=w.spawn(&type,320,640,0,0),target=w.spawn(&enemy,600,640,0,1);
    if(patrol)w.patrol(id,1400,640);else w.attackMove(id,1400,640,false);
    bool engaged=false,retained=true,arrived=false;
    for(int n=0;n<3000;++n) {
        w.tick(1.f/30);const auto& u=*w.unit(id);
        if(!u.orders.empty()&&u.orders.front().targetId==target) {
            engaged=true;bool point=false;
            for(const auto& o:u.orders)point|=o.groundMission&&o.missionTarget==
                std::pair{Fixed::fromInt(1400),Fixed::fromInt(640)};
            retained&=point;
        }
        arrived|=u.x>Fixed::fromInt(1300);
    }
    const auto* remaining=w.unit(target);
    check(engaged&&retained&&(!remaining||!remaining->alive())&&arrived,
        "fight-move/patrol engages an enemy and resumes its original destination");
}
void mazeProduction(const char* data) {
    auto vfs=tak::hpi::mountRetailRoot(data,tak::hpi::OverridePolicy::None);
    TypeRegistry registry;setupRegistry(registry,vfs,true);
    constexpr auto recipe="~gen1~0700f0a981b8b476a2cb010008000808ffffff67ff034a616e6b204d617a65207631";
    auto generated=tak::mapgen::generate(tak::mapgen::decodeMapId(recipe),vfs);
    auto files=std::make_shared<tak::hpi::Vfs::Files>();(*files)["maps/orders-maze.tnt"]=generated.map.save();vfs.setMapFiles(files);
    for(bool mobile:{false,true})for(bool rally:{false,true}) {
        World w;w.setSerialThreads(true);w.setVisPlayer(-1);
        MatchConfig cfg;cfg.vfs=&vfs;cfg.mapPath="maps/orders-maze.tnt";cfg.loadCrt=false;
        cfg.pathfindingMode=PathfindingMode::Flowfield;cfg.slots.resize(1);setupMatch(w,registry,cfg);
        auto hunter=*registry.find("zonter");hunter.weapons.clear();hunter.weapon.damage=0;
        // Keep authored Hunter movement/footprints/scripts; accelerate production
        // so this fixture measures exit/rally behavior rather than the economy.
        hunter.buildTime=1;hunter.buildCost=0;
        UnitType producer{};producer.id="maze-producer";producer.footX=6;producer.footZ=8;
        producer.maxHp=10000;producer.isBuilder=true;producer.workerTime=1000;
        if(mobile){producer.maxVel=hunter.maxVel;producer.canMove=true;}
        const int id=w.spawn(&producer,8448,24320,0,0);w.setRepeat(id,&hunter);
        if(rally)w.order(id,4864,16640,false);
        w.updateNavigationExploration();auto& known=const_cast<std::vector<uint16_t>&>(w.navigationExploration());
        std::fill(known.begin(),known.end(),0xffff);
        bool stopped=false;int built=0,idle=0;
        for(int tick=0;tick<120000;++tick) {
            w.tick(1.f/30);
            if(!stopped&&w.units().size()>=17){w.stop(id);stopped=true;}
            built=idle=0;
            for(const auto& u:w.units())if(u.id!=id&&u.alive()&&!u.underConstruction){++built;idle+=u.orders.empty();}
            if(stopped&&built==idle)break;
        }
        std::printf("maze mobile=%d rally=%d tick=%u built=%d idle=%d hash=%016llx\n",
            mobile,rally,w.tickCount(),built,idle,(unsigned long long)w.stateHash());
        check(stopped&&built==16&&idle==built,"all Hunters finish production exit/rally moves on the actual 64x64 maze");
    }
}
}
int main(int argc,char** argv) {
    if(argc==2) {
        try {mazeProduction(argv[1]);return failures?1:0;}
        catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
    }
    if(argc!=1)return 2;
    for(auto mode:{PathfindingMode::Retail,PathfindingMode::Flowfield}) {
        for(bool patrol:{false,true})for(bool clipped:{false,true})partial(mode,patrol,clipped);
        group(mode,false,true);group(mode,true,true);patrolLaps(mode);
        combatResume(mode,false);combatResume(mode,true);
    }
    const auto serial=group(PathfindingMode::Flowfield,false,true);
    check(serial==group(PathfindingMode::Flowfield,false,false),"Flow group hashes match serial and threaded preparation");
    for(bool mobile:{false,true})for(bool rally:{false,true}) {
        const auto hash=production(mobile,rally,true);
        check(hash==production(mobile,rally,false),"production hashes match serial and threaded preparation");
    }
    mobileFightRally();std::printf("movement orders: %d failures\n",failures);return failures?1:0;
}
