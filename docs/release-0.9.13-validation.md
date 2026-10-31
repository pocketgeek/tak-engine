# 0.9.13 release validation

Protocol **307** (was 305 in 0.9.12; 306 was never released), replay format
**15** (unchanged), generator version **9** (unchanged).

TODO-LEAD: tag line (tag `v0.9.13` at which commit, full sha, annotated tag
object, publication time).

## Scope and compatibility

0.9.13 follows 0.9.12 (protocol 305; tag `v0.9.12` -> `a6e56964`, validation
commit `61f294ac`). Seven branches were merged on main, each reviewed on its
own, plus one gameplay commit, two docs commits and one CI commit on main
itself (and 0.9.12's own validation commit `61f294ac`); the code tested below is main `49f15e38`:

- **Status spells fly** (`task-los-status-flight`, merged as `1a91aa4a`,
  protocol 306): `8d4586b4` the mind-control, stone and freeze Line-of-Sight
  classes fly a real shot (`52d8d0` / `52db40` / `52dda0`) and act through
  the original's callbacks (`52da10` / `52dca0` / `52df00`, also the area
  charm's and freeze's): player, mover, Monarch, immunity and construction
  gates, one CRT roll, no damage; `5f23e400` docs; `fdca690d` a caster that
  changed sides names no hitter. The `--mpai` gates were unchanged from 0.9.12.
- **Melee rectangle and air traffic** (`task-wolves-drakes`, `172ee75b`,
  protocol 307), from the owner's 0.9.12 replay `wolves-0912.takrep`:
  `f9b5fc96` melee attackers walk to Attack_Melee's rectangle round a
  building (`404690` -> `4d4e80`), `457f5578` flyers step out of a stack
  (unit `+0x12a`, `4214b0`) and AirToGroundHover (`41d820`) ported whole,
  `5b4df7ef` (review) the AIs read past a flyer's step-out and a structure
  gets no melee rectangle.
- **Enemies with construction left** (`task-idle-site`, `c6c501cf`):
  `ff72a34c` idle, patrolling and moving units take an enemy site or a unit
  still being built, behind finished targets (`411700`, `412620` x10,
  `50b2c0`, `40f330`); `096c5d90` docs. Docs on main: `cbaa31c9`,
  `faf59206`.
- **Legion flight stations** (`task-legion-flyer-formation`, `a2c0786e`):
  `c3734891` only an Alt+N formation's flyers keep the ground's pace, and its
  landing lanes are used only when they all fit on the map; `e36e86af` docs.
- **Melee and flyers in the air** (`task-titan-air`, `f77910c0`):
  `d5996336` a melee weapon never reaches a unit in the air (`52b980`,
  `404778..4047ee`, `412620`, `40fcb3`, `409db4`); a landed flyer is a
  target.
- **Air traffic, the rest** (`task-air-traffic-rest`, `4489dcc2`):
  `cced7a8c` the step-out on fight-move legs, patrols, flyer rally steps and
  the flying builders' jobs; VTOL_SeekGuard and AirStrike ported whole;
  fight-move flyers keep the group spread; `85984517` (review) the AIs read
  past a flying builder's step-out.
- **Air flags by layer** (main, `b4e83a49`): every `noairweapon` /
  `toairweapon` test reads the target's current layer, and Attack_Chase's
  head ends a walker's ranged attack on a carried target. The seven gates
  were unchanged by it.
- **Performance follow-up** (`perf-followup`, merged as `2e2b09a8`): twelve
  exact optimizations and two measurement changes, each on its own branch
  with an adversarial review, one `--no-ff` merge each (table in
  [perf-followup-2026-10.md](perf-followup-2026-10.md#integration-2026-10-30)),
  a merge of main with the protocol 307 fixes (`d710eca4`), `95b66181`
  (`naval_combat_test`: an attack on a target without a mover is the melee
  rectangle's) and `0d77e76a` (the pick-index verify probes one body's own
  centre). The one visible change, `98e01695` (`kShadowMaxPages` 4 -> 16),
  was accepted by the owner (`0405551c`).
- **CI** (`49f15e38`): `legion_cluster_test` and `legion_routebins_test` added
  to the CI build target list (every CI job had reported them Not Run on
  `2e2b09a8`).

`git diff --stat v0.9.12 49f15e38`: 160 files changed, 192845 insertions(+),
140411 deletions(-), of which `tools/scenarios/baseline.json` (the scenario
baseline, retaken by `f9b5fc96`, `457f5578` and `cced7a8c`) is 174,298 and
139,541; `-- src`: 33 files changed, 4997 insertions(+), 580 deletions(-)
(`src/sim` 11 files, `src/client` 14, `src/ai` 4, `src/net` 4; the rest is
tests, test baselines, scenario fixtures, benches, `tools/re` probes, CI
workflows, `CMakeLists.txt`, `CHANGELOG.md` and the documentation). 139
commits (117 without the merges).

TODO-LEAD: final `git diff --stat v0.9.12 v0.9.13` (whole tree and `-- src`)
and the commit count, once the tag exists.

The preparation commit on top changes no code: `CMakeLists.txt` (the version
line), `CHANGELOG.md`, `README.md`, the version and protocol lines in `docs/`,
the notes, this report, `docs/img/README.md` and `docs/img/title.jpg`.

**Compatibility.** `kNetVersion` is **307** in `src/net/protocol.h` (305 at
`v0.9.12`). `kReplayFormat` is **15** in `src/net/replayhdr.h`, unchanged. The
map generator's `formatVer` **9** is unchanged. The gameplay data hash of the
test install is `d6000f4fa9095a51`, as for 0.9.12 (server logs of the gates
below).

- **Multiplayer.** `Server::handshake` rejects a Hello whose protocol differs
  from `kNetVersion`; the release string is never compared. So **0.9.13 plays
  with no 0.9.12 or older client or server**.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  format is 1..15 and its protocol equals `kNetVersion`, so 0.9.13 refuses
  every 0.9.12 (protocol 305) replay. `tools/replay_test.cpp` pins 307.
- **Hashed state.** The flying status spells, the melee rectangle, the air
  traffic counter and step-out, the melee and layer tests and the targets
  with construction left change the simulation; the enemy-site targets moved
  four of the seven gates (below). The performance follow-up is exact: it
  changes no hashed state, AI command or random draw (identity in
  [perf-followup-2026-10.md](perf-followup-2026-10.md#integration-2026-10-30)
  against its 0.9.12 base, and the gates below equal the pre-merge values).
- **Maps, mods and settings.** No map, recipe, package or settings format
  changed.

## Local preparation checks

All ran on Linux, on cores 4-23 under `nice`. Every takclient and takserver
run used scratch `HOME`, `XDG_DATA_HOME` and `XDG_CONFIG_HOME`; builds and
tests used `TMPDIR` under `~/tak-tmp`. Worktree `~/tak-tmp/wt/release-0913`
from `origin/main` `49f15e38`; data and logs in
`~/tak-tmp/scratch/release-0913/`.

- **Builds.** Debug `-O2 -g` of `49f15e38` (`build-o2` in the worktree, all
  1058 targets, `TAK_TEST_DATA` the test install `assets/game`): its
  binaries print `0.9.12 (build v0.9.12-139-g49f15e381f16)`, because the
  version number changes in the preparation commit; the gates below ran on
  them. Rebuilt in the prepared tree (dirty only with the preparation
  edits), `takclient --version` and `takserver --version` print
  `0.9.13 (build v0.9.12-139-g49f15e381f16-dirty)`; the full suite and the
  title picture ran on that build.
- **Full suite** (`ctest -j 12` on that build, scratch `HOME` and XDG dirs,
  log `ctest-full2.log`, 951 s): 843 of 845 passed, `vashim_system_loader`
  skipped and `vashim_unavailable` failed, the known local failure (a render
  node without libva).
- **Cross-compiler golden hash** (`tools/check-determinism.sh`, on
  `49f15e38`): golden `dcef618cd2e4d558` from g++ and clang++ at -O0, -O2 and
  -O3, unchanged from 0.9.12 ("DETERMINISM OK"); the detmath guard is OK. The
  aarch64 legs were skipped because no target libc or headers are installed
  here.
- **Gameplay data hash.** All 14 gate servers loaded the test install
  (`~/tak_data`) with retail gameplay hash `d6000f4fa9095a51`, as 0.9.12, and
  listened on protocol v307.
- **Scenario baseline and navigation checkpoints:** not retaken in the
  preparation. The branches added 16 fixtures (145 -> 161
  `tools/scenarios/*.scn`: the `melee-*` and air-traffic fixtures) and retook
  the baseline's `hash@` keys where their fixtures moved (`f9b5fc96`,
  `457f5578`, `cced7a8c`); `tools/navigation_checkpoints.json` is unchanged
  since 0.9.12. The performance follow-up checked all 145 fixtures it had
  against the unchanged baseline (`check` PASS, 0 failed). The
  `legion_check_*` and `navigation_determinism` tests in the full suite run
  against the committed files.
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`,
  `TAK_LEGION=1` for Legion, `TAK_AI_LEVEL=6` for the Retail AI, `--crusades`
  for the Crusades games, `TAK_SPEED=80`, `--data ~/tak_data`), on
  `49f15e38`, twice (wall 42 and 43 s for the seven in parallel; script
  `gates.sh`, results `gates-run1.txt` and `gates-run2.txt`). All seven
  equal the expected values in both runs, `err=none` in every game, and no
  desync, mismatch or referee-suspect line in any client or server log.
  "0.9.12" is `v0.9.12`:

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | **`8690eaee7b16b4e6`** (0.9.12 the same) | **`269f3da1835a17ea`** (0.9.12 `d028e465c7676e68`) |
| Legion | **`b558575535124afc`** (the same) | **`a860707b7b4113e6`** (`eab501625cd4381a`) |
| Retail AI (`TAK_AI_LEVEL=6`) | **`12003fba552b8696`** (the same) | not run |
| Crusades, Retail pathfinding | not run | **`6f8d58fdcd304fc7`** (`def92d0b7d4a798e`) |
| Crusades, Legion | not run | **`4db379ba59111b0c`** (`fb33fb2edffd997e`) |

**Four changed, as expected; three are 0.9.12's.** The status flights
(`1a91aa4a`) moved none. The melee rectangle and air traffic (`172ee75b`)
moved Legion 300 s and both Crusades games (`6dcd4362dceab088`,
`9007a6066de753a5`, `62a3c12a701911cd`); the enemy-site targets
(`c6c501cf`, landed with `a2c0786e`) moved those three again and Retail
300 s to the values above, and the flight-station change moved none
(`a2c0786e`). `f77910c0`, `4489dcc2` and `b4e83a49` left all seven unchanged
(their commit messages), and the performance follow-up, merged after them,
leaves them unchanged too (these runs). The 60 s games (4 units) meet no
building site, flyer or melee fight.

## Screenshots

In the preparation, `docs/img/title.jpg` was retaken by the prepared tree's
build (Debug `-O2`, CMake `VERSION 0.9.13`) with the 0.9.1 title capture
(`takclient menu --shot`, offscreen driver, 1920 x 1080 window cropped to the
1440 x 1080 artwork; script `shots/title.sh`). It reads VERSION 0.9.13 and
shows the three doors (looked at). The melee, air-traffic, status-spell,
target and Legion flyer changes and the performance pass change no picture in
the folder (the larger shadow atlas changes only views of thousands of units
at 7680 x 2160, which no picture shows), so none other was retaken or
deleted. `docs/img/README.md` lists the details.

## Desync sweeps

No local or remote desync sweep (`tools/desync-hunt.sh`,
`tools/desync-hunt-remote.sh`) was run for 0.9.13, and no two-client game.
The gameplay changes were checked on their branches by the gates (two runs
each), the scenario baseline and `navigation_determinism`; the performance
follow-up by `tools/legion_identity.sh` (four recorded games, every
checkpoint's section hashes, serial and workers) and the crowdbench matrix.

None is planned before the tag: by the project's testing rule a release runs
no remote sweep, and the tag CI determinism tests are the cross-platform check.

## CI and packages

TODO-LEAD: tag workflows and their results, package verification (assets,
digests, build strings, `--version` of each package).

## Signing

TODO-LEAD: Windows Authenticode and macOS notarisation results.

## Publication

TODO-LEAD: publication time, asset count, Latest flag.

## Limits

- No game mixing 0.9.13 with 0.9.12 or older is possible: protocol 307 is
  refused by every older server and client. No mixed-version game was run
  with a real 0.9.12 binary.
- No Windows or macOS `--mpai` for 0.9.13; their tag CI determinism tests
  cover them.
- The performance measurements ran on an Intel Core Ultra 9 275HX; their
  limits (the shadow pan p99 +4%, the raise search without real-game
  evidence, the Legion items rarely reached, the pre-existing crash under a
  384 MiB video-memory cap at 7680x2160, the untraced code-layout effects)
  are in [perf-followup-2026-10.md](perf-followup-2026-10.md#limits-and-blockers-with-next-steps)
  and the [release notes](release-0.9.13-notes.md#known-limits).
- The gameplay limits (melee drop timing, Legion's mobile melee ring, the
  air-traffic parts not ported, the status-shot draw order, sites at 0 HP)
  are in the [release notes](release-0.9.13-notes.md#known-limits).
- TODO-LEAD: tag CI limits (failures, reruns, stalls).
- All of 0.9.12's limits carry over (see
  [release-0.9.12-validation.md](release-0.9.12-validation.md#limits)).
