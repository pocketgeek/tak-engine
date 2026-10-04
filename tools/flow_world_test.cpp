#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/flowmemory.h"
#include <cstdio>
#include <stdexcept>
using namespace tak;
namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
void setup(sim::World& w,const sim::UnitType* type,bool serial,bool wall) {
    w.setVisPlayer(-1);w.setSerialThreads(serial);w.setPathService(true);
    w.setPathfindingMode(sim::PathfindingMode::Flowfield);
    w.setTerrain(std::vector<uint8_t>(128*96,type->domain==sim::UnitType::Domain::Water?10:100),128,96,64);
    if(wall)w.blockCells(63,0,2,65,true);
    const int id=w.spawn(type,160,160,std::nullopt,0);
    w.order(id,1776,192,false);
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        auto vfs=hpi::mountRetailRoot(argv[1],hpi::OverridePolicy::None);
        sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,false);
        // Landed flyers participate in authoritative ground collision even
        // though the coarse moving-unit occupancy grid excludes flying types.
        // They must trigger local avoidance; airborne versions must not.
        for(int flightMode:{1,0}) {
            auto ground=*registry.find("araarch"),flyer=*registry.find("arafast");
            ground.weapons.clear();ground.weapon.damage=0;flyer.weapons.clear();flyer.weapon.damage=0;
            sim::World serial,parallel;
            for(auto* w:{&serial,&parallel}) {
                w->setVisPlayer(-1);w->setSerialThreads(w==&serial);w->setPathService(true);
                w->setPathfindingMode(sim::PathfindingMode::Flowfield);
                w->setTerrain(std::vector<uint8_t>(64*64,100),64,64,64);
                w->setMapPlacementFeatures(std::vector<uint16_t>(64*64,0xffff),{});
                w->spawn(&ground,160,160,std::nullopt,0);w->spawn(&flyer,256,160,std::nullopt,0);
                w->unit(2)->flightGroundMode=flightMode;
                w->unit(2)->flightY=sim::Fixed::fromInt(flightMode?100:200);
                w->order(1,640,160,false);
            }
            int excursion=0;
            for(int tick=0;tick<2000&&!serial.unit(1)->orders.empty();++tick) {
                serial.tick(1.f/30);parallel.tick(1.f/30);
                check(serial.stateHash()==parallel.stateHash(),"landed flyer avoidance differs by worker mode");
                excursion=std::max(excursion,std::abs(serial.unit(1)->z.floorInt()-160));
                check(serial.unit(2)->flightGroundMode==flightMode,"flying obstacle changed its collision state");
            }
            check(serial.unit(1)->orders.empty()&&serial.unit(1)->x.floorInt()>620,
                "ground mover remained blocked by an idle landed flyer");
            std::printf("flow flying obstacle: mode=%d tick=%u excursion=%d\n",flightMode,serial.tickCount(),excursion);
            check(flightMode?excursion>16:excursion<=8,
                "ground avoidance failed to distinguish landed and airborne flyers");
        }
        sim::UnitType closedYard;closedYard.id="flow_closed_yard";closedYard.maxVel={};closedYard.maxHp=1000;
        closedYard.footX=2;closedYard.footZ=64;closedYard.yardMap=std::string(128,'c');
        for(const char* name:{"arasword","aratrans"})
        for(int wall:{0,1,2}) {
            const auto* type=registry.find(name);check(type,"movement fixture unit missing");
            sim::World inlineWorld,workerWorld;setup(inlineWorld,type,true,wall==1);setup(workerWorld,type,false,wall==1);
            if(wall==2) {
                inlineWorld.spawn(&closedYard,1024,512,std::nullopt,0);
                workerWorld.spawn(&closedYard,1024,512,std::nullopt,0);
                check(inlineWorld.navFor(type).walkable(63,20),"closed-yard fixture already blocked in terrain");
            }
            workerWorld.setVisPlayer(0);
            check(inlineWorld.stateHash()==workerWorld.stateHash(),"initial hash differs with workers/local display viewer");
            bool arrived=false;
            for(int i=0;i<10000;++i) {
                inlineWorld.tick(1.f/30);workerWorld.tick(1.f/30);
                check(inlineWorld.stateHash()==workerWorld.stateHash(),"flow world worker determinism");
                const auto* u=inlineWorld.unit(1);
                if(u->orders.empty()) {arrived=true;break;}
            }
            const auto* u=inlineWorld.unit(1);
            const auto stats=inlineWorld.flowStats();
            std::printf("flow world type=%s wall=%d tick=%u position=%.1f,%.1f requests=%llu deliveries=%llu pending=%zu hash=%016llx\n",
                name,wall,inlineWorld.tickCount(),u->x.toFloat(),u->z.toFloat(),
                (unsigned long long)stats.requests,(unsigned long long)stats.deliveries,stats.pending,
                (unsigned long long)inlineWorld.stateHash());
            if(!arrived) {
                const int cx=sim::footprintCell(u->x,type->footX),cz=sim::footprintCell(u->z,type->footZ);
                std::fprintf(stderr,"blocked foot=%d,%d cell=%d,%d orders=%zu\n",type->footX,type->footZ,cx,cz,u->orders.size());
                for(int z=cz-2;z<=cz+2;++z) {for(int x=cx-2;x<=cx+2;++x)std::fprintf(stderr," %d",inlineWorld.cellScore(type,x,z,u->id));std::fputc('\n',stderr);}
            }
            check(arrived&&u->x.toFloat()>1700,"flow world did not reach destination");
            check(stats.deliveries>0,"world did not consume a flow route");
        }
        // A route being prepared must not revive a stopped or replaced order.
        sim::World cancelled;setup(cancelled,registry.find("arasword"),true,false);
        cancelled.tick(1.f/30);cancelled.stop(1);
        for(int i=0;i<40;++i)cancelled.tick(1.f/30);
        check(cancelled.unit(1)->orders.empty()&&cancelled.flowStats().pending==0,"Stop retained a pending flow route");
        cancelled.order(1,400,800,false);cancelled.order(1,160,400,true);
        bool completed=false;
        for(int i=0;i<4000;++i) {cancelled.tick(1.f/30);if(cancelled.unit(1)->orders.empty()){completed=true;break;}}
        check(completed&&cancelled.unit(1)->x.toFloat()<200&&cancelled.unit(1)->z.toFloat()>350,
            "flow route replacement lost queued destination");
        bool rejected=false;
        try {cancelled.setPathfindingMode(sim::PathfindingMode::Retail);}catch(const std::logic_error&) {rejected=true;}
        check(rejected,"pathfinding mode changed during a running match");
        {
            // 4,096 bodies select two exploration workers on the threaded
            // World. Expanding sight after flow preparation starts exercises
            // private reveal notifications, including overlapping new bits.
            auto scout=*registry.find("arasword");scout.sight=40;scout.weapon.damage=0;scout.weapons.clear();
            sim::World serial,parallel;
            for(auto* w:{&serial,&parallel}) {
                w->setSerialThreads(w==&serial);w->setVisPlayer(-1);w->setPathService(true);
                w->setPathfindingMode(sim::PathfindingMode::Flowfield);
                w->setTerrain(std::vector<uint8_t>(512*512,100),512,512,64);
                for(int i=0;i<4096;++i)w->spawn(&scout,float(128+(i%64)*48),float(128+(i/64)*48),std::nullopt,0);
                w->order(1,1776,192,false);
            }
            for(int tick=0;tick<4;++tick) {
                if(tick==2)scout.sight=160;
                serial.tick(1.f/30);parallel.tick(1.f/30);
                check(serial.navigationExploration()==parallel.navigationExploration(),"parallel reveal masks differ");
                check(serial.stateHash()==parallel.stateHash(),"parallel exploration changes flow publication state");
            }
        }
        {
            const size_t capacity=sim::flow::MemoryPlan::forMap(512,512).profiles;
            std::vector<sim::UnitType> types(capacity+1);
            for(size_t i=0;i<types.size();++i) {
                types[i]=*registry.find("arasword");types[i].id="flow_profile_"+std::to_string(i);
                types[i].roadMult=sim::Fixed::raw(65536+int32_t(i)*1024);
                types[i].sight=8192; // Fully known tiles keep cold preparation busy beyond the fallback delay.
            }
            sim::World serial,parallel;
            for(auto* w:{&serial,&parallel}) {
                w->setSerialThreads(w==&serial);w->setVisPlayer(-1);w->setPathService(true);
                w->setPathfindingMode(sim::PathfindingMode::Flowfield);
                w->setTerrain(std::vector<uint8_t>(512*512,100),512,512,64);
                for(size_t i=0;i<types.size();++i) {
                    const int id=w->spawn(&types[i],160,float(160+i*64),std::nullopt,0);
                    // Keep the admitted far routes resident, then exercise a complete
                    // nearby fallback for the waiting profile. A greedy
                    // partial route toward a far goal is deliberately forbidden.
                    w->order(id,i+1==types.size()?550:5500,float(160+i*64),false);
                }
            }
            for(int tick=0;tick<160;++tick) {
                serial.tick(1.f/30);parallel.tick(1.f/30);
                check(serial.stateHash()==parallel.stateHash(),"capacity fallback differs across worker execution");
            }
            const auto stats=serial.flowStats();
            std::printf("flow capacity: local routes=%llu local work=%llu profiles=%zu\n",
                (unsigned long long)stats.localDeliveries,(unsigned long long)stats.localWork,stats.profiles);
            check(stats.localDeliveries>0,"full profile cache did not exercise bounded fallback");
            check(stats.localWork<=160*8192&&stats.profiles<=capacity,"fallback/profile work caps exceeded");
            for(int id=1;id<=int(types.size());++id) {serial.stop(id);parallel.stop(id);}
            const auto before=serial.flowStats().localWork;
            for(int tick=0;tick<8;++tick) {
                serial.tick(1.f/30);parallel.tick(1.f/30);
                check(serial.stateHash()==parallel.stateHash(),"cancelled fallback differs across workers");
            }
            check(serial.flowStats().pending==0&&serial.flowStats().localWork==before,"Stop retained local fallback jobs");
        }
        sim::World implicitRetail,explicitRetail;
        explicitRetail.setPathfindingMode(sim::PathfindingMode::Retail);
        check(implicitRetail.stateHash()==explicitRetail.stateHash(),"explicit Retail changed default state");
        check(implicitRetail.flowStats().bytes==0,"Retail allocated flow state");
        std::puts("PASS flow World route delivery/worker determinism/retail default");return 0;
    }catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
