// Factory output release and AI Navy Yard siting, on shipped units and the map
// from a 0.9.0 report: a Creon Normal AI put its Navy Yard (crenavy) in a
// landlocked 111-cell pond on a generated map, at (1800,5272). The yard's exit
// step then ordered each new Submersible (cresubm) 316 px south, into the next
// body of water; the sub never moved, held the pad, and the yard built nothing
// for 3m46s until it was killed.
//
// Retail releases a factory's output with GetBuilt (0x402220): the factory's
// queued QMove/QPatrol orders, else PARK in a ring around it. Our release ports
// that, keeping the ring only when the output can reach it (see
// World::parkProductionOutput). The AI's coastal projection must also refuse
// the pond: its first wet ring around an enemy start is the sea, not the pond.
#include "ai/ai.h"
#include "ai/builtinprofile.h"
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "tnt/mapgen.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace tak::ai {
struct PlacementProbe {
    static bool reach(const Controller& c,const sim::World& w,const sim::UnitType* ship,
                      float fx,float fz,float tx,float tz) {
        float x,z;return c.reachablePoint(w,ship,fx,fz,tx,tz,x,z);
    }
    static bool body(const Controller& c,const sim::World& w,const sim::UnitType* ship,float x,float z) {
        return c.navalBodyAtLeast(w,ship,x,z,Controller::kMinNavalBodyCells);
    }
};
}

using namespace tak;
namespace {
int failures=0,cases=0;
void check(bool ok,const std::string& label) {
    std::printf("[%s] %s\n",ok?"PASS":"FAIL",label.c_str());++cases;if(!ok)++failures;
}
struct Rect {int x,z,w,h;};
Rect footprint(const sim::Unit& u) {
    return {sim::footprintOrigin(u.x,u.type->footX),sim::footprintOrigin(u.z,u.type->footZ),u.type->footX,u.type->footZ};
}
bool intersects(const Rect& a,const Rect& b) {return a.x<b.x+b.w && b.x<a.x+a.w && a.z<b.z+b.h && b.z<a.z+a.h;}

// The reported room: generated map, 8 seats; the start spots it used.
constexpr auto kRecipe="~gen1~09000100000000000000038001800108ffffffffff0004";
const std::vector<std::pair<float,float>> kStarts{{4352,1792},{5376,3328},{4352,4352},{3328,5376},
    {1792,4352},{768,3328},{1792,1792},{3328,768}};

void setupPondMap(sim::World& w,const sim::TypeRegistry& registry,hpi::Vfs& vfs,sim::PathfindingMode mode) {
    w.setSerialThreads(true);w.setVisPlayer(-1);
    sim::MatchConfig cfg;cfg.vfs=&vfs;cfg.mapPath="maps/shipyard-pond.tnt";cfg.loadCrt=false;
    cfg.pathfindingMode=mode;cfg.slots.resize(1);sim::setupMatch(w,registry,cfg);
}

// A Navy Yard on the pond, building Submersibles. Each must leave the pad and
// the yard must go on to the next.
void pondYard(const sim::TypeRegistry& registry,hpi::Vfs& vfs,sim::PathfindingMode mode,bool crusades) {
    sim::World w;setupPondMap(w,registry,vfs,mode);
    const auto* sub=registry.find("cresubm");
    auto yardType=*registry.find("crenavy");yardType.workerTime*=20;yardType.storage=1000000;
    const int yard=w.spawn(&yardType,1800,5272,0,0);
    w.player(0).mana=1000000;w.train(yard,sub,3);
    const Rect pad=footprint(*w.unit(yard));
    std::vector<int> subs;int firstDone=-1,firstClear=-1,tick=0;
    for(;tick<12000;++tick) {
        w.player(0).mana=1000000;w.tick(1.f/30);
        for(const auto& u:w.units())
            if(u.alive() && u.type==sub && std::find(subs.begin(),subs.end(),u.id)==subs.end())subs.push_back(u.id);
        if(!subs.empty()) {
            const auto* first=w.unit(subs.front());
            if(first && !first->underConstruction) {
                if(firstDone<0)firstDone=tick;
                if(firstClear<0 && !intersects(footprint(*first),pad))firstClear=tick;
            }
        }
        if(w.unit(yard)->buildQueue.empty() && firstClear>=0)break;
    }
    for(int settle=0;settle<900;++settle)w.tick(1.f/30);
    int finished=0,onPad=0;
    for(int id:subs)if(const auto* u=w.unit(id);u && u->alive() && !u->underConstruction) {
        ++finished;onPad+=intersects(footprint(*u),pad);
    }
    std::printf("pond balance=%s mode=%s subs=%zu finished=%d firstClearAfter=%d onPad=%d ticks=%d hash=%016llx\n",
        crusades?"Crusades":"standard",mode==sim::PathfindingMode::Legion?"Legion":"Retail",subs.size(),finished,
        firstClear<0?-1:firstClear-firstDone,onPad,tick,(unsigned long long)w.stateHash());
    check(firstClear>=0 && firstClear-firstDone<=600,"a Submersible built on the pond yard leaves the pad");
    check(finished==3 && onPad==0,"the pond yard builds its next Submersibles, none left on the pad");
}

// The AI's view of the same pond: no enemy start projects onto it, it is too
// small for a fleet, and the open sea a Creon yard used in that game still is.
void pondSiting(const sim::TypeRegistry& registry,hpi::Vfs& vfs) {
    sim::World w;setupPondMap(w,registry,vfs,sim::PathfindingMode::Retail);
    const auto* sub=registry.find("cresubm");
    const float padX=1802,padZ=5298;
    check(!w.pathExists(sub,1872,5616,padX,padZ),"fixture: the pond is not connected to the sea south of it");
    std::vector<std::pair<float,float>> enemies(kStarts.begin(),kStarts.end());
    enemies.erase(enemies.begin()+4);   // seat 4 placed the yard
    ai::Controller controller(4,registry,ai::builtinProfile(ai::Difficulty::Normal),1,ai::Difficulty::Normal,enemies);
    int accepted=0;
    for(const auto& [x,z]:enemies)accepted+=ai::PlacementProbe::reach(controller,w,sub,padX,padZ,x,z);
    check(accepted==0,"no enemy start's coastal projection lands in the landlocked pond");
    check(!ai::PlacementProbe::body(controller,w,sub,padX,padZ),"a 111-cell pond is too small a body for a Navy Yard");
    std::vector<std::pair<float,float>> west(kStarts.begin(),kStarts.end());
    west.erase(west.begin()+5);
    ai::Controller seat5(5,registry,ai::builtinProfile(ai::Difficulty::Normal),1,ai::Difficulty::Normal,west);
    int seaAccepted=0;
    for(const auto& [x,z]:west)seaAccepted+=ai::PlacementProbe::reach(seat5,w,sub,538,2194,x,z);
    check(seaAccepted>0 && ai::PlacementProbe::body(seat5,w,sub,538,2194),
          "the west sea a Creon yard used in the same game is still accepted");
}

// A land factory parks its output in front of it, inside PARK's ring; a rally
// point still wins over PARK.
void landFactory(const sim::TypeRegistry& registry,sim::PathfindingMode mode,bool rally) {
    sim::World w;w.setSerialThreads(true);w.setVisPlayer(-1);w.setPathfindingMode(mode);
    w.setTerrain(std::vector<uint8_t>(256*256,100),256,256,20);
    w.setMapPlacementFeatures(std::vector<uint16_t>(256*256,0xffff),{});
    w.buildNavClasses(registry);w.setPathService(true);
    auto barracks=*registry.find("arakeep");barracks.workerTime*=20;barracks.storage=1000000;
    const auto* sword=registry.find("arasword");
    const int id=w.spawn(&barracks,1800,1800,0,0);
    if(rally)w.order(id,2600,1000,false);
    w.player(0).mana=1000000;w.train(id,sword,4);
    const Rect yard=footprint(*w.unit(id));
    std::vector<int> out;bool parked=true,rallied=true;
    for(int tick=0;tick<6000;++tick) {
        w.player(0).mana=1000000;w.tick(1.f/30);
        for(const auto& u:w.units())
            if(u.alive() && u.type==sword && !u.underConstruction && std::find(out.begin(),out.end(),u.id)==out.end()) {
                out.push_back(u.id);
                // Its release, the tick it is finished.
                const bool park=std::any_of(u.orders.begin(),u.orders.end(),[](const auto& o){return bool(o.park);});
                parked=parked && park;
                rallied=rallied && !park && !u.orders.empty();
            }
        if(out.size()==4 && std::all_of(out.begin(),out.end(),[&](int u){return w.unit(u)->orders.empty();}))break;
    }
    int front=0,inRing=0,arrived=0,inYard=0;
    const auto* f=w.unit(id);
    const int fx=sim::footprintOrigin(f->x,sword->footX),fz=sim::footprintOrigin(f->z,sword->footZ);
    for(int u:out) {
        const auto* s=w.unit(u);
        inYard+=intersects(footprint(*s),yard);
        const float dx=s->x.toFloat()-f->x.toFloat(),dz=s->z.toFloat()-f->z.toFloat();
        front+=dz>0 && std::abs(dx)<dz;
        // PARK's ring: footX*16 + padding 48..160 + half the Barracks diagonal.
        const int cx=sim::footprintOrigin(s->x,sword->footX)-fx,cz=sim::footprintOrigin(s->z,sword->footZ)-fz;
        const int cells2=cx*cx+cz*cz;
        const int half=int(std::sqrt(double(barracks.footX*barracks.footX+barracks.footZ*barracks.footZ))*8);
        const int inner=sword->footX*16+48+half,outer=3*(sword->footX*16+160+half);
        inRing+=cells2*256>=inner*inner && cells2*256<=outer*outer;
        const float rx=s->x.toFloat()-2600,rz=s->z.toFloat()-1000;
        arrived+=rx*rx+rz*rz<160*160;
    }
    const std::string label=std::string(mode==sim::PathfindingMode::Legion?"Legion: ":"Retail: ");
    std::printf("land mode=%s rally=%d outputs=%zu parked=%d rallied=%d front=%d inRing=%d arrived=%d inYard=%d hash=%016llx\n",
        mode==sim::PathfindingMode::Legion?"Legion":"Retail",rally,out.size(),parked,rallied,front,inRing,arrived,inYard,
        (unsigned long long)w.stateHash());
    if(rally) {
        check(out.size()==4 && rallied && arrived==4 && inYard==0,label+"a Barracks with a rally point sends its output there, not to PARK");
    } else {
        check(out.size()==4 && parked,label+"a Barracks without a rally point releases its output with PARK");
        // Legion's exact ring goal queues a column behind the first output and
        // releases each once clear of the factory (LegionNavigator), so only
        // the Retail navigator is held to the ring itself.
        if(mode==sim::PathfindingMode::Legion)
            check(front==4 && inYard==0,label+"the parked output stands in front of the Barracks, clear of it");
        else check(front==4 && inRing==4 && inYard==0,label+"the parked output stands in front of the Barracks, in PARK's ring");
    }
}
}

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto vfs=hpi::mountRetailRoot(argv[1]);
    auto generated=mapgen::generate(mapgen::decodeMapId(kRecipe),vfs);
    auto files=std::make_shared<hpi::Vfs::Files>();(*files)["maps/shipyard-pond.tnt"]=generated.map.save();vfs.setMapFiles(files);
    for(bool crusades:{true,false}) {
        sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,crusades);
        if(crusades)pondSiting(registry,vfs);
        for(auto mode:{sim::PathfindingMode::Retail,sim::PathfindingMode::Legion}) {
            pondYard(registry,vfs,mode,crusades);
            if(crusades)for(bool rally:{false,true})landFactory(registry,mode,rally);
        }
    }
    std::printf("shipyard park: %d cases, %d failures\n",cases,failures);
    return failures?1:0;
}
