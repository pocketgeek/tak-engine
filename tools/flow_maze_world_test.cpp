#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "tnt/mapgen.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string_view>
using namespace tak;
namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
struct Result {uint32_t arrival=0;std::vector<uint64_t> hashes;};
Result run(hpi::Vfs& vfs,sim::TypeRegistry& registry,const sim::UnitType& type,
           bool known,bool serial,int startX=8448,int startZ=24320,
           int goalX=4864,int goalZ=16640,uint32_t limit=75000,
           sim::PathfindingMode selected=sim::PathfindingMode::Flowfield) {
    sim::World world;world.setVisPlayer(-1);world.setSerialThreads(serial);
    sim::MatchConfig cfg;cfg.vfs=&vfs;cfg.mapPath="maps/flow-maze-regression.tnt";
    cfg.loadCrt=false;cfg.pathfindingMode=selected;
    cfg.slots.resize(1); // No starting monarch: isolate physical steering of one unit.
    sim::setupMatch(world,registry,cfg);
    const int id=world.spawn(&type,float(startX),float(startZ),std::nullopt,0);
    world.order(id,float(goalX),float(goalZ),false);
    world.updateNavigationExploration();
    if(known) {
        // Test-only fully explored fixture. The unknown run discovers normally.
        auto& explored=const_cast<std::vector<uint16_t>&>(world.navigationExploration());
        std::fill(explored.begin(),explored.end(),0xffff);
    }
    Result result;
    for(uint32_t tick=1;tick<=limit;++tick) {
        world.tick(1.f/30);
        const auto* unit=world.unit(id);
        check(unit&&unit->alive(),"maze mover disappeared");
        if(!known&&type.id=="crebeas"&&tick%60000==0) {
            const auto& explored=world.navigationExploration();
            std::printf("discovery tick=%u at=%.1f,%.1f explored=%zu requests=%llu\n",tick,
                unit->x.toFloat(),unit->z.toFloat(),
                size_t(std::count_if(explored.begin(),explored.end(),[](uint16_t value){return value&1;})),
                (unsigned long long)world.flowStats().requests);std::fflush(stdout);
        }
        if(tick%300==0||tick==limit||unit->orders.empty()) {
            check(world.mobilePlacement(*unit,sim::footprintOrigin(unit->x,type.footX),
                sim::footprintOrigin(unit->z,type.footZ),false),
                "maze mover occupied an illegal terrain-edge footprint");
            result.hashes.push_back(world.stateHash());
        }
        if(unit->orders.empty()) {
            check(std::abs(unit->x.toFloat()-goalX)<64&&std::abs(unit->z.toFloat()-goalZ)<64,
                  "maze order cleared before reaching destination");
            result.arrival=tick;break;
        }
    }
    const auto* unit=world.unit(id);const auto stats=world.flowStats();
    const bool retailPlus=selected==sim::PathfindingMode::RetailPlus;
    const uint64_t requests=retailPlus?world.pathStats().requests():stats.requests;
    const uint64_t deliveries=retailPlus?world.pathStats().completions():stats.deliveries;
    std::printf("maze type=%s known=%d serial=%d tick=%u position=%.1f,%.1f requests=%llu deliveries=%llu hash=%016llx\n",
        type.id.c_str(),known,serial,world.tickCount(),unit->x.toFloat(),unit->z.toFloat(),
        (unsigned long long)requests,(unsigned long long)deliveries,
        (unsigned long long)world.stateHash());
    check(result.arrival!=0,"single unit looped instead of traversing maze");
    check(deliveries>0,"maze fixture did not exercise its selected route service");
    if(retailPlus)check(stats.requests==0,"Retail+ unexpectedly requested a shared terrain field");
    return result;
}
}
int main(int argc,char** argv) {
    if(argc<2||argc>3)return 2;
    if(argc==3&&std::string_view(argv[2])!="--discovery"&&
       std::string_view(argv[2])!="--retail-plus-known")return 2;
    try {
        auto vfs=hpi::mountRetailRoot(argv[1],hpi::OverridePolicy::None);
        sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,true);
        const auto* authored=registry.find("arasword");check(authored,"maze unit missing");
        auto mover=*authored;mover.weapon.damage=0;mover.weapons.clear();
        // User-reported 64x64 Taros maze. Its authored features and tight turns
        // matter: an obstacle-free field walk did not reproduce the World loop.
        constexpr auto recipe="~gen1~0800f0a981b8b476a2cb010008000808ffffff67ff034a616e6b204d617a65207631";
        auto generated=mapgen::generate(mapgen::decodeMapId(recipe),vfs);
        check(generated.map.width==2048&&generated.map.height==2048,"maze recipe dimensions changed");
        check(generated.starts.size()==8&&generated.starts[5]==std::pair(528,1520)&&
              generated.starts[6]==std::pair(304,1040),"maze recipe starts changed");
        auto files=std::make_shared<hpi::Vfs::Files>();
        (*files)["maps/flow-maze-regression.tnt"]=generated.map.save();vfs.setMapFiles(files);
        if(argc==3&&std::string_view(argv[2])=="--retail-plus-known") {
            // Retail's tracer permits native diagonal corners whose side
            // anchors are blocked. Applying local-detour side checks to that
            // route stranded this isolated mover before its first long bend.
            const auto serial=run(vfs,registry,mover,true,true,8448,24320,4864,16640,
                                  30000,sim::PathfindingMode::RetailPlus);
            const auto workers=run(vfs,registry,mover,true,false,8448,24320,4864,16640,
                                   30000,sim::PathfindingMode::RetailPlus);
            check(serial.arrival==workers.arrival&&serial.hashes==workers.hashes,
                  "Retail+ TNT route changes with serial/threaded execution");
            std::puts("PASS Retail+ known TNT physical steering and serial/threaded checkpoints");
            return 0;
        }
        if(argc==3) {
            const auto* source=registry.find("crebeas");check(source,"slow-turn maze unit missing");
            auto slow=*source;slow.weapon.damage=0;slow.weapons.clear();
            run(vfs,registry,slow,false,true,8448,8448,16640,4864,600000);
            return 0;
        }
        const auto serial=run(vfs,registry,mover,true,true);
        const auto threaded=run(vfs,registry,mover,true,false);
        check(serial.arrival==threaded.arrival&&serial.hashes==threaded.hashes,
              "maze route changes with serial/threaded execution");
        run(vfs,registry,mover,false,true);
        // Larger slow-turning units exposed another loop after the waypoint
        // detour fix: their turning arc left the only legal anchor-cell row.
        for(const char* id:{"aracan","crebeas"}) {
            const auto* source=registry.find(id);check(source,"slow-turn maze unit missing");
            auto slow=*source;slow.weapon.damage=0;slow.weapons.clear();
            const bool noPivot=slow.id=="crebeas";
            if(noPivot)check(slow.turnInPlaceRate==0,"zero-pivot fixture changed");
            const int sx=noPivot?27904:8448,sz=noPivot?16640:24320;
            const int gx=noPivot?24320:4864,gz=noPivot?24320:16640;
            const auto expected=run(vfs,registry,slow,true,true,sx,sz,gx,gz);
            if(noPivot) {
                const auto actual=run(vfs,registry,slow,true,false,sx,sz,gx,gz);
                check(expected.arrival==actual.arrival&&expected.hashes==actual.hashes,
                      "slow-turn maze route changes with serial/threaded execution");
                const auto discovered=run(vfs,registry,slow,false,true,8448,8448,16640,4864,600000);
                const auto discoveredWorker=run(vfs,registry,slow,false,false,8448,8448,16640,4864,600000);
                check(discovered.arrival==discoveredWorker.arrival&&discovered.hashes==discoveredWorker.hashes,
                      "narrow-corner discovery changes with serial/threaded execution");
            }
        }
        std::puts("PASS maze physical steering, discovery and serial/threaded checkpoints");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
