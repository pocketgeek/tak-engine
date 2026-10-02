// Real MpClient over loopback; no game data or authoritative simulation needed.
#include "net/client.h"
#include "net/netcompat.h"
#include <chrono>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <deque>
using namespace tak;
namespace {
int checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
struct Peer {
 int listener=-1;uint16_t port=0;net::Conn conn;std::deque<net::Frame> pending;
 Peer(){std::string error;listener=net::listenOn(0,error,true);if(listener<0)throw std::runtime_error(error);sockaddr_storage address{};socklen_t n=sizeof address;if(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&n))throw std::runtime_error("getsockname");port=address.ss_family==AF_INET?ntohs(reinterpret_cast<sockaddr_in*>(&address)->sin_port):ntohs(reinterpret_cast<sockaddr_in6*>(&address)->sin6_port);}
 ~Peer(){if(listener>=0)net::sockClose(listener);}
 void acceptClient(){
  // A completed client connect can precede server-side readiness on macOS.
  // The listener is nonblocking; wait for delivery, not a scheduler coincidence.
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(std::chrono::steady_clock::now()<deadline){
   sockaddr_storage address{};socklen_t n=sizeof address;
   int fd=int(accept(listener,reinterpret_cast<sockaddr*>(&address),&n));
   if(fd>=0){net::setupSocket(fd);conn=net::Conn(fd);return;}
   const int error=net::sockErr();
   if(!net::sockWouldBlock(error)&&!net::sockInterrupted(error))
    throw std::runtime_error("accept: "+net::sockErrStr(error));
   std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  throw std::runtime_error("timed out accepting client connection");
 }
 net::Frame receive(net::MpClient& client){if(!pending.empty()){auto f=std::move(pending.front());pending.pop_front();return f;}for(int i=0;i<2000;++i){client.poll();if(!conn.recv())throw std::runtime_error("peer receive");net::Frame f;if(conn.poll(f)){if(f.kind==net::Msg::Ping){conn.send(net::Msg::Pong);conn.flushWrite();continue;}return f;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error("timed out receiving client request");}
 // A Pong behind the response proves the client processed it. Four 1ms polls
 // were not a delivery guarantee under parallel sweep load (especially replay chunks).
 void send(net::Msg kind,const net::Writer& w,net::MpClient& client){
  conn.send(kind,w);conn.send(net::Msg::Ping);
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(std::chrono::steady_clock::now()<deadline){
   check(conn.flushWrite(),"peer send");client.poll();check(conn.recv(),"peer acknowledgement receive");
   net::Frame f;while(conn.poll(f)){
    if(f.kind==net::Msg::Pong)return;
    if(f.kind==net::Msg::Ping){conn.send(net::Msg::Pong);continue;}
    pending.push_back(std::move(f));
   }
   std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  throw std::runtime_error("timed out waiting for client to process response");
 }
 void login(net::MpClient& client){client.setLogin("alice","test-password");check(client.connect("127.0.0.1",port,"Alice"),"connect");acceptClient();check(receive(client).kind==net::Msg::Hello,"hello");send(net::Msg::AuthRequired,{},client);auto begin=receive(client);check(begin.kind==net::Msg::AuthBegin,"auth begin");net::Reader rd(begin.payload.data(),begin.payload.size());auto user=rd.str();auto nonce=rd.bytes();std::vector<uint8_t> salt(16,3),serverNonce(32,7);auto keys=auth::deriveKeys("test-password",salt.data(),salt.size(),1000);net::Writer challenge;challenge.u8(0);challenge.bytes(salt);challenge.u32(1000);challenge.bytes(serverNonce);send(net::Msg::AuthChallenge,challenge,client);auto proof=receive(client);check(proof.kind==net::Msg::AuthProof,"auth proof");auto transcript=auth::authMessage(user,nonce,serverNonce,salt,1000);auto signature=auth::serverSignature(keys.serverKey,transcript);net::Writer answer;answer.u8(uint8_t(net::AuthStatus::Ok));answer.bytes(signature.data(),signature.size());answer.str("");send(net::Msg::AuthResult,answer,client);net::Writer welcome;welcome.u32(1);welcome.str("Alice");send(net::Msg::Welcome,welcome,client);check(client.auth()==net::MpClient::Auth::Ok&&client.state()==net::MpClient::State::Lobby,"authenticated lobby");}
};
}
namespace cw=net::crusades;
namespace {
uint32_t request(net::Frame frame,net::Msg expected,cw::RequestKind kind){check(frame.kind==expected,"wrong campaign request kind");auto value=cw::decodeRequest(kind,frame.payload);return std::visit([](const auto& v){return v.requestId;},value);}
void reply(Peer& peer,net::MpClient& client,net::Msg kind,cw::Response value){net::Writer bytes;bytes.b=cw::encode(value);peer.send(kind,bytes,client);}
cw::Snapshot snapshot(uint32_t id,uint64_t revision){cw::Snapshot value;value.requestId=id;value.campaignId="test";value.displayName="Test";value.revision=revision;value.rulesPolicy="historical-darien-v1";cw::Territory t;t.id=1;t.displayName="One";t.owner=cw::Owner::Contested;t.recon.fatigueVictoryPoints=2.5;value.territories={t};return value;}
void matchmaking(){
 net::MpClient client;Peer peer;peer.login(client);
 request(peer.receive(client),net::Msg::CrusadesListCampaigns,cw::RequestKind::List);
 client.subscribeCampaign("test");
 const auto full=request(peer.receive(client),net::Msg::CrusadesGetSnapshot,cw::RequestKind::Snapshot);
 request(peer.receive(client),net::Msg::CrusadesGetPlayerStatus,cw::RequestKind::PlayerStatus);
 reply(peer,client,net::Msg::CrusadesCampaignSnapshot,snapshot(full,2));
 const auto read=client.getCampaignMatchmaking("test");
 check(read==request(peer.receive(client),net::Msg::CrusadesGetMatchmaking,cw::RequestKind::Matchmaking),"matchmaking request not correlated");
 cw::MatchmakingStatus board;board.requestId=read;board.campaignId="test";board.campaignRevision=2;board.generation=1;board.canSearch=true;
 board.territories={{1,true,1,0,0,0}};
 reply(peer,client,net::Msg::CrusadesMatchmakingStatus,board);
 check(client.campaignMatchmaking()&&client.campaignMatchmaking()->generation==1,"matchmaking board not cached");
 const auto search=client.searchCampaignBattle("test",1);auto frame=peer.receive(client);
 check(search==request(frame,net::Msg::CrusadesSearchBattle,cw::RequestKind::MatchSearch)&&std::get<cw::MatchSearchRequest>(cw::decodeRequest(cw::RequestKind::MatchSearch,frame.payload)).territory==1,"match search identity/correlation");
 board.requestId=search;board.generation=2;board.searchingTerritory=1;board.searchExpiresUnix=123;
 reply(peer,client,net::Msg::CrusadesMatchmakingStatus,board);
 check(client.campaignMatchmaking()->searchingTerritory==1,"search operation response not accepted");
 auto invalid=board;invalid.requestId=0;invalid.generation=1;invalid.searchingTerritory.reset();invalid.searchExpiresUnix.reset();
 reply(peer,client,net::Msg::CrusadesMatchmakingStatus,invalid);
 check(client.campaignMatchmaking()->generation==2&&client.campaignMatchmaking()->searchingTerritory==1,"old queue generation rolled status back");
 invalid=board;invalid.requestId=0;invalid.territories[0].waitingHonor=9;
 reply(peer,client,net::Msg::CrusadesMatchmakingStatus,invalid);
 check(client.campaignMatchmaking()->territories[0].waitingHonor==1,"equal generation conflicting queue overwritten");
 invalid=board;invalid.requestId=0;invalid.campaignRevision=3;
 reply(peer,client,net::Msg::CrusadesMatchmakingStatus,invalid);
 check(client.campaignMatchmaking()->campaignRevision==2&&client.campaignError()->code==cw::ErrorCode::StaleRevision,"equal generation with changed persisted revision accepted");
 invalid=board;invalid.requestId=0;invalid.generation=3;invalid.campaignRevision=1;
 reply(peer,client,net::Msg::CrusadesMatchmakingStatus,invalid);
 check(client.campaignMatchmaking()->generation==2,"lower persisted revision with higher queue generation accepted");
 invalid=board;invalid.requestId=0;invalid.generation=3;invalid.campaignId="other";
 reply(peer,client,net::Msg::CrusadesMatchmakingStatus,invalid);
 check(client.campaignMatchmaking()->campaignId=="test","unsubscribed matchmaking update published");
 net::Writer malformed;malformed.b=cw::encode(cw::Response{board});malformed.b.pop_back();peer.send(net::Msg::CrusadesMatchmakingStatus,malformed,client);
 check(client.campaignMatchmaking()->generation==2&&client.campaignError()->code==cw::ErrorCode::Malformed,"partial malformed matchmaking state published");
 const auto wrong=client.getCampaignMatchmaking("test");request(peer.receive(client),net::Msg::CrusadesGetMatchmaking,cw::RequestKind::Matchmaking);
 reply(peer,client,net::Msg::CrusadesCampaignSnapshot,snapshot(wrong,3));
 check(client.campaignReplica().find("test")->revision==2,"wrong matchmaking response family mutated snapshot");
 const auto wrongTarget=client.getCampaignMatchmaking("test");request(peer.receive(client),net::Msg::CrusadesGetMatchmaking,cw::RequestKind::Matchmaking);
 invalid=board;invalid.requestId=wrongTarget;invalid.generation=3;invalid.campaignId="other";
 reply(peer,client,net::Msg::CrusadesMatchmakingStatus,invalid);
 check(client.campaignMatchmaking()->generation==2&&client.campaignError()->code==cw::ErrorCode::Malformed,"mismatched queue target accepted");
 const auto cancel=client.cancelCampaignSearch("test");
 check(cancel==request(peer.receive(client),net::Msg::CrusadesCancelSearch,cw::RequestKind::MatchCancel),"cancel request not correlated");
 board.requestId=cancel;board.generation=3;board.searchingTerritory.reset();board.searchExpiresUnix.reset();
 reply(peer,client,net::Msg::CrusadesMatchmakingStatus,board);
 check(!client.campaignMatchmaking()->searchingTerritory,"cancel operation response not accepted");
 board.requestId=0;board.generation=4;board.canSearch=false;board.territories[0].eligible=false;
 reply(peer,client,net::Msg::CrusadesMatchmakingStatus,board);
 check(!client.campaignMatchmaking()->canSearch,"unsolicited authoritative queue update ignored");
 const auto late=client.getCampaignMatchmaking("test");request(peer.receive(client),net::Msg::CrusadesGetMatchmaking,cw::RequestKind::Matchmaking);
 client.subscribeCampaign("other");
 request(peer.receive(client),net::Msg::CrusadesGetSnapshot,cw::RequestKind::Snapshot);request(peer.receive(client),net::Msg::CrusadesGetPlayerStatus,cw::RequestKind::PlayerStatus);
 check(!client.campaignMatchmaking(),"subscription change retained old queue");
 board.requestId=late;board.generation=5;reply(peer,client,net::Msg::CrusadesMatchmakingStatus,board);
 check(!client.campaignMatchmaking(),"late old subscription reply restored queue");
 client.subscribeCampaign("test");
 request(peer.receive(client),net::Msg::CrusadesGetSnapshot,cw::RequestKind::Snapshot);request(peer.receive(client),net::Msg::CrusadesGetPlayerStatus,cw::RequestKind::PlayerStatus);
 board.requestId=late;board.generation=5;reply(peer,client,net::Msg::CrusadesMatchmakingStatus,board);
 check(!client.campaignMatchmaking(),"late reply restored old queue after switching away and back");
 board.requestId=0;board.generation=0;reply(peer,client,net::Msg::CrusadesMatchmakingStatus,board);
 check(client.campaignMatchmaking()&&client.campaignMatchmaking()->generation==0,"subscription reset retained stale queue generation");
 client.subscribeCampaign("");check(!client.campaignMatchmaking(),"empty subscription retained queue");
 client.disconnect();check(client.getCampaignMatchmaking("test")==0&&client.searchCampaignBattle("test",1)==0&&client.cancelCampaignSearch("test")==0&&!client.campaignMatchmaking(),"offline queue operation/cache retained");
}
void historyAndReplay(){
 struct Scratch {
  std::filesystem::path root=std::filesystem::temp_directory_path()/("tak-replay-test-"+crypto::toHex(crypto::randomVec(12)));
  Scratch(){std::filesystem::create_directories(root);}~Scratch(){std::error_code ec;std::filesystem::remove_all(root,ec);}
 } scratch;
 net::MpClient client;client.setCampaignReplayCacheRoot(scratch.root);Peer peer;peer.login(client);
 request(peer.receive(client),net::Msg::CrusadesListCampaigns,cw::RequestKind::List);
 net::Writer replay; net::ReplayHeader header;header.mapId="maps/one";header.crusades=1;header.engineVersion="test";net::writeReplayHeader(replay,header);
 replay.b.resize(cw::kReplayChunkBytes+1234,13); const auto digest=crypto::toHex(crypto::sha256(replay.b.data(),replay.b.size()));
 cw::HistoryBattle battle;battle.battleId="battle-z";battle.territory=1;battle.campaignRevision=2;battle.recordedUnix=100;battle.mapIdentifier="maps/one";
 battle.result={cw::Outcome::Victory,100,123,{"alice"}};battle.participants={{"alice",1,2,-3,4,5,"Aramon",0,false},{"bob",2,1,3,4,5,"Veruna",1,true}};
 battle.replay=cw::ReplayMetadata{digest,replay.b.size(),net::kReplayFormat,net::kNetVersion,{},0};
 auto historyId=client.getTerritoryHistory("test",1);check(historyId==request(peer.receive(client),net::Msg::CrusadesGetTerritoryHistory,cw::RequestKind::TerritoryHistory),"history request correlation");
 cw::TerritoryHistory history{historyId,"test",1,{battle},cw::HistoryCursor{100,"battle-z"}};
 reply(peer,client,net::Msg::CrusadesTerritoryHistory,history);check(client.territoryHistory("test",1)&&client.territoryHistory("test",1)->entries[0].participants[0].score==-3,"history not cached or negative score changed");
 const auto staleId=client.getTerritoryHistory("test",1);request(peer.receive(client),net::Msg::CrusadesGetTerritoryHistory,cw::RequestKind::TerritoryHistory);
 const auto currentId=client.getTerritoryHistory("test",1);request(peer.receive(client),net::Msg::CrusadesGetTerritoryHistory,cw::RequestKind::TerritoryHistory);
 history.requestId=currentId;reply(peer,client,net::Msg::CrusadesTerritoryHistory,history);
 auto empty=history;empty.requestId=staleId;empty.entries.clear();empty.nextCursor.reset();reply(peer,client,net::Msg::CrusadesTerritoryHistory,empty);
 check(client.territoryHistory("test",1)->entries.size()==1,"late page overwrote newer page");
 history.requestId=client.getTerritoryHistory("test",1);request(peer.receive(client),net::Msg::CrusadesGetTerritoryHistory,cw::RequestKind::TerritoryHistory);history.entries[0].result.finalStateHash=124;
 reply(peer,client,net::Msg::CrusadesTerritoryHistory,history);check(client.territoryHistory("test",1)->entries[0].result.finalStateHash==123&&client.campaignError(),"conflicting archive result published");history.entries[0]=battle;
 const auto paged=client.getTerritoryHistory("test",1,cw::HistoryCursor{100,"battle-z"},1);auto pageRequest=peer.receive(client);
 request(pageRequest,net::Msg::CrusadesGetTerritoryHistory,cw::RequestKind::TerritoryHistory);check(std::get<cw::TerritoryHistoryRequest>(cw::decodeRequest(cw::RequestKind::TerritoryHistory,pageRequest.payload)).cursor->battleId=="battle-z","history cursor lost");
 history.requestId=paged;reply(peer,client,net::Msg::CrusadesTerritoryHistory,history);check(client.campaignError()&&client.territoryHistory("test",1)->requestId==currentId,"cursor boundary replay accepted");
 auto first=client.requestCampaignReplay(battle.battleId);check(first!=0,"download unavailable");
 auto pullFrame=peer.receive(client);check(first==request(pullFrame,net::Msg::CrusadesGetReplayChunk,cw::RequestKind::ReplayChunk),"first replay pull correlation");
 auto pull=std::get<cw::ReplayChunkRequest>(cw::decodeRequest(cw::RequestKind::ReplayChunk,pullFrame.payload));check(pull.offset==0&&pull.limit==65536,"initial replay pull bounds");
 cw::ReplayChunk chunk{first,battle.battleId,digest,replay.b.size(),0,cw::Bytes(replay.b.begin(),replay.b.begin()+65536),false};reply(peer,client,net::Msg::CrusadesReplayChunk,chunk);
 check(client.campaignReplayDownload().receivedBytes==65536&&client.campaignReplayDownload().state==net::MpClient::CampaignReplayState::Downloading,"first chunk not streamed");
 pullFrame=peer.receive(client);pull=std::get<cw::ReplayChunkRequest>(cw::decodeRequest(cw::RequestKind::ReplayChunk,pullFrame.payload));check(pull.offset==65536,"next pull out of order");
 chunk.requestId=pull.requestId;chunk.offset=65536;chunk.bytes.assign(replay.b.begin()+65536,replay.b.end());chunk.final=true;reply(peer,client,net::Msg::CrusadesReplayChunk,chunk);
 const auto path=client.campaignReplayDownload().path;check(client.campaignReplayDownload().state==net::MpClient::CampaignReplayState::Ready&&std::filesystem::file_size(path)==replay.b.size(),"complete replay not published");
 check(client.requestCampaignReplay(battle.battleId)==UINT32_MAX&&client.campaignReplayDownload().path==path,"verified cache not reused");
 {std::ofstream corrupt(path,std::ios::binary|std::ios::trunc);corrupt<<"corrupt";}
 first=client.requestCampaignReplay(battle.battleId);check(first!=0&&first!=UINT32_MAX&&!std::filesystem::exists(path),"corrupt cache reused");pullFrame=peer.receive(client);
 chunk={first,battle.battleId,digest,replay.b.size(),0,cw::Bytes(replay.b.begin(),replay.b.begin()+65536),false};chunk.bytes[0]^=1;
 reply(peer,client,net::Msg::CrusadesReplayChunk,chunk);pullFrame=peer.receive(client);pull=std::get<cw::ReplayChunkRequest>(cw::decodeRequest(cw::RequestKind::ReplayChunk,pullFrame.payload));
 chunk.requestId=pull.requestId;chunk.offset=65536;chunk.bytes.assign(replay.b.begin()+65536,replay.b.end());chunk.final=true;reply(peer,client,net::Msg::CrusadesReplayChunk,chunk);
 check(client.campaignReplayDownload().state==net::MpClient::CampaignReplayState::Failed&&std::filesystem::is_empty(scratch.root),"digest mismatch published/leaked partial replay");
 for(int mismatch=0;mismatch<4;++mismatch){
  first=client.requestCampaignReplay(battle.battleId);pullFrame=peer.receive(client);
  chunk={first,battle.battleId,digest,replay.b.size(),0,cw::Bytes(replay.b.begin(),replay.b.begin()+65536),false};
  if(mismatch==0)chunk.battleId="other-battle";
  if(mismatch==1)chunk.digest=std::string(64,'a');
  if(mismatch==2)chunk.offset=1;
  if(mismatch==3)chunk.totalBytes++;
  reply(peer,client,net::Msg::CrusadesReplayChunk,chunk);
  check(client.campaignReplayDownload().state==net::MpClient::CampaignReplayState::Failed&&std::filesystem::is_empty(scratch.root),"mismatched chunk published/leaked partial");
 }
 first=client.requestCampaignReplay(battle.battleId);pullFrame=peer.receive(client);reply(peer,client,net::Msg::CrusadesError,cw::Error{first,cw::ErrorCode::Forbidden,"test",{},"Not enrolled"});
 check(client.campaignReplayDownload().state==net::MpClient::CampaignReplayState::Failed&&client.campaignReplayDownload().error=="Not enrolled"&&std::filesystem::is_empty(scratch.root),"forbidden replay not cleaned up");
 first=client.requestCampaignReplay(battle.battleId);pullFrame=peer.receive(client);reply(peer,client,net::Msg::CrusadesError,cw::Error{first,cw::ErrorCode::Unavailable,"test",{},"Replay is unavailable"});
 check(client.campaignReplayDownload().state==net::MpClient::CampaignReplayState::Failed&&std::filesystem::is_empty(scratch.root),"missing replay artifact retained partial file");
 first=client.requestCampaignReplay(battle.battleId);pullFrame=peer.receive(client);net::Writer truncated;truncated.b={uint8_t(cw::kVersion)};peer.send(net::Msg::CrusadesReplayChunk,truncated,client);
 check(client.campaignReplayDownload().state==net::MpClient::CampaignReplayState::Failed&&std::filesystem::is_empty(scratch.root),"truncated chunk retained partial file");
 first=client.requestCampaignReplay(battle.battleId);pullFrame=peer.receive(client);client.cancelCampaignReplayDownload();chunk={first,battle.battleId,digest,replay.b.size(),0,cw::Bytes(replay.b.begin(),replay.b.begin()+65536),false};reply(peer,client,net::Msg::CrusadesReplayChunk,chunk);
 check(client.campaignReplayDownload().state==net::MpClient::CampaignReplayState::Idle&&std::filesystem::is_empty(scratch.root),"cancelled transfer revived by late chunk");
 history.entries[0]=battle;history.entries[0].replay.reset();history.nextCursor.reset();history.requestId=client.getTerritoryHistory("test",1);request(peer.receive(client),net::Msg::CrusadesGetTerritoryHistory,cw::RequestKind::TerritoryHistory);reply(peer,client,net::Msg::CrusadesTerritoryHistory,history);
 check(client.requestCampaignReplay(battle.battleId)==0&&std::filesystem::is_empty(scratch.root),"unavailable history replay requested");
 history.entries[0]=battle;history.entries[0].battleId="unsupported";history.entries[0].replay->protocolVersion=999;history.requestId=client.getTerritoryHistory("test",1);request(peer.receive(client),net::Msg::CrusadesGetTerritoryHistory,cw::RequestKind::TerritoryHistory);reply(peer,client,net::Msg::CrusadesTerritoryHistory,history);
 check(client.requestCampaignReplay("unsupported")==0&&std::filesystem::is_empty(scratch.root),"incompatible replay metadata requested");
 history.entries[0]=battle;history.requestId=client.getTerritoryHistory("test",1);request(peer.receive(client),net::Msg::CrusadesGetTerritoryHistory,cw::RequestKind::TerritoryHistory);reply(peer,client,net::Msg::CrusadesTerritoryHistory,history);
 // Fill 512 newer immutable records across pages while keeping territory1's
 // original page visible. Its oldest record is evicted from the smaller map.
 for(uint32_t territory=2;territory<=33;++territory){
  cw::TerritoryHistory page;page.campaignId="test";page.territory=territory;
  for(int n=15;n>=0;--n){auto entry=battle;entry.territory=territory;entry.battleId="zz-"+std::to_string(territory)+"-"+std::to_string(1000+n);entry.recordedUnix=1000+n;entry.replay.reset();entry.participants[0].score=INT64_MIN;entry.participants[0].kills=uint64_t(INT64_MAX);page.entries.push_back(std::move(entry));}
  page.requestId=client.getTerritoryHistory("test",territory);request(peer.receive(client),net::Msg::CrusadesGetTerritoryHistory,cw::RequestKind::TerritoryHistory);reply(peer,client,net::Msg::CrusadesTerritoryHistory,page);
 }
 check(client.territoryHistory("test",33)->entries[0].participants[0].score==INT64_MIN&&client.territoryHistory("test",33)->entries[0].participants[0].kills==uint64_t(INT64_MAX),"extreme signed/unsigned archive stats changed");
 const auto retainedId=client.territoryHistory("test",1)->requestId;
 for(int changed=0;changed<3;++changed){
  history.entries[0]=battle;if(changed==0)history.entries[0].result.finalStateHash++;if(changed==1)history.entries[0].participants[0].score--;if(changed==2)history.entries[0].participants[0].kills++;
  history.requestId=client.getTerritoryHistory("test",1);request(peer.receive(client),net::Msg::CrusadesGetTerritoryHistory,cw::RequestKind::TerritoryHistory);reply(peer,client,net::Msg::CrusadesTerritoryHistory,history);
  check(client.territoryHistory("test",1)->requestId==retainedId&&client.territoryHistory("test",1)->entries[0].result.finalStateHash==123&&client.territoryHistory("test",1)->entries[0].participants[0].score==-3&&client.territoryHistory("test",1)->entries[0].participants[0].kills==1,"evicted record lost immutable protection while its page remained cached");
 }
 first=client.requestCampaignReplay(battle.battleId);pullFrame=peer.receive(client);client.disconnect();
 check(!client.territoryHistory("test",1)&&client.campaignReplayDownload().state==net::MpClient::CampaignReplayState::Idle&&std::filesystem::is_empty(scratch.root),"disconnect retained download/history");
 Peer second;second.login(client);request(second.receive(client),net::Msg::CrusadesListCampaigns,cw::RequestKind::List);check(client.requestCampaignReplay(battle.battleId)==0,"reconnect reused old server history");client.disconnect();
}
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
 check(peer.conn.recv(),"read after malformed");net::Frame extra;check(peer.pending.empty()&&!peer.conn.poll(extra),"runaway stale/malformed refresh");
 const auto explicitId=client.getCampaignSnapshot("test");request(peer.receive(client),net::Msg::CrusadesGetSnapshot,cw::RequestKind::Snapshot);reply(peer,client,net::Msg::CrusadesCampaignSnapshot,snapshot(explicitId,4));check(client.campaignReplica().find("test")->revision==4,"explicit recovery failed");
 net::Writer invitation;invitation.u8(0);invitation.str("test");invitation.str("issued:test");invitation.u32(7);invitation.str(std::string(300,'m'));invitation.u64(999);invitation.str("");peer.send(net::Msg::CrusadesBattleResult,invitation,client);
 const auto battleId=request(peer.receive(client),net::Msg::CrusadesGetBattleStatus,cw::RequestKind::BattleStatus);check(client.campaignInvitation()&&client.campaignInvitation()->roomId==7,"legacy invitation not exposed");
 cw::BattleStatus battle;battle.requestId=battleId;battle.campaignId="test";battle.battleId="issued:test";battle.campaignRevision=4;battle.territory=1;battle.status=cw::BattlePhase::Issued;battle.mapIdentifier=std::string(300,'m');battle.expiresUnix=999;battle.roomId=7;
 reply(peer,client,net::Msg::CrusadesBattleStatus,battle);battle.requestId=0;
 battle.roomId=8;reply(peer,client,net::Msg::CrusadesBattleStatus,battle);
 check(client.campaignBattles().at("issued:test").roomId==7,"issued battle rebound to a different room");
 battle.roomId=7;battle.status=cw::BattlePhase::Started;reply(peer,client,net::Msg::CrusadesBattleStatus,battle);check(client.campaignBattles().at("issued:test").status==cw::BattlePhase::Started,"lifecycle notification ignored");
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
int main(){try{run();matchmaking();historyAndReplay();std::cout<<"PASS: "<<checks<<" real MpClient campaign checks\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
