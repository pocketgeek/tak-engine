#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/flowmemory.h"
#include <array>
#include <cstdio>
#include <stdexcept>
using namespace tak;
namespace { void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);} }
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        auto vfs=hpi::mountRetailRoot(argv[1],hpi::OverridePolicy::None);
        sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,false);
        const auto* sword=registry.find("arasword");check(sword,"missing admission fixture unit");
        const size_t capacity=sim::flow::MemoryPlan::forMap(256,256).profiles;
        std::vector<sim::UnitType> types(capacity+2);
        for(size_t i=0;i<types.size();++i) {
            types[i]=*sword;types[i].id="flow_admission_"+std::to_string(i);
            types[i].roadMult=sim::Fixed::raw(65536+int32_t(i)*1024);
            types[i].sight=8192;types[i].weapon.damage=0;types[i].weapons.clear();
        }
        sim::World serial,parallel;
        for(auto* world:{&serial,&parallel}) {
            world->setSerialThreads(world==&serial);world->setVisPlayer(-1);world->setPathService(true);
            world->setPathfindingMode(sim::PathfindingMode::Flowfield);
            world->setTerrain(std::vector<uint8_t>(256*256,100),256,256,64);
            // This wall is much longer than a local fallback window. Getting
            // around it requires a global field taking units away from their
            // goal before returning north on the far side.
            world->blockCells(120,0,2,210,true);
            for(size_t i=0;i<types.size();++i) {
                const int id=world->spawn(&types[i],1440,float(200+i*160),std::nullopt,0);
                world->order(id,float(2800+i*32),float(200+i*160),false);
            }
        }
        bool arrived=false;
        for(int tick=0;tick<30000;++tick) {
            serial.tick(1.f/30);parallel.tick(1.f/30);
            check(serial.stateHash()==parallel.stateHash(),"admission schedule differs between worker modes");
            check(serial.flowStats().profiles<=capacity,"profile capacity exceeded");
            arrived=true;
            for(int id=1;id<=int(types.size());++id)if(!serial.unit(id)->orders.empty())arrived=false;
            if(arrived)break;
        }
        const auto stats=serial.flowStats();
        std::printf("flow admission ticks=%u evictions=%llu snapshot failures=%llu local routes=%llu\n",
            serial.tickCount(),(unsigned long long)stats.profileEvictions,
            (unsigned long long)stats.snapshotFailures,(unsigned long long)stats.localDeliveries);
        for(int id=1;id<=int(types.size());++id) {
            const auto* u=serial.unit(id);
            std::printf("unit %d %.1f,%.1f remaining=%zu\n",id,u->x.toFloat(),u->z.toFloat(),u->orders.size());
            check(u->x.toFloat()>2700,"profile class never crossed global detour");
        }
        check(arrived,"profile admission failed eventual arrival");
        check(stats.profileEvictions>0,"profile capacity fixture never evicted a profile");
        {
            // More disconnected one-cell islands than the graph component cap
            // force an explicit snapshot failure. Removing the obstacles must
            // let the bounded retry recover without resetting the World.
            auto tiny=*sword;tiny.id="flow_failure_recovery";tiny.footX=1;tiny.footZ=1;
            tiny.sight=16384;tiny.weapon.damage=0;tiny.weapons.clear();
            sim::World world;world.setSerialThreads(true);world.setVisPlayer(-1);world.setPathService(true);
            world.setPathfindingMode(sim::PathfindingMode::Flowfield);
            world.setTerrain(std::vector<uint8_t>(512*512,100),512,512,64);
            for(int z=0;z<512;++z)for(int x=(z+1)%2;x<512;x+=2)world.blockCells(x,z,1,1,true);
            const int id=world.spawn(&tiny,160,160,std::nullopt,0);world.order(id,1200,1200,false);
            for(int tick=0;tick<1000&&!world.flowStats().snapshotFailures;++tick)world.tick(1.f/30);
            check(world.flowStats().snapshotFailures>0,"fragmented topology did not exercise snapshot failure");
            const auto failures=world.flowStats().snapshotFailures;
            for(int tick=0;tick<30;++tick)world.tick(1.f/30);
            check(world.flowStats().snapshotFailures==failures,"failed snapshots retry without backoff");
            world.blockCells(0,0,512,512,false);
            for(int tick=0;tick<6000&&!world.unit(id)->orders.empty();++tick)world.tick(1.f/30);
            check(world.unit(id)->orders.empty()&&world.unit(id)->x.toFloat()>1100,
                "failed snapshot did not recover after terrain repair");
        }
        std::puts("PASS flow profile admission, long detours and snapshot recovery");return 0;
    }catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
