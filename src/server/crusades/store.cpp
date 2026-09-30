#include "server/crusades/store.h"
#include <sqlite3.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include "net/crypto.h"
#include <stdexcept>

namespace tak::srv::crusades {
namespace {
constexpr int kApplicationId = 0x54414b43; // TAKC
constexpr int kSchemaVersion = 5;
constexpr size_t kMaxPayload = 16 * 1024 * 1024;

const std::vector<std::string>& schemaStatements() {
    static const std::vector<std::string> statements{
        "CREATE TABLE campaigns(id TEXT PRIMARY KEY,definition TEXT NOT NULL,revision INTEGER NOT NULL CHECK(revision>=0))",
        "CREATE TABLE campaign_events(campaign_id TEXT NOT NULL REFERENCES campaigns(id),revision INTEGER NOT NULL CHECK(revision>=0),reason TEXT NOT NULL,battle_id TEXT,snapshot BLOB NOT NULL,PRIMARY KEY(campaign_id,revision))",
        "CREATE TABLE battle_results(campaign_id TEXT NOT NULL,battle_id TEXT NOT NULL,payload BLOB NOT NULL,revision INTEGER NOT NULL,PRIMARY KEY(campaign_id,battle_id),FOREIGN KEY(campaign_id,revision) REFERENCES campaign_events(campaign_id,revision))",
        "CREATE TRIGGER campaign_events_no_update BEFORE UPDATE ON campaign_events BEGIN SELECT RAISE(ABORT,'immutable campaign event'); END",
        "CREATE TRIGGER campaign_events_no_delete BEFORE DELETE ON campaign_events BEGIN SELECT RAISE(ABORT,'immutable campaign event'); END",
        "CREATE TRIGGER battle_results_no_update BEFORE UPDATE ON battle_results BEGIN SELECT RAISE(ABORT,'immutable battle result'); END",
        "CREATE TRIGGER battle_results_no_delete BEFORE DELETE ON battle_results BEGIN SELECT RAISE(ABORT,'immutable battle result'); END",
        "CREATE TRIGGER campaign_definition_no_update BEFORE UPDATE OF id,definition ON campaigns BEGIN SELECT RAISE(ABORT,'immutable campaign definition'); END",
    };
    return statements;
}
const std::vector<std::string>& allegianceSchemaStatements() {
    static const std::vector<std::string> statements{
        "CREATE TABLE campaign_participants(campaign_id TEXT NOT NULL REFERENCES campaigns(id),account_id TEXT NOT NULL,revision INTEGER NOT NULL CHECK(revision>=0),PRIMARY KEY(campaign_id,account_id))",
        "CREATE TABLE allegiance_events(campaign_id TEXT NOT NULL,account_id TEXT NOT NULL,revision INTEGER NOT NULL CHECK(revision>=0),alliance INTEGER NOT NULL CHECK(alliance IN (1,2)),joined_unix INTEGER NOT NULL CHECK(joined_unix>=0),changed_unix INTEGER NOT NULL CHECK(changed_unix>=joined_unix),PRIMARY KEY(campaign_id,account_id,revision),FOREIGN KEY(campaign_id,account_id) REFERENCES campaign_participants(campaign_id,account_id))",
        "CREATE TRIGGER allegiance_events_no_update BEFORE UPDATE ON allegiance_events BEGIN SELECT RAISE(ABORT,'immutable allegiance event'); END",
        "CREATE TRIGGER allegiance_events_no_delete BEFORE DELETE ON allegiance_events BEGIN SELECT RAISE(ABORT,'immutable allegiance event'); END",
        "CREATE TRIGGER campaign_participant_identity_no_update BEFORE UPDATE OF campaign_id,account_id ON campaign_participants BEGIN SELECT RAISE(ABORT,'immutable participant identity'); END",
    };
    return statements;
}
const std::vector<std::string>& battleSchemaStatements() {
    static const std::vector<std::string> statements{
        "CREATE TABLE issued_battles(id TEXT PRIMARY KEY,campaign_id TEXT NOT NULL REFERENCES campaigns(id),campaign_revision INTEGER NOT NULL,territory INTEGER NOT NULL,context BLOB NOT NULL,created_unix INTEGER NOT NULL,expires_unix INTEGER NOT NULL,launch_token TEXT NOT NULL UNIQUE,revision INTEGER NOT NULL CHECK(revision>=0))",
        "CREATE TABLE battle_status_events(battle_id TEXT NOT NULL REFERENCES issued_battles(id),revision INTEGER NOT NULL CHECK(revision>=0),status INTEGER NOT NULL CHECK(status BETWEEN 0 AND 4),changed_unix INTEGER NOT NULL,PRIMARY KEY(battle_id,revision))",
        "CREATE TABLE battle_rooms(room_token TEXT PRIMARY KEY,battle_id TEXT NOT NULL UNIQUE REFERENCES issued_battles(id))",
        "CREATE TRIGGER issued_battle_identity_no_update BEFORE UPDATE OF id,campaign_id,campaign_revision,territory,context,created_unix,expires_unix,launch_token ON issued_battles BEGIN SELECT RAISE(ABORT,'immutable issued battle'); END",
        "CREATE TRIGGER battle_status_no_update BEFORE UPDATE ON battle_status_events BEGIN SELECT RAISE(ABORT,'immutable battle status'); END",
        "CREATE TRIGGER battle_status_no_delete BEFORE DELETE ON battle_status_events BEGIN SELECT RAISE(ABORT,'immutable battle status'); END",
        "CREATE TRIGGER battle_rooms_no_update BEFORE UPDATE ON battle_rooms BEGIN SELECT RAISE(ABORT,'immutable battle room'); END",
        "CREATE TRIGGER battle_rooms_no_delete BEFORE DELETE ON battle_rooms BEGIN SELECT RAISE(ABORT,'immutable battle room'); END",
    };
    return statements;
}
const std::vector<std::string>& resultSchemaStatements() {
    static const std::vector<std::string> statements{
        "CREATE TABLE verified_match_results(battle_id TEXT PRIMARY KEY REFERENCES issued_battles(id),replay_id TEXT UNIQUE,replay_digest TEXT,outcome INTEGER NOT NULL CHECK(outcome BETWEEN 0 AND 9),winner_account TEXT,payload BLOB NOT NULL,recorded_unix INTEGER NOT NULL CHECK(recorded_unix>=0))",
        "CREATE TRIGGER verified_results_no_update BEFORE UPDATE ON verified_match_results BEGIN SELECT RAISE(ABORT,'immutable verified result'); END",
        "CREATE TRIGGER verified_results_no_delete BEFORE DELETE ON verified_match_results BEGIN SELECT RAISE(ABORT,'immutable verified result'); END",
    };
    return statements;
}
const std::vector<std::string>& rulesSchemaStatements() {
    static const std::vector<std::string> statements{
        "CREATE TABLE campaign_rules(campaign_id TEXT PRIMARY KEY REFERENCES campaigns(id),policy_id TEXT NOT NULL)",
        "CREATE TABLE issued_battle_rules(battle_id TEXT PRIMARY KEY REFERENCES issued_battles(id),policy_id TEXT NOT NULL)",
        "CREATE TABLE rule_decisions(battle_id TEXT PRIMARY KEY REFERENCES verified_match_results(battle_id),policy_id TEXT NOT NULL,before_revision INTEGER NOT NULL CHECK(before_revision>=0),after_revision INTEGER NOT NULL CHECK(after_revision>=before_revision),disposition INTEGER NOT NULL CHECK(disposition BETWEEN 0 AND 4),evidence INTEGER NOT NULL CHECK(evidence BETWEEN 0 AND 3),reason TEXT NOT NULL,snapshot BLOB NOT NULL)",
        "CREATE TRIGGER campaign_rules_no_update BEFORE UPDATE ON campaign_rules BEGIN SELECT RAISE(ABORT,'immutable campaign rules'); END",
        "CREATE TRIGGER campaign_rules_no_delete BEFORE DELETE ON campaign_rules BEGIN SELECT RAISE(ABORT,'immutable campaign rules'); END",
        "CREATE TRIGGER issued_battle_rules_no_update BEFORE UPDATE ON issued_battle_rules BEGIN SELECT RAISE(ABORT,'immutable battle rules'); END",
        "CREATE TRIGGER issued_battle_rules_no_delete BEFORE DELETE ON issued_battle_rules BEGIN SELECT RAISE(ABORT,'immutable battle rules'); END",
        "CREATE TRIGGER rule_decisions_no_update BEFORE UPDATE ON rule_decisions BEGIN SELECT RAISE(ABORT,'immutable rules decision'); END",
        "CREATE TRIGGER rule_decisions_no_delete BEFORE DELETE ON rule_decisions BEGIN SELECT RAISE(ABORT,'immutable rules decision'); END",
    };
    return statements;
}
void validateAccountId(const std::string& accountId) {
    const auto alnum = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    if (accountId.size() < 3 || accountId.size() > 20 || !alnum(accountId.front()))
        throw std::runtime_error("invalid canonical account ID");
    for (const char c : accountId)
        if (!alnum(c) && c != '_' && c != '-' && c != '.')
            throw std::runtime_error("invalid canonical account ID");
}
[[noreturn]] void fail(sqlite3* db, const std::string& what) {
    throw std::runtime_error(what + ": " + sqlite3_errmsg(db));
}
void exec(sqlite3* db, const char* sql) {
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) fail(db, "campaign database operation");
}
struct Statement {
    sqlite3* db;
    sqlite3_stmt* value = nullptr;
    Statement(sqlite3* db_, const char* sql) : db(db_) {
        if (sqlite3_prepare_v2(db, sql, -1, &value, nullptr) != SQLITE_OK) fail(db, "prepare campaign query");
    }
    ~Statement() { sqlite3_finalize(value); }
    void text(int index, const std::string& s) {
        if (s.size() > kMaxPayload) throw std::runtime_error("campaign string exceeds storage limit");
        if (sqlite3_bind_text(value, index, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT) != SQLITE_OK)
            fail(db, "bind campaign string");
    }
    void blob(int index, const std::string& s) {
        if (s.size() > kMaxPayload) throw std::runtime_error("campaign snapshot exceeds storage limit");
        if (sqlite3_bind_blob(value, index, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT) != SQLITE_OK)
            fail(db, "bind campaign snapshot");
    }
    void integer(int index, int64_t n) {
        if (sqlite3_bind_int64(value, index, n) != SQLITE_OK) fail(db, "bind campaign integer");
    }
    bool row() {
        const int rc = sqlite3_step(value);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        fail(db, "execute campaign query");
    }
    void done() { if (row()) throw std::runtime_error("unexpected campaign query row"); }
    std::string bytes(int index) const {
        const auto* ptr = static_cast<const char*>(sqlite3_column_blob(value, index));
        const int count = sqlite3_column_bytes(value, index);
        if (count < 0 || static_cast<size_t>(count) > kMaxPayload) throw std::runtime_error("oversized stored campaign value");
        return count == 0 ? std::string{} : std::string(ptr, static_cast<size_t>(count));
    }
    int64_t number(int index) const { return sqlite3_column_int64(value, index); }
};
struct Transaction {
    sqlite3* db;
    bool committed = false;
    explicit Transaction(sqlite3* db_, bool write = true) : db(db_) { exec(db, write ? "BEGIN IMMEDIATE" : "BEGIN"); }
    ~Transaction() { if (!committed) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); }
    void finish() { exec(db, "COMMIT"); committed = true; }
};
int64_t scalar(sqlite3* db, const char* sql) {
    Statement query(db, sql);
    if (!query.row()) throw std::runtime_error("missing database metadata");
    return query.number(0);
}
RulesPolicy permittedPolicy(const std::string& id, const StoreOptions& options) {
    auto policy = parsePolicyIdentifier(id);
    if (policy.mode == RulesMode::Modern || (policy.mode == RulesMode::Fixture && !options.allowFixtureRules))
        throw std::runtime_error("campaign rules policy is not enabled");
    return policy;
}
void requireText(const std::string& text, const char* name) {
    if (text.empty() || text.size() > kMaxPayload || text.find('\0') != std::string::npos)
        throw std::runtime_error(std::string("invalid ") + name);
}
std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) { if (c == '"' || c == '\\') out += '\\'; out += c; }
    return out + '"';
}
std::string definitionText(const CampaignDefinition& definition) {
    std::string text = "campaign 1 " + quote(definition.id()) + " " + quote(definition.displayName()) + "\n";
    for (const auto& [id, territory] : definition.territories()) {
        const std::string key = std::to_string(id);
        text += "territory " + key + " " + quote(territory.displayName) + "\n";
        if (territory.nativeFaction) text += "native " + key + " " + quote(*territory.nativeFaction) + "\n";
        if (territory.terrain) text += "terrain " + key + " " + quote(*territory.terrain) + "\n";
        if (territory.mapIdentifier) text += "map " + key + " " + quote(*territory.mapIdentifier) + "\n";
        if (territory.neighbors) {
            text += "neighbors " + key;
            for (const auto neighbor : *territory.neighbors) text += " " + std::to_string(neighbor);
            text += '\n';
        }
    }
    return text;
}
void put(std::string& output, uint64_t value, unsigned bytes) {
    if (output.size() > kMaxPayload || bytes > kMaxPayload - output.size())
        throw std::runtime_error("oversized campaign snapshot");
    while (bytes--) { output += static_cast<char>(value & 255); value >>= 8; }
}
void putString(std::string& output, const std::string& value) {
    if (value.size() > kMaxPayload) throw std::runtime_error("oversized campaign snapshot field");
    put(output, value.size(), 4);
    if (value.size() > kMaxPayload - output.size()) throw std::runtime_error("oversized campaign snapshot");
    output += value;
}
void putMetric(std::string& output, const std::optional<double>& value) {
    put(output, value ? 1 : 0, 1);
    if (value) {
        static_assert(sizeof(double) == sizeof(uint64_t) && std::numeric_limits<double>::is_iec559);
        uint64_t bits; std::memcpy(&bits, &*value, sizeof bits); put(output, bits, 8);
    }
}
std::string encode(const CampaignState& state) {
    std::string output = "TAKCS1";
    putString(output, state.campaignId);
    put(output, state.territories.size(), 4);
    for (const auto& [id, territory] : state.territories) {
        put(output, id, 4);
        put(output, territory.owner ? 1 + static_cast<unsigned>(*territory.owner) : 0, 1);
        put(output, territory.assignedMap ? 1 : 0, 1);
        if (territory.assignedMap) putString(output, *territory.assignedMap);
        putMetric(output, territory.recon.fatigueVictoryPoints);
        for (const auto* side : {&territory.recon.honor, &territory.recon.terror}) {
            putMetric(output, side->requiredVictoryPoints);
            putMetric(output, side->supportVictoryPoints);
            putMetric(output, side->battleVictoryPoints);
        }
    }
    if (output.size() > kMaxPayload) throw std::runtime_error("oversized campaign snapshot");
    return output;
}
struct Reader {
    const std::string& input;
    size_t offset = 0;
    uint64_t get(unsigned bytes) {
        if (bytes > input.size() - offset) throw std::runtime_error("truncated campaign snapshot");
        uint64_t value = 0;
        for (unsigned i = 0; i < bytes; ++i) value |= uint64_t(static_cast<unsigned char>(input[offset++])) << (8*i);
        return value;
    }
    std::string string() {
        const auto size = get(4);
        if (size > input.size() - offset) throw std::runtime_error("truncated campaign snapshot string");
        const auto result = input.substr(offset, static_cast<size_t>(size)); offset += static_cast<size_t>(size); return result;
    }
    bool present() {
        const auto value = get(1);
        if (value > 1) throw std::runtime_error("invalid snapshot optional flag");
        return value != 0;
    }
    std::optional<double> metric() {
        if (!present()) return {};
        const uint64_t bits = get(8); double value; std::memcpy(&value, &bits, sizeof value); return value;
    }
};
CampaignState decode(const std::string& input, const CampaignDefinition& definition) {
    if (input.size() < 6 || input.substr(0, 6) != "TAKCS1") throw std::runtime_error("unsupported campaign snapshot");
    Reader reader{input, 6};
    CampaignState state;
    state.campaignId = reader.string();
    const auto count = reader.get(4);
    if (count != definition.territories().size()) throw std::runtime_error("snapshot territory count mismatch");
    for (uint64_t n = 0; n < count; ++n) {
        const auto id = static_cast<TerritoryId>(reader.get(4));
        TerritoryState territory;
        const auto owner = reader.get(1);
        if (owner > 3) throw std::runtime_error("invalid snapshot owner");
        if (owner) territory.owner = static_cast<TerritoryOwner>(owner - 1);
        if (reader.present()) territory.assignedMap = reader.string();
        territory.recon.fatigueVictoryPoints = reader.metric();
        for (auto* side : {&territory.recon.honor, &territory.recon.terror}) {
            side->requiredVictoryPoints = reader.metric();
            side->supportVictoryPoints = reader.metric();
            side->battleVictoryPoints = reader.metric();
        }
        if (!state.territories.emplace(id, std::move(territory)).second) throw std::runtime_error("duplicate snapshot territory");
    }
    if (reader.offset != input.size()) throw std::runtime_error("trailing snapshot bytes");
    validateState(definition, state);
    return state;
}
} // namespace

struct CampaignStore::Impl {
    sqlite3* db = nullptr;
    StoreOptions options;
    ~Impl() { if (db) sqlite3_close(db); }
    void event(const std::string& id, int64_t revision, const CampaignState& state,
               const std::string& reason, const std::optional<BattleResult>& battle) {
        Statement insert(db, "INSERT INTO campaign_events(campaign_id,revision,reason,battle_id,snapshot) VALUES(?,?,?,?,?)");
        insert.text(1, id); insert.integer(2, revision); insert.text(3, reason);
        if (battle) insert.text(4, battle->battleId);
        insert.blob(5, encode(state)); insert.done();
    }
    void finish(Transaction& transaction) { if (options.beforeCommit) options.beforeCommit(); transaction.finish(); }
};

CampaignStore::CampaignStore(const std::filesystem::path& path, StoreOptions options) : impl_(std::make_unique<Impl>()) {
    impl_->options = std::move(options);
    const auto utf8 = path.u8string();
    if (utf8.empty() || utf8.find('\0') != decltype(utf8)::npos) throw std::runtime_error("invalid campaign database path");
    if (sqlite3_open_v2(reinterpret_cast<const char*>(utf8.c_str()), &impl_->db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
        fail(impl_->db, "open campaign database");
    auto* db = impl_->db;
    sqlite3_busy_timeout(db, 5000);
    // Refuse unrelated or newer databases before changing persistent pragmas.
    const auto checkIdentity = [&]() {
        const auto app = scalar(db, "PRAGMA application_id");
        const auto version = scalar(db, "PRAGMA user_version");
        const bool empty = scalar(db, "SELECT count(*) FROM sqlite_master WHERE name NOT GLOB 'sqlite_*'") == 0;
        if (!(app == kApplicationId && (version >= 1 && version <= kSchemaVersion) && !empty) && !(app == 0 && version == 0 && empty))
            throw std::runtime_error("not a supported TAK campaign database");
        if (!empty) {
            std::vector<std::string> actual;
            Statement schema(db, "SELECT sql FROM sqlite_master WHERE name NOT GLOB 'sqlite_*'");
            while (schema.row()) actual.push_back(schema.bytes(0));
            auto expected = schemaStatements();
            if (version >= 2) expected.insert(expected.end(), allegianceSchemaStatements().begin(), allegianceSchemaStatements().end());
            if (version >= 3) expected.insert(expected.end(), battleSchemaStatements().begin(), battleSchemaStatements().end());
            if (version >= 4) expected.insert(expected.end(), resultSchemaStatements().begin(), resultSchemaStatements().end());
            if (version >= 5) expected.insert(expected.end(), rulesSchemaStatements().begin(), rulesSchemaStatements().end());
            std::sort(actual.begin(), actual.end());
            std::sort(expected.begin(), expected.end());
            if (actual != expected) throw std::runtime_error("unsupported or damaged campaign database schema");
        }
        return empty ? int64_t(0) : version;
    };
    {
        Transaction read(db, false);
        (void)checkIdentity();
        read.finish();
    }
    exec(db, "PRAGMA foreign_keys=ON");
    exec(db, "PRAGMA journal_mode=DELETE");
    exec(db, "PRAGMA synchronous=EXTRA");
    if (scalar(db, "PRAGMA foreign_keys") != 1 || scalar(db, "PRAGMA synchronous") != 3)
        throw std::runtime_error("campaign database durability settings unavailable");
    {
        Statement mode(db, "PRAGMA journal_mode");
        if (!mode.row() || (mode.bytes(0) != "delete" && mode.bytes(0) != "memory"))
            throw std::runtime_error("campaign database rollback journal unavailable");
    }
    Transaction transaction(db);
    // Another opener may have initialized the file while this connection waited.
    const auto version = checkIdentity();
    if (version == 0) {
        for (const auto& sql : schemaStatements()) exec(db, sql.c_str());
        exec(db, "PRAGMA application_id=1413565251");
    }
    if (version < 2)
        for (const auto& sql : allegianceSchemaStatements()) exec(db, sql.c_str());
    if (version < 3)
        for (const auto& sql : battleSchemaStatements()) exec(db, sql.c_str());
    if (version < 4)
        for (const auto& sql : resultSchemaStatements()) exec(db, sql.c_str());
    if (version < 5) {
        for (const auto& sql : rulesSchemaStatements()) exec(db, sql.c_str());
        // Existing campaigns and pending battles remain historical. Old verified
        // results are NOT replayed through rules or given invented audit decisions.
        exec(db,"INSERT INTO campaign_rules SELECT id,'historical-darien-v1' FROM campaigns");
        exec(db,"INSERT INTO issued_battle_rules SELECT id,'historical-darien-v1' FROM issued_battles");
        exec(db,"PRAGMA user_version=5");
        if (version != 0 && impl_->options.beforeCommit) impl_->options.beforeCommit();
    }
    Statement policies(db, "SELECT policy_id FROM campaign_rules");
    while (policies.row()) (void)permittedPolicy(policies.bytes(0), impl_->options);
    transaction.finish();
}
CampaignStore::~CampaignStore() = default;

void CampaignStore::create(const CampaignDefinition& definition, const CampaignState& initialState, const std::string& reason, const RulesPolicy& rules) {
    const auto policyId = policyIdentifier(rules);
    (void)permittedPolicy(policyId, impl_->options);
    requireText(reason, "campaign event reason");
    validateState(definition, initialState);
    const auto text = definitionText(definition);
    // Also ensures serialized canonical form remains within loader constraints.
    (void)loadDefinitionText(text);
    Transaction transaction(impl_->db);
    Statement insert(impl_->db, "INSERT INTO campaigns(id,definition,revision) VALUES(?,?,0)");
    insert.text(1, definition.id()); insert.text(2, text); insert.done();
    Statement policy(impl_->db, "INSERT INTO campaign_rules VALUES(?,?)");
    policy.text(1, definition.id()); policy.text(2, policyId); policy.done();
    impl_->event(definition.id(), 0, initialState, reason, {});
    impl_->finish(transaction);
}

StoredCampaign CampaignStore::load(const std::string& campaignId) const {
    Statement query(impl_->db, "SELECT c.definition,c.revision,e.snapshot,p.policy_id FROM campaigns c JOIN campaign_events e ON e.campaign_id=c.id AND e.revision=c.revision JOIN campaign_rules p ON p.campaign_id=c.id WHERE c.id=?");
    query.text(1, campaignId);
    if (!query.row()) throw std::runtime_error("unknown or incomplete campaign");
    auto definition = loadDefinitionText(query.bytes(0));
    const int64_t revision = query.number(1);
    auto state = decode(query.bytes(2), definition);
    if (definition.id() != campaignId || revision < 0) throw std::runtime_error("invalid stored campaign identity/revision");
    return {std::move(definition), std::move(state), revision, permittedPolicy(query.bytes(3), impl_->options)};
}

bool CampaignStore::hasCampaign(const std::string& campaignId) const {
    Statement query(impl_->db, "SELECT 1 FROM campaigns WHERE id=?");
    query.text(1, campaignId);
    return query.row();
}

int64_t CampaignStore::commit(const std::string& campaignId, int64_t expectedRevision,
        const CampaignState& nextState, const std::string& reason, const std::optional<BattleResult>& battle) {
    requireText(reason, "campaign event reason");
    if (battle && battle->battleId.rfind("issued:", 0) == 0) throw std::runtime_error("issued battles require authorized result processing");
    if (battle) { requireText(battle->battleId, "battle ID"); if (battle->payload.size() > kMaxPayload) throw std::runtime_error("oversized battle result"); }
    Transaction transaction(impl_->db);
    const auto current = load(campaignId);
    if (current.revision != expectedRevision) throw std::runtime_error("campaign revision conflict");
    if (expectedRevision == std::numeric_limits<int64_t>::max()) throw std::runtime_error("campaign revision exhausted");
    validateState(current.definition, nextState);
    const int64_t revision = expectedRevision + 1;
    impl_->event(campaignId, revision, nextState, reason, battle);
    if (battle) {
        Statement insert(impl_->db, "INSERT INTO battle_results(campaign_id,battle_id,payload,revision) VALUES(?,?,?,?)");
        insert.text(1, campaignId); insert.text(2, battle->battleId); insert.blob(3, battle->payload); insert.integer(4, revision); insert.done();
    }
    Statement update(impl_->db, "UPDATE campaigns SET revision=? WHERE id=?");
    update.integer(1, revision); update.text(2, campaignId); update.done();
    impl_->finish(transaction);
    return revision;
}

std::vector<CampaignEvent> CampaignStore::history(const std::string& campaignId) const {
    Transaction transaction(impl_->db, false);
    const auto current = load(campaignId);
    Statement query(impl_->db, "SELECT revision,reason,battle_id,snapshot FROM campaign_events WHERE campaign_id=? ORDER BY revision");
    query.text(1, campaignId);
    std::vector<CampaignEvent> events;
    while (query.row()) {
        const auto revision = query.number(0);
        if (revision != static_cast<int64_t>(events.size())) throw std::runtime_error("campaign history revision gap");
        std::optional<std::string> battle;
        if (sqlite3_column_type(query.value, 2) != SQLITE_NULL) battle = query.bytes(2);
        events.push_back({revision, query.bytes(1), std::move(battle), decode(query.bytes(3), current.definition)});
    }
    if (events.empty() || events.back().revision != current.revision) throw std::runtime_error("campaign history/current revision mismatch");
    transaction.finish();
    return events;
}

std::optional<std::string> CampaignStore::battleResult(const std::string& campaignId, const std::string& battleId) const {
    Transaction transaction(impl_->db, false);
    (void)load(campaignId);
    Statement query(impl_->db, "SELECT payload FROM battle_results WHERE campaign_id=? AND battle_id=?");
    query.text(1, campaignId); query.text(2, battleId);
    std::optional<std::string> result;
    if (query.row()) result = query.bytes(0);
    transaction.finish();
    return result;
}

namespace {
std::optional<Allegiance> readAllegiance(sqlite3* db, const std::string& campaignId,
                                       const std::string& accountId) {
    Statement query(db, "SELECT p.revision,e.alliance,e.joined_unix,e.changed_unix FROM campaign_participants p LEFT JOIN allegiance_events e ON e.campaign_id=p.campaign_id AND e.account_id=p.account_id AND e.revision=p.revision WHERE p.campaign_id=? AND p.account_id=?");
    query.text(1, campaignId); query.text(2, accountId);
    if (!query.row()) return {};
    if (sqlite3_column_type(query.value, 1) == SQLITE_NULL)
        throw std::runtime_error("missing current allegiance event");
    const auto revision = query.number(0), side = query.number(1), joined = query.number(2), changed = query.number(3);
    if (revision < 0 || (side != 1 && side != 2) || joined < 0 || changed < joined)
        throw std::runtime_error("invalid stored allegiance");
    return Allegiance{accountId, static_cast<Alliance>(side), joined, changed, revision};
}
} // namespace

std::optional<Allegiance> CampaignStore::allegiance(const std::string& campaignId,
                                                  const std::string& accountId) const {
    validateAccountId(accountId);
    Transaction transaction(impl_->db, false);
    (void)load(campaignId);
    auto result = readAllegiance(impl_->db, campaignId, accountId);
    transaction.finish();
    return result;
}

Allegiance CampaignStore::setAllegiance(const std::string& campaignId, const std::string& accountId,
                                       Alliance alliance, int64_t expectedRevision, int64_t unixTime) {
    validateAccountId(accountId);
    if (alliance != Alliance::Honor && alliance != Alliance::Terror)
        throw std::runtime_error("invalid allegiance");
    if (expectedRevision < -1 || unixTime < 0) throw std::runtime_error("invalid allegiance revision/time");
    Transaction transaction(impl_->db);
    (void)load(campaignId);
    const auto previous = readAllegiance(impl_->db, campaignId, accountId);
    if ((previous ? previous->revision : -1) != expectedRevision)
        throw std::runtime_error("allegiance revision conflict");
    if (previous && previous->alliance == alliance) throw std::runtime_error("allegiance unchanged");
    if (previous && unixTime < previous->changedUnix) throw std::runtime_error("allegiance timestamp moved backwards");
    if (expectedRevision == std::numeric_limits<int64_t>::max()) throw std::runtime_error("allegiance revision exhausted");
    Allegiance result{accountId, alliance, previous ? previous->joinedUnix : unixTime, unixTime, expectedRevision + 1};
    if (!previous) {
        Statement insert(impl_->db, "INSERT INTO campaign_participants(campaign_id,account_id,revision) VALUES(?,?,0)");
        insert.text(1, campaignId); insert.text(2, accountId); insert.done();
    }
    Statement event(impl_->db, "INSERT INTO allegiance_events(campaign_id,account_id,revision,alliance,joined_unix,changed_unix) VALUES(?,?,?,?,?,?)");
    event.text(1, campaignId); event.text(2, accountId); event.integer(3, result.revision);
    event.integer(4, static_cast<int>(alliance)); event.integer(5, result.joinedUnix); event.integer(6, result.changedUnix); event.done();
    Statement update(impl_->db, "UPDATE campaign_participants SET revision=? WHERE campaign_id=? AND account_id=?");
    update.integer(1, result.revision); update.text(2, campaignId); update.text(3, accountId); update.done();
    impl_->finish(transaction);
    return result;
}

std::vector<Allegiance> CampaignStore::allegianceHistory(const std::string& campaignId,
                                                       const std::string& accountId) const {
    validateAccountId(accountId);
    Transaction transaction(impl_->db, false);
    (void)load(campaignId);
    const auto current = readAllegiance(impl_->db, campaignId, accountId);
    Statement query(impl_->db, "SELECT revision,alliance,joined_unix,changed_unix FROM allegiance_events WHERE campaign_id=? AND account_id=? ORDER BY revision");
    query.text(1, campaignId); query.text(2, accountId);
    std::vector<Allegiance> result;
    while (query.row()) {
        const auto revision = query.number(0), side = query.number(1), joined = query.number(2), changed = query.number(3);
        if (revision != static_cast<int64_t>(result.size()) || (side != 1 && side != 2) || joined < 0 || changed < joined ||
            (!result.empty() && (joined != result.front().joinedUnix || changed < result.back().changedUnix ||
                                side == static_cast<int>(result.back().alliance))))
            throw std::runtime_error("invalid allegiance history");
        result.push_back({accountId, static_cast<Alliance>(side), joined, changed, revision});
    }
    if ((current && (result.empty() || result.back().revision != current->revision)) || (!current && !result.empty()))
        throw std::runtime_error("allegiance history/current revision mismatch");
    transaction.finish();
    return result;
}

namespace {
void battleString(const std::string& text) {
    requireText(text, "battle binding");
    if (text.size() > 256) throw std::runtime_error("oversized battle binding");
}
void normalizeContext(BattleContext& context) {
    battleString(context.mapIdentifier); battleString(context.mapDigest); battleString(context.rulesDigest);
    if (!context.crusadesBalance || context.participants.size() != 2) throw std::runtime_error("unsupported battle rules/roster");
    for (const auto& account : context.participants) validateAccountId(account);
    std::sort(context.participants.begin(), context.participants.end());
    if (context.participants[0] == context.participants[1]) throw std::runtime_error("duplicate battle participant");
}
std::string randomToken() {
    return crypto::toHex(crypto::randomVec(32));
}

std::string encodeBattle(const IssuedBattle& battle) {
    std::string data = "TAKCB1";
    putString(data, battle.context.mapIdentifier); putString(data, battle.context.mapDigest); putString(data, battle.context.rulesDigest);
    for (size_t i = 0; i < 2; ++i) {
        putString(data, battle.context.participants[i]); put(data, static_cast<unsigned>(battle.participantAlliances[i]), 1);
        put(data, static_cast<uint64_t>(battle.participantRevisions[i]), 8);
    }
    return data;
}
void decodeBattle(const std::string& data, IssuedBattle& battle) {
    if (data.substr(0, 6) != "TAKCB1") throw std::runtime_error("invalid battle context version");
    Reader reader{data, 6};
    battle.context.mapIdentifier = reader.string(); battle.context.mapDigest = reader.string(); battle.context.rulesDigest = reader.string();
    for (unsigned i = 0; i < 2; ++i) {
        battle.context.participants.push_back(reader.string());
        const auto side = reader.get(1), revision = reader.get(8);
        if ((side != 1 && side != 2) || revision > uint64_t(std::numeric_limits<int64_t>::max())) throw std::runtime_error("invalid battle participant snapshot");
        battle.participantAlliances.push_back(static_cast<Alliance>(side)); battle.participantRevisions.push_back(static_cast<int64_t>(revision));
    }
    if (reader.offset != data.size() || battle.context.participants[0] >= battle.context.participants[1] ||
        battle.participantAlliances[0] == battle.participantAlliances[1]) throw std::runtime_error("invalid battle roster");
    normalizeContext(battle.context);
}
void validateContext(const IssuedBattle& battle, BattleContext context) {
    normalizeContext(context);
    if (context.mapIdentifier != battle.context.mapIdentifier || context.mapDigest != battle.context.mapDigest ||
        context.rulesDigest != battle.context.rulesDigest || context.participants != battle.context.participants)
        throw std::runtime_error("battle context mismatch");
}
void validateBattleFresh(sqlite3* db, const CampaignStore& store, const IssuedBattle& battle) {
    const auto campaign = store.load(battle.campaignId);
    if (policyIdentifier(campaign.rules) != battle.policyId) throw std::runtime_error("battle policy mismatch");
    if (campaign.revision != battle.campaignRevision) throw StaleBattleError("stale battle campaign revision");
    for (size_t i = 0; i < 2; ++i) {
        const auto current = readAllegiance(db, battle.campaignId, battle.context.participants[i]);
        if (!current || current->revision != battle.participantRevisions[i] || current->alliance != battle.participantAlliances[i])
            throw StaleBattleError("stale battle allegiance");
    }
}
void transitionBattle(sqlite3* db, const std::string& id, BattleStatus status, int64_t now) {
    Statement event(db, "INSERT INTO battle_status_events(battle_id,revision,status,changed_unix) SELECT id,revision+1,?,? FROM issued_battles WHERE id=?");
    event.integer(1, static_cast<int>(status)); event.integer(2, now); event.text(3, id); event.done();
    Statement update(db, "UPDATE issued_battles SET revision=revision+1 WHERE id=?"); update.text(1, id); update.done();
}
void validateReport(sqlite3* db, const CampaignStore& store, const IssuedBattle& battle,
                    const std::string& room, BattleContext context, int64_t now) {
    battleString(room);
    if (battle.status != BattleStatus::Started || !battle.roomToken || *battle.roomToken != room || now < battle.changedUnix)
        throw std::runtime_error("battle is not reportable from this room");
    validateContext(battle, std::move(context)); validateBattleFresh(db, store, battle);
}
} // namespace

IssuedBattle CampaignStore::issueBattle(const std::string& campaignId, int64_t expectedRevision,
        TerritoryId territory, BattleContext context, int64_t now, int64_t expires) {
    normalizeContext(context);
    if (now < 0 || expires <= now) throw std::runtime_error("invalid battle launch deadline");
    Transaction transaction(impl_->db);
    const auto campaign = load(campaignId);
    if (campaign.revision != expectedRevision) throw std::runtime_error("battle campaign revision conflict");
    const auto* definition = campaign.definition.find(territory);
    if (!definition) throw std::runtime_error("unknown battle territory");
    const auto& stateMap = campaign.state.territories.at(territory).assignedMap;
    const auto& map = stateMap ? stateMap : definition->mapIdentifier;
    if (!map || *map != context.mapIdentifier) throw std::runtime_error("wrong battle map");
    IssuedBattle result{};
    result.policyId = policyIdentifier(campaign.rules);
    result.id = "issued:" + randomToken(); result.launchToken = randomToken();
    result.campaignId = campaignId; result.campaignRevision = campaign.revision; result.territory = territory;
    result.context = std::move(context); result.createdUnix = result.changedUnix = now; result.expiresUnix = expires; result.status = BattleStatus::Issued;
    for (const auto& account : result.context.participants) {
        const auto allegiance = readAllegiance(impl_->db, campaignId, account);
        if (!allegiance) throw std::runtime_error("unenrolled battle participant");
        result.participantAlliances.push_back(allegiance->alliance); result.participantRevisions.push_back(allegiance->revision);
    }
    if (result.participantAlliances[0] == result.participantAlliances[1]) throw std::runtime_error("battle requires opposing alliances");
    Statement insert(impl_->db, "INSERT INTO issued_battles(id,campaign_id,campaign_revision,territory,context,created_unix,expires_unix,launch_token,revision) VALUES(?,?,?,?,?,?,?,?,0)");
    insert.text(1, result.id); insert.text(2, campaignId); insert.integer(3, expectedRevision); insert.integer(4, territory);
    insert.blob(5, encodeBattle(result)); insert.integer(6, now); insert.integer(7, expires); insert.text(8, result.launchToken); insert.done();
    Statement policy(impl_->db, "INSERT INTO issued_battle_rules VALUES(?,?)");
    policy.text(1, result.id); policy.text(2, result.policyId); policy.done();
    Statement event(impl_->db, "INSERT INTO battle_status_events VALUES(?,0,0,?)"); event.text(1, result.id); event.integer(2, now); event.done();
    impl_->finish(transaction); return result;
}

IssuedBattle CampaignStore::battle(const std::string& id) const {
    Statement query(impl_->db, "SELECT b.campaign_id,b.campaign_revision,b.territory,b.context,b.created_unix,b.expires_unix,b.launch_token,e.status,e.changed_unix,r.room_token,p.policy_id FROM issued_battles b JOIN battle_status_events e ON e.battle_id=b.id AND e.revision=b.revision LEFT JOIN battle_rooms r ON r.battle_id=b.id JOIN issued_battle_rules p ON p.battle_id=b.id WHERE b.id=?");
    query.text(1, id);
    if (!query.row()) throw std::runtime_error("unknown or incomplete issued battle");
    IssuedBattle result{}; result.id = id; result.campaignId = query.bytes(0); result.campaignRevision = query.number(1);
    const auto territory = query.number(2), status = query.number(7);
    if (territory <= 0 || territory > std::numeric_limits<TerritoryId>::max() || status < 0 || status > 4) throw std::runtime_error("invalid issued battle");
    result.territory = static_cast<TerritoryId>(territory); decodeBattle(query.bytes(3), result);
    result.createdUnix = query.number(4); result.expiresUnix = query.number(5); result.launchToken = query.bytes(6);
    result.policyId = query.bytes(10); (void)permittedPolicy(result.policyId, impl_->options);
    result.status = static_cast<BattleStatus>(status); result.changedUnix = query.number(8);
    if (sqlite3_column_type(query.value, 9) != SQLITE_NULL) result.roomToken = query.bytes(9);
    if (result.campaignRevision < 0 || result.createdUnix < 0 || result.expiresUnix <= result.createdUnix || result.changedUnix < result.createdUnix ||
        ((result.status == BattleStatus::Started || result.status == BattleStatus::Completed) && !result.roomToken)) throw std::runtime_error("invalid issued battle state");
    return result;
}

void CampaignStore::startBattle(const std::string& id, const std::string& launchToken,
        const std::string& roomToken, BattleContext context, int64_t now) {
    battleString(roomToken);
    Transaction transaction(impl_->db);
    const auto current = battle(id);
    if (current.status != BattleStatus::Issued || current.launchToken != launchToken || now < current.createdUnix || now >= current.expiresUnix)
        throw std::runtime_error("battle cannot be started");
    validateContext(current, std::move(context)); validateBattleFresh(impl_->db, *this, current);
    Statement room(impl_->db, "INSERT INTO battle_rooms(room_token,battle_id) VALUES(?,?)"); room.text(1, roomToken); room.text(2, id); room.done();
    transitionBattle(impl_->db, id, BattleStatus::Started, now); impl_->finish(transaction);
}
IssuedBattle CampaignStore::authorizeBattleReport(const std::string& id, const std::string& roomToken,
        BattleContext context, int64_t now) const {
    Transaction transaction(impl_->db, false);
    auto current = battle(id); validateReport(impl_->db, *this, current, roomToken, std::move(context), now);
    transaction.finish(); return current;
}
void CampaignStore::completeBattle(const std::string&, const std::string&, BattleContext, int64_t) {
    throw std::runtime_error("battle completion requires a verified referee result");
}

void CampaignStore::cancelBattle(const std::string& id, int64_t now) {
    Transaction transaction(impl_->db); const auto current = battle(id);
    if ((current.status != BattleStatus::Issued && current.status != BattleStatus::Started) || now < current.changedUnix)
        throw std::runtime_error("battle cannot be cancelled");
    transitionBattle(impl_->db, id, BattleStatus::Cancelled, now); impl_->finish(transaction);
}
void CampaignStore::expireBattles(int64_t now) {
    if (now < 0) throw std::runtime_error("invalid battle expiry time");
    Transaction transaction(impl_->db);
    std::vector<std::string> ids;
    {
        Statement query(impl_->db, "SELECT b.id FROM issued_battles b JOIN battle_status_events e ON e.battle_id=b.id AND e.revision=b.revision WHERE e.status=0 AND b.expires_unix<=? ORDER BY b.id");
        query.integer(1, now); while (query.row()) ids.push_back(query.bytes(0));
    }
    for (const auto& id : ids) transitionBattle(impl_->db, id, BattleStatus::Expired, now);
    impl_->finish(transaction);
}

namespace {
bool eligibleOutcome(ResultOutcome outcome) {
    return outcome == ResultOutcome::Victory || outcome == ResultOutcome::Resignation;
}
void validateResult(VerifiedMatchResult& result, const IssuedBattle& battle) {
    const int outcome = static_cast<int>(result.outcome);
    if (outcome < 0 || outcome > 9) throw std::runtime_error("invalid verified result outcome");
    battleString(result.engineBuild);
    const bool eligible = eligibleOutcome(result.outcome);
    if ((eligible && (result.winners.size() != 1 || result.finalTick == 0)) || (!eligible && !result.winners.empty()))
        throw std::runtime_error("invalid verified result winner/duration");
    if (result.replayId.empty() != result.replayDigest.empty() || (eligible && result.replayId.empty()))
        throw std::runtime_error("missing verified replay identity");
    if (!result.replayId.empty()) {
        battleString(result.replayId);
        if (result.replayDigest.size() != 64 || !std::all_of(result.replayDigest.begin(), result.replayDigest.end(),
            [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
            throw std::runtime_error("invalid replay SHA256");
    }
    if (result.participantResults.size() != battle.context.participants.size()) throw std::runtime_error("result participant count mismatch");
    std::sort(result.participantResults.begin(), result.participantResults.end(),
        [](const auto& a, const auto& b) { return a.accountId < b.accountId; });
    for (size_t i = 0; i < result.participantResults.size(); ++i) {
        const auto& participant = result.participantResults[i];
        validateAccountId(participant.accountId); battleString(participant.faction);
        if (participant.accountId != battle.context.participants[i] || participant.kills < 0 || participant.losses < 0 ||
            participant.built < 0 || participant.currentUnits < 0 || participant.team < 0)
            throw std::runtime_error("invalid verified participant stats");
    }
    if (eligible) {
        const auto& winner = result.winners.front();
        validateAccountId(winner);
        if (!std::binary_search(battle.context.participants.begin(), battle.context.participants.end(), winner))
            throw std::runtime_error("winner is not a battle participant");
        if (result.participantResults[0].team == result.participantResults[1].team)
            throw std::runtime_error("opposing battle participants share a team");
        for (const auto& participant : result.participantResults)
            if (participant.defeated == (participant.accountId == winner))
                throw std::runtime_error("winner contradicts referee defeat state");
    }
}
void putSigned(std::string& data, int64_t value) {
    uint64_t bits; std::memcpy(&bits, &value, sizeof bits); put(data, bits, 8);
}
int64_t readSigned(Reader& reader) {
    const uint64_t bits = reader.get(8); int64_t value; std::memcpy(&value, &bits, sizeof value); return value;
}
std::string encodeResult(const VerifiedMatchResult& result) {
    std::string data = "TAKCR1";
    put(data, static_cast<unsigned>(result.outcome), 1);
    put(data, result.winners.size(), 1);
    for (const auto& winner : result.winners) putString(data, winner);
    put(data, result.finalTick, 8); put(data, result.finalStateHash, 8); put(data, result.gameplayFingerprint, 8);
    putString(data, result.engineBuild); putString(data, result.replayId); putString(data, result.replayDigest);
    put(data, result.participantResults.size(), 1);
    for (const auto& participant : result.participantResults) {
        putString(data, participant.accountId);
        putSigned(data, participant.kills); putSigned(data, participant.losses); putSigned(data, participant.score);
        putSigned(data, participant.built); putSigned(data, participant.currentUnits);
        putString(data, participant.faction); putSigned(data, participant.team); put(data, participant.defeated ? 1 : 0, 1);
    }
    return data;
}
VerifiedMatchResult decodeResult(const std::string& data, const IssuedBattle& battle) {
    if (data.substr(0, 6) != "TAKCR1") throw std::runtime_error("unsupported verified result encoding");
    Reader reader{data, 6}; VerifiedMatchResult result;
    result.outcome = static_cast<ResultOutcome>(reader.get(1));
    const auto winners = reader.get(1);
    if (winners > 1) throw std::runtime_error("invalid result winner count");
    for (uint64_t n = 0; n < winners; ++n) result.winners.push_back(reader.string());
    result.finalTick = reader.get(8); result.finalStateHash = reader.get(8); result.gameplayFingerprint = reader.get(8);
    result.engineBuild = reader.string(); result.replayId = reader.string(); result.replayDigest = reader.string();
    const auto count = reader.get(1);
    if (count != 2) throw std::runtime_error("invalid result roster count");
    for (uint64_t n = 0; n < count; ++n) {
        ParticipantMatchResult participant;
        participant.accountId = reader.string(); participant.kills = readSigned(reader); participant.losses = readSigned(reader);
        participant.score = readSigned(reader); participant.built = readSigned(reader); participant.currentUnits = readSigned(reader);
        participant.faction = reader.string(); participant.team = readSigned(reader); participant.defeated = reader.present();
        result.participantResults.push_back(std::move(participant));
    }
    if (reader.offset != data.size()) throw std::runtime_error("trailing verified result bytes");
    validateResult(result, battle); return result;
}
} // namespace

void CampaignStore::recordVerifiedResult(const std::string& id, const std::string& roomToken,
        BattleContext context, VerifiedMatchResult result, int64_t now) {
    Transaction transaction(impl_->db);
    const auto current = battle(id);
    battleString(roomToken);
    if (current.status != BattleStatus::Started || !current.roomToken || *current.roomToken != roomToken || now < current.changedUnix)
        throw std::runtime_error("battle cannot accept a verified result from this room");
    validateContext(current, std::move(context));
    validateResult(result, current);
    if (eligibleOutcome(result.outcome)) validateBattleFresh(impl_->db, *this, current);
    const auto campaign = load(current.campaignId);
    if (policyIdentifier(campaign.rules) != current.policyId) throw std::runtime_error("battle policy mismatch");
    RuleBattle input{current.territory, {}};
    if (eligibleOutcome(result.outcome)) {
        const auto winner = std::find(current.context.participants.begin(), current.context.participants.end(), result.winners.front());
        const auto index = static_cast<size_t>(winner - current.context.participants.begin());
        input.winningSide = current.participantAlliances.at(index) == Alliance::Honor ? TerritoryOwner::Honor : TerritoryOwner::Terror;
    }
    const auto decision = evaluateRules(campaign.definition, campaign.state, campaign.rules, input);
    const bool changed = encode(decision.nextState) != encode(campaign.state);
    if (decision.policyId != current.policyId || decision.changed != changed ||
        (changed && (!eligibleOutcome(result.outcome) || campaign.rules.mode != RulesMode::Fixture)))
        throw std::runtime_error("invalid rules decision");
    if (changed && campaign.revision == std::numeric_limits<int64_t>::max()) throw std::runtime_error("campaign revision overflow");
    const int64_t after = campaign.revision + (changed ? 1 : 0);
    Statement insert(impl_->db, "INSERT INTO verified_match_results(battle_id,replay_id,replay_digest,outcome,winner_account,payload,recorded_unix) VALUES(?,?,?,?,?,?,?)");
    insert.text(1, id);
    if (!result.replayId.empty()) { insert.text(2, result.replayId); insert.text(3, result.replayDigest); }
    insert.integer(4, static_cast<int>(result.outcome));
    if (!result.winners.empty()) insert.text(5, result.winners.front());
    insert.blob(6, encodeResult(result)); insert.integer(7, now); insert.done();
    Statement audit(impl_->db, "INSERT INTO rule_decisions VALUES(?,?,?,?,?,?,?,?)");
    audit.text(1,id); audit.text(2,decision.policyId); audit.integer(3,campaign.revision); audit.integer(4,after);
    audit.integer(5,static_cast<int>(decision.disposition)); audit.integer(6,static_cast<int>(decision.evidence));
    audit.text(7,decision.reason); audit.blob(8,encode(decision.nextState)); audit.done();
    if (changed) {
        impl_->event(current.campaignId, after, decision.nextState, "RulesApplied: " + decision.policyId, BattleResult{id,{}});
        Statement update(impl_->db,"UPDATE campaigns SET revision=? WHERE id=?");
        update.integer(1,after); update.text(2,current.campaignId); update.done();
    }
    transitionBattle(impl_->db, id, eligibleOutcome(result.outcome) ? BattleStatus::Completed : BattleStatus::Cancelled, now);
    impl_->finish(transaction);
}

std::optional<VerifiedMatchResult> CampaignStore::verifiedResult(const std::string& id) const {
    Transaction transaction(impl_->db, false);
    const auto current = battle(id);
    Statement query(impl_->db, "SELECT payload,replay_id,replay_digest,outcome,winner_account,recorded_unix FROM verified_match_results WHERE battle_id=?");
    query.text(1, id);
    std::optional<VerifiedMatchResult> result;
    if (query.row()) {
        result = decodeResult(query.bytes(0), current);
        const auto expectedStatus = eligibleOutcome(result->outcome) ? BattleStatus::Completed : BattleStatus::Cancelled;
        if (current.status != expectedStatus || query.bytes(1) != result->replayId || query.bytes(2) != result->replayDigest ||
            query.number(3) != static_cast<int>(result->outcome) || query.bytes(4) != (result->winners.empty() ? "" : result->winners.front()) ||
            query.number(5) != current.changedUnix)
            throw std::runtime_error("verified result metadata/lifecycle mismatch");
    }
    transaction.finish(); return result;
}

std::optional<StoredRulesDecision> CampaignStore::rulesDecision(const std::string& id) const {
    Transaction transaction(impl_->db, false);
    const auto current = battle(id);
    const auto campaign = load(current.campaignId);
    Statement query(impl_->db,"SELECT policy_id,before_revision,after_revision,disposition,evidence,reason,snapshot FROM rule_decisions WHERE battle_id=?");
    query.text(1,id);
    std::optional<StoredRulesDecision> result;
    if (query.row()) {
        const auto before=query.number(1), after=query.number(2), disposition=query.number(3), evidence=query.number(4);
        if (query.bytes(0)!=current.policyId || before<0 || after<before || after-before>1 || disposition<0 || disposition>4 || evidence<0 || evidence>3)
            throw std::runtime_error("invalid stored rules decision");
        result=StoredRulesDecision{RulesDecision{query.bytes(0),static_cast<RulesEvidence>(evidence),static_cast<RulesDisposition>(disposition),query.bytes(5),decode(query.bytes(6),campaign.definition),after!=before},before,after};
        if (current.policyId != policyIdentifier(campaign.rules) || after > campaign.revision)
            throw std::runtime_error("rules decision campaign binding mismatch");
        Statement states(impl_->db,"SELECT snapshot,battle_id FROM campaign_events WHERE campaign_id=? AND revision=?");
        states.text(1,current.campaignId); states.integer(2,after);
        if (!states.row() || states.bytes(0)!=query.bytes(6) || (after!=before && states.bytes(1)!=id))
            throw std::runtime_error("rules decision state history mismatch");
        Statement prior(impl_->db,"SELECT snapshot FROM campaign_events WHERE campaign_id=? AND revision=?");
        prior.text(1,current.campaignId); prior.integer(2,before);
        if (!prior.row()) throw std::runtime_error("rules decision missing prior state");
        Statement verified(impl_->db,"SELECT payload FROM verified_match_results WHERE battle_id=?"); verified.text(1,id);
        if (!verified.row()) throw std::runtime_error("rules decision missing verified result");
        const auto match=decodeResult(verified.bytes(0),current);
        if (current.status != (eligibleOutcome(match.outcome) ? BattleStatus::Completed : BattleStatus::Cancelled))
            throw std::runtime_error("rules decision lifecycle mismatch");
        if (eligibleOutcome(match.outcome) && before!=current.campaignRevision)
            throw std::runtime_error("rules decision issued revision mismatch");
        requireText(result->decision.reason,"rules decision reason");
        RuleBattle input{current.territory,{}};
        if (eligibleOutcome(match.outcome)) {
            const auto winner=std::find(current.context.participants.begin(),current.context.participants.end(),match.winners.front());
            input.winningSide=current.participantAlliances.at(static_cast<size_t>(winner-current.context.participants.begin()))==Alliance::Honor ? TerritoryOwner::Honor : TerritoryOwner::Terror;
        }
        const auto expected=evaluateRules(campaign.definition,decode(prior.bytes(0),campaign.definition),campaign.rules,input);
        if (expected.changed!=(after!=before) || expected.disposition!=result->decision.disposition ||
            expected.evidence!=result->decision.evidence || encode(expected.nextState)!=query.bytes(6))
            throw std::runtime_error("stored rules decision does not match policy");
    }
    transaction.finish(); return result;
}

} // namespace tak::srv::crusades
