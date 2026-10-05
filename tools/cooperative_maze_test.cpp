// Giant actual-terrain fixture derived from flow_group_maze_test. Discovery
// and complete crossing are opt-in; mode selection keeps legacy tests intact.
#include "cooperative_test_common.h"
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/retailplus.h"
#include "tnt/mapgen.h"
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <limits>
#include <cmath>
#include <stdexcept>
#include <set>
using namespace tak;
namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
struct Bounds {
    int32_t minX=INT32_MAX,maxX=INT32_MIN,minZ=INT32_MAX,maxZ=INT32_MIN;
    void include(const sim::Unit& u) {
        minX=std::min(minX,u.x.v);maxX=std::max(maxX,u.x.v);
        minZ=std::min(minZ,u.z.v);maxZ=std::max(maxZ,u.z.v);
    }
    bool progressed() const {return int64_t(maxX)-minX>128*65536||int64_t(maxZ)-minZ>128*65536;}
};
std::vector<uint64_t> run(hpi::Vfs& vfs,sim::TypeRegistry& registry,
                         const char* name,bool serial,sim::PathfindingMode selected,bool full=false,
                         int soakTicks=900000) {
    const bool discovery=std::string_view(name)=="crebeas";
    const int count=discovery?8:std::string_view(name)=="zonter"?32:16;
    const int startX=528,startZ=discovery?528:1520;
    const int goalX=discovery?16640:4864,goalZ=discovery?4864:16640;
    const int limit=discovery?(full?soakTicks:90000):30000;
    if(full)std::printf("maze soak_tick_limit=%d\n",limit);
    const auto* authored=registry.find(name);check(authored,"group maze unit missing");
    auto type=*authored;type.weapon.damage=0;type.weapons.clear();
    if(discovery)check(type.turnInPlaceRate==0,"group maze zero-pivot fixture changed");
    sim::World world;world.setVisPlayer(-1);world.setSerialThreads(serial);
    sim::MatchConfig cfg;cfg.vfs=&vfs;cfg.mapPath="maps/flow-group-maze.tnt";
    cfg.loadCrt=false;cfg.pathfindingMode=selected;cfg.slots.resize(1);
    sim::setupMatch(world,registry,cfg);
    const bool retailPlus=selected==sim::PathfindingMode::RetailPlus;
    const auto navigationBytes=[&] {return retailPlus?world.retailPlusStats().bytes:world.flowStats().bytes;};
    std::vector<int> ids;
    // Deterministic separated placement in the real starting clearing. Test
    // physical placement too: fog-aware navigation alone is not sufficient.
    for(int radius=0;radius<100&&int(ids.size())<count;++radius)
        for(int dz=-radius;dz<=radius&&int(ids.size())<count;++dz)
            for(int dx=-radius;dx<=radius&&int(ids.size())<count;++dx) {
                if(std::max(std::abs(dx),std::abs(dz))!=radius)continue;
                const int cx=startX+dx*(type.footX+1),cz=startZ+dz*(type.footZ+1);
                if(world.cellScore(&type,cx,cz,0)!=6)continue;
                const auto x=sim::footprintWaypoint(cx,type.footX),z=sim::footprintWaypoint(cz,type.footZ);
                ids.push_back(world.spawn(&type,x.toFloat(),z.toFloat(),std::nullopt,0));
            }
    check(int(ids.size())==count,"group maze spawn capacity");
    for(int id:ids) {
        const auto& unit=*world.unit(id);
        check(world.mobilePlacement(unit,sim::footprintOrigin(unit.x,type.footX),
                                   sim::footprintOrigin(unit.z,type.footZ),false),
              "group maze spawn overlaps a body or obstacle");
    }
    for(int id:ids)world.order(id,float(goalX),float(goalZ),false);
    world.updateNavigationExploration();
    if(!discovery) {
        auto& explored=const_cast<std::vector<uint16_t>&>(world.navigationExploration());
        std::fill(explored.begin(),explored.end(),0xffff);
    }
    const auto exploredCount=[&] {const auto& explored=world.navigationExploration();
        return size_t(std::count_if(explored.begin(),explored.end(),[](uint16_t value){return value&1;}));};
    const size_t initiallyExplored=exploredCount();size_t peak=0;
    std::vector<Bounds> bounds(ids.size());std::vector<std::set<std::pair<int,int>>> visited(ids.size());std::vector<uint64_t> hashes;
    for(int tick=1;tick<=limit;++tick) {
        world.tick(1.f/30);
        if(full||tick>limit-20000)for(size_t i=0;i<ids.size();++i) {
            const auto* unit=world.unit(ids[i]);check(unit&&unit->alive(),"group maze mover disappeared");
            bounds[i].include(*unit);
            if(discovery&&!full)visited[i].emplace(sim::footprintCell(unit->x,type.footX),sim::footprintCell(unit->z,type.footZ));
        }
        if(tick%300==0||tick==limit) {
            for(int id:ids) {
                const auto& unit=*world.unit(id);
                check(world.mobilePlacement(unit,sim::footprintOrigin(unit.x,type.footX),
                    sim::footprintOrigin(unit.z,type.footZ),false),"group occupied an illegal terrain-edge footprint");
            }
            hashes.push_back(world.stateHash());
            peak=std::max(peak,navigationBytes());
        }
        if(full&&tick%30000==0) {
            int arrived=0;for(int id:ids)arrived+=world.unit(id)->orders.empty();
            const auto& explored=world.navigationExploration();
            std::printf("group discovery tick=%d arrived=%d/%d explored=%zu requests=%llu\n",tick,arrived,count,
                size_t(std::count_if(explored.begin(),explored.end(),[](uint16_t value){return value&1;})),
                (unsigned long long)(retailPlus?world.pathStats().requests():world.flowStats().requests));std::fflush(stdout);
        }
        if(full&&std::all_of(ids.begin(),ids.end(),[&](int id){return world.unit(id)->orders.empty();}))break;
    }
    int arrived=0;bool completed=true,compact=true,progress=true;
    for(size_t i=0;i<ids.size();++i) {
        const auto& unit=*world.unit(ids[i]);arrived+=unit.orders.empty();
        std::printf("group type=%s serial=%d tick=%d id=%d position=%.1f,%.1f orders=%zu span=%.1f,%.1f\n",
            name,serial,int(world.tickCount()),unit.id,unit.x.toFloat(),unit.z.toFloat(),unit.orders.size(),
            double(int64_t(bounds[i].maxX)-bounds[i].minX)/65536,
            double(int64_t(bounds[i].maxZ)-bounds[i].minZ)/65536);
        if(!discovery||full) {
            // Baseline Hunters jammed both at an early corner and behind a
            // settled single-file queue near the goal. Every mover must finish.
            completed&=unit.orders.empty();
            const int64_t dx=unit.x.floorInt()-goalX,dz=unit.z.floorInt()-goalZ;
            const int64_t area=int64_t(count)*(type.footX+1)*(type.footZ+1);
            const int radius=16*(int(std::sqrt(double(area)))+2*(std::max(type.footX,type.footZ)+1))+32;
            compact&=dx*dx+dz*dz<=int64_t(radius)*radius;
        } else {
            // The unknown maze takes hundreds of thousands of ticks to finish.
            // Require every active mover to escape its local trap during this
            // observation window; full arrival is covered by the manual soak.
            progress&=unit.orders.empty()||(bounds[i].progressed()&&visited[i].size()>=64);
        }
    }
    check(completed,"known-map group did not finish its move");
    check(compact,"maze group stopped outside its finite destination area");
    check(progress,"Beast Rider group contains a persistent mid-route jam");
    if(full) {
        const auto completionTick=world.tickCount();
        std::vector<std::pair<sim::Fixed,sim::Fixed>> positions;
        const int64_t area=int64_t(count)*(type.footX+1)*(type.footZ+1);
        const int radius=16*(int(std::sqrt(double(area)))+2*(std::max(type.footX,type.footZ)+1))+32;
        // Native movement may finish its final braking steps after the order
        // retires. Match the group fixtures' bounded settling period, then
        // require exact rest while keeping every body inside the arrival area.
        for(int tick=0;tick<420;++tick) {
            if(tick==300)for(int id:ids){const auto& unit=*world.unit(id);positions.emplace_back(unit.x,unit.z);}
            world.tick(1.f/30);
            for(size_t i=0;i<ids.size();++i) {
                const auto* unit=world.unit(ids[i]);
                check(unit&&unit->alive()&&unit->orders.empty(),"completed maze mover resumed its mission");
                if(tick>=300)check(std::pair{unit->x,unit->z}==positions[i],"completed maze mover did not remain at rest");
                const int64_t dx=unit->x.floorInt()-goalX,dz=unit->z.floorInt()-goalZ;
                check(dx*dx+dz*dz<=int64_t(radius)*radius,"completed maze mover left its destination area");
                check(world.mobilePlacement(*unit,sim::footprintOrigin(unit->x,type.footX),
                    sim::footprintOrigin(unit->z,type.footZ),false),"resting maze mover has an illegal footprint");
            }
            peak=std::max(peak,navigationBytes());
        }
        hashes.push_back(world.stateHash());
        std::printf("maze completion_tick=%u settling_ticks=300 rest_ticks=120\n",completionTick);
    }
    if(discovery)check(exploredCount()>initiallyExplored+1024,"unknown giant-maze run did not discover new terrain");
    check(peak<=(retailPlus?sim::retailplus::Traffic::memoryLimit:512ull*1024*1024),
          "giant-maze navigation exceeds its memory ceiling");
    if(retailPlus)check(world.pathStats().completions()>0&&world.flowStats().requests==0,
                       "Retail+ maze did not exclusively use native long-distance routes");
    std::printf("maze exploration=%zu->%zu peak_navigation_bytes=%zu\n",initiallyExplored,exploredCount(),peak);
    std::printf("group %s serial=%d arrived=%d/%d hash=%016llx\n",name,serial,arrived,count,
        (unsigned long long)world.stateHash());
    return hashes;
}
}
int main(int argc,char** argv) {
    if(argc<2||argc>5){std::fprintf(stderr,"usage: cooperative_maze_test DATA [MODE] [known|discovery|soak] [SOAK_TICKS]\n");return 2;}
    try {
        const std::string_view mode=argc>2?argv[2]:"cooperative";
        const auto selected=mode=="retail-plus"?sim::PathfindingMode::RetailPlus:cooperative_test::mode(mode);
        const std::string_view scenario=argc>3?argv[3]:"known";
        check(scenario=="known"||scenario=="discovery"||scenario=="soak","invalid maze scenario");
        int soakTicks=900000;
        if(argc==5) {
            check(scenario=="soak","SOAK_TICKS is only valid with soak");
            const std::string_view value=argv[4];
            const auto parsed=std::from_chars(value.data(),value.data()+value.size(),soakTicks);
            check(parsed.ec==std::errc{}&&parsed.ptr==value.data()+value.size()&&
                  soakTicks>=900000&&soakTicks<=1200000,
                  "SOAK_TICKS must be a decimal integer from 900000 through 1200000");
        }
        auto vfs=hpi::mountRetailRoot(argv[1],hpi::OverridePolicy::None);
        sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,true);
        constexpr auto recipe="~gen1~0800f0a981b8b476a2cb010008000808ffffff67ff034a616e6b204d617a65207631";
        auto generated=mapgen::generate(mapgen::decodeMapId(recipe),vfs);
        check(generated.map.width==2048&&generated.map.height==2048,"giant maze recipe dimensions changed");
        auto files=std::make_shared<hpi::Vfs::Files>();
        (*files)["maps/flow-group-maze.tnt"]=generated.map.save();vfs.setMapFiles(files);
        if(scenario=="soak")run(vfs,registry,"crebeas",true,selected,true,soakTicks);
        else if(scenario=="discovery") {
            const auto serial=run(vfs,registry,"crebeas",true,selected);
            const auto workers=run(vfs,registry,"crebeas",false,selected);
            check(serial==workers,"unknown giant-maze serial/worker checkpoints differ");
        } else {
            run(vfs,registry,"zonter",true,selected);
            run(vfs,registry,"arasword",true,selected);
        }
        std::puts("PASS giant maze physical crossing/discovery checks");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
