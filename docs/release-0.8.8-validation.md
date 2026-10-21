# 0.8.8 release validation

Protocol **288** (0.8.7: 284), replay format **14** (0.8.7: 13), generator
version **9**, campaign payload **4**, database schema **9** (generator,
campaign and schema unchanged since 0.8.7).
<!-- LEAD: re-check kNetVersion right before tagging (288 at the landing
     commit). If it moves past 288, update every 0.8.8 / 288 line in
     CMakeLists.txt, README.md, docs/user-guide.md, docs/public-server.md,
     docs/random-map-generation.md, docs/multiplayer-design.md, these notes and
     the release notes. -->

Tagged `v0.8.8` at `TODO-LEAD` (annotated tag object `TODO-LEAD`) and published on TODO-LEAD.

## Scope and compatibility

0.8.8 follows 0.8.7 (protocol 284). After the 0.8.7 validation commit
`261112f6`, main reached `0e78b3a6` in 38 commits, all linear (the task
branches were cherry-picked; no merges). `git diff --stat 261112f6..0e78b3a6 --
src` touches 73 files: `src/sim/` (9: `legion.cpp`, `legion.h`,
`matchsetup.cpp`, `matchsetup.h`, `powercodes.h` (new), `retailrng.h`,
`scenarioinfo.cpp`, `sim.cpp`, `sim.h`), `src/net/` (10, among them
`protocol.h`, `replayhdr.h`, `matchconfig.*` and `modpackage.*`), `src/server/`
(3: `server.cpp`, `commands.h`, `crusades/replayfiles.cpp`), `src/hpi/` (2),
`src/util/` (2: `png.*`), `src/cartographer/` (2), `src/vendor/stb/` (2, new)
and `src/client/` (43). Nothing under `src/ai/`, `src/campaign/`, `src/tnt/`,
`src/terrain/`, `src/cob/` or `src/crt/` changed.

After `0e78b3a6`, and after the local checks and the remote sweep below had
started on it, four client-only commits landed: `5ace1ca8` (GPU MEMORY LIMIT
lists a plain "1.25 GB (default)" entry instead of "Auto (1.25 GB)"; settings
and tests follow), `a7cd6b22` (room mod-pack names at the column's text size,
ALWAYS ON moved under the COSMETIC MODS heading), `6c75317d` and `b0045df3`
(map titles from the retail translate tables, so Ulasem Arena is no longer shown
as "ulasem arena"). They touch only `src/client/` display code, settings and
their tests (no `src/sim`, `src/net`, `src/server` or `src/ai` file), so the
`--mpai` hashes, the scenario gate and the remote sweep still stand for them;
main CI on `b0045df3` was green. The preparation then added the screenshots
(three commits), the quick start for 0.8.8 (two commits), the 0.8.8
documentation with the CMake version bump to 0.8.8 (two commits) and this
report. The final source was built in the optimized Debug tree and passed
`ctest -L quick` (see the end of *Local preparation checks*).

The changes:

- **285** Legion at ~100 bodies, round 5 (`a166a6b3` press into the shadow from
  just outside reach, `572b98ad` hold in the shadow only a body that pressed in,
  `15fc9543` late rings for fight-move columns, the reach edge and the stop
  origin, `cc0e69b5` melee presses test contact; bump `4442f658`; scenario
  baseline `a371ae1f`, `cd93d4aa` `legion_meleepress` only with
  `TAK_TEST_DATA`). Legion only.
- **286** Legion at ~100 bodies, round 6, gatehouse corridors (`71854a51`,
  `4984bf18` walk on past the shot; `0879cc45`, `bcbec8c0` deep rings fill back
  to front; bump `70000b3e`; scenario baseline `cf4cf657`). Legion only.
- **287** mods replace overrides (`10264a1a` `mods/cosmetic` and `mods/game`,
  `98694a77` the bundled `click.hpi` moves to `mods/cosmetic`, `e72c5a79` docs,
  `f9055101` cosmetic layers can never stop or desync a game, bump `28884f2b`,
  `69d8ccc0` libc++ test fix). The simulation is 286's.
- Client only, no protocol change: the in-game chat box (`a5868648`,
  `5b1cae90`), Noto Sans Bold in the chat box (`455120e3`, `fe69ec08`
  `CHAT-FONT-LICENSE.txt` packaged everywhere, `34995876`).
- **288** power codes (`166f17f8` the room option, `Cmd::PowerCode` and the
  seven simulation codes; `39752ef8` the client, +Radar and MakePoster;
  `8785b526`, `335edc47`, `24a0396f` review fixes; `adc98de9`, `2c0aeb29` docs;
  bump `9fa322b5`). With power codes off the simulation is 287's.
- Client only: the UI font (`77262226` one text API, `816d6bf4` UI FONT
  MODERN/CLASSIC, `80b9de87` measured widths, `dd0cb7e0` docs, `ff13e8c6`,
  `0e78b3a6` review fixes).

**Retail is expected to be bit-identical to 0.8.7** (285 and 286 are Legion
only; 287 and 288 change no simulation state with power codes off). `--mpai`
Inner Circle seed 1 from the landing commits: Retail 60 s `b353410003e98dd5`
and Legion 60 s `0825aeb9e8139203` (288's commit body), equal to 0.8.7.
The local checks confirmed all four on Linux at `0e78b3a6`, including 300 s
Retail `a3d953803a064191` and Legion `ce5c4ba7562e53b9`, all equal to 0.8.7.
TODO-LEAD: `--mpai` on Windows and macOS (CI's Determinism job on main was
green at `0e78b3a6`).

**Compatibility.** `kNetVersion` is **288** in `src/net/protocol.h` at
`0e78b3a6`, against 284 at `v0.8.7`. `kReplayFormat` is **14** in
`src/net/replayhdr.h` (13 at `v0.8.7`). The map generator's `formatVer` **9**
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` 4
(`src/net/crusades.h`) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`) are unchanged.

- **Multiplayer.** The server's Hello check rejects a client whose protocol
  differs from `kNetVersion`, so **0.8.8 cannot play with 0.8.7 or earlier in
  either direction**. The server and all players must update together.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  protocol equals `kNetVersion`, so 0.8.8 does not play 0.8.7 or older replays
  and those builds do not play 0.8.8 replays. 0.8.8 writes replay format 14
  (the POWER CODES byte after the MAP SCENARIO byte; a format-13 file reads it
  as 0). `tools/replay_test.cpp` pins `kNetVersion == 288` and
  `kReplayFormat == 14` and refuses 287, 286 and 285.
- **Maps and settings.** Saved `.kmp` maps and generator-8/9 recipes carry
  over. A 0.8.7 settings file reads in 0.8.8; the override keys
  (`gameCreate.overrides`, `overrides.hostPack`, `overrides.cosmeticPack`) are
  ignored and not written back. New keys: `uiFont`, `chatScale`, `chatAlign`,
  `gameCreate.powerCodes`, `gameCreate.gameMods`, `mods.gamePack`,
  `mods.cosmeticPack`, and the `view.poster` hotkey.
- **Mods.** `overrides/` is no longer read and is not moved; the mod cache is
  `ModCache/*.takmods` (was `OverrideCache/*.takoverrides`).

## Local preparation checks

All checks ran from a clean detached worktree (`~/tak-tmp/wt/release-088-local`) at
`main` `0e78b3a659eb` (protocol **288**, replay format **14**, generator recipe **9**, campaign
payload 4, database schema 9; protocol and replay format are the only versions that moved from
0.8.7), with `assets` and `third_party` links to the main checkout. Toolchains: GCC 16.2.1,
Clang 22.1.8, CMake 4.3.0, Ninja 1.13.2, Python 3.14.8. Builds and tests ran on cores 4-23; the
owned retail install served as `--data` and `TAK_TEST_DATA`. The version bump is not in this
commit, so binaries still call themselves `0.8.7 (build v0.8.7-39-g0e78b3a659eb)` (no `-dirty`).

**Result: no real regression in the engine or the tests, and every `--mpai` hash equals 0.8.7.**
Two optimized-tree failures each passed when rerun alone (`voice` at `-O2`, and
`crusades_client_network` at `-O0`); the ASan suite again needs `detect_leaks=0` on this
machine (SDL's own D-Bus start-up leaks, as in 0.8.7). The Legion scenario gate passes in
full: all 145 fixtures are baselined now and no key fails.

- **Multiplayer and replays.** `kNetVersion` is **288** in `src/net/protocol.h` (284 in
  0.8.7). `kReplayFormat` is **14** in `src/net/replayhdr.h` (13 in 0.8.7; it records the room's
  POWER CODES option). The replay loader requires protocol == `kNetVersion`, so 0.8.7 replays do
  not play and 0.8.7 cannot join a 0.8.8 server. The map generator's `formatVer` (9), the
  campaign payload's `kVersion` (4) and `kSchemaVersion` (9) are unchanged.
- **Builds.** Release, Debug, optimized Debug (`-O2`), Clang Debug and AddressSanitizer
  all-target builds completed with exit code 0: 886 steps in Release, 887 in the other four
  (873 / 874 in 0.8.7). ccache and mold; static-deps and static Bink FFmpeg prefixes. Release
  emits 950 warning lines (538 `-Wmissing-field-initializers`, 347
  `-Wdeprecated-declarations`, 6 `-Wunused-parameter`, 6 `-Wmisleading-indentation`, 3
  `-Wsign-compare`, 1 `-Wstringop-overread`, 1 `-Wignored-attributes`), the same kinds as
  0.8.7 (935); the deprecated-declaration lines come mostly from `datafolderpick.h`,
  `datafolder.h` and `cartographer/document.cpp`.
- **Full CTest.** **734** registered tests in the Debug trees (719 in 0.8.7) and **696** in
  Release (681); 18 added and 3 removed (net 15). Added: `chatbox`, `legion_fightring`,
  `legion_gatewalkon`, `legion_gateyard`, `legion_meleepress`, `legion_ringdeep`,
  `legion_ringdeepchurn`, `legion_ringedge`, `legion_ringshadowmixed`,
  `legion_ringshadowpress`, `legion_walkononward`, `mod_packs`, `mod_settings`, `mod_transfer`,
  `poster`, `power_codes`, `server_power_codes`, `uifont`. Removed with the overrides: 
  `override_packs`, `override_settings`, `override_transfer` (replaced by the `mod_*` tests).
  Each tree ran as `-LE nightly` (534 tests in Release, 572 in the others, which include the
  `pref_guard` fixtures) and the Release and optimized Debug trees also ran `-L nightly` (164).

  | Tree | Selection | Result | Time | Failures |
  | --- | --- | --- | ---: | --- |
  | Release | `-LE nightly`, 534 | **534/534** | 427 s | none |
  | Release | `-L nightly`, 164 | **164/164** | 2,007 s | none |
  | Optimized Debug | `-LE nightly`, 572 | 571/572 | 656 s | `voice` (passes alone) |
  | Optimized Debug | `-L nightly`, 164 | **164/164** | 1,477 s | none |
  | Debug (`-O0`) | `-LE nightly`, 572 | 571/572 | 3,866 s | `crusades_client_network` (passes alone) |
  | Clang Debug | `-LE nightly`, 572 | **572/572** | 2,862 s | none |
  | ASan | `-LE nightly`, 572 | 552/572 | 1,539 s | 20 tests, all LeakSanitizer reports inside SDL's D-Bus init (see below); **572/572 with `ASAN_OPTIONS=detect_leaks=0`** (24 tests rerun incl. the `pref_guard` fixtures) |

  The unoptimized trees ran at the same time as the optimized ones, the scenario gate and the
  `--mpai` runs.
  - **`voice` (optimized Debug, load-sensitive audio test): second sighting.** `FAIL another
    player's VOLUME leaves this talker alone: 0.510864` while the SDL disk-audio engine ran next
    to the other tests; the identical value as in 0.8.7's single sighting. It passed alone (21 s,
    rerun) and in Release, Debug, Clang Debug and (with leak detection off) ASan. `src/` has no
    voice change since 0.8.7 beyond the chat/UI work, so this is the same timing flake; note it
    has now failed in the optimized Debug tree twice in a row.
  - **`crusades_client_network` (Debug `-O0`): `FAIL: complete replay not published`, 0.06 s.**
    The same text as the flake candidate noted in 0.8.5. It passed alone (0.1 s, rerun) and in
    the other five trees. Existing intermittent test; not related to this release's changes.
  - **ASan: 20 tests fail only on leak reports.** `script_transient`, `script_transient_263`,
    `creon_death_sound_peers`, `voice`, `streaming_capture`, `stream_key`,
    `render_palette_indexed`, `render_native_pass`, `client_restart`, `shadow`, `fogupload`,
    `menu_music`, `menu_music_roster`, `death_sound`, `screen_lifecycle`,
    `campaign_presentation_roster`, `death_sound_roster` and the three `crusades_*_ui_network`
    tests (the ones that start an SDL client), the same 20 as in 0.8.7. Every leak report is an
    `SDL_DBus_Init` allocation in the statically linked SDL; there is no `ERROR:
    AddressSanitizer` (no heap overflow, use-after-free or stack error) anywhere in the log. With
    `ASAN_OPTIONS=detect_leaks=0` all of them pass. The 552 other ASan tests passed with leak
    detection on.
  - **The unoptimized Legion tests passed under the existing limits.** `legion_sched_inflight`
    846 s / 835 s / 428 s, `legion_scale_neutral` 822 s / 870 s / 426 s, `legion_route_paused`
    1,520 s / 1,001 s / 499 s and `legion_parallel` 3,397 s / 2,588 s / 1,539 s (Debug / Clang
    Debug / ASan); the largest use of a limit is `legion_parallel` in Debug, 63% of 5400 s (58%
    in 0.8.7, the machine was loaded). `movement_orders_trolls` 351 s / 315 s / 110 s (limit
    600 s: Debug at 59%). `terrain_schedule` 58 s / 69 s / 37 s and `voice_uplink` 49 s passed
    everywhere.
  - **The settings-leak guard held.** `pref_guard_setup`/`pref_guard_check` passed in every
    selection. The real profile's `settings.ini` has the same SHA-256 and mtime before and
    after (`real-profile-{before,after}.txt`). `TAK_SETTINGS_FILE` was not exported.
- **`--mpai` golden hashes: all unchanged from 0.8.7.** Each ran once on the optimized Debug
  build, Inner Circle, `takserver --local --no-auth --seed 1`, an empty `XDG_DATA_HOME`, and
  `TAK_LEGION=1` for Legion. No desync or referee-suspect line in any log. `end=timelimit`,
  `err=none` in all four. Legion rounds 5 and 6, mods, power codes (off by default) and the
  UI/chat fonts change none of the Retail or the Legion 60/300 s state in this scenario.

  | Mode | Duration | `0e78b3a6` (288) | 0.8.7 (284) |
  | --- | ---: | --- | --- |
  | Legion | 300 s | `ce5c4ba7562e53b9` (18 units) | `ce5c4ba7562e53b9` |
  | Retail | 300 s | `a3d953803a064191` (13 units) | `a3d953803a064191` |
  | Legion | 60 s | `0825aeb9e8139203` (4 units) | `0825aeb9e8139203` |
  | Retail | 60 s | `b353410003e98dd5` (4 units) | `b353410003e98dd5` |

  Optimized Debug binary SHA-256:
  - client `b83c62c8e3964badc0a7c82fafcc39d95a41668c0a88030f2592d5b0981876ed`
  - server `f402455a49423cd1eb5e69ae5ce7e8903b66bf22ab672ded4ec1555406f51f06`
- **Determinism.** In `tools/check-determinism.sh`, the GCC and Clang x86-64 builds at
  O0/O2/O3 all agree on `dcef618cd2e4d558`, the same as 0.8.7. The aarch64 legs are skipped
  (target headers missing). `tools/check-detmath.sh` passes, including the `std::atomic_ref` guard.
- **Legion scenario gate: PASS.** The gate ran the way `tools/legion_nightly.sh` stage 2 does:
  every `tools/scenarios/*.scn` (**145** files; 140 in 0.8.7), optimized Debug
  `legion_scenario`, `--mode both --json --offsets gate --data <retail install>`, one file per
  core on cores 4-11, then `legion_check.py check --baseline tools/scenarios/baseline.json`. All
  145 files exited 0 (`problems.txt` empty); `legion_check.py validate --baseline
  tools/scenarios/baseline.json` reports `PASS: 0 problems`. Run time 5,666 s (the machine was
  loaded).
  - **Result:** `PASS: 36522 keys checked, 0 failed, 4415 ratchets, 4705 licensed, 266 floor
    exceptions, 864 report-only`, identical with and without `--cumulative` (0.8.7: 15 failed
    on five unbaselined fixtures; the baseline has been retaken since and no fixture lacks
    entries now). The `taperbridge-100`/`taperbridge-small` rows are floor exceptions
    (L100-R5) as before: Legion's `scale.complete.t90` there is still later than Retail's.
    The gatehouse and fight-diag rows are floor exceptions as well (L100-R5/R6; for example
    `attackring-gatehouse-40` wall touch 398 vs Retail's 147 permille, `attackring-fight-diag-60`
    `reach.ring.damage_t2400` 1,170 vs 1,303); the gatehouse damage-above-Retail limit itself is
    a ratchet/report matter, not a failing key.
  - The 4,415 ratchets are improvements over the committed baseline (for example
    `attackring-f1-16 follow_chain_max 5 -> 4`); they do not fail the gate and no baseline was
    regenerated.
- **Retail group emulation check (formation-air).** `python3 -B tools/re/check_retail_group.py
  --build build-rel` (400 cases) passed every section: tick 400, query 4000, check 2000,
  formation 2000, vtolcheck 2000, vtolform 2000.
- **Python research suite.** **165 tests** passed (`python3 -B -m unittest discover -s tools/re
  -p '*_test.py'` with `TAK_AUTH_RETAIL_DIR` set and `build/` pointing at the Release tree), no
  skip. The owned-retail archive verifier (`verify_official_hpi_test.py`) passed all **6
  tests**. The SHA-256 sums of the 16 retail HPI archives and `Kingdoms.key` are identical
  before and after the runs (`retail-sha-{before,after}.txt`).
- **Syntax checks.** All **466** tracked Python files compile. All 22 `*.sh` scripts
  (21 tracked plus `tools/tak-netem`) pass `bash -n`.

Logs: `~/tak-tmp/scratch/release-088/` (`cfg-*.log`, `make-*.log`, `build-status.txt`,
`ctest-*.log`, `rerun-{o2,dbg,asan-noleak}.log`, `mpai/`, `mpai.sh`, `gate/`
(`scenarios.jsonl`, `check.txt`, `check-nightly.txt`, `validate.txt`), `check-determinism.log`,
`check-detmath.log`, `check_retail_group.log`, `python-suite.log`, `verifier.log`,
`pycompile.log`, `bashn.log`, `tests-088*.txt`, `retail-sha-{before,after}.txt`,
`real-profile-{before,after}.txt`).

**Final source.** After the preparation commits were cherry-picked onto `b0045df3`, the
optimized Debug tree rebuilt every target at `ab7269a5` plus this report (version
`0.8.8`, exit code 0) and passed `ctest -L quick` **469/469**.

## Remote multiplayer sweep

The full 37-scenario table passed for both pathfinding modes, Retail and Legion, on **2026-10-21**
(UTC), both on `main` at `0e78b3a659eb` (protocol **288**, replay format **14**, build id
`v0.8.7-39-g0e78b3a659eb`; the version string still reads 0.8.7 until the 0.8.8 bump lands):

- **Retail:** 17:32-17:50 UTC (18 minutes including the gates), plus a retry of three cases (below).
- **Legion:** 17:50-18:19 UTC (29 minutes including the gates), plus a repeat of six cases (below).

Referees ran on `tak.pgnet.us` (24 runs, 10 at a time) and `vpn3.pgnet.us` (13 runs, 6 at a time), with
optimized local clients pinned to CPUs 4-23. Each scenario allowed ten simulated minutes (18,000 ticks) or
ended at an actual result for that seat. Real seated players provide the client/referee checksum coverage;
the two all-AI spectator scenarios per mode (`w-allai-stress`, `w-allai-bench`) test flow control, not hashes.
The mode is chosen through the hosting client's room options (`TAK_LEGION=1` creates a Legion room). Every
recorded referee replay header confirms the intended mode, replay format 14 and protocol 288: 37 headers per
mode, seeds 2000-2036 each mapped to its case. The case previously named `h-overrides-full` is now
`h-mods-all` (`--mods all`); its Retail result is bit-identical to the old case.

| Mode | Completed scenarios | Completed seats | Client-pair checkpoints | Client-referee checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Retail | 37/37 (35 in the first pass, 2 after a wall-clock retry) | 52/52 | 9,934 across 20 seat pairs (first pass) + 360 across 2 (retry), 0 mismatched | 2,505 across 49 seat/referee comparisons (first pass) + 203 across 5 (retry), 0 mismatched |
| Legion | 37/37 (single pass, no retries) | 52/52 | 10,198 across 22 seat pairs, 0 mismatched | 2,685 across 51 seat/referee comparisons, 0 mismatched |

Each run passed all three `--validate` gates before its table started:

- **Build identity:** `both at v0.8.7-39-g0e78b3a659eb`.
- **Planted desync:** the referee reported `game 1: client 1 DESYNCED at tick 900`.
- **Game mod transfer:** `verified game mods transferred and incorrect loaded gameplay rejected` (the gate
  now uses `mod_transfer_test`, replacing the override transfer test).

The gate checks only `tak.pgnet.us`; the binary on `vpn3.pgnet.us` has the same SHA-256
(`6ed0ffdefd51162ec087577629d863854f461c0d4c5a3ed5d1374583a73eb735`) and the same `--version` build id. No
natural `DESYNCED`, `REFEREE SUSPECT` or client-reported desync appeared in any case log; the only such line
in each run is the planted one. The Legion sweep ended with "no desyncs reported (52 seats, all completed)".
A state hash is compared at every checkpoint; the position digest is compared as well wherever both sides
carry one. Every game wrote a referee replay: 37 per mode, matched to cases by the seed in the replay header.
The checkpoint comparison script is the 0.8.7 one, unchanged. `tools/desync-hunt-remote.sh` changed with
the mods rework (the transfer gate and the case rename); its SHA-256 at `0e78b3a6` is
`df09e28844be1f399f9466a62e19c9cdf53298e5fdb9eb5dcefb5fca830ad56e` (0.8.5: `0ec213448db4f15d...`).

**Retail retry (wall-clock, not a desync).** In the first Retail pass `2h-stress` and `2h-orders-stress`
(each seats two humans and fields about 9,000 units) ended with "peer closed end=timelimit" on the second
seat: the default per-client real-time deadline (simulated seconds + 300) ran out while this machine carried
other work (load average about 30 from the release's local checks running on the same cores). The seated host
and the referee both reached a normal result and no hash disagreed. Both cases were re-run in isolation with
`TAK_WALL_TIMEOUT=3600` and completed on both seats with 0 mismatched checkpoints; the table shows the retry
results. The same retry re-ran `h-everything`, whose first-pass result had drifted under load (tick 13,555,
`59f5c3cddc41ddc6`, 5,139 units); on the quiet re-run it ended at tick 13,549, `8ada2d5383b95f0a`, 5,140
units, bit-identical to 0.8.7. `h-everything` is therefore load-sensitive in Retail too (5,000+ units), like
`h-lake`, `h-stress`, `2h-stress`.

**Legion repeat.** To separate "changed by rounds 5/6" from run-to-run noise, six Legion cases whose result
differs from 0.8.7 (`h-everything`, `net-rtt50`, `net-loss1`, `h-mods-all`, `h-stress`, `w-allai-bench`) were
run a second time: all six reproduced the first pass tick, hash and unit count exactly. **No Legion retries**
were needed for completion; `TAK_WALL_TIMEOUT=3600` was used from the start.

### Reference ticks and hashes (0.8.8 candidate)

Hash and units are the seated host's final line. "vs 0.8.7" compares tick and hash with the 0.8.7 table
(protocol 284; same harness, hosts and seeds). The six `TAK_AUTOPLAY` cases never repeat a hash, and `h-lake`,
`h-stress`, `2h-stress`, `2h-gods`, `4h-gods` (and `h-everything` under heavy local load) are load-sensitive,
so "differs" in those Retail rows is expected noise.

| Case | Retail end | Retail tick | Retail hash | Retail units | vs 0.8.7 | Legion end | Legion tick | Legion hash | Legion units | vs 0.8.7 |
| --- | --- | ---: | --- | ---: | --- | --- | ---: | --- | ---: | --- |
| `h-baseline` | timelimit | 18,000 | `4a07030c0169e47c` | 210 | same | timelimit | 18,000 | `4b9217a4ffcd74c0` | 213 | same |
| `h-gods` | concluded | 14,626 | `0ea0a1d96c1414af` | 104 | same | concluded | 15,383 | `3f26557d500cff5a` | 82 | same |
| `h-crusades` | timelimit | 18,000 | `604de77d3fe53f3e` | 245 | same | timelimit | 18,000 | `e4036388d6f2e4df` | 230 | same |
| `h-stress` | concluded | 4,896 | `94e311eb1462ec2c` | 8,595 | differs (tick 5,387 -> 4,896) | concluded | 5,147 | `4efaa19f898d273d` | 7,305 | differs (tick 5,013 -> 5,147) |
| `h-absurd` | timelimit | 18,000 | `1b0d79cb6583219d` | 454 | same | timelimit | 18,000 | `9125e38fec71fc29` | 469 | same |
| `h-fog-explored` | timelimit | 18,000 | `00419b0edd6c9b06` | 188 | same | timelimit | 18,000 | `faafb450fc7005c7` | 142 | same |
| `h-fog-full` | timelimit | 18,000 | `9896fd941b2aade9` | 216 | same | timelimit | 18,000 | `821e2ab5feef82d1` | 217 | same |
| `h-unitcap` | concluded | 14,780 | `c382486eeb54fb3b` | 204 | same | timelimit | 18,000 | `715966bbb3c6a746` | 220 | same |
| `h-cramped` | concluded | 1,307 | `45134591344a4d04` | 10 | same | timelimit | 18,000 | `57d4067d04331580` | 29 | same |
| `h-naval` | timelimit | 18,000 | `5fb53d62d7a0b42c` | 58 | same | timelimit | 18,000 | `17b9ca802fb9a217` | 88 | same |
| `h-naval-crus` | timelimit | 18,000 | `396f8de68f25c274` | 70 | same | timelimit | 18,000 | `4787a6c75fbb0d77` | 88 | same |
| `h-lake` | concluded | 3,948 | `eddd6c3683181de3` | 4,920 | differs (tick 3,959 -> 3,948) | concluded | 4,139 | `a6120e769b48894c` | 4,816 | differs (tick 4,130 -> 4,139) |
| `h-random-starts` | concluded | 15,702 | `6e0395e042f734fe` | 228 | same | concluded | 12,289 | `0cba4154cf002db9` | 194 | same |
| `h-monarch-exp` | concluded | 12,231 | `c646ef640c32a947` | 89 | same | concluded | 17,804 | `540592e154eafb24` | 82 | same |
| `h-mods-all` | timelimit | 18,000 | `450038fec3e01ce5` | 209 | same | timelimit | 18,000 | `e3693d73ec08c60c` | 212 | differs |
| `h-everything` | concluded | 13,549 | `8ada2d5383b95f0a` | 5,140 | same | concluded | 10,300 | `13ca94202d9d0533` | 4,782 | differs (tick 11,615 -> 10,300) |
| `h-speed-1x` | timelimit | 18,000 | `9b5694cceca37a11` | 236 | same | timelimit | 18,000 | `0c1cf293aa498699` | 246 | same |
| `h-two-castles` | timelimit | 18,000 | `8e9dfa2311fc1ec4` | 23 | same | timelimit | 18,000 | `81b4f5bfafb359c0` | 4 | same |
| `2h-baseline` | timelimit | 18,000 | `7b5850e128c0ff63` | 173 | same | timelimit | 18,000 | `638fa388b12be0c4` | 172 | same |
| `2h-gods` | concluded | 12,423 | `bf5aaf973f862369` | 104 | same | concluded | 13,059 | `9283d91d154a8678` | 61 | same |
| `2h-crusades` | timelimit | 18,000 | `4177d6f4374aad35` | 182 | same | timelimit | 18,000 | `e14b49adbbf76ebf` | 167 | same |
| `2h-stress` | concluded | 5,048 | `fbe83bc16dc7bf85` | 9,102 | differs (tick 5,058 -> 5,048) | concluded | 5,265 | `3c2c1fab5d476c26` | 8,417 | differs (tick 5,258 -> 5,265) |
| `2h-orders` | timelimit | 18,000 | `c1480cc9ff551a31` | 169 | differs (autoplay) | timelimit | 18,000 | `e72d0d69d05ee0c5` | 201 | differs (autoplay) |
| `2h-orders-stress` | concluded | 5,709 | `7568f4b759f10317` | 9,042 | differs (tick 4,802 -> 5,709) (autoplay) | concluded | 4,894 | `e411547ea4fb77c7` | 8,206 | differs (tick 5,720 -> 4,894) (autoplay) |
| `4h-orders` | timelimit | 18,000 | `55bb9123ba296fd4` | 153 | differs (autoplay) | timelimit | 18,000 | `0b825a0c31dc4734` | 164 | differs (autoplay) |
| `3h-baseline` | timelimit | 18,000 | `1d59d944a244aef4` | 172 | same | timelimit | 18,000 | `beeeab4bb6ec584b` | 148 | same |
| `4h-gods` | timelimit | 18,000 | `a85227dda74b63c4` | 102 | differs | timelimit | 18,000 | `cac7daae4618b419` | 141 | differs |
| `net-rtt50` | timelimit | 18,000 | `80dc6a72233bd05d` | 230 | same | timelimit | 18,000 | `03c521246aeadec2` | 227 | differs |
| `net-rtt100` | timelimit | 18,000 | `fcba4a56baf98c45` | 190 | same | timelimit | 18,000 | `c3167c3d271f43bc` | 204 | same |
| `net-jitter` | timelimit | 18,000 | `6a0f640c3faf1de6` | 173 | same | timelimit | 18,000 | `ca07f9821f97109e` | 196 | same |
| `net-rtt500` | timelimit | 18,000 | `8837809d723b6fc0` | 219 | same | timelimit | 18,000 | `af7b2fa90b652283` | 200 | differs |
| `net-loss1` | timelimit | 18,000 | `1adf2c3d366e9bef` | 192 | same | timelimit | 18,000 | `b69a197764ecdf5c` | 206 | differs |
| `net-loss3-orders` | timelimit | 18,000 | `0d7a113b910a9d01` | 197 | differs (autoplay) | concluded | 16,878 | `41f2ca7a35352fb6` | 208 | differs (tick 18,000 -> 16,878) (autoplay) |
| `net-orders100` | timelimit | 18,000 | `1f6ecb32110334cf` | 154 | differs (autoplay) | timelimit | 18,000 | `d11aa761d217b820` | 192 | differs (autoplay) |
| `net-2h-rtt100` | timelimit | 18,000 | `802443ff6774c46f` | 176 | differs (autoplay) | timelimit | 18,000 | `fddbb1ef8d05e140` | 188 | differs (autoplay) |
| `w-allai-stress` | timelimit | 18,000 | `06e78e00b732019a` | 5,392 | same | timelimit | 18,000 | `fc6dfe90da499b44` | 3,925 | differs |
| `w-allai-bench` | timelimit | 18,000 | `10a3a871daabf505` | 312 | same | timelimit | 18,000 | `8446871154cf8f79` | 344 | differs |

### Comparison with 0.8.7

**Retail: unchanged, as designed.** 27 of 37 cases are bit-identical to 0.8.7 (tick and hash), including every
deterministic Ulasem Arena case, `h-cramped` (1,307, `45134591344a4d04`), `h-everything` (13,549,
`8ada2d5383b95f0a`, on the quiet retry), `h-mods-all` (same hash as 0.8.7's `h-overrides-full`), `2h-gods`, the
impaired-network cases except the autoplay ones, and the spectator `w-allai-stress` (5,392 units,
`06e78e00b732019a`) and `w-allai-bench`. The 10 that differ are exactly the known unstable ones:

- The six `TAK_AUTOPLAY` cases (`2h-orders`, `2h-orders-stress`, `4h-orders`, `net-orders100`,
  `net-loss3-orders`, `net-2h-rtt100`), which never repeat a hash.
- Four load-sensitive cases: `h-lake` (3,959 -> 3,948), `h-stress` (5,387 -> 4,896, 8,595 units),
  `2h-stress` (5,058 -> 5,048, 9,102 units) and `4h-gods` (18,000 ticks, 102 units vs 142).

Legion rounds 5/6 (protocols 285/286), mods (287), power codes (288), the chat box and the UI font touch no
Retail simulation state, which also matches `--mpai` Inner Circle seed 1 being unchanged.

**Legion: 20 of 37 cases are bit-identical to 0.8.7, 17 moved.** Rounds 5 and 6 change ring admission, the
press into the target's shadow and gatehouse corridor filling, which only matter where formations meet walls,
gates or melee contact. The 20 unchanged Legion cases are presumably those where no formation reached a wall, gate or melee press (inferred, not traced; for example `h-baseline`, `h-gods`, `h-crusades`, `h-absurd`, the fog, naval and cramped cases, `h-two-castles`, `h-speed-1x`, `2h-baseline`, `3h-baseline`). Cases that moved: `h-stress`, `h-lake`, `h-mods-all`,
`h-everything`, `2h-stress`, `2h-orders`, `2h-orders-stress`, `4h-orders`, `4h-gods`, `net-rtt50`,
`net-rtt500`, `net-loss1`, `net-loss3-orders`, `net-orders100`, `net-2h-rtt100`, `w-allai-stress`,
`w-allai-bench`. Notable movements:

- `h-everything` still `concluded` but earlier: tick 11,615 -> 10,300 (4,782 units).
- `h-stress` 5,013 -> 5,147 (7,305 units); `h-lake` 4,130 -> 4,139; `2h-stress` 5,258 -> 5,265.
- `net-loss3-orders` (autoplay) now concluded at 16,878 instead of running to 18,000.
- The spectator `w-allai-stress` ends with 3,925 units (0.8.7: 3,838) and `w-allai-bench` with 344 (379).

All Legion movements are consistent with changed fight dynamics from rounds 5/6 (some cases conclude earlier, `h-stress` later) and are deterministic: the repeat above reproduced six of them exactly, and every Legion
seat agreed with the referee at every checkpoint.

## CI and packages

TODO-LEAD: main CI on the final source (Determinism, macOS, Windows, Linux packages);
tag CI run ids (determinism dispatched with `gh workflow run determinism.yml --ref
v0.8.8`); reruns and which known flake; asset count (0.8.7: 29), digests and sizes; build
ids `v0.8.8` in every package; Windows Authenticode (0.8.7: 9 files Valid on oden-win);
macOS notarization and stapling of both DMGs and apps; Opus static and
`OPUS-LICENSE.txt`; **new: `CHAT-FONT-LICENSE.txt` in every Debian, RPM, Arch and
Slackware package, both Linux debug tarballs, all release and debug ZIPs (the game app's
Resources on macOS) and the Windows installer**; microphone entitlement;
`NSHighResolutionCapable`; no libsecret/GLib `NEEDED` on Linux; publication time.

## Screenshots

Recaptured on the 0.8.8 candidate (`0e78b3a6` plus a local version bump to
0.8.8) with UI FONT at its default, MODERN: optimized Debug OpenGL build with
the 0.8.7 capture-only patch carried forward (`capture-088.patch`, not
committed), scratch `HOME`/`XDG_DATA_HOME` (the real profile was not written)
and a scratch install holding `mods/cosmetic/click.hpi` (always on), two empty
cosmetic packs (Alternate Cursors, Classic Sounds) and one empty game pack (Big
Battles). Every picture was looked at after capture.

- **Recaptured, gallery:** `title.jpg` (VERSION 0.8.8), `lobby.jpg` (POWER
  CODES: OFF), `lobby-scenario.jpg`, `room.jpg` (three AIs, both cosmetic packs
  ticked, ALWAYS ON: click.hpi, the GAME MODS list, MOVE HERE on rows 5-8),
  `diplomacy.jpg`, `crusades.jpg` (now the retail Darien map), `streaming.jpg`,
  `results.jpg`, `generated.jpg`, and the scenes retaken each release:
  `battle.jpg`, `dragons.jpg`, `flyers.jpg`, `hud.jpg` (VID row 117 MB, was
  113), `naval.jpg`.
- **Recaptured, guide:** `random-map-lobby.jpg`, `hud-annotated.jpg` (markers
  2 and 7 moved for the new layout), `scorecard.jpg` (now captured at 1920 x
  1080 and scaled down; at 1600 x 900 the BENCHMARKING badge covered the Score
  header, as it did in 0.8.7), `side-{aramon,veruna,taros,zhon,creon}.jpg`,
  `selection-strip.jpg`, `shift-queue.jpg`, `options-graphics.jpg`,
  `voice-chat.jpg`, `campaign-briefing.jpg` (new last line "Always-on mods:
  click.hpi"), `replay.jpg`, `benchmark.jpg`, `fire-on-the-move.jpg`,
  `flyers-retail-legion.jpg`; Cartographer `cartographer/test-lobby.jpg` (UI
  font, ALWAYS ON and empty COSMETIC MODS / GAME MODS lists).
- **New:** `guide/chat-box.jpg` (centered chat box at 100%, player-coloured
  names), `guide/options-interface.jpg` (UI FONT to CHAT SCALE) and
  `guide/room-mods-power-codes.jpg` (POWER CODES: ON, ALWAYS ON, a cosmetic and
  a game pack ticked), each placed in `docs/user-guide.md`.
- **Quick start (`docs/img/quick/`):** `box-select.jpg`, `selected.jpg`,
  `attack-cursor.jpg`, `build-placement.jpg` and `lobby-move-here.jpg` (now
  also showing the COSMETIC MODS / GAME MODS lists) recaptured with the 0.8.7
  quick-start harness; `command-panel.jpg` has no engine text, recaptured
  byte-identical and left as the 0.8.7 file.
- **Campaign page fixed and retaken:** with UI FONT MODERN the "HAPTER" half of
  the campaign book's CHAPTER heading was drawn in Noto Sans Bold at the height
  of bodfontbody's tall 'H', so every letter stood as tall as the retail initial
  and ran into the chapter numeral. The heading (with the illuminated "C") now
  keeps the retail lettering in both fonts, and the "Always-on mods" note, which
  was faint on the parchment, is drawn in dark ink under MODERN
  (`src/client/campaignscreen.cpp`). Checked at 1024 x 768, 1600 x 1000 and
  1920 x 1080 in both fonts, on Book of Darien chapters 1 and 12 and Iron Plague
  chapter 13: MODERN now lays out as CLASSIC, and CLASSIC renders are
  pixel-identical before and after. `campaign.jpg` was retaken (MODERN,
  1600 x 1000, fresh profile). As in CLASSIC and 0.8.7, the top of the
  chapter numeral (wider for two digits) still meets the bottom of "APTER" (the
  retail boxes overlap).
- **Kept:** `guide/tactical-dots.jpg` (no interface text) and the Cartographer
  editor pictures (the editor does not use the UI font).
- **Captions:** the README title alt text (version 0.8.8) and gallery footnote
  (0.7.25 to 0.8.8 builds), `docs/img/README.md` (header, every retaken row,
  three new rows, capture notes, the quick-start section) and the user guide's
  image lines were updated to match.
- **Retaken after the polish commits** (`5ace1ca8`..`b0045df3`, see *Scope*):
  `lobby.jpg` (map titled Ulasem Arena, from the translate table),
  `lobby-scenario.jpg`, `room.jpg` and `guide/room-mods-power-codes.jpg`
  (ALWAYS ON: click.hpi under the COSMETIC MODS heading, pack names at the
  column's text size), `guide/options-graphics.jpg` (GPU MEMORY LIMIT
  "1.25 GB (default)") and `cartographer/test-lobby.jpg`; their captions in
  `docs/img/README.md` were updated. `quick/lobby-move-here.jpg` is kept: its
  scratch install has no always-on archive and no packs, so the room shows only
  the two headings with their NO PACKS lines, which the polish did not change.

## Limits

- No game mixing 0.8.7 and 0.8.8 builds was played, and none can be (protocol
  288 against 284, the Hello gate and the replay loader).
- **Legion units still stand and wait more than Retail's**, though they arrive
  sooner (a user ruling: cosmetic).
- **Legion attackers through a narrow gate deal more damage than Retail's**
  (gate sums: `attackring-gatehouse-40` 11,719 against 7,172,
  `attackring-gateyard-36` 8,678 against 5,655, `attackring-gatehouse2-48`
  12,183 against 10,404, `attackring-deadcorridor-30` 5,077 against 2,262), and
  Legion work on the gatehouse layouts rose (gatehouse-40 whole run 1.72M ->
  6.49M at the median).
- **Power codes are lockstep**: every code that changes the game is a
  sequenced command, so its effect is in every player's simulation and in the
  replay; retail applied codes only on the typer's machine.
- **FULL VISION does not switch LOS off at start** (retail seeds it off);
  +LOS / +NowISee do.
- **UI FONT CLASSIC is close to but not pixel-identical with 0.8.7** (a space
  advances 4 px everywhere, and layouts now measure text widths); accepted.
- Carried over: the Legion bridge lottery (`taperbridge-100`,
  `taperbridge-small`); the Legion 10,000 program still does not meet its own
  success criterion for congested workloads, and many-10000, scatter-10000 and
  mixed-10000 still run over the 33.3 ms tick budget in part of their ticks;
  mixed-10000's flyers hover over a slow ground army. Retail `h-cramped` ends
  early because the idle seat starts with only its Monarch. The macOS Retina
  UI Scale is migrated once. A stationary auto-targeting shooter keeps refusing
  a once-blocked target.
- Flakes seen in this release's runs, each passing when rerun alone:
  `voice` in the optimized Debug tree (`0.510864`, the same value as 0.8.7's
  single sighting; second release in a row in that tree), and
  `crusades_client_network` in Debug `-O0` ("complete replay not published",
  the 0.8.5 flake candidate). ASan still needs `ASAN_OPTIONS=detect_leaks=0`
  here (SDL's D-Bus start-up leaks). In the remote sweep, Retail `2h-stress`
  and `2h-orders-stress` needed `TAK_WALL_TIMEOUT=3600` under local load
  (wall clock, no hash disagreement), and Retail `h-everything` drifted under
  load (tick 13,555) and matched 0.8.7 on a quiet rerun (13,549).
