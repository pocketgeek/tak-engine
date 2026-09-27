// Buildings retain their placement heading while their weapon scripts aim.
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include <cstdio>
#include <algorithm>
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto vfs=tak::hpi::mountRetailRoot(argv[1]);
    int failures=0,cases=0;
    for(bool crusades:{false,true}) {
        tak::sim::TypeRegistry registry;tak::sim::setupRegistry(registry,vfs,crusades);
        for(const auto& [id,type]:registry.types()) {
            if(!type.isStructure() || type.weapon.damage<=0 || type.weapon.melee)continue;
            for(int direction=0;direction<4;++direction) {
                tak::sim::World w;w.setVisPlayer(-1);
                const bool water=type.domain==tak::sim::UnitType::Domain::Water;
                w.setTerrain(std::vector<uint8_t>(256*256,water?10:70),256,256,64);
                w.buildNavClasses(registry);
                auto victim=*registry.find(water?"aratrans":"araarch");
                victim.maxHp=1000000;victim.healTime=0;victim.weapons.clear();victim.weapon.damage=0;
                victim.maxVel={};
                const float distance=std::max(float(type.weapon.minRange)+50,float(type.weapon.range)*0.6f);
                const float dx=direction==0?distance:direction==1?-distance:0;
                const float dz=direction==2?distance:direction==3?-distance:0;
                int a=w.spawn(&type,2000,2000,0,0),b=w.spawn(&victim,2000+dx,2000+dz,0,1);
                w.player(0).mana=100000;
                const auto initial=w.unit(a)->heading;
                w.attack(a,b,false);
                bool fired=false,turned=false;
                for(int tick=0;tick<600;++tick) {
                    w.tick(1.f/30.f);
                    fired|=w.unit(a)->justFired;
                    turned|=w.unit(a)->heading.v!=initial.v;
                }
                ++cases;
                std::printf("%s balance=%d direction=%d fired=%d turned=%d\n",id.c_str(),crusades,direction,fired,turned);
                if(turned || !fired)++failures;
            }
        }
    }
    std::printf("structure_combat: %d cases, %d failures\n",cases,failures);
    return failures?1:0;
}
