#pragma once
#include "server/crusades/store.h"
#include "net/crusades.h"
#include "net/protocol.h"
#include <functional>
namespace tak::srv::crusades {
struct ReadResponse { net::Msg kind; net::Writer payload; };
using RoomResolver = std::function<std::optional<uint32_t>(const std::string& battleId)>;
// Account is supplied only by the authenticated server connection. Requests
// contain no account selector. Runtime room lookup is trusted server state.
ReadResponse handleCampaignRead(CampaignStore* store, const std::string& authenticatedAccount,
    net::Msg operation, const std::vector<uint8_t>& payload, const RoomResolver& rooms = {});
// Typed builders also support server notifications (requestId zero). They use
// the same authorization/conversion boundary as ordinary requests.
ReadResponse campaignReadResponse(CampaignStore* store, const std::string& authenticatedAccount,
    const net::crusades::Request& request, const RoomResolver& rooms = {});
ReadResponse campaignReadError(uint32_t requestId, net::crusades::ErrorCode code);
} // namespace tak::srv::crusades
