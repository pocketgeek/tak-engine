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
int main(int argc,char** argv){try{
 check(argc>=4 && argc<=6,"port host-root peer-root [server-host] [--bad-loaded] required");
 const std::string serverHost=argc>=5?argv[4]:"127.0.0.1";const bool badLoaded=argc==6 && std::string(argv[5])=="--bad-loaded";auto hostRoot=std::filesystem::path(argv[2]),peerRoot=std::filesystem::path(argv[3]);
 auto base=hpi::mountRetailRoot(hostRoot,hpi::OverridePolicy::None);const auto original=base.read("units/araking.fbi");std::string text(original.begin(),original.end());
 const std::regex hp("maxdamage\\s*=\\s*[0-9]+",std::regex::icase);check(std::regex_search(text,hp),"monarch HP fixture missing");
 put(hostRoot,"overrides/Alpha/units/araking.fbi",std::regex_replace(text,hp,"MaxDamage=12345"));
 put(hostRoot,"overrides/Beta/units/araking.fbi",std::regex_replace(text,hp,"MaxDamage=23456"));
 put(hostRoot,"overrides/Alpha/sounds/pack-test.wav",std::string(80000,'a'));
 put(peerRoot,"overrides/Local/units/araking.fbi",std::regex_replace(text,hp,"MaxDamage=99999"));
 put(peerRoot,"overrides/Local/sounds/pack-test.wav","local");
 net::MpClient host,peer;host.setMapRoot(hostRoot);peer.setMapRoot(peerRoot);host.setHostOverridePacks({"Beta","Alpha"});
 auto stockHash=hpi::gameplayHash(base);host.setDataHash(stockHash);peer.setDataHash(stockHash);
 check(host.connect(serverHost,uint16_t(std::stoi(argv[1])),"pack-host"),"host connect");check(peer.connect(serverHost,uint16_t(std::stoi(argv[1])),"pack-peer"),"peer connect");
 sim::TypeRegistry registry;hpi::Vfs hostData,peerData;sim::World wh,wp;
 bool created=false,joined=false,ready=false,started=false,loaded=false;int transition=0;std::string initialDigest,alphaDigest;uint32_t ht=0,pt=0;std::map<uint32_t,uint64_t> hashes;
 auto setup=[&](net::MpClient& c,hpi::Vfs& data,sim::World& world){
  data=c.overrideVfs(hpi::OverridePolicy::Cosmetic,&c==&host?std::vector<std::string>{}:std::vector<std::string>{"Local","Removed pack"});
  check(hpi::gameplayHash(data)!=stockHash,"full pack did not change gameplay");
  if(&c==&host) {sim::setupRegistry(registry,data,false);check(registry.find("araking")->maxHp==23456,"alphabetical pack precedence lost");}
  else {check(hpi::gameplayHash(data)==hpi::gameplayHash(hostData),"peer cosmetics changed gameplay");check(data.read("sounds/pack-test.wav")==std::vector<uint8_t>({'l','o','c','a','l'}),"peer cosmetic pack not loaded");}
  data.setMapFiles(c.mapPackage()->files);sim::MatchConfig cfg;cfg.vfs=&data;cfg.mapPath=c.mapPackage()->mapPath;cfg.startSeed=c.startSeed();cfg.slots={{true,0,0,1,false,false},{true,1,1,1,false,false}};cfg.unitCap=2000;
  world.setVisPlayer(-1);sim::setupMatch(world,registry,cfg);c.reportLoaded(hpi::gameplayHash(data) ^ (badLoaded && &c==&host ? 1 : 0));
 };
 const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(90);
 while(std::chrono::steady_clock::now()<end){
  const bool hostOk=host.poll();
  if(badLoaded && loaded && host.error().find("override mismatch")!=std::string::npos){
   std::cout<<"PASS: verified Full packs transferred; mismatching loaded gameplay rejected\n";return 0;
  }
  check(hostOk,host.error().c_str());check(peer.poll(),peer.error().c_str());
  if(!created && host.state()==net::MpClient::State::Lobby){net::GameOptions o;o.overridePolicy=2;for(int duplicate=0;duplicate<16;++duplicate)host.createGame("packs","","Ulasem Arena",o,2);created=true;}
  if(!joined && host.room().id && peer.state()==net::MpClient::State::Lobby){for(int duplicate=0;duplicate<160;++duplicate)peer.joinGame(host.room().id,"");joined=true;}
  if(!ready && peer.room().id && peer.room().mySlot>=0){host.setSlot(0,1,0,0,0,1);peer.setSlot(1,1,1,1,1,1);ready=true;}
  if(ready && !started) {
   if(transition==0 && host.room().mapsReady && peer.overridePackage()) {
    initialDigest=host.overridePackage()->digest;auto opts=host.room().opts;opts.overridePolicy=0;host.setGameOptions(opts);transition=1;
   } else if(transition==1 && host.room().opts.overridePolicy==0 && peer.room().opts.overridePolicy==0) {
    check(!host.overridePackage()&&!peer.overridePackage(),"Off retained shared overrides");
    check(hpi::gameplayHash(peer.overrideVfs(hpi::OverridePolicy::Cosmetic,{"Local"}))==stockHash,"Off loaded local gameplay");
    auto opts=host.room().opts;opts.overridePolicy=2;host.setGameOptions(opts);transition=2;
   } else if(transition==2 && host.room().opts.overridePolicy==2 && host.room().mapsReady && peer.overridePackage()) {
    check(host.overridePackage()->digest==initialDigest,"reenabling changed selected packs");
    host.setHostOverridePacks({"Alpha"});transition=3;
   } else if(transition==3 && !host.overridePending() && host.room().mapsReady && peer.overridePackage() && host.overridePackage()->digest!=initialDigest && peer.overridePackage()->digest==host.overridePackage()->digest) {
    alphaDigest=host.overridePackage()->digest;check(alphaDigest!=initialDigest,"selection did not replace pack");host.setHostOverridePacks({"Beta","Alpha"});transition=4;
   } else if(transition==4 && !host.overridePending() && host.room().mapsReady && peer.overridePackage() && peer.overridePackage()->digest==initialDigest)transition=5;
  }
  if(!started && transition==5 && ready && host.room().mapsReady && host.room().slots[1].ready){check(host.overridePackage()&&peer.overridePackage(),"start allowed before packs received");check(host.overridePackage()->digest==peer.overridePackage()->digest,"pack digest mismatch");host.startGame();started=true;}
  if(!loaded && host.starting() && peer.starting()){setup(host,hostData,wh);setup(peer,peerData,wp);loaded=true;}
  if(loaded){
   auto advance=[&](net::MpClient& c,sim::World& world,uint32_t& tick,bool first){net::Bundle b;while((first || hashes.count(tick))&&c.takeBundle(tick,b)){
    for(const auto& cmd:b.cmds)sim::applyCommand(world,registry,cmd);for(const auto& ev:b.events)sim::applyEvent(world,ev);world.tick(1.f/net::kServerHz);auto h=world.stateHash();if(first)hashes[tick]=h;else check(hashes.at(tick)==h,"peers diverged");if(tick%net::kHashPeriod==0)c.sendHash(tick,h);++tick;}};
   advance(host,wh,ht,true);advance(peer,wp,pt,false);check(!host.desynced()&&!peer.desynced(),"server disagrees with full override simulation");
   if(ht>=90 && pt>=90){check(!badLoaded,"incorrect loaded hash was admitted");host.leaveGame();peer.leaveGame();std::cout<<"PASS: multi-pack upload, peer download, cosmetic isolation, gameplay loading and 90 lockstep ticks\n";return 0;}
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 throw std::runtime_error("override network test timed out: "+host.overrideStatus()+" / "+peer.overrideStatus()+" map="+host.mapStatus()+" err="+host.error()+" / "+peer.error());
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
