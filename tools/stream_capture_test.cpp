#include "client/streaming.h"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
int main(int argc,char** argv) {
    SDL_SetMainReady();
    if(SDL_Init(SDL_INIT_VIDEO)!=0){std::fprintf(stderr,"%s\n",SDL_GetError());return 1;}
    const int w=argc>3?std::atoi(argv[3]):640, h=argc>4?std::atoi(argv[4]):480;
    SDL_Window* win=SDL_CreateWindow("capture test",0,0,w,h,SDL_WINDOW_HIDDEN);
    SDL_Renderer* ren=SDL_CreateRenderer(win,-1,argc>1?SDL_RENDERER_ACCELERATED:SDL_RENDERER_SOFTWARE);
    if(!ren){std::fprintf(stderr,"%s\n",SDL_GetError());return 1;}
    SDL_RendererInfo info{};SDL_GetRendererInfo(ren,&info);
    std::string path=argc>2?argv[2]:(std::filesystem::temp_directory_path()/"tak-stream-capture.flv").string();
    {
        tak::Streaming streaming(ren);
        tak::video::StreamConfig c;c.encoder=argc>6?argv[6]:"libx264";
        if(argc>5 && std::string(argv[5])=="native") {c.width=w&~1;c.height=h&~1;c.bitrateKbps=30000;}
        if(!streaming.stream().startRecording(c,path))return 1;
        SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_F9;
        if(!streaming.input(e,w,h)||!streaming.shown())return 2;
        Uint64 start=SDL_GetTicks64();
        uint64_t captureUs=0; unsigned calls=0;
        while(SDL_GetTicks64()-start<1800){
            // Wide red left half, green right half; blue top-left corner lets
            // a decoder check both channel order and vertical orientation.
            SDL_SetRenderDrawColor(ren,255,0,0,255);SDL_RenderClear(ren);
            SDL_SetRenderDrawColor(ren,0,255,0,255);SDL_Rect r{w/2,0,w-w/2,h};SDL_RenderFillRect(ren,&r);
            SDL_SetRenderDrawColor(ren,0,0,255,255);r={0,0,w/6,h/5};SDL_RenderFillRect(ren,&r);
            const auto before=SDL_GetPerformanceCounter();
            streaming.frame(w,h);
            captureUs+=(SDL_GetPerformanceCounter()-before)*1000000/SDL_GetPerformanceFrequency(); ++calls; // private panel drawn AFTER captured image
            SDL_RenderPresent(ren);SDL_Delay(16);
        }
        auto s=streaming.stream().status();
        std::printf("capture average_us=%llu calls=%u dropped=%llu\n", (unsigned long long)(captureUs/calls), calls, (unsigned long long)s.dropped);
        if(s.frames<30){std::fprintf(stderr,"%s\n",s.state.c_str());return 3;}
        std::printf("capture backend=%s file=%s frames=%llu\n",info.name,path.c_str(),(unsigned long long)s.frames);
        streaming.stream().stop();
    }
    SDL_DestroyRenderer(ren);SDL_DestroyWindow(win);SDL_Quit();
    if(argc<2)std::filesystem::remove(path);
}
