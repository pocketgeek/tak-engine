#pragma once
#include "client/gpuvram.h"
#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <vector>

namespace tak {
inline int aaStep(int value,int maximum=16) {
    int result=0;for(int n:{2,4,8,16})if(n<=value && n<=maximum)result=n;return result;
}
struct AAPlan {int samples=0,w=0,h=0,mw=0,mh=0;size_t bytes=0;int tileW=0,tileH=0;};
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
// Prefer smaller reusable model tiles before reducing sample count. A two-pixel
// guard on each side and even high-target dimensions keep both resolve stages
// on the same screen-space sample grid across tile boundaries.
inline AAPlan aaModelPlan(int width,int height,int requested,int limitW,int limitH,size_t budget) {
    if(width<=0 || height<=0)return {};
    for(int n=aaStep(requested);n>=2;n/=2) {
        const double scale=std::sqrt(double(n));
        for(int edge:{1024,512,256,128,64}) {
            const int tw=std::min(width,edge),th=std::min(height,edge);
            const int w=(int(std::ceil((tw+4)*scale))+3)&~1;
            const int h=(int(std::ceil((th+4)*scale))+3)&~1;
            const int mw=n>=8?w/2:0,mh=n>=8?h/2:0;
            const uint64_t pixels=uint64_t(w)*h+uint64_t(mw)*mh;
            if(w<=limitW && h<=limitH && pixels<=budget/4)
                return {n,w,h,mw,mh,size_t(pixels*4),tw,th};
        }
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
    int width_=0,height_=0,request_=-1;
    std::string reason_,loggedReason_;
    int loggedRequest_=-1,loggedEffective_=-1;
    size_t loggedBytes_=0;
    std::vector<SDL_Vertex> translated_;
    uint64_t retryAt_=0;
    bool opaque_=false,tileCull_=false;
    SDL_BlendMode composite_=SDL_BLENDMODE_NONE;
public:
    SelectiveAA()=default;
    SelectiveAA(const SelectiveAA&)=delete;
    SelectiveAA& operator=(const SelectiveAA&)=delete;
    static constexpr size_t maxBytes=512u<<20; // terrain ceiling, also <= half shared cap
    static constexpr size_t modelMaxBytes=128u<<20;
    const std::string& fallbackReason() const {return reason_;}
    int tileWidth() const {return plan_.tileW;}
    int tileHeight() const {return plan_.tileH;}
    bool tileCulling() const {return effective && tileCull_;}
    int effective=0;
    uint64_t resolves=0;
    size_t bytes() const {return plan_.bytes;}
    ~SelectiveAA(){clear();}
    void clear() {
        gpuvram::destroy(high_);gpuvram::destroy(middle_);high_=middle_=nullptr;
        plan_={};effective=0;request_=-1;loggedRequest_=loggedEffective_=-1;loggedBytes_=0;loggedReason_.clear();reason_.clear();retryAt_=0;renderer_=nullptr;
    }
    void configure(SDL_Renderer* r,int w,int h,int requested,bool opaque) {
        if(renderer_!=r || width_!=w || height_!=h || request_!=requested || opaque_!=opaque) {
            clear();renderer_=r;width_=w;height_=h;request_=requested;opaque_=opaque;
        }
        resolves=0;
        if(high_ && !gpuvram::blocked() && (effective==request_ || SDL_GetTicks64()<retryAt_))return;
        // Keep a working degraded target during backoff instead of discarding it
        // on every frame, or repeatedly allocating while the GPU is under pressure.
        if(SDL_GetTicks64()<retryAt_)return;
        if(high_) {gpuvram::destroy(high_);gpuvram::destroy(middle_);high_=middle_=nullptr;plan_={};effective=0;}
        reason_.clear();
        if(!requested)return;
        retryAt_=SDL_GetTicks64()+3000;
        SDL_RendererInfo info{};
        if(SDL_GetRendererInfo(r,&info)!=0){reason_="renderer information unavailable";return;}
        if(!(info.flags&SDL_RENDERER_ACCELERATED)){reason_="software renderer";return;}
        if(!SDL_RenderTargetSupported(r)){reason_="render targets unsupported";return;}
        if(gpuvram::blocked()){reason_="GPU allocation backoff";return;}
        if(w<=0 || h<=0){reason_="empty drawable";return;}
        composite_=opaque?SDL_BLENDMODE_NONE:SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE,
            SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,SDL_BLENDOPERATION_ADD,SDL_BLENDFACTOR_ONE,
            SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,SDL_BLENDOPERATION_ADD);
        const size_t available=gpuvram::cap()-std::min(gpuvram::bytes(),gpuvram::cap());
        const size_t passBudget=opaque?std::min(maxBytes,gpuvram::cap()/2):std::min(modelMaxBytes,gpuvram::cap()/4);
        const size_t budget=std::min(passBudget,available);
        auto choose=[&](int n,size_t bytes) {
            return opaque?aaPlan(w,h,n,info.max_texture_width,info.max_texture_height,bytes)
                         :aaModelPlan(w,h,n,info.max_texture_width,info.max_texture_height,bytes);
        };
        auto plan=choose(requested,budget);
        if(plan.samples!=requested) {
            const auto unlimited=choose(requested,SIZE_MAX);
            if(unlimited.samples!=requested)reason_="texture dimension limit "+std::to_string(info.max_texture_width)+"x"+std::to_string(info.max_texture_height);
            else reason_=available<passBudget?"shared texture-memory budget":"AA pass memory budget";
        }
        while(plan.samples) {
            high_=gpuvram::create(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,plan.w,plan.h);
            if(high_ && plan.mw)middle_=gpuvram::create(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,plan.mw,plan.mh);
            const bool allocated=high_ && (!plan.mw || middle_);
            if(allocated &&
               SDL_SetTextureBlendMode(middle_?middle_:high_,composite_)==0 &&
               SDL_SetTextureScaleMode(high_,SDL_ScaleModeLinear)==0 &&
               SDL_SetTextureBlendMode(high_,middle_?SDL_BLENDMODE_NONE:composite_)==0 &&
               (!middle_ || SDL_SetTextureScaleMode(middle_,SDL_ScaleModeLinear)==0)) {
                plan_=plan;effective=plan.samples;break;
            }
            reason_=(allocated?std::string("required blending/filtering unsupported: "):std::string("render-target allocation failed: "))+SDL_GetError();
            gpuvram::destroy(high_);gpuvram::destroy(middle_);high_=middle_=nullptr;
            if(allocated)break; // changing dimensions cannot add a missing blend/filter capability
            // A smaller tile can retain the requested sample level on a driver
            // that rejects the preferred allocation. Terrain still steps down.
            plan=opaque?choose(plan.samples/2,budget):choose(plan.samples,std::min(budget,plan.bytes-1));
        }
        if(effective==requested)reason_.clear();
    }
    void report(const char* name) {
        if(request_==loggedRequest_ && effective==loggedEffective_ && bytes()==loggedBytes_ && reason_==loggedReason_)return;
        loggedRequest_=request_;loggedEffective_=effective;loggedBytes_=bytes();loggedReason_=reason_;
        std::fprintf(stderr,"AA %s: requested %dx, effective %dx, targets %.2f MiB",name,request_,effective,double(bytes())/(1<<20));
        if(plan_.tileW)std::fprintf(stderr,", reusable tile %dx%d",plan_.tileW,plan_.tileH);
        if(!reason_.empty())std::fprintf(stderr," (%s)",reason_.c_str());
        std::fprintf(stderr,"\n");
    }
    // Reuse translated storage and cull triangles outside the current padded
    // tile. This keeps a wide homogeneous model run from resubmitting all its
    // vertices for every tile. Source geometry remains immutable.
    std::span<const SDL_Vertex> translated(std::span<const SDL_Vertex> vertices,SDL_FPoint offset,SDL_Rect tile) {
        if(!effective || !plan_.tileW || tile.w<=0 || tile.h<=0)return vertices;
        if(!tileCull_) {
            translated_.assign(vertices.begin(),vertices.end());
            for(auto& v:translated_){v.position.x+=offset.x;v.position.y+=offset.y;}
            return translated_;
        }
        translated_.clear();
        for(size_t i=0;i+2<vertices.size();i+=3) {
            const auto triangle=vertices.subspan(i,3);
            const auto b=bounds(triangle);
            if(b.x+b.w<tile.x || b.y+b.h<tile.y || b.x>tile.x+tile.w || b.y>tile.y+tile.h)continue;
            for(auto v:triangle){v.position.x+=offset.x;v.position.y+=offset.y;translated_.push_back(v);}
        }
        return translated_;
    }
    static SDL_FRect bounds(std::span<const SDL_Vertex> verts) {
        if(verts.empty())return {};
        float x=verts[0].position.x,y=verts[0].position.y,r=x,b=y;
        for(const auto& v:verts){x=std::min(x,v.position.x);y=std::min(y,v.position.y);r=std::max(r,v.position.x);b=std::max(b,v.position.y);}
        return {x,y,r-x,b-y};
    }
    template<class F> void render(SDL_Renderer* r,SDL_FRect bounds,F draw) {
        if(!high_ || !effective){draw(SDL_FPoint{},SDL_Rect{});return;}
        if(!std::isfinite(bounds.x+bounds.y+bounds.w+bounds.h)){draw(SDL_FPoint{},SDL_Rect{});return;}
        SDL_Texture* previous=SDL_GetRenderTarget(r);
        float sx,sy;SDL_RenderGetScale(r,&sx,&sy);
        // All game coordinates are final drawable pixels. A nested/offscreen
        // caller must use its own pass instead of accidentally multiplying AA.
        if(previous || sx!=1 || sy!=1){draw(SDL_FPoint{},SDL_Rect{});return;}
        if(plan_.tileW){renderTiles(r,bounds,draw);return;}
        SDL_Rect viewport,clip;SDL_RenderGetViewport(r,&viewport);SDL_RenderGetClipRect(r,&clip);
        const bool clipped=SDL_RenderIsClipEnabled(r);
        SDL_BlendMode blend;Uint8 red,green,blue,alpha;
        SDL_GetRenderDrawBlendMode(r,&blend);SDL_GetRenderDrawColor(r,&red,&green,&blue,&alpha);
        auto restore=[&]{SDL_SetRenderTarget(r,previous);SDL_RenderSetScale(r,sx,sy);SDL_RenderSetViewport(r,&viewport);SDL_RenderSetClipRect(r,clipped?&clip:nullptr);SDL_SetRenderDrawBlendMode(r,blend);SDL_SetRenderDrawColor(r,red,green,blue,alpha);};
        auto fallback=[&]{
            restore();gpuvram::destroy(high_);gpuvram::destroy(middle_);high_=middle_=nullptr;
            plan_={};effective=0;reason_="render-target bind/resolve failed";retryAt_=SDL_GetTicks64()+3000;draw(SDL_FPoint{},SDL_Rect{});
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
        try {draw(SDL_FPoint{},clearArea);}catch(...){restore();throw;}
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
    template<class F> void renderTiles(SDL_Renderer* r,SDL_FRect bounds,F& draw) {
        SDL_Rect viewport,clip;SDL_RenderGetViewport(r,&viewport);SDL_RenderGetClipRect(r,&clip);
        const bool clipped=SDL_RenderIsClipEnabled(r);
        SDL_BlendMode blend;Uint8 red,green,blue,alpha;
        SDL_GetRenderDrawBlendMode(r,&blend);SDL_GetRenderDrawColor(r,&red,&green,&blue,&alpha);
        auto restore=[&]{SDL_SetRenderTarget(r,nullptr);SDL_RenderSetScale(r,1,1);SDL_RenderSetViewport(r,&viewport);SDL_RenderSetClipRect(r,clipped?&clip:nullptr);SDL_SetRenderDrawBlendMode(r,blend);SDL_SetRenderDrawColor(r,red,green,blue,alpha);};
        const int left=int(std::clamp(std::floor(double(bounds.x))-2.,0.,double(width_)));
        const int top=int(std::clamp(std::floor(double(bounds.y))-2.,0.,double(height_)));
        const int right=int(std::clamp(std::ceil(double(bounds.x)+bounds.w)+2.,0.,double(width_)));
        const int bottom=int(std::clamp(std::ceil(double(bounds.y)+bounds.h)+2.,0.,double(height_)));
        SDL_Rect area{left,top,right-left,bottom-top};
        if(clipped){SDL_Rect intersection;if(!SDL_IntersectRect(&area,&clip,&intersection))return;area=intersection;}
        if(area.w<=0 || area.h<=0)return;
        const auto plan=plan_;
        const int firstX=area.x/plan.tileW*plan.tileW;
        const int firstY=area.y/plan.tileH*plan.tileH;
        tileCull_=area.x+area.w>firstX+plan.tileW || area.y+area.h>firstY+plan.tileH;
        const double scale=std::sqrt(double(plan.samples));
        auto fail=[&] {
            restore();gpuvram::destroy(high_);gpuvram::destroy(middle_);high_=middle_=nullptr;
            plan_={};effective=0;reason_="render-target bind/resolve failed";retryAt_=SDL_GetTicks64()+3000;
        };
        // Anchor tile origins to the screen, not the current batch's bounds.
        // Moving another unit in a crowd must not change an otherwise stationary
        // face's atlas sampling or the phase of its downsample.
        for(int y=firstY;y<area.y+area.h;y+=plan.tileH)for(int x=firstX;x<area.x+area.w;x+=plan.tileW) {
            const SDL_Rect cell{x,y,plan.tileW,plan.tileH};
            SDL_Rect tile;
            if(!SDL_IntersectRect(&cell,&area,&tile))continue;
            const SDL_Rect padded{tile.x-2,tile.y-2,tile.w+4,tile.h+4};
            // Integer high-pixel origin, aligned for the intermediate 2:1
            // resolve. Fractional 2x/8x sampling stays screen-anchored as a
            // model moves or its bounds cross a tile boundary.
            const double ox=std::floor((x-2)*scale/2)*2,oy=std::floor((y-2)*scale/2)*2;
            const SDL_FPoint offset{float(-ox/scale),float(-oy/scale)};
            auto native=[&] {
                restore();SDL_RenderSetClipRect(r,&tile);
                try {draw(SDL_FPoint{},padded);}catch(...){restore();throw;}
                restore();
            };
            if(!effective){native();continue;}
            if(SDL_SetRenderTarget(r,high_)!=0){fail();native();continue;}
            SDL_RenderSetViewport(r,nullptr);SDL_RenderSetScale(r,1,1);SDL_RenderSetClipRect(r,nullptr);
            SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);SDL_SetRenderDrawColor(r,0,0,0,0);
            // Clear only the used region plus the filter guard. Full-target
            // clears for every tiny projectile would dominate sparse scenes.
            const int usedX=std::max(0,int(std::floor(padded.x*scale-ox)));
            const int usedY=std::max(0,int(std::floor(padded.y*scale-oy)));
            const int usedRight=std::min(plan.w,int(std::ceil((padded.x+padded.w)*scale-ox)));
            const int usedBottom=std::min(plan.h,int(std::ceil((padded.y+padded.h)*scale-oy)));
            const SDL_Rect used{usedX,usedY,usedRight-usedX,usedBottom-usedY};
            if(SDL_RenderFillRect(r,&used)!=0){fail();native();continue;}
            SDL_RenderSetScale(r,float(scale),float(scale));
            SDL_SetRenderDrawBlendMode(r,blend);SDL_SetRenderDrawColor(r,red,green,blue,alpha);
            try {draw(offset,padded);}catch(...){restore();throw;}
            SDL_Texture* resolved=high_;
            if(middle_) {
                if(SDL_SetRenderTarget(r,middle_)!=0){fail();native();continue;}
                SDL_RenderSetViewport(r,nullptr);SDL_RenderSetScale(r,1,1);
                const SDL_Rect usedMiddle{used.x/2,used.y/2,
                    (used.x+used.w+1)/2-used.x/2,(used.y+used.h+1)/2-used.y/2};
                SDL_RenderSetClipRect(r,&usedMiddle);
                if(SDL_RenderCopy(r,high_,nullptr,nullptr)!=0){fail();native();continue;}
                resolved=middle_;
            }
            restore();SDL_RenderSetClipRect(r,&tile);
            const SDL_FRect dest{float(ox/scale),float(oy/scale),float(plan.w/scale),float(plan.h/scale)};
            if(SDL_RenderCopyF(r,resolved,nullptr,&dest)!=0){fail();native();continue;}
            ++resolves;
        }
        restore();
    }
    void geometry(SDL_Renderer* r,SDL_Texture* texture,const SDL_Vertex* verts,int count) {
        render(r,effective?bounds({verts,size_t(count)}):SDL_FRect{},[&](SDL_FPoint offset,SDL_Rect tile){
            const auto v=translated({verts,size_t(count)},offset,tile);
            if(!v.empty())SDL_RenderGeometry(r,texture,v.data(),int(v.size()),nullptr,0);
        });
    }
};
}
