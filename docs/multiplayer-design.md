# Multiplayer Design: Client–Server, 8 Players, Teams

Status: **proposed** (v1 scope agreed 2026-09-04; revised after adversarial
design review). Supersedes the 2-player peer-to-peer lockstep in
`src/net/lockstep.{h,cpp}` as the multiplayer story; the deterministic-sim
substrate underneath it is kept.

## 1. Goals

- **Client–server topology.** All clients make one outbound TCP connection to a
  central server (`takserver`) reachable on the LAN or internet. Everything —
  lobby, game traffic — is proxied through it. No NAT traversal or port
  forwarding on any player's side.
- **Up to 8 players per game**, any mix of humans and AIs.
- **Up to 8 teams.** Default: every player/AI on its own team (free-for-all).
  Players on the same team share vision and radar (fog of war and minimap).
- **Own color per player** (the retail art ships exactly 10 color variants).
- **AI players run on the server**, not on the host's machine. The host picks
  them in the lobby; the server simulates and commands them.
- **Control isolation:** a player commands only their own units — allies
  included cannot be commanded.
- **Named games with optional passwords**, chosen by the host.
- **Game browser:** the server lists joinable (lobby-state) games and live
  (running, unjoinable) games.
- **Durable sessions:** a player the server stops hearing from holds the game
  for up to 30 seconds -- in all, per game, over all of their stalls, counted
  from when they went quiet -- (everyone sees the countdown); back in time -- the same connection, or an
  automatic reconnect -- the game goes on, otherwise a server AI (Normal)
  takes the seat over (§4, protocol 289).

The current implementation supports spectators and [verified map transfer](map-transfer.md).

Remaining non-goals for v1 (tracked in §10): allied mana sharing / unit gifting, mid-game diplomacy, persistent
accounts or ratings, wire encryption (deploy behind a TLS proxy — and on
untrusted networks that proxy is *not* optional, see resume tokens in §4).

## 2. What exists today (and what survives)

The engine already has the hard part: a **deterministic 30 Hz sim** where only
commands cross the wire, verified in 2-player lockstep with periodic FNV-1a
state hashes. The sim proper is RNG-free, the COB VM fixed-seeded, and
`stateHash()` already iterates all players generically.

| Keep | Replace |
|---|---|
| Deterministic `World::tick` at fixed 1/30s | `net::Session` 2-peer host/client class |
| 35-byte `Command` wire struct + serialization | `--host`/`--join` out-of-band setup (identical command lines) |
| `issue()` → `apply()` command funnel with per-unit `owns()` check | Trusting the wire-supplied `Command.player` byte |
| Length-prefixed framing (u32 len + u8 kind) | The 3-message protocol (Hello is sent but never checked) |
| Periodic state hashing (every 30 ticks) | Desync-is-fatal handling (becomes referee + auto-resync) |

Known command-path leaks that must be fixed regardless (they desync even the
current 2-player mode): **Ctrl+D** (self-destruct writes `hp=0` directly) and
the **F9/F11** stress-spawn keys. Self-destruct becomes a real `Cmd::Destroy`;
the stress keys are disabled in networked games.

## 3. Architecture overview

```
   takclient (client)  ─┐
   takclient (client)  ─┼─ TCP ──►  takserver ── per-game: GameRoom
   takclient (client)  ─┘             │  ├─ command sequencer (tick authority)
                                    │  ├─ headless World (referee + AI host)
                                    │  ├─ ai::Controller per AI slot
                                    │  └─ bundle log (reconnect + replay)
                                    └─ lobby: game registry, browser, chat
```

**Server-sequenced deterministic lockstep.** Every participant that needs game
state — each client, and the server itself — runs the identical sim. Clients
send their commands to the server; the server is the *only* clock: it assigns
every command (human or AI) to a tick, closes each tick on a fixed 30 Hz wall
schedule, and broadcasts one `TickBundle` per tick to all clients. A client may
only advance its sim through tick T once it has bundle T, and **clients never
apply their own commands locally** — every command takes the round trip and
executes only when its bundle arrives (the current `issue()` funnel already has
this shape).

Causality note: a command issued against the client's view of tick T executes
at server tick S ≥ T (latency ≈ RTT + at most one bundle interval). This is
standard relay-sequenced lockstep, and safe because commands are *intent*
(move-to, attack-id) validated at execution time — a stale reference (unit died
in between) degrades to a no-op via the existing `owns()`/unit-lookup checks in
`apply()`. The server additionally pre-validates `owns()` against the referee
sim at the closing tick, so accept/reject is identical on every sim.

**Sequenced server events.** Anything that affects sim state but originates
from wall-clock server decisions — an AI taking over the seat of a player
whose disconnect wait ran out, a player leaving voluntarily, (later) an
alliance change — is
**not** a side-band notice: it is an *event record inside the TickBundle*,
executing at that bundle's tick on every sim, and therefore part of the log.
This is what keeps three properties simultaneously true: the log replays to
the same end state, a spectator reconstructs the same world as live clients,
and the referee's hash stays comparable. (Purely informational notices —
connected/dropped indicators, ping — remain side-band `PlayerStatus`
messages; they don't touch the sim.) The bundle format carries its (possibly
empty) event section **from the first server milestone** so later features
never break the wire or log format.

Why this model (vs. authoritative state replication):

- Bandwidth is tiny and flat: ~35 bytes per command, one small frame per tick.
- The deterministic sim already exists and is verified; replication would mean
  serializing full World state and rewriting interpolation.
- **AI-on-server falls out for free**: the server already runs a sim per game
  (as referee), so an AI player is just another command source feeding the
  sequencer — exactly like a human, subject to the same validation.
- **Catch-up nearly falls out for free**: the sequenced bundle log *is* the
  game; a spectator replays it headless to catch up, and a reconnecting
  player takes it from the first tick it lacks (§4). A log written to disk is
  a replay file.

Clients never gate on *each other*. A slow or stalled client only delays
itself — the server keeps closing ticks on schedule and other clients are
unaffected. Honestly stated, though: that client's own command-to-effect
latency equals its lag (10 s behind ⇒ its orders visibly land ~10 s later on
its own screen), so the client HUD shows a "behind by N s" indicator. A client
that goes SILENT (nothing at all for `kStallMs`, 8 s) is another matter: the
game stops and waits for it (§4).

**Client jitter buffer + adaptive playout.** Bundles are sequenced by the server
but *arrive* unevenly (network jitter, TCP retransmits). Draining "play every
bundle the instant it lands" turns each late bundle into a visible micro-stall.
So the client keeps a small receive buffer and paces playout on its own wall
clock, staying `netDelay_` bundles behind the newest received; a late bundle is
covered from that reserve, and only a gap deeper than the reserve stalls. This
is pure pacing — the same bundles play in the same order, so every sim and hash
stays byte-identical (`mpStep()` in `src/client/main.cpp`).

Two refinements make it self-tuning:
- **Auto-sizing** (the default; `TAK_NET_DELAY` overrides — `0` disables the
  buffer, a positive integer pins a fixed depth): an active RTT probe (one
  ping/sec) plus a decaying-max of the measured bundle-arrival jitter size the
  reserve each frame — `clamp(2 + max(kJit, kRtt), 2, 16)` bundles — so a clean
  LAN sits at 2 and a jittery WAN grows the cushion only as needed.
- **Servo playout** holds the reserve *at* that target instead of drifting: the
  sim clock runs slightly fast when the buffer is deep (shed latency) and slightly
  slow when it is shallow (rebuild the reserve) — `rate = 30·clamp(1 + 0.06·(buffered
  − target), 0.7, 1.3)` Hz. Equilibrium is exactly `buffered == target`, so added
  latency stays tight rather than accreting. A backlog deeper than target+60 (a
  rejoin replay, or a client that fell behind) is fast-forwarded, not paced.

Only game ticks are gated by this buffer; camera, zoom, and edge-scroll run every
render frame in `cameraFrame(dt)`, independent of the sim, so input feel is never
coupled to link latency.

*Test harness.* A synthetic link model injects receive delay on the client with
no server needed: `TAK_NET_BASE_MS` (constant one-way), `TAK_NET_JITTER_MS`
(uniform 0..N added), `TAK_NET_LOSS_PCT` (probability of a ~2·base retransmit
spike). `TAK_NETBENCH=1` runs the headless client at a fixed 60 fps and reports
`delay`, measured `rtt`, frames, and starved-frame `stalls`. Measured over 15 s
of an `--mpai` game, the servo cuts stalls roughly 10–12× on lossy links
(base60+jit60+loss5%: 28.2% → 2.3%; base80+jit120+loss10%: 61.2% → 6.0%) while
holding delay at 4–5 bundles.

### Trust model

The server stamps `Command.player` from the connection it arrived on and
discards whatever the client claimed — closing today's spoof hole (any client
could set `player=0` and command the opponent). Sim-level `owns()` checks
remain in every client as defense in depth. The server's own sim gives it a
canonical state hash for attribution — with one humility rule (§4, referee
suspicion): when everyone disagrees with the referee but agrees with each
other, the referee suspects *itself*.

### Game-data agreement + mods

Every peer reads its game files through a VFS over a retail install directory
(`hpi::mountRetailRoot`: root `*.hpi` + `Maps/*.kmp` + `Music/`, resolved by the
retail newest-date rule, plus one merged mod layer from `mods/`). Lockstep needs
every sim to feed on byte-identical *gameplay* data, so that's fingerprinted and
agreed:

- **`hpi::gameplayHash(vfs)`** — a content hash over just the files the sim reads
  (unit `.fbi`, weapon/side/game `.tdf`, build lists, features; **not** maps, which
  are per-game, and **not** cosmetics — art, models, animation, sound, music, GUI).
- Each game carries a **game-mods flag** (`GameOptions.gameMods`, 0/1). When on,
  the host's ticked `mods/game/<pack>` folders are packaged and transferred (see
  [Per-room game mods](#per-room-game-mods)); every player mounts that verified
  package at game start so its registry matches the referee's. A single-player
  room always sends 1; a multiplayer room defaults to 0 (the create screen's GAME
  MODS toggle); campaign missions and Crusades battles are forced to 0.
- **Cosmetic mods are per player and never travel.** Each client lays on top of
  the room's package its own always-on `mods/cosmetic/*.hpi` archives (every
  room) and its ticked `mods/cosmetic/<pack>` folders (every room but a campaign
  one), through the cosmetic keep-filter (`!affectsGameplay`, no `features/`, no
  `ai/`). That filter strips everything `gameplayHash` reads, so the Loaded hash
  is unchanged by them. The handshake mount, the referee data and map packages
  (`maps::build`) never include any cosmetic layer, so a cosmetic `maps/*.tdf`
  cannot reach a map digest.
- Enforcement is two-stage. The **`Hello`** carries the client's *pure-retail*
  gameplay hash (mounted with no mods), so the base game files must match for
  everyone regardless of mods — a modified retail file is rejected at connect. At
  **`Loaded`** the client sends its gameplay hash *with the room's game mods*, and
  the referee (mounted with the same package) rejects a mismatch — so a player
  lacking or differing on a game mod is caught before tick 0 rather than as a
  mid-game desync. A pure relay (no `--data`) has no referee hash and falls back
  to the live `StateHash` cross-check.
- The old `overrides/` folder is never read. A client that finds one logs one
  line at startup and leaves it untouched.

### Determinism contract (explicit)

Lockstep at 8 players lives or dies on bit-identical simulation, so the
contract is stated, enforced, and narrow:

- **Same protocol version, not the same binary.** The `Hello` carries a build
  id, but the server only logs it; it refuses a client whose `kNetVersion`
  differs. Linux, Windows and macOS builds with the same `kNetVersion` play
  together (the sim's math is implementation-independent, as below, and a
  cross-build golden hash is checked in CI). The sim is hardened as below.
- The sim (and anything it calls) is compiled without `-ffast-math` and with
  `-ffp-contract=off` pinned in CMake for EVERY target (a directory-wide
  `add_compile_options`, so the server's AI and any sim header inlined into
  client/server code are covered too -- see `fp_contract_test`).
- The sim's transcendentals (`sin`/`cos`/`atan2`/`sqrt` in movement/turning)
  route through a small deterministic math shim (`sim/detmath`) rather than
  raw libm — libm results differ across implementations and versions even on
  the same architecture. The shim has landed (`tools/check-detmath.sh`
  enforces it), so builds need not share a libm.
- Data gate: `Hello` carries a checksum over the loaded unit registry + build
  tree + map; the server refuses mixed-data games with a readable error.
- The AI's RNG is outside the sim and cannot desync the world — only its
  *commands* matter, and those are sequenced like everyone else's.
- *(Historical — the flow fields are gone; the invariant is not.)* The
  flow-field pathfinding cache stays a **pure memo**: field content must
  remain a function of (nav grid, goal, domain) only — never of when or
  whether it was built — because the server's AI queries build cache entries
  clients never build. `pathExists` stays query-only; cache access is
  single-threaded per room (debug-asserted). This invariant is what makes the
  `mutable` cache safe; breaking it desyncs only server-vs-client
  configurations, the nastiest kind of bug, so it's load-bearing and
  documented in the code.
- Fog of war is excluded from `stateHash` (render-only), so per-client team
  fog cannot desync; the referee runs with **no vis player set** and skips the
  visibility pass entirely (the AI is omniscient and nothing on the server
  renders).

## 4. The server (`takserver`)

A new headless binary. Needs the same game data as clients (it runs the sim
and the AI): point it at the extracted data directory. No SDL, no rendering —
link `sim`, `tdf`, `net`, and the new `ai` module only.

- **Lobby service.** Holds the game registry. Handles create / list / join /
  leave / slot changes / chat / start. One TCP port (default 7677) for
  everything; a connection is in lobby state or attached to one game. If the
  host disconnects **pre-start**, host powers pass to the next human in the
  lobby (the game dissolves if none remain).
- **GameRoom.** One per game: player slots (up to 8), the sequencer, the
  headless referee sim, one `ai::Controller` per AI slot, the bundle log, and
  per-game settings (map, Crusades balance, gods on/off).
- **Tick loop.** Fixed 30 Hz per room. Each tick: collect queued client
  commands (stamped, validated, capped — §5 hardening), run each due AI
  controller (they emit commands into the same queue), append due sequenced
  events (forfeits, leaves), close the bundle, append it to the log, broadcast,
  advance the referee sim. AI controllers think once per 30 ticks (1 s cadence,
  matching today's AI), staggered across AI slots. On `Resume` after a pause
  the tick closer rebases its wall-clock schedule — no burst of catch-up ticks.
- **Desync referee.** Clients send `StateHash` every 30 ticks. Clients lag the
  referee by RTT + buffering, so the server keeps a bounded ring of its own
  per-tick hashes to compare against late-arriving client hashes. A
  mismatching client gets a `Desynced` notice and its game ends (a reconnect
  would only continue its wrong world, so it never reconnects); when its
  connection goes, the disconnect wait below runs and an AI takes the seat.
  **Referee suspicion rule:** if a hash round shows the clients agreeing with
  each other and disagreeing with the referee, the server suspects its own sim
  (e.g. a server-only AI code path bug), logs loudly, stops dropping clients,
  and flags the room for investigation instead of drop-looping every innocent client.
- **Hash breadth.** To attribute desyncs quickly, the extended `stateHash`
  (§6) also folds in projectile owner + quantized position and order targets —
  counts alone let same-count divergences hide until damage lands.
- **Disconnect policy (protocol 289).** The rule: if a player effectively
  stops communicating, the game waits 30 seconds for them, paused, with a
  countdown; if they are still not communicating after that, they are dropped
  and an AI at Normal plays their units. The 30 seconds are a budget per seat
  per game, not per stall, spent from the player's last contact (not from when
  the countdown appears), so nobody can hold a game indefinitely by going
  quiet and answering just in time, again and again -- not even with silences
  too short to start a wait.
  - *Stopping.* A seated human in a running game (past tick 0, not defeated)
    stops communicating when the server has received nothing from it for
    `kStallMs` (8 s; a healthy client reports progress every few ticks and
    answers the server's 5 s keepalive ping, and the game client pings once a
    second), or at once when its connection closes without a `LeaveGame`. The
    server then sets that seat's wait deadline to its last contact plus what
    is left of its budget (`kDropWaitMs`, 30 s per seat per game) -- the
    silence before the wait is already spent, so a first full stall counts
    down from 22 and is taken over 30 s after the player went quiet -- and
    closes no tick while any seat is waiting: every peer freezes on the same
    tick. A silence that has already used what was left is taken over at once,
    with no `Waiting`.
    `Msg::Waiting` (the seats and each one's milliseconds left) goes to the
    whole room whenever the set changes, and to a spectator or a reconnected
    player as it arrives; clients draw `WAITING FOR <name> 27`, one line and
    countdown per seat, so several stalls each run their own clock. A player
    alone in the room (single-player) gets no wait: nobody else is affected, and
    when its link goes the room ends as before.
  - *The budget.* A wait that ends charges the seat's budget for the whole
    silence, from its last contact to its return, in whole seconds, rounded up
    and never past what is left (the countdown shows whole seconds: back while
    it reads 1 spends the budget). The next stall's countdown starts from the
    rest less that stall's own silence -- after 20 s spent, from 2 -- and a
    stall whose silence has used what was left is taken over at once, with no
    wait and no `Waiting` for it.
  - *Short holds.* A silence shorter than `kStallMs` starts no wait but can
    still stop the game: flow control (`canAdvance`) will not run the room more
    than the seat's lead past its last acknowledged tick, so after the lead
    (under a second at a normal ping) the room stands until the seat speaks
    again. The server measures that directly: wherever a tick would close, a
    seat is *holding* the game while the room is past `ackTick + leadTicks`
    for it. The rule: each hold is timed from when the server first finds the
    room stopped on the seat to when it no longer is (a pause or another seat's
    wait suspends the clock; only time the game really stood for this seat
    counts), and a hold that ends is charged its length minus a 1 s grace
    (`kHoldGraceMs`), in whole seconds rounded up, never past what is left; one
    of 1 s or less costs nothing, so ordinary network jitter is free. A hold
    that reaches the grace plus what the budget has left is taken over then and
    there, as a wait that runs out. The same applies to a seat that talks but
    does not acknowledge (a stuck client still answering pings) and to a
    reconnected seat catching up its missed ticks. A hold that turns into a
    wait (the silence reaches `kStallMs`) is not charged twice: the wait
    charges the silence from the last contact, and only what the hold held
    before that contact (a seat that talked without keeping up, then went
    quiet) is charged as a hold. Holds count only where a wait would (someone
    else to wait with, past tick 0). The accounting is the server's alone and is not sim
    state: peers see it only as the `ms left` in `Waiting` (which they count
    down) and the `AiTakeover` tick, which the replay records like any event, so
    clients, referee and replay agree. Each `Waiting` also ends with the
    recipient's own budget (`running`, `ms`: its running countdown, else what
    its seat has left, which the client shortens by its own silence since it
    last heard from the server), so a client whose link drops counts down and
    retries for as long as the server will really wait, not a fresh 30 s; a
    hold that is charged sends the seat's client a fresh `Waiting` for this.
    Silence before the first tick, or before the client has loaded, is not
    charged (loading has its own deadlines): a connection that closes then
    counts down from the close. `Waiting`
    is not recorded and no sim state changed, so protocol 289 still. Like the
    budget, the hold accounting is wall-clock time on the server alone.
  - *Coming back.* Talking again on the same connection inside the window (a
    frozen frame, a hitch, a Wi-Fi blip TCP rode out) ends that seat's wait. A
    client whose link actually dropped reconnects by itself: a new connection,
    the same login (the client keeps the login's SCRAM keys for the session;
    the password itself is gone), then `Rejoin{game, resume token, fromTick}`,
    `fromTick` being the first tick whose bundle it never received. The server
    answers `Reseated{slot, rotated token, fromTick, logEnd}` and streams the
    log from `fromTick`; the client keeps its world (no rebuild from tick 0)
    and treats the ticks before `logEnd` as history -- its own orders wait and
    its acknowledgements are not counted until it is through them. A `Rejoin`
    also replaces a seat's old connection the server has not seen go yet.
    The resume token is ≥64-bit random and single-use (rotated on every
    reconnect).
  - *Taken over.* When a wait runs out the seat's connection, if still open,
    is closed (`Bye`: an AI took over your seat), and from the next tick the
    seat is a server AI at Normal: the same player id, units, buildings and
    mana. A sequenced `Event::AiTakeover{player}` in that tick's bundle marks
    the player computer-owned (`Player::automaticGates`, hashed) on every peer,
    the referee and the replay; the server adds an `ai::Controller` for the
    seat (only with no tick in flight, since the worker owns the controllers
    while one runs), which reads the empire afresh every think -- home is its
    buildings' centroid, producers and army are whatever it has -- so starting
    mid-game needs nothing from the opening. Its commands enter the bundles
    like any AI seat's, so peers and replays simply apply them. A reconnect
    after that is refused: "the wait ran out: an AI took over your seat".
    Issued Crusades battles are the exception: a duel between two accounts does
    not go on with a stand-in, so the absent participant forfeits
    (`CampaignForfeit`) when the wait runs out, as before.
  - *Leaving on purpose* is unchanged: `LeaveGame` (the game client sends it
    when the player quits a running game from the menu) is a `Leave` event at
    once -- the units go inert, no wait. A defeated player's departure never
    waits either.
  - Replaced (0.8.9 and earlier): the slot held for 5 minutes, the 2-minute
    per-player auto-pause budget with its grace-expiry `Forfeit`, and the long
    rejoin that rebuilt the world from tick 0 by replaying the whole bundle log
    (with its resume-ticket file and `--mprejoin`). Spectators still catch up
    that way; seated players no longer do.
- **Log format.** Bundles including events, prefixed by a header: format
  version, build id, data checksum, and the full `GameStarting` payload (slot
  table, map, options, per-AI seeds) — that header is exactly what makes a log
  a self-contained replay file. Logs are memory-bounded per room (command data
  is ~KB/min; caps stated in config anyway) and optionally persisted.
- **Scaling.** One thread per GameRoom plus one I/O thread (poll/epoll).
  Late-game rooms cost real CPU (double-digit-ms ticks were the reason the
  sprite auto-tuner exists), so capacity claims wait for the M4 measurement;
  rejoin fast-forwards run niced/capped so they can't starve live rooms.

## 5. Protocol

Framing (kept): `u32 LE length` + `u8 kind` + payload, over one TCP stream
with `TCP_NODELAY`. All multi-byte integers little-endian. The `Command` wire
format (35 bytes) is kept, with the caveat that the `player` byte is
server-assigned on receipt.

**Handshake — actually enforced this time.** `Hello` carries protocol version,
engine build id (binary content hash), and the data checksum. The server
rejects mismatches with a human-readable reason. (Today's kHello is sent and
silently skipped.)

### Lobby messages (client ⇄ server)

| Msg | Payload | Notes |
|---|---|---|
| `Hello` / `Welcome` | proto ver, build id, data hash / assigned session id | version + data gate |
| `SetName` | display name | no accounts in v1 |
| `ListGames` / `GameList` | — / entries: id, name, map, players/capacity, state (lobby/running), passworded, uptime | running games listed but unjoinable |
| `CreateGame` | name, password?, map id, options (crusades, gods, speed, unit cap, forfeit rule) | creator becomes host |
| `JoinGame` / `JoinResult` | game id, password? / slot or error | error: full, bad password, running |
| `SlotUpdate` | slot #, {human/AI/closed}, faction, color, team, ready | players set their own; host sets any (host resolves conflicts, assigns AIs); only the server seats a human, so the host can neither change a seated slot's type nor make an empty one human -- such an edit (e.g. one sent before a seat move landed) is dropped |
| `LobbyState` | full slot table + settings | broadcast on every change |
| `SetGameOptions` | full `GameOptions` | host only; in the lobby the server adopts them and rebroadcasts `LobbyState`; in-game only `speed` takes effect (if `speedUnlock`), pushed via `SpeedUpdate` |
| `Chat` | text | lobby and in-game |
| `Kick` | slot # | host only |
| `MoveSeat` | slot # | a seated player (the host too) takes an OPEN seat inside the map capacity, lobby only; not in a Crusades battle or campaign mission room; side, colour, team and ready move with the player, the old seat opens with the target's colour and team; the server answers the mover with `JoinResult` (its new slot), then broadcasts `LobbyState`; anything else is ignored (protocol 284) |
| `StartGame` | — | host only; server validates: every human ready, colors unique, players ≤ map start positions |
| `GameStarting` | final slot table, map id, options, per-AI RNG seeds, resume token | clients load and report `Loaded`; server starts when all humans are loaded; a `Loaded` timeout kicks that player back to the lobby and re-opens the slot (host decides: re-fill or start without) |

### Game messages

| Msg | Payload | Notes |
|---|---|---|
| `PlayerCommands` | n × Command | client → server; player byte ignored/overwritten |
| `OrderBegin` / `OrderMembers` / `OrderCommit` / `OrderAbort` | serial, template Command, shape, count / serial, member deltas (≤ 2,048) / serial, count, digest / serial | client → server (protocol 267): one explicit selection order as a transaction; the server puts the committed order whole into ONE TickBundle (`Cmd::Order`, its activation tick). Legion clients send their selection verbs this way, Retail clients keep per-unit commands. `net/protocol.h` beside `kOrderMaxMembers`, `server/orders.h`, docs/legion-pathfinding.md "Legion 10,000 stage 3" |
| `TickBundle` | tick u32, n × Command (ascending player, then arrival order), m × Event | server → all clients, 30/s; empty bundles are the clock; the event section exists from day one (§3) |
| `StateHash` | tick u32, hash u64 | client → server, every 30 ticks |
| `Desynced` | tick, reason | server → one client; its game ends (no reconnect) |
| `Pause` / `Resume` | cause, player | the host's pause (`SetPause`): server stops closing ticks, never expires |
| `Waiting` | n × (slot, ms left), own (running, ms) | protocol 289: the seats the room is waiting for (§4 disconnect policy); no tick closes while n > 0 |
| `SpeedUpdate` | speed (tenths, 10 = 1×) | server → all; host changed speed mid-game (needs `speedUnlock`); re-cadences tick pacing, not the per-tick `dt`, so the sim stays deterministic |
| `PlayerStatus` | slot, {connected, dropped, left} + ping | informational only — never sim-affecting (sim-affecting lifecycle is an Event in the bundle) |
| `Rejoin` | game id, resume token, fromTick | protocol 289: a reconnect inside the disconnect wait → `Reseated` (slot, rotated token, fromTick, logEnd), then the bundles from fromTick → live bundles |
| `Bye` | reason | clean shutdown both ways |

### Voice chat (protocol 276)

Voice is **presentation only**: it never enters a TickBundle, the replay log, the
referee or `World::stateHash`, and the sim is unchanged by it. The server relays it
beside the game on three messages (`net/protocol.h` beside `kVoiceMaxOpus`):

| Msg | Payload | Notes |
|---|---|---|
| `VoiceListen` | u8 on | client → server: this connection wants voice (voice chat switched on). Off by default, so the headless harness, a benchmark and anyone with voice off are sent none |
| `VoiceUp` | u16 seq, u8 recipients, u8 flags, u16 len, Opus | client → server: one 20 ms frame (48 kHz mono, ~24 kbit/s, in-band FEC), `recipients` = the talker's diplomacy VOICE mask. Flags: START (resets the receiver), END (key released, no audio) |
| `VoiceDown` | u32 talker id, u8 slot (0xFF spectator), u16 seq, u8 flags, u16 len, Opus, [str name] | server → client; the name rides on START frames and every 25th (`kVoiceHasName`) |

Recipients follow the **chat rules exactly**: before the game runs, or for mask
0xff, the whole room including spectators; otherwise the seated players whose bit
is set. Never back to the talker, never to a connection catching up (`replaying`)
or one that has not sent `VoiceListen 1`. Spectators cannot open diplomacy, so
their mask is 0xff and they talk to everyone, as their chat does.

Voice must never cost the game anything. Relayed frames go out through
`Conn::sendLow`: a separate queue that joins the byte stream only once every
ordinary frame queued so far has left, holds at most 16 frames (the newest win),
and refuses frames while 64 KiB of game traffic waits. That ordering holds inside
the program; once bytes are handed to the operating system they are sent in
order, so voice also waits for the kernel: a batch (at most 1 KiB) joins the
stream only while the kernel holds under 2 KiB for that peer, unsent or not yet
acknowledged (`kernelQueued`: `SIOCOUTQ` on Linux, `SO_NWRITE` on macOS; on
Windows `SIO_TCP_INFO`, Windows 10 1703 or later, as the bytes `send()` accepted --
counted by `Conn`, or the TLS socket BIO's count -- minus `BytesOut - BytesRetrans -
BytesInFlight`). Where the kernel cannot answer, voice is dropped rather than sent
ungated: before this, Windows let a talker's voice fill Winsock's 64 KiB send
buffer ahead of every `Progress`. A tick bundle queued later finds at most about
3 KiB of voice ahead of it whatever the link speed, and on a link the game already
fills voice overflows its queue and is dropped. The server also caps each client
socket's unsent kernel queue at 16 KiB (`TCP_NOTSENT_LOWAT`, `limitUnsent`), so a
backlog of game traffic waits in the program where the send-backlog limits see it.

What the sender's kernel cannot show -- a slow reader's receive buffer, or a few
KiB of voice already ahead of a `Progress` on a link of a few kbit/s -- is caught
at each end by how late the game's own traffic is:

- **The talker's uplink** (`MpClient::sendVoice`). While transmitting, the client
  pings the server four times a second (every two seconds while voice is on, for a
  quiet baseline). A round trip more than max(100 ms, 4 ticks) above the lowest one
  (back, or still out) means its own voice is queueing ahead of its `Progress` and commands: voice is
  held -- an END closes the transmission, the queued voice is thrown away, captured
  frames are dropped -- for a back-off of 5 s doubling to 60 s (forgotten after 2
  quiet minutes), and resumes with a START once the probe is quiet.
  The Pong is read once a rendered frame, so a probe is judged by the least it can
  have taken: from its Ping to the last read that found the socket empty of it, not
  to the read that found it. Otherwise a talker whose machine stutters (a 180 ms
  frame every 0.7 s on a fast link) was held for 54 s of every 60.
  `voice_uplink_test` (all platforms, Windows included) talks over a 2500 B/s
  uplink: no `Progress` waits more than ~0.4 s, where without the hold Linux queued
  up to 2.4 s and Windows 5.5 s after 15 s and 22 s after 60 s; and with 180 ms and
  250 ms frames on a fast link nothing is held.
- **A listener's downlink** (`Server::voiceBehind`). `Progress` reports the newest
  tick that has *arrived* as well as the one processed. In a running game the server
  stops voice to a listener whose arrival lags the room by more than 3/4 of the lead
  it would have on an idle link (`flowLeadTicks` at its lowest reported round trip),
  drops its queued voice, and resumes once it is back within half that lead, after
  the same back-off. Arrival, not processing: a slow *machine* has its bundles in
  hand and runs them late, and in a room it holds back it lags by its whole lead for
  as long as that lasts, which is no reason to take its voice away.
  `server_voice_slow_test.py` reads two listeners (one seated, one spectating) at
  1500 B/s while a third player talks: the room keeps about 27 of its 30 ticks a
  second and neither falls more than ~25 ticks behind; without these rules the room
  ran at 2-8 ticks a second. Its `--case cpu` (ctest `server_voice_cpu`) seats a
  fast-link player who runs 22 ticks a second: he holds the room and hears every
  frame (judged by the processed tick he would have heard none).

Per talker the server relays 60 frames / 16 KB a second and drops the
excess. An END marker skips those budgets so a cut-off transmission still
closes, but it carries no audio (audio or START with END fails the connection)
and only the one END owed to a transmission that relayed audio is passed on, so
ENDs can never outnumber relayed audio frames. 300 frames a second, a malformed
frame or a client sending `VoiceDown` fails that connection; 4,000 frames a second
across the server. `VoiceListen` is exempt from the lobby budgets shared by an
address, an account and the server (a dropped one would never be repeated), but
not from the connection's own. The client
(`client/voice.h`) captures and plays on SDL audio devices of its own, buffers
2–8 frames per talker, rebuilds a frame the server dropped from the next frame's
FEC, and conceals a late one. It sends while the push-to-talk key is held or,
under TALK MODE Voice Activation (0.8.6, client only, no wire change), while
`voice::VoiceGate` (`client/voiceptt.h`) is open: a 20 ms frame whose peak
reaches ACTIVATION LEVEL opens it, it holds 20 frames (400 ms) after the last
such frame, and the 2 frames (40 ms) before it opened lead the transmission. It mixes the talkers with each one scaled by that
player's diplomacy **VOLUME** slider (0-100 by player slot, 100 by default; humans
other than yourself only, none for an AI), then applies OUTPUT VOLUME and a soft
limiter. The sliders are local: never sent, hashed or recorded, reset to 100 each
game, greyed while voice chat is off. Opus is built from its pinned source release
(`cmake/Opus.cmake`) and linked only into takclient. Tests: `voice_test` (codec,
jitter buffer, push-to-talk, engine on SDL's disk audio driver, the VOICE CHAT
page, the per-player mix), `diplomacy_ui` (the VOLUME sliders), `voice_client_test` (MpClient on the wire), `server_voice_test.py`
(routing, budgets, abuse against a real server), `server_voice_slow_test.py`
(slow listeners do not hold up the game), `conn_test` (the low-priority queue).

Bundle **Event** records (u8 kind + payload): `Forfeit{player}` (explicit
surrender), `Leave{player}` (voluntary, units per forfeit rule),
`CampaignForfeit{player}` (an issued Crusades battle), `AiTakeover{player}`
(protocol 289: the disconnect wait ran out; a server Normal AI plays the seat
from this tick, the player becomes computer-owned),
reserved: `AllianceChange` (v2 diplomacy). Defeat itself is *not* an event —
it's derived deterministically inside the sim from unit state, so every sim
concludes it independently on the same tick.

New `Cmd` kind: `Destroy` (self-destruct via the command path). No other new
kinds are required — the AI's four world mutations (train, 2× startBuild,
attackMove) all map onto existing `Train` / `Build` / `AttackMove` commands.

### Server hardening (v1, not later)

- Frame-length cap (e.g. 64 KB) — the inherited parser trusts a u32 length up
  to 4 GB and grows its buffer unbounded; reject oversized frames, drop the
  connection.
- Per-player per-tick command cap (e.g. 64) so a spammer can't inflate every
  bundle for eight clients; excess commands are dropped with a notice.
  Explicit orders (protocol 267) have limits of their own: 16,384 members, 64
  orders / 384 KiB outstanding per client, 96 KiB of order records per tick
  shared by the room (first pick rotating), the same per-second byte and
  command budgets (an order's members count as commands; over the command
  budget an order waits at the head of its client's queue, it is never
  dropped). An order in a Retail game fails the connection.
- Full validation of remote input: `Cmd` kind range, `type` strings looked up
  against the registry, unit/target ids sane. Malformed ⇒ disconnect.
- Lobby rate limits (CreateGame, Chat, ListGames floods).
- Keepalive with numbers: client pings every 2 s; either side times out after
  15 s of silence. Load-bearing during pause, when no bundles flow.
- `--server <host>` resolves via `getaddrinfo` — IPv6 and hostnames (today's
  `join()` is IPv4-literal-only).

## 6. Sim changes (engine-wide, benefits single-player too)

1. **8 players.** `players_` grows from the hard-coded 4 to 8 (sized from
   game setup); add a bounds guard in `World::spawn` — today every
   `players_[u.player]` index is unchecked. Grow the F4 panel, god-check
   loops, and scenario arrays that assume 4. Colors need **no structural
   work**: `colorSlot_[8]` already covers 8 players, the `& 7` masks become
   correct bounds guards, and each slot's value spans the 10 retail palette
   variants — every player picks any of the 10 colors, and with 8 players
   lobby-enforced uniqueness is always satisfiable.
2. **Team layer.** `Player::team` (0–7, set at game setup; default = player
   index) + `World::allied(a, b)`. Convert friend/foe tests to `allied()`:
   splash damage, target auto-acquire, auras, resurrect, cloak proximity,
   capture, and the viewer's attack-click/AI target scans. `owns()` stays
   per-player — allies cannot command each other's units. `team` is immutable
   in v1; if diplomacy ever makes it mutable, the change must be a sequenced
   event **and `team` must join `stateHash`** (noted in §10 so we don't paint
   into that corner).
3. **Shared team vision.** The single fog grid design survives: it is
   render-only and excluded from `stateHash`, so each client computing *its
   own team's* fog is desync-safe. The one-line core change is the visibility
   pass filter (`u.player != visPlayer_` → `!allied(u.player, visPlayer_)`),
   plus converting every viewer-side `u.player != localPlayer_ &&
   !cellVisible` render/minimap gate to "not on my team" so allied units draw
   through fog. Radar is already merged into the reveal radius, so shared team
   radar is free. The referee runs with no vis player and skips the pass.
4. **Win condition: last team standing — and defined defeat.** Replace the
   hardcoded 2-player check with per-team alive counts, computed **inside the
   sim** so every sim (and the referee) concludes it independently on the same
   tick. A player is defeated when they have no living units; a *forfeited*
   player's units are disposed per the lobby's forfeit rule (inert or
   self-destruct). A defeated-but-connected human stays in the game with their
   **team's** vision until the game ends (their team fights on) or leaves via
   `Bye`; in an FFA their defeat ends their game — no free spectating
   (spectators are v2). A team is out when all its players are; the game ends
   when one team remains. Ally vision shrink on defeat needs no work — fog is
   recomputed from living units every pass.
5. **N-player spawn.** Generalize the 2-monarch setup: assign map start
   positions (`.ota` `StartPosN` specials) to players — allies clustered on
   adjacent starts when possible — with per-player faction kits and starting
   mana.
6. **Hash coverage.** Extend `stateHash` with veterancy, reload timers, god
   favor, projectile (owner, quantized position), and order targets — counts
   alone hide same-count divergences, and fast attribution is the referee's
   whole job.

## 7. AI extraction (`src/ai/`)

The skirmish AI (~200 lines in `GameView`, `main.cpp:2926–3131`) moves to a
new module, one `ai::Controller` per AI player:

```cpp
namespace tak::ai {
  Profile loadProfile(path);              // parses ai/default.txt weight/limit
  class Controller {
    Controller(int player, const sim::TypeRegistry&, const Profile&, uint32_t seed);
    void tick(sim::World&, uint32_t simTick, const CommandSink&);  // emits net::Commands
  };
}
```

- **Commands, not mutations.** The AI's four direct `world_` calls become
  emitted `Command`s through the sink — on the server they enter the sequencer
  like any client's; offline they feed straight into `apply()`. Same code both
  places. (This also eliminates the class of server-only direct-mutation bugs
  that would make the referee itself desync — see §4's suspicion rule for the
  backstop.)
- **Sim-tick cadence.** Today's `aiTimer_ -= dt` is wall-clock (frame-rate
  dependent — not replay-safe). The controller thinks every 30 sim ticks.
- **Seeded RNG.** The private LCG keeps the AI deterministic, but seed it per
  game + player (from `GameStarting`) instead of the constant, so eight AIs
  don't mirror each other. AI RNG stays outside the sim.
- **World access.** Add const accessors the AI needs (`player()` const, the
  mana-spot list — the AI currently reads the *viewer's* copy) and make the
  flow-field cache `mutable` under the purity invariant of §3 (memo-only,
  single-threaded per room, debug-asserted).
- **Omniscience stays.** The AI ignores fog (positional intel only; its
  economy is honest). That matches retail-era AI and avoids per-AI fog grids.
- Vestigial members from the pre-rewrite AI (`aiKeepId_`, `aiCycle_`, …) are
  dropped during the move.

Single-player keeps working identically: `GameView` instantiates local
Controllers feeding `apply()` — same behavior, and it proves the sink
interface before the server exists.

## 8. Client changes (`takclient`)

- **Server browser** (new screen or `--server <addr>` entry): game list with
  name, map, players/capacity, lobby/running state, lock icon; create-game
  dialog (name, password, map picker showing start-position capacity,
  options).
- **Lobby screen:** slot rows (name/AI/closed, faction, color swatch, team
  number, ready check, ping), chat, host controls (add AI, kick, close slot,
  start). Players edit their own row; host edits any.
- **In-game:** allied units render through shared fog and show on the minimap;
  score/status panel grows to 8 players with team grouping; net status HUD
  (tick, ping, **behind-by indicator**, paused-by, the disconnect wait's
  `WAITING FOR <name> 27` countdowns and the own `RECONNECTING` line); chat
  overlay.
- The `--host`/`--join` peer mode is removed once the server path works
  (`net::Session` retired); `--server`, `--name` added. LAN play = run
  `takserver` on any machine on the LAN.

## 9. Maps, capacity, colors (retail facts)

- Capacity comes from counting `StartPosN` specials in the map's `.ota` — not
  `numplayers`, which is list-valued ("2, 4, 6, 8") or empty. The lobby caps
  human+AI slots at that count.
- Retail ships 55 skirmish maps: 6 support 2 starts, 9×3, 22×4, 4×5, 5×6, and
  **9 maps support the full 8** (e.g. Angvir's Maze, Black Heart Jungle,
  Sewers of Elam). The 8-player cap is chosen to match this retail ceiling —
  no shipped map has more than 8 starts, so no synthetic spawn-point
  fallback is needed and every player count has real maps.
- 10 colors confirmed in the retail art (10-frame GAF color sequences
  throughout; the engine already samples `playerColors_[10]`) — 8 players
  picking among 10 colors means uniqueness never runs out of options.

## 10. Retail parity backlog (post-v1)

The retail binary confirms these existed; all are natural extensions of this
design and none block v1:

- **Spectators** ("Watching" lobby checkbox, host-deniable) — a watcher is a
  bundle subscriber with no slot. The host-deniable part landed in protocol 258
  as the room's ALLOW SPECTATORS rule (see the protocol history); per-team fog
  toggling for watchers is still open.
- **In-lobby map transfer:** implemented in protocol 193; see [map transfer](map-transfer.md).
- **Alliance economy:** `AutoShareMana` with limit/percentage, unit gifting
  (`ShareUnits`).
- **Mid-game diplomacy:** requires mutable `Player::team` as a sequenced
  bundle Event *and* `team` entering `stateHash` (reserved in the Event enum
  now precisely so the wire format doesn't break later).
- **God-game chance:** retail rolled a 10% chance a multiplayer game is
  god-enabled (`gods.tdf GameChance=0.1`). Ours deliberately does not: gods do
  not appear in multiplayer (see `src/sim/matchsetup.cpp`).
- **Spectator snapshots:** referee-written World snapshots to cut a late
  spectator's catch-up in long games (the log header/format anticipates them).
  Seated players no longer rebuild from the log (§4 disconnect policy).
- Host migration is a non-issue by construction (the server owns the game; the
  "host" is a player with lobby permissions — pre-start they pass to the next
  human, in-game they matter only for pause/kick powers).

## 11. Milestones

Each lands independently, keeps single-player green, and is verifiable.

- **M0 — Command-path hygiene.** `Cmd::Destroy`; disable stress spawns in net
  games; enforce the Hello version check in the existing session (small,
  immediate). *Verify: 2-player lockstep still hash-clean.*
- **M1 — Sim foundation.** 8 players, bounds guards, team layer + `allied()`,
  shared team vision, sim-side team win/defeat, N-player spawn, 10 colors,
  extended hash (incl. projectiles + order targets), determinism flags
  (`-ffp-contract=off`, no fast-math) pinned in CMake. *Verify: local
  4-player FFA (3 local AIs) and a 2v2 with shared fog, via new `--players`
  dev flags.*
- **M2 — AI extraction.** `src/ai/` Controller emitting commands via sink into
  `apply()`; sim-tick cadence; seeded RNG; delete GameView AI. *Verify: fixed
  seed ⇒ bit-identical `stateHash` sequence across two runs; behavior parity
  with pre-extraction AI.*
- **M3 — Server + protocol, humans only.** `takserver` lobby (create/list/
  join/password/slots/chat/kick/start), sequencer, **TickBundle with the event
  section and the log header from day one**, referee sim + suspicion rule,
  hash ring, server hardening (§5), client browser + lobby screens; retire
  `--host`/`--join`. *Known gap until M5: a mid-game disconnect ends that
  player's game (no rejoin yet).* *Verify: 3 humans on LAN, full game, hashes
  clean; password/full/running join rejections; malformed-frame fuzz doesn't
  kill the server.*
- **M4 — Server-hosted AI.** Controllers in the GameRoom; mixed games up to 8
  slots. *Verify: 1 human + 7 AIs through the server; per-room CPU measured
  (grounds the §4 scaling claim); 8-start map cap enforced.*
- **M5 — Durability.** Reconnect via log replay + rotating resume tokens, drop
  auto-pause with budgets, grace-expiry **Forfeit events**, keepalive + ping,
  clean `Bye`, replay-to-disk, replay-throughput guard ("machine too slow").
  *Verify: kill -9 a client mid-game, rejoin, finish hash-clean; measure
  replay time vs. game length (decides the snapshot backlog item's priority);
  forfeit fires as a logged event and replays identically.* (Protocol 289
  replaced the hold, the budgets and the log-replay rejoin with the 30 s
  disconnect wait and the AI takeover, §4; tests `server_disconnect` and
  `disconnect_client`.)
- **M6 — Polish.** In-game team/status UI at 8 players, chat overlay, det-math
  shim (`sim/detmath`) for cross-build play, spectator/map-transfer
  groundwork, docs + server deployment notes (systemd unit, TLS proxy
  example).

## 12. Testing strategy

- **Headless client mode** (`takclient --headless-net`) so CI can run N scripted
  clients + server on one machine and assert every hash matches to game end.
- **Replay determinism:** every CI game's log re-run must reproduce the final
  hash — this also guards single-player AI determinism.
- **Fault injection:** socket kill, delayed delivery, malformed/oversized
  frames, command floods, and a deliberately desynced client (mutated unit
  hp) must produce: referee flags exactly that client, reconnect restores it,
  game completes. A deliberately desynced *referee* (test hook) must trigger
  the suspicion rule, not a mass drop.
- The existing `--shot`/`--time` smoke harness keeps covering the offline
  path.

### Per-game simulation isolation

Each running room lazily starts its own persistent simulation worker. The main
network thread freezes commands and events for one tick, then polls for its
completion without waiting for other rooms. There is at most one tick in flight
per room. Completed bundles, replay history, checksums, and campaign results are
published by the network thread; workers never touch sockets or shared room
metadata. Command ordering and the wire protocol are unchanged.

The running-room limit bounds the number of simulation workers. When multiple
games run, each simulation uses serial internal jobs to avoid nested worker
oversubscription. A slow tick no longer holds up the network loop or another
room's ticks, although games still compete for CPU and memory bandwidth. Map
startup and replay file writes remain synchronous work on the main thread.

The `room_worker` regression checks independent progress with a blocked worker,
serial/worker/client checksum equivalence, replay-budget rejection, and worker
exception recovery. It runs without networking or retail assets.

### Per-room game mods

Protocol 213 added the pack transfer messages alongside map transfer; they are
now ModOffer/ModRequest/ModChunk/ModReady/ModError (same ids, same order; the
offer label is `"mods"`). The host selects named immediate subfolders of
`mods/game/`; files directly in `mods/` or `mods/game/` are excluded. Selections
are merged in alphabetical order into a bounded, SHA-256-addressed package (no
cosmetic file ever goes in it). Server and peers verify it before room
readiness. Each game-mods room owns its own VFS and registries, leaving other
rooms unchanged. The server takes a package only from the host of a skirmish
room with game mods on: an offer or chunk from a guest or in a campaign or
Crusades room, and an offer while game mods are off, are answered with ModError. Replay
format 10 records the shared package digest (`ReplayHeader::modDigest`, with the
`gameMods` byte where `overridePolicy` was) and loads it from `ModCache/`.
The package also carries a pack's `ai/takengine.txt` (loose in the pack's `ai/`
folder or under `ai/` in an archive in it), and a map package carries it when
the map's own archive (its `.kmp`) has it (`maps::build`, checked on decode). The
server reads the room's one file, from the game-mod package, else from the map
package, and each of its AI levels 0-5 applies it over that level's own built-in
profile table (`src/ai/builtinprofile.h`). The old per-level files `ai/d.txt` ...
`ai/ad.txt` are not read. A root `takengine.txt` is not read, cosmetic
packs can never supply `ai/` files, and the base install's `ai/` folder is never
consulted for these names. Classic reads the map's `ai/<aiprofile>.txt` /
`ai/default.txt` with the same rule for `ai/` files: game mods, else the map
package, else the base data (`ai::classicProfileScript` with the room's layers);
other map content keeps the view's map-over-mod layering.

### Match pathfinding selection

The creation-time choice is `Retail` (the default) or experimental `Legion`.
Protocol 220 introduced the selection (then Retail or Flowfield); protocols
225-227 added Cooperative and Retail+, and 228 added Legion. Protocol 231
removed Retail+, Flowfield and Cooperative (2026-10-06, by user decision; they
remain in git history). Protocols 232-236 carry later Legion and Retail
behavior changes; 0.7.25 shipped protocol 236. Protocol 237 adds grouped flyer
movement (retail's VTOL_Move group checks in Retail; Legion flyers holding
stations over a mixed formation); 0.7.26 shipped protocol 237. Protocol 238
changes Legion internals for large battles (clustered static-map repair and
held units resting between updates); 0.7.27 shipped protocol 238. Protocols
239-248 carry the Legion plan's workstreams W0-W9 (shared goal fields, one
convoy per click, upkeep on demand, reach rings, flyer stations for squads,
paused first builds, crossing-group corridors, the passage gate and head-on
meets); 239 also carries whole-file override packages and nearest-first
lodestone areas. Protocol 249 adds client progress acks and ping-scaled flow
control, 250 the original game's speed levels (0.1x-2x), 251 the original's
blocked-site placement retry, 252 the fire-at-will weapon sweep with
deterministic per-player vision, AI weapon switching and idle retaliation, and
253 the sweep's `shootme` test; 0.7.28 shipped protocol 253. Protocol 254 moves
Legion flyer stations in squads that mix ground units and flyers over the front
of the ground (stations, positions and the flyer order point; Legion only), and
255 admits a factory's ground output only by the footprint-strict mobile
placement test, with a hashed retry delay when the site is refused, and 256
lets an air transport whose unload empties its orders stand by (and land) at
the drop point instead of flying on along the unload's step-out course; 0.8.0
and 0.8.1 ship protocol 256. Protocol 257 ends Legion's walk-in-place livelock
(Legion only): the pinwheel no longer takes a lone tree or rock for a wall end,
a blocked side-step does not step straight back, and a body without net
headway stands in its scripts and is held; 0.8.2 ships protocol 257. Protocols
258 to 260 change only the lobby (ALLOW SPECTATORS, MAP SCENARIO, START
LOCATIONS: BY SLOT), 261 ports retail healtime self-repair, 262 and 263 air
combat (see below), 264 carries the 2026-10-14 Legion review (Legion only; see
the replay notes below), 265 the 2026-10-15 retail path-search fidelity fixes
(all modes), and 266 entity pools sized from retail's maxunits that grow as
needed (all modes); 0.8.3 ships protocol 266. Protocols 267 to 272 carry the
Legion 10,000 program (Legion only), 273 shares short mana across construction
(all modes), 274 lets a lodestone upgrade finish under a short share, 275 adds
the Absurdly Defensive AI level, 276 voice chat, and 277 keeps a Legion
formation's flyers over the front while its ground waits to register (Legion
only); 0.8.4 ships protocol 277. Protocol 278 adds random-map recipe version 9
(scattered neutral mana; all modes); 0.8.5 ships protocol 278. Protocol 279 retunes Legion movement at about 100
bodies: passage admission, departures through arrived crowds and attack rings
(Legion only). Protocol 280 is the second round at about 100 bodies: sub-cell
presses and ring bends, passage lanes through wide gaps and flow-around at travel
speed (Legion only). Protocol 281 makes attackers walk to a clear shot when their
shots keep stopping on terrain or scenery they cannot destroy (all modes);
0.8.6 ships protocol 281. Protocol 282 is the third Legion round at about 100
bodies: rings round still targets, passage greens that scale with the crossing
with small groups queueing, and corridor-mouth funnel claims (Legion only).
Protocol 283 is the fourth: lane order kept through turns, deeper rings along
walls, fight and guard members round still targets, and room at a corner's
merge (Legion only). Protocol 284 adds the lobby seat move (`MoveSeat`) with an
unchanged simulation; 0.8.7 ships protocol 284. Protocol 285 is the fifth Legion
round at about 100 bodies: attackers press into a wall's shadow, fight-move columns
ring a still target, and a body on the reach edge counts as in reach (Legion only).
Protocol 286 is the sixth, for gatehouse corridors: an attacker that would plug a
narrow approach walks on past the shot, and deep rings fill back to front (Legion only).
Protocol 287 replaces override packs with mods: the host-only game-mods flag replaces the
override tier, and cosmetic mods are never sent; the simulation is 286's.
Protocol 288 adds power codes: the POWER CODES room option and `Cmd::PowerCode`; with
the option off the simulation is 287's; 0.8.8 ships protocol 288, and so does 0.8.9
(client-only changes; it plays with 0.8.8 and its replays).
Protocol 289 replaces the disconnect hold and the log-replay rejoin with the 30 s
disconnect wait and the AI takeover (§4): `Waiting`, `Reseated`, `Rejoin` with its
`fromTick`, and `Event::AiTakeover`; the simulation is 288's until a takeover,
except that 289 also seats the campaign human at the mission's Player1 (world slot 0,
as retail does) and scales the target score's turn penalty by the shooter's terrain
turn rate (roads and water, as retail does). 0.8.10 ships protocol 289; it plays
with neither 0.8.9 nor 0.8.8, nor their replays.
Protocol 290 adds AI level 6,
Classic, shown to players as RETAIL (DEFAULT in 0.8.11; `SlotInfo.aiLevel` 6): a server-side port of the retail AI
(`ai::ClassicBrain`, [classic-ai-plan.md](classic-ai-plan.md)). Its seat gets
exactly a Normal seat's match config, so the simulation is 289's; a 289 peer would
clamp level 6 to Normal, hence the bump. A campaign mission's strategic computer
players are always Classic (`CreateGame` carries no AI choice). 289 replays are refused.
0.8.11 ships protocol 290; it plays with neither 0.8.10 nor any earlier version,
nor their replays.
Game options
and every lobby/start/spectate snapshot carry the choice. The server validates the
value, includes it in the authoritative rules digest, and retains it when a
host sends later option updates. Both clients and the referee apply that choice
through shared match setup. Campaign missions and Crusades battle rooms remain
Retail. The lobby reports the selected mode without changing it.
Protocol 291 changes which AI files a map package carries, not the messages: a
map's `ai/<aiprofile>.txt`, `ai/default.txt` and level files now come from the
map's own archive (`Vfs::readFromMapArchive`). Before, the package took only the
copy that won the `Maps/` mount, so of two installed `.kmp` files carrying the same
file name (the Cartographer stamps every entry with date 0) one map silently sent
none, and a ticked game mod's copy kept the map's out of its package. Hosts and
clients both build packages, so a 290 peer could compute a different digest for
such a map; the simulation is 290's. 290 replays are refused.
Protocol 292 changes the simulation: a Shift-queued patrol of an armed or
builder flyer now closes its loop when the leg starts (`World::openFlightPatrol`,
`Order::patrolOpen`), adding the return point where the flyer then stands, as
ground units and unarmed flyers already did. Under 291 such a flyer (drakes, say)
flew "move A, Shift-patrol B" to B and hovered there until its next order. 291
replays are refused.
Protocol 293 changes map and game-mod packages, not the simulation: our six AI
levels read one file, `ai/takengine.txt`, each applying it over its own built-in
table, instead of `ai/d.txt` ... `ai/ad.txt`. A map's package carries
`ai/takengine.txt` and no longer the per-level files (a package naming one is
refused), so a 292 peer could compute a different digest for a map that carries
them. 292 replays are refused.

Protocol 258 adds the room's ALLOW SPECTATORS rule and changes only the lobby
wire: `GameOptions.allowSpectators` (u8, default 1) follows the pathfinder byte
in `CreateGame`, `SetGameOptions`, `LobbyState` and `GameStarting`, and each
`GameList` entry ends with it. With the rule off the server answers every
`Spectate` with a failed `JoinResult` ("spectators are not allowed in this
game"). `Spectate` is the only way into a room without a slot, so this one check
covers the browser's watch request, a seated player who left and asks to watch,
and a dropped watcher reconnecting (watchers hold no resume token, and `Rejoin`
seats only seated players). The host changes the rule in the lobby. In a running
game `SetGameOptions` adopts only speed, so the rule is fixed at start and an
admitted watcher is never stranded. The only lobby spectator is a host who
created the room to watch it. The rule governs who else may watch, so that host
is never removed. Crusades battle rooms carry 0. The Crusades rules digest still
hashes 257's option bytes (`putRules`), so battles issued before the upgrade
still verify. Because the simulation was unchanged, protocol-257 replays played
under 258 (`supportedReplayProtocol`; 261 ended that). The client keeps a refusal's reason for the
browser (`MpClient::joinError`). It also clears the pending spectate on any
refusal; before this, a refused watch turned the next ordinary join's start into
a spectator start.

Protocol 259 adds the room's MAP SCENARIO option: `GameOptions.mapScenario` (u8,
default 0) follows `allowSpectators` in `CreateGame`, `SetGameOptions`,
`LobbyState` and `GameStarting`; `GameList` is unchanged. With it on, shared
match setup plays the map's own scenario -- the `.crt` placements and rules and
the Use Only `.tdf` -- as for a Cartographer Test Map copy, except that a Use
Only entry no unit type answers to is left out rather than refused (maps made
for the original game name such units; NeverGlory names 44). Setup runs the
scenario when the map's `.ota` carries the Test Map opt-in (`[TAKPlaytest]
authoredscenario=1`) or when the option is on and the `.ota` declares
`hasscenario`. The opt-in route is unchanged, so Test Map and `--play-map`
games, and their recordings, behave as before. The option is creation-only: the
server keeps the created value through later `SetGameOptions`, because players
choose seats, sides and READY for the game it makes, and the creating client
sized the room's open seats for it (a scenario can need seats past the map's
start positions). The server clears it for campaign missions, generated maps,
stress tests and benchmarks (at creation, and when a lobby update turns a stress
test or benchmark on), and forces Random Start Locations off with it. At START
it reads the verified package's own files (`maps::scenarioInfo` over the
package, never the host's claim): it refuses a room whose map has no scenario
content ("this map has no scenario to play"), refuses while a seat the scenario
needs is empty, with setupMatch's own message ("Seat player N in the lobby to
play this scenario"), and refuses a package that leaves out a scenario file
setupMatch will read (below). A start refusal reaches the host as `MapError` and
the client shows it in the room (`MpClient::startError`) without recording it as
a map or connection error; before this, a setup refusal at START was invisible,
because the room shows its map status only while maps are being verified. The
seats a scenario needs are its placement owners 0-7 and every Player N rule bank
with a group that has a condition or an action; a bank holding only empty groups
no longer demands a seat. That fix only lets a refused setup start, so with the
option off the simulation is 258's and protocol-257 and 258 replays played under 259.
The option is sim-affecting and is recorded in replay format 13. It is outside
the Crusades rules digest (`putRules` keeps 257's bytes); Crusades battle rooms
carry 0, and the Crusades replay archive refuses a recording that has it on. The
map package carries every map's `.ota`, `.crt` and `.tdf`, and its digest covers
the files it contains. A package may leave a companion out, though, and
setupMatch would then read each machine's own copy (from `maps.hpi`, or from a
mod pack, since `maps/*.tdf` is not gameplay data), which no digest covers.
So at START the server refuses a room whose setup will read a scenario file the
package lacks: the `.crt` of a declared scenario, or the named Use Only list
("the map package leaves out its Use Only list"). A damaged or misnamed file
inside the package is setupMatch's to refuse, with its own message. With that,
every peer and the referee play the same scenario files.

Protocol 260 adds START LOCATIONS: BY SLOT. `GameOptions.randomStarts` keeps its
byte and place and gains the value 2 (`net::kStartsBySlot`): slot N takes the
map's Nth start whichever seats are empty, and a seated slot past the map's last
start takes the lowest start no seated slot claims by its own number, then a
spot near the map centre (`sim::startSpotsFor`). Any other nonzero value still
reads as RANDOM (`normalizeStartMode`), which is why 259 needed the bump: a 259
peer would read 2 as RANDOM and desync. FIXED and RANDOM set up exactly as in
259, and the replay header's `randomStarts` byte simply carries 2 as well, so
the replay format is unchanged and protocol 257-259 recordings played under 260. A
MAP SCENARIO room is still forced to FIXED.

Replay format 11 records the pathfinder. Playback requires a protocol that
`supportedReplayProtocol` accepts. While 258-260 were current that included
257 onward (their simulation is the same); protocol 261 (retail healtime
self-repair) changed the simulation, so playback again requires the current
protocol, and protocol 262 changed it again: a flyer held still in range of its
attack target zeroes its hashed `flightVelocity` and speed, so shooters stop
leading it by the speed it flew in at (see "Shooting at flyers" in
`retail-engine.md`). Protocol 263 changes air combat, and protocol 262
recordings no longer play: World's other flyer holds stop the velocity and
speed too; impacts follow retail's dispatch, a weapon with areaofeffect 16 or
less that struck a unit damaging it alone and every other impact damaging units
by 3D distance to the blast; each weapon slot's facing request points at its led
SweetSpot; and a flyer without hoverattack flies attack runs at a target that
cannot fly instead of holding still in range (see "World's other flyer holds",
"Impact damage", "The facing request" and "Attack runs" in `retail-engine.md`).
Its `--mpai` Inner Circle seed 1 hashes: Retail 300 s `bf6f1ac3081f9ee6`, 60 s
`8db23af1b0260ba1`; Legion 300 s `7def7a0fa5dedfc6`, 60 s `5bc7e8001c06611f`
(the 60 s ones are 262's: no flyer fights in the first minute).
Normal's larger builder total (up to the Monarch and four, see
`ai-expansion-2026-09-29.md`) moves the 300 s ones, protocol unchanged: Retail
`76f7e976cd6a00c9`, Legion `99e2f137cde8d769`; the 60 s ones stand.
Protocol 264 changes Legion movement and only Legion (every Retail record of the
scenario set is unchanged), and protocol 263 recordings no longer play: field
potentials are 32-bit, so a long connected route is walked instead of standing
trapped; a body's progress is measured in one measure at a time, so its stall
clock runs with its walk; the settle rule reads a body of another footprint
where it stands; a formation takes the slots past a narrow entrance in the order
its bodies come through it; a formation member keeps its lateral place through a
short passage; and one formation slot-search budget a tick is shared by every
point, round robin (see the 2026-10-14 review in `legion-pathfinding.md`). The
`--mpai` Inner Circle seed 1 hashes stay as above in both modes: Retail 300 s
`76f7e976cd6a00c9`, 60 s `8db23af1b0260ba1`; Legion 300 s `99e2f137cde8d769`,
60 s `5bc7e8001c06611f`.
Protocol 265 changes path search in every mode, and protocol 264 recordings no
longer play: each player's path requests are admitted from a production entity
pool, scanned slot by slot with its holes as retail's scheduler does (the CRT
stream, each pool's size and every unit's slot are hashed); where ground
footprints overlap, the cell keeps its live holder instead of going to the last
unit in vector order; and re-requesting a pending path for the same controller
and goal leaves it alone however far the unit has moved (see the 2026-10-15
audits in `pathfinding-port.md`). The `--mpai` Inner Circle seed 1 hashes:
Retail 300 s `9b48eb78e62650cb`, 60 s `42a9fc16c5df77b8`; Legion 300 s
`96fa40176b9881d5`, 60 s `24b3ccbd8e6bb61e`.
Protocol 266 sizes those pools as retail does, and protocol 265 recordings no
longer play: a player's pool starts at retail's maxunits for the unit cap (the
cap itself from 20 to 500, 500 for the 1,000 and 2,000 lobby caps, 250
uncapped) instead of the cap, and gains a slot only when a unit finds it full,
so the default 2,000 cap scans 500 slots a player, not 2,000 (hashed pool
sizes; see the 2026-10-15 grow-as-needed audit in `pathfinding-port.md`). The
`--mpai` Inner Circle seed 1 hashes: Retail 300 s `7535cb08ee787b75`, 60 s
`2c39430636a3dbda`; Legion 300 s `c707e1a8063bc0c7`, 60 s `532539f51d70840b`.
Protocol 267 adds explicit selection orders (`Cmd::Order` in the tick bundles,
the OrderBegin/OrderMembers/OrderCommit/OrderAbort uplink transaction; Legion
clients only, so Retail is bit-identical), and protocol 266 recordings no longer
play. An AI-only game issues no orders, so the `--mpai` hashes above are
unchanged under 267 (both modes, 60 s and 300 s).
Protocols 268 to 272 carry stages 4 to 8 of the Legion 10,000 program (budgeted
navigation scheduling, route queries, hierarchical routes, static changes apart
from traffic, chokepoints, subgroups and arrivals; see `legion-pathfinding.md`
and `legion-10000-report.md`). Each changes Legion movement and only Legion, and
each refuses the previous protocol's recordings; Retail stays bit-identical to
266. The program's merge left the `--mpai` Legion hashes at 300 s
`47b705a730bbc22f`, 60 s `f75b2a11410f43b4`, and Retail's as under 266.
Protocol 273 shares short mana across construction in every mode: production,
mobile construction and repair each do the player's allocation (the stored mana
over last tick's demand) of a full tick of work, as retail does, instead of each
paying in full or doing nothing in unit order. Per-player demand and
per-producer fractional progress are hashed, and 272 recordings no longer play.
The `--mpai` Inner Circle seed 1 hashes: Retail 300 s `a3d953803a064191`, 60 s
`b353410003e98dd5`; Legion 300 s `ce5c4ba7562e53b9`, 60 s `0825aeb9e8139203`.
Protocol 274 scales a lodestone upgrade's tick of work by that allocation before
clamping it to what is left, so a short share no longer leaves the upgrade one
rounding step from done; 273 recordings no longer play, and the `--mpai` hashes
are unchanged. Protocol 275 adds AI level 5, Absurdly Defensive
(`SlotInfo.aiLevel`): every peer derives its double income and defensive stance
from the level, so an older peer would read 5 as Normal and desync; the
simulation is otherwise unchanged and 274 recordings no longer play. Protocol
276 adds voice chat (`VoiceListen`, `VoiceUp`, `VoiceDown`; see
[Voice chat](#voice-chat-protocol-276)), relayed beside the game and never in a
tick bundle or replay; the simulation is unchanged, but a 275 peer would not
know the messages, and 275 recordings no longer play. The `--mpai` hashes stand
as under 273. Protocol 277 (Legion only) counts a unit waiting in the Legion
registration queue as delayed, not stalled, so a formation's flyers keep their
stations over the front while the ground waits for its first navigation plane
(on big maps since stage 4) instead of flying straight to the goal; Retail is
bit-identical, 276 recordings no longer play, and the `--mpai` hashes stand as
under 273 (the AI never forms formations). Protocol 278 adds generator recipe
version 9, whose one extra byte in the `~gen1~` map id carries the Scattered
Mana level (`Params.manaScatter`); a 277 peer would refuse a v9 id, so 277
recordings no longer play. Version-8 ids still decode as None and regenerate
byte-identically, and authored maps are untouched, so the simulation is
otherwise unchanged. Protocol 279 (Legion only) admits one stream per narrow
passage however many opening records it spans, lets head-on meets narrow near
the goals, has allied settled bodies step aside for a departing order and
keeps attack rings filled round their target; protocol 280 (Legion only) adds
sub-cell presses into reach, ring bends round other commands' rings, passage
lanes through gaps up to 32 origins and flow-around shuffles at travel speed.
Retail is bit-identical under both, and 278 and 279 recordings no longer play.
Protocol 281 (all modes) watches where shots land: three in a row on terrain,
water or scenery the weapon cannot destroy write the target off for auto
weapons and send an attacking ground mover off Hold Position to a nearby spot
with a clear shot (`World::noteShotImpact`, `repositionForShot`); that state
is hashed only once a blocked impact occurs, so the `--mpai` hashes stand as
under 278 (60 s Retail `b353410003e98dd5`, Legion `0825aeb9e8139203`), but 280
recordings no longer play.
Protocol 282 (Legion only) is the third round at about 100 bodies: attackers round
a still target stop creeping, passage greens scale with the crossing and small
groups queue for their turn, and single-file corridor mouths hand out claims; Retail
is bit-identical and the 60 s `--mpai` hashes are unchanged, but 281 recordings no
longer play.
Protocol 283 (Legion only) is the fourth round at about 100 bodies: a formation keeps
its lanes through turns (the turn ribbon and lane exchange), rings along walls stand
deeper and walk on through a still target's shadow, fight and guard members round a
still target stop creeping and flow round at travel speed, and members passing a
starved queue head at a corner's merge move over a lane; Retail is bit-identical
and the `--mpai` hashes are unchanged (60 s and 300 s), but 282 recordings no longer play.
Protocol 284 adds the lobby seat move (`MoveSeat`); the sim is 283's, but 283
recordings no longer play.
Protocol 285 (Legion only) is the fifth round at about 100 bodies: a body held just
outside reach of a ring that walls hide in part presses into the shadow and, once it
has pressed, holds there; an attack group of eight or more that got no ring at its
first field (a fight-move column acquiring one body at a time) plans one late; a
press may end exactly on the reach edge, where combat fires (a melee weapon keeps its
contact test); and a ring's stop origin is the edge point's own origin. Retail is
bit-identical and the `--mpai` hashes are unchanged (60 s and 300 s, Retail and
Legion), but 284 recordings no longer play.
Protocol 286 (Legion only) is the sixth round at about 100 bodies, for attack rings
reached through a narrow approach (a gatehouse corridor): a ring member that combat
would brake where it plugs the approach, with the queue behind it out of reach, walks
on first to the farthest ground with the shot, only onward and only past a queue that
comes up to it; and a ring that a narrow approach feeds hands out its spots deepest
first by walk, so the approach fills from its far end as Retail's does. Retail is
bit-identical and the `--mpai` hashes are unchanged (60 s and 300 s, Retail and
Legion), but 285 recordings no longer play.
Protocol 287 replaces override packs with mods. `GameOptions` byte 3 is now
`gameMods` (0 or 1, default 0; was the 0/1/2 override tier): only the host selects
game packs, the server refuses them from anyone else and forces the flag off in
campaigns and Crusades battles, and the `Override*` messages are renamed `Mod*`
with the same ids and the offer label `"mods"`. The replay header keeps the same
byte as `gameMods` and the digest as `modDigest`. Cosmetic mods (selected pack
folders and the always-on `mods/cosmetic/*.hpi` archives) stay local to each player
and are never sent. The simulation is 286's, but 286 recordings no longer play.
Protocol 288 adds power codes. `GameOptions` gains a `powerCodes` byte after
`mapScenario` (0 or 1, default 0, outside the Crusades rules digest): set when the
room is created and kept by later option updates, and forced off by the server in
campaigns and Crusades battles. `Cmd::PowerCode` (28, after `Order`) carries one of
the seven simulation codes (`targetId` the code, `queue` the +LOS argument,
`unitId` the +ManaMe unit); the server drops it while the room's option is off and
treats an unknown code or argument as a forgery. Strict replay reads accept it from
protocol 288. Replay format 14 appends the POWER CODES byte after the MAP SCENARIO
byte: bit 0 the option, bit 1 the map started revealed (FOG OF WAR Explored or Full
Vision, which +Mapping starts from); a format-13 file reads it as 0. With power codes
off the simulation and the `--mpai` hashes are 287's, but 287 recordings no longer
play.
Older recordings require their original engine.
Campaign playback forces Retail even if a header supplies another mode.
Replay format 13 adds the room's MAP SCENARIO byte after the slot table (0 when
reading an older file). Saved
`gameCreate.pathfindingMode` values and the wire byte keep the stable identities
`0=Retail` and `4=Legion`; `1`, `2` and `3` (the removed Flowfield, Cooperative
and Retail+) are invalid. Missing, unknown or removed saved values default to
Retail, while unknown network/replay mode bytes are rejected. The creation
control switches between Retail and Legion. Debug-only `TAK_LEGION=1` selects
Legion in the headless multiplayer test harness. `crowdbench` accepts `--mode
retail` or `legion` for direct comparisons. Legion's groups and per-unit state
are part of the simulation checksum only in mode 4. Replay format 11's byte
layout is unchanged; the protocol bump prevents silently replaying old
simulation rules.
For `tools/desync-hunt-remote.sh`, `--minutes` limits simulated match time.
`TAK_WALL_TIMEOUT=3600` grants each client up to one real hour for a dense
stress run; the default remains simulated seconds plus 300.
The same tick, completion and hash checks apply. `--only` retains each scenario's
full-table seed so targeted retries reproduce its original map/start seed.

Protocol 221 appends `BuildManaArea` to the command enum. It carries a builder,
lodestone type, queue flag, and two rectangle corners in 43 bytes, like area
reclaim; existing commands retain their 35-byte layout. Every peer executes one
persistent area mission, exploring deposits before trying normal clearing,
placement, construction, and upgrades. Its cursor and exploration stage are
hashed, with no per-deposit command list or render-owned decisions. The server
validates coordinates and command ownership; build-menu restrictions apply.

New matches also enable automatic nearby repairs during builder patrols.
Replay format 11 is unchanged. Defensive AI interception is server-local policy;
its ordinary movement commands are sequenced and recorded as usual.

(History: protocols 222-227 below concern the since-removed Flowfield,
Cooperative and Retail+ modes.)

Protocol 222 retains the same wire layout and replay format, while fixing
Flowfield arrivals around parked formations at a shared rally point. The
additional allowance remains bounded by group population, local distance and
full-footprint collision checks. Retail pathfinding is unchanged.

Protocol 223 adds footprint-sized destination areas, reserved final approaches,
equal-cost lane preservation, and strict Flowfield formation pacing. The wire
layout and replay format remain unchanged. Local traffic reservations, work
admission, and validation budgets participate in deterministic simulation state.
All live peers and the server must use the same protocol. Older replay protocols are refused rather
than running compatibility variants of the simulation.

Protocol 224 retains the wire layout and replay format while fixing Flowfield
turning, local crowd escapes, and arrival avoidance around terrain obstacles.
It requires matching clients and server; protocol-223 replays require the engine
that recorded them. Retail pathfinding behavior is unchanged.

Protocol 225 first added Cooperative selection without changing the existing mode
values, command layout, or replay format 11. Retail and Flowfield remain separate
choices with their existing behavior. This was superseded by protocols 226 and
227. Version 0.8.11 (peers and server) requires protocol 290; 0.8.10 required 289, 0.8.9 and 0.8.8 288, 0.8.7 284, 0.8.6 281, 0.8.5 278, 0.8.4 277, 0.8.3 266,
0.8.2 257, 0.8.0 and 0.8.1 256. The replay loader rejects every other simulation protocol.
Campaign missions and Crusades battle rooms remain fixed to Retail.
