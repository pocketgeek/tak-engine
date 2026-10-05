#pragma once
// Tools-only evaluation of a CACHED abstract portal graph for the shared-field
// modes. Unlike flow_portal_prototype.h (a fresh search per destination), the
// graph here is destination-independent: it is built once per Topology
// generation and reused by every destination. A Topology already carries the
// movement profile, footprint and exploration identity, so one graph per
// Topology preserves all three. Tiles whose immutable payload and portal cells
// are unchanged reuse their intra-tile cost tables from the previous
// generation, so an edit dirties only the edited tiles and the neighbours whose
// shared border changed. Per destination only the goal tiles are searched,
// followed by a small abstract Dijkstra over the cached graph.
//
// All work is charged in fixed units (one heap pop, scan position, relaxation
// or reuse check); quotas never depend on time. Ties break by (cost, node id)
// in the abstract graph and by (cost, cell index) inside a tile.
#include "sim/flowfield.h"
#include <algorithm>
#include <array>
#include <future>
#include <memory>
#include <stdexcept>
#include <vector>

namespace tak::test::portal {
using sim::flow::Cell;
using sim::flow::Field;
using sim::flow::GoalRegion;
using sim::flow::Tile;
using sim::flow::Topology;
using sim::flow::kTileCells;
using sim::flow::kTileSize;
using sim::flow::kUnreachable;
inline constexpr std::array<int,8> dx{0,1,0,-1,1,1,-1,-1};
inline constexpr std::array<int,8> dz{-1,0,1,0,-1,1,1,-1};
inline constexpr std::array<uint8_t,8> opposite{2,3,0,1,6,7,4,5};
inline uint32_t add(uint32_t a,uint32_t b) {return a>=kUnreachable-b?kUnreachable:a+b;}
inline void fold(uint64_t& h,uint64_t value) {h=(h^value)*1099511628211ull;}
inline int local(Cell c) {return c.z%kTileSize*kTileSize+c.x%kTileSize;}

// Reverse within-tile Dijkstra: distance[c] is the cost of travelling from c
// to the nearest seed, charging the cell each move leaves (x1024 cardinal,
// x1448 diagonal) exactly like sim::flow::FieldBuilder. Diagonals never cut corners.
struct TileSearch {
    const Tile* tile=nullptr;
    std::array<uint32_t,kTileCells> distance;
    std::array<uint8_t,kTileCells> direction;
    std::array<int32_t,kTileCells> position;
    std::array<uint16_t,kTileCells> heap;
    size_t size=0;
    void reset(const Tile& t) {
        tile=&t;size=0;distance.fill(kUnreachable);direction.fill(255);position.fill(-1);
    }
    bool less(uint16_t a,uint16_t b) const {return distance[a]!=distance[b]?distance[a]<distance[b]:a<b;}
    void swap(size_t a,size_t b) {std::swap(heap[a],heap[b]);position[heap[a]]=int32_t(a);position[heap[b]]=int32_t(b);}
    void offer(uint16_t i,uint32_t cost,uint8_t dir) {
        if(!tile->cost[i]||position[i]==-2||cost>=distance[i])return;
        distance[i]=cost;direction[i]=dir;
        if(position[i]<0){position[i]=int32_t(size);heap[size++]=i;}
        for(size_t at=size_t(position[i]);at&&less(heap[at],heap[(at-1)/2]);at=(at-1)/2)swap(at,(at-1)/2);
    }
    // Settles one cell and returns it, or -1 once the frontier is empty.
    int pop() {
        if(!size)return -1;
        const uint16_t cell=heap[0];swap(0,size-1);--size;position[cell]=-2;
        for(size_t at=0;;) {
            const size_t left=at*2+1;if(left>=size)break;
            const size_t right=left+1,best=right<size&&less(heap[right],heap[left])?right:left;
            if(!less(heap[best],heap[at]))break;
            swap(at,best);at=best;
        }
        const int x=cell%kTileSize,z=cell/kTileSize;
        for(int d=0;d<8;++d) {
            const int nx=x+dx[d],nz=z+dz[d];
            if(nx<0||nz<0||nx>=kTileSize||nz>=kTileSize)continue;
            const auto n=uint16_t(nz*kTileSize+nx);
            if(!tile->cost[n]||(d>=4&&(!tile->cost[size_t(z)*kTileSize+nx]||!tile->cost[size_t(nz)*kTileSize+x])))continue;
            offer(n,add(distance[cell],uint32_t(tile->cost[n])*(d<4?1024:1448)),opposite[d]);
        }
        return cell;
    }
};

// Exact intra-tile travel costs between the portal cells of one tile:
// cost[from*k+to]. Destination-independent, immutable once finished.
struct TileTable {
    std::shared_ptr<const Tile> tile;
    std::vector<uint16_t> cells; // local indices, graph order
    std::vector<uint32_t> cost;
    size_t bytes() const {return sizeof(*this)+cells.capacity()*sizeof(uint16_t)+cost.capacity()*sizeof(uint32_t);}
};
// Resumable, self-contained table job: owns its input, never touches a graph,
// so a worker can run it. Each target is one reverse search that stops once
// every portal of the target's own component is settled.
class TableBuilder {
public:
    TableBuilder(std::shared_ptr<const Tile> tile,std::vector<uint16_t> cells)
        :result_(std::make_shared<TileTable>()),search_(std::make_unique<TileSearch>()) {
        result_->tile=std::move(tile);result_->cells=std::move(cells);
        const size_t k=result_->cells.size();result_->cost.assign(k*k,kUnreachable);
    }
    bool done() const {return target_==result_->cells.size();}
    size_t step(size_t budget) {
        size_t work=0;
        const auto& t=*result_->tile;const auto& cells=result_->cells;const size_t k=cells.size();
        while(work<budget&&!done()) {
            ++work;
            if(!active_) {
                search_->reset(t);search_->offer(cells[target_],0,8);active_=true;settled_=0;wanted_=0;
                const auto component=t.component[cells[target_]];
                for(auto c:cells)wanted_+=t.component[c]==component;
                continue;
            }
            const int cell=search_->pop();
            if(cell>=0)for(size_t from=0;from<k;++from)if(cells[from]==cell){
                result_->cost[from*k+target_]=search_->distance[size_t(cell)];++settled_;
            }
            if(cell<0||settled_==wanted_){active_=false;++target_;}
        }
        work_+=work;return work;
    }
    std::shared_ptr<const TileTable> finish() const {return done()?result_:nullptr;}
    uint64_t work() const {return work_;}
private:
    std::shared_ptr<TileTable> result_;
    std::unique_ptr<TileSearch> search_;
    size_t target_=0,settled_=0,wanted_=0;
    uint64_t work_=0;
    bool active_=false;
};

struct GraphLimits {size_t nodes=65536,entries=size_t(4)<<20;}; // entries: sum of k*k table cells
class Graph {
public:
    struct Node {Cell cell;uint32_t peer;uint8_t direction;uint16_t slot;int32_t tile;};
    using Limits=GraphLimits;
    struct Stats {
        uint64_t scanWork=0,tableWork=0,reuseWork=0;
        size_t nodes=0,entries=0,reusedTiles=0,rebuiltTiles=0,bytes=0;
        uint64_t work() const {return scanWork+tableWork+reuseWork;}
    };
    static constexpr size_t lanes=4;
    Graph(std::shared_ptr<const Topology> topology,unsigned stride,Limits limits={},std::shared_ptr<const Graph> previous={})
        :topology_(std::move(topology)),previous_(std::move(previous)),stride_(stride),limits_(limits) {
        if(!topology_||stride_<1||stride_>64||!limits_.nodes||!limits_.entries)throw std::invalid_argument("portal graph limits");
        tileNodes_.resize(topology_->tiles.size());tables_.resize(topology_->tiles.size());
        // A previous generation is only a cache; anything structural differing
        // simply disables reuse rather than mixing incompatible tables.
        if(previous_&&(previous_->stride_!=stride_||previous_->topology_->width!=topology_->width||
            previous_->topology_->height!=topology_->height||!previous_->done()))previous_.reset();
    }
    bool done() const {return phase_==Phase::Done;}
    bool failed() const {return phase_==Phase::Failed;}
    // workers=0 runs inline. The lane schedule and quanta are identical for any
    // worker count, so published tables and charged work cannot depend on it.
    size_t step(size_t budget,unsigned workers=0) {
        size_t work=0;
        while(work<budget&&!done()&&!failed()) {
            if(phase_==Phase::Tables){work+=tables(budget-work,workers);continue;}
            ++work;
            if(phase_==Phase::Scan){++stats_.scanWork;scan();}
            else {++stats_.reuseWork;assign();}
        }
        return work;
    }
    const Topology& topology() const {return *topology_;}
    std::shared_ptr<const Topology> topologyPtr() const {return topology_;}
    const std::vector<Node>& nodes() const {return nodes_;}
    const std::vector<uint32_t>& tileNodes(int tile) const {return tileNodes_[size_t(tile)];}
    const TileTable& table(int tile) const {return *tables_[size_t(tile)];}
    Stats stats() const {
        auto s=stats_;s.nodes=nodes_.size();s.entries=entries_;
        s.bytes=sizeof(*this)+nodes_.capacity()*sizeof(Node)+tileNodes_.capacity()*sizeof(tileNodes_[0])+
            tables_.capacity()*sizeof(tables_[0]);
        for(const auto& v:tileNodes_)s.bytes+=v.capacity()*sizeof(uint32_t);
        for(const auto& t:tables_)if(t)s.bytes+=t->bytes(); // shared tables counted in each generation
        return s;
    }
    uint64_t hash() const {
        uint64_t h=1469598103934665603ull;
        for(const auto& n:nodes_){fold(h,uint64_t(n.cell.x));fold(h,uint64_t(n.cell.z));fold(h,n.peer);fold(h,n.direction);}
        for(const auto& t:tables_)if(t)for(auto c:t->cost)fold(h,c);
        return h;
    }
private:
    enum class Phase {Scan,Assign,Tables,Done,Failed};
    Phase phase_=Phase::Scan;
    std::shared_ptr<const Topology> topology_;
    std::shared_ptr<const Graph> previous_;
    unsigned stride_;
    Limits limits_;
    std::vector<Node> nodes_;
    std::vector<std::vector<uint32_t>> tileNodes_;
    std::vector<std::shared_ptr<const TileTable>> tables_;
    std::vector<size_t> pending_;
    std::array<std::unique_ptr<TableBuilder>,lanes> active_{};
    std::array<size_t,lanes> activeTile_{};
    Stats stats_;
    size_t cursor_=0,entries_=0,next_=0;
    int run_=0;
    Cell lastA_{-1,-1},lastB_{-1,-1},selected_{-1,-1};
    uint8_t lastDirection_=0;
    void portal(Cell a,Cell b,uint8_t d) {
        if(nodes_.size()+2>limits_.nodes){phase_=Phase::Failed;return;}
        const auto id=uint32_t(nodes_.size());
        const int ta=topology_->tileAt(a),tb=topology_->tileAt(b);
        nodes_.push_back({a,id+1,d,uint16_t(tileNodes_[size_t(ta)].size()),ta});tileNodes_[size_t(ta)].push_back(id);
        nodes_.push_back({b,id,opposite[d],uint16_t(tileNodes_[size_t(tb)].size()),tb});tileNodes_[size_t(tb)].push_back(id+1);
        selected_=a;
    }
    void endRun() {
        if(run_&&lastA_!=selected_)portal(lastA_,lastB_,lastDirection_);
        run_=0;selected_={-1,-1};
    }
    // One unit per east/south border position. Every maximal run of passable
    // crossing pairs gets a portal at its start, every stride, and its end.
    void scan() {
        const auto& t=*topology_;
        if(cursor_==t.tiles.size()*128) {endRun();if(!failed()){cursor_=0;phase_=Phase::Assign;}return;}
        const size_t tile=cursor_/128,border=cursor_%128;++cursor_;
        if(border%64==0){endRun();if(failed())return;}
        const int tx=int(tile)%t.tilesX,tz=int(tile)/t.tilesX,offset=int(border%64);
        const Cell a=border<64?Cell{(tx+1)*64-1,tz*64+offset}:Cell{tx*64+offset,(tz+1)*64-1};
        const Cell b=border<64?Cell{a.x+1,a.z}:Cell{a.x,a.z+1};
        if(!t.cost(a)||!t.cost(b)){endRun();return;}
        const auto direction=uint8_t(border<64?1:2);
        if(run_%int(stride_)==0)portal(a,b,direction);
        ++run_;lastA_=a;lastB_=b;lastDirection_=direction;
    }
    std::vector<uint16_t> cells(size_t tile) const {
        std::vector<uint16_t> out;out.reserve(tileNodes_[tile].size());
        for(auto id:tileNodes_[tile])out.push_back(uint16_t(local(nodes_[id].cell)));
        return out;
    }
    // One unit per tile: reuse the previous generation's table when the tile
    // payload is the same immutable object and its portal cells are identical.
    void assign() {
        if(cursor_==tileNodes_.size()){phase_=Phase::Tables;return;}
        const size_t tile=cursor_++;const size_t k=tileNodes_[tile].size();
        if(entries_+k*k>limits_.entries){phase_=Phase::Failed;return;}
        entries_+=k*k;
        if(!k){auto empty=std::make_shared<TileTable>();empty->tile=topology_->tiles[tile];tables_[tile]=std::move(empty);return;}
        if(previous_) {
            const auto& old=previous_->tables_[tile];
            if(old&&old->tile==topology_->tiles[tile]&&old->cells==cells(tile)) {
                tables_[tile]=old;++stats_.reusedTiles;return;
            }
        }
        pending_.push_back(tile);
    }
    size_t tables(size_t budget,unsigned workers) {
        for(size_t lane=0;lane<lanes;++lane)if(!active_[lane]&&next_<pending_.size()) {
            const auto tile=pending_[next_++];activeTile_[lane]=tile;
            active_[lane]=std::make_unique<TableBuilder>(topology_->tiles[tile],cells(tile));
        }
        size_t count=0;for(const auto& a:active_)count+=bool(a);
        if(!count){phase_=Phase::Done;return 0;}
        // Deterministic split: equal shares, remainder to the lowest lanes.
        std::array<size_t,lanes> quantum{},used{};
        for(size_t lane=0,seen=0;lane<lanes;++lane)if(active_[lane]){quantum[lane]=budget/count+(seen<budget%count);++seen;}
        std::array<std::future<size_t>,lanes> futures;
        for(size_t lane=0;lane<lanes;++lane)if(active_[lane]&&quantum[lane]) {
            auto* job=active_[lane].get();const size_t q=quantum[lane];
            if(workers)futures[lane]=std::async(std::launch::async,[job,q]{return job->step(q);});
            else used[lane]=job->step(q);
        }
        size_t work=0;
        for(size_t lane=0;lane<lanes;++lane)if(active_[lane]) {
            if(workers&&futures[lane].valid())used[lane]=futures[lane].get();
            work+=used[lane];
            if(active_[lane]->done()) {
                tables_[activeTile_[lane]]=active_[lane]->finish();active_[lane].reset();++stats_.rebuiltTiles;
            }
        }
        stats_.tableWork+=work;
        return work;
    }
};

// Per-destination potential over a cached graph. Preserves goal identity
// (exact cell list or controller GoalRegion); goals in blocked cells add nothing.
class Destination {
public:
    struct Stats {uint64_t seedWork=0,goalWork=0,solveWork=0,reachWork=0;uint64_t work() const {return seedWork+goalWork+solveWork+reachWork;}};
    Destination(std::shared_ptr<const Graph> graph,std::vector<Cell> goals):graph_(std::move(graph)),goals_(std::move(goals)) {
        if(!graph_||!graph_->done())throw std::invalid_argument("portal destination needs a finished graph");
        if(goals_.size()>4096)throw std::length_error("portal destination goal cap");
        std::sort(goals_.begin(),goals_.end(),[](Cell a,Cell b){return a.z!=b.z?a.z<b.z:a.x<b.x;});
        goals_.erase(std::unique(goals_.begin(),goals_.end()),goals_.end());
        seeds_=goals_.size();init();
    }
    Destination(std::shared_ptr<const Graph> graph,GoalRegion region):graph_(std::move(graph)),region_(region) {
        if(!graph_||!graph_->done())throw std::invalid_argument("portal destination needs a finished graph");
        const auto [low,high]=region.bounds();const auto& t=graph_->topology();
        seedMin_={std::max(0,low.x),std::max(0,low.z)};seedMax_={std::min(t.width-1,high.x),std::min(t.height-1,high.z)};
        seeds_=seedMin_.x<=seedMax_.x&&seedMin_.z<=seedMax_.z?size_t(seedMax_.x-seedMin_.x+1)*size_t(seedMax_.z-seedMin_.z+1):0;
        init();
    }
    bool done() const {return phase_==Phase::Done;}
    size_t step(size_t budget) {
        size_t work=0;
        while(work<budget&&!done()) {
            ++work;
            switch(phase_) {
                case Phase::Seeds:++stats_.seedWork;seed();break;
                case Phase::Goals:++stats_.goalWork;goal();break;
                case Phase::Solve:++stats_.solveWork;solve();break;
                case Phase::Reach:++stats_.reachWork;reach();break;
                default:break;
            }
        }
        return work;
    }
    const Graph& graph() const {return *graph_;}
    uint32_t distance(uint32_t node) const {return distance_[node];}
    bool reachable(Cell c) const {
        const auto component=graph_->topology().componentAt(c);
        return done()&&component!=kUnreachable&&potential_[component]!=kUnreachable;
    }
    // Component potential: 0 when it holds a goal, else its cheapest portal.
    uint32_t potential(uint32_t component) const {return component<potential_.size()?potential_[component]:kUnreachable;}
    // Geometric goal-box estimate, identical in form to sim::flow::Destination.
    uint32_t estimate(Cell c) const {
        const int64_t ex=std::max({int64_t(goalMin_.x)-c.x,int64_t(0),int64_t(c.x)-goalMax_.x});
        const int64_t ez=std::max({int64_t(goalMin_.z)-c.z,int64_t(0),int64_t(c.z)-goalMax_.z});
        return uint32_t(std::min<int64_t>(kUnreachable-1,std::max(ex,ez)*1024+std::min(ex,ez)*424));
    }
    const std::vector<uint16_t>& goalCells(int tile) const {
        static const std::vector<uint16_t> none;
        const auto it=std::lower_bound(goalTiles_.begin(),goalTiles_.end(),tile,[](const auto& e,int t){return e.first<t;});
        return it!=goalTiles_.end()&&it->first==tile?it->second:none;
    }
    Stats stats() const {return stats_;}
    size_t bytes() const {
        size_t b=sizeof(*this)+goals_.capacity()*sizeof(Cell)+(distance_.capacity()+heap_.capacity())*sizeof(uint32_t)+
            positions_.capacity()*sizeof(int32_t)+potential_.capacity()*sizeof(uint32_t)+goalTiles_.capacity()*sizeof(goalTiles_[0]);
        for(const auto& [tile,cells]:goalTiles_){(void)tile;b+=cells.capacity()*sizeof(uint16_t);}
        return b+(search_?sizeof(TileSearch):0);
    }
    uint64_t hash() const {uint64_t h=1469598103934665603ull;for(auto d:distance_)fold(h,d);return h;}
private:
    enum class Phase {Seeds,Goals,Solve,Reach,Done};
    Phase phase_=Phase::Seeds;
    std::shared_ptr<const Graph> graph_;
    std::vector<Cell> goals_;
    std::optional<GoalRegion> region_;
    Cell seedMin_{},seedMax_{};
    size_t seeds_=0,cursor_=0,goalTile_=0,emit_=0,relax_=0;
    std::vector<std::pair<int,std::vector<uint16_t>>> goalTiles_; // sorted by tile
    std::unique_ptr<TileSearch> search_;
    std::vector<uint32_t> distance_,heap_;
    std::vector<int32_t> positions_;
    std::vector<uint32_t> potential_;
    Cell goalMin_{},goalMax_{};
    uint32_t solving_=kUnreachable;
    Stats stats_;
    void init() {
        const auto n=graph_->nodes().size();
        distance_.assign(n,kUnreachable);positions_.assign(n,-1);potential_.assign(graph_->topology().components,kUnreachable);
        if(region_){const auto [low,high]=region_->bounds();goalMin_=low;goalMax_=high;}
        else if(!goals_.empty()) {
            goalMin_=goalMax_=goals_.front();
            for(Cell c:goals_){goalMin_={std::min(goalMin_.x,c.x),std::min(goalMin_.z,c.z)};goalMax_={std::max(goalMax_.x,c.x),std::max(goalMax_.z,c.z)};}
        }
    }
    void seed() {
        if(cursor_==seeds_){cursor_=0;phase_=Phase::Goals;return;}
        Cell c;
        if(region_) {
            const size_t width=size_t(seedMax_.x-seedMin_.x+1);
            c={seedMin_.x+int(cursor_%width),seedMin_.z+int(cursor_/width)};
            ++cursor_;if(!region_->contains(c))return;
        }else c=goals_[cursor_++];
        const auto& t=graph_->topology();
        if(!t.cost(c))return;
        const int tile=t.tileAt(c);
        auto it=std::lower_bound(goalTiles_.begin(),goalTiles_.end(),tile,[](const auto& e,int v){return e.first<v;});
        if(it==goalTiles_.end()||it->first!=tile)it=goalTiles_.insert(it,{tile,{}});
        it->second.push_back(uint16_t(local(c)));
        potential_[t.componentAt(c)]=0;
    }
    // One goal tile at a time: a reverse search seeded by every goal cell,
    // then one unit per portal of that tile to read its goal cost.
    void goal() {
        if(goalTile_==goalTiles_.size()){search_.reset();phase_=Phase::Solve;return;}
        const auto& [tile,cells]=goalTiles_[goalTile_];
        if(!search_) {
            search_=std::make_unique<TileSearch>();search_->reset(*graph_->topology().tiles[size_t(tile)]);
            for(auto c:cells)search_->offer(c,0,8);
            emit_=0;return;
        }
        if(search_->pop()>=0)return;
        const auto& ids=graph_->tileNodes(tile);
        if(emit_<ids.size()) {
            const auto id=ids[emit_++];offer(id,search_->distance[size_t(local(graph_->nodes()[id].cell))]);return;
        }
        search_.reset();++goalTile_;
    }
    bool less(uint32_t a,uint32_t b) const {return distance_[a]!=distance_[b]?distance_[a]<distance_[b]:a<b;}
    void swap(size_t a,size_t b) {std::swap(heap_[a],heap_[b]);positions_[heap_[a]]=int32_t(a);positions_[heap_[b]]=int32_t(b);}
    void offer(uint32_t id,uint32_t cost) {
        if(positions_[id]==-2||cost>=distance_[id])return;
        distance_[id]=cost;
        if(positions_[id]<0){positions_[id]=int32_t(heap_.size());heap_.push_back(id);}
        for(auto at=size_t(positions_[id]);at&&less(heap_[at],heap_[(at-1)/2]);at=(at-1)/2)swap(at,(at-1)/2);
    }
    // Reverse relaxation: one unit per pop, per same-tile predecessor and for
    // the crossing peer. Every move pays the cost of the cell it LEAVES.
    void solve() {
        const auto& nodes=graph_->nodes();
        if(solving_!=kUnreachable) {
            const auto& node=nodes[solving_];
            const auto& ids=graph_->tileNodes(node.tile);
            if(relax_<ids.size()) {
                const auto& table=graph_->table(node.tile);const size_t k=ids.size();
                const auto from=relax_++;
                if(from!=node.slot)offer(ids[from],add(distance_[solving_],table.cost[from*k+node.slot]));
                return;
            }
            // Crossing peer -> settled node pays to LEAVE the peer cell, as within tiles.
            offer(node.peer,add(distance_[solving_],uint32_t(graph_->topology().cost(nodes[node.peer].cell))*1024));
            solving_=kUnreachable;return;
        }
        if(heap_.empty()){cursor_=0;phase_=Phase::Reach;return;}
        solving_=heap_[0];relax_=0;swap(0,heap_.size()-1);heap_.pop_back();positions_[solving_]=-2;
        for(size_t at=0;;) {
            const size_t left=at*2+1;if(left>=heap_.size())break;
            const size_t right=left+1,best=right<heap_.size()&&less(heap_[right],heap_[left])?right:left;
            if(!less(heap_[best],heap_[at]))break;
            swap(at,best);at=best;
        }
    }
    // Every crossing pair lies on a run whose portals join the same two
    // components, so component reachability through portals is exact.
    void reach() {
        const auto& nodes=graph_->nodes();
        if(cursor_==nodes.size()){heap_.clear();heap_.shrink_to_fit();positions_.clear();positions_.shrink_to_fit();phase_=Phase::Done;return;}
        const auto id=cursor_++;
        auto& p=potential_[graph_->topology().componentAt(nodes[id].cell)];p=std::min(p,distance_[id]);
    }
};

// One detailed tile from a portal destination. Two seeding rules:
//
// Bound (coherent): goal cells (0) and every exit cell whose outside neighbour
// has an UPPER BOUND on its potential: the best portal on that border run plus
// an explicit walk along the neighbour's border row. The bound keeps a broad
// front (all exits, not just portal cells) while guaranteeing next-tile
// potential <= this-tile potential - step cost, so ONE routing potential
// strictly decreases along every route (no cycles).
//
// Runs (weighted generalisation of production's component-hop seeding): every
// exit on a border run whose run potential R (cheapest far-side portal on the
// run) is strictly below the inside component's potential P, valued
// R + step + production's goal-box bias. A run's far-side component has
// P <= R, so P strictly decreases at every tile crossing (acyclic, and every
// reachable component has a downhill run); the per-tile field descends.
enum class Seeding {Bound,Runs};
class FieldBuilder {
public:
    FieldBuilder(std::shared_ptr<const Destination> destination,int tile,Seeding seeding=Seeding::Bound)
        :destination_(std::move(destination)),search_(std::make_unique<TileSearch>()),seeding_(seeding) {
        if(!destination_||!destination_->done())throw std::invalid_argument("portal field destination not ready");
        const auto& t=destination_->graph().topology();
        result_.tile=tile;result_.originX=tile%t.tilesX*kTileSize;result_.originZ=tile/t.tilesX*kTileSize;
        search_->reset(*t.tiles[size_t(tile)]);
        const int x=result_.originX,z=result_.originZ;
        biasBase_=std::min({destination_->estimate({x,z}),destination_->estimate({x+63,z}),
            destination_->estimate({x,z+63}),destination_->estimate({x+63,z+63})});
        biasBase_=biasBase_>128*1448?biasBase_-128*1448:0;
    }
    bool done() const {return phase_==Phase::Done;}
    size_t step(size_t budget) {
        size_t work=0;
        while(work<budget&&!done()) {
            ++work;++work_;
            if(phase_==Phase::Goals) {
                const auto& goals=destination_->goalCells(result_.tile);
                if(cursor_<goals.size())search_->offer(goals[cursor_++],0,8);else{cursor_=0;phase_=Phase::Portals;}
            }else if(phase_==Phase::Portals)portals();
            else if(phase_==Phase::Front)front();
            else if(search_->pop()<0)finish();
        }
        return work;
    }
    const Field& field() const {return result_;}
    uint64_t work() const {return work_;}
private:
    enum class Phase {Goals,Portals,Front,Integrate,Done};
    Phase phase_=Phase::Goals;
    std::shared_ptr<const Destination> destination_;
    std::unique_ptr<TileSearch> search_;
    Seeding seeding_;
    Field result_;
    size_t cursor_=0;
    uint64_t work_=0;
    uint32_t biasBase_=0;
    std::array<uint32_t,kTileSize> best_{};
    std::array<std::array<uint32_t,kTileSize>,4> portal_{}; // side, offset -> outside node id
    // One unit per portal of this tile: index its far-side node by border slot.
    void portals() {
        const auto& g=destination_->graph();const auto& ids=g.tileNodes(result_.tile);
        if(cursor_==0)for(auto& side:portal_)side.fill(kUnreachable);
        if(cursor_==ids.size()){cursor_=0;phase_=Phase::Front;return;}
        const auto& node=g.nodes()[ids[cursor_++]];
        const int d=node.direction,offset=d==0||d==2?node.cell.x-result_.originX:node.cell.z-result_.originZ;
        portal_[size_t(d)][size_t(offset)]=node.peer;
    }
    // Units per side: 64 for the forward sweep, 64 backward + seed.
    void front() {
        if(cursor_==4*2*kTileSize){phase_=Phase::Integrate;return;}
        const int d=int(cursor_/(2*kTileSize)),pass=int(cursor_%(2*kTileSize))/kTileSize,i0=int(cursor_%kTileSize);
        ++cursor_;
        const auto& g=destination_->graph();const auto& t=g.topology();
        const int i=pass?kTileSize-1-i0:i0;
        const auto inside=[&](int at){
            return d==0?Cell{result_.originX+at,result_.originZ}:d==1?Cell{result_.originX+63,result_.originZ+at}:
                d==2?Cell{result_.originX+at,result_.originZ+63}:Cell{result_.originX,result_.originZ+at};
        };
        const auto outside=[&](int at){const Cell c=inside(at);return Cell{c.x+dx[d],c.z+dz[d]};};
        const auto open=[&](int at){return at>=0&&at<kTileSize&&t.cost(inside(at))&&t.cost(outside(at));};
        // Bound walks the far border row; Runs takes the run minimum.
        const bool walk=seeding_==Seeding::Bound;
        if(!pass) {
            // Portal potential on the far side of this crossing, if any.
            uint32_t value=kUnreachable;
            if(open(i)&&portal_[size_t(d)][size_t(i)]!=kUnreachable)value=destination_->distance(portal_[size_t(d)][size_t(i)]);
            if(open(i)&&open(i-1))value=std::min(value,add(best_[size_t(i-1)],walk?uint32_t(t.cost(outside(i)))*1024:0));
            best_[size_t(i)]=open(i)?value:kUnreachable;
            return;
        }
        if(!open(i))return;
        if(open(i+1))best_[size_t(i)]=std::min(best_[size_t(i)],add(best_[size_t(i+1)],walk?uint32_t(t.cost(outside(i)))*1024:0));
        const auto step=uint32_t(t.cost(inside(i)))*1024;
        if(walk) {search_->offer(uint16_t(local(inside(i))),add(best_[size_t(i)],step),uint8_t(d));return;}
        if(best_[size_t(i)]>=destination_->potential(t.componentAt(inside(i))))return;
        const auto estimate=destination_->estimate(outside(i));
        const auto bias=(estimate>biasBase_?estimate-biasBase_:0)*uint32_t(t.tiles[size_t(result_.tile)]->minimumCost);
        search_->offer(uint16_t(local(inside(i))),add(add(best_[size_t(i)],step),bias),uint8_t(d));
    }
    void finish() {
        result_.distance=search_->distance;result_.direction=search_->direction;
        search_.reset();phase_=Phase::Done;
    }
};
} // namespace tak::test::portal
