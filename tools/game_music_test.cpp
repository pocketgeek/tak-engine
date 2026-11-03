#include "client/sound.h"
#include "hpi/hpi.h"
#include <cmath>
#include <iostream>

// In-game music through SoundBank: it plays at its native 22050 Hz in stereo, unit
// effects keep their length after the 11025 -> 22050 Hz decode, and the stream
// tap still receives 11025 Hz PCM. No device: mix()/mixThunk are driven directly.
struct SoundBankTestAccess {
    static void layout(SoundBank& bank,int channels) {bank.chan_=channels;bank.musicVol_=256;}
    static std::optional<std::vector<int16_t>> convert(const std::vector<uint8_t>& wav,int channels) {
        return SoundBank::convertWav(wav.data(),wav.size(),channels);
    }
    static void setMusic(SoundBank& bank,std::vector<int16_t> pcm) {
        bank.music_=std::move(pcm);bank.musicPos_=0;bank.musicDone_=false;
    }
    static bool musicDone(const SoundBank& bank) {return bank.musicDone_;}
    static std::vector<int16_t> mix(SoundBank& bank,int frames) {
        std::vector<int16_t> out(size_t(frames)*size_t(bank.chan_));
        bank.mix(out.data(),int(out.size()));return out;
    }
    static std::vector<int16_t> mixThunk(SoundBank& bank,int frames) {
        std::vector<int16_t> out(size_t(frames)*size_t(bank.chan_));
        SoundBank::mixThunk(&bank,reinterpret_cast<Uint8*>(out.data()),int(out.size()*2));
        return out;
    }
    static bool decodeSfx(SoundBank& bank,const std::string& name,const std::vector<uint8_t>& wav) {
        auto pcm=bank.decodeWav(wav.data(),wav.size());
        if(!pcm)return false;
        bank.cache_[name]=std::move(*pcm);bank.index_[name]=name;return true;
    }
    static size_t cached(SoundBank& bank,const std::string& name) {return bank.cache_[name].size();}
    // playAt needs a device to admit a voice; a paused dummy one never calls back.
    static bool openPaused(SoundBank& bank) {
        SDL_AudioSpec want{};want.freq=SoundBank::kMixRate;want.format=AUDIO_S16SYS;
        want.channels=1;want.samples=512;
        bank.dev_=SDL_OpenAudioDevice(nullptr,0,&want,&bank.spec_,0);
        return bank.dev_!=0;
    }
};

namespace {
// A canonical PCM RIFF/WAVE file in memory.
std::vector<uint8_t> wavFile(int rate,int channels,int bits,const std::vector<int>& samples) {
    std::vector<uint8_t> out;
    auto u32=[&](uint32_t v){for(int i=0;i<4;++i)out.push_back(uint8_t(v>>(8*i)));};
    auto u16=[&](uint16_t v){out.push_back(uint8_t(v));out.push_back(uint8_t(v>>8));};
    const uint32_t bytes=uint32_t(samples.size())*uint32_t(bits/8);
    out.insert(out.end(),{'R','I','F','F'});u32(36+bytes);out.insert(out.end(),{'W','A','V','E'});
    out.insert(out.end(),{'f','m','t',' '});u32(16);u16(1);u16(uint16_t(channels));u32(uint32_t(rate));
    u32(uint32_t(rate*channels*bits/8));u16(uint16_t(channels*bits/8));u16(uint16_t(bits));
    out.insert(out.end(),{'d','a','t','a'});u32(bytes);
    for(int s:samples) {if(bits==8)out.push_back(uint8_t(s));else u16(uint16_t(int16_t(s)));}
    return out;
}
// Goertzel magnitude of one channel of interleaved PCM at `freq`, normalised so a
// full-scale-A sine reads ~A.
double tone(const std::vector<int16_t>& pcm,int channels,int channel,double freq,int rate) {
    const size_t n=pcm.size()/size_t(channels);
    const double w=2*M_PI*freq/rate,c=2*std::cos(w);
    double s1=0,s2=0;
    for(size_t i=0;i<n;++i){double s=pcm[i*size_t(channels)+size_t(channel)]+c*s1-s2;s2=s1;s1=s;}
    return std::sqrt(s1*s1+s2*s2-c*s1*s2)*2/double(n);
}
}

int main(int argc,char** argv) {
    int failures=0;
    const auto check=[&](bool ok,const std::string& message) {if(!ok){++failures;std::cerr<<"FAIL "<<message<<'\n';}};
    SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    if(SDL_Init(SDL_INIT_AUDIO)!=0)return 2;
    constexpr int R=SoundBank::kMixRate;
    {
        // 22050 Hz stereo track: an 8 kHz tone (above the old 5.5 kHz ceiling) on
        // the left, 440 Hz on the right. Both must survive, each on its own side.
        constexpr int N=4410;
        std::vector<int> s;
        for(int i=0;i<N;++i){s.push_back(int(8000*std::sin(2*M_PI*8000*i/R)));
                             s.push_back(int(8000*std::sin(2*M_PI*440*i/R)));}
        auto pcm=SoundBankTestAccess::convert(wavFile(R,2,16,s),2);
        check(pcm && pcm->size()==size_t(2*N),"native-rate stereo track decodes without resampling");
        SoundBank bank;SoundBankTestAccess::layout(bank,2);
        if(pcm)SoundBankTestAccess::setMusic(bank,*pcm);
        auto out=SoundBankTestAccess::mix(bank,N);
        const double l8=tone(out,2,0,8000,R),r8=tone(out,2,1,8000,R);
        const double l4=tone(out,2,0,440,R),r4=tone(out,2,1,440,R);
        std::cerr<<"stereo: L8k="<<l8<<" R8k="<<r8<<" L440="<<l4<<" R440="<<r4<<'\n';
        check(l8>7000 && r8<200,"8 kHz left-channel content reaches only the left speaker");
        check(r4>7000 && l4<200,"440 Hz right-channel content reaches only the right speaker");
        check(!SoundBankTestAccess::musicDone(bank),"track still playing on its last frame");
        SoundBankTestAccess::mix(bank,1);
        check(SoundBankTestAccess::musicDone(bank),"track end flags the advance to the next track");
    }
    {
        // Music volume applies per channel; mono output folds L and R.
        SoundBank bank;SoundBankTestAccess::layout(bank,2);bank.setMusicVolume(128);
        SoundBankTestAccess::setMusic(bank,{8192,-4096,8192,-4096});
        auto out=SoundBankTestAccess::mix(bank,2);
        check(out[0]==4096 && out[1]==-2048,"music volume scales both channels");
        SoundBankTestAccess::layout(bank,1);SoundBankTestAccess::setMusic(bank,{8192,-4096});
        check(SoundBankTestAccess::mix(bank,1)[0]==2048,"mono output gets (L+R)/2");
    }
    {
        // 5.1: L/R halved on front and rear, a quarter of each in the centre, and the
        // LFE only the low-passed mono sum.
        SoundBank bank;SoundBankTestAccess::layout(bank,6);
        std::vector<int16_t> pcm;for(int i=0;i<R;++i){pcm.push_back(8192);pcm.push_back(-4096);}
        SoundBankTestAccess::setMusic(bank,pcm);
        auto out=SoundBankTestAccess::mix(bank,R);
        check(out[0]==4096 && out[1]==-2048 && out[2]==1024 && out[4]==4096 && out[5]==-2048,
              "5.1 music layout: FL/FR/BL/BR halved, centre (L+R)/4");
        check(out[3]>0 && out[3]<100,"LFE starts low-passed, not the full feed");
        const int settled=out[size_t(R-1)*6+3];
        check(settled>=1420 && settled<=1434,"LFE settles to the sub gain of (L+R)/2 = "+std::to_string(settled));
        // ~110 Hz one-pole at 22050 Hz: 63% of the step after 1/(2*pi*110) s = 32 frames.
        const int tau=out[32*6+3];
        check(tau>800 && tau<1000,"LFE cutoff stays ~110 Hz at the new rate ("+std::to_string(tau)+")");
    }
    {
        // A mod track at 11025 Hz mono is converted to the mixer's stereo format,
        // with its duration kept.
        std::vector<int> s(1000,3000);
        auto pcm=SoundBankTestAccess::convert(wavFile(11025,1,16,s),2);
        check(pcm && std::abs(int(pcm->size())-4000)<=4,"11025 Hz mono mod track becomes 22050 Hz stereo");
    }
    {
        // An 11025 Hz 8-bit mono unit sound lasts as long as before: twice the
        // frames at twice the rate.
        SoundBank bank;
        if(!SoundBankTestAccess::openPaused(bank))return 2;
        SoundBankTestAccess::layout(bank,1);
        check(SoundBankTestAccess::decodeSfx(bank,"blip",wavFile(11025,1,8,std::vector<int>(1102,0xC0))),
              "11025 Hz 8-bit SFX decodes");
        const size_t n=SoundBankTestAccess::cached(bank,"blip");
        check(n>=2202 && n<=2206,"decoded SFX has twice the samples ("+std::to_string(n)+")");
        bank.play("blip");
        auto out=SoundBankTestAccess::mix(bank,3000);
        int last=-1;for(int i=0;i<3000;++i)if(out[size_t(i)]!=0)last=i;
        check(last>=2196 && last<=2206,"SFX plays 0.1 s = ~2204 frames at 22050 Hz ("+std::to_string(last+1)+")");
    }
    {
        // Tap: 512 device frames at 22050 Hz reach the stream as 256 at 11025 Hz,
        // channel count unchanged, levels kept; an odd callback carries over.
        struct Sink {int frames=0,channels=0,calls=0;std::vector<int16_t> pcm;} sink;
        SoundBank bank;SoundBankTestAccess::layout(bank,2);
        std::vector<int16_t> pcm;for(int i=0;i<4000;++i){pcm.push_back(8192);pcm.push_back(-4096);}
        SoundBankTestAccess::setMusic(bank,pcm);
        bank.setAudioTap(&sink,[](void* ctx,const int16_t* p,int frames,int channels) {
            auto* s=static_cast<Sink*>(ctx);s->frames+=frames;s->channels=channels;++s->calls;
            s->pcm.insert(s->pcm.end(),p,p+size_t(frames)*size_t(channels));
        });
        SoundBankTestAccess::mixThunk(bank,512);
        check(sink.frames==256 && sink.channels==2,"tap gets half the frames, same channels");
        SoundBankTestAccess::mixThunk(bank,511);SoundBankTestAccess::mixThunk(bank,1);
        check(sink.frames==512,"odd-sized callbacks still decimate exactly 2:1 ("+std::to_string(sink.frames)+")");
        check(sink.pcm[2]==8192 && sink.pcm[3]==-4096 && sink.pcm[1022]==8192 && sink.pcm[1023]==-4096,
              "tap keeps the stereo levels");
        bank.setAudioTap(nullptr,nullptr);
    }
    if(argc==2) {
        // Retail tracks are 22050 Hz stereo: they reach the mixer frame for frame.
        auto vfs=tak::hpi::mountRetailRoot(argv[1]);
        int tracks=0;
        for(int t=1;t<=20;++t) {
            const std::string path="music/track"+std::to_string(t)+".wav";
            if(!vfs.has(path))continue;
            auto raw=vfs.read(path);
            SDL_AudioSpec spec{};Uint8* buf=nullptr;Uint32 len=0;
            if(!SDL_LoadWAV_RW(SDL_RWFromConstMem(raw.data(),int(raw.size())),1,&spec,&buf,&len)) {
                check(false,path+" loads");continue;
            }
            SDL_FreeWAV(buf);
            check(spec.freq==22050 && spec.channels==2,path+" is 22050 Hz stereo");
            auto pcm=SoundBankTestAccess::convert(raw,2);
            check(pcm && pcm->size()==size_t(len/2),path+" plays at its own rate and channel count");
            ++tracks;
        }
        check(tracks>0,"retail music tracks found");
    }
    SDL_Quit();
    if(failures)return 1;
    std::cout<<"game music ok\n";
    return 0;
}
