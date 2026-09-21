#include "client/shadowmask.h"
#include <SDL.h>
#include <array>
#include <cstdio>

int main() {
    // A cutout quad: transparent corners, opaque centre, partial edge coverage.
    // Exercise the same MOD textured geometry submission as the unit renderer.
    if (SDL_Init(0)!=0) return 1;
    auto* surface=SDL_CreateRGBSurfaceWithFormat(0,3,1,32,SDL_PIXELFORMAT_RGBA32);
    if (!surface) return 1;
    auto* renderer=SDL_CreateSoftwareRenderer(surface);
    if (!renderer) return 1;
    const std::array<uint8_t,12> rgba={200,0,0,0, 0,200,0,255, 0,0,200,128};
    const auto pixels=tak::shadowMaskPixels(rgba,140);
    auto* texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,3,1);
    if (!renderer || !texture) return 1;
    SDL_UpdateTexture(texture,nullptr,pixels.data(),12);
    SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_MOD);
    SDL_SetRenderDrawColor(renderer,200,160,120,255);
    SDL_RenderClear(renderer);
    const SDL_Color white{255,255,255,255};
    SDL_Vertex quad[4]={{{0,0},white,{0,0}},{{3,0},white,{1,0}},
                        {{3,1},white,{1,1}},{{0,1},white,{0,1}}};
    const int indices[6]={0,1,2,0,2,3};
    if (SDL_RenderGeometry(renderer,texture,quad,4,indices,6)!=0) return 1;
    std::array<uint8_t,12> out{};
    SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA32,out.data(),12);
    bool ok=true;
    for (int c=0;c<3;++c) {
        const int background=200-40*c;
        ok &= out[c]==background; // outside the silhouette must be untouched
        ok &= std::abs(int(out[4+c])-background*140/255)<=1;
        ok &= out[8+c]>out[4+c] && out[8+c]<out[c];
    }
    SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_FreeSurface(surface);SDL_Quit();
    std::puts(ok?"PASS: cutout shadow preserves background and darkens only covered pixels":"FAIL: shadow mask compositing");
    return ok?0:1;
}
