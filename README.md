# TAK Engine

A modern, cross-platform engine recreation for **Total Annihilation: Kingdoms**
(Cavedog Entertainment, 1999), in the spirit of OpenRA and Robot War Engine.

> **This project contains no game content.** You must own the original game
> (e.g. the GOG release of *Total Annihilation: Kingdoms + The Iron Plague*) and
> place its data files under `assets/` (gitignored) to run anything below.

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

## Game data

Point the engine at your own extracted game files. Either extract a single
archive, or merge a whole game directory the way the retail engine layers it:

```sh
# one archive:
./build/hpitool extract assets/game/<archive>.hpi assets/extracted/<name>

# ...or merge every *.hpi / *.ufo in a directory (patches override the base):
./build/hpitool merge assets/game assets/extracted
```

**HPI precedence.** The retail game shipped each update as a new HPI/UFO that
superseded older copies of a file. `hpitool merge` reproduces the exact rule
(reverse-engineered from `KINGDOMS.icd`): a loose file on disk wins; otherwise,
across all `*.hpi` then `*.ufo`, the copy whose archive entry has the **newest
date** wins (ties keep the earlier-mounted, `*.hpi` before `*.ufo`). So dropping
a newer patch archive (e.g. `V3Rocket.hpi`) into the game directory Just Works,
as the original did. `hpitool where <dir> <path>` shows which archive a file
resolves to.

## Playing

`takview game` takes the map, the terrain directory, and the data root:

```sh
./build/takview game "assets/extracted/maps/Maps/King of the Hill.tnt" \
    assets/extracted/terrain/terrain assets/extracted/data \
    --side ara --aiside tar
```

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
| **Camera** | arrows / middle-drag / **screen-edge** scroll · wheel zoom (toward cursor) · minimap click/drag = move · right-click minimap = move the selection |
| **Build queue** | at a training building: left-click **+1**, **Shift** **+5**, **Ctrl+Shift** **+10**; right-click removes the same; **Ctrl**+left toggles infinite production. Each icon shows its queued count. (A builder that *places* things — structures, or a mobile conjurer like a Beast Handler — arms placement instead: click to position.) |
| **Game** | **Pause** · **+/−** game speed (−10…+10; 0 = normal, +10 = 10×) · **F4** status/scoreboard · **F6** player-colour picker |

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
./build/takserver --port 7677 --data <extracted-data-dir>

# each player:
./build/takview game <map> <terrain> <data> --server <host> [--serverport N] [--name X]
```

- **Referee sim.** With `--data`, the server also runs a referee simulation that
  hosts the AI players (so no host machine is loaded by them) and holds the
  canonical state hash every client is checked against. Without `--data` it's a
  pure relay and clients cross-check hashes among themselves.
- **Lobby.** The in-client lobby has a game browser, a create-game dialog
  (name/password/map, crusades & gods toggles), and a room where each player
  picks faction, colour, and team and readies up; the host opens/closes slots,
  kicks, and starts.
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
./build/takview replay <file.takrep> <terrain> <data>
```

**Pause** and the **+/−** speed keys scrub it; a bar shows elapsed / total time.

## Sound overrides

`overrides/click.hpi` replaces the faction order-acknowledgement tones with a
soft click (the game plays either that or the unit voice line). Drop your own
`click.hpi` (an HPI archive of replacement `sounds/*.wav`) next to the game data
to override it, exactly like the original game.

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
