#include "sim/cooperative.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

using tak::sim::cooperative::Traffic;
using Cell=tak::sim::flow::Cell;
namespace {
void check(bool good,const char* message) {if(!good){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
Traffic::Context context(int id,Cell at,Cell target) {
    Traffic::Context c;c.id=id;c.player=1;c.controller=uint64_t(id);c.tick=1;c.issuedTick=1;
    c.position=at;c.target=target;c.steeringTarget=target;c.footX=c.footZ=2;c.plainMove=true;
    c.free=[](Cell){return true;};c.terrainFree=c.free;
    c.arrivalReachable=[](Cell){return false;};c.contactReachable=c.arrivalReachable;
    return c;
}
tak::sim::flow::Traffic::Neighbor neighbor(const Traffic::Context& c) {
    tak::sim::flow::Traffic::Neighbor n;
    n.id=c.id;n.player=c.player;n.position=c.position;n.footX=c.footX;n.footZ=c.footZ;
    n.steeringTarget=c.steeringTarget;
    n.identity=tak::sim::flow::Traffic::Identity{c.controller,c.target,c.missionKind,c.targetId};return n;
}
bool overlap(Cell a,int ax,int az,Cell b,int bx,int bz) {
    const int x=a.x-ax/2,z=a.z-az/2,xx=b.x-bx/2,zz=b.z-bz/2;
    return x<xx+bx&&xx<x+ax&&z<zz+bz&&zz<z+az;
}
void groupCentroid() {
    Traffic t;auto a=context(1,{100,90},{300,200}),b=context(2,{100,110},{300,200});
    t.registerMove(a);check(!t.corridorAim(a),"solo movement has no formation lane objective");t.registerMove(b);
    check(t.corridorDirection(a)==Cell{200,100},"shared starting centroid");
    check(t.corridorAim(a)!=t.corridorAim(b),"group members retain distinct lateral arrival lanes");
    a.position={120,90};t.registerMove(a);
    check(t.corridorDirection(a)==Cell{200,100},"travel does not rotate retained group corridor");
    ++b.issuedTick;b.target={500,200};t.registerMove(b);
    check(t.corridorDirection(a)==Cell{200,110},"new command leaves prior group");
    t.cancel(a.id);check(!t.corridorDirection(a),"cancellation removes corridor membership");
}
void opposedShoulder() {
    Traffic t;auto a=context(1,{200,200},{1000,200}),b=context(2,{202,200},{0,200});
    a.blocked=b.blocked=2;a.obstructionFriendly=b.obstructionFriendly=true;
    a.obstruction=neighbor(b);b.obstruction=neighbor(a);
    a.free=[&](Cell at){return !overlap(at,2,2,b.position,2,2);};
    b.free=[&](Cell at){return !overlap(at,2,2,a.position,2,2);};
    t.registerMove(a);t.registerMove(b);t.update(a);t.update(b);
    a.tick=b.tick=2;
    const auto first=t.update(a),second=t.update(b);
    check(first.detour&&first.detour->z>a.position.z,"eastward stream takes its right shoulder too");
    check(second.detour.has_value(),"other stream commits a passing shoulder");
    check(second.detour->z<b.position.z,"westward stream takes its right shoulder");
    check(t.stats().reservations>0,"passing route reserves its swept footprint");
    auto crossing=context(3,{200,203},{200,0});crossing.tick=2;t.registerMove(crossing);
    check(t.update(crossing).wait,"unobstructed third mover respects a reserved crossing");
    auto enemy=crossing;enemy.id=4;enemy.controller=4;enemy.player=2;t.registerMove(enemy);
    check(!t.update(enemy).wait,"enemy movement does not honor friendly reservation promises");
    t.cancel(a.id);t.cancel(b.id);check(t.stats().reservations==0,"Stop releases passing reservation immediately");
}
void diagonalProof() {
    Traffic t;auto c=context(1,{100,100},{900,900});c.blocked=2;c.obstructionFriendly=true;
    auto peer=context(2,{101,100},{900,900});c.obstruction=neighbor(peer);
    c.free=[](Cell at){return at==Cell{100,100}||at==Cell{101,101};};
    t.registerMove(c);t.update(c);c.tick=2;const auto result=t.update(c);
    check(result.wait&&!result.detour,"diagonal endpoint cannot cross blocked side anchors");
    check(t.stats().routes==0,"unproved diagonal creates no reservation route");
}
void completedShoulderKeepsPassingInterval() {
    Traffic t;auto c=context(1,{200,200},{1000,200});
    const auto peer=context(2,{202,200},{0,200});
    c.blocked=2;c.obstructionFriendly=true;c.obstruction=neighbor(peer);
    c.free=[&](Cell at){return !overlap(at,2,2,peer.position,2,2);};
    t.registerMove(c);t.update(c);c.tick=2;const auto route=t.update(c);
    check(route.detour.has_value(),"passing interval fixture owns a shoulder route");
    const auto searches=t.stats().searches;
    c.position=*route.detour;c.tick=82;c.obstruction.reset();c.blocked=0;
    for(;c.tick<202;++c.tick) {
        const auto result=t.update(c);
        check(result.wait&&!result.detour&&!result.repath&&!result.settled,
            "completed shoulder waits for 120 ticks even after its obstruction clears");
        check(t.stats().reservations==0&&t.stats().searches==searches,
            "completed shoulder retains no swept claim and performs no new search");
    }
    const auto released=t.update(c);
    check(released.repath&&!released.wait&&!released.settled,
        "passing interval expires from physical completion and resumes the mission");
    ++c.tick;check(!t.update(c).wait,"expired shoulder cannot restart its passing interval");
}
void retainedRouteAndIdentity() {
    Traffic t;auto c=context(1,{100,100},{900,100});c.blocked=2;c.obstructionFriendly=true;
    auto peer=context(2,{102,100},{900,100});c.obstruction=neighbor(peer);
    c.free=[&](Cell at){return !overlap(at,2,2,peer.position,2,2);};
    t.registerMove(c);t.update(c);c.tick=2;auto result=t.update(c);
    check(result.detour.has_value(),"same-way obstruction has a bounded complete route");
    const auto searches=t.stats().searches;const Cell first=*result.detour;
    c.tick=3;result=t.update(c);
    check(result.detour==first&&t.stats().searches==searches,"retained lane does not replan every tick");
    c.position=first;c.tick=4;result=t.update(c);
    check(result.detour.has_value()||result.repath,"route advances after first steering leg");
    ++c.issuedTick;c.target={100,900};c.steeringTarget=c.target;c.obstruction.reset();c.blocked=0;++c.tick;
    result=t.update(c);
    check(!result.detour&&t.stats().reservations==0,"new order releases previous route and claim");
}
void quotasAndDeterminism() {
    Traffic a,b;std::vector<Traffic::Context> inputs;
    for(int id=1;id<=160;++id) {
        auto c=context(id,{100+id*30,100},{100+id*30,1000});c.blocked=2;c.obstructionFriendly=true;
        auto peer=context(10000+id,{c.position.x,102},c.target);c.obstruction=neighbor(peer);
        const Cell start=c.position;c.free=[start](Cell at){return at==start;};
        inputs.push_back(c);a.registerMove(c);b.registerMove(c);
    }
    for(uint32_t tick=1;tick<=5;++tick) {
        const auto before=a.stats();
        for(auto& c:inputs){c.tick=tick;a.update(c);b.update(c);}
        const auto after=a.stats();
        check(after.probes-before.probes<=8192,"shared footprint probe allowance");
        check(after.searches-before.searches<=64,"fixed planning admissions");
        check(a.checksum()==b.checksum(),"repeatable state including fair admission cursor");
    }
    check(a.stats().searches>=160,"rotating admission services the complete population");
    a.prune(160,[](int,int,uint64_t,Cell,bool){return false;});
    check(a.stats().records==0&&a.stats().reservations==0,"death prune removes all sparse claims");
}
void largeRegistration() {
    Traffic t;
    for(int id=1;id<=16000;++id) {
        auto c=context(id,{id%256,id/256},{1200,1200});t.registerMove(c);
    }
    const auto s=t.stats();check(s.records==16000,"16000 members fit coordinator capacity");
    check(s.bytes<=Traffic::memoryLimit,"full population fits explicit memory reservation");
    check(s.searches==0&&s.reservations==0,"clear population allocates no movement reservations");
}
void passageBatch() {
    Traffic t;std::vector<Traffic::Context> east;
    for(int id=1;id<=9;++id){east.push_back(context(id,{88,100},{900,100}));t.registerMove(east.back());}
    auto west=context(20,{144,100},{0,100});t.registerMove(west);
    for(const auto& c:east)t.setPassage(c.id,Traffic::Passage{{100,100},{132,100},2});
    t.setPassage(west.id,Traffic::Passage{{132,100},{100,100},2});
    check(!t.update(east[0]).wait,"first stream receives single-file entry permit");
    check(t.update(west).wait,"opposite entry waits before the narrow mouth");
    for(size_t i=1;i<8;++i)check(!t.update(east[i]).wait,"bounded same-direction batch enters");
    check(t.update(east[8]).wait,"waiting opposite stream caps current batch");
    for(size_t i=0;i<8;++i){east[i].position={140,100};east[i].tick=2;t.update(east[i]);}
    west.tick=2;check(!t.update(west).wait,"drained passage switches to waiting stream");
    east[8].tick=2;check(t.update(east[8]).wait,"next batch cannot override active opposite permit");
    t.cancel(west.id);east[8].tick=3;
    check(!t.update(east[8]).wait,"cancel releases passage immediately for the next stream");
}
void staleProgressDoesNotExpireNewRoute() {
    Traffic t;auto c=context(1,{100,100},{900,100});t.registerMove(c);
    c.tick=1000;c.blocked=2;c.obstructionFriendly=true;
    auto other=context(2,{102,100},{900,100});c.obstruction=neighbor(other);
    c.free=[&](Cell at){return !overlap(at,2,2,other.position,2,2);};
    t.update(c);++c.tick;const auto result=t.update(c);
    check(result.detour&&!result.repath,"new route has its own progress deadline after a long queue wait");
    check(t.stats().reservations>0,"new escape receives a real swept claim after long stationary period");
}
void disconnectedLocalExit() {
    Traffic t;auto c=context(1,{100,100},{900,100});c.blocked=2;c.obstructionFriendly=true;
    c.obstruction=neighbor(context(2,{102,100},{900,100}));
    c.terrainFree=[](Cell at){return !(at.z==101&&at.x>100&&at.x<=112);};
    c.free=[&](Cell at){return c.terrainFree(at)&&(at.x<=100||at.z>=102);};
    t.registerMove(c);t.update(c);c.tick=2;auto result=t.update(c);
    check(!result.detour,"forward projection across a dividing wall is not a route exit");
    c.free=[start=c.position](Cell at){return at==start;};
    c.tick=200;result=t.update(c);
    check(result.repath,"local window with neither forward nor shoulder exit eventually reanchors the terrain prefix");
    ++c.tick;result=t.update(c);check(!result.repath,"reanchor attempts remain rate limited");
}
void passageReconciliation() {
    Traffic t;auto east=context(1,{88,100},{900,100}),west=context(2,{144,100},{0,100});
    t.registerMove(east);t.registerMove(west);
    const Traffic::Passage forward{{100,100},{132,100},2},reverse{{132,100},{100,100},2};
    t.setPassage(east.id,forward);t.setPassage(west.id,reverse);
    check(!t.update(east).wait&&t.update(west).wait,"reconciliation fixture starts with one permitted stream");
    east.tick=2;east.position={132,100};t.update(east);t.setPassage(east.id,{});
    west.tick=2;check(t.update(west).wait,"route replacement retains permit while rear footprint overlaps corridor");
    east.tick=3;east.position={133,100};t.update(east);west.tick=3;
    check(!t.update(west).wait,"stale descriptor releases as soon as complete body clears old strip");
    t.cancel(west.id);east.position={90,140};east.tick=4;t.registerMove(east);t.setPassage(east.id,forward);
    west.id=3;west.controller=3;west.tick=4;t.registerMove(west);t.setPassage(west.id,reverse);
    t.update(east);check(!t.update(west).wait,"far transverse approach cannot reserve a passage prematurely");
    t.cancel(west.id);east.position={90,100};east.tick=5;t.update(east);
    east.position={90,140};t.registerMove(east);t.setPassage(east.id,{});
    west.id=4;west.controller=4;west.tick=5;t.registerMove(west);t.setPassage(west.id,reverse);
    check(!t.update(west).wait,"replacement outside corridor releases ownership immediately");
}
void latePassageEntryRetreats() {
    Traffic t;auto east=context(1,{99,100},{900,100}),west=context(2,{144,100},{0,100});
    t.registerMove(east);t.registerMove(west);
    t.setPassage(west.id,Traffic::Passage{{132,100},{100,100},2});
    check(!t.update(west).wait,"outgoing stream owns the passage");
    // A bounded terrain prefix finished just outside the opposite mouth before
    // its next route identified the passage. The body must vacate that outlet.
    t.setPassage(east.id,Traffic::Passage{{100,100},{132,100},2});
    t.update(east);east.tick=2;const auto result=t.update(east);
    check(result.detour&&result.detour->x<east.position.x,"late denied entrant retreats instead of blocking outgoing stream");
    check(!result.settled&&t.stats().reservations>0,"retreat retains mission and reserves a legal movement leg");
}
void widenedBayKeepsOwnership() {
    Traffic t;auto east=context(1,{88,100},{900,100}),west=context(2,{144,100},{0,100});
    t.registerMove(east);t.registerMove(west);
    t.setPassage(east.id,Traffic::Passage{{100,100},{132,100},2,12});
    t.setPassage(west.id,Traffic::Passage{{132,100},{100,100},2,12});
    check(!t.update(east).wait&&t.update(west).wait,"merged corridor uses narrowest width for admission");
    east.position={116,104};east.tick=2;t.update(east);t.setPassage(east.id,{});west.tick=2;
    check(t.update(west).wait,"member in a widened bay retains corridor ownership outside narrow centerline");
    east.position={116,107};east.tick=3;t.update(east);west.tick=3;
    check(!t.update(west).wait,"stale bay ownership releases after the full body leaves its extent");
}
void alliedPassagesOnly() {
    Traffic t;auto a=context(1,{88,100},{900,100}),b=context(2,{144,100},{0,100});b.player=2;
    t.registerMove(a);t.registerMove(b);
    const Traffic::Passage forward{{100,100},{132,100},2},reverse{{132,100},{100,100},2};
    t.setPassage(a.id,forward);t.setPassage(b.id,reverse);
    check(!t.update(a).wait&&!t.update(b).wait,"hostile streams cannot claim each other's passage scheduler");
    t.setAllianceMask(1,6);t.setAllianceMask(2,6);++a.tick;++b.tick;t.registerMove(a);t.registerMove(b);
    t.setPassage(a.id,forward);t.setPassage(b.id,reverse);
    check(!t.update(a).wait&&t.update(b).wait,"allied players share a canonical passage lease");
    t.setAllianceMask(1,2);t.setAllianceMask(2,4);++b.tick;t.registerMove(b);t.setPassage(b.id,reverse);
    check(!t.update(b).wait,"changed allegiance cannot retain a former ally's admission lock");
}
void expensiveJobsRemainFair() {
    Traffic t;std::vector<Traffic::Context> callers;int lastProbes=0;
    for(int id=1;id<=17;++id) {
        auto c=context(id,{id*100,100},{id*100,1000});c.footX=c.footZ=8;c.blocked=2;c.obstructionFriendly=true;
        c.obstruction=neighbor(context(1000+id,{id*100,108},c.target));
        c.free=[id,&lastProbes](Cell){if(id==17){++lastProbes;return true;}return false;};
        callers.push_back(c);t.registerMove(c);
    }
    for(uint32_t tick=1;tick<=10;++tick) {
        const auto before=t.stats().probes;
        for(auto& c:callers){c.tick=tick;t.update(c);}
        check(t.stats().probes-before<=8192,"expensive callers share the same weighted quota");
    }
    check(lastProbes>0,"actual probe rotation serves high IDs after lower IDs exhaust the quota");
}
void targetedPassageRegistration() {
    Traffic t;auto a=context(1,{88,100},{900,100}),b=context(2,{144,100},{0,100});
    a.plainMove=b.plainMove=false;a.targetId=b.targetId=99;
    t.registerMove(a);t.registerMove(b);
    t.setPassage(a.id,Traffic::Passage{{100,100},{132,100},2});
    t.setPassage(b.id,Traffic::Passage{{132,100},{100,100},2});
    check(t.stats().records==2,"targeted controllers register before first traffic update");
    check(!t.update(a).wait&&t.update(b).wait,"targeted orders accept passage descriptors before route delivery");
    a.position=a.target;a.goalReached=true;++a.tick;
    check(!t.update(a).settled&&t.arrivalRadiusSquared(a)==0,"targeted mission arrival remains owned by its mission");
}
void reservedArrivalCorner() {
    Traffic t;auto c=context(1,{116,102},{100,100});
    c.terrainFree=[](Cell at){return !(at.z==101&&at.x>=114&&at.x<=116);};
    c.free=c.terrainFree;
    // The raster route first walks west, then north. From the body's northwest
    // subcell corner, a straight ray to this same slot immediately hits north.
    c.arrivalReachable=[&](Cell at){return at==c.target&&tak::sim::flow::directRoute(c.position,at,64,c.terrainFree,[at](Cell p){return p==at;});};
    for(int id=1;id<=16;++id){auto member=c;member.id=id;member.controller=id;t.registerMove(member);}
    t.update(c);++c.tick;const auto slot=t.update(c);
    check(slot.arrivalApproach&&slot.detour==c.target,"corner fixture reserves a raster-reachable arrival slot");
    c.blocked=2;++c.tick;const auto pending=t.update(c);
    check(pending.wait&&pending.arrivalApproach&&!pending.detour,"blocked slot approach defers to bounded route admission");
    ++c.tick;const auto leg=t.update(c);
    check(leg.arrivalApproach&&leg.detour&&leg.detour->x<c.position.x&&leg.detour->z==c.position.z,
        "reserved slot repairs north-corner refusal with a proved westward leg");
    const auto searches=t.stats().searches;c.blocked=0;++c.tick;
    const auto retained=t.update(c);
    check(retained.arrivalApproach&&retained.detour==leg.detour&&t.stats().searches==searches,
        "arrival ownership and short reservation persist after body refusal clears");
    const auto proved=c.arrivalReachable;bool deferred=false;c.arrivalDeferred=&deferred;
    c.arrivalReachable=[&](Cell){deferred=true;return false;};c.blocked=2;++c.tick;
    const auto proofWait=t.update(c);
    check(proofWait.wait&&proofWait.arrivalApproach&&t.stats().reservations>0,
        "deferred slot proof retains the arrival route and its movement claim");
    c.arrivalReachable=proved;deferred=false;c.blocked=0;++c.tick;const auto resumed=t.update(c);
    check(resumed.arrivalApproach&&resumed.detour==leg.detour&&t.stats().searches==searches,
        "available proof resumes the same arrival leg without replanning");
    bool done=false;
    for(int tick=8;tick<42&&!done;++tick) {
        c.tick=uint32_t(tick);const auto step=t.update(c);
        if(step.settled){done=true;break;}
        check(step.arrivalApproach,"local slot route cannot prematurely complete the mission");
        if(step.detour&&!step.wait)c.position=*step.detour;
    }
    check(done&&t.stats().reservations==0,"repaired approach reaches its proved slot and releases movement claims");

    // A lost slot cannot leave the old local route steering toward it.
    Traffic invalid;auto q=context(1,{116,102},{100,100});q.free=c.free;q.terrainFree=c.terrainFree;
    q.arrivalReachable=[](Cell at){return at==Cell{100,100};};
    for(int id=1;id<=16;++id){auto member=q;member.id=id;member.controller=id;invalid.registerMove(member);}
    invalid.update(q);q.tick=2;invalid.update(q);q.blocked=2;q.tick=3;invalid.update(q);q.tick=4;
    check(invalid.update(q).arrivalApproach&&invalid.stats().reservations>0,"invalid-slot fixture has a retained movement claim");
    q.arrivalReachable=[](Cell){return false;};++q.tick;const auto lost=invalid.update(q);
    check(!lost.arrivalApproach&&!lost.detour&&invalid.stats().reservations==0,"lost arrival proof cancels the retained slot route");
}
void closedRightShoulder() {
    Traffic t;auto c=context(1,{100,100},{900,100}),peer=context(2,{102,100},{0,100});
    c.blocked=2;c.obstructionFriendly=true;c.obstruction=neighbor(peer);
    c.terrainFree=[](Cell at){return at.z<=100;};
    c.free=[&](Cell at){return c.terrainFree(at)&&!overlap(at,2,2,peer.position,2,2);};
    t.registerMove(c);t.update(c);++c.tick;const auto before=t.stats();const auto turn=t.update(c);
    check(turn.detour&&turn.detour->z<c.position.z,"closed right shoulder falls back to a proved left passing exit");
    check(t.stats().searches-before.searches==1&&t.stats().probes-before.probes<=8192,
        "opposite shoulder reuses the same bounded search and shared allowance");
    const auto searches=t.stats().searches;++c.tick;
    check(t.update(c).detour==turn.detour&&t.stats().searches==searches,"alternate shoulder stays committed while passing");
}
void arrivalWaitKeepsOwnership() {
    Traffic t;auto passing=context(1,{100,100},{900,100}),peer=context(2,{102,100},{0,100});
    passing.blocked=2;passing.obstructionFriendly=true;passing.obstruction=neighbor(peer);
    passing.free=[&](Cell at){return !overlap(at,2,2,peer.position,2,2);};t.registerMove(passing);
    auto arriving=context(3,{100,103},{100,90});arriving.arrivalReachable=[](Cell at){return at==Cell{100,90};};
    for(int id=3;id<=18;++id){auto member=arriving;member.id=id;member.controller=id;t.registerMove(member);}
    t.update(passing);t.update(arriving);passing.tick=arriving.tick=2;
    check(t.update(passing).detour.has_value(),"crossing fixture has a reserved passing leg");
    const auto wait=t.update(arriving);
    check(wait.wait&&wait.arrivalApproach&&!wait.detour,"crossing reservation wait retains the arrival slot's mission ownership");
}
void mergingLargeBodiesYieldRoom() {
    Traffic t;auto c=context(6,{1077,1021},{1040,300}),peer=context(1,{1074,1021},{1040,300});
    c.footX=c.footZ=peer.footX=peer.footZ=3;
    c.steeringTarget=Cell{1076,1022};peer.steeringTarget=Cell{1076,1021};
    c.blocked=2;c.obstructionFriendly=true;c.obstruction=neighbor(peer);
    // The southern merge is obstructed. The old first northward pocket left
    // this 3x3 body overlapping its peer's next eastward footprint.
    c.terrainFree=[](Cell at){return at.z<=1021;};
    c.free=[&](Cell at){return c.terrainFree(at)&&!overlap(at,3,3,peer.position,3,3);};
    t.registerMove(c);t.update(c);++c.tick;auto result=t.update(c);
    check(result.detour.has_value(),"large merge has a retained yielding route");
    bool finished=false;
    for(int tick=3;tick<20;++tick) {
        if(result.detour)c.position=*result.detour;
        c.tick=uint32_t(tick);result=t.update(c);
        if(!result.detour){finished=true;break;}
    }
    check(finished&&!overlap(c.position,3,3,{1075,1021},3,3),
        "completed yield clears the larger peer's next movement footprint");
    check(c.position.z<=1018,"merge cannot accept the old one- or two-cell north pocket");
    Traffic pocket;auto trapped=context(1,{1074,1021},{1040,300}),other=context(6,{1077,1020},{1040,300});
    trapped.footX=trapped.footZ=other.footX=other.footZ=3;
    trapped.steeringTarget=other.steeringTarget=Cell{1076,1021};
    trapped.blocked=2;trapped.obstructionFriendly=true;trapped.obstruction=neighbor(other);
    trapped.free=[](Cell at){return at==Cell{1074,1021}||at==Cell{1073,1021}||at==Cell{1074,1020}||at==Cell{1074,1019};};
    trapped.terrainFree=trapped.free;pocket.registerMove(trapped);pocket.update(trapped);++trapped.tick;
    const auto wait=pocket.update(trapped);
    check(wait.wait&&!wait.detour&&pocket.stats().routes==0,
        "small shoulder pocket waits when no standing endpoint clears the peer's next sweep");

    Traffic escape;other.blocked=2;other.obstructionFriendly=true;other.obstruction=neighbor(trapped);
    other.free=[](Cell at){return at.x>=1077&&at.x<=1078&&at.z>=1020&&at.z<=1021;};
    other.terrainFree=other.free;escape.registerMove(other);escape.update(other);++other.tick;
    const auto clear=escape.update(other);
    check(clear.wait&&!clear.detour,"short forward pocket remains blocked when it still occupies the continuing peer lane");
    other.free=[](Cell at){return at.x>=1077&&at.x<=1078&&at.z>=1020&&at.z<=1024;};
    other.terrainFree=other.free;++other.tick;const auto farther=escape.update(other);
    check(farther.detour&&farther.detour->z>=1024,"expanded shoulder permits a complete standing footprint outside the peer lane");
}
void retreatMustLeavePeerLane() {
    Traffic t;auto c=context(18,{156,128},{40,128}),peer=context(2,{153,128},{200,128});
    c.footX=c.footZ=4;c.steeringTarget=Cell{107,128};
    c.blocked=2;c.obstructionFriendly=true;c.obstruction=neighbor(peer);
    // This is the east mouth of the mixed passing bay. A diagonal step to
    // (157,127) clears only the peer's first step, then blocks its second.
    c.terrainFree=[](Cell){return true;};
    c.free=[&](Cell at){return !overlap(at,4,4,peer.position,2,2);};
    t.registerMove(c);t.update(c);++c.tick;auto step=t.update(c);
    check(step.detour.has_value(),"mixed bay has a legal complete yielding route");
    bool finished=false;
    for(int tick=3;tick<20;++tick) {
        if(step.detour)c.position=*step.detour;
        c.tick=uint32_t(tick);step=t.update(c);
        if(!step.detour){finished=true;break;}
    }
    check(finished,"mixed bay yield route finishes within the local window");
    for(int x=153;x<=170;++x)check(!overlap(c.position,4,4,{x,128},2,2),
        "yield endpoint must clear the continuing lane, not only the immediate peer step");
}
void stationaryQueueCanYieldAside() {
    Traffic t;auto c=context(1,{100,100},{900,100}),peer=context(2,{103,100},{900,100});
    c.footX=c.footZ=peer.footX=peer.footZ=3;
    c.blocked=2;c.obstructionFriendly=true;c.obstruction=neighbor(peer);
    // Forward projection cannot leave this four-cell-deep queue pocket. A
    // legal shoulder behind the mover clears the stationary front member's
    // continuing lane; taking it lets that member or another peer move.
    c.free=[](Cell at){return at.x>=96&&at.x<=100&&at.z>=96&&at.z<=104;};
    c.terrainFree=[](Cell){return true;};t.registerMove(c);t.update(c);++c.tick;
    const auto early=t.update(c);check(!early.detour,"progressing same-way queue keeps its installed route");
    c.tick=181;const auto before=t.stats();auto step=t.update(c);
    check(step.detour.has_value(),"stationary same-way queue can yield into a proved clear shoulder");
    check(t.stats().searches-before.searches==1&&t.stats().probes-before.probes<=8192,
        "stalled shoulder reuses the forward search and shared work quota");
    for(int tick=182;tick<200&&step.detour;++tick){c.position=*step.detour;c.tick=uint32_t(tick);step=t.update(c);}
    for(int x=103;x<=116;++x)check(!overlap(c.position,3,3,{x,100},3,3),
        "same-way yielding endpoint leaves the peer's continuing lane clear");

    Traffic forward;auto q=context(1,{100,100},{900,100});q.blocked=2;q.obstructionFriendly=true;
    q.obstruction=neighbor(context(2,{102,100},{900,100}));
    q.free=[](Cell at){return at.z<=100;};q.terrainFree=[](Cell){return true;};
    forward.registerMove(q);forward.update(q);q.tick=181;const auto advancing=forward.update(q);
    check(advancing.detour&&advancing.detour->x>q.position.x,
        "available connected forward exit outranks the remembered stalled shoulder");
}
void expensiveAdmissionWindowsRemainFair() {
    Traffic t;std::vector<Traffic::Context> callers;
    std::array<unsigned,256> probes{};
    for(int id=1;id<=256;++id) {
        auto c=context(id,{id*64,100},{id*64,2000});
        c.footX=c.footZ=32;c.blocked=2;c.obstructionFriendly=true;
        c.obstruction=neighbor(context(1000+id,{id*64,132},c.target));
        c.free=[id,&probes](Cell){++probes[size_t(id-1)];return false;};
        callers.push_back(c);t.registerMove(c);
    }
    // Eight rejected neighboring footprints consume the entire tick allowance.
    // Multiple admission windows must not phase-lock with the probe cursor.
    for(uint32_t tick=1;tick<=513;++tick) {
        const auto before=t.stats();
        for(auto& c:callers){c.tick=tick;t.update(c);}
        const auto after=t.stats();
        check(after.probes-before.probes<=8192,"large admission population shares the weighted probe quota");
        check(after.searches-before.searches<=64,"large admission population retains the search admission cap");
    }
    for(unsigned count:probes)check(count>=16,"every expensive caller receives two complete attempts across admission windows");
}
void enclosedIdlePocketWaits() {
    Traffic t;auto c=context(1,{100,100},{0,100});
    c.blocked=2;c.obstructionFriendly=true;
    c.obstruction=neighbor(context(2,{98,101},{98,101}));
    c.obstruction->idle=true;c.obstruction->steeringTarget.reset();
    c.free=[](Cell at){return at.x>=100&&at.x<=101&&at.z>=100&&at.z<=102;};
    c.terrainFree=[](Cell){return true;};t.registerMove(c);t.update(c);
    for(c.tick=181;c.tick<600;++c.tick) {
        const auto result=t.update(c);
        check(!result.detour&&!result.settled&&(result.wait||result.repath),
            "enclosed idle-body pocket waits instead of repeatedly vacating a nonexistent moving lane");
    }
    check(t.stats().routes==0,"closed idle pocket publishes no pointless shoulder route");
    c.free=[](Cell at){return !overlap(at,2,2,{98,101},2,2);};
    bool released=false;
    for(int retry=0;retry<=9&&!released;++retry,++c.tick)released=t.update(c).detour.has_value();
    check(released,"idle blocker permits a proved forward bypass after its bounded retry delay");
}
void exhaustiveFailuresUseBoundedRetryScheduling() {
    Traffic t;auto c=context(1,{100,100},{900,100});
    c.blocked=2;c.obstructionFriendly=true;c.obstruction=neighbor(context(2,{102,100},c.target));
    c.free=[](Cell){return false;};t.registerMove(c);
    for(c.tick=1;c.tick<=91;++c.tick) {
        const auto result=t.update(c);
        check(result.wait&&!result.detour&&result.followLeader==2,
              "retry scheduling retains ordinary same-way follower eligibility");
    }
    const auto before=t.stats();
    check(before.searches>=11&&before.searches<=12&&before.completeFailures==before.searches&&
          before.deferredSearches==0&&before.retrySkips>=70,
          "exhaustive closed searches repeat only after their bounded delay");
    // Other bodies and terrain can change without changing the first peer.
    // They are not a reused NoRoute proof: opening any free exit is discovered
    // within eight ticks plus the existing one-tick admission in this fixture.
    c.free=[](Cell){return true;};bool moved=false;
    for(int retry=0;retry<=9&&!moved;++retry,++c.tick)moved=t.update(c).detour.has_value();
    check(moved,"an untracked clearance change outlived bounded retry scheduling");

    for(int change=0;change<7;++change) {
        Traffic wake;auto q=context(1,{100,100},{900,100});
        q.blocked=2;q.obstructionFriendly=true;q.obstruction=neighbor(context(2,{102,100},q.target));
        q.free=[](Cell){return false;};wake.registerMove(q);
        for(q.tick=1;q.tick<=3;++q.tick)wake.update(q);
        const auto searches=wake.stats().searches;
        switch(change) {
            case 0:++q.position.z;break;
            case 1:++q.obstruction->position.z;break;
            case 2:++q.obstruction->id;break;
            case 3:q.steeringTarget=Cell{900,101};break;
            case 4:++q.controller;break;
            case 5:++q.issuedTick;break;
            case 6:q.obstruction->steeringTarget=Cell{0,100};break;
        }
        wake.update(q);++q.tick;wake.update(q);
        check(wake.stats().searches>searches,"changed position, mission, aim or peer did not wake a failed search");
    }
}
void limitedSearchesNeverBecomeCompleteFailures() {
    for(int bound=0;bound<3;++bound) {
        Traffic t;auto c=context(1,{100,100},{900,100});
        c.blocked=2;c.obstructionFriendly=true;c.obstruction=neighbor(context(2,{102,100},c.target));
        c.obstruction->idle=true;
        if(bound==0) {
            c.footX=c.footZ=64;c.free=[](Cell){return false;};
        } else if(bound==1) {
            c.footX=c.footZ=1;c.free=[](Cell){return true;};c.terrainFree=[](Cell){return false;};
        } else {
            c.footX=c.footZ=1;
            c.free=[](Cell at) {
                if(at.z==100||at.z==102||at.z==104)return at.x>=88&&at.x<=100;
                if(at.z==106)return at.x>=88&&at.x<=104;
                return (at.z==101||at.z==105)?at.x==88:at.z==103&&at.x==100;
            };
        }
        t.registerMove(c);
        for(c.tick=1;c.tick<=6;++c.tick) {
            const auto before=t.stats();const auto result=t.update(c);const auto after=t.stats();
            check(result.wait&&!result.detour,"incomplete local proof unexpectedly installed a route");
            check(after.probes-before.probes<=8192,"deferred search exceeded its shared footprint quota");
        }
        const auto stats=t.stats();
        check(stats.searches==5&&stats.deferredSearches==5&&stats.completeFailures==0&&stats.retrySkips==0,
              "quota, visit or retained-path limit became a reusable complete failure");
    }
}
void retrySchedulingExcludesPassageOppositionAndArrival() {
    for(bool passage:{false,true}) {
        Traffic t;auto c=context(1,{100,100},{900,100});
        c.blocked=2;c.obstructionFriendly=true;
        c.obstruction=neighbor(context(2,{102,100},passage?c.target:Cell{0,100}));
        c.free=[](Cell){return false;};t.registerMove(c);
        if(passage)check(t.setPassage(c.id,Traffic::Passage{{300,100},{340,100},2}),
                         "retry exclusion fixture could not register its upcoming passage");
        for(c.tick=1;c.tick<=6;++c.tick)t.update(c);
        const auto stats=t.stats();
        check(stats.searches==5&&stats.completeFailures==5&&stats.retrySkips==0,
              "opposing or registered passage movement acquired an ordinary retry delay");
    }
    Traffic t;auto c=context(1,{116,102},{100,100});
    c.arrivalReachable=[goal=c.target](Cell at){return at==goal;};
    for(int id=1;id<=16;++id){auto member=c;member.id=id;member.controller=id;t.registerMove(member);}
    t.update(c);++c.tick;const auto slot=t.update(c);
    check(slot.arrivalApproach&&slot.detour==c.target,"retry exclusion fixture did not own an arrival slot");
    c.blocked=2;c.free=[goal=c.target](Cell at){return at==goal;};
    ++c.tick;t.update(c);++c.tick;t.update(c);
    check(t.stats().completeFailures==1&&t.stats().retrySkips==0,
          "closed local arrival approach did not remain an uncached complete failure");
    c.free=[](Cell){return true;};++c.tick;const auto resumed=t.update(c);
    check(resumed.arrivalApproach&&resumed.detour&&!resumed.wait&&t.stats().retrySkips==0,
          "newly clear reserved arrival route waited for ordinary retry expiry");
}
void retryAndFollowingRequireOpenTerrain() {
    Traffic t;auto c=context(1,{100,100},{900,100});
    c.blocked=2;c.obstructionFriendly=true;c.obstruction=neighbor(context(2,{102,100},c.target));
    c.free=[](Cell){return false;};c.cooperativeOpenTerrain=false;t.registerMove(c);
    for(c.tick=1;c.tick<=6;++c.tick) {
        const auto result=t.update(c);
        check(result.wait&&!result.detour&&!result.followLeader,"non-open terrain offered follower movement");
    }
    check(t.stats().searches==5&&t.stats().completeFailures==5&&!t.stats().retrySkips,
          "non-open terrain acquired a failure retry delay");
    c.cooperativeOpenTerrain=true;t.update(c);++c.tick;t.update(c);
    const auto before=t.stats();check(before.retrySkips==1,"open terrain did not acquire its bounded retry schedule");
    c.cooperativeOpenTerrain=false;++c.tick;const auto result=t.update(c);
    check(t.stats().searches==before.searches+1&&t.stats().retrySkips==before.retrySkips&&!result.followLeader,
          "losing the open proof retained an earlier retry delay or follower eligibility");
    c.obstruction->idle=true;++c.tick;t.update(c);const auto idle=t.stats();++c.tick;t.update(c);
    check(t.stats().searches==idle.searches&&t.stats().retrySkips==idle.retrySkips+1,
          "an unchanged idle obstruction on non-open terrain lost bounded retry scheduling");
}
void passageCapacityDefersWithoutLosingOwnership() {
    Traffic t;auto east=context(1,{112,100},{900,100}),west=context(2,{144,100},{0,100});
    const Traffic::Passage forward{{100,100},{132,100},2},reverse{{132,100},{100,100},2};
    t.registerMove(east);t.registerMove(west);
    check(t.setPassage(east.id,forward)&&t.setPassage(west.id,reverse),"initial passage descriptors are admitted");
    check(!t.update(east).wait&&t.update(west).wait,"capacity fixture owns a live passage lease");
    for(int id=3;id<=4097;++id) {
        const int x=200+(id-3)*4;
        auto member=context(id,{x-12,200},{x+100,200});t.registerMove(member);
        check(t.setPassage(id,Traffic::Passage{{x,200},{x+2,200},2}),"gate cache admits its bounded capacity");
    }
    auto extra=context(4098,{88,300},{900,300});t.registerMove(extra);
    const Traffic::Passage additional{{100,300},{132,300},2};
    const auto bytes=t.stats().bytes;
    for(int repeat=0;repeat<16;++repeat)
        check(!t.setPassage(extra.id,additional),"new passage admission explicitly defers at capacity");
    check(t.stats().bytes==bytes&&bytes<=Traffic::memoryLimit,"rejected descriptors allocate no overflow state");
    check(!t.setPassage(999999,{}),"missing coordinator member cannot admit a descriptor");
    check(!t.setPassage(east.id,additional),"shared occupied gate cannot release its lease for a rejected replacement");
    ++west.tick;check(t.update(west).wait,"rejected replacement preserves the occupied old lease");
    check(t.setPassage(east.id,forward),"same descriptor remains admissible at capacity");
    check(t.setPassage(4,Traffic::Passage{{100,400},{132,400},2}),"sole-member replacement reuses its gate allocation");
    check(t.setPassage(east.id,{}),"clearing inside a passage retains its stale lease successfully");
    ++west.tick;check(t.update(west).wait,"successful clear still holds ownership until the body exits");
    t.cancel(3);
    check(t.setPassage(extra.id,additional),"deferred descriptor succeeds after capacity is released");
    extra.tick=west.tick;check(!t.update(extra).wait,"admitted retry receives a real passage permit");
    check(t.setPassage(extra.id,{}),"clear outside passage succeeds");
    check(t.setPassage(extra.id,{}),"empty descriptor clear is a successful no-op");
}
void invalidFootprintsDoNoWork() {
    for(int size:{-1,0,65,std::numeric_limits<int>::max()})for(bool xAxis:{false,true}) {
        Traffic t;auto c=context(1,{100,100},{900,100});int callbacks=0;
        c.free=[&](Cell){++callbacks;return true;};c.terrainFree=c.free;c.arrivalReachable=c.free;
        (xAxis?c.footX:c.footZ)=size;c.blocked=2;c.obstructionFriendly=true;
        c.obstruction=neighbor(context(2,{102,100},{900,100}));
        t.registerMove(c);const auto result=t.update(c);
        check(result.wait&&!result.settled&&!result.detour&&callbacks==0&&t.stats().records==0,
            "unsupported footprint is deferred before registration, geometry or callbacks");
    }
    Traffic t;auto c=context(1,{100,100},{900,100});c.blocked=2;c.obstructionFriendly=true;
    c.obstruction=neighbor(context(2,{102,100},{900,100}));
    t.registerMove(c);t.update(c);++c.tick;check(t.update(c).detour.has_value(),"footprint invalidation fixture owns a movement claim");
    c.footX=65;++c.tick;check(t.update(c).wait&&t.stats().records==0&&t.stats().reservations==0,
        "invalid resized body releases its prior valid movement claims");
}
}
int main() {
    groupCentroid();opposedShoulder();diagonalProof();completedShoulderKeepsPassingInterval();retainedRouteAndIdentity();quotasAndDeterminism();largeRegistration();passageBatch();staleProgressDoesNotExpireNewRoute();disconnectedLocalExit();passageReconciliation();latePassageEntryRetreats();widenedBayKeepsOwnership();alliedPassagesOnly();expensiveJobsRemainFair();targetedPassageRegistration();reservedArrivalCorner();closedRightShoulder();arrivalWaitKeepsOwnership();mergingLargeBodiesYieldRoom();retreatMustLeavePeerLane();stationaryQueueCanYieldAside();expensiveAdmissionWindowsRemainFair();enclosedIdlePocketWaits();exhaustiveFailuresUseBoundedRetryScheduling();limitedSearchesNeverBecomeCompleteFailures();retrySchedulingExcludesPassageOppositionAndArrival();retryAndFollowingRequireOpenTerrain();passageCapacityDefersWithoutLosingOwnership();invalidFootprintsDoNoWork();
    std::cout<<"cooperative traffic tests passed\n";
}
