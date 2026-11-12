# 1.0.4 release validation

Protocol **326**, replay format **15**, random map generator version **10**.

## Scope and compatibility

1.0.4 is 1.0.3 (tag `v1.0.3` -> `0a9221d7`) plus the changes below (from `cae85cda` on), listed in
the CHANGELOG's 1.0.4 entry and described in `docs/retail-engine.md` ("Ours, not retail: a
repeating mobile builder parks its output; output stances"):

| Change | Game state |
|---|---|
| A finished production building, or a mobile builder on an infinite queue, holds a stance for its output (`Unit::outputStance`, set by a human player's Stance, cleared by `World::kStanceNone`); a repeating builder's command gate lets Stance through | simulation |
| A mobile builder on an infinite queue releases its output with the factory's PARK ring round it; a building it raises keeps the exit step, and under Legion its output keeps the plain ring | simulation |
| A lodestone area job ghosts every deposit it is predicted to build (`GameView::areaBuildPlan`) | client only |
| Legion: a body's display headway window measures against the pace the mover gave it (`World::legionPace_`), so a formation member held to a slow member's pace keeps its gait | simulation (hashed `legionStill`) |
| The TRAINING line reads (+++) under infinite production | client only |

**Compatibility.** The simulation changed, so `kNetVersion` is 326: 1.0.4 does not play
with 1.0.3 or earlier, nor play their replays.

## Local tests

Linux, Debug `-O2 -g` (`build-o2`):

- **Quick suite** (`ctest -L quick`): 630 tests. The run on the final simulation code had
  one failure, `conjure`, whose check that a repeating builder rejects every other command
  now has to let Stance through; the test was updated (Stance checked on its own) and
  passes.
- **Legion suite** (`ctest -L legion`): 414 tests. The formation gait fix moved the final
  hashes of the 14 formation ("squad") scenarios (`legionStill` is hashed and walk scripts
  draw from the game's random stream); their hashes were retaken with `tools/legion_check.py
  baseline --hashes` and their 195 improved metrics ratcheted. A first version of the fix
  raised `walk_in_place` in the 120-unit room squad scenarios from 0 to 8-17 (creeping members
  counted as walking); the fix uses the formation's pace only while the body walks at it.
- **The change's own tests:** `production_exit` (274 cases: the output stance through
  `setStance` and through the command path, cleared, refused to a computer; a repeating
  Beast Tamer's wolves parked on the ring with its stance), `production` and
  `legion_production`, `movement_orders` and `legion_movement_orders`, `conjure`,
  `area_build_ghosts`, `area_drag_input`, `order_trail`, `output_stance_hud` (an idle
  finished Barracks shows the stance buttons, none lit; the chosen one lit; its new
  unit takes it).
- **`--mpai` gates**, Inner Circle, seed 1, `TAK_SPEED=80`, all unchanged from 1.0.3:

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
- **By hand:** the owner played a game with a repeating Beast Handler; its replay showed
  the stance reaching the simulation and being dropped by the repeating builder's command
  gate, which led to the fix. Retested by the owner after it. A later test found a
  factory showed the buttons only while training, and lit its own stance rather than
  its output's; both were fixed before the tag and are covered by `output_stance_hud`.

Picture retaken with the 1.0.4 build: `docs/img/title.jpg` (VERSION 1.0.4).

## Not run for this release

The full suite, the desync sweeps and the Windows and macOS test runs. Tag CI runs the
determinism workflow and builds every package.
