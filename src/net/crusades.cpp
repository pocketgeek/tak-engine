#include "net/crusades.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <type_traits>

namespace tak::net::crusades {
namespace {
[[noreturn]] void fail(const std::string& why, ErrorCode code = ErrorCode::Malformed) { throw DecodeError(code, why); }
void require(bool condition, const char* why) { if (!condition) fail(why); }
bool utf8(std::string_view text) {
    for (size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i++]);
        if (lead < 0x80) { if (lead < 0x20 || lead == 0x7f) return false; continue; }
        unsigned count; uint32_t value, minimum;
        if (lead >= 0xc2 && lead <= 0xdf) { count = 1; value = lead & 0x1f; minimum = 0x80; }
        else if (lead >= 0xe0 && lead <= 0xef) { count = 2; value = lead & 0xf; minimum = 0x800; }
        else if (lead >= 0xf0 && lead <= 0xf4) { count = 3; value = lead & 7; minimum = 0x10000; }
        else return false;
        while (count--) {
            if (i == text.size()) return false;
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) || (value >= 0x80 && value <= 0x9f)) return false;
    }
    return true;
}
void stringValid(const std::string& text, size_t maximum, bool empty = false) {
    if (text.size() > maximum) fail("campaign string exceeds limit", ErrorCode::TooLarge);
    require((empty || !text.empty()) && utf8(text), "invalid campaign UTF-8 string");
}
void revision(uint64_t value) { require(value <= uint64_t(INT64_MAX), "campaign revision/time exceeds signed 64-bit range"); }
void account(const std::string& id) {
    require(id.size() >= 3 && id.size() <= 20, "invalid winner account length");
    const auto alnum = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    require(alnum(id.front()), "invalid winner account prefix");
    for (char c : id) require(alnum(c) || c == '_' || c == '-' || c == '.', "noncanonical winner account");
}
struct Writer {
    Bytes bytes;
    void number(uint64_t value, size_t width) {
        if (width > kMaxPayload - bytes.size()) fail("campaign payload exceeds limit", ErrorCode::TooLarge);
        for (size_t n = 0; n < width; ++n) { bytes.push_back(uint8_t(value & 255)); value >>= 8; }
    }
    void text(const std::string& value, size_t maximum, bool empty = false) {
        stringValid(value, maximum, empty);
        number(value.size(), 2);
        if (value.size() > kMaxPayload - bytes.size()) fail("campaign payload exceeds limit", ErrorCode::TooLarge);
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
    void optionalText(const std::optional<std::string>& value, size_t maximum) {
        number(value ? 1 : 0, 1); if (value) text(*value, maximum);
    }
    void metric(const std::optional<double>& value) {
        number(value ? 1 : 0, 1);
        if (value) {
            require(std::isfinite(*value), "nonfinite campaign metric");
            static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
            uint64_t bits; std::memcpy(&bits, &*value, 8); number(bits, 8);
        }
    }
};
struct Reader {
    const Bytes& bytes;
    size_t offset = 0;
    explicit Reader(const Bytes& data) : bytes(data) {
        if (bytes.size() > kMaxPayload) fail("campaign payload exceeds limit", ErrorCode::TooLarge);
    }
    uint64_t number(size_t width) {
        require(width <= bytes.size() - offset, "truncated campaign payload");
        uint64_t value = 0;
        for (size_t n = 0; n < width; ++n) value |= uint64_t(bytes[offset++]) << (8*n);
        return value;
    }
    bool flag() { const auto value = number(1); require(value <= 1, "invalid campaign optional flag"); return value != 0; }
    std::string text(size_t maximum, bool empty = false) {
        const auto size = number(2);
        if (size > maximum) fail("campaign string exceeds limit", ErrorCode::TooLarge);
        require(size <= bytes.size() - offset, "truncated campaign string");
        std::string value(bytes.begin() + offset, bytes.begin() + offset + size); offset += size;
        stringValid(value, maximum, empty); return value;
    }
    std::optional<std::string> optionalText(size_t maximum) { return flag() ? std::optional<std::string>(text(maximum)) : std::nullopt; }
    std::optional<double> metric() {
        if (!flag()) return {};
        const uint64_t bits = number(8); double value; std::memcpy(&value, &bits, 8);
        require(std::isfinite(value), "nonfinite campaign metric"); return value;
    }
    size_t count(size_t width, size_t maximum) {
        const auto value = number(width);
        if (value > maximum) fail("campaign collection exceeds limit", ErrorCode::TooLarge);
        return static_cast<size_t>(value);
    }
    void done() { require(offset == bytes.size(), "trailing campaign payload bytes"); }
};
void prefix(Writer& writer, uint32_t id) { writer.number(kVersion, 2); writer.number(id, 4); }
uint32_t prefix(Reader& reader) {
    if (reader.number(2) != kVersion) fail("unsupported campaign payload version", ErrorCode::UnsupportedVersion);
    return uint32_t(reader.number(4));
}
void metrics(Writer& writer, const ReconMetrics& m) {
    for (const auto* field : {&m.fatigueVictoryPoints, &m.honorRequiredVictoryPoints, &m.honorSupportVictoryPoints,
        &m.honorBattleVictoryPoints, &m.terrorRequiredVictoryPoints, &m.terrorSupportVictoryPoints, &m.terrorBattleVictoryPoints}) writer.metric(*field);
}
ReconMetrics metrics(Reader& reader) {
    ReconMetrics m;
    for (auto* field : {&m.fatigueVictoryPoints, &m.honorRequiredVictoryPoints, &m.honorSupportVictoryPoints,
        &m.honorBattleVictoryPoints, &m.terrorRequiredVictoryPoints, &m.terrorSupportVictoryPoints, &m.terrorBattleVictoryPoints}) *field = reader.metric();
    return m;
}
void phase(BattlePhase value) { require(static_cast<unsigned>(value) <= 4, "invalid battle phase"); }
void normalize(Snapshot& snapshot) {
    stringValid(snapshot.campaignId, kMaxIdentifier); stringValid(snapshot.displayName, kMaxDisplayName);
    stringValid(snapshot.rulesPolicy, kMaxIdentifier); revision(snapshot.revision);
    require(!snapshot.territories.empty(), "snapshot has no territories");
    if (snapshot.territories.size() > kMaxTerritories) fail("too many campaign territories", ErrorCode::TooLarge);
    std::sort(snapshot.territories.begin(), snapshot.territories.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    uint32_t previous = 0;
    for (auto& territory : snapshot.territories) {
        require(territory.id != 0 && territory.id != previous, "duplicate or zero territory ID"); previous = territory.id;
        stringValid(territory.displayName, kMaxDisplayName);
        for (const auto* value : {&territory.nativeFaction, &territory.terrain}) if (*value) stringValid(**value, kMaxDisplayName);
        for (const auto* value : {&territory.mapIdentifier, &territory.assignedMap}) if (*value) stringValid(**value, kMaxMapIdentifier);
        if (territory.owner) require(static_cast<unsigned>(*territory.owner) >= 1 && static_cast<unsigned>(*territory.owner) <= 3, "invalid territory owner");
        for (const auto* field : {&territory.recon.fatigueVictoryPoints, &territory.recon.honorRequiredVictoryPoints, &territory.recon.honorSupportVictoryPoints,
            &territory.recon.honorBattleVictoryPoints, &territory.recon.terrorRequiredVictoryPoints, &territory.recon.terrorSupportVictoryPoints, &territory.recon.terrorBattleVictoryPoints})
            if (*field) require(std::isfinite(**field), "nonfinite campaign metric");
        if (territory.neighbors) {
            if (territory.neighbors->size() > kMaxNeighbors) fail("too many territory neighbors", ErrorCode::TooLarge);
            std::sort(territory.neighbors->begin(), territory.neighbors->end());
            require(std::adjacent_find(territory.neighbors->begin(), territory.neighbors->end()) == territory.neighbors->end(), "duplicate territory neighbor");
        }
    }
    for (const auto& territory : snapshot.territories) if (territory.neighbors) for (uint32_t neighbor : *territory.neighbors) {
        require(neighbor != territory.id, "self adjacency");
        const auto other = std::lower_bound(snapshot.territories.begin(), snapshot.territories.end(), neighbor,
            [](const auto& value, uint32_t id) { return value.id < id; });
        require(other != snapshot.territories.end() && other->id == neighbor, "unknown adjacency territory");
        require(other->neighbors && std::binary_search(other->neighbors->begin(), other->neighbors->end(), territory.id), "asymmetric territory adjacency");
    }
}
void write(Writer& writer, const CampaignList& list) {
    if (list.entries.size() > kMaxCampaigns) fail("too many campaign entries", ErrorCode::TooLarge);
    writer.number(list.entries.size(), 2);
    std::string previous;
    for (const auto& entry : list.entries) {
        require(previous.empty() || previous < entry.id, "campaign page not uniquely ordered"); previous = entry.id;
        writer.text(entry.id, kMaxIdentifier); writer.text(entry.displayName, kMaxDisplayName);
        revision(entry.revision); writer.number(entry.revision, 8); writer.text(entry.rulesPolicy, kMaxIdentifier);
    }
    require(list.nextCursor.empty() || (!list.entries.empty() && list.nextCursor == list.entries.back().id), "invalid campaign pagination cursor");
    writer.text(list.nextCursor, kMaxIdentifier, true);
}
void write(Writer& writer, Snapshot snapshot) {
    normalize(snapshot);
    writer.text(snapshot.campaignId, kMaxIdentifier); writer.text(snapshot.displayName, kMaxDisplayName);
    writer.number(snapshot.revision, 8); writer.text(snapshot.rulesPolicy, kMaxIdentifier); writer.number(snapshot.territories.size(), 2);
    for (const auto& t : snapshot.territories) {
        writer.number(t.id, 4); writer.text(t.displayName, kMaxDisplayName);
        writer.optionalText(t.nativeFaction, kMaxDisplayName); writer.optionalText(t.terrain, kMaxDisplayName); writer.optionalText(t.mapIdentifier, kMaxMapIdentifier);
        writer.number(t.neighbors ? 1 : 0, 1);
        if (t.neighbors) { writer.number(t.neighbors->size(), 2); for (auto id : *t.neighbors) writer.number(id, 4); }
        writer.number(t.owner ? static_cast<unsigned>(*t.owner) : 0, 1);
        writer.optionalText(t.assignedMap, kMaxMapIdentifier); metrics(writer, t.recon);
    }
}
void write(Writer& writer, const PlayerStatus& status) {
    writer.text(status.campaignId, kMaxIdentifier); revision(status.campaignRevision); writer.number(status.campaignRevision, 8);
    writer.number(status.allegiance ? 1 : 0, 1);
    if (status.allegiance) {
        const auto& a = *status.allegiance;
        require(a.alliance == Alliance::Honor || a.alliance == Alliance::Terror, "invalid player alliance");
        revision(a.revision); revision(a.joinedUnix); revision(a.changedUnix); require(a.changedUnix >= a.joinedUnix, "invalid allegiance times");
        writer.number(static_cast<unsigned>(a.alliance), 1); writer.number(a.revision, 8); writer.number(a.joinedUnix, 8); writer.number(a.changedUnix, 8);
    }
    if (status.battles.size() > kMaxRecentBattles) fail("too many recent battles", ErrorCode::TooLarge);
    writer.number(status.battles.size(), 1); std::set<std::string> ids;
    for (const auto& battle : status.battles) {
        require(ids.insert(battle.id).second && battle.territory != 0, "duplicate battle reference or zero territory"); phase(battle.status);
        writer.text(battle.id, kMaxIdentifier); writer.number(battle.territory, 4); writer.number(static_cast<unsigned>(battle.status), 1);
    }
    require(!status.battlesTruncated || !status.battles.empty(), "truncated empty battle list");
    writer.number(status.battlesTruncated ? 1 : 0, 1);
}
void write(Writer& writer, const BattleStatus& status) {
    writer.text(status.campaignId, kMaxIdentifier); writer.text(status.battleId, kMaxIdentifier);
    revision(status.campaignRevision); writer.number(status.campaignRevision, 8);
    require(status.territory != 0, "zero battle territory"); writer.number(status.territory, 4);
    phase(status.status); writer.number(static_cast<unsigned>(status.status), 1); writer.text(status.mapIdentifier, kMaxMapIdentifier);
    revision(status.expiresUnix); writer.number(status.expiresUnix, 8); writer.number(status.roomId, 4);
    require(status.roomId == 0 || status.status == BattlePhase::Issued || status.status == BattlePhase::Started,
        "terminal battle exposes a live room");
    writer.number(status.result ? 1 : 0, 1);
    if (status.result) {
        const auto& r = *status.result;
        require(static_cast<unsigned>(r.outcome) <= 9, "invalid battle outcome");
        const bool eligible = r.outcome == Outcome::Victory || r.outcome == Outcome::Resignation;
        require((eligible && r.winners.size() == 1 && r.finalTick > 0 && status.status == BattlePhase::Completed) ||
            (!eligible && r.winners.empty() && status.status == BattlePhase::Cancelled), "invalid battle result lifecycle/winners");
        writer.number(static_cast<unsigned>(r.outcome), 1); writer.number(r.finalTick, 8); writer.number(r.finalStateHash, 8); writer.number(r.winners.size(), 1);
        for (const auto& winner : r.winners) { account(winner); writer.text(winner, 20); }
    }
}
void write(Writer& writer, const Error& error) {
    require(static_cast<unsigned>(error.code) >= 1 && static_cast<unsigned>(error.code) <= 8, "invalid campaign error code");
    writer.number(static_cast<unsigned>(error.code), 1); writer.text(error.campaignId, kMaxIdentifier, true);
    writer.number(error.currentRevision ? 1 : 0, 1);
    if (error.currentRevision) { revision(*error.currentRevision); writer.number(*error.currentRevision, 8); }
    writer.text(error.reason, kMaxReason);
}
} // namespace

RequestKind kindOf(const Request& request) { return static_cast<RequestKind>(request.index()); }
ResponseKind kindOf(const Response& response) { return static_cast<ResponseKind>(response.index()); }
Bytes encode(const Request& request) {
    Writer writer;
    std::visit([&](const auto& message) {
        require(message.requestId != 0, "request ID zero is reserved for notifications"); prefix(writer, message.requestId);
        using T = std::decay_t<decltype(message)>;
        if constexpr (std::is_same_v<T, ListRequest>) {
            require(message.limit >= 1 && message.limit <= kMaxCampaigns, "invalid campaign page limit");
            writer.text(message.afterCampaignId, kMaxIdentifier, true); writer.number(message.limit, 2);
        } else if constexpr (std::is_same_v<T, SnapshotRequest>) {
            writer.text(message.campaignId, kMaxIdentifier);
            if (message.expectedRevision != kUnknownRevision) revision(message.expectedRevision);
            writer.number(message.expectedRevision, 8);
        } else if constexpr (std::is_same_v<T, PlayerStatusRequest>) writer.text(message.campaignId, kMaxIdentifier);
        else writer.text(message.battleId, kMaxIdentifier);
    }, request);
    return std::move(writer.bytes);
}
Bytes encode(const Response& response) {
    Writer writer;
    std::visit([&](const auto& message) { prefix(writer, message.requestId); write(writer, message); }, response);
    return std::move(writer.bytes);
}
Request decodeRequest(RequestKind kind, const Bytes& payload) {
    Reader reader(payload); const auto id = prefix(reader);
    require(id != 0, "request ID zero is reserved for notifications");
    Request result;
    switch (kind) {
    case RequestKind::List: { ListRequest m; m.requestId = id; m.afterCampaignId = reader.text(kMaxIdentifier, true); m.limit = uint16_t(reader.number(2)); result = m; break; }
    case RequestKind::Snapshot: { SnapshotRequest m; m.requestId = id; m.campaignId = reader.text(kMaxIdentifier); m.expectedRevision = reader.number(8); result = m; break; }
    case RequestKind::PlayerStatus: { PlayerStatusRequest m; m.requestId = id; m.campaignId = reader.text(kMaxIdentifier); result = m; break; }
    case RequestKind::BattleStatus: { BattleStatusRequest m; m.requestId = id; m.battleId = reader.text(kMaxIdentifier); result = m; break; }
    default: fail("unknown campaign request kind");
    }
    reader.done(); (void)encode(result); return result;
}
Response decodeResponse(ResponseKind kind, const Bytes& payload) {
    Reader reader(payload); const auto id = prefix(reader); Response result;
    switch (kind) {
    case ResponseKind::List: {
        CampaignList m; m.requestId = id;
        const auto count = reader.count(2, kMaxCampaigns);
        for (size_t i = 0; i < count; ++i) {
            CampaignEntry entry; entry.id = reader.text(kMaxIdentifier); entry.displayName = reader.text(kMaxDisplayName);
            entry.revision = reader.number(8); entry.rulesPolicy = reader.text(kMaxIdentifier); m.entries.push_back(std::move(entry));
        }
        m.nextCursor = reader.text(kMaxIdentifier, true); result = std::move(m); break;
    }
    case ResponseKind::Snapshot: {
        Snapshot m; m.requestId = id; m.campaignId = reader.text(kMaxIdentifier); m.displayName = reader.text(kMaxDisplayName);
        m.revision = reader.number(8); m.rulesPolicy = reader.text(kMaxIdentifier);
        const auto count = reader.count(2, kMaxTerritories);
        for (size_t i = 0; i < count; ++i) {
            Territory t; t.id = uint32_t(reader.number(4)); t.displayName = reader.text(kMaxDisplayName);
            t.nativeFaction = reader.optionalText(kMaxDisplayName); t.terrain = reader.optionalText(kMaxDisplayName); t.mapIdentifier = reader.optionalText(kMaxMapIdentifier);
            if (reader.flag()) { t.neighbors.emplace(); const auto n = reader.count(2, kMaxNeighbors); for (size_t j = 0; j < n; ++j) t.neighbors->push_back(uint32_t(reader.number(4))); }
            const auto owner = reader.number(1); if (owner) t.owner = static_cast<Owner>(owner);
            t.assignedMap = reader.optionalText(kMaxMapIdentifier); t.recon = metrics(reader); m.territories.push_back(std::move(t));
        }
        normalize(m); result = std::move(m); break;
    }
    case ResponseKind::PlayerStatus: {
        PlayerStatus m; m.requestId = id; m.campaignId = reader.text(kMaxIdentifier); m.campaignRevision = reader.number(8);
        if (reader.flag()) { PlayerAllegiance a; a.alliance = static_cast<Alliance>(reader.number(1)); a.revision = reader.number(8); a.joinedUnix = reader.number(8); a.changedUnix = reader.number(8); m.allegiance = a; }
        const auto count = reader.count(1, kMaxRecentBattles);
        for (size_t i = 0; i < count; ++i) { BattleReference b; b.id = reader.text(kMaxIdentifier); b.territory = uint32_t(reader.number(4)); b.status = static_cast<BattlePhase>(reader.number(1)); m.battles.push_back(std::move(b)); }
        m.battlesTruncated = reader.flag(); result = std::move(m); break;
    }
    case ResponseKind::BattleStatus: {
        BattleStatus m; m.requestId = id; m.campaignId = reader.text(kMaxIdentifier); m.battleId = reader.text(kMaxIdentifier);
        m.campaignRevision = reader.number(8); m.territory = uint32_t(reader.number(4)); m.status = static_cast<BattlePhase>(reader.number(1));
        m.mapIdentifier = reader.text(kMaxMapIdentifier); m.expiresUnix = reader.number(8); m.roomId = uint32_t(reader.number(4));
        if (reader.flag()) {
            BattleResult r; r.outcome = static_cast<Outcome>(reader.number(1)); r.finalTick = reader.number(8); r.finalStateHash = reader.number(8);
            const auto count = reader.count(1, 1); for (size_t i = 0; i < count; ++i) r.winners.push_back(reader.text(20)); m.result = std::move(r);
        }
        result = std::move(m); break;
    }
    case ResponseKind::Error: {
        Error m; m.requestId = id; m.code = static_cast<ErrorCode>(reader.number(1)); m.campaignId = reader.text(kMaxIdentifier, true);
        if (reader.flag()) m.currentRevision = reader.number(8);
        m.reason = reader.text(kMaxReason); result = std::move(m); break;
    }
    default: fail("unknown campaign response kind");
    }
    reader.done(); (void)encode(result); return result;
}
Snapshot decodeSnapshot(const Bytes& payload) { return std::get<Snapshot>(decodeResponse(ResponseKind::Snapshot, payload)); }
ApplyResult Replica::apply(const Bytes& payload) { return apply(decodeSnapshot(payload)); }
ApplyResult Replica::apply(Snapshot snapshot) {
    normalize(snapshot); (void)encode(Response{snapshot});
    const auto found = snapshots_.find(snapshot.campaignId);
    if (found == snapshots_.end()) {
        if (snapshots_.size() >= kMaxCampaigns) fail("campaign replica cache is full", ErrorCode::TooLarge);
        snapshots_.emplace(snapshot.campaignId, std::move(snapshot)); return ApplyResult::Inserted;
    }
    if (snapshot.revision < found->second.revision) return ApplyResult::Stale;
    if (snapshot.revision == found->second.revision) {
        auto before = found->second; before.requestId = 0; snapshot.requestId = 0;
        return encode(Response{before}) == encode(Response{snapshot}) ? ApplyResult::Unchanged : ApplyResult::Conflict;
    }
    found->second = std::move(snapshot); return ApplyResult::Updated;
}
const Snapshot* Replica::find(const std::string& id) const { const auto it = snapshots_.find(id); return it == snapshots_.end() ? nullptr : &it->second; }
} // namespace tak::net::crusades
