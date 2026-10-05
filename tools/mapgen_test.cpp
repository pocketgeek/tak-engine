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
int manaCount(const tak::mapgen::Result& r) {
    int count=0;
    for(auto f:r.map.features)
        if(f<r.map.featureNames.size() && tak::hpi::MountSet::key(r.map.featureNames[f]).find("mana")!=std::string::npos)++count;
    return count;
}
void manaSlider(const tak::hpi::Vfs& vfs) {
    using namespace tak::mapgen;
    for(const auto [size,players]:std::array<std::pair<int,int>,3>{{{256,2},{512,2},{2048,8}}}) {
        int previous=-1;
        for(int density:{0,43,85,128,170,213,255}) {
            Params p;p.widthCells=p.heightCells=uint16_t(size);p.players=uint8_t(players);
            p.waterDensity=p.reliefDensity=p.treeDensity=p.rockDensity=0;p.manaDensity=uint8_t(density);
            const auto r=generate(p,vfs);const int count=manaCount(r),rounds=extraManaRounds(p.manaDensity);
            check(count>=previous,"raising extra mana removed deposits");
            check(count>=players*3 && count<=players*(3+rounds) && count%players==0,"extra mana count violates balanced-round limit");
            if(!density)check(count==players*3,"zero extra mana changed guaranteed home deposits");
            if(size>=512)check(count==players*(3+rounds),"roomy flat map did not honor extra mana setting");
            std::printf("mana slider size=%d players=%d requested=%d actual=%d\n",size,players,rounds,count);
            previous=count;
        }
    }
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
    for(int density=0;density<=255;++density) {
        const int rounds=tak::mapgen::extraManaRounds(uint8_t(density));
        check(rounds>=0 && rounds<=6,"extra mana scale is unbounded");
        if(density)check(rounds>=tak::mapgen::extraManaRounds(uint8_t(density-1)),"extra mana scale is not monotone");
    }
    check(tak::mapgen::extraManaRounds(0)==0 && tak::mapgen::extraManaRounds(128)==3 &&
          tak::mapgen::extraManaRounds(255)==6,"extra mana endpoints/default changed");
    tak::mapgen::Params old;old.formatVer=7;bool refused=false;
    try {tak::mapgen::generate(old,vfs);}catch(const std::runtime_error&) {refused=true;}
    check(refused,"obsolete map recipe silently regenerated with different rules");
    for(int players=2;players<=8;++players) {
        tak::mapgen::Params p;p.players=players;p.seed=0xfedcba9876543210ULL;
        p.waterDensity=p.reliefDensity=0;p.treeDensity=p.rockDensity=p.manaDensity=255;
        const auto r=tak::mapgen::generate(p,vfs);
        const auto repeated=tak::mapgen::generate(tak::mapgen::decodeMapId(tak::mapgen::encodeMapId(p)),vfs);
        check(hash(r)==hash(repeated),"synthetic map not deterministic");
        check(r.waterPercent==0&&r.reliefPatches==0,"dry flat synthetic map changed terrain");
        if(players==8) {
            check(hash(r)==0x6bf977a42891c638ULL,"cross-platform generator golden changed");
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
    manaSlider(vfs);
}
void verify(const tak::mapgen::Params& input,const tak::mapgen::Result& r) {
    const auto p=tak::mapgen::sanitize(input);const auto& m=r.map;
    check(m.width==p.widthCells&&m.height==p.heightCells,"wrong dimensions");
    check(r.starts.size()==p.players,"wrong start count");
    check(m.features.size()==m.heights.size()&&m.heights.size()==size_t(m.width)*m.height,"wrong cell planes");
    check(m.tileKeys.size()==size_t(m.width/2)*(m.height/2),"wrong tile plane");
    check(p.waterDensity||tak::mapgen::automaticWater(p.layout)||r.waterPercent==0,"zero water produced water");
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
    check((p.layout!=tak::mapgen::Islands&&p.layout!=tak::mapgen::Ports)||r.harbors.size()==p.players,"missing island harbors");
}
}
int main(int argc,char** argv) {
    try {
        using namespace tak::mapgen;
        for(int version:{1,2,3,4,5,6,7,8})for(int layout=0;layout<3;++layout)for(int players=2;players<=8;++players) {
            Params p;p.formatVer=version;p.layout=layout;p.players=players;p.seed=0xfedcba9876543210ULL;
            p.widthCells=768;p.heightCells=640;
            const auto id=encodeMapId(p);const auto d=decodeMapId(id);const auto expected=sanitize(p);
            check(d.seed==p.seed&&d.players==p.players&&d.widthCells==expected.widthCells&&d.heightCells==expected.heightCells,"seed codec mismatch");
            if(version>=2)check(d.treeDensity==p.treeDensity&&d.rockDensity==p.rockDensity,"density codec mismatch");
            if(version>=3)check(d.layout==p.layout,"layout codec mismatch");
        }
        // A high-bit seed, invalid input and legacy identifiers must not wrap
        // dimensions or allow a new layout to reinterpret an old seed.
        for(int layout=Maze;layout<kLayouts;++layout) {
            Params p;p.layout=layout;p.name="Themed";
            const auto d=decodeMapId(encodeMapId(p));
            check(d.layout==layout && themedLayout(d.mapType)==layout,"theme codec/world mismatch");
            p.formatVer=5;check(sanitize(p).layout<=Islands,"legacy recipe accepts new layout");
        }
        Params invalid;invalid.widthCells=0;invalid.heightCells=65535;invalid.players=255;invalid.layout=255;
        auto sane=sanitize(invalid);check(sane.players==8&&sane.widthCells>=384&&sane.heightCells==2048,"sanitize bounds");
        Params named; named.name = "  My Lake-7_  ";
        check(decodeMapId(encodeMapId(named)).name == "My Lake-7_", "map name round trip");
        check(friendlyLabel(named) == "My Lake-7_", "map name display");
        named.name = "../bad;[name]\\test";
        check(sanitize(named).name == "badnametest", "unsafe map name");
        named.name = std::string(100, 'a');
        check(decodeMapId(encodeMapId(named)).name.size() == 24, "map name length");
        named.formatVer = 4;
        check(decodeMapId(encodeMapId(named)).name.empty(), "legacy recipe gained name");
        syntheticMaps();
        if(argc<2){std::puts("mapgen codec/bounds passed (retail sweep takes a data path)");return 0;}
        const auto vfs=tak::hpi::mountRetailRoot(argv[1]);
        if(argc>2 && std::string(argv[2])=="--mana") {
            Params cramped;cramped.mapType=Veruna;cramped.players=7;cramped.widthCells=cramped.heightCells=384;
            const auto limited=generate(cramped,vfs);verify(cramped,limited);
            check(manaCount(limited)%7==0,"omitted expansion left a partial resource round");
            check(hash(limited)==hash(generate(cramped,vfs)),"omitted expansion is not deterministic");
            for(int world=0;world<kMapTypes;++world)for(int layout:{int(Mainland),int(themedLayout(uint8_t(world)))}) {
                int previous=-1;
                for(int density:{0,128,255}) {
                    Params p;p.mapType=uint8_t(world);p.layout=uint8_t(layout);p.widthCells=p.heightCells=1024;
                    p.players=2;p.manaDensity=uint8_t(density);
                    const auto r=generate(p,vfs);verify(p,r);const int count=manaCount(r);
                    check(count>=previous,"authored terrain reduced mana when slider increased");
                    check(count>=6 && count<=2*(3+extraManaRounds(p.manaDensity)),"authored terrain ignored extra mana limit");
                    std::printf("mana world=%d layout=%d density=%d count=%d\n",world,layout,density,count);
                    previous=count;
                }
                check(previous>6,"roomy authored map never added extra mana");
            }
            std::puts("extra mana slider passed");return 0;
        }
        if(argc>2&&(std::string(argv[2])=="--naval"||std::string(argv[2])=="--ports")) {
            for(bool crusades:{false,true}) {
                tak::sim::TypeRegistry registry;tak::sim::setupRegistry(registry,vfs,crusades);
                const auto* fort=registry.find("verasy");check(fort,"Sea Fort definition");
                for(int world=0;world<5;++world) {
                    const bool ports=std::string(argv[2])=="--ports";
                    if(ports && world!=Veruna)continue;
                    Params p;p.mapType=world;p.layout=ports?Ports:Islands;p.players=2;
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
        // Never place grass/cobbles relief directly into another ground family.
        for(int world:{int(Taros),int(Veruna)}) {
            std::vector<uint32_t> incompatible;
            for(const auto& path:vfs.list(world==Taros?"sections/taros/low specials":"sections/veruna/low specials")) {
                const auto key=tak::hpi::MountSet::key(path);
                if(!(key.ends_with(".tnt") && ((world==Veruna && key.find("/breaker")!=std::string::npos) ||
                    key.ends_with("/unique23.tnt")||key.ends_with("/unique25.tnt")||key.ends_with("/unique27.tnt"))))continue;
                auto piece=tak::tnt::Map::load(vfs.read(path));
                incompatible.push_back(piece.tileKeys[piece.tileKeys.size()/2]);
            }
            Params p;p.mapType=world;p.widthCells=p.heightCells=768;p.reliefDensity=255;p.waterDensity=0;
            const auto r=generate(p,vfs);verify(p,r);
            for(auto key:r.map.tileKeys) check(std::find(incompatible.begin(),incompatible.end(),key)==incompatible.end(),"incompatible relief surface used");
        }
        // Lakes may meet the edge; there is no guaranteed dry rim.
        int edgeWet=0;
        for(int seed=1;seed<=5;++seed) {
            Params p;p.layout=Lakes;p.waterDensity=255;p.seed=seed;
            const auto r=generate(p,vfs);verify(p,r);const auto& m=r.map;
            for(int x=0;x<m.width;++x)edgeWet+=m.heights[x]<m.seaLevel || m.heights[size_t(m.height-1)*m.width+x]<m.seaLevel;
            for(int z=0;z<m.height;++z)edgeWet+=m.heights[size_t(z)*m.width]<m.seaLevel || m.heights[size_t(z)*m.width+m.width-1]<m.seaLevel;
        }
        check(edgeWet>0,"lakes still have a guaranteed dry rim");
        if(argc>2&&std::string(argv[2])=="--themes") {
            for(int layout=Maze;layout<kLayouts;++layout)for(int players:{2,3,4,5,6,7,8})for(int seed=1;seed<=3;++seed) {
                Params p;p.layout=layout;p.players=players;p.seed=seed;
                if(seed==2)p.treeDensity=p.rockDensity=p.manaDensity=p.reliefDensity=0;
                if(seed==3)p.treeDensity=p.rockDensity=p.manaDensity=p.reliefDensity=255;
                p=sanitize(p);
                const auto r=generate(p,vfs);verify(p,r);
                check(hash(r)==hash(generate(decodeMapId(encodeMapId(p)),vfs)),"themed recipe not deterministic");
                if(layout==Maze||(layout==Highlands&&p.reliefDensity)) check(*std::max_element(r.map.heights.begin(),r.map.heights.end())>200,"themed uplands absent");
                if(layout==Maze) {
                    const auto& m=r.map;
                    const auto low=[&](int x,int z){return m.heights[size_t(z)*m.width+x]<200;};
                    for(int x=96;x<=m.width-96;x+=128)
                        check(low(x,0)&&low(x,m.height-1),"maze north/south exits blocked");
                    for(int z=96;z<=m.height-96;z+=128)
                        check(low(0,z)&&low(m.width-1,z),"maze west/east exits blocked");
                }
                if(layout==Ports||layout==Riverlands) check(r.waterPercent>0,"themed waterways absent");
                std::printf("theme=%s players=%d seed=%d water=%d harbors=%zu passed\n",layoutName(layout),players,seed,r.waterPercent,r.harbors.size());std::fflush(stdout);
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
