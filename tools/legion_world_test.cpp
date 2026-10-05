// Legion navigation (PathfindingMode 4) World tests, asset-free.
//
//   legion_world_test CASE
//
// Cases: clearance groupreuse jagged trapped crowdhold replace unreachable
// quota determinism. Each builds a small flat map with feature walls (the
// same placement plane a loaded match uses) and checks one requirement of
// docs/legion-pathfinding.md.
#include "sim/sim.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
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
        const int inside=f.spawn(mover(2),15,15);
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
void determinism() {
    const uint64_t a=scenario(true),b=scenario(true),c=scenario(false);
    std::printf("determinism serial=%016llx repeat=%016llx workers=%016llx\n",(unsigned long long)a,(unsigned long long)b,(unsigned long long)c);
    check(a==b,"Legion run is not repeatable");
    check(a==c,"Legion serial and worker hashes differ");
}
}

int main(int argc,char** argv) {
    const std::map<std::string_view,std::function<void()>> cases{
        {"clearance",clearance},{"groupreuse",groupreuse},{"jagged",jagged},{"trapped",trapped},
        {"crowdhold",crowdhold},{"replace",replace},{"unreachable",unreachable},{"quota",quota},
        {"determinism",determinism}};
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
