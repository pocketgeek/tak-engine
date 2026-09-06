# TAK Engine

A modern, cross-platform engine recreation for **Total Annihilation: Kingdoms**
(Cavedog Entertainment, 1999), in the spirit of OpenRA and Robot War Engine.

**Version 0.0.1** — reported by `takview --version` and `takserver --version`
(and shown in the window title / server banner). The release version is set in
one place, `project(... VERSION ...)` in `CMakeLists.txt`, and is separate from
the multiplayer wire protocol version, which is gated independently at connect.

### Download

Latest pre-built binaries (self-contained; you still supply your own retail game
data — see **Game data**):

- **Windows x64** — [takview-windows-x64.zip](https://github.com/pocketgeek/tak-engine/releases/latest/download/takview-windows-x64.zip)
- **macOS (Apple Silicon)** — [takview-macos-arm64.zip](https://github.com/pocketgeek/tak-engine/releases/latest/download/takview-macos-arm64.zip)
- [**All releases**](https://github.com/pocketgeek/tak-engine/releases) · or build from source below.

These always resolve to the newest [release](https://github.com/pocketgeek/tak-engine/releases);
they appear once the first tagged release finishes building. Linux users build from source.

> **This project contains no game content.** You must own the original game
> (e.g. the GOG release of *Total Annihilation: Kingdoms + The Iron Plague*); the
> engine reads its install directory directly (see **Game data**), and any local
> copy of that content stays gitignored.

Much of the behaviour was cross-checked by disassembling the retail engine
(`KINGDOMS.icd`); see `docs/retail-engine.md` for the findings (class model,
config schema, and the veterancy/build formulas read out of the binary).

## Status

Every stage is complete:

1. ~~**Format tooling**~~ — HPI v2, GAF/TAF, TNT, 3DO, COB, TDF/FBI/OTA, GAF
   fonts, WAV all parse.
2. ~~**Asset viewer**~~ — `takview map` / `takview model` (textured, COB-animated).
3. ~~**Simulation**~~ — movement, A* pathfinding, combat, mana economy,
   production, per-unit COB VMs, sound.
4. ~~**Skirmish game**~~ — playable vs AI: fog of war, minimap, building
   placement, production, player colours, faction select, a classic HUD, and
   `Keys.TDF` hotkeys.
5. ~~**Campaign**~~ — mission loading via `.ota`/`.cob` with the `MAP_COMMAND`
   scripting API and `.crt` scenario/trigger parsing.
6. ~~**Multiplayer**~~ — client–server deterministic lockstep for up to 8
   players/teams, cross-build deterministic. See [Multiplayer](#multiplayer).
7. ~~**Combat & unit depth**~~ — the FBI/weapon data is driven faithfully: HP
   regen, veterancy (kills → +10 %/level attack·armour·reload, gold sheen,
   promoted `veteranmodel`), per-unit mana pools & mana-per-shot, area-of-effect
   splash + per-target-category damage, status weapons (freeze / petrify /
   paralyze / mind-control) with immunities, cloaking, reclaim / resurrect /
   capture, `AdjustArmor`/`AdjustAttack` auras, terrain-class movement
   (`MOVEINFO.tdf` slope/water limits + water/road speed), radar sight,
   line-of-sight firing, flow-field group movement, and a summonable-god economy.
8. ~~**Effects & audio**~~ — real GAF/TAF explosion, splash, shockwave-ring,
   ground-fire and muzzle-flash effects; material-specific impact sounds; unit
   shadows; camera shake; positional/surround audio.
9. ~~**Rendering at scale**~~ — thousands of units on screen, smoothly. The
   per-unit model projection runs across a worker pool; units are frustum-culled;
   each colour's textures are packed into one atlas so an army is a handful of
   draw calls; and each unit's walk/fly cycle is baked to an **animated sprite
   sheet** (16 facings, real cycle timing) drawn as a single quad — the classic
   RTS trick — with the full 3D model kept for close-ups and attack/death poses.
   The sim is O(n) (spatial-hash neighbour queries, staggered acquisition), so a
   thousand-unit battle is CPU-cheap too.

## Building

Requires CMake ≥ 3.24, a C++20 compiler, and Ninja. SDL2 is found on the system
or fetched automatically.

```sh
cmake -B build -G Ninja
cmake --build build
```

### Cross-platform builds

The engine builds for **Linux**, **Windows 11 (x64)**, and **macOS (Apple
Silicon / ARM64)** from one source tree — the net layer abstracts POSIX sockets
vs Winsock in `src/net/netcompat.h`, and process launch is the only other
platform split (`fork`/`exec` vs `CreateProcess`, in `src/viewer/main.cpp`).

- **Windows, cross-compiled from Fedora** with MinGW-w64:

  ```sh
  sudo dnf install mingw64-gcc-c++ mingw64-SDL2 mingw64-zlib mingw64-libjpeg-turbo
  mingw64-cmake -B build-win -G Ninja -DBUILD_SHARED_LIBS=OFF
  cmake --build build-win
  ```

  The GCC/C++ runtime is static-linked, so a Windows box needs only the exes plus
  `SDL2.dll`, `libjpeg-62.dll`, and `zlib1.dll` (from the mingw sysroot `bin/`)
  in the same folder.

- **macOS ARM64** (on a Mac): `brew install ninja sdl2 jpeg-turbo`, then
  `cmake -B build -G Ninja` and `cmake --build build`.

CI builds both in `.github/workflows/`: `windows.yml` (MSYS2/MinGW on a Windows
runner) and `macos.yml` (native `macos-14` Apple-silicon runner) each upload a
binary artifact and run the determinism gates. `determinism.yml` guards
cross-build lockstep for the sim.

## Game data

Point the engine straight at a **retail install directory** with `--data` -- no
extraction step. It reads the shipped archives and folders in place:

```
<install>/
  *.hpi              the shipped archives (data, terrain, maps, sections,
                     english, the IP* Iron Plague expansion, community packs…)
  Maps/              downloadable maps as *.kmp (each an HPI) + loose maps
  Music/             track*.wav soundtrack
  overrides/         YOUR overrides -- loose files or *.hpi/*.kmp, highest priority
```

Only the `*.hpi` in the install root are read (loose files there are ignored);
maps come from `maps.hpi` and the `Maps/*.kmp`, music from `Music/`, and anything
in `overrides/` wins over everything.

**HPI precedence.** The retail game shipped each update as a new HPI/UFO that
superseded older copies of a file, and the engine reproduces the exact rule
(reverse-engineered from `KINGDOMS.icd`): a loose file wins; otherwise, across all
`*.hpi` then `*.ufo`, the copy whose archive entry has the **newest date** wins
(ties keep the earlier-mounted). So dropping a newer patch archive (e.g.
`V3Rocket.hpi`) into the install Just Works. `hpitool where <dir> <path>` shows
which archive a file resolves to; the offline `hpitool merge` still bakes a flat
tree if you want one.

## Playing

The engine is **client-server only** — every game runs on a `takserver`, and the
AI runs *only* on the server. Single-player is just a private game on a server the
client starts for you.

`takview game` takes a **map name** and the install directory:

```sh
./build/takview game "King of the Hill" --data /path/to/tak_install \
    --side ara --aiside tar
```

With no `--server`, this **auto-launches a private local server** in the
background, connects to it, and starts a single-player game vs a server-run AI
(`--side` / `--aiside` pick the factions). The local server is torn down when you
quit, and the game is never visible to other players. Add `--server <host>` (see
below) to play on a shared server instead.

`--overrides {none,cosmetic,full}` chooses which of your `overrides/` are mounted
(default `full`): `none` = pure retail, `cosmetic` = only art/sound/music (never
affects a multiplayer game), `full` = everything including gameplay data.

Each side begins with **only its Monarch**, dropped on the map's real start
positions (from the `.ota`). The Monarch trickles mogrium and builds the first
lodestones and keep, which then train the army — the AI opponent bootstraps the
same way.

### Options

| Flag | Effect |
| --- | --- |
| `--side ara\|tar\|ver\|zon\|cre` | your faction (Zhon has no Keep — its Monarch and Beast Handlers summon creatures; Creon needs the Iron Plague data) |
| `--aiside <faction>` | the AI opponent's faction |
| `--color N` / `--aicolor N` | player-colour variant (0–9), independent of faction |
| `--crusades` | the **Crusades balance** — the alternate stats and build menus the final patch shipped for ranked "Darien Crusades" play (`unitscb`/`canbuildcb`, layered over the base roster) |
| `--cheat` | all construction/production is instant and free |
| `--demo` | stage a ready-army AI-vs-AI war instead |
| `--mission <.ota>` | load a campaign mission (`.ota`/`.cob`) |
| `--server <host>` … | join a multiplayer game — see [Multiplayer](#multiplayer) |
| `--maxfps N` / `--novsync` | frame cap (`0` = uncapped) / disable vsync (title shows live FPS) |

In a god-enabled match, a faction whose priests (`attractsgods` units) have
channelled enough mana favour manifests its **god** once the appear time passes
(`TAK_GODTIME=<seconds>` shortens it for testing).

### Controls

Hotkeys follow the game's `Keys.TDF`.

| | |
| --- | --- |
| **Select** | drag = box-select · **Ctrl+A** all your units · **Ctrl+Z** all of that type · **Ctrl+U** everything on screen · **N** cycle to next unit |
| **Order** | right-click = move/attack (**Shift** queues) · **F** fight-move · **M** move · **A** attack · **P** patrol · **G** guard · **S** stop · **Ctrl+D** destroy · **Esc** cancel an armed order |
| **Groups** | **Ctrl+1–0** assign · **1–0** recall · **Ctrl+Shift+1–0** add to a group |
| **Camera** | arrows / middle-drag / **screen-edge** scroll · wheel zoom (toward cursor) · minimap click/drag = move the camera · right-click minimap = move the selection there |
| **Minimap orders** | with an order armed (**F**/**M**/**A**/**P**/**G**), click the minimap to issue it at that spot — e.g. **F** then a minimap click = fight-move across the map |
| **Build queue** | at a training building: left-click **+1**, **Shift** **+5**, **Ctrl+Shift** **+10**; right-click removes the same; **Ctrl**+left toggles infinite production. Each icon shows its queued count. (A builder that *places* things — structures, or a mobile conjurer like a Beast Handler — arms placement instead: click to position.) |
| **Reclaim** | with a mobile builder (any unit with `canreclaim`, monarchs included) selected, **right-click-drag** a box to clear it — the builder roams the area reclaiming trees, rocks, and buildings for mana (nearest first). Sacred Stones and Standing Stones are left alone. **Shift** appends the sweep to its orders. |
| **Game** | **Pause** · **+/−** game speed (single-player: −10…+10, 0 = normal, +10 = 10×; in a net game only the **host** can change it, and only if the lobby's *in-game speed* is unlocked) · **F4** status/scoreboard · **F6** player-colour picker |
| **Disco** 🪩 | **Shift+D** — your monarchs spin, bob, hue-cycle, and glow on a little dance floor for 10s, to a synthesised disco track that plays positionally from the monarch. Purely cosmetic, but synced over the lockstep so every player sees it. |
| **Headbang** 🤘 | **Shift+H** — your monarchs headbang to a synthesised heavy-metal track (positional, from the monarch), nodding and flashing red on a mosh-pit glow for 10s. Also cosmetic and lockstep-synced. |

Rendering toggles (all default to the smart option): **F10** cycles sprite-sheet
rendering **AUTO** → **ON** → **OFF** (AUTO drops to the cheaper animated sprites
only while a real crowd can't hold 60 fps, then back to 3D); **F8** toggles the
distance-impostor LOD (a cached billboard for units that are tiny on screen).
Background music plays from the faction soundtrack.

## Multiplayer

Multiplayer is **client–server**: everyone connects out to one central
`takserver`, so there's no NAT or port-forwarding on the players' side. The
server relays a **server-sequenced deterministic lockstep** — up to 8 players on
up to 8 teams (allies share vision and economy), every machine running the
identical sim with only ~35-byte commands on the wire.

```sh
# somewhere reachable (default port 7677):
./build/takserver --port 7677 --data /path/to/tak_install

# each player:
./build/takview game "<map name>" --data /path/to/tak_install \
    --server <host> [--serverport N] [--name X] [--overrides none|cosmetic|full]
```

- **Referee sim.** With `--data`, the server also runs a referee simulation that
  hosts the AI players (so no host machine is loaded by them) and holds the
  canonical state hash every client is checked against. Without `--data` it's a
  pure relay and clients cross-check hashes among themselves.
- **Game-data agreement.** Every peer fingerprints the gameplay data its sim will
  read (`hpi::gameplayHash`: unit/weapon/side/build/feature files, never maps or
  cosmetics) and sends it in the handshake. The server rejects anyone whose
  fingerprint differs from the referee's -- so a modified retail file, or a
  `full`-tier gameplay override not shared by everyone, is caught at join instead
  of desyncing mid-game. Cosmetic (`cosmetic`-tier) overrides don't change the
  fingerprint, so players can keep their own art and sound.
- **Lobby.** The in-client lobby has a game browser, a create-game dialog
  (name/password/map, crusades & gods toggles), and a room where each player
  picks faction, colour, and team and readies up; the host opens/closes slots,
  kicks, and starts. The host also sets the **unit cap** — the per-player live-unit
  limit (250 / 500 / 1000 / 2000 / 5000, default 2000; production and new builds
  stall a player once they reach it) — and can **allow in-game speed changes** so
  the host's **+/−** keys re-cadence the match live (0.5×–4×). Speed only changes
  how fast ticks happen in wall-clock — the per-tick `dt` is fixed — so the sim
  stays bit-identical and deterministic.
- **Cross-build determinism.** The sim's trig is routed through a
  deterministic-math shim (`src/sim/detmath`), so lockstep holds across
  compilers and CPUs, not just the same binary. Everyone still needs the same
  engine build and game data (the handshake gates the protocol version).
- **Reconnect & forfeit.** A dropped player's slot is held; they can rejoin with
  a resume token (the client replays the bundle log to catch up). Otherwise they
  forfeit deterministically.
- **Spectate.** A running game can be **watched live** from the browser (the
  **WATCH** button): the spectator replays the bundle log to the present, then
  follows along with no fog and no control.

See `docs/multiplayer-design.md` for the full design, and `docs/detmath-scope.md`
for the determinism contract. (The old 2-player `--host`/`--join` peer mode is
retired.)

## Replays

Start the server with `--replaydir <dir>` and it writes a self-contained
`.takrep` for every finished game. Play one back as a spectator:

```sh
./build/takview replay <file.takrep> --data /path/to/tak_install
```

**Pause** and the **+/−** speed keys scrub it; a bar shows elapsed / total time.

## Overrides

Anything in the install's `overrides/` folder -- loose files or `*.hpi`/`*.kmp`
archives -- overrides the shipped data, exactly like the original game. For
example a `overrides/click.hpi` holding `sounds/*.wav` replaces the faction
order-acknowledgement tones. Overrides are classified as **cosmetic** (art,
models, animation, sound, music, fonts, GUI) or **gameplay** (unit/weapon/side/
build/feature data, maps); `--overrides cosmetic` mounts only the former.
Cosmetic overrides never affect a multiplayer game and can differ between
players; gameplay overrides (mounted only under `--overrides full`) change the
data fingerprint, so under `full` every player must share the same ones.

## Project layout

| Path | Contents |
| --- | --- |
| `src/hpi/` | HPI archive reader (TAK's revised format vs. classic TA) |
| `src/gaf/` | GAF/TAF sprite, animation, and font decoding |
| `src/tnt/` | TNT map decoding |
| `src/tdo/` | 3DO model loading |
| `src/cob/` | COB script bytecode VM (unit animation/scripting) |
| `src/tdf/` | TDF/FBI/OTA text-config parsing |
| `src/crt/` | `.crt` scenario/trigger parsing |
| `src/sim/` | deterministic simulation (movement, A* pathing, combat, economy) |
| `src/net/` | multiplayer wire format, framed TCP, client protocol |
| `src/server/` | `takserver`, the headless lobby + lockstep relay |
| `src/ai/` | the skirmish AI (server-portable; emits commands) |
| `src/terrain/` | terrain / palette handling |
| `src/util/` | shared helpers |
| `src/viewer/` | the SDL2 app (`takview`: asset viewer + game) |
| `tools/` | CLI format tools (`hpitool`, `gaftool`, `tnttool`, `modeltool`, `cobtool`, `tdftool`) |
| `docs/` | format notes + reverse-engineering findings (`retail-engine.md` = the `KINGDOMS.icd` disassembly) |
