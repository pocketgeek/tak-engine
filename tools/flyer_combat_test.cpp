// Landing must not turn ordinary flyers into powered-down combat units.
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include <cstdio>
#include <algorithm>

int main(int argc,char** argv) {
    if (argc!=2) return 2;
    auto vfs=tak::hpi::mountRetailRoot(argv[1]);
    int failures=0;
    auto check=[&](bool ok,const char* label) {
        std::printf("[%s] %s\n",ok?"PASS":"FAIL",label);
        if (!ok) ++failures;
    };
    for (bool crusades:{false,true}) {
        tak::sim::TypeRegistry registry;tak::sim::setupRegistry(registry,vfs,crusades);
        std::vector<std::string> flyers;
        for (const auto& [id,type]:registry.types())
            if (type.canFly && !type.onOffable && type.weapon.damage>0) flyers.push_back(id);
        std::sort(flyers.begin(),flyers.end());
        for (const auto& name:flyers) for (int order=0;order<3;++order) {
            tak::sim::World world;world.setVisPlayer(-1);
            world.setTerrain(std::vector<uint8_t>(256*256,100),256,256,20);
            world.buildNavClasses(registry);
            const auto* type=registry.find(name);
            if (!type) { ++failures;continue; }
            int flyer=world.spawn(type,1000,1000,0,0);
            world.order(flyer,1200,1000,false);
            // Exercise the actual VTOL standby/landing controller, not a manually
            // cleared active flag: this is what made already-landed drakes fail.
            for (int tick=0;tick<1800 && world.unit(flyer)->active;++tick)
                world.tick(1.f/30.f);
            if (type->vtolStandby)
                check(!world.unit(flyer)->active,"landing runs the retail deactivation callback");
            for (int tick=0;tick<300;++tick) world.tick(1.f/30.f);
            auto victim=*registry.find("tarzom");victim.maxHp=30000;victim.healTime=0;victim.weapon.damage=0;victim.weapons.clear();
            const float x=world.unit(flyer)->x.toFloat(),z=world.unit(flyer)->z.toFloat();
            const float distance=order==0 ? std::min(200.f,float(type->weapon.range)*0.5f) : 1000.f;
            victim.maxVel={};
            world.player(0).mana=100000;
            int target=world.spawn(&victim,x+distance,z,0,1);
            if (order==1) world.attackMove(flyer,x+distance,z,false);
            if (order==2) world.attack(flyer,target,false);
            bool fired=false;
            for (int tick=0;tick<1200 && !fired;++tick) {
                world.tick(1.f/30.f);
                fired=world.unit(flyer)->justFired;
            }
            std::printf("balance=%s unit=%s order=%d fired=%d\n",crusades?"Crusades":"standard",
                        name.c_str(),order,int(fired));
            // A very short authored maneuver leash (e.g. the Crusades Angel)
            // intentionally refuses this idle target. Explicit orders override it.
            if (order==0 && world.unit(flyer)->moveState==1 && type->leash>0 && distance>type->leash) {
                check(!fired,"automatic attack respects the authored maneuver leash");
                continue;
            }
            check(fired,"landed flyer can acquire, Move Fight, and directly attack");
        }
        // Range alone is not the hover attack destination. Keep targets alive so
        // these checks cover settling, backing away, altitude and target pursuit.
        for (const auto& name:flyers) {
            auto type=*registry.find(name);
            if (!type.hoverAttack && !type.weapon.hoverAttack) continue;
            for (bool close:{false,true}) {
                tak::sim::World w;w.setVisPlayer(-1);
                const bool overrides=name=="zondrake" && close;
                const int ground=overrides?240:100;
                w.setTerrain(std::vector<uint8_t>(256*256,overrides?230:100),256,256,overrides?240:20);
                auto victim=*registry.find("tarzom");victim.maxHp=1000000;victim.healTime=0;
                victim.weapon.damage=0;victim.weapons.clear();
                for (auto& weapon:type.weapons) { weapon.mindControl=false;weapon.damage=1; }
                type.canCapture=false;
                if (overrides) {
                    type.weapons.front().hoverAttackDistance=88;
                    type.weapons.front().hoverAttackAltitude=300;
                }
                type.weapon=type.weapons.front();
                const int a=w.spawn(&type,1500,1500,0,0);
                const int b=w.spawn(&victim,close?1520:2500,1500,0,1);
                w.player(0).mana=1000000;w.attack(a,b,false);
                for (int tick=0;tick<1500;++tick) w.tick(1.f/30.f);
                const auto* u=w.unit(a);const auto* t=w.unit(b);
                const auto& weapon=type.weapons.front();
                const int distance=weapon.hoverAttackDistance?weapon.hoverAttackDistance:type.hoverAttackDistance;
                const int altitude=weapon.hoverAttackAltitude?weapon.hoverAttackAltitude:type.hoverAttackAltitude;
                const float radius=tak::sim::fxLen(u->x-t->x,u->z-t->z).toFloat();
                std::printf("hover balance=%d unit=%s close=%d distance=%.2f wanted=%d height=%.2f wanted=%d\n",
                    int(crusades),name.c_str(),int(close),radius,distance,u->flightY.toFloat(),std::min(511,ground+altitude));
                check(std::abs(radius-distance)<20,"hover flyer settles at authored attack distance");
                check(std::abs(u->flightY.toFloat()-std::min(511,ground+altitude))<2,
                      "hover flyer uses attack altitude and remains airborne");
                check(u->flightGroundMode==2 && !u->orders.empty(),"hover arrival preserves attack order");
                const float previousZ=t->z.toFloat();
                w.order(b,t->x.toFloat(),previousZ+150,false);
                for (int tick=0;tick<900;++tick) w.tick(1.f/30.f);
                u=w.unit(a);t=w.unit(b);
                check(t->z.toFloat()>previousZ+100,"pursuit fixture target actually moved");
                check(std::abs(tak::sim::fxLen(u->x-t->x,u->z-t->z).toFloat()-distance)<20,
                      "hover flyer follows a moving target");
            }
        }
        // Preserve the intentional off switch for units that actually support it.
        auto turret=*registry.find("zondrake");turret.onOffable=true;
        auto victim=*registry.find("tarzom");victim.maxHp=30000;victim.healTime=0;victim.weapon.damage=0;victim.weapons.clear();
        tak::sim::World world;world.setVisPlayer(-1);
        world.setTerrain(std::vector<uint8_t>(128*128,100),128,128,20);
        int a=world.spawn(&turret,800,800,1.570796327f,0),b=world.spawn(&victim,950,800,0,1);
        world.setActive(a,false);world.attack(a,b,false);
        for (int tick=0;tick<300;++tick) world.tick(1.f/30.f);
        check(world.unit(b)->hp.toFloat()==victim.maxHp,"powered-down on/off units still cannot fire");
        world.setActive(a,true);
        for (int tick=0;tick<300;++tick) world.tick(1.f/30.f);
        check(world.unit(b)->hp.toFloat()<victim.maxHp,"powered-up units resume firing");
    }
    return failures?1:0;
}
