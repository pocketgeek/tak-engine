#include "util/winargv.h"
#include "cartographer/playtest.h"
#include "cartographer/document.h"
#include "net/mappackage.h"
#include "tnt/tnt.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <stdexcept>

static void check(bool good,const char* message) {if(!good)throw std::runtime_error(message);}
static int runTest(int argc,char** argv) try {
    namespace fs=std::filesystem;
    // This executable also stands in for the game, exercising OS argument passing
    // and the full child lifecycle without SDL, assets, or a shell command.
    if(argc>1 && std::string(argv[1])=="--data") {
        check(argc==5 && std::string(argv[3])=="--play-map","launch arguments lost");
        check(fs::u8path(argv[2]).filename()==fs::u8path("data spaces ü & $"),"data path quoting/Unicode changed");
        tak::hpi::Archive snapshot(fs::u8path(argv[4]));
        check(!snapshot.entries().empty(),"child could not read snapshot");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return 0;
    }
    cart::Playtest test;
    tak::tnt::Map map;map.width=map.height=32;map.blocksX=map.blocksY=16;
    map.heights.assign(1024,40);map.features.assign(1024,0xffff);
    map.tileKeys.assign(256,1);map.tileCols.assign(256,0);map.tileRows.assign(256,0);
    const auto files=cart::documentFiles(map,{}, {},{}, {},"Snapshot");
    const auto root=fs::temp_directory_path()/fs::u8path("data spaces ü & $");
    std::string error;
    check(!test.start(root/"nonexistent",root,files,error) && !error.empty() && !test.running(),"missing executable did not fail cleanly");
    check(test.start(fs::absolute(fs::u8path(argv[0])),root,files,error),error.c_str());
    const auto path=test.snapshot();check(fs::is_regular_file(path),"temporary snapshot missing");
    check(!test.start(fs::absolute(fs::u8path(argv[0])),root,files,error),"second concurrent test accepted");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    std::optional<int> result;
    while(!result && std::chrono::steady_clock::now()<deadline) {
        result=test.poll();std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(result && *result==0,"child launch/argument test failed or timed out");
    check(!fs::exists(path.parent_path()) && !test.running(),"finished snapshot not cleaned up");
    check(map.width==32 && files.front().data==map.save(),"playtest modified source document");
    std::cout<<"PASS: temporary playtest snapshot, Unicode arguments, single child, launch failure and cleanup\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}

int main(int argc,char** argv) {
#ifdef _WIN32
    return tak::utf8Main(runTest);
#else
    return runTest(argc,argv);
#endif
}
