# 0.9.1 release validation

Protocol **295** (was 294 in 0.9.0), replay format **15** (unchanged),
generator version **9**, campaign payload **4**, database schema **9**.

Tagged `v0.9.1` at `41b2b81f` (full `41b2b81fee07a96d1609e3a52810de853f034aca`, annotated tag object `a8f15c7b`, "TAK Engine 0.9.1") and published on 2026-10-25 21:33:11 UTC.

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

Preparation commits on top of `b1d133a3`, cherry-picked linearly (no code
changes): `0865310e` (screenshots: `title.jpg`, `streaming.jpg`,
`guide/options-graphics.jpg`, `docs/img/README.md`, the user-guide caption),
`dcaaab0f` (CMake `VERSION 0.9.1`, CHANGELOG dated, these notes and this
report's skeleton, version lines), `377e2f5d` (docs review: renderer label as
drawn, 0.9.0 stays downloadable, screenshots recorded) and the commit filling
this report. A `git fetch` before landing showed `origin/main` unmoved at
`b1d133a3`. `git diff --stat b1d133a3..` over these commits touches only
`CMakeLists.txt` (the version line), `CHANGELOG.md`, `README.md`, nine text files
under `docs/` and the three images.

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
changed. The same stat over the prepared tree (`v0.9.0..` the report commit)
is unchanged: 32 files, the preparation commits touch nothing under `src/`.
On the final tag, `git diff --stat v0.9.0 v0.9.1 -- src` gives the same 32
files (1,998 insertions, 171 deletions).

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

No mixed-version check was run: no 0.9.0 client joined a 0.9.1 server (or the
reverse), and no 0.9.0 replay was opened on 0.9.1. The points above rest on the
source and `replay_test`.

## Local preparation checks

These ran on Linux, cores 4-23, with an optimized Debug build (`build-o2`, `-O2 -g`, ccache + mold, `TAK_TEST_DATA` = the retail install). The build was made fresh in a detached worktree at `b1d133a3`, all targets; `takclient --version` and `takserver --version` report `0.9.0 (build v0.9.0-31-gb1d133a36268)`. The version string still says 0.9.0 because `CMakeLists.txt` `project(... VERSION 0.9.0)` is bumped in the docs commit, not in this code. A Release build with the vendored static libraries was also configured fresh from the same worktree (`~/tak-tmp/scratch/release-091/build-rel`, all targets): its `takclient --version` and `takserver --version` also read `0.9.0 (build v0.9.0-31-gb1d133a36268)`, `ldd takclient` lists only libc, libm and the loader, and `--help` lists only `--data`, `--play-map`, `--game-mod` and `--version`. By the user's rule (minimal testing), no Release, `-O0` Debug, Clang or ASan suite was run, and no repeat runs beyond the reruns below.

- **Full suite.** The one full `ctest` run on `-O2` at `b1d133a3` (`-LE nightly -j16`, scratch HOME/XDG_DATA_HOME) passed **622/625, with 1 skipped and 2 failed** (`rc=8`, 527 s real). 787 tests are defined; the 164 `nightly`-labelled ones were excluded. Neither failure is a product fault:
  - **`vashim_unavailable`** (0.05 s, `FAIL: no display on a render node without libva`) is local only. The local `third_party/ffmpeg-bink` is still the old build with libva 2.22 bundled statically (both local takclients still contain the `__vaDriverInit_%d_%d` loader string), so the shim this test checks is not linked. CI builds FFmpeg with `tools/vashim.c`. The integrator ran a separate build against the shim-built prefix, where `vashim_unavailable`, `vashim_system` and `vashim_system_loader` pass and takclient has no `__vaDriverInit_`. `vashim_system_loader` is the one skipped test (exit 77, same cause).
  - **`voice`** (20.9 s, not a timeout) failed one of 243 checks: `another player's VOLUME leaves this talker alone: 0.510864`. Run again alone it failed the same way (0.520471). The cause was this worktree's configure, not the code: it set `-DCMAKE_CXX_FLAGS=-O2` but left `CMAKE_C_FLAGS` empty, so the C libraries, Opus among them, were built at `-O0`. The integrator's `build-o2` at the same `b1d133a3` has `CMAKE_C_FLAGS=-O2`, and its `voice_test` passes 243/243. So do the Release `voice_test` binaries at `b1d133a3` and at 0.9.0. After reconfiguring with `-DCMAKE_C_FLAGS=-O2` (153 C objects rebuilt, no C++ objects), `voice` passed when run alone. No source changed: the only file `voice_test` builds that differs from `v0.9.0` is `src/client/options.cpp` (renderer label, cursor note).

  The tests for this release's changes passed inside the run: `sweetspot`, `legion_park`, `shipyard_park`, `production_exit`, `server_speed_pacing`, `streaming`, `streaming_overload`, `streaming_native`, `streaming_pacing`, `streaming_rtmps`, `streaming_cursor`, `streaming_capture`, `streaming_capture_cursor`, `stream_key`, `rgbascale`, `streamcursor`, `renderdriver`, `vashim_system`, `replay` and `replay_file`. `mod_transfer`, the one failure at 0.9.0, now passes (19.5 s). Logs: `~/tak-tmp/scratch/release-091/ctest.out`, `ctest-voice-rerun.out`, `ctest-voice-cflags-rerun.out`.
- **Detmath guard** (`tools/check-detmath.sh`): OK. The sim path has no direct libm transcendentals and no `std::atomic_ref`.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): g++ and clang++ at -O0/-O2/-O3 all give `dcef618cd2e4d558`, the same golden as 0.9.0. The aarch64 legs were SKIPPED because there is no target libc or headers locally; the macOS CI leg covers ARM64. Log: `~/tak-tmp/scratch/release-091/local-check-determinism.log`.
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`, a clean scratch HOME/XDG_DATA_HOME per game, `TAK_LEGION=1` for Legion, `TAK_AI_LEVEL=6` for the Retail AI; one run each, on the rebuilt `build-o2`). All five games ended `err=none end=timelimit`, and no log has a desync or referee-suspect line.

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `d375d03a6c472773` (4 units) | `a470242be92a0e14` (16 units) |
| Legion | `426ab0842b709c28` (4 units) | `a65a48d7a63825ae` (18 units) |
| Retail AI (`TAK_AI_LEVEL=6`) | `c5846d769e0a7d93` (4 units) | not run |

Every hash matches the integration runs at `b1d133a3`, each of which ran twice. Compared with `2fa90510` (main before the six 0.9.1 branches landed), only Legion 300 changed (`c36f45f69e9248fd` to `a65a48d7a63825ae`): Legion factory outputs now spread over the ring. The Retail hashes and the 60 s gates are unchanged. All differ from 0.9.0's, as they should: the sim changed under protocol 295 (the drawn-bounds SweetSpot, the factory rally-else-PARK exit for ground units and flyers, and the AI's refusal of pond shipyards). The Retail AI server again logged `game 1: DEFAULT AI profile ai/DEFAULT.txt from the base data`; this is log wording only, as before. Logs: `~/tak-tmp/scratch/release-091/local-mpai/`.

- **Compatibility constants** (checked at `b1d133a3`): `kNetVersion` **295** (`src/net/protocol.h`, was 294 in `v0.9.0`). `kReplayFormat` is still **15** (`src/net/replayhdr.h`). `tools/replay_test.cpp` pins `kNetVersion == 295` and `kReplayFormat == 15`, and checks that format 15 under protocol 294 is refused. Unchanged since `v0.9.0`: map generator `formatVer` 9 (`src/tnt/mapgen.h`), campaign `kVersion` 4 (`src/net/crusades.h`), `kSchemaVersion` 9 (`src/server/crusades/store.cpp`).
- **How versions are compared** (source check for the notes):
  - *Joining a server.* `Server::handshake` (`src/server/server.cpp`) rejects a Hello when `ver != kNetVersion` ("protocol version mismatch"), or when the client's pure-retail gameplay-data hash differs from the referee's. `tak::kVersion` is never compared. So **0.9.1 does not play with 0.9.0 or any older client or server**. A 0.9.0 (protocol 294) client is refused by a 0.9.1 server, and a 0.9.1 client is refused by a 0.9.0 server. Public-server operators must update `takserver` along with their players.
  - *Replays.* A replay loads only when `supportedReplayProtocol(format, protocol)` holds (`1 <= format <= 15` and `protocol == kNetVersion`). 0.9.0 replays carry protocol 294, so **0.9.1 cannot play 0.9.0 replays**, and 0.9.0 cannot play 0.9.1's (also format 15, but protocol 295).

**No mixed-version check was run.** No 0.9.0 client joined a 0.9.1 server (or the reverse), and no 0.9.0 replay was opened on 0.9.1. The points above rest on the source and `replay_test`.

**Real failures: none.** Two tests failed in the full run, both from the local setup, and neither was fixed in code. `vashim_unavailable` failed because the local FFmpeg prefix bundles libva and lacks the shim, which CI does build. `voice` failed because this worktree's C code was built at `-O0`; it passes once the C code is built at `-O2`.

**On the prepared tree** (`377e2f5d`, the three preparation commits on
`b1d133a3`; only the CMake version line differs from the code checked above):
all targets built in a fresh `build-o2` (Debug `-O2`, C and C++), and
`ctest -L quick` passed 402/403, the one failure being the local-only
`vashim_unavailable` above (`vashim_system_loader` skipped, same cause). A
fresh Release build with the vendored static libraries (all targets) reports
`takclient (TAK engine) 0.9.1` and `takserver (TAK engine) 0.9.1`; `ldd
takclient` lists only libc, libm and the loader, and `--help` lists only
`--data`, `--play-map`, `--game-mod` and `--version`.

## Screenshots

Three pictures were retaken on branch `task-091-shots` (`a4d43808`, on
`b1d133a3`) by the 0.9.1 build (Debug `-O2`, CMake `VERSION 0.9.1` set locally),
with scratch profiles and the offscreen driver: `title.jpg` (VERSION 0.9.1),
`guide/options-graphics.jpg` (RENDERER **Auto (OpenGL)**, HARDWARE CURSOR note
**OS CURSOR 16X25**; user-guide alt text and caption updated) and
`streaming.jpg` (idle panel, unchanged apart from the version line behind it).
`docs/img/README.md` has a "Retaken for 0.9.1" note and the three rows. The new
stream failure and reconnect reasons appear only after a real start against
YouTube, so no picture shows them. No other picture's scene or caption depends
on a 0.9.1 change. No image was edited by the docs branch.

The screenshot commit landed on main as `0865310e` (cherry-picked from
`a4d43808`).

## Remote multiplayer sweep

The full 37-scenario table passed for both pathfinding modes, Retail and Legion, on **2026-10-25** (UTC). Both ran at `b1d133a3`, the 0.9.1 integration head of `main` (protocol **295**, replay format **15**, build id `v0.9.0-31-gb1d133a36268`; the CMake project version is still 0.9.0 at this sha, so the binaries and replay headers say engine `0.9.0`). The preparation commits after `b1d133a3` change only documentation, images and the CMake version line, so the swept simulation, network and server code is the released code. Each mode passed in a single pass with no retries. No seat was lost and no `stopped responding` line appeared. The only `held the game` lines are in two Retail stress cases (see Seat budget).

- **Retail:** 18:59-19:17 UTC (17 minutes including the gates), single pass.
- **Legion:** 19:17-19:44 UTC (27 minutes including the gates), single pass.
- **Repeats:** afterwards, four Retail and four Legion cases were repeated to check that the new references reproduce (below).

Referees ran on `tak.pgnet.us` (24 runs, 10 at a time) and `vpn3.pgnet.us` (13 runs, 6 at a time), with optimized local clients pinned to CPUs 4-23.

- **Isolation:** each host ran its own referee binary, `~/takserver.sweep091`. Each mode had its own replay and map-cache folders on the remotes. The ports were:
  - Retail: 8690/8691 and 8700+.
  - Legion: 8790/8791 and 8800+.
  - Repeats: 8900+ and 9000+.

  The public `takserver` on `tak.pgnet.us` (port 7677) was not touched: it was the same process (PID 75501, started 03:41 UTC) before and after. Every file the sweep put on either remote has been removed (binary, replay and cache folders, `/tmp` server logs).
- **Duration:** each scenario allowed ten simulated minutes (18,000 ticks) or ended at an actual result for that seat.
- **Coverage:** real seated players provide the client/referee checksum coverage. The two all-AI spectator scenarios per mode (`w-allai-stress`, `w-allai-bench`) test flow control, not hashes.
- **Mode:** chosen through the hosting client's room options (`TAK_LEGION=1` creates a Legion room). `TAK_WALL_TIMEOUT=3600` was set for both modes.
- **Replay headers:** every recorded referee replay header confirms the intended mode, replay format 15, protocol 295 and engine `0.9.0` (the unbumped project version). There are 37 headers per mode, seeds 2000-2036, each mapped to its case.

| Mode | Completed scenarios | Completed seats | Client-pair checkpoints | Client-referee checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Retail | 37/37 (single pass) | 52/52 | 11,621 across 22 seat pairs, 0 mismatched | 2,685 across 51 seat/referee comparisons, 0 mismatched |
| Legion | 37/37 (single pass) | 52/52 | 10,445 across 22 seat pairs, 0 mismatched | 2,706 across 51 seat/referee comparisons, 0 mismatched |

Each run passed all three `--validate` gates before its table started:

- **Build identity:** `both at v0.9.0-31-gb1d133a36268`.
- **Planted desync:** the referee reported `game 1: client 1 DESYNCED at tick 900`.
- **Game mod transfer:** `verified game mods transferred and incorrect loaded gameplay rejected`.

**Referee binary.** The gate checks only `tak.pgnet.us`. The binary on `vpn3.pgnet.us` is the same file: SHA-256 `75cd7d7d39ac9a3ea9f89bf75fcce1eb78ead4a3d5b920c797e6d83a85253522`, `--version` `0.9.0 (build v0.9.0-31-gb1d133a36268)` on both. The referee was built from this exact source in the Ubuntu 24.04 container, as Debug `-O2 -g`, and needs at most GLIBC 2.38. The client is a Debug `-O2 -g` build of the same sha.

**No desyncs.** No natural `DESYNCED`, `REFEREE SUSPECT` or client-reported desync appeared in any case log; the only such line in each run is the planted one. Every seated client ended `err=none` (52 per mode). Both sweeps ended with "no desyncs reported (52 seats, all completed)", and their lists of runs that did not complete were empty. The repeats ended with "no desyncs reported (4 seats, all completed)" for Retail and "(5 seats, all completed)" for Legion.

**What was compared.** A state hash is compared at every checkpoint, and the position digest as well wherever both sides carry one. Every game wrote a referee replay (37 per mode, matched to cases by the seed in the replay header).

**Harness unchanged.** `tools/desync-hunt-remote.sh` is unchanged since 0.8.8 (SHA-256 `df09e28844be1f399f9466a62e19c9cdf53298e5fdb9eb5dcefb5fca830ad56e`), and so is the checkpoint comparison script (SHA-256 `ebdd8dc6…7eafc4`). As before, the harness ran with the short `TMPDIR` `/home/pocket_geek/tk`, because a longer path overflows the ssh ControlPath limit.

**Seat budget.** No seat was lost in either mode. Legion charged no wait time at all. In Retail, the two 9,000-unit cases charged a little of their 30-second wait budget, all of it recovered:

- `2h-stress`: slot 0 held the game six times, 1.0-2.4 s each.
- `2h-orders-stress`: slots 0 and 1 held it four times, 1.0-2.2 s each.

That is the same load-sensitive class as 0.8.10's lost `2h-stress` seat, but well short of it. Local load was about 10-14 when Retail started (the sweep builds had just finished) and about 5-7 for the rest of the sweep. The end-of-game `slot N disconnected -- waiting` lines in every log are the headless clients quitting after the game ended; they are not stalls.

### Reference ticks and hashes (0.9.1)

Hash and units are the seated host's final line.

**Hashes and outcomes are not comparable with 0.9.0.** 0.9.1 changes hashed simulation state in almost every battle:

- hunter and drawn-bounds SweetSpot aiming, and the faster SweetSpot query;
- the AI refusing pond shipyards;
- the retail factory rally-else-PARK exit, for ground units and flyers;
- Legion factory outputs spread over the ring.

So 72 of 74 hashes differ from 0.9.0 by construction (only Legion `h-two-castles` kept its hash). The "vs 0.9.0" column records end, tick and units against the [0.9.0 table](release-0.9.0-validation.md#reference-ticks-and-hashes-090) (same harness, hosts and seeds) for reference only. Only two rows match: Retail `4h-orders` (autoplay, by chance) and Legion `h-two-castles`. Whether the new numbers are stable is answered by the repeats below, not by this column.

**Expected noise.** The six `TAK_AUTOPLAY` cases never repeat a result. `h-lake`, `h-stress`, `2h-stress`, `2h-orders-stress` and `4h-gods` are load-sensitive.

| Case | Retail end | Retail tick | Retail hash | Retail units | vs 0.9.0 | Legion end | Legion tick | Legion hash | Legion units | vs 0.9.0 |
| --- | --- | ---: | --- | ---: | --- | --- | ---: | --- | ---: | --- |
| `h-baseline` | timelimit | 18,000 | `d16a84eac95a9386` | 188 | differs (units 210 -> 188) | timelimit | 18,000 | `5ff4101217521641` | 195 | differs (units 213 -> 195) |
| `h-gods` | concluded | 14,197 | `b9f38b6a587bcd3f` | 75 | differs (tick 14,626 -> 14,197; units 104 -> 75) | concluded | 13,445 | `a5072c5170f4e52e` | 59 | differs (tick 15,383 -> 13,445; units 82 -> 59) |
| `h-crusades` | timelimit | 18,000 | `dc8cef16a8775019` | 216 | differs (units 245 -> 216) | timelimit | 18,000 | `d5bf78fb4cf563c9` | 211 | differs (units 230 -> 211) |
| `h-stress` | concluded | 5,200 | `c44a9b764aa57349` | 8,789 | differs (tick 4,891 -> 5,200; units 8,596 -> 8,789) | concluded | 4,719 | `88965f7f4aaa6988` | 7,910 | differs (tick 5,147 -> 4,719; units 7,305 -> 7,910) |
| `h-absurd` | timelimit | 18,000 | `8313c7cab0579aee` | 463 | differs (units 454 -> 463) | timelimit | 18,000 | `fe455d3758be9ce5` | 421 | differs (units 471 -> 421) |
| `h-fog-explored` | timelimit | 18,000 | `953b84f8fea5c4c7` | 193 | differs (units 188 -> 193) | timelimit | 18,000 | `89259667e97a10a9` | 180 | differs (units 142 -> 180) |
| `h-fog-full` | timelimit | 18,000 | `708a8a7ba421de4c` | 226 | differs (units 216 -> 226) | timelimit | 18,000 | `85b5aa87986b7f33` | 226 | differs (units 217 -> 226) |
| `h-unitcap` | timelimit | 18,000 | `eba8f1cf0b1f5fae` | 226 | differs (concluded 14,780 -> timelimit 18,000; units 204 -> 226) | timelimit | 18,000 | `4478cdc06aa549d5` | 221 | differs (units 220 -> 221) |
| `h-cramped` | concluded | 1,359 | `26f0ec623793ebf7` | 9 | differs (tick 1,307 -> 1,359; units 10 -> 9) | timelimit | 18,000 | `ec6893bcef8191d1` | 37 | differs (units 29 -> 37) |
| `h-naval` | timelimit | 18,000 | `e020ac56d13e55a4` | 92 | differs (units 58 -> 92) | concluded | 15,169 | `9182ec691d9cd35d` | 73 | differs (timelimit 18,000 -> concluded 15,169; units 88 -> 73) |
| `h-naval-crus` | timelimit | 18,000 | `10c224efb69e8aaa` | 74 | differs (units 70 -> 74) | concluded | 16,487 | `498fabe001b6d2f4` | 93 | differs (timelimit 18,000 -> concluded 16,487; units 88 -> 93) |
| `h-lake` | concluded | 4,242 | `71d7f4596b6afd9f` | 4,600 | differs (tick 3,923 -> 4,242; units 4,893 -> 4,600) | concluded | 4,378 | `ab0c08248a499a84` | 4,653 | differs (tick 4,268 -> 4,378; units 4,905 -> 4,653) |
| `h-random-starts` | concluded | 13,068 | `972b2ec655faa614` | 189 | differs (tick 15,702 -> 13,068; units 228 -> 189) | concluded | 13,051 | `f7877445b1beb8dc` | 215 | differs (tick 12,289 -> 13,051; units 194 -> 215) |
| `h-monarch-exp` | concluded | 11,761 | `6fc631cced40e773` | 88 | differs (tick 12,231 -> 11,761; units 89 -> 88) | concluded | 11,753 | `cf7b3e04f15ae3ba` | 60 | differs (tick 17,804 -> 11,753; units 82 -> 60) |
| `h-mods-all` | timelimit | 18,000 | `c166ac0ae1694e66` | 172 | differs (units 209 -> 172) | timelimit | 18,000 | `060fa66ca90f340b` | 199 | differs (units 212 -> 199) |
| `h-everything` | timelimit | 18,000 | `0dd58b6999a0a9a5` | 5,246 | differs (units 4,985 -> 5,246) | timelimit | 18,000 | `aa648d4ee947e05c` | 4,997 | differs (units 5,167 -> 4,997) |
| `h-speed-1x` | timelimit | 18,000 | `6259f27ff66a4e82` | 202 | differs (units 236 -> 202) | timelimit | 18,000 | `0fce78c794a4deae` | 245 | differs (units 246 -> 245) |
| `h-two-castles` | timelimit | 18,000 | `7c23b583a6f8f885` | 27 | differs (units 23 -> 27) | timelimit | 18,000 | `b200d4fe20647a7f` | 4 | same (hash too) |
| `2h-baseline` | timelimit | 18,000 | `646a0f3f9e117fa1` | 146 | differs (units 173 -> 146) | timelimit | 18,000 | `cfc26d4c0568a62e` | 181 | differs (units 172 -> 181) |
| `2h-gods` | concluded | 12,434 | `9a79a416401a4529` | 60 | differs (tick 12,423 -> 12,434; units 104 -> 60) | concluded | 16,301 | `eaaa6b4c280f95ff` | 81 | differs (tick 13,059 -> 16,301; units 61 -> 81) |
| `2h-crusades` | timelimit | 18,000 | `3b4be18a68aaa3e8` | 195 | differs (units 182 -> 195) | timelimit | 18,000 | `197c2f97456519d5` | 193 | differs (units 167 -> 193) |
| `2h-stress` | concluded | 5,109 | `f1b14559c59b0612` | 9,285 | differs (tick 5,048 -> 5,109; units 9,102 -> 9,285) | concluded | 5,197 | `0445cb01ddcbd93e` | 8,564 | differs (tick 5,248 -> 5,197; units 8,427 -> 8,564) |
| `2h-orders` | timelimit | 18,000 | `5399d70734e8434b` | 195 | differs (units 181 -> 195) (autoplay) | timelimit | 18,000 | `fbae9936a5597a86` | 202 | differs (units 192 -> 202) (autoplay) |
| `2h-orders-stress` | concluded | 5,076 | `de9c01686fae61d4` | 9,386 | differs (tick 5,063 -> 5,076; units 9,008 -> 9,386) (autoplay) | timelimit | 18,000 | `ab092138c8f1fac4` | 5,237 | differs (concluded 5,401 -> timelimit 18,000; units 8,083 -> 5,237) (autoplay) |
| `4h-orders` | timelimit | 18,000 | `27b2b9a0db0fae39` | 161 | same (autoplay) | timelimit | 18,000 | `30600c5df8ea00b1` | 148 | differs (units 166 -> 148) (autoplay) |
| `3h-baseline` | timelimit | 18,000 | `8193a9be35cb6029` | 179 | differs (units 172 -> 179) | timelimit | 18,000 | `c032e19f3b96f55e` | 162 | differs (units 148 -> 162) |
| `4h-gods` | timelimit | 18,000 | `2d9bfb3fd4315b08` | 119 | differs (units 149 -> 119) | timelimit | 18,000 | `c07f7acd69c4b020` | 144 | differs (units 129 -> 144) |
| `net-rtt50` | timelimit | 18,000 | `29ca201931bc8d72` | 219 | differs (units 230 -> 219) | timelimit | 18,000 | `da8d07814c6c0929` | 238 | differs (units 227 -> 238) |
| `net-rtt100` | timelimit | 18,000 | `c37e0f011bf91bac` | 173 | differs (units 190 -> 173) | timelimit | 18,000 | `4651546f79fe7dc2` | 180 | differs (units 204 -> 180) |
| `net-jitter` | timelimit | 18,000 | `456eca08e6bcf105` | 185 | differs (units 173 -> 185) | timelimit | 18,000 | `f8cc949398208181` | 207 | differs (units 196 -> 207) |
| `net-rtt500` | timelimit | 18,000 | `64a66cd2c2ca0ede` | 224 | differs (units 219 -> 224) | timelimit | 18,000 | `d0913a8b6141ca90` | 185 | differs (units 200 -> 185) |
| `net-loss1` | timelimit | 18,000 | `95cbd0ea376adf51` | 217 | differs (units 192 -> 217) | timelimit | 18,000 | `9e8b6a05e1844394` | 224 | differs (units 206 -> 224) |
| `net-loss3-orders` | timelimit | 18,000 | `0eb1105e77270ed1` | 208 | differs (units 188 -> 208) (autoplay) | timelimit | 18,000 | `a5f47f0f43fbe7fc` | 205 | differs (units 222 -> 205) (autoplay) |
| `net-orders100` | timelimit | 18,000 | `7fe4222e6765b74f` | 210 | differs (units 185 -> 210) (autoplay) | timelimit | 18,000 | `0250dd5daf2fe2c6` | 179 | differs (units 193 -> 179) (autoplay) |
| `net-2h-rtt100` | timelimit | 18,000 | `54fc6c29cb6818ce` | 161 | differs (units 184 -> 161) (autoplay) | timelimit | 18,000 | `432a065d745fa6ff` | 155 | differs (units 195 -> 155) (autoplay) |
| `w-allai-stress` | timelimit | 18,000 | `ef336bdaa84e3a19` | 6,139 | differs (units 5,392 -> 6,139) | timelimit | 18,000 | `69e40be4f7ae2819` | 3,370 | differs (units 3,925 -> 3,370) |
| `w-allai-bench` | timelimit | 18,000 | `96f9c1880fa77263` | 368 | differs (units 312 -> 368) | timelimit | 18,000 | `bbb79914b478246a` | 419 | differs (units 344 -> 419) |

### Comparison with 0.9.0: the new references reproduce

Nearly every case moved, so instead of explaining individual rows, eight cases with no autoplay and no load sensitivity were run a second time on the same hosts. Each one gave exactly the same end, tick, hash and units as in the first pass:

- **Retail:**
  - `h-baseline` (`d16a84eac95a9386`, 188 units)
  - `h-gods` (concluded 14,197, `b9f38b6a587bcd3f`)
  - `h-naval` (`e020ac56d13e55a4`, 92 units)
  - `h-everything` (`0dd58b6999a0a9a5`, 5,246 units)
- **Legion:**
  - `h-baseline` (`5ff4101217521641`, 195 units)
  - `h-naval` (concluded 15,169, `9182ec691d9cd35d`)
  - `2h-gods` (concluded 16,301, `eaaa6b4c280f95ff`)
  - `h-everything` (`aa648d4ee947e05c`, 4,997 units)

These include the naval cases, which test the pond-shipyard and factory-exit changes, and the case that fields every unit type. The moves are consistent with the 0.9.1 combat, AI and factory-exit changes, but no bisect was done, so they are not proven to come from them.

There is no desync anywhere: every seat agreed with the referee at every checkpoint in every case, in both modes and in the repeats. Logs, replays and checkpoint data: `~/tak-tmp/scratch/release-091/sweep/`.

## CI and packages

Main CI ran once on the integrated code (`b1d133a3`): Determinism, Windows
and macOS passed; Linux packages passed 15 of 16 jobs, and Debian 12 arm64 failed
before building anything (an SSL connect error downloading the Opus source from
downloads.xiph.org) and passed on a rerun of that job. The runs on the prep
commit `41b2b81f` (docs, images and the version only) were cancelled, because
the tag builds the same commit.

On the tag, all four workflows passed on the first attempt, with no reruns:
[Determinism](https://github.com/pocketgeek/tak-engine/actions/runs/40229540844)
(dispatched on the tag; every GCC and Clang x86-64 build agrees on golden
`dcef618cd2e4d558`, unchanged from 0.9.0),
[Linux packages](https://github.com/pocketgeek/tak-engine/actions/runs/40229541104)
(16 distro jobs),
[Windows](https://github.com/pocketgeek/tak-engine/actions/runs/40229541114)
(build, sign and publish) and
[macOS](https://github.com/pocketgeek/tak-engine/actions/runs/40229541127)
(build, sign and notarize).

All 29 assets were downloaded and match GitHub's SHA-256 digests. Every binary
that can run here reports `0.9.1 (build v0.9.1)`; the Arch client needs Arch's
newer glibc (`GLIBC_2.44`) and does not start on this Fedora machine, as with
0.9.0, while its server reports the same version. The Linux binaries link only
libc and libm (plus libdl/libpthread on one older target); none has a libva,
libdrm, libsecret or GLib `NEEDED`. The licence files, including
`STREAMING-LICENSES.txt` with the VA-API headers-only wording, are in every
package. No `mods/`, `overrides/` or `click.hpi` is shipped. The packaged
Linux `takclient --help` lists only `--data`, `--play-map`, `--game-mod` and
`--version`, and the packaged `takserver` runs the Retail AI game to
`c5846d769e0a7d93`, the same as the local table.

Windows: on oden-win, `Get-AuthenticodeSignature` reports **Valid**
(timestamped, "Curtis Edge") for all 9 signed files: the x64 installer and
`takclient`, `takserver`, `cartographer` and `crusades_admin` in both the x64
and arm64 zips. Both report `0.9.1 (build v0.9.1)`.

macOS: on the MacBook, both DMGs and all four apps (from the DMGs and the zips)
pass `stapler validate`, and `spctl` accepts them as "Notarized Developer ID".

New checks for 0.9.1:

- No shipped Linux `takclient` contains a libva driver loader: none of the 18
  Linux clients holds the `__vaDriverInit_` string (0.9.0's did). CI's
  "no bundled libva loader" check passed in all 16 Linux jobs, and
  `vashim_test unavailable` passed in each of them.
- macOS tag CI ran the RTMPS upload test: "PASS: RTMPS upload stayed live;
  wrong certificate refused with its reason".
- `renderdriver` passed in every Linux, Windows and macOS CI job (20 jobs).

`--mpai` on Windows: on oden-win (MSYS2 MINGW64 GCC, optimized Debug build of
the tag, run at 8× with `TAK_SPEED=80`, which gives the same hashes as 1×), all
five games match the Linux table: Retail 60 s `d375d03a6c472773`, Retail 300 s
`a470242be92a0e14`, Legion 60 s `426ab0842b709c28`, Legion 300 s
`a65a48d7a63825ae`, Retail AI 60 s `c5846d769e0a7d93`. The SSH connection to
oden-win dropped twice during the runs; the games it cut off were rerun and
matched. `--mpai` on macOS was not completed: on the MacBook the test server
exited before the games started (tick 0, "peer closed"), a harness problem on
that machine. macOS CI on Apple Silicon ran the determinism checks for this tag
(detmath golden, `fp_contract`, `navigation_determinism`, `sim_driver_equiv`)
and they passed.

Published with `gh release edit v0.9.1 --draft=false --latest` at
2026-10-25 21:33:11 UTC, with 29 assets. 0.9.0 and later releases are kept.

## Limits

- No game mixing 0.9.1 with 0.9.0 or older is possible: protocol 295 is refused
  by every older server and client. No mixed-version check was run.
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
- The local checks and the remote sweep ran at `b1d133a3`, before the CMake
  version bump, so those binaries and the sweep's replay headers report engine
  `0.9.0`; the preparation commits change no code.
- Tag CI: all four workflows passed on the first attempt. `--mpai` on macOS
  was not completed (see CI and packages).
