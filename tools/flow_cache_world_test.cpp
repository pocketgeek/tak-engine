#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/flowmemory.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
using namespace tak;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void known(sim::World& w,uint16_t mask) {
    w.updateNavigationExploration();
    auto& explored=const_cast<std::vector<uint16_t>&>(w.navigationExploration());
    std::fill(explored.begin(),explored.end(),mask);
}
void ready(sim::World& w,const std::vector<int>& ids,uint64_t deliveries) {
    for(int tick=0;tick<20000;++tick) {
        bool all=true;for(int id:ids)all&=!w.unit(id)->orders.empty()&&w.unit(id)->routeStamp>1;
        if(all&&w.flowStats().deliveries>=deliveries)return;w.tick(1.f/30);
    }
    throw std::runtime_error("cache fixture route preparation timed out");
}
void evict(sim::World& w,std::vector<sim::UnitType>& pressure) {
    const auto before=w.flowStats().profileEvictions;
    std::vector<int> ids;
    for(size_t i=0;i<pressure.size();++i) {
        const int id=w.spawn(&pressure[i],float(2400+(i%4)*64),float(256+(i/4)*64),std::nullopt,0);
        ids.push_back(id);w.order(id,3500,3500,false);
    }
    for(int tick=0;tick<20000&&w.flowStats().profileEvictions<before+2;++tick)w.tick(1.f/30);
    check(w.flowStats().profileEvictions>=before+2,"cache fixture failed to evict warm profiles");
    for(int id:ids)w.stop(id);
}
std::vector<uint64_t> run(const sim::UnitType& sword,bool serial) {
    auto slow=sword;slow.maxVel=sim::Fixed::raw(1);slow.sight=0;
    slow.weapons.clear();slow.weapon.damage=0;
    const size_t capacity=sim::flow::MemoryPlan::forMap(256,256).profiles;
    std::vector<sim::UnitType> pressure(capacity+2,slow);
    for(size_t i=0;i<pressure.size();++i){pressure[i].id="cache_pressure_"+std::to_string(i);pressure[i].roadMult=sim::Fixed::raw(70000+int32_t(i)*1000);}
    sim::World w;w.setSerialThreads(serial);w.setVisPlayer(-1);w.setPathService(true);
    w.setPathfindingMode(sim::PathfindingMode::Flowfield);
    w.setTerrain(std::vector<uint8_t>(256*256,100),256,256,64);w.setPlayerCount(2);
    auto wall=slow;wall.id="cache_wall";wall.maxVel={};wall.canMove=false;
    wall.footX=2;wall.footZ=14;wall.yardMap.assign(28,'o');
    wall.simulationScript.reset();wall.productionScript.reset();
    std::vector<int> wallIds;
    for(int row=0;row<15;++row)wallIds.push_back(w.spawn(&wall,816,float((7+row*14)*16),std::nullopt,0));
    const int owner=w.spawn(&slow,640,640,std::nullopt,0);
    const int other=w.spawn(&slow,640,960,std::nullopt,1);
    known(w,1);
    uint64_t expectedDeliveries=0;
    auto request=[&]{expectedDeliveries=w.flowStats().deliveries+2;for(int id:{owner,other}){w.stop(id);w.unit(id)->routeStamp=-1;w.order(id,2240,id==owner?640.f:960.f,false);}};
    auto inspect=[&] {
        ready(w,{owner,other},expectedDeliveries);
        int ownerZ=0,otherZ=0;
        for(const auto& order:w.unit(owner)->orders)ownerZ=std::max(ownerZ,order.z.floorInt());
        for(const auto& order:w.unit(other)->orders)otherZ=std::max(otherZ,std::abs(order.z.floorInt()-960));
        check(ownerZ>700,"explored owner lost its known wall detour");
        check(otherZ<32,"cached owner knowledge leaked into unexplored player route");
    };
    request();inspect();std::vector<uint64_t> hashes{w.stateHash()};
    w.stop(owner);w.stop(other);evict(w,pressure);hashes.push_back(w.stateHash());
    const auto warmHits=w.flowStats().tileCacheHits;
    request();inspect();hashes.push_back(w.stateHash());
    check(w.flowStats().tileCacheHits>warmHits,"readmitted profiles did not reuse prepared tiles");
    // A static edit while both profiles are evicted must invalidate retained
    // tiles. The old wall must no longer force a detour after readmission.
    w.stop(owner);w.stop(other);evict(w,pressure);
    for(int id:wallIds)w.unit(id)->hp={};w.tick(1.f/30);known(w,3);
    request();ready(w,{owner,other},expectedDeliveries);
    for(const auto& order:w.unit(owner)->orders)
        check(std::abs(order.z.floorInt()-640)<32,"evicted cache retained removed wall");
    hashes.push_back(w.stateHash());
    // Retained owner costs must not grant an enemy the automatic gate's
    // passage. Inspect immutable prefixes before these slow movers get near
    // the gate and trigger any physical opening behavior.
    auto gate=wall;gate.id="cache_gate";gate.gate=true;gate.footX=14;gate.footZ=4;
    gate.yardMap="ooooccccccooooooooccccccooooooooccccccooooooooccccccoooo";
    check(gate.yardMap.size()==56,"gate fixture yard dimensions");
    w.player(0).automaticGates=true;
    w.spawn(&gate,1536,1792,std::nullopt,0);
    const int friendly=w.spawn(&slow,1536,1280,std::nullopt,0);
    const int enemy=w.spawn(&slow,1536,1344,std::nullopt,1);
    known(w,3);
    // Let the newly inserted gate's revision publish before inspecting a
    // prefix: old safe routes may remain installed during bounded rebuilding.
    for(int tick=0;tick<300;++tick)w.tick(1.f/30);
    auto gateRoutes=[&] {
        w.stop(friendly);w.stop(enemy);
        const auto delivery=w.flowStats().deliveries+2;
        w.order(friendly,1536,2304,false);w.order(enemy,1536,2368,false);
        ready(w,{friendly,enemy},delivery);
        int ownerDeviation=0,enemyDeviation=0;
        for(const auto& order:w.unit(friendly)->orders)ownerDeviation=std::max(ownerDeviation,std::abs(order.x.floorInt()-1536));
        for(const auto& order:w.unit(enemy)->orders)enemyDeviation=std::max(enemyDeviation,std::abs(order.x.floorInt()-1536));
        check(ownerDeviation<32,"automatic gate owner lost passage");
        check(enemyDeviation>=112,"cached gate owner passage leaked to enemy");
    };
    gateRoutes();hashes.push_back(w.stateHash());
    w.stop(friendly);w.stop(enemy);w.stop(owner);w.stop(other);evict(w,pressure);
    gateRoutes();hashes.push_back(w.stateHash());
    std::printf("cache world serial=%d capacity=%zu evictions=%llu\n",serial,capacity,(unsigned long long)w.flowStats().profileEvictions);
    return hashes;
}
}
int main(int argc,char**argv){
    if(argc!=2)return 2;
    try{auto vfs=hpi::mountRetailRoot(argv[1],hpi::OverridePolicy::None);sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,false);
        const auto* sword=registry.find("arasword");check(sword,"cache fixture sword missing");
        const auto serial=run(*sword,true),threaded=run(*sword,false);
        check(serial==threaded,"cache eviction/edit results differ between worker modes");
        std::puts("PASS cached profile eviction, exploration/gate isolation and static invalidation");return 0;
    }catch(const std::exception&e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
