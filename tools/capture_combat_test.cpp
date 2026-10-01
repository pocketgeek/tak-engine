// Capture attacks must reject targets their impact/contact conversion cannot take.
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include <algorithm>
#include <cstdio>

namespace {
int failures=0;
void check(bool ok,const char* message) {
    std::printf("[%s] %s\n",ok?"PASS":"FAIL",message);
    if(!ok)++failures;
}
tak::sim::UnitType victimType() {
    tak::sim::UnitType t;t.id="victim";t.maxHp=10000;t.canMove=true;
    return t;
}
tak::sim::UnitType casterType(bool contact) {
    auto t=victimType();t.id="caster";t.canCapture=contact;
    tak::sim::Weapon w;w.damage=1;w.range=400;w.reload=0.1f;
    w.mindControl=true;w.kind=tak::sim::Weapon::Kind::Remote;
    w.remote=tak::sim::Weapon::RemoteKind::MindCtl;w.unitsOnly=true;w.aoe=32;w.buildUpTicks=8;
    t.weapon=w;t.weapons={w};return t;
}
void setup(tak::sim::World& w) {
    w.setVisPlayer(-1);w.setPlayerCount(3);w.setTeam(2,0);
    w.setTerrain(std::vector<uint8_t>(128*128,100),128,128,20);
}
bool targets(const tak::sim::Unit& unit,int id) {
    return std::any_of(unit.orders.begin(),unit.orders.end(),
                       [&](const auto& order){return order.targetId==id;});
}

void rejectedTargets(const tak::sim::UnitType& caster) {
    for(int reason=0;reason<6;++reason) {
        auto victim=victimType();
        victim.commander=reason==0;victim.cantBeCaptured=reason==1;
        tak::sim::World w;setup(w);
        const int a=w.spawn(&caster,1000,1000,0,0);
        const int b=w.spawn(&victim,1000,1100,0,reason==5?2:1);
        if(reason==2)w.unit(b)->inTransport=a;
        if(reason==3)w.unit(b)->stonedFor=1000;
        if(reason==4)w.setUnitCap(1);
        w.unit(a)->fireState=2;w.unit(a)->moveState=2;
        w.attack(a,b,false);
        check(!targets(*w.unit(a),b),"explicit capture rejects an ineligible target");
        bool fired=false,aimed=false;
        for(int tick=0;tick<120;++tick) {
            w.tick(1.f/30);
            fired|=w.unit(a)->justFired;
            aimed|=targets(*w.unit(a),b) || w.unit(a)->scriptAimTarget==b;
        }
        check(!fired && !aimed && !w.unit(a)->captureProg,
              "automatic capture does not acquire, fire or channel against immunity/capacity");
        check(w.unit(b)->player==(reason==5?2:1),"rejected capture preserves ownership");
    }
}
}

int main(int argc,char** argv) {
    if(argc>2)return 2;
    for(bool contact:{false,true}) {
        auto caster=casterType(contact);rejectedTargets(caster);
        // A nearer immune enemy must not starve a farther eligible target.
        {
            auto immune=victimType(),eligible=victimType();immune.commander=true;
            tak::sim::World w;setup(w);
            const int a=w.spawn(&caster,1000,1000,0,0);
            const int blocked=w.spawn(&immune,1000,1100,0,1);
            const int b=w.spawn(&eligible,1000,1200,0,1);
            w.unit(a)->fireState=2;w.unit(a)->moveState=2;
            bool wrong=false;
            for(int tick=0;tick<300 && w.unit(b)->player!=0;++tick) {
                w.tick(1.f/30);wrong|=targets(*w.unit(a),blocked);
            }
            check(!wrong && w.unit(b)->player==0,"capture skips nearer immunity and converts an eligible enemy");
            check(w.unit(blocked)->player==1,"capture does not steal a monarch through contact fallback");
        }
        // Eligibility changes after an attack was accepted. Preserve its queued
        // destination while clearing aim/channel state before another release.
        for(bool capacity:{false,true}) {
            auto victim=victimType();tak::sim::World w;setup(w);
            const int a=w.spawn(&caster,1000,1000,0,0);
            const int b=w.spawn(&victim,1000,1100,0,1);
            w.unit(a)->fireState=0;w.attack(a,b,false);w.tick(1.f/30);
            check(targets(*w.unit(a),b),"eligible attack remains active before eligibility changes");
            w.order(a,1500,1500,true);
            if(capacity)w.setUnitCap(1);else victim.cantBeCaptured=true;
            w.tick(1.f/30);
            check(!targets(*w.unit(a),b) && !w.unit(a)->scriptAimTarget && !w.unit(a)->captureProg && !w.unit(a)->justFired,
                  "new immunity or full capacity retires an active capture attack");
            check(!w.unit(a)->orders.empty(),"capture retirement retains queued movement");
        }
    }
    // The active spell's category table determines eligibility, even if a
    // different selectable spell could capture this unit.
    {
        auto caster=casterType(false),victim=victimType();victim.damageCategory="immune";
        caster.weaponSwitching=true;caster.weapons.push_back(caster.weapon);
        caster.weapons[0].dmgVs["immune"]=0;
        tak::sim::World w;setup(w);
        const int a=w.spawn(&caster,1000,1000,0,0),b=w.spawn(&victim,1000,1100,0,1);
        w.unit(a)->fireState=2;w.attack(a,b,false);
        for(int tick=0;tick<30;++tick)w.tick(1.f/30);
        check(!targets(*w.unit(a),b) && w.unit(b)->player==1,"inactive spell cannot make a category-immune target eligible");
        w.setWeapon(a,1);w.attack(a,b,false);
        for(int tick=0;tick<300 && w.unit(b)->player!=0;++tick)w.tick(1.f/30);
        check(w.unit(b)->player==0,"switching to an eligible capture spell works");
    }
    // Ordinary weapons retain explicit friendly fire.
    {
        auto caster=casterType(false),victim=victimType();
        caster.weapons[0].mindControl=false;caster.weapons[0].remote=tak::sim::Weapon::RemoteKind::Plain;
        caster.weapon=caster.weapons[0];tak::sim::World w;setup(w);
        const int a=w.spawn(&caster,1000,1000,0,0),b=w.spawn(&victim,1000,1100,0,0);
        w.unit(a)->fireState=0;w.attack(a,b,false);w.tick(1.f/30);
        check(w.unit(a)->justFired,"ordinary explicit attacks still allow friendly fire");
    }
    if(argc==2) {
        auto vfs=tak::hpi::mountRetailRoot(argv[1]);
        for(bool crusades:{false,true}) {
            tak::sim::TypeRegistry registry;tak::sim::setupRegistry(registry,vfs,crusades);
            int count=0;
            for(const auto& [name,type]:registry.types()) {
                if(!type.canCapture && std::none_of(type.weapons.begin(),type.weapons.end(),
                    [](const auto& weapon){return weapon.mindControl;}))continue;
                std::printf("capture roster balance=%d unit=%s\n",crusades,name.c_str());
                rejectedTargets(type);++count;
            }
            check(count>=3,"shipped capture roster includes Harpy, Mind Mage and campaign caster");
        }
    }
    return failures?1:0;
}
