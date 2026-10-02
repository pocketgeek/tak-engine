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
  overrides/<pack>/  selectable packs -- loose files or *.hpi/*.ufo/*.kmp
```

Only the **canonical** retail archives in the install root are read — the base
game, the recognized Iron Plague archives, and the official map/rocket packs;
any other `*.hpi` dropped in the root (and all loose files there) is ignored. Maps
come from `maps.hpi` and the `Maps/*.kmp`, and music from `Music/`.
Selected override packs replace retail files. A small **authenticity manifest** of those root
archives is recorded with the folder and recomputed each launch; a moved or
unreadable install re-opens the folder picker.

**Archive precedence.** Within a mounted layer, loose files win over archive
entries; among archives, the entry with the newest stored date wins, with ties
keeping the earlier-mounted copy. Layers then determine priority: selected overrides outrank installed Maps,
which outrank the recognized root archives and loose music. Verified map-package
resources for the current match take precedence over these layers.
Unknown root archives are not mounted; place custom content in a named subfolder of `overrides/`.
Archives in `Maps/` contribute maps and cosmetics, not replacement unit/build
rosters. `hpitool where <dir> <path>` helps inspect archive resolution, and
`hpitool merge` can produce a flat tree for tooling.

## Playing

Mobile builders that can reclaim automatically clear reclaimable trees, rocks,
and other obstacles from an otherwise valid building footprint before starting
construction. Shift-queued sites and build lines use the same sequence; Stop
cancels it. Unreclaimable features, unsuitable terrain, and occupying units still
block placement. This automatic clearing is an engine convenience beyond retail.

Construction assistance follows the retail builder restriction: an unrestricted
builder can assist unfinished construction owned by the same player; a
`builderlimited` unit must also have that target in its build menu. The cursor
indicates whether the selected units can perform the action.

To upgrade a lodestone, select a builder that offers the faction's advanced
lodestone and place it on your completed basic lodestone's mana spot. This works
for all five factions, including Creon's Mana Amplifier, with either balance set.
The larger footprint must still fit. The basic lodestone dissolves during the
first half of the advanced building's normal construction time; the advanced
one materializes during the second half, with the faction's construction effects.
The upgrade costs the advanced building's normal mana cost, produces no mana
until complete, and needs no additional unit-cap slot. Stopping or destroying
the unfinished upgrade removes it normally; the consumed basic lodestone is
not refunded or restored.

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
| **Defensive** | keeps building an army and defenses near home as mana allows, up to the game cap; never sends attacks |
| **Easy** | builds slowly; attacks after four minutes in groups of at most eight, with at least two minutes between waves; no raids |
| **Normal** | harasses with small **raiding parties** while massing a main army sized to its mana income |
| **Hard** | continues expanding income and production; spends on raids while saving a heavy force |
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
Explicit player attack orders still work. Defensive AI units begin in the Defensive stance.
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
Development builds after 0.7.1 also add boat shadows, an intentional enhancement
over retail Glide, controlled by the same Shadows option.

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

The in-game stats panel shows **Units** (your living units) with **Kills** directly
below it. Spectators see the total living units in the match. **Real Time** is real
elapsed match time; **Game Time** advances with the simulation. **Client CPU** measures
the client's share of total CPU capacity, from 0% to 100%. **Server CPU** measures
the local server's share and is omitted when connected to a remote server.
**GPU** shows the driver-reported utilization, or **N/A** when unavailable.
GPU utilization is whole-device, from 0% to 100%, including other applications.
NVIDIA uses the driver-installed NVML library on Windows and Linux; it launches
no helper process and requires no bundled NVIDIA library. AMD Linux uses amdgpu
sysfs; macOS uses IORegistry. Windows AMD/Intel use the system GPU Engine counters,
summing processes on each engine and reporting the busiest engine at 0–100%.
The name and dedicated-memory readings follow that same adapter. This uses
built-in Windows libraries, with no extra downloads. On multi-GPU systems,
NVIDIA uses its first adapter; the Windows fallback chooses the busiest adapter.
Intel Linux i915 uses system-wide perf engine counters and reports the busiest
engine. Access follows the kernel perf permissions; blocked counters show N/A.
The newer Xe driver is not yet supported by this sampler. No process-only
utilization is substituted for unavailable system-wide readings.
GPU readings refresh in the background so driver queries do not stall rendering.
The **F4** player panel uses retail’s Name, Kills, Losses, and Score table, with
faction emblems in each player’s color. Defeated players remain marked OUT.
Performance metrics, clocks and unit counts live in the side panel.
The side panel also shows your kills and score below Units when playing.

### Campaigns

The campaign door opens the authored chapter book. Choose a campaign tab, then
use the page arrows, **Left/Right**, or **Page Up/Page Down** to browse chapters.
**Home/End** selects the first/last chapter; **Tab** switches campaigns;
**Enter** plays the selected chapter and **Escape** returns to the menu.
Every chapter remains selectable, including Iron Plague's alternate ending.

After the chapter movie and loading screen, the briefing appears over the paused
battlefield. **Enter**, **Space**, **Escape**, or a left click dismisses it and
starts the mission. The mouse wheel and **Up/Down/Page Up/Page Down** scroll long
objectives. **O** toggles the objectives panel during play. Mission sounds follow
their script events; a missing authored sound is not replaced with another clip.

Victory records chapter completion. **Next** follows the authored campaign order;
**Retry** reloads the same chapter. See the [campaign notes](campaign-design.md)
for the retail comparisons and remaining differences.

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
[src/client/main.cpp](../src/client/main.cpp) includes additional harness options. The lobby controls override policy and pack selection; no packs load implicitly.
`--overrides` selects a policy for debug paths, but does not select packs.

The legacy `game <stem> --mission` spelling now launches through the same campaign
server path as `--campaign <stem>`; it no longer uses a separate mission interpreter.

### Controls

The [complete default hotkey and mouse reference](../README.md#controls) is in
the README. Command, selection, view, and emote bindings are editable in
**Esc / Settings menu → CONTROLS** (click a row, press the new key; right-click
clears).

| | |
| --- | --- |
| **Select** | drag = box-select · **Ctrl+A** all your units · **Ctrl+Z** all your units of every type in the current selection · **Ctrl+U** everything on screen · **N** cycle to next unit |
| **Order** | right-click = move/attack (**Shift** queues and shows selected units’ routes, including production-building rally orders) · **F** fight-move · **M** move · **A** attack · **P** patrol · **G** guard · **S** stop · **Ctrl+Shift+D** toggle self-destruct · **Esc** cancel an armed order |
| **Groups & formations** | **Ctrl+1–0** assign a group · **Alt+1–0** assign a **formation** · **1–0** select, press again to track · **+Shift** appends when assigning · **Ctrl+Esc** leave. A unit is in one squad at a time, and a number is a group *or* a formation. A **formation** moves at its slowest member's speed and its stragglers rejoin. Each unit shows its squad under it (`3` = group 3, `3F` = formation 3). Recalling a squad skips its builders — a builder rides along only so anything it builds auto-joins the squad. |
| **Camera** | arrows / middle-drag / **screen-edge** scroll · wheel zoom (toward cursor) · minimap click/drag = move the camera · right-click minimap = move the selection there |
| **Minimap orders** | with an order armed (**F**/**M**/**A**/**P**/**G**), click the minimap to issue it at that spot — e.g. **F** then a minimap click = fight-move across the map |
| **Build queue** | at a training building: left-click **+1**, **Shift** **+5**, **Ctrl+Shift** **+10**; right-click removes the same; **Ctrl**+left starts/toggles infinite production at a stationary producer and also starts it for mobile builders. Each icon shows its queued count. (A builder that *places* things — structures, or a mobile conjurer like a Beast Handler — arms placement instead: click to position.) A mobile builder running infinite production accepts **Move/Patrol** as rally orders for its output; **Shift** appends rally steps. **Stop** clears production and restores normal orders. |
| **Reclaim** | with a mobile builder (any unit with `canreclaim`, monarchs included) selected, **right-click-drag** a box to clear it — the builder roams the area reclaiming trees, rocks, and buildings for mana (nearest first). Sacred Stones and Standing Stones are left alone. **Shift** appends the sweep to its orders. |
| **Game** | **Pause** · **+/−** game speed (0.5×–4× in live games, including single-player; only the **host** can change it, with *in-game speed* unlocked in the lobby) · **F4** names/kills/losses/score |
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
  (name/password/map; **Crusades**, **Double Sight/Radar**, and **Monarch Expendable** toggles),
  and a room where each player picks faction, colour, and team and readies up; the
  host opens/closes slots, kicks, and starts. Fog and start-location rules are
  chosen when creating the game and shown as read-only information in the room.
  **Not Explored** starts terrain hidden; **Explored** starts it mapped. Both
  retain discovered terrain under fog when sight is lost; hidden enemies remain
  unselectable. **Full Vision** removes fog.
  Choose **Double Sight/Radar** during multiplayer or skirmish game setup;
  the lobby displays its status as read-only information. It
  doubles every unit’s sight and radar distances, without changing weapon range.
  Campaigns always use their authored sight/radar values.
  Defeated players go to results automatically after three real-time seconds;
  surviving players keep playing throughout.
  Random maps can be generated from the create-game screen. Choose Mainland,
  Lakes, or Islands and one of the five world palettes. The generator reserves
  flat bases, clear army approaches, and three home mana spots per player (one
  of each strength). Extra mana is added in equal rounds, with comparable walking
  distances; space limits can reduce the number for everyone. Forests and rocks
  form clusters outside reserved approaches. Hills use retail terrain sections,
  copying their painted artwork and heights together.

  Water amount is an intensity control, not a percentage of the map. The preview
  reports actual water coverage. At zero, Mainland and Lakes are dry. Islands
  allocate water automatically, with connected shipping lanes and open harbor
  space; they require naval or air travel between bases. Minimum size increases
  with layout and player count to leave enough room. Sliders control additional
  scenery, not the decorations already painted or placed in retail sections.
  Existing version-1/version-2 generated-map IDs retain their old layouts;
  newly generated maps use version 3.
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
compatible engine behavior and game data. Version 0.7.21 uses protocol
**213** and replay format **10**. Format-9 recordings from protocol 212 remain
compatible because this update does not change the tactical simulation. Use
matching builds for all clients and the server; other protocol versions are rejected.

## Overrides

Put each override pack in its own immediate subfolder of `<data>/overrides/`.
For example, `overrides/New Sounds/sounds/click.wav`, or an HPI/UFO/KMP archive
inside `overrides/New Sounds/`. **Files directly in `overrides/` are never loaded.**

On the game-create screen, the host chooses **Off**, **Cosmetic**, or **Full**.
The lobby displays that mode as information and lets players check any number
of packs (up to 64). Off hides the list and loads
none. Selections are remembered between sessions, including while Off. Packs load
in alphabetical folder-name order; later names win when files overlap.

Cosmetic allows textures, sprites, sounds, music, fonts and GUI art. Gameplay
files, including unit/weapon/build definitions, feature definitions, maps, COB
scripts and 3DO models, require Full. Multiplayer guests can select their own
local cosmetic packs separately; their gameplay files are ignored. The host's
Off setting disables local overrides too. Campaigns use the retail data.

In Full mode, the host's selected packs are bundled and automatically sent to
the server and other players. Everyone verifies the same package before the
match can start. Transfers are limited to 256 MiB and 65,536 files; downloads
are stored by checksum in `OverrideCache/`, without extracting arbitrary paths.
These shared files apply only to that room. Local cosmetic packs can customize
presentation on top of the shared package. Replays record the package checksum
and require the matching cached package.

## Map editor

![Cartographer unit browser, terrain canvas and minimap](img/cartographer.png)

Cartographer shares the engine's map loader, terrain renderer and placement
rules. Launch it from its application shortcut, or use the command below. It
reuses the game's remembered data folder and offers a folder picker when needed.
The File menu provides New, Open/recent maps, Save, Save As and loose export.
**Ctrl+S** saves a playable `.kmp` bundle; **Ctrl+Shift+S** opens Save As.
Loose export is an advanced File-menu action, not the normal save format.

Search the terrain, feature and unit browsers; unit rows include build portraits,
friendly names and internal identifiers. Place, Select, Erase and Pan are separate
tools. Right-drag pans. Selection supports group movement, copy/paste, duplication,
deletion and properties. The Regions tool draws and resizes named trigger areas.
**Scenario → Placed units** searches placed units by authored name, type/friendly
name, or owner. Click or press Enter to select and locate a match; Shift+Enter
opens its properties. Named units display their names on the map when zoom allows.
**F6** or **View → Unit model preview** opens the selected unit's 3D model (or
the current unit-browser entry). Drag to rotate, use the wheel to zoom, and press
Escape to close. Models/textures load from the archives in the background.
The preview initializes the model script and uses the game renderer's helper-piece
policy; it is a visual inspector, not a full gameplay animation simulator.
Missing textures are reported and shown in magenta.

![Cartographer 3D unit model preview](img/cartographer-model.png)
View offers a minimap, Fit Map, Frame Selection, a bookmark, layer visibility,
UI scaling, and engine-backed movement/buildability/water/slope overlays.

Scenario provides metadata, resize, restrictions, Check Map and trigger editing.
Check Map reports located warnings, including inaccessible starts/mana, isolated
terrain and initial naval factory output clearance. Click a located result to
inspect it. These are authoring diagnostics, not a guarantee that every mission
outcome or complete ship launch animation works.

Edits support **Ctrl+Z/Ctrl+Y** undo/redo and periodic recovery. Saves retain the
previous version as `.bak`; save/export runs in the background with progress.
Escape cancels before publication; a replacement already underway finishes or
rolls back. Prefer KMP over loose multi-file exports for a single replacement.

**Test Map (F5)** launches a temporary snapshot in the normal private game lobby.
Seat the scenario's players and start; the editor and unsaved document stay open.
Verified authored scenarios can start with a single participant in the current
checkout. Ordinary skirmishes still require at least two.
The snapshot enables authored placements/rules on both client and server without
changing ordinary retail-map skirmish setup. The following runtime additions are
in 0.7.14 (protocol 200), following 0.7.13 (protocol 198):

- **Use Only** limits what players and AIs can construct. Existing placements and
  units created by scenario actions are retained even if their types are excluded.
- Neutral placements use their own owner, do not appear as a lobby/score participant,
  and do not automatically fight nearby players. Players can explicitly attack or
  capture an eligible neutral unit.
- Placed **Health %** sets starting HP, without changing maximum HP. Type and
  placement **Armor % / Weapon %** multiply; type defaults apply to later spawns too.
  Placed veterancy replaces the type default; units marked `noveteran` stay unranked.
  Retail does not apply the custom-type Health field, so Check Map warns about it.
- Rule ownership is **All Players**, followed by **Player 1–8**. All Players rules
  execute separately for each seated player. Rules run at startup and once per game
  second while their conditions remain true; use Disable Rule for a one-time event.
- **Victory/Defeat** actions determine results for their selected players. A victory
  does not automatically defeat opponents. Defeat removes the defeated player's
  units immediately. Normal skirmish elimination is disabled in an authored
  scenario, so add terminal rules if it should end.

- **Display Name** sets the placed unit's in-game label (retail retains up to 31
  bytes). It is cosmetic, accepts duplicates, and is not a trigger identifier.
  CRT vertical values are retained when saving but do not override altitude;
  retail ignores that field. Position cells refer to the footprint's origin.
- **Heal/Damage** amounts are HP, not percentages. A positive **resource limit**
  caps storage and suspends natural income; **Set resources normal** restores
  the units' normal income/storage. Scripted resource changes still work.
- Missing regions select nothing. Unknown unit-type names behave like **Any Unit**
  in retail, so resolve Check Map's unknown-type warnings before playing.

See [scenario runtime details and limits](crt-triggers.md) before distributing a map.

Enable **Scenario → Log Test Map triggers** before F5 for local execution
diagnostics. The launch message gives the persistent log path, under Cartographer's
preferences folder in `playtest-logs`. Each file records game tick, player, rule
group, and action opcode/operands. Player/group/action numbers start at one;
game time uses 30 ticks per second. An action record means execution was attempted,
not that the requested operation succeeded. Records stop after a 4 MiB budget.
The option is off by default, is remembered, and does not enable logging on peers
or the server. It does not change simulation hashes or ordinary saved maps.

Build the `cartographer` target, then launch it with a map name and retail data:

```sh
./build/cartographer "ulasem arena" --data /path/to/tak_install --out /path/to/output
```

Linux packages and Windows/macOS release bundles include the editor. Recent
cross-platform packaging changes still need live Windows/macOS verification.
See [the implementation ledger](cartographer-improvements-progress.md) for
validation and remaining limitations, and [cartographer-port.md](cartographer-port.md)
for historical retail research.
