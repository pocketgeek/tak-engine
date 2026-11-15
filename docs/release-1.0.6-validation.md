# 1.0.6 release validation

Protocol **330**, replay format **15**, random map generator version **10**.

TAG: lander fills in `v1.0.6` -> the version commit, the annotated tag object, and the
publication time.

## Scope and compatibility

1.0.6 is 1.0.5 (tag `v1.0.5` -> `8f8680a5`, validation commit `4961b000`) plus the
changes below (`b715407e` to `29362d4e`), the fixes for the findings of an audit of an
18-hour replay of eight Absurd computer players (2048x2048 Volcano map, Legion,
Crusades, unit cap 2000) and the owner's Normal AI decision. They are listed in the
CHANGELOG's 1.0.6 entry and described in `docs/legion-pathfinding.md`,
`docs/classic-ai-plan.md`, `docs/multiplayer-design.md`, `docs/retail-engine.md`,
`docs/unit-gifting.md` and the user guide:

| Change | Game state |
|---|---|
| Queued attacks under Retail pathfinding head for the next target, not the map corner (also behind a guard whose ward died) | hashed sim |
| A Load queued behind other orders: the transport holds the pickup until its turn | hashed sim |
| Change of owner: other units' orders, queued attacks, attack rallies and guards on the unit end (retail's new-object rule) | hashed sim |
| Flyers stop at the east and south edges of a 2048-cell map instead of wrapping; distances of 32,768 px or more read whole | hashed sim |
| Unit ids past 65,535 keep blocking ground units; reachability labellings built before tick 0; the minimum-range back-off counts three walks per target | hashed sim |
| Conjuring: an accepted order is carried out (free ground of the unit's own size, idle units make room, red ghost); Legion builders take a free cell round their site | hashed sim |
| AI: Hard and Absurd size production to income (one factory per 60), lodestone caps, unsafe deposits, Zhon producers at home, scout and transport limits; every level but Retail: cap-refused builds | hashed sim (server AI orders) |
| AI (all but Retail): a stuck attack-move is stopped and sent again; no repeat attacks during a firing-spot walk; charm and stone only where the sim takes them; the side fixed at the first think | hashed sim (server AI orders) |
| AI: Normal takes Hard's factory rule (ceiling 8) and a lodestone target of 60 income a factory (protocol 330) | hashed sim (server AI orders) |
| A game with no human seated ends when decided; watchers get the result screen; the recording ends there; an ended game leaves the game list | server and client; outcome unchanged |
| Replay rewind points: spread over the whole game, taken again after a jump back when none fits, a quarter to a third of the memory (shared planes); hours in the time label; Load Replay lists playable replays with their length | client only |
| Legion: route starvation on 2048-cell maps (small routes for five cases, a cut-off unit walks on and keeps its order five minutes) | hashed sim (Legion only) |
| Legion: pair locks, settled crowds and passage starvation (stand-down, give-way, ally swaps, firing-line hold, route record that survives map changes) | hashed sim (Legion only) |

**Compatibility.** The simulation and the server AI changed, so `kNetVersion` is 330
(328 for the first batch, 329 for Legion, 330 for Normal): 1.0.6 does not play with
1.0.5 or earlier, nor play their replays. The replay format stays 15 and the random map
recipe version 10, so random maps and saved map files are unchanged.

## Local tests

Linux, Debug `-O2 -g` (`build-o2`). On main, as each part landed:

- **Batch 1** (`4a1cab33`, protocol 328): full build; quick suite (`ctest -L quick`) 649
  of 650 passed. The one failure, `depth_fx` ("FADE: the tree in front of an effect does
  not fade"), is a load flake main had before the batch (4 of 40 and 6 of 40 failures
  with 40 copies at once, 3 of 3 passes alone).
- **Batch 2, Legion** (`49906637`, protocol 329): full build; `ctest -L legion` 466 of
  466 passed; quick suite 690 of 691 (the same `depth_fx` flake, passing alone).
- **Normal AI** (`29362d4e`, protocol 330): quick suite 691 of 691 passed; the change's
  own `ai_economy_test` and the `ai_` tests; one all-Normal 20-minute room (income 745
  -> 903 mana a second, armed units 320 -> 388).
- **`tools/check-determinism.sh`** on every landing: golden `dcef618cd2e4d558`.
- **`--mpai` gates**, Inner Circle (Ulasem Arena for the last), seed 1, `TAK_SPEED=80`.
  All eight were unchanged from 1.0.5 through batches 1 and 2. The Normal AI fix moved
  the 300 s gates and Ulasem, which seat Normal computer players; in the 60 s gates it
  does not show yet, and the AI gate seats no Normal player:

  | Gate | 1.0.5 | 1.0.6 |
  |---|---|---|
  | Retail 60 s | `beb81788610d2a6c` | `beb81788610d2a6c` |
  | Legion 60 s | `87efa5106c2d3c0e` | `87efa5106c2d3c0e` |
  | Retail AI 60 s | `a3ddc1ba59738936` | `a3ddc1ba59738936` |
  | Retail 300 s | `30a3e25d3f414fad` | `e058fbd245a7be8f` |
  | Legion 300 s | `bd274b29f05f2c18` | `5ac9f4c0ad44dafc` |
  | Crusades Retail 300 s | `23d5e2016eae1bb8` | `7415b4dbd309b753` |
  | Crusades Legion 300 s | `ee9c38ff55448ab3` | `12c69fd491f4f258` |
  | Ulasem | `e8b009af2418e33e` | `29ae726896b1338f` |

On the version commit's tree (`29362d4e` plus the version bump), built in a fresh build directory:

- **Quick suite**: 691 tests, all passed.
- **`tools/check-determinism.sh`**: the six x86_64 builds (g++ and clang++ at `-O0`,
  `-O2`, `-O3`) agree on golden `dcef618cd2e4d558` (the aarch64 legs skip here).
- **`--mpai` gates**: all eight gave the 1.0.6 hashes above.

Picture retaken with the 1.0.6 build: `docs/img/title.jpg` (VERSION 1.0.6).

## Not run for this release

The full suite, the desync sweeps and the Windows and macOS test runs. Tag CI runs the
determinism workflow and builds every package.

## Tag CI and packages

TAGCI: lander fills in main's CI on the version commit, tag CI on `v1.0.6`, the asset
count and digest check, and the Windows and macOS signature checks.
