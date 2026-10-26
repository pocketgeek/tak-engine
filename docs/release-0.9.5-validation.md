# 0.9.5 release validation

Protocol **299** (was 298 in 0.9.4), replay format **15** (unchanged),
generator version **9** (unchanged).

Tagged `v0.9.5` at TODO-LEAD (tag line: short and full sha, annotated tag object, publication time).

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
each way. TODO-LEAD: the diff stat `v0.9.4..v0.9.5` once the tag exists (the
preparation commit adds only version lines, documentation and `title.jpg`, so
`src` should be unchanged).

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

- **Windows test fixes.** TODO-LEAD: the integration report does not say that the
  eight Windows test fixes were re-run on oden-win, and it notes that the
  Windows tests were not re-run after the tools review fix. State the result
  here if they were (the eight tests are `ai_profile_data`,
  `cartographer_aiprofile_mod_ui`, `cartographer_aiprofile_extras_ui`,
  `cartographer_aiprofile_packs_ui`, `cartographer_aimod_ui`,
  `cartographer_ai_profile_e2e`, `classic_ai_map_profile` and `ai_mod_profile`);
  otherwise leave them to tag CI and say so.

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

TODO-LEAD: tag CI results for `v0.9.5` (Linux packages, Windows, macOS,
Determinism: run ids, attempts, reruns), the package set and sizes against
0.9.4's 29 assets, SHA-256 checks, the `--version` of each runnable binary.

Signing: TODO-LEAD: Authenticode results on oden-win (expected 7 signed files,
as 0.9.4) and macOS notarisation and `stapler`/`spctl` results on the MacBook.

Publication: TODO-LEAD: the `gh release edit` time, asset count and that it is
the Latest release; 0.9.0 to 0.9.4 stay on the releases page.

## Limits

- No game mixing 0.9.5 with 0.9.4 or older is possible: protocol 299 is refused
  by every older server and client. No mixed-version game was run with a real
  0.9.4 binary.
- No remote multiplayer sweep, and no Windows or macOS `--mpai`, for 0.9.5 (see
  above).
- The scenario gate had not run for seven releases (0.8.11 to 0.9.4); its
  baseline is now current, but nothing else in that span was re-checked by it.
- Screenshots and Cartographer saves still write into the data folder.
- A `takserver` started by hand without `XDG_DATA_HOME` or `--map-cache-dir` now
  writes its cache to the real per-user folder; this includes the `--mpai`
  command in `CLAUDE.md` and `desync-hunt.sh`, so test runs set a scratch
  `XDG_DATA_HOME` for the server too.
- `tickConstruction` adds to health without an upper limit, so a mod with an
  absurdly short build time could overflow it; a retail construction site's
  progress health is not updated when it is hit. Both predate 0.9.5.
- The Windows `linkOrCopy` fallback skips directories.
- The `mod_transfer` flake from the 0.9.4 test pass ("Not a directory ...
  ModCache") was not investigated.
- Local only: `vashim_unavailable` fails against a `third_party/ffmpeg-bink`
  built before the shim; CI builds FFmpeg with it.
- Tag CI: TODO-LEAD.
- All of 0.9.4's limits carry over (see
  [release-0.9.4-validation.md](release-0.9.4-validation.md#limits)).
