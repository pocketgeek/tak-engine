# 0.8.9 release validation

Protocol **288**, replay format **14**, generator version **9**, campaign
payload **4**, database schema **9**: all unchanged from 0.8.8.
<!-- LEAD: re-check kNetVersion and kReplayFormat right before tagging (288 / 14
     at 71757b75). If either moves, 0.8.9 is no longer compatible with 0.8.8:
     rewrite the compatibility statements in these notes, the release notes,
     README.md ("New in 0.8.9", protocol lines), docs/user-guide.md (Versions,
     Replays, troubleshooting), docs/public-server.md,
     docs/random-map-generation.md and docs/multiplayer-design.md. -->

Tagged `v0.8.9` at `e170e226` (annotated tag object `93bbdf28`) and published on 2026-10-22 06:18 UTC.

## Scope and compatibility

0.8.9 follows 0.8.8 (protocol 288). After the 0.8.8 validation commit
`62fd8588`, main reached `71757b75` in 10 commits, all linear (no merges).
`git diff --stat 62fd8588..71757b75 -- src` touches 12 files: `src/client/`
(11: `areadrag.h` and `buildline.h` (both new), `chatbox.h`, `gameview.h`,
`gameview_devharness.cpp`, `gameview_hud.cpp`, `gameview_impl.cpp`,
`gameview_input.cpp`, `gameview_lobby.cpp`, `gameview_net.cpp`,
`gameview_render.cpp`) and `src/net/client.h` (a header-only display helper,
`MpClient::roomModStatus`, that no simulation, wire or replay code calls).
Nothing under `src/sim/`, `src/server/`, `src/ai/`, `src/campaign/`,
`src/tnt/`, `src/terrain/`, `src/cob/` or `src/crt/` changed. The other
files are tests (`tools/area_drag_test.cpp`, `tools/buildline_test.cpp` (both
new), `tools/chatbox_test.cpp`, `tools/mod_packs_test.cpp`), their CMake
registration, and docs (`docs/retail-engine.md`, `docs/user-guide.md`,
`docs/quick-start.md`).
No commit landed on main after `71757b75` before the preparation commits. The
preparation commits (docs and images only, plus the CMake version bump to 0.8.9)
are the screenshots (`docs/img: 0.8.9 screenshots`, the user-guide build-line
picture, README and guide alt text), the quick start (`Quick start for 0.8.9`),
the docs (`0.8.9 docs: version 0.8.9, release notes, validation skeleton` and its
follow-up) and this report. None touches `src/`.

The changes, all client only:

- Create screen and room layout: `2f601915` (every create-screen option button
  300 px wide, every caption at one size in both UI fonts), `627698d2` (room
  rules: MAP SCENARIO is a grid line at the others' size, scenario details
  under both columns), `d6377bd9` (GAME MODS VERIFIED only when game packs are
  in play; `mod_packs_test` covers the rule).
- Chat box: `4ecae85c` (always above the build row and its TRAINING line, idle
  and typing), `24f7b991` (a build row that is not drawn never hides chat:
  room for two lines and the composer, `chatbox::minRoom`).
- Area commands: `4f5e9a9d` (reclaim and lodestone boxes need a deliberate
  32 x 32 UI-px box, UI-scale aware; drag boxes anchored to the map),
  `aa572bde` (clicks act at the press; the pointer must move 32 px on either
  axis; edge-scrolled boxes count), `c6d9168c` (Windows `near` macro rename).
  Retail finding (`0x5202c0`, `0x527bf0`) in `docs/retail-engine.md`.
- Shift-drag build lines: `8b1d5631` (tight default spacing, wheel gap 0..12
  with a GAP label and no camera zoom, gap kept for the game and reset each
  game, never saved; builders kept on one side with short Moves), `71757b75`
  (chained lines plan from the builder's queue; wheel notch handling).

Build lines are queued as the existing Build and Move commands, so **Retail and
Legion are expected to be bit-identical to 0.8.8.** `--mpai` Inner Circle seed 1
must equal 0.8.8: Retail 60 s `b353410003e98dd5`, 300 s `a3d953803a064191`;
Legion 60 s `0825aeb9e8139203`, 300 s `ce5c4ba7562e53b9`.
On Linux (optimized Debug, `71757b75`) all four equal 0.8.8 (see the local checks).
`--mpai` on Windows and macOS was run on optimized Debug builds (`-O2`) of the tag `e170e226`, built on oden-win (MSYS2 MINGW64, GCC) and on the arm64 MacBook (Apple Clang). The release binaries reject the harness flags, so Debug builds were needed. The settings were Inner Circle, `takserver --local --no-auth --seed 1`, a scratch profile, and `TAK_LEGION=1` for Legion. All eight games ended `err=none end=timelimit` and match Linux and 0.8.8: Retail 60 s `b353410003e98dd5` (4 units), 300 s `a3d953803a064191` (13 units); Legion 60 s `0825aeb9e8139203` (4 units), 300 s `ce5c4ba7562e53b9` (18 units), on both hosts. Neither host logged a desync or referee-suspect line. The tag's dispatched Determinism run (39726695130) runs no `--mpai` game. Its cross-build check agrees on golden `dcef618cd2e4d558`.

**Compatibility.** `kNetVersion` is **288** in `src/net/protocol.h` at
`71757b75`, as at `v0.8.8`. `kReplayFormat` is **14** in
`src/net/replayhdr.h`, as at `v0.8.8`. The map generator's `formatVer` **9**
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` 4
(`src/net/crusades.h`) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`) are unchanged.

- **Multiplayer.** The server's Hello check rejects only a client whose
  protocol differs from `kNetVersion` (or whose gameplay-data hash differs); the
  build id is logged, not enforced. So **0.8.9 and 0.8.8 play together in
  either direction**, and a server may host a mix of both. 0.8.7 or earlier
  still cannot join either.
- **Replays.** `supportedReplayProtocol` accepts a recording whose protocol
  equals `kNetVersion`, so 0.8.9 plays 0.8.8 replays and 0.8.8 plays 0.8.9
  replays (both write format 14). `tools/replay_test.cpp` still pins
  `kNetVersion == 288` and `kReplayFormat == 14`.
- **Maps, mods and settings.** Saved `.kmp` maps, generator-8/9 recipes,
  `mods/` packs and `ModCache/*.takmods` carry over. No settings key was added
  or removed (the build-line gap is per game and never saved).
- No mixed 0.8.8 / 0.8.9 game was played (as with 0.8.1): compatibility rests
  on the unchanged protocol, replay format and simulation sources.

## Local preparation checks

All checks ran from a clean detached worktree (`~/tak-tmp/wt/release-089-local`) at
`main` `71757b759693` (protocol **288**, replay format **14**, generator recipe **9**, campaign
payload 4, database schema 9; none moved since 0.8.8), with `assets` and `third_party` links to
the main checkout. Toolchains: GCC 16.2.1, Clang 22.1.8, CMake 4.3.0, Ninja 1.13.2, Python
3.14.8. Builds and tests ran on cores 4-23 (other release-preparation work shared the machine,
load average about 12-30); the owned retail install served as `--data` and `TAK_TEST_DATA`. The
version bump is not in this commit, so binaries still call themselves `0.8.8 (build
v0.8.8-11-g71757b759693)` (no `-dirty`).

**Result: no real regression, and every `--mpai` hash equals 0.8.8.** One Debug (`-O0`)
failure (`cartographer_ui_smoke`, load-sensitive) passed when rerun alone; the ASan suite
again needs `detect_leaks=0` on this machine (SDL's own D-Bus start-up leaks, the same 20 tests
by name as in 0.8.8). The Legion scenario gate passes in full with the same key counts as 0.8.8.

- **Multiplayer and replays.** `kNetVersion` is **288** in `src/net/protocol.h` and
  `kReplayFormat` is **14** in `src/net/replayhdr.h`, both as at `v0.8.8`. `git diff
  v0.8.8..71757b75 -- src/sim src/net src/server src/ai` is one header-only display helper in
  `src/net/client.h` (`roomModStatus`, so a room says GAME MODS VERIFIED only when game packs are
  in play); no simulation, protocol, server or AI file changed. 0.8.9 is therefore multiplayer-
  and replay-compatible with 0.8.8. The map generator's `formatVer` (9), the campaign payload's
  `kVersion` (4) and `kSchemaVersion` (9) are unchanged.
- **Builds.** Release, Debug, optimized Debug (`-O2`), Clang Debug and AddressSanitizer
  all-target builds completed with exit code 0: 890 steps in Release, 891 in the other four
  (886 / 887 in 0.8.8). ccache and mold; static-deps and static Bink FFmpeg prefixes. Release
  emits 950 warning lines, the same count and kinds as 0.8.8 (538
  `-Wmissing-field-initializers`, 347 `-Wdeprecated-declarations`, 6 `-Wunused-parameter`, 6
  `-Wmisleading-indentation`, 3 `-Wsign-compare`, 1 `-Wstringop-overread`, 1
  `-Wignored-attributes`, the rest library-header `-Warray-bounds` notes); none from the new
  files.
- **Full CTest.** **742** registered tests in the Debug trees (734 in 0.8.8) and **698** in
  Release (696); 8 added, none removed: `area_drag`, `area_drag_input`, `buildline`,
  `buildline_harness`, `buildline_chain_box`, `buildline_chain_double_back`,
  `buildline_chain_tower_v`, `buildline_chain_u`. Each tree ran as `-LE nightly` (536 tests in
  Release, 580 in the others, which include the `pref_guard` fixtures) and the Release and
  optimized Debug trees also ran `-L nightly` (164).

  | Tree | Selection | Result | Time | Failures |
  | --- | --- | --- | ---: | --- |
  | Release | `-LE nightly`, 536 | **536/536** | 400 s | none |
  | Release | `-L nightly`, 164 | **164/164** | 1,678 s | none |
  | Optimized Debug | `-LE nightly`, 580 | **580/580** | 647 s | none |
  | Optimized Debug | `-L nightly`, 164 | **164/164** | 1,386 s | none |
  | Debug (`-O0`) | `-LE nightly`, 580 | 579/580 | 3,502 s | `cartographer_ui_smoke` (passes alone) |
  | Clang Debug | `-LE nightly`, 580 | **580/580** | 2,788 s | none |
  | ASan | `-LE nightly`, 580 | 560/580 | 1,500 s | 20 tests, all LeakSanitizer reports inside SDL's D-Bus init (see below); **580/580 with `ASAN_OPTIONS=detect_leaks=0`** (the 20 plus the two `pref_guard` fixtures rerun, 22/22) |

  The unoptimized trees ran at the same time as the optimized ones, the scenario gate and the
  `--mpai` runs.
  - **`cartographer_ui_smoke` (Debug `-O0`): `FAIL: veruna map 'Abnar's Terrace' smoke: stuck at
    step 487` (modal 17, smoke stopped at step 487 of 557), 205.6 s.** The scripted UI smoke
    stalled on one step while the machine was loaded. It passed alone (132.4 s, rerun) and in
    the Release, optimized Debug, Clang Debug and ASan trees. Nothing under `src/cartographer`
    changed since 0.8.8; load-sensitive, but not seen before, so a candidate for the flake list.
  - **`voice` (optimized Debug) and `crusades_client_network` (Debug)** failed in 0.8.8 under
    load and did not fail this time.
  - **ASan: 20 tests fail only on leak reports.** `script_transient`, `script_transient_263`,
    `creon_death_sound_peers`, `voice`, `streaming_capture`, `stream_key`,
    `render_palette_indexed`, `render_native_pass`, `client_restart`, `shadow`, `fogupload`,
    `menu_music`, `menu_music_roster`, `death_sound`, `screen_lifecycle`,
    `campaign_presentation_roster`, `death_sound_roster` and the three `crusades_*_ui_network`
    tests, the same 20 by name as in 0.8.8 and 0.8.7. Every leak report is an `SDL_DBus_Init`
    allocation in the statically linked SDL; there is no `ERROR: AddressSanitizer` (no heap
    overflow, use-after-free or stack error) anywhere in the log. With
    `ASAN_OPTIONS=detect_leaks=0` all of them pass. The 560 other ASan tests passed with leak
    detection on.
  - **The unoptimized Legion tests passed under the existing limits.** `legion_sched_inflight`
    763 s / 800 s / 429 s, `legion_scale_neutral` 768 s / 801 s / 414 s, `legion_route_paused`
    1,340 s / 999 s / 499 s and `legion_parallel` 3,066 s / 2,482 s / 1,390 s (Debug / Clang
    Debug / ASan); the largest use of a limit is `legion_parallel` in Debug, 57% of 5400 s.
    `movement_orders_trolls` 338 s / 309 s / 108 s (limit 600 s: Debug at 56%).
    `terrain_schedule` 60 s / 64 s / 38 s and `voice_uplink` 49 s passed everywhere.
  - **The settings-leak guard held.** `pref_guard_setup`/`pref_guard_check` passed in every
    selection. The real profile's `settings.ini` has the same SHA-256 and mtime before and
    after (`real-profile-{before,after}.txt`), and no `settings.ini` appeared in `~/tak_data`.
    `TAK_SETTINGS_FILE` was not exported.
- **`--mpai` golden hashes: all unchanged from 0.8.8.** Each ran once on the optimized Debug
  build, Inner Circle, `takserver --local --no-auth --seed 1`, an empty `XDG_DATA_HOME`, and
  `TAK_LEGION=1` for Legion. No desync or referee-suspect line in any log. `end=timelimit`,
  `err=none` in all four. The area-command, build-line, chat-box and room-layout changes are
  client-side and change none of the Retail or the Legion 60/300 s state in this scenario.

  | Mode | Duration | `71757b75` (288) | 0.8.8 (288) |
  | --- | ---: | --- | --- |
  | Legion | 300 s | `ce5c4ba7562e53b9` (18 units) | `ce5c4ba7562e53b9` |
  | Retail | 300 s | `a3d953803a064191` (13 units) | `a3d953803a064191` |
  | Legion | 60 s | `0825aeb9e8139203` (4 units) | `0825aeb9e8139203` |
  | Retail | 60 s | `b353410003e98dd5` (4 units) | `b353410003e98dd5` |

  Optimized Debug binary SHA-256:
  - client `51501ad566a11c4a51f4703ab73159acdc214cbf326359f2aa2e9a503d6ebf3c`
  - server `b81bd4eccaac4fd0f3608fbd2a5bfefd2ea636c9be21f3a042eb7a4923e5c56c`
- **Determinism.** In `tools/check-determinism.sh`, the GCC and Clang x86-64 builds at
  O0/O2/O3 all agree on `dcef618cd2e4d558`, the same as 0.8.8. The aarch64 legs are skipped
  (target headers missing). `tools/check-detmath.sh` passes, including the `std::atomic_ref` guard.
- **Legion scenario gate: PASS.** The gate ran the way `tools/legion_nightly.sh` stage 2 does:
  every `tools/scenarios/*.scn` (**145** files, as in 0.8.8), optimized Debug
  `legion_scenario`, `--mode both --json --offsets gate --data <retail install>`, one file per
  core on cores 4-11, then `legion_check.py check --baseline tools/scenarios/baseline.json`. All
  145 files exited 0 (`problems.txt` empty); `legion_check.py validate --baseline
  tools/scenarios/baseline.json` reports `PASS: 0 problems`. Run time 5,131 s (the machine was
  loaded).
  - **Result:** `PASS: 36522 keys checked, 0 failed, 4415 ratchets, 4705 licensed, 266 floor
    exceptions, 864 report-only`, identical with and without `--cumulative` and identical to
    0.8.8's counts (no Legion state changed). The floor exceptions are the same rows as in 0.8.8
    (`taperbridge-*`, the gatehouse and fight-diag rows, `wall-4x50`).
  - The 4,415 ratchets are improvements over the committed baseline; they do not fail the gate
    and no baseline was regenerated.
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

Logs: `~/tak-tmp/scratch/release-089/` (`cfg-*.log`, `make-*.log`, `build-status.txt`,
`ctest-*.log`, `rerun-{dbg,asan-noleak}.log`, `mpai/`, `mpai.sh`, `gate/`
(`scenarios.jsonl`, `check.txt`, `check-nightly.txt`, `validate.txt`), `check-determinism.log`,
`check-detmath.log`, `check_retail_group.log`, `python-suite.log`, `verifier.log`,
`pycompile.log`, `bashn.log`, `retail-sha-{before,after}.txt`,
`real-profile-{before,after}.txt`).

**Final source.** After the preparation commits, the optimized Debug tree rebuilt every
target (version `0.8.9`) and passed `ctest -L quick` 477/477.

## Remote multiplayer sweep

The full 37-scenario table passed for both pathfinding modes, Retail and Legion, on **2026-10-22**
(UTC), both on `main` at `71757b759693` (protocol **288**, replay format **14**, build id
`v0.8.8-11-g71757b759693`; the version string still reads 0.8.8 until the 0.8.9 bump lands):

- **Retail:** 02:45-03:03 UTC (18 minutes including the gates), single pass, no retries.
- **Legion:** 03:03-03:31 UTC (29 minutes including the gates), single pass, no retries.

Referees ran on `tak.pgnet.us` (24 runs, 10 at a time) and `vpn3.pgnet.us` (13 runs, 6 at a time), with
optimized local clients pinned to CPUs 4-23. Each scenario allowed ten simulated minutes (18,000 ticks) or
ended at an actual result for that seat. Real seated players provide the client/referee checksum coverage;
the two all-AI spectator scenarios per mode (`w-allai-stress`, `w-allai-bench`) test flow control, not hashes.
The mode is chosen through the hosting client's room options (`TAK_LEGION=1` creates a Legion room).
`TAK_WALL_TIMEOUT=3600` was set for both modes from the start, so the wall-clock retry that 0.8.8's Retail pass
needed (`2h-stress`, `2h-orders-stress`) did not recur. Every recorded referee replay header confirms the
intended mode, replay format 14 and protocol 288: 37 headers per mode, seeds 2000-2036 each mapped to its case.

| Mode | Completed scenarios | Completed seats | Client-pair checkpoints | Client-referee checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Retail | 37/37 (single pass) | 52/52 | 10,274 across 22 seat pairs, 0 mismatched | 2,626 across 51 seat/referee comparisons, 0 mismatched |
| Legion | 37/37 (single pass) | 52/52 | 10,213 across 22 seat pairs, 0 mismatched | 2,687 across 51 seat/referee comparisons, 0 mismatched |

Each run passed all three `--validate` gates before its table started:

- **Build identity:** `both at v0.8.8-11-g71757b759693`.
- **Planted desync:** the referee reported `game 1: client 1 DESYNCED at tick 900`.
- **Game mod transfer:** `verified game mods transferred and incorrect loaded gameplay rejected`.

The gate checks only `tak.pgnet.us`; the binary on `vpn3.pgnet.us` has the same SHA-256
(`f5621d4ffc1f6083de28d35d8cd0e7ecea6d8b26de97286200cc941afc5552cd`) and the same `--version` build id. No
natural `DESYNCED`, `REFEREE SUSPECT` or client-reported desync appeared in any case log; the only such line
in each run is the planted one. Both sweeps ended with "no desyncs reported (52 seats, all completed)".
A state hash is compared at every checkpoint; the position digest is compared as well wherever both sides
carry one. Every game wrote a referee replay: 37 per mode, matched to cases by the seed in the replay header.
The checkpoint comparison script is the 0.8.8 one, unchanged, and `tools/desync-hunt-remote.sh` is unchanged
since 0.8.8 (SHA-256 `df09e28844be1f399f9466a62e19c9cdf53298e5fdb9eb5dcefb5fca830ad56e`). The referee was
rebuilt in the Ubuntu 24.04 container from this exact source.

### Reference ticks and hashes (0.8.9 candidate)

Hash and units are the seated host's final line. "vs 0.8.8" compares tick and hash with the 0.8.8 table
(protocol 288; same harness, hosts and seeds). The six `TAK_AUTOPLAY` cases never repeat a hash, and `h-lake`,
`h-stress`, `2h-stress`, `2h-gods`, `4h-gods`, `h-everything` are load-sensitive, so "differs" in those
rows is expected noise. The machine carried a load average of about 25 from other release work during both runs.

| Case | Retail end | Retail tick | Retail hash | Retail units | vs 0.8.8 | Legion end | Legion tick | Legion hash | Legion units | vs 0.8.8 |
| --- | --- | ---: | --- | ---: | --- | --- | ---: | --- | ---: | --- |
| `h-baseline` | timelimit | 18,000 | `4a07030c0169e47c` | 210 | same | timelimit | 18,000 | `4b9217a4ffcd74c0` | 213 | same |
| `h-gods` | concluded | 14,626 | `0ea0a1d96c1414af` | 104 | same | concluded | 15,383 | `3f26557d500cff5a` | 82 | same |
| `h-crusades` | timelimit | 18,000 | `604de77d3fe53f3e` | 245 | same | timelimit | 18,000 | `e4036388d6f2e4df` | 230 | same |
| `h-stress` | concluded | 4,903 | `67ad340e719d474e` | 8,593 | differs (tick 4,896 -> 4,903) | concluded | 5,147 | `4efaa19f898d273d` | 7,305 | same |
| `h-absurd` | timelimit | 18,000 | `1b0d79cb6583219d` | 454 | same | timelimit | 18,000 | `9125e38fec71fc29` | 469 | same |
| `h-fog-explored` | timelimit | 18,000 | `00419b0edd6c9b06` | 188 | same | timelimit | 18,000 | `faafb450fc7005c7` | 142 | same |
| `h-fog-full` | timelimit | 18,000 | `9896fd941b2aade9` | 216 | same | timelimit | 18,000 | `821e2ab5feef82d1` | 217 | same |
| `h-unitcap` | concluded | 14,780 | `c382486eeb54fb3b` | 204 | same | timelimit | 18,000 | `715966bbb3c6a746` | 220 | same |
| `h-cramped` | concluded | 1,307 | `45134591344a4d04` | 10 | same | timelimit | 18,000 | `57d4067d04331580` | 29 | same |
| `h-naval` | timelimit | 18,000 | `5fb53d62d7a0b42c` | 58 | same | timelimit | 18,000 | `17b9ca802fb9a217` | 88 | same |
| `h-naval-crus` | timelimit | 18,000 | `396f8de68f25c274` | 70 | same | timelimit | 18,000 | `4787a6c75fbb0d77` | 88 | same |
| `h-lake` | concluded | 3,945 | `fe64c8f45a404f6c` | 4,922 | differs (tick 3,948 -> 3,945) | concluded | 4,108 | `0b1069698f25511e` | 4,836 | differs (tick 4,139 -> 4,108) |
| `h-random-starts` | concluded | 15,702 | `6e0395e042f734fe` | 228 | same | concluded | 12,289 | `0cba4154cf002db9` | 194 | same |
| `h-monarch-exp` | concluded | 12,231 | `c646ef640c32a947` | 89 | same | concluded | 17,804 | `540592e154eafb24` | 82 | same |
| `h-mods-all` | timelimit | 18,000 | `450038fec3e01ce5` | 209 | same | timelimit | 18,000 | `e3693d73ec08c60c` | 212 | same |
| `h-everything` | concluded | 13,553 | `c8a3b86655f699f1` | 5,140 | differs (tick 13,549 -> 13,553) | concluded | 10,300 | `13ca94202d9d0533` | 4,782 | same |
| `h-speed-1x` | timelimit | 18,000 | `9b5694cceca37a11` | 236 | same | timelimit | 18,000 | `0c1cf293aa498699` | 246 | same |
| `h-two-castles` | timelimit | 18,000 | `8e9dfa2311fc1ec4` | 23 | same | timelimit | 18,000 | `81b4f5bfafb359c0` | 4 | same |
| `2h-baseline` | timelimit | 18,000 | `7b5850e128c0ff63` | 173 | same | timelimit | 18,000 | `638fa388b12be0c4` | 172 | same |
| `2h-gods` | concluded | 12,423 | `bf5aaf973f862369` | 104 | same | concluded | 13,059 | `9283d91d154a8678` | 61 | same |
| `2h-crusades` | timelimit | 18,000 | `4177d6f4374aad35` | 182 | same | timelimit | 18,000 | `e14b49adbbf76ebf` | 167 | same |
| `2h-stress` | concluded | 5,051 | `9cbca5e70ece9898` | 9,101 | differs (tick 5,048 -> 5,051) | concluded | 5,248 | `2467ac365aab5e5b` | 8,427 | differs (tick 5,265 -> 5,248) |
| `2h-orders` | timelimit | 18,000 | `6179769dbd0a466b` | 187 | differs (autoplay) | timelimit | 18,000 | `bbd77f0ada3375f1` | 158 | differs (autoplay) |
| `2h-orders-stress` | concluded | 5,114 | `69f982d90677705f` | 8,984 | differs (tick 5,709 -> 5,114) (autoplay) | concluded | 5,074 | `a9e4035b35b53077` | 8,307 | differs (tick 4,894 -> 5,074) (autoplay) |
| `4h-orders` | timelimit | 18,000 | `2a1d7913b0beaa9d` | 156 | differs (autoplay) | timelimit | 18,000 | `899a57b3b6ab1e0c` | 157 | differs (autoplay) |
| `3h-baseline` | timelimit | 18,000 | `1d59d944a244aef4` | 172 | same | timelimit | 18,000 | `beeeab4bb6ec584b` | 148 | same |
| `4h-gods` | timelimit | 18,000 | `39e8fa7afa49b6f9` | 149 | differs | timelimit | 18,000 | `01629df7129fa319` | 152 | differs |
| `net-rtt50` | timelimit | 18,000 | `80dc6a72233bd05d` | 230 | same | timelimit | 18,000 | `03c521246aeadec2` | 227 | same |
| `net-rtt100` | timelimit | 18,000 | `fcba4a56baf98c45` | 190 | same | timelimit | 18,000 | `c3167c3d271f43bc` | 204 | same |
| `net-jitter` | timelimit | 18,000 | `6a0f640c3faf1de6` | 173 | same | timelimit | 18,000 | `ca07f9821f97109e` | 196 | same |
| `net-rtt500` | timelimit | 18,000 | `8837809d723b6fc0` | 219 | same | timelimit | 18,000 | `af7b2fa90b652283` | 200 | same |
| `net-loss1` | timelimit | 18,000 | `1adf2c3d366e9bef` | 192 | same | timelimit | 18,000 | `b69a197764ecdf5c` | 206 | same |
| `net-loss3-orders` | timelimit | 18,000 | `5bb89190055f4386` | 199 | differs (autoplay) | concluded | 17,529 | `a2ead6daac4a0719` | 204 | differs (tick 16,878 -> 17,529) (autoplay) |
| `net-orders100` | timelimit | 18,000 | `40acbbae96f8a5e0` | 193 | differs (autoplay) | timelimit | 18,000 | `0a2f77d3fe3edd46` | 188 | differs (autoplay) |
| `net-2h-rtt100` | timelimit | 18,000 | `6e1a627c2887571a` | 182 | differs (autoplay) | timelimit | 18,000 | `b19dd6f6f94f8d83` | 173 | differs (autoplay) |
| `w-allai-stress` | timelimit | 18,000 | `06e78e00b732019a` | 5,392 | same | timelimit | 18,000 | `fc6dfe90da499b44` | 3,925 | same |
| `w-allai-bench` | timelimit | 18,000 | `10a3a871daabf505` | 312 | same | timelimit | 18,000 | `8446871154cf8f79` | 344 | same |

### Comparison with 0.8.8

0.8.9 changes no simulation, network, server or AI code (`git diff v0.8.8..HEAD -- src/sim src/net src/server src/ai`
shows only a 10-line header-only display helper in `src/net/client.h`; `kNetVersion` is still 288 and the replay
format 14), so 0.8.9 is multiplayer- and replay-compatible with 0.8.8, and every deterministic case should
reproduce 0.8.8 exactly.

**Retail: 26 of 37 cases are bit-identical to 0.8.8 (tick and hash).** The 11 that differ are exactly the known unstable ones:

- The six `TAK_AUTOPLAY` cases (`2h-orders`, `2h-orders-stress`, `4h-orders`, `net-orders100`, `net-loss3-orders`, `net-2h-rtt100`), which never repeat a hash.
- Five load-sensitive cases: `h-stress` (4,896 -> 4,903), `h-lake` (3,948 -> 3,945), `2h-stress` (5,048 -> 5,051), `h-everything` (13,549 -> 13,553; 5,140 units both times) and `4h-gods` (18,000 ticks both times, 149 units vs 102).

**Legion: 28 of 37 cases are bit-identical to 0.8.8.** The 9 that differ are the six autoplay cases plus `h-lake`
(4,139 -> 4,108), `2h-stress` (5,265 -> 5,248) and `4h-gods`, the same load-sensitive set. Every case that
0.8.8 flagged as moved by rounds 5/6 and that is not load-sensitive (`h-mods-all`, `net-rtt50`, `net-rtt500`,
`net-loss1`, `w-allai-stress`, `w-allai-bench`, `h-stress`, `h-everything`) reproduced 0.8.8 exactly.

Caveat: the differing rows were not re-run in isolation. They are explained by the documented load sensitivity
(very large games and cases where a wall-clock-driven client pacing decision shifts command arrival ticks),
not by a code change: the source delta contains no simulation code, and every seat agreed with the referee at
every checkpoint in every case.

## CI and packages

Main CI on the final source `e170e226`: [Determinism](https://github.com/pocketgeek/tak-engine/actions/runs/39723733377), [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/39723733426), [Linux packages](https://github.com/pocketgeek/tak-engine/actions/runs/39723733393) and [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/39723733422) all passed on the first attempt.

On the exact tag (`e170e226`, annotated tag object `93bbdf28`), all four workflows passed. Linux packages needed one rerun for a known network flake: on the first attempt the Ubuntu 22.04 x64 job failed in "Configure + build" because `https://downloads.xiph.org/releases/opus/opus-1.5.2.tar.gz` could not be downloaded, so "Attach to release" was skipped. `gh run rerun --failed` passed on attempt 2. No other job failed, so none of the other known flakes appeared: macOS `stream_test` timing (x64 and arm64), "Upload CI artifact: ENOTFOUND", macOS x64 `hdiutil` "Resource busy", the Fedora openh264 mirror, the Slackware mirror `md5sum` error, `gitlab.freedesktop.org` connect failures, and the Ubuntu 22.04 `crusades_client_network_test` "complete replay not published".

- [Determinism and core tests](https://github.com/pocketgeek/tak-engine/actions/runs/39726695130): started manually on the tag because the workflow is path-filtered. `unit-tests`, `detmath` and `sim-equiv` passed. The GCC and Clang x86-64 builds at O0/O2/O3 all agree on golden `dcef618cd2e4d558` (aarch64 skipped), the same as 0.8.8. This workflow runs no `--mpai` game.
- [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/39726687160): all 16 native package jobs and the release attachment (attempt 2).
- [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/39726687269): x64, ARM64 and the x64 signing/package job.
- [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/39726687304): both builds plus the **Sign + notarize** job for each architecture. That job replaced the unsigned macOS ZIPs and DMGs in the draft with signed, notarized and stapled ones.

The release was created as a draft with these notes, and tagged CI attached the packages. All **29 assets** were downloaded after the macOS signing finished. They are the same set as 0.8.8 with the version swapped, and none has size 0 or an upload state other than "uploaded". Each one matched GitHub's SHA-256 digest and byte size before publication. Every release asset is between 0.03% and 0.12% larger than its 0.8.8 counterpart (ratios 1.0003 to 1.0012, 7-32 KB). The debug archives grew 0.20% to 0.26%. The release `takclient` changed by -16 KB to +68 KB across platforms, and every `takserver` is exactly the same size as in 0.8.8. That fits a client-only release with no new dependencies. Each package, installer and ZIP ships the same file list as its 0.8.8 counterpart.

- **Build ids.** The client and server in every Linux package carry `v0.8.9`. That covers Debian 12 and 13, Ubuntu 22.04, 24.04 and 26.04, Fedora 44 and openSUSE Leap 16.0 (each for both architectures), plus Arch, Slackware and both Linux debug tarballs.
  - On Linux, the x86-64 `takclient` and `takserver` report `0.8.9 (build v0.8.9)`. That holds for Debian 12 and 13, Ubuntu 22.04, 24.04 and 26.04, Fedora 44, openSUSE Leap 16.0, Slackware and the x64 debug tarball. So does the Arch `takserver`. The Arch `takclient` needs glibc 2.44, newer than the checking host's 2.43, so only its build string was checked. For the arm64 binaries, only the build string `v0.8.9` was checked.
  - On Windows 11, the x64 Windows ZIP's `takclient.exe` and `takserver.exe` report `0.8.9 (build v0.8.9)`. For the ARM64 `takclient.exe` and `takserver.exe`, and for the installer's, only the build string `v0.8.9` was checked.
  - On the arm64 MacBook (macOS 26.6.2), `takclient` and `takserver` report `0.8.9 (build v0.8.9)` for both architectures. This holds for the bare programs from the ZIPs and for the copies inside the game app from each DMG and each ZIP. The x64 copies ran under Rosetta.
- **Windows signatures.** `Get-AuthenticodeSignature` on oden-win reports **Valid** (CN=Curtis Edge, timestamped) for 9 signed files, the same count as 0.8.8. They are the setup `.exe`, plus `takclient.exe`, `takserver.exe`, `cartographer.exe` and `crusades_admin.exe` in both the x64 and ARM64 ZIPs. The debug ZIPs were not checked.
- **macOS notarization.** On the MacBook, both DMGs pass `xcrun stapler validate` and `spctl` (open, primary signature) as **accepted, source=Notarized Developer ID, origin=Developer ID Application: Curtis Edge (L479Y59RN8)**.
  - Both apps (the game and TAK Cartographer) pass both checks the same way, from each DMG and from each ZIP.
  - Every bundled and bare `takclient`, `takserver`, `cartographer` and `crusades_admin` passes `codesign --verify --strict` (TeamIdentifier L479Y59RN8).
  - `spctl` says the bare command-line programs do not seem to be an app. That is expected for unstapled command-line tools.
- **Opus is static.**
  - No packaged Linux `takclient` or `takserver` has a `NEEDED` entry beyond libc, libm and the loader (Slackware also lists libdl and libpthread), so none needs libopus. The Debian and Ubuntu packages depend only on `libc6`, and the RPMs require only libc, libm and the loader.
  - No Windows `.exe` imports or names an Opus DLL, and the ZIPs ship no DLLs. The x64 import tables list only system DLLs.
  - No macOS binary links a libopus dylib (`otool -L`).
  - `OPUS-LICENSE.txt` is in every Debian, RPM, Arch and Slackware package, in both Linux debug tarballs, in the Windows installer, in all four release ZIPs (inside the game app's Resources on macOS) and in the four debug ZIPs.
- **Font licences.** `CHAT-FONT-LICENSE.txt` sits beside `EDITOR-FONT-LICENSE.txt` in every place the game ships:
  - every Debian, RPM, Arch and Slackware package (`share/licenses/tak-engine`) and both Linux debug tarballs;
  - the Windows installer, both Windows release ZIPs and both Windows debug ZIPs;
  - both macOS debug ZIPs;
  - the game app's `Contents/Resources` from both DMGs and both ZIPs.

  As in 0.8.8, TAK Cartographer's app has only `EDITOR-FONT-LICENSE.txt`, which covers its Noto Sans Mono editor font.
- **Mods folder.** No package, installer, ZIP or DMG contains an `overrides/` or `mods/` directory, and none contains `click.hpi`. This is the same as 0.8.8: `mods/cosmetic/click.hpi` exists only in the repository.
- **macOS microphone.** The game app's `Info.plist` carries `NSMicrophoneUsageDescription` in both DMGs and both ZIPs. The `com.apple.security.device.audio-input` entitlement is on the game app's `takclient` (DMG and ZIP, both architectures) and on the bare ZIP `takclient`. It is absent from `takserver`, `crusades_admin` and `cartographer`.
- **macOS Retina.** `NSHighResolutionCapable` is `true` in the `Info.plist` of both apps (the game and TAK Cartographer), from both DMGs and both ZIPs.
- As in 0.8.1 to 0.8.8, no packaged Linux `takclient` has a `NEEDED` entry for libsecret, GLib, GIO or GObject.
- `docs/quick-start.md`, `docs/release-0.8.9-notes.md`, this report and the build-line pictures `docs/img/guide/build-line.jpg` and `docs/img/quick/build-line.jpg` are in the source tree at the tag. `kNetVersion` there is 288.

Published 2026-10-22 06:18 UTC with `gh release edit v0.8.9 --draft=false --latest`; the API's latest release is `v0.8.9`, with 29 assets.

## Screenshots

Retaken on branch `task-release-089-shots` (by the 0.8.9 capture build,
version set to 0.8.9 locally; capture-only patch in
`~/tak-tmp/scratch/release-089/shots/capture-089.patch`, not committed), each
opened and checked by eye:

- **Recaptured, gallery:** `title.jpg` (VERSION 0.8.9), `lobby.jpg` and
  `lobby-scenario.jpg` (one option button width, one label size), `room.jpg`
  (MAP SCENARIO: OFF as a rules line at the others' size; no GAME MODS
  VERIFIED with no game pack ticked).
- **Recaptured, guide:** `guide/chat-box.jpg` (the box above a Barracks'
  build row and its TRAINING Horseman (10) line; from the `--testbuild`
  selection still life, not a local-server game: the capture patch lets that
  serverless fixture draw the chat box), `guide/room-mods-power-codes.jpg`
  (Big Battles ticked, so GAME MODS VERIFIED still shows; MAP SCENARIO rules
  line), `guide/random-map-lobby.jpg` (crop from x 100: the panel sits 50 px
  further right); Cartographer `cartographer/test-lobby.jpg` (MAP SCENARIO: ON
  (TEST MAP) as a rules line, details under both columns).
- **New:** `guide/build-line.jpg` (a nine-site wall line with GAP 2, from the
  debug `TAK_BUILDLINE_SHOT=560,0,2` fixture), under *Building* in the user
  guide.
- **Quick start** (branch `task-release-089-quick`): `quick/lobby-move-here.jpg`
  recaptured (its 0.8.8 picture showed GAME MODS VERIFIED with no game pack);
  new `quick/build-line.jpg` (a tight diagonal and a GAP 3 line).
- **Re-rendered and kept:** `diplomacy.jpg`, `guide/options-interface.jpg`,
  `guide/options-graphics.jpg` (nothing they show changed). Every other
  picture, including the per-release scenes, keeps its earlier capture.
- README title alt text and gallery footnote, the `chat-box.jpg` alt text and
  the Cartographer guide's `test-lobby.jpg` alt text were updated to match;
  `docs/img/README.md` header, rows and scratch list updated.

## Limits

- No game mixing 0.8.8 and 0.8.9 builds was played (it can be: same protocol
  and replay format).
- All of 0.8.8's limits carry over unchanged (the simulation is 0.8.8's):
  Legion units stand and wait more than Retail's; Legion attackers through a
  narrow gate deal more damage than Retail's; power codes are lockstep (retail
  applied them only on the typer's machine); FULL VISION does not switch LOS
  off at start; UI FONT CLASSIC is not pixel-identical with 0.8.7; the Legion
  bridge lottery; the Legion 10,000 workloads' congestion and tick budget,
  mixed-10000's hovering flyers; Retail `h-cramped` ends early; the macOS
  Retina UI Scale is migrated once; a stationary auto-targeting shooter keeps
  refusing a once-blocked target.
- The campaign book's chapter numeral still touches the bottom of "APTER"
  (retail-equivalent: the retail lettering boxes overlap).
- The area-command test deliberately differs from retail's (`0x527bf0`): 32 px
  on both axes of the box and a real pointer movement, no held-time rule.
- Flakes seen in this release's runs: `cartographer_ui_smoke` stalled once at
  step 487 in the loaded Debug (`-O0`) tree and passed alone (new candidate for
  the flake list); ASan's 20 SDL D-Bus leak reports (known, `detect_leaks=0`).
  The remote sweep needed no retries.
