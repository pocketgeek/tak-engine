// M5 issued-battle capability tests: synthetic maps/accounts, no retail rules.
#include "server/crusades/store.h"
#include <sqlite3.h>
#include <chrono>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>

namespace c=tak::srv::crusades;
namespace fs=std::filesystem;
namespace {
int checks=0;
void check(bool ok,const std::string& what){++checks;if(!ok)throw std::runtime_error(what);}
template<class F>void rejects(F f,const std::string& what){++checks;try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error("accepted invalid battle: "+what);}
c::CampaignDefinition definition(){return c::loadDefinitionText(
    "campaign 1 \"synthetic\" \"Synthetic\"\nterritory 1 \"One\"\nmap 1 \"maps/one.ota\"\nterritory 2 \"Unmapped\"\n");}
void seed(c::CampaignStore& store){auto d=definition();store.create(d,c::makeInitialState(d),"start");
    store.setAllegiance("synthetic","alice",c::Alliance::Honor,-1,1);
    store.setAllegiance("synthetic","bob",c::Alliance::Terror,-1,1);}
c::BattleContext context(){return {"maps/one.ota",std::string(64,'a'),std::string(64,'b'),{"alice","bob"},true};}
c::IssuedBattle issue(c::CampaignStore& s,int64_t now=100,int64_t expires=200){return s.issueBattle("synthetic",0,1,context(),now,expires);}
void issueValidation(const fs::path& path){
    c::CampaignStore s(path);seed(s);auto good=context();
    rejects([&]{s.issueBattle("absent",0,1,good,100,200);},"unknown campaign");
    rejects([&]{s.issueBattle("synthetic",1,1,good,100,200);},"stale campaign revision");
    rejects([&]{s.issueBattle("synthetic",0,99,good,100,200);},"unknown territory");
    rejects([&]{s.issueBattle("synthetic",0,2,good,100,200);},"missing authored/runtime map");
    rejects([&]{issue(s,100,100);},"nonfuture expiry");rejects([&]{issue(s,-1,200);},"negative issuance time");
    for(int kind=0;kind<8;++kind){auto bad=good;
        switch(kind){case 0:bad.mapIdentifier="maps/other.ota";break;case 1:bad.mapDigest="";break;
        case 2:bad.rulesDigest="";break;case 3:bad.crusadesBalance=false;break;case 4:bad.participants={"alice"};break;
        case 5:bad.participants={"alice","alice"};break;case 6:bad.participants={"Alice","bob"};break;
        case 7:bad.participants={"alice","unknown"};break;}
        rejects([&]{s.issueBattle("synthetic",0,1,bad,100,200);},"invalid issuance context");
    }
    s.setAllegiance("synthetic","charlie",c::Alliance::Honor,-1,1);auto same=good;same.participants={"alice","charlie"};
    rejects([&]{s.issueBattle("synthetic",0,1,same,100,200);},"same alliance opponents");
    std::set<std::string> ids,tokens;
    for(int n=0;n<12;++n){const auto b=issue(s);check(ids.insert(b.id).second,"globally distinct generated ID");
        check(tokens.insert(b.launchToken).second,"distinct generated launch capability");
        check(b.status==c::BattleStatus::Issued && b.campaignRevision==0 && b.territory==1,"issued record pins campaign state");
        s.cancelBattle(b.id,100);}
    auto reversed=good;reversed.participants={"bob","alice"};auto canonical=s.issueBattle("synthetic",0,1,reversed,100,200);
    check(canonical.context.participants==good.participants && canonical.participantAlliances==std::vector<c::Alliance>{c::Alliance::Honor,c::Alliance::Terror},"sorted roster preserves account-side associations");
    check(s.load("synthetic").revision==0 && s.history("synthetic").size()==1,"issuance does not apply campaign points or state mutations");
}
void binding(const fs::path& path){
    c::CampaignStore s(path);seed(s);const auto b=issue(s);auto good=context();
    rejects([&]{s.authorizeBattleReport(b.id,"room-one",good,110);},"unstarted battle cannot report");
    rejects([&]{s.startBattle(b.id,"forged", "room-one",good,110);},"forged launch token");
    rejects([&]{s.startBattle(b.id,b.launchToken,"",good,110);},"empty room binding");
    auto bad=good;bad.mapDigest=std::string(64,'c');
    rejects([&]{s.startBattle(b.id,b.launchToken,"room-one",bad,110);},"wrong map contents at start");
    s.startBattle(b.id,b.launchToken,"room-one",good,110);
    check(s.battle(b.id).status==c::BattleStatus::Started && s.battle(b.id).roomToken=="room-one","server room bound once");
    rejects([&]{s.startBattle(b.id,b.launchToken,"room-two",good,111);},"launch capability cannot replay");
    s.setAllegiance("synthetic","carol",c::Alliance::Honor,-1,1);
    s.setAllegiance("synthetic","david",c::Alliance::Terror,-1,1);
    auto otherContext=good;otherContext.participants={"carol","david"};
    const auto other=s.issueBattle("synthetic",0,1,otherContext,100,200);
    rejects([&]{s.startBattle(other.id,other.launchToken,"room-one",otherContext,111);},"room identity cannot bind another battle");
    rejects([&]{s.authorizeBattleReport(b.id,"ordinary-room",good,115);},"ordinary match cannot forge room identity");
    rejects([&]{s.authorizeBattleReport("invented-id","room-one",good,115);},"unissued battle cannot report");
    for(int kind=0;kind<5;++kind){bad=good;switch(kind){case 0:bad.mapIdentifier="maps/other.ota";break;
        case 1:bad.mapDigest=std::string(64,'c');break;case 2:bad.rulesDigest=std::string(64,'d');break;
        case 3:bad.participants={"alice","charlie"};break;case 4:bad.crusadesBalance=false;break;}
        rejects([&]{s.authorizeBattleReport(b.id,"room-one",bad,115);},"bound match context cannot be substituted");}
    rejects([&]{s.authorizeBattleReport(b.id,"room-one",good,109);},"report time cannot precede launch");
    const auto saved=s.load("synthetic");
    rejects([&]{s.commit("synthetic",0,saved.state,"forged credit",c::BattleResult{b.id,"I won"});},"generic result API cannot apply issued battle credit");
    check(!s.battleResult("synthetic",b.id) && s.load("synthetic").revision==0,"forged generic credit is atomic no-op");
    check(s.authorizeBattleReport(b.id,"room-one",good,250).id==b.id,"launch expiry does not terminate already-started match");
    rejects([&]{s.completeBattle(b.id,"room-one",good,251);},"unverified completion disabled");
    s.cancelBattle(b.id,251);
    check(s.battle(b.id).status==c::BattleStatus::Cancelled,"cancellation is terminal marker");
    rejects([&]{s.completeBattle(b.id,"room-one",good,252);},"completed battle cannot complete twice");
    rejects([&]{s.authorizeBattleReport(b.id,"room-one",good,252);},"completed battle cannot report again");
    rejects([&]{s.startBattle(b.id,b.launchToken,"room-two",good,252);},"completed battle cannot launch again");
    check(!s.battleResult("synthetic",b.id) && s.load("synthetic").revision==0,"cancellation creates no campaign credit or result payload");
}
void terminalAndStale(const fs::path& path){
    c::CampaignStore s(path);seed(s);auto good=context();auto expired=issue(s);
    rejects([&]{s.startBattle(expired.id,expired.launchToken,"expired-room",good,200);},"deadline is exclusive");
    s.expireBattles(200);check(s.battle(expired.id).status==c::BattleStatus::Expired,"unlaunched deadline expires");
    rejects([&]{s.startBattle(expired.id,expired.launchToken,"expired-room",good,199);},"expired status cannot resurrect with older time");
    auto cancelled=issue(s,300,400);s.cancelBattle(cancelled.id,310);
    check(s.battle(cancelled.id).status==c::BattleStatus::Cancelled,"cancelled battle terminal");
    rejects([&]{s.startBattle(cancelled.id,cancelled.launchToken,"cancelled-room",good,311);},"cancelled battle cannot launch");
    auto changed=issue(s,400,500);
    s.setAllegiance("synthetic","alice",c::Alliance::Terror,0,410);
    s.setAllegiance("synthetic","alice",c::Alliance::Honor,1,420);
    rejects([&]{s.startBattle(changed.id,changed.launchToken,"changed-room",good,430);},"switch-away-and-back invalidates pinned allegiance revision");
    s.cancelBattle(changed.id,430);
    auto changedAfterStart=issue(s,450,490);s.startBattle(changedAfterStart.id,changedAfterStart.launchToken,"changed-started-room",good,451);
    s.setAllegiance("synthetic","bob",c::Alliance::Honor,0,452);
    s.setAllegiance("synthetic","bob",c::Alliance::Terror,1,453);
    rejects([&]{s.authorizeBattleReport(changedAfterStart.id,"changed-started-room",good,454);},"postlaunch allegiance revision change blocks credit");
    s.cancelBattle(changedAfterStart.id,454);
    auto stale=issue(s,500,600);s.startBattle(stale.id,stale.launchToken,"stale-room",good,510);
    auto current=s.load("synthetic");s.commit("synthetic",current.revision,current.state,"explicit state revision");
    rejects([&]{s.authorizeBattleReport(stale.id,"stale-room",good,520);},"campaign revision change invalidates report authorization");
}
struct Raw {
    sqlite3* db=nullptr;
    explicit Raw(const fs::path& path){const auto bytes=path.u8string();if(sqlite3_open(reinterpret_cast<const char*>(bytes.c_str()),&db)!=SQLITE_OK)throw std::runtime_error("open raw fixture");}
    ~Raw(){sqlite3_close(db);}
    void sql(const std::string& sql){char* error=nullptr;if(sqlite3_exec(db,sql.c_str(),nullptr,nullptr,&error)!=SQLITE_OK){std::string message=error?error:"raw SQL";sqlite3_free(error);throw std::runtime_error(message);}}
    int64_t count(const char* sql){sqlite3_stmt* q=nullptr;if(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)!=SQLITE_OK)throw std::runtime_error("prepare raw count");
        const int result=sqlite3_step(q);const auto n=sqlite3_column_int64(q,0);sqlite3_finalize(q);if(result!=SQLITE_ROW)throw std::runtime_error("raw count");return n;}
};
void rollbackAndRestart(const fs::path& path){
    bool fail=false;
    c::IssuedBattle original;
    {c::CampaignStore s(path,c::StoreOptions{[&]{if(fail)throw std::runtime_error("injected persistence failure");}});seed(s);
     fail=true;rejects([&]{issue(s);},"failed issuance is atomic");
     {Raw raw(path);check(raw.count("SELECT count(*) FROM issued_battles")==0,"failed issuance leaves no definition");
      check(raw.count("SELECT count(*) FROM battle_status_events")==0,"failed issuance leaves no lifecycle event");}
     fail=false;original=issue(s);fail=true;
     rejects([&]{s.startBattle(original.id,original.launchToken,"restart-room",context(),110);},"failed room binding is atomic");
     check(s.battle(original.id).status==c::BattleStatus::Issued && !s.battle(original.id).roomToken,"failed start leaves capability usable");
     fail=false;s.startBattle(original.id,original.launchToken,"restart-room",context(),110);
     fail=true;rejects([&]{s.cancelBattle(original.id,120);},"failed cancellation is atomic");
     check(s.battle(original.id).status==c::BattleStatus::Started,"failed cancellation leaves started status");}
    {c::CampaignStore reopened(path);const auto loaded=reopened.battle(original.id);
     check(loaded.context.mapDigest==original.context.mapDigest && loaded.launchToken==original.launchToken && loaded.roomToken=="restart-room" && loaded.status==c::BattleStatus::Started,"restart preserves issuance, binding and lifecycle");
     rejects([&]{reopened.authorizeBattleReport(original.id,"new-process-recycled-room",context(),130);},"restart cannot reuse numeric room identity");
     check(reopened.authorizeBattleReport(original.id,"restart-room",context(),130).id==original.id,"trusted persisted binding remains identifiable");
     reopened.cancelBattle(original.id,140);
     rejects([&]{reopened.authorizeBattleReport(original.id,"restart-room",context(),141);},"cancelled started match cannot report");
     auto another=issue(reopened,150,250);
     rejects([&]{reopened.startBattle(another.id,another.launchToken,"restart-room",context(),160);},"terminal battle room token never reassigned");}
}
void globalParticipation(const fs::path& path){
    bool fail=false;
    c::CampaignStore first(path,c::StoreOptions{[&]{if(fail)throw std::runtime_error("injected participation failure");}});
    seed(first);
    auto otherDefinition=c::loadDefinitionText("campaign 1 \"other\" \"Other\"\nterritory 1 \"One\"\nmap 1 \"maps/one.ota\"\n");
    first.create(otherDefinition,c::makeInitialState(otherDefinition),"other initial");
    first.setAllegiance("other","alice",c::Alliance::Honor,-1,1);
    first.setAllegiance("other","bob",c::Alliance::Terror,-1,1);
    c::CampaignStore second(path);
    check(!first.activeBattleForAccount("alice",100),"new account has active battle");
    rejects([&]{first.activeBattleForAccount("Alice",100);},"noncanonical active account query");
    rejects([&]{first.activeBattleForAccount("alice",-1);},"negative active query time");
    const auto offered=issue(first);
    check(second.activeBattleForAccount("alice",199)->id==offered.id&&
          second.activeBattleForAccount("bob",199)->id==offered.id,"second handle misses active offer for either participant");
    rejects([&]{issue(second);},"alternate database handle issued duplicate participants");
    rejects([&]{second.issueBattle("other",0,1,context(),150,250);},"cross-campaign duplicate participation");
    // The unexpired second participant alone must be sufficient to reject.
    first.setAllegiance("other","aaron",c::Alliance::Honor,-1,1);
    auto partial=context();partial.participants={"aaron","bob"};
    rejects([&]{second.issueBattle("other",0,1,partial,150,250);},"second participant conflict missed");
    check(!first.activeBattleForAccount("alice",200),"exclusive offer deadline still reserves account");
    const auto fresh=second.issueBattle("other",0,1,context(),200,300);
    rejects([&]{first.startBattle(offered.id,offered.launchToken,"rewound",context(),199);},"clock rewind started old offer over new reservation");
    check(first.battle(offered.id).status==c::BattleStatus::Issued&&!first.battle(offered.id).roomToken,"rejected conflicting start wrote room or lifecycle");
    second.startBattle(fresh.id,fresh.launchToken,"new-room",context(),201);
    check(first.activeBattleForAccount("alice",1000)->id==fresh.id,"Started reservation expired with launch deadline");
    rejects([&]{first.issueBattle("synthetic",0,1,context(),1000,1100);},"Started participant reused after deadline");
    second.cancelBattle(fresh.id,1001);
    check(!first.activeBattleForAccount("alice",1001)&&!first.activeBattleForAccount("bob",1001),"terminal cancellation did not release both participants");
    fail=true;rejects([&]{issue(first,1002,1102);},"failed issuance ignored rollback injection");fail=false;
    check(!second.activeBattleForAccount("alice",1002),"failed issuance retained global participant reservation");
    const auto retry=issue(first,1002,1102);
    fail=true;rejects([&]{first.cancelBattle(retry.id,1003);},"failed cancel injection ignored");fail=false;
    check(second.activeBattleForAccount("bob",1003)->id==retry.id,"failed cancellation released reservation");
    rejects([&]{second.issueBattle("other",0,1,context(),1003,1103);},"failed cancellation allowed duplicate issuance");
    first.expireBattles(1102);
    check(first.battle(retry.id).status==c::BattleStatus::Expired&&!second.activeBattleForAccount("bob",1102),"terminal expiry retained reservation");
    second.issueBattle("other",0,1,context(),1102,1202);
    {Raw raw(path);check(raw.count("SELECT count(*) FROM battle_participants")==8,"failed writes left participant projection fragments");}
    std::atomic<unsigned> ready=0;
    std::atomic<bool> go=false;
    std::optional<c::IssuedBattle> winners[2];
    std::exception_ptr failures[2];
    const auto compete=[&](unsigned n,c::CampaignStore& store,const char* campaign){
        ++ready;while(!go.load())std::this_thread::yield();
        try{winners[n]=store.issueBattle(campaign,0,1,context(),1300,1400);}
        catch(...){failures[n]=std::current_exception();}
    };
    std::thread one(compete,0,std::ref(first),"synthetic"),two(compete,1,std::ref(second),"other");
    while(ready.load()!=2)std::this_thread::yield();
    go=true;one.join();two.join();
    check(bool(winners[0])!=bool(winners[1]),"concurrent handles did not grant exactly one global reservation");
    const auto loser=winners[0]?1:0;
    bool duplicate=false;
    try{std::rethrow_exception(failures[loser]);}
    catch(const std::runtime_error& error){duplicate=std::string(error.what()).find("already has an active battle")!=std::string::npos;}
    check(duplicate,"concurrent loser failed for an unrelated reason");
    const auto winner=winners[0]?winners[0]->id:winners[1]->id;
    check(first.activeBattleForAccount("alice",1300)->id==winner&&second.activeBattleForAccount("bob",1300)->id==winner,"concurrent claim did not atomically reserve both participants");
}
void participationMigration(const fs::path& path){
    std::string id;
    {c::CampaignStore store(path);seed(store);id=issue(store).id;}
    {
        Raw raw(path);
        // Schema 6 permitted overlapping offers. Preserve an independently
        // constructed old offer to verify migration does not discard history.
        raw.sql("INSERT INTO issued_battles SELECT 'legacy-overlap',campaign_id,campaign_revision,territory,context,created_unix,expires_unix,'legacy-launch',0 FROM issued_battles");
        raw.sql("INSERT INTO battle_status_events VALUES('legacy-overlap',0,0,100)");
        raw.sql("INSERT INTO issued_battle_rules VALUES('legacy-overlap','historical-darien-v1')");
        raw.sql("INSERT INTO battle_participants SELECT 'legacy-overlap',campaign_id,account_id,created_unix FROM battle_participants");
        raw.sql("DROP TABLE territory_battle_history");
        raw.sql("DROP INDEX battle_participants_global_account");raw.sql("PRAGMA user_version=6");
    }
    rejects([&]{c::CampaignStore store(path,c::StoreOptions{[]{throw std::runtime_error("schema8 interrupted");}});},"6 to 8 migration ignored failure hook");
    {Raw raw(path);check(raw.count("PRAGMA user_version")==6&&raw.count("SELECT count(*) FROM sqlite_master WHERE name='battle_participants_global_account'")==0,"failed schema8 migration left partial index or version");}
    c::CampaignStore migrated(path);
    check(migrated.battle(id).status==c::BattleStatus::Issued&&migrated.battle("legacy-overlap").status==c::BattleStatus::Issued,"migration changed old overlapping lifecycle");
    check(migrated.ownBattleIds("synthetic","alice").ids.size()==2,"migration lost old duplicate history");
    rejects([&]{issue(migrated);},"migrated active participants allowed duplicate offer");
    rejects([&]{migrated.startBattle(id,migrated.battle(id).launchToken,"legacy-room",context(),110);},"migrated overlapping offer started without resolving conflict");
    migrated.cancelBattle("legacy-overlap",110);
    migrated.startBattle(id,migrated.battle(id).launchToken,"legacy-room",context(),111);
    check(migrated.activeBattleForAccount("alice",500)->id==id,"resolved migrated offer could not retain Started participation");
    {Raw raw(path);check(raw.count("PRAGMA user_version")==8&&raw.count("SELECT count(*) FROM battle_participants")==4,"schema8 index migration changed history");}
}
// Build exact older schemas by retaining their original, unchanged SQL objects.
// Data is written before removing only the later milestone's unused objects.
void olderDatabase(const fs::path& path,int version){
    {c::CampaignStore s(path);seed(s);auto current=s.load("synthetic");
     current.state.territories.at(1).recon.fatigueVictoryPoints=1.25;
     s.commit("synthetic",0,current.state,"old history",c::BattleResult{"old-result","old payload"});
     s.setAllegiance("synthetic","alice",c::Alliance::Terror,0,2);}
    std::set<std::string> retained={"campaigns","campaign_events","battle_results","campaign_events_no_update","campaign_events_no_delete",
        "battle_results_no_update","battle_results_no_delete","campaign_definition_no_update"};
    if(version==2)for(const char* name:{"campaign_participants","allegiance_events","allegiance_events_no_update","allegiance_events_no_delete","campaign_participant_identity_no_update"})retained.insert(name);
    Raw raw(path);sqlite3_stmt* q=nullptr;
    check(sqlite3_prepare_v2(raw.db,"SELECT type,name FROM sqlite_master WHERE name NOT GLOB 'sqlite_*' ORDER BY type DESC",-1,&q,nullptr)==SQLITE_OK,"read migration fixture schema");
    std::vector<std::pair<std::string,std::string>> remove;
    while(sqlite3_step(q)==SQLITE_ROW){std::string type=reinterpret_cast<const char*>(sqlite3_column_text(q,0));std::string name=reinterpret_cast<const char*>(sqlite3_column_text(q,1));
        if(!retained.count(name))remove.emplace_back(type,name);}
    sqlite3_finalize(q);
    for(const auto& [type,name]:remove)raw.sql("DROP "+type+" IF EXISTS \""+name+"\"");
    raw.sql("PRAGMA user_version="+std::to_string(version));
}
void migration(const fs::path& root){
    for(int version:{1,2}){
        const auto path=root/("migration"+std::to_string(version)+".sqlite");olderDatabase(path,version);
        rejects([&]{c::CampaignStore failure(path,c::StoreOptions{[]{throw std::runtime_error("interrupted migration");}});},"failed migration rolls back");
        {Raw raw(path);check(raw.count("PRAGMA user_version")==version,"failed migration preserves older version");
         check(raw.count("SELECT count(*) FROM sqlite_master WHERE name='issued_battles'")==0,"failed migration leaves no new battle table");}
        c::CampaignStore migrated(path);check(migrated.load("synthetic").revision==1 && migrated.history("synthetic").size()==2,"older campaign revisions/history retained");
        check(migrated.load("synthetic").state.territories.at(1).recon.fatigueVictoryPoints==1.25 && migrated.battleResult("synthetic","old-result")=="old payload","older state and battle results retained");
        if(version==2){auto enrolled=migrated.allegiance("synthetic","alice");
            check(enrolled && enrolled->alliance==c::Alliance::Terror && enrolled->revision==1 && enrolled->joinedUnix==1 && enrolled->changedUnix==2,"v2 participant and revision preserved");
            check(migrated.allegianceHistory("synthetic","alice").size()==2,"v2 allegiance audit preserved");}
        else check(!migrated.allegiance("synthetic","alice"),"v1 migration invents no allegiance");
        {Raw raw(path);check(raw.count("PRAGMA user_version")==8,"current schema version8 installed");}
    }
}
} // namespace
int main(){
    const auto root=fs::temp_directory_path()/("tak-battles-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup{fs::path p;~Cleanup(){std::error_code e;fs::remove_all(p,e);}}cleanup{root};
    try{fs::create_directories(root);issueValidation(":memory:");issueValidation(root/"issue.sqlite");binding(root/"binding.sqlite");
        terminalAndStale(root/"terminal.sqlite");rollbackAndRestart(root/"restart.sqlite");globalParticipation(root/"global.sqlite");participationMigration(root/"participation-migration.sqlite");migration(root);
        std::cout<<"PASS: "<<checks<<" Crusades battle issuance checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
