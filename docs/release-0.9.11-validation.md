# 0.9.11 release validation

Protocol **304** (was 303 in 0.9.10), replay format **15** (unchanged),
generator version **9** (unchanged).

TODO-LEAD: tag line (tag `v0.9.11` at which commit, full sha, annotated tag
object, publication time).

## Scope and compatibility

0.9.11 follows 0.9.10 (protocol 303; tag `v0.9.10` -> `99ccf7e5`). Six
branches, each from main `5b7a4f97` (0.9.10's validation commit) and each
reviewed on its own, were merged on `task-0911`; the code tested below is
`afaacb9a`:

- `7ec1e800` Sim: a hover attacker whose shots stop three times running on
  scenery it cannot destroy moves round its ring to a clear line
  (`World::hoverForShot`, `chordClear`; beyond retail), merged from
  `task-monarch-hit` as `2b6888d2`.
- `583e3239` Client: full zoom-out and the scroll limits fit the map to the
  world view (less the right-hand panel and the bottom bar; spectators have
  no bar); Tactical Dots and the Benchmark camera follow; merged from
  `task-zoom-bar` as `a11e21f1`.
- `133ee281` Client: Hidden Units Fade Opacity defaults to 50% (was 65%);
  saved values kept; Options picture retaken; merged from `task-fade-50` as
  `e333d394`.
- `a0585458`, `754f7b83` Sim: a dying factory kills its unfinished output
  (retail `BuildingBuild` abort, 30000 damage of type 11), and so do
  cancelling the unit in work (last copy, repeat off, Stop), ownership changes
  and leaving players; merged from `task-factory-death` as `7a1416d3`.
- `761eac47` Sim (Legion only): bodies sliding round units and walls face
  their actual motion (`Member::course`, `faceMotion`); merged from
  `task-legion-facing` as `b9f0f515`.
- `a6ce2dc0`, `f44e51d0` AI (levels 0-5; Retail AI unchanged): strikes weigh
  every enemy unit and building, seen or not, and keep going until the enemy
  is gone; a site per kind of mover; merged from `task-ai-targets` as
  `da5d26f4`.
- `afaacb9a` Combine: `kNetVersion` 303 -> 304, `replay_test` pins 304,
  retaken gates, scenario baseline and navigation checkpoints.

`git diff --stat 5b7a4f97 afaacb9a`: 35 files changed, 2401 insertions(+),
993 deletions(-); `-- src`: 18 files changed, 741 insertions(+), 145
deletions(-) (`src/sim` 4 files, `src/client` 11, `src/ai` 2, `src/net` 1,
none in `src/server`; the rest is tests, test baselines, `CMakeLists.txt`,
`CHANGELOG.md`, the documentation and the Options picture). 15 commits (9
without the merges). Only `CHANGELOG.md` conflicted in the merges.

TODO-LEAD: final `git diff --stat v0.9.10 v0.9.11` (whole tree and `-- src`) and
the commit count, once the tag exists.

The preparation commit on top changes no code: `CMakeLists.txt` (the version
line), `CHANGELOG.md`, `README.md`, the version and protocol lines in `docs/`,
the notes, this report, `docs/img/README.md` and `docs/img/title.jpg`.

**Compatibility.** `kNetVersion` is **304** in `src/net/protocol.h` (303 at
`v0.9.10`). `kReplayFormat` is **15** in `src/net/replayhdr.h`, unchanged. The
map generator's `formatVer` **9** is unchanged. The gameplay data hash of the
test install is `d6000f4fa9095a51`, as for 0.9.10 (server logs).

- **Multiplayer.** `Server::handshake` rejects a Hello whose protocol differs
  from `kNetVersion`; the release string is never compared. So **0.9.11 plays
  with no 0.9.10 or older client or server**.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  format is 1..15 and its protocol equals `kNetVersion`, so 0.9.11 refuses every
  0.9.10 (protocol 303) replay. `tools/replay_test.cpp` pins 304 and checks
  that 303 is refused.
- **Hashed state.** The hover move (`Unit::blockedTarget` state, hashed with a
  tag once a blocked impact is seen) and the factory's output dying change
  the game only where a hover attacker's shots stop on scenery, or a factory
  dies, changes hands or has its unit in work cancelled; no gate game does.
  Legion facing adds tracked member fields to the Legion checksum (folded
  into the state hash in Legion games); on its own it left the seven gates
  unchanged, and the scenario gate's Legion `hash@` keys moved. The AI's target choice changes every gate game that lasts long
  enough for a strike: **four gate hashes changed** (below).
- **Maps, mods and settings.** No map, recipe, package or settings format
  changed; `hiddenFadeOpacity` absent now reads as 50 (was 65), and a saved
  value is kept.

## Local preparation checks

All ran on Linux with optimized Debug builds (`build-0911-o2`, `-O2 -g`, ccache,
`TAK_TEST_DATA` set so the data-backed tests ran) and a Release build. Every
takclient and takserver run used scratch `HOME`, `XDG_DATA_HOME` and
`XDG_CONFIG_HOME`.

- **Builds.** At `afaacb9a` the binaries print `0.9.10 (build ...)`, because
  the version number changes in the preparation commit; the prepared tree's
  own `takclient --version` and `takserver --version` print
  `0.9.11 (build v0.9.10-16-gafaacb9a47b9-dirty)` from a Release build (the tree was dirty only with the
  preparation edits), and the title picture's Debug `-O2` build reads
  VERSION 0.9.11.
- **New tests.** `factory_death` (8 checks fail on `5b7a4f97`, 12 more on
  `a0585458`), the `blocked_shot` / `legion_blocked_shot` hover case (the
  reported Thirsha geometry: before 14 bolts on the stone and no damage, after
  4, one move and 10 hits), the `legion_world_test` facing cases (sideways /
  backward steps per 1,000 round a wall end 35/4 -> 9/1, round a block 69/11 ->
  16/3, arrivals unchanged), the `ai_behavior_test` strike cases, the
  `zoom_floor_*` harness cases (1920 x 1080 and 7680 x 2160, UI scale 100% and
  200%, spectating) and `settings_test` Fade Opacity 50.
- **Quick suite** (`ctest -L quick`, `build-0911-o2`, during the combine): 517
  of 519 passed. `navigation_determinism` failed until the Legion navigation
  checkpoints were retaken (in `afaacb9a`) and passes now; `vashim_unavailable` is the known
  local failure (a render node without libva; it failed in 0.9.10's run too).
  `replay`, `factory_death`, `blocked_shot` and the facing cases pass.
- TODO-LEAD: full suite (`ctest`, `build-0911-suite`) on the final code and
  the gates on the final code (result of the run in progress).
- **Detmath guard** (`tools/check-detmath.sh`, on the prepared tree): OK.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): golden
  `dcef618cd2e4d558` from g++ and clang++ at -O0, -O2 and -O3, unchanged from
  0.9.10; the aarch64 leg was skipped (no toolchain here). The tag CI Determinism workflow
  checks it too.
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`,
  `TAK_LEGION=1` for Legion, `TAK_AI_LEVEL=6` for the Retail AI, `--crusades`
  for the Crusades games, `TAK_SPEED=80`, `build-0911-o2`, all seven gates, run
  twice with identical hashes):

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `d0da4ccc71d2067c` (unchanged) | **`7a0be4440cd154b7`** (was `e820d37865085bd9`) |
| Legion | `c3f6a3a13f0c167f` (unchanged) | **`51989ae7896320d8`** (was `0783cb05f7f128cf`) |
| Retail AI (`TAK_AI_LEVEL=6`) | `4952493f51b9a568` (unchanged) | not run |
| Crusades, Retail pathfinding | not run | **`3e8c0ad3b4f1b859`** (was `f2f51dc1703fc209`) |
| Crusades, Legion | not run | **`0e2d8ac5fdbae8ca`** (was `c4189ae189dd68dc`) |

**The four 300 s hashes changed, and exactly as `task-ai-targets` predicted
on its own**: the AI's strikes now pick from every enemy unit. The 60 s gates
end before any strike, and the Retail AI is unchanged. The other five
branches leave all seven gates unchanged on their own.

- **Scenario baseline** (`tools/scenarios/baseline.json`): all 145 scenarios
  in both modes at the gate offsets; the values of 797 existing Legion
  `hash@` keys in 76 scenarios retaken (no Retail hash moved), and six
  offset-spread exceptions (median of 5) added with reasons for Legion facing:
  `walk_in_place` in `staticblock-own`, `staticblock-other`,
  `pocket-304-open`, `gap14` and `taperbridge-100`, and `sideways` in
  `taperbridge-small`. `validate` and `legion_baseline_reasons` report no
  problem.
- **Navigation checkpoints** (`tools/navigation_checkpoints.json`): the 24
  Legion lines retaken; Retail identical; serial equals workers, and a gcc
  -O2 build matches a clang Release build.
- **AI time to a winner** (all-AI games, debug builds, game minutes): the
  reported generated map with 8 Absurd, seed 1 no winner in 120 -> 82, seed 2
  80.8 -> 36.2; Ulasem Arena with four levels, no winner -> 34.8 and 54.7;
  Inner Circle Absurd against Absurd 25.6 -> 33.8 and 11.1 -> 25.8 (slower,
  noisy).
- **Legion facing** (31 scenario fixtures, offset 0): sideways steps 395,217 ->
  127,299, backward 86,133 -> 28,674 (Retail 277,784 / 9,722); arrivals, t90,
  done, spins, reversals and stop-go identical in all 31.

## Screenshots

`docs/img/guide/options-graphics.jpg` was retaken on `task-fade-50` before this
commit for the Fade Opacity default (50%; `docs/img/README.md`). In the
preparation, `docs/img/title.jpg` was retaken by the prepared tree's build
(Debug `-O2`, CMake `VERSION 0.9.11`) with the 0.9.1 title capture
(`takclient menu --shot`, offscreen driver, 1920 x 1080 window cropped to the
1440 x 1080 artwork). It reads VERSION 0.9.11 and shows the three doors
(looked at). The zoom floor, the hover move, the factory change, Legion facing
and the AI change no picture in the folder, so none other was retaken or
deleted. `docs/img/README.md` lists the details.

## Desync sweeps

No local or remote desync sweep (`tools/desync-hunt.sh`,
`tools/desync-hunt-remote.sh`) was run for 0.9.11, and no two-client game, so
no multiplayer run had a hover attacker moving round its ring or a factory's
output dying; both are covered by `blocked_shot_test` and `factory_death_test`,
the AI change by the server-run gates.

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

- No game mixing 0.9.11 with 0.9.10 or older is possible: protocol 304 is
  refused by every older server and client. No mixed-version game was run
  with a real 0.9.10 binary.
- No Windows or macOS `--mpai` for 0.9.11; their tag CI determinism tests
  cover them.
- No desync sweep and no multiplayer or remote run had a hover attacker
  re-aiming or a factory's output dying.
- **Two scenario checks still fail** with the retaken baseline (7 of 36,566
  keys), from `task-legion-facing` on its own (main passes them): `slowturn`
  Legion `walk_in_place`, an exact-match safety key that must stay 0, reads 26
  (0..78 per offset; arrivals and `walk_net` identical); `mixed2` Legion group
  F `done` is "never" on the core-five median with `left_behind` 1, plus four
  work keys (`contact_own_permille` 845, `work.slides.max` 8,
  `work.trace_cells.p99` 1562, `work.legion_total.p99` 2199; main already
  leaves 1 to 3 behind at several offsets). Neither was licensed or retaken.
  TODO-LEAD: ruling on these two (fix, exception or accepted limit).
- The Legion facing, AI, factory and hover limits are in the
  [release notes](release-0.9.11-notes.md#known-limits).
- TODO-LEAD: tag CI limits (failures, reruns, stalls).
- All of 0.9.10's limits carry over (see
  [release-0.9.10-validation.md](release-0.9.10-validation.md#limits)).
