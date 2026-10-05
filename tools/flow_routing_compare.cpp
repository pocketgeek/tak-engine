#include "flow_portal_prototype.h"
#include "sim/flowsnapshot.h"
#include <chrono>
#include <cstdio>
#include <future>
#include <map>
#include <queue>
#include <string>

using namespace tak::sim::flow;
using tak::test::PortalRoute;
namespace {
using Clock=std::chrono::steady_clock;
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
struct Fixture {std::string name;int width,height;std::vector<uint16_t> cost;Cell start,goal;};
Fixture fixture(const std::string& name) {
    Fixture f{name,192,192,std::vector<uint16_t>(192*192,1),{4,18},{185,18}};
    if(name=="open")f.goal={185,172};
    else if(name=="weighted") {
        // A direct, expensive middle band competes with an extra-tile detour.
        for(int z=0;z<64;++z)for(int x=72;x<120;++x)f.cost[size_t(z)*192+x]=64;
    }else if(name=="maze") {
        for(int x:{48,96,144})for(int z=0;z<192;++z)
            if(x==96?z>14:z<177)f.cost[size_t(z)*192+x]=0;
    }else if(name=="unreachable") {
        for(int z=0;z<192;++z)f.cost[size_t(z)*192+96]=0;
    }else throw std::invalid_argument("unknown route comparison fixture");
    return f;
}
std::shared_ptr<const Topology> topology(const Fixture& f,std::shared_ptr<const Topology> previous={},std::vector<bool> dirty={}) {
    SnapshotBuilder b(f.width,f.height,1,1,[&](int x,int z){return f.cost[size_t(z)*f.width+x];},previous,dirty);
    while(!b.done()&&!b.failed())b.step(4096);
    check(b.done(),"comparison snapshot failed");return b.finish();
}
std::shared_ptr<Destination> destination(std::shared_ptr<const Topology> t,Cell goal,uint64_t* work=nullptr) {
    auto d=std::make_shared<Destination>(t,std::vector<Cell>{goal});
    while(!d->done()){const auto used=d->step(37);if(work)*work+=used;}
    return d;
}
Field field(std::shared_ptr<const Destination> d,int tile,uint64_t* work=nullptr) {
    FieldBuilder b(d,tile);while(!b.done()){const auto used=b.step(37);if(work)*work+=used;}return b.field();
}
struct Travel {bool arrived=false;uint64_t length=0,cost=0;size_t steps=0;};
template<class Build> Travel travel(const Topology& t,Cell start,Cell goal,Build build,std::map<int,Field>& fields,bool coherent=false) {
    Cell at=start;Travel result;
    const auto load=[&](Cell cell)->const Field& {
        const auto tile=t.tileAt(cell);check(tile>=0,"route left map");
        auto it=fields.find(tile);if(it==fields.end())it=fields.emplace(tile,build(tile)).first;
        return it->second;
    };
    while(at!=goal&&result.steps<size_t(t.width)*t.height) {
        const auto& f=load(at);Cell next;
        if(!f.next(at,next))return result;
        check(t.cost(next)>0,"comparison route entered blocked cell");
        const int dx=next.x-at.x,dz=next.z-at.z;
        check(std::abs(dx)<=1&&std::abs(dz)<=1&&(dx||dz),"comparison route nonlocal step");
        if(dx&&dz)check(t.cost({at.x,next.z})&&t.cost({next.x,at.z}),"comparison route cut corner");
        const auto length=dx&&dz?1448u:1024u,cost=uint32_t(t.cost(at))*length;
        if(coherent) {
            const auto& n=load(next);
            const auto from=f.distance[size_t(at.z%64)*64+at.x%64];
            const auto to=n.distance[size_t(next.z%64)*64+next.x%64];
            check(uint64_t(to)+cost==from,"portal potential failed strict descent");
        }
        result.length+=length;result.cost+=cost;++result.steps;at=next;
    }
    result.arrived=at==goal;check(result.arrived,"comparison field cycle");return result;
}
void finish(PortalRoute& route,size_t quantum) {
    while(!route.done()&&!route.failed())check(route.step(quantum)<=quantum,"portal quota exceeded");
    check(route.done(),"portal capacity exhausted unexpectedly");
}
// A conservative dependency proof for the EXISTING BFS fields. Field seeds
// depend on the exact tile body, goal identity and the allowed outgoing border
// cells, not on unrelated graph components or their numeric component IDs.
// This is comparison code only: all destinations must first finish rebuilding.
bool reusable(const Destination& old,const Destination& fresh,int tile) {
    const auto& a=old.topology();const auto& b=fresh.topology();
    if(!old.done()||!fresh.done()||a.width!=b.width||a.height!=b.height||
       old.goals()!=fresh.goals()||old.region()!=fresh.region()||a.tiles[size_t(tile)]!=b.tiles[size_t(tile)])return false;
    constexpr int dx[4]={0,1,0,-1},dz[4]={-1,0,1,0};
    const int x=tile%a.tilesX*64,z=tile/a.tilesX*64;
    for(int d=0;d<4;++d)for(int i=0;i<64;++i) {
        const Cell c=d==0?Cell{x+i,z}:d==1?Cell{x+63,z+i}:d==2?Cell{x+i,z+63}:Cell{x,z+i};
        const Cell n{c.x+dx[d],c.z+dz[d]};
        const auto exit=[&](const Destination& dest) {
            const auto from=dest.distance(dest.topology().componentAt(c));
            const auto to=dest.distance(dest.topology().componentAt(n));
            return from!=kUnreachable&&to!=kUnreachable&&to+1==from;
        };
        if(exit(old)!=exit(fresh))return false;
    }
    return true;
}
void invalidation(bool report,int repeat) {
    for(const std::string change:{"irrelevant","disconnect","open-shortcut","explore-shortcut","cost-change"}) {
        Fixture before=fixture("open");before.start={4,18};before.goal={185,18};
        if(change=="open-shortcut"||change=="explore-shortcut")
            for(int z=0;z<170;++z)before.cost[size_t(z)*192+128]=0;
        if(change=="irrelevant") {
            for(int x=72;x<=92;++x)before.cost[72*192+x]=before.cost[92*192+x]=0;
            for(int z=72;z<=92;++z)before.cost[size_t(z)*192+72]=before.cost[size_t(z)*192+92]=0;
        }
        Fixture after=before;
        std::vector<bool> dirty(9);
        const auto edit=[&](int x,int z,uint16_t cost) {
            after.cost[size_t(z)*192+x]=cost;
            SnapshotBuilder::dirtyRectangle(dirty,192,192,1,1,x,z,1,1);
        };
        if(change=="irrelevant")edit(80,80,0);
        else if(change=="disconnect")for(int z=0;z<192;++z)edit(128,z,0);
        else if(change=="cost-change")edit(129,18,32);
        else for(int z=8;z<28;++z)edit(128,z,1);
        auto original=topology(before),changed=topology(after,original,dirty);
        auto old=destination(original,before.goal),fresh=destination(changed,after.goal);
        size_t kept=0,rejected=0;uint64_t rebuildWork=0,avoidedWork=0;
        double proofUs=0,rebuildUs=0;
        for(size_t tile=0;tile<original->tiles.size();++tile) {
            const auto oldField=field(old,int(tile));
            auto started=Clock::now();const bool keep=reusable(*old,*fresh,int(tile));
            proofUs+=std::chrono::duration<double,std::micro>(Clock::now()-started).count();
            uint64_t work=0;started=Clock::now();const auto rebuilt=field(fresh,int(tile),&work);
            rebuildUs+=std::chrono::duration<double,std::micro>(Clock::now()-started).count();rebuildWork+=work;
            if(keep){check(oldField.hash()==rebuilt.hash(),"dependency proof preserved stale field");++kept;avoidedWork+=work;}
            else ++rejected;
        }
        if(change=="disconnect")check(!fresh->reachable(before.start),"disconnect retained connectivity");
        if(change=="open-shortcut"||change=="explore-shortcut") {
            std::map<int,Field> oldFields,newFields;
            const auto a=travel(*original,before.start,before.goal,[&](int tile){return field(old,tile);},oldFields);
            const auto b=travel(*changed,after.start,after.goal,[&](int tile){return field(fresh,tile);},newFields);
            check(b.arrived&&b.length<a.length,"opened shortcut remained stale");
            check(rejected>1,"shortcut proof missed a remote dependency");
        }
        check(kept+rejected==9,"dependency profiling lost field");
        if(report)std::printf("invalidation,%s,%d,%zu,%zu,%llu,%llu,%.3f,%.3f\n",change.c_str(),repeat,
            kept,rejected,(unsigned long long)rebuildWork,(unsigned long long)avoidedWork,proofUs,rebuildUs);
    }
}
void compare(const Fixture& f,bool report,int repeat,const std::vector<unsigned>& strides,const std::string& selection) {
    const auto t=topology(f);uint64_t bfsWork=0;
    const auto started=Clock::now();auto d=destination(t,f.goal,&bfsWork);
    std::map<int,Field> bfsFields;
    const auto bfs=travel(*t,f.start,f.goal,[&](int tile){return field(d,tile,&bfsWork);},bfsFields);
    const double bfsMs=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
    if(report&&selection!="portal")std::printf("routing,%s,%d,bfs,0,%d,%zu,%llu,%llu,%llu,0,%zu,%.3f\n",f.name.c_str(),repeat,
        bfs.arrived,bfs.steps,(unsigned long long)bfs.length,(unsigned long long)bfs.cost,(unsigned long long)bfsWork,
        d->bytes()+bfsFields.size()*sizeof(Field)+sizeof(FieldBuilder),bfsMs);
    if(selection=="bfs")return;
    for(unsigned stride:strides) {
        PortalRoute route(t,{f.goal},stride,{});const auto begin=Clock::now();finish(route,137);
        std::map<int,Field> fields;
        const auto routed=travel(*t,f.start,f.goal,[&](int tile){return route.field(tile);},fields,true);
        const auto stats=route.stats();
        const double ms=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
        check(routed.arrived==bfs.arrived,"portal connectivity differs from BFS");
        if(f.name=="weighted")check(routed.cost<bfs.cost,"weighted portal routing missed cheaper global route");
        if(!report) {
            // Different deterministic quanta and reversed independent completion
            // order must publish byte-identical potentials and directions.
            auto job=std::async(std::launch::async,[t,goal=f.goal,stride] {
                PortalRoute repeated(t,{goal},stride,{});finish(repeated,4096);return repeated;
            });
            auto repeated=job.get();
            for(const auto& [tile,field]:fields)check(field.hash()==repeated.field(tile).hash(),"portal quantum/scheduling changed result");
            check(stats.graphWork==repeated.stats().graphWork&&stats.routeWork==repeated.stats().routeWork,
                "portal quantum changed charged work");
        }
        if(report)std::printf("routing,%s,%d,portal,%u,%d,%zu,%llu,%llu,%llu,%llu,%zu,%.3f\n",f.name.c_str(),repeat,stride,
            routed.arrived,routed.steps,(unsigned long long)routed.length,(unsigned long long)routed.cost,
            (unsigned long long)(stats.routeWork+stats.fieldWork),(unsigned long long)stats.graphWork,
            stats.bytes+fields.size()*sizeof(Field),ms);
    }
}
void regressions() {
    auto f=fixture("open");const auto t=topology(f);
    PortalRoute limit(t,{f.goal},16,{2,100});while(!limit.done()&&!limit.failed())limit.step(1);
    check(limit.failed(),"portal node cap ignored");
    PortalRoute edges(t,{f.goal},64,{8192,1});while(!edges.done()&&!edges.failed())edges.step(1);
    check(edges.failed(),"portal edge cap ignored");
    // Portal crossing costs and diagonal clearance use prepared footprints,
    // exactly as the existing profile/exploration-specific Destination does.
    f=fixture("maze");
    SnapshotBuilder shaped(f.width,f.height,3,5,[&](int x,int z){return f.cost[size_t(z)*f.width+x];});
    while(!shaped.done()&&!shaped.failed())shaped.step(257);
    check(shaped.done(),"mixed footprint topology failed");
    PortalRoute large(shaped.finish(),{f.goal},64,{});finish(large,19);
    std::map<int,Field> fields;
    check(travel(*shaped.finish(),f.start,f.goal,[&](int tile){return large.field(tile);},fields,true).arrived,
        "footprint portal route did not arrive");
    // Goal sets are exact and deterministic, including legal boundary samples
    // supplied by circle/ring/rectangle controllers; blocked goals add no seed.
    for(GoalRegion region:std::array<GoalRegion,3>{{
        {GoalRegion::Kind::Circle,170,170,0,0,16,0},
        {GoalRegion::Kind::Rectangle,164,164,176,176,0,0},
        {GoalRegion::Kind::Ring,170,170,0,0,25,9*256}}}) {
        std::vector<Cell> goals;
        const auto [lo,hi]=region.bounds();
        for(int z=lo.z;z<=hi.z;++z)for(int x=lo.x;x<=hi.x;++x)if(region.contains({x,z}))goals.push_back({x,z});
        PortalRoute a(t,goals,64,{});std::reverse(goals.begin(),goals.end());PortalRoute b(t,goals,64,{});
        finish(a,1);finish(b,83);
        for(int tile=0;tile<9;++tile)check(a.field(tile).hash()==b.field(tile).hash(),"goal ordering changed portal field");
    }
    // All-crossing reference on a small map is feasible, while admission stays
    // explicitly capped on the large maps where this prototype is expensive.
    Fixture small{"small",80,24,std::vector<uint16_t>(80*24,1),{2,2},{77,20}};
    const auto st=topology(small);PortalRoute exact(st,{small.goal},1,{});finish(exact,11);
    fields.clear();const auto shortest=travel(*st,small.start,small.goal,[&](int tile){return exact.field(tile);},fields,true);
    check(shortest.arrived&&shortest.length==57*1024+18*1448,"all-crossing open route not exact");
    uint32_t rng=731;
    for(auto& cost:small.cost){rng=rng*1664525u+1013904223u;cost=(rng>>24)<24?0:1+rng%16;}
    small.cost[size_t(small.goal.z)*80+small.goal.x]=1;
    const auto random=topology(small);PortalRoute all(random,{small.goal},1,{});finish(all,23);
    std::vector<uint32_t> reference(small.cost.size(),kUnreachable);
    using Node=std::pair<uint32_t,int>;std::priority_queue<Node,std::vector<Node>,std::greater<Node>> pending;
    const int goal=small.goal.z*80+small.goal.x;reference[size_t(goal)]=0;pending.push({0,goal});
    while(!pending.empty()) {
        const auto [distance,index]=pending.top();pending.pop();if(reference[size_t(index)]!=distance)continue;
        const Cell at{index%80,index/80};
        for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx) {
            const Cell next{at.x+dx,at.z+dz};if((!dx&&!dz)||!random->cost(next))continue;
            if(dx&&dz&&(!random->cost({next.x,at.z})||!random->cost({at.x,next.z})||
                random->tileAt(at)!=random->tileAt(next)))continue;
            const auto candidate=distance+uint32_t(random->cost(next))*(dx&&dz?1448:1024);
            const auto n=size_t(next.z)*80+next.x;
            if(candidate<reference[n]){reference[n]=candidate;pending.push({candidate,int(n)});}
        }
    }
    for(int tile=0;tile<2;++tile) {
        const auto f=all.field(tile);
        for(int z=0;z<24;++z)for(int x=tile*64;x<std::min(80,(tile+1)*64);++x)
            check(f.distance[size_t(z)*64+x%64]==reference[size_t(z)*80+x],
                "all-crossing portal potential differs from independent cell Dijkstra");
    }
}
}
int main(int argc,char** argv) {
    try {
        bool bench=false;int repeats=1;std::vector<unsigned> strides{64,16};std::string selection="both";
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--bench") {bench=true;repeats=5;if(i+1<argc&&argv[i+1][0]!='-')repeats=std::stoi(argv[++i]);}
            else if(arg=="--portal-stride"&&i+1<argc) {
                const int stride=std::stoi(argv[++i]);check(stride>=1&&stride<=64,"invalid portal stride");strides={unsigned(stride)};
            }else if(arg=="--routing"&&i+1<argc)selection=argv[++i];
            else throw std::invalid_argument("usage: flow_routing_compare [--bench [REPEATS]] [--routing bfs|portal|both] [--portal-stride 1..64]");
        }
        check(selection=="both"||selection=="bfs"||selection=="portal","invalid comparison routing switch");
        check(repeats>=1&&repeats<=100,"invalid comparison repeats");
        if(bench) {
            std::puts("# routing,scenario,repeat,algorithm,portal_stride,arrived,steps,length_1024,cost_1024,route_and_field_work,graph_work,owned_payload_estimate_bytes,total_ms");
            std::puts("# invalidation,change,repeat,kept_fields,rejected_fields,rebuild_work,avoidable_work,proof_us,rebuild_us");
        }
        for(int repeat=0;repeat<repeats;++repeat) {
            for(const std::string name:{"open","weighted","maze","unreachable"})compare(fixture(name),bench,repeat,strides,selection);
            invalidation(bench,repeat);
        }
        if(!bench){regressions();std::puts("PASS bounded weighted portal comparison, decreasing potential, capacity, footprints, goals, deterministic quanta and invalidation proofs");}
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"flow routing comparison: %s\n",e.what());return 1;}
}
