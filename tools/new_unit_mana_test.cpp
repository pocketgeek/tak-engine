// Every new unit starts with an empty mana pool, as retail's 511990 makes it
// (tools/re/check_new_unit_mana.py): every creation -- production, a skirmish
// start, a mission placement, Create, a revive, a change of owner -- runs
// 512060 / 512460 -> 511f60 -> 511990, whose 401000 sets the pool to 0 and
// only its maximum to MaxMana. Nothing else fills it but a placement's
// ManaPercentage (514cd3, default 0), a script's SetAttribute (4d3eb9), the
// ManaMe cheat and regeneration, which waits for the unit to be complete
// (401160: +0x108 == 0). Ours started every caster full.
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/mission.h"
#include "sim/sim.h"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace tak::sim;
static void check(bool ok,const char* what) {if(!ok)throw std::runtime_error(what);}
static UnitType caster() {
    UnitType t;t.id="caster";t.maxHp=651;t.healTime=0;t.sight=300;t.maxMana=150;t.manaRegen=3;
    t.maxVel=Fixed::fromInt(2);t.buildAngle=4096;
    t.weapon.damage=10;t.weapon.range=100;t.weapon.melee=true;t.weapon.reload=1;t.weapon.aimTol=65536;
    t.weapons={t.weapon};return t;
}
static void terrain(World& w) {
    w.setPlayerCount(2);
    w.setTerrain(std::vector<uint8_t>(64*64,50),64,64,0);
}

// tools/re/check_new_unit_mana.py: lines "<maxmana float bits> <maxdamage> <pct>",
// answered "<mana float bits> <hp>" as the placement record and SetAttribute set them.
static int native() {
    uint32_t bits=0;int32_t maxHp=0,pct=0;
    while (std::cin>>bits>>maxHp>>pct) {
        UnitType t=caster();std::memcpy(&t.maxMana,&bits,4);t.maxHp=maxHp;
        const float mana=float(MissionScript::percentMana(t,pct));
        uint32_t out=0;std::memcpy(&out,&mana,4);
        std::cout<<out<<' '<<MissionScript::percentHp(t,pct)<<'\n';
    }
    return 0;
}

// tools/re/check_new_unit_mana.py storage: lines "<flag> <storage bits> <stored
// bits> <capacity bits> <total bits>", answered "<stored> <capacity> <total>"
// bits after a creation whose 512060 passes the flag.
static int storageCredit() {
    uint32_t flag=0,storage=0,stored=0,capacity=0;uint64_t total=0;
    while (std::cin>>flag>>storage>>stored>>capacity>>total) {
        UnitType t=caster();std::memcpy(&t.storage,&storage,4);
        World w;terrain(w);
        auto& r=w.player(0).retailResources.emplace();
        std::memcpy(&r.stored,&stored,4);std::memcpy(&r.capacity,&capacity,4);std::memcpy(&r.totalProduced,&total,8);
        const int id=w.spawn(&t,400,400,0,0);
        if (flag) w.creditCreationStorage(id);
        uint32_t s=0,c=0;uint64_t tt=0;
        std::memcpy(&s,&r.stored,4);std::memcpy(&c,&r.capacity,4);std::memcpy(&tt,&r.totalProduced,8);
        std::cout<<s<<' '<<c<<' '<<tt<<'\n';
    }
    return 0;
}

// The games a shipped install opens (512060's storage credit, S/E): a
// skirmish's players hold their Monarch's mogriumstorage, 5000, and a
// campaign mission's the storage of the units it places. Ours opened every
// skirmish with 2800 and every mission with 500.
static int shipped(const char* root) {
    auto vfs=tak::hpi::mountRetailRoot(root);
    TypeRegistry reg;setupRegistry(reg,vfs,false);
    {
        World w;MatchConfig cfg;cfg.vfs=&vfs;cfg.mapPath=tak::hpi::findMap(vfs,"ulasem arena");
        for (int f=0;f<5;++f) {MatchSlot s;s.used=true;s.faction=f;s.team=f;cfg.slots.push_back(s);}
        setupMatch(w,reg,cfg);
        for (int p=0;p<5;++p) {
            const auto* monarch=reg.find(kMonarchs[p]);
            check(monarch && monarch->storage==5000.f,"a Monarch's mogriumstorage is not 5000");
            if (w.player(p).mana!=5000.0)
                throw std::runtime_error(std::string("a skirmish opens ")+kMonarchs[p]+"'s player with "+
                                         std::to_string(w.player(p).mana)+" mana, not its 5000 storage");
        }
        w.tick(1.f/30.f);
        check(w.player(0).storage==5000.f,"the Monarch's storage is not the opening capacity");
    }
    {
        World w;int human=0;
        check(setupMission(w,reg,vfs,"takmission01_mt",human,nullptr),"takmission01_mt did not load");
        std::vector<double> want(size_t(w.numPlayers()),0.0);
        for (const auto& u:w.units())
            if (u.type && u.player>=0 && u.player<w.numPlayers()) want[size_t(u.player)]+=double(u.type->storage);
        for (int p=0;p<w.numPlayers();++p)
            if (w.player(p).mana!=want[size_t(p)])
                throw std::runtime_error("mission player "+std::to_string(p)+" opens with "+
                    std::to_string(w.player(p).mana)+" mana, not its placed storage "+std::to_string(want[size_t(p)]));
    }
    std::cout<<"shipped: a skirmish opens with 5000 a player, a mission with its placed storage\n";
    return 0;
}

int main(int argc,char** argv) try {
    if (argc==2 && std::string(argv[1])=="--native") return native();
    if (argc==2 && std::string(argv[1])=="--storage") return storageCredit();
    if (argc==3 && std::string(argv[1])=="--data") return shipped(argv[2]);
    const UnitType t=caster();
    {
        // 511990 (E): a new caster's pool is empty. Ours filled it to MaxMana.
        World w;terrain(w);
        const int id=w.spawn(&t,400,400,0,0);
        check(w.unit(id)->mana==0,"a new caster does not start with an empty pool");
        // Regeneration fills it from there: ManaRechargeRate a second.
        for (int i=0;i<30;++i) w.tick(1.f/30.f);
        check(w.unit(id)->mana>2.9 && w.unit(id)->mana<3.1,"an empty pool does not regenerate");
    }
    {
        // 401160 (E): no regeneration while the unit is being built (+0x108 != 0).
        World w;terrain(w);
        UnitType site=t;site.buildTime=1000;site.buildCost=100;
        const int id=w.spawn(&site,400,400,0,0);
        w.unit(id)->underConstruction=true;w.unit(id)->hp=Fixed::fromInt(10);
        for (int i=0;i<60;++i) w.tick(1.f/30.f);
        check(w.unit(id)!=nullptr,"the unit under construction is gone");
        check(w.unit(id)->mana==0,"a unit under construction regenerates mana");
    }
    {
        // 514cb9 / 514cd3 / 4d3e6a / 4d3eb9 (E): HP (maxdamage * pct) / 100 chopped
        // to a whole word; mana MaxMana * pct * 0.01f, capped at MaxMana.
        check(MissionScript::percentHp(t,25)==162,"HealthPercentage 25 of 651 is not 162 hit points");
        check(MissionScript::percentHp(t,100)==651,"HealthPercentage 100 is not whole");
        check(MissionScript::percentMana(t,0)==0,"ManaPercentage 0 is not empty");
        check(MissionScript::percentMana(t,100)==150,"ManaPercentage 100 is not full");
        check(MissionScript::percentMana(t,150)==150,"ManaPercentage over 100 is not capped at MaxMana");
        check(MissionScript::percentMana(t,1)==double(float(150.0*1.0*double(0.01f))),"ManaPercentage 1 rounds differently");
    }
    {
        // 4d3e27 (E): SetAttribute writes only a unit whose object lives; a
        // retired unit keeps what it has.
        World w;terrain(w);
        const int live=w.spawn(&t,400,400,0,0),gone=w.spawn(&t,600,400,0,0);
        w.unit(gone)->hp=Fixed();w.unit(gone)->deathType=1;w.tick(1.f/30.f);
        check(!w.unit(gone)->alive() && !w.unit(gone)->deathPending,"the fixture did not retire");
        TypeRegistry registry;
        MissionScript mission(std::vector<uint8_t>{},tak::tdf::Node{},registry,0,"setattribute fixture");
        mission.command(w,"SetAttribute HealthPercentage",{live,25});
        mission.command(w,"SetAttribute HealthPercentage",{gone,25});
        mission.command(w,"SetAttribute ManaPercentage",{gone,100});
        check(w.unit(live)->hp==Fixed::fromInt(162),"SetAttribute did not set a live unit's hit points");
        check(w.unit(gone)->hp<=Fixed() && w.unit(gone)->mana==0,"SetAttribute wrote a retired unit");
    }
    {
        // 512060's storage credit (5123ce, E): a CRT Create adds the new unit's
        // mogriumstorage to its owner's pool and capacity; production does not.
        World w;terrain(w);
        UnitType keep=t;keep.id="keep";keep.storage=300;keep.maxVel=Fixed();
        w.player(0).mana=10;
        const int made=w.scenarioCreate(&keep,0,10,10,12,12);
        check(made && w.player(0).mana==310,"a CRT Create did not credit the unit's storage");
        check(w.player(0).storage==300.f,"a CRT Create did not raise the capacity at once");
        w.spawn(&keep,600,600,0,0);
        check(w.player(0).mana==310,"a plain creation credited storage");
    }
    std::cout<<"all passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr<<"FAIL: "<<e.what()<<'\n';
    return 1;
}
