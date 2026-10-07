# 0.7.25 release validation

Protocol **236**, replay format **11**, generator version **8**, campaign
payload **4**, database schema **9**.

Status: **in preparation.** Sections marked *pending* have not been run or
recorded yet. 0.7.25 supersedes the unpublished 0.7.24; its scope is everything
since the published [0.7.23](release-0.7.23-validation.md).

## Scope and compatibility

Two pathfinding modes ship: Retail (the default, used by campaigns and Crusades
battles) and experimental Legion. Retail+, Flowfield and Cooperative were
removed before release; the wire byte, replay header and saved preference accept
only Retail (0) and Legion (4), and a saved 1–3 falls back to Retail. Retail
behavior changed where the port was corrected against the retail binary
(flyer landing site and airborne grid, group pacing, path-budget class,
same-cell destinations, embarked-cargo exploration). Legion's known limits are
listed in [legion-pathfinding.md](legion-pathfinding.md#known-weaknesses).

Protocol 236 is required for live play. Replays need the exact simulation
protocol, so protocol-221 (0.7.23) recordings are not playable by this build.
Campaign payload 4 and database schema 9 are unchanged since 0.7.23; generator
version 8 replaces 0.7.23's generator 7, whose recipes cannot be regenerated.

*Pending:* confirmation that the tagged build reports protocol 236 from both
client and server, and any further scope notes from the lead.

## Local preparation checks

All checks ran on a clean detached worktree of `3490f5544e65b17a23d796d8fee9937a9c206e91`
(version **0.7.25**, protocol **236**) with the owned retail install as game data.
Toolchains: GCC 16.2.1, Clang 22.1.8, Python 3.14.7.

- **Builds.** Release, Debug, optimized Debug (`-O2 -g`), Clang Debug and
  AddressSanitizer all-target builds completed. Every client and server reports
  `0.7.25 (build v0.7.23-197-g3490f5544e65)` with no `-dirty` suffix; the
  `v0.7.23` base appears because 0.7.24 was never tagged.
- **Full CTest.** Release passed **237/237**. Debug, optimized Debug and Clang
  Debug each passed **249/249**. `legion_acceptance_crowdheld_legion` is the only
  skipped case; it is intentionally disabled (see
  [Legion known weaknesses](legion-pathfinding.md#known-weaknesses)). No test
  timed out or needed a repeat.
- **AddressSanitizer/leak run.** A focused selection of **81** cases ran with
  leak detection, SDL DBus shutdown cleanup and no suppressions. It covers
  Legion and Retail navigation and acceptance, movement orders and Shift-queue,
  flight landing, production and factory exits, transport, settings migration
  and the Bilinear toggle, renderer caches and GUI smoothing, replay, parser
  limits, validation workers, server commands and status, ACME, Crusades UI,
  builder automation and flyer combat. The first run passed 77/81: four Legion
  World fixtures passed a temporary unit type to `World::spawn`, which keeps a
  pointer to it, and ASan reported stack-use-after-scope on the first tick. The
  engine was not at fault. The fixtures were corrected in `3108e23` (test-only),
  after which those four pass under ASan and in Debug. No leaks were reported.
- **Determinism.** `tools/check-determinism.sh`: GCC and Clang x86-64 at O0/O2/O3
  agree on `dcef618cd2e4d558`. The aarch64 emulation legs skip for missing target
  headers; native ARM64 CI is the separate gate. `tools/check-detmath.sh` passes.
- **Headless AI games.** Inner Circle, seed 1, optimized Debug, reproduced the
  expected state hashes: Legion 300 s `ca02108dfc933123`, Retail 300 s
  `b240750e5765c02b`, Legion 60 s `dadf535a50b404e1`, Retail 60 s
  `56cfcbf8ef57181e`.
- **Python research suite.** **165 tests** passed, one optional corpus test
  skipped. The separate owned-retail archive verifier passed all **6 tests**,
  including the corpus; SHA-256 sums of the 16 retail archives and the key file
  are unchanged before and after.
- **Syntax checks.** All **421** Python files compile; all **15** shell scripts and
  `tools/tak-netem` pass `bash -n`.
- **Warnings.** Existing `-Wmissing-field-initializers` and deprecated `u8path`
  warnings remain. GCC 16 Release reports `-Warray-bounds` in Legion lane
  parting (`src/sim/legion.cpp`); the index is capped at the array size just
  before, so it is a false positive.

## Remote multiplayer sweep

The full 37-scenario table passed for both pathfinding modes, with referees on
`tak.pgnet.us` and `vpn3.pgnet.us` and optimized local clients. Each scenario
allows ten simulated minutes or ends at an actual match result.

| Mode | Scenarios | Seats | Client-pair checkpoints | Client-referee checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Retail | 37/37 | 52/52 | 9,815 over 22 pairs, 0 mismatched | 763 over 16 comparisons, 0 mismatched |
| Legion | 37/37 | 52/52 | 10,575 over 22 pairs, 0 mismatched | 1,712 over 32 comparisons, 0 mismatched |

- In each mode, the build-identity check, the planted desync (detected at tick
  900) and override-transfer/loaded-hash rejection passed. No natural desync or
  referee-suspect report appeared. Every recorded replay header shows the
  intended mode, protocol 236 and engine 0.7.25.
- Three dense Legion stress cases (`2h-stress`, `2h-orders-stress`,
  `w-allai-stress`, seeds 2021, 2023 and 2035) exceeded the default 900-second
  real-time deadline while progressing. With a 3,600-second allowance and the
  same seeds they completed in 1,011, 1,533 and 1,072 seconds with all shared
  checkpoints matching. The referee ran near one full core, at roughly 0.4–0.6×
  real time in this `-O2 -g` Debug build; Retail finished the same cases within
  the default deadline. This shows consensus and completion, not 1× speed for
  Legion in those stress battles.
- All eight impaired-network scenarios in each mode ran with verified shaping:
  confirmed setup, archived delay/loss rules, a filter on the case's exact game
  port, and teardown back to the interface default.
- Client-referee counts are partial because the harness stops each referee when
  its clients exit, so 15 Retail and 24 Legion games saved a referee replay. The
  referee's live hash comparison ran in every seated game.
- Cleanup was confirmed on both hosts: no test referee, listener, shaping rule,
  ownership file or scheduled cleanup remains. The public server binary and
  service were never replaced, stopped or restarted; no certificates were touched.

Referees were built in an Ubuntu 24.04 container (GCC 13.3, `-O2 -g` Debug) and
clients with GCC 16.2.1 (`-O2 -g` Debug), all from `3490f55` with a clean build
identity. The harness was a working copy of `tools/desync-hunt-remote.sh` with
stricter shaping and cleanup evidence than the committed version. SHA-256:

- Referee: `09b097a21580f8124667807d9f547d158b8b4569d01b76fa5eba1c5fbd5c72dc`
- Client: `696946888240b441a8af52d52d2489fbf4a97819204a3aaf3bfa32cf4d881337`
- Override transfer test: `1d96cb7938997d748475cd9d423e1bbb3e35c4d9465a2d944936de33bbf58c86`
- Harness: `02d27068863de212b488e19ba8dbe14e9a386b74039dd709defabaabbd6db974`

## CI and packages

*Pending:* pre-tag CI, tagged CI on all four workflows, asset download and
SHA-256/size checks, Windows signing verification and package payload checks.

## Screenshots

All thirteen README and Cartographer screenshots were freshly captured on
2026-10-06 from the 0.7.25 preparation build (`takclient` reporting
**0.7.25**, build `v0.7.23-197-g3490f5544e65`, optimized Debug, the
`release-docs` branch at `origin/main` 3490f55) and visually inspected. Game and
menu views used the offscreen video driver with OpenGL rendering (confirmed in
each log); Cartographer views used the dummy driver and software renderer. Each
capture used isolated `XDG_DATA_HOME`/`XDG_CONFIG_HOME` profiles, never a user
profile. Both scratch servers reported protocol v236. The diplomacy fixture and
the Cartographer model-inspector workflow test printed PASS.

Nine images changed. `campaign.jpg`, `crusades.jpg`, `results.jpg` and
`cartographer.png` were re-rendered but are byte-identical to the previous
refresh. The room fixture now selects Legion, because Retail+ no longer exists.
The JPEG encoder reproduces the previous published bytes exactly from the
previous raw capture, so differences come from rendering only. The capture
notes in [the image directory](img/README.md) identify development demos and
sample results rather than live-game measurements.

## Limits

Local visual inspection covers Linux OpenGL screenshots and the software
Cartographer renderer. Interactive Windows/macOS GPU rendering, physical
high-DPI displays and a multi-hour combat soak are *pending* or out of scope;
the lead will record which.
