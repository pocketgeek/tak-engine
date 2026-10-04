#include "flowfield.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace tak::sim::flow {
namespace {
void fold(uint64_t& h,uint64_t value) {h=(h^value)*1099511628211ull;}
constexpr int dx[8]={0,1,0,-1,1,1,-1,-1};
constexpr int dz[8]={-1,0,1,0,-1,1,1,-1};
int local(Cell c) { return (c.z%kTileSize)*kTileSize+c.x%kTileSize; }
uint32_t addCost(uint32_t value,uint32_t cost) {
    return value>=kUnreachable-cost ? kUnreachable : value+cost;
}
}
bool GoalRegion::contains(Cell c) const {
    if(kind==Kind::Rectangle)
        return ((c.x==x||c.x==maxX)&&c.z>=z&&c.z<=maxZ)||
            ((c.z==z||c.z==maxZ)&&c.x>=x&&c.x<=maxX);
    if(outerSquared<0)return false;
    const int64_t dx=int64_t(c.x)-x,dz=int64_t(c.z)-z;
    // Reject distant coordinates before squaring to keep malformed endpoints
    // from overflowing signed arithmetic. Authored radii are signed 32-bit.
    if(dx>46340||dx< -46340||dz>46340||dz< -46340)return false;
    const uint64_t squared=uint64_t(dx*dx+dz*dz);
    return squared<=uint32_t(outerSquared)&&(kind!=Kind::Ring||squared*256>=innerWorldSquared);
}
std::pair<Cell,Cell> GoalRegion::bounds() const {
    if(kind==Kind::Rectangle)return {{x,z},{maxX,maxZ}};
    if(outerSquared<0)return {{0,0},{-1,-1}};
    uint32_t radius=0;
    for(uint32_t bit=32768;bit;bit>>=1) {
        const uint32_t next=radius|bit;
        if(uint64_t(next)*next<=uint32_t(outerSquared))radius=next;
    }
    const auto bounded=[](int64_t v){return int(std::clamp<int64_t>(v,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()));};
    return {{bounded(int64_t(x)-radius),bounded(int64_t(z)-radius)},
            {bounded(int64_t(x)+radius),bounded(int64_t(z)+radius)}};
}
int Topology::tileAt(Cell c) const {
    if(c.x<0 || c.z<0 || c.x>=width || c.z>=height) return -1;
    return (c.z/kTileSize)*tilesX+c.x/kTileSize;
}
uint16_t Topology::cost(Cell c) const {
    const int t=tileAt(c);return t<0?0:tiles[size_t(t)]->cost[size_t(local(c))];
}
uint32_t Topology::componentAt(Cell c) const {
    const int t=tileAt(c);if(t<0)return kUnreachable;
    const auto component=tiles[size_t(t)]->component[size_t(local(c))];
    return component ? offsets[size_t(t)]+component-1 : kUnreachable;
}
size_t Topology::metadataBytes() const {
    return sizeof(*this)+tiles.capacity()*sizeof(tiles[0])+
        offsets.capacity()*sizeof(uint32_t)+edges.capacity()*sizeof(Edge)+
        edgeOffsets.capacity()*sizeof(uint32_t);
}
size_t Topology::bytes() const {return metadataBytes()+tiles.capacity()*sizeof(Tile);}
uint64_t TopologyBuilder::checksum() const {
    uint64_t h=digest_;
    for(uint64_t value:{uint64_t(result_->width),uint64_t(result_->height),uint64_t(limits_.maxTiles),
        uint64_t(limits_.maxComponents),uint64_t(limits_.maxEdges),uint64_t(phase_),uint64_t(started_),work_,uint64_t(tile_),uint64_t(scan_),
        uint64_t(head_),uint64_t(tail_),uint64_t(border_),uint64_t(index_),
        uint64_t(result_->components),uint64_t(result_->edges.size()),uint64_t(sorted_.size())})fold(h,value);
    return h;
}
size_t TopologyBuilder::scratchBytes() const {
    return sizeof(*this)+writable_.capacity()*sizeof(writable_[0])+(reused_.capacity()+7)/8+
        sorted_.capacity()*sizeof(Edge)+cursor_.capacity()*sizeof(uint32_t)+
        edgePairs_.bucket_count()*sizeof(void*)+edgePairs_.size()*(sizeof(uint64_t)+2*sizeof(void*));
}
size_t TopologyBuilder::bytes() const {
    // Conservative total: shared tiles may be charged in both generations.
    return scratchBytes()+(result_?result_->bytes():0);
}
TopologyBuilder::TopologyBuilder(int width,int height,Limits limits):limits_(limits),result_(std::make_shared<Topology>()) {
    if(width<=0 || height<=0 || width>32768 || height>32768) throw std::invalid_argument("flow topology dimensions");
    auto& r=*result_;r.width=width;r.height=height;
    r.tilesX=(width+kTileSize-1)/kTileSize;r.tilesZ=(height+kTileSize-1)/kTileSize;
    const size_t count=size_t(r.tilesX)*r.tilesZ;
    if(count>limits.maxTiles) {phase_=Phase::Failed;return;}
    r.tiles.resize(count,std::make_shared<const Tile>());writable_.resize(count);r.offsets.resize(count+1);
    reused_.resize(count);
}
void TopologyBuilder::setCost(Cell c,uint16_t cost) {
    if(started_ || failed()) throw std::logic_error("flow topology already started/unavailable");
    const int t=result_->tileAt(c);if(t<0)throw std::out_of_range("flow topology cell");
    if(cost>255)throw std::invalid_argument("flow cell cost exceeds 255");
    if(reused_[size_t(t)])throw std::logic_error("immutable reused flow tile");
    if(!writable_[size_t(t)]) {
        if(!cost)return;
        writable_[size_t(t)]=std::make_shared<Tile>();result_->tiles[size_t(t)]=writable_[size_t(t)];
    }
    writable_[size_t(t)]->cost[size_t(local(c))]=cost;
    fold(digest_,uint64_t(t));fold(digest_,uint64_t(local(c)));fold(digest_,cost);
}
void TopologyBuilder::reuseTile(size_t index,std::shared_ptr<const Tile> tile) {
    if(started_ || failed() || !tile || index>=writable_.size())throw std::logic_error("invalid reused flow tile");
    fold(digest_,index);fold(digest_,tile->componentCount);fold(digest_,tile->minimumCost);
    result_->tiles[index]=std::move(tile);writable_[index].reset();reused_[index]=true;
}
void TopologyBuilder::labelOne() {
    if(tile_>=writable_.size()) {phase_=Phase::Offsets;tile_=0;return;}
    if(!writable_[tile_]) {++tile_;scan_=head_=tail_=0;return;}
    auto& tile=*writable_[tile_];
    if(head_<tail_) {
        const int cell=queue_[head_++],x=cell%kTileSize,z=cell/kTileSize;
        // Four neighbours are enough to identify connectivity when diagonals may
        // not cut corners. Detailed integration below still uses eight neighbours.
        for(int d=0;d<4;++d) {
            const int nx=x+dx[d],nz=z+dz[d];
            if(nx<0 || nz<0 || nx>=kTileSize || nz>=kTileSize)continue;
            const size_t n=size_t(nz)*kTileSize+nx;
            if(tile.cost[n] && !tile.component[n]) {
                tile.component[n]=tile.componentCount;queue_[tail_++]=uint16_t(n);
                fold(digest_,tile_);fold(digest_,n);fold(digest_,tile.componentCount);
            }
        }
    } else if(scan_<kTileCells) {
        const size_t cell=scan_++;
        if(tile.cost[cell])tile.minimumCost=std::min(tile.minimumCost,tile.cost[cell]);
        if(tile.cost[cell] && !tile.component[cell]) {
            ++tile.componentCount;tile.component[cell]=tile.componentCount;
            fold(digest_,tile_);fold(digest_,cell);fold(digest_,tile.componentCount);
            head_=0;tail_=1;queue_[0]=uint16_t(cell);
        }
    } else {++tile_;scan_=head_=tail_=0;}
}
void TopologyBuilder::borderOne() {
    auto& r=*result_;
    if(tile_>=r.tiles.size()) {phase_=Phase::Count;index_=0;r.edgeOffsets.assign(size_t(r.components)+1,0);return;}
    // Right and bottom borders; emit both directions for each connected pair.
    const int tx=int(tile_)%r.tilesX,tz=int(tile_)/r.tilesX;
    const int offset=int(border_%kTileSize);
    const Cell a=border_<kTileSize ? Cell{(tx+1)*kTileSize-1,tz*kTileSize+offset}
                                  : Cell{tx*kTileSize+offset,(tz+1)*kTileSize-1};
    const Cell b=border_<kTileSize ? Cell{a.x+1,a.z}:Cell{a.x,a.z+1};
    const auto ca=r.componentAt(a),cb=r.componentAt(b);
    if(ca!=kUnreachable && cb!=kUnreachable) {
        // One adjacency per component pair is enough for the coarse route.
        // Detailed integration enumerates every actual crossing along the border.
        // Without this, an open tile edge visits the same neighbour 64 times.
        const uint64_t key=(uint64_t(std::min(ca,cb))<<32)|std::max(ca,cb);
        if(!edgePairs_.contains(key)) {
            if(r.edges.size()+2>limits_.maxEdges) {phase_=Phase::Failed;return;}
            edgePairs_.insert(key);
            fold(digest_,ca);fold(digest_,cb);fold(digest_,a.x);fold(digest_,a.z);fold(digest_,b.x);fold(digest_,b.z);
            r.edges.push_back({ca,cb,a,b});r.edges.push_back({cb,ca,b,a});
        }
    }
    if(++border_==2*kTileSize) {border_=0;++tile_;}
}
size_t TopologyBuilder::step(size_t budget) {
    started_=true;size_t work=0;
    while(work<budget && !done() && !failed()) {
        ++work;++work_;
        switch(phase_) {
        case Phase::Label:labelOne();break;
        case Phase::Offsets:
            if(tile_==result_->tiles.size()) {
                result_->offsets[tile_]=result_->components;tile_=0;phase_=Phase::Edges;
            } else {
                result_->offsets[tile_]=result_->components;
                result_->components+=result_->tiles[tile_++]->componentCount;
                if(result_->components>limits_.maxComponents)phase_=Phase::Failed;
            }
            break;
        case Phase::Edges:borderOne();break;
        case Phase::Count:
            if(index_<result_->edges.size()) ++result_->edgeOffsets[result_->edges[index_++].from+1];
            else {phase_=Phase::Prefix;index_=1;cursor_.resize(result_->edgeOffsets.size());}
            break;
        case Phase::Prefix:
            if(index_<result_->edgeOffsets.size()) {
                result_->edgeOffsets[index_]+=result_->edgeOffsets[index_-1];
                cursor_[index_]=result_->edgeOffsets[index_];++index_;
            } else {phase_=Phase::Allocate;sorted_.reserve(result_->edges.size());}
            break;
        case Phase::Allocate:
            if(sorted_.size()<result_->edges.size())sorted_.push_back({});
            else {phase_=Phase::Scatter;index_=0;}
            break;
        case Phase::Scatter:
            if(index_<result_->edges.size()) {
                const auto& edge=result_->edges[index_++];sorted_[cursor_[edge.from]++]=edge;
            } else {result_->edges=std::move(sorted_);phase_=Phase::Done;}
            break;
        default:break;
        }
    }
    return work;
}
std::shared_ptr<const Topology> TopologyBuilder::finish() const {return done()?result_:nullptr;}
Destination::Destination(std::shared_ptr<const Topology> topology,std::vector<Cell> goals)
    :topology_(std::move(topology)),goals_(std::move(goals)) {
    if(!topology_)throw std::invalid_argument("missing flow topology");
    if(goals_.size()>4096)throw std::length_error("flow destination goal limit");
    distance_.assign(topology_->components,kUnreachable);queue_.reserve(topology_->components);
    undiscovered_.reserve(topology_->tiles.size());
    for(const auto& tile:topology_->tiles)undiscovered_.push_back(tile->componentCount);
    std::sort(goals_.begin(),goals_.end(),[](Cell a,Cell b){return a.z!=b.z?a.z<b.z:a.x<b.x;});
    goals_.erase(std::unique(goals_.begin(),goals_.end()),goals_.end());
    if(!goals_.empty())goalMin_=goalMax_=goals_.front();
    for(Cell c:goals_) {
        fold(digest_,c.x);fold(digest_,c.z);
        goalMin_.x=std::min(goalMin_.x,c.x);goalMin_.z=std::min(goalMin_.z,c.z);
        goalMax_.x=std::max(goalMax_.x,c.x);goalMax_.z=std::max(goalMax_.z,c.z);
        const auto component=topology_->componentAt(c);
        if(component!=kUnreachable && distance_[component]==kUnreachable) {
            discover(component,0);
        }
    }
    if(!queue_.empty())edge_=topology_->edgeOffsets[queue_[0]];
}
Destination::Destination(std::shared_ptr<const Topology> topology,GoalRegion region)
    :Destination(std::move(topology),std::vector<Cell>{}) {
    region_=region;
    for(uint64_t value:{uint64_t(region.kind),uint64_t(region.x),uint64_t(region.z),uint64_t(region.maxX),
        uint64_t(region.maxZ),uint64_t(region.outerSquared),region.innerWorldSquared})fold(digest_,value);
    const auto [low,high]=region.bounds();goalMin_=low;goalMax_=high;
    seedMin_={std::max(0,low.x),std::max(0,low.z)};
    seedMax_={std::min(topology_->width-1,high.x),std::min(topology_->height-1,high.z)};
    if(seedMin_.x<=seedMax_.x&&seedMin_.z<=seedMax_.z)
        regionCells_=size_t(seedMax_.x-seedMin_.x+1)*size_t(seedMax_.z-seedMin_.z+1);
}
size_t Destination::step(size_t budget) {
    size_t work=0;
    while(work<budget && !done()) {
        ++work;++work_;
        if(regionSeed_<regionCells_) {
            const size_t width=size_t(seedMax_.x-seedMin_.x+1);
            const Cell c{seedMin_.x+int(regionSeed_%width),seedMin_.z+int(regionSeed_/width)};
            if(region_->contains(c)) {
                const auto component=topology_->componentAt(c);
                if(component!=kUnreachable&&distance_[component]==kUnreachable) {
                    discover(component,0);
                }
            }
            if(++regionSeed_==regionCells_&&!queue_.empty())edge_=topology_->edgeOffsets[queue_[0]];
            continue;
        }
        const auto component=queue_[head_];
        if(edge_<topology_->edgeOffsets[component+1]) {
            const auto& edge=topology_->edges[edge_++];
            if(distance_[edge.to]==kUnreachable) {
                discover(edge.to,distance_[component]+1);
            }
        } else if(++head_<queue_.size())edge_=topology_->edgeOffsets[queue_[head_]];
    }
    return work;
}
void Destination::discover(uint32_t component,uint32_t distance) {
    distance_[component]=distance;queue_.push_back(component);
    fold(digest_,component);fold(digest_,distance);
    // Repeated offsets belong to empty tiles. upper_bound identifies the
    // unique nonempty tile owning this global component without another map.
    const size_t tile=size_t(std::upper_bound(topology_->offsets.begin(),topology_->offsets.end(),component)-
        topology_->offsets.begin()-1);
    --undiscovered_[tile];
}
bool Destination::tileReady(int tile) const {
    return tile>=0&&size_t(tile)<undiscovered_.size()&&
        (done()||(regionSeed_==regionCells_&&!undiscovered_[size_t(tile)]));
}
uint64_t Destination::checksum() const {
    uint64_t h=digest_;
    for(uint64_t value:{work_,uint64_t(head_),uint64_t(edge_),uint64_t(queue_.size()),
        uint64_t(regionSeed_),uint64_t(regionCells_)})fold(h,value);
    return h;
}
uint32_t Destination::distance(uint32_t component) const {
    return component<distance_.size()?distance_[component]:kUnreachable;
}
bool Destination::reachable(Cell c) const {return regionSeed_==regionCells_ && distance(topology_->componentAt(c))!=kUnreachable;}
uint32_t Destination::estimate(Cell c) const {
    const int64_t dx=std::max({int64_t(goalMin_.x)-c.x,int64_t(0),int64_t(c.x)-goalMax_.x});
    const int64_t dz=std::max({int64_t(goalMin_.z)-c.z,int64_t(0),int64_t(c.z)-goalMax_.z});
    return uint32_t(std::min<int64_t>(kUnreachable-1,std::max(dx,dz)*1024+std::min(dx,dz)*424));
}
size_t Destination::bytes() const {return sizeof(*this)+goals_.capacity()*sizeof(Cell)+(distance_.capacity()+queue_.capacity())*sizeof(uint32_t)+undiscovered_.capacity()*sizeof(uint16_t);}
bool Field::next(Cell from,Cell& to) const {
    if(from.x<originX || from.z<originZ || from.x>=originX+kTileSize || from.z>=originZ+kTileSize)return false;
    const auto d=direction[size_t(local(from))];
    if(d>7)return false;
    to={from.x+dx[d],from.z+dz[d]};return true;
}
uint64_t Field::hash() const {
    uint64_t h=1469598103934665603ull;
    for(size_t i=0;i<distance.size();++i) {
        for(unsigned shift=0;shift<32;shift+=8) {h^=uint8_t(distance[i]>>shift);h*=1099511628211ull;}
        h^=direction[i];h*=1099511628211ull;
    }
    return h;
}
FieldBuilder::FieldBuilder(std::shared_ptr<const Destination> destination,int tile):destination_(std::move(destination)) {
    if(!destination_ || !destination_->tileReady(tile))
        throw std::invalid_argument("flow field destination/tile not ready");
    result_.tile=tile;result_.distance.fill(kUnreachable);result_.direction.fill(255);heapPos_.fill(-1);
    originX_=(tile%destination_->topology().tilesX)*kTileSize;
    originZ_=(tile/destination_->topology().tilesX)*kTileSize;
    result_.originX=originX_;result_.originZ=originZ_;
    goalSeeds_=destination_->goals().size();
    if(destination_->region()) {
        const auto [low,high]=destination_->region()->bounds();
        seedMin_={std::max(originX_,low.x),std::max(originZ_,low.z)};
        seedMax_={std::min(originX_+63,high.x),std::min(originZ_+63,high.z)};
        goalSeeds_=seedMin_.x<=seedMax_.x&&seedMin_.z<=seedMax_.z?
            size_t(seedMax_.x-seedMin_.x+1)*size_t(seedMax_.z-seedMin_.z+1):0;
    }
    // Subtract a constant so distant goals do not inflate per-tile distances.
    // The goal-box heuristic biases otherwise equivalent portals toward a
    // diagonal heading on open ground instead of an entire-map L-shaped route.
    biasBase_=std::min({destination_->estimate({originX_,originZ_}),
        destination_->estimate({originX_+63,originZ_}),destination_->estimate({originX_,originZ_+63}),
        destination_->estimate({originX_+63,originZ_+63})});
    biasBase_=biasBase_>128*1448?biasBase_-128*1448:0;
}
bool FieldBuilder::less(uint16_t a,uint16_t b) const {
    return result_.distance[a]!=result_.distance[b]?result_.distance[a]<result_.distance[b]:a<b;
}
void FieldBuilder::swapHeap(size_t a,size_t b) {
    std::swap(heap_[a],heap_[b]);heapPos_[heap_[a]]=int32_t(a);heapPos_[heap_[b]]=int32_t(b);
}
void FieldBuilder::offer(uint16_t cell,uint32_t distance,uint8_t direction) {
    if(heapPos_[cell]==-2 || distance>=result_.distance[cell])return;
    result_.distance[cell]=distance;result_.direction[cell]=direction;
    fold(digest_,cell);fold(digest_,distance);fold(digest_,direction);
    if(heapPos_[cell]<0) {heapPos_[cell]=int32_t(heapSize_);heap_[heapSize_++]=cell;}
    size_t i=size_t(heapPos_[cell]);
    while(i && less(heap_[i],heap_[(i-1)/2])) {swapHeap(i,(i-1)/2);i=(i-1)/2;}
}
void FieldBuilder::seedOne() {
    const auto& topo=destination_->topology();
    const auto& tile=*topo.tiles[size_t(result_.tile)];
    if(scan_<goalSeeds_) {
        Cell c;
        if(destination_->region()) {
            const size_t width=size_t(seedMax_.x-seedMin_.x+1);
            c={seedMin_.x+int(scan_%width),seedMin_.z+int(scan_/width)};
        }else c=destination_->goals()[scan_];
        ++scan_;
        if(topo.tileAt(c)==result_.tile&&topo.cost(c)&&
            (!destination_->region()||destination_->goalContains(c)))offer(uint16_t(local(c)),0,8);
        return;
    }
    const size_t border=scan_++-goalSeeds_;
    if(border>=4*kTileSize) {phase_=Phase::Integrate;return;}
    const int d=int(border/kTileSize),offset=int(border%kTileSize);
    const Cell c=d==0?Cell{originX_+offset,originZ_}:d==1?Cell{originX_+63,originZ_+offset}:
        d==2?Cell{originX_+offset,originZ_+63}:Cell{originX_,originZ_+offset};
    const Cell next{c.x+dx[d],c.z+dz[d]};
    // tileReady guarantees final distances for every local component. FIFO
    // reverse BFS has already discovered every strictly lower-distance peer;
    // later discoveries can only be equal/higher and cannot add exit seeds.
    // Destination work runs before field jobs and all jobs join each tick.
    const auto from=destination_->distance(topo.componentAt(c));
    const auto to=destination_->distance(topo.componentAt(next));
    if(from!=kUnreachable && to!=kUnreachable && to+1==from)
        offer(uint16_t(local(c)),addCost(uint32_t(tile.cost[size_t(local(c))])*1024,
            (destination_->estimate(next)>biasBase_?destination_->estimate(next)-biasBase_:0)*uint32_t(tile.minimumCost)),uint8_t(d));
}
void FieldBuilder::integrateOne() {
    if(!heapSize_) {phase_=Phase::Done;return;}
    const uint16_t cell=heap_[0];swapHeap(0,heapSize_-1);--heapSize_;heapPos_[cell]=-2;
    size_t i=0;
    for(;;) {
        const size_t left=i*2+1;if(left>=heapSize_)break;
        const size_t right=left+1,best=right<heapSize_&&less(heap_[right],heap_[left])?right:left;
        if(!less(heap_[best],heap_[i]))break;
        swapHeap(i,best);i=best;
    }
    const auto& tile=*destination_->topology().tiles[size_t(result_.tile)];
    const int x=cell%kTileSize,z=cell/kTileSize;
    for(int d=0;d<8;++d) {
        const int nx=x+dx[d],nz=z+dz[d];
        if(nx<0 || nz<0 || nx>=kTileSize || nz>=kTileSize)continue;
        const uint16_t n=uint16_t(nz*kTileSize+nx);
        if(!tile.cost[n] || (d>=4 && (!tile.cost[size_t(z)*kTileSize+nx] || !tile.cost[size_t(nz)*kTileSize+x])))continue;
        // Reverse relaxation: cost of travelling n -> cell. Opposite direction
        // table is fixed, including the non-rotational diagonal storage order.
        constexpr uint8_t opposite[8]={2,3,0,1,6,7,4,5};
        offer(n,addCost(result_.distance[cell],uint32_t(tile.cost[n])*(d<4?1024:1448)),opposite[d]);
    }
}
size_t FieldBuilder::step(size_t budget) {
    size_t work=0;
    while(work<budget && !done()) {++work;++work_;if(phase_==Phase::Seeds)seedOne();else integrateOne();}
    return work;
}
uint64_t FieldBuilder::checksum() const {
    uint64_t h=digest_;
    for(uint64_t value:{work_,uint64_t(phase_),uint64_t(result_.tile),uint64_t(scan_),uint64_t(heapSize_),
        uint64_t(goalSeeds_),uint64_t(biasBase_)})fold(h,value);
    return h;
}
size_t FieldBuilder::bytes() const {return sizeof(*this);}
} // namespace tak::sim::flow
