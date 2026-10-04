#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include <cstdio>
#include <stdexcept>
namespace tak::sim {
struct RetailReplayProbe {
    static bool flowRouteBlocked(World& w,const Unit& u) {return w.flow_->routeBlocked(u);}
};
}
using namespace tak;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void setup(sim::World& world,bool serial) {
    world.setVisPlayer(-1);world.setSerialThreads(serial);world.setPathService(true);
    world.setPathfindingMode(sim::PathfindingMode::Flowfield);
    world.setTerrain(std::vector<uint8_t>(128*96,100),128,96,64);
    world.blockCells(63,0,2,65,true);
    world.player(0).mana=1000000;
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        auto vfs=hpi::mountRetailRoot(argv[1],hpi::OverridePolicy::None);
        sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,false);
        for(int kind=0;kind<8;++kind) {
            auto mover=*registry.find(kind==2?"araking":kind==4?"cretort":(kind==3||kind==6||kind==7)?"araarch":"arasword");mover.sight=4096;
            if(kind==3||kind==6) {mover.weapon.minRange=96;for(auto& weapon:mover.weapons)weapon.minRange=96;}
            if(kind==6) {
                auto close=mover.weapons.front();close.minRange=0;close.range=128;
                mover.weapons.push_back(close);mover.weaponSwitching=false;
            }
            auto target=*registry.find("arasword");target.id="flow_action_target";
            if(kind!=5&&kind!=7) {target.maxVel={};target.canMove=false;}target.weapon.damage=0;target.weapons.clear();
            target.healTime=0;target.maxHp=10000;target.buildTime=10;target.buildCost=10;
            sim::World serial,parallel;
            for(auto* w:{&serial,&parallel}) {
                setup(*w,w==&serial);
                w->spawn(&mover,160,160,std::nullopt,0);
                w->spawn(&target,kind==4?208:kind==6?224:1776,(kind==4||kind==6)?160:192,std::nullopt,(kind==1||kind==3||kind==4||kind==6||kind==7)?1:0);
                if(kind==0||kind==5)w->guard(1,2,false);
                else if(kind==1||kind==3||kind==4||kind==6||kind==7)w->attack(1,2,false);
                else {w->unit(2)->hp=sim::Fixed::fromInt(100);w->repair(1,2,false);}
                if(kind==7)w->order(2,1776,1200,false);
            }
            bool achieved=false;
            for(int tick=0;tick<10000;++tick) {
                serial.tick(1.f/30);parallel.tick(1.f/30);
                check(serial.stateHash()==parallel.stateHash(),"action routing differs by worker mode");
                const auto* unit=serial.unit(1);const auto* destination=serial.unit(2);
                const auto dx=unit->x-destination->x,dz=unit->z-destination->z;
                achieved=(kind==0||kind==5) ? (sim::fxLen(dx,dz)<=sim::Fixed::fromInt(70)) :
                    (kind==1||kind==3||kind==4||kind==6||kind==7) ? (destination->hp<sim::Fixed::fromInt(10000)) :
                    (destination->hp>sim::Fixed::fromInt(100));
                if(achieved)break;
            }
            const auto stats=serial.flowStats();const auto* u=serial.unit(1);
            std::printf("flow action kind=%d tick=%u achieved=%d position=%d,%d deliveries=%llu failures=%llu pending=%zu\n",
                kind,serial.tickCount(),achieved,u->x.floorInt(),u->z.floorInt(),
                (unsigned long long)stats.deliveries,(unsigned long long)stats.failures,stats.pending);
            check(achieved,"guard/attack/repair did not negotiate terrain wall");
            if(kind<=2)check(stats.deliveries<20,"interaction approach repeatedly replanned near its occupied target");
            if(kind==6)check(stats.requests==0,"multiweapon attacker retreated despite usable close weapon");
            else check(stats.deliveries>0,"mission did not consume flow navigation");
            if(kind==4)check(sim::fxLen(serial.unit(1)->x-serial.unit(2)->x,
                serial.unit(1)->z-serial.unit(2)->z)>=sim::Fixed::fromInt(mover.weapons.front().minRange),
                "minimum-range weapon did not retreat into its firing band");
            if(kind==5) {
                for(auto* w:{&serial,&parallel})w->order(2,1776,1200,false);
                bool followed=false;
                for(int tick=0;tick<10000;++tick) {
                    serial.tick(1.f/30);parallel.tick(1.f/30);
                    check(serial.stateHash()==parallel.stateHash(),"moving escort differs by worker mode");
                    followed=serial.unit(2)->z.floorInt()>1150&&
                        sim::fxLen(serial.unit(1)->x-serial.unit(2)->x,
                            serial.unit(1)->z-serial.unit(2)->z)<=sim::Fixed::fromInt(70);
                    if(followed)break;
                }
                check(followed,"guard did not follow its moving target");
            }
        }
        // Being inside weapon range does not mean the current side of a
        // wall is a firing position. Also exercise target acquisition from a
        // fight order: its installed route must survive the combat controller.
        for(const char* name:{"araarch","arasword"})for(int command=0;command<3;++command) {
            auto attacker=*registry.find(name);attacker.sight=4096;
            auto target=*registry.find("arasword");target.maxHp=20000;
            target.healTime=0;target.weapons.clear();target.weapon.damage=0;
            if(command==2){target.maxVel={};target.canMove=false;}
            sim::World serial,parallel;
            for(auto* w:{&serial,&parallel}) {
                setup(*w,w==&serial);
                w->spawn(&attacker,800,800,std::nullopt,0);
                w->spawn(&target,1100,800,std::nullopt,1);
                if(command)w->attackMove(1,command==2?1100:1400,800,false);
                else w->attack(1,2,false);
            }
            bool hit=false;
            for(int tick=0;tick<10000;++tick) {
                const auto before=serial.flowStats();
                serial.tick(1.f/30);parallel.tick(1.f/30);
                check(serial.stateHash()==parallel.stateHash(),"wall combat differs by worker mode");
                check(serial.flowStats().firingRays-before.firingRays<=64&&
                    serial.flowStats().firingCells-before.firingCells<=8192,
                    "combat firing-position search exceeded its per-tick budget");
                check(serial.unit(1)&&serial.unit(2),"wall combat fixture unexpectedly lost a unit");
                if(serial.unit(2)->hp<sim::Fixed::fromInt(20000)){hit=true;break;}
            }
            const auto stats=serial.flowStats();
            std::printf("flow wall combat: %s fight=%d tick=%u hit=%d requests=%llu deliveries=%llu\n",
                name,command,serial.tickCount(),hit,(unsigned long long)stats.requests,
                (unsigned long long)stats.deliveries);
            check(hit,"combat approach remained on blocked side of wall");
            check(stats.deliveries>0,"wall combat bypassed flow navigation");
            check(stats.requests<64,"combat controller repeatedly replaced the installed route");
        }
        {
            auto attacker=*registry.find("araarch");attacker.sight=4096;
            auto target=*registry.find("arasword");target.maxHp=20000;
            target.weapons.clear();target.weapon.damage=0;
            sim::World world;setup(world,true);
            world.spawn(&attacker,800,800,std::nullopt,0);
            world.spawn(&target,1100,800,std::nullopt,1);
            world.unit(1)->fireState=0;world.attack(1,2,false);
            for(int tick=0;tick<2000&&!world.flowStats().firingRays;++tick)world.tick(1.f/30);
            check(world.flowStats().firingRays>0&&world.flowStats().pending>0,
                "LOS cancellation fixture never entered pending firing-position search");
            world.stop(1);
            for(int tick=0;tick<120;++tick) {
                world.tick(1.f/30);
                check(world.unit(1)->orders.empty()&&world.flowStats().pending==0,
                    "cancelled firing-position search restored a combat order");
            }
        }
        {
            // A disconnected fight destination must not repeatedly submit a
            // search every tick, or expand its arrival region back to its start.
            auto type=*registry.find("arasword");type.sight=4096;
            sim::World serial,parallel;
            for(auto* w:{&serial,&parallel}) {
                setup(*w,w==&serial);w->blockCells(63,0,2,96,true);
                w->spawn(&type,160,800,std::nullopt,0);w->attackMove(1,1776,800,false);
            }
            for(int tick=0;tick<1800;++tick) {
                serial.tick(1.f/30);parallel.tick(1.f/30);
                check(serial.stateHash()==parallel.stateHash(),"unreachable fight differs by worker mode");
                const auto* unit=serial.unit(1);
                check(unit&&!unit->orders.empty(),"unreachable fight falsely completed near its start");
                const auto& goal=unit->orders.back();
                check(goal.missionTarget&&goal.missionTarget->first==sim::Fixed::fromInt(1776)&&
                    goal.missionTarget->second==sim::Fixed::fromInt(800),"failed fight lost its original destination");
                check(goal.missionRadius<=256,"failed fight expanded its arrival region without bound");
                check(unit->x<sim::Fixed::fromInt(1008),"unreachable fight crossed a solid wall");
            }
            const auto stats=serial.flowStats();
            std::printf("flow unreachable fight: requests=%llu failures=%llu\n",
                (unsigned long long)stats.requests,(unsigned long long)stats.failures);
            check(stats.requests>0&&stats.requests<=64,"unreachable fight lacks bounded search retries");
        }
        {
            auto type=*registry.find("arasword");type.sight=4096;type.weapon.damage=0;type.weapons.clear();
            sim::World serial,parallel;
            for(auto* w:{&serial,&parallel}) {
                w->setVisPlayer(-1);w->setSerialThreads(w==&serial);w->setPathService(true);
                w->setPathfindingMode(sim::PathfindingMode::Flowfield);
                w->setTerrain(std::vector<uint8_t>(128*96,100),128,96,64);
                const int id=w->spawn(&type,160,192,std::nullopt,0);w->order(id,1776,192,false);
            }
            for(int tick=0;tick<200&&!serial.flowStats().deliveries;++tick) {
                serial.tick(1.f/30);parallel.tick(1.f/30);
                check(serial.stateHash()==parallel.stateHash(),"initial compressed route differs by worker mode");
            }
            check(serial.flowStats().deliveries>0&&serial.unit(1)->orders.size()<10,
                "straight route did not install compressed corners");
            // The new wall lies inside the installed long segment; both its
            // start and far endpoint remain legal. Endpoint-only validation
            // cannot see this obstacle while approaching it.
            for(auto* w:{&serial,&parallel})w->blockCells(40,0,2,65,true);
            bool arrived=false;
            for(int tick=0;tick<10000;++tick) {
                serial.tick(1.f/30);parallel.tick(1.f/30);
                check(serial.stateHash()==parallel.stateHash(),"compressed route obstacle repair differs by worker mode");
                arrived=serial.unit(1)->orders.empty()&&serial.unit(1)->x.floorInt()>1700;
                if(arrived)break;
            }
            check(arrived,"new wall inside compressed route prevented eventual arrival");
        }
        {
            auto type=*registry.find("arasword");type.sight=4096;
            sim::World world;world.setVisPlayer(-1);world.setSerialThreads(true);world.setPathService(true);
            world.setPathfindingMode(sim::PathfindingMode::Flowfield);
            world.setTerrain(std::vector<uint8_t>(128*96,100),128,96,64);
            world.blockCells(60,67,1,1,true);
            world.spawn(&type,160,192,std::nullopt,0);world.order(1,1776,192,false);
            for(int tick=0;tick<500&&!world.flowStats().deliveries;++tick)world.tick(1.f/30);
            check(world.flowStats().deliveries>0,"off-segment fixture has no topology");
            auto probe=*world.unit(1);
            probe.x=sim::footprintWaypoint(60,type.footX);probe.z=sim::footprintWaypoint(66,type.footZ);
            auto order=probe.orders.front();order.goal=false;order.hasSegment=true;
            order.segmentX=sim::footprintWaypoint(64,type.footX);order.segmentZ=sim::footprintWaypoint(65,type.footZ);
            order.x=sim::footprintWaypoint(64,type.footX);order.z=sim::footprintWaypoint(70,type.footZ);
            probe.orders={order};probe.bodyBlockStreak=0;
            check(!sim::RetailReplayProbe::flowRouteBlocked(world,probe),"free off-segment mover replanned prematurely");
            probe.bodyBlockStreak=2;
            check(sim::RetailReplayProbe::flowRouteBlocked(world,probe),"blocked off-segment mover did not reanchor");
            probe.orders.front().hasSegment=false;
            check(!sim::RetailReplayProbe::flowRouteBlocked(world,probe),"raw factory exit acquired a fabricated segment");
            probe.orders.front()=order;probe.orders.front().segmentX=probe.x;probe.orders.front().segmentZ=probe.z;
            probe.orders.front().x=sim::footprintWaypoint(58,type.footX);probe.orders.front().z=probe.z;
            check(!sim::RetailReplayProbe::flowRouteBlocked(world,probe),"reanchored mover immediately repeated off-segment repair");
            // A retreat can remain on the infinite line but lie behind the
            // stored origin (or past its end), with a wall in the return gap.
            // Projecting only onto the stored segment overlooks that gap.
            probe.orders.front()=order;
            probe.orders.front().segmentX=probe.x;probe.orders.front().x=probe.x;
            probe.orders.front().segmentZ=sim::footprintWaypoint(70,type.footZ);
            probe.orders.front().z=sim::footprintWaypoint(74,type.footZ);
            check(sim::RetailReplayProbe::flowRouteBlocked(world,probe),"blocked mover behind segment origin did not reanchor");
            std::swap(probe.orders.front().segmentZ,probe.orders.front().z);
            check(sim::RetailReplayProbe::flowRouteBlocked(world,probe),"blocked mover beyond segment end did not reanchor");
            probe.orders.front().hasSegment=false;
            check(!sim::RetailReplayProbe::flowRouteBlocked(world,probe),"raw order beyond segment bounds acquired a fabricated segment");

            // Installed routes outlive their resident profile. A cold movement
            // class must still detect static clipping after repeated refusal,
            // without misclassifying mobile congestion as a terrain edit.
            auto coldType=type;coldType.roadMult=sim::Fixed::raw(type.roadMult.v+1);
            probe.type=&coldType;probe.orders.front()=order;
            probe.bodyBlockStreak=0;
            check(!sim::RetailReplayProbe::flowRouteBlocked(world,probe),"cold profile checked an unblocked mover");
            probe.bodyBlockStreak=2;
            check(sim::RetailReplayProbe::flowRouteBlocked(world,probe),"cold profile hid static clipping of installed route");
            world.blockCells(60,67,1,1,false);
            world.spawn(&type,sim::footprintWaypoint(61,type.footX).toFloat(),
                sim::footprintWaypoint(67,type.footZ).toFloat(),std::nullopt,0);
            check(!sim::RetailReplayProbe::flowRouteBlocked(world,probe),"cold profile treated a mobile blocker as static terrain");

        }
        std::puts("PASS flow guard/attack/repair actions and compressed route invalidation");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
