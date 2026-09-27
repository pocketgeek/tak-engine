#include "client/menumusic.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <tuple>

namespace tak {
// The test uses the ordinary SDL dummy device, without the Options UI.
SDL_AudioDeviceID openAudioDevice(int capture, const SDL_AudioSpec* want,
                                 SDL_AudioSpec* have, int allowed) {
    return SDL_OpenAudioDevice(nullptr,capture,want,have,allowed);
}
struct MenuMusicTestAccess {
    static void source(MenuMusic& music, SDL_AudioFormat format, int rate, int channels) {
        music.stop();
        music.fmt_=format;music.silence_=format==AUDIO_U8?128:0;
        music.openWant_.freq=rate;music.openWant_.channels=Uint8(channels);
        const int bytes=SDL_AUDIO_BITSIZE(format)/8;
        music.src_.resize(size_t(rate*channels*bytes));
        for(int i=0;i<rate*channels;++i) {
            auto* out=music.src_.data()+i*bytes;
            if(format==AUDIO_U8)*out=160;
            else if(format==AUDIO_F32SYS) {float value=channels==2 && i%2 ? -.125f : .25f;std::memcpy(out,&value,4);}
            else {int16_t value=channels==2 && i%2 ? -4096 : 8192;std::memcpy(out,&value,2);}
        }
        music.resetTapConverter();
    }
    static void fill(MenuMusic& music, int frames) {
        std::vector<Uint8> out(size_t(frames*music.openWant_.channels*SDL_AUDIO_BITSIZE(music.fmt_)/8));
        music.fill(out.data(),int(out.size()));
    }
};
}
struct Capture {
    std::vector<int16_t> pcm;
    static void tap(void* context,const int16_t* data,int frames,int channels) {
        if(channels!=2)throw std::runtime_error("tap must be stereo");
        auto& out=static_cast<Capture*>(context)->pcm;
        out.insert(out.end(),data,data+frames*channels);
    }
};
int main(int argc,char**argv) {
    SDL_SetMainReady();SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    if(SDL_Init(SDL_INIT_AUDIO)!=0)return 2;
    try {
        tak::MenuMusic music;Capture capture;
        music.setAudioTap(&capture,Capture::tap);
        for(auto [format,rate,channels]: {std::tuple{AUDIO_U8,11025,1},
                std::tuple{AUDIO_S16SYS,44100,2},std::tuple{AUDIO_F32SYS,48000,2}}) {
            tak::MenuMusicTestAccess::source(music,SDL_AudioFormat(format),rate,channels);
            music.setVolume(256,128);capture.pcm.clear();
            for(int i=0;i<100;++i)tak::MenuMusicTestAccess::fill(music,rate/50);
            const size_t frames=capture.pcm.size()/2;
            if(frames<21000 || frames>22100)throw std::runtime_error("wrong resampled duration");
            const size_t middle=(capture.pcm.size()/4)*2;
            if(std::abs(int(capture.pcm[middle])-4096)>20)
                throw std::runtime_error("music volume not reflected in stream");
            if(std::abs(int(capture.pcm[middle+1])-(channels==1?4096:-2048))>20)
                throw std::runtime_error("stereo channels lost during menu capture");
            music.setVolume(0,128);
            for(int i=0;i<20;++i)tak::MenuMusicTestAccess::fill(music,rate/50);
            if(std::abs(int(capture.pcm.back()))>1)throw std::runtime_error("master mute not reflected");
            music.setAudioTap(nullptr,nullptr);const auto count=capture.pcm.size();
            tak::MenuMusicTestAccess::fill(music,rate/50);
            if(capture.pcm.size()!=count)throw std::runtime_error("detached tap still called");
            music.setAudioTap(&capture,Capture::tap);
        }
        music.stop();
        if(music.playing())throw std::runtime_error("stopped music still owns audio");
        if(argc==2) {
            auto vfs=tak::hpi::mountRetailRoot(argv[1],tak::hpi::OverridePolicy::None);
            capture.pcm.clear();
            music.setVolume(256,256);music.start(vfs,15);
            if(!music.playing())throw std::runtime_error("retail title track failed to start");
            SDL_Delay(200);music.reopen();
            if(!music.playing())throw std::runtime_error("music device reopen failed");
            SDL_Delay(200);music.stop();
            // The callback is joined before reading the capture vector.
            bool audible=false;for(auto sample:capture.pcm)audible|=sample!=0;
            if(!audible)throw std::runtime_error("retail title tap remained silent");
        }
        music.setAudioTap(nullptr,nullptr);
        std::cout<<"PASS: menu audio conversion, volume, mute, looping, detach and device lifecycle\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}
    SDL_Quit();
}
