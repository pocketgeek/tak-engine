# 0.9.8 release validation

Protocol **302** (was 301 in 0.9.7), replay format **15** (unchanged),
generator version **9** (unchanged).

Tagged `v0.9.8` at `2a26b2b4` (full `2a26b2b48559e21666e1bf5a79f68445218afa32`, annotated tag object `ac0fa52d`) and published on 2026-10-27 23:32:45 UTC.

## Scope and compatibility

0.9.8 follows 0.9.7 (protocol 301; tag `v0.9.7` -> `3c4cf3b2`). Main after
0.9.7's validation commit `98810658` has the following, in order of landing;
the code tested below is `b23f8cac` (full suite) and `7b99dfff` (the
environment cleanup on top, which changes no game state):

- `4585e9da` Sim: guards answer their guarded unit's attacker; revives wait
  for mana and respect the unit limit (`kNetVersion` 301 -> 302).
- `5ce3c0a1` Docs: guard defends, revive retry and unit limit (CHANGELOG,
  retail notes).
- `99cc8a91` Client: right-click Guard for builders (the original's per-unit
  resolver, `src/client/friendlyorder.h`; `friendly_order_test`), merged from
  `task-rclick-guard` as `e9a3c9fb` with its docs commit `b256fa18`.
- `0dc7cb83`, `4d4244df`, `30559fb4` Documentation audit of about 35 living
  documents (README, user guide and quick start; developer, server and
  multiplayer design docs; Cartographer, pathfinding, retail-engine, AI,
  campaign and research docs), merged from `task-docs-a`, `-b` and `-c` as
  `cc51e368`, `7aae1a73` and `49591f08`.
- `48e004b6`, `b23f8cac` Docs: guard defence, right-click Guard and revive
  limits in the living docs; guarding an ally's unit is not possible.
- `7b99dfff` Release builds read no `TAK_*` diagnostic environment variables
  outside the four `takserver` timers (`src/util/devenv.h`).

`git diff --stat 98810658 7b99dfff`: 52 files changed, 1175 insertions(+),
257 deletions(-); `-- src`: 14 files changed, 290 insertions(+), 108
deletions(-) (`src/sim` 3 files, `src/client` 4, `src/net` 2, `src/server` 2,
`src/ai` 1, `src/cartographer` 1, `src/util` 1; the rest is tests,
`CMakeLists.txt`, `CHANGELOG.md`, `CLAUDE.md`, `tools/we_timing.sh` and the
documentation). 14 commits.

Final `git diff --stat v0.9.7 v0.9.8`: 58 files changed, 1610 insertions(+), 290 deletions(-), over 17 commits (`git log v0.9.7..v0.9.8`: the 0.9.7 validation commit `98810658`, the 14 commits listed above, and the two preparation commits `4ab1d890` and `2a26b2b4`). `-- src` alone: 14 files changed, 290 insertions(+), 108 deletions(-) (`src/ai` 1, `src/cartographer` 1, `src/client` 4, `src/net` 2, `src/server` 2, `src/sim` 3 including `pathsearch.cpp`, `src/util` 1). Outside `src`: 44 files, 1320 insertions(+), 182 deletions(-) (34 in `docs/`, 5 in `tools/`, plus `CMakeLists.txt`, `CHANGELOG.md`, `CLAUDE.md` and `README.md`).

The preparation commit on top changes no code: `CMakeLists.txt` (the version
line), `CHANGELOG.md`, `README.md`, the version and protocol lines in `docs/`,
the notes, this report, `docs/img/README.md` and `docs/img/title.jpg`.

**Compatibility.** `kNetVersion` is **302** in `src/net/protocol.h` (301 at
`v0.9.7`). `kReplayFormat` is **15** in `src/net/replayhdr.h`, unchanged. The
map generator's `formatVer` **9** is unchanged. The gameplay data hash of the
test install is `d6000f4fa9095a51`.

- **Multiplayer.** `Server::handshake` rejects a Hello whose protocol differs
  from `kNetVersion`; the release string is never compared. So **0.9.8 plays
  with no 0.9.7 or older client or server**.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  format is 1..15 and its protocol equals `kNetVersion`, so 0.9.8 refuses every
  0.9.7 (protocol 301) replay. `tools/replay_test.cpp` pins 302.
- **Hashed state.** It gains `Unit::guardHeard` (folded in with a tag when
  set); `Unit::attackedTick` is not hashed. A guard's answer and the raising
  limits change the game only where a guard's charge is hit, or a raising
  meets a limit or a short computer caster. No gate game does, so **the gate
  hashes did not change** (below).
- **Maps, mods and settings.** No map, recipe, package or settings format
  changed.

## Local preparation checks

All ran on Linux with optimized Debug builds (`build-o2`, `-O2 -g`, ccache +
mold, `TAK_TEST_DATA` set so the data-backed tests ran) and a Release build.
Every takclient and takserver run used scratch `HOME`, `XDG_DATA_HOME` and
`XDG_CONFIG_HOME`.

- **Builds.** At `7b99dfff` the binaries print `0.9.7 (build ...)`, because
  the version number changes in the preparation commit; the prepared tree's
  own `takclient --version` prints `0.9.8 (build v0.9.7-15-g7b99dfffce6a-dirty)`
  from a fresh Release build and from `build-o2` (the tree was dirty only with
  the preparation edits).
- **New tests.** `helpbuild_test` `guardDefends` (with its same-hash
  determinism check), `production_test` `abandonedReviveWaits` and
  `reviveRespectsUnitLimit`, and `friendly_order` / `friendly_order_data` all
  fail on the old code and pass on the new.
- **Full suite** (`ctest`, `build-o2`, 777 tests) on the merged code at
  `b23f8cac`: **776 of 777** passed; the one failure is `vashim_unavailable`,
  the known failure on this host (the local `third_party/ffmpeg-bink` predates
  the libva shim, which CI builds).
- **Quick suite** (`ctest -L quick`) after the environment cleanup at
  `7b99dfff`: **512 of 513** passed; the one failure is `vashim_unavailable`
  (as above).
- **Detmath guard** (`tools/check-detmath.sh`, on the prepared tree): OK.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): golden
  `dcef618cd2e4d558`, unchanged from 0.9.7.
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`,
  `TAK_LEGION=1` for Legion, `TAK_AI_LEVEL=6` for the Retail AI, `--crusades`
  for the Crusades games, `TAK_SPEED=80`, `build-o2`). The gates ran on the
  guard and revive change; the Retail AI 60 s gate was run again after the
  environment cleanup and gave the same hash.

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `d0da4ccc71d2067c` | `e820d37865085bd9` |
| Legion | `c3f6a3a13f0c167f` | `0783cb05f7f128cf` |
| Retail AI (`TAK_AI_LEVEL=6`) | `4952493f51b9a568` | not run |
| Crusades, Retail pathfinding | not run | `f2f51dc1703fc209` |
| Crusades, Legion | not run | `c4189ae189dd68dc` |

**The gate hashes are the 0.9.6 and 0.9.7 values.** The AIs never give Guard
orders, so no guard answers an alarm and `guardHeard` stays zero, and no gate
game raises past a limit or has a computer caster retry a raising. The
cross-compiler golden is unchanged for the same reason.

## Screenshots

One picture was retaken, by the prepared tree's build (Debug `-O2`, CMake
`VERSION 0.9.8`): `docs/img/title.jpg`, with the 0.9.1 title capture
(`takclient menu --shot`, offscreen driver, 1920 x 1080 window cropped to the
1440 x 1080 artwork). It reads VERSION 0.9.8 and shows the three doors (looked
at). Nothing else in 0.9.8 changes a picture, so none was retaken or deleted.
`docs/img/README.md` lists the details.

## Desync sweeps

No local or remote desync sweep (`tools/desync-hunt.sh`,
`tools/desync-hunt-remote.sh`) was run for 0.9.8, and no two-client game, so
no multiplayer run had a guard answering an alarm (the AIs never guard); the
answer is covered by `helpbuild_test` (including its same-hash check).

None is planned before the tag: by the project's testing rule a release runs
no remote sweep, and the tag CI determinism tests are the cross-platform check.

## CI and packages

All four tag workflows ran on `2a26b2b4` and passed on attempt 1, with no failure, rerun or stall: [Linux packages 40597937993](https://github.com/pocketgeek/tak-engine/actions/runs/40597937993) (18 jobs, all success, finished 23:22 UTC), [Windows 40597938082](https://github.com/pocketgeek/tak-engine/actions/runs/40597938082) (x64, arm64, sign and publish, cache prune; finished 23:15 UTC) and [macOS 40597938017](https://github.com/pocketgeek/tak-engine/actions/runs/40597938017) (arm64 and x64 build, then sign and notarize for each; finished 23:11 UTC), all started 22:51 UTC. [Determinism 40597938032](https://github.com/pocketgeek/tak-engine/actions/runs/40597938032) (dispatched on the tag) passed on attempt 1 in about 7 minutes: detmath, sim-equiv and unit-tests all succeeded (`100% tests passed out of 8`). GCC and Clang x86-64 at -O0/-O2/-O3 all agree on golden `dcef618cd2e4d558` ("DETERMINISM OK", unchanged from 0.9.7); the driver-equivalence test printed OK. The duplicate runs of the same commit on `main` were cancelled; the tag runs are the check.

All 29 assets were downloaded fresh and match GitHub's SHA-256 digests and sizes. The file set is identical to 0.9.7's: the per-asset file lists of the 27 packages, installer, zips and debug archives match 0.9.7's exactly (after replacing the version string), and the two DMGs hold the same three entries (the two apps and the Applications link). Sizes grew slightly, from +0.002% (openSUSE x86-64 RPM, +559 bytes) to +0.154% (Arch package, +32,801 bytes); the two Ubuntu arm64 packages for 24.04 and 26.04 shrank by 68 and 48 bytes; the largest absolute growth is the Windows x64 debug zip at +18,961 bytes. Every one of the 54 Linux ELF binaries carries the `v0.9.8` build string and no older `v0.9.x` string, and so do all 69 release `takclient`, `takserver` and `cartographer` binaries (Linux, Windows, macOS, apps and bare programs). None of the 54 Linux ELF binaries has a libva, libdrm, libsecret, GLib or libsqlite `NEEDED` entry, and none contains `__vaDriverInit_`, `SQLite format` or `sqlite3_`; the `NEEDED` set of every one of the 54 is identical to 0.9.7's. `strings` (ASCII and UTF-16) on all 69 release executables finds the `TAK_` names only as expected: `takserver` (all 23) holds exactly its four timers (`TAK_TIMEOUT_MS`, `TAK_LOAD_TIMEOUT_MS`, `TAK_STALL_MS`, `TAK_DROP_WAIT_MS`); `takclient` holds only `TAK_MARK_OUTSIDE`; `cartographer` holds only `TAK_CARTOGRAPHER_RECOVERY` and `TAK_EDITOR_RULE_NAMES` plus their split pieces; none contains `AUTOPLAY`, `TAK_START_FLYERS`, `TAK_NET_JITTER_MS`, `TAK_FOG_EYE` or `TAK_CARTOGRAPHER_PROFILE`. The licence files (`OPUS-LICENSE.txt`, `CHAT-FONT-LICENSE.txt`, `EDITOR-FONT-LICENSE.txt`, `STREAMING-LICENSES.txt`, `ACME-LICENSES.txt`) are in every package, zip, installer, DMG and app (the Cartographer app carries `EDITOR-FONT-LICENSE.txt` only, as before); the per-package licence sets equal 0.9.7's. All 27 `STREAMING-LICENSES.txt` copies are byte-identical to 0.9.7's (the same two checksums, 22 and 5 copies) and all 27 carry the OpenSSL notice. No `mods/`, `overrides/` or `click.hpi` is shipped. The packaged Linux `takclient --help` lists only `--data`, `--play-map`, `--game-mod` and `--version` (identical output from the Debian 12, Ubuntu 26.04, openSUSE and Slackware packages), and the Fedora 44 x86-64 RPM's `takclient`, `takserver` and `cartographer` all report `0.9.8 (build v0.9.8)`; so do the `takclient` and `takserver` of every other x86-64 package (the Arch package's `takclient` needs GLIBC 2.44 and cannot run on this Fedora host, as in 0.9.7). That RPM's `takserver` printed "listening ... (protocol v302)" with retail gameplay hash `d6000f4fa9095a51`. It ran the Retail AI game on Inner Circle with `--local --no-auth --seed 1` and a scratch HOME, `XDG_DATA_HOME` and `XDG_CONFIG_HOME`; the client was the Debug build at `2a26b2b4` (`build-dbg`, rebuilt with all targets for this check) with `TAK_AI_LEVEL=6 TAK_SPEED=80 --mpai --time 60`, and the game ended `hash=4952493f51b9a568 units=4 err=none end=timelimit` (tick 1800), the same as 0.9.7 and the local gate; no desync or referee-suspect line appeared. The same RPM's `cartographer "Inner Circle" --bundle <file>` (headless, scratch HOME) loaded the map (96 x 96 blocks, 4 start positions, 215 features, 1261 sections), wrote a 379,088-byte `.kmp`, exited 0 and left nothing in the scratch HOME, as in 0.9.7. The packaged release `takclient --data <read-only link farm> --play-map <that .kmp>` reached the single-player lobby (local server on a private port, protocol v302, game created, map verified, 141 features placed, 4 mana deposits) with the retail data folder as a `chmod a-w` copy that symlinks every file (1,108 entries, the same count as the real `assets/game`). Nothing was created or modified in that copy or in the real `assets/game` (no file there changed in mtime or ctime), and the map cache, mod cache and `settings.ini` landed under the scratch `XDG_DATA_HOME/TAKengine/TAKingdoms`. Every `blob` `v0.9.8` link in the release notes (4 distinct URLs, 5 occurrences, 2 with anchors: `public-server.md#versions` and `release-0.9.7-notes.md#known-limits`) resolves to a file and heading present at the tag.

## Signing

Signing. Windows: on oden-win, `Get-AuthenticodeSignature` reports **Valid** (timestamped, "Curtis Edge") for all 7 signed files: the x64 installer and `takclient`, `takserver` and `cartographer` in both the x64 and arm64 zips, the same 7 as 0.9.7. The x64 `takclient.exe` and `takserver.exe` report `0.9.8 (build v0.9.8)`, and the arm64 ones carry the `v0.9.8` build string. `--mpai` was not run on Windows or macOS, per the release rule (no build flags, compilers or CI determinism coverage changed); the Windows and macOS tag CI determinism tests cover them.

macOS: on the MacBook (no builds), both DMGs and all four apps (the game and TAK Cartographer, from the DMGs and from the zips) pass `stapler validate`, and `spctl -a -vv` accepts them as "Notarized Developer ID" (Curtis Edge, L479Y59RN8). Both DMGs themselves are also stapled and accepted as Notarized Developer ID. Each app passes `codesign --verify --strict --deep` and has `NSHighResolutionCapable` true; the game apps have `NSMicrophoneUsageDescription`, and the takclient has the `audio-input` entitlement (the other binaries do not). The bare zip programs (takclient, takserver, cartographer) pass `codesign --strict` with the same team id; `spctl` calls them "not an app", as expected. The macOS `takclient` and `takserver` report `0.9.8 (build v0.9.8)` (arm64 and x64, apps, bare programs and the debug zips' takclient) and contain the MP4 muxer strings. No licence file is missing, and no `mods/`, `overrides/` or `click.hpi` is present. `otool -L` of the shipped arm64 and x64 `takclient`, `takserver` and `cartographer` shows no `libssl`, `libcrypto`, `libsqlite` or libopus dylib. Both takclients contain `OpenSSL 3.5.8 25 Aug 2026` and no SecureTransport TLS symbols, and Security.framework is still linked for the system root anchors.

## Publication

Published with `gh release edit v0.9.8 --draft=false --latest` on 2026-10-27 23:32:45 UTC with 29 assets; it is the Latest release. 0.9.0 to 0.9.7 stay on the releases page.

## Limits

- No game mixing 0.9.8 with 0.9.7 or older is possible: protocol 302 is refused
  by every older server and client. No mixed-version game was run with a real
  0.9.7 binary.
- No Windows or macOS `--mpai` for 0.9.8; their tag CI determinism tests cover
  them.
- No desync sweep and no multiplayer or remote run had a guard answering an
  alarm; the AI never guards. The guard-defence and right-click limits are in
  the [release notes](release-0.9.8-notes.md#known-limits).
- Local only: `vashim_unavailable` fails against a `third_party/ffmpeg-bink`
  built before the shim; CI builds FFmpeg with it.
- Tag CI: no failures, reruns or stalls. Linux packages, Windows, macOS and Determinism all passed on attempt 1 (Determinism in about 7 minutes). The duplicate `main` runs of the tagged commit were cancelled.
- All of 0.9.7's limits carry over (see
  [release-0.9.7-validation.md](release-0.9.7-validation.md#limits)), except
  that the flying guard's attacked-guarded-unit reaction is ported in 0.9.8
  and `disconnect_client` passed in this release's full suite.
