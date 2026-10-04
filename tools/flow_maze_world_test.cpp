#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "tnt/mapgen.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
using namespace tak;
namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
struct Result {uint32_t arrival=0;std::vector<uint64_t> hashes;};
Result run(hpi::Vfs& vfs,sim::TypeRegistry& registry,const sim::UnitType& type,
           bool known,bool serial,int startX=8448,int startZ=24320,
           int goalX=4864,int goalZ=16640,uint32_t limit=75000,bool cornerEscape=false) {
    sim::World world;world.setVisPlayer(-1);world.setSerialThreads(serial);
    sim::MatchConfig cfg;cfg.vfs=&vfs;cfg.mapPath="maps/flow-maze-regression.tnt";
    cfg.loadCrt=false;cfg.pathfindingMode=sim::PathfindingMode::Flowfield;
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
        if(tick%300==0||tick==limit||unit->orders.empty())result.hashes.push_back(world.stateHash());
        if(unit->orders.empty()) {
            check(std::abs(unit->x.toFloat()-goalX)<64&&std::abs(unit->z.toFloat()-goalZ)<64,
                  "maze order cleared before reaching destination");
            result.arrival=tick;break;
        }
    }
    const auto* unit=world.unit(id);const auto stats=world.flowStats();
    std::printf("maze type=%s known=%d serial=%d tick=%u position=%.1f,%.1f requests=%llu deliveries=%llu hash=%016llx\n",
        type.id.c_str(),known,serial,world.tickCount(),unit->x.toFloat(),unit->z.toFloat(),
        (unsigned long long)stats.requests,(unsigned long long)stats.deliveries,
        (unsigned long long)world.stateHash());
    if(cornerEscape) {
        // Without constrained turn speed this discovery route circles around
        // (8912,8016) indefinitely. Require progress beyond its first corridor,
        // not final arrival through the very long unexplored maze.
        check(unit->z<sim::Fixed::fromInt(7000)&&!unit->orders.empty(),
              "zero-pivot mover did not escape the discovered narrow corner");
    } else check(result.arrival!=0,"single unit looped instead of traversing maze");
    check(stats.deliveries>0,"maze fixture did not exercise Flowfield");
    return result;
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        auto vfs=hpi::mountRetailRoot(argv[1],hpi::OverridePolicy::None);
        sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,true);
        const auto* authored=registry.find("arasword");check(authored,"maze unit missing");
        auto mover=*authored;mover.weapon.damage=0;mover.weapons.clear();
        // User-reported 64x64 Taros maze. Its authored features and tight turns
        // matter: an obstacle-free field walk did not reproduce the World loop.
        constexpr auto recipe="~gen1~0700f0a981b8b476a2cb010008000808ffffff67ff034a616e6b204d617a65207631";
        auto generated=mapgen::generate(mapgen::decodeMapId(recipe),vfs);
        check(generated.map.width==2048&&generated.map.height==2048,"maze recipe dimensions changed");
        check(generated.starts.size()==8&&generated.starts[5]==std::pair(528,1520)&&
              generated.starts[6]==std::pair(304,1040),"maze recipe starts changed");
        auto files=std::make_shared<hpi::Vfs::Files>();
        (*files)["maps/flow-maze-regression.tnt"]=generated.map.save();vfs.setMapFiles(files);
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
                const auto discovered=run(vfs,registry,slow,false,true,8448,8448,16640,4864,10000,true);
                const auto discoveredWorker=run(vfs,registry,slow,false,false,8448,8448,16640,4864,10000,true);
                check(discovered.hashes==discoveredWorker.hashes,
                      "narrow-corner discovery changes with serial/threaded execution");
            }
        }
        std::puts("PASS maze physical steering, discovery and serial/threaded checkpoints");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
