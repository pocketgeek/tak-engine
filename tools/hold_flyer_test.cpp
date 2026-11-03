// A flyer on Hold Position does not leave its post to attack what it finds.
//   hold_flyer_test <data> [trace]
//
// Retail's VTOL_Standby scans (4d8370, 4176bc) and offers what it finds to
// 4d7de0(unit, found, 0, 0, 1, 0) (4176cc), the call that inserts the auto
// attack. 4d7de0 refuses it on Hold Position: (+0x130 & 0x30000) == 0 with
// neither explicit (arg 3) nor the forced return (arg 6) set returns 0
// (4d7e0a..4d7e19), so no attack mission is pushed and the standby goes on
// (417736). What is left is the fire-at-will sweep (40f330): the auto slots
// take a target in the weapon's own range and fire from where the flyer is.
//
// Every armed flyer in the shipped data, on Hold Position, fire at will,
// with a seen enemy inside its weapon range (450 px when the weapon reaches
// it): no attack order and the flyer stays at its post, and most fire at it
// from there through their auto slots (before the fix every one of them
// that acquired it flew off 190-960 px after it). On Maneuver the same
// flyer takes the attack (the case that shows the test can fail). A flyer
// whose weapon reaches the air is also tried against a hovering enemy, which
// is AirToAir's (41ca50) attack when ordered.
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/powercodes.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
using namespace tak;
namespace {
int failures=0;
void check(bool ok,const std::string& label) {
    if (!ok) std::printf("FAIL %s\n",label.c_str());
    failures+=!ok;
}
struct Result { bool attacked=false; float drift=0; int shots=0; bool slotTook=false; };
Result runCase(const sim::TypeRegistry& registry,const sim::UnitType& flyer,bool airTarget,float dist,
               uint8_t moveState,bool trace,int ticks) {
    sim::World w;
    w.setSerialThreads(true);
    w.setPlayerCount(2);
    w.setTerrain(std::vector<uint8_t>(512*512,70),512,512,64);
    w.buildNavClasses(registry);
    w.setPathService(true);
    w.player(0).mana=1e6;w.player(1).mana=1e6;
    // The sweep takes only enemies its player sees (Player::visibleEnemies).
    w.setPowerCodes(true);
    w.applyPowerCode(0,sim::PowerCode::NowISee,sim::PowerArg::None,0);
    const int a=w.spawn(&flyer,4096,4096,1.5707963f,0);
    w.unit(a)->mana=flyer.maxMana;
    w.unit(a)->moveState=moveState;w.unit(a)->fireState=2;
    // Let it settle into its standby (landed, or hovering for one that
    // cannot land) before the enemy shows up.
    for (int i=0;i<90;++i) w.tick(1.f/30);
    const float postX=w.unit(a)->x.toFloat(),postZ=w.unit(a)->z.toFloat();
    sim::UnitType target=*registry.find(airTarget ? "zongryp" : "araarch");
    target.maxHp=30000;target.healTime=0;target.weapons.clear();target.weapon.damage=0;
    const int t=w.spawn(&target,postX+dist,postZ,0,1);
    w.setStance(t,2);w.unit(t)->moveState=0;w.unit(t)->fireState=0;
    if (airTarget) w.order(t,postX+dist,postZ+12,false);
    Result r;
    int lastReload[3]{};
    for (int tick=0;tick<ticks;++tick) {
        w.unit(t)->hp=sim::Fixed::fromInt(30000);
        if (airTarget && tick%30==29) w.order(t,postX+dist,postZ+((tick/30)&1 ? 12.f : -12.f),false);
        w.tick(1.f/30);
        const sim::Unit& u=*w.unit(a);
        for (const auto& o:u.orders) if (o.targetId==t) r.attacked=true;
        for (int s=0;s<3;++s) {
            // A shot sets the bit, or (for a scripted fire that reports
            // none) restarts the slot's reload.
            if ((u.firedWeapons>>s&1) || u.reloads[size_t(s)]>lastReload[s]) ++r.shots;
            lastReload[s]=u.reloads[size_t(s)];
            if (u.slotTarget[size_t(s)]==t) r.slotTook=true;
        }
        const float dx=u.x.toFloat()-postX,dz=u.z.toFloat()-postZ;
        r.drift=std::max(r.drift,std::sqrt(dx*dx+dz*dz));
        if (trace && tick%30==0)
            std::printf("  t=%3d pos=%.1f,%.1f y=%.1f mode=%d orders=%zu slot=%d fired=%x\n",tick,u.x.toFloat(),
                        u.z.toFloat(),u.flightY.toFloat(),int(u.flightGroundMode),u.orders.size(),
                        u.slotTarget[0],u.firedWeapons);
    }
    return r;
}
} // namespace
int main(int argc,char** argv) {
    if (argc<2) return 2;
    const bool trace=argc>2 && std::strcmp(argv[2],"trace")==0;
    auto vfs=hpi::mountRetailRoot(argv[1]);
    sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,false);
    for (const char* id:{"zongryp","araarch"})
        if (!registry.find(id)) { std::printf("FAIL missing type %s\n",id);return 1; }
    std::vector<const sim::UnitType*> flyers;
    for (const auto& [id,type]:registry.types())
        if (type.canFly && !type.isStructure() && type.weapon.damage>0 && !type.weapons.empty())
            flyers.push_back(&type);
    int cases=0,maneuverTook=0,held=0,firedFromPost=0;
    for (const auto* f:flyers) {
        for (bool air:{false,true}) {
            const sim::Weapon& wp=f->weapons.front();
            if (wp.melee) continue;
            if (air && wp.noAir) continue;
            if (!air && wp.toAir) continue;
            const float range=float(f->maxRange());
            const float dist=range>470.f ? 450.f : range*0.8f;
            if (trace) std::printf("%s %s at %.0f (range %.0f, hover %d)\n",f->id.c_str(),air?"air":"ground",
                                   dist,range,int(f->hoverAttack));
            const Result hold=runCase(registry,*f,air,dist,0,trace,450);
            const Result maneuver=runCase(registry,*f,air,dist,1,false,450);
            ++cases;
            maneuverTook+=maneuver.attacked;
            held+=!hold.attacked && hold.drift<=24.f;
            firedFromPost+=hold.shots>0;
            std::printf("  %-9s %-6s at %3.0f: Hold attack=%d drift=%.1f shots=%d slot=%d | Maneuver attack=%d drift=%.1f\n",
                        f->id.c_str(),air?"air":"ground",dist,int(hold.attacked),hold.drift,hold.shots,
                        int(hold.slotTook),int(maneuver.attacked),maneuver.drift);
            const std::string label=f->id+(air?" (air target)":" (ground target)");
            check(!hold.attacked,label+": no attack order on Hold Position (4d7de0 refuses the standby's call)");
            check(hold.drift<=24.f,label+": stays at its post on Hold Position");
        }
    }
    std::printf("hold_flyer: %d cases, %d held, %d fired from their post, %d took the attack on Maneuver\n",
                cases,held,firedFromPost,maneuverTook);
    // The control: on Maneuver the standby's call goes through, so the
    // attack the Hold case must not get is there to be got.
    check(cases>=8,"enough armed flyers in the shipped data");
    check(maneuverTook*2>=cases,"on Maneuver most flyers take the attack (the test can fail)");
    // Hold Position is not Hold Fire: the sweep's auto slots still shoot a
    // seen enemy in range from where the flyer stands by.
    check(firedFromPost*2>=cases,"on Hold Position most flyers fire from their post");
    std::printf("hold_flyer: %d failures\n",failures);
    return failures?1:0;
}
