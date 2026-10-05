#include "sim/cooperativecorridor.h"
#include "sim/flowservice.h"
#include "sim/navigationmemory.h"
#include <cstdio>
#include <limits>
#include <set>
#include <stdexcept>

using namespace tak::sim;
using flow::Cell;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
std::shared_ptr<const flow::Topology> terrain(bool wall) {
    flow::TopologyBuilder builder(128,64);
    for(int z=0;z<64;++z)for(int x=0;x<128;++x)
        builder.setCost({x,z},wall&&x==63&&(z<44||z>51)?0:1);
    while(!builder.done()&&!builder.failed())builder.step(8192);
    check(builder.done(),"topology construction failed");return builder.finish();
}
void lanes() {
    auto topology=terrain(false);
    auto goal=std::make_shared<flow::Destination>(topology,std::vector<Cell>{{60,45}});
    while(!goal->done())goal->step(8192);
    flow::FieldBuilder builder(goal,0);while(!builder.done())builder.step(8192);
    const auto& field=builder.field();const uint64_t before=field.hash();
    // An eastbound front can retain its individual rows while equally cheap
    // diagonal alternatives exist. It must eventually reach the exact goal.
    for(int z=12;z<=28;z+=4) {
        Cell at{4,z};std::set<std::pair<int,int>> visited;
        for(int steps=0;at!=Cell{60,45};++steps) {
            check(steps<100,"lane did not finish");
            check(visited.emplace(at.x,at.z).second,"lane preference introduced a cycle");
            Cell next;check(field.next(at,next),"lane lost reachable terrain");
            next=cooperative::corridorStep(*topology,field,at,next,{1,0});
            if(at.x<12)check(next.z==z,"equally cheap eastbound rows collapsed");
            const auto distance=[&](Cell c){return field.distance[size_t(c.z)*64+c.x];};
            check(distance(next)<distance(at),"lane failed strict potential descent");
            at=next;
        }
    }
    check(before==field.hash(),"corridor sampling mutated shared field");
}
void alignmentPrecision() {
    auto topology=terrain(false);
    auto goal=std::make_shared<flow::Destination>(topology,std::vector<Cell>{{60,32}});
    while(!goal->done())goal->step(8192);
    flow::FieldBuilder builder(goal,0);while(!builder.done())builder.step(8192);
    const auto& field=builder.field();const Cell at{20,32};Cell fallback;
    check(field.next(at,fallback),"alignment fixture has no next step");
    // With the ordinary eight-cell lookahead, a four-cell lateral error
    // gives diagonal alignment12/sqrt(2), strictly better than straight8.
    // Rounding both scores to integers made them tie and kept the squeezed
    // lane straight. Both alternatives retain strict field descent.
    check(cooperative::corridorStep(*topology,field,at,fallback,{1,0},Cell{60,36})==Cell{21,33},
          "integer rounding discarded a better outward lane step");
    check(cooperative::corridorStep(*topology,field,at,fallback,{1,0},Cell{60,28})==Cell{21,31},
          "integer rounding discarded the mirrored outward lane step");
    const auto current=field.distance[size_t(at.z)*64+at.x];
    // Direction without a lane target is used directly. Products remain in
    // int64 even at the full signed-int extremes: dot has two unit-step terms
    // and the comparison multiplies only by1024 or1448.
    const int low=std::numeric_limits<int>::min(),high=std::numeric_limits<int>::max();
    for(Cell direction:std::array<Cell,8>{{{high,high},{low,low},{high,low},{low,high},
                                          {high,0},{low,0},{0,high},{0,low}}}) {
        const Cell next=cooperative::corridorStep(*topology,field,at,fallback,direction);
        check(topology->cost(next)>0&&field.distance[size_t(next.z)*64+next.x]<current,
              "large alignment direction lost strict terrain progress");
        check(std::abs(next.x-at.x)<=1&&std::abs(next.z-at.z)<=1,
              "large alignment direction produced a nonlocal step");
    }
}
void preserveNonRestorationAlignment() {
    auto topology=terrain(false);
    auto goal=std::make_shared<flow::Destination>(topology,std::vector<Cell>{{60,52}});
    while(!goal->done())goal->step(8192);
    flow::FieldBuilder builder(goal,0);while(!builder.done())builder.step(8192);
    const auto& field=builder.field();const Cell at{20,32},fallback{21,32};
    check(field.distance[size_t(fallback.z)*64+fallback.x]+1024==field.distance[size_t(at.z)*64+at.x],
          "alignment control fallback is not a shortest terrain step");
    check(cooperative::corridorStep(*topology,field,at,fallback,{2,1})==fallback,
          "width restoration changed a route without an assigned lane");
    check(cooperative::corridorStep(*topology,field,at,fallback,{2,1},Cell{60,52})==fallback,
          "width restoration changed an already aligned lane");
    check(cooperative::corridorStep(*topology,field,at,fallback,{3,1},Cell{60,48})==fallback,
          "cardinal width restoration changed diagonal terrain guidance");
}
uint64_t aroundWall(unsigned workers,bool lane=false) {
    auto topology=terrain(true);flow::Service service;
    check(service.bind(1,1,topology,{{110,10}}),"corridor binding failed");
    Cell at{10,10};std::set<std::pair<int,int>> visited;bool crossed=false;
    for(int tick=0;tick<10000;++tick) {
        service.tick(workers);
        const auto result=service.sampleCooperative(1,at,{1,0},lane?std::optional<Cell>{{110,18}}:std::nullopt);
        if(result.status==flow::Service::Status::Pending)continue;
        if(result.status==flow::Service::Status::Arrived) {
            check(at==Cell{110,10}&&crossed,"corridor finished outside commanded component");
            return service.checksum();
        }
        check(result.status==flow::Service::Status::Ready,"corridor declared reachable maze blocked");
        check(visited.emplace(at.x,at.z).second,"cross-tile corridor cycled");
        const auto next=result.next;
        check(topology->cost(next)>0,"corridor crossed wall");
        if(at.x!=next.x&&at.z!=next.z)
            check(topology->cost({at.x,next.z})&&topology->cost({next.x,at.z}),"diagonal cut blocked corner");
        if(at.x<=63&&next.x>63) {check(at.z>=44&&at.z<=51,"wrong portal");crossed=true;}
        at=next;
    }
    throw std::runtime_error("corridor timed out");
}
void memory() {
    for(int size:{1,64,128,512,1024,2048}) {
        const auto old=flow::MemoryPlan::forMap(size,size);
        const auto unchanged=navigationMemoryPlan(PathfindingMode::Flowfield,size,size);
        check(old.reservedBytes==unchanged.reservedBytes&&old.profiles==unchanged.profiles,
              "Cooperative reservation changed Flowfield admission");
        const auto plan=navigationMemoryPlan(PathfindingMode::Cooperative,size,size);
        check(plan.supported&&plan.profiles<=old.profiles,"Cooperative lost supported terrain");
        check(plan.reservedBytes<=flow::MemoryPlan::defaultLimit,"Cooperative exceeds total memory ceiling");
        check(plan.sharedBytes==old.sharedBytes+cooperative::Traffic::memoryLimit+cooperative::Passages::memoryLimit+
            cooperative::MovementBatch::memoryLimit,"coordinator omitted from memory reservation");
    }
    check(!navigationMemoryPlan(PathfindingMode::Cooperative,64,64,cooperative::Traffic::memoryLimit-1).supported,
          "undersized coordinator budget accepted");
}
}
int main() {try {
    lanes();alignmentPrecision();preserveNonRestorationAlignment();const auto serial=aroundWall(0),threaded=aroundWall(4);
    check(serial==threaded,"corridor state depends on worker scheduling");
    check(aroundWall(0,true)==aroundWall(4,true),"lane restoration changes threaded maze routes");memory();
    std::printf("PASS Cooperative corridor lanes, maze portals, determinism and memory\n");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
