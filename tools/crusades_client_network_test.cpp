// Real MpClient over loopback; no game data or authoritative simulation needed.
#include "net/client.h"
#include "net/netcompat.h"
#include <chrono>
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
uint32_t request(net::Frame frame,net::Msg expected,cw::RequestKind kind){check(frame.kind==expected,"wrong campaign request kind");auto value=cw::decodeRequest(kind,frame.payload);return std::visit([](const auto& v){return v.requestId;},value);}
void reply(Peer& peer,net::MpClient& client,net::Msg kind,cw::Response value){net::Writer bytes;bytes.b=cw::encode(value);peer.send(kind,bytes,client);}
cw::Snapshot snapshot(uint32_t id,uint64_t revision){cw::Snapshot value;value.requestId=id;value.campaignId="test";value.displayName="Test";value.revision=revision;value.rulesPolicy="historical-darien-v1";cw::Territory t;t.id=1;t.displayName="One";t.owner=cw::Owner::Contested;t.recon.fatigueVictoryPoints=2.5;value.territories={t};return value;}
void run(){
 net::MpClient client;client.subscribeCampaign("test");check(client.getCampaignSnapshot("test")==0,"offline request sent");Peer peer;peer.login(client);
 const auto list=request(peer.receive(client),net::Msg::CrusadesListCampaigns,cw::RequestKind::List);
 const auto full=request(peer.receive(client),net::Msg::CrusadesGetSnapshot,cw::RequestKind::Snapshot);
 const auto player=request(peer.receive(client),net::Msg::CrusadesGetPlayerStatus,cw::RequestKind::PlayerStatus);
 check(list&&full&&player&&list!=full&&full!=player,"unique nonzero request IDs");
 reply(peer,client,net::Msg::CrusadesCampaignList,cw::CampaignList{list,{{"test","Test",2,"historical-darien-v1"}}, {}});
 reply(peer,client,net::Msg::CrusadesCampaignSnapshot,snapshot(full,2));
 cw::PlayerStatus status;status.requestId=player;status.campaignId="test";status.campaignRevision=2;status.allegiance=cw::PlayerAllegiance{cw::Alliance::Honor,0,1,1};
 reply(peer,client,net::Msg::CrusadesPlayerStatus,status);
 check(client.campaignList()&&client.campaignList()->entries.size()==1,"catalog cached");check(client.campaignReplica().find("test")&&client.campaignReplica().find("test")->revision==2,"snapshot cached");check(client.playerCampaignStatus()&&client.playerCampaignStatus()->allegiance,"own status cached");
 client.startRecording();net::Writer bundle;bundle.u32(9);bundle.u32(0);bundle.u32(0);peer.send(net::Msg::TickBundle,bundle,client);
 check(client.bufferedBundles()==1&&client.replayLog().size()==1,"baseline tactical bundle");const auto log=client.replayLog();const auto state=client.state();
 reply(peer,client,net::Msg::CrusadesCampaignSnapshot,snapshot(99999,99));check(client.campaignReplica().find("test")->revision==2,"unsolicited nonzero request ID accepted");
 const auto correlationId=client.getCampaignSnapshot("test");request(peer.receive(client),net::Msg::CrusadesGetSnapshot,cw::RequestKind::Snapshot);
 reply(peer,client,net::Msg::CrusadesCampaignList,cw::CampaignList{correlationId,{}, {}});check(client.campaignList()->entries.size()==1&&client.campaignError(),"wrong response family published");
 reply(peer,client,net::Msg::CrusadesCampaignSnapshot,snapshot(correlationId,2));
 auto higher=snapshot(0,3);reply(peer,client,net::Msg::CrusadesCampaignSnapshot,higher);
 check(client.campaignReplica().find("test")->revision==3,"unsolicited selected full snapshot");
 auto other=higher;other.campaignId="other";reply(peer,client,net::Msg::CrusadesCampaignSnapshot,other);check(!client.campaignReplica().find("other"),"unsubscribed snapshot accepted");
 auto conflict=snapshot(0,3);conflict.displayName="Conflicting";reply(peer,client,net::Msg::CrusadesCampaignSnapshot,conflict);check(client.campaignReplica().find("test")->displayName=="Test"&&client.campaignError(),"equal revision conflict changed state");
 reply(peer,client,net::Msg::CrusadesCampaignSnapshot,snapshot(0,2));auto refresh=peer.receive(client);const auto refreshId=request(refresh,net::Msg::CrusadesGetSnapshot,cw::RequestKind::Snapshot);check(std::get<cw::SnapshotRequest>(cw::decodeRequest(cw::RequestKind::Snapshot,refresh.payload)).expectedRevision==cw::kUnknownRevision,"stale refresh not full");
 reply(peer,client,net::Msg::CrusadesCampaignSnapshot,snapshot(refreshId,1));check(client.campaignReplica().find("test")->revision==3,"stale refresh replaced state");
 net::Writer malformed;malformed.b=cw::encode(cw::Response{snapshot(0,4)});malformed.b.pop_back();peer.send(net::Msg::CrusadesCampaignSnapshot,malformed,client);check(client.campaignReplica().find("test")->revision==3&&client.campaignError()->code==cw::ErrorCode::Malformed,"malformed snapshot published");
 for(int i=0;i<4;++i){client.poll();}
 check(peer.conn.recv(),"read after malformed");net::Frame extra;check(!peer.conn.poll(extra),"runaway stale/malformed refresh");
 const auto explicitId=client.getCampaignSnapshot("test");request(peer.receive(client),net::Msg::CrusadesGetSnapshot,cw::RequestKind::Snapshot);reply(peer,client,net::Msg::CrusadesCampaignSnapshot,snapshot(explicitId,4));check(client.campaignReplica().find("test")->revision==4,"explicit recovery failed");
 net::Writer invitation;invitation.u8(0);invitation.str("test");invitation.str("issued:test");invitation.u32(7);invitation.str(std::string(300,'m'));invitation.u64(999);invitation.str("");peer.send(net::Msg::CrusadesBattleResult,invitation,client);
 const auto battleId=request(peer.receive(client),net::Msg::CrusadesGetBattleStatus,cw::RequestKind::BattleStatus);check(client.campaignInvitation()&&client.campaignInvitation()->roomId==7,"legacy invitation not exposed");
 cw::BattleStatus battle;battle.requestId=battleId;battle.campaignId="test";battle.battleId="issued:test";battle.campaignRevision=4;battle.territory=1;battle.status=cw::BattlePhase::Issued;battle.mapIdentifier=std::string(300,'m');battle.expiresUnix=999;battle.roomId=7;
 reply(peer,client,net::Msg::CrusadesBattleStatus,battle);battle.requestId=0;battle.status=cw::BattlePhase::Started;reply(peer,client,net::Msg::CrusadesBattleStatus,battle);check(client.campaignBattles().at("issued:test").status==cw::BattlePhase::Started,"lifecycle notification ignored");
 battle.status=cw::BattlePhase::Issued;reply(peer,client,net::Msg::CrusadesBattleStatus,battle);check(client.campaignBattles().at("issued:test").status==cw::BattlePhase::Started,"battle status regressed");
 battle.status=cw::BattlePhase::Completed;battle.roomId=0;battle.result=cw::BattleResult{cw::Outcome::Victory,100,123,{"alice"}};reply(peer,client,net::Msg::CrusadesBattleStatus,battle);
 battle.result->finalStateHash=999;reply(peer,client,net::Msg::CrusadesBattleStatus,battle);check(client.campaignBattles().at("issued:test").result->finalStateHash==123,"terminal result rewritten");
 client.subscribeCampaign("other");request(peer.receive(client),net::Msg::CrusadesGetSnapshot,cw::RequestKind::Snapshot);const auto latePlayer=request(peer.receive(client),net::Msg::CrusadesGetPlayerStatus,cw::RequestKind::PlayerStatus);
 client.subscribeCampaign("test");request(peer.receive(client),net::Msg::CrusadesGetSnapshot,cw::RequestKind::Snapshot);const auto currentPlayer=request(peer.receive(client),net::Msg::CrusadesGetPlayerStatus,cw::RequestKind::PlayerStatus);
 status.requestId=currentPlayer;status.campaignRevision=4;reply(peer,client,net::Msg::CrusadesPlayerStatus,status);status.requestId=latePlayer;status.campaignId="other";reply(peer,client,net::Msg::CrusadesPlayerStatus,status);check(client.playerCampaignStatus()->campaignId=="test","old subscription response replaced selected player status");
 status.requestId=0;status.campaignId="test";status.allegiance->alliance=cw::Alliance::Terror;reply(peer,client,net::Msg::CrusadesPlayerStatus,status);check(client.playerCampaignStatus()->allegiance->alliance==cw::Alliance::Honor,"same revision allegiance rewritten");
 check(client.state()==state&&client.bufferedBundles()==1&&client.replayLog()==log&&client.hashLog().empty(),"campaign traffic affected tactical state/recording");net::Bundle taken;check(client.takeBundle(9,taken)&&taken.cmds.empty()&&taken.events.empty(),"campaign traffic altered bundle");
 client.disconnect();check(!client.campaignList()&&!client.campaignReplica().find("test")&&!client.playerCampaignStatus()&&client.campaignBattles().empty()&&!client.campaignInvitation(),"disconnect retained server caches");check(client.subscribedCampaign()=="test","disconnect lost subscription");
 Peer second;second.login(client);request(second.receive(client),net::Msg::CrusadesListCampaigns,cw::RequestKind::List);const auto renewed=request(second.receive(client),net::Msg::CrusadesGetSnapshot,cw::RequestKind::Snapshot);request(second.receive(client),net::Msg::CrusadesGetPlayerStatus,cw::RequestKind::PlayerStatus);reply(second,client,net::Msg::CrusadesCampaignSnapshot,snapshot(renewed,0));check(client.campaignReplica().find("test")->revision==0,"old server revision survived reconnect");client.disconnect();
 net::MpClient anonymous;Peer open;check(anonymous.connect("127.0.0.1",open.port,"guest"),"anonymous connect");open.acceptClient();check(open.receive(anonymous).kind==net::Msg::Hello,"anonymoushello");net::Writer welcome;welcome.u32(2);welcome.str("guest");open.send(net::Msg::Welcome,welcome,anonymous);check(anonymous.listCampaigns()==0,"anonymous campaign request permitted");reply(open,anonymous,net::Msg::CrusadesCampaignSnapshot,snapshot(0,1));check(!anonymous.campaignReplica().find("test"),"anonymous notification accepted");anonymous.disconnect();
}
}
int main(){try{run();std::cout<<"PASS: "<<checks<<" real MpClient campaign checks\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
