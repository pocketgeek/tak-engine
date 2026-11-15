// Our levels' army orders, as a real ai::Controller's commands on constructed
// worlds: a unit whose queue still holds the attack it was given is left alone.
#include "ai/ai.h"
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using namespace tak;
static int fails=0;
static void check(bool ok,const char* label) {
    std::printf("[%s] %s\n",ok ? "PASS" : "FAIL",label);if (!ok) ++fails;
}
static sim::UnitType fighter() {
    sim::UnitType t;t.id="fighter";t.maxHp=100;t.maxVel=sim::Fixed::fromInt(2);
    t.canMove=true;t.footX=t.footZ=2;t.turnRate=t.turnInPlaceRate=1000;
    t.weapon.damage=10;t.weapon.range=120;t.weapons={t.weapon};
    t.defaultMove=1;t.defaultFire=2;t.buildTime=1;t.buildMovementCode=1;return t;
}
// A building that pays mana and never falls: what a strike goes for.
static sim::UnitType lodestone() {
    auto t=fighter();t.id="lode";t.maxVel={};t.canMove=false;t.onMana=true;t.buildMovementCode=0;
    t.weapon.damage=0;t.weapons.clear();t.buildCost=300;t.maxHp=30000;return t;
}
// A flat 4096 px map, FULL VISION: every unit is in sight, ticked or not.
static void terrain(sim::World& w) {
    w.setTerrain(std::vector<uint8_t>(256*256,100),256,256,20);
    w.setRoomVision(true,true);
}
static void pastStart(sim::World& w) {w.resumeClocks(1,w.gameRngState());}
static std::vector<net::Command> think(ai::Controller& ai,sim::World& w,uint32_t tick) {
    std::vector<net::Command> out;ai.tick(w,tick,[&](const auto& c){out.push_back(c);});return out;
}
static void apply(sim::World& w,const sim::TypeRegistry& registry,const std::vector<net::Command>& cs) {
    for (const auto& c:cs) sim::applyCommand(w,registry,c);
}
// The sim puts legs of its own in front of an attack: the walk out of a weapon's
// minimum range, or to a clear shot (World::repositionForShot inserts a plain
// move at the head of the queue). The attack behind it is still the unit's
// order, and the AI does not send it again.
static void heldAttack(const sim::TypeRegistry& registry,const ai::Profile& profile) {
    const auto soldier=fighter(),lode=lodestone();
    sim::World w;terrain(w);w.player(0).income=100;
    const int id=w.spawn(&soldier,1000,1000,0,0);
    const int target=w.spawn(&lode,1100,1000,0,1);
    pastStart(w);
    ai::Controller controller(0,registry,profile,1,ai::Difficulty::Normal,{{3400,800}});
    auto cs=think(controller,w,0);
    check(cs.size()==1 && cs.front().kind==net::Cmd::Attack && cs.front().unitId==id && cs.front().targetId==target,
          "a fighter beside the enemy's lodestone is ordered to attack it");
    apply(w,registry,cs);
    // The back-off leg, as repositionForShot builds it.
    sim::Order step{sim::Fixed::fromInt(900),sim::Fixed::fromInt(1000),0};
    step.goal=true;step.groundMission=true;
    auto& orders=w.unit(id)->orders;
    orders.insert(orders.begin(),step);
    int again=0;
    for (uint32_t tick=30;tick<=300;tick+=30) again+=int(think(controller,w,tick).size());
    check(again==0,"a unit walking to a firing spot in front of the attack it was given is not ordered again");
    // The walk done, the attack leads again: still nothing to add.
    orders.erase(orders.begin());
    check(think(controller,w,330).empty(),"... nor while it attacks");
    // A unit on a plain move alone is free, as before.
    w.stop(id);
    w.order(id,900,1000,false);
    cs=think(controller,w,360);
    check(cs.size()==1 && cs.front().kind==net::Cmd::Attack && cs.front().unitId==id,
          "a striker on a plain move with no attack behind it is free for its next order");
}

// The audited defect itself, on the shipped data (Crusades balance): a Veruna
// Catapult ordered onto a unit inside its minimum range walks out to fire. The
// AI used to send the Attack again on every think of the walk, the sim's three
// walks were spent in two seconds and every later order was dropped: no shot.
static void catapultBackOff(const char* dataRoot) {
    auto vfs=hpi::mountRetailRoot(dataRoot);
    sim::TypeRegistry reg;sim::setupRegistry(reg,vfs,true);
    const auto* catapult=reg.find("verpult");
    const auto* tamer=reg.find("zontrain");
    if (!catapult || !tamer || catapult->weapons.empty() || catapult->weapons.front().minRange<=0) {
        check(false,"the Crusades roster has a Veruna Catapult with a minimum range");
        return;
    }
    // A Beast Tamer that neither moves nor fights nor falls.
    auto victim=*tamer;victim.maxVel={};victim.weapon.damage=0;victim.weapons.clear();victim.maxHp=30000;
    for (auto mode:{sim::PathfindingMode::Retail,sim::PathfindingMode::Legion}) {
        const char* name=mode==sim::PathfindingMode::Retail ? "Retail pathfinding" : "Legion pathfinding";
        char label[200];
        auto say=[&](const char* what) {std::snprintf(label,sizeof label,"%s: %s",name,what);return label;};
        sim::World w;
        w.setPlayerCount(2);
        w.setTerrain(std::vector<uint8_t>(256*256,100),256,256,20);
        w.setMapPlacementFeatures(std::vector<uint16_t>(256*256,0xffff),{});
        w.buildNavClasses(reg);w.setPathService(true);w.setPathfindingMode(mode);
        w.setRoomVision(true,true);
        w.player(0).income=100;
        const int id=w.spawn(catapult,2000,2000,0,0);
        for (int i=0;i<30;++i) w.tick(1.f/30.f);
        const int target=w.spawn(&victim,2000+float(catapult->weapons.front().minRange)/2,2000,0,1);
        ai::Profile profile;
        ai::Controller controller(0,reg,profile,1,ai::Difficulty::Normal,{{3400,800}});
        int attacks=0,shots=0,walks=0;
        for (uint32_t tick=30;tick<30+1500;++tick) {
            const auto cs=think(controller,w,tick);
            for (const auto& c:cs) if (c.unitId==id && c.kind==net::Cmd::Attack && c.targetId==target) ++attacks;
            apply(w,reg,cs);
            w.tick(1.f/30.f);
            const auto* u=w.unit(id);
            if (u->justFired) ++shots;
            walks=std::max(walks,int(u->repositions));
        }
        std::printf("  %s: %d Attack orders, %d walks, %d shots, target %.0f of 30000 hp\n",name,attacks,walks,shots,
                    w.unit(target)->hp.toFloat());
        check(attacks==1,say("the Catapult is ordered onto the unit inside its minimum range once"));
        check(walks==1 && shots>0 && w.unit(target)->hp.toFloat()<30000,say("it backs off once and fires"));
    }
}

int main(int argc,char** argv) {
    if (argc>1) {
        catapultBackOff(argv[1]);
        std::printf("%d failed\n",fails);
        return fails ? 1 : 0;
    }
    sim::TypeRegistry empty;ai::Profile profile;
    heldAttack(empty,profile);
    std::printf("%d failed\n",fails);
    return fails ? 1 : 0;
}
