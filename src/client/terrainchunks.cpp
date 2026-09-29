#include "client/terrainchunks.h"
#include "client/gpuvram.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace tak {
namespace {constexpr size_t budget=size_t(256)<<20;}
TerrainChunks::TerrainChunks(terrain::Compositor& c):compositor_(c) {worker_=std::thread([this]{work();});}
TerrainChunks::~TerrainChunks() {
    {std::lock_guard lock(mutex_);stop_=true;}
    cv_.notify_all();worker_.join();clear();
}
void TerrainChunks::clear() {
    {std::lock_guard lock(mutex_);++epoch_;jobs_.clear();done_.clear();}
    cv_.notify_all();pending_.clear();visible_.clear();
    for(auto& [key,e]:cache_)gpuvram::destroy(e.texture);
    cache_.clear();bytes_=0;++revision_;
}
void TerrainChunks::resetSource() {
    clear();
    std::unique_lock lock(mutex_);
    cv_.wait(lock,[&]{return !busy_;});
    mips_.clear();
}
const jpeg::Image& TerrainChunks::mip(uint32_t key,int level,bool stockTerrain) {
    if(!level)return compositor_.sectionImage(key,stockTerrain);
    const auto id=std::tuple{key,level,stockTerrain};
    if(auto i=mips_.find(id);i!=mips_.end())return i->second;
    const auto& src=mip(key,level-1,stockTerrain);
    jpeg::Image out;out.width=std::max(1,src.width/2);out.height=std::max(1,src.height/2);
    out.rgba.resize(size_t(out.width)*out.height*4);
    for(int y=0;y<out.height;++y)for(int x=0;x<out.width;++x)for(int c=0;c<4;++c) {
        unsigned sum=0;
        for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
            sum+=src.rgba[(size_t(std::min(y*2+dy,src.height-1))*src.width+std::min(x*2+dx,src.width-1))*4+c];
        out.rgba[(size_t(y)*out.width+x)*4+c]=uint8_t((sum+2)/4);
    }
    return mips_.emplace(id,std::move(out)).first->second;
}
void TerrainChunks::compose(Job& j) {
    const auto [level,cx,cy]=j.key;const int tile=32>>level;
    std::vector<const jpeg::Image*> images;images.reserve(j.tiles.size());
    std::map<uint32_t,const jpeg::Image*> decoded;
    for(const auto& t:j.tiles) {
        auto [entry,added]=decoded.try_emplace(t.key,nullptr);
        if(added)try {entry->second=&mip(t.key,level,j.stockTerrain);}catch(...) {}
        images.push_back(entry->second);
    }
    j.pixels.resize(size_t(j.w+2)*(j.h+2)*4);
    // A texel of real neighbouring terrain on every side prevents filtering
    // seams. At the map boundary repeat its edge, rather than wrapping a tile.
    for(int y=-1;y<=j.h;++y)for(int x=-1;x<=j.w;++x) {
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
void TerrainChunks::work() {
    std::unique_lock lock(mutex_);
    for(;;) {
        cv_.wait(lock,[&]{return stop_ || (!jobs_.empty() && done_.size()<8);});
        if(stop_)return;
        auto job=std::move(jobs_.front());jobs_.pop_front();busy_=true;lock.unlock();
        try {compose(job);}catch(...) {job.pixels.clear();}
        lock.lock();if(job.epoch==epoch_)done_.push_back(std::move(job));
        busy_=false;cv_.notify_all();
    }
}
void TerrainChunks::prepare(SDL_Renderer* renderer,const tnt::Map& map,float x,float y,float zoom,int w,int h,bool linear) {
    ++frame_;visible_.clear();if(map.blocksX<=0 || map.blocksY<=0 || zoom<=0)return;
    float sx=1,sy=1;SDL_RenderGetScale(renderer,&sx,&sy);
    level_=0;
    const float density=zoom*std::max(sx,sy);
    while(level_<5 && (1.f/float(2<<(level_)))>=density)++level_;
    const int nx=(map.blocksX*32+span-1)/span,ny=(map.blocksY*32+span-1)/span;
    const int x0=std::clamp(int(std::floor(x/span)),0,nx-1),y0=std::clamp(int(std::floor(y/span)),0,ny-1);
    const int x1=std::clamp(int(std::floor((x+w/zoom)/span)),0,nx-1),y1=std::clamp(int(std::floor((y+h/zoom)/span)),0,ny-1);
    // If supersampling would exceed the cache, preserve logical-pixel detail
    // first. Never cycle visible chunks through an undersized cache each frame.
    while(level_<5 && (1.f/float(2<<level_))>=zoom &&
          size_t(x1-x0+1)*(y1-y0+1)*size_t((span>>level_)+2)*((span>>level_)+2)*4>budget)
        ++level_;
    for(int cy=y0;cy<=y1;++cy)for(int cx=x0;cx<=x1;++cx) {
        Key key{level_,cx,cy};visible_.push_back(key);
        if(auto i=cache_.find(key);i!=cache_.end())i->second.used=frame_;
    }
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
        cv_.notify_all();pending_.erase(job.key);
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
        auto* texture=gpuvram::create(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,job.w+2,job.h+2);
        if(!texture){gpuvram::noteFail();continue;}
        if(SDL_UpdateTexture(texture,nullptr,job.pixels.data(),(job.w+2)*4)!=0){gpuvram::destroy(texture);continue;}
        SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture,linear?SDL_ScaleModeLinear:SDL_ScaleModeNearest);
        cache_.emplace(job.key,Entry{texture,job.w,job.h,frame_});
        bytes_+=size;uploaded+=size;++uploads_;++revision_;
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
        pending_.insert(key);
        {std::lock_guard lock(mutex_);jobs_.push_back(std::move(job));}cv_.notify_all();
    }
}
bool TerrainChunks::covers(int bx,int by) const {return cache_.count({level_,bx/32,by/32});}
bool TerrainChunks::draw(SDL_Renderer* renderer,float x,float y,float zoom) {
    bool complete=true;
    for(const auto& key:visible_) {
        const auto i=cache_.find(key);if(i==cache_.end()){complete=false;continue;}
        const auto [level,cx,cy]=key;const auto& e=i->second;
        // Shared rounded boundaries prevent cracks. The one-texel border lets
        // filtering sample neighbouring terrain at the edges of this crop.
        const float x0=std::round((cx*span-x)*zoom),y0=std::round((cy*span-y)*zoom);
        const float x1=std::round((cx*span+(e.w<<level)-x)*zoom),y1=std::round((cy*span+(e.h<<level)-y)*zoom);
        const SDL_Rect source{1,1,e.w,e.h};const SDL_FRect dest{x0,y0,x1-x0,y1-y0};
        if(SDL_RenderCopyF(renderer,e.texture,&source,&dest)!=0)complete=false;
    }
    return complete && !visible_.empty();
}
}
