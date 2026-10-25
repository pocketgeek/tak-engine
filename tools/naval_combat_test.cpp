// Naval sight must not confuse water depth with a wall, including shore attacks.
// With --legion the moving cases (attack-move, attack, melee closing) run in
// Legion mode on a placement plane, as a loaded map has, and must be routed
// by Legion.
#include "hpi/hpi.h"
#include "sim/legion.h"
#include "sim/matchsetup.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

int main(int argc,char**argv) {
    if (argc!=2 && argc!=3) return 2;
    const bool legion=argc==3 && std::string_view(argv[2])=="--legion";
    if (argc==3 && !legion) return 2;
    auto vfs=tak::hpi::mountRetailRoot(argv[1]);
    int failures=0,cases=0;
    // Legion plans boats on the map's placement plane (feature-free here).
    auto legionMode=[&](tak::sim::World& w,int width,int height) {
        if (!legion) return;
        w.setPathfindingMode(tak::sim::PathfindingMode::Legion);w.setPathService(true);
        w.setMapPlacementFeatures(std::vector<uint16_t>(size_t(width)*height,0xffff),{});
    };
    auto served=[&](tak::sim::World& w,int id,bool& seen) {
        if (auto* l=w.legionNavigator(); l && w.unit(id))
            seen|=l->mission(*w.unit(id))!=tak::sim::LegionMission::None;
    };
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
            w.setTerrain(heights,512,256,64);w.buildNavClasses(registry);legionMode(w,512,256);
            auto victim=*registry.find(shore?"arakeep":"aratrans");
            victim.maxHp=30000;victim.healTime=0;victim.weapon.damage=0;
            victim.weapons.clear();victim.maxVel={};
            const float tx=shore?4160.f:3800.f,tz=1800.f;
            const float initial=order==0?distance:float(type->weapon.range)+300.f;
            const int a=w.spawn(type,tx-initial,tz,0,0),b=w.spawn(&victim,tx,tz,0,1);
            w.player(0).mana=30000;
            if (order==1) w.attackMove(a,shore?4048.f:tx,tz,false);
            if (order==2) w.attack(a,b,false);
            bool fired=false,routed=false;
            for (int tick=0;tick<1800 && !fired;++tick) {
                w.tick(1.f/30.f);fired=w.unit(a)->justFired;served(w,a,routed);
            }
            const float actual=tak::sim::fxLen(w.unit(a)->x-w.unit(b)->x,w.unit(a)->z-w.unit(b)->z).toFloat();
            std::printf("balance=%d unit=%s shore=%d order=%d fired=%d distance=%.1f range=%d\n",
                crusades,name.c_str(),shore,order,fired,actual,type->weapon.range);
            ++cases;check(fired,"ship acquires, attack-moves, and attacks across open water/shore");
            if (order==0) check(actual>=distance-20,"in-range ship turns and fires without charging the target");
            if (legion && order>0) check(routed,"Legion routes the ship's approach");
        }
        // Melee water creatures still close to adjacency rather than using their
        // misleading authored ranged radius. No shoreline target beyond reach.
        for (const auto& [name,type]:registry.types()) {
            if (type.domain!=tak::sim::UnitType::Domain::Water || type.isStructure() ||
                type.weapon.damage<=0 || !type.weapon.melee) continue;
            for (int order=0;order<3;++order) {
                tak::sim::World w;w.setVisPlayer(-1);
                w.setTerrain(std::vector<uint8_t>(128*128,10),128,128,64);w.buildNavClasses(registry);legionMode(w,128,128);
                auto victim=*registry.find("aratrans");victim.maxHp=30000;victim.healTime=0;
                victim.weapon.damage=0;victim.weapons.clear();victim.maxVel={};
                const float distance=order==0?40.f:500.f;
                const int a=w.spawn(&type,1000-distance,1000,0,0),b=w.spawn(&victim,1000,1000,0,1);
                if (order==1) w.attackMove(a,1000,1000,false);
                if (order==2) w.attack(a,b,false);
                bool fired=false,routed=false;
                for (int tick=0;tick<1800 && !fired;++tick) { w.tick(1.f/30.f);fired=w.unit(a)->justFired;served(w,a,routed); }
                std::printf("melee balance=%d unit=%s order=%d fired=%d\n",crusades,name.c_str(),order,fired);
                ++cases;check(fired,"melee water creature closes and attacks");
                if (legion && order>0) check(routed,"Legion routes the water creature's approach");
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
    // A Creon Submersible's Marine Mortar (Ballistic, lobpreferred,
    // gravityadjustment 2.8) against an Aramon Divine Lodestone (3x3) on an
    // island, as in the user's 0.9.1 game. 52bc60 rules a target out of range
    // when the shell's arc cannot reach it and tests the centre distance with
    // no footprint pad; our footprint pad let subs 822-848 px off lob shells
    // flat into the water at their own bow. Never in Legion: no movement here
    // that Legion routes differently matters to what is checked.
    if (!legion) {
        tak::sim::TypeRegistry registry;tak::sim::setupRegistry(registry,vfs,true);   // range 800, as in the game
        const auto* sub=registry.find("cresubm");
        const auto* mana=registry.find("aramana");
        check(sub && mana && sub->weapon.ballistic,"shipped cresubm (ballistic) and aramana load");
        if (sub && mana) {
            const float range=float(sub->weapon.range);
            // island: cells [isl0,isl1) square, land 66 over sea level 64.
            struct Shot { int fired=0,hits=0,bowSplashes=0,acquired=0; bool holding=false; float closest=1e9f,moved=0,lastMoves=0; };
            auto run=[&](float distance,int moveState,int islandHalf,int ticks,uint8_t land=66) {
                tak::sim::World w;w.setVisPlayer(-1);
                const int size=256;
                std::vector<uint8_t> heights(size_t(size)*size,10);
                const int c=size/2;
                for (int z=c-islandHalf;z<c+islandHalf;++z) for (int x=c-islandHalf;x<c+islandHalf;++x)
                    heights[size_t(z)*size+x]=land;
                w.setTerrain(heights,size,size,64);w.buildNavClasses(registry);
                auto victim=*mana;victim.maxHp=60000;victim.healTime=0;
                const float tx=float(c*16),tz=float(c*16);
                const int b=w.spawn(&victim,tx,tz,0,1);
                const int a=w.spawn(sub,tx-distance,tz,1.570796327f,0);
                w.unit(a)->moveState=moveState;
                w.player(0).mana=30000;
                Shot out;
                std::vector<std::pair<float,float>> trail;
                bool attacking=false;
                for (int tick=0;tick<ticks;++tick) {
                    w.tick(1.f/30.f);
                    const auto* u=w.unit(a);
                    if (!u || !w.unit(b)) break;
                    out.fired+=u->justFired;
                    const bool now=!u->orders.empty() && u->orders.front().targetId==b;
                    out.acquired+=now && !attacking;
                    attacking=out.holding=now;
                    trail.push_back({u->x.toFloat(),u->z.toFloat()});
                    out.closest=std::min(out.closest,tak::sim::fxLen(u->x-w.unit(b)->x,u->z-w.unit(b)->z).toFloat());
                    for (const auto& h:w.hits()) {
                        if (h.fromPlayer!=0) continue;
                        if (h.victimId==b) { ++out.hits;continue; }
                        const float dx=h.x-u->x.toFloat(),dz=h.z-u->z.toFloat();
                        if (dx*dx+dz*dz<300.f*300.f) ++out.bowSplashes;
                    }
                }
                for (size_t i=1;i<trail.size();++i) {
                    const float step=std::hypot(trail[i].first-trail[i-1].first,trail[i].second-trail[i-1].second);
                    out.moved+=step;
                    if (i+600>=trail.size()) out.lastMoves+=step;
                }
                return out;
            };
            auto show=[&](const char* what,float d,const Shot& s) {
                std::printf("cresubm %-26s from %.0f px (range %.0f): fired=%d hits=%d bow-splashes=%d acquired=%d holding=%d closest=%.0f moved=%.0f last20s=%.0f\n",
                    what,d,range,s.fired,s.hits,s.bowSplashes,s.acquired,int(s.holding),s.closest,s.moved,s.lastMoves);
            };
            // In arc and in range: it hits from where it lies.
            const Shot near=run(range-60,0,8,900);show("hold, near",range-60,near);
            check(near.hits>0 && near.bowSplashes==0,"a sub well inside its range lobs onto the lodestone");
            // Past the range: a sub that may move closes in and hits; it never
            // lobs a shell flat into the water at its bow.
            for (float d:{range+30,range+40,range+48}) {
                const Shot open=run(d,2,8,1800);show("roam, open water",d,open);
                check(open.bowSplashes==0,"no flat shell into the water from beyond the arc's reach");
                check(open.hits>0 && open.closest<=range,"the sub closes to its range and hits");
            }
            // The band where the origin arc reaches but the muzzle-to-SweetSpot
            // solve may not: a shell is lobbed onto the target or not at all.
            for (float d:{range-15,range-5,range-1}) {
                const Shot band=run(d,0,8,900);show("hold, edge of range",d,band);
                check(band.bowSplashes==0 && (band.fired==0 || band.hits>0),
                      "at the edge of its range a sub hits the lodestone or holds its fire");
            }
            // A lodestone on a raised island, in the sub's 2D range: the
            // origin arc (52bc60) may reach while the muzzle-to-SweetSpot
            // solve does not, so the aimed sub has no shot. A Hold sub cannot
            // close in: after kBlockedShots reloads without an arc it writes
            // the target off and drops it (before: it held it for ever with
            // its slots off auto), and it is not acquired again from there.
            for (uint8_t land:{uint8_t(70),uint8_t(80),uint8_t(100),uint8_t(120)}) {
                const Shot high=run(range-5,0,8,900,land);
                std::printf("  (island height %d)\n",int(land));show("hold, raised island",range-5,high);
                check(high.bowSplashes==0 && high.acquired<=1 && (high.hits>0 || !high.holding),
                      "a Hold sub with no arc drops the target and does not take it up again");
            }
            // Past the range with the shore in the way: it cannot close, so it
            // holds: no shell, and no churning at the shoreline.
            for (float d:{range+30,range+48}) {
                const Shot shore=run(d,2,int(range+8)/16,1800);show("roam, shore-bound",d,shore);
                check(shore.fired==0 && shore.bowSplashes==0,"a sub kept past its range by the shore does not fire");
                check(shore.lastMoves<64,"a shore-bound sub holds instead of churning");
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
