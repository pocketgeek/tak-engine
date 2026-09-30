#include "tnt/mapgen.h"
#include "hpi/hpi.h"
#include "sim/sim.h"
#include "sim/matchsetup.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
uint64_t hash(const tak::mapgen::Result& r) {
    uint64_t h=1469598103934665603ULL;
    const auto mix=[&](uint64_t n){for(int i=0;i<8;++i){h^=(n>>(i*8))&255;h*=1099511628211ULL;}};
    const auto& m=r.map;mix(m.width);mix(m.height);mix(m.seaLevel);
    for(auto n:m.heights)mix(n);
    for(auto n:m.features)mix(n);
    for(auto n:m.tileKeys)mix(n);
    for(auto n:m.tileCols)mix(n);
    for(auto n:m.tileRows)mix(n);
    for(const auto& s:m.featureNames){for(unsigned char c:s)mix(c);mix(0);}
    for(auto [x,z]:r.starts){mix(x);mix(z);}
    return h;
}
void syntheticMaps() {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("tak-mapgen-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { fs::path path;~Cleanup(){std::error_code ec;fs::remove_all(path,ec);} } cleanup{root};
    fs::create_directories(root/"features"/"aramon");
    {
        std::ofstream out(root/"features"/"aramon"/"fixture.tdf");
        for(const auto [kind,count]:std::array<std::pair<const char*,int>,4>{{{"Tree",10},{"Rock",7},{"Henge",9},{"Mana",3}}})
            for(int i=1;i<=count;++i) {
                char name[32];std::snprintf(name,sizeof name,"Ara%s%02d",kind,i);
                const bool mana=std::string(kind)=="Mana";
                out<<"["<<name<<"] {\nfootprintx="<<(mana?2:3)<<";\nfootprintz="<<(mana?2:4)
                   <<";\nblocking="<<(!mana)<<"; }\n";
            }
    }
    tak::hpi::Vfs vfs;vfs.addLayer(tak::hpi::MountSet(root));
    for(int version : {3,4}) for(int players=2;players<=8;++players) {
        tak::mapgen::Params p;p.players=players;p.seed=0xfedcba9876543210ULL;
        p.formatVer=version; // Keep the previous recipe golden stable.
        p.waterDensity=p.reliefDensity=0;p.treeDensity=p.rockDensity=p.manaDensity=255;
        const auto r=tak::mapgen::generate(p,vfs);
        const auto repeated=tak::mapgen::generate(tak::mapgen::decodeMapId(tak::mapgen::encodeMapId(p)),vfs);
        check(hash(r)==hash(repeated),"synthetic map not deterministic");
        check(r.waterPercent==0&&r.reliefPatches==0,"dry flat synthetic map changed terrain");
        if(players==8 && version==3) {
            check(hash(r)==0x9258a896baaf4285ULL,"cross-platform generator golden changed");
            std::printf("synthetic generator golden: %016llx\n",(unsigned long long)hash(r));
        }
        // Non-square feature footprints are deliberately unlike the old guessed
        // sizes. No two procedural anchors may claim the same cell.
        std::vector<uint8_t> occupied(r.map.heights.size());
        for(int z=0;z<r.map.height;++z)for(int x=0;x<r.map.width;++x) {
            const auto f=r.map.features[size_t(z)*r.map.width+x];if(f>=r.map.featureNames.size())continue;
            const bool mana=r.map.featureNames[f].find("Mana")!=std::string::npos;
            for(int dz=0;dz<(mana?2:4);++dz)for(int dx=0;dx<(mana?2:3);++dx) {
                check(x+dx<r.map.width&&z+dz<r.map.height,"feature extends off map");
                auto& cell=occupied[size_t(z+dz)*r.map.width+x+dx];
                if(cell) {
                    std::fprintf(stderr,"overlap players=%d feature=%s origin=%d,%d at=%d,%d previous=%s\n",players,
                        r.map.featureNames[f].c_str(),x,z,x+dx,z+dz,r.map.featureNames[cell-1].c_str());
                    throw std::runtime_error("generated feature footprints overlap");
                }
                cell=uint8_t(f+1);
            }
        }
    }
}
void verify(const tak::mapgen::Params& input,const tak::mapgen::Result& r) {
    const auto p=tak::mapgen::sanitize(input);const auto& m=r.map;
    check(m.width==p.widthCells&&m.height==p.heightCells,"wrong dimensions");
    check(r.starts.size()==p.players,"wrong start count");
    check(m.features.size()==m.heights.size()&&m.heights.size()==size_t(m.width)*m.height,"wrong cell planes");
    check(m.tileKeys.size()==size_t(m.width/2)*(m.height/2),"wrong tile plane");
    check(p.waterDensity||p.layout==tak::mapgen::Islands||r.waterPercent==0,"zero water produced water");
    check(p.layout!=tak::mapgen::Lakes||!p.waterDensity||r.waterPercent>0,"lake layout has no water");
    check(p.reliefDensity||r.reliefPatches==0,"zero relief produced hills");
    for(size_t i=0;i<r.starts.size();++i) {
        const auto [x,z]=r.starts[i];
        for(size_t j=0;j<i;++j) {
            const int dx=x-r.starts[j].first,dz=z-r.starts[j].second;
            check(dx*dx+dz*dz>=72*72,"starts too close");
        }
        const auto level=m.heights[size_t(z)*m.width+x];
        check(level>m.seaLevel,"underwater start");
        for(int dz=-12;dz<12;++dz)for(int dx=-12;dx<12;++dx) {
            const size_t c=size_t(z+dz)*m.width+x+dx;
            check(m.heights[c]==level,"base isn't flat");
            check(m.features[c]==0xffff,"base has feature anchors");
        }
        const std::array<std::pair<int,int>,3> offsets={{{-24,-24},{24,-24},{0,28}}};
        for(int tier=0;tier<3;++tier) {
            const auto [dx,dz]=offsets[tier];const auto f=m.features[size_t(z+dz)*m.width+x+dx];
            check(f<m.featureNames.size(),"missing home mana");
            const auto name=tak::hpi::MountSet::key(m.featureNames[f]);
            check(name.ends_with("mana0"+std::to_string(tier+1)),"wrong home mana tier");
        }
    }
    int mana=0;
    for(auto f:m.features) {
        if(f>=0xfffb)continue;
        check(f<m.featureNames.size(),"invalid feature index");
        if(tak::hpi::MountSet::key(m.featureNames[f]).find("mana")!=std::string::npos)++mana;
    }
    if (p.formatVer>=4) for (int z=0;z<m.height;++z) for (int x=0;x<m.width;++x) {
        const auto f=m.features[size_t(z)*m.width+x];
        if (f>=m.featureNames.size() || tak::hpi::MountSet::key(m.featureNames[f]).find("mana")==std::string::npos) continue;
        bool ruins=false;
        for (int dz=-24;dz<=24 && !ruins;++dz) for (int dx=-24;dx<=24;++dx) {
            const int nx=x+dx,nz=z+dz;
            if(nx<0||nz<0||nx>=m.width||nz>=m.height)continue;
            const auto r=m.features[size_t(nz)*m.width+nx];
            if(r<m.featureNames.size() && tak::hpi::MountSet::key(m.featureNames[r]).find("henge")!=std::string::npos) { ruins=true; break; }
        }
        check(ruins,"mana spot has no surrounding ruins");
    }
    check(mana>=p.players*3&&mana%p.players==0,"unequal resource rounds");
    check(p.layout!=tak::mapgen::Islands||r.harbors.size()==p.players,"missing island harbors");
}
}
int main(int argc,char** argv) {
    try {
        using namespace tak::mapgen;
        for(int version:{1,2,3,4})for(int layout=0;layout<3;++layout)for(int players=2;players<=8;++players) {
            Params p;p.formatVer=version;p.layout=layout;p.players=players;p.seed=0xfedcba9876543210ULL;
            p.widthCells=768;p.heightCells=640;
            const auto id=encodeMapId(p);const auto d=decodeMapId(id);const auto expected=sanitize(p);
            check(d.seed==p.seed&&d.players==p.players&&d.widthCells==expected.widthCells&&d.heightCells==expected.heightCells,"seed codec mismatch");
            if(version>=2)check(d.treeDensity==p.treeDensity&&d.rockDensity==p.rockDensity,"density codec mismatch");
            if(version>=3)check(d.layout==p.layout,"layout codec mismatch");
        }
        // A high-bit seed, invalid input and legacy identifiers must not wrap
        // dimensions or allow a new layout to reinterpret an old seed.
        Params invalid;invalid.widthCells=0;invalid.heightCells=65535;invalid.players=255;invalid.layout=255;
        auto sane=sanitize(invalid);check(sane.players==8&&sane.widthCells>=384&&sane.heightCells==2048,"sanitize bounds");
        syntheticMaps();
        if(argc<2){std::puts("mapgen codec/bounds passed (retail sweep takes a data path)");return 0;}
        const auto vfs=tak::hpi::mountRetailRoot(argv[1]);
        if(argc>2&&std::string(argv[2])=="--naval") {
            for(bool crusades:{false,true}) {
                tak::sim::TypeRegistry registry;tak::sim::setupRegistry(registry,vfs,crusades);
                const auto* fort=registry.find("verasy");check(fort,"Sea Fort definition");
                for(int world=0;world<5;++world) {
                    Params p;p.mapType=world;p.layout=Islands;p.players=2;
                    const auto generated=generate(p,vfs);
                    for(const auto& name:registry.buildable("verasy")) {
                        const auto* ship=registry.find(name);check(ship,"ship definition");
                        tak::sim::World simulation;simulation.setVisPlayer(-1);
                        tak::sim::MatchConfig cfg;cfg.vfs=&vfs;cfg.mapPath=encodeMapId(p);
                        cfg.slots={{true,2,0,1.f,false,false}};
                        tak::sim::setupMatch(simulation,registry,cfg);
                        for(const auto [x,z]:generated.harbors)
                            check(simulation.canPlace(fort,float(x*16),float(z*16)),"harbor cannot fit actual Sea Fort yard");
                        const auto [x,z]=generated.harbors[0];
                        const int id=simulation.spawn(fort,float(x*16),float(z*16),std::nullopt,0);
                        simulation.train(id,ship,2);
                        for(int tick=0;tick<18000&&!simulation.unit(id)->buildQueue.empty();++tick) {
                            simulation.player(0).mana=100000;simulation.tick(1.f/30);
                        }
                        int complete=0;
                        for(const auto& u:simulation.units())complete+=u.alive()&&u.type==ship&&!u.underConstruction;
                        std::printf("naval world=%d balance=%d ship=%s complete=%d\n",world,crusades,name.c_str(),complete);
                        check(complete==2&&simulation.unit(id)->buildQueue.empty(),"generated harbor ship production failed");
                    }
                }
            }
            std::puts("generated-map naval production passed");return 0;
        }
        if (argc>2 && std::string(argv[2])=="--large") {
            for(int world=0;world<5;++world) for(int layout=0;layout<3;++layout) {
                Params p;p.widthCells=p.heightCells=2048;p.players=8;p.mapType=world;p.layout=layout;
                const auto begin=std::chrono::steady_clock::now();
                const auto r=generate(p,vfs); verify(p,r);
                check(hash(r)==hash(generate(decodeMapId(encodeMapId(p)),vfs)),"large map recipe changed");
                std::printf("large map world=%d layout=%d hash=%016llx %.2fs\n",world,layout,
                    (unsigned long long)hash(r),std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count());
                std::fflush(stdout);
            }
            return 0;
        }
        const bool sweep=argc>2&&std::string(argv[2])=="--sweep";
        const int seeds=sweep?5:1;int count=0,failures=0,patches=0;
        const auto begin=std::chrono::steady_clock::now();
        for(int world=0;world<5;++world)for(int layout=0;layout<3;++layout)
        for(int players=2;players<=8;++players)for(int seed=1;seed<=seeds;++seed)
        for(int size:(sweep?std::vector<int>{256,512,768}:std::vector<int>{384}))
        for(int density:(sweep?std::vector<int>{0,128,255}:std::vector<int>{128})) {
            Params p;p.mapType=world;p.layout=layout;p.players=players;p.seed=seed;
            p.widthCells=p.heightCells=size;
            p.treeDensity=p.rockDensity=p.manaDensity=p.waterDensity=p.reliefDensity=density;
            try {
                const auto r=generate(p,vfs);verify(p,r);patches+=r.reliefPatches;
                if(argc>2&&std::string(argv[2])=="--hashes")
                    std::printf("map hash world=%d layout=%d players=%d seed=%d: %016llx\n",
                        world,layout,players,seed,(unsigned long long)hash(r));
                if(!sweep||count%97==0)check(hash(r)==hash(generate(decodeMapId(encodeMapId(p)),vfs)),"non-repeatable map");
                if(argc>3&&seed==1&&players==3&&size==512&&density==128) {
                    const auto bytes=r.map.save();
                    const auto file=std::string(argv[3])+"/world"+std::to_string(world)+"-layout"+std::to_string(layout)+".tnt";
                    std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
                }
                ++count;
            } catch(const std::exception& e) {
                std::printf("FAIL world=%d layout=%d players=%d seed=%d size=%d density=%d: %s\n",world,layout,players,seed,size,density,e.what());
                ++failures;
            }
            std::fflush(stdout);
        }
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
        std::printf("mapgen: %d passed, %d failed, %d authored relief patches; %.3fs\n",count,failures,patches,seconds);
        return failures?1:0;
    } catch(const std::exception& e){std::fprintf(stderr,"mapgen: %s\n",e.what());return 1;}
}
