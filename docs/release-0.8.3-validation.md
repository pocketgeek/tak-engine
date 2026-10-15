# 0.8.3 release validation

Protocol **266** (0.8.2: 257), replay format **13** (0.8.2: 12), generator
version **8**, campaign payload **4**, database schema **9** (the last three
unchanged since 0.8.0).

Tag and publication: *Pending.*

## Scope and compatibility

0.8.3 follows 0.8.2 (protocol 257). After the 0.8.2 validation commit
`cd1bda91`, 103 commits up to `b5c45401` change the lobby, the simulation in both
pathfinding modes, the server-side AI, the Cartographer, the tests and the
documentation. `git diff --stat v0.8.2..b5c45401 -- src` touches 74 files:
`src/sim/` (15), `src/client/` (20), `src/cartographer/` (21), `src/net/` (8),
`src/ai/` (2), `src/util/` (3), `src/cob/` (2), `src/crt/` (1),
`src/server/server.cpp` and `src/server/crusades/replayfiles.cpp`. Nothing under
`src/tnt/` (the map generator) or `src/campaign/` changed. The protocol steps,
each recorded in `src/net/protocol.h` and `docs/multiplayer-design.md`:

- **258** ALLOW SPECTATORS (`e00637a7`, lobby wire only).
- **259** MAP SCENARIO (`55929d8c`, review fixes `d18fb416`, `352598df`,
  `d1f69041`); replay format 12 -> 13 records the option.
- **260** START LOCATIONS: BY SLOT (`2b7c0bbc`, `4ce08d7c`).
- **261** retail healtime self-repair (`0e438b70`, `5afe9538`), hashed.
- **262** a flyer held in range of its attack target stops its velocity
  (`915eff4d`).
- **263** air combat: every flyer hold, retail impact dispatch and 3D area
  damage, the facing request at the led SweetSpot, AirToGround attack runs
  (`77616793`, `3d2ef9a5`, `59549b12`, `b8ff8565`, `54d89402`).
- **264** the 2026-10-14 Legion review, Legion only (`f7d1b40c`, `b2ecc86e`,
  `a8ed05b3`, `c3dcb932`, `1861aff9`, `9987e4fd`, `e5df18b7`, `af879323`,
  `cecdd0e1`).
- **265** retail path-search fidelity, all modes: a pending re-request survives
  displacement, the ground word keeps its live holder, production entity pools
  (`889f1b60`, `cee6ac55`, `6fcde5ea`, `9bfd090d`).
- **266** entity pools start at retail's maxunits and grow as needed
  (`7b0d9182`, `9ab1fe4c`).

Without a protocol change: the AI series (`3539ba13` to `34425983`: deposit
reservations, allies' home deposits, the nearest-ally rule, the stall timeout,
the short-of-mana lodestone within retail's base radius, Normal's builder
growth, Zhon's producers, the full-store economy, the per-type and difficulty
caps), the client-only spectator health bars (`173e28f7`), death cries
(`5a193bf0`, `e7e20d5d`, `65162d4b`), lobby start markers (`783794dc`), the
Cartographer fixes, BLOCKED tool, scrollbars, edge scrolling, fullscreen and
data-folder setting, and the per-test scratch profiles (`d3468b6b`, `646313e9`,
`04f142b3`). The scenario baseline was retaken for 263 (`650c977e`), 264
(`44c77cbd`, `44263867`), 265 (`a1247035`) and 266 (`b5c45401`, the user's
"accept all" ruling of 2026-10-15); `legion_check check --cumulative` passes
at `b5c45401` (31,753 keys, 0 failed).

Unlike 0.8.2, **Retail is not bit-identical to the previous release**: 261,
262, 263, 265 and 266 change Retail games. The `--mpai` Inner Circle seed 1
hashes recorded by `9ab1fe4c` (build-o2, `takserver --local --no-auth --seed 1`,
scratch XDG directories) are Retail 300 s `7535cb08ee787b75`, 60 s
`2c39430636a3dbda`; Legion 300 s `c707e1a8063bc0c7`, 60 s `532539f51d70840b`
(0.8.2's Retail 300 s was `95b05c8290b55317`).

Every measured figure in the [release notes](release-0.8.3-notes.md) is taken
from the commit that made the change, with its setup, or from the section of
`docs/legion-pathfinding.md`, `docs/pathfinding-port.md`,
`docs/ai-expansion-2026-09-29.md` or `docs/retail-engine.md` it cites.

**Compatibility.** `kNetVersion` is **266** in `src/net/protocol.h` at
`b5c45401`, against 257 at `v0.8.2`. `kReplayFormat` is **13** in
`src/net/replayhdr.h` (12 at `v0.8.2`); the map generator's `formatVer` is 8
(`src/tnt/mapgen.h`, no change under `src/tnt/`), the campaign payload's
`kVersion` 4 (`src/net/crusades.h`, unchanged) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`, unchanged).

- **Multiplayer.** The server's Hello check (`src/server/server.cpp`) rejects a
  client whose protocol differs from `kNetVersion` ("protocol version
  mismatch"), as well as a different pure-retail gameplay-data hash. So **0.8.3
  cannot play with 0.8.2 or earlier in either direction**: 0.8.3 clients cannot
  join a 0.8.2 or older server, and older clients cannot join a 0.8.3 server.
  The server and all players must update together.
- **Replays.** `supportedReplayProtocol` (`src/net/replayhdr.h`) accepts a
  recording only when its protocol equals `kNetVersion`; the client's replay
  loader (`src/client/replayfile.cpp`), the campaign replay path
  (`src/net/client.cpp`) and the Crusades replay files
  (`src/server/crusades/replayfiles.cpp`, which also refuses a recording with
  MAP SCENARIO on) all use it. So 0.8.3 does not play 0.8.2 or older replays,
  and those builds do not play 0.8.3 replays (format 13 is also newer than they
  read). `tools/replay_test.cpp` pins `kNetVersion == 266`.
- **Maps and settings.** Generated-map recipes (generator 8) and saved `.kmp`
  maps carry over. A 0.8.2 settings file reads the same in 0.8.3. 0.8.3 adds
  `gameCreate.allowSpectators` and lets `gameCreate.randomStarts` hold 2 (By
  Slot); 0.8.2 reads only `1`, `true` or `on` as Random, so it reads a saved By
  Slot as Fixed. Cartographer's `preferences.txt` gains its display, edge
  scrolling, UI scale and `blocked` keys.

`kNetVersion` must be re-checked immediately before tagging (in 0.8.0 the
protocol moved during preparation).

## Local preparation checks

All checks ran on a clean detached worktree of `origin/main` at
`b5c45401929b61e7315ef4ef15c2d160605f2d96` (protocol **266**, replay format 13).
The version bump lives on the docs branch, so these binaries still call themselves
0.8.2. The owned retail install served as `--data` and `TAK_TEST_DATA`. Toolchains
were GCC 16.2.1, Clang 22.1.8, CMake 4.3.0, Ninja 1.13.2 and Python 3.14.8. Cores
0–3 stay reserved for timing work, so builds and tests ran on cores 4–23.

**Result: every check passed except one timeout in Clang Debug.**
`movement_orders_trolls` hit its 300 s limit in the parallel Clang Debug suite. Run
alone once, it passed in 237.7 s. This is a timing margin, not a hang or a wrong
result: it passed in every other tree, and the Retail `--mpai` hashes are the
expected ones. The fix raises its limit to 600 s, the same as
`legion_movement_orders_maze`. It ships in 0.8.3 as commit
`8682bd3e`. No sanitizer finding, desync or
stale baseline turned up, and no baseline branch was needed. The 104 commits since
`v0.8.2` include the Cartographer and data-folder work, the lobby options
(protocols 258–260), healtime (261), the Lokken held-flyer fix (262), air combat
(263), Legion review six (264), retail path-search fidelity (265), entity pools that
grow from retail's `maxunits` (266), and the AI economy and limit work. Details
follow.

- **No multiplayer or replay compatibility with 0.8.2 or earlier.** `kNetVersion` is
  now **266** (it was 257 in 0.8.2). The server's Hello gate
  (`src/server/server.cpp`) rejects any client whose protocol differs from its own
  ("protocol version mismatch"), so 0.8.3 clients cannot join 0.8.2 servers, and the
  reverse. `src/net/replayhdr.h` accepts a replay only when its protocol equals
  `kNetVersion` (`supportedReplayProtocol`), so 0.8.2 replays do not play in 0.8.3,
  and the reverse. The replay format moved from 12 to **13**, which records the
  room's MAP SCENARIO option. **Unlike 0.8.2, the Retail simulation is not
  bit-identical to the previous release.** Healtime, air combat, path-search
  fidelity and the entity pools all change it, so both Retail and Legion `--mpai`
  hashes moved.
- **Builds.** Release, Debug, optimized Debug (`-O2 -g`), Clang Debug and
  AddressSanitizer all-target builds completed with exit code 0: 652 steps in
  Release and 653 in the Debug-type trees (up from 599/600). These were fresh trees,
  with ccache on the GCC builds. They use the static-deps prefix
  (`third_party/static-deps`) and the static Bink/streaming FFmpeg prefix
  (`third_party/ffmpeg-bink`). Every client and server reports
  `takclient (TAK engine) 0.8.2 (build v0.8.2-104-gb5c45401929b)` and
  `takserver (TAK engine) 0.8.2 (build v0.8.2-104-gb5c45401929b)`. The build identity
  is clean, with no `-dirty` suffix. The server announces `protocol v266`.
- **Full CTest.** There are now 605 registered tests (585 in Release), up from 560.
  164 of them carry the `nightly` label (160 in 0.8.2). The 45 new tests are:
  - 20 Cartographer UI and placement tests: `cartographer_place_*` and
    `cartographer_feature_ops_*` per race, `cartographer_place_{slots,features,units,blocked}`,
    `cartographer_starts_ui`, `cartographer_browser_rescan`, `cartographer_ui_smoke`,
    `cartographer_{palette_scroll,display,datafolder}_ui`.
  - 9 Legion review and long-route tests: `legion_{entryplanes,farroom,handout,longroute,mixedsettle,passagelanes,progressclock}`
    and `legion_scenario_serplong` / `legion_scenario_serplong-control`.
  - Lobby, spectator and start tests: `start_positions`, `start_markers`,
    `scenario_info`, `server_map_scenario`, `server_spectators`, `spectator_client`
    and `spectator_health_bars`.
  - Death cries: `creon_death_sound` and `creon_death_sound_peers`.
  - Data folder and settings-leak guard: `datafolder`, `options_datafolder`,
    `client_restart` and `pref_guard_{setup,check,lint,selftest}`.

  The AI behaviour work extends the existing `ai_behavior` / `ai_behavior_data`, and
  the path-fidelity work extends `pathsearch` and `pathblock`. Results by tree:
  - **Release: 585/585 passed. Optimized Debug: 605/605 passed.** Both ran the full
    suite, nightly label included. The worktree had an `assets` link to the real
    `assets` directory, so `cobanim` and `gaitpace_shipped` ran for real.
  - **Debug and Clang Debug ran with `-LE nightly`**, as in 0.8.2. **Debug: 443/443
    passed. Clang Debug: 442/443.** The one failure was `movement_orders_trolls`
    (`***Timeout 300.03 sec`). One isolated rerun on cores 8–23 passed in **237.7 s**.
    In the same suites, Debug took 260.0 s, and in 0.8.2 Debug and Clang Debug took
    275.7 s and 275.3 s. The 0.8.2 report already flagged this headroom. The test
    fails only on time, never on a check, and only at `-O0`; at `-O2` it takes
    36–45 s. Fix: `8682bd3e` raises its limit to 600 s.
    `legion_movement_orders_trolls_maze` (900 s limit) took 274.4 s / 308.9 s in the
    same two suites.
  - **The settings-leak guard held.** `pref_guard_setup`/`pref_guard_check` bracket
    every suite, and they passed in all five trees: no test wrote a `TAKengine`
    folder into the real profile. `pref_guard_lint` passed, and so did
    `pref_guard_selftest`, which runs in the Debug-type trees.
  - **`credstore` ran for real**, not as skip code 77. It round-tripped through the
    session's Secret Service under the test-only service name "TAK Engine Test" and
    removed its entry. No real stream key was involved.
  - **All 67 `legion_check_*` scenario gates passed** in Release and optimized Debug
    against `tools/scenarios/baseline.json`, including the protocol-266 keys the user
    accepted (`b5c45401`). `legion_check.py validate --baseline
    tools/scenarios/baseline.json` reports 0 problems. No baseline was regenerated.
  - **Battle cost and naval tests passed.** `legion_cost`,
    `legion_scenario_battle-field-2x500`, `legion_scenario_battle-assault`,
    `naval_ai` and `legion_acceptance_crowdheld_legion` passed in Release and
    optimized Debug, with no ratchet failure against `tools/legion_cost_baseline.json`.
  - **Slowest tests.** In Release and optimized Debug: `legion_check_pocket-615-open`
    (1013.8 s / 892.3 s), `legion_check_pocket-615-wall` (1000.9 s / 871.9 s) and
    `legion_check_tail-corner380` (953.9 s / 867.6 s). All are under their 1800 s
    limit. In Debug and Clang Debug the slowest was `legion_w7_awarebig`
    (332.3 s / 372.8 s), which has no explicit limit. The new
    `legion_scenario_serplong` took about 125 s in both optimized trees.
- **Focused AddressSanitizer/leak run: 197/197 passed.** The selection is the 149
  tests from 0.8.2 plus 46 more, and the two `pref_guard` fixtures that ctest adds.
  The 46 are every test new since 0.8.2 except the two nightly serplong scenarios and
  the fixtures, plus `ai_behavior`, `ai_behavior_data`, `retail_script`, `retailgap`
  and `retail_construction`. Together they cover the Legion review, the
  path-fidelity search, AI behaviour, death cries, start markers and positions, the
  spectator and scenario options, the data folder and the Cartographer placement
  tools. The run used `bash tools/run-ctest-selection.sh` with
  `SDL_SHUTDOWN_DBUS_ON_QUIT=1`, dummy SDL video/audio, `ASAN_OPTIONS=detect_leaks=1`
  and no suppressions, and took 1158 s. There were no AddressSanitizer,
  LeakSanitizer or runtime-error reports.
- **Retail group emulation check (formation-air).** I ran
  `python3 -B tools/re/check_retail_group.py` (400 cases by default) against the
  emulated `KINGDOMS.icd`. It passed every section: tick 400, query 4000, check
  2000, formation 2000, vtolcheck 2000 and vtolform 2000. Every outcome count is
  identical to 0.8.2.
- **Determinism.** In `tools/check-determinism.sh`, the GCC and Clang x86-64 builds
  at O0/O2/O3 all agree on `dcef618cd2e4d558`, the same as 0.8.2. The aarch64 legs
  are skipped because target headers are missing; native ARM64 CI remains a separate
  gate. `tools/check-detmath.sh` passes, including the `std::atomic_ref` guard.
- **`--mpai` golden hashes.** Following the minimal-testing rule for this release,
  each run was made **once**. Every hash equals the expected value recorded with
  protocol 266. The client and server logs contain no desync or referee-suspect
  line. The runs used the optimized Debug build, Inner Circle,
  `takserver --local --no-auth --seed 1`, an empty `XDG_DATA_HOME`, and
  `TAK_LEGION=1` for the Legion runs. **Both modes moved from 0.8.2, as expected.**

  | Mode | Duration | Hash | Expected (protocol 266) | 0.8.2 |
  | --- | ---: | --- | --- | --- |
  | Legion | 300 s | `c707e1a8063bc0c7` | `c707e1a8063bc0c7` | `70e826bb0db70ced` |
  | Retail | 300 s | `7535cb08ee787b75` | `7535cb08ee787b75` | `95b05c8290b55317` |
  | Legion | 60 s | `532539f51d70840b` | `532539f51d70840b` | `55b0eec316e45071` |
  | Retail | 60 s | `2c39430636a3dbda` | `2c39430636a3dbda` | `84fdef37bbb3606e` |

  Optimized Debug binary SHA-256:
  - client `895b4910e8dae50491c1c81e5b0d17d00ab2943f68313340e814002ed3879820`
  - server `68025b039e8ab116c29e81b85c21addca80f3dc12644c553fc8c5dfadd6bfd42`
- **Python research suite.** **165 tests** passed, with one skip: the optional corpus
  test, which runs only when `TAK_AUTH_RETAIL_DIR` is set. The new
  `tools/re/check_*.py` emulation checks (air attack run, ground owner, self-repair,
  splash units, start positions) are standalone scripts, not suite members, so the
  count is unchanged. The suite ran after the Release build, so the `build/hpitool`
  tests ran too. The separate owned-retail archive verifier (`TAK_AUTH_RETAIL_DIR`)
  passed all **6 tests**, including the corpus and in-memory negative checks. I took
  SHA-256 sums of the 16 retail HPI archives and `Kingdoms.key` before and after the
  runs. They are unchanged, and identical to the 0.8.2 run.
- **Syntax checks.** All **446** tracked Python files compile (438 in 0.8.2). All
  **21** tracked `*.sh` scripts and the extensionless `tools/tak-netem` pass `bash -n`.
- **Notes (informational).**
  - The Release build emits 725 warnings (530 in 0.8.2). Every new warning is one of
    two kinds that 0.8.2 already had:
    - `-Wdeprecated-declarations` for `std::filesystem::u8path`: 145 from the new
      `src/client/datafolder.h`/`datafolderpick.h`, plus more from the Cartographer
      sources and tests that include them.
    - `-Wmissing-field-initializers`: `OptionsScreen::Control` in `options.cpp`, and
      one in `scenario_restrictions_test.cpp`.

    The vendored-SQLite `-Wstringop-overread` false positive is unchanged.
  - Retail h-stress (seed 2003) is a remote-sweep scenario and was not run locally.
    The remote section records its 0.8.3 value. 0.8.0 recorded 5,634, and 0.8.1/0.8.2
    recorded 5,606. A new value is expected, because the simulation changed.
  - `tools/run-ctest-selection.sh` is still tracked as mode 100644, so it must be run
    with `bash`.
  - A worktree outside the main checkout still needs an `assets` link for `cobanim`
    and `gaitpace_shipped`.

Logs: `~/tak-tmp/scratch/release-083/local/` (`cfg-*.log`, `make-*.log`,
`build-status.txt`, `versions.txt`, `toolchains.txt`, `ctest-*.log`,
`ctest-status.txt`, `ctest-clang-trolls-rerun.log`, `ctest-asan.log`,
`asan-list.txt`, `tests-new.txt`, `mpai-*.log`, `mpai-results.txt`,
`o2-sha256.txt`, `check-determinism.log`, `check-detmath.log`,
`check_retail_group.log`, `legion-validate.log`, `python-suite.log`,
`verifier.log`, `pycompile.log`, `bashn.log`, `warn-08{2,3}.txt`,
`retail-sha-{before,after}.txt`).

## Remote multiplayer sweep

The full 37-scenario table passed for both shipped pathfinding modes, Retail and
Legion, on **2026-10-15 (17:45–18:28 UTC)**. Referees ran on `tak.pgnet.us` and
`vpn3.pgnet.us`, with optimized local clients. Each scenario allowed ten
simulated minutes (18,000 ticks) or ended at an actual result for that seat.
Real seated players provide the client/referee checksum coverage. The two
all-AI spectator scenarios per mode test flow control, not hashes.

The harness picks the mode through the hosting client's room options:
`TAK_LEGION=1` creates a Legion room, and without it the room is Retail. Every
recorded client and referee replay header (78 Retail, 72 Legion) confirms the
intended mode, replay format 13 and protocol 266. The headers carry engine
version 0.8.2 because the sweep ran on `origin/main` before the 0.8.3 version
bump, which changes only the version string.

| Mode | Completed scenarios | Completed seats | Client-pair checkpoints | Client–referee checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Retail | 37/37 | 52/52 | 10,295 across 22 seat pairs, 0 mismatched | 1,967 across 38 seat/referee comparisons, 0 mismatched |
| Legion | 37/37 (single pass, no retries) | 52/52 | 10,738 across 22 seat pairs, 0 mismatched | 1,356 across 26 seat/referee comparisons, 0 mismatched |

Each mode passed both negative gates before its table started. The referee
reported the planted desync (`client 1 DESYNCED at tick 900`), and the
override-transfer test rejected the incorrect loaded gameplay data
(`verified packs transferred and incorrect loaded gameplay rejected`). The
build-identity gate also passed in each mode (`both at v0.8.2-104-gb5c45401929b`).
The gate checks only `tak.pgnet.us`, so the referee on `vpn3.pgnet.us` was
checked by hand after deployment: the same SHA-256 and the same `--version`
build id. No natural `DESYNCED`, `REFEREE SUSPECT` or client-reported desync
appeared in any case log. The only `DESYNCED` and `err=desync detected` lines
are the planted ones in the validation logs.

The first Retail attempt was started without `--validate` by mistake. It was
stopped after about a minute (one case had finished), its referee replays were
deleted from both hosts, and the Retail pass was restarted from the beginning
with the gates. The table above is the restarted pass only.

The checkpoint counts compare the per-tick hash records in the seats'
`.takrep` files (every 30 ticks). The client-referee column compares the seats
with referee replays (every 300 ticks). Each checkpoint (format 12 and later)
carries a position digest as well as the state hash, and the comparison script
checks both wherever both sides recorded one. All 2,655 referee checkpoints
(1,486 Retail, 1,169 Legion) and all 54,464 seated checkpoints carried a
digest. Format 13 only appends the MAP SCENARIO byte to the header, so the
checkpoint layout is unchanged; before the counts were trusted, the script was
still re-checked against a copy of one format-13 game's replay (`2h-baseline`,
Retail) with one hash bit flipped, and then one digest bit flipped. Both times
it reported a mismatch at tick 9,000 between the seats and between the edited
seat and the referee. Referee replays were matched to their cases by the seed
in the replay header (seed = 2000 + scenario index), not by port. The port was
also checked, and it agreed for every replay. The referee counts are partial:
the harness stops each referee as soon as its clients exit, so only 27 Retail
games (25 seated) and 21 Legion games (19 seated) wrote a referee replay. The
referee's live check runs on every seated game, whether or not a replay was
written.

**No Legion retries.** As in 0.8.1 and 0.8.2, the Legion pass ran with
`TAK_WALL_TIMEOUT=3600` from the start, with the same seeds and the same
simulated-time limit. It changes only the real-time deadline, not the tick or
hash checks. Every Legion seat completed in the single pass (25 minutes).
`2h-orders-stress` reached 18,000 ticks with 7,137 units, `h-stress` concluded
at tick 5,687 with 7,807 units and `2h-stress` at tick 6,167 with 8,520 units.
The Retail pass used the default limit, and every case finished inside it (18
minutes, including the gates): `h-stress`, `2h-stress` and `2h-orders-stress`
concluded at ticks 4,789, 4,863 and 5,174 with 8,411, 9,172 and 8,972 units.

**Reproducibility across releases.** The simulation changed in both modes
since `v0.8.2` (healtime self-repair, air combat, the Legion review fixes,
retail path-search fidelity and the growable entity pools; protocol 257 → 266),
so no case is expected to repeat a 0.8.2 hash, and none does: all 37 Retail and
all 37 Legion cases ended with a different tick or hash from the 0.8.2 sweep. In
every one of them the seats and the referee agreed. One Retail outcome moved a
long way: `h-cramped` (Inner Circle, four players, `TAK_GODS=1`, idle human
seat) now concludes at tick 1,303 with 11 units, against 11,045 in 0.8.2. It is
deterministic (the aborted first attempt ended at the same tick with the same
hash, `dc97809f3ba31a8c`) and the seat matched the referee at 5 of 5 shared
checkpoints; the cause was not investigated here. The Legion `h-cramped` ran to
18,000 ticks.

**Retail `h-stress` (seed 2003), the 0.8.1 follow-up.** This sweep concluded at
tick 4,789 with hash `8e7c0acbf9752f10` and 8,411 units, and the seat matched
the referee at 16 of 16 shared checkpoints. The 0.7.28–0.8.2 value (tick 5,606,
`c93ae6becfcde6ca`, 8,559 units) and the single 0.8.0 outlier (tick 5,634) are
not comparable any more, because the Retail simulation changed. The new value
is the reference for the next sweep; whether it repeats there is what would
show if the run-to-run variation seen once in 0.8.0 still exists.

**Impaired networks.** All eight impaired scenarios ran under netem in both
modes: 50, 100 and 500 ms delay, 100 ms ± 30 ms jitter, 1% and 3% loss, live
orders, and two seated players over 100 ms. For every case, the harness
archived the `__SETUP_OK__` marker, the `prio 1:` root with `netem 30:` on band
1:3, and the u32 filter matching that case's exact game source port (Retail
7927–7934, Legion 8027–8034). Each teardown log ends with only the interface's
default qdisc (`fq_codel` on tak `ens18`, `noqueue` on vpn3 `wg0`). No case
recorded an unclean teardown.

**Isolation.** The referee was a separate test executable,
`~/takserver.sweep083`, using test ports 7890–7936 and 7990–8036 (the public
service uses 7677). It used per-mode map caches
`~/tak_cache.sweep083-{retail,legion}` and per-run replay directories
`~/tak_replay.sweep083-{retail,legion}`. The public `/usr/bin/takserver` on
tak.pgnet.us (PID 38719, port 7677, active since 2026-10-14 13:32:47 UTC) was
not replaced, stopped or restarted. No certificates were touched. Local clients
were pinned to CPUs 4–23.

**Cleanup.** After all runs, both hosts were checked:

- No process was executing `takserver.sweep083`.
- No listener remained on the test ports.
- No `netem` or `prio` qdisc remained on any interface (`fq_codel` on tak
  `ens18`, `noqueue` on vpn3 `wg0`).
- No `/tmp/tak-netem-owner.*` claim file remained. Nine 1800-second watchdogs
  from impaired cases were still sleeping: six on tak (Retail
  `net-2h-rtt100`; Legion `net-rtt50`, `net-jitter`, `net-rtt500`,
  `net-loss3-orders`, `net-2h-rtt100`) and three on vpn3 (Legion `net-rtt100`,
  `net-loss1`, `net-orders100`). Their claim files were already gone, so they
  would have exited without running `tc`. They were stopped by exact PID.
- The test binary, the sweep's map caches and replay directories, and the
  `/tmp/tak-{srv,val,neg}-*.log` files were removed from both hosts after the
  referee replays had been copied locally.

Locally, no sweep `takclient` or harness process remained. The public
tak.pgnet.us service was still `active`, with the same PID as before the sweep.

**Binaries.** All were built from `b5c45401` (`origin/main`), with build id
`v0.8.2-104-gb5c45401929b` and a clean tree. Protocol `kNetVersion` was 266.

- Referee: Ubuntu 24.04 container, GCC 13.3.0, optimized Debug (`-O2 -g`), max
  symbol version GLIBC_2.38. SHA-256
  `0fb6c61e8914ce071b3c16663457d26040cb0b88384dca083de60baabb0df732`
  (identical on both hosts, checked after deployment).
- Client: Fedora, GCC 16.2.1, optimized Debug (`-O2 -g`). SHA-256
  `895b4910e8dae50491c1c81e5b0d17d00ab2943f68313340e814002ed3879820`.
- `override_transfer_test`: SHA-256
  `b88a821cfcb20eb08a17a4dd3193f162fe1bd3464b980c9c749102c53686db34`.

**Harness.** These runs used the committed `tools/desync-hunt-remote.sh`,
unmodified, from a clean detached worktree. SHA-256
`02d27068863de212b488e19ba8dbe14e9a386b74039dd709defabaabbd6db974`
(byte-identical to the file used for 0.7.25–0.8.2).

**Mixed versions.** No game mixing a 0.8.2 build with a 0.8.3 build was played.
None can be: 0.8.3 speaks protocol 266, the server's Hello rejects any client
whose `kNetVersion` differs (`protocol version mismatch`), and the replay
loader accepts a replay only when its protocol equals `kNetVersion`
(`supportedReplayProtocol` in `src/net/replayhdr.h`). 0.8.3 also writes replay
format 13, which older builds refuse. So 0.8.3 is neither multiplayer- nor
replay-compatible with 0.8.2 or earlier.

## CI and packages

*Pending.*

## Screenshots

Done on 2026-10-15 with the 0.8.3 release-preparation build (`takclient
--version` 0.8.3, build `v0.8.2-104-gb5c45401929b-dirty` from the version bump
and the capture patch; Debug with `-O2`, OpenGL) and the capture-only patch of
0.8.2, merged onto 0.8.3's own Options capture hook and not committed. Each
re-rendered image was inspected by eye against its caption.

- **Why these.** The lobby gained MAP SCENARIO, BY SLOT and numbered start
  markers (the create screen's preview and the room's map); ALLOW SPECTATORS is
  multiplayer only and does not appear on the single-player screens pictured.
  The Cartographer gained the BLOCKED tool and browser scrollbars. Spectators
  now see every side's health bars, which changes every spectator view of a
  fight. The simulation changed in both modes, which moves the spawned and
  recorded scenes.
- **Gallery.** Re-rendered: `title.jpg` (shows VERSION 0.8.3), `battle.jpg` and
  `dragons.jpg` (health bars on every side), `flyers.jpg`, `lobby.jpg`,
  `lobby-scenario.jpg`, `room.jpg` (start markers in the four seated players'
  colours) and `cartographer.png` (five tools).
- **User guide.** Re-rendered: `guide/flyers-retail-legion.jpg` (both
  halves), `guide/replay.jpg` (a fresh recording played back by the same build;
  0.8.3 cannot play the 0.8.2 recording), `guide/fire-on-the-move.jpg` (now four
  of sixteen fallen at 14 s, against five in the 0.8.1 picture),
  `guide/benchmark.jpg`, `guide/scorecard.jpg`, `guide/tactical-dots.jpg` (both
  halves from one deterministic run) and `guide/cartographer-terrain.jpg`.
- **Kept.** The 0.8.1 information-bar pictures (`hud.jpg`, `naval.jpg`,
  `diplomacy.jpg`, the guide's HUD, selection strip, five side pictures, Shift
  queue, campaign briefing and Options pictures): their screens did not change,
  though the 0.8.3 simulation would place some units a little differently a few
  seconds in. `generated.jpg` (a spectator view with no unit in sight), the
  menus, the results and Crusades screens, `streaming.jpg`,
  `cartographer-model.png` and the other 26 Cartographer guide pictures, whose
  whole-window shots still show four tool buttons (noted in
  [the capture notes](img/README.md)).
- **Encoding.** As documented in [the capture notes](img/README.md): JPEG with
  full chroma resolution, quality 90 (title), 88 (gallery) and 86 (guide),
  guide images scaled from 1600 × 900 to 1280 × 720, the title cropped to its
  1440 × 1080 artwork, `lobby-scenario.jpg` cropped to the lobby panel;
  `cartographer.png` is the editor's `--shot` PNG, stored as RGB.
- **Size.** Total tracked image bytes 18,875,806 -> 18,826,503 (−49 KB).
- **Links.** All relative links, anchors and image paths in the changed
  Markdown resolve (a scripted check of 378 links in the nine changed files),
  and `tools/legion_doc_links.py` reports 61 citations resolved, 0 problems.
- **Not captured.** Multiplayer screens (ALLOW SPECTATORS, the NO WATCH
  button), BY SLOT in the room, the BLOCKED tool in the Cartographer guide's
  other pictures, motion (attack runs, self-repair), Windows and macOS
  rendering.

## Limits

*Pending.*
