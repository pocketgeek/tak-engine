#include "cartographer/editorfont.h"
#include "cartographer/font5x7.h"
#include <algorithm>
#include <map>
#include <vector>
namespace cart {
namespace {
#include "cartographer/fontdata.inc"
SDL_Renderer* owner=nullptr;
std::map<size_t,SDL_Texture*> pages;
SDL_Texture* pageFor(size_t page) {
    if(auto found=pages.find(page);found!=pages.end())return found->second;
    constexpr int width=256,height=512;
    std::vector<uint8_t> rgba(width*height*4,255);
    for(size_t i=3;i<rgba.size();i+=4)rgba[i]=0;
    for(size_t cell=0;cell<256 && page*256+cell<std::size(fontGlyphs);++cell) {
        const auto& glyph=fontGlyphs[page*256+cell];size_t offset=glyph.offset;int pixel=0;
        while(pixel<512) {
            const int count=fontAlpha[offset++];const uint8_t alpha=fontAlpha[offset++];
            for(int n=0;n<count;++n,++pixel) {
                const int x=int(cell%16)*16+pixel%16,y=int(cell/16)*32+pixel/16;
                rgba[(size_t(y)*width+x)*4+3]=alpha;
            }
        }
    }
    auto* texture=SDL_CreateTexture(owner,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,width,height);
    if(texture) {SDL_UpdateTexture(texture,nullptr,rgba.data(),width*4);SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(texture,SDL_ScaleModeLinear);}
    pages[page]=texture;return texture;
}
}
EditorFont::EditorFont(SDL_Renderer* renderer):renderer_(renderer) {owner=renderer;}
EditorFont::~EditorFont() {if(owner==renderer_) {for(auto [page,t]:pages)if(t)SDL_DestroyTexture(t);pages.clear();owner=nullptr;}}
bool drawEditorText(SDL_Renderer* renderer,const std::string& text,int x,int y,int scale,Uint8 r,Uint8 g,Uint8 b) {
    if(renderer!=owner)return false;
    for(size_t p=0;p<text.size();) {
        const auto code=nextGlyph(text,p);
        auto glyph=std::lower_bound(std::begin(fontGlyphs),std::end(fontGlyphs),code,[](auto glyph,uint32_t value){return glyph.code<value;});
        if(glyph==std::end(fontGlyphs) || glyph->code!=code)glyph=std::lower_bound(std::begin(fontGlyphs),std::end(fontGlyphs),uint32_t(0xfffd),[](auto glyph,uint32_t value){return glyph.code<value;});
        if(glyph==std::end(fontGlyphs)) {x+=6*scale;continue;}
        const size_t index=size_t(glyph-std::begin(fontGlyphs));
        if(auto* texture=pageFor(index/256)) {
            SDL_SetTextureColorMod(texture,r,g,b);
            SDL_Rect source{int(index%16)*16,int(index%256/16)*32,16,32};
            SDL_FRect target{float(x-scale),float(y)-4.5f*scale,8.f*scale,16.f*scale};
            SDL_RenderCopyF(renderer,texture,&source,&target);
        }
        x+=6*scale;
    }
    return true;
}
}
