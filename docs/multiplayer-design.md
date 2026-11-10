# Multiplayer Design: Client–Server, 8 Players, Teams

Status: **implemented** (v1 scope agreed 2026-09-04; milestones M0-M6 all
shipped), on the deterministic-sim substrate (`src/net/lockstep.h` holds the
command format). Sections 1-12 are the original design, kept as written apart
from short notes where the engine differs; the later sections describe the
current engine.
Where the engine differs from the v1 design: single-player runs a private local
`takserver` (`--local --no-auth`) instead of in-process AI
([sp-mp-equivalence.md](sp-mp-equivalence.md)); the server requires `--data`,
runs the referee for every game and hosts the AI; accounts with sign-in are the
default and public listeners use native TLS ([public-server.md](public-server.md));
allied mana sharing and unit gifting exist ([unit-gifting.md](unit-gifting.md));
the headless test client is `TAK_HEADLESS=1 takclient game ... --mpai`
(debug builds).

## 1. Goals

- **Client–server topology.** All clients make one outbound TCP connection to a
  central server (`takserver`) reachable on the LAN or internet. Everything —
  lobby, game traffic — is proxied through it. No NAT traversal or port
  forwarding on any player's side.
- **Up to 8 players per game**, any mix of humans and AIs.
- **Up to 8 teams.** Default: every player/AI on its own team (free-for-all).
  Players on the same team share vision (fog of war); radar marks the minimap
  from a player's own units only, as in retail (`height-and-fog.md`).
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
  takes the seat over (§4).

The current implementation supports spectators and [verified map transfer](map-transfer.md).

Remaining non-goals for v1 (tracked in §10): allied mana sharing / unit gifting, mid-game diplomacy, persistent
accounts or ratings, wire encryption (deploy behind a TLS proxy — and on
untrusted networks that proxy is *not* optional, see resume tokens in §4).
*(Since shipped: mana sharing and unit gifting, persistent accounts with
SCRAM-style sign-in, and native TLS. Mid-game diplomacy and ratings remain
open.)*

## 2. What exists today (and what survives)

The engine already has the hard part: a **deterministic 30 Hz sim** where only
commands cross the wire, verified in 2-player lockstep with periodic 64-bit
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
  reserve each frame — `clamp(2 + max(kJit, kRtt, kFrame), 2, cap)` ticks
  (`tak::net::jitterTargetTicks`, `src/net/pacing.h`) — so a clean LAN sits at 2
  and a jittery WAN grows the cushion only as needed. The cap is
  `min(max(16, 16·speed), flowBufferLimit)`: never more than server flow control
  lets the client hold (`flowLeadTicks` less a Progress period, the round trip
  and the sim inbox), which would pin the servo at its floor. A bundle's
  lateness is measured against the *previous* poll and the jitter decays on
  wall time, so a client's own stall (loading, alt-tab, a menu) is not counted
  as network jitter (`ArrivalJitter`).
- **Servo playout** holds the reserve *at* that target instead of drifting: the
  sim clock runs slightly fast when the buffer is deep (shed latency) and slightly
  slow when it is shallow (rebuild the reserve) — `rate = 30·clamp(1 + 0.06·(buffered
  − target), 0.7, 1.3)` Hz. Equilibrium is exactly `buffered == target`, so added
  latency stays tight rather than accreting. A backlog deeper than target+60 (a
  rejoin replay, or a client that fell behind) is fast-forwarded, not paced.

Only game ticks are gated by this buffer; camera, zoom, and edge-scroll run every
render frame in `cameraFrame(dt)`, independent of the sim, so input feel is never
coupled to link latency.

*Test harness.* A synthetic link model (debug builds) injects receive delay on the client with
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
  the host's always-enabled `mods/game/always_enabled/<pack>` folders and its
  ticked `mods/game/<pack>` folders are packaged and transferred (see
  [Per-room game mods](#per-room-game-mods)); every player mounts that verified
  package at game start so its registry matches the referee's. Every host
  sends 1, single player and multiplayer alike (the room has no GAME MODS
  toggle); 0 comes only from a debug `--mods` launch and means no package
  and no game packs at all. Campaign missions are forced to 0.
- **Cosmetic mods are per player and never travel.** Each client lays on top of
  the room's package its own always-enabled `mods/cosmetic/always_enabled/<pack>`
  folders (every room) and its
  ticked `mods/cosmetic/<pack>` folders (every room but a campaign one), through the cosmetic keep-filter (`!affectsGameplay`, no `features/`, no
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
  mid-game desync. `takserver` refuses to host without `--data`, so every game
  has the referee's hash.

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
  host leaves or disconnects **pre-start** (a host watching its room as a
  spectator too), host powers pass to the next human in the lobby (the game
  dissolves if none remain; an unstarted room nobody is in is dissolved in any
  case). In a running game they belong to the host's **seat**: a host that
  reconnects is the host again under its new connection; when the seat leaves
  for good (a leave, or an AI takeover) they pass to the first seated player
  still connected and playing (else to one held for its reconnect, which takes
  them when it is back), or to nobody, and the host's pause ends with it.
  A LOCAL SERVER's (`--lan`) game is its host's: it closes for everyone when
  the host leaves, in the lobby or in the game.
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
- **Disconnect policy.** The rule: if a player effectively
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
    is not recorded and changes no sim state. Like the
    budget, the hold accounting is wall-clock time on the server alone.
  - *Coming back.* Talking again on the same connection inside the window (a
    frozen frame, a hitch, a Wi-Fi blip TCP rode out) ends that seat's wait. A
    client whose link actually dropped reconnects by itself: a new connection,
    the same login (the client keeps the login's SCRAM keys for the session;
    the password itself is gone; a session signed in by token spends its next
    sign-in token instead), then `Rejoin{game, resume token, fromTick}`,
    `fromTick` being the first tick whose bundle it never received. The server
    answers `Reseated{slot, rotated token, fromTick, logEnd}`, then what no
    bundle carries (`LobbyState`: the game speed and the host as they are now;
    `Pause` while the host has the game paused; `Waiting`), and streams the
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
  - *Leaving on purpose* is unchanged: `LeaveGame` (the game client sends it
    when the player quits a running game from the menu) is a `Leave` event at
    once -- the units go inert, no wait. A defeated player's departure never
    waits either.
  - *Spectators* catch up by rebuilding the world from tick 0, replaying the
    whole bundle log; seated players never do.
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
| `SetName` | display name | no accounts in v1 *(superseded: players sign in to an account by default, `--no-auth` serves without one)* |
| `AuthToken` / `AuthRevoke` | username, sign-in token | TLS only (`src/net/auth.h`). In place of `AuthBegin`: a password login's `AuthResult` carries a token, which **Game Select** spends to sign in with no password (the `AuthResult` carries the next one) and quitting revokes. The server holds per-account hashes in memory, and a login's line of tokens ends 12 hours after the password however often it rotates. A token used twice ends its line, except a resend within 30 seconds after a lost reply; a bad token counts in the login throttle and sends the player to the normal password prompt |
| `ListGames` / `GameList` | — / entries: id, name, map, players/capacity, state (lobby/running), passworded, uptime | running games listed but unjoinable |
| `CreateGame` | name, password, map id, mission, options, capacity, spectate, private | creator becomes host; an empty name is the server's `"<name>'s Game"`; a name past 32 printable-ASCII bytes or a password past 128 is answered with `JoinResult(0, why)`, and so is a server at its game limit (`--max-games`: "server game capacity reached"); a random map's capacity is its player count, read by the server (protocol 323) |
| `JoinGame` / `JoinResult` | game id, password? / ok, slot (0xFF = watching), why, room revision | error: full, bad password, running; every JoinResult carries the room's revision, so a READY sent before the first `LobbyState` counts. A refusal keeps the connection: the client stays in the game list with the reason. `JoinResult(ok)` is what puts a client in a room, always before that room's first `LobbyState` (see [Room scope](#room-scope-what-a-client-takes-and-when)) |
| `SlotUpdate` | slot #, {human/AI/closed}, faction, color, team, ready, AI level, room revision | players set their own; host sets any (host resolves conflicts, assigns AIs); only the server seats a human, so the host can neither change a seated slot's type nor make an empty one human -- such an edit (e.g. one sent before a seat move landed) is dropped. Only a seat's own player changes its READY, and a guest's READY on an older revision than the room's is stored as not ready (protocol 323) |
| `LobbyState` | `RoomState`: id, name, map, mission, options, host, maps ready, capacity, passworded, revision, change bits, map serial, the 8 slots | broadcast on every change |
| `SetGameOptions` | full `GameOptions` | host only; in the lobby the server adopts them (see [Room editing](#room-editing-protocol-323)) and rebroadcasts `LobbyState`; in-game only `speed` takes effect (if `speedUnlock`), pushed via `SpeedUpdate` |
| `SetRoomMap` | serial, map id, capacity, MAP SCENARIO, start mode | host only, lobby only, never a mission or Benchmark room; answered by the `LobbyState` carrying the serial, after a `RoomError` when refused (protocol 323) |
| `SetRoomInfo` | flags (1 name, 2 password), name, password | host only, lobby only; only the flagged fields apply; no player is unreadied (protocol 323) |
| `RoomError` | request (1 map, 2 info), serial, reason | server → host: a refusal it can act on, never fatal, always followed by a `LobbyState` (protocol 323) |
| `Chat` | text | lobby and in-game |
| `Kick` | slot # | host only |
| `MoveSeat` | slot # (0xFF = watch) | a seated player (the host too) takes an OPEN seat inside the map capacity, lobby only; not in a campaign mission room; side, colour, team and ready move with the player, the old seat opens with the target's colour and team; the server answers the mover with `JoinResult` (its new slot), then broadcasts `LobbyState`; the host may leave its seat to watch (0xFF) and sit down again (protocol 323); anything else is ignored (protocol 284) |
| `StartGame` | — | host only; server validates: every human ready, colors unique, players ≤ map start positions. A START the server will not run is answered to the host with `MapError` (room, reason), which the room shows as CANNOT START: the map has no scenario, a seat the scenario needs is empty, the match cannot be set up, the game mods cannot be loaded, or the server is at its running-game limit (`--max-running-games`: "server running-game capacity reached; retry later"). The room stays as it is and the host may press START again |
| `GameStarting` | final slot table, map id, options, per-AI RNG seeds, resume token | clients load and report `Loaded`; server starts when all humans are loaded; a `Loaded` timeout kicks that player back to the lobby and re-opens the slot (host decides: re-fill or start without) |

### Refusals and `Reject`

`Reject` ends the connection: the client shows DISCONNECTED with the reason, whether
or not the server also closes the socket. It is kept for what is fatal by design: a
protocol violation (a malformed handshake or login, an invalid pathfinder byte, a
random-map id this build cannot read), a refused sign-in step or a login rate limit,
a version or data mismatch (protocol, retail data at `Hello`, game data at `Loaded`,
a map or mission the server's data cannot referee), a benchmark, stress or campaign
request on a public server, a game the server stops (replay memory, a failed
simulation), and a LOCAL SERVER's game closing with its host.

A request a player may ordinarily make is refused with its own message and the
connection kept: `JoinResult(0, why)` for `CreateGame`, `JoinGame`, `Spectate` and
`Rejoin` (the server's game limit included), `MapError` to the host for a `StartGame`
(the running-game limit included), `RoomError` for the host's room edits.

### Room scope: what a client takes, and when

The server sends a room's messages only to the clients it has in that room, and it
puts a client there with a message of that client's own before any other:

| The client asked | The server sends, in order |
|---|---|
| `CreateGame`, `JoinGame` | `JoinResult(ok)`, then the room's first `LobbyState` (then the package offers) |
| `Spectate` | the room's `MapOffer` / `ModOffer`, `GameStarting` (slot 0xFF), `Waiting` while a countdown runs, then the bundle log |
| `Rejoin` | `Reseated`, then `LobbyState`, `Pause` while the host has the game paused, `Waiting`, then the missed bundles |
| `MoveSeat`, or the host's map moved its seat | `JoinResult(ok)` with the new seat, then `LobbyState` (already in the room) |

A host succession is a `LobbyState` to the clients already in the room, and no room
outlives its game, so nothing enters a room by `LobbyState` alone. `MpClient::takes`
holds the client to that: a room's messages (the table, `RoomError`, the map and
game-mod transfer, chat, voice, `GameStarting` and everything of a running game) are
acted on only from that first message until the client leaves, and are otherwise
dropped unread. A `LobbyState` never opens a room, and one for another room than the
client's is not its room's.

What the server sent before it took a client's `LeaveGame` still arrives after the
client has left: a table sent as a package verified, a seat the map just moved. It
used to put the client back in the room (one the server might already have
dissolved). `LeaveGame` has no answer, so `MpClient::leaveGame` sends a `Ping` right
behind it: the server answers a connection's messages in order, so every room
message before that `Ping`'s `Pong` was sent before the leave and is dropped, and
what follows the `Pong` answers what the client sent after it. The same call takes
back a request still on its way (CANCEL in the browser's windows, or NO ANSWER):
`LeaveGame` follows the request to the server, which leaves the room or watched game
it opened for it, and the request's answer -- a yes with its table, a no, a watched
game's `GameStarting` and bundles -- arrives before the `Pong` and is dropped, so it
is never taken for the answer to the next request. No wire format changed: old and
new builds of protocol 323 play together.

A watched game's stream is taken before its `GameStarting` is: that message waits in
`MpClient` until the packages it names have arrived and verified
(`MpClient::watchLoading`), while the bundle log, a `Pause`, a `Waiting` and chat
lines behind it are stored. `leaveGame` therefore also clears what a game's stream
left (the buffered bundles and their tick counter, the pause, the wait, chat, voice):
a watch taken back during that wait used to leave its bundles under the tick numbers
of the next game on the connection. The server takes a `LeaveGame` whatever the
request budgets say (it is exempt from the shared ones, as `Ping` is): it has no
answer, so a dropped one left its sender browsing the game list while the room still
seated it.

### Game messages

| Msg | Payload | Notes |
|---|---|---|
| `PlayerCommands` | n × Command | client → server; player byte ignored/overwritten |
| `OrderBegin` / `OrderMembers` / `OrderCommit` / `OrderAbort` | serial, template Command, shape, count / serial, member deltas (≤ 2,048) / serial, count, digest / serial | client → server: one explicit selection order as a transaction; the server puts the committed order whole into ONE TickBundle (`Cmd::Order`, its activation tick). Legion clients send their selection verbs this way, Retail clients keep per-unit commands. `net/protocol.h` beside `kOrderMaxMembers`, `server/orders.h`, docs/legion-pathfinding.md "Legion 10,000 stage 3" |
| `TickBundle` | tick u32, n × Command (ascending player, then arrival order), m × Event | server → all clients, 30/s; empty bundles are the clock; the event section exists from day one (§3) |
| `StateHash` | tick u32, hash u64 | client → server, every 30 ticks |
| `Desynced` | tick, reason | server → one client; its game ends (no reconnect) |
| `Pause` / `Resume` | cause, player | the host's pause (`SetPause`): server stops closing ticks, never expires; ends if the host leaves the game |
| `Waiting` | n × (slot, ms left), own (running, ms) | the seats the room is waiting for (§4 disconnect policy); no tick closes while n > 0 |
| `SpeedUpdate` | speed (tenths, 10 = 1×) | server → all; host changed speed mid-game (needs `speedUnlock`); re-cadences tick pacing, not the per-tick `dt`, so the sim stays deterministic |
| `PlayerStatus` | slot, {connected, dropped, left} + ping | informational only — never sim-affecting (sim-affecting lifecycle is an Event in the bundle) |
| `Rejoin` | game id, resume token, fromTick | a reconnect inside the disconnect wait → `Reseated` (slot, rotated token, fromTick, logEnd), then the bundles from fromTick → live bundles |
| `Bye` | reason | clean shutdown both ways |

### Voice chat

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
not from the connection's own (`LeaveGame` likewise: see
[Room scope](#room-scope-what-a-client-takes-and-when)). The client
(`client/voice.h`) captures and plays on SDL audio devices of its own, buffers
2–8 frames per talker, rebuilds a frame the server dropped from the next frame's
FEC, and conceals a late one. It sends while the push-to-talk key is held or,
under TALK MODE Voice Activation (client only, no wire message of its own), while
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
`AiTakeover{player}`
(the disconnect wait ran out; a server Normal AI plays the seat
from this tick, the player becomes computer-owned; kind 3 is reserved and refused),
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
  Explicit orders have limits of their own: 16,384 members, 64
  orders / 384 KiB outstanding per client, 96 KiB of order records per tick
  shared by the room (first pick rotating), the same per-second byte and
  command budgets (an order's members count as commands; over the command
  budget an order waits at the head of its client's queue, it is never
  dropped). An order in a Retail game fails the connection.
- Full validation of remote input: `Cmd` kind range, `type` strings looked up
  against the registry, unit/target ids sane. Malformed ⇒ disconnect.
- Lobby rate limits (CreateGame, Chat, ListGames floods).
- Keepalive with numbers: client pings every 2 s; either side times out after
  15 s of silence. Load-bearing during pause, when no bundles flow. The
  server's allowance is 30 s for a seated client that is loading (from its
  `GameStarting` until its `Loaded`; `Client::loaded` is false in between),
  since the client loads on its main thread without reading the socket. The
  drop reason reads `timed out while loading (30 s)`. No protocol change.
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
   (Superseded 2026-10-29: the display fog is now retail's sight plane, which
   every peer's sim keeps anyway, merged for the local team on the client;
   radar only marks the minimap, from the local player's own units. See
   `height-and-fog.md`.)
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
  options). *(superseded in protocol 323: CREATE asks only the game's name and
  password, then opens the room, and the host picks the map and options in it;
  see [Room editing](#room-editing-protocol-323))*
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
  bundle subscriber with no slot. The host-deniable part is the room's ALLOW
  SPECTATORS rule (see [Room options](#room-options)); per-team fog toggling for
  watchers is still open.
- **In-lobby map transfer:** implemented; see [map transfer](map-transfer.md).
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
  Seated players never rebuild from the log (§4 disconnect policy).
- Host migration is a non-issue by construction (the server owns the game; the
  "host" is a player with lobby permissions — pre-start they pass to the next
  human, in-game they matter only for pause and speed, and follow the host's
  seat; see the lobby service in §4).

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
  forfeit fires as a logged event and replays identically.* (The engine has the
  30 s disconnect wait and the AI takeover instead of the hold, the budgets and
  the log-replay rejoin, §4; tests `server_disconnect` and `disconnect_client`.)
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
oversubscription; a lone game's internal jobs use the threads `--sim-threads`
allows (`auto` by default). A slow tick no longer holds up the network loop or another
room's ticks, although games still compete for CPU and memory bandwidth. Map
startup and replay file writes remain synchronous work on the main thread.

The `room_worker` regression checks independent progress with a blocked worker,
serial/worker/client checksum equivalence, replay-budget rejection, and worker
exception recovery. It runs without networking or retail assets.

### Per-room game mods

The pack transfer messages, alongside map transfer, are
ModOffer/ModRequest/ModChunk/ModReady/ModError (the offer label is `"mods"`). The host selects named immediate subfolders of
`mods/game/`, and every immediate subfolder of `mods/game/always_enabled/` is
added (`MpClient::hostGameModPacks`); files directly in `mods/`, `mods/game/` or
`always_enabled/` are excluded, and `always_enabled` itself is never a pack. An
always-enabled pack hides an ordinary pack whose name matches ignoring case
(`hpi::modPacks`), so the names in the package are unique and plain. A guest never adds its own always-enabled game packs: it mounts the
host's package alone. Selections
are merged with the always-enabled packs first, then the ticked ones, each in
alphabetical order (later layers win), into a bounded, SHA-256-addressed package (no
cosmetic file ever goes in it). Server and peers verify it before room
readiness. Each game-mods room owns its own VFS and registries, leaving other
rooms unchanged. The server takes a package only from the host of a skirmish
room with game mods on: an offer or chunk from a guest or in a campaign
room, and an offer while game mods are off, are answered with ModError. The
replay header records the shared package digest (`ReplayHeader::modDigest`) and
the `gameMods` byte, and playback loads the package from `ModCache/` (in the
per-user folder, `hpi::userMapRoot`; never the game data folder).
The package also carries a pack's `ai/takengine.txt` (loose in the pack's `ai/`
folder or under `ai/` in an archive in it), and a map package carries it when
the map's own archive (its `.kmp`) has it (`maps::build`, checked on decode). The
server reads the room's one file, from the game-mod package, else from the map
package, and each of its AI levels 0-5 applies it over that level's own built-in
profile table (`src/ai/builtinprofile.h`). No other file name in `ai/` is read
for them. A root `takengine.txt` is not read, cosmetic
packs can never supply `ai/` files, and the base install's `ai/` folder is never
consulted for these names. Classic reads the map's `ai/<aiprofile>.txt` /
`ai/default.txt` with the same rule for `ai/` files: game mods, else the map
package, else the base data (`ai::classicProfileScript` with the room's layers);
other map content keeps the view's map-over-mod layering.

### Match pathfinding selection

The host's choice is `Retail` (the default) or `Legion`. Game options and
every lobby/start/spectate snapshot carry it. The server validates the value,
adopts it from the host's option updates in a skirmish or Benchmark lobby and
keeps it once the game runs; both clients and the referee apply it through
shared match setup. Campaign missions are always Retail, and
campaign playback forces Retail whatever a header supplies. The host sets it
among the room's rules; every other client shows it read-only.

The wire byte, the replay header and the saved room settings' `pathfindingMode` use
the identities `0=Retail` and `4=Legion`; `1`, `2` and `3` name no mode. A
missing or unknown saved value loads as Retail, while an unknown network or
replay mode byte is rejected. Legion's groups and per-unit state are part of the
simulation checksum only in mode 4. A Legion client sends each click as one
explicit selection order (`Cmd::Order` in the tick bundles, carried up by the
OrderBegin/OrderMembers/OrderCommit/OrderAbort transaction); Retail clients, the
AI and scripts send per-unit commands. Debug-only `TAK_LEGION=1` selects Legion
in the headless multiplayer test harness, and `crowdbench` accepts `--mode
retail` or `legion` for direct comparisons.

### Room options

`GameOptions` is 19 bytes on the wire, in this order: crusades,
forfeitSelfDestruct, gameMods, speed, speedUnlock, unitCap (u32),
monarchExpendable, stressTest, fogExplored, benchmark, randomStarts,
doubleSight, the pathfinding mode, allowSpectators, mapScenario and powerCodes.
`CreateGame`, `SetGameOptions`, `LobbyState` and `GameStarting` carry it, and
each `GameList` entry ends with the room's allowSpectators byte. In a running
game `SetGameOptions` adopts only speed.

- **ALLOW SPECTATORS** (`allowSpectators`, default 1). With it off the server
  answers every `Spectate` with a failed `JoinResult` ("spectators are not
  allowed in this game"). `Spectate` is the only way into a room without a slot,
  so this one check covers the browser's watch request, a seated player who left
  and asks to watch, and a dropped watcher reconnecting (watchers hold no resume
  token, and `Rejoin` seats only seated players). The host changes the rule in
  the lobby; it is fixed at start, so an admitted watcher is never stranded. A
  host who created the room to watch it is never removed by the rule. The client
  keeps a refusal's reason for the browser (`MpClient::joinError`) and clears the
  pending spectate on any refusal.
- **MAP SCENARIO** (`mapScenario`, default 0; in the lobby it changes only with the
  map, through `SetRoomMap`). With it on, shared
  match setup plays the map's own scenario -- the `.crt` placements and rules
  and the Use Only `.tdf` -- as for a Cartographer Test Map copy, except that a
  Use Only entry no unit type answers to is left out rather than refused (maps
  made for the original game name such units; NeverGlory names 44). Setup runs
  the scenario when the map's `.ota` carries the Test Map opt-in
  (`[TAKPlaytest] authoredscenario=1`) or when the option is on and the `.ota`
  declares `hasscenario`. `SetGameOptions` never changes it: the capacity it
  implies sizes the room's open seats (a scenario can need seats past the map's
  start positions), so it changes with the map's capacity in one `SetRoomMap`,
  which moves seats as a map change does. The server clears it
  for campaign missions, generated maps, stress tests and benchmarks, and forces
  Start Locations to FIXED with it. At START it reads the verified package's own
  files (`maps::scenarioInfo` over the package, never the host's claim): it
  refuses a room whose map has no scenario content ("this map has no scenario
  to play"), refuses while a seat the scenario needs is empty ("Seat player N in
  the lobby to play this scenario"), and refuses a package that leaves out a
  scenario file setupMatch will read: the `.crt` of a declared scenario, or the
  named Use Only list ("the map package leaves out its Use Only list"), since
  setupMatch would otherwise read each machine's own copy, which no digest
  covers. A start refusal reaches the host as `MapError` and the client shows it
  in the room (`MpClient::startError`). The seats a scenario needs are its
  placement owners 0-7 and every Player N rule bank with a group that has a
  condition or an action. The option is sim-affecting and recorded in the replay
  header.
- **START LOCATIONS** (`randomStarts`): 0 FIXED, 1 RANDOM, 2 BY SLOT
  (`net::kStartsBySlot`): slot N takes the map's Nth start whichever seats are
  empty, and a seated slot past the map's last start takes the lowest start no
  seated slot claims by its own number, then a spot near the map centre
  (`sim::startSpotsFor`). Any other nonzero value reads as RANDOM
  (`normalizeStartMode`).
- **POWER CODES** (`powerCodes`, default 0, set by the host in a skirmish lobby,
  fixed once the game runs, forced off in campaigns). `Cmd::PowerCode` carries one of the
  seven simulation codes (`targetId` the code, `queue` the +LOS argument,
  `unitId` the +ManaMe unit); the server drops it while the room's option is
  off and treats an unknown code or argument as a forgery.
- **FOG OF WAR** (`fogExplored`, 0..2) is display for every client, and
  sim-affecting for area clear only (`MatchConfig::roomMapRevealed` /
  `roomLosOff`, `World::setRoomVision`: retail's explored allowance reads the
  Mapping plane, which Map Revealed fills).
- **Benchmark** rooms run the spawn ramp on the server's wall clock: the server
  adds `Cmd::BenchClock` to a tick's bundle whenever more of the ramp's stages
  are due (`targetId`, out of `benchmarkSpawns(level)` spread over 60 real
  seconds from the first tick) and once more when the 60 seconds are up
  (`queue` = 1); every sim fires the due stages in order and ends the run on it,
  and a replay reproduces it from the recorded commands. A client never sends
  `BenchClock` (its parser refuses the kind). A benchmark room may run at
  `kMaxBenchmarkGameSpeed` (110, +100).

### Room editing (protocol 323)

The host picks the map and every rule in the room itself, before START. The
server keeps one rule for what counts as a change: every host request it accepts
in the lobby that changes the room, except the name, the password, the speed and
the host's own READY, moves the room's revision on (`RoomState::roomRev`, from 1)
and unreadies every ready guest in the same step, before the broadcast
(`Server::bumpRules`). `changeBits` says what the last one changed (map,
scenario, options, players, mods, capacity: `net::RoomChange`) so a guest's
room can say what happened without inferring it. The changes are option
updates, the map, MAP SCENARIO, a new game-mod package, a seat edit (a seat
other than the host's own, or the host's own side, colour or team), the host
moving seat, watching or sitting down again, and a KICK. A guest's own edits
unready nobody.

- **Stale READY.** `SlotUpdate` carries the revision its sender last saw. A
  guest's READY on an older one is stored as not ready (the other fields apply),
  so a READY clicked on a view older than the host's change never readies a guest
  for a game it has not seen. `JoinResult` carries the revision too. The host
  never writes another human's READY. A seat that opens or closes, a seat its
  player left, and a newcomer's seat are not ready.
- **Options** (`SetGameOptions`): a skirmish lobby adopts every option but MAP
  SCENARIO, including the unit limit, the pathfinder and POWER CODES; a Benchmark
  lobby keeps its unit limit and has no game mods; a campaign mission's lobby
  adopts only the speed and ALLOW SPECTATORS. A speed change alone is no change.
- **The map** (`SetRoomMap`: serial, map id, capacity, MAP SCENARIO, start mode).
  The client sends one at a time, numbered past the room's `mapSerial`; every
  well-formed request with a new serial is answered by the `LobbyState` that
  carries it, after a `RoomError` when refused (more seated humans than the map
  has starts, a random-map recipe this build cannot make). A random map's
  capacity is its player count, read by the server; a file map's is the host's
  count, as at CreateGame. MAP SCENARIO is forced off for a random map, a stress
  test and a Benchmark; the start mode applies whenever the scenario ends up off,
  so turning it off restores the host's choice in the same change. An unchanged
  map, scenario and start mode is answered to the host alone. A new map drops the
  room's map package and every client's transfer of the old one
  (`Server::resetRoomMap`); the game mods are untouched. The host's client builds
  a file map into a pending package and makes it the room's only when the answer
  arrives, then offers it as usual.
- **Capacity.** One plan (`net/roomseats.h` `planCapacity`) is applied by the
  server and read by the room to warn: a human past the new capacity moves to the
  lowest OPEN seat below it, else the lowest CLOSED one, else the highest AI seat
  (that AI is removed), keeping its side, colour and team; every AI past it is
  removed; the seats past it close and the seats it gains open. A moved player
  is told its seat with `JoinResult` before the table.
- **Name and password** (`SetRoomInfo`): only the flagged fields apply (a lobby
  successor never knew the password, and a rename must not clear it); a name is
  1-32 printable-ASCII bytes after trimming, a password 0-128. The browser and
  the LAN announcement read the room live, so they follow. `MpClient::createGame`
  sends only the printable ASCII of a name, cut to 32 bytes, so a remembered or
  typed name the server would refuse still creates the room. The server still
  takes `SetRoomInfo`, but the client no longer sends it: a game's name and
  password are set when it is created (below) and are static in the room. (A
  client that does send it can still change them. The table carries only whether
  there is a password, so this client forgets the one it shows behind SHOW when
  a table says the lock is gone, and cannot tell a password changed in one step;
  retiring `SetRoomInfo` at the next protocol version closes that.)
- **The game browser's windows and the room's name line**
  (`gameview_lobby.cpp`: `BrowserWindow`, `browserStep`). The browser has no
  password field. CREATE opens a modal window for GAME NAME (default
  `"<account>'s Game"`, up to `kRoomNameMax` printable-ASCII bytes; blank means
  the default) and PASSWORD (OPTIONAL); a click on a locked game's row, or WATCH
  on a locked live game, opens a password window instead of sending anything.
  Both hide the password as pips behind a SHOW / HIDE button, and both answer
  their own request: JOINING... / OPENING THE GAME... while it waits, the
  server's refusal (`CANNOT JOIN: wrong password`) with the text kept, NO ANSWER
  FROM THE SERVER after 10 s (the next request first gives the stale one up with
  `LeaveGame`), THE GAME HAS CLOSED / STARTED when the list no longer offers
  it. A WATCH the server took whose map or game mods this client still has to
  download is answered, not silent (`MpClient::watchLoading`: its `GameStarting`
  waits for them): no clock runs on it, the window (or, for an unlocked game,
  the browser's bottom line, with a CANCEL of its own) says what is arriving
  (DOWNLOADING MAP 37%), WATCH stays greyed and CANCEL takes it back. If that
  game ends meanwhile the list, which the server answers again, no longer has it:
  THE GAME HAS CLOSED. CANCEL or Esc while a window's own `JoinGame`, `Spectate`
  or `CreateGame` is still unanswered takes it back (`browserTakeBack`, which NO
  ANSWER's retry uses too): the password that went with it is wiped and
  `MpClient::leaveGame`
  does the rest -- `LeaveGame` follows the request, so a room or watched game the
  server opens for it is left there at once, and its answer, yes or no, is
  dropped when it arrives (see
  [Room scope](#room-scope-what-a-client-takes-and-when)). The browser is free
  again straight away: the next request's answer cannot be taken for the old
  one's. (A window opened while a game's row, clicked just before, is still
  unanswered says WAITING FOR THE SERVER... beside its greyed button.) A server
  at its game limit refuses CREATE in the window like any other refusal
  (CANNOT OPEN A GAME: server game capacity reached), with the typed name and
  password kept.
  The lobby's screens lie over one another (a window's CREATE over the room's
  LEAVE, its CANCEL and LEAVE over the list's rows, a row over the password
  window's buttons), so a press aimed at the screen before is dropped
  (`lobbyPressStale`): the rest of a double click that changed the screen under
  the pointer, and any press within 300 ms of the new screen's first frame (a
  room that opens or closes as the button goes down); the client's log says so.
  An unlocked game is joined at once and its refusal stays on the browser's CANNOT JOIN
  line, which also says NO ANSWER FROM THE SERVER for a request no window is
  waiting on. In every multiplayer room the name line is static, for the host
  as for a guest: the name, then LOCKED, or, when this client's player typed
  the password (the CREATE or password window, or CONNECT TO SERVER for a LAN
  game), PASSWORD with eight pips and the same SHOW / HIDE (the name is laid
  out the same either way; a shown password has the room it leaves, shrinks,
  then is cut with "..."). That copy is kept
  only while in the room (`roomPass_`, wiped on leaving); the password never
  appears in a log. A shown password is drawn in the MODERN face in either UI
  FONT (`GameView::ExactText`): CLASSIC draws capitals only, and a password
  must read as it was typed.
- **Watching.** The seated host may leave its seat to watch (`MoveSeat` 0xFF,
  answered `JoinResult` 0xFF; ALLOW SPECTATORS does not apply) and take an OPEN
  seat again; a room started with its host watching gives it slot 0xFF.
- **Transfers.** `MapRequest`/`ModRequest` are `room, digest, xfer` and every
  `MapChunk`/`ModChunk` is `room, xfer, offset, bytes`: the receiving side picks
  `xfer`, a per-connection counter, so a chunk left over from an earlier request
  (the map changed, or changed back) is dropped quietly instead of failing the
  transfer, and a request for a package the sender no longer holds is ignored. A
  newer request restarts an upload from offset 0. The server keeps a map
  validation and a game-mod validation per client apart, so a map change never
  cancels a game-mod validation; it counts only live validations against its cap,
  answers a host's offer of the package it has already verified with that
  package's offer, and ignores an offer of a map the room no longer plays. A host
  offer refused for a busy server is offered again after 2, 4 and 8 s.
- **The client's room** (`src/client/gameview_room.cpp`,
  `gameview_mappicker.cpp`, `roomchanges.{h,cpp}`). There is no create screen:
  Play the Machine and a LOCAL SERVER host's START/CONNECT send `CreateGame` at
  once (`GameView::createRoomFromPrefs`), and so does the browser's CREATE once
  its window has the game's name and password, from
  the remembered set of the room's kind, `Settings::spRoom` or `mpRoom` (rules,
  map or random-map recipe, map sort, game-mod packs; separate since protocol
  323, no migration of the old `gameCreate.*` keys). A remembered map that
  cannot be built falls back to the launch's map, then to the generator's
  default recipe, and the room says why. Only the host's own clicks are written
  back, one field at a time, and a map only when the server accepts it; never
  the name, password, watching or MAP SCENARIO, and never from a harness, a
  `--play-map` room, a mission or a Benchmark. The left column's options block
  sends `SetGameOptions` (cells for MAP SCENARIO send `SetRoomMap`) and the
  CHANGE MAP panel's PICK
  `SetRoomMap` with the capacity `seatsFor` gives the map (start positions,
  widened for its scenario). The panel greys a map with fewer seats than the
  room's seated humans and warns which AIs a PICK removes (`planCapacity`). A
  guest diffs each new `LobbyState` against the last (`roomChanges`, with the
  server's `changeBits`), flashes the changed cells and seats and shows one
  note (`HOST CHANGED FOG OF WAR -- READY AGAIN`); multiplayer also adds a
  SYSTEM chat line. Single player's WATCH is `MoveSeat` 0xFF, after which the
  client seats random-race AIs in the open seats when fewer than two are seated.
  A `--play-map` room keeps its map (no CHANGE MAP, LEAVE reads QUIT); mission
  and Benchmark rooms are read-only. Debug builds drive the room through the
  same members with `TAK_ROOM_SCRIPT` (`tools/room_flow_test.py`).

AI seats carry their level in `SlotInfo.aiLevel`: 0-4 our Defensive, Easy,
Normal, Hard and Absurd, 5 Absurdly Defensive (every peer derives its double
income and defensive stance from the level), 6 Classic, shown to players as
RETAIL (a server-side port of the retail AI, `ai::ClassicBrain`; its seat gets
exactly a Normal seat's match config), and 7 Passive (run as Easy with no
exploring and builders held near home; an Easy seat's match config). A level of
8 or more is stored as Normal. A campaign mission's strategic computer players
are always Classic. The server's classic AI tells the sims its squads with
`Cmd::AiGroup`, applied only for a computer-owned player and never accepted from
a client.

Chat and voice in a running game reach the seated players in the recipients mask,
and spectators only when the sender ticks them (`Chat`'s trailing u8, `VoiceUp`'s
`kVoiceSpectators` flag, which the server strips before `VoiceDown`); a
spectator's own chat and voice reach nobody. Before the game starts everything
goes to the whole room.

`BuildManaArea` carries a builder, lodestone type, queue flag and two rectangle
corners in 43 bytes, like area reclaim; every other command keeps the 35-byte
layout. Every peer executes one persistent area mission, exploring deposits
before trying normal clearing, placement, construction and upgrades; its cursor
and exploration stage are hashed. An order verb's `queue` byte carries
`kQueueFront` (Ctrl: the order goes in at the head without clearing the queue)
and the `kQueueToggle` bit (a player's own Shift or Ctrl click takes back a
queued order of the same kind on its target or within 16 px of its point); a
producer's rally list takes both. Fire at a point arrives as `Cmd::AttackGround`.
Builders on patrol repair nearby units automatically (`MatchConfig::patrolRepairs`).
Defensive AI interception is server-local policy; its ordinary movement commands
are sequenced and recorded as usual.

### Versions and replays

Peers, the server and replays must all match `kNetVersion` exactly: the handshake
refuses a peer of any other protocol, and a replay plays only under the replay
format and protocol that recorded it (`supportedReplayProtocol`). The loader reads
the current format (15) and refuses any other format or protocol, and a strict
bundle read accepts every current command kind and event kinds 1, 2 and 4 (3 is
reserved). There are no compatibility variants of the simulation. The replay
header records, beside the map, slots and seed, the pathfinding mode, the MAP
SCENARIO byte, the POWER CODES byte (bit 0 the option, bit 1 the map started
revealed: FOG OF WAR Explored or Full Vision, which +Mapping starts from), the FOG
OF WAR byte, and the `gameMods` byte with the shared package's `modDigest`.
Which release uses which protocol and replay format is in the
[CHANGELOG](../CHANGELOG.md).

For `tools/desync-hunt-remote.sh`, `--minutes` limits simulated match time.
`TAK_WALL_TIMEOUT=3600` grants each client up to one real hour for a dense
stress run; the default remains simulated seconds plus 300.
The same tick, completion and hash checks apply. `--only` retains each scenario's
full-table seed so targeted retries reproduce its original map/start seed.
