#include "cooperative_test_common.h"
#include "sim/flownavigator.h"

using namespace cooperative_test;
namespace tak::sim {
struct RetailReplayProbe {
    static void dirty(World& world,int x,int z,int w,int h) {
        world.flow_->dirty(x,z,w,h);
    }
    static bool request(World& world,int id,Fixed x,Fixed z) {
        return world.flow_->request(*world.unit(id),x,z);
    }
};
}
namespace {
enum class Scenario {Open,LocalDirty,ScreeningDirty,FootprintHaloDirty,Narrow,ChangedScreening};
std::vector<uint64_t> run(Scenario scenario,bool serial) {
    constexpr int cells=512;
    World world;setup(world,PathfindingMode::Cooperative,serial,false,cells);
    if(scenario==Scenario::Narrow) {
        std::vector<uint16_t> features(size_t(cells)*cells,0xffff);
        for(int z=0;z<cells;++z)if(z<40||z>=44)for(int x=64;x<96;++x)
            features[size_t(z)*cells+x]=0;
        world.blockCells(64,0,32,40,true);world.blockCells(64,44,32,cells-44,true);
        world.setMapPlacementFeatures(features,{{"freshness-wall",1,1,true,true,false,0}});
    }
    auto type=mover();type.maxVel=Fixed::raw(1);
    if(scenario==Scenario::FootprintHaloDirty)type.footX=type.footZ=4;
    const int z=scenario==Scenario::Narrow?42:scenario==Scenario::ChangedScreening?106:scenario==Scenario::FootprintHaloDirty?46:48;
    const int id=world.spawn(&type,40*16,float(z*16),std::nullopt,0);
    known(world);world.order(id,120*16,float(z*16),false);
    for(int tick=0;tick<2000&&(world.flowStats().deliveries==0||world.flowStats().pending);++tick)
        world.tick(1.f/30);
    require(world.flowStats().deliveries>0&&!world.flowStats().pending,"freshness fixture did not warm its published route");
    std::vector<uint64_t> hashes{world.stateHash()};
    const auto before=world.flowStats();
    if(scenario==Scenario::ChangedScreening) {
        // The published tile was wholly open. Replacing it with a narrow
        // footprint-cost corridor must replace its proof at publication.
        std::vector<uint16_t> features(size_t(cells)*cells,0xffff);
        for(int wallZ=0;wallZ<cells;++wallZ)if(wallZ<104||wallZ>=108)for(int x=64;x<96;++x)
            features[size_t(wallZ)*cells+x]=0;
        world.blockCells(64,0,32,104,true);world.blockCells(64,108,32,cells-108,true);
        world.setMapPlacementFeatures(features,{{"changed-screening-wall",1,1,true,true,false,0}});
        RetailReplayProbe::dirty(world,64,0,32,cells);
    }
    // Sixteen distant tiles require more than one snapshot quantum. The old
    // prefix and its screening rays use only unaffected tiles near the start.
    RetailReplayProbe::dirty(world,256,256,256,256);
    if(scenario==Scenario::LocalDirty)RetailReplayProbe::dirty(world,40,z,1,1);
    if(scenario==Scenario::ScreeningDirty)RetailReplayProbe::dirty(world,32,64,128,1);
    // This raw edit lies beyond the positive screening ray (z=62), but
    // erosion for the 4-cell footprint dirties its neighboring anchor tile.
    if(scenario==Scenario::FootprintHaloDirty)RetailReplayProbe::dirty(world,32,64,128,1);
    require(RetailReplayProbe::request(world,id,Fixed::fromInt(120*16),Fixed::fromInt(z*16)),
            "freshness fixture did not enqueue a repeated route");
    world.tick(1.f/30);
    const auto first=world.flowStats();
    if(scenario==Scenario::Open) {
        require(first.deliveries==before.deliveries+1&&!first.pending,
                "unrelated dirty tiles froze an already proved open prefix");
        require(first.cooperativeClearanceHits>before.cooperativeClearanceHits,
                "repeated route did not share its unchanged open clearance proofs");
    } else {
        require(first.deliveries==before.deliveries&&first.pending==1,
                "local dirt or a narrow strip bypassed fresh passage admission");
    }
    hashes.push_back(world.stateHash());world.tick(1.f/30);
    require(world.flowStats().snapshotWork>first.snapshotWork,
            "fixture did not keep the unrelated snapshot rebuild active");
    for(int tick=0;tick<2000&&world.flowStats().pending;++tick)world.tick(1.f/30);
    require(!world.flowStats().pending&&world.flowStats().deliveries==before.deliveries+1,
            "deferred prefix did not resume after current terrain published");
    if(scenario==Scenario::ChangedScreening)
        require(world.flowStats().cooperativePassageProbes>before.cooperativePassageProbes,
                "new narrow topology retained its previous whole-tile open proof");
    legal(world,{id});hashes.push_back(world.stateHash());return hashes;
}
}
int main() {try {
    for(const auto scenario:{Scenario::Open,Scenario::LocalDirty,Scenario::ScreeningDirty,Scenario::FootprintHaloDirty,Scenario::Narrow,Scenario::ChangedScreening})
        require(run(scenario,true)==run(scenario,false),"local freshness admission depends on worker scheduling");
    std::puts("PASS Cooperative open-prefix freshness, local and screening dirt, narrow admission, proof replacement and worker determinism");
    return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}}
