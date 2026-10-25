# 0.9.1 release validation

Protocol **295** (was 294 in 0.9.0), replay format **15** (unchanged),
generator version **9**, campaign payload **4**, database schema **9**.

TODO-LEAD: Tagged `v0.9.1` at `<sha>` (full `<full sha>`, annotated tag object `<obj>`, "TAK Engine 0.9.1") and published on 2026-10-<dd> <hh:mm:ss> UTC.

## Scope and compatibility

0.9.1 follows 0.9.0 (protocol 294; tag `v0.9.0` -> `2dc5e712`, validation
commit `6f64ab50`). Every code change landed on main linearly after
`6f64ab50`, from fix branches cherry-picked onto main (no merges):

- `fbc98cf3`, `14cb29ad`: SweetSpot is retail's drawn piece bounds
  (`retailPieceDrawnCenter` in `src/sim/retailpiecepose.h`,
  `UnitType::scriptPieceVertices`); the first model object a piece names.
  **Hashed simulation change: protocol 295.** New ctest `sweetspot`.
- `bf45dc79`, `71214f5a`: a structure's ground/water output gets retail
  GetBuilt (`0x402220`) rally-else-PARK, the ring kept to the birth point's
  component; our AI refuses pond yards (`src/ai/ai.cpp`). Hashed, under 295.
- `fb03e3be`, `db72685b`: client pacing (`src/net/pacing.h`): a client stall is
  not network jitter; the jitter target stays under `flowBufferLimit`. Client
  only, nothing on the wire. New ctest `pacing`.
- `249a72b5`: integration (lightning at the Creon Submersible re-checked
  against retail `52a4d0`; protocol 295 notes; changelog).
- `adaac1ca`: SweetSpot per-unit cache and branchless rounding,
  bit-identical.
- `4c22fbe3`, `fe0dfdcb`: factory flyers get retail GetBuilt rally-else-PARK,
  PARK's flight branch (`407ca0`), held as a PARK until the flight arrives.
  Hashed, under 295.
- `2fa90510`: docs (flyer PARK, SweetSpot speedup).
- `6269cd36`, `647668a9`: Legion factory PARK spread
  (`World::legionParkSpot`); Retail mode bit-identical. Hashed (Legion only),
  under 295. New ctest `legion_park`.
- `3ee91a78`, `fa2f62d1`: Linux VA-API through the system libva
  (`tools/vashim.c`, `tools/build-stream-deps.sh`); ctests `vashim_*`.
- `575a95ea`: macOS FFmpeg SecureTransport non-blocking patch
  (`tools/ffmpeg-securetransport-nonblock.patch`), stream failure reasons,
  reconnect count reset after a minute live; ctest `streaming_rtmps`.
- `6399faa6`, `cccffed4`: Windows streaming speed (`src/video/rgbascale`,
  `src/client/streamcapture_d3d`, high-resolution waitable timer); ctests
  `streaming_pacing`, `rgbascale`.
- `a12dce10`, `62295650`, `82b3b59d`: hardware cursor kept while streaming,
  cursor stamped into the stream (`src/video/streamcursor`), Options cursor
  note; ctests `streamcursor`, `streaming_cursor`, `streaming_capture_cursor`.
- `f1fd241c`, `cc164d74`, `061af064`, `679f00e1`, `c2526145`: Windows Auto
  renderer OpenGL first, then Direct3D 11, then Direct3D 9
  (`src/client/renderdriver.h`, `src/client/main.cpp`); ctest `renderdriver`
  in all three CI builds.
- `d9389129`, `6c84dee1`: render-study fullscreen-toggle and periodic-capture
  hooks (debug); `screenshot()` reads an explicit rectangle.
- `9daeb3f2`, `b1d133a3`: integration (streaming capture table;
  `vashim_test` links `streamcursor.cpp` and `rgbascale.cpp`).

TODO-LEAD: the preparation commits on top of `b1d133a3` (this docs branch
`task-091-docs`: CMake `VERSION 0.9.1`, CHANGELOG date, notes, this skeleton,
version lines; screenshot branch if any; the commit filling this report), and
whether a `git fetch` before landing showed `origin/main` unmoved.

`git diff --stat v0.9.0..b1d133a3 -- src` touches 32 files: `src/ai/`
(`ai.cpp`, `ai.h`, `classicbrain.cpp`), `src/client/` (`cursorpath.h`,
`cursors.cpp`, `cursors.h`, `gameview.h`, `gameview_hud.cpp`,
`gameview_impl.cpp`, `gameview_net.cpp`, `main.cpp`, `options.cpp`,
`renderdriver.h`, `streamcapture_d3d.cpp`, `streamcapture_d3d.h`,
`streaming.cpp`, `streaming.h`), `src/net/` (`client.cpp`, `client.h`,
`pacing.h`, `protocol.h`), `src/sim/` (`legion.cpp`, `retailpark.h`,
`retailpiecepose.h`, `sim.cpp`, `sim.h`) and `src/video/` (`rgbascale.cpp`,
`rgbascale.h`, `stream.cpp`, `stream.h`, `streamcursor.cpp`,
`streamcursor.h`). Nothing under `src/server/`, `src/tnt/`, `src/campaign/`,
`src/net/replayhdr.h`, `src/net/crusades.h` or `src/client/settings.*`
changed. TODO-LEAD: re-run this stat on the final tag.

**Compatibility.** `kNetVersion` is **295** in `src/net/protocol.h` (294 at
`v0.9.0`). `kReplayFormat` is **15** in `src/net/replayhdr.h`, unchanged. The
map generator's `formatVer` **9** (`src/tnt/mapgen.h`), the campaign payload's
`kVersion` 4 (`src/net/crusades.h`) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`) are unchanged.

- **Multiplayer.** `Server::handshake` (`src/server/server.cpp`) rejects a
  Hello whose protocol differs from `kNetVersion` with "protocol version
  mismatch (server 295, client 294) -- update your build"; the release string
  is never compared. So **0.9.1 plays with no 0.9.0 or older client or
  server**.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  format is 1..15 and its protocol equals `kNetVersion`, so 0.9.1 refuses every
  0.9.0 (protocol 294) replay; Crusades battle replays recorded by a 0.9.0
  server are shown as *Recorded replay version unsupported*.
  `tools/replay_test.cpp` pins `kNetVersion == 295` and refuses format 15
  under protocol 294.
- **Maps, mods and settings.** No settings key, map, recipe or package format
  changed. The Windows Auto renderer reads the same `renderer` key; a saved
  explicit choice is used as before.

TODO-LEAD: mixed-version check (none planned; say so).

## Local preparation checks

TODO-LEAD: fill from the local-checks agent (`~/tak-tmp/scratch/release-091/`).
Host, cores, build (`build-o2`, `-O2 -g`, ccache + mold, `TAK_TEST_DATA` =
retail install), tree sha, `takclient --version`.

- **Full suite.** TODO-LEAD: one full `ctest` (`-LE nightly`) result and log
  path. The integration's `ctest -L quick` was 402/403 with `vashim_unavailable`
  failing only because the local `third_party/ffmpeg-bink` predates the shim;
  on a build against the shim prefix (`build-o2-va`) `vashim_unavailable`,
  `vashim_system` and `vashim_system_loader` passed and `takclient` contained
  no `__vaDriverInit_`. Data tests run directly at integration: `legion_park`
  39 cases / 0 failures, `shipyard_park` 106/0, `production_exit` 268/0,
  `sweetspot` PASS. Streaming tests (14) all passed.
- **Detmath guard** (`tools/check-detmath.sh`): TODO-LEAD (OK at integration).
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): TODO-LEAD
  (golden `dcef618cd2e4d558` at integration, same as 0.9.0; aarch64 skipped
  locally).
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`,
  clean scratch HOME/XDG_DATA_HOME per game, `TAK_LEGION=1` for Legion,
  `TAK_AI_LEVEL=6` for the Retail AI), each run twice. At integration
  (`b1d133a3`):

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `d375d03a6c472773` (unchanged) | `a470242be92a0e14` (0.9.0: `b69b4e0842ba4eef`) |
| Legion | `426ab0842b709c28` (unchanged) | `a65a48d7a63825ae` (0.9.0: `0da23f54df7a1b9d`) |
| Retail AI (`TAK_AI_LEVEL=6`) | `c5846d769e0a7d93` (unchanged) | not run |

  TODO-LEAD: confirm on the final tree (units, `err=none end=timelimit`, no
  desync or referee-suspect line, log path).
- **Release build.** TODO-LEAD: vendored static libraries, `ldd`, `--help`
  listing only `--data`, `--play-map`, `--game-mod` and `--version`;
  `--version` reads `0.9.1`.
- **Compatibility constants**: as under *Scope and compatibility*.

Failures: TODO-LEAD.

## Screenshots

TODO-LEAD: which pictures were retaken for 0.9.1 (at least `title.jpg`, which
reads VERSION 0.9.0, and the Options pictures if the Renderer or Hardware
Cursor rows changed: `options-graphics.jpg` shows RENDERER AUTO), by which
build, and the `docs/img/README.md` rows updated; or that none were and why.
No image was edited by the docs branch.

## Remote multiplayer sweep

TODO-LEAD: the 37-scenario table for both pathfinding modes
(`tools/desync-hunt-remote.sh`, referees on `tak.pgnet.us` and
`vpn3.pgnet.us`, isolated binaries and ports, the public `takserver` on
port 7677 untouched), the three `--validate` gates, replay headers (format 15,
protocol 295, engine `0.9.1`), checkpoint counts and mismatches, referee
binary SHA-256 and `--version`.

### Reference ticks and hashes (0.9.1)

TODO-LEAD: table. Hashes are not comparable with 0.9.0 (protocol 295 changes
aiming and factory output); compare end, tick and units with the 0.9.0 table
in [release-0.9.0-validation.md](release-0.9.0-validation.md#reference-ticks-and-hashes-090).

### Comparison with 0.9.0

TODO-LEAD: which cases moved and why (aiming, factory PARK, flyer PARK,
Legion spots), autoplay and load-sensitive noise, repeats.

## CI and packages

TODO-LEAD: main CI on the final sha (checked once), tag CI runs (Determinism
dispatched on the tag, Linux, Windows, macOS with Sign + notarize), reruns and
flakes, the 29 assets (digests, sizes, build ids `v0.9.1`), dependencies
(Debian/Ubuntu `libc6` only; no libva, libdrm, libsecret or GLib `NEEDED`; Arch
`optdepend` libva), Authenticode on oden-win, notarization on the MacBook,
packaged server running the Retail AI, packaged `--help`, mods folder, streaming
muxer, Opus static, licences, microphone and Retina plist keys.

New checks for 0.9.1:

- TODO-LEAD: no shipped Linux `takclient` contains a libva driver loader
  (`__vaDriverInit_`); CI's "no bundled libva loader" step result.
- TODO-LEAD: macOS `streaming_rtmps` and `stream_network_test` in tag CI.
- TODO-LEAD: `renderdriver` ran in the Linux, macOS and Windows CI builds.

`--mpai` on Windows and macOS: TODO-LEAD (oden-win MSYS2 MINGW64 GCC and the
arm64 MacBook, optimized Debug builds of the tag; the five games above, each
matching the Linux table).

TODO-LEAD: publication line (`gh release edit v0.9.1 --draft=false --latest`,
time, asset count). 0.9.0 and later releases are kept.

## Limits

- No game mixing 0.9.1 with 0.9.0 or older is possible: protocol 295 is refused
  by every older server and client. TODO-LEAD: no mixed-version check was run.
- New (see [the notes](release-0.9.1-notes.md#known-limits)): Legion factory
  spots (one Catapult of 100 at 20× held behind the Keep, neighbour door lanes
  not avoided, all-16-layers fallback); the 256-cell pond threshold and the
  farthest-reachable fallback are ours; the SweetSpot query is slower than
  0.9.0's and has no large-battle benchmark; no engine cause found for the
  hardware cursor that "feels like software"; Direct3D 9 capture still waits
  for the GPU and Metal/software read back the whole window; the Windows
  renderer trade-off was measured on one NVIDIA machine.
- Local only: `vashim_unavailable` fails against a `third_party/ffmpeg-bink`
  built before the shim; CI builds FFmpeg with it.
- Not in any CI selection (their branches never added them): `legion_park`,
  `rgbascale`, `streamcursor`, `streaming_pacing`, `streaming_cursor`.
- All of 0.9.0's limits carry over (see
  [release-0.9.0-validation.md](release-0.9.0-validation.md#limits)), except
  the mixed-version notes, which now concern 0.9.0.
- Tag CI: TODO-LEAD.
