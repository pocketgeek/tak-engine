// Physical World regressions for the third pathfinder. The optional mode
// argument also allows direct comparisons with unchanged legacy navigators.
#include "cooperative_test_common.h"
#include <functional>
#include <map>
using namespace cooperative_test;
namespace {
constexpr int goalX=3200,goalZ=2048;
struct Rectangle {int x,z,w,h;};
void obstacles(World& world,const std::vector<Rectangle>& rectangles) {
    std::vector<uint16_t> cells(256*256,0xffff);
    for(const auto& r:rectangles){world.blockCells(r.x,r.z,r.w,r.h,true);
        for(int z=r.z;z<r.z+r.h;++z)for(int x=r.x;x<r.x+r.w;++x)cells[size_t(z)*256+x]=0;}
    world.setMapPlacementFeatures(cells,{{"cooperative-wall",1,1,true,true,false,0}});
}
std::vector<int> cohort(World& world,const std::array<UnitType,3>& types,int count=32,int x=512,int z=1808) {
    std::vector<int> ids;int spacing=0;
    for(const auto& type:types)spacing=std::max(spacing,(std::max(type.footX,type.footZ)+1)*16);
    for(int i=0;i<count;++i)ids.push_back(world.spawn(&types[size_t(i%3)],float(x+i%4*spacing),float(z+i/4*spacing),std::nullopt,0));
    return ids;
}
void describe(const World& world,const std::vector<int>& ids,const char* scenario,int width=0) {
    int arrived=0;for(int id:ids)arrived+=world.unit(id)->orders.empty();const auto stats=world.flowStats();
    std::printf("scenario=%s tick=%u arrived=%d/%zu middle_width=%d bytes=%zu requests=%llu deliveries=%llu\n",
        scenario,world.tickCount(),arrived,ids.size(),width,stats.bytes,(unsigned long long)stats.requests,(unsigned long long)stats.deliveries);
    if(arrived!=int(ids.size()))for(int id:ids){const auto& u=*world.unit(id);if(!u.orders.empty())
        std::printf("active id=%d xy=%.2f,%.2f orders=%zu blocked=%d\n",id,u.x.toFloat(),u.z.toFloat(),u.orders.size(),u.bodyBlockStreak);}
}
std::vector<uint64_t> traverse(PathfindingMode selected,bool serial,bool boat,bool narrow,bool queued) {
    World world;setup(world,selected,serial,boat);
    if(narrow)obstacles(world,{{112,0,24,126},{112,130,24,126}});
    std::array<UnitType,3> types{mover(0,boat),mover(1,boat),mover(2,boat)};
    const auto ids=cohort(world,types);legal(world,ids);
    for(int id:ids){world.order(id,goalX,goalZ,false);if(queued)world.order(id,goalX,3008,true);}
    known(world);int openWidth=-1,exitWidth=-1;std::vector<bool> crossed(ids.size()),firstVisited(ids.size());
    std::vector<uint64_t> hashes;
    for(int tick=1;tick<=14000&&!finished(world,ids);++tick) {
        world.tick(1.f/30);int64_t sum=0;
        for(size_t i=0;i<ids.size();++i){const auto& u=*world.unit(ids[i]);sum+=u.x.floorInt();
            crossed[i]=crossed[i]||u.x.floorInt()>2304;
            const int64_t dx=u.x.floorInt()-goalX,dz=u.z.floorInt()-goalZ;
            firstVisited[i]=firstVisited[i]||dx*dx+dz*dz<448*448;
            if(narrow&&u.x.floorInt()>=1808&&u.x.floorInt()<2160)
                require(u.z.floorInt()>=2016&&u.z.floorInt()<=2088,"mover crossed outside the physical corridor");}
        const int mean=int(sum/int64_t(ids.size()));
        if(openWidth<0&&mean>=1280)openWidth=middleWidth(world,ids);
        if(exitWidth<0&&mean>=2600)exitWidth=middleWidth(world,ids);
        if(tick%60==0)legal(world,ids);
        if(tick%300==0)hashes.push_back(world.stateHash());
    }
    describe(world,ids,narrow?"bottleneck":boat?"boats":queued?"queued":"open",openWidth);
    if(narrow)std::printf("bottleneck middle_width_before=%d middle_width_after=%d\n",openWidth,exitWidth);
    require(finished(world,ids),"group did not physically complete its destination");
    require(std::all_of(crossed.begin(),crossed.end(),[](bool v){return v;}),"a member never crossed the map");
    require(openWidth>=96,"group collapsed to single file on open ground");
    if(narrow)require(exitWidth>=64,"group did not spread after leaving its narrow passage");
    if(queued)require(std::all_of(firstVisited.begin(),firstVisited.end(),[](bool v){return v;}),"queued group skipped its first destination");
    compact(world,ids,goalX,queued?3008:goalZ,448);legal(world,ids);rest(world,ids);
    hashes.push_back(world.stateHash());return hashes;
}
void opposing(PathfindingMode selected,bool mixed=false,bool single=false,bool thin=false) {
    World world;setup(world,selected);
    if(thin)obstacles(world,{{128,0,1,127},{128,129,1,127}});
    else if(single)obstacles(world,{{112,0,32,127},{112,129,32,127}});
    else if(mixed)obstacles(world,{{112,0,12,126},{132,0,12,126},{124,0,8,122},
        {112,130,12,126},{132,130,12,126},{124,134,8,122}});
    else obstacles(world,{{96,0,64,126},{96,130,64,126}});
    std::array<UnitType,3> types{mover(0),mover(1),mover(2)};
    if(mixed){types[2].footX=types[2].footZ=4;types[2].id="cooperative-large-mover";}
    const int count=single?8:12;
    const auto left=cohort(world,types,count,800,1920),right=cohort(world,types,count,2800,1920);
    for(int id:left)world.order(id,3200,2048,false);
    for(int id:right)world.order(id,640,2048,false);
    known(world);std::vector<int> both=left;both.insert(both.end(),right.begin(),right.end());legal(world,both);
    std::map<int,int> crossedAt;
    for(int tick=1;tick<=22000&&(!finished(world,left)||!finished(world,right));++tick){world.tick(1.f/30);
        for(int id:left)if(world.unit(id)->x.floorInt()>2624)crossedAt.try_emplace(id,tick);
        for(int id:right)if(world.unit(id)->x.floorInt()<1472)crossedAt.try_emplace(id,tick);
        if(tick%60==0)legal(world,both);}
    describe(world,both,thin?"thin-wall-opposing":single?"single-file":mixed?"mixed-passing-bay":"opposing");require(crossedAt.size()==both.size(),"opposing corridor starved a member of either stream");
    require(finished(world,left)&&finished(world,right),"opposing streams did not finish");
    compact(world,left,3200,2048,448);compact(world,right,640,2048,448);rest(world,both);
}

void returnedCorner(PathfindingMode selected) {
    World world;setup(world,selected,true,false,512);auto type=mover();
    const int id=world.spawn(&type,6928.0138f,2272.235f,std::nullopt,0);
    known(world);world.order(id,7360,2272,false);world.order(id,7360,2752,true);
    for(int tick=0;tick<120&&(world.flowStats().deliveries==0||world.flowStats().pending);++tick)world.tick(1.f/30);
    require(world.flowStats().deliveries>0&&!world.flowStats().pending,"returned-corner fixture route was not delivered");
    auto& u=*world.unit(id);const size_t goal=World::currentLeg(u.orders);
    require(goal<u.orders.size()&&u.orders[goal].goal&&goal+1<u.orders.size(),"returned-corner fixture lost its queued mission");
    const auto controller=u.orders[goal].controller;const auto nextTarget=u.orders.back().missionTarget.value_or(std::pair{u.orders.back().x,u.orders.back().z});
    u.orders.erase(u.orders.begin(),u.orders.begin()+int(goal));
    Order front;front.x=Fixed::fromInt(6960);front.z=Fixed::fromInt(2272);front.segmentX=Fixed::fromInt(6976);front.segmentZ=Fixed::fromInt(2256);front.hasSegment=true;
    Order returned;returned.x=Fixed::fromInt(6928);returned.z=Fixed::fromInt(2272);returned.segmentX=front.x;returned.segmentZ=front.z;returned.hasSegment=true;
    // A short local yield has returned to the following proved corner. The
    // old prefix would ask native acceleration to travel out and back first.
    u.orders.insert(u.orders.begin(),{front,returned});
    u.x=Fixed::fromFloat(6928.0138f);u.z=Fixed::fromFloat(2272.235f);u.heading=Bam(19101);u.speed={};u.groundMovementMode=1;u.groundPitch=0;u.bodyBlockStreak=0;u.routeStamp=int32_t(world.tickCount());
    legal(world,{id});
    const auto start=std::pair{u.x,u.z};bool firstVisited=false;
    for(int tick=1;tick<=1800&&!u.orders.empty();++tick) {
        world.tick(1.f/30);
        const int64_t dx=u.x.floorInt()-7360,dz=u.z.floorInt()-2272;
        firstVisited|=dx*dx+dz*dz<64*64;
        if(tick==60) {
            require(fxLen(u.x-start.first,u.z-start.second)>Fixed::fromInt(16),"returned route corner kept the native mover at zero speed");
            require(u.orders[World::currentLeg(u.orders)].controller==controller,"corner advancement replaced the active mission controller");
            require(u.orders.back().missionTarget.value_or(std::pair{u.orders.back().x,u.orders.back().z})==nextTarget,"corner advancement changed the queued destination");
        }
        if(tick%60==0)legal(world,{id});
    }
    describe(world,{id},"returned-corner");
    require(firstVisited&&u.orders.empty(),"returned-corner route did not finish both ordered destinations");
    compact(world,{id},7360,2752,96);rest(world,{id});
}
void formation(PathfindingMode selected) {
    World world;setup(world,selected);auto slow=mover(1),fast=mover(0);fast.maxVel=Fixed::fromInt(4);
    const int a=world.spawn(&slow,800,2048,std::nullopt,0),b=world.spawn(&fast,640,2096,std::nullopt,0);
    for(int id:{a,b}){world.setSquad(id,-1);world.order(id,3200,2048,false);}
    known(world);Fixed maximum;
    for(int tick=0;tick<1200;++tick){const auto x=world.unit(b)->x,z=world.unit(b)->z;world.tick(1.f/30);
        const Fixed step=fxLen(world.unit(b)->x-x,world.unit(b)->z-z);maximum=fxMax(maximum,step);
        require(step<=slow.maxVel+Fixed::raw(4),"formation exceeded its slowest member's speed");}
    require(world.unit(a)->x>Fixed::fromInt(1500)&&world.unit(b)->x>Fixed::fromInt(1400),"mixed-speed formation made insufficient progress");
    std::printf("scenario=formation maximum_step=%.6f\n",maximum.toFloat());legal(world,{a,b});
}
void lifecycle(PathfindingMode selected) {
    World world;setup(world,selected);std::array<UnitType,3> types{mover(0),mover(1),mover(2)};
    const auto ids=cohort(world,types,16);for(int id:ids)world.order(id,3200,2048,false);known(world);
    for(int tick=0;tick<300;++tick)world.tick(1.f/30);
    world.stop(ids[0]);world.order(ids[1],640,3008,false);
    const int target=world.spawn(&types[0],3008,3008,std::nullopt,0);world.guard(ids[2],target,false);
    // Install a terrain/physical obstacle before the group reaches it.
    obstacles(world,{{112,96,8,56}});world.destroy(target);
    for(int tick=0;tick<240;++tick)world.tick(1.f/30);
    const auto stopped=std::pair{world.unit(ids[0])->x,world.unit(ids[0])->z};
    world.order(ids[2],640,3072,false);
    for(int tick=0;tick<12000;++tick){world.tick(1.f/30);if(tick%60==0)legal(world,ids);if(finished(world,ids))break;}
    describe(world,ids,"lifecycle");require(finished(world,ids),"retargeting or an obstacle change retained stale coordination");
    require(std::pair{world.unit(ids[0])->x,world.unit(ids[0])->z}==stopped,"cancelled mover resumed an old group route");
    compact(world,{ids[1]},640,3008,96);compact(world,{ids[2]},640,3072,96);
    std::vector<int> continuing(ids.begin()+3,ids.end());compact(world,continuing,3200,2048,320);rest(world,ids);
}
void production(PathfindingMode selected,bool mobile) {
    World world;setup(world,selected);auto type=mover(0),producer=mover(1);producer.id="cooperative-producer";
    producer.isBuilder=true;producer.workerTime=1000;producer.footX=producer.footZ=6;if(!mobile)producer.maxVel={};
    const int parent=world.spawn(&producer,640,1600,std::nullopt,0);world.player(0).mana=1e9;
    world.setRepeat(parent,&type);world.order(parent,3200,2048,false);known(world);
    std::vector<int> ids;bool stopped=false;
    for(int tick=0;tick<14000;++tick){world.tick(1.f/30);ids.clear();
        for(const auto& u:world.units())if(u.id!=parent&&u.alive()&&!u.underConstruction)ids.push_back(u.id);
        if(!stopped&&ids.size()>=24){world.stop(parent);stopped=true;}
        if(stopped&&finished(world,ids))break;}
    describe(world,ids,mobile?"mobile-production":"building-production");
    require(stopped&&ids.size()>=24,"production never created the requested cohort");require(finished(world,ids),"produced cohort jammed at its rally");
    compact(world,ids,3200,2048,448);legal(world,ids);rest(world,ids);
}
}
int main(int argc,char** argv) {
    if(argc>3){std::fprintf(stderr,"usage: cooperative_group_test [MODE] [CASE]\n");return 2;}
    try {
        const auto selected=mode(argc>1?argv[1]:"cooperative");const std::string_view only=argc>2?argv[2]:"all";int ran=0,failed=0;
        const auto run=[&](const char* name,const std::function<void()>& test){if(only!="all"&&only!=name)return;++ran;
            try{test();std::printf("PASS %s\n",name);}catch(const std::exception& e){++failed;std::printf("FAIL %s: %s\n",name,e.what());}};
        run("returned-corner",[&]{returnedCorner(selected);});run("open",[&]{traverse(selected,true,false,false,false);});run("boats",[&]{traverse(selected,true,true,false,false);});
        run("bottleneck",[&]{traverse(selected,true,false,true,false);});run("queued",[&]{traverse(selected,true,false,false,true);});
        run("opposing",[&]{opposing(selected);});run("formation",[&]{formation(selected);});run("lifecycle",[&]{lifecycle(selected);});
        run("mixed-passing-bay",[&]{opposing(selected,true);});
        run("single-file",[&]{opposing(selected,false,true);});
        run("thin-wall-opposing",[&]{opposing(selected,false,false,true);});
        run("production",[&]{production(selected,false);production(selected,true);});
        run("determinism",[&]{require(traverse(selected,true,false,true,false)==traverse(selected,false,false,true,false),"serial and worker simulation checkpoints differ");});
        return !ran||failed?1:0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
