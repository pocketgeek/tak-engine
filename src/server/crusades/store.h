#pragma once

#include "server/crusades/campaign.h"
#include <memory>

namespace tak::srv::crusades {

struct BattleResult {
    std::string battleId;
    // Opaque caller-supplied result, not a tactical-score-to-campaign formula.
    // Binary payloads (including NUL) round-trip; maximum size is 16 MiB.
    std::string payload;
};

struct StoredCampaign {
    CampaignDefinition definition;
    CampaignState state;
    int64_t revision;
};

struct CampaignEvent {
    int64_t revision;
    std::string reason;
    std::optional<std::string> battleId;
    CampaignState state;
};

enum class Alliance { Honor = 1, Terror = 2 };

struct Allegiance {
    std::string accountId;
    Alliance alliance;
    int64_t joinedUnix;
    int64_t changedUnix;
    int64_t revision;
};

struct BattleContext {
    std::string mapIdentifier;
    std::string mapDigest;
    std::string rulesDigest;
    std::vector<std::string> participants;
    bool crusadesBalance = true;
};
enum class BattleStatus { Issued = 0, Started = 1, Cancelled = 2, Completed = 3, Expired = 4 };
struct IssuedBattle {
    std::string id, campaignId;
    int64_t campaignRevision;
    TerritoryId territory;
    BattleContext context;
    std::vector<Alliance> participantAlliances;
    std::vector<int64_t> participantRevisions;
    int64_t createdUnix, expiresUnix, changedUnix;
    BattleStatus status;
    std::string launchToken;
    std::optional<std::string> roomToken;
};

struct StoreOptions {
    // Diagnostic/test hook after all mutation writes and before COMMIT,
    // including an existing database's schema migration (not fresh creation).
    // Throwing rolls back. Must not reenter this store.
    std::function<void()> beforeCommit;
};

// Independent campaign database, never the account credential file. SQLite's
// :memory: path uses the same schema/transactions as persistent stores.
// One object is used by one thread at a time; separate connections serialize
// writes and enforce expectedRevision to prevent lost updates.
// Throws std::runtime_error on validation, revision, duplicate or I/O failures.
// Initial creation is revision zero; each successful commit advances by one.
// Snapshots and individual stored strings are capped at 16 MiB. This is a
// standalone persistence service; it does not authenticate battle reports.
class CampaignStore {
public:
    explicit CampaignStore(const std::filesystem::path& path, StoreOptions options = {});
    ~CampaignStore();
    CampaignStore(const CampaignStore&) = delete;
    CampaignStore& operator=(const CampaignStore&) = delete;

    void create(const CampaignDefinition& definition, const CampaignState& initialState,
                const std::string& reason);
    StoredCampaign load(const std::string& campaignId) const;
    bool hasCampaign(const std::string& campaignId) const;
    int64_t commit(const std::string& campaignId, int64_t expectedRevision,
                   const CampaignState& nextState, const std::string& reason,
                   const std::optional<BattleResult>& battle = {});
    std::vector<CampaignEvent> history(const std::string& campaignId) const;
    std::optional<std::string> battleResult(const std::string& campaignId,
                                           const std::string& battleId) const;

    // Trusted persistence boundary: caller must supply the authenticated,
    // canonical lowercase account ID, not a display name or client claim.
    // No credentials/account registry live here. Missing participation is null.
    std::optional<Allegiance> allegiance(const std::string& campaignId,
                                         const std::string& accountId) const;
    // Modern policy: immediate switches allowed, no historical rank rules.
    // expectedRevision=-1 joins at revision 0; subsequent changes increment the
    // independent per-account revision. Same-alliance mutations are rejected.
    // unixTime is server supplied, nonnegative and cannot precede last change.
    Allegiance setAllegiance(const std::string& campaignId, const std::string& accountId,
                            Alliance alliance, int64_t expectedRevision, int64_t unixTime);
    std::vector<Allegiance> allegianceHistory(const std::string& campaignId,
                                              const std::string& accountId) const;

    // Trusted server calls only. Modern policy: two authenticated, enrolled
    // opponents; CB mandatory. Digests and room tokens are server generated.
    // Roster is canonicalized by account ID. Start/report require unchanged
    // campaign and allegiance revisions. Switching away and back invalidates
    // outstanding credit; this is a modern safety policy, not retail rules.
    // expires is a launch deadline only; Started battles remain live until
    // cancellation/completion. Room tokens are globally single-use in this DB.
    IssuedBattle issueBattle(const std::string& campaignId, int64_t expectedRevision,
        TerritoryId territory, BattleContext context, int64_t now, int64_t expires);
    IssuedBattle battle(const std::string& battleId) const;
    void startBattle(const std::string& battleId, const std::string& launchToken,
        const std::string& roomToken, BattleContext context, int64_t now);
    IssuedBattle authorizeBattleReport(const std::string& battleId, const std::string& roomToken,
        BattleContext context, int64_t now) const;
    // Terminal lifecycle marker only; no result/campaign-point processing (M6).
    void completeBattle(const std::string& battleId, const std::string& roomToken,
        BattleContext context, int64_t now);
    void cancelBattle(const std::string& battleId, int64_t now);
    void expireBattles(int64_t now);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tak::srv::crusades
