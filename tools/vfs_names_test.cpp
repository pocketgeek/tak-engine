#include "hpi/hpi.h"
#include "util/virtualpath.h"
#include "sim/matchsetup.h"
#include <chrono>
#include <clocale>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void require(bool b,const char* m) { if(!b) throw std::runtime_error(m); }
int main(int argc,char** argv) {
    namespace fs=std::filesystem;
    std::setlocale(LC_ALL,"C"); // Match the default Windows release process locale.
    const auto root=fs::temp_directory_path()/fs::u8path("tak-vfs-\xc3\xa9-\xe6\xb0\xb4-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root/"Maps");
        const std::string unicode="Maps/\xc3\x89tang \xe6\xb0\xb4.tnt";
        const std::string legacy="Maps/Legacy \xe9.tnt";
        const std::string archived="Maps/Archive \xe6\xb0\xb4.tnt";
        const std::vector<uint8_t> data{'m','a','p'};
        { std::ofstream f(root/fs::u8path(unicode),std::ios::binary); f.write("map",3); }
        { auto bytes=tak::hpi::pack({{legacy,data},{archived,data}});
          std::ofstream f(root/fs::u8path("\xe6\xb0\xb4.hpi"),std::ios::binary);
          f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()); }
        tak::hpi::Vfs vfs;
        vfs.addLayer(tak::hpi::MountSet(root));
        auto maps=tak::hpi::listMaps(vfs);
        require(maps.size()==3,"all Unicode and legacy maps listed");
        for(const auto& path:{unicode,legacy,archived}) {
            const auto name=tak::vpath::stem(path);
            require(tak::hpi::findMap(vfs,name)==path,"map lookup preserves name bytes");
            require(vfs.has(path),"map exists");
            require(vfs.read(path)==data,"map read succeeds");
            
            require(tak::vpath::extension(tak::vpath::replaceExtension(path,".ota"))==".ota","metadata sibling");
        }
        require(tak::vpath::stem("Maps\\a.b.tnt")=="a.b","backslash path");
        require(tak::vpath::extension("dir.dot/file").empty(),"directory dot is not extension");
        require(tak::vpath::stem(".hidden")==".hidden","dotfile stem");
        if(argc>1) {
            auto real=tak::hpi::mountRetailRoot(fs::u8path(argv[1]));
            auto all=tak::hpi::listMaps(real);
            unsigned checked=0;
            for(const auto& [name,path]:all) {
                if(name.find_first_of("\xba\xe9") == std::string::npos) continue;
                require(tak::hpi::findMap(real,name)==path,"installed map lookup");
                require(!real.read(path).empty(),"installed map data readable");
                require(!tak::sim::parseStartPositions(real,path).empty(),"installed map start positions");
                ++checked;
            }
            require(checked>=2,"installed legacy-named maps covered");
            std::cout<<"Installed map catalog: "<<all.size()<<" maps, "<<checked<<" legacy-named maps read\n";
        }
        fs::remove_all(root);
        std::cout<<"PASS: Unicode loose maps and byte-preserved archive maps in C locale\n";
    } catch(const std::exception& e) {
        std::cerr<<"FAIL: "<<e.what()<<'\n';
        std::error_code ec;fs::remove_all(root,ec);return 1;
    }
}
