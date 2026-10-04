#include "client/selectiveaa.h"
#include "client/geometrysubmit.h"
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
    for(int n:{2,4,8,16}) {
        auto p=aaModelPlan(7680,4320,n,8192,8192,SelectiveAA::modelMaxBytes);
        check(p.samples==n && p.tileW==1024 && p.bytes<(size_t(81)<<20),"large output reduces model AA");
        p=aaModelPlan(7680,4320,n,1024,1024,SelectiveAA::modelMaxBytes);
        check(p.samples==n && p.w<=1024 && p.h<=1024,"model tiles ignore backend dimensions");
        p=aaModelPlan(7680,4320,n,8192,8192,1<<20);
        check(p.samples==n && p.bytes<=1<<20,"smaller tile should precede sample fallback");
    }
    check(aaModelPlan(7680,4320,16,16,16,SelectiveAA::modelMaxBytes).samples==0,"tiny backend limit");
    check(aaModelPlan(7680,4320,16,8192,8192,1).samples==0,"tiny tile budget");
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
        terrainAA.render(r,{0,0,320,240},[&](SDL_FPoint,SDL_Rect){
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
    if(gpu) {
        // Same translucent geometry, different tile boundaries and memory caps.
        // Interior sampling must agree; tile seams must neither
        // leave a gap nor apply alpha twice. Include viewport + caller clipping.
        constexpr int width=1500,height=1100;
        SDL_SetWindowSize(window,width,height);
        SDL_Vertex wide[]={{{11.25f,21.75f},{255,255,255,128},{0,0}},
                           {{1480.25f,35.75f},{255,255,255,128},{1,0}},
                           {{1310.25f,1080.75f},{255,255,255,128},{1,1}}};
        std::vector<unsigned char> reference(width*height*4),tiled(reference.size());
        for(int n:{2,4,8,16}) {
            modelAA.configure(r,width,height,n,false);
            check(modelAA.effective==n && modelAA.tileWidth()==1024,"preferred tile unavailable");
            const auto retained=gpuvram::bytes();
            SelectiveAA tight;
            const auto cap=gpuvram::g_cap;
            gpuvram::g_cap=gpuvram::bytes()+(1<<20);
            tight.configure(r,width,height,n,false);
            gpuvram::g_cap=cap;
            check(tight.effective==n && tight.bytes()<=1<<20 && tight.tileWidth()<1024,"memory pressure reduced samples before tile size");
            auto capture=[&](SelectiveAA& aa,std::vector<unsigned char>& pixels) {
                SDL_SetRenderDrawColor(r,40,80,120,255);SDL_RenderClear(r);
                SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
                SDL_Rect vp{3,5,width-3,height-5},clip{17,29,width-50,height-70};
                SDL_RenderSetViewport(r,&vp);SDL_RenderSetClipRect(r,&clip);
                aa.geometry(r,texture,wide,3);
                SDL_Rect after;SDL_RenderGetViewport(r,&after);
                check(SDL_RectEquals(&vp,&after),"tile lost viewport");
                SDL_RenderGetClipRect(r,&after);check(SDL_RectEquals(&clip,&after),"tile lost clip");
                SDL_RenderSetViewport(r,nullptr);SDL_RenderSetClipRect(r,nullptr);
                check(SDL_RenderReadPixels(r,nullptr,SDL_PIXELFORMAT_RGBA32,pixels.data(),width*4)==0,SDL_GetError());
            };
            capture(modelAA,reference);capture(tight,tiled);
            int maxDifference=0;size_t significant=0;
            for(size_t i=0;i<reference.size();++i) {
                const int difference=std::abs(int(reference[i])-int(tiled[i]));
                maxDifference=std::max(maxDifference,difference);
                significant+=difference>3;
            }
            std::printf("tile comparison samples=%d large=%dx%d small=%dx%d max_difference=%d significant=%zu bytes=%zu/%zu\n",n,
                modelAA.tileWidth(),modelAA.tileHeight(),tight.tileWidth(),tight.tileHeight(),maxDifference,significant,modelAA.bytes(),tight.bytes());
            // SDL rounds translated float vertices to its raster grid. Changing
            // tile sizes may move a handful of silhouette coverage samples by
            // one high-resolution pixel; interior seams remain unacceptable.
            size_t interiorChanges=0;
            for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
                bool differs=false;
                for(int c=0;c<4;++c)differs|=std::abs(int(reference[(y*width+x)*4+c])-int(tiled[(y*width+x)*4+c]))>3;
                if(!differs)continue;
                float edgeDistance=1e9f;
                for(int e=0;e<3;++e) {
                    const auto a=wide[e].position,b=wide[(e+1)%3].position;
                    const float dx=b.x-a.x,dy=b.y-a.y;
                    edgeDistance=std::min(edgeDistance,std::abs(dx*(y+.5f-5-a.y)-dy*(x+.5f-3-a.x))/std::sqrt(dx*dx+dy*dy));
                }
                interiorChanges+=edgeDistance>1;
            }
            check(interiorChanges==0 && significant<=32 && maxDifference<=12,
                  "tile boundaries changed interior sampling or introduced alpha seams");
            tight.clear();check(gpuvram::bytes()==retained,"tile allocations leaked");
            modelAA.configure(r,width,height,n,false);
            check(gpuvram::bytes()==retained,"stable model targets reallocated");
            modelAA.clear();
        }
        // A moving neighbour changes a batch's bounds, not the texture of a
        // stationary face. Use packed-atlas UVs, mixed small/large submissions
        // and alpha: a solid-color triangle cannot expose this sampling flicker.
        SDL_Texture* atlases[3]{};
        std::vector<unsigned char> texels(2048*511*4);
        for(int t=0;t<3;++t) {
            atlases[t]=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,2048,511);
            check(atlases[t],SDL_GetError());
            for(size_t i=0;i<texels.size()/4;++i) {
                texels[i*4]=t==0?230:20;texels[i*4+1]=t==1?230:20;
                texels[i*4+2]=t==2?230:20;texels[i*4+3]=i%3==0?128:255;
            }
            check(SDL_UpdateTexture(atlases[t],nullptr,texels.data(),2048*4)==0,SDL_GetError());
            SDL_SetTextureBlendMode(atlases[t],SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(atlases[t],SDL_ScaleModeNearest);
        }
        GeometrySubmit submit;
        std::vector<SDL_Vertex> mesh;
        for(int n:{2,4,8,16}) {
            modelAA.configure(r,width,height,n,false);
            for(int phase=0;phase<2;++phase) {
                SDL_SetRenderDrawColor(r,40,80,120,255);SDL_RenderClear(r);
                SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
                for(int batch=0;batch<35;++batch) {
                    mesh.clear();
                    for(int i=0;i<(batch%4==0?1:100);++i) {
                        const float x=float((batch*131+i*23)%1000)+200.25f;
                        const float y=float((batch*67+i*11)%700)+200.5f;
                        const SDL_Color c{255,255,255,255};
                        const SDL_Vertex quad[]={{{x,y},c,{.12f,.21f}},{{x+20,y},c,{.125f,.21f}},
                            {{x+20,y+20},c,{.125f,.23f}},{{x,y+20},c,{.12f,.23f}}};
                        for(int v:{0,1,2,0,2,3})mesh.push_back(quad[v]);
                    }
                    modelAA.render(r,{float(phase*3),float(phase*2),1450,1050},[&](SDL_FPoint offset,SDL_Rect tile) {
                        const bool cull=modelAA.tileCulling();
                        const auto vertices=cull?modelAA.translated(mesh,offset,tile):std::span<const SDL_Vertex>(mesh);
                        check(submit.draw(r,atlases[batch%3],vertices,true,0,cull?SDL_FPoint{}:offset)==0,SDL_GetError());
                    });
                    if(batch%3==0) {
                        SDL_SetRenderDrawColor(r,128,128,128,100);
                        const SDL_Rect scenery{batch*10,10,30,25};SDL_RenderFillRect(r,&scenery);
                    }
                }
                auto& pixels=phase?tiled:reference;
                check(SDL_RenderReadPixels(r,nullptr,SDL_PIXELFORMAT_RGBA32,pixels.data(),width*4)==0,SDL_GetError());
            }
            size_t changed=0;
            for(size_t i=0;i<reference.size();++i)changed+=std::abs(int(reference[i])-int(tiled[i]))>2;
            std::printf("stationary atlas samples=%d changed_channels=%zu\n",n,changed);
            check(changed==0,"moving batch bounds flicker stationary atlas textures");
            modelAA.clear();
        }
        submit.clear();for(auto* atlas:atlases)SDL_DestroyTexture(atlas);
        modelAA.configure(r,7680,4320,16,false);
        check(modelAA.effective==16 && modelAA.bytes()<(size_t(81)<<20),"8K model AA fallback");
        modelAA.clear();
    }
    const auto cap=gpuvram::g_cap;gpuvram::g_cap=1;
    modelAA.configure(r,320,240,16,false);
    check(modelAA.effective==0 && modelAA.bytes()==0,"budget fallback reported active");
    check(!modelAA.fallbackReason().empty(),"missing fallback reason");
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
