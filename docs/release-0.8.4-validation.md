# 0.8.4 release validation


Protocol **277** (0.8.3: 266), replay format **13**, generator version **8**,
campaign payload **4**, database schema **9** (the last four unchanged since
0.8.3).
<!-- LEAD: re-check kNetVersion right before tagging (it moved during 0.8.0 prep).
     Version 0.8.4 and protocol 277 are already written in: CMakeLists.txt
     (project VERSION); README.md (badge, random maps x2, multiplayer, overrides,
     New in 0.8.4); docs/user-guide.md (intro, Versions, Replays, public server,
     old replays); docs/public-server.md; docs/random-map-generation.md;
     docs/multiplayer-design.md (pathfinding protocol summary, 268-277 replay
     history, the required-protocol line); docs/img/README.md (the flyers
     captions); these notes and the release notes. (276 stays where it names
     the voice chat step.) If kNetVersion moves past 277, update every one of
     them. -->

Tagged `v0.8.4` at `de8cd784` (annotated tag object `1fc81edd`) and published on 2026-10-18 23:03 UTC.

## Scope and compatibility

0.8.4 follows 0.8.3 (protocol 266). After the 0.8.3 validation commit
`c2c25707`, main reached `6a5fa106` in 226 commits, and the release adds four
test and warning fixes on top (`8cf4c93c` to `0fe6305d`), 230 in all. The 226
are 121 commits up to `c551ae6c` (the simulation in both pathfinding modes, the
server-side AI, the client with voice chat and replays, the network layer and
server, the tests and the documentation) and 105 after it: the rendering
optimization program's 100 commits on its branch (based on `c2c25707`) and its
later `cd46c2c3` and `7cae1561`, which make 102 for the program; its merge
`a8bbe6ed` (made on the program's branch); `f11fbc1a`, the program's final
report; and `6a5fa106` (the Legion formation flyers, 277). So 121 + 100 + 2 +
1 + 1 + 1 = 226, and 230 with the four fixes. The four fixes change tests, the
build's test limits and two warnings. `git diff --stat v0.8.3..0fe6305d -- src` touches 91 files
(the same files as at `f11fbc1a`): `src/sim/` (12), `src/client/` (59),
`src/net/` (13), `src/server/` (3), `src/ai/` (2) and `src/terrain/` (2). The
rendering program changes 35 client files (6 of them also changed on main) and
both terrain files, and no file under `src/sim/`, `src/net/`, `src/server/` or
`src/ai/`. Nothing under `src/tnt/` (the map generator), `src/campaign/`,
`src/cartographer/`, `src/cob/`, `src/crt/` or `src/util/` changed.

The protocol steps, each recorded in `src/net/protocol.h`:

- **267** explicit selection orders, Legion 10,000 stage 3 (`3312368f`,
  `956d8dce`, `6fbce6bd`); Retail clients keep per-unit commands, so Retail is
  bit-identical.
- **268** fair navigation scheduling and budgets, stage 4 (`d487d65d`).
- **269** route queries, stage 5 (`662574f1`).
- **270** hierarchical routes, stage 6 (`ca157c28`).
- **271** static navigation apart from traffic, stage 7 (`eebef946`).
- **272** chokepoints, subgroups and arrivals, stage 8 (`4f30f19e`,
  `1bb377c3`). Stage 9 (parallel planning, `9064aa35` and before) is exact and
  has no protocol step; the program was merged by `f1374ceb`.
- **273** construction shares short mana, Retail and Legion (`af90da79`),
  hashed.
- **274** a lodestone upgrade finishes under a short share (`57a218d1`).
- **275** AI level 5, Absurdly Defensive (`a5f8b429`).
- **276** voice chat: `VoiceListen` / `VoiceUp` / `VoiceDown`, relayed by the
  server, never in a tick bundle or replay (`60e98711`, `6766c844`,
  `4dc8fa88`). Sim unchanged.
- **277** Legion formation flyers keep their stations over the front while
  their ground waits in the registration queue for its first navigation plane
  (`6a5fa106`, Legion only; a regression from stage 4's `b052597e`, never
  published). Retail is bit-identical.

Without a protocol change: the Defensive AI melee answer (`7e1e3798`) and its
grown base, sites and stations (`8588b3df`); replays streamed while playing
(`eadcfd76`, `c2319131`, replay format unchanged); the rest of the voice chat
client (`3d73859d`, `e0714bac`, `8fcd0718`, `b9ab446a`, `e6dc6e30`, `63acb2fd`,
`ae90d023`, `7d04578c`, `c551ae6c`); the CI target fix (`27066ebe`); the
desync sweep's `h-cramped` Monarch Expendable (`5774601a`); and the release's
test and warning fixes: `voice_uplink` no longer forms `&buf[size()]` for a bare
Ping (`8cf4c93c`), `terrain_schedule` and `terrain_cache` run on simulated
frame clocks and gate their upload-time check (`bf587967`), the `-Wswitch` and
`-Wunused-variable` warnings new since 0.8.3 are gone (`124374db`), and
`legion_route_paused`, `legion_parallel`, `legion_scale_neutral` and
`legion_sched_inflight` get longer limits in unoptimized and sanitizer builds
(`TAK_SLOW_TEST_BUILD`, `0fe6305d`).

**Retail is not bit-identical to 0.8.3**: 273 (shared mana) and 274 change
Retail games. 267 to 272 leave Retail bit-identical (the Legion 10,000 report,
section 4), 275 and 276 leave the default-level simulation unchanged, and 277
changes Legion only.
The `--mpai` Inner Circle seed 1 hashes recorded by `af90da79` and unchanged
since are Retail 300 s `a3d953803a064191`, 60 s `b353410003e98dd5`; Legion
300 s `ce5c4ba7562e53b9`, 60 s `0825aeb9e8139203` (0.8.3's Retail 300 s was
`7535cb08ee787b75`). An optimized Debug build of `657aef1b` (`f11fbc1a` plus
these docs) gave all four, once each, against the user's retail install
through a read-only scratch folder (`takserver --local --no-auth --seed 1`,
scratch profiles). The rendering program's own merge check found the same four
on its merge `a8bbe6ed`, whose `src/` equals `f11fbc1a`'s. `6a5fa106` (277)
recorded the same four (the AI never forms formations), and `124374db`
rechecked the two 60 s hashes. The rerun on the final tree, `0fe6305d`, matched all four (Retail 60 s
`b353410003e98dd5`, 300 s `a3d953803a064191`; Legion 60 s `0825aeb9e8139203`,
300 s `ce5c4ba7562e53b9`).

Every measured figure in the [release notes](release-0.8.4-notes.md) is taken
from the commit that made the change, with its setup, or from the section of
`docs/legion-10000-report.md`, `docs/legion-pathfinding.md`,
`docs/ai-defensive-2026-10-17.md` or `docs/multiplayer-design.md` it cites. The
Rendering section's figures are Experiment 17 of
`docs/render-optimization-progress.md` (forced-completion table and memory
table, A = `60f2b586`, the 0.8.3 renderer with the program's telemetry, B =
`f97f21c8`, the program's final renderer, on the same simulation).

**Compatibility.** `kNetVersion` is **277** in `src/net/protocol.h` at
`6a5fa106`, against 266 at `v0.8.3`. `kReplayFormat` is **13** in
`src/net/replayhdr.h` (13 at `v0.8.3`); the map generator's `formatVer` is 8
(`src/tnt/mapgen.h`, no change under `src/tnt/`), the campaign payload's
`kVersion` 4 (`src/net/crusades.h`, unchanged) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`, unchanged).

- **Multiplayer.** The server's Hello check (`src/server/server.cpp`) rejects a
  client whose protocol differs from `kNetVersion` ("protocol version
  mismatch"), as well as a different pure-retail gameplay-data hash. So **0.8.4
  cannot play with 0.8.3 or earlier in either direction**. The server and all
  players must update together.
- **Replays.** `supportedReplayProtocol` (`src/net/replayhdr.h`) accepts a
  recording only when its protocol equals `kNetVersion`, so 0.8.4 does not play
  0.8.3 or older replays and those builds do not play 0.8.4 replays, although
  the file format is still 13. `tools/replay_test.cpp` pins
  `kNetVersion == 277`. The streamed writer's finished `.takrep` is the format-13
  layout byte for byte (`replay_writer`).
- **Maps and settings.** Generated-map recipes (generator 8) and saved `.kmp`
  maps carry over. A 0.8.3 settings file reads the same in 0.8.4. 0.8.4 adds
  `voiceEnabled`, `voiceMicDevice`, `voiceOutputSameAsGame`,
  `voiceOutputDevice`, `voiceMicVolume`, `voiceOutputVolume` and the
  `hotkey.voice.ptt` override; 0.8.3 ignores the voice keys (and drops them when
  it saves) and keeps any `hotkey.*` entry it does not know.

## Local preparation checks" and "

## Remote multiplayer sweep". Written by the
     validation agents from a clean detached worktree (~/tak-tmp/wt/release-084-val):
     first round on main f11fbc1a (logs ~/tak-tmp/scratch/release-084/{local,remote}/),
     second round on task-release-084-fixes 0fe6305d = main 6a5fa106 + four test/warning
     fixes (logs ~/tak-tmp/scratch/release-084/rerun277/). The f11fbc1a-only version of
     this fragment is kept as validation-sections.f11fbc1a.md. -->

## Local preparation checks

The checks ran in two rounds, both from a clean detached worktree
(`~/tak-tmp/wt/release-084-val`) with `assets` and `third_party` links to the main
checkout.

1. **First round: `main` at `f11fbc1a278c7f2b21cd060609ac8a57a2dd0ef6`**
   (protocol 276). This covered everything: five build trees, full CTest in each,
   ASan, emulation, determinism, `--mpai`, the research suites and the syntax
   checks. Details are under [First round](#first-round-f11fbc1a-protocol-276)
   below.
2. **Second round: `0fe6305d701a1b248862b7bc0fc44b166702dd89`**, the head of
   `task-release-084-fixes` (protocol **277**). This is `main` `6a5fa106` (the
   Legion flyer-front fix) plus four commits that fix the test and warning
   problems the first round found:
   - `8cf4c93c`: `voice_uplink` vector index;
   - `bf587967`: terrain tests use simulated clocks;
   - `124374db`: the two new warnings;
   - `0fe6305d`: longer Legion test timeouts in unoptimized builds.

   This round reran what `6a5fa106` can invalidate, plus the tests the fixes
   touched.

**Why a second round, and what it covers.** `6a5fa106` changes hashed Legion
state (`LegionNavigator::advancing` now treats a unit queued for registration as
waiting), so it bumps `kNetVersion` to 277. Everything that pins Legion
simulation results was redone on the new head:

- the four `--mpai` hashes;
- `check-determinism`;
- the Legion scenario gate (`legion_check check --cumulative`);
- `ctest -L quick` in Release and optimized Debug;
- the Legion remote sweep (see [Remote multiplayer sweep](#remote-multiplayer-sweep)).

The tests the four fix commits touched were rerun in every tree: `voice_uplink`,
`terrain_schedule`, `terrain_cache`, `legion_route_paused`, `legion_parallel`,
`legion_scale_neutral` and `legion_sched_inflight`.

Some first-round checks were not repeated:

- **The full five-tree CTest and the focused ASan run.** For the Legion change,
  the scenario gate over every scenario file stands in for them, together with
  the quick label in both optimized trees and the touched tests in all five
  trees.
- **The Retail group emulation check, the Python research suite, the archive
  verifier and the syntax checks.** These exercise code and files that neither
  `6a5fa106` nor the fixes touch.

`6a5fa106` touches only `src/sim/legion.{h,cpp}`, `src/net/protocol.h`,
`tools/legion_world_test.cpp` and `tools/replay_test.cpp`. The fixes touch only:

- three test sources;
- CMake test timeouts;
- a no-op `case Cmd::Order: break;` in `src/sim/matchsetup.cpp`, where the
  switch already returned earlier for that command;
- a `[[maybe_unused]]` in `src/client/gameview_render.cpp`.

### Second round: `0fe6305d` (protocol 277)

**Result: every rerun check passed, with no reruns or retries needed.** The four
`--mpai` hashes equal the protocol-276 values. Determinism holds. The Legion
gate passes all 135 scenario files. Both quick suites pass, and so does the
Legion remote sweep. The first round's test and warning problems are fixed:
every touched test passed in every tree, including `-O0`, Clang Debug and ASan, with
the machine loaded. The Retail `h-cramped` limitation is recorded as known (see
the remote section).

- **Protocol 277.** `kNetVersion` is **277** in `src/net/protocol.h`, and every
  `takserver` announced `protocol v277`. The replay format stays **13**. So
  0.8.4 can play neither multiplayer nor replays with 0.8.3 or earlier, nor with
  the protocol-276 builds of the first round.
- **Builds.** The five trees from the first round were rebuilt incrementally at
  `0fe6305d`: Release, Debug, optimized Debug (`-O2 -g`), Clang Debug and ASan.
  Each exited 0 after the 385 or 386 steps the change set needed. Every binary
  reports `0.8.3 (build v0.8.3-231-g0fe6305d701a)`, with no `-dirty` suffix.
  **Neither the `-Wswitch` nor the `-Wunused-variable` warning appears any
  more** in the Release or optimized Debug build logs, which recompiled both
  files (`make-build.log`, `make-build-o2.log`).
- **`--mpai` golden hashes: all four unchanged.** Each ran once, on the optimized
  Debug build, Inner Circle, `takserver --local --no-auth --seed 1`, an empty
  `XDG_DATA_HOME`, and `TAK_LEGION=1` for Legion. The four runs were concurrent
  on separate core pairs. No client or server log has a desync or
  referee-suspect line. The AI never forms the squads that 277 changes, so the
  hashes are expected not to move.

  | Mode | Duration | `0fe6305d` (277) | `f11fbc1a` (276) | 0.8.3 |
  | --- | ---: | --- | --- | --- |
  | Legion | 300 s | `ce5c4ba7562e53b9` | `ce5c4ba7562e53b9` | `c707e1a8063bc0c7` |
  | Retail | 300 s | `a3d953803a064191` | `a3d953803a064191` | `7535cb08ee787b75` |
  | Legion | 60 s | `0825aeb9e8139203` | `0825aeb9e8139203` | `532539f51d70840b` |
  | Retail | 60 s | `b353410003e98dd5` | `b353410003e98dd5` | `2c39430636a3dbda` |

  Optimized Debug binary SHA-256:
  - client `e40b760b7757cd2dfb2d220cfeaf74629d38036f247260aa459a912ff70fb7c3`
  - server `8412363520930caabd526db5690affdaccd8caad18d75172b6995aacb3552cf9`
- **Determinism.** In `tools/check-determinism.sh`, the GCC and Clang x86-64
  builds at O0/O2/O3 all agree on `dcef618cd2e4d558`, the same as 0.8.3. The
  aarch64 legs are skipped because target headers are missing.
  `tools/check-detmath.sh` passes, including the `std::atomic_ref` guard.
- **Legion scenario gate: PASS.** The gate ran the way `tools/legion_nightly.sh`
  stage 2 does:
  - every `tools/scenarios/*.scn` (135 files), with the optimized Debug
    `legion_scenario`;
  - `--mode both --json --offsets gate --data <retail install>`;
  - one file per core on cores 14–23.

  Then it ran `legion_check.py check --cumulative --baseline
  tools/scenarios/baseline.json` over the 270 result lines.
  - **Result:** `PASS: 31859 keys checked, 0 failed, 3005 ratchets, 3333
    licensed, 220 floor exceptions, 864 report-only`. That is the same key count
    `6a5fa106` reported.
  - **Hashes:** every `hash@` key equals the baseline in both modes, so no
    gate fixture's hash moved, Retail or Legion.
  - Every file exited 0, and none was skipped for missing data.
  - `legion_check.py validate` reports `PASS: 0 problems`.
  - No baseline was regenerated. The run took 3,680 s.
- **`ctest -L quick`: all passed.** Release passed **399/399** in 85 s, and
  optimized Debug **413/413** in 195 s. Both ran at `-j 10` on cores 4–23,
  while the Legion gate and the `-O0` Legion tests below held most of the
  machine (load average 12–15).
- **The touched short tests passed every run in every tree** (3 runs each, under
  the same load):

  | Tree | `voice_uplink` | `terrain_schedule` | `terrain_cache` |
  | --- | --- | --- | --- |
  | Release | 3/3 (49 s) | 3/3 (21–32 s) | 3/3 (16–19 s) |
  | Optimized Debug | 3/3 (49 s) | 3/3 (21 s) | 3/3 (16–17 s) |
  | Debug (`-O0`) | 3/3 (49 s) | 3/3 (59–66 s) | 3/3 (55–64 s) |
  | Clang Debug | 3/3 (49 s) | 3/3 (55–61 s) | 3/3 (60–63 s) |
  | ASan | 3/3 (49 s) | 3/3 (39–43 s) | 3/3 (27–32 s) |

  In the first round, `terrain_schedule` failed 3 of 3 runs alone at `-O0`.
  `voice_uplink`'s abort depends on timing: it hit 3 of 3 runs in the first
  round, but 0 of 7 runs of the old binary while the fix was prepared. So
  passing is not proof by itself. The fix (`buf.data() + off + 5`) removes
  the out-of-range `operator[]` that the GCC 16 assertion caught, and the code
  shows that directly. ASan reported nothing (no AddressSanitizer,
  LeakSanitizer or runtime-error line).
- **The four Legion tests pass in the unoptimized trees under their new limits.**
  `TAK_SLOW_TEST_BUILD` selects these limits in the Debug, Clang Debug and ASan
  trees. Release and optimized Debug keep the old ones (900/1800/600/900 s), as
  `ctest --show-only` confirms. Each tree ran the four at `-j 4`, on 4 (Debug),
  6 (Clang Debug) and 10 (ASan) cores shared with the gate and the other trees:

  | Test (new limit) | Debug | Clang Debug | ASan |
  | --- | ---: | ---: | ---: |
  | `legion_sched_inflight` (1800 s) | 624 s | 593 s | 439 s |
  | `legion_scale_neutral` (1800 s) | 668 s | 614 s | 459 s |
  | `legion_route_paused` (3600 s) | 1,365 s | 1,265 s | 646 s |
  | `legion_parallel` (5400 s) | 2,232 s | 2,100 s | 1,379 s |

  The largest use of a limit is `legion_parallel` in Debug, at 41% of 5400 s.
  The first round's limits (900/1800/600/900 s) would again have failed three of
  the four (all but `legion_sched_inflight`) in both GCC and Clang Debug.
- **Load-sensitive tests not touched by the fixes.** `voice`, `replay_always`
  (ASan) and `crusades_history_ui_network` (Clang Debug) fail only under load
  (first round, below). They are not in the quick label, were not changed, and
  were not rerun. They remain known load-sensitive tests.
- **The settings-leak guard held.** `pref_guard_setup`/`pref_guard_check`
  passed in every selection. The real profile's `settings.ini` has the same
  SHA-256 and mtime before and after (`real-profile-{before,after}.txt`).
  `TAK_SETTINGS_FILE` was not exported.
- **Known limit recorded: Legion `mixed-10000` with flyers.** This is the change
  `6a5fa106` reports for its mixed 10,000-unit benchmark. The formation flyers
  now hold station over a ground that crawls, and never land. Ground arrivals
  rose from 2,265 to 2,335. The mean tick rose from 56.8 to 74.1 ms. This is the
  intended behaviour (flyers keep their rows over the front) at a CPU cost in
  that benchmark only. It is listed as a known limit for 0.8.4, not a
  regression to fix before the tag. This round did not re-measure it.

Logs: `~/tak-tmp/scratch/release-084/rerun277/`:

- builds: `make-*.log`, `build-status.txt`;
- `--mpai`: `mpai-*.log`, `mpai-results.txt`;
- `check-determinism.log`, `check-detmath.log`;
- the Legion gate: `gate/` (`scenarios.jsonl`, `check.txt`, `validate.txt`,
  `status.txt`, `scn/`);
- the quick suites: `ctest-quick-*.log`, `ctest-status.txt`;
- the touched tests: `touched-*.log` and `tmp/touched-*.log`;
- `sha256.txt`, `real-profile-{before,after}.txt`.

### First round: `f11fbc1a` (protocol 276)

All checks ran on the clean detached worktree at
`f11fbc1a278c7f2b21cd060609ac8a57a2dd0ef6` (protocol **276**, replay format 13).
That commit is `origin/main` after the rendering merge, and its Determinism,
Linux, Windows and macOS workflows passed (runs 39140724401, 39140724358,
39140724368 and 39140724402). The version bump lives on the docs branch, so these
binaries still call themselves 0.8.3. The owned retail install served as `--data`
and `TAK_TEST_DATA`. Toolchains were GCC 16.2.1, Clang 22.1.8, CMake 4.3.0, Ninja
1.13.2 and Python 3.14.8. Builds and tests ran on cores 4–23. The remote sweep's
local clients ran alongside the suites, as in 0.8.3.

**First-round result: every release-relevant check passed: builds, Release and
optimized Debug suites (after reruns), every Legion scenario gate, determinism,
all four `--mpai` hashes, the research suites and both remote sweeps.** The failures are all in
tests that are new since 0.8.3 or that the rendering program rewrote. None is a
sanitizer report, a desync or a wrong simulation result. They fall into three
groups:

1. **A real defect in a new test (`voice_uplink`), caught only at `-O0`.** It
   aborts every time in Debug and Clang Debug and passes in every optimized tree.
   It is a test bug, not an engine bug; details follow. **Fixed by `8cf4c93c`**
   (second round).
2. **Wall-clock-sensitive new tests that fail under load or at `-O0`:**
   `terrain_schedule`, `terrain_cache` and `voice`, plus `replay_always` under
   ASan and `crusades_history_ui_network` in Clang Debug. Each one passed when
   rerun alone on reserved cores (4–7, with at most three other tests running
   elsewhere on the machine), in Release and optimized Debug. The exception is
   `terrain_schedule`, which fails every time at `-O0`. **`terrain_schedule`
   and `terrain_cache` were made load-independent by `bf587967`** (second round);
   `voice`, `replay_always` and `crusades_history_ui_network` are unchanged and
   remain known load-sensitive tests.
3. **Timeouts of new Legion 10,000 tests in the `-O0` and ASan trees:**
   `legion_scale_neutral` (600 s limit), `legion_route_paused` (900 s) and
   `legion_parallel` (1800 s). All three pass in Release and optimized Debug.
   **Their unoptimized-build limits were raised by `0fe6305d`** (second round).

Details follow.

- **No multiplayer or replay compatibility with 0.8.3 or earlier.** `kNetVersion`
  is **276** in `src/net/protocol.h` at `f11fbc1a` (266 in 0.8.3). Every `takserver`
  announced `protocol v276`. The replay format stays **13**. Because the replay
  loader requires protocol == `kNetVersion`, 0.8.3 replays still do not play.
- **Builds.** Release, Debug, optimized Debug (`-O2 -g`), Clang Debug and
  AddressSanitizer all-target builds completed with exit code 0: 869 steps in
  Release and 870 in the Debug-type trees (0.8.3: 652/653). These were fresh
  trees, with ccache on the GCC builds. They used the static-deps prefix
  (`third_party/static-deps`) and the static Bink/streaming FFmpeg prefix. Every
  client and server reports `0.8.3 (build v0.8.3-226-gf11fbc1a278c)`, with no
  `-dirty` suffix.
- **Full CTest.** There are now **678** registered tests (640 in Release), up from
  605/585. 164 carry the `nightly` label, the same as 0.8.3. None was removed. The
  **73 new tests**, none of them nightly, are:
  - **Rendering program** (34): `animated_atlas`, `cpu_budget`,
    `geometry_owners`, `geometry_stages`, `native_pass`, `palette_indexed`,
    `palette_sheets`, `render_atlas_verify`, `render_benchmark_tool`,
    `render_body_arena`, `render_body_arena_fail`, `render_body_arena_still`,
    `render_cpu_budget`, `render_distant_lookup`, `render_geometry_ownership`,
    `render_geometry_stages`, `render_native_pass`, `render_palette_indexed`,
    `render_schedule`, `render_shadow_atlas`, `render_shadow_atlas_band`,
    `render_shadow_atlas_zoom`, `render_shadow_cache`, `render_telemetry`,
    `render_unit_index`, `render_unit_index_jump`, `shadow_atlas`,
    `shadow_tiles`, `terrain_compositor`, `terrain_schedule`, `terrain_work`,
    `unit_index`, `vertex_arena` and `work_schedule`.
  - **Legion 10,000** (29): `legion_arrival`, `legion_arrival_press`,
    `legion_chainplane`, `legion_chainresolve`, `legion_chains`,
    `legion_chainschurn`, `legion_choke`, `legion_entrances`,
    `legion_hier_test`, `legion_improvedefer`, `legion_parallel`,
    `legion_planepin`, `legion_route_paused`, `legion_route_test`,
    `legion_scale_neutral`, `legion_scale_one100`, `legion_sched_inflight`,
    `legion_sched_load`, `legion_sched_load_div`, `legion_sched_mix`,
    `legion_sched_pin`, `legion_sharedarea`, `legion_traffic`,
    `legion_trafficlane`, `legion_verify_battle-assault`,
    `legion_verify_crosslong`, `legion_verify_gap8`, `navbudget_test`,
    `selection_order`.
  - **Voice chat** (6): `voice`, `voice_client`, `voice_uplink`,
    `server_voice`, `server_voice_slow`, `server_voice_cpu`.
  - **Replays and orders** (4): `replay_writer`, `replay_always`,
    `server_orders`, `server_orders_network`.

  Results by tree:

  | Tree | Selection | Result | Failures |
  | --- | --- | --- | --- |
  | Release | full, 640 | 637/640 | `voice`, `terrain_schedule`, `terrain_cache` (load; pass on rerun, below) |
  | Optimized Debug | full, 678 | 676/678 | `terrain_schedule`, `terrain_cache` (load; pass on rerun) |
  | Debug | `-LE nightly`, 516 | 509/516 | `voice_uplink` (assertion), `voice`, `terrain_schedule`, `terrain_cache`, timeouts `legion_scale_neutral`, `legion_route_paused`, `legion_parallel` |
  | Clang Debug | `-LE nightly`, 516 | 509/516 | `voice_uplink` (assertion), `crusades_history_ui_network`, `terrain_schedule`, `terrain_cache`, timeouts `legion_scale_neutral`, `legion_route_paused`, `legion_parallel` |
  | ASan (focused) | 270 | 263/270 | `replay_always`, `voice`, `terrain_schedule`, `terrain_cache`, timeouts `legion_scale_neutral`, `legion_route_paused`, `legion_parallel`; no sanitizer report |

  The suites took 1,946 s (Release), 2,043 s (optimized Debug), 2,209 s (Debug),
  2,347 s (Clang Debug) and 3,409 s (ASan). They ran in parallel with each other
  and with the remote sweep's clients. No test reported a skip (exit code 77) in
  any tree.

  - **`voice_uplink` (new; a real defect in the test, not the engine).** In Debug
    and Clang Debug it aborts after 0.6 s on every run (3 of 3 alone):
    `stl_vector.h:1253 ... operator[] ... Assertion '__n < this->size()' failed`.
    The backtrace points at the fake server's reader thread,
    `tools/voice_uplink_test.cpp:225`:
    `const uint8_t* p = &buf[off + 5];`. For a one-byte message (`Ping`, whose
    length is 1), `off + 5 == buf.size()`, so the code takes the address of
    `operator[](size())`. The pointer is never dereferenced for a Ping, so
    optimized builds pass. However, GCC 16's libstdc++ enables
    `_GLIBCXX_ASSERTIONS` at `-O0` by default, and the assertion fires there.
    Fixed in `8cf4c93c` as suggested, `buf.data() + off + 5`. The test passes in Release, optimized
    Debug and ASan (Clang `-O1`, no default assertions). (While preparing the
    fix, the old binary did not abort in 7 further `-O0` runs, so the abort
    depends on a bare Ping landing at the very end of the buffer.)
  - **`terrain_schedule` (new) and `terrain_cache` (existing, rewritten by the
    rendering program): load and `-O0` sensitive.** Their checks depend on wall
    time: background chunk baking, and a pan that counts as "fast" only while each
    frame takes less than 0.9 s. In the loaded suites they failed with
    `FAIL: a fast pan queues no new work`, `FAIL: chunk-less first frame is drawn
    completely from sections` and `FAIL: a jump draws completely from re-uploaded
    sections`. Three reruns of the three tests under the same load failed
    `terrain_cache` once. Five Release reruns on cores 4–7, while the Debug and
    Clang suites still ran on 8–23, failed `terrain_schedule` once. Reruns on
    cores 4–7 with at most three other tests on the machine: **Release 10/10
    and optimized Debug 5/5 passed both tests.** At `-O0` they keep failing alone:
    `terrain_schedule` failed 3/3 in Debug (`a fast pan queues no new work`, then
    `gateall: the same pan is gated`) and 3/3 in Clang Debug (`gateall: ...`),
    while `terrain_cache` passed 3/3 in both. So `terrain_schedule` cannot pass an
    unoptimized build on this machine. Shipped builds are optimized. Both now
    run on simulated clocks (`bf587967`) and passed every second-round run.
  - **`voice` (new): load sensitive.** The one failing check is
    `another player's VOLUME leaves this talker alone`. The measured value is
    always 0.510864 (0.514551 under ASan): the reference `full` playback, which
    goes through SDL's disk audio driver in real time, comes out low when the
    machine is busy. It failed in the loaded Release, Debug and ASan suites and
    in 1 of 3 loaded reruns. It passed 10/10 on cores 4–7 in Release, 5/5 in optimized
    Debug, 3/3 in Clang Debug and 2 of 3 in Debug. The Debug failure was the
    first run, while the `-O0` timing runs below held cores 8–23.
  - **`replay_always` under ASan (new): load.** Its `mission-crash` case failed in
    the ASan suite with the client at `tick=0 ... end=timelimit`: the ASan client
    did not start the mission inside the case's 60 s while eight cases and four
    other suites shared the machine. The other seven cases passed. Rerun alone
    under ASan, it passed 2/2 (about 41 s each). It passed in Debug, Clang Debug
    and optimized Debug.
  - **`crusades_history_ui_network` in Clang Debug (existing; passed in 0.8.3):
    load.** A `takclient --shot` step timed out after 60 s while the Clang suite
    started. Rerun alone, it passed 3/3 (about 69 s each). It passed in every
    other tree.
  - **Legion 10,000 timeouts at `-O0` and under ASan (all new tests).**
    `legion_scale_neutral` (600 s limit) took 265 s in Release and 189 s in
    optimized Debug. It timed out in the parallel Debug, Clang Debug and ASan
    runs, and passed alone in Debug in **486.3 s** (81% of its limit).
    `legion_route_paused` (900 s) took 43 s / 197 s; alone in Debug (cores
    20–23) it **timed out again at 900 s**. `legion_parallel` (1800 s) took
    749 s / 760 s; alone in Debug (cores 8–15) it **timed out again at 1800 s**.
    So those two cannot pass an `-O0` tree with their current limits whatever the
    load (`ctest --timeout` does not override a test's own TIMEOUT, so their full
    `-O0` time was not measured). Close to their limits in the `-O0` trees, but
    passing, were `legion_sched_inflight` (862 s of 900 in Clang Debug, 634 s in
    Debug), `legion_choke` (729 s of 1200) and `legion_chainplane` (613 s). As
    with `movement_orders_trolls` in 0.8.3 (`8682bd3e`), this is limit headroom
    in unoptimized trees, not a hang: each passes in both optimized trees. A
    follow-up could raise the `-O0` limits, or label `legion_parallel` and
    `legion_route_paused` `nightly` (the Debug suites skip that label).
    `0fe6305d` raised them (second round).
  - **The settings-leak guard held.** `pref_guard_setup`/`pref_guard_check`
    passed in all five trees; `pref_guard_lint` passed everywhere, and
    `pref_guard_selftest` in the Debug-type trees. The real profile's
    `settings.ini` has the same SHA-256 and mtime before and after
    (`real-profile-{before,after}.txt`). `TAK_SETTINGS_FILE` was not exported.
  - **`credstore` ran for real.** It round-tripped through the Secret Service
    under the test-only service "TAK Engine Test" (`credstore_test: round trip
    ok`). No real stream key was involved.
  - **Legion scenario gate: all 67 `legion_check_*` gates passed** in Release
    and optimized Debug against `tools/scenarios/baseline.json`, at the protocol
    268–272 baselines that the Legion 10,000 stages retook (`44d08559` to
    `8c2bb90b`). `legion_check.py validate --baseline
    tools/scenarios/baseline.json` reports `PASS: 0 problems`. No baseline was
    regenerated.
  - **Battle cost and naval tests passed** in Release and optimized Debug:
    `legion_cost`, `legion_scenario_battle-field-2x500`,
    `legion_scenario_battle-assault`, `naval_ai` and
    `legion_acceptance_crowdheld_legion`. There was no ratchet failure.
  - **Voice tests.** These passed in all four trees where they are registered
    (ASan ran `voice_client` and the three `server_voice*` tests, which passed):
    - `voice_client`, `server_voice`, `server_voice_slow`, `server_voice_cpu` and
      `conn`;
    - `diplomacy_ui`, which is Debug-only.

    `voice_uplink` and `voice` are as described above. `voice` and `voice_uplink`
    use SDL's disk audio driver and a local fake server, so no real microphone or
    speaker was involved.
  - **Replay tests.** `replay_writer` passed in all five trees. `replay_always`
    (Debug-only) passed all eight cases in Debug, Clang Debug and optimized
    Debug; this includes `mission` and `mission-crash`, the campaign mission
    cases. Under ASan it passed on rerun, as described above.
  - **Slowest tests.** In Release and optimized Debug, `legion_check_pocket-615-open`
    (1148.5 s / 1183.3 s), `legion_check_pocket-615-wall` (1140.4 s / 1179.3 s)
    and `legion_check_tail-corner380` (1069.4 s / 1156.4 s) are the slowest, all
    under their 1800 s limit. `movement_orders_trolls` took 266 s / 330 s in
    Debug / Clang Debug (limit 600 s since `8682bd3e`).
- **Focused AddressSanitizer/leak run: 263/270, with no AddressSanitizer,
  LeakSanitizer or runtime-error report.** The selection is 0.8.3's 195 names
  plus the 73 new tests, and the two `pref_guard` fixtures. It ran with
  `bash tools/run-ctest-selection.sh`, `SDL_SHUTDOWN_DBUS_ON_QUIT=1`, dummy SDL
  video/audio, `ASAN_OPTIONS=detect_leaks=1`, no suppressions, cores 4–7 and
  `CTEST_PARALLEL_LEVEL=5`, and took 3,409 s. The seven failures are the load
  and timeout cases above.
- **Retail group emulation check (formation-air).** `python3 -B
  tools/re/check_retail_group.py` (400 cases) passed every section: tick 400,
  query 4000, check 2000, formation 2000, vtolcheck 2000 and vtolform 2000.
  Every outcome count is identical to 0.8.3 (593 s).
- **Determinism.** In `tools/check-determinism.sh`, the GCC and Clang x86-64
  builds at O0/O2/O3 all agree on `dcef618cd2e4d558`, the same as 0.8.3. The
  aarch64 legs are skipped because target headers are missing.
  `tools/check-detmath.sh` passes, including the `std::atomic_ref` guard.
- **`--mpai` golden hashes: all four equal the expected values.** Each ran once,
  on the optimized Debug build, Inner Circle, `takserver --local --no-auth --seed 1`,
  an empty `XDG_DATA_HOME`, and `TAK_LEGION=1` for Legion. The four runs were
  concurrent on separate core pairs. No client or server log has a desync or
  referee-suspect line.

  | Mode | Duration | Hash | Expected (protocol 276) | 0.8.3 |
  | --- | ---: | --- | --- | --- |
  | Legion | 300 s | `ce5c4ba7562e53b9` | `ce5c4ba7562e53b9` | `c707e1a8063bc0c7` |
  | Retail | 300 s | `a3d953803a064191` | `a3d953803a064191` | `7535cb08ee787b75` |
  | Legion | 60 s | `0825aeb9e8139203` | `0825aeb9e8139203` | `532539f51d70840b` |
  | Retail | 60 s | `b353410003e98dd5` | `b353410003e98dd5` | `2c39430636a3dbda` |

  So the rendering merge left the simulation where `af90da79` put it. Optimized
  Debug binary SHA-256:
  - client `ecc9e02efc3fcaf92d5bb52c68c744b163d1ceb410ecbd105c3129ff1e07ef27`
  - server `e5d63d0584b6eef6e741266a9a29147372caebcf63e2d1129bac16141710cfcc`
- **Python research suite.** **165 tests** passed, with one skip: the optional
  corpus test, which needs `TAK_AUTH_RETAIL_DIR`. It ran after the Release
  build. The owned-retail archive verifier (`TAK_AUTH_RETAIL_DIR`) passed all
  **6 tests**, including the corpus and in-memory negative checks. The SHA-256
  sums of the 16 retail HPI archives and `Kingdoms.key` are unchanged by the runs
  and identical to 0.8.3's.
- **Syntax checks.** All **462** tracked Python files compile (446 in 0.8.3).
  All 21 tracked `*.sh` scripts and `tools/tak-netem` pass `bash -n`.
- **Notes (informational).**
  - The Release build emits **852** warnings (725 in 0.8.3). Most of the
    increase is from existing kinds:
    - +99 `-Wmissing-field-initializers` for `OptionsScreen::Control` in
      `options.cpp` (the voice chat page);
    - +26 `-Wdeprecated-declarations` for `std::filesystem::u8path`, from
      `replay_writer_test.cpp`, `replaywriter.cpp`, `mainmenu.cpp`,
      `server.cpp`, `datafolder*.h`.

    Two kinds are new, one site each and both benign (both fixed by
    `124374db`, second round):
    - `-Wswitch` at `src/sim/matchsetup.cpp:102`: `Cmd::Order` not handled in
      the switch, but it returns earlier at line 87.
    - `-Wunused-variable` at `src/client/gameview_render.cpp:280`: `indexed` is
      used only under `#ifndef NDEBUG`.
  - `tools/run-ctest-selection.sh` is still mode 100644 (run it with `bash`).

First-round logs: `~/tak-tmp/scratch/release-084/local/` (`cfg-*.log`, `make-*.log`,
`build-status.txt`, `versions.txt`, `toolchains.txt`, `ctest-*.log`,
`ctest-status.txt`, `ctest-build-rerun-loaded.log`, `flaky-*.log`,
`ctest-asan-replay-always-rerun.log`, `ctest-clang-crusades-history-rerun.log`,
`o0-legion_*.log`, `voice_uplink-dbg-bt.txt`, `credstore-verbose.log`,
`asan-list.txt`, `tests-new.txt`, `mpai-*.log`, `mpai-results.txt`,
`o2-sha256.txt`, `check-determinism.log`, `check-detmath.log`,
`check_retail_group.log`, `legion-validate.log`, `python-suite.log`,
`verifier.log`, `pycompile.log`, `bashn.log`, `warn-084.txt`,
`retail-sha-{before,after}.txt`, `real-profile-{before,after}.txt`).
## Remote multiplayer sweep

The full 37-scenario table passed for both pathfinding modes, Retail and Legion,
on **2026-10-18**. The two modes ran on different commits:

- **Retail** ran on `f11fbc1a` (protocol 276), 15:36–15:56 UTC.
- **Legion** ran first on `f11fbc1a` (15:56–16:25 UTC; also a clean pass), and
  then **again on `0fe6305d`** (protocol 277), 20:05–20:31 UTC.

The Legion rerun was needed because the flyer-front fix (`6a5fa106`, protocol 277)
changes hashed Legion state. The Legion figures below are from that rerun. The
Retail run stands, because Retail is bit-identical between the two commits:

- `6a5fa106` changes only Legion's navigator: `LegionNavigator::advancing` in
  `src/sim/legion.{h,cpp}`, plus the protocol number and two tests.
- The four fix commits change only:
  - tests and CMake test timeouts;
  - an explicit no-op `case Cmd::Order: break;` after a switch that already
    returned for that command;
  - a `[[maybe_unused]]` in the client.
- On `0fe6305d`, the Retail `--mpai` hashes equal `f11fbc1a`'s. So do the
  scenario gate's Retail `hash@` keys: all 135 files, `eq` rule, 0 failed.

Referees ran on `tak.pgnet.us` (24 runs, 10 at a time) and `vpn3.pgnet.us` (13
runs, 6 at a time), with optimized local clients. Each scenario allowed ten
simulated minutes (18,000 ticks) or ended at an actual result for that seat. Real
seated players provide the client/referee checksum coverage. The two all-AI
spectator scenarios per mode test flow control, not hashes.

The harness picks the mode through the hosting client's room options:
`TAK_LEGION=1` creates a Legion room, and without it the room is Retail. Every
recorded replay header confirms the intended mode and replay format 13:

- **Retail:** 88 headers at protocol 276 (`f11fbc1a`).
- **Legion:** 88 headers at protocol 277 (`0fe6305d`).

Each set is 51 seat replays and 37 referee replays. The Benchmark spectator
(`w-allai-bench`) records no client replay, by design (`beginGameRecording`). The
headers carry engine version 0.8.3 because both commits predate the version bump.

| Mode | Commit | Completed scenarios | Completed seats | Client-pair checkpoints | Client–referee checkpoints |
| --- | --- | ---: | ---: | ---: | ---: |
| Retail | `f11fbc1a` | 37/37 | 52/52 | 10,041 across 22 seat pairs, 0 mismatched | 2,625 across 50 seat/referee comparisons, 0 mismatched |
| Legion | `0fe6305d` | 37/37 (single pass, no retries) | 52/52 | 10,209 across 22 seat pairs, 0 mismatched | 2,694 across 50 seat/referee comparisons, 0 mismatched |

(The superseded Legion pass on `f11fbc1a` also completed 37/37 and 52/52, with
10,244 pair and 2,722 client–referee checkpoints and 0 mismatches.)

Each run passed all three `--validate` gates before its table started:

- **Build identity:** Retail `both at v0.8.3-226-gf11fbc1a278c`; Legion `both at
  v0.8.3-231-g0fe6305d701a`.
- **Planted desync:** the referee reported `game 1: client 1 DESYNCED at tick 900`.
- **Override transfer:** `verified packs transferred and incorrect loaded gameplay
  rejected`.

The gate checks only `tak.pgnet.us`. So after each deployment the binary on
`vpn3.pgnet.us` was checked by hand: it had the same SHA-256 and the same
`--version` build id. No natural `DESYNCED`, `REFEREE SUSPECT` or
client-reported desync appeared in any case log. The only such lines are the
planted ones in the `validate.*.log` files.

The negative-gate client log again contains `OVERRIDE PACKS VERIFIED; SAVE
FAILED: ... Not a directory [.../override-gate/peer/OverrideCache]` before
`PASS`. This is inherited: the same line is in the 0.8.2 and 0.8.3 sweep logs.
The gate checks rejection, not the save.

The checkpoint counts compare the per-tick hash records in the seats' `.takrep`
files (every 30 ticks). The client–referee column compares the seats with
referee replays (every 300 ticks). Each checkpoint carries a position digest as
well as the state hash, and the script compares both. Every checkpoint carried a
digest:

- **Referee checkpoints:** 2,056 in Retail, 2,117 in Legion.
- **Seated checkpoints:** 26,184 in Retail, 26,896 in Legion.

Before the counts were trusted, the comparison script was re-checked against
copies of one game's replays (`2h-baseline`): Retail from `f11fbc1a`, and again
Legion from the `0fe6305d` rerun. The check flipped one hash bit in the host's
checkpoint at tick 9,000, then (in a fresh copy) one digest bit. Each time it
reported exactly one mismatch, at tick 9,000, between the two seats and between
the edited seat and the referee (`fliptest.txt` in each log directory).

Referee replays were matched to their cases by the seed in the replay header
(seed = 2000 + scenario index), not by port. The port was also checked, and it
agreed for every replay. **Every game wrote a referee replay in both runs: 37 per
mode, 35 of them seated.** (0.8.3 got 27 Retail and 21 Legion.) This fits the
replays now being streamed while playing: the harness still stops each referee as
soon as its clients exit.

**No Legion retries.** As in 0.8.1–0.8.3, the Legion run used
`TAK_WALL_TIMEOUT=3600` from the start, with the same seeds and the same
simulated-time limit. Every Legion seat completed in the single pass (26 minutes,
including the gates):

| Legion case (`0fe6305d`) | End | Tick | Units |
| --- | --- | ---: | ---: |
| `h-stress` | concluded | 4,954 | 7,753 |
| `2h-stress` | concluded | 5,654 | 8,268 |
| `2h-orders-stress` | concluded | 4,981 | 8,565 |

(On `f11fbc1a`: 4,951, 5,654 and 6,049. In 0.8.3, `2h-orders-stress` ran to
18,000 ticks.) The Retail run used the default limit, and every case finished
inside it (20 minutes, including the gates):

| Retail case (`f11fbc1a`) | End | Tick | Units |
| --- | --- | ---: | ---: |
| `h-stress` | concluded | 5,240 | 8,250 |
| `2h-stress` | concluded | 4,851 | 9,174 |
| `2h-orders-stress` | concluded | 4,728 | 9,187 |
| `h-lake` | concluded | 4,548 | 4,685 |

**Reproducibility across releases.** The simulation changed in both modes since
`v0.8.3`:

- protocols 267–272 (Legion 10,000) and 277 change Legion;
- 273 and 274 change both modes.

So no seated case is expected to repeat a 0.8.3 hash, and none does. All 37
Legion cases and 36 of 37 Retail cases ended with a different tick or hash. In
every case the seats and the referee agreed. The one exact repeat is Retail
`w-allai-stress`, the all-AI stress spectator game: tick 18,000, hash
`e10d25236d48c058`, 5,622 units, the same as 0.8.3. Shared-mana construction
(273) and the lodestone change (274) evidently never trigger in that game.

**Legion before and after 277.** Comparing the Legion rerun with the Legion pass
on `f11fbc1a`, 28 of 37 cases ended at the same tick and hash
(`legion277-vs-f11.txt`). The other 9 split into two groups:

- **Six are `TAK_AUTOPLAY` runs:** `2h-orders`, `2h-orders-stress`, `4h-orders`,
  `net-loss3-orders`, `net-orders100` and `net-2h-rtt100`. These issue live
  orders, which the server buckets by arrival tick, so they never repeat a hash
  from run to run.
- **Three are reproducible cases that 277 changed:** `h-stress`, `h-lake` and
  `4h-gods`. That is expected on big maps: 277 changes which formation flyers
  hold station there while their ground waits to register (Ulasem Arena, Lake
  Lokken).

In each of the nine, the seats and the referee agreed.

**Retail `h-stress` (seed 2003).** The `f11fbc1a` run concluded at tick
**5,240** with hash `1fe1eb4e3754ca93` and 8,250 units. The seat matched the
referee at 18 of 18 shared checkpoints. 0.8.3's reference (tick 4,789,
`8e7c0acbf9752f10`) no longer applies, because 273/274 change Retail. The new
value is the reference for the next sweep. It holds for `0fe6305d` too, since
Retail is bit-identical.

**`h-cramped` with Monarch Expendable (`5774601a`): known limitation.** The entry
now runs with `TAK_GODS=1 TAK_MONARCH_EXPENDABLE=1`. The client and referee
replay headers record Monarch Expendable as on (header byte 1), so the flag
reached the room. **In Retail the game still concludes early:** tick **1,307**,
hash `45134591344a4d04`, 10 units, with the seat matching the referee at 5 of 5
checkpoints. That is essentially 0.8.3's 1,303.

The reason is that a match spawns only the Monarch for each slot
(`src/sim/matchsetup.cpp`; extra units come only with stress). An idle human seat
therefore has the Monarch as its only unit. When the god storm kills it, the seat
is eliminated by the "no living units" rule (`sim.cpp`, `aliveByPlayer == 0`),
which Monarch Expendable does not affect. So `5774601a` does not achieve its
stated aim, "keeps the seat alive so the cramped four-player game with gods runs
its full length". The entry still completes deterministically, just briefly. It
is recorded as known for 0.8.4, and the harness was not changed. Making the
Retail entry run longer needs a seat with more than one unit, or a different map
or seed. That is a harness change for after the release.

Legion `h-cramped` ran to 18,000 ticks in both Legion runs: `bbbd4becad56c714`,
34 units, the same tick and hash before and after 277. On `0fe6305d` the seat
matched the referee at 60 of 60.

**Voice.** No case exercised voice chat. The sweep's headless clients never open
voice. Voice is presentation-only relay traffic, never in a tick bundle or a
replay, so it cannot affect the checksums compared here.

**Impaired networks.** All eight impaired scenarios ran under netem in every run:

- 50, 100 and 500 ms delay;
- 100 ms ± 30 ms jitter;
- 1% and 3% loss;
- live orders;
- two seated players over 100 ms.

For every case, the harness archived:

- the `__SETUP_OK__` marker;
- the `prio 1:` root with `netem 30:` on band 1:3;
- the u32 filter matching that case's exact game source port: Retail 7927–7934,
  Legion 8027–8034.

Each teardown log ends with only the interface's default qdisc (`fq_codel` on tak
`ens18`, `noqueue` on vpn3 `wg0`). No case recorded an unclean teardown
(`shaping-{retail,legion,legion277}.txt`).

**Isolation.** The referee was a separate test executable,
`~/takserver.sweep084`, using test ports 7890–7936 and 7990–8036 (the public
service uses 7677). It was deployed once for the `f11fbc1a` runs and again, as a
fresh build, for the `0fe6305d` rerun. It used per-mode map caches
`~/tak_cache.sweep084-{retail,legion}` and per-run replay directories
`~/tak_replay.sweep084-{retail,legion,legion277}`.

The public `/usr/bin/takserver` on tak.pgnet.us was not replaced, stopped or
restarted. It was PID 1035, port 7677, active since 2026-10-18 08:02:04 UTC,
before and after both sessions (`public-before.txt`, `cleanup-check-after.txt`).
No certificates were touched. Local clients were pinned to CPUs 4–23. `vpn3` runs
no takserver service.

**Cleanup.** After each session, on both hosts:

- No process was executing `takserver.sweep084`.
- No listener remained on the test ports.
- No `netem` or `prio` qdisc remained (`fq_codel` on tak `ens18`, `noqueue` on
  vpn3 `wg0`).
- No `/tmp/tak-netem-owner.*` claim file remained.
- Each Legion run left eight 1800-second netem watchdogs sleeping: five on tak
  (`net-rtt50`, `net-jitter`, `net-rtt500`, `net-loss3-orders`,
  `net-2h-rtt100`) and three on vpn3 (`net-rtt100`, `net-loss1`,
  `net-orders100`). The Retail ones had already expired. Their claim files were
  gone, so they would have exited without running `tc`. They were stopped by
  exact PID.
- The test binary, the sweep's map caches and replay directories, and the
  `/tmp/tak-{srv,val,neg}-*.log` files were removed after the referee replays had
  been copied locally.

Locally, no sweep `takclient` or harness process remained
(`cleanup-check-{before,after}.txt`).

**Binaries.** Each set was built from a clean tree, with optimized Debug
(`-O2 -g`) for both referee and client.

- **Referee:** Ubuntu 24.04 container, GCC 13.3, `TAK_BUILD_CLIENT=OFF`, max
  symbol version GLIBC_2.38. On each host the deployed SHA-256 matched the build
  and was checked after deployment.
- **Client:** Fedora, GCC 16.2.1, the same binary as the local `--mpai` runs of
  its commit.

| Binary | Retail run (`f11fbc1a`, `v0.8.3-226-gf11fbc1a278c`, protocol 276) | Legion run (`0fe6305d`, `v0.8.3-231-g0fe6305d701a`, protocol 277) |
| --- | --- | --- |
| Referee | `22496d434db79e0c1d24c63dd9133287008ee2c09d86e0afe5bb421f73e68eac` | `17e08d3cc6ec1bf12765a715f51475d4006f61d1618ec3ac47c00d978f5d6818` |
| Client | `ecc9e02efc3fcaf92d5bb52c68c744b163d1ceb410ecbd105c3129ff1e07ef27` | `e40b760b7757cd2dfb2d220cfeaf74629d38036f247260aa459a912ff70fb7c3` |
| `override_transfer_test` | `ae46399a043227e574a510bf2cfed93701f5905ec16629ac7ec6972f54b088a3` | `0e6830b6f5f5fb98c655464490b098015394d687f86388d71a1af51293c37b36` |

**Harness.** Every run used the committed `tools/desync-hunt-remote.sh`,
unmodified, from the clean detached worktree. Its SHA-256,
`0ec213448db4f15df1ada6bd5c4bf8ab8d1bbe719e0d11bf03776200e2ba365b`, is the same
at both commits. It differs from the file used for 0.7.25–0.8.3 (`02d27068…`)
only by `5774601a`, the `h-cramped` Monarch Expendable entry.

**Mixed versions.** No game mixing a 0.8.3 build with a 0.8.4 build was played.
None can be: 0.8.4 speaks protocol 277, the server's Hello rejects any client
whose `kNetVersion` differs, and the replay loader accepts a replay only when its
protocol equals `kNetVersion`.

Logs:

- **`f11fbc1a` runs:** `~/tak-tmp/scratch/release-084/remote/`:
  - `retail.sweep.log`, `legion.sweep.log`, `*.start`/`*.end`;
  - `deploy.txt`, `sha256.txt`, `public-before.txt`,
    `cleanup-check-{before,after}.txt`;
  - `{retail,legion}-checkpoints.json`, `{retail,legion}-mapref.txt`,
    `ref-{retail,legion}/`, `fliptest.txt`, `shaping-{retail,legion}.txt`,
    `{retail,legion}-vs-083.txt`.

  Case logs and seat replays: `~/tak-tmp/rr84/desync-remote-1869073` (Retail) and
  `~/tak-tmp/rr84/desync-remote-3095601` (Legion).
- **`0fe6305d` Legion rerun:** `~/tak-tmp/scratch/release-084/rerun277/`:
  - `legion277.sweep.log`, `legion277.{start,end}`;
  - `deploy.txt`, `sha256.txt`, `public-before.txt`,
    `cleanup-check-{before,after}.txt`;
  - `legion277-checkpoints.json`, `legion277-mapref.txt`, `ref-legion277/`,
    `fliptest.txt`, `shaping-legion277.txt`, `l277.txt`,
    `legion277-vs-f11.txt`.

  Case logs and seat replays: `~/tak-tmp/rr84/desync-remote-3558477`.

## CI and packages

The final source (`de8cd784`) passed the Determinism, Linux, Windows and macOS
workflows on `main` (runs 39167653985, 39167653927, 39167654128 and
39167654074). On the exact tag, Determinism, Linux and Windows passed on the
first attempt; macOS needed one rerun:

- [Determinism and core tests](https://github.com/pocketgeek/tak-engine/actions/runs/39170719631), started manually on the tag because the workflow is path-filtered.
- [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/39170713842): all native package jobs and release attachment.
- [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/39170713848): x64, ARM64 and the x64 signing/package job.
- [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/39170713884): on the first attempt the x64 job built and tested but failed its last step, uploading the debug CI artifact (`Failed to CreateArtifact: Unable to make request: ENOTFOUND`), so the **Sign + notarize** jobs were skipped. That is a network error on the hosted runner, not a build or test failure, so the failed job was rerun once. Attempt 2 passed both builds and the **Sign + notarize** job for each architecture (Apple accepted every submission; stapling and validation succeeded), which replaced the unsigned macOS ZIPs and DMGs in the draft with the signed, notarized and stapled ones.

The release was created as a draft with these notes and tagged CI attached the
packages. All **29 assets** (the same set as 0.8.3) were downloaded after the
macOS signing finished and matched GitHub's SHA-256 digests and byte sizes
before publication.

- **Build ids.** The client and server in every Linux package (Debian 12 and
  13, Ubuntu 22.04, 24.04 and 26.04, Fedora 44, openSUSE Leap 16.0, Arch,
  Slackware) carry `v0.8.4`. Run: the Debian 12 amd64, Fedora 44 x86-64 and
  Slackware packages report `takclient (TAK engine) 0.8.4 (build v0.8.4)`;
  the Arch `takserver` reports `0.8.4 (build v0.8.4)` (its `takclient` needs
  glibc 2.44, newer than the checking host, so only its build string was
  checked); the x64 Windows ZIP's `takclient.exe` reports `0.8.4 (build v0.8.4)`
  on Windows 11; on the arm64 MacBook (macOS 26.6.2) the release ZIPs'
  `takclient` reports `0.8.4 (build v0.8.4)` for both architectures.
- **Windows signatures.** `Get-AuthenticodeSignature` on oden-win reports
  **Valid** (CN=Curtis Edge) for the setup `.exe` and for `takclient.exe`,
  `takserver.exe`, `cartographer.exe` and `crusades_admin.exe` in both the x64
  and ARM64 ZIPs: 9 signed files. The debug ZIPs were not checked.
- **macOS notarization.** On the MacBook, both DMGs pass `xcrun stapler
  validate` and `spctl` (open, primary signature) as **accepted, source=Notarized
  Developer ID, origin=Developer ID Application: Curtis Edge (L479Y59RN8)**; so
  do both apps (the game and TAK Cartographer) from each DMG and from each ZIP.
  The bare command-line programs are Developer ID signed (TeamIdentifier
  L479Y59RN8); `spctl` says they do not seem to be an app, as expected for
  unstapled command-line tools.

New checks for 0.8.4:

- **Opus is static.** No packaged Linux `takclient` or `takserver` has a
  `NEEDED` entry beyond libc, libm and the loader (Slackware also lists libdl
  and libpthread), so none needs libopus; the Debian packages depend only on
  `libc6`. Neither Windows `takclient.exe` imports or names an Opus DLL (the x64
  import table lists only system DLLs), and neither macOS `takclient` names a
  libopus dylib. `OPUS-LICENSE.txt` is in every Debian, RPM, Arch and Slackware
  package and in all four release ZIPs.
- **macOS microphone.** The game app's `Info.plist` carries
  `NSMicrophoneUsageDescription` in both ZIPs, and `codesign -d --entitlements -`
  on the game app from both DMG and ZIP shows
  `com.apple.security.device.audio-input`; the entitlement is also embedded in
  the app's `takclient` and absent from `takserver` and `crusades_admin`.
- As in 0.8.1 to 0.8.3: no packaged Linux `takclient` has a `NEEDED` entry for
  libsecret, GLib, GIO or GObject.

## Screenshots

Recaptured on 2026-10-18 per [the capture notes](img/README.md), with an
optimized Debug build (`-O2`, the debug capture harnesses) of `657aef1b` (main
`f11fbc1a` plus these docs and the version bump) and the capture-only patch;
the two flyer pictures came later from a build of `0fe6305d` (below).
The retail data came from the user's install, read-only, through a scratch
folder with its own caches. All scratch profiles were used. Every new picture
was inspected.

- Interface: `title.jpg` (VERSION 0.8.4), `diplomacy.jpg` (VOICE and VOLUME
  columns, with live sliders at 50% and 25% for the two other humans and none
  for the computer player), and the new `guide/voice-chat.jpg`, now shown in
  the user guide's Voice Chat section. The guide no longer says that the
  diplomacy picture predates the Voice and Volume columns.
- Simulation-dependent: `battle.jpg`, `dragons.jpg`, `hud.jpg`, `naval.jpg`,
  `guide/benchmark.jpg`, `guide/scorecard.jpg`, `guide/tactical-dots.jpg` and
  `guide/fire-on-the-move.jpg`. Each is the 0.8.3 scene, with units moved a
  little; the captions were updated where a count changed. The HUD's stats panel
  now reads VID 94 MB (0.8.3 capture: 87 MB), and three of sixteen units
  have fallen in the fire picture (0.8.3: four).
- Rendering: the program draws the same picture as 0.8.3 apart from 1–14
  shadow-edge pixels a frame and the slower terrain fill after a long camera
  jump at 7680×2160 (the capture sizes are 1920×1080 and 1600×900). So no
  picture was retaken for the rendering alone, and none shows a difference.
- Not retaken: `lobby.jpg`, `lobby-scenario.jpg` and `room.jpg` (their screens
  did not change; Absurdly Defensive appears only in a slot's level list),
  `generated.jpg`, the menus and the editor pictures.
- **`flyers.jpg` and `guide/flyers-retail-legion.jpg` retaken after the
  flyer fix.** On the `657aef1b` build the Legion flyers of a formation flew
  ahead to the destination, as Retail's do, in the gallery scene (44 Zhon, one
  formation move, 22 s) and in the committed `TAK_FLYER_FRONT` fixture
  (`--testbuild`, 30 Swordsmen and 8 Gryphons, 14 s), while a `v0.8.3` build
  kept them over the front. Both were retaken with a build of `0fe6305d`
  (`6a5fa106`, protocol 277, plus the four test fixes) and the same capture
  patch and scenes, the same way. In the gallery picture and the Legion half
  the Gryphons, Harpies and Drakes ride over the leading Trolls and Stone
  Giants, close to the 0.8.3 pictures; in the Retail half they are far ahead,
  over the rocks. The `TAK_FLYER_FRONT` fixture on this build draws the same
  picture as on `v0.8.3`. The user guide's [Air units](user-guide.md#air-units)
  text and the picture caption match this behaviour unchanged.

## Limits

- No game mixing 0.8.3 and 0.8.4 builds was played, and none can be (protocol
  277 against 266, the Hello gate and the replay loader).
- Rendering was timed only with NVIDIA OpenGL on Linux. The native fast paths
  (vertices of unchanged units kept on the card, batched body passes, the
  palette-lookup building shading) are OpenGL-only; Direct3D, Metal, OpenGL ES
  and the software renderer keep 0.8.3's way for them. After a long camera
  jump at 7680x2160 the terrain fills in over more frames than before. Units
  that stand still keep about 21% more geometry in main memory, and the
  retained vertices take up to about 45 MiB more graphics memory on OpenGL.
- Voice chat was not exercised by the remote sweep; its coverage is the voice
  tests above and the user's own multiplayer sessions. Windows before 10
  version 1703 cannot send voice (no `SIO_TCP_INFO`), by design.
- On Windows, over a 1,500 B/s uplink, voice's worst `Progress` delay is about
  0.7 to 0.9 s. The `voice_uplink` fix (`8cf4c93c`) is shown by the code (no
  `&buf[size()]` is formed for a bare Ping), not by a before/after run: the
  abort needs timing and `-O0` and was not reproduced on demand after the fix.
- The Legion 10,000 program does not meet its own success criterion for
  congested workloads (the report's sections 13 and 14): crowds at corners and
  narrow passages, and the queue for many unrelated destinations. Three of the
  ten 10,000-unit Legion workloads run over the 33.3 ms tick budget:
  many-10000 and scatter-10000 in 13% and 2% of ticks (more bodies now move),
  and mixed-10000, whose overrun is inherited from its flyers' exploration
  phase outside Legion.
- **mixed-10000 under 277.** With the flyer fix, mixed-10000's flyers hover
  over a ground army that crawls, and none lands within the run's 19,400 ticks
  (444 landed on `f11fbc1a`); 2,335 units arrive instead of 2,265, and the mean
  tick rises from 56.8 to 74.1 ms (p99 113 to 150 ms; 19,394 of 19,400 ticks
  over budget, 19,242 before). A known limit of the 10,000-unit workload, not
  changed for 0.8.4.
- **Retail `h-cramped` ends at tick 1,307** despite Monarch Expendable, because
  the idle seat starts with only its Monarch (see the remote sweep). Known; the
  harness is unchanged for 0.8.4.
- Local visual inspection covers Linux OpenGL only; Windows and macOS packages
  are checked from CI and by signature, notarization and build-id checks.
