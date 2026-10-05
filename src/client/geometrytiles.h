#pragma once
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

namespace tak {
// Exact bounds for an existing contiguous model/material range. This is only
// an acceleration index: retained vertices, painter order and rasterization
// are unchanged. Coordinates are the same final-output pixels as the source.
struct GeometryTileRange {
    size_t first=0,count=0;
    SDL_FRect bounds{};
};

inline SDL_FRect geometryTileBounds(std::span<const GeometryTileRange> ranges) {
    if(ranges.empty())return {};
    float x=ranges.front().bounds.x,y=ranges.front().bounds.y;
    float right=x+ranges.front().bounds.w,bottom=y+ranges.front().bounds.h;
    for(const auto& range:ranges) {
        const auto& b=range.bounds;
        x=std::min(x,b.x);y=std::min(y,b.y);
        right=std::max(right,b.x+b.w);bottom=std::max(bottom,b.y+b.h);
    }
    return {x,y,right-x,bottom-y};
}

inline std::span<const SDL_Vertex> translatedGeometryTile(
        std::span<const SDL_Vertex> vertices,std::span<const GeometryTileRange> ranges,
        SDL_FPoint offset,SDL_Rect tile,std::vector<SDL_Vertex>& scratch) {
    scratch.clear();
    const float right=float(tile.x+tile.w),bottom=float(tile.y+tile.h);
    const auto append=[&](std::span<const SDL_Vertex> source) {
        const size_t start=scratch.size();
        scratch.insert(scratch.end(),source.begin(),source.end());
        for(size_t i=start;i<scratch.size();++i) {
            scratch[i].position.x+=offset.x;scratch[i].position.y+=offset.y;
        }
    };
    for(const auto& range:ranges) {
        const auto& b=range.bounds;
        // Rectangle width/height subtraction may lose low bits, especially
        // across zero. Expand them before reconstructing the far edge, then
        // round outward again. Never reject a triangle retained by the
        // original per-triangle test.
        const float far=std::numeric_limits<float>::infinity();
        const float bx=std::nextafter(b.x,-far),by=std::nextafter(b.y,-far);
        const float br=std::nextafter(b.x+std::nextafter(b.w,far),far);
        const float bb=std::nextafter(b.y+std::nextafter(b.h,far),far);
        if(br<tile.x || bb<tile.y || bx>right || by>bottom)continue;
        const auto source=vertices.subspan(range.first,range.count);
        if(bx>=tile.x && by>=tile.y && br<=right && bb<=bottom) {
            append(source);continue;
        }
        // Only models crossing a tile edge need per-triangle rejection. Use
        // the same inclusive comparisons as the original submission path.
        for(size_t i=0;i+2<source.size();i+=3) {
            const auto triangle=source.subspan(i,3);
            float x=triangle[0].position.x,y=triangle[0].position.y,r=x,d=y;
            for(const auto& v:triangle) {
                x=std::min(x,v.position.x);y=std::min(y,v.position.y);
                r=std::max(r,v.position.x);d=std::max(d,v.position.y);
            }
            if(x+(r-x)<tile.x || y+(d-y)<tile.y || x>right || y>bottom)continue;
            append(triangle);
        }
    }
    return scratch;
}
}
