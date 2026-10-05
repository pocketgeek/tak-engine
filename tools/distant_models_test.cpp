#include "client/distantmodels.h"
#include "client/shadowcomposite.h"
#include "client/geometrysubmit.h"
#include <algorithm>
#include "client/gpuvram.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <cstdlib>
#include <cmath>
static void check(bool v,const char* message) {if(!v)throw std::runtime_error(message);}
int main() try {
    SDL_SetMainReady();
    check(SDL_Init(SDL_INIT_VIDEO)==0,SDL_GetError());
    auto* window=SDL_CreateWindow("distant model test",0,0,96,96,SDL_WINDOW_HIDDEN);
    check(window,SDL_GetError());
    auto* renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_TARGETTEXTURE);
    if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    check(renderer,SDL_GetError());
    auto* texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,2,2);
    check(texture,SDL_GetError());
    const unsigned char texels[]={255,0,0,128, 0,255,0,255, 0,0,255,0, 255,255,255,255};
    SDL_UpdateTexture(texture,nullptr,texels,8);SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
    std::vector<SDL_Vertex> vertices;
    for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
        SDL_Vertex q[4];
        for(int i=0;i<4;++i) {
            const int dx=i==1 || i==2,dy=i>=2;
            q[i]={{20.f+2*(x+dx),20.f+2*(y+dy)},{255,255,255,255},{float(x+dx)/8,float(y+dy)/8}};
        }
        for(int i:{0,1,2,0,2,3})vertices.push_back(q[i]);
    }
    std::pair<SDL_Texture*,int> run{texture,int(vertices.size())};
    tak::DistantModels cache;
    tak::DistantModels::Item item;item.id=1;item.revision=1;item.x=20;item.y=20;
    item.zoom=.25;item.eligible=true;item.source=vertices;item.runs={&run,1};
    auto read=[&] {std::vector<unsigned char> p(96*96*4);check(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA32,p.data(),96*4)==0,SDL_GetError());return p;};
    auto background=[&] {SDL_SetRenderDrawColor(renderer,80,90,100,255);SDL_RenderClear(renderer);};
    background();SDL_RenderGeometry(renderer,texture,vertices.data(),int(vertices.size()),nullptr,0);
    const auto reference=read();
    cache.prepare(renderer,{&item,1},100);
    if(!item.texture) {
        check(!cache.used,"unsupported renderer must retain full geometry");
        if(std::getenv("TAK_REQUIRE_DISTANT_GPU"))throw std::runtime_error("accelerated distant path unavailable");
        std::puts("PASS: unsupported renderer keeps full geometry (GPU comparisons skipped)");
    } else {
        background();SDL_RenderGeometry(renderer,item.texture,item.quad.data(),6,nullptr,0);
        auto actual=read();int worst=0;
        for(size_t i=0;i<actual.size();++i)worst=std::max(worst,std::abs(int(actual[i])-int(reference[i])));
        check(worst<=2,"cached cutout/translucent texels must not darken or lose coverage");
        // Camera translation changes placement immediately, without rebaking the pose.
        const float oldX=item.quad[0].position.x;
        item.x+=4;item.revision=2;
        for(auto& v:vertices)v.position.x+=4;
        cache.prepare(renderer,{&item,1},120);
        check(item.texture && cache.refreshed==0 && item.quad[0].position.x==oldX+4,"smooth translation between pose refreshes");
        cache.prepare(renderer,{&item,1},200);
        check(item.texture && cache.refreshed==1,"changed poses refresh after bounded interval");
        item.textureTick=1;
        cache.prepare(renderer,{&item,1},220);
        check(item.texture && cache.refreshed==0,"texture tick bypassed image refresh interval");
        cache.prepare(renderer,{&item,1},280);
        check(item.texture && cache.refreshed==1,"texture tick must refresh even when geometry revision holds");
        auto relocated=vertices;
        item.source=relocated;
        cache.prepare(renderer,{&item,1},380);
        check(item.texture && cache.refreshed==1,"geometry slot changes invalidate equal revision numbers");
        SDL_RenderSetScale(renderer,2,2);
        cache.prepare(renderer,{&item,1},400);
        float sx=0,sy=0;SDL_RenderGetScale(renderer,&sx,&sy);
        check(item.texture && cache.refreshed==1 && sx==2 && sy==2,"output scale change refreshes and restores renderer state");
        SDL_RenderSetScale(renderer,1,1);
        item.zoom=1;cache.prepare(renderer,{&item,1},400);
        check(!item.texture,"zooming in restores full geometry immediately");
        item.zoom=.25;item.eligible=false;cache.prepare(renderer,{&item,1},500);
        check(!item.texture,"special render paths must bypass sprites");
        std::printf("PASS: GPU sprite transparency, camera, refresh and zoom (max channel error %d)\n",worst);
        item.eligible=true;
        cache.prepare(renderer,{&item,1},520);
        check(cache.bytes()>0 && cache.used==1 && cache.refreshed==1,
              "AA transition fixture must contain a live distant page and work counters");
        cache.clear();
        check(gpuvram::bytes()==0 && cache.used==0 && cache.refreshed==0 &&
              cache.bakeVertices==0 && cache.bakeDraws==0 && cache.targetSwitches==0,
              "disabling distant images releases AA budget and clears work counters");
        cache.prepare(renderer,{&item,1},550);
        check(item.texture && cache.used==1 && cache.refreshed==1,
              "returning from model AA can rebuild native distant images");
        for(int frame=0;frame<180;++frame)cache.prepare(renderer,{},500);
        check(gpuvram::bytes()==0,"unused distant pages expire without another eligible model");
    }
    // Verify the actual shadow-batch quad builder against SDL's former copy
    // path, including texture alpha and independently overlapping silhouettes.
    SDL_SetTextureColorMod(texture,0,0,0);SDL_SetTextureAlphaMod(texture,127);
    const SDL_Rect src{0,0,2,2};
    const SDL_FRect first{20,20,16,16},second{28,24,16,16};
    background();
    SDL_RenderCopyF(renderer,texture,&src,&first);SDL_RenderCopyF(renderer,texture,&src,&second);
    const auto shadows=read();
    std::vector<SDL_Vertex> shadowBatch;
    tak::appendShadowComposite(shadowBatch,src,first,2,2,127);
    tak::appendShadowComposite(shadowBatch,src,second,2,2,127);
    // Degenerate padding exercises the direct-submit threshold without
    // saturating the overlapping shadows to black and hiding blend errors.
    shadowBatch.resize(600);
    background();tak::GeometrySubmit submit;submit.draw(renderer,texture,shadowBatch);
    const auto batched=read();int shadowError=0;
    for(size_t i=0;i<shadows.size();++i)shadowError=std::max(shadowError,std::abs(int(shadows[i])-int(batched[i])));
    check(shadowError<=2,"batched shadows preserve modulation and overlapping coverage");
    std::printf("PASS: shadow composites (max channel error %d)\n",shadowError);
    cache.clear();check(gpuvram::bytes()==0,"sprite atlas teardown releases GPU budget");
    SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return 0;
} catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
