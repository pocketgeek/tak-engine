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
        if (territory.activity) require(territory.activity->offered <= 1000000 && territory.activity->active <= 1000000, "invalid battle activity counts");
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
        writer.number(t.activity ? 1 : 0,1);
        if (t.activity) {writer.number(t.activity->offered,4);writer.number(t.activity->active,4);}
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
void write(Writer& writer, const MatchmakingStatus& status) {
    writer.text(status.campaignId, kMaxIdentifier);
    revision(status.campaignRevision); revision(status.generation);
    writer.number(status.campaignRevision, 8); writer.number(status.generation, 8);
    writer.number(status.canSearch ? 1 : 0, 1);
    require(status.searchingTerritory.has_value() == status.searchExpiresUnix.has_value(), "incomplete own match search");
    writer.number(status.searchingTerritory ? 1 : 0, 1);
    if (status.searchingTerritory) {
        require(status.canSearch, "own match search is not searchable");
        require(*status.searchingTerritory != 0 && *status.searchExpiresUnix > 0, "invalid own match search identity/time");
        revision(*status.searchExpiresUnix);
        writer.number(*status.searchingTerritory, 4); writer.number(*status.searchExpiresUnix, 8);
    }
    if (status.territories.size() > kMaxTerritories) fail("too many matchmaking territories", ErrorCode::TooLarge);
    writer.number(status.territories.size(), 2);
    uint32_t previous = 0; bool foundSearch = !status.searchingTerritory;
    for (const auto& t : status.territories) {
        require(t.id != 0 && t.id > previous, "unordered or duplicate matchmaking territory"); previous = t.id;
        if (status.searchingTerritory && t.id == *status.searchingTerritory) {
            require(t.eligible && (t.waitingHonor || t.waitingTerror), "own match search has no eligible waiting territory");
            foundSearch = true;
        }
        require(t.waitingHonor <= 1000000 && t.waitingTerror <= 1000000 && t.offered <= 1000000 && t.active <= 1000000,
            "invalid matchmaking activity counts");
        writer.number(t.id, 4); writer.number(t.eligible ? 1 : 0, 1);
        writer.number(t.waitingHonor, 4); writer.number(t.waitingTerror, 4); writer.number(t.offered, 4); writer.number(t.active, 4);
    }
    require(foundSearch, "own search references missing matchmaking territory");
}
void digest(const std::string& value, bool empty = false) {
    if (empty && value.empty()) return;
    require(value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }), "invalid replay digest");
}
void cursor(Writer& w, const std::optional<HistoryCursor>& c) {
    w.number(c ? 1 : 0, 1);
    if (c) { revision(c->recordedUnix); require(c->recordedUnix > 0, "zero history cursor time"); w.number(c->recordedUnix, 8); w.text(c->battleId, kMaxIdentifier); }
}
std::optional<HistoryCursor> cursor(Reader& r) {
    if (!r.flag()) return {};
    HistoryCursor c; c.recordedUnix = r.number(8); c.battleId = r.text(kMaxIdentifier); return c;
}
void write(Writer& w, const TerritoryHistory& history) {
    require(history.requestId != 0 && history.territory != 0, "invalid history identity");
    w.text(history.campaignId, kMaxIdentifier); w.number(history.territory, 4);
    require(history.entries.size() <= kMaxHistory, "too many history records"); w.number(history.entries.size(), 2);
    std::set<std::string> ids; uint64_t previousTime = UINT64_MAX; std::string previousId;
    for (const auto& b : history.entries) {
        require(b.territory == history.territory && ids.insert(b.battleId).second, "invalid history record territory/ID");
        revision(b.recordedUnix); revision(b.campaignRevision);
        require(b.recordedUnix > 0 && (b.recordedUnix < previousTime || (b.recordedUnix == previousTime && b.battleId < previousId)), "unordered history records");
        previousTime = b.recordedUnix; previousId = b.battleId;
        w.text(b.battleId, kMaxIdentifier); w.number(b.territory, 4); w.number(b.campaignRevision, 8); w.number(b.recordedUnix, 8); w.text(b.mapIdentifier, kMaxMapIdentifier);
        const auto& result = b.result; require(static_cast<unsigned>(result.outcome) <= 9, "invalid archive outcome");
        const bool victory = result.outcome == Outcome::Victory || result.outcome == Outcome::Resignation;
        require(victory ? (result.winners.size() == 1 && result.finalTick > 0) : result.winners.empty(), "invalid archive winners/tick");
        w.number(static_cast<unsigned>(result.outcome), 1); w.number(result.finalTick, 8); w.number(result.finalStateHash, 8); w.number(result.winners.size(), 1);
        for (const auto& id : result.winners) { account(id); w.text(id, 20); }
        require(!b.participants.empty() && b.participants.size() <= 8, "invalid archive participant count");
        w.number(b.participants.size(), 1); std::set<std::string> participants;
        for (const auto& p : b.participants) {
            account(p.accountId); require(participants.insert(p.accountId).second, "duplicate archive participant"); w.text(p.accountId, 20);
            for (auto n : {p.kills,p.losses}) { revision(n); w.number(n, 8); }
            w.number(static_cast<uint64_t>(p.score), 8);
            for (auto n : {p.built,p.currentUnits}) { revision(n); w.number(n, 8); }
            w.text(p.faction, kMaxIdentifier); require(p.team <= 7, "invalid archive team"); w.number(p.team, 1); w.number(p.defeated ? 1 : 0, 1);
        }
        for (const auto& id : result.winners) require(participants.count(id), "archive winner is not a participant");
        w.number(b.replay ? 1 : 0, 1);
        if (b.replay) {
            const auto& m = *b.replay; digest(m.digest); digest(m.mapDigest, true);
            require(m.totalBytes > 0 && m.totalBytes <= kMaxReplayBytes && m.format > 0 && m.protocolVersion > 0, "invalid replay metadata");
            w.text(m.digest, 64); w.number(m.totalBytes, 8); w.number(m.format, 4); w.number(m.protocolVersion, 4); w.text(m.mapDigest, 64, true); w.number(m.gameplayFingerprint, 8);
        }
    }
    if (history.nextCursor) require(!history.entries.empty() && history.nextCursor->recordedUnix == history.entries.back().recordedUnix && history.nextCursor->battleId == history.entries.back().battleId, "invalid history next cursor");
    cursor(w, history.nextCursor);
}
void write(Writer& w, const ReplayChunk& chunk) {
    require(chunk.requestId != 0, "replay chunk cannot notify"); digest(chunk.digest);
    require(chunk.totalBytes > 0 && chunk.totalBytes <= kMaxReplayBytes && chunk.offset < chunk.totalBytes &&
        !chunk.bytes.empty() && chunk.bytes.size() <= kReplayChunkBytes && chunk.bytes.size() <= chunk.totalBytes - chunk.offset &&
        chunk.final == (chunk.offset + chunk.bytes.size() == chunk.totalBytes), "invalid replay chunk bounds/final flag");
    w.text(chunk.battleId, kMaxIdentifier); w.text(chunk.digest, 64); w.number(chunk.totalBytes, 8); w.number(chunk.offset, 8);
    w.number(chunk.final ? 1 : 0, 1); w.number(chunk.bytes.size(), 4);
    w.bytes.insert(w.bytes.end(), chunk.bytes.begin(), chunk.bytes.end());
}
void write(Writer& writer, const Error& error) {
    require(static_cast<unsigned>(error.code) >= 1 && static_cast<unsigned>(error.code) <= 9, "invalid campaign error code");
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
        } else if constexpr (std::is_same_v<T, TerritoryHistoryRequest>) {
            writer.text(message.campaignId, kMaxIdentifier); require(message.territory != 0, "zero history territory"); writer.number(message.territory, 4);
            cursor(writer, message.cursor); require(message.limit > 0 && message.limit <= kMaxHistory, "invalid history page limit"); writer.number(message.limit, 2);
        } else if constexpr (std::is_same_v<T, ReplayChunkRequest>) {
            writer.text(message.battleId, kMaxIdentifier); require(message.offset < kMaxReplayBytes && message.limit > 0 && message.limit <= kReplayChunkBytes, "invalid replay request bounds");
            writer.number(message.offset, 8); writer.number(message.limit, 4);
        } else if constexpr (std::is_same_v<T, BattleStatusRequest>) writer.text(message.battleId, kMaxIdentifier);
        else {
            writer.text(message.campaignId, kMaxIdentifier);
            if constexpr (std::is_same_v<T, MatchSearchRequest>) {
                require(message.territory != 0, "zero match search territory"); writer.number(message.territory, 4);
            }
        }
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
    case RequestKind::Matchmaking: { MatchmakingRequest m; m.requestId = id; m.campaignId = reader.text(kMaxIdentifier); result = m; break; }
    case RequestKind::MatchSearch: { MatchSearchRequest m; m.requestId = id; m.campaignId = reader.text(kMaxIdentifier); m.territory = uint32_t(reader.number(4)); result = m; break; }
    case RequestKind::MatchCancel: { MatchCancelRequest m; m.requestId = id; m.campaignId = reader.text(kMaxIdentifier); result = m; break; }
    case RequestKind::TerritoryHistory: { TerritoryHistoryRequest m; m.requestId=id; m.campaignId=reader.text(kMaxIdentifier); m.territory=uint32_t(reader.number(4)); m.cursor=cursor(reader); m.limit=uint16_t(reader.number(2)); result=std::move(m); break; }
    case RequestKind::ReplayChunk: { ReplayChunkRequest m; m.requestId=id; m.battleId=reader.text(kMaxIdentifier); m.offset=reader.number(8); m.limit=uint32_t(reader.number(4)); result=std::move(m); break; }
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
            t.assignedMap = reader.optionalText(kMaxMapIdentifier); t.recon = metrics(reader);
            if (reader.flag()) t.activity=BattleActivity{uint32_t(reader.number(4)),uint32_t(reader.number(4))};
            m.territories.push_back(std::move(t));
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
    case ResponseKind::Matchmaking: {
        MatchmakingStatus m; m.requestId = id; m.campaignId = reader.text(kMaxIdentifier);
        m.campaignRevision = reader.number(8); m.generation = reader.number(8); m.canSearch = reader.flag();
        if (reader.flag()) { m.searchingTerritory = uint32_t(reader.number(4)); m.searchExpiresUnix = reader.number(8); }
        const auto count = reader.count(2, kMaxTerritories);
        for (size_t i = 0; i < count; ++i) {
            MatchTerritory t; t.id = uint32_t(reader.number(4)); t.eligible = reader.flag();
            t.waitingHonor = uint32_t(reader.number(4)); t.waitingTerror = uint32_t(reader.number(4));
            t.offered = uint32_t(reader.number(4)); t.active = uint32_t(reader.number(4)); m.territories.push_back(t);
        }
        result = std::move(m); break;
    }
    case ResponseKind::TerritoryHistory: {
        TerritoryHistory m; m.requestId=id; m.campaignId=reader.text(kMaxIdentifier); m.territory=uint32_t(reader.number(4));
        const auto count=reader.count(2,kMaxHistory);
        for(size_t i=0;i<count;++i) {
            HistoryBattle b; b.battleId=reader.text(kMaxIdentifier); b.territory=uint32_t(reader.number(4)); b.campaignRevision=reader.number(8); b.recordedUnix=reader.number(8); b.mapIdentifier=reader.text(kMaxMapIdentifier);
            b.result.outcome=static_cast<Outcome>(reader.number(1)); b.result.finalTick=reader.number(8); b.result.finalStateHash=reader.number(8);
            const auto winners=reader.count(1,1); for(size_t j=0;j<winners;++j)b.result.winners.push_back(reader.text(20));
            const auto participants=reader.count(1,8);
            for(size_t j=0;j<participants;++j) {
                HistoryParticipant p; p.accountId=reader.text(20); p.kills=reader.number(8); p.losses=reader.number(8); {const auto bits=reader.number(8); std::memcpy(&p.score,&bits,8);} p.built=reader.number(8); p.currentUnits=reader.number(8);
                p.faction=reader.text(kMaxIdentifier); p.team=uint8_t(reader.number(1)); p.defeated=reader.flag(); b.participants.push_back(std::move(p));
            }
            if(reader.flag()) { ReplayMetadata r; r.digest=reader.text(64); r.totalBytes=reader.number(8); r.format=uint32_t(reader.number(4)); r.protocolVersion=uint32_t(reader.number(4)); r.mapDigest=reader.text(64,true); r.gameplayFingerprint=reader.number(8); b.replay=std::move(r); }
            m.entries.push_back(std::move(b));
        }
        m.nextCursor=cursor(reader); result=std::move(m); break;
    }
    case ResponseKind::ReplayChunk: {
        ReplayChunk m; m.requestId=id; m.battleId=reader.text(kMaxIdentifier); m.digest=reader.text(64); m.totalBytes=reader.number(8); m.offset=reader.number(8); m.final=reader.flag();
        const auto count=reader.count(4,kReplayChunkBytes); require(count<=reader.bytes.size()-reader.offset,"truncated replay chunk bytes");
        m.bytes.assign(reader.bytes.begin()+reader.offset,reader.bytes.begin()+reader.offset+count);reader.offset+=count; result=std::move(m); break;
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
        if (encode(Response{before}) == encode(Response{snapshot})) return ApplyResult::Unchanged;
        auto withoutActivity= snapshot;
        for (auto& territory:before.territories) territory.activity.reset();
        for (auto& territory:withoutActivity.territories) territory.activity.reset();
        if (encode(Response{before}) != encode(Response{withoutActivity})) return ApplyResult::Conflict;
        // Runtime counts are outside the persisted campaign revision. Transport
        // preserves response order; accept only this ephemeral field changing.
        found->second=std::move(snapshot); return ApplyResult::Updated;
    }
    found->second = std::move(snapshot); return ApplyResult::Updated;
}
const Snapshot* Replica::find(const std::string& id) const { const auto it = snapshots_.find(id); return it == snapshots_.end() ? nullptr : &it->second; }
} // namespace tak::net::crusades
