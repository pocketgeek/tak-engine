// The fire-at-will sweep's companions: deterministic per-player vision (icd
// 4f7210 / 4c6800 / 411700), AI weapon switching (40f411..40fb92) and the
// idle retaliation mission (409a50 part a, 4d7de0), and the target score's
// terrain-scaled turn rate (412a77..412ae2). One ctest per part:
//   fire_sweep_test vision | switch | retaliation | turnrate
#include "sim/sim.h"
#include "sim/retailcost.h"
#include "sim/retailexploration.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>
#include <algorithm>
#include <cmath>
using namespace tak;
namespace {
int failures=0;
void check(bool ok,const char* label) {
    std::printf("%s %s\n",ok?"PASS":"FAIL",label);failures+=!ok;
}
constexpr int kMap=128;
void setup(sim::World& w,int players) {
    w.setSerialThreads(true);w.setPathfindingMode(sim::PathfindingMode::Retail);
    w.setTerrain(std::vector<uint8_t>(kMap*kMap,100),kMap,kMap,20);
    w.setPathService(true);w.setPlayerCount(players);
}
void ticks(sim::World& w,int n) {for(int i=0;i<n;++i)w.tick(1.f/30);}
sim::UnitType gunner(int sight) {
    sim::UnitType t;t.id="gunner";t.canMove=true;t.upright=true;t.maxHp=500;t.footX=t.footZ=2;
    t.maxVel=sim::Fixed::fromInt(2);t.turnRate=t.turnInPlaceRate=600;t.sight=sight;t.sightHeight=24;
    sim::Weapon w;w.name="bolt";w.range=300;w.reload=0.3f;w.damage=10;w.projVel=900;w.aimTol=33000;
    t.weapon=w;t.weapons={w};return t;
}
sim::UnitType dummy() {
    sim::UnitType t;t.id="dummy";t.maxHp=5000;t.footX=t.footZ=2;t.maxVel=sim::Fixed::fromInt(1);
    t.canMove=true;t.upright=true;t.sight=40;t.sightHeight=24;return t;
}

// The view counters must equal a from-scratch sum of the active footprints.
bool countsMatchRebuild(const sim::World& w) {
    const int width=kMap/2,height=kMap/2;
    const auto heights=sim::retailExplorationHeights(kMap,kMap,20,[](int,int){return 100;});
    std::vector<uint16_t> expect(w.viewCounts().size(),0);
    if (expect.empty()) return false;
    const size_t cells=size_t(width)*height;
    for (const auto& u:w.units()) {
        if (!u.viewSight.active) continue;
        auto sight=u.viewSight;sight.active=false;
        sim::retailSightFootprint(sight,true,false,width,height,uint8_t(u.viewPlayer),
            [&](int x,int z){return heights[size_t(z)*width+x];},
            [&](int x,int z,int d,uint16_t){expect[size_t(u.viewPlayer)*cells+size_t(z)*width+x]+=uint16_t(d);});
    }
    return expect==w.viewCounts();
}

void vision() {
    const auto g=gunner(40);const auto scout=gunner(160);const auto d=dummy();
    sim::World w;setup(w,2);
    const int shooter=w.spawn(&g,400,1000,0,0),foe=w.spawn(&d,650,1000,0,1);
    w.unit(shooter)->fireState=0;   // keep the order-driven acquisition out of it
    bool consistent=true;
    for(int i=0;i<12;++i){ticks(w,1);consistent=consistent && countsMatchRebuild(w);}
    check(!w.playerSees(0,*w.unit(foe)),"vision: an enemy beyond the player's sight is unseen");
    check(w.playerSees(1,*w.unit(foe)) && w.playerSees(0,*w.unit(shooter)),"vision: a player sees its own units");
    check(w.visibleEnemies(0).empty(),"vision: the unseen enemy is not on the visible-enemy list");
    check(w.retailSelectTarget(*w.unit(shooter),0,true)==0,"vision: the sweep does not select an enemy its player cannot see");
    // A scout walks up: the enemy becomes seen at once, and a target within
    // one 7-tick list rebuild.
    const int spotter=w.spawn(&scout,650,1070,0,0);
    w.unit(spotter)->fireState=0;
    ticks(w,1);consistent=consistent && countsMatchRebuild(w);
    check(w.playerSees(0,*w.unit(foe)),"vision: an enemy inside another own unit's footprint is seen");
    int picked=0,waited=0;
    for(;waited<8 && !picked;++waited){
        picked=w.retailSelectTarget(*w.unit(shooter),0,true);
        if(!picked){ticks(w,1);consistent=consistent && countsMatchRebuild(w);}
    }
    check(picked==foe,"vision: once seen, the enemy is selected within one list rebuild");
    // Cloak hides an enemy, never an own unit.
    w.unit(foe)->cloaked=true;w.unit(shooter)->cloaked=true;
    check(!w.playerSees(0,*w.unit(foe)) && w.playerSees(0,*w.unit(shooter)),"vision: cloak hides enemies, never own units");
    w.unit(foe)->cloaked=false;w.unit(shooter)->cloaked=false;
    // The scout leaves: the enemy is unseen again; the snapshot may hold it
    // for up to six more ticks, then it drops.
    w.order(spotter,200,1700,false);
    int lost=-1;
    for(int i=0;i<300;++i){ticks(w,1);consistent=consistent && countsMatchRebuild(w);
        if(lost<0 && !w.playerSees(0,*w.unit(foe)))lost=i;
        if(lost>=0 && i>=lost+8)break;}
    check(lost>=0 && w.visibleEnemies(0).empty(),"vision: an enemy that left sight drops off the list at the next rebuild");
    check(consistent,"vision: the view counters equal a rebuild from the footprints every tick");
}
// shootme: a human-owned shooter's sweep skips types without it; an AI-owned
// shooter picks them anyway (icd 412620).
void shootMe() {
    const auto g=gunner(400);auto d=dummy();d.shootMe=false;
    for (bool ai:{false,true}) {
        sim::World w;setup(w,2);w.player(0).automaticGates=ai;
        const int shooter=w.spawn(&g,400,1000,0,0),foe=w.spawn(&d,600,1000,0,1);
        w.unit(shooter)->fireState=0;
        int picked=0;
        for(int i=0;i<8 && !picked;++i){ticks(w,1);picked=w.retailSelectTarget(*w.unit(shooter),0,true);}
        check(ai ? picked==foe : picked==0,ai ? "shootme: an AI-owned shooter picks a type without shootme"
                                              : "shootme: a human-owned shooter does not pick a type without shootme");
    }
    {   // An explicit attack order is never gated by shootme.
        sim::World w;setup(w,2);
        const int shooter=w.spawn(&g,400,1000,0,0),foe=w.spawn(&d,600,1000,0,1);
        w.attack(shooter,foe,false);
        const auto hp=w.unit(foe)->hp;
        ticks(w,90);
        check(w.unit(foe)->hp<hp,"shootme: a human-ordered attack still hits a type without shootme");
    }
}
} // namespace

void weaponSwitch();
void retaliation();
void turnRate();

int main(int argc,char** argv) {
    const std::string part=argc>1 ? argv[1] : "all";
    if(part=="vision" || part=="all")vision();
    if(part=="vision" || part=="all")shootMe();
    if(part=="switch" || part=="all")weaponSwitch();
    if(part=="retaliation" || part=="all")retaliation();
    if(part=="turnrate" || part=="all")turnRate();
    return failures?1:0;
}
void weaponSwitch() {
    using sim::RetailWeaponSwitchInput;
    auto row=[](int e,int f,int rnd,int* draws){
        return std::tuple{[e](int){return e;},[f](int){return f;},[rnd,draws](int){++*draws;return rnd;}};
    };
    auto run=[&](const RetailWeaponSwitchInput& in,int e,int f,int rnd=5,int* drawsOut=nullptr){
        int draws=0;auto [E,F,R]=row(e,f,rnd,&draws);
        const int c=sim::retailWeaponSwitchChoice(in,E,F,R);
        if(drawsOut)*drawsOut=draws;return c;
    };
    RetailWeaponSwitchInput base;base.target=true;base.selected=0;base.targetHp=500;
    base.slots[0]={true,true,true,0,false,false,60};
    {   auto in=base;in.slots[1]={true,true,true,40,false,false,30};
        check(run(in,1,1)==1,"switch: an affordable aoe<=50 slot is taken outright");
        in.targetHp=121;check(run(in,1,1)==-1,"switch: the primary is kept when it does half the target's HP");
        in.targetHp=122;check(run(in,1,1)==1,"switch: ...and dropped just above that");
        in.slots[1].affordable=false;in.targetHp=500;check(run(in,1,1)==-1,"switch: an unaffordable slot is skipped");
    }
    {   auto in=base;in.slots[2]={true,true,true,250,false,true,0};
        check(run(in,2,0)==-1,"switch: aoe>=200 needs more than two enemies");
        check(run(in,3,0)==2,"switch: aoe>=200 with three enemies and no allies");
        check(run(in,3,1)==-1,"switch: aoe>=200 refuses unless allies are outnumbered 3:1");
        in.lowMonarch=true;check(run(in,3,1)==2,"switch: a low Monarch ignores its allies");
    }
    {   auto in=base;in.slots[1]={true,true,true,100,false,false,0};
        check(run(in,5,1)==-1 && run(in,6,1)==1,"switch: medium aoe needs allies outnumbered 5:1");
        in.slots[1].mindControl=true;in.slots[1].aoe=40;in.targetHp=10;
        check(run(in,1,1)==1,"switch: a mind-control slot overrides the primary-finishes-it rule");
    }
    {   auto in=base;in.selected=1;in.slots[1]={true,true,false,40,false,false,0};
        check(run(in,1,1)==0,"switch: from slot 1 it returns straight to the primary");
    }
    {   auto in=base;in.target=false;in.selected=2;in.slots[2]={true,true,true,250,false,true,0};
        int draws=0;
        check(run(in,0,0,0,&draws)==0 && draws==1,"switch: no target, costly slot: back to 0 on rand(10)==0");
        check(run(in,0,0,3,&draws)==-1 && draws==1,"switch: ...and kept otherwise (one draw)");
        in.slots[2].costlyOrSpecial=false;
        check(run(in,0,0,0,&draws)==-1 && draws==0,"switch: no draw for a cheap ordinary slot");
    }
    // World: an AI switcher facing a clump switches to its splash slot; a
    // human-owned one keeps the primary.
    auto caster=[]{
        auto t=gunner(400);t.id="caster";t.weaponSwitching=true;
        sim::Weapon a=t.weapons[0],b=a,c=a;
        b.name="ball";b.aoe=150;c.name="wave";c.aoe=250;
        t.weapons={a,b,c};t.weapon=a;return t;
    }();
    const auto d=dummy();
    for(bool ai:{true,false}) {
        sim::World w;setup(w,2);w.player(0).automaticGates=ai;
        const int id=w.spawn(&caster,400,1000,0,0);
        for(int i=0;i<4;++i)w.spawn(&d,600+40*(i%2),960+40*(i/2),0,1);
        int slot=0;
        for(int t=0;t<90 && !slot;++t){ticks(w,1);slot=w.unit(id)->weaponSlot;}
        if(ai)check(slot==2,"switch: an AI switcher next to an enemy clump takes its splash weapon");
        else check(slot==0,"switch: a human-owned switcher never auto-switches");
    }
}
void retaliation() {
    auto victimType=[]{auto t=gunner(160);t.id="victim";t.leash=300;return t;}();
    auto raider=[](int range){auto t=gunner(160);t.id="raider";t.maxVel=sim::Fixed::fromInt(4);
        t.weapons[0].range=range;t.weapon=t.weapons[0];return t;};
    const auto r1=raider(500),r2=raider(700);
    struct Hit { int victim=0,attacker=0; bool hit=false; sim::Fixed hitX,hitZ; };
    // Runs until the victim is first hit; returns where it stood.
    auto firstHit=[&](sim::World& w,Hit& h,int limit) {
        for(int t=0;t<limit && !h.hit;++t){
            const sim::Unit& v=*w.unit(h.victim);
            const sim::Fixed x=v.x,z=v.z,hp=v.hp;
            ticks(w,1);
            if(w.unit(h.victim)->hp<hp){h.hit=true;h.hitX=x;h.hitZ=z;}
        }
    };
    {   // 1: idle Offensive mobile, hit from 1.5x range inside 0.9 leash + range.
        sim::World w;setup(w,2);Hit h;
        h.victim=w.spawn(&victimType,400,1000,0,0);h.attacker=w.spawn(&r1,850,1000,0,1);
        auto& v=*w.unit(h.victim);v.moveState=1;v.fireState=2;
        w.attack(h.attacker,h.victim,false);
        firstHit(w,h,300);
        const auto& o=w.unit(h.victim)->orders;
        const bool attack=!o.empty() && o[0].targetId==h.attacker && o[0].autoTarget && o[0].chaseLeashed &&
            o[0].chaseLeash==300 && o[0].chaseOrigin.x==int16_t(w.unit(h.victim)->x.floorInt());
        // Behind the attack leg (its route waypoints share its target).
        const auto ret=std::find_if(o.begin(),o.end(),[](const sim::Order& x){return !x.targetId;});
        const bool back=ret!=o.end() && ret->groundMission && ret->groundResponse.mode==1;
        check(h.hit && attack && back,"retaliation: an idle Offensive unit takes a leashed attack with a return move");
        const int16_t originX=o.empty() ? 0 : o[0].chaseOrigin.x;
        // The attacker flees past the leash: the attack retires, the unit walks home.
        w.order(h.attacker,1900,1000,false);w.unit(h.attacker)->fireState=0;
        float farthest=0;bool retired=false,home=false;
        for(int t=0;t<1200;++t){
            ticks(w,1);
            const auto& u=*w.unit(h.victim);
            farthest=std::max(farthest,u.x.toFloat()-originX);
            if(!retired && (u.orders.empty() || u.orders[0].targetId!=h.attacker))retired=true;
            if(retired && u.orders.empty()){home=std::abs(u.x.toFloat()-originX)<48;break;}
        }
        check(retired && farthest>=250 && farthest<360,"retaliation: the chase retires at the leash");
        check(home,"retaliation: the unit then walks back to where it was hit");
    }
    {   // 2: idle Defensive: no mission.
        sim::World w;setup(w,2);Hit h;
        h.victim=w.spawn(&victimType,400,1000,0,0);h.attacker=w.spawn(&r1,850,1000,0,1);
        auto& v=*w.unit(h.victim);v.moveState=0;v.fireState=2;
        w.attack(h.attacker,h.victim,false);
        firstHit(w,h,300);
        check(h.hit && w.unit(h.victim)->orders.empty(),"retaliation: an idle Defensive unit gets no mission");
    }
    {   // 3: patrolling Defensive with fire at will: attack + return, then the patrol resumes.
        sim::World w;setup(w,2);Hit h;
        h.victim=w.spawn(&victimType,400,1000,0,0);h.attacker=w.spawn(&r1,880,1000,0,1);
        auto& v=*w.unit(h.victim);v.moveState=0;v.fireState=2;
        w.patrol(h.victim,400,1300);
        w.attack(h.attacker,h.victim,false);
        firstHit(w,h,300);
        const auto& o=w.unit(h.victim)->orders;
        const bool attack=!o.empty() && o[0].targetId==h.attacker && o[0].chaseLeashed;
        const auto ret=std::find_if(o.begin(),o.end(),[](const sim::Order& x){return !x.targetId;});
        const bool back=ret!=o.end() && ret->groundMission && ret->groundResponse.mode==1 && !ret->patrol;
        const bool patrolKept=std::any_of(o.begin(),o.end(),[](const sim::Order& x){return x.patrol;});
        check(h.hit && attack && back && patrolKept,"retaliation: a patrolling Defensive unit attacks with a return and keeps its patrol");
    }
    {   // 4: attacker beyond 0.9 leash + range: no mission.
        sim::World w;setup(w,2);Hit h;
        h.victim=w.spawn(&victimType,400,1000,0,0);h.attacker=w.spawn(&r2,1050,1000,0,1);
        auto& v=*w.unit(h.victim);v.moveState=1;v.fireState=2;
        w.attack(h.attacker,h.victim,false);
        firstHit(w,h,400);
        check(h.hit && w.unit(h.victim)->orders.empty(),"retaliation: an attacker beyond 0.9 x leash + range gets no mission");
    }
    {   // 5: 409b56..409b67: an armed building still under construction skips
        // both the retaliation mission (a) and the slot handoff (b); the same
        // building finished answers. The attacker stands outside the tower's
        // sight, so the sweep cannot pick it: only the hit response can.
        auto tower=gunner(160);tower.id="tower";tower.canMove=false;tower.maxVel=sim::Fixed();
        tower.buildTime=1000;   // an unattended site decays slowly instead of vanishing
        for (const bool site:{true,false}) {
            sim::World w;setup(w,2);Hit h;
            h.victim=w.spawn(&tower,400,1000,0,0);h.attacker=w.spawn(&r1,650,1000,0,1);
            auto& v=*w.unit(h.victim);v.fireState=2;v.underConstruction=site;
            w.attack(h.attacker,h.victim,false);
            // A site's HP moves without a hit (no builder): watch the hit itself.
            for (int t=0;t<300 && !h.hit && w.unit(h.victim);++t) {
                ticks(w,1);
                h.hit=w.unit(h.victim) && w.unit(h.victim)->lastHitBy==h.attacker;
            }
            if (!w.unit(h.victim)) {check(false,"retaliation: the building survives its first hit");continue;}
            const auto& u=*w.unit(h.victim);
            const bool answered=(!u.orders.empty() && u.orders[0].targetId==h.attacker) ||
                u.slotTarget[0]==h.attacker;
            if (site) check(h.hit && u.orders.empty() && u.slotTarget[0]==0,
                            "retaliation: an unfinished building under fire takes no target");
            else check(h.hit && answered,"retaliation: the finished building answers the same hit");
        }
    }
}

// 412a77..412ae2 on a synthetic unit (UnitDef +0x18e turnrate, +0x16e/+0x172
// water/road multiplier, mover +0x36 flags), emulated in the retail binary.
void turnRate() {
    struct Case { uint16_t turn; int32_t road,water; uint16_t flags; uint16_t want; const char* label; };
    const Case cases[]={
        {300,0x13333,0x10000,0,     300,"turnrate: off-road dry ground keeps the type's rate"},
        {300,0x13333,0x10000,0x800, 359,"turnrate: road bit scales by roadmultiplier, truncated"},
        {300,0x13333,0x8000, 0x1000,150,"turnrate: water bit scales by watermultiplier"},
        {300,0x13333,0x8000, 0x1800,359,"turnrate: road wins over water"},
        {455,0x18000,0x6666, 0x800, 682,"turnrate: 1.5 road"},
        {455,0x18000,0x6666, 0x1000,181,"turnrate: 0.4 water truncates toward zero"},
        {333,0x15555,0xaaaa, 0x1000,221,"turnrate: 2/3 water"},
        {999,0x10001,0,      0x800, 999,"turnrate: just over 1.0 road"},
        {1,  0x8000, 0,      0x800, 0,  "turnrate: a product under 1 truncates to 0"},
        {0,  0x13333,0,      0x800, 0,  "turnrate: zero stays zero"},
        {700,-0x8000,0x10000,0x800, 65186,"turnrate: a negative multiplier wraps in 16 bits"},
        {900,0x640000,0,     0x800, 24464,"turnrate: an overflowing product keeps the low 16 bits"},
        {65535,0x20000,0,    0x1000,0,"turnrate: a zero water multiplier gives 0"},
    };
    for(const Case& c:cases)
        check(sim::retailEffectiveTurnRate(c.turn,c.road,c.water,c.flags)==c.want,c.label);
    // In the sweep: a 600 turnrate pays the two penalty draws; on a 2.0 road
    // it turns at 1200 and pays none, in water at 0.5 it still pays them.
    auto draws=[](int32_t road,int32_t water,uint16_t flags) {
        auto g=gunner(400);g.roadMult=sim::Fixed::raw(road);g.waterMult=sim::Fixed::raw(water);
        const auto d=dummy();
        sim::World w;setup(w,2);
        const int shooter=w.spawn(&g,400,1000,0,0);w.spawn(&d,400,800,0,1);
        w.unit(shooter)->fireState=0;
        int picked=0;
        for(int i=0;i<8 && !picked;++i){ticks(w,1);picked=w.retailSelectTarget(*w.unit(shooter),0,true);}
        if(!picked)return -1;
        int n=0;w.setRngObserver([&](const sim::World::RngObservation&){++n;});
        w.unit(shooter)->groundTerrainFlags=flags;
        return w.retailSelectTarget(*w.unit(shooter),0,true) ? n : -1;
    };
    const int base=draws(0x20000,0x8000,0);
    check(base>0 && draws(0x20000,0x8000,0x800)==base-2,"turnrate: a 2.0 road lifts the sweep's rate past the penalty");
    check(base>0 && draws(0x20000,0x8000,0x1000)==base,"turnrate: 0.5 water keeps the penalty draws");
}
