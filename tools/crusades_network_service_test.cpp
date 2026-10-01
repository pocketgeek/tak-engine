// Authenticated read service, privacy, paging, migrations and durable lifecycle.
#include "server/crusades/network.h"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
namespace c=tak::srv::crusades;
namespace fs=std::filesystem;
namespace {
int checks=0;
void check(bool b,const char* m){++checks;if(!b)throw std::runtime_error(m);}
template<class F>void rejects(F f,const char* m){++checks;try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error(m);}
c::BattleContext context(){return {"one.ota",std::string(64,'a'),std::string(64,'b'),{"alice","bob"},true};}
void seed(c::CampaignStore& s){auto d=c::loadDefinitionText("campaign 1 \"test\" \"Test\"\nterritory 1 \"One\"\nmap 1 \"one.ota\"\n");s.create(d,c::makeInitialState(d),"initial");s.setAllegiance("test","alice",c::Alliance::Honor,-1,1);s.setAllegiance("test","bob",c::Alliance::Terror,-1,1);}
c::IssuedBattle started(c::CampaignStore& s,const std::string& room){auto b=s.issueBattle("test",s.load("test").revision,1,context(),10,20);s.startBattle(b.id,b.launchToken,room,context(),11);return b;}
c::VerifiedMatchResult result(const std::string& replay="replay-one"){
 c::VerifiedMatchResult r;r.outcome=c::ResultOutcome::Victory;r.winners={"alice"};r.finalTick=1234;r.finalStateHash=UINT64_MAX;r.gameplayFingerprint=UINT64_C(0x8000000000000001);r.engineBuild="test-build";r.replayId=replay;r.replayDigest=std::string(64,'c');
 r.participantResults={{"bob",1,2,-7,3,0,"Taros",1,true},{"alice",2,1,99,4,3,"Aramon",0,false}};return r;
}
struct Raw{sqlite3* db=nullptr;explicit Raw(const fs::path& p){auto bytes=p.u8string();if(sqlite3_open(reinterpret_cast<const char*>(bytes.c_str()),&db)!=SQLITE_OK)throw std::runtime_error("raw open");}~Raw(){sqlite3_close(db);}void sql(const std::string& s){char* e=nullptr;if(sqlite3_exec(db,s.c_str(),nullptr,nullptr,&e)!=SQLITE_OK){std::string m=e?e:"SQL";sqlite3_free(e);throw std::runtime_error(m);}}int64_t count(const char* s){sqlite3_stmt* q=nullptr;if(sqlite3_prepare_v2(db,s,-1,&q,nullptr)!=SQLITE_OK)throw std::runtime_error("prepare");int rc=sqlite3_step(q);auto n=sqlite3_column_int64(q,0);sqlite3_finalize(q);if(rc!=SQLITE_ROW)throw std::runtime_error("row");return n;}};
namespace w=tak::net::crusades;
namespace n=tak::net;
w::Response unpack(const c::ReadResponse& r){w::ResponseKind k=w::ResponseKind::Error;switch(r.kind){case n::Msg::CrusadesCampaignList:k=w::ResponseKind::List;break;case n::Msg::CrusadesCampaignSnapshot:k=w::ResponseKind::Snapshot;break;case n::Msg::CrusadesPlayerStatus:k=w::ResponseKind::PlayerStatus;break;case n::Msg::CrusadesBattleStatus:k=w::ResponseKind::BattleStatus;break;case n::Msg::CrusadesTerritoryHistory:k=w::ResponseKind::TerritoryHistory;break;default:break;}return w::decodeResponse(k,r.payload.b);}
w::Response query(c::CampaignStore* s,const std::string& account,const w::Request& request,const c::RoomResolver& resolver={},const c::ReplayResolver& replays={}){n::Msg m=n::Msg::CrusadesListCampaigns;switch(w::kindOf(request)){case w::RequestKind::List:break;case w::RequestKind::Snapshot:m=n::Msg::CrusadesGetSnapshot;break;case w::RequestKind::PlayerStatus:m=n::Msg::CrusadesGetPlayerStatus;break;case w::RequestKind::BattleStatus:m=n::Msg::CrusadesGetBattleStatus;break;case w::RequestKind::TerritoryHistory:m=n::Msg::CrusadesGetTerritoryHistory;break;case w::RequestKind::ReplayChunk:case w::RequestKind::Matchmaking:case w::RequestKind::MatchSearch:case w::RequestKind::MatchCancel:throw std::runtime_error("matchmaking is owned by the live server service");}return unpack(c::handleCampaignRead(s,account,m,w::encode(request),resolver,{},replays));}
void expectError(const w::Response& r,w::ErrorCode code){check(std::holds_alternative<w::Error>(r)&&std::get<w::Error>(r).code==code,"wrong typed error");}
void authoredWireBounds(){
 auto make=[](const std::string& field){return c::loadDefinitionText("campaign 1 \"test\" \"Test\"\nterritory 1 \"One\"\n"+field);};
 const auto valid=make("map 1 \"one.ota\"\n");auto state=c::makeInitialState(valid);
 c::validateCampaignNetworkState(valid,state);check(!state.territories.at(1).owner&&!state.territories.at(1).assignedMap,"validation invented campaign state");
 const auto identifier=c::loadDefinitionText("campaign 1 \""+std::string(129,'x')+"\" \"Test\"\nterritory 1 \"One\"\n");
 rejects([&]{c::validateCampaignNetworkState(identifier,c::makeInitialState(identifier));},"unaddressable campaign ID accepted");
 for(const auto& field:std::vector<std::string>{"native 1 \""+std::string(1025,'x')+"\"\n","terrain 1 \""+std::string(1025,'x')+"\"\n","map 1 \""+std::string(4097,'x')+"\"\n"}) {
  const auto definition=make(field);rejects([&]{c::validateCampaignNetworkState(definition,c::makeInitialState(definition));},"unservable authored field accepted");
 }
 state.territories.at(1).assignedMap=std::string(4097,'x');rejects([&]{c::validateCampaignNetworkState(valid,state);},"unservable assigned map accepted");
 state.territories.at(1).assignedMap=std::string("\xc0\xaf");rejects([&]{c::validateCampaignNetworkState(valid,state);},"invalid runtime UTF-8 accepted");
 std::string crowded="campaign 1 \"crowded\" \"Crowded\"\n";
 for(unsigned id=1;id<=234;++id)crowded+="territory "+std::to_string(id)+" \""+std::string(1024,'x')+"\"\n";
 const auto nearLimit=c::loadDefinitionText(crowded);const auto initial=c::makeInitialState(nearLimit);
 c::CampaignStore store(":memory:");store.create(nearLimit,initial,"legacy authored state");
 check(c::campaignReadResponse(&store,"alice",w::SnapshotRequest{1,"crowded",0}).kind==n::Msg::CrusadesCampaignSnapshot,"near-limit snapshot without live activity should fit");
 rejects([&]{c::validateCampaignNetworkState(nearLimit,initial);},"runtime activity grew snapshot beyond wire cap");
 check(store.load("crowded").revision==0&&store.history("crowded").size()==1,"authored validation mutated store");
}
void authAndWire(){c::CampaignStore s(":memory:");seed(s);for(const w::Request& r:std::vector<w::Request>{w::ListRequest{1,"",64},w::SnapshotRequest{2,"test",0},w::PlayerStatusRequest{3,"test"},w::BattleStatusRequest{4,"unknown"}}){expectError(query(&s,"",r),w::ErrorCode::AuthenticationRequired);expectError(query(nullptr,"alice",r),w::ErrorCode::Disabled);expectError(query(&s,"Alice",r),w::ErrorCode::AuthenticationRequired);}
 auto bytes=w::encode(w::Request{w::SnapshotRequest{17,"test",0}});bytes[0]=99;expectError(unpack(c::handleCampaignRead(&s,"alice",n::Msg::CrusadesGetSnapshot,bytes)),w::ErrorCode::UnsupportedVersion);bytes=w::encode(w::Request{w::SnapshotRequest{17,"test",0}});bytes.push_back(42);expectError(unpack(c::handleCampaignRead(&s,"alice",n::Msg::CrusadesGetSnapshot,bytes)),w::ErrorCode::Malformed);
 expectError(unpack(c::handleCampaignRead(&s,"alice",n::Msg::CrusadesCampaignSnapshot,{})),w::ErrorCode::Malformed);expectError(query(&s,"alice",w::SnapshotRequest{1,"missing",0}),w::ErrorCode::NotFound);check(s.load("test").revision==0&&s.history("test").size()==1,"reads mutated campaign");}
void snapshots(){c::CampaignStore s(":memory:");seed(s);auto old=s.load("test");auto& t=old.state.territories.at(1);t.owner=c::TerritoryOwner::Contested;t.recon.honor.battleVictoryPoints=0;t.recon.terror.supportVictoryPoints=-1.25;t.assignedMap="runtime.ota";s.commit("test",0,old.state,"change");for(uint64_t revision:{uint64_t(0),uint64_t(1),uint64_t(99),UINT64_MAX}){auto snap=std::get<w::Snapshot>(query(&s,"alice",w::SnapshotRequest{19,"test",revision}));check(snap.requestId==19&&snap.revision==1&&snap.territories.size()==1,"stale/equal/ahead not full latest");auto& v=snap.territories.front();check(v.owner==w::Owner::Contested&&!v.neighbors&&v.recon.honorBattleVictoryPoints==0&&!v.recon.fatigueVictoryPoints&&v.recon.terrorSupportVictoryPoints==-1.25&&v.assignedMap=="runtime.ota","snapshot lost unknown/zero/metrics");}
 auto notification=std::get<w::Snapshot>(unpack(c::campaignReadResponse(&s,"alice",w::SnapshotRequest{0,"test",0})));check(notification.requestId==0,"notification correlation incorrect");auto observed=std::get<w::Snapshot>(unpack(c::campaignReadResponse(&s,"alice",w::SnapshotRequest{0,"test",0},{},[](const std::string& campaign,uint32_t territory){check(campaign=="test"&&territory==1,"activity resolver identity incorrect");return std::optional<w::BattleActivity>{{2,3}};})));check(observed.territories[0].activity&&observed.territories[0].activity->offered==2&&observed.territories[0].activity->active==3,"authoritative activity missing");check(!notification.territories[0].activity,"missing activity resolver invented zeros");}
void paging(){c::CampaignStore s(":memory:");for(int i=0;i<66;++i){std::string id="c"+std::to_string(100+i);auto d=c::loadDefinitionText("campaign 1 \""+id+"\" \"Display\"\nterritory 1 \"One\"\n");s.create(d,c::makeInitialState(d),"initial");}auto page=std::get<w::CampaignList>(query(&s,"alice",w::ListRequest{1,"",64}));check(page.entries.size()==64&&page.nextCursor=="c163","first catalog page incorrect");auto last=std::get<w::CampaignList>(query(&s,"alice",w::ListRequest{2,page.nextCursor,64}));check(last.entries.size()==2&&last.entries.front().id=="c164"&&last.nextCursor.empty(),"keyset catalog skips/repeats");rejects([&]{s.campaignIds("",65);},"unbounded catalog accepted");}
void lifecycle(const fs::path& path){std::string id;{c::CampaignStore s(path);seed(s);auto b=started(s,"server-secret-room");id=b.id;auto resolver=[](const std::string&){return std::optional<uint32_t>{42};};auto status=std::get<w::BattleStatus>(query(&s,"alice",w::BattleStatusRequest{1,id},resolver));check(status.roomId==42&&status.status==w::BattlePhase::Started&&!status.result,"live binding incorrect");auto encoded=w::encode(w::Response{status});std::string raw(encoded.begin(),encoded.end());check(raw.find(b.launchToken)==std::string::npos&&raw.find("server-secret-room")==std::string::npos&&raw.find(context().rulesDigest)==std::string::npos,"private capability/context disclosed");expectError(query(&s,"carol",w::BattleStatusRequest{2,id}),w::ErrorCode::NotFound);expectError(query(&s,"carol",w::BattleStatusRequest{2,"not-real"}),w::ErrorCode::NotFound);auto own=std::get<w::PlayerStatus>(query(&s,"alice",w::PlayerStatusRequest{3,"test"}));check(own.allegiance&&own.battles.size()==1&&own.battles.front().id==id,"own status missing membership");auto outsider=std::get<w::PlayerStatus>(query(&s,"carol",w::PlayerStatusRequest{4,"test"}));check(!outsider.allegiance&&outsider.battles.empty(),"own status leaked others");s.recordVerifiedResult(id,"server-secret-room",context(),result(),12);status=std::get<w::BattleStatus>(query(&s,"bob",w::BattleStatusRequest{5,id},resolver));check(status.roomId==0&&status.result&&status.result->winners==std::vector<std::string>{"alice"}&&status.result->finalStateHash==UINT64_MAX,"terminal result not authoritative");
 for(int i=0;i<35;++i){auto offer=s.issueBattle("test",0,1,context(),100+i,200+i);s.cancelBattle(offer.id,100+i);}
 own=std::get<w::PlayerStatus>(query(&s,"alice",w::PlayerStatusRequest{6,"test"}));check(own.battles.size()==32&&own.battlesTruncated,"own history not bounded/truncated");check(s.battle(own.battles.front().id).createdUnix==134,"own battles not newest first");check(s.ownBattleIds("test","carol").ids.empty(),"projection leaked outsider");rejects([&]{s.ownBattleIds("test","alice",33);},"unbounded own history accepted");}
 {c::CampaignStore s(path);auto status=std::get<w::BattleStatus>(query(&s,"alice",w::BattleStatusRequest{1,id}));check(status.result&&status.status==w::BattlePhase::Completed&&status.roomId==0,"restarted status lost durable result");}
 // Simulate valid schema5 and exercise projection migration/rollback.
 {Raw r(path);r.sql("DROP TABLE admin_events");r.sql("DROP TABLE territory_battle_history");r.sql("DROP TABLE battle_participants");r.sql("PRAGMA user_version=5");}
 rejects([&]{c::CampaignStore s(path,c::StoreOptions{[]{throw std::runtime_error("migration interrupted");}});},"projection migration rollback hook ignored");{Raw r(path);check(r.count("PRAGMA user_version")==5&&r.count("SELECT count(*) FROM sqlite_master WHERE name='battle_participants'")==0,"migration rollback partial");}
 {c::CampaignStore s(path);check(s.ownBattleIds("test","alice").ids.size()==32&&s.ownBattleIds("test","alice").truncated,"migration lost indexed own history");check(s.verifiedResult(id).has_value(),"migration lost result");}
 {Raw r(path);check(r.count("SELECT count(*) FROM battle_participants")==72,"migration projection count wrong");rejects([&]{r.sql("DELETE FROM battle_participants");},"projection mutable");
 sqlite3_stmt* q=nullptr;check(sqlite3_prepare_v2(r.db,"EXPLAIN QUERY PLAN SELECT battle_id FROM battle_participants WHERE campaign_id='test' AND account_id='alice' ORDER BY created_unix DESC,battle_id DESC LIMIT 33",-1,&q,nullptr)==SQLITE_OK,"query plan prepare");check(sqlite3_step(q)==SQLITE_ROW,"query plan row");std::string plan=reinterpret_cast<const char*>(sqlite3_column_text(q,3));sqlite3_finalize(q);check(plan.find("COVERING INDEX battle_participants_account")!=std::string::npos,"own history query not indexed");
 // A damaged prior-version roster must not produce a partial projection.
 r.sql("DROP TABLE admin_events");r.sql("DROP TABLE territory_battle_history");r.sql("DROP TABLE battle_participants");r.sql("PRAGMA user_version=5");
 check(sqlite3_prepare_v2(r.db,"SELECT sql FROM sqlite_master WHERE name='issued_battle_identity_no_update'",-1,&q,nullptr)==SQLITE_OK,"read trigger");check(sqlite3_step(q)==SQLITE_ROW,"trigger row");std::string trigger=reinterpret_cast<const char*>(sqlite3_column_text(q,0));sqlite3_finalize(q);
 r.sql("DROP TRIGGER issued_battle_identity_no_update");r.sql("UPDATE issued_battles SET context=x'00'");r.sql(trigger);
 }
 rejects([&]{c::CampaignStore s(path);},"invalid legacy context migrated");{Raw r(path);check(r.count("PRAGMA user_version")==5&&r.count("SELECT count(*) FROM sqlite_master WHERE name='battle_participants'")==0,"invalid context left partial migration");}
}
void territoryHistory(){
 c::CampaignStore store(":memory:");seed(store);std::vector<c::IssuedBattle> battles;
 for(int i=0;i<35;++i){const auto room="history-room-"+std::to_string(i);const auto b=started(store,room);store.recordVerifiedResult(b.id,room,context(),result("history-replay-"+std::to_string(i)),20+i/3);battles.push_back(store.battle(b.id));}
 const w::TerritoryHistoryRequest request{71,"test",1,{},16};
 expectError(query(&store,"",request),w::ErrorCode::AuthenticationRequired);
 expectError(query(nullptr,"alice",request),w::ErrorCode::Disabled);
 expectError(query(&store,"Alice",request),w::ErrorCode::AuthenticationRequired);
 expectError(query(&store,"carol",request),w::ErrorCode::Forbidden);
 expectError(query(&store,"alice",w::TerritoryHistoryRequest{72,"missing",1,{},16}),w::ErrorCode::NotFound);
 store.setAllegiance("test","carol",c::Alliance::Honor,-1,1);
 auto first=std::get<w::TerritoryHistory>(query(&store,"carol",request));
 check(first.requestId==71&&first.campaignId=="test"&&first.territory==1&&first.entries.size()==16&&first.nextCursor,"enrolled observer cannot read bounded history");
 check(first.entries[0].participants.size()==2&&first.entries[0].participants[1].score==-7&&first.entries[0].result.winners==std::vector<std::string>{"alice"},"history projection lost result/statistics");
 std::set<std::string> ids;for(const auto& row:first.entries){check(!row.replay,"absent resolver invented replay availability");check(ids.insert(row.battleId).second,"first page repeated battle");}
 auto second=std::get<w::TerritoryHistory>(query(&store,"carol",w::TerritoryHistoryRequest{73,"test",1,first.nextCursor,16}));
 check(second.entries.size()==16&&second.nextCursor,"second history page incomplete");for(const auto& row:second.entries)check(ids.insert(row.battleId).second,"next page repeated battle");
 auto third=std::get<w::TerritoryHistory>(query(&store,"bob",w::TerritoryHistoryRequest{74,"test",1,second.nextCursor,16}));
 check(third.entries.size()==3&&!third.nextCursor,"final history page cursor incorrect");for(const auto& row:third.entries)check(ids.insert(row.battleId).second,"last page repeated battle");
 check(ids.size()==35,"history paging lost battle");
 const auto available=[](const c::IssuedBattle& b,const c::VerifiedMatchResult& r){check(b.territory==1&&r.finalStateHash==UINT64_MAX,"resolver lost trusted identities");return std::optional<w::ReplayMetadata>{{r.replayDigest,512,9,210,b.context.mapDigest,r.gameplayFingerprint}};};
 auto playable=std::get<w::TerritoryHistory>(query(&store,"alice",request,{},available));
 check(playable.entries.size()==16&&playable.entries[0].replay&&playable.entries[0].replay->protocolVersion==210,"valid resolver metadata unavailable");
 const c::ReplayResolver throwing=[](const c::IssuedBattle&,const c::VerifiedMatchResult&)->std::optional<w::ReplayMetadata>{throw std::runtime_error("deleted/corrupt artifact");};
 auto missing=std::get<w::TerritoryHistory>(query(&store,"alice",request,{},throwing));
 check(missing.entries.size()==first.entries.size()&&!missing.entries[0].replay&&missing.entries[0].battleId==first.entries[0].battleId&&missing.entries[0].result.finalStateHash==UINT64_MAX,"artifact exception removed/changed history metadata");
 for(int kind=0;kind<7;++kind){
  const c::ReplayResolver badMetadata=[kind](const c::IssuedBattle& b,const c::VerifiedMatchResult& r)->std::optional<w::ReplayMetadata>{
   w::ReplayMetadata metadata{r.replayDigest,512,9,210,b.context.mapDigest,r.gameplayFingerprint};
   if(kind==0)metadata.digest=std::string(64,'d');
   if(kind==1)metadata.mapDigest=std::string(64,'b');
   if(kind==2)metadata.gameplayFingerprint^=1;
   if(kind==3)metadata.totalBytes=0;
   if(kind==4)metadata.format=0;
   if(kind==5)metadata.totalBytes=w::kMaxReplayBytes+1;
   if(kind==6)metadata.protocolVersion=0;
   return metadata;
  };
  const auto unavailable=std::get<w::TerritoryHistory>(query(&store,"alice",request,{},badMetadata));
  check(unavailable.entries.size()==first.entries.size()&&!unavailable.entries[0].replay&&unavailable.entries[0].battleId==first.entries[0].battleId&&unavailable.entries[0].result.finalStateHash==UINT64_MAX,"invalid resolver metadata hid or changed authoritative history");
 }
 const auto encoded=w::encode(w::Response{playable});const std::string bytes(encoded.begin(),encoded.end());
 for(const auto& b:battles){check(bytes.find(b.launchToken)==std::string::npos&&bytes.find(*b.roomToken)==std::string::npos&&bytes.find(b.context.rulesDigest)==std::string::npos,"history disclosed private launch/room/rules capability");}
 expectError(query(&store,"alice",w::TerritoryHistoryRequest{75,"test",1,w::HistoryCursor{999,battles.front().id},16}),w::ErrorCode::Unavailable);
 check(store.load("test").revision==0&&store.history("test").size()==1,"history service mutated territory state");
}
}
int main(){auto root=fs::temp_directory_path()/("tak-network-service-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));struct Cleanup{fs::path p;~Cleanup(){std::error_code e;fs::remove_all(p,e);}}cleanup{root};try{fs::create_directories(root);authoredWireBounds();authAndWire();snapshots();paging();lifecycle(root/"campaign.sqlite");territoryHistory();std::cout<<"PASS: "<<checks<<" campaign service checks\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
