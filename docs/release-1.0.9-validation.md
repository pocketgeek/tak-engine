# 1.0.9 release validation

Protocol **336**, replay format **15**, random map generator version **10**.

Tag `v1.0.9` -> `a2641214` (the spawn fix, after the version commits `2651eebe` and `6648775b`);
annotated tag object `16deb896`. Published 2026-11-19 16:36:40 UTC as the Latest release; 1.0.0 to
1.0.8 stay GitHub releases.

## Scope and compatibility

1.0.9 is 1.0.8 (tag `v1.0.8` -> `224ab2bf`, validation commit `8975eac6`) plus the six
changes below (`4096b578` to `6cd2c6d1`, and the spawn fix found by this release's full
suite), from the owner's replays and reports of 2026-11-18 and 2026-11-19. They are listed in the CHANGELOG's 1.0.9 entry and described
in `docs/legion-pathfinding.md` ("Facing follows motion", the overtaking paragraph) and
`docs/retail-engine.md` (the factory door paragraph):

| Change | Game state |
|---|---|
| Legion: unfinished ground bodies (conjures) are overlaid on Legion's occupancy view, never parted, and a body held by one alone detours at once (`4096b578`; test `legion_unfinishedsite`) | hashed sim (Legion only) |
| Legion: a fight or patrol leg on a detour route faces its motion; a follower behind an ally under 3/4 of its cap is offered an overtake before it keeps station (`f9ab3575`) | hashed sim (Legion only) |
| Legion: any leg with no slot target faces its motion (`6fad01ac`) | hashed sim (Legion only) |
| Legion: patrol, attack and guard legs overtake as move and fight legs do (`69c39a18`; fixtures `overtake-patrol`, `-attack`, `-guard`) | hashed sim (Legion only) |
| Guards of a land factory take no spot in the lane in front of its pad and step aside for a leaving output only off its lane (`6cd2c6d1`; new `Unit::padDx/padDz/padKnown`, hashed) | hashed sim (both modes) |
| Match setup's spawn snap (stress and Benchmark armies, a Monarch whose authored start does not fit) requires the footprint passable for the type itself, its own maxSlope included (`World::footprintPassable`) | hashed sim (stress and Benchmark starts only; authored starts unchanged) |

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
  load and passed alone (60 s). `watch_end` failed its `allai` case: the Retail game of two
  computer players with stress armies was not decided within its 30 minutes of game time.
  1.0.8 failed it the same way (1.0.8's cut did not run the full suite). Bisected to
  `24a10c47` (flyers re-aim at release, protocol 331), which only changed how the seeded
  battle went: from then on Taros survived the first clash, and Aramon's reserve stood at
  home for good. Replayed, 128 of the 399 stress bodies spawned on cells steeper than
  their type's `maxSlope` (the snap tested only the class grid; the mover tests the type)
  and walked in place there. With the spawn fix the game is decided at tick 7,951 and
  `watch_end` passes; on the fixed tree the quick suite passed 691 of 691 and the rest of
  the suite 315 of 316 (`harness_seat_load` again under load, passing alone), and all
  eight gates below gave the same hashes as before the fix.
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

`v1.0.9` was first tagged on `6648775b`. The full suite then found the spawn fault above, and
before anything was published the draft and the tag were deleted and made again on `a2641214`
(owner: "Ship it in 1.0.9"); that first tag's CI was cancelled. CI on main for `6cd2c6d1`:
Determinism, macOS and Windows green; Linux packages failed at an openSUSE mirror (repository
metadata not retrieved), the infrastructure fault seen before (for `69c39a18` the macOS
`hdiutil` "Resource busy" and an Ubuntu mirror timeout). Main's runs for the tagged commits were
cancelled: tag CI builds the same commit.

Tag CI on `v1.0.9` (`a2641214`): Determinism (sim) 44551714100 (dispatched), Linux packages
44551714842 and Windows build 44551714638 green at the first attempt; macOS build 44551714802
green after one rerun of the arm64 Sign + notarize job, which GitHub cancelled before it
started. The first tag's CI had made a draft of its own ("TAK Engine v1.0.9"), and the second
tag's CI uploaded into it, replacing every file: all 39 assets carry upload times after the
second tag's CI began. That draft was given the notes and published; the empty draft made with
the notes was deleted. It published without its tag (the tag had been deleted and made again
since the draft was created) and was bound to `v1.0.9` about a minute later.

39 assets, the same set as 1.0.8's. All 39 were downloaded and match the release's SHA-256
digests and sizes. `takclient` and `takserver` from the Ubuntu 24.04 x64 package report
`1.0.9 (build v1.0.9)`, its `takclient` links no libsecret or GLib, and the x86_64 AppImage
reports the same version.
Windows: on oden-win (scratch `C:/tak/w/rel109sig`), `Get-AuthenticodeSignature` reports
**Valid** (timestamped, "Curtis Edge") for the x64 setup program and `takclient`, `takserver`
and `cartographer` in both zips; the x64 `takclient.exe` and `takserver.exe` report
`1.0.9 (build v1.0.9)`.
macOS: the MacBook was not reachable, so the hands-on check was not run. In tag CI's Sign +
notarize jobs Apple accepted all three submissions of each architecture, the staple and
validate step passed for both apps and the DMG of each, and `spctl` accepted all of them as
Notarized Developer ID.
Every link in the notes answers 200.
