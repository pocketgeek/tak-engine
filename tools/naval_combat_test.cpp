// Naval sight must not confuse water depth with a wall, including shore attacks.
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include <algorithm>
#include <cstdio>

int main(int argc,char**argv) {
    if (argc!=2) return 2;
    auto vfs=tak::hpi::mountRetailRoot(argv[1]);
    int failures=0,cases=0;
    auto check=[&](bool ok,const char* label) {
        std::printf("[%s] %s\n",ok?"PASS":"FAIL",label);
        if (!ok) ++failures;
    };
    for (bool crusades:{false,true}) {
        tak::sim::TypeRegistry registry;tak::sim::setupRegistry(registry,vfs,crusades);
        std::vector<std::string> ships;
        for (const auto& [id,t]:registry.types())
            if (t.domain==tak::sim::UnitType::Domain::Water && !t.isStructure() &&
                t.weapon.damage>0 && !t.weapon.melee) ships.push_back(id);
        std::sort(ships.begin(),ships.end());
        for (const auto& name:ships) for (bool shore:{false,true}) for (int order=0;order<3;++order) {
            const auto* type=registry.find(name);
            // Stay outside minimum range and inside both range and the idle leash.
            const float distance=std::max(200.f,float(type->weapon.minRange)+80.f);
            if (distance>float(type->weapon.range)*0.85f ||
                (order==0 && type->leash>0 && distance>type->leash)) continue;
            tak::sim::World w;w.setVisPlayer(-1);
            std::vector<uint8_t> heights(512*256,10);
            if (shore) for (int z=0;z<256;++z) for (int x=256;x<512;++x)
                heights[size_t(z)*512+x]=70;
            w.setTerrain(heights,512,256,64);w.buildNavClasses(registry);
            auto victim=*registry.find(shore?"arakeep":"aratrans");
            victim.maxHp=30000;victim.healTime=0;victim.weapon.damage=0;
            victim.weapons.clear();victim.maxVel={};
            const float tx=shore?4160.f:3800.f,tz=1800.f;
            const float initial=order==0?distance:float(type->weapon.range)+300.f;
            const int a=w.spawn(type,tx-initial,tz,0,0),b=w.spawn(&victim,tx,tz,0,1);
            w.player(0).mana=30000;
            if (order==1) w.attackMove(a,shore?4048.f:tx,tz,false);
            if (order==2) w.attack(a,b,false);
            bool fired=false;
            for (int tick=0;tick<1800 && !fired;++tick) {
                w.tick(1.f/30.f);fired=w.unit(a)->justFired;
            }
            const float actual=tak::sim::fxLen(w.unit(a)->x-w.unit(b)->x,w.unit(a)->z-w.unit(b)->z).toFloat();
            std::printf("balance=%d unit=%s shore=%d order=%d fired=%d distance=%.1f range=%d\n",
                crusades,name.c_str(),shore,order,fired,actual,type->weapon.range);
            ++cases;check(fired,"ship acquires, attack-moves, and attacks across open water/shore");
            if (order==0) check(actual>=distance-20,"in-range ship turns and fires without charging the target");
        }
        // Melee water creatures still close to adjacency rather than using their
        // misleading authored ranged radius. No shoreline target beyond reach.
        for (const auto& [name,type]:registry.types()) {
            if (type.domain!=tak::sim::UnitType::Domain::Water || type.isStructure() ||
                type.weapon.damage<=0 || !type.weapon.melee) continue;
            for (int order=0;order<3;++order) {
                tak::sim::World w;w.setVisPlayer(-1);
                w.setTerrain(std::vector<uint8_t>(128*128,10),128,128,64);w.buildNavClasses(registry);
                auto victim=*registry.find("aratrans");victim.maxHp=30000;victim.healTime=0;
                victim.weapon.damage=0;victim.weapons.clear();victim.maxVel={};
                const float distance=order==0?40.f:500.f;
                const int a=w.spawn(&type,1000-distance,1000,0,0),b=w.spawn(&victim,1000,1000,0,1);
                if (order==1) w.attackMove(a,1000,1000,false);
                if (order==2) w.attack(a,b,false);
                bool fired=false;
                for (int tick=0;tick<1800 && !fired;++tick) { w.tick(1.f/30.f);fired=w.unit(a)->justFired; }
                std::printf("melee balance=%d unit=%s order=%d fired=%d\n",crusades,name.c_str(),order,fired);
                ++cases;check(fired,"melee water creature closes and attacks");
            }
        }
        // Shore defenders must also be able to see and fire at ships. Face the
        // fixture toward its target: this check isolates the reverse sight ray.
        {
            tak::sim::World w;w.setVisPlayer(-1);
            std::vector<uint8_t> heights(128*128,10);
            for (int z=0;z<128;++z) for (int x=64;x<128;++x) heights[size_t(z)*128+x]=70;
            w.setTerrain(heights,128,128,64);w.buildNavClasses(registry);
            auto defender=*registry.find("verflag");defender.domain=tak::sim::UnitType::Domain::Ground;
            defender.maxVel={};defender.weapon.lobPreferred=false;
            for (auto& weapon:defender.weapons) weapon.lobPreferred=false;
            auto victim=*registry.find("aratrans");victim.maxHp=30000;
            victim.weapon.damage=0;victim.weapons.clear();victim.maxVel={};
            const int a=w.spawn(&defender,1100,800,-1.570796327f,0);
            w.spawn(&victim,800,800,0,1);
            bool fired=false;
            for (int tick=0;tick<300 && !fired;++tick) { w.tick(1.f/30.f);fired=w.unit(a)->justFired; }
            check(fired,"shore defender acquires and fires at a ship across water");
        }
        // A straight-shooting ship must still respect an actual obstacle. This
        // exercises acquisition and explicit fire gates, with default ground
        // combat and ground navigation checked separately below.
        for (bool cliff:{false,true}) for (bool attack:{false,true}) {
            tak::sim::World w;w.setVisPlayer(-1);
            std::vector<uint8_t> heights(128*128,10);
            std::vector<uint16_t> features(128*128,0xFFFF);
            for (int z=0;z<128;++z) for (int x=53;x<59;++x)
                if (cliff) heights[size_t(z)*128+x]=220;
                else features[size_t(z)*128+x]=0xFFFC;
            w.setTerrain(heights,128,128,64,&features);w.buildNavClasses(registry);
            auto type=*registry.find("verflag");type.weapon.lobPreferred=false;
            for (auto& wp:type.weapons) wp.lobPreferred=false;
            auto victim=*registry.find("aratrans");victim.maxHp=30000;
            victim.weapon.damage=0;victim.weapons.clear();victim.maxVel={};
            const int a=w.spawn(&type,800,800,1.570796327f,0),b=w.spawn(&victim,1000,800,0,1);
            if (attack) w.attack(a,b,false);
            bool fired=false;
            for (int tick=0;tick<300;++tick) { w.tick(1.f/30.f);fired|=w.unit(a)->justFired; }
            check(!fired,cliff?"raised land blocks naval acquisition and fire":"explicit wall blocks naval acquisition and fire");
            if (!cliff) {
                w.blockCells(53,0,6,128,false);
                for (int tick=0;tick<600 && !fired;++tick) { w.tick(1.f/30.f);fired=w.unit(a)->justFired; }
                check(fired,"removing a shared obstacle immediately opens naval sight");
            }
        }
    }
    // The naval combat mask changes no movement cell or ground raycast.
    tak::sim::World w;w.setTerrain(std::vector<uint8_t>(64*64,10),64,64,64);
    tak::sim::UnitType boat;boat.domain=tak::sim::UnitType::Domain::Water;
    check(!w.nav().losBetween(200,200,600,200),"ground-only sight across deep water is unchanged");
    check(!w.nav().walkable(20,20) && w.navFor(&boat).walkable(20,20),"ground and boat movement grids are unchanged");
    std::printf("naval combat: %d cases, %d failures\n",cases,failures);
    return failures?1:0;
}
