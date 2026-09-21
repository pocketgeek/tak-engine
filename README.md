<div align="center">

# ⚔️ TAK Engine

**A modern, cross-platform re-creation of _Total Annihilation: Kingdoms_**

_Cavedog's 1999 fantasy RTS — reborn in clean-room C++20 / SDL2, in the spirit of OpenRA and the Robot War Engine._

[![version](https://img.shields.io/badge/version-0.7.0-c9a227?style=flat-square)](https://github.com/pocketgeek/tak-engine/releases)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599c?style=flat-square&logo=cplusplus&logoColor=white)](CMakeLists.txt)
[![platforms](https://img.shields.io/badge/platforms-Linux%20·%20Windows%20·%20macOS-4c8c4a?style=flat-square)](#download)
[![multiplayer](https://img.shields.io/badge/multiplayer-deterministic%20lockstep-b03a2e?style=flat-square)](#multiplayer)
[![license](https://img.shields.io/badge/license-GPL--3.0-6c3483?style=flat-square)](LICENSE)

<br>

<img src="docs/img/title.jpg" width="70%" alt="TAK Engine — the retail three-door front-end, rebuilt from scratch">

<br><br>

<table>
  <tr>
    <td width="50%"><img src="docs/img/ingame.jpg" alt="The built-in 8-AI benchmark in flight: a monarch on the battlefield, thousands of units in play, a live scoreboard and the run countdown"></td>
    <td width="50%"><img src="docs/img/gameplay.jpg" alt="A skirmish in progress — a monarch, its keep and an army, under the retail command HUD"></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/img/benchmark.jpg" alt="The benchmark results screen: per-10s CPU, memory, frame rate, sim speed, GPU utilisation and VRAM for client and server"></td>
    <td width="50%"><img src="docs/img/lobby.jpg" alt="The multiplayer / single-player lobby: 8 slots, teams, colours, factions, unit cap and speed options"></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/img/disco.jpg" height="260" alt="A monarch dancing on a glowing disco floor"><br><em>Monarchs can disco… <code>Shift+D</code></em></td>
    <td align="center"><img src="docs/img/headbang.jpg" height="260" alt="A monarch headbanging in a red mosh-pit glow"><br><em>…and headbang to synth-metal. <code>Shift+H</code></em></td>
  </tr>
</table>

<sub>Thousands of units on screen · deterministic lockstep MP · animated 3D models · a full retail-style HUD · a built-in benchmark · and, yes, dancing kings.</sub>

</div>

---

A modern, cross-platform engine recreation for **Total Annihilation: Kingdoms**
(Cavedog Entertainment, 1999), in the spirit of OpenRA and Robot War Engine.

**Version 0.7.0** — reported by `takclient --version` and `takserver --version`
(and shown in the window title / server banner). The release version is set in
one place, `project(... VERSION ...)` in `CMakeLists.txt`, and is separate from
the multiplayer wire protocol version, which is gated independently at connect.

### Download

Latest pre-built binaries (self-contained; you still supply your own retail game
data — see **Game data**):

- **Windows x64** — the `tak-engine-<version>-windows-x64-setup.exe` installer (Start-menu shortcuts + uninstaller), or the plain `takclient-<version>-windows-x64.zip`
- **macOS (Apple Silicon)** — the `tak-engine-<version>-macos-arm64.dmg` disk image (drag *TAK Engine* to Applications; right-click → Open the first time), or `takclient-<version>-macos-arm64.zip` (contains the same *TAK Engine.app* — launch that, not the bare `takclient`, or Finder opens a Terminal window)
- **Ubuntu 22.04 / 24.04 / 26.04** — `tak-engine-<version>-ubuntu<rel>-amd64.deb`, then `sudo apt install ./tak-engine-*.deb`
- **Debian 12 / 13** — `tak-engine-<version>-debian<rel>-amd64.deb`, then `sudo apt install ./tak-engine-*.deb`
- **Fedora 44** — `tak-engine-<version>-fedora44-x86_64.rpm`, then `sudo dnf install ./tak-engine-*.rpm`
- **Arch** — `tak-engine-<version>-1-x86_64.pkg.tar.zst`, then `sudo pacman -U ./tak-engine-*.pkg.tar.zst`
- All from the [latest release](https://github.com/pocketgeek/tak-engine/releases/latest) · [all releases](https://github.com/pocketgeek/tak-engine/releases) · or build from source below.

Each release also attaches per-platform **debug** binaries (`*-debug`) — the same
`takclient`/`takserver` *without* the release CLI/env hardening, so developers get the
launch modes, dev flags, `TAK_*` env hooks, and the headless `--mp*` harness.

Linux packages install `takclient`, `takserver`, and `cartographer` to `/usr/bin`.
The game libraries are bundled statically; normal system windowing, audio, and
graphics support is still required. See [Building](#building) for details.

> **This project contains no game content.** You must own the original game
> (e.g. the GOG release of *Total Annihilation: Kingdoms + The Iron Plague*); the
> engine reads its install directory directly (see **Game data**), and any local
> copy of that content stays gitignored.

Much of the behaviour was cross-checked by disassembling the retail engine
(`KINGDOMS.icd`); see `docs/retail-engine.md` for the findings (class model,
config schema, and the veterancy/build formulas read out of the binary).

## Status

The engine is playable, with skirmish AI, campaign/scenario support, multiplayer,
replays, and a map editor. It is still under active development; support for a
feature does not mean every retail behavior or mission has been verified.

- **Game data and tools:** HPI, GAF/TAF, TNT, 3DO, COB, TDF/FBI/OTA, CRT,
  fonts, and audio loaders; debug asset viewers and standalone inspection tools.
- **Simulation:** terrain-aware ground, boat, and flying movement; combat,
  construction and production; mana economy; unit scripts; veterancy, status
  effects, reclaim, capture, resurrection, and gods.
- **Skirmish and multiplayer:** up to eight player slots, five factions, five AI
  difficulties, retail and Crusades balance, generated maps, fog of war, teams,
  spectators, and deterministic lockstep with a server referee.
- **Presentation:** animated 3D units, retail-style menus and HUD, Bink door
  videos, projectile models and effects, shadows, and positional audio.
- **Campaigns and editing:** mission scripts and scenario triggers, plus
  Cartographer's terrain, object, scenario, and map-bundle tools.

Movement and other selected routines are compared against the retail executable
with emulation-based checks. The AI is this project's implementation, not a
complete reproduction of retail AI. See [retail-engine.md](docs/retail-engine.md)
and [pathfinding-port.md](docs/pathfinding-port.md) for the scope and evidence.

Rendering uses worker threads, culling, texture atlases, batched shadows, streamed
terrain, and texture-budget controls. Simulation uses spatial queries, bounded
path searches, and selected parallel work. Performance still depends on the map,
unit mix, orders, hardware, and build configuration. Stress testing targets up to
**16,000 total units** (2,000 per player); that is not a guarantee of real-time
simulation at that population. See [performance notes](docs/performance-2026-09-20.md).

Naval combat checks sight across the water surface and shore, while retaining
obstacle and raised-terrain blocking. Ships without a separate pivot rate use
their turning rate to face attack targets after stopping in firing range.

## Building

Use CMake ≥ 3.24, a C++20 compiler (GCC/Clang or MinGW-w64), Ninja, Git,
Make, and pkg-config. Linux and macOS builds require the vendored static
zlib, libjpeg-turbo, SDL2, and Bink-only FFmpeg libraries. Installing a system
SDL2 package alone is not sufficient.

On Linux, install development headers for the SDL video/audio backends you need
(X11/Wayland, ALSA/PulseAudio, OpenGL/EGL). Backends whose headers are absent
when SDL is built may be unavailable. Linux also requires the static C++ runtime
archives; on Fedora these include `libstdc++-static`. The exact package lists
used by CI are in [linux.yml](.github/workflows/linux.yml).

From the repository root:

```sh
./tools/build-ffmpeg-bink.sh
./tools/build-static-deps.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

The dependency scripts download their sources and reuse existing installations
under `third_party/`. Their `PREFIX` environment variable sets the destination;
pass matching `-DTAK_FFMPEG_PREFIX=...` and `-DTAK_STATIC_DEPS_PREFIX=...`
CMake options when using custom locations. Missing required libraries fail
configuration rather than silently switching to shared libraries.

For developer launch modes, diagnostics, and headless harnesses, use a Debug
build. An optimized Debug build keeps those features while improving performance:

```sh
cmake -S . -B build-o2 -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS_DEBUG="-O2 -g" -DTAK_TEST_DATA=/path/to/tak_install
cmake --build build-o2 -j4
ctest --test-dir build-o2 --output-on-failure
```

`TAK_TEST_DATA` enables retail-data tests in addition to the data-independent
suite. Some animation checks also require extracted scripts under
`assets/extracted/all/scripts`. Rebuild **all targets** after simulation, AI, or
network changes so the client, server, and tools use the same code.

### Static dependencies and menu videos

Menu door clips use Bink video decoded by the bundled, minimal FFmpeg build.
No separate FFmpeg installation is needed at runtime. FFmpeg is required for a
source build; a missing or unreadable clip can fall back to static menu art.
The dependency script enables Bink decoding without GPL codecs.

SDL2, libjpeg, zlib, and FFmpeg are linked statically. Linux also embeds the
GCC/C++ runtime but retains system C libraries; SDL loads available windowing and
audio backends at runtime. Windows binaries need no separately bundled SDL,
JPEG, zlib, FFmpeg, or MinGW runtime DLLs. macOS uses system libraries and
frameworks. CI checks these dependency boundaries for release packages.

### Platform builds and releases

- **Windows x64:** use the MSYS2 **MINGW64** environment with its GCC, CMake,
  Ninja, SDL2, libjpeg-turbo, zlib, and pkgconf packages, plus Git and Make.
  Run `./tools/build-ffmpeg-bink.sh`, then configure and build as above.
  CMake uses the toolchain's static dependency archives; the native
  `build-static-deps.sh` step is not needed. See
  [windows.yml](.github/workflows/windows.yml) for the exact package list and
  installer build.
- **macOS ARM64:** install Xcode Command Line Tools, then
  `brew install cmake ninja pkg-config`. Run both dependency scripts and the
  source-build commands above, adding `-DCMAKE_FIND_FRAMEWORK=LAST` at configure
  time. See [macos.yml](.github/workflows/macos.yml) for app/DMG packaging.
- **Linux x64:** CI packages Ubuntu 22.04/24.04/26.04, Debian 12/13,
  Fedora 44, and Arch; use the package matching your distribution.

The platform workflows build Release and selected Debug artifacts on every
`main` push. The determinism workflow runs for relevant source changes; Windows
and macOS also check the math golden hash natively. Retail-data tests run locally
because the game assets are not included in the repository or CI.

To cut a release, update the CMake project version and README version, commit and
push, and wait for platform checks to pass. Then tag that commit and push the tag:

```sh
git tag -a vX.Y.Z -m "TAK Engine X.Y.Z"
git push origin vX.Y.Z
```

Tags matching `v*` trigger package builds and GitHub Release uploads. Check every
platform job and the complete asset set, then update the release notes; workflows
can create/publish the release with generated notes during upload.

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
[src/client/main.cpp](src/client/main.cpp) includes additional harness options. `--overrides` defaults to `full` (a release build always mounts `full`):
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

Units are drawn from their full 3D models throughout, with the per-unit projection
spread across a worker pool and each colour's textures packed into a single atlas, so
a large army still costs only a handful of draw calls. Background music plays from the
faction soundtrack.

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

  Sign-in uses SCRAM-SHA-256-style challenge/response over the game's binary
  framing. The server stores salts and derived keys rather than passwords;
  password derivation uses 600,000 PBKDF2-HMAC-SHA256 iterations. Authentication
  does not encrypt game traffic, and first-time account registration has no
  established server identity to authenticate against. See
  [src/net/auth.h](src/net/auth.h) for the protocol and its limits. Repeated
  failures trigger account/address lockouts.

  Accounts live in one plain-text file (`--accounts`, default
  `takserver-accounts.conf`), written owner-read/write on POSIX and replaced atomically —
  no database. New passwords must be at least 8 characters; names are 3-20
  characters of letters, digits, `_`, `-` or `.`, unique case-insensitively.
  `tools/authtest.cpp` checks the primitives against the published FIPS/RFC test
  vectors and drives the exchange through replay, downgrade and stolen-file
  attacks.

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

See `docs/multiplayer-design.md` for the full design, and `docs/detmath-scope.md`
for the determinism contract. (The old 2-player `--host`/`--join` peer mode is
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
compatible engine behavior and game data. Version 0.7.0 uses protocol **177** for the naval combat fixes.
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
See [cartographer-port.md](docs/cartographer-port.md) for implementation notes.

## Project layout

| Path | Contents |
| --- | --- |
| `src/hpi/` | HPI archive reader (TAK's revised format vs. classic TA) |
| `src/gaf/` | GAF/TAF sprite, animation, and font decoding |
| `src/video/` | `.bik` (Bink Video) decoding for the menu door clips (FFmpeg-backed) |
| `src/tnt/` | TNT map decoding |
| `src/tdo/` | 3DO model loading |
| `src/cob/` | COB script bytecode VM (unit animation/scripting) |
| `src/tdf/` | TDF/FBI/OTA text-config parsing |
| `src/crt/` | `.crt` scenario/trigger parsing |
| `src/campaign/` | campaign spine (`camps/*.tdf`) + in-sim mission/god-script runner |
| `src/sim/` | deterministic simulation (movement, pathfinding, combat, economy) |
| `src/net/` | multiplayer wire format, framed TCP, client protocol |
| `src/server/` | `takserver`, the headless lobby + lockstep relay |
| `src/ai/` | the skirmish AI (server-portable; emits commands) |
| `src/terrain/` | terrain / palette handling |
| `src/util/` | shared helpers |
| `src/gui/` | retail `.gui` HUD/gadget layout parsing |
| `src/client/` | the SDL2 app (`takclient`: asset viewer + game) |
| `src/cartographer/` | `cartographer`, a clean-room port of the retail map editor (in progress) |
| `tools/` | CLI dev tools (`hpitool`, `gaftool`, `tnttool`, `modeltool`, `cobtool`, `tdftool`, `missiontool`, `biktool`, `aitool`) |
| `docs/` | format notes + reverse-engineering findings (`retail-engine.md` = the `KINGDOMS.icd` disassembly) |

## License

TAK Engine is free software, licensed under the **GNU General Public License,
version 3 or later** (`GPL-3.0-or-later`) — see [`LICENSE`](LICENSE) for the full
text. Copyright © 2026 the TAK Engine authors.

This covers the engine's own source code only. It grants no rights to *Total
Annihilation: Kingdoms* itself: Cavedog's game code, data, and art remain their
owners' property; this project ships none of them and reads them only from a copy
you already own (see **Game data**).
