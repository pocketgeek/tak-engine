// Retail's air traffic: flyers step out of a stack. After every occupancy
// rebuild a flyer's counter (unit +0x12a, retailAirTrafficStep) rises by the
// flyers its secondary occupancy list holds and falls by 2 when it holds
// none; each flyer mission, at its wake, pushes a step-out ahead of itself
// once the counter reaches 5 (VTOL_StepOut 4214b0: the least crowded octant
// round the flyer's heading, D +- D/4 px out). Through protocol 306 World
// kept no counter and no step-out: Drakes hovering on one target or
// dogfighting one flyer piled onto a spot and stayed there (the owner's
// 0.9.12 replay: 42% of the hover ticks and 91% of the dogfight ticks
// stacked).
//
//   air_traffic_test <data dir> <scenario dir>
//
// Runs the air-stack-*.scn fixtures in both pathfinding modes, a target's hit
// points topped up to 30000 every tick, and counts the ticks an airborne Drake near
// the fight (or the move's point) shares its footprint cells with another
// flyer: its overlap list, the counter's input, is not empty.
#include "legion_scn.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int failures=0;
void check(bool ok,const std::string& message) {
    std::printf("[%s] %s\n",ok?"PASS":"FAIL",message.c_str());
    if(!ok)++failures;
}

struct Case {
    const char* file;
    const char* target;       // the attacked group (topped up), or nullptr: the move's point
    float x,z;                // the move's point (px), without a target
    int maxStackedPermille;   // the most stacked near Drake-ticks, per mille, the case may show
};

void run(const std::string& dir,const char* data,const Case& c,tak::sim::PathfindingMode mode) {
    const char* modeName=mode==tak::sim::PathfindingMode::Legion ? "legion" : "retail";
    const auto s=tak::scn::load(dir+"/"+c.file);
    tak::scn::BuildOptions options;
    options.mode=mode;
    options.serial=true;
    options.data=data;
    auto b=tak::scn::build(s,options);
    if(!b->skipped.empty()) { check(false,std::string(c.file)+": "+b->skipped); return; }
    auto& w=*b->world;
    const auto& drakes=b->groups.at("drakes");
    const int target=c.target ? b->groups.at(c.target).front() : 0;
    tak::scn::OrderFeed feed(s,*b);
    int near=0,stacked=0;
    long damage=0;
    for(uint32_t t=0;t<s.ticks;++t) {
        feed.apply(t);
        float fx=c.x,fz=c.z;
        tak::sim::Fixed full;
        if(target) {
            auto* u=w.unit(target);
            if(!u || !u->alive()) { check(false,std::string(c.file)+" "+modeName+": the target died (topped up)"); return; }
            // Far above any type's maximum: three Fire Breaths in one tick
            // never kill it.
            full=tak::sim::Fixed::fromInt(30000);
            u->hp=full;
            fx=u->x.toFloat();fz=u->z.toFloat();
        }
        w.tick(1.f/30);
        if(target) if(const auto* u=w.unit(target)) damage+=(full-u->hp).floorInt();
        for(int id:drakes) {
            const auto* u=w.unit(id);
            if(!u || !u->alive() || u->flightGroundMode!=2) continue;
            if(std::hypot(u->x.toFloat()-fx,u->z.toFloat()-fz)>400.f) continue;
            ++near;
            stacked+=u->airOccupant.count!=0;
        }
    }
    const int permille=near ? stacked*1000/near : 0;
    check(near>200 && permille<=c.maxStackedPermille,std::string(c.file)+" "+modeName+": "+std::to_string(stacked)+
          " of "+std::to_string(near)+" airborne Drake-ticks near "+(target ? "the fight" : "the point")+" stacked ("+
          std::to_string(permille)+" per mille, at most "+std::to_string(c.maxStackedPermille)+")");
    if(target) check(damage>0,std::string(c.file)+" "+modeName+": the Drakes still hurt the target ("+
                     std::to_string(damage)+" hit points)");
}
} // namespace

int main(int argc,char** argv) {
    if(argc!=3) { std::fprintf(stderr,"usage: air_traffic_test <data dir> <scenario dir>\n"); return 2; }
    // Per mille stacked on protocol 306 (both modes): 992, 980, 282, 277;
    // with the step-out: 44, 58, 86, 141.
    const std::vector<Case> cases{
        {"air-stack-barracks.scn","keep",0,0,300},
        {"air-stack-barracks-5.scn","keep",0,0,300},
        {"air-stack-duel.scn","spy",0,0,180},
        {"air-stack-move.scn",nullptr,1600,1280,210},
    };
    try {
        for(const auto& c:cases)
            for(auto mode:{tak::sim::PathfindingMode::Retail,tak::sim::PathfindingMode::Legion})
                run(argv[2],argv[1],c,mode);
    } catch(const std::exception& e) {
        std::fprintf(stderr,"air_traffic_test: %s\n",e.what());
        return 2;
    }
    std::printf("air_traffic: %d failures\n",failures);
    return failures?1:0;
}
