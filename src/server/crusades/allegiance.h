#pragma once

#include "server/crusades/store.h"
#include "net/protocol.h"

namespace tak::srv::crusades {

enum class AllegianceStatus : uint8_t { Ok = 0, BadRequest = 1, Unauthorized = 2, Disabled = 3, Rejected = 4 };

// Account comes exclusively from a successfully authenticated server connection,
// already normalized with auth::foldUsername. Never take it from packet data.
// Empty account rejects all operations, including queries. No tactical effects.
net::Writer handleAllegiance(CampaignStore* store, const std::string& authenticatedAccount,
    net::Msg operation, const std::vector<uint8_t>& payload, int64_t unixTime);

} // namespace tak::srv::crusades
