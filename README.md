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

Many of these were cross-checked by disassembling the retail engine
(`KINGDOMS.icd`) — see `docs/retail-engine.md` for the findings (class model,
config schema, and the veterancy/build formulas read out of the binary).

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
arrows/middle-drag/**screen-edge** = scroll, wheel = zoom (toward cursor),
minimap click/drag = move camera. **F4** toggles a per-faction unit counter
with the live frame rate; **F6** opens the player-colour picker (click a swatch
to recolour your units, HUD and minimap). Background music plays from the game
soundtrack. Frame rate is capped at 60 fps (`--maxfps N`, or `--maxfps 0` for
uncapped).

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
production finish instantly and cost no mana.

In a god-enabled match, a faction whose priests (`attractsgods` units) have
channelled enough mana favour manifests its **god** among its forces once the
appear time passes — set `TAK_GODTIME=<seconds>` to shorten it for testing.

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
- `docs/` — format notes and reverse-engineering findings
  (`retail-engine.md` documents the `KINGDOMS.icd` disassembly)
