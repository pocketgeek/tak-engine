<div align="center">

# TAK Engine

**Play Total Annihilation: Kingdoms on modern systems.**

A clean-room C++20 / SDL2 recreation of Cavedog's 1999 fantasy RTS.

[![version](https://img.shields.io/badge/version-0.7.12-c9a227?style=flat-square)](https://github.com/pocketgeek/tak-engine/releases)
[![platforms](https://img.shields.io/badge/platforms-Linux%20·%20Windows%20·%20macOS-4c8c4a?style=flat-square)](#download)
[![license](https://img.shields.io/badge/license-GPL--3.0-6c3483?style=flat-square)](LICENSE)

[Download](https://github.com/pocketgeek/tak-engine/releases/latest) · [Getting started](#getting-started) · [User guide](docs/user-guide.md) · [Build from source](docs/development.md)

<a href="docs/img/title.jpg"><img src="docs/img/title.jpg" width="80%" alt="TAK Engine title screen"></a>

<table>
  <tr>
    <td colspan="2"><a href="docs/img/army.jpg"><img src="docs/img/army.jpg" alt="Hundreds of Aramon troops, cavalry, and siege units visible together on Ulasem Arena"></a><br><sub>A large Aramon force — 16,000-unit development stress scene, showing part of one army</sub></td>
  </tr>
  <tr>
    <td width="50%"><a href="docs/img/gameplay.jpg"><img src="docs/img/gameplay.jpg" alt="Aramon barracks and troops in the local development demo"></a><br><sub>Aramon base on Ulasem Arena</sub></td>
    <td width="50%"><a href="docs/img/naval.jpg"><img src="docs/img/naval.jpg" alt="Veruna ships fighting on the water at Cairbray Coast Landing"></a><br><sub>Naval combat on Cairbray Coast Landing</sub></td>
  </tr>
  <tr>
    <td width="50%"><a href="docs/img/lobby.jpg"><img src="docs/img/lobby.jpg" alt="The create-game screen with map selection and preview"></a><br><sub>Maps and match setup</sub></td>
    <td width="50%"><a href="docs/img/campaign.jpg"><img src="docs/img/campaign.jpg" alt="The Book of Darien campaign mission picker"></a><br><sub>Campaign selection</sub></td>
  </tr>
  <tr>
    <td width="50%"><a href="docs/img/streaming.jpg"><img src="docs/img/streaming.jpg" alt="YouTube streaming panel with Max 3840 and 60 FPS selected"></a><br><sub>Built-in YouTube streaming</sub></td>
    <td width="50%"><a href="docs/img/results.jpg"><img src="docs/img/results.jpg" alt="Post-game statistics with aligned player columns"></a><br><sub>Post-game statistics (sample data)</sub></td>
  </tr>
</table>

<sub>Captured from the 0.7.12 source build. Army, base, and naval views: development demos. Results: sample statistics.</sub>

</div>

Command Aramon, Taros, Veruna, Zhon, or Creon in skirmishes, campaigns, and
multiplayer. The engine reads your original game's maps, models, scripts,
interface art, and sound directly from its installation.

**You need your own copy of Total Annihilation: Kingdoms.** No retail game
assets are included. An installation of *Kingdoms + The Iron Plague*, such as
the GOG edition, supplies the game data.

## New in 0.7.12

<a href="docs/img/diplomacy.jpg"><img src="docs/img/diplomacy.jpg" width="70%" alt="Diplomacy with outgoing mana and chat recipient checkboxes; gift buttons disabled for an ineligible selection"></a>

*Diplomacy test scene after gifting: no eligible units remain selected.*

- **Diplomacy on D:** all players, eligible-unit gifting, outgoing mana-sharing
  checkboxes for teammates, and checkboxes choosing who receives your chat.
- **Hard/Absurd expansion:** earlier expansion builders, reserved mana sites, and
  new deposits prioritized over nearby upgrades. See [expansion results](docs/ai-expansion-2026-09-29.md).
- **Memory/resource fixes:** loading fonts, model/editor teardown, stopped-stream
  buffers, and a terrain reload worker race. See [the memory audit](docs/memory-audit-2026-09-29.md).

These changes ship in **0.7.12** and require **protocol 195**. Update clients and
servers together. See the [release notes](docs/release-0.7.12-notes.md) and
[full validation report](docs/release-0.7.12-validation.md).

### Previously in 0.7.11

- **Defensive Passive AI:** continues growing its army and defenses near home
  as mana allows, without sending attacks.
- **Persistent Hard and Absurd AI:** keep expanding income and production and
  building troops beyond the former AI policy ceilings. Actual player and
  unit-type limits still apply. Absurd retains its double mana income.
- **Zhon production:** mobile producers prioritize an opening army and keep
  producing troops while other builders expand. Expansion orders remain counted
  while builders travel, and idle troops clear production sites.
- **Normal retains its force targets.** Movement and pathfinding rules are
  unchanged; these changes adjust the AI's production decisions.

See the [0.7.11 validation report](docs/release-0.7.11-validation.md) and
[AI behavior measurements](docs/ai-growth-2026-09-28.md).

### Previously in 0.7.10

- **Large-map performance:** terrain chunk caching, fog geometry reuse, fewer
  distant model and shadow submissions, and less pathfinding, script scheduling,
  and collision-grid overhead. These improve measured workloads; 4× simulation
  speed and 16,000-unit real-time play are not guaranteed.
- **Random maps and map sharing:** balanced generation with validated routes,
  automatic transfer of missing or differing maps, and generated maps saved on
  the host, clients, and server when play starts.
- **Team support:** allied unit gifting, shared vision, and surplus mana sent
  first to allies with the lowest storage fill percentage.
- **Setup and player identity:** choose unit limits and Allow Speed Change when
  creating a game; the lobby displays them. Leaving a skirmish lobby returns to
  creation. **Options → Player Name** sets the name for new local games.
- **Presentation and controls:** restored Unit Info fonts and button feedback,
  corrected stationary building rendering, mobile-builder production rally
  orders, and embedded Windows/macOS application icons.

See the [0.7.10 validation report](docs/release-0.7.10-validation.md),
[performance measurements](docs/performance-2026-09-28.md), and
[map sharing guide](docs/map-transfer.md).
Version 0.7.9 added Windows startup/audio fixes, direct GPU telemetry, and the
retail-style score table; see its [validation report](docs/release-0.7.9-validation.md).

Version 0.7.8 restored scripted corpses and death timing, improved building
lighting, and fixed streaming shutdown; its
[validation report](docs/release-0.7.8-validation.md) documents those changes.

Version 0.7.7 added automatic clearing of reclaimable construction obstacles,
in-place lodestone upgrades for all factions, more accurate contextual cursors,
Double Sight/Radar, explored-terrain memory, factory rally-order displays, and
multiplayer defeat handling that lets survivors keep playing. See its
[validation report](docs/release-0.7.7-validation.md) and
[construction, cursor, and ruin audit](docs/construction-cursors-2026-09-27.md).

Recent releases also added **YouTube streaming** with title/lobby music,
**Max 3840 / 60 FPS** defaults, hardware encoding, and VSync pacing; improved
rendering/shadow performance and campaign presentation; corrected naval
construction and combat; and restored the monarch-under-attack warning.
Group recall uses one number-key press to select and a second to track.

## Download

Get **version 0.7.12** from the [latest release](https://github.com/pocketgeek/tak-engine/releases/latest).
Choose the package for your system:

| System | Package |
| --- | --- |
| Windows x64 | `windows-x64-setup.exe` installer, or the portable ZIP |
| macOS Apple Silicon | `macos-arm64.dmg`; drag **Total Annihilation - Kingdoms** to Applications |
| Ubuntu 22.04 / 24.04 / 26.04 | Matching `ubuntu…-amd64.deb` |
| Debian 12 / 13 | Matching `debian…-amd64.deb` |
| Fedora 44 | `fedora44-x86_64.rpm` |
| Arch Linux | `x86_64.pkg.tar.zst` |

Release filenames also include the project name and version. Install Linux
packages with `apt install ./…deb`, `dnf install ./…rpm`, or `pacman -U ./…pkg.tar.zst`
using administrator privileges. Linux packages include the client, dedicated
server, and Cartographer map editor.

Game libraries are bundled; your system still provides windowing, audio, and
graphics support. On macOS, right-click → **Open** on the first launch if needed.
For the ZIP, launch **Total Annihilation - Kingdoms.app**, rather than its internal executable.

Packages label the client **Total Annihilation: Kingdoms**
(**Total Annihilation - Kingdoms** for Windows shortcuts and the macOS app folder),
omit server launchers, and start the Windows client without a console window.
Windows executables/installers and the macOS app bundle include the crown icon.
The macOS bundle keeps its required `.app` suffix on disk. Finder’s filename-extension
preferences control whether that suffix is displayed.
The `-debug` downloads are for diagnostics and development.

## Getting started

1. Install or unpack TAK Engine.
2. Launch **Total Annihilation: Kingdoms** and choose your original game's installation folder
   when prompted. Select the folder containing the retail `.hpi` archives;
   no extraction is needed. The engine remembers this location.
3. Choose the single-player door for a skirmish, multiplayer to join a server,
   or campaign to browse the missions.
4. Pick your map, faction, opponents, and balance settings, then start playing.

You can also supply the data folder explicitly:

```sh
takclient --data /path/to/tak_install
```

Release builds otherwise use the menus; `--version` prints the installed
version. Custom maps belong in the installation's `Maps/` folder, and custom
content belongs in `overrides/`. Unknown archives dropped into the installation
root are not loaded. See the [game-data guide](docs/user-guide.md#game-data)
for layout, archive precedence, and troubleshooting a rejected folder.

## What you can play

- **Skirmish:** up to eight slots, five factions, teams, generated maps,
  retail or Crusades balance, and five AI difficulty levels.
- **Multiplayer:** hosted games, shared allied vision, spectators, reconnects,
  and a server referee running the same deterministic simulation as the clients.
- **Campaigns and scenarios:** mission scripts and scenario triggers, using
  the original game data. Individual missions remain subject to ongoing fixes.
- **Replays:** open recorded matches through **Settings → Load Replay**.
- **Map editing:** Cartographer creates and edits terrain, objects, start
  positions, named regions, and scenario rules, and exports `.kmp` map bundles.
  The **Regions** tool draws, moves and resizes trigger areas on the map;
  **Enter** opens the selected region's name and coordinates. Editing supports
  undo/redo and periodic recovery copies; recovery tries the previous snapshot
  if the newest copy is damaged. Shrinking a map asks before cropping terrain
  and features; move outlying units, starts and regions inside first.
  Units and features support box selection,
  group movement, copy/paste and deletion; feature moves protect occupied cells.
  **View** can hide features, units, starts and regions; layer choices are remembered.
  Terrain thumbnails load in the background, and the minimap refreshes after terrain edits.
  A first-launch guide introduces editing and saving. Hover over toolbar controls,
  filters or the minimap for action help and shortcuts.
  The embedded editor font supports mixed case, accented Latin, Greek and Cyrillic.
  **T** opens scenario scripting; its full-text area wraps long operands, supports
  **Page Up/Page Down** scrolling, and offers **Copy Text** for the selected rule.
  Flag fields accept new names or suggest existing flags from that player’s rules;
  use the arrow button or **Alt+Down** to open the suggestions.
  **Scenario → Check Map** reports
  terrain, start, mana and scenario issues in a clickable results list, including
  disconnected movement regions for distinct mobile unit profiles and footprints.
  **Scenario → Regenerate from Recipe** restores a generated map's settings;
  preview changes before accepting them. Reproduction needs the same game assets.

Each ordinary skirmish starts with your Monarch. Build your economy and army;
Zhon's mobile conjurers replace the conventional keep-based production chain.
Unless **Monarch Expendable** is enabled, losing your Monarch loses the game.
The lobby supports a maximum of **2,000 live units per player**.
**Double Sight/Radar** doubles every unit's sight and radar distance in skirmish
and multiplayer. Choose it in game setup; the lobby only displays its status.
It does not change weapon range or campaign missions.
Terrain you have explored remains visible under fog after your units leave;
enemy units outside current sight remain untargetable.

| AI | What to expect |
| --- | --- |
| Passive | Keeps growing its army and defenses at home as mana allows; sends no attacks |
| Easy | Builds slowly and gathers an army before attacking |
| Normal | Sends probing raids while saving a larger attacking force |
| Hard | Keeps expanding income and production, spending on raids and larger attack waves |
| Absurd | Hard behavior with double mana income, including reclaim |

Choose **Generate Random Map** under the map preview to configure a Mainland,
Lakes, or Islands map. The available sizes depend on the layout and player count.
The preview reflects the selected seed and settings; see
[random map generation](docs/random-map-generation.md) for the placement and
connectivity rules.

The AI considers income and available terrain when building its force.
[The user guide](docs/user-guide.md#playing) explains standing orders,
production queues, match options, and AI behavior in more detail.

## Player scores

The results screen uses retail scoring: destroying a completed unit adds that
unit's authored **experience points** to the attacking player's score. Values
come from the active balance data, so Retail and Crusades can award different
points for the same unit. For example, a Taros Zombie is worth 3 points in Retail
and 5 in Crusades.

Building units, gathering mana, and losing your own units do not change your
score. Destroying your own units or unfinished construction awards no points;
retail does award points for destroying another player's units even if allied.
Some campaign scripts explicitly control the score instead. See the
[retail scoring evidence](docs/campaign-presentation-2026-09-26.md#result-score).

## Controls

These are the engine's default bindings. Command, selection, view, and emote
bindings can be changed in **Settings → Controls** (click a row and press a
new key; right-click clears it). Number-key squads, **Esc**, chat, pause, and
speed controls are fixed. Orders requiring a target are armed by the key and
issued with a left-click; **Shift** queues the order.

### Orders and unit actions

| Key | Action |
| --- | --- |
| **M** | Move |
| **A** | Attack |
| **F** | Fight-move |
| **P** | Patrol |
| **G** | Guard |
| **H** | Heal / repair a friendly unit |
| **L** / **U** | Load a passenger / unload at a destination |
| **S** / **C** | Stop / clear orders; both clear the selected units' queues |
| **W** | Cycle the active weapon of a selected unit with multiple weapons |
| **K** | Toggle cloak for selected units that can cloak |
| **O** | Open / close selected gates |
| **D** | Open diplomacy: give eligible selected units, toggle outgoing mana sharing with teammates, and choose chat recipients |
| **Ctrl+Shift+D** | Toggle self-destruct for selected units; press again to cancel |

### Selection

These shortcuts select your own units. Category selections replace the current
selection. **N** requires a current selection to cycle from.

| Key | Select |
| --- | --- |
| **Ctrl+A** | All your units |
| **Ctrl+Z** | All units of every type in the current selection |
| **Ctrl+U** | All your units on screen |
| **Ctrl+X** | On-screen units matching the first selected unit's type |
| **Ctrl+M** | Your monarch, and follow it with the camera |
| **Ctrl+B** | Builders, including builder structures |
| **Ctrl+F** | Factories / builder structures |
| **Ctrl+E** | Mobile melee units |
| **Ctrl+G** | Mobile magic users with a personal mana pool |
| **Ctrl+N** | Boats / water-domain units |
| **Ctrl+R** | Units with ballistic weapons |
| **Ctrl+T** | Armed mobile troops, excluding boats and monarchs |
| **Ctrl+W** | Armed units, excluding monarchs |
| **Ctrl+Y** | Flying units |
| **N** | Next unit |

### Groups and formations

| Key | Action |
| --- | --- |
| **Ctrl+1–0** | Assign the selection to a group |
| **Alt+1–0** | Assign the selection to a formation |
| **Ctrl+Shift+1–0** / **Alt+Shift+1–0** | Append to a group / formation |
| **1–0** | Select the group or formation; press again to track it; **0** is squad 10 |
| **Ctrl+Esc** | Remove selected units from their squads |

A unit belongs to one squad at a time. Each number holds either a group or a
formation; formations move at their slowest member's speed. Recall skips
builders, but units they produce inherit their squad.

### Camera, information, and game controls

| Key | Action |
| --- | --- |
| **Arrow keys** | Pan the camera |
| **T** | Toggle camera tracking of the selection |
| **Tab** | Toggle the full-screen map; press again to return |
| **F1** | Toggle unit information; **Enter** or **Esc** also closes the dialog |
| **F4** | Toggle player names, kills, losses, and score |
| **F9** | YouTube streaming setup / status |
| **O**, with no units selected | Toggle campaign objectives when available |
| **Pause** | Pause / resume the game or replay |
| **+** / **−** (also **=** and keypad **+/−**) | Change game / replay speed |
| **Enter** (also keypad Enter) | Open chat; press again to send; **Esc** cancels |
| **Esc** | Close unit information, cancel placement / an armed order, clear selection, then open the game menu as applicable |
| **Shift+D** | Monarch disco emote |
| **Shift+H** | Monarch headbang emote |

Live game speed ranges from 0.5× to 4×. Only the host can change it, and the
game creation screen’s **Allow speed change** option must be enabled (the lobby
only displays it). Chat is available in live networked games (single-player also uses a local server), not replay playback.

### Mouse and construction

| Input | Action |
| --- | --- |
| **Left-click / left-drag** | Select a unit / box-select |
| **Shift** + selection | Add to the selection |
| **Ctrl** + selection | Remove from the selection |
| **Right-click** | Contextual move, attack, or other applicable order; **Shift** queues |
| **Middle-drag / screen edges** | Pan the camera |
| **Mouse wheel** | Zoom toward the cursor |
| **Minimap left-click / drag** | Move the camera; an armed order instead targets that location |
| **Minimap right-click** | Move the selection there; **Shift** queues |
| **Build icon left-click / right-click** | Add / remove one queued unit at a training building |
| **Shift** / **Ctrl+Shift** + build-icon click | Add or remove five / ten queued units |
| **Ctrl+left-click** a build icon | Start infinite production; toggle it at stationary producers |
| **Left-click** while placing | Place the construction site |
| **Shift+left-click / drag** while placing | Queue a site / a line of sites |
| **Right-click / Esc** while placing | Cancel placement |
| **Right-drag** with a reclaim-capable mobile builder | Reclaim an area; **Shift** appends the sweep |

Mobile builders use build icons to arm placement instead of ordinary factory
queues. A reclaim-capable builder automatically clears trees, rocks, and other
reclaimable obstacles from a valid site before construction starts. Terrain,
other units, and unreclaimable obstacles can still prevent placement.

To upgrade a lodestone, choose your faction's advanced version and place it on
your completed basic lodestone's mana spot. Its larger footprint must fit. The
old building dissolves during the first half of construction; the new one
materializes during the second half. The upgrade uses the normal advanced cost
and build time, needs no extra unit-cap slot, and produces no mana until complete.
Cancellation or destruction does not restore the consumed basic lodestone.

While a mobile builder runs an infinite queue, **Move** and **Patrol** set rally
orders for its produced units; **Shift** appends rally steps and displays the route.
**Stop** cancels production and restores normal control of the builder. See the [user guide](docs/user-guide.md#controls)
for construction and reclaim details.

## Multiplayer and replays

Choose a server through the multiplayer menu, sign in, then create or join a
game. An unused account name is registered on first sign-in. Players connect
to the server; only the server needs an incoming network port available.
Single-player starts its own private server automatically.

Multiplayer automatically verifies and transfers missing or differing maps before play.
Downloaded maps remain available in the map picker; generated maps are saved on every
participant and the server when the match starts. See [map sharing and storage](docs/map-transfer.md).

Allies share vision automatically. Excess mana is distributed to allied players
with the lowest storage fill percentage first, among teammates you allow in
**D — Diplomacy**. Mana sharing starts enabled for teammates; chat starts enabled
for everyone and controls who receives your messages. The dialog lists all players,
with gifts and mana sharing unavailable for opponents. Gift buttons are disabled
without eligible selected units. Giving units keeps the dialog open and updates
the buttons. Ineligible units remain yours; transfers
respect the recipient's unit limits, remove transferred units from your control
groups, and change them to the recipient's colors. Campaigns do not offer gifting.
See [unit gifting](docs/unit-gifting.md) for eligibility details.

Choose the per-player unit limit on the game-creation screen: the button cycles
through 250, 500, 1,000, and 2,000. The lobby displays this value without allowing
it to change.

Use the **same engine build and compatible game data** on every participant.
Version **0.7.12 uses protocol 195** for directed mana sharing and chat.
Versions **0.7.10 and 0.7.11 use protocol 194**. Restart/update both client and
server together. **Released versions 0.7.9 and 0.7.8 use protocol 189** for script-controlled corpse selection
and death lifetimes. Version **0.7.7 uses protocol 187**; versions 0.7.6 and
0.7.5 use protocol 184. These builds cannot share a match. Older incompatible
clients and recordings are rejected.
The connection checks gameplay definitions, scripts, and models. Selected map
contents are verified separately and transferred automatically when needed.

Clients record matches in their per-user application data directory beside
`settings.ini`. Replays contain commands and match setup, not game assets.
[Hosting and accounts](docs/user-guide.md#multiplayer) and
[replay playback](docs/user-guide.md#replays) are covered in the user guide.

## Display and performance

Set **Options → Player Name** and click **Save** to choose your name for new
local and unauthenticated multiplayer games. Account-based multiplayer uses
your login name.

Use **Options** to adjust audio, anti-aliasing, filtering, shadows, health bars,
UI scale, cursor size, and camera behavior. **Smooth GUI Art** requires a restart;
**Smooth Movies** smooths the menu clips as they play.

The shadow option controls unit, scenery, and projectile shadows. Shadows follow
animated poses, including swaying trees; shading baked into terrain artwork
remains visible. Accelerated renderers use cached shadow silhouettes and tiles,
with fallbacks where needed. Animated boat shadows are an intentional enhancement
over retail Glide, controlled by the same Shadows option.

At distant zoom, tiny stationary units can use cached body images; moving,
selected, and special-effect units retain their full geometry. Wide-map rendering
also reuses fog geometry and combines fog cells over flat terrain. See the
[distant rendering measurements](docs/distant-rendering-performance.md) for
limits and local comparisons.

Unit scripts control corpse selection and the handoff from death animation to
wreck. Buildings also use the authored palette shading and per-piece shadow
flags observed in retail Glide.

The in-game stats panel shows **Real Time**, **Game Time**, **Client CPU**
(the game process’s share of total CPU capacity), and whole-system GPU usage. When the client
launches a local server for a skirmish or campaign, **Server CPU** shows that
server process’s share of total CPU capacity (0–100%). It is omitted for remote
servers; unavailable samples display N/A.
NVIDIA statistics query the GPU through the driver-provided NVML library
on Windows and Linux, without launching `nvidia-smi` or bundling extra libraries.
AMD Linux uses amdgpu sysfs, Intel Linux uses i915 perf counters (subject to kernel
permissions), and macOS uses IORegistry. Windows AMD/Intel use Windows GPU Engine
counters across all processes, reporting the busiest engine at 0–100%; adapter
identity and dedicated memory come from DXGI and Windows memory counters.
These use built-in Windows libraries, with no extra downloads. Unsupported
counters (including Linux Xe utilization) display N/A. On multi-GPU systems,
NVIDIA uses its first adapter; the Windows fallback reports the busiest adapter.

**Settings → Benchmark** provides a repeatable eight-AI load test and reports
frame rate, simulation speed, memory, and other available performance metrics.
Client and server CPU results each use 0–100% of total machine CPU capacity.
GPU results use the same 0–100% whole-device measurement as the stats panel;
unavailable measurements display N/A.
Testing targets up to **16,000 total units**, not a guarantee of real-time play
at that population. Map, unit mix, orders, hardware, and graphics settings all
matter. See the [large-map collision-grid results](docs/large-map-collision-performance.md),
[moving-army, shadow and simulation results](docs/performance-2026-09-28.md),
[geometry and shadow reuse results](docs/render-reuse-2026-09-26.md),
[rendering and animation improvements](docs/render-submission-2026-09-26.md),
[engine profiling results](docs/performance-2026-09-26.md), and
[earlier performance notes](docs/performance-2026-09-20.md).

## Project status

TAK Engine is playable and under active development. The aim is an accurate
representation of retail behavior; this is not a claim that every mission,
animation, or interaction is identical or fully verified.

Selected movement, combat, transport, scripting, and animation routines are
checked against the original executable through headless emulation. Ground
pathfinding parity is a continuing constraint on engine changes. The skirmish
AI is this project's implementation, rather than a reproduction of retail AI.

For the evidence and remaining scope, see the [retail engine notes](docs/retail-engine.md),
[pathfinding work](docs/pathfinding-port.md),
[guessed-behavior corrections](docs/guessed-fallback-audit-2026-09-26.md),
[campaign implementation and comparisons](docs/campaign-design.md),
[transport and animation audit](docs/transport-animation-audit-2026-09-21.md), and
[naval building placement correction](docs/naval-building-placement.md).

## YouTube streaming

Open **Settings → YouTube Streaming** at the title screen, **Esc → YouTube
Streaming** in a game, or press **F9**. Copy your YouTube Studio stream key,
click **Paste**, and then **Start Streaming**. The key is masked, stays in memory,
and is excluded from captures and diagnostics.

The defaults are **Max 3840** and **60 FPS**. Max 3840 scales down to at most
3840 pixels wide while preserving your window's aspect ratio: a 7680×2160
window sends 3840×1080. Smaller windows are not enlarged. Output dimensions
are fixed when you start; stop and restart to change them. Other choices are
720p, 1080p, 1440p, 4K, and Full Resolution, with 30 or 60 FPS.

Bitrate is adjustable from **3,000 to 80,000 Kbps**; the initial value is
**6,000 Kbps**. Increase it for higher-resolution video as your upload permits.
The highest selectable value is not a YouTube recommendation. Full Resolution
can exceed YouTube's accepted dimensions even when your GPU can encode it;
use Max 3840 or 4K if YouTube reports an unsupported resolution.

Automatic encoding tries NVIDIA, AMD, Intel, or Apple hardware, with CPU fallback
at smaller sizes and hardware HEVC for dimensions beyond common H.264 limits.
The panel shows the encoder actually used. **Dropped** counts missed output
frames; **Replaced** counts superseded capture submissions. Rendering slowdowns
skip late frames without forcing reconnects. With VSync enabled, a refresh-rate
pacing fallback keeps streaming from uncapping presentation.

Closing the panel or returning to the title screen keeps the stream running.
Stop it with **Stop Streaming** or exit the application. Game audio is included;
title and lobby music is included too. Loading/modal screens retain the last
captured frame. Microphone capture is not included.

FFmpeg, its codecs, and additional non-system dependencies are linked statically;
no separate FFmpeg installation is required. GPU encoding uses the installed
system driver. See [streaming setup and platform details](docs/streaming.md).

## Build and contribute

Source builds use C++20, CMake, and the bundled static dependency builds.
Start with the [development guide](docs/development.md) for dependencies,
platform instructions, tests, developer launch modes, and release packaging.
After simulation, AI, or networking changes, rebuild all targets so client
and server remain in sync.

Bug reports are most useful with the engine version, platform, map, balance
setting, steps to reproduce, and a replay or screenshot where relevant.
Do not attach retail game archives or executables.

## License

The engine is **GPL-3.0-or-later**; see [LICENSE](LICENSE).
The original game's code, data, and art remain their owners' property.
This repository distributes engine source, not the retail game content.
