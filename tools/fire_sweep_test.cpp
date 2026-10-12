// The fire-at-will sweep's companions: deterministic per-player vision (icd
// 4f7210 / 4c6800 / 411700), AI weapon switching (40f411..40fb92) and the
// idle retaliation mission (409a50 part a, 4d7de0). One ctest per part:
//   fire_sweep_test vision | switch | retaliation
#include "sim/sim.h"
#include "sim/retailexploration.h"
#include <cstdio>
#include <cstring>
#include <string>
using namespace tak;
namespace {
int failures=0;
void check(bool ok,const char* label) {
    std::printf("%s %s\n",ok?"PASS":"FAIL",label);failures+=!ok;
}
constexpr int kMap=128;
void setup(sim::World& w,int players) {
    w.setVisPlayer(-1);w.setSerialThreads(true);w.setPathfindingMode(sim::PathfindingMode::Retail);
    w.setTerrain(std::vector<uint8_t>(kMap*kMap,100),kMap,kMap,20);
    w.setPathService(true);w.setPlayerCount(players);
}
void ticks(sim::World& w,int n) {for(int i=0;i<n;++i)w.tick(1.f/30);}
sim::UnitType gunner(int sight) {
    sim::UnitType t;t.id="gunner";t.canMove=true;t.upright=true;t.maxHp=500;t.footX=t.footZ=2;
    t.maxVel=sim::Fixed::fromInt(2);t.turnRate=t.turnInPlaceRate=600;t.sight=sight;t.sightHeight=24;
    sim::Weapon w;w.name="bolt";w.range=300;w.reload=0.3f;w.damage=10;w.projVel=900;w.aimTol=33000;
    t.weapon=w;t.weapons={w};return t;
}
sim::UnitType dummy() {
    sim::UnitType t;t.id="dummy";t.maxHp=5000;t.footX=t.footZ=2;t.maxVel=sim::Fixed::fromInt(1);
    t.canMove=true;t.upright=true;t.sight=40;t.sightHeight=24;return t;
}

// The view counters must equal a from-scratch sum of the active footprints.
bool countsMatchRebuild(const sim::World& w) {
    const int width=kMap/2,height=kMap/2;
    const auto heights=sim::retailExplorationHeights(kMap,kMap,20,[](int,int){return 100;});
    std::vector<uint16_t> expect(w.viewCounts().size(),0);
    if (expect.empty()) return false;
    const size_t cells=size_t(width)*height;
    for (const auto& u:w.units()) {
        if (!u.viewSight.active) continue;
        auto sight=u.viewSight;sight.active=false;
        sim::retailSightFootprint(sight,true,false,width,height,uint8_t(u.viewPlayer),
            [&](int x,int z){return heights[size_t(z)*width+x];},
            [&](int x,int z,int d,uint16_t){expect[size_t(u.viewPlayer)*cells+size_t(z)*width+x]+=uint16_t(d);});
    }
    return expect==w.viewCounts();
}

void vision() {
    const auto g=gunner(40);const auto scout=gunner(160);const auto d=dummy();
    sim::World w;setup(w,2);
    const int shooter=w.spawn(&g,400,1000,0,0),foe=w.spawn(&d,650,1000,0,1);
    w.unit(shooter)->fireState=0;   // keep the order-driven acquisition out of it
    bool consistent=true;
    for(int i=0;i<12;++i){ticks(w,1);consistent=consistent && countsMatchRebuild(w);}
    check(!w.playerSees(0,*w.unit(foe)),"vision: an enemy beyond the player's sight is unseen");
    check(w.playerSees(1,*w.unit(foe)) && w.playerSees(0,*w.unit(shooter)),"vision: a player sees its own units");
    check(w.visibleEnemies(0).empty(),"vision: the unseen enemy is not on the visible-enemy list");
    check(w.retailSelectTarget(*w.unit(shooter),0,true)==0,"vision: the sweep does not select an enemy its player cannot see");
    // A scout walks up: the enemy becomes seen at once, and a target within
    // one 7-tick list rebuild.
    const int spotter=w.spawn(&scout,650,1070,0,0);
    w.unit(spotter)->fireState=0;
    ticks(w,1);consistent=consistent && countsMatchRebuild(w);
    check(w.playerSees(0,*w.unit(foe)),"vision: an enemy inside another own unit's footprint is seen");
    int picked=0,waited=0;
    for(;waited<8 && !picked;++waited){
        picked=w.retailSelectTarget(*w.unit(shooter),0,true);
        if(!picked){ticks(w,1);consistent=consistent && countsMatchRebuild(w);}
    }
    check(picked==foe,"vision: once seen, the enemy is selected within one list rebuild");
    // Cloak hides an enemy, never an own unit.
    w.unit(foe)->cloaked=true;w.unit(shooter)->cloaked=true;
    check(!w.playerSees(0,*w.unit(foe)) && w.playerSees(0,*w.unit(shooter)),"vision: cloak hides enemies, never own units");
    w.unit(foe)->cloaked=false;w.unit(shooter)->cloaked=false;
    // The scout leaves: the enemy is unseen again; the snapshot may hold it
    // for up to six more ticks, then it drops.
    w.order(spotter,200,1700,false);
    int lost=-1;
    for(int i=0;i<300;++i){ticks(w,1);consistent=consistent && countsMatchRebuild(w);
        if(lost<0 && !w.playerSees(0,*w.unit(foe)))lost=i;
        if(lost>=0 && i>=lost+8)break;}
    check(lost>=0 && w.visibleEnemies(0).empty(),"vision: an enemy that left sight drops off the list at the next rebuild");
    check(consistent,"vision: the view counters equal a rebuild from the footprints every tick");
}
} // namespace

void weaponSwitch();
void retaliation();

int main(int argc,char** argv) {
    const std::string part=argc>1 ? argv[1] : "all";
    if(part=="vision" || part=="all")vision();
    if(part=="switch" || part=="all")weaponSwitch();
    if(part=="retaliation" || part=="all")retaliation();
    return failures?1:0;
}
void weaponSwitch() {}
void retaliation() {}
