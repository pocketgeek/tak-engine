# 0.9.10 release validation

Protocol **303** (unchanged from 0.9.9), replay format **15** (unchanged),
generator version **9** (unchanged).

Tagged `v0.9.10` at `99ccf7e5` (full `99ccf7e550d9b1759d2e1d68b84835d735291eaf`, annotated tag object `dc0af088`) and published on 2026-10-29 01:17:19 UTC.

## Scope and compatibility

0.9.10 follows 0.9.9 (protocol 303; tag `v0.9.9` -> `c990bbfb`). Main after
0.9.9's validation commit `f2a219e3` has the following, in order of landing;
the code tested below is `4832e58d`:

- `c2f5a24b` Draw flat features (TDF height below 10) under every unit, as
  retail; the `-24` mana-deposit sort bias removed.
- `c2e6cd8d`, `7494602c`, `9b376f27` Options > Graphics: HIDDEN UNITS, its
  policy, settings and renderer tests, and its documentation.
- `eadc0b78` to `8df5569d` (with the merges `c9f19757` and `8a480ded` of
  `task-hidden-units`) and `b381e197`: HIDDEN UNITS display modes developed
  and measured on the branch, with their tests and cost notes; `f2a81d19`
  removed them again before release, and no released setting uses them.
- `94814f4b` macOS: Auto renderer tries OpenGL first, then Metal.
- `4ba5725b` Metal: skip draws whose clip rectangle covers no pixel
  (`src/client/cliprect.h`); `cb9404d9` a harness skip for machines without
  OpenGL.
- `bb163613` Docs: retail does not z-buffer between sprites; the world draw
  order. `9645c6fb` Docs: debug CLI single-player opens its room;
  `TAK_SP_AIS` starts it. `5acb7861` Options picture retaken. Merged as
  `2f4e91bb` (`task-hidden-docs`) and `03976633` (`task-win-lobby`).
- `64bff627` HIDDEN UNITS: FADE, a tree or rock over a unit fades out.
- `a37dfbc3` FADE fades buildings too, FADE OPACITY slider, FADE the default.
- `f2a81d19` OFF, DOTS and FADE only.
- `2ac340d3` Docs: HIDDEN UNITS is Off, Dots and Fade; Options picture
  retaken.
- `4832e58d` HIDDEN UNITS: unused helpers dropped.

`git diff --stat f2a219e3 4832e58d`: 27 files changed, 1731 insertions(+),
157 deletions(-); `-- src`: 16 files changed, 1255 insertions(+), 114
deletions(-), **all in `src/client/`** (the rest is tests, `CMakeLists.txt`,
`tools/render_benchmark.py`, `CHANGELOG.md`, the documentation and the Options
picture). `git diff v0.9.9 4832e58d -- src/sim src/net src/ai src/server` is
empty. 38 commits after `f2a219e3` (34 without the merges).

Final `git diff --stat v0.9.9 v0.9.10`: 36 files changed, 2166 insertions(+), 198 deletions(-); `-- src`: 16 files changed, 1255 insertions(+), 114 deletions(-), all in `src/client/` (none in `src/sim`, `src/net`, `src/ai` or `src/server`); outside `src`: 20 files changed, 911 insertions(+), 84 deletions(-) (tests, `CMakeLists.txt`, `tools/render_benchmark.py`, `CHANGELOG.md`, the documentation and the pictures). 41 commits from `v0.9.9` to `v0.9.10` (37 without the merges), including the preparation and review commits; 40 of them come after `f2a219e3`.

The preparation commit `8ae1e372` and the review commit `99ccf7e5` on top of it (the tag points at `99ccf7e5`) change no code: `git diff 4832e58d v0.9.10 -- src` is empty. They touch `CMakeLists.txt` (the version line), `CHANGELOG.md`, `README.md`, the version and protocol lines in `docs/`, the notes, this report, `docs/img/README.md` and `docs/img/title.jpg`; the review commit reworded the CHANGELOG and notes and added the suite and gate results.

**Compatibility.** `kNetVersion` is **303** in `src/net/protocol.h`, as at
`v0.9.9`. `kReplayFormat` is **15** in `src/net/replayhdr.h`, unchanged. The
map generator's `formatVer` **9** is unchanged. The gameplay data hash of the
test install is `d6000f4fa9095a51`, as for 0.9.9.

- **Multiplayer.** `Server::handshake` rejects a Hello whose protocol differs
  from `kNetVersion`; the release string is never compared, and the server
  code is unchanged. So **0.9.10 plays with 0.9.9 clients and servers in either
  direction** (checked in source; no mixed-version game was run), and with no
  0.9.8 or older one.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  format is 1..15 and its protocol equals `kNetVersion`; both are unchanged,
  so 0.9.10 plays 0.9.9 replays and 0.9.9 plays 0.9.10's. A 0.9.8 (protocol
  302) replay is refused, as in 0.9.9.
- **Hashed state.** No simulation, network, server or AI code changed. Hidden
  Units, the flat-feature draw order, the macOS renderer choice and the Metal
  clip fix are display only and not hashed, so **the gate hashes did not
  change** (below).
- **Maps, mods and settings.** No map, recipe or package format changed.
  Settings gain `hiddenUnits` (`off`, `dots`, `fade`; absent or unknown reads
  as `fade`) and `hiddenFadeOpacity` (0..100, absent = 65); 0.9.9 skips both.

## Local preparation checks

All ran on Linux with optimized Debug builds (`-O2 -g`, ccache + mold,
`TAK_TEST_DATA` set so the data-backed tests ran) and a Release build. Every
takclient and takserver run used scratch `HOME`, `XDG_DATA_HOME` and
`XDG_CONFIG_HOME`.

- **Builds.** At `4832e58d` the binaries print `0.9.9 (build ...)`, because
  the version number changes in the preparation commit; the prepared tree's
  own `takclient --version` and `takserver --version` print
  `0.9.10 (build v0.9.9-39-g4832e58df30e-dirty)` from a Release build (the
  tree was dirty only with the preparation edits), and the title picture's
  Debug `-O2` build reads VERSION 0.9.10.
- **New tests.** `hidden_units_policy` (`tools/hidden_units_test.cpp`), the
  data-backed `hidden_units` harness (Ulasem Arena, `--testbuild`, 1280 x
  960: flat features under every unit, Dots' cover rules and click target,
  Fade's covers and that 100% equals Off), `renderdriver_test` (macOS Auto
  order, Apple's software renderer refused, Apple A18 Pro kept) and
  `settings_test` (Hidden Units and Fade Opacity: default, clamp, round trip,
  reset).
- **Full suite on `4832e58d`** (the final code; Debug `-O2`, `ctest -j12`,
  scratch `HOME` and XDG folders, test data `~/tak_data`): 776 of 779 passed,
  one skipped (`vashim_system_loader`, which skips itself). Two failed.
  `vashim_unavailable` is the known local failure (a render node without
  libva); CI builds FFmpeg with the shim and runs it there.
  `disconnect_client` failed because of the test data: `~/tak_data` has an
  always-enabled game mod, and a replay recorded with it cannot be checked on
  a fresh profile that lacks the cached mod package. The 0.9.9 suite ran on
  `assets/game`, which has no mods; there the test passes all four cases on
  `4832e58d`. The code involved is unchanged since 0.9.9, so this is a
  follow-up, not a 0.9.10 regression.
- **Detmath guard** (`tools/check-detmath.sh`, on the prepared tree): OK (no
  file under `src/sim` changed since 0.9.9).
- **Cross-compiler golden hash** (`tools/check-determinism.sh`, the guard
  OK): golden `dcef618cd2e4d558` from g++ and clang++ at -O0, -O2 and -O3 (6
  of 6 agree), unchanged from 0.9.9; the aarch64 leg was skipped (no
  toolchain here). The tag CI Determinism workflow checks it too.
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`,
  `TAK_LEGION=1` for Legion, `TAK_AI_LEVEL=6` for the Retail AI, `--crusades`
  for the Crusades games, `TAK_SPEED=80`, Debug `-O2` build of `4832e58d`,
  all seven gates): every hash is the expected one. The Retail AI 60 s gate
  had also run repeatedly on the hidden-units branch with the same hash.

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `d0da4ccc71d2067c` | `e820d37865085bd9` |
| Legion | `c3f6a3a13f0c167f` | `0783cb05f7f128cf` |
| Retail AI (`TAK_AI_LEVEL=6`) | `4952493f51b9a568` | not run |
| Crusades, Retail pathfinding | not run | `f2f51dc1703fc209` |
| Crusades, Legion | not run | `c4189ae189dd68dc` |

**The gate hashes are the 0.9.6 to 0.9.9 values**: nothing the hash folds in
changed.

- **Hidden Units cost** (render benchmark, 1280 x 960, single runs): Fade
  about +0.07 ms a frame in a forest and +0.57 ms with a 1,800-unit army in
  view; Off at 10,000 units equal to main within noise.
- **Other platforms** (during development, before the final mode set): Windows
  on `oden-win` with OpenGL, Direct3D 11 and Direct3D 9; macOS on the MacBook
  Neo with OpenGL and Metal. On 2026-10-28 the Neo's Auto chose OpenGL
  ("Apple A18 Pro").

## Screenshots

`docs/img/guide/options-graphics.jpg` was retaken on main before this commit
for the Hidden Units and Fade Opacity rows (`docs/img/README.md`). In the
preparation, `docs/img/title.jpg` was retaken by the prepared tree's build
(Debug `-O2`, CMake `VERSION 0.9.10`) with the 0.9.1 title capture
(`takclient menu --shot`, offscreen driver, 1920 x 1080 window cropped to the
1440 x 1080 artwork). It reads VERSION 0.9.10 and shows the three doors
(looked at). Pictures taken before 0.9.10 that show a unit behind a tree or a
building show it as **Off** does; they were not retaken for Hidden Units, and
none was deleted. `docs/img/README.md` lists the details.

## Desync sweeps

No local or remote desync sweep (`tools/desync-hunt.sh`,
`tools/desync-hunt-remote.sh`) was run for 0.9.10, and no two-client game: no
simulation, network or server code changed, and Hidden Units is local display.

None is planned before the tag: by the project's testing rule a release runs
no remote sweep, and the tag CI determinism tests are the cross-platform check.

## CI and packages

All four tag workflows ran on `99ccf7e5` and passed on attempt 1, with no failure, rerun or stall, all started 2026-10-29 00:42 UTC: [Linux packages 40795810158](https://github.com/pocketgeek/tak-engine/actions/runs/40795810158) (18 jobs, all success, finished 01:07 UTC), [Windows 40795810270](https://github.com/pocketgeek/tak-engine/actions/runs/40795810270) (4 jobs; finished 01:09 UTC) and [macOS 40795810175](https://github.com/pocketgeek/tak-engine/actions/runs/40795810175) (4 jobs; finished 01:07 UTC). [Determinism 40795810119](https://github.com/pocketgeek/tak-engine/actions/runs/40795810119) (dispatched on the tag) passed on attempt 1 in about 4 minutes: detmath, sim-equiv and unit-tests all succeeded (`100% tests passed out of 8`). GCC and Clang x86-64 at -O0/-O2/-O3 all agree on golden `dcef618cd2e4d558` ("DETERMINISM OK", unchanged from 0.9.9).

All 29 assets were downloaded fresh and match GitHub's SHA-256 digests and sizes. The file set is identical to 0.9.9's: the per-asset file lists of the 27 packages, installer, zips and debug archives match 0.9.9's exactly (after replacing the version string), and the asset names are the same 29. Sizes grew, from +0.028% (openSUSE Leap 16.0 x86-64 RPM, +7,568 bytes) to +1.70% (Windows x64 debug zip, +2,069,539 bytes, also the largest absolute growth); the Linux debug archives grew about 1.5% (+1.6 MB). No asset shrank. Every one of the 54 Linux ELF binaries carries the `v0.9.10` build string and no older `v0.9.x` string, and so do all 69 release `takclient`, `takserver` and `cartographer` binaries (Linux, Windows, macOS, apps and bare programs). None of the 54 Linux ELF binaries has a libva, libdrm, libsecret, GLib or libsqlite `NEEDED` entry, and none contains `__vaDriverInit_`, `SQLite format` or `sqlite3_`; their `NEEDED` set is libm, libc and the dynamic loader only. `strings` (ASCII and UTF-16) on all 69 release executables finds the `TAK_` names only as expected: the 23 `takserver` binaries hold exactly their four timers (`TAK_TIMEOUT_MS`, `TAK_LOAD_TIMEOUT_MS`, `TAK_STALL_MS`, `TAK_DROP_WAIT_MS`); the 23 `takclient` binaries hold only `TAK_MARK_OUTSIDE`; the 23 `cartographer` binaries hold only `TAK_CARTOGRAPHER_RECOVERY` and `TAK_EDITOR_RULE_NAMES` plus their split pieces; none contains `AUTOPLAY`, `TAK_START_FLYERS`, `TAK_NET_JITTER_MS`, `TAK_FOG_EYE`, `TAK_CARTOGRAPHER_PROFILE` or `TAK_HIDDEN_UNITS`. The licence files (`OPUS-LICENSE.txt`, `CHAT-FONT-LICENSE.txt`, `EDITOR-FONT-LICENSE.txt`, `STREAMING-LICENSES.txt`, `ACME-LICENSES.txt`) are in every package, zip, installer, DMG and app (the Cartographer app carries `EDITOR-FONT-LICENSE.txt` only, as before); the licence file lists equal 0.9.9's. All 27 `STREAMING-LICENSES.txt` copies are byte-identical to 0.9.9's (the same two checksums, 22 and 5 copies) and all 27 carry the OpenSSL notice. No `mods/`, `overrides/` or `click.hpi` is shipped. The packaged Linux `takclient --help` lists only `--data`, `--play-map`, `--game-mod` and `--version` (the usage text is identical in the Fedora 44 and Slackware packages), and the Fedora 44 x86-64 RPM's `takclient`, `takserver` and `cartographer` all report `0.9.10 (build v0.9.10)`; so do the `takclient` and `takserver` of every other x86-64 package (the Arch package's `takclient` needs GLIBC 2.44 and cannot run on this Fedora host, as in 0.9.9; its `takserver` runs). That RPM's `takserver` printed "listening ... (protocol v303)" with retail gameplay hash `d6000f4fa9095a51`. It ran the Retail AI game on Inner Circle with `--local --no-auth --seed 1` and a scratch HOME, `XDG_DATA_HOME` and `XDG_CONFIG_HOME`; the client was the Debug build of `4832e58d` (`build-dbg`, whose code equals `v0.9.10`'s apart from the version string) with `TAK_AI_LEVEL=6 TAK_SPEED=80 --mpai --time 60`, and the game ended `hash=4952493f51b9a568 units=4 err=none end=timelimit` (tick 1800), the same as 0.9.9 and the local gate; the server's gameplay data hash accepted the client, and no desync or referee-suspect line appeared. The same RPM's `cartographer "Inner Circle" --bundle <file>` (headless, scratch HOME) loaded the map (96 x 96 blocks, 4 start positions, 215 features, 1261 sections), wrote a 379,088-byte `.kmp`, exited 0 and left nothing in the scratch HOME, as in 0.9.9. The packaged release `takclient --data <read-only link farm> --play-map <that .kmp>` reached the single-player lobby (local server on a private port, protocol v303, game created, map verified, 141 features placed, 4 mana deposits) with the retail data folder as a `chmod a-w` copy that symlinks every file (1,108 entries, the same count as the real `assets/game`). Nothing was created or modified in that copy or in the real `assets/game` (no file there changed in mtime or ctime), and the map cache, mod cache and `settings.ini` landed under the scratch `XDG_DATA_HOME/TAKengine/TAKingdoms`. Every `blob` `v0.9.10` link in the release notes (5 distinct URLs, 3 of them with anchors: `public-server.md#versions`, `release-0.9.9-notes.md#known-limits` and `user-guide.md#renderer`) resolves to a file and heading present at the tag.

## Signing

Signing. Windows: on oden-win, `Get-AuthenticodeSignature` reports **Valid** (timestamped, "Curtis Edge") for all 7 signed files: the x64 installer and `takclient`, `takserver` and `cartographer` in both the x64 and arm64 zips, the same 7 as 0.9.9. The x64 `takclient.exe` and `takserver.exe` report `0.9.10 (build v0.9.10)`, and the arm64 ones carry the `v0.9.10` build string. `--mpai` was not run on Windows or macOS, per the release rule (no build flags, compilers or CI determinism coverage changed); the Windows and macOS tag CI determinism tests cover them. The files were run from a scratch copy with a scratch profile; the real profile listing on oden-win was identical before and after (346 file entries), and the scratch folder was removed.

macOS: on the MacBook (no builds), both DMGs and all four apps (the game and TAK Cartographer, from the DMGs and from the zips) pass `stapler validate`, and `spctl -a -vv` accepts them as "Notarized Developer ID" (Curtis Edge, L479Y59RN8). Both DMGs themselves are also stapled and accepted as Notarized Developer ID. Each of the 8 app copies passes `codesign --verify --strict --deep` and has `NSHighResolutionCapable` true; the game apps have `NSMicrophoneUsageDescription`, and the `takclient` has the `audio-input` entitlement (the other binaries do not). The bare zip programs (takclient, takserver, cartographer) pass `codesign --strict` with the same team id; `spctl` calls them "not an app", as expected. The macOS `takclient` and `takserver` report `0.9.10 (build v0.9.10)` (arm64 and x64, apps, bare programs and the debug zips' takclient) and contain the MP4 muxer strings. No licence file is missing, and no `mods/`, `overrides/` or `click.hpi` is present. `otool -L` of the 12 shipped arm64 and x64 binaries (bare programs and app executables) shows no `libssl`, `libcrypto`, `libsqlite` or libopus dylib. Both takclients contain `OpenSSL 3.5.8 25 Aug 2026` and no SecureTransport TLS symbols, and Security.framework is still linked for the system root anchors. The Mac's real `TAKengine` profile listing was identical before and after (81 files), and the scratch folder was removed.

## Publication

Published with `gh release edit v0.9.10 --draft=false --latest` on 2026-10-29 01:17:19 UTC with 29 assets; it is the Latest release. 0.9.0 to 0.9.9 stay on the releases page.

## Limits

- No game mixing 0.9.10 with 0.9.9 was run; that they play together is
  checked in source (same protocol, replay format and server code).
- No Windows or macOS `--mpai` for 0.9.10; their tag CI determinism tests
  cover them. The final Off / Dots / Fade build was not rerun on Windows or
  macOS after the other modes were removed.
- Retina sharpness with OpenGL on a Mac's built-in panel has not been looked at
  by a person. The Fade harness does not cover swaying-tree or distant-image
  building fades. These and the other Hidden Units limits are in the
  [release notes](release-0.9.10-notes.md#known-limits).
- Tag CI: no failures, reruns or stalls. Linux packages, Windows, macOS and Determinism all passed on attempt 1 (Linux packages, macOS and Windows in about 25 to 27 minutes, Determinism in about 4).
- All of 0.9.9's limits carry over (see
  [release-0.9.9-validation.md](release-0.9.9-validation.md#limits)).
