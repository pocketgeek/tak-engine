#include "client/geometrytiles.h"
#include "client/selectiveaa.h"
#include <array>
#include <bit>
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}

// The former AA path independently tested every triangle in painter order.
// Keep that as a reference for the range acceleration, including its inclusive
// boundary and floating-point rectangle reconstruction behavior.
std::vector<SDL_Vertex> reference(std::span<const SDL_Vertex> vertices,
        std::span<const tak::GeometryTileRange> ranges,SDL_FPoint offset,SDL_Rect tile) {
    std::vector<SDL_Vertex> result;
    for(const auto& range:ranges)for(size_t i=0;i+2<range.count;i+=3) {
        const auto tri=vertices.subspan(range.first+i,3);
        const auto b=tak::SelectiveAA::bounds(tri);
        if(b.x+b.w<tile.x || b.y+b.h<tile.y || b.x>tile.x+tile.w || b.y>tile.y+tile.h)continue;
        for(auto v:tri) {v.position.x+=offset.x;v.position.y+=offset.y;result.push_back(v);}
    }
    return result;
}

bool same(const SDL_Vertex& a,const SDL_Vertex& b) {
    auto bits=[](float value){return std::bit_cast<uint32_t>(value);};
    return bits(a.position.x)==bits(b.position.x) && bits(a.position.y)==bits(b.position.y) &&
        a.color.r==b.color.r && a.color.g==b.color.g && a.color.b==b.color.b && a.color.a==b.color.a &&
        bits(a.tex_coord.x)==bits(b.tex_coord.x) && bits(a.tex_coord.y)==bits(b.tex_coord.y);
}
}

int main() try {
    std::vector<SDL_Vertex> vertices,scratch;
    std::vector<tak::GeometryTileRange> ranges;
    auto triangle=[&](float x,float y,float w,float h,unsigned id) {
        const SDL_Color color{uint8_t(id),uint8_t(id*17),uint8_t(id*31),uint8_t(id*47)};
        vertices.push_back({{x,y},color,{.03125f,.8125f}});
        vertices.push_back({{x+w,y+.125f},color,{.15625f,.8125f}});
        vertices.push_back({{x+w*.25f,y+h},color,{.0625f,.9375f}});
    };
    // Material runs include tiny triangles, large overlapping models, a zero-
    // area triangle, offscreen models and unreferenced storage between ranges.
    for(unsigned i=0;i<64;++i) {
        triangle(-50000,-50000,1,1,i); // not referenced by any draw range
        const size_t first=vertices.size();
        const float x=float(int(i*373%2400)-300)+.25f;
        const float y=float(int(i*197%1400)-200)+.375f;
        for(unsigned j=0;j<i%9+1;++j)
            triangle(x+float(j)*.375f,y-float(j)*.25f,
                     i%7==0?1200.f:float(j)*7.25f,i%11==0?900.f:float(j)*5.5f,i*9+j);
        ranges.push_back({first,vertices.size()-first,tak::SelectiveAA::bounds(
            std::span<const SDL_Vertex>(vertices).subspan(first))});
    }
    // Reconstructed rectangle extrema can differ from a vertex by an ULP.
    for(float edge:{-1024.f,0.f,64.f,512.f,1024.f,16384.f}) {
        const size_t first=vertices.size();
        for(float x:{std::nextafter(edge,-INFINITY),edge,std::nextafter(edge,INFINITY)})
            triangle(x,std::nextafter(edge,INFINITY),.00001f,.00003f,121);
        ranges.push_back({first,vertices.size()-first,tak::SelectiveAA::bounds(
            std::span<const SDL_Vertex>(vertices).subspan(first))});
    }
    for(float distant:{-4096.f,-32768.f})for(float tiny:{std::nextafter(0.f,1.f),.0001f,-.0001f}) {
        const size_t first=vertices.size();
        triangle(distant,-100,1,1,17);
        triangle(tiny,8,0,1,99);
        ranges.push_back({first,vertices.size()-first,tak::SelectiveAA::bounds(
            std::span<const SDL_Vertex>(vertices).subspan(first))});
    }
    size_t cases=0,retained=0;
    for(int edge:{64,128,256,512,1024})for(int samples:{2,4,8,16})
        for(int x:{-1024,-64,0,64,512,1024,2048,16384})for(int y:{-64,0,64,512,1024}) {
            const double scale=std::sqrt(double(samples));
            const SDL_FPoint offset{float(-std::floor((x-2)*scale/2)*2/scale),
                                   float(-std::floor((y-2)*scale/2)*2/scale)};
            const SDL_Rect tile{x-2,y-2,edge+4,edge+4};
            for(bool subset:{false,true}) {
                const auto selected=subset?std::span(ranges).subspan(3,17):std::span(ranges);
                const auto expected=reference(vertices,selected,offset,tile);
                const auto actual=tak::translatedGeometryTile(vertices,selected,offset,tile,scratch);
                check(actual.size()==expected.size(),"range culling changed triangle count");
                for(size_t i=0;i<actual.size();++i)
                    check(same(actual[i],expected[i]),"range culling changed painter order or vertex attributes");
                retained+=actual.size();++cases;
                const auto capacity=scratch.capacity();
                tak::translatedGeometryTile(vertices,selected,offset,tile,scratch);
                check(scratch.capacity()==capacity,"repeated tile allocated scratch storage");
            }
        }
    check(retained>10000,"fixture did not retain enough intersecting geometry");
    const auto merged=tak::geometryTileBounds(ranges);
    for(const auto& range:ranges) {
        check(merged.x<=range.bounds.x && merged.y<=range.bounds.y,"merged bounds omit near edge");
        // One ULP of rectangle reconstruction is allowed; the renderer adds a
        // two-pixel filter guard before drawing the model tile.
        check(merged.x+merged.w+.01f>=range.bounds.x+range.bounds.w &&
              merged.y+merged.h+.01f>=range.bounds.y+range.bounds.h,"merged bounds omit far edge");
    }
    check(tak::translatedGeometryTile(vertices,{}, {},{0,0,100,100},scratch).empty(),"empty range list retained geometry");
    std::printf("PASS exact AA geometry tile culling: %zu cases, %zu retained vertices\n",cases,retained);
    return 0;
} catch(const std::exception& error) {std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}
