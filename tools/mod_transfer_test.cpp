#include "net/client.h"
#include "sim/matchsetup.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <thread>
using namespace tak;
static void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
static void put(const std::filesystem::path& root,const std::string& p,const std::string& b){auto f=root/std::filesystem::u8path(p);std::filesystem::create_directories(f.parent_path());std::ofstream(f,std::ios::binary)<<b;}
static void archive(const std::filesystem::path& root,const std::string& p,const std::vector<std::pair<std::string,std::string>>& entries){
 std::vector<hpi::PackFile> files;for(const auto& [name,value]:entries)files.push_back({name,std::vector<uint8_t>(value.begin(),value.end())});
 const auto bytes=hpi::pack(files);put(root,p,std::string(bytes.begin(),bytes.end()));}
int main(int argc,char** argv){try{
 check(argc>=4 && argc<=6,"port host-root peer-root [server-host] [--bad-loaded] required");
 const std::string serverHost=argc>=5?argv[4]:"127.0.0.1";const bool badLoaded=argc==6 && std::string(argv[5])=="--bad-loaded";auto hostRoot=std::filesystem::path(argv[2]),peerRoot=std::filesystem::path(argv[3]);
 auto base=hpi::mountRetailRoot(hostRoot);const auto original=base.read("units/araking.fbi");std::string text(original.begin(),original.end());
 const std::regex hp("maxdamage\\s*=\\s*[0-9]+",std::regex::icase);check(std::regex_search(text,hp),"monarch HP fixture missing");
 put(hostRoot,"mods/game/Alpha/units/araking.fbi",std::regex_replace(text,hp,"MaxDamage=12345"));
 put(hostRoot,"mods/game/Beta/units/araking.fbi",std::regex_replace(text,hp,"MaxDamage=23456"));
 put(hostRoot,"mods/game/Alpha/sounds/pack-test.wav",std::string(80000,'a'));
 put(hostRoot,"mods/cosmetic/HostArt/sounds/host-art.wav","host"); // the host's own cosmetic pack: mounted locally, never sent
 put(hostRoot,"overrides/Gamma/units/araking.fbi",std::regex_replace(text,hp,"MaxDamage=777")); // legacy folder: never read
 put(peerRoot,"mods/cosmetic/Local/units/araking.fbi",std::regex_replace(text,hp,"MaxDamage=99999"));
 put(peerRoot,"mods/cosmetic/Local/sounds/pack-test.wav","local");
 put(peerRoot,"mods/game/PeerGame/units/araking.fbi",std::regex_replace(text,hp,"MaxDamage=4242")); // a guest's game pack is never used
 // Always-enabled cosmetic packs (mods/cosmetic/always_enabled/<pack>/): each player's
 // own, in every room, never sent; a gameplay file inside one is filtered out like a
 // cosmetic pack's. An archive directly in mods/cosmetic/ (0.8.13's always-on) never loads.
 archive(hostRoot,"mods/cosmetic/always_enabled/HostAlways/host-always.hpi",{{"sounds/host-always.wav","h"}});
 archive(peerRoot,"mods/cosmetic/always_enabled/PeerAlways/peer-always.hpi",{{"sounds/peer-always.wav","p"},{"sounds/pack-test.wav","always"},{"units/araking.fbi",std::regex_replace(text,hp,"MaxDamage=31337")}});
 archive(peerRoot,"mods/cosmetic/root.hpi",{{"sounds/peer-root.wav","r"}});
 // Always-enabled game packs (mods/game/always_enabled/<pack>/): applied in every
 // non-campaign room the player HOSTS and sent like ticked packs. A guest's own never apply.
 put(hostRoot,"mods/game/always_enabled/HostRules/sounds/host-rules.wav","rules");
 put(peerRoot,"mods/game/always_enabled/PeerRules/units/araking.fbi",std::regex_replace(text,hp,"MaxDamage=5151"));
 put(peerRoot,"mods/game/always_enabled/PeerRules/sounds/peer-rules.wav","peer rules");
 // Downloads go to each player's own folder, never the data folder (hpi::userMapRoot).
 const auto hostCache=hostRoot.parent_path()/(hostRoot.filename().string()+"-cache"),peerCache=peerRoot.parent_path()/(peerRoot.filename().string()+"-cache");
 put(peerCache,"ModCache","not a directory"); // the first peer cache save fails: the verified packs must still be accepted
 net::MpClient host,peer;host.setMapRoot(hostRoot);peer.setMapRoot(peerRoot);host.setCacheRoot(hostCache);peer.setCacheRoot(peerCache);host.setGameModPacks({"Beta","Alpha"});peer.setGameModPacks({"PeerGame"});
 auto stockHash=hpi::gameplayHash(base);host.setDataHash(stockHash);peer.setDataHash(stockHash);
 check(host.connect(serverHost,uint16_t(std::stoi(argv[1])),"pack-host"),"host connect");check(peer.connect(serverHost,uint16_t(std::stoi(argv[1])),"pack-peer"),"peer connect");
 sim::TypeRegistry registry;hpi::Vfs hostData,peerData;sim::World wh,wp;
 bool created=false,joined=false,ready=false,started=false,loaded=false,saveFailed=false;int transition=0,campaign=0;std::string initialDigest,alphaDigest;uint32_t ht=0,pt=0;std::map<uint32_t,uint64_t> hashes;
 auto setup=[&](net::MpClient& c,hpi::Vfs& data,sim::World& world){
  data=c.modVfs(&c==&host?std::vector<std::string>{"HostArt"}:std::vector<std::string>{"Local","Removed pack"});
  check(hpi::gameplayHash(data)!=stockHash,"game pack did not change gameplay");
  if(&c==&host) {sim::setupRegistry(registry,data,false);check(registry.find("araking")->maxHp==23456,"alphabetical pack precedence lost");check(data.has("sounds/host-art.wav"),"host cosmetic pack not loaded");
   check(data.has("sounds/host-always.wav") && !data.has("sounds/peer-always.wav"),"host always-enabled cosmetic pack not loaded (or a guest's was sent)");
   check(data.has("sounds/host-rules.wav") && !data.has("sounds/peer-rules.wav"),"host always-enabled game pack not applied (or a guest's was)");}
  else {check(hpi::gameplayHash(data)==hpi::gameplayHash(hostData),"peer cosmetics changed gameplay");check(data.read("sounds/pack-test.wav")==std::vector<uint8_t>({'l','o','c','a','l'}),"peer cosmetic pack not loaded over its always-on archive");check(!data.has("sounds/host-art.wav"),"host cosmetic pack was transferred");
   check(data.has("sounds/peer-always.wav") && !data.has("sounds/host-always.wav"),"peer always-enabled cosmetic pack not loaded (or the host's was sent)");
   check(!data.has("sounds/peer-root.wav"),"an archive directly in mods/cosmetic/ was loaded");
   check(data.has("sounds/host-rules.wav") && c.modPackage()->mapPath.find("HostRules")!=std::string::npos,"the host's always-enabled game pack was not sent");
   check(!data.has("sounds/peer-rules.wav") && c.modPackage()->mapPath.find("PeerRules")==std::string::npos,"a guest's own always-enabled game pack applied in the host's room");
   {sim::TypeRegistry mine;sim::setupRegistry(mine,data,false);check(mine.find("araking")->maxHp==23456,"the guest's always-enabled game pack changed the host's units");}
   check(!c.modPackage()->files->count("sounds/host-always.wav") && !c.modPackage()->files->count("sounds/host-art.wav"),"the game-mod package carried cosmetic files");}
  data.setMapFiles(c.mapPackage()->files);sim::MatchConfig cfg;cfg.vfs=&data;cfg.mapPath=c.mapPackage()->mapPath;cfg.startSeed=c.startSeed();cfg.slots={{true,0,0,1,false,false},{true,1,1,1,false,false}};cfg.unitCap=2000;
  cfg.roomMapRevealed=c.room().opts.fogExplored>=1;cfg.roomLosOff=c.room().opts.fogExplored==2;   // hashed room vision, as net::matchConfigFor sets it
  world.setVisPlayer(-1);sim::setupMatch(world,registry,cfg);c.reportLoaded(hpi::gameplayHash(data) ^ (badLoaded && &c==&host ? 1 : 0));
 };
 const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(90);
 while(std::chrono::steady_clock::now()<end){
  const bool hostOk=host.poll();
  if(badLoaded && loaded && host.error().find("game data mismatch")!=std::string::npos){
   std::cout<<"PASS: verified game mods transferred; mismatching loaded gameplay rejected\n";return 0;
  }
  check(hostOk,host.error().c_str());check(peer.poll(),peer.error().c_str());
  saveFailed|=peer.modStatus().find("SAVE FAILED")!=std::string::npos;
  if(!created && host.state()==net::MpClient::State::Lobby){net::GameOptions o;o.gameMods=1;for(int duplicate=0;duplicate<16;++duplicate)host.createGame("packs","","Ulasem Arena",o,2);created=true;}
  if(!joined && host.room().id && peer.state()==net::MpClient::State::Lobby){for(int duplicate=0;duplicate<160;++duplicate)peer.joinGame(host.room().id,"");joined=true;}
  if(!ready && peer.room().id && peer.room().mySlot>=0){host.setSlot(0,1,0,0,0,1);peer.setSlot(1,1,1,1,1,1);ready=true;}
  if(ready && !started) {
   if(transition==0 && host.room().mapsReady && peer.modPackage()) {
    check(saveFailed,"peer cache failure not reported");std::filesystem::remove(peerCache/"ModCache"); // later transitions save, then reuse, the cache
    initialDigest=host.modPackage()->digest;auto opts=host.room().opts;opts.gameMods=0;host.setGameOptions(opts);transition=1;
   } else if(transition==1 && host.room().opts.gameMods==0 && peer.room().opts.gameMods==0) {
    check(!host.modPackage()&&!peer.modPackage(),"GAME MODS off retained shared game mods");
    // A room is off only when an older (0.8.12/0.8.13) host asks: then no game pack
    // at all, the host's always-enabled ones included.
    check(!host.modVfs({}).has("sounds/host-rules.wav"),"GAME MODS off still applied an always-enabled game pack");
    check(hpi::gameplayHash(peer.modVfs({"Local"}))==stockHash,"GAME MODS off loaded local gameplay");
    check(peer.modVfs({"Local"}).has("sounds/pack-test.wav"),"cosmetic packs must load with GAME MODS off");
    auto opts=host.room().opts;opts.gameMods=1;host.setGameOptions(opts);transition=2;
   } else if(transition==2 && host.room().opts.gameMods && host.room().mapsReady && peer.modPackage()) {
    check(host.modPackage()->digest==initialDigest,"reenabling changed selected packs");
    host.setGameModPacks({"Alpha"});transition=3;
   } else if(transition==3 && !host.modPending() && host.room().mapsReady && peer.modPackage() && host.modPackage()->digest!=initialDigest && peer.modPackage()->digest==host.modPackage()->digest) {
    alphaDigest=host.modPackage()->digest;check(alphaDigest!=initialDigest,"selection did not replace pack");host.setGameModPacks({"Beta","Alpha"});peer.setGameModPacks({"PeerGame"});transition=4;
   } else if(transition==4 && !host.modPending() && host.room().mapsReady && peer.modPackage() && peer.modPackage()->digest==initialDigest){check(std::filesystem::exists(peerCache/"ModCache"/(initialDigest+".takmods")),"peer cache not saved after recovery");transition=5;}
  }
  if(!started && transition==5 && ready && host.room().mapsReady && host.room().slots[1].ready){check(host.modPackage()&&peer.modPackage(),"start allowed before packs received");check(host.modPackage()->digest==peer.modPackage()->digest,"pack digest mismatch");host.startGame();started=true;}
  if(!loaded && !campaign && host.starting() && peer.starting()){setup(host,hostData,wh);setup(peer,peerData,wp);loaded=true;}
  if(loaded){
   auto advance=[&](net::MpClient& c,sim::World& world,uint32_t& tick,bool first){net::Bundle b;while((first || hashes.count(tick))&&c.takeBundle(tick,b)){
    for(const auto& cmd:b.cmds)sim::applyCommand(world,registry,cmd);for(const auto& ev:b.events)sim::applyEvent(world,ev);world.tick(1.f/net::kServerHz);auto h=world.stateHash();if(first)hashes[tick]=h;else check(hashes.at(tick)==h,"peers diverged");if(tick%net::kHashPeriod==0)c.sendHash(tick,h);++tick;}};
   advance(host,wh,ht,true);advance(peer,wp,pt,false);check(!host.desynced()&&!peer.desynced(),"server disagrees with game-mod simulation");
   if(ht>=90 && pt>=90 && !campaign){check(!badLoaded,"incorrect loaded hash was admitted");host.leaveGame();peer.leaveGame();loaded=false;campaign=1;}
  }
  // A campaign mission room: GAME MODS is forced off (even when asked for), the
  // ticked cosmetic pack folders and every game pack (always-enabled ones too) stay
  // out, and only the always-enabled cosmetic packs load.
  if(campaign==1 && host.state()==net::MpClient::State::Lobby){net::GameOptions o;o.gameMods=1;host.createGame("campaign","","King of the Hill",o,8,false,true,"camp01");campaign=2;}
  if(campaign==2 && host.state()==net::MpClient::State::InRoom && !host.room().mission.empty()){
   check(host.room().opts.gameMods==0,"a campaign room adopted GAME MODS");
   const auto data=host.modVfs({"HostArt"});
   check(data.has("sounds/host-always.wav"),"always-enabled cosmetic pack not loaded in a campaign");
   check(!data.has("sounds/host-art.wav"),"a cosmetic pack folder loaded in a campaign");
   check(!data.has("sounds/host-rules.wav"),"an always-enabled game pack loaded in a campaign");
   check(hpi::gameplayHash(data)==stockHash,"a campaign game loaded game mods");
   host.leaveGame();campaign=3;
  }
  // A private (single-player) room: the host's always-enabled game pack applies there too.
  if(campaign==3 && host.state()==net::MpClient::State::Lobby){net::GameOptions o;o.gameMods=1;host.setGameModPacks({});host.createGame("solo","","Ulasem Arena",o,2,false,true);campaign=4;}
  if(campaign==4 && host.state()==net::MpClient::State::InRoom && host.room().mapsReady && host.modPackage() && !host.modPending()){
   check(host.room().opts.gameMods==1,"a single-player room had game mods off");
   check(host.modPackage()->mapPath=="HostRules" && host.modPackage()->files->count("sounds/host-rules.wav"),"the single-player room's package lacks the always-enabled game pack");
   check(host.modVfs({}).has("sounds/host-rules.wav"),"the always-enabled game pack did not apply in single player");
   host.leaveGame();
   std::cout<<"PASS: multi-pack upload, peer download, cosmetic isolation, always-enabled cosmetic and game packs (host only, never a guest's, never in a campaign), gameplay loading, 90 lockstep ticks, a mod-free campaign room and a single-player room\n";return 0;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 throw std::runtime_error("game mod network test timed out: "+host.modStatus()+" / "+peer.modStatus()+" map="+host.mapStatus()+" err="+host.error()+" / "+peer.error());
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
