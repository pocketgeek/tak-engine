# 0.9.7 release validation

Protocol **301** (was 300 in 0.9.6), replay format **15** (unchanged),
generator version **9** (unchanged).

Tagged `v0.9.7` at `3c4cf3b2` (full `3c4cf3b2c1be40465995bcfb581c550543ffb7a6`, annotated tag object `58ebb376`) and published on 2026-10-27 19:48:12 UTC.

## Scope and compatibility

0.9.7 follows 0.9.6 (protocol 300; tag `v0.9.6` -> `f4073c83`). It is one
feature branch, `task-097-helpbuild`, on top of `ee52e546`, and the flying-guard
port, `task-097-flyguard`, on top of the prepared docs, then its idle orbit,
`task-097-flyorbit`; the code tested below is `3b44b94c`. In order of landing:

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
- `ff17f1e4` 0.9.7 docs for the flying guards (no code).
- `1e1c8a98` Sim: flying guards orbit the guarded unit (the original's
  `VTOL_Follow` idle orbit, for every flyer on guard), with the debug-only
  `TAK_START_FLYERS` / `TAK_AUTOPLAY_SIDE` harness hooks and a flying-guard
  autoplay turn.
- `3b44b94c` Sim: a flying guard's look deadline lives on its mission, and an
  off-map start keeps the guard (review fixes: a flyer told to guard while off
  the map lost its guard on the way back; a queued guard inherited the earlier
  guard's sleep, or never looked after an off-map wait).
- The docs commit after it changes no code but a comment in the debug-only
  `TAK_START_FLYERS` fixture.

`git diff --stat ee52e546 3b44b94c`: 21 files changed, 2385 insertions(+), 114
deletions(-); `-- src`: 7 files changed, 755 insertions(+), 74 deletions(-)
(`src/sim` 3 files, `src/client` 3, `src/net` 1; the rest is
`tools/helpbuild_test.cpp`, `tools/flyer_combat_test.cpp`,
`tools/replay_test.cpp`, `CMakeLists.txt` and documentation).

Final `git diff --stat v0.9.6 v0.9.7 -- src`: 7 files changed, 756 insertions(+), 74 deletions(-) (`src/sim` 3 files, `src/client` 3, `src/net` 1); that is `3b44b94c` plus one comment in `src/sim/matchsetup.cpp` (the debug-only `TAK_START_FLYERS` fixture). Outside `src`: 15 files changed, 1,725 insertions(+), 46 deletions(-). Over the whole tree: 22 files changed, 2,481 insertions(+), 120 deletions(-). `v0.9.6..v0.9.7` is 15 commits, linear, no merges. The tag differs from `3b44b94c` only in `CHANGELOG.md`, `docs/release-0.9.7-notes.md`, `docs/user-guide.md`, this report and that one comment.

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
  `Order::guardRepair` (each folded in when set), and a flying guard's mission
  with `Order::guardOrbitAngle` (folded in once the guard runs); shared production progress
  changes the game only where a helper works on a produced unit. No gate game
  does, so **the gate hashes did not change** (below).
- **Maps, mods and settings.** No map, recipe, package or settings format
  changed.

## Local preparation checks

All ran on Linux with optimized Debug builds (`build-o2`, `-O2 -g`, ccache +
mold, `TAK_TEST_DATA` set so the data-backed tests ran) and a Release build,
all at `3b44b94c` in the worktree `~/tak-tmp/wt/flyorbit-097`. Every takclient
and takserver run used scratch `HOME`, `XDG_DATA_HOME` and `XDG_CONFIG_HOME`.

- **Builds.** All targets built clean in `build-o2` and in Release
  (`build-rel`). The Release build was only built: a release `takclient` is
  hardened and cannot run the gates, which use the `build-o2` binaries.
- **`helpbuild` test** at the final code: all checks pass, including the new
  orbit sections (the 320 px and 360 px rings, 128 px acceptance, angle steps of
  0x4000 to 0x5fff only on arrival, the next-tick wake, an orbit that follows a
  moving unit, the orbit resuming after a repair, an off-map guard that returns
  and keeps its guard, a queued guard that starts at once). Its determinism
  checks give the same hash twice: `be0f5f65d009b19c` for guard-help, assist and
  shared production (unchanged since before the flying-guard port),
  `aee0614708017998` for flying guards' help and repair, and `258154c60853265e`
  for flying guards' orbits. The orbit checks failed on `ff17f1e4` and the
  off-map and queued-guard checks on `1e1c8a98`. `flyer_combat_test` now checks
  that a guarding Zongryp or Creaeri circles its charge.
- **Full suite**, nightly label included (`ctest -j12`, `build-o2`, 771 tests):
  **769 of 771** passed. `vashim_unavailable` is the known failure on this host
  (the local `third_party/ffmpeg-bink` predates the libva shim, which CI
  builds); `vashim_system_loader` skipped (known). `disconnect_client` failed 3
  of its 4 cases ("the verified game mods for this replay are missing") and
  fails the same way on `ff17f1e4`, so this change did not cause it; it looks
  environmental (a game-mod pack the replay names is not found). No test timed
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
runs in these games (flying guards and their circling included), and the new
hashed fields stay zero. The cross-compiler golden is unchanged for the same reason.

## Screenshots

One picture was retaken, by the prepared tree's build (Debug `-O2`, CMake
`VERSION 0.9.7`): `docs/img/title.jpg`, with the 0.9.1 title capture
(`takclient menu --shot`, offscreen driver, 1920 x 1080 window cropped to the
1440 x 1080 artwork). It reads VERSION 0.9.7 and shows the three doors (looked
at). Nothing else in 0.9.7 changes a picture, so none was retaken or deleted.
`docs/img/README.md` lists the details.

## Desync sweeps

**Local sweep.** `tools/desync-hunt.sh --minutes 20 --jobs 8` at `3b44b94c`
(`build-o2`), with `TAK_AUTOPLAY=15` exported so the debug autoplay seat issues
the new orders, on cores 16-23 and scratch `HOME` / `XDG_*`: 34 runs, **no
desyncs**, no referee suspect, and every run finished cleanly. Two of them are
the benchmark runs (`flow-bench-high`, `flow-bench-absurd`), which compare no
hashes by design. (An earlier sweep of the same size at `7b810f95` counted
2,830 Guard and 1,285 Assist orders from the autoplay seat and 275 ground guard
decisions, 261 HelpBuild and 14 repairs, matching the referee exactly, but no
flying guard ever got work.)

**Flying guards in multiplayer.** Twelve headless 900 s games at `3b44b94c`,
each a `takserver` referee with `--seed` plus two autoplay clients
(`--mphost` / `--mpjoin`), `TAK_START_FLYERS=4` on every peer,
`TAK_AUTOPLAY=15`, `TAK_SPEED=80`: Inner Circle with no AIs (seeds 1, 2, 3, 9,
11, 13) and Ulasem Arena with 2 to 6 AIs (seeds 4 to 8 and 12), with Aramon,
Taros and Zhon seats in six side pairings. The counts come from a private,
uncommitted copy of `3b44b94c` (`~/tak-tmp/wt/flyorbit-count`) that only adds
stderr counters, printed every 1,800 ticks by every peer.

- **Every counter line was identical** on both clients and the referee at every
  common tick, and there were no `DESYNCED`, `REFEREE SUSPECT` or crashes.
  Both clients ended on the same hash, except in one Ulasem game (seed 5) whose
  first client's team was eliminated at tick 21,968: it is compared up to
  there, and the second client played on to tick 27,000.
- Over the twelve games (final checkpoint, tick 25,200; 21,600 for seed 5),
  flying guards took on **37 help and 64 repair jobs**, worked 298,124 help
  ticks and 81,228 repair ticks (about 52,500 HP repaired), installed 32,137
  orbit points and stepped the angle on 2,552 arrivals. Ground guards took on
  59 help and 7 repair jobs (47,843 and 9,824 work ticks), alongside 68 assists
  and 983 finished products.
- **Zhon needed scratch harness changes.** The Zhon flying builder is its
  Monarch (Thirsha), which `TAK_START_FLYERS` skips and the committed autoplay
  never puts on guard. Three of the twelve games (seeds 11, 12, 13) used a
  scratch fixture that also puts a working flying Monarch on guard; there the
  Monarch took on 8 jobs, installed 3,222 orbit points and worked 14,975 help
  and 3,510 repair ticks. Six more Zhon runs (three with the committed harness,
  three escort-only) also matched on every peer and are not in the totals.

**What the sweeps did not drive.** A guard repairing an ally (the AIs do not
damage their allies; the test covers it), the unreachable-repair skip, and
Zhon flying guards without the scratch fixture.

No remote multiplayer sweep (`tools/desync-hunt-remote.sh`) was run for 0.9.7;
the local runs compared every client against the referee.

## CI and packages

All four tag workflows ran on `3c4cf3b2` and are green. Three passed on attempt 1: [Linux packages 40564746420](https://github.com/pocketgeek/tak-engine/actions/runs/40564746420) (18 jobs, all success, finished 19:29 UTC), [Windows 40564746132](https://github.com/pocketgeek/tak-engine/actions/runs/40564746132) (x64, arm64, sign and publish, cache prune; finished 19:21 UTC) and [macOS 40564746434](https://github.com/pocketgeek/tak-engine/actions/runs/40564746434) (arm64 and x64 build, then sign and notarize for each; finished 19:19 UTC). [Determinism 40564746743](https://github.com/pocketgeek/tak-engine/actions/runs/40564746743) (dispatched on the tag) needed a second attempt: on attempt 1 detmath and sim-equiv passed, but the `unit-tests` job sat in "Install build deps" from 18:59:08 to 19:23:04 UTC (about 24 minutes, a runner stall, no output) and was cancelled; attempt 2 ran `unit-tests` in about 10 minutes and passed (`100% tests passed out of 8`). GCC and Clang x86-64 at -O0/-O2/-O3 all agree on golden `dcef618cd2e4d558` ("DETERMINISM OK", unchanged from 0.9.6); the driver-equivalence test printed OK.

All 29 assets were downloaded fresh and match GitHub's SHA-256 digests and sizes. The file set is identical to 0.9.6's: the per-asset file lists of the 27 packages, installer, zips and debug archives match 0.9.6's exactly (after replacing the version string), and the two DMGs hold the same three entries (the two apps and the Applications link). Sizes grew slightly, from +0.07% (openSUSE x86-64 RPM) to +0.19% (Arch package); the Slackware package shrank by 0.10% (-8,924 bytes); the largest absolute growth is the Linux x64 debug tarball at +86,572 bytes. Every one of the 54 Linux ELF binaries carries the `v0.9.7` build string and no older `v0.9.x` string. Every one of the 46 release takclient and takserver binaries (Linux, Windows, macOS, apps and bare programs) contains no `AUTOPLAY`, `TAK_START_FLYERS` or `TAK_AUTOPLAY_SIDE` string, so the harness hooks are debug-only; the Linux and macOS debug archives do contain them, as intended. None of the 54 Linux ELF binaries has a libva, libdrm, libsecret, GLib or libsqlite `NEEDED` entry, and none contains `__vaDriverInit_`, `SQLite format` or `sqlite3_`; the `NEEDED` list of every one of the 54 is identical to 0.9.6's. The licence files (`OPUS-LICENSE.txt`, `CHAT-FONT-LICENSE.txt`, `EDITOR-FONT-LICENSE.txt`, `STREAMING-LICENSES.txt`, `ACME-LICENSES.txt`) are in every package, zip, installer, DMG and app (the Cartographer app carries `EDITOR-FONT-LICENSE.txt` only, as before). All 27 `STREAMING-LICENSES.txt` copies are byte-identical to 0.9.6's (the same two checksums, 22 and 5 copies) and all 27 carry the OpenSSL notice. No `mods/`, `overrides/` or `click.hpi` is shipped. The packaged Linux `takclient --help` lists only `--data`, `--play-map`, `--game-mod` and `--version`, and the Fedora 44 x86-64 RPM's `takclient`, `takserver` and `cartographer` all report `0.9.7 (build v0.9.7)`. That RPM's `takserver` printed "listening ... (protocol v301)" with retail gameplay hash `d6000f4fa9095a51`. It ran the Retail AI game on Inner Circle with `--local --no-auth --seed 1` and a scratch HOME and `XDG_DATA_HOME`; the client was the Debug build at `3c4cf3b2` (`build-dbg`, rebuilt for this check) with `TAK_AI_LEVEL=6 TAK_SPEED=80 --mpai --time 60`, and the game ended `hash=4952493f51b9a568 units=4 err=none end=timelimit` (tick 1800), the same as the local gate in this report; no desync or referee-suspect line appeared. The same RPM's `cartographer "Inner Circle" --bundle <file>` (headless) loaded the map (96 x 96 blocks, 4 start positions, 215 features, 1261 sections), wrote a 379,088-byte `.kmp` and exited 0, as in 0.9.6. The packaged release `takclient --data <read-only link farm> --play-map <that .kmp>` reached the single-player lobby (local server on a private port, game created, map verified, 141 features placed, 4 mana deposits) with the retail data folder as a `chmod a-w` copy that symlinks every file (1,108 entries, the same count as the real `assets/game`). Nothing was created or modified in that copy or in the real `assets/game`, and the map cache, mod cache and `settings.ini` landed under the scratch `XDG_DATA_HOME/TAKengine/TAKingdoms`. Every `blob` `v0.9.7` link in the release notes (5 links, 3 with anchors: `public-server.md#versions`, `release-0.9.5-notes.md#known-limits` and `release-0.9.6-notes.md#known-limits`) resolves to a file and heading present at the tag.

## Signing

Signing. Windows: on oden-win, `Get-AuthenticodeSignature` reports **Valid** (timestamped, "Curtis Edge") for all 7 signed files: the x64 installer and `takclient`, `takserver` and `cartographer` in both the x64 and arm64 zips, the same 7 as 0.9.6. The x64 `takclient.exe` and `takserver.exe` report `0.9.7 (build v0.9.7)`, and the arm64 ones carry the `v0.9.7` build string. `--mpai` was not run on Windows or macOS, per the release rule (no build flags, compilers or CI determinism coverage changed); the Windows and macOS tag CI determinism tests cover them.

macOS: on the MacBook (no builds), both DMGs and all four apps (the game and TAK Cartographer, from the DMGs and from the zips) pass `stapler validate`, and `spctl -a -vv` accepts them as "Notarized Developer ID" (Curtis Edge, L479Y59RN8). Both DMGs themselves are also stapled and accepted as Notarized Developer ID. Each app passes `codesign --verify --strict --deep` and has `NSHighResolutionCapable` true; the game apps have `NSMicrophoneUsageDescription`, and the takclient has the `audio-input` entitlement (the other binaries do not). The bare zip programs (takclient, takserver, cartographer) pass `codesign --strict` with the same team id; `spctl` calls them "not an app", as expected. The macOS `takclient` and `takserver` report `0.9.7 (build v0.9.7)` (arm64 and x64, apps, bare programs and the debug zips' takclient) and contain the MP4 muxer strings. No licence file is missing, and no `mods/`, `overrides/` or `click.hpi` is present. `otool -L` of the shipped arm64 and x64 `takclient`, `takserver` and `cartographer` shows no `libssl`, `libcrypto`, `libsqlite` or libopus dylib. Both takclients contain `OpenSSL 3.5.8 25 Aug 2026` and no SecureTransport TLS symbols, and Security.framework is still linked for the system root anchors.

## Publication

Published with `gh release edit v0.9.7 --draft=false --latest` on 2026-10-27 19:48:12 UTC with 29 assets; it is the Latest release. 0.9.0 to 0.9.6 stay on the releases page.

## Limits

- No game mixing 0.9.7 with 0.9.6 or older is possible: protocol 301 is refused
  by every older server and client. No mixed-version game was run with a real
  0.9.6 binary.
- No Windows or macOS `--mpai` for 0.9.7; their tag CI determinism tests cover
  them.
- The original flying guard's attacked-guarded-unit reaction, landed step-out
  and attack scan are not ported, and a flying builder busy with its own job
  hovers near the guarded unit until its guard runs; Zhon flying guards were
  driven only by scratch harness changes; no test covers a guard skipping an
  unreachable repair; the repair re-aims on a once-a-second schedule (see the
  [release notes](release-0.9.7-notes.md#known-limits)).
- Local only: `disconnect_client` fails 3 of 4 cases here, on the base
  `ff17f1e4` too (a game-mod pack its replay names is missing); not yet looked
  into.
- Local only: `vashim_unavailable` fails against a `third_party/ffmpeg-bink`
  built before the shim; CI builds FFmpeg with it.
- Tag CI: no failures. Linux packages, Windows and macOS passed on attempt 1. Determinism needed one rerun: its `unit-tests` job stalled in "Install build deps" for about 24 minutes on attempt 1 (a runner stall; it had not started building) and was cancelled; attempt 2 passed in about 10 minutes, and detmath and sim-equiv passed on attempt 1.
- All of 0.9.6's limits carry over (see
  [release-0.9.6-validation.md](release-0.9.6-validation.md#limits)).
