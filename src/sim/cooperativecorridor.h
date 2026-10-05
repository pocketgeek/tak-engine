#pragma once
#include "flowfield.h"
#include <algorithm>
#include <array>
#include <utility>

namespace tak::sim::cooperative {
// Select a lane through the existing shared terrain corridor. Alternatives
// strictly descend the field. A supplied lane target permits one diagonal's
// bounded detour cost to restore lateral spacing after a narrow passage;
// a width preference cannot invent connectivity, cut a corner or add a cycle.
// Cross-tile transitions retain the portal chosen by the terrain planner.
inline flow::Cell corridorStep(const flow::Topology& topology,const flow::Field& field,
                              flow::Cell from,flow::Cell fallback,flow::Cell direction,
                              std::optional<flow::Cell> laneTarget={}) {
    using flow::Cell;
    if(!direction.x&&!direction.z)return fallback;
    if(topology.tileAt(from)!=field.tile||topology.tileAt(fallback)!=field.tile)return fallback;
    const auto index=[&](Cell at){return size_t(at.z-field.originZ)*flow::kTileSize+at.x-field.originX;};
    const uint32_t current=field.distance[index(from)];
    const uint16_t cost=topology.cost(from);
    if(!cost||current==flow::kUnreachable||!current)return fallback;
    bool restoring=false;
    if(laneTarget) {
        // A maze may lead sideways or away from the final group heading.
        // Follow that bend directly instead of continually spending detour
        // slack trying to restore a front that cannot fit around it yet.
        if(int64_t(fallback.x-from.x)*direction.x+
           int64_t(fallback.z-from.z)*direction.z<=0)return fallback;
        // A short lookahead restores a group's initial lateral spacing as soon
        // as space opens. A distant target alone leaves a squeezed column
        // compressed until it is almost at the final destination.
        const int64_t norm=int64_t(direction.x)*direction.x+int64_t(direction.z)*direction.z;
        const int64_t cross=-int64_t(direction.z)*(laneTarget->x-from.x)+
                            int64_t(direction.x)*(laneTarget->z-from.z);
        int64_t x=-int64_t(direction.z)*cross/norm,z=int64_t(direction.x)*cross/norm;
        const int64_t correction=std::max(std::abs(x),std::abs(z));
        if(correction>8){x=x*8/correction;z=z*8/correction;}
        const int scale=std::max(std::abs(direction.x),std::abs(direction.z));
        const Cell forward{int(int64_t(direction.x)*8/scale),int(int64_t(direction.z)*8/scale)};
        // Keep diagonal terrain guidance's established tie choices. Only
        // cardinal lane restoration needs the more precise comparison.
        restoring=(x||z)&&(!forward.x||!forward.z);
        direction={int(forward.x+x),int(forward.z+z)};
    }
    const auto alignment=[&](Cell at) {
        const int dx=at.x-from.x,dz=at.z-from.z;
        return std::pair{int64_t(dx)*direction.x+int64_t(dz)*direction.z,int64_t(dx&&dz?1448:1024)};
    };
    Cell selected=fallback;auto best=alignment(fallback);
    constexpr std::array<Cell,8> steps{{{0,-1},{1,0},{0,1},{-1,0},{1,-1},{1,1},{-1,1},{-1,-1}}};
    for(const Cell delta:steps) {
        const Cell at{from.x+delta.x,from.z+delta.z};
        if(topology.tileAt(at)!=field.tile||!topology.cost(at))continue;
        if(delta.x&&delta.z&&(!topology.cost({at.x,from.z})||!topology.cost({from.x,at.z})))continue;
        const uint32_t remaining=field.distance[index(at)];
        const uint64_t routeCost=uint64_t(remaining)+uint64_t(cost)*(delta.x&&delta.z?1448:1024);
        if(remaining>=current||routeCost>uint64_t(current)+uint64_t(cost)*(laneTarget?848:0))continue;
        const auto score=alignment(at);
        // A cardinal lane's outward diagonal must not round down to a tie,
        // which would leave the compressed group too narrow.
        const bool better=restoring?score.first*best.second>best.first*score.second:
            score.first*1024/score.second>best.first*1024/best.second;
        if(better){best=score;selected=at;}
    }
    return selected;
}
}
