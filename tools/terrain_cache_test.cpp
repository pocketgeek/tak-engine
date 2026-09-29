#include "client/mapview.h"
#include "client/gpuvram.h"
#include "hpi/hpi.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
static void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv) try {
    if(argc!=2 && argc!=3)return 2;
    SDL_SetMainReady();check(SDL_Init(SDL_INIT_VIDEO)==0,SDL_GetError());
    auto* window=SDL_CreateWindow("terrain chunks",0,0,640,480,SDL_WINDOW_HIDDEN);
    auto* renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_TARGETTEXTURE);
    if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    check(renderer,SDL_GetError());
    const auto baseline=gpuvram::bytes();
    {
        auto vfs=tak::hpi::mountRetailRoot(argv[1]);
        MapView map(renderer,vfs,tak::hpi::findMap(vfs,argc==3 ? argv[2] : "Ulasem Arena"));map.finishChunks();
        auto* target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,1280,960);
        check(target,SDL_GetError());SDL_SetRenderTarget(renderer,target);
        auto draw=[&] {
            SDL_SetRenderDrawColor(renderer,70,80,90,255);SDL_RenderClear(renderer);
            map.draw(610,450);
            check(SDL_GetRenderTarget(renderer)==target,"render target unchanged");
        };
        auto warm=[&] {
            const auto start=SDL_GetTicks();
            do {
                draw();const auto stats=map.chunkStats();
                if(stats.visible && stats.visible==stats.ready)return;
                check(SDL_GetTicks()-start<60000,"terrain chunks failed to finish");SDL_Delay(1);
            }while(true);
        };
        auto read=[&] {std::vector<unsigned char> pixels(1280*960*4);
            check(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA32,pixels.data(),1280*4)==0,SDL_GetError());return pixels;};
        for(float scale:{1.f,1.5f,2.f}) {
            SDL_RenderSetScale(renderer,scale,scale);
            SDL_Rect viewport{4,6,620,460},clip{2,3,601,441};
            SDL_RenderSetViewport(renderer,&viewport);SDL_RenderSetClipRect(renderer,&clip);
            map.setZoom(.05f);map.setOffset(300,300);warm();
            const auto first=read();const auto wideUploads=map.chunkStats().uploads;
            for(int frame=0;frame<6;++frame) {map.setOffset(300+frame,300+frame);draw();}
            check(map.chunkStats().uploads==wideUploads,"panning reuses map-space images");
            map.setOffset(300,300);draw();check(read()==first,"returning camera restores image");
            map.setZoom(1);warm();const auto bothUploads=map.chunkStats().uploads;
            map.setZoom(.05f);draw();check(map.chunkStats().uploads==bothUploads,"returning to a cached resolution does not rebuild");
            map.setBilinear(true);draw();check(map.chunkStats().uploads==bothUploads,"filter changes reuse chunk pixels");
            map.setBilinear(false);draw();check(read()==first,"filter reset restores nearest image");
            SDL_Rect actual{};SDL_RenderGetViewport(renderer,&actual);
            check(actual.x==viewport.x && actual.y==viewport.y && actual.w==viewport.w && actual.h==viewport.h,"viewport unchanged");
            SDL_RenderGetClipRect(renderer,&actual);
            check(SDL_RenderIsClipEnabled(renderer) && actual.x==clip.x && actual.w==clip.w && actual.y==clip.y && actual.h==clip.h,"clip unchanged");
            float sx,sy;SDL_RenderGetScale(renderer,&sx,&sy);check(sx==scale && sy==scale,"AA scale unchanged");
            map.invalidateRenderTargets();check(map.chunkStats().bytes==0,"reset frees images");warm();check(read()==first,"reset recreates same pixels");
        }
        // Full-detail texels, including both sides of a chunk boundary, match
        // the independent CPU terrain compositor. This catches crop/gutter seams.
        SDL_RenderSetScale(renderer,1,1);SDL_RenderSetViewport(renderer,nullptr);
        SDL_Rect terrainClip{0,0,610,450};SDL_RenderSetClipRect(renderer,&terrainClip);
        map.setZoom(1);map.setOffset(800,800);warm();const auto full=read();
        for(int y:{31,32,223,224,225,440})for(int x:{31,32,223,224,225,600}) {
            const int wx=800+x,wy=800+y;
            std::vector<uint8_t> block(32*32*4);
            map.compositor().renderBlock(map.map(),wx/32,wy/32,block,32,0,0);
            const size_t from=(size_t(wy%32)*32+wx%32)*4,to=(size_t(y)*1280+x)*4;
            for(int c=0;c<4;++c)check(full[to+c]==block[from+c],"full-resolution terrain texel or chunk seam mismatch");
        }
        auto& edited=map.editMap();
        const auto before=read();
        for(auto& col:edited.tileCols)col^=1;
        map.tilesEdited();check(map.chunkStats().images==0,"terrain edits invalidate images");warm();
        check(read()!=before,"edited terrain appears in rebuilt chunks");
        check(map.chunkStats().bytes<=(size_t(256)<<20),"chunk memory budget");
        // Reload while chunk composition may still be reading the old source.
        // The section decoder and chunk worker must both relinquish its images.
        for(int i=0;i<4;++i) {
            map.invalidateRenderTargets();map.setZoom(.1f);draw();
            map.reload(vfs,tak::hpi::findMap(vfs,i%2 ? "Ulasem Arena" : "Cairbray Coast Landing"));
            check(map.chunkStats().bytes==0,"reload frees old chunk images");
        }
        warm();
        SDL_SetRenderTarget(renderer,nullptr);SDL_DestroyTexture(target);
    }
    check(gpuvram::bytes()==baseline,"terrain teardown frees textures");
    SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
    std::puts("PASS: terrain chunk pan/zoom reuse, filtering, AA, edits, reset and cleanup");return 0;
} catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
