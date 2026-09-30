// Software SDL strategic UI driven through authenticated real MpClient.
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include "client/crusadesscreen.h"
#include "hpi/hpi.h"
#include "net/client.h"
#include "net/netcompat.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace tak;
namespace {
int checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
struct Peer {
 int listener=-1;uint16_t port=0;net::Conn conn;
 Peer(){std::string error;listener=net::listenOn(0,error,true);if(listener<0)throw std::runtime_error(error);sockaddr_storage address{};socklen_t n=sizeof address;if(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&n))throw std::runtime_error("getsockname");port=address.ss_family==AF_INET?ntohs(reinterpret_cast<sockaddr_in*>(&address)->sin_port):ntohs(reinterpret_cast<sockaddr_in6*>(&address)->sin6_port);}
 ~Peer(){if(listener>=0)net::sockClose(listener);}
 void acceptClient(){sockaddr_storage address{};socklen_t n=sizeof address;int fd=int(accept(listener,reinterpret_cast<sockaddr*>(&address),&n));check(fd>=0,"accept");net::setupSocket(fd);conn=net::Conn(fd);}
 net::Frame receive(net::MpClient& client){for(int i=0;i<2000;++i){client.poll();if(!conn.recv())throw std::runtime_error("peer receive");net::Frame f;if(conn.poll(f)){if(f.kind==net::Msg::Ping){conn.send(net::Msg::Pong);conn.flushWrite();continue;}return f;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error("timed out receiving client request");}
 void send(net::Msg kind,const net::Writer& w,net::MpClient& client){conn.send(kind,w);check(conn.flushWrite(),"peer send");for(int i=0;i<4;++i){client.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
 void login(net::MpClient& client){client.setLogin("alice","test-password");check(client.connect("127.0.0.1",port,"Alice"),"connect");acceptClient();check(receive(client).kind==net::Msg::Hello,"hello");send(net::Msg::AuthRequired,{},client);auto begin=receive(client);check(begin.kind==net::Msg::AuthBegin,"auth begin");net::Reader rd(begin.payload.data(),begin.payload.size());auto user=rd.str();auto nonce=rd.bytes();std::vector<uint8_t> salt(16,3),serverNonce(32,7);auto keys=auth::deriveKeys("test-password",salt.data(),salt.size(),1000);net::Writer challenge;challenge.u8(0);challenge.bytes(salt);challenge.u32(1000);challenge.bytes(serverNonce);send(net::Msg::AuthChallenge,challenge,client);auto proof=receive(client);check(proof.kind==net::Msg::AuthProof,"auth proof");auto transcript=auth::authMessage(user,nonce,serverNonce,salt,1000);auto signature=auth::serverSignature(keys.serverKey,transcript);net::Writer answer;answer.u8(uint8_t(net::AuthStatus::Ok));answer.bytes(signature.data(),signature.size());answer.str("");send(net::Msg::AuthResult,answer,client);net::Writer welcome;welcome.u32(1);welcome.str("Alice");send(net::Msg::Welcome,welcome,client);check(client.auth()==net::MpClient::Auth::Ok&&client.state()==net::MpClient::State::Lobby,"authenticated lobby");}
};
}
namespace cw=net::crusades;
namespace {
uint32_t request(Peer& p,net::MpClient& client,net::Msg msg,cw::RequestKind kind){auto f=p.receive(client);check(f.kind==msg,"unexpected UI request");return std::visit([](const auto& v){return v.requestId;},cw::decodeRequest(kind,f.payload));}
void reply(Peer& p,net::MpClient& client,net::Msg msg,cw::Response response){net::Writer w;w.b=cw::encode(response);p.send(msg,w,client);}
void click(CrusadesScreen& ui,int x,int y,uint8_t button=SDL_BUTTON_LEFT){SDL_Event e{};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=button;check(ui.input(e,x,y)==CrusadesScreen::Action::None,"unexpected navigation");}
void type(CrusadesScreen& ui,const char* value){SDL_Event e{};e.type=SDL_TEXTINPUT;SDL_strlcpy(e.text.text,value,sizeof e.text.text);ui.input(e,0,0);}
void run(){
 SDL_SetHint(SDL_HINT_VIDEODRIVER,"dummy");check(SDL_Init(SDL_INIT_VIDEO)==0,"SDL init");struct Quit{~Quit(){SDL_Quit();}}quit;
 SDL_Surface* surface=SDL_CreateRGBSurfaceWithFormat(0,1200,700,32,SDL_PIXELFORMAT_RGBA32);check(surface!=nullptr,"surface");SDL_Renderer* ren=SDL_CreateSoftwareRenderer(surface);check(ren!=nullptr,"renderer");struct Graphics{SDL_Surface* s;SDL_Renderer* r;~Graphics(){SDL_DestroyRenderer(r);SDL_FreeSurface(s);}}graphics{surface,ren};
 net::MpClient client;client.subscribeCampaign("test");Peer peer;peer.login(client);
 auto catalogId=request(peer,client,net::Msg::CrusadesListCampaigns,cw::RequestKind::List);auto snapId=request(peer,client,net::Msg::CrusadesGetSnapshot,cw::RequestKind::Snapshot);auto playerId=request(peer,client,net::Msg::CrusadesGetPlayerStatus,cw::RequestKind::PlayerStatus);
 reply(peer,client,net::Msg::CrusadesCampaignList,cw::CampaignList{catalogId,{{"test","Synthetic campaign",0,"historical-darien-v1"}}, {}});
 cw::Snapshot snap;snap.requestId=snapId;snap.campaignId="test";snap.displayName="Synthetic campaign";snap.rulesPolicy="historical-darien-v1";
 for(uint32_t id=1;id<=40;++id){cw::Territory t;t.id=id;t.displayName="Territory "+std::to_string(id);if(id==1){t.displayName="Élan meadow";t.mapIdentifier="one.ota";t.recon.fatigueVictoryPoints=0;t.activity=cw::BattleActivity{0,1};}snap.territories.push_back(t);}
 reply(peer,client,net::Msg::CrusadesCampaignSnapshot,snap);cw::PlayerStatus own;own.requestId=playerId;own.campaignId="test";reply(peer,client,net::Msg::CrusadesPlayerStatus,own);
 hpi::Vfs empty;CrusadesScreen ui(ren,empty,client);SDL_RenderSetScale(ren,1.1f,1.1f);SDL_Rect clip{0,0,960,540};SDL_RenderSetClipRect(ren,&clip);ui.draw();float xscale,yscale;SDL_RenderGetScale(ren,&xscale,&yscale);SDL_Rect after;SDL_RenderGetClipRect(ren,&after);check(xscale==1.1f&&yscale==1.1f&&after.w==clip.w&&after.h==clip.h,"screen changed parent render scale/clip");
 click(ui,40,135);check(ui.selectedTerritory()==1,"territory row selection");ui.draw();click(ui,250,465);type(ui,"bob");ui.draw();click(ui,500,465);check(peer.conn.recv(),"empty wire read");net::Frame none;check(!peer.conn.poll(none),"unenrolled request battle emitted");
 click(ui,45,465);auto join=peer.receive(client);check(join.kind==net::Msg::CrusadesSetAllegiance,"join allegiance action");net::Reader jr(join.payload.data(),join.payload.size());check(jr.str()=="test"&&jr.u64()==UINT64_MAX&&jr.u8()==1&&jr.ok&&jr.p==jr.end,"join has authenticated implicit identity and expected absence");
 own.requestId=0;own.allegiance=cw::PlayerAllegiance{cw::Alliance::Honor,4,1,2};reply(peer,client,net::Msg::CrusadesPlayerStatus,own);ui.draw();click(ui,500,465);auto issue=peer.receive(client);check(issue.kind==net::Msg::CrusadesIssueBattle,"duel action");net::Reader ir(issue.payload.data(),issue.payload.size());check(ir.str()=="test"&&ir.u32()==1&&ir.str()=="bob"&&ir.ok&&ir.p==ir.end,"duel selected correct territory and opponent only");
 if(const char* path=std::getenv("TAK_CRUSADES_UI_SCREENSHOT")){ui.draw();SDL_RenderPresent(ren);check(SDL_SaveBMP(surface,path)==0,"save UI preview");}
 click(ui,145,465);auto change=peer.receive(client);check(change.kind==net::Msg::CrusadesSetAllegiance,"switch action");net::Reader cr(change.payload.data(),change.payload.size());check(cr.str()=="test"&&cr.u64()==4&&cr.u8()==2,"switch expected server revision");
 click(ui,50,107);type(ui,"40");ui.update();ui.draw();click(ui,45,136);check(ui.selectedTerritory()==40,"search reaches offscreen territory");ui.draw();click(ui,500,465);client.poll();check(peer.conn.recv()&&!peer.conn.poll(none),"unknown map enabled duel");
 ui.selectTerritory(1);ui.draw(1200,675);click(ui,50,168);check(ui.selectedTerritory()==40,"scaled logical hit selection");ui.selectTerritory(999);ui.update();check(ui.selectedTerritory()==40,"invalid restored ID selected");
 ui.draw();cw::BattleStatus battle;battle.campaignId="test";battle.battleId="issued:sample";battle.territory=1;battle.mapIdentifier="one.ota";battle.expiresUnix=100; battle.roomId=7;reply(peer,client,net::Msg::CrusadesBattleStatus,battle);ui.draw();click(ui,300,358);ui.draw();click(ui,680,478);auto accept=peer.receive(client);check(accept.kind==net::Msg::JoinGame,"invitation acceptance action");net::Reader ar(accept.payload.data(),accept.payload.size());check(ar.u32()==7&&ar.str().empty(),"join invitation correct room");
 net::Writer seated;seated.u8(1);seated.u8(0);seated.str("");peer.send(net::Msg::JoinResult,seated,client);
 net::Writer lobby;lobby.u32(7);lobby.str("Campaign room");lobby.str("one.ota");lobby.str("");lobby.u8(1);lobby.u8(0);lobby.u8(0);lobby.u8(10);lobby.u8(0);lobby.u32(2000);for(int n=0;n<6;++n)lobby.u8(0);lobby.u32(99);lobby.u8(1);for(int n=0;n<net::kMaxSlots;++n){for(int f=0;f<6;++f)lobby.u8(0);lobby.str("");}peer.send(net::Msg::LobbyState,lobby,client);check(client.campaignRoom(),"issued room binding missing");
 battle.status=cw::BattlePhase::Completed;battle.roomId=0;battle.result=cw::BattleResult{cw::Outcome::Victory,1,123,{"alice"}};reply(peer,client,net::Msg::CrusadesBattleStatus,battle);check(client.campaignRoom(),"terminal status unlocked bound campaign lobby");
 const auto* unchanged=client.campaignReplica().find("test");check(unchanged&&unchanged->revision==0&&!unchanged->territories[0].owner&&unchanged->territories[0].recon.fatigueVictoryPoints==0&&!unchanged->territories[0].recon.honorSupportVictoryPoints,"UI invented owner/metrics or modified target snapshot");
 client.disconnect();ui.draw();SDL_Event e{};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;check(ui.input(e,45,118)==CrusadesScreen::Action::Reconnect,"disconnected sign-in action");e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_ESCAPE;check(ui.input(e,0,0)==CrusadesScreen::Action::Back,"escape/back action");
 SDL_RenderPresent(ren);
}
}
int main(){try{run();std::cout<<"PASS: "<<checks<<" Crusades SDL UI checks\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
