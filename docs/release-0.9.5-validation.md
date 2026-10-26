# 0.9.5 release validation

Protocol **299** (was 298 in 0.9.4), replay format **15** (unchanged),
generator version **9** (unchanged).

Tagged `v0.9.5` at `35ee5c27` (full `35ee5c27b03fd84fdb00ed4f0d9e0bb55b27db26`, annotated tag object `ec29d3c2`) and published on 2026-10-26 19:23:25 UTC.

## Scope and compatibility

0.9.5 follows 0.9.4 (protocol 298; tag `v0.9.4` -> `9f6978c8`, validation
commit `7dae3b51`). Seven commits landed linearly on `7dae3b51` (no merges), in
this order, and were integrated at `085d4c45`:

- `90eb60b4`, `b01077c9`: **a dying unit takes no more hits.** The original's
  hit routine (`51a140`) returns before touching a unit already flagged dying,
  after its random spread draw. `hurt()` now does the same for a unit at zero
  health or below (a retail construction site excepted, as in the sweep), and
  the HP subtractions in `hurt()`, in the unattended-construction decay and in
  deadly water are done in 64 bits and clamped to the `Fixed` range. A second
  same-tick hit used to move the kill credit and death type, and a big one took
  the health past -32768, a signed overflow in `fixed.h` (UBSan in `retailgap`,
  `creon_death_sound(_peers)` and `render_geometry_stages`) that wrapped to about
  +30,000 so the unit survived. `maxdamage` is capped at 32,767 on load and goes
  through `truncSat32`. New `hp_overflow` test (fails before, including the
  decay case). **Hashed simulation change: protocol 299.**
- `d06ba6d0`, `1ef443cb`: test and tool fixes from the 0.9.4 test report
  (items 1.3 to 1.6 and `desync-hunt`): MinGW's `create_symlink` always fails, so
  the AI profile and Cartographer UI tests fall back to a hard link, then a copy
  (5 tests); `hpitool cat` writes binary on Windows (3 tests); a `createGame`
  whose map cannot be built no longer spins the `--mpai`/`--mphost` harness and
  the interactive lobby shows *CANNOT CREATE* with the reason (new
  `harness_createfail`, fails before); an out-of-bounds read in
  `cartographer_ui_smoke`; `desync-hunt.sh` matches the desync messages and takes
  `BUILD`. Not hashed.
- `f3518c51`: **the data folder is only read.** Map downloads, Test Map copies,
  mod downloads and saved generated maps go to the per-user folder
  (`hpi::setUserMapRoot`); `takserver` defaults `--map-cache-dir` to
  `TAKengine/takserver` there. New `map_cache_readonly` (fails on `7dae3b51`).
  Not hashed.
- `19b50831`: the Legion scenario baseline retaken (below). Data only.
- `085d4c45` (integration): `kNetVersion` 298 -> 299 with a history comment,
  `tools/replay_test.cpp` pins 299 and refuses 298, CHANGELOG,
  `docs/multiplayer-design.md`.

`git diff --stat v0.9.4 085d4c45 -- src`: 11 files changed, 154 insertions(+),
27 deletions(-) (`src/client` 3 files, `src/hpi` 2, `src/net` 3,
`src/cartographer` 1, `src/server` 1, `src/sim` 1; nothing in `src/ai`). Over the
whole tree at the integration commit: 37 files changed, 2,978 insertions(+),
2,275 deletions(-), of which `tools/scenarios/baseline.json` is 2,193 lines
each way. `git diff --stat v0.9.4 v0.9.5 -- src` is the same, so the preparation commits changed no `src` file. Over the whole tree, `v0.9.4..v0.9.5` is 11 commits and 43 files changed, 3,371 insertions(+), 2,302 deletions(-), of which `tools/scenarios/baseline.json` is 2,193 lines each way. Outside `src` the follow-up commits touch only CHANGELOG, README, `CLAUDE.md`, the notes, this report, `docs/img/` and the version line in `CMakeLists.txt`.

The preparation commit on top changes no code: `CMakeLists.txt` (the version
line), `CHANGELOG.md` (dated, links), `README.md`, the version lines in
`docs/`, the notes, this report, `docs/img/README.md` and `docs/img/title.jpg`.

**Compatibility.** `kNetVersion` is **299** in `src/net/protocol.h` (298 at
`v0.9.4`). `kReplayFormat` is **15** in `src/net/replayhdr.h`, unchanged. The
map generator's `formatVer` **9** is unchanged.

- **Multiplayer.** `Server::handshake` rejects a Hello whose protocol differs
  from `kNetVersion`; the release string is never compared. So **0.9.5 plays
  with no 0.9.4 or older client or server**.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  format is 1..15 and its protocol equals `kNetVersion`, so 0.9.5 refuses every
  0.9.4 (protocol 298) replay. `tools/replay_test.cpp` pins `kNetVersion == 299`
  and refuses 298.
- **Hashed state.** It changes only in games where a unit is hit again in the
  tick it dies. None of the seven gate games does that, so all gate hashes are
  unchanged (below); the protocol bump is for the games that do.
- **Maps, mods and settings.** No map, recipe or package format changed. Caches
  an older version left in a data folder are ignored.

## Local preparation checks

All ran on Linux, cores 4-23, with optimized Debug builds (`build-o2`, `-O2 -g`,
ccache + mold), configured with `TAK_TEST_DATA` so the data-backed tests ran, on
the integrated tree at `085d4c45`. Testing followed the minimal-testing rule:
each change's own proving tests, then one full suite at integration.

- **Builds.** Every target built in `build/` (Release) and `build-dbg/` after
  the integration. The release binary prints `0.9.4 (build v0.9.4-8-g085d4c45)`
  at that commit, because the version number changes in the preparation commit.
- **Full suite**, nightly label included (`ctest -j16`): **767 of 769**
  passed, 1 skipped (`vashim_system_loader`), 1 failed: `vashim_unavailable`,
  the known failure on this host (the local `third_party/ffmpeg-bink` predates
  the libva shim, which CI builds). No timeout reruns were needed. `replay`,
  `replay_always` and `hp_overflow` also passed when run alone.
- **Legion scenario gate.** All 67 `legion_check_*` tests passed. This gate
  **went unrun from 0.8.11 to 0.9.4**: every validation in that span ran
  `-LE nightly`, which skips it, and its last pass was 0.8.10. Its stored
  `hash@` keys had gone stale at v0.9.0 (`7c0325b6`, reclaiming as the retail
  RECLAIM, which changed what `stateHash()` folds in). Before the retake, on the
  integration tree: **FAIL**, 36,522 keys checked, 2,193 failed, all of them
  `hash@` keys. The retake was `legion_check.py baseline --hashes --keys
  'hash@*'` over all 145 `tools/scenarios/*.scn` (`--mode both --offsets
  gate`); the 989 hash entries and 10 exceptions the tool added on its own were
  removed, so only the values of 2,193 existing `hash@` keys in 143 scenarios
  changed, and every other entry, exception, reference and history line is
  byte-identical. After: `check` and `check --cumulative` both **PASS** (36,522
  keys, 0 failed, 4,415 ratchets, 4,705 licensed, 266 floor exceptions, 864
  report-only) and `validate` **PASS** with 0 problems. Movement and timing
  limits are unchanged.
- **Detmath guard** (`tools/check-detmath.sh`): OK.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): "DETERMINISM
  OK", golden `dcef618cd2e4d558` for g++ and clang++ at -O0, -O2 and -O3,
  unchanged from 0.9.4; the aarch64 legs were skipped (toolchain missing).
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`, a
  clean scratch XDG_DATA_HOME per game, `TAK_LEGION=1` for Legion,
  `TAK_AI_LEVEL=6` for the Retail AI, `--crusades` for the Crusades games,
  `TAK_SPEED=80`). All seven hashes are **unchanged from 0.9.4**, and a Retail
  60 s run at normal speed (1x) gave the same hash as at 80x.

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `d375d03a6c472773` | `387df8b0916a182e` |
| Legion | `426ab0842b709c28` | `2d09d13d3ef0d324` |
| Retail AI (`TAK_AI_LEVEL=6`) | `c5846d769e0a7d93` | not run |
| Crusades, Retail pathfinding | not run | `3a380ae964f75b5a` |
| Crusades, Legion | not run | `772874591d07dbdf` |

- **Windows test fixes.** The 0.9.5 Windows test fixes were verified on oden-win (Windows 11, MinGW GCC 16.2, GNU ld) at tag v0.9.5 (35ee5c27), built as Release and as Debug -O2 against the retail test data, with each test run individually through the updated `runctest.py` (which normalises CRLF). Passing in both builds: ai_profile_data, cartographer_aiprofile_mod_ui, cartographer_aiprofile_extras_ui, cartographer_aiprofile_packs_ui, cartographer_aimod_ui and hp_overflow, each in 1 to 4 s. Passing in the Debug -O2 build, where they are registered: cartographer_ai_profile_e2e (77 s), classic_ai_map_profile (95 s), ai_mod_profile (66 s) and render_shadow_cache (32 s). The three profile tests that timed out or failed in the 0.9.4 pass now pass in 66 to 95 s. No `--mpai` games were run on Windows for this check.

Not run locally: the lobby *CANNOT CREATE* message and the Benchmark and
single-player paths when a map fails to build (compiled, and covered by
`harness_createfail` for the harness only); `desync-hunt.sh` (syntax-checked
only); the macOS and Windows builds; CPack DEB/RPM.

## Screenshots

Only `title.jpg` was retaken, by the prepared tree's build (Debug `-O2`, CMake
`VERSION 0.9.5`), with the 0.9.1 title capture (`takclient menu --shot`,
offscreen driver, 1920 x 1080 window cropped to the 1440 x 1080 artwork; script
`~/tak-tmp/scratch/release-095/shots/title.sh`). It reads VERSION 0.9.5 and shows
the three doors. `docs/img/README.md` has a "Retaken for 0.9.5" note and the
updated row. No picture shows a dying unit, a downloaded-map folder or the
lobby's new message, so no other picture was retaken or deleted.

## Remote multiplayer sweep and other platforms

**Skipped**, per the user's minimal-testing rule for this release. No remote
multiplayer sweep (the 37-scenario Retail and Legion tables against
`tak.pgnet.us` and `vpn3.pgnet.us`) was run, and `--mpai` was not run on Windows
or macOS. The simulation change is covered locally by `hp_overflow`, the seven
gate hashes, the Legion scenario gate and the cross-compiler golden hash;
cross-platform determinism rests on the tag Determinism workflow and the
Windows and macOS test jobs. The last remote sweep is the 0.9.4 one
([release-0.9.4-validation.md](release-0.9.4-validation.md#remote-multiplayer-sweep));
protocol 299 was never exercised between a client and a remote referee before
release.

## CI and packages

All four tag workflows ran on `35ee5c27` and passed on the first attempt, with no reruns: [Linux packages 40376004449](https://github.com/pocketgeek/tak-engine/actions/runs/40376004449) (18 jobs, all success, finished 18:46 UTC), [Windows 40376004367](https://github.com/pocketgeek/tak-engine/actions/runs/40376004367) (x64, arm64, sign and publish, cache prune), [macOS 40376004589](https://github.com/pocketgeek/tak-engine/actions/runs/40376004589) (arm64 and x64 build, then sign and notarize for each) and [Determinism 40376004317](https://github.com/pocketgeek/tak-engine/actions/runs/40376004317) (dispatched on the tag; detmath, unit-tests 8/8 and sim-equiv all success; GCC and Clang x86-64 at -O0/-O2/-O3 all agree on golden `dcef618cd2e4d558`, "DETERMINISM OK", unchanged from 0.9.4). The Determinism sim-equiv job took 61 minutes because "Install build deps" stalled for 59 minutes (18:15:55 to 19:14:42 UTC); it finished by itself and the driver-equivalence test printed OK, the same runner stall as in 0.9.4.

All 29 assets were downloaded and match GitHub's SHA-256 digests and sizes. The file set is identical to 0.9.4's: all 29 per-asset file lists (packages, installer, zips, DMGs, debug archives) match, apart from the download path printed in the installer's listing. Sizes grew slightly, from +0.01% (Linux x64 debug tarball) to +0.10% (Arch package); the largest absolute growth is the Windows x64 debug zip at +20 KB. Every takclient and takserver (36 Linux, 22 Windows and macOS) carries the `v0.9.5` build string and none carries an older `v0.9.x` string; the 18 cartographer binaries carry no version string, as in 0.9.4. Every one that can run here reports `0.9.5 (build v0.9.5)` (Slackware, Debian, Ubuntu, Fedora, openSUSE x86-64 and the debug tarball); the Arch client needs Arch's newer glibc (`GLIBC_2.44`) and does not start on this Fedora machine, as with 0.9.0 to 0.9.4, while its server reports the same version. None of the 54 Linux ELF binaries has a libva, libdrm, libsecret, GLib or libsqlite `NEEDED` entry, and none contains `__vaDriverInit_`, `SQLite format` or `sqlite3_`; the Windows and macOS binaries contain no SQLite strings either. The licence files (`OPUS-LICENSE.txt`, `CHAT-FONT-LICENSE.txt`, `EDITOR-FONT-LICENSE.txt`, `STREAMING-LICENSES.txt`, `ACME-LICENSES.txt`) are in every package, zip, installer, DMG and app. All 27 `STREAMING-LICENSES.txt` copies are byte-identical to 0.9.4's (the same two checksums, 22 and 5 copies), all 27 carry the OpenSSL notice, and none contains SQLite text. No `mods/`, `overrides/` or `click.hpi` is shipped. The packaged Linux `takclient --help` lists only `--data`, `--play-map`, `--game-mod` and `--version`. The Fedora 44 x86-64 RPM's `takserver` (`0.9.5 (build v0.9.5)`, "listening ... (protocol v299)", retail gameplay hash `d6000f4fa9095a51`) documents `--map-cache-dir` in its `--help`. It ran the Retail AI game on Inner Circle with `--local --no-auth --seed 1` and a scratch HOME and `XDG_DATA_HOME`; the client was the Debug build at `35ee5c27` (`build-dbg`) with `TAK_AI_LEVEL=6 TAK_SPEED=80 --mpai --time 60`, and the game ended `hash=c5846d769e0a7d93 units=4 err=none end=timelimit` (tick 1800), the same as 0.9.4 and as the local gate. No desync or referee-suspect line appeared. The server's map and mod caches landed under the scratch `XDG_DATA_HOME/TAKengine/takserver`, the client's under `TAKengine/TAKingdoms`. The same RPM's `cartographer "Inner Circle" --bundle` loaded the map (96 x 96 blocks, 4 start positions, 215 features, 1261 sections), wrote a 379,088-byte `.kmp` and exited 0, as in 0.9.4. The packaged release `takclient --data <read-only copy> --play-map <that .kmp>` reached the single-player lobby (local server on a private port, protocol v299, game created, map verified, features loaded) with the retail data folder as a `chmod a-w` copy that symlinks every file. Nothing was created or modified in that copy or in the real `assets/game`, and the map cache (`.kmp` and `.takmap`) and mod cache landed under the scratch `XDG_DATA_HOME/TAKengine/TAKingdoms`. Every `blob` `v0.9.5` link in the release notes (6 links, 2 with anchors: `public-server.md#versions` and `release-0.9.4-notes.md#known-limits`) resolves to a file and heading present at the tag.

Signing. Windows: on oden-win, `Get-AuthenticodeSignature` reports **Valid** (timestamped, "Curtis Edge") for all 7 signed files: the x64 installer and `takclient`, `takserver` and `cartographer` in both the x64 and arm64 zips, the same 7 as 0.9.4. The x64 `takclient.exe` and `takserver.exe` report `0.9.5 (build v0.9.5)`, and the arm64 ones carry the `v0.9.5` build string. `--mpai` was not run on Windows or macOS, per the release rule (no build flags, compilers or CI determinism coverage changed); the Windows and macOS tag CI determinism tests cover them.

macOS: on the MacBook (no builds, macOS 26.6.2), both DMGs and all four apps (the game and TAK Cartographer, from the DMGs and from the zips) pass `stapler validate`, and `spctl -a -vv` accepts them as "Notarized Developer ID" (Curtis Edge, L479Y59RN8). Both DMGs themselves are also stapled and accepted as Notarized Developer ID. Each app passes `codesign --verify --strict --deep` and has `NSHighResolutionCapable` true; the game apps have `NSMicrophoneUsageDescription`, and the takclient has the `audio-input` entitlement (the other binaries do not). The bare zip programs (takclient, takserver, cartographer) pass `codesign --strict` with the same team id; `spctl` calls them "not an app", as expected. The macOS `takclient` and `takserver` report `0.9.5 (build v0.9.5)` (arm64 and x64, apps, bare programs and debug zips) and contain the MP4 muxer strings. No licence file is missing, and no `mods/`, `overrides/` or `click.hpi` is present. `otool -L` of the shipped arm64 and x64 `takclient`, `takserver` and `cartographer` shows no `libssl`, `libcrypto`, `libsqlite` or libopus dylib. Both takclients contain `OpenSSL 3.5.8 25 Aug 2026` and no SecureTransport TLS symbols, and Security.framework is still linked for the system root anchors.

Published with `gh release edit v0.9.5 --draft=false --latest` on 2026-10-26 19:23:25 UTC with 29 assets; it is the Latest release. 0.9.0 to 0.9.4 stay on the releases page.

## Limits

- No game mixing 0.9.5 with 0.9.4 or older is possible: protocol 299 is refused
  by every older server and client. No mixed-version game was run with a real
  0.9.4 binary.
- No remote multiplayer sweep, and no Windows or macOS `--mpai`, for 0.9.5 (see
  above).
- The scenario gate had not run for seven releases (0.8.11 to 0.9.4); its
  baseline is now current, but nothing else in that span was re-checked by it.
- A `takserver` started by hand without `XDG_DATA_HOME` or `--map-cache-dir`
  writes its cache to the real per-user folder. The `--mpai` command in
  `CLAUDE.md` (`35ee5c27`) and `tools/desync-hunt.sh` (after the tag) now give
  the server a scratch `XDG_DATA_HOME`; `desync-hunt-remote.sh` already passes
  `--map-cache-dir`. Any other hand-started test server still needs one.
- `tickConstruction` adds to health without an upper limit, so a mod with an
  absurdly short build time could overflow it; a retail construction site's
  progress health is not updated when it is hit. Both predate 0.9.5.
- The Windows `linkOrCopy` fallback skips directories.
- The `mod_transfer` flake from the 0.9.4 test pass ("Not a directory ...
  ModCache") was not investigated.
- Local only: `vashim_unavailable` fails against a `third_party/ffmpeg-bink`
  built before the shim; CI builds FFmpeg with it.
- Tag CI: no failures and no reruns; all four tag workflows passed on attempt 1. The Determinism run's sim-equiv job was slow (the runner's "Install build deps" step stalled for 59 minutes) but passed without intervention, as in 0.9.4.
- All of 0.9.4's limits carry over (see
  [release-0.9.4-validation.md](release-0.9.4-validation.md#limits)).
