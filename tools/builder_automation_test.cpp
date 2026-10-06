#include "sim/matchsetup.h"
#include "net/protocol.h"
#include "client/renderframe.h"
#include "hpi/hpi.h"
#include <algorithm>
#include <cstdio>
#include <limits>
using namespace tak;
namespace {
int failures=0;
void check(bool ok,const char* label) {
    std::printf("%s %s\n",ok?"PASS":"FAIL",label);failures+=!ok;
}
sim::UnitType worker(bool air=false) {
    sim::UnitType t;t.id="worker";t.isBuilder=t.canMove=t.canReclaim=t.canPatrol=true;
    t.canFly=air;t.upright=true;t.maxHp=100;t.footX=t.footZ=2;t.maxVel=sim::Fixed::fromInt(3);
    t.turnRate=t.turnInPlaceRate=10000;t.sight=160;t.sightHeight=24;t.workerTime=100;
    t.buildDist=80;t.storage=10000;return t;
}
sim::UnitType lodestone() {
    sim::UnitType t;t.id="lode";t.side="ARA";t.maxHp=100;t.maxVel={};
    t.footX=t.footZ=4;t.onMana=true;t.buildTime=100;t.buildCost=10;return t;
}
void setup(sim::World& w,sim::PathfindingMode mode,bool serial=true) {
    w.setVisPlayer(-1);w.setSerialThreads(serial);w.setPathfindingMode(mode);
    w.setTerrain(std::vector<uint8_t>(128*128,100),128,128,20);
    w.setPathService(true);w.player(0).mana=10000;
}
void ticks(sim::World& w,int n) {for(int i=0;i<n;++i)w.tick(1.f/30);}
uint64_t areaBuild(sim::PathfindingMode mode,bool air,bool serial=true) {
    sim::World w;setup(w,mode,serial);auto builder=worker(air);builder.sight=100;auto lode=lodestone();
    w.setManaSpots({{512,256},{896,256},{1280,640}});
    const int id=w.spawn(&builder,256,400,0,0);
    w.addFeature(7,896,256,1,1,1,1,true);
    w.queueManaBuildArea(id,&lode,1050,400,400,128,false);
    check(w.unit(id)->orders.size()==1 && w.unit(id)->orders.front().manaBuildArea,
          "reversed box remains one bounded area order");
    UnitR snapshot;snapshot.captureOrders(*w.unit(id));
    check(snapshot.hasQueuedBuild() && snapshot.hasQueuedWork() && snapshot.orders.front().manaBuildArea &&
          snapshot.orders.front().buildType==&lode,
          "render snapshot reports pending area construction");
    w.tick(1.f/30);
    check(w.unit(id)->orders.size()==2 && !w.unit(id)->orders.front().manaBuildArea &&
          w.unit(id)->buildSiteId==0,"unexplored deposit starts an exploration approach before any construction");
    bool exploredAtBirth=true;
    for(int tick=0;tick<6000 && !w.unit(id)->orders.empty();++tick) {
        w.tick(1.f/30);
        if (const auto* site=w.unit(w.unit(id)->buildSiteId)) {
            const int x=site->x.floorInt()/32,z=site->z.floorInt()/32;
            exploredAtBirth&=(w.navigationExploration()[size_t(z)*64+x]&1)!=0;
        }
    }
    const int complete=int(std::count_if(w.units().begin(),w.units().end(),[&](const auto& u) {
        return u.alive() && u.type==&lode && !u.underConstruction;
    }));
    check(complete==2 && w.unit(id)->orders.empty(),"area builds every enclosed deposit and completes in both path modes, ground and air");
    check(exploredAtBirth && !w.feature(7)->alive,"area explores first and reuses obstacle-clearing construction");
    check(w.canPlace(&lode,1280,640,0),"deposit outside the box stays untouched");
    return w.stateHash();
}
void areaPolicy() {
    sim::World w;setup(w,sim::PathfindingMode::Retail);auto builder=worker();auto lode=lodestone();
    const int id=w.spawn(&builder,256,400,0,0);w.setManaSpots({{512,256},{896,256}});
    w.order(id,320,400,false);w.queueManaBuildArea(id,&lode,400,128,1050,400,true);
    check(w.unit(id)->orders.back().manaBuildArea && w.unit(id)->orders.front().groundMission,
          "Shift area queues behind existing movement");
    w.cancelBuilds(id);w.stop(id);check(w.unit(id)->orders.empty(),"Stop cancels area construction and exploration");
    w.queueManaBuildArea(id,&lode,400,128,1050,400,false);
    w.cancelBuilds(id);w.order(id,640,640,false);
    check(w.unit(id)->orders.size()==1 && !w.unit(id)->orders.front().manaBuildArea,
          "replacement move cancels the persistent area");
    auto other=lode;other.onMana=false;
    w.queueManaBuildArea(id,&other,0,0,1024,1024,false);
    check(w.unit(id)->orders.size()==1 && !w.unit(id)->orders.front().manaBuildArea,
          "area construction rejects non-lodestone types");
    w.queueManaBuildArea(id,&lode,std::numeric_limits<float>::quiet_NaN(),0,1024,1024,false);
    check(w.unit(id)->orders.size()==1 && !w.unit(id)->orders.front().manaBuildArea,"invalid area coordinates leave prior orders intact");
    w.unit(id)->repeatType=&lode;
    w.queueManaBuildArea(id,&lode,0,0,1024,1024,false);
    check(!w.unit(id)->orders.front().manaBuildArea,"infinite mobile production retains its Stop-only construction gate");
    w.unit(id)->repeatType=nullptr;w.stop(id);
    const int occupied=w.spawn(&lode,512,256,0,1);w.blockFoot(lode,512,256,true);
    w.queueManaBuildArea(id,&lode,400,128,1050,400,false);ticks(w,6000);
    check(w.unit(occupied)->player==1 && std::count_if(w.units().begin(),w.units().end(),[&](const auto& u) {
        return u.alive() && u.player==0 && u.type==&lode && !u.underConstruction;
    })==1 && w.unit(id)->orders.empty(),"occupied enemy deposit is skipped without blocking later sites");
}
uint64_t patrolRepair(sim::PathfindingMode mode,bool air,bool serial=true) {
    sim::World w;setup(w,mode,serial);auto builder=worker(air);auto target=lodestone();target.onMana=false;
    const int id=w.spawn(&builder,256,512,0,0);
    const int damaged=w.spawn(&target,640,592,0,0);w.unit(damaged)->hp=sim::Fixed::fromInt(25);
    w.blockFoot(target,640,592,true);w.patrol(id,1024,512);
    bool repaired=false,patrolRetained=false;
    for(int tick=0;tick<2400;++tick) {
        w.tick(1.f/30);
        if (w.unit(damaged)->hp==sim::Fixed::fromInt(100)) repaired=true;
        if (repaired && w.unit(id)->x.toFloat()>900) patrolRetained=true;
    }
    check(repaired && patrolRetained,"ground and flying patrols repair an encountered building and then continue their route");
    check(std::count_if(w.unit(id)->orders.begin(),w.unit(id)->orders.end(),[](const auto& o){return o.goal && o.patrol;})==2,
          "automatic repair preserves the original two patrol waypoints");
    check(w.player(0).mana<10000,"automatic repairs consume ordinary repair mana");
    return w.stateHash();
}
void repairPolicy() {
    sim::World w;setup(w,sim::PathfindingMode::Retail);auto builder=worker();builder.sight=200;
    const int id=w.spawn(&builder,256,512,0,0);auto target=lodestone();target.onMana=false;
    const int ally=w.spawn(&target,352,512,0,1);w.setTeam(1,0);w.unit(ally)->hp=sim::Fixed::fromInt(50);
    w.patrol(id,1024,512);ticks(w,35);
    check(w.unit(id)->repairId==ally || w.unit(ally)->hp>sim::Fixed::fromInt(50),"patrolling builders repair teammates as well as their own units");
    w.player(0).mana=0;const auto hp=w.unit(ally)->hp;ticks(w,15);
    check(w.unit(ally)->hp==hp,"automatic repair pauses when mana is exhausted");
    w.setTeam(1,1);ticks(w,3);
    check(!w.unit(id)->repairId && std::none_of(w.unit(id)->orders.begin(),w.unit(id)->orders.end(),[](const auto& o){return o.repairTarget!=0;}),
          "automatic repair ends when a target becomes hostile");
    w.cancelBuilds(id);w.stop(id);check(!w.unit(id)->repairId && w.unit(id)->orders.empty(),"Stop cancels repair and patrol together");
    w.setTeam(1,0);w.player(0).mana=10000;w.patrol(id,1024,512);ticks(w,35);
    w.unit(ally)->x=sim::Fixed::fromInt(1800);ticks(w,3);
    check(!w.unit(id)->repairId,"automatic repairs do not chase a departing ally across the map");
    w.cancelBuilds(id);w.stop(id);w.unit(ally)->hp={};w.unit(ally)->x=sim::Fixed::fromInt(352);
    w.patrol(id,1024,512);ticks(w,35);
    check(!w.unit(id)->repairId,"patrol repair does not resurrect a dying target");
}
void repairEligibility() {
    for(auto mode:{sim::PathfindingMode::Retail}) {
        sim::World w;setup(w,mode);auto builder=worker();builder.sight=200;
        auto target=lodestone();target.onMana=false;target.footX=target.footZ=2;
        const int id=w.spawn(&builder,256,512,0,0);
        const int enemy=w.spawn(&target,272,512,0,1);w.unit(enemy)->hp=sim::Fixed::fromInt(20);
        const int unfinished=w.spawn(&target,272,544,0,0);
        w.unit(unfinished)->hp=sim::Fixed::fromInt(20);w.unit(unfinished)->underConstruction=true;
        const int cargo=w.spawn(&target,272,480,0,0);
        w.unit(cargo)->hp=sim::Fixed::fromInt(20);w.unit(cargo)->inTransport=id;
        const int unreachable=w.spawn(&target,384,512,0,0);w.unit(unreachable)->hp=sim::Fixed::fromInt(20);
        w.blockCells(20,0,2,128,true);
        const int mobile=w.spawn(&builder,256,608,0,0);w.unit(mobile)->hp=sim::Fixed::fromInt(20);
        w.patrol(id,288,736);ticks(w,50);
        check(w.unit(mobile)->hp>sim::Fixed::fromInt(20) && w.unit(enemy)->hp==sim::Fixed::fromInt(20) &&
              w.unit(unreachable)->hp==sim::Fixed::fromInt(20) && w.unit(cargo)->hp==sim::Fixed::fromInt(20),
              "patrol repairs mobile allies and skips enemies, construction, cargo and disconnected targets");
        w.cancelBuilds(id);w.stop(id);w.unit(mobile)->hp=sim::Fixed::fromInt(20);
        w.setPatrolRepairs(false);w.patrol(id,288,736);ticks(w,120);
        check(w.unit(mobile)->hp==sim::Fixed::fromInt(20) && !w.unit(id)->repairId,
              "legacy replay policy retains patrols without automatic repair");
    }
}
void movingRepair() {
    for(auto mode:{sim::PathfindingMode::Retail})
        for(bool air:{false,true}) {
            sim::World w;setup(w,mode);auto builder=worker(air);builder.sight=300;builder.workerTime=10;
            auto allyType=worker();allyType.buildTime=100;allyType.buildCost=10;
            const int id=w.spawn(&builder,256,512,0,0);
            const int ally=w.spawn(&allyType,352,512,0,0);w.unit(ally)->hp=sim::Fixed::fromInt(20);
            w.patrol(id,1024,512);ticks(w,35);
            check(w.unit(id)->repairId==ally,"moving-ally fixture starts a patrol repair");
            w.unit(ally)->x=sim::Fixed::fromInt(448);w.unit(ally)->z=sim::Fixed::fromInt(608);
            ticks(w,900);
            check(w.unit(ally)->hp==sim::Fixed::fromInt(100) &&
                  std::count_if(w.unit(id)->orders.begin(),w.unit(id)->orders.end(),[](const auto& o){return o.goal && o.patrol;})==2,
                  "patrol repair refreshes a nearby ally's position and retains its patrol");
        }
}
void areaCommandPolicy(const sim::TypeRegistry& registry) {
    const auto* lode=registry.find("aralode");const auto* foreign=registry.find("zonmana");
    const sim::UnitType* source=nullptr;
    for(const auto& [name,type]:registry.types()) {
        const auto& menu=registry.buildable(name);
        if(type.isBuilder && !type.isStructure() &&
           std::find(menu.begin(),menu.end(),"aralode")!=menu.end()) {source=&type;break;}
    }
    check(source && lode && foreign,"area command authorization fixture has retail builders and lodestones");
    if(!source || !lode || !foreign) return;
    auto builder=*source;builder.builderLimited=true;
    sim::World w;setup(w,sim::PathfindingMode::Retail);w.setManaSpots({{512,512}});
    const int id=w.spawn(&builder,256,512,0,1);
    net::Command c;c.kind=net::Cmd::BuildManaArea;c.unitId=id;c.x=400;c.z=400;c.x2=600;c.z2=600;
    std::snprintf(c.type,sizeof c.type,"aralode");sim::applyCommand(w,registry,c);
    check(w.unit(id)->orders.empty(),"area command cannot control another player's builder");
    c.player=1;sim::applyCommand(w,registry,c);
    check(w.unit(id)->orders.size()==1 && w.unit(id)->orders.front().manaBuildArea,
          "owned builder accepts an authorized lodestone area command");
    w.stop(id);std::snprintf(c.type,sizeof c.type,"zonmana");sim::applyCommand(w,registry,c);
    check(w.unit(id)->orders.empty(),"area command rejects a lodestone absent from the builder's build menu");
}
void realLodestones(const char* install) {
    auto vfs=hpi::mountRetailRoot(install,hpi::OverridePolicy::None);
    for(bool crusades:{false,true}) {
        sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,crusades);
        areaCommandPolicy(registry);
        for(const char* race:{"ara","tar","ver","zon","cre"}) {
            const auto* basic=registry.find(std::string(race)+"lode");
            const auto* divine=registry.find(std::string(race)+"mana");
            check(basic && divine,"all factions have basic and Divine lodestone types");
            if (!basic || !divine) continue;
            const sim::UnitType* builder=nullptr;
            for(const auto& [name,type]:registry.types()) {
                const auto& menu=registry.buildable(name);
                if (type.isBuilder && !type.isStructure() &&
                    std::find(menu.begin(),menu.end(),divine->id)!=menu.end()) {builder=&type;break;}
            }
            check(basic && divine && builder,"all factions have an authorized Divine lodestone builder");
            if (!builder) continue;
            for(auto mode:{sim::PathfindingMode::Retail}) {
                sim::World w;setup(w,mode);w.buildNavClasses(registry);
                w.setManaSpots({{512,512},{896,512}});
                w.setSacredSites({{31,31,2,2,1},{55,31,2,2,1}});
                const int id=w.spawn(builder,512,800,0,0);
                auto bank=lodestone();bank.id="bank";bank.onMana=false;bank.income=10000;bank.storage=100000;
                w.spawn(&bank,128,128,0,0);w.player(0).mana=50000;
                const int old=w.startBuild(id,basic,512,512,sim::World::Approach::None);
                check(old!=0,"area-upgrade fixture admits the basic lodestone");if (!old) continue;
                w.unit(old)->underConstruction=false;w.unit(old)->hp=sim::Fixed::fromInt(basic->maxHp);
                w.cancelBuilds(id);w.stop(id);
                w.queueManaBuildArea(id,divine,464,464,944,560,false);
                bool replacement=false;
                for(int tick=0;tick<12000 && !w.unit(id)->orders.empty();++tick) {
                    w.tick(1.f/30);
                    if (const auto* site=w.unit(w.unit(id)->buildSiteId))
                        replacement|=site->lodestoneReplacement.has_value();
                }
                check(replacement && std::count_if(w.units().begin(),w.units().end(),[&](const auto& u) {
                    return u.alive() && u.type==divine && !u.underConstruction;
                })==2 && w.unit(id)->orders.empty(),"area upgrades its own basic lodestone and builds Divine on the empty spot, every faction/balance/path mode");
                std::printf("area-upgrade %s crusades=%d path=%d hash=%016llx\n",race,crusades,int(mode),
                            (unsigned long long)w.stateHash());
            }
        }
    }
}
}
int main(int argc,char** argv) {
    for(auto mode:{sim::PathfindingMode::Retail})
        for(bool air:{false,true}) {areaBuild(mode,air);patrolRepair(mode,air);}
    check(areaBuild(sim::PathfindingMode::Retail,false,true)==areaBuild(sim::PathfindingMode::Retail,false,false),
          "area build serial/threaded hashes agree");
    check(patrolRepair(sim::PathfindingMode::Retail,false,true)==patrolRepair(sim::PathfindingMode::Retail,false,false),
          "patrol repair serial/threaded hashes agree");
    areaPolicy();repairPolicy();repairEligibility();movingRepair();
    net::Command c;c.kind=net::Cmd::BuildManaArea;c.unitId=1;c.x=100;c.z=200;c.x2=800;c.z2=900;
    std::snprintf(c.type,sizeof c.type,"lode");net::Writer out;out.cmd(c);
    net::Reader in(out.b.data(),out.b.size());auto decoded=in.cmd();
    check(in.ok && decoded.kind==c.kind && decoded.x2==800 && decoded.z2==900 &&
          std::string(decoded.type)=="lode","lodestone-area wire roundtrip preserves its type and both corners");
    net::Reader shortIn(out.b.data(),out.b.size()-1);(void)shortIn.cmd();
    check(!shortIn.ok,"truncated lodestone-area command is rejected");
    net::Writer mixed;net::Command move;move.unitId=7;move.x=650;
    mixed.cmd(move);mixed.cmd(c);move.unitId=8;mixed.cmd(move);
    net::Reader batch(mixed.b.data(),mixed.b.size());
    const auto first=batch.cmd(),area=batch.cmd(),last=batch.cmd();
    check(batch.ok && batch.p==batch.end && first.unitId==7 && area.x2==800 && last.unitId==8,
          "area commands preserve framing between ordinary movement commands");
    if (argc>1) realLodestones(argv[1]);
    std::printf("%d failures\n",failures);return failures?1:0;
}
