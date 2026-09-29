#include "cartographer/savejob.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
static void check(bool good,const char* message) {if(!good)throw std::runtime_error(message);}
static std::vector<uint8_t> read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};
}
int main() try {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("tak-savejob-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    struct Cleanup {fs::path p;~Cleanup(){std::error_code error;fs::remove_all(p,error);}} cleanup{root};
    cart::SaveSnapshot source;auto& map=source.map;
    map.width=map.height=64;map.blocksX=map.blocksY=32;
    map.heights.assign(4096,40);map.features.assign(4096,0xffff);
    map.tileKeys.assign(1024,0);map.tileCols.assign(1024,0);map.tileRows.assign(1024,0);
    source.metadata.missionDescription="Snapshot before editing";
    source.destination=root/"Saved map.kmp";
    auto progress=std::make_shared<cart::SaveProgress>();
    auto job=cart::saveInBackground(source,progress);
    source.metadata.missionDescription="Later edits";source.map.heights[0]=70;
    check(job.get().ok,"background save failed");
    check(progress->phase==cart::SaveProgress::Finished,"save completion not reported");
    const auto first=read(source.destination);
    tak::hpi::Archive saved(source.destination);bool terrain=false,description=false;
    for(const auto& entry:saved.entries()) {
        if(entry.path.ends_with(".tnt"))terrain=tak::tnt::Map::load(saved.read(entry)).heights[0]==40;
        if(entry.path.ends_with(".ota")) {const auto bytes=saved.read(entry);description=tak::tnt::Scenario::parse(std::string(bytes.begin(),bytes.end())).missionDescription=="Snapshot before editing";}
    }
    check(terrain && description,"worker read changed source instead of its snapshot");
    progress=std::make_shared<cart::SaveProgress>();progress->cancel=true;
    const auto cancelled=cart::saveInBackground(source,progress).get();
    check(!cancelled.ok && cancelled.error=="Save cancelled","cancellation ignored");
    check(read(source.destination)==first && !fs::exists(source.destination.string()+".bak"),"cancelled save modified original or backup");
    check(cart::saveInBackground(source,std::make_shared<cart::SaveProgress>()).get().ok,"replacement save failed");
    check(read(source.destination.string()+".bak")==first,"background replacement lost previous save");
    source.bundle=false;source.destination=root/"Loose map.tnt";
    check(cart::saveInBackground(source,std::make_shared<cart::SaveProgress>()).get().ok,"background loose export failed");
    check(fs::exists(root/"Loose map.crt") && fs::exists(root/"Loose map.editor") && fs::exists(root/"Loose map.tdf"),"loose export dropped companions");
    const auto previous=read(source.destination);
    fs::remove(root/"Loose map.crt");fs::create_directory(root/"Loose map.crt");
    source.map.heights[0]=90;
    auto failed=cart::saveInBackground(source,std::make_shared<cart::SaveProgress>()).get();
    check(!failed.ok && !failed.error.empty() && read(source.destination)==previous,"failed export replaced part of the document");
    for(const auto& entry:fs::directory_iterator(root))check(!entry.path().filename().string().starts_with(".tak-save-"),"save leaked staging directory");
    std::cout<<"PASS: background immutable snapshots, cancellation, backup preservation and failed-export atomicity\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
