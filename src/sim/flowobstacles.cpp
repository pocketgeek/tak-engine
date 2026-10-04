#include "flowobstacles.h"
#include <stdexcept>
#include <algorithm>

namespace tak::sim::flow {
Obstacles::Obstacles(int width,int height,size_t limit,size_t gateTileLimit):width_(width),height_(height),limit_(limit),gateTileLimit_(gateTileLimit) {
    if(!limit||limit>65535)throw std::invalid_argument("flow obstacle body limit");
    if(width<=0||height<=0||width>2048||height>2048)throw std::invalid_argument("flow obstacle dimensions");
    count_.resize(size_t(width)*height);
    if(gateTileLimit>1024)throw std::invalid_argument("flow gate tile limit");
    tileWidth_=(width+63)/64;tileCount_=tileWidth_*((height+63)/64);
    gateCounts_.resize(size_t(tileCount_)*8);
}
Obstacles::Edit Obstacles::set(int id,std::optional<Stamp> stamp) {
    if(stamp&&(stamp->automaticGateOwner < -1 || stamp->automaticGateOwner >= 8 || stamp->w<=0||stamp->h<=0||stamp->w>64||stamp->h>64||
        (!stamp->yard.empty()&&stamp->yard.size()!=size_t(stamp->w)*stamp->h)))
        throw std::invalid_argument("flow obstacle footprint");
    auto it=entries_.find(id);
    if(it==entries_.end()) {
        if(!stamp)return Edit::Unchanged;
        if(entries_.size()>=limit_)return Edit::Full;
        it=entries_.try_emplace(id).first;
    }
    auto& entry=it->second;
    if(entry.desired==stamp)return Edit::Unchanged;
    entry.desired=stamp;pending_[id]=true;return Edit::Changed;
}
void Obstacles::cell(const Stamp& s,size_t index,bool remove) {
    const int64_t x=int64_t(s.x)+int(index%s.w),z=int64_t(s.z)+int(index/s.w);
    if(x<0||z<0||x>=width_||z>=height_)return;
    if(!s.yard.empty()) {
        const char c=s.yard[index];
        if(c=='.'||(s.open&&(c=='c'||c=='C')))return;
    }
    if(s.automaticGateOwner>=0&&!s.yard.empty()&&(s.yard[index]=='c'||s.yard[index]=='C')) {
        const size_t tile=size_t(s.automaticGateOwner)*tileCount_+size_t(z/64)*tileWidth_+size_t(x/64);
        auto& plane=gateCounts_[tile];
        if(!plane&&!remove&&gateTiles_<gateTileLimit_) {
            plane=std::make_unique<GateTile>();++gateTiles_;
        }
        if(plane) {
            auto& pass=(*plane)[size_t(z%64)*64+size_t(x%64)];
            if(remove) {if(!pass)throw std::logic_error("flow gate permission underflow");--pass;}
            else {if(pass==65535)throw std::overflow_error("flow gate overlap");++pass;}
        } else if(!remove)++gateLimitHits_;
    }
    auto& count=count_[size_t(z)*width_+size_t(x)];
    if(remove) {if(!count)throw std::logic_error("flow obstacle underflow");--count;}
    else {if(count==65535)throw std::overflow_error("flow obstacle overlap");++count;}
}
size_t Obstacles::step(size_t budget) {
    size_t work=0;
    while(work<budget&&!pending_.empty()) {
        auto next=pending_.upper_bound(cursorId_);if(next==pending_.end())next=pending_.begin();
        cursorId_=next->first;auto it=entries_.find(cursorId_);auto& e=it->second;
        if(!e.started) {e.started=true;e.remove=true;e.cursor=0;e.adding=e.desired;}
        const auto& stamp=e.remove?e.installed:e.adding;
        ++work;
        if(stamp&&e.cursor<size_t(stamp->w)*stamp->h) {cell(*stamp,e.cursor++,e.remove);continue;}
        if(e.remove) {e.remove=false;e.cursor=0;continue;}
        e.installed=e.adding;e.adding.reset();e.started=false;
        if(e.installed==e.desired) {
            pending_.erase(next);
            if(!e.installed)entries_.erase(it);
        }
    }
    work_+=work;return work;
}
bool Obstacles::blocked(int x,int z,int player) const {
    if(!settled())throw std::logic_error("sampling unfinished flow obstacles");
    if(x<0||z<0||x>=width_||z>=height_)return true;
    const auto count=count_[size_t(z)*width_+x];
    if(!count)return false;
    if(player<0||player>=8)return true;
    const auto& plane=gateCounts_[size_t(player)*tileCount_+size_t(z/64)*tileWidth_+size_t(x/64)];
    return !plane||(*plane)[size_t(z%64)*64+size_t(x%64)]!=count;
}
bool Obstacles::playerSensitive(int x,int z,int w,int h) const {
    if(w<=0||h<=0)return false;
    const int x0=std::max(0,x),z0=std::max(0,z),x1=std::min(width_,x+w),z1=std::min(height_,z+h);
    if(x1<=x0||z1<=z0)return false;
    for(int player=0;player<8;++player)for(int tz=z0/64;tz<=(z1-1)/64;++tz)
        for(int tx=x0/64;tx<=(x1-1)/64;++tx)
            if(gateCounts_[size_t(player)*tileCount_+size_t(tz)*tileWidth_+tx])return true;
    return false;
}
size_t Obstacles::bytes() const {
    return sizeof(*this)+count_.capacity()*sizeof(uint16_t)+gateCounts_.capacity()*sizeof(std::unique_ptr<GateTile>)+gateTiles_*sizeof(GateTile)+entries_.size()*(sizeof(Entry)+64)+pending_.size()*64;
}
}
