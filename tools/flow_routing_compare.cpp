#include "flow_portal_prototype.h"
#include "flow_portal_graph.h"
#include "sim/flowsnapshot.h"
#include <chrono>
#include <cmath>
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
    }else if(name=="large") {
        // 1024x1024 (256 tiles): a serpentine of seven walls with alternating
        // gaps, plus slow patches a hop-count route walks straight through.
        f={name,1024,1024,std::vector<uint16_t>(size_t(1024)*1024,1),{4,500},{1019,520}};
        for(int k=1;k<8;++k)for(int z=0;z<1024;++z)
            if(k%2?z>40:z<984)for(int x=k*128;x<k*128+3;++x)f.cost[size_t(z)*1024+x]=0;
        for(int k=0;k<8;++k)for(int z=300;z<700;++z)for(int x=k*128+20;x<k*128+108;++x)f.cost[size_t(z)*1024+x]=24;
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
enum class Coherence {None,Exact,Descending};
template<class Build> Travel travel(const Topology& t,Cell start,Cell goal,Build build,std::map<int,Field>& fields,Coherence coherent=Coherence::None) {
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
        if(coherent!=Coherence::None) {
            const auto& n=load(next);
            const auto from=f.distance[size_t(at.z%64)*64+at.x%64];
            const auto to=n.distance[size_t(next.z%64)*64+next.x%64];
            // Exact: one coherent potential. Descending: the next tile's
            // potential may be lower than this tile's bound, never higher.
            if(!(coherent==Coherence::Exact?uint64_t(to)+cost==from:uint64_t(to)+cost<=from))std::fprintf(stderr,"descent at %d,%d -> %d,%d from %u to %u cost %u start %d,%d goal %d,%d\n",at.x,at.z,next.x,next.z,from,to,cost,start.x,start.z,goal.x,goal.z);
            check(coherent==Coherence::Exact?uint64_t(to)+cost==from:uint64_t(to)+cost<=from,
                "portal potential failed strict descent");
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
        const auto routed=travel(*t,f.start,f.goal,[&](int tile){return route.field(tile);},fields,Coherence::Exact);
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
    check(travel(*shaped.finish(),f.start,f.goal,[&](int tile){return large.field(tile);},fields,Coherence::Exact).arrived,
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
    fields.clear();const auto shortest=travel(*st,small.start,small.goal,[&](int tile){return exact.field(tile);},fields,Coherence::Exact);
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
// ---- Cached destination-independent portal graph (tools/flow_portal_graph.h)
namespace portal=tak::test::portal;
portal::Seeding gSeeding=portal::Seeding::Bound;
const char* seedingName() {return gSeeding==portal::Seeding::Bound?"bound":"runs";}
// Bound fields form one potential that never increases across tiles; Runs
// fields are acyclic by component potential, so only arrival is checked.
Coherence descending() {return gSeeding==portal::Seeding::Bound?Coherence::Descending:Coherence::None;}
std::shared_ptr<const portal::Graph> buildGraph(std::shared_ptr<const Topology> t,unsigned stride,
        std::shared_ptr<const portal::Graph> previous={},size_t quantum=137,unsigned workers=0,portal::Graph::Limits limits={}) {
    auto g=std::make_shared<portal::Graph>(t,stride,limits,previous);
    while(!g->done()&&!g->failed())check(g->step(quantum,workers)<=quantum,"portal graph quota exceeded");
    check(g->done(),"portal graph capacity exhausted unexpectedly");return g;
}
template<class Goal> std::shared_ptr<portal::Destination> portalDestination(std::shared_ptr<const portal::Graph> g,Goal goal,size_t quantum=37) {
    std::shared_ptr<portal::Destination> d;
    if constexpr(std::is_same_v<Goal,Cell>)d=std::make_shared<portal::Destination>(g,std::vector<Cell>{goal});
    else d=std::make_shared<portal::Destination>(g,goal);
    while(!d->done())check(d->step(quantum)<=quantum,"portal destination quota exceeded");
    return d;
}
Field portalField(std::shared_ptr<const portal::Destination> d,int tile,uint64_t* work=nullptr,size_t quantum=37) {
    portal::FieldBuilder b(d,tile,gSeeding);
    while(!b.done())check(b.step(quantum)<=quantum,"portal field quota exceeded");
    if(work)*work+=b.work();
    return b.field();
}
// Deterministic passable destinations for the amortisation measurement.
std::vector<Cell> sampleGoals(const Fixture& f,size_t count,uint32_t seed) {
    std::vector<Cell> out;
    while(out.size()<count) {
        seed=seed*1664525u+1013904223u;const int x=int((seed>>8)%uint32_t(f.width));
        seed=seed*1664525u+1013904223u;const int z=int((seed>>8)%uint32_t(f.height));
        if(f.cost[size_t(z)*f.width+x])out.push_back({x,z});
    }
    return out;
}
void cached(const Fixture& f,bool report,int repeat,const std::vector<unsigned>& strides) {
    const auto t=topology(f);
    // Reference: production component-hop BFS, destination and fields split.
    uint64_t bfsDestWork=0,bfsFieldWork=0;
    auto bd=destination(t,f.goal,&bfsDestWork);
    std::map<int,Field> bfsFields;
    const auto bfs=travel(*t,f.start,f.goal,[&](int tile){return field(bd,tile,&bfsFieldWork);},bfsFields);
    const auto goals=sampleGoals(f,16,uint32_t(f.width*31+f.height));
    uint64_t bfsMulti=0;for(Cell g:goals)destination(t,g,&bfsMulti);
    for(unsigned stride:strides) {
        auto begin=Clock::now();
        const auto g=buildGraph(t,stride);
        auto d=portalDestination(g,f.goal);
        std::map<int,Field> fields;uint64_t fieldWork=0;
        Travel routed;
        if(bfs.arrived)routed=travel(*t,f.start,f.goal,[&](int tile){return portalField(d,tile,&fieldWork);},fields,descending());
        const double coldMs=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
        // Warm: the graph is already cached; only destination + fields run.
        begin=Clock::now();
        auto warm=portalDestination(g,f.goal);std::map<int,Field> warmFields;
        Travel again;
        if(bfs.arrived)again=travel(*t,f.start,f.goal,[&](int tile){return portalField(warm,tile);},warmFields,descending());
        const double warmMs=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
        check(again.cost==routed.cost&&warm->hash()==d->hash(),"warm portal route differs from cold");
        check(d->reachable(f.start)==bd->reachable(f.start),"cached portal reachability differs from BFS");
        uint64_t multi=0;for(Cell goal:goals){auto m=portalDestination(g,goal);multi+=m->stats().work();}
        const auto gs=g->stats();
        const size_t fieldBytes=fields.size()*sizeof(Field);
        if(report)std::printf("cached-%s,%s,%d,%u,%d,%zu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%zu,%zu,%zu,%zu,%zu,%.3f,%.3f,%llu,%llu,%zu,%llu,%llu\n",
            seedingName(),f.name.c_str(),repeat,stride,routed.arrived,routed.steps,(unsigned long long)routed.length,(unsigned long long)routed.cost,
            (unsigned long long)d->stats().work(),(unsigned long long)fieldWork,(unsigned long long)gs.work(),
            (unsigned long long)bfsDestWork,(unsigned long long)bfsFieldWork,gs.nodes,gs.entries,gs.bytes,d->bytes(),fieldBytes,
            coldMs,warmMs,(unsigned long long)(multi/goals.size()),(unsigned long long)(bfsMulti/goals.size()),
            bfs.steps,(unsigned long long)bfs.length,(unsigned long long)bfs.cost);
    }
}
// Topology edits: the incremental graph (previous generation as cache) must
// equal a full rebuild bit-for-bit; report how many tile tables were dirtied.
void cachedEdits(bool report,int repeat,unsigned stride) {
    for(const std::string change:{"irrelevant","disconnect","open-shortcut","explore-shortcut","cost-change","large-building"}) {
        Fixture before=fixture(change=="large-building"?"large":"open");
        if(change!="large-building"){before.start={4,18};before.goal={185,18};}
        const int width=before.width;
        if(change=="open-shortcut"||change=="explore-shortcut")for(int z=0;z<170;++z)before.cost[size_t(z)*width+128]=0;
        // Unexplored cells are blocked for this profile until revealed.
        if(change=="explore-shortcut")for(int z=0;z<40;++z)for(int x=120;x<136;++x)before.cost[size_t(z)*width+x]=0;
        Fixture after=before;
        std::vector<bool> dirty(size_t((width+63)/64)*size_t((before.height+63)/64));
        const auto edit=[&](int x,int z,uint16_t cost) {
            after.cost[size_t(z)*width+x]=cost;SnapshotBuilder::dirtyRectangle(dirty,width,before.height,1,1,x,z,1,1);
        };
        if(change=="irrelevant")edit(80,80,0);
        else if(change=="disconnect")for(int z=0;z<192;++z)edit(128,z,0);
        else if(change=="cost-change")edit(129,18,32);
        else if(change=="large-building")for(int z=480;z<496;++z)for(int x=60;x<76;++x)edit(x,z,0);
        else if(change=="explore-shortcut")for(int z=0;z<40;++z)for(int x=120;x<136;++x)edit(x,z,1);
        else for(int z=8;z<28;++z)edit(128,z,1);
        const auto original=topology(before),changed=topology(after,original,dirty);
        const auto old=buildGraph(original,stride);
        const auto incremental=buildGraph(changed,stride,old);
        const auto full=buildGraph(changed,stride);
        check(incremental->hash()==full->hash(),"incremental portal graph differs from full rebuild");
        const auto is=incremental->stats(),fs=full->stats();
        auto oldDest=portalDestination(old,before.goal),newDest=portalDestination(incremental,after.goal);
        std::map<int,Field> a,b;Travel ra,rb;
        ra=travel(*original,before.start,before.goal,[&](int tile){return portalField(oldDest,tile);},a,descending());
        if(change=="disconnect")check(!newDest->reachable(before.start),"portal disconnect retained connectivity");
        else rb=travel(*changed,after.start,after.goal,[&](int tile){return portalField(newDest,tile);},b,descending());
        if(change=="open-shortcut"||change=="explore-shortcut")check(rb.arrived&&rb.cost<ra.cost,"portal graph missed opened shortcut");
        if(report)std::printf("cachededit-%s,%s,%d,%u,%zu,%zu,%zu,%llu,%llu,%llu,%llu,%llu\n",seedingName(),change.c_str(),repeat,stride,
            changed->tiles.size(),is.reusedTiles,is.rebuiltTiles,(unsigned long long)is.work(),(unsigned long long)fs.work(),
            (unsigned long long)newDest->stats().work(),(unsigned long long)ra.cost,(unsigned long long)rb.cost);
    }
}
// Single-unit route quality on the crowdbench geometry (one player, 100%
// moving, 2x2 footprint, uniform terrain): every sampled mover's own start ->
// own goal, no congestion. This isolates global route quality from crowding.
void benchGeometry(const std::string& scenario,int units,int sampleEvery,unsigned stride,bool report) {
    const int spacing=3,rows=int(std::ceil(std::sqrt(double(units)))),columns=(units+rows-1)/rows,laneHeight=rows*spacing+16;
    const auto round64=[](int n){return (n+63)/64*64;};
    const int width=round64(2*columns*spacing+320),height=round64(laneHeight+64);
    const int middle=width/2,left=32,right=width-32-columns*spacing;
    std::vector<uint16_t> cost(size_t(width)*height,1);
    const auto wall=[&](int x,int z,int w,int h){for(int zz=z;zz<z+h;++zz)for(int xx=x;xx<x+w;++xx)cost[size_t(zz)*width+xx]=0;};
    const int low=26,high=24+laneHeight,center=(low+high)/2;
    if(scenario!="open"){wall(0,24,width,2);wall(0,24+laneHeight,width,2);}
    if(scenario=="doors"||scenario=="bridges") {
        const int thickness=scenario=="doors"?4:64;
        wall(middle-thickness/2,low,thickness,center-3-low);wall(middle-thickness/2,center+3,thickness,high-center-3);
    }
    if(scenario=="maze")for(int b=0;b<4;++b)wall(left+columns*spacing+24+b*32,low+(b%2?12:0),4,high-low-12);
    SnapshotBuilder builder(width,height,2,2,[&](int x,int z){return cost[size_t(z)*width+x];});
    while(!builder.done()&&!builder.failed())builder.step(4096);
    check(builder.done(),"bench geometry snapshot failed");
    const auto t=builder.finish();
    uint64_t graphWork=0;
    const auto g=buildGraph(t,stride);graphWork=g->stats().work();
    uint64_t bfsCost=0,portalCost=0,bfsLength=0,portalLength=0,bfsWork=0,portalWork=0,bfsSteps=0,portalSteps=0;
    size_t routes=0,better=0,worse=0,unreached=0;
    for(int k=0;k<units;k+=sampleEvery) {
        const int col=k/rows,row=k%rows,slotX=(columns-1-col)*spacing,slotZ=row*spacing,baseZ=36;
        const Cell start{left+slotX,baseZ+slotZ},goal{right+slotX,baseZ+slotZ};
        if(!t->cost(start)||!t->cost(goal)){++unreached;continue;}
        uint64_t dw=0;auto bd=destination(t,goal,&dw);
        std::map<int,Field> bf;
        const auto b=travel(*t,start,goal,[&](int tile){return field(bd,tile,&dw);},bf);
        auto pd=portalDestination(g,goal);uint64_t pw=pd->stats().work();
        std::map<int,Field> pf;
        const auto p=travel(*t,start,goal,[&](int tile){return portalField(pd,tile,&pw);},pf,descending());
        check(b.arrived&&p.arrived,"bench geometry route failed");
        ++routes;bfsCost+=b.cost;portalCost+=p.cost;bfsLength+=b.length;portalLength+=p.length;
        bfsSteps+=b.steps;portalSteps+=p.steps;bfsWork+=dw;portalWork+=pw;
        better+=p.cost<b.cost;worse+=p.cost>b.cost;
    }
    if(report)std::printf("benchgeom-%s,%s,%d,%u,%d,%d,%zu,%zu,%zu,%zu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
        seedingName(),scenario.c_str(),units,stride,width,height,routes,better,worse,unreached,
        (unsigned long long)bfsCost,(unsigned long long)portalCost,(unsigned long long)bfsLength,(unsigned long long)portalLength,
        (unsigned long long)bfsSteps,(unsigned long long)portalSteps,(unsigned long long)bfsWork,(unsigned long long)portalWork,
        (unsigned long long)graphWork);
}
// Independent full-grid weighted Dijkstra (no diagonal across tile seams).
std::vector<uint32_t> oracle(const Topology& t,const std::vector<Cell>& goals) {
    std::vector<uint32_t> reference(size_t(t.width)*t.height,kUnreachable);
    using Node=std::pair<uint32_t,int>;std::priority_queue<Node,std::vector<Node>,std::greater<Node>> pending;
    for(Cell g:goals)if(t.cost(g)){reference[size_t(g.z)*t.width+g.x]=0;pending.push({0,g.z*t.width+g.x});}
    while(!pending.empty()) {
        const auto [distance,index]=pending.top();pending.pop();if(reference[size_t(index)]!=distance)continue;
        const Cell at{index%t.width,index/t.width};
        for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx) {
            const Cell next{at.x+dx,at.z+dz};if((!dx&&!dz)||!t.cost(next))continue;
            if(dx&&dz&&(!t.cost({next.x,at.z})||!t.cost({at.x,next.z})||t.tileAt(at)!=t.tileAt(next)))continue;
            const auto candidate=distance+uint32_t(t.cost(next))*(dx&&dz?1448:1024);
            const auto n=size_t(next.z)*t.width+next.x;
            if(candidate<reference[n]){reference[n]=candidate;pending.push({candidate,int(n)});}
        }
    }
    return reference;
}
Fixture randomFixture(int width,int height,uint32_t rng,Cell goal) {
    Fixture f{"random",width,height,std::vector<uint16_t>(size_t(width)*height,1),{1,1},goal};
    for(auto& cost:f.cost){rng=rng*1664525u+1013904223u;cost=(rng>>24)<40?0:1+rng%16;}
    f.cost[size_t(goal.z)*width+goal.x]=1;return f;
}
void cachedRegressions() {
    // 1. Weighted oracle: with every crossing a portal (stride 1) the field
    // equals an independent full-grid Dijkstra in every cell; with sparse
    // portals every field value is an upper bound and every route descends.
    for(uint32_t seed:{731u,99u,4242u}) {
        const auto f=randomFixture(150,130,seed,{140,120});const auto t=topology(f);
        const auto reference=oracle(*t,{f.goal});
        for(unsigned stride:{1u,4u,16u,64u}) {
            const auto g=buildGraph(t,stride);auto d=portalDestination(g,f.goal);
            for(int tile=0;tile<int(t->tiles.size());++tile) {
                const auto field=portalField(d,tile);
                for(int z=field.originZ;z<std::min(t->height,field.originZ+64);++z)
                    for(int x=field.originX;x<std::min(t->width,field.originX+64);++x) {
                        const auto got=field.distance[size_t(z%64)*64+x%64],want=reference[size_t(z)*t->width+x];
                        if(gSeeding==portal::Seeding::Bound)check(stride==1?got==want:got>=want,"portal field disagrees with weighted oracle");
                        check((want==kUnreachable)==!d->reachable({x,z}),"portal reachability disagrees with oracle");
                        if(want!=kUnreachable)check(got!=kUnreachable,"portal field lost a reachable cell");
                    }
            }
            for(Cell start:sampleGoals(f,12,seed+stride))if(d->reachable(start)) {
                std::map<int,Field> fields;
                check(travel(*t,start,f.goal,[&](int tile){return portalField(d,tile);},fields,
                    stride==1&&gSeeding==portal::Seeding::Bound?Coherence::Exact:descending()).arrived,"portal route did not arrive");
            }
        }
    }
    // 2. Deterministic ties and scheduling: quanta, goal order and serial vs
    // workers must publish identical bytes and charge identical work.
    {
        auto f=fixture("open");const auto t=topology(f);
        const auto a=buildGraph(t,16,{},1,0),b=buildGraph(t,16,{},4096,4),c=buildGraph(t,16,{},97,4);
        check(a->hash()==b->hash()&&a->hash()==c->hash(),"portal graph depends on quantum or workers");
        check(a->stats().work()==b->stats().work()&&b->stats().work()==c->stats().work(),"portal graph work depends on quantum or workers");
        std::vector<Cell> goals{{170,170},{171,170},{170,171},{20,180}};
        auto d1=portalDestination(a,goals,1);std::reverse(goals.begin(),goals.end());auto d2=portalDestination(c,goals,4096);
        check(d1->hash()==d2->hash()&&d1->stats().work()==d2->stats().work(),"portal destination depends on goal order or quantum");
        for(int tile=0;tile<9;++tile)check(portalField(d1,tile,nullptr,1).hash()==portalField(d2,tile,nullptr,4096).hash(),
            "portal field depends on quantum");
    }
    // 3. Footprints and profile identity: a 3x5 prepared footprint is its own
    // topology; a graph built for another profile is never reused as a cache.
    {
        auto f=fixture("maze");
        SnapshotBuilder shaped(f.width,f.height,3,5,[&](int x,int z){return f.cost[size_t(z)*f.width+x];});
        while(!shaped.done()&&!shaped.failed())shaped.step(257);
        check(shaped.done(),"footprint topology failed");
        const auto st=shaped.finish();const auto plain=topology(f);
        const auto pg=buildGraph(plain,16);const auto sg=buildGraph(st,16,pg);
        check(sg->stats().reusedTiles==0||sg->hash()==buildGraph(st,16)->hash(),"cross-profile cache changed result");
        check(sg->hash()==buildGraph(st,16)->hash(),"cross-profile cache changed result");
        auto d=portalDestination(sg,f.goal);std::map<int,Field> fields;
        check(travel(*st,f.start,f.goal,[&](int tile){return portalField(d,tile);},fields,descending()).arrived,
            "footprint portal route did not arrive");
        const auto reference=oracle(*st,{f.goal});
        for(const auto& [tile,field]:fields)for(int i=0;i<kTileCells;++i) {
            const Cell c{field.originX+i%64,field.originZ+i/64};
            if(gSeeding==portal::Seeding::Bound&&c.x<st->width&&c.z<st->height)check(field.distance[size_t(i)]>=reference[size_t(c.z)*st->width+c.x],
                "footprint field undercut oracle");
        }
    }
    // 4. Goal regions: identical potentials to the explicit contained-cell
    // list; exact against the oracle with all crossings; blocked goals add nothing.
    {
        auto f=fixture("weighted");const auto t=topology(f);const auto g=buildGraph(t,16),all=buildGraph(t,1);
        for(GoalRegion region:std::array<GoalRegion,3>{{
            {GoalRegion::Kind::Circle,170,170,0,0,16,0},
            {GoalRegion::Kind::Rectangle,164,164,176,176,0,0},
            {GoalRegion::Kind::Ring,170,170,0,0,25,9*256}}}) {
            std::vector<Cell> goals;const auto [lo,hi]=region.bounds();
            for(int z=lo.z;z<=hi.z;++z)for(int x=lo.x;x<=hi.x;++x)if(region.contains({x,z}))goals.push_back({x,z});
            auto r=portalDestination(g,region);auto l=portalDestination(g,goals);
            check(r->hash()==l->hash(),"goal region and goal list potentials differ");
            for(int tile=0;tile<9;++tile)check(portalField(r,tile).hash()==portalField(l,tile).hash(),"goal region field differs");
            const auto reference=oracle(*t,goals);const auto exact=portalDestination(all,region);
            for(int tile=0;tile<9&&gSeeding==portal::Seeding::Bound;++tile) {
                const auto field=portalField(exact,tile);
                for(int i=0;i<kTileCells;++i)check(field.distance[size_t(i)]==
                    reference[size_t(field.originZ+i/64)*192+field.originX+i%64],"goal region disagrees with oracle");
            }
        }
        f.cost[size_t(30)*192+30]=0;const auto bt=topology(f);
        auto blocked=portalDestination(buildGraph(bt,16),std::vector<Cell>{{30,30}});
        check(!blocked->reachable(f.start),"blocked goal produced a route");
    }
    // 5. Resource exhaustion is a deterministic failure, never a partial graph.
    {
        auto f=fixture("open");const auto t=topology(f);
        portal::Graph nodes(t,16,{4,1000});while(!nodes.done()&&!nodes.failed())nodes.step(1);
        check(nodes.failed(),"portal node cap ignored");
        portal::Graph entries(t,16,{65536,10});while(!entries.done()&&!entries.failed())entries.step(3);
        check(entries.failed(),"portal table cap ignored");
        portal::Graph again(t,16,{65536,10});while(!again.done()&&!again.failed())again.step(4096,4);
        check(again.failed()&&again.stats().work()==entries.stats().work(),"portal failure point depends on quantum/workers");
    }
    // 6. Topology edits (disconnect, opened/explored shortcuts, building):
    // incremental == full rebuild, checked inside cachedEdits.
    for(unsigned stride:{16u,64u})cachedEdits(false,0,stride);
}
int main(int argc,char** argv) {
    try {
        bool bench=false,cachedOnly=false,geometry=false;int repeats=1;std::vector<unsigned> strides{64,16};std::string selection="both";
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--bench") {bench=true;repeats=5;if(i+1<argc&&argv[i+1][0]!='-')repeats=std::stoi(argv[++i]);}
            else if(arg=="--portal-stride"&&i+1<argc) {
                const int stride=std::stoi(argv[++i]);check(stride>=1&&stride<=64,"invalid portal stride");strides={unsigned(stride)};
            }else if(arg=="--routing"&&i+1<argc)selection=argv[++i];
            else if(arg=="--cached")cachedOnly=true;
            else if(arg=="--geometry")geometry=true;
            else throw std::invalid_argument("usage: flow_routing_compare [--bench [REPEATS]] [--cached] [--geometry] [--routing bfs|portal|both] [--portal-stride 1..64]");
        }
        check(selection=="both"||selection=="bfs"||selection=="portal","invalid comparison routing switch");
        check(repeats>=1&&repeats<=100,"invalid comparison repeats");
        if(bench) {
            std::puts("# routing,scenario,repeat,algorithm,portal_stride,arrived,steps,length_1024,cost_1024,route_and_field_work,graph_work,owned_payload_estimate_bytes,total_ms");
            std::puts("# invalidation,change,repeat,kept_fields,rejected_fields,rebuild_work,avoidable_work,proof_us,rebuild_us");
            std::puts("# cached,scenario,repeat,stride,arrived,steps,length_1024,cost_1024,dest_work,field_work,graph_work,bfs_dest_work,bfs_field_work,graph_nodes,graph_entries,graph_bytes,dest_bytes,field_bytes,cold_ms,warm_ms,dest_work_avg16,bfs_dest_work_avg16,bfs_steps,bfs_length_1024,bfs_cost_1024");
            std::puts("# cachededit,change,repeat,stride,tiles,reused_tables,rebuilt_tables,incremental_graph_work,full_graph_work,dest_work,cost_before,cost_after");
            std::puts("# benchgeom,scenario,units,stride,width,height,routes,portal_better,portal_worse,skipped,bfs_cost,portal_cost,bfs_length,portal_length,bfs_steps,portal_steps,bfs_dest_field_work,portal_dest_field_work,portal_graph_work_once");
        }
        for(int repeat=0;repeat<repeats;++repeat) {
            if(!cachedOnly) {
                for(const std::string name:{"open","weighted","maze","unreachable"})compare(fixture(name),bench,repeat,strides,selection);
                invalidation(bench,repeat);
            }
            if(!bench&&!cachedOnly)continue;
            for(auto seeding:{portal::Seeding::Bound,portal::Seeding::Runs}) {
                gSeeding=seeding;
                for(const std::string name:{"open","weighted","maze","unreachable","large"})cached(fixture(name),bench,repeat,strides);
                for(unsigned stride:strides)cachedEdits(bench,repeat,stride);
                if(bench&&repeat==0&&geometry)for(unsigned stride:strides)for(const std::string s:{"open","doors","bridges","maze"}) {
                    benchGeometry(s,200,1,stride,true);benchGeometry(s,1000,5,stride,true);benchGeometry(s,2000,10,stride,true);
                }
            }
        }
        if(!bench) {
            if(!cachedOnly)regressions();
            for(auto seeding:{portal::Seeding::Bound,portal::Seeding::Runs}){gSeeding=seeding;cachedRegressions();}
            std::puts("PASS bounded weighted portal comparison, decreasing potential, capacity, footprints, goals, deterministic quanta and invalidation proofs; "
                "cached portal graph oracle, ties, footprints, goal regions, edits, exhaustion and workers");
        }
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"flow routing comparison: %s\n",e.what());return 1;}
}
