#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/flowmemory.h"
#include <array>
#include <cstdio>
#include <stdexcept>
using namespace tak;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        auto vfs=hpi::mountRetailRoot(argv[1],hpi::OverridePolicy::None);
        sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,false);
        auto attacker=*registry.find("araarch");attacker.sight=4096;
        attacker.weapon.range=256;for(auto& weapon:attacker.weapons)weapon.range=256;
        auto target=*registry.find("arasword");target.maxHp=20000;
        target.healTime=0;target.weapons.clear();target.weapon.damage=0;
        target.maxVel={};target.canMove=false;
        const size_t countProfiles=sim::flow::MemoryPlan::forMap(1024,1024).profiles+2;
        std::vector<sim::UnitType> attackers(countProfiles,attacker);
        for(size_t i=0;i<attackers.size();++i)attackers[i].roadMult=sim::Fixed::raw(65536+int32_t(i)*128);
        sim::World serial,parallel;
        for(auto* w:{&serial,&parallel}) {
            w->setPlayerCount(8);
            w->setVisPlayer(-1);w->setSerialThreads(w==&serial);
            w->setPathService(true);w->setPathfindingMode(sim::PathfindingMode::Flowfield);
            w->setTerrain(std::vector<uint8_t>(1024*1024,100),1024,1024,64);
            // More movement/owner profiles than the memory plan can retain. Each
            // attacker starts near weapon range, behind a wall requiring a
            // detour longer than one 63-cell route prefix.
            // The distance-only goal region is locally reachable but does not
            // itself prove that the chosen firing position has line of sight.
            for(int i=0;i<int(countProfiles);++i) {
                const int row=i/3,col=i%3,z=row*2560+800,x=col*4096+800;
                w->blockCells(63+col*256,row*160,2,96,true);
                w->spawn(&attackers[size_t(i)],x,z,std::nullopt,i%6);
                w->spawn(&target,x+300,z,std::nullopt,7);
                w->attack(2*i+1,2*i+2,false);
            }
        }
        std::vector<bool> hit(countProfiles,false);
        for(int tick=0;tick<10000;++tick) {
            const auto before=serial.flowStats();
            serial.tick(1.f/30);parallel.tick(1.f/30);
            check(serial.stateHash()==parallel.stateHash(),"contended combat differs by worker mode");
            const auto after=serial.flowStats();
            check(after.firingRays-before.firingRays<=64&&after.firingCells-before.firingCells<=8192,
                "contended combat exceeded firing work quota");
            bool all=true;
            for(int i=0;i<int(countProfiles);++i) {
                const auto* u=serial.unit(2*i+2);
                hit[size_t(i)]=hit[size_t(i)]||!u||u->hp<sim::Fixed::fromInt(20000);
                all&=hit[size_t(i)];
            }
            if(all)break;
        }
        const auto stats=serial.flowStats();
        int count=0;for(bool h:hit)count+=h;
        std::printf("contended firing: hits=%d/%zu tick=%u evictions=%llu rays=%llu local=%llu\n",
            count,countProfiles,serial.tickCount(),(unsigned long long)stats.profileEvictions,
            (unsigned long long)stats.firingRays,(unsigned long long)stats.localDeliveries);
        for(int i=0;i<int(countProfiles);++i)if(!hit[size_t(i)]) {const auto* u=serial.unit(2*i+1);std::printf("miss %d pos=%d,%d orders=%zu blocked=%d\n",i,u->x.floorInt(),u->z.floorInt(),u->orders.size(),u->bodyBlockStreak);}
        check(stats.profileEvictions>0,"combat fixture did not contend for profile slots");
        check(stats.localDeliveries==0,"unclassified combat published a distance-only local endpoint");
        check(stats.firingRays>0,"combat fixture did not search firing positions");
        check(count==int(countProfiles),"cold/evicted combat profile lost its firing-position route");
        return 0;
    }catch(const std::exception& error) {std::fprintf(stderr,"flow combat admission: %s\n",error.what());return 1;}
}
