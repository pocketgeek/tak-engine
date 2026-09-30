// Persistent-store tests use only independently authored synthetic campaigns.
#include "server/crusades/store.h"
#include <sqlite3.h>
#include <array>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace c = tak::srv::crusades;
namespace fs = std::filesystem;
namespace {
int checks = 0;
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) throw std::runtime_error(label);
}
template<class F> void rejects(F operation, const std::string& label) {
    ++checks;
    try {operation();} catch(const std::runtime_error&) {return;}
    throw std::runtime_error("accepted invalid store mutation: " + label);
}
c::CampaignDefinition definition(const std::string& id = "synthetic") {
    return c::loadDefinitionText("campaign 1 \"" + id + R"(" "Café synthetic campaign"
territory 1 "One"
territory 2 "Two"
territory 3 "Three"
native 1 "Aramon"
terrain 1 "forest"
map 1 "maps/one.ota"
neighbors 1 2
neighbors 2 1
neighbors 3
)");
}
bool equalNumber(const std::optional<double>& a,const std::optional<double>& b) {
    return a.has_value() == b.has_value() && (!a || (*a == *b && std::signbit(*a) == std::signbit(*b)));
}
bool equalSide(const c::SideReconMetrics& a,const c::SideReconMetrics& b) {
    return equalNumber(a.requiredVictoryPoints,b.requiredVictoryPoints) &&
        equalNumber(a.supportVictoryPoints,b.supportVictoryPoints) && equalNumber(a.battleVictoryPoints,b.battleVictoryPoints);
}
void sameState(const c::CampaignState& a,const c::CampaignState& b) {
    check(a.campaignId == b.campaignId && a.territories.size() == b.territories.size(),"campaign state identity survives persistence");
    for (const auto& [id,t] : a.territories) {
        const auto it = b.territories.find(id);
        check(it != b.territories.end(),"territory reference survives persistence");
        const auto& other = it->second;
        check(t.owner == other.owner && t.assignedMap == other.assignedMap &&
            equalNumber(t.recon.fatigueVictoryPoints,other.recon.fatigueVictoryPoints) &&
            equalSide(t.recon.honor,other.recon.honor) && equalSide(t.recon.terror,other.recon.terror),
            "complete territory state including exact finite metrics survives persistence");
    }
}
void sameDefinition(const c::CampaignDefinition& a,const c::CampaignDefinition& b) {
    check(a.id() == b.id() && a.displayName() == b.displayName() && a.territories().size() == b.territories().size(),
        "definition identity survives persistence");
    for (const auto& [id,t] : a.territories()) {
        const auto* other = b.find(id);
        check(other && t.id == other->id && t.displayName == other->displayName &&
            t.nativeFaction == other->nativeFaction && t.terrain == other->terrain &&
            t.mapIdentifier == other->mapIdentifier && t.neighbors == other->neighbors,
            "all immutable definition fields survive persistence");
    }
}
c::CampaignState populated(const c::CampaignDefinition& d) {
    auto state = c::makeInitialState(d);
    auto& t = state.territories.at(1);
    t.owner = c::TerritoryOwner::Honor;
    t.assignedMap = "maps/runtime.ota";
    t.recon.fatigueVictoryPoints = -0.0;
    t.recon.honor.requiredVictoryPoints = std::numeric_limits<double>::max();
    t.recon.honor.supportVictoryPoints = std::numeric_limits<double>::denorm_min();
    t.recon.honor.battleVictoryPoints = 1.25;
    t.recon.terror.requiredVictoryPoints = -19.5;
    t.recon.terror.supportVictoryPoints = 0.0;
    t.recon.terror.battleVictoryPoints = std::numeric_limits<double>::lowest();
    state.territories.at(2).owner = c::TerritoryOwner::Contested;
    // Territory three remains wholly unknown, distinct from explicit zeros.
    return state;
}

int childProcess(const fs::path& executable,const fs::path& database) {
#ifdef _WIN32
    // Paths come from the filesystem and cannot contain literal double quotes.
    std::wstring command = L"\"" + executable.wstring() + L"\" --crash-writer \"" + database.wstring() + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process))
        throw std::runtime_error("cannot start crash-test child");
    WaitForSingleObject(process.hProcess,INFINITE);
    DWORD result = 0; GetExitCodeProcess(process.hProcess,&result);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return static_cast<int>(result);
#else
    const auto pid = fork();
    if (pid < 0) throw std::runtime_error("cannot fork crash-test child");
    if (pid == 0) {
        const auto program = executable.string(), path = database.string();
        execl(program.c_str(),program.c_str(),"--crash-writer",path.c_str(),static_cast<char*>(nullptr));
        std::_Exit(74);
    }
    int status = 0;
    pid_t waited;
    do { waited = waitpid(pid,&status,0); } while(waited < 0 && errno == EINTR);
    if (waited != pid) throw std::runtime_error("cannot wait for crash-test child");
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}
} // namespace

namespace {
void checkCurrent(c::CampaignStore& store,const c::CampaignDefinition& def,const c::CampaignState& state,int64_t revision) {
    const auto current = store.load(def.id());
    check(current.revision == revision,"revision survives persistence");
    sameDefinition(def,current.definition);
    sameState(state,current.state);
}
void basicSemantics(const fs::path& path) {
    const auto def = definition();
    const auto initial = populated(def);
    c::CampaignStore store(path);
    rejects([&]{(void)store.load("absent");},"unknown campaign");
    store.create(def,initial,"CampaignStarted");
    checkCurrent(store,def,initial,0);
    rejects([&]{store.create(def,initial,"duplicate");},"duplicate campaign");
    check(!store.battleResult(def.id(),"absent"),"unknown battle has no result");
    auto next = initial;
    next.territories.at(1).owner = c::TerritoryOwner::Terror;
    next.territories.at(2).recon.fatigueVictoryPoints = 27;
    check(store.commit(def.id(),0,next,"BattleCompleted",c::BattleResult{"battle-1","opaque result"}) == 1,"successful battle increments revision once");
    check(store.battleResult(def.id(),"battle-1") == "opaque result","committed battle result retained");
    rejects([&]{store.commit(def.id(),1,next,"duplicate",c::BattleResult{"battle-1","opaque result"});},"same battle result");
    rejects([&]{store.commit(def.id(),1,next,"altered",c::BattleResult{"battle-1","changed payload"});},"same battle with different payload");
    rejects([&]{store.commit(def.id(),0,initial,"stale revision");},"stale revision cannot overwrite newer state");
    auto invalid = next; invalid.territories.erase(2);
    rejects([&]{store.commit(def.id(),1,invalid,"invalid state",c::BattleResult{"bad","payload"});},"invalid state atomic");
    check(!store.battleResult(def.id(),"bad"),"invalid mutation leaves no result");
    checkCurrent(store,def,next,1);
    // Snapshot history must explain every current value, not just ownership.
    auto history = store.history(def.id());
    check(history.size() == 2 && history[0].revision == 0 && history[1].revision == 1,
        "history contains only committed revisions");
    check(history[0].reason == "CampaignStarted" && !history[0].battleId && history[1].reason == "BattleCompleted" && history[1].battleId == "battle-1",
        "history identifies creation and battle cause");
    sameState(initial,history[0].state); sameState(next,history[1].state);
    auto final = next; final.territories.at(3).owner = c::TerritoryOwner::Honor;
    check(store.commit(def.id(),1,final,"TerritoryOwnerChanged") == 2,"non-battle revision commits");
    history = store.history(def.id());
    check(history.size() == 3 && !history[2].battleId,"non-battle history entry recorded");
    sameState(final,history.back().state);
    sameState(initial,history.front().state);
    // Battle IDs are unique per campaign, not across unrelated campaigns.
    const auto otherDef = definition("other"); const auto otherState = populated(otherDef);
    store.create(otherDef,otherState,"CampaignStarted");
    check(store.commit("other",0,otherState,"BattleCompleted",c::BattleResult{"battle-1","other result"}) == 1,
        "independent campaign may use same battle identifier");
    check(store.battleResult("synthetic","battle-1") == "opaque result" && store.battleResult("other","battle-1") == "other result",
        "result identities remain scoped to campaign");
}

void restartAndConflicts(const fs::path& path) {
    const auto def = definition(); const auto initial = populated(def);
    auto next = initial; next.territories.at(1).owner = c::TerritoryOwner::Terror;
    {
        c::CampaignStore store(path);
        store.create(def,initial,"start");
        store.commit(def.id(),0,next,"battle",c::BattleResult{"persistent-battle","result"});
    }
    {
        c::CampaignStore first(path),second(path);
        checkCurrent(first,def,next,1);
        auto history = first.history(def.id());
        check(history.size() == 2 && history[1].battleId == "persistent-battle","history and battle survive close/reopen");
        sameState(initial,history[0].state); sameState(next,history[1].state);
        rejects([&]{first.commit(def.id(),1,next,"duplicate",c::BattleResult{"persistent-battle","changed"});},"duplicate battle rejected after restart");
        const auto stale = second.load(def.id());
        auto changed = next; changed.territories.at(3).owner = c::TerritoryOwner::Honor;
        first.commit(def.id(),1,changed,"owner changed");
        rejects([&]{second.commit(def.id(),stale.revision,stale.state,"lost update",c::BattleResult{"loser","wrong"});},"separate connection detects revision conflict");
        check(!second.battleResult(def.id(),"loser"),"conflict has no partial result");
        checkCurrent(second,def,changed,2);
        check(second.commit(def.id(),2,changed,"no-op explicit event") == 3,"explicit same-state event has one revision");
        rejects([&]{first.commit(def.id(),2,changed,"replayed no-op");},"same expected revision cannot replay a no-op");
    }
}

void exceptionRollback(const fs::path& path) {
    const auto def = definition(); const auto initial = populated(def);
    bool fail = true;
    c::CampaignStore store(path,c::StoreOptions{[&]{if(fail)throw std::runtime_error("injected before commit");}});
    rejects([&]{store.create(def,initial,"failed creation");},"creation rolled back after writes");
    rejects([&]{(void)store.load(def.id());},"failed creation invisible");
    fail = false; store.create(def,initial,"start");
    auto next = initial; next.territories.at(1).owner = c::TerritoryOwner::Terror;
    fail = true;
    rejects([&]{store.commit(def.id(),0,next,"failed battle",c::BattleResult{"retryable","result"});},"battle rolled back after every write");
    checkCurrent(store,def,initial,0);
    check(store.history(def.id()).size() == 1 && !store.battleResult(def.id(),"retryable"),"rollback removes event and completion result");
    fail = false;
    check(store.commit(def.id(),0,next,"retry",c::BattleResult{"retryable","result"}) == 1,"rolled-back result ID reusable");
}

void schemaRejection(const fs::path& path) {
    {c::CampaignStore store(path);const auto d = definition();store.create(d,populated(d),"start");}
    sqlite3* database = nullptr;
    const auto utf8 = path.u8string();
    check(sqlite3_open(reinterpret_cast<const char*>(utf8.c_str()),&database) == SQLITE_OK,"open schema fixture");
    struct Close {sqlite3* database;~Close(){sqlite3_close(database);}} close{database};
    check(sqlite3_exec(database,"PRAGMA user_version=2147483647",nullptr,nullptr,nullptr) == SQLITE_OK,"mark future schema");
    rejects([&]{c::CampaignStore incompatible(path);},"future schema must not be silently overwritten");
    sqlite3_stmt* query = nullptr;
    check(sqlite3_prepare_v2(database,"PRAGMA user_version",-1,&query,nullptr) == SQLITE_OK,"read schema version");
    const bool unchanged = sqlite3_step(query) == SQLITE_ROW && sqlite3_column_int(query,0) == 2147483647;
    sqlite3_finalize(query);
    check(unchanged,"rejected open preserves unsupported schema");
}

void damagedSchema(const fs::path& root) {
    {
        const auto path=root/"unrelated.sqlite";const auto utf8=path.u8string();sqlite3* db=nullptr;
        check(sqlite3_open(reinterpret_cast<const char*>(utf8.c_str()),&db)==SQLITE_OK,"open unrelated database");
        const int result=sqlite3_exec(db,"CREATE TABLE sqlitefoo(value TEXT)",nullptr,nullptr,nullptr);
        sqlite3_close(db);check(result==SQLITE_OK,"create nonreserved sqlite-prefixed user table");
        rejects([&]{c::CampaignStore store(path);},"unrelated database with sqlite-prefixed user table");
    }
    const std::vector<std::pair<std::string,std::string>> cases = {
        {"missing-trigger", "DROP TRIGGER campaign_events_no_update"},
        {"changed-trigger", "DROP TRIGGER campaign_events_no_update;CREATE TRIGGER campaign_events_no_update BEFORE UPDATE ON campaign_events BEGIN SELECT 1; END"},
        {"missing-table", "DROP TABLE battle_results"},
        {"extra-table", "CREATE TABLE unrelated(value TEXT)"},
    };
    for(const auto& [label,sql] : cases) {
        const auto path = root / (label+".sqlite");
        {c::CampaignStore store(path);const auto d=definition();store.create(d,populated(d),"start");}
        sqlite3* database = nullptr;
        const auto utf8=path.u8string();
        check(sqlite3_open(reinterpret_cast<const char*>(utf8.c_str()),&database)==SQLITE_OK,"open schema tamper fixture");
        const int result=sqlite3_exec(database,sql.c_str(),nullptr,nullptr,nullptr);
        sqlite3_close(database);
        check(result==SQLITE_OK,"alter schema fixture: "+label);
        rejects([&]{c::CampaignStore store(path);},"damaged schema: "+label);
    }
}

void corruptedSnapshots(const fs::path& root) {
    for(int kind=0;kind<3;++kind) {
        const auto path=root/("snapshot-"+std::to_string(kind)+".sqlite");
        {c::CampaignStore store(path);const auto d=definition();store.create(d,populated(d),"start");}
        sqlite3* db=nullptr;const auto utf8=path.u8string();
        check(sqlite3_open(reinterpret_cast<const char*>(utf8.c_str()),&db)==SQLITE_OK,"open corruption fixture");
        struct Close {sqlite3* db;~Close(){sqlite3_close(db);}} close{db};
        const auto read=[&](const char* sql) {
            sqlite3_stmt* q=nullptr;
            check(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)==SQLITE_OK,"prepare corruption fixture read");
            check(sqlite3_step(q)==SQLITE_ROW,"read corruption fixture row");
            const std::string out(static_cast<const char*>(sqlite3_column_blob(q,0)),sqlite3_column_bytes(q,0));
            sqlite3_finalize(q);return out;
        };
        const auto trigger=read("SELECT sql FROM sqlite_master WHERE name='campaign_events_no_update'");
        auto snapshot=read("SELECT snapshot FROM campaign_events WHERE revision=0");
        if(kind==0) snapshot[5]='9'; // Unsupported snapshot encoding version.
        else if(kind==1) snapshot.resize(snapshot.size()/2); // Truncated territory.
        else {
            // First metric is present: replace its IEEE-754 bits with quiet NaN.
            const size_t offset=6+4+std::string("synthetic").size()+4+4+1+1+4+std::string("maps/runtime.ota").size()+1;
            check(offset+8<=snapshot.size(),"metric corruption offset within fixture");
            for(size_t i=0;i<8;++i) snapshot[offset+i]=static_cast<char>((UINT64_C(0x7ff8000000000000)>>(8*i))&255);
        }
        check(sqlite3_exec(db,"DROP TRIGGER campaign_events_no_update",nullptr,nullptr,nullptr)==SQLITE_OK,"temporarily remove mutation guard");
        sqlite3_stmt* write=nullptr;
        check(sqlite3_prepare_v2(db,"UPDATE campaign_events SET snapshot=? WHERE revision=0",-1,&write,nullptr)==SQLITE_OK,"prepare corrupt snapshot");
        sqlite3_bind_blob(write,1,snapshot.data(),static_cast<int>(snapshot.size()),SQLITE_TRANSIENT);
        const int updated=sqlite3_step(write);sqlite3_finalize(write);
        check(updated==SQLITE_DONE,"write corruption fixture");
        check(sqlite3_exec(db,trigger.c_str(),nullptr,nullptr,nullptr)==SQLITE_OK,"restore exact canonical schema");
        c::CampaignStore reopened(path);
        rejects([&]{(void)reopened.load("synthetic");},"corrupted persisted snapshot fails closed");
        rejects([&]{(void)reopened.history("synthetic");},"corrupted audit history fails closed");
    }
}

int crashWriter(const fs::path& path) {
    c::CampaignStore store(path,c::StoreOptions{[&]{
        // A valid rollback header proves dirty pages spilled beyond SQLite's
        // cache; recovery must undo disk writes, not merely discard RAM.
        auto journal = path; journal += "-journal";
        std::ifstream input(journal,std::ios::binary);
        std::array<unsigned char,8> header{};
        input.read(reinterpret_cast<char*>(header.data()),header.size());
        constexpr std::array<unsigned char,8> magic{0xd9,0xd5,0x05,0xf9,0x20,0xa1,0x63,0xd7};
        std::_Exit(input && header == magic ? 73 : 77);
    }});
    auto current = store.load("synthetic");
    for (auto& [id,t] : current.state.territories) {
        (void)id; t.owner = c::TerritoryOwner::Terror; t.assignedMap = "maps/crash.ota";
        t.recon.fatigueVictoryPoints = 777;
        t.recon.honor.requiredVictoryPoints = 888;
        t.recon.terror.supportVictoryPoints = 999;
    }
    store.commit("synthetic",current.revision,current.state,"interrupted battle",c::BattleResult{"crashed-battle",std::string(4*1024*1024,'x')});
    return 75; // Reaching this means the fault injection failed.
}
void crashRecovery(const fs::path& executable,const fs::path& path) {
    const auto def = definition(); const auto initial = populated(def);
    {c::CampaignStore store(path);store.create(def,initial,"start");}
    check(childProcess(executable,path) == 73,"child terminated inside transaction after actual store writes");
    c::CampaignStore reopened(path);
    checkCurrent(reopened,def,initial,0);
    check(reopened.history(def.id()).size() == 1,"crash leaves no partial event");
    check(!reopened.battleResult(def.id(),"crashed-battle"),"crash leaves no partial completion or result");
    auto next = initial; next.territories.at(1).owner = c::TerritoryOwner::Terror;
    check(reopened.commit(def.id(),0,next,"recovered retry",c::BattleResult{"crashed-battle","real result"}) == 1,
        "recovered transaction can apply same battle once");
}
} // namespace

int main(int argc,char** argv) {
    try {
        fs::path executable;
#ifndef _WIN32
        executable = fs::absolute(argv[0]);
#endif
        if (argc == 3 && std::string(argv[1]) == "--crash-writer") {
#ifdef _WIN32
            const std::wstring command = GetCommandLineW();
            const std::wstring marker = L" --crash-writer \"";
            const auto start = command.find(marker);
            if(start == std::wstring::npos || command.back() != L'"') return 76;
            return crashWriter(fs::path(command.substr(start+marker.size(),command.size()-start-marker.size()-1)));
#else
            return crashWriter(fs::path(argv[2]));
#endif
        }
#ifdef _WIN32
        std::wstring nativeExe(32768,L'\0');
        const DWORD size = GetModuleFileNameW(nullptr,nativeExe.data(),static_cast<DWORD>(nativeExe.size()));
        if(!size || size == nativeExe.size()) throw std::runtime_error("cannot resolve test executable");
        nativeExe.resize(size); executable = fs::path(nativeExe);
#endif
        const auto root = fs::temp_directory_path() / ("tak-store-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root);
        struct Cleanup {fs::path path;~Cleanup(){std::error_code ec;fs::remove_all(path,ec);}} cleanup{root};
        basicSemantics(":memory:");
        basicSemantics(root / fs::path(u8"Café-世界.sqlite"));
        restartAndConflicts(root / "restart.sqlite");
        exceptionRollback(":memory:");
        exceptionRollback(root / "rollback.sqlite");
        crashRecovery(executable,root / fs::path(u8"crash-Café-世界.sqlite"));
        schemaRejection(root / "future.sqlite");
        damagedSchema(root);
        corruptedSnapshots(root);
        std::cout << "PASS: " << checks << " persistent Crusades store checks\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n'; return 1;
    }
}
