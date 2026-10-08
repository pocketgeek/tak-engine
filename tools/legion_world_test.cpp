// Legion navigation (PathfindingMode 4) World tests, asset-free.
//
//   legion_world_test CASE
//
// Cases: clearance groupreuse jagged trapped crowdhold replace unreachable
// quota determinism. Each builds a small flat map with feature walls (the
// same placement plane a loaded match uses) and checks one requirement of
// docs/legion-pathfinding.md.
#include "sim/sim.h"
#include "sim/matchsetup.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace tak::sim;
namespace {
void check(bool ok,const std::string& message) {if(!ok)throw std::runtime_error(message);}

UnitType mover(int foot) {
    UnitType t{};t.id=t.name="legion-foot-"+std::to_string(foot);
    t.canMove=true;t.maxHp=100;t.footX=t.footZ=foot;t.sight=4096;t.maxVel=Fixed::raw(117964);
    t.accel=t.brake=Fixed::fromInt(10);t.turnRate=t.turnInPlaceRate=2500;t.halfCellTicks=3;t.buildTime=1;
    return t;
}

struct Fixture;
void printLeft(Fixture& f,const std::vector<int>& ids);
struct Fixture {
    World world;
    int width,height;
    std::vector<uint16_t> cells;
    Fixture(int w,int h,bool serial=true,std::vector<uint8_t> heights={}):width(w),height(h),cells(size_t(w)*h,0xffff) {
        world.setGameSeed(7);world.setVisPlayer(-1);world.setSerialThreads(serial);world.setPathService(true);
        world.setPathfindingMode(PathfindingMode::Legion);
        world.setPlayerCount(2);world.setTeam(0,0);world.setTeam(1,1);
        if(heights.empty())heights.assign(size_t(w)*h,100);
        world.setTerrain(heights,w,h,64);
    }
    void wall(int x,int z) {
        if(x<0||z<0||x>=width||z>=height)return;
        cells[size_t(z)*width+x]=0;world.blockCells(x,z,1,1,true);
    }
    void rect(int x,int z,int w,int h) {for(int j=0;j<h;++j)for(int i=0;i<w;++i)wall(x+i,z+j);}
    void open(int x,int z,int w,int h) {
        for(int j=0;j<h;++j)for(int i=0;i<w;++i)cells[size_t(z+j)*width+x+i]=0xffff;
        world.blockCells(x,z,w,h,false);
    }
    void publish() {world.setMapPlacementFeatures(cells,{{"legion-wall",1,1,true,true,false,0}});}
    int spawn(const UnitType& t,int cx,int cz,int player=0) {
        const int id=world.spawn(&t,float(cx*16),float(cz*16),std::nullopt,player);
        check(id>0,"spawn failed");return id;
    }
    void start() {
        world.tick(1.f/30);
        world.updateNavigationExploration();
        auto& explored=const_cast<std::vector<uint16_t>&>(world.navigationExploration());
        std::fill(explored.begin(),explored.end(),0xffff);
    }
    bool legal(int id) {
        const auto& u=*world.unit(id);
        return world.mobilePlacement(u,footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ),false);
    }
};

// Spinning and oscillation observer. A spin is a heading change without any
// movement. An oscillation is a return to a 16 px cell the body left less
// than 90 ticks earlier (A -> B -> A); sub-cell steering wiggle is not one.
struct Motion {
    struct Track {int32_t x=0,z=0,heading=0;std::vector<std::pair<int64_t,int>> cells;};
    std::map<int,Track> tracks;
    uint64_t spins=0,reversals=0;
    int tick=0;
    void observe(World& w,const std::vector<int>& ids) {
        ++tick;
        for(int id:ids) {
            const auto& u=*w.unit(id);
            auto [it,fresh]=tracks.try_emplace(id);
            auto& t=it->second;
            if(!fresh&&u.x.v==t.x&&u.z.v==t.z&&u.heading.v!=t.heading)++spins;
            const int64_t cell=(int64_t(u.z.v>>20)<<32)|uint32_t(u.x.v>>20);
            if(t.cells.empty()||t.cells.back().first!=cell) {
                for(size_t i=0;i+1<t.cells.size();++i)
                    if(t.cells[i].first==cell&&tick-t.cells[i].second<90) {++reversals;break;}
                t.cells.push_back({cell,tick});
                if(t.cells.size()>6)t.cells.erase(t.cells.begin());
            }
            t.x=u.x.v;t.z=u.z.v;t.heading=u.heading.v;
        }
    }
};

void printLeft(Fixture& f,const std::vector<int>& ids) {
    for(int id:ids)if(!f.world.unit(id)->orders.empty()) {
        const auto& u=*f.world.unit(id);
        std::printf("  left id=%d at %.1f,%.1f goal %.1f,%.1f state=%d\n",id,u.x.toFloat()/16,u.z.toFloat()/16,
            u.orders.back().x.toFloat()/16,u.orders.back().z.toFloat()/16,f.world.legionNavigator()->unitState(id));
    }
}

void clearance() {
    // Terrain with slopes, water and jagged feature walls: Legion's static
    // plane must agree with mobilePlacement (bodies aside) at every origin.
    const int W=96,H=80;
    std::vector<uint8_t> heights(size_t(W)*H);
    uint32_t r=12345;
    for(int z=0;z<H;++z)for(int x=0;x<W;++x) {
        r=r*1103515245u+12345u;
        int h=100+int((x*7+z*3)%40)-((r>>16)%9==0?60:0);
        if(x>60&&z>50)h=30;   // a lake
        heights[size_t(z)*W+x]=uint8_t(std::clamp(h,0,255));
    }
    Fixture f(W,H,true,heights);
    for(int i=0;i<40;++i)f.wall(10+i,20+(i%3));          // jagged ridge
    for(int i=0;i<25;++i)f.wall(30+(i*i)%7,40+i);
    f.publish();
    std::vector<UnitType> types;
    for(int foot=1;foot<=4;++foot) {auto t=mover(foot);t.maxSlope=8+foot*4;types.push_back(t);}
    types.push_back(mover(2));types.back().footX=3;types.back().id=types.back().name="legion-rect";
    std::vector<int> ids;
    for(auto& t:types)ids.push_back(f.spawn(t,2+int(ids.size())*0,2));
    f.start();
    auto* legion=f.world.legionNavigator();
    check(legion!=nullptr,"legion navigator not created in Legion mode");
    uint64_t compared=0,legalCount=0;
    for(int id:ids) {
        const auto& u=*f.world.unit(id);
        for(int z=-1;z<=H;++z)for(int x=-1;x<=W;++x) {
            // Bodies only matter where this unit's own footprint is: the plane
            // excludes mobile bodies, so compare away from every other unit.
            bool nearBody=false;
            for(int other:ids)if(other!=id) {
                const auto& o=*f.world.unit(other);
                const int ox=footprintOrigin(o.x,o.type->footX),oz=footprintOrigin(o.z,o.type->footZ);
                if(x<ox+o.type->footX&&x+u.type->footX>ox&&z<oz+o.type->footZ&&z+u.type->footZ>oz)nearBody=true;
            }
            if(nearBody)continue;
            const bool a=legion->staticLegal(u,x,z),b=f.world.mobilePlacement(u,x,z,false);
            if(a!=b)throw std::runtime_error("plane differs from mobilePlacement for "+u.type->id+" at "+
                std::to_string(x)+","+std::to_string(z)+" plane="+std::to_string(a));
            ++compared;legalCount+=a;
        }
    }
    check(legalCount>1000&&legalCount<compared,"clearance fixture is degenerate");
    std::printf("clearance compared=%llu legal=%llu\n",(unsigned long long)compared,(unsigned long long)legalCount);
}

void groupreuse() {
    Fixture f(160,64);
    f.rect(70,0,4,28);f.rect(70,36,4,28);   // a door
    f.publish();
    const auto type=mover(2);
    std::vector<int> ids;
    for(int i=0;i<40;++i)ids.push_back(f.spawn(type,10+(i%8)*3,14+(i/8)*3));
    f.start();
    for(size_t i=0;i<ids.size();++i)f.world.order(ids[i],float((110+(int(i)%8)*4)*16),float((12+int(i)/8*4)*16),false);
    for(int t=0;t<5;++t)f.world.tick(1.f/30);
    auto* legion=f.world.legionNavigator();
    const int group=legion->unitGroup(ids[0]);
    for(int id:ids)check(legion->unitGroup(id)==group,"group order split into several groups");
    const auto s=f.world.legionStats();
    check(s.fieldsBuilt==1,"one group must build exactly one field, built "+std::to_string(s.fieldsBuilt));
    for(int t=0;t<6000;++t)f.world.tick(1.f/30);
    int arrived=0;
    for(int id:ids)arrived+=f.world.unit(id)->orders.empty();
    const auto e=f.world.legionStats();
    printLeft(f,ids);
    std::printf("  detours=%llu cells=%llu holds=%llu\n",(unsigned long long)f.world.legionStats().detours,(unsigned long long)f.world.legionStats().detourCells,(unsigned long long)f.world.legionStats().holds);
    std::printf("groupreuse arrived=%d fields=%llu work=%llu\n",arrived,(unsigned long long)e.fieldsBuilt,(unsigned long long)e.fieldWork);
    check(arrived==int(ids.size()),"group did not pass the door");
    check(e.fieldsBuilt<=2,"field rebuilt per member");
}

// A sawtooth ridge with one gap at the far end: units must slide along the
// teeth (never stall pressing into them) and route around through the gap.
void jaggedRun(int count,int stride,int foot,const char* label) {
    Fixture f(128,96);
    for(int i=0;i<70;++i) {
        const int x=30+i,z=20+i/2+((i%5)<2?(i%5):0);
        f.wall(x,z);f.wall(x,z+1);
        if(i%7==3)f.wall(x,z-1);
    }
    for(int z=0;z<20;++z)f.wall(30,z);
    f.publish();
    const auto type=mover(foot);
    std::vector<int> ids;
    for(int i=0;i<count;++i)ids.push_back(f.spawn(type,40+(i%4)*(foot+2),8+(i/4)*(foot+2)));
    f.start();
    for(size_t i=0;i<ids.size();++i)f.world.order(ids[i],float((44+int(i%4)*stride)*16),float((62+int(i/4)*stride)*16),false);
    Motion motion;uint64_t pressing=0;
    for(int t=0;t<4000;++t) {
        f.world.tick(1.f/30);
        motion.observe(f.world,ids);
        for(int id:ids) {
            const auto& u=*f.world.unit(id);
            check(f.legal(id),"illegal footprint on jagged terrain");
            // Pressing: an ordered body that Legion is moving (not holding)
            // yet has no speed -- the signature of pushing into a wall.
            if(!u.orders.empty()&&f.world.legionNavigator()->unitState(id)==1&&u.speed==Fixed())++pressing;
        }
    }
    int arrived=0;for(int id:ids)arrived+=f.world.unit(id)->orders.empty();
    for(int id:ids)if(!f.world.unit(id)->orders.empty()) {
        const auto& u=*f.world.unit(id);
        std::printf("  left id=%d at %.1f,%.1f goal %.1f,%.1f state=%d\n",id,u.x.toFloat()/16,u.z.toFloat()/16,
            u.orders.back().x.toFloat()/16,u.orders.back().z.toFloat()/16,f.world.legionNavigator()->unitState(id));
    }
    std::printf("jagged %s arrived=%d/%zu spins=%llu reversals=%llu pressing=%llu\n",label,arrived,ids.size(),
        (unsigned long long)motion.spins,(unsigned long long)motion.reversals,(unsigned long long)pressing);
    check(arrived==int(ids.size()),std::string("units stuck against jagged terrain: ")+label);
    check(motion.spins==0,"units turned in place while stuck");
    check(pressing==0,"units pressed into terrain");
}
void jagged() {
    jaggedRun(1,0,2,"single-2x2");
    jaggedRun(1,0,3,"single-3x3");
    jaggedRun(1,0,4,"single-4x4");
    jaggedRun(12,6,2,"group-2x2");
}

void trapped() {
    // A sealed pocket: the body inside stops at once and never moves, turns
    // or rocks; its order waits out the grace period, then retires.
    {
        Fixture f(64,64);
        f.rect(10,10,12,1);f.rect(10,21,12,1);f.rect(10,10,1,12);f.rect(21,10,1,12);
        f.publish();
        const auto type=mover(2);
        const int inside=f.spawn(type,15,15),outside=f.spawn(type,40,15);
        f.start();
        f.world.order(inside,50*16,50*16,false);f.world.order(outside,50*16,50*16,false);
        Motion motion;
        int clearedAt=-1;uint64_t moved=0;
        int32_t x=f.world.unit(inside)->x.v,z=f.world.unit(inside)->z.v;
        for(int t=0;t<9200;++t) {
            f.world.tick(1.f/30);motion.observe(f.world,{inside});
            const auto& u=*f.world.unit(inside);
            moved+=u.x.v!=x||u.z.v!=z;x=u.x.v;z=u.z.v;
            if(clearedAt<0&&u.orders.empty())clearedAt=t;
        }
        std::printf("trapped cleared_at=%d moved_ticks=%llu spins=%llu reversals=%llu\n",clearedAt,
            (unsigned long long)moved,(unsigned long long)motion.spins,(unsigned long long)motion.reversals);
        check(moved==0,"trapped unit moved");
        check(clearedAt>=8950&&clearedAt<=9060,"trapped order not retired after the grace period");
        check(motion.spins==0&&motion.reversals==0,"trapped unit turned or rocked");
        check(f.world.legionStats().trapped>=1,"trapped classification not recorded");
        check(f.world.unit(outside)->orders.empty(),"free unit did not arrive");
    }
    // The same pocket opens before the grace period ends: the held order
    // resumes and the unit arrives (change-driven wake-up, no polling).
    {
        Fixture f(64,64);
        f.rect(10,10,12,1);f.rect(10,21,12,1);f.rect(10,10,1,12);f.rect(21,10,1,12);
        f.publish();
        const auto type=mover(2);const int inside=f.spawn(type,15,15);
        f.start();
        f.world.order(inside,50*16,50*16,false);
        for(int t=0;t<300;++t)f.world.tick(1.f/30);
        check(!f.world.unit(inside)->orders.empty(),"trapped order retired too early");
        f.open(21,14,1,4);f.publish();
        for(int t=0;t<1500;++t)f.world.tick(1.f/30);
        std::printf("trapped reopened arrived=%d\n",int(f.world.unit(inside)->orders.empty()));
        check(f.world.unit(inside)->orders.empty(),"unit did not resume after the pocket opened");
        const auto& u=*f.world.unit(inside);
        check(std::abs(u.x.toFloat()-800)<20&&std::abs(u.z.toFloat()-800)<20,"resumed unit did not reach its goal");
    }
}

void crowdhold() {
    // A one-body corridor plugged by an enemy that never moves: the column
    // behind it must hold still -- no turning in place, no back-and-forth.
    Fixture f(128,40);
    f.rect(40,0,30,18);f.rect(40,21,30,19);    // corridor rows 18..20 (3 cells)
    f.publish();
    const auto type=mover(2);
    const int plug=f.spawn(type,55,20,1);(void)plug;
    std::vector<int> ids;
    for(int i=0;i<20;++i)ids.push_back(f.spawn(type,8+(i%5)*3,10+(i/5)*4));
    f.start();
    for(int id:ids)f.world.order(id,110*16,20*16,false);
    Motion motion;
    for(int t=0;t<1500;++t) {
        f.world.tick(1.f/30);
        if(t>600)motion.observe(f.world,ids);
    }
    int holding=0;for(int id:ids)holding+=f.world.legionNavigator()->unitState(id)==2;
    std::printf("crowdhold holding=%d spins=%llu reversals=%llu\n",holding,(unsigned long long)motion.spins,(unsigned long long)motion.reversals);
    check(motion.spins==0,"held crowd turned in place");
    check(motion.reversals<=4,"held crowd oscillated");
    for(int id:ids)check(f.legal(id),"illegal footprint in held crowd");
}

// A group sent across open ground with a large standing block of idle
// bodies on its straight way (of the same player, then of another): it
// must plan round the block, not walk into it and wait against its face.
// Every member arrives in about the time of the way round, nobody spins.
int staticblockRun(int owner) {
    Fixture f(200,100);f.publish();
    const auto type=mover(2);
    std::vector<int> block,ids;
    if(owner>=0)for(int c=0;c<10;++c)for(int r=0;r<12;++r)block.push_back(f.spawn(type,92+c*3,32+r*3,owner));
    for(int i=0;i<40;++i)ids.push_back(f.spawn(type,20+(i%8)*3,44+(i/8)*3));
    f.start();
    for(int t=0;t<90;++t)f.world.tick(1.f/30);   // the block has stood still a while
    for(int id:ids)f.world.order(id,160*16,50*16,false);
    Motion motion;
    int t=0,arrived=0,half=-1;
    for(;t<4000&&arrived<int(ids.size());++t) {
        f.world.tick(1.f/30);motion.observe(f.world,ids);
        arrived=0;for(int id:ids)arrived+=f.world.unit(id)->orders.empty();
        if(half<0&&2*arrived>=int(ids.size()))half=t;
    }
    arrived=0;for(int id:ids)arrived+=f.world.unit(id)->orders.empty();
    for(int id:ids)check(f.legal(id),"illegal footprint");
    const auto s=f.world.legionStats();
    std::printf("staticblock owner=%d arrived=%d/%zu ticks=%d half=%d holds=%llu detours=%llu spins=%llu reversals=%llu\n",owner,arrived,ids.size(),t,half,
        (unsigned long long)s.holds,(unsigned long long)s.detours,(unsigned long long)motion.spins,(unsigned long long)motion.reversals);
    if(std::getenv("STATIC_VERBOSE"))printLeft(f,ids);
    if(std::getenv("STATIC_REPORT"))return t;   // measure only (e.g. on an older build)
    check(arrived==int(ids.size()),"group did not get round the standing block");
    check(motion.spins==0,"group spun at the standing block");
    // The way round the block (by one side, clear of its face) is barely
    // longer than the straight 140 cells. On open ground (no block) half the
    // group arrives at 1226 ticks and all of it at 1803; round the block it
    // streams by one side in a narrower file, so the tail is longer. Without
    // soft obstacles only 11 of 40 had arrived after 4000 ticks.
    check(half<=1800,"half the group took far longer than the way round");
    check(t<=3600,"group took far longer than the way round");
    return t;
}
void staticblock() {
    if(std::getenv("STATIC_OPEN"))staticblockRun(-1);   // reference: no block
    staticblockRun(0);staticblockRun(1);
}

// Landed flyers stand on the ground grid (retail stamps a mode-1 flyer into
// word +0, so mobilePlacement refuses a ground step into one). A group sent
// across a field of them -- a block of 30 and 12 scattered -- must plan and
// steer round them like any standing body: no overlap, no pushing into
// them, every member arrives. layout: 0 block+scattered, 1 none (open
// reference), 2 the block takes off soon after the order (its cells must
// clear at once for ground steering). Retail runs the same layout for
// reference (measured, not asserted).
struct FlyerRun {uint64_t hash=0;int arrived=0,ticks=0,half=-1,most=-1;uint64_t holds=0,pushes=0,stuck=0,overlap=0,spins=0,reversals=0;};
FlyerRun landedflyersRun(int layout,bool legion,int flyerFoot,bool serial=true) {
    Fixture f(200,100,serial);f.publish();
    if(!legion)f.world.setPathfindingMode(PathfindingMode::Retail);
    const auto type=mover(2);
    UnitType flyer{};flyer.id=flyer.name="legion-flyer";
    flyer.canFly=flyer.canMove=true;flyer.maxHp=100;flyer.footX=flyer.footZ=flyerFoot;flyer.sight=4096;
    flyer.maxVel=Fixed::fromInt(4);flyer.accel=flyer.brake=Fixed::fromInt(1);flyer.turnRate=1200;flyer.cruiseAlt=80;flyer.buildTime=1;
    // FLYER_AS_GROUND: the same layout of idle ground bodies (reference).
    const UnitType& standing=std::getenv("FLYER_AS_GROUND")&&flyerFoot==2?type:flyer;
    std::vector<int> flyers,block,ids;
    if(layout!=1) {
        const int pitch=flyerFoot+1;
        for(int c=0;c<5;++c)for(int r=0;r<6;++r)block.push_back(f.spawn(standing,90+c*pitch,38+r*pitch,c%2));
        for(int i=0;i<12;++i)flyers.push_back(f.spawn(standing,60+(i%4)*10+(i/4)*3,30+(i%4)*7+(i/4)*9,1));
        flyers.insert(flyers.end(),block.begin(),block.end());
    }
    for(int i=0;i<40;++i)ids.push_back(f.spawn(type,20+(i%8)*3,44+(i/8)*3));
    f.start();
    for(int t=0;t<90;++t)f.world.tick(1.f/30);
    for(int id:flyers)check(f.world.unit(id)->flightGroundMode==1,"flyer not landed");
    const bool asGround=&standing==&type;
    for(int id:ids)f.world.order(id,160*16,50*16,false);
    Motion motion;FlyerRun r;
    for(;r.ticks<5000&&r.arrived<int(ids.size());++r.ticks) {
        if(layout==2&&r.ticks==150)for(int id:block)f.world.order(id,190*16,95*16,false);
        f.world.tick(1.f/30);motion.observe(f.world,ids);
        r.arrived=0;
        for(int id:ids) {
            const auto& u=*f.world.unit(id);
            r.arrived+=u.orders.empty();
            const int ux=footprintOrigin(u.x,2),uz=footprintOrigin(u.z,2);
            bool touching=false;
            for(int fid:flyers) {
                const auto& v=*f.world.unit(fid);
                if(v.flightGroundMode!=1||(asGround&&!v.orders.empty()))continue;
                const int vx=footprintOrigin(v.x,flyerFoot),vz=footprintOrigin(v.z,flyerFoot);
                if(ux<vx+flyerFoot&&vx<ux+2&&uz<vz+flyerFoot&&vz<uz+2)++r.overlap;
                if(ux<=vx+flyerFoot&&vx<=ux+2&&uz<=vz+flyerFoot&&vz<=uz+2)touching=true;
            }
            // Pushing: an ordered body steered as moving, next to a landed
            // flyer, yet without speed. Stuck: any ordered body standing
            // still against one (holding there counts).
            if(legion&&touching&&!u.orders.empty()&&f.world.legionNavigator()->unitState(id)==1&&u.speed==Fixed())++r.pushes;
            if(touching&&!u.orders.empty()&&u.speed==Fixed())++r.stuck;
        }
        if(r.half<0&&2*r.arrived>=int(ids.size()))r.half=r.ticks;
        if(r.most<0&&10*r.arrived>=9*int(ids.size()))r.most=r.ticks;
    }
    for(int id:ids)check(f.legal(id),"illegal footprint");
    r.hash=f.world.stateHash();
    r.holds=legion?f.world.legionStats().holds:0;r.spins=motion.spins;r.reversals=motion.reversals;
    std::printf("landedflyers %s layout=%d foot=%d arrived=%d/%zu ticks=%d half=%d p90=%d holds=%llu pushes=%llu stuck=%llu overlap=%llu spins=%llu reversals=%llu\n",
        legion?"legion":"retail",layout,flyerFoot,r.arrived,ids.size(),r.ticks,r.half,r.most,(unsigned long long)r.holds,(unsigned long long)r.pushes,(unsigned long long)r.stuck,
        (unsigned long long)r.overlap,(unsigned long long)r.spins,(unsigned long long)r.reversals);
    if(std::getenv("STATIC_VERBOSE"))printLeft(f,ids);
    return r;
}
void landedflyers() {
    const bool report=std::getenv("STATIC_REPORT")!=nullptr;
    if(std::getenv("FLYER_RETAIL")) {landedflyersRun(0,false,2);landedflyersRun(1,false,2);}
    const auto open=landedflyersRun(1,true,2);
    // Before the fix (flyers missing from Legion's view of the ground):
    // 23/40 and 4/40 arrived in 5000 ticks, 64164 and 99588 member-ticks
    // standing against a flyer. After: as for the same layout of idle
    // ground bodies (FLYER_AS_GROUND), 2631 ticks, 2536 of those ticks
    // (the stream filing along the block's face).
    for(int foot:{2,3}) {
        const auto r=landedflyersRun(0,true,foot);
        if(report)continue;
        check(r.overlap==0,"ground body overlapped a landed flyer");
        check(r.arrived==40,"group did not get round the landed flyers");
        check(r.spins==0,"group spun at the landed flyers");
        check(r.pushes<=40,"group pushed into the landed flyers");
        check(r.stuck<=6000,"group stood against the landed flyers");
        check(r.ticks<=open.ticks*2,"group took far longer than the way round");
    }
    // Before: 31/40 in 5000 ticks (the scattered flyers held the rest).
    const auto off=landedflyersRun(2,true,2);
    if(report)return;
    check(landedflyersRun(2,true,2,false).hash==off.hash,"workers run differs from the serial run");
    check(off.overlap==0&&off.arrived==40&&off.spins==0,"group failed after the flyers took off");
    check(off.half<=open.half*5/4&&off.ticks<=open.ticks*2,"group did not use the ground the flyers left");
}

// Idle landed flyers make way for an allied ground group (World::
// requestLegionLift): 12 landed flyers stand on the straight way of 40
// ground bodies. mode 0: own flyers -- they lift, the group walks under
// them as on open ground, and they land again on the spots they left;
// 1: an enemy's -- they stay landed obstacles; 2: own flyers on guard
// orders (a guard within reach stays landed) -- they do not lift; 3: own
// flyers ON the group's destination -- they lift, the group settles there,
// and they land nearby once, without bobbing; 4: no flyers (reference).
struct LiftRun {uint64_t hash=0;int arrived=0,ticks=0,half=-1,lifted=0,takeoffs=0,maxTakeoffs=0,landed=0,home=0;
                uint64_t overlap=0,flyerOverlap=0,spins=0,detour=0,lifts=0;};
LiftRun liftflyersRun(int mode,bool serial=true) {
    Fixture f(200,100,serial);f.publish();
    const auto type=mover(2);
    UnitType flyer{};flyer.id=flyer.name="legion-flyer";
    flyer.canFly=flyer.canMove=true;flyer.maxHp=100;flyer.footX=flyer.footZ=2;flyer.sight=4096;
    flyer.maxVel=Fixed::fromInt(4);flyer.accel=flyer.brake=Fixed::fromInt(1);flyer.turnRate=1200;flyer.cruiseAlt=80;flyer.buildTime=1;
    std::vector<int> flyers,ids;
    const int owner=mode==1?1:0;
    if(mode!=4)for(int i=0;i<12;++i)flyers.push_back(f.spawn(flyer,mode==3?150+(i%4)*4:70+(i%4)*3,mode==3?44+(i/4)*4:46+(i/4)*3,owner));
    // Guards: a post inside the flyers' reach (70 px) keeps them landed.
    const int post=mode==2?f.spawn(type,64,40,0):0;
    for(int i=0;i<40;++i)ids.push_back(f.spawn(type,20+(i%8)*3,44+(i/8)*3));
    f.start();
    for(int t=0;t<90;++t)f.world.tick(1.f/30);
    for(int id:flyers)check(f.world.unit(id)->flightGroundMode==1,"flyer not landed");
    if(mode==2) {
        for(int id:flyers)f.world.guard(id,post,false);
        for(int t=0;t<30;++t)f.world.tick(1.f/30);
    }
    std::vector<std::pair<int,int>> spots;
    for(int id:flyers) {const auto& v=*f.world.unit(id);spots.push_back({footprintOrigin(v.x,2),footprintOrigin(v.z,2)});}
    std::vector<uint8_t> mode0(flyers.size(),1);std::vector<int> takeoffs(flyers.size(),0);
    for(int id:ids)f.world.order(id,160*16,50*16,false);
    Motion motion;LiftRun r;
    std::vector<int32_t> startZ;for(int id:ids)startZ.push_back(f.world.unit(id)->z.v);
    int done=-1;
    for(;r.ticks<6000;++r.ticks) {
        f.world.tick(1.f/30);motion.observe(f.world,ids);
        r.arrived=0;
        for(size_t k=0;k<ids.size();++k) {
            const auto& u=*f.world.unit(ids[k]);
            r.arrived+=u.orders.empty();
            // Detour: how far a body strays across the straight way (cells).
            r.detour=std::max<uint64_t>(r.detour,uint64_t(std::abs(int64_t(u.z.v)-startZ[k])>>20));
            const int ux=footprintOrigin(u.x,2),uz=footprintOrigin(u.z,2);
            for(int fid:flyers) {
                const auto& v=*f.world.unit(fid);
                if(v.flightGroundMode!=1)continue;
                const int vx=footprintOrigin(v.x,2),vz=footprintOrigin(v.z,2);
                if(ux<vx+2&&vx<ux+2&&uz<vz+2&&vz<uz+2)++r.overlap;
            }
        }
        for(size_t a=0;a<flyers.size();++a) {
            const auto& v=*f.world.unit(flyers[a]);
            if(mode0[a]==1&&v.flightGroundMode==2)++takeoffs[a];
            if(std::getenv("LIFT_TRACE")&&mode0[a]!=v.flightGroundMode)std::printf("  t=%d flyer %d mode %d->%d at %d,%d lift=%d arrived=%d\n",r.ticks,flyers[a],mode0[a],v.flightGroundMode,footprintOrigin(v.x,2),footprintOrigin(v.z,2),int(v.legionLift),r.arrived);
            mode0[a]=v.flightGroundMode;
            if(v.flightGroundMode!=1)continue;
            for(size_t b=a+1;b<flyers.size();++b) {
                const auto& o=*f.world.unit(flyers[b]);
                if(o.flightGroundMode!=1)continue;
                const int ax=footprintOrigin(v.x,2),az=footprintOrigin(v.z,2),bx=footprintOrigin(o.x,2),bz=footprintOrigin(o.z,2);
                if(ax<bx+2&&bx<ax+2&&az<bz+2&&bz<az+2)++r.flyerOverlap;
            }
        }
        if(r.half<0&&2*r.arrived>=int(ids.size()))r.half=r.ticks;
        if(r.arrived==int(ids.size())&&done<0)done=r.ticks;
        // After the group is in, let lifted flyers land again (quiet time,
        // then a descent), then stop.
        if(done>=0&&r.ticks>=done+600)break;
    }
    if(done>=0)r.ticks=done;
    for(int id:ids)check(f.legal(id),"illegal footprint");
    for(size_t a=0;a<flyers.size();++a) {
        const auto& v=*f.world.unit(flyers[a]);
        r.lifted+=takeoffs[a]>0;r.takeoffs+=takeoffs[a];r.maxTakeoffs=std::max(r.maxTakeoffs,takeoffs[a]);
        r.landed+=v.flightGroundMode==1;
        r.home+=v.flightGroundMode==1&&footprintOrigin(v.x,2)==spots[a].first&&footprintOrigin(v.z,2)==spots[a].second;
    }
    r.hash=f.world.stateHash();r.spins=motion.spins;r.lifts=f.world.legionStats().lifts;
    std::printf("liftflyers mode=%d arrived=%d/%zu ticks=%d half=%d detour=%llu lifted=%d takeoffs=%d max=%d landed=%d home=%d/%zu overlap=%llu flyer_overlap=%llu spins=%llu lifts=%llu\n",
        mode,r.arrived,ids.size(),r.ticks,r.half,(unsigned long long)r.detour,r.lifted,r.takeoffs,r.maxTakeoffs,r.landed,r.home,flyers.size(),
        (unsigned long long)r.overlap,(unsigned long long)r.flyerOverlap,(unsigned long long)r.spins,(unsigned long long)f.world.legionStats().lifts);
    return r;
}
void liftflyers() {
    const bool report=std::getenv("STATIC_REPORT")!=nullptr;
    const auto open=liftflyersRun(4);
    const auto own=liftflyersRun(0);
    const auto enemy=liftflyersRun(1);
    const auto guards=liftflyersRun(2);
    const auto onGoal=liftflyersRun(3);
    if(report)return;
    // Own flyers: all lift, the group walks through as on open ground, and
    // every flyer lands again on its own spot, once.
    check(own.arrived==40&&own.overlap==0&&own.flyerOverlap==0&&own.spins==0,"group failed under lifting flyers");
    check(own.lifted==12&&own.maxTakeoffs==1,"own idle flyers did not lift exactly once");
    check(own.landed==12&&own.home==12,"lifted flyers did not land again on their spots");
    check(own.ticks<=open.ticks*11/10&&own.half<=open.half*11/10&&own.detour<=open.detour+1,"group detoured round flyers that lift");
    // An enemy's flyers never lift: obstacles, planned and steered round.
    check(enemy.lifted==0&&enemy.landed==12,"enemy flyers lifted");
    check(enemy.arrived==40&&enemy.overlap==0&&enemy.spins==0,"group failed round enemy flyers");
    check(enemy.detour>open.detour,"group did not go round the enemy flyers");
    // Flyers with orders never lift (guards go their own way; Legion never
    // asks one of them).
    check(guards.lifts==0,"flyers with orders were lifted");
    check(guards.arrived==40&&guards.overlap==0,"group failed round guarding flyers");
    // Flyers on the destination: lift once, land nearby, no bobbing.
    check(onGoal.arrived==40&&onGoal.overlap==0&&onGoal.flyerOverlap==0,"group failed on the flyers' spots");
    check(onGoal.lifted>0&&onGoal.maxTakeoffs<=1,"flyers on the destination bobbed");
    check(onGoal.landed==12,"flyers on the destination did not land again");
    check(liftflyersRun(0,false).hash==own.hash,"workers run differs from the serial run");
}

void replace() {
    Fixture f(128,96);
    f.rect(60,0,4,40);f.rect(60,48,4,48);
    f.publish();
    const auto type=mover(2);
    std::vector<int> ids;
    for(int i=0;i<30;++i)ids.push_back(f.spawn(type,10+(i%6)*3,30+(i/6)*3));
    f.start();
    uint32_t r=99;
    for(int round=0;round<25;++round) {
        for(int id:ids) {
            r=r*1664525u+1013904223u;
            f.world.order(id,float((8+int(r>>8)%110)*16),float((8+int(r>>20)%80)*16),round%3==2);
        }
        for(int t=0;t<7;++t)f.world.tick(1.f/30);
    }
    for(size_t i=0;i<ids.size();++i)f.world.order(ids[i],float((96+int(i%6)*4)*16),float((30+int(i/6)*4)*16),false);
    for(int t=0;t<4000;++t)f.world.tick(1.f/30);
    int arrived=0;for(int id:ids)arrived+=f.world.unit(id)->orders.empty();
    const auto s=f.world.legionStats();
    std::printf("replace arrived=%d groups=%llu bytes=%zu\n",arrived,(unsigned long long)s.groups,s.bytes);
    check(arrived==int(ids.size()),"units lost after rapid replacement");
    check(s.bytes<4u*1024*1024,"stale replacement state retained");
}

void unreachable() {
    Fixture f(96,64);
    f.rect(48,0,3,64);   // full wall
    f.publish();
    const auto type=mover(2);
    std::vector<int> ids;
    for(int i=0;i<16;++i)ids.push_back(f.spawn(type,10+(i%4)*3,20+(i/4)*3));
    f.start();
    for(int id:ids)f.world.order(id,80*16,30*16,false);
    Motion motion;
    for(int t=0;t<9100;++t) {f.world.tick(1.f/30);motion.observe(f.world,ids);}
    int cleared=0;for(int id:ids)cleared+=f.world.unit(id)->orders.empty();
    std::printf("unreachable cleared=%d spins=%llu\n",cleared,(unsigned long long)motion.spins);
    check(cleared==int(ids.size()),"unreachable orders not retired");
    check(motion.spins==0,"unreachable units spun");
}

void slotblock() {
    // A shared-point order whose arrival slots are covered mid-approach (a
    // wall stands in for a corpse or new building): no member may claim a
    // covered slot, so every order completes or retires -- no livelock of
    // claim, re-register, claim -- and nobody spins.
    Fixture f(96,64);
    f.publish();
    const auto type=mover(2);
    std::vector<int> ids;
    for(int i=0;i<16;++i)ids.push_back(f.spawn(type,8+(i%4)*3,20+(i/4)*3));
    f.start();
    for(int id:ids)f.world.order(id,60*16,32*16,false);
    Motion motion;
    int done=-1;
    for(int t=0;t<4000;++t) {
        if(t==150) {f.rect(61,30,2,5);f.rect(57,35,4,1);f.publish();}
        f.world.tick(1.f/30);motion.observe(f.world,ids);
        int left=0;for(int id:ids)left+=!f.world.unit(id)->orders.empty();
        if(!left&&done<0)done=t;
    }
    int cleared=0;for(int id:ids)cleared+=f.world.unit(id)->orders.empty();
    std::printf("slotblock cleared=%d at=%d spins=%llu reversals=%llu\n",cleared,done,
        (unsigned long long)motion.spins,(unsigned long long)motion.reversals);
    printLeft(f,ids);
    check(cleared==int(ids.size()),"shared-point orders livelocked on covered slots");
    for(int id:ids)check(f.legal(id),"illegal footprint after slot cover");
    check(motion.spins==0,"units spun around covered slots");
}

void quota() {
    // Many independent single-unit orders on a large map exceed the field
    // cap: evicted fields rebuild deterministically and nobody starves.
    Fixture f(320,320);
    f.rect(150,0,4,150);f.rect(150,170,4,150);
    f.publish();
    const auto type=mover(2);
    std::vector<int> ids;
    for(int i=0;i<64;++i)ids.push_back(f.spawn(type,10+(i%8)*4,10+(i/8)*4));
    f.start();
    for(size_t i=0;i<ids.size();++i) {
        f.world.order(ids[i],float((200+int(i%8)*12)*16),float((40+int(i/8)*30)*16),false);
        f.world.tick(1.f/30);   // a separate issue tick: separate groups
    }
    for(int t=0;t<9000;++t)f.world.tick(1.f/30);
    int arrived=0;for(int id:ids)arrived+=f.world.unit(id)->orders.empty();
    const auto s=f.world.legionStats();
    std::printf("quota arrived=%d fields=%llu evictions=%llu bytes=%zu\n",arrived,(unsigned long long)s.fieldsBuilt,
        (unsigned long long)s.fieldEvictions,(unsigned long long)s.bytes);
    check(arrived==int(ids.size()),"orders starved under the field cap");
    check(s.fieldEvictions>0,"fixture did not exercise eviction");
}

void pens() {
    // More independent orders than the field budget holds whole-map fields,
    // each penned in its own small walled region behind a wall stub: fields
    // are sized to their region, so nobody is evicted mid-route or starved.
    constexpr int P=24,N=12;   // pen size (cells), pens per side
    Fixture f(P*N,P*N);
    std::vector<int> ids;std::vector<std::pair<int,int>> goals;
    const auto type=mover(2);
    for(int j=0;j<N;++j)for(int i=0;i<N;++i) {
        const int x=i*P,z=j*P;
        f.rect(x,z,P,1);f.rect(x,z+P-1,P,1);f.rect(x,z,1,P);f.rect(x+P-1,z,1,P);
        f.rect(x+P/2,z+4,2,P-8);
    }
    f.publish();
    for(int j=0;j<N;++j)for(int i=0;i<N;++i) {ids.push_back(f.spawn(type,i*P+5,j*P+P/2));goals.push_back({i*P+P-5,j*P+P/2});}
    f.start();
    for(size_t i=0;i<ids.size();++i) {
        f.world.order(ids[i],float(goals[i].first*16),float(goals[i].second*16),false);
        f.world.tick(1.f/30);   // a separate issue tick: separate groups
    }
    int arrived=0,all=-1;
    for(int t=0;t<3000&&all<0;++t) {
        f.world.tick(1.f/30);
        arrived=0;for(int id:ids)arrived+=f.world.unit(id)->orders.empty();
        if(arrived==int(ids.size()))all=t;
    }
    const auto s=f.world.legionStats();
    std::printf("pens all_arrived_tick=%d arrived=%d/%zu fields=%llu evictions=%llu bytes=%zu\n",all,arrived,ids.size(),(unsigned long long)s.fieldsBuilt,
        (unsigned long long)s.fieldEvictions,(unsigned long long)s.bytes);
    check(ids.size()>48,"fixture does not exceed the whole-map field cap");
    check(arrived==int(ids.size()),"penned orders starved under the field budget");
    check(s.fieldEvictions==0,"small-region fields were evicted");
    // Every group gets its field at once (242 ticks); waiting for whole-map
    // field slots in waves took 609.
    check(all>=0&&all<400,"penned orders waited for field slots");
    for(int id:ids)check(f.legal(id),"illegal footprint in pens");
}

void formation() {
    // Open ground, a formation sent far (beyond the direct-line reach) to the
    // same lattice translated: every body should travel its own row, so the
    // army keeps its shape and nobody crosses into a neighbour's lane.
    Fixture f(480,128);
    f.publish();
    const auto type=mover(2);
    std::vector<int> ids,rows;
    for(int i=0;i<200;++i) {ids.push_back(f.spawn(type,32+(i/15)*3,36+(i%15)*3));rows.push_back(36+(i%15)*3);}
    f.start();
    for(size_t i=0;i<ids.size();++i)f.world.order(ids[i],float((420+int(i/15)*3)*16),float(rows[i]*16),false);
    float drift=0;
    for(int t=0;t<9000;++t) {
        f.world.tick(1.f/30);
        for(size_t i=0;i<ids.size();++i)drift=std::max(drift,std::abs(f.world.unit(ids[i])->z.toFloat()/16-float(rows[i])));
    }
    int arrived=0;for(int id:ids)arrived+=f.world.unit(id)->orders.empty();
    printLeft(f,ids);
    std::printf("formation arrived=%d/%zu max_row_drift_cells=%.2f\n",arrived,ids.size(),drift);
    check(arrived==int(ids.size()),"formation did not arrive on open ground");
    check(drift<=1.0f,"formation members left their lanes on open ground");
}

uint64_t scenario(bool serial) {
    Fixture f(128,64,serial);
    f.rect(60,0,4,28);f.rect(60,34,4,30);
    f.publish();
    std::vector<UnitType> types{mover(2),mover(3)};
    std::vector<int> ids;
    for(int i=0;i<60;++i)ids.push_back(f.spawn(types[size_t(i%2)],8+(i%10)*4,8+(i/10)*6,i%2));
    f.start();
    for(size_t i=0;i<ids.size();++i)f.world.order(ids[i],float((100-int(i%10)*3)*16),float((12+int(i/10)*6)*16),false);
    for(int t=0;t<1500;++t)f.world.tick(1.f/30);
    return f.world.stateHash();
}
// Units killed mid-move (hp to zero) or stripped of their orders without a
// cancel leave Legion: groups, members and point claims shrink to nothing, and the
// survivors still arrive.
void deathsshared() {
    // Half of a shared-point group dies mid-approach (corpses can land on
    // arrival slots): every survivor must still settle, with no spinning,
    // and Legion's containers must empty.
    Fixture f(160,64);
    f.rect(70,0,4,28);f.rect(70,36,4,28);
    f.publish();
    const auto type=mover(2);
    std::vector<int> ids;
    for(int i=0;i<40;++i)ids.push_back(f.spawn(type,10+(i%8)*3,14+(i/8)*3));
    f.start();
    for(int id:ids)f.world.order(id,126.f*16,32.f*16,false);
    for(int t=0;t<60;++t)f.world.tick(1.f/30);
    std::vector<int> live;
    for(size_t i=0;i<ids.size();++i) {if(i%2)f.world.unit(ids[i])->hp=Fixed();else live.push_back(ids[i]);}
    Motion motion;
    for(int t=0;t<6000;++t) {f.world.tick(1.f/30);motion.observe(f.world,live);}
    int arrived=0;for(int id:live)arrived+=f.world.unit(id)->orders.empty();
    const auto e=f.world.legionStats();
    std::printf("deathsshared arrived=%d/%zu members=%zu groups=%zu spins=%llu\n",arrived,live.size(),
        e.liveMembers,e.liveGroups,(unsigned long long)motion.spins);
    printLeft(f,live);
    check(arrived==int(live.size()),"shared-point survivors did not arrive");
    for(int id:live)check(f.legal(id),"illegal survivor footprint");
    check(motion.spins==0,"shared-point survivors spun");
    check(e.liveMembers==0&&e.liveGroups==0,"Legion containers did not empty");
}

void deaths() {
    Fixture f(160,64);
    f.rect(70,0,4,28);f.rect(70,36,4,28);
    f.publish();
    const auto type=mover(2);
    std::vector<int> ids;
    for(int i=0;i<40;++i)ids.push_back(f.spawn(type,10+(i%8)*3,14+(i/8)*3));
    f.start();
    // Distinct goals: shared-point slots under fresh corpses are a separate
    // concern (arrival slots), not membership.
    for(size_t i=0;i<ids.size();++i)f.world.order(ids[i],float((110+(int(i)%8)*4)*16),float((12+int(i)/8*4)*16),false);
    for(int t=0;t<60;++t)f.world.tick(1.f/30);
    std::vector<int> live;
    for(size_t i=0;i<ids.size();++i) {
        if(i%2) {f.world.unit(ids[i])->hp=Fixed();} else live.push_back(ids[i]);
    }
    // Orders cleared without a cancel (a path that forgets cancelPath): the
    // member is stale and must be pruned too.
    for(size_t i=0;i<ids.size();i+=4)f.world.unit(ids[i])->orders.clear();
    for(int t=0;t<120;++t)f.world.tick(1.f/30);
    for(size_t i=1;i<ids.size();i+=2) {
        const auto* u=f.world.unit(ids[i]);
        check(!u||!u->alive(),"hp=0 did not kill the unit");
    }
    auto mid=f.world.legionStats();
    std::printf("deaths mid members=%zu groups=%zu points=%zu\n",mid.liveMembers,mid.liveGroups,mid.livePoints);
    size_t moving=0;for(int id:live)moving+=!f.world.unit(id)->orders.empty();
    check(mid.liveMembers<=moving,"dead or orderless units are still Legion members");
    for(int t=0;t<6000;++t)f.world.tick(1.f/30);
    int arrived=0;for(int id:live)arrived+=f.world.unit(id)->orders.empty();
    const auto e=f.world.legionStats();
    std::printf("deaths arrived=%d/%zu members=%zu groups=%zu points=%zu fields=%zu\n",arrived,live.size(),
        e.liveMembers,e.liveGroups,e.livePoints,e.liveFields);
    printLeft(f,live);
    check(arrived==int(live.size()),"survivors did not arrive");
    check(e.liveMembers==0&&e.liveGroups==0&&e.livePoints==0&&e.liveFields==0,"Legion containers did not empty");
}

// One command sends two units to goals within the cluster radius, one inside
// a sealed pocket. They must not share a field: the cut-off member is trapped
// and retires; it never descends to its teammate's seed and holds forever.
void splitgoal() {
    Fixture f(64,64);
    f.rect(30,10,10,1);f.rect(30,19,10,1);f.rect(30,10,1,10);f.rect(39,10,1,10);
    f.publish();
    const auto type=mover(2);
    const int a=f.spawn(type,10,14),b=f.spawn(type,10,20);
    f.start();
    f.world.order(a,34*16,14*16,false);   // inside the pocket
    f.world.order(b,44*16,14*16,false);   // outside, 10 cells away
    int cleared=-1;
    for(int t=0;t<9200;++t) {
        f.world.tick(1.f/30);
        if(cleared<0&&f.world.unit(a)->orders.empty())cleared=t;
    }
    std::printf("splitgoal cut-off cleared_at=%d other_arrived=%d\n",cleared,int(f.world.unit(b)->orders.empty()));
    check(f.world.unit(b)->orders.empty(),"reachable member did not arrive");
    check(cleared>=0,"cut-off member never retired");
}

// A click deep inside a blocked mass (beyond the near goal search) walks to
// the nearest reachable legal point, as Retail does, instead of trapping.
void farclick() {
    Fixture f(128,64);
    f.rect(60,0,68,64);
    f.publish();
    const auto type=mover(2);const int id=f.spawn(type,10,30);
    f.start();
    f.world.order(id,110*16,30*16,false);
    int cleared=-1;
    for(int t=0;t<3000&&cleared<0;++t) {f.world.tick(1.f/30);if(f.world.unit(id)->orders.empty())cleared=t;}
    const auto& u=*f.world.unit(id);
    std::printf("farclick cleared_at=%d x=%.1f trapped=%llu\n",cleared,u.x.toFloat()/16,(unsigned long long)f.world.legionStats().trapped);
    check(cleared>=0&&u.x.toFloat()/16>50,"far click did not walk to the nearest reachable point");
}

// A goal behind a full wall, from a region with room to move: the body walks
// to the region's nearest point to the goal, then holds there -- order kept,
// zero speed, constant heading, no pushing against the wall.
void approachhold() {
    Fixture f(96,64);
    f.rect(48,0,3,64);
    f.publish();
    const auto type=mover(2);const int id=f.spawn(type,10,30);
    f.start();
    f.world.order(id,80*16,30*16,false);
    Motion motion;
    int stillFrom=-1;int32_t x=f.world.unit(id)->x.v,z=f.world.unit(id)->z.v,heading=f.world.unit(id)->heading.v;
    uint64_t movedAfter=0,turnedAfter=0;
    for(int t=0;t<3000;++t) {
        f.world.tick(1.f/30);motion.observe(f.world,{id});
        const auto& u=*f.world.unit(id);
        if(stillFrom<0&&f.world.legionNavigator()->unitState(id)==5)stillFrom=t;
        if(stillFrom>=0&&t>stillFrom) {movedAfter+=u.x.v!=x||u.z.v!=z;turnedAfter+=u.heading.v!=heading;}
        x=u.x.v;z=u.z.v;heading=u.heading.v;
    }
    const auto& u=*f.world.unit(id);
    std::printf("approachhold held_at=%d x=%.1f z=%.1f moved_after=%llu turned_after=%llu spins=%llu speed=%d\n",stillFrom,
        u.x.toFloat()/16,u.z.toFloat()/16,(unsigned long long)movedAfter,(unsigned long long)turnedAfter,
        (unsigned long long)motion.spins,u.speed.v);
    check(stillFrom>=0,"approaching unit never held");
    check(u.x.toFloat()/16>44&&u.x.toFloat()/16<48.5f&&std::abs(u.z.toFloat()/16-30)<2,"unit did not walk to the nearest reachable point");
    check(movedAfter==0&&turnedAfter==0&&motion.spins==0,"held unit moved, turned or spun");
    check(u.speed.v==0,"held unit has speed");
    check(!u.orders.empty(),"approach order dropped before the grace period");
    check(f.legal(id),"illegal footprint at the approach point");
}

// The wall comes down while the body holds at its approach point: the kept
// order resumes and the body arrives at the real goal, no re-issued order.
void approachopen() {
    Fixture f(96,64);
    f.rect(48,0,3,64);
    f.publish();
    const auto type=mover(2);const int id=f.spawn(type,10,30);
    f.start();
    f.world.order(id,80*16,30*16,false);
    for(int t=0;t<1500;++t)f.world.tick(1.f/30);
    const float heldX=f.world.unit(id)->x.toFloat()/16;
    check(heldX>44&&!f.world.unit(id)->orders.empty(),"unit did not approach the wall with its order kept");
    f.open(48,0,3,64);f.publish();
    int arrived=-1;
    for(int t=0;t<1500&&arrived<0;++t) {f.world.tick(1.f/30);if(f.world.unit(id)->orders.empty())arrived=t;}
    const auto& u=*f.world.unit(id);
    std::printf("approachopen held_x=%.1f arrived_after=%d at %.1f,%.1f\n",heldX,arrived,u.x.toFloat()/16,u.z.toFloat()/16);
    check(arrived>=0&&arrived<600,"unit did not resume promptly after the wall opened");
    check(std::abs(u.x.toFloat()-80*16)<20&&std::abs(u.z.toFloat()-30*16)<20,"resumed unit did not reach its real goal");
}

// Static changes away from a moving group (a corpse-like cell toggled every
// few ticks) must not drop its finished field: no member goes back to
// waiting for a field, and the group still arrives.
void churn() {
    Fixture f(160,64);
    f.rect(70,0,4,28);f.rect(70,36,4,28);
    f.publish();
    const auto type=mover(2);
    std::vector<int> ids;
    for(int i=0;i<24;++i)ids.push_back(f.spawn(type,10+(i%8)*3,14+(i/8)*3));
    f.start();
    for(size_t i=0;i<ids.size();++i)f.world.order(ids[i],float((110+(int(i)%8)*4)*16),float((12+int(i)/8*4)*16),false);
    auto* legion=f.world.legionNavigator();
    bool ready=false;int waits=0,arrived=0;
    for(int t=0;t<6000;++t) {
        if(t%5==0)f.world.blockCells(150,60,1,1,(t/5)%2==0);
        f.world.tick(1.f/30);
        if(!ready&&f.world.legionStats().fieldsBuilt>0)ready=true;
        else if(ready)for(int id:ids)waits+=legion->unitState(id)==3;
    }
    for(int id:ids)arrived+=f.world.unit(id)->orders.empty();
    std::printf("churn arrived=%d waits=%d planes=%llu\n",arrived,waits,(unsigned long long)f.world.legionStats().planeBuilds);
    check(arrived==int(ids.size()),"group did not arrive under static churn");
    check(waits==0,"members lost their field to an unrelated static change");
}

// Constant static churn on a large map (a cell toggled EVERY tick, as
// corpses and burning features do in a battle) while two classes move: the
// plane rebuild debt must stay bounded, so a group ordered during the churn
// still gets a field and moves (its goal is behind a wall, not in line).
void churnfield() {
    Fixture f(1024,1024);
    f.rect(0,600,900,4);
    f.publish();
    const auto small=mover(1),large=mover(2);
    std::vector<int> movers;
    for(int i=0;i<4;++i) {movers.push_back(f.spawn(small,10+i*3,100));movers.push_back(f.spawn(large,10+i*4,200));}
    const int late=f.spawn(large,50,500);
    f.start();
    for(size_t i=0;i<movers.size();++i)f.world.order(movers[i],float((1000-int(i)*4)*16),float((100+int(i)%2*100)*16),false);
    for(int t=0;t<30;++t)f.world.tick(1.f/30);
    auto* legion=f.world.legionNavigator();
    const int32_t x0=f.world.unit(late)->x.v,z0=f.world.unit(late)->z.v;
    int fieldAt=-1;
    for(int t=0;t<330;++t) {
        f.world.blockCells(1010,1010,1,1,t%2==0);
        if(t==30)f.world.order(late,50*16,700*16,false);
        f.world.tick(1.f/30);
        if(t>30&&fieldAt<0&&legion->fieldPotential(late,footprintOrigin(f.world.unit(late)->x,2),
                                                     footprintOrigin(f.world.unit(late)->z,2))>=0)fieldAt=t-30;
    }
    const auto& u=*f.world.unit(late);
    const int64_t dx=int64_t(u.x.v-x0)>>16,dz=int64_t(u.z.v-z0)>>16;
    std::printf("churnfield field_after=%d moved_px=%lld planes=%llu state=%d\n",fieldAt,(long long)isqrt64(uint64_t(dx*dx+dz*dz)),
        (unsigned long long)f.world.legionStats().planeBuilds,legion->unitState(late));
    check(fieldAt>=0,"group ordered during constant churn never got a field");
    check(dx*dx+dz*dz>=64*64,"group ordered during constant churn did not move");
}

// An approach member under unrelated static churn (a far cell toggled every
// 10 ticks) keeps its walk and its clock: it still reaches the approach
// point, holds, and retires after the grace period counted from the order.
void approachchurn() {
    Fixture f(96,64);
    f.rect(48,0,3,64);
    f.publish();
    const auto type=mover(2);
    const int id=f.spawn(type,10,30);
    f.start();
    f.world.order(id,80*16,30*16,false);
    const uint64_t registrations=f.world.legionStats().registrations;
    int cleared=-1;
    for(int t=0;t<9300&&cleared<0;++t) {
        if(t%10==0)f.world.blockCells(2,60,1,1,(t/10)%2==0);
        f.world.tick(1.f/30);
        if(f.world.unit(id)->orders.empty())cleared=t;
    }
    const uint64_t again=f.world.legionStats().registrations-registrations;
    std::printf("approachchurn cleared_at=%d reregistrations=%llu\n",cleared,(unsigned long long)again);
    check(cleared>=8950&&cleared<=9100,"approach order not retired after the grace period under churn");
    check(again<=2,"approach member re-registered on unrelated static changes");
}

// A legacy terrain-only world (no placement plane): a member blocked in a
// corridor by a settled same-player arrival asks it to yield. The yield's
// legality test must not need the placement plane (it threw before).
void legacyyield() {
    Fixture f(64,32);
    f.rect(4,11,56,1);f.rect(4,15,56,1);   // corridor rows 12..14, no publish()
    const auto type=mover(2);
    const int settled=f.spawn(type,20,13),walker=f.spawn(type,8,13);
    f.start();
    f.world.order(settled,30*16,13*16,false);
    for(int t=0;t<300;++t)f.world.tick(1.f/30);
    check(f.world.unit(settled)->orders.empty(),"settler did not arrive");
    f.world.order(walker,50*16,13*16,false);
    for(int t=0;t<600;++t)f.world.tick(1.f/30);
    const auto& u=*f.world.unit(walker);
    std::printf("legacyyield walker x=%.1f orders=%zu slides=%llu\n",u.x.toFloat()/16,u.orders.size(),
        (unsigned long long)f.world.legionStats().slides);
    check(u.x.toFloat()/16>20,"walker did not reach the settled body");
}

// Legacy nav-grid world (no placement plane): bodies sent past the end of a
// blocked wall must round it, alone and as a queued column. The proven line
// passed the wall's corner a fraction of a pixel away; one update's step
// jumped both origins at once onto the illegal side cell, was refused, and
// the body held there forever.
void wallendRun(int count) {
    Fixture f(64,64);
    f.rect(0,30,40,1);   // wall row 30, open end at x>=40; no publish()
    const auto type=mover(2);
    std::vector<int> ids;
    for(int i=0;i<count;++i)ids.push_back(f.spawn(type,10+(i%4)*3,40+(i/4)*3));
    f.start();
    for(int id:ids)f.world.order(id,12*16,18*16,false);
    Motion motion;
    int done=-1;
    for(int t=0;t<3000;++t) {
        f.world.tick(1.f/30);motion.observe(f.world,ids);
        bool all=true;for(int id:ids)all=all&&f.world.unit(id)->orders.empty();
        if(all){done=t;break;}
    }
    int left=0;for(int id:ids)left+=!f.world.unit(id)->orders.empty();
    std::printf("wallend count=%d done=%d left=%d spins=%llu reversals=%llu\n",count,done,left,
        (unsigned long long)motion.spins,(unsigned long long)motion.reversals);
    if(left) {
        printLeft(f,ids);
        auto* n=f.world.legionNavigator();
        for(int id:ids)if(!f.world.unit(id)->orders.empty()) {
            const auto& u=*f.world.unit(id);
            const int ox=footprintOrigin(u.x,2),oz=footprintOrigin(u.z,2);
            std::printf("   id=%d origin %d,%d pot:",id,ox,oz);
            for(int j=-1;j<=1;++j)for(int i=-1;i<=1;++i)std::printf(" %d",n->fieldPotential(id,ox+i,oz+j));
            std::printf(" staticLegal:");
            for(int j=-1;j<=1;++j)for(int i=-1;i<=1;++i)std::printf("%d",int(n->staticLegal(u,ox+i,oz+j)));
            std::printf("\n");
        }
    }
    check(left==0,"bodies did not round the wall end");
    check(motion.spins==0,"bodies turned in place at the wall end");
}
void wallend() {wallendRun(1);wallendRun(12);}

// Pinwheel: a formation of 48 2x2 bodies on open ground, sent round the
// end of a long wall (a U-turn round its top, to a point beyond it). The
// field's shortest ways all touch the wall end, so without the pinwheel the
// whole group files over it one body wide. With it, the outer files take
// wider concentric arcs and stay abreast: the group crosses the wall's line
// several bodies wide. `gap` > 0 puts a second wall above the end, leaving a
// gap that many cells wide: the files must fold in (a 6-cell gap holds three
// 2-cell files) and take it as before the pinwheel, no slower.
// Measured on the build before the pinwheel (f767ed2), where the group files over
// the end: 1.37 files, 90% round by tick 1939; through the gap 2323.
constexpr int kPinwheelSingleFile90=1939,kPinwheelGap90=2323;
constexpr double kPinwheelFiles=2.5;
struct PinwheelResult {int arrived=0,total=0,rounded90=-1,done=-1;double lanes=0,spread=0;uint64_t spins=0,reversals=0;};
PinwheelResult pinwheelRun(int gap) {
    Fixture f(200,140);
    f.rect(98,48,4,92);                      // the wall: x 98-101, z 48 down to the map edge
    if(gap>0)f.rect(98,0,4,48-gap);          // a second wall above it, leaving a gap
    f.publish();
    const auto type=mover(2);
    std::vector<int> ids;
    for(int i=0;i<48;++i)ids.push_back(f.spawn(type,40+(i%8)*3,96+(i/8)*3));
    f.start();
    for(int id:ids)f.world.order(id,150*16,110*16,false);   // one point: a formation
    Motion motion;
    PinwheelResult r;r.total=int(ids.size());
    int samples=0;double lanes=0,spread=0;
    int t=0;
    for(;t<6000;++t) {
        f.world.tick(1.f/30);motion.observe(f.world,ids);
        int arrived=0,over=0;
        // Bodies passing over the wall's end (above x 97-102): how many
        // 2-cell files they use, and how far they spread above the end.
        std::set<int> files;int lo=1<<30,hi=-1,count=0;
        for(int id:ids) {
            const auto& u=*f.world.unit(id);
            arrived+=u.orders.empty();
            const int x=int(u.x.v>>20),z=int(u.z.v>>20);
            over+=x>=104;
            if(x>=97&&x<=102&&z<48) {files.insert(z/2);lo=std::min(lo,z);hi=std::max(hi,z);++count;}
        }
        if(count>=3) {++samples;lanes+=double(files.size());spread+=double(hi-lo);}
        if(r.rounded90<0&&over*10>=r.total*9)r.rounded90=t;
        r.arrived=arrived;
        if(std::getenv("PINWHEEL_ASCII")&&t%300==0) {
            std::vector<std::string> g(70,std::string(100,'.'));
            for(int z=0;z<70;++z)for(int x=0;x<100;++x)if(f.cells[size_t(z+20)*200+x+50]==0)g[size_t(z)][size_t(x)]='#';
            for(int id:ids) {const auto& u=*f.world.unit(id);const int x=int(u.x.v>>20)-50,z=int(u.z.v>>20)-20;
                if(x>=0&&x<99&&z>=0&&z<69)for(int j=0;j<2;++j)for(int i=0;i<2;++i)g[size_t(z+j)][size_t(x+i)]=u.orders.empty()?'o':char('a'+id%26);}
            std::printf("t=%d\n",t);for(auto& l:g)std::printf("|%s\n",l.c_str());
        }
        if(arrived==r.total) {r.done=t;break;}
    }
    for(int id:ids)check(f.legal(id),"illegal footprint");
    r.lanes=samples?lanes/samples:0;r.spread=samples?spread/samples:0;r.spins=motion.spins;r.reversals=motion.reversals;
    std::printf("pinwheel gap=%d arrived=%d/%d rounded90=%d done=%d files=%.2f spread_cells=%.2f spins=%llu reversals=%llu\n",gap,r.arrived,r.total,
        r.rounded90,r.done,r.lanes,r.spread,(unsigned long long)motion.spins,(unsigned long long)motion.reversals);
    if(std::getenv("PINWHEEL_VERBOSE"))printLeft(f,ids);
    return r;
}
void pinwheel() {
    const auto open=pinwheelRun(0);
    const auto gap=pinwheelRun(6);
    if(std::getenv("PINWHEEL_REPORT"))return;   // measure only (e.g. on an older build)
    check(open.arrived==open.total,"group did not get round the wall end");
    check(open.spins==0,"group spun at the wall end");
    check(open.lanes>=kPinwheelFiles,"group folded into a file at the wall end");
    // Wider arcs are longer, but files abreast drain the end faster than
    // one file: no slower than single file round it.
    check(open.rounded90>=0&&open.rounded90<=kPinwheelSingleFile90,"group rounded the wall end slower than single file");
    check(gap.arrived==gap.total,"group did not get through the gap beside the wall end");
    check(gap.spins==0,"group spun in the gap beside the wall end");
    // A 6-cell gap holds at most three 2-cell files: the files fold in.
    check(gap.spread<=6.0,"group did not fold into the gap");
    check(gap.rounded90>=0&&gap.rounded90<=kPinwheelGap90,"group took longer through the gap than single file");
}

// A member whose own goal lies inside a lattice of settled same-player
// arrivals (2x2 bodies at a 3-cell stride: the 1-cell gaps fit no body). The
// way in exists only if the settled bodies yield. It must ask them to, and
// get there, not plan detours away and shuffle back forever.
void latticeRun(int sx,int sz,int gi,int gj) {
    Fixture f(96,64);
    f.publish();
    const auto type=mover(2);
    std::vector<int> settlers;
    std::vector<std::pair<int,int>> sites;
    for(int j=0;j<7;++j)for(int i=0;i<7;++i) {
        if(i==gi&&j==gj)continue;                    // the walker's slot
        const int x=40+3*i,z=20+3*j;
        sites.push_back({x,z});settlers.push_back(f.spawn(type,x,z));
    }
    const int walker=f.spawn(type,sx,sz);
    f.start();
    for(size_t k=0;k<settlers.size();++k)f.world.order(settlers[k],sites[k].first*16,sites[k].second*16,false);
    for(int t=0;t<200;++t)f.world.tick(1.f/30);
    for(int id:settlers)check(f.world.unit(id)->orders.empty(),"settler did not arrive");
    f.world.order(walker,(40+3*gi)*16,(20+3*gj)*16,false);
    Motion motion;
    int arrivedAt=-1;
    for(int t=0;t<2400&&arrivedAt<0;++t) {
        f.world.tick(1.f/30);
        motion.observe(f.world,{walker});
        if(f.world.unit(walker)->orders.empty())arrivedAt=t;
    }
    const auto& u=*f.world.unit(walker);
    std::printf("lattice arrived_at=%d at %.1f,%.1f reversals=%llu spins=%llu\n",arrivedAt,u.x.toFloat()/16,
        u.z.toFloat()/16,(unsigned long long)motion.reversals,(unsigned long long)motion.spins);
    check(arrivedAt>=0,"walker never reached the lattice slot");
    check(motion.reversals<=4,"walker oscillated against settled arrivals");
    for(int id:settlers)check(f.legal(id),"illegal settler footprint");
}

void lattice() {
    latticeRun(49,50,3,3);
    // Approaches from the side and from below that at 4cc95ba never arrived:
    // the walker planned a detour away, dropped it and shuffled back, every
    // ~450 ticks, without asking the settled bodies to yield.
    latticeRun(75,25,3,4);latticeRun(50,55,3,2);
}

void determinism() {
    const uint64_t a=scenario(true),b=scenario(true),c=scenario(false);
    std::printf("determinism serial=%016llx repeat=%016llx workers=%016llx\n",(unsigned long long)a,(unsigned long long)b,(unsigned long long)c);
    check(a==b,"Legion run is not repeatable");
    check(a==c,"Legion serial and worker hashes differ");
}
}

namespace {
void planeincrementalSeed(uint32_t seed) {
    // Random static churn (features of every size placed over each other,
    // blocking and not, walls that split regions and reopen, structures with
    // yards built and destroyed): the incrementally maintained plane must
    // equal a whole-map rebuild after every change, labels included.
    const int W=96,H=80;
    std::vector<uint8_t> heights(size_t(W)*H);
    for(int z=0;z<H;++z)for(int x=0;x<W;++x)
        heights[size_t(z)*W+x]=uint8_t(std::clamp(100+int((x*7+z*3)%40)-(x>60&&z>50?70:0),0,255));
    Fixture f(W,H,true,heights);
    for(int i=0;i<40;++i)f.wall(10+i,20+(i%3));
    f.publish();
    std::vector<UnitType> types;
    for(int foot=1;foot<=4;++foot) {auto t=mover(foot);t.maxSlope=8+foot*4;types.push_back(t);}
    types.push_back(mover(2));types.back().footX=3;types.back().id=types.back().name="legion-rect";
    UnitType yard{};yard.id=yard.name="legion-yard";yard.maxHp=100;yard.footX=4;yard.footZ=3;yard.buildTime=1;
    yard.yardMap="o..oo..ooooo";
    std::vector<int> ids;
    for(auto& t:types)ids.push_back(f.spawn(t,2,2));
    f.start();
    auto* legion=f.world.legionNavigator();
    check(legion!=nullptr,"legion navigator not created in Legion mode");
    uint32_t r=seed;
    auto rnd=[&](int n) {r=r*1103515245u+12345u;return int((r>>8)%uint32_t(n));};
    std::vector<int> structures;
    const uint64_t builds0=f.world.legionStats().planeBuilds;
    for(int step=0;step<400;++step) {
        const int kind=rnd(10);
        if(kind<5) {
            const int fx=1+rnd(3),fz=1+rnd(3),x=4+rnd(W-10),z=4+rnd(H-10);
            f.world.addFeature(z*W+x,float(x*16+fx*8),float(z*16+fz*8),0,1,fx,fz,rnd(3)!=0,-1,true);
        } else if(kind<7) {
            // A wall line across the map (splits), later overwritten open.
            const bool across=rnd(2),blocks=rnd(3)!=0;const int at=6+rnd(across?H-12:W-12);
            for(int i=0;i<(across?W:H)-4;++i) {
                const int x=across?2+i:at,z=across?at:2+i;
                f.world.addFeature(z*W+x,float(x*16+8),float(z*16+8),0,1,1,1,blocks,-1,true);
            }
        } else if(kind<8) {
            const int x=6+rnd(W-14),z=6+rnd(H-14);
            structures.push_back(f.world.spawn(&yard,float(x*16+32),float(z*16+24),std::nullopt,1));
        } else if(kind<9&&!structures.empty()) {
            const size_t i=size_t(rnd(int(structures.size())));
            f.world.destroy(structures[i]);structures.erase(structures.begin()+long(i));
        } else f.world.tick(1.f/30);
        if(step%3==0||kind>=9)for(int id:ids)
            check(legion->planeMatchesRebuild(*f.world.unit(id)),"incremental plane differs from a rebuild at step "+
                std::to_string(step)+" for "+f.world.unit(id)->type->id);
    }
    const auto stats=f.world.legionStats();
    std::printf("planeincremental refreshes=%llu relabels=%llu\n",(unsigned long long)stats.planeRefreshes,
        (unsigned long long)stats.planeRelabels);
    (void)builds0;
    check(stats.planeRefreshes>50,"churn never exercised the incremental path");
}
void planeincremental() {for(uint32_t seed:{987654321u,7u,42u,2026u})planeincrementalSeed(seed);}
}

namespace {
void planeprebuild() {
    // Planes of classes already on the map build in the background, a
    // bounded slice per tick, while features change under them; the result
    // must equal a whole-map rebuild, and no synchronous build may happen.
    const int W=384,H=320;
    std::vector<uint8_t> heights(size_t(W)*H);
    for(int z=0;z<H;++z)for(int x=0;x<W;++x)
        heights[size_t(z)*W+x]=uint8_t(std::clamp(100+int((x*7+z*3)%40)-(x>260&&z>200?70:0),0,255));
    Fixture f(W,H,true,heights);
    for(int i=0;i<300;++i)f.wall(10+i,120+(i%3));
    f.publish();
    std::vector<UnitType> types;
    for(int foot=1;foot<=4;++foot) {auto t=mover(foot);t.maxSlope=8+foot*4;types.push_back(t);}
    types.push_back(mover(2));types.back().footX=3;types.back().id=types.back().name="legion-rect";
    std::vector<int> ids;
    for(auto& t:types)ids.push_back(f.spawn(t,2,2));
    f.start();
    auto* legion=f.world.legionNavigator();
    check(legion!=nullptr,"legion navigator not created in Legion mode");
    uint32_t r=24680u;
    auto rnd=[&](int n) {r=r*1103515245u+12345u;return int((r>>8)%uint32_t(n));};
    for(int step=0;step<400;++step) {
        if(step%2==0) {
            const int fx=1+rnd(3),fz=1+rnd(3),x=4+rnd(W-10),z=4+rnd(H-10);
            f.world.addFeature(z*W+x,float(x*16+fx*8),float(z*16+fz*8),0,1,fx,fz,rnd(3)!=0,-1,true);
        }
        if(step%37==0) {
            const int at=6+rnd(H-12),blocks=rnd(2);
            for(int x=2;x<W-2;++x)f.world.addFeature(at*W+x,float(x*16+8),float(at*16+8),0,1,1,1,blocks,-1,true);
        }
        f.world.tick(1.f/30);
    }
    const uint64_t built=f.world.legionStats().planeBuilds;
    check(built==types.size(),"expected one background build per class, got "+std::to_string(built));
    for(int id:ids)check(legion->planeMatchesRebuild(*f.world.unit(id)),"background plane differs from a rebuild for "+
        f.world.unit(id)->type->id);
    std::printf("planeprebuild built=%llu\n",(unsigned long long)built);
}
}

namespace {
void penstale() {
    // A static change in one walled region leaves another region's field
    // valid (it cannot reach any step inside it); a change inside the
    // region stales the field and it is rebuilt.
    constexpr int P=40;
    Fixture f(2*P+8,P+8);
    auto pen=[&](int x,int z) {f.rect(x,z,P,1);f.rect(x,z+P-1,P,1);f.rect(x,z,1,P);f.rect(x+P-1,z,1,P);};
    pen(0,0);pen(P+4,0);
    f.publish();
    const auto type=mover(2);
    const int id=f.spawn(type,4,P/2);
    f.start();
    f.world.order(id,float((P-5)*16),float((P/2)*16),false);
    for(int t=0;t<20;++t)f.world.tick(1.f/30);
    const auto before=f.world.legionStats();
    check(before.fieldsBuilt>=1,"no field for the penned order");
    for(int t=0;t<40;++t) {
        const int x=P+4+10+t%8,z=10;
        f.world.addFeature(z*f.width+x,float(x*16+8),float(z*16+8),0,1,1,1,t%2==0,-1,true);
        f.world.tick(1.f/30);
    }
    const auto other=f.world.legionStats();
    check(other.planeRefreshes>before.planeRefreshes,"the other region's changes never reached the plane");
    check(other.fieldsBuilt==before.fieldsBuilt,"a change in another region rebuilt the field");
    check(!f.world.unit(id)->orders.empty(),"arrived before the in-region change");
    f.world.addFeature(30*f.width+20,float(20*16+8),float(30*16+8),0,1,1,1,true,-1,true);
    for(int t=0;t<10;++t)f.world.tick(1.f/30);
    const auto inside=f.world.legionStats();
    check(inside.fieldsBuilt>other.fieldsBuilt,"a change inside the region did not refresh its field");
    std::printf("penstale fields=%llu/%llu/%llu refreshes=%llu\n",(unsigned long long)before.fieldsBuilt,
        (unsigned long long)other.fieldsBuilt,(unsigned long long)inside.fieldsBuilt,(unsigned long long)inside.planeRefreshes);
}
}

// ---- boats and hovercraft ------------------------------------------------
// Legion plans boats (Domain::Water) and hovercraft (Domain::Hover) on the
// same footprint-origin plane, built from mobilePlacement's own predicate
// with the type's water-depth and slope limits. Sea level is 64 in these
// fixtures; a height below it is water that deep.
namespace {
UnitType boat(int foot,int minDepth=13) {
    auto t=mover(foot);t.id=t.name="legion-boat-"+std::to_string(foot)+"-"+std::to_string(minDepth);
    t.domain=UnitType::Domain::Water;t.floater=true;t.minWaterDepth=minDepth;t.maxWaterDepth=10000;
    // Ships specify only a travel turn rate.
    t.turnRate=900;t.turnInPlaceRate=0;
    return t;
}
UnitType hover(int foot) {
    auto t=mover(foot);t.id=t.name="legion-hover-"+std::to_string(foot);
    t.domain=UnitType::Domain::Hover;t.maxSlope=10;t.maxWaterSlope=24;
    return t;
}
Weapon gun() {
    Weapon weapon;weapon.name="legion-naval-gun";weapon.range=120;weapon.damage=100;
    weapon.reload=0.1f;weapon.aimTol=32767;return weapon;
}
// An archipelago: deep sea (height 20), an island with a shallow shelf (56:
// too shallow for a 13-deep keel, fine for hover) and jagged cliffs, a land
// barrier east of it pierced by one narrow strait, a river mouth cut into the
// north shore, and a gentle beach on the south shore.
constexpr int kSeaW=176,kSeaH=104;
std::vector<uint8_t> archipelago() {
    std::vector<uint8_t> h(size_t(kSeaW)*kSeaH,20);
    auto at=[&](int x,int z)->uint8_t&{return h[size_t(z)*kSeaW+x];};
    for(int z=0;z<kSeaH;++z)for(int x=0;x<kSeaW;++x) {
        const int dx=x-70,dz=z-52,d2=dx*dx+dz*dz;
        if(d2<=14*14)at(x,z)=uint8_t(120+(x*5+z*3)%9);     // island, jagged cliffs
        else if(d2<=19*19)at(x,z)=56;                       // shelf
        // North shore with a river mouth (x 30..37) running inland.
        if(z<10&&!(x>=30&&x<=37))at(x,z)=110;
        // South beach: an 8-per-cell ramp from the sea to land (z 74..85),
        // leaving a 6-cell channel south of the island's shelf. (Its bottom
        // rows are off the map: retailMapBoundary cuts high southern land.)
        if(z>=74)at(x,z)=uint8_t(std::min(110,20+(z-74)*8));
        // Barrier at x 116..123, sea to land, with a strait z 47..56 (10
        // cells): the only water between the two seas.
        if(x>=116&&x<=123&&(z<47||z>56))at(x,z)=110;
    }
    return h;
}
}

namespace {
void navalclearance() {
    // Boats with several keel depths and footprints, hovercraft with slope
    // limits: the plane equals mobilePlacement at every origin, on shores,
    // shelves, the strait, the river mouth and the beach; and after random
    // static churn (piers, blocking features, docks with yards) the
    // incremental plane equals a whole-map rebuild.
    Fixture f(kSeaW,kSeaH,true,archipelago());
    for(int i=0;i<20;++i)f.wall(40+i,30+(i%3));            // a jagged reef of features
    f.publish();
    std::vector<UnitType> types;
    types.push_back(boat(2));types.push_back(boat(3,4));types.push_back(boat(4,30));types.push_back(boat(3,13));
    types.back().footZ=2;types.back().id=types.back().name="legion-boat-rect";
    types.push_back(hover(2));types.push_back(hover(3));types.back().maxWaterSlope=6;
    std::vector<int> ids;
    for(size_t i=0;i<types.size();++i)ids.push_back(f.spawn(types[i],6,40+int(i)*6));
    f.start();
    auto* legion=f.world.legionNavigator();
    check(legion!=nullptr,"legion navigator not created in Legion mode");
    for(int id:ids)check(legion->mission(*f.world.unit(id))==LegionMission::None,"idle unit has a mission");
    for(int id:ids) {
        f.world.order(id,float(150*16),float(52*16),false);
        check(legion->mission(*f.world.unit(id))==LegionMission::Move,"Legion does not route "+f.world.unit(id)->type->id);
    }
    f.world.tick(1.f/30);
    uint64_t compared=0,legalCount=0;
    for(int id:ids) {
        const auto& u=*f.world.unit(id);
        uint64_t mine=0;
        for(int z=-1;z<=kSeaH;++z)for(int x=-1;x<=kSeaW;++x) {
            bool nearBody=false;
            for(int other:ids)if(other!=id) {
                const auto& o=*f.world.unit(other);
                const int ox=footprintOrigin(o.x,o.type->footX),oz=footprintOrigin(o.z,o.type->footZ);
                if(x<ox+o.type->footX&&x+u.type->footX>ox&&z<oz+o.type->footZ&&z+u.type->footZ>oz)nearBody=true;
            }
            if(nearBody)continue;
            const bool a=legion->staticLegal(u,x,z),b=f.world.mobilePlacement(u,x,z,false);
            if(a!=b)throw std::runtime_error("plane differs from mobilePlacement for "+u.type->id+" at "+
                std::to_string(x)+","+std::to_string(z)+" plane="+std::to_string(a));
            ++compared;legalCount+=a;mine+=a;
        }
        check(mine>500&&mine<uint64_t(kSeaW)*kSeaH*9/10,"degenerate plane for "+u.type->id);
    }
    // Spot checks of the domains themselves.
    const auto& keel=*f.world.unit(ids[0]);const auto& skim=*f.world.unit(ids[4]);
    check(!legion->staticLegal(keel,70,52),"island interior is land");
    check(legion->staticLegal(skim,70,52)&&!legion->staticLegal(skim,70,52-15),"hover: the island top is open, its cliff is not");
    check(!legion->staticLegal(keel,70,35)&&legion->staticLegal(skim,70,35),"shelf: too shallow for a keel, fine for hover");
    check(legion->staticLegal(keel,119,51)&&legion->staticLegal(keel,10,60),"strait and open sea are navigable");
    check(legion->staticLegal(skim,20,88)&&legion->staticLegal(skim,20,80),"hover climbs the beach");
    check(!legion->staticLegal(keel,20,88)&&!legion->staticLegal(keel,20,80),"boat stays off the beach");
    // Static churn: piers (blocking features), buoys (non-blocking) and
    // docks with yards. The incremental plane must equal a rebuild.
    UnitType yard{};yard.id=yard.name="legion-dock";yard.maxHp=100;yard.footX=4;yard.footZ=3;yard.buildTime=1;
    yard.yardMap="o..oo..ooooo";
    uint32_t r=97531u;
    auto rnd=[&](int n) {r=r*1103515245u+12345u;return int((r>>8)%uint32_t(n));};
    std::vector<int> structures;
    for(int step=0;step<240;++step) {
        const int kind=rnd(10);
        if(kind<6) {
            const int fx=1+rnd(3),fz=1+rnd(3),x=4+rnd(kSeaW-10),z=4+rnd(kSeaH-10);
            f.world.addFeature(z*kSeaW+x,float(x*16+fx*8),float(z*16+fz*8),0,1,fx,fz,rnd(3)!=0,-1,true);
        } else if(kind<7) {
            const int x=6+rnd(kSeaW-14),z=6+rnd(kSeaH-14);
            structures.push_back(f.world.spawn(&yard,float(x*16+32),float(z*16+24),std::nullopt,1));
        } else if(kind<8&&!structures.empty()) {
            const size_t i=size_t(rnd(int(structures.size())));
            f.world.destroy(structures[i]);structures.erase(structures.begin()+long(i));
        } else f.world.tick(1.f/30);
        if(step%3==0||kind>=8)for(int id:ids)
            check(legion->planeMatchesRebuild(*f.world.unit(id)),"incremental naval plane differs from a rebuild at step "+
                std::to_string(step)+" for "+f.world.unit(id)->type->id);
    }
    std::printf("navalclearance compared=%llu legal=%llu refreshes=%llu\n",(unsigned long long)compared,
        (unsigned long long)legalCount,(unsigned long long)f.world.legionStats().planeRefreshes);
}

// A fleet crosses the archipelago: around the island and its shelf, then
// through the 10-cell strait, to a point beyond it. Every body stays legal
// every tick, Legion serves the move, everyone arrives, no spinning, and
// serial == workers.
uint64_t navalislandRun(bool serial,int foot,int count,bool print) {
    Fixture f(kSeaW,kSeaH,serial,archipelago());
    f.publish();
    const auto type=boat(foot);
    std::vector<int> ids;
    for(int i=0;i<count;++i)ids.push_back(f.spawn(type,6+(i%4)*(foot+1),34+(i/4)*(foot+1)));
    f.start();
    auto* legion=f.world.legionNavigator();
    for(size_t i=0;i<ids.size();++i)f.world.order(ids[i],float(150*16),float(52*16),false);
    for(int id:ids)check(legion->mission(*f.world.unit(id))==LegionMission::Move,"Legion does not route the fleet");
    Motion motion;int last=-1;
    std::map<int,bool> passedStrait;
    for(int t=0;t<9000;++t) {
        f.world.tick(1.f/30);
        motion.observe(f.world,ids);
        bool all=true;
        for(int id:ids) {
            check(f.legal(id),"boat "+std::to_string(id)+" on an illegal origin at tick "+std::to_string(t));
            const auto& u=*f.world.unit(id);
            if(u.x>Fixed::fromInt(116*16)&&u.x<Fixed::fromInt(124*16))passedStrait[id]=true;
            all&=u.orders.empty();
        }
        if(all) {last=t;break;}
    }
    int arrived=0,east=0;
    for(int id:ids) {arrived+=f.world.unit(id)->orders.empty();east+=f.world.unit(id)->x>Fixed::fromInt(124*16);}
    if(print)printLeft(f,ids);
    if(print)std::printf("navalisland foot=%d count=%d arrived=%d east=%d strait=%zu tick=%d spins=%llu reversals=%llu hash=%016llx\n",
        foot,count,arrived,east,passedStrait.size(),last,(unsigned long long)motion.spins,
        (unsigned long long)motion.reversals,(unsigned long long)f.world.stateHash());
    check(arrived==count&&east==count,"fleet did not cross the strait");
    check(int(passedStrait.size())==count,"a boat bypassed the strait");
    check(motion.spins==0,"a boat spun in place");
    return f.world.stateHash();
}
void navalisland() {
    const uint64_t a=navalislandRun(true,3,12,true);
    navalislandRun(true,4,8,true);
    navalislandRun(true,2,24,true);
    check(a==navalislandRun(false,3,12,false),"serial and workers differ");
}

// Hovercraft cross the beach from land into the sea and back onto land;
// boats go up a narrow river mouth, and boats sent onto the beach stop at
// the water's edge. Everyone stays legal every tick.
void hovershore() {
    Fixture f(kSeaW,kSeaH,true,archipelago());
    f.publish();
    const auto skim=hover(2);const auto keel=boat(3);
    std::vector<int> hovers,boats;
    for(int i=0;i<10;++i)hovers.push_back(f.spawn(skim,20+(i%5)*3,87+(i/5)*3));
    for(int i=0;i<4;++i)boats.push_back(f.spawn(keel,96+i*5,64));
    f.start();
    auto* legion=f.world.legionNavigator();
    // Out to sea past the island (land -> beach -> water).
    for(int id:hovers)f.world.order(id,float(100*16),float(30*16),false);
    for(int id:hovers)check(legion->mission(*f.world.unit(id))==LegionMission::Move,"Legion does not route hovercraft");
    auto run=[&](const std::vector<int>& ids,int limit) {
        Motion motion;
        for(int t=0;t<limit;++t) {
            f.world.tick(1.f/30);motion.observe(f.world,ids);
            bool all=true;
            for(int id:hovers)check(f.legal(id),"hovercraft "+std::to_string(id)+" on an illegal origin at "+
                std::to_string(f.world.unit(id)->x.toFloat()/16)+","+std::to_string(f.world.unit(id)->z.toFloat()/16)+" tick "+std::to_string(t));
            for(int id:boats)check(f.legal(id),"boat on an illegal origin");
            for(int id:ids)all&=f.world.unit(id)->orders.empty();
            if(all)return std::pair{t,motion.spins};
        }
        return std::pair{-1,motion.spins};
    };
    const auto out=run(hovers,6000);
    std::printf("hovershore out tick=%d spins=%llu\n",out.first,(unsigned long long)out.second);
    printLeft(f,hovers);
    check(out.first>=0,"hovercraft did not reach the sea");
    for(int id:hovers)check(f.world.unit(id)->z<Fixed::fromInt(50*16),"hovercraft not at sea");
    // Back onto land up the beach further east; the boats go up the narrow
    // river mouth in the north shore.
    for(int id:hovers)f.world.order(id,float(140*16),float(84*16),false);
    for(int id:boats)f.world.order(id,float(34*16),float(3*16),false);
    for(int id:boats)check(legion->mission(*f.world.unit(id))==LegionMission::Move,"Legion does not route boats");
    std::vector<int> both=hovers;both.insert(both.end(),boats.begin(),boats.end());
    const auto back=run(both,6000);
    printLeft(f,both);
    int landed=0,river=0;
    for(int id:hovers)landed+=f.world.unit(id)->z>Fixed::fromInt(80*16);
    for(int id:boats)river+=f.world.unit(id)->z<Fixed::fromInt(12*16);
    std::printf("hovershore back tick=%d spins=%llu landed=%d river=%d\n",back.first,(unsigned long long)back.second,landed,river);
    check(back.first>=0&&landed==int(hovers.size()),"hovercraft did not land up the beach");
    check(river==int(boats.size()),"boats did not enter the river mouth");
    // Boats sent onto the beach: the goal resolves to the nearest water
    // they can float in (within 24 cells), so they stop at the water's edge.
    for(int id:boats)f.world.order(id,float(100*16),float(88*16),false);
    for(int t=0;t<3600;++t) {
        f.world.tick(1.f/30);
        for(int id:boats)check(f.legal(id),"boat on an illegal origin");
    }
    int held=0;
    for(int id:boats) {
        const auto& u=*f.world.unit(id);
        std::printf("  boat %d at %.1f,%.1f orders=%zu state=%d\n",id,u.x.toFloat()/16,u.z.toFloat()/16,u.orders.size(),legion->unitState(id));
        held+=u.z>Fixed::fromInt(66*16)&&std::abs((u.x-Fixed::fromInt(100*16)).toFloat())<24*16&&u.speed==Fixed();
    }
    std::printf("hovershore beach held=%d\n",held);
    check(held==int(boats.size()),"boats sent onto the beach did not stop at the water's edge");
    // A mixed selection (ground units, hovercraft, boats) ordered to one sea
    // point just off the beach: one group per class sharing the point's
    // area; boats and hovercraft arrive, ground units stop at the shore.
    auto foot=mover(2);foot.maxWaterDepth=4;
    std::vector<int> walkers;
    for(int i=0;i<4;++i)walkers.push_back(f.spawn(foot,130+i*3,88));
    for(int t=0;t<2;++t)f.world.tick(1.f/30);
    std::vector<int> mixed=walkers;mixed.insert(mixed.end(),hovers.begin(),hovers.begin()+4);
    mixed.insert(mixed.end(),boats.begin(),boats.end());
    for(int id:mixed)f.world.order(id,float(100*16),float(68*16),false);
    for(int t=0;t<3600;++t) {
        f.world.tick(1.f/30);
        for(int id:mixed)check(f.legal(id),"mixed selection: illegal origin for "+std::to_string(id));
    }
    int done=0,ashore=0;
    for(int id:mixed) {
        const auto& u=*f.world.unit(id);done+=u.orders.empty()&&u.speed==Fixed();
        if(std::find(walkers.begin(),walkers.end(),id)!=walkers.end())ashore+=u.z>Fixed::fromInt(74*16);
    }
    printLeft(f,mixed);
    std::printf("hovershore mixed done=%d/%zu ashore=%d\n",done,mixed.size(),ashore);
    check(done==int(mixed.size())&&ashore==int(walkers.size()),"mixed selection did not settle by class");
    check(out.second==0&&back.second==0,"a hovercraft spun in place");
}

// The mission families on water: patrol laps around the island, fight-move
// across it, an attack approach around it and a guard escort. Each must be
// served by Legion and do its job.
void navalmissions() {
    auto armed=[] {auto t=boat(3);t.weapon=gun();t.weapons.push_back(t.weapon);return t;};
    {   // Patrol: around the island and back, several laps.
        Fixture f(kSeaW,kSeaH,true,archipelago());f.publish();
        const auto t=boat(3);
        std::vector<int> ids;for(int i=0;i<6;++i)ids.push_back(f.spawn(t,30+(i%3)*4,48+(i/3)*4));
        f.start();
        for(int id:ids)f.world.patrol(id,float(104*16),float(52*16));
        std::vector<int> laps(ids.size(),0);std::vector<bool> out(ids.size(),false);bool legion=false;
        for(int n=0;n<9000;++n) {
            f.world.tick(1.f/30);
            for(size_t k=0;k<ids.size();++k) {
                const auto& u=*f.world.unit(ids[k]);check(f.legal(ids[k]),"patrol boat illegal");
                legion|=f.world.legionNavigator()->mission(u)==LegionMission::Patrol;
                // A lap: the outbound leg ends (the current goal flips from
                // the patrol point back to the start) east of the island.
                const bool outbound=u.orders[World::currentLeg(u.orders)].x>Fixed::fromInt(80*16);
                if(out[k]&&!outbound&&u.x>Fixed::fromInt(80*16))++laps[k];
                out[k]=outbound;
            }
        }
        const int least=*std::min_element(laps.begin(),laps.end());
        for(size_t k=0;k<ids.size();++k) {
            const auto& u=*f.world.unit(ids[k]);
            std::printf("  patrol %d at %.1f,%.1f laps=%d orders=%zu state=%d\n",ids[k],u.x.toFloat()/16,u.z.toFloat()/16,
                laps[k],u.orders.size(),f.world.legionNavigator()->unitState(ids[k]));
        }
        std::printf("navalmissions patrol least=%d legion=%d\n",least,legion);
        check(legion&&least>=2,"boat patrol around the island");
    }
    {   // Fight-move across the island.
        Fixture f(kSeaW,kSeaH,true,archipelago());f.publish();
        const auto t=armed();
        std::vector<int> ids;for(int i=0;i<6;++i)ids.push_back(f.spawn(t,30+(i%3)*4,48+(i/3)*4));
        f.start();
        for(int id:ids)f.world.attackMove(id,float(104*16),float(52*16),false);
        bool legion=false;int done=-1;
        for(int n=0;n<6000&&done<0;++n) {
            f.world.tick(1.f/30);bool all=true;
            for(int id:ids) {
                const auto& u=*f.world.unit(id);check(f.legal(id),"fight-move boat illegal");
                legion|=f.world.legionNavigator()->mission(u)==LegionMission::Fight;all&=u.orders.empty();
            }
            if(all)done=n;
        }
        std::printf("navalmissions fight done=%d legion=%d\n",done,legion);
        check(legion&&done>=0,"boat fight-move across the island");
    }
    {   // Attack approach: the target sits behind the island.
        Fixture f(kSeaW,kSeaH,true,archipelago());f.publish();
        const auto t=armed();auto target=boat(3);target.maxVel=Fixed();
        const int id=f.spawn(t,40,52),enemy=f.spawn(target,100,52,1);
        f.start();
        f.world.attack(id,enemy,false);
        bool legion=false;int killed=-1;
        for(int n=0;n<6000&&killed<0;++n) {
            f.world.tick(1.f/30);
            legion|=f.world.legionNavigator()->mission(*f.world.unit(id))==LegionMission::Attack;
            check(f.legal(id),"attacking boat illegal");
            const auto* e=f.world.unit(enemy);if(!e||!e->alive())killed=n;
        }
        std::printf("navalmissions attack killed=%d legion=%d\n",killed,legion);
        check(legion&&killed>=0,"boat attack approach around the island");
    }
    {   // Guard: an escort follows its charge around the island.
        Fixture f(kSeaW,kSeaH,true,archipelago());f.publish();
        const auto t=boat(3);
        const int charge=f.spawn(t,40,52),escort=f.spawn(t,34,44);
        f.start();
        f.world.guard(escort,charge,false);f.world.order(charge,float(104*16),float(52*16),false);
        bool legion=false;
        for(int n=0;n<6000;++n) {
            f.world.tick(1.f/30);check(f.legal(escort),"escort illegal");
            legion|=f.world.legionNavigator()->mission(*f.world.unit(escort))==LegionMission::Guard;
        }
        const auto& c=*f.world.unit(charge);const auto& e=*f.world.unit(escort);
        const float d=fxLen(c.x-e.x,c.z-e.z).toFloat();
        std::printf("navalmissions guard dist=%.1f legion=%d\n",d,legion);
        check(legion&&c.orders.empty()&&d<=160.f&&!e.orders.empty()&&e.orders.front().guard,"boat guard escort");
    }
}
// A control formation (Alt+N: squad -N) commanded through the real command
// path (Cmd::SetSquad, then one shared-point Cmd::Move per member, as the
// HUD issues it) across rocks and through a narrow gap. A formation is wider
// than any fixed straggler radius, and it arrives one by one through the gap:
// every member must settle legally near the point and stay settled (no
// endless rejoin re-orders), with no spin.
void squadformation() {
    Fixture f(220,100);
    f.rect(96,0,4,46);f.rect(96,52,4,48);           // a wall with a 6-cell gap
    f.rect(150,38,3,3);f.rect(168,58,4,2);f.rect(140,60,2,5);f.rect(175,40,2,6); // rocks
    f.publish();
    const auto type=mover(2);
    std::vector<int> ids;
    for(int i=0;i<120;++i)ids.push_back(f.spawn(type,14+(i%12)*3,32+(i/12)*4));
    f.start();
    TypeRegistry registry;
    auto command=[&](tak::net::Cmd kind,int id,int target,float x,float z) {
        tak::net::Command c;c.kind=kind;c.player=0;c.unitId=id;c.targetId=target;c.x=x;c.z=z;c.queue=0;
        applyCommand(f.world,registry,c);
    };
    for(int id:ids)command(tak::net::Cmd::SetSquad,id,-1,0,0);
    for(int t=0;t<600;++t)f.world.tick(1.f/30);      // assigning the formation gathers it
    const float px=160*16,pz=50*16;
    for(int id:ids)command(tak::net::Cmd::Move,id,0,px,pz);
    Motion motion;
    std::map<int,int> reorders;std::map<int,bool> idle;
    int lastOrdered=-1;
    constexpr int kTicks=12000;
    for(int t=0;t<kTicks;++t) {
        f.world.tick(1.f/30);
        motion.observe(f.world,ids);
        for(int id:ids) {
            const auto& u=*f.world.unit(id);
            check(f.legal(id),"illegal footprint in a commanded formation");
            const bool now=u.orders.empty();
            if(idle.count(id)&&idle[id]&&!now) {++reorders[id];lastOrdered=t;}
            if(!now)lastOrdered=std::max(lastOrdered,t);
            idle[id]=now;
        }
    }
    int settled=0,maxReorders=0;float far=0;
    for(int id:ids) {
        const auto& u=*f.world.unit(id);
        settled+=u.orders.empty()&&u.speed==Fixed();
        maxReorders=std::max(maxReorders,reorders[id]);
        far=std::max(far,fxLen(u.x-Fixed::fromFloat(px),u.z-Fixed::fromFloat(pz)).toFloat());
    }
    printLeft(f,ids);
    std::printf("squadformation settled=%d/%zu last_ordered=%d max_reorders=%d far=%.0f spins=%llu reversals=%llu\n",settled,ids.size(),
        lastOrdered,maxReorders,far,(unsigned long long)motion.spins,(unsigned long long)motion.reversals);
    check(settled==int(ids.size()),"formation members never settled");
    check(lastOrdered<kTicks-3000,"formation members kept being re-ordered");
    check(maxReorders<=1,"a settled formation member was re-ordered repeatedly");
    // Near its spot: inside the native formation radius (2*sqrt(area) cells).
    check(far<=32.f*std::sqrt(float(ids.size()*4)),"a formation member settled far from the point");
    check(motion.spins==0,"formation members turned in place");
}
}

// A formation (Alt+1) of 20 ground bodies and 8 flyers given one order.
// Measures how far the flyers stray from the ground members' centroid while
// the ground is under way, and where they end up.
struct MixedRun {int groundDone=-1,flyersDone=-1;float maxAway=0,meanAway=0,endAway=0;uint64_t hash=0;int landedAhead=0;};
MixedRun mixedformationRun(bool legion,tak::net::Cmd kind,bool serial=true) {
    Fixture f(260,100,serial);f.publish();
    if(!legion)f.world.setPathfindingMode(PathfindingMode::Retail);
    const auto type=mover(2);
    UnitType flyer{};flyer.id=flyer.name="legion-flyer";
    flyer.canFly=flyer.canMove=true;flyer.maxHp=100;flyer.footX=flyer.footZ=3;flyer.sight=4096;
    flyer.maxVel=Fixed::fromInt(4);flyer.accel=flyer.brake=Fixed::fromInt(1);flyer.turnRate=1200;flyer.cruiseAlt=80;flyer.buildTime=1;
    std::vector<int> ground,flyers,all;
    for(int i=0;i<20;++i)ground.push_back(f.spawn(type,14+(i%5)*3,40+(i/5)*4));
    for(int i=0;i<8;++i)flyers.push_back(f.spawn(flyer,16+(i%4)*4,58+(i/4)*4));
    all=ground;all.insert(all.end(),flyers.begin(),flyers.end());
    f.start();
    TypeRegistry registry;
    auto command=[&](tak::net::Cmd k,int id,int target,float x,float z) {
        tak::net::Command c;c.kind=k;c.player=0;c.unitId=id;c.targetId=target;c.x=x;c.z=z;c.queue=0;
        applyCommand(f.world,registry,c);
    };
    for(int id:all)command(tak::net::Cmd::SetSquad,id,-1,0,0);
    for(int t=0;t<300;++t)f.world.tick(1.f/30);
    const float px=220*16,pz=50*16;
    // As the move UI does: flyers keep their offset from the selection's
    // centre, clamped to 60 px per axis; Legion surface movers share the point.
    float sx=0,sz=0;
    for(int id:all){sx+=f.world.unit(id)->x.toFloat();sz+=f.world.unit(id)->z.toFloat();}
    sx/=float(all.size());sz/=float(all.size());
    for(int id:all) {
        const auto& u=*f.world.unit(id);
        const bool offset=kind==tak::net::Cmd::Move&&(u.type->canFly||!legion);
        command(kind,id,0,offset?px+std::clamp(u.x.toFloat()-sx,-60.f,60.f):px,
                offset?pz+std::clamp(u.z.toFloat()-sz,-60.f,60.f):pz);
    }
    MixedRun r;Motion motion;double sum=0;int samples=0;
    const int ticks=kind==tak::net::Cmd::Patrol?3000:6000;
    auto centroid=[&](float& cx,float& cz) {
        double sx=0,sz=0;for(int id:ground){sx+=f.world.unit(id)->x.toFloat();sz+=f.world.unit(id)->z.toFloat();}
        cx=float(sx/ground.size());cz=float(sz/ground.size());
    };
    for(int t=0;t<ticks;++t) {
        f.world.tick(1.f/30);motion.observe(f.world,ground);
        for(int id:ground)check(f.legal(id),"illegal footprint in a mixed formation");
        bool groundBusy=false,flyersBusy=false;
        for(int id:ground)groundBusy|=!f.world.unit(id)->orders.empty();
        for(int id:flyers)flyersBusy|=!f.world.unit(id)->orders.empty();
        if(!groundBusy&&r.groundDone<0)r.groundDone=t;
        if(!flyersBusy&&r.flyersDone<0)r.flyersDone=t;
        if(groundBusy&&t>=150) {
            float cx,cz;centroid(cx,cz);
            for(int id:flyers) {
                const auto& u=*f.world.unit(id);
                const float d=std::hypot(u.x.toFloat()-cx,u.z.toFloat()-cz);
                r.maxAway=std::max(r.maxAway,d);sum+=d;++samples;
                if(t%30==0&&u.flightGroundMode==1&&u.x.toFloat()>cx+300)++r.landedAhead;
            }
        }
        if(std::getenv("MIXED_TRACE")&&t%150==0) {
            float cx,cz;centroid(cx,cz);
            std::printf("  t=%d ground %.0f,%.0f busy=%d |",t,cx,cz,int(groundBusy));
            for(int id:flyers) {const auto& u=*f.world.unit(id);
                std::printf(" %.0f,%.0f/%zu%s%d",u.x.toFloat(),u.z.toFloat(),u.orders.size(),
                    u.orders.empty()?"":u.orders.front().formationLevel?"F":"",int(u.flightGroundMode));}
            std::printf("\n");
        }
        if(kind!=tak::net::Cmd::Patrol&&r.groundDone>=0&&r.flyersDone>=0&&t>r.groundDone+600)break;
    }
    float cx,cz;centroid(cx,cz);
    for(int id:flyers)r.endAway=std::max(r.endAway,std::hypot(f.world.unit(id)->x.toFloat()-cx,f.world.unit(id)->z.toFloat()-cz));
    r.meanAway=samples?float(sum/samples):0;r.hash=f.world.stateHash();
    const char* name=kind==tak::net::Cmd::Move?"move":kind==tak::net::Cmd::AttackMove?"fight":"patrol";
    std::printf("mixedformation %s %s ground_done=%d flyers_done=%d away_max=%.0f away_mean=%.0f end_away=%.0f landed_ahead=%d spins=%llu hash=%016llx\n",
        legion?"legion":"retail",name,r.groundDone,r.flyersDone,r.maxAway,r.meanAway,r.endAway,r.landedAhead,
        (unsigned long long)motion.spins,(unsigned long long)r.hash);
    return r;
}
void mixedformation() {
    for(auto kind:{tak::net::Cmd::Move,tak::net::Cmd::AttackMove,tak::net::Cmd::Patrol}) {
        mixedformationRun(false,kind);   // Retail: reported, the 417e02 re-forming port
        const auto r=mixedformationRun(true,kind);
        if(std::getenv("MIXED_REPORT"))continue;   // measurement only
        // Flyers stay over the ground (spiral of 8 flyers: <= ~2 rings of 64 px,
        // plus the lag of a 4 px/tick flyer behind its moving station).
        check(r.maxAway<=320.f,"a formation flyer strayed from the ground");
        check(r.landedAhead==0,"a formation flyer landed ahead of the ground");
        if(kind!=tak::net::Cmd::Patrol) {
            check(r.groundDone>=0&&r.flyersDone>=0,"the mixed formation never finished");
            check(r.flyersDone>=r.groundDone,"formation flyers finished before the ground");
            check(r.endAway<=200.f,"formation flyers settled away from the ground");
        }
    }
    const auto a=mixedformationRun(true,tak::net::Cmd::Move,true),b=mixedformationRun(true,tak::net::Cmd::Move,false);
    check(a.hash==b.hash,"mixed formation: serial and workers differ");
}

int main(int argc,char** argv) {
    const std::map<std::string_view,std::function<void()>> cases{
        {"clearance",clearance},{"groupreuse",groupreuse},{"jagged",jagged},{"trapped",trapped},
        {"crowdhold",crowdhold},{"replace",replace},{"unreachable",unreachable},{"quota",quota},{"pens",pens},
        {"determinism",determinism},{"formation",formation},{"slotblock",slotblock},
        {"staticblock",staticblock},{"deaths",deaths},{"deathsshared",deathsshared},{"splitgoal",splitgoal},{"farclick",farclick},{"churn",churn},
        {"approachhold",approachhold},{"approachopen",approachopen},
        {"churnfield",churnfield},{"planeincremental",planeincremental},{"planeprebuild",planeprebuild},{"penstale",penstale},{"legacyyield",legacyyield},{"approachchurn",approachchurn},{"lattice",lattice},{"wallend",wallend},
        {"navalclearance",navalclearance},{"navalisland",navalisland},{"hovershore",hovershore},{"navalmissions",navalmissions},{"squadformation",squadformation},
        {"pinwheel",pinwheel},{"landedflyers",landedflyers},{"mixedformation",mixedformation},{"liftflyers",liftflyers}};
    try {
        if(argc<2) {for(const auto& [name,fn]:cases)fn();}
        else {
            const auto found=cases.find(argv[1]);
            if(found==cases.end())throw std::runtime_error("unknown case");
            found->second();
        }
    } catch(const std::exception& e) {std::fprintf(stderr,"legion_world_test: %s\n",e.what());return 1;}
    std::puts("ok");
    return 0;
}
