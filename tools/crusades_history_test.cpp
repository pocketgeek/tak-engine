// Synthetic verified territory history; no retail assets or campaign arithmetic.
#include "server/crusades/store.h"
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

namespace c=tak::srv::crusades;
namespace fs=std::filesystem;
namespace {
int checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F run,const char* message){++checks;try{run();}catch(const std::runtime_error&){return;}throw std::runtime_error(message);}
c::BattleContext context(){return {"one.ota",std::string(64,'a'),std::string(64,'b'),{"alice","bob"},true};}
void seed(c::CampaignStore& store,const std::string& campaign="test"){
    auto d=c::loadDefinitionText("campaign 1 \""+campaign+"\" \"Synthetic\"\nterritory 1 \"One\"\nmap 1 \"one.ota\"\nterritory 2 \"Two\"\nmap 2 \"one.ota\"\nterritory 3 \"Empty\"\n");
    store.create(d,c::makeInitialState(d),"initial");
    store.setAllegiance(campaign,"alice",c::Alliance::Honor,-1,1);
    store.setAllegiance(campaign,"bob",c::Alliance::Terror,-1,1);
}
c::IssuedBattle started(c::CampaignStore& store,const std::string& room,const std::string& campaign="test",uint32_t territory=1){
    auto b=store.issueBattle(campaign,store.load(campaign).revision,territory,context(),100,200);
    store.startBattle(b.id,b.launchToken,room,context(),101);return b;
}
c::VerifiedMatchResult result(const std::string& replay,c::ResultOutcome outcome=c::ResultOutcome::Victory){
    c::VerifiedMatchResult r;r.outcome=outcome;r.engineBuild="synthetic-build";
    r.participantResults={{"alice",2,1,99,4,3,"Aramon",0,false},{"bob",1,2,-7,3,0,"Taros",1,true}};
    r.finalTick=1234;r.finalStateHash=UINT64_MAX;r.gameplayFingerprint=UINT64_C(0x8000000000000001);
    if(outcome==c::ResultOutcome::Victory || outcome==c::ResultOutcome::Resignation){
        r.winners={"alice"};r.replayId=replay;r.replayDigest=std::string(64,'c');
    }
    return r;
}
struct Raw {
    sqlite3* db=nullptr;
    explicit Raw(const fs::path& path){const auto bytes=path.u8string();if(sqlite3_open(reinterpret_cast<const char*>(bytes.c_str()),&db)!=SQLITE_OK)throw std::runtime_error("raw open");}
    ~Raw(){sqlite3_close(db);}
    void sql(const std::string& sql){char* error=nullptr;if(sqlite3_exec(db,sql.c_str(),nullptr,nullptr,&error)!=SQLITE_OK){std::string message=error?error:"raw SQL";sqlite3_free(error);throw std::runtime_error(message);}}
    int64_t count(const char* sql){sqlite3_stmt* query=nullptr;if(sqlite3_prepare_v2(db,sql,-1,&query,nullptr)!=SQLITE_OK)throw std::runtime_error("raw prepare");
        const auto status=sqlite3_step(query);const auto value=sqlite3_column_int64(query,0);sqlite3_finalize(query);if(status!=SQLITE_ROW)throw std::runtime_error("raw row");return value;}
};
void paging(){
    c::CampaignStore store(":memory:");seed(store);seed(store,"other");
    struct Expected{std::string id;int64_t when;};std::vector<Expected> expected;
    for(int i=0;i<71;++i){
        const auto battle=started(store,"room-"+std::to_string(i));
        const auto when=110+i/4;
        store.recordVerifiedResult(battle.id,"room-"+std::to_string(i),context(),result("replay-"+std::to_string(i)),when);
        expected.push_back({battle.id,when});
    }
    const auto elsewhere=started(store,"territory-two","test",2);
    store.recordVerifiedResult(elsewhere.id,"territory-two",context(),result("two"),190);
    const auto foreign=started(store,"other-campaign","other",1);
    store.recordVerifiedResult(foreign.id,"other-campaign",context(),result("foreign"),190);
    auto unplayed=store.issueBattle("test",0,1,context(),200,300);store.cancelBattle(unplayed.id,200);
    auto expired=store.issueBattle("test",0,1,context(),300,400);store.expireBattles(400);
    const auto active=started(store,"active");
    std::sort(expected.begin(),expected.end(),[](const auto& a,const auto& b){return std::tie(a.when,a.id)>std::tie(b.when,b.id);});
    std::optional<c::HistoryCursor> cursor;
    size_t position=0,pages=0;
    do {
        const auto page=store.territoryHistory("test",1,cursor,17);++pages;
        check(!page.entries.empty()&&page.entries.size()<=17,"history page size incorrect");
        for(const auto& entry:page.entries){
            check(position<expected.size()&&entry.battle.id==expected[position].id&&entry.recordedUnix==expected[position].when,"keyset order skipped, repeated or mixed scope");
            check(entry.battle.campaignId=="test"&&entry.battle.territory==1&&entry.battle.status==c::BattleStatus::Completed,"history battle scope/status changed");
            check(entry.result.winners==std::vector<std::string>{"alice"}&&entry.result.participantResults[1].score==-7&&entry.result.finalStateHash==UINT64_MAX,"history result payload lost immutable metadata");
            ++position;
        }
        check(page.truncated==(position<expected.size()),"incorrect truncated flag");
        const auto& last=page.entries.back();cursor=c::HistoryCursor{last.recordedUnix,last.battle.id};
        if(!page.truncated)break;
    }while(true);
    check(position==71&&pages==5,"history pagination lost results");
    check(store.territoryHistory("test",1,cursor).entries.empty(),"last cursor repeated final row");
    check(store.territoryHistory("test",2).entries.size()==1&&store.territoryHistory("other",1).entries.size()==1&&store.territoryHistory("test",3).entries.empty(),"territory/campaign filtering incorrect");
    const auto defaultPage=store.territoryHistory("test",1);check(defaultPage.entries.size()==32&&defaultPage.truncated,"default history page is unbounded");
    rejects([&]{store.territoryHistory("test",1,{},0);},"zero page limit accepted");
    rejects([&]{store.territoryHistory("test",1,{},33);},"unbounded page limit accepted");
    rejects([&]{store.territoryHistory("missing",1);},"unknown campaign accepted");
    rejects([&]{store.territoryHistory("test",0);},"zero territory accepted");
    rejects([&]{store.territoryHistory("test",99);},"unknown territory accepted");
    for(const auto& bad:std::vector<c::HistoryCursor>{{-1,expected[0].id},{expected[0].when+1,expected[0].id},{190,elsewhere.id},{190,foreign.id},{200,unplayed.id},{400,expired.id},{101,active.id},{0,"missing"},{0,""},{0,std::string(257,'x')},{0,std::string("bad\0",4)}})
        rejects([&]{store.territoryHistory("test",1,bad);},"invalid or other-scope cursor accepted");
    const auto stable=store.territoryHistory("test",1,{},4);
    const auto& last=stable.entries.back();const c::HistoryCursor continuation{last.recordedUnix,last.battle.id};
    store.cancelBattle(active.id,400);
    const auto newest=started(store,"newest");store.recordVerifiedResult(newest.id,"newest",context(),result("newest"),500);
    const auto resumed=store.territoryHistory("test",1,continuation,32);
    check(resumed.entries.front().battle.id==expected[4].id,"new front insertion disturbed existing keyset continuation");
    check(store.territoryHistory("test",1,{},1).entries[0].battle.id==newest.id,"latest history not visible after append");
    check(store.load("test").revision==0&&store.history("test").size()==1,"history read or historical result invented campaign mutation");
}
void allOutcomes(){
    c::CampaignStore store(":memory:");seed(store);
    for(int outcome=0;outcome<=9;++outcome){
        const auto room="outcome-"+std::to_string(outcome);const auto b=started(store,room);
        store.recordVerifiedResult(b.id,room,context(),result(room,static_cast<c::ResultOutcome>(outcome)),110+outcome);
    }
    const auto page=store.territoryHistory("test",1);
    check(page.entries.size()==10&&!page.truncated,"audited terminal outcomes omitted");
    for(size_t i=0;i<page.entries.size();++i){
        const auto& entry=page.entries[i];check(static_cast<int>(entry.result.outcome)==9-int(i),"outcome history ordering incorrect");
        check(entry.battle.status==(i>=8?c::BattleStatus::Completed:c::BattleStatus::Cancelled),"audited abort status changed");
    }
}
void durableAndArtifacts(const fs::path& root){
    const auto db=root/"durable.sqlite",artifact=root/"local-replay.takrep";
    std::string id;bool fail=false;
    {c::CampaignStore store(db,c::StoreOptions{[&]{if(fail)throw std::runtime_error("injected history persistence failure");}});seed(store);
        auto b=started(store,"durable-room");id=b.id;auto r=result(artifact.filename().string());
        fail=true;rejects([&]{store.recordVerifiedResult(id,"durable-room",context(),r,110);},"history result rollback ignored hook");fail=false;
        check(store.territoryHistory("test",1).entries.empty()&&!store.verifiedResult(id),"failed result left visible history");
        {Raw raw(db);check(raw.count("SELECT count(*) FROM territory_battle_history")==0,"rollback left history projection fragment");}
        store.recordVerifiedResult(id,"durable-room",context(),r,110);
        {std::ofstream file(artifact);file<<"synthetic artifact";}
        const auto before=store.territoryHistory("test",1).entries[0];
        fs::remove(artifact);
        const auto missing=store.territoryHistory("test",1).entries[0];
        check(missing.battle.id==before.battle.id&&missing.result.replayId==before.result.replayId&&missing.result.replayDigest==before.result.replayDigest,"deleted artifact removed/changed history");
        {std::ofstream file(artifact);file<<"corrupt replacement";}
        check(store.territoryHistory("test",1).entries[0].result.replayDigest==r.replayDigest,"corrupt artifact rewrote historical identity");
        r.finalStateHash=0;rejects([&]{store.recordVerifiedResult(id,"durable-room",context(),r,111);},"duplicate result replaced history");
        check(store.territoryHistory("test",1).entries[0].result.finalStateHash==UINT64_MAX,"duplicate changed history payload");
    }
    {c::CampaignStore store(db);const auto page=store.territoryHistory("test",1);
        check(page.entries.size()==1&&page.entries[0].battle.id==id&&page.entries[0].recordedUnix==110,"reopen lost history");
        check(page.entries[0].result.replayId==artifact.filename().string()&&page.entries[0].result.finalStateHash==UINT64_MAX,"reopen lost independent result metadata");}
    {Raw raw(db);rejects([&]{raw.sql("UPDATE territory_battle_history SET recorded_unix=111");},"history projection mutable");
        rejects([&]{raw.sql("DELETE FROM territory_battle_history");},"history projection deletable");
        sqlite3_stmt* query=nullptr;check(sqlite3_prepare_v2(raw.db,"EXPLAIN QUERY PLAN SELECT battle_id,recorded_unix FROM territory_battle_history WHERE campaign_id='test' AND territory=1 AND (recorded_unix,battle_id)<(110,'id') ORDER BY recorded_unix DESC,battle_id DESC LIMIT 33",-1,&query,nullptr)==SQLITE_OK,"history query plan prepare");
        check(sqlite3_step(query)==SQLITE_ROW,"history query plan row");const std::string plan=reinterpret_cast<const char*>(sqlite3_column_text(query,3));sqlite3_finalize(query);
        check(plan.find("COVERING INDEX territory_battle_history_page")!=std::string::npos,"history keyset query lacks covering index");
    }
}
void downgrade(const fs::path& path,int version){
    std::set<std::string> keep={"campaigns","campaign_events","battle_results","campaign_events_no_update","campaign_events_no_delete","battle_results_no_update","battle_results_no_delete","campaign_definition_no_update"};
    if(version>=2)for(const char* name:{"campaign_participants","allegiance_events","allegiance_events_no_update","allegiance_events_no_delete","campaign_participant_identity_no_update"})keep.insert(name);
    if(version>=3)for(const char* name:{"issued_battles","battle_status_events","battle_rooms","issued_battle_identity_no_update","battle_status_no_update","battle_status_no_delete","battle_rooms_no_update","battle_rooms_no_delete"})keep.insert(name);
    if(version>=4)for(const char* name:{"verified_match_results","verified_results_no_update","verified_results_no_delete"})keep.insert(name);
    if(version>=5)for(const char* name:{"campaign_rules","issued_battle_rules","rule_decisions","campaign_rules_no_update","campaign_rules_no_delete","issued_battle_rules_no_update","issued_battle_rules_no_delete","rule_decisions_no_update","rule_decisions_no_delete"})keep.insert(name);
    if(version>=6)for(const char* name:{"battle_participants","battle_participants_account","battle_participants_no_update","battle_participants_no_delete"})keep.insert(name);
    if(version>=7)keep.insert("battle_participants_global_account");
    Raw raw(path);sqlite3_stmt* query=nullptr;check(sqlite3_prepare_v2(raw.db,"SELECT type,name FROM sqlite_master WHERE name NOT GLOB 'sqlite_*' ORDER BY type DESC",-1,&query,nullptr)==SQLITE_OK,"migration fixture schema prepare");
    std::vector<std::pair<std::string,std::string>> remove;
    while(sqlite3_step(query)==SQLITE_ROW){std::string type=reinterpret_cast<const char*>(sqlite3_column_text(query,0)),name=reinterpret_cast<const char*>(sqlite3_column_text(query,1));if(!keep.count(name))remove.emplace_back(type,name);}
    sqlite3_finalize(query);
    for(const auto& [type,name]:remove)raw.sql("DROP "+type+" IF EXISTS \""+name+"\"");
    raw.sql("PRAGMA user_version="+std::to_string(version));
}
void damagedProjection(const fs::path& root){
    for(int kind=0;kind<4;++kind){
        const auto path=root/("damaged-"+std::to_string(kind)+".sqlite");std::string id;
        {c::CampaignStore store(path);seed(store);seed(store,"other");const auto b=started(store,"damaged-room");id=b.id;store.recordVerifiedResult(id,"damaged-room",context(),result("damaged-replay"),110);}
        {Raw raw(path);
            if(kind<3){raw.sql("DROP TRIGGER territory_battle_history_no_update");
                if(kind==0)raw.sql("UPDATE territory_battle_history SET recorded_unix=111");
                if(kind==1)raw.sql("UPDATE territory_battle_history SET territory=2");
                if(kind==2)raw.sql("UPDATE territory_battle_history SET campaign_id='other'");
                raw.sql("CREATE TRIGGER territory_battle_history_no_update BEFORE UPDATE ON territory_battle_history BEGIN SELECT RAISE(ABORT,'immutable territory battle history'); END");
            }else{raw.sql("DROP TRIGGER verified_results_no_update");raw.sql("UPDATE verified_match_results SET payload=x'00'");
                raw.sql("CREATE TRIGGER verified_results_no_update BEFORE UPDATE ON verified_match_results BEGIN SELECT RAISE(ABORT,'immutable verified result'); END");}
        }
        c::CampaignStore store(path);
        const auto campaign=kind==2?"other":"test";const auto territory=kind==1?2u:1u;const auto time=kind==0?111:110;
        rejects([&]{store.territoryHistory(campaign,territory);},"damaged projection/result fabricated historical row");
        rejects([&]{store.territoryHistory(campaign,territory,c::HistoryCursor{time,id});},"damaged cursor bypassed immutable result validation");
        check(store.load("test").revision==0&&store.load("other").revision==0,"damaged history read changed campaign state");
    }
}
void migrations(const fs::path& root){
    for(int version=1;version<=7;++version){
        const auto path=root/("migration-"+std::to_string(version)+".sqlite");std::string verified,unplayed;
        {c::CampaignStore store(path);seed(store);const auto b=started(store,"old-room");verified=b.id;store.recordVerifiedResult(b.id,"old-room",context(),result("old-replay"),110);
            const auto offered=store.issueBattle("test",0,1,context(),120,200);unplayed=offered.id;store.cancelBattle(offered.id,120);}
        downgrade(path,version);
        rejects([&]{c::CampaignStore interrupted(path,c::StoreOptions{[]{throw std::runtime_error("schema8 interrupted");}});},"old schema migration did not roll back");
        {Raw raw(path);check(raw.count("PRAGMA user_version")==version&&raw.count("SELECT count(*) FROM sqlite_master WHERE name='territory_battle_history'")==0,"failed migration left partial schema8");}
        {c::CampaignStore migrated(path);const auto page=migrated.territoryHistory("test",1);
            check(page.entries.size()==(version>=4?1u:0u),"migration invented or lost verified history");
            check(migrated.load("test").revision==0&&migrated.history("test").size()==1,"migration changed campaign state/history");
            if(version>=4)check(page.entries[0].battle.id==verified&&page.entries[0].result.replayId=="old-replay"&&page.entries[0].recordedUnix==110,"migration lost verified result metadata");
            if(version>=3)check(migrated.battle(unplayed).status==c::BattleStatus::Cancelled&&!migrated.verifiedResult(unplayed),"migration manufactured unplayed result");
        }
        {Raw raw(path);check(raw.count("PRAGMA user_version")==9,"migration did not publish schema9");}
    }
    const auto orphanPath=root/"orphaned-result.sqlite";
    {c::CampaignStore store(orphanPath);seed(store);const auto b=started(store,"orphan-room");store.recordVerifiedResult(b.id,"orphan-room",context(),result("orphan-replay"),110);}
    downgrade(orphanPath,7);
    {Raw raw(orphanPath);raw.sql("DELETE FROM issued_battles");}
    rejects([&]{c::CampaignStore damaged(orphanPath);},"orphaned verified result silently lost during migration");
    {Raw raw(orphanPath);check(raw.count("PRAGMA user_version")==7&&raw.count("SELECT count(*) FROM sqlite_master WHERE name='territory_battle_history'")==0,"orphan migration left partial schema8");
        check(raw.count("SELECT count(*) FROM verified_match_results")==1,"rejected orphan migration destroyed immutable metadata");}
}
}
int main(){const auto root=fs::temp_directory_path()/("tak-history-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup{fs::path path;~Cleanup(){std::error_code error;fs::remove_all(path,error);}}cleanup{root};
    try{fs::create_directories(root);paging();allOutcomes();durableAndArtifacts(root);migrations(root);damagedProjection(root);std::cout<<"PASS: "<<checks<<" Crusades history checks\n";return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
