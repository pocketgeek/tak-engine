#include "sim/flowfield.h"
#include "sim/flowservice.h"
#include "sim/flowsnapshot.h"
#include "sim/flowobstacles.h"
#include "sim/flowcost.h"
#include "sim/flowlocal.h"
#include "sim/flowroute.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <future>
#include <queue>
#include <stdexcept>
#include <string>
using namespace tak::sim::flow;
namespace {
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
std::shared_ptr<const Topology> build(int w,int h,const std::vector<uint16_t>& costs,size_t quantum=137) {
    TopologyBuilder builder(w,h);
    for(int z=0;z<h;++z)for(int x=0;x<w;++x)builder.setCost({x,z},costs[size_t(z)*w+x]);
    while(!builder.done()&&!builder.failed())check(builder.step(quantum)<=quantum,"topology work bound");
    check(!builder.failed(),"unexpected topology limit");return builder.finish();
}
std::shared_ptr<Destination> destination(std::shared_ptr<const Topology> topo,Cell goal,size_t quantum=37) {
    auto d=std::make_shared<Destination>(std::move(topo),std::vector<Cell>{goal});
    while(!d->done())check(d->step(quantum)<=quantum,"destination work bound");
    return d;
}
Field field(std::shared_ptr<const Destination> d,int tile,size_t quantum=31) {
    FieldBuilder b(std::move(d),tile);while(!b.done())check(b.step(quantum)<=quantum,"integration work bound");
    return b.field();
}
int travel(std::shared_ptr<const Destination> d,Cell start,Cell goal,std::map<int,Field>& cache) {
    const auto& t=d->topology();Cell at=start;int steps=0;
    while(at!=goal && steps++<t.width*t.height) {
        const int tile=t.tileAt(at);
        if(!cache.contains(tile))cache.emplace(tile,field(d,tile));
        Cell next;
        check(cache.at(tile).next(at,next),"reachable field has no direction");
        check(t.cost(next)!=0,"field entered blocked cell");
        if(at.x!=next.x && at.z!=next.z)check(t.cost({at.x,next.z})&&t.cost({next.x,at.z}),"diagonal corner cut");
        at=next;
    }
    check(at==goal,"field cycle or excessive route");return steps;
}
}
int main(int argc,char** argv) {
    try {
        {
            const auto cost=[](bool road,int ground,bool upright=false,bool floater=false,bool hover=false,int waterline=0) {
                return terrainCost(6,road,ground,20,upright,floater,hover,waterline,131072,32768);
            };
            check(cost(false,25)==64,"land travel cost");
            check(cost(false,15)==128,"water multiplier missing");
            check(cost(true,15)==32,"road multiplier must win over water");
            check(cost(false,15,false,true,false,0)==64,"surface floater should not use water multiplier");
            check(cost(false,15,false,true,false,3)==128,"submerged floater waterline missing");
            check(cost(false,15,true,false,true,0)==64,"upright hover sea clamp missing");
            check(cost(false,15,true,false,true,3)==128,"upright hover waterline missing");
            check(cost(false,15,false,false,true,3)==64,"supported hover should rest on sea");
            check(terrainCost(4,false,25,20,false,false,false,0,65536,65536)==96,"rough terrain cost");
            check(terrainCost(6,true,25,20,false,false,false,0,0,65536)==255,"zero speed bounded cost");
        }
        {
            Obstacles gates(192,96,32,1);
            Obstacles::Stamp gate{4,4,2,1,"oc",false,2};
            gates.set(1,gate);while(!gates.settled())gates.step(3);
            check(gates.blocked(4,4,2)&&!gates.blocked(5,4,2),"automatic gate must permit only passage cells");
            check(gates.blocked(5,4)&&gates.blocked(5,4,1),"automatic gate exposed to another owner");
            gates.set(2,Obstacles::Stamp{5,4,1,1,{},false});while(!gates.settled())gates.step(3);
            check(gates.blocked(5,4,2),"nongate overlap erased by gate permission");
            gates.set(2,std::nullopt);while(!gates.settled())gates.step(3);
            check(!gates.blocked(5,4,2),"gate permission not restored after overlap removed");
            auto captured=gate;captured.automaticGateOwner=3;gates.set(1,captured);
            while(!gates.settled())gates.step(3);
            check(gates.blocked(5,4,2)&&gates.blocked(5,4,3)&&gates.gateLimitHits()>0,"gate permission cap must block conservatively");
            captured.open=true;gates.set(1,captured);while(!gates.settled())gates.step(3);
            check(!gates.blocked(5,4,2),"open gate remained blocked after permission cap");
            gates.set(1,std::nullopt);while(!gates.settled())gates.step(3);
            check(!gates.blocked(4,4)&&gates.gateTiles()==1,"gate tile reuse bound");
            Obstacles overlap(32,32);
            overlap.set(1,gate);overlap.set(2,gate);while(!overlap.settled())overlap.step(3);
            check(!overlap.blocked(5,4,2),"overlapping same-owner gates blocked");
            auto enemy=gate;enemy.automaticGateOwner=3;overlap.set(3,enemy);
            while(!overlap.settled())overlap.step(3);
            check(overlap.blocked(5,4,2)&&overlap.blocked(5,4,3),"mixed-owner overlap exposed");
            overlap.set(3,std::nullopt);while(!overlap.settled())overlap.step(3);
            check(!overlap.blocked(5,4,2),"mixed-owner removal did not restore owner permission");
        }
        const bool bench=argc>1&&std::string(argv[1])=="--bench";
        auto started=std::chrono::steady_clock::now();
        for(int size:{33,64,65,128,193}) {
            std::vector<uint16_t> cost(size_t(size)*size,1);
            // A wall splitting a tile's connectivity, with a distant opening.
            for(int z=0;z<size-2;++z)cost[size_t(z)*size+size/2]=0;
            auto t=build(size,size,cost);
            const Cell goal{size-2,2};auto d=destination(t,goal);
            std::map<int,Field> cache;
            check(d->reachable({1,1}),"valid detour wrongly disconnected");
            travel(d,{1,1},goal,cache);
            for(const auto& [tile,f]:cache)check(f.hash()==field(d,tile,8192).hash(),"quantum changes field");
            auto repeat=build(size,size,cost,8192);
            auto repeated=destination(repeat,goal,8192);
            for(const auto& [tile,f]:cache)check(f.hash()==field(repeated,tile).hash(),"topology quantum changes route");
        }
        {
            std::vector<uint16_t> cost(128*128,1);
            for(int z=0;z<128;++z)cost[size_t(z)*128+32]=0;
            auto d=destination(build(128,128,cost),{80,20});
            check(!d->reachable({20,20}),"different components in same tile falsely connected");
            std::map<int,Field> cache;travel(d,{40,20},{80,20},cache);
        }
        {
            std::vector<uint16_t> cost(64*64,1);cost[1]=cost[64]=0;
            auto d=destination(build(64,64,cost),{2,2});
            check(!d->reachable({0,0}),"diagonal-only corner reported reachable");
            auto f=field(d,0);Cell next;check(!f.next({64,0},next),"wrong tile sample accepted");
        }
        {
            std::vector<uint16_t> cost(64*64,1);
            for(int x=4;x<30;++x)cost[32*64+x]=100;
            auto d=destination(build(64,64,cost),{32,32});std::map<int,Field> cache;
            check(travel(d,{2,32},{32,32},cache)<40,"weighted field did not avoid expensive cells");
            Cell next;check(cache.at(0).next({3,32},next)&&next.z!=32,"weighted field crosses expensive strip");
        }
        {
            TopologyBuilder limited(65,65,Limits{1,10,10});check(limited.failed(),"tile limit ignored");
            TopologyBuilder b(64,64,Limits{1,1,10});
            b.setCost({0,0},1);b.setCost({10,10},1);
            while(!b.done()&&!b.failed())b.step(7);
            check(b.failed()&&!b.finish(),"component limit ignored");
            TopologyBuilder e(128,64,Limits{2,2,1});
            for(int z=0;z<64;++z)for(int x=0;x<128;++x)e.setCost({x,z},1);
            while(!e.done()&&!e.failed())e.step(7);
            check(e.failed(),"edge limit ignored");
        }
        {
            auto old=build(128,64,std::vector<uint16_t>(128*64,1));
            TopologyBuilder edited(128,64);edited.reuseTile(0,old->tiles[0]);
            for(int z=0;z<64;++z)for(int x=64;x<128;++x)edited.setCost({x,z},x==80?0:1);
            while(!edited.done())edited.step(512);
            auto current=edited.finish();
            check(current->tiles[0]==old->tiles[0],"unchanged tile not shared");
            check(old->cost({80,20})==1&&current->cost({80,20})==0,"edit mutated published snapshot");
            check(!destination(current,{100,20})->reachable({20,20}),"new blockage missed");
        }
        {
            auto d=destination(build(512,512,std::vector<uint16_t>(512*512,1)),{500,500});
            std::map<int,Field> cache;
            check(travel(d,{1,1},{500,500},cache)<560,"open-ground hierarchy produces a long L-shaped route");
        }
        {
            auto topo=build(64,64,std::vector<uint16_t>(64*64,1));
            const Cell goal{60,32};
            Service service;
            check(service.bind(1,1,topo,{goal}),"wide-front field bind failed");
            for(int tick=0;tick<100;++tick){service.sample(1,{3,8});service.tick();}
            const auto integrated=field(destination(topo,goal),0);
            int canonicalLow=64,canonicalHigh=0,straightLow=64,straightHigh=0;
            for(int z=8;z<=28;z+=4) {
                Cell canonical{3,z},straight=canonical;
                for(int step=0;step<24;++step) {
                    const auto normal=service.sample(1,canonical);
                    const auto preferred=service.sample(1,straight,goal);
                    check(normal.status==Service::Status::Ready&&preferred.status==Service::Status::Ready,
                          "wide-front sample not ready");
                    const int dx=std::abs(preferred.next.x-straight.x),dz=std::abs(preferred.next.z-straight.z);
                    check(integrated.distance[size_t(straight.z)*64+straight.x]==
                          integrated.distance[size_t(preferred.next.z)*64+preferred.next.x]+(dx&&dz?1448u:1024u),
                          "lane preservation chose a more expensive or non-descending step");
                    canonical=normal.next;straight=preferred.next;
                }
                canonicalLow=std::min(canonicalLow,canonical.z);canonicalHigh=std::max(canonicalHigh,canonical.z);
                straightLow=std::min(straightLow,straight.z);straightHigh=std::max(straightHigh,straight.z);
                for(int step=0;step<100&&straight!=goal;++step) {
                    const auto next=service.sample(1,straight,goal);
                    check(next.status==Service::Status::Ready,"lane preservation failed to finish");straight=next.next;
                }
                check(straight==goal,"lane preservation cycles before goal");
            }
            check(straightHigh-straightLow>=10&&canonicalHigh-canonicalLow<straightHigh-straightLow,
                  "equal-cost straight choice collapsed the open-ground front width");
            check(service.fields()==1&&service.destinations()==1,"lane preservation duplicated shared fields");
        }
        {
            auto topo=build(64,64,std::vector<uint16_t>(64*64,1));
            const Cell goal{60,60};Service service;
            check(service.bind(1,1,topo,{goal}),"diagonal-front field bind failed");
            for(int tick=0;tick<100;++tick){service.sample(1,{4,4});service.tick();}
            const auto integrated=field(destination(topo,goal),0);
            // A touching square cohort must advance in parallel near45 degrees,
            // not have its two triangular halves fold into the same centerline.
            for(int z=4;z<16;++z)for(int x=4;x<16;++x) {
                Cell at{x,z};
                for(int step=0;step<24;++step) {
                    const auto result=service.sample(1,at,goal);
                    check(result.status==Service::Status::Ready,"diagonal-front sample not ready");
                    check(result.next==Cell{at.x+1,at.z+1},
                          "diagonal-front tie choice merges adjacent lanes into a seam");
                    check(integrated.distance[size_t(at.z)*64+at.x]==
                          integrated.distance[size_t(result.next.z)*64+result.next.x]+1448u,
                          "diagonal lane chose a more expensive or non-descending step");
                    at=result.next;
                }
                check(at.x-at.z==x-z,"diagonal front lost its lateral offset");
            }
            check(service.fields()==1&&service.destinations()==1,"diagonal lanes duplicated shared fields");
        }
        {
            uint32_t rng=97231;std::vector<uint16_t> costs(64*64);
            for(auto& cost:costs) {rng=rng*1664525u+1013904223u;cost=(rng>>24)<40?0:1+(rng%255);}
            const Cell goal{32,32};costs[32*64+32]=1;
            auto d=destination(build(64,64,costs),goal);const auto f=field(d,0);
            std::vector<uint32_t> reference(64*64,kUnreachable);
            using Node=std::pair<uint32_t,int>;std::priority_queue<Node,std::vector<Node>,std::greater<Node>> q;
            reference[32*64+32]=0;q.push({0,32*64+32});
            while(!q.empty()) {
                const auto [distance,index]=q.top();q.pop();if(distance!=reference[index])continue;
                const int x=index%64,z=index/64;
                for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx) {
                    if(!dx&&!dz)continue;
                    const int nx=x+dx,nz=z+dz;
                    if(nx<0||nz<0||nx>=64||nz>=64||!costs[nz*64+nx])continue;
                    if(dx&&dz&&(!costs[z*64+nx]||!costs[nz*64+x]))continue;
                    const uint32_t candidate=distance+costs[nz*64+nx]*(dx&&dz?1448:1024);
                    if(candidate<reference[nz*64+nx]) {reference[nz*64+nx]=candidate;q.push({candidate,nz*64+nx});}
                }
            }
            check(std::equal(reference.begin(),reference.end(),f.distance.begin()),"indexed-heap integration differs from reference Dijkstra");
            std::printf("flowfield golden: %016llx\n",static_cast<unsigned long long>(f.hash()));
        }
        // Independent jobs may finish in any order; step quanta and scheduling
        // never change their published content.
        {
            auto d=destination(build(128,128,std::vector<uint16_t>(128*128,1)),{100,100});
            std::vector<std::future<Field>> jobs;
            for(int tile=0;tile<4;++tile)jobs.push_back(std::async(std::launch::async,[d,tile]{return field(d,tile,13);}));
            for(int tile=3;tile>=0;--tile)check(jobs[size_t(tile)].get().hash()==field(d,tile,4096).hash(),
                "parallel completion order changes field");
        }
        // Reference flood fill checks topology connectivity across irregular
        // tile boundaries and randomly fragmented components.
        {
            uint32_t rng=12345;
            for(int run=0;run<12;++run) {
                constexpr int w=133,h=71;std::vector<uint16_t> costs(w*h);
                for(auto& c:costs) {rng=rng*1664525u+1013904223u;c=(rng>>24)<80?0:1;}
                const Cell goal{110,30};costs[goal.z*w+goal.x]=1;
                auto d=destination(build(w,h,costs),goal);
                std::vector<bool> seen(w*h);std::queue<Cell> q;q.push(goal);seen[goal.z*w+goal.x]=true;
                while(!q.empty()) {
                    const Cell c=q.front();q.pop();
                    for(Cell n:std::array<Cell,4>{{{c.x-1,c.z},{c.x+1,c.z},{c.x,c.z-1},{c.x,c.z+1}}}) {
                        if(n.x<0||n.z<0||n.x>=w||n.z>=h)continue;
                        const int i=n.z*w+n.x;if(!costs[i]||seen[i])continue;
                        seen[i]=true;q.push(n);
                    }
                }
                std::map<int,Field> cache;
                for(int z=0;z<h;++z)for(int x=0;x<w;++x) {
                    check(d->reachable({x,z})==seen[z*w+x],"hierarchy differs from reference reachability");
                    if(seen[z*w+x] && (x+z*17)%137==0)travel(d,{x,z},goal,cache);
                }
            }
        }
        {
            TopologyBuilder a(32,32),b(32,32);
            for(int z=0;z<32;++z)for(int x=0;x<32;++x) {
                const uint16_t cost=(x==15&&z!=18)?0:uint16_t(1+(x+z)%3);
                a.setCost({x,z},cost);b.setCost({x,z},cost);
            }
            check(a.checksum()==b.checksum(),"identical topology inputs have different checksum");
            const auto before=a.checksum();a.step(1);
            check(a.checksum()!=before,"topology checksum omitted pending work progress");
            while(!a.done())a.step(13);while(!b.done())b.step(37);
            check(a.checksum()==b.checksum(),"topology checksum depends on work quantum");
            auto d1=std::make_shared<Destination>(a.finish(),std::vector<Cell>{{3,3}});
            auto d2=std::make_shared<Destination>(b.finish(),std::vector<Cell>{{3,3}});
            while(!d1->done())d1->step(1);while(!d2->done())d2->step(11);
            check(d1->checksum()==d2->checksum(),"destination checksum depends on work quantum");
            FieldBuilder f1(d1,0),f2(d2,0);
            while(!f1.done())f1.step(1);while(!f2.done())f2.step(47);
            check(f1.checksum()==f2.checksum(),"field checksum depends on work quantum");
        }
        {
            auto topology=build(1024,64,std::vector<uint16_t>(1024*64,1));
            auto progressive=std::make_shared<Destination>(topology,std::vector<Cell>{{1,1}});
            while(!progressive->tileReady(1))check(progressive->step(1)==1,"early connectivity stopped making progress");
            check(!progressive->done()&&progressive->reachable({70,10}),"nearby tile waited for complete destination BFS");
            const auto early=field(progressive,1,13);
            while(!progressive->done())progressive->step(7);
            check(early.hash()==field(progressive,1).hash(),"progressive field differs from completed connectivity");
            Service::Budget budget;budget.destinationWork=1;
            Service serial(budget),workers(budget);
            check(serial.bind(1,1,topology,{{1,1}})&&workers.bind(1,1,topology,{{1,1}}),"progressive service bind");
            Destination reference(topology,std::vector<Cell>{{1,1}});bool publishedEarly=false;
            int firstReady=-1,connected=-1;
            for(int tick=0;tick<100;++tick) {
                const auto a=serial.sample(1,{70,10}),b=workers.sample(1,{70,10});
                check(a.status==b.status&&a.next==b.next,"progressive workers changed publication");
                if(a.status==Service::Status::Ready&&!reference.done())publishedEarly=true;
                if(a.status==Service::Status::Ready&&firstReady<0)firstReady=tick;
                serial.tick();workers.tick(4);reference.step(1);
                if(reference.done()&&connected<0)connected=tick+1;
                check(serial.publishedHash()==workers.publishedHash(),"progressive field hash differs by workers");
                check(serial.checksum()==workers.checksum(),"progressive future state differs by workers");
            }
            check(publishedEarly,"service deferred usable tile until distant graph exhaustion");
            std::printf("progressive destination: first field tick=%d complete connectivity tick=%d\n",firstReady,connected);
            // The region's remaining zero-distance seeds could change every
            // downstream hop distance, so none may publish before seeding ends.
            auto region=std::make_shared<Destination>(topology,GoalRegion{GoalRegion::Kind::Circle,10,10,0,0,100,0});
            region->step(20);
            check(!region->tileReady(0),"partially admitted region exposed an unstable field");
            while(!region->tileReady(0))region->step(1);
            check(!region->done(),"region waited for full graph despite ready goal tile");
            const auto regionEarly=field(region,0);
            while(!region->done())region->step(17);
            check(regionEarly.hash()==field(region,0).hash(),"region progressive field changed after BFS");
            // A reached component cannot hide another unresolved component in
            // its tile. Only completed BFS may classify that island unreachable.
            std::vector<uint16_t> split(128*64,1);
            for(int z=0;z<64;++z)split[size_t(z)*128+32]=0;
            auto divided=build(128,64,split);
            auto island=std::make_shared<Destination>(divided,std::vector<Cell>{{1,1}});
            check(!island->tileReady(0),"partly reached tile incorrectly ready");
            while(!island->done())island->step(1);
            check(island->tileReady(0)&&!island->reachable({40,1}),"unreachable tile component lost final classification");
            FieldBuilder finalIsland(island,0);while(!finalIsland.done())finalIsland.step(19);
            Cell next;check(!finalIsland.field().next({40,1},next),"unreachable island gained a route");
        }
        {
            auto t=build(193,129,std::vector<uint16_t>(193*129,1));
            Service serial,parallel,one;
            for(int i=0;i<16000;++i)for(Service* s:{&serial,&parallel,&one})
                check(s->bind(i,1,t,{{190,120}}),"shared group admission");
            check(serial.destinations()==1,"same goal creates per-unit fields");
            for(int tick=0;tick<150;++tick) {
                for(Cell c:std::array<Cell,4>{{{1,1},{70,10},{130,90},{190,120}}}) {
                    const auto a=serial.sample(0,c),b=parallel.sample(0,c),d=one.sample(0,c);
                    check(a.status==b.status && a.next==b.next && a.status==d.status && a.next==d.next,
                        "worker count changes publication tick/result");
                }
                serial.tick();parallel.tick(4);one.tick(1);
                check(serial.publishedHash()==parallel.publishedHash()&&serial.publishedHash()==one.publishedHash(),
                    "parallel publication differs");
                check(serial.checksum()==parallel.checksum()&&serial.checksum()==one.checksum(),
                    "parallel service future state differs");
            }
            check(serial.sample(0,{190,120}).status==Service::Status::Arrived,"goal not published");
            check(serial.sample(0,{1,1}).status==Service::Status::Ready,"shared field not ready");
            serial.invalidate(1);check(serial.bindings()==0&&serial.fields()==0&&serial.destinations()==0,
                "invalidated profile retains bindings/fields");
            Service::Budget budget;budget.destinations=1;budget.fields=1;budget.builders=1;budget.bindings=2;
            Service limited(budget);check(limited.bind(1,1,t,{{20,20}}),"first limited group refused");
            check(!limited.bind(2,1,t,{{100,100}}),"active destination evicted");
            check(limited.bind(1,1,t,{{30,30}}),"sole subscriber cannot replace full-capacity destination");
            check(limited.destinations()==1&&limited.bindings()==1,"replacement exceeded destination cap");
            check(limited.bind(2,1,t,{{30,30}}),"shared destination rejected at capacity");
            check(!limited.bind(1,1,t,{{40,40}}),"replacement evicted another active subscriber");
            limited.cancel(2);
            limited.cancel(1);check(limited.bind(2,1,t,{{100,100}}),"unused destination not evicted");
            for(int tick=0;tick<100;++tick) {limited.sample(2,{1,1});limited.sample(2,{100,100});limited.tick(4);}
            check(limited.fields()<=1&&limited.builders()<=1,"field/builder bounds exceeded");
            limited.invalidate(1);check(limited.builders()==0,"cancel leaves worker builder");
        }
        {
            constexpr int w=133,h=75;std::vector<uint16_t> raw(w*h,1);uint32_t rng=17;
            for(auto& c:raw) {rng=rng*1664525u+1013904223u;c=(rng>>24)<10?0:1+(rng%8);}
            for(auto [fx,fz]:std::array<std::pair<int,int>,5>{{{1,1},{2,2},{3,5},{8,2},{7,7}}}) {
                const auto sample=[&](int x,int z){return raw[size_t(z)*w+x];};
                SnapshotBuilder b(w,h,fx,fz,sample);
                while(!b.done()&&!b.failed())check(b.step(37)<=37,"snapshot budget exceeded");
                check(!b.failed(),"snapshot unexpectedly failed");const auto original=b.finish();
                for(int z=0;z<h;++z)for(int x=0;x<w;++x) {
                    bool fits=true;
                    for(int dz=0;dz<fz;++dz)for(int dx=0;dx<fx;++dx) {
                        const int nx=x-fx/2+dx,nz=z-fz/2+dz;
                        fits=fits&&nx>=0&&nz>=0&&nx<w&&nz<h;
                        if(fits)fits=raw[size_t(nz)*w+nx]!=0;
                    }
                    check(original->cost({x,z})==(fits?raw[size_t(z)*w+x]:0),"summed-area footprint differs from brute force");
                }
                // A conservative dirty notification must not invalidate shared
                // destinations when the effective costs have not changed.
                SnapshotBuilder unchanged(w,h,fx,fz,sample,original);
                while(!unchanged.done()&&!unchanged.failed())check(unchanged.step(13)<=13,"unchanged snapshot budget");
                check(unchanged.finish()==original,"unchanged costs replaced snapshot identity");
                SnapshotBuilder clean(w,h,fx,fz,sample,original,std::vector<bool>(original->tiles.size(),false));
                while(!clean.done()&&!clean.failed())clean.step(1);
                check(clean.finish()==original&&clean.samples()==0,"clean snapshot sampled terrain");
                const auto before=raw[63*w+63];raw[63*w+63]=before?0:1;
                std::vector<bool> dirty(original->tiles.size());
                SnapshotBuilder::dirtyRectangle(dirty,w,h,fx,fz,63,63,1,1);
                SnapshotBuilder edited(w,h,fx,fz,sample,original,dirty),fresh(w,h,fx,fz,sample);
                while(!edited.done()&&!edited.failed())edited.step(97);
                while(!fresh.done()&&!fresh.failed())fresh.step(4096);
                check(edited.done()&&fresh.done(),"edited snapshot failed");
                for(int z=0;z<h;++z)for(int x=0;x<w;++x)
                    check(edited.finish()->cost({x,z})==fresh.finish()->cost({x,z}),"dirty tile missed footprint halo");
                for(size_t tile=0;tile<dirty.size();++tile)if(!dirty[tile])
                    check(edited.finish()->tiles[tile]==original->tiles[tile],"unchanged footprint tile copied");
                raw[63*w+63]=before;
            }
        }
        {
            Obstacles obstacles(128,96);
            const Obstacles::Stamp closed{63,63,2,2,"oc.c",false};
            auto open=closed;open.open=true;
            check(obstacles.set(1,closed)==Obstacles::Edit::Changed,"initial structure change missed");
            while(!obstacles.settled())check(obstacles.step(1)==1,"structure work budget");
            check(obstacles.blocked(63,63)&&obstacles.blocked(64,63)&&!obstacles.blocked(63,64),"closed yard mask");
            obstacles.set(2,closed);obstacles.set(1,open);
            while(!obstacles.settled())obstacles.step(3);
            check(obstacles.blocked(64,63),"overlapping structure was erased by gate opening");
            obstacles.set(2,std::nullopt);
            while(!obstacles.settled())obstacles.step(2);
            check(!obstacles.blocked(64,63)&&obstacles.blocked(63,63),"open yard mask");
            // Coalesce rapid open/close/destruction during an in-flight stamp.
            obstacles.set(1,closed);obstacles.step(3);obstacles.set(1,open);obstacles.step(1);
            obstacles.set(1,std::nullopt);
            while(!obstacles.settled())obstacles.step(1);
            for(int z=0;z<96;++z)for(int x=0;x<128;++x)check(!obstacles.blocked(x,z),"cancelled structure left occupancy");
            obstacles.set(3,Obstacles::Stamp{-1,-1,3,3,{},false});
            while(!obstacles.settled())obstacles.step(7);
            check(obstacles.blocked(0,0)&&obstacles.blocked(1,1)&&!obstacles.blocked(2,2),"clipped structure stamp");
            Obstacles limited(32,32,1);
            const Obstacles::Stamp single{4,4,2,2,{},false};
            check(limited.set(1,single)==Obstacles::Edit::Changed,"first capped structure refused");
            check(limited.set(2,single)==Obstacles::Edit::Full,"structure cap exceeded");
            check(limited.set(1,std::nullopt)==Obstacles::Edit::Changed,"full cache rejects removal");
            while(!limited.settled())limited.step(1);
            check(limited.set(2,single)==Obstacles::Edit::Changed,"structure capacity did not recover");
            while(!limited.settled())limited.step(1);
            check(limited.blocked(4,4),"admitted structure was not stamped");
            Obstacles edits(32,32);std::map<int,Obstacles::Stamp> expected;uint32_t rng=71;
            for(int i=0;i<3000;++i) {
                rng=rng*1664525u+1013904223u;const int id=int(rng%64);
                if((rng>>8)%4==0) {edits.set(id,std::nullopt);expected.erase(id);}
                else {
                    const Obstacles::Stamp stamp{int((rng>>10)%34)-1,int((rng>>17)%34)-1,2,2,"oc.c",bool(rng&128)};
                    edits.set(id,stamp);expected[id]=stamp;
                }
                check(edits.step(i%11)<=size_t(i%11),"coalesced obstacle work budget");
            }
            while(!edits.settled())edits.step(13);
            for(int z=0;z<32;++z)for(int x=0;x<32;++x) {
                bool occupied=false;
                for(const auto& [id,s]:expected) {
                    (void)id;if(x<s.x||z<s.z||x>=s.x+s.w||z>=s.z+s.h)continue;
                    const char yard=s.yard[size_t(z-s.z)*s.w+x-s.x];
                    occupied|=yard!='.'&&!(s.open&&(yard=='c'||yard=='C'));
                }
                check(edits.blocked(x,z)==occupied,"coalesced occupancy differs from full reconstruction");
            }
        }
        {
            auto topology=build(128,96,std::vector<uint16_t>(128*96,1));
            for(const GoalRegion region:std::array<GoalRegion,3>{{
                {GoalRegion::Kind::Circle,60,45,0,0,400,0},
                {GoalRegion::Kind::Ring,60,45,0,0,576,25600},
                {GoalRegion::Kind::Rectangle,20,20,110,70,0,0}}}) {
                std::vector<Cell> explicitGoals;
                for(int z=0;z<96;++z)for(int x=0;x<128;++x)if(region.contains({x,z}))explicitGoals.push_back({x,z});
                auto compact=std::make_shared<Destination>(topology,region);
                auto reference=std::make_shared<Destination>(topology,explicitGoals);
                while(!compact->done())check(compact->step(7)<=7,"region seeding work budget");
                while(!reference->done())reference->step(13);
                for(size_t tile=0;tile<topology->tiles.size();++tile)
                    check(field(compact,int(tile)).hash()==field(reference,int(tile)).hash(),"compact goal differs from explicit goal cells");
            }
            // A controller region may contain far more than 4,096 cells without
            // a per-unit vector or silently dropping part of its target area.
            auto large=build(512,512,std::vector<uint16_t>(512*512,1));
            Service shared;
            const GoalRegion wide{GoalRegion::Kind::Circle,256,256,0,0,160000,0};
            for(int id=0;id<16000;++id)check(shared.bindRegion(id,9,large,wide),"large shared region rejected");
            check(shared.destinations()==1&&shared.bytes()<2*1024*1024,"region storage grows with cell count per unit");
            for(int tick=0;tick<100;++tick) {shared.sample(0,{1,1});shared.tick();}
            check(shared.sample(0,{1,1}).status==Service::Status::Arrived,"large region lost distant accepted cells");
        }
        {
            const GoalRegion goal{GoalRegion::Kind::Circle,120,64,0,0,0,0};
            auto sample=[](int x,int z)->uint16_t{return x==60&&z<75?0:1;};
            std::vector<Cell> expectedRoute;
            for(size_t quantum:{size_t(1),size_t(31),size_t(4096)}) {
                LocalRoute route(128,128,2,2,{48,64},{120,64},goal,sample);
                while(!route.done())check(route.step(quantum)<=quantum,"local route exceeded work budget");
                check(route.progresses(),"local route failed to find detour progress");
                check(!route.reachesGoal(),"greedy local boundary prefix accepted as mission guidance");
                if(expectedRoute.empty())expectedRoute=route.route();
                check(route.route()==expectedRoute,"local route changes with work quantum");
                check(route.route().size()<=64&&route.bytes()<512*1024,"local route resource bound");
                for(const auto c:route.route())for(int dz=-1;dz<=0;++dz)for(int dx=-1;dx<=0;++dx)
                    check(sample(c.x+dx,c.z+dz)!=0,"local fallback crossed blocked footprint");
            }
            const GoalRegion nearby{GoalRegion::Kind::Circle,55,64,0,0,0,0};
            LocalRoute finish(128,128,1,1,{48,64},{55,64},nearby,sample);
            while(!finish.done())finish.step(31);
            check(finish.reachesGoal(),"local complete route rejected");
            LocalRoute boxed(128,128,1,1,{48,64},{120,64},goal,
                [](int x,int z)->uint16_t{return x==48&&z==64?1:0;});
            while(!boxed.done())boxed.step(13);
            check(!boxed.progresses(),"boxed local route invented progress");
        }
        {
            std::vector<Cell> route;
            for(Cell cell:std::vector<Cell>{{0,0},{1,1},{2,2},{3,2},{4,2},{3,2},{2,2},{2,1},{2,0}})
                appendRouteCorner(route,cell);
            check(route==std::vector<Cell>{{0,0},{2,2},{4,2},{2,2},{2,0}},
                "flow route compression lost a bend, diagonal corner, or backtrack");
            route.clear();
            for(int cell=0;cell<64;++cell)appendRouteCorner(route,Cell{cell,cell});
            check(route==std::vector<Cell>{{0,0},{63,63}},"straight flow route retained per-cell steering points");
        }
        {
            const auto clear=[](Cell){return true;};
            const auto end=[](Cell c){return c==Cell{8,3};};
            check(directRoute(Cell{0,0},Cell{8,3},64,clear,end),"valid slot supercover rejected");
            check(!directRoute(Cell{0,0},Cell{8,3},64,
                [](Cell c){return c!=Cell{4,1};},end),"slot ray cut a diagonal terrain edge");
            check(!directRoute(Cell{0,0},Cell{65,0},64,clear,
                [](Cell c){return c==Cell{65,0};}),"slot ray exceeded local work bound");
            check(directRoute(Cell{0,0},Cell{8,0},64,
                [](Cell c){return c.x!=8;},[](Cell c){return c.x==7;}),
                "interaction-area ray demanded occupied target centre");
            check(!directRoute(Cell{0,0},Cell{8,0},64,
                [](Cell c){return c.x!=4;},[](Cell c){return c.x>=7;}),
                "opposite-side-of-wall slot accepted");
        }
        {
            ProofBudget budget(8);
            std::array<bool,8> served{};
            // Every early ID keeps requesting work, even after its successful
            // proof: later callers must still get a complete proof opportunity.
            for(int tick=0;tick<8;++tick) {
                size_t work=0;
                for(int id=1;id<=8;++id)if(budget.spend(id,3)) {served[size_t(id-1)]=true;work+=3;}
                check(work<=8,"arrival proof quota exceeded");budget.tick();
            }
            check(std::all_of(served.begin(),served.end(),[](bool value){return value;}),
                  "low-ID arrival proofs starved a later valid destination");
            ProofBudget wrap(2);
            check(wrap.spend(8,2),"proof quota setup failed");wrap.tick();
            check(!wrap.accepts(1)&&!wrap.accepts(8),"exhausted proof priority did not advance");
            wrap.tick();check(wrap.accepts(1),"proof priority did not wrap after a deferred tick");
            ProofBudget ordinary;
            check(ordinary.spend(8,2),"ordinary proof failed");ordinary.tick();
            check(ordinary.accepts(1),"unsaturated proof quota unnecessarily excluded an earlier unit");
        }
        if(bench) {
            const int size=2048;auto t=build(size,size,std::vector<uint16_t>(size_t(size)*size,1),65536);
            auto d=destination(t,{size-2,size-2},65536);std::map<int,Field> cache;
            for(int i=0;i<16000;++i)travel(d,{1+i%125,1+(i/125)%125},{size-2,size-2},cache);
            std::printf("16000 shared routes: topology=%zu bytes destination=%zu bytes fields=%zu bytes tiles=%zu\n",
                t->bytes(),d->bytes(),cache.size()*sizeof(Field),cache.size());
        }
        std::printf("PASS flowfield topology/integration/limits/immutable edits; %.3fs\n",
            std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL flowfield: %s\n",e.what());return 1;}
}
