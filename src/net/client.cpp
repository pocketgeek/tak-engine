#include "net/client.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <system_error>

#include "net/auth.h"
#include "net/crypto.h"
#include "tnt/mapgen.h"

namespace tak::net {

namespace {
uint64_t nowMs() {   // monotonic wall-clock (pacing/keepalive only; never hashed)
    using namespace std::chrono;
    return uint64_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
constexpr uint64_t kPingIdleMs = 5000, kTimeoutMs = 15000;
constexpr uint64_t kSpectatorTimeoutMs = 120000;   // matches the server's spectator grace
}  // namespace

bool MpClient::connect(const std::string& host, uint16_t port, const std::string& name) {
    if (derive_.joinable()) derive_.join();
    clearCampaignCache();
    crypto::wipe(keys_.clientKey.data(), keys_.clientKey.size());
    crypto::wipe(keys_.serverKey.data(), keys_.serverKey.size());
    auth_ = Auth::None; account_.clear(); pend_ = PendingAuth{};
    deriveDone_.store(false, std::memory_order_relaxed);
    conn_ = Conn{}; err_.clear();
    name_ = name;
    if (!conn_.connect(host, port)) { err_ = conn_.error(); state_ = State::Done; return false; }
    state_ = State::Connecting;
    Writer w;
    w.u32(kNetVersion);
    w.str("");          // build id (version-gated only)
    w.u64(dataHash_);   // gameplay-data fingerprint (enforced by the server)
    w.str(name_);
    send(Msg::Hello, w);
    lastRecvMs_ = nowMs();
    return true;
}

MpClient::~MpClient() {
    cancelCampaignReplayDownload();
    if (derive_.joinable()) derive_.join();   // never outlive the key-derivation worker
    crypto::wipe(loginPass_);
}

void MpClient::setLogin(const std::string& user, const std::string& password) {
    loginUser_ = user;
    loginPass_ = password;
}

void MpClient::disconnect(const std::string& reason) {
    if (conn_.ok()) { Writer w; w.str(reason); send(Msg::Bye, w); conn_.flushWrite(); }
    conn_.closeNow();
    clearCampaignCache();
    state_ = State::Done;
}

bool MpClient::poll() {
    if (state_ == State::Offline || state_ == State::Done) return false;
    if (jitterMs_ < 0) {   // one-time init of the test link model
        const char* j = std::getenv("TAK_NET_JITTER_MS");
        const char* b = std::getenv("TAK_NET_BASE_MS");
        const char* l = std::getenv("TAK_NET_LOSS_PCT");
        jitterMs_ = j ? std::max(0, std::atoi(j)) : 0;
        baseMs_ = b ? std::max(0, std::atoi(b)) : 0;
        lossPct_ = l ? std::clamp(std::atoi(l), 0, 100) : 0;
    }
    // recv() no longer fails on a clean close, so the frames that arrived alongside
    // the FIN get drained BEFORE the connection is finished off. That matters here as
    // well as on the server: a Reject sent immediately before the close used to be
    // buffered and then thrown away, leaving only a bare "peer closed" to explain a
    // rejection the server had actually spelled out.
    const bool recvOk = conn_.recv();
    Frame f;
    if (recvOk)
        while (conn_.poll(f)) { onFrame(f); if (!conn_.ok()) break; }
    if (!recvOk || conn_.peerClosed()) {
        // Keep an existing reason (e.g. the server's Reject text): the socket
        // closing right after a Reject must not overwrite WHY with "peer closed".
        if (err_.empty()) {
            err_ = conn_.error().empty() ? std::string("peer closed") : conn_.error();
            // A close during the handshake (never Welcomed) is almost always a
            // version gate on a server too old to flush its Reject reason --
            // say so instead of a bare "peer closed".
            if (state_ == State::Connecting)
                err_ = "server closed during handshake -- likely a protocol version "
                       "mismatch (this client speaks v" + std::to_string(kNetVersion) + ")";
        }
        state_ = State::Done;
        clearCampaignCache();
        return false;
    }
    mapSend_.pump(conn_);
    if (startRequested_ && state_ == State::InRoom && room_.mapsReady) {
        send(Msg::StartGame); startRequested_ = false;
    }
    pumpDerive();          // the login's PBKDF2 finished on its worker: send the proof
    uint64_t now = nowMs();
    if (campaignReplayNextPullMs_ && now >= campaignReplayNextPullMs_ && campaignReplayDownload_.state == CampaignReplayState::Downloading) {
        campaignReplayNextPullMs_ = 0;
        campaignReplayRequest_ = sendCampaignRequest(Msg::CrusadesGetReplayChunk,
            crusades::ReplayChunkRequest{0,campaignReplayDownload_.battleId,campaignReplayDownload_.receivedBytes,crusades::kReplayChunkBytes},campaignReplayDownload_.battleId);
        if (!campaignReplayRequest_) failCampaignReplay("Cannot request next replay chunk");
    }
    for (auto it=campaignPending_.begin(); it!=campaignPending_.end();) {
        if (now-it->second.sentMs >= 15000) {
            if (it->first == campaignReplayRequest_) failCampaignReplay("Replay request timed out");
            campaignError_=crusades::Error{it->first,crusades::ErrorCode::Unavailable,{}, {},"Campaign request timed out"};
            it=campaignPending_.erase(it);
        } else ++it;
    }
    // Release any jitter-held bundles whose delay has elapsed.
    if (!jitterHeld_.empty()) {
        for (auto it = jitterHeld_.begin(); it != jitterHeld_.end();) {
            if (it->releaseMs <= now) { bundles_[it->tick] = std::move(it->bd);
                                        noteBundleArrival(now); it = jitterHeld_.erase(it); }
            else ++it;
        }
    }
    // Active RTT probe (only when a consumer enabled it): one ping/sec.
    if (measureRtt_ && !pingSentMs_ && now - lastPingMs_ > 1000) {
        send(Msg::Ping); lastPingMs_ = now; pingSentMs_ = now;
    }
    if (now - lastRecvMs_ > kPingIdleMs && now - lastPingMs_ > kPingIdleMs) {
        send(Msg::Ping); lastPingMs_ = now;
    }
    // A spectator's render loop can freeze for several seconds on a heavy frame
    // (big supersampled resolution under VRAM pressure) without pumping the socket;
    // give it the same long grace the server extends to spectators so a transient
    // stall doesn't self-terminate a display-only connection.
    uint64_t timeoutMs = spectator_ ? kSpectatorTimeoutMs : kTimeoutMs;
    if (now - lastRecvMs_ > timeoutMs) { if (err_.empty()) err_ = "server timeout"; state_ = State::Done; }
    if (!conn_.flushWrite()) { if (err_.empty()) err_ = conn_.error(); state_ = State::Done; }
    if (!conn_.ok() && err_.empty()) { err_ = conn_.error(); state_ = State::Done; }
    if (state_ == State::Done) clearCampaignCache();
    return state_ != State::Done;
}

bool MpClient::campaignAuthenticated() const {
    return (auth_ == Auth::Ok || auth_ == Auth::Created) && !account_.empty() &&
        state_ != State::Offline && state_ != State::Done && state_ != State::Connecting;
}
void MpClient::clearCampaignCache() {
    cancelCampaignReplayDownload(); campaignHistory_.clear(); campaignHistoryRequests_.clear(); campaignHistoryRecords_.clear();
    campaignReplica_.clear(); campaignList_.reset(); campaignPlayer_.reset(); campaignMatchmaking_.reset(); campaignBattles_.clear(); campaignRoomBindings_.clear();
    campaignError_.reset(); campaignInvitation_.reset(); campaignPending_.clear();
    campaignRequestId_ = 0; campaignRefreshAttempt_.clear();
}
uint32_t MpClient::sendCampaignRequest(Msg kind, crusades::Request request, const std::string& target) {
    if (!campaignAuthenticated() || campaignPending_.size() >= 128 || campaignRequestId_ == UINT32_MAX) return 0;
    const auto id = ++campaignRequestId_;
    std::visit([&](auto& value) { value.requestId = id; }, request);
    try {
        auto bytes = crusades::encode(request);
        CampaignPending pending{kind, target, nowMs(), {}};
        if (const auto* history = std::get_if<crusades::TerritoryHistoryRequest>(&request)) pending.history = *history;
        campaignPending_.emplace(id, std::move(pending));
        conn_.send(kind, bytes);
        return id;
    } catch (const crusades::DecodeError& e) {
        campaignError_ = crusades::Error{id, e.code, {}, {}, e.what()}; return 0;
    }
}
uint32_t MpClient::listCampaigns(const std::string& after, uint16_t limit) {
    return sendCampaignRequest(Msg::CrusadesListCampaigns, crusades::ListRequest{0, after, limit}, {});
}
uint32_t MpClient::getCampaignSnapshot(const std::string& campaign, uint64_t expected) {
    return sendCampaignRequest(Msg::CrusadesGetSnapshot, crusades::SnapshotRequest{0, campaign, expected}, campaign);
}
uint32_t MpClient::getPlayerCampaignStatus(const std::string& campaign) {
    return sendCampaignRequest(Msg::CrusadesGetPlayerStatus, crusades::PlayerStatusRequest{0, campaign}, campaign);
}
uint32_t MpClient::getCampaignBattleStatus(const std::string& battle) {
    return sendCampaignRequest(Msg::CrusadesGetBattleStatus, crusades::BattleStatusRequest{0, battle}, battle);
}
uint32_t MpClient::getCampaignMatchmaking(const std::string& campaign) {
    return sendCampaignRequest(Msg::CrusadesGetMatchmaking, crusades::MatchmakingRequest{0, campaign}, campaign);
}
uint32_t MpClient::searchCampaignBattle(const std::string& campaign, uint32_t territory) {
    return sendCampaignRequest(Msg::CrusadesSearchBattle, crusades::MatchSearchRequest{0, campaign, territory}, campaign);
}
uint32_t MpClient::cancelCampaignSearch(const std::string& campaign) {
    return sendCampaignRequest(Msg::CrusadesCancelSearch, crusades::MatchCancelRequest{0, campaign}, campaign);
}
uint32_t MpClient::getTerritoryHistory(const std::string& campaign, uint32_t territory,
    std::optional<crusades::HistoryCursor> cursor, uint16_t limit) {
    const auto id = sendCampaignRequest(Msg::CrusadesGetTerritoryHistory,
        crusades::TerritoryHistoryRequest{0,campaign,territory,std::move(cursor),limit},campaign);
    if (id) {
        const HistoryKey key{campaign,territory};
        if (!campaignHistoryRequests_.count(key) && campaignHistoryRequests_.size() >= 64) campaignHistoryRequests_.erase(campaignHistoryRequests_.begin());
        campaignHistoryRequests_[key] = id;
    }
    return id;
}
const crusades::TerritoryHistory* MpClient::territoryHistory(const std::string& campaign, uint32_t territory) const {
    const auto it = campaignHistory_.find({campaign,territory}); return it == campaignHistory_.end() ? nullptr : &it->second;
}
void MpClient::cancelCampaignReplayDownload() {
    if (campaignReplayRequest_) campaignPending_.erase(campaignReplayRequest_);
    campaignReplayRequest_ = 0; campaignReplayNextPullMs_ = 0;
    if (campaignReplayOutput_.is_open()) campaignReplayOutput_.close();
    if (!campaignReplayPartial_.empty()) { std::error_code ec; std::filesystem::remove(campaignReplayPartial_, ec); }
    campaignReplayPartial_.clear(); campaignReplayHash_.reset(); campaignReplayDownload_ = {};
}
void MpClient::failCampaignReplay(const std::string& error) {
    // Caller may hold a pending-map iterator; erase the correlation at its call site.
    campaignReplayRequest_ = 0; campaignReplayNextPullMs_ = 0;
    if (campaignReplayOutput_.is_open()) campaignReplayOutput_.close();
    if (!campaignReplayPartial_.empty()) { std::error_code ec; std::filesystem::remove(campaignReplayPartial_, ec); }
    campaignReplayPartial_.clear(); campaignReplayHash_.reset();
    campaignReplayDownload_.state = CampaignReplayState::Failed; campaignReplayDownload_.path.clear(); campaignReplayDownload_.error = error;
}
namespace {
bool validReplayCache(const std::filesystem::path& path, const crusades::ReplayMetadata& metadata) {
    std::error_code ec;
    if (std::filesystem::is_symlink(path,ec) || std::filesystem::file_size(path,ec) != metadata.totalBytes || ec) return false;
    std::ifstream in(path,std::ios::binary); if (!in) return false;
    crypto::Sha256 hash; std::array<char,crusades::kReplayChunkBytes> bytes{}; uint64_t read = 0;
    while (read < metadata.totalBytes) {
        const auto n = size_t(std::min<uint64_t>(bytes.size(),metadata.totalBytes-read));
        in.read(bytes.data(),std::streamsize(n)); if (!in) return false;
        hash.update(bytes.data(),n); read += n;
    }
    return crypto::toHex(hash.final()) == metadata.digest;
}
}
uint32_t MpClient::requestCampaignReplay(const std::string& battle) {
    cancelCampaignReplayDownload(); campaignReplayDownload_.battleId = battle;
    if (!campaignAuthenticated()) { failCampaignReplay("Sign in to download a campaign replay"); return 0; }
    const crusades::HistoryBattle* record = nullptr;
    for (const auto& [key,page] : campaignHistory_) {
        (void)key;
        for (const auto& entry : page.entries) if (entry.battleId == battle) { record = &entry; break; }
        if (record) break;
    }
    if (!record || !record->replay) { failCampaignReplay("Replay is unavailable"); return 0; }
    const auto& metadata = *record->replay;
    if (!supportedReplayProtocol(metadata.format,metadata.protocolVersion)) { failCampaignReplay("Replay requires a different engine version"); return 0; }
    if (campaignReplayCacheRoot_.empty()) { failCampaignReplay("Replay cache is unavailable"); return 0; }
    campaignReplayDownload_.digest = metadata.digest; campaignReplayDownload_.totalBytes = metadata.totalBytes;
    try {
        std::filesystem::create_directories(campaignReplayCacheRoot_);
        const auto path = campaignReplayCacheRoot_ / (metadata.digest + ".takrep");
        if (validReplayCache(path,metadata)) {
            campaignReplayDownload_.state = CampaignReplayState::Ready; campaignReplayDownload_.path = path;
            campaignReplayDownload_.receivedBytes = metadata.totalBytes;
            // No request was required: nonzero indicates locally fulfilled action.
            return UINT32_MAX;
        }
        std::error_code ec; std::filesystem::remove(path,ec);
        const auto nonce = crypto::toHex(crypto::randomVec(16));
        campaignReplayPartial_ = campaignReplayCacheRoot_ / (metadata.digest + "." + nonce + ".part");
        campaignReplayOutput_.open(campaignReplayPartial_,std::ios::binary | std::ios::trunc);
        if (!campaignReplayOutput_) throw std::runtime_error("cannot create replay cache file");
        campaignReplayDownload_.state = CampaignReplayState::Downloading;
        campaignReplayRequest_ = sendCampaignRequest(Msg::CrusadesGetReplayChunk,crusades::ReplayChunkRequest{0,battle,0,crusades::kReplayChunkBytes},battle);
        if (!campaignReplayRequest_) throw std::runtime_error("cannot request replay");
        return campaignReplayRequest_;
    } catch (const std::exception&) { failCampaignReplay("Cannot write replay cache"); return 0; }
}
void MpClient::acceptCampaignReplayChunk(const crusades::ReplayChunk& chunk) {
    const auto& download = campaignReplayDownload_;
    if (download.state != CampaignReplayState::Downloading || chunk.requestId != campaignReplayRequest_ ||
        chunk.battleId != download.battleId || chunk.digest != download.digest || chunk.totalBytes != download.totalBytes || chunk.offset != download.receivedBytes)
        throw crusades::DecodeError(crusades::ErrorCode::Malformed,"replay chunk identity/order mismatch");
    campaignReplayOutput_.write(reinterpret_cast<const char*>(chunk.bytes.data()),std::streamsize(chunk.bytes.size()));
    if (!campaignReplayOutput_) throw std::runtime_error("replay cache write failed");
    campaignReplayHash_.update(chunk.bytes.data(),chunk.bytes.size()); campaignReplayDownload_.receivedBytes += chunk.bytes.size();
    campaignReplayRequest_ = 0;
    if (chunk.final) {
        campaignReplayOutput_.flush(); if (!campaignReplayOutput_) throw std::runtime_error("replay cache flush failed"); campaignReplayOutput_.close();
        if (campaignReplayOutput_.fail() || crypto::toHex(campaignReplayHash_.final()) != download.digest) throw std::runtime_error("replay digest mismatch");
        const auto path = campaignReplayCacheRoot_ / (download.digest + ".takrep");
        std::filesystem::rename(campaignReplayPartial_,path); campaignReplayPartial_.clear();
        campaignReplayDownload_.path = path; campaignReplayDownload_.state = CampaignReplayState::Ready; campaignReplayHash_.reset();
    } else {
        campaignReplayNextPullMs_ = nowMs() + 20;
    }
}
void MpClient::subscribeCampaign(const std::string& campaign) {
    // Outstanding queue reads/operations belong to the subscription that issued
    // them. Dropping their correlations prevents late replies from restoring a
    // previous search after switching away and back to the same campaign.
    const auto discardMatchRequests = [&] {
        for (auto it = campaignPending_.begin(); it != campaignPending_.end();) {
            const auto kind = it->second.kind;
            if (kind == Msg::CrusadesGetMatchmaking || kind == Msg::CrusadesSearchBattle || kind == Msg::CrusadesCancelSearch)
                it = campaignPending_.erase(it);
            else ++it;
        }
    };
    if (campaign.empty()) {
        discardMatchRequests(); campaignSubscription_.clear(); campaignPlayer_.reset();
        campaignMatchmaking_.reset(); campaignRefreshAttempt_.clear(); return;
    }
    // Validate before retaining a reconnect subscription, even while offline.
    (void)crusades::encode(crusades::Request{crusades::SnapshotRequest{1, campaign, crusades::kUnknownRevision}});
    if (campaignSubscription_ != campaign) { discardMatchRequests(); campaignMatchmaking_.reset(); }
    campaignSubscription_ = campaign; campaignRefreshAttempt_.clear(); campaignPlayer_.reset();
    if (campaignAuthenticated()) { getCampaignSnapshot(campaign); getPlayerCampaignStatus(campaign); }
}
void MpClient::refreshCampaignOnce(const std::string& campaign) {
    if (campaign.empty() || campaign != campaignSubscription_ || campaignRefreshAttempt_ == campaign) return;
    for (const auto& [id, pending] : campaignPending_) {
        (void)id;
        if (pending.kind == Msg::CrusadesGetSnapshot && pending.target == campaign) return;
    }
    campaignRefreshAttempt_ = campaign;
    getCampaignSnapshot(campaign);
}
void MpClient::getCampaignAllegiance(const std::string& campaign) {
    if (!campaignAuthenticated()) return;
    (void)crusades::encode(crusades::Request{crusades::PlayerStatusRequest{1,campaign}});
    Writer w; w.str(campaign); send(Msg::CrusadesGetAllegiance,w);
}
void MpClient::setCampaignAllegiance(const std::string& campaign, uint64_t revision, crusades::Alliance side) {
    if (!campaignAuthenticated()) return;
    (void)crusades::encode(crusades::Request{crusades::PlayerStatusRequest{1,campaign}});
    if (side != crusades::Alliance::Honor && side != crusades::Alliance::Terror) return;
    Writer w; w.str(campaign); w.u64(revision); w.u8(uint8_t(side)); send(Msg::CrusadesSetAllegiance,w);
}
void MpClient::issueCampaignBattle(const std::string& campaign, uint32_t territory, const std::string& opponent) {
    if (!campaignAuthenticated() || !territory || !auth::validUsername(opponent)) return;
    (void)crusades::encode(crusades::Request{crusades::PlayerStatusRequest{1,campaign}});
    Writer w; w.str(campaign); w.u32(territory); w.str(auth::foldUsername(opponent)); send(Msg::CrusadesIssueBattle,w);
}
void MpClient::campaignFrame(const Frame& f) {
    if (!campaignAuthenticated()) return;
    namespace cw = crusades;
    try {
        if (f.kind == Msg::CrusadesBattleResult) {
            Reader r(f.payload.data(),f.payload.size()); const auto status=r.u8();
            CampaignInvitation value; value.campaignId=r.str(); value.battleId=r.str(); value.roomId=r.u32();
            value.map=r.str(); value.expiresUnix=r.u64(); const auto reason=r.str();
            if (!r.ok || r.p!=r.end || status>4 || value.campaignId.size()>cw::kMaxIdentifier || value.battleId.size()>cw::kMaxIdentifier || value.map.size()>cw::kMaxMapIdentifier || reason.size()>cw::kMaxReason)
                throw cw::DecodeError(cw::ErrorCode::Malformed,"malformed campaign invitation");
            if (status) {
                cw::Error error{0,cw::ErrorCode::Unavailable,value.campaignId,{},reason};
                (void)cw::encode(cw::Response{error});campaignError_=std::move(error);return;
            }
            cw::BattleStatus validated;validated.campaignId=value.campaignId;validated.battleId=value.battleId;
            validated.territory=1;validated.mapIdentifier=value.map;validated.expiresUnix=value.expiresUnix;validated.roomId=value.roomId;
            (void)cw::encode(cw::Response{validated});
            // The new strict query encoder validates the supplied identifier.
            if (!value.roomId || !getCampaignBattleStatus(value.battleId))
                throw cw::DecodeError(cw::ErrorCode::Malformed,"invalid campaign invitation identity");
            campaignInvitation_=std::move(value); return;
        }
        if (f.kind == Msg::CrusadesAllegianceResult) {
            Reader r(f.payload.data(),f.payload.size()); const auto operation=r.u8(),status=r.u8();const auto campaign=r.str();
            const auto side=r.u8(); const auto revision=r.u64(),joined=r.u64(),changed=r.u64();const auto reason=r.str();
            if (!r.ok || r.p!=r.end || operation>1 || status>4 || side>2 || campaign.size()>128 || reason.size()>512 || changed<joined ||
                (side==0 && (revision!=UINT64_MAX || joined || changed)) ||
                (side!=0 && revision>uint64_t(INT64_MAX)) || joined>uint64_t(INT64_MAX) || changed>uint64_t(INT64_MAX))
                throw cw::DecodeError(cw::ErrorCode::Malformed,"malformed allegiance response");
            cw::Error validated{0,cw::ErrorCode::Unavailable,campaign,{},reason.empty()?"Allegiance response":reason};
            (void)cw::encode(cw::Response{validated});
            if (status) campaignError_=std::move(validated);
            else getPlayerCampaignStatus(campaign);
            return;
        }
        auto kind=cw::ResponseKind::Error; auto expected=Msg::CrusadesError;
        if (f.kind==Msg::CrusadesCampaignList) {kind=cw::ResponseKind::List;expected=Msg::CrusadesListCampaigns;}
        if (f.kind==Msg::CrusadesCampaignSnapshot) {kind=cw::ResponseKind::Snapshot;expected=Msg::CrusadesGetSnapshot;}
        if (f.kind==Msg::CrusadesPlayerStatus) {kind=cw::ResponseKind::PlayerStatus;expected=Msg::CrusadesGetPlayerStatus;}
        if (f.kind==Msg::CrusadesBattleStatus) {kind=cw::ResponseKind::BattleStatus;expected=Msg::CrusadesGetBattleStatus;}
        if (f.kind==Msg::CrusadesMatchmakingStatus) {kind=cw::ResponseKind::Matchmaking;expected=Msg::CrusadesGetMatchmaking;}
        if (f.kind==Msg::CrusadesTerritoryHistory) {kind=cw::ResponseKind::TerritoryHistory;expected=Msg::CrusadesGetTerritoryHistory;}
        if (f.kind==Msg::CrusadesReplayChunk) {kind=cw::ResponseKind::ReplayChunk;expected=Msg::CrusadesGetReplayChunk;}
        auto response=cw::decodeResponse(kind,f.payload);
        const auto id=std::visit([](const auto& v){return v.requestId;},response);
        auto pending=campaignPending_.find(id);
        std::string target;
        if (id) {
            if (pending==campaignPending_.end()) return;
            target=pending->second.target;
            const bool matchOperation = kind == cw::ResponseKind::Matchmaking &&
                (pending->second.kind == Msg::CrusadesSearchBattle || pending->second.kind == Msg::CrusadesCancelSearch);
            if (kind!=cw::ResponseKind::Error && pending->second.kind!=expected && !matchOperation)
                throw cw::DecodeError(cw::ErrorCode::Malformed,"campaign response kind does not match request");
        }
        if (auto* value=std::get_if<cw::TerritoryHistory>(&response)) {
            if (!id) return;
            const auto request = pending->second.history;
            if (!request || value->campaignId != request->campaignId || value->territory != request->territory || value->entries.size() > request->limit)
                throw cw::DecodeError(cw::ErrorCode::Malformed,"history response target/limit mismatch");
            const HistoryKey key{value->campaignId,value->territory};
            campaignPending_.erase(id);
            const auto latest = campaignHistoryRequests_.find(key); if (latest == campaignHistoryRequests_.end() || latest->second != id) return;
            const auto previousRecord = [&](const std::string& battle) -> const cw::HistoryBattle* {
                const auto record = campaignHistoryRecords_.find({value->campaignId,battle});
                if (record != campaignHistoryRecords_.end()) return &record->second;
                // A page can outlive its entry in the smaller immutable-record
                // cache. Keep every currently visible record protected as well.
                for (const auto& [pageKey,page] : campaignHistory_) {
                    if (pageKey.first != value->campaignId) continue;
                    for (const auto& entry : page.entries) if (entry.battleId == battle) return &entry;
                }
                return nullptr;
            };
            for (const auto& entry : value->entries) {
                if (request->cursor && !(entry.recordedUnix < request->cursor->recordedUnix ||
                    (entry.recordedUnix == request->cursor->recordedUnix && entry.battleId < request->cursor->battleId)))
                    throw cw::DecodeError(cw::ErrorCode::Malformed,"history response precedes requested cursor");
                const auto* old = previousRecord(entry.battleId);
                if (old) {
                    // Availability may disappear through retention. Verified terminal
                    // metadata and an available replay's immutable identity may not change.
                    cw::TerritoryHistory a{1,value->campaignId,value->territory,{*old},{}};
                    cw::TerritoryHistory b{1,value->campaignId,value->territory,{entry},{}};
                    if (!a.entries[0].replay || !b.entries[0].replay) { a.entries[0].replay.reset(); b.entries[0].replay.reset(); }
                    if (cw::encode(cw::Response{a}) != cw::encode(cw::Response{b})) throw cw::DecodeError(cw::ErrorCode::Malformed,"conflicting archive record");
                }
            }
            for (const auto& entry : value->entries) {
                const auto recordKey=std::make_pair(value->campaignId,entry.battleId);
                auto saved = entry;
                const auto* previous = previousRecord(entry.battleId);
                if (!saved.replay && previous) saved.replay = previous->replay;
                if (!campaignHistoryRecords_.count(recordKey) && campaignHistoryRecords_.size() >= 512) campaignHistoryRecords_.erase(campaignHistoryRecords_.begin());
                campaignHistoryRecords_[recordKey]=std::move(saved);
            }
            if (!campaignHistory_.count(key) && campaignHistory_.size() >= 64) campaignHistory_.erase(campaignHistory_.begin());
            campaignHistory_[key]=std::move(*value);
        } else if (auto* value=std::get_if<cw::ReplayChunk>(&response)) {
            if (!id) return;
            campaignPending_.erase(id); acceptCampaignReplayChunk(*value);
        } else if (auto* value=std::get_if<cw::Snapshot>(&response)) {
            if (id && value->campaignId!=target) throw cw::DecodeError(cw::ErrorCode::Malformed,"campaign response target mismatch");
            if (!id && value->campaignId!=campaignSubscription_) return;
            if(id)campaignPending_.erase(id);
            const auto applied=campaignReplica_.apply(*value);
            if(applied==cw::ApplyResult::Stale || applied==cw::ApplyResult::Conflict) {
                campaignError_=cw::Error{id,cw::ErrorCode::StaleRevision,value->campaignId,value->revision,"Rejected stale or conflicting campaign snapshot"};
                if(applied==cw::ApplyResult::Stale)refreshCampaignOnce(value->campaignId);
            } else campaignRefreshAttempt_.clear();
        } else if (auto* value=std::get_if<cw::CampaignList>(&response)) {
            if(!id)return;
            campaignList_=std::move(*value); campaignPending_.erase(id);
        } else if (auto* value=std::get_if<cw::PlayerStatus>(&response)) {
            if (id && value->campaignId!=target) throw cw::DecodeError(cw::ErrorCode::Malformed,"campaign response target mismatch");
            if (!id && value->campaignId!=campaignSubscription_) return;
            if(id)campaignPending_.erase(id);
            if(!campaignSubscription_.empty() && value->campaignId!=campaignSubscription_)return;
            if(campaignPlayer_ && campaignPlayer_->campaignId==value->campaignId &&
                (campaignPlayer_->campaignRevision>value->campaignRevision ||
                 (campaignPlayer_->allegiance && (!value->allegiance ||
                  campaignPlayer_->allegiance->revision>value->allegiance->revision ||
                  (campaignPlayer_->allegiance->revision==value->allegiance->revision &&
                   (campaignPlayer_->allegiance->alliance!=value->allegiance->alliance ||
                    campaignPlayer_->allegiance->joinedUnix!=value->allegiance->joinedUnix ||
                    campaignPlayer_->allegiance->changedUnix!=value->allegiance->changedUnix)))))) {
                campaignError_=cw::Error{id,cw::ErrorCode::StaleRevision,value->campaignId,value->campaignRevision,"Rejected stale player campaign status"};
                return;
            }
            campaignPlayer_=std::move(*value);
        } else if (auto* value=std::get_if<cw::BattleStatus>(&response)) {
            if(id && value->battleId!=target)throw cw::DecodeError(cw::ErrorCode::Malformed,"battle response target mismatch");
            if(id)campaignPending_.erase(id);
            const auto old=campaignBattles_.find(value->battleId);
            if(old!=campaignBattles_.end()) {
                const auto& before=old->second;
                const bool identityChanged=before.campaignId!=value->campaignId || before.campaignRevision!=value->campaignRevision ||
                    before.territory!=value->territory || before.mapIdentifier!=value->mapIdentifier || before.expiresUnix!=value->expiresUnix ||
                    (before.roomId && value->roomId && before.roomId!=value->roomId);
                auto a=before,b=*value;a.requestId=b.requestId=0;
                if(identityChanged || uint8_t(before.status)>uint8_t(value->status) ||
                    (before.status>=cw::BattlePhase::Cancelled && cw::encode(cw::Response{a})!=cw::encode(cw::Response{b}))) {
                    campaignError_=cw::Error{id,cw::ErrorCode::StaleRevision,value->campaignId,{},"Rejected regressing or conflicting battle status"};return;
                }
            }
            if(old==campaignBattles_.end() && campaignBattles_.size()>=64)campaignBattles_.erase(campaignBattles_.begin());
            if (value->roomId) {
                if (campaignRoomBindings_.size() >= 64 && !campaignRoomBindings_.count(value->roomId))
                    campaignRoomBindings_.erase(campaignRoomBindings_.begin());
                campaignRoomBindings_[value->roomId] = value->battleId;
            }
            campaignBattles_[value->battleId]=std::move(*value);
        } else if (auto* value=std::get_if<cw::MatchmakingStatus>(&response)) {
            if (id && value->campaignId != target) throw cw::DecodeError(cw::ErrorCode::Malformed,"matchmaking response target mismatch");
            if (id) campaignPending_.erase(id);
            if (value->campaignId != campaignSubscription_) return;
            const auto* snapshot = campaignReplica_.find(value->campaignId);
            if ((snapshot && value->campaignRevision < snapshot->revision) ||
                (campaignMatchmaking_ && (value->campaignRevision < campaignMatchmaking_->campaignRevision ||
                    value->generation < campaignMatchmaking_->generation))) {
                campaignError_ = cw::Error{id,cw::ErrorCode::StaleRevision,value->campaignId,value->campaignRevision,"Rejected stale matchmaking status"};
                return;
            }
            // The generation advances for every personal or aggregate change,
            // including persisted revision changes. Equal generations are immutable.
            if (campaignMatchmaking_ && value->generation == campaignMatchmaking_->generation) {
                auto before = *campaignMatchmaking_, after = *value; before.requestId = after.requestId = 0;
                if (cw::encode(cw::Response{before}) != cw::encode(cw::Response{after})) {
                    campaignError_ = cw::Error{id,cw::ErrorCode::StaleRevision,value->campaignId,value->campaignRevision,"Rejected conflicting matchmaking status"};
                    return;
                }
            }
            campaignMatchmaking_ = std::move(*value);
        } else if (auto* value=std::get_if<cw::Error>(&response)) {
            if (id && id == campaignReplayRequest_) failCampaignReplay(value->reason);
            if(id)campaignPending_.erase(id);
            campaignError_=*value;
            if(value->code==cw::ErrorCode::StaleRevision)refreshCampaignOnce(value->campaignId);
        }
    } catch (const std::exception&) {
        if (f.kind == Msg::CrusadesReplayChunk && f.payload.size() < 6 && campaignReplayDownload_.state == CampaignReplayState::Downloading) {
            const auto id = campaignReplayRequest_; failCampaignReplay("Replay transfer failed validation"); campaignPending_.erase(id);
        }
        if(f.kind!=Msg::CrusadesBattleResult && f.kind!=Msg::CrusadesAllegianceResult && f.payload.size()>=6) {
            Reader header(f.payload.data(),f.payload.size());(void)header.u8();(void)header.u8();const auto failedId=header.u32();
            if (failedId == campaignReplayRequest_ || (f.kind == Msg::CrusadesReplayChunk && campaignReplayDownload_.state == CampaignReplayState::Downloading && !campaignReplayRequest_)) failCampaignReplay("Replay transfer failed validation");
            campaignPending_.erase(failedId);
        }
        // No partial publication, disconnection or automatic malformed-response loop.
        campaignError_=cw::Error{0,cw::ErrorCode::Malformed,{}, {},"Malformed campaign response"};
    }
}

// ---- account login ---------------------------------------------------------
//
// The exchange, once the server answers our Hello with AuthRequired:
//
//   C->S  AuthBegin      username, client nonce
//   S->C  AuthChallenge  does that account exist?, salt, iterations, server nonce
//   C->S  AuthProof      proof of the password       (existing account)
//     or  AuthRegister   the verifiers to store      (new account)
//   S->C  AuthResult     outcome + the server's own signature
//   S->C  Welcome        ... and we are in the lobby
//
// See src/net/auth.h for what each value is and why the password itself never
// appears anywhere in it.

void MpClient::failAuth(const std::string& why) {
    auth_ = Auth::Failed;
    if (err_.empty()) err_ = why;
    // Settle the key-derivation worker before touching anything it writes. This
    // can cost the tail of one PBKDF2 run, which is a fine price on a path that
    // has already decided to hang up -- and the alternative is a live data race
    // between the worker filling keys_ and us abandoning it.
    if (derive_.joinable()) derive_.join();
    crypto::wipe(loginPass_);
    crypto::wipe(keys_.clientKey.data(), keys_.clientKey.size());
    crypto::wipe(keys_.serverKey.data(), keys_.serverKey.size());
    // Hang up rather than sit on a connection we can never use. The error is
    // already recorded, so the socket closing cannot overwrite why.
    conn_.closeNow();
    state_ = State::Done;
}

void MpClient::sendAuthBegin() {
    if (loginUser_.empty()) {
        failAuth("this server requires an account -- sign in from the multiplayer menu");
        return;
    }
    std::string why;
    if (!auth::validUsername(loginUser_, &why)) { failAuth(why); return; }
    try {
        pend_ = PendingAuth{};
        pend_.clientNonce = crypto::randomVec(auth::kNonceLen);
    } catch (const std::exception& e) {
        failAuth(std::string("cannot sign in: ") + e.what());
        return;
    }
    auth_ = Auth::Pending;
    Writer w;
    w.str(loginUser_);
    w.bytes(pend_.clientNonce);
    send(Msg::AuthBegin, w);
}

void MpClient::onAuthChallenge(Reader& r) {
    if (auth_ != Auth::Pending) { failAuth("unexpected login challenge"); return; }
    // Exactly one challenge per login. Without this a server (or anything posing
    // as one) can send challenges in a loop: each spawns a fresh 600k-iteration
    // derivation, and startDerive joins the previous worker ON THE CALLER'S
    // THREAD -- so a handful of 60-byte frames freeze the client for seconds.
    if (pend_.challenged) { failAuth("the server sent a second login challenge"); return; }
    pend_.newAccount = r.u8() != 0;
    pend_.salt = r.bytes();
    pend_.iters = r.u32();
    pend_.serverNonce = r.bytes(auth::kNonceLen);
    if (!r.ok || pend_.salt.empty()) { failAuth("malformed login challenge"); return; }
    // A hostile server could ask for an absurd work factor to hang the client, or
    // a trivial one to weaken the derivation. Neither is worth entertaining.
    if (pend_.iters < 1000 || pend_.iters > 5000000) {
        failAuth("the server asked for an unreasonable password work factor");
        return;
    }
    if (pend_.newAccount) {
        // Creating the account: hold the password to our own rules, not the
        // server's, so a weak one is refused before it is ever committed to.
        std::string why;
        if (!auth::validPassword(loginPass_, &why)) { failAuth(why); return; }
    }
    pend_.challenged = true;
    startDerive();
}

void MpClient::startDerive() {
    if (derive_.joinable()) derive_.join();
    deriveDone_.store(false, std::memory_order_relaxed);
    std::string pass = loginPass_;                 // the worker owns its own copy
    std::vector<uint8_t> salt = pend_.salt;
    uint32_t iters = pend_.iters;
    derive_ = std::thread([this, pass, salt, iters]() mutable {
        keys_ = auth::deriveKeys(pass, salt.data(), salt.size(), iters);
        crypto::wipe(pass);                        // the lambda's copy
        deriveDone_.store(true, std::memory_order_release);   // publishes keys_
    });
    crypto::wipe(pass);   // ...and the one we copied FROM, which the lambda cloned
}

void MpClient::pumpDerive() {
    if (auth_ != Auth::Pending || pend_.proofSent) return;
    if (!deriveDone_.load(std::memory_order_acquire)) return;
    if (derive_.joinable()) derive_.join();
    pend_.proofSent = true;
    crypto::wipe(loginPass_);                      // done with it, for good

    if (pend_.newAccount) {
        // Registration. The server gets the two verifiers and nothing else -- it
        // could not reconstruct the password from them if it wanted to.
        auth::Credential c = auth::makeCredential(keys_, pend_.salt.data(),
                                                  pend_.salt.size(), pend_.iters);
        Writer w;
        w.bytes(c.storedKey.data(), c.storedKey.size());
        w.bytes(c.serverKey.data(), c.serverKey.size());
        send(Msg::AuthRegister, w);
    } else {
        std::vector<uint8_t> am = auth::authMessage(loginUser_, pend_.clientNonce,
                                                    pend_.serverNonce, pend_.salt, pend_.iters);
        crypto::Digest proof = auth::clientProof(keys_, am);
        Writer w;
        w.bytes(proof.data(), proof.size());
        send(Msg::AuthProof, w);
    }
}

void MpClient::onAuthResult(Reader& r) {
    auto status = AuthStatus(r.u8());
    std::vector<uint8_t> sig = r.bytes();
    std::string msg = r.str();
    if (!r.ok) { failAuth("malformed login result"); return; }

    if (status != AuthStatus::Ok && status != AuthStatus::Created) {
        // The failure statuses are the only ones a server may legitimately send
        // before a challenge (Throttled / BadUsername / ServerError all come
        // straight out of the AuthBegin handler), so they are answered here,
        // ahead of the state guard below.
        failAuth(msg.empty() ? "the server refused the login" : msg);
        return;
    }
    // Only entertain a SUCCESS for an exchange we actually completed, and only
    // the KIND of success we asked for. Without this, a machine posing as the
    // server answers our AuthBegin with AuthResult{Ok} and never sends a
    // challenge at all -- leaving pend_ empty and keys_ still zero-filled, so the
    // signature it has to forge is HMAC over an all-zero key, which anyone can
    // compute. Requiring proofSent also means the derivation thread was joined,
    // so reading keys_ here is safe; it is otherwise a live data race against the
    // worker when a challenge and a result arrive in the same TCP segment.
    if (auth_ != Auth::Pending || !pend_.proofSent ||
        pend_.newAccount != (status == AuthStatus::Created)) {
        failAuth("the server answered a login we never made -- "
                 "do not trust this connection");
        return;
    }
    // Mutual authentication, on BOTH paths. Only something holding this account's
    // ServerKey can produce this signature. Registration is included because the
    // server signs with the key we just gave it -- an unsigned "account created"
    // would otherwise be a free way to skip this check entirely.
    {
        std::vector<uint8_t> am = auth::authMessage(loginUser_, pend_.clientNonce,
                                                    pend_.serverNonce, pend_.salt, pend_.iters);
        crypto::Digest want = auth::serverSignature(keys_.serverKey, am);
        if (sig.size() != want.size() || !crypto::equalCT(sig.data(), want.data(), want.size())) {
            failAuth("the server failed to prove it knows this account -- "
                     "do not trust this connection");
            return;
        }
    }
    account_ = loginUser_;
    auth_ = status == AuthStatus::Created ? Auth::Created : Auth::Ok;
    crypto::wipe(keys_.clientKey.data(), keys_.clientKey.size());
    crypto::wipe(keys_.serverKey.data(), keys_.serverKey.size());
    // The server follows this with a Welcome, which moves us into the lobby.
}

static void readSlots(Reader& r, RoomView& v) {
    v.id = r.u32();
    v.name = r.str();
    v.mapId = r.str();
    v.mission = r.str();
    v.opts.crusades = r.u8(); v.opts.forfeitSelfDestruct = r.u8();
    v.opts.overridePolicy = r.u8();
    v.opts.speed = r.u8(); v.opts.speedUnlock = r.u8();
    v.opts.unitCap = uint16_t(r.u32());
    v.opts.monarchExpendable = r.u8();
    v.opts.stressTest = r.u8();
    v.opts.fogExplored = r.u8();
    v.opts.benchmark = r.u8();
    v.opts.randomStarts = r.u8();
    v.opts.doubleSight = r.u8();
    v.hostId = r.u32();
    v.mapsReady = r.u8() != 0;
    for (int i = 0; i < kMaxSlots; ++i) {
        SlotInfo& s = v.slots[i];
        s.type = r.u8(); s.faction = r.u8(); s.color = r.u8(); s.team = r.u8();
        s.ready = r.u8(); s.aiLevel = r.u8(); s.name = r.str();
    }
}

void MpClient::onFrame(const Frame& f) {
    lastRecvMs_ = nowMs();
    Reader r(f.payload.data(), f.payload.size());
    if (f.kind == Msg::CrusadesCampaignList || f.kind == Msg::CrusadesCampaignSnapshot ||
        f.kind == Msg::CrusadesPlayerStatus || f.kind == Msg::CrusadesBattleStatus ||
        f.kind == Msg::CrusadesError || f.kind == Msg::CrusadesMatchmakingStatus || f.kind == Msg::CrusadesTerritoryHistory ||
        f.kind == Msg::CrusadesReplayChunk || f.kind == Msg::CrusadesBattleResult ||
        f.kind == Msg::CrusadesAllegianceResult) { campaignFrame(f); return; }
    switch (f.kind) {
        case Msg::Welcome:
            myId_ = r.u32();
            name_ = r.str();
            // A Welcome is NOT a login. If we came here with credentials, the
            // only way in is a verified AuthResult -- otherwise a machine posing
            // as the server could skip the whole exchange and answer our Hello
            // with a bare Welcome, and we would happily play on a connection that
            // proved nothing.
            if (!loginUser_.empty() && auth_ != Auth::Ok && auth_ != Auth::Created) {
                failAuth("the server let us in without checking the account -- "
                         "do not trust this connection");
                return;
            }
            // The Welcome name is authoritative: names match case-insensitively,
            // so someone who typed "CURTIS" is signed in to "curtis" and should be
            // shown as its owner spelled it, not as they happened to type it.
            if (auth_ != Auth::None && !name_.empty()) account_ = name_;
            state_ = State::Lobby;
            if (campaignAuthenticated()) {
                listCampaigns();
                if (!campaignSubscription_.empty()) {
                    getCampaignSnapshot(campaignSubscription_);
                    getPlayerCampaignStatus(campaignSubscription_);
                }
            }
            break;
        case Msg::AuthRequired: sendAuthBegin(); break;
        case Msg::AuthChallenge: onAuthChallenge(r); break;
        case Msg::AuthResult: onAuthResult(r); break;
        case Msg::Reject:
            err_ = r.str();
            state_ = State::Done;
            break;
        case Msg::Ping: send(Msg::Pong); break;
        case Msg::Pong:
            if (pingSentMs_) {
                // Add the modelled return-path delay so RTT reflects the test link.
                float sample = float(nowMs() - pingSentMs_) + float(receiveDelayMs());
                rttMs_ = rttMs_ > 0 ? 0.7f * rttMs_ + 0.3f * sample : sample;
                pingSentMs_ = 0;
            }
            break;
        case Msg::Bye: err_ = r.str(); state_ = State::Done; break;
        case Msg::GameList: {
            games_.clear();
            uint32_t n = r.u32();
            for (uint32_t i = 0; i < n && r.ok; ++i) {
                GameInfo g;
                g.id = r.u32(); g.name = r.str(); g.mapId = r.str();
                g.players = r.u8(); g.capacity = r.u8(); g.running = r.u8();
                g.passworded = r.u8(); g.uptimeSec = r.u32();
                games_.push_back(g);
            }
            break;
        }
        case Msg::JoinResult: {
            uint8_t ok = r.u8(); uint8_t slot = r.u8(); r.str();
            if (ok) {
                // 0xFF = the host created the game as a slot-less spectator.
                room_.mySlot = (slot == 0xFF) ? -1 : int(slot);
                spectator_ = (slot == 0xFF);
                state_ = State::InRoom;
            }
            break;
        }
        case Msg::MapOffer: case Msg::MapRequest: case Msg::MapChunk: case Msg::MapError:
            mapFrame(f); break;
        case Msg::LobbyState: {
            int keep = room_.mySlot;
            readSlots(r, room_);
            room_.mySlot = keep;
            gameSpeed_ = room_.opts.speed;
            if (state_ == State::Lobby) state_ = State::InRoom;
            if (!room_.mission.empty() || mapgen::isGeneratedMapId(room_.mapId)) {
                mapReadyRoom_ = room_.id; mapStatus_ = "MAP READY";
            } else if (room_.hostId == myId_ && mapOfferedRoom_ != room_.id) {
                try {
                    if (!mapPackage_) mapPackage_ = maps::build(hpi::mountRetailRoot(mapRoot_,
                        hpi::OverridePolicy(std::min<uint8_t>(room_.opts.overridePolicy, 2))), room_.mapId);
                    send(Msg::MapOffer, maps::offer(room_.id, room_.mapId, *mapPackage_));
                    mapOfferedRoom_ = room_.id; mapStatus_ = "CHECKING MAP WITH SERVER";
                } catch (const std::exception& e) {
                    mapStatus_ = e.what(); Writer error; error.u32(room_.id); error.str(mapStatus_);
                    send(Msg::MapError, error); mapOfferedRoom_ = room_.id;
                }
            }
            break;
        }
        case Msg::SpeedUpdate: { uint8_t s = r.u8(); if (r.ok) { gameSpeed_ = s; room_.opts.speed = s; } break; }
        case Msg::MissionOutcome: { int8_t o = int8_t(r.u8()); if (r.ok) missionOutcome_ = o; break; }
        case Msg::Chat: {
            std::string who = r.str(), text = r.str();
            if (r.ok) chat_.push_back({who, text});
            break;
        }
        case Msg::GameStarting: {
            RoomView check; Reader header(f.payload.data(), f.payload.size()); readSlots(header, check);
            if (header.ok && check.mission.empty() && !mapgen::isGeneratedMapId(check.mapId) && mapReadyRoom_ != check.id) {
                pendingMapStart_ = f; return;
            }
            int keep = room_.mySlot;
            readSlots(r, room_);
            gameSpeed_ = room_.opts.speed;
            if (room_.mission.empty() && mapgen::isGeneratedMapId(room_.mapId)) {
                try {
                    const auto data = hpi::mountRetailRoot(mapRoot_,
                        hpi::OverridePolicy(std::min<uint8_t>(room_.opts.overridePolicy, 2)));
                    maps::saveGenerated(mapRoot_, data, room_.mapId);
                } catch (const std::exception& e) {
                    mapStatus_ = std::string("MAP SAVE FAILED: ") + e.what();
                    chat_.push_back({"SYSTEM", mapStatus_});
                    std::fprintf(stderr, "%s\n", mapStatus_.c_str());
                }
            }
            missionOutcome_ = 0;   // fresh game/replay
            slotLoaded_.fill(false);

            // Snapshot the starting slot table for a replay header, before forfeits
            // and drops rewrite the live one.
            for (int i = 0; i < kMaxSlots; ++i) startSlots_[i] = room_.slots[i];
            uint8_t mySlot = r.u8();
            startSeed_ = r.u32();
            resumeToken_ = r.u64();
            replayTicks_ = r.u32();   // bundles logged before we joined (0 = none)
            // 0xFF marks a spectator (no slot); map it to -1.
            room_.mySlot = !r.ok ? keep : (mySlot == 0xFF ? -1 : int(mySlot));
            // Record a replay if we are PLAYING this game. Started here rather than
            // when the view sets up, because the backlog a rejoin/spectate receives
            // arrives immediately after this message -- any later and the recording
            // would be missing its first ticks. Spectators (slot -1) do not record:
            // the request is that each human PLAYER keeps its own copy.
            if (room_.mySlot >= 0) startRecording(); else stopRecording();
            // Any 0xFF start is a spectator (a create-as-spectator host, or spectate()).
            spectator_ = expectingSpectate_ || (r.ok && mySlot == 0xFF);
            // Route EVERY spectator through the "set up before the state branches" path
            // (isRejoin): a spectator never reports Loaded, so the server starts sending
            // TickBundles immediately and the client's state can flip Starting->InGame
            // before the normal starting-branch runs -- which would skip world setup and
            // leave the spectator watching an empty sim. A host-spectator whose game is
            // just starting simply has an empty backlog to "replay".
            rejoin_ = expectingRejoin_ || expectingSpectate_ || spectator_;
            expectingRejoin_ = expectingSpectate_ = false;
            state_ = State::Starting;
            break;
        }
        case Msg::PlayerStatus: {
            // slot, status (0=connected 1=dropped 2=loaded), ping. Only the loaded
            // flag is consumed today -- it fills that player's loading-screen bar.
            uint8_t slot = r.u8(), status = r.u8();
            if (r.ok && slot < kMaxSlots && status == 2) slotLoaded_[slot] = true;
            break;
        }
        case Msg::Pause: paused_ = true; break;
        case Msg::Resume: paused_ = false; break;
        case Msg::TickBundle: {
            // Record the payload VERBATIM, before parsing: the server logs this exact
            // buffer, so replaying our copy reproduces the game bit-for-bit. Parsing
            // and re-serializing would risk a format drift between the two writers.
            if (recording_) replayLog_.push_back(f.payload);
            uint32_t tk = r.u32();
            Bundle bd;
            uint32_t nc = r.u32();
            for (uint32_t i = 0; i < nc && r.ok; ++i) bd.cmds.push_back(r.cmd());
            uint32_t ne = r.u32();
            for (uint32_t i = 0; i < ne && r.ok; ++i) {
                Event e; e.kind = Event::Kind(r.u8()); e.player = r.u8();
                bd.events.push_back(e);
            }
            if (r.ok) {
                uint64_t delay = receiveDelayMs();
                if (delay > 0) {   // model the link: hold, then release later
                    jitterHeld_.push_back({nowMs() + delay, tk, std::move(bd)});
                } else {
                    bundles_[tk] = std::move(bd);
                    noteBundleArrival(nowMs());
                }
                if (state_ == State::Starting) state_ = State::InGame;
            }
            break;
        }
        case Msg::Desynced: {
            uint32_t tk = r.u32(); std::string why = r.str();
            desynced_ = true;
            desyncReason_ = why + " (tick " + std::to_string(tk) + ")";
            break;
        }
        default: break;
    }
}

// ---- lobby actions --------------------------------------------------------

void MpClient::listGames() { send(Msg::ListGames); }

void MpClient::createGame(const std::string& name, const std::string& password,
                          const std::string& mapId, const GameOptions& o, uint8_t capacity,
                          bool spectate, bool priv, const std::string& mission) {
    mapPackage_.reset(); mapReceive_ = {}; mapSend_ = {};
    mapReadyRoom_ = mapOfferedRoom_ = 0; startRequested_ = false; pendingMapStart_.reset();
    mapStatus_ = "CHECKING MAP";
    if (mission.empty() && !mapgen::isGeneratedMapId(mapId)) {
        try {
            mapPackage_ = maps::build(hpi::mountRetailRoot(mapRoot_,
                hpi::OverridePolicy(std::min<uint8_t>(o.overridePolicy, 2))), mapId);
        } catch (const std::exception& e) { err_ = mapStatus_ = e.what(); return; }
    }
    Writer w; w.str(name); w.str(password); w.str(mapId); w.str(mission);
    w.u8(o.crusades); w.u8(o.forfeitSelfDestruct); w.u8(o.overridePolicy);
    w.u8(o.speed); w.u8(o.speedUnlock); w.u32(o.unitCap); w.u8(o.monarchExpendable);
    w.u8(o.stressTest); w.u8(o.fogExplored); w.u8(o.benchmark); w.u8(o.randomStarts); w.u8(o.doubleSight);
    w.u8(capacity);   // map's start-position count (the server has no map data)
    w.u8(spectate ? 1 : 0);   // host watches, taking no slot
    w.u8(priv ? 1 : 0);       // private (single-player): not in the public game list
    send(Msg::CreateGame, w);
}

void MpClient::joinGame(uint32_t id, const std::string& password) {
    Writer w; w.u32(id); w.str(password);
    send(Msg::JoinGame, w);
}

void MpClient::leaveGame() {
    send(Msg::LeaveGame);
    mapPackage_.reset(); mapReceive_ = {}; mapSend_ = {};
    mapReadyRoom_ = mapOfferedRoom_ = 0; startRequested_ = false; pendingMapStart_.reset(); mapStatus_.clear();
    room_ = RoomView{};
    state_ = State::Lobby;
}

void MpClient::setSlot(int slot, uint8_t type, uint8_t faction, uint8_t color,
                       uint8_t team, uint8_t ready, uint8_t aiLevel) {
    Writer w; w.u8(uint8_t(slot)); w.u8(type); w.u8(faction); w.u8(color); w.u8(team); w.u8(ready);
    w.u8(aiLevel);
    send(Msg::SlotUpdate, w);
}

void MpClient::setGameOptions(const GameOptions& o) {
    Writer w; w.u8(o.crusades); w.u8(o.forfeitSelfDestruct);
    w.u8(o.overridePolicy); w.u8(o.speed); w.u8(o.speedUnlock); w.u32(o.unitCap); w.u8(o.monarchExpendable);
    w.u8(o.stressTest); w.u8(o.fogExplored); w.u8(o.benchmark); w.u8(o.randomStarts); w.u8(o.doubleSight);
    send(Msg::SetGameOptions, w);
}

void MpClient::setPause(bool want) { Writer w; w.u8(want ? 1 : 0); send(Msg::SetPause, w); }

void MpClient::kick(int slot) { Writer w; w.u8(uint8_t(slot)); send(Msg::Kick, w); }

void MpClient::chat(const std::string& text, uint8_t recipients) {
    Writer w; w.str(text); w.u8(recipients); send(Msg::Chat, w);
}

void MpClient::startGame() { startRequested_ = true; }

// ---- game play ------------------------------------------------------------

void MpClient::reportLoaded(uint64_t dataHash) { Writer w; w.u64(dataHash); send(Msg::Loaded, w); }

void MpClient::rejoin(uint32_t gameId, uint64_t token) {
    expectingRejoin_ = true;
    Writer w; w.u32(gameId); w.u64(token);
    send(Msg::Rejoin, w);
}

void MpClient::spectate(uint32_t gameId, const std::string& password) {
    expectingSpectate_ = true;
    Writer w; w.u32(gameId); w.str(password);
    send(Msg::Spectate, w);
}

bool MpClient::takeBundle(uint32_t tick, Bundle& out) {
    auto it = bundles_.find(tick);
    if (it == bundles_.end()) return false;
    out = std::move(it->second);
    bundles_.erase(it);
    return true;
}

void MpClient::sendCommands(const std::vector<Command>& cmds) {
    if (cmds.empty()) return;
    Writer w; w.u32(uint32_t(cmds.size()));
    for (const auto& c : cmds) w.cmd(c);
    send(Msg::PlayerCommands, w);
}

void MpClient::sendHash(uint32_t tick, uint64_t hash) {
    // Keep our own trail while recording, so the replay carries what this client
    // actually computed at each checkpoint.
    if (recording_) hashLog_.push_back({tick, hash});
    Writer w; w.u32(tick); w.u64(hash);
    send(Msg::StateHash, w);
}

}  // namespace tak::net

namespace tak::net {
void MpClient::acceptMap(std::shared_ptr<maps::Package> package, uint32_t room) {
    mapPackage_ = std::move(package); mapReadyRoom_ = room; mapReceive_ = {};
    mapStatus_ = "MAP VERIFIED";
    try { maps::saveCache(mapRoot_, *mapPackage_); }
    catch (const std::exception& e) {
        mapStatus_ = std::string("MAP VERIFIED; SAVE FAILED: ") + e.what();
        chat_.push_back({"SYSTEM", mapStatus_}); std::fprintf(stderr, "%s\n", mapStatus_.c_str());
    }
    Writer w; w.u32(room); w.str(mapPackage_->digest); send(Msg::MapReady, w);
    if (pendingMapStart_) {
        auto start = std::move(*pendingMapStart_); pendingMapStart_.reset(); onFrame(start);
    }
}
void MpClient::mapFrame(const Frame& f) {
    try {
        Reader r(f.payload.data(), f.payload.size());
        if (f.kind == Msg::MapOffer) {
            auto id = r.u32(); auto mapId = r.str(), digest = r.str(); auto size = r.u32();
            if (!r.ok || r.p != r.end) throw std::runtime_error("invalid map offer");
            if (room_.id && room_.id != id && !expectingSpectate_ && !expectingRejoin_) return;
            if (mapReceive_.room == id && mapReceive_.digest == digest) return;
            if (mapPackage_ && mapPackage_->digest == digest) { acceptMap(mapPackage_, id); return; }
            mapReadyRoom_ = 0; mapReceive_.begin(id, size, digest);
            if (auto cached = maps::loadCache(mapRoot_, digest)) { acceptMap(std::move(cached), id); return; }
            try {
                auto local = maps::build(hpi::mountRetailRoot(mapRoot_,
                    hpi::OverridePolicy(std::min<uint8_t>(room_.opts.overridePolicy, 2))), mapId);
                if (local->digest == digest) { acceptMap(std::move(local), id); return; }
            } catch (const std::exception&) {} // Missing/different map: request the host's copy.
            Writer request; request.u32(id); send(Msg::MapRequest, request);
            mapStatus_ = "DOWNLOADING MAP 0%";
        } else if (f.kind == Msg::MapRequest) {
            auto id = r.u32();
            if (!r.ok || id != room_.id || room_.hostId != myId_ || !mapPackage_ || mapSend_.package)
                throw std::runtime_error("unexpected map upload request");
            mapSend_ = {id, 0, mapPackage_}; mapStatus_ = "UPLOADING MAP TO SERVER";
        } else if (f.kind == Msg::MapChunk) {
            Reader header(f.payload.data(), f.payload.size());
            if (header.u32() != mapReceive_.room || !mapReceive_.room) return; // left the old room
            if (mapReceive_.append(r)) {
                const auto id = mapReceive_.room;
                acceptMap(maps::decode(std::move(mapReceive_.bytes), mapReceive_.digest), id);
            } else mapStatus_ = "DOWNLOADING MAP " + std::to_string(100 * mapReceive_.bytes.size() / mapReceive_.size) + "%";
        } else if (f.kind == Msg::MapError) {
            auto id = r.u32(); auto why = r.str();
            if (r.ok && id == room_.id) { mapStatus_ = "MAP ERROR: " + why; err_ = mapStatus_; }
        }
    } catch (const std::exception& e) {
        mapStatus_ = std::string("MAP ERROR: ") + e.what(); mapReadyRoom_ = 0;
        Writer w; w.u32(mapReceive_.room ? mapReceive_.room : room_.id); w.str(mapStatus_);
        send(Msg::MapError, w); mapReceive_ = {}; mapSend_ = {}; err_ = mapStatus_;
    }
}
}
