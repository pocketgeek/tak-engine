#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
int main(int argc,char**argv){
 if(argc!=6&&argc!=7){std::fprintf(stderr,"usage: flow_benchmark DATA UNITS TICKS PATHFINDING SERIAL [CRUSADES]\nPATHFINDING: 0=Retail, 1=Flowfield, 2=Cooperative.\nSynthetic flat 64x64 map; timing includes the complete World tick.\n");return 2;}
 using namespace tak;using Clock=std::chrono::steady_clock;
 const bool crusades=argc==7&&std::atoi(argv[6])!=0;
 auto vfs=hpi::mountRetailRoot(argv[1],hpi::OverridePolicy::None);sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,crusades);
 const int count=std::atoi(argv[2]),ticks=std::atoi(argv[3]);const bool serial=std::atoi(argv[5]);
 char* end=nullptr;const long selected=std::strtol(argv[4],&end,10);
 if(end==argv[4]||*end||selected<0||selected>255||!sim::validPathfindingMode(uint8_t(selected)))return 2;
 const auto mode=sim::PathfindingMode(selected);
 if(count<1||count>16000||ticks<1||ticks>10000)return 2;
 auto type=*registry.find("arasword");type.weapons.clear();type.weapon.damage=0;
 sim::World w;w.setSerialThreads(serial);w.setVisPlayer(-1);w.setPathService(true);w.setPathfindingMode(mode);
 w.setTerrain(std::vector<uint8_t>(2048*2048,100),2048,2048,64);
 for(int i=0;i<count;++i){int id=w.spawn(&type,float(512+(i%128)*32),float(512+(i/128)*32),std::nullopt,0);w.order(id,24000,24000,false);}
 std::vector<double> times;times.reserve(ticks);double sum=0;
 for(int i=0;i<ticks;++i){auto t=Clock::now();w.tick(1.f/30);double ms=std::chrono::duration<double,std::milli>(Clock::now()-t).count();times.push_back(ms);sum+=ms;}
 int moved=0;int64_t displacement=0;for(int i=0;i<count;++i){auto* u=w.unit(i+1);const int dx=std::abs(u->x.floorInt()-512-(i%128)*32),dz=std::abs(u->z.floorInt()-512-(i/128)*32);moved+=(dx||dz);displacement+=dx+dz;}
 std::printf("moved=%d/%d mean_l1_displacement_px=%.1f\n",moved,count,double(displacement)/count);
 std::sort(times.begin(),times.end());auto s=w.flowStats();
 const char* label=mode==sim::PathfindingMode::Retail?"retail":mode==sim::PathfindingMode::Flowfield?"flow":"cooperative";
 std::printf("synthetic64x64 units=%d ticks=%d mode=%s serial=%d crusades=%d mean_ms=%.3f p50_ms=%.3f p95_ms=%.3f max_ms=%.3f flow_bytes=%zu deliveries=%llu pending=%zu hash=%016llx\n",count,ticks,label,serial,crusades,sum/ticks,times[ticks/2],times[size_t(ticks-1)*95/100],times.back(),s.bytes,(unsigned long long)s.deliveries,s.pending,(unsigned long long)w.stateHash());
}
