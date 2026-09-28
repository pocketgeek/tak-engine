#include "net/mappackage.h"
#include "net/client.h"
#include "sim/matchsetup.h"
#include "tnt/mapgen.h"
#include "tnt/tnt.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>

using namespace tak;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
template<class F> static void rejects(F f, const char* why) {
    bool failed = false; try { f(); } catch (const std::exception&) { failed = true; } check(failed, why);
}
static void write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
int main(int argc, char** argv) try {
    if (argc >= 5 && std::string(argv[1]) == "--network") {
        const auto port = uint16_t(std::stoi(argv[2]));
        if (argc == 5) {
            auto original = hpi::mountRetailRoot(argv[3], hpi::OverridePolicy::None);
            auto package = net::maps::build(original, "Ulasem Arena");
            std::vector<hpi::PackFile> entries;
            for (const auto& [name,data] : *package->files) {
                std::string path = name;
                if (name == package->mapPath) path = "kmap/transfer test.tnt";
                else if (name.ends_with(".ota")) path = "kmap/transfer test.ota";
                if (name == package->mapPath) {
                    auto m=tnt::Map::load(data);
                    m.features[size_t(m.width)*10+10]=uint16_t(m.featureNames.size());
                    m.featureNames.push_back("transfer_tree");
                    entries.push_back({path,m.save()});
                } else entries.push_back({path,data});
            }
            const std::string feature="[transfer_tree]\n{\nblocking=1;\nfootprintx=2;\nfootprintz=2;\nreclaimable=1;\nenergy=75;\n}\n";
            entries.push_back({"features/transfer-test.tdf",{feature.begin(),feature.end()}});
            write(std::filesystem::path(argv[3])/"Maps"/"transfer-test.kmp",hpi::pack(entries));
            if (std::getenv("TAK_MAP_TEST_MISMATCH")) {
                for (auto& entry:entries) if(entry.path.ends_with(".tnt")) {
                    auto m=tnt::Map::load(entry.data); m.heights[100] ^= 1; entry.data=m.save();
                }
                write(std::filesystem::path(argv[4])/"Maps"/"different-copy.kmp",hpi::pack(entries));
            }
        }
        hpi::Vfs hostData = hpi::mountRetailRoot(argv[3], hpi::OverridePolicy::None);
        hpi::Vfs peerData = hpi::mountRetailRoot(argv[4], hpi::OverridePolicy::None);
        net::MpClient host, peer, spectator;
        host.setMapRoot(argv[3]); peer.setMapRoot(argv[4]); spectator.setMapRoot(std::getenv("TAK_MAP_TEST_SPECTATOR_ROOT") ? std::getenv("TAK_MAP_TEST_SPECTATOR_ROOT") : argv[4]);
        host.setDataHash(hpi::gameplayHash(hostData));
        peer.setDataHash(hpi::gameplayHash(peerData)); spectator.setDataHash(peer.dataHash());
        check(host.connect("127.0.0.1", port, "maphost"), "host connection");
        check(peer.connect("127.0.0.1", port, "mappeer"), "peer connection");
        bool created = false, joined = false, readied = false, started = false, late = false;
        bool hostLoaded = false, peerLoaded = false, spectatorLoaded = false, spectateSent = false;
        uint32_t ht=0, pt=0, st=0;
        std::map<uint32_t,uint64_t> hashes;
        sim::World wh, wp, ws; sim::TypeRegistry reg;
        sim::setupRegistry(reg, peerData, true);
        const std::string mapId = argc > 5 ? argv[5] : "Transfer Test";
        auto setup = [&](net::MpClient& client, hpi::Vfs& base, sim::World& world) {
            auto room = client.startRoom();
            hpi::Vfs view(&base);
            if (client.mapPackage()) view.setMapFiles(client.mapPackage()->files);
            sim::MatchConfig cfg; cfg.vfs = &view;
            cfg.mapPath = client.mapPackage() ? client.mapPackage()->mapPath : hpi::findMap(view, room.mapId);
            cfg.startSeed = client.startSeed(); cfg.randomStarts = room.opts.randomStarts;
            cfg.unitCap = room.opts.unitCap;
            cfg.slots.resize(2); cfg.slots[0] = {true,0,0,1,false,false}; cfg.slots[1] = {true,1,1,1,false,false};
            sim::setupMatch(world, reg, cfg); client.reportLoaded(hpi::gameplayHash(base));
        };
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        while (std::chrono::steady_clock::now() < until) {
            check(host.poll(), host.error().c_str()); check(peer.poll(), peer.error().c_str());
            if (!created && host.state() == net::MpClient::State::Lobby) {
                net::GameOptions opts; opts.crusades = 1; opts.overridePolicy = 0;
                host.createGame("map transfer test", "", mapId, opts, 2); created = true;
            }
            if (!joined && host.room().id && peer.state() == net::MpClient::State::Lobby) {
                peer.joinGame(host.room().id, ""); joined = true;
            }
            if (!readied && peer.room().id) {
                // START must remain blocked even when human READY arrived first.
                host.setSlot(0,1,0,0,0,1); peer.setSlot(1,1,1,1,1,1); readied = true;
            }
            if (!started && readied && host.room().mapsReady && host.room().slots[1].ready) {
                host.startGame(); started = true;
            }
            if (!hostLoaded && host.starting()) { setup(host,hostData,wh); hostLoaded = true; }
            if (!peerLoaded && peer.starting()) { setup(peer,peerData,wp); peerLoaded = true; }
            if (hostLoaded && peerLoaded && !late) {
                check(wh.stateHash() == wp.stateHash(), "different initial map worlds");
                check(spectator.connect("127.0.0.1", port, "lateviewer"), "spectator connect"); late = true;
            }
            if (late) {
                check(spectator.poll(), spectator.error().c_str());
                if (!spectateSent && spectator.state() == net::MpClient::State::Lobby) { spectator.spectate(host.room().id, ""); spectateSent = true; }
                if (!spectatorLoaded && (spectator.starting() || spectator.isRejoin())) {
                    setup(spectator,peerData,ws); spectatorLoaded = true;

                }
            }
            auto advance = [&](net::MpClient& client, sim::World& world, uint32_t& tick, bool reference) {
                net::Bundle bundle;
                while (tick < 300 && (reference || hashes.count(tick)) && client.takeBundle(tick,bundle)) {
                    for (const auto& c:bundle.cmds) sim::applyCommand(world,reg,c);
                    for (const auto& e:bundle.events) sim::applyEvent(world,e);
                    world.tick(1.f/30.f);
                    const auto hash=world.stateHash();
                    if (reference) hashes[tick]=hash;
                    else check(hashes.at(tick)==hash,"map worlds diverged in live ticks");
                    if (!client.isSpectator() && tick % net::kHashPeriod == 0) client.sendHash(tick,hash);
                    ++tick;
                }
            };
            if (hostLoaded) advance(host,wh,ht,true);
            if (peerLoaded && ht>pt) advance(peer,wp,pt,false);
            if (spectatorLoaded && ht>st) advance(spectator,ws,st,false);
            check(!host.desynced() && !peer.desynced(), "server map differs");
            if (ht==300 && pt==300 && st==300) {
                if (mapgen::isGeneratedMapId(mapId)) {
                    const auto file="Generated-"+crypto::toHex(crypto::sha256(mapId))+".kmp";
                    check(std::filesystem::exists(std::filesystem::path(argv[3])/"Maps"/file),"host did not save generated map");
                    check(std::filesystem::exists(std::filesystem::path(argv[4])/"Maps"/file),"peer did not save generated map");
                }
                std::cout << "network map transfer: host, peer, late spectator, 300 matching ticks, hash " << std::hex << wh.stateHash() << '\n';
                return 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        throw std::runtime_error("map transfer timed out: " + host.mapStatus() + "; " + peer.mapStatus());
    }
    const auto root = std::filesystem::temp_directory_path() / ("tak-map-test-" + crypto::toHex(crypto::randomVec(8)));
    struct Clean { std::filesystem::path p; ~Clean() { std::error_code ec; std::filesystem::remove_all(p,ec); } } clean{root};
    tnt::Map map; map.width=map.height=64; map.blocksX=map.blocksY=32; map.seaLevel=0;
    map.heights.assign(4096,40); map.features.assign(4096,0xffff);
    map.tileKeys.assign(1024,0x1234); map.tileCols.assign(1024,0); map.tileRows.assign(1024,0);
    auto files = std::make_shared<hpi::Vfs::Files>();
    (*files)["kmap/test.tnt"] = map.save(); (*files)["terrain/00001234.jpg"] = {1,2,3};
    std::string ota="[GlobalHeader]\n{\ngravity=99;\n}\n";
    (*files)["kmap/test.ota"] = {ota.begin(),ota.end()};
    hpi::Vfs source; source.setMapFiles(files);
    auto p = net::maps::build(source,"test");
    check(p->digest == net::maps::build(source,"TEST")->digest, "unstable map fingerprint");
    net::maps::saveCache(root,*p); check(bool(net::maps::loadCache(root,p->digest)), "cache roundtrip");
    hpi::Vfs catalog; catalog.refreshMapCache(root);
    const auto listed=hpi::listMaps(catalog);
    check(listed.size()==1,"download absent from subsequent map picker");
    check(!catalog.has("terrain/00001234.jpg"),"cached map art leaked globally");
    check(net::maps::build(catalog,listed.front().first)->digest==p->digest,"reselected download changed identity");
    auto corrupt = p->bytes; corrupt.back() ^= 1;
    rejects([&]{net::maps::decode(corrupt,p->digest);}, "corruption accepted");
    auto bad = std::make_shared<hpi::Vfs::Files>(*files);
    (*bad)["kmap/test.ota"].push_back(' '); hpi::Vfs changed; changed.setMapFiles(bad);
    check(net::maps::build(changed,"test")->digest != p->digest, "scenario mismatch ignored");
    (*bad)["terrain/00001234.jpg"].push_back(4);
    check(net::maps::build(changed,"test")->digest != p->digest, "art mismatch ignored");
    hpi::Vfs room(&source); room.setMapFiles(bad);
    check(source.read("terrain/00001234.jpg").size()==3 && room.read("terrain/00001234.jpg").size()==4,"room data leaked");
    net::maps::Receiver receive; receive.begin(1,uint32_t(p->bytes.size()),p->digest);
    for (size_t off=0; off<p->bytes.size();) {
        net::Writer w; w.u32(1); w.u32(uint32_t(off));
        size_t n=std::min(net::maps::kChunkBytes,p->bytes.size()-off);
        w.b.insert(w.b.end(),p->bytes.begin()+off,p->bytes.begin()+off+n);
        net::Reader r(w.b.data(),w.b.size()); receive.append(r); off+=n;
    }
    check(receive.complete() && receive.bytes==p->bytes,"chunk assembly");
    rejects([&]{receive.begin(1,uint32_t(net::maps::kMaxBytes+1),p->digest);},"oversized offer");
    receive.begin(1,uint32_t(p->bytes.size()),p->digest);
    net::Writer wrong; wrong.u32(2); wrong.u32(0); wrong.u8(1);
    rejects([&]{net::Reader r(wrong.b.data(),wrong.b.size());receive.append(r);},"wrong room chunk");
    wrong.b[0]=1; wrong.b[4]=1;
    rejects([&]{net::Reader r(wrong.b.data(),wrong.b.size());receive.append(r);},"out-of-order chunk");
    net::Writer hostile; hostile.u32(2); hostile.str("kmap/../evil.tnt"); hostile.u32(1);
    rejects([&]{net::maps::decode(hostile.b,"");},"traversal accepted");
    hostile={}; hostile.u32(2); hostile.str("kmap/test.tnt"); hostile.u32(2); hostile.str("scripts/evil.cob"); hostile.u32(1); hostile.u8(0);
    rejects([&]{net::maps::decode(hostile.b,"");},"script import accepted");
    if (argc == 2) {
        auto vfs=hpi::mountRetailRoot(argv[1],hpi::OverridePolicy::None);
        mapgen::Params params; params.waterDensity=0;
        auto recipe=mapgen::encodeMapId(params);
        auto output=net::maps::saveGenerated(root,vfs,recipe);
        hpi::Vfs saved(&vfs); saved.addLayer(hpi::MountSet(root/"Maps",{false,{".kmp"},{},{}}));
        const auto maps=hpi::listMaps(saved);
        std::string path;
        for (const auto& [name,pth]:maps) if(name.starts_with("Generated-")) path=pth;
        check(!path.empty(),"saved generated map absent from browser");
        auto generated=mapgen::generate(params,vfs);
        auto loaded=tnt::Map::load(saved.read(path));
        check(loaded.save()==generated.map.save(),"saved generated geometry differs");
        auto starts=sim::parseStartPositions(saved,path);
        check(starts.size()==generated.starts.size(),"saved start count");
        for(size_t i=0;i<starts.size();++i) check(starts[i].first==generated.starts[i].first*16 && starts[i].second==generated.starts[i].second*16,"saved start location");
        const auto time=std::filesystem::last_write_time(output);
        check(net::maps::saveGenerated(root,vfs,recipe)==output && std::filesystem::last_write_time(output)==time,"duplicate generated save");
    }
    std::cout << "map transfer: package identity, isolation, cache, chunks, and validation passed\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1;}
