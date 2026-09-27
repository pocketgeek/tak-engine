#include "client/geometrysubmit.h"
#include <array>
#include <vector>
#include <cstdio>
int main() {
    const bool accelerated=std::getenv("TAK_GEOMETRY_TEST_ACCELERATED");
    SDL_Window* window=nullptr;SDL_Surface* surface=nullptr;SDL_Renderer* renderer=nullptr;
    if(accelerated) {
        if(SDL_Init(SDL_INIT_VIDEO)!=0)return 2;
        window=SDL_CreateWindow("geometry parity",0,0,128,128,SDL_WINDOW_HIDDEN);
        if(window)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);
    } else {
        surface=SDL_CreateRGBSurfaceWithFormat(0,128,128,32,SDL_PIXELFORMAT_RGBA32);
        if(surface)renderer=SDL_CreateSoftwareRenderer(surface);
    }
    if(!renderer){std::fprintf(stderr,"%s\n",SDL_GetError());return 2;}
    SDL_RendererInfo info{};SDL_GetRendererInfo(renderer,&info);
    if(std::strcmp(info.name,"opengl")==0) {
        auto getString=reinterpret_cast<const GLubyte* (APIENTRY *)(GLenum)>(SDL_GL_GetProcAddress("glGetString"));
        if(getString)std::printf("OpenGL renderer: %s\n",reinterpret_cast<const char*>(getString(GL_RENDERER)));
    }
    auto* target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,128,128);
    auto* texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,3,2);
    if(!target || !texture)return 2;
    const uint8_t pixels[24]={240,20,10,255,20,240,10,0,10,20,240,128,
                             200,200,20,64,20,200,200,192,200,20,200,255};
    SDL_UpdateTexture(texture,nullptr,pixels,12);
    std::vector<SDL_Vertex> vertices;
    for(int i=0;i<100;++i) {
        const float x=float(i%10)*5+.25f,y=float(i/10)*5+.5f;
        const SDL_Color color{uint8_t(100+i),180,uint8_t(250-i),uint8_t(100+i)};
        SDL_Vertex quad[4]={{{x,y},color,{0,0}},{{x+4,y},color,{1,0}},
                            {{x+4,y+4},color,{1,1}},{{x,y+4},color,{0,1}}};
        for(int index:{0,1,2,0,2,3})vertices.push_back(quad[index]);
    }
    tak::GeometrySubmit submit;
    bool ok=true;int cases=0;
    for(float scale:{1.f,2.f,1.41421356f})for(bool textured:{false,true})
    for(bool clipped:{false,true})for(bool offscreen:{false,true})
    for(auto blend:{SDL_BLENDMODE_NONE,SDL_BLENDMODE_BLEND,SDL_BLENDMODE_ADD,SDL_BLENDMODE_MOD})
    for(auto filter:{SDL_ScaleModeNearest,SDL_ScaleModeLinear}) {
        std::array<uint8_t,128*128*4> expected{},actual{};
        SDL_SetTextureBlendMode(texture,blend);SDL_SetTextureScaleMode(texture,filter);
        for(bool native:{false,true}) {
            SDL_SetRenderTarget(renderer,offscreen?target:nullptr);
            SDL_RenderSetScale(renderer,1,1);SDL_RenderSetViewport(renderer,nullptr);SDL_RenderSetClipRect(renderer,nullptr);
            SDL_SetRenderDrawColor(renderer,45,70,110,255);SDL_RenderClear(renderer);
            SDL_RenderSetScale(renderer,scale,scale);
            SDL_Rect viewport{2,3,int(120/scale),int(120/scale)},clip{3,4,42,39};
            SDL_RenderSetViewport(renderer,&viewport);SDL_RenderSetClipRect(renderer,clipped?&clip:nullptr);
            SDL_SetRenderDrawBlendMode(renderer,blend);
            ok &= submit.draw(renderer,textured?texture:nullptr,vertices,native)==0;
            // A different SDL draw follows immediately, detecting leaked arrays,
            // matrix scale, color, texture, clipping and target state.
            SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
            SDL_RenderGeometry(renderer,nullptr,vertices.data()+60,6,nullptr,0);
            const SDL_FRect follow{53,7,5,6};
            SDL_RenderCopyF(renderer,texture,nullptr,&follow);
            SDL_RenderSetScale(renderer,1,1);SDL_RenderSetViewport(renderer,nullptr);SDL_RenderSetClipRect(renderer,nullptr);
            ok &= SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA32,
                native?actual.data():expected.data(),128*4)==0;
        }
        if(actual!=expected) {
            std::printf("FAIL scale=%f texture=%d clip=%d target=%d blend=%d filter=%d\n",scale,textured,clipped,offscreen,blend,filter);
            ok=false;
        }
        ++cases;
    }
    if(std::strcmp(info.name,"opengl")==0 && !submit.active())ok=false;
    std::printf("%s: %d geometry pixel/state comparisons (%s, direct=%d)\n",ok?"PASS":"FAIL",cases,info.name,submit.active());
    SDL_SetRenderTarget(renderer,nullptr);SDL_DestroyTexture(target);SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_FreeSurface(surface);SDL_Quit();
    return ok?0:1;
}
