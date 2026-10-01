// Synthetic offline operations and recovery. No retail data or inferred rules.
#include "server/crusades/store.h"
#include <sqlite3.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>

namespace c = tak::srv::crusades;
namespace fs = std::filesystem;
namespace {
int checks = 0;
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F run, const char* message) {
    ++checks; try { run(); } catch (const std::runtime_error&) { return; } throw std::runtime_error(message);
}
struct Raw {
    sqlite3* db = nullptr;
    explicit Raw(const fs::path& path) {
        const auto utf8 = path.u8string();
        if (sqlite3_open(reinterpret_cast<const char*>(utf8.c_str()), &db) != SQLITE_OK) throw std::runtime_error("raw database open");
    }
    ~Raw() { sqlite3_close(db); }
    void sql(const std::string& sql) {
        char* error = nullptr;
        if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
            const std::string message = error ? error : "raw SQL failed"; sqlite3_free(error); throw std::runtime_error(message);
        }
    }
    int64_t count(const char* sql) {
        sqlite3_stmt* row = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &row, nullptr) != SQLITE_OK) throw std::runtime_error("raw prepare");
        const auto status = sqlite3_step(row); const auto result = sqlite3_column_int64(row, 0); sqlite3_finalize(row);
        if (status != SQLITE_ROW) throw std::runtime_error("raw query failed");
        return result;
    }
};
c::CampaignDefinition definition(const std::string& id = "test") {
    return c::loadDefinitionText("campaign 1 \"" + id + "\" \"Synthetic\"\nterritory 1 \"One\"\nmap 1 \"one.ota\"\nnative 1 \"Aramon\"\nterritory 2 \"Two\"\n");
}
c::AdminRequest request(int64_t revision, int64_t now = 500) { return {"operator", "synthetic operational reason", revision, now}; }
void enroll(c::CampaignStore& store, const std::string& campaign = "test", const std::string& a = "alice", const std::string& b = "bob") {
    store.setAllegiance(campaign, a, c::Alliance::Honor, -1, 10);
    store.setAllegiance(campaign, b, c::Alliance::Terror, -1, 10);
}
c::BattleContext context(const std::string& a = "alice", const std::string& b = "bob") {
    return {"one.ota", std::string(64, 'a'), std::string(64, 'b'), {a, b}, true};
}
c::IssuedBattle started(c::CampaignStore& store, const std::string& room, int64_t now = 100) {
    auto battle = store.issueBattle("test", store.campaignRevision("test"), 1, context(), now, now + 50);
    store.startBattle(battle.id, battle.launchToken, room, context(), now + 1); return store.battle(battle.id);
}
c::VerifiedMatchResult result(const std::string& replay = "synthetic-replay", c::ResultOutcome outcome = c::ResultOutcome::Victory) {
    c::VerifiedMatchResult result;
    result.outcome = outcome; result.engineBuild = "synthetic-admin-test";
    result.finalTick = 123; result.finalStateHash = UINT64_MAX; result.gameplayFingerprint = 789;
    result.participantResults = {{"alice", 2, 1, 77, 3, 2, "Aramon", 0, false}, {"bob", 1, 2, -4, 2, 0, "Taros", 1, true}};
    if (outcome == c::ResultOutcome::Victory || outcome == c::ResultOutcome::Resignation) {
        result.winners = {"alice"}; result.replayId = replay; result.replayDigest = std::string(64, 'c');
    }
    return result;
}
void unknown(const c::CampaignState& state) {
    for (const auto& [id, territory] : state.territories) {
        (void)id;
        check(!territory.owner && !territory.assignedMap && !territory.recon.fatigueVictoryPoints &&
            !territory.recon.honor.requiredVictoryPoints && !territory.recon.honor.supportVictoryPoints && !territory.recon.honor.battleVictoryPoints &&
            !territory.recon.terror.requiredVictoryPoints && !territory.recon.terror.supportVictoryPoints && !territory.recon.terror.battleVictoryPoints,
            "unknown initial state inferred ownership/map/metrics");
    }
}
std::string bytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void auditedMutations(const fs::path& path) {
    bool fail = false;
    c::CampaignStore store(path, c::StoreOptions{[&] { if (fail) throw std::runtime_error("injected admin failure"); }});
    const auto def = definition();
    for (int invalid = 0; invalid < 6; ++invalid) {
        auto bad = request(-1, 1);
        if (invalid == 0) bad.actor = "Operator";
        if (invalid == 1) bad.reason = "";
        if (invalid == 2) bad.reason = "  \t";
        if (invalid == 3) bad.reason = std::string(513, 'x');
        if (invalid == 4) bad.expectedRevision = 0;
        if (invalid == 5) bad.unixTime = -1;
        rejects([&] { store.adminStart(def, {}, bad); }, "invalid authored start accepted");
    }
    fail = true;
    rejects([&] { store.adminStart(def, {}, request(-1, 1)); }, "start ignored rollback hook");
    fail = false; check(!store.hasCampaign("test"), "failed start leaked campaign");
    store.adminStart(def, {}, request(-1, 1)); unknown(store.load("test").state);
    const auto audit = store.adminHistory("test");
    check(audit.entries.size() == 1 && audit.entries[0].actor == "operator" && audit.entries[0].reason == request(-1).reason &&
          audit.entries[0].action == "start" && audit.entries[0].beforeRevision == -1 && audit.entries[0].afterRevision == 0,
          "start audit missing exact actor/reason/revisions");
    rejects([&] { store.adminStart(def, {}, request(-1, 2)); }, "duplicate campaign start accepted");
    enroll(store);
    auto original = c::makeInitialState(def);
    original.territories.at(1).owner = c::TerritoryOwner::Honor;
    original.territories.at(1).recon.honor.battleVictoryPoints = 7.25;
    store.commit("test", 0, original, "authored checkpoint", c::BattleResult{"legacy", "immutable legacy payload"});
    const auto completed = started(store, "finished-room");
    store.recordVerifiedResult(completed.id, "finished-room", context(), result(), 110);
    const auto pending = started(store, "orphan-room", 120);
    rejects([&] { store.adminReset("test", {}, request(1)); }, "reset silently cancelled live battle");
    rejects([&] { store.adminCancelBattle(pending.id, c::BattleStatus::Issued, request(1)); }, "cancel ignored status guard");
    rejects([&] { store.adminCancelBattle(pending.id, c::BattleStatus::Started, request(0)); }, "cancel ignored campaign revision guard");
    fail = true;
    rejects([&] { store.adminCancelBattle(pending.id, c::BattleStatus::Started, request(1)); }, "cancel ignored rollback hook");
    fail = false;
    check(store.battle(pending.id).status == c::BattleStatus::Started && store.adminHistory("test").entries.size() == 1,
          "failed cancel leaked state/audit");
    store.commit("test", 1, original, "unrelated authored update");
    check(store.adminCancelBattle(pending.id, c::BattleStatus::Started, request(2)).status == c::BattleStatus::Cancelled,
          "orphan cancellation demanded obsolete issued campaign revision");
    check(!store.verifiedResult(pending.id), "admin cancellation manufactured result");
    rejects([&] { store.adminCancelBattle(pending.id, c::BattleStatus::Started, request(2)); }, "duplicate cancellation accepted");
    rejects([&] { store.adminCancelBattle(completed.id, c::BattleStatus::Completed, request(2)); }, "verified result administratively cancelled");
    fail = true;
    rejects([&] { store.adminReset("test", {}, request(2, 501)); }, "reset ignored rollback hook"); fail = false;
    check(store.load("test").revision == 2 && store.load("test").state.territories.at(1).owner == c::TerritoryOwner::Honor,
          "failed reset changed persistent state");
    rejects([&] { store.adminReset("test", {}, request(1)); }, "stale reset accepted");
    auto invalid = original; invalid.territories.at(1).recon.honor.battleVictoryPoints = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { store.adminReset("test", invalid, request(2, 501)); }, "reset accepted fabricated nonfinite metrics");
    check(store.adminReset("test", original, request(2, 501)) == 3, "authored reset failed");
    check(store.load("test").state.territories.at(1).recon.honor.battleVictoryPoints == 7.25, "authored reset changed explicit metric");
    check(store.adminReset("test", {}, request(3, 502)) == 4, "unknown reset failed"); unknown(store.load("test").state);
    rejects([&] { store.adminReset("test", {}, request(4, 501)); }, "admin time moved backwards");
    check(store.allegiance("test", "alice")->alliance == c::Alliance::Honor && store.allegianceHistory("test", "alice").size() == 1,
          "reset erased memberships/history");
    check(store.battleResult("test", "legacy") == "immutable legacy payload", "reset erased legacy result");
    check(store.verifiedResult(completed.id)->finalStateHash == UINT64_MAX && store.territoryHistory("test", 1).entries.size() == 1,
          "reset erased/rewrote verified result or replay history");
    auto altered = result(); altered.finalStateHash = 0;
    rejects([&] { store.recordVerifiedResult(completed.id, "finished-room", context(), altered, 503); }, "replayed terminal result changed original");
    const auto duplicateReplay = started(store, "duplicate-replay-room", 510);
    rejects([&] { store.recordVerifiedResult(duplicateReplay.id, "duplicate-replay-room", context(), result(), 512); }, "replay identity reused");
    check(store.battle(duplicateReplay.id).status == c::BattleStatus::Started && !store.verifiedResult(duplicateReplay.id), "duplicate replay leaked terminal status");
    store.adminCancelBattle(duplicateReplay.id, c::BattleStatus::Started, request(4, 513));
    const auto aborted = started(store, "aborted-room", 520);
    store.recordVerifiedResult(aborted.id, "aborted-room", context(), result("", c::ResultOutcome::ServerAbort), 522);
    rejects([&] { store.adminCancelBattle(aborted.id, c::BattleStatus::Cancelled, request(4, 523)); }, "audited no-credit result erased by cancel");
    check(store.verifiedResult(aborted.id)->outcome == c::ResultOutcome::ServerAbort, "admin rewrote audited terminal abort");
    check(store.health().healthy, "valid admin data fails health");
    const auto limited = store.health(1);
    check(!limited.complete && !limited.healthy && limited.checkedRows == 1 && limited.issues.size() <= 64, "health limit claimed complete validation");
    rejects([&] { store.health(0); }, "health accepted unbounded zero limit");
    { Raw raw(path);
      rejects([&] { raw.sql("UPDATE admin_events SET reason='forged'"); }, "audit mutable");
      rejects([&] { raw.sql("DELETE FROM admin_events"); }, "audit deletable");
      check(raw.count("SELECT count(*) FROM admin_events WHERE battle_id IS NOT NULL") == 2, "duplicate/terminal cancel created audits"); }
}
void pagingAndAuthoredStart() {
    c::CampaignStore store(":memory:"); const auto def = definition(); auto initial = c::makeInitialState(def);
    initial.territories.at(1).recon.fatigueVictoryPoints = -0.0;
    initial.territories.at(2).owner = c::TerritoryOwner::Terror;
    store.adminStart(def, initial, request(-1, 0));
    check(store.adminHistory("test").entries[0].action == "start-authored" && std::signbit(*store.load("test").state.territories.at(1).recon.fatigueVictoryPoints),
          "explicit authored initial state/audit changed");
    for (int revision = 0; revision < 70; ++revision) store.adminReset("test", {}, request(revision, revision + 1));
    const auto first = store.events("test", -1, 17); check(first.entries.size() == 17 && first.truncated, "event page unbounded/missing truncation");
    const auto next = store.events("test", first.entries.back().revision, 64);
    check(next.entries.size() == 54 && !next.truncated && next.entries.front().revision == 17 && next.entries.back().revision == 70,
          "event keyset repeated/skipped rows");
    const auto audit = store.adminHistory("test"); check(audit.entries.size() == 64 && audit.truncated, "audit page exceeded bound");
    const auto rest = store.adminHistory("test", audit.entries.back().sequence);
    check(rest.entries.size() == 7 && !rest.truncated && rest.entries.front().sequence > audit.entries.back().sequence, "audit keyset repeated/skipped rows");
    rejects([&] { store.events("test", -2); }, "invalid revision cursor accepted");
    rejects([&] { store.adminHistory("test", -1); }, "invalid audit cursor accepted");
    rejects([&] { store.events("test", -1, 65); }, "event page exceeded maximum");
    rejects([&] { store.adminHistory("test", 0, 65); }, "audit page exceeded maximum");
    check(store.health().healthy, "paged authored data health failed");
}
void restartRecovery(const fs::path& path) {
    bool fail = false; std::string finished, active, offered;
    { c::CampaignStore store(path, c::StoreOptions{[&] { if (fail) throw std::runtime_error("recovery rollback"); }});
      store.adminStart(definition(), {}, request(-1, 1)); enroll(store); enroll(store, "test", "carol", "dave");
      const auto done = started(store, "previous-terminal"); finished = done.id;
      store.recordVerifiedResult(done.id, "previous-terminal", context(), result(), 110);
      active = started(store, "orphan-runtime", 120).id;
      offered = store.issueBattle("test", 0, 1, context("carol", "dave"), 120, 150).id;
      fail = true; rejects([&] { store.recoverInterruptedBattles(200); }, "recovery ignored rollback"); fail = false;
      check(store.battle(active).status == c::BattleStatus::Started && store.battle(offered).status == c::BattleStatus::Issued &&
            store.adminHistory("test").entries.size() == 1, "partial recovery leaked status/audit"); }
    { c::CampaignStore store(path);
      check(store.recoverInterruptedBattles(200) == 2, "startup omitted orphan issued/started battles");
      check(store.recoverInterruptedBattles(201) == 0, "repeated startup re-cancelled terminal battles");
      check(!store.activeBattleForAccount("alice", 200) && !store.activeBattleForAccount("carol", 200), "startup left global account reservations");
      check(!store.verifiedResult(active) && !store.verifiedResult(offered) && store.verifiedResult(finished)->replayId == "synthetic-replay",
            "startup manufactured or modified verified outcomes");
      const auto audit = store.adminHistory("test");
      check(audit.entries.size() == 3 && audit.entries[1].actor == "takserver" && audit.entries[1].action == "recover-battle" &&
            audit.entries[2].beforeStatus.has_value() && store.load("test").revision == 0, "startup lacked audit or invented credit");
      const auto old = store.battle(active);
      rejects([&] { store.authorizeBattleReport(active, "orphan-runtime", context(), 201); }, "old room resumed after restart");
      rejects([&] { store.startBattle(active, old.launchToken, "replacement-runtime", context(), 201); }, "old capability revived after restart");
      check(store.health().healthy, "startup audit health failed"); }
}
void backupRestore(const fs::path& source, const fs::path& root) {
    c::CampaignStore store(source); const auto before = store.health();
    const auto originalFile = bytes(source); const auto backup = root / "backup.sqlite";
    store.backupTo(backup);
    check(bytes(source) == originalFile, "backup mutated source file");
    { c::CampaignStore restored(backup); const auto after = restored.health();
      check(after.healthy && after.schemaVersion == 9 && after.events == before.events && after.results == before.results &&
            after.memberships == before.memberships && after.adminEvents == before.adminEvents && after.battles == before.battles,
            "independent backup reopen lost durable rows");
      check(restored.load("test").revision == 4 && restored.battleResult("test", "legacy") == "immutable legacy payload" &&
            restored.territoryHistory("test", 1).entries.front().result.outcome == c::ResultOutcome::ServerAbort,
            "backup restore lost revision/result/replay identity"); }
    const auto backupFile = bytes(backup);
    rejects([&] { store.backupTo(backup); }, "backup overwrote existing destination");
    rejects([&] { store.backupTo(source); }, "backup overwrote source");
    check(bytes(backup) == backupFile && bytes(source) == originalFile, "rejected overwrite modified files");
    for (const char* suffix : {"-journal", "-wal", "-shm", ".service-lock"}) {
        auto companion = source; companion += suffix;
        rejects([&] { store.backupTo(companion); }, "backup created a source SQLite/service companion");
        check(!fs::exists(companion), "source companion rejection created a file");
    }
    const auto conflicted = root / "conflicted-backup.sqlite";
    auto existingCompanion = conflicted; existingCompanion += "-journal";
    { std::ofstream file(existingCompanion); file << "user-owned journal"; }
    rejects([&] { store.backupTo(conflicted); }, "backup reused an existing destination SQLite companion");
    check(bytes(existingCompanion) == "user-owned journal" && !fs::exists(conflicted), "companion conflict changed files");
    const auto sentinel = root / "sentinel"; { std::ofstream file(sentinel); file << "user-owned destination"; }
    rejects([&] { store.backupTo(sentinel); }, "backup overwrote unrelated file"); check(bytes(sentinel) == "user-owned destination", "backup changed unrelated contents");
#ifndef _WIN32
    const auto dangling = root / "dangling"; fs::create_symlink(root / "absent", dangling);
    rejects([&] { store.backupTo(dangling); }, "backup followed dangling symlink"); check(fs::is_symlink(dangling), "backup removed existing symlink");
#endif
    store.adminReset("test", {}, request(4, 600));
    { c::CampaignStore restored(backup); check(restored.campaignRevision("test") == 4 && store.campaignRevision("test") == 5, "backup changed alongside source"); }
    c::CampaignStore memory(":memory:"); memory.adminStart(definition("memory"), {}, request(-1, 1)); memory.backupTo(root / "memory.sqlite");
    { c::CampaignStore reopened(root / "memory.sqlite"); check(reopened.health().healthy && reopened.hasCampaign("memory"), "memory backup could not reopen"); }
}
void downgrade(const fs::path& path, int version) {
    std::set<std::string> keep = {"campaigns", "campaign_events", "battle_results", "campaign_events_no_update", "campaign_events_no_delete",
        "battle_results_no_update", "battle_results_no_delete", "campaign_definition_no_update"};
    if (version >= 2) for (const char* name : {"campaign_participants", "allegiance_events", "allegiance_events_no_update", "allegiance_events_no_delete", "campaign_participant_identity_no_update"}) keep.insert(name);
    if (version >= 3) for (const char* name : {"issued_battles", "battle_status_events", "battle_rooms", "issued_battle_identity_no_update", "battle_status_no_update", "battle_status_no_delete", "battle_rooms_no_update", "battle_rooms_no_delete"}) keep.insert(name);
    if (version >= 4) for (const char* name : {"verified_match_results", "verified_results_no_update", "verified_results_no_delete"}) keep.insert(name);
    if (version >= 5) for (const char* name : {"campaign_rules", "issued_battle_rules", "rule_decisions", "campaign_rules_no_update", "campaign_rules_no_delete", "issued_battle_rules_no_update", "issued_battle_rules_no_delete", "rule_decisions_no_update", "rule_decisions_no_delete"}) keep.insert(name);
    if (version >= 6) for (const char* name : {"battle_participants", "battle_participants_account", "battle_participants_no_update", "battle_participants_no_delete"}) keep.insert(name);
    if (version >= 7) keep.insert("battle_participants_global_account");
    if (version >= 8) for (const char* name : {"territory_battle_history", "territory_battle_history_page", "territory_battle_history_no_update", "territory_battle_history_no_delete"}) keep.insert(name);
    Raw raw(path); sqlite3_stmt* rows = nullptr;
    if (sqlite3_prepare_v2(raw.db, "SELECT type,name FROM sqlite_master WHERE name NOT GLOB 'sqlite_*' ORDER BY type DESC", -1, &rows, nullptr) != SQLITE_OK)
        throw std::runtime_error("migration fixture schema prepare");
    std::vector<std::pair<std::string, std::string>> remove;
    while (sqlite3_step(rows) == SQLITE_ROW) {
        const std::string type = reinterpret_cast<const char*>(sqlite3_column_text(rows, 0)), name = reinterpret_cast<const char*>(sqlite3_column_text(rows, 1));
        if (!keep.count(name)) remove.emplace_back(type, name);
    }
    sqlite3_finalize(rows);
    for (const auto& [type, name] : remove) raw.sql("DROP " + type + " IF EXISTS \"" + name + "\"");
    raw.sql("PRAGMA user_version=" + std::to_string(version));
}
void migrations(const fs::path& root) {
    for (int version = 1; version <= 8; ++version) {
        const auto path = root / ("schema-" + std::to_string(version) + ".sqlite"); std::string battle;
        { c::CampaignStore store(path); const auto def = definition(); store.create(def, c::makeInitialState(def), "original historical start"); enroll(store);
          const auto done = started(store, "old-room"); battle = done.id; store.recordVerifiedResult(done.id, "old-room", context(), result(), 110); }
        downgrade(path, version);
        rejects([&] { c::CampaignStore interrupted(path, c::StoreOptions{[] { throw std::runtime_error("migration interrupted"); }}); }, "schema migration ignored rollback hook");
        { Raw raw(path); check(raw.count("PRAGMA user_version") == version && raw.count("SELECT count(*) FROM sqlite_master WHERE name='admin_events'") == 0,
                              "migration rollback left partial schema9"); }
        { c::CampaignStore migrated(path);
          check(migrated.campaignRevision("test") == 0 && migrated.events("test").entries[0].reason == "original historical start" &&
                migrated.adminHistory("test").entries.empty(), "migration invented audit actors/reasons or state");
          if (version >= 2) check(migrated.allegiance("test", "alice").has_value(), "migration lost original membership");
          if (version >= 4) check(migrated.verifiedResult(battle)->finalStateHash == UINT64_MAX && migrated.territoryHistory("test", 1).entries.size() == 1, "migration lost result/history");
          // Downgraded pre-M4 synthetic fixture still has Completed status but
          // intentionally no verified result; health must report that gap.
          check(migrated.health().healthy == (version < 3 || version >= 4), "health ignored missing historical result or rejected valid migration"); }
        { Raw raw(path); check(raw.count("PRAGMA user_version") == 9, "migration failed to publish schema9"); }
    }
    const auto damaged = root / "unsupported.sqlite";
    { c::CampaignStore store(damaged); store.adminStart(definition(), {}, request(-1, 1)); }
    { Raw raw(damaged); raw.sql("PRAGMA user_version=10"); }
    const auto unchanged = bytes(damaged); rejects([&] { c::CampaignStore unsupported(damaged); }, "future schema accepted");
    check(bytes(damaged) == unchanged, "unsupported schema rejection changed file");
}
void tamper(const fs::path& root) {
    for (int kind = 0; kind < 6; ++kind) {
        const auto path = root / ("tampered-" + std::to_string(kind) + ".sqlite");
        { c::CampaignStore store(path); store.adminStart(definition(), {}, request(-1, 1)); enroll(store);
          const auto done = started(store, "tampered-room"); store.recordVerifiedResult(done.id, "tampered-room", context(), result(), 110); }
        { Raw raw(path);
          if (kind == 0) { raw.sql("DROP TRIGGER campaign_events_no_update"); raw.sql("UPDATE campaign_events SET snapshot=x'00'");
              raw.sql("CREATE TRIGGER campaign_events_no_update BEFORE UPDATE ON campaign_events BEGIN SELECT RAISE(ABORT,'immutable campaign event'); END"); }
          if (kind == 1) { raw.sql("DROP TRIGGER verified_results_no_update"); raw.sql("UPDATE verified_match_results SET payload=x'00'");
              raw.sql("CREATE TRIGGER verified_results_no_update BEFORE UPDATE ON verified_match_results BEGIN SELECT RAISE(ABORT,'immutable verified result'); END"); }
          if (kind == 2) { raw.sql("DROP TRIGGER admin_events_no_update"); raw.sql("UPDATE admin_events SET reason='rewritten'");
              raw.sql("CREATE TRIGGER admin_events_no_update BEFORE UPDATE ON admin_events BEGIN SELECT RAISE(ABORT,'immutable admin event'); END"); }
          if (kind == 3) { raw.sql("DROP TRIGGER territory_battle_history_no_update"); raw.sql("UPDATE territory_battle_history SET recorded_unix=111");
              raw.sql("CREATE TRIGGER territory_battle_history_no_update BEFORE UPDATE ON territory_battle_history BEGIN SELECT RAISE(ABORT,'immutable territory battle history'); END"); }
          if (kind == 4) raw.sql("DELETE FROM campaigns");
          if (kind == 5) { raw.sql("DROP TRIGGER rule_decisions_no_update"); raw.sql("UPDATE rule_decisions SET snapshot=x'00'");
              raw.sql("CREATE TRIGGER rule_decisions_no_update BEFORE UPDATE ON rule_decisions BEGIN SELECT RAISE(ABORT,'immutable rules decision'); END"); }
        }
        c::CampaignStore store(path); const auto original = bytes(path); const auto health = store.health();
        check(!health.healthy && !health.issues.empty() && health.issues.size() <= 64, "health accepted corrupted semantic/foreign-key state");
        const auto backup = root / ("rejected-backup-" + std::to_string(kind));
        rejects([&] { store.backupTo(backup); }, "backup accepted damaged database");
        check(!fs::exists(backup) && bytes(path) == original, "failed backup left destination or mutated source");
    }
    const auto schema = root / "tampered-schema.sqlite";
    { c::CampaignStore store(schema); store.adminStart(definition(), {}, request(-1, 1)); Raw raw(schema); raw.sql("DROP TRIGGER admin_events_no_delete");
      check(!store.health().healthy, "live health failed to inspect immutable trigger schema"); }
    rejects([&] { c::CampaignStore reopened(schema); }, "schema tampering accepted on reopen");
}
} // namespace
int main() {
    const auto root = fs::temp_directory_path() / ("tak-admin-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{root};
    try {
        fs::create_directories(root); const auto source = root / "source.sqlite";
        auditedMutations(source); pagingAndAuthoredStart(); restartRecovery(root / "restart.sqlite"); backupRestore(source, root); migrations(root); tamper(root);
        std::cout << "PASS: " << checks << " Crusades admin checks\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
