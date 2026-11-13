# 1.0.5 release validation

Protocol **327**, replay format **15**, random map generator version **10**.

## Scope and compatibility

1.0.5 is 1.0.4 (tag `v1.0.4` -> `9e8ff792`) plus the changes below (`a1693536` to
`eeb62843`), listed in the CHANGELOG's 1.0.5 entry and described in
`docs/random-map-generation.md` and the user guide ("Pin to CCDs"):

| Change | Game state |
|---|---|
| Random maps: a compact mountain peak where no whole range fits; the room's recipe box and Cartographer's preview say when none fits (MOUNTAINS: NO ROOM) | generated maps |
| Random maps: the Volcano world (`kingdom=volcano`, Iron Plague's sections) with its layouts Lava Flows and Industrial Zone; dead-end walkway pads hold mana | generated maps |
| Industrial Zone: the recipe's water amount sets its cut-off lava pads (none at 0), each holding mana only flyers reach (`Result::padMana`), centred on its pad | generated maps |
| Random map sliders that cannot change the map read NONE (Forests and Hills on Volcano, Hills on Veruna Ports, Volcano's kits) | menus only |
| PIN TO CCDS (Settings > Game > Performance): the game and its local server each pinned to a CCD (CPUs sharing an L3), with per-CCD stats; `takserver --cpus` | scheduling only (does not change a game's outcome) |
| A spectator or replay viewer sees the selected unit's kill count | client only |

**Compatibility.** Random maps changed, so `kNetVersion` is 327: 1.0.5 does not play
with 1.0.4 or earlier, nor play their replays. The recipe format stays at version 10,
so a 1.0.4 recipe still opens but can build a different map.

## Local tests

Linux, Debug `-O2 -g` (`build-o2`), on `eeb62843`:

- **Quick suite** (`ctest -L quick`): 633 tests, all passed.
- **Random map and Cartographer tests** (`ctest -R "mapgen|cartographer"`): 63 tests,
  all passed. `mapgen_themes` builds every themed layout for 2-8 players and three
  seeds; on Industrial Zone it checks the dead-end mana (23 spots), the pad mana (112)
  and that every pad stone sits midway, within a cell, between the lava on each side
  (135 stones; two cells off before `eeb62843`), and that LAVA PADS at NONE gives no
  pad mana. `mapgen_kits` checks compact peaks appear (at least 4 of 6 maps at 384
  cells with 2 players). A 10-minute headless AI game on Industrial Zone ran clean.
- **The change's own tests:** `options_ccd` (two-CCD and one-CCD fixtures: the toggle
  offered only with two CCDs, the sliders and their stats lines), `simpool` (a
  7950X3D-shaped fixture), `cpu_topology` (this machine's CCDs printed; a process
  pinned and unpinned); `TAK_WATCHONLY_TEST` (a watcher's panel shows kills).
- **`takserver --cpus 0-3`** kept every server thread on CPUs 0-3. This machine (Core
  Ultra 9 275HX) has one CCD, so PIN TO CCDS is greyed out here; it is unmeasured on
  real multi-CCD hardware.
- **`--mpai` gates**, Inner Circle (Ulasem Arena for the last), seed 1, `TAK_SPEED=80`,
  all unchanged from 1.0.4 (the changes touch generated maps, menus and scheduling,
  not the simulation):

  | Gate | Hash |
  |---|---|
  | Retail 60 s | `beb81788610d2a6c` |
  | Legion 60 s | `87efa5106c2d3c0e` |
  | Retail AI 60 s | `a3ddc1ba59738936` |
  | Retail 300 s | `30a3e25d3f414fad` |
  | Legion 300 s | `bd274b29f05f2c18` |
  | Crusades Retail 300 s | `23d5e2016eae1bb8` |
  | Crusades Legion 300 s | `ee9c38ff55448ab3` |
  | Ulasem | `e8b009af2418e33e` |

- **`tools/check-determinism.sh`**: every build agrees on golden `dcef618cd2e4d558`.

Picture retaken with the 1.0.5 build: `docs/img/title.jpg` (VERSION 1.0.5).

## Not run for this release

The full suite, the desync sweeps and the Windows and macOS test runs. Tag CI runs the
determinism workflow and builds every package.

## Tag CI and packages

TAGCI
