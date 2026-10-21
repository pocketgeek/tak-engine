# 0.8.7 release validation

Protocol **284** (0.8.6: 281), replay format **13**, generator version **9**,
campaign payload **4**, database schema **9** (replay, generator, campaign and
schema unchanged since 0.8.6).
<!-- LEAD: re-check kNetVersion right before tagging.
     Version 0.8.7 and protocol 284 are already written in: CMakeLists.txt
     (project VERSION); README.md (badge, New in 0.8.7, random maps x2,
     multiplayer, overrides); docs/user-guide.md (intro, Versions, Replays,
     public server, old replays); docs/public-server.md;
     docs/random-map-generation.md (header line); docs/multiplayer-design.md
     (protocol summary, 282-284 replay history, the required-protocol line);
     these notes and the release notes.
     The README title alt text, the gallery footnote and docs/img/README.md
     were bumped with the recaptured pictures. If kNetVersion moves past 284,
     update every one of them. -->

Tagged `v0.8.7` at `47c09924` (annotated tag object `6ba21d88`) and published on 2026-10-21 06:00 UTC.

## Scope and compatibility

0.8.7 follows 0.8.6 (protocol 281). After the 0.8.6 validation commit
`bd7f3f6e`, main reached `e6425449` in 45 commits: 33 changes and 12 merges of
the task branches. `git diff --stat bd7f3f6e..e6425449 -- src` touches 7 files:
`src/sim/` (2: `legion.cpp`, `legion.h`), `src/net/` (3: `protocol.h`,
`client.cpp`, `client.h`), `src/server/` (1: `server.cpp`) and `src/client/`
(1: `gameview_lobby.cpp`). Nothing under `src/ai/`, `src/campaign/`,
`src/tnt/`, `src/cartographer/`, `src/terrain/`, `src/cob/`, `src/crt/` or
`src/util/` changed.

After `e6425449` the preparation added only documentation and pictures: the
recaptured screenshots (`d626a441`, `b0b6d6ad`), the new quick-start controls
reference `docs/quick-start.md` with six pictures under `docs/img/quick/`
(`ca7bfd72`, `ddc6ce95`), the 0.8.7 documentation with the CMake version bump
to 0.8.7 (`d71fdccd`, `9d899b21`) and this report. None of them touches
`src/`, `tools/` or the test lists, so the checks below, all run at
`e6425449`, stand for the release source; the only binary difference is the
version string (0.8.7 instead of 0.8.6).

The changes:

- **282** Legion at ~100 bodies, round 3 (landing `03a1d3a0`, bump `f86cceb2`;
  `4dbfd41c` passage greens scale with the crossing, `a6d4ce09` groups under 8
  queue at narrow passages, `0addb7a8`, `b766b76d` attack rings round still
  targets, `93eac5c7`, `b7f3a3bb` corridor-mouth funnel claims and own-slot
  arrivals; scenario baseline `30813a73`). Legion only.
- **283** Legion at ~100 bodies, round 4 (bump `e7a44c10`; `43e436f1`,
  `6ec272e6`, `3bffab7a` the turn ribbon, with points over 256 members keeping
  the pinwheel and a blocked file let go after 120 ticks; `c4e5ba39` slot
  wedge; `c6bab4da`, `684370a3`, `b6b11439` rings along walls; `7dadea58`,
  `0de09b45` fight and guard rings round still targets; `da132d1c`, `e56aa0c1`
  room at a merge; scenario baseline and navigation golden `e48b1c12`). Legion
  only.
- **284** lobby seat moves (`4ff11cd0` `Msg::MoveSeat` and MOVE HERE,
  `f5f59995` host slot edits that would seat or unseat a human are dropped,
  bump `e6425449`). The simulation is 283's.

**Retail is expected to be bit-identical to 0.8.6** (282 and 283 are Legion
only; 284 changes no sim state). `--mpai` Inner Circle seed 1 from the landing
commits: Retail 60 s `b353410003e98dd5`, 300 s `a3d953803a064191`; Legion 60 s
`0825aeb9e8139203`, 300 s `ce5c4ba7562e53b9`, all equal to 0.8.6, verified for
300 s on Linux, Windows and macOS.

**Compatibility.** `kNetVersion` is **284** in `src/net/protocol.h` at
`e6425449`, against 281 at `v0.8.6`. `kReplayFormat` is **13** in
`src/net/replayhdr.h`, the map generator's `formatVer` **9**
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` 4
(`src/net/crusades.h`) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`), all unchanged.

- **Multiplayer.** The server's Hello check rejects a client whose protocol
  differs from `kNetVersion`, so **0.8.7 cannot play with 0.8.6 or earlier in
  either direction**. The server and all players must update together.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  protocol equals `kNetVersion`, so 0.8.7 does not play 0.8.6 or older replays
  and those builds do not play 0.8.7 replays, although the file format is still
  13. `tools/replay_test.cpp` pins `kNetVersion == 284` and refuses 283, 282
  and 281.
- **Maps and settings.** Saved `.kmp` maps and generator-8/9 recipes carry
  over. A 0.8.6 settings file reads the same in 0.8.7; no settings keys were
  added.

## Local preparation checks

All checks ran from a clean detached worktree (`~/tak-tmp/wt/release-087-local`) at
`main` `e6425449749f` (protocol **284**, replay format **13**, generator recipe **9**, campaign
payload 4, database schema 9, all unchanged from 0.8.6 except the protocol), with `assets` and
`third_party` links to the main checkout. Toolchains: GCC 16.2.1, Clang 22.1.8, CMake 4.3.0,
Ninja 1.13.2, Python 3.14.8. Builds and tests ran on cores 4-23; the owned retail install served as
`--data` and `TAK_TEST_DATA`. The version bump is not in this commit, so binaries still call
themselves `0.8.6 (build v0.8.6-46-ge6425449749f)` (no `-dirty`).

**Result: no real regression in the engine or the tests, and every `--mpai` hash equals 0.8.6.**
Two points need the lead's attention, both about the Legion scenario gate and its baseline,
neither a simulation change: the committed baseline no longer fully covers the 140 fixtures
(see the scenario gate below), and the ASan suite needs `detect_leaks=0` on this machine
(SDL's own D-Bus start-up leaks). Everything else passed; the two unoptimized load failures
each passed when rerun alone.

- **Multiplayer and replays.** `kNetVersion` is **284** in `src/net/protocol.h` (281 in
  0.8.6). Replay format stays 13; the replay loader requires protocol == `kNetVersion`, so
  0.8.6 replays do not play and 0.8.6 cannot join a 0.8.7 server.
- **Builds.** Release, Debug, optimized Debug (`-O2`), Clang Debug and AddressSanitizer
  all-target builds completed with exit code 0: 873 steps in Release, 874 in the other four
  (same as 0.8.6). ccache and mold; static-deps and static Bink FFmpeg prefixes. Release
  emits 935 warning lines (538 `-Wmissing-field-initializers`, 332
  `-Wdeprecated-declarations`), the same kinds as 0.8.6 (937). Present in the log and harmless:
  `-Wunused-parameter` `dt` in four `sim.cpp` functions and `u` at `gameview_impl.cpp:2056`,
  `-Wunused-but-set-variable` `band0` at `legion.cpp:10190` (was 9539) and `ax`/`az` at
  `legion_flyers_test.cpp:584`, `-Wformat` `%zu` at `legion_world_test.cpp:1134`, a
  `-Warray-bounds` note inside `<compare>` from `legion.cpp` `yieldLane` (new this run, from
  libstdc++ 16 inlining) and `-Wmisleading-indentation` in test sources.
- **Full CTest.** **719** registered tests in the Debug trees (696 in 0.8.6) and **681** in
  Release (658); 23 added: `legion_attackflush`, `legion_cornermerge`, `legion_fightshuffle`,
  `legion_fightstill`, `legion_funnelmouth`, `legion_funneltie`, `legion_guardstill`,
  `legion_mixedrandom`, `legion_passage_green`, `legion_passage_small`, `legion_ribbon`,
  `legion_ringblind`, `legion_ringclosed`, `legion_ringnosight`, `legion_ringpocket`,
  `legion_ringroute`, `legion_ringroutespeed`, `legion_ringshadow`, `legion_ringshuffle`,
  `legion_ringstill`, `legion_ringwait`, `legion_slotwedge`, `server_seat_move`. None was
  removed. Each tree ran as `-LE nightly` (519 tests in Release, 557 in the others, which
  include the `pref_guard` fixtures) and the Release and optimized Debug trees also ran
  `-L nightly` (164).

  | Tree | Selection | Result | Time | Failures |
  | --- | --- | --- | ---: | --- |
  | Release | `-LE nightly`, 519 | **519/519** | 517 s | none |
  | Release | `-L nightly`, 164 | **164/164** | 1,675 s | none (the baseline retake worked: 0.8.6 had 57 `legion_check_*` failures) |
  | Optimized Debug | `-LE nightly`, 557 | 556/557 | 719 s | `voice` (load; passes alone) |
  | Optimized Debug | `-L nightly`, 164 | **164/164** | 1,289 s | none |
  | Debug (`-O0`) | `-LE nightly`, 557 | 556/557 | 3,610 s | `override_transfer` (load; passes alone) |
  | Clang Debug | `-LE nightly`, 557 | **557/557** | 2,746 s | none |
  | ASan | `-LE nightly`, 557 | 537/557 | 1,517 s | 20 tests, all LeakSanitizer reports inside SDL's D-Bus init (see below); **557/557 with `ASAN_OPTIONS=detect_leaks=0`** |

  The unoptimized trees ran at the same time as the optimized ones, the scenario gate and
  the `--mpai` runs (load average 25-30 on 20 cores for the first hour).
  - **`voice` (optimized Debug, existing load-sensitive audio test): load.** `FAIL
    another player's VOLUME leaves this talker alone: 0.510864` while the SDL disk-audio
    engine ran next to the other tests. It passed alone (21 s, rerun) and in Release, Debug,
    Clang Debug and (with leak detection off) ASan. `voice` was not in 0.8.6's failure
    list, and `src/` has no voice change since 0.8.6, so treat it as a first sighting of a
    timing flake.
  - **`override_transfer` (Debug, third run in a row on a loaded machine): load.** `OVERRIDE
    PACKS VERIFIED; SAVE FAILED: filesystem error: cannot create directories: Not a
    directory [.../peer/OverrideCache]`, the same text as in 0.8.6's Clang Debug and ASan
    runs. It passed alone (23 s) and in the other four trees. Neither the test nor
    `overridepackage.cpp` changed since 0.8.5, so this is a pre-existing intermittent
    failure of the test's scratch directory under load; 0.8.6 already flagged it.
  - **ASan: 20 tests fail only on leak reports.** `script_transient`, `script_transient_263`,
    `creon_death_sound_peers`, `voice`, `streaming_capture`, `stream_key`,
    `render_palette_indexed`, `render_native_pass`, `client_restart`, `shadow`, `fogupload`,
    `menu_music`, `menu_music_roster`, `death_sound`, `screen_lifecycle`,
    `campaign_presentation_roster`, `death_sound_roster` and the three
    `crusades_*_ui_network` tests (the ones that start an SDL client). Every report is a
    `Direct leak ... SDL_DBus_Init` (small allocations in the statically linked SDL's D-Bus
    set-up, which runs in any process that creates an SDL video or audio context here); there is no
    `ERROR: AddressSanitizer` (no heap overflow, use-after-free or stack error) anywhere in the log. Unsetting
    `DBUS_SESSION_BUS_ADDRESS` did not help (20/22 still fail); with `ASAN_OPTIONS=detect_leaks=0`
    the same 22 tests (20 plus the two `pref_guard` fixtures) pass 22/22. 0.8.6's ASan run did not hit this;
    no TAK source touching SDL start-up changed, so the cause is the machine's session or system
    bus (not verified further). The 537 other ASan tests passed with leak detection on.
  - **The unoptimized Legion tests passed under the 0.8.4 limits.** `legion_sched_inflight`
    917 s / 709 s / 405 s, `legion_scale_neutral` 881 s / 742 s / 426 s,
    `legion_route_paused` 1,337 s / 997 s / 506 s and `legion_parallel` 3,124 s / 2,453 s /
    1,517 s (Debug / Clang Debug / ASan); the largest use of a limit is `legion_parallel` in
    Debug, 58% of 5400 s. `movement_orders_trolls` 314 s / 306 s / 108 s (limit 600 s: Debug at
    52%). `terrain_schedule` 72 s / 72 s / 37 s, `voice_uplink` 49 s, and the new
    `server_seat_move` (3-6 s) passed everywhere.
  - **The settings-leak guard held.** `pref_guard_setup`/`pref_guard_check` passed in every
    selection. The real profile's `settings.ini` has the same SHA-256 and mtime before and
    after (`real-profile-{before,after}.txt`). `TAK_SETTINGS_FILE` was not exported.
- **`--mpai` golden hashes: all unchanged from 0.8.6.** Each ran once on the optimized Debug
  build, Inner Circle, `takserver --local --no-auth --seed 1`, an empty `XDG_DATA_HOME`, and
  `TAK_LEGION=1` for Legion. No desync or referee-suspect line in any log. `end=timelimit`,
  `err=none` in all four.

  | Mode | Duration | `e6425449` (284) | 0.8.6 (281) |
  | --- | ---: | --- | --- |
  | Legion | 300 s | `ce5c4ba7562e53b9` (18 units) | `ce5c4ba7562e53b9` |
  | Retail | 300 s | `a3d953803a064191` (13 units) | `a3d953803a064191` |
  | Legion | 60 s | `0825aeb9e8139203` (4 units) | `0825aeb9e8139203` |
  | Retail | 60 s | `b353410003e98dd5` (4 units) | `b353410003e98dd5` |

  Optimized Debug binary SHA-256:
  - client `0907e1c7737a01b99e13b05c2cdaef83a6c4496d7b4f30ab8ac374495dedd223`
  - server `d60683016e898ff59be49426fea2d283aaddc8ab75fbb20adeae6060df205837`
- **Determinism.** In `tools/check-determinism.sh`, the GCC and Clang x86-64 builds at
  O0/O2/O3 all agree on `dcef618cd2e4d558`, the same as 0.8.6. The aarch64 legs are skipped
  (target headers missing). `tools/check-detmath.sh` passes, including the `std::atomic_ref` guard.
- **Legion scenario gate: the 135 baselined fixtures all pass; FAIL on 15 Retail-floor keys of
  five fixtures that have no baseline entries.** The gate ran the way
  `tools/legion_nightly.sh` stage 2 does: every `tools/scenarios/*.scn` (140 files; 137 in
  0.8.6), optimized Debug `legion_scenario`, `--mode both --json --offsets gate --data <retail
  install>`, one file per core on cores 4-11, then `legion_check.py check --baseline
  tools/scenarios/baseline.json`. All 140 files exited 0 (`problems.txt` empty);
  `legion_check.py validate --baseline tools/scenarios/baseline.json` reports `PASS: 0
  problems`. Run time 4,558 s (the machine was loaded).
  - **Result:** `FAIL: 32143 keys checked, 15 failed, 4312 ratchets, 4668 licensed, 240 floor
    exceptions, 864 report-only` (0.8.6: 3,205 failed). The key count equals the one the
    baseline commit `e48b1c12` records ("legion_check check PASS (32,143 keys)"). Run with
    `--cumulative`, as 0.8.6's gate did, there is one more failure, `battle-field-2x60/legion/work.legion_total.p99:
    cumulative combat cost: 123801 above the W0 base 116526`, a W0-reference check the nightly does not use.
    **Every `hash@` and metric key of the 135 baselined fixtures matches; no Retail key fails;
    all `legion_check_*` ctests pass.**
  - **The 15 failures are all in fixtures the baseline has no entries for** (`check` prints
    `has no baseline entries` for exactly these five, both modes): `attackring-gatehouse-40` (8
    keys: `reach.ring.damage_*`, `reach.ring.ever_*`, `wall_touch*_permille`; Legion 5 in reach
    vs Retail 15, ring damage 167 vs 584), `attackring-fight-diag-60` (2 keys,
    `reach.ring.damage_end` 1,709 vs 2,183 and `_t2400` 889 vs 1,303), `taperbridge-100` (3 keys:
    `contact_other_permille` 65 vs 26, `scale.complete.t90` 11,029 vs 6,038, `scale.start.t90` 13 vs 3), and
    `taperbridge-small` (2 keys: `scale.complete.t90` 9,999 vs 8,114, `scale.start.t90` 12 vs 2);
    `ringbend-pocket` has no entries but no failing key. Three of the five `.scn` files were added after 0.8.6
    (`attackring-gatehouse-40`, `attackring-fight-diag-60`, `taperbridge-small`); with no entry
    there is no Retail-floor exception to license them, so the floor reads them raw. The gatehouse and
    fight-diag rows are the documented "wall/gatehouse rings hold fewer attackers in reach than
    Retail" limit (next round). The taperbridge rows are the "a few fixtures arrive later than
    Retail" limit, but the `scale.complete.t90` gap here (Legion 1.8x and 1.2x Retail's tick) is far above 5%; the
    lead should decide whether to baseline these fixtures (`legion_check.py baseline`, with exceptions)
    or state the limit in the notes.
  - No baseline was regenerated.
- **Retail group emulation check (formation-air).** `python3 -B tools/re/check_retail_group.py
  --build build-rel` (400 cases) passed every section: tick 400, query 4000, check 2000, formation
  2000, vtolcheck 2000, vtolform 2000.
- **Python research suite.** **165 tests** passed (`python3 -B -m unittest discover -s tools/re
  -p '*_test.py'` with `TAK_AUTH_RETAIL_DIR` set and `build/` pointing at the Release tree), no
  skip. The owned-retail archive verifier (`verify_official_hpi_test.py`) passed all **6
  tests**. The SHA-256 sums of the 16 retail HPI archives and `Kingdoms.key` are identical
  before and after the runs.
- **Syntax checks.** All **465** tracked Python files compile. All 22 `*.sh` scripts
  (21 tracked plus `tools/tak-netem`) pass `bash -n`.

Logs: `~/tak-tmp/scratch/release-087/` (`cfg-*.log`, `make-*.log`, `build-status.txt`,
`ctest-*.log`, `rerun-{o2,dbg,asan,asan-noleak}.log`, `mpai/`, `mpai.sh`, `gate/`
(`scenarios.jsonl`, `check.txt`, `check-nightly.txt`, `validate.txt`), `check-determinism.log`,
`check-detmath.log`, `check_retail_group.log`, `python-suite.log`, `verifier.log`,
`pycompile.log`, `bashn.log`, `o2-sha256.txt`, `retail-sha-{before,after}.txt`,
`real-profile-{before,after}.txt`).

## Remote multiplayer sweep

The full 37-scenario table passed for both pathfinding modes, Retail and Legion, on
**2026-10-21** (UTC; evening of 2026-10-20 local), both on `main` at `e6425449749f`
(protocol **284**, replay format 13, build id `v0.8.6-46-ge6425449749f`; the version
string still reads 0.8.6 until the 0.8.7 bump lands):

- **Retail:** 01:44-02:02 UTC (18 minutes including the gates).
- **Legion:** 02:02-02:30 UTC (28 minutes including the gates).

Referees ran on `tak.pgnet.us` (24 runs, 10 at a time) and `vpn3.pgnet.us` (13 runs, 6
at a time), with optimized local clients pinned to CPUs 4-23. Each scenario allowed ten
simulated minutes (18,000 ticks) or ended at an actual result for that seat. Real seated
players provide the client/referee checksum coverage; the two all-AI spectator scenarios
per mode (`w-allai-stress`, `w-allai-bench`) test flow control, not hashes.

The harness picks the mode through the hosting client's room options: `TAK_LEGION=1`
creates a Legion room, and without it the room is Retail. Every recorded referee replay
header confirms the intended mode, replay format 13 and protocol 284: 37 headers per
mode, seeds 2000-2036 each mapped to its case.

| Mode | Completed scenarios | Completed seats | Client-pair checkpoints | Client-referee checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Retail | 37/37 | 52/52 | 10,264 across 22 seat pairs, 0 mismatched | 2,626 across 51 seat/referee comparisons, 0 mismatched |
| Legion | 37/37 (single pass, no retries) | 52/52 | 10,235 across 22 seat pairs, 0 mismatched | 2,694 across 51 seat/referee comparisons, 0 mismatched |

Each run passed all three `--validate` gates before its table started:

- **Build identity:** `both at v0.8.6-46-ge6425449749f`.
- **Planted desync:** the referee reported `game 1: client 1 DESYNCED at tick 900`.
- **Override transfer:** `verified packs transferred and incorrect loaded gameplay rejected`.

The gate checks only `tak.pgnet.us`; the binary on `vpn3.pgnet.us` has the same SHA-256
and the same `--version` build id (checked by hand after deployment). No natural
`DESYNCED`, `REFEREE SUSPECT` or client-reported desync appeared in any case log; the
only such line in each run is the planted one in `validate.server.log`. Both sweeps ended
with "no desyncs reported (52 seats, all completed)" and an empty "runs that did not
complete" list. A state hash is compared at every checkpoint; the position digest is
compared as well wherever both sides carry one. Every game wrote a referee replay in both
runs: 37 per mode, matched to cases by the seed in the replay header. The checkpoint
comparison script is the 0.8.6 one, unchanged.

**No Legion retries.** The Legion run used `TAK_WALL_TIMEOUT=3600` from the start; every
seat completed in the single pass.

### Reference ticks and hashes (0.8.7 candidate)

Hash and units are the seated host's final line. "vs 0.8.6" compares tick and hash with
the published 0.8.6 table (`575a70d2`, protocol 281; same harness, hosts and seeds). The
six `TAK_AUTOPLAY` cases never repeat a hash, and `h-lake`, `h-stress`, `2h-stress`,
`2h-gods`, `4h-gods` are load-sensitive (see 0.8.5's notes), so "differs" in those rows
is expected noise.

| Case | Retail end | Retail tick | Retail hash | Retail units | vs 0.8.6 | Legion end | Legion tick | Legion hash | Legion units | vs 0.8.6 |
| --- | --- | ---: | --- | ---: | --- | --- | ---: | --- | ---: | --- |
| `h-baseline` | timelimit | 18,000 | `4a07030c0169e47c` | 210 | same | timelimit | 18,000 | `4b9217a4ffcd74c0` | 213 | differs |
| `h-gods` | concluded | 14,626 | `0ea0a1d96c1414af` | 104 | same | concluded | 15,383 | `3f26557d500cff5a` | 82 | differs (tick 15,425 -> 15,383) |
| `h-crusades` | timelimit | 18,000 | `604de77d3fe53f3e` | 245 | same | timelimit | 18,000 | `e4036388d6f2e4df` | 230 | differs |
| `h-stress` | concluded | 5,387 | `75570f6ee247c83c` | 8,384 | differs (tick 4,891 -> 5,387) | concluded | 5,013 | `902e76f6157d9b19` | 7,650 | differs (tick 5,025 -> 5,013) |
| `h-absurd` | timelimit | 18,000 | `1b0d79cb6583219d` | 454 | same | timelimit | 18,000 | `9125e38fec71fc29` | 469 | differs |
| `h-fog-explored` | timelimit | 18,000 | `00419b0edd6c9b06` | 188 | same | timelimit | 18,000 | `faafb450fc7005c7` | 142 | differs |
| `h-fog-full` | timelimit | 18,000 | `9896fd941b2aade9` | 216 | same | timelimit | 18,000 | `821e2ab5feef82d1` | 217 | differs |
| `h-unitcap` | concluded | 14,780 | `c382486eeb54fb3b` | 204 | same | timelimit | 18,000 | `715966bbb3c6a746` | 220 | differs |
| `h-cramped` | concluded | 1,307 | `45134591344a4d04` | 10 | same | timelimit | 18,000 | `57d4067d04331580` | 29 | differs |
| `h-naval` | timelimit | 18,000 | `5fb53d62d7a0b42c` | 58 | same | timelimit | 18,000 | `17b9ca802fb9a217` | 88 | differs |
| `h-naval-crus` | timelimit | 18,000 | `396f8de68f25c274` | 70 | same | timelimit | 18,000 | `4787a6c75fbb0d77` | 88 | differs |
| `h-lake` | concluded | 3,959 | `2e7cef791b18081c` | 4,918 | differs (tick 3,944 -> 3,959) | concluded | 4,130 | `815544d41bba4eed` | 4,887 | differs (tick 4,002 -> 4,130) |
| `h-random-starts` | concluded | 15,702 | `6e0395e042f734fe` | 228 | same | concluded | 12,289 | `0cba4154cf002db9` | 194 | differs |
| `h-monarch-exp` | concluded | 12,231 | `c646ef640c32a947` | 89 | same | concluded | 17,804 | `540592e154eafb24` | 82 | differs (tick 17,505 -> 17,804) |
| `h-overrides-full` | timelimit | 18,000 | `450038fec3e01ce5` | 209 | same | timelimit | 18,000 | `adc0b6b5090b5ef5` | 215 | differs |
| `h-everything` | concluded | 13,549 | `8ada2d5383b95f0a` | 5,140 | same | concluded | 11,615 | `5cf4d36c20ad7602` | 4,790 | differs (tick 18,000 -> 11,615) |
| `h-speed-1x` | timelimit | 18,000 | `9b5694cceca37a11` | 236 | same | timelimit | 18,000 | `0c1cf293aa498699` | 246 | differs |
| `h-two-castles` | timelimit | 18,000 | `8e9dfa2311fc1ec4` | 23 | same | timelimit | 18,000 | `81b4f5bfafb359c0` | 4 | differs |
| `2h-baseline` | timelimit | 18,000 | `7b5850e128c0ff63` | 173 | same | timelimit | 18,000 | `638fa388b12be0c4` | 172 | differs |
| `2h-gods` | concluded | 12,423 | `bf5aaf973f862369` | 104 | same | concluded | 13,059 | `9283d91d154a8678` | 61 | differs (tick 13,039 -> 13,059) |
| `2h-crusades` | timelimit | 18,000 | `4177d6f4374aad35` | 182 | same | timelimit | 18,000 | `e14b49adbbf76ebf` | 167 | differs |
| `2h-stress` | concluded | 5,058 | `9f12c5430cf39071` | 9,100 | differs (tick 5,048 -> 5,058) | concluded | 5,258 | `20a0ee08afc437cb` | 8,432 | differs (tick 5,313 -> 5,258) |
| `2h-orders` | timelimit | 18,000 | `79a9d2230acc797b` | 193 | differs | timelimit | 18,000 | `2a302753f0f48798` | 188 | differs |
| `2h-orders-stress` | concluded | 4,802 | `9d2e04080280c963` | 9,171 | differs (tick 5,687 -> 4,802) | concluded | 5,720 | `3a424353c938d2e5` | 7,730 | differs (tick 18,000 -> 5,720) |
| `4h-orders` | timelimit | 18,000 | `5a92023a6ad64262` | 150 | differs | timelimit | 18,000 | `93eb539dcbd0d17b` | 163 | differs |
| `3h-baseline` | timelimit | 18,000 | `1d59d944a244aef4` | 172 | same | timelimit | 18,000 | `beeeab4bb6ec584b` | 148 | differs |
| `4h-gods` | timelimit | 18,000 | `9425d45754a87784` | 142 | differs | timelimit | 18,000 | `f11b204de695bf75` | 129 | differs |
| `net-rtt50` | timelimit | 18,000 | `80dc6a72233bd05d` | 230 | same | timelimit | 18,000 | `6d0d693c7500418b` | 227 | differs |
| `net-rtt100` | timelimit | 18,000 | `fcba4a56baf98c45` | 190 | same | timelimit | 18,000 | `c3167c3d271f43bc` | 204 | differs |
| `net-jitter` | timelimit | 18,000 | `6a0f640c3faf1de6` | 173 | same | timelimit | 18,000 | `ca07f9821f97109e` | 196 | differs |
| `net-rtt500` | timelimit | 18,000 | `8837809d723b6fc0` | 219 | same | timelimit | 18,000 | `8ddcf8fcc178d917` | 195 | differs |
| `net-loss1` | timelimit | 18,000 | `1adf2c3d366e9bef` | 192 | same | timelimit | 18,000 | `25a8d84c6a74a607` | 209 | differs |
| `net-loss3-orders` | timelimit | 18,000 | `8a7c6ca2a9d7316f` | 193 | differs | timelimit | 18,000 | `38a4d7611b0ad5d4` | 204 | differs (tick 17,281 -> 18,000) |
| `net-orders100` | timelimit | 18,000 | `a3c76fa21af7e08b` | 210 | differs | timelimit | 18,000 | `64e14440b5d1b078` | 170 | differs |
| `net-2h-rtt100` | timelimit | 18,000 | `2499956b0c81cb21` | 161 | differs | timelimit | 18,000 | `3f2a4edec6d50004` | 140 | differs |
| `w-allai-stress` | timelimit | 18,000 | `06e78e00b732019a` | 5,392 | same | timelimit | 18,000 | `4815dcbab4f8cf65` | 3,838 | differs |
| `w-allai-bench` | timelimit | 18,000 | `10a3a871daabf505` | 312 | same | timelimit | 18,000 | `bd398ca368de8b2e` | 379 | differs |

### Comparison with 0.8.6

The reference is 0.8.6's own sweep logs (`~/tak-tmp/scratch/release-086/remote/`), not a
fresh re-run of 0.8.6.

**Retail: unchanged, as designed.** 27 of 37 cases are bit-identical to 0.8.6 (tick and
hash), including every deterministic Ulasem Arena case (`h-baseline`, `h-gods`,
`h-crusades`, `h-absurd`, `h-everything`, `h-unitcap`, ...), `h-cramped` (1,307,
`45134591344a4d04`), `2h-gods`, all the impaired-network cases except the autoplay ones,
and the spectator `w-allai-stress` (5,392 units, `06e78e00b732019a`) and `w-allai-bench`.
The 10 that differ are exactly the known unstable ones:

- The six `TAK_AUTOPLAY` cases (`2h-orders`, `2h-orders-stress`, `4h-orders`,
  `net-orders100`, `net-loss3-orders`, `net-2h-rtt100`), which never repeat a hash.
- Four of the five load-sensitive cases: `h-lake` (3,944 -> 3,959, 4,918 units),
  `h-stress` (4,891 -> 5,387, 8,384 units), `2h-stress` (5,048 -> 5,058, 9,100 units) and
  `4h-gods` (18,000 ticks, 142 units vs 111). `2h-gods` happened to repeat.

I did not re-run these four to show them wobbling run-to-run in this sweep; they are
classified by 0.8.6's finding (same source, repeated sweeps, different ticks). The
Retail-visible changes in Legion rounds 3/4 and the lobby seat move do not touch Retail
simulation state, which also matches `--mpai` Inner Circle seed 1 being unchanged.

**Legion: every case moves, and that was expected.** All 37 differ from 0.8.6, since
rounds 3 and 4 (protocols 282/283: attack rings, passage queues, corridor-mouth funnels,
the turn ribbon, merge-at-corner) change formation movement, ring admission and queueing
everywhere; the protocol 284 lobby change touches no simulation. Notable movements:

- `h-everything` (Ulasem Arena, everything on) now `concluded` at 11,615 with 4,790 units,
  where 0.8.6 ran to the 18,000-tick limit with 5,588 units.
- `h-monarch-exp` concludes at 17,804 (was 17,505); `h-gods` 15,383 (was 15,425); `2h-gods`
  13,059 (was 13,039); `h-lake` 4,130 (was 4,002); `h-stress` 5,013 (was 5,025);
  `2h-stress` 5,258 (was 5,313). `h-random-starts` concludes at the identical tick 12,289
  with 194 units (195).
- `h-cramped` still runs to 18,000 ticks but ends at 29 units (0.8.6: 43).
- `h-fog-explored` 142 units (171); `4h-gods` 129 (172, load-sensitive); `h-absurd` 469
  (491); `w-allai-bench` 379 (423); `w-allai-stress` 3,838 (4,084).
- Autoplay: `2h-orders-stress` now concludes at 5,720 (7,730 units) where 0.8.6 hit the
  time limit; `net-loss3-orders` hit the time limit where 0.8.6 concluded at 17,281. Both
  are autoplay, so neither is a regression signal.

In every case the seats and the referee agreed at every checkpoint, so these are changes
of simulation behaviour between versions, not desyncs. They are part of why 0.8.7
(protocol 284) is neither multiplayer- nor replay-compatible with 0.8.6 (protocol 281).

**Impaired networks.** All eight impaired scenarios (`net-rtt50`, `net-rtt100`,
`net-jitter`, `net-rtt500`, `net-loss1`, `net-loss3-orders`, `net-orders100`,
`net-2h-rtt100`) ran under netem in both runs and completed. No `netem` or `prio` qdisc
was left on either host.

**Lobby seat moves.** No scenario moves a seat after joining, so the sweep only shows
that the new `Msg::MoveSeat` handling does not disturb ordinary hosting and joining; the
move itself is covered by the `server_seat_move` test.

**Isolation.** The referee was a separate test executable, `~/takserver.sweep087`, on
test ports 7890-7936 (Retail) and 7990-8036 (Legion); the public service uses 7677. It
used per-mode map caches `~/tak_cache.sweep087-{retail,legion}` and replay directories
`~/tak_replay.sweep087-{retail,legion}`. The public `/usr/bin/takserver` on tak.pgnet.us
was not replaced, stopped or restarted: PID 25649, port 7677, same PID before and after
(it had run 18 h 35 min when the sweep started). No certificates were touched. `vpn3`
runs no takserver service.

**Cleanup.** After the sweeps, on both hosts: no process was executing the test binary,
no listener remained on the test ports, and the test binary, map caches, replay
directories and `/tmp/tak-{srv,val,neg}-*.log` files were removed after the referee
replays had been copied locally.

**Binaries.** Built from a clean detached worktree (`~/tak-tmp/wt/release-087-val` at
`e6425449`) with optimized Debug (`-O2 -g`) for both referee and client.

- **Referee:** Ubuntu 24.04 container (`tak-sweep-builder-0719`), GCC 13.3,
  `TAK_BUILD_CLIENT=OFF`, max symbol version GLIBC_2.38. On each host the deployed
  SHA-256 matched the build.
- **Client:** Fedora, GCC 16.

| Binary | SHA-256 (`e6425449`, `v0.8.6-46-ge6425449749f`, protocol 284) |
| --- | --- |
| Referee | `deb842b044e151c482d89d79f8d3e666c2e3a047ab7361be135761a975e273a2` |
| Client | `04b35161cd149e8d4527ae8e85673f9e9e7e6901d0193f6c54693c84fa4c9dff` |

**Harness.** The committed `tools/desync-hunt-remote.sh`, unmodified; its SHA-256,
`0ec213448db4f15df1ada6bd5c4bf8ab8d1bbe719e0d11bf03776200e2ba365b`, is unchanged since
0.8.5. The SSH control sockets used `TMPDIR=/home/pocket_geek/tk` (short path).

**Mixed versions.** None played: 0.8.7 speaks protocol 284, and the server's Hello
rejects any client at another protocol.

Logs: `~/tak-tmp/scratch/release-087/rs/remote/` (`{retail,legion}.sweep.log`,
`*-checkpoints.json`, `logs-{retail,legion}/`, `ref-{retail,legion}/` referee replays,
`table.md`).

## CI and packages

Main CI on the final source `47c09924` passed on the first attempt: Windows 39547793293, Linux packages 39547793294 and macOS 39547793307. Determinism is path-filtered and was not triggered by that notes-only commit; it ran on the tag (below).

On the exact tag (`47c09924`, annotated tag object `6ba21d88`), all four workflows passed on the first attempt. No job needed a rerun, so none of the known flakes appeared: macOS x64 `stream_test` timing, "Upload CI artifact: ENOTFOUND", macOS x64 `hdiutil` "Resource busy", the Fedora openh264 mirror, and the Ubuntu 22.04 `crusades_client_network_test` "complete replay not published".

- [Determinism and core tests](https://github.com/pocketgeek/tak-engine/actions/runs/39547815025): started manually on the tag because the workflow is path-filtered.
- [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/39547815459): all native package jobs and the release attachment.
- [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/39547815496): x64, ARM64 and the x64 signing/package job.
- [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/39547815464): both builds plus the **Sign + notarize** job for each architecture. That job replaced the unsigned macOS ZIPs and DMGs in the draft with signed, notarized and stapled ones.

The release was created as a draft with these notes, and tagged CI attached the packages. All **29 assets** were downloaded after the macOS signing finished, the same set as 0.8.6. Each one matched GitHub's SHA-256 digest and byte size before publication. Every asset is within 0.4% of its 0.8.6 size, with ratios from 1.0015 to 1.0036.

- **Build ids.** The client and server in every Linux package carry `v0.8.7`. That covers Debian 12 and 13, Ubuntu 22.04, 24.04 and 26.04, Fedora 44 and openSUSE Leap 16.0 (each for both architectures), plus Arch, Slackware and both Linux debug tarballs.
  - On Linux, the x86-64 `takclient` and `takserver` report `0.8.7 (build v0.8.7)`. That holds for Debian 12 and 13, Ubuntu 22.04, 24.04 and 26.04, Fedora 44, openSUSE Leap 16.0, Slackware and the x64 debug tarball. So does the Arch `takserver`. The Arch `takclient` needs glibc 2.44, newer than the checking host's 2.43, so only its build string was checked. For the arm64 binaries, only the build string `v0.8.7` was checked.
  - On Windows 11, the x64 Windows ZIP's `takclient.exe` and `takserver.exe` report `0.8.7 (build v0.8.7)`. For the ARM64 `takclient.exe` and `takserver.exe`, only the build string `v0.8.7` was checked.
  - On the arm64 MacBook (macOS 26.6.2), `takclient` and `takserver` report `0.8.7 (build v0.8.7)` for both architectures. This holds for the bare programs from the ZIPs and for the copies inside the game app from each DMG and each ZIP. The x64 copies ran under Rosetta.
- **Windows signatures.** `Get-AuthenticodeSignature` on oden-win reports **Valid** (CN=Curtis Edge, timestamped) for 9 signed files, the same count as 0.8.6. They are the setup `.exe`, plus `takclient.exe`, `takserver.exe`, `cartographer.exe` and `crusades_admin.exe` in both the x64 and ARM64 ZIPs. The debug ZIPs were not checked.
- **macOS notarization.** On the MacBook, both DMGs pass `xcrun stapler validate` and `spctl` (open, primary signature) as **accepted, source=Notarized Developer ID, origin=Developer ID Application: Curtis Edge (L479Y59RN8)**.
  - Both apps (the game and TAK Cartographer) pass both checks the same way, from each DMG and from each ZIP.
  - Every bundled and bare `takclient`, `takserver`, `cartographer` and `crusades_admin` passes `codesign --verify --strict` (TeamIdentifier L479Y59RN8).
  - `spctl` says the bare command-line programs do not seem to be an app. That is expected for unstapled command-line tools.
- **Opus is static.**
  - No packaged Linux `takclient` or `takserver` has a `NEEDED` entry beyond libc, libm and the loader (Slackware also lists libdl and libpthread), so none needs libopus. The Debian and Ubuntu packages depend only on `libc6`, and the RPMs require only libc, libm and the loader.
  - No Windows `.exe` imports or names an Opus DLL, and the ZIPs ship no DLLs. The x64 import tables list only system DLLs.
  - No macOS binary links a libopus dylib (`otool -L`).
  - `OPUS-LICENSE.txt` is in every Debian, RPM, Arch and Slackware package, in both Linux debug tarballs, in all four release ZIPs (inside the game app's Resources on macOS) and in the four debug ZIPs.
- **macOS microphone.** The game app's `Info.plist` carries `NSMicrophoneUsageDescription` in both DMGs and both ZIPs. The `com.apple.security.device.audio-input` entitlement is on the game app's `takclient` (DMG and ZIP, both architectures) and on the bare ZIP `takclient`. It is absent from `takserver`, `crusades_admin` and `cartographer`.
- **macOS Retina.** `NSHighResolutionCapable` is `true` in the `Info.plist` of both apps (the game and TAK Cartographer), from both DMGs and both ZIPs.
- As in 0.8.1 to 0.8.6, no packaged Linux `takclient` has a `NEEDED` entry for libsecret, GLib, GIO or GObject.
- The new `docs/quick-start.md` controls reference is in the source tree at the tag (`git show v0.8.7:docs/quick-start.md`).

The release was published on 2026-10-21 06:00 UTC and is the latest release.

## Screenshots

Recaptured on the 0.8.7 candidate (`e6425449` plus the local version bump) with
the 0.8.6 capture settings: Debug `-O2` OpenGL build with the 0.8.6 capture-only
patch (not committed), the same scenes and jobs, a scratch install and scratch
`HOME`/`XDG_DATA_HOME` (the real profile was not written). Every picture was
looked at after capture.

- **Recaptured:** `title.jpg` (VERSION 0.8.7), `room.jpg` (single-player room
  on Ulasem Arena, MOVE HERE on open rows 5-8), `cartographer/test-lobby.jpg`
  (Test Map lobby, MOVE HERE on seats 2-4, now with the map preview), and the
  scenes retaken each release: `battle.jpg`, `dragons.jpg`, `flyers.jpg`,
  `hud.jpg`, `naval.jpg`, `guide/benchmark.jpg`, `guide/scorecard.jpg`,
  `guide/fire-on-the-move.jpg`, `guide/flyers-retail-legion.jpg`. Retail and
  the server AI did not change, so the standard scenes differ from 0.8.6 only
  slightly (mean pixel difference 0.4-3.6; 0.1-1.8% of pixels changed by more
  than 40).
- **New:** the six `docs/quick-start.md` pictures in `docs/img/quick/`
  (`box-select.jpg`, `selected.jpg`, `attack-cursor.jpg`,
  `command-panel.jpg`, `build-placement.jpg`, `lobby-move-here.jpg`), each
  under 185 KB. The capture patch gained press/move/release entries for
  `TAK_SHOT_CLICKS` so the drag pictures could be taken; it is not committed.
- **Kept:** `guide/voice-chat.jpg`, `lobby.jpg`, `lobby-scenario.jpg`,
  `guide/random-map-lobby.jpg` and every other picture (no slot table shown,
  screens unchanged; the three lobby pictures are create screens).
- **Captions:** the README title alt text (version 0.8.7), the gallery
  footnote (0.7.25 to 0.8.7 builds), the `room.jpg` alt text and caption
  (README and user guide), the `test-lobby.jpg` alt text in the Cartographer
  guide and `docs/img/README.md` (recaptured and kept lists, the room and
  test-lobby methods, a Quick start section) were updated to match.
- **Seat-move evidence (not committed):** before/after captures of a
  single-player room moving the player to row 6 under START LOCATIONS: FIXED
  and BY SLOT. In BY SLOT the player's marker moved from start 1 to start 6 and
  start 1 became an open marker; in FIXED row 6 became the player (Aramon,
  blue, team 1, colour and team carried), row 1 became OPEN with its own MOVE
  HERE, and the preview reordered the starts. MOVE HERE was also checked by eye on
  every open row of a single-player room at 1600x900 and 1024x768 and a
  multiplayer host room at 1024x768 (it never shares a row with READY, and is
  absent on AI and closed rows).

## Limits

- No game mixing 0.8.6 and 0.8.7 builds was played, and none can be (protocol
  284 against 281, the Hello gate and the replay loader).
- **Legion units still stand and wait more than Retail's**, though they arrive
  sooner (a user ruling: cosmetic).
- **Legion rings along walls and gatehouses** hold fewer attackers within reach
  than Retail's (`attackring-gatehouse-40`, `attackring-wall-64`); next round.
- A few Legion fixtures arrive later than Retail. The scenario gate's Retail
  floor names two without baseline entries where the gap is larger than the
  notes' "under 5%": `taperbridge-100` (`scale.complete.t90` 11,029 against
  Retail's 6,038) and `taperbridge-small` (9,999 against 8,114).
  Lead decision: not baselined. The taperbridge gap is the bridge lottery carried over
  from 0.8.6 and is now named in the notes and the guide; the gatehouse and fight-diag
  rows are round 5's target, so they get baselines after that round.
- Carried over: the Legion 10,000 program still does not meet its own success
  criterion for congested workloads, and many-10000, scatter-10000 and
  mixed-10000 still run over the 33.3 ms tick budget in part of their ticks;
  mixed-10000's flyers hover over a slow ground army. Retail `h-cramped` ends
  early because the idle seat starts with only its Monarch. The macOS Retina
  UI Scale is migrated once. A stationary auto-targeting shooter keeps refusing
  a once-blocked target.
- Flakes in this release's runs: `voice` failed once in the optimized Debug
  tree under load (first sighting; passed alone), `override_transfer` failed
  once in the Debug tree under load (as in 0.8.6; passed alone), and the ASan
  tree needs `ASAN_OPTIONS=detect_leaks=0` here because SDL's D-Bus start-up
  reports leaks in the 20 tests that start an SDL client.
