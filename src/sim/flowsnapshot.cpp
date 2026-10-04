#include "flowsnapshot.h"
#include "flowworkers.h"
#include <algorithm>
#include <exception>
#include <future>
#include <stdexcept>

namespace tak::sim::flow {
namespace {void mix(uint64_t& hash,uint64_t value){hash=(hash^value)*1099511628211ULL;}}
PreparedTileBuilder::PreparedTileBuilder(RawTile input,std::shared_ptr<const Tile> previous)
    :input_(std::move(input)),previous_(std::move(previous)),result_(std::make_shared<Tile>()),changed_(!previous_) {
    const auto& in=input_;
    if(in.width<=0||in.height<=0||in.footX<1||in.footX>64||in.footZ<1||in.footZ>64||
       in.originX<0||in.originZ<0||in.originX>=in.width||in.originZ>=in.height||
       in.originX%kTileSize||in.originZ%kTileSize)
        throw std::invalid_argument("flow prepared tile dimensions");
    rawW_=kTileSize+in.footX-1;rawH_=kTileSize+in.footZ-1;
    if(in.uniform) {
        if(*in.uniform>255)throw std::invalid_argument("flow uniform cost exceeds 255");
        if(!in.costs.empty())throw std::invalid_argument("flow uniform tile also has raw costs");
        stage_=Stage::Emit;
    } else {
        if(in.costs.size()!=size_t(rawW_)*rawH_)throw std::invalid_argument("flow raw tile size");
        blocked_.resize(size_t(rawW_+1)*(rawH_+1));
    }
}
size_t PreparedTileBuilder::bytes() const {
    return sizeof(*this)+sizeof(Tile)+input_.costs.capacity()*sizeof(uint16_t)+blocked_.capacity()*sizeof(uint32_t);
}
size_t PreparedTileBuilder::step(size_t budget) {
    size_t work=0;
    const auto& in=input_;
    while(work<budget&&!done()) {
        ++work;
        if(stage_==Stage::Sum) {
            const int x=int(cursor_%rawW_),z=int(cursor_/rawW_);
            const uint16_t cost=in.costs[cursor_];mix(fingerprint_,cost);
            if(cost>255)throw std::invalid_argument("flow snapshot cost exceeds 255");
            const size_t row=size_t(z+1)*(rawW_+1),above=size_t(z)*(rawW_+1);
            if(x==0)blocked_[row]=0;
            blocked_[row+x+1]=blocked_[above+x+1]+blocked_[row+x]-blocked_[above+x]+(cost==0);
            if(++cursor_==in.costs.size()){cursor_=0;stage_=Stage::Emit;}
        } else if(stage_==Stage::Emit) {
            const int x=int(cursor_%kTileSize),z=int(cursor_/kTileSize);
            uint16_t cost=0;
            if(in.originX+x<in.width&&in.originZ+z<in.height) {
                if(in.uniform) {
                    const int left=in.originX+x-in.footX/2,top=in.originZ+z-in.footZ/2;
                    if(left>=0&&top>=0&&left+in.footX<=in.width&&top+in.footZ<=in.height)cost=*in.uniform;
                } else {
                    const size_t top=size_t(z)*(rawW_+1),bottom=size_t(z+in.footZ)*(rawW_+1);
                    const uint32_t count=blocked_[bottom+x+in.footX]-blocked_[bottom+x]-blocked_[top+x+in.footX]+blocked_[top+x];
                    if(!count)cost=in.costs[size_t(z+in.footZ/2)*rawW_+x+in.footX/2];
                }
            }
            result_->cost[cursor_]=cost;mix(fingerprint_,cost);
            if(previous_&&previous_->cost[cursor_]!=cost)changed_=true;
            if(in.uniform&&cost) {
                result_->component[cursor_]=1;result_->componentCount=1;result_->minimumCost=cost;
            }
            if(++cursor_==kTileCells) {
                cursor_=0;
                stage_=in.uniform||!changed_?Stage::Done:Stage::Label;
            }
        } else if(stage_==Stage::Label) {
            if(head_<tail_) {
                const int cell=queue_[head_++],x=cell%kTileSize,z=cell/kTileSize;
                constexpr int dx[4]={0,1,0,-1},dz[4]={-1,0,1,0};
                for(int d=0;d<4;++d) {
                    const int nx=x+dx[d],nz=z+dz[d];
                    if(nx<0||nz<0||nx>=kTileSize||nz>=kTileSize)continue;
                    const size_t n=size_t(nz)*kTileSize+nx;
                    if(result_->cost[n]&&!result_->component[n]) {
                        result_->component[n]=result_->componentCount;queue_[tail_++]=uint16_t(n);
                        mix(fingerprint_,n);mix(fingerprint_,result_->componentCount);
                    }
                }
            } else if(cursor_<kTileCells) {
                const size_t cell=cursor_++;
                if(result_->cost[cell])result_->minimumCost=std::min(result_->minimumCost,result_->cost[cell]);
                if(result_->cost[cell]&&!result_->component[cell]) {
                    ++result_->componentCount;result_->component[cell]=result_->componentCount;
                    mix(fingerprint_,cell);mix(fingerprint_,result_->componentCount);
                    head_=0;tail_=1;queue_[0]=uint16_t(cell);
                }
            } else stage_=Stage::Done;
        }
    }
    return work;
}
uint64_t PreparedTileBuilder::checksum() const {
    uint64_t hash=1469598103934665603ULL;
    mix(hash,uint64_t(stage_));mix(hash,input_.width);mix(hash,input_.height);
    mix(hash,input_.footX);mix(hash,input_.footZ);mix(hash,input_.originX);mix(hash,input_.originZ);
    mix(hash,input_.fingerprint);mix(hash,input_.uniform.has_value());mix(hash,input_.uniform.value_or(0));
    mix(hash,cursor_);mix(hash,head_);mix(hash,tail_);mix(hash,changed_);mix(hash,bool(previous_));
    mix(hash,result_->componentCount);mix(hash,result_->minimumCost);mix(hash,fingerprint_);return hash;
}
uint64_t SnapshotBuilder::checksum() const {
    uint64_t hash=1469598103934665603ULL;
    mix(hash,uint64_t(stage_));mix(hash,width_);mix(hash,height_);mix(hash,footX_);mix(hash,footZ_);
    mix(hash,tile_);mix(hash,cursor_);mix(hash,samples_);mix(hash,work_);mix(hash,dirtyHash_);
    mix(hash,anyChanged_);mix(hash,bool(previous_));mix(hash,publishedHash_);mix(hash,builder_.checksum());
    mix(hash,sampling_.fingerprint);mix(hash,sampling_.originX);mix(hash,sampling_.originZ);
    mix(hash,sampling_.uniform.has_value());mix(hash,sampling_.uniform.value_or(0));
    mix(hash,uniformCost_.has_value());mix(hash,uniformCost_.value_or(0));
    for(const auto& p:pending_) {mix(hash,p.tile);mix(hash,p.builder?p.builder->checksum():uint64_t(p.uniform.value_or(0)));}
    return hash;
}
size_t SnapshotBuilder::bytes() const {
    size_t bytes=sizeof(*this)-sizeof(builder_)+builder_.bytes()+(dirty_.capacity()+7)/8+
        sampling_.costs.capacity()*sizeof(uint16_t);
    for(const auto& p:pending_)bytes+=sizeof(Pending)+(p.builder?p.builder->bytes():0);
    return bytes;
}
SnapshotBuilder::SnapshotBuilder(int width,int height,int footX,int footZ,Sampler sampler,
                                 std::shared_ptr<const Topology> previous,std::vector<bool> dirty,Limits limits,
                                 Hooks hooks,unsigned workers)
    :width_(width),height_(height),footX_(footX),footZ_(footZ),sampler_(std::move(sampler)),
     hooks_(std::move(hooks)),workers_(workers),previous_(std::move(previous)),dirty_(std::move(dirty)),builder_(width,height,limits) {
    if(footX<1||footZ<1||footX>64||footZ>64||!sampler_)throw std::invalid_argument("flow snapshot footprint/sampler");
    if(builder_.failed()) {stage_=Stage::Failed;return;}
    tilesX_=(width+kTileSize-1)/kTileSize;
    rawW_=kTileSize+footX-1;rawH_=kTileSize+footZ-1;
    const size_t count=size_t(tilesX_)*((height+kTileSize-1)/kTileSize);
    if(previous_&&(previous_->width!=width||previous_->height!=height))throw std::invalid_argument("flow snapshot dimensions changed");
    if(dirty_.empty())dirty_.assign(count,true);
    if(dirty_.size()!=count)throw std::invalid_argument("flow snapshot dirty tile count");
    for(bool value:dirty_)mix(dirtyHash_,value);
}
void SnapshotBuilder::enqueue() {
    const bool full=sampling_.originX-footX_/2>=0&&sampling_.originZ-footZ_/2>=0&&
        sampling_.originX-footX_/2+rawW_<=width_&&sampling_.originZ-footZ_/2+rawH_<=height_;
    const auto uniform=sampling_.uniform&&(full||!*sampling_.uniform)?sampling_.uniform:std::nullopt;
    pending_.push_back({tile_,std::make_unique<PreparedTileBuilder>(std::move(sampling_),previous_?previous_->tiles[tile_]:nullptr),uniform,{}});
    sampling_={};++tile_;stage_=Stage::Choose;
}
size_t SnapshotBuilder::prepare(size_t budget) {
    // Fixed quotas, submission order, joins and publication order in both modes.
    // No worker captures this builder or any live World/sampling callback.
    std::array<std::future<size_t>,maxPreparingTiles> futures;
    std::array<size_t,maxPreparingTiles> work{};
    std::exception_ptr failure;
    size_t selected=0,left=budget,remaining=0;
    for(const auto& p:pending_)if(!p.done())++remaining;
    const bool parallel=workers_&&remaining>=2&&budget/remaining>=4096;
    for(auto& p:pending_) {
        if(p.done()||!left)continue;
        const size_t quota=left/remaining+(left%remaining!=0);left-=quota;--remaining;
        auto* job=p.builder.get();const size_t at=selected++;
        try {
            if(parallel)futures[at]=submitWork([job,quota]{return job->step(quota);});
            else work[at]=job->step(quota);
        }catch(...){if(!failure)failure=std::current_exception();}
    }
    for(size_t i=0;i<selected;++i)if(futures[i].valid()) {
        try {work[i]=futures[i].get();}catch(...){if(!failure)failure=std::current_exception();}
    }
    if(failure)std::rethrow_exception(failure);
    size_t total=0;for(size_t i=0;i<selected;++i)total+=work[i];return total;
}
size_t SnapshotBuilder::step(size_t budget) {
    size_t work=0;
    while(work<budget&&!done()&&!failed()) {
        if(stage_==Stage::Topology) {
            work+=builder_.step(budget-work);
            if(builder_.done())stage_=Stage::Done;
            if(builder_.failed())stage_=Stage::Failed;
            continue;
        }
        if(!pending_.empty()&&pending_.front().done()) {
            ++work;
            const auto index=pending_.front().tile;
            auto tile=pending_.front().finish();
            mix(publishedHash_,index);
            mix(publishedHash_,pending_.front().builder?pending_.front().builder->checksum():uint64_t(*pending_.front().uniform));
            if(!previous_||previous_->tiles[index]!=tile)anyChanged_=true;
            builder_.reuseTile(index,tile);
            if(pending_.front().uniform){uniformCost_=pending_.front().uniform;uniformTile_=tile;}
            if(hooks_.publish)hooks_.publish(index,tile);
            pending_.pop_front();continue;
        }
        if(stage_==Stage::Choose&&(pending_.size()==maxPreparingTiles||tile_==dirty_.size())) {
            if(!pending_.empty()){work+=prepare(budget-work);continue;}
            ++work;stage_=previous_&&!anyChanged_?Stage::Done:Stage::Topology;continue;
        }
        ++work;
        if(stage_==Stage::Choose) {
            if(previous_&&!dirty_[tile_]) {builder_.reuseTile(tile_,previous_->tiles[tile_]);++tile_;continue;}
            if(hooks_.prepared)if(auto cached=hooks_.prepared(tile_)) {
                if(!previous_||previous_->tiles[tile_]!=cached)anyChanged_=true;
                builder_.reuseTile(tile_++,std::move(cached));continue;
            }
            sampling_={};sampling_.width=width_;sampling_.height=height_;sampling_.footX=footX_;sampling_.footZ=footZ_;
            sampling_.originX=int(tile_%tilesX_)*kTileSize;sampling_.originZ=int(tile_/tilesX_)*kTileSize;
            if(hooks_.uniform) {
                const int x=std::max(0,sampling_.originX-footX_/2),z=std::max(0,sampling_.originZ-footZ_/2);
                const int right=std::min(width_,sampling_.originX-footX_/2+rawW_);
                const int bottom=std::min(height_,sampling_.originZ-footZ_/2+rawH_);
                sampling_.uniform=hooks_.uniform({x,z,right-x,bottom-z});
            }
            if(sampling_.uniform) {
                const bool full=sampling_.originX-footX_/2>=0&&sampling_.originZ-footZ_/2>=0&&
                    sampling_.originX-footX_/2+rawW_<=width_&&sampling_.originZ-footZ_/2+rawH_<=height_;
                if(uniformTile_&&uniformCost_==sampling_.uniform&&(full||!*sampling_.uniform)&&
                   (!previous_||previous_->tiles[tile_]==uniformTile_)) {
                    pending_.push_back({tile_,{},uniformCost_,uniformTile_});
                    ++tile_;continue;
                }
                enqueue();continue;
            }
            sampling_.costs.resize(size_t(rawW_)*rawH_);cursor_=0;stage_=Stage::Sample;
        } else if(stage_==Stage::Sample) {
            const int x=int(cursor_%rawW_),z=int(cursor_/rawW_);
            const int wx=sampling_.originX-footX_/2+x,wz=sampling_.originZ-footZ_/2+z;
            const uint16_t cost=wx>=0&&wz>=0&&wx<width_&&wz<height_?sampler_(wx,wz):0;
            if(cost>255)throw std::invalid_argument("flow snapshot cost exceeds 255");
            sampling_.costs[cursor_]=cost;mix(sampling_.fingerprint,cost);++samples_;
            if(++cursor_==sampling_.costs.size())enqueue();
        }
    }
    work_+=work;return work;
}
void SnapshotBuilder::dirtyRectangle(std::vector<bool>& dirty,int width,int height,int footX,int footZ,int x,int z,int w,int h) {
    if(w<=0||h<=0||width<=0||height<=0)return;
    const int tilesX=(width+kTileSize-1)/kTileSize,tilesZ=(height+kTileSize-1)/kTileSize;
    if(dirty.size()!=size_t(tilesX)*tilesZ)throw std::invalid_argument("flow dirty rectangle dimensions");
    // Expand by footprint offsets before mapping to tiles. Use 64-bit bounds
    // so a malformed rectangle cannot wrap an end coordinate into the map.
    const int64_t x0=std::max<int64_t>(0,int64_t(x)-(footX-1-footX/2));
    const int64_t z0=std::max<int64_t>(0,int64_t(z)-(footZ-1-footZ/2));
    const int64_t x1=std::min<int64_t>(width,int64_t(x)+w+footX/2);
    const int64_t z1=std::min<int64_t>(height,int64_t(z)+h+footZ/2);
    if(x1<=x0||z1<=z0)return;
    for(int tz=int(z0)/kTileSize;tz<=(z1-1)/kTileSize;++tz)
        for(int tx=int(x0)/kTileSize;tx<=(x1-1)/kTileSize;++tx)dirty[size_t(tz)*tilesX+tx]=true;
}
}
