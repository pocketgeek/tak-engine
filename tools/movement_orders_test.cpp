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
PathfindingMode sharedMode=PathfindingMode::Flowfield;
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
    World w;setup(w,sharedMode,serial);auto type=soldier(),producer=soldier();
    producer.id=producer.name="producer-test";producer.isBuilder=true;producer.workerTime=1000;
    producer.footX=6;producer.footZ=8;if(!mobile)producer.maxVel=Fixed();
    const int id=w.spawn(&producer,600,600,0,0);w.player(0).mana=1e9;
    w.setRepeat(id,&type);if(rally)w.order(id,1600,640,false);
    bool stopped=false,controllerSeen=false;
    std::map<int,std::pair<Fixed,Fixed>> births;
    bool birthplaceClear=true;
    for(int n=0;n<18000;++n) {
        w.tick(1.f/30);
        for(const auto& u:w.units())if(u.id!=id&&!u.underConstruction) {
            if(!births.contains(u.id))for(const auto& order:u.orders)
                if(order.productionExit){births.emplace(u.id,*order.productionExit);break;}
            if(u.orders.empty()) {
                const auto origin=births.find(u.id);
                birthplaceClear&=origin!=births.end()&&
                    (std::abs(footprintOrigin(u.x,type.footX)-footprintOrigin(origin->second.first,type.footX))>=type.footX||
                     std::abs(footprintOrigin(u.z,type.footZ)-footprintOrigin(origin->second.second,type.footZ))>=type.footZ);
            }
        }
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
    if(isSharedPathfinding(sharedMode))
        check(birthplaceClear&&births.size()==size_t(built),"every produced output clears its full birthplace before stopping");
    return w.stateHash();
}
void mobileFightRally() {
    World w;setup(w,sharedMode);auto type=soldier(),builder=soldier();builder.isBuilder=true;
    const int id=w.spawn(&builder,320,640,0,0);w.setRepeat(id,&type);w.attackMove(id,1600,640,false);
    check(w.unit(id)->orders.empty()&&w.unit(id)->rally.size()==1&&w.unit(id)->rally.front().attackMove,
        "infinite mobile producer retains the fight-move rally flag");
}
void exitRally(PathfindingMode mode,bool rally) {
    World w;setup(w,mode);
    auto type=soldier(),producer=soldier();producer.isBuilder=true;producer.workerTime=1000;
    const int id=w.spawn(&producer,600,600,0,0);w.player(0).mana=1e9;
    w.setRepeat(id,&type);if(rally)w.order(id,1600,640,false);
    int child=0;
    for(int tick=0;tick<600&&!child;++tick) {
        w.tick(1.f/30);
        for(const auto& u:w.units())if(u.id!=id&&u.alive()&&!u.underConstruction){child=u.id;break;}
    }
    check(child!=0,"exit/rally fixture produced an output");if(!child)return;
    const bool flexible=isSharedPathfinding(mode);
    check(w.unit(child)->orders[World::currentLeg(w.unit(child)->orders)].productionExit.has_value()==(flexible||mode==PathfindingMode::Legion),
          "only automatic shared-navigation and Legion exits receive birthplace clearance");
    w.stop(id);
    if(flexible) {
        auto* u=w.unit(child);const auto exit=u->orders[World::currentLeg(u->orders)];
        w.deliverSearchRoute(child,{{38,40},{exit.x.floorInt()/16,exit.z.floorInt()/16}},exit.x,exit.z,false,false,false,true);
        check(w.unit(child)->orders[World::currentLeg(w.unit(child)->orders)].productionExit==exit.productionExit,
              "route replacement preserves the production birthplace");
        u=w.unit(child);u->x=exit.productionExit->first;u->z=exit.productionExit->second;u->bodyBlockStreak=2;
        ticks(w,1);
        check(w.unit(child)->orders[World::currentLeg(w.unit(child)->orders)].productionExit.has_value(),
              "blocked output cannot skip its exit while still at its birthplace");
    }
    auto* u=w.unit(child);u->x=Fixed::fromInt(1000);u->z=Fixed::fromInt(900);u->bodyBlockStreak=2;
    ticks(w,5);
    bool rallyActive=false;
    for(const auto& order:w.unit(child)->orders)
        if(order.goal){rallyActive=order.missionTarget==std::pair{Fixed::fromInt(1600),Fixed::fromInt(640)};break;}
    check(rallyActive==(flexible&&rally),"blocked exit hands over to its rally only after shared-navigation birthplace clearance");
    if(flexible&&!rally)check(w.unit(child)->orders.empty(),"blocked automatic parking finishes after clearing its birthplace without a rally");
}
void queuedRally() {
    World w;setup(w,sharedMode);auto type=soldier(),producer=soldier();
    producer.isBuilder=true;producer.workerTime=1000;
    const int id=w.spawn(&producer,600,600,0,0);w.player(0).mana=1e9;
    w.setRepeat(id,&type);w.order(id,1600,640,false);int child=0;
    for(int tick=0;tick<600&&!child;++tick) {
        w.tick(1.f/30);
        for(const auto& u:w.units())if(u.id!=id&&u.alive()&&!u.underConstruction){child=u.id;break;}
    }
    check(child!=0,"queued rally fixture produced an output");if(!child)return;
    w.stop(id);const int anchor=w.spawn(&type,1600,640,0,0);w.order(anchor,1600,640,false);
    ticks(w,10);check(w.unit(anchor)->orders.empty(),"rally anchor finished its original move");
    w.order(child,1600,1400,true);
    auto* u=w.unit(child);u->x=Fixed::fromInt(1640);u->z=Fixed::fromInt(640);u->bodyBlockStreak=2;
    ticks(w,10);bool later=false,earlier=false;
    for(const auto& o:w.unit(child)->orders)if(o.goal) {
        const auto point=o.missionTarget.value_or(std::pair{o.x,o.z});
        later|=point==std::pair{Fixed::fromInt(1600),Fixed::fromInt(1400)};
        earlier|=point==std::pair{Fixed::fromInt(1600),Fixed::fromInt(640)};
    }
    check(later&&!earlier,"filled rally arrival retires its exit/point while preserving the next queued move");
    ticks(w,1600);check(w.unit(child)->orders.empty()&&w.unit(child)->z>Fixed::fromInt(1300),
        "output follows its preserved later rally command");
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
// Legion routes these missions; the other modes leave them to their own
// navigation. Records whether the navigator ever served `kind` for `id`.
void sawLegion(World& w,int id,LegionMission kind,bool& seen) {
    if(auto* legion=w.legionNavigator();legion&&w.unit(id))seen|=legion->mission(*w.unit(id))==kind;
}
UnitType armed() {
    auto type=soldier();
    Weapon weapon;weapon.name="order-test-weapon";weapon.range=120;weapon.damage=100;
    weapon.reload=0.1f;weapon.aimTol=32767;type.weapon=weapon;type.weapons.push_back(weapon);
    return type;
}
// Attack approach around terrain, then a chase of a target that walks away
// behind the same wall: both must close to firing range and kill.
void attackChase(PathfindingMode mode,bool moving) {
    World w;setup(w,mode);auto type=armed(),enemy=soldier();
    if(!moving)enemy.maxVel=Fixed();else enemy.maxVel=Fixed::fromFloat(30.f/30);
    w.nav().block(38,0,3,49,true);
    const int id=w.spawn(&type,320,640,0,0),target=w.spawn(&enemy,900,640,0,1);
    if(moving)w.order(target,900,1500,false);
    w.attack(id,target,false);
    bool legion=false;int killedAt=-1;
    for(int n=0;n<6000&&killedAt<0;++n) {
        w.tick(1.f/30);sawLegion(w,id,LegionMission::Attack,legion);
        const auto* t=w.unit(target);if(!t||!t->alive())killedAt=n;
    }
    std::printf("attack mode=%d moving=%d killedAt=%d legion=%d\n",int(mode),moving,killedAt,legion);
    check(killedAt>=0,moving?"attack chases a target walking away behind terrain and kills it":
                             "attack approach routes around terrain into firing range and kills");
    if(mode==PathfindingMode::Legion)check(legion,"Legion routes the attack approach");
}
// Guard follows its charge around a wall and stays close; the order is kept.
void guardFollow(PathfindingMode mode) {
    World w;setup(w,mode);auto type=soldier();
    w.nav().block(38,0,3,49,true);
    const int charge=w.spawn(&type,320,640,0,0),escort=w.spawn(&type,240,560,0,0);
    w.guard(escort,charge,false);w.order(charge,1280,640,false);
    bool legion=false;
    for(int n=0;n<6000;++n){w.tick(1.f/30);sawLegion(w,escort,LegionMission::Guard,legion);}
    const auto& c=*w.unit(charge);const auto& e=*w.unit(escort);
    const float d=fxLen(c.x-e.x,c.z-e.z).toFloat();
    std::printf("guard mode=%d dist=%.1f orders=%zu legion=%d\n",int(mode),d,e.orders.size(),legion);
    check(c.orders.empty()&&d<=140.f&&!e.orders.empty()&&e.orders.front().guard,
          "guard follows its charge around terrain and stays beside it");
    if(mode==PathfindingMode::Legion)check(legion,"Legion routes the guard escort");
}
// Work approaches around the same wall: a builder repairs a damaged ally,
// then walks to and puts up a site. The work handlers keep reach, start and
// retirement; Legion (in --legion) only routes the approaches.
void workApproach(PathfindingMode mode) {
    World w;setup(w,mode);auto worker=soldier(),ally=soldier();
    worker.isBuilder=true;worker.workerTime=1000;worker.buildDist=48;
    ally.buildTime=1;ally.buildCost=0;ally.maxVel=Fixed();ally.canMove=false;
    UnitType site{};site.id=site.name="work-site";site.maxVel=Fixed();site.footX=site.footZ=4;
    site.maxHp=100;site.buildTime=1;site.buildCost=0;
    // A water wall on the placement plane (on the legacy nav-grid plane
    // Legion holds a body, a plain Move as well, at a blocked wall's end:
    // docs/legion-pathfinding.md "Scope").
    std::vector<uint8_t> heights(size_t(128)*128,100);
    for(int z=0;z<49;++z)for(int x=38;x<41;++x)heights[size_t(z)*128+x]=0;
    w.setTerrain(heights,128,128,40);w.setMapPlacementFeatures(std::vector<uint16_t>(size_t(128)*128,0xffff),{});
    w.player(0).mana=1e9;
    const int builder=w.spawn(&worker,240,560,0,0),hurt=w.spawn(&ally,1100,400,0,0);
    w.unit(hurt)->hp=Fixed::fromInt(10);
    w.repair(builder,hurt,false);w.queueBuild(builder,&site,1100,700,true);
    bool repaired=false,legionRepair=false,legionBuild=false;int built=0;
    for(int n=0;n<9000&&!(built&&!w.unit(built)->underConstruction);++n) {
        w.tick(1.f/30);
        sawLegion(w,builder,LegionMission::Repair,legionRepair);sawLegion(w,builder,LegionMission::Build,legionBuild);
        repaired|=w.unit(hurt)->hp>=Fixed::fromFloat(ally.maxHp);
        for(const auto& u:w.units())if(u.type==&site&&u.alive())built=u.id;
    }
    std::printf("work mode=%d repaired=%d built=%d tick=%u legion=%d/%d\n",int(mode),repaired,built,w.tickCount(),legionRepair,legionBuild);
    if(auto* legion=w.legionNavigator())
        check(!legion->staticLegal(*w.unit(builder),39,20),"Legion plans around the wall (not through it)");
    check(repaired,"builder walks around terrain and repairs its ally");
    check(built&&!w.unit(built)->underConstruction,"builder walks around terrain and finishes its queued site");
    if(mode==PathfindingMode::Legion)check(legionRepair&&legionBuild,"Legion routes the repair and build approaches");
}
// A group patrol: twelve bodies loop between their starts and one shared
// point; every one keeps lapping. (Open ground: patrolLaps covers a single
// patrol around terrain. On this legacy nav-grid world a 12-body column
// jams at the wall's end in Legion for plain queued moves as well.)
uint64_t groupPatrol(PathfindingMode mode,bool serial) {
    World w;setup(w,mode,serial);auto type=soldier();
    std::vector<int> ids;
    for(int i=0;i<12;++i) {const int id=w.spawn(&type,float(240+i%3*48),float(560+i/3*48),0,0);ids.push_back(id);w.patrol(id,1280,640);}
    std::vector<int> laps(ids.size(),0);std::vector<bool> outbound(ids.size(),false);bool legion=false;
    for(int n=0;n<18000;++n) {
        w.tick(1.f/30);
        for(size_t k=0;k<ids.size();++k) {
            const auto& u=*w.unit(ids[k]);sawLegion(w,ids[k],LegionMission::Patrol,legion);
            if(u.x>Fixed::fromInt(1150))outbound[k]=true;
            if(outbound[k]&&u.x<Fixed::fromInt(450)){++laps[k];outbound[k]=false;}
        }
    }
    const int least=*std::min_element(laps.begin(),laps.end());
    std::printf("group patrol mode=%d least laps=%d legion=%d\n",int(mode),least,legion);
    check(least>=2,"every member of a group patrol keeps lapping around terrain");
    if(mode==PathfindingMode::Legion)check(legion,"Legion routes the group patrol");
    return w.stateHash();
}
void mazeProduction(const char* data) {
    auto vfs=tak::hpi::mountRetailRoot(data,tak::hpi::OverridePolicy::None);
    TypeRegistry registry;setupRegistry(registry,vfs,true);
    constexpr auto recipe="~gen1~0800f0a981b8b476a2cb010008000808ffffff67ff034a616e6b204d617a65207631";
    auto generated=tak::mapgen::generate(tak::mapgen::decodeMapId(recipe),vfs);
    auto files=std::make_shared<tak::hpi::Vfs::Files>();(*files)["maps/orders-maze.tnt"]=generated.map.save();vfs.setMapFiles(files);
    for(bool mobile:{false,true})for(bool rally:{false,true}) {
        World w;w.setSerialThreads(true);w.setVisPlayer(-1);
        MatchConfig cfg;cfg.vfs=&vfs;cfg.mapPath="maps/orders-maze.tnt";cfg.loadCrt=false;
        cfg.pathfindingMode=sharedMode;cfg.slots.resize(1);setupMatch(w,registry,cfg);
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
uint64_t trollRally(const TypeRegistry& registry,bool serial,const tak::hpi::Vfs* maze=nullptr,int count=100) {
    World w;w.setSerialThreads(serial);w.setVisPlayer(-1);
    if(maze) {
        MatchConfig cfg;cfg.vfs=maze;cfg.mapPath="maps/orders-troll-maze.tnt";cfg.loadCrt=false;
        cfg.pathfindingMode=sharedMode;cfg.slots.resize(1);setupMatch(w,registry,cfg);
    } else {
        w.setPathfindingMode(sharedMode);
        w.setTerrain(std::vector<uint8_t>(256*256,100),256,256,20);
        w.buildNavClasses(registry);w.setPathService(true);
    }
    auto producer=*registry.find("zonhand"),troll=*registry.find("zontroll");
    // Preserve authored movement, footprint and scripts. Speed up only the
    // economy to expose continued arrivals beyond the earlier 16-unit tests.
    producer.workerTime=1000;producer.storage=10000000;
    troll.buildTime=1;troll.buildCost=0;troll.weapons.clear();troll.weapon.damage=0;
    const int id=w.spawn(&producer,maze?8448:600,maze?24320:600,0,0);
    w.player(0).mana=10000000;w.setRepeat(id,&troll);
    w.order(id,maze?4864:1600,maze?16640:640,false);
    w.updateNavigationExploration();auto& known=const_cast<std::vector<uint16_t>&>(w.navigationExploration());
    std::fill(known.begin(),known.end(),0xffff);
    bool stopped=false,legionExit=false;int built=0,idle=0;
    for(int tick=0;tick<90000;++tick) {
        w.tick(1.f/30);built=idle=0;
        for(const auto& u:w.units())if(u.id!=id&&u.alive()&&!u.underConstruction){++built;idle+=u.orders.empty();
            sawLegion(w,u.id,LegionMission::Exit,legionExit);}
        if(!stopped&&built>=count){w.stop(id);stopped=true;}
        if(stopped&&built==idle)break;
    }
    std::printf("Troll rally maze=%d serial=%d tick=%u built=%d idle=%d hash=%016llx\n",
        maze!=nullptr,serial,w.tickCount(),built,idle,(unsigned long long)w.stateHash());
    for(const auto& u:w.units())if(u.id!=id&&u.alive()&&!u.underConstruction&&!u.orders.empty())
        std::printf("unsettled Troll id=%d at=%.1f,%.1f orders=%zu legion=%d\n",u.id,u.x.toFloat(),u.z.toFloat(),u.orders.size(),
            w.legionNavigator()?int(w.legionNavigator()->mission(u)):-1);
    check(stopped&&built==count&&idle==built,"all Beast Handler Trolls finish their exit and shared rally orders");
    if(sharedMode==PathfindingMode::Legion)check(legionExit,"Legion routes the Trolls' production exits");
    if(!maze)for(const auto& u:w.units())if(u.id!=id&&u.alive()&&!u.underConstruction) {
        const int64_t dx=u.x.floorInt()-1600,dz=u.z.floorInt()-640;
        const int64_t radius=int64_t(count)*48+32;
        check(dx*dx+dz*dz<=radius*radius,"Troll stopped outside its shared rally formation");
    }
    return w.stateHash();
}
// `open`: only the open-ground cohorts (the maze cohort is a plain-Move
// column on the generated maze; see docs/legion-pathfinding.md "Scope").
void trollProduction(const char* data,bool open=false) {
    auto vfs=tak::hpi::mountRetailRoot(data,tak::hpi::OverridePolicy::None);
    for(bool crusades:{false,true}) {
        TypeRegistry registry;setupRegistry(registry,vfs,crusades);
        const auto serial=trollRally(registry,true);
        check(serial==trollRally(registry,false),"authored Troll arrivals match serial and threaded preparation");
        if(crusades) {
            const auto large=trollRally(registry,true,nullptr,256);
            check(large==trollRally(registry,false,nullptr,256),"256-Troll arrivals match serial and threaded preparation");
            constexpr auto recipe="~gen1~0800f0a981b8b476a2cb010008000808ffffff67ff034a616e6b204d617a65207631";
            auto generated=tak::mapgen::generate(tak::mapgen::decodeMapId(recipe),vfs);
            auto files=std::make_shared<tak::hpi::Vfs::Files>();(*files)["maps/orders-troll-maze.tnt"]=generated.map.save();
            if(!open){vfs.setMapFiles(files);trollRally(registry,true,&vfs);}
        }
    }
}
}
int main(int argc,char** argv) {
    bool onlyShared=false;
    if(argc>1&&(std::string(argv[1])=="--cooperative"||std::string(argv[1])=="--flow"||std::string(argv[1])=="--legion")) {
        sharedMode=std::string(argv[1])=="--cooperative"?PathfindingMode::Cooperative:
            std::string(argv[1])=="--legion"?PathfindingMode::Legion:PathfindingMode::Flowfield;
        onlyShared=true;--argc;++argv;
    }
    if((argc==3||(argc==4&&std::string(argv[3])=="open"))&&std::string(argv[1])=="--trolls") {
        try {trollProduction(argv[2],argc==4);return failures?1:0;}
        catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
    }
    if(argc==2) {
        try {mazeProduction(argv[1]);return failures?1:0;}
        catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
    }
    if(argc!=1)return 2;
    const std::vector<PathfindingMode> modes=onlyShared?std::vector{sharedMode}:
        std::vector{PathfindingMode::Retail,PathfindingMode::Flowfield};
    for(auto mode:modes) {
        for(bool patrol:{false,true})for(bool clipped:{false,true})partial(mode,patrol,clipped);
        group(mode,false,true);group(mode,true,true);patrolLaps(mode);
        combatResume(mode,false);combatResume(mode,true);
        // Mission-goal routing (attack, chase, guard, group patrol): the
        // default run covers Retail, --legion covers Legion.
        if(mode==PathfindingMode::Retail||mode==PathfindingMode::Legion) {
            attackChase(mode,false);attackChase(mode,true);guardFollow(mode);workApproach(mode);
            const auto patrolHash=groupPatrol(mode,true);
            check(patrolHash==groupPatrol(mode,false),"group patrol hashes match serial and threaded preparation");
        }
    }
    const auto serial=group(sharedMode,false,true);
    check(serial==group(sharedMode,false,false),"shared-navigation group hashes match serial and threaded preparation");
    for(bool mobile:{false,true})for(bool rally:{false,true}) {
        const auto hash=production(mobile,rally,true);
        check(hash==production(mobile,rally,false),"production hashes match serial and threaded preparation");
    }
    for(auto mode:modes)
        for(bool far:{false,true})exitRally(mode,far);
    if(isSharedPathfinding(sharedMode))queuedRally();
    mobileFightRally();std::printf("movement orders: %d failures\n",failures);return failures?1:0;
}
