#pragma once

#include <filesystem>
#include <memory>

namespace tak::srv::crusades {

// Exclusive process lease for a running campaign service or offline admin
// command. The kernel releases it after a crash; a leftover companion file is
// harmless and must never be deleted to bypass an active lease. SQLite's own
// transactions still govern independent trusted store connections.
class CampaignServiceLease {
public:
    explicit CampaignServiceLease(const std::filesystem::path& database);
    ~CampaignServiceLease();
    CampaignServiceLease(const CampaignServiceLease&) = delete;
    CampaignServiceLease& operator=(const CampaignServiceLease&) = delete;
    const std::filesystem::path& databasePath() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tak::srv::crusades
