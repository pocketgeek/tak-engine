// Real file loader: incompatible protocols and corrupt inner/outer records
// must not silently launch a different simulation.
#include "client/replayfile.h"
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
 file.u32(1);file.u32(0);file.u64(1234);return file;
}
void run(){
 const auto root=std::filesystem::temp_directory_path()/("tak-replay-file-"+tak::crypto::toHex(tak::crypto::randomVec(12)));
 std::filesystem::create_directories(root);struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove_all(p,ec);}}cleanup{root};
 const auto path=root/std::filesystem::u8path("replay-\xc3\xa9.takrep");
 const auto utf=path.u8string();const std::string name(utf.begin(),utf.end());
 auto load=[&](const Writer& value,ReplayFile* decoded=nullptr){std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(value.b.data()),std::streamsize(value.b.size()));out.close();ReplayFile replay;const bool ok=loadReplayFile(name,replay);if(ok && decoded)*decoded=std::move(replay);return ok;};
 check(load(recording()),"current simulation recording refused");
 ReplayFile decoded;
 for(auto mode:{tak::sim::PathfindingMode::Retail,tak::sim::PathfindingMode::Flowfield,tak::sim::PathfindingMode::Cooperative,tak::sim::PathfindingMode::RetailPlus})
  check(load(recording(kNetVersion,Command{},0,false,mode),&decoded)&&decoded.cfg.pathfindingMode==mode,
        "recorded pathfinder did not reach match configuration");
 check(load(recording(kNetVersion,Command{},0,false,tak::sim::PathfindingMode::Cooperative,"campaign"),&decoded)&&
       decoded.cfg.pathfindingMode==tak::sim::PathfindingMode::Retail,"campaign replay used a non-Retail pathfinder");
 check(!load(recording(kNetVersion,Command{},0,false,tak::sim::PathfindingMode(4))),"unknown pathfinder accepted");
 check(!load(recording(kNetVersion,Command{},0,false,tak::sim::PathfindingMode(255))),"invalid pathfinder byte accepted");
 for(uint32_t protocol:{219,220,221,222,223,224,225,226})
  check(!load(recording(protocol,Command{},0,false,tak::sim::PathfindingMode::Flowfield)),
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
}
}
int main(){try{run();std::cout<<"PASS: "<<checks<<" replay file checks\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
