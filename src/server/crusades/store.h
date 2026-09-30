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
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tak::srv::crusades
