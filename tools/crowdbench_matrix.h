#pragma once

// Parameterized, asset-free comparisons. Observation, traces, and scenario
// mutations are outside the timed World::tick. Include in crowdbench.cpp only.
#include "sim/sim.h"
#include "crowdbench_telemetry.h"
#include "crowdbench_acceptance.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
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
    if(o.mode!="retail"&&o.mode!="legion")
        throw std::runtime_error("mode must be retail or legion");
    constexpr std::array names{"open","doors","bridges","maze","opposingcolumns","sharedgoal",
        "mixedfootprints","exploration","dynamicobstacle","rapidreplacement","unreachable","recovery","recovery-passive",
        "jagged","trapped","crowdtrap","singleunit","groupdetour"};
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
    if constexpr(requires {world.legionStats();}) {
        const auto l=world.legionStats();
        metric("legion_plane_builds",l.planeBuilds);metric("legion_field_work",l.fieldWork);
        metric("legion_fields_built",l.fieldsBuilt);metric("legion_field_evictions",l.fieldEvictions);
        metric("legion_groups",l.groups);metric("legion_registrations",l.registrations);
        metric("legion_holds",l.holds);metric("legion_slides",l.slides);metric("legion_arrivals",l.arrivals);
        metric("legion_contact_arrivals",l.contactArrivals);metric("legion_trapped",l.trapped);
        metric("legion_escapes",l.escapes);metric("legion_bytes",l.bytes);
        metric("legion_pass_scans",l.passScans);metric("legion_pass_scans_skipped",l.passScansSkipped);
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
// Acceptance scenarios (jagged trapped crowdtrap singleunit groupdetour). The
// geometry depends only on scenario, units and players, never on the mode.
struct Spawn {float x=0,z=0,gx=0,gz=0,radius=32;int foot=2,player=0;bool moving=true;};
struct Layout {
    int width=0,height=0;std::vector<Rect> walls;std::vector<Spawn> spawns;
    Rect gate{0,0,0,0};int closeTick=-1,openTick=-1;
    std::vector<Rect> blocks; // groupdetour obstacle per player (route-side observation)
};
inline bool acceptanceScenario(const std::string& name) {
    return name=="jagged"||name=="trapped"||name=="crowdtrap"||name=="singleunit"||name=="groupdetour";
}
inline Layout acceptanceLayout(const Options& o) {
    Layout l;const int movingPerPlayer=o.units*o.movingPercent/100;
    auto cell=[&](int x,int z) {l.walls.push_back({x,z,1,1});};
    if(o.scenario=="singleunit") {
        // Every unit alone in its own walled tile with a concave cup facing it:
        // even tiles start in front of the cup, odd tiles start inside it.
        constexpr int TW=56,TH=40;const int total=o.units*o.players;
        const int perRow=int(std::ceil(std::sqrt(double(total)))),tileRows=(total+perRow-1)/perRow;
        l.width=round64(perRow*TW);l.height=round64(tileRows*TH);
        for(int i=0;i<total;++i) {
            const int tx=i%perRow*TW,tz=i/perRow*TH,zc=tz+TH/2;
            l.walls.push_back({tx,tz,TW,1});l.walls.push_back({tx,tz+TH-1,TW,1});
            l.walls.push_back({tx,tz,1,TH});l.walls.push_back({tx+TW-1,tz,1,TH});
            l.walls.push_back({tx+32,zc-10,2,22});
            l.walls.push_back({tx+16,zc-10,18,2});l.walls.push_back({tx+16,zc+10,18,2});
            Spawn s;s.player=i/o.units;s.moving=i%o.units<movingPerPlayer;
            s.x=float((tx+(i%2?24:8))*16);s.z=float(zc*16);s.gx=float((tx+TW-8)*16);s.gz=float(zc*16);
            l.spawns.push_back(s);
        }
        return l;
    }
    const bool jagged=o.scenario=="jagged",crowdtrap=o.scenario=="crowdtrap";
    const int stride=jagged?5:3,rows=int(std::ceil(std::sqrt(double(o.units))));
    const int columns=(o.units+rows-1)/rows;
    const int laneHeight=std::max(rows*stride+(crowdtrap?24:16),o.scenario=="groupdetour"?64:48);
    l.width=round64(2*columns*stride+320);l.height=round64(o.players*laneHeight+64);
    const int middle=l.width/2,left=32,right=l.width-32-columns*stride;
    for(int p=0;p<=o.players;++p)l.walls.push_back({0,24+p*laneHeight,l.width,2});
    for(int p=0;p<o.players;++p) {
        const int low=26+p*laneHeight,high=24+(p+1)*laneHeight,span=high-low,zc=(low+high)/2;
        const int baseZ=36+p*laneHeight,divider=baseZ+(rows/2)*stride+1;
        if(jagged) {
            // Funnel of 2-cell stair steps, a corridor whose faces carry a
            // triangle staircase, 1-cell teeth and 1-cell notches, then a
            // staggered field of small plus/L/staircase rocks.
            const int hw=std::max(11,span/4),c0=middle-24,c1=middle+24;
            const int depth=zc-hw-low,funnel=(depth+1)/2;
            auto jag=[](int x) {const int t=x%8<4?x%8:8-x%8;return t+(x%13==0?3:0)-(x%17==5?3:0);};
            for(int x=c0;x<c1;++x) {
                const int top=zc-hw+jag(x),bottom=zc+hw-jag(x+3);
                l.walls.push_back({x,low,1,top-low});l.walls.push_back({x,bottom,1,high-bottom});
            }
            for(int i=0;i<funnel;++i) {
                const int h=std::min(depth,2*(i+1)+(i%2)),g=std::min(depth,2*(funnel-i)+(i%2));
                l.walls.push_back({c0-funnel+i,low,1,h});l.walls.push_back({c0-funnel+i,high-h,1,h});
                l.walls.push_back({c1+i,low,1,g});l.walls.push_back({c1+i,high-g,1,g});
            }
            const int r0=c1+funnel+6;
            for(int gx=0;gx<=30;gx+=10)for(int gz=3+(gx/10%2)*5;gz+3<=span-3;gz+=10) {
                const int x=r0+gx,z=low+gz;
                switch((gx+gz)%3) {
                case 0:cell(x+1,z);cell(x,z+1);cell(x+1,z+1);cell(x+2,z+1);cell(x+1,z+2);break;
                case 1:cell(x,z);cell(x,z+1);cell(x,z+2);cell(x+1,z+2);cell(x+2,z+2);break;
                default:cell(x,z);cell(x+1,z);cell(x+1,z+1);cell(x+2,z+1);cell(x+2,z+2);break;
                }
            }
        }
        if(crowdtrap) {
            // Two sub-lanes, each with a 6-cell door; the lower door is gated.
            l.walls.push_back({0,divider,l.width,2});
            const int upper=(low+divider)/2-3,lower=(divider+2+high)/2-3;
            l.walls.push_back({middle-2,low,4,upper-low});
            l.walls.push_back({middle-2,upper+6,4,divider-upper-6});
            l.walls.push_back({middle-2,divider+2,4,lower-divider-2});
            l.walls.push_back({middle-2,lower+6,4,high-lower-6});
            if(!p) {l.gate={middle-2,lower,4,6};l.closeTick=std::min(30,std::max(1,o.ticks/3));l.openTick=std::max(2,o.ticks/2);}
        }
        if(o.scenario=="groupdetour") {
            const Rect block{middle-24,low+std::max(6,span*30/100),48,0};
            Rect b=block;b.h=high-std::max(6,span*15/100)-b.z;
            l.walls.push_back(b);l.blocks.push_back(b);
        }
        int pockets=0;
        for(int k=0;k<o.units;++k) {
            const int col=k/rows,row=k%rows,slotX=(columns-1-col)*stride;
            const int z=baseZ+row*stride+(crowdtrap&&row>=rows/2?4:0);
            Spawn s;s.player=p;s.moving=k<movingPerPlayer;s.foot=jagged?1+k%4:2;
            s.x=float((left+slotX)*16);s.z=float(z*16);s.gx=float((right+slotX)*16);s.gz=float(z*16);
            if(o.scenario=="trapped"&&k%8==7) {
                // Alternately sealed, or leaking through a gap one cell narrower
                // than the footprint. Pitch 12 leaves >=4-cell lanes between pockets.
                const int j=pockets++,f=2+(j%3),perColumn=std::max(1,(span-4)/12);
                const int px=left+columns*stride+12+(j/perColumn)*12,pz=low+2+(j%perColumn)*12,size=f+4;
                if(px+size>=right-8)throw std::runtime_error("trapped scenario: pockets do not fit");
                l.walls.push_back({px,pz,size,1});l.walls.push_back({px,pz+size-1,size,1});
                l.walls.push_back({px,pz,1,size});
                if(j%2==0)l.walls.push_back({px+size-1,pz,1,size});
                else {
                    const int gap=f-1,gz=pz+1+(f+2-gap)/2;
                    l.walls.push_back({px+size-1,pz,1,gz-pz});
                    l.walls.push_back({px+size-1,gz+gap,1,pz+size-gz-gap});
                }
                s.foot=f;s.x=float((px+1)*16+(f+2)*8);s.z=float((pz+1)*16+(f+2)*8);
            }
            l.spawns.push_back(s);
        }
    }
    return l;
}
inline int run(const Options& o) {
    const auto setupStart=Clock::now();
    const bool mixed=o.scenario=="mixedfootprints",shared=mixed||o.scenario=="sharedgoal";
    const bool opposing=o.scenario=="opposingcolumns",exploring=o.scenario=="exploration";
    const int stride=mixed?5:3,rows=int(std::ceil(std::sqrt(double(o.units))));
    const int columns=(o.units+rows-1)/rows,laneHeight=rows*stride+16;
    const bool accept=acceptanceScenario(o.scenario);const Layout acc=accept?acceptanceLayout(o):Layout{};
    const int width=accept?acc.width:round64(2*columns*stride+320),height=accept?acc.height:round64(o.players*laneHeight+64);
    const int middle=width/2,left=32,right=width-32-columns*stride;
    const int movingPerPlayer=o.units*o.movingPercent/100,totalMoving=movingPerPlayer*o.players;
    LatencyObserver latency;
    World world;world.setGameSeed(o.seed);world.setVisPlayer(-1);world.setSerialThreads(!o.workers);world.setPathService(true);
    // Numeric identities (Retail 0, Legion 4) so the frozen baseline, which
    // predates the Legion enumerator, builds this harness unchanged.
    world.setPathfindingMode(PathfindingMode(o.mode=="legion"?4:0));
    world.setPlayerCount(o.players);for(int p=0;p<o.players;++p)world.setTeam(p,0);
    world.setTerrain(std::vector<uint8_t>(size_t(width)*height,100),width,height,64);
    std::vector<Rect> walls;if(accept)walls=acc.walls;
    const bool lanes=!accept&&(o.scenario=="doors"||o.scenario=="bridges"||o.scenario=="maze"||exploring);
    if(lanes)for(int p=0;p<=o.players;++p)walls.push_back({0,24+p*laneHeight,width,2});
    if(!accept)for(int p=0;p<o.players;++p) {
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
    std::array<UnitType,4> footTypes{mover(1,false),mover(2,false),mover(3,false),mover(4,false)};
    if(accept)for(const auto& s:acc.spawns) {
        Member member;member.player=s.player;member.moving=s.moving;member.gx=s.gx;member.gz=s.gz;
        member.alternateX=s.x;member.alternateZ=s.z;member.radius=s.radius;
        member.id=world.spawn(&footTypes[size_t(s.foot-1)],s.x,s.z,std::nullopt,s.player);
        if(member.id<=0)throw std::runtime_error("failed to spawn requested population");
        const auto& u=*world.unit(member.id);member.initialX=member.previousX=u.x.v;
        member.initialZ=member.previousZ=u.z.v;members.push_back(member);
    }
    if(!accept)for(int p=0;p<o.players;++p)for(int k=0;k<o.units;++k) {
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
    // Acceptance observation (all modes, all scenarios): see crowdbench_acceptance.h.
    namespace ca=crowdbench_acceptance;
    ca::Observer observer;ca::Totals totals;std::vector<ca::Track> tracks(members.size());
    for(auto& t:tracks)t.init();
    std::vector<int> foots;
    for(const auto& m:members) {const int f=world.unit(m.id)->type->footX;
        if(std::find(foots.begin(),foots.end(),f)==foots.end())foots.push_back(f);}
    std::sort(foots.begin(),foots.end());
    std::vector<Rect> observedWalls=walls;
    std::map<std::tuple<int,int,int,int>,std::vector<int32_t>> goalLabels;
    auto rebuildObserver=[&] {
        std::vector<ca::Wall> w;for(const auto& r:walls)w.push_back({r.x,r.z,r.w,r.h});
        observer.reset(width,height,w,foots);goalLabels.clear();observedWalls=walls;
    };
    rebuildObserver();
    auto originOf=[](const Unit& u) {
        return std::pair{footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ)};
    };
    // Static path to the goal disc for this footprint (component membership).
    auto reachable=[&](const Member& m,const Unit& u) {
        const int f=u.type->footX;const auto [ox,oz]=originOf(u);
        const auto& p=observer.prints.at(f);
        if(!observer.legalAt(p,ox,oz))return true; // not judged: placement keeps origins legal
        const auto key=std::make_tuple(f,int(m.gx),int(m.gz),int(m.radius));
        auto it=goalLabels.find(key);
        if(it==goalLabels.end()) {
            std::vector<int32_t> labels;
            observer.goalOrigins(p,int(m.gx),int(m.gz),int(m.radius),[&](int c){labels.push_back(p.label[c]);});
            std::sort(labels.begin(),labels.end());labels.erase(std::unique(labels.begin(),labels.end()),labels.end());
            it=goalLabels.emplace(key,std::move(labels)).first;
        }
        return std::binary_search(it->second.begin(),it->second.end(),p.label[size_t(oz)*width+ox]);
    };
    const bool computeOptimal=o.scenario=="singleunit"||totalMoving<=256;
    if(computeOptimal)for(size_t i=0;i<members.size();++i)if(members[i].moving) {
        const auto& u=*world.unit(members[i].id);const auto [ox,oz]=originOf(u);
        tracks[i].optimal=observer.optimalPx(u.type->footX,ox,oz,int(members[i].gx),int(members[i].gz),int(members[i].radius));
    }
    std::vector<size_t> stamped;
    std::vector<uint32_t> occMember(size_t(width)*height,0); // cell -> member index + 1 (0 = none)
    std::vector<ca::Step> steps(members.size());
    auto observeAcceptance=[&](int tick) {
        for(size_t c:stamped)observer.occupancy[c]=0,occMember[c]=0;
        stamped.clear();
        for(size_t i=0;i<members.size();++i) {
            const auto& m=members[i];
            const auto& u=*world.unit(m.id);if(!u.alive())continue;
            const auto [ox,oz]=originOf(u);
            for(int z=oz;z<oz+u.type->footZ;++z)for(int x=ox;x<ox+u.type->footX;++x)
                if(x>=0&&z>=0&&x<width&&z<height) {
                    const size_t c=size_t(z)*width+x;observer.occupancy[c]=m.id;occMember[c]=uint32_t(i+1);stamped.push_back(c);
                }
        }
        // Advance every window first, so a leader's progress over this same
        // window is known when a follower behind it is classified.
        for(size_t i=0;i<members.size();++i) {
            const auto& u=*world.unit(members[i].id);
            steps[i]=ca::advance(tracks[i],u.x.v,u.z.v,u.heading.v);
        }
        // Queued behind a moving leader (user decision, 2026-10-06; see
        // crowdbench_acceptance.h): a same-player mobile member whose centre
        // is within two body widths and within 60 degrees of this body's goal
        // or static travel direction, and which itself made progress over the
        // same window.
        auto queuedBehindMover=[&](size_t i,const Unit& u,int ox,int oz,int tx,int tz) {
            const auto& m=members[i];const int f=u.type->footX;
            const double ux=u.x.v/65536.0,uz=u.z.v/65536.0;
            const double gx=m.gx-ux,gz=m.gz-uz,gl=std::sqrt(gx*gx+gz*gz),tl=std::sqrt(double(tx*tx+tz*tz));
            for(int z=oz-2*f;z<oz+3*f;++z)for(int x=ox-2*f;x<ox+3*f;++x) {
                if(x<0||z<0||x>=width||z>=height)continue;
                const uint32_t k=occMember[size_t(z)*width+x];
                if(!k||k-1==i)continue;
                const size_t j=k-1;const auto& lm=members[j];const auto& l=*world.unit(lm.id);
                if(lm.player!=m.player||l.type->maxVel<=Fixed()||tracks[j].filled<=ca::window||steps[j].stationary)continue;
                const double vx=l.x.v/65536.0-ux,vz=l.z.v/65536.0-uz,vl=std::sqrt(vx*vx+vz*vz);
                if(vl<=0||vl>(f+l.type->footX)*16.0)continue;
                const bool goalAhead=gl>0&&vx*gx+vz*gz>=0.5*vl*gl;
                const bool travelAhead=tl>0&&vx*tx+vz*tz>=0.5*vl*tl;
                if(goalAhead||travelAhead)return true;
            }
            return false;
        };
        for(size_t i=0;i<members.size();++i) {
            const auto& m=members[i];auto& t=tracks[i];const auto& u=*world.unit(m.id);
            const auto& step=steps[i];
            if(step.stationary) {totals.headingStationaryBam+=step.turnAbs;totals.travelReversals+=step.reversed;}
            if(step.spinning) {++t.spinTicks;++totals.spinTicks;}
            if(!m.moving)continue;
            const auto [ox,oz]=originOf(u);const int f=u.type->footX;
            int cls;
            if(m.near)cls=ca::AtGoal;
            else if(!reachable(m,u))cls=ca::Trapped;
            else if(tick-m.commandTick<ca::window)cls=ca::Warmup;
            else if(!step.stationary)cls=ca::Progressing;
            else if(const auto* field=observer.direction(f,int(m.gx),int(m.gz),int(m.radius));!field||field->at(ox,oz)==ca::far)cls=ca::Unclassified;
            else {
                // Follow the static distance field for foot+2 steps; any other
                // body on those footprint cells means the crowd holds the unit.
                const auto& p=observer.prints.at(f);
                int cx=ox,cz=oz;bool crowd=false;int tx=0,tz=0;
                for(int s=0;s<f+2&&!crowd;++s) {
                    int best=-1;uint16_t bestD=field->at(cx,cz);
                    observer.forNeighbours(p,cx,cz,[&](int n,int){const uint16_t d=field->at(n%width,n/width);if(d<bestD){bestD=d;best=n;}});
                    if(best<0)break;
                    cx=best%width;cz=best/width;
                    if(s==0)tx=cx-ox,tz=cz-oz;
                    for(int z=cz;z<cz+f;++z)for(int x=cx;x<cx+f;++x) {
                        const int32_t id=observer.occupancy[size_t(z)*width+x];crowd|=id&&id!=m.id;
                    }
                }
                bool terrain=false;
                for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx)terrain|=!observer.legalAt(p,ox+dx,oz+dz);
                if(!crowd&&terrain&&queuedBehindMover(i,u,ox,oz,tx,tz))crowd=true;
                cls=crowd?ca::CrowdHeld:terrain?ca::TerrainStuck:ca::OpenIdle;
            }
            ++totals.classTicks[cls];t.finalClass=cls;
            t.everTerrainStuck|=cls==ca::TerrainStuck;t.everCrowdHeld|=cls==ca::CrowdHeld;
            if(cls==ca::CrowdHeld&&step.spinning)++totals.spinCrowdHeld;
            if(cls==ca::Trapped) {
                if(t.trappedSince<0)t.trappedSince=tick;
                t.everTrapped=true;
                if(step.spinning)++totals.spinTrapped;
                if(step.moved||step.turned) {
                    t.settleMax=std::max(t.settleMax,tick-t.trappedSince);
                    if(tick-t.trappedSince>=ca::trappedGrace) {++totals.trappedMoving;t.movingAfterGrace=true;}
                }
            } else t.trappedSince=-1;
            if(m.near&&t.pathAtGoal<0)t.pathAtGoal=m.path;
            if(t.side<0&&size_t(m.player)<acc.blocks.size()) {
                const auto& b=acc.blocks[size_t(m.player)];
                if(u.x.v>=int32_t(b.x*16)<<16&&u.x.v<int32_t((b.x+b.w)*16)<<16)
                    t.side=int64_t(u.z.v)*2<(int64_t(b.z*2+b.h)*16<<16)?0:1;
            }
        }
        if(tick%30==0)for(int p=0;p<o.players;++p) {
            std::vector<int32_t> xs;
            for(const auto& m:members)if(m.moving&&m.player==p&&!m.near)xs.push_back(world.unit(m.id)->x.v);
            if(xs.size()<2)continue;
            std::sort(xs.begin(),xs.end());
            const double spread=double(xs[(xs.size()-1)*9/10]-xs[(xs.size()-1)/10])/65536;
            totals.spreadSum+=spread;totals.spreadMax=std::max(totals.spreadMax,spread);++totals.spreadSamples;
        }
    };
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
        trace<<"tick,world_tick,hash,retail_requests,retail_completions,retail_failures,retail_work,retail_pending\n";
        unitTrace<<"tick,id,epoch,x_raw,z_raw,speed_raw,orders,route_stamp,control_hash,front_x_raw,front_z_raw,controller,segment,exhausted,consumed,goal_x,goal_z,goal_radius,foot_x,foot_z,pending,heading_bam,turn_request_bam,blocked,route_failed,mission_events,leg_controller,leg_x_raw,leg_z_raw,leg_exhausted,leg_consumed,mission_stage,mission_pending,mission_wait_mask,mission_radius,mission_target_x_raw,mission_target_z_raw,next_x_raw,next_z_raw\n";
    }
    const double setupMs=millis(setupStart,Clock::now());
    std::vector<double> times;times.reserve(size_t(o.ticks));
    uint64_t illegalSamples=0,peakPending=0;
    double eventMs=0;int allAt=-1,firstCross=-1,lastCross=-1;
    auto observeTrace=[&](int tick) {
        if(!trace.is_open())return;
        const auto& retail=world.pathStats();
        trace<<tick<<','<<world.tickCount()<<','<<std::hex<<world.stateHash()<<std::dec<<','
            <<retail.requests()<<','<<retail.completions()<<','<<retail.failures()<<','<<retail.workSpent()<<','
            <<retail.pendingCount()<<'\n';
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
        if(accept&&acc.closeTick>0&&(tick==acc.closeTick||tick==acc.openTick)) {
            std::vector<Rect> next=acc.walls;if(tick==acc.closeTick)next.push_back(acc.gate);
            barriers(world,width,height,walls,next);walls=std::move(next);traceWalls(tick);
        }
        eventMs+=millis(eventStart,Clock::now());
        if(walls.size()!=observedWalls.size()||!std::equal(walls.begin(),walls.end(),observedWalls.begin(),
            [](const Rect& a,const Rect& b){return a.x==b.x&&a.z==b.z&&a.w==b.w&&a.h==b.h;}))rebuildObserver();
        crowdbench_allocation::enabled.store(o.allocations,std::memory_order_relaxed);
        const auto begin=Clock::now();world.tick(1.f/30);const auto end=Clock::now();
        crowdbench_allocation::enabled.store(false,std::memory_order_relaxed);
        times.push_back(millis(begin,end));
        peakPending=std::max(peakPending,uint64_t(world.pathStats().pendingCount()));
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
        observeAcceptance(tick);
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
    const auto& retail=world.pathStats();double sum=0;for(double t:times)sum+=t;
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
    std::printf("\"retail_work_sum\":%llu,\"retail_requests\":%llu,\"retail_completions\":%llu,\"retail_failures\":%llu,\"process_peak_rss_kib\":%ld,",
        (unsigned long long)retail.workSpent(),(unsigned long long)retail.requests(),(unsigned long long)retail.completions(),(unsigned long long)retail.failures(),
        peakRss);
    {
        uint64_t spinning=0,everTerrain=0,everCrowd=0,everTrapped=0,trappedMovers=0,reachableMovers=0,optimalUnits=0;
        int settleMax=-1;uint64_t finals[ca::ClassCount]{};double ratioSum=0,ratioMax=0;
        int sides=0,minority=0,sided=0;
        for(size_t i=0;i<members.size();++i) {
            const auto& t=tracks[i];spinning+=t.spinTicks>0;
            if(!members[i].moving)continue;
            everTerrain+=t.everTerrainStuck;everCrowd+=t.everCrowdHeld;everTrapped+=t.everTrapped;
            trappedMovers+=t.movingAfterGrace;if(t.everTrapped)settleMax=std::max(settleMax,t.settleMax);
            if(t.finalClass>=0)++finals[t.finalClass];
            reachableMovers+=reachable(members[i],*world.unit(members[i].id));
            if(t.optimal>0&&t.pathAtGoal>=0) {const double r=t.pathAtGoal/t.optimal;++optimalUnits;ratioSum+=r;ratioMax=std::max(ratioMax,r);}
        }
        for(int p=0;p<int(acc.blocks.size());++p) {
            int count[2]{};
            for(size_t i=0;i<members.size();++i)if(members[i].player==p&&tracks[i].side>=0)++count[tracks[i].side];
            sides+=(count[0]>0)+(count[1]>0);minority+=std::min(count[0],count[1]);sided+=count[0]+count[1];
        }
        std::printf("\"acceptance_window_ticks\":%d,\"acceptance_progress_px\":%d,\"acceptance_trapped_grace_ticks\":%d,",ca::window,ca::progressPx,ca::trappedGrace);
        std::printf("\"units_spinning\":%llu,\"spin_unit_ticks\":%llu,\"heading_change_while_stationary_deg\":%.3f,\"travel_reversals_while_stationary\":%llu,\"spin_while_trapped_unit_ticks\":%llu,\"spin_while_crowd_held_unit_ticks\":%llu,",
            (unsigned long long)spinning,(unsigned long long)totals.spinTicks,totals.headingStationaryBam*360.0/65536,(unsigned long long)totals.travelReversals,
            (unsigned long long)totals.spinTrapped,(unsigned long long)totals.spinCrowdHeld);
        for(int c=0;c<ca::ClassCount;++c)std::printf("\"class_%s_unit_ticks\":%llu,\"final_%s\":%llu,",ca::className(c),(unsigned long long)totals.classTicks[c],ca::className(c),(unsigned long long)finals[c]);
        std::printf("\"units_ever_terrain_stuck\":%llu,\"units_ever_crowd_held\":%llu,\"units_ever_trapped\":%llu,\"static_reachable_movers\":%llu,",
            (unsigned long long)everTerrain,(unsigned long long)everCrowd,(unsigned long long)everTrapped,(unsigned long long)reachableMovers);
        std::printf("\"trapped_moving_unit_ticks\":%llu,\"trapped_units_moving_after_grace\":%llu,\"trapped_settle_ticks_max\":%d,",
            (unsigned long long)totals.trappedMoving,(unsigned long long)trappedMovers,settleMax);
        std::printf("\"path_optimality_units\":%llu,\"path_optimality_ratio_mean\":%.6f,\"path_optimality_ratio_max\":%.6f,",
            (unsigned long long)optimalUnits,optimalUnits?ratioSum/optimalUnits:-1.0,optimalUnits?ratioMax:-1.0);
        std::printf("\"group_sides_taken\":%d,\"group_minority_side_fraction\":%.6f,\"group_progress_spread_px_mean\":%.3f,\"group_progress_spread_px_max\":%.3f,",
            sides,sided?double(minority)/sided:0.0,totals.spreadSamples?totals.spreadSum/totals.spreadSamples:0.0,totals.spreadMax);
    }
    printBuild();
    printDiagnostics(world,o.profile);
    std::printf("\"allocation_counting\":%s,\"tick_cpp_allocation_calls\":%llu,\"tick_cpp_allocation_requested_bytes\":%llu,\"hash\":\"%016llx\"}\n",
        o.allocations?"true":"false",(unsigned long long)crowdbench_allocation::calls.load(),(unsigned long long)crowdbench_allocation::bytes.load(),(unsigned long long)world.stateHash());
    return 0;
}
inline int main(int argc,char** argv) {
    if(argc==2&&std::string_view(argv[1])=="--help") {
        std::puts("crowdbench [legacy-scenario...]\ncrowdbench --mode retail|legion --units N --players N --moving-percent N --ticks N --scenario NAME [--seed N] [--workers] [--allocations] [--profile] [--latency] [--trace PATH]\n"
            "Scenarios: open doors bridges maze opposingcolumns sharedgoal mixedfootprints exploration dynamicobstacle rapidreplacement unreachable recovery recovery-passive\n"
            "Acceptance scenarios: jagged trapped crowdtrap singleunit groupdetour (see tools/crowdbench_acceptance.h for metric definitions)\n"
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
