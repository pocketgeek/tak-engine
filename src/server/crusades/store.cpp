#include "server/crusades/store.h"
#include <sqlite3.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace tak::srv::crusades {
namespace {
constexpr int kApplicationId = 0x54414b43; // TAKC
constexpr int kSchemaVersion = 1;
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
        if (!(app == kApplicationId && version == kSchemaVersion && !empty) && !(app == 0 && version == 0 && empty))
            throw std::runtime_error("not a supported TAK campaign database");
        if (!empty) {
            std::vector<std::string> actual;
            Statement schema(db, "SELECT sql FROM sqlite_master WHERE name NOT GLOB 'sqlite_*'");
            while (schema.row()) actual.push_back(schema.bytes(0));
            auto expected = schemaStatements();
            std::sort(actual.begin(), actual.end());
            std::sort(expected.begin(), expected.end());
            if (actual != expected) throw std::runtime_error("unsupported or damaged campaign database schema");
        }
        return empty;
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
    if (checkIdentity()) {
        for (const auto& sql : schemaStatements()) exec(db, sql.c_str());
        exec(db, "PRAGMA application_id=1413565251;PRAGMA user_version=1;");
    }
    transaction.finish();
}
CampaignStore::~CampaignStore() = default;

void CampaignStore::create(const CampaignDefinition& definition, const CampaignState& initialState, const std::string& reason) {
    requireText(reason, "campaign event reason");
    validateState(definition, initialState);
    const auto text = definitionText(definition);
    // Also ensures serialized canonical form remains within loader constraints.
    (void)loadDefinitionText(text);
    Transaction transaction(impl_->db);
    Statement insert(impl_->db, "INSERT INTO campaigns(id,definition,revision) VALUES(?,?,0)");
    insert.text(1, definition.id()); insert.text(2, text); insert.done();
    impl_->event(definition.id(), 0, initialState, reason, {});
    impl_->finish(transaction);
}

StoredCampaign CampaignStore::load(const std::string& campaignId) const {
    Statement query(impl_->db, "SELECT c.definition,c.revision,e.snapshot FROM campaigns c JOIN campaign_events e ON e.campaign_id=c.id AND e.revision=c.revision WHERE c.id=?");
    query.text(1, campaignId);
    if (!query.row()) throw std::runtime_error("unknown or incomplete campaign");
    auto definition = loadDefinitionText(query.bytes(0));
    const int64_t revision = query.number(1);
    auto state = decode(query.bytes(2), definition);
    if (definition.id() != campaignId || revision < 0) throw std::runtime_error("invalid stored campaign identity/revision");
    return {std::move(definition), std::move(state), revision};
}

int64_t CampaignStore::commit(const std::string& campaignId, int64_t expectedRevision,
        const CampaignState& nextState, const std::string& reason, const std::optional<BattleResult>& battle) {
    requireText(reason, "campaign event reason");
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

} // namespace tak::srv::crusades
