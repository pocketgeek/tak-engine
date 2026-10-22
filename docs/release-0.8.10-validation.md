# 0.8.10 release validation

Protocol **289** (was 288), replay format **14**, generator version **9**,
campaign payload **4**, database schema **9**.

Tagged `v0.8.10` at `66055338` (annotated tag object `de785d73`) and published on 2026-10-22 22:21 UTC.

## Scope and compatibility

0.8.10 follows 0.8.9 (protocol 288). After the 0.8.9 validation commit
`b35d63b9`, main reached `c73416f0` in 17 commits, all linear (no merges).
`git diff --stat b35d63b9..c73416f0 -- src` touches 27 files:
`src/server/server.cpp`, `src/server/acme.cpp`, `src/server/acme_internal.h`;
`src/net/` (`client.cpp`, `client.h`, `conn.cpp`, `protocol.cpp`, `protocol.h`,
`revocation.cpp` and `revocation.h` (both new)); `src/sim/` (`matchsetup.cpp`,
`matchsetup.h`, `mission.cpp`, `retailcost.h`, `sim.cpp`); `src/client/`
(`gameview.h`, `gameview_impl.cpp`, `gameview_net.cpp`, `gameview_render.cpp`,
`main.cpp`, `settings.cpp`, `settings.h`, `streaming.cpp`, `streaming.h`);
`src/video/stream.cpp`, `src/video/stream.h`; and
`src/cartographer/playtest.cpp`. Nothing under `src/ai/`, `src/campaign/`,
`src/tnt/`, `src/terrain/`, `src/cob/` or `src/crt/` changed.
No commits landed on main after `c73416f0` before the preparation commits:
`c2ec34ab` (0.8.10 screenshots: title, streaming panel, disconnect wait),
`275319ef` (version 0.8.10, release notes, validation skeleton) and the commit
that fills this report.

The changes:

- Disconnect wait and AI takeover (server, net, client; protocol 289):
  `deb6a31b` (wait with WAITING FOR countdowns, `Event::AiTakeover` to a Normal
  server AI, short reconnect from the first missing tick with `Reseated`, no
  world rebuild; removed the 5 min slot hold, the 2 min pause budget and its
  `Forfeit`, the full-log seated rejoin, `TAK_RESUME` and `--mprejoin`; quit
  from the menu sends `LeaveGame`), `96a4a6cf` (a countdown begun before the
  first tick runs out; reconnect with a login), `82bb831d` (one 30 s budget per
  seat per game), `bf14a9e4` (`Msg::Waiting` carries the recipient's own budget;
  the client reconnects within it), `b2e14782` (budget spent from the last
  contact; holds past a 1 s grace charged), `c73416f0` (no charge for pre-game
  or loading silence; the client's reconnect window kept current).
- Streaming: `abdeea70` (SAVE A LOCAL COPY, fragmented MP4 in `videos/`,
  `streamLocalCopy` setting; FFmpeg `--enable-muxer=flv,mp4`).
- Campaign: `ec38fdfa` (the human is always Player1 / world slot 0; fixes
  `takx22_mt`, `takx24_mt`), `13477a0b` (`replay_test` pins 289 and refuses 288).
- Cartographer: `23ecea9c` (Test Map finds the game from inside the macOS app
  bundle, DMG and ZIP layouts), `7df207d4` (guide), `57ce78e1` (Windows test
  expectation).
- Simulation: `e19580b9` (retail terrain-scaled turn rate in the target score's
  turn penalty, `retailEffectiveTurnRate`; `fire_sweep_turnrate`).
- Public server TLS: `7b674dd5` (ACME Renewal Information, RFC 9773, with
  `replaces`; CRL revocation checks in the TLS client and the renewal worker),
  `5552c586` (`replaces` only for a described certificate; window clipped at
  expiry).
- Docs: `cd2df9d7`, `b2999ea9`.

The disconnect accounting is server wall-clock only and changes no simulation
state until a takeover; the campaign seat changes hashed state for `takx22_mt`
and `takx24_mt` only; the turn-rate change reaches only shooters on roads or
water. `--mpai` Inner Circle seed 1 at 60 s is unchanged per `deb6a31b` and
`e19580b9`: Retail `b353410003e98dd5`, Legion `0825aeb9e8139203`.
At 300 s they are unchanged as well: Retail `a3d953803a064191`, Legion
`ce5c4ba7562e53b9`, the same as 0.8.9 (the turn-rate change evidently never
changes a target score in this small scenario; see the local checks).
`--mpai` on Windows and macOS was run on optimized Debug builds (`-O2`) of the tag `66055338`, built on oden-win (MSYS2 MINGW64, GCC) and on the arm64 MacBook (Apple Clang). The release binaries reject the harness flags, so Debug builds were needed. The settings were Inner Circle, `takserver --local --no-auth --seed 1`, a scratch profile, and `TAK_LEGION=1` for Legion. All eight games ended `err=none end=timelimit` and match Linux and 0.8.9: Retail 60 s `b353410003e98dd5` (4 units), 300 s `a3d953803a064191` (13 units); Legion 60 s `0825aeb9e8139203` (4 units), 300 s `ce5c4ba7562e53b9` (18 units), on both hosts. Neither host logged a desync or referee-suspect line. The tag's dispatched Determinism run (39857683568) runs no `--mpai` game. Its cross-build check agrees on golden `dcef618cd2e4d558`.

**Compatibility.** `kNetVersion` is **289** in `src/net/protocol.h` at
`c73416f0` (288 at `v0.8.9`). `kReplayFormat` is **14** in
`src/net/replayhdr.h`, as at `v0.8.9`. The map generator's `formatVer` **9**
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` 4
(`src/net/crusades.h`) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`) are unchanged.

- **Multiplayer.** The server's Hello check rejects a client whose protocol
  differs from `kNetVersion` (or whose gameplay-data hash differs). So **0.8.10
  cannot play with 0.8.9, 0.8.8 or earlier in either direction**; the server and
  every player must update together.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  protocol equals `kNetVersion`, so 0.8.10 refuses 0.8.9's and 0.8.8's
  protocol-288 replays (format 14 as well) and 0.8.9 refuses 0.8.10's.
  `tools/replay_test.cpp` pins `kNetVersion == 289` and `kReplayFormat == 14`
  and checks that `supportedReplayProtocol(14, 288)` is refused.
- **Maps, mods and settings.** Saved `.kmp` maps, generator-8/9 recipes,
  `mods/` packs and `ModCache/*.takmods` carry over. One settings key was added
  (`streamLocalCopy`, default off); none was removed.

## Local preparation checks

All checks ran from a clean detached worktree (`~/tak-tmp/wt/release-0810-local`) at `main`
`c73416f0` (protocol **289**, replay format **14**), with `assets` and `third_party` links to the
main checkout (`third_party/ffmpeg-bink` rebuilt with the mp4 muxer). Builds and tests ran on cores
4-23; the owned retail install served as `--data` and `TAK_TEST_DATA`. The retail install's
SHA-256 list (17 files) is identical before and after the run.

**Result: no real regression. Every `--mpai` hash equals 0.8.9.** The one Release failure
(`voice`) passed when rerun alone. The three Debug (`-O0`) failures are timing failures; all three
passed in the full optimized (`-O2`) suite and when rerun alone in the Debug tree. The ASan
suite was stopped early (at about 562 of 583 tests); no failure before the stop.

- **Multiplayer and replays.** `kNetVersion` is **289** in `src/net/protocol.h` (288 in 0.8.9)
  and `kReplayFormat` is **14** in `src/net/replayhdr.h` (unchanged). **0.8.10 is not multiplayer-
  or replay-compatible with 0.8.9 or 0.8.8**: servers and every player must update together.
  `supportedReplayProtocol()` accepts a replay only when its recorded protocol equals
  `kNetVersion`, so replays recorded by 0.8.9 and earlier (protocol 288 and below) are refused,
  even though their format number (14) is still known. `tools/replay_test.cpp` pins `kNetVersion == 289`
  and `kReplayFormat == 14` and checks that `supportedReplayProtocol(14, 288)` is refused (no
  real protocol-288 file was loaded).
- **Builds.** Release, Debug, optimized Debug (`-O2`), Clang Debug and AddressSanitizer
  all-target builds completed with exit code 0: 892 steps in Release, 893 in the other four
  (890 / 891 in 0.8.9). ccache and mold. Warning lines: Release 951, `-O2` 928, Debug 918,
  Clang 506, ASan 933 (0.8.9 Release: 950).
- **Full CTest.** **745** registered tests in the Debug trees (742 in 0.8.9) and **700** in
  Release (698). Each tree ran as `-LE nightly` (538 tests in Release, 583 in the others, which
  include the `pref_guard` fixtures); the Release and optimized Debug trees also ran `-L nightly`
  (164).

  | Tree | Selection | Result | Time | Failures |
  | --- | --- | --- | ---: | --- |
  | Release | `-LE nightly`, 538 | 537/538, **538/538 after one rerun** | 567 s | `voice` (passes alone) |
  | Release | `-L nightly`, 164 | **164/164** | 1,647 s | none |
  | Optimized Debug | `-LE nightly`, 583 | **583/583** | 823 s | none |
  | Optimized Debug | `-L nightly`, 164 | **164/164** | 1,424 s | none |
  | Debug (`-O0`) | `-LE nightly`, 583 | 580/583 | 3,662 s | `cartographer_ui_smoke`, `mod_transfer`, `crusades_history_ui_network` (timing; each passed rerun alone; see below) |
  | Clang Debug | `-LE nightly`, 583 | **583/583** | 2,988 s | none |
  | ASan | `-LE nightly`, 583 | stopped at 562/583, **0 failures** | not completed | none before the stop |

  - **`voice` (Release): `FAIL another player's VOLUME leaves this talker alone: 0.510864`,
    `voice_test: 243 checks, 1 failed`, 20.8 s.** A load-sensitive audio-mix check (many tests
    ran at once). Rerun once alone with the same `TAK_TEST_DATA` on cores 4-23: **passed**
    (20.75 s). It passed in the optimized Debug, Debug, Clang and (before the stop) ASan trees.
    0.8.8 saw `voice` fail under load in the optimized Debug tree; 0.8.9 had no `voice` failure.
  - **Debug (`-O0`) timing failures.** All three passed in the full `-O2` suite, and each passed
    when rerun alone in the Debug tree (`rerun-dbg-a.log`: `crusades_history_ui_network` 74.8 s,
    `mod_transfer` 25.8 s; `rerun-dbg-b.log`: `cartographer_ui_smoke` 107.0 s). The unoptimized
    tree was not run a second time in full (only needed when chasing a specific failure):
    - `crusades_history_ui_network` (72.3 s): `subprocess.TimeoutExpired: Command
      '.../build-dbg/takclient game "Frey River Plain" ... --user Carol ...' timed out after 60
      seconds`.
    - `cartographer_ui_smoke` (196.0 s): `FAIL: zhon map 'black heart jungle' smoke: stuck at
      step 523 (modal 17 ...)` and `FAIL: creon blank map smoke: stuck at step 375 (modal 16
      ...)`. The same test failed in 0.8.9's Debug tree (veruna map, step 487) and passed alone.
    - `mod_transfer` (42.6 s): `AssertionError: GAME MODS VERIFIED; SAVE FAILED: filesystem
      error: cannot create directories: Not a directory [.../tak-mod-net-m6p7f1vl/peer/ModCache]`
      then `FAIL: peer closed`. Not seen in 0.8.9 or 0.8.8.
    - All three are slow, load-sensitive UI or network tests that the unoptimized client runs
      near their time limits while other suites share the machine. No source was changed in
      response.
  - **Comparison with 0.8.9 by name.** 0.8.9's only failure was `cartographer_ui_smoke` (Debug)
    plus the 20 ASan leak-report tests; 0.8.10 repeats `cartographer_ui_smoke` and adds
    `mod_transfer`, `crusades_history_ui_network` (Debug) and `voice` (Release), all
    timing-sensitive and clean when run in isolation or on the optimized trees. 0.8.9's
    ASan leak-report failures (the 20 SDL D-Bus tests, which need `ASAN_OPTIONS=detect_leaks=0`)
    did not appear in the 562 tests that ran before the stop.
  - **Unoptimized Legion tests passed under the existing limits** (Debug / Clang Debug / ASan):
    `legion_sched_inflight` 963 s / 932 s / 421 s, `legion_scale_neutral` 954 s / 912 s / 429 s,
    `legion_parallel` 3,225 s (Debug) / 2,687 s (Clang), `legion_route_paused` 1,250 s / 1,025 s,
    `movement_orders_trolls` 322 s / 344 s / 110 s, `terrain_schedule` 85 s / 67 s / 38 s,
    `voice_uplink` 49 s everywhere. `legion_parallel` in Debug is 60% of its 5400 s limit.
  - **The settings-leak guard held.** `pref_guard_setup`/`pref_guard_check` passed in every
    selection.
- **Determinism.** `tools/check-detmath.sh`: OK (no direct libm transcendentals, no
  `std::atomic_ref`). `tools/check-determinism.sh`: **DETERMINISM OK**, every x86_64 build
  (g++ and clang++ at -O0/-O2/-O3) agrees on golden `dcef618cd2e4d558` (same as 0.8.9); the
  aarch64 cross builds SKIP locally (no target libc), as before.
- **`--mpai` golden hashes: all unchanged from 0.8.9.** Inner Circle, `takserver --local
  --no-auth --seed 1`, scratch profile, optimized Debug binaries, `TAK_LEGION=1` for Legion. Run
  twice (`mpai/` and `mpai1/`), identical both times. All eight games ended `err=none
  end=timelimit`; no desync or referee-suspect line.

  | Mode | Length | Hash (units) | 0.8.9 |
  | --- | --- | --- | --- |
  | Retail | 60 s | `b353410003e98dd5` (4) | `b353410003e98dd5` |
  | Retail | 300 s | `a3d953803a064191` (13) | `a3d953803a064191` |
  | Legion | 60 s | `0825aeb9e8139203` (4) | `0825aeb9e8139203` |
  | Legion | 300 s | `ce5c4ba7562e53b9` (18) | `ce5c4ba7562e53b9` |

  The turn-penalty change (`e19580b9`, `src/sim/retailcost.h` and `sim.cpp`) does not move
  hashed state in this scenario: it only changes the turn penalty in target scoring for shooters on
  roads or water, which this small scenario evidently never exercises. The scenario gate keys
  (same counts as 0.8.9) are the wider check.
- **Legion scenario gate: PASS.** `gate/check.txt` and `gate/check-nightly.txt`: `PASS: 36522
  keys checked, 0 failed, 4415 ratchets, 4705 licensed, 266 floor exceptions, 864 report-only`,
  the same counts as 0.8.9. `gate/validate.txt`: `PASS: 0 problems`. Elapsed 5,605 s.
- **Retail group emulation check** (`tools/re/check_retail_group.py`): `PASS: tick 400, query 4000, check 2000, formation 2000, vtolcheck 2000, vtolform 2000; notified 544, slowed 546, reform 1019, wait 232, cancel 102, vreform 367, vwait 48, vcancel 19, vf7 276, vf5 1357, vf0 206, vf1 161`.
- **Python.** `tools` byte-compile: 469 compiled, 0 bad. Python suite: 165 tests OK.

Logs: `~/tak-tmp/scratch/release-0810/` (`ctest-*.log`, `rerun-*.log`, `mpai/`, `mpai1/`, `gate/`,
`rs/remote/`).

**Final source.** After the preparation commits (`275319ef`), the main checkout's optimized Debug
tree rebuilt every target (exit 0; `takclient --version`: `0.8.10 (build v0.8.9-20-g275319ef3fb3)`)
and passed `ctest -L quick`: **478/478**.

## Remote multiplayer sweep

The full 37-scenario table passed for both pathfinding modes, Retail and Legion, on **2026-10-22**
(UTC), both on `main` at `c73416f0fffa` (protocol **289**, replay format **14**, build id
`v0.8.9-18-gc73416f0fffa`; the version string still reads 0.8.9 until the 0.8.10 bump lands). Retail passed
in a single pass. Legion passed after **one** retry of one case (`2h-stress`) that lost a seat to the new
disconnect rule (details below); that retry and a repeat all agreed with the referee.

- **Retail:** 18:33-18:52 UTC (18 minutes including the gates), single pass, no retries.
- **Legion:** 18:52-19:21 UTC (30 minutes including the gates), then a single-case retry of `2h-stress`.

Referees ran on `tak.pgnet.us` (24 runs, 10 at a time) and `vpn3.pgnet.us` (13 runs, 6 at a time), with
optimized local clients pinned to CPUs 4-23. Each scenario allowed ten simulated minutes (18,000 ticks) or
ended at an actual result for that seat. Real seated players provide the client/referee checksum coverage;
the two all-AI spectator scenarios per mode (`w-allai-stress`, `w-allai-bench`) test flow control, not hashes.
The mode is chosen through the hosting client's room options (`TAK_LEGION=1` creates a Legion room).
`TAK_WALL_TIMEOUT=3600` was set for both modes. Every recorded referee replay header confirms the intended
mode, replay format 14 and protocol 289: 37 headers per mode, seeds 2000-2036 each mapped to its case.

| Mode | Completed scenarios | Completed seats | Client-pair checkpoints | Client-referee checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Retail | 37/37 (single pass) | 52/52 | 10,557 across 22 seat pairs, 0 mismatched | 2,659 across 51 seat/referee comparisons, 0 mismatched |
| Legion | 37/37 (36 first pass + `2h-stress` retry) | 52/52 | 10,633 across 22 seat pairs, 0 mismatched | 2,758 across 51 seat/referee comparisons, 0 mismatched |

Each run passed all three `--validate` gates before its table started:

- **Build identity:** `both at v0.8.9-18-gc73416f0fffa`.
- **Planted desync:** the referee reported `game 1: client 1 DESYNCED at tick 900`.
- **Game mod transfer:** `verified game mods transferred and incorrect loaded gameplay rejected`.

The gate checks only `tak.pgnet.us`; the binary on `vpn3.pgnet.us` is the same file (SHA-256
`fb1f06eff7f31231f0b0b5d50f699496a8c3f6d74b90fd0645df519baa978cf7`, `--version` build id identical). No natural
`DESYNCED`, `REFEREE SUSPECT` or client-reported desync appeared in any case log; the only such line in each
run is the planted one. The Retail sweep ended with "no desyncs reported (52 seats, all completed)"; the Legion
sweep ended with "no desyncs in the 51 seat(s) that completed" plus the one failed seat described below, and the
retry ended with "no desyncs reported (2 seats, all completed)". A state hash is compared at every checkpoint; the
position digest is compared as well wherever both sides carry one. Every game wrote a referee replay (37 per
mode, matched to cases by the seed in the replay header; the Legion `2h-stress` replay is the retry's).
`tools/desync-hunt-remote.sh` is unchanged since 0.8.8 (SHA-256
`df09e28844be1f399f9466a62e19c9cdf53298e5fdb9eb5dcefb5fca830ad56e`) and so is the checkpoint comparison script.
The referee was rebuilt in the Ubuntu 24.04 container from this exact source.

### Effect of the new disconnect rule on the harness

The harness never relied on the old rejoin (a quit is not a stall, and no case reconnects on purpose), but its
heavy cases do make a headless client go quiet, which the 0.8.10 server now sees and charges:

- **Stalls pause the room and are counted.** The server logged `held the game` (a hold beyond 1 s, charged to
  that seat) in only two Retail cases and one Legion case, all stress games: Retail `2h-stress` (28 holds; seat 1
  charged 32.0 s of hold time, 7 s of its 30 s budget left, seat 0 14.8 s), Retail `2h-orders-stress` (19 holds;
  seat 1 21.9 s), Legion `2h-orders-stress` (5 holds plus one "stopped responding" for 8 s that recovered, seat 0
  ended with 14 s left). None ran out, and none changed the result.
- **One seat was lost.** Legion `2h-stress` (about 11,700 units by tick 1,381): seat 0 went silent for 8 s twice
  and held the game four times (6.5 s); its budget ran out and the server logged `slot 0 stopped responding --
  its wait is spent, an AI takes over` and `player 0 stopped communicating -- AI (Normal) takes over`. The
  client printed `connection lost (peer closed) -- reconnecting` and ended with `the wait ran out: an AI took
  over your seat` at tick 1,381, and the harness counted it as `client-error`. The other seat played to tick
  18,000 against the referee with no mismatch. Before 0.8.10 that stall would have been a silent reconnect or a
  slow run (0.8.9 completed this case at tick 5,248); now it is a handover. The local machine was loaded
  (load average ~20 from the release's parallel local checks on the same cores), so the cause is a starved
  client, not an engine fault. Retried alone at the same load: `ok`, tick 5,248, hash `2467ac365aab5e5b`, units
  8,427, identical to 0.8.9 and 0.8.8. A second, separate repeat reproduced it exactly.
- **Consequence for readers of this table:** heavy Legion/stress rows on a loaded harness can lose a seat to the
  30 s budget. That is the documented limit (a client that hitches counts as a hold), not a hash problem. The
  end-of-game `slot N disconnected -- waiting` lines in every log are the headless clients quitting after the game
  ended; they are not stalls.

### Reference ticks and hashes (0.8.10 candidate)

Hash and units are the seated host's final line. "vs 0.8.9" compares tick and hash with the 0.8.9 table
(protocol 288, same harness, hosts and seeds). The six `TAK_AUTOPLAY` cases never repeat a hash, and `h-lake`,
`h-stress`, `2h-stress` and `2h-orders-stress` are load-sensitive (`4h-gods` is too: Retail matched 0.8.9 in the first pass but its repeat gave `ebad528a24b6e10f`), so "differs" in those rows is expected noise.
Legion `2h-stress` is the retry row.

| Case | Retail end | Retail tick | Retail hash | Retail units | vs 0.8.9 | Legion end | Legion tick | Legion hash | Legion units | vs 0.8.9 |
| --- | --- | ---: | --- | ---: | --- | --- | ---: | --- | ---: | --- |
| `h-baseline` | timelimit | 18,000 | `4a07030c0169e47c` | 210 | same | timelimit | 18,000 | `4b9217a4ffcd74c0` | 213 | same |
| `h-gods` | concluded | 14,626 | `0ea0a1d96c1414af` | 104 | same | concluded | 15,383 | `3f26557d500cff5a` | 82 | same |
| `h-crusades` | timelimit | 18,000 | `604de77d3fe53f3e` | 245 | same | timelimit | 18,000 | `e4036388d6f2e4df` | 230 | same |
| `h-stress` | concluded | 4,899 | `074f7ea92d92d775` | 8,593 | differs (tick 4,903 -> 4,899) | concluded | 5,161 | `60a27ba49d34492e` | 7,301 | differs (tick 5,147 -> 5,161) |
| `h-absurd` | timelimit | 18,000 | `1b0d79cb6583219d` | 454 | same | timelimit | 18,000 | `9125e38fec71fc29` | 469 | same |
| `h-fog-explored` | timelimit | 18,000 | `00419b0edd6c9b06` | 188 | same | timelimit | 18,000 | `faafb450fc7005c7` | 142 | same |
| `h-fog-full` | timelimit | 18,000 | `9896fd941b2aade9` | 216 | same | timelimit | 18,000 | `821e2ab5feef82d1` | 217 | same |
| `h-unitcap` | concluded | 14,780 | `c382486eeb54fb3b` | 204 | same | timelimit | 18,000 | `715966bbb3c6a746` | 220 | same |
| `h-cramped` | concluded | 1,307 | `45134591344a4d04` | 10 | same | timelimit | 18,000 | `57d4067d04331580` | 29 | same |
| `h-naval` | timelimit | 18,000 | `5fb53d62d7a0b42c` | 58 | same | timelimit | 18,000 | `17b9ca802fb9a217` | 88 | same |
| `h-naval-crus` | timelimit | 18,000 | `396f8de68f25c274` | 70 | same | timelimit | 18,000 | `4787a6c75fbb0d77` | 88 | same |
| `h-lake` | concluded | 3,864 | `6a70462c1b10e88b` | 4,890 | differs (tick 3,945 -> 3,864) | concluded | 4,538 | `5ebbfdd3c26f5899` | 4,791 | differs (tick 4,108 -> 4,538) |
| `h-random-starts` | concluded | 15,702 | `6e0395e042f734fe` | 228 | same | concluded | 12,289 | `0cba4154cf002db9` | 194 | same |
| `h-monarch-exp` | concluded | 12,231 | `c646ef640c32a947` | 89 | same | concluded | 17,804 | `540592e154eafb24` | 82 | same |
| `h-mods-all` | timelimit | 18,000 | `450038fec3e01ce5` | 209 | same | timelimit | 18,000 | `e3693d73ec08c60c` | 212 | same |
| `h-everything` | timelimit | 18,000 | `e6fc7a26a6d0ef0c` | 4,237 | differs (tick 13,553 -> 18,000) | timelimit | 18,000 | `afc4b4a9bfcce80c` | 4,237 | differs (tick 10,300 -> 18,000) |
| `h-speed-1x` | timelimit | 18,000 | `9b5694cceca37a11` | 236 | same | timelimit | 18,000 | `0c1cf293aa498699` | 246 | same |
| `h-two-castles` | timelimit | 18,000 | `8e9dfa2311fc1ec4` | 23 | same | timelimit | 18,000 | `81b4f5bfafb359c0` | 4 | same |
| `2h-baseline` | timelimit | 18,000 | `7b5850e128c0ff63` | 173 | same | timelimit | 18,000 | `638fa388b12be0c4` | 172 | same |
| `2h-gods` | concluded | 12,423 | `bf5aaf973f862369` | 104 | same | concluded | 13,059 | `9283d91d154a8678` | 61 | same |
| `2h-crusades` | timelimit | 18,000 | `4177d6f4374aad35` | 182 | same | timelimit | 18,000 | `e14b49adbbf76ebf` | 167 | same |
| `2h-stress` | concluded | 5,074 | `cc893dee35637579` | 9,096 | differs (tick 5,051 -> 5,074) | concluded | 5,248 | `2467ac365aab5e5b` | 8,427 | same |
| `2h-orders` | timelimit | 18,000 | `1b40e2c3ceeb0a66` | 179 | differs (autoplay) | timelimit | 18,000 | `a2d39db469bf9b9e` | 175 | differs (autoplay) |
| `2h-orders-stress` | concluded | 13,590 | `c690f5cf79f7969e` | 7,562 | differs (tick 5,114 -> 13,590) (autoplay) | timelimit | 18,000 | `32a879dcd6d197cc` | 5,166 | differs (tick 5,074 -> 18,000) (autoplay) |
| `4h-orders` | timelimit | 18,000 | `e447b3c6416db6e9` | 159 | differs (autoplay) | timelimit | 18,000 | `40e9ec2f6ecfd65b` | 155 | differs (autoplay) |
| `3h-baseline` | timelimit | 18,000 | `1d59d944a244aef4` | 172 | same | timelimit | 18,000 | `beeeab4bb6ec584b` | 148 | same |
| `4h-gods` | timelimit | 18,000 | `39e8fa7afa49b6f9` | 149 | same | timelimit | 18,000 | `0138eb2fd52d3de6` | 142 | differs |
| `net-rtt50` | timelimit | 18,000 | `80dc6a72233bd05d` | 230 | same | timelimit | 18,000 | `03c521246aeadec2` | 227 | same |
| `net-rtt100` | timelimit | 18,000 | `fcba4a56baf98c45` | 190 | same | timelimit | 18,000 | `c3167c3d271f43bc` | 204 | same |
| `net-jitter` | timelimit | 18,000 | `6a0f640c3faf1de6` | 173 | same | timelimit | 18,000 | `ca07f9821f97109e` | 196 | same |
| `net-rtt500` | timelimit | 18,000 | `8837809d723b6fc0` | 219 | same | timelimit | 18,000 | `af7b2fa90b652283` | 200 | same |
| `net-loss1` | timelimit | 18,000 | `1adf2c3d366e9bef` | 192 | same | timelimit | 18,000 | `b69a197764ecdf5c` | 206 | same |
| `net-loss3-orders` | timelimit | 18,000 | `ff46fd960aa71801` | 190 | differs (autoplay) | timelimit | 18,000 | `7e269b8810b6950c` | 222 | differs (tick 17,529 -> 18,000) (autoplay) |
| `net-orders100` | timelimit | 18,000 | `4c1133050348eddb` | 183 | differs (autoplay) | timelimit | 18,000 | `91d8f23982d1be6d` | 187 | differs (autoplay) |
| `net-2h-rtt100` | timelimit | 18,000 | `92399c1fe9e944cb` | 190 | differs (autoplay) | timelimit | 18,000 | `abe8a0935c0131cb` | 168 | differs (autoplay) |
| `w-allai-stress` | timelimit | 18,000 | `06e78e00b732019a` | 5,392 | same | timelimit | 18,000 | `fc6dfe90da499b44` | 3,925 | same |
| `w-allai-bench` | timelimit | 18,000 | `10a3a871daabf505` | 312 | same | timelimit | 18,000 | `8446871154cf8f79` | 344 | same |

### Comparison with 0.8.9

0.8.10 changes the simulation (retail terrain-scaled turn rate in target scoring, `e19580b9`), the network
protocol (289: the disconnect rule and the campaign seat rule) and the replay loader (288 replays are refused),
so it is not multiplayer- or replay-compatible with 0.8.9, and some references were expected to move.

**Retail: 27 of 37 cases reproduce 0.8.9 exactly (tick and hash)**; the 10 that differ:

- The six `TAK_AUTOPLAY` cases (`2h-orders`, `2h-orders-stress`, `4h-orders`, `net-orders100`, `net-loss3-orders`,
  `net-2h-rtt100`), which never repeat a hash.
- Load-sensitive, same class as in 0.8.9: `h-stress` (4,903 -> 4,899; a repeat gave 4,891), `h-lake` (3,945 ->
  3,864; a repeat gave 3,833) and `2h-stress` (5,051 -> 5,074; a repeat gave 5,049). Units are within 1% of
  0.8.9 in each (8,593 / 4,890 / 9,096 vs 8,593 / 4,922 / 9,101); these cases ended by a concluded result a few
  ticks apart, the same wobble 0.8.8 -> 0.8.9 showed.
- **`h-everything` moved for real:** 0.8.9 concluded at tick 13,553 with 5,140 units; 0.8.10 runs to the time
  limit at tick 18,000 with 4,237 units, hash `e6fc7a26a6d0ef0c`, and a repeat reproduced exactly that hash
  (it is stable, not noise).

**Legion: 27 of 37 cases reproduce 0.8.9 exactly.** The 10 that differ are the six autoplay cases plus
`h-stress` (5,147 -> 5,161 in the first pass; the repeat gave 5,147 and hash `4efaa19f898d273d`, i.e. 0.8.9 exactly),
`h-lake` (4,108 -> 4,538; a repeat gave 4,532), `4h-gods` (same 18,000 ticks; hash differs, as it already did between 0.8.8 and 0.8.9; a repeat gave yet another hash, `cac7daae4618b419`) and **`h-everything`**, which moved for real the same way as Retail: 0.8.9 concluded at tick 10,300 with
4,782 units; 0.8.10 reaches the limit at 18,000 with 4,237 units, hash `afc4b4a9bfcce80c`, reproduced exactly on a
repeat. `2h-stress` (the lost-seat case) reproduced 0.8.9 after the retry.

**Why `h-everything` moved, and why only it.** The turn-rate change affects only a shooter's turn penalty in
target scoring, and only for units whose movement class scales their turn rate on roads or water (retail's
`roadmultiplier` / `watermultiplier`). `h-everything` is the one case that fields every unit type, so it is the
case where such shooters change which targets they pick, and the whole battle diverges after that point (both
modes, same direction: no conclusion before the time limit, about 900 fewer units). Every other deterministic case
(27 of the 31 non-autoplay Retail cases are bit-identical, including the naval cases `h-naval` and
`h-naval-crus`) evidently never reaches a differing score. This attribution rests on
(a) the change being the only simulation delta since 0.8.9 and (b) two exact reproductions of the new
references in each mode; I did not bisect `h-everything` against the pre-`e19580b9` commit, so say "consistent
with" rather than "proven" if the notes quote it. The `--mpai` Inner Circle 60 s hashes are unchanged, as the
commit says; the 300 s hashes are measured separately by the local checks.

There is no desync anywhere: every seat agreed with the referee at every checkpoint in every case that
completed, and the one case that lost its seat agrees with the referee on the seat that stayed.

## CI and packages

Main CI on the final source `66055338`: [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/39853029630), [Linux packages](https://github.com/pocketgeek/tak-engine/actions/runs/39853029616) and [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/39853029594) passed on the first attempt. [Determinism](https://github.com/pocketgeek/tak-engine/actions/runs/39853029625) passed on attempt 2. On attempt 1 its `sim-equiv` job hung for 28 minutes in "Install build deps" (the GitHub runner hang): it was force-cancelled (`gh api -X POST .../force-cancel`) and `gh run rerun --failed` passed. `detmath` and `unit-tests` had passed on attempt 1.

On the exact tag (`66055338`, annotated tag object `de785d73`), all four workflows passed. macOS needed one rerun for a known flake: on attempt 1 the x64 job failed in "App bundle + DMG" with `hdiutil: create failed - Resource busy`, so both **Sign + notarize** jobs were skipped. `gh run rerun --failed` passed on attempt 2, including both signing jobs. No other job failed, so none of the other known flakes appeared: macOS `stream_test` timing (x64 and arm64), "Upload CI artifact: ENOTFOUND", the Fedora openh264 mirror, the Slackware mirror `md5sum` error, `gitlab.freedesktop.org` or `downloads.xiph.org` connect failures, the Ubuntu 22.04 `crusades_client_network_test` "complete replay not published", and the runner hang in "Install build deps".

- [Determinism and core tests](https://github.com/pocketgeek/tak-engine/actions/runs/39857683568): started manually on the tag because the workflow is path-filtered. `unit-tests` (8/8), `detmath` and `sim-equiv` passed. The GCC and Clang x86-64 builds at O0/O2/O3 all agree on golden `dcef618cd2e4d558` (aarch64 skipped), the same as 0.8.9. This workflow runs no `--mpai` game.
- [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/39857684029): all 16 native package jobs and the release attachment, first attempt.
- [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/39857683792): x64, ARM64 and the x64 signing/package job, first attempt.
- [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/39857683818): both builds plus the **Sign + notarize** job for each architecture (attempt 2). That job replaced the unsigned macOS ZIPs and DMGs in the draft with signed, notarized and stapled ones.

The release was created as a draft with these notes, and tagged CI attached the packages. All **29 assets** were downloaded after the macOS signing finished.
- They are the same set as 0.8.9 with the version swapped. None has size 0 or an upload state other than "uploaded". Each one matched GitHub's SHA-256 digest and byte size before publication.
- Every release asset is 0.41% to 0.77% larger than its 0.8.9 counterpart (+76 KB to +266 KB). The debug archives grew 0.37% to 0.58% (+157 KB to +468 KB).
- The release `takclient` grew by 196 KB to 277 KB across platforms, which is the MP4 muxer in FFmpeg plus the local-copy code. The release `takserver` grew by 4 KB to 70 KB (disconnect wait).
- There are no new dependencies. Debian and Ubuntu packages still depend only on `libc6`, RPMs require only libc, libm and the loader, and the Windows import tables are identical to 0.8.9's.
- Each package, installer and ZIP ships the same file list as its 0.8.9 counterpart, and the DMGs hold the same two apps and `Applications` link.

- **Build ids.** The client and server in every Linux package carry `v0.8.10`. That covers Debian 12 and 13, Ubuntu 22.04, 24.04 and 26.04, Fedora 44 and openSUSE Leap 16.0 (each for both architectures), plus Arch, Slackware and both Linux debug tarballs. Installer, DMG and ZIP names carry `0.8.10`.
  - On Linux, the x86-64 `takclient` and `takserver` report `0.8.10 (build v0.8.10)`. That holds for Debian 12 and 13, Ubuntu 22.04, 24.04 and 26.04, Fedora 44, openSUSE Leap 16.0, Slackware and the x64 debug tarball. So does the Arch `takserver`. The Arch `takclient` needs glibc 2.44, newer than the checking host's 2.43, so only its build string was checked. For the arm64 binaries, only the build string `v0.8.10` was checked.
  - On Windows 11, the x64 Windows ZIP's `takclient.exe` and `takserver.exe` report `0.8.10 (build v0.8.10)`. For the ARM64 `takclient.exe` and `takserver.exe`, and for the installer's, only the build string `v0.8.10` was checked.
  - On the arm64 MacBook (macOS 26.6.2), `takclient` and `takserver` report `0.8.10 (build v0.8.10)` for both architectures. This holds for the bare programs from the ZIPs and for the copies inside the game app from each DMG and each ZIP. The x64 copies ran under Rosetta.
- **Windows signatures.** `Get-AuthenticodeSignature` on oden-win reports **Valid** (CN=Curtis Edge, timestamped) for 9 signed files, the same count as 0.8.9. They are the setup `.exe`, plus `takclient.exe`, `takserver.exe`, `cartographer.exe` and `crusades_admin.exe` in both the x64 and ARM64 ZIPs. The debug ZIPs were not checked.
- **macOS notarization.** On the MacBook, both DMGs pass `xcrun stapler validate` and `spctl` (open, primary signature) as **accepted, source=Notarized Developer ID, origin=Developer ID Application: Curtis Edge (L479Y59RN8)**.
  - Both apps (the game and TAK Cartographer) pass both checks the same way, from each DMG and from each ZIP.
  - Every bundled and bare `takclient`, `takserver`, `cartographer` and `crusades_admin` passes `codesign --verify --strict` (TeamIdentifier L479Y59RN8).
  - `spctl` says the bare command-line programs do not seem to be an app. That is expected for unstapled command-line tools.
- **macOS Test Map.** On the MacBook, in a scratch folder with a scratch `HOME` and the retail data folder, the check covered four layouts: both apps copied out of each DMG side by side, and each ZIP extracted.
  - A probe linked from the tag's own `cartographer/playtest.cpp` objects ran `cart::Playtest::clientPath` on `TAK Cartographer.app/Contents/Resources/` (the base path SDL reports inside a bundle) and on `Contents/MacOS`.
  - In all four layouts, both lookups resolve to `Total Annihilation - Kingdoms.app/Contents/MacOS/takclient` beside the Cartographer app. That client exists and reports `0.8.10 (build v0.8.10)`.
  - The packaged Cartographer then wrote a snapshot headless (`cartographer --data <retail> "Inner Circle" --bundle "Cartographer Test.kmp"`). The resolved client was started with the same arguments Test Map passes (`--data <retail> --play-map <snapshot>`, `SDL_VIDEODRIVER=dummy`).
  - In all four layouts it started the bundled `takserver`, created the private 'Single Player' game, verified the map and connected as `player`. It was still running after 15 s and was then stopped by PID.
  - The Test Map menu itself was not clicked; this was not a GUI run.
- **Streaming local copy in packages.** Every shipped `takclient` contains FFmpeg's MP4 muxer: 29 copies across all Linux packages and tarballs, the Windows installer and ZIPs, and the macOS ZIPs, debug ZIPs and the game apps from both DMGs and both ZIPs.
  - Each has the muxer's `video/mp4` MIME string and the `frag_keyframe` movflags option, next to the FLV muxer's `video/x-flv`. Every 0.8.9 `takclient` has `video/x-flv` but neither MP4 string, which shows the check can fail.
  - Functionally, the tag CI's "Streaming encode test" ran `stream_test` against the same static FFmpeg build that is linked into the packages, on all 16 Linux package jobs and on the Windows x64, Windows ARM64, macOS x64 and macOS arm64 builds.
  - Each run wrote a fragmented local copy (`local copy .../videos/TAK-<date>-<time>.mp4: 2 fragments, video 73-76 samples 2.56s, audio 2.53-2.55s`). No run printed the `SKIP local copy` line, and under `CI` that test fails rather than skips when the muxer is missing.
- **Opus is static.**
  - No packaged Linux `takclient` or `takserver` has a `NEEDED` entry beyond libc, libm and the loader (Slackware also lists libdl and libpthread), so none needs libopus.
  - No Windows `.exe` imports or names an Opus DLL, and the ZIPs ship no DLLs (the installer has only its NSIS plugin DLLs). The x64 import tables list only system DLLs.
  - No macOS binary links a libopus dylib (`otool -L`).
  - `OPUS-LICENSE.txt` is in every Debian, RPM, Arch and Slackware package, in both Linux debug tarballs, in the Windows installer, in all four release ZIPs (inside the game app's Resources on macOS) and in the four debug ZIPs.
- **Font licences.** `CHAT-FONT-LICENSE.txt` sits beside `EDITOR-FONT-LICENSE.txt` in every place the game ships:
  - every Debian, RPM, Arch and Slackware package (`share/licenses/tak-engine`) and both Linux debug tarballs;
  - the Windows installer, both Windows release ZIPs and both Windows debug ZIPs;
  - both macOS debug ZIPs;
  - the game app's `Contents/Resources` from both DMGs and both ZIPs.

  As in 0.8.9, TAK Cartographer's app has only `EDITOR-FONT-LICENSE.txt`, which covers its Noto Sans Mono editor font.
- **Mods folder.** No package, installer, ZIP, DMG or app contains an `overrides/` or `mods/` directory, and none contains `click.hpi`. This is the same as 0.8.9.
- **macOS microphone.** The game app's `Info.plist` carries `NSMicrophoneUsageDescription` in both DMGs and both ZIPs. The `com.apple.security.device.audio-input` entitlement is on the game app's `takclient` (DMG and ZIP, both architectures) and on the bare ZIP `takclient`. It is absent from `takserver`, `crusades_admin` and `cartographer`.
- **macOS Retina.** `NSHighResolutionCapable` is `true` in the `Info.plist` of both apps (the game and TAK Cartographer), from both DMGs and both ZIPs.
- As in 0.8.1 to 0.8.9, no packaged Linux `takclient` has a `NEEDED` entry for libsecret, GLib, GIO or GObject.
- `docs/release-0.8.10-notes.md`, this report, `docs/img/streaming.jpg` and `docs/img/guide/disconnect-wait.jpg` are in the source tree at the tag. `kNetVersion` there is 289.

Published 2026-10-22 22:21 UTC with `gh release edit v0.8.10 --draft=false --latest`; the API's latest release is `v0.8.10`, with 29 assets.

## Screenshots

Retaken in `c2ec34ab` (cherry-picked from the shots branch's `b0359921`):
`docs/img/title.jpg` (title menu reading VERSION 0.8.10, 0.8.9's crop),
`docs/img/streaming.jpg` (YouTube Streaming panel with the new SAVE A LOCAL COPY
row, off; key NOT SET from the test key store) and
`docs/img/guide/disconnect-wait.jpg` (retaken at the normal 8 s stall threshold:
"WAITING FOR Brannoc 18"; the user-guide caption says 18). README title alt
text, streaming alt text and gallery footnote ("0.7.25 to 0.8.10 builds"),
and `docs/img/README.md` (0.8.10 paragraph, `title.jpg`/`streaming.jpg` rows, new
`guide/disconnect-wait.jpg` row, capture-script list) are updated. Campaign and
Cartographer pictures show nothing 0.8.10 changed and were kept.

## Limits

- No game mixing 0.8.10 with 0.8.9 or older was attempted: the protocols differ,
  so the server refuses it.
- All of 0.8.9's limits carry over: Legion units stand and wait more than
  Retail's; Legion attackers through a narrow gate deal more damage than
  Retail's; power codes are lockstep (retail applied them only on the typer's
  machine); FULL VISION does not switch LOS off at start; UI FONT CLASSIC is not
  pixel-identical with 0.8.7; the Legion bridge lottery; the Legion 10,000
  workloads' congestion and tick budget, mixed-10000's hovering flyers; Retail
  `h-cramped` ends early; the macOS Retina UI Scale is migrated once; a
  stationary auto-targeting shooter keeps refusing a once-blocked target; the
  campaign book's chapter numeral touches "APTER".
- The macOS Cartographer Test Map limit is fixed (`23ecea9c`).
- New with the disconnect wait: a server-side upload stall longer than 1 s is
  charged to every seat it holds up; a reconnecting seat that hitches while
  catching up is charged as a hold.
- Flakes seen in the preparation runs (none is a code fault, no source changed):
  `voice` in the loaded Release suite (passed alone); `cartographer_ui_smoke`,
  `mod_transfer` and `crusades_history_ui_network` in the loaded `-O0` Debug
  suite (passed alone and at `-O2`); Legion `2h-stress` in the remote sweep lost
  its seat to the 30 s budget on the loaded harness (the documented hitch
  caveat; passed on retry, identical to 0.8.9).
- Flakes seen in CI (none is a code fault, no source changed):
  - Main CI on `66055338`: Determinism's `sim-equiv` job hung for 28 minutes in "Install build deps" (the GitHub runner hang). It was force-cancelled, and `gh run rerun --failed` passed.
  - Tag CI: the macOS x64 job failed in "App bundle + DMG" with `hdiutil: create failed - Resource busy`, which also skipped both Sign + notarize jobs. One `gh run rerun --failed` passed everything.
  - No other CI job failed.
