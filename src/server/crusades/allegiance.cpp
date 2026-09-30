#include "server/crusades/allegiance.h"

#include <limits>

namespace tak::srv::crusades {
namespace {
net::Writer response(net::Msg operation, AllegianceStatus status, const std::string& campaign,
                     const std::optional<Allegiance>& allegiance, const std::string& message) {
    net::Writer w;
    w.u8(operation == net::Msg::CrusadesSetAllegiance ? 1 : 0);
    w.u8(static_cast<uint8_t>(status));
    w.str(campaign);
    w.u8(allegiance ? static_cast<uint8_t>(allegiance->alliance) : 0);
    w.u64(allegiance ? static_cast<uint64_t>(allegiance->revision) : UINT64_MAX);
    w.u64(allegiance ? static_cast<uint64_t>(allegiance->joinedUnix) : 0);
    w.u64(allegiance ? static_cast<uint64_t>(allegiance->changedUnix) : 0);
    w.str(message);
    return w;
}
}

net::Writer handleAllegiance(CampaignStore* store, const std::string& authenticatedAccount,
        net::Msg operation, const std::vector<uint8_t>& payload, int64_t unixTime) {
    if (authenticatedAccount.empty())
        return response(operation,AllegianceStatus::Unauthorized,"",{},"account authentication required");
    if (!store)
        return response(operation,AllegianceStatus::Disabled,"",{},"campaign service disabled");
    const bool set = operation == net::Msg::CrusadesSetAllegiance;
    if ((!set && operation != net::Msg::CrusadesGetAllegiance) || payload.size() < 3 || payload.size() > 139)
        return response(operation,AllegianceStatus::BadRequest,"",{},"invalid allegiance request");
    net::Reader r(payload.data(),payload.size());
    const std::string campaign = r.str();
    uint64_t revision = UINT64_MAX;
    uint8_t alliance = 0;
    if (set) { revision = r.u64(); alliance = r.u8(); }
    if (!r.ok || r.p != r.end || campaign.empty() || campaign.size() > 128 ||
        campaign.find('\0') != std::string::npos ||
        (set && (alliance < 1 || alliance > 2 ||
            (revision != UINT64_MAX && revision > uint64_t(std::numeric_limits<int64_t>::max())))))
        return response(operation,AllegianceStatus::BadRequest,"",{},"invalid allegiance request");
    try {
        std::optional<Allegiance> current;
        if (set) {
            // Engine policy: an authenticated player may switch immediately.
            // Historical rank penalties/house restrictions are not implemented.
            current = store->setAllegiance(campaign,authenticatedAccount,static_cast<Alliance>(alliance),
                revision == UINT64_MAX ? -1 : static_cast<int64_t>(revision),unixTime);
        } else current = store->allegiance(campaign,authenticatedAccount);
        return response(operation,AllegianceStatus::Ok,campaign,current,"");
    } catch (const std::exception&) {
        // Storage diagnostics can reveal local paths/schema details. Keep those
        // out of peer replies; failed transactions do not change allegiance.
        return response(operation,AllegianceStatus::Rejected,campaign,{},"allegiance request rejected; query current state before retrying");
    }
}
} // namespace tak::srv::crusades
