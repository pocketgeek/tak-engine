#include "net/mappackage.h"
#include "net/client.h"
#include "sim/matchsetup.h"
#include "tnt/mapgen.h"
#include "tnt/tnt.h"
#include "tnt/ota.h"
#include "sim/scenario.h"
#include "crt/crt.h"
#include <chrono>
#include <cstring>
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
static int soloNetwork(uint16_t port,const char* hostRoot,const char* peerRoot) {
    auto original=hpi::mountRetailRoot(hostRoot,hpi::OverridePolicy::None);
    const auto stock=net::maps::build(original,"Ulasem Arena");
    const auto starts=sim::parseStartPositions(original,stock->mapPath);
    check(!starts.empty(),"solo fixture needs a start");
    crt::Scenario scenario;scenario.players.resize(9);
    crt::Unit monarch;monarch.objectName="araking";monarch.player=0;
    monarch.x=int(starts[0].first/16);monarch.z=int(starts[0].second/16);
    monarch.health=75;monarch.armor=150;monarch.weapon=175;monarch.veteran=3;
    monarch.uniqueName="Network monarch with a deliberately long display name";
    monarch.y=999;
    scenario.units.push_back(monarch);
    auto neutral=monarch;neutral.player=8;neutral.x+=10;neutral.uniqueName="Neutral monarch";
    scenario.units.push_back(neutral);
    scenario.customTypes.push_back({"araking",{100,200,200,2}});
    scenario.regions.push_back({"Reinforcements",monarch.x-6,monarch.z-6,monarch.x+6,monarch.z+6});
    scenario.regions.push_back({"Rally",monarch.x+4,monarch.z+4,monarch.x+6,monarch.z+6});
    scenario.players[1].push_back({{{0,{}}},{{13,{"Player 1","Solo authored network"}},
        {16,{"500"}},{17,{"123"}},{7,{"ARAARCH","Reinforcements"}},
        {11,{"ARAARCH","7","Anywhere"}},
        {15,{"ARAARCH","Anywhere","Rally"}},{14,{}}}});
    scenario.players[1].push_back({{{1,{"3"}}},{{20,{}},{14,{}}}});
    scenario.players[1].push_back({{{1,{"8"}}},{{5,{}},{14,{}}}});
    std::vector<hpi::PackFile> entries;
    const auto stem=stock->mapPath.substr(0,stock->mapPath.size()-4);
    for(const auto& [name,data]:*stock->files) {
        const auto path=name.starts_with(stem+".")?"kmap/solo authored"+name.substr(stem.size()):name;
        if(path=="kmap/solo authored.crt" || path=="kmap/solo authored.tdf")continue;
        if(path=="kmap/solo authored.ota") {
            auto metadata=tnt::Scenario::parse(std::string(data.begin(),data.end()));
            metadata.hasScenario=true;metadata.useOnlyUnits="solo authored.tdf";
            const auto text=metadata.write()+"\n[TAKPlaytest]{\nauthoredscenario=1;\n}\n";
            entries.push_back({path,{text.begin(),text.end()}});
        } else entries.push_back({path,data});
    }
    entries.push_back({"kmap/solo authored.crt",crt::write(scenario)});
    const std::string restriction="[ARAKING]{}\n";
    entries.push_back({"kmap/solo authored.tdf",{restriction.begin(),restriction.end()}});
    write(std::filesystem::path(hostRoot)/"Maps"/"solo-authored.kmp",hpi::pack(entries));
    auto hostData=hpi::mountRetailRoot(hostRoot,hpi::OverridePolicy::None);
    auto peerData=hpi::mountRetailRoot(peerRoot,hpi::OverridePolicy::None);
    net::MpClient host,observer;host.setMapRoot(hostRoot);observer.setMapRoot(peerRoot);
    host.setDataHash(hpi::gameplayHash(hostData));observer.setDataHash(hpi::gameplayHash(peerData));
    check(host.connect("127.0.0.1",port,"solo-author"),"solo host connect");
    sim::TypeRegistry registry;sim::setupRegistry(registry,hostData,true);
    sim::World wh,wo;
    bool created=false,ready=false,started=false,loaded=false,connected=false,spectating=false,observerLoaded=false,sent=false;
    uint32_t ht=0,ot=0;std::map<uint32_t,uint64_t> hashes;
    auto setup=[&](net::MpClient& client,hpi::Vfs& base,sim::World& world) {
        check(client.mapPackage() && net::maps::authoredScenario(*client.mapPackage()),"verified authored package missing");
        hpi::Vfs view(&base);view.setMapFiles(client.mapPackage()->files);
        sim::MatchConfig cfg;cfg.vfs=&view;cfg.mapPath=client.mapPackage()->mapPath;
        cfg.startSeed=client.startSeed();cfg.randomStarts=client.startRoom().opts.randomStarts;
        cfg.unitCap=client.startRoom().opts.unitCap;cfg.slots={{true,0,0,1,false,false}};
        sim::setupMatch(world,registry,cfg);
        check(world.numPlayers()==1 && world.units().size()==3,"scripted reinforcement missing or neutral became a lobby player");
        check(world.unit(1)->scenarioArmor>2.99f && world.unit(1)->scenarioArmor<3.01f &&
              world.unit(1)->scenarioWeapon>3.49f && world.unit(1)->scenarioWeapon<3.51f,
              "authored stat multipliers lost on network participant");
        check(!world.buildAllowed(registry.find("tarnecro")) && !world.buildAllowed(registry.find("araarch")) &&
              world.buildAllowed(registry.find("araking")),"transferred Use Only not installed");
        check(world.unit(1)->scenarioName==monarch.uniqueName.substr(0,31),"named placement lost on network participant");
        check(world.player(0).scenarioResourceLimit==500 && world.player(0).mana==123,"startup resource actions lost on network participant");
        const auto* reinforcement=world.unit(3);
        check(reinforcement && reinforcement->type==registry.find("araarch") &&
              reinforcement->hp.floorInt()==reinforcement->type->maxHp-7,
              "scripted create bypass or raw HP damage failed on network participant");
        check(!reinforcement->orders.empty(),"scripted reinforcement move was not queued");
        client.reportLoaded(hpi::gameplayHash(base));
    };
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(90);
    while(std::chrono::steady_clock::now()<until) {
        check(host.poll(),host.error().c_str());
        if(!created && host.state()==net::MpClient::State::Lobby) {
            net::GameOptions opts;opts.crusades=1;opts.overridePolicy=0;
            host.createGame("solo authored test","","Solo Authored",opts,2);created=true;
        }
        if(!ready && host.room().id) {host.setSlot(0,1,0,0,0,1);ready=true;}
        if(!started && ready && host.room().mapsReady && host.room().slots[0].ready) {
            check(host.room().slots[1].type!=1 && host.room().slots[1].type!=2,"solo fixture accidentally seated opponent");
            host.startGame();started=true;
        }
        if(!loaded && host.starting()) {setup(host,hostData,wh);loaded=true;}
        if(loaded && ht>30 && !connected) {check(observer.connect("127.0.0.1",port,"solo-observer"),"solo observer connect");connected=true;}
        if(connected) {
            check(observer.poll(),observer.error().c_str());
            if(!spectating && observer.state()==net::MpClient::State::Lobby) {observer.spectate(host.room().id,"");spectating=true;}
            if(!observerLoaded && (observer.starting() || observer.isRejoin())) {setup(observer,peerData,wo);observerLoaded=true;}
        }
        if(loaded && ht>30 && !sent) {
            net::Command command;command.player=7;command.unitId=1;command.kind=net::Cmd::Train;
            std::strcpy(command.type,"tarnecro");
            auto repeat=command;repeat.kind=net::Cmd::RepeatTrain;
            auto build=command;build.kind=net::Cmd::Build;build.x=400;build.z=400;
            host.sendCommands({command,repeat,build});sent=true;
        }
        auto advance=[&](net::MpClient& client,sim::World& world,uint32_t& tick,bool reference) {
            net::Bundle bundle;
            while((reference || hashes.count(tick)) && client.takeBundle(tick,bundle)) {
                for(const auto& c:bundle.cmds)sim::applyCommand(world,registry,c);
                for(const auto& e:bundle.events)sim::applyEvent(world,e);
                world.tick(1.f/30.f);
                if(tick<90)check(world.player(0).scenarioResourceLimit==500 && world.player(0).mana==123,
                                 "resource limit did not suppress natural income during network replay");
                const auto hash=world.stateHash();
                if(reference)hashes[tick]=hash;else check(hashes.at(tick)==hash,"solo observer replay diverged");
                if(!client.isSpectator() && tick%net::kHashPeriod==0)client.sendHash(tick,hash);
                ++tick;
            }
        };
        if(loaded)advance(host,wh,ht,true);
        if(observerLoaded)advance(observer,wo,ot,false);
        check(!host.desynced() && !observer.desynced(),"solo referee hash mismatch");
        // CRT results are per-player deterministic world state, not the campaign's
        // broadcast MissionOutcome message. Continue beyond the result so the
        // referee receives/checks post-result periodic hashes as well.
        if(observerLoaded && ht>=330 && wh.scenarioOutcome(0)==1 && wo.scenarioOutcome(0)==1 && ot==ht) {
            check(ht>240 && sent,"solo result occurred before authored victory condition");
            check(wh.scenarioOutcome(0)==1 && wo.scenarioOutcome(0)==1,"server result differs from scenario");
            check(wh.unit(1)->buildQueue.empty() && wh.units().size()==3,"network construction bypassed Use Only");
            check(wh.player(0).scenarioResourceLimit==0 && wo.player(0).scenarioResourceLimit==0 && wh.player(0).mana>123,
                  "timed Resources Normal did not restore income across peers");
            check(wo.unit(1)->scenarioName==monarch.uniqueName.substr(0,31),"late observer lost authored display name");
            std::cout<<"PASS: solo authored names, neutral/stats/restrictions, scripted create/raw HP/move/resource reset, host/referee/late observer parity, victory verified after "<<ht<<" ticks hash "<<std::hex<<wh.stateHash()<<'\n';
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("solo authored network timed out: "+host.mapStatus()+"; "+host.error());
}

int main(int argc, char** argv) try {
    if(argc==4 && std::string(argv[1])=="--export-package") {
        auto data=hpi::mountRetailRoot(argv[2],hpi::OverridePolicy::None);
        write(argv[3],net::maps::build(data,"Ulasem Arena")->bytes);return 0;
    }
    if(argc==5 && std::string(argv[1])=="--solo-network")return soloNetwork(uint16_t(std::stoi(argv[2])),argv[3],argv[4]);
    if (argc >= 5 && std::string(argv[1]) == "--network") {
        const auto port = uint16_t(std::stoi(argv[2]));
        const bool diplomacy=std::getenv("TAK_DIPLOMACY_NETWORK")!=nullptr;
        if (argc == 5) {
            auto original = hpi::mountRetailRoot(argv[3], hpi::OverridePolicy::None);
            auto package = net::maps::build(original, "Ulasem Arena");
            std::vector<hpi::PackFile> entries;
            for (const auto& [name,data] : *package->files) {
                std::string path = name;
                const auto stem=package->mapPath.substr(0,package->mapPath.size()-4);
                if(name.starts_with(stem+"."))path="kmap/transfer test"+name.substr(stem.size());
                if (name == package->mapPath) {
                    auto m=tnt::Map::load(data);
                    m.features[size_t(m.width)*10+10]=uint16_t(m.featureNames.size());
                    m.featureNames.push_back("transfer_tree");
                    entries.push_back({path,m.save()});
                } else entries.push_back({path,data});
            }
            const std::string feature="[transfer_tree]\n{\nblocking=1;\nfootprintx=2;\nfootprintz=2;\nreclaimable=1;\nenergy=75;\n}\n";
            entries.push_back({"features/transfer-test.tdf",{feature.begin(),feature.end()}});
            auto companion=[&](const std::string& suffix,const std::vector<uint8_t>& data) {
                const auto path="kmap/transfer test"+suffix;
                std::erase_if(entries,[&](const auto& entry){return entry.path==path;});
                entries.push_back({path,data});
            };
            crt::Scenario authored;authored.players.resize(2);
            authored.players[1].push_back({{{20,{}}},{{13,{"All Players","Transferred scenario"}}}});
            if(std::getenv("TAK_MAP_TEST_SCENARIO")) {
                auto starts=sim::parseStartPositions(original,package->mapPath);
                check(starts.size()>=2,"scenario transfer fixture needs two start positions");
                for(int p=0;p<2;++p) {
                    crt::Unit unit;unit.objectName=sim::kMonarchs[p];unit.player=p;
                    unit.x=int(starts[size_t(p)].first/16);unit.z=int(starts[size_t(p)].second/16);
                    authored.units.push_back(unit);
                }
                authored.players[1][0].conditions[0].opcode=0;
                for(auto& entry:entries) if(entry.path=="kmap/transfer test.ota") {
                    auto metadata=tnt::Scenario::parse(std::string(entry.data.begin(),entry.data.end()));
                    metadata.hasScenario=true;const auto text=metadata.write()+"\n[TAKPlaytest]{\nauthoredscenario=1;\n}\n";entry.data={text.begin(),text.end()};
                }
            }
            companion(".crt",crt::write(authored));
            companion(".tdf",{});
            const std::string names="TAK_EDITOR_RULE_NAMES 1\n1 0 \"Transfer rule\"\n";
            companion(".editor",{names.begin(),names.end()});
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
        bool chatSent=false,sharingSent=false,sharingSeen=false,sharingRestored=false;
        std::vector<std::pair<std::string,std::string>> hostChat,peerChat,spectatorChat;
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
            cfg.slots.resize(diplomacy ? 3 : 2); cfg.slots[0] = {true,0,0,1,false,false};
            cfg.slots[1] = {true,1,diplomacy ? 0 : 1,1,false,false};
            if(diplomacy)cfg.slots[2]={true,2,1,1,true,true};
            if(argc==5) {
                const auto authored=crt::parse(view.read("kmap/transfer test.crt"));
                check(authored.players.size()==2 && authored.players[1].size()==1 &&
                      authored.players[1][0].actions.size()==1 &&
                      authored.players[1][0].actions[0].slot[1]=="Transferred scenario",
                      "scenario companion missing on a network participant");
                check(view.has("kmap/transfer test.editor") && view.has("kmap/transfer test.tdf") &&
                      view.read("kmap/transfer test.tdf").empty(),"editor metadata or empty restriction lost over network");
            }
            sim::setupMatch(world, reg, cfg);
            if(std::getenv("TAK_MAP_TEST_SCENARIO"))
                check(world.scenario() && world.units().size()==2,"network peer did not initialize authored scenario");
            client.reportLoaded(hpi::gameplayHash(base));
        };
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        while (std::chrono::steady_clock::now() < until) {
            check(host.poll(), host.error().c_str()); check(peer.poll(), peer.error().c_str());
            if (!created && host.state() == net::MpClient::State::Lobby) {
                net::GameOptions opts; opts.crusades = 1; opts.overridePolicy = 0;
                host.createGame("map transfer test", "", mapId, opts, diplomacy ? 3 : 2); created = true;
            }
            if (!joined && host.room().id && peer.state() == net::MpClient::State::Lobby) {
                peer.joinGame(host.room().id, ""); joined = true;
            }
            if (!readied && peer.room().id) {
                // START must remain blocked even when human READY arrived first.
                host.setSlot(0,1,0,0,0,1); peer.setSlot(1,1,1,1,diplomacy ? 0 : 1,1);
                if(diplomacy)host.setSlot(2,2,2,2,1,1,0);
                readied = true;
            }
            if (!started && readied && host.room().mapsReady && host.room().slots[1].ready && (!diplomacy || host.room().slots[2].type==2)) {
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
            if (diplomacy && hostLoaded && ht>30 && !sharingSent) {
                net::Command c;c.kind=net::Cmd::ShareMana;c.targetId=1;c.queue=0;
                c.player=7; // the server must stamp the actual sender, not trust this
                host.sendCommands({c});sharingSent=true;
            }
            if (diplomacy && sharingSent && !(wh.player(0).manaShareMask & 2)) sharingSeen=true;
            if (diplomacy && sharingSeen && ht>150 && !sharingRestored) {
                net::Command c;c.kind=net::Cmd::ShareMana;c.targetId=1;c.queue=1;
                host.sendCommands({c});sharingRestored=true;
            }
            if (spectatorLoaded && ht>30 && !chatSent) {
                host.chat("PRIVATE_PEER",2);host.chat("NOBODY",0);host.chat("EVERYONE");
                peer.chat("PRIVATE_HOST",1);chatSent=true;
            }
            auto collect=[](net::MpClient& client,auto& messages) {
                auto incoming=client.takeChat();messages.insert(messages.end(),incoming.begin(),incoming.end());
            };
            collect(host,hostChat);collect(peer,peerChat);collect(spectator,spectatorChat);
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
                if(diplomacy) {
                    check(sharingSeen && sharingRestored && wh.player(0).manaShareMask==0xff &&
                          wp.player(0).manaShareMask==0xff && ws.player(0).manaShareMask==0xff,
                          "sharing command did not apply and restore on all peers");
                    std::cout << "mana sharing: server-stamped sender, disable/enable, referee and spectator lockstep passed\n";
                }
                auto has=[](const auto& messages,const char* text) {
                    return std::any_of(messages.begin(),messages.end(),[&](const auto& item){return item.second==text;});
                };
                check(chatSent && has(hostChat,"PRIVATE_HOST") && has(peerChat,"PRIVATE_PEER"),"directed chat missing recipient");
                check(!has(hostChat,"PRIVATE_PEER") && !has(peerChat,"PRIVATE_HOST") &&
                      !has(spectatorChat,"PRIVATE_PEER") && !has(spectatorChat,"PRIVATE_HOST"),"private chat escaped recipient mask");
                for(const auto* messages:{&hostChat,&peerChat,&spectatorChat}) {
                    check(has(*messages,"EVERYONE"),"default chat did not reach everyone");
                    check(!has(*messages,"NOBODY"),"empty chat recipient mask delivered a message");
                }
                std::cout << "directed chat: recipients, exclusion, empty mask, broadcast and spectator privacy passed\n";
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
    crt::Scenario scenario;
    scenario.players.resize(2);
    crt::RuleGroup group;
    group.actions.push_back({0, {"transferred scenario", "", "", "", ""}});
    scenario.players[1].push_back(group);
    (*files)["kmap/test.crt"] = crt::write(scenario);
    (*files)["kmap/test.tdf"] = {}; // An empty restriction list must survive too.
    (*files)["kmap/test.txt"] = {'m','a','p'};
    (*files)["kmap/test.editor"] = {'n','a','m','e'};
    (*files)["kmap/test.recipe"] = {'s','e','e','d'};
    // Neither another map's metadata nor arbitrary scripts belong in this package.
    (*files)["kmap/other.crt"] = crt::write(scenario);
    (*files)["scripts/evil.cob"] = {1};
    hpi::Vfs source; source.setMapFiles(files);
    auto p = net::maps::build(source,"test");
    check(p->digest == net::maps::build(source,"TEST")->digest, "unstable map fingerprint");
    {
        // A resource that will ultimately be rejected as unreferenced must not
        // get an unlimited parser budget before the reference check.
        auto hostile=*p->files;
        std::string text;for(int i=0;i<70000;++i)text+="[x]{}\n";
        hostile["features/unreferenced.tdf"]={text.begin(),text.end()};
        net::Writer encoded;encoded.u32(2);encoded.str(p->mapPath);encoded.u32(uint32_t(hostile.size()));
        for(const auto& [name,data]:hostile) {
            encoded.str(name);encoded.u32(uint32_t(data.size()));
            encoded.b.insert(encoded.b.end(),data.begin(),data.end());
        }
        rejects([&]{net::maps::decode(encoded.b,"");},"unreferenced adversarial TDF accepted");
        tak::StopSource cancelled;cancelled.request_stop();
        rejects([&]{net::maps::decode(p->bytes,p->digest,cancelled.get_token());},"cancelled map decoded");
    }

    check(!net::maps::authoredScenario(*p),"ordinary CRT map became a solo scenario");
    {
        auto authoredFiles=std::make_shared<hpi::Vfs::Files>(*p->files);
        auto authored=*p;authored.files=authoredFiles;
        auto metadata=tnt::Scenario::parse(ota);metadata.hasScenario=true;
        auto setMetadata=[&](const std::string& text) {
            (*authoredFiles)["kmap/test.ota"]={text.begin(),text.end()};
        };
        setMetadata(metadata.write());
        check(!net::maps::authoredScenario(authored),"hasScenario alone enabled solo skirmish");
        setMetadata(metadata.write()+"\n[TAKPlaytest]{\nauthoredscenario=1;\n}\n");
        check(net::maps::authoredScenario(authored),"explicit valid authored scenario cannot start solo");
        hpi::Vfs view;view.setMapFiles(authoredFiles);
        check(net::maps::authoredScenario(view,"kmap/test.tnt"),"local and packaged scenario detection differ");
        const auto crt=authoredFiles->at("kmap/test.crt");
        authoredFiles->erase("kmap/test.crt");
        check(!net::maps::authoredScenario(authored),"missing CRT enabled solo exception");
        (*authoredFiles)["kmap/test.crt"]={1,2,3};
        check(!net::maps::authoredScenario(authored),"damaged CRT enabled solo exception");
        (*authoredFiles)["kmap/test.crt"]=crt;
        metadata.hasScenario=false;
        setMetadata(metadata.write()+"\n[TAKPlaytest]{\nauthoredscenario=1;\n}\n");
        check(!net::maps::authoredScenario(authored),"disabled scenario enabled solo exception");
        setMetadata(metadata.write()+"\n[TAKPlaytest]{\nauthoredscenario=0;\n}\n");
        check(!net::maps::authoredScenario(authored),"disabled opt-in marker enabled solo exception");
    }
    for (const auto* extension : {".ota", ".crt", ".tdf", ".txt", ".editor", ".recipe"}) {
        const auto path = std::string("kmap/test") + extension;
        check(p->files->at(path) == files->at(path), "map companion lost or modified");
        auto changedFiles = std::make_shared<hpi::Vfs::Files>(*files);
        changedFiles->at(path).push_back(' ');
        hpi::Vfs changedSource; changedSource.setMapFiles(changedFiles);
        check(net::maps::build(changedSource,"test")->digest != p->digest,
              "map companion excluded from fingerprint");
    }
    check(!p->files->count("kmap/other.crt") && !p->files->count("scripts/evil.cob"),
          "unrelated files included in package");
    rejects([&]{net::maps::saveCache(root,*p,1);},"disk quota accepted oversized package");
    check(!std::filesystem::exists(root/"MapCache"/(p->digest+".takmap")),"quota rejection left partial cache");
    net::maps::saveCache(root,*p);
    net::maps::saveCache(root,*p,1); // an existing download needs no additional quota

    rejects([&]{net::maps::loadCache(root,p->digest,p->bytes.size()-1);},"actual cache size escaped budget");
    rejects([&]{net::maps::loadCache(root,p->digest,net::maps::kMaxBytes,p->bytes.size()-1);},"cache advertised mismatch accepted");
    check(std::filesystem::exists(root/"MapCache"/(p->digest+".takmap")),"admission removed valid cache");
    const auto cached = net::maps::loadCache(root,p->digest);
    check(cached && *cached->files == *p->files, "cache companion roundtrip");
    hpi::Vfs catalog; catalog.refreshMapCache(root);
    const auto listed=hpi::listMaps(catalog);
    check(listed.size()==1,"download absent from subsequent map picker");
    check(!catalog.has("terrain/00001234.jpg"),"cached map art leaked globally");
    check(net::maps::build(catalog,listed.front().first)->digest==p->digest,"reselected download changed identity");
    {
        const auto snapshotPath=root/"Test Snapshot.kmp";
        auto changed=std::make_shared<hpi::Vfs::Files>(*p->files);
        changed->at("kmap/test.txt")={'e','d','i','t','e','d'};
        std::vector<hpi::PackFile> members;
        for(const auto& [name,data]:*changed)members.push_back({name,data});
        auto writeSnapshot=[&] {
            const auto bytes=hpi::pack(members);
            std::ofstream output(snapshotPath,std::ios::binary|std::ios::trunc);
            output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
        };
        writeSnapshot();
        const auto imported=net::maps::importSnapshot(catalog,snapshotPath);
        check(imported->files->at("kmap/test.txt")==changed->at("kmap/test.txt"),"older cached map shadowed current playtest snapshot");
        check(imported->digest!=p->digest,"edited snapshot has stale fingerprint");
        members.push_back({"kmap/second.tnt",map.save()});writeSnapshot();
        rejects([&]{net::maps::importSnapshot(catalog,snapshotPath);},"ambiguous multi-map playtest bundle accepted");
    }
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
    auto rawPackage = [&](const std::string& extra, const std::vector<uint8_t>& payload) {
        net::Writer w; w.u32(2); w.str(p->mapPath); w.u32(uint32_t(p->files->size()+1));
        for (const auto& [name, data] : *p->files) {
            w.str(name); w.u32(uint32_t(data.size())); w.b.insert(w.b.end(),data.begin(),data.end());
        }
        w.str(extra); w.u32(uint32_t(payload.size())); w.b.insert(w.b.end(),payload.begin(),payload.end());
        return w.b;
    };
    for (const auto* extra : {"kmap/other.crt", "kmap/test.cob", "kmap/test/evil.crt",
                             "kmap/test.crt/../evil.crt", "kmap/test.editor", "units/test.tdf"})
        rejects([&]{net::maps::decode(rawPackage(extra,{1}),"");},"unrelated or duplicate companion accepted");
    auto legacyFiles = std::make_shared<hpi::Vfs::Files>();
    (*legacyFiles)["kmap/test.tnt"] = map.save();
    (*legacyFiles)["terrain/00001234.jpg"] = {1,2,3};
    hpi::Vfs legacy; legacy.setMapFiles(legacyFiles);
    check(net::maps::build(legacy,"test")->files->size()==2,"map without companions rejected");
    if (argc == 2) {
        auto vfs=hpi::mountRetailRoot(argv[1],hpi::OverridePolicy::None);
        mapgen::Params params; params.waterDensity=0; params.name="My Lake";
        auto recipe=mapgen::encodeMapId(params);
        auto output=net::maps::saveGenerated(root,vfs,recipe);
        check(output.filename().string().starts_with("Generated-My Lake-"),"named map filename");
        // Simulate the original named archive, then verify saving repairs it.
        std::vector<hpi::PackFile> oldFiles;
        {
            hpi::Archive archive(output);
            for (const auto& entry:archive.entries()) if (!entry.isDirectory) {
                auto oldPath=entry.path;
                const auto prefix="kmap/My Lake-"+crypto::toHex(crypto::sha256(recipe)).substr(0,16);
                if (oldPath.starts_with(prefix)) oldPath.replace(0,prefix.size(),"kmap/My Lake");
                oldFiles.push_back({oldPath,archive.read(entry)});
            }
        }
        const auto oldBytes=hpi::pack(oldFiles);
        { std::ofstream out(output,std::ios::binary|std::ios::trunc);
          out.write(reinterpret_cast<const char*>(oldBytes.data()),oldBytes.size()); }
        check(net::maps::saveGenerated(root,vfs,recipe)==output,"legacy named repair changed filename");
        check(hpi::Archive(output).find("kmap/My Lake-"+crypto::toHex(crypto::sha256(recipe)).substr(0,16)+".tnt")!=nullptr,"legacy named archive not repaired");
        hpi::Vfs saved(&vfs); saved.addLayer(hpi::MountSet(root/"Maps",{false,{".kmp"},{},{}}));
        const auto maps=hpi::listMaps(saved);
        std::string path;
        for (const auto& [name,pth]:maps) if(name=="My Lake-"+crypto::toHex(crypto::sha256(recipe)).substr(0,16)) path=pth;
        check(!path.empty(),"saved generated map absent from browser");
        auto generated=mapgen::generate(params,vfs);
        auto loaded=tnt::Map::load(saved.read(path));
        check(loaded.save()==generated.map.save(),"saved generated geometry differs");
        auto starts=sim::parseStartPositions(saved,path);
        check(starts.size()==generated.starts.size(),"saved start count");
        for(size_t i=0;i<starts.size();++i) check(starts[i].first==generated.starts[i].first*16 && starts[i].second==generated.starts[i].second*16,"saved start location");
        const auto time=std::filesystem::last_write_time(output);
        check(net::maps::saveGenerated(root,vfs,recipe)==output && std::filesystem::last_write_time(output)==time,"duplicate generated save");
        // Same title, different recipe: neither VFS path nor browser selection
        // may mask the other map (including case-insensitive title collisions).
        for (int i=0;i<2;++i) {
            auto other=params; other.seed+=i+1;
            if (i) other.name="my lake";
            const auto otherRecipe=mapgen::encodeMapId(other);
            const auto otherOutput=net::maps::saveGenerated(root,vfs,otherRecipe);
            check(otherOutput!=output,"duplicate title overwrote archive");
            hpi::Vfs both(&vfs); both.addLayer(hpi::MountSet(root/"Maps",{false,{".kmp"},{},{}}));
            const auto entries=hpi::listMaps(both);
            const auto otherName=other.name+"-"+crypto::toHex(crypto::sha256(otherRecipe)).substr(0,16);
            int found=0;
            for (const auto& [title,pth]:entries) {
                if (pth==path) {
                    ++found;
                    check(both.read(pth)==generated.map.save(),"original duplicate title changed geometry");
                }
                if (title==otherName) {
                    ++found;
                    check(hpi::findMap(both,title)==pth,"duplicate title cannot be selected");
                    check(both.read(pth)==mapgen::generate(other,vfs).map.save(),"duplicate title loaded wrong geometry");
                    check(net::maps::build(both,title)->mapPath==hpi::MountSet::key(pth),"duplicate title package resolved wrong map");
                }
            }
            check(found==2,"duplicate titles not independently listed");
        }

    }
    std::cout << "map transfer: package identity, isolation, cache, chunks, and validation passed\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1;}
