// Shared distant detailed fields: destinations whose goals lie in the same
// goal tiles share every field at least one tile away from those tiles when
// the exact exit-seed mask also agrees. These checks prove the shared field is
// byte-identical to what each destination would build alone, that it stays a
// strictly descending, legal route, and that topology publication can both
// disconnect a route and expose a better one without reusing stale content.
#include "sim/flowfield.h"
#include "sim/flowservice.h"
#include "sim/flowsnapshot.h"
#include <cstdio>
#include <map>
#include <stdexcept>
#include <vector>

using namespace tak::sim::flow;
namespace {
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
std::shared_ptr<const Topology> build(int w,int h,const std::vector<uint16_t>& costs) {
    TopologyBuilder builder(w,h);
    for(int z=0;z<h;++z)for(int x=0;x<w;++x)builder.setCost({x,z},costs[size_t(z)*w+x]);
    while(!builder.done()&&!builder.failed())builder.step(211);
    check(!builder.failed(),"unexpected topology limit");return builder.finish();
}
std::shared_ptr<Destination> destination(std::shared_ptr<const Topology> topo,Cell goal) {
    auto d=std::make_shared<Destination>(std::move(topo),std::vector<Cell>{goal});
    while(!d->done())d->step(37);
    return d;
}
Field field(std::shared_ptr<const Destination> d,int tile,std::optional<std::pair<Cell,Cell>> anchor={}) {
    FieldBuilder b(std::move(d),tile,anchor);while(!b.done())b.step(29);return b.field();
}
// Walk one unit through the service, ticking whenever a field is pending.
// Every step must be legal, the per-tile potential must strictly descend, and
// the walk must end Arrived (or Unreachable when expected).
struct Walk {Service::Status status=Service::Status::Pending;int steps=0;uint64_t length=0;};
Walk walk(Service& service,int unit,const Topology& t,Cell from,int limit=100000) {
    Walk out;Cell at=from;
    for(int guard=0;guard<limit;++guard) {
        const auto s=service.sample(unit,at);
        if(s.status==Service::Status::Pending){service.tick();continue;}
        if(s.status!=Service::Status::Ready){out.status=s.status;return out;}
        const Cell next=s.next;
        check(t.cost(next)!=0,"shared route entered a blocked cell");
        check(std::abs(next.x-at.x)<=1&&std::abs(next.z-at.z)<=1&&next!=at,"shared route made a non-local step");
        if(next.x!=at.x&&next.z!=at.z)check(t.cost({at.x,next.z})&&t.cost({next.x,at.z}),"shared route cut a corner");
        out.length+=next.x!=at.x&&next.z!=at.z?1448:1024;at=next;
        check(++out.steps<=t.width*t.height,"shared route cycled");
    }
    throw std::runtime_error("walk did not finish");
}
std::vector<uint16_t> open(int w,int h) {return std::vector<uint16_t>(size_t(w)*h,1);}
}
int main() {
    try {
        constexpr int W=448,H=192;
        {
            // Identity: two goals in the same goal tile share far fields exactly.
            auto t=build(W,H,open(W,H));
            const auto a=destination(t,{400,100}),b=destination(t,{430,120});
            const std::pair<Cell,Cell> anchor{{384,64},{447,127}};
            size_t compared=0;
            for(int tile=0;tile<t->tilesX*t->tilesZ;++tile) {
                const int tx=tile%t->tilesX;if(tx>=5)continue;
                check(a->exits(tile)==b->exits(tile),"same goal component produced different exit masks");
                check(field(a,tile,anchor).hash()==field(b,tile,anchor).hash(),"anchored field depends on the exact goal");
                // The key alone (topology, tile, exits, box) builds the same field.
                FieldBuilder keyed(t,tile,a->exits(tile),anchor);while(!keyed.done())keyed.step(41);
                check(keyed.field().hash()==field(a,tile,anchor).hash(),"key-built shared field differs");
                ++compared;
            }
            check(compared==15,"identity test covered unexpected tiles");
            bool threw=false;
            try {FieldBuilder bad(a,13,anchor);} catch(const std::invalid_argument&) {threw=true;}
            check(threw,"anchored field accepted a tile containing its goal");
            // An anchored field depends only on the box, also for a finer one.
            const auto c=destination(t,{410,108});
            const std::pair<Cell,Cell> block{{400,96},{415,111}};
            check(a->exits(12)==c->exits(12)&&field(a,12,block).hash()==field(c,12,block).hash(),
                "neighbour field depends on the exact goal inside its block");
            // Without an anchor the near-goal field keeps the exact goal bias.
            check(field(a,12).hash()!=field(b,12).hash(),"private near-goal fields unexpectedly identical");
        }
        {
            // Service: many units with distinct goals in one goal tile build
            // each distant field once; near-goal tiles stay per destination.
            auto t=build(W,H,open(W,H));
            Service shared,separate(Service::Budget{.shareDistant=false});
            Walk first{};
            for(int unit=0;unit<12;++unit) {
                const Cell goal{390+unit*4,70+unit*4};
                for(Service* s:{&shared,&separate})check(s->bind(unit,1,t,{goal}),"bind");
                const auto a=walk(shared,unit,*t,{5,10+unit*10});
                const auto b=walk(separate,unit,*t,{5,10+unit*10});
                check(a.status==Service::Status::Arrived&&b.status==Service::Status::Arrived,"open route did not arrive");
                // Biasing distant tiles toward the goal tile cannot add more than
                // the bounded tie-break detour of a single near tile.
                check(a.length<=b.length+64*1024,"shared distant fields lengthened the route");
                if(!unit)first=a;
            }
            const auto& c=shared.counters();
            check(c.sharedFieldsBuilt>0&&c.sharedFieldsBuilt<separate.counters().fieldsBuilt/2,"distant fields were not shared");
            check(c.sharedReuses>0,"later destinations did not reuse distant fields");
            check(separate.counters().sharedResolutions==0,"disabled sharing still resolved shared fields");
            check(c.fieldsBuilt<separate.counters().fieldsBuilt,"sharing did not reduce detailed field builds");
            std::printf("shared fields: built %llu vs %llu (shared %llu, reuses %llu), first route %d steps\n",
                (unsigned long long)c.fieldsBuilt,(unsigned long long)separate.counters().fieldsBuilt,
                (unsigned long long)c.sharedFieldsBuilt,(unsigned long long)c.sharedReuses,first.steps);
            // The goal tile and its neighbours resolve privately.
            Service near;check(near.bind(1,1,t,{{400,100}}),"bind near");
            check(walk(near,1,*t,{330,100}).status==Service::Status::Arrived,"near route");
            check(near.counters().sharedResolutions==0,"goal tile or its neighbour used a shared field");
        }
        {
            // Serial and worker scheduling publish identical shared state.
            auto t=build(W,H,open(W,H));
            Service serial,workers;
            for(int unit=0;unit<24;++unit)for(Service* s:{&serial,&workers})
                check(s->bind(unit,1,t,{{392+unit%16*3,70+unit/16*20}}),"bind");
            for(int tick=0;tick<200;++tick) {
                for(int unit=0;unit<24;++unit) {
                    const Cell from{5+unit,5+unit*4};
                    const auto a=serial.sample(unit,from),b=workers.sample(unit,from);
                    check(a.status==b.status&&a.next==b.next,"workers changed shared sample");
                }
                serial.tick();workers.tick(4);
                check(serial.checksum()==workers.checksum()&&serial.publishedHash()==workers.publishedHash(),
                    "workers changed shared service state");
            }
            check(serial.counters().sharedReuses>0,"worker test did not exercise sharing");
        }
        {
            // Topology changes: a disconnection and a newly opened shortcut.
            // Each publication invalidates the profile; rebinding must never
            // reuse a shared field of the older generation.
            std::vector<uint16_t> gap=open(W,H),closed=open(W,H),shortcut=open(W,H);
            for(int z=0;z<H;++z) {
                gap[size_t(z)*W+200]=z>=180;                       // only a far bottom gap
                closed[size_t(z)*W+200]=0;                         // fully disconnected
                shortcut[size_t(z)*W+200]=z>=180||(z>=8&&z<20);    // plus a gap on the straight line
            }
            const Cell start{5,12},goal{430,12};
            Service service;
            auto before=build(W,H,gap);
            check(service.bind(1,7,before,{goal}),"bind before");
            const auto detour=walk(service,1,*before,start);
            check(detour.status==Service::Status::Arrived,"bottom-gap route failed");
            const auto builtBefore=service.counters().sharedFieldsBuilt;
            check(builtBefore>0,"detour used no shared fields");
            service.invalidate(7);
            check(service.fields()==0,"invalidation retained a shared field");
            auto disconnected=build(W,H,closed);
            check(service.bind(1,7,disconnected,{goal}),"bind disconnected");
            check(walk(service,1,*disconnected,start).status==Service::Status::Unreachable,
                "disconnected route still reached the goal through a stale field");
            service.invalidate(7);
            auto opened=build(W,H,shortcut);
            check(service.bind(1,7,opened,{goal}),"bind opened");
            const auto better=walk(service,1,*opened,start);
            check(better.status==Service::Status::Arrived&&better.length+100*1024<detour.length,
                "opened shortcut was not used");
            check(service.counters().invalidatedFields>0,"stale shared fields were not discarded");
            // A wall inside a distant tile that leaves every exit mask intact:
            // the sharing key is unchanged, so only generation invalidation
            // keeps the old field from walking the unit into the new wall.
            {
                std::vector<uint16_t> walled=shortcut;
                for(int z=0;z<40;++z)for(int x=20;x<24;++x)walled[size_t(z)*W+x]=0;
                const auto rebuilt=build(W,H,walled);
                service.invalidate(7);
                check(service.bind(1,7,rebuilt,{goal}),"bind walled");
                const auto around=walk(service,1,*rebuilt,start);
                check(around.status==Service::Status::Arrived&&around.length>better.length,
                    "interior wall change reused a stale shared field");
            }
            std::printf("topology changes: detour %d steps, shortcut %d steps\n",detour.steps,better.steps);
        }
        {
            // Retention across publication. Snapshots reuse the immutable tiles
            // of unchanged regions; finished shared fields of those tiles may
            // survive. Every case must walk exactly like a service that
            // rebuilt everything from scratch, including when an unchanged
            // tile's exits change because a remote route closed or opened.
            std::vector<uint16_t> costs=open(W,H);
            const auto snapshot=[&](std::shared_ptr<const Topology> previous,std::vector<bool> dirty) {
                SnapshotBuilder b(W,H,1,1,[&](int x,int z){return costs[size_t(z)*W+x];},previous,dirty);
                while(!b.done()&&!b.failed())b.step(4096);
                check(b.done(),"retention snapshot failed");return b.finish();
            };
            const auto edit=[&](std::vector<bool>& dirty,int x,int z,int w,int h,uint16_t cost) {
                for(int zz=z;zz<z+h;++zz)for(int xx=x;xx<x+w;++xx)costs[size_t(zz)*W+xx]=cost;
                SnapshotBuilder::dirtyRectangle(dirty,W,H,1,1,x,z,w,h);
            };
            const Cell start{5,12},goal{430,12};
            Service kept;
            auto current=snapshot({},{});
            check(kept.bind(1,7,current,{goal}),"bind retention");
            check(walk(kept,1,*current,start).status==Service::Status::Arrived,"retention base route");
            struct Change {const char* name;int x,z,w,h;uint16_t cost;bool retains;};
            const Change changes[]={
                {"unrelated wall",140,150,10,10,0,true},
                {"interior wall on the route",20,0,4,40,0,true},
                {"remote closure changes exits",200,0,4,180,0,false},
                {"remote opening changes exits",200,8,4,12,1,false},
            };
            for(const auto& change:changes) {
                std::vector<bool> dirty(size_t(current->tilesX*current->tilesZ));
                edit(dirty,change.x,change.z,change.w,change.h,change.cost);
                auto next=snapshot(current,dirty);
                const auto before=kept.counters().retainedFields,built=kept.counters().fieldsBuilt;
                kept.invalidate(7,next);current=next;
                Service fresh(Service::Budget{.retainShared=false});
                check(kept.bind(1,7,current,{goal})&&fresh.bind(1,7,current,{goal}),"rebind after publication");
                const auto a=walk(kept,1,*current,start),b=walk(fresh,1,*current,start);
                check(a.status==b.status&&a.steps==b.steps&&a.length==b.length,change.name);
                if(change.retains)check(kept.counters().fieldsBuilt-built<fresh.counters().fieldsBuilt,
                    "unchanged tiles were rebuilt after publication");
                std::printf("retention %s: retained %llu, %d steps\n",change.name,
                    (unsigned long long)(kept.counters().retainedFields-before),a.steps);
            }
        }
        std::puts("PASS shared distant flow fields: identity, reuse, workers, disconnect, shortcut and retention");
        return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"flow shared field test: %s\n",e.what());return 1;}
}
