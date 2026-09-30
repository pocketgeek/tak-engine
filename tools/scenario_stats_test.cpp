// Authored stat effects and the separate nonparticipant scenery owner.
#include "sim/sim.h"
#include <iostream>
#include <stdexcept>
using namespace tak::sim;
static void check(bool ok,const char* what) {if(!ok)throw std::runtime_error(what);}
static UnitType fighter() {
    UnitType t;t.id="fighter";t.maxHp=2000;t.healTime=0;t.sight=600;
    t.weapon.damage=40;t.weapon.range=100;t.weapon.melee=true;
    t.weapon.reload=1;t.weapon.aimTol=65536;t.weapons={t.weapon};return t;
}
static void terrain(World& w) {
    w.setPlayerCount(2);w.setVisPlayer(-1);
    w.setTerrain(std::vector<uint8_t>(64*64,50),64,64,0);
}
static int oneHit(float attack,float armor) {
    auto t=fighter(),victim=t;victim.id="victim";victim.weapon.damage=0;victim.weapons.clear();
    World w;terrain(w);int a=w.spawn(&t,400,400,0,0),b=w.spawn(&victim,440,400,0,1);
    w.unit(a)->scenarioWeapon=attack;w.unit(b)->scenarioArmor=armor;
    w.attack(a,b,false);
    bool fired=false;
    for(int i=0;i<120 && !fired;++i) {w.tick(1.f/30);fired=w.unit(a)->justFired;}
    check(fired,"synthetic attacker never fired");
    check(w.unit(a)->scenarioWeapon==attack && w.unit(b)->scenarioArmor==armor,"aura relaxation erased authored factors");
    return victim.maxHp-w.unit(b)->hp.floorInt();
}
int main() try {
    const int base=oneHit(1,1),boost=oneHit(1.5f,1),armored=oneHit(1,2);
    check(base>0 && boost>base && armored<base && armored>0,"authored attack/armor factors do not affect damage");
    check(oneHit(0,1)==0,"zero authored weapon percentage still dealt damage");
    check(oneHit(1.5f,1.5f)==base,"equal attack and armor factors did not cancel");
    {
        auto t=fighter();World w;terrain(w);w.enableScenarioNeutralPlayer();
        int a=w.spawn(&t,400,400,0,0),n=w.spawn(&t,440,400,0,8);
        for(int i=0;i<120;++i)w.tick(1.f/30);
        check(w.unit(a)->hp.floorInt()==t.maxHp && w.unit(n)->hp.floorInt()==t.maxHp,
              "neutral units or nearby players auto-acquired each other");
        check(w.numPlayers()==2 && w.player(0).unitCount==1 && w.player(8).unitCount==1,
              "neutral units contaminated a participant's count");
        w.attack(a,n,false);for(int i=0;i<120 && w.unit(n)->hp.floorInt()==t.maxHp;++i)w.tick(1.f/30);
        check(w.unit(n)->hp.floorInt()<t.maxHp,"explicit attack on a neutral unit failed");
    }
    {
        auto t=fighter(),victim=t;t.weapon.mindControl=true;t.weapons={t.weapon};
        victim.id="victim";victim.weapon.damage=0;victim.weapons.clear();
        World w;terrain(w);w.enableScenarioNeutralPlayer();
        int a=w.spawn(&t,400,400,0,0),n=w.spawn(&victim,440,400,0,8);
        w.attack(a,n,false);
        for(int i=0;i<600 && w.unit(n)->player==8;++i)w.tick(1.f/30);
        check(w.unit(n)->player==0 && w.player(8).unitCount==0 && w.player(0).unitCount==2,
              "neutral capture failed or left stale ownership counts");
    }
    {
        auto t=fighter();World a,b;terrain(a);terrain(b);
        a.setScenarioTypeStats(t.id,200,150,7);b.setScenarioTypeStats(t.id,200,150,7);
        const int id=a.spawn(&t,400,400,0,0);b.spawn(&t,400,400,0,0);
        check(a.unit(id)->veteran==7 && a.unit(id)->xp==7,"authored veteran did not initialize experience");
        check(a.stateHash()==b.stateHash(),"same stat configuration diverged");
        b.unit(id)->scenarioWeapon=.5f;check(a.stateHash()!=b.stateHash(),"per-unit stat factor absent from lockstep hash");
    }
    {
        auto t=fighter(),builder=t; t.buildTime=1;t.buildCost=0;
        builder.id="builder";builder.isBuilder=true;builder.canMove=true;builder.maxVel=Fixed::fromInt(1);builder.buildDist=200;
        builder.weapon.damage=0;builder.weapons.clear();
        World w;terrain(w);w.setScenarioTypeStats(t.id,200,150,7);
        int a=w.spawn(&builder,400,400,0,0),b=w.spawn(&t,440,400,0,0);
        w.unit(b)->hp=Fixed::fromInt(100);w.repair(a,b,false);
        for(int i=0;i<300 && w.unit(b)->hp.floorInt()<t.maxHp;++i)w.tick(1.f/30);
        check(w.unit(b)->hp.floorInt()==t.maxHp && w.unit(b)->maximumHp()==t.maxHp,
              "repair did not use original type maximum HP");
        check(w.unit(b)->scenarioArmor==2.f && w.unit(b)->scenarioWeapon==1.5f && w.unit(b)->veteran==7,
              "repair or aura tick erased authored stats");
    }
    {
        auto t=fighter();t.noVeteran=true;World w;terrain(w);
        w.setScenarioTypeStats(t.id,100,100,7);
        const int id=w.spawn(&t,400,400,0,0);
        check(w.unit(id)->veteran==0 && w.unit(id)->xp==0,"noveteran unit accepted scenario veteran default");
    }
    std::cout<<"PASS: authored combat percentages, neutral targeting/capture and state hashing\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
