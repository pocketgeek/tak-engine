// Real file loader: incompatible protocols and corrupt inner/outer records
// must not silently launch a different simulation.
#include "client/replayfile.h"
#include "client/postrail.h"
#include "legion_scn.h"
#include "net/crypto.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace tak::net;
namespace {
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
Writer recording(uint32_t protocol=kNetVersion,const Command& command=Command{},uint8_t event=0,bool innerTrailing=false,
                 tak::sim::PathfindingMode mode=tak::sim::PathfindingMode::Retail,const char* mission=""){
 Writer file;ReplayHeader header;header.mapId="Synthetic map";header.unitCap=2000;
 header.pathfindingMode=mode;header.mission=mission;
 header.slotType[0]=header.slotType[1]=1;header.slotFaction[1]=1;header.slotTeam[1]=1;
 writeReplayHeader(file,header);for(int i=0;i<4;++i)file.b[8+i]=uint8_t(protocol>>(8*i));
 if(protocol==219){file.b.erase(file.b.end()-kMaxSlots*5-2);file.b[4]=10;}
 Writer bundle;bundle.u32(0);bundle.u32(1);bundle.cmd(command);bundle.u32(event?1:0);
 if(event){bundle.u8(event);bundle.u8(0);}if(innerTrailing)bundle.u8(99);
 file.u32(1);file.u32(uint32_t(bundle.b.size()));file.b.insert(file.b.end(),bundle.b.begin(),bundle.b.end());
 file.u32(1);file.u32(0);file.u64(1234);file.u64(5678);return file;
}
void run(){
 const auto root=std::filesystem::temp_directory_path()/("tak-replay-file-"+tak::crypto::toHex(tak::crypto::randomVec(12)));
 std::filesystem::create_directories(root);struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove_all(p,ec);}}cleanup{root};
 const auto path=root/std::filesystem::u8path("replay-\xc3\xa9.takrep");
 const auto utf=path.u8string();const std::string name(utf.begin(),utf.end());
 auto load=[&](const Writer& value,ReplayFile* decoded=nullptr){std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(value.b.data()),std::streamsize(value.b.size()));out.close();ReplayFile replay;const bool ok=loadReplayFile(name,replay);if(ok && decoded)*decoded=std::move(replay);return ok;};
 check(load(recording()),"current simulation recording refused");
 ReplayFile decoded;
 for(auto mode:{tak::sim::PathfindingMode::Retail,tak::sim::PathfindingMode::Legion})
  check(load(recording(kNetVersion,Command{},0,false,mode),&decoded)&&decoded.cfg.pathfindingMode==mode,
        "recorded pathfinder did not reach match configuration");
 check(load(recording(kNetVersion,Command{},0,false,tak::sim::PathfindingMode::Legion,"campaign"),&decoded)&&
       decoded.cfg.pathfindingMode==tak::sim::PathfindingMode::Retail,"campaign replay used Legion");
 // 1-3 were the removed Flowfield, Cooperative and Retail+ identities.
 for(int removed:{1,2,3,5})
  check(!load(recording(kNetVersion,Command{},0,false,tak::sim::PathfindingMode(removed))),"unknown pathfinder accepted");
 check(!load(recording(kNetVersion,Command{},0,false,tak::sim::PathfindingMode(255))),"invalid pathfinder byte accepted");
 for(uint32_t protocol:{219,220,221,222,223,224,225,226,227})
  check(!load(recording(protocol,Command{},0,false,tak::sim::PathfindingMode::Legion)),
        "incompatible simulation recording accepted");
 Command area;area.kind=Cmd::BuildManaArea;area.unitId=1;area.x=100;area.z=200;area.x2=800;area.z2=900;
 std::snprintf(area.type,sizeof area.type,"aralode");
 check(load(recording(kNetVersion,area),&decoded) && decoded.cfg.patrolRepairs &&
       decoded.bundles.size()==1 && decoded.bundles.front().cmds.front().x2==800 &&
       decoded.bundles.front().cmds.front().z2==900,"current lodestone area replay lost its endpoints or patrol policy");
 check(!load(recording(220,area)),"legacy replay accepted a new area command");
 check(!load(recording(210)),"recording predating naval production correction accepted");
 check(!load(recording(211)),"protocol 211 recording accepted by changed simulation");
 check(!load(recording(209)),"older simulation recording accepted");
 check(!load(recording(kNetVersion+1)),"future protocol recording accepted");
 auto outer=recording();outer.u8(99);check(!load(outer),"outer trailing bytes accepted");
 check(!load(recording(kNetVersion,Command{},0,true)),"inner trailing bytes accepted");
 auto shortFile=recording();shortFile.b.pop_back();check(!load(shortFile),"truncated checkpoint accepted");
 Command bad;bad.kind=Cmd(255);check(!load(recording(kNetVersion,bad)),"unknown command accepted");
 bad=Command{};bad.x=std::numeric_limits<float>::quiet_NaN();check(!load(recording(kNetVersion,bad)),"nonfinite command accepted");
 bad=Command{};bad.player=kMaxSlots;check(!load(recording(kNetVersion,bad)),"invalid command player accepted");
 check(!load(recording(kNetVersion,Command{},255)),"unknown event accepted");
 check(load(recording(kNetVersion,Command{},uint8_t(Event::Kind::CampaignForfeit))),"real campaign forfeit event refused");
 // Format 12: the checkpoint carries posDigest. A format-11 file (12-byte records) still loads, digest absent.
 check(load(recording(),&decoded)&&decoded.formatVersion==12&&decoded.checks.size()==1&&decoded.checks[0].hash==1234&&
       decoded.checks[0].posDigest==5678,"format 12 checkpoint lost its position digest");
 {auto old=recording();old.b[4]=11;old.b.resize(old.b.size()-8);
  check(load(old,&decoded)&&decoded.formatVersion==11&&decoded.checks.size()==1&&decoded.checks[0].hash==1234&&
        decoded.checks[0].posDigest==0,"format 11 recording refused or misread");
  auto stale=recording();stale.b[4]=11;
  check(!load(stale),"format 11 file with format 12 records accepted");}
 // The tracker separates a hash-layout change from changed play.
 {using C=ReplayCheck;
  const C ck[3]={{0,10,100},{300,20,200},{600,30,300}};
  ReplayCheckTracker clean;for(const auto& c:ck)clean.observe(c,c.hash,[&]{return c.posDigest;});
  check(!clean.stateDiverged()&&clean.summary().empty(),"matching playback reported a divergence");
  ReplayCheckTracker layout;for(const auto& c:ck)layout.observe(c,c.hash+1,[&]{return c.posDigest;});
  check(layout.stateDiverged()&&layout.stateTick()==0&&!layout.posDiverged()&&
        layout.summary().find("hash layout change only")!=std::string::npos,"layout-only divergence not identified");
  ReplayCheckTracker play;for(const auto& c:ck)play.observe(c,c.hash+1,[&]{return c.tick<300?c.posDigest:c.posDigest+1;});
  check(play.stateDiverged()&&play.stateTick()==0&&play.posDiverged()&&play.posTick()==300&&
        play.summary()=="state diverged at tick 0; positions/hp/orders match until tick 300 (hash layout changed first, behaviour later)",
        "state-then-position divergence not reported with both ticks");
  ReplayCheckTracker none;for(const auto& c:ck)none.observe(C{c.tick,c.hash,0},c.hash+1,[&]{return 0ull;});
  check(none.stateDiverged()&&!none.haveDigest()&&none.summary().find("no position digest")!=std::string::npos,"old-format file claimed a digest");}
 // World::posDigest: stable for equal worlds, sensitive to positions and orders, independent of the hash layout.
 {const char* text="scn 1\nname pd\nticks 60\nmap flat 64 64\nplayers 2\ntype a mover 2 2500 10 1.8\n"
                   "group g 0 a 4 rect 5 5 20 20\nat 1 move all 40 40\n";
  auto digest=[&](bool order,int ticks){
   auto scn=tak::scn::parse(text);if(!order)scn.orders.clear();
   auto built=tak::scn::build(scn,{tak::sim::PathfindingMode::Legion,true,0,nullptr});
   tak::scn::OrderFeed feed(scn,*built);
   for(int t=0;t<ticks;++t){feed.apply(uint32_t(t));built->world->tick(1.f/30);}
   check(tak::postrail::digest(*built->world)==built->world->posDigest(),"postrail digest is not World::posDigest");
   return std::pair<uint64_t,uint64_t>{built->world->posDigest(),built->world->stateHash()};};
  const auto a=digest(true,40),b=digest(true,40),idle=digest(false,40),later=digest(true,41);
  check(a.first==b.first&&a.second==b.second,"posDigest not reproducible");
  check(a.first!=idle.first,"posDigest blind to orders and movement");
  check(a.first!=later.first,"posDigest blind to a tick of movement");
  check(a.first!=a.second,"posDigest equals the state hash");}
}
}
int main(){try{run();std::cout<<"PASS: "<<checks<<" replay file checks\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
