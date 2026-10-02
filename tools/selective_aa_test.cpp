#include "client/selectiveaa.h"
#include "client/settings.h"
#include <SDL.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
using namespace tak;
static void check(bool b,const char* text){if(!b)throw std::runtime_error(text);}
int main(int argc,char** argv) try {
    const bool gpu=argc>1 && std::string(argv[1])=="--gpu";
    for(int t:{0,2,4})for(int m:{0,2,4,8,16}) {
        auto terrain=aaPlan(320,240,t,8192,8192,SelectiveAA::maxBytes);
        auto model=aaPlan(320,240,m,8192,8192,SelectiveAA::maxBytes);
        check(terrain.samples==t && model.samples==m,"AA settings multiply or incorrectly clamp");
        for(const auto& p:{terrain,model})if(p.samples)
            check(std::abs(double(p.w)*p.h/(320*240)-p.samples)<.05,"sample pixel ratio");
    }
    check(aaPlan(7680,4320,16,8192,8192,SelectiveAA::maxBytes).samples==0,"memory limit");
    check(aaPlan(320,240,16,640,480,SelectiveAA::maxBytes).samples==4,"backend size limit");
    check(aaPlan(320,240,4,8192,8192,1).samples==0,"tiny budget");
    SDL_SetMainReady();check(SDL_Init(SDL_INIT_VIDEO)==0,SDL_GetError());
    auto* window=SDL_CreateWindow("selective AA test",0,0,320,240,SDL_WINDOW_HIDDEN);
    check(window,SDL_GetError());
    auto* r=SDL_CreateRenderer(window,-1,gpu?SDL_RENDERER_ACCELERATED:SDL_RENDERER_SOFTWARE);
    check(r,SDL_GetError());SDL_RenderSetVSync(r,0);
    const auto directory=std::filesystem::temp_directory_path()/"tak-selective-aa-results";
    std::filesystem::create_directories(directory);
    auto* texture=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,2,2);
    const unsigned char pixels[]={255,0,0,255,255,0,0,255,255,0,0,255,255,0,0,255};
    SDL_UpdateTexture(texture,nullptr,pixels,8);SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
    const SDL_Vertex model[]={{{30.25f,40.25f},{255,255,255,128},{0,0}},{{270.25f,50.25f},{255,255,255,128},{1,0}},{{150.25f,195.25f},{255,255,255,128},{.5f,1}}};
    SelectiveAA terrainAA,modelAA;
    std::vector<unsigned char> readback(320*240*4);
    auto scene=[&] {
        SDL_SetRenderDrawColor(r,40,80,120,255);SDL_RenderClear(r);
        terrainAA.render(r,{0,0,320,240},[&]{
            SDL_SetRenderDrawColor(r,40,80,120,255);SDL_RenderFillRect(r,nullptr);
            SDL_SetRenderDrawColor(r,60,90,130,255);SDL_RenderDrawLine(r,0,220,320,0);
        });
        SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
        modelAA.geometry(r,texture,model,3);
        // Bitmap scenery remains between the two model draws.
        SDL_SetRenderDrawColor(r,0,255,0,255);SDL_Rect scenery{100,80,30,70};SDL_RenderFillRect(r,&scenery);
        const SDL_Vertex small[]={{{115,115},{255,255,255,255},{0,0}},{{140,115},{255,255,255,255},{1,0}},{{130,140},{255,255,255,255},{1,1}}};
        modelAA.geometry(r,texture,small,3);
        // Single-pixel UI must be unaffected by either setting.
        SDL_SetRenderDrawColor(r,255,255,255,255);SDL_Rect ui{0,0,1,240};SDL_RenderFillRect(r,&ui);
    };
    for(int t:{0,2,4})for(int m:{0,2,4,8,16}) {
        terrainAA.configure(r,320,240,t,true);modelAA.configure(r,320,240,m,false);
        check(terrainAA.effective==(gpu?t:0) && modelAA.effective==(gpu?m:0),"effective capability reporting");
        scene();check(SDL_RenderReadPixels(r,nullptr,SDL_PIXELFORMAT_RGBA32,readback.data(),320*4)==0,SDL_GetError());
        auto pixel=[&](int x,int y,int c){return readback[(y*320+x)*4+c];};
        check(pixel(0,50,0)==255 && pixel(1,50,0)==40,"UI not native resolution");
        check(pixel(110,100,1)==255 && pixel(130,120,0)>200,"model/scenery painter order");
        check(std::abs(pixel(150,90,0)-148)<=3 && std::abs(pixel(150,90,2)-60)<=3,"premultiplied translucent composition");
        // Along the sloping transparent edge, red/blue must remain on the
        // foreground/background blend line, rather than darkening towards black.
        for(int x=40;x<210;++x)for(int y=40;y<55;++y)
            check(std::abs(pixel(x,y,0)-(255.-215.*pixel(x,y,2)/120.))<5,"dark fringe at model edge");
        if(gpu) {
            auto* surface=SDL_CreateRGBSurfaceWithFormatFrom(readback.data(),320,240,32,320*4,SDL_PIXELFORMAT_RGBA32);
            SDL_SaveBMP(surface,(directory/("terrain-"+std::to_string(t)+"-model-"+std::to_string(m)+".bmp")).string().c_str());SDL_FreeSurface(surface);
        }
        auto start=std::chrono::steady_clock::now();
        for(int i=0;i<60;++i){scene();SDL_RenderPresent(r);}
        double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/60;
        std::printf("terrain=%d model=%d effective=%d/%d target_bytes=%zu frame_ms=%.3f\n",t,m,terrainAA.effective,modelAA.effective,terrainAA.bytes()+modelAA.bytes(),ms);
        const size_t retained=gpuvram::bytes();
        SDL_SetRenderDrawColor(r,40,80,120,255);SDL_RenderClear(r);
        SDL_Rect clip{100,70,30,80};SDL_RenderSetClipRect(r,&clip);
        modelAA.geometry(r,texture,model,3);
        SDL_Rect restored;SDL_RenderGetClipRect(r,&restored);
        check(SDL_RenderIsClipEnabled(r) && SDL_RectEquals(&clip,&restored),"clip state lost");
        SDL_RenderSetClipRect(r,nullptr);
        check(SDL_RenderReadPixels(r,nullptr,SDL_PIXELFORMAT_RGBA32,readback.data(),320*4)==0,SDL_GetError());
        check(pixel(99,90,0)==40 && pixel(130,90,0)==40 && std::abs(pixel(100,90,0)-148)<=3,"terrain clip/resolve edge");
        std::vector<SDL_Vertex> moved(std::begin(model),std::end(model));
        for(auto& v:moved)v.position.x+=50;
        SDL_SetRenderDrawColor(r,40,80,120,255);SDL_RenderClear(r);
        modelAA.geometry(r,texture,moved.data(),int(moved.size()));
        check(SDL_RenderReadPixels(r,nullptr,SDL_PIXELFORMAT_RGBA32,readback.data(),320*4)==0,SDL_GetError());
        check(pixel(50,50,0)==40 && pixel(200,90,0)>140,"moving geometry left stale coverage");
        check(gpuvram::bytes()==retained,"per-draw target allocation");
    }
    terrainAA.clear();modelAA.clear();
    const auto cap=gpuvram::g_cap;gpuvram::g_cap=1;
    modelAA.configure(r,320,240,16,false);
    check(modelAA.effective==0 && modelAA.bytes()==0,"budget fallback reported active");
    gpuvram::g_cap=cap;
    SDL_SetWindowSize(window,400,300);
    modelAA.configure(r,400,300,16,false);
    check(modelAA.effective==(gpu?16:0),"resize failed to recover AA");
    modelAA.clear();modelAA.configure(r,400,300,4,false);
    check(modelAA.effective==(gpu?4:0),"renderer target reset failed to recover AA");
    modelAA.clear();check(gpuvram::bytes()==0,"AA resources leaked");
    SDL_DestroyTexture(texture);SDL_DestroyRenderer(r);SDL_DestroyWindow(window);SDL_Quit();
    std::puts("PASS independent AA combinations, composition, UI, resource plans and fallback");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
