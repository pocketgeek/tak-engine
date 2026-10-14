# 0.8.2 release validation

Protocol **257** (0.8.0 and 0.8.1: 256), replay format **12**, generator
version **8**, campaign payload **4**, database schema **9** (all but the
protocol unchanged since 0.8.0).

Tag and publication: *Pending.*

## Scope and compatibility

0.8.2 follows 0.8.1 (protocol 256). After the 0.8.1 validation commit
`9f677c87`, three commits change the Legion simulation, its tests, CI and
documentation:

- **Walk-in-place livelock** (`3d4737ad`, protocol 256 -> 257). In the
  pinwheel, a blocked cell is a wall contact only when its blocked run is not
  all blocking features of TDF category trees, plant(s) or rocks inside 7 × 7
  cells (`LegionNavigator`'s `wallOrigin` / `loneCell`,
  `RetailMapFeatureType::treeOrRock`, set from the feature's TDF `category` in
  `src/sim/matchsetup.cpp`). A body blocked again on the cell a flow-around
  took it to does not flow straight back for `kSlideHold` = 60 hold updates
  (hashed `Member::slideCell` / `slideFrom`). `World::updateLegionStill` judges
  stillness by net headway over 8-tick windows (`LegionHeadway`); a window that
  stands although the body walked or turned to and fro latches
  `Unit::legionShuffle` (hashed with its window), which holds the body until it
  is a body length clear or `kLock` = 64 ticks pass. A standing body, and steps
  that do not turn it, request no turn (`turnReqBam` 0), so no `TurnDirection`
  reaches its scripts. New tests `legion_lonetrees`, `legion_slideback`,
  `legion_stillturn` and `legion_still` (`tools/legion_still_test.cpp`, added to
  the Legion CI build in `linux.yml`, `windows.yml` and `macos.yml`); the
  scenario, cost and navigation-checkpoint baselines retaken for Legion.
- **A forest is a wall** (`05855040`, review of `3d4737ad`, no baseline move).
  `loneCell` links blocked cells the plane's body cannot pass between (at most
  `footX` columns and `footZ` rows apart) instead of 8-connected runs, and its
  memo is keyed on a navigator `cellGen` bumped by every static change
  rectangle. New test `legion_forestedge`.
- **User acceptance** (`c100382f`): the six `LL-257` Retail-floor exceptions
  and the lane-crossing rise (6,578 -> 7,117) are recorded as accepted in
  `docs/legion-pathfinding.md` and `tools/scenarios/baseline.json`.

Every measured figure in the [release notes](release-0.8.2-notes.md) is taken
from those commits and the "Livelock: the 0.8.1 walk-in-place" section of
[the Legion notes](legion-pathfinding.md#livelock-the-081-walk-in-place-protocol-257),
with its setup. The commits record Retail as bit-identical: every Retail record
of the full scenario run equals the previous head's, and the Retail `--mpai`
hashes are unchanged.

**Compatibility.** `kNetVersion` is **257** in `src/net/protocol.h` at
`c100382f`, against 256 at `v0.8.1` and `v0.8.0`. `git diff v0.8.1..c100382f --
src` touches only `src/sim/` (`legion.cpp`, `matchsetup.cpp`, `retailmap.h`,
`sim.cpp`, `sim.h`) and `src/net/protocol.h`; nothing under `src/client/`.
`git diff v0.8.0..c100382f` touches nothing under `src/server/`, `src/ai/`,
`src/tnt/` (the map generator, `formatVer` 8), `src/campaign/`,
`src/server/crusades/` or `src/net/replayhdr.h` / `src/net/crusades.h`, so
replay format 12 (`kReplayFormat`), generator version 8, campaign payload 4
(`crusades.h` `kVersion`) and schema 9 (`kSchemaVersion`) are unchanged.

- **Multiplayer.** The server's Hello check (`src/server/server.cpp`) rejects a
  client whose protocol differs from `kNetVersion` ("protocol version
  mismatch"), as well as a different pure-retail gameplay-data hash. So **0.8.2
  cannot play with 0.8.0 or 0.8.1 in either direction**: 0.8.2 clients cannot
  join a 0.8.0/0.8.1 server, and 0.8.0/0.8.1 clients cannot join a 0.8.2
  server. The server and all players must update together.
- **Replays.** `supportedReplayProtocol` (`src/net/replayhdr.h`) accepts a
  recording only when its protocol equals `kNetVersion`, and the client's
  replay loader (`src/client/replayfile.cpp`) and the Crusades replay files
  (`src/server/crusades/replayfiles.cpp`) both use it. So 0.8.2 does not play
  0.8.0 or 0.8.1 replays, and those builds do not play 0.8.2 replays.
  `tools/replay_test.cpp` now pins `kNetVersion == 257`.
- **Maps and settings.** Generated-map recipes (generator 8) and saved `.kmp`
  maps carry over. No setting was added or removed, so a settings file reads
  the same in 0.8.0, 0.8.1 and 0.8.2.

`kNetVersion` must be re-checked immediately before tagging (in 0.8.0 the
protocol moved during preparation).

## Local preparation checks

All checks ran on a clean detached worktree of `origin/main` at
`c100382fb06cf7a816554f4e1b2289413e8e76ac` (protocol **257**, replay format 12).
The version bump lives on the docs branch, so these binaries still call themselves
0.8.1. The owned retail install served as `--data` and `TAK_TEST_DATA`. Toolchains
were GCC 16.2.1, Clang 22.1.8, CMake 4.3.0, Ninja 1.13.2 and Python 3.14.8. Cores
0–3 stay reserved for timing work, so builds and tests ran on cores 4–23.

**Result: every check passed.** There was no crash, desync, sanitizer finding or
stale baseline, so no `release-local-082-baselines` branch was needed, and no fix
was required before tagging. The four commits since `v0.8.1` are the Legion
walk-in-place livelock fix (`3d4737ad`, `05855040`), the record of the user's
acceptance of its costs (`c100382f`), and the 0.8.1 validation commit itself. The
fix touches `src/sim` (`legion.cpp`, `sim.cpp`, `sim.h`, `matchsetup.cpp`,
`retailmap.h`) and `src/net/protocol.h`, and it moves the Legion baselines
(`tools/scenarios/baseline.json`, `tools/legion_cost_baseline.json`,
`tools/navigation_checkpoints.json`). Details follow.

- **No multiplayer or replay compatibility with 0.8.0/0.8.1.** `kNetVersion` is now
  **257** (it was 256 in 0.8.0 and 0.8.1). The server's Hello gate
  (`src/server/server.cpp`) rejects any client whose protocol differs from its own
  ("protocol version mismatch"), so 0.8.2 clients cannot join 0.8.0/0.8.1 servers,
  and the reverse. `src/net/replayhdr.h` accepts a replay only when its protocol
  equals `kNetVersion`, so 0.8.0/0.8.1 replays do not play in 0.8.2, and the reverse.
  The replay format stays 12. The Retail simulation is bit-identical to 0.8.1: the
  Retail `--mpai` hashes below are unchanged. Only Legion moved.
- **Builds.** Release, Debug, optimized Debug (`-O2 -g`), Clang Debug and
  AddressSanitizer all-target builds completed with exit code 0: 599 steps in
  Release and 600 in the Debug-type trees (up from 597/598). These were fresh
  trees, with ccache on the GCC builds. They use the static-deps prefix
  (`third_party/static-deps`) and the static Bink/streaming FFmpeg prefix
  (`third_party/ffmpeg-bink`). Every client and server reports
  `takclient (TAK engine) 0.8.1 (build v0.8.1-4-gc100382fb06c)` and
  `takserver (TAK engine) 0.8.1 (build v0.8.1-4-gc100382fb06c)`. The build identity
  is clean, with no `-dirty` suffix. The server announces `protocol v257`.
- **Full CTest.** There are now 560 registered tests (546 in Release), up from 555.
  160 of them carry the `nightly` label, as in 0.8.1. The 5 new tests are
  `legion_still` (the net-headway stand rule and the shuffle latch, unit level) and
  the Legion world cases `legion_lonetrees`, `legion_slideback`, `legion_stillturn`
  and `legion_forestedge`.
  - **Release: 546/546 passed. Optimized Debug: 560/560 passed.** Both ran the full
    suite, nightly label included. The worktree had an `assets` link to the real
    `assets` directory, so `cobanim` ran its 11 checks.
  - **Debug and Clang Debug ran with `-LE nightly`**, as in 0.8.1. At `-O0` some
    nightly tests exceed their timeouts, and the nightly job only runs them at
    `-O2`. **Debug: 400/400 passed. Clang Debug: 400/400 passed.** The two suites
    ran one after the other.
  - **`credstore` ran for real.** It round-tripped through the session's Secret
    Service under the test-only service name "TAK Engine Test" and removed its
    entry. No real stream key was involved.
  - **All 67 `legion_check_*` scenario gates passed** in Release and optimized
    Debug against the updated `tools/scenarios/baseline.json`.
    `legion_check.py validate --baseline tools/scenarios/baseline.json` reports
    0 problems. The stale Retail scenario hashes that the nightly's full-set check
    reports (362 Retail hashes and 23 Legion keys, failing identically on
    `c100382f` and on the nightlies before 0.8.1) are outside these ctest gates, so
    no release test failed on them and no baseline was regenerated.
  - **Battle cost and naval tests passed.** `legion_cost`,
    `legion_scenario_battle-field-2x500`, `legion_scenario_battle-assault`,
    `naval_ai` and `legion_acceptance_crowdheld_legion` passed in Release and
    optimized Debug, with no ratchet failure against the updated
    `tools/legion_cost_baseline.json`.
  - **Slowest tests.** In Release and optimized Debug: `legion_check_pocket-615-open`
    (856.2 s / 974.1 s), `legion_check_pocket-615-wall` (822.6 s / 949.6 s) and
    `legion_check_tail-corner380` (825.3 s / 931.4 s). All are under their 1800 s
    limit. In Debug and Clang Debug the slowest was `legion_w7_awarebig`
    (363.2 s / 329.4 s), which has no explicit limit.
  - **Headroom note.** `movement_orders_trolls` took 275.7 s (Debug) and 275.3 s
    (Clang Debug) against its 300 s limit (223.8 s and 263.1 s in 0.8.1). It passed,
    but the margin is now thin in both unoptimized trees, not only Clang. It is a
    Retail test and the Retail hashes did not change. I did not establish whether
    the extra time comes from machine load or from per-tick cost added in
    `sim.cpp`; it is worth a look before the limit is hit.
- **Focused AddressSanitizer/leak run: 149/149 passed.** The selection is the 144
  tests from 0.8.1 plus the new `legion_still`, `legion_lonetrees`,
  `legion_slideback`, `legion_stillturn` and `legion_forestedge`. The run used
  `bash tools/run-ctest-selection.sh` with `SDL_SHUTDOWN_DBUS_ON_QUIT=1`, dummy SDL
  video/audio, `ASAN_OPTIONS=detect_leaks=1` and no suppressions. There were no
  AddressSanitizer, LeakSanitizer or runtime-error reports.
- **Retail group emulation check (formation-air).** I ran
  `python3 -B tools/re/check_retail_group.py` (400 cases by default) against the
  emulated `KINGDOMS.icd`. It passed every section: tick 400, query 4000, check
  2000, formation 2000, vtolcheck 2000 and vtolform 2000. Every outcome count is
  identical to 0.8.1.
- **Determinism.** In `tools/check-determinism.sh`, the GCC and Clang x86-64 builds
  at O0/O2/O3 all agree on `dcef618cd2e4d558`, the same as 0.8.1. The aarch64 legs
  are skipped because target headers are missing; native ARM64 CI remains a separate
  gate. `tools/check-detmath.sh` passes, including the `std::atomic_ref` guard.
- **`--mpai` golden hashes.** All four runs were made twice and produced the same
  hash both times. The client and server logs contain no desync or referee-suspect
  line. The runs used the optimized Debug build, Inner Circle,
  `takserver --local --no-auth --seed 1`, an empty `XDG_DATA_HOME`, and
  `TAK_LEGION=1` for the Legion runs. **Retail is unchanged from 0.8.1. Legion
  moved, as expected from protocol 257, and equals the hashes recorded with
  `3d4737ad`/`05855040`.**

  | Mode | Duration | Hash (both runs) | 0.8.1 |
  | --- | ---: | --- | --- |
  | Legion | 300 s | `70e826bb0db70ced` | `21055e66adec40d6` |
  | Retail | 300 s | `95b05c8290b55317` | `95b05c8290b55317` |
  | Legion | 60 s | `55b0eec316e45071` | `025e4a35abfcd04e` |
  | Retail | 60 s | `84fdef37bbb3606e` | `84fdef37bbb3606e` |

  Optimized Debug binary SHA-256:
  - client `7f9c629b48f569f389deab8266c8a573cdd425868d593879be2fd830a92e937c`
  - server `9e7a6b2730f41f6c26ba8a5fe5b05a5ef773414ab685d3eb51042ebe82b6bd08`
- **Python research suite.** **165 tests** passed, with one skip: the optional corpus
  test, which runs only when `TAK_AUTH_RETAIL_DIR` is set. The suite ran after the
  Release build, so the `build/hpitool` tests ran too. The separate owned-retail
  archive verifier (`TAK_AUTH_RETAIL_DIR`) passed all **6 tests**, including the
  corpus and in-memory negative checks. I took SHA-256 sums of the 16 retail HPI
  archives and `Kingdoms.key` before and after the runs. They are unchanged, and
  identical to the 0.8.1 run.
- **Syntax checks.** All **438** tracked Python files compile. All **21** tracked
  `*.sh` scripts and the extensionless `tools/tak-netem` pass `bash -n`.
- **Notes (informational).**
  - The Release build emits 530 warnings. The set is identical to 0.8.1, file by
    file, including the one vendored-SQLite `-Wstringop-overread` false positive.
    The livelock fix adds no warning.
  - `tools/run-ctest-selection.sh` is still tracked as mode 100644, so it must be
    run with `bash`.
  - A worktree outside the main checkout still needs an `assets` link for
    `cobanim`.

Logs: `~/tak-tmp/scratch/release-082/local/` (`cfg-*.log`, `make-*.log`,
`build-status.txt`, `versions.txt`, `toolchains.txt`, `ctest-*.log`,
`ctest-status.txt`, `ctest-asan.log`, `asan-list.txt`, `mpai-*.log`,
`mpai-results.txt`, `o2-sha256.txt`, `check-determinism.log`, `check-detmath.log`,
`check_retail_group.log`, `legion-validate.log`, `python-suite.log`,
`verifier.log`, `pycompile.log`, `bashn.log`, `retail-sha-{before,after}.txt`).

## Remote multiplayer sweep

The full 37-scenario table passed for both shipped pathfinding modes, Retail and
Legion, on **2026-10-14 (11:29–12:13 UTC)**. Referees ran on `tak.pgnet.us` and
`vpn3.pgnet.us`, with optimized local clients. Each scenario allowed ten
simulated minutes (18,000 ticks) or ended at an actual result for that seat.
Real seated players provide the client/referee checksum coverage. The two
all-AI spectator scenarios per mode test flow control, not hashes.

The harness picks the mode through the hosting client's room options:
`TAK_LEGION=1` creates a Legion room, and without it the room is Retail. Every
recorded client and referee replay header (70 Retail, 76 Legion) confirms the
intended mode, replay format 12 and protocol 257. The headers carry engine
version 0.8.1 because the sweep ran on `origin/main` before the 0.8.2 version
bump, which changes only the version string.

| Mode | Completed scenarios | Completed seats | Client-pair checkpoints | Client–referee checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Retail | 37/37 | 52/52 | 10,813 across 22 seat pairs, 0 mismatched | 1,300 across 25 seat/referee comparisons, 0 mismatched |
| Legion | 37/37 (single pass, no retries) | 52/52 | 11,591 across 22 seat pairs, 0 mismatched | 1,734 across 31 seat/referee comparisons, 0 mismatched |

Each mode passed both negative gates before its table started. The referee
reported the planted desync (`client 1 DESYNCED at tick 900`), and the
override-transfer test rejected the incorrect loaded gameplay data
(`verified packs transferred and incorrect loaded gameplay rejected`). The
build-identity gate also passed in each mode (`both at v0.8.1-4-gc100382fb06c`).
The gate checks only `tak.pgnet.us`, so the referee on `vpn3.pgnet.us` was
checked by hand after deployment: the same SHA-256 and the same `--version`
build id. No natural `DESYNCED`, `REFEREE SUSPECT` or client-reported desync
appeared in any case log. The only `DESYNCED` and `err=desync detected` lines
are the planted ones in the validation logs.

The checkpoint counts compare the per-tick hash records in the seats'
`.takrep` files (every 30 ticks). The client-referee column compares the seats
with referee replays (every 300 ticks). Each format-12 checkpoint carries a
position digest as well as the state hash, and the comparison script checks
both wherever both sides recorded one. All 2,476 referee checkpoints and all
55,830 seated checkpoints carried a digest. Before the counts were trusted, the
script was checked against a copy of one game's replay (`2h-baseline`, Retail)
with one hash bit flipped, and then one digest bit flipped. It reported a
mismatch both times. Referee replays were matched to their cases by the seed in
the replay header (seed = 2000 + scenario index), not by port. The port was
also checked, and it agreed for every replay. The referee counts are partial:
the harness stops each referee as soon as its clients exit, so only 19 Retail
games (17 seated) and 25 Legion games (23 seated) wrote a referee replay. The
referee's live check runs on every seated game, whether or not a replay was
written.

**No Legion retries.** As in 0.8.1, the Legion pass ran with
`TAK_WALL_TIMEOUT=3600` from the start, with the same seeds and the same
simulated-time limit. It changes only the real-time deadline, not the tick or
hash checks. Every Legion seat completed in the single pass (26 minutes).
`2h-stress` and `2h-orders-stress` reached 18,000 ticks with 7,409 and 6,731
units, and `h-stress` concluded at tick 6,506 with 7,834 units (in 0.8.1 it ran
to 18,000 ticks). The Retail pass used the default limit, and every case
finished inside it (17 minutes): `h-stress` and `2h-stress` concluded at ticks
5,606 and 6,611, and `2h-orders-stress` concluded at tick 6,227 with 8,807
units.

**Reproducibility across releases.** Since `v0.8.1` the only engine changes
are in `src/sim` (the Legion pinwheel, side-step, hold and turn changes) and
`src/net/protocol.h` (`kNetVersion` 256 → 257); `src/ai` and `src/server` are
unchanged. Retail is meant to be bit-identical, and the sweep agrees: 31 of the
37 Retail cases ended with the same tick and hash as the 0.8.1 sweep, and the
six that differ are exactly the six order-autoplay cases (`2h-orders`,
`2h-orders-stress`, `4h-orders`, `net-loss3-orders`, `net-orders100`,
`net-2h-rtt100`). Those cases bucket live orders by arrival tick, so their
hashes are not expected to repeat. All 37 Legion cases ended with a different
hash from 0.8.1, which is expected from the deliberate Legion behaviour change;
in every one of them the seats and the referee agreed.

**Retail `h-stress` (seed 2003), the 0.8.1 follow-up.** This sweep concluded at
tick 5,606 with hash `c93ae6becfcde6ca` and 8,559 units, the same
tick and hash as the 0.7.28 sweep and all three 0.8.1 runs (the 0.8.1 sweep also had 8,559 units). The
seat matched the referee at 19 of 19 shared checkpoints. The 0.8.0 result
(tick 5,634, `c8293ed0b1e62c64`) has now not recurred in five runs across three
releases. It remains a single unexplained run-to-run variation late in the game,
after tick 5,580, with seat and referee in agreement; it was not investigated
further here and does not block the release.

**Impaired networks.** All eight impaired scenarios ran under netem in both
modes: 50, 100 and 500 ms delay, 100 ms ± 30 ms jitter, 1% and 3% loss, live
orders, and two seated players over 100 ms. For every case, the harness
archived the `__SETUP_OK__` marker, the `prio 1:` root with `netem 30:` on band
1:3, and the u32 filter matching that case's exact game source port (Retail
7927–7934, Legion 8027–8034). Each teardown log ends with only the interface's
default qdisc (`fq_codel` on tak `ens18`, `noqueue` on vpn3 `wg0`). No case
recorded an unclean teardown.

**Isolation.** The referee was a separate test executable,
`~/takserver.sweep082`, using test ports 7890–7936 and 7990–8036 (the public
service uses 7677). It used per-mode map caches
`~/tak_cache.sweep082-{retail,legion}` and per-run replay directories
`~/tak_replay.sweep082-{retail,legion}`. The public `/usr/bin/takserver` on
tak.pgnet.us (PID 25743, port 7677, active since 2026-10-13 19:02:44 UTC) was
not replaced, stopped or restarted. No certificates were touched. Local clients
were pinned to CPUs 4–23.

**Cleanup.** After all runs, both hosts were checked:

- No process was executing `takserver.sweep082`.
- No listener remained on the test ports.
- No `netem` or `prio` qdisc remained on any interface.
- No `/tmp/tak-netem-owner.*` claim file remained. Eight 1800-second watchdogs
  from Legion impaired cases were still sleeping: five on tak (`net-rtt50`,
  `net-rtt500`, `net-jitter`, `net-2h-rtt100`, `net-loss3-orders`) and three on
  vpn3 (`net-rtt100`, `net-loss1`, `net-orders100`). Their claim files were
  already gone, so they would have exited without running `tc`. They were
  stopped by exact PID.
- The test binary, the sweep's map caches and replay directories, and the
  `/tmp/tak-{srv,val,neg}-*.log` files were removed from both hosts after the
  referee replays had been copied locally.

Locally, no sweep `takclient` or harness process remained. The public
tak.pgnet.us service was still `active`, with the same PID as before the sweep.

**Binaries.** All were built from `c100382f` (`origin/main`), with build id
`v0.8.1-4-gc100382fb06c` and a clean tree. Protocol `kNetVersion` was 257.

- Referee: Ubuntu 24.04 container, GCC 13.3.0, optimized Debug (`-O2 -g`), max
  symbol version GLIBC_2.38. SHA-256
  `56fd7a7c7281553347f54fe8d72e40f84c5dffae0b219d17dfad12b42f8a7843`
  (identical on both hosts, checked after deployment).
- Client: Fedora, GCC 16.2.1, optimized Debug (`-O2 -g`). SHA-256
  `c27fdb84af925524880084c28925c1576539bf10a20abd2db9df66a837b6ae28`.
- `override_transfer_test`: SHA-256
  `ab6bc94e2edca6b9a5d12dba3d61229a1c744251dd3b4202871db32daf83ded5`.

**Harness.** These runs used the committed `tools/desync-hunt-remote.sh`,
unmodified, from a clean detached worktree. SHA-256
`02d27068863de212b488e19ba8dbe14e9a386b74039dd709defabaabbd6db974`
(byte-identical to the file used for 0.7.25–0.8.1).

**Mixed versions.** No game mixing a 0.8.1 build with a 0.8.2 build was played.
None can be: 0.8.2 speaks protocol 257, and the server's Hello rejects any
client whose `kNetVersion` differs (`protocol version mismatch`), and the replay
loader accepts a replay only when its protocol equals `kNetVersion`
(`src/net/replayhdr.h`). So 0.8.2 is neither multiplayer- nor
replay-compatible with 0.8.0 or 0.8.1.

## CI and packages

*Pending.*

## Screenshots

Done on 2026-10-14 with the 0.8.2 release-preparation build (`takclient
--version` 0.8.2, build `v0.8.1-4-gc100382fb06c-dirty` from the version bump and
the capture patch; Debug with `-O2`, OpenGL) and the same capture-only patch as
0.8.1, which is not committed. Each re-rendered image was inspected by eye
against its caption.

- **Why these.** No user-interface code changed since 0.8.1 (no `src/client/`
  change), so only the title's version string and pictures whose scene the
  Legion change moves are stale. A first render of the `flyers.jpg` scene showed
  the Zhon ground units in different places among Ulasem Arena's trees, so the
  three Legion scenes were re-rendered.
- **Gallery.** Re-rendered: `title.jpg` (shows VERSION 0.8.2) and `flyers.jpg`
  (the 44-unit Zhon formation 22 s after its move order; flyers still hold
  stations over the front of the ground).
- **User guide.** Re-rendered: `guide/flyers-retail-legion.jpg` (both halves,
  so the Retail/Legion pair comes from one build) and `guide/replay.jpg` (a
  fresh recording of the same scripted Legion move, played back by the same
  build, 9 s of 16 s; 0.8.2 cannot play the 0.8.1 recording).
- **Kept.** Every other picture: the information-bar pictures re-rendered for
  0.8.1, `guide/fire-on-the-move.jpg` (Retail), the spectator, menu, Options
  and Cartographer pictures. None shows Legion movement or a changed screen.
- **Encoding.** As documented in [the capture notes](img/README.md): JPEG with
  full chroma resolution, quality 90 (title), 88 (gallery) and 86 (guide),
  guide images scaled from 1600 × 900 to 1280 × 720, the title cropped to its
  1440 × 1080 artwork.
- **Size.** Total tracked image bytes 15,769,010 -> 15,765,757 (−3 KB).
- **Links.** All relative links, anchors and image paths in the changed
  Markdown resolve.
- **Not captured.** A unit standing instead of walking on the spot cannot be
  told from a still picture; Windows and macOS rendering.

## Limits

*Pending.*
