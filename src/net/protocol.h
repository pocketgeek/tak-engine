#pragma once

// Wire protocol for the client-server multiplayer (docs/multiplayer-design.md).
// A central takserver relays a server-sequenced deterministic lockstep: clients
// send commands, the server assigns them to ticks and broadcasts one TickBundle
// per tick. This header is shared by the server and the client.
//
// Framing (kept from the old 2-peer lockstep): u32 LE length prefix (payload
// size + 1) then a u8 message kind then the payload. All integers little-endian.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "sim/pathmode.h"

#include "net/lockstep.h"   // Command / Cmd (the 35-byte command wire format is reused)

namespace tak::net {

constexpr uint32_t kNetVersion = 244;      // 244: Legion W5 -- attack and guard members braked by combat in reach are Engaged (hashed
                                           //      member state 6): settled and still to local steering, never yielding or parting,
                                           //      soft to other commands only (as is a member held on its ring spot); a reach kind's
                                           //      held same-target peer (30 ticks) is settled to the bodies behind it, and reach kinds
                                           //      never take the settle rule or contact arrival. Reach groups are keyed by target,
                                           //      reach bucket and seeded origin; two or more attackers plan to a ring of spots at
                                           //      weapon reach (band 0 LoS/component-filtered seeds, outer waiting bands, bulk
                                           //      angular claims, re-choice at most 3 times, claims following engaged bodies; hashed
                                           //      when set; a guard plans to its ward without a ring). A moving goal's group re-seeds
                                           //      in place and reseats its members with their clocks; an illegal goal re-claims in its
                                           //      group; a soft block of 20+ cells formed on a group's way after its field re-plans it
                                           //      once (300-tick cooldown)
                                           // 243: Legion W6 -- flyers are paired with the ground of their own click on (order class,
                                           //      convoyTick) for Ctrl+N and Alt+N squads (up to 4 stations a squad) and released
                                           //      for their order when the ground stops (no headway for 450 ticks, or a centroid
                                           //      still for 900; hashed on the order when set); a landing flyer is stamped on its
                                           //      touchdown footprint from the start of its descent (leave-only); the rest-time
                                           //      rejoin gathers ground only, at the formation's modal settled point, never
                                           //      re-ordering a settled body; a lift episode is capped at 1800 ticks (hashed start
                                           //      tick) with a 600-tick rest, kept up only by members making headway, and a lifted
                                           //      flyer polls for a target every 8 ticks; a stalled squad member lifts its own
                                           //      squad's flyer off its goal or next cells; a station flyer without horizontal
                                           //      velocity does not yaw
                                           // 242: Legion W4 -- settled-arrival, parting and approach records are erased when a
                                           //      unit's orders change (World::noteOrders from the order helpers, mission dispatch,
                                           //      target acquisition and the death edge) instead of by a per-tick walk; prune's
                                           //      hashed 256-a-tick backstop cursor also walks those records. A stale field
                                           //      refreshes only for an active group (a Moving member in the last 2 ticks, a
                                           //      blocked member, or the hashed demand flag the aware re-plan sets). scanStill
                                           //      samples bodies by id % 30 each tick and keeps the soft stamps incrementally;
                                           //      softHash is an order-independent sum over stamped cells and owned ids
                                           // 241: Legion W3 -- one convoy per click (Order::convoyTick and the open-convoy table are
                                           //      hashed) and Legion keys on it: one point, formation and settle chain per click,
                                           //      slots handed out 32 a tick (at most 131072 ring cells of slot search) once the
                                           //      convoy closes (a first part under 64 takes its formation at once, rebuilt if another
                                           //      part joins); hold and stall clocks are tick stamps and rest is stride-free; one
                                           //      settle rule: a queued, pressed unit settles where it stands within its capped settle
                                           //      reach (or one row behind its command's settled crowd) after up to 3 re-choices (none
                                           //      while it stands on its own slot; the
                                           //      reach disc and 600-tick wait are gone; at a patrol waypoint the held crowd is the
                                           //      queue); a held formation unit re-aims its lane every 20 held ticks
                                           // 240: Legion W2 -- structures, units under construction and speed-0 units never pace or
                                           //      anchor an Alt+N formation (FormAgg sums Fixed); an unreachable order drops on
                                           //      arrival at its approach point, or when held behind its own stopped army (holder
                                           //      rule); a patrolling builder's repair detour is a Legion Repair leg
                                           // 239: override packages carry whole files (TOV2); area build nearest first; Legion shared goal fields + bounded rebuilds,
                                           //      no walking in place, idle allied landed flyers lift for passing ground units,
                                           //      formation flyers cruise/land promptly, moving groups plan round each other
                                           // 238: Legion clustered static-map repair; held Legion units rest between updates
                                           // 237: grouped flyers: retail VTOL_Move group checks; Legion flyers hold over the formation
                                           // 236: Legion flyers never touch down on a mobile ground unit
                                           // 235: Shift-patrol closes the loop; Legion queued legs share a point; Legion sees landed flyers
                                           // 234: Legion pinwheel round wall ends; Legion flyers never land on flyers
                                           // 233: Legion soft obstacles round standing bodies; formation rejoin only at rest
                                           // 232: Legion idle same-player bodies part a lane for held members
                                           // 231: Retail+, Flowfield and Cooperative pathfinding removed;
                                           //      mode bytes 1-3 are now invalid (Retail 0, Legion 4)
                                           // 230: Legion routes all applicable movement commands (combat/escort,
                                           //      work/logistics, boats, hovercraft) + corner-step/column fix
                                           // 229: Legion lag/settle/follow/lane changes; Retail audit (5x budget
                                           //      class, same-cell submit, group pacing, cargo sight, exact
                                           //      flyer landing + airborne occupancy)
                                           // 228: Legion group pathfinding mode (value 4); shared far-tile flow
                                           //      fields + field reuse; Retail+ occupancy obstruction
                                           // 227: Retail+ route and local-traffic mode (value 3)
                                           // 226: Cooperative follower scheduling, retry and lane behavior
                                           // 224: bounded Flowfield crowd avoidance and legal compact arrivals
                                           // 223: Flowfield group areas, route spacing and formation pacing
                                           // 222: Flowfield arrivals at crowded rally formations
                                           // 220: authoritative Retail/Flowfield match selection
                                           // 219: weapon damage and rubble transitions for death-spawned corpses
                                           // 218: v7 generated mazes with open edges
                                           // 217: real-time emote triggers; buildings excluded
                                           // 216: feature-tail impact damage and selected-unit emotes
                                           // 215: themed v6 generated-map layouts
                                           // 214: named v5 generated maps with open lake edges
                                           // 213: selected override pack transfer and verification
                                           // 212: scripted production pads check open yard occupancy
                                           // 211: completed campaign history and replay transfers
                                           // 210: campaign territory rendezvous matchmaking
                                           // 209: campaign live territory activity
                                           // 208: versioned Crusades snapshots and battle lifecycle
                                           // 207: server-sequenced campaign forfeits
                                           // 206: server-issued Crusades battle rooms
                                           // 205: authenticated Crusades allegiance query/change
                                           // 204: retail damage-category selection and integer damage tables
                                           // 203: generated-map v4 recipes and gentler Easy AI
                                           // 195: directed mana sharing and chat recipients
                                           // 191: infinite mobile builders accept move/patrol rally orders
                                           // 190: structures never enter navigation steering
                                           // 189: authored corpse decisions and death owner lifetimes
                                           // 184: stationary buildings aim without rotating their bodies
                                           // 182: authoritative area reclaim command and target selection
                                           // 181: reject building yards extending into the map border
                                           // 180: water-yard building placement follows retail waterline rules
                                           // 179: nonblocking map features no longer obstruct navigation
                                           // 178: retail transports; Unload targetId carries fixed-point destination Y
                                           // 176: infinite mobile production accepts only Stop
                                           // 175: data-driven hover attack positioning for flyers
                                           // 174: flyer activation, selected-weapon approach range, and attack turning
                                           // 170: bound off-map grade aging and stress-flyer spawns
                                           // 169: AI validates snapped construction sites and builder approaches
                                           // 168: defensive AI, stances, income, conjure hover and birth orientation
                                           // 142: terrain-scaled active steering and braking geometry
                                           // 141: combat coasting and terrain-scaled stopping pivots
                                           // 140: ground braking retains movement and terrain state
                                           // 139: terrain pitch and ground speed limits
                                           // 138: rectangular placement in ground movement commitment
                                           // 137: stored surface height and water transition timing
                                           // 136: corpse and statue navigation footprints
                                           // 135: mutable feature footprints in navigation
                                           // 134: traffic and path costs retain mover terrain flags
                                           // 108: ground responses, corner pruning and player bookkeeping RNG
                                           // 105: navigator admission timing and retry stamp
                                           // 104: simulation-owned wind and RNG consumption
                                           // 103: persistent three-dimensional flight motion
                                           // 97: verified resumable retail search phases in gameplay
                                           // 94: failed routes stop at their traced endpoint
                                           // 93: retail steering/braking, local scans, refusal retries,
                                           //     waypoint advance, terrain and default path budget
                                           // an observed retail route -- corner-cuts
                                           // like retail (no stepLegal in the search)
                                           // 84: retail's phase-2 Dijkstra makes the
                                           // route on tracer arrival (emulated costs)
                                           // 83: the cadence ladder is retail's --
                                           // per-frame dice, flag-keyed, emulated
                                           // 82: retail's movement layer -- per-cell
                                           // search grades and mover probe, best-effort
                                           // routes, randomised repath cadences
                                           // 81: gods are retail's -- a seeded 10%
                                           //     GameChance roll and a random appear
                                           //     time, no favour pool, no lobby option
                                           // 80: the SEARCH refuses to route through a
                                           //     parked body (it is solid to the mover)
                                           // 79: route shortcuts respect parked bodies;
                                           //     spawn claims use the centred footprint
                                           // 78: benchmark/stress fills reserve whole
                                           //     footprints and stop at map capacity
                                           // 77: traced routes are string-pulled from
                                           //     the unit's CURRENT cell before install
                                           // 76: factory output spawns/rallies to FREE
                                           //     spots; buildings hold rally orders
                                           // 75: the SIM summons gods (the client did,
                                           //     the referee never did -> desync)
                                           // 74: per-category DAMAGE is a multiplier
                                           //     on default, not absolute damage
                                           // 73: benchmark/stress spawns snap to terrain
                                           //     the unit can actually occupy
                                           // 72: replacing a unit's orders cancels its
                                           //     pending search; the unload approach
                                           //     must be REACHABLE, not just fit
                                           // 71: an unload approaches the nearest cell
                                           //     the transport FITS, within unload range
                                           // 70: a transport unload routes through the
                                           //     path service instead of an inline A*
                                           // 69: that re-request also adopts the newer
                                           //     exact destination (same cell, new point)
                                           // 68: a path re-request for the same goal from
                                           //     the same cell no longer restarts the search
                                           // 67: GameStarting carries the replay boundary
                                           // 66: cloak defaults OFF (retail CLOAK/DECLOAK are orders)
                                           // 65: reclaim and repair join the one queue;
                                           // reclaimQueue gone, repairs can stack
                                           // 64: ONE order queue -- builds are ordinary
                                           // orders; Unit::buildOrders is gone
                                           // 63: a build queued behind movement rides
                                           // the move queue and blocks it while it builds
                                           // 62: unitstandorders gates the stance
                                           // 61: idle flyers look for a landable spot
                                           // 60: standing orders split into retail's
                                           // move + fire axes, seeded per unit type
                                           // 59: fireatwillrandom scatters auto-acquired
                                           // targets for the 16 missile-troop types
                                           // 58: order queues survive a repath; Patrol,
                                           // Repair and Assist honour their queue flag
                                           // 57: account login (SCRAM-SHA-256) sits
                                           // between the Hello and the Welcome
                                           // 56: canPlace honours the yardmap -- a '.' cell
                                           // is not part of the footprint and is not tested
                                           // 55: the server-side AI no longer shares the sim's
                                           // flow-field cache (it was desyncing the referee);
                                           // flow key is per movement CLASS again
                                           // 54: one nav grid per MOVEMENT CLASS, and one
                                           // shared obstacle overlay (buildings used to block
                                           // the ground grid only -- hover walked through them)
                                           // 53: mobile units get their REAL footprint from
                                           // the movement class (2x2..5x5, and a 7x7 Trebuchet)
                                           // 52: retail terrain passability -- a cell is
                                           // blocked on its OWN quad's height spread, not on
                                           // whether a neighbour rises above it (which severed
                                           // every ramp)
                                           // 51: units are solid -- a parked body blocks a
                                           // move; blocked units clamp-and-slow instead of
                                           // stopping; separation spacing 13 -> 16px
                                           // 50: missed ballistic shells crater where they
                                           // land; target leading + dontleadtargets;
                                           // lobbers ignore the line-of-sight gate;
                                           // all four walls block nav (isStructure)
                                           // 49: cosmetic-lows sim batch -- turninplacerate +
                                           // retail turn/brake coupling, transportdistance,
                                           // [AdjustJoy] repair aura + retail aura falloff,
                                           // guided sub-step speed-up
                                           // 48: Random Start Locations (seeded start-spot shuffle)
                                           // 47: Standby_wander wildlife roam (hashed movement)
                                           // 46: mission VO hook + deadly water (waterdoesdamage)
                                           // 45: Msg::SetPause (player-requested pause) + mission order verbs
                                           // 44: mission GET table + escort/protect win-lose conditions
                                           // 43: storm ids folded into the hash (viewer draws storms)
                                           // 42: weapon classes -- guided homing, Remote Effect,
                                           // wandering storms, mind control, unitsonly
                                           // 41: [EXPLODEAS] death blasts, totalallowed unique cap,
                                           // data-only missions (.cob optional) + InitialMission/ManaPercentage
                                           // 40: fire breath is a Line-of-Sight beam (instant hit, FBI range, no 170 cap)
                                           // 39: a builder mid-job doesn't auto-acquire/wander off
                                           // 38: self-destruct is a 5s toggled countdown (Ctrl+Shift+D)
                                           // 37: don't auto-acquire/keep attacking undamageable targets
                                           // 36: in-sim .crt scenario trigger runner + forced-defeat
                                           // 35: structures exempt from separation/unstick
                                           // 28: retail melee = footprint adjacency
                                           // 26: benchmark level 6 (Extra Absurd)
                                           // 25: GameOptions.benchmark carries an INTENSITY
                                           // level (0=off, 1=Low..N), was a bool
                                           // 24: GameOptions.benchmark (all-AI perf run)
                                           // 20: +control squads/formations (Cmd::SetSquad)
                                           // 15: GameOptions.speed/speedUnlock + Set/SpeedUpdate msgs
                                           // 14: Cmd::Headbang (Shift+H emote)
                                           // 13: under-construction units contribute no income/storage (economy fix)
                                           // 12: builder repair (Cmd::Repair + Unit::repairId)
                                           // 11: unit stance/cloak/active in sim + Cmd::Stance/Cloak/SetActive
                                           // 10: reclaimable features in sim + Cmd::Reclaim (area-clear)
constexpr uint32_t kMaxFrame = 1u << 18;   // 256 KB frame cap (hardening) -- headroom for a
                                           // big tick bundle when many units are ordered at once
constexpr int kMaxSlots = 8;               // players per game (= max map start positions)
constexpr int kServerHz = 30;              // sim/tick rate
constexpr int kHashPeriod = 30;            // ticks between StateHash reports; the
                                           // referee hashes only these ticks too
// Commands one client may land in one tick. The server enforces it as flood
// protection and DISCARDS the excess, so the client has to know it too and spread
// a bigger batch over several ticks -- otherwise ordering a large selection
// silently loses everything past the cap. Shared here so the two cannot drift.
constexpr int kCmdCapPerTick = 64;
// How many commands the server will hold for one client that has run ahead of the
// drain. The client's send credit is capped at exactly this, which is what makes
// overflow unreachable for an honest client: it can never have more outstanding
// than the server can hold, whatever its frame rate or how its uplink batches.
constexpr int kCmdQueueCap = 8 * kCmdCapPerTick;

// Commands a client may send after `ticks` sim ticks have passed, carrying `have`
// unspent credit. Shared so the two sides cannot drift: the server drains
// kCmdCapPerTick per tick, so credit accrues at exactly that rate.
//
// Crediting by ELAPSED TICKS rather than per call is the point. Gating on "once
// per tick" still let the frame rate set the throughput -- one batch per rendered
// frame, so at 10fps a client offered 640 commands/sec against a server willing to
// take 1920, and a 2000-unit order crawled out over three seconds while a Stop
// issued behind it waited its turn.
//
// This is a RATE limit and nothing more. It does NOT bound how much is in flight:
// over a 16-tick uplink stall a client spends and re-accrues, sending 1024 while
// the server's 512 queue sees none of it, and half is dropped when the stall
// clears. Outstanding is bounded separately, by cmdSendWindow below.
inline int cmdSendCredit(int have, uint32_t ticks) {
    const long long c = (long long)have + (long long)ticks * kCmdCapPerTick;
    return int(c > kCmdQueueCap ? kCmdQueueCap : c);
}

// How many commands may be sent right now given `inFlight` already sent but not
// yet seen coming back, and `credit` from the rate limiter above. The window is
// the server's queue, so a client physically cannot overrun it however long its
// uplink stalls.
//
// The acknowledgement is free and already on the wire: this is a lockstep relay,
// so the server broadcasts each tick's bundle to EVERY peer including the sender.
// A client's own commands coming back in a bundle are proof the server took them.
// No new message, no version bump.
inline int cmdSendWindow(int credit, int inFlight) {
    const int room = kCmdQueueCap - inFlight;
    const int n = credit < room ? credit : room;
    return n > 0 ? n : 0;
}

// Message kinds. Lobby and game messages share one stream per connection.
enum class Msg : uint8_t {
    // handshake / session
    Hello = 1,          // C->S: version, buildId, dataHash, name
    Welcome,            // S->C: sessionId, your name accepted
    Reject,             // S->C: reason string (version/data mismatch, etc.)
    Ping, Pong,         // keepalive (either direction)
    Bye,                // clean shutdown, reason string
    // lobby
    ListGames,          // C->S
    GameList,           // S->C: list of GameInfo
    CreateGame,         // C->S: name, password, mapId, options
    JoinGame,           // C->S: gameId, password
    JoinResult,         // S->C: ok+slot or error reason
    LeaveGame,          // C->S
    SlotUpdate,         // C->S: slot edit (own, or any if host)
    LobbyState,         // S->C: full slot table + settings (broadcast)
    Chat,               // both: text
    Kick,               // C->S (host): slot
    StartGame,          // C->S (host)
    GameStarting,       // S->C: final setup + seeds + resume token
    Loaded,             // C->S: map/data loaded, ready to tick
    // in-game
    PlayerCommands,     // C->S: n commands (player byte overwritten by server)
    TickBundle,         // S->C: tick, commands (sorted), events
    StateHash,          // C->S: tick, hash
    Desynced,           // S->C: tick, reason
    PlayerStatus,       // S->C: slot, status, ping (informational)
    // durability (M5)
    Rejoin,             // C->S: gameId, resume token -> GameStarting + bundle log
    Pause,              // S->C: cause, player (game paused; ticks stop)
    Resume,             // S->C: cause, player (game resumes)
    Spectate,           // C->S: gameId, password -> GameStarting (slot 0xFF) + log
    SetGameOptions,     // C->S (host): full GameOptions (lobby: rebroadcast; in-game: speed)
    SpeedUpdate,        // S->C: game speed changed in-game (speed byte) -> client re-paces
    MissionOutcome,     // S->C: campaign mission won/lost (int8: +1 victory, -1 defeat)
    SetPause,           // C->S (host/only-human): u8 want -- pause or resume the game.
                        // Distinct from the drop-driven pause above: it never expires,
                        // because nobody is disconnected and nothing should forfeit.
    // account login (SCRAM-SHA-256; see src/net/auth.h). APPENDED, not slotted in
    // beside Hello where they belong logically: these values are written into
    // replay files, so renumbering the existing ones would break every replay
    // ever recorded.
    AuthRequired,       // S->C: this server wants an account -- sent INSTEAD of Welcome
    AuthBegin,          // C->S: username, client nonce
    AuthChallenge,      // S->C: does the account exist?, salt, iterations, server nonce
    AuthProof,          // C->S: the client's proof  (existing account)
    AuthRegister,       // C->S: StoredKey + ServerKey  (creating a new account)
    AuthResult,         // S->C: status, server signature, message
    MapOffer,          // both: room, map id, SHA-256, byte count
    MapRequest,        // both: room; request the offered package
    MapChunk,          // both: room, offset, <=64 KiB raw bytes
    MapReady,          // C->S: room, verified SHA-256
    MapError,          // both: room, explanation
    // Opt-in persistent Crusades service. Requests have NO account/player ID;
    // server binds them to its authenticated connection, never a display name.
    CrusadesGetAllegiance, // C->S: str campaign (1..128 bytes)
    CrusadesSetAllegiance, // C->S: str campaign, u64 expected allegiance revision
                          // (UINT64_MAX = not yet joined), u8 alliance (1 Honor, 2 Terror)
    CrusadesAllegianceResult, // S->C: u8 operation (0 get,1 set), u8 status
                          // (0 OK,1 malformed,2 unauthenticated,3 disabled,4 rejected),
                          // str campaign, u8 alliance (0 absent), u64 revision
                          // (UINT64_MAX absent), u64 joinedUnix, u64 changedUnix,
                          // str explanation. Absent timestamps=0. Exact payloads;
                          // no trailing extensions or client-supplied identity.
    CrusadesIssueBattle, // C->S: str campaign(1..128),u32 territory,str opponent(3..20).
                        // Account caller, map, rules, and launch token are NEVER client supplied.
    CrusadesBattleResult, // S->C (issuer response/opponent invitation): u8 status
                        // (0 OK,1 malformed,2 unauthenticated,3 disabled,4 rejected),
                        // str campaign,str battle ID,u32 room,str map,u64 expiresUnix,str reason.
                        // Designated opponent accepts via ordinary JoinGame; exact payloads.

    // Independent strategic messages, never commands or replay tick bundles.
    // All payloads start with u16 campaign protocol version, u32 request ID.
    // See net/crusades.h for bounded, exact versioned structures. ID zero is
    // reserved for unsolicited server lifecycle/snapshot notifications.
    CrusadesListCampaigns,    // C->S: paginated catalog request
    CrusadesGetSnapshot,      // C->S: campaign + last known revision
    CrusadesCampaignList,     // S->C: authoritative catalog page
    CrusadesCampaignSnapshot, // S->C: complete definition + state + revision
    CrusadesGetPlayerStatus,  // C->S: own authenticated campaign participation
    CrusadesPlayerStatus,     // S->C: own allegiance + recent battle references
    CrusadesGetBattleStatus,  // C->S: battle ID (participants only)
    CrusadesBattleStatus,     // S->C: issued/started/terminal + verified result
    CrusadesError,            // S->C: typed sanitized campaign error
    CrusadesGetMatchmaking,   // C->S: current campaign aggregate rendezvous board
    CrusadesSearchBattle,     // C->S: own authenticated territory search
    CrusadesCancelSearch,     // C->S: cancel own search, never another account
    CrusadesMatchmakingStatus,// S->C: versioned modern queue aggregate + own search
    CrusadesGetTerritoryHistory, // C->S: bounded terminal archive page
    CrusadesTerritoryHistory,    // S->C: verified metadata, opaque replay availability
    CrusadesGetReplayChunk,      // C->S: battle ID + bounded offset/limit
    CrusadesReplayChunk,         // S->C: request-bound immutable replay bytes
    OverrideOffer, OverrideRequest, OverrideChunk, OverrideReady, OverrideError,


};

// The outcome of a login, as carried by Msg::AuthResult.
enum class AuthStatus : uint8_t {
    Ok = 0,             // signed in to an existing account
    Created = 1,        // no such account, so one was created -- and you are signed in
    BadPassword = 2,    // the account exists and the proof did not check out
    BadUsername = 3,    // the name breaks the naming rules (not "no such account")
    Throttled = 4,      // too many failed attempts; the payload carries the wait in ms
    NameTaken = 5,      // someone registered the name between challenge and register
    ServerError = 6,    // the server could not save the account
};

// A slot in a game's setup. type: 0=open, 1=human, 2=ai, 3=closed.
struct SlotInfo {
    uint8_t type = 0;
    uint8_t faction = 0;   // 0=ara 1=tar 2=ver 3=zon 4=cre
    uint8_t color = 0;     // 0..9 palette slot
    uint8_t team = 0;      // 0..kMaxSlots-1
    uint8_t ready = 0;
    uint8_t aiLevel = 2;   // AI difficulty: 0=passive 1=easy 2=normal 3=hard 4=absurd
    std::string name;      // player display name ("" for open/ai)
};

// A game as seen in the browser.
struct GameInfo {
    uint32_t id = 0;
    std::string name;
    std::string mapId;
    uint8_t players = 0, capacity = 0;
    uint8_t running = 0;      // 0=lobby (joinable), 1=running (view only)
    uint8_t passworded = 0;
    uint32_t uptimeSec = 0;
};

// Maximum live game speed, in tenths (80 = 8x); shared by UI and server admission.
inline constexpr int kMaxGameSpeed = 80;

// Per-game settings chosen by the host.
struct GameOptions {
    uint8_t crusades = 0;
    uint8_t forfeitSelfDestruct = 0;   // 0=units go inert on forfeit, 1=self-destruct
    // Override tier for this game (hpi::OverridePolicy): 0=none (pure retail),
    // 1=cosmetic (art/sound overrides allowed, never affect the sim), 2=full
    // (gameplay overrides allowed but every player must share the same ones).
    // MP defaults to cosmetic. Every player mounts the room's tier at game start.
    uint8_t overridePolicy = 1;
    // Game speed in tenths (10 = 1.0x). Changes the server's tick CADENCE (and the
    // client's playout rate); the per-tick dt stays 1/kServerHz so the sim math is
    // identical -- only how fast ticks happen in wall-clock changes. Deterministic.
    uint8_t speed = 10;
    uint8_t speedUnlock = 0;   // 1 = the host may change speed in-game with -/+
    // Per-player unit limit (production/build halts a player at this many live units).
    // One of 250/500/1000/2000; 2000 default. Serialised as u32.
    uint16_t unitCap = 2000;
    // When 0 (default) losing your Monarch loses the game; 1 makes it just a unit.
    uint8_t monarchExpendable = 0;
    // Stress test (SP all-AI spectate): 1 = spawn each AI at ~95% of the unit cap in
    // its faction's combat units the moment the game starts, to load-test the sim.
    uint8_t stressTest = 0;
    // Fog of war (client-only display, never hashed -- every client applies the
    // same room rule, so it stays fair): 0 = NOT EXPLORED (terrain starts hidden),
    // 1 = EXPLORED (terrain starts mapped). Both retain seen terrain under fog;
    // 2 = FULL VISION (no fog at all: the whole map and every unit are visible).
    // Host-set in the room.
    // Random Start Locations: 0 = fixed (slot N always takes the map's Nth start,
    // so players memorise spawns), 1 = the starts are shuffled for the match.
    // Deterministic -- derived from the match seed, so every peer agrees.
    uint8_t randomStarts = 0;
    uint8_t fogExplored = 1;
    sim::PathfindingMode pathfindingMode = sim::PathfindingMode::Retail;
    uint8_t doubleSight = 0;   // skirmish/MP only; ignored for campaign missions
    // Benchmark INTENSITY: 0=off, 1=Low..6=Extra Absurd -- an all-AI perf run with a spawn ramp
    // (see MatchConfig::benchmark). Deterministic, so it IS part of the hashed sim.
    uint8_t benchmark = 0;
};

// A sim-affecting server decision, sequenced inside a TickBundle so every peer
// (and the referee) applies it on the same tick and the log replays identically.
struct Event {
    // CampaignForfeit is emitted only by an issued campaign room. Ordinary
    // skirmish Leave/Forfeit keep their existing inert-army behavior.
    enum class Kind : uint8_t { Forfeit = 1, Leave = 2, CampaignForfeit = 3 };
    Kind kind = Kind::Forfeit;
    uint8_t player = 0;
};

// ------- little-endian byte buffer writer/reader ---------------------------

struct Writer {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); }
    void u64(uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8 * i))); }
    void f32(float v) { uint32_t x; std::memcpy(&x, &v, 4); u32(x); }
    void str(const std::string& s) {              // u16 length + bytes (capped)
        uint32_t n = uint32_t(s.size() > 4096 ? 4096 : s.size());
        u8(uint8_t(n)); u8(uint8_t(n >> 8));
        b.insert(b.end(), s.begin(), s.begin() + n);
    }
    // Raw bytes, u16 length + payload -- nonces, salts and digests in the login
    // handshake. Same framing as str(), but it carries values with embedded NULs.
    void bytes(const void* p, size_t n) {
        uint32_t m = uint32_t(n > 4096 ? 4096 : n);
        u8(uint8_t(m)); u8(uint8_t(m >> 8));
        const uint8_t* q = static_cast<const uint8_t*>(p);
        b.insert(b.end(), q, q + m);
    }
    void bytes(const std::vector<uint8_t>& v) { bytes(v.data(), v.size()); }
    // Whole-file payloads (override package entries): u32 length + payload,
    // never truncated. Not for login fields -- those keep str()/bytes() and their
    // 4 KiB cap. The caller bounds the total (e.g. maps::kMaxBytes).
    void blob(const std::vector<uint8_t>& v) {
        u32(uint32_t(v.size()));
        b.insert(b.end(), v.begin(), v.end());
    }
    void cmd(const Command& c);
};

struct Reader {
    const uint8_t* p; const uint8_t* end;
    bool ok = true;
    Reader(const uint8_t* d, size_t n) : p(d), end(d + n) {}
    bool avail(size_t n) const { return size_t(end - p) >= n; }
    uint8_t u8() { if (!avail(1)) { ok = false; return 0; } return *p++; }
    uint32_t u32() {
        if (!avail(4)) { ok = false; return 0; }
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= uint32_t(*p++) << (8 * i);
        return v;
    }
    uint64_t u64() {
        if (!avail(8)) { ok = false; return 0; }
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= uint64_t(*p++) << (8 * i);
        return v;
    }
    float f32() { uint32_t x = u32(); float v; std::memcpy(&v, &x, 4); return v; }
    std::string str() {
        if (!avail(2)) { ok = false; return {}; }
        uint32_t n = uint32_t(p[0]) | (uint32_t(p[1]) << 8); p += 2;
        if (n > 4096 || !avail(n)) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p), n); p += n; return s;
    }
    // Counterpart to Writer::bytes. `want` (when non-zero) is the exact length
    // the caller expects -- a digest is always 32 bytes, and accepting any other
    // length would mean parsing an attacker-chosen size into a fixed buffer.
    std::vector<uint8_t> bytes(size_t want = 0) {
        if (!avail(2)) { ok = false; return {}; }
        uint32_t n = uint32_t(p[0]) | (uint32_t(p[1]) << 8); p += 2;
        if (n > 4096 || !avail(n) || (want && n != want)) { ok = false; return {}; }
        std::vector<uint8_t> v(p, p + n); p += n; return v;
    }
    // Counterpart to Writer::blob: a u32 length no larger than `maxLen` and no
    // larger than what is actually left, checked before anything is allocated.
    std::vector<uint8_t> blob(size_t maxLen) {
        const uint32_t n = u32();
        if (!ok || n > maxLen || !avail(n)) { ok = false; return {}; }
        std::vector<uint8_t> v(p, p + n); p += n; return v;
    }
    Command cmd();
};

}  // namespace tak::net
