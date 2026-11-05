// Every new unit starts with an empty mana pool, as retail's 511990 makes it
// (tools/re/check_new_unit_mana.py): every creation -- production, a skirmish
// start, a mission placement, Create, a revive, a change of owner -- runs
// 512060 / 512460 -> 511f60 -> 511990, whose 401000 sets the pool to 0 and
// only its maximum to MaxMana. Nothing else fills it but a placement's
// ManaPercentage (514cd3, default 0), a script's SetAttribute (4d3eb9), the
// ManaMe cheat and regeneration, which waits for the unit to be complete
// (401160: +0x108 == 0). Ours started every caster full.
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

int main(int argc,char** argv) try {
    if (argc==2 && std::string(argv[1])=="--native") return native();
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
    std::cout<<"all passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr<<"FAIL: "<<e.what()<<'\n';
    return 1;
}
