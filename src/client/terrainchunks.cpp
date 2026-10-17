#include "client/terrainchunks.h"
#include "client/gpuvram.h"
#include "client/rendertelemetry.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace tak {
namespace {
constexpr size_t budget=size_t(256)<<20;
uint64_t nowNs() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
// What the cache may hold right now: its own budget, or less when the global GPU cap
// (Options GPU MEMORY LIMIT, or a post-failure tightening) leaves less room beside
// everything else that is resident.
size_t room(size_t own) {
    const size_t others=gpuvram::bytes()-std::min(own,gpuvram::bytes());
    const size_t cap=gpuvram::cap();
    return cap<=others?0:std::min(budget,cap-others);
}
}
TerrainChunks::TerrainChunks(terrain::Compositor& c):compositor_(c) {worker_=std::thread([this]{work();});}
TerrainChunks::~TerrainChunks() {
    {std::lock_guard lock(mutex_);stop_=true;}
    cv_.notify_all();worker_.join();clear();
}
void TerrainChunks::clear() {
    for(const auto& [key,cancelled]:pending_)cancelled->store(true,std::memory_order_relaxed);
    jobsCancelled_+=pending_.size();
    {std::lock_guard lock(mutex_);++epoch_;jobs_.clear();done_.clear();}
    cv_.notify_all();pending_.clear();visible_.clear();drawn_.clear();
    for(auto& [key,e]:cache_)gpuvram::destroy(e.texture);
    cache_.clear();bytes_=0;++revision_;
}
void TerrainChunks::invalidate(int bx0,int by0,int bx1,int by1) {
    if(bx0>=bx1 || by0>=by1)return;
    const auto affected=[&](const Key& key) {
        const auto [level,cx,cy]=key;
        return bx0<(cx+1)*32+1 && bx1>cx*32-1 && by0<(cy+1)*32+1 && by1>cy*32-1;
    };
    // Jobs own immutable tile snapshots. Cancel only edited chunks (including
    // gutters); continuous painting must not starve unrelated pending images.
    for(auto i=pending_.begin();i!=pending_.end();) {
        if(affected(i->first)) {i->second->store(true,std::memory_order_relaxed);i=pending_.erase(i);++jobsCancelled_;}
        else ++i;
    }
    {
        std::lock_guard lock(mutex_);
        const auto cancelled=[](const Job& job) {return job.cancelled->load(std::memory_order_relaxed);};
        std::erase_if(jobs_,cancelled);std::erase_if(done_,cancelled);
    }
    cv_.notify_all();
    for(auto i=cache_.begin();i!=cache_.end();) {
        if(affected(i->first)) {
            bytes_-=size_t(i->second.w+2)*(i->second.h+2)*4;
            gpuvram::destroy(i->second.texture);i=cache_.erase(i);
        } else ++i;
    }
    ++revision_;
}
void TerrainChunks::resetSource() {
    clear();
    std::unique_lock lock(mutex_);
    cv_.wait(lock,[&]{return !busy_;});
    // The worker is idle and holds the lock's handshake: its fields are ours now.
    mips_.clear();mipsEvicted_.clear();mipBytes_.store(0,std::memory_order_relaxed);
}
void TerrainChunks::setMipBudget(size_t high,size_t low,uint64_t idleMs) {
    mipHigh_.store(high,std::memory_order_relaxed);
    mipLow_.store(std::min(low,high),std::memory_order_relaxed);
    mipIdleMs_=idleMs;
}
void TerrainChunks::evictMips(size_t target) {
    while(mipBytes_.load(std::memory_order_relaxed)>target) {
        auto victim=mips_.end();
        for(auto i=mips_.begin();i!=mips_.end();++i)
            if(i->second.image.use_count()==1 && (victim==mips_.end() || i->second.lastUse<victim->second.lastUse))victim=i;
        if(victim==mips_.end())return;   // the rest is pinned by the running job
        mipBytes_.fetch_sub(victim->second.bytes,std::memory_order_relaxed);
        mipEvictions_.fetch_add(1,std::memory_order_relaxed);
        mipsEvicted_.insert(victim->first);
        mips_.erase(victim);
    }
}
terrain::SectionRef TerrainChunks::mip(uint32_t key,int level,bool stockTerrain) {
    if(!level)return compositor_.sectionImage(key,stockTerrain);
    const auto id=std::tuple{key,level,stockTerrain};
    if(auto i=mips_.find(id);i!=mips_.end()) {i->second.lastUse=++mipClock_;return i->second.image;}
    const terrain::SectionRef source=mip(key,level-1,stockTerrain);   // pinned while it is read
    const auto& src=*source;
    auto image=std::make_shared<jpeg::Image>();auto& out=*image;
    out.width=std::max(1,src.width/2);out.height=std::max(1,src.height/2);
    out.rgba.resize(size_t(out.width)*out.height*4);
    for(int y=0;y<out.height;++y)for(int x=0;x<out.width;++x)for(int c=0;c<4;++c) {
        unsigned sum=0;
        for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
            sum+=src.rgba[(size_t(std::min(y*2+dy,src.height-1))*src.width+std::min(x*2+dx,src.width-1))*4+c];
        out.rgba[(size_t(y)*out.width+x)*4+c]=uint8_t((sum+2)/4);
    }
    const size_t bytes=out.rgba.capacity();
    const size_t total=mipBytes_.fetch_add(bytes,std::memory_order_relaxed)+bytes;
    if(total>mipPeakBytes_.load(std::memory_order_relaxed))mipPeakBytes_.store(total,std::memory_order_relaxed);
    if(mipsEvicted_.count(id))mipRegens_.fetch_add(1,std::memory_order_relaxed);
    terrain::SectionRef result=std::move(image);   // pinned by the caller
    mips_[id]=Mip{result,bytes,++mipClock_};
    if(total>mipHigh_.load(std::memory_order_relaxed))evictMips(mipLow_.load(std::memory_order_relaxed));
    return result;
}
void TerrainChunks::compose(Job& j) {
    const auto [level,cx,cy]=j.key;const int tile=32>>level;
    std::vector<const jpeg::Image*> images;images.reserve(j.tiles.size());
    // Job-scoped ownership: every image this job reads stays pinned until it returns.
    std::map<uint32_t,terrain::SectionRef> decoded;
    for(const auto& t:j.tiles) {
        if(j.cancelled->load(std::memory_order_relaxed))return;
        auto [entry,added]=decoded.try_emplace(t.key);
        if(added)try {entry->second=mip(t.key,level,j.stockTerrain);}catch(...) {}
        images.push_back(entry->second.get());
    }
    if(level) {   // level 0 images are the compositor's (counted there)
        size_t pinned=0;
        for(const auto& [key,image]:decoded)if(image)pinned+=image->rgba.capacity();
        mipPinnedBytes_.store(pinned,std::memory_order_relaxed);
    }
    j.pixels.resize(size_t(j.w+2)*(j.h+2)*4);
    // A texel of real neighbouring terrain on every side prevents filtering
    // seams. At the map boundary repeat its edge, rather than wrapping a tile.
    for(int y=-1;y<=j.h;++y) {
        if(j.cancelled->load(std::memory_order_relaxed))return;
        for(int x=-1;x<=j.w;++x) {
            const int gx=std::clamp((cx*span>>level)+x,0,j.mapW-1);
            const int gy=std::clamp((cy*span>>level)+y,0,j.mapH-1);
            const size_t index=size_t(gy/tile-j.by)*j.bw+gx/tile-j.bx;
            const auto* image=images[index];if(!image)continue;
            const auto& t=j.tiles[index];
            const int sx=(int(t.col)*tile)%image->width,sy=(int(t.row)*tile)%image->height;
            const size_t from=(size_t(std::min(sy+gy%tile,image->height-1))*image->width+
                                     std::min(sx+gx%tile,image->width-1))*4;
            const size_t to=(size_t(y+1)*(j.w+2)+x+1)*4;
            std::memcpy(j.pixels.data()+to,image->rgba.data()+from,4);
        }
    }
}
void TerrainChunks::work() {
    std::unique_lock lock(mutex_);
    for(;;) {
        cv_.wait(lock,[&]{return stop_ || trimMips_ || (!jobs_.empty() && done_.size()<8);});
        if(stop_)return;
        if(trimMips_ && jobs_.empty()) {
            // Sustained low demand (the render thread's request): no job runs, so no
            // mip is pinned; the worker drops its own images (only it touches them).
            trimMips_=false;busy_=true;lock.unlock();
            if(!mips_.empty()) {
                for(const auto& [id,m]:mips_)mipsEvicted_.insert(id);
                mips_.clear();mipBytes_.store(0,std::memory_order_relaxed);
                mipTrims_.fetch_add(1,std::memory_order_relaxed);
            }
            lock.lock();busy_=false;cv_.notify_all();continue;
        }
        trimMips_=false;   // work arrived: demand is not low after all
        if(jobs_.empty() || done_.size()>=8)continue;
        auto job=std::move(jobs_.front());jobs_.pop_front();busy_=true;lock.unlock();
        const uint64_t started=nowNs();
        try {compose(job);}catch(...) {job.pixels.clear();}
        mipPinnedBytes_.store(0,std::memory_order_relaxed);
        // The job's pins are released: images it held past the CPU budgets can go now.
        compositor_.trim();
        if(mipBytes_.load(std::memory_order_relaxed)>mipHigh_.load(std::memory_order_relaxed))
            evictMips(mipLow_.load(std::memory_order_relaxed));
        composeNs_.fetch_add(nowNs()-started,std::memory_order_relaxed);
        jobsCompleted_.fetch_add(1,std::memory_order_relaxed);
        lock.lock();if(job.epoch==epoch_ && !job.cancelled->load(std::memory_order_relaxed))done_.push_back(std::move(job));
        busy_=false;cv_.notify_all();
    }
}
void TerrainChunks::prepare(SDL_Renderer* renderer,const tnt::Map& map,float x,float y,float zoom,int w,int h,bool linear,uint64_t nowMs) {
    ++frame_;visible_.clear();
    // Mip idle trim (workstream J): terrain work = a job queued or completed. After
    // mipIdleMs_ and 120 frames without any, ask the worker to drop its mip images
    // (regenerated on demand; the chunks already composed are unaffected).
    {
        const size_t seen=size_t(jobsQueued_+jobsCompleted_.load(std::memory_order_relaxed));
        if(seen!=lastJobsSeen_ || !lastWorkFrame_) {lastJobsSeen_=seen;lastWorkMs_=nowMs;lastWorkFrame_=frame_;}
        else if(mipBytes_.load(std::memory_order_relaxed) && nowMs-lastWorkMs_>=mipIdleMs_ && frame_-lastWorkFrame_>=120) {
            {std::lock_guard lock(mutex_);trimMips_=true;}
            cv_.notify_all();
            lastWorkMs_=nowMs;lastWorkFrame_=frame_;   // one request per idle period
        }
    }
    if(map.blocksX<=0 || map.blocksY<=0 || zoom<=0)return;
    float sx=1,sy=1;SDL_RenderGetScale(renderer,&sx,&sy);
    level_=0;
    const float density=zoom*std::max(sx,sy);
    while(level_<5 && (1.f/float(2<<(level_)))>=density)++level_;
    const int nx=(map.blocksX*32+span-1)/span,ny=(map.blocksY*32+span-1)/span;
    const int x0=std::clamp(int(std::floor(x/span)),0,nx-1),y0=std::clamp(int(std::floor(y/span)),0,ny-1);
    const int x1=std::clamp(int(std::floor((x+w/zoom)/span)),0,nx-1),y1=std::clamp(int(std::floor((y+h/zoom)/span)),0,ny-1);
    // If supersampling would exceed the cache, preserve logical-pixel detail
    // first. Never cycle visible chunks through an undersized cache each frame.
    const size_t limit=room(bytes_);
    while(level_<5 && (1.f/float(2<<level_))>=zoom &&
          size_t(x1-x0+1)*(y1-y0+1)*size_t((span>>level_)+2)*((span>>level_)+2)*4>limit)
        ++level_;
    for(int cy=y0;cy<=y1;++cy)for(int cx=x0;cx<=x1;++cx) {
        Key key{level_,cx,cy};visible_.push_back(key);
        if(auto i=cache_.find(key);i!=cache_.end())i->second.used=frame_;
    }
    // A lowered GPU limit takes effect now: give back off-screen chunks until the
    // total is under the cap again, rather than waiting for the next upload.
    if(gpuvram::overCap())shed(0);
    if(linear!=linear_)for(auto& [key,e]:cache_)
        SDL_SetTextureScaleMode(e.texture,linear?SDL_ScaleModeLinear:SDL_ScaleModeNearest);
    linear_=linear;
    // Bound render-thread uploads; stale jobs are discarded without SDL work.
    size_t uploaded=0;
    for(int n=0;n<64 && uploaded<(size_t(8)<<20);++n) {
        Job job;
        {std::lock_guard lock(mutex_);
            if(done_.empty() || (uploaded && uploaded+done_.front().pixels.size()>(size_t(8)<<20)))break;
            job=std::move(done_.front());done_.pop_front();
        }
        cv_.notify_all();
        ++jobsDiscarded_;   // until it is actually uploaded below
        if(job.cancelled->load(std::memory_order_relaxed))continue;
        pending_.erase(job.key);
        if(job.pixels.empty() || std::find(visible_.begin(),visible_.end(),job.key)==visible_.end())continue;
        const size_t size=job.pixels.size();
        while(bytes_+size>budget || !gpuvram::wouldFit(size)) {
            auto oldest=cache_.end();
            for(auto i=cache_.begin();i!=cache_.end();++i)
                if(i->second.used!=frame_ && (oldest==cache_.end() || i->second.used<oldest->second.used))oldest=i;
            if(oldest==cache_.end())break;
            bytes_-=size_t(oldest->second.w+2)*(oldest->second.h+2)*4;
            gpuvram::destroy(oldest->second.texture);cache_.erase(oldest);++revision_;
        }
        if(bytes_+size>budget || !gpuvram::wouldFit(size) || gpuvram::blocked())continue;
        const uint64_t uploadStart=nowNs();
        auto* texture=gpuvram::create(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,job.w+2,job.h+2);
        if(!texture){gpuvram::noteFail();continue;}
        if(SDL_UpdateTexture(texture,nullptr,job.pixels.data(),(job.w+2)*4)!=0){gpuvram::destroy(texture);continue;}
        SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture,linear?SDL_ScaleModeLinear:SDL_ScaleModeNearest);
        uploadNs_+=nowNs()-uploadStart;   // CPU submission only: no GPU synchronization
        render::countTextureUpload(size);
        cache_.emplace(job.key,Entry{texture,job.w,job.h,frame_});
        bytes_+=size;uploaded+=size;++uploads_;++revision_;
        --jobsDiscarded_;++jobsUsed_;uploadBytes_+=size;
    }
    for(const auto& key:visible_) {
        if(cache_.count(key) || pending_.count(key) || pending_.size()>=64 || gpuvram::blocked())continue;
        const auto [level,cx,cy]=key;Job job;job.key=key;job.epoch=epoch_;job.stockTerrain=map.stockTerrain;
        job.mapW=map.blocksX*(32>>level);job.mapH=map.blocksY*(32>>level);
        job.w=std::min(span>>level,job.mapW-(cx*span>>level));
        job.h=std::min(span>>level,job.mapH-(cy*span>>level));
        const size_t imageBytes=size_t(job.w+2)*(job.h+2)*4;
        if((bytes_+imageBytes>budget || !gpuvram::wouldFit(imageBytes)) &&
           std::none_of(cache_.begin(),cache_.end(),[&](const auto& entry){return entry.second.used!=frame_;}))
            continue; // wait for space instead of repeatedly composing unusable images

        job.bx=std::max(0,cx*32-1);job.by=std::max(0,cy*32-1);
        job.bw=std::min(map.blocksX,(cx+1)*32+1)-job.bx;
        job.bh=std::min(map.blocksY,(cy+1)*32+1)-job.by;
        job.tiles.reserve(size_t(job.bw)*job.bh);
        for(int by=job.by;by<job.by+job.bh;++by)for(int bx=job.bx;bx<job.bx+job.bw;++bx) {
            const auto at=size_t(by)*map.blocksX+bx;
            job.tiles.push_back({map.tileKeys[at],map.tileCols[at],map.tileRows[at]});
        }
        job.cancelled=std::make_shared<std::atomic_bool>(false);
        pending_.emplace(key,job.cancelled);++jobsQueued_;
        {std::lock_guard lock(mutex_);jobs_.push_back(std::move(job));}cv_.notify_all();
    }
}
size_t TerrainChunks::shed(size_t need) {
    size_t freed=0;
    while(!gpuvram::wouldFit(need)) {
        auto oldest=cache_.end();
        for(auto i=cache_.begin();i!=cache_.end();++i)
            if(i->second.used!=frame_ && (oldest==cache_.end() || i->second.used<oldest->second.used))oldest=i;
        if(oldest==cache_.end())break;
        const size_t size=size_t(oldest->second.w+2)*(oldest->second.h+2)*4;
        bytes_-=size;freed+=size;
        gpuvram::destroy(oldest->second.texture);cache_.erase(oldest);++revision_;
    }
    return freed;
}
bool TerrainChunks::covers(int bx,int by) const {
    return std::binary_search(drawn_.begin(),drawn_.end(),std::pair{bx/32,by/32});
}
bool TerrainChunks::draw(SDL_Renderer* renderer,float x,float y,float zoom) {
    bool complete=true;drawn_.clear();
    for(const auto& key:visible_) {
        auto i=cache_.find(key);
        const auto [want,cx,cy]=key;
        if(i==cache_.end()) {
            complete=false;
            // Still baking at this level (a zoom just changed it, or a pan brought it
            // in): stand in with the same map area at the nearest level still cached,
            // a little softer or sharper for a few frames, rather than sending the
            // whole view to the section fallback. Not stamped as used, so it never
            // holds room the real level needs.
            for(int d=1;d<=5 && i==cache_.end();++d)
                for(int level:{want-d,want+d})
                    if(level>=0 && level<=5 && i==cache_.end())i=cache_.find({level,cx,cy});
            if(i==cache_.end())continue;
        }
        const int level=std::get<0>(i->first);const auto& e=i->second;
        // Shared rounded boundaries prevent cracks. The one-texel border lets
        // filtering sample neighbouring terrain at the edges of this crop.
        const float x0=std::round((cx*span-x)*zoom),y0=std::round((cy*span-y)*zoom);
        const float x1=std::round((cx*span+(e.w<<level)-x)*zoom),y1=std::round((cy*span+(e.h<<level)-y)*zoom);
        const SDL_Rect source{1,1,e.w,e.h};const SDL_FRect dest{x0,y0,x1-x0,y1-y0};
        render::countLogicalDraw();
        if(SDL_RenderCopyF(renderer,e.texture,&source,&dest)!=0) {complete=false;continue;}
        drawn_.push_back({cx,cy});
    }
    std::sort(drawn_.begin(),drawn_.end());
    return complete && !visible_.empty();
}
}