#include "cooperative_test_common.h"
#include "crowdbench_telemetry.h"
#include "sim/flowmemory.h"
#include <tuple>

using namespace cooperative_test;
using crowdbench_matrix::LatencyObserver;
namespace tak::sim {
struct RetailReplayProbe {
    static void observe(World& world,NavigationTelemetry* observer) {
        world.paths_.setTelemetry(observer);
        if(isSharedPathfinding(world.pathfindingMode_)) {
            if(!world.flow_)world.flow_=std::make_unique<FlowNavigator>(world);
            world.flow_->setTelemetry(observer);
        }
    }
    static FlowNavigator& flow(World& world) {return *world.flow_;}
};
}
namespace {
void request(PathService& service,int id=1,int goal=20,uint64_t controller=1) {
    service.request(id,{2,2},{goal,20},24,24,Fixed::fromInt(goal*16),Fixed::fromInt(320),
                    0,false,0,0,{},{},0,controller);
}
int score(int,int x,int z) {return x<0||z<0||x>=24||z>=24?0:6;}
void queueLifecycle() {
    LatencyObserver observer,other;PathService service;service.setTelemetry(&observer);
    observer.tick=3;request(service);request(service);
    require(observer.sequence==1&&observer.pending.size()==1,"duplicate retry became a new latency request");
    bool rejected=false;
    try{service.setTelemetry(&other);}catch(const std::logic_error&){rejected=true;}
    require(rejected,"observer replacement accepted outstanding tokens");
    uint64_t callbackTick=0;
    for(observer.tick=4;observer.tick<80&&service.pending(1);++observer.tick) {
        service.tick(score,[&](int,const std::vector<PathCell>&,Fixed,Fixed,bool,bool,bool,bool) {
            callbackTick=observer.tick;
            require(observer.deliveries.size()==1,"delivery observer ran after callback");
        },{},uint32_t(observer.tick),[&](int,uint32_t){return observer.tick>=14;});
        if(observer.tick<14)require(!service.requests()&&observer.deliveries.empty(),"queued wait counted as delivery");
    }
    require(callbackTick>=14&&observer.deliveries==std::vector<double>{double(callbackTick-3)},
            "request-to-delivery latency omitted pre-admission wait");
    require(observer.successful.size()==1&&observer.failed.empty(),"success classified as failure");
    request(service);request(service,1,19);request(service,1,18,2);
    require(observer.cancellations[1]==2,"goal/controller replacements not recorded separately");
    service.cancel(1);require(observer.cancellations[0]==1,"explicit cancellation lost");
    request(service,1);request(service,2);service.clear();
    require(observer.cancellations[3]==2&&observer.pending.empty()&&!observer.unknown,"clear orphaned requests");
    service.setTelemetry(&other);request(service);service.cancel(1);
    require(other.sequence==1&&other.pending.empty()&&!other.unknown,"reattached observer inherited old tokens");
}
void failedDelivery() {
    LatencyObserver observer;PathService service;service.setTelemetry(&observer);request(service);
    bool callbackFailed=true;
    for(observer.tick=1;observer.tick<100&&service.pending(1);++observer.tick)
        service.tick([](int,int x,int z){return x==2&&z==2?6:0;},
            [&](int,const std::vector<PathCell>& route,Fixed,Fixed,bool partial,bool,bool,bool) {
                require(route.empty(),"boxed fixture produced route");callbackFailed=partial;
            });
    require(!callbackFailed&&observer.failed.size()==1&&observer.empty==1&&observer.successful.empty(),
            "empty failure was mistaken for success because the partial-route bit was clear");
    // The collector keeps successful, failed partial, and empty outcomes
    // independent. A partial callback still has a measurable delivery latency.
    observer.tick=101;const auto token=observer.requested(2);observer.tick=107;
    observer.delivered(token,true,4);
    require(observer.failed.back()==6&&observer.empty==1,"partial route lost its actual delivery latency");
}
void callbackObserverGuard() {
    LatencyObserver observer,other;PathService service;service.setTelemetry(&observer);
    service.setBudget(1000000);request(service,1);request(service,2);
    unsigned callbacks=0;
    service.tick(score,[&](int,const std::vector<PathCell>&,Fixed,Fixed,bool,bool,bool,bool) {
        ++callbacks;
        require(!service.pendingCount(),"observer guard fixture did not drain requests before delivery");
        service.setTelemetry(&observer); // Keeping the same owner remains valid.
        for(auto* replacement:std::array<NavigationTelemetry*,2>{&other,nullptr}) {
            bool rejected=false;
            try{service.setTelemetry(replacement);}catch(const std::logic_error&){rejected=true;}
            require(rejected,"callback rebound observer while finished tokens were being delivered");
        }
    });
    require(callbacks==2&&observer.deliveries.size()==2&&observer.pending.empty()&&
            !observer.unknown&&!other.sequence&&!other.unknown,"callback rebind lost token ownership");
    service.setTelemetry(&other);request(service);
    struct CallbackFailure {};
    bool threw=false;
    try {
        service.tick(score,[](int,const std::vector<PathCell>&,Fixed,Fixed,bool,bool,bool,bool) {
            throw CallbackFailure{};
        });
    }catch(const CallbackFailure&){threw=true;}
    require(threw&&!service.pendingCount()&&other.pending.empty(),"callback exception fixture did not deliver");
    service.setTelemetry(nullptr); // The guard must unwind after callback failure.
}
using Snapshot=std::tuple<uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t>;
std::vector<Snapshot> worldRun(PathfindingMode selected,bool enabled,bool serial) {
    LatencyObserver observer;World world;setup(world,selected,serial,false,128);
    auto type=mover();std::vector<int> ids;
    for(int i=0;i<8;++i)ids.push_back(world.spawn(&type,float((12+i%2*4)*16),float((24+i/2*4)*16)));
    known(world);if(enabled)RetailReplayProbe::observe(world,&observer);
    for(int id:ids)world.order(id,90*16,48*16,false);
    std::vector<Snapshot> state;
    for(int tick=1;tick<=180;++tick) {
        observer.tick=uint64_t(tick);
        if(tick==40)for(int id:ids)world.order(id,70*16,64*16,false);
        if(tick==80)world.stop(ids.front());
        world.tick(1.f/30);
        const auto f=world.flowStats();const auto& p=world.pathStats();
        state.emplace_back(world.stateHash(),p.requests(),p.completions(),p.workSpent(),f.requests,f.deliveries);
    }
    if(enabled) {
        require(observer.sequence>0&&!observer.deliveries.empty()&&!observer.unknown,"World telemetry missed real events");
        uint64_t retired=observer.deliveries.size()+observer.pending.size();
        for(uint64_t count:observer.cancellations)retired+=count;
        require(retired==observer.sequence,"request lifecycle does not balance");
    }
    return state;
}
void sharedLifecycle(PathfindingMode selected) {
    LatencyObserver observer,other;World world;setup(world,selected,true,false,128);
    auto type=mover();const int id=world.spawn(&type,24*16,24*16);
    known(world);RetailReplayProbe::observe(world,&observer);
    world.order(id,96*16,96*16,false);auto& nav=RetailReplayProbe::flow(world);
    auto& unit=*world.unit(id);
    require(nav.request(unit,Fixed::fromInt(96*16),Fixed::fromInt(96*16)),"shared request refused");
    const auto count=observer.sequence;
    nav.request(unit,Fixed::fromInt(96*16),Fixed::fromInt(96*16));
    require(observer.sequence==count,"shared duplicate request restarted latency");
    bool rejected=false;
    try{nav.setTelemetry(&other);}catch(const std::logic_error&){rejected=true;}
    require(rejected,"shared observer detached outstanding request");
    unit.orders.clear();observer.tick=1;nav.tick();
    require(observer.cancellations[2]==1&&observer.pending.empty(),"stale shared request counted as delivery");
    // Exercise deterministic admission capacity without spending a map search
    // quantum or requiring 16K physical bodies. Invalid IDs are removed later
    // by normal stale-result validation; they occupy real request records here.
    for(size_t i=0;i<flow::MemoryPlan::maxRequests;++i) {
        unit.id=int(i+100);
        require(nav.request(unit,Fixed::fromInt(96*16),Fixed::fromInt(96*16)),"capacity rejected too early");
    }
    const auto atCapacity=observer.sequence;unit.id=20000;
    require(!nav.request(unit,Fixed::fromInt(96*16),Fixed::fromInt(96*16))&&observer.sequence==atCapacity,
            "capacity refusal invented an admitted latency request");
    unit.id=id;
    for(size_t i=0;i<flow::MemoryPlan::maxRequests;++i)nav.cancel(int(i+100));
    require(observer.pending.empty()&&!observer.unknown,"capacity cancellation orphaned observer tokens");
}
}
int main() {try {
    queueLifecycle();failedDelivery();callbackObserverGuard();
    for(const auto selected:{PathfindingMode::Retail,PathfindingMode::RetailPlus,
                             PathfindingMode::Flowfield,PathfindingMode::Cooperative}) {
        const auto reference=worldRun(selected,false,true);
        require(reference==worldRun(selected,true,true),"telemetry changed per-tick serial simulation");
        require(reference==worldRun(selected,true,false),"telemetry or worker execution changed simulation");
    }
    sharedLifecycle(PathfindingMode::Flowfield);sharedLifecycle(PathfindingMode::Cooperative);
    std::puts("navigation telemetry passed");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"navigation telemetry: %s\n",error.what());return 1;}}
