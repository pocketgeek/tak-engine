#include "video/stream.h"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
static void require(bool value,const char* message) {if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static uint32_t be24(const unsigned char* p){return uint32_t(p[0])<<16|uint32_t(p[1])<<8|p[2];}
int main(int argc,char** argv) {
    tak::video::StreamConfig config;
    config.encoder=argc>1?argv[1]:"libx264";
    if(config.encoder=="auto")config.encoder.clear();
    auto path=std::filesystem::temp_directory_path()/("tak-stream-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".flv");
    {
        tak::video::Stream stream;
        require(!stream.start(config),"missing key rejected without connection");
        config.key="invalid/secret?key";
        require(!stream.start(config),"URL injection rejected without connection");
        config.width=1;require(!stream.startRecording(config,path.string()),"invalid dimensions rejected");config.width=1280;
        require(!stream.startRecording(config,"rtmps://example.invalid/test"),"recording cannot contact a network destination");
        config.width=8194;require(!stream.startRecording(config,path.string()),"oversized dimensions rejected");
        config.width=1279;require(!stream.startRecording(config,path.string()),"odd dimensions rejected");
        config.width=1280;
        if(argc>2 && std::string(argv[2])=="4k") {config.width=3840;config.height=2160;config.bitrateKbps=30000;}
        if(argc>2 && std::string(argv[2])=="native") {config.width=1582;config.height=934;}
        if(argc>2 && std::string(argv[2])=="wide") {config.width=7680;config.height=2160;config.bitrateKbps=30000;}
        if(argc>3)config.fps=std::atoi(argv[3]);
        config.key.clear();
        require(stream.startRecording(config,path.string()),"start recording");
        require(!stream.startRecording(config,path.string()),"cannot start twice");
        std::vector<uint8_t> pixels(640*480*4);
        for(int y=0;y<480;++y)for(int x=0;x<640;++x){size_t p=(y*640+x)*4;pixels[p]=uint8_t(x/3);pixels[p+1]=uint8_t(y/2);pixels[p+2]=80;pixels[p+3]=255;}
        std::array<int16_t,256*6> audio{};
        int samples=0;
        bool delayed=false;
        const bool overload=argc>2 && std::string(argv[2])=="overload";
        auto begin=std::chrono::steady_clock::now();
        while(std::chrono::steady_clock::now()-begin<(overload?4500ms:2500ms)) {
            require(stream.active(),stream.status().state.c_str());
            if(overload && !delayed && stream.status().frames>=15) {
                stream.testDelayOnce(1300); delayed=true;
            }
            stream.video(pixels.data(),640,480,640*4);
            for(int i=0;i<256;++i) {
                int16_t s=int16_t(std::sin(double(samples++)*6.283185307179586*440/11025)*12000);
                for(int c=0;c<6;++c)audio[i*6+c]=s;
            }
            stream.audio(audio.data(),256,6);
            std::this_thread::sleep_for(23ms);
        }
        auto status=stream.status();
        std::printf("encoder=%s frames=%llu dropped=%llu bytes=%llu\n",status.encoder.c_str(),
                    (unsigned long long)status.frames,(unsigned long long)status.dropped,(unsigned long long)status.bytes);
        if(overload) {require(delayed,"overload injected");require(status.dropped>=30,"late video slots skipped");require(status.state=="RECORDING","encoder stall did not restart stream");}
        require(status.frames>=30,"video frames encoded");require(status.bytes>10000,"packets written");
        uint64_t pausedFrames=0;
        if(overload) {
            // Deterministically stop after the worker has entered an iteration
            // but before audio catch-up. Previously it still encoded a video
            // frame at the new timestamp after cancellation stopped the audio.
            stream.testDelayOnce(10000);
            const auto deadline=std::chrono::steady_clock::now()+5s;
            while(!stream.testDelayInProgress() && std::chrono::steady_clock::now()<deadline)
                std::this_thread::sleep_for(1ms);
            require(stream.testDelayInProgress(),"worker paused before cancellation");
            pausedFrames=stream.status().frames;
            std::this_thread::sleep_for(200ms);
            require(stream.testDelayInProgress(),"worker remains paused at stop boundary");
        }
        auto stop=std::chrono::steady_clock::now();stream.stop();
        require(std::chrono::steady_clock::now()-stop<50ms,"stop does not block UI");
        {
            const auto deadline=std::chrono::steady_clock::now()+5s;
            while(stream.active() && std::chrono::steady_clock::now()<deadline)
                std::this_thread::sleep_for(1ms);
            require(!stream.active(),"cancel wakes paused encoder and finishes drain");
            require(stream.testRetainedVideoBytes()==0,"stopped stream releases video buffers");
            if(overload)require(stream.status().frames==pausedFrames,"no video submitted after stop interrupted audio");
        }
    }
    std::ifstream file(path,std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});
    file.close(); // Windows cannot remove an output while our reader still holds it.
    require(bytes.size()>13 && bytes[0]=='F'&&bytes[1]=='L'&&bytes[2]=='V',"valid FLV header");
    require((bytes[4]&5)==5,"both audio and video advertised");
    size_t pos=13;int videos=0,audios=0,keys=0;uint32_t vt=0,at=0;
    bool vc=false,ac=false;
    while(pos+15<=bytes.size()) {
        unsigned type=bytes[pos];size_t len=be24(&bytes[pos+1]);
        uint32_t ts=be24(&bytes[pos+4])|(uint32_t(bytes[pos+7])<<24);
        require(pos+11+len+4<=bytes.size(),"complete packet");
        auto* p=&bytes[pos+11];
        if(type==9&&len>=5) {
            const bool enhanced=(p[0]&0x80)!=0;
            if(enhanced)require(std::string(reinterpret_cast<const char*>(p+1),4)=="hvc1","HEVC fourcc");
            else require((p[0]&15)==7,"H264 video");
            const int packet=enhanced?(p[0]&15):p[1];
            if(packet==0)vc=true;
            if(packet==1 || (enhanced && packet==3)) {
                require(ts>=vt,"monotonic video timestamp");vt=ts;++videos;
                if(((p[0]>>4)&7)==1)++keys;
            }
        }
        if(type==8&&len>=2) {
            require((p[0]>>4)==10,"AAC audio");
            if(p[1]==0)ac=true;
            if(p[1]==1){require(ts>=at,"monotonic audio timestamp");at=ts;++audios;}
        }
        pos+=11+len+4;
    }
    require(vc&&ac,"both codec configuration headers present");
    require(videos>=30&&audios>=40&&keys>=1,"decodable video and audio packets");
    std::printf("audio/video end_ms=%u/%u\n",vt,at);
    require(std::abs(int(vt)-int(at))<100,"audio/video end times within 100 ms");
    std::printf("file=%s video=%d audio=%d keyframes=%d end_ms=%u/%u\n",path.string().c_str(),videos,audios,keys,vt,at);
    if(argc<2 || (argc>2 && std::string(argv[2])=="overload"))std::filesystem::remove(path);
    path += ".restart";
    // Failure is local, reported without throwing through the host or exposing a key.
    {
        tak::video::Stream stream;
        auto bad=path/"missing"/"output.flv";
        require(stream.startRecording(config,bad.string()),"failed destination scheduled asynchronously");
        auto limit=std::chrono::steady_clock::now()+5s;
        while(stream.active()&&std::chrono::steady_clock::now()<limit)std::this_thread::sleep_for(10ms);
        require(!stream.active()&&stream.status().state.starts_with("FAILED"),"I/O failure reported");
        require(stream.startRecording(config,path.string()),"can restart after failure");stream.stop();
    }
    std::filesystem::remove(path);
    return 0;
}
