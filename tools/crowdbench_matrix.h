#pragma once

// Parameterized, asset-free comparisons. Observation, traces, and scenario
// mutations are outside the timed World::tick. Include in crowdbench.cpp only.
#include "sim/sim.h"
#include "crowdbench_telemetry.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#if defined(_WIN32)
#include <malloc.h>
#endif
#if defined(__unix__)
#include <sys/resource.h>
#endif

namespace crowdbench_allocation {
inline std::atomic<bool> enabled{false};
inline std::atomic<uint64_t> calls{0}, bytes{0};
inline void count(size_t n) {
    if (enabled.load(std::memory_order_relaxed)) {
        calls.fetch_add(1,std::memory_order_relaxed);
        bytes.fetch_add(n,std::memory_order_relaxed);
    }
}
inline void* allocate(size_t n) {
    if (void* p=std::malloc(std::max(size_t(1),n))) {count(n);return p;}
    throw std::bad_alloc();
}
inline void* aligned(size_t n,size_t alignment) {
    if (n>std::numeric_limits<size_t>::max()-alignment) throw std::bad_alloc();
#if defined(_WIN32)
    void* p=_aligned_malloc(std::max(size_t(1),n),alignment);
#else
    const size_t rounded=(std::max(size_t(1),n)+alignment-1)/alignment*alignment;
    void* p=std::aligned_alloc(alignment,rounded);
#endif
    if(p) {count(n);return p;}
    throw std::bad_alloc();
}
inline void alignedFree(void* p) noexcept {
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
}
void* operator new(size_t n) {return crowdbench_allocation::allocate(n);}
void* operator new[](size_t n) {return crowdbench_allocation::allocate(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
void operator delete[](void* p,size_t) noexcept {std::free(p);}
void* operator new(size_t n,std::align_val_t a) {return crowdbench_allocation::aligned(n,size_t(a));}
void* operator new[](size_t n,std::align_val_t a) {return crowdbench_allocation::aligned(n,size_t(a));}
void operator delete(void* p,std::align_val_t) noexcept {crowdbench_allocation::alignedFree(p);}
void operator delete[](void* p,std::align_val_t) noexcept {crowdbench_allocation::alignedFree(p);}
void operator delete(void* p,size_t,std::align_val_t) noexcept {crowdbench_allocation::alignedFree(p);}
void operator delete[](void* p,size_t,std::align_val_t) noexcept {crowdbench_allocation::alignedFree(p);}

namespace tak::sim {
// Existing diagnostic friendship: observe pending ownership without changing it.
struct RetailReplayProbe {
    static bool pending(const World& world,int id) {return world.pathPending(id);}
    static void telemetry(World& world,NavigationTelemetry* observer) {
        world.paths_.setTelemetry(observer);
        if(isSharedPathfinding(world.pathfindingMode_)) {
            if(!world.flow_)world.flow_=std::make_unique<FlowNavigator>(world);
            world.flow_->setTelemetry(observer);
        }
    }
};
}
namespace crowdbench_matrix {
using namespace tak::sim;
using Clock=std::chrono::steady_clock;
constexpr int restTicks=30;
struct Options {
    std::string mode="retail",scenario="open",trace;
    int units=200,players=1,movingPercent=100,ticks=3600;
    uint32_t seed=0;
    bool workers=false,allocations=false,profile=false,latency=false;
};
inline int integer(const char* value) {
    size_t consumed=0;const int result=std::stoi(value,&consumed);
    if(consumed!=std::string_view(value).size())throw std::runtime_error("invalid integer");
    return result;
}
inline Options parse(int argc,char** argv) {
    Options o;
    for(int i=1;i<argc;++i) {
        const std::string_view key=argv[i];
        if(key=="--workers") {o.workers=true;continue;}
        if(key=="--allocations") {o.allocations=true;continue;}
        if(key=="--profile") {o.profile=true;continue;}
        if(key=="--latency") {o.latency=true;continue;}
        if(i+1==argc)throw std::runtime_error("option needs a value");
        const char* value=argv[++i];
        if(key=="--mode")o.mode=value;
        else if(key=="--scenario")o.scenario=value;
        else if(key=="--units")o.units=integer(value);
        else if(key=="--players")o.players=integer(value);
        else if(key=="--moving-percent")o.movingPercent=integer(value);
        else if(key=="--ticks")o.ticks=integer(value);
        else if(key=="--trace")o.trace=value;
        else if(key=="--seed") {
            size_t count=0;const std::string seed=value;
            const auto parsed=std::stoull(seed,&count);
            if(seed.empty()||seed.front()=='-'||count!=seed.size()||parsed>UINT32_MAX)
                throw std::runtime_error("seed must be an unsigned 32-bit integer");
            o.seed=uint32_t(parsed);
        }
        else throw std::runtime_error("unknown option: "+std::string(key));
    }
    if(o.mode!="retail"&&o.mode!="retail-plus"&&o.mode!="flowfield"&&o.mode!="cooperative"&&o.mode!="legion")
        throw std::runtime_error("mode must be retail, retail-plus, flowfield, cooperative, or legion");
#ifdef TAK_CROWDBENCH_BASELINE
    if(o.mode=="retail-plus")throw std::runtime_error("Retail+ is unavailable in the frozen baseline");
#endif
    constexpr std::array names{"open","doors","bridges","maze","opposingcolumns","sharedgoal",
        "mixedfootprints","exploration","dynamicobstacle","rapidreplacement","unreachable","recovery","recovery-passive"};
    if(std::find(names.begin(),names.end(),o.scenario)==names.end())throw std::runtime_error("unknown scenario");
    if(o.units<1||o.units>2000||o.players<1||o.players>8||o.movingPercent<0||o.movingPercent>100||o.ticks<1)
        throw std::runtime_error("units: 1..2000 per player; players: 1..8; moving-percent: 0..100; ticks: positive");
    return o;
}
inline UnitType mover(int footprint,bool exploring) {
    UnitType type{};type.id=type.name="benchmark-foot-"+std::to_string(footprint);
    type.canMove=true;type.maxHp=100;type.footX=type.footZ=footprint;
    type.sight=exploring?192:4096;type.maxVel=Fixed::raw(117964);
    type.accel=type.brake=Fixed::fromInt(10);type.turnRate=type.turnInPlaceRate=2500;
    type.halfCellTicks=3;type.buildTime=1;return type;
}
struct Member {
    int id=0,player=0,epoch=0,commandTick=0,firstMove=-1,firstAdmission=-1,firstRoute=-1,firstPendingClear=-1;
    int rest=0,arrivedTick=-1,retiredTick=-1,crossedTick=-1,stamp=-1;
    int initialX=0,initialZ=0,previousX=0,previousZ=0;
    float gx=0,gz=0,alternateX=0,alternateZ=0,radius=32;
    double path=0,straight=0;uint64_t stalled=0;
    bool moving=false,reverse=false,near=false,legal=true,pending=false;
    uint64_t traceControl=~uint64_t(0);
};
struct Rect {int x,z,w,h;};
inline int round64(int n) {return (n+63)/64*64;}
inline double millis(Clock::time_point begin,Clock::time_point end) {
    return std::chrono::duration<double,std::milli>(end-begin).count();
}
inline double percentile(std::vector<double> values,double fraction) {
    if(values.empty())return -1;
    std::sort(values.begin(),values.end());
    return values[size_t(std::ceil(fraction*values.size()))-1];
}
inline int milestone(std::vector<int> times,int count,double fraction) {
    if(!count)return 0;
    std::sort(times.begin(),times.end());
    const size_t index=size_t(std::ceil(fraction*count))-1;
    return index<times.size()?times[index]:-1;
}
inline uint64_t mix(uint64_t h,uint64_t v) {return (h^v)*1099511628211ull;}
inline uint64_t control(const Unit& u) {
    uint64_t h=mix(1469598103934665603ull,u.orders.size());h=mix(h,uint32_t(u.routeStamp));
    for(const auto& order:u.orders) {
        h=mix(h,uint32_t(order.x.v));h=mix(h,uint32_t(order.z.v));h=mix(h,order.goal);
        h=mix(h,order.controller);h=mix(h,order.hasSegment);
        h=mix(h,order.navigationConsumed);h=mix(h,order.navigationExhausted);
        h=mix(h,uint32_t(order.segmentX.v));h=mix(h,uint32_t(order.segmentZ.v));
    }
    return h;
}
inline void known(World& world) {
    world.updateNavigationExploration();
    auto& cells=const_cast<std::vector<uint16_t>&>(world.navigationExploration());
    std::fill(cells.begin(),cells.end(),0xffff);
}
template<class W> void profiling(W& world,bool enabled) {
    if constexpr(requires {world.setPathProfiling(enabled);world.resetPathDiagnostics();}) {
        world.setPathProfiling(enabled);world.resetPathDiagnostics();
    } else if(enabled)throw std::runtime_error("path profiling is unavailable in this build");
}
template<class W> size_t retailPlusBytes(const W& world) {
    if constexpr(requires {world.retailPlusStats().bytes;})return world.retailPlusStats().bytes;
    else return 0;
}
// Build identity carried in the result itself, so a debug or unoptimized
// executable cannot silently stand in for a Release benchmark binary.
inline void printBuild() {
#if defined(__clang__)
    const char* compiler="clang " __clang_version__;
#elif defined(__GNUC__)
    const char* compiler="gcc " __VERSION__;
#elif defined(_MSC_VER)
    const char* compiler="msvc";
#else
    const char* compiler="unknown";
#endif
#if defined(NDEBUG)
    const bool ndebug=true;
#else
    const bool ndebug=false;
#endif
#if defined(__OPTIMIZE__)
    const bool optimized=true;
#else
    const bool optimized=false;
#endif
    std::printf("\"build_compiler\":\"%s\",\"build_ndebug\":%s,\"build_optimized\":%s,",compiler,
        ndebug?"true":"false",optimized?"true":"false");
}
template<class W> void printDiagnostics(const W& world,bool enabled) {
    auto metric=[](const char* name,uint64_t value) {
        std::printf("\"%s\":%llu,",name,(unsigned long long)value);
    };
    if constexpr(requires {world.pathDiagnostics();}) {
        const auto d=world.pathDiagnostics();
        std::printf("\"path_profiling\":%s,\"search_initializations\":%llu,\"search_execution_slices\":%llu,\"search_reset_cells\":%llu,\"search_initialization_ns\":%llu,\"search_preparation_ns\":%llu,\"search_execution_ns\":%llu,\"search_scratch_bytes\":%zu,\"body_snapshot_rebuilds\":%llu,\"body_snapshot_bytes\":%zu,\"grade_plane_bytes\":%zu,",
            enabled?"true":"false",(unsigned long long)d.initializations,(unsigned long long)d.executionSlices,
            (unsigned long long)d.resetCells,(unsigned long long)d.initializationNanoseconds,
            (unsigned long long)d.preparationNanoseconds,(unsigned long long)d.executionNanoseconds,d.scratchBytes,
            (unsigned long long)d.bodySnapshotRebuilds,d.bodySnapshotBytes,d.gradePlaneBytes);
    } else std::printf("\"path_profiling\":false,\"search_scratch_bytes\":null,");
    if constexpr(requires {world.retailPlusStats();}) {
        const auto s=world.retailPlusStats();
        std::printf("\"retail_plus_bytes\":%zu,\"retail_plus_proof_cells\":%llu,",s.bytes,(unsigned long long)s.proofCells);
        if constexpr(requires {s.bodyEntries;s.bodyDeferrals;})
            std::printf("\"retail_plus_body_entries\":%llu,\"retail_plus_body_deferrals\":%llu,",(unsigned long long)s.bodyEntries,(unsigned long long)s.bodyDeferrals);
        if constexpr(requires {s.traffic.searches;s.traffic.routes;s.traffic.waits;})
            std::printf("\"retail_plus_searches\":%llu,\"retail_plus_routes\":%llu,\"retail_plus_waits\":%llu,",(unsigned long long)s.traffic.searches,(unsigned long long)s.traffic.routes,(unsigned long long)s.traffic.waits);
        if constexpr(requires {s.traffic.deferredSearches;s.traffic.retrySkips;}) {
            metric("retail_plus_deferred_searches",s.traffic.deferredSearches);
            metric("retail_plus_retry_skips",s.traffic.retrySkips);
            metric("retail_plus_complete_failures",s.traffic.completeFailures);
            metric("retail_plus_probes",s.traffic.probes);
            metric("retail_plus_conflicts",s.traffic.conflicts);
        }
        if constexpr(requires {s.contextNanoseconds;s.fastUpdates;}) {
            metric("retail_plus_context_ns",s.contextNanoseconds);
            metric("retail_plus_setup_ns",s.setupNanoseconds);
            metric("retail_plus_policy_ns",s.policyNanoseconds);
            metric("retail_plus_maintenance_ns",s.maintenanceNanoseconds);
            metric("retail_plus_fast_updates",s.fastUpdates);
            metric("retail_plus_full_updates",s.fullUpdates);
        }
    }
    if constexpr(requires {world.legionStats();}) {
        const auto l=world.legionStats();
        metric("legion_plane_builds",l.planeBuilds);metric("legion_field_work",l.fieldWork);
        metric("legion_fields_built",l.fieldsBuilt);metric("legion_field_evictions",l.fieldEvictions);
        metric("legion_groups",l.groups);metric("legion_registrations",l.registrations);
        metric("legion_holds",l.holds);metric("legion_slides",l.slides);metric("legion_arrivals",l.arrivals);
        metric("legion_contact_arrivals",l.contactArrivals);metric("legion_trapped",l.trapped);
        metric("legion_escapes",l.escapes);metric("legion_bytes",l.bytes);
    }
    const auto f=world.flowStats();
    metric("flow_snapshot_work",f.snapshotWork);metric("flow_field_work",f.fieldWork);
    metric("flow_local_work",f.localWork);metric("flow_local_deliveries",f.localDeliveries);
    metric("flow_failures",f.failures);metric("flow_profiles",f.profiles);metric("flow_fields",f.fields);
    metric("flow_profile_evictions",f.profileEvictions);metric("flow_snapshot_failures",f.snapshotFailures);
    metric("flow_prepared_tiles",f.preparedTiles);metric("flow_tile_cache_hits",f.tileCacheHits);
    metric("flow_tile_cache_evictions",f.tileCacheEvictions);metric("flow_cached_tiles",f.cachedTiles);
    metric("flow_arrival_cells",f.arrivalCells);
    metric("cooperative_probes",f.cooperativeProbes);metric("cooperative_searches",f.cooperativeSearches);
    metric("cooperative_routes",f.cooperativeRoutes);metric("cooperative_waits",f.cooperativeWaits);
    metric("cooperative_conflicts",f.cooperativeConflicts);
    metric("cooperative_complete_failures",f.cooperativeCompleteFailures);
    metric("cooperative_deferred_searches",f.cooperativeDeferredSearches);
    metric("cooperative_retry_skips",f.cooperativeRetrySkips);
    metric("cooperative_records",f.cooperativeRecords);metric("cooperative_reservations",f.cooperativeReservations);
    metric("cooperative_bytes",f.cooperativeBytes);metric("cooperative_passage_probes",f.cooperativePassageProbes);
    metric("cooperative_passage_hits",f.cooperativePassageHits);
    metric("cooperative_passage_screening_cells",f.cooperativePassageScreeningCells);
    metric("cooperative_clearance_hits",f.cooperativeClearanceHits);
    metric("cooperative_clearance_rebuilds",f.cooperativeClearanceRebuilds);
    if constexpr(requires {world.cooperativeMovementStats();}) {
        const auto m=world.cooperativeMovementStats();
        metric("cooperative_movement_queued",m.queued);metric("cooperative_movement_attempted",m.attempted);
        metric("cooperative_movement_moved",m.moved);metric("cooperative_movement_blocked",m.blocked);
        metric("cooperative_movement_cycle",m.cycle);metric("cooperative_movement_deferred",m.deferred);
        metric("cooperative_movement_invalid",m.invalid);metric("cooperative_movement_probes",m.probes);
    }
}
inline void barriers(World& world,int width,int height,const std::vector<Rect>& previous,
                     const std::vector<Rect>& next) {
    for(const auto& r:previous)world.blockCells(r.x,r.z,r.w,r.h,false);
    std::vector<uint16_t> cells(size_t(width)*height,0xffff);
    for(const auto& r:next) {
        world.blockCells(r.x,r.z,r.w,r.h,true);
        for(int z=r.z;z<r.z+r.h;++z)for(int x=r.x;x<r.x+r.w;++x)
            cells[size_t(z)*width+x]=0;
    }
    world.setMapPlacementFeatures(cells,{{"benchmark-wall",1,1,true,true,false,0}});
}
inline int run(const Options& o) {
    const auto setupStart=Clock::now();
    const bool mixed=o.scenario=="mixedfootprints",shared=mixed||o.scenario=="sharedgoal";
    const bool opposing=o.scenario=="opposingcolumns",exploring=o.scenario=="exploration";
    const int stride=mixed?5:3,rows=int(std::ceil(std::sqrt(double(o.units))));
    const int columns=(o.units+rows-1)/rows,laneHeight=rows*stride+16;
    const int width=round64(2*columns*stride+320),height=round64(o.players*laneHeight+64);
    const int middle=width/2,left=32,right=width-32-columns*stride;
    const int movingPerPlayer=o.units*o.movingPercent/100,totalMoving=movingPerPlayer*o.players;
    LatencyObserver latency;
    World world;world.setGameSeed(o.seed);world.setVisPlayer(-1);world.setSerialThreads(!o.workers);world.setPathService(true);
    world.setPathfindingMode(PathfindingMode(o.mode=="retail"?0:o.mode=="flowfield"?1:o.mode=="cooperative"?2:o.mode=="legion"?4:3));
    world.setPlayerCount(o.players);for(int p=0;p<o.players;++p)world.setTeam(p,0);
    world.setTerrain(std::vector<uint8_t>(size_t(width)*height,100),width,height,64);
    std::vector<Rect> walls;
    const bool lanes=o.scenario=="doors"||o.scenario=="bridges"||o.scenario=="maze"||exploring;
    if(lanes)for(int p=0;p<=o.players;++p)walls.push_back({0,24+p*laneHeight,width,2});
    for(int p=0;p<o.players;++p) {
        const int low=26+p*laneHeight,high=24+(p+1)*laneHeight,center=(low+high)/2;
        if(o.scenario=="doors"||o.scenario=="bridges") {
            const int thickness=o.scenario=="doors"?4:64;
            walls.push_back({middle-thickness/2,low,thickness,center-3-low});
            walls.push_back({middle-thickness/2,center+3,thickness,high-center-3});
        }
        if(o.scenario=="maze"||exploring)for(int b=0;b<4;++b) {
            const int x=left+columns*stride+24+b*32;
            walls.push_back({x,low+(b%2?12:0),4,high-low-12});
        }
    }
    if(o.scenario=="unreachable"||o.scenario=="recovery"||o.scenario=="recovery-passive")walls.push_back({middle-2,0,4,height});
    barriers(world,width,height,{},walls);
    std::array<UnitType,3> types{mover(2,exploring),mover(3,exploring),mover(4,exploring)};
    std::vector<Member> members;members.reserve(size_t(o.units)*o.players);
    for(int p=0;p<o.players;++p)for(int k=0;k<o.units;++k) {
        const int slot=opposing?k/2:k;
        const int col=slot/rows,row=slot%rows,baseZ=36+p*laneHeight;
        // Movers occupy the front columns; inactive bodies do not form an
        // artificial wall between the commanded crowd and its destination.
        const int slotX=(columns-1-col)*stride,slotZ=row*stride;
        Member member;member.player=p;member.moving=k<movingPerPlayer;
        member.reverse=opposing&&(k%2==1);
        const int x=(member.reverse?right:left)+slotX,z=baseZ+slotZ;
        member.gx=float(((member.reverse?left:right)+slotX)*16);member.gz=float(z*16);
        member.alternateX=float((left+slotX)*16);member.alternateZ=float(z*16);
        if(shared) {
            member.gx=float((right+columns*stride/2)*16);
            member.gz=float((baseZ+rows*stride/2)*16);
            // Fixed destination geometry, authored before running any mode.
            // Each settled footprint gets its area plus one cell of clearance.
            const int body=mixed?64:32;
            member.radius=float(std::ceil(std::sqrt(double(movingPerPlayer)*body*body/3.141592653589793))+2*stride*16);
        }
        const UnitType& type=types[size_t(mixed?k%3:0)];
        member.id=world.spawn(&type,float(x*16),float(z*16),std::nullopt,p);
        if(member.id<=0)throw std::runtime_error("failed to spawn requested population");
        const auto& u=*world.unit(member.id);member.initialX=member.previousX=u.x.v;
        member.initialZ=member.previousZ=u.z.v;members.push_back(member);
    }
    // Build spatial indices once before commands; that setup tick is untimed.
    world.tick(1.f/30);if(!exploring)known(world);
    for(const auto& m:members) {
        const auto& u=*world.unit(m.id);
        if(!world.mobilePlacement(u,footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ),false))
            throw std::runtime_error("invalid initial footprint for unit "+std::to_string(m.id));
    }
    auto command=[&](int tick,bool alternate) {
        for(auto& m:members)if(m.moving) {
            if(alternate) {std::swap(m.gx,m.alternateX);std::swap(m.gz,m.alternateZ);}
            world.order(m.id,m.gx,m.gz,false);const auto& u=*world.unit(m.id);
            m.commandTick=tick;++m.epoch;m.firstMove=m.firstAdmission=m.firstRoute=-1;
            m.firstPendingClear=-1;m.pending=RetailReplayProbe::pending(world,m.id);
            m.rest=0;m.arrivedTick=m.retiredTick=-1;m.stamp=u.routeStamp;
            m.initialX=u.x.v;m.initialZ=u.z.v;
            if(m.epoch==1)m.straight=std::hypot(double(u.x.toFloat()-m.gx),double(u.z.toFloat()-m.gz));
        }
    };
    if(o.latency)RetailReplayProbe::telemetry(world,&latency);
    // The observer's own map/vector growth is harness work, not simulation
    // allocation. Pausing the global switch is only exact without workers.
    if(o.allocations&&!o.workers)latency.quiet=&crowdbench_allocation::enabled;
    command(0,false);
    profiling(world,o.profile);
    std::ofstream trace,unitTrace,wallTrace;
    // Plotting sidecar: map extent, then every published barrier set in cells.
    auto traceWalls=[&](int tick) {
        if(!wallTrace.is_open())return;
        wallTrace<<tick<<",set,0,0,0,0\n";
        for(const auto& r:walls)wallTrace<<tick<<",wall,"<<r.x<<','<<r.z<<','<<r.w<<','<<r.h<<'\n';
    };
    if(!o.trace.empty()) {
        trace.open(o.trace);unitTrace.open(o.trace+".units.csv");wallTrace.open(o.trace+".walls.csv");
        if(!trace||!unitTrace||!wallTrace)throw std::runtime_error("cannot create trace output");
        wallTrace<<"tick,kind,x,z,w,h\n0,map,0,0,"<<width<<','<<height<<'\n';
        traceWalls(0);
        trace<<"tick,world_tick,hash,retail_requests,retail_completions,retail_failures,retail_work,retail_pending,flow_requests,flow_deliveries,flow_pending\n";
        unitTrace<<"tick,id,epoch,x_raw,z_raw,speed_raw,orders,route_stamp,control_hash,front_x_raw,front_z_raw,controller,segment,exhausted,consumed,goal_x,goal_z,goal_radius,foot_x,foot_z,pending,heading_bam,turn_request_bam,blocked,route_failed,mission_events,leg_controller,leg_x_raw,leg_z_raw,leg_exhausted,leg_consumed,mission_stage,mission_pending,mission_wait_mask,mission_radius,mission_target_x_raw,mission_target_z_raw,next_x_raw,next_z_raw\n";
    }
    const double setupMs=millis(setupStart,Clock::now());
    std::vector<double> times;times.reserve(size_t(o.ticks));
    uint64_t illegalSamples=0,peakPending=0;size_t navigationBytes=0,retailPlusPeakBytes=0;
    double eventMs=0;int allAt=-1,firstCross=-1,lastCross=-1;
    auto observeTrace=[&](int tick) {
        if(!trace.is_open())return;
        const auto flow=world.flowStats();const auto& retail=world.pathStats();
        trace<<tick<<','<<world.tickCount()<<','<<std::hex<<world.stateHash()<<std::dec<<','
            <<retail.requests()<<','<<retail.completions()<<','<<retail.failures()<<','<<retail.workSpent()<<','
            <<retail.pendingCount()<<','<<flow.requests<<','<<flow.deliveries<<','<<flow.pending<<'\n';
        for(auto& m:members) {
            const auto& u=*world.unit(m.id);const uint64_t hash=mix(control(u),m.pending);
            if(hash==m.traceControl&&tick%30!=0)continue;
            m.traceControl=hash;unitTrace<<tick<<','<<m.id<<','<<m.epoch<<','<<u.x.v<<','<<u.z.v<<','<<u.speed.v
                <<','<<u.orders.size()<<','<<u.routeStamp<<','<<std::hex<<hash<<std::dec;
            if(u.orders.empty())unitTrace<<",0,0,0,0,0,0";
            else {const auto& q=u.orders.front();unitTrace<<','<<q.x.v<<','<<q.z.v<<','<<q.controller<<','
                <<q.hasSegment<<','<<q.navigationExhausted<<','<<q.navigationConsumed;}
            unitTrace<<','<<m.gx<<','<<m.gz<<','<<m.radius<<','<<u.type->footX<<','<<u.type->footZ<<','<<m.pending
                <<','<<u.heading.v<<','<<u.turnReqBam<<','<<u.bodyBlockStreak<<','<<u.routeFailed<<','<<u.missionEvents;
            if(u.orders.empty())unitTrace<<",0,0,0,0,0,0,0,0,0,0,0";
            else {const auto& q=u.orders[World::currentLeg(u.orders)];
                unitTrace<<','<<q.controller<<','<<q.x.v<<','<<q.z.v<<','<<q.navigationExhausted<<','<<q.navigationConsumed
                    <<','<<int(q.mission.stage)<<','<<q.mission.pending<<','<<q.mission.waitMask<<','<<q.missionRadius
                    <<','<<(q.missionTarget?q.missionTarget->first.v:0)<<','<<(q.missionTarget?q.missionTarget->second.v:0);}
            if(u.orders.size()>1)unitTrace<<','<<u.orders[1].x.v<<','<<u.orders[1].z.v;
            else unitTrace<<",0,0";
            unitTrace<<'\n';
        }
    };
    observeTrace(0);
    for(int tick=1;tick<=o.ticks;++tick) {
        latency.tick=uint64_t(tick);
        const auto eventStart=Clock::now();
        if(o.scenario=="rapidreplacement"&&tick%120==0&&tick<=o.ticks/2)command(tick,true);
        if((o.scenario=="recovery"||o.scenario=="recovery-passive")&&tick==std::max(1,o.ticks/3)) {
            barriers(world,width,height,walls,{});walls.clear();traceWalls(tick);
            if(o.scenario=="recovery")command(tick,false);
        }
        if(o.scenario=="dynamicobstacle"&&(tick==std::min(30,std::max(1,o.ticks/3))||tick==std::max(2,o.ticks*2/3))) {
            std::vector<Rect> next;
            if(tick<o.ticks*2/3)for(int p=0;p<o.players;++p) {
                // Publish within 30 ticks, before even the fastest mover can
                // reach the wall. The geometry and event times are independent
                // of the selected navigator or its intermediate positions.
                const int z=36+p*laneHeight+rows*stride/3;
                next.push_back({middle-2,z,4,std::max(1,rows*stride/3)});
            }
            barriers(world,width,height,walls,next);walls=std::move(next);traceWalls(tick);
        }
        eventMs+=millis(eventStart,Clock::now());
        crowdbench_allocation::enabled.store(o.allocations,std::memory_order_relaxed);
        const auto begin=Clock::now();world.tick(1.f/30);const auto end=Clock::now();
        crowdbench_allocation::enabled.store(false,std::memory_order_relaxed);
        times.push_back(millis(begin,end));
        const auto flow=world.flowStats();const auto& retail=world.pathStats();
        navigationBytes=std::max(navigationBytes,flow.bytes);
        // Shared modes can still queue native searches for unsupported
        // movers, and Retail modes report no flow requests: count both.
        peakPending=std::max(peakPending,uint64_t(flow.pending)+uint64_t(retail.pendingCount()));
        if(tick%60==0||tick==o.ticks)retailPlusPeakBytes=std::max(retailPlusPeakBytes,retailPlusBytes(world));
        int arrived=0;
        for(auto& m:members) {
            const auto& u=*world.unit(m.id);
            bool checkedPlacement=false;
            const bool moved=u.x.v!=m.previousX||u.z.v!=m.previousZ;
            const double dx=double(int64_t(u.x.v)-m.previousX)/65536,dz=double(int64_t(u.z.v)-m.previousZ)/65536;
            if(m.moving) {
                m.path+=std::hypot(dx,dz);
                if(moved&&m.firstMove<0)m.firstMove=tick-m.commandTick;
                if(u.routeStamp!=m.stamp&&m.firstAdmission<0)m.firstAdmission=tick-m.commandTick;
                if(m.firstRoute<0&&std::any_of(u.orders.begin(),u.orders.end(),[](const Order& q){return q.hasSegment;}))
                    m.firstRoute=tick-m.commandTick;
                const bool pending=RetailReplayProbe::pending(world,m.id);
                if(m.pending&&!pending&&m.firstPendingClear<0)m.firstPendingClear=tick-m.commandTick;
                m.pending=pending;
                if(!moved&&!u.orders.empty())++m.stalled;
                m.near=std::hypot(double(u.x.toFloat()-m.gx),double(u.z.toFloat()-m.gz))<=m.radius;
                if(u.orders.empty()&&m.retiredTick<0)m.retiredTick=tick;
                if(!u.orders.empty())m.retiredTick=-1;
                if(u.alive()&&u.orders.empty()&&u.speed.v==0&&m.near&&!moved) {
                    m.legal=world.mobilePlacement(u,footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ),false);
                    checkedPlacement=true;illegalSamples+=!m.legal;
                    if(m.legal)++m.rest;else m.rest=0;
                } else m.rest=0;
                if(m.rest>=restTicks) {if(m.arrivedTick<0)m.arrivedTick=tick; ++arrived;}else m.arrivedTick=-1;
                if(m.crossedTick<0&&(m.reverse?u.x.toFloat()<middle*16-32:u.x.toFloat()>middle*16+32)) {
                    m.crossedTick=tick;if(firstCross<0)firstCross=tick;lastCross=tick;
                }
            }
            if(!checkedPlacement&&(tick%60==0||tick==o.ticks)) {
                m.legal=u.alive()&&world.mobilePlacement(u,footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ),false);
                illegalSamples+=!m.legal;
            }
            m.previousX=u.x.v;m.previousZ=u.z.v;
        }
        if(arrived==totalMoving&&allAt<0)allAt=tick;
        if(arrived!=totalMoving)allAt=-1;
        observeTrace(tick);
    }
    int arrived=0,physical=0,retired=0,crossed=0,illegalFinal=0,alive=0,finalCommand=0;
    uint64_t stalled=0;double totalPath=0,totalStraight=0,maxDistance=0;
    std::vector<int> arrivalTimes,firstMoves,firstAdmissions,firstRoutes,firstPendingClears;
    for(const auto& m:members)if(m.moving) {
        const auto& u=*world.unit(m.id);alive+=u.alive();retired+=u.orders.empty();
        finalCommand=std::max(finalCommand,m.commandTick);
        physical+=m.near&&m.legal;illegalFinal+=!m.legal;crossed+=m.crossedTick>=0;
        const bool completed=m.rest>=restTicks&&m.legal;arrived+=completed;
        if(completed)arrivalTimes.push_back(m.arrivedTick);
        if(m.firstMove>=0)firstMoves.push_back(m.firstMove);
        if(m.firstAdmission>=0)firstAdmissions.push_back(m.firstAdmission);
        if(m.firstRoute>=0)firstRoutes.push_back(m.firstRoute);
        if(m.firstPendingClear>=0)firstPendingClears.push_back(m.firstPendingClear);
        totalPath+=m.path;totalStraight+=m.straight;stalled+=m.stalled;
        maxDistance=std::max(maxDistance,std::hypot(double(u.x.toFloat()-m.gx),double(u.z.toFloat()-m.gz)));
    }
    if(arrived!=totalMoving)allAt=-1;
    long peakRss=0;
#if defined(__unix__)
    rusage usage{};if(!getrusage(RUSAGE_SELF,&usage))peakRss=usage.ru_maxrss;
#if defined(__APPLE__)
    peakRss/=1024; // Darwin reports bytes; Linux reports KiB.
#endif
#endif
    const auto flow=world.flowStats();const auto& retail=world.pathStats();double sum=0;for(double t:times)sum+=t;
    std::printf("{\"schema\":2,\"mode\":\"%s\",\"scenario\":\"%s\",\"units_per_player\":%d,\"players\":%d,\"total_units\":%zu,\"moving_percent\":%d,\"moving_units\":%d,\"ticks\":%d,\"workers\":%s,\"map_cells\":[%d,%d],",
        o.mode.c_str(),o.scenario.c_str(),o.units,o.players,members.size(),o.movingPercent,totalMoving,o.ticks,o.workers?"true":"false",width,height);
    std::printf("\"seed\":%u,\"latency_observation\":%s,",o.seed,o.latency?"true":"false");
    if(o.latency) {
        const auto ages=latency.pendingAges();
        std::printf("\"route_requests_observed\":%llu,\"route_deliveries_observed\":%zu,\"route_delivery_successes\":%zu,\"route_delivery_failures\":%zu,\"route_delivery_empty\":%llu,\"route_requests_still_pending\":%zu,\"route_request_cancelled\":%llu,\"route_request_replaced\":%llu,\"route_request_stale\":%llu,\"route_request_cleared\":%llu,\"route_telemetry_unmatched\":%llu,",
            (unsigned long long)latency.sequence,latency.deliveries.size(),latency.successful.size(),latency.failed.size(),
            (unsigned long long)latency.empty,latency.pending.size(),(unsigned long long)latency.cancellations[0],
            (unsigned long long)latency.cancellations[1],(unsigned long long)latency.cancellations[2],
            (unsigned long long)latency.cancellations[3],(unsigned long long)latency.unknown);
        std::printf("\"route_request_to_delivery_ticks_received_only_p50\":%.0f,\"route_request_to_delivery_ticks_received_only_p95\":%.0f,\"route_request_to_delivery_ticks_received_only_p99\":%.0f,\"route_request_to_success_ticks_received_only_p95\":%.0f,\"route_pending_age_ticks_p95\":%.0f,\"route_pending_age_ticks_max\":%.0f,",
            percentile(latency.deliveries,.5),percentile(latency.deliveries,.95),percentile(latency.deliveries,.99),
            percentile(latency.successful,.95),percentile(ages,.95),percentile(ages,1));
        uint64_t retired=latency.deliveries.size()+latency.pending.size();
        for(uint64_t count:latency.cancellations)retired+=count;
        std::printf("\"route_request_to_success_ticks_received_only_p50\":%.0f,\"route_request_to_success_ticks_received_only_p99\":%.0f,\"route_request_to_delivery_ticks_received_only_max\":%.0f,\"route_requests_censored\":%zu,\"route_lifecycle_balanced\":%s,",
            percentile(latency.successful,.5),percentile(latency.successful,.99),percentile(latency.deliveries,1),
            latency.pending.size(),retired==latency.sequence&&!latency.unknown?"true":"false");
        std::printf("\"route_request_to_delivery_wall_ms_received_only_p50\":%.6f,\"route_request_to_delivery_wall_ms_received_only_p95\":%.6f,\"route_request_to_delivery_wall_ms_received_only_p99\":%.6f,",
            percentile(latency.deliveryWallMs,.5),percentile(latency.deliveryWallMs,.95),percentile(latency.deliveryWallMs,.99));
    }
    std::printf("\"tick_ms_mean\":%.6f,\"tick_ms_p50\":%.6f,\"tick_ms_p95\":%.6f,\"tick_ms_p99\":%.6f,\"setup_ms\":%.3f,\"event_ms\":%.3f,",
        sum/times.size(),percentile(times,.5),percentile(times,.95),percentile(times,.99),setupMs,eventMs);
    std::printf("\"alive\":%d,\"physical_in_goal\":%d,\"orders_complete\":%d,\"arrived_settled\":%d,\"arrival_rest_ticks\":%d,\"all_arrived_tick\":%d,\"arrival_tick_p50\":%d,\"arrival_tick_p95\":%d,\"max_goal_distance_px\":%.4f,",
        alive,physical,retired,arrived,restTicks,allAt,milestone(arrivalTimes,totalMoving,.5),milestone(arrivalTimes,totalMoving,.95),maxDistance);
    std::printf("\"final_command_tick\":%d,\"whole_group_latest_order_ticks\":%d,\"authored_goal_radius_px\":%.4f,",finalCommand,allAt<0?-1:allAt-finalCommand,members.empty()?0:members.front().radius);
    std::printf("\"first_move_tick_p50\":%d,\"first_move_tick_p95\":%d,\"admission_stamp_tick_p95\":%d,\"initial_segment_available_tick_p95\":%d,\"pending_clear_tick_p95\":%d,\"stalled_unit_ticks\":%llu,\"path_length_px\":%.4f,\"path_to_initial_straight_ratio\":%.6f,",
        milestone(firstMoves,totalMoving,.5),milestone(firstMoves,totalMoving,.95),milestone(firstAdmissions,totalMoving,.95),milestone(firstRoutes,totalMoving,.95),
        milestone(firstPendingClears,totalMoving,.95),(unsigned long long)stalled,totalPath,totalStraight>0?totalPath/totalStraight:0);
    std::printf("\"crossed_middle\":%d,\"first_cross_tick\":%d,\"last_cross_tick\":%d,\"crossings_per_sim_second\":%.6f,\"illegal_footprint_samples\":%llu,\"illegal_final_movers\":%d,\"peak_pending\":%llu,",
        crossed,firstCross,lastCross,crossed*30.0/o.ticks,(unsigned long long)illegalSamples,illegalFinal,(unsigned long long)peakPending);
    std::printf("\"retail_work_sum\":%llu,\"retail_requests\":%llu,\"retail_completions\":%llu,\"retail_failures\":%llu,\"flow_requests\":%llu,\"flow_deliveries\":%llu,\"flow_work\":%llu,\"flow_navigation_peak_bytes\":%zu,\"process_peak_rss_kib\":%ld,",
        (unsigned long long)retail.workSpent(),(unsigned long long)retail.requests(),(unsigned long long)retail.completions(),(unsigned long long)retail.failures(),
        (unsigned long long)flow.requests,(unsigned long long)flow.deliveries,(unsigned long long)(flow.snapshotWork+flow.fieldWork+flow.localWork),navigationBytes,peakRss);
    std::printf("\"retail_plus_peak_bytes_sampled\":%zu,",retailPlusPeakBytes);
    printBuild();
    printDiagnostics(world,o.profile);
    std::printf("\"allocation_counting\":%s,\"tick_cpp_allocation_calls\":%llu,\"tick_cpp_allocation_requested_bytes\":%llu,\"hash\":\"%016llx\"}\n",
        o.allocations?"true":"false",(unsigned long long)crowdbench_allocation::calls.load(),(unsigned long long)crowdbench_allocation::bytes.load(),(unsigned long long)world.stateHash());
    return 0;
}
inline int main(int argc,char** argv) {
    if(argc==2&&std::string_view(argv[1])=="--help") {
        std::puts("crowdbench [legacy-scenario...]\ncrowdbench --mode retail|retail-plus|flowfield|cooperative --units N --players N --moving-percent N --ticks N --scenario NAME [--seed N] [--workers] [--allocations] [--profile] [--latency] [--trace PATH]\n"
            "Scenarios: open doors bridges maze opposingcolumns sharedgoal mixedfootprints exploration dynamicobstacle rapidreplacement unreachable recovery recovery-passive\n"
            "units is per player; movement percentage rounds down per player. Default execution is serial.\n"
            "arrived_settled requires live, empty orders, zero speed, legal footprint, authored goal area and 30 unchanged ticks.\n"
            "Latency percentiles are censored (-1) if that fraction has not reached the event. Initial segment availability is not search delivery; pending clear can include a failed or cancelled search.\n"
            "--latency separately measures actual queued request to delivery callbacks in simulation ticks, including admission wait; received-only percentiles exclude cancelled and outstanding requests, whose counts/ages are reported. Wall-ms measures elapsed harness time between the same callbacks, including observation/events. This diagnostic adds allocation overhead.\n"
            "C++ allocation counting is opt-in, only during ticks; it excludes direct malloc and adds measurement overhead.\n"
            "Recovery explicitly reissues commands after removing the barrier; recovery-passive preserves the original mission. Rapid replacement stops after the first half of the run.\n"
            "Trace writes per-tick hashes/counters and controller/route changes plus 30-tick unit snapshots; traces are outside tick timings.");return 0;
    }
    try{return run(parse(argc,argv));}catch(const std::exception& error){std::fprintf(stderr,"crowdbench: %s\n",error.what());return 2;}
}
}
