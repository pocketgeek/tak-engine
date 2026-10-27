# 0.9.7 release validation

Protocol **301** (was 300 in 0.9.6), replay format **15** (unchanged),
generator version **9** (unchanged).

TODO-LEAD: tag line (tag `v0.9.7` at which commit, full sha, annotated tag
object, publication time).

## Scope and compatibility

0.9.7 follows 0.9.6 (protocol 300; tag `v0.9.6` -> `f4073c83`). It is one
feature branch, `task-097-helpbuild`, on top of `ee52e546`; the code tested
below is `c68fa4c5`. In order of landing:

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

`git diff --stat ee52e546 c68fa4c5`: 11 files changed, 922 insertions(+), 53
deletions(-); `-- src`: 7 files changed, 390 insertions(+), 50 deletions(-)
(`src/sim` 3 files, `src/client` 3, `src/net` 1; the rest is
`tools/helpbuild_test.cpp`, `tools/replay_test.cpp`, `CMakeLists.txt` and
`docs/multiplayer-design.md`).

TODO-LEAD: final `git diff --stat v0.9.6 v0.9.7` (whole tree and `-- src`) and
the commit count, once the tag exists.

The preparation commit on top, and the review's doc fixes after it, change no
code: `CMakeLists.txt` (the version line), `CHANGELOG.md`, `README.md`, the
version and protocol lines in `docs/`, the notes, this report,
`docs/img/README.md` and `docs/img/title.jpg`.

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
all at `c68fa4c5` in the worktree `~/tak-tmp/wt/helpbuild-097`. Every takclient
and takserver run used scratch `HOME`, `XDG_DATA_HOME` and `XDG_CONFIG_HOME`.

- **Builds.** All targets built clean in `build-o2` and in Release. At
  `c68fa4c5` the binaries print `0.9.6 (build ...)`, because the version number
  changes in the preparation commit; the prepared tree's own build prints
  `0.9.7` (checked on the Release `takclient --version` and the `build-o2` one).
- **`helpbuild` test** at the final code: all checks pass, and the same-hash
  determinism check gives `be0f5f65d009b19c` twice. The three checks added with
  `c68fa4c5` (the guard keeps within 250 px of a damaged unit that walks 900 px
  away, repairs it to full, and has its guard order back in front) fail on
  `c288f469` (the builder ends 904 px away and the unit stays on 50 hp; output in
  the lead's scratch folder `rev097fix/prefix_run.txt`) and pass at `c68fa4c5`.
- **Full suite**, nightly label included (`ctest -j12`, `build-o2`, 775 tests):
  **774 of 775** passed; the one failure is `vashim_unavailable`, the known
  failure on this host (the local `third_party/ffmpeg-bink` predates the libva
  shim, which CI builds); `vashim_system_loader` skipped (known).
- **Release quick suite** (550 tests, cores 4-23): two failures, neither a code
  failure: `vashim_unavailable` (as above) and `cli_tools`, because `build-rel`
  was configured without `TAK_TEST_DATA`, so the test got an empty `--data`
  argument. `cli_tools` passes in the full `build-o2` run above, which has the
  data.
- **Legion scenario gate.** `legion_check.py` ran all 145 scenarios in both
  modes at the gate offsets and passed: 36,522 keys, 0 failed, so no baseline
  hash key moved and no retake was needed.
- **Detmath guard** (`tools/check-detmath.sh`): OK.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): golden
  `dcef618cd2e4d558`, unchanged from 0.9.6.
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`,
  `TAK_LEGION=1` for Legion, `TAK_AI_LEVEL=6` for the Retail AI, `--crusades`
  for the Crusades games, `TAK_SPEED=80`, `build-o2`). Two full passes gave
  identical hashes, and a Retail 60 s run at normal speed (1x, `TAK_SPEED=10`)
  gave the same hash as at 80x.

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `d0da4ccc71d2067c` | `e820d37865085bd9` |
| Legion | `c3f6a3a13f0c167f` | `0783cb05f7f128cf` |
| Retail AI (`TAK_AI_LEVEL=6`) | `4952493f51b9a568` | not run |
| Crusades, Retail pathfinding | not run | `f2f51dc1703fc209` |
| Crusades, Legion | not run | `c4189ae189dd68dc` |

**The gate hashes are the 0.9.6 values.** The AIs never give Guard or Assist
orders and never have a helper on their production, so none of the new paths
runs in these games, and the new hashed fields stay zero. The cross-compiler
golden is unchanged for the same reason.

## Screenshots

One picture was retaken, by the prepared tree's build (Debug `-O2`, CMake
`VERSION 0.9.7`): `docs/img/title.jpg`, with the 0.9.1 title capture
(`takclient menu --shot`, offscreen driver, 1920 x 1080 window cropped to the
1440 x 1080 artwork). It reads VERSION 0.9.7 and shows the three doors (looked
at). Nothing else in 0.9.7 changes a picture, so none was retaken or deleted.
`docs/img/README.md` lists the details.

## Desync sweeps

**Local sweep.** `tools/desync-hunt.sh` with `BUILD=` the `build-o2` (Debug
`-O2 -g`), `TAK_AUTOPLAY=15` exported so the debug autoplay seat issues the new
orders, 8 jobs on cores 16-23, 20 minutes per case, all 34 cases. The summary
reads "no desyncs in any run", with 0 HIT or FAIL lines. 33 cases finished with
`err=none`; the benchmark case runs as a spectator and compares no hashes, as
always. No crash, no client or server desync and no referee suspect.

The order counts below come from a private, uncommitted copy of the branch
(`~/tak-tmp/wt/hb097-count`) that only adds stderr counters; it changes no sim
state, so its hashes equal the branch's, but the counts did not come from the
exact `build-o2` binaries. The gates and ctest ran on the real `build-o2`.

| What the sweep exercised (all 34 cases) | Count |
|---|---|
| Orders the autoplay seat issued | 11,086 |
| of which Train | 3,569 |
| of which Build | 2,228 |
| of which Guard | 2,736 |
| of which RepeatTrain | 1,298 |
| of which Assist | 1,255 |
| Guard started a HelpBuild | 261 |
| of which on a factory or conjurer product | 250 |
| Guard started a repair | 10 (all of the guarded unit itself) |
| Assists the sim accepted | 139 |
| of which on a product | 83 |
| Products a helper worked on (first touch) | 314 |
| Products a helper finished | 136 |

In every case the referee's counters equalled the client's exactly (0 cases
differed).

**Two-client games with the referee on.** Two games on Ulasem Arena (up to 900
s), one Retail and one Legion, each with 2 order-issuing clients (host and join,
`TAK_AUTOPLAY=30`) and 6 AIs: the referee logged 0 desync or suspect lines in
either, and the host, join and referee event streams were identical (same md5
up to each tick). Retail run: 7 assists, 2 guard repairs, 11 guard HelpBuilds,
18 helper-work events and 8 helper-finished products. Legion run: 3 assists, 6
guard HelpBuilds, 3 helper-work events and 2 helper-finished products.

**What the sweeps did not drive.** A guard repairing an ally (the AIs do not
damage their allies; the test covers it), a flying builder (escort only), and
the unreachable-repair skip. No remote multiplayer sweep
(`tools/desync-hunt-remote.sh`) was run for 0.9.7.

TODO-LEAD: say whether a remote multiplayer sweep is wanted before the tag.

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
- Flying builders that guard only escort; no test covers a guard skipping an
  unreachable repair; the repair re-aims on a once-a-second schedule (see the
  [release notes](release-0.9.7-notes.md#known-limits)).
- Local only: `vashim_unavailable` fails against a `third_party/ffmpeg-bink`
  built before the shim; CI builds FFmpeg with it. The Release `build-rel` here
  has no `TAK_TEST_DATA`, so its `cli_tools` fails.
- TODO-LEAD: tag CI limits (failures, reruns, stalls).
- All of 0.9.6's limits carry over (see
  [release-0.9.6-validation.md](release-0.9.6-validation.md#limits)).
