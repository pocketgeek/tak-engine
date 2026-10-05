#include "cooperative_test_common.h"
#include <functional>
#include <string>
#include <tuple>

using namespace cooperative_test;
namespace {
constexpr auto selected=PathfindingMode::RetailPlus;
using Intent=std::pair<Fixed,Fixed>;
Intent intent(const Order& o) {return o.missionTarget.value_or(Intent{o.x,o.z});}
struct Rect {int x,z,w,h;};
void obstacles(World& world,const std::vector<Rect>& rectangles,int cells=256) {
    std::vector<uint16_t> features(size_t(cells)*cells,0xffff);
    for(const auto& r:rectangles) {
        world.blockCells(r.x,r.z,r.w,r.h,true);
        for(int z=r.z;z<r.z+r.h;++z)for(int x=r.x;x<r.x+r.w;++x)features[size_t(z)*cells+x]=0;
    }
    world.setMapPlacementFeatures(features,{{"retailplus-test-wall",1,1,true,true,false,0}});
}
int groupRadius(size_t count,int foot=2) {
    const int spacing=foot+1,area=int(count)*spacing*spacing;int radius=0;
    while((radius+1)*(radius+1)<=area)++radius;
    // Contact shell plus one cell enclosing the <=sqrt(2)*8px difference
    // between the proved anchor and a continuous legal standing position.
    return (radius+2*spacing+1)*16;
}
void describe(const World& world,const std::vector<int>& ids,const char* name) {
    size_t done=0;for(int id:ids)done+=world.unit(id)->orders.empty();
    const auto stats=world.retailPlusStats();
    std::printf("scenario=%s tick=%u arrived=%zu/%zu requests=%llu searches=%llu routes=%llu waits=%llu bytes=%zu\n",
        name,world.tickCount(),done,ids.size(),(unsigned long long)world.pathStats().requests(),
        (unsigned long long)stats.traffic.searches,(unsigned long long)stats.traffic.routes,
        (unsigned long long)stats.traffic.waits,stats.bytes);
    if(done!=ids.size())for(int id:ids){const auto& u=*world.unit(id);if(!u.orders.empty())
        std::printf("active id=%d xy=%.3f,%.3f speed=%d block=%d orders=%zu radius=%u\n",
            id,u.x.toFloat(),u.z.toFloat(),u.speed.v,u.bodyBlockStreak,u.orders.size(),u.orders[World::currentLeg(u.orders)].missionRadius);}
}
std::vector<uint64_t> groupMoves(bool serial) {
    World world;setup(world,selected,serial);std::array<UnitType,3> types{mover(0),mover(1),mover(2)};
    std::vector<int> ids;std::vector<bool> firstRetired(12);
    for(int i=0;i<12;++i) {
        const int id=world.spawn(&types[size_t(i)%types.size()],float(640+i%3*48),float(1504+i/3*48),std::nullopt,0);
        ids.push_back(id);world.order(id,3008,1600,false);world.order(id,3008,2800,true);
    }
    known(world);std::vector<uint64_t> hashes;
    const int radius=groupRadius(ids.size());
    for(int tick=1;tick<=9000&&!finished(world,ids);++tick) {
        world.tick(1.f/30);
        for(size_t i=0;i<ids.size();++i) {
            const auto& u=*world.unit(ids[i]);const int64_t dx=u.x.floorInt()-3008,dz=u.z.floorInt()-1600;
            const Intent first{Fixed::fromInt(3008),Fixed::fromInt(1600)};
            if(!firstRetired[i]&&(u.orders.empty()||intent(u.orders[World::currentLeg(u.orders)])!=first)) {
                require(dx*dx+dz*dz<=int64_t(radius)*radius,"queued Move retired its first mission outside the actual goal area");
                firstRetired[i]=true;
            }
        }
        if(tick%30==0)legal(world,ids);
        if(tick%300==0)hashes.push_back(world.stateHash());
    }
    describe(world,ids,"queued-mixed-group");
    require(finished(world,ids),"Retail+ group did not complete both Move destinations");
    require(std::all_of(firstRetired.begin(),firstRetired.end(),[](bool v){return v;}),"queued Move never retired its first actual goal area");
    compact(world,ids,3008,2800,radius);rest(world,ids);hashes.push_back(world.stateHash());
    return hashes;
}
void replacementAndStop() {
    World world;setup(world,selected);auto type=mover();std::vector<int> ids;
    for(int i=0;i<6;++i){const int id=world.spawn(&type,float(640+i%2*48),float(1504+i/2*48),std::nullopt,0);
        ids.push_back(id);world.order(id,3008,1600,false);}
    known(world);for(int tick=0;tick<300;++tick)world.tick(1.f/30);
    world.stop(ids[0]);world.order(ids[1],640,2800,false);
    for(int tick=0;tick<180;++tick)world.tick(1.f/30);
    const auto stopped=Intent{world.unit(ids[0])->x,world.unit(ids[0])->z};
    for(int tick=0;tick<7000&&!finished(world,ids);++tick){world.tick(1.f/30);if(tick%30==0)legal(world,ids);}
    describe(world,ids,"replacement-stop");
    require(finished(world,ids),"replacement/Stop retained a stale local route");
    require(Intent{world.unit(ids[0])->x,world.unit(ids[0])->z}==stopped,"stopped member resumed old traffic motion");
    compact(world,{ids[1]},640,2800,32);
    compact(world,std::vector<int>(ids.begin()+2,ids.end()),3008,1600,groupRadius(6));rest(world,ids);
}
void jaggedTerrain() {
    World world;setup(world,selected);std::vector<Rect> wall;
    for(int z=72;z<128;++z)wall.push_back({112+(z/4)%2,z,3,1});
    obstacles(world,wall);auto type=mover();type.footX=type.footZ=3;
    std::vector<int> ids;
    for(int i=0;i<4;++i){const int id=world.spawn(&type,float(896+i%2*64),float(1536+i/2*64),std::nullopt,0);
        ids.push_back(id);world.order(id,3136,1600,false);}
    known(world);legal(world,ids);std::vector<bool> crossed(ids.size());
    for(int tick=1;tick<=10000&&!finished(world,ids);++tick) {
        world.tick(1.f/30);legal(world,ids);
        for(size_t i=0;i<ids.size();++i)crossed[i]=crossed[i]||world.unit(ids[i])->x>Fixed::fromInt(1920);
    }
    describe(world,ids,"jagged-terrain");
    require(finished(world,ids)&&std::all_of(crossed.begin(),crossed.end(),[](bool v){return v;}),
        "Retail+ did not physically route complete footprints around jagged terrain");
    compact(world,ids,3136,1600,groupRadius(ids.size(),3));rest(world,ids);
}
void correctSideOfGoalWall() {
    World world;setup(world,selected);obstacles(world,{{128,108,3,40}});auto type=mover();
    std::vector<int> ids;
    for(int i=0;i<8;++i){const int id=world.spawn(&type,float(1760+i%2*48),float(1984+i/2*48),std::nullopt,0);
        ids.push_back(id);world.order(id,2112,2048,false);}
    known(world);
    for(int tick=1;tick<=10000&&!finished(world,ids);++tick) {
        world.tick(1.f/30);if(tick%15==0)legal(world,ids);
        for(int id:ids){const auto& u=*world.unit(id);if(u.orders.empty())
            require(u.x>Fixed::fromInt(2080),"arrival contact retired a Move on the wrong side of its goal wall");}
    }
    describe(world,ids,"goal-wall");require(finished(world,ids),"goal-wall cohort never reached its proved destination side");
    compact(world,ids,2112,2048,groupRadius(ids.size()));rest(world,ids);
}
void boxedMoveRetainsQueuedGoal() {
    World world;setup(world,selected);obstacles(world,{{40,40,9,1},{40,48,9,1},{40,41,1,7},{48,41,1,7}});
    auto type=mover();const int id=world.spawn(&type,704,704,std::nullopt,0);
    known(world);world.order(id,2800,704,false);world.order(id,2800,2800,true);
    const Intent first{Fixed::fromInt(2800),Fixed::fromInt(704)},second{Fixed::fromInt(2800),Fixed::fromInt(2800)};
    for(int tick=0;tick<2400;++tick) {
        world.tick(1.f/30);const auto& u=*world.unit(id);
        require(u.orders.size()>=2,"failed local/global search retired a boxed Move or advanced its queue");
        const size_t leg=World::currentLeg(u.orders);
        require(leg+1<u.orders.size()&&intent(u.orders[leg])==first&&intent(u.orders.back())==second,
            "failed route replaced the issued destination with its partial endpoint");
        require(u.x<Fixed::fromInt(48*16)&&u.z<Fixed::fromInt(48*16),"boxed body escaped through a blocking footprint");
        if(tick%30==0)legal(world,{id});
    }
    describe(world,{id},"boxed-queued-move");
}
void returnedCorner() {
    World world;setup(world,selected,true,false,512);auto type=mover();type.maxVel=Fixed::raw(1);
    const int id=world.spawn(&type,6928.0138f,2272.235f,std::nullopt,0);
    known(world);world.order(id,7360,2272,false);world.order(id,7360,2752,true);
    for(int tick=0;tick<300&&(!world.pathStats().completions()||world.pathStats().pending(id));++tick)world.tick(1.f/30);
    require(world.pathStats().completions()&&!world.pathStats().pending(id),"returned-corner Retail route was not delivered");
    auto& u=*world.unit(id);const size_t leg=World::currentLeg(u.orders);
    require(leg+1<u.orders.size(),"returned-corner setup lost the queued command");
    const auto queued=intent(u.orders.back());u.orders.erase(u.orders.begin(),u.orders.begin()+int(leg));
    Order front;front.x=Fixed::fromInt(6960);front.z=Fixed::fromInt(2272);front.segmentX=Fixed::fromInt(6976);front.segmentZ=Fixed::fromInt(2256);front.hasSegment=true;
    Order returned;returned.x=Fixed::fromInt(6928);returned.z=Fixed::fromInt(2272);returned.segmentX=front.x;returned.segmentZ=front.z;returned.hasSegment=true;
    u.orders.insert(u.orders.begin(),{front,returned});
    type.maxVel=Fixed::raw(117964);u.baseSpeed=type.maxVel;u.x=Fixed::fromFloat(6928.0138f);u.z=Fixed::fromFloat(2272.235f);
    u.heading=Bam(19101);u.speed={};u.groundMovementMode=1;u.groundPitch=0;u.bodyBlockStreak=0;u.routeStamp=int32_t(world.tickCount());
    const auto start=Intent{u.x,u.z};bool visited=false;
    for(int tick=1;tick<=2400&&!u.orders.empty();++tick) {
        world.tick(1.f/30);const int64_t dx=u.x.floorInt()-7360,dz=u.z.floorInt()-2272;visited|=dx*dx+dz*dz<64*64;
        if(tick==120) {
            require(fxLen(u.x-start.first,u.z-start.second)>Fixed::fromInt(16),"returned route corner permanently braked a Retail+ mover");
            require(intent(u.orders.back())==queued,"corner recovery changed the queued Move destination");
        }
        if(tick%30==0)legal(world,{id});
    }
    describe(world,{id},"returned-corner");require(visited&&u.orders.empty(),"returned-corner mover did not complete its two real destinations");
    compact(world,{id},7360,2752,32);rest(world,{id});
}
using NativeSample=std::tuple<int32_t,int32_t,int32_t,int,size_t,int32_t,int32_t>;
std::vector<NativeSample> unsupportedTrace(PathfindingMode mode,int kind) {
    World world;setup(world,mode);auto type=mover();
    if(kind==0)type.isBuilder=true;
    if(kind==1){type.canTransport=true;type.transportCap=2;}
    if(kind==3)type.footX=type.footZ=5;
    const int id=world.spawn(&type,640,1600,std::nullopt,0);known(world);
    if(kind==2)world.attackMove(id,3008,1600,false);else world.order(id,3008,1600,false);
    std::vector<NativeSample> trace;
    for(int tick=0;tick<900;++tick) {
        world.tick(1.f/30);const auto& u=*world.unit(id);
        const auto target=u.orders.empty()?Intent{}:intent(u.orders[World::currentLeg(u.orders)]);
        trace.emplace_back(u.x.v,u.z.v,u.speed.v,u.heading.v,u.orders.size(),target.first.v,target.second.v);
    }
    require(world.unit(id)->x>Fixed::fromInt(1200),"unsupported native fixture did not move");
    if(mode==selected)require(!world.retailPlusStats().traffic.records,"unsupported goal allocated Retail+ records");
    legal(world,{id});return trace;
}
void fallback() {for(int kind=0;kind<4;++kind)
    require(unsupportedTrace(PathfindingMode::Retail,kind)==unsupportedTrace(selected,kind),
            "Retail+ changed unsupported builder/transport/Fight/large-body native movement");}
void formation() {
    World world;setup(world,selected);auto slow=mover(1),fast=mover(0);fast.maxVel=Fixed::fromInt(4);
    const int a=world.spawn(&slow,800,2048,std::nullopt,0),b=world.spawn(&fast,832,2112,std::nullopt,0);
    for(int id:{a,b}){world.setSquad(id,-1);world.order(id,3200,2048,false);}known(world);
    for(int tick=0;tick<1200;++tick){world.tick(1.f/30);if(tick%30==0)legal(world,{a,b});}
    require(world.unit(a)->x>Fixed::fromInt(1500)&&world.unit(b)->x>Fixed::fromInt(1500),
        "Retail+ mixed-speed formation failed to make actual forward progress");
    require(world.unit(a)->squad==-1&&world.unit(b)->squad==-1,"traffic changed native formation membership");
    describe(world,{a,b},"formation-progress");
}
void boundedAirborneNeighbors() {
    World world;setup(world,selected);auto ground=mover(),air=mover();
    air.canFly=true;air.maxVel=Fixed::raw(1);
    const int id=world.spawn(&ground,32,1024,std::nullopt,0);std::vector<int> airborne;
    // Airborne bodies may legitimately share a horizontal cell. They still
    // occupy entries in the shared body index, so a small query can have an
    // arbitrarily large candidate list even without any ground collision.
    for(int i=0;i<300;++i) {
        const int other=world.spawn(&air,48,1024,std::nullopt,0);airborne.push_back(other);
        auto& u=*world.unit(other);u.flightGroundMode=2;u.flightY=Fixed::fromInt(250);u.standbyAllowed=false;
    }
    known(world);world.order(id,3008,1024,false);legal(world,{id});
    const Intent start{world.unit(id)->x,world.unit(id)->z};
    for(int tick=0;tick<120;++tick) {
        world.unit(id)->bodyBlockStreak=2;
        const auto before=world.retailPlusStats();world.tick(1.f/30);const auto after=world.retailPlusStats();
        require(after.bodyEntries-before.bodyEntries<=32768,"Retail+ exceeded the shared body-query work allowance");
        require(!world.unit(id)->orders.empty(),"unavailable body evidence retired the distant Move");
        legal(world,{id});
        if(tick==29)require(world.unit(id)->x>start.first+Fixed::fromInt(16),
            "irrelevant airborne bucket permanently held a physically clear native step");
    }
    require(world.retailPlusStats().bodyDeferrals>0,"overfull airborne bucket did not exercise body-query deferral");
    for(int other:airborne){const auto& u=*world.unit(other);
        require(u.flightGroundMode!=1&&footprintCell(u.x,air.footX)/8==0&&footprintCell(u.z,air.footZ)/8==8,
            "airborne bucket dispersed before the native progress check");}
    require(world.unit(id)->x>start.first+Fixed::fromInt(100),"ground mover failed to pass the unchanged airborne bucket");
    legal(world,{id});
}
void blockedStandingArrival() {
    for(const bool airborneCrowd:{false,true}) {
        World world;setup(world,selected);auto type=mover(),air=mover();
        air.canFly=true;air.maxVel=Fixed::raw(1);
        const int id=world.spawn(&type,1024,1024,std::nullopt,0);
        if(airborneCrowd)for(int i=0;i<300;++i) {
            const int other=world.spawn(&air,1024,1024,std::nullopt,0);
            auto& u=*world.unit(other);u.flightGroundMode=2;u.flightY=Fixed::fromInt(250);u.standbyAllowed=false;
        }
        known(world);world.order(id,1024,1024,false);world.order(id,1408,1024,true);
        const Intent first{Fixed::fromInt(1024),Fixed::fromInt(1024)};
        const Intent second{Fixed::fromInt(1408),Fixed::fromInt(1024)};
        // The order's geometric circle accepts the current anchor. A feature
        // makes the body illegal, including when irrelevant airborne neighbors
        // force the local adapter to fall back to native movement.
        obstacles(world,{{63,63,2,2}});
        for(int tick=0;tick<30;++tick) {
            world.tick(1.f/30);const auto& u=*world.unit(id);
            require(u.orders.size()>=2&&intent(u.orders[World::currentLeg(u.orders)])==first&&intent(u.orders.back())==second,
                "native goal geometry retired a Move despite its illegal standing footprint");
            require(!world.mobilePlacement(u,footprintOrigin(u.x,type.footX),footprintOrigin(u.z,type.footZ),false),
                "dynamic-feature fixture stopped exercising an illegal standing arrival");
        }
        if(airborneCrowd)require(world.retailPlusStats().bodyDeferrals>0,
            "illegal standing arrival did not exercise the overfull airborne bucket");
        world.blockCells(63,63,2,2,false);
        world.setMapPlacementFeatures(std::vector<uint16_t>(256*256,0xffff),{});
        for(int tick=0;tick<1200&&!world.unit(id)->orders.empty();++tick)world.tick(1.f/30);
        require(world.unit(id)->orders.empty(),"legal standing arrival did not resume after its feature was removed");
        compact(world,{id},1408,1024,32);rest(world,{id});
    }
}
void queueObstruction() {
    // A blocked queue member names the ground body ahead from the occupancy
    // grid without charging the shared body-index allowance. Bodies the grid
    // does not record (a landed flyer here) still take the full query.
    for(const bool landedFlyer:{false,true}) {
        World world;setup(world,selected);auto type=mover(),air=mover();air.canFly=true;
        const int id=world.spawn(&type,1024,1024,std::nullopt,0);
        const int peer=world.spawn(landedFlyer?&air:&type,1056,1024,std::nullopt,0);
        if(landedFlyer){auto& p=*world.unit(peer);p.flightGroundMode=1;p.standbyAllowed=false;}
        known(world);world.tick(1.f/30);world.order(id,3008,1024,false);legal(world,{id});
        const Intent start{world.unit(id)->x,world.unit(id)->z};
        world.unit(id)->bodyBlockStreak=2;
        const auto before=world.retailPlusStats();world.tick(1.f/30);const auto after=world.retailPlusStats();
        require(after.traffic.waits>before.traffic.waits,"blocked member did not recognise the friendly body ahead");
        require(Intent{world.unit(id)->x,world.unit(id)->z}==start||world.unit(id)->x<=start.first+Fixed::fromInt(1),
            "blocked member advanced into the body ahead");
        if(landedFlyer)require(after.bodyEntries>before.bodyEntries,"a body missing from the occupancy grid skipped the full query");
        else require(after.bodyEntries==before.bodyEntries,"occupied-ahead check charged the shared body-index allowance");
        require(after.bodyDeferrals==before.bodyDeferrals,"single blocked member deferred its obstruction evidence");
        legal(world,{id});
        // The obstruction leaves: the member resumes native movement.
        world.unit(peer)->x=Fixed::fromInt(1024);world.unit(peer)->z=Fixed::fromInt(1600);
        for(int tick=0;tick<120;++tick)world.tick(1.f/30);
        require(world.unit(id)->x>start.first+Fixed::fromInt(64),"member stayed parked after the body ahead left");
        legal(world,{id});
    }
}
}
int main(int argc,char** argv) {try {
    const std::string wanted=argc>1?argv[1]:"all";
    const auto run=[&](const char* name,const std::function<void()>& test){if(wanted=="all"||wanted==name){test();std::printf("PASS %s\n",name);}};
    run("moves",[]{require(groupMoves(true)==groupMoves(false),"Retail+ movement depends on worker scheduling");});
    run("replacement",replacementAndStop);run("jagged",jaggedTerrain);run("goal-wall",correctSideOfGoalWall);
    run("boxed",boxedMoveRetainsQueuedGoal);run("returned-corner",returnedCorner);run("fallback",fallback);run("formation",formation);
    run("body-budget",boundedAirborneNeighbors);
    run("blocked-arrival",blockedStandingArrival);
    run("queue-obstruction",queueObstruction);
    return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}}
