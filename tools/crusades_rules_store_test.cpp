// Synthetic policy persistence/atomicity tests; fixture rules are not historical.
#include "server/crusades/store.h"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace c=tak::srv::crusades;
namespace fs=std::filesystem;
namespace {
int checks=0;
void check(bool b,const char* m){++checks;if(!b)throw std::runtime_error(m);}
template<class F>void rejects(F f,const char* m){++checks;try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error(m);}
c::BattleContext context(){return {"one.ota",std::string(64,'a'),std::string(64,'b'),{"alice","bob"},true};}
void seed(c::CampaignStore& s,c::RulesPolicy policy={}){auto d=c::loadDefinitionText("campaign 1 \"test\" \"Test\"\nterritory 1 \"One\"\nmap 1 \"one.ota\"\nterritory 2 \"Neighbor\"\nneighbors 1 2\nneighbors 2 1\n");s.create(d,c::makeInitialState(d),"initial",policy);s.setAllegiance("test","alice",c::Alliance::Honor,-1,1);s.setAllegiance("test","bob",c::Alliance::Terror,-1,1);}
c::IssuedBattle started(c::CampaignStore& s,const std::string& room){auto b=s.issueBattle("test",s.load("test").revision,1,context(),10,20);s.startBattle(b.id,b.launchToken,room,context(),11);return b;}
c::VerifiedMatchResult result(const std::string& replay="replay-one"){
 c::VerifiedMatchResult r;r.outcome=c::ResultOutcome::Victory;r.winners={"alice"};r.finalTick=1234;r.finalStateHash=UINT64_MAX;r.gameplayFingerprint=UINT64_C(0x8000000000000001);r.engineBuild="test-build";r.replayId=replay;r.replayDigest=std::string(64,'c');
 r.participantResults={{"bob",1,2,-7,3,0,"Taros",1,true},{"alice",2,1,99,4,3,"Aramon",0,false}};return r;
}
struct Raw{sqlite3* db=nullptr;explicit Raw(const fs::path& p){auto bytes=p.u8string();if(sqlite3_open(reinterpret_cast<const char*>(bytes.c_str()),&db)!=SQLITE_OK)throw std::runtime_error("raw open");}~Raw(){sqlite3_close(db);}void sql(const std::string& s){char* e=nullptr;if(sqlite3_exec(db,s.c_str(),nullptr,nullptr,&e)!=SQLITE_OK){std::string m=e?e:"SQL";sqlite3_free(e);throw std::runtime_error(m);}}int64_t count(const char* s){sqlite3_stmt* q=nullptr;if(sqlite3_prepare_v2(db,s,-1,&q,nullptr)!=SQLITE_OK)throw std::runtime_error("prepare");int rc=sqlite3_step(q);auto n=sqlite3_column_int64(q,0);sqlite3_finalize(q);if(rc!=SQLITE_ROW)throw std::runtime_error("row");return n;}};
void historical(){c::CampaignStore s(":memory:");seed(s);auto b=started(s,"room");check(b.policyId=="historical-darien-v1","battle policy not pinned");s.recordVerifiedResult(b.id,"room",context(),result(),12);auto d=s.rulesDecision(b.id);check(d&&d->decision.disposition==c::RulesDisposition::UnknownRules&&d->decision.evidence==c::RulesEvidence::Unknown,"historical decision invented rules");check(!d->decision.changed&&d->beforeRevision==0&&d->afterRevision==0&&s.history("test").size()==1,"historical mutated campaign");check(!s.load("test").state.territories.at(1).owner,"historical invented owner");}
void fixture(const fs::path& p){bool fail=false;std::string id;const c::RulesPolicy policy{c::RulesMode::Fixture,2};
 {c::CampaignStore s(p,c::StoreOptions{[&]{if(fail)throw std::runtime_error("injected transaction failure");},true});seed(s,policy);auto b=started(s,"first");id=b.id;
 fail=true;rejects([&]{s.recordVerifiedResult(id,"first",context(),result(),12);},"rollback hook ignored");fail=false;
 check(!s.verifiedResult(id)&&!s.rulesDecision(id)&&s.load("test").revision==0&&s.history("test").size()==1&&s.battle(id).status==c::BattleStatus::Started,"atomic result/decision/state rollback failed");
 s.setAllegiance("test","carol",c::Alliance::Honor,-1,1);s.setAllegiance("test","david",c::Alliance::Terror,-1,1);
 auto staleContext=context();staleContext.participants={"carol","david"};
 auto stale=s.issueBattle("test",0,1,staleContext,10,20);s.startBattle(stale.id,stale.launchToken,"stale",staleContext,11);
 auto staleResult=result("stale");staleResult.winners={"carol"};staleResult.participantResults[0].accountId="david";staleResult.participantResults[1].accountId="carol";
 s.recordVerifiedResult(id,"first",context(),result(),12);auto d=s.rulesDecision(id);check(d&&d->beforeRevision==0&&d->afterRevision==1&&d->decision.changed&&d->decision.evidence==c::RulesEvidence::AuthoredFixture,"fixture credit audit missing");auto state=s.load("test");check(!state.state.territories.at(1).owner&&state.state.territories.at(1).recon.honor.battleVictoryPoints==1,"threshold captured early");
 rejects([&]{s.recordVerifiedResult(stale.id,"stale",staleContext,staleResult,12);},"stale battle applied");check(!s.rulesDecision(stale.id),"stale decision persisted");auto abort=staleResult;abort.replayId="abort";abort.outcome=c::ResultOutcome::ServerAbort;abort.winners.clear();s.recordVerifiedResult(stale.id,"stale",staleContext,abort,12);check(s.rulesDecision(stale.id)->decision.disposition==c::RulesDisposition::Ineligible&&s.load("test").revision==1,"abort changed fixture state");
 auto second=started(s,"second");s.recordVerifiedResult(second.id,"second",context(),result("second"),12);check(s.load("test").state.territories.at(1).owner==c::TerritoryOwner::Honor&&s.load("test").revision==2&&s.history("test").size()==3,"threshold capture/history missing");
 check(!s.load("test").state.territories.at(2).owner && !s.load("test").state.territories.at(2).recon.honor.battleVictoryPoints,"fixture propagated to neighbor");
 auto saturated=started(s,"third");s.recordVerifiedResult(saturated.id,"third",context(),result("third"),12);check(!s.rulesDecision(saturated.id)->decision.changed&&s.load("test").revision==2,"saturated policy invented mutation");
 }
 rejects([&]{c::CampaignStore s(p);},"live default opened fixture campaign");
 {c::CampaignStore s(p,c::StoreOptions{{},true});check(s.rulesDecision(id)->afterRevision==1&&s.load("test").revision==2,"reopen lost policy audit");rejects([&]{s.recordVerifiedResult(id,"first",context(),result(),13);},"reopen duplicate accepted");check(s.load("test").revision==2,"duplicate applied twice");}
 {Raw raw(p);rejects([&]{raw.sql("UPDATE campaign_rules SET policy_id='historical-darien-v1'");},"campaign policy mutable");rejects([&]{raw.sql("UPDATE issued_battle_rules SET policy_id='historical-darien-v1'");},"battle policy mutable");rejects([&]{raw.sql("DELETE FROM rule_decisions");},"decision deletable");check(raw.count("SELECT count(*) FROM rule_decisions")==4,"audit count wrong");raw.sql("DROP TRIGGER issued_battle_rules_no_update");raw.sql("UPDATE issued_battle_rules SET policy_id='historical-darien-v1'");raw.sql("CREATE TRIGGER issued_battle_rules_no_update BEFORE UPDATE ON issued_battle_rules BEGIN SELECT RAISE(ABORT,'immutable battle rules'); END");}
 {c::CampaignStore s(p,c::StoreOptions{{},true});rejects([&]{s.rulesDecision(id);},"wrong bound policy accepted");}
}
void gates(){c::CampaignStore s(":memory:");rejects([&]{seed(s,{c::RulesMode::Fixture,2});},"default permits fixture");rejects([&]{seed(s,{c::RulesMode::Modern,3});},"reserved modern policy accepted");check(!s.hasCampaign("test"),"rejected policy left campaign");}
void migration(const fs::path& p){std::string id;{c::CampaignStore s(p);seed(s);id=started(s,"old").id;s.recordVerifiedResult(id,"old",context(),result(),12);}
 {Raw r(p);for(const auto* name:{"territory_battle_history","battle_participants","campaign_rules","issued_battle_rules","rule_decisions"})r.sql(std::string("DROP TABLE ")+name);r.sql("PRAGMA user_version=4");}
 rejects([&]{c::CampaignStore s(p,c::StoreOptions{[]{throw std::runtime_error("migration rollback");}});},"migration hook ignored");{Raw r(p);check(r.count("PRAGMA user_version")==4,"migration rollback advanced version");}
 {c::CampaignStore s(p);check(c::policyIdentifier(s.load("test").rules)=="historical-darien-v1"&&s.battle(id).policyId=="historical-darien-v1","migration policy not historical");check(s.verifiedResult(id).has_value()&&!s.rulesDecision(id)&&s.load("test").revision==0,"legacy result retroactively applied");}
}
}
int main(){auto root=fs::temp_directory_path()/("tak-rules-store-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));struct Cleanup{fs::path p;~Cleanup(){std::error_code e;fs::remove_all(p,e);}}cleanup{root};try{fs::create_directories(root);historical();gates();fixture(root/"fixture.sqlite");migration(root/"migration.sqlite");std::cout<<"PASS: "<<checks<<" rules store checks\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
