// featgen_test -- the contract behind World::featGeneration().
//
// The renderer's feature sync (GameView::syncBurningFeatures) used to take simMutex_
// and rescan every feature every cosmetic step to discover changes that, measured over
// 45s, happened on 0% of steps: ~1ms of pure LOCK WAIT per step for nothing (the scan
// itself was 0.012ms). It now skips both locked scans while featGeneration() is
// unchanged, which makes this counter load-bearing in two directions:
//
//   * A MISSED bump silently freezes feature visuals -- a tree that ignites, burns to
//     its next stage or is reclaimed away would never update on screen.
//   * A SPURIOUS bump (e.g. bumping every tick) puts the lock wait straight back and
//     the optimization is worth nothing.
//
// Both directions are asserted here, including ignition timestamps retained for
// viewers that skip simulation ticks. The test probe reaches private ignition.

#include "sim/sim.h"
#include "sim/matchsetup.h"
#include "hpi/hpi.h"

#include <cstdio>

namespace tak::sim {
struct RetailReplayProbe {
    static void ignite(World& world,int id) {
        world.igniteFeature(world.features_.at(world.featureIdx_.at(id)));
    }
    static void visualIgnition(World& world,int id,uint32_t tick) {
        world.features_.at(world.featureIdx_.at(id)).burnStarted=tick;
    }
    static int grade(const World& world,int x,int z) { return world.mapFeatureGrade(x,z); }
    static void cache(World& world,int id) { world.prepareSearchGrade(id,false); }
    static int cached(const World& world,int x,int z) {
        const auto& plane=world.searchGrades_.front();
        return plane.cells.at(size_t(z)*plane.nav->width()+x)&7;
    }
    static void retire(World& world,int id) { world.retireCorpse(*world.unit(id)); }
    static void replace(World& world,int id,int type) {
        world.swapFeature(world.features_.at(world.featureIdx_.at(id)),type);
    }
};
}

using tak::sim::World;

static int g_fail = 0;
static void check(bool cond, const char* what) {
    std::printf("  %-64s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) ++g_fail;
}

int main() {
    std::printf("featgen_test\n");

    // A lobby registry rebuild may reuse a corpse-bearing unit's address for
    // a corpse-less drake. The stale mapping stayed invisible until its death,
    // when only the client gave it a permanent corpse (live replay tick 77549).
    for (int reset=0;reset<3;++reset) {
        using namespace tak::sim;
        World reused,fresh;
        reused.setVisPlayer(-1);fresh.setVisPlayer(-1);
        UnitType type;type.id="old-building";type.maxHp=100;
        FeatType corpse;corpse.name="old-corpse";corpse.decomposeTicks=0;
        reused.setFeatureTypes({corpse});
        reused.mapCorpse(&type,0);reused.mapStatue(&type,0,0);
        type=UnitType{};type.id="corpse-less-flyer";type.maxHp=100;type.canFly=true;
        if (reset==0) reused.clearFeatures();
        else if (reset==1) reused.resetForReplay();
        else {
            tak::hpi::Vfs vfs;
            tak::tnt::Map map;
            registerMapFeatures(reused,map,vfs);
        }
        check(reused.corpseTypeOf(&type)==-1 && reused.statueTypeOf(&type,false)==-1 &&
              reused.statueTypeOf(&type,true)==-1,
              "feature rebuild/replay reset discards all old unit-type mappings");
        // Keep an unrelated permanent feature at index zero, as on a real map.
        reused.setFeatureTypes({corpse});fresh.setFeatureTypes({corpse});
        for (World* world:{&reused,&fresh}) {
            const int id=world->spawn(&type,128,128,0,0);
            world->unit(id)->hp={};
            world->tick(1.0f/30);
        }
        check(reused.units().front().corpseUntil==World::kCorpseAnimTicks &&
              reused.stateHash()==fresh.stateHash(),
              "corpse-less death after registry reuse matches a fresh referee");
    }

    {
        using namespace tak::sim;
        World fire;fire.setVisPlayer(-1);
        FeatType tree;tree.name="tree";tree.flamable=true;tree.hasBurnAnim=true;tree.burnTicks=30;
        fire.setFeatureTypes({tree});
        fire.addFeature(77,100,100,10,5,1,1,false,0);
        for(int i=0;i<17;++i)fire.tick(1.0f/30);
        const auto generation=fire.featGeneration();
        RetailReplayProbe::ignite(fire,77);
        check(fire.feature(77)->burnStarted==17 && fire.featGeneration()!=generation,
              "ignition preserves the actual simulation tick for late viewers");
        for(int i=0;i<9;++i)fire.tick(1.0f/30);
        RetailReplayProbe::ignite(fire,77);
        check(fire.feature(77)->burnStarted==17,
              "repeated ignition and skipped visual ticks do not restart the clock");
        const auto hash=fire.stateHash();
        RetailReplayProbe::visualIgnition(fire,77,1000);
        check(fire.stateHash()==hash,"visual ignition timestamp does not affect lockstep hash");
        for(int i=0;i<20;++i)fire.tick(1.0f/30);
        check(fire.feature(77)->alive && fire.feature(77)->burnLeft==1,
              "burn remains present through the final authored display tick");
        fire.tick(1.0f/30);
        check(!fire.feature(77)->alive,"burn retires exactly at authored lifetime");
    }

    for (uint32_t lifetime : {1u,2u}) {
        using namespace tak::sim;
        World fire;fire.setVisPlayer(-1);
        fire.setTerrain(std::vector<uint8_t>(16*16,0),16,16,0);
        FeatType tree;tree.name="spark-tree";tree.flamable=true;tree.hasBurnAnim=true;
        tree.burnTicks=lifetime;tree.spreadChance=100;tree.sparkTicks=0;
        fire.setFeatureTypes({tree});
        fire.addFeature(85,88,88,10,5,1,1,false,0);
        fire.addFeature(86,104,88,10,5,1,1,false,0);
        RetailReplayProbe::ignite(fire,85);
        fire.tick(1.0f/30);
        check(bool(fire.feature(86)->burn)==(lifetime>1),
              lifetime==1 ? "expiry suppresses a simultaneous spark into the adjacent tree"
                          : "a spark before expiry still ignites the adjacent tree");
    }

    World w;
    // Headless: no fog work, no renderer. Matches how the referee runs.
    w.setVisPlayer(-1);

    const uint32_t atStart = w.featGeneration();

    // Ticking an empty world must not move the counter. This is the property that makes
    // the render-side skip worth anything -- if idle ticks bumped it, the sync would
    // take the lock every step exactly as before.
    for (int i = 0; i < 30; ++i) w.tick(1.0f / 30.0f);
    check(w.featGeneration() == atStart,
          "an idle world does not bump the generation (no spurious rescans)");

    // Adding a feature must move it: the renderer has to notice a new feature to give
    // it a visual instance at all.
    w.addFeature(1, 100.0f, 100.0f, 10.0f, 5.0f, 1, 1, false, -1);
    const uint32_t afterOne = w.featGeneration();
    check(afterOne != atStart, "adding a feature bumps the generation");
    check(w.features().size() == 1, "and the feature is actually there");

    // Distinct adds must be distinguishable, not collapsed into one edge.
    w.addFeature(2, 200.0f, 200.0f, 10.0f, 5.0f, 1, 1, false, -1);
    check(w.featGeneration() != afterOne, "a second add bumps it again");

    // ...and ticking after the adds must go quiet again, so the sync settles back to
    // the lock-free path instead of staying permanently dirty.
    const uint32_t afterAdds = w.featGeneration();
    for (int i = 0; i < 30; ++i) w.tick(1.0f / 30.0f);
    check(w.featGeneration() == afterAdds,
          "ticking with features present still does not bump it");

    // Occupancy must follow runtime features, including replacement geometry.
    {
        using namespace tak::sim;
        World map;map.setVisPlayer(-1);
        std::vector<uint8_t> heights(32*32,0);
        std::vector<uint16_t> cells(32*32,0xffff);
        map.setTerrain(heights,32,32,0);
        map.setMapPlacementFeatures(cells,{});
        FeatType statue;statue.name="statue";statue.fx=2;statue.fz=3;statue.blocking=true;
        FeatType stump;stump.name="stump";stump.fx=1;stump.fz=1;stump.blocking=true;
        FeatType wall;wall.name="wall";wall.blocking=true;wall.indestructible=true;
        FeatType cycle=statue;cycle.name="cycle";cycle.deadType=3;cycle.burntType=2;
        map.setFeatureTypes({statue,stump,wall,cycle});
        UnitType mover;mover.footX=mover.footZ=1;mover.maxHp=100;
        mover.maxVel=Fixed::fromInt(1);mover.canMove=true;
        const int unit=map.spawn(&mover,72,72,0,0);
        RetailReplayProbe::cache(map,unit);
        const int emptyGrade=RetailReplayProbe::cached(map,9,10);
        const int id=8*32+8;
        map.addFeature(id,8*16+16,8*16+24,0,60,2,3,true,0);
        check(RetailReplayProbe::grade(map,9,10)==1,
              "a newly created statue blocks its entire rectangular footprint");
        check(RetailReplayProbe::grade(map,10,10)==7,
              "new feature occupancy stops at its footprint edge");
        check(RetailReplayProbe::cached(map,9,10)==1,
              "creation updates an already active search cache");
        RetailReplayProbe::replace(map,id,1);
        check(RetailReplayProbe::cached(map,9,10)==emptyGrade,
              "shrinking a feature refreshes cached grades over its old footprint");
        check(RetailReplayProbe::grade(map,8,8)==1 && RetailReplayProbe::grade(map,9,10)==7,
              "a smaller replacement frees the old footprint tails");
        RetailReplayProbe::replace(map,id,3);
        check(RetailReplayProbe::grade(map,9,10)==0,
              "a cyclic replacement graph with an indestructible successor is not clearable");
        check(RetailReplayProbe::cached(map,9,10)==0,
              "replacement refreshes cached clearability as well as live occupancy");
        RetailReplayProbe::replace(map,id,-1);
        check(RetailReplayProbe::cached(map,9,10)==emptyGrade,
              "removal refreshes the active search cache");
        check(RetailReplayProbe::grade(map,8,8)==7 && RetailReplayProbe::grade(map,9,10)==7,
              "removal clears the expanded footprint");
        map.addFeature(id,8*16+8,8*16+8,0,60,1,1,true,2);
        const auto count=map.features().size();
        map.addFeature(id,8*16+16,8*16+24,0,60,2,3,true,0);
        check(map.features().size()==count && RetailReplayProbe::grade(map,8,8)==0,
              "placement cannot overwrite an indestructible feature");
        const int removable=8*32+7;
        map.addFeature(removable,7*16+8,8*16+8,0,60,1,1,true,1);
        map.addFeature(removable,7*16+16,8*16+24,0,60,2,3,true,0);
        check(!map.feature(removable)->alive && RetailReplayProbe::grade(map,7,8)==7 &&
              RetailReplayProbe::grade(map,8,8)==0,
              "a refused placement retains earlier removals in scan order");
    }

    {
        using namespace tak::sim;
        World map;map.setVisPlayer(-1);
        map.setTerrain(std::vector<uint8_t>(32*32,100),32,32,0);
        map.setMapPlacementFeatures(std::vector<uint16_t>(32*32,0xffff),{});
        UnitType building;building.footX=building.footZ=2;building.maxHp=100;
        building.corpseAdjX=-1;building.corpseAdjZ=2;
        FeatType wreck;wreck.name="wreck";wreck.fx=3;wreck.fz=1;wreck.blocking=true;
        map.setFeatureTypes({wreck});map.mapCorpse(&building,0);
        const int id=map.spawn(&building,176,176,0,0);
        map.unit(id)->hp=Fixed();map.tick(1.0f/30);
        check(!map.unit(id)->corpseBlocks && RetailReplayProbe::grade(map,10,10)==7,
              "a dying building releases its original body footprint");
        for (int i=0;i<120;++i) map.tick(1.0f/30);
        check(map.unit(id)->corpseBlocks && RetailReplayProbe::grade(map,9,12)==1 &&
              RetailReplayProbe::grade(map,11,12)==1 && RetailReplayProbe::grade(map,9,13)==7,
              "the death lifecycle places the wreck's dimensions at the offset unit origin");
        map.unit(id)->corpseUntil=map.unit(id)->deadFor+1;
        map.tick(1.0f/30);
        check(!map.unit(id)->corpseBlocks && RetailReplayProbe::grade(map,11,12)==7,
              "decomposition clears the wreck footprint, including cells outside the old unit");
    }
    {
        using namespace tak::sim;
        World map;map.setVisPlayer(-1);
        map.setTerrain(std::vector<uint8_t>(32*32,100),32,32,0);
        map.setMapPlacementFeatures(std::vector<uint16_t>(32*32,0xffff),{});
        UnitType mover;mover.footX=mover.footZ=1;mover.maxHp=100;
        mover.maxVel=Fixed::fromInt(1);mover.canMove=true;
        FeatType statue;statue.name="frozen";statue.fx=2;statue.fz=3;statue.blocking=true;
        statue.reclaimable=true;
        FeatType wall;wall.name="wall";wall.blocking=true;wall.indestructible=true;
        map.setFeatureTypes({statue,wall});map.mapStatue(&mover,-1,0);
        const int id=map.spawn(&mover,168,168,0,0);
        map.unit(id)->frozenFor=100;map.unit(id)->hp=Fixed();map.tick(1.0f/30);
        check(map.unit(id)->corpseBlocks && RetailReplayProbe::grade(map,11,12)==1,
              "a frozen mobile unit immediately places the statue's full footprint");
        map.addFeature(10*32+10,168,168,0,60,1,1,true,1);
        check(!map.unit(id)->corpseBlocks && RetailReplayProbe::grade(map,11,12)==7,
              "overwriting a statue retires its owner and frees the old footprint");
        RetailReplayProbe::retire(map,id);
        check(RetailReplayProbe::grade(map,10,10)==0,
              "retiring an old corpse cannot remove its replacement");
        const int refused=map.spawn(&mover,168,168,0,0);
        map.unit(refused)->frozenFor=100;map.unit(refused)->hp=Fixed();map.tick(1.0f/30);
        check(!map.unit(refused)->corpseBlocks && RetailReplayProbe::grade(map,10,10)==0,
              "an indestructible obstacle refuses statue placement without being cleared");
        const int reclaimable=map.spawn(&mover,248,168,0,0);
        map.unit(reclaimable)->frozenFor=100;map.unit(reclaimable)->hp=Fixed();map.tick(1.0f/30);
        UnitType builder=mover;builder.canReclaim=true;
        const int worker=map.spawn(&builder,216,168,0,0);
        map.reclaim(worker,-reclaimable,false);
        for (int i=0;i<60 && map.unit(reclaimable)->corpseBlocks;++i) map.tick(1.0f/30);
        check(!map.unit(reclaimable)->corpseBlocks && RetailReplayProbe::grade(map,16,12)==7,
              "ordered corpse reclaim clears the actual statue footprint");
    }

    {
        using namespace tak::sim;
        World map;map.setVisPlayer(-1);
        map.setTerrain(std::vector<uint8_t>(32*32,100),32,32,0);
        map.setMapPlacementFeatures(std::vector<uint16_t>(32*32,0xffff),{});
        UnitType mover;mover.footX=mover.footZ=1;mover.maxHp=100;
        mover.maxVel=Fixed::fromInt(1);mover.canMove=true;
        UnitType victim=mover;victim.corpseAdjZ=6;
        FeatType statue;statue.name="offset statue";statue.blocking=true;
        map.setFeatureTypes({statue});map.mapStatue(&victim,-1,0);
        const int observer=map.spawn(&mover,72,72,0,0);
        const int id=map.spawn(&victim,168,168,0,0);
        RetailReplayProbe::cache(map,observer);
        const int clear=RetailReplayProbe::cached(map,20,10);
        map.unit(id)->frozenFor=100;map.unit(id)->hp=Fixed();map.tick(1.0f/30);
        check(RetailReplayProbe::cached(map,10,10)==clear &&
              RetailReplayProbe::cached(map,10,16)==1,
              "an offset statue clears the old cached body and blocks its new anchor");
    }

    std::printf(g_fail ? "featgen_test: %d FAILURE(S)\n" : "featgen_test: all passed\n",
                g_fail);
    return g_fail ? 1 : 0;
}
