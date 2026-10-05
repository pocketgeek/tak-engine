#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace tak::sim::flow {
// Proofs run in unit-update order. Rotate their starting ID only after quota
// exhaustion so an expensive low-ID obstruction cannot starve later movers.
// A wrap may defer one tick; unsaturated ticks never exclude a caller.
struct ProofBudget {
    size_t limit=8192,remaining=8192;
    int after=0,last=0;
    bool exhausted=false;
    explicit ProofBudget(size_t capacity=8192):limit(capacity),remaining(capacity) {}
    void tick() {
        after=exhausted?last:0;last=after;remaining=limit;exhausted=false;
    }
    bool accepts(int id) const {return id>after;}
    bool spend(int id,size_t cells) {
        if(!accepts(id))return false;
        if(cells>remaining){exhausted=true;return false;}
        remaining-=cells;last=std::max(last,id);
        if(!remaining)exhausted=true;
        return true;
    }
};
// Bounded supercover through footprint-anchor cells. All diagonal side cells
// are included; merely checking sampled centres would cut terrain corners.
// An interaction region can finish the ray before its occupied target centre.
template<class Cell,class Free,class Accept>
bool directRoute(Cell from,Cell to,int limit,Free free,Accept accepts) {
    if(!free(from))return false;
    if(accepts(from))return true;
    const int64_t dx=int64_t(to.x)-from.x,dz=int64_t(to.z)-from.z;
    const int64_t length=std::max(dx<0?-dx:dx,dz<0?-dz:dz);
    if(!length||length>limit)return false;
    const auto rounded=[length](int64_t value) {
        return int(value<0?-((-value+length/2)/length):(value+length/2)/length);
    };
    Cell previous=from;
    for(int64_t n=1;n<=length;++n) {
        const Cell at{from.x+rounded(dx*n),from.z+rounded(dz*n)};
        if(!free(at)||(at.x!=previous.x&&at.z!=previous.z&&
           (!free(Cell{at.x,previous.z})||!free(Cell{previous.x,at.z}))))return false;
        if(accepts(at))return true;
        previous=at;
    }
    return false;
}
// Collapse only adjacent steps on the same directed line. Every bend and
// backtrack remains: this is lossless polyline compression, not line-of-sight
// smoothing. Callers still cap the number of sampled field cells separately.
template<class Cell>
void appendRouteCorner(std::vector<Cell>& route,Cell next) {
    if(route.size()>=2) {
        const auto a=route[route.size()-2],b=route.back();
        const int64_t ax=int64_t(b.x)-a.x,az=int64_t(b.z)-a.z;
        const int64_t bx=int64_t(next.x)-b.x,bz=int64_t(next.z)-b.z;
        if(ax*bz==az*bx && ax*bx+az*bz>0) {route.back()=next;return;}
    }
    route.push_back(next);
}
}
