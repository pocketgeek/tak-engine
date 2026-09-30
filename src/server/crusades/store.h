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

struct StoreOptions {
    // Diagnostic/test hook after all mutation writes and before COMMIT.
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
    int64_t commit(const std::string& campaignId, int64_t expectedRevision,
                   const CampaignState& nextState, const std::string& reason,
                   const std::optional<BattleResult>& battle = {});
    std::vector<CampaignEvent> history(const std::string& campaignId) const;
    std::optional<std::string> battleResult(const std::string& campaignId,
                                           const std::string& battleId) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tak::srv::crusades
