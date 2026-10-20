# 0.8.6 release validation

Protocol **281** (0.8.5: 278), replay format **13**, generator version **9**,
campaign payload **4**, database schema **9** (replay, generator, campaign and
schema unchanged since 0.8.5).
<!-- LEAD: re-check kNetVersion right before tagging.
     Version 0.8.6 and protocol 281 are already written in: CMakeLists.txt
     (project VERSION); README.md (badge, title alt text, gallery footnote,
     New in 0.8.6, random maps x2, multiplayer, overrides); docs/user-guide.md
     (intro, Versions, Replays, public server, old replays);
     docs/public-server.md; docs/random-map-generation.md (header line);
     docs/multiplayer-design.md (protocol summary, 279-281 replay history, the
     required-protocol line); these notes and the release notes.
     docs/img/README.md belongs to the screenshots branch. If kNetVersion moves
     past 281, update every one of them. -->

<!-- LEAD: tag line, e.g. "Tagged `v0.8.6` at `<sha>` (annotated tag object `<obj>`) and published on <date> UTC." -->
**PLACEHOLDER (lead): tag and publication line.**

## Scope and compatibility

0.8.6 follows 0.8.5 (protocol 278). After the 0.8.5 validation commit
`216feeed`, main reached `575a70d2` in 27 commits: 20 changes and 7 merges of
the task branches. `git diff --stat v0.8.5..575a70d2 -- src` touches 16 files:
`src/client/` (11), `src/sim/` (4) and `src/net/` (1). Nothing under `src/ai/`,
`src/server/`, `src/campaign/`, `src/tnt/`, `src/cartographer/`,
`src/terrain/`, `src/cob/`, `src/crt/` or `src/util/` changed.

After `575a70d2` the preparation added only the screenshots (`d5b7c2c2`), the
documentation with the CMake version bump to 0.8.6 (`75d4b6bc`, `b1e1f1d8`) and
this report. No source, test or baseline file changed, so the checks below, run
on `575a70d2`, stand for the release source.

The changes:

- **279** Legion at ~100 bodies, round 1 (landing `48c5b05c`; `1848af7c`,
  `5d697081` one admission per narrow passage, `659ce752` head-on meets near
  goals, `906339c1` friendly step-aside and brisk detours for departures,
  `7827f6e8`, `8e60cc60` attack-ring upkeep, press-in and ringcross; cost
  baseline `4fe78bdb`). Legion only.
- **280** Legion at ~100 bodies, round 2 (landing `aacb1f2c`; `0c820175`,
  `53863b04` sub-cell presses and ring bends, `2d29aecd` passage lanes through
  wide gaps, `96f301f6` flow-around at travel speed). Legion only.
- **281** blocked-shot repositioning, all pathfinding modes (`c74c7f4e`,
  `94fb3b17`, protocol bump `575a70d2`): `World::noteShotImpact`,
  `shotBlockedHere`, `repositionForShot`, `Unit::searchFailed`; the state is
  folded into `World::stateHash()` only once a blocked impact occurs. A
  documented deviation from retail (`docs/retail-engine.md`).
- Client only: voice chat TALK MODE / ACTIVATION LEVEL (`aab3a14f`,
  `voice::VoiceGate`, settings `voiceActivation`, `voiceActivationLevel`);
  enemy-hover cursor by retail selector `0x4dd780` (`f72fb389`, with
  `cf35db0c` restoring Attack orders for every selected unit).

**Retail is expected to be bit-identical to 0.8.5 except where a shot is
blocked** (279 and 280 are Legion only). `--mpai` Inner Circle seed 1 on
`575a70d2`, from the landing commit: Retail 60 s `b353410003e98dd5`, 180 s
`fe47c6bf443c5d37`; Legion 60 s `0825aeb9e8139203`, 180 s `1e87f71790f15023`,
all equal to 0.8.5.
Confirmed by the local checks on `575a70d2` (300 s also unchanged: Retail
`a3d953803a064191`, Legion `ce5c4ba7562e53b9`).

**Compatibility.** `kNetVersion` is **281** in `src/net/protocol.h` at
`575a70d2`, against 278 at `v0.8.5`. `kReplayFormat` is **13** in
`src/net/replayhdr.h`, the map generator's `formatVer` **9**
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` 4
(`src/net/crusades.h`) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`), all unchanged.

- **Multiplayer.** The server's Hello check rejects a client whose protocol
  differs from `kNetVersion`, so **0.8.6 cannot play with 0.8.5 or earlier in
  either direction**. The server and all players must update together.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  protocol equals `kNetVersion`, so 0.8.6 does not play 0.8.5 or older replays
  and those builds do not play 0.8.6 replays, although the file format is still
  13. `tools/replay_test.cpp` pins `kNetVersion == 281` and refuses 280, 279
  and 278.
- **Maps and settings.** Saved `.kmp` maps and generator-8/9 recipes carry
  over. A 0.8.5 settings file reads the same in 0.8.6; new keys
  `voiceActivation` (default off, Push to Talk) and `voiceActivationLevel`
  (default 15). 0.8.5 drops them when it saves.

## Local preparation checks

All checks ran from a clean detached worktree (`~/tak-tmp/wt/release-086-local`) at
`main` `575a70d2ff7c` (protocol **281**, replay format **13**, generator recipe **9**, campaign
payload 4, database schema unchanged since 0.8.5), with `assets` and `third_party` links to the
main checkout. Toolchains: GCC 16.2.1, Clang 22.1.8, CMake 4.3.0, Ninja 1.13.2, Python 3.14.8.
Builds and tests ran on cores 4-23; the owned retail install served as `--data` and
`TAK_TEST_DATA`. The version bump is not in this commit, so binaries still call themselves
`0.8.5 (build v0.8.5-28-g575a70d2ff7c)` (no `-dirty`).

**Result: no real regression in the engine or the tests. One expected, known gate failure:
the Legion scenario baseline (`tools/scenarios/baseline.json`) has not been retaken since
Legion rounds 1 and 2, so its Legion keys fail (57 nightly `legion_check_*` tests; the
scenario gate below).** Everything else passed. Eight load or port-collision failures in the
unoptimized suites each passed when rerun alone.

- **Multiplayer and replays.** `kNetVersion` is **281** in `src/net/protocol.h` (278 in
  0.8.5). Replay format stays 13; the replay loader requires protocol == `kNetVersion`, so
  0.8.5 replays do not play and 0.8.5 cannot join a 0.8.6 server.
- **Builds.** Release, Debug, optimized Debug (`-O2 -g`), Clang Debug and AddressSanitizer
  all-target builds completed with exit code 0: 873 steps in Release, 874 in the other four
  (869/870 in 0.8.5). ccache and mold; static-deps and static Bink FFmpeg prefixes. Release
  emits 937 warning lines (538 `-Wmissing-field-initializers`, 332
  `-Wdeprecated-declarations`), the same kinds as 0.8.5 (about 850). New since 0.8.5, all
  harmless: `-Wunused-parameter` `dt` in four `sim.cpp` functions, `-Wunused-but-set-variable`
  `band0` at `legion.cpp:9539`, and in `tools/legion_world_test.cpp` two `%zu` format
  warnings (lines 1134, 1167) and an unused `sz` (line 3107).
- **Full CTest.** **696** registered tests in the Debug trees (678 in 0.8.5) and **658** in
  Release (640); 18 added: `legion_widegaplanes`, `legion_ringupkeep`, `legion_attackwalled`,
  `legion_ringcross`, `legion_pressin`, `legion_ringfill`, `legion_ringbend`,
  `legion_ringbend_pocket`, `legion_passage_admission`, `legion_passage_taper`,
  `legion_meetnear`, `legion_departure`, `legion_flowshuffle`, `legion_flowshuffle_idle`,
  `blocked_shot`, `legion_blocked_shot`, `cursor_enemy`, `legion_passage_records`. Each tree
  ran as two ctest selections: `-LE nightly` and `-L nightly` (the nightly selection was run
  in Release and optimized Debug; the other trees' `legion_check_*` outcomes are identical
  by construction and were not repeated).

  | Tree | Selection | Result | Time | Failures |
  | --- | --- | --- | ---: | --- |
  | Release | `-LE nightly`, 496 | **496/496** | 538 s | none |
  | Release | `-L nightly`, 164 | 107/164 | 1,400 s | 57 `legion_check_*` (baseline, see gate) |
  | Optimized Debug | `-LE nightly`, 534 | **534/534** | 1,015 s | none |
  | Optimized Debug | `-L nightly`, 164 | 107/164 | 2,594 s | the same 57 `legion_check_*` |
  | Debug (`-O0`) | `-LE nightly`, 534 | 531/534 | 6,094 s | `terrain_cache`, `replay_always`, `crusades_history_ui_network` (load; pass alone) |
  | Clang Debug | `-LE nightly`, 534 | 532/534 | 5,992 s | `override_transfer`, `replay_always` (load; pass alone) |
  | ASan | `-LE nightly`, 534 | 532/534 | 3,759 s | `override_transfer`, `server_seed` (load / port; pass alone) |

  (The 534 figure includes the `pref_guard` fixtures.) The unoptimized trees and the gate ran
  at the same time (load average 12-25 on 20 cores), and the reruns used reserved cores.
  - **The 57 `legion_check_*` failures are new since 0.8.5 (0.8.5 had 640/640) and are the
    stale-baseline set, not new defects.** They are identical in Release and optimized Debug
    (same 57 names, 0 timeouts). All 57 are Legion keys; **no Retail key fails in any of
    them.** See the scenario gate below for the classification. Slowest: `legion_check_tail-corner380`
    949 s / 1,586 s and `legion_check_pocket-615-open` 858 s / 1,708 s (Release / optimized
    Debug, the latter under 1800 s limit only because the machine was loaded).
  - **`terrain_cache` (Debug only, new this run): load.** "FAIL: a jump draws completely from
    re-uploaded sections" (118,756 hole px) while 23 other tests ran; same family as
    `terrain_schedule` (frame-time dependent). Passed alone (rerun, Debug), and in all other
    trees. `terrain_schedule` itself passed (64 / 91 / 103 s Debug / Clang / ASan).
  - **`replay_always` (existing load-sensitive): Debug and Clang Debug** in the full runs,
    passed alone (Clang 37 s, Debug rerun). It passed in ASan, Release and optimized Debug.
  - **`crusades_history_ui_network` (existing load-sensitive, Debug):** passed alone.
  - **`override_transfer` (Clang Debug and ASan, new candidate): load.** Both full runs failed with
    `OVERRIDE PACKS VERIFIED; SAVE FAILED: filesystem error: cannot create directories: Not a
    directory [.../peer/OverrideCache]`; it passed alone in both trees (27 s, 21 s) and passed
    in Release, optimized Debug and Debug full runs. Neither the test nor `overridepackage.cpp` changed
    since 0.8.5 (`git log 86db5d61..575a70d2` shows only the protocol bumps in `src/net`).
    Worth a look if it repeats on a quiet machine.
  - **`server_seed` (ASan): port collision.** `takserver: bind failed on port 60433` (the
    random test port was taken by a concurrent test); passed alone.
  - **The unoptimized Legion tests passed under the 0.8.4 limits.** `legion_sched_inflight`
    1,180 s / 790 s / 869 s, `legion_scale_neutral` 1,261 s / 752 s / 988 s,
    `legion_route_paused` 1,555 s / 1,077 s / 651 s and `legion_parallel` 3,960 s / 3,133 s /
    2,712 s (Debug / Clang Debug / ASan); the largest use of a limit is `legion_parallel` in
    Debug, 73% of 5400 s. `movement_orders_trolls` 524 s / 363 s / 265 s (limit 600 s: Debug at
    87%). `voice`, `server_voice*`, `voice_uplink` (49 s) and the new `blocked_shot`,
    `legion_blocked_shot` and `cursor_enemy` passed everywhere.
  - **The settings-leak guard held.** `pref_guard_setup`/`pref_guard_check` passed in every
    selection. The real profile's `settings.ini` has the same SHA-256 and mtime before and
    after (`real-profile-{before,after}.txt`). `TAK_SETTINGS_FILE` was not exported.
- **`--mpai` golden hashes: all unchanged from 0.8.5.** Each ran once on the optimized Debug
  build, Inner Circle, `takserver --local --no-auth --seed 1`, an empty `XDG_DATA_HOME`, and
  `TAK_LEGION=1` for Legion. No desync or referee-suspect line in any log.

  | Mode | Duration | `575a70d2` (281) | 0.8.5 (278) |
  | --- | ---: | --- | --- |
  | Legion | 300 s | `ce5c4ba7562e53b9` | `ce5c4ba7562e53b9` |
  | Retail | 300 s | `a3d953803a064191` | `a3d953803a064191` |
  | Legion | 180 s | `1e87f71790f15023` | (not recorded) |
  | Retail | 180 s | `fe47c6bf443c5d37` | (not recorded) |
  | Legion | 60 s | `0825aeb9e8139203` | `0825aeb9e8139203` |
  | Retail | 60 s | `b353410003e98dd5` | `b353410003e98dd5` |

  The 180 s values equal the ones expected for this release. Optimized Debug binary SHA-256:
  - client `6b75218d99a4abce67d5af18eed604c763e7e4b5c050af5f92384369f562eb02`
  - server `205dca2cb00f1170a9789a0c2fdbf711131579c4f8804b31027e44f08c17b35a`
- **Determinism.** In `tools/check-determinism.sh`, the GCC and Clang x86-64 builds at
  O0/O2/O3 all agree on `dcef618cd2e4d558`, the same as 0.8.5. The aarch64 legs are skipped
  (target headers missing). `tools/check-detmath.sh` passes, including the `std::atomic_ref` guard.
- **Legion scenario gate: FAIL against the committed baseline (expected, see below).** The
  gate ran the way `tools/legion_nightly.sh` stage 2 does: every `tools/scenarios/*.scn`
  (137 files, 135 in 0.8.5), optimized Debug `legion_scenario`, `--mode both --json --offsets
  gate --data <retail install>`, one file per core on cores 4-11, then `legion_check.py check
  --cumulative --baseline tools/scenarios/baseline.json`. All 137 files exited 0 (`problems.txt`
  empty); `legion_check.py validate --baseline tools/scenarios/baseline.json` reports `PASS: 0
  problems`. Run time 4,095 s.
  - **Result:** `FAIL: 31859 keys checked, 3205 failed, 4070 ratchets, 2313 licensed, 220 floor
    exceptions, 864 report-only` (0.8.5: 0 failed, 3005 ratchets, 3333 licensed).
  - **All 3,205 failures are Legion keys, in 116 of the 137 fixtures; 0 Retail keys fail**, so
    Retail is unchanged on every fixture (all Retail `hash@` keys equal the baseline). The
    Legion `hash@` keys moved (624 in the ctest subset alone) because rounds 1 and 2 (protocol
    279, 280) changed Legion behaviour and the baseline was not retaken; the round 2 notes in
    `docs/legion-pathfinding.md` already record that the gate "failed at the parent on 115 of
    its 136 fixtures (round 1's behaviour, not retaken there)", and that the 57 nightly
    `legion_check_*` fixtures fail on the parent too. Protocol 281 (blocked shots) adds nothing
    new here: the failing set is the same 57 / 116, and Retail is bit-identical.
  - **Failures that are not hash-only (to be judged when the baseline is retaken).** Mostly
    movement-metric drifts under the round 1/2 trades (wall touch, detour/sideways counts,
    work counters, `churn.*`). Four look like worsenings rather than trades and are the ones to
    look at: `attackring-r200-64` `statue_ticks` 290 -> 20,977 (Retail 155; ring in reach
    52/64 as documented, Retail 64), `attackring-f1-16` `statue_ticks` 0 -> 1,669 (Retail 2),
    `attackring-wall-64` `g.A.arrived`/`complete_n` 2 -> 0 and `attackring-struct-60`
    `g.T.arrived` 1 -> 0, plus `deadend-w2-n40-room-plain`
    `complete_n` 38 -> 32. These are all attack-ring fixtures or dead-end rooms where firing
    bodies hold position; no file crashed or hung (every file exited 0). I did not bisect them to a protocol, so whether `statue_ticks` is an
    intended trade of ring upkeep is the lead's call.
  - No baseline was regenerated.
- **Retail group emulation check (formation-air).** `python3 -B tools/re/check_retail_group.py`
  (400 cases) passed every section: tick 400, query 4000, check 2000, formation 2000,
  vtolcheck 2000, vtolform 2000 (228 s).
- **Python research suite.** **165 tests** passed (`python3 -B -m unittest discover -s tools/re
  -p '*_test.py'` with `TAK_AUTH_RETAIL_DIR` set and `build/` pointing at the Release tree), no
  skip. The owned-retail archive verifier (`verify_official_hpi_test.py`) passed all **6
  tests**. The SHA-256 sums of the 16 retail HPI archives and `Kingdoms.key` are identical
  before and after the runs.
- **Syntax checks.** All **464** tracked Python files compile. All 22 `*.sh` scripts
  (21 tracked plus `tools/tak-netem`) pass `bash -n`.

Logs: `~/tak-tmp/scratch/release-086/` (`cfg-*.log`, `make-*.log`, `build-status.txt`,
`ctest-*.log`, `rerun-{dbg,clang,asan}.log`, `failed-{rel,o2}.txt`, `mpai/`, `mpai.sh`, `gate/`
(`scenarios.jsonl`, `check.txt`, `validate.txt`), `check-determinism.log`, `check-detmath.log`,
`check_retail_group.log`, `python-suite.log`, `verifier.log`, `pycompile.log`, `bashn.log`,
`o2-sha256.txt`, `retail-sha-{before,after}.txt`, `real-profile-{before,after}.txt`).

## Remote multiplayer sweep

The full 37-scenario table passed for both pathfinding modes, Retail and Legion, on
**2026-10-20** (UTC), both on `main` at `575a70d2ff7c` (protocol **281**, replay format
13, build id `v0.8.5-28-g575a70d2ff7c`; the version string still reads 0.8.5 until the
0.8.6 bump lands):

- **Retail:** 02:56-03:14 UTC (17 minutes including the gates).
- **Legion:** 03:14-03:41 UTC (27 minutes including the gates).

Referees ran on `tak.pgnet.us` (24 runs, 10 at a time) and `vpn3.pgnet.us` (13 runs, 6
at a time), with optimized local clients pinned to CPUs 4-23. Each scenario allowed ten
simulated minutes (18,000 ticks) or ended at an actual result for that seat. Real seated
players provide the client/referee checksum coverage; the two all-AI spectator scenarios
per mode (`w-allai-stress`, `w-allai-bench`) test flow control, not hashes.

The harness picks the mode through the hosting client's room options: `TAK_LEGION=1`
creates a Legion room, and without it the room is Retail. Every recorded referee replay
header confirms the intended mode, replay format 13 and protocol 281: 37 headers per
mode, seeds 2000-2036 each mapped to its case.

| Mode | Completed scenarios | Completed seats | Client-pair checkpoints | Client-referee checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Retail | 37/37 | 52/52 | 10,293 across 22 seat pairs, 0 mismatched | 2,627 across 50 seat/referee comparisons, 0 mismatched |
| Legion | 37/37 (single pass, no retries) | 52/52 | 10,662 across 22 seat pairs, 0 mismatched | 2,753 across 50 seat/referee comparisons, 0 mismatched |

Each run passed all three `--validate` gates before its table started:

- **Build identity:** `both at v0.8.5-28-g575a70d2ff7c`.
- **Planted desync:** the referee reported `game 1: client 1 DESYNCED at tick 900`.
- **Override transfer:** `verified packs transferred and incorrect loaded gameplay rejected`.

The gate checks only `tak.pgnet.us`; the binary on `vpn3.pgnet.us` has the same SHA-256
and the same `--version` build id (checked by hand after deployment). No natural
`DESYNCED`, `REFEREE SUSPECT` or client-reported desync appeared in any case log; the
only such line is the planted one in `validate.server.log`. Both sweeps ended with "no
desyncs reported (52 seats, all completed)" and an empty "runs that did not complete"
list. A state hash is compared at every checkpoint; the position digest (replay format
12+) is compared as well wherever both sides carry one. Every game wrote a referee
replay in both runs: 37 per mode. Referee replays were matched to cases by the seed in
the replay header. The checkpoint comparison script is the 0.8.4/0.8.5 one, updated to
the 20-byte format-12 trailer record (tick, hash, position digest).

**No Legion retries.** As in 0.8.1-0.8.5, the Legion run used `TAK_WALL_TIMEOUT=3600`
from the start; every seat completed in the single pass.

### Reference ticks and hashes (0.8.6 candidate)

Hash and units are the seated host's final line. "vs 0.8.5" compares tick and hash with
0.8.5 (`86db5d61`) re-run today with the same harness, hosts and seeds (see the next
section); "vs pre-281" compares with `8a28ab61` (everything except the blocked-shot
change). The six `TAK_AUTOPLAY` cases never repeat a hash, and `h-lake`, `h-stress`,
`2h-stress`, `2h-gods`, `4h-gods` are load-sensitive (see 0.8.5's notes), so "differs" in
those rows is expected noise.

| Case | Retail end | Retail tick | Retail hash | Retail units | vs 0.8.5 | vs pre-281 | Legion end | Legion tick | Legion hash | Legion units | vs 0.8.5 | vs pre-281 |
| --- | --- | ---: | --- | ---: | --- | --- | --- | ---: | --- | ---: | --- | --- |
| `h-baseline` | timelimit | 18,000 | `4a07030c0169e47c` | 210 | differs | differs | timelimit | 18,000 | `651a23a9da21f771` | 209 | differs | differs |
| `h-gods` | concluded | 14,626 | `0ea0a1d96c1414af` | 104 | differs | differs | concluded | 15,425 | `fcc7074afe4b074b` | 87 | differs | same |
| `h-crusades` | timelimit | 18,000 | `604de77d3fe53f3e` | 245 | differs | differs | timelimit | 18,000 | `df4ecd98de1c1c8c` | 234 | differs | differs |
| `h-stress` | concluded | 4,891 | `442f917704150f0d` | 8,596 | differs | differs | concluded | 5,025 | `804de805c2949f53` | 7,620 | differs | differs |
| `h-absurd` | timelimit | 18,000 | `1b0d79cb6583219d` | 454 | differs | differs | timelimit | 18,000 | `b92f0189579fa01d` | 491 | differs | differs |
| `h-fog-explored` | timelimit | 18,000 | `00419b0edd6c9b06` | 188 | differs | differs | timelimit | 18,000 | `f204316160ad0c7c` | 171 | differs | differs |
| `h-fog-full` | timelimit | 18,000 | `9896fd941b2aade9` | 216 | same | same | timelimit | 18,000 | `f3ede5f2f915c7cd` | 207 | differs | differs |
| `h-unitcap` | concluded | 14,780 | `c382486eeb54fb3b` | 204 | differs | differs | timelimit | 18,000 | `12f844e151739a59` | 209 | differs | differs |
| `h-cramped` | concluded | 1,307 | `45134591344a4d04` | 10 | same | same | timelimit | 18,000 | `2571189f606c656f` | 43 | differs | differs |
| `h-naval` | timelimit | 18,000 | `5fb53d62d7a0b42c` | 58 | differs | differs | timelimit | 18,000 | `2d4076e696e3b782` | 81 | differs | differs |
| `h-naval-crus` | timelimit | 18,000 | `396f8de68f25c274` | 70 | differs | differs | timelimit | 18,000 | `54bede798666ebe5` | 88 | differs | differs |
| `h-lake` | concluded | 3,944 | `91064721fcedbc4a` | 4,922 | differs | differs | concluded | 4,002 | `0291a05b660b968e` | 4,882 | differs | differs |
| `h-random-starts` | concluded | 15,702 | `6e0395e042f734fe` | 228 | differs | differs | concluded | 12,289 | `0090a9dcacf4a410` | 195 | differs | same |
| `h-monarch-exp` | concluded | 12,231 | `c646ef640c32a947` | 89 | same | same | concluded | 17,505 | `9f930617aab8180b` | 82 | differs | same |
| `h-overrides-full` | timelimit | 18,000 | `450038fec3e01ce5` | 209 | differs | differs | timelimit | 18,000 | `5045aa7469e4e29d` | 205 | differs | differs |
| `h-everything` | concluded | 13,549 | `8ada2d5383b95f0a` | 5,140 | differs | differs | timelimit | 18,000 | `deb24546d91215fc` | 5,588 | differs | differs |
| `h-speed-1x` | timelimit | 18,000 | `9b5694cceca37a11` | 236 | differs | differs | timelimit | 18,000 | `c7cf7ddca87e0a01` | 238 | differs | differs |
| `h-two-castles` | timelimit | 18,000 | `8e9dfa2311fc1ec4` | 23 | same | same | timelimit | 18,000 | `5a79577c084b2dad` | 4 | same | same |
| `2h-baseline` | timelimit | 18,000 | `7b5850e128c0ff63` | 173 | differs | differs | timelimit | 18,000 | `01e9a31e7b7ba667` | 168 | differs | differs |
| `2h-gods` | concluded | 12,423 | `bf5aaf973f862369` | 104 | differs | same | concluded | 13,039 | `2771a90866fd208d` | 61 | differs | same |
| `2h-crusades` | timelimit | 18,000 | `4177d6f4374aad35` | 182 | differs | differs | timelimit | 18,000 | `f6c039254bef9503` | 174 | differs | differs |
| `2h-stress` | concluded | 5,048 | `fbe83bc16dc7bf85` | 9,102 | differs | differs | concluded | 5,313 | `91e8919bee0991f1` | 8,352 | differs | differs |
| `2h-orders` | timelimit | 18,000 | `2b158dfae696135f` | 160 | differs | differs | timelimit | 18,000 | `95e47dba229d1d26` | 191 | differs | differs |
| `2h-orders-stress` | concluded | 5,687 | `c087caeb23c69514` | 8,528 | differs | differs | timelimit | 18,000 | `05d4bec63c5b0cad` | 5,546 | differs | differs |
| `4h-orders` | timelimit | 18,000 | `244dc7b1d0bff73a` | 151 | differs | differs | timelimit | 18,000 | `8f8699c64ffd8120` | 168 | differs | differs |
| `3h-baseline` | timelimit | 18,000 | `1d59d944a244aef4` | 172 | differs | differs | timelimit | 18,000 | `20d11b12ad8bcb68` | 136 | differs | differs |
| `4h-gods` | timelimit | 18,000 | `eabc4471bde6900d` | 111 | differs | differs | timelimit | 18,000 | `f3b2f54c10fb7982` | 172 | differs | differs |
| `net-rtt50` | timelimit | 18,000 | `80dc6a72233bd05d` | 230 | differs | differs | timelimit | 18,000 | `6cc12e8a0af722c7` | 230 | differs | differs |
| `net-rtt100` | timelimit | 18,000 | `fcba4a56baf98c45` | 190 | differs | differs | timelimit | 18,000 | `563eb53dc55b053a` | 185 | differs | differs |
| `net-jitter` | timelimit | 18,000 | `6a0f640c3faf1de6` | 173 | differs | differs | timelimit | 18,000 | `20894aa764ce0ef6` | 162 | differs | differs |
| `net-rtt500` | timelimit | 18,000 | `8837809d723b6fc0` | 219 | differs | differs | timelimit | 18,000 | `9b4ab072af188a68` | 196 | differs | differs |
| `net-loss1` | timelimit | 18,000 | `1adf2c3d366e9bef` | 192 | differs | differs | timelimit | 18,000 | `865cdc354ceb7b68` | 202 | differs | differs |
| `net-loss3-orders` | timelimit | 18,000 | `76822a3e9a7dee8b` | 197 | differs | differs | concluded | 17,281 | `c09828b0a32b9f9f` | 242 | differs | differs |
| `net-orders100` | timelimit | 18,000 | `00864809a77e2090` | 187 | differs | differs | timelimit | 18,000 | `c0c6731f15c84b96` | 180 | differs | differs |
| `net-2h-rtt100` | timelimit | 18,000 | `048ef1cb515ee0d6` | 172 | differs | differs | timelimit | 18,000 | `e78c0092fb7d014b` | 155 | differs | differs |
| `w-allai-stress` | timelimit | 18,000 | `06e78e00b732019a` | 5,392 | differs | differs | timelimit | 18,000 | `65d2f05a9575a5f1` | 4,084 | differs | differs |
| `w-allai-bench` | timelimit | 18,000 | `10a3a871daabf505` | 312 | differs | differs | timelimit | 18,000 | `d265fad8faccfc6f` | 423 | differs | differs |

### Comparison with 0.8.5

0.8.5's per-case logs were no longer on disk, so the comparison is against a fresh run:
the 0.8.5 source (`86db5d61`, same container-built referee recipe and `-O2 -g` client)
was swept today on the same two hosts, same seeds, same harness, both modes (all 52
seats completed, no desync). Its numbers reproduce the published 0.8.5 values
(Retail `h-cramped` 1,307 `45134591344a4d04`; `w-allai-stress` 18,000
`e10d25236d48c058`; Legion `h-cramped` 18,000 `bbbd4becad56c714`, 34 units). A third
sweep of `8a28ab61` (the tree just before the blocked-shot work, so with Legion rounds
1-2 and the hover cursor but not protocol 281) attributes each difference.

**Retail: only protocol 281 moves anything.**

- `8a28ab61` vs 0.8.5: 26 of 37 cases are bit-identical (tick and hash). The 11 that
  differ are exactly the known unstable ones: the six `TAK_AUTOPLAY` cases and the five
  load-sensitive cases (`h-lake` 4,115 -> 4,113, `h-stress` 4,762 -> 4,761, `2h-gods`
  12,424 -> 12,423, `2h-stress`, `4h-gods`). So Legion rounds 1-2, the voice and cursor
  work do not touch Retail state, as designed.
- 0.8.6 vs 0.8.5: only **4 of 37** stay identical: `h-cramped` (1,307,
  `45134591344a4d04`), `h-fog-full`, `h-monarch-exp` and `h-two-castles`. Of the 33 that
  differ, 11 are the autoplay/load-sensitive ones above; the other **22 are
  deterministic cases moved by the blocked-shot change** (they match `8a28ab61` and
  0.8.5 run-to-run, so this is not noise). In 19 of them the game ends at the same tick
  with the same unit count and only the hash differs (a few units stand or shoot from
  different spots; for example `h-gods` 14,626 / 104 units, `h-unitcap` 14,780,
  `h-random-starts` 15,702, all `concluded` at the identical tick). The visible outcome
  changes are `h-everything` (0.8.5: 18,000 ticks, 4,036 units; now `concluded` at
  13,549 with 5,140 units), `h-stress` (4,891 / 8,596 units vs 4,762 / 8,422), `2h-stress`
  (5,048 vs 4,846), and the spectators `w-allai-stress` (5,392 units vs 5,622) and
  `w-allai-bench` (312 vs 276).
- Why `--mpai` Inner Circle seed 1 is unchanged but Ulasem Arena cases move: Inner Circle
  / Two Castles games (`h-cramped`, `h-two-castles`, `h-monarch-exp`) never produce a
  run of blocked shots in these games, while Ulasem Arena (cliffs, scenery) does.
  This is an inference from which cases move, not something checked in the sim.

**Legion: every case moves, and that was expected.**

- `8a28ab61` vs 0.8.5: 36 of 37 differ (only `h-two-castles`, 4 units, is identical), the
  Legion pathfinding rounds 1-2 (protocols 279/280) changing formation movement,
  passage admission and rings everywhere. Examples: `h-random-starts` ends at 12,289
  (was 13,114), `h-monarch-exp` now concludes at 17,505 (was a time limit), `h-lake`
  4,057 (was 4,508), `h-stress` 5,270 (was 4,951).
- 0.8.6 vs `8a28ab61`: 32 of 37 differ again (blocked-shot change); `h-gods`,
  `h-random-starts`, `h-monarch-exp`, `2h-gods` and `h-two-castles` are bit-identical to
  `8a28ab61`. Legion `h-cramped` 34 units / `bbbd4becad56c714` (0.8.5) -> 43 units
  / `3111998964a295e8` (`8a28ab61`) -> 43 units / `2571189f606c656f` (now); it still
  runs to 18,000 ticks.
- Legion `net-loss3-orders` (autoplay) now concludes at 17,281 where 0.8.5 hit the time
  limit; `h-everything` still hits 18,000 but with 5,588 units (0.8.5: 3,415).

In every case, in all three source versions, the seats and the referee agreed at every
checkpoint. The Retail/Legion hash differences are therefore changes of simulation
behaviour between versions, not desyncs. They are the reason 0.8.6 is neither
multiplayer- nor replay-compatible with 0.8.5 (protocol 281 vs 278).

**`h-cramped` with Monarch Expendable: known limitation, unchanged in Retail.** Retail
still concludes early at tick 1,307, 10 units, `45134591344a4d04`, exactly 0.8.5's value
(a match spawns only the Monarch, so the god storm eliminates an idle seat). Legion
`h-cramped` runs to 18,000 ticks (43 units).

**Impaired networks.** All eight impaired scenarios (`net-rtt50`, `net-rtt100`,
`net-jitter`, `net-rtt500`, `net-loss1`, `net-loss3-orders`, `net-orders100`,
`net-2h-rtt100`) ran under netem in both runs and completed. No `netem` or `prio` qdisc
was left on either host.

**Voice.** No case exercised voice chat; voice is presentation-only relay traffic, never
in a tick bundle or a replay. The talk-mode change is client-only.

**Isolation.** The referee was a separate test executable, `~/takserver.sweep086`, on
test ports 7890-7936 (Retail) and 7990-8036 (Legion); the public service uses 7677. It
used per-mode map caches `~/tak_cache.sweep086-{retail,legion}` and replay directories
`~/tak_replay.sweep086-{retail,legion}`. The comparison sweeps used
`~/takserver.base085` and `~/takserver.mid085` on separate ports and directories. The
public `/usr/bin/takserver` on tak.pgnet.us was not replaced, stopped or restarted: PID
12853, port 7677, started 2026-10-19 06:02:19 UTC (its own auto-update, before this
work), the same PID before and after. No certificates were touched. `vpn3` runs no
takserver service.

**Cleanup.** After the sweeps, on both hosts: no process was executing any of the three
test binaries, no listener remained on the test ports, and the test binaries, map caches,
replay directories and `/tmp/tak-{srv,val,neg}-*.log` files were removed after the
referee replays had been copied locally.

**Binaries.** Built from clean detached worktrees (`~/tak-tmp/wt/release-086-val` at
`575a70d2`) with optimized Debug (`-O2 -g`) for both referee and client.

- **Referee:** Ubuntu 24.04 container (`tak-sweep-builder-0719`), GCC 13.3,
  `TAK_BUILD_CLIENT=OFF`, max symbol version GLIBC_2.38. On each host the deployed
  SHA-256 matched the build.
- **Client:** Fedora, GCC 16.

| Binary | SHA-256 (`575a70d2`, `v0.8.5-28-g575a70d2ff7c`, protocol 281) |
| --- | --- |
| Referee | `95c4b1b6caf5dd7f0aa551f6ff1c59308cb80625b1156477f345295a37a79f02` |
| Client | `e06243f6e56890cbf0c6636051d94a9872706d142b5ce27995d9630ff6b8180f` |
| `override_transfer_test` (client side) | `365ec9bd3c9b876f93cc29ea9230f01e3311cb2eaece77817a2b197dd2dad82a` |

**Harness.** The committed `tools/desync-hunt-remote.sh`, unmodified; its SHA-256,
`0ec213448db4f15df1ada6bd5c4bf8ab8d1bbe719e0d11bf03776200e2ba365b`, is unchanged since
0.8.5. Note for next time: the harness' SSH control sockets need a short `TMPDIR`
(`$TMPDIR/desync-remote-PID/ctl-%C` must stay under the 108-byte socket path limit, or
the build-id gate reads an empty server id); this run used `/home/pocket_geek/tk`.

**Mixed versions.** None played: 0.8.6 speaks protocol 281, and the server's Hello
rejects any client at another protocol, so 0.8.6 is neither multiplayer- nor
replay-compatible with 0.8.5 or earlier.

Logs: `~/tak-tmp/scratch/release-086/remote/` (`{retail,legion}.sweep.log`,
`base-{retail,legion}.sweep.log` (0.8.5), `mid-{retail,legion}.sweep.log`
(`8a28ab61`), `*-checkpoints.json`, `logs-{retail,legion}/`, `ref-{retail,legion}/`
referee replays).

## CI and packages

<!-- LEAD: main CI on the final source (Determinism, Linux packages, Windows,
     macOS run ids and any reruns / known flakes: macOS x64 stream_test, macOS
     "Upload CI artifact" ENOTFOUND, Ubuntu 22.04 crusades_client_network_test);
     tag CI run ids (determinism dispatched with gh workflow run determinism.yml
     --ref v0.8.6); the 29 assets checked against gh release view digests; build
     ids "0.8.6 (build v0.8.6)" on Linux, Windows 11 (oden-win) and the MacBook;
     Authenticode Valid on the Windows files; both DMGs and apps notarized and
     stapled; audio-input entitlement and NSHighResolutionCapable in every macOS
     app; no libsecret/glib NEEDED in the Linux takclient; Opus static and
     OPUS-LICENSE in every package. -->
**PLACEHOLDER (lead): CI and packages.**

## Screenshots

Eleven gallery and guide pictures were recaptured on the 0.8.6 candidate
(`575a70d2` plus the capture-only version bump; optimized Debug, OpenGL) with
the 0.8.5 capture settings (same jobs, cameras, seed 42, profile and crops,
rebuilt from the archived 0.8.5 capture patch). Each was inspected by eye
against its 0.8.5 version, and `docs/img/README.md` describes how each was
taken.

- Interface changes: `title.jpg` (VERSION 0.8.6) and `guide/voice-chat.jpg`
  (the new TALK MODE row at Push to Talk and ACTIVATION LEVEL 15 greyed out,
  other values default, MIC LEVEL empty; crop now 832 x 651, was 840 x 570,
  because the panel is taller). The user guide's alt text was updated to match.
- Scenes retaken each release: `battle.jpg` (Absurd benchmark, Ulasem Arena,
  55 s; armies sit slightly differently), `dragons.jpg` (Creon Neo Dragons on
  the Aramon siege column, 90 s), `flyers.jpg` (Legion Zhon formation, 22 s:
  narrower column, flyers over the leading ground units, caption still holds),
  `hud.jpg` (45 SELECTED, FORMATION 1, VID 113 MB), `naval.jpg`,
  `guide/benchmark.jpg` (BENCHMARKING 0:05), `guide/scorecard.jpg` (F4 at
  55 s), `guide/fire-on-the-move.jpg` (same result as 0.8.5) and
  `guide/flyers-retail-legion.jpg` (Retail half identical to 0.8.5, Legion
  half shows the new movement).
- Kept from 0.8.5 (screens unchanged): `generated.jpg`,
  `guide/options-graphics.jpg`, `guide/tactical-dots.jpg`,
  `guide/random-map-lobby.jpg`, `cartographer/random-generator.jpg`; older
  pictures as listed in `docs/img/README.md`.
- Captions that depend on the new shots were checked: README title alt text
  ("version 0.8.6") and gallery footnote ("0.7.25 to 0.8.6 builds"), the
  guide's voice-chat alt text, and `docs/img/README.md`.

**Evidence not committed** (`~/tak-tmp/scratch/release-086/evidence/`):

- **Enemy-hover cursor.** In-game on Ulasem Arena with the pointer on an enemy
  Taros Archer: with 8 Aramon Archers selected, the nearest 880 px away (bow
  range 450), the client reports cursor 3, the crossed-swords **Attack**; with
  only a Veruna Bastion tower selected, 960 px away (range 580), cursor 5,
  **TooFar**. This is the retail rule (mobile attackers show Attack at any
  range; TooFar for immobile attackers out of range). No guide picture shows
  this cursor, so none was added.
- **Blocked shots.** No shipped map has a known spot with a passable crest or
  indestructible scenery to stage an in-game picture, so the evidence is a
  top-down trace from the sim using the `blocked_shot_test` scenes (shipped
  Archer against a Swordsman, 30 s each):

  | Scene | Short shots | Hits | Target HP | Archer moved |
  | --- | ---: | ---: | --- | --- |
  | Hold Position, ridge or campfire row | 11 | 0 | unchanged | 0 px |
  | Maneuver, ridge (Retail / Legion) | 3 | 5 | down to about 150 | 49 / 56 px, walk starts at tick 201 |
  | Maneuver, campfire row (Retail / Legion) | 3 | 5 | down to about 150-430 | 80 / 88 px, walk starts at tick 204 |

  These match the commit message of `c74c7f4e`/`575a70d2`.

## Limits

- No game mixing 0.8.5 and 0.8.6 builds was played, and none can be (protocol
  281 against 278, the Hello gate and the replay loader).
- **Blocked shots:** a stationary auto-targeting shooter keeps refusing a
  once-blocked target even after it moves into the open (an attack order or a
  move clears it); the clear-shot search covers about 12 cells. Retail has no
  such behaviour at all; this is a documented deviation.
- **Legion passage admission is still a lottery at ~100 bodies:** over the
  wide offsets, `taperbridge-100` gridlocks at offset +1 (a gate offset: 19 of
  100 arrive) and `bridges-100` at another; `legion_passage_admission` and
  `legion_passage_taper` hold their offsets. See docs/legion-pathfinding.md,
  Known weaknesses.
- Legion attack rings along walls fill more thinly than Retail's, a target
  walled in on every side is ringed but never fired on, and the faster
  flow-around raises lane swaps where streams cross after a turn
  (`mixed2:gate.mid.crossings` 13 -> 38, Retail 0).
- The voice-activation gate and the enemy-hover cursor are client-only and are
  covered by `voice_test` and `cursor_enemy`; neither is visible to the remote
  sweep.
- Carried over: the Legion 10,000 program still does not meet its own success
  criterion for congested workloads, and many-10000, scatter-10000 and
  mixed-10000 still run over the 33.3 ms tick budget in part of their ticks;
  mixed-10000's flyers hover over a slow ground army. Retail `h-cramped` ends
  early because the idle seat starts with only its Monarch. The macOS Retina
  UI Scale is migrated once.
- **Legion scenario baseline not retaken for 279/280.** The gate fails on
  3,205 Legion keys in 116 of 137 fixtures (57 nightly `legion_check_*` tests);
  0 Retail keys fail. Beyond the expected hash and metric drift, five keys look
  like worsenings and are to be judged when the baseline is retaken:
  `attackring-r200-64` `statue_ticks` 290 -> 20,977, `attackring-f1-16`
  `statue_ticks` 0 -> 1,669, `attackring-wall-64` `complete_n` 2 -> 0,
  `attackring-struct-60` `g.T.arrived` 1 -> 0, `deadend-w2-n40-room-plain`
  `complete_n` 38 -> 32.
- `override_transfer` failed under load in the Clang and ASan full runs
  ("Not a directory ... OverrideCache") and passed alone; a new flake
  candidate (neither the test nor its code changed since 0.8.5).
- Retail behaviour is not bit-identical to 0.8.5 wherever shots are blocked:
  22 deterministic Retail sweep cases on Ulasem Arena moved (19 hash-only;
  `h-everything` now concludes at tick 13,549).
