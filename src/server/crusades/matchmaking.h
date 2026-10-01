#pragma once

#include "server/crusades/store.h"
#include <map>
#include <utility>

namespace tak::srv::crusades {

// Modern, volatile server policy. This is not a recovered retail queue or rank
// rule. Entries disappear on restart; durable battle capabilities do not.
struct MatchEntry {
    std::string accountId, campaignId;
    TerritoryId territory = 0;
    Alliance alliance = Alliance::Honor;
    int64_t allegianceRevision = 0, campaignRevision = 0;
    int64_t expiresUnix = 0;
    uint64_t sequence = 0; // assigned by the queue, never supplied by clients
};

class MatchQueue {
public:
    static constexpr int64_t kLifetimeSeconds = 600;
    static constexpr size_t kMaximumCapacity = 1024;
    explicit MatchQueue(size_t capacity = kMaximumCapacity);
    // One entry per account across campaigns. Replacing a changed request moves
    // it to the back; an identical request preserves FIFO position and deadline.
    // now and deadline are server supplied; deadline may not exceed ten minutes.
    MatchEntry put(MatchEntry entry, int64_t now);
    bool erase(const std::string& accountId);
    const MatchEntry* find(const std::string& accountId) const;
    std::vector<MatchEntry> expire(int64_t now);
    // Oldest unexpired opposing enrollment for this exact campaign, territory,
    // and campaign revision. Caller must revalidate account/allegiance/store/map
    // eligibility before issuing a battle, then erase both only after success.
    // For issuance use firstPair: arrival-specific selection can bypass an
    // older same-side entry retained after an unsuccessful attempt.
    std::optional<MatchEntry> firstOpponent(const MatchEntry& entry, int64_t now) const;
    // Select the oldest entry from each opposing alliance for this exact scope,
    // regardless of which arrival triggers matching. Returned entries are in
    // sequence order (the first is the modern host). Selection is read-only:
    // failed issuance retains both positions, so a later same-side arrival
    // cannot bypass a waiting older entry. Revalidate both before issuance.
    std::optional<std::pair<MatchEntry, MatchEntry>> firstPair(
        const std::string& campaignId, TerritoryId territory,
        int64_t campaignRevision, int64_t now) const;
    std::vector<MatchEntry> entries() const;
    size_t size() const { return entries_.size(); }
    size_t count(const std::string& campaignId, TerritoryId territory, int64_t now) const;
private:
    size_t capacity_;
    uint64_t nextSequence_ = 1;
    std::map<std::string, MatchEntry> entries_;
};

} // namespace tak::srv::crusades
