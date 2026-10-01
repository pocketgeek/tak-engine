#include "server/crusades/network.h"
#include <algorithm>
namespace tak::srv::crusades {
namespace wire=net::crusades;
namespace {
ReadResponse pack(const wire::Response& response) {
    net::Msg msg=net::Msg::CrusadesError;
    switch(wire::kindOf(response)) {
    case wire::ResponseKind::List:msg=net::Msg::CrusadesCampaignList;break;
    case wire::ResponseKind::Snapshot:msg=net::Msg::CrusadesCampaignSnapshot;break;
    case wire::ResponseKind::PlayerStatus:msg=net::Msg::CrusadesPlayerStatus;break;
    case wire::ResponseKind::BattleStatus:msg=net::Msg::CrusadesBattleStatus;break;
    case wire::ResponseKind::Matchmaking:msg=net::Msg::CrusadesMatchmakingStatus;break;
    case wire::ResponseKind::Error:break;
    }
    net::Writer payload;payload.b=wire::encode(response);return {msg,std::move(payload)};
}
bool canonicalAccount(const std::string& account) {
    auto alnum=[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9');};
    return account.size()>=3&&account.size()<=20&&alnum(account.front())&&
        std::all_of(account.begin(),account.end(),[&](char c){return alnum(c)||c=='_'||c=='.'||c=='-';});
}
wire::Snapshot snapshot(CampaignStore& store,const wire::SnapshotRequest& request,const ActivityResolver& activity) {
    const auto campaign=store.load(request.campaignId);
    wire::Snapshot out;out.requestId=request.requestId;out.campaignId=campaign.definition.id();
    out.displayName=campaign.definition.displayName();out.revision=static_cast<uint64_t>(campaign.revision);
    out.rulesPolicy=policyIdentifier(campaign.rules);
    for(const auto& [id,definition]:campaign.definition.territories()) {
        const auto& state=campaign.state.territories.at(id);
        wire::Territory t;t.id=id;t.displayName=definition.displayName;t.nativeFaction=definition.nativeFaction;
        t.terrain=definition.terrain;t.mapIdentifier=definition.mapIdentifier;t.neighbors=definition.neighbors;
        if(state.owner) t.owner=static_cast<wire::Owner>(static_cast<unsigned>(*state.owner)+1);
        t.assignedMap=state.assignedMap;
        const auto& m=state.recon;
        t.recon={m.fatigueVictoryPoints,m.honor.requiredVictoryPoints,m.honor.supportVictoryPoints,m.honor.battleVictoryPoints,
            m.terror.requiredVictoryPoints,m.terror.supportVictoryPoints,m.terror.battleVictoryPoints};
        if(activity)t.activity=activity(out.campaignId,id);
        out.territories.push_back(std::move(t));
    }
    return out; // Always complete, including equal/ahead/stale client revisions.
}
bool participant(const IssuedBattle& battle,const std::string& account) {
    return std::find(battle.context.participants.begin(),battle.context.participants.end(),account)!=battle.context.participants.end();
}
}
ReadResponse campaignReadError(uint32_t id,wire::ErrorCode code) {
    const char* reason="campaign request unavailable";
    switch(code) {
    case wire::ErrorCode::Malformed:reason="invalid campaign request";break;
    case wire::ErrorCode::UnsupportedVersion:reason="unsupported campaign protocol version";break;
    case wire::ErrorCode::AuthenticationRequired:reason="account authentication required";break;
    case wire::ErrorCode::Disabled:reason="campaign service disabled";break;
    case wire::ErrorCode::NotFound:reason="campaign resource unavailable";break;
    case wire::ErrorCode::TooLarge:reason="campaign response exceeds protocol limits";break;
    default:break;
    }
    return pack(wire::Error{id,code,"",{},reason});
}
ReadResponse campaignReadResponse(CampaignStore* store,const std::string& account,const wire::Request& request,const RoomResolver& rooms,const ActivityResolver& activity) {
    const auto id=std::visit([](const auto& r){return r.requestId;},request);
    if(!canonicalAccount(account))return campaignReadError(id,wire::ErrorCode::AuthenticationRequired);
    if(!store)return campaignReadError(id,wire::ErrorCode::Disabled);
    try {
        if(const auto* r=std::get_if<wire::ListRequest>(&request)) {
            const auto page=store->campaignIds(r->afterCampaignId,r->limit);wire::CampaignList out;out.requestId=id;
            for(const auto& name:page.ids) {const auto campaign=store->load(name);out.entries.push_back({name,campaign.definition.displayName(),static_cast<uint64_t>(campaign.revision),policyIdentifier(campaign.rules)});}
            if(page.truncated)out.nextCursor=page.ids.back();
            return pack(out);
        }
        if(const auto* r=std::get_if<wire::SnapshotRequest>(&request)) {
            if(!store->hasCampaign(r->campaignId))return campaignReadError(id,wire::ErrorCode::NotFound);
            return pack(snapshot(*store,*r,activity));
        }
        if(const auto* r=std::get_if<wire::PlayerStatusRequest>(&request)) {
            if(!store->hasCampaign(r->campaignId))return campaignReadError(id,wire::ErrorCode::NotFound);
            const auto status=store->playerStatus(r->campaignId,account);
            wire::PlayerStatus out;out.requestId=id;out.campaignId=r->campaignId;out.campaignRevision=static_cast<uint64_t>(status.campaignRevision);
            if(const auto& allegiance=status.allegiance)
                out.allegiance=wire::PlayerAllegiance{static_cast<wire::Alliance>(allegiance->alliance),static_cast<uint64_t>(allegiance->revision),static_cast<uint64_t>(allegiance->joinedUnix),static_cast<uint64_t>(allegiance->changedUnix)};
            out.battlesTruncated=status.truncated;
            for(const auto& battle:status.battles)
                out.battles.push_back({battle.id,battle.territory,static_cast<wire::BattlePhase>(battle.status)});
            return pack(out);
        }
        if(!std::holds_alternative<wire::BattleStatusRequest>(request))return campaignReadError(id,wire::ErrorCode::Malformed);
        const auto& r=std::get<wire::BattleStatusRequest>(request);
        // Unknown IDs and unauthorized IDs deliberately share the same reply.
        IssuedBattle battle;
        try {battle=store->battle(r.battleId);}catch(const std::exception&){return campaignReadError(id,wire::ErrorCode::NotFound);}
        if(!participant(battle,account))return campaignReadError(id,wire::ErrorCode::NotFound);
        wire::BattleStatus out;out.requestId=id;out.campaignId=battle.campaignId;out.battleId=battle.id;
        out.campaignRevision=static_cast<uint64_t>(battle.campaignRevision);out.territory=battle.territory;
        out.status=static_cast<wire::BattlePhase>(battle.status);out.mapIdentifier=battle.context.mapIdentifier;
        out.expiresUnix=static_cast<uint64_t>(battle.expiresUnix);
        if(rooms&&(battle.status==BattleStatus::Issued||battle.status==BattleStatus::Started))out.roomId=rooms(battle.id).value_or(0);
        if(const auto result=store->verifiedResult(battle.id))out.result=wire::BattleResult{static_cast<wire::Outcome>(result->outcome),result->finalTick,result->finalStateHash,result->winners};
        return pack(out);
    }catch(const wire::DecodeError& e){return campaignReadError(id,e.code==wire::ErrorCode::TooLarge?e.code:wire::ErrorCode::Unavailable);}
    catch(const std::exception&){return campaignReadError(id,wire::ErrorCode::Unavailable);}
}
ReadResponse handleCampaignRead(CampaignStore* store,const std::string& account,net::Msg operation,const std::vector<uint8_t>& payload,const RoomResolver& rooms,const ActivityResolver& activity) {
    uint32_t id=0;if(payload.size()>=6)for(unsigned i=0;i<4;++i)id|=uint32_t(payload[2+i])<<(8*i);
    if(!canonicalAccount(account))return campaignReadError(id,wire::ErrorCode::AuthenticationRequired);
    if(!store)return campaignReadError(id,wire::ErrorCode::Disabled);
    wire::RequestKind kind;
    switch(operation) {
    case net::Msg::CrusadesListCampaigns:kind=wire::RequestKind::List;break;
    case net::Msg::CrusadesGetSnapshot:kind=wire::RequestKind::Snapshot;break;
    case net::Msg::CrusadesGetPlayerStatus:kind=wire::RequestKind::PlayerStatus;break;
    case net::Msg::CrusadesGetBattleStatus:kind=wire::RequestKind::BattleStatus;break;
    default:return campaignReadError(id,wire::ErrorCode::Malformed);
    }
    try{return campaignReadResponse(store,account,wire::decodeRequest(kind,payload),rooms,activity);}
    catch(const wire::DecodeError& e){return campaignReadError(id,e.code);}
    catch(const std::exception&){return campaignReadError(id,wire::ErrorCode::Malformed);}
}
} // namespace tak::srv::crusades
