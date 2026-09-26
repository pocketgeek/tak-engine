# TAK Engine user guide

[Back to the README](../README.md) · [Build and development guide](development.md)

Detailed play, configuration, hosting, and content reference. See the
[README](../README.md) for the current release and compatibility requirements.

## Game data

Point the engine straight at a **retail install directory** — no extraction
step. Pass it with `--data`, or just launch `takclient` with no arguments: it
pops up a **native folder picker** ("choose your TA:Kingdoms install"), checks
the folder actually holds the game data, and **remembers it** (saved in config,
re-validated each launch) so you're only asked once. It reads the shipped
archives and folders in place:

```
<install>/
  *.hpi              the shipped archives (data, terrain, maps, sections,
                     english, the supported Iron Plague and official packs…)
  Maps/              downloadable maps as *.kmp (each an HPI) + loose maps
  Music/             track*.wav soundtrack
  overrides/         YOUR overrides -- loose files or *.hpi/*.ufo/*.kmp, highest priority
```

Only the **canonical** retail archives in the install root are read — the base
game, the recognized Iron Plague archives, and the official map/rocket packs;
any other `*.hpi` dropped in the root (and all loose files there) is ignored. Maps
come from `maps.hpi` and the `Maps/*.kmp`, music from `Music/`, and anything in
`overrides/` wins over everything. A small **authenticity manifest** of those root
archives is recorded with the folder and recomputed each launch; a moved or
unreadable install re-opens the folder picker.

**Archive precedence.** Within a mounted layer, loose files win over archive
entries; among archives, the entry with the newest stored date wins, with ties
keeping the earlier-mounted copy. Layers then determine priority: overrides
outrank Maps, which outrank the recognized root archives and loose music.
Unknown root archives are not mounted; place custom content in `overrides/`.
Archives in `Maps/` contribute maps and cosmetics, not replacement unit/build
rosters. `hpitool where <dir> <path>` helps inspect archive resolution, and
`hpitool merge` can produce a flat tree for tooling.

## Playing

The engine is **client-server only** — every game runs on a `takserver`, and the
AI runs *only* on the server. Single-player is just a private game on a server the
client starts for you.

Point it at your install and it opens the retail **front-end menu**:

```sh
./build/takclient --data /path/to/tak_install
```

The three doors lead to **Single-Player**, **Multiplayer**, and **Campaign**.
Skirmish lobbies let you choose a map, faction, colour, teams, and one of five
**difficulty levels** for each AI opponent. Campaigns use their mission setup:

| Difficulty | Behaviour |
| --- | --- |
| **Passive** | builds an income-scaled defensive army and defenses near home; never sends attacks |
| **Easy** | builds up slowly and gathers an army before attacking; no raids |
| **Normal** | harasses with small **raiding parties** while massing a main army sized to its mana income |
| **Hard** | expands more aggressively; probes with raids while saving an income-scaled heavy force |
| **Absurd** | Hard, with **double mana income** from recurring and reclaim sources |

From **Normal** up the AI doesn't trickle units in: it peels off a few for **raids**
to pressure and scout, and holds the main force back until it's massed a decisive army
scaled to its mana income, then commits it — re-mustering the next wave afterward.
(Easy skips raids and gathers its army more slowly.)
Unit selection accounts for land connectivity, flight, and usable water; ships can
be sent to reachable coastal firing positions. Raids have a limited allocation
between heavy waves so they do not consume the entire reserve.

Built and conjured units receive their type's default standing orders:
**Offensive** engages and pursues within its standing-order limits,
**Defensive** fires without pursuing, and **Passive** does not auto-engage.
Explicit player attack orders still work. Passive AI units begin Defensive.
Games start partly zoomed in and centered on the local player's Monarch.


In a normal skirmish, each side begins with **only its Monarch** at a map start
position (from the `.ota`, or supplied by the map generator). The Monarch
provides initial mana income and starts the faction's economy and production
chain; Zhon uses mobile conjurers rather than a conventional keep. The AI
bootstraps from the same starting point. In a god-enabled match, a faction
whose priests (`attractsgods` units) have channelled enough mana favour manifests its
**god** once the appear time passes.

Audio (master / music / SFX volumes, per-speaker trim, output device), display,
camera, and interface preferences are set in the in-game **Options** screen
(Esc → Options) and persisted per user: anti-aliasing, **bilinear filtering**
(retail's smooth-scaling video option), **shadows**, swaying trees,
**health bars** (off / damaged / always), build-menu alignment and scale, UI
scale, cursor size and **hardware cursor**, smooth motion, and edge scrolling.
The shadow toggle controls unit, scenery, and projectile shadows. Shading baked
into terrain artwork remains visible; swaying trees also deform their shadows.

Two of those exist because the art is from 1999 and modern displays are not.
**SMOOTH GUI ART** edge-directed-upscales the static interface art, faction
backgrounds, cursors and fonts once at load (no per-frame cost; it says RESTART
because already-built textures keep what they were built with), and **SMOOTH
MOVIES** deblocks the Bink clips as they decode — smoothing across the 8×8
transform seams only where the step looks like an artifact rather than an edge,
which is the opposite of sharpening and the only thing that helps at 12× stretch.

**Benchmark.** *Settings → Benchmark* runs a fixed, deterministic 8-AI
free-for-all on Ulasem Arena at a chosen **intensity** — Low to *Extra Absurd*,
spawning one unit per faction every 1 s, 0.5 s, 0.25 s, 0.125 s, 0.0625 s, or
0.03125 s — for 60 seconds of simulation time. A **stats screen** reports, at
each 10-second mark, the
client and server **CPU %, memory, frame rate, sim speed**, plus **GPU
utilisation, texture VRAM and device VRAM** and the display settings that
produced them. A repeatable load test that stresses the sim and renderer at
scale. Spawns respect map capacity, player caps, and unit-specific limits, so
the requested rate does not guarantee a particular final unit count. A slow
simulation can take longer than 60 seconds of wall-clock time.

### Command line

A **release `takclient`** has a minimal command line:

| Flag | Effect |
| --- | --- |
| `--data <retail-install-dir>` | the game-data root (root `*.hpi` + `Maps/` + `Music/` + `overrides/`). **Optional** — with no `--data`, the client re-uses the folder saved in config, or pops the folder picker on first run (see **Game data**) |
| `--version` | print the version and exit (`--help` prints this usage) |

So a release `takclient` needs **no arguments at all** to launch. Everything else —
the data folder, factions, colours, difficulty, the map, multiplayer, overrides — is
handled by the first-run picker and the menu. Client developer `TAK_*` hooks
are disabled in Release; system/SDL environment handling and server options
are separate.

**Debug builds** additionally accept the launch modes `game <map>` / `map <map>` /
`replay <file.takrep>` / `model <file.3do>` and the dev/test flags (`--side`,
`--aiside`, `--server`, `--overrides {none,cosmetic,full}`, `--crusades`, `--cheat`,
`--demo`, `--mission`, `--campaign`, `--maxfps`, `--novsync`, the `--mp*` headless
harness, …) plus the `TAK_*` diagnostic env vars. Debug `--help` lists the main modes and common flags; the argument parser in
[src/client/main.cpp](../src/client/main.cpp) includes additional harness options. `--overrides` defaults to `full` (a release build always mounts `full`):
`none` = pure retail, `cosmetic` = only art/sound/music, `full` = everything including
gameplay data.

### Controls

Default command hotkeys follow the game's `Keys.TDF`; command, selection, and
emote bindings are editable in the **Esc / Settings menu → CONTROLS** (click a
row, press the new key; right-click clears).

| | |
| --- | --- |
| **Select** | drag = box-select · **Ctrl+A** all your units · **Ctrl+Z** all your units of every type in the current selection · **Ctrl+U** everything on screen · **N** cycle to next unit |
| **Order** | right-click = move/attack (**Shift** queues) · **F** fight-move · **M** move · **A** attack · **P** patrol · **G** guard · **S** stop · **Ctrl+D** destroy · **Esc** cancel an armed order |
| **Groups & formations** | **Ctrl+1–0** assign a group · **Alt+1–0** assign a **formation** · **1–0** recall · **+Shift** appends · **Ctrl+Esc** leave. A unit is in one squad at a time, and a number is a group *or* a formation. A **formation** moves at its slowest member's speed and its stragglers rejoin. Each unit shows its squad under it (`3` = group 3, `3F` = formation 3). Recalling a squad skips its builders — a builder rides along only so anything it builds auto-joins the squad. |
| **Camera** | arrows / middle-drag / **screen-edge** scroll · wheel zoom (toward cursor) · minimap click/drag = move the camera · right-click minimap = move the selection there |
| **Minimap orders** | with an order armed (**F**/**M**/**A**/**P**/**G**), click the minimap to issue it at that spot — e.g. **F** then a minimap click = fight-move across the map |
| **Build queue** | at a training building: left-click **+1**, **Shift** **+5**, **Ctrl+Shift** **+10**; right-click removes the same; **Ctrl**+left starts/toggles infinite production at a stationary producer and also starts it for mobile builders. Each icon shows its queued count. (A builder that *places* things — structures, or a mobile conjurer like a Beast Handler — arms placement instead: click to position.) A mobile builder running infinite production accepts **only Stop**, which clears its queue and restores normal orders. |
| **Reclaim** | with a mobile builder (any unit with `canreclaim`, monarchs included) selected, **right-click-drag** a box to clear it — the builder roams the area reclaiming trees, rocks, and buildings for mana (nearest first). Sacred Stones and Standing Stones are left alone. **Shift** appends the sweep to its orders. |
| **Game** | **Pause** · **+/−** game speed (0.5×–4× in live games, including single-player; only the **host** can change it, with *in-game speed* unlocked in the lobby) · **F4** status/scoreboard |
| **Disco** 🪩 | **Shift+D** — your monarchs spin, bob, hue-cycle, and glow on a little dance floor for 10s, to a synthesised disco track that plays positionally from the monarch. Purely cosmetic, but synced over the lockstep so every player sees it. |
| **Headbang** 🤘 | **Shift+H** — your monarchs headbang to a synthesised heavy-metal track (positional, from the monarch), nodding and flashing red on a mosh-pit glow for 10s. Also cosmetic and lockstep-synced. |

Units are drawn from their 3D models, with projection work distributed across
worker threads. Background music plays from the faction soundtrack.

## Multiplayer

Multiplayer is **client–server**: everyone connects out to one central
`takserver`, so there's no NAT or port-forwarding on the players' side. The
server relays a **server-sequenced deterministic lockstep** — up to 8 players on
up to 8 teams with shared allied vision. Every participant runs the same
simulation, exchanging commands and tick bundles rather than continuous unit
position updates.

```sh
# somewhere reachable (default port 7677):
./build/takserver --port 7677 --data /path/to/tak_install --accounts accounts.conf

# each player — launch the client and join through the menu's Multiplayer door
# (pick the server, sign in with an account name and password, then browse/
# create/join in the lobby):
./build/takclient --data /path/to/tak_install
```

(A debug build can also connect straight from the command line, skipping the menu:
`takclient game "<map>" --data <dir> --server <host> [--serverport N]
[--user NAME --pass PASSWORD]`.)

- **Accounts.** Players sign in with a name and a password; a name the server has
  never seen is registered as you sign in, so there is no separate sign-up step.
  The name is the player's identity in the lobby, in chat and on the scoreboard
  — it replaces the free-text name a client used to be able to claim, so nobody
  can pose as somebody else.

  Server authentication, account-file storage, and protocol limitations are
  described in the [development guide](development.md#server-authentication).

  `--no-auth` serves anyone who connects, with no account at all; it is only for
  a private or LAN server and should be paired with `--local` (bind loopback
  only). That is exactly how single-player launches its private server.

- **Referee sim.** `--data` is required. The server runs the referee simulation
  and AI players and checks clients against its canonical state hash. There is
  no relay-only mode. In single-player the private server runs on your machine.
  A server hosting
  several games at once ticks their sims **in parallel** across CPU cores (games
  are independent), while a single game keeps its intra-tick worker parallelism.
- **Game-data agreement.** The handshake compares `hpi::gameplayHash`, which
  covers unit definitions, weapons, build lists, selected game configuration,
  and simulation fields in feature definitions. A mismatch is rejected at join.
  Presentation-only art/sound overrides do not change it. This is not a checksum
  of the entire install: maps, scripts, and model files are outside this hash,
  although they can affect simulation and must be compatible between players.
- **Lobby.** The in-client lobby has a game browser, a create-game dialog
  (name/password/map; **crusades**, **gods**, and **Monarch Expendable** toggles),
  and a room where each player picks faction, colour, and team and readies up; the
  host opens/closes slots, kicks, and starts. Fog and start-location rules are
  chosen when creating the game and shown as read-only information in the room.
  Random maps can be generated from the create-game screen.
  **Monarch Expendable** is the loss
  rule: *off* (the retail commander rule) means losing your Monarch loses you the
  game even if other units survive; *on* makes the Monarch just another unit. The
  host also sets the **unit cap** — the per-player live-unit limit (250 / 500 /
  1000 / 2000, default 2000; production and new builds stall a player once
  they reach it) — and can **allow in-game speed changes** so the host's **+/−**
  keys re-cadence the match live (0.5×–4×). Speed only changes how fast ticks
  happen in wall-clock — the per-tick `dt` is fixed — so the sim stays bit-identical
  and deterministic.
- **Cross-build determinism.** The sim's trig is routed through a
  deterministic-math shim (`src/sim/detmath`), so lockstep holds across
  compilers and CPUs, not just the same binary. Everyone still needs the same
  engine build and game data (the handshake gates the protocol version).
- **Reconnect & forfeit.** An active dropped player's slot is held for a grace
  period; they can rejoin with a resume token and replay the bundle log to catch
  up. Otherwise they forfeit deterministically. A defeated player leaving does
  not pause surviving players for reconnect.
- **Spectate.** A running game can be **watched live** from the browser (the
  **WATCH** button): the spectator replays the bundle log to the present, then
  follows along with no fog, no control, and a radar that shows every unit.
  Single-player has its own spectate mode too — flip **SPECTATE (WATCH AIS)** in
  the SP lobby and every slot fills with a random-faction AI to just watch them
  fight.

See [multiplayer design](multiplayer-design.md) for the full design and
[deterministic math scope](detmath-scope.md) for the determinism contract. (The old 2-player `--host`/`--join` peer mode is
retired.)

## Replays

Use **Settings → Load Replay** in the title menu to open a recorded game,
including in release builds. Clients save recordings beside `settings.ini` in
the per-user application data directory; the menu lists `.takrep` files in that
directory. A server can additionally save finished
games with `--replaydir <dir>`.

Debug builds also support direct playback:

```sh
./build-o2/takclient replay <file.takrep> --data /path/to/tak_install
```

**Pause** and **+/−** control playback; the time bar shows elapsed and total time.
Replays contain match setup and commands, not the retail assets. They require
compatible engine behavior and game data. Version 0.7.1 uses protocol **179**; version 0.7.0 uses **177**.
All multiplayer clients and servers must run the same compatible build.
Different-protocol peers and replays, including those from 0.6.9 (protocol 176),
are rejected.

## Overrides

Anything in the install's `overrides/` folder -- loose files or `*.hpi`/`*.ufo`/`*.kmp`
archives -- overrides the shipped data, exactly like the original game. For
example a `overrides/click.hpi` holding `sounds/*.wav` replaces the faction
order-acknowledgement tones. Overrides are classified as **cosmetic** (textures,
sprites, sound, music, fonts, GUI) or **gameplay** (unit/weapon/side/
build/feature data, maps, COB scripts and 3DO models). Scripts and model origins
control factory production and must agree across peers. A release build always mounts **`full`** (everything);
a debug build can restrict it with `--overrides {none,cosmetic,full}` (`cosmetic`
mounts only the art/sound tier). Presentation-only overrides can differ between
players. Feature files contain
both art and simulation fields: changing simulation fields can change the
fingerprint even under the cosmetic tier. Keep gameplay overrides identical
between peers, including scripts and models; the current fingerprint does not
cover every simulation input.

## Map editor

`cartographer` is the project's clean-room map editor, sharing the engine's VFS,
TNT loader, and terrain compositor. It opens maps and creates blank or generated
maps, paints retail section prefabs, edits features/units/start positions, and
provides scenario properties, resize, unit restrictions, and trigger-rule editing.
**Ctrl+S** saves loose map files; **Ctrl+B** writes a distributable `.kmp` bundle.

Build the `cartographer` target, then launch it with a map name and retail data:

```sh
./build/cartographer "ulasem arena" --data /path/to/tak_install --out /path/to/output
```

It is included in Linux packages; Windows/macOS game bundles currently carry
only the client and server. Editor polish and retail parity remain ongoing.
See [cartographer-port.md](cartographer-port.md) for implementation notes.
