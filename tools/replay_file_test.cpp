// Real file loader: narrowly compatible strategic protocol bumps and corrupt
// inner/outer records must not silently launch a different simulation.
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
Writer recording(uint32_t protocol=kNetVersion,const Command& command=Command{},uint8_t event=0,bool innerTrailing=false){
 Writer file;ReplayHeader header;header.mapId="Synthetic map";header.unitCap=2000;
 header.slotType[0]=header.slotType[1]=1;header.slotFaction[1]=1;header.slotTeam[1]=1;
 writeReplayHeader(file,header);for(int i=0;i<4;++i)file.b[8+i]=uint8_t(protocol>>(8*i));
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
 auto load=[&](const Writer& value){std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(value.b.data()),std::streamsize(value.b.size()));out.close();ReplayFile replay;return loadReplayFile(name,replay);};
 check(load(recording(210)),"M10 recording refused after strategic protocol bump");
 check(load(recording(211)),"M11 recording refused");
 check(!load(recording(209)),"older simulation recording accepted");
 check(!load(recording(212)),"future protocol recording accepted");
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
