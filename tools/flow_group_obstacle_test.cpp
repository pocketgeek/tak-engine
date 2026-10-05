// Fast-turning synthetic groups can hide the small detour loops seen when
// ordinary ground units meet a convoy or a crowd outside a production site.
#include "sim/sim.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace tak::sim;
namespace {
constexpr int kCount=32,kGoalX=512,kGoalZ=2048;
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
UnitType walker(int kind) {
    UnitType type;
    type.id=type.name="obstacle-walker-"+std::to_string(kind);
    type.canMove=true;type.maxHp=100;type.footX=type.footZ=2;type.sight=5000;
    // Hunter, Troll and Goblin movement rates, without models, scripts or assets.
    // Their authored acceleration and turn rates expose overshoot that the
    // existing group fixture's 10,000 BAM/tick movers avoid.
    type.maxVel=Fixed::raw(std::array{117964,65536,91750}[kind]);
    type.accel=type.brake=Fixed::fromInt(10);
    type.turnRate=type.turnInPlaceRate=std::array{2500,2200,2400}[kind];
    type.halfCellTicks=std::array{3,6,4}[kind];
    return type;
}
void legal(const World& world,const std::vector<int>& ids) {
    for(int id:ids) {
        const auto& unit=*world.unit(id);
        check(world.mobilePlacement(unit,footprintOrigin(unit.x,unit.type->footX),
            footprintOrigin(unit.z,unit.type->footZ),false),
            "group crossed a blocked footprint or overlapped another unit");
    }
}
void diagonalSideBlocker() {
    World world;world.setVisPlayer(-1);
    world.setPathService(true);world.setPathfindingMode(PathfindingMode::Flowfield);
    world.setTerrain(std::vector<uint8_t>(128*128,100),128,128,64);
    world.setMapPlacementFeatures(std::vector<uint16_t>(128*128,0xffff),{});
    auto type=walker(0),neighbor=type;neighbor.id="diagonal-neighbor";
    const int id=world.spawn(&type,1000,1000,std::nullopt,0);
    world.spawn(&neighbor,1017.57f,995.535f,std::nullopt,0);
    auto& unit=*world.unit(id);
    unit.x=Fixed::raw(Fixed::fromInt(1000).v-1);
    unit.z=Fixed::raw(Fixed::fromInt(1000).v+1);
    // An already-installed diagonal leg can refuse its first cardinal cell
    // transition while its diagonal endpoint remains clear. Probe the real
    // adapter before a new field request can replace that retained leg.
    FlowNavigator navigator(world);
    Order order;order.x=Fixed::fromInt(1296);order.z=Fixed::fromInt(1664);
    order.goal=true;order.controller=1;
    bool avoided=false;
    for(int tick=0;tick<16&&!avoided;++tick) {
        unit.orders.clear();unit.speed={};world.tick(1.f/30);
        const int x=footprintCell(unit.x,2),z=footprintCell(unit.z,2);
        check(world.mobilePlacement(unit,x,z,false),"diagonal endpoint fixture is occupied");
        check(!world.mobilePlacement(unit,x,z-1,false),"diagonal side fixture has no blocker");
        unit.orders.push_back(order);unit.bodyBlockStreak=2;navigator.tick();
        const auto result=navigator.traffic(unit);
        if(result.detour) {
            check(world.mobilePlacement(unit,result.detour->x-1,result.detour->z-1,false),
                "diagonal side avoidance chose an occupied footprint");
            avoided=true;
        }
    }
    check(avoided,"a ground body on a diagonal side never reached avoidance");
}
std::vector<uint64_t> run(bool obstacles,bool serial) {
    World world;world.setVisPlayer(-1);world.setSerialThreads(serial);
    world.setPathService(true);world.setPathfindingMode(PathfindingMode::Flowfield);
    world.setTerrain(std::vector<uint8_t>(256*256,100),256,256,64);
    world.setMapPlacementFeatures(std::vector<uint16_t>(256*256,0xffff),{});
    std::array<UnitType,3> types{walker(0),walker(1),walker(2)};
    auto wall=types[0];wall.id="obstacle-wall";wall.maxVel={};wall.footX=4;wall.footZ=64;
    std::vector<int> parked,ids;
    if(obstacles) {
        // A real structure participates in both route planning and physical
        // placement; an artificial nav-only block cannot test their agreement.
        for(int z=0;z<5;++z)for(int x=0;x<4;++x)
            parked.push_back(world.spawn(&types[1],float(1808+x*32),float(1936+z*32),std::nullopt,0));
        world.spawn(&wall,1184,2048,std::nullopt,0);
    }
    for(int i=0;i<kCount;++i) {
        const int id=world.spawn(&types[i%3],float(2192+i%4*48),
            float(1840+i/4*48),std::nullopt,0);
        ids.push_back(id);world.unit(id)->heading=Bam(49152);
        world.order(id,kGoalX,kGoalZ,false);
    }
    legal(world,ids);legal(world,parked);
    std::vector<std::pair<Fixed,Fixed>> parkedAt;
    for(int id:parked)parkedAt.emplace_back(world.unit(id)->x,world.unit(id)->z);
    const int limit=obstacles?10000:4000;
    std::vector<uint64_t> hashes;
    int done=0;
    for(int tick=1;tick<=limit;++tick) {
        world.tick(1.f/30);
        done=int(std::count_if(ids.begin(),ids.end(),[&](int id){return world.unit(id)->orders.empty();}));
        if(tick%30==0||done==kCount) {legal(world,ids);legal(world,parked);}
        if(tick%300==0||done==kCount)hashes.push_back(world.stateHash());
        if(done==kCount)break;
    }
    int crossed=0;
    for(int id:ids)crossed+=world.unit(id)->x.floorInt()<1100;
    std::printf("obstacle group obstacles=%d serial=%d tick=%u arrived=%d/%d crossed=%d requests=%llu\n",
        obstacles,serial,world.tickCount(),done,kCount,crossed,
        (unsigned long long)world.flowStats().requests);
    if(done!=kCount)for(int id:ids) {
        const auto& unit=*world.unit(id);
        if(!unit.orders.empty())std::printf("  active id=%d at=%.1f,%.1f orders=%zu\n",
            id,unit.x.toFloat(),unit.z.toFloat(),unit.orders.size());
    }
    // The open convoy's slowest member needs under 1,900 ticks in free travel.
    // More than twice that allowance must not disappear into repeated sidesteps.
    check(done==kCount,"a group member kept circling instead of finishing its move");
    check(crossed==kCount,"an order completed on the starting side of the obstacle");
    for(int id:ids) {
        const auto& unit=*world.unit(id);
        const int64_t dx=unit.x.floorInt()-kGoalX,dz=unit.z.floorInt()-kGoalZ;
        check(dx*dx+dz*dz<=352*352,"a group member stopped outside the shared destination");
    }
    for(size_t i=0;i<parked.size();++i) {
        const auto& unit=*world.unit(parked[i]);
        check(std::pair{unit.x,unit.z}==parkedAt[i],"passing group displaced an idle production neighbor");
    }
    for(int tick=0;tick<30;++tick)world.tick(1.f/30);
    std::vector<std::pair<Fixed,Fixed>> resting;
    for(int id:ids)resting.emplace_back(world.unit(id)->x,world.unit(id)->z);
    for(int tick=0;tick<120;++tick) {
        world.tick(1.f/30);
        for(size_t i=0;i<ids.size();++i) {
            const auto& unit=*world.unit(ids[i]);
            check(unit.orders.empty()&&std::pair{unit.x,unit.z}==resting[i],
                "an arrived group member resumed moving without a command");
        }
    }
    legal(world,ids);hashes.push_back(world.stateHash());return hashes;
}
}
int main() {
    try {
        diagonalSideBlocker();
        for(bool obstacles:{false,true}) {
            const auto serial=run(obstacles,true),parallel=run(obstacles,false);
            check(serial==parallel,"group obstacle checkpoints differ between worker modes");
        }
        std::puts("PASS authored ground movement, convoy detours, obstacle crossing and sustained rest");return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL %s\n",error.what());return 1;
    }
}
