# 0.9.11 release validation

Protocol **304** (was 303 in 0.9.10), replay format **15** (unchanged),
generator version **9** (unchanged).

Tagged `v0.9.11` at `fbae4bbd` (full
`fbae4bbd90c63fc00b37ff7c029c7f42b7f23181`, annotated tag object `184058da`)
and published on 2026-10-29 07:39:08 UTC.

## Scope and compatibility

0.9.11 follows 0.9.10 (protocol 303; tag `v0.9.10` -> `99ccf7e5`). Six
branches, each from main `5b7a4f97` (0.9.10's validation commit) and each
reviewed on its own, were merged on `task-0911`; the code tested below is
`afaacb9a` and, where a check says so, the walk-in-place repair `96841296`
on top (the last item):

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
- `96841296` Sim (Legion only), from the release review: a Legion body's
  stand in its scripts (`Unit::legionStill`, display only) is judged by the
  steps and turn the body shows, in a window of its own
  (`World::legionBodyWindow_`), not by its course's turn; the livelock and
  every movement decision keep the course. Legion `hash@` keys, navigation
  checkpoints and the three Legion gates retaken, six `mixed2` keys
  licensed (Limits).

`git diff --stat 5b7a4f97 afaacb9a`: 35 files changed, 2401 insertions(+),
993 deletions(-); `-- src`: 18 files changed, 741 insertions(+), 145
deletions(-) (`src/sim` 4 files, `src/client` 11, `src/ai` 2, `src/net` 1,
none in `src/server`; the rest is tests, test baselines, `CMakeLists.txt`,
`CHANGELOG.md`, the documentation and the Options picture). 15 commits (9
without the merges). Only `CHANGELOG.md` conflicted in the merges.

Final `git diff --stat v0.9.10 v0.9.11`: 43 files changed, 3238 insertions(+),
1204 deletions(-); `-- src`: 18 files changed, 767 insertions(+), 148
deletions(-) (`src/sim` 4 files, `src/client` 11, `src/ai` 2, `src/net` 1,
none in `src/server`); outside `src`: 25 files changed, 2471 insertions(+),
1056 deletions(-) (tests, test baselines, `CMakeLists.txt`, `CHANGELOG.md`,
the documentation and the pictures). 20 commits from `v0.9.10` to `v0.9.11`
(14 without the merges), including `5b7a4f97`, 0.9.10's own validation commit,
so 19 come after it.

Three commits sit on top of the preparation commit `4cbe54f6`, and the tag
points at the last, `fbae4bbd`. `ef11921a` (full suite and final-code gates)
and `fbae4bbd` (the walk-in-place repair, mixed2 ruling and Legion gates)
change only the notes and this report. `96841296` changes code, in `src/sim`
only (`legion.cpp`, `legion.h`, `sim.cpp`, `sim.h`: 4 files, 32 insertions, 9
deletions). It also touches `tools/legion_world_test.cpp`,
`docs/legion-pathfinding.md`, the Legion `hash@` keys in
`tools/scenarios/baseline.json` and `tools/navigation_checkpoints.json`, and
the notes and this report. The preparation commit itself changes no code. It
touches `CMakeLists.txt` (the version line), `CHANGELOG.md`, `README.md`, the
version and protocol lines in `docs/`, the notes, this report,
`docs/img/README.md` and `docs/img/title.jpg`.

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
  unchanged, and the scenario gate's Legion `hash@` keys moved. The
  walk-in-place repair's stand window (hashed with the Legion unit state)
  changes the three Legion gates. The AI's target choice changes every gate
  game that lasts long enough for a strike: **five gate hashes changed** in
  all (below).
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
  `0.9.11 (build v0.9.10-16-gafaacb9a47b9-dirty)` from a Release build (the
  tree was dirty only with the preparation edits), and the title picture's
  Debug `-O2` build reads
  VERSION 0.9.11.
- **New tests.** `factory_death` (8 checks fail on `5b7a4f97`, 12 more on
  `a0585458`), the `blocked_shot` / `legion_blocked_shot` hover case (the
  reported Thirsha geometry: before 14 bolts on the stone and no damage, after
  4, one move and 10 hits), the `legion_world_test` facing cases (sideways /
  backward steps per 1,000 round a wall end 35/4 -> 9/1, round a block 69/11 ->
  16/3, arrivals unchanged; with `96841296` a turn-400 group round a wall's
  top at five start shifts, 0, 8, 18, 18 and 0 walk-in-place ticks on
  `afaacb9a`, 0 now), the `ai_behavior_test` strike cases, the
  `zoom_floor_*` harness cases (1920 x 1080 and 7680 x 2160, UI scale 100% and
  200%, spectating) and `settings_test` Fade Opacity 50.
- **Quick suite** (`ctest -L quick`, `build-0911-o2`, during the combine): 517
  of 519 passed. `navigation_determinism` failed until the Legion navigation
  checkpoints were retaken (in `afaacb9a`) and passes now; `vashim_unavailable` is the known
  local failure (a render node without libva; it failed in 0.9.10's run too).
  `replay`, `factory_death`, `blocked_shot` and the facing cases pass. On
  `96841296` (all targets rebuilt): 518 of 519 passed, `vashim_unavailable`
  the one failure, `navigation_determinism`, `legion_facing` and
  `legion_baseline_reasons` among the passes.
- **Full suite on `afaacb9a`** (before the walk-in-place repair, which ran
  the quick suite only; Debug `-O2`, all targets
  built, `ctest -j12`, scratch `HOME` and XDG folders, test data
  `assets/game`, which has no mods): 785 of 787 passed, one skipped
  (`vashim_system_loader`, which skips itself). One failed:
  `vashim_unavailable`, the known local failure (a render node without libva;
  CI builds FFmpeg with the shim and runs it there; it failed in 0.9.10's run
  too). `disconnect_client` passes on the mod-free data; 0.9.10's failure came
  from the always-enabled game mod in `~/tak_data` and is still a separate
  follow-up. `navigation_determinism` (with the retaken Legion checkpoints),
  `replay`, `factory_death`, `blocked_shot`, `legion_blocked_shot`,
  `legion_facing`, `settings` and both `zoom_floor_*` cases pass. Wall time
  1,315 s.
- **Gates on the final code** (`96841296`, the same `--mpai` set-up as below,
  `build-0911-o2` on ports 7981-7987): all seven hashes equal the table
  below in three runs (the third after the last rebuild). All seven servers
  loaded retail gameplay hash `d6000f4fa9095a51`, and no desync, mismatch or
  referee-suspect line appeared. On `afaacb9a` the suite build had matched
  the two `build-0911-o2` runs of that code (the hashes before the repair,
  below the table).
- **Detmath guard** (`tools/check-detmath.sh`, on the prepared tree): OK.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): golden
  `dcef618cd2e4d558` from g++ and clang++ at -O0, -O2 and -O3, unchanged from
  0.9.10, and again on `96841296` with `check-detmath.sh` OK; the aarch64 leg
  was skipped (no toolchain here). The tag CI Determinism workflow
  checks it too.
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`,
  `TAK_LEGION=1` for Legion, `TAK_AI_LEVEL=6` for the Retail AI, `--crusades`
  for the Crusades games, `TAK_SPEED=80`, `build-0911-o2`, all seven gates, run
  three times on `96841296` with identical hashes):

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `d0da4ccc71d2067c` (unchanged) | **`7a0be4440cd154b7`** (was `e820d37865085bd9`) |
| Legion | **`7a403f097f3506b3`** (was `c3f6a3a13f0c167f`) | **`dc2652648568ad54`** (was `0783cb05f7f128cf`) |
| Retail AI (`TAK_AI_LEVEL=6`) | `4952493f51b9a568` (unchanged) | not run |
| Crusades, Retail pathfinding | not run | **`3e8c0ad3b4f1b859`** (was `f2f51dc1703fc209`) |
| Crusades, Legion | not run | **`b0c9748d62aafeb6`** (was `c4189ae189dd68dc`) |

**The four 300 s hashes changed, and exactly as `task-ai-targets` predicted
on its own**: the AI's strikes now pick from every enemy unit. The 60 s gates
end before any strike, and the Retail AI is unchanged. The other five
branches leave all seven gates unchanged on their own (on `afaacb9a`: Legion
60 s `c3f6a3a13f0c167f`, Legion 300 s `51989ae7896320d8`, Crusades Legion
300 s `0e2d8ac5fdbae8ca`). The walk-in-place repair then changes the three
Legion gates (the body's stand window is hashed state); the four Retail
gates keep their hashes.

- **Scenario baseline** (`tools/scenarios/baseline.json`): all 145 scenarios
  in both modes at the gate offsets; the values of the existing Legion
  `hash@` keys retaken, 797 in 76 scenarios at `afaacb9a` and 959 in 90
  (against 0.9.10) at `96841296` (no Retail hash moved), and one
  offset-spread exception (median of 5) added with its reason for Legion
  facing: `sideways` in `taperbridge-small`. The combine's five
  `walk_in_place` spread exceptions (`staticblock-own`, `staticblock-other`,
  `pocket-304-open`, `gap14`, `taperbridge-100`) were dropped with the
  repair: `walk_in_place` is 0 in every fixture at every offset. Six
  `mixed2` keys are licensed (Limits). `legion_check` passes (36,566 keys,
  0 failed); `validate` and `legion_baseline_reasons` report no problem.
- **Navigation checkpoints** (`tools/navigation_checkpoints.json`): the 24
  Legion lines retaken (again with the repair); Retail identical; serial
  equals workers, and a gcc -O2 build matches a clang Release build.
- **AI time to a winner** (all-AI games, debug builds, game minutes): the
  reported generated map with 8 Absurd, seed 1 no winner in 120 -> 82, seed 2
  80.8 -> 36.2; Ulasem Arena with four levels, no winner -> 34.8 and 54.7;
  Inner Circle Absurd against Absurd 25.6 -> 33.8 and 11.1 -> 25.8 (slower,
  noisy).
- **Legion facing** (31 scenario fixtures, offset 0): sideways steps 395,217 ->
  127,299, backward 86,133 -> 28,674 (Retail 277,784 / 9,722); arrivals, t90,
  done, spins, reversals and stop-go identical in all 31. With the repair
  every one of these is unchanged, and `walk_in_place` falls from 6 to 0.

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

All four tag workflows ran on `fbae4bbd` (branch/tag `v0.9.11`), started
2026-10-29 06:42 UTC, and passed on attempt 1, with no failure, rerun or
stall: [Linux packages
40830851171](https://github.com/pocketgeek/tak-engine/actions/runs/40830851171)
(18 jobs, all success, finished 07:17 UTC), [macOS
40830851168](https://github.com/pocketgeek/tak-engine/actions/runs/40830851168)
(4 jobs, finished 07:17 UTC) and [Windows
40830851169](https://github.com/pocketgeek/tak-engine/actions/runs/40830851169)
(4 jobs, finished 07:26 UTC). [Determinism
40830851579](https://github.com/pocketgeek/tak-engine/actions/runs/40830851579)
(dispatched on the tag) passed on attempt 1 in about 4 minutes: detmath,
sim-equiv and unit-tests all succeeded (`100% tests passed out of 8`). GCC and
Clang x86-64 at -O0/-O2/-O3 all agree on golden `dcef618cd2e4d558`
("DETERMINISM OK", unchanged from 0.9.10).

All 29 assets were downloaded fresh and match GitHub's SHA-256 digests and
sizes. The file set is identical to 0.9.10's: the same 29 asset names, and the
file lists of all 27 packages, installer, zips and debug archives match
0.9.10's exactly (after replacing the version string). Every asset grew, from
+16,060 bytes (+0.060%, openSUSE Leap 16.0 x86-64 RPM) to +330,650 bytes
(+0.267%, Windows x64 debug zip, the largest absolute growth); the largest
relative growth is +0.286% (Linux x64 debug archive, +303,681 bytes). Nothing
shrank. All 69 release `takclient`, `takserver` and `cartographer` binaries
(48 Linux ELF, 9 Windows, 12 macOS in the zips and apps) carry the `v0.9.11`
build string and no older `v0.9.x` string. None of the 48 Linux ELF binaries
has a libva, libdrm, libsecret, GLib or libsqlite `NEEDED` entry, and none
contains `__vaDriverInit_`, `SQLite format` or `sqlite3_`. Each binary's
`NEEDED` set is identical to its 0.9.10 counterpart's (libm, libc and the
dynamic loader; libdl and libpthread on the Slackware binaries only).
`strings` (ASCII and UTF-16) on all 69 release executables finds the `TAK_`
names only as expected, identical to 0.9.10 binary for binary: the 23
`takserver` binaries hold exactly their four timers (`TAK_TIMEOUT_MS`,
`TAK_LOAD_TIMEOUT_MS`, `TAK_STALL_MS`, `TAK_DROP_WAIT_MS`); the 23 `takclient`
binaries hold only `TAK_MARK_OUTSIDE`; the 23 `cartographer` binaries hold
only `TAK_CARTOGRAPHER_RECOVERY` and `TAK_EDITOR_RULE_NAMES` plus their split
pieces; none contains `AUTOPLAY`, `TAK_START_FLYERS`, `TAK_NET_JITTER_MS`,
`TAK_FOG_EYE`, `TAK_CARTOGRAPHER_PROFILE` or `TAK_HIDDEN_UNITS`. The licence
files (`OPUS-LICENSE.txt`, `CHAT-FONT-LICENSE.txt`, `EDITOR-FONT-LICENSE.txt`,
`STREAMING-LICENSES.txt`, `ACME-LICENSES.txt`) are in every package, zip,
installer and app (the Cartographer app carries `EDITOR-FONT-LICENSE.txt`
only, as before). Their checksums equal 0.9.10's, and all 27
`STREAMING-LICENSES.txt` copies carry the OpenSSL notice. No `mods/`,
`overrides/` or `click.hpi` is shipped. The packaged Linux `takclient --help`
(Fedora 44 and Slackware) lists only `--data`, `--play-map`, `--game-mod` and
`--version`, with text identical to 0.9.10's. The `takclient`, `takserver` and
`cartographer` of every x86-64 package report `0.9.11 (build v0.9.11)` (the
Arch `takclient` needs GLIBC 2.44 and cannot run on this Fedora host, as in
0.9.10; its `takserver` and `cartographer` run).

The Fedora 44 x86-64 RPM's `takserver` printed "listening ... (protocol v304)"
with retail gameplay hash `d6000f4fa9095a51`. It ran the Retail AI game on
Inner Circle with `--local --no-auth --seed 1` and a scratch HOME,
`XDG_DATA_HOME` and `XDG_CONFIG_HOME`. The client was the Debug build of
`fbae4bbd` (`build-dbg`, which reports `0.9.11 (build v0.9.11)`) with
`TAK_AI_LEVEL=6 TAK_SPEED=80 --mpai --time 60`. The game ended
`hash=4952493f51b9a568 units=4 err=none end=timelimit` (tick 1800), the same
as 0.9.10 and the local gate. The server's gameplay data hash accepted the
client, and no desync or referee-suspect line appeared. The same RPM's
`cartographer "Inner Circle" --bundle <file>` (headless, scratch HOME) loaded
the map (96 x 96 blocks, 4 start positions, 215 features, 1261 sections),
wrote a 379,088-byte `.kmp`, exited 0 and left nothing in the scratch HOME.
The packaged release `takclient --data <read-only link farm> --play-map <that
.kmp>` reached the single-player lobby (local server on a private port,
protocol v304, game created, map verified, 141 features placed, 4 mana
deposits). The retail data folder was a `chmod -R a-w` symlink copy of
`assets/game` (1,108 entries plus the root). Nothing was created or modified
in that copy or in the real `assets/game` (no entry changed in mtime or
ctime), and the map cache, mod cache and `settings.ini` landed under the
scratch `XDG_DATA_HOME/TAKengine/TAKingdoms`. The draft's live body equals the
prepared `body.md` apart from one trailing newline. Every `blob` `v0.9.11`
link in the release notes (4 distinct URLs: `docs/development.md`,
`docs/public-server.md#versions`, `docs/release-0.9.10-notes.md#known-limits`
and `docs/release-0.9.11-validation.md`) resolves to a file present at the tag
(HTTP 200), and both anchors exist.

## Signing

Signing. Windows: on oden-win, `Get-AuthenticodeSignature` reports **Valid**
(timestamped, "Curtis Edge") for all 7 signed files: the x64 installer and
`takclient`, `takserver` and `cartographer` in both the x64 and arm64 zips,
the same 7 as 0.9.10. The x64 `takclient.exe`, `takserver.exe` and
`cartographer.exe` report `0.9.11 (build v0.9.11)`, and the arm64
`takclient.exe` and `takserver.exe` carry the `v0.9.11` build string. `--mpai`
was not run on Windows or macOS, per the release rule (no build flags,
compilers or CI determinism coverage changed); the Windows and macOS tag CI
determinism tests cover them. The files were run from a scratch copy with a
scratch profile; the real profile listing on oden-win was identical before and
after (352 file entries), and the scratch folder was removed.

macOS: on the MacBook (no builds), both DMGs and all four apps (the game and
TAK Cartographer, from the DMGs and from the zips) pass `stapler validate`,
and `spctl -a -vv` accepts them as "Notarized Developer ID" (Curtis Edge,
L479Y59RN8). Both DMGs themselves are also stapled and accepted as Notarized
Developer ID. Each of the 8 app copies passes `codesign --verify --strict
--deep` and has `NSHighResolutionCapable` true; the game apps have
`NSMicrophoneUsageDescription`, and the `takclient` has the `audio-input`
entitlement (the other binaries do not). The bare zip programs (takclient,
takserver, cartographer) pass `codesign --strict` with the same team id;
`spctl` calls them "not an app", as expected. The macOS `takclient` and
`takserver` report `0.9.11 (build v0.9.11)` (arm64 and x64, apps, bare
programs and the debug zips' takclient) and contain the MP4 muxer strings. No
licence file is missing, and no `mods/`, `overrides/` or `click.hpi` is
present. `otool -L` of the 12 shipped arm64 and x64 binaries (bare programs
and app executables) shows no `libssl`, `libcrypto`, `libsqlite` or libopus
dylib. Both takclients contain `OpenSSL 3.5.8 25 Aug 2026` and no
SecureTransport TLS symbols, and Security.framework is still linked for the
system root anchors. The Mac's real `TAKengine` profile listing was identical
before and after (86 files), and the scratch folder was removed.

## Publication

Published on 2026-10-29 07:39:08 UTC as the Latest release, 29 assets (v0.9.11
-> `fbae4bbd`, tag object `184058da`); 0.9.10 and earlier releases are kept.

## Limits

- No game mixing 0.9.11 with 0.9.10 or older is possible: protocol 304 is
  refused by every older server and client. No mixed-version game was run
  with a real 0.9.10 binary.
- No Windows or macOS `--mpai` for 0.9.11; their tag CI determinism tests
  cover them.
- No desync sweep and no multiplayer or remote run had a hover attacker
  re-aiming or a factory's output dying.
- **The two scenario checks the review blocked on.** `slowturn`'s Legion
  `walk_in_place` (an exact-match safety key, 0..78 per offset on
  `afaacb9a`) is fixed in `96841296`: the stand in the scripts was judged by
  the course's turn, so a body facing its way crept at the facing cap while
  an unseen course turned round; it is now judged by the body (0 in every
  fixture at every gate offset, movement identical). The `mixed2` group F
  keys are not changed by the repair and are offset noise, now licensed with
  reasons: over 25 start offsets (0, +-1..+-12) the facing code leaves group
  F incomplete at 19 against main's 20, 1.0 bodies behind against 1.2,
  `contact_own_permille` 821 against 822, `work.trace_cells.p99` 1597
  against 1596 and `work.legion_total.p99` 2190 against 2185 (means); only
  `work.slides.max` is up, 6.8 against 6.4. Licensed at the gate's
  core-five medians: `g.F.done` never, `g.F.left_behind` 1,
  `contact_own_permille` 845, `work.slides.max` 8, `work.trace_cells.p99`
  1562, `work.legion_total.p99` 2199.
- The Legion facing, AI, factory and hover limits are in the
  [release notes](release-0.9.11-notes.md#known-limits).
- Tag CI: all four tag workflows (Linux packages, Windows, macOS, Determinism)
  passed on attempt 1, with no failure, rerun or stall.
- All of 0.9.10's limits carry over (see
  [release-0.9.10-validation.md](release-0.9.10-validation.md#limits)).
