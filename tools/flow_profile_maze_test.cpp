#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/flowmemory.h"
#include "tnt/mapgen.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
using namespace tak;
namespace {
void check(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
std::vector<uint64_t> run(hpi::Vfs& vfs,sim::TypeRegistry& registry,
                         const std::vector<std::pair<int,int>>& starts,bool serial) {
    sim::World world;world.setVisPlayer(-1);world.setSerialThreads(serial);
    sim::MatchConfig cfg;cfg.vfs=&vfs;cfg.mapPath="maps/flow-profile-maze.tnt";
    cfg.loadCrt=false;cfg.pathfindingMode=sim::PathfindingMode::Flowfield;cfg.slots.resize(8);
    sim::setupMatch(world,registry,cfg);
    const auto* authored=registry.find("arasword");check(authored,"profile maze sword missing");
    auto type=*authored;type.weapon.damage=0;type.weapons.clear();
    std::vector<int> ids;
    for(int player=0;player<8;++player) {
        const auto at=starts[player],goal=starts[(player+1)%8];
        const int id=world.spawn(&type,float(at.first*16),float(at.second*16),std::nullopt,player);
        ids.push_back(id);world.order(id,float(goal.first*16),float(goal.second*16),false);
    }
    // Remove discovery/combat/crowding as explanations: only the player key
    // distinguishes eight independently keyed profiles; admission is bounded by the map plan.
    world.updateNavigationExploration();
    auto& explored=const_cast<std::vector<uint16_t>&>(world.navigationExploration());
    std::fill(explored.begin(),explored.end(),0xffff);
    std::vector<uint64_t> hashes;int finished=0;
    for(int tick=1;tick<=180000;++tick) {
        world.tick(1.f/30);
        if(tick%300!=0)continue;
        hashes.push_back(world.stateHash());
        const auto stats=world.flowStats();
        check(stats.profiles<=sim::flow::MemoryPlan::forMap(2048,2048).profiles,"profile maze exceeded resident profile cap");
        bool arrived=true;
        for(int id:ids) {const auto* unit=world.unit(id);check(unit&&unit->alive(),"profile maze mover lost");arrived&=unit->orders.empty();}
        if(arrived) {finished=tick;break;}
    }
    check(finished!=0,"eight-player maze contains persistent profile-churn loops");
    const auto stats=world.flowStats();
    if(sim::flow::MemoryPlan::forMap(2048,2048).profiles<8)
        check(stats.profileEvictions>0,"profile maze did not exercise eviction");
    check(stats.localWork<1000000,"distant goals consumed repeated local fallback work");
    std::printf("profile maze serial=%d arrival=%d evictions=%llu localwork=%llu hash=%016llx\n",
        serial,finished,(unsigned long long)stats.profileEvictions,
        (unsigned long long)stats.localWork,(unsigned long long)world.stateHash());
    return hashes;
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        auto vfs=hpi::mountRetailRoot(argv[1],hpi::OverridePolicy::None);
        sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,true);
        constexpr auto recipe="~gen1~0800f0a981b8b476a2cb010008000808ffffff67ff034a616e6b204d617a65207631";
        auto generated=mapgen::generate(mapgen::decodeMapId(recipe),vfs);
        check(generated.map.width==2048&&generated.map.height==2048&&generated.starts.size()==8,
              "profile maze recipe changed");
        auto files=std::make_shared<hpi::Vfs::Files>();
        (*files)["maps/flow-profile-maze.tnt"]=generated.map.save();vfs.setMapFiles(files);
        const auto serial=run(vfs,registry,generated.starts,true);
        const auto threaded=run(vfs,registry,generated.starts,false);
        check(serial==threaded,"profile maze serial/threaded checkpoint mismatch");
        std::puts("PASS eight-player maze profile churn, complete arrival and bounded local fallback");return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}
}
