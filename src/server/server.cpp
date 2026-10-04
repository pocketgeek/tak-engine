// takserver: the central multiplayer server (docs/multiplayer-design.md, M3).
//
// A network poll loop hosts the lobby; each running game has its own worker. Each game
// is a server-sequenced deterministic lockstep: clients send commands, the
// server buckets them per tick, closes one tick every 1/30 s, and broadcasts a
// TickBundle to every client in the game. Clients advance their sims in step.
//
// With --data the server runs a REFEREE sim (a headless World built by the same
// setupMatch the clients use) that also hosts the AI players: each tick the AI
// controllers append their orders to the bundle, and the referee's own state
// hash is the canonical one clients are checked against (with a suspicion rule
// that blames the referee if every client agrees against it). Without --data the
// server is a pure relay and clients cross-check hashes among themselves (M3).

#include "net/mappackage.h"
#include "net/overridepackage.h"
#include "server/commands.h"
#include "server/acme.h"
#include "server/roomworker.h"
#include "server/validationworker.h"
#include "util/stoptoken.h"
#include "server/roomtick.h"
#include "net/crusades.h"
#include "tnt/mapgen.h"
#include "tnt/ota.h"
#include "net/netcompat.h"
#include "util/winargv.h"
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <fstream>
#include <functional>
#include <mutex>
#include <random>
#include <set>
#include <thread>
#include <tuple>
#include <type_traits>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ai/ai.h"
#include "hpi/hpi.h"
#include "net/auth.h"
#include "util/storagequota.h"
#include "server/limits.h"
#include "net/crypto.h"
#include "server/accounts.h"
#include "server/crusades/allegiance.h"
#include "server/crusades/network.h"
#include "server/crusades/replayfiles.h"
#include "server/crusades/matchmaking.h"
#include "server/crusades/servicelease.h"
#include "tdf/tdf.h"
#include "net/conn.h"
#include "net/protocol.h"
#include "net/replayhdr.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"
#include "version.h"
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace tak::net;

namespace {

uint64_t nowMs() {   // monotonic wall-clock (tick pacing / timeouts; never hashed)
    using namespace std::chrono;
    return uint64_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

constexpr uint64_t kPingIdleMs = 5000;    // ping a quiet client after this
constexpr uint64_t kTimeoutMs = 15000;    // drop a silent seated player after this
// A SPECTATOR is display-only and non-authoritative: it holds up nobody (canAdvance
// paces to it only in an all-AI game, and even then a stale ack just slows the sim,
// never desyncs it). A busy client render loop -- e.g. a VRAM-starved frame that
// freezes for several seconds at a big supersampled resolution -- stops pumping the
// socket for that whole frame, so a 15s player timeout evicts a spectator that is
// merely hitching. Give spectators a much longer grace so a transient stall doesn't
// tear the connection down (the client's own self-timeout below is widened to match).
constexpr uint64_t kSpectatorTimeoutMs = 120000;
constexpr uint64_t kLoginDeadlineMs = 30000;
constexpr size_t kMaxClients = 256, kMaxPendingLogins = 64, kMaxClientsPerAddress = 64;
constexpr size_t kMaxServerTxBytes = 64u << 20, kMaxClientTxBytes = 4u << 20;

// Service work quotas survive socket churn. Bound identity memory as well as
// work, and retain quiet keys briefly so reconnecting cannot reset the quota.
struct WorkBudget {
    uint64_t window = 0;
    unsigned used = 0;
    bool available(uint64_t now,unsigned limit,unsigned cost=1) const {return now-window>=1000 || (used<=limit && cost<=limit-used);}
    bool take(uint64_t now, unsigned limit,unsigned cost=1) {
        if (now-window >= 1000) {window=now;used=0;}
        if (used>limit || cost>limit-used)return false;
        used+=cost;return true;
    }
};
class SharedWorkBudget {
public:
    explicit SharedWorkBudget(uint64_t retentionMs=60000):retentionMs_(retentionMs) {}
    bool take(const std::string& key,uint64_t now,unsigned limit,unsigned cost=1) {
        if(now-lastSweep_>=1000) {
            std::erase_if(keys_,[this,now](const auto& entry){return now-entry.second.window>=retentionMs_;});
            lastSweep_=now;
        }
        auto found=keys_.find(key);
        if(found==keys_.end()) {
            if(keys_.size()>=4096)return false;
            found=keys_.emplace(key,WorkBudget{}).first;
        }
        return found->second.take(now,limit,cost);
    }
private:
    std::map<std::string,WorkBudget> keys_;
    uint64_t lastSweep_ = 0;
    uint64_t retentionMs_;
};
// per-client command cap per tick lives in protocol.h: the CLIENT has to know it
// too, so it can spread a big batch instead of having the excess discarded here.
using tak::net::kCmdCapPerTick;
// The server never runs more than this many ticks ahead of the slowest seated human
// player: a player whose machine can't sustain the game speed gracefully SLOWS the
// whole match to what it can handle (the actual speed drops below the requested one)
// instead of desyncing or being dropped. Generous enough not to throttle normal play
// (hashes are reported every kHashPeriod ticks, plus the client's jitter buffer).
constexpr uint32_t kMaxLeadTicks = 120;
// How long to wait before re-checking a room that is flow-controlled (waiting on
// a lagging player). Rebasing its deadline to `now` instead leaves the poll
// timeout at 0, so the server spins at full CPU while making no progress.
constexpr uint64_t kFlowRetryMs = 5;
// How many commands one client may have waiting. A few ticks' worth:
// enough that an honest burst (a big selection, or two render steps landing in one
// server tick) always survives, small enough that a flooder cannot make the server
// hold an unbounded queue on its behalf.
constexpr size_t kCmdQueueMax = size_t(tak::net::kCmdQueueCap);
uint64_t kGraceMs = 300000;     // hold a dropped slot this long (5 min)
uint64_t kPauseBudgetMs = 120000;  // total auto-pause a player may cause

// A resume token (unguessable per session; not sim state, so no determinism or
// cross-platform concern). std::random_device is entropy where available; mix in
// the clock so a deterministic random_device (some libstdc++ builds) still varies.
uint64_t randToken() {
    std::random_device rd;
    std::mt19937_64 g((uint64_t(rd()) << 32) ^ rd() ^ nowMs());
    uint64_t v = g();
    return v ? v : 1;
}

struct Client {
    Conn conn;
    uint32_t id = 0;
    std::string name;
    // Signing in: the exchange sits between the Hello and the Welcome, so a
    // connection on a server that requires an account passes through Auth before
    // it can do anything at all. See src/net/auth.h.
    struct PendingAuth {
        std::string user;                     // as typed, for display and transcript
        std::vector<uint8_t> clientNonce, serverNonce, salt;
        uint32_t iters = 0;
        bool newAccount = false;              // no such account: this is a registration
        bool challenged = false;              // guards against a second AuthBegin
    } pendAuth;
    std::string account;                      // set once signed in ("" = open server)
    std::string peer = "?";                   // numeric address, for rate limiting
    enum State { Handshake, Auth, Lobby, InGame } state = Handshake;
    uint32_t roomId = 0;
    int slot = -1;
    uint64_t lastRecvMs = 0;
    uint64_t connectedMs = 0;
    uint64_t lastPingMs = 0;
    bool loaded = false;
    uint32_t mapReadyRoom = 0, mapOfferedRoom = 0;
    // Strategic subscription and query budget are connection-local metadata.
    // They never feed room command queues, referee state or tick/replay logs.
    bool campaignProtocol = false;
    std::string campaignSubscription;
    uint64_t campaignRevision = tak::net::crusades::kUnknownRevision;
    uint64_t campaignActivityVersion = 0;
    std::string campaignMatchSubscription;
    std::optional<tak::net::crusades::MatchmakingStatus> campaignMatchBoard;
    uint64_t campaignReadWindow = 0;
    unsigned campaignReads = 0;
    uint64_t campaignReplayWindow = 0;
    unsigned campaignReplayReads = 0;
    WorkBudget campaignWork, campaignReplayWork;
    uint64_t campaignLimitedWindow = 0;
    bool campaignLimitNotified = false;
    tak::net::maps::Receiver overrideReceive;
    tak::net::maps::Sender overrideSend;
    std::string overrideReady,overrideOffered;
    uint64_t overrideStarted=0,overrideProgress=0;
    tak::net::maps::Receiver mapReceive;
    tak::net::maps::Sender mapSend;

    uint32_t ackTick = 0;   // latest tick this client reported a hash for (flow control)
    // kCmdCapPerTick is a per-TICK budget, so it has to be tracked per client
    // across messages. Clamping each message independently caps nothing: the
    // server drains every readable message before closing the tick and appends
    // each one's commands to the same pending list, so N messages buy N*cap
    // commands -- more sim work for everyone, and a bundle that can grow past
    // the 256 KiB frame limit every receiver enforces, disconnecting them.
    // EVERY command from this client lands here on arrival, and the per-tick drain
    // in closeTick is the ONLY thing that feeds a room's pending list. One queue,
    // one budget, one scheduling rule.
    //
    // The first cut had two entry points -- accept up to the budget straight into
    // r.pending, defer the rest here -- and that was wrong three ways at once: a
    // command arriving next tick jumped ahead of older deferred ones (a stale Move
    // overriding a newer Stop), reception and the drain each granted a fresh
    // 64-command allowance so one client could put 128 in a tick, and the drain
    // ignored TAK_SRV_DELAY so deferred commands outran delayed ones.
    std::deque<Command> cmdQueue;
    uint64_t cmdDropped = 0;     // past the queue cap; logged, not silent
    // Catch-up streaming. A resuming or spectating client needs every bundle
    // logged so far, which for a long game is far more than belongs in one
    // socket buffer -- pushing it all at once put the entire replay in memory
    // and handed one client an unbounded write queue. Instead we hold a cursor
    // and feed it as the buffer drains. Every live bundle is appended to r.log
    // too, so the cursor catches up to the present by itself; until it does,
    // this client is skipped by the live TickBundle broadcast so the stream
    // cannot go out of order.
    WorkBudget lobbyWork,uploadWork,commandBytes,commandCount;
    uint64_t commandLogAt=0, validationId=0;
    std::optional<Frame> deferredMapOffer;
    uint64_t mapReceiveStarted=0,mapReceiveProgress=0;
    bool replaying = false;
    size_t replayPos = 0;        // next index into Room::log to send
};

// pausePlayer sentinel for a player-REQUESTED pause: distinct from any real slot,
// so the disconnect budget sweep and the rejoin-resume path both ignore it.
static constexpr int kPauseByRequest = -2;

using tak::srv::TickResult;

    struct DataSet {
        tak::hpi::Vfs vfs;
        tak::sim::TypeRegistry reg, regCb;
        bool haveCb = false;
        uint64_t hash = 0;
        bool built = false;
    };

struct Room {
    std::unique_ptr<DataSet> overrideData; // outlives the referee and room VFS
    uint32_t id = 0;
    std::string name, password, mapId;
    std::shared_ptr<tak::net::maps::Package> mapPackage;
    std::unique_ptr<tak::hpi::Vfs> mapVfs;

    // Persistent Crusades identity is server-owned, never part of CreateGame.
    std::string campaignBattleId, campaignLaunchToken, campaignRoomToken, campaignId;
    std::string campaignAccounts[2];
    int64_t campaignExpires = 0;
    uint32_t campaignTerritory = 0;
    bool campaignCancelled = false;
    std::optional<tak::srv::crusades::VerifiedMatchResult> campaignResult;
    std::optional<tak::srv::crusades::ResultOutcome> campaignFault;
    uint64_t campaignResultDue = 0;
    bool campaignResultRecorded = false;
    bool campaignResigned[2] = {}, campaignDisconnected[2] = {}, campaignTimeout[2] = {}, campaignLoaded[2] = {};
    std::string mission;           // campaign mission stem (empty = ordinary skirmish/MP)
    GameOptions opts;
    uint32_t hostId = 0;
    SlotInfo slots[kMaxSlots];
    int slotClient[kMaxSlots];      // client id in each slot, -1 = none/ai/open
    std::vector<uint32_t> spectators;   // watching, not seated (no slot, no hash)
    bool running = false;
    bool priv = false;             // private (single-player): hidden from the game list
    int cap = kMaxSlots;            // map capacity (from the host's CreateGame)
    uint64_t createdMs = 0;
    // Per-game RNG seed, broadcast in GameStarting so every peer derives the same
    // arrangement from it. Rolled per ROOM rather than computed from the room id: a
    // single-player game launches a fresh private server, whose ids restart at 1, so
    // `0x7a6b0000 + id` handed every SP game the identical seed -- and "Random Start
    // Locations" then dealt the identical "random" layout every single time.
    uint32_t seed = 0;
    // running state
    uint32_t tick = 0;                          // next tick to close
    std::vector<Command> pending;               // commands for the next tick
    std::map<uint32_t, std::vector<Command>> pendingAt;  // server input-delay: cmds bucketed by future tick
    std::vector<Event> pendingEvents;           // sequenced events for the next tick
    uint64_t nextTickMs = 0;                    // wall deadline for the next tick
    // hash cross-check: tick -> (clientId -> hash)
    std::map<uint32_t, std::map<uint32_t, uint64_t>> hashes;
    std::map<uint32_t, bool> desyncFlagged;     // clientId -> already told
    // referee sim (server-side, drives server-hosted AI + a canonical hash)
    std::unique_ptr<tak::sim::World> ref;
    const tak::sim::TypeRegistry* reg = nullptr;   // the balance this game uses
    std::vector<tak::ai::Controller> ai;        // one per AI slot
    std::map<uint32_t, uint64_t> refHash;       // tick -> referee hash (bounded ring)
    // The replay's hash trail, kept SEPARATELY from refHash. refHash is a ring pruned
    // to 300 entries because desync checking only ever looks a few ticks back -- but
    // writing the replay from it meant a long game shipped checkpoints for its last
    // few seconds and nothing else, so a divergence early on could not be located,
    // which is the one thing the trail is for. This is append-only and sampled
    // coarsely: an hour at 30Hz costs a few thousand entries, about 40KB.
    std::vector<std::pair<uint32_t, uint64_t>> replayChecks;
    bool refSuspect = false;                    // referee itself suspected desynced
    int8_t missionOutcomeSent = 0;              // campaign result already broadcast (0 = none)
    // durability (M5): the full bundle log for reconnect/replay, per-slot resume
    // tokens, and drop-hold / auto-pause state.
    size_t logBytes=0;
    bool resourceLimited=false;
    std::vector<std::vector<uint8_t>> log;      // serialized TickBundle payload per tick
    SlotInfo startSlots[kMaxSlots];             // slot config at game start (for the replay)
    uint64_t slotToken[kMaxSlots] = {};         // resume token per human slot (0 = none)
    bool slotDropped[kMaxSlots] = {};           // slot held after a mid-game disconnect
    uint64_t graceDeadline[kMaxSlots] = {};     // forfeit time for a dropped slot
    bool paused = false;                        // a drop paused the game (ticks stop)
    int pausePlayer = -1;                       // which dropped player paused it
    uint64_t pauseStartMs = 0;                  // when the current pause began
    uint64_t pauseBudgetMs[kMaxSlots] = {};     // remaining pause budget per player
    std::shared_ptr<tak::net::overrides::Package> overridePackage;
    std::future<TickResult> tickJob;
    std::array<bool,kMaxSlots> defeated{}; // last completed tick; safe during a worker tick
    std::string simulationError;
    // Last member: join on shutdown before destroying the World/AI it owns.
    // Normal room teardown waits for readiness without blocking the network loop.
    std::unique_ptr<tak::srv::RoomWorker> worker;
    Room() { for (int i = 0; i < kMaxSlots; ++i) slotClient[i] = -1; }
    int capacity() const { return cap; }
    int usedSlots() const {
        int n = 0;
        for (int i = 0; i < kMaxSlots; ++i) if (slots[i].type == 1 || slots[i].type == 2) ++n;
        return n;
    }
};

class Server {
public:
    ~Server() {for(auto& job:validations_)job.stop.request_stop();}
    void setReplayDir(const std::string& d) { replayDir_ = d; }
    // Pin every game's RNG seed (--seed). Games are otherwise seeded randomly, which
    // is what makes "Random Start Locations" actually random -- but it also makes a
    // harness run unrepeatable, and the headless --mpai check exists precisely to
    // produce the same state hash twice. Not for a real server.
    void setFixedSeed(uint32_t v) { fixedSeed_ = v; }
    void setNoAuth() { requireAuth_ = false; }
#ifndef NDEBUG
    void allowBenchmarks() {testWork_=true;}
#endif
    void setAcme(std::shared_ptr<tak::srv::AcmeCertificates> acme) {acme_=std::move(acme);tlsContext_=acme_->context();}
    void setTls(std::shared_ptr<tak::net::TlsContext> context) {tlsContext_=std::move(context);}
    void setLimits(tak::srv::Limits limits) {limits_=limits;}
    void closeRegistration() { registrationOpen_=false; }
    void setLoopbackOnly() { loopbackOnly_ = true; }
    // Load (or start) the account file. Returns false with `err` set if it exists
    // but cannot be read -- starting anyway would mean running an open server
    // while looking like a closed one.
    bool loadAccounts(const std::string& path, std::string& err) {
        return accounts_.load(path, &err);
    }
    void enableCrusades(const std::filesystem::path& database,
                        const std::filesystem::path& definition) {
        if (!requireAuth_) throw std::runtime_error("Crusades requires account authentication");
        std::optional<tak::srv::crusades::CampaignDefinition> bootstrap;
        if(!definition.empty()) {
            bootstrap=tak::srv::crusades::loadDefinition(definition);
            tak::srv::crusades::validateCampaignNetworkState(*bootstrap,tak::srv::crusades::makeInitialState(*bootstrap));
        }
        campaignLease_ = std::make_unique<tak::srv::crusades::CampaignServiceLease>(database);
        crusades_ = std::make_unique<tak::srv::crusades::CampaignStore>(campaignLease_->databasePath());
        // Validate every page, including campaigns other than the configured
        // bootstrap, before admitting clients or auditing startup mutations.
        std::string after;
        for(;;) {
            const auto page=crusades_->campaignIds(after,64);
            for(const auto& id:page.ids) {
                const auto existing=crusades_->load(id);
                tak::srv::crusades::validateCampaignNetworkState(existing.definition,existing.state,existing.rules);
            }
            if(!page.truncated)break;
            after=page.ids.back();
        }
        const auto unixNow=std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        campaignSession_ = tak::crypto::toHex(tak::crypto::randomVec(32));
        campaignReplayDir_ = replayDir_.empty() ? std::filesystem::absolute(database).parent_path()/"crusades-replays"
                                             : std::filesystem::absolute(std::filesystem::u8path(replayDir_));
        if (bootstrap) {
            const auto& campaign = *bootstrap;
            if (!crusades_->hasCampaign(campaign.id())) {
                crusades_->adminStart(campaign,{}, {"takserver","server configured campaign definition",-1,unixNow});
            } else {
                const auto existing = crusades_->load(campaign.id());
                bool same = campaign.displayName() == existing.definition.displayName() &&
                    campaign.territories().size() == existing.definition.territories().size();
                for (const auto& [id,t] : campaign.territories()) {
                    const auto* old = existing.definition.find(id);
                    same = same && old && t.id == old->id && t.displayName == old->displayName &&
                        t.nativeFaction == old->nativeFaction && t.terrain == old->terrain &&
                        t.mapIdentifier == old->mapIdentifier && t.neighbors == old->neighbors;
                }
                if (!same) throw std::runtime_error("authored campaign differs from persisted definition");
            }
        }
        crusades_->recoverInterruptedBattles(unixNow);
    }
    // `dataRoot` is never empty -- main() refuses to start without --data.
    Server(uint16_t port, const std::string& dataRoot,const std::string& mapRoot) : port_(port), dataRoot_(dataRoot),mapRoot_(mapRoot.empty()?dataRoot:mapRoot) {
        // The referee reads the retail install directly, per the ROOM's override
        // tier. Build the pure-retail set eagerly (the connection-level Hello
        // check + the common none/cosmetic game); the Full set is built lazily
        // the first time a Full game starts.
        buildDataSet(retail_, tak::hpi::OverridePolicy::None);
        aiProfile_ = tak::ai::loadProfile(retail_.vfs);
        aiNames_ = loadAiNames(retail_.vfs);
        haveCb_ = retail_.haveCb;
        std::fprintf(stderr, "takserver: loaded game data from %s (referee sim + AI%s), "
                     "retail gameplay hash %016llx\n",
                     dataRoot_.c_str(), haveCb_ ? ", +Crusades" : "",
                     (unsigned long long)retail_.hash);
    }
    int run();

private:
    uint16_t port_;
    std::string dataRoot_,mapRoot_;
    // Accounts. requireAuth_ is the default; --no-auth turns it off for a private
    // or LAN server (single-player launches one of those).
    bool requireAuth_ = true;
    bool registrationOpen_ = true;
    bool testWork_=false;
    tak::srv::Limits limits_;
    std::shared_ptr<tak::net::TlsContext> tlsContext_;
    std::shared_ptr<tak::srv::AcmeCertificates> acme_;
    bool loopbackOnly_ = false;
    tak::srv::AccountStore accounts_;
    std::unique_ptr<tak::srv::crusades::CampaignServiceLease> campaignLease_;
    std::unique_ptr<tak::srv::crusades::CampaignStore> crusades_;
    SharedWorkBudget acceptKeys_,lobbyWorkKeys_;
    WorkBudget acceptWork_,commandBytes_,commandCount_,commandLogs_;
    SharedWorkBudget commandByteKeys_,commandCountKeys_;
    WorkBudget lobbyGlobalWork_, uploadGlobalWork_, accountWriteWork_;
    bool allowLobbyWork(Client& c,const Frame& f);
    bool canRead(const Client& c,uint64_t now) const {return c.commandBytes.available(now,4u<<20) && commandBytes_.available(now,32u<<20) && ((!c.mapReceive.size && !c.overrideReceive.size) || (c.uploadWork.available(now,4096,64) && uploadGlobalWork_.available(now,16384,64)));}
    size_t mapMemory() const;
    struct Validation {
        uint64_t id; uint32_t client,room,size; bool overrides,cache;
        std::string digest,map;
        tak::StopSource stop;
        std::future<std::shared_ptr<tak::net::maps::Package>> result;
    };
    std::vector<Validation> validations_;
    uint64_t nextValidation_=1;
    tak::srv::ValidationWorker validationWorker_;
    void validatePackage(Client& c,Room& room,bool overrides,bool cache);
    void finishValidations();
    SharedWorkBudget campaignWorkKeys_, campaignReplayKeys_;
    SharedWorkBudget loginWorkKeys_{tak::srv::LoginThrottle::kForgetMs};
    WorkBudget campaignGlobalWork_, campaignGlobalReplay_, loginGlobalWork_;
    bool allowCampaignWork(Client& c,const Frame& f);
    std::string campaignSession_;
    std::filesystem::path campaignReplayDir_;
    int64_t campaignSweepTime_ = -1;
    uint64_t campaignActivityVersion_ = 1;
    uint64_t campaignMatchGeneration_ = 1;
    uint64_t campaignMatchRetryMs_ = 0, campaignMatchRetrySequence_ = 0;
    void retryCampaignMatches();
    tak::srv::crusades::MatchQueue campaignMatchQueue_;
    std::map<std::string,std::shared_ptr<tak::net::maps::Package>> campaignMapCache_;
    std::map<std::string,bool> campaignMapEligibility_;
    bool campaignMapEligible(const std::string& map);
    std::shared_ptr<tak::net::maps::Package> campaignBattleMap(const std::string& map);
    Client* campaignAvailablePeer(const std::string& account);
    bool createCampaignBattle(Client& caller, const std::string& campaign, uint32_t territory, const std::string& opponent);
    void campaignMatchmaking(Client& c, const Frame& f);
    void sendCampaignMatchmaking(Client& c, const std::string& campaign, uint32_t requestId);
    void refreshCampaignMatchmaking(Client& c);
    void pruneCampaignSearches();
    std::optional<tak::net::crusades::BattleActivity> campaignActivity(const std::string& campaign, uint32_t territory) const;
    void campaignRead(Client& c, const Frame& f);
    void campaignReplayChunk(Client& c, const Frame& f);
    std::optional<uint32_t> campaignRoomId(const std::string& battleId) const;
    void notifyCampaignBattle(const std::string& battleId);
    void refreshCampaignSnapshot(Client& c, bool force = false);
    void notifyCampaignPlayer(Client& c, const std::string& campaignId);
    void issueCampaignBattle(Client& c, const Frame& f);
    tak::srv::crusades::BattleContext campaignContext(const Room& r, bool requirePresent) const;
    void cancelCampaignBattle(Room& r);
    void closeCampaignLobby(uint32_t roomId, const char* reason);
    void finalizeCampaign(Room& room, bool abandoning = false);
    std::pair<std::string,std::string> saveCampaignReplay(Room& room);
    Writer replayBytes(Room& room);
    tak::srv::LoginThrottle throttle_;
    // A mounted data set at one override tier: the VFS, its base + Crusades
    // registries, and the gameplay-data fingerprint peers are held to.
    DataSet retail_;                   // shared immutable retail data; Full is room-local
    bool haveCb_ = false;
    tak::ai::Profile aiProfile_;
    // Per-mission build profiles (ai/<name>.txt), cached by name -- a Controller
    // holds a reference to its Profile, so these must outlive the room.
    std::map<std::string, tak::ai::Profile> missionProfiles_;
    const tak::ai::Profile& missionProfile(const tak::hpi::Vfs& vfs, const std::string& name) {
        auto it = missionProfiles_.find(name);
        if (it != missionProfiles_.end()) return it->second;
        return missionProfiles_.emplace(name, tak::ai::loadProfile(vfs, name)).first->second;
    }
    std::vector<std::string> aiNames_;   // retail's gamedata/ainames.tdf pool

    static std::vector<std::string> loadAiNames(const tak::hpi::Vfs& vfs) {
        std::vector<std::string> out;
        try {
            auto b = vfs.read("gamedata/ainames.tdf");
            auto root = tak::tdf::parseText(std::string(b.begin(), b.end()), "ainames.tdf");
            if (const auto* sec = root.child("AI_NAMES"))
                for (const auto& [k, v] : sec->values)
                    if (!v.empty()) out.push_back(v);
        } catch (const std::exception&) {}
        return out;
    }
    // Give an AI slot a random name from the pool, avoiding names already taken by
    // other AI slots in the room. Display-only (broadcast in the slot), so a plain RNG.
    void assignAiName(Room& r, int slot) {
        if (aiNames_.empty()) return;
        std::set<std::string> used;
        for (int i = 0; i < kMaxSlots; ++i)
            if (i != slot && r.slots[i].type == 2 && !r.slots[i].name.empty())
                used.insert(r.slots[i].name);
        std::vector<const std::string*> avail;
        for (const auto& n : aiNames_)
            if (!used.count(n)) avail.push_back(&n);
        if (avail.empty())
            for (const auto& n : aiNames_) avail.push_back(&n);
        static std::mt19937 rng{std::random_device{}()};
        r.slots[slot].name = *avail[rng() % avail.size()];
    }
    void buildDataSet(DataSet& ds, tak::hpi::OverridePolicy pol) {
        ds.vfs = tak::hpi::mountRetailRoot(std::filesystem::u8path(dataRoot_), pol);
        if(mapRoot_!=dataRoot_)ds.vfs.refreshMapCache(std::filesystem::u8path(mapRoot_));
        tak::sim::setupRegistry(ds.reg, ds.vfs, false);
        if (!ds.vfs.list("unitscb").empty()) {
            tak::sim::setupRegistry(ds.regCb, ds.vfs, true);
            ds.haveCb = true;
        }
        ds.hash = tak::hpi::gameplayHash(ds.vfs);
        ds.built = true;
    }
    std::string replayDir_;
    uint32_t fixedSeed_ = 0;           // --seed: 0 = roll one per game
    int listenFd_ = -1;
    uint32_t nextClientId_ = 1, nextRoomId_ = 1;
    std::unordered_map<uint32_t, std::unique_ptr<Client>> clients_;
    std::map<uint32_t, Room> rooms_;

    void onFrame(Client& c, const Frame& f);
    void handshake(Client& c, const Frame& f);
    void authMsg(Client& c, const Frame& f);
    // The two keys every login attempt is measured against: the host doing the
    // guessing, and the account being guessed at. See LoginThrottle for why they
    // are policed differently.
    std::string ipKeyFor(const Client& c) const { return "ip:" + c.peer; }
    static std::string userKeyFor(const std::string& user) {
        return "user:" + tak::auth::foldUsername(user);
    }
    // Milliseconds the caller must refuse for, or 0 to proceed.
    uint64_t loginLocked(const Client& c, const std::string& user, uint64_t now) const {
        return std::max(throttle_.lockedFor(ipKeyFor(c), now),
                        throttle_.lockedFor(userKeyFor(user), now));
    }
    void sendThrottled(Client& c, uint64_t wait) {
        sendAuthResult(c, AuthStatus::Throttled, nullptr,
                       "too many failed sign-ins -- try again in " +
                       std::to_string((wait + 999) / 1000) + "s");
        std::fprintf(stderr, "client %u (%s) login throttled (%llums left)\n",
                     c.id, c.peer.c_str(), (unsigned long long)wait);
    }
    void sendWelcome(Client& c);
    void sendAuthResult(Client& c, AuthStatus st, const tak::crypto::Digest* sig,
                        const std::string& msg);
    void lobbyMsg(Client& c, const Frame& f);
    void mapMsg(Client& c, const Frame& f);
    void acceptMap(Room& r, std::shared_ptr<tak::net::maps::Package> package);
    bool mapsReady(const Room& r) const;

    void gameMsg(Client& c, const Frame& f);

    void sendReject(Client& c, const std::string& why);
    void sendGameList(Client& c);
    void broadcastLobby(Room& r);
    void broadcastRoom(Room& r, Msg kind, const Writer& w, uint32_t exceptClient = 0);
    Room* roomOf(Client& c) { auto it = rooms_.find(c.roomId); return it == rooms_.end() ? nullptr : &it->second; }
    void dropPendingCommands(Client& c, Room& r);   // both disconnect paths
    void leaveRoom(Client& c, const char* reason);
    void tryStart(Client& c);
    void overrideMsg(Client& c,const Frame& f);
    DataSet& roomData(Room& r) {return r.overrideData ? *r.overrideData : retail_;}
    void closeTick(Room& r, bool multipleGames);
    void finishTick(Room& r);
    bool canAdvance(const Room& r) const;   // false = wait for a lagging player (flow control)
    void checkHashes(Room& r, uint32_t tick);
    void dropClient(uint32_t id, const char* reason);
    void writeSlots(Writer& w, Room& r, bool fromStart = false);
    void writeReplay(Room& r);
};

// A running game is abandoned once nobody is left to watch it. Normally that means
// no human slot has a live or held (dropped-within-grace) client. A game created by
// a SPECTATOR host to watch the AIs fight (single-player spectate / TAK_MP_WATCH) has
// NO human slots at all, so it also stays alive while a spectator is connected and at
// least one AI is still playing -- otherwise the server would tear it down the instant
// it started.
static bool roomActive(const Room& r) {
    bool aiPresent = false;
    for (int i = 0; i < kMaxSlots; ++i) {
        if (r.slots[i].type == 1 && (r.slotClient[i] >= 0 || r.slotDropped[i])) return true;
        if (r.slots[i].type == 2) aiPresent = true;
    }
    return aiPresent && !r.spectators.empty();
}

// True while SOMEBODY is still connected to the room -- a live seated player or a
// spectator. A HELD slot does not count: holding it is what lets a player rejoin,
// but if every other human has gone too then nobody is left watching, and the room
// would otherwise keep its referee sim and AIs running for the whole drop grace
// (or until the auto-pause budget ran out and it resumed at full speed) with not a
// single client attached. Games are meant to stop when the last human leaves, so a
// room that empties is torn down immediately rather than ticking on unattended.
// Note this deliberately gives up the rejoin window when the LAST player drops:
// there is nobody for a paused game to be unfair to, and a ghost game costs the
// server a sim + AI per room.
static bool roomOccupied(const Room& r) {
    for (int i = 0; i < kMaxSlots; ++i)
        if (r.slotClient[i] >= 0) return true;
    return !r.spectators.empty();
}

// Builds the whole replay into a second buffer and writes it synchronously on the
// server loop, so a teardown does stall every other room for the duration. Measured
// before worrying about it: a 3600-tick game is 58 KB and 2.9 ms end to end. Even at
// 20x that it is under 60 ms, once, at room teardown. Left synchronous on purpose --
// an async writer here would be complexity bought against a cost I could not measure.
// Worth revisiting only if replays get much larger (many seated humans issuing orders
// every tick) or the store is slow.
Writer Server::replayBytes(Room& r) {
    // Self-contained replay: header (see net/replayhdr.h -- one definition, shared
    // with the client writer and the loader), every tick bundle, then the referee's
    // recorded hash checkpoints. A viewer can rebuild the world, play it back, and
    // compare its own hashes against what actually happened.
    tak::net::ReplayHeader h;
    h.mapId = r.mapId;
    if (r.overridePackage) h.overrideDigest=r.overridePackage->digest;
    if (r.mapPackage) h.mapDigest = r.mapPackage->digest;
    h.mission = r.mission;
    h.engineVersion = tak::kVersion;
    h.crusades = r.opts.crusades;
    h.forfeitSelfDestruct = r.opts.forfeitSelfDestruct;
    h.overridePolicy = r.opts.overridePolicy;
    h.unitCap = r.opts.unitCap;
    h.monarchExpendable = r.opts.monarchExpendable;
    h.doubleSight = r.opts.doubleSight;
    h.pathfindingMode = r.opts.pathfindingMode;
    h.stressTest = r.opts.stressTest;
    h.randomStarts = r.opts.randomStarts;
    h.benchmark = uint8_t(r.opts.benchmark);
    h.seed = r.seed;
    h.dataHash = roomData(r).hash;
    for (int i = 0; i < kMaxSlots; ++i) {
        const SlotInfo& s = r.startSlots[i];   // start config, not the forfeited end state
        h.slotType[i] = s.type;
        h.slotFaction[i] = s.faction;
        h.slotColor[i] = s.color;
        h.slotTeam[i] = s.team;
        h.slotAiLevel[i] = s.aiLevel;
    }
    Writer w;
    tak::net::writeReplayHeader(w, h);
    w.u32(uint32_t(r.log.size()));
    for (const auto& b : r.log) { w.u32(uint32_t(b.size())); w.b.insert(w.b.end(), b.begin(), b.end()); }
    // The referee's hash trail. Two identical reruns only prove the reruns agree;
    // this is what lets a replay say where it diverged from the real game.
    w.u32(uint32_t(r.replayChecks.size()));
    for (const auto& [tk, hs] : r.replayChecks) { w.u32(tk); w.u64(hs); }
    return w;
}
void Server::writeReplay(Room& r) {
    if (replayDir_.empty() || r.log.empty() || !r.campaignBattleId.empty()) return;
    try {tak::storageRoom(tak::storageUsage(replayDir_),r.logBytes+65536,limits_.replayDisk);}
    catch(const std::exception& e) {std::fprintf(stderr,"replay not saved: %s\n",e.what());return;}
    const Writer w=replayBytes(r);
    std::string path = replayDir_ + "/game-" + std::to_string(r.id) + "-" +
                       std::to_string(r.createdMs) + ".takrep";
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(w.b.data(), 1, w.b.size(), f);
        std::fclose(f);
        std::fprintf(stderr, "game %u: wrote replay %s (%zu ticks, %zu bytes)\n",
                     r.id, path.c_str(), r.log.size(), w.b.size());
    }
}

void Server::sendReject(Client& c, const std::string& why) {
    Writer w; w.str(why);
    c.conn.send(Msg::Reject, w);
    // Every caller fail()s/closes the connection right after: push the reason out
    // NOW, or the queued Reject dies with the socket and the client can only
    // report "peer closed" instead of WHY it was turned away.
    c.conn.flushWrite();
    std::fprintf(stderr, "reject client %u: %s\n", c.id, why.c_str());
}

void Server::handshake(Client& c, const Frame& f) {
    if (f.kind != Msg::Hello) { sendReject(c, "expected Hello"); c.conn.fail("no hello"); return; }
    Reader r(f.payload.data(), f.payload.size());
    uint32_t ver = r.u32();
    std::string build = r.str();
    uint64_t dataHash = r.u64();
    std::string name = r.str();
    (void)build; (void)dataHash;   // logged; clients are gated by version here
    if (!r.ok || r.p!=r.end) { sendReject(c, "malformed hello"); c.conn.fail("bad hello"); return; }
    if (ver != kNetVersion) {
        sendReject(c, "protocol version mismatch (server " + std::to_string(kNetVersion) +
                       ", client " + std::to_string(ver) + ") -- update your build");
        c.conn.fail("version");
        return;
    }
    // Gameplay-data agreement: every peer must feed its sim byte-identical gameplay
    // data (verifies the retail files are unmodified, and that Full-override players
    // share the same overrides). The referee's own hash is the authority -- there is
    // no relay mode adopting the first client's any more, which could only ever hold
    // peers to whatever the earliest arrival happened to have.
    // The Hello hash is the client's PURE-RETAIL gameplay fingerprint (mounted with
    // no overrides), so the base game files must match regardless of anyone's
    // override tier. (Full-tier gameplay overrides are checked per game at Loaded.)
    const uint64_t want = retail_.hash;
    if (dataHash != want) {
        char msg[128];
        std::snprintf(msg, sizeof msg,
                      "retail game data mismatch (server %016llx, you %016llx) -- your "
                      "base game files differ", (unsigned long long)want,
                      (unsigned long long)dataHash);
        sendReject(c, msg);
        c.conn.fail("datahash");
        std::fprintf(stderr, "client %u rejected: data hash %016llx != %016llx\n",
                     c.id, (unsigned long long)dataHash, (unsigned long long)want);
        return;
    }
    if (requireAuth_) {
        // The name in the Hello is only a suggestion and is discarded: on a server
        // with accounts, who you are is the account you prove, not what you typed.
        c.state = Client::Auth;
        c.conn.send(Msg::AuthRequired);
        return;
    }
    c.name = name.empty() ? ("player" + std::to_string(c.id)) : name;
    sendWelcome(c);
}

void Server::sendWelcome(Client& c) {
    c.state = Client::Lobby;
    Writer w; w.u32(c.id); w.str(c.name);
    c.conn.send(Msg::Welcome, w);
    std::fprintf(stderr, "client %u '%s' joined lobby\n", c.id, c.name.c_str());
}

void Server::sendAuthResult(Client& c, AuthStatus st, const tak::crypto::Digest* sig,
                            const std::string& msg) {
    Writer w;
    w.u8(uint8_t(st));
    if (sig) w.bytes(sig->data(), sig->size()); else w.bytes(nullptr, 0);
    w.str(msg);
    c.conn.send(Msg::AuthResult, w);
}

// The login exchange. Everything here runs BEFORE the client can reach the lobby,
// so the only frames entertained are the three login ones; anything else drops
// the connection rather than being queued up for later.
void Server::authMsg(Client& c, const Frame& f) {
    Reader r(f.payload.data(), f.payload.size());
    const uint64_t now = nowMs();
    if(!loginWorkKeys_.take(c.peer,now,64) || !loginGlobalWork_.take(now,256)) {
        sendReject(c,"login request rate exceeded; retry later");c.conn.fail("login rate");return;
    }

    if (f.kind == Msg::AuthBegin) {
        if (c.pendAuth.challenged) { sendReject(c, "duplicate login"); c.conn.fail("dup auth"); return; }
        std::string user = r.str();
        std::vector<uint8_t> cnonce = r.bytes(tak::auth::kNonceLen);
        if (!r.ok || r.p!=r.end) { sendReject(c, "malformed login"); c.conn.fail("bad auth"); return; }

        // Rate-limit on BOTH the name and the address, so neither hammering one
        // account from many hosts nor many accounts from one host gets a free run.
        // The address is checked first and always, which is also what stops this
        // endpoint being used to sweep for which usernames exist.
        if (uint64_t wait = loginLocked(c, user, now)) { sendThrottled(c, wait); return; }
        std::string why;
        if (!tak::auth::validUsername(user, &why)) {
            throttle_.fail(ipKeyFor(c), now, tak::srv::LoginThrottle::kAddress);
            sendAuthResult(c, AuthStatus::BadUsername, nullptr, why);
            return;
        }

        c.pendAuth = Client::PendingAuth{};
        c.pendAuth.user = user;
        c.pendAuth.clientNonce = std::move(cnonce);
        try {
            c.pendAuth.serverNonce = tak::crypto::randomVec(tak::auth::kNonceLen);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "takserver: %s\n", e.what());
            sendAuthResult(c, AuthStatus::ServerError, nullptr, "the server cannot sign you in");
            return;
        }
        const tak::srv::Account* a = accounts_.find(user);
        if (a) {
            c.pendAuth.newAccount = false;
            c.pendAuth.salt = a->cred.salt;
            c.pendAuth.iters = a->cred.iters;
        } else {
            // No such account. Say so -- the client offers to create it -- and hand
            // over a fresh server-chosen salt for it to derive against. The salt is
            // ours, not the client's, so a client cannot register with a weak or
            // shared one.
            c.pendAuth.newAccount = true;
            c.pendAuth.iters = tak::auth::kPbkdf2Iters;
            try {
                c.pendAuth.salt = tak::crypto::randomVec(tak::auth::kSaltLen);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "takserver: %s\n", e.what());
                sendAuthResult(c, AuthStatus::ServerError, nullptr, "the server cannot sign you in");
                return;
            }
        }
        c.pendAuth.challenged = true;
        Writer w;
        w.u8(c.pendAuth.newAccount ? 1 : 0);
        w.bytes(c.pendAuth.salt);
        w.u32(c.pendAuth.iters);
        w.bytes(c.pendAuth.serverNonce);
        c.conn.send(Msg::AuthChallenge, w);
        return;
    }

    if (f.kind == Msg::AuthProof) {
        if (!c.pendAuth.challenged || c.pendAuth.newAccount) {
            sendReject(c, "unexpected login proof"); c.conn.fail("bad auth"); return;
        }
        std::vector<uint8_t> proofBytes = r.bytes(tak::crypto::kHashLen);
        if (!r.ok || r.p!=r.end) { sendReject(c, "malformed login proof"); c.conn.fail("bad auth"); return; }
        // Check the lock HERE too, not just at AuthBegin. A guesser that opens a
        // hundred connections first, collects a hundred challenges, and only then
        // starts sending proofs would otherwise never meet the throttle at all --
        // the one gate it passed was armed before any of its guesses were made.
        if (uint64_t wait = loginLocked(c, c.pendAuth.user, now)) {
            sendThrottled(c, wait);
            c.pendAuth.challenged = false;
            return;
        }
        const tak::srv::Account* a = accounts_.find(c.pendAuth.user);
        if (!a) { sendAuthResult(c, AuthStatus::BadPassword, nullptr, "that account no longer exists"); return; }

        tak::crypto::Digest proof{};
        std::memcpy(proof.data(), proofBytes.data(), proof.size());
        std::vector<uint8_t> am = tak::auth::authMessage(c.pendAuth.user, c.pendAuth.clientNonce,
                                                    c.pendAuth.serverNonce, a->cred.salt,
                                                    a->cred.iters);
        const std::string ipKey = ipKeyFor(c), userKey = userKeyFor(c.pendAuth.user);
        if (!tak::auth::verifyClientProof(a->cred, am, proof)) {
            throttle_.fail(ipKey, now, tak::srv::LoginThrottle::kAddress);
            throttle_.fail(userKey, now, tak::srv::LoginThrottle::kAccount);
            sendAuthResult(c, AuthStatus::BadPassword, nullptr, "that password is not right");
            std::fprintf(stderr, "client %u (%s) failed sign-in for '%s'\n",
                         c.id, c.peer.c_str(), a->name.c_str());
            // Do NOT drop the connection: the client may simply have fumbled the
            // password and can try again, which is what the throttle is for.
            c.pendAuth.challenged = false;
            return;
        }
        // Clear the ACCOUNT only. Clearing the address as well would hand anyone
        // with one valid account a reset button for their own failure record,
        // to be pressed between guesses at somebody else's.
        throttle_.succeed(userKey);
        tak::crypto::Digest sig = tak::auth::serverSignature(a->cred.serverKey, am);
        c.account = a->name;
        c.name = a->name;
        std::string err;
        if(!accountWriteWork_.take(now,16)) {sendReject(c,"login persistence rate exceeded; retry later");c.conn.fail("login write budget");return;}
        if (!accounts_.noteLogin(a->name, &err))
            std::fprintf(stderr, "takserver: could not record login: %s\n", err.c_str());
        sendAuthResult(c, AuthStatus::Ok, &sig, "signed in");
        std::fprintf(stderr, "client %u (%s) signed in as '%s'\n",
                     c.id, c.peer.c_str(), c.name.c_str());
        sendWelcome(c);
        return;
    }

    if (f.kind == Msg::AuthRegister) {
        if (!c.pendAuth.challenged || !c.pendAuth.newAccount) {
            sendReject(c, "unexpected registration"); c.conn.fail("bad auth"); return;
        }
        std::vector<uint8_t> stored = r.bytes(tak::crypto::kHashLen);
        std::vector<uint8_t> serverKey = r.bytes(tak::crypto::kHashLen);
        if (!r.ok || r.p!=r.end) { sendReject(c, "malformed registration"); c.conn.fail("bad auth"); return; }

        // Registration is the one unauthenticated operation that WRITES, and each
        // one rewrites the whole account file on the tick thread -- so a flood is
        // both a disk-filling attack and a way to stall every running game. Meter
        // it per address on the same escalating curve as a failed password.
        const std::string ipKey = ipKeyFor(c);
        if (uint64_t wait = throttle_.lockedFor(ipKey, now)) { sendThrottled(c, wait); return; }
        throttle_.fail(ipKey, now, tak::srv::LoginThrottle::kAddress);

        if(!registrationOpen_ || accounts_.size()>=limits_.accounts || !accountWriteWork_.take(now,8)) {
            sendAuthResult(c,AuthStatus::ServerError,nullptr,"registration unavailable; retry later");
            c.pendAuth.challenged=false;return;
        }
        tak::auth::Credential cred;
        cred.iters = c.pendAuth.iters;
        cred.salt = c.pendAuth.salt;
        std::memcpy(cred.storedKey.data(), stored.data(), cred.storedKey.size());
        std::memcpy(cred.serverKey.data(), serverKey.data(), cred.serverKey.size());

        std::string err;
        if (!accounts_.create(c.pendAuth.user, cred, &err)) {
            // Either the name was taken in the moments since the challenge, or the
            // file could not be written. Both are worth distinguishing to the player.
            bool taken = accounts_.find(c.pendAuth.user) != nullptr;
            sendAuthResult(c, taken ? AuthStatus::NameTaken : AuthStatus::ServerError, nullptr,
                           taken ? "that name was just taken -- pick another" : err);
            std::fprintf(stderr, "takserver: registration of '%s' failed: %s\n",
                         c.pendAuth.user.c_str(), err.c_str());
            c.pendAuth.challenged = false;
            return;
        }
        c.account = c.pendAuth.user;
        c.name = c.pendAuth.user;
        // Sign the result with the ServerKey we were just handed, exactly as on
        // the sign-in path. The client can then demand a valid signature for BOTH
        // outcomes instead of having to trust an unsigned "Created" -- which a
        // machine posing as the server would otherwise use to skip the check.
        std::vector<uint8_t> am = tak::auth::authMessage(c.pendAuth.user, c.pendAuth.clientNonce,
                                                         c.pendAuth.serverNonce, c.pendAuth.salt,
                                                         c.pendAuth.iters);
        tak::crypto::Digest sig = tak::auth::serverSignature(cred.serverKey, am);
        // A registration that got this far is not a failed attempt.
        throttle_.succeed(userKeyFor(c.pendAuth.user));
        sendAuthResult(c, AuthStatus::Created, &sig, "new account created");
        std::fprintf(stderr, "client %u (%s) created account '%s' (%zu total)\n",
                     c.id, c.peer.c_str(), c.name.c_str(), accounts_.size());
        sendWelcome(c);
        return;
    }

    sendReject(c, "expected a login");
    c.conn.fail("no login");
}

namespace {
int64_t campaignNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
Writer battleReply(uint8_t status, const std::string& campaign = "", const std::string& battle = "",
                   uint32_t room = 0, const std::string& map = "", int64_t expires = 0, const std::string& reason = "") {
    Writer w;w.u8(status);w.str(campaign);w.str(battle);w.u32(room);w.str(map);w.u64(uint64_t(expires));w.str(reason);return w;
}
}

tak::srv::crusades::BattleContext Server::campaignContext(const Room& r, bool requirePresent) const {
    if (r.campaignBattleId.empty() && requirePresent) throw std::runtime_error("not a campaign room");
    if (!r.mapPackage || r.cap != 2 || !r.mission.empty() || !r.spectators.empty())
        throw std::runtime_error("invalid campaign room");
    tak::srv::crusades::BattleContext context;
    context.mapIdentifier = r.mapId;context.mapDigest = r.mapPackage->digest;
    context.crusadesBalance = r.opts.crusades == 1;
    Writer rules;
    rules.str("modern-crusades-duel-v1");rules.u32(r.seed);
    const auto& o=r.opts;
    rules.u8(o.crusades);rules.u8(o.forfeitSelfDestruct);rules.u8(o.overridePolicy);
    rules.u8(o.speed);rules.u8(o.speedUnlock);rules.u32(o.unitCap);rules.u8(o.monarchExpendable);
    rules.u8(o.stressTest);rules.u8(o.fogExplored);rules.u8(o.benchmark);rules.u8(o.randomStarts);rules.u8(o.doubleSight);rules.u8(uint8_t(o.pathfindingMode));
    for (int i=0;i<kMaxSlots;++i) {
        const auto& slot=r.running ? r.startSlots[i] : r.slots[i];
        rules.u8(slot.type);rules.u8(slot.faction);rules.u8(slot.color);rules.u8(slot.team);rules.u8(slot.aiLevel);
        if(i>=2) {if(slot.type!=3 || r.slotClient[i]>=0)throw std::runtime_error("extra campaign participant");continue;}
        if(slot.type!=1)throw std::runtime_error("campaign requires two human participants");
        std::string account=r.campaignAccounts[i];
        if(r.slotClient[i]>=0) {
            const auto peer=clients_.find(uint32_t(r.slotClient[i]));
            if(peer==clients_.end() || peer->second->account.empty() || peer->second->roomId!=r.id || peer->second->slot!=i)
                throw std::runtime_error("campaign participant connection missing");
            account=tak::auth::foldUsername(peer->second->account);
            if(account!=r.campaignAccounts[i])throw std::runtime_error("campaign participant changed");
        } else if(requirePresent)throw std::runtime_error("campaign participant not joined");
        context.participants.push_back(account);
    }
    context.rulesDigest=tak::crypto::toHex(tak::crypto::sha256(rules.b.data(),rules.b.size()));
    return context;
}

std::pair<std::string,std::string> Server::saveCampaignReplay(Room& r) {
    namespace fs=std::filesystem;
    const Writer bytes=replayBytes(r);
    const auto digest=tak::crypto::toHex(tak::crypto::sha256(bytes.b.data(),bytes.b.size()));
    // Content-addressed filename plus issued identity: immutable, safe on Windows,
    // and directly traceable from the database without changing replay format.
    const auto identity=tak::crypto::toHex(tak::crypto::sha256(r.campaignBattleId));
    const std::string name="battle-"+identity+"-"+digest+".takrep";
    fs::create_directories(campaignReplayDir_);
    const auto final=campaignReplayDir_/name;
    const auto matches=[&]() {
        std::ifstream input(final,std::ios::binary);if(!input)return false;
        tak::crypto::Sha256 hash;char buffer[16384];
        while(input.read(buffer,sizeof buffer) || input.gcount())hash.update(buffer,size_t(input.gcount()));
        return input.eof() && !input.bad() && tak::crypto::toHex(hash.final())==digest;
    };
    const auto synchronize=[&]() {
#ifdef _WIN32
        HANDLE file=CreateFileW(final.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("cannot open published replay for sync");
        const bool synced=FlushFileBuffers(file)!=0;const bool closed=CloseHandle(file)!=0;
        if(!synced || !closed)throw std::runtime_error("cannot durably sync published replay");
#else
        // Also required on the retry path: a previous publication could have
        // succeeded before its directory durability barrier failed.
        const auto syncPath=[](const fs::path& path) {
            const int fd=::open(path.c_str(),O_RDONLY);
            if(fd<0)throw std::runtime_error("cannot open replay artifact for sync");
            const bool synced=::fsync(fd)==0;const bool closed=::close(fd)==0;
            if(!synced || !closed)throw std::runtime_error("cannot durably publish campaign replay");
        };
        syncPath(final);
        for(auto directory=campaignReplayDir_;;directory=directory.parent_path()) {
            syncPath(directory);
            if(directory==directory.parent_path())break;
        }
#endif
    };
    if(fs::exists(final)) {
        if(!matches())throw std::runtime_error("campaign replay artifact failed verification");
        synchronize();
        return {name,digest};
    }
    tak::storageRoom(tak::storageUsage(campaignReplayDir_),bytes.b.size(),limits_.replayDisk);
    auto temporary=final;temporary+="."+tak::crypto::toHex(tak::crypto::randomVec(8))+".tmp";
    struct Cleanup {fs::path path;~Cleanup(){std::error_code ec;fs::remove(path,ec);}} cleanup{temporary};
#ifdef _WIN32
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("cannot create campaign replay");
    bool ok=true;
    for(size_t offset=0;offset<bytes.b.size();) {
        DWORD count=0;const DWORD amount=DWORD(std::min<size_t>(bytes.b.size()-offset,1u<<20));
        if(!WriteFile(file,bytes.b.data()+offset,amount,&count,nullptr) || !count){ok=false;break;}
        offset+=count;
    }
    if(ok && !FlushFileBuffers(file))ok=false;
    if(!CloseHandle(file))ok=false;
    if(!ok)throw std::runtime_error("cannot durably write campaign replay");
    if(!MoveFileExW(temporary.c_str(),final.c_str(),MOVEFILE_WRITE_THROUGH) && !matches())
        throw std::runtime_error("cannot publish campaign replay");
#else
    const int file=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
    if(file<0)throw std::runtime_error("cannot create campaign replay");
    bool ok=true;
    for(size_t offset=0;offset<bytes.b.size();) {
        const auto count=::write(file,bytes.b.data()+offset,bytes.b.size()-offset);
        if(count<0 && errno==EINTR)continue;
        if(count<=0){ok=false;break;}offset+=size_t(count);
    }
    if(ok && ::fsync(file)!=0)ok=false;
    if(::close(file)!=0)ok=false;
    if(!ok)throw std::runtime_error("cannot durably write campaign replay");
    // Publish without replacing an existing immutable artifact.
    if(::link(temporary.c_str(),final.c_str())!=0 && !matches())
        throw std::runtime_error("cannot publish campaign replay");
    fs::remove(temporary);
#endif
    if(!matches())throw std::runtime_error("campaign replay verification failed");
    synchronize();
    return {name,digest};
}

void Server::finalizeCampaign(Room& r,bool abandoning) {
    namespace campaign=tak::srv::crusades;
    using Outcome=campaign::ResultOutcome;
    if(r.campaignBattleId.empty() || !r.running || r.campaignResultRecorded || r.tickJob.valid())return;
    if(!r.campaignResult) {
        std::optional<Outcome> reason=r.campaignFault;
        int winner=-1;
        if(!reason && !r.ref)reason=Outcome::RefereeFailure;
        if(!reason && r.ref && r.ref->winningTeam()>=0) {
            for(int i=0;i<2;++i)if(r.startSlots[i].team==r.ref->winningTeam())winner=i;
            if(winner<0 || r.tick==0 || !r.campaignLoaded[0] || !r.campaignLoaded[1])reason=Outcome::InvalidClient;
            else if(r.campaignTimeout[0] || r.campaignTimeout[1])reason=Outcome::Timeout;
            else if(r.campaignDisconnected[1-winner] && !r.campaignResigned[1-winner])reason=Outcome::Disconnect;
            else reason=r.campaignResigned[1-winner]?Outcome::Resignation:Outcome::Victory;
        }
        if(!reason && r.ref && r.ref->numPlayers()>=2 && r.ref->player(0).defeated && r.ref->player(1).defeated)
            reason=Outcome::Draw;
        if(!reason && abandoning)reason=(r.campaignTimeout[0]||r.campaignTimeout[1])?Outcome::Timeout:
            (r.campaignDisconnected[0]||r.campaignDisconnected[1])?Outcome::Disconnect:Outcome::ServerAbort;
        if(!reason)return;
        campaign::VerifiedMatchResult result;
        result.outcome=*reason;result.finalTick=r.log.size();result.finalStateHash=r.ref?r.ref->stateHash():0;
        result.gameplayFingerprint=roomData(r).hash;result.engineBuild=tak::kBuildId;
        if(*reason==Outcome::Victory || *reason==Outcome::Resignation)result.winners.push_back(r.campaignAccounts[winner]);
        static const char* factions[]={"aramon","taros","veruna","zhon","creon"};
        for(int i=0;i<2;++i) {
            campaign::ParticipantMatchResult row;
            row.accountId=r.campaignAccounts[i];row.faction=factions[r.startSlots[i].faction%5];row.team=r.startSlots[i].team;
            if(r.ref && i<r.ref->numPlayers()) {
                const auto& p=r.ref->player(i);
                row.kills=p.kills;row.losses=p.losses;row.score=p.score;row.built=p.built;
                row.currentUnits=p.unitCount;row.defeated=p.defeated;
            }
            result.participantResults.push_back(std::move(row));
        }
        if(result.finalTick) {
            const auto tick=uint32_t(result.finalTick-1);
            if(!r.replayChecks.empty() && r.replayChecks.back().first==tick)r.replayChecks.back().second=result.finalStateHash;
            else r.replayChecks.emplace_back(tick,result.finalStateHash);
        }
        r.campaignResult=std::move(result);
        ++campaignActivityVersion_; ++campaignMatchGeneration_; // Frozen matches cease being active before durable completion.
        // Freeze the referee and replay at this terminal snapshot, while allowing
        // two seconds for already-in-flight client hashes to invalidate integrity.
        r.campaignResultDue=nowMs()+((*reason==Outcome::Victory || *reason==Outcome::Resignation)?2000:0);
    }
    auto& result=*r.campaignResult;
    if(r.campaignFault) {result.outcome=*r.campaignFault;result.winners.clear();}
    if(nowMs()<r.campaignResultDue)return;
    if(result.replayId.empty()) {
        try {auto artifact=saveCampaignReplay(r);result.replayId=std::move(artifact.first);result.replayDigest=std::move(artifact.second);}
        catch(const std::exception& e) {
            std::fprintf(stderr,"campaign replay: %s\n",e.what());
            if(result.outcome==Outcome::Victory || result.outcome==Outcome::Resignation) {
                result.outcome=Outcome::ServerAbort;result.winners.clear();
            }
        }
    }
    try {
        const auto context=campaignContext(r,false);
        try {crusades_->recordVerifiedResult(r.campaignBattleId,r.campaignRoomToken,context,result,campaignNow());}
        catch(const campaign::StaleBattleError&) {
            if(result.outcome!=Outcome::Victory && result.outcome!=Outcome::Resignation)throw;
            // A proven stale allegiance/campaign cannot
            // become points. Retain the exact room snapshot as a no-credit abort.
            result.outcome=Outcome::ServerAbort;result.winners.clear();
            crusades_->recordVerifiedResult(r.campaignBattleId,r.campaignRoomToken,context,result,campaignNow());
        }
        r.campaignResultRecorded=true;
        notifyCampaignBattle(r.campaignBattleId);
        std::fprintf(stderr,"campaign battle %s recorded outcome %d at tick %llu\n",r.campaignBattleId.c_str(),int(result.outcome),
            static_cast<unsigned long long>(result.finalTick));
    } catch(const std::exception& e) {
        try {if(crusades_->verifiedResult(r.campaignBattleId)) {r.campaignResultRecorded=true;notifyCampaignBattle(r.campaignBattleId);return;}}catch(const std::exception&) {}
        std::fprintf(stderr,"campaign result persistence: %s\n",e.what());
        r.campaignResultDue=nowMs()+1000; // Keep the frozen room; never discard an unrecorded outcome.
    }
}

void Server::cancelCampaignBattle(Room& r) {
    if(r.campaignBattleId.empty() || !crusades_)return;
    r.campaignCancelled=true; // Never expose a closing room, even during its notification.
    ++campaignActivityVersion_;++campaignMatchGeneration_;
    try {
        const auto status=crusades_->battle(r.campaignBattleId).status;
        if(status==tak::srv::crusades::BattleStatus::Issued || status==tak::srv::crusades::BattleStatus::Started)
            crusades_->cancelBattle(r.campaignBattleId,campaignNow());
        notifyCampaignBattle(r.campaignBattleId);
    }
    catch(const std::exception& e) {std::fprintf(stderr,"campaign cancellation: %s\n",e.what());}
}

void Server::closeCampaignLobby(uint32_t roomId,const char* reason) {
    auto it=rooms_.find(roomId);if(it==rooms_.end())return;
    auto& r=it->second;cancelCampaignBattle(r);
    for(auto& [id,peer]:clients_) {
        (void)id;if(peer->roomId!=roomId)continue;
        dropPendingCommands(*peer,r);peer->state=Client::Lobby;peer->roomId=0;peer->slot=-1;peer->loaded=false;
        peer->conn.send(Msg::CrusadesBattleResult,battleReply(4,r.campaignId,r.campaignBattleId,0,"",0,reason));
    }
    rooms_.erase(it);
}

std::optional<uint32_t> Server::campaignRoomId(const std::string& battleId) const {
    for(const auto& [id,room]:rooms_)
        if(room.campaignBattleId==battleId && !room.campaignCancelled && !room.campaignResultRecorded &&
            !room.campaignResult && !room.campaignFault && (room.running || campaignNow()<room.campaignExpires))return id;
    return {};
}

std::optional<tak::net::crusades::BattleActivity> Server::campaignActivity(const std::string& campaign,uint32_t territory) const {
    tak::net::crusades::BattleActivity activity;
    for(const auto& [id,room]:rooms_) {
        (void)id;
        if(room.campaignId!=campaign || room.campaignTerritory!=territory || room.campaignBattleId.empty() ||
            room.campaignCancelled || room.campaignResultRecorded || room.campaignResult || room.campaignFault)continue;
        if(room.running)++activity.active;
        else if(campaignNow()<room.campaignExpires)++activity.offered;
    }
    return activity;
}

void Server::campaignRead(Client& c,const Frame& f) {
    namespace wire=tak::net::crusades;
    // Limit synchronous database work and snapshot amplification per peer.
    // The connection already bounds incoming frames and queued outgoing bytes.
    const auto now=nowMs();
    if(now-c.campaignReadWindow>=1000) {c.campaignReadWindow=now;c.campaignReads=0;}
    if(c.campaignReads>=32) {
        if(c.campaignReads==32) {
            uint32_t requestId=0;
            if(f.payload.size()>=6)for(unsigned i=0;i<4;++i)requestId|=uint32_t(f.payload[2+i])<<(8*i);
            c.conn.send(Msg::CrusadesError,wire::encode(wire::Response{
                wire::Error{requestId,wire::ErrorCode::Unavailable,"",{},"campaign query rate exceeded; retry later"}}));
            ++c.campaignReads;
        }
        return;
    }
    ++c.campaignReads;
    const bool authenticated=requireAuth_ && !c.account.empty() &&
        (c.state==Client::Lobby || c.state==Client::InGame);
    const auto account=authenticated?tak::auth::foldUsername(c.account):std::string{};
    const auto reply=tak::srv::crusades::handleCampaignRead(crusades_.get(),account,f.kind,f.payload,
        [this](const std::string& id){return campaignRoomId(id);},
        [this](const std::string& campaign,uint32_t territory){return campaignActivity(campaign,territory);},
        [this](const auto& battle,const auto& result){return tak::srv::crusades::ReplayFiles(campaignReplayDir_).inspect(battle,result);});
    c.conn.send(reply.kind,reply.payload);
    if(authenticated) c.campaignProtocol=true;
    if(reply.kind==Msg::CrusadesCampaignSnapshot) {
        const auto snapshot=wire::decodeSnapshot(reply.payload.b);
        c.campaignSubscription=snapshot.campaignId;c.campaignRevision=snapshot.revision;
        c.campaignActivityVersion=campaignActivityVersion_;
    }
}

void Server::campaignReplayChunk(Client& c,const Frame& f) {
    namespace wire=tak::net::crusades;
    uint32_t requestId=0;
    if(f.payload.size()>=6)for(unsigned i=0;i<4;++i)requestId|=uint32_t(f.payload[2+i])<<(8*i);
    auto error=[&](wire::ErrorCode code){const auto reply=tak::srv::crusades::campaignReadError(requestId,code);c.conn.send(reply.kind,reply.payload);};
    // A paced 64KiB transfer uses up to 50 requests/s. Give it its own quota;
    // archive downloads must not starve campaign reads or allocate whole files.
    const auto now=nowMs();
    if(now-c.campaignReplayWindow>=1000){c.campaignReplayWindow=now;c.campaignReplayReads=0;}
    if(c.campaignReplayReads>=64){if(c.campaignReplayReads==64){++c.campaignReplayReads;error(wire::ErrorCode::Unavailable);}return;}
    ++c.campaignReplayReads;
    if(!requireAuth_ || c.account.empty() || (c.state!=Client::Lobby&&c.state!=Client::InGame)) {
        error(wire::ErrorCode::AuthenticationRequired);return;
    }
    if(!crusades_){error(wire::ErrorCode::Disabled);return;}
    try {
        const auto request=std::get<wire::ReplayChunkRequest>(wire::decodeRequest(wire::RequestKind::ReplayChunk,f.payload));
        tak::srv::crusades::IssuedBattle battle;
        try{battle=crusades_->battle(request.battleId);}catch(const std::exception&){error(wire::ErrorCode::NotFound);return;}
        const auto result=crusades_->verifiedResult(battle.id);
        if(!result || (battle.status!=tak::srv::crusades::BattleStatus::Completed&&battle.status!=tak::srv::crusades::BattleStatus::Cancelled)) {
            error(wire::ErrorCode::NotFound);return;
        }
        if(!crusades_->allegiance(battle.campaignId,tak::auth::foldUsername(c.account))){error(wire::ErrorCode::Forbidden);return;}
        const auto chunk=tak::srv::crusades::ReplayFiles(campaignReplayDir_).read(battle,*result,requestId,request.offset,request.limit);
        if(!chunk){error(wire::ErrorCode::Unavailable);return;}
        c.conn.send(Msg::CrusadesReplayChunk,wire::encode(wire::Response{*chunk}));c.campaignProtocol=true;
    }catch(const wire::DecodeError& e){error(e.code);}
    catch(const std::exception&){error(wire::ErrorCode::Unavailable);}
}

void Server::refreshCampaignSnapshot(Client& c,bool force) {
    namespace wire=tak::net::crusades;
    if(!crusades_ || !c.campaignProtocol || c.campaignSubscription.empty() || c.account.empty() ||
        c.conn.txPending()>wire::kMaxPayload)return;
    try {
        if(!force && c.campaignActivityVersion==campaignActivityVersion_ &&
            uint64_t(crusades_->campaignRevision(c.campaignSubscription))==c.campaignRevision)return;
        const auto reply=tak::srv::crusades::campaignReadResponse(crusades_.get(),tak::auth::foldUsername(c.account),
            wire::Request{wire::SnapshotRequest{0,c.campaignSubscription,c.campaignRevision}}, {},
            [this](const std::string& campaign,uint32_t territory){return campaignActivity(campaign,territory);});
        const auto snapshot=wire::decodeSnapshot(reply.payload.b);
        c.conn.send(reply.kind,reply.payload);c.campaignRevision=snapshot.revision;
        c.campaignActivityVersion=campaignActivityVersion_;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"campaign snapshot refresh: %s\n",e.what());
        // End a broken subscription; an explicit request can correct it. Never
        // spin each second on a campaign too large for the bounded wire format.
        c.campaignSubscription.clear();c.campaignRevision=wire::kUnknownRevision;
        c.conn.send(Msg::CrusadesError,wire::encode(wire::Response{
            wire::Error{0,wire::ErrorCode::Unavailable,"",{},"campaign subscription unavailable; request a new snapshot"}}));
    }
}

void Server::notifyCampaignPlayer(Client& c,const std::string& campaignId) {
    if(!crusades_ || !c.campaignProtocol || c.account.empty() ||
        c.conn.txPending()>tak::net::crusades::kMaxPayload)return;
    try {
        const auto reply=tak::srv::crusades::campaignReadResponse(crusades_.get(),tak::auth::foldUsername(c.account),
            tak::net::crusades::Request{tak::net::crusades::PlayerStatusRequest{0,campaignId}});
        c.conn.send(reply.kind,reply.payload);
    } catch(const std::exception& e) {std::fprintf(stderr,"campaign player status: %s\n",e.what());}
}

void Server::notifyCampaignBattle(const std::string& battleId) {
    if(!crusades_)return;
    ++campaignActivityVersion_;++campaignMatchGeneration_;
    try {
        const auto battle=crusades_->battle(battleId);
        for(auto& [id,peer]:clients_) {
            (void)id;
            if(!peer->campaignProtocol || peer->account.empty() ||
                peer->conn.txPending()>tak::net::crusades::kMaxPayload)continue;
            const auto account=tak::auth::foldUsername(peer->account);
            if(std::find(battle.context.participants.begin(),battle.context.participants.end(),account)==battle.context.participants.end())continue;
            const auto reply=tak::srv::crusades::campaignReadResponse(crusades_.get(),account,
                tak::net::crusades::Request{tak::net::crusades::BattleStatusRequest{0,battleId}},
                [this](const std::string& key){return campaignRoomId(key);});
            peer->conn.send(reply.kind,reply.payload);
            notifyCampaignPlayer(*peer,battle.campaignId);
            refreshCampaignSnapshot(*peer);
        }
    } catch(const std::exception& e) {std::fprintf(stderr,"campaign battle notification: %s\n",e.what());}
}

Client* Server::campaignAvailablePeer(const std::string& account) {
    Client* available=nullptr;
    unsigned connections=0;
    for(auto& [id,peer]:clients_) {
        (void)id;
        if(peer->account.empty() || tak::auth::foldUsername(peer->account)!=account)continue;
        // Failed peers still own their account's queue cleanup until dropClient
        // runs at the end of this poll iteration. Do not let a replacement
        // session inherit that entry or enqueue one the old cleanup would erase.
        ++connections;
        if(peer->conn.ok() && peer->state==Client::Lobby && !peer->roomId)available=peer.get();
    }
    return connections==1?available:nullptr;
}

std::shared_ptr<tak::net::maps::Package> Server::campaignBattleMap(const std::string& map) {
    if(auto found=campaignMapCache_.find(map);found!=campaignMapCache_.end())return found->second;
    if(auto found=campaignMapEligibility_.find(map);found!=campaignMapEligibility_.end() && !found->second)return {};
    try {
        if(map.empty() || map.size()>4096 || tak::mapgen::isGeneratedMapId(map))throw std::runtime_error("unsupported campaign map");
        auto package=tak::net::maps::build(retail_.vfs,map);
        if(tak::net::maps::authoredScenario(*package))throw std::runtime_error("campaign map is a mission");
        auto ota=package->mapPath;ota.replace(ota.size()-4,4,".ota");
        const auto file=package->files->find(ota);
        if(file==package->files->end())throw std::runtime_error("campaign map metadata missing");
        const auto& data=file->second;
        if(tak::tnt::Scenario::parse(std::string(data.begin(),data.end())).starts.size()<2)
            throw std::runtime_error("campaign map has fewer than two starts");
        campaignMapEligibility_[map]=true;
        size_t size=package->bytes.size();for(const auto& [name,bytes]:*package->files){(void)name;size+=bytes.size();}
        constexpr size_t budget=64u<<20;
        if(size<=budget) {
            size_t held=0;
            for(const auto& [name,cached]:campaignMapCache_) {
                (void)name;held+=cached->bytes.size();for(const auto& [path,bytes]:*cached->files){(void)path;held+=bytes.size();}
            }
            if(campaignMapCache_.size()>=8 || held>budget-size)campaignMapCache_.clear();
            campaignMapCache_[map]=package;
        }
        return package;
    } catch(const std::exception&) {campaignMapEligibility_[map]=false;return {};}
}

bool Server::campaignMapEligible(const std::string& map) {
    if(auto found=campaignMapEligibility_.find(map);found!=campaignMapEligibility_.end())return found->second;
    return bool(campaignBattleMap(map));
}

void Server::pruneCampaignSearches() {
    if(!crusades_)return;
    const auto now=campaignNow();
    if(!campaignMatchQueue_.expire(now).empty())++campaignMatchGeneration_;
    std::map<std::string,tak::srv::crusades::StoredCampaign> campaigns;
    for(const auto& entry:campaignMatchQueue_.entries()) {
        bool valid=false;
        try {
            if(!campaignAvailablePeer(entry.accountId) || crusades_->activeBattleForAccount(entry.accountId,now))
                throw std::runtime_error("search participant unavailable");
            auto saved=campaigns.find(entry.campaignId);
            if(saved==campaigns.end())saved=campaigns.emplace(entry.campaignId,crusades_->load(entry.campaignId)).first;
            const auto side=crusades_->allegiance(entry.campaignId,entry.accountId);
            const auto* parcel=saved->second.definition.find(entry.territory);
            if(!parcel || saved->second.revision!=entry.campaignRevision || !side ||
                side->revision!=entry.allegianceRevision || side->alliance!=entry.alliance)
                throw std::runtime_error("search enrollment changed");
            const auto& live=saved->second.state.territories.at(entry.territory);
            const auto map=live.assignedMap?live.assignedMap:parcel->mapIdentifier;
            valid=haveCb_ && map && campaignMapEligible(*map);
        } catch(const std::exception&) {}
        if(!valid && campaignMatchQueue_.erase(entry.accountId))++campaignMatchGeneration_;
    }
}

void Server::retryCampaignMatches() {
    const auto now=nowMs();if(now<campaignMatchRetryMs_)return;
    campaignMatchRetryMs_=now+5000;
    using Entry=tak::srv::crusades::MatchEntry;
    using Key=std::tuple<std::string,uint32_t,int64_t>;
    std::map<Key,std::array<std::optional<Entry>,2>> groups;
    for(const auto& entry:campaignMatchQueue_.entries()) {
        auto& side=groups[{entry.campaignId,entry.territory,entry.campaignRevision}][entry.alliance==tak::srv::crusades::Alliance::Honor?0:1];
        if(!side || entry.sequence<side->sequence)side=entry;
    }
    std::map<uint64_t,std::pair<Entry,Entry>> pairs;
    for(const auto& [key,sides]:groups) {
        (void)key;if(!sides[0] || !sides[1])continue;
        const bool honorFirst=sides[0]->sequence<sides[1]->sequence;
        const auto& host=*sides[honorFirst?0:1];const auto& guest=*sides[honorFirst?1:0];
        pairs.emplace(host.sequence,std::make_pair(host,guest));
    }
    if(pairs.empty())return;
    auto next=pairs.upper_bound(campaignMatchRetrySequence_);if(next==pairs.end())next=pairs.begin();
    campaignMatchRetrySequence_=next->first;
    const auto& [host,guest]=next->second;
    // Reattempt one pair per interval after pruning; transient issuance failure
    // neither discards valid searches nor stalls unrelated lobby traffic in a loop.
    if(auto* peer=campaignAvailablePeer(host.accountId))
        createCampaignBattle(*peer,host.campaignId,host.territory,guest.accountId);
}

void Server::sendCampaignMatchmaking(Client& c,const std::string& campaign,uint32_t requestId) {
    namespace wire=tak::net::crusades;
    try {
        if(!crusades_->hasCampaign(campaign)) {
            const auto error=tak::srv::crusades::campaignReadError(requestId,wire::ErrorCode::NotFound);
            c.conn.send(error.kind,error.payload);return;
        }
        const auto saved=crusades_->load(campaign);
        const auto account=tak::auth::foldUsername(c.account);
        const auto side=crusades_->allegiance(campaign,account);
        wire::MatchmakingStatus board;board.requestId=requestId;board.campaignId=campaign;
        board.campaignRevision=uint64_t(saved.revision);board.generation=campaignMatchGeneration_;
        board.canSearch=haveCb_ && side && campaignAvailablePeer(account)==&c && !crusades_->activeBattleForAccount(account,campaignNow());
        std::map<uint32_t,wire::MatchTerritory> rows;
        for(const auto& [id,parcel]:saved.definition.territories()) {
            const auto& live=saved.state.territories.at(id);
            const auto map=live.assignedMap?live.assignedMap:parcel.mapIdentifier;
            rows[id]={id,haveCb_ && map && campaignMapEligible(*map),0,0,0,0};
        }
        for(const auto& entry:campaignMatchQueue_.entries())if(entry.campaignId==campaign) {
            const auto row=rows.find(entry.territory);if(row==rows.end())continue;
            if(entry.alliance==tak::srv::crusades::Alliance::Honor)++row->second.waitingHonor;
            else ++row->second.waitingTerror;
            if(entry.accountId==account) {board.searchingTerritory=entry.territory;board.searchExpiresUnix=uint64_t(entry.expiresUnix);}
        }
        for(const auto& [id,room]:rooms_) {
            (void)id;
            if(room.campaignId!=campaign || room.campaignBattleId.empty() || room.campaignCancelled ||
                room.campaignResultRecorded || room.campaignResult || room.campaignFault)continue;
            const auto row=rows.find(room.campaignTerritory);if(row==rows.end())continue;
            if(room.running)++row->second.active;
            else if(campaignNow()<room.campaignExpires)++row->second.offered;
        }
        for(const auto& [id,row]:rows){(void)id;board.territories.push_back(row);}
        if(c.campaignMatchBoard) {
            auto before=*c.campaignMatchBoard,after=board;
            before.requestId=after.requestId=0;before.generation=after.generation=0;
            if(wire::encode(wire::Response{before})!=wire::encode(wire::Response{after}))++campaignMatchGeneration_;
        }
        board.generation=campaignMatchGeneration_;
        c.conn.send(Msg::CrusadesMatchmakingStatus,wire::encode(wire::Response{board}));
        c.campaignMatchSubscription=campaign;c.campaignMatchBoard=std::move(board);c.campaignProtocol=true;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"campaign matchmaking read: %s\n",e.what());
        const auto error=tak::srv::crusades::campaignReadError(requestId,wire::ErrorCode::Unavailable);
        c.conn.send(error.kind,error.payload);
    }
}

void Server::refreshCampaignMatchmaking(Client& c) {
    if(!crusades_ || c.account.empty() || c.campaignMatchSubscription.empty() ||
        c.conn.txPending()>tak::net::crusades::kMaxPayload)return;
    try {
        // Only aggregate/room mutations or a personal availability/revision change
        // need a new board; idle peers never rebuild every territory each tick.
        const auto& previous=c.campaignMatchBoard;
        const auto account=tak::auth::foldUsername(c.account);
        if(previous && previous->generation==campaignMatchGeneration_ &&
            previous->campaignRevision==uint64_t(crusades_->campaignRevision(c.campaignMatchSubscription))) {
            const auto side=crusades_->allegiance(c.campaignMatchSubscription,account);
            const bool available=haveCb_ && side && campaignAvailablePeer(account)==&c && !crusades_->activeBattleForAccount(account,campaignNow());
            if(available==previous->canSearch)return;
        }
        sendCampaignMatchmaking(c,c.campaignMatchSubscription,0);
    } catch(const std::exception&) {c.campaignMatchSubscription.clear();c.campaignMatchBoard.reset();}
}

void Server::campaignMatchmaking(Client& c,const Frame& f) {
    namespace wire=tak::net::crusades;
    uint32_t requestId=0;if(f.payload.size()>=6)for(unsigned i=0;i<4;++i)requestId|=uint32_t(f.payload[2+i])<<(8*i);
    const auto error=[&](wire::ErrorCode code) {const auto reply=tak::srv::crusades::campaignReadError(requestId,code);c.conn.send(reply.kind,reply.payload);};
    const auto now=nowMs();
    if(now-c.campaignReadWindow>=1000){c.campaignReadWindow=now;c.campaignReads=0;}
    if(c.campaignReads>=32){if(c.campaignReads==32){++c.campaignReads;error(wire::ErrorCode::Unavailable);}return;}
    ++c.campaignReads;
    if(!requireAuth_ || c.account.empty() || (c.state!=Client::Lobby && c.state!=Client::InGame)) {error(wire::ErrorCode::AuthenticationRequired);return;}
    if(!crusades_){error(wire::ErrorCode::Disabled);return;}
    try {
        const auto kind=f.kind==Msg::CrusadesGetMatchmaking?wire::RequestKind::Matchmaking:
            f.kind==Msg::CrusadesSearchBattle?wire::RequestKind::MatchSearch:wire::RequestKind::MatchCancel;
        const auto request=wire::decodeRequest(kind,f.payload);
        const auto campaign=std::visit([](const auto& value)->std::string {
            using T=std::decay_t<decltype(value)>;
            if constexpr(std::is_same_v<T,wire::MatchmakingRequest> || std::is_same_v<T,wire::MatchSearchRequest> || std::is_same_v<T,wire::MatchCancelRequest>)return value.campaignId;
            else return {};
        },request);
        if(!crusades_->hasCampaign(campaign)){error(wire::ErrorCode::NotFound);return;}
        pruneCampaignSearches();
        const auto account=tak::auth::foldUsername(c.account);
        if(const auto* search=std::get_if<wire::MatchSearchRequest>(&request)) {
            const auto saved=crusades_->load(campaign);
            const auto side=crusades_->allegiance(campaign,account);
            const auto* parcel=saved.definition.find(search->territory);
            if(!haveCb_ || !side || !parcel || campaignAvailablePeer(account)!=&c || crusades_->activeBattleForAccount(account,campaignNow()))
                throw std::runtime_error("search participant unavailable");
            const auto& live=saved.state.territories.at(search->territory);
            const auto map=live.assignedMap?live.assignedMap:parcel->mapIdentifier;
            if(!map || !campaignMapEligible(*map))throw std::runtime_error("search territory unavailable");
            const auto entry=campaignMatchQueue_.put({account,campaign,search->territory,side->alliance,side->revision,saved.revision,
                campaignNow()+tak::srv::crusades::MatchQueue::kLifetimeSeconds,0},campaignNow());
            ++campaignMatchGeneration_;
            if(const auto pair=campaignMatchQueue_.firstPair(campaign,entry.territory,entry.campaignRevision,campaignNow())) {
                const auto& [host,guest]=*pair;
                auto* peer=campaignAvailablePeer(host.accountId);
                if(!peer || !createCampaignBattle(*peer,campaign,search->territory,guest.accountId)) {
                    throw std::runtime_error("match creation rejected; valid searches retained");
                }
            }
        } else if(std::holds_alternative<wire::MatchCancelRequest>(request)) {
            const auto* entry=campaignMatchQueue_.find(account);
            if(entry && entry->campaignId==campaign && campaignMatchQueue_.erase(account))++campaignMatchGeneration_;
        }
        sendCampaignMatchmaking(c,campaign,requestId);
    } catch(const wire::DecodeError& e){error(e.code);}
    catch(const std::exception& e){std::fprintf(stderr,"campaign matchmaking: %s\n",e.what());error(wire::ErrorCode::Unavailable);}
}

void Server::issueCampaignBattle(Client& c,const Frame& f) {
    if(!requireAuth_ || c.account.empty() || (c.state!=Client::Lobby && c.state!=Client::InGame)) {
        c.conn.send(Msg::CrusadesBattleResult,battleReply(2,"","",0,"",0,"account authentication required"));return;
    }
    if(!crusades_) {c.conn.send(Msg::CrusadesBattleResult,battleReply(3,"","",0,"",0,"campaign service disabled"));return;}
    if(f.payload.size()<11 || f.payload.size()>156) {c.conn.send(Msg::CrusadesBattleResult,battleReply(1));return;}
    Reader rd(f.payload.data(),f.payload.size());const auto campaign=rd.str();const auto territory=rd.u32();const auto opponentName=rd.str();
    if(!rd.ok || rd.p!=rd.end || campaign.empty() || campaign.size()>128 || campaign.find('\0')!=std::string::npos ||
        !territory || !tak::auth::validUsername(opponentName)) {c.conn.send(Msg::CrusadesBattleResult,battleReply(1));return;}
    createCampaignBattle(c,campaign,territory,tak::auth::foldUsername(opponentName));
}

bool Server::createCampaignBattle(Client& c,const std::string& campaign,uint32_t territory,const std::string& opponentName) {
    std::string issued;
    uint32_t roomId=0;
    try {
        if(rooms_.size()>=limits_.rooms)throw std::runtime_error("server game capacity reached");
        if(c.state!=Client::Lobby || c.roomId || !haveCb_)throw std::runtime_error("campaign match unavailable");
        const auto caller=tak::auth::foldUsername(c.account),opponent=tak::auth::foldUsername(opponentName);
        if(caller==opponent)throw std::runtime_error("opponent must be another account");
        if(campaignAvailablePeer(caller)!=&c)throw std::runtime_error("issuer has multiple available connections");
        Client* invited=campaignAvailablePeer(opponent);
        if(!invited)throw std::runtime_error("opponent is not available in the lobby");
        if(crusades_->activeBattleForAccount(caller,campaignNow()) || crusades_->activeBattleForAccount(opponent,campaignNow()))
            throw std::runtime_error("account already has an authoritative battle");
        const auto saved=crusades_->load(campaign);
        const auto* parcel=saved.definition.find(territory);
        if(!parcel)throw std::runtime_error("unknown campaign territory");
        const auto& live=saved.state.territories.at(territory);
        const auto map=live.assignedMap ? live.assignedMap : parcel->mapIdentifier;
        if(!map || map->size()>4096 || tak::mapgen::isGeneratedMapId(*map))throw std::runtime_error("territory has no installed battle map");
        auto package=campaignBattleMap(*map);
        if(!package)throw std::runtime_error("territory has no eligible battle map");
        auto a=crusades_->allegiance(campaign,caller),b=crusades_->allegiance(campaign,opponent);
        if(!a || !b || a->alliance==b->alliance)throw std::runtime_error("opponents must have opposite campaign allegiances");
        Room room;room.id=nextRoomId_++;roomId=room.id;room.hostId=c.id;room.name="Crusades battle";
        room.mapId=*map;room.mapPackage=std::move(package);room.mapVfs=std::make_unique<tak::hpi::Vfs>(&retail_.vfs);
        room.mapVfs->setMapFiles(room.mapPackage->files);room.cap=2;room.createdMs=nowMs();
        room.seed=fixedSeed_?fixedSeed_:uint32_t(randToken());room.opts.crusades=1;room.opts.overridePolicy=0;
        room.opts.fogExplored=0;room.opts.forfeitSelfDestruct=1;
        room.campaignId=campaign;room.campaignTerritory=territory;room.campaignAccounts[0]=caller;room.campaignAccounts[1]=opponent;
        room.campaignRoomToken=campaignSession_+":"+std::to_string(room.id);
        room.campaignExpires=campaignNow()+600;
        for(int i=0;i<kMaxSlots;++i) {room.slots[i].type=i<2?1:3;room.slots[i].color=uint8_t(i);room.slots[i].team=uint8_t(i);}
        // Minimal modern match policy; not a historical faction eligibility rule.
        room.slots[0].faction=a->alliance==tak::srv::crusades::Alliance::Honor?0:1;
        room.slots[1].faction=b->alliance==tak::srv::crusades::Alliance::Honor?0:1;
        room.slots[0].name=c.name;room.slots[1].name=invited->name;
        const auto battle=crusades_->issueBattle(campaign,saved.revision,territory,campaignContext(room,false),campaignNow(),room.campaignExpires);
        issued=battle.id;room.campaignBattleId=battle.id;room.campaignLaunchToken=battle.launchToken;
        room.slotClient[0]=int(c.id);
        rooms_.emplace(room.id,std::move(room));
        c.cmdQueue.clear();c.state=Client::InGame;c.roomId=roomId;c.slot=0;c.loaded=false;
        const auto& stored=rooms_.at(roomId);
        const auto reply=battleReply(0,campaign,issued,roomId,*map,stored.campaignExpires);
        c.conn.send(Msg::CrusadesBattleResult,reply);invited->conn.send(Msg::CrusadesBattleResult,reply);
        campaignMatchQueue_.erase(caller);campaignMatchQueue_.erase(opponent);++campaignMatchGeneration_;
        notifyCampaignBattle(issued);
        Writer joined;joined.u8(1);joined.u8(0);joined.str("");c.conn.send(Msg::JoinResult,joined);
        broadcastLobby(rooms_.at(roomId));
        return true;
    } catch(const std::exception& e) {
        if(roomId && rooms_.count(roomId))closeCampaignLobby(roomId,"campaign issuance failed");
        else if(!issued.empty()) {try{crusades_->cancelBattle(issued,campaignNow());}catch(const std::exception&) {}}
        std::fprintf(stderr,"campaign issuance: %s\n",e.what());
        c.conn.send(Msg::CrusadesBattleResult,battleReply(4,campaign,"",0,"",0,"campaign battle issuance rejected"));
        return false;
    }
}

void Server::sendGameList(Client& c) {
    Writer w;
    uint64_t t = nowMs();
    // Private (single-player) games are not advertised in the browser.
    uint32_t n = 0;
    for (auto& [id, r] : rooms_) if (!r.priv && r.campaignBattleId.empty()) ++n;
    w.u32(n);
    for (auto& [id, r] : rooms_) {
        if (r.priv || !r.campaignBattleId.empty()) continue;
        w.u32(r.id);
        w.str(r.name);
        w.str(r.mapId);
        w.u8(uint8_t(r.usedSlots()));
        w.u8(uint8_t(r.capacity()));
        w.u8(r.running ? 1 : 0);
        w.u8(r.password.empty() ? 0 : 1);
        w.u32(uint32_t((t - r.createdMs) / 1000));
    }
    c.conn.send(Msg::GameList, w);
}

// Snap an incoming unit-cap value to the allowed lobby set (defensive against a
// malformed client); anything unexpected falls back to the 2000 default.
static uint16_t clampUnitCap(uint16_t v) {
    for (uint16_t a : {250, 500, 1000, 2000}) if (v == a) return v;
    return 2000;
}

// `fromStart` writes the table the game BEGAN with instead of the live one. A
// rejoining client replays the whole bundle log from tick 0, so it must rebuild the
// world from the START configuration: hand it the current table and a slot closed
// since (a forfeit, or a drop that ran out its budget) reads as unused, so that
// player's opening units never spawn and the rebuilt world diverges from the game it
// is rejoining -- and any replay that client saves records the wrong match.
void Server::writeSlots(Writer& w, Room& r, bool fromStart) {
    w.u32(r.id);
    w.str(r.name);
    w.str(r.mapId);
    w.str(r.mission);
    w.u8(r.opts.crusades); w.u8(r.opts.forfeitSelfDestruct);
    w.u8(r.opts.overridePolicy);
    w.u8(r.opts.speed); w.u8(r.opts.speedUnlock); w.u32(r.opts.unitCap); w.u8(r.opts.monarchExpendable);
    w.u8(r.opts.stressTest); w.u8(r.opts.fogExplored); w.u8(r.opts.benchmark);
    w.u8(r.opts.randomStarts); w.u8(r.opts.doubleSight); w.u8(uint8_t(r.opts.pathfindingMode));
    w.u32(r.hostId);
    w.u8(mapsReady(r) ? 1 : 0);
    for (int i = 0; i < kMaxSlots; ++i) {
        const SlotInfo& s = (fromStart && r.running) ? r.startSlots[i] : r.slots[i];
        w.u8(s.type); w.u8(s.faction); w.u8(s.color); w.u8(s.team); w.u8(s.ready);
        w.u8(s.aiLevel);
        w.str(s.name);
    }
}

void Server::broadcastLobby(Room& r) {
    Writer w; writeSlots(w, r);
    for (int i = 0; i < kMaxSlots; ++i) {
        if (r.slotClient[i] < 0) continue;
        auto it = clients_.find(uint32_t(r.slotClient[i]));
        if (it != clients_.end()) it->second->conn.send(Msg::LobbyState, w);
    }
    // Lobby spectators (e.g. a host who created the game to watch) see the room
    // fill too -- and this is how they learn hostId, so they can start it.
    for (uint32_t sid : r.spectators) {
        auto it = clients_.find(sid);
        if (it != clients_.end()) it->second->conn.send(Msg::LobbyState, w);
    }
    if (r.mapPackage) for (auto& [id, peer] : clients_) {
        if (peer->roomId == r.id && peer->mapOfferedRoom != r.id) {
            peer->conn.send(Msg::MapOffer, tak::net::maps::offer(r.id, r.mapId, *r.mapPackage));
            peer->mapOfferedRoom = r.id;
        }
    }

    if(r.opts.overridePolicy==2 && r.overridePackage)for(auto& [id,peer]:clients_) {
        if(peer->roomId==r.id && peer->overrideOffered!=r.overridePackage->digest){
            peer->conn.send(Msg::OverrideOffer,tak::net::maps::offer(r.id,"overrides",*r.overridePackage));
            peer->overrideOffered=r.overridePackage->digest;
        }
    }

}

void Server::broadcastRoom(Room& r, Msg kind, const Writer& w, uint32_t exceptClient) {
    for (int i = 0; i < kMaxSlots; ++i) {
        if (r.slotClient[i] < 0) continue;
        uint32_t cid = uint32_t(r.slotClient[i]);
        if (cid == exceptClient) continue;
        auto it = clients_.find(cid);
        // A client still streaming catch-up gets its bundles from the log cursor
        // instead; sending the live one now would land it ahead of the history.
        if (it != clients_.end() &&
            !(kind == Msg::TickBundle && it->second->replaying))
            it->second->conn.send(kind, w);
    }
    // Spectators receive the same stream (bundles, chat, pause/resume, status).
    for (uint32_t cid : r.spectators) {
        if (cid == exceptClient) continue;
        auto it = clients_.find(cid);
        if (it != clients_.end() &&
            !(kind == Msg::TickBundle && it->second->replaying))
            it->second->conn.send(kind, w);
    }
}

void Server::lobbyMsg(Client& c, const Frame& f) {
    switch (f.kind) {
        case Msg::ListGames: sendGameList(c); break;
        case Msg::CreateGame: {
            if(rooms_.size()>=limits_.rooms) {sendReject(c,"server game capacity reached");return;}
            Reader r(f.payload.data(), f.payload.size());
            std::string name = r.str(), pass = r.str(), mapId = r.str(), mission = r.str();
            GameOptions o; o.crusades = r.u8(); o.forfeitSelfDestruct = r.u8();
            o.overridePolicy = std::min<uint8_t>(r.u8(),2);
            if(!mission.empty())o.overridePolicy=0;
            o.speed = r.u8(); o.speedUnlock = r.u8();
            if (o.speed < 1) o.speed = 10;
            o.unitCap = clampUnitCap(uint16_t(r.u32()));
            o.monarchExpendable = r.u8() ? 1 : 0;
            o.stressTest = r.u8() ? 1 : 0;
            // THREE-state, not a bool: 0 = not explored, 1 = explored, 2 = full
            // vision. Decoding it as `? 1 : 0` silently folded FULL VISION back to
            // EXPLORED, so the room's fog button cycled through a setting the
            // server threw away -- it looked like the button did nothing.
            o.fogExplored = std::min<uint8_t>(r.u8(), 2);
            o.benchmark = r.u8();
            o.randomStarts = r.u8() ? 1 : 0;
            o.doubleSight = r.u8() ? 1 : 0;
            const auto pathMode = r.u8();
            if (pathMode > uint8_t(tak::sim::PathfindingMode::Flowfield)) { sendReject(c,"invalid pathfinding mode"); return; }
            o.pathfindingMode = mission.empty() ? tak::sim::PathfindingMode(pathMode) : tak::sim::PathfindingMode::Retail;
            if (!mission.empty()) o.doubleSight = 0;
            int cap = int(r.u8());
            uint8_t spectate = r.u8();   // host watches, taking no slot (all-AI game)
            uint8_t priv = r.u8();       // private (single-player): hidden from the list
            if (!r.ok || r.p!=r.end || name.size()>128 || pass.size()>128 || mapId.size()>4096 || mission.size()>256) return;
            if(!(testWork_ || (loopbackOnly_ && !requireAuth_)) && (o.stressTest || o.benchmark || !mission.empty())) {
                sendReject(c,"public servers do not accept benchmark, stress or campaign mission requests");return;
            }
            o.speed=std::clamp<uint8_t>(o.speed,1,kMaxGameSpeed);
            if (cap < 2 || cap > kMaxSlots) cap = kMaxSlots;   // sane default
            Room& room = rooms_[nextRoomId_];
            room.id = nextRoomId_++;
            room.name = name.empty() ? ("game" + std::to_string(room.id)) : name;
            room.password = pass;
            room.mapId = mapId;
            room.mission = mission;
            room.opts = o;
            room.priv = priv != 0;
            room.cap = cap;
            room.hostId = c.id;
            room.createdMs = nowMs();
            // Roll this game's seed here, once. It is broadcast in GameStarting, so
            // every peer and the referee derive the same arrangement from it -- it
            // just must not be a function of the room id (see Room::seed).
            room.seed = fixedSeed_ ? fixedSeed_ : uint32_t(randToken());
            // Open slots up to the map capacity, close the rest (the map has no
            // start position for them).
            for (int i = 0; i < kMaxSlots; ++i) {
                room.slots[i].type = i < cap ? 0 : 3;
                room.slots[i].color = uint8_t(i);
                room.slots[i].team = uint8_t(i);
            }
            if (spectate) {
                // Host watches without a slot -- every capacity slot stays open (fill
                // with AIs). The host still owns the room (add AIs, start).
                room.spectators.push_back(c.id);
                c.cmdQueue.clear();   // never inherit a previous room's queue
            c.state = Client::InGame; c.roomId = room.id; c.slot = -1;
                Writer jr; jr.u8(1); jr.u8(0xFF); jr.str("");   // ok, spectator
                c.conn.send(Msg::JoinResult, jr);
            } else {
                // Host takes slot 0 (human).
                room.slots[0].type = 1; room.slots[0].faction = 0; room.slots[0].color = 0;
                room.slots[0].team = 0; room.slots[0].name = c.name;
                room.slotClient[0] = int(c.id);
                c.cmdQueue.clear();   // never inherit a previous room's queue
            c.state = Client::InGame; c.roomId = room.id; c.slot = 0;
                Writer jr; jr.u8(1); jr.u8(0); jr.str("");   // ok, slot 0
                c.conn.send(Msg::JoinResult, jr);
            }
            broadcastLobby(room);
            std::fprintf(stderr, "client %u created game %u '%s'\n", c.id, room.id, room.name.c_str());
            break;
        }
        case Msg::JoinGame: {
            Reader r(f.payload.data(), f.payload.size());
            uint32_t gid = r.u32(); std::string pass = r.str();
            if (!r.ok) return;
            auto it = rooms_.find(gid);
            auto reject = [&](const std::string& why) {
                Writer jr; jr.u8(0); jr.u8(0); jr.str(why); c.conn.send(Msg::JoinResult, jr);
            };
            if (it == rooms_.end()) { reject("no such game"); break; }
            Room& room = it->second;
            if (room.running) { reject("game already started"); break; }
            if (!room.password.empty() && room.password != pass) { reject("wrong password"); break; }
            int freeSlot = -1;
            if(!room.campaignBattleId.empty()) {
                if(!requireAuth_ || c.account.empty() || c.state!=Client::Lobby || c.roomId ||
                    tak::auth::foldUsername(c.account)!=room.campaignAccounts[1] || room.slotClient[1]>=0 ||
                    campaignNow()>=room.campaignExpires) {reject("not an eligible campaign participant");break;}
                try {
                    if(crusades_->battle(room.campaignBattleId).status!=tak::srv::crusades::BattleStatus::Issued)
                        throw std::runtime_error("campaign battle unavailable");
                } catch(const std::exception&) {reject("campaign battle unavailable");break;}
                freeSlot=1;
            } else for (int i = 0; i < kMaxSlots; ++i)
                if (room.slots[i].type == 0) { freeSlot = i; break; }
            if (freeSlot < 0) { reject("game is full"); break; }
            room.slots[freeSlot].type = 1;
            room.slots[freeSlot].name = c.name;
            room.slotClient[freeSlot] = int(c.id);
            c.cmdQueue.clear();   // never inherit a previous room's queue
            c.state = Client::InGame; c.roomId = room.id; c.slot = freeSlot;
            Writer jr; jr.u8(1); jr.u8(uint8_t(freeSlot)); jr.str("");
            c.conn.send(Msg::JoinResult, jr);
            broadcastLobby(room);
            std::fprintf(stderr, "client %u joined game %u at slot %d\n", c.id, room.id, freeSlot);
            break;
        }
        case Msg::Rejoin: {
            Reader r(f.payload.data(), f.payload.size());
            uint32_t gid = r.u32(); uint64_t token = r.u64();
            if (!r.ok || r.p!=r.end) return;
            auto it = rooms_.find(gid);
            auto reject = [&](const std::string& why) {
                Writer jr; jr.u8(0); jr.u8(0); jr.str(why); c.conn.send(Msg::JoinResult, jr);
            };
            if (it == rooms_.end() || !it->second.running) { reject("game not found or ended"); break; }
            Room& room = it->second;
            int slot = -1;
            for (int i = 0; i < kMaxSlots; ++i)
                if (room.slotDropped[i] && token != 0 && room.slotToken[i] == token) { slot = i; break; }
            if (slot < 0) { reject("invalid or expired resume token"); break; }
            if(!room.campaignBattleId.empty()) {
                if(room.campaignResult || room.campaignFault || room.campaignResultRecorded) {
                    reject("campaign battle has ended");break;
                }
                if(slot>=2 || !requireAuth_ || c.account.empty() || tak::auth::foldUsername(c.account)!=room.campaignAccounts[slot]) {
                    reject("resume account does not match campaign participant");break;
                }
                try { (void)crusades_->authorizeBattleReport(room.campaignBattleId,room.campaignRoomToken,campaignContext(room,false),campaignNow()); }
                catch(const std::exception&) {reject("campaign battle is no longer eligible");break;}
            }
            // Re-seat the client and rotate the token (single use).
            room.slotClient[slot] = int(c.id);
            room.slotDropped[slot] = false;
            if(!room.campaignBattleId.empty() && slot<2)room.campaignDisconnected[slot]=false;
            room.slotToken[slot] = randToken();
            room.desyncFlagged.clear();   // fresh sim; old desync flags are stale
            c.cmdQueue.clear();   // never inherit a previous room's queue
            c.state = Client::InGame; c.roomId = gid; c.slot = slot; c.loaded = true;
            // GameStarting rebuilds the client's world; then the whole bundle log
            // replays it up to now, after which it receives live bundles. Because that
            // rebuild starts at tick 0, it needs the START slot table -- see writeSlots.
            Writer w; writeSlots(w, room, /*fromStart=*/true);
            w.u8(uint8_t(slot)); w.u32(room.seed); w.u64(room.slotToken[slot]);
            // Where the HISTORY ends. The client must not send -- or treat its own
            // commands coming back as acknowledgements -- until it has consumed
            // every bundle logged before it rejoined. It cannot work that out
            // locally: the replay is streamed in chunks paced by the socket, so its
            // receive buffer legitimately runs dry BETWEEN chunks while history is
            // still coming, and a buffer-depth guess releases the gate early.
            w.u32(uint32_t(room.log.size()));
            if (room.mapPackage) {
                c.mapReadyRoom = 0; c.mapOfferedRoom = room.id;
                c.conn.send(Msg::MapOffer, tak::net::maps::offer(room.id, room.mapId, *room.mapPackage));
            }
            if(room.overridePackage)c.conn.send(Msg::OverrideOffer,tak::net::maps::offer(room.id,"overrides",*room.overridePackage));
            c.conn.send(Msg::GameStarting, w);
            c.replaying = true; c.replayPos = 0;   // streamed below, paced by txPending()
            // Unpause if this was the player we were waiting on.
            if (room.paused && room.pausePlayer == slot) {
                room.pauseBudgetMs[slot] -= std::min(room.pauseBudgetMs[slot], nowMs() - room.pauseStartMs);
                room.paused = false; room.pausePlayer = -1;
                room.nextTickMs = nowMs();   // rebase the tick clock (no burst)
                Writer rw; rw.u8(0); rw.u8(uint8_t(slot));
                broadcastRoom(room, Msg::Resume, rw);
            }
            Writer ps; ps.u8(uint8_t(slot)); ps.u8(0 /*connected*/); ps.u32(0);
            broadcastRoom(room, Msg::PlayerStatus, ps, c.id);
            std::fprintf(stderr, "game %u: client %u REJOINED slot %d (replaying %zu ticks)\n",
                         room.id, c.id, slot, room.log.size());
            break;
        }
        case Msg::Spectate: {
            Reader r(f.payload.data(), f.payload.size());
            uint32_t gid = r.u32(); std::string pass = r.str();
            if (!r.ok) return;
            auto it = rooms_.find(gid);
            auto reject = [&](const std::string& why) {
                Writer jr; jr.u8(0); jr.u8(0); jr.str(why); c.conn.send(Msg::JoinResult, jr);
            };
            if (it == rooms_.end() || !it->second.running) { reject("game not found or ended"); break; }
            Room& room = it->second;
            if(!room.campaignBattleId.empty()) {reject("campaign battles do not allow spectators");break;}
            if (!room.password.empty() && room.password != pass) { reject("wrong password"); break; }
            // Seat nothing: a spectator has no slot, sends no commands or hashes,
            // and holds nothing on disconnect. It just receives the stream.
            if (std::find(room.spectators.begin(), room.spectators.end(), c.id) == room.spectators.end())
                room.spectators.push_back(c.id);
            c.cmdQueue.clear();   // never inherit a previous room's queue
            c.state = Client::InGame; c.roomId = gid; c.slot = -1; c.loaded = true;
            // GameStarting (slot 0xFF = spectator) rebuilds the world; the whole
            // bundle log replays it to now, then live bundles stream via broadcast.
            // Same reason as the rejoin above: that rebuild starts at tick 0, so it
            // needs the START table, not one with mid-game forfeits already applied.
            Writer w; writeSlots(w, room, /*fromStart=*/true);
            w.u8(0xFF); w.u32(room.seed); w.u64(0);
            w.u32(uint32_t(room.log.size()));   // replay boundary (see above)
            if (room.mapPackage) {
                c.mapReadyRoom = 0; c.mapOfferedRoom = room.id;
                c.conn.send(Msg::MapOffer, tak::net::maps::offer(room.id, room.mapId, *room.mapPackage));
            }
            if(room.overridePackage)c.conn.send(Msg::OverrideOffer,tak::net::maps::offer(room.id,"overrides",*room.overridePackage));
            c.conn.send(Msg::GameStarting, w);
            c.replaying = true; c.replayPos = 0;   // streamed below, paced by txPending()
            std::fprintf(stderr, "game %u: client %u SPECTATING (replaying %zu ticks)\n",
                         room.id, c.id, room.log.size());
            break;
        }
        default: break;   // ignore other messages while in the lobby
    }
}

// Everything a connection has in flight for a room, dropped in one place.
//
// Queued commands are ROOM state living on a CONNECTION: left behind they drain
// into whatever game this connection joins next, carrying the old player stamp
// and unit ids into a world where those ids mean something else.
//
// The SCHEDULED ones matter for a different reason. With server input delay
// (TAK_SRV_DELAY > 0) a departing player's commands sit in pendingAt buckets
// several ticks out, which puts them in bundles AFTER the replay boundary a
// rejoin is handed -- so they arrive looking like acknowledgements for commands
// sent after the rejoin. Safe to purge: nothing here has been bundled or
// broadcast yet, so every peer still agrees. A player who just dropped should not
// have orders fire seconds later in any case.
//
// Called from BOTH disconnect paths. It first lived only in leaveRoom, which a
// seated player disconnecting from a running game never reaches -- that takes the
// slot-preserving branch of dropClient, which is exactly the rejoin path this is
// meant to protect. The fix ran everywhere except where it was needed.
void Server::dropPendingCommands(Client& c, Room& r) {
    c.overrideSend={};c.overrideReceive={};c.overrideReady.clear();c.overrideOffered.clear();
    c.mapSend = {}; c.mapReceive = {}; c.mapReadyRoom = c.mapOfferedRoom = 0;
    c.cmdQueue.clear();
    c.cmdDropped = 0;
    if (c.slot < 0) return;
    const int slot = c.slot;
    size_t purged = 0;
    for (auto& [tick, cmds] : r.pendingAt) {
        const size_t before = cmds.size();
        cmds.erase(std::remove_if(cmds.begin(), cmds.end(),
                                  [slot](const Command& q) { return int(q.player) == slot; }),
                   cmds.end());
        purged += before - cmds.size();
    }
    if (purged)
        std::fprintf(stderr, "game %u: slot %d detached -- purged %zu scheduled command(s)\n",
                     r.id, slot, purged);
}

void Server::leaveRoom(Client& c, const char* reason) {
    Room* r = roomOf(c);
    if (!r) return;
    if(!r->campaignBattleId.empty()) {
        if(!r->running) {closeCampaignLobby(r->id,"campaign participant left");return;}
        finalizeCampaign(*r);
        if(c.slot>=0 && c.slot<2 && !r->campaignResult) {
            if(std::strcmp(reason,"left")==0) r->campaignResigned[c.slot]=true;
            else r->campaignDisconnected[c.slot]=true;
        }
    }
    dropPendingCommands(c, *r);
    // A spectator just detaches from the stream -- no slot, nothing to forfeit.
    {
        auto& sp = r->spectators;
        auto it = std::find(sp.begin(), sp.end(), c.id);
        if (it != sp.end()) {
            sp.erase(it);
            c.validationId=0;c.deferredMapOffer.reset();
            c.state = Client::Lobby; c.roomId = 0; c.slot = -1; c.loaded = false;
            std::fprintf(stderr, "client %u stopped spectating game %u (%s)\n", c.id, r->id, reason);
            return;
        }
    }
    if (c.slot >= 0 && c.slot < kMaxSlots) {
        r->slotClient[c.slot] = -1;
        if (!r->running) { r->slots[c.slot].type = 0; r->slots[c.slot].name.clear(); }
        else {
            // Voluntary leave mid-game = immediate forfeit (sequenced event so the
            // sim disposes the units in lockstep).
            r->slots[c.slot].type = 3;
            r->pendingEvents.push_back({r->campaignBattleId.empty()?tak::net::Event::Kind::Leave:tak::net::Event::Kind::CampaignForfeit, uint8_t(c.slot)});
        }
    }
    uint32_t rid = r->id;
    bool wasHost = (r->hostId == c.id);
    c.validationId=0;c.deferredMapOffer.reset();
    c.state = Client::Lobby; c.roomId = 0; c.slot = -1; c.loaded = false;
    // If the host left in the lobby, pass host to the next human (or dissolve).
    if (!r->running && wasHost) {
        int next = -1;
        for (int i = 0; i < kMaxSlots; ++i)
            if (r->slotClient[i] >= 0) { next = r->slotClient[i]; break; }
        if (next >= 0) r->hostId = uint32_t(next);
        else { rooms_.erase(rid); std::fprintf(stderr, "game %u dissolved\n", rid); return; }
    }
    broadcastLobby(*r);   // reachable only when the room still exists (not dissolved)
    std::fprintf(stderr, "client %u left game %u (%s)\n", c.id, rid, reason);
}

bool Server::mapsReady(const Room& r) const {
    if(r.opts.overridePolicy==2){
        if(!r.overridePackage)return false;
        for(const auto& [id,peer]:clients_)if(peer->roomId==r.id && peer->overrideReady!=r.overridePackage->digest)return false;
    }
    if (!r.mission.empty() || tak::mapgen::isGeneratedMapId(r.mapId)) return true;
    if (!r.mapPackage) return false;
    for (const auto& [id, peer] : clients_)
        if (peer->roomId == r.id && peer->mapReadyRoom != r.id) return false;
    return true;
}
void Server::validatePackage(Client& c,Room& room,bool overrides,bool cache) {
    if(validations_.size()>=3)throw std::runtime_error("map validation capacity reached; retry later");
    auto& receive=overrides?c.overrideReceive:c.mapReceive;
    if(!receive.size || mapMemory()>limits_.mapMemory)throw std::runtime_error("server map memory budget reached");
    Validation job{nextValidation_++,c.id,room.id,receive.size,overrides,cache,receive.digest,room.mapId,{}, {}};
    const auto token=job.stop.get_token();
    const auto root=std::filesystem::u8path(mapRoot_);
    const auto digest=job.digest;const auto size=job.size;const auto quota=limits_.mapDisk;
    job.result=validationWorker_.submit([root,digest,size,quota,overrides,cache,token,bytes=std::move(receive.bytes)]() mutable {
        if(token.stop_requested())throw std::runtime_error("map validation cancelled");
        std::shared_ptr<tak::net::maps::Package> package;
        if(cache)package=overrides?tak::net::overrides::loadCache(root,digest,size,size):tak::net::maps::loadCache(root,digest,size,size,token);
        else {
            if(bytes.size()!=size)throw std::runtime_error("uploaded package size mismatch");
            package=overrides?tak::net::overrides::decode(std::move(bytes),digest):tak::net::maps::decode(std::move(bytes),digest,token);
        }
        if(token.stop_requested())throw std::runtime_error("map validation cancelled");
        if(package && !cache) {
            if(overrides)tak::net::overrides::saveCache(root,*package,quota);
            else tak::net::maps::saveCache(root,*package,quota);
        }
        return package;
    });
    c.validationId=job.id;receive={};
    validations_.push_back(std::move(job));
}
void Server::finishValidations() {
    for(auto it=validations_.begin();it!=validations_.end();) {
        auto peer=clients_.find(it->client);auto room=rooms_.find(it->room);
        const bool valid=peer!=clients_.end() && room!=rooms_.end() &&
            peer->second->validationId==it->id && peer->second->roomId==it->room &&
            room->second.hostId==it->client && !room->second.running && room->second.mapId==it->map &&
            (!it->overrides || room->second.opts.overridePolicy==2);
        if(!valid)it->stop.request_stop();
        if(it->result.wait_for(std::chrono::seconds(0))!=std::future_status::ready) {++it;continue;}
        auto job=std::move(*it);it=validations_.erase(it);
        try {
            auto package=job.result.get();
            if(!valid)continue;
            auto& c=*peer->second;auto& r=room->second;c.validationId=0;
            if(!package && job.cache) {
                if(uint64_t(mapMemory())+uint64_t(job.size)*2>limits_.mapMemory)throw std::runtime_error("server map memory budget reached");
                auto& receive=job.overrides?c.overrideReceive:c.mapReceive;
                receive.begin(job.room,job.size,job.digest);
                if(job.overrides)c.overrideStarted=c.overrideProgress=nowMs();
                else c.mapReceiveStarted=c.mapReceiveProgress=nowMs();
                Writer request;request.u32(job.room);c.conn.send(job.overrides?Msg::OverrideRequest:Msg::MapRequest,request);
                continue;
            }
            if(!package || package->bytes.size()!=job.size || uint64_t(mapMemory())+uint64_t(package->bytes.size())*2>limits_.mapMemory)
                throw std::runtime_error("package size/budget mismatch");
            if(job.overrides) {
                r.overridePackage=std::move(package);broadcastLobby(r);
            } else acceptMap(r,std::move(package));
        } catch(const std::exception& e) {
            if(valid) {
                auto& c=*peer->second;c.validationId=0;
                Writer error;error.u32(job.room);error.str(e.what());c.conn.send(job.overrides?Msg::OverrideError:Msg::MapError,error);
            }
        }
    }
    for(auto& [id,peer]:clients_)if(!peer->validationId && !peer->overrideReceive.size && peer->deferredMapOffer) {
        auto frame=std::move(*peer->deferredMapOffer);peer->deferredMapOffer.reset();
        mapMsg(*peer,frame);
    }
}
void Server::acceptMap(Room& room, std::shared_ptr<tak::net::maps::Package> package) {
    if(uint64_t(mapMemory())+uint64_t(package->bytes.size())*2>limits_.mapMemory)throw std::runtime_error("server map memory budget reached");
    room.mapPackage = std::move(package);
    std::fprintf(stderr, "game %u: map verified %s (%zu bytes)\n", room.id,
                 room.mapPackage->digest.c_str(), room.mapPackage->bytes.size());
    room.mapVfs = std::make_unique<tak::hpi::Vfs>(&roomData(room).vfs);
    room.mapVfs->setMapFiles(room.mapPackage->files);
    broadcastLobby(room);
}
void Server::overrideMsg(Client& c,const Frame& f) {
    Room* room=roomOf(c);if(!room || !room->mission.empty() || !room->campaignBattleId.empty())return;
    try {
        Reader rd(f.payload.data(),f.payload.size());
        if(f.kind==Msg::OverrideOffer){
            if(room->running || room->hostId!=c.id || room->opts.overridePolicy!=2 || c.overrideReceive.size || c.validationId)return;
            const auto id=rd.u32();const auto label=rd.str(),digest=rd.str();const auto count=rd.u32();
            if(!rd.ok || rd.p!=rd.end || id!=room->id || label!="overrides")throw std::runtime_error("invalid override offer");
            if(count>tak::net::maps::kMaxBytes || uint64_t(mapMemory())+uint64_t(count)*2>limits_.mapMemory)throw std::runtime_error("server override memory budget reached");
            c.overrideReceive.begin(id,count,digest);c.overrideStarted=c.overrideProgress=nowMs();
            room->overrideData.reset();room->overridePackage.reset();
            room->mapVfs.reset();room->mapPackage.reset();
            for(auto& [pid,peer]:clients_)if(peer->roomId==id){peer->mapReadyRoom=peer->mapOfferedRoom=0;peer->mapSend={};peer->mapReceive={};peer->validationId=0;peer->deferredMapOffer.reset();}
            for(auto& [pid,peer]:clients_)if(peer->roomId==id){peer->overrideReady.clear();peer->overrideOffered.clear();peer->overrideSend={};if(pid!=c.id)peer->overrideReceive={};}
            broadcastLobby(*room);
            validatePackage(c,*room,true,true);
        }else if(f.kind==Msg::OverrideChunk){
            if(room->running || room->hostId!=c.id || room->opts.overridePolicy!=2)return;
            c.overrideProgress=nowMs();if(c.overrideReceive.append(rd))validatePackage(c,*room,true,false);
        }else if(f.kind==Msg::OverrideRequest){
            auto id=rd.u32();auto digest=rd.str();
            if(!rd.ok || rd.p!=rd.end || id!=room->id || !room->overridePackage || digest!=room->overridePackage->digest || c.overrideSend.package)return;
            c.overrideSend={id,0,room->overridePackage};
        }else if(f.kind==Msg::OverrideReady){
            auto id=rd.u32();auto digest=rd.str();
            if(!rd.ok || rd.p!=rd.end || id!=room->id)throw std::runtime_error("override verification failed");
            if(!room->overridePackage || digest!=room->overridePackage->digest)return;
            c.overrideReady=digest;if(!room->running)broadcastLobby(*room);
        }else if(f.kind==Msg::OverrideError){
            auto id=rd.u32();auto why=rd.str();if(rd.ok && rd.p==rd.end && id==room->id && why.size()<=512){c.overrideReady.clear();c.overrideSend={};c.overrideReceive={};}
        }
    }catch(const std::exception& e){
        c.validationId=0;c.overrideReceive={};c.overrideSend={};c.overrideReady.clear();
        Writer error;error.u32(room->id);error.str(e.what());c.conn.send(Msg::OverrideError,error);
    }
}

void Server::mapMsg(Client& c, const Frame& f) {
    Room* room = roomOf(c); if (!room) return;
    if(!room->campaignBattleId.empty() && (f.kind==Msg::MapOffer || f.kind==Msg::MapChunk))return;
    try {
        Reader rd(f.payload.data(), f.payload.size());
        if (f.kind == Msg::MapOffer) {
            if (room->running || room->hostId != c.id || room->mapPackage || c.mapReceive.size) return;
            auto id = rd.u32(); auto name = rd.str(), hash = rd.str(); auto size = rd.u32();
            if (!rd.ok || rd.p != rd.end || id != room->id || name != room->mapId ||
                !room->mission.empty() || tak::mapgen::isGeneratedMapId(name))
                throw std::runtime_error("invalid map offer");
            // Clients pipeline their map offer behind override chunks. Hold one
            // bounded offer until the asynchronous override validation finishes.
            if(c.validationId || c.overrideReceive.size) {
                if(f.payload.size()>2048)throw std::runtime_error("map offer too large");
                c.deferredMapOffer=f;return;
            }
            // Reserve the advertised upload, not only bytes received so far.
            // Decoding keeps both the canonical package and extracted files.
            if(size>tak::net::maps::kMaxBytes || uint64_t(mapMemory())+uint64_t(size)*2>limits_.mapMemory)
                throw std::runtime_error("server map memory budget reached");
            c.mapReceive.begin(id, size, hash);
            c.mapReceiveStarted=c.mapReceiveProgress=nowMs();
            // Installed maps follow the same content-addressed upload path on
            // a cache miss; never rebuild arbitrary maps on the network thread.
            validatePackage(c,*room,false,true);
        } else if (f.kind == Msg::MapChunk) {
            if (room->running || room->hostId != c.id || room->mapPackage) return;
            c.mapReceiveProgress=nowMs();
            if (c.mapReceive.append(rd)) {
                validatePackage(c,*room,false,false);
            }
        } else if (f.kind == Msg::MapRequest) {
            auto id = rd.u32();
            if (!rd.ok || id != room->id || !room->mapPackage || c.mapSend.package) return;
            c.mapSend = {room->id, 0, room->mapPackage};
        } else if (f.kind == Msg::MapReady) {
            auto id = rd.u32(); auto hash = rd.str();
            if (!rd.ok || id != room->id)throw std::runtime_error("map verification failed");
            if(!room->mapPackage || hash!=room->mapPackage->digest)return;
            c.mapReadyRoom = room->id;
            if (!room->running) broadcastLobby(*room);
        } else if (f.kind == Msg::MapError) {
            auto id = rd.u32(); auto why = rd.str();
            if (rd.ok && rd.p==rd.end && why.size()<=512 && id == room->id) {
                c.validationId=0;c.mapReadyRoom = 0; c.mapReceive = {}; c.mapSend = {};
                Writer chat; chat.str("SERVER"); chat.str(c.name + ": " + why);
                broadcastRoom(*room, Msg::Chat, chat);
            }
        }
    } catch (const std::exception& e) {
        c.validationId=0;c.mapReadyRoom = 0; c.mapReceive = {}; c.mapSend = {};
        Writer error; error.u32(room->id); error.str(e.what()); c.conn.send(Msg::MapError, error);
    }
}

void Server::tryStart(Client& c) {
    Room* r = roomOf(c);
    if (!r || r->hostId != c.id || r->running) return;
    if(std::count_if(rooms_.begin(),rooms_.end(),[](const auto& entry){return entry.second.running;})>=limits_.running) {
        sendReject(c,"server running-game capacity reached; retry later");return;
    }
    if(!r->campaignBattleId.empty()) {
        try { (void)campaignContext(*r,true); }
        catch(const std::exception&) {return;}
        if(campaignNow()>=r->campaignExpires) {closeCampaignLobby(r->id,"campaign battle expired");return;}
    }
    if (!mapsReady(*r)) {
        Writer w; w.u32(r->id); w.str("waiting for every player and the server to verify the map");
        c.conn.send(Msg::MapError, w); return;
    }
    // Validate: >=2 used slots, every human ready, unique colors among used slots.
    // Authored playtests may be solo objectives. The exception requires the verified
    // package's explicit scenario marker and valid CRT, not a host-supplied claim.
    // Campaign missions retain their separate minimum-player exemption.
    const bool authored=!r->opts.stressTest && !r->opts.benchmark && r->mapPackage &&
        tak::net::maps::authoredScenario(*r->mapPackage);
    if (r->mission.empty() && r->usedSlots() < (authored ? 1 : 2)) return;
    bool usedColor[10] = {};
    for (int i = 0; i < kMaxSlots; ++i) {
        const SlotInfo& s = r->slots[i];
        if (s.type != 1 && s.type != 2) continue;
        if (s.type == 1 && !s.ready) return;             // a human isn't ready
        if (s.color < 10) { if (usedColor[s.color]) return; usedColor[s.color] = true; }
    }
    // The referee is MANDATORY, so resolve what it needs before committing to run.
    // Failing here used to leave r->ref null and the game carried on as a relay --
    // silently giving up the canonical hash and, for a mission, the strategic AI.
    // A game that cannot be refereed does not start; the host is told why.
    if(r->opts.overridePolicy==2 && !r->overrideData) {
        if(!r->overridePackage){sendReject(c,"waiting for override packs");return;}
        try {
            auto ds=std::make_unique<DataSet>();ds->vfs=tak::hpi::Vfs(&retail_.vfs);
            ds->vfs.setOverrideFiles(r->overridePackage->files);
            tak::sim::setupRegistry(ds->reg,ds->vfs,false);
            if(retail_.haveCb){tak::sim::setupRegistry(ds->regCb,ds->vfs,true);ds->haveCb=true;}
            ds->hash=tak::hpi::gameplayHash(ds->vfs);ds->built=true;r->overrideData=std::move(ds);
            if(r->mapPackage){r->mapVfs=std::make_unique<tak::hpi::Vfs>(&r->overrideData->vfs);r->mapVfs->setMapFiles(r->mapPackage->files);}
        }catch(const std::exception& e){sendReject(c,std::string("cannot load override packs: ")+e.what());return;}
    }
    DataSet& dataSet = roomData(*r);
    const bool wantMission = !r->mission.empty();
    std::string mapResolved = wantMission ? std::string()
                                          : r->mapPackage ? r->mapPackage->mapPath
                                          : tak::hpi::findMap(dataSet.vfs, r->mapId);
    if (!wantMission && mapResolved.empty()) {
        char msg[192];
        std::snprintf(msg, sizeof msg,
                      "the server has no map '%s' in its game data, so it cannot "
                      "referee this game", r->mapId.c_str());
        sendReject(c, msg);
        std::fprintf(stderr, "game %u: refusing to start -- map '%s' not in server data\n",
                     r->id, r->mapId.c_str());
        return;
    }
    if (!wantMission && tak::mapgen::isGeneratedMapId(r->mapId)) {
        try { tak::net::maps::saveGenerated(std::filesystem::u8path(mapRoot_), dataSet.vfs, r->mapId,limits_.mapDisk); }
        catch (const std::exception& e) {
            Writer w; w.u32(r->id); w.str(std::string("cannot save generated map: ") + e.what());
            c.conn.send(Msg::MapError, w); return;
        }
    }
    r->running = true;
    r->tick = 0;
    r->nextTickMs = nowMs();
    for (int i = 0; i < kMaxSlots; ++i) r->startSlots[i] = r->slots[i];   // for the replay
    // Build the referee sim (and AI controllers) if we have game data. The world
    // is built by the SAME setupMatch the clients use, so its hash is canonical.
    // Everything the referee reads comes from the data set for THIS game's override
    // tier (retail for none/cosmetic, full for full), so the referee's sim matches
    // the clients that adopted the same tier.
    DataSet* ds = &dataSet;
    const bool isMission = wantMission;
    // A campaign mission builds its own world (placements + the in-sim god script) and
    // needs no map id; a skirmish resolves its map by name (resolved above, where a
    // failure refuses the start).
    const std::string& mapPath = mapResolved;
    {
        r->reg = r->opts.crusades && dataSet.haveCb ? &dataSet.regCb : &dataSet.reg;
        r->ref = std::make_unique<tak::sim::World>();
        r->ref->setVisPlayer(-1);   // headless referee: no fog pass
        if (isMission) {
            // The client builds the SAME world (setupMission is deterministic), so the
            // referee and every peer stay in lockstep.
            int human = 0;
            tak::sim::MissionSetup ms;
            if (!tak::sim::setupMission(*r->ref, *r->reg, ds->vfs, r->mission, human, &ms)) {
                // Same rule as a missing map: no referee, no game.
                char msg[192];
                std::snprintf(msg, sizeof msg,
                              "the server has no mission '%s' in its game data, so it "
                              "cannot referee this game", r->mission.c_str());
                sendReject(c, msg);
                std::fprintf(stderr, "game %u: refusing to start -- mission '%s' not in "
                             "server data\n", r->id, r->mission.c_str());
                r->ref.reset();
                r->running = false;
                return;
            } else {
                // Give every "strategic opponent" a brain. The mission script places
                // and choreographs units, but nothing made those bases BUILD or
                // counterattack -- every one of the shipped missions declares at
                // least one strategic player, so they all played as static
                // set-pieces. The AI lives server-side and emits ordinary commands
                // into the tick stream, exactly as it does for a skirmish, so
                // lockstep is unaffected. A "passive neutral" gets nothing: it is
                // scenery. Mana income is deliberately NOT multiplied here -- a
                // mission is balanced around its placements, not a skirmish curve.
                const tak::ai::Profile& prof =
                    ms.aiProfile.empty() ? aiProfile_
                                         : missionProfile(ds->vfs, ms.aiProfile);
                for (int slot : ms.aiSlots) {
                    std::vector<std::pair<float, float>> enemyStarts;
                    for (size_t j = 0; j < ms.slotPos.size(); ++j) {
                        if (int(j) == slot || r->ref->allied(slot, int(j))) continue;
                        if (ms.slotPos[j].first == 0.0f && ms.slotPos[j].second == 0.0f) continue;
                        enemyStarts.push_back(ms.slotPos[j]);
                    }
                    r->ai.emplace_back(slot, *r->reg, prof, r->seed + uint32_t(slot),
                                       tak::ai::Difficulty::Normal, std::move(enemyStarts));
                }
                if (!ms.aiSlots.empty())
                    std::fprintf(stderr, "takserver: mission '%s' -- %zu strategic AI player(s)%s\n",
                                 r->mission.c_str(), ms.aiSlots.size(),
                                 ms.aiProfile.empty() ? "" : (" profile=" + ms.aiProfile).c_str());
            }
        } else {
            int maxSlot = 0;
            for (int i = 0; i < kMaxSlots; ++i)
                if (r->slots[i].type == 1 || r->slots[i].type == 2) maxSlot = i;
            tak::sim::MatchConfig cfg;
            cfg.vfs = r->mapVfs ? r->mapVfs.get() : &ds->vfs;
            cfg.mapPath = mapPath;
            cfg.unitCap = r->opts.unitCap;
            cfg.monarchExpendable = r->opts.monarchExpendable != 0;
            cfg.doubleSight = r->opts.doubleSight != 0;
            cfg.pathfindingMode = r->opts.pathfindingMode;
            cfg.stressTest = r->opts.stressTest != 0;
            cfg.benchmark = r->opts.benchmark;
            cfg.randomStarts = r->opts.randomStarts != 0;
            cfg.startSeed = r->seed;   // the seed sent in GameStarting
            cfg.slots.resize(size_t(maxSlot + 1));
            for (int i = 0; i <= maxSlot; ++i) {
                const auto& s = r->slots[i];
                // Absurd AI slots earn double income (incomeMultFor); derived from the
                // shared aiLevel so the client mirror sets the same factor (lockstep).
                float mm = s.type == 2
                    ? tak::ai::incomeMultFor(tak::ai::difficultyFromLevel(s.aiLevel)) : 1.0f;
                cfg.slots[size_t(i)] = {s.type == 1 || s.type == 2, s.faction % 5, s.team, mm, s.type == 2, s.type == 2 && s.aiLevel == 0};
            }
            std::vector<std::pair<float,float>> spots;
            try { spots = tak::sim::setupMatch(*r->ref, *r->reg, cfg); }
            catch (const std::exception& e) {
                r->ref.reset(); r->running = false; r->ai.clear();
                Writer error; error.u32(r->id); error.str(e.what());
                c.conn.send(Msg::MapError, error);
                return;
            }
            // setupMatch returns start positions in USED-slot order; remap to slot index.
            std::vector<std::pair<float, float>> slotPos(size_t(maxSlot + 1), {0.f, 0.f});
            for (int i = 0, k = 0; i <= maxSlot; ++i)
                if (r->slots[i].type == 1 || r->slots[i].type == 2) {
                    if (k < int(spots.size())) slotPos[size_t(i)] = spots[size_t(k)];
                    ++k;
                }
            r->ai.reserve(size_t(maxSlot + 1));
            for (int i = 0; i <= maxSlot; ++i)
                if (r->slots[i].type == 2) {
                    // The enemy start positions this AI marches on before it has spotted
                    // any units (fog): every used, non-allied slot's start.
                    std::vector<std::pair<float, float>> enemyStarts;
                    for (int j = 0; j <= maxSlot; ++j) {
                        if (j == i || (r->slots[j].type != 1 && r->slots[j].type != 2)) continue;
                        if (r->ref->allied(i, j)) continue;
                        enemyStarts.push_back(slotPos[size_t(j)]);
                    }
                    auto diff = tak::ai::difficultyFromLevel(r->slots[i].aiLevel);
                    r->ai.emplace_back(i, *r->reg, aiProfile_, r->seed,
                                       diff, std::move(enemyStarts));
                }
        }
    }
    if(!r->campaignBattleId.empty()) {
        try {
            crusades_->startBattle(r->campaignBattleId,r->campaignLaunchToken,r->campaignRoomToken,
                campaignContext(*r,true),campaignNow());
            r->campaignLaunchToken.clear(); // Single-use launch credential never sent to clients.
            notifyCampaignBattle(r->campaignBattleId);
        } catch(const std::exception&) {
            r->running=false;r->ref.reset();r->ai.clear();
            closeCampaignLobby(r->id,"campaign battle no longer eligible to start");return;
        }
    }
    // GameStarting: final slot table + options + seed + a per-slot resume token
    // (used to rejoin the held slot after a disconnect).
    for (int i = 0; i < kMaxSlots; ++i) {
        r->pauseBudgetMs[i] = kPauseBudgetMs;
        if (r->slotClient[i] < 0) continue;
        r->slotToken[i] = randToken();
        auto it = clients_.find(uint32_t(r->slotClient[i]));
        if (it == clients_.end()) continue;
        Writer w; writeSlots(w, *r);
        w.u8(uint8_t(i));                 // your slot
        w.u32(r->seed);       // per-game RNG seed base
        w.u64(r->slotToken[i]);           // resume token
        w.u32(0);                         // fresh game: no history to replay
        it->second->conn.send(Msg::GameStarting, w);
        it->second->loaded = false;
    }
    // Spectators (incl. a host who created the game to watch) get a slot-less
    // GameStarting; they don't gate the first tick, so mark them loaded.
    for (uint32_t sid : r->spectators) {
        auto it = clients_.find(sid);
        if (it == clients_.end()) continue;
        Writer w; writeSlots(w, *r);
        w.u8(0xFF); w.u32(r->seed); w.u64(0);
        w.u32(uint32_t(r->log.size()));   // replay boundary
        it->second->conn.send(Msg::GameStarting, w);
        it->second->loaded = true;
    }
    for(int i=0;i<std::min(kMaxSlots,r->ref->numPlayers());++i)r->defeated[size_t(i)]=r->ref->player(i).defeated;
    std::fprintf(stderr, "game %u starting with %d players\n", r->id, r->usedSlots());
}

void Server::gameMsg(Client& c, const Frame& f) {
    Room* r = roomOf(c);
    if (!r) return;
    switch (f.kind) {
        case Msg::SlotUpdate: {
            Reader rd(f.payload.data(), f.payload.size());
            int slot = int(rd.u8());
            uint8_t type = rd.u8(), faction = rd.u8(), color = rd.u8(), team = rd.u8(), ready = rd.u8();
            uint8_t aiLevel = rd.u8();
            if (!rd.ok || slot < 0 || slot >= kMaxSlots || r->running) return;
            if(!r->campaignBattleId.empty()) {
                const auto& current=r->slots[slot];
                if(rd.p!=rd.end || slot!=c.slot || type!=current.type || faction!=current.faction || color!=current.color ||
                    team!=current.team || aiLevel!=current.aiLevel || ready>1)return;
                r->slots[slot].ready=ready;broadcastLobby(*r);break;
            }
            bool isHost = (r->hostId == c.id);
            // A player edits only their own slot; the host may edit any.
            if (!isHost && slot != c.slot) return;
            SlotInfo& s = r->slots[slot];
            // Non-host can only touch faction/color/team/ready on their own slot.
            // Slots past the map capacity stay closed (no start position for them).
            if (isHost) {
                if (type <= 3 && !(slot >= r->cap && type != 3)) {
                    uint8_t old = s.type;
                    s.type = type;
                    if (type == 2 && old != 2) assignAiName(*r, slot);  // new AI: random name
                    else if (type != 2 && old == 2) s.name.clear();     // no longer AI
                }
            }
            s.faction = faction % 5;
            s.color = color % 10;
            s.team = uint8_t(team % kMaxSlots);
            s.ready = ready ? 1 : 0;
            s.aiLevel = aiLevel > 4 ? 2 : aiLevel;   // 0=passive 1=easy 2=normal 3=hard 4=absurd
            broadcastLobby(*r);
            break;
        }
        case Msg::Kick: {
            if(!r->campaignBattleId.empty())return;
            if (r->hostId != c.id || r->running) return;
            Reader rd(f.payload.data(), f.payload.size());
            int slot = int(rd.u8());
            if (!rd.ok || slot < 0 || slot >= kMaxSlots) return;
            int cid = r->slotClient[slot];
            if (cid >= 0 && uint32_t(cid) != c.id) {
                auto it = clients_.find(uint32_t(cid));
                if (it != clients_.end()) { leaveRoom(*it->second, "kicked"); it->second->conn.send(Msg::Bye, Writer{}); }
            }
            break;
        }
        case Msg::SetPause: {
            if(!r->campaignBattleId.empty())return; // Issued matches cannot be held indefinitely by a host.
            // A player asking to pause. Only the host may (in single-player the host
            // IS the only human), and only in a running game. Unlike the drop-driven
            // pause this one has NO budget: nobody has disconnected, so nothing should
            // forfeit -- pausePlayer stays at the sentinel so the budget sweep and the
            // rejoin-resume both skip it.
            if (!r || !r->running || r->hostId != c.id) return;
            Reader rd(f.payload.data(), f.payload.size());
            bool want = rd.u8() != 0;
            if (want && !r->paused) {
                r->paused = true; r->pausePlayer = kPauseByRequest; r->pauseStartMs = nowMs();
                Writer pw; pw.u8(1); pw.u8(0);
                broadcastRoom(*r, Msg::Pause, pw);
                std::fprintf(stderr, "game %u: paused by request\n", r->id);
            } else if (!want && r->paused && r->pausePlayer == kPauseByRequest) {
                r->paused = false; r->pausePlayer = -1; r->nextTickMs = nowMs();
                Writer rw; rw.u8(1); rw.u8(0);
                broadcastRoom(*r, Msg::Resume, rw);
                std::fprintf(stderr, "game %u: resumed by request\n", r->id);
            }
            break;
        }
        case Msg::SetGameOptions: {
            if(!r->campaignBattleId.empty())return;
            if (r->hostId != c.id) return;   // host only
            Reader rd(f.payload.data(), f.payload.size());
            GameOptions o; o.crusades = rd.u8(); o.forfeitSelfDestruct = rd.u8();
            o.overridePolicy = rd.u8(); o.speed = rd.u8(); o.speedUnlock = rd.u8();
            o.unitCap = clampUnitCap(uint16_t(rd.u32())); o.monarchExpendable = rd.u8() ? 1 : 0;
            o.stressTest = rd.u8() ? 1 : 0;
            o.fogExplored = std::min<uint8_t>(rd.u8(), 2);   // 0/1/2, see CreateGame
            o.benchmark = rd.u8();
            o.randomStarts = rd.u8() ? 1 : 0;
            o.doubleSight = rd.u8() ? 1 : 0;
            const auto pathMode = rd.u8();
            if (pathMode > uint8_t(tak::sim::PathfindingMode::Flowfield)) { sendReject(c,"invalid pathfinding mode"); return; }
            // Creation-only: lobby updates cannot change the selected pathfinder.
            o.pathfindingMode = r->opts.pathfindingMode;
            if (!r->mission.empty()) o.doubleSight = 0;
            if (!rd.ok || rd.p!=rd.end) return;
            if(!(testWork_ || (loopbackOnly_ && !requireAuth_)) && (o.stressTest || o.benchmark)) {sendReject(c,"public benchmark/stress requests disabled");return;}
            if (o.speed < 1) o.speed = 1;
            if (o.speed > kMaxGameSpeed) o.speed = kMaxGameSpeed;   // clamp 0.1x .. 8.0x
            if (!r->running) {
                // The unit limit is fixed when the room is created.
                o.unitCap = r->opts.unitCap;
                // Lobby: adopt the remaining options and rebroadcast the slot table.
                o.overridePolicy = std::min<uint8_t>(o.overridePolicy,2);
                if(o.overridePolicy!=r->opts.overridePolicy){
                    r->overrideData.reset();r->overridePackage.reset();
                    r->mapVfs.reset();r->mapPackage.reset();
                    for(auto& [id,peer]:clients_)if(peer->roomId==r->id){peer->mapReadyRoom=peer->mapOfferedRoom=0;peer->mapSend={};peer->mapReceive={};peer->validationId=0;peer->deferredMapOffer.reset();}
                    for(auto& [id,peer]:clients_)if(peer->roomId==r->id){peer->overrideReady.clear();peer->overrideOffered.clear();peer->overrideSend={};peer->overrideReceive={};}
                }
                r->opts = o;
                broadcastLobby(*r);
            } else if (r->opts.speedUnlock && r->opts.speed != o.speed) {
                // In-game: only speed can change (re-cadences the sim), and only if the
                // game was created with speed-unlock. Tell every peer to re-pace.
                r->opts.speed = o.speed;
                Writer w; w.u8(o.speed);
                broadcastRoom(*r, Msg::SpeedUpdate, w);
            }
            break;
        }
        case Msg::StartGame: tryStart(c); break;
        case Msg::Loaded: {
            // The client reports its gameplay-data fingerprint at the room's override
            // tier. With referee data we hold it to the tier's canonical hash, so a
            // Full-tier player missing (or differing on) a gameplay override is caught
            // here, before the first tick, instead of desyncing mid-game.
            Reader rd(f.payload.data(), f.payload.size());
            uint64_t clientHash = rd.u64();
            if(!r->campaignBattleId.empty()) {
                if(!rd.ok || rd.p!=rd.end || !clientHash || clientHash!=roomData(*r).hash || c.slot<0 || c.slot>=2) {
                    r->campaignFault=tak::srv::crusades::ResultOutcome::InvalidClient;
                    c.conn.fail("invalid campaign gameplay fingerprint");return;
                }
                r->campaignLoaded[c.slot]=true;
            }
            if (rd.ok && clientHash != 0) {
                uint64_t want = roomData(*r).hash;
                if (clientHash != want) {
                    char msg[176];
                    std::snprintf(msg, sizeof msg,
                        "gameplay-override mismatch (tier %d: server %016llx, you %016llx) -- "
                        "you don't have the same gameplay overrides as the host",
                        int(r->opts.overridePolicy), (unsigned long long)want,
                        (unsigned long long)clientHash);
                    sendReject(c, msg);
                    c.conn.fail("gamedatahash");
                    std::fprintf(stderr, "game %u: client %u rejected at load: %016llx != %016llx (tier %d)\n",
                        r->id, c.id, (unsigned long long)clientHash, (unsigned long long)want,
                        int(r->opts.overridePolicy));
                    return;
                }
            }
            c.loaded = true;
            // Tell the room who just finished loading, so everyone else's loading
            // screen can fill that player's bar instead of sitting at "waiting".
            {
                int slot = -1;
                for (int i = 0; i < kMaxSlots; ++i)
                    if (r->slotClient[i] == int(c.id)) { slot = i; break; }
                if (slot >= 0) {
                    Writer ps; ps.u8(uint8_t(slot)); ps.u8(2 /*loaded*/); ps.u32(0);
                    broadcastRoom(*r, Msg::PlayerStatus, ps);
                }
            }
            break;
        }
        case Msg::LeaveGame: leaveRoom(c, "left"); break;
        case Msg::Chat: {
            Reader rd(f.payload.data(), f.payload.size());
            std::string text = rd.str();
            const uint8_t recipients = rd.u8();
            if (!rd.ok || text.size() > 512) return;
            Writer w; w.str(c.name); w.str(text);
            if (!r->running || recipients == 0xff) broadcastRoom(*r, Msg::Chat, w);
            else for (int slot=0;slot<kMaxSlots;++slot) {
                if (!(recipients & (1u<<slot))) continue;
                auto found=clients_.find(uint32_t(r->slotClient[slot]));
                if (found!=clients_.end()) found->second->conn.send(Msg::Chat,w.b);
            }
            break;
        }
        case Msg::PlayerCommands: {
            if (!r->running) return;
            if (c.slot < 0) { c.conn.fail("spectators cannot issue commands"); return; }
            if (!r->campaignBattleId.empty() && (r->campaignResult || r->campaignFault)) return;
            Reader header(f.payload.data(),f.payload.size());
            const auto count=header.u32();
            if(!header.ok || count>f.payload.size()/35) {c.conn.fail("invalid player command count");return;}
            const auto now=nowMs();
            const auto key=c.account.empty()?"ip:"+c.peer:"user:"+tak::auth::foldUsername(c.account);
            const bool allowed=header.ok &&
                c.commandBytes.take(now,4u<<20,unsigned(f.payload.size())) &&
                commandByteKeys_.take(key,now,4u<<20,unsigned(f.payload.size())) &&
                commandBytes_.take(now,32u<<20,unsigned(f.payload.size())) &&
                c.commandCount.take(now,65536,count) &&
                commandCountKeys_.take(key,now,65536,count) &&
                commandCount_.take(now,524288,count);
            if(!allowed || count>kCmdQueueMax-c.cmdQueue.size()) {
                if(!allowed) {c.commandBytes.window=now;c.commandBytes.used=4u<<20;}
                c.cmdDropped+=count;
                if(now-c.commandLogAt>=5000 && commandLogs_.take(now,4)) {
                    c.commandLogAt=now;
                    std::fprintf(stderr,"client %u: command budget/queue exceeded; dropped %llu commands total\n",c.id,(unsigned long long)c.cmdDropped);
                }
                break;
            }
            std::vector<Command> parsed;
            if (!tak::srv::parsePlayerCommands(f.payload,parsed)) {
                if (!r->campaignBattleId.empty())
                    r->campaignFault=tak::srv::crusades::ResultOutcome::InvalidClient;
                c.conn.fail("invalid player commands");
                return;
            }
            for(auto& cmd:parsed) {
                cmd.player=uint8_t(c.slot);
                c.cmdQueue.push_back(cmd);
            }
            break;
        }
        case Msg::StateHash: {
            if (!r->running) return;
            Reader rd(f.payload.data(), f.payload.size());
            uint32_t tk = rd.u32(); uint64_t h = rd.u64();
            if(!r->campaignBattleId.empty() && (!rd.ok || rd.p!=rd.end)) {
                r->campaignFault=tak::srv::crusades::ResultOutcome::InvalidClient;return;
            }
            if (!rd.ok) return;
            if(!r->campaignBattleId.empty() && r->refHash.count(tk) && h!=r->refHash.at(tk))
                r->campaignFault=tak::srv::crusades::ResultOutcome::Desync;
            // A report is only meaningful for a tick the server has actually CLOSED.
            // Without this check `tk` is whatever the peer says: a future tick both
            // advanced ackTick (so the client claimed flow-control progress it had not
            // made) and allocated a r.hashes entry that consensus can never retire,
            // because checkHashes() bails out while waiting for a second client that
            // will never report a tick the server never emitted. Thousands of distinct
            // fabricated ticks therefore grew the room forever.
            // r->tick is the NEXT tick to close, so the newest tick actually emitted is
            // r->tick - 1 and ">=" is the right test. (It also reads correctly at
            // r->tick == 0, where nothing has been emitted and every report is bogus.)
            if (tk >= r->tick) break;   // not emitted yet -- ignore, do not ack
            if (tk > c.ackTick) c.ackTick = tk;   // flow control: this client is up to `tk`
            // SEATED clients only. A spectator's StateHash is a progress ACK and
            // nothing more -- it deliberately sends a trivial 0 rather than
            // folding thousands of units into an FNV on the render thread every
            // 0.4s (see gameview_net.cpp, which states outright that the server
            // never desync-checks it). We were checking it anyway: the 0 landed
            // in the consensus map, disagreed with the referee's canonical hash
            // and dropped the spectator. Its first heartbeat carries ackTick 0,
            // so every spectator died at tick 0 -- spectating was broken outright.
            //
            // Ignoring it is also the correct trust boundary: a spectator is
            // non-authoritative by design (it holds nobody up in canAdvance), so
            // it must not be able to vote on what the canonical state is either.
            if (c.slot >= 0) {
                r->hashes[tk][c.id] = h;
                checkHashes(*r, tk);
                // Retention bound that does NOT depend on consensus. checkHashes only
                // trims once every live client has reported a tick, so one client
                // reporting while another lags (or stops reporting entirely, without
                // yet being dropped) leaves entries pinned indefinitely. Clients report
                // every kHashPeriod ticks, so this window is generous in real time while
                // still being finite. Oldest first -- the map is ordered by tick.
                constexpr size_t kMaxHashTicks = 256;   // ~8500 ticks of slack at kHashPeriod
                while (r->hashes.size() > kMaxHashTicks)
                    r->hashes.erase(r->hashes.begin());
            }
            break;
        }
        default: break;
    }
}

void Server::checkHashes(Room& r, uint32_t tick) {
    auto it = r.hashes.find(tick);
    if (it == r.hashes.end()) return;
    // Wait until every live human client in the room has reported this tick.
    int live = 0;
    for (int i = 0; i < kMaxSlots; ++i)
        if (r.slots[i].type == 1 && r.slotClient[i] >= 0) ++live;
    if (int(it->second.size()) < live || live == 0) return;

    // The canonical hash is the REFEREE's. A game only runs if a referee was built
    // (tryStart refuses otherwise), so there is always one; the ring just may not
    // hold this tick yet, in which case wait rather than judging anybody.
    //
    // The client-majority branch that used to sit here belonged to the relay-only
    // mode and is gone with it. It was never a good judge anyway: with two clients
    // it had no majority to find, and with a modded pair against one honest client
    // it would convict the honest one.
    if (!r.refHash.count(tick)) return;
    const uint64_t canon = r.refHash[tick];
    // Referee suspicion needs a client CONSENSUS to appeal against the server
    // sim: with >=2 clients all agreeing with each other but NOT the referee,
    // the server sim is the odd one out (a server bug) -- don't punish the
    // clients. With a single client there is no consensus, so the referee is
    // authoritative and a lone disagreeing client is simply desynced.
    if (live >= 2) {
        std::map<uint64_t, int> ctally;
        for (auto& [cid, h] : it->second) ++ctally[h];
        if (ctally.size() == 1 && it->second.begin()->second != canon && !r.refSuspect) {
            r.refSuspect = true;
            if(!r.campaignBattleId.empty())r.campaignFault=tak::srv::crusades::ResultOutcome::RefereeFailure;
            std::fprintf(stderr, "game %u: REFEREE SUSPECT at tick %u -- all %d clients agree "
                                 "with each other but disagree with the server sim; not dropping.\n",
                         r.id, tick, live);
        }
    }
    if (r.refSuspect) { r.hashes.erase(r.hashes.begin(), std::next(it)); return; }
    for (auto& [cid, h] : it->second) {
        if (h != canon && !r.desyncFlagged[cid]) {
            r.desyncFlagged[cid] = true;
            if(!r.campaignBattleId.empty())r.campaignFault=tak::srv::crusades::ResultOutcome::Desync;
            auto ci = clients_.find(cid);
            if (ci != clients_.end()) {
                Writer w; w.u32(tick); w.str("desync detected (state diverged from the game)");
                ci->second->conn.send(Msg::Desynced, w);
                std::fprintf(stderr, "game %u: client %u DESYNCED at tick %u\n", r.id, cid, tick);
            }
        }
    }
    r.hashes.erase(r.hashes.begin(), std::next(it));   // drop this and older
}

bool Server::canAdvance(const Room& r) const {
    if(r.campaignResult || r.campaignFault)return false;
    // Slow to the slowest CONSUMER so nobody is flooded past what they can process:
    // the server may not run more than kMaxLeadTicks past the least-advanced acked
    // tick. Seated humans are the primary constraint (AIs are server-run and always
    // current). When a game has NO seated humans -- an all-AI game watched by
    // spectators -- pace to the slowest spectator instead, otherwise the server
    // outruns a spectator that can't sustain the speed and floods it until the
    // connection breaks. (A spectator NEVER holds up a real match: if any human is
    // seated, spectators lag on their own.)
    uint32_t slowest = r.tick;
    bool anyHuman = false;
    for (int i = 0; i < kMaxSlots; ++i) {
        if (r.slots[i].type != 1 || r.slotClient[i] < 0) continue;
        // An eliminated client must never pace the surviving players.
        if (r.defeated[size_t(i)]) continue;
        auto it = clients_.find(uint32_t(r.slotClient[i]));
        if (it == clients_.end() || !it->second->loaded) continue;
        anyHuman = true;
        slowest = std::min(slowest, it->second->ackTick);
    }
    if (anyHuman) return r.tick <= slowest + kMaxLeadTicks;
    bool anySpec = false;
    for (uint32_t sid : r.spectators) {
        auto it = clients_.find(sid);
        if (it == clients_.end()) continue;
        anySpec = true;
        slowest = std::min(slowest, it->second->ackTick);
    }
    if (anySpec) return r.tick <= slowest + kMaxLeadTicks;
    return true;   // no live consumers at all
}

void Server::closeTick(Room& r, bool multipleGames) {
    if(r.tickJob.valid())return;
    // All humans must have loaded before the first tick.
    if (r.tick == 0) {
        for (int i = 0; i < kMaxSlots; ++i)
            if (r.slots[i].type == 1 && r.slotClient[i] >= 0) {
                auto it = clients_.find(uint32_t(r.slotClient[i]));
                if (it == clients_.end() || !it->second->loaded) { r.nextTickMs = nowMs() + 100; return; }
            }
    }
    // The ONE place a client's commands enter a tick: strict FIFO, one budget per
    // client per tick, and the same input-delay rule for all of them.
    //
    // Server-side input delay (TAK_SRV_DELAY=K, default 0) buckets commands K ticks
    // into the future so one never "just misses" a tick boundary; costs K ticks of
    // latency.
    static const int srvDelay = [] {
        const char* e = std::getenv("TAK_SRV_DELAY"); return e ? std::max(0, std::atoi(e)) : 0;
    }();
    for (int i = 0; i < kMaxSlots; ++i) {
        if (r.slotClient[i] < 0) continue;
        auto ci = clients_.find(uint32_t(r.slotClient[i]));
        if (ci == clients_.end()) continue;
        Client& c = *ci->second;
        for (int taken = 0; taken < kCmdCapPerTick && !c.cmdQueue.empty(); ++taken) {
            if (srvDelay > 0) r.pendingAt[r.tick + uint32_t(srvDelay)].push_back(c.cmdQueue.front());
            else r.pending.push_back(c.cmdQueue.front());
            c.cmdQueue.pop_front();
        }
    }
    tak::srv::TickInput input;
    input.tick=r.tick;input.roomId=r.id;input.multipleGames=multipleGames;
    input.commands=std::exchange(r.pending,{});input.events=std::exchange(r.pendingEvents,{});
    input.replayBudget=limits_.replayMemory-std::min<uint64_t>(limits_.replayMemory,r.logBytes);
    if (auto it=r.pendingAt.find(r.tick);it!=r.pendingAt.end()) {
        input.scheduled=std::move(it->second);r.pendingAt.erase(it);
    }
    if(!r.worker)r.worker=std::make_unique<tak::srv::RoomWorker>();
    // Frozen inputs become committed at submission. New commands/events stay on
    // main for the following tick, including leaves or reconnects during this job.
    r.tickJob=r.worker->submit([ref=r.ref.get(),ai=&r.ai,reg=r.reg,input=std::move(input)]() mutable {
        return tak::srv::simulateRoomTick(*ref,*reg,*ai,std::move(input));
    });
}

void Server::finishTick(Room& r) {
    if(!r.tickJob.valid() || r.tickJob.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
    TickResult result;
    try {result=r.tickJob.get();}
    catch(const std::exception& e) {r.simulationError=e.what();}
    catch(...) {r.simulationError="unknown simulation failure";}
    if(!r.simulationError.empty() || result.resourceLimited) {
        r.resourceLimited=result.resourceLimited;
        if(!r.campaignBattleId.empty())r.campaignFault=tak::srv::crusades::ResultOutcome::RefereeFailure;
        if(!r.simulationError.empty())std::fprintf(stderr,"game %u simulation failed: %s\n",r.id,r.simulationError.c_str());
        return;
    }
    // Publish one completed tick atomically from the network thread. A reconnect
    // sees either the old replay boundary or this complete bundle, never half a tick.
    Writer w;w.b=std::move(result.bundle);
    broadcastRoom(r,Msg::TickBundle,w);
    r.logBytes+=w.b.size()+2*sizeof(std::vector<uint8_t>);
    r.log.push_back(std::move(w.b));
    r.defeated=result.defeated;
    if(result.hash) {
        r.refHash[r.tick]=*result.hash;
        if(!replayDir_.empty() && (r.tick/uint32_t(kHashPeriod))%10==0)
            r.replayChecks.emplace_back(r.tick,*result.hash);
        while(r.refHash.size()>300)r.refHash.erase(r.refHash.begin());
    }
    if(!r.missionOutcomeSent && result.missionOutcome) {
        r.missionOutcomeSent=int8_t(result.missionOutcome);
        Writer mw;mw.u8(uint8_t(r.missionOutcomeSent));broadcastRoom(r,Msg::MissionOutcome,mw);
        std::fprintf(stderr,"game %u mission %s: %s\n",r.id,r.mission.c_str(),result.missionOutcome>0?"VICTORY":"DEFEAT");
    }
    ++r.tick;
    r.nextTickMs+=uint64_t(10000/(kServerHz*std::max<int>(1,int(r.opts.speed))));
}

void Server::dropClient(uint32_t id, const char* reason) {
    auto it = clients_.find(id);
    if (it == clients_.end()) return;
    Client& c = *it->second;
    Room* r = c.roomId ? roomOf(c) : nullptr;
    // Defeated players normally disconnect from the result screen. Their
    // departure must not pause the surviving players for reconnect grace.
    const bool defeated = r && c.slot >= 0 && c.slot < kMaxSlots && r->defeated[size_t(c.slot)];
    if (r && r->running && c.slot >= 0 && c.slot < kMaxSlots &&
        r->slots[c.slot].type == 1 && !defeated) {
        // A disconnect from a running game HOLDS the slot: the player may rejoin
        // with their resume token within the grace window. Auto-pause (budget
        // permitting) so nobody is fighting a frozen empire meanwhile.
        int s = c.slot;
        if(!r->campaignBattleId.empty() && s<2)r->campaignDisconnected[s]=true;
        dropPendingCommands(c, *r);   // the slot is HELD; its in-flight orders are not
        r->slotDropped[s] = true;
        r->slotClient[s] = -1;
        r->graceDeadline[s] = nowMs() + kGraceMs;
        if (r->pauseBudgetMs[s] > 0 && !r->paused) {
            r->paused = true; r->pausePlayer = s; r->pauseStartMs = nowMs();
            Writer pw; pw.u8(1 /*drop-grace*/); pw.u8(uint8_t(s));
            broadcastRoom(*r, Msg::Pause, pw);
        }
        Writer w; w.u8(uint8_t(s)); w.u8(1 /*dropped*/); w.u32(0);
        broadcastRoom(*r, Msg::PlayerStatus, w);
        std::fprintf(stderr, "game %u: client %u dropped -- slot %d held (grace %llus)%s\n",
                     r->id, id, s, (unsigned long long)(kGraceMs / 1000),
                     r->paused ? ", paused" : "");
    } else if (r) {
        leaveRoom(c, reason);
    }
    std::fprintf(stderr, "client %u dropped (%s)\n", id, reason);
    if(!c.account.empty() && campaignMatchQueue_.erase(tak::auth::foldUsername(c.account)))++campaignMatchGeneration_;
    clients_.erase(it);
}

bool Server::allowCampaignWork(Client& c,const Frame& f) {
    const bool replay=f.kind==Msg::CrusadesGetReplayChunk;
    auto& local=replay?c.campaignReplayWork:c.campaignWork;
    auto& keys=replay?campaignReplayKeys_:campaignWorkKeys_;
    auto& global=replay?campaignGlobalReplay_:campaignGlobalWork_;
    const auto now=nowMs();
    // A catalog page loads up to 64 full definitions/states. Charge its
    // requested page size so maximum pages cannot amplify a cheap query budget.
    unsigned cost=1;
    if(f.kind==Msg::CrusadesListCampaigns) {
        cost=8;
        if(f.payload.size()>=10) {
            const size_t afterSize=size_t(f.payload[6])|(size_t(f.payload[7])<<8);
            if(afterSize<=128 && f.payload.size()==10+afterSize) {
                const auto offset=8+afterSize;
                const unsigned limit=unsigned(f.payload[offset])|(unsigned(f.payload[offset+1])<<8);
                cost=std::max(1u,(std::min(limit,64u)+7)/8);
            }
        }
    }
    if(local.take(now,64,cost) &&
        (c.account.empty() || keys.take("account:"+tak::auth::foldUsername(c.account),now,64,cost)) &&
        keys.take("ip:"+c.peer,now,256,cost) && global.take(now,1024,cost))return true;
    if(now-c.campaignLimitedWindow>=1000) {c.campaignLimitedWindow=now;c.campaignLimitNotified=false;}
    if(c.campaignLimitNotified)return false;
    c.campaignLimitNotified=true;
    const char* reason="campaign request rate exceeded; retry later";
    if(f.kind==Msg::CrusadesIssueBattle) {
        c.conn.send(Msg::CrusadesBattleResult,battleReply(4,"","",0,"",0,reason));
    } else if(f.kind==Msg::CrusadesGetAllegiance || f.kind==Msg::CrusadesSetAllegiance) {
        Writer reply;reply.u8(f.kind==Msg::CrusadesSetAllegiance?1:0);reply.u8(4);reply.str("");
        reply.u8(0);reply.u64(UINT64_MAX);reply.u64(0);reply.u64(0);reply.str(reason);
        c.conn.send(Msg::CrusadesAllegianceResult,reply);
    } else {
        uint32_t requestId=0;
        if(f.payload.size()>=6)for(unsigned i=0;i<4;++i)requestId|=uint32_t(f.payload[2+i])<<(8*i);
        const auto reply=tak::srv::crusades::campaignReadError(requestId,tak::net::crusades::ErrorCode::Unavailable);
        c.conn.send(reply.kind,reply.payload);
    }
    return false;
}

// Ordinary lobby work needs a wall-clock budget too; a per-poll frame cap
// alone lets a peer flood indefinitely by sending just below that cap.
bool Server::allowLobbyWork(Client& c,const Frame& f) {
    unsigned cost=1;
    switch(f.kind) {
        case Msg::OverrideChunk: case Msg::MapChunk:return true; // upload reads are paced before recv(), not discarded
        case Msg::CreateGame: case Msg::StartGame: case Msg::OverrideOffer: case Msg::MapOffer: cost=16;break;
        case Msg::ListGames: case Msg::JoinGame: case Msg::Spectate:
        case Msg::OverrideRequest: case Msg::OverrideReady: case Msg::OverrideError:
        case Msg::Chat: case Msg::MapRequest: case Msg::MapReady: case Msg::MapError:
        case Msg::Ping: case Msg::Pong: case Msg::SlotUpdate: case Msg::SetGameOptions:
        case Msg::LeaveGame: case Msg::Rejoin: case Msg::Kick: case Msg::SetPause: break;
        default:return true; // Login, campaign and command queues have their own budgets.
    }
    auto& keys=lobbyWorkKeys_;
    auto& global=lobbyGlobalWork_;
    const unsigned personal=128,shared=2048;
    const auto now=nowMs();
    auto& local=c.lobbyWork;
    if(!local.take(now,personal,cost)) {c.conn.fail("request rate exceeded");return false;}
    // A hostile peer behind a NAT must not disconnect healthy neighbours.
    // Keepalive has only the per-connection budget; shared exhaustion drops
    // expensive requests without revoking anybody else's session.
    if(f.kind==Msg::Ping || f.kind==Msg::Pong)return true;
    return keys.take("ip:"+c.peer,now,personal*4,cost) &&
       (c.account.empty() || keys.take("user:"+tak::auth::foldUsername(c.account),now,personal*2,cost)) &&
       global.take(now,shared,cost);
}

size_t Server::mapMemory() const {
    size_t used=0;
    for(const auto& job:validations_)used+=size_t(job.size)*2;
    for(const auto& [id,peer]:clients_)used+=(size_t(peer->mapReceive.size)+peer->overrideReceive.size)*2;
    for(const auto& [id,room]:rooms_)if(room.mapPackage)used+=room.mapPackage->bytes.size()*2;
    for(const auto& [id,room]:rooms_)if(room.overridePackage)used+=room.overridePackage->bytes.size()*2;
    return used;
}

void Server::onFrame(Client& c, const Frame& f) {
    c.lastRecvMs = nowMs();
    if(!allowLobbyWork(c,f))return;
    if (f.kind == Msg::Ping) { c.conn.send(Msg::Pong); return; }
    if (f.kind == Msg::Pong) return;
    if (f.kind == Msg::Bye) { c.conn.fail("bye"); return; }
    switch(f.kind) {
        case Msg::CrusadesListCampaigns: case Msg::CrusadesGetSnapshot:
        case Msg::CrusadesGetPlayerStatus: case Msg::CrusadesGetBattleStatus:
        case Msg::CrusadesGetTerritoryHistory: case Msg::CrusadesGetReplayChunk:
        case Msg::CrusadesGetMatchmaking: case Msg::CrusadesSearchBattle:
        case Msg::CrusadesCancelSearch: case Msg::CrusadesIssueBattle:
        case Msg::CrusadesGetAllegiance: case Msg::CrusadesSetAllegiance:
            if(!allowCampaignWork(c,f))return;
            break;
        default: break;
    }
    if(auto* room=roomOf(c); room && !room->campaignBattleId.empty() && room->running &&
        (f.kind==Msg::CrusadesBattleResult || f.kind==Msg::GameStarting || f.kind==Msg::TickBundle || f.kind==Msg::MissionOutcome ||
         f.kind==Msg::CrusadesCampaignList || f.kind==Msg::CrusadesCampaignSnapshot || f.kind==Msg::CrusadesPlayerStatus ||
         f.kind==Msg::CrusadesBattleStatus || f.kind==Msg::CrusadesMatchmakingStatus || f.kind==Msg::CrusadesTerritoryHistory ||
         f.kind==Msg::CrusadesReplayChunk || f.kind==Msg::CrusadesError)) {
        room->campaignFault=tak::srv::crusades::ResultOutcome::InvalidClient;return;
    }
    if(f.kind==Msg::CrusadesListCampaigns || f.kind==Msg::CrusadesGetSnapshot ||
        f.kind==Msg::CrusadesGetPlayerStatus || f.kind==Msg::CrusadesGetBattleStatus || f.kind==Msg::CrusadesGetTerritoryHistory) {
        campaignRead(c,f);return;
    }
    if(f.kind==Msg::CrusadesGetReplayChunk){campaignReplayChunk(c,f);return;}
    if(f.kind==Msg::CrusadesGetMatchmaking || f.kind==Msg::CrusadesSearchBattle || f.kind==Msg::CrusadesCancelSearch) {
        campaignMatchmaking(c,f);return;
    }
    if (f.kind == Msg::CrusadesIssueBattle) { issueCampaignBattle(c,f); return; }
    if (f.kind == Msg::CrusadesGetAllegiance || f.kind == Msg::CrusadesSetAllegiance) {
        // Only successful SCRAM proof/registration populates account. Hello's
        // display name, a pending AuthBegin, and --no-auth never grant access.
        const bool authenticated = requireAuth_ &&
            (c.state == Client::Lobby || c.state == Client::InGame) && !c.account.empty();
        const std::string account = authenticated ? tak::auth::foldUsername(c.account) : "";
        const auto unixTime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto reply=tak::srv::crusades::handleAllegiance(crusades_.get(),account,f.kind,f.payload,unixTime);
        c.conn.send(Msg::CrusadesAllegianceResult,reply);
        if(f.kind==Msg::CrusadesSetAllegiance && reply.b.size()>1 && reply.b[1]==0) {
            campaignMatchQueue_.erase(account);++campaignMatchGeneration_;
            Reader request(f.payload.data(),f.payload.size());const auto campaign=request.str();
            for(auto& [id,peer]:clients_) {
                (void)id;if(!peer->account.empty() && tak::auth::foldUsername(peer->account)==account)
                    notifyCampaignPlayer(*peer,campaign);
            }
        }
        return;
    }
    if(c.state==Client::InGame && f.kind>=Msg::OverrideOffer && f.kind<=Msg::OverrideError){overrideMsg(c,f);return;}
    if (c.state == Client::InGame && (f.kind == Msg::MapOffer || f.kind == Msg::MapRequest ||
        f.kind == Msg::MapChunk || f.kind == Msg::MapReady || f.kind == Msg::MapError)) {
        mapMsg(c, f); return;
    }
    switch (c.state) {
        case Client::Handshake: handshake(c, f); break;
        case Client::Auth: authMsg(c, f); break;
        case Client::Lobby: lobbyMsg(c, f); break;
        case Client::InGame: gameMsg(c, f); break;
    }
}

int Server::run() {
    std::string err;
    listenFd_ = listenOn(port_, err, loopbackOnly_);
    if (listenFd_ < 0) { std::fprintf(stderr, "takserver: %s on port %u\n", err.c_str(), port_); return 1; }
    std::fprintf(stderr, "takserver %s listening on %s port %u (protocol v%u)\n",
                 tak::kVersion, loopbackOnly_ ? "loopback" : "all interfaces", port_, kNetVersion);
    // Build id on its own line: the harness greps it to refuse a server built from
    // different source than the client, which otherwise looks exactly like a desync.
    std::fprintf(stderr, "takserver: build %s\n", tak::kBuildId);
    std::fprintf(stderr,"takserver: transport %s\n",acme_?"TLS (ACME; connections wait for certificate)":tlsContext_?"TLS (plaintext rejected)":"UNENCRYPTED TCP");
    if (requireAuth_)
        std::fprintf(stderr, "takserver: accounts required -- %zu in %s\n",
                     accounts_.size(), accounts_.path().c_str());
    else
        std::fprintf(stderr, "takserver: NO ACCOUNTS REQUIRED (--no-auth)%s\n",
                     loopbackOnly_ ? "" : " -- anyone who can reach this port can play");

    // Hoisted out of the loop so their capacity persists across wakeups (this loop
    // runs at least at tick rate; rebuilding the contents is cheap, reallocating
    // them thousands of times a second is not).
    std::vector<pollfd> pfds;
    std::vector<uint32_t> ids;
    for (;;) {
        finishValidations();
        for(auto& [id,room]:rooms_) {(void)id;finishTick(room);}
        // Build the pollfd set: listen + every client (POLLOUT when it has pending writes).
        pfds.clear();
        ids.clear();
        // Field-wise (not brace) init: a socket fd is `int` here but `SOCKET`
        // (unsigned) in a Windows pollfd, which brace-init would reject as narrowing.
        pollfd lp{}; lp.fd = listenFd_; lp.events = POLLIN; pfds.push_back(lp);
        ids.push_back(0);
        for (auto& [id, c] : clients_) {
            short ev = canRead(*c,nowMs()) ? POLLIN : 0;
            if (c->conn.wantWrite()) ev |= POLLOUT;
            pollfd cp{}; cp.fd = c->conn.fd(); cp.events = ev; pfds.push_back(cp);
            ids.push_back(id);
        }
        // Timeout = time until the soonest running room's next tick (or 1s idle).
        uint64_t now = nowMs();
        uint64_t soonest = now + (validations_.empty()?1000:10);
        // Only rooms ELIGIBLE to tick may pull the poll deadline in. A paused room
        // is skipped by the tick loop below but its nextTickMs still slides into
        // the past, so counting it here pinned the timeout at 0 and span the
        // server at full CPU for as long as the pause lasted -- and a manual
        // pause has no expiry, so that is indefinite.
        for (auto& [rid, r] : rooms_) {
            (void)rid;
            if(r.tickJob.valid()) {soonest=std::min(soonest,now+2);continue;}
            if(r.campaignResult || r.campaignFault) {
                if(r.campaignResult && !r.campaignResultRecorded && r.campaignResultDue>now)
                    soonest=std::min(soonest,r.campaignResultDue);
                continue;
            }
            if (r.running && !r.paused && r.nextTickMs < soonest) soonest = r.nextTickMs;
        }
        // A client still streaming catch-up needs servicing regardless of the
        // tick clock -- otherwise resuming into a PAUSED game would feed it one
        // chunk per idle second, since a paused room no longer pulls the
        // deadline in.
        for (const auto& [id, c] : clients_) {
            if (c->conn.bufferedInput() && canRead(*c,now))soonest=now;
            if ((c->replaying || c->mapSend.package || c->mapReceive.size || c->overrideSend.package || c->overrideReceive.size) && now + 10 < soonest) soonest = now + 10;
        }
        int timeout = int(soonest > now ? soonest - now : 0);

        int n = TAK_POLL(pfds.data(), (unsigned)pfds.size(), timeout);
        if (n < 0) { if (sockInterrupted(sockErr())) continue; break; }

        for(auto& [id,room]:rooms_) {(void)id;finishTick(room);}
        throttle_.expire(now);   // forget hosts that have long since behaved

        // Accept new connections.
        if (pfds[0].revents & POLLIN) {
            for (unsigned accepted=0;accepted<32;++accepted) {
                int fd = int(accept(listenFd_, nullptr, nullptr));
                if (fd < 0) break;
                const auto address=peerAddress(fd);
                if(!acceptKeys_.take(address,now,32) || !acceptWork_.take(now,128)) {sockClose(fd);continue;}
                size_t pending=0,fromAddress=0;
                for(const auto& [id,peer]:clients_) {
                    (void)id;
                    pending+=peer->state==Client::Handshake || peer->state==Client::Auth;
                    fromAddress+=peer->peer==address;
                }
                if(clients_.size()>=kMaxClients || pending>=kMaxPendingLogins || fromAddress>=kMaxClientsPerAddress) {
                    sockClose(fd);continue;
                }
                // Never silently accept plaintext while initial ACME issuance waits.
                if(acme_ && !acme_->context()) {sockClose(fd);continue;}
                setupSocket(fd);
                auto c = std::make_unique<Client>();
                c->id = nextClientId_++;
                c->conn = Conn(fd,acme_?acme_->context():tlsContext_);
                c->peer = address;
                c->connectedMs = c->lastRecvMs = nowMs();
                clients_[c->id] = std::move(c);
            }
        }
        // Service clients.
        std::vector<uint32_t> dead;
        for (size_t i = 1; i < pfds.size(); ++i) {
            uint32_t id = ids[i];
            auto it = clients_.find(id);
            if (it == clients_.end()) continue;
            Client& c = *it->second;
            if(!canRead(c,now) && (pfds[i].revents & (POLLHUP|POLLERR))) {dead.push_back(id);continue;}
            if (((pfds[i].revents & (POLLIN | POLLHUP | POLLERR)) || c.conn.bufferedInput()) && canRead(c,now)) {
                const bool uploading=c.mapReceive.size!=0 || c.overrideReceive.size!=0;
                if(uploading) {c.uploadWork.take(now,4096,64);uploadGlobalWork_.take(now,16384,64);}
                if (!c.conn.recv(uploading?65536:1u<<20)) { dead.push_back(id); continue; }
                Frame fr;
                unsigned frames=0;
                while (c.conn.poll(fr)) {
                    if(++frames>1024) {c.conn.fail("message flood");break;}
                    try {onFrame(c,fr);}
                    catch(const std::exception& e) {
                        std::fprintf(stderr,"client request failed: %s\n",e.what());
                        c.conn.fail("invalid client request");
                    }
                    if (!c.conn.ok() || !canRead(c,nowMs())) break;
                }
                // A clean close is only final once its trailing frames are drained
                // above -- the last LeaveGame usually arrives in the same segment.
                if (c.conn.peerClosed()) c.conn.fail("peer closed");
            }
            if (!c.conn.ok()) { dead.push_back(id); continue; }
            if (pfds[i].revents & POLLOUT) c.conn.flushWrite();
        }
        if(crusades_ && campaignSweepTime_!=campaignNow()) {
            campaignSweepTime_=campaignNow();
            try {crusades_->expireBattles(campaignSweepTime_);}catch(const std::exception& e) {
                std::fprintf(stderr,"campaign expiration: %s\n",e.what());
            }
            std::vector<uint32_t> expired;
            for(const auto& [rid,room]:rooms_)if(!room.campaignBattleId.empty() && !room.running && campaignSweepTime_>=room.campaignExpires)
                expired.push_back(rid);
            for(auto rid:expired)closeCampaignLobby(rid,"campaign battle expired");
            pruneCampaignSearches();retryCampaignMatches();
            for(auto& [id,peer]:clients_) {(void)id;refreshCampaignSnapshot(*peer);refreshCampaignMatchmaking(*peer);}
        }
        // Grace / pause-budget expiry: a held slot that isn't reclaimed in time,
        // or a pause that outlasts its budget, forfeits the player. The forfeit is
        // a SEQUENCED event so every sim (and the replay) disposes their units on
        // the same tick.
        now = nowMs();
        for (auto& [rid, r] : rooms_) {
            if (!r.running) continue;
            bool budgetOut = r.paused && r.pausePlayer >= 0 &&
                             now - r.pauseStartMs >= r.pauseBudgetMs[r.pausePlayer];
            for (int i = 0; i < kMaxSlots; ++i) {
                if (!r.slotDropped[i]) continue;
                bool forfeit = now >= r.graceDeadline[i] ||
                               (budgetOut && i == r.pausePlayer);
                if (!forfeit) continue;
                r.slotDropped[i] = false;
                if(!r.campaignBattleId.empty() && i<2)r.campaignTimeout[i]=true;
                r.slots[i].type = 3;   // slot closed; the player is out
                r.pendingEvents.push_back({r.campaignBattleId.empty()?tak::net::Event::Kind::Forfeit:tak::net::Event::Kind::CampaignForfeit, uint8_t(i)});
                std::fprintf(stderr, "game %u: player %d FORFEIT (grace/budget expired)\n", rid, i);
                if (r.paused && r.pausePlayer == i) {
                    r.paused = false; r.pausePlayer = -1; r.nextTickMs = now;
                    Writer rw; rw.u8(0); rw.u8(uint8_t(i));
                    broadcastRoom(r, Msg::Resume, rw);
                }
            }
        }
        // Tear down abandoned running games (everyone left/forfeited): write the
        // replay, then erase.
        std::vector<std::pair<uint32_t, const char*>> doneRooms;
        for (auto& [rid, r] : rooms_) {
            if (!r.running || r.tickJob.valid()) continue;
            if(r.resourceLimited || !r.simulationError.empty()) {
                for(auto& [cid,peer]:clients_)if(peer->roomId==rid) {
                    sendReject(*peer,r.resourceLimited?"game replay memory limit reached":"game simulation failed");peer->conn.fail("game stopped");
                }
                if(r.campaignBattleId.empty()) {doneRooms.push_back({rid,r.resourceLimited?"replay memory limit":"simulation failure"});continue;}
            }
            if(!r.campaignBattleId.empty()) {
                finalizeCampaign(r,!roomOccupied(r) || !roomActive(r));
                if(!r.campaignResultRecorded)continue;
            }
            if (!roomOccupied(r)) doneRooms.push_back({rid, "last player left"});
            else if (!roomActive(r)) doneRooms.push_back({rid, "all players gone"});
        }
        for (auto& [rid, why] : doneRooms) {
            Room& r = rooms_.at(rid);
            writeReplay(r);
            // Detach any lingering spectators before the room vanishes: reset their
            // server-side state to Lobby (their client already shows the game's end
            // from the sim, and can browse/leave on its own).
            for (uint32_t sid : r.spectators) {
                auto it = clients_.find(sid);
                if (it == clients_.end()) continue;
                it->second->state = Client::Lobby;
                it->second->roomId = 0; it->second->slot = -1;
            }
            std::fprintf(stderr, "game %u ended (%s)\n", rid, why);
            rooms_.erase(rid);
        }
        // No global batch join: slow rooms keep their own worker busy while
        // networking and other rooms continue. Only one tick per room is in flight.
        for(auto& [id,room]:rooms_) {(void)id;if(!room.campaignBattleId.empty() && room.running)finalizeCampaign(room);}
        now=nowMs();
        size_t running=0;for(const auto& [id,room]:rooms_) {(void)id;running+=room.running;}
        for(auto& [id,room]:rooms_) {
            (void)id;
            if(!room.running || room.paused || room.tickJob.valid() || room.campaignResult ||
               room.campaignFault || room.resourceLimited || !room.simulationError.empty() ||
               !roomOccupied(room) || !roomActive(room) || room.nextTickMs>now)continue;
            if(!canAdvance(room)) {room.nextTickMs=now+kFlowRetryMs;continue;}
            try {closeTick(room,running>1);}
            catch(const std::exception& e) {
                room.simulationError=e.what();
                if(!room.campaignBattleId.empty())room.campaignFault=tak::srv::crusades::ResultOutcome::RefereeFailure;
                std::fprintf(stderr,"game %u tick submission failed: %s\n",id,e.what());
            }
        }
        // Feed catch-up streams. A client resuming or spectating takes its
        // history from the room log a chunk at a time, only topping up when its
        // write buffer has drained, so a long game cannot put its whole replay
        // into one socket buffer. The log keeps growing with live bundles, so
        // reaching the end IS being caught up -- at which point the live
        // broadcast takes over.
        constexpr size_t kReplayChunkBytes = 256u << 10;
        for (auto& [id, c] : clients_) {
            if (!c->replaying) continue;
            auto rit = rooms_.find(c->roomId);
            if (rit == rooms_.end()) { c->replaying = false; continue; }
            if (rit->second.mapPackage && c->mapReadyRoom != rit->second.id) continue;
            const auto& log = rit->second.log;
            while (c->replayPos < log.size() &&
                   c->conn.txPending() < kReplayChunkBytes)
                c->conn.send(Msg::TickBundle, log[c->replayPos++]);
            if (c->replayPos >= log.size()) c->replaying = false;
        }
        // Flush all pending writes (bundles just queued) + keepalive + timeouts.
        now = nowMs();
        size_t queued=0;
        std::vector<std::pair<size_t,uint32_t>> backlogs;
        for(const auto& [id,c]:clients_) {
            const auto bytes=c->conn.txPending();
            if(bytes>kMaxClientTxBytes) {c->conn.fail("send backlog exceeded");dead.push_back(id);}
            else {queued+=bytes;backlogs.emplace_back(bytes,id);}
        }
        std::sort(backlogs.rbegin(),backlogs.rend());
        for(const auto& [bytes,id]:backlogs) {
            if(queued<=kMaxServerTxBytes)break;
            clients_.at(id)->conn.fail("server send backlog exceeded");dead.push_back(id);queued-=bytes;
        }
        for (auto& [id, c] : clients_) {
            if(!c->conn.ok()) {dead.push_back(id);continue;}
            if(c->mapReceive.size && (now-c->mapReceiveProgress>30000 || now-c->mapReceiveStarted>300000)) {
                c->mapReceive={};c->conn.fail("map upload deadline exceeded");dead.push_back(id);continue;
            }
            if(c->overrideReceive.size && (now-c->overrideProgress>30000 || now-c->overrideStarted>300000)){
                c->overrideReceive={};c->conn.fail("override upload deadline exceeded");dead.push_back(id);continue;
            }
            c->overrideSend.pump(c->conn,Msg::OverrideChunk);
            c->mapSend.pump(c->conn);
            if (!c->conn.flushWrite()) { dead.push_back(id); continue; }
            if (c->state != Client::Handshake && now - c->lastRecvMs > kPingIdleMs &&
                now - c->lastPingMs > kPingIdleMs) {
                c->conn.send(Msg::Ping); c->lastPingMs = now; c->conn.flushWrite();
            }
            // Spectators (in a game, no seat) get a far longer grace than seated players.
            bool spectator = c->state == Client::InGame && c->slot < 0;
            if((c->state==Client::Handshake || c->state==Client::Auth) && now-c->connectedMs>kLoginDeadlineMs)
                dead.push_back(id);
            if (now - c->lastRecvMs > (spectator ? kSpectatorTimeoutMs : kTimeoutMs))
                dead.push_back(id);
        }
        for (uint32_t id : dead) dropClient(id, "disconnected");
    }
    return 0;
}

}  // namespace

static int serverMain(int argc, char** argv) {
    // Test overrides for the reconnect timers (seconds).
    if (const char* g = std::getenv("TAK_GRACE_MS")) kGraceMs = uint64_t(std::atoll(g));
    if (const char* b = std::getenv("TAK_PAUSE_BUDGET_MS")) kPauseBudgetMs = uint64_t(std::atoll(b));
    uint16_t port = 7677;
    std::string dataRoot, replayDir,tlsCert,tlsKey,mapRoot;
    tak::srv::AcmeOptions acme;acme.state="takserver-acme";
    bool acmeOption=false;
    bool allowTestWork=false;
    bool allowPlaintext=false;
    std::string accountsPath = "takserver-accounts.conf";
    std::string crusadesDb, crusadesDefinition;
    uint32_t fixedSeed = 0;
    tak::srv::Limits limits;
    bool noAuth = false, loopbackOnly = false, closedRegistration=false;
    for (int i = 1; i < argc; ++i) {
        try {if(limits.set(argv[i],i+1<argc?argv[i+1]:"")) {++i;continue;}}
        catch(const std::exception& e) {std::fprintf(stderr,"takserver: %s: %s\n",argv[i],e.what());return 1;}
        if (!std::strcmp(argv[i], "--port") && i + 1 < argc) port = uint16_t(std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--data") && i + 1 < argc) dataRoot = argv[++i];
        else if (!std::strcmp(argv[i], "--replaydir") && i + 1 < argc) replayDir = argv[++i];
#ifndef NDEBUG
        else if (!std::strcmp(argv[i], "--allow-benchmarks")) allowTestWork=true;
#endif
        else if (!std::strcmp(argv[i], "--map-cache-dir") && i+1<argc)mapRoot=argv[++i];
        else if (!std::strcmp(argv[i], "--accounts") && i + 1 < argc) accountsPath = argv[++i];
        else if (!std::strcmp(argv[i], "--crusades-db") && i + 1 < argc) crusadesDb = argv[++i];
        else if (!std::strcmp(argv[i], "--crusades-definition") && i + 1 < argc) crusadesDefinition = argv[++i];
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc)
            fixedSeed = uint32_t(std::strtoul(argv[++i], nullptr, 0));
        else if (!std::strcmp(argv[i], "--no-auth")) noAuth = true;
        else if (!std::strcmp(argv[i], "--closed-registration")) closedRegistration=true;
        else if (!std::strcmp(argv[i], "--tls-cert") && i+1<argc) tlsCert=argv[++i];
        else if (!std::strcmp(argv[i], "--tls-key") && i+1<argc) tlsKey=argv[++i];
        else if (!std::strcmp(argv[i], "--acme-domain") && i+1<argc) {acme.domain=argv[++i];acmeOption=true;}
        else if (!std::strcmp(argv[i], "--acme-email") && i+1<argc) {acme.email=argv[++i];acmeOption=true;}
        else if (!std::strcmp(argv[i], "--acme-state") && i+1<argc) {acme.state=std::filesystem::u8path(argv[++i]);acmeOption=true;}
        else if (!std::strcmp(argv[i], "--acme-agree-tos")) {acme.agreeTerms=true;acmeOption=true;}
        else if (!std::strcmp(argv[i], "--acme-staging")) {acme.directory="https://acme-staging-v02.api.letsencrypt.org/directory";acmeOption=true;}
        else if (!std::strcmp(argv[i], "--allow-plaintext")) allowPlaintext=true;
        else if (!std::strcmp(argv[i], "--local")) loopbackOnly = true;
        else if (!std::strcmp(argv[i], "--version") || !std::strcmp(argv[i], "-v")) {
            std::printf("takserver (TAK engine) %s (build %s)\n", tak::kVersion, tak::kBuildId);
            return 0;
        }
        else if (!std::strcmp(argv[i], "--help")) {
            std::printf("usage: takserver --data <retail-install-dir> [--port N]\n"
                        "                 [--replaydir <dir>] [--accounts <file>]\n"
                        "                 [--no-auth] [--local] [--closed-registration]\n"
                        "                 [--tls-cert fullchain.pem --tls-key private.pem]\n"
                        "                 [--acme-domain hostname --acme-agree-tos]\n"
                        "                 [--acme-email address] [--acme-state directory] [--acme-staging]\n"
                        "                 [--allow-plaintext] (trusted LAN/test networks only)\n"
                        "                 [--map-cache-dir <writable directory>]\n"
                        "                 [--max-games N] [--max-running-games N] [--max-accounts N]\n"
                        "                 [--map-memory-mib N] [--map-storage-mib N]\n"
                        "                 [--replay-memory-mib N] [--replay-storage-mib N]\n"
                        "                 [--crusades-db <file>] [--crusades-definition <file>]\n"
                        "  --data is REQUIRED: it is what the referee sim and the\n"
                        "  server-hosted AI players read. There is no relay-only mode.\n"
                        "  --replaydir writes a .takrep replay file per finished game.\n"
                        "  --seed N pins every game's RNG seed, so a headless run is\n"
                        "  repeatable. Games are otherwise seeded randomly (which is what\n"
                        "  makes Random Start Locations differ game to game).\n"
                        "  --accounts is the account file (default takserver-accounts.conf).\n"
                        "  Players sign in with a name and password; an unused name is\n"
                        "  registered on the spot. No password is stored or transmitted --\n"
                        "  see src/net/auth.h.\n"
                        "  --crusades-db enables persistent authenticated campaign allegiance.\n"
                        "  It must be separate from the account file; --no-auth is incompatible.\n"
                        "  --crusades-definition imports a new campaign or verifies the saved definition.\n"
                        "  Allegiance switching currently uses an immediate, free modern policy,\n"
                        "  not historical rank penalties or house restrictions.\n"
                        "  --no-auth serves anyone who connects, with no account at all. Only\n"
                        "  for a private or LAN server; pair it with --local.\n"
                        "  --local binds loopback only, so nothing off this machine connects.\n"
                        "  --closed-registration allows existing accounts only.\n"
                        "  Limits default to 16 games / 4 running, 10000 accounts, 512 MiB\n"
                        "  shared map memory, 4096 MiB map storage, 256 MiB replay memory\n"
                        "  per game and 4096 MiB replay storage. Limits must be positive.\n"
                        "  Benchmark/stress and local campaign missions require --local --no-auth.\n");
            return 0;
        } else {std::fprintf(stderr,"takserver: unknown option or missing value: %s\n",argv[i]);return 1;}
    }
    if(acmeOption && (acme.domain.empty() || !acme.agreeTerms || !tlsCert.empty() || !tlsKey.empty() || allowPlaintext || loopbackOnly || port==80)) {
        std::fprintf(stderr,"takserver: ACME requires --acme-domain and --acme-agree-tos; cannot combine with manual TLS, plaintext, --local, or game port 80.\n");return 1;
    }
    if(tlsCert.empty()!=tlsKey.empty() || (!loopbackOnly && tlsCert.empty() && !allowPlaintext && !acmeOption)) {
        std::fprintf(stderr,"takserver: public listeners require --tls-cert and --tls-key, or --acme-domain and --acme-agree-tos. Use --local for private games, or explicitly --allow-plaintext for a trusted LAN/test network.\n");return 1;
    }
    // --data is mandatory. The server used to run without it as a pure relay, with
    // the clients cross-checking hashes among themselves; that mode is gone. It gave
    // up the canonical referee hash (so a lone client could not be told apart from a
    // desynced one), it could not host AI players at all, and it made a whole second
    // set of paths that no shipped configuration exercised -- single-player launches
    // its private server with --data, and so does every harness.
    if (dataRoot.empty()) {
        std::fprintf(stderr,
            "takserver: --data <retail-install-dir> is required.\n"
            "  It is what the referee sim and the AI players read. Point it at a\n"
            "  TA:Kingdoms install (the folder holding the root *.hpi and Maps/).\n"
            "  Run with --help for the full usage.\n");
        return 1;
    }
    if ((!crusadesDb.empty() && noAuth) || (!crusadesDefinition.empty() && crusadesDb.empty())) {
        std::fprintf(stderr,"takserver: Crusades requires --crusades-db and authenticated accounts; --no-auth is incompatible.\n");
        return 1;
    }
    if (!crusadesDb.empty()) {
        try {
            namespace fs = std::filesystem;
            const auto database = fs::u8path(crusadesDb), accounts = fs::u8path(accountsPath);
            // Check both the database and SQLite's reserved companion files.
            // SQLite may resolve a database symlink before naming its journal,
            // so guard companions of the supplied and resolved paths alike.
            const auto canonical = [](const fs::path& path) {
                return fs::weakly_canonical(fs::absolute(path));
            };
            const auto accountCanonical = canonical(accounts);
            const auto aliasesAccount = [&](const fs::path& path) {
                std::error_code ec;
                if (fs::equivalent(path,accounts,ec)) return true; // Existing hard links.
                const auto resolved = canonical(path);
#ifdef _WIN32
                // path::operator== is case-sensitive even on Windows. Neither
                // file must already exist for these names to be aliases.
                const auto& a = resolved.native();
                const auto& b = accountCanonical.native();
                return CompareStringOrdinal(a.data(),static_cast<int>(a.size()),
                    b.data(),static_cast<int>(b.size()),TRUE) == CSTR_EQUAL;
#elif defined(__APPLE__)
                // Most Mac volumes ignore case and canonical Unicode spelling.
                // Conservatively reserve these aliases on case-sensitive APFS
                // too; this comparison must also work before either file exists.
                const auto& a = resolved.native();
                const auto& b = accountCanonical.native();
                struct CfString {
                    CFStringRef value;
                    ~CfString() { if (value) CFRelease(value); }
                };
                const CfString left{CFStringCreateWithBytes(kCFAllocatorDefault,
                    reinterpret_cast<const UInt8*>(a.data()),static_cast<CFIndex>(a.size()),kCFStringEncodingUTF8,false)};
                const CfString right{CFStringCreateWithBytes(kCFAllocatorDefault,
                    reinterpret_cast<const UInt8*>(b.data()),static_cast<CFIndex>(b.size()),kCFStringEncodingUTF8,false)};
                if (!left.value || !right.value)
                    throw std::runtime_error("cannot compare campaign and account paths");
                return CFStringCompare(left.value,right.value,
                    kCFCompareCaseInsensitive | kCFCompareNonliteral) == kCFCompareEqualTo;
#else
                return resolved == accountCanonical;
#endif
            };
            for (const auto& base : {database,canonical(database)}) {
                for (const char* suffix : {"","-journal","-wal","-shm"}) {
                    auto reserved = base; reserved += suffix;
                    if (aliasesAccount(reserved))
                        throw std::runtime_error("campaign database and its SQLite companion files must be separate from account credentials");
                }
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr,"takserver: %s\n",e.what());return 1;
        }
    }
    Server s(port, dataRoot,mapRoot);
#ifndef NDEBUG
    if(allowTestWork)s.allowBenchmarks();
#else
    (void)allowTestWork;
#endif
    if (!replayDir.empty()) s.setReplayDir(replayDir);
    if (fixedSeed) s.setFixedSeed(fixedSeed);
    s.setLimits(limits);
    if(acmeOption)s.setAcme(std::make_shared<tak::srv::AcmeCertificates>(acme));
    if(!tlsCert.empty())s.setTls(tak::net::TlsContext::server(tlsCert,tlsKey));
    if (loopbackOnly) s.setLoopbackOnly();
    if (closedRegistration) s.closeRegistration();
    if (noAuth) {
        s.setNoAuth();
    } else {
        std::string err;
        if (!s.loadAccounts(accountsPath, err)) {
            // Refuse to start rather than come up looking like a server with
            // accounts while actually having none of them.
            std::fprintf(stderr, "takserver: %s\n", err.c_str());
            return 1;
        }
    }
    if (!crusadesDb.empty()) {
        try {
            s.enableCrusades(std::filesystem::u8path(crusadesDb),std::filesystem::u8path(crusadesDefinition));
        } catch (const std::exception& e) {
            std::fprintf(stderr,"takserver: cannot start Crusades: %s\n",e.what());return 1;
        }
    }
    return s.run();
}

int main(int argc,char** argv) {
    try {
#ifdef _WIN32
        return tak::utf8Main(serverMain);
#else
        return serverMain(argc,argv);
#endif
    } catch(const std::exception& e) {std::fprintf(stderr,"takserver: %s\n",e.what());return 1;}
}
