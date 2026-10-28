# 0.9.9 release validation

Protocol **303** (was 302 in 0.9.8), replay format **15** (unchanged),
generator version **9** (unchanged).

Tagged `v0.9.9` at `c990bbfb` (full `c990bbfb5375d994af6dc9361bb0d578a2701e79`, annotated tag object `f3931f49`) and published on 2026-10-28 04:39:52 UTC.

## Scope and compatibility

0.9.9 follows 0.9.8 (protocol 302; tag `v0.9.8` -> `2a26b2b4`). Main after
0.9.8's validation commit `b047a049` has the following, in order of landing;
the code tested below is `fd124c2f` (the merged branches, full suite) and
`233073b0` (the two AI changes on top):

- `a6c32139` HUD: the TRAINING line gives way to a build icon's tooltip.
- `6755a667`, `a6f52c20` Sim: own idle and guarding units step off a build
  site, and such a site can be placed; exit points skip taken spots and the
  site test counts airborne flyers (`kNetVersion` 302 -> 303), merged from
  `task-buildsite-clear` as `432a0a04`.
- `b71bff0b`, `8ad3987e` Placement: refuse a site the selected walking builder
  can never reach (`World::buildSiteReachable`, client only), merged from
  `task-naval-reach` as `bd668e6d`.
- `ee0915b1`, `25a66700` Sim: a ground guard keeps to Follow_Ground's ring
  round its ward and walks into it (protocol 303), merged from `task-doorway`
  as `e089dac2`.
- `88709e8f` Options: Max FPS 30-240 and Unlimited.
- `5876347b`, `b53a5d92` Menu: Settings screens return to SETTINGS.
- `2040b8ab` Hotkeys: MAKE POSTER defaults to Shift+P.
- `bcf36319`, `ab4ac1ce` Benchmark: no mod packs of any kind (client and
  server), merged from `task-bench-nomods` as `4cd49f03`.
- `252f432e` CHANGELOG: protocol line under the Unreleased heading.
- `fd124c2f` pathblock test: a guard that stepped off a site is back on its
  guard ring.
- `c17099bb` Retail AI: no in-place lodestone upgrade.
- `3669eb2b`, `233073b0` AI: divine lodestone upgrades only where they pay,
  one at a time, never holding a reserve that cannot be spent.

`git diff --stat b047a049 233073b0`: 43 files changed, 2236 insertions(+),
284 deletions(-); `-- src`: 20 files changed, 750 insertions(+), 66
deletions(-) (`src/sim` 3 files, `src/client` 11, `src/ai` 4, `src/net` 1,
`src/server` 1; the rest is tests, test fixtures and baselines,
`CMakeLists.txt`, `CHANGELOG.md` and the documentation). 22 commits (18
without the merges).

Final `git diff --stat v0.9.8 v0.9.9`: 53 files changed, 2769 insertions(+), 322 deletions(-); `-- src`: 20 files changed, 750 insertions(+), 66 deletions(-) (`src/sim` 3 files, `src/client` 11, `src/ai` 4, `src/net` 1, `src/server` 1); outside `src`: 33 files changed, 2019 insertions(+), 256 deletions(-) (tests, test fixtures and baselines, `CMakeLists.txt`, `CHANGELOG.md` and the documentation). 26 commits from `v0.9.8` to `v0.9.9` (22 without the merges), including the preparation and validation commits.

The preparation commit on top changes no code: `CMakeLists.txt` (the version
line), `CHANGELOG.md`, `README.md`, the version and protocol lines in `docs/`,
the notes, this report, `docs/img/README.md` and `docs/img/title.jpg`.

**Compatibility.** `kNetVersion` is **303** in `src/net/protocol.h` (302 at
`v0.9.8`). `kReplayFormat` is **15** in `src/net/replayhdr.h`, unchanged. The
map generator's `formatVer` **9** is unchanged. The gameplay data hash of the
test install is `d6000f4fa9095a51`.

- **Multiplayer.** `Server::handshake` rejects a Hello whose protocol differs
  from `kNetVersion`; the release string is never compared. So **0.9.9 plays
  with no 0.9.8 or older client or server**.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  format is 1..15 and its protocol equals `kNetVersion`, so 0.9.9 refuses every
  0.9.8 (protocol 302) replay. `tools/replay_test.cpp` pins 303.
- **Hashed state.** No field is added. Units stepping off a build site and the
  guard ring change the game only where a build order meets the player's own
  idle or guarding units on its site, or a ground unit guards; the divine
  lodestone rules only where one of our AI levels would have picked a divine
  lodestone or a priest would have started troops (an upgrade itself comes at
  6 minutes at the earliest), and the Retail AI change only where it would have
  upgraded in place. No gate game does, so **the gate hashes did not change**
  (below).
  The placement-reach check is client-only and not hashed.
- **Maps, mods and settings.** No map, recipe, package or settings format
  changed; a saved `maxFps` above 240 (or 0) now loads as Unlimited.

## Local preparation checks

All ran on Linux with optimized Debug builds (`build-o2`, `-O2 -g`, ccache +
mold, `TAK_TEST_DATA` set so the data-backed tests ran) and a Release build.
Every takclient and takserver run used scratch `HOME`, `XDG_DATA_HOME` and
`XDG_CONFIG_HOME`.

- **Builds.** At `233073b0` the binaries print `0.9.8 (build ...)`, because
  the version number changes in the preparation commit; the prepared tree's
  own `takclient --version` and `takserver --version` print
  `0.9.9 (build v0.9.8-23-g233073b0d878-dirty)` from a Release build (the tree
  was dirty only with the preparation edits), and the title picture's Debug
  `-O2` build reads VERSION 0.9.9.
- **New tests.** `doorway` (232 cases), `naval_reach` / `legion_naval_reach`,
  the `pathblock_test` site-clear cases, `benchmark_mods` and the Benchmark
  step of `mod_transfer`, `settings_test` Max FPS, `poster_test` Shift+P,
  `classic_ai_test` lodestone sites and the `ai_behavior_test` divine upgrade
  cases (13 checks) all fail on the old code and pass on the new.
- **Full suite** (`ctest`, `build-o2`, 781 tests) on the merged branches:
  **780 of 781** passed; the one failure, `pathblock` (its expectation for a
  guard that stepped off a site, which the guard ring changed), was fixed by
  the test commit `fd124c2f` (the guard is back on its guard ring).
- **Full suite on `233073b0`** (the final code; -O2 debug, `ctest -j12`, scratch
  HOME and XDG folders): 780 of 781 passed. The one failure is
  `vashim_unavailable`, the known local failure (a render node without libva);
  CI builds FFmpeg with the shim and runs it there.
- **Detmath guard** (`tools/check-detmath.sh`, on the prepared tree): OK.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): golden
  `dcef618cd2e4d558`, expected unchanged from 0.9.8 (the gate hashes did not
  change); the tag CI Determinism workflow checks it.
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`,
  `TAK_LEGION=1` for Legion, `TAK_AI_LEVEL=6` for the Retail AI, `--crusades`
  for the Crusades games, `TAK_SPEED=80`, `build-o2`). The gates ran on each
  branch and on the merged code at `fd124c2f`, and on the AI changes.

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `d0da4ccc71d2067c` | `e820d37865085bd9` |
| Legion | `c3f6a3a13f0c167f` | `0783cb05f7f128cf` |
| Retail AI (`TAK_AI_LEVEL=6`) | `4952493f51b9a568` | not run |
| Crusades, Retail pathfinding | not run | `f2f51dc1703fc209` |
| Crusades, Legion | not run | `c4189ae189dd68dc` |

**The gate hashes are the 0.9.6 to 0.9.8 values.** The AIs never give Guard
orders, so no gate game has a guard ring; no gate game builds on a site its
own idle units stand on; the gate games end before our levels' earliest
divine upgrade (6 minutes), no AI in them picked a divine lodestone or had a
priest start troops, and the Retail AI never chose an in-place upgrade in them.
The cross-compiler golden is unchanged for the same reason.

- **Real-data AI games** (all-AI, debug build): Inner Circle Normal, 1200 s,
  upgraded a lodestone at 13.8 minutes (finished 17.5); Absurdly Defensive at
  14.0 (17.0); Ulasem Arena Hard made four upgrades between 11.8 and 22.3
  minutes. None on a free deposit, and no dragon started with an empty store.
- **Doorway crowding** (`attackring-guard-64`, Retail): walking in place 1060
  at 0.9.8, 867 now.

## Screenshots

One picture was retaken, by the prepared tree's build (Debug `-O2`, CMake
`VERSION 0.9.9`): `docs/img/title.jpg`, with the 0.9.1 title capture
(`takclient menu --shot`, offscreen driver, 1920 x 1080 window cropped to the
1440 x 1080 artwork). It reads VERSION 0.9.9 and shows the three doors (looked
at). Nothing else in 0.9.9 changes a picture, so none was retaken or deleted.
`docs/img/README.md` lists the details.

## Desync sweeps

No local or remote desync sweep (`tools/desync-hunt.sh`,
`tools/desync-hunt-remote.sh`) was run for 0.9.9, and no two-client game, so
no multiplayer run had units stepping off a build site or a guard on its ring;
both are covered by `pathblock_test` and `doorway_test` and the local checks.

None is planned before the tag: by the project's testing rule a release runs
no remote sweep, and the tag CI determinism tests are the cross-platform check.

## CI and packages

All four tag workflows ran on `c990bbfb` and passed on attempt 1, with no failure, rerun or stall: [Linux packages 40629787300](https://github.com/pocketgeek/tak-engine/actions/runs/40629787300) (18 jobs, all success, finished 04:32 UTC), [Windows 40629787375](https://github.com/pocketgeek/tak-engine/actions/runs/40629787375) (x64, arm64, sign and publish, cache prune; finished 04:33 UTC) and [macOS 40629787162](https://github.com/pocketgeek/tak-engine/actions/runs/40629787162) (arm64 and x64 build, then sign and notarize for each; finished 04:19 UTC), all started 03:59 UTC. [Determinism 40629787034](https://github.com/pocketgeek/tak-engine/actions/runs/40629787034) (dispatched on the tag) passed on attempt 1 in about 4 minutes: detmath, sim-equiv and unit-tests all succeeded (`100% tests passed out of 8`). GCC and Clang x86-64 at -O0/-O2/-O3 all agree on golden `dcef618cd2e4d558` ("DETERMINISM OK", unchanged from 0.9.8); the driver-equivalence test printed OK. The duplicate runs of the same commit on `main` were cancelled; the tag runs are the check.

All 29 assets were downloaded fresh and match GitHub's SHA-256 digests and sizes. The file set is identical to 0.9.8's: the per-asset file lists of the 27 packages, installer, zips and debug archives match 0.9.8's exactly (after replacing the version string), and the asset names are the same 29. Sizes grew slightly, from +0.043% (Slackware package, +4,012 bytes) to +0.2% (macOS x64 debug zip, +52,371 bytes); no asset shrank; the largest absolute growth is the Windows x64 debug zip at +156,597 bytes. Every one of the 54 Linux ELF binaries carries the `v0.9.9` build string and no older `v0.9.x` string, and so do all 69 release `takclient`, `takserver` and `cartographer` binaries (Linux, Windows, macOS, apps and bare programs). None of the 54 Linux ELF binaries has a libva, libdrm, libsecret, GLib or libsqlite `NEEDED` entry, and none contains `__vaDriverInit_`, `SQLite format` or `sqlite3_`; the `NEEDED` set of every one of the 54 is identical to 0.9.8's. `strings` (ASCII and UTF-16) on all 69 release executables finds the `TAK_` names only as expected: `takserver` holds exactly its four timers (`TAK_TIMEOUT_MS`, `TAK_LOAD_TIMEOUT_MS`, `TAK_STALL_MS`, `TAK_DROP_WAIT_MS`); `takclient` holds only `TAK_MARK_OUTSIDE`; `cartographer` holds only `TAK_CARTOGRAPHER_RECOVERY` and `TAK_EDITOR_RULE_NAMES` plus their split pieces; none contains `AUTOPLAY`, `TAK_START_FLYERS`, `TAK_NET_JITTER_MS`, `TAK_FOG_EYE` or `TAK_CARTOGRAPHER_PROFILE`. The licence files (`OPUS-LICENSE.txt`, `CHAT-FONT-LICENSE.txt`, `EDITOR-FONT-LICENSE.txt`, `STREAMING-LICENSES.txt`, `ACME-LICENSES.txt`) are in every package, zip, installer, DMG and app (the Cartographer app carries `EDITOR-FONT-LICENSE.txt` only, as before); the licence file lists equal 0.9.8's. All 27 `STREAMING-LICENSES.txt` copies are byte-identical to 0.9.8's (the same two checksums, 22 and 5 copies) and all 27 carry the OpenSSL notice. No `mods/`, `overrides/` or `click.hpi` is shipped. The packaged Linux `takclient --help` lists only `--data`, `--play-map`, `--game-mod` and `--version` (identical output from the Debian 12, Ubuntu 26.04, openSUSE and Slackware packages), and the Fedora 44 x86-64 RPM's `takclient`, `takserver` and `cartographer` all report `0.9.9 (build v0.9.9)`; so do the `takclient` and `takserver` of every other x86-64 package (the Arch package's `takclient` needs GLIBC 2.44 and cannot run on this Fedora host, as in 0.9.8). That RPM's `takserver` printed "listening ... (protocol v303)" with retail gameplay hash `d6000f4fa9095a51`. It ran the Retail AI game on Inner Circle with `--local --no-auth --seed 1` and a scratch HOME, `XDG_DATA_HOME` and `XDG_CONFIG_HOME`; the client was the Debug build at `c990bbfb` (`build-dbg`, rebuilt with all targets for this check) with `TAK_AI_LEVEL=6 TAK_SPEED=80 --mpai --time 60`, and the game ended `hash=4952493f51b9a568 units=4 err=none end=timelimit` (tick 1800), the same as 0.9.8 and the local gate; no desync or referee-suspect line appeared. The same RPM's `cartographer "Inner Circle" --bundle <file>` (headless, scratch HOME) loaded the map (96 x 96 blocks, 4 start positions, 215 features, 1261 sections), wrote a 379,088-byte `.kmp`, exited 0 and left nothing in the scratch HOME, as in 0.9.8. The packaged release `takclient --data <read-only link farm> --play-map <that .kmp>` reached the single-player lobby (local server on a private port, protocol v303, game created, map verified, 141 features placed, 4 mana deposits) with the retail data folder as a `chmod a-w` copy that symlinks every file (1,108 entries, the same count as the real `assets/game`). Nothing was created or modified in that copy or in the real `assets/game` (no file there changed in mtime or ctime), and the map cache, mod cache and `settings.ini` landed under the scratch `XDG_DATA_HOME/TAKengine/TAKingdoms`. Every `blob` `v0.9.9` link in the release notes (4 distinct URLs, 5 occurrences, 2 with anchors: `public-server.md#versions` and `release-0.9.8-notes.md#known-limits`) resolves to a file and heading present at the tag.

## Signing

Signing. Windows: on oden-win, `Get-AuthenticodeSignature` reports **Valid** (timestamped, "Curtis Edge") for all 7 signed files: the x64 installer and `takclient`, `takserver` and `cartographer` in both the x64 and arm64 zips, the same 7 as 0.9.8. The x64 `takclient.exe` and `takserver.exe` report `0.9.9 (build v0.9.9)`, and the arm64 ones carry the `v0.9.9` build string. `--mpai` was not run on Windows or macOS, per the release rule (no build flags, compilers or CI determinism coverage changed); the Windows and macOS tag CI determinism tests cover them.

macOS: on the MacBook (no builds), both DMGs and all four apps (the game and TAK Cartographer, from the DMGs and from the zips) pass `stapler validate`, and `spctl -a -vv` accepts them as "Notarized Developer ID" (Curtis Edge, L479Y59RN8). Both DMGs themselves are also stapled and accepted as Notarized Developer ID. Each app passes `codesign --verify --strict --deep` and has `NSHighResolutionCapable` true; the game apps have `NSMicrophoneUsageDescription`, and the takclient has the `audio-input` entitlement (the other binaries do not). The bare zip programs (takclient, takserver, cartographer) pass `codesign --strict` with the same team id; `spctl` calls them "not an app", as expected. The macOS `takclient` and `takserver` report `0.9.9 (build v0.9.9)` (arm64 and x64, apps, bare programs and the debug zips' takclient) and contain the MP4 muxer strings. No licence file is missing, and no `mods/`, `overrides/` or `click.hpi` is present. `otool -L` of the shipped arm64 and x64 `takclient`, `takserver` and `cartographer` (12 binaries) shows no `libssl`, `libcrypto`, `libsqlite` or libopus dylib. Both takclients contain `OpenSSL 3.5.8 25 Aug 2026` and no SecureTransport TLS symbols, and Security.framework is still linked for the system root anchors.

## Publication

Published with `gh release edit v0.9.9 --draft=false --latest` on 2026-10-28 04:39:52 UTC with 29 assets; it is the Latest release. 0.9.0 to 0.9.8 stay on the releases page.

## Limits

- No game mixing 0.9.9 with 0.9.8 or older is possible: protocol 303 is refused
  by every older server and client. No mixed-version game was run with a real
  0.9.8 binary.
- No Windows or macOS `--mpai` for 0.9.9; their tag CI determinism tests cover
  them.
- No desync sweep and no multiplayer or remote run had units stepping off a
  build site or a guard on its ring; the AI never guards. The placement-reach,
  guard-ring, site-clearing, Benchmark and lodestone limits are in the
  [release notes](release-0.9.9-notes.md#known-limits).
- Tag CI: no failures, reruns or stalls. Linux packages, Windows, macOS and Determinism all passed on attempt 1 (Determinism in about 4 minutes). The duplicate `main` runs of the tagged commit were cancelled.
- All of 0.9.8's limits carry over (see
  [release-0.9.8-validation.md](release-0.9.8-validation.md#limits)).
