#include "client/streaming.h"
#include "client/blockfont.h"
#include <SDL_opengl.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace tak {
namespace {
// Double-buffered OpenGL readback: only map after the fence signals. SDL's
// Metal/D3D/software backends use a throttled portable readback fallback.
class Capture {
public:
    ~Capture() { clear(); }
    void reset() { clear(); w_=h_=0; checked_=available_=false; context_=nullptr; std::vector<uint8_t>().swap(pixels_); }
    bool get(SDL_Renderer* ren, int w, int h, video::Stream& stream) {
        if (w<=0 || h<=0 || w>8192 || h>8192) return false;
        SDL_RendererInfo info{}; SDL_GetRendererInfo(ren,&info);
        bool gl = info.name && !std::strcmp(info.name,"opengl");
        if (gl && !checked_) { checked_=true; available_=load(); context_=SDL_GL_GetCurrentContext(); }
        if (gl && available_ && SDL_GL_GetCurrentContext()==context_) {
            SDL_RenderFlush(ren);
            if (w!=w_ || h!=h_) {clear();w_=w;h_=h; gen_(2,buffers_.data());}
            GLint old=0, align=0; get_(GL_PIXEL_PACK_BUFFER_BINDING,&old);get_(GL_PACK_ALIGNMENT,&align);
            GLint rowLength=0, skipRows=0, skipPixels=0;
            get_(GL_PACK_ROW_LENGTH,&rowLength); get_(GL_PACK_SKIP_ROWS,&skipRows); get_(GL_PACK_SKIP_PIXELS,&skipPixels);
            store_(GL_PACK_ROW_LENGTH,0); store_(GL_PACK_SKIP_ROWS,0); store_(GL_PACK_SKIP_PIXELS,0);
            store_(GL_PACK_ALIGNMENT,1);
            bool delivered=false;
            // Slots can wrap while an older readback is still pending. Deliver
            // ready frames in capture order so the newest image always wins.
            const std::array<int,2> order = serial_[0] <= serial_[1] ? std::array<int,2>{0,1} : std::array<int,2>{1,0};
            for(int i : order) if(fences_[i]) {
                GLenum result=wait_(fences_[i],GL_SYNC_FLUSH_COMMANDS_BIT,0);
                if(result==GL_ALREADY_SIGNALED || result==GL_CONDITION_SATISFIED) {
                    bind_(GL_PIXEL_PACK_BUFFER,buffers_[i]);
                    const auto* ptr=static_cast<const uint8_t*>(map_(GL_PIXEL_PACK_BUFFER,GL_READ_ONLY));
                    if(ptr) {
                        pixels_.resize(size_t(w)*h*4);
                        for(int y=0;y<h;++y) std::memcpy(pixels_.data()+size_t(y)*w*4,ptr+size_t(h-1-y)*w*4,size_t(w)*4);
                        unmap_(GL_PIXEL_PACK_BUFFER);
                        delivered=stream.video(pixels_.data(),w,h,w*4);
                    }
                    deleteSync_(fences_[i]);fences_[i]=nullptr;
                }
            }
            for(int i=0;i<2;++i) if(!fences_[i]) {
                bind_(GL_PIXEL_PACK_BUFFER,buffers_[i]);
                data_(GL_PIXEL_PACK_BUFFER,GLsizeiptr(size_t(w)*h*4),nullptr,GL_STREAM_READ);
                read_(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
                fences_[i]=fence_(GL_SYNC_GPU_COMMANDS_COMPLETE,0);serial_[i]=++nextSerial_;break;
            }
            bind_(GL_PIXEL_PACK_BUFFER,GLuint(old));store_(GL_PACK_ALIGNMENT,align);
            store_(GL_PACK_ROW_LENGTH,rowLength); store_(GL_PACK_SKIP_ROWS,skipRows); store_(GL_PACK_SKIP_PIXELS,skipPixels);
            return delivered;
        }
        pixels_.resize(size_t(w)*h*4);
        if(SDL_RenderReadPixels(ren,nullptr,SDL_PIXELFORMAT_RGBA32,pixels_.data(),w*4)!=0)return false;
        return stream.video(pixels_.data(),w,h,w*4);
    }
private:
    template<class T> bool proc(T& out,const char* name) {out=reinterpret_cast<T>(SDL_GL_GetProcAddress(name));return out!=nullptr;}
    bool load() {
        if (!SDL_GL_ExtensionSupported("GL_ARB_sync")) return false;
        return proc(gen_,"glGenBuffers") && proc(del_,"glDeleteBuffers") && proc(bind_,"glBindBuffer") &&
          proc(data_,"glBufferData") && proc(map_,"glMapBuffer") && proc(unmap_,"glUnmapBuffer") &&
          proc(fence_,"glFenceSync") && proc(wait_,"glClientWaitSync") && proc(deleteSync_,"glDeleteSync") &&
          proc(read_,"glReadPixels") && proc(get_,"glGetIntegerv") && proc(store_,"glPixelStorei");
    }
    void clear() {
        if(context_ && context_==SDL_GL_GetCurrentContext()) {
            for(auto& f:fences_) if(f) {deleteSync_(f);f=nullptr;}
            if(buffers_[0]) del_(2,buffers_.data());
        }
        buffers_={};fences_={};
    }
    bool checked_=false,available_=false;
    int w_=0,h_=0;SDL_GLContext context_=nullptr;
    std::array<uint64_t,2> serial_{};uint64_t nextSerial_=0;
    std::array<GLuint,2> buffers_{};std::array<GLsync,2> fences_{};
    std::vector<uint8_t> pixels_;
    void (APIENTRY *gen_)(GLsizei,GLuint*)=nullptr;
    void (APIENTRY *del_)(GLsizei,const GLuint*)=nullptr;
    void (APIENTRY *bind_)(GLenum,GLuint)=nullptr;
    void (APIENTRY *data_)(GLenum,GLsizeiptr,const void*,GLenum)=nullptr;
    void* (APIENTRY *map_)(GLenum,GLenum)=nullptr;
    GLboolean (APIENTRY *unmap_)(GLenum)=nullptr;
    GLsync (APIENTRY *fence_)(GLenum,GLbitfield)=nullptr;
    GLenum (APIENTRY *wait_)(GLsync,GLbitfield,GLuint64)=nullptr;
    void (APIENTRY *deleteSync_)(GLsync)=nullptr;
    void (APIENTRY *read_)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*)=nullptr;
    void (APIENTRY *get_)(GLenum,GLint*)=nullptr;
    void (APIENTRY *store_)(GLenum,GLint)=nullptr;
};
}
struct Streaming::Impl {
    SDL_Renderer* ren;
    video::Stream stream;
    video::StreamConfig config;
    Capture capture;
    bool visible=false,editing=false,wasActive=false,wasTextInput=false;
    std::string error;
    uint64_t nextCapture=0, rateTime=0, rateBytes=0;
    uint64_t rateKbps=0;
    float x=0,y=0,u=1;
    SDL_FRect row(int n) const {return {x+20*u,y+(70+45*n)*u,600*u,34*u};}
    void layout(int w,int h) {u=std::max(.4f,std::min({float(w)/680,float(h)/510,1.5f}));x=(w-640*u)/2;y=(h-470*u)/2;}
    void show(bool on) {
        if(on&&!visible)wasTextInput=SDL_IsTextInputActive();
        visible=on;editing=on&&!stream.active();
        if(editing||(!on&&wasTextInput))SDL_StartTextInput();else SDL_StopTextInput();
    }
    void draw(int w,int h) {
        auto s=stream.status();
        uint64_t now=SDL_GetTicks64();
        if(!s.active || s.bytes<rateBytes) {rateTime=now;rateBytes=s.bytes;rateKbps=0;}
        else if(now-rateTime>=1000) {rateKbps=(s.bytes-rateBytes)*8/(now-rateTime);rateTime=now;rateBytes=s.bytes;}
        if(!visible) {
            if(s.active || s.state.starts_with("FAILED")) {
                std::string text="STREAM " + s.state + " - F9";
                SDL_SetRenderDrawColor(ren,10,12,18,230);SDL_FRect r{10,10,std::min(float(w-20),blockTextWidth(text,1.5f)+20),30};SDL_RenderFillRectF(ren,&r);
                drawBlockText(ren,text,20,20,1.5f,{255,210,100,255});
            }
            return;
        }
        layout(w,h);
        SDL_SetRenderDrawBlendMode(ren,SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren,12,17,26,248);SDL_FRect panel{x,y,640*u,470*u};SDL_RenderFillRectF(ren,&panel);
        auto text=[&](const std::string& t,float px,float py,SDL_Color col=SDL_Color{235,235,235,255}) {drawBlockText(ren,t,x+px*u,y+py*u,1.5f*u,col);};
        text("YOUTUBE STREAMING",20,20);
        text("GAME VIDEO AND AUDIO - KEY IS NOT SAVED",20,45);
        std::array<std::string,6> labels={
            "STREAM KEY: " + (config.key.empty()?std::string("CLICK AND PASTE"):std::string(std::min<size_t>(config.key.size(),36),'*')),
            "RESOLUTION: " + std::to_string(config.width)+" X "+std::to_string(config.height),
            "FRAME RATE: " + std::to_string(config.fps),
            "BITRATE: " + std::to_string(config.bitrateKbps)+" KBPS",
            "ENCODER: " + std::string(config.encoder.empty()?"AUTOMATIC":"CPU"),
            s.active?"STOP STREAMING":"START STREAMING"};
        for(int i=0;i<6;++i) {
            auto r=row(i);SDL_SetRenderDrawColor(ren,i==0&&editing?55:35,45,60,255);SDL_RenderFillRectF(ren,&r);
            drawBlockText(ren,labels[i],r.x+10*u,r.y+11*u,1.5f*u,{240,235,210,255});
        }
        text(s.state,20,352,{255,210,100,255});
        text(s.encoder+"  FRAMES "+std::to_string(s.frames)+"  DROPPED "+std::to_string(s.dropped),20,375);
        text("SENT "+std::to_string(s.bytes/1024/1024)+" MB  RATE "+std::to_string(rateKbps)+" KBPS",20,398);
        text(error.empty()?"ESC / F9: CLOSE - STREAM CONTINUES UNTIL STOP":error,20,437);
    }
};
Streaming::Streaming(SDL_Renderer* ren):p_(std::make_unique<Impl>()) {p_->ren=ren;}
Streaming::~Streaming(){if(p_->visible)SDL_StopTextInput();}
video::Stream& Streaming::stream(){return p_->stream;}
bool Streaming::shown() const{return p_->visible;}
bool Streaming::input(const SDL_Event& e,int w,int h) {
    if((e.type==SDL_KEYDOWN && !e.key.repeat && e.key.keysym.sym==SDLK_F9) ||
       (e.type==SDL_USEREVENT && e.user.code==kStreamingEvent)) {p_->show(!p_->visible);return true;}
    if(e.type==SDL_RENDER_TARGETS_RESET || e.type==SDL_RENDER_DEVICE_RESET)p_->capture.reset();
    if(!p_->visible)return false;
    p_->layout(w,h);
    if(e.type==SDL_KEYDOWN) {
        if(e.key.keysym.sym==SDLK_ESCAPE){p_->show(false);return true;}
        if(p_->editing&&!p_->stream.active()) {
            if(e.key.keysym.sym==SDLK_BACKSPACE && !p_->config.key.empty())p_->config.key.pop_back();
            if((e.key.keysym.mod & KMOD_CTRL) && e.key.keysym.sym==SDLK_v) {
                char* t=SDL_GetClipboardText();if(t){p_->config.key=t;SDL_free(t);}
                if(p_->config.key.size()>256)p_->config.key.resize(256);
            }
            if((e.key.keysym.mod & KMOD_CTRL) && e.key.keysym.sym==SDLK_a)p_->config.key.clear();
        }
    }
    if(e.type==SDL_TEXTINPUT && p_->editing && !p_->stream.active() && p_->config.key.size()<256)p_->config.key+=e.text.text;
    if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT) {
        SDL_FPoint pt{float(e.button.x),float(e.button.y)};
        for(int i=0;i<6;++i) {auto r=p_->row(i);if(!SDL_PointInFRect(&pt,&r))continue;
            if(i==5) {
                if(p_->stream.active())p_->stream.stop();
                else if(!p_->stream.start(p_->config))p_->error="ENTER A VALID YOUTUBE STUDIO STREAM KEY";
                else {p_->error.clear();p_->editing=false;SDL_StopTextInput();}
            } else if(!p_->stream.active()) {
                p_->editing=i==0;if(p_->editing)SDL_StartTextInput();else SDL_StopTextInput();
                if(i==1){p_->config.width=p_->config.width==1280?1920:1280;p_->config.height=p_->config.width==1280?720:1080;}
                if(i==2)p_->config.fps=p_->config.fps==30?60:30;
                if(i==3)p_->config.bitrateKbps=p_->config.bitrateKbps>=12000?3000:p_->config.bitrateKbps+3000;
                if(i==4)p_->config.encoder=p_->config.encoder.empty()?"libx264":"";
            }
        }
    }
    // Don't send setup keystrokes/clicks as game orders. Still pass window events.
    return e.type==SDL_KEYDOWN||e.type==SDL_KEYUP||e.type==SDL_TEXTINPUT||e.type==SDL_MOUSEBUTTONDOWN||e.type==SDL_MOUSEBUTTONUP||e.type==SDL_MOUSEMOTION||e.type==SDL_MOUSEWHEEL;
}
void Streaming::draw(int w,int h) { p_->draw(w,h); }
void Streaming::frame(int w,int h,bool drawPanel) {
    uint64_t now=SDL_GetTicks64()*1000;
    bool active=p_->stream.active();
    if(!active && p_->wasActive)p_->capture.reset();
    p_->wasActive=active;
    if(active&&now>=p_->nextCapture&&w>0&&h>0){
        p_->capture.get(p_->ren,w,h,p_->stream);
        const uint64_t interval=1000000/p_->config.fps;
        // Keep a stable cadence across uneven render frames; now + interval
        // loses a whole render frame whenever the deadline is slightly late.
        p_->nextCapture+=interval;
        if(p_->nextCapture<=now)p_->nextCapture=now+interval;
    }
    if(drawPanel) p_->draw(w,h);
}
}
