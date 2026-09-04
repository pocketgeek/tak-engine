# TAK Engine

A modern, cross-platform engine recreation for **Total Annihilation: Kingdoms**
(Cavedog Entertainment, 1999), in the spirit of OpenRA and Robot War Engine.

This project contains **no game content**. You must own the original game
(e.g. the GOG release of *Total Annihilation: Kingdoms + The Iron Plague*)
and place its data files in `assets/` (gitignored) to use the engine.

## Status

1. ~~**Format tooling**~~ ✅ HPI v2, GAF/TAF, TNT, 3DO, COB, TDF/FBI/OTA,
   GAF fonts, WAV — all retail files parse.
2. ~~**Asset viewer**~~ ✅ `takview map` / `takview model` (textured,
   COB-animated).
3. ~~**Simulation**~~ ✅ movement, A* pathfinding, combat, mana economy,
   production, per-unit COB VMs, sound.
4. ~~**Game**~~ ✅ playable skirmish vs AI (`takview game`): fog of war,
   minimap, building placement, production, player colors, faction select,
   a classic HUD (build icons, order-button column, mana/stats),
   `Keys.TDF` hotkeys, and per-faction soundtrack music.
5. ~~**Campaign**~~ ✅ mission loading via `.ota`/`.cob` with the
   `MAP_COMMAND` scripting API and `.crt` scenario/trigger parsing.
6. **Multiplayer** 🚧 client–server (a central `takserver` relays a
   server-sequenced deterministic lockstep for up to 8 players/teams, with a
   lobby GUI). With `--data` the server also runs a referee sim that hosts the
   AI players and validates every client's state hash. Reconnect is the main
   remaining piece — see `docs/multiplayer-design.md`.
7. ~~**Combat & unit depth**~~ ✅ the FBI/weapon data is driven faithfully:
   HP regen, veterancy (kills → +10%/level attack·armour·reload, gold sheen,
   promoted `veteranmodel`), per-unit mana pools & mana-per-shot, area-of-effect
   splash + per-target-category damage, status weapons (freeze / petrify /
   paralyze / mind-control) with immunities, cloaking, reclaim / resurrect /
   capture, `AdjustArmor`/`AdjustAttack` stat auras, terrain-class movement
   (`MOVEINFO.tdf` slope/water limits + water/road speed), radar sight,
   line-of-sight firing (no shooting through walls), flow-field group movement,
   and a summonable-god economy.
8. ~~**Effects & audio**~~ ✅ real GAF/TAF explosion, splash, shockwave-ring,
   ground-fire and muzzle-flash effects; material-specific impact sounds; unit
   shadows; camera shake; positional/surround audio.
9. ~~**Rendering at scale**~~ ✅ thousands of units on screen, smoothly. The
   per-unit model projection runs across a worker pool; units are frustum-culled
   to the viewport; every unit texture is packed into a per-colour atlas so a
   whole army collapses to a handful of draw calls; and each unit's walk/fly
   cycle is baked to an **animated sprite sheet** (16 facings, real per-type
   cycle timing, multi-page atlas) drawn as a single quad — the classic-RTS
   technique — with the full 3D model kept for close-up and attack/death poses.
   The sim side is O(n) (spatial-hash neighbour queries, staggered target
   acquisition), so a thousand-unit battle is CPU-cheap too.

Many of these were cross-checked by disassembling the retail engine
(`KINGDOMS.icd`) — see `docs/retail-engine.md` for the findings (class model,
config schema, and the veterancy/build formulas read out of the binary).

## Quick start (after placing game data in assets/)

```sh
# extract one archive:
./build/hpitool extract assets/game/<archive>.hpi assets/extracted/<name>
# ...or merge a whole game directory the way the retail engine layers it
# (all *.hpi/*.ufo, patch archives override the base — see below):
./build/hpitool merge assets/game assets/extracted
./build/takview game "assets/extracted/maps/Maps/King of the Hill.tnt" \
    assets/extracted/terrain/terrain assets/extracted/data
```

**HPI precedence** — the retail game shipped each update as a new HPI/UFO that
superseded older copies of a file. `hpitool merge` reproduces the exact rule
(reverse-engineered from `KINGDOMS.icd`): a loose file on disk wins; otherwise,
across all `*.hpi` then `*.ufo` in the directory, the copy whose archive entry
has the **newest date** wins (ties keep the earlier-mounted, `*.hpi` before
`*.ufo`). `hpitool where <dir> <path>` shows which archive a given file resolves
to. So dropping newer patch archives (e.g. `V3Rocket.hpi`) into the game
directory Just Works, as the original did.

Controls (hotkeys follow the game's `Keys.TDF`): drag = box-select,
right-click = move/attack (shift queues). Order keys arm a command you then
click to place — **F** = fight-move (attack-move), **M** = move, **A** = attack,
**P** = patrol, **G** = guard; **S** = stop (immediate), **N** = cycle to the next
unit, Esc cancels an armed order. Selection: **Ctrl+A** = all your units,
**Ctrl+Z** = all of the selected type, **Ctrl+U** = everything on screen.
Control groups: **Ctrl+1–9/0** assign the selection, **1–9/0** recall it,
**Ctrl+Shift+1–9/0** add to a group. **Pause** toggles pause. **+**/**−** step the
game speed over −10…+10 (0 = normal; +10 = 10×, −10 = 0.1×).
Click the build icons at a selected builder/keep to train or place,
arrows/middle-drag/**screen-edge** = scroll, wheel = zoom (toward cursor),
minimap click/drag = move camera; right-click the minimap moves the selection.
**Ctrl+D** destroys the selected unit(s). **F4** toggles a per-player status
panel — live frame rate plus each player's unit count and enemy kills;
**F6** opens the player-colour picker (click a
swatch to recolour your units, HUD and minimap). Units render as full 3D
models by default; **F10** cycles sprite-sheet rendering **AUTO** (the default —
it switches to the cheaper animated sprites only while the frame can't hold
60 fps with a real crowd on screen, back to 3D once it clears) → **ON** →
**OFF**. **F8** toggles the distance-impostor level-of-detail (on by default),
which replaces a unit with a cached billboard only when it's really zoomed out
and small on screen. Background music plays from the game soundtrack. Frame rate is capped at 60 fps (`--maxfps N`, or `--maxfps 0` for
uncapped); `--novsync` disables vsync (the window title shows live FPS).

Each side begins a skirmish with **only its Monarch**, dropped on the
map's real start positions (read from the `.ota`). The Monarch generates
a trickle of mogrium and builds the first lodestones and keep, which then
train the army — the AI opponent bootstraps the same way. `--demo`
instead stages a ready-army AI-vs-AI war; `--mission` loads a campaign
mission's .ota/.cob; `--side ara|tar|ver|zon|cre` picks your faction and
`--aiside` the enemy's (Zhon has no Keep — its Monarch and Beast Handlers
summon creatures; Creon needs the Iron Plague data). `--color N` /
`--aicolor N` (0–9) choose the player-colour variant your / the AI's units
render in, independent of faction. `--cheat` makes all construction and
production finish instantly and cost no mana. `--crusades` uses the **Crusades
balance** — the alternate unit stats and build menus the final patch shipped
for ranked "Darien Crusades" play (`unitscb`/`canbuildcb` in the data, layered
over the base roster).

In a god-enabled match, a faction whose priests (`attractsgods` units) have
channelled enough mana favour manifests its **god** among its forces once the
appear time passes — set `TAK_GODTIME=<seconds>` to shorten it for testing.

Multiplayer is **client–server**: run the headless `takserver` (default port
7677) somewhere reachable, and each player connects with
`takview game <map> <terrain> <data> --server <host> [--serverport N] [--name X]`.
The server hosts a lobby and relays a server-sequenced deterministic lockstep —
up to 8 players on up to 8 teams (allies share vision), each machine running the
identical sim with only ~35-byte commands on the wire. Start the server with
`takserver --port 7677 --data <extracted-data-dir>` and it also runs a **referee
simulation** that hosts the AI players (so the host's machine isn't loaded by
them) and holds the canonical state hash every client is checked against; without
`--data` it is a pure relay and clients cross-check hashes among themselves. All
players connect out to the one server, so no NAT or port-forwarding on the
players' side. Everyone needs the same engine build and game data (the handshake
gates protocol version). See
`docs/multiplayer-design.md` for the full design and milestone plan. The
in-client lobby has a game browser, a create-game dialog, and a room screen
where each player picks their faction, colour, and team and readies up (the host
opens/closes slots, kicks, and starts). Server-run AI and reconnect are the
remaining milestones. The old 2-player `--host`/`--join` peer mode is retired.

## Building

Requires CMake ≥ 3.24, a C++20 compiler, and Ninja. SDL2 is found on the
system or fetched automatically.

```sh
cmake -B build -G Ninja
cmake --build build
```

## Sound overrides

`overrides/click.hpi` replaces the faction order-acknowledgement tones
with a soft click (the game plays either that or the unit voice line).
Drop your own `click.hpi` (an HPI archive of replacement `sounds/*.wav`)
next to the game data to override it, exactly like the original game.

## Layout

- `src/hpi/` — HPI archive reader (TAK uses a revised format vs. classic TA)
- `src/gaf/` — GAF/TAF sprite, animation, and font decoding
- `src/tnt/` — TNT map decoding
- `src/tdo/` — 3DO model loading
- `src/cob/` — COB script bytecode VM (unit animation/scripting)
- `src/tdf/` — TDF/FBI/OTA text-config parsing
- `src/crt/` — `.crt` scenario/trigger parsing
- `src/sim/` — deterministic simulation (movement, A* pathing, combat, economy)
- `src/net/` — multiplayer: command wire format, framed TCP connection,
  client-server protocol, and the client handler
- `src/server/` — `takserver`, the headless lobby + lockstep relay
- `src/ai/` — the skirmish AI (server-portable; emits commands)
- `src/terrain/` — terrain/palette handling
- `src/util/` — shared helpers
- `src/viewer/` — SDL2 application (`takview`: asset viewer + game)
- `tools/` — command-line format tools (`hpitool`, `gaftool`, `tnttool`,
  `modeltool`, `cobtool`, `tdftool`)
- `docs/` — format notes and reverse-engineering findings
  (`retail-engine.md` documents the `KINGDOMS.icd` disassembly)
