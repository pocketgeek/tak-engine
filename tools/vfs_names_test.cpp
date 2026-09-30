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
        { // Close mapped archives before deleting their directory on Windows.
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
            // Downloaded-map terrain may replace stock keys for that map, but
            // generated maps must see the retail tile (explicit user overrides win).
            const auto writeTile=[&](const char* dir,const char* value) {
                fs::create_directories(root/dir/"terrain");
                std::ofstream f(root/dir/"terrain"/"12345678.jpg");f<<value;
            };
            writeTile("base","stock");writeTile("download","map");writeTile("override","user");
            tak::hpi::Vfs scoped;
            scoped.addLayer(tak::hpi::MountSet(root/"base"));
            scoped.addLayer(tak::hpi::MountSet(root/"download"),"",true);
            const auto tile=[&](bool stock) {auto b=scoped.read("terrain/12345678.jpg",stock);return std::string(b.begin(),b.end());};
            require(tile(false)=="map"&&tile(true)=="stock","generated terrain ignores map reskins");
            scoped.addLayer(tak::hpi::MountSet(root/"override"));
            require(tile(false)=="user"&&tile(true)=="user","explicit user terrain override retained");
            // Only the five case-insensitive loose Crusades art files are mounted.
            const auto artRoot = root / "crusades-root";
            fs::create_directories(artRoot / "bOnEyArDs" / "mEtAgAmE");
            const auto beforeArt = tak::hpi::gameplayHash(tak::hpi::mountRetailRoot(artRoot));
            for (const char* file : {"DaRiEn.DeF", "BORDERS.PNG", "HonorMap.png", "terrorMap.PNG", "ContestedMap.png",
                                     "weapons.tdf", "evil.tnt", "PreInit.jje"}) {
                std::ofstream f(artRoot / "bOnEyArDs" / "mEtAgAmE" / file); f << "synthetic";
            }
            {
                auto artVfs = tak::hpi::mountRetailRoot(artRoot, tak::hpi::OverridePolicy::None);
                for (const char* file : {"Darien.def", "Borders.png", "HonorMap.png", "TerrorMap.png", "ContestedMap.png"}) {
                    const auto path = std::string("Boneyards/Metagame/") + file;
                    require(artVfs.has(path, true), "campaign cosmetic case-insensitive mount");
                    require(artVfs.read(path, true).size() == 9, "campaign cosmetic read");
                }
                require(artVfs.list("Boneyards/Metagame", true).size() == 5, "exact cosmetic allowlist");
                require(!artVfs.has("Boneyards/Metagame/weapons.tdf"), "adjacent TDF hidden");
                require(!artVfs.has("Boneyards/Metagame/evil.tnt"), "adjacent map hidden");
                require(!artVfs.has("Boneyards/Metagame/PreInit.jje"), "adjacent script hidden");
                require(tak::hpi::gameplayHash(artVfs) == beforeArt, "cosmetic mount preserves gameplay hash");
            }
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
        }
        fs::remove_all(root);
        std::cout<<"PASS: Unicode loose maps and byte-preserved archive maps in C locale\n";
    } catch(const std::exception& e) {
        std::cerr<<"FAIL: "<<e.what()<<'\n';
        std::error_code ec;fs::remove_all(root,ec);return 1;
    }
}
