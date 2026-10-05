#include "sim/retailplus.h"
#include "sim/sim.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <vector>

using namespace tak::sim;
using Traffic=retailplus::Traffic;
using Cell=flow::Cell;
namespace {
void check(bool pass,const char* message) {
    if(!pass){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}
}
Traffic::Context context(int id,Cell at,Cell target) {
    Traffic::Context c;c.id=id;c.player=0;c.controller=uint64_t(id);c.tick=1;c.issuedTick=1;
    c.position=at;c.target=target;c.steeringTarget=target;c.footX=c.footZ=2;
    c.plainMove=true;c.missionKind=1;
    c.free=[](Cell){return true;};c.terrainFree=c.free;
    c.arrivalReachable=[](Cell){return false;};c.contactReachable=c.arrivalReachable;
    return c;
}
flow::Traffic::Neighbor neighbor(const Traffic::Context& c,bool idle=false) {
    flow::Traffic::Neighbor n{c.id,c.player,c.position,c.footX,c.footZ,idle};
    n.steeringTarget=c.steeringTarget;
    n.identity=flow::Traffic::Identity{c.controller,c.target,c.missionKind,c.targetId};return n;
}
bool overlaps(Cell a,int ax,int az,Cell b,int bx,int bz) {
    const int x=a.x-ax/2,z=a.z-az/2,xx=b.x-bx/2,zz=b.z-bz/2;
    return x<xx+bx&&xx<x+ax&&z<zz+bz&&zz<z+az;
}
void scope() {
    UnitType type;type.canMove=true;type.footX=type.footZ=2;
    Order goal;goal.goal=goal.groundMission=true;goal.controller=1;
    check(Traffic::supports(type,goal),"ordinary ground Move is outside Retail+ scope");
    using TypeChange=std::function<void(UnitType&)>;
    const std::vector<TypeChange> typeChanges{
        [](auto& t){t.maxVel=Fixed();},[](auto& t){t.canFly=true;},
        [](auto& t){t.domain=UnitType::Domain::Water;},[](auto& t){t.domain=UnitType::Domain::Hover;},
        [](auto& t){t.isBuilder=true;},[](auto& t){t.builderLimited=true;},[](auto& t){t.buildDist=1;},
        [](auto& t){t.canReclaim=true;},[](auto& t){t.canResurrect=true;},[](auto& t){t.canCapture=true;},
        [](auto& t){t.canTransport=true;},[](auto& t){t.transportCap=1;},
        [](auto& t){t.transportSizeCap=1;},[](auto& t){t.maxTransportSize=1;},
        [](auto& t){t.footX=0;},[](auto& t){t.footX=5;},[](auto& t){t.footZ=5;},[](auto& t){t.footZ=65;}
    };
    for(const auto& change:typeChanges){auto t=type;change(t);check(!Traffic::supports(t,goal),"special type entered Retail+ traffic");}
    using GoalChange=std::function<void(Order&)>;
    const std::vector<GoalChange> goalChanges{
        [](auto& o){o.goal=false;},[](auto& o){o.groundMission=false;},[](auto& o){o.targetId=2;},
        [](auto& o){o.attackMove=true;},[](auto& o){o.patrol=true;},[](auto& o){o.guard=true;},
        [](auto& o){o.load=true;},[](auto& o){o.unload=true;},[&](auto& o){o.buildType=&type;},
        [](auto& o){o.reclaimFeat=1;},[](auto& o){o.reclaimArea.emplace();},[](auto& o){o.manaBuildArea.emplace();},
        [](auto& o){o.repairTarget=2;},[](auto& o){o.patrolRepair=true;},[](auto& o){o.autoTarget=true;},
        [](auto& o){o.landing=true;},[](auto& o){o.wait=1;},[](auto& o){o.waitAttack=true;},
        [](auto& o){o.productionExit.emplace();},[](auto& o){o.nativeProductionExit=true;},[](auto& o){o.transportPickup=true;},
        [](auto& o){o.transportProductionAhead=1;},[](auto& o){o.transportUnloadApproach=true;},
        [](auto& o){o.transportUnloadReleasePending=true;},[](auto& o){o.transportUnloadTransferDeferred=true;},
        [](auto& o){o.transportTicks=1;},[](auto& o){o.transportPassenger=2;},
        [](auto& o){o.flightMoveMission=true;},[](auto& o){o.park.emplace();},
        [](auto& o){o.buildRectangle.emplace();},[](auto& o){o.flightGoal.emplace();},
        [](auto& o){o.hoverAttackRefresh=1;}
    };
    for(const auto& change:goalChanges){auto o=goal;change(o);check(!Traffic::supports(type,o),"special goal entered Retail+ traffic");}
    // Exhausting a partial route is still the same ordinary Move mission.
    goal.navigationConsumed=goal.navigationExhausted=true;
    goal.missionTarget=std::pair{Fixed::fromInt(900),Fixed::fromInt(700)};
    check(Traffic::supports(type,goal),"partial route exhaustion discarded the issued Move");
    Traffic traffic;auto c=context(1,{100,100},{900,100});unsigned queries=0;
    c.free=[&](Cell){++queries;return true;};c.missionKind|=2;c.blocked=2;c.goalReached=true;
    traffic.registerMove(c);const auto result=traffic.update(c);
    check(!result.settled&&!result.detour&&!result.wait&&!result.repath&&!queries&&!traffic.stats().records,
          "special context performed Retail+ work or arrival");
    c.missionKind=1;c.footX=5;traffic.registerMove(c);const auto large=traffic.update(c);
    check(!large.settled&&!large.detour&&!large.wait&&!large.repath&&!queries&&!traffic.stats().records,
          "oversized modified body entered the bounded local coordinator");
}
uint64_t retainedRoute() {
    Traffic traffic;auto c=context(1,{200,200},{900,200});c.steeringTarget=Cell{210,200};
    const auto peer=context(2,{202,200},{900,200});
    c.blocked=2;c.obstructionFriendly=true;c.obstruction=neighbor(peer);
    c.terrainFree=[](Cell at){return at.x>=190&&at.x<=210&&at.z>=190&&at.z<=210;};
    c.free=[&](Cell at){return c.terrainFree(at)&&!overlaps(at,2,2,peer.position,2,2);};
    traffic.registerMove(c);traffic.update(c);++c.tick;
    auto result=traffic.update(c);
    check(result.detour&&!result.settled&&!result.followLeader,"blocked Retail leg did not get a local route");
    const auto searches=traffic.stats().searches;bool completed=false;
    for(int tick=0;tick<50;++tick) {
        if(result.repath){completed=true;break;}
        check(result.detour.has_value(),"committed local route lost its next leg");
        const Cell end=*result.detour;
        check(end.x<=210,"local route passed its installed Retail corner");
        check(flow::directRoute(c.position,end,12,c.free,[&](Cell at){return at==end;}),
              "local leg lacks complete endpoint/diagonal clearance");
        c.position=end;++c.tick;result=traffic.update(c);
        check(!result.settled&&!result.arrivalApproach&&!result.followLeader,"local progress retired the distant mission");
    }
    check(completed&&traffic.stats().searches==searches,
          "local route repeatedly replanned instead of completing its retained legs");
    check(c.position.x>200&&traffic.stats().reservations==0,"completed local detour failed to clear the blocker");
    const auto hash=traffic.checksum();traffic.cancel(c.id);
    check(!traffic.stats().records&&!traffic.stats().reservations,"cancel kept a local movement reservation");
    traffic.reset();Traffic empty;check(traffic.checksum()==empty.checksum(),"reset retained traffic state");
    return hash;
}
void diagonalAndEnclosed() {
    Traffic traffic;auto c=context(1,{100,100},{900,900});c.steeringTarget=Cell{101,101};
    c.blocked=2;c.obstructionFriendly=true;c.obstruction=neighbor(context(2,{101,100},{900,900}));
    c.free=[](Cell at){return at==Cell{100,100}||at==Cell{101,101};};
    traffic.registerMove(c);traffic.update(c);++c.tick;
    const auto result=traffic.update(c);
    check(result.wait&&!result.detour&&!result.settled&&!result.followLeader&&!traffic.stats().routes,
          "diagonal local steering cut blocked side footprints");
    c.obstruction->idle=true;c.obstruction->steeringTarget.reset();
    for(c.tick=200;c.tick<600;++c.tick) {
        const auto out=traffic.update(c);
        check(!out.detour&&!out.settled&&!out.followLeader,"enclosed idle pocket invented an exit or arrival");
    }
}
void opposingAndLifecycle() {
    Traffic traffic;auto a=context(1,{200,200},{900,200}),b=context(2,{204,200},{0,200});
    a.footX=a.footZ=b.footX=b.footZ=3;
    a.blocked=b.blocked=2;a.obstructionFriendly=b.obstructionFriendly=true;
    a.obstruction=neighbor(b);b.obstruction=neighbor(a);
    a.free=[&](Cell at){return !overlaps(at,3,3,b.position,3,3);};
    b.free=[&](Cell at){return !overlaps(at,3,3,a.position,3,3);};
    traffic.registerMove(a);traffic.registerMove(b);traffic.update(a);traffic.update(b);a.tick=b.tick=2;
    const auto first=traffic.update(a),second=traffic.update(b);
    check(first.detour&&second.detour&&first.detour->z>200&&second.detour->z<200,
          "opposing movers did not commit distinct clear shoulders");
    check(traffic.stats().reservations>0,"opposing detours have no retained reservations");
    a.plainMove=false;const auto unsupported=traffic.update(a);
    check(!unsupported.detour&&!unsupported.wait&&!unsupported.settled,
          "a changed special mission inherited a local route");
    traffic.cancel(b.id);check(!traffic.stats().reservations,"cancel did not release opposing claims");
    size_t calls=0;traffic.prune(1000,[&](int,int,uint64_t,Cell,bool){++calls;return false;});
    check(!traffic.stats().records&&calls<=2*Traffic::pruneBudget,"lifecycle maintenance exceeded its two bounded sweeps");
}
void strictArrivals() {
    Traffic traffic;auto anchor=context(1,{100,100},{100,100}),member=context(2,{103,100},{100,100});
    traffic.registerMove(anchor);traffic.registerMove(member);
    anchor.goalReached=true;anchor.free=[](Cell){return false;};
    const auto illegal=traffic.update(anchor);
    check(!illegal.settled&&illegal.arrivalApproach,"native acceptance ignored an illegal current footprint or lost its retirement veto");
    anchor.free=[](Cell){return true;};++anchor.tick;
    check(traffic.update(anchor).settled,"legal native arrival did not settle");
    std::array<flow::Traffic::Neighbor,1> neighbors{neighbor(anchor,true)};
    member.neighbors=neighbors;member.blocked=2;member.obstructionFriendly=true;member.obstruction=neighbors[0];
    member.tick=2;
    check(!traffic.update(member).settled,"blocked contact crossed an unproved goal connection");
    neighbors[0].position.x++;member.contactReachable=[](Cell){return true;};++member.tick;
    check(!traffic.update(member).settled,"stale settled-peer position authorized arrival");
    neighbors[0].position=anchor.position;++member.tick;
    check(traffic.update(member).settled,"proved same-goal legal contact could not join the arrival area");
    traffic.cancel(anchor.id);
    check(traffic.settled(anchor.id,anchor.position),"successful controller release lost its verified arrival anchor");
    traffic.prune(64,[&](int id,int,uint64_t,Cell,bool settled){return id==member.id&&settled;});
    check(!traffic.settled(anchor.id,anchor.position)&&traffic.settled(member.id,member.position),
          "prune did not distinguish invalid and verified idle arrival anchors");

    Traffic partial;auto c=context(3,{200,200},{900,900});c.steeringTarget=c.position;
    const auto out=partial.update(c);
    check(!out.settled&&!out.arrivalApproach,"current Retail waypoint became the mission destination");
}
void boundedFairWork() {
    Traffic traffic;constexpr size_t count=80;
    std::vector<Traffic::Context> contexts;std::array<unsigned,count> calls{};
    for(size_t i=0;i<count;++i) {
        auto c=context(int(i+1),{100+int(i)*40,100},{10000,10000});c.footX=c.footZ=4;
        c.blocked=2;c.obstructionFriendly=true;
        auto peer=c;peer.id+=1000;peer.position.x+=4;c.obstruction=neighbor(peer);
        c.free=[&,i](Cell){++calls[i];return false;};traffic.registerMove(c);contexts.push_back(c);
    }
    for(uint32_t tick=1;tick<=2*count+1;++tick) {
        const auto before=traffic.stats().probes;
        for(auto& c:contexts){c.tick=tick;const auto out=traffic.update(c);check(!out.followLeader,"Retail+ leaked a movement-batch request");}
        check(traffic.stats().probes-before<=8192,"local traffic exceeded its shared footprint budget");
    }
    for(unsigned visits:calls)check(visits>=16,"expensive later caller starved across admission windows");
    check(traffic.bytes()<=Traffic::memoryLimit,"bounded traffic exceeded its canonical reservation");
}
void deferredLocalQueries() {
    // Exhausted body work is unknown, rather than evidence that an existing
    // destination slot or committed local leg has become occupied.
    Traffic arrival;auto c=context(1,{116,102},{100,100});
    c.terrainFree=[](Cell at){return !(at.z==101&&at.x>=114&&at.x<=116);};
    c.free=c.terrainFree;c.arrivalReachable=[&](Cell at){return at==c.target;};
    for(int id=1;id<=16;++id){auto member=c;member.id=id;member.controller=id;arrival.registerMove(member);}
    arrival.update(c);++c.tick;const auto slot=arrival.update(c);
    check(slot.arrivalApproach&&slot.detour==c.target,"deferred-body fixture did not reserve its arrival slot");
    bool unavailable=false;c.localDeferred=&unavailable;const auto free=c.free;
    c.free=[&](Cell){unavailable=true;return false;};c.blocked=2;++c.tick;
    const auto waiting=arrival.update(c);
    check(unavailable&&waiting.wait&&waiting.arrivalApproach&&waiting.detour==slot.detour&&!waiting.settled,
          "deferred body query discarded a reserved arrival slot");
    c.free=free;unavailable=false;c.blocked=0;++c.tick;
    const auto resumed=arrival.update(c);
    check(resumed.arrivalApproach&&resumed.detour==slot.detour&&!resumed.repath,
          "available body work did not resume the original arrival slot");

    Traffic actual,control;auto route=context(1,{200,200},{900,200});route.steeringTarget=Cell{210,200};
    const auto peer=context(2,{202,200},{900,200});
    route.blocked=2;route.obstructionFriendly=true;route.obstruction=neighbor(peer);
    route.terrainFree=[](Cell at){return at.x>=190&&at.x<=210&&at.z>=190&&at.z<=210;};
    route.free=[&](Cell at){return route.terrainFree(at)&&!overlaps(at,2,2,peer.position,2,2);};
    actual.registerMove(route);control.registerMove(route);actual.update(route);control.update(route);++route.tick;
    const auto first=actual.update(route),reference=control.update(route);
    check(first.detour&&first.detour==reference.detour,"deferred-leg fixture has no matching local routes");
    const auto searches=actual.stats().searches;route.position=*first.detour;++route.tick;
    const auto onward=control.update(route);check(onward.detour.has_value(),"deferred-leg fixture ended before a second leg");
    auto delayed=route;delayed.localDeferred=&unavailable;
    delayed.free=[&](Cell){unavailable=true;return false;};unavailable=false;
    const auto paused=actual.update(delayed);
    check(unavailable&&paused.wait&&!paused.repath&&!paused.settled,"unknown local leg was treated as a failed path");
    ++delayed.tick;unavailable=false;check(actual.update(delayed).wait,"retained local leg did not await body-query work");
    route.tick=delayed.tick+1;route.blocked=0;const auto continued=actual.update(route);
    check(continued.detour==onward.detour&&actual.stats().searches==searches,
          "deferred local leg lost its committed route or required a new search");

    // A strict contact proof has already succeeded. Unknown transverse
    // footprints must not masquerade as opposing corridor walls and extend
    // contact settlement beyond the group's fixed open-ground shell.
    Traffic contact;auto member=context(2,{123,100},{100,100});
    for(int id=1;id<=16;++id){auto q=member;q.id=id;q.controller=id;contact.registerMove(q);}
    auto anchor=member;anchor.id=1;anchor.controller=1;anchor.position={120,100};anchor.goalReached=true;
    check(contact.update(anchor).settled,"deferred corridor fixture has no settled contact anchor");
    const std::array<flow::Traffic::Neighbor,1> neighbors{neighbor(anchor,true)};
    member.neighbors=neighbors;member.blocked=2;member.contactReachable=[](Cell){return true;};
    member.localDeferred=&unavailable;member.terrainFree=[&](Cell){unavailable=true;return false;};
    member.tick=2;unavailable=false;const auto unproved=contact.update(member);
    check(unavailable&&!unproved.settled,"deferred transverse checks invented a constrained arrival corridor");
    member.terrainFree=[](Cell){return true;};unavailable=false;++member.tick;
    check(!contact.update(member).settled,"open terrain allowed contact outside the bounded arrival shell");
    member.terrainFree=[](Cell at){return at.z==100;};++member.tick;
    check(contact.update(member).settled,"proved opposing corridor walls could not admit the same bounded contact queue");
}
void cancelledArrivalSlot() {
    Traffic traffic;auto c=context(1,{116,102},{100,100});
    c.arrivalReachable=[&](Cell at){return at==c.target;};
    for(int id=1;id<=16;++id){auto q=c;q.id=id;q.controller=id;traffic.registerMove(q);}
    traffic.update(c);++c.tick;
    check(traffic.update(c).detour==c.target,"cancel fixture did not reserve the sole proved arrival slot");
    traffic.cancel(c.id);
    c.id=17;c.controller=17;++c.tick;traffic.registerMove(c);traffic.update(c);++c.tick;
    check(traffic.update(c).detour==c.target,"cancel left an uncompleted arrival slot reserved until pruning");
    auto special=c;special.plainMove=false;++special.tick;traffic.update(special);
    c.id=18;c.controller=18;c.tick=special.tick+1;traffic.registerMove(c);traffic.update(c);++c.tick;
    check(traffic.update(c).detour==c.target,"special-order replacement retained the previous plain Move arrival slot");
}
void proofCosts() {
    check(Traffic::contactProofCost({0,0},{174,174},4,4)==8368,
          "diagonal contact proof omitted its full-footprint side anchors");
    check(Traffic::arrivalProofCost({0,0},{0,0},{174,174},4,4)==8400,
          "standing arrival proof omitted its two live/static approach probes");
    check(Traffic::arrivalProofCost({0,0},{0,0},{174,174},4,4)>8192,
          "unaffordable supported 4x4 arrival proof could be retried forever");
    check(Traffic::arrivalProofCost({10,10},{13,12},{18,14},2,3)==156,
          "asymmetric two-leg proof cost does not include both approach passes");
    const Cell low{std::numeric_limits<int>::min(),std::numeric_limits<int>::min()};
    const Cell high{std::numeric_limits<int>::max(),std::numeric_limits<int>::max()};
    check(Traffic::contactProofCost(low,high,4,4)==206158430176ull,
          "large coordinate differences overflowed a proof estimate");
    check(Traffic::arrivalProofCost(low,high,low,std::numeric_limits<int>::max(),std::numeric_limits<int>::max())==
          std::numeric_limits<uint64_t>::max(),"unrepresentable weighted proof cost did not saturate");
    check(Traffic::contactProofCost({0,0},{0,0},0,2)==std::numeric_limits<uint64_t>::max(),
          "invalid footprint produced an affordable proof");
}
void unblockedEquivalence() {
    Traffic reference,fast;
    std::array<Traffic::Context,12> members;
    for(size_t i=0;i<members.size();++i) {
        members[i]=context(int(i+1),{100,100+int(i)*4},{1000,100});
        reference.registerMove(members[i]);fast.registerMove(members[i]);
    }
    unsigned shortcuts=0,declines=0;
    for(uint32_t tick=1;tick<=1000;++tick)for(auto& c:members) {
        c.tick=tick;
        if(tick<350||tick>700)++c.position.x;
        if(tick==120)++c.controller; // Internal retry preserves mission progress.
        if(tick==240){++c.issuedTick;c.target.z+=100;} // Replacement changes group.
        if(tick==410){reference.cancel(c.id);fast.cancel(c.id);}
        if(tick==720){c.blocked=2;c.obstructionFriendly=true;c.obstruction=neighbor(members.back());}
        if(tick==760){c.blocked=0;c.obstruction.reset();}
        if(tick==950){c.position=c.target;c.goalReached=true;}
        auto cheap=c;cheap.free={};cheap.terrainFree={};cheap.arrivalReachable={};cheap.contactReachable={};
        const auto expected=reference.update(c);
        Traffic::Result actual;
        const auto before=fast.checksum();
        if(fast.updateUnblocked(cheap))++shortcuts;
        else {
            check(fast.checksum()==before,"declined shortcut changed state before a deferred adapter query");
            ++declines;actual=fast.update(c);
        }
        check(expected.settled==actual.settled&&expected.detour==actual.detour&&expected.wait==actual.wait&&
              expected.repath==actual.repath&&expected.arrivalApproach==actual.arrivalApproach&&
              expected.followLeader==actual.followLeader&&reference.checksum()==fast.checksum(),
              "unblocked shortcut changed traffic results or per-update state");
    }
    check(shortcuts>1000&&declines>100,"equivalence fixture missed fast or retained/arrival paths");
}

}
int main() {
    scope();const auto first=retainedRoute(),second=retainedRoute();
    check(first==second,"identical traffic contexts produced different retained state");
    diagonalAndEnclosed();opposingAndLifecycle();strictArrivals();boundedFairWork();deferredLocalQueries();cancelledArrivalSlot();
    proofCosts();unblockedEquivalence();
    std::printf("PASS Retail+ scope, retained legal routes, strict arrivals, lifecycle and fair bounded work hash=%016llx\n",
        (unsigned long long)first);
}
