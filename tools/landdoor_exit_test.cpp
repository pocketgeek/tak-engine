// Whether idle units at a land factory's door hold its outputs in (user report
// after 0.9.19: "idle units at a land factory's door don't move aside; only the
// Navy Yard got that fix").
//
// One factory per faction (Aramon Barracks, Veruna Keep, Taros Dungeon, Creon
// Academy), each making six of a small and of a large output, in both
// pathfinding modes, on a flat loaded map (feature plane, so movement and the
// site test take the native rectangular footprint test). Measured per output:
// ticks from completion until it is clear of the factory's rectangle and until
// it is clear of the door (the rectangle grown kDoorCells). Then the same with
// a dozen idle units of the owner parked 8 px apart just off the door, with
// one idle unit on the pad, and with the dozen guarding one of them.
//
// Retail moves no idle body (BUGGER_OFF is stored, never acted on: 0x50d50e;
// BUILD stage 2's footprint test 0x401deb holds the next site; the mover only
// clamps and slows, 0x4dbe2c), so the crowd and the unit on the pad hold the
// factory there as here before the change. Now the owner's idle units in front
// of the factory move aside (World::clearYardDoor), guards and other players'
// units stay, and the factory's own parked outputs are moved again only off the
// pad or when one holds up the next output (`nudged` counts an output of this
// factory given an order after it came to rest: with the Navy Yard's first rule,
// every own parked unit in the door's area, 127 over these 64 runs; now 26).
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace tak;
namespace {
int failures=0,cases=0;
std::string only;
void check(bool ok,const std::string& label) {
    std::printf("[%s] %s\n",ok?"PASS":"FAIL",label.c_str());++cases;if(!ok)++failures;
}
struct Rect {int x,z,w,h;};
Rect footprint(const sim::Unit& u) {
    return {sim::footprintOrigin(u.x,u.type->footX),sim::footprintOrigin(u.z,u.type->footZ),u.type->footX,u.type->footZ};
}
bool intersects(const Rect& a,const Rect& b) {return a.x<b.x+b.w && b.x<a.x+a.w && a.z<b.z+b.h && b.z<a.z+a.h;}
constexpr int kCells=256,kDoorCells=4,kOutputs=6;

enum class Crowd {None,Door,Pad,Guard};
const char* crowdName(Crowd c) {return c==Crowd::None?"none":c==Crowd::Door?"door":c==Crowd::Pad?"pad":"guard";}

struct Result {
    int produced=0,stuck=0,lastDone=-1,inDoor=0,nudged=0,crowdMoved=0;
    std::vector<int> clear,door;
    static int median(std::vector<int> v) {
        if(v.empty())return -1;
        std::sort(v.begin(),v.end());return v[v.size()/2];
    }
    static int max(const std::vector<int>& v) {return v.empty()?-1:*std::max_element(v.begin(),v.end());}
};

Result run(const sim::TypeRegistry& registry,sim::PathfindingMode mode,const char* factoryName,const char* outputName,
           const char* crowdTypeName,Crowd crowd) {
    sim::World w;w.setSerialThreads(true);w.setPathfindingMode(mode);
    w.setTerrain(std::vector<uint8_t>(kCells*kCells,100),kCells,kCells,20);
    w.setMapPlacementFeatures(std::vector<uint16_t>(kCells*kCells,0xffff),{});
    w.buildNavClasses(registry);w.setPathService(true);
    auto factory=*registry.find(factoryName);factory.workerTime*=4;factory.storage=1000000;
    const auto* output=registry.find(outputName);
    const auto* crowdType=registry.find(crowdTypeName);
    // On its cell grid as a placed building stands, at the middle of its
    // buildangle range: every shipped ground factory's pad then lies south.
    const float fx=float(112*16+factory.footX%2*8),fz=float(112*16+factory.footZ%2*8);
    const int id=w.spawn(&factory,fx,fz,float(int16_t(factory.orientation))*6.28318530718f/65536.f,0);
    const Rect yard=footprint(*w.unit(id));
    const Rect door{yard.x-kDoorCells,yard.z-kDoorCells,yard.w+2*kDoorCells,yard.h+2*kDoorCells};
    const float doorZ=float((yard.z+yard.h)*16);
    // Two rows of six, 8 px between bodies, the first row 24 px off the door.
    const float gap=float(crowdType->footX*16+8);
    std::vector<int> idle;
    if(crowd==Crowd::Door || crowd==Crowd::Guard)
        for(int i=0;i<12;++i)
            idle.push_back(w.spawn(crowdType,fx-2.5f*gap+float(i%6)*gap,doorZ+24+float(i/6)*gap,0,0));
    if(crowd==Crowd::Pad)idle.push_back(w.spawn(crowdType,fx,doorZ-24,0,0));
    if(crowd==Crowd::Guard)for(size_t i=1;i<idle.size();++i)w.guard(idle[i],idle[0],false);
    for(int t=0;t<30;++t)w.tick(1.f/30);
    std::vector<std::pair<float,float>> crowdAt;
    for(int k:idle)crowdAt.emplace_back(w.unit(k)->x.toFloat(),w.unit(k)->z.toFloat());
    w.player(0).mana=1000000;w.train(id,output,kOutputs);
    struct Out {int id,done,clear=-1,door=-1;bool rested=false;};
    std::vector<Out> outs;Result r;int tick=0;
    bool exitOrder=false;
    for(;tick<20000;++tick) {
        w.player(0).mana=1000000;w.tick(1.f/30);
        // The guards (all but the first, their ward) never get the door's move.
        for(size_t i=crowd==Crowd::Guard ? 1 : 0;i<idle.size();++i)if(const auto* u=w.unit(idle[i]);u && u->alive())
            for(const auto& o:u->orders)exitOrder|=bool(o.productionExit);
        for(const auto& u:w.units()) {
            if(!u.alive() || u.type!=output || u.underConstruction || u.id==id)continue;
            if(std::find(idle.begin(),idle.end(),u.id)!=idle.end())continue;
            auto at=std::find_if(outs.begin(),outs.end(),[&](const Out& o){return o.id==u.id;});
            if(at==outs.end()) {outs.push_back({u.id,tick});at=outs.end()-1;r.lastDone=tick;}
            if(at->clear<0 && !intersects(footprint(u),yard))at->clear=tick;
            if(at->clear>=0 && u.orders.empty())at->rested=true;
            else if(at->rested && !u.orders.empty()) {++r.nudged;at->rested=false;}
            if(at->door<0 && !intersects(footprint(u),door))at->door=tick;
        }
        if(int(outs.size())==kOutputs && std::all_of(outs.begin(),outs.end(),[](const Out& o){return o.clear>=0;}))break;
    }
    for(int settle=0;settle<1500;++settle)w.tick(1.f/30);
    r.produced=int(outs.size());
    for(const auto& o:outs) {
        if(o.clear>=0)r.clear.push_back(o.clear-o.done);
        if(o.door>=0)r.door.push_back(o.door-o.done);
        if(const auto* u=w.unit(o.id);u && u->alive() && (intersects(footprint(*u),yard) || !u->orders.empty()))++r.stuck;
    }
    // The strip straight out of the door (6 cells deep) and the yard itself.
    const Rect strip{yard.x,yard.z,yard.w,yard.h+6};
    for(int k:idle)if(const auto* u=w.unit(k);u && u->alive())r.inDoor+=intersects(footprint(*u),strip);
    r.crowdMoved=exitOrder;
    std::printf("exit mode=%s factory=%s output=%s crowd=%s produced=%d/%d stuck=%d lastDone=%d inDoor=%d nudged=%d "
                "exitOrder=%d clear med=%d max=%d door med=%d max=%d hash=%016llx\n",
        mode==sim::PathfindingMode::Legion?"Legion":"Retail",factoryName,outputName,crowdName(crowd),r.produced,kOutputs,
        r.stuck,r.lastDone,r.inDoor,r.nudged,int(exitOrder),Result::median(r.clear),Result::max(r.clear),
        Result::median(r.door),Result::max(r.door),(unsigned long long)w.stateHash());
    return r;
}
}

int main(int argc,char** argv) {
    if(argc!=2 && argc!=3)return 2;
    if(argc==3)only=argv[2];
    auto vfs=hpi::mountRetailRoot(argv[1]);
    sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,false);
    struct Case {const char* factory;const char* small;const char* large;const char* crowd;};
    const Case factories[]{
        {"arakeep","arasword","arapult","arasword"},
        {"verkeep","versword","verpult","versword"},
        {"tardung","tararch","tarfire","tararch"},
        {"creacad","creshoc","crebeas","creshoc"},
    };
    for(auto mode:{sim::PathfindingMode::Retail,sim::PathfindingMode::Legion})
        for(const auto& c:factories)
            for(const char* output:{c.small,c.large}) {
                const std::string label=std::string(mode==sim::PathfindingMode::Legion?"Legion ":"Retail ")+
                    c.factory+" "+output+": ";
                if(!only.empty() && label.find(only)==std::string::npos)continue;
                if(!registry.find(c.factory) || !registry.find(output) || !registry.find(c.crowd)) {
                    check(false,label+"types present");continue;
                }
                const Result clear=run(registry,mode,c.factory,output,c.crowd,Crowd::None);
                check(clear.produced==kOutputs && int(clear.clear.size())==kOutputs && clear.stuck==0,
                      label+"every output leaves, median "+std::to_string(Result::median(clear.clear))+" ticks");
                check(clear.nudged<=2,label+"at most two parked outputs are moved again ("+std::to_string(clear.nudged)+")");
                const Result door=run(registry,mode,c.factory,output,c.crowd,Crowd::Door);
                check(door.produced==kOutputs && int(door.clear.size())==kOutputs && door.stuck==0,
                      label+"a dozen idle units at the door hold no output in");
                check(door.produced==kOutputs && door.lastDone<=clear.lastDone+clear.lastDone*15/100,
                      label+"with them the factory builds near its clear-door rate ("+std::to_string(door.lastDone)+
                      " vs "+std::to_string(clear.lastDone)+" ticks)");
                check(door.inDoor==0,label+"the idle units made way");
                check(door.nudged<=2,label+"at most two of its parked outputs are moved again ("+std::to_string(door.nudged)+")");
                const Result pad=run(registry,mode,c.factory,output,c.crowd,Crowd::Pad);
                check(pad.produced==kOutputs && int(pad.clear.size())==kOutputs && pad.inDoor==0,
                      label+"an idle unit on the pad moves off it and the factory builds");
                const Result guard=run(registry,mode,c.factory,output,c.crowd,Crowd::Guard);
                check(!guard.crowdMoved,label+"guards are not moved aside");
            }
    std::printf("landdoor exit: %d cases, %d failures\n",cases,failures);
    return failures?1:0;
}
