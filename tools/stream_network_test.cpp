// Test-only RTMPS sink injection; never compiled into takclient.
#include "video/stream.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
int main(int argc,char** argv){
    if(argc!=4 && argc!=5)return 2;
    tak::video::Stream stream;tak::video::StreamConfig c;c.encoder=argc>4?argv[4]:"libx264";
    if(!stream.startTestEndpoint(c,argv[1],argv[2]))return 3;
    std::vector<uint8_t> pixels(320*240*4,100);
    std::string previous;
    auto start=std::chrono::steady_clock::now();
    while(std::chrono::steady_clock::now()-start<std::chrono::seconds(std::atoi(argv[3]))) {
        stream.video(pixels.data(),320,240,1280);
        auto s=stream.status();
        if(s.state!=previous){std::printf("%s frames=%llu\n",s.state.c_str(),(unsigned long long)s.frames);std::fflush(stdout);previous=s.state;}
        if(!s.active)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    stream.stop();
}
