#pragma once
#include "client/gpuvram.h"
#include <algorithm>
#include <cmath>
#include <span>
#include <string>

namespace tak {
inline int aaStep(int value,int maximum=16) {
    int result=0;for(int n:{2,4,8,16})if(n<=value && n<=maximum)result=n;return result;
}
struct AAPlan {int samples=0,w=0,h=0,mw=0,mh=0;size_t bytes=0;};
inline AAPlan aaPlan(int width,int height,int requested,int limitW,int limitH,size_t budget) {
    if(width<=0 || height<=0)return {};
    for(int n=aaStep(requested);n>=2;n/=2) {
        const double scale=std::sqrt(double(n));
        const double dw=std::ceil(width*scale),dh=std::ceil(height*scale);
        if(dw>limitW || dh>limitH)continue;
        const int w=int(dw),h=int(dh),mw=n>=8?w/2+w%2:0,mh=n>=8?h/2+h%2:0;
        const uint64_t pixels=uint64_t(w)*h+uint64_t(mw)*mh;
        if(pixels<=budget/4)return {n,w,h,mw,mh,size_t(pixels*4)};
    }
    return {};
}
// A reusable, explicitly scoped layer. Every resolve happens at its original
// painter-order boundary. No scene-wide final downsample; UI never enters here.
class SelectiveAA {
    SDL_Texture* high_=nullptr;
    SDL_Texture* middle_=nullptr;
    SDL_Renderer* renderer_=nullptr;
    AAPlan plan_;
    int width_=0,height_=0,request_=-1,logged_=-1;
    uint64_t retryAt_=0;
    bool opaque_=false;
    SDL_BlendMode composite_=SDL_BLENDMODE_NONE;
public:
    SelectiveAA()=default;
    SelectiveAA(const SelectiveAA&)=delete;
    SelectiveAA& operator=(const SelectiveAA&)=delete;
    static constexpr size_t maxBytes=256u<<20;
    int effective=0;
    uint64_t resolves=0;
    size_t bytes() const {return plan_.bytes;}
    ~SelectiveAA(){clear();}
    void clear() {
        gpuvram::destroy(high_);gpuvram::destroy(middle_);high_=middle_=nullptr;
        plan_={};effective=0;request_=-1;logged_=-1;retryAt_=0;renderer_=nullptr;
    }
    void configure(SDL_Renderer* r,int w,int h,int requested,bool opaque) {
        if(renderer_!=r || width_!=w || height_!=h || request_!=requested || opaque_!=opaque) {
            clear();renderer_=r;width_=w;height_=h;request_=requested;opaque_=opaque;
        }
        resolves=0;
        if(high_ && !gpuvram::blocked() && (effective==request_ || SDL_GetTicks64()<retryAt_))return;
        if(high_) {gpuvram::destroy(high_);gpuvram::destroy(middle_);high_=middle_=nullptr;plan_={};effective=0;}
        if(!requested || SDL_GetTicks64()<retryAt_)return;
        retryAt_=SDL_GetTicks64()+3000;
        SDL_RendererInfo info{};
        if(SDL_GetRendererInfo(r,&info)!=0 || !(info.flags&SDL_RENDERER_ACCELERATED) || !SDL_RenderTargetSupported(r) || gpuvram::blocked())return;
        composite_=opaque?SDL_BLENDMODE_NONE:SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE,
            SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,SDL_BLENDOPERATION_ADD,SDL_BLENDFACTOR_ONE,
            SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,SDL_BLENDOPERATION_ADD);
        const size_t available=gpuvram::cap()-std::min(gpuvram::bytes(),gpuvram::cap());
        auto plan=aaPlan(w,h,requested,info.max_texture_width*7/8,info.max_texture_height*7/8,std::min(maxBytes,available));
        while(plan.samples) {
            high_=gpuvram::create(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,plan.w,plan.h);
            if(high_ && plan.mw)middle_=gpuvram::create(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,plan.mw,plan.mh);
            if(high_ && (!plan.mw || middle_) &&
               SDL_SetTextureBlendMode(middle_?middle_:high_,composite_)==0 &&
               SDL_SetTextureScaleMode(high_,SDL_ScaleModeLinear)==0 &&
               SDL_SetTextureBlendMode(high_,middle_?SDL_BLENDMODE_NONE:composite_)==0 &&
               (!middle_ || SDL_SetTextureScaleMode(middle_,SDL_ScaleModeLinear)==0)) {
                plan_=plan;effective=plan.samples;break;
            }
            gpuvram::destroy(high_);gpuvram::destroy(middle_);high_=middle_=nullptr;
            plan=aaPlan(w,h,plan.samples/2,info.max_texture_width*7/8,info.max_texture_height*7/8,std::min(maxBytes,available));
        }
    }
    void report(const char* name) {
        const int code=request_*32+effective;
        if(code==logged_)return;
        logged_=code;
        std::fprintf(stderr,"AA %s: requested %dx, effective %dx, targets %.2f MiB%s\n",name,request_,effective,double(bytes())/(1<<20),request_!=effective?" (capability/size/memory fallback)":"");
    }
    static SDL_FRect bounds(std::span<const SDL_Vertex> verts) {
        if(verts.empty())return {};
        float x=verts[0].position.x,y=verts[0].position.y,r=x,b=y;
        for(const auto& v:verts){x=std::min(x,v.position.x);y=std::min(y,v.position.y);r=std::max(r,v.position.x);b=std::max(b,v.position.y);}
        return {x,y,r-x,b-y};
    }
    template<class F> void render(SDL_Renderer* r,SDL_FRect bounds,F draw) {
        if(!high_ || !effective){draw();return;}
        if(!std::isfinite(bounds.x+bounds.y+bounds.w+bounds.h)){draw();return;}
        SDL_Texture* previous=SDL_GetRenderTarget(r);
        float sx,sy;SDL_RenderGetScale(r,&sx,&sy);
        // All game coordinates are final drawable pixels. A nested/offscreen
        // caller must use its own pass instead of accidentally multiplying AA.
        if(previous || sx!=1 || sy!=1){draw();return;}
        SDL_Rect viewport,clip;SDL_RenderGetViewport(r,&viewport);SDL_RenderGetClipRect(r,&clip);
        const bool clipped=SDL_RenderIsClipEnabled(r);
        SDL_BlendMode blend;Uint8 red,green,blue,alpha;
        SDL_GetRenderDrawBlendMode(r,&blend);SDL_GetRenderDrawColor(r,&red,&green,&blue,&alpha);
        auto restore=[&]{SDL_SetRenderTarget(r,previous);SDL_RenderSetScale(r,sx,sy);SDL_RenderSetViewport(r,&viewport);SDL_RenderSetClipRect(r,clipped?&clip:nullptr);SDL_SetRenderDrawBlendMode(r,blend);SDL_SetRenderDrawColor(r,red,green,blue,alpha);};
        auto fallback=[&]{
            restore();gpuvram::destroy(high_);gpuvram::destroy(middle_);high_=middle_=nullptr;
            plan_={};effective=0;retryAt_=SDL_GetTicks64()+3000;draw();
        };
        const int left=int(std::clamp(std::floor(double(bounds.x))-2.,0.,double(width_)));
        const int top=int(std::clamp(std::floor(double(bounds.y))-2.,0.,double(height_)));
        const int right=int(std::clamp(std::ceil(double(bounds.x)+bounds.w)+2.,0.,double(width_)));
        const int bottom=int(std::clamp(std::ceil(double(bounds.y)+bounds.h)+2.,0.,double(height_)));
        SDL_Rect area{left,top,right-left,bottom-top};
        const SDL_Rect clearArea=area;
        if(clipped){SDL_Rect intersection;if(!SDL_IntersectRect(&area,&clip,&intersection))return;area=intersection;}
        if(area.w<=0 || area.h<=0)return;
        if(SDL_SetRenderTarget(r,high_)!=0){fallback();return;}
        SDL_RenderSetViewport(r,nullptr);SDL_RenderSetScale(r,float(plan_.w)/width_,float(plan_.h)/height_);
        SDL_RenderSetClipRect(r,nullptr);SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r,opaque_?18:0,opaque_?18:0,opaque_?26:0,opaque_?255:0);
        if(SDL_RenderFillRect(r,&clearArea)!=0){fallback();return;}
        // Apply the caller's clip only on the native resolve. Clipping the high
        // target at a fractional scale would attenuate the first visible pixel.
        SDL_RenderSetClipRect(r,&clearArea);SDL_SetRenderDrawBlendMode(r,blend);SDL_SetRenderDrawColor(r,red,green,blue,alpha);
        try {draw();}catch(...){restore();throw;}
        SDL_Texture* resolved=high_;
        if(middle_) {
            if(SDL_SetRenderTarget(r,middle_)!=0){fallback();return;}
            SDL_RenderSetViewport(r,nullptr);SDL_RenderSetScale(r,float(plan_.mw)/width_,float(plan_.mh)/height_);SDL_RenderSetClipRect(r,&clearArea);
            SDL_Rect full{0,0,width_,height_};
            if(SDL_RenderCopy(r,high_,nullptr,&full)!=0){fallback();return;}
            resolved=middle_;
        }
        restore();SDL_RenderSetClipRect(r,&area);
        SDL_Rect full{0,0,width_,height_};
        if(SDL_RenderCopy(r,resolved,nullptr,&full)!=0){fallback();return;}
        SDL_RenderSetClipRect(r,clipped?&clip:nullptr);++resolves;
    }
    void geometry(SDL_Renderer* r,SDL_Texture* texture,const SDL_Vertex* verts,int count) {
        render(r,effective?bounds({verts,size_t(count)}):SDL_FRect{},[&]{SDL_RenderGeometry(r,texture,verts,count,nullptr,0);});
    }
};
}
