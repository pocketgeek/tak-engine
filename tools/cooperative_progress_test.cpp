// Small physical cohorts for comparing navigation revisions. There are no
// wall-clock assertions: progress, complete arrival and resting are separate.
#include "cooperative_test_common.h"
#include <cstdlib>
#include <limits>

using namespace cooperative_test;
namespace {
struct Position {Fixed x,z;};
struct Wall {int x,z,w,h;};
constexpr int destinationX=3200,destinationZ=2048;
void walls(World& world,const std::vector<Wall>& boxes) {
    std::vector<uint16_t> cells(256*256,0xffff);
    for(const auto& box:boxes) {
        world.blockCells(box.x,box.z,box.w,box.h,true);
        for(int z=box.z;z<box.z+box.h;++z)for(int x=box.x;x<box.x+box.w;++x)
            cells[size_t(z)*256+x]=0;
    }
    world.setMapPlacementFeatures(cells,{{"progress-wall",1,1,true,true,false,0}});
}
int distance(const Unit& unit,int x,int z) {
    return fxLen(unit.x-Fixed::fromInt(x),unit.z-Fixed::fromInt(z)).floorInt();
}
int width(std::vector<int> values) {
    if(values.empty())return -1;
    std::sort(values.begin(),values.end());
    return values[values.size()*3/4]-values[values.size()/4];
}
void run(PathfindingMode selected,std::string_view scenario,int count,int ticks,bool complete) {
    World world;setup(world,selected);
    const bool diagonal=scenario=="diagonal";
    const bool narrow=scenario=="corridor"||scenario=="corridor-far";
    const int goalX=scenario=="corridor-far"?3856:destinationX;
    const int goalZ=diagonal?3200:destinationZ;
    if(scenario=="turn")walls(world,{{112,0,8,150}});
    else if(narrow)walls(world,{{112,0,24,126},{112,130,24,126}});
    auto type=mover();const int columns=count==16?4:8,rows=(count+columns-1)/columns;
    std::vector<int> ids,initialDistance;
    std::vector<Position> previous;
    std::vector<int64_t> travel(size_t(count),0);
    std::vector<int> atExit(size_t(count),std::numeric_limits<int>::min());
    std::vector<int> afterExit=atExit;
    for(int i=0;i<count;++i) {
        const int x=512+i%columns*32,z=(diagonal?768:destinationZ)-(rows-1)*16+i/columns*32;
        const int id=world.spawn(&type,float(x),float(z),std::nullopt,0);ids.push_back(id);
        previous.push_back({Fixed::fromInt(x),Fixed::fromInt(z)});
        initialDistance.push_back(distance(*world.unit(id),goalX,goalZ));
        world.order(id,goalX,goalZ,false);
    }
    known(world);legal(world,ids);
    const int initialWidth=middleWidth(world,ids);
    const auto report=[&] {
        int arrived=0,moved=0,blocked=0,stopped=0;int64_t sum=0,totalTravel=0;
        std::vector<int> progress;
        for(size_t i=0;i<ids.size();++i) {
            const auto& unit=*world.unit(ids[i]);
            const int forward=initialDistance[i]-distance(unit,goalX,goalZ);
            progress.push_back(forward);sum+=forward;totalTravel+=travel[i];moved+=forward>=32;
            arrived+=unit.orders.empty();blocked+=unit.bodyBlockStreak!=0;stopped+=unit.speed==Fixed{};
        }
        std::sort(progress.begin(),progress.end());const auto stats=world.flowStats();
        std::printf("PROGRESS mode=%u case=%.*s count=%d tick=%u forward_mean=%.1f forward_p10=%d forward_p50=%d travel_mean=%.1f moved32=%d arrived=%d blocked=%d stopped=%d width=%d pending=%zu requests=%llu searches=%llu waits=%llu hash=%016llx\n",
            unsigned(selected),int(scenario.size()),scenario.data(),count,world.tickCount(),double(sum)/count,
            progress[size_t(count-1)/10],progress[size_t(count-1)/2],double(totalTravel)/(65536*count),
            moved,arrived,blocked,stopped,middleWidth(world,ids),stats.pending,
            (unsigned long long)stats.requests,(unsigned long long)stats.cooperativeSearches,
            (unsigned long long)stats.cooperativeWaits,(unsigned long long)world.stateHash());
        require(stats.bytes<=512ull*1024*1024,"navigation exceeded the shared memory limit");
        // Every member of these short known routes must leave its starting
        // footprint. Edge movers alone must not hide a stationary interior.
        if(world.tickCount()>=1200)require(progress.front()>=32,"a cohort member made less than one footprint of forward progress");
        std::fflush(stdout);
    };
    for(int tick=1;tick<=ticks&&!finished(world,ids);++tick) {
        world.tick(1.f/30);
        for(size_t i=0;i<ids.size();++i) {
            const auto& unit=*world.unit(ids[i]);
            travel[i]+=fxLen(unit.x-previous[i].x,unit.z-previous[i].z).v;
            previous[i]={unit.x,unit.z};
            if(narrow&&unit.x>=Fixed::fromInt(2192)&&atExit[i]==std::numeric_limits<int>::min())atExit[i]=unit.z.floorInt();
            if(narrow&&unit.x>=Fixed::fromInt(2704)&&afterExit[i]==std::numeric_limits<int>::min())afterExit[i]=unit.z.floorInt();
        }
        if(tick%30==0)legal(world,ids);
        if(tick%300==0)report();
    }
    if(world.tickCount()%300)report();
    legal(world,ids);
    const auto crossingWidth=[](std::vector<int> values) {
        std::erase(values,std::numeric_limits<int>::min());
        return std::pair{values.size(),width(std::move(values))};
    };
    const auto exit=crossingWidth(atExit),expanded=crossingWidth(afterExit);
    std::printf("WIDTH initial=%d exit_count=%zu exit_width=%d expanded_count=%zu expanded_width=%d\n",
        initialWidth,exit.first,exit.second,expanded.first,expanded.second);
    if(complete) {
        require(finished(world,ids),"cohort did not finish within the specified tick horizon");
        if(scenario=="corridor-far"&&selected==PathfindingMode::Cooperative) {
            // Both planes precede the far destination's arrival band. Compare
            // each body's first crossing, so the waiting tail cannot make a
            // compressed traveling column appear wider than it really is.
            require(exit.first==ids.size()&&expanded.first==ids.size(),
                    "a member never crossed the corridor's width measurement planes");
            require(expanded.second>exit.second,
                    "traveling group did not regain width beyond its narrow exit");
        }
        // These are all2x2 bodies: arrival area is count*(foot+1)^2,
        // plus the existing two-stride contact shell. Check anchors exactly;
        // a fixed world radius would mishandle even-footprint rounding.
        const int radius=int(isqrt64(uint64_t(count)*9))+6;
        for(int id:ids) {
            const auto& unit=*world.unit(id);
            const int64_t dx=footprintCell(unit.x,2)-goalX/16,dz=footprintCell(unit.z,2)-goalZ/16;
            require(dx*dx+dz*dz<=int64_t(radius)*radius,"cohort retired outside its bounded destination area");
        }
        rest(world,ids);
        std::printf("REST count=%d ticks=120 hash=%016llx\n",count,(unsigned long long)world.stateHash());
    }
}
}
int main(int argc,char** argv) {
    if(argc<3||argc>6) {
        std::fprintf(stderr,"usage: cooperative_progress_test MODE east|diagonal|turn|corridor|corridor-far [16|64|128] [TICKS] [complete]\n");return 2;
    }
    try {
        const auto selected=mode(argv[1]);const std::string_view scenario=argv[2];
        require(scenario=="east"||scenario=="diagonal"||scenario=="turn"||scenario=="corridor"||scenario=="corridor-far","unknown scenario");
        const int count=argc>3?std::atoi(argv[3]):64,ticks=argc>4?std::atoi(argv[4]):1200;
        require(count==16||count==64||count==128,"count must be16,64 or128");
        require(ticks>0&&ticks<=30000,"tick horizon must be1..30000");
        require(argc<6||std::string_view(argv[5])=="complete","last argument must be complete");
        run(selected,scenario,count,ticks,argc==6);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}
}
