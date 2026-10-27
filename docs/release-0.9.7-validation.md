# 0.9.7 release validation

Protocol **301** (was 300 in 0.9.6), replay format **15** (unchanged),
generator version **9** (unchanged).

TODO-LEAD: tag line (tag `v0.9.7` at which commit, full sha, annotated tag
object, publication time).

## Scope and compatibility

0.9.7 follows 0.9.6 (protocol 300; tag `v0.9.6` -> `f4073c83`). It is one
feature branch, `task-097-helpbuild`, on top of `ee52e546`, and the flying-guard
port, `task-097-flyguard`, on top of the prepared docs; the code tested below is
`7b810f95`. In order of landing:

- `1f9dc49f` Sim: retail HelpBuild, one shared production progress, sim-checked
  Assist (`kNetVersion` 300 -> 301).
- `9190f092` Sim: a guarding mobile builder helps and repairs (Follow_Ground
  stage 1).
- `5680c638` Client: a repeating conjurer may assist an unfinished unit.
- `677face9` Tests: `helpbuild_test` (registered as `helpbuild`).
- `c288f469` Harness: `TAK_AUTOPLAY` builds, trains, guards and assists (debug
  only).
- `c68fa4c5` Sim: a guard's repair follows its moving target (a review finding:
  a guarding builder that started a repair stayed where the target had been).
- `e8f083a1`, `aaf3ed60` Release preparation and its review fixes (no code).
- `f925aa4c` Sim: flying builders help and repair from guard (the original's
  `VTOL_Follow`, with `VTOL_HelpBuild` / `VTOL_RepairUnit` reach and circling).
- `7b810f95` Sim: a flyer's repair check refuses a target under the sea (review
  fixes: the refusal lets a flying guard go on to its next checks, a flyer's
  Repair order and patrol repair skip such targets, and a flying repair
  measures reach in whole pixels).

`git diff --stat ee52e546 7b810f95`: 20 files changed, 1886 insertions(+), 99
deletions(-); `-- src`: 7 files changed, 542 insertions(+), 67 deletions(-)
(`src/sim` 3 files, `src/client` 3, `src/net` 1; the rest is
`tools/helpbuild_test.cpp`, `tools/replay_test.cpp`, `CMakeLists.txt` and
documentation).

TODO-LEAD: final `git diff --stat v0.9.6 v0.9.7` (whole tree and `-- src`) and
the commit count, once the tag exists.

The preparation commit and the review's doc fixes after it change no code:
`CMakeLists.txt` (the version line), `CHANGELOG.md`, `README.md`, the version
and protocol lines in `docs/`, the notes, this report, `docs/img/README.md` and
`docs/img/title.jpg`.

**Compatibility.** `kNetVersion` is **301** in `src/net/protocol.h` (300 at
`v0.9.6`). `kReplayFormat` is **15** in `src/net/replayhdr.h`, unchanged. The
map generator's `formatVer` **9** is unchanged.

- **Multiplayer.** `Server::handshake` rejects a Hello whose protocol differs
  from `kNetVersion`; the release string is never compared. So **0.9.7 plays
  with no 0.9.6 or older client or server**.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  format is 1..15 and its protocol equals `kNetVersion`, so 0.9.7 refuses every
  0.9.6 (protocol 300) replay. `tools/replay_test.cpp` pins 301 and refuses 300.
- **Hashed state.** It gains `Unit::guardHelpTick`, `Order::helpSite` and
  `Order::guardRepair` (each folded in when set); shared production progress
  changes the game only where a helper works on a produced unit. No gate game
  does, so **the gate hashes did not change** (below).
- **Maps, mods and settings.** No map, recipe, package or settings format
  changed.

## Local preparation checks

All ran on Linux with optimized Debug builds (`build-o2`, `-O2 -g`, ccache +
mold, `TAK_TEST_DATA` set so the data-backed tests ran) and a Release build,
all at `7b810f95` in the worktree `~/tak-tmp/wt/flyguard-097`. Every takclient
and takserver run used scratch `HOME`, `XDG_DATA_HOME` and `XDG_CONFIG_HOME`.

- **Builds.** All targets built clean in `build-o2` and in Release
  (`build-rel`). The Release build was only built: a release `takclient` is
  hardened and cannot run the gates, which use the `build-o2` binaries.
- **`helpbuild` test** at the final code: all checks pass. Its determinism
  checks give the same hash twice: `be0f5f65d009b19c` for guard-help, assist and
  shared production (the same value as before the flying-guard port) and
  `323ecfd1bcbee9b2` for flying guards' help and repair.
- **Full suite**, nightly label included (`ctest -j12`, `build-o2`, 775 tests):
  **774 of 775** passed; the one failure is `vashim_unavailable`, the known
  failure on this host (the local `third_party/ffmpeg-bink` predates the libva
  shim, which CI builds); `vashim_system_loader` skipped (known). No test timed
  out.
- **Legion scenario gate.** `legion_check` validate reports 0 problems and the
  `legion_check_*` tests passed, so no baseline hash key moved and no retake was
  needed.
- **Detmath guard** (`tools/check-detmath.sh`): OK.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): every build
  agrees on `dcef618cd2e4d558`, unchanged from 0.9.6 (the aarch64 legs were
  skipped: no toolchain on this host).
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`,
  `TAK_LEGION=1` for Legion, `TAK_AI_LEVEL=6` for the Retail AI, `--crusades`
  for the Crusades games, `TAK_SPEED=80`, `build-o2`). Two full passes gave
  identical hashes, and a Retail 60 s run at normal speed (1x) gave the same
  hash as at 80x.

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `d0da4ccc71d2067c` | `e820d37865085bd9` |
| Legion | `c3f6a3a13f0c167f` | `0783cb05f7f128cf` |
| Retail AI (`TAK_AI_LEVEL=6`) | `4952493f51b9a568` | not run |
| Crusades, Retail pathfinding | not run | `f2f51dc1703fc209` |
| Crusades, Legion | not run | `c4189ae189dd68dc` |

**The gate hashes are the 0.9.6 values.** The AIs never give Guard or Assist
orders and never have a helper on their production, so none of the new paths
runs in these games (flying guards included), and the new hashed fields stay
zero. The cross-compiler golden is unchanged for the same reason.

## Screenshots

One picture was retaken, by the prepared tree's build (Debug `-O2`, CMake
`VERSION 0.9.7`): `docs/img/title.jpg`, with the 0.9.1 title capture
(`takclient menu --shot`, offscreen driver, 1920 x 1080 window cropped to the
1440 x 1080 artwork). It reads VERSION 0.9.7 and shows the three doors (looked
at). Nothing else in 0.9.7 changes a picture, so none was retaken or deleted.
`docs/img/README.md` lists the details.

## Desync sweeps

**Local sweep.** `tools/desync-hunt.sh --minutes 20 --jobs 8` at `7b810f95`,
with `TAK_AUTOPLAY=15` exported so the debug autoplay seat issues the new
orders, on cores 16-23 after the gates had finished: 33 runs, **no desyncs**, no
referee suspect, no client desync report, and every run ended with `err=none`.
Two of them are the benchmark runs (`flow-bench-high`, `flow-bench-absurd`),
which compare no hashes by design.

The counts below come from a private, uncommitted copy of `7b810f95`
(`~/tak-tmp/wt/flyguard-count`) that only adds stderr counters; it changes no
sim state (it gives the Retail 60 s gate hash, `d0da4ccc71d2067c`), but the
counts did not come from the exact `build-o2` binaries.

| What the sweep exercised (33 runs) | Count |
|---|---|
| Guard orders the autoplay seat issued | 2,830 |
| Assist orders | 1,285 |
| Train orders | 3,649 |
| Build orders | 2,245 |
| RepeatTrain orders | 1,350 |
| Ground guard decisions (help or repair started) | 275 |
| of which HelpBuild | 261 |
| of which repair | 14 |
| Ground guard help work, in ticks | 342,769 |
| Flying guard helps or repairs | 0 |

**The referee compared every client.** In every run the referee's guard
decisions equalled the clients' exactly. (The work-tick count comes from the
client logs only: the referee is stopped before it prints its totals.)

**Flying guards were not exercised in multiplayer.** The autoplay seat plays
Aramon in every run and never trained a Flying Builder. 40 more targeted runs
from scratch harness changes (the seat on other sides, finally Taros, biased
towards training flying builders, flyers forced to take the guard turn) also
had no desync and matching guard decisions, but still no flying guard helped or
repaired: the Dark Priest was usually still unfinished, and one that finished
held a guard order with nothing to help. Flying guards are covered by
`helpbuild_test` only.

**What the sweeps did not drive.** A guard repairing an ally (the AIs do not
damage their allies; the test covers it), a flying builder on guard (above),
and the unreachable-repair skip.

No remote multiplayer sweep (`tools/desync-hunt-remote.sh`) was run for 0.9.7;
the local sweep with the autoplay hook compared every client against the
referee.

## CI and packages

TODO-LEAD: tag workflows and their results, package verification (assets,
digests, build strings, `--version` of each package).

## Signing

TODO-LEAD: Windows Authenticode and macOS notarisation results.

## Publication

TODO-LEAD: publication time, asset count, Latest flag.

## Limits

- No game mixing 0.9.7 with 0.9.6 or older is possible: protocol 301 is refused
  by every older server and client. No mixed-version game was run with a real
  0.9.6 binary.
- No Windows or macOS `--mpai` for 0.9.7; their tag CI determinism tests cover
  them.
- A flying guard holds near the guarded unit instead of circling it, and no
  multiplayer run has had a flying guard help or repair; no test covers a guard
  skipping an unreachable repair; the repair re-aims on a once-a-second schedule
  (see the [release notes](release-0.9.7-notes.md#known-limits)).
- Local only: `vashim_unavailable` fails against a `third_party/ffmpeg-bink`
  built before the shim; CI builds FFmpeg with it.
- TODO-LEAD: tag CI limits (failures, reruns, stalls).
- All of 0.9.6's limits carry over (see
  [release-0.9.6-validation.md](release-0.9.6-validation.md#limits)).
