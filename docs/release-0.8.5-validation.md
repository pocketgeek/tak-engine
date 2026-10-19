# 0.8.5 release validation

Protocol **278** (0.8.4: 277), replay format **13**, generator version **9**
(0.8.4: 8), campaign payload **4**, database schema **9** (replay, campaign and
schema unchanged since 0.8.4).
<!-- LEAD: re-check kNetVersion right before tagging.
     Version 0.8.5 and protocol 278 are already written in: CMakeLists.txt
     (project VERSION); README.md (badge, title alt text, gallery footnote,
     New in 0.8.5, random maps x2, multiplayer, overrides); docs/user-guide.md
     (intro, Versions, Replays, public server, old replays);
     docs/public-server.md; docs/random-map-generation.md (generator 9);
     docs/multiplayer-design.md (protocol summary, 278 replay history, the
     required-protocol line); docs/img/README.md (from the screenshots branch); these
     notes and the release notes. If kNetVersion moves past 278, update every
     one of them. -->

Tagged `v0.8.5` at `86db5d61` (annotated tag object `b1901899`) and published on 2026-10-19 05:51 UTC.

## Scope and compatibility

0.8.5 follows 0.8.4 (protocol 277). After the 0.8.4 validation commit
`8b8618db`, main reached `8788c4fa` in 15 commits: 10 changes and 5 merges of
the task branches. `git diff --stat v0.8.4..8788c4fa -- src` touches 29 files:
`src/client/` (18), `src/cartographer/` (5), `src/tnt/` (3), `src/sim/` (2)
and `src/net/` (1). Nothing under `src/ai/`, `src/server/`, `src/campaign/`,
`src/terrain/`, `src/cob/`, `src/crt/` or `src/util/` changed.

After `8788c4fa`, the release preparation added only the screenshots, the
documentation and the CMake version bump; no source file changed.

The changes:

- **278** random-map recipe version 9, Scattered Mana (`b1d5749f`; lobby
  layout `8984d094`, `8788c4fa`). The level rides in the `~gen1~` map id as one
  byte before the title; a 277 peer would refuse a v9 id. Version-8 ids decode
  as None and regenerate byte-identically (`mapgen_test`'s asset-free golden
  `0x6bf977a42891c638` unchanged).
- Death and unit sounds on retail's view plane, carried and cloaked other-owner
  units silent (`296ebe0f`, `2c557b47`). The simulation records the position
  and hidden state with each `ScriptSound` (`src/sim/sim.{h,cpp}`); that
  record is presentation only and not folded into `World::stateHash()`.
- Client only: native Retina resolution on macOS (`b0f823f3`); world effects
  and projectiles hidden under Tactical Dots (`5cd02b1d`, `e7f6cdda`); Screen
  Shake option, 50% scales and new graphics defaults (`025bedfe`); no
  queued-build ghost over a site under construction (`2a84a023`).

**Retail and Legion are expected to be bit-identical to 0.8.4** on authored
maps and on generated maps with Scattered Mana at None.
Confirmed on `8788c4fa` (see Local preparation checks): `--mpai` Inner Circle seed 1
Retail 300 s `a3d953803a064191`, 60 s `b353410003e98dd5`; Legion 300 s
`ce5c4ba7562e53b9`, 60 s `0825aeb9e8139203`, all equal to 0.8.4. The release
commits after it change only documentation, screenshots and the version number.

**Compatibility.** `kNetVersion` is **278** in `src/net/protocol.h` at
`8788c4fa`, against 277 at `v0.8.4`. `kReplayFormat` is **13** in
`src/net/replayhdr.h` (unchanged); the map generator's `formatVer` is **9**
(`src/tnt/mapgen.h`, 8 at `v0.8.4`), the campaign payload's `kVersion` 4
(`src/net/crusades.h`, unchanged) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`, unchanged).

- **Multiplayer.** The server's Hello check rejects a client whose protocol
  differs from `kNetVersion`, so **0.8.5 cannot play with 0.8.4 or earlier in
  either direction**. The server and all players must update together.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  protocol equals `kNetVersion`, so 0.8.5 does not play 0.8.4 or older replays
  and those builds do not play 0.8.5 replays, although the file format is still
  13. `tools/replay_test.cpp` pins `kNetVersion == 278` and refuses 277.
- **Maps and settings.** Saved `.kmp` maps and generator-8 recipes carry over.
  A 0.8.4 settings file reads the same in 0.8.5 and keeps its saved values;
  the new defaults apply only to keys a file does not have (and a file with no
  texture-filter key at all now resolves to Everything instead of Off). New key:
  `screenShake`. 0.8.4 drops it when it saves, and clamps a scale below 75% to
  75%.

## Local preparation checks

All checks ran from a clean detached worktree (`~/tak-tmp/wt/release-085-local`) at
`main` `8788c4fa61e8` (protocol **278**, replay format **13**, generator recipe **9**, campaign
payload 4, database schema unchanged), with `assets` and `third_party` links to the
main checkout. Toolchains: GCC 16.2.1, Clang 22.1.8, CMake 4.3.0, Ninja 1.13.2, Python 3.14.8.
Builds and tests ran on cores 4-23; the owned retail install served as `--data` and
`TAK_TEST_DATA`. The version bump is not in this commit, so binaries still call themselves
`0.8.4 (build v0.8.4-16-g8788c4fa61e8)` (no `-dirty`).

**Result: every release-relevant check passed. No real failure.** Four failures in the
unoptimized suites were load timeouts: each passed when rerun alone.

- **No multiplayer or replay compatibility with 0.8.4 or earlier.** `kNetVersion` is **278**
  in `src/net/protocol.h` (277 in 0.8.4). The replay format stays 13; the replay loader
  requires protocol == `kNetVersion`, so 0.8.4 replays do not play. 278 exists because a
  277 peer would refuse a recipe-v9 map id.
- **Builds.** Release, Debug, optimized Debug (`-O2 -g`), Clang Debug and AddressSanitizer
  all-target builds completed with exit code 0: 869 steps in Release, 870 in the other
  four (same as 0.8.4). ccache on the GCC trees; static-deps and static Bink FFmpeg
  prefixes. Release emits about 850 warning lines (451 `-Wmissing-field-initializers`, 332
  `-Wdeprecated-declarations`), the same kinds as 0.8.4 (852); neither of 0.8.4's two
  fixed warnings came back.
- **Full CTest.** Still **678** registered tests (640 in Release; the
  nightly label is unchanged); no test was added or removed since 0.8.4 (the set of names is identical).

  | Tree | Selection | Result | Time | Failures |
  | --- | --- | --- | ---: | --- |
  | Release | full, 640 | **640/640** | 2,552 s | none |
  | Optimized Debug | full, 678 | **678/678** | 3,129 s | none |
  | Debug (`-O0`) | `-LE nightly`, 516 | 514/516 | 4,418 s | `cartographer_ui_smoke`, `replay_always` (load; pass alone) |
  | Clang Debug | `-LE nightly`, 516 | 514/516 | 5,475 s | `cartographer_ui_smoke`, `replay_always` (load; pass alone) |
  | ASan (focused) | 270 | **270/270** | 2,124 s | none; no AddressSanitizer, LeakSanitizer or runtime-error report |

  The suites ran in parallel with each other and with the Legion gate (load average
  25-40 on 24 cores). No test reported a skip.
  - **`cartographer_ui_smoke` (existing; passed in 0.8.4 at 134-154 s): `-O0` load.** It
    failed in both unoptimized trees with `stuck at step 405 (modal 16 ...)`, then
    `smoke stopped at step 405 of 581`. Modal 16 is `M_ANALYZING`: the smoke script's
    "stalled for 400 frames" guard fires while an asynchronous analysis job is still
    running, so it depends on wall time. Rerun alone on reserved cores: **Debug 2/2
    (127 s, 121 s) and Clang Debug 1/1 (147 s)**. Release and optimized Debug passed in
    the loaded full runs. The random-map commit `b1d5749f` touched the generator
    dialog in Cartographer, so this is the one failure worth a look if it ever repeats
    on a quiet machine; it is not a regression here.
  - **`replay_always` (existing; passed in 0.8.4): load.** Case `mission-crash` failed
    in both unoptimized trees (client `tick=0 ... err=peer closed end=timelimit`, the
    mission did not start inside 60 s); the other seven cases passed. Rerun alone:
    Debug 1/1 (48 s) and Clang Debug 1/1 (54 s). This is the same case that failed
    under ASan load in 0.8.4's first round.
  - **The unoptimized Legion tests passed under the 0.8.4 limits.** `legion_sched_inflight`
    506 s / 995 s, `legion_scale_neutral` 560 s / 917 s, `legion_route_paused` 1,269 s /
    1,323 s and `legion_parallel` 2,786 s / 3,881 s (Debug / Clang Debug); the largest use
    of a limit is `legion_parallel` in Clang Debug, 72% of 5400 s. `movement_orders_trolls`
    took 327 s / 419 s (limit 600 s). `terrain_schedule` 92 s / 105 s, `voice` and
    `server_voice` passed in both. The 0.8.4 test-fix commits held.
  - **The settings-leak guard held.** `pref_guard_setup`/`pref_guard_check` passed in every
    selection. The real profile's `settings.ini` has the same SHA-256 and mtime before and
    after (`real-profile-{before,after}.txt`; it was last changed at 19:39, before the
    run started). `TAK_SETTINGS_FILE` was not exported.
  - **Slowest tests** (Release / optimized Debug): `legion_check_pocket-615-wall` 1,071 s /
    1,344 s, `legion_check_pocket-615-open` 1,019 s / 1,234 s and
    `legion_check_tail-corner380` 968 s / 1,144 s, all under their 1800 s limit.
- **Focused AddressSanitizer/leak run: 270/270**, with no sanitizer report. It is 0.8.4's
  270-test selection (`bash tools/run-ctest-selection.sh`, dummy SDL video/audio,
  `ASAN_OPTIONS=detect_leaks=1`, no suppressions). Because the new commits touch tests
  outside that list, a second ASan selection of the touched tests also passed **18/18**:
  `mapgen`, `mapgen_mana`, `mapgen_naval`, `mapgen_ports`, `mapgen_roster`,
  `mapgen_themes`, `override_settings`, `settings`, `builder_automation`,
  `cartographer_validation`, `death_sound`, `death_sound_roster`, `creon_death_sound`,
  `creon_death_sound_peers`, `tactical_dots`, `replay` and the two `pref_guard` fixtures.
- **`--mpai` golden hashes: all four unchanged from 0.8.4.** Each ran once, on the
  optimized Debug build, Inner Circle, `takserver --local --no-auth --seed 1`, an empty
  `XDG_DATA_HOME`, and `TAK_LEGION=1` for Legion. The four ran concurrently on separate
  core pairs. No client or server log has a desync or referee-suspect line.

  | Mode | Duration | `8788c4fa` (278) | 0.8.4 (277) | 0.8.3 |
  | --- | ---: | --- | --- | --- |
  | Legion | 300 s | `ce5c4ba7562e53b9` | `ce5c4ba7562e53b9` | `c707e1a8063bc0c7` |
  | Retail | 300 s | `a3d953803a064191` | `a3d953803a064191` | `7535cb08ee787b75` |
  | Legion | 60 s | `0825aeb9e8139203` | `0825aeb9e8139203` | `532539f51d70840b` |
  | Retail | 60 s | `b353410003e98dd5` | `b353410003e98dd5` | `2c39430636a3dbda` |

  They are expected not to move. The only protocol-278 change is the random-map recipe v9
  (`Params.manaScatter` in the map id, v8 ids regenerate identically; Inner Circle is a
  retail map, not generated). The only `src/sim` change is the death-sound record, which
  gained two presentation fields (`ScriptSound::y` and `hidden`, filled in `sim.cpp` when a
  dying VM plays a sound); the record is read by the client's audio and is not folded
  into `stateHash()`. Optimized Debug binary SHA-256:
  - client `098987fd266081d1955851d0353f1b13602e50926e2b5bf0068198d04ecdd72c`
  - server `5a0d8235de85dcbe9ee02cee53b7aa675975429aa3c5ef89c7ecf39012d10664`
- **Determinism.** In `tools/check-determinism.sh`, the GCC and Clang x86-64 builds at
  O0/O2/O3 all agree on `dcef618cd2e4d558`, the same as 0.8.4. The aarch64 legs are
  skipped (target headers missing). `tools/check-detmath.sh` passes, including the
  `std::atomic_ref` guard.
- **Legion scenario gate: PASS.** The gate ran the way `tools/legion_nightly.sh` stage 2
  does: every `tools/scenarios/*.scn` (135 files), optimized Debug `legion_scenario`,
  `--mode both --json --offsets gate --data <retail install>`, one file per core on cores
  14-23, then `legion_check.py check --cumulative --baseline tools/scenarios/baseline.json`.
  - **Result:** `PASS: 31859 keys checked, 0 failed, 3005 ratchets, 3333 licensed, 220 floor
    exceptions, 864 report-only` - identical to 0.8.4, so every `hash@` key still equals the
    baseline in both modes.
  - Every file exited 0 (`problems.txt` empty). `legion_check.py validate` reports
    `PASS: 0 problems`. No baseline was regenerated. The run took 3,924 s.
- **Retail group emulation check (formation-air).** `python3 -B tools/re/check_retail_group.py`
  (400 cases) passed every section: tick 400, query 4000, check 2000, formation 2000,
  vtolcheck 2000, vtolform 2000, with the same outcome counts as 0.8.4.
- **Python research suite.** **165 tests** passed, with one skip (the optional corpus test
  that needs `TAK_AUTH_RETAIL_DIR`), the same as 0.8.4. The owned-retail archive verifier
  (`TAK_AUTH_RETAIL_DIR`) passed all **6 tests**. The SHA-256 sums of the 16 retail HPI
  archives and `Kingdoms.key` are identical before and after the runs.
- **Syntax checks.** All **462** tracked Python files compile. All 22 `*.sh` scripts
  (21 tracked plus `tools/tak-netem`) pass `bash -n`.

Logs: `~/tak-tmp/scratch/release-085/` (`cfg.out`, `make-*.log`, `build-status.txt`,
`ctest-*.log`, `ctest-status.txt`, `ctest-asan-touched.log`, `rerun-*.log`,
`mpai-*.log`, `mpai-results.txt`, `gate/`, `check-determinism.log`, `check-detmath.log`,
`check_retail_group.log`, `python-suite.log`, `verifier.log`, `pycompile.log`,
`bashn.log`, `o2-sha256.txt`, `retail-sha-{before,after}.txt`,
`real-profile-{before,after}.txt`).


## Remote multiplayer sweep

The full 37-scenario table passed for both pathfinding modes, Retail and Legion,
on **2026-10-19** (UTC), both on `main` at `8788c4fa61e8038a98fff0ef1586dc5281e2974e`
(protocol **278**, build id `v0.8.4-16-g8788c4fa61e8`; the version string still reads
0.8.4 because the 0.8.5 bump comes later):

- **Retail:** 01:48-02:05 UTC (17.5 minutes including the gates).
- **Legion:** 02:05-02:32 UTC (26.5 minutes including the gates).

Referees ran on `tak.pgnet.us` (24 runs, 10 at a time) and `vpn3.pgnet.us` (13
runs, 6 at a time), with optimized local clients pinned to CPUs 4-23. Each scenario
allowed ten simulated minutes (18,000 ticks) or ended at an actual result for that
seat. Real seated players provide the client/referee checksum coverage. The two
all-AI spectator scenarios per mode test flow control, not hashes.

The harness picks the mode through the hosting client's room options:
`TAK_LEGION=1` creates a Legion room, and without it the room is Retail. Every
recorded referee replay header confirms the intended mode, replay format 13 and
protocol 278: 37 headers per mode, seeds 2000-2036 each mapped to its case
(`retail-mapref.txt`, `legion-mapref.txt`).

| Mode | Completed scenarios | Completed seats | Client-pair checkpoints | Client-referee checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Retail | 37/37 | 52/52 | 10,280 across 22 seat pairs, 0 mismatched | 2,640 across 50 seat/referee comparisons, 0 mismatched |
| Legion | 37/37 (single pass, no retries) | 52/52 | 10,211 across 22 seat pairs, 0 mismatched | 2,718 across 50 seat/referee comparisons, 0 mismatched |

Each run passed all three `--validate` gates before its table started:

- **Build identity:** `both at v0.8.4-16-g8788c4fa61e8`.
- **Planted desync:** the referee reported `game 1: client 1 DESYNCED at tick 900`.
- **Override transfer:** `verified packs transferred and incorrect loaded gameplay
  rejected`.

The gate checks only `tak.pgnet.us`; the binary on `vpn3.pgnet.us` was checked by
hand after deployment: same SHA-256 and the same `--version` build id. No natural
`DESYNCED`, `REFEREE SUSPECT` or client-reported desync appeared in any case log;
the only such line is the planted one in `validate.server.log`.

Every checkpoint carried a position digest as well as the state hash, and the
script compares both:

- **Referee checkpoints:** 2,060 in Retail, 2,142 in Legion.
- **Seated checkpoints:** 26,345 in Retail, 27,143 in Legion; none lacked a digest.

The comparison script (`checkpoints.py`) is the one used for 0.8.4, whose
bit-flip self-test (`fliptest`) was done for that release; it was not changed.
**Every game wrote a referee replay in both runs: 37 per mode, 35 of them seated.**
Referee replays were matched to cases by the seed in the replay header.

**No Legion retries.** As in 0.8.1-0.8.4, the Legion run used
`TAK_WALL_TIMEOUT=3600` from the start, with the same seeds and simulated-time
limit. Every seat completed in the single pass:

| Case | Retail end | Retail tick | Retail units | Legion end | Legion tick | Legion units |
| --- | --- | ---: | ---: | --- | ---: | ---: |
| `h-stress` | concluded | 4,763 | 8,422 | concluded | 4,962 | 7,749 |
| `2h-stress` | concluded | 4,851 | 9,174 | concluded | 5,654 | 8,268 |
| `2h-orders-stress` | concluded | 5,512 | 8,858 | concluded | 5,066 | 8,394 |
| `h-lake` | concluded | 4,149 | 4,845 | concluded | 4,541 | 4,775 |

**Reproducibility against 0.8.4.** The hashed simulation is unchanged since the
0.8.4 sweep: the only `src/sim` edit is two extra unhashed fields in the
presentation-only `ScriptSound` record (`scriptSounds_` is never folded into
`stateHash()`), and protocol 278 only covers the random-map recipe v9 id format.
So every deterministic seated case is expected to repeat 0.8.4's tick and hash:

- **Retail:** 29 of 37 ended at exactly 0.8.4's tick and hash (reference
  `f11fbc1a`, bit-identical to `0fe6305d`). That includes `h-cramped` (1,307,
  `45134591344a4d04`), `w-allai-stress` (18,000, `e10d25236d48c058`), `2h-stress`,
  `2h-gods`, `4h-gods` and `h-everything`.
- **Legion:** 28 of 37 ended at exactly the 0.8.4 (`0fe6305d`) tick and hash,
  including `h-cramped` (18,000, `bbbd4becad56c714`, 34 units).

The other 8 Retail and 9 Legion cases differ, in two groups. In every one, the seats
and the referee agreed at every checkpoint.

- **Six `TAK_AUTOPLAY` runs per mode:** `2h-orders`, `2h-orders-stress`,
  `4h-orders`, `net-2h-rtt100`, `net-loss3-orders` and `net-orders100`. They issue
  live orders that the server buckets by arrival tick, so they never repeat a hash
  from run to run.
- **Retail `h-stress` and `h-lake`; Legion `h-stress`, `h-lake` and `4h-gods`:**
  these are load-sensitive cases that are not expected to repeat across sweeps (see
  below).

**Load-sensitive cases (not a 0.8.5 change).** Retail `h-stress` (seed 2003) ended at
tick **4,763**, hash `8cff11dde3091026`, 8,422 units; 0.8.4's reference was 5,240
(`1fe1eb4e3754ca93`, 8,250 units). To tell a real change from run-to-run variation
the three cases whose result moved were rerun twice each, per mode, with `--only
h-stress,h-lake,4h-gods` (same binaries, same seeds, no `--validate`, a quieter
referee):

| Case | Retail sweep | Retail rerun 1 / 2 | Legion sweep | Legion rerun 1 / 2 |
| --- | --- | --- | --- | --- |
| `h-stress` | 4,763 `8cff11dd...` | 4,761 `131cdfb6...` / same | 4,962 `a4c31945...` | 4,951 `99d35bd2...` / same |
| `h-lake` | 4,149 `e3a0793a...` | 4,113 `e7ff2616...` / same | 4,541 `eb42a972...` | 4,507 `300e926c...` / same |
| `4h-gods` | 18,000 `d2eb67f9...` (= 0.8.4) | 18,000 `8e445ce5...` / same | 18,000 `60825d98...` | `1267de66...` / `52cf3d76...` (= 0.8.4) |

The same binary gives different results for these cases depending on the run, while
back-to-back reruns under the same conditions repeat each other (all but Legion
`4h-gods`, which produced three different hashes in three runs, one of them equal
to 0.8.4's). Seats agreed with each other at every checkpoint in all reruns
(6 pairs, 1,983 / 1,983 / 1,999 / 2,013 shared checkpoints, 0 mismatched; the rerun
referee replays were not compared). This matches the history of the same seed:
Retail `h-stress` has ended at 5,634, 5,606, 5,240 and now 4,763 / 4,761 across
0.8.0-0.8.5 with seats and referee agreeing every time, and `h-lake` moved the same
way in both modes. The stress fill (`kStressStaged`) and the god storm interact with
the server's pacing, so these cases give a different but consistent outcome
under different referee load. The new tick and hash are the references for the next
sweep only where noted as reproducible; do not diff these three cases across
sweeps. Nothing in the 0.8.5 changes touches hashed state, and the 29 / 28 exact
repeats (including the stress spectator `w-allai-stress` and `2h-stress` in both
modes) show it.

**`h-cramped` with Monarch Expendable: known limitation, unchanged.** In Retail the
game still concludes early at tick 1,307, hash `45134591344a4d04`, 10 units, exactly
0.8.4's value, with the seat matching the referee (see the 0.8.4 notes: a match
spawns only the Monarch, so the god storm eliminates an idle seat). Legion
`h-cramped` runs to 18,000 ticks, 34 units, `bbbd4becad56c714`, as in 0.8.4.

**Voice.** No case exercised voice chat; voice is presentation-only relay traffic,
never in a tick bundle or a replay.

**Impaired networks.** All eight impaired scenarios (`net-rtt50`, `net-rtt100`,
`net-jitter`, `net-rtt500`, `net-loss1`, `net-loss3-orders`, `net-orders100`,
`net-2h-rtt100`) ran under netem in both runs and completed; the harness set up and
tore down shaping per case, and no `netem` or `prio` qdisc was left on either host
(only the default `fq_codel`/`noqueue` qdiscs remain).

**Isolation.** The referee was a separate test executable,
`~/takserver.sweep085`, on test ports 7890-7936 (Retail) and 7990-8036 (Legion);
the public service uses 7677. It used per-mode map caches
`~/tak_cache.sweep085-{retail,legion}` and per-run replay directories
`~/tak_replay.sweep085-{retail,legion}[-rrN]`. The public `/usr/bin/takserver` on
tak.pgnet.us was not replaced, stopped or restarted: PID 8238, port 7677, active
since 2026-10-18 23:32:20 UTC, unchanged before and after. No certificates were
touched. `vpn3` runs no takserver service.

**Cleanup.** After the sweep, on both hosts: no process was executing
`takserver.sweep085`, no listener remained on the test ports, no netem claim
file or watchdog remained, and the test binary, map caches, replay directories and
`/tmp/tak-{srv,val,neg}-*.log` files were removed after the referee replays had been
copied locally (`cleanup-check-{before,after}.txt`).

**Binaries.** Built from a clean detached worktree
(`~/tak-tmp/wt/release-085-val`) with optimized Debug (`-O2 -g`) for both referee and
client.

- **Referee:** Ubuntu 24.04 container, GCC 13.3, `TAK_BUILD_CLIENT=OFF`, max symbol
  version GLIBC_2.38. On each host the deployed SHA-256 matched the build.
- **Client:** Fedora, GCC 16.2.1.

| Binary | SHA-256 (`8788c4fa`, `v0.8.4-16-g8788c4fa61e8`, protocol 278) |
| --- | --- |
| Referee | `df9fee3ea830686b61a2e138a340feb68d58636d3cc6a6b44b8f8bb7a7476507` |
| Client | `e73d98e677e3900589a553a9770a38638694d66f37b908ad09825a7accd0e8a4` |
| `override_transfer_test` | `65563bed31e38278ebf554384de7fa2a39fb175bb50bfe8c87b0d594db8d622c` |

**Harness.** The committed `tools/desync-hunt-remote.sh`, unmodified; its SHA-256,
`0ec213448db4f15df1ada6bd5c4bf8ab8d1bbe719e0d11bf03776200e2ba365b`, is the same as
for the 0.8.4 sweep and has not changed since `5774601a`.

**Mixed versions.** None played: 0.8.5 speaks protocol 278, and the server's Hello
rejects any client at another protocol, so 0.8.5 is neither multiplayer- nor
replay-compatible with 0.8.4 or earlier.

Logs: `~/tak-tmp/scratch/release-085/remote/` (`{retail,legion}.sweep.log`,
`r085.txt`, `l085.txt`, `*-vs-084.txt`, `*-checkpoints.json`, `*-rr{1,2}.sweep.log`).


## CI and packages

Main CI on the final source `86db5d61` passed: Determinism 39199985285, macOS
39199985334, Windows 39199985237, and Linux packages 39199985245 after one
rerun (Ubuntu 22.04 x64 `crusades_client_network_test`, "complete replay not
published"; the same source had passed in run 39186433179, and the job passed
on rerun, so it is recorded as a new flake candidate).

On the exact tag (`86db5d61`, annotated tag object `b1901899`), all four
workflows passed on the first attempt. No job needed a rerun, so none of the
known flakes appeared, including the new Ubuntu 22.04 candidate
`crusades_client_network_test`:

- [Determinism and core tests](https://github.com/pocketgeek/tak-engine/actions/runs/39203615343), started manually on the tag because the workflow is path-filtered.
- [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/39203608328): all native package jobs and release attachment.
- [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/39203608344): x64, ARM64 and the x64 signing/package job.
- [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/39203608314): both builds and the **Sign + notarize** job for each architecture, which replaced the unsigned macOS ZIPs and DMGs in the draft with the signed, notarized and stapled ones.

The release was created as a draft with these notes, and tagged CI attached the
packages. All **29 assets** (the same set as 0.8.4) were downloaded after the
macOS signing finished. Each one matched GitHub's SHA-256 digest and byte size
before publication. Every asset is within 0.1% of its 0.8.4 size: ratios run
from 1.000 to 1.001.

- **Build ids.** The client and server in every Linux package (Debian 12 and
  13, Ubuntu 22.04, 24.04 and 26.04, Fedora 44 and openSUSE Leap 16.0, both
  architectures each, plus Arch and Slackware) carry `v0.8.5`. Run: the x86-64
  `takclient` and `takserver` from Debian 12 and 13, Ubuntu 22.04, 24.04 and
  26.04, Fedora 44, openSUSE Leap 16.0 and Slackware report
  `0.8.5 (build v0.8.5)`. The Arch `takserver` reports `0.8.5 (build v0.8.5)`.
  The Arch `takclient` needs glibc 2.44, newer than the checking host's 2.43,
  so only its build string was checked. The x64 Windows ZIP's `takclient.exe`
  and `takserver.exe` report `0.8.5 (build v0.8.5)` on Windows 11. The ARM64
  `takclient.exe` carries `v0.8.5` (build string only). On the arm64 MacBook
  (macOS 26.6.2), `takclient` and `takserver` report `0.8.5 (build v0.8.5)` for
  both architectures, both as bare programs from the ZIPs and inside the game
  app from each DMG and each ZIP.
- **Windows signatures.** `Get-AuthenticodeSignature` on oden-win reports
  **Valid** (CN=Curtis Edge) for the setup `.exe` and for `takclient.exe`,
  `takserver.exe`, `cartographer.exe` and `crusades_admin.exe` in both the x64
  and ARM64 ZIPs: 9 signed files. The debug ZIPs were not checked.
- **macOS notarization.** On the MacBook, both DMGs pass `xcrun stapler
  validate` and `spctl` (open, primary signature) as **accepted, source=Notarized
  Developer ID, origin=Developer ID Application: Curtis Edge (L479Y59RN8)**. So
  do both apps (the game and TAK Cartographer) from each DMG and from each ZIP.
  Every bundled and bare `takclient`, `takserver`, `cartographer` and
  `crusades_admin` passes `codesign --verify --strict` (TeamIdentifier
  L479Y59RN8). `spctl` says the bare command-line programs do not seem to be an
  app, as expected for unstapled command-line tools.
- **Opus is static.** No packaged Linux `takclient` or `takserver` has a
  `NEEDED` entry beyond libc, libm and the loader (Slackware also lists libdl
  and libpthread), so none needs libopus. The Debian and Ubuntu packages depend
  only on `libc6`. Neither Windows `takclient.exe` imports or names an Opus DLL;
  the x64 import table lists only system DLLs. No macOS binary links a libopus
  dylib (`otool -L`). `OPUS-LICENSE.txt` is in every Debian, RPM, Arch and
  Slackware package and in all four release ZIPs.
- **macOS microphone.** The game app's `Info.plist` carries
  `NSMicrophoneUsageDescription` in both DMGs and both ZIPs. The
  `com.apple.security.device.audio-input` entitlement is on the game app and on
  its `takclient` (DMG and ZIP, both architectures), and also on the bare ZIP
  `takclient`. It is absent from `takserver`, `crusades_admin` and
  `cartographer`.
- As in 0.8.1 to 0.8.4: no packaged Linux `takclient` has a `NEEDED` entry for
  libsecret, GLib, GIO or GObject.

New check for 0.8.5:

- **macOS Retina.** `NSHighResolutionCapable` is `true` in the `Info.plist` of
  both apps (the game and TAK Cartographer), from both DMGs and both ZIPs.
  The packaged clients were not launched on a Retina display; the same code,
  built from `0a70e24b` on the MacBook, rendered 2816x1762 in a 1408x881-point
  window (backing scale 2.00) and was checked by eye on a 2x built-in panel and
  a 1x external display, including moving the window between them.

The release was published on 2026-10-19 05:51 UTC and is the latest release.

## Screenshots

Fifteen gallery and guide pictures were recaptured with the 0.8.5 defaults
(Texture Filtering Everything, Smooth GUI Art and Smooth Movies on, Tactical
Dots on at 0%, Screen Shake off) and one was added; each was inspected by eye,
and `docs/img/README.md` describes how each was taken.

- Recaptured: `title.jpg` (VERSION 0.8.5), `battle.jpg`, `dragons.jpg`,
  `flyers.jpg`, `hud.jpg` (VID 113 MB with the new defaults), `naval.jpg`,
  `generated.jpg` (the same Silverford recipe on generator 9 with Scattered Mana
  Medium: 53 mana spots instead of 24, and an AI lodestone on a neutral spot in
  the clearing that the version-8 map lacks), `guide/options-graphics.jpg`
  (Display and the whole Graphics section at the new defaults with the SCREEN
  SHAKE row, 1600 x 1100 window), `guide/tactical-dots.jpg` (full zoom-out,
  0.1875: fire and spell effects on the models half, none on the dots half),
  `guide/benchmark.jpg`, `guide/scorecard.jpg`, `guide/fire-on-the-move.jpg`,
  `guide/flyers-retail-legion.jpg` and `cartographer/random-generator.jpg`
  (12 fields with SCATTERED MANA (BY MAP AREA) at Medium).
- New: `guide/random-map-lobby.jpg`, the single-player random-map panel with
  SIZE and PLAYERS sharing a row and the SCATTERED MANA slider at HIGH (preview
  MANA: 57, SCATTERED: 33 NEUTRAL), linked from the user guide's Random maps.
- Kept: `diplomacy.jpg`, `guide/voice-chat.jpg` and the lobby and room pictures
  from earlier releases, whose screens did not change; they are drawn with the
  old unfiltered defaults, as the README says. There is no picture of the 50%
  UI and Scorecard Scale ranges, and none of the macOS Retina change (Linux
  capture host).
- Captions written for the new pictures (README title and generated alt text
  and the gallery footnote, the user guide's options-graphics, tactical-dots,
  random-map-lobby and generated captions, the Cartographer guide's
  random-generator alt text) were checked against the images.

In-game evidence for the two client fixes (kept locally in
`~/tak-tmp/scratch/release-085/evidence/`, not committed):

- **Build ghost (`2a84a023`).** Thirsha (`zonhunt`) conjures a Zhon lodestone on
  Ulasem Arena, approaching from the south so that she overlaps the site. On a
  build with the fix reverted, a washed-out full lodestone (pillars and dome) is
  drawn on top of her wings and robe; on main she is drawn over the half-built
  site and no outline appears.
- **Tactical Dots (`5cd02b1d`, `e7f6cdda`).** The same benchmark tick and camera
  at zoom 0.6 with Tactical Dots Zoom at 100%: with dots on, only the coloured
  dots are drawn, with no projectiles, fire streams, rings or glows; with dots
  off, the same view shows flame streams, explosions and spell rings. Zoomed in
  to 1.3 with dots still on, the view returns to models and the effects
  reappear.


## Limits

- No game mixing 0.8.4 and 0.8.5 builds was played, and none can be (protocol
  278 against 277, the Hello gate and the replay loader).
- The macOS Retina change was tried on one MacBook, including moving the
  window between a 2× and a 1× display. UI Scale is migrated once, so on a display of the other
  scale the interface looks larger or smaller until UI Scale is changed.
- Local visual inspection covers Linux OpenGL only; Windows and macOS packages
  are checked from CI and by signature, notarization and build-id checks.
- The Legion 10,000 program still does not meet its own success criterion for
  congested workloads (crowds at corners and narrow passages, many unrelated
  destinations), and many-10000, scatter-10000 and mixed-10000 still run over
  the 33.3 ms tick budget in part of their ticks; mixed-10000's flyers hover
  over a slow ground army since 277. Not re-measured for 0.8.5: no navigation,
  Legion or hashed simulation code changed since 0.8.4.
- **Retail `h-cramped` ends at tick 1,307** despite Monarch Expendable, because
  the idle seat starts with only its Monarch (see the remote sweep). Known.
- The remote sweep's Retail `h-stress`/`h-lake` and both modes' `4h-gods` end
  on different ticks or hashes from sweep to sweep, with seats and referee
  agreeing at every checkpoint; the hashed simulation is unchanged since 0.8.4,
  so this is load-dependent timing, not a desync. Do not diff these three cases
  across sweeps.
- Scattered Mana on a generated map was not played in the remote sweep; its
  coverage is `mapgen_test`, `cartographer_validation_test` and the screenshot
  games.
