#include "cooperativepassages.h"
#include <algorithm>
#include <limits>

namespace tak::sim::cooperative {
Passages::Passages():Passages(Limits{}) {}
Passages::Passages(Limits limits):limits_(limits) {
    limits_.probesPerTick=std::min<size_t>(limits_.probesPerTick,65536);
    limits_.probesPerCall=std::clamp<size_t>(limits_.probesPerCall,1,128);
    limits_.entries=std::min(limits_.entries,(memoryLimit-baseCharge)/entryCharge);
    limits_.maxWidth=std::clamp(limits_.maxWidth,1,64);
}
void Passages::begin(uint32_t tick) {
    if(started_&&tick_==tick)return;
    started_=true;tick_=tick;remaining_=limits_.probesPerTick;
    admissions_=(remaining_+limits_.probesPerCall-1)/limits_.probesPerCall;
    // Probe-only users reclaim abandoned jobs by inactivity. The engine's
    // independent work loop drains them instead: a delayed but live requester
    // must not lose its proof just because combat uses the delivery budget.
    for(auto at=entries_.begin();at!=entries_.end();) {
        if(!autonomous_&&!at->second.complete&&uint32_t(tick_-at->second.seen)>pendingLifetimeTicks) {
            at=entries_.erase(at);++evictions_;
        } else ++at;
    }
    if(entries_.empty())return;
    auto at=cursor_?entries_.upper_bound(*cursor_):entries_.begin();
    for(size_t visited=0;visited<entries_.size()&&admissions_;++visited) {
        if(at==entries_.end())at=entries_.begin();
        if(!at->second.complete) {
            at->second.admitted=tick_;at->second.hasAdmission=true;
            cursor_=at->first;--admissions_;
        }
        ++at;
    }
}
void Passages::advance(uint32_t tick,const std::function<const flow::Topology*(uint64_t,uint64_t)>& topology) {
    autonomous_=true;begin(tick);
    // The cache itself is capped. Admission still rotates in begin(), and
    // probe() shares both the tick budget and each entry's one-call quantum.
    for(auto at=entries_.begin();at!=entries_.end()&&remaining_;) {
        auto current=at++;
        auto& entry=current->second;
        if(entry.complete||!entry.hasAdmission||entry.admitted!=tick_||
           (entry.hasWorked&&entry.worked==tick_))continue;
        const auto& key=current->first;
        const auto* source=topology(key.profile,key.generation);
        if(!source||source->width!=key.width||source->height!=key.height) {
            entries_.erase(current);++evictions_;++admissions_;continue;
        }
        entry.worked=tick_;entry.hasWorked=true;
        advance(key,entry,*source);
        if(entry.complete)++admissions_;
    }
}
bool Passages::room() {
    if(entries_.size()<limits_.entries)return true;
    auto oldest=entries_.end();
    for(auto at=entries_.begin();at!=entries_.end();++at)
        if(at->second.complete&&(oldest==entries_.end()||at->second.used<oldest->second.used))oldest=at;
    // Never discard a partially proved strip to admit a later caller.
    if(oldest==entries_.end())return false;
    entries_.erase(oldest);++evictions_;return true;
}
Passages::Result Passages::result(const Entry& entry,bool reverse) const {
    if(!entry.complete)return {Status::Pending,{}};
    if(!entry.passage)return {};
    auto passage=*entry.passage;if(reverse)std::swap(passage.first,passage.last);
    return {Status::Found,passage};
}
void Passages::remember(const Key& key) {
    size_t at=0;while(at<recentCount_&&recent_[at]!=key)++at;
    if(at==recentCount_) {
        if(recentCount_<recent_.size())++recentCount_;
        at=recentCount_-1;
    }
    for(;at;--at)recent_[at]=recent_[at-1];
    recent_[0]=key;
}
Passages::Result Passages::probe(uint32_t tick,uint64_t profile,uint64_t generation,
        const flow::Topology& topology,int footX,int footZ,Cell anchor,Cell direction) {
    begin(tick);
    const bool alongX=direction.x!=0&&direction.z==0;
    if((!alongX&&!(direction.x==0&&direction.z!=0))||footX<1||footZ<1||
       footX>64||footZ>64||topology.width<1||topology.height<1||
       topology.width>32768||topology.height>32768||anchor.x<0||anchor.z<0||
       anchor.x>=topology.width||anchor.z>=topology.height||
       topology.tilesX!=(topology.width+63)/64||topology.tilesZ!=(topology.height+63)/64||
       topology.tiles.size()!=size_t(topology.tilesX)*size_t(topology.tilesZ))return {};
    if((alongX?footZ:footX)>limits_.maxWidth)return {};
    const Key key{profile,generation,topology.width,topology.height,footX,footZ,anchor.x,anchor.z,alongX?0:1};
    auto at=entries_.find(key);
    const bool ownPending=at!=entries_.end()&&!at->second.complete;
    bool shared=false;
    if(at==entries_.end()||ownPending)for(size_t n=0;n<recentCount_;++n) {
        const auto& candidate=recent_[n];
        if(candidate.profile!=key.profile||candidate.generation!=key.generation||
           candidate.width!=key.width||candidate.height!=key.height||candidate.footX!=key.footX||
           candidate.footZ!=key.footZ||candidate.axis!=key.axis)continue;
        const auto reusable=entries_.find(candidate);if(reusable==entries_.end())continue;
        const auto& proof=reusable->second;
        if(!proof.side||(proof.complete&&!proof.passage)||(ownPending&&!proof.complete))continue;
        const int axial=alongX?anchor.x:anchor.z,transverse=alongX?anchor.z:anchor.x;
        if(axial>=proof.first&&axial<=proof.last&&transverse>=proof.baseLow&&transverse<=proof.baseHigh) {
            at=reusable;shared=true;break;
        }
    }
    if(at==entries_.end()) {
        if(!limits_.entries||!room())return {Status::Pending,{}};
        Entry entry;entry.section=alongX?anchor.x:anchor.z;entry.across=alongX?anchor.z:anchor.x;
        entry.low=entry.high=entry.across;
        if(admissions_&&remaining_) {entry.admitted=tick_;entry.hasAdmission=true;--admissions_;cursor_=key;}
        at=entries_.emplace(key,entry).first;
    } else if(at->second.complete||shared)++hits_;
    auto& entry=at->second;entry.used=++clock_;entry.seen=tick_;remember(at->first);
    if(!entry.complete&&entry.hasAdmission&&entry.admitted==tick_&&
       (!entry.hasWorked||entry.worked!=tick_)&&remaining_) {
        entry.worked=tick_;entry.hasWorked=true;advance(at->first,entry,topology);
        if(entry.complete)++admissions_;
    }
    return result(entry,alongX?direction.x<0:direction.z<0);
}
void Passages::advance(const Key& key,Entry& entry,const flow::Topology& topology) {
    const int transverse=key.axis==0?key.footZ:key.footX;
    const int axial=key.axis==0?key.footX:key.footZ;
    const int origin=key.axis==0?key.x:key.z;
    size_t allowance=std::min(remaining_,limits_.probesPerCall);
    const auto nextSection=[&] {
        entry.cross=0;entry.low=entry.high=entry.across;
    };
    const auto endSide=[&] {
        entry.bayCells=entry.bayWidth=0;
        if(entry.side==1) {entry.side=2;entry.section=origin+1;nextSection();return;}
        // Undo footprint erosion of the rectangle's raw wall faces. Endpoints
        // are wall start and exclusive wall end; the coordinator accounts for
        // the moving body's extent when granting and releasing a permit.
        const int first=entry.first+axial-1-axial/2,last=entry.last-axial/2+1;
        const int width=entry.baseHigh-entry.baseLow+transverse;
        const int middle=entry.baseLow-transverse/2+width/2;
        if(first<last)entry.passage=Traffic::Passage{
            key.axis==0?Cell{first,middle}:Cell{middle,first},
            key.axis==0?Cell{last,middle}:Cell{middle,last},uint16_t(width),
            uint16_t(entry.extentWidth>width?entry.extentWidth:0)};
        entry.complete=true;
    };
    const auto sectionDone=[&](bool wide) {
        if(entry.side==0) {
            if(wide){entry.complete=true;return;}
            entry.baseLow=entry.low;entry.baseHigh=entry.high;
            entry.first=entry.last=entry.section;
            entry.extentWidth=entry.high-entry.low+transverse;
            entry.side=1;--entry.section;nextSection();return;
        }
        const bool same=!wide&&entry.low==entry.baseLow&&entry.high==entry.baseHigh;
        if(same) {
            // Only a resumed narrow section commits an enclosed bay to this
            // descriptor. Opposite seeds then discover identical outer exits.
            entry.extentWidth=std::max(entry.extentWidth,entry.bayWidth);
            entry.bayCells=entry.bayWidth=0;
            if(entry.side==1)entry.first=entry.section--;
            else entry.last=entry.section++;
            nextSection();return;
        }
        const bool wider=wide||(entry.low<=entry.baseLow&&entry.high>=entry.baseHigh&&
            (entry.low<entry.baseLow||entry.high>entry.baseHigh));
        if(!wider) {
            if(entry.bayCells)endSide();else entry.complete=true;
            return;
        }
        const int width=entry.high-entry.low+transverse;
        const int center=entry.low-transverse/2+width/2;
        const int baseWidth=entry.baseHigh-entry.baseLow+transverse;
        const int baseCenter=entry.baseLow-transverse/2+baseWidth/2;
        // A bounded, centered bay is still one shared bottleneck when the
        // original section resumes within sixteen raw terrain cells. Include
        // footprint erosion in that limit so mixed bodies make the same cut.
        // Open ground and shifted bays remain separate local geometry.
        if(!wide&&center==baseCenter&&entry.bayCells+axial<=16) {
            ++entry.bayCells;entry.bayWidth=std::max(entry.bayWidth,width);
            entry.section+=entry.side==1?-1:1;nextSection();return;
        }
        endSide();
    };
    while(allowance&&!entry.complete) {
        const int across=entry.cross==0?entry.across:entry.cross==1?entry.low-1:entry.high+1;
        const Cell cell=key.axis==0?Cell{entry.section,across}:Cell{across,entry.section};
        --allowance;--remaining_;++probes_;
        const int tile=topology.tileAt(cell);
        if(tile<0||size_t(tile)>=topology.tiles.size()||!topology.tiles[size_t(tile)]) {
            entry.complete=true;break; // unavailable map edge is not a proved wall/exit
        }
        const bool free=topology.cost(cell)!=0;
        if(entry.cross==0) {
            if(!free) {
                if(entry.bayCells)endSide();else entry.complete=true;
                continue; // a proved bay can end before its far wall
            }
            entry.cross=1;
        } else if(entry.cross==1) {
            if(free)--entry.low;else entry.cross=2;
        } else {
            if(free)++entry.high;else {sectionDone(false);continue;}
        }
        if(entry.high-entry.low+transverse>limits_.maxWidth)sectionDone(true);
    }
}
void Passages::invalidate(uint64_t profile) {
    for(auto at=entries_.begin();at!=entries_.end();) {
        if(at->first.profile==profile)at=entries_.erase(at);else ++at;
    }
    size_t keep=0;
    for(size_t n=0;n<recentCount_;++n)if(recent_[n].profile!=profile)recent_[keep++]=recent_[n];
    recentCount_=keep;
}
void Passages::clear() {
    entries_.clear();cursor_.reset();recentCount_=0;clock_=probes_=hits_=evictions_=0;
    tick_=0;remaining_=admissions_=0;started_=autonomous_=false;
}
size_t Passages::bytes() const {
    static_assert(sizeof(Passages)<=baseCharge);
    return baseCharge+entries_.size()*entryCharge;
}
Passages::Stats Passages::stats() const {
    Stats value{probes_,hits_,evictions_,entries_.size(),0,remaining_};
    for(const auto& [key,entry]:entries_){(void)key;value.pending+=!entry.complete;}
    return value;
}
uint64_t Passages::checksum() const {
    uint64_t hash=1469598103934665603ull;
    const auto mix=[&](uint64_t value){hash=(hash^value)*1099511628211ull;};
    const auto keyHash=[&](const Key& key) {
        mix(key.profile);mix(key.generation);mix(key.width);mix(key.height);mix(key.footX);mix(key.footZ);
        mix(key.x);mix(key.z);mix(key.axis);
    };
    mix(limits_.probesPerTick);mix(limits_.probesPerCall);mix(limits_.entries);mix(limits_.maxWidth);
    mix(clock_);mix(tick_);mix(remaining_);mix(admissions_);mix(started_);mix(autonomous_);mix(bool(cursor_));
    if(cursor_)keyHash(*cursor_);
    mix(recentCount_);for(size_t n=0;n<recentCount_;++n)keyHash(recent_[n]);
    mix(entries_.size());
    for(const auto& [key,entry]:entries_) {
        keyHash(key);mix(entry.section);mix(entry.across);mix(entry.low);mix(entry.high);
        mix(entry.baseLow);mix(entry.baseHigh);mix(entry.first);mix(entry.last);
        mix(entry.bayCells);mix(entry.bayWidth);mix(entry.extentWidth);
        mix(entry.cross);mix(entry.side);mix(entry.complete);mix(entry.used);
        mix(entry.admitted);mix(entry.worked);mix(entry.seen);mix(entry.hasAdmission);mix(entry.hasWorked);mix(bool(entry.passage));
        if(entry.passage) {mix(entry.passage->first.x);mix(entry.passage->first.z);
            mix(entry.passage->last.x);mix(entry.passage->last.z);mix(entry.passage->width);mix(entry.passage->extentWidth);}
    }
    return hash;
}
}
