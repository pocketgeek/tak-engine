// M4 storage/migration tests: independently authored data, never account secrets.
#include "server/crusades/store.h"
#include <sqlite3.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace c=tak::srv::crusades;
namespace fs=std::filesystem;
namespace {
int checks=0;
void check(bool value,const std::string& why) { ++checks;if(!value)throw std::runtime_error(why); }
template<class F> void rejects(F f,const std::string& why) {
    ++checks;try{f();}catch(const std::runtime_error&){return;}
    throw std::runtime_error("accepted invalid allegiance: "+why);
}
struct Raw {
    sqlite3* db=nullptr;
    explicit Raw(const fs::path& path) {
        const auto text=path.u8string();
        if(sqlite3_open(reinterpret_cast<const char*>(text.c_str()),&db)!=SQLITE_OK)
            throw std::runtime_error("open test database");
    }
    ~Raw(){sqlite3_close(db);}
    void sql(const std::string& text) {
        char* error=nullptr;
        if(sqlite3_exec(db,text.c_str(),nullptr,nullptr,&error)!=SQLITE_OK) {
            std::string message=error?error:"SQL error";sqlite3_free(error);throw std::runtime_error(message);
        }
    }
    int64_t scalar(const char* sql) {
        sqlite3_stmt* stmt=nullptr;
        if(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)!=SQLITE_OK)throw std::runtime_error("prepare test scalar");
        const int status=sqlite3_step(stmt);const int64_t result=sqlite3_column_int64(stmt,0);sqlite3_finalize(stmt);
        if(status!=SQLITE_ROW)throw std::runtime_error("read test scalar");
        return result;
    }
};
const std::string defText="campaign 1 \"legacy\" \"Legacy campaign\"\nterritory 1 \"One\"\nnative 1 \"Aramon\"\nneighbors 1\n";
c::CampaignDefinition definition(){return c::loadDefinitionText(defText);}
void seed(c::CampaignStore& store) {auto d=definition();store.create(d,c::makeInitialState(d),"start");}
void equal(const c::Allegiance& a,const c::Allegiance& b) {
    check(a.accountId==b.accountId && a.alliance==b.alliance && a.joinedUnix==b.joinedUnix &&
          a.changedUnix==b.changedUnix && a.revision==b.revision,"complete allegiance record preserved");
}
void behavior(const fs::path& path) {
    c::CampaignStore store(path);seed(store);
    check(!store.allegiance("legacy","alice"),"known campaign without enrollment is absent");
    check(store.allegianceHistory("legacy","alice").empty(),"unenrolled history empty");
    rejects([&]{store.setAllegiance("missing","alice",c::Alliance::Honor,-1,10);},"unknown campaign");
    rejects([&]{(void)store.allegiance("missing","alice");},"unknown campaign lookup");
    for(const std::string name:{"","ab","Alice","_alice","a b","al/ice","abcdefghijklmnopqrstu","caf\xc3\xa9"}) {
        rejects([&]{store.setAllegiance("legacy",name,c::Alliance::Honor,-1,10);},"noncanonical account "+name);
        rejects([&]{(void)store.allegiance("legacy",name);},"noncanonical lookup "+name);
    }
    rejects([&]{store.setAllegiance("legacy",std::string("ali\0ce",6),c::Alliance::Honor,-1,10);},"embedded NUL account");
    for(int side:{-1,0,3,99})rejects([&]{store.setAllegiance("legacy","alice",static_cast<c::Alliance>(side),-1,10);},"invalid alliance");
    rejects([&]{store.setAllegiance("legacy","alice",c::Alliance::Honor,-2,10);},"invalid initial revision");
    rejects([&]{store.setAllegiance("legacy","alice",c::Alliance::Honor,0,10);},"missing prior enrollment");
    rejects([&]{store.setAllegiance("legacy","alice",c::Alliance::Honor,-1,-1);},"negative timestamp");
    const auto joined=store.setAllegiance("legacy","alice",c::Alliance::Honor,-1,10);
    check(joined.accountId=="alice" && joined.alliance==c::Alliance::Honor && joined.revision==0 &&
          joined.joinedUnix==10 && joined.changedUnix==10,"initial authenticated identity enrollment");
    rejects([&]{store.setAllegiance("legacy","alice",c::Alliance::Terror,-1,11);},"duplicate join");
    rejects([&]{store.setAllegiance("legacy","alice",c::Alliance::Honor,0,11);},"same-side no-op");
    rejects([&]{store.setAllegiance("legacy","alice",c::Alliance::Terror,0,9);},"time goes backwards");
    const auto switched=store.setAllegiance("legacy","alice",c::Alliance::Terror,0,10);
    check(switched.alliance==c::Alliance::Terror && switched.revision==1 && switched.joinedUnix==10 && switched.changedUnix==10,
          "modern immediate switch accepts same server timestamp without inventing cooldown");
    rejects([&]{store.setAllegiance("legacy","alice",c::Alliance::Honor,0,12);},"stale revision");
    auto history=store.allegianceHistory("legacy","alice");
    check(history.size()==2,"only successful allegiance changes enter audit");equal(history[0],joined);equal(history[1],switched);
    check(store.load("legacy").revision==0 && store.history("legacy").size()==1,"allegiance uses independent revision without tactical state changes");
    const auto other=store.setAllegiance("legacy","bob-2.test_",c::Alliance::Honor,-1,12);
    check(other.revision==0,"different accounts independently enroll");
    auto second=c::loadDefinitionText("campaign 1 \"second\" \"Second\"\nterritory 1 \"One\"\n");
    store.create(second,c::makeInitialState(second),"start");
    store.setAllegiance("second","alice",c::Alliance::Honor,-1,20);
    check(store.allegiance("legacy","alice")->alliance==c::Alliance::Terror && store.allegiance("second","alice")->alliance==c::Alliance::Honor,
          "no invented global allegiance across campaigns");
}
void reopenAndRollback(const fs::path& path) {
    {c::CampaignStore store(path);seed(store);store.setAllegiance("legacy","alice",c::Alliance::Honor,-1,100);}
    bool inject=true;
    c::CampaignStore first(path,c::StoreOptions{[&]{if(inject)throw std::runtime_error("injected rollback");}});
    c::CampaignStore second(path);
    const auto before=*second.allegiance("legacy","alice");
    rejects([&]{first.setAllegiance("legacy","alice",c::Alliance::Terror,0,110);},"all writes rollback on exception");
    equal(*second.allegiance("legacy","alice"),before);
    check(second.allegianceHistory("legacy","alice").size()==1,"rollback leaves audit unchanged");
    rejects([&]{first.setAllegiance("legacy","newuser",c::Alliance::Honor,-1,110);},"initial enrollment rollback");
    check(!second.allegiance("legacy","newuser") && second.allegianceHistory("legacy","newuser").empty(),"rolled-back participant absent");
    inject=false;
    const auto changed=first.setAllegiance("legacy","alice",c::Alliance::Terror,0,110);
    rejects([&]{second.setAllegiance("legacy","alice",c::Alliance::Terror,before.revision,120);},"separate stale connection");
    equal(*second.allegiance("legacy","alice"),changed);
    c::CampaignStore reopened(path);equal(*reopened.allegiance("legacy","alice"),changed);
    auto events=reopened.allegianceHistory("legacy","alice");check(events.size()==2,"restart preserves entire allegiance audit");equal(events.front(),before);equal(events.back(),changed);
}
// Frozen schema v1 fixture: must remain independent of the current migration code.
const char* v1Schema[]={
 "CREATE TABLE campaigns(id TEXT PRIMARY KEY,definition TEXT NOT NULL,revision INTEGER NOT NULL CHECK(revision>=0))",
 "CREATE TABLE campaign_events(campaign_id TEXT NOT NULL REFERENCES campaigns(id),revision INTEGER NOT NULL CHECK(revision>=0),reason TEXT NOT NULL,battle_id TEXT,snapshot BLOB NOT NULL,PRIMARY KEY(campaign_id,revision))",
 "CREATE TABLE battle_results(campaign_id TEXT NOT NULL,battle_id TEXT NOT NULL,payload BLOB NOT NULL,revision INTEGER NOT NULL,PRIMARY KEY(campaign_id,battle_id),FOREIGN KEY(campaign_id,revision) REFERENCES campaign_events(campaign_id,revision))",
 "CREATE TRIGGER campaign_events_no_update BEFORE UPDATE ON campaign_events BEGIN SELECT RAISE(ABORT,'immutable campaign event'); END",
 "CREATE TRIGGER campaign_events_no_delete BEFORE DELETE ON campaign_events BEGIN SELECT RAISE(ABORT,'immutable campaign event'); END",
 "CREATE TRIGGER battle_results_no_update BEFORE UPDATE ON battle_results BEGIN SELECT RAISE(ABORT,'immutable battle result'); END",
 "CREATE TRIGGER battle_results_no_delete BEFORE DELETE ON battle_results BEGIN SELECT RAISE(ABORT,'immutable battle result'); END",
 "CREATE TRIGGER campaign_definition_no_update BEFORE UPDATE OF id,definition ON campaigns BEGIN SELECT RAISE(ABORT,'immutable campaign definition'); END"
};
void put(std::string& out,uint64_t value,unsigned size){while(size--){out+=char(value&255);value>>=8;}}
std::string oldSnapshot(bool updated) {
    std::string s="TAKCS1";put(s,6,4);s+="legacy";put(s,1,4);put(s,1,4);
    put(s,updated?3:2,1);put(s,0,1); // Territory 1 owner; no runtime map.
    put(s,1,1);const double metric=updated?1.25:-0.0;uint64_t bits;std::memcpy(&bits,&metric,8);put(s,bits,8);
    for(int i=0;i<6;++i)put(s,0,1); // Other recon metrics absent.
    return s;
}
const std::string resultPayload("result\0legacy",13);
void legacyDatabase(const fs::path& path) {
    Raw db(path);for(const auto* statement:v1Schema)db.sql(statement);
    db.sql("PRAGMA application_id=1413565251;PRAGMA user_version=1");
    sqlite3_stmt* statement=nullptr;
    check(sqlite3_prepare_v2(db.db,"INSERT INTO campaigns VALUES('legacy',?,1)",-1,&statement,nullptr)==SQLITE_OK,"prepare legacy definition");
    sqlite3_bind_text(statement,1,defText.data(),int(defText.size()),SQLITE_TRANSIENT);
    const int inserted=sqlite3_step(statement);sqlite3_finalize(statement);check(inserted==SQLITE_DONE,"insert legacy definition");
    for(int revision=0;revision<2;++revision) {
        check(sqlite3_prepare_v2(db.db,"INSERT INTO campaign_events VALUES('legacy',?,'legacy event',?,?)",-1,&statement,nullptr)==SQLITE_OK,"prepare legacy event");
        sqlite3_bind_int(statement,1,revision);
        if(revision)sqlite3_bind_text(statement,2,"old-battle",-1,SQLITE_STATIC);
        const auto bytes=oldSnapshot(revision!=0);sqlite3_bind_blob(statement,3,bytes.data(),int(bytes.size()),SQLITE_TRANSIENT);
        const int status=sqlite3_step(statement);sqlite3_finalize(statement);check(status==SQLITE_DONE,"insert legacy snapshot");
    }
    check(sqlite3_prepare_v2(db.db,"INSERT INTO battle_results VALUES('legacy','old-battle',?,1)",-1,&statement,nullptr)==SQLITE_OK,"prepare legacy result");
    sqlite3_bind_blob(statement,1,resultPayload.data(),int(resultPayload.size()),SQLITE_TRANSIENT);
    const int status=sqlite3_step(statement);sqlite3_finalize(statement);check(status==SQLITE_DONE,"insert legacy result");
}
void legacyPreserved(c::CampaignStore& store) {
    const auto loaded=store.load("legacy");check(loaded.revision==1,"migration preserves tactical revision");
    check(loaded.definition.displayName()=="Legacy campaign" && loaded.definition.find(1)->nativeFaction=="Aramon" &&
          loaded.definition.find(1)->neighbors->empty(),"migration preserves immutable definition");
    const auto& state=loaded.state.territories.at(1);
    check(state.owner==c::TerritoryOwner::Terror && state.recon.fatigueVictoryPoints==1.25,"migration preserves latest state");
    const auto history=store.history("legacy");check(history.size()==2 && history[1].battleId=="old-battle","migration preserves history");
    check(history[0].state.territories.at(1).owner==c::TerritoryOwner::Honor &&
          std::signbit(*history[0].state.territories.at(1).recon.fatigueVictoryPoints),"migration preserves earlier ownership and negative zero");
    check(store.battleResult("legacy","old-battle")==resultPayload,"migration preserves binary battle payload");
    rejects([&]{store.commit("legacy",1,loaded.state,"duplicate",c::BattleResult{"old-battle","changed"});},"migration preserves duplicate protection");
}
void migration(const fs::path& path,const fs::path& rollbackPath) {
    legacyDatabase(path);
    {c::CampaignStore store(path);legacyPreserved(store);
     check(!store.allegiance("legacy","alice"),"migration does not invent participants");
     store.setAllegiance("legacy","alice",c::Alliance::Honor,-1,15);}
    {c::CampaignStore reopened(path);legacyPreserved(reopened);check(reopened.allegiance("legacy","alice")->alliance==c::Alliance::Honor,"new allegiance survives migrated restart");}
    Raw db(path);check(db.scalar("PRAGMA user_version")==4,"schema migration publishes current version4");
    check(db.scalar("SELECT count(*) FROM pragma_table_info('campaign_participants')")==3 &&
          db.scalar("SELECT count(*) FROM pragma_table_info('campaign_participants') WHERE name NOT IN ('campaign_id','account_id','revision')")==0,
          "participant schema contains only campaign/account references and revision");
    check(db.scalar("SELECT count(*) FROM pragma_table_info('allegiance_events')")==6 &&
          db.scalar("SELECT count(*) FROM pragma_table_info('allegiance_events') WHERE name NOT IN ('campaign_id','account_id','revision','alliance','joined_unix','changed_unix')")==0,
          "allegiance audit stores no password, salt, verifier or authentication transcript");
    rejects([&]{db.sql("UPDATE allegiance_events SET alliance=2");},"audit cannot be rewritten");
    rejects([&]{db.sql("DELETE FROM allegiance_events");},"audit cannot be deleted");
    rejects([&]{db.sql("UPDATE campaign_participants SET account_id='bob'");},"participant identity cannot be reassigned");
    legacyDatabase(rollbackPath);
    rejects([&]{c::CampaignStore failed(rollbackPath,c::StoreOptions{[]{throw std::runtime_error("migration interrupted");}});},"migration rollback at precommit boundary");
    {Raw old(rollbackPath);check(old.scalar("PRAGMA user_version")==1,"failed migration leaves version1");
     check(old.scalar("SELECT count(*) FROM sqlite_master WHERE name='campaign_participants'")==0,"failed migration leaves no partial allegiance table");
     check(old.scalar("SELECT count(*) FROM campaign_events")==2,"failed migration preserves previous history");}
    c::CampaignStore retried(rollbackPath);legacyPreserved(retried);
}
void incompatibleMigration(const fs::path& root) {
    const auto future=root/"future.sqlite";legacyDatabase(future);
    {Raw db(future);db.sql("PRAGMA user_version=77");}
    rejects([&]{c::CampaignStore store(future);},"unknown schema version");
    {Raw db(future);check(db.scalar("PRAGMA user_version")==77,"future schema remains untouched");}
    const auto damaged=root/"damaged-v1.sqlite";legacyDatabase(damaged);
    {Raw db(damaged);db.sql("DROP TRIGGER campaign_events_no_update");}
    rejects([&]{c::CampaignStore store(damaged);},"incomplete version1 schema cannot migrate");
    {Raw db(damaged);check(db.scalar("PRAGMA user_version")==1 &&
        db.scalar("SELECT count(*) FROM sqlite_master WHERE name='campaign_participants'")==0,
        "rejected version1 leaves no partial migration");}
}
} // namespace
int main() {
    const auto root=fs::temp_directory_path()/("tak-allegiance-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {fs::path p;~Cleanup(){std::error_code e;fs::remove_all(p,e);}} cleanup{root};
    try {
        fs::create_directories(root);
        behavior(":memory:");behavior(root/"behavior.sqlite");
        reopenAndRollback(root/"rollback.sqlite");migration(root/"legacy.sqlite",root/"migration-rollback.sqlite");
        incompatibleMigration(root);
        std::cout<<"PASS: "<<checks<<" Crusades allegiance storage/migration checks\n";return 0;
    } catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
