#pragma once
// Tools-only comparison prototype. No gameplay mode, protocol, or save option.
// Crossing samples are explicit portals; each edge inside a tile is an exact
// integer shortest path. The graph and integration both charge fixed work units.
#include "sim/flowfield.h"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <vector>

namespace tak::test {
class PortalRoute {
    using Cell=sim::flow::Cell;
    using Topology=sim::flow::Topology;
    using Field=sim::flow::Field;
    static constexpr uint32_t infinity=sim::flow::kUnreachable;
    static constexpr std::array<int,8> dx{0,1,0,-1,1,1,-1,-1};
    static constexpr std::array<int,8> dz{-1,0,1,0,-1,1,1,-1};
    static constexpr std::array<uint8_t,8> opposite{2,3,0,1,6,7,4,5};
    static uint32_t add(uint32_t a,uint32_t b) {return a>=infinity-b?infinity:a+b;}
    static int local(Cell c) {return c.z%64*64+c.x%64;}
    struct Node {Cell cell;uint32_t peer;uint8_t direction;};
    struct Edge {uint32_t from,cost;}; // Reverse adjacency, indexed by destination.
    struct Local {
        const Topology& topology;
        Field result;
        std::array<int,4096> position;
        std::array<uint16_t,4096> heap;
        size_t size=0,work=0;
        Local(const Topology& t,int tile):topology(t) {
            result.tile=tile;result.originX=tile%t.tilesX*64;result.originZ=tile/t.tilesX*64;
            result.distance.fill(infinity);result.direction.fill(255);position.fill(-1);
        }
        bool less(uint16_t a,uint16_t b)const {
            return result.distance[a]!=result.distance[b]?result.distance[a]<result.distance[b]:a<b;
        }
        void swap(size_t a,size_t b) {
            std::swap(heap[a],heap[b]);position[heap[a]]=int(a);position[heap[b]]=int(b);
        }
        void offer(Cell c,uint32_t cost,uint8_t direction) {
            const int i=local(c);
            if(topology.tileAt(c)!=result.tile||!topology.cost(c)||position[i]==-2||cost>=result.distance[i])return;
            result.distance[i]=cost;result.direction[i]=direction;
            if(position[i]<0){position[i]=int(size);heap[size++]=uint16_t(i);}
            size_t at=size_t(position[i]);
            while(at&&less(heap[at],heap[(at-1)/2])){swap(at,(at-1)/2);at=(at-1)/2;}
        }
        bool step() {
            if(!size)return false;
            ++work;
            const auto cell=heap[0];swap(0,size-1);--size;position[cell]=-2;
            for(size_t at=0;;) {
                const size_t left=2*at+1;if(left>=size)break;
                const size_t right=left+1,best=right<size&&less(heap[right],heap[left])?right:left;
                if(!less(heap[best],heap[at]))break;
                swap(at,best);at=best;
            }
            const int x=cell%64,z=cell/64;
            const auto& tile=*topology.tiles[size_t(result.tile)];
            for(int d=0;d<8;++d) {
                const int nx=x+dx[d],nz=z+dz[d];
                if(nx<0||nz<0||nx>=64||nz>=64||!tile.cost[size_t(nz)*64+nx])continue;
                if(d>=4&&(!tile.cost[size_t(z)*64+nx]||!tile.cost[size_t(nz)*64+x]))continue;
                offer({result.originX+nx,result.originZ+nz},
                    add(result.distance[cell],uint32_t(tile.cost[size_t(nz)*64+nx])*(d<4?1024:1448)),opposite[d]);
            }
            return true;
        }
    };
public:
    struct Limits {size_t nodes=8192,edges=262144;};
    struct Stats {uint64_t graphWork=0,routeWork=0,fieldWork=0;size_t nodes=0,edges=0,bytes=0;};
    PortalRoute(std::shared_ptr<const Topology> topology,std::vector<Cell> goals,unsigned stride,Limits limits)
        :topology_(std::move(topology)),goals_(std::move(goals)),stride_(stride),limits_(limits) {
        if(!topology_||stride_<1||stride_>64||!limits_.nodes||!limits_.edges)throw std::invalid_argument("portal prototype limits");
        if(goals_.size()>4096)throw std::length_error("portal prototype goal cap");
        std::sort(goals_.begin(),goals_.end(),[](Cell a,Cell b){return a.z!=b.z?a.z<b.z:a.x<b.x;});
        goals_.erase(std::unique(goals_.begin(),goals_.end()),goals_.end());
        tileNodes_.resize(topology_->tiles.size());tileGoals_.resize(topology_->tiles.size());
        for(Cell c:goals_)if(topology_->cost(c))tileGoals_[size_t(topology_->tileAt(c))].push_back(c);
    }
    bool done()const {return phase_==Phase::Done;}
    bool failed()const {return phase_==Phase::Failed;}
    size_t step(size_t budget) {
        size_t work=0;
        while(work<budget&&!done()&&!failed()) {
            ++work;
            if(phase_==Phase::Solve)++stats_.routeWork;else ++stats_.graphWork;
            switch(phase_) {
                case Phase::Scan:scan();break;
                case Phase::Cross:cross();break;
                case Phase::Costs:costs();break;
                case Phase::Solve:solve();break;
                default:break;
            }
        }
        return work;
    }
    Field field(int tile) {
        if(!done()||tile<0||size_t(tile)>=tileNodes_.size())throw std::logic_error("portal prototype not ready");
        Local local(*topology_,tile);
        for(Cell c:tileGoals_[size_t(tile)])local.offer(c,0,8);
        for(auto id:tileNodes_[size_t(tile)]) {
            const auto& node=nodes_[id];
            const auto crossed=add(distance_[node.peer],uint32_t(topology_->cost(node.cell))*1024);
            // Only seed crossings that attain the shortest graph potential.
            // Every internal/cross-tile movement then strictly decreases it.
            if(distance_[id]!=infinity&&crossed==distance_[id])local.offer(node.cell,crossed,node.direction);
        }
        while(local.step()){}
        stats_.fieldWork+=local.work;
        return local.result;
    }
    Stats stats()const {
        auto s=stats_;s.nodes=nodes_.size();s.edges=edgeCount_;
        s.bytes=sizeof(*this)+nodes_.capacity()*sizeof(Node)+goals_.capacity()*sizeof(Cell)+
            distance_.capacity()*sizeof(uint32_t)+heap_.capacity()*sizeof(uint32_t)+positions_.capacity()*sizeof(int)+
            reverse_.capacity()*sizeof(std::vector<Edge>)+tileNodes_.capacity()*sizeof(std::vector<uint32_t>)+
            tileGoals_.capacity()*sizeof(std::vector<Cell>)+sizeof(Local);
        for(const auto& v:reverse_)s.bytes+=v.capacity()*sizeof(Edge);
        for(const auto& v:tileNodes_)s.bytes+=v.capacity()*sizeof(uint32_t);
        for(const auto& v:tileGoals_)s.bytes+=v.capacity()*sizeof(Cell);
        return s;
    }
    const Topology& topology()const {return *topology_;}
private:
    enum class Phase {Scan,Cross,Costs,Solve,Done,Failed};
    Phase phase_=Phase::Scan;
    std::shared_ptr<const Topology> topology_;
    std::vector<Cell> goals_;
    unsigned stride_;
    Limits limits_;
    std::vector<Node> nodes_;
    std::vector<std::vector<uint32_t>> tileNodes_;
    std::vector<std::vector<Cell>> tileGoals_;
    std::vector<std::vector<Edge>> reverse_;
    std::vector<uint32_t> distance_,heap_;
    std::vector<int> positions_;
    std::unique_ptr<Local> local_;
    Stats stats_;
    size_t cursor_=0,edgeCount_=0,tile_=0,target_=0,emitted_=0,solveEdge_=0;
    int run_=0;
    Cell lastA_{-1,-1},lastB_{-1,-1},selected_{-1,-1};
    uint8_t lastDirection_=0;
    uint32_t solving_=infinity;
    bool targetGoal_=false;
    void portal(Cell a,Cell b,uint8_t d) {
        if(nodes_.size()+2>limits_.nodes){phase_=Phase::Failed;return;}
        const auto id=uint32_t(nodes_.size());
        nodes_.push_back({a,id+1,d});nodes_.push_back({b,id,opposite[d]});
        tileNodes_[size_t(topology_->tileAt(a))].push_back(id);
        tileNodes_[size_t(topology_->tileAt(b))].push_back(id+1);
        selected_=a;
    }
    void endRun() {
        if(run_&&lastA_!=selected_)portal(lastA_,lastB_,lastDirection_);
        run_=0;selected_={-1,-1};
    }
    void scan() {
        const auto& t=*topology_;
        if(cursor_==t.tiles.size()*128) {
            endRun();if(failed())return;
            reverse_.resize(nodes_.size()+1);cursor_=0;phase_=Phase::Cross;return;
        }
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
    void edge(uint32_t from,uint32_t to,uint32_t cost) {
        if(cost==infinity)return;
        if(edgeCount_==limits_.edges){phase_=Phase::Failed;return;}
        reverse_[to].push_back({from,cost});++edgeCount_;
    }
    void cross() {
        if(cursor_<nodes_.size()) {
            const auto id=uint32_t(cursor_++);const auto& n=nodes_[id];
            edge(id,n.peer,uint32_t(topology_->cost(n.cell))*1024);return;
        }
        cursor_=0;phase_=Phase::Costs;
    }
    void costs() {
        if(tile_==tileNodes_.size()) {
            const auto count=nodes_.size()+1;
            distance_.assign(count,infinity);positions_.assign(count,-1);heap_.reserve(count);
            offer(uint32_t(nodes_.size()),0);phase_=Phase::Solve;return;
        }
        const auto& nodes=tileNodes_[tile_];
        if(!local_) {
            if(target_>=nodes.size()+(tileGoals_[tile_].empty()?0:1)){++tile_;target_=0;return;}
            local_=std::make_unique<Local>(*topology_,int(tile_));emitted_=0;
            targetGoal_=target_==nodes.size();
            if(targetGoal_)for(Cell c:tileGoals_[tile_])local_->offer(c,0,8);
            else local_->offer(nodes_[nodes[target_]].cell,0,8);
            return;
        }
        if(local_->step())return;
        if(emitted_<nodes.size()) {
            const auto from=nodes[emitted_++];
            const auto to=targetGoal_?uint32_t(nodes_.size()):nodes[target_];
            if(from!=to)edge(from,to,local_->result.distance[size_t(local(nodes_[from].cell))]);
            return;
        }
        local_.reset();++target_;
    }
    bool less(uint32_t a,uint32_t b)const {return distance_[a]!=distance_[b]?distance_[a]<distance_[b]:a<b;}
    void swap(size_t a,size_t b) {
        std::swap(heap_[a],heap_[b]);positions_[heap_[a]]=int(a);positions_[heap_[b]]=int(b);
    }
    void offer(uint32_t id,uint32_t cost) {
        if(positions_[id]==-2||cost>=distance_[id])return;
        distance_[id]=cost;
        if(positions_[id]<0){positions_[id]=int(heap_.size());heap_.push_back(id);}
        auto at=size_t(positions_[id]);while(at&&less(heap_[at],heap_[(at-1)/2])){swap(at,(at-1)/2);at=(at-1)/2;}
    }
    void solve() {
        if(solving_!=infinity) {
            if(solveEdge_<reverse_[solving_].size()) {
                const auto& e=reverse_[solving_][solveEdge_++];offer(e.from,add(distance_[solving_],e.cost));return;
            }
            solving_=infinity;return;
        }
        if(heap_.empty()){phase_=Phase::Done;return;}
        solving_=heap_[0];solveEdge_=0;swap(0,heap_.size()-1);heap_.pop_back();positions_[solving_]=-2;
        for(size_t at=0;;) {
            const size_t left=at*2+1;if(left>=heap_.size())break;
            const size_t right=left+1,best=right<heap_.size()&&less(heap_[right],heap_[left])?right:left;
            if(!less(heap_[best],heap_[at]))break;
            swap(at,best);at=best;
        }
    }
};
}
