# 0.9.3 release validation

Protocol **297** (was 296 in 0.9.2), replay format **15** (unchanged),
generator version **9**, campaign payload **4**, database schema **9**.

<!-- TODO-LEAD: tag line: "Tagged `v0.9.3` at `<short sha>` (full `<sha>`, annotated tag object `<obj>`, "TAK Engine 0.9.3") and published on <date time> UTC." -->

## Scope and compatibility

0.9.3 follows 0.9.2 (protocol 296; tag `v0.9.2` -> `2c48b983`, validation
commit `43d22d18`). Every code change landed on main linearly after `43d22d18`
(no merges):

- `66914175`: per-weapon clear-line test, the Trebuchet fix. `Weapon::needsClearLine`
  (ranged, not `ballisticClass`) decides each weapon on its own in
  `slotClearShot`/`slotAims`, `repositionForShot` and
  `World::approachNeedsClearLine` (`tickCombat`, `findTarget`, the flyer and
  structure hold, Legion's `reachPx`); `UnitType::lobs` is removed; a unit that
  may not chase drops an auto target it has no line to. **Hashed simulation
  change: protocol 297.** `ballistic_line_test` (Retail and Legion) fails before
  and passes after.
- `04f9e934`: `docs/retail-engine.md` correction (the test's wall is CAVStalag01
  stalagmites).
- `9da5f2d1`, `21f742ce`: macOS streaming moves from SecureTransport to the
  static OpenSSL 3.5.8 that `build-stream-deps.sh` builds (client side only;
  nothing hashed). `tools/ffmpeg-securetransport-nonblock.patch` is removed;
  `tak::net::systemRootsPem()` exports the system anchors; `streaming_rtmps` uses
  a throwaway root CA; docs wording.
- `4e766af8`: `kNetVersion` 296 -> 297, `tools/replay_test.cpp` pins 297 and
  refuses 296, CHANGELOG, `docs/multiplayer-design.md`, `docs/user-guide.md`.

- `87907355`: HUD top-left column (`src/client/hudcorner.h`): the stream
  banner, clock, scorecard, objectives and voice speakers stack;
  `hudcorner_test` checks 5,760 layouts (the old placement fails 5,744).
  Client only.
- `37bfd340`, `6e34fd16`: the streaming panel saves resolution, frame rate,
  bitrate and encoder (`settings_test` round trip, invalid values, defaults
  reset; bitrate floor on load). Client only.
- After these two client branches were added on top of the prep commit, the
  `build-o2` tree was rebuilt and `ctest -L quick` plus `hudcorner` and the
  settings tests were run once: `ctest -L quick` 519 of 521 passed (the known
  host-only `vashim_unavailable` failed, `vashim_system_loader` skipped);
  `settings`, `mod_settings` and `hudcorner` passed.

<!-- TODO-LEAD: diff stat: `git diff --stat v0.9.2 v0.9.3 -- src` (files, insertions, deletions) and the list of files outside src. At 4e766af8, `git diff --stat v0.9.2 4e766af8` touches 28 files (702 insertions, 170 deletions), 4 of them in .github/workflows. -->

The preparation commit on top of `4e766af8` changes no code: `CMakeLists.txt`
(the version line), `CHANGELOG.md` (dated, links), `README.md`, the version
lines in `docs/`, the notes, this report, `docs/img/README.md` and
`docs/img/title.jpg`.

**Compatibility.** `kNetVersion` is **297** in `src/net/protocol.h` (296 at
`v0.9.2`). `kReplayFormat` is **15** in `src/net/replayhdr.h`, unchanged. The
map generator's `formatVer` **9**, the campaign payload's `kVersion` 4 and
`kSchemaVersion` 9 are unchanged.

- **Multiplayer.** `Server::handshake` rejects a Hello whose protocol differs
  from `kNetVersion`; the release string is never compared. So **0.9.3 plays
  with no 0.9.2 or older client or server**.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  format is 1..15 and its protocol equals `kNetVersion`, so 0.9.3 refuses every
  0.9.2 (protocol 296) replay. `tools/replay_test.cpp` pins `kNetVersion == 297`
  and refuses 296.
- **Maps, mods and settings.** No settings key, map, recipe or package format
  changed.

No mixed-version check was run: no 0.9.2 client joined a 0.9.3 server (or the
reverse), and no 0.9.2 replay was opened on 0.9.3. The points above rest on the
source and `replay_test`.

## Local preparation checks

By the user's rule (minimal testing: each change's own tests, one full suite,
the gates), no Release suite, `-O0` Debug, Clang or ASan run and no repeat runs
were made. All ran on Linux, cores 4-23, with optimized Debug builds
(`build-o2`, `-O2 -g`, ccache + mold), configured with `TAK_TEST_DATA` so the
data-backed tests ran.

**On the integrated tree** (main at `4e766af8`, all targets, once):

- **Change tests.** `ballistic_line_test` (Retail and Legion) fails on
  `43d22d18` and passes after the fix; `streaming_rtmps` runs against a chain
  from a throwaway root CA, refuses a certificate for another host name and
  one from an untrusted root (both with the TLS CERTIFICATE reason).
- **Full suite.** `ctest -LE nightly -j16`: **627/628** passed. The one
  failure, `vashim_unavailable`, is the known failure on this host: the local
  `third_party/ffmpeg-bink` predates the libva shim, which CI builds;
  `vashim_system_loader` is skipped for the same reason. No test timed out.
- **Detmath guard** (`tools/check-detmath.sh`): OK.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): "DETERMINISM
  OK", golden `dcef618cd2e4d558`, unchanged from 0.9.2; the aarch64 legs were
  skipped.
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`,
  a clean scratch XDG_DATA_HOME per game, `TAK_LEGION=1` for Legion,
  `TAK_AI_LEVEL=6` for the Retail AI, `TAK_SPEED=80`). All five hashes are
  unchanged from 0.9.2, and a Retail 60 s run at normal speed (1x) gave the
  same hash as at 8x:

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `d375d03a6c472773` | `387df8b0916a182e` |
| Legion | `426ab0842b709c28` | `2d09d13d3ef0d324` |
| Retail AI (`TAK_AI_LEVEL=6`) | `c5846d769e0a7d93` | not run |

- **The Trebuchet problem, re-measured.** In a 16-seed set of Classic (Retail
  AI) Creon against Aramon duels on Edmont's Field (out-of-tree probe), the
  old code had 80 holds of a target for over 60 s without a shot (the longest, an
  Aramon Trebuchet, 2,820 s on seed `0x2222`) and one duel with no winner in 90
  minutes; the new code has 4 such holds and a winner in that duel too. Logs: `~/tak-tmp/wt/sweep093/`.
- **Not explained.** On seed `0x6666` a Prismatic Mirror still held an Aramon
  Trebuchet target for 120 s without firing although a line existed; and a
  Vermage on attack-move with Maneuver dealt no damage while taking 40 hits,
  the same on `43d22d18`. Neither was investigated.

**macOS streaming** (branch `task-093-macssl`, on the MacBook Neo, Apple
Silicon; no builds of the release): the new FFmpeg configuration built against
the static OpenSSL. A manual TLS handshake to a public site (google.com)
through the exported system anchors succeeded, and `stream_network_test
--system-roots` found GTS Root R1 and GlobalSign Root CA, YouTube's ingest
roots, in the exported bundle (it checks the bundle against itself, so an
anchor OpenSSL cannot decode is skipped rather than detected). A test app
(`~/tak/neo-test-093` on the Neo) was left for a real YouTube streaming try.
No automated test shows a chain to a real system root being accepted. macOS x64
was not run with the new FFmpeg configuration.

**On the prepared tree** (branch `task-093-docs`): `takclient menu` renders
VERSION 0.9.3 (the title screenshot).

## Screenshots

Only `title.jpg` was retaken, by the prepared tree's build (Debug `-O2`, CMake
`VERSION 0.9.3`), with the 0.9.1 title capture (`takclient menu --shot`,
offscreen driver, 1920 x 1080 window cropped to the 1440 x 1080 artwork; script
`~/tak-tmp/scratch/release-093/shots/title.sh`). It reads VERSION 0.9.3.
`docs/img/README.md` has a "Retaken for 0.9.3" note and the updated row. No
other picture shows anything 0.9.3 changes.

## Remote multiplayer sweep

**SKIPPED for this release, at the user's request** (minimal testing). No
37-scenario sweep was run against the remote referees, and `--mpai` was not run
on Windows or macOS. What covers the ground it would have covered:

- client/referee agreement on the new code: the `--mpai` gates above (a real
  `takserver` referee with server-side AI, no desync or referee-suspect line);
- cross-platform and cross-compiler determinism: tag CI (the Determinism
  workflow's GCC and Clang x86-64 builds against golden `dcef618cd2e4d558`, and
  the Windows and macOS CI determinism tests: detmath golden,
  `sim_driver_equiv`, `navigation_determinism`, `fp_contract` on macOS).

So there are no 0.9.3 reference ticks and hashes for the sweep's cases.

## CI and packages

<!-- TODO-LEAD: tag CI: the four workflow runs (Determinism, Linux packages, Windows, macOS) with links, result and any reruns. Note that every platform's FFmpeg cache misses once on the tag (cache keys changed in 9da5f2d1), so the first CI build is slow. -->

<!-- TODO-LEAD: packages: 29 assets vs GitHub digests; file set vs 0.9.2; build strings v0.9.3 and --version; no libva/__vaDriverInit_; licence files incl. STREAMING-LICENSES.txt (which gained 3 lines in 9da5f2d1); no mods/overrides/click.hpi; packaged `--help`; RPM takserver protocol v297 run. -->

<!-- TODO-LEAD: Windows Authenticode on oden-win (9 signed files expected). -->

<!-- TODO-LEAD: macOS DMG and app notarisation on the Neo (stapler, spctl, codesign); optionally run a real RTMPS stream from the shipped macOS build. -->

<!-- TODO-LEAD: publication: `gh release edit v0.9.3 --draft=false --latest` date and time UTC, 29 assets, Latest; 0.9.0 to 0.9.2 stay. -->

## Limits

- No game mixing 0.9.3 with 0.9.2 or older is possible: protocol 297 is refused
  by every older server and client. No mixed-version check was run.
- The remote multiplayer sweep was skipped (above); `--mpai` on Windows and
  macOS was not run (tag CI determinism covers them).
- New (see [the notes](release-0.9.3-notes.md#known-limits)): every arrow
  tower and archer now fires over scenery (as in the original); an explicit
  attack order to a building with no line still holds without firing for a unit
  that needs the line; one unexplained hold (seed `0x6666`) and one Vermage with
  no damage (also in 0.9.2); on macOS, keychain trust you added yourself is not
  used for streaming, no automated test shows a real system-root chain being
  accepted, and macOS x64 was not run with the new FFmpeg.
- Local only: `vashim_unavailable` fails against a `third_party/ffmpeg-bink`
  built before the shim; CI builds FFmpeg with it. The shared Mac FFmpeg prefix
  (`~/tak/src/third_party/ffmpeg-bink`) is still the SecureTransport build.
- All of 0.9.2's limits carry over (see
  [release-0.9.2-validation.md](release-0.9.2-validation.md#limits)), except
  that macOS streaming no longer uses SecureTransport and the mixed-version
  notes now concern 0.9.2.
- <!-- TODO-LEAD: Tag CI: whether any workflow needed a rerun. -->
