#pragma once
#include <SDL.h>
#include <algorithm>
#include <cstring>
#include <span>
#include <vector>

namespace tak {
// Visibility generations can advance without changing a pixel. Retain the last
// successful upload and update only changed 64-cell blocks, on every SDL backend.
class FogUpload {
public:
    bool update(SDL_Texture* texture,std::span<const uint8_t> visibility,int w,int h,bool force=false) {
        uploadedBytes_=0;
        if(!texture || w<=0 || h<=0 || visibility.size()!=size_t(w)*h)return false;
        const bool full=force || width_!=w || previous_.size()!=visibility.size();
        constexpr int block=64;
        const int columns=(w+block-1)/block,rows=(h+block-1)/block;
        std::vector<uint8_t> dirty(size_t(columns)*rows,full);
        if(!full)for(int y=0;y<h;++y) {
            const size_t row=size_t(y)*w;
            if(!std::memcmp(previous_.data()+row,visibility.data()+row,size_t(w)))continue;
            for(int x=0;x<w;x+=block)
                if(std::memcmp(previous_.data()+row+x,visibility.data()+row+x,size_t(std::min(block,w-x))))
                    dirty[size_t(y/block)*columns+x/block]=1;
        }
        auto upload=[&](SDL_Rect rect) {
            void* pixels=nullptr;int pitch=0;
            if(SDL_LockTexture(texture,&rect,&pixels,&pitch)!=0)return false;
            for(int y=0;y<rect.h;++y) {
                auto* row=static_cast<uint8_t*>(pixels)+size_t(y)*pitch;
                for(int x=0;x<rect.w;++x) {
                    const auto v=visibility[size_t(y+rect.y)*w+x+rect.x];
                    row[x*4]=row[x*4+1]=row[x*4+2]=0;
                    row[x*4+3]=v==2?0:v==1?110:235;
                }
            }
            SDL_UnlockTexture(texture);uploadedBytes_+=size_t(rect.w)*rect.h*4;
            return true;
        };
        if(full) { if(!upload({0,0,w,h}))return false; }
        else for(int y=0;y<rows;++y)for(int x=0;x<columns;++x) {
            if(!dirty[size_t(y)*columns+x])continue;
            int end=x+1;while(end<columns && dirty[size_t(y)*columns+end])++end;
            if(!upload({x*block,y*block,std::min(end*block,w)-x*block,std::min(block,h-y*block)}))return false;
            x=end-1;
        }
        if(uploadedBytes_) {previous_.assign(visibility.begin(),visibility.end());++generation_;}
        width_=w;return true;
    }
    void clear() { previous_.clear();width_=0; }
    uint32_t generation() const { return generation_; }
    size_t uploadedBytes() const { return uploadedBytes_; }
private:
    std::vector<uint8_t> previous_;
    uint32_t generation_=0;
    int width_=0;
    size_t uploadedBytes_=0;
};
}
