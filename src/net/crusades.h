#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

// Engine-owned campaign protocol. No tactical World, command/bundle, store,
// credential, retail wire format or original campaign assets are involved.
namespace tak::net::crusades {
using Bytes = std::vector<uint8_t>;
constexpr uint16_t kVersion = 2;
constexpr uint64_t kUnknownRevision = UINT64_MAX;
constexpr size_t kMaxPayload = 240 * 1024;
constexpr size_t kMaxCampaigns = 64;
constexpr size_t kMaxTerritories = 1024;
constexpr size_t kMaxNeighbors = 256;
constexpr size_t kMaxRecentBattles = 32;
constexpr size_t kMaxIdentifier = 128;
constexpr size_t kMaxMapIdentifier = 4096;
constexpr size_t kMaxDisplayName = 1024;
constexpr size_t kMaxReason = 512;

enum class ErrorCode : uint8_t {
    Malformed = 1, UnsupportedVersion = 2, AuthenticationRequired = 3,
    Disabled = 4, NotFound = 5, StaleRevision = 6, TooLarge = 7, Unavailable = 8
};
class DecodeError : public std::runtime_error {
public:
    DecodeError(ErrorCode code, const std::string& message) : std::runtime_error(message), code(code) {}
    ErrorCode code;
};
struct ListRequest { uint32_t requestId = 0; std::string afterCampaignId; uint16_t limit = 64; };
struct SnapshotRequest { uint32_t requestId = 0; std::string campaignId; uint64_t expectedRevision = kUnknownRevision; };
struct PlayerStatusRequest { uint32_t requestId = 0; std::string campaignId; };
struct BattleStatusRequest { uint32_t requestId = 0; std::string battleId; };
using Request = std::variant<ListRequest, SnapshotRequest, PlayerStatusRequest, BattleStatusRequest>;

struct CampaignEntry { std::string id, displayName; uint64_t revision = 0; std::string rulesPolicy; };
struct CampaignList { uint32_t requestId = 0; std::vector<CampaignEntry> entries; std::string nextCursor; };
enum class Owner : uint8_t { Contested = 1, Honor = 2, Terror = 3 };
enum class Alliance : uint8_t { Honor = 1, Terror = 2 };
struct ReconMetrics {
    std::optional<double> fatigueVictoryPoints;
    std::optional<double> honorRequiredVictoryPoints, honorSupportVictoryPoints, honorBattleVictoryPoints;
    std::optional<double> terrorRequiredVictoryPoints, terrorSupportVictoryPoints, terrorBattleVictoryPoints;
};
struct BattleActivity { uint32_t offered = 0, active = 0; };
struct Territory {
    uint32_t id = 0;
    std::string displayName;
    std::optional<std::string> nativeFaction, terrain, mapIdentifier;
    std::optional<std::vector<uint32_t>> neighbors;
    std::optional<Owner> owner;
    std::optional<std::string> assignedMap;
    ReconMetrics recon;
    // Modern live server room counts, not historical traffic or stored rules.
    std::optional<BattleActivity> activity;
};
struct Snapshot {
    uint32_t requestId = 0;
    std::string campaignId, displayName;
    uint64_t revision = 0;
    std::string rulesPolicy;
    std::vector<Territory> territories;
};
struct PlayerAllegiance {
    Alliance alliance = Alliance::Honor;
    uint64_t revision = 0, joinedUnix = 0, changedUnix = 0;
};
enum class BattlePhase : uint8_t { Issued = 0, Started = 1, Cancelled = 2, Completed = 3, Expired = 4 };
struct BattleReference { std::string id; uint32_t territory = 0; BattlePhase status = BattlePhase::Issued; };
struct PlayerStatus {
    uint32_t requestId = 0;
    std::string campaignId;
    uint64_t campaignRevision = 0;
    std::optional<PlayerAllegiance> allegiance;
    std::vector<BattleReference> battles;
    bool battlesTruncated = false;
};
enum class Outcome : uint8_t {
    Victory = 0, Resignation = 1, Disconnect = 2, Timeout = 3, ServerAbort = 4,
    Draw = 5, RefereeFailure = 6, Desync = 7, InvalidClient = 8, ParticipantSubstitution = 9
};
struct BattleResult { Outcome outcome = Outcome::ServerAbort; uint64_t finalTick = 0, finalStateHash = 0; std::vector<std::string> winners; };
struct BattleStatus {
    uint32_t requestId = 0;
    std::string campaignId, battleId;
    uint64_t campaignRevision = 0;
    uint32_t territory = 0;
    BattlePhase status = BattlePhase::Issued;
    std::string mapIdentifier;
    uint64_t expiresUnix = 0;
    uint32_t roomId = 0;
    std::optional<BattleResult> result;
};
struct Error {
    uint32_t requestId = 0;
    ErrorCode code = ErrorCode::Malformed;
    std::string campaignId;
    std::optional<uint64_t> currentRevision;
    std::string reason;
};
using Response = std::variant<CampaignList, Snapshot, PlayerStatus, BattleStatus, Error>;

enum class RequestKind { List, Snapshot, PlayerStatus, BattleStatus };
enum class ResponseKind { List, Snapshot, PlayerStatus, BattleStatus, Error };
RequestKind kindOf(const Request& request);
ResponseKind kindOf(const Response& response);

// Every payload begins u16 version, u32 requestId (little endian); the outer
// network Msg identifies the kind. requestId0 is reserved for notifications.
// Encoders and decoders both validate complete messages. They throw DecodeError
// (a runtime_error) without publishing partial objects. All strings are UTF-8,
// NUL/control-free and byte-bounded. Counts, optional flags and enums are strict.
Bytes encode(const Request& request);
Bytes encode(const Response& response);
Request decodeRequest(RequestKind kind, const Bytes& payload);
Response decodeResponse(ResponseKind kind, const Bytes& payload);
Snapshot decodeSnapshot(const Bytes& payload);

// Full snapshots only. Canonical territory/neighbor ordering makes equivalent
// snapshots compare equal regardless of source ordering or requestId.
// Malformed payloads throw; stale/conflicting updates return a rejection without
// changing anything. At most64 cached campaigns; erase/clear release old entries.
enum class ApplyResult { Inserted, Updated, Unchanged, Stale, Conflict };
class Replica {
public:
    ApplyResult apply(const Bytes& payload);
    ApplyResult apply(Snapshot snapshot);
    const Snapshot* find(const std::string& campaignId) const;
    void erase(const std::string& campaignId) { snapshots_.erase(campaignId); }
    void clear() { snapshots_.clear(); }
private:
    std::map<std::string, Snapshot> snapshots_;
};
} // namespace tak::net::crusades
