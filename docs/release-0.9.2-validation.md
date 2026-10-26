# 0.9.2 release validation

Protocol **296** (was 295 in 0.9.1), replay format **15** (unchanged),
generator version **9**, campaign payload **4**, database schema **9**.

TODO-LEAD: Tagged `v0.9.2` at `<sha>` (full `<full sha>`, annotated tag object `<obj>`, "TAK Engine 0.9.2") and published on 2026-10-<dd> <hh:mm:ss> UTC.

## Scope and compatibility

0.9.2 follows 0.9.1 (protocol 295; tag `v0.9.1` -> `41b2b81f`, validation
commit `87a04421`). It is a bug-fix release for four problems the user saw
watching an all-AI game (Creon against Aramon, both Absurd, Legion, Crusades
balance, a generated map), plus a targeting fix that `classic_ai_duels` led
to. Every code change landed on main linearly after `87a04421`, from fix
branches cherry-picked or fast-forwarded onto main (no merges):

- `6697b952`: Ballistic weapons use retail's BallisticWeapon inRange
  (`0x52bc60`, origin to origin under the map's gravity, no footprint pad) in
  `slotInRange`, the attack order's fire gate and approach, and
  `repositionForShot`; `queryWeaponAim` flags a no-arc solve unreachable.
  Flames report impacts to `noteShotImpact` (`FlameShot::targetId`, hashed);
  a Hold Position unit whose auto target interrupted a movement order
  repositions for a clear shot or drops the target. **Hashed simulation
  change: protocol 296.** `tools/re/check_ballistic_inrange.py` matches 6,000
  emulated `52bc60` answers.
- `cc5e966e`: sim review: `findTarget` skips targets past the origin arc for
  units that may not leave their post; a loaded, aimed slot with no muzzle arc
  counts a blocked shot (`World::noteNoArc`) and writes the target off after
  `kBlockedShots`. New `naval_combat_test` case (a Hold Submersible 795 px from
  an Aramana on a raised island drops the target), which fails on `6697b952`.
- `1a2f1333`: our AI levels (not the Retail AI) keep factory door strips clear
  (`placeSite`, door clearing, `stationArmy`, conjurers in an open yard) and
  site Navy Yards by door clearance to open water (`navalDoorSteps`). Server
  side: it adds no hashed state, but the AI gives different orders, so games
  with our AI levels (and the 300 s gates) play out differently.
- `064cb448`: AI review: `navalDoorSteps` cache per think; no door strip for
  producers whose outputs all fly.
- `db237626`: integration: `kNetVersion` 295 -> 296, `tools/replay_test.cpp`
  pins 296 and refuses 295; CHANGELOG, `docs/multiplayer-design.md`,
  `docs/user-guide.md`.
- `6c3e8359`: static reach, landed after the preparation commits:
  `World::combatReach` is the one reach test (selected slot via
  `approachSlot`, the longest when all fire; structure footprint pad, or the
  arc for a Ballistic weapon). `tickCombat`'s keep test and `findTarget`'s
  filter for units that may not leave their post both use it, for every
  ranged weapon (melee exempt), so towers and switching Monarchs on Hold no
  longer acquire and drop out-of-reach targets. New test
  `structure_combat_reach`; `classic_ai_expansion` judges each Build after the
  think. **Hashed: folded into protocol 296** (unreleased, no bump).
- `24fb36be`: review: `findTarget(..., chases)` keeps the AI squad attack
  missions (`response.mode<0`, which `mayChase` lets chase even on Hold) out
  of that filter.

`git diff --stat v0.9.1 24fb36be -- src` touches 6 files (519 insertions, 36
deletions): `src/ai/ai.cpp`, `src/ai/ai.h`, `src/net/protocol.h`,
`src/sim/retailaim.h`, `src/sim/sim.cpp` and `src/sim/sim.h` (at `db237626`:
the same 6 files, 474 insertions, 18 deletions). Nothing under `src/server/`,
`src/client/`, `src/tnt/`, `src/campaign/`, `src/net/replayhdr.h` or
`src/net/crusades.h` changed.

The preparation commits on top of `db237626` change no code: `CMakeLists.txt`
(the version line), `CHANGELOG.md` (dated, compatibility and links),
`README.md`, the version lines in `docs/`, these notes, this report,
`docs/img/README.md` and `docs/img/title.jpg`. The static-reach commits
followed them; the docs commit after those changes no code.

TODO-LEAD: `git diff --stat v0.9.1 v0.9.2 -- src` on the final tag (expected:
the same 6 files, 519 insertions, 36 deletions).

**Compatibility.** `kNetVersion` is **296** in `src/net/protocol.h` (295 at
`v0.9.1`). `kReplayFormat` is **15** in `src/net/replayhdr.h`, unchanged. The
map generator's `formatVer` **9** (`src/tnt/mapgen.h`), the campaign payload's
`kVersion` 4 (`src/net/crusades.h`) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`) are unchanged (none of these files differs
from `v0.9.1`).

- **Multiplayer.** `Server::handshake` (`src/server/server.cpp`, unchanged)
  rejects a Hello whose protocol differs from `kNetVersion` ("protocol version
  mismatch"); the release string is never compared. So **0.9.2 plays with no
  0.9.1 or older client or server**.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  format is 1..15 and its protocol equals `kNetVersion`, so 0.9.2 refuses every
  0.9.1 (protocol 295) replay; Crusades battle replays recorded by a 0.9.1
  server are shown as *Recorded replay version unsupported*.
  `tools/replay_test.cpp` pins `kNetVersion == 296` and refuses format 15 under
  protocol 295.
- **Maps, mods and settings.** No settings key, map, recipe or package format
  changed.

No mixed-version check was run: no 0.9.1 client joined a 0.9.2 server (or the
reverse), and no 0.9.1 replay was opened on 0.9.2. The points above rest on the
source and `replay_test`.

## Local preparation checks

By the user's rule for this release (minimal testing: each change's own tests,
one full suite, the gates), no Release suite, `-O0` Debug, Clang or ASan run
and no repeat runs were made. All ran on Linux, cores 4-23, with optimized
Debug builds (`build-o2`, `-O2 -g`, ccache + mold).

**On the integrated code** (`db237626`, integration worktree, all targets):

- **Change tests.** The fix branches checked that their new checks fail without
  the fix (for example, the new `naval_combat` case fails on `6697b952`) and
  ran `ctest -L quick`.
- **Full suite.** `ctest -LE nightly`: **435/437** passed (1 skipped, 1 failed,
  345 s real). The failure, `vashim_unavailable`, also fails on `87a04421` on
  this host: the local `third_party/ffmpeg-bink` predates the libva shim, which
  CI builds (as in 0.9.1); `vashim_system_loader` is the skipped test, same
  cause. This build was configured without `TAK_TEST_DATA`, so the tests that
  need the retail install (562 tests defined, against 787 with it) did not run
  here; see the prepared-tree run below.
- **Detmath guard** (`tools/check-detmath.sh`): OK.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): OK, golden
  `dcef618cd2e4d558`, unchanged from 0.9.1.
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`, a
  clean scratch HOME/XDG_DATA_HOME/`TAK_SETTINGS_FILE` per game, `TAK_LEGION=1`
  for Legion, `TAK_AI_LEVEL=6` for the Retail AI, `TAK_SPEED=80`). Each gate
  ran twice and both runs agreed; all ended `err=none end=timelimit`. A Retail
  60 s run at normal speed (1×) gave the same hash as at 8×.

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `d375d03a6c472773` (4 units, same as 0.9.1) | `00499a875aa5f0db` (18 units; 0.9.1 `a470242be92a0e14`) |
| Legion | `426ab0842b709c28` (4 units, same as 0.9.1) | `15097d3fe06e195b` (18 units; 0.9.1 `a65a48d7a63825ae`) |
| Retail AI (`TAK_AI_LEVEL=6`) | `c5846d769e0a7d93` (4 units, same as 0.9.1) | not run |

Only the AI change moves the two 300 s gates; the sim branch on its own left
all five at their 0.9.1 values. The static-reach fix moved both 300 s gates
again; the final values are under "On the static-reach fix" below. Logs: `~/tak-tmp/fix092-int/` (`ctest.log`,
`gates.out`, `gate-*.log`).

- **The reported problems, re-measured** on the integrated build. A recorded
  20-minute game (seed 1853210448, Creon against Aramon, both Absurd, Legion,
  Crusades balance, generated map, 36,000 ticks, final hash
  `1a2e424a043ee716`) replayed in the out-of-tree probe matched the recording
  at 121 of 121 checkpoints.
  - Submersibles: 197 shots, 35 hits on units. 1 shell landed within 300 px
    of the Submersible (0.5%), 290 px out on high ground, not at the bow;
    `87a04421` gave 33.8% and the user's 0.9.1 game 44%. Only 2 shots were at
    targets beyond 800 px (800.1-800.2 px, at knights).
  - Fire Wagons: 135 shots, no stuck-firing episode.
  - AI factories (three 15-minute games, seeds 1853210448, 1 and 7): no two
    factory doors closer than 80 px (smallest 80 px Creon, 96 px Aramon); every
    new land unit left its factory within 300 ticks (average 39-42 ticks); no
    Navy Yard at the inlet sites the old AI chose. Ships at the main yard still
    take a median of 193-204 ticks to get 150 px out (70-167 ticks at other
    yards).

**On the prepared tree** (branch `task-092-docs`, `db237626` plus the CMake
version line and docs; no code differs from the integrated code above):

- All targets built in a fresh `build-o2` with `TAK_TEST_DATA` set to the
  retail install.
- **The changes' own data tests**, which need the install and so did not run
  in the integration suite: `naval_combat`, `legion_naval_combat`,
  `blocked_shot`, `legion_blocked_shot`, `ai_behavior`, `ai_behavior_data`
  and `replay` all passed.
- **Full suite with data**, run once (`ctest -LE nightly -j12`, scratch
  HOME/XDG_DATA_HOME, 425 s real): **621/625**, 1 skipped
  (`vashim_system_loader`) and 3 failed. 787 tests are defined; the nightly
  ones were excluded.
  - `vashim_unavailable`: the local-only FFmpeg prefix, as above.
  - `mod_settings` ("settings not isolated"): the harness, not the code. The
    run also set `TAK_SETTINGS_FILE`, which overrides the path this test
    checks; run again without it, it passes.
  - **`classic_ai_duels`: a real change in behaviour, since fixed (below).** The
    Classic (Retail AI) duel Creon against Aramon on Edmont's Field ends with
    no winner within the 90-minute cap (peak units 52 / 99). It is
    deterministic (the same numbers on a second run) and bisects to
    `cc5e966e`: on `87a04421` Creon wins after 48.7 game-minutes, on
    `6697b952` after 80.7, and from `cc5e966e` on there is no winner. The
    Retail AI code is unchanged, so the sim review's targeting rules (no
    acquiring past the origin arc for units that hold their post, and writing
    off a target after three no-arc reloads) change how this game plays out.
    The test is labelled `quick` but needs the install, so the branches' and
    the integration's data-less `ctest` runs did not include it.
    The other two pairings still pass, but one moved a long way: Aramon
    against Veruna went from an Aramon win after 60.8 game-minutes on
    `87a04421` to a Veruna win after 86.4, close to the cap (Taros against Zhon:
    Zhon after 27.4, now 28.6).
    Ruling: change the code, for every static unit (the user's decision).
    Logs: `~/tak-tmp/scratch/release-092/ctest-full.out`, `duels-*.out`.
- `takclient menu` renders VERSION 0.9.2 (the title screenshot).

**The `classic_ai_duels` diagnosis.** Instrumented duels (out-of-tree probes,
16 seeds per build; outputs in `~/tak-tmp/wt/sweep/`) found no arc-path stall:
0.9.2's no-arc paths (`noteNoArc`, its write-off, the arc drops and the
`fire` no-arc return) never ran in these duels. The outcome shift came from
`cc5e966e`'s `findTarget` filter alone. A unit that may not leave its post
acquired within `maxRange+90` with no reach test, `tickCombat` dropped the
target at once (`!mayChase && !inReach`), and every acquisition ran
`setSlotsManual`, which clears the fire-at-will sweep's slot targets: such a
unit spent its time acquiring and dropping instead of shooting what was in
range. `cc5e966e` stopped that for Ballistic weapons only, which strengthened
the Creon and Aramon ballistic towers and changed the matchup; the Prismatic
Mirror, the Stronghold and the weapon-switching Creon Monarch on Hold
(searching `maxRange` 1200 with Blue Flame selected) still churned (counts
below). Stalls
were not new: over 16 seeds, `87a04421` had 1 duel with no winner (8 Creon /
7 Aramon), `664ea36c` 2 (6 / 8); the default seed landed on one.

**On the static-reach fix** (`24fb36be`, worktree `build-o2` with
`TAK_TEST_DATA` set, scratch HOME/XDG_DATA_HOME, all targets):

- **Change test.** `structure_combat_reach`: a shooter with an enemy
  structure in reach only through the footprint pad and a nearer mobile decoy
  just past its reach. Damage dealt to the structure with the decoy present,
  before (`c1c46a79`) and after the fix, against the no-decoy run:

| Shooter | Before | After | No decoy |
|---|---|---|---|
| Prismatic Mirror `crepris` (range 1478) | 0 | 11373 | 11373 |
| Stronghold `arassh` (range 600) | 0 | 14146 | 14728 |
| Creon Monarch `cresage`, Hold, slot 2 (range 120) | 0 | 10382 | 9151 |

- **Affected tests**, all 19 passed: `classic_ai_duels`, every `classic_ai*`
  (with `classic_ai_expansion` and `classic_ai_map_profile`),
  `structure_combat`, `structure_combat_reach`, `blocked_shot`,
  `legion_blocked_shot`, `naval_combat`, `legion_naval_combat`, `ai_behavior`
  and `ai_behavior_data`. `ctest -L quick` with data (on `6c3e8359`):
  517/518, the one failure `vashim_unavailable` as above.
- **`classic_ai_duels`** (default seeds), all three won within the cap:
  Creon beats Aramon after 54.2 game-minutes (no winner in 90 on
  `664ea36c`; 48.7 on `87a04421`), Aramon beats Veruna after 39.6 (Veruna
  after 86.4 on `664ea36c`; Aramon after 60.8 on `87a04421`), Zhon beats Taros
  after 26.3 (28.6; 27.4). Over the same 16 seeds the prototype of this fix
  had 1 duel with no winner (6 Creon / 9 Aramon), as many as 0.9.1.
- **Churn** on the default Creon/Aramon seed, drops of an out-of-reach
  target, `664ea36c` -> fix: `cresage` 2628 -> 1, `crepris` 907 -> 47,
  `arassh` 1149 -> 13, `crebomb` 44 -> 24, `araat` 7 -> 5. What remains is a
  target that moves out of reach between acquisition and the next combat tick.
- **Detmath guard** OK; **cross-compiler golden** (`check-determinism`) OK,
  `dcef618cd2e4d558`, unchanged.
- **`--mpai` gates** as above, both 300 s gates run twice with the same hash
  (on `6c3e8359`) and once more on `24fb36be`; these are the 0.9.2 values:

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `d375d03a6c472773` (unchanged) | `387df8b0916a182e` (was `00499a875aa5f0db` at `db237626`) |
| Legion | `426ab0842b709c28` (unchanged) | `2d09d13d3ef0d324` (was `15097d3fe06e195b` at `db237626`) |
| Retail AI (`TAK_AI_LEVEL=6`) | `c5846d769e0a7d93` (unchanged) | not run |

  Logs: `~/tak-tmp/rev092/` (`r60.log`, `r300.log`, `l60.log`, `l300.log`,
  `ai60.log`, `ctest.txt`).

## Screenshots

Only `title.jpg` was retaken, by the prepared tree's build (Debug `-O2`, CMake
`VERSION 0.9.2`), with the 0.9.1 title capture (`takclient menu --shot`,
offscreen driver, 1920 × 1080 window cropped to the 1440 × 1080 artwork; script
`~/tak-tmp/scratch/release-092/shots/title.sh`). It reads VERSION 0.9.2 and,
compared with the 0.9.1 file, differs only in the last digit of that line.
`docs/img/README.md` has a "Retaken for 0.9.2" note and the updated row. No
other picture shows anything 0.9.2 changes.

## Remote multiplayer sweep

**SKIPPED for this release, at the user's request** (minimal testing for a
bug-fix release). No 37-scenario sweep was run against the remote referees.
What covers the ground it would have covered:

- client/referee agreement on the new code: the `--mpai` gates above (a real
  `takserver` referee with server-side AI, checked at every checkpoint, no
  desync or referee-suspect line) and the 121-checkpoint replay match;
- cross-platform and cross-compiler determinism: tag CI (the Determinism
  workflow's GCC and Clang x86-64 builds against golden `dcef618cd2e4d558`,
  and the Windows and macOS CI determinism tests: detmath golden,
  `sim_driver_equiv`, `navigation_determinism`, `fp_contract` on macOS).

So there are no 0.9.2 reference ticks and hashes for the sweep's cases; the
0.9.1 table no longer applies to 0.9.2 games.

## CI and packages

TODO-LEAD: tag CI runs (Determinism dispatched on the tag, Linux packages,
Windows, macOS), reruns if any, and the asset check (count, SHA-256 digests,
`--version` of every runnable binary, Linux `NEEDED`, licence files, no
`mods/`/`overrides/`/`click.hpi`, packaged `--help`).

TODO-LEAD: Windows Authenticode (oden-win) and macOS notarisation (MacBook)
checks. `--mpai` on Windows and macOS only if the lead runs them (by the
0.9.0 rule they are skipped unless build flags, compilers or CI determinism
coverage changed; none did).

TODO-LEAD: publication line (`gh release edit v0.9.2 --draft=false --latest`,
time, asset count). 0.9.0 and later releases are kept.

## Limits

- No game mixing 0.9.2 with 0.9.1 or older is possible: protocol 296 is refused
  by every older server and client. No mixed-version check was run.
- The remote multiplayer sweep was skipped (above).
- New (see [the notes](release-0.9.2-notes.md#known-limits)): a boat whose only
  target is inland past the shore holds it without firing (as retail); where
  only the muzzle arc fails we spend reloads and drop the target after three,
  where retail lobs a flat shell; ships leave the main Navy Yard of the test
  map slowly and a crowd of Iron Clads can still block a yard door; Aramon
  Keep and Castle get door strips on three sides; a conjurer in a factory yard
  could wait for ever with no free clearing spot within 600 px (not seen);
  melee buildings and melee units on Hold can still acquire and drop an
  out-of-reach target. `structure_combat_reach` fails without the static
  `findTarget` reach filter.
- `classic_ai_duels` passes again; a Classic duel with no winner within 90
  minutes still happens on about 1 seed in 16, as it did in 0.9.1.
- Static `findTarget` limits that predate this release: the search radius is
  `maxRange+90` (`+0` on Hold), so a footprint pad above 90 px does not extend
  acquisition; a tower whose longest weapon is Ballistic acquires only inside
  that arc and leaves targets only a shorter direct weapon reaches to the
  sweep. The AI squad-mission exemption (`24fb36be`) has no test of its own.
- Local only: `vashim_unavailable` fails against a `third_party/ffmpeg-bink`
  built before the shim; CI builds FFmpeg with it.
- All of 0.9.1's limits carry over (see
  [release-0.9.1-validation.md](release-0.9.1-validation.md#limits)), except
  the mixed-version notes, which now concern 0.9.1.
- TODO-LEAD: tag CI outcome.
