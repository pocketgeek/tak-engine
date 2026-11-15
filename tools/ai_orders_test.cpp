// Our levels' army orders, as a real ai::Controller's commands on constructed
// worlds: a unit whose queue still holds the attack it was given is left alone;
// an attack the sim would refuse is not ordered; and the side stays the seat's
// when its Monarch is gone.
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

// An Attack is ordered only where the sim would take it (World::canAttackTarget):
// a charm takes a finished mobile unit that is not immune, with its side below
// the unit cap; a stone weapon one that can be stoned. Otherwise the unit
// fight-moves to the site and takes what it can there.
static void refusedAttacks(const sim::TypeRegistry& registry,const ai::Profile& profile) {
    auto harpy=fighter();harpy.id="harpy";harpy.buildCost=1000;
    harpy.weapon.damage=1;harpy.weapon.mindControl=true;harpy.weapons={harpy.weapon};
    auto basilisk=fighter();basilisk.id="basilisk";basilisk.buildCost=1000;
    basilisk.weapon.status=sim::Weapon::Status::Stoned;basilisk.weapons={basilisk.weapon};
    auto troll=fighter();troll.id="troll";troll.weapon.damage=0;troll.weapons.clear();troll.buildCost=100;
    auto immune=troll;immune.id="immune";immune.cantBeCaptured=true;immune.cantBeStoned=true;
    const auto lode=lodestone();
    // The one order `caster` gets with `victim` 100 px from it.
    auto order=[&](const sim::UnitType& caster,const sim::UnitType& victim,int unitCap) {
        sim::World w;terrain(w);w.player(0).income=100;
        if (unitCap) w.setUnitCap(unitCap);
        const int id=w.spawn(&caster,1000,1000,0,0);
        const int target=w.spawn(&victim,1100,1000,0,1);
        pastStart(w);
        ai::Controller controller(0,registry,profile,1,ai::Difficulty::Normal,{{3400,800}});
        const auto cs=think(controller,w,0);
        if (cs.size()!=1 || cs.front().unitId!=id) return 0;
        return cs.front().kind==net::Cmd::Attack && cs.front().targetId==target ? 1 :
               cs.front().kind==net::Cmd::AttackMove ? 2 : 0;
    };
    check(order(harpy,troll,0)==1,"a Harpy is ordered to charm a mobile unit beside it");
    check(order(harpy,lode,0)==2,"a Harpy beside a lodestone fight-moves there: a charm takes no building");
    check(order(harpy,immune,0)==2,"... nor a unit that cannot be captured");
    check(order(harpy,troll,1)==2,"... nor anything while its side is at the unit cap");
    check(order(basilisk,troll,0)==1,"a Basilisk is ordered to stone a mobile unit beside it");
    check(order(basilisk,lode,0)==2,"a Basilisk beside a lodestone fight-moves there: nothing stones a building");
    check(order(basilisk,immune,0)==2,"... nor a unit that cannot be stoned");
    check(order(fighter(),lode,0)==1 && order(fighter(),immune,1)==1,
          "a unit whose weapon does damage attacks either, at the unit cap too");
}

// The seat's faction is its Monarch's, for the whole game. With the Monarch dead
// it used to be read off the lowest-numbered mobile builder -- which a captured
// one can be: a Zhon seat then counted as a side with factory buildings, and
// stopped replacing the producers that are its factories.
struct Roster {
    std::shared_ptr<hpi::Vfs::Files> files=std::make_shared<hpi::Vfs::Files>();
    hpi::Vfs vfs;
    sim::TypeRegistry reg;
    ai::Profile profile;
    void file(const std::string& path,const std::string& text) {
        std::string lines;
        for (char ch : text) { lines += ch; if (ch == ';' || ch == '{' || ch == '}') lines += '\n'; }
        (*files)[path]={lines.begin(),lines.end()};
    }
    void type(const std::string& id,const std::string& fields) {
        file("units/"+id+".fbi","[UNITINFO]{unitname="+id+";objectname="+id+
            ";maxdamage=100;footprintx=2;footprintz=2;buildtime=30;buildcost=10;sightdistance=600;"+fields+"}");
    }
    const sim::UnitType* operator()(const char* id) const {return reg.find(id);}
    Roster() {
        const std::string mobile="canmove=1;maxvelocity=1;";
        // ARA has a factory building; ZON has none: its handlers and tamers are its factories.
        type("arakeep","side=ARA;builder=1;workertime=30;");
        type("aramason","side=ARA;builder=1;workertime=30;"+mobile);
        type("arasword","side=ARA;"+mobile);
        file("canbuild/arakeep/arasword.tdf","[CANBUILD]{sortbias=0;}");
        file("canbuild/aramason/arakeep.tdf","[CANBUILD]{sortbias=0;}");
        type("zonking","side=ZON;builder=1;commander=1;workertime=30;"+mobile);
        type("zongod","side=ZON;"+mobile);
        type("zonhand","side=ZON;builder=1;workertime=30;"+mobile);
        type("zontame","side=ZON;builder=1;workertime=30;"+mobile);
        type("zongob","side=ZON;"+mobile);
        file("canbuild/zonking/zonhand.tdf","[CANBUILD]{sortbias=0;}");
        file("canbuild/zonhand/zontame.tdf","[CANBUILD]{sortbias=0;}");
        file("canbuild/zonhand/zongob.tdf","[CANBUILD]{sortbias=1;}");
        file("canbuild/zontame/zongob.tdf","[CANBUILD]{sortbias=0;}");
        vfs.setMapFiles(files);
        reg.loadDir(vfs,"units");reg.loadBuildTree(vfs,"canbuild");
        for (const char* id : {"arasword","zongob"}) {
            auto* t=const_cast<sim::UnitType*>(reg.find(id));
            t->weapon.damage=10;t->weapon.range=120;t->weapons={t->weapon};
        }
        for (const char* id : {"arakeep","aramason","arasword","zonhand","zontame","zongob"}) profile.weight[id]=10;
    }
};
static void seatSide() {
    Roster r;
    // Orders to conjure a producer: the Monarch's Beast Handler, a handler's Tamer.
    auto producers=[](const std::vector<net::Command>& cs) {
        return int(std::count_if(cs.begin(),cs.end(),[](const auto& c) {
            return c.kind==net::Cmd::Build && (std::string(c.type)=="zonhand" || std::string(c.type)=="zontame");
        }));
    };
    // A Zhon seat on 100 income wants six producers and has four. `captured`: an
    // Aramon constructor with a lower unit id than any of them is the seat's too.
    struct Seat { sim::World w; int monarch=0; };
    auto seat=[&](Seat& s,bool captured,bool monarch) {
        terrain(s.w);s.w.player(0).income=100;s.w.player(0).mana=5000;
        if (captured) s.w.spawn(r("aramason"),700,1000,0,0);
        if (monarch) s.monarch=s.w.spawn(r("zonking"),1000,1000,0,0);
        for (int i=0;i<4;++i) s.w.spawn(r("zonhand"),1000+float(i)*200,1400,0,0);
        for (int i=0;i<14;++i) s.w.spawn(r("zongob"),1000+float(i)*40,1800,0,0);
        pastStart(s.w);
    };
    {
        Seat s;seat(s,true,true);
        ai::Controller controller(0,r.reg,r.profile,1,ai::Difficulty::Normal);
        check(producers(think(controller,s.w,0))>0,"a Zhon seat short of producers conjures more");
        s.w.unit(s.monarch)->deadFor=0;
        check(producers(think(controller,s.w,30))>0,
              "with its Monarch dead and a captured constructor its lowest-numbered builder, it still does: the side is the seat's");
    }
    {
        // The Monarch was dead before this AI took the seat: the god the match
        // setup resolved from it tells the faction.
        Seat s;seat(s,true,false);
        s.w.player(0).godType=r("zongod");
        ai::Controller controller(0,r.reg,r.profile,1,ai::Difficulty::Normal);
        check(producers(think(controller,s.w,0))>0,"an AI taking a Zhon seat whose Monarch is dead knows the side by the seat's god");
    }
    {
        // Controls: no captured builder; and nothing to tell the seat by (no
        // Monarch seen, no god), where the builders' side is read as before.
        Seat s;seat(s,false,true);
        ai::Controller controller(0,r.reg,r.profile,1,ai::Difficulty::Normal);
        think(controller,s.w,0);
        s.w.unit(s.monarch)->deadFor=0;
        check(producers(think(controller,s.w,30))>0,"a Zhon seat with no captured builder conjures producers after its Monarch as before");
        Seat t;seat(t,true,false);
        ai::Controller unknown(0,r.reg,r.profile,1,ai::Difficulty::Normal);
        check(producers(think(unknown,t.w,0))==0,"with neither Monarch nor god to tell the seat by, the lowest-numbered builder's side is read");
    }
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
    refusedAttacks(empty,profile);
    seatSide();
    std::printf("%d failed\n",fails);
    return fails ? 1 : 0;
}
