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
   minimap, building placement, production, team colors, faction select,
   a classic HUD (build icons, order-button column, mana/stats),
   `Keys.TDF` hotkeys, and per-faction soundtrack music.
5. ~~**Campaign**~~ ✅ mission loading via `.ota`/`.cob` with the
   `MAP_COMMAND` scripting API and `.crt` scenario/trigger parsing.
6. ~~**Multiplayer**~~ ✅ 2-player TCP lockstep, verified deterministic
   (commands scheduled at tick+4, periodic state-hash sync check).

## Quick start (after placing game data in assets/)

```sh
tools/… # extract: ./build/hpitool extract assets/game/<archive>.hpi assets/extracted/<name>
./build/takview game "assets/extracted/maps/Maps/King of the Hill.tnt" \
    assets/extracted/terrain/terrain assets/extracted/data
```

Controls (hotkeys follow the game's `Keys.TDF`): drag = box-select,
right-click = move/attack (shift queues). Order keys arm a command you then
click to place — **F** = fight-move (attack-move), **M** = move, **A** = attack,
**P** = patrol, **G** = guard; **S** = stop (immediate), **N** = cycle to the next
unit, Esc cancels an armed order. Selection: **Ctrl+A** = all your units,
**Ctrl+Z** = all of the selected type, **Ctrl+U** = everything on screen.
Control groups: **Ctrl+1–9/0** assign the selection, **1–9/0** recall it,
**Ctrl+Shift+1–9/0** add to a group. **Pause** toggles pause.
Click the build icons at a selected builder/keep to train or place,
arrows/middle-drag = scroll, wheel = zoom (toward cursor),
minimap click/drag = move camera. Background music plays from the game soundtrack. Frame rate is capped
at 60 fps (`--maxfps N`, or `--maxfps 0` for uncapped). `--demo`
stages an AI-vs-AI war; `--mission` loads a campaign mission's .ota/.cob;
`--side ara|tar|ver|zon|cre` picks your faction and `--aiside` the
enemy's (Zhon has no Keep — Beast Handlers summon creatures via the
build ghost; Creon needs the Iron Plague data).

Multiplayer (2-player TCP lockstep): host runs with `--host 7777`, the
other player adds `--join <host-ip> 7777`. Host commands the Aramon
base (blue, west), joiner commands Taros (east). Both machines need
the same engine build and game data.

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
- `src/net/` — TCP lockstep multiplayer session
- `src/terrain/` — terrain/palette handling
- `src/util/` — shared helpers
- `src/viewer/` — SDL2 application (`takview`: asset viewer + game)
- `tools/` — command-line format tools (`hpitool`, `gaftool`, `tnttool`,
  `modeltool`, `cobtool`, `tdftool`)
- `docs/` — format notes as we verify them against real data
