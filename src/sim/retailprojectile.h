#pragma once
#include "sim/retailpiecepose.h"
#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <vector>
#include <algorithm>

namespace tak::sim {
// 52a6f3: environmental part of the projectile cell test. Feature height
// is relative to the cell minimum; callers resolve multi-cell feature anchors.
inline bool retailProjectileHitsEnvironment(int32_t y,int32_t& verticalSpeed,
        uint32_t flags,uint8_t minimum,uint8_t sea,std::optional<uint8_t> featureHeight,
        bool waterPass) {
    if(flags&0x800)return false;
    const int height=std::bit_cast<int16_t>(uint16_t(uint32_t(y)>>16));
    if(featureHeight && height<=int(minimum)+*featureHeight)return true;
    if(height<=minimum) {
        if(!(flags&0x1000))return true;
        verticalSpeed=-(verticalSpeed/4);
        return false;
    }
    return !(flags&0x2000) && height<sea && !waterPass;
}

// 52a519: optional interception proximity to another projectile. Each squared
// fixed-point delta is truncated separately before the signed, wrapping sum.
inline bool retailProjectileProximity(const std::array<int32_t,3>& point,
        const std::array<int32_t,3>& target,uint16_t radius) {
    uint32_t distance=0;
    for(unsigned axis=0;axis<3;++axis) {
        const int32_t delta=std::bit_cast<int32_t>(uint32_t(point[axis])-uint32_t(target[axis]));
        distance+=uint32_t(uint64_t(int64_t(delta)*delta)>>32);
    }
    return std::bit_cast<int32_t>(distance)<std::bit_cast<int32_t>(uint32_t(radius)*radius);
}

// 506c40 / 519bd0 / 519c10: the secondary map occupant is selected from
// overlapping airborne footprints. Input order is the native player/entity order.
struct RetailAirCollisionBody {
    int id=0,x=0,z=0,width=1,height=1;
    bool airborne=true;
};
template<class Random>
std::vector<int> retailAirCollisionGrid(int width,int height,
        const std::vector<RetailAirCollisionBody>& bodies,Random random) {
    struct Links { std::array<int,7> ids{}; int count=0; };
    std::vector<Links> links(bodies.size());
    // Internal indices avoid imposing native 16-bit entity IDs on World IDs.
    std::vector<int> cells(size_t(width)*height,-1);
    auto add=[&](int a,int b) {
        auto& list=links[size_t(a)];
        if(a==b || list.count<0)return;
        if(std::find(list.ids.begin(),list.ids.begin()+list.count,b)!=list.ids.begin()+list.count)return;
        if(list.count==7)list.count=-1;
        else list.ids[size_t(list.count++)]=b;
    };
    for(size_t index=0;index<bodies.size();++index) {
        const auto& body=bodies[index];
        if(!body.airborne || body.x<0 || body.z<0 || body.x+body.width>=width || body.z+body.height>=height)continue;
        for(int z=body.z;z<body.z+body.height;++z)for(int x=body.x;x<body.x+body.width;++x) {
            int& owner=cells[size_t(z)*width+x];
            if(owner==-1) {owner=int(index);continue;}
            if(owner==-2) {links[index].count=-1;continue;}
            const int previous=owner;
            if(links[size_t(previous)].count<0) {owner=-2;links[index].count=-1;continue;}
            add(previous,int(index));add(int(index),previous);
            std::array<int,7> candidates{previous,int(index)};
            int count=2;
            for(int n=0;n<links[size_t(previous)].count;++n) {
                const int other=links[size_t(previous)].ids[size_t(n)];
                if(other==int(index))continue;
                const auto& candidate=bodies[size_t(other)];
                if(x<candidate.x || z<candidate.z || x>=candidate.x+candidate.width || z>=candidate.z+candidate.height)continue;
                if(count==7) {
                    owner=-2;links[size_t(other)].count=-1;
                    for(int c:candidates)links[size_t(c)].count=-1;
                    break;
                }
                add(other,int(index));add(int(index),other);
                candidates[size_t(count++)]=other;
            }
            if(owner!=-2)owner=candidates[size_t(random(unsigned(count)))];
        }
    }
    for(auto& cell:cells)cell=cell==-1 ? 0 : cell==-2 ? -1 : bodies[size_t(cell)].id;
    return cells;
}

using RetailCollisionQuad=std::array<std::array<int32_t,2>,4>;
// 51f340: unit base/top bounds, then the selection primitive projected into
// signed whole-word world coordinates. Edges are excluded by 549090.
inline bool retailProjectileInUnit(const std::array<int32_t,3>& point,
        const std::array<int32_t,3>& origin,int32_t modelTop,uint16_t heading,
        const RetailCollisionQuad& quad) {
    const int32_t top=std::bit_cast<int32_t>(uint32_t(origin[1])+uint32_t(modelTop));
    if(point[1]<origin[1] || point[1]>top)return false;
    auto whole=[](int32_t v) {return int32_t(std::bit_cast<int16_t>(uint16_t(uint32_t(v)>>16)));};
    RetailCollisionQuad world;
    for(unsigned i=0;i<4;++i) {
        auto vertex=quad[3-i];retailRotatePair(vertex[0],vertex[1],heading);
        for(unsigned axis=0;axis<2;++axis)
            world[i][axis]=whole(std::bit_cast<int32_t>(uint32_t(vertex[axis])+uint32_t(origin[axis*2])));
    }
    const int32_t x=whole(point[0]),z=whole(point[2]);
    auto product=[](int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)*uint32_t(b));};
    for(unsigned i=0;i<4;++i) {
        const auto& a=world[i];const auto& b=world[(i+1)%4];
        if(product(b[1]-a[1],x-a[0])<=product(z-a[1],b[0]-a[0]))return false;
    }
    return true;
}
} // namespace tak::sim
