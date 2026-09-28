#include "client/fogupload.h"
#include <cstdio>
#include <stdexcept>
static void check(bool yes,const char* message) {if(!yes)throw std::runtime_error(message);}
int main() try {
    SDL_SetMainReady();check(SDL_Init(SDL_INIT_VIDEO)==0,SDL_GetError());
    auto* window=SDL_CreateWindow("fog uploads",0,0,257,193,SDL_WINDOW_HIDDEN);
    auto* renderer=SDL_CreateRenderer(window,-1,0);check(renderer,SDL_GetError());
    auto* texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,257,193);
    auto* target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,257,193);
    check(texture && target,SDL_GetError());
    SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE);
    tak::FogUpload upload;std::vector<uint8_t> vis(257*193,0);
    auto verify=[&](bool force=false) {
        check(upload.update(texture,vis,257,193,force),"fog upload failed");
        SDL_SetRenderTarget(renderer,target);SDL_RenderCopy(renderer,texture,nullptr,nullptr);
        std::vector<uint8_t> pixels(vis.size()*4);
        check(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA32,pixels.data(),257*4)==0,"readback failed");
        for(size_t i=0;i<vis.size();++i)
            check(!pixels[i*4] && !pixels[i*4+1] && !pixels[i*4+2] &&
                  pixels[i*4+3]==(vis[i]==2?0:vis[i]==1?110:235),"partial fog update changed incorrect pixels");
    };
    verify();check(upload.uploadedBytes()==vis.size()*4,"initial complete upload");
    const auto generation=upload.generation();
    verify();check(upload.generation()==generation,"unchanged pixels preserve mesh generation");check(upload.uploadedBytes()==0,"identical generation skips upload");
    vis[65*257+65]=2;verify();check(upload.generation()!=generation,"changed pixels advance mesh generation");check(upload.uploadedBytes()==64*64*4,"single change updates one block");
    vis[0]=1;vis.back()=2;verify();check(upload.uploadedBytes()==(64*64+1)*4,"disjoint changes and clipped edge blocks");
    for(int y=62;y<67;++y)for(int x=62;x<132;++x)vis[y*257+x]=uint8_t((x+y)%3);
    verify();check(upload.uploadedBytes()<vis.size()*4,"moving visibility stays partial");
    verify(true);check(upload.uploadedBytes()==vis.size()*4,"forced baseline upload");
    upload.clear();verify();check(upload.uploadedBytes()==vis.size()*4,"reset restores whole image");
    SDL_SetRenderTarget(renderer,nullptr);SDL_DestroyTexture(target);SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
    std::puts("PASS: fog upload reuse, dirty blocks, edges and reset");return 0;
} catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
