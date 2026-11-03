// How fast a finished ship gets out of a Navy Yard (crenavy), and whether idle
// ships at the door hold it in (the 0.9.2 known limit: "ships leave some Navy
// Yards slowly, and a crowd of Iron Clads can block a yard door").
//
// Each case trains six ships (Submersible 2x2, Iron Clad and Stern Wheeler
// 3x3) at a yard on open water, and at one whose door opens partly onto a
// shore (land in front of the door's east two columns), in both pathfinding
// modes. Measured per output: ticks from completion until it is clear of the
// yard's rectangle (the pad is free for the next) and until it is clear of the
// door (the yard grown by kDoorCells); at the end none may stand in the yard
// or still be on its way out. Then the same with eight idle Iron Clads
// parked a body's gap apart just off the door, and with one idle Iron Clad on
// the pad itself. Retail moves no idle body: on origin/main the crowd held the
// first 3x3 output wedged just out of the yard and the yard built two ships of
// six; the Iron Clad on the pad stopped the yard outright. Now the yard owner's
// idle ships in front of the yard move aside (World::clearYardDoor), and the
// yard builds as fast as with the door clear.
//
// The remaining time out of a door facing a shore is retail's own: a boat
// whose forward probe meets a shore cell or a body within 176 px runs at a
// third of its speed (4dba80, retailGroundScan), so it crawls out of such a
// door. Exit times are printed per case (clear/door med and max).
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace tak;
namespace {
int failures=0,cases=0;
std::string only;bool trace=false;long traceIndex=0;int traceEvery=1;
void check(bool ok,const std::string& label) {
    std::printf("[%s] %s\n",ok?"PASS":"FAIL",label.c_str());++cases;if(!ok)++failures;
}
struct Rect {int x,z,w,h;};
Rect footprint(const sim::Unit& u) {
    return {sim::footprintOrigin(u.x,u.type->footX),sim::footprintOrigin(u.z,u.type->footZ),u.type->footX,u.type->footZ};
}
bool intersects(const Rect& a,const Rect& b) {return a.x<b.x+b.w && b.x<a.x+a.w && a.z<b.z+b.h && b.z<a.z+a.h;}
constexpr int kCells=256,kDoorCells=4;

enum class Layout {Open,Shore};
const char* layoutName(Layout l) {return l==Layout::Open?"open":"shore";}

struct Result {
    int produced=0,stuck=0,lastDone=-1,refusals=0,inDoor=0,nudged=0;
    std::vector<int> clear,door;
    int median(std::vector<int> v) const {
        if(v.empty())return -1;
        std::sort(v.begin(),v.end());return v[v.size()/2];
    }
    int max(const std::vector<int>& v) const {return v.empty()?-1:*std::max_element(v.begin(),v.end());}
};

enum class Crowd {None,Door,Pad};
const char* crowdName(Crowd c) {return c==Crowd::None?"none":c==Crowd::Door?"door":"pad";}

Result run(const sim::TypeRegistry& registry,sim::PathfindingMode mode,Layout layout,const char* shipName,int count,
           Crowd crowd) {
    sim::World w;w.setSerialThreads(true);w.setPathfindingMode(mode);
    // Deep water (sea level 100) with land north of the yard; the shore layout
    // also puts land south-east of the door, so the door opens partly onto
    // the shore.
    constexpr float fx=1800,fz=1792;
    std::vector<uint8_t> heights(kCells*kCells,0);
    for(int z=0;z<kCells;++z)for(int x=0;x<kCells;++x) {
        // The yard covers cells 109..115 x 108..116; its door is row 116.
        const bool land=z<108 || (layout==Layout::Shore && x>=114 && z>=117 && z<127);
        if(land)heights[size_t(z)*kCells+x]=160;
    }
    w.setTerrain(heights,kCells,kCells,100);
    w.setMapPlacementFeatures(std::vector<uint16_t>(kCells*kCells,0xffff),{});
    w.buildNavClasses(registry);w.setPathService(true);
    auto yardType=*registry.find("crenavy");yardType.workerTime*=4;yardType.storage=1000000;
    const auto* ship=registry.find(shipName);
    const auto* iron=registry.find("creiron");
    const int id=w.spawn(&yardType,fx,fz,0,0);
    const Rect yard=footprint(*w.unit(id));
    const Rect door{yard.x-kDoorCells,yard.z-kDoorCells,yard.w+2*kDoorCells,yard.h+2*kDoorCells};
    // Idle Iron Clads parked in front of the door, a body's gap apart, or one
    // on the pad.
    const float doorZ=float((yard.z+yard.h)*16);
    std::vector<int> idle;
    if(crowd==Crowd::Door)
        for(int i=0;i<8;++i) {
            // On the shore layout the crowd shifts west, onto the water.
            const float x=(layout==Layout::Shore?fx-208:fx-96)+float(i%4)*64;
            idle.push_back(w.spawn(iron,x,doorZ+40+float(i/4)*64,0,0));
        }
    if(crowd==Crowd::Pad)idle.push_back(w.spawn(iron,fx,fz+32,0,0));
    for(int t=0;t<30;++t)w.tick(1.f/30);
    w.player(0).mana=1000000;w.train(id,ship,count);
    struct Out {int id,done,clear=-1,door=-1;Rect birth;bool rested=false;};
    std::vector<Out> outs;Result r;uint32_t lastRetry=0;int tick=0;
    for(;tick<20000;++tick) {
        w.player(0).mana=1000000;w.tick(1.f/30);
        const auto* f=w.unit(id);
        if(f->productionRetryTick && f->productionRetryTick!=lastRetry)++r.refusals;
        lastRetry=f->productionRetryTick;
        for(const auto& u:w.units()) {
            if(!u.alive() || u.type!=ship || u.underConstruction || u.id==id)continue;
            auto at=std::find_if(outs.begin(),outs.end(),[&](const Out& o){return o.id==u.id;});
            if(at==outs.end()) {
                if(std::find(idle.begin(),idle.end(),u.id)!=idle.end())continue;   // a crowd body, not an output
                outs.push_back({u.id,tick,-1,-1,footprint(u)});at=outs.end()-1;r.lastDone=tick;
            }
            if(at->clear<0 && !intersects(footprint(u),yard))at->clear=tick;
            // An output that finished its PARK and was later given an order:
            // the door clearing moved a parked ship (printed as nudged; with a
            // free door it still happens when the next output leaves past it).
            if(at->clear>=0 && u.orders.empty())at->rested=true;
            else if(at->rested && !u.orders.empty()) {++r.nudged;at->rested=false;}
            if(at->door<0 && !intersects(footprint(u),door))at->door=tick;
            if(trace && at-outs.begin()==traceIndex && at->door<0 && (tick-at->done)%traceEvery==0) {
                std::printf("  t=%d x=%.2f z=%.2f h=%d req=%d speed=%.3f stamp=%d orders=%zu",tick-at->done,
                    u.x.toFloat(),u.z.toFloat(),int(u.heading.v),int(u.turnReqBam),u.speed.toFloat(),int(u.routeStamp),u.orders.size());
                for(const auto& o:u.orders)std::printf(" [%s goal=%.0f,%.0f seg=%d %.0f,%.0f exh=%d stage=%d]",o.park?"park":"leg",
                    o.x.toFloat(),o.z.toFloat(),int(o.hasSegment),o.segmentX.toFloat(),o.segmentZ.toFloat(),int(o.navigationExhausted),
                    int(o.mission.stage));
                std::printf("\n");
            }
        }
        if(int(outs.size())==count && std::all_of(outs.begin(),outs.end(),[](const Out& o){return o.clear>=0;}))break;
    }
    for(int settle=0;settle<1500;++settle)w.tick(1.f/30);
    r.produced=int(outs.size());
    // An output stuck: still in the yard, or still on its way out (it never
    // finished its PARK) by the end.
    for(const auto& o:outs) {
        if(o.clear>=0)r.clear.push_back(o.clear-o.done);
        if(o.door>=0)r.door.push_back(o.door-o.done);
        if(const auto* u=w.unit(o.id);u && u->alive() && (intersects(footprint(*u),yard) || !u->orders.empty()))++r.stuck;
    }
    // Every idle ship that stood in front of the door is out of the yard's way
    // by the end: clear of the yard and of the strip straight out of its door
    // (6 cells).
    const Rect strip{yard.x,yard.z,yard.w,yard.h+6};
    for(int k:idle)if(const auto* u=w.unit(k);u && u->alive())r.inDoor+=intersects(footprint(*u),strip);
    std::printf("exit mode=%s layout=%s ship=%s crowd=%s produced=%d/%d stuck=%d lastDone=%d refusals=%d inDoor=%d nudged=%d "
                "clear med=%d max=%d door med=%d max=%d ticks=%d hash=%016llx\n",
        mode==sim::PathfindingMode::Legion?"Legion":"Retail",layoutName(layout),shipName,crowdName(crowd),r.produced,count,r.stuck,
        r.lastDone,r.refusals,r.inDoor,r.nudged,r.median(r.clear),r.max(r.clear),r.median(r.door),r.max(r.door),tick,
        (unsigned long long)w.stateHash());
    std::printf("  door:");for(const auto& o:outs)std::printf(" %d",o.door<0?-1:o.door-o.done);std::printf("\n");
    return r;
}
}

int main(int argc,char** argv) {
    if(argc!=2 && argc!=3)return 2;
    if(argc==3)only=argv[2];
    trace=std::getenv("EXIT_TRACE")!=nullptr;
    if(trace)traceIndex=std::atol(std::getenv("EXIT_TRACE"));
    if(const char* e=std::getenv("EXIT_EVERY"))traceEvery=std::max(1,std::atoi(e));
    auto vfs=hpi::mountRetailRoot(argv[1]);
    sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,false);
    constexpr int kShips=6;
    for(auto mode:{sim::PathfindingMode::Retail,sim::PathfindingMode::Legion})
        for(auto layout:{Layout::Open,Layout::Shore})
            for(const char* ship:{"cresubm","creiron","crester"}) {
                const std::string label=std::string(mode==sim::PathfindingMode::Legion?"Legion ":"Retail ")+
                    layoutName(layout)+" "+ship+": ";
                if(!only.empty() && label.find(only)==std::string::npos)continue;
                const Result clear=run(registry,mode,layout,ship,kShips,Crowd::None);
                check(clear.produced==kShips && int(clear.clear.size())==kShips && clear.stuck==0 &&
                      clear.median(clear.clear)<=180,
                      label+"every ship leaves the yard, median "+std::to_string(clear.median(clear.clear))+" ticks");
                const Result door=run(registry,mode,layout,ship,kShips,Crowd::Door);
                check(door.produced==kShips && int(door.clear.size())==kShips && door.stuck==0,
                      label+"eight idle Iron Clads at the door hold no ship in");
                check(door.produced==kShips && door.median(door.clear)<=180 &&
                      door.lastDone<=clear.lastDone+clear.lastDone*15/100,
                      label+"with them the yard builds near its clear-door rate ("+std::to_string(door.lastDone)+
                      " vs "+std::to_string(clear.lastDone)+" ticks)");
                check(door.inDoor==0,label+"the idle Iron Clads made way");
                const Result pad=run(registry,mode,layout,ship,kShips,Crowd::Pad);
                check(pad.produced==kShips && int(pad.clear.size())==kShips && pad.inDoor==0,
                      label+"an idle Iron Clad on the pad moves off it and the yard builds");
            }
    std::printf("navyyard exit: %d cases, %d failures\n",cases,failures);
    return failures?1:0;
}
