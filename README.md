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
4. **Game** — playable skirmish vs a wave AI works today (`takview game`);
   next: fog of war, building placement, campaign loading, lockstep
   multiplayer.

## Quick start (after placing game data in assets/)

```sh
tools/… # extract: ./build/hpitool extract assets/game/<archive>.hpi assets/extracted/<name>
./build/takview game "assets/extracted/maps/Maps/King of the Hill.tnt" \
    assets/extracted/terrain/terrain assets/extracted/data
```

Controls: drag = box-select, right-click = move/attack (shift queues),
A + click = attack-move, P + click = patrol, H = halt, 1–6 = train or
place buildings at a selected builder, arrows/middle-drag = scroll,
wheel = zoom, minimap click = jump camera, S = screenshot. `--demo`
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

## Layout

- `src/hpi/` — HPI archive reader (TAK uses a revised format vs. classic TA)
- `src/gaf/` — GAF sprite/animation decoding
- `src/tnt/` — TNT map decoding
- `src/viewer/` — SDL2 asset viewer application
- `tools/` — command-line format tools (`hpitool`, …)
- `docs/` — format notes as we verify them against real data
