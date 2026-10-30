// Buildings retain their placement heading while their weapon scripts aim.
//   structure_combat_test <data>         heading cases
//   structure_combat_test <data> reach   a unit that may not leave its post
//     (a non-ballistic tower, a Hold Position Monarch, Fire Mage or
//     Acolyte on its selected weapon)
//     never auto-acquires an enemy outside tickCombat's reach: it would drop
//     it at once, and every acquisition wipes the fire-at-will sweep.
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
namespace {
// One unit `id` at the centre of a flat field and an enemy structure 20 px
// inside its weapon range (a structure is measured centre to centre, as
// retail's 530580, for every weapon but melee: Weapon::centreReach); for a
// melee reach weapon, past the range, in reach only by tickCombat's
// footprint pad. With
// `decoy`, a mobile enemy also stands nearer, but past the reach: before the
// fix, findTarget took the nearer decoy on every acquisition turn,
// tickCombat dropped it at once, and each acquisition wiped the sweep's slot
// targets, so the structure in reach was barely shot. Returns the damage the
// structure takes.
long reachCase(const tak::sim::TypeRegistry& registry,const char* id,bool hold,int slot,
               float reach,bool decoy,int ticks) {
    const auto& type=*registry.find(id);
    tak::sim::World w;
    w.setTerrain(std::vector<uint8_t>(512*512,70),512,512,64);
    w.buildNavClasses(registry);
    auto mobile=*registry.find("araarch");
    mobile.maxHp=30000;mobile.healTime=0;mobile.weapons.clear();mobile.weapon.damage=0;
    auto building=*registry.find("aralode");
    building.maxHp=30000;building.healTime=0;building.weapons.clear();building.weapon.damage=0;
    // The reach weapon (World::approachSlot): the selected one, the longest
    // when every weapon fires.
    int reachSlot=std::max(slot,0);
    if(!type.weaponSwitching && type.weapons.size()>1)
        for(int s=0;s<int(type.weapons.size());++s)
            if(type.weapons[size_t(s)].range>type.weapons[size_t(reachSlot)].range)reachSlot=s;
    const float pad=float(type.weapons[size_t(reachSlot)].structurePad(std::max(building.footX,building.footZ)));
    const int a=w.spawn(&type,4096,4096,0,0);
    // Past the range, inside range+pad: a centre distance of reach+pad/2;
    // with no pad, 20 px inside the range.
    const int inner=w.spawn(&building,4096,4096-(pad>0.0f ? reach+pad*0.5f : reach-20.0f),0,1);
    const int outer=decoy ? w.spawn(&mobile,4096+reach+std::max(pad*0.25f,10.0f),4096,0,1) : 0;
    w.player(0).mana=100000;
    if(hold)w.unit(a)->moveState=0;
    if(slot>=0)w.setWeapon(a,slot);
    long damage=0;
    for(int tick=0;tick<ticks;++tick) {
        if(outer)w.unit(outer)->hp=tak::sim::Fixed::fromInt(30000);
        w.unit(inner)->hp=tak::sim::Fixed::fromInt(30000);
        w.unit(a)->hp=tak::sim::Fixed::fromInt(type.maxHp);
        w.tick(1.f/30.f);
        damage+=30000-w.unit(inner)->hp.floorInt();
    }
    return damage;
}
// Compares the structure's damage with and without the decoy.
int reachPair(const tak::sim::TypeRegistry& registry,const char* id,bool hold,int slot,float reach) {
    const long alone=reachCase(registry,id,hold,slot,reach,false,1800);
    const long decoyed=reachCase(registry,id,hold,slot,reach,true,1800);
    // Shots are few (a Stronghold reloads in 4 s) and splash varies, so
    // half is the bar; the churn left it at nothing.
    const bool ok=alone>0 && decoyed*2>=alone;
    std::printf("%s %s%s slot=%d reach=%.0f: structure in reach takes %ld alone, %ld beside an out-of-reach decoy\n",
                ok?"PASS":"FAIL",id,hold?" (hold)":"",slot,reach,alone,decoyed);
    return ok?0:1;
}
// tickCombat's reach without a structure pad: the selected weapon's range,
// the longest when every weapon fires.
float reachOf(const tak::sim::UnitType& t,int slot) {
    if(!t.weaponSwitching && t.weapons.size()>1)return t.maxRange();
    return float(t.weapons[size_t(std::max(slot,0))].range);
}
int reachCases(const char* data) {
    auto vfs=tak::hpi::mountRetailRoot(data);
    tak::sim::TypeRegistry registry;tak::sim::setupRegistry(registry,vfs,false);
    int failures=0;
    for(const char* id:{"crepris","arassh","cresage","tarmage","arapries","araarch","aralode"})
        if(!registry.find(id)){std::printf("FAIL missing type %s\n",id);return 1;}
    // The duel's churning towers: Prismatic Mirror and Stronghold.
    for(const char* id:{"crepris","arassh"}) {
        const auto& t=*registry.find(id);
        if(t.weapons.empty() || t.weapons[0].ballisticClass() || t.weapons[0].melee) {
            std::printf("FAIL %s: expected a non-ballistic ranged weapon\n",id);++failures;continue;
        }
        failures+=reachPair(registry,id,false,-1,reachOf(t,-1));
    }
    // The Creon Monarch on Hold Position with its shortest weapon selected:
    // the reach is that weapon's, not the longest one's.
    const auto& t=*registry.find("cresage");
    int shortest=0;
    for(int s=0;s<int(t.weapons.size());++s)
        if(t.weapons[size_t(s)].damage>0 && !t.weapons[size_t(s)].melee &&
           !t.weapons[size_t(s)].ballisticClass() && t.weapons[size_t(s)].range<t.weapons[size_t(shortest)].range)
            shortest=s;
    if(!t.weaponSwitching || reachOf(t,shortest)>=t.maxRange()) {
        std::printf("FAIL cresage: expected a weapon switcher with a short weapon\n");++failures;
    } else {
        failures+=reachPair(registry,"cresage",true,shortest,reachOf(t,shortest));
    }
    // Hold Position on a weapon of each other class 0.9.12 still padded: a
    // Hailstorm, an Earthquake (cresage's shortest is a Remote Effect).
    for(const auto& [id,slot]:{std::pair<const char*,int>{"tarmage",2},{"arapries",0}}) {
        const auto* h=registry.find(id);
        if(!h || int(h->weapons.size())<=slot || !h->weaponSwitching) {
            std::printf("FAIL %s: expected a weapon switcher with slot %d\n",id,slot);++failures;continue;
        }
        failures+=reachPair(registry,id,true,slot,reachOf(*h,slot));
    }
    std::printf("structure_combat reach: %d failures\n",failures);
    return failures?1:0;
}
} // namespace
int main(int argc,char** argv) {
    if(argc==3 && std::strcmp(argv[2],"reach")==0)return reachCases(argv[1]);
    if(argc!=2)return 2;
    auto vfs=tak::hpi::mountRetailRoot(argv[1]);
    int failures=0,cases=0;
    for(bool crusades:{false,true}) {
        tak::sim::TypeRegistry registry;tak::sim::setupRegistry(registry,vfs,crusades);
        for(const auto& [id,type]:registry.types()) {
            if(!type.isStructure() || type.weapon.damage<=0 || type.weapon.melee)continue;
            for(float fraction:{0.6f,0.98f,1.1f}) for(int direction=0;direction<4;++direction) {
                tak::sim::World w;
                const bool water=type.domain==tak::sim::UnitType::Domain::Water;
                w.setTerrain(std::vector<uint8_t>(512*512,water?10:70),512,512,64);
                w.buildNavClasses(registry);
                auto victim=*registry.find(water?"aratrans":"araarch");
                victim.maxHp=30000;victim.healTime=0;victim.weapons.clear();victim.weapon.damage=0;
                const float distance=std::max(float(type.weapon.minRange)+50,float(type.weapon.range)*fraction);
                const float dx=direction==0?distance:direction==1?-distance:0;
                const float dz=direction==2?distance:direction==3?-distance:0;
                int a=w.spawn(&type,4096,4096,0,0),b=w.spawn(&victim,4096+dx,4096+dz,0,1);
                w.player(0).mana=100000;
                const auto initial=w.unit(a)->heading;
                w.attack(a,b,false);
                bool fired=false,turned=false;
                for(int tick=0;tick<600;++tick) {
                    w.unit(b)->hp=tak::sim::Fixed::fromInt(30000);
                    w.tick(1.f/30.f);
                    fired|=w.unit(a)->justFired;
                    turned|=w.unit(a)->heading.v!=initial.v;
                }
                ++cases;
                std::printf("%s balance=%d direction=%d range=%.2f fired=%d turned=%d\n",id.c_str(),crusades,direction,fraction,fired,turned);
                if(turned || (fraction<1 && !fired))++failures;
            }
        }
    }
    std::printf("structure_combat: %d cases, %d failures\n",cases,failures);
    return failures?1:0;
}
