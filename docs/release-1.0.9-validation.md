# 1.0.9 release validation

Protocol **336**, replay format **15**, random map generator version **10**.

Tag `v1.0.9` -> TODO-LEAD (the version commit); annotated tag object TODO-LEAD.
TODO-LEAD publication line.

## Scope and compatibility

1.0.9 is 1.0.8 (tag `v1.0.8` -> `224ab2bf`, validation commit `8975eac6`) plus the five
changes below (`4096b578` to `6cd2c6d1`), all from the owner's replays and reports of
2026-11-18 and 2026-11-19. They are listed in the CHANGELOG's 1.0.9 entry and described
in `docs/legion-pathfinding.md` ("Facing follows motion", the overtaking paragraph) and
`docs/retail-engine.md` (the factory door paragraph):

| Change | Game state |
|---|---|
| Legion: unfinished ground bodies (conjures) are overlaid on Legion's occupancy view, never parted, and a body held by one alone detours at once (`4096b578`; test `legion_unfinishedsite`) | hashed sim (Legion only) |
| Legion: a fight or patrol leg on a detour route faces its motion; a follower behind an ally under 3/4 of its cap is offered an overtake before it keeps station (`f9ab3575`) | hashed sim (Legion only) |
| Legion: any leg with no slot target faces its motion (`6fad01ac`) | hashed sim (Legion only) |
| Legion: patrol, attack and guard legs overtake as move and fight legs do (`69c39a18`; fixtures `overtake-patrol`, `-attack`, `-guard`) | hashed sim (Legion only) |
| Guards of a land factory take no spot in the lane in front of its pad and step aside for a leaving output only off its lane (`6cd2c6d1`; new `Unit::padDx/padDz/padKnown`, hashed) | hashed sim (both modes) |

**Compatibility.** `kNetVersion` is 336 (`4096b578`): 1.0.9 does not play with 1.0.8 or
earlier, nor play their replays. The replay format stays 15 and the random map recipe
version 10, so random maps and saved map files are unchanged.

## Measurements as the changes landed

Linux, Debug `-O2 -g` (`build-o2`), each against its parent:

- **Unfinished sites** (`4096b578`): on the owner's replay, Hunters held ~375 ticks behind
  the next conjure; with the change a body held by one plans its detour on the first held
  update. `legion_unfinishedsite` covers it; the four gates below were unchanged by it.
- **Facing** (`f9ab3575`, `6fad01ac`): sideways and backward steps in the attack and guard
  ring fixtures fall 50-85% (`attackring-guard-64` sideways 9,818 -> 3,412, backward
  5,921 -> 1,195); ring reach and t90 unchanged. Owner-accepted costs: `attackring-f1-64`
  and `-squad-64` spins 258 -> 281, `battle-field-2x60` contact aware pairs 133 -> 149,
  `attackring-fight-60` parked/waiting no-progress keys (already over their 0 reference
  on the parent), `fight-retarget` completion distance 3, `battle-assault` contact pass
  scan cells 3,169,156 -> 3,568,607, and `landdoor_exit`'s guard-crowd tolerance 15% ->
  20% (`verkeep verpult` 1,400 -> 1,439 ticks; all 36 Legion door cases 165 ticks faster
  together). Scenario hashes and Legion navigation goldens retaken.
- **Overtakes on every leg** (`f9ab3575`, `69c39a18`): `overtake-field` open field limit
  1,200 -> 1,260 (one knight's pass at offset +1). On the new columns, fast stops over 7
  offsets: patrol 706 -> 66, attack 786 -> 12, guard 2,674 -> 7; fast bodies' last
  arrival (median) attack 2,049 -> 909, guard 2,294 -> 914, patrol 1,089 -> 1,004; slow
  bodies no later. No pinned scenario hash moved; 24 Legion navigation goldens retaken.
- **Factory guards** (`6cd2c6d1`): `doorway_test`'s builder guarding each shipped factory
  (54 cases a mode): walked 1,223 -> 728 px under Legion and 1,395 -> 1,131 Retail
  (medians), four outputs ~80-120 ticks sooner, no case worse in Legion; `landdoor_exit`'s
  dozen guards of the factory step aside 43 -> 22 (Legion) and 43 -> 25 (Retail) at
  level production; guards of other units unchanged byte for byte. With the lane, a
  shore Navy Yard's eight Iron Clad guards held its sixth ship in, so yards keep the
  area rule. With the new pad fields left out of the state hash, all four gates below
  played to 1.0.8's hashes: the 300 s gates move only by the hashed pad.

## Local tests

On the landed tree (`6cd2c6d1`), Linux, Debug `-O2 -g` (`build-o2`), built once:

- **Full suite** (all labels): 1,005 tests, 1,003 passed. `harness_seat_load` failed under
  load and passed alone (60 s). `watch_end` fails its `allai` case: the Retail game of two
  computer players with stress armies is not decided within its 30 minutes of game time.
  1.0.8 (`v1.0.8`, built for the check) fails the same case the same way, and so does
  `69c39a18`, before the only change here that touches Retail: it predates this release
  (1.0.8's cut did not run the full suite) and is left open.
- **`tools/check-determinism.sh`**: the six x86_64 builds (g++ and clang++ at `-O0`,
  `-O2`, `-O3`) agree on golden `dcef618cd2e4d558` (the aarch64 legs skip here).
- **`--mpai` gates**, Inner Circle (Ulasem Arena for the last), seed 1, `TAK_SPEED=80`:

  | Gate | 1.0.8 | 1.0.9 |
  |---|---|---|
  | Retail 60 s | `beb81788610d2a6c` | `beb81788610d2a6c` |
  | Legion 60 s | `87efa5106c2d3c0e` | `87efa5106c2d3c0e` |
  | Retail AI 60 s | `a3ddc1ba59738936` | `a3ddc1ba59738936` |
  | Retail 300 s | `e058fbd245a7be8f` | `6ef0b1fbbaa61815` |
  | Legion 300 s | `5ac9f4c0ad44dafc` | `8ac4b5887eefea82` |
  | Crusades Retail 300 s | `7415b4dbd309b753` | `4da74d1650f8c6df` |
  | Crusades Legion 300 s | `12c69fd491f4f258` | `7fb55681ed620dee` |
  | Ulasem | `29ae726896b1338f` | `0742b016af71e1ba` |

  The five that moved move only by the new hashed pad fields: built from `6cd2c6d1` with
  the pad fields left out of `World::stateHash`, all eight gates give 1.0.8's hashes.

Picture retaken with the 1.0.9 build: `docs/img/title.jpg` (VERSION 1.0.9).

## Not run for this release

The desync sweeps and the Windows and macOS test runs. Tag CI runs the determinism
workflow and builds every package.

## Tag CI and packages

TODO-LEAD
