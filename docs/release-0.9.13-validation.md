# 0.9.13 release validation

Protocol **307** (was 305 in 0.9.12; 306 was never released), replay format
**15** (unchanged), generator version **9** (unchanged).

Tag `v0.9.13` -> `4ad86ec6baeb63a7473eccc8fc69761615cb1ad9` (the preparation
commit on top of `49f15e38`); annotated tag object
`bd1a764a321345c1caa29c10694d0e17fcc3e45f` (tagged 2026-10-31 22:27:49 UTC,
pushed). Published 2026-10-31 23:04:00 UTC as the Latest release.

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

`git diff --stat v0.9.12 v0.9.13`: 167 files changed, 193552 insertions(+),
140442 deletions(-), of which `tools/scenarios/baseline.json` is 174,298 and
139,541; `-- src`: 33 files changed, 4997 insertions(+), 580 deletions(-)
(`src/sim` 11 files, `src/client` 14, `src/ai` 4, `src/net` 4), unchanged
from the pre-tag count. 140 commits (118 without the merges). The 7 extra
files and the +707 / +31 lines against `49f15e38` are the preparation commit
(version, CHANGELOG, README, notes, this report, title picture).

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

All four tag workflows ran on `4ad86ec6` (tag `v0.9.13`), started 2026-10-31
22:27 UTC, and all passed on attempt 1: [Linux packages
41248289195](https://github.com/pocketgeek/tak-engine/actions/runs/41248289195)
(18 jobs, finished 22:49 UTC), [Windows
41248289193](https://github.com/pocketgeek/tak-engine/actions/runs/41248289193)
(4 jobs, finished 22:56 UTC), [macOS
41248289223](https://github.com/pocketgeek/tak-engine/actions/runs/41248289223)
(4 jobs, finished 22:52 UTC) and [Determinism
41248289286](https://github.com/pocketgeek/tak-engine/actions/runs/41248289286)
(3 jobs, about 6 minutes): detmath, sim-equiv and unit-tests all succeeded,
and GCC and Clang x86-64 at -O0/-O2/-O3 all agree on golden
`dcef618cd2e4d558` ("DETERMINISM OK", unchanged).

All 29 assets were downloaded fresh and match GitHub's SHA-256 digests and
sizes. The file set is identical to 0.9.12's: the same 29 asset names, and the
file lists of all 21 packages, installer and zips match 0.9.12's exactly
(after replacing the version string). 28 assets grew, from +34,628 bytes
(+0.36%, Slackware txz) to +1,041,211 bytes (+0.83%, Windows x64 debug zip,
the largest absolute growth; Windows arm64 debug zip +1.00% is the largest
relative growth). One shrank: the macOS arm64 DMG, by 19,687 bytes (-0.11%).
All 69 release `takclient`, `takserver` and `cartographer` binaries (48 Linux
ELF, 9 Windows including the installer's three, 12 macOS in the zips and
apps) carry the `v0.9.13` build string and no other `v0.9.x` string. None of
the 48 Linux ELF binaries has a libva, libdrm, libsecret, GLib or libsqlite
`NEEDED` entry, and none contains `__vaDriverInit_`, `SQLite format` or
`sqlite3_`. Each of the 48 binaries' `NEEDED` set equals its 0.9.12
counterpart's.

`strings` (ASCII and UTF-16) on all 69 release executables finds the `TAK_`
names only as expected, identical to 0.9.12: the 23 `takserver` binaries hold
exactly their four timers (`TAK_TIMEOUT_MS`, `TAK_LOAD_TIMEOUT_MS`,
`TAK_STALL_MS`, `TAK_DROP_WAIT_MS`); the 23 `takclient` binaries hold only
`TAK_MARK_OUTSIDE`; the 23 `cartographer` binaries hold only
`TAK_CARTOGRAPHER_RECOVERY` and `TAK_EDITOR_RULE_NAMES` plus their split
pieces; none contains `AUTOPLAY`, `TAK_START_FLYERS`, `TAK_NET_JITTER_MS`,
`TAK_FOG_EYE`, `TAK_CARTOGRAPHER_PROFILE`, `TAK_HIDDEN_UNITS`, `TAK_SHOT_MENU`,
`TAK_VERIFYPROF`, `TAK_PICK_INDEX_VERIFY`, `TAK_GHOST_VERIFY`,
`TAK_RENDER_STUDY` or `TAK_RENDER_MOUSE`. The licence files are in every
package, zip, installer and app (the Cartographer app carries
`EDITOR-FONT-LICENSE.txt` only, as before), and all `STREAMING-LICENSES.txt`
copies carry the OpenSSL notice. No `mods/`, `overrides/`, `click.hpi`,
ManaPost or CUTbugfix is shipped, in the packages or the debug archives. The
packaged Linux `takclient --help` (Fedora 44, Slackware and the 6 other
x86-64 packages that run here, identical text) lists only `--data`,
`--play-map`, `--game-mod` and `--version` (plus `--help`). The `takclient`,
`takserver` and `cartographer` of every x86-64 package, and the Linux x64
debug archive, report `0.9.13 (build v0.9.13)` (the Arch `takclient` needs
GLIBC 2.44 and cannot run on this Fedora host, as before; its `takserver` and
`cartographer` run).

The Fedora 44 x86-64 RPM's `takserver` printed "listening ... (protocol v307)"
with retail gameplay hash `d6000f4fa9095a51`. It ran the Retail AI game on
Inner Circle with `--local --no-auth --seed 1` and scratch HOME,
`XDG_DATA_HOME` and `XDG_CONFIG_HOME`. The client was the Debug build of
`4ad86ec6` (`build-dbg`, `0.9.13 (build v0.9.13)`) with `TAK_AI_LEVEL=6
TAK_SPEED=80 --mpai --time 60`. The game ended `hash=12003fba552b8696 units=4
err=none end=timelimit` (tick 1800), the expected value. The server's data
hash accepted the client, and no desync or referee-suspect line appeared; the
server was then killed by PID. The same RPM's `cartographer "Inner Circle"
--bundle <file>` (headless, scratch HOME) loaded the map, wrote a 379,088-byte
`.kmp`, exited 0 and left nothing in the scratch HOME. The packaged release
`takclient --data <read-only link farm> --play-map <that .kmp>` reached the
single-player lobby (client joined, game created, map verified, protocol
v307). The retail data folder was a `chmod -R a-w` symlink copy of
`assets/game` (76 entries plus the root). No entry in that copy changed in
name or mtime, and the profile files landed under the scratch
`XDG_DATA_HOME`. The draft's live body equals the prepared `body.md` apart
from one trailing newline. Every `blob` `v0.9.13` link in the release notes (5
distinct URLs: `docs/development.md`, `docs/perf-followup-2026-10.md`,
`docs/public-server.md#versions`,
`docs/release-0.9.12-notes.md#known-limits` and
`docs/release-0.9.13-validation.md`) resolves to a file present at the tag,
and both anchors exist.

## Signing

Windows: on oden-win, `Get-AuthenticodeSignature` reports **Valid**
(timestamped, "Curtis Edge") for all 7 signed files: the x64 installer and
`takclient`, `takserver` and `cartographer` in both the x64 and arm64 zips,
the same 7 as 0.9.12. The x64 `takclient.exe` and `takserver.exe` report
`0.9.13 (build v0.9.13)`, and the arm64 `takclient.exe` and `takserver.exe`
carry the `v0.9.13` build string. `--mpai` was not run on Windows or macOS,
per the release rule; the Windows and macOS tag CI determinism tests cover
them. The files were run from a scratch copy with a scratch profile; the real
profile listing on oden-win was identical before and after (372 file entries),
and the scratch folder was removed.

macOS: on the MacBook (no builds), both DMGs and all eight app copies (the game
and TAK Cartographer, from the DMGs and from the zips, arm64 and x64) pass
`stapler validate`, and `spctl -a -vv` accepts them as "Notarized Developer ID"
(Curtis Edge, L479Y59RN8). Both DMGs themselves are also stapled and accepted
as Notarized Developer ID. Each app copy passes `codesign --verify --strict
--deep` and has `NSHighResolutionCapable` true; the game apps have
`NSMicrophoneUsageDescription`, and the `takclient` has the `audio-input`
entitlement (the other binaries do not). The bare zip programs pass `codesign
--strict` with the same team id; `spctl` calls them "not an app", as expected.
The macOS `takclient` and `takserver` report `0.9.13 (build v0.9.13)` (arm64
and x64, apps, bare programs and the debug zips' takclient) and contain the MP4
muxer strings. No licence file is missing, and no `mods/`, `overrides/` or
`click.hpi` is present. `otool -L` of the shipped binaries shows no `libssl`,
`libcrypto`, `libsqlite` or libopus dylib. Both takclients contain `OpenSSL
3.5.8 25 Aug 2026` and no SecureTransport TLS symbols, and Security.framework
is still linked for the system root anchors.

`vtool -show-build` on all 24 Mach-O `takclient`, `takserver` and
`cartographer` binaries (the zips, the apps in the zips and DMGs, and the
debug zips, arm64 and x86_64) reports `LC_BUILD_VERSION platform MACOS minos
14.0 sdk 15.5`, linker LD 1167.5 (arm64 built on `macos-15`, as in 0.9.12),
and all 8 apps have `LSMinimumSystemVersion` 14.0 and version 0.9.13. The
Mac's real `TAKengine`, Preferences and Caches listing (107 files) was
identical before and after, and the scratch folder was removed.

## Publication

Published on 2026-10-31 23:04:00 UTC as the Latest release, 29 assets
(v0.9.13 -> `4ad86ec6`, tag object `bd1a764a`); 0.9.12 and earlier releases
are kept.

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
- Tag CI: Linux packages, Windows, macOS and Determinism all passed on
  attempt 1 (runs 41248289195, 41248289193, 41248289223 and 41248289286); no
  failure, rerun, stall or re-tag. The tag was created once, at `4ad86ec6`,
  and never moved. The macOS arm64 job ran on `macos-15` (deployment target
  14.0) as in 0.9.12.
- All of 0.9.12's limits carry over (see
  [release-0.9.12-validation.md](release-0.9.12-validation.md#limits)).
