#pragma once
#include <cstdint>
#include <vector>

namespace tak::sim::flow {
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
