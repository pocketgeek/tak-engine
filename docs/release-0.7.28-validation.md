# 0.7.28 release validation

Protocol **253**, replay format **12**, generator version **8**, campaign
payload **4**, database schema **9**.

**Pending:** version 0.7.28 has not been tagged or published yet. This report
is completed as the checks below are run.

## Scope and compatibility

0.7.28 follows 0.7.27 (protocol 238) and carries protocols 239 to 253:

- **Legion plan W0–W9 (protocols 239–248).** Shared goal fields, one convoy
  per click, the settle rule, upkeep on demand, reach rings, flyer stations for
  squads, paused first builds, crossing-group corridors, the passage gate and
  head-on meets. Workstreams that changed Legion behaviour regenerated the
  Legion navigation golden and re-licensed the scenario baseline.
  Eleven harder targets were accepted as known limits at the close of the plan
  (user decision W9-1; see `docs/legion-pathfinding.md`).
- **Engine-wide performance (WE).** The simulation steps are exact: as their
  commits record, each was checked with `tools/legion_identity.sh` against its
  base (replays, goldens, `--mpai` games, crowdbench rows and the determinism
  golden SAME).
  Protocol 249 adds client progress acks and ping-scaled flow control (no sim
  or hash change); the presentation clock and jitter buffer are client-only.
- **Game speed (protocol 250).** The original's levels −9..+10 (0.1×–2×),
  pacing and setup only, no hashed change in a normal game; release servers
  clamp to 2×. Protocol 250 also carries the staged stress fill.
- **Blocked building sites (protocol 251)** and the **fire-at-will sweep with
  deterministic per-player vision, AI weapon switching and idle retaliation
  (protocol 252)**, plus the sweep's `shootme` test (protocol 253). These change
  hashed simulation state in both pathfinding modes; the Retail and Legion
  navigation goldens and the Legion battle baseline were regenerated for 252.
- **AI-side changes** (Defensive AI against unseen attackers, the AI Monarch
  fighting back, Defensive build sites out of fights) issue recorded commands
  only and need no protocol change.
- **Client-only changes:** build-menu portraits with Smooth GUI Art, Max FPS
  greyed out under VSync, VSync presentation pacing, the Windows
  display-scaling option, and the stats-panel and Benchmark speed level.
- **Replay format 12** adds a position digest to checkpoints; format-11 files
  still load. Directory-wide `-ffp-contract=off` fixes a macOS-hosted game
  diverging from Linux and Windows without changing Linux or Windows code.

Protocol 253 is required for live play. Replays need the exact simulation
protocol, so protocol-238 (0.7.27) recordings are not playable by this build.
Generator version 8, campaign payload 4 and database schema 9 are unchanged
since 0.7.27 (checked in source against `v0.7.27`: `git diff` touches none of
`src/tnt/mapgen*`, `src/net/crusades.h` or the Crusades store; the replay
header change is the format-12 digest).

## Local preparation checks

All checks ran on a clean detached worktree of `origin/main` at
`3a90d24ce5a7df2dba690ceba2824614b2e409a1` (protocol **253**, replay format 12).
The version bump lives on the docs branch, so these binaries still call themselves
0.7.27. The owned retail install served as `--data` and `TAK_TEST_DATA`. Toolchains
were GCC 16.2.1, Clang 22.1.8, CMake 4.3.0, Ninja 1.13.2 and Python 3.14.8. Cores
0–3 stay reserved for timing work, so builds and tests ran on cores 4–23.

**Result: no crash, desync or sanitizer finding in engine code.** Three findings
from this run were resolved before tagging; the details are below each bullet:

1. **Stale scenario hashes:** `tools/scenarios/baseline.json` retaken (`cd508a45`).
   A full `--offsets gate` run of all 67 scenarios failed only on the 828
   `hash@offset` keys; after the retake all 16,253 keys pass and
   `legion_check.py validate` reports 0 problems.
2. **`naval_ai`:** not a regression; the Taros case passed by seed luck (about a
   third of 64 seeds pass before and after the fire-at-will series). An AI-side
   fix (`9d86b86d`) lets hover units sail as the fleet when no ground unit can
   reach the objective; `naval_ai` passes 146/146 and `--mpai` is unchanged.
3. **ASan test-harness defects:** fixed in `10445359`; the six affected tests pass
   under ASan with leak detection.

The expected Legion battle-cost ratchet failure did **not** happen, so no
`release-local-728-baselines` branch was needed. Details follow.

- **Builds.** Release, Debug, optimized Debug (`-O2 -g`), Clang Debug and
  AddressSanitizer all-target builds completed with exit code 0, at 545 steps each.
  These were fresh trees, with ccache on the GCC builds. They use the static-deps
  prefix (`third_party/static-deps`) and the static Bink/streaming FFmpeg prefix
  (`third_party/ffmpeg-bink`). Every client and server reports
  `takclient (TAK engine) 0.7.27 (build v0.7.27-391-g3a90d24ce5a7)` and
  `takserver (TAK engine) 0.7.27 (build v0.7.27-391-g3a90d24ce5a7)`. The build
  identity is clean, with no `-dirty` suffix. The server announces `protocol v253`.
- **Full CTest.** There are now 533 registered tests (520 in Release), up from 253.
  160 of them carry the `nightly` label.
  - **Release: 451/520 passed. Optimized Debug: 465/533 passed.** Both ran the full
    suite, nightly label included. The failures are the 67 `legion_check_*` gates and
    `naval_ai` (both explained below). In Release, `cobanim` also failed because the
    worktree had no `assets/extracted` tree. After I linked it in, `cobanim` passed
    (11 checks). This is a property of the environment, not the code.
  - **Debug and Clang Debug ran with `-LE nightly`.** I first started a full Debug
    run. At `-O0`, `legion_pocket` and `legion_scenario_split-450-open` hit their
    1200 s timeouts. They take 160–186 s at `-O2`, and the nightly job only ever runs
    them at `-O2`. I stopped that run at 269/533 (log kept as
    `ctest-build-dbg-full-aborted.log`). The nightly tests therefore ran in Release
    and optimized Debug only.
  - **Debug: 372/373 passed.** The one failure is `naval_ai`.
  - **Clang Debug: 370/373 passed.** Besides `naval_ai`, `movement_orders_trolls`
    timed out (300 s limit) and `override_transfer` failed with "peer closed". Both
    happened while the Debug and Clang suites were running at the same time. Run
    alone, both passed: 212.4 s and 23.1 s. So both are load effects, but
    `movement_orders_trolls` has little headroom under unoptimized Clang.
  - **Battle cost tests passed.** `legion_cost` and `legion_scenario_battle-field-2x60`,
    `-2x250`, `-2x500` and `battle-assault` all passed in Release and optimized Debug.
    The fire-at-will mopup ratchet we expected did not fire.
  - **`legion_acceptance_crowdheld_legion` is enabled now and passes.** For 0.7.27 it
    was disabled.
  - **Slowest tests.** In Release and optimized Debug: `legion_check_tail-corner380`
    (962.7 s / 838.5 s), `legion_check_pocket-615-wall` (955.2 s / 844.6 s) and
    `legion_check_pocket-615-open` (948.3 s / 854.2 s). All are under their 1800 s
    limit. In Debug and Clang Debug the slowest was `legion_w7_awarebig`
    (415.4 s / 457.9 s).
- **Stale scenario hashes (resolved in `cd508a45`).** All 67 `legion_check_*` gates fail in
  Release and in optimized Debug, and they are the same 67. Every failing key is a
  `hash@offset` equality key: 319 Legion keys and 509 Retail keys in the
  optimized Debug log. No work, metric or floor key fails. The gates also print 184
  RATCHET improvements. The last nightly run passed at `8b855e1e`, which is before
  the fire-at-will series (protocols 252/253). That series changes hashed unit state,
  and `tools/scenarios/baseline.json` was not retaken when it landed. Tonight's
  nightly will fail the same way. The hashes need to be retaken with
  `tools/legion_check.py baseline --hashes`. I did not do this, because it is outside
  the ratchet-only regeneration this check was allowed to make.
- **`naval_ai` (resolved in `9d86b86d`).** It fails in all four builds on a single case:
  `balance=1 faction=1 completed=3 attacks=0`, the Crusades Taros natural opening.
  The other 145 cases pass. A bisect from `8b855e1e` (passing) points to `8f176917`,
  "Boarding announces TargetCleared once, not twice". That commit clears
  `slotTarget` in `clearScriptWeaponTarget`.
  - **The case was already fragile before that commit.** I ran it with the AI seed
    varied over 8 values (an uncommitted local change). At the parent commit
    `7c2d225a`, 6 of 8 seeds already failed a Taros natural-opening variant. At HEAD,
    7 of 8 fail.
  - **So the commit only flipped the test's pinned seed `0x1234`.** It does not look
    like a sim bug.
  - **What it does show is real:** with the natural opening, the Taros AI rarely
    builds and deploys hover units.
- **Focused AddressSanitizer/leak run: 116/122 passed.** The selection is the 84
  tests from 0.7.27 plus 38 tests added since: `fire_sweep_*`, `issue_selection_test`
  (+`_data`), `legion_idmap_test`, `convoy_test`, `fp_contract`, `legion_aware`,
  `legion_b3events`, `legion_doorplug`, `legion_factorysquad`, `legion_lanegeom`,
  `legion_liftflyers`, `legion_patrolrepair`, `legion_pausedemand`,
  `legion_settlelatency`, `legion_staticidle`, `legion_structsquad`,
  `legion_unreachable_orders`, `legion_w5_fixtures`, four `legion_flyers_*`, two
  `legion_w7_*`, `legion_scenario_smoke`, `legion_scenario_battle-field-2x60`,
  `observer_neutral`, `pacelog`, `server_seed`, `server_speed_pacing`, `simpool`,
  `simpool_passes`, `sound_hit`, `sound_hit_data` and `tick_pacer`. `pathblock` was
  already in the selection. The run used `bash tools/run-ctest-selection.sh` with
  `SDL_SHUTDOWN_DBUS_ON_QUIT=1`, dummy SDL video/audio,
  `ASAN_OPTIONS=detect_leaks=1` and no suppressions. There were no leak reports.
  The six failures come from two defects, both in test harnesses:
  - **`issue_selection_test` and `issue_selection_test_data`: stack-use-after-scope**
    at `tools/legion_issue_selection.h:93`. In `tools/issue_selection_test.cpp`,
    lines 416 and 427–429 (added in `cc8a9d7a`) call `f.block(flyer(), ...)` and
    `f.block(ground(2), ...)` on temporary `UnitType`s. The spawned units keep
    pointers to those temporaries after the statement ends. The fix is to bind each
    type to a named local, as line 389 already does.
  - **`legion_acceptance_{singleunit,group,jagged,trapped}_legion`:
    alloc-dealloc-mismatch** in the allocation-counting `operator new`/`delete`
    replacements in `tools/crowdbench_matrix.h:62-73`. libstdc++'s
    `std::inplace_merge` gets its temporary buffer through the nothrow
    `operator new`, which the harness does not replace. The harness's sized
    `operator delete` then frees that buffer with `std::free`. The code path is
    new since 0.7.27: `IdMap::normalize()` came in with `a7a7902a`. Outside ASan the
    mismatch is harmless (malloc/free). With `alloc_dealloc_mismatch=0` all four
    tests pass and report no other error or leak. The fix is to replace the nothrow
    `new` variants too.
- **Retail group emulation check (formation-air).** I ran
  `python3 -B tools/re/check_retail_group.py` (400 cases by default) against the
  emulated `KINGDOMS.icd`. It passed every section: tick 400, query 4000, check 2000,
  formation 2000, **vtolcheck 2000** and **vtolform 2000**. The outcome counts are
  identical to 0.7.27, including vtolcheck: 367 reform, 48 wait and 19 cancel.
- **Determinism.** In `tools/check-determinism.sh`, the GCC and Clang x86-64 builds
  at O0/O2/O3 all agree on `dcef618cd2e4d558`, the same as 0.7.27. The aarch64 legs
  are skipped because target headers are missing; native ARM64 CI remains a separate
  gate. `tools/check-detmath.sh` passes, including the new `std::atomic_ref` guard.
- **`--mpai` golden hashes.** All four runs were made twice and produced the same
  hash both times. The client and server logs contain no desync or referee-suspect
  line. The runs used the optimized Debug build, Inner Circle,
  `takserver --local --no-auth --seed 1`, an empty `XDG_DATA_HOME`, and
  `TAK_LEGION=1` for the Legion runs. Every hash differs from 0.7.27, as expected:
  protocols 239–253 change hashed sim state (fire-at-will, retail game speed, AI
  and Legion changes).

  | Mode | Duration | Hash (both runs) | 0.7.27 |
  | --- | ---: | --- | --- |
  | Legion | 300 s | `21055e66adec40d6` | `ca02108dfc933123` |
  | Retail | 300 s | `95b05c8290b55317` | `b240750e5765c02b` |
  | Legion | 60 s | `025e4a35abfcd04e` | `dadf535a50b404e1` |
  | Retail | 60 s | `84fdef37bbb3606e` | `56cfcbf8ef57181e` |

  Optimized Debug binary SHA-256:
  - client `59786ad587ad176fb2886adc9ee931a3ebe9eea8b089b8d97bd0db243f4560d4`
  - server `7819c67b1ffe7df47ce318facf6ac75c6389f437761bf47a97abf945bef0c198`
- **Python research suite.** **165 tests** passed, with one skip: the optional corpus
  test, which runs only when `TAK_AUTH_RETAIL_DIR` is set. The suite ran after the
  Release build. Before that build, 6 more tests skip because they need
  `build/hpitool`. The separate owned-retail archive verifier (`TAK_AUTH_RETAIL_DIR`)
  passed all **6 tests**, including the corpus and in-memory negative checks. I took
  SHA-256 sums of the 16 retail HPI archives and `Kingdoms.key` before and after the
  runs. They are unchanged, and identical to the 0.7.27 run.
- **Syntax checks.** All **437** tracked Python files compile. All **21** tracked
  `*.sh` scripts and the extensionless `tools/tak-netem` pass `bash -n`.
- **Notes (informational).**
  - The Release build reports the same warning categories as 0.7.27:
    `-Wmissing-field-initializers`, `-Wdeprecated-declarations` (`u8path`),
    `-Wmisleading-indentation`, `-Wunused-parameter`, `-Wsign-compare` and
    `-Wignored-attributes`. There is no new category; the build emits 610 warnings
    in total.
  - `tools/run-ctest-selection.sh` is still tracked as mode 100644, so it must be
    run with `bash`.
  - `cobanim` reads `${CMAKE_SOURCE_DIR}/assets/extracted/all/scripts`. A worktree
    outside the main checkout needs an `assets` link to the real `assets` directory,
    or that test fails with "no COBs found".

Logs: `~/tak-tmp/scratch/release-728/local/` (`cfg-*.log`, `make-*.log`,
`build-status.txt`, `versions.txt`, `ctest-*.log`, `ctest-status.txt`,
`ctest-build-clang-rerun.log`, `ctest-build-dbg-full-aborted.log`, `ctest-asan.log`,
`ctest-asan-recheck-*.log`, `asan-list.txt`, `mpai-*.log`, `mpai-results.txt`,
`o2-sha256.txt`, `check-determinism.log`, `check-detmath.log`,
`check_retail_group.log`, `python-suite.log`, `verifier.log`, `pycompile.log`,
`bashn.log`, `retail-sha-{before,after}.txt`, `bisect-run.log`,
`bisect-results.txt`, `seeds.txt`).

## Remote multiplayer sweep

The full 37-scenario table passed for both shipped pathfinding modes, Retail and
Legion, on **2026-10-12 (01:47–02:47 UTC)**. Referees ran on `tak.pgnet.us` and
`vpn3.pgnet.us`, with optimized local clients. Each scenario allowed ten
simulated minutes (18,000 ticks) or ended at an actual result for that seat.
Real seated players provide the client/referee checksum coverage. The two
all-AI spectator scenarios per mode test flow control, not hashes.

The harness picks the mode through the hosting client's room options:
`TAK_LEGION=1` creates a Legion room, and without it the room is Retail. Every
recorded client and referee replay header (79 Retail, 62 + 10 Legion) confirms
the intended mode, replay format 12 and protocol 253. The headers carry engine
version 0.7.27 because the sweep ran on `origin/main` before the 0.7.28 version
bump, which changes only the version string.

| Mode | Completed scenarios | Completed seats | Client-pair checkpoints | Client–referee checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Retail | 37/37 | 52/52 | 10,994 across 22 seat pairs, 0 mismatched | 2,156 across 40 seat/referee comparisons, 0 mismatched |
| Legion | 37/37 (33 first pass + 4 retried) | 52/52 (46 first pass + 6 retried) | 11,605 across 22 seat pairs, 0 mismatched | 1,193 across 21 seat/referee comparisons, 0 mismatched |

Each mode passed both negative gates before its table started. The referee
reported the planted desync (`client 1 DESYNCED at tick 900`), and the
override-transfer test rejected the incorrect loaded gameplay data
(`verified Full packs transferred; mismatching loaded gameplay rejected`). The
build-identity gate also passed in each mode, and again before the Legion
retries (`both at v0.7.27-391-g3a90d24ce5a7`). No natural `DESYNCED`,
`REFEREE SUSPECT` or client-reported desync appeared in any case log. The only
`err=desync detected` lines are the planted ones in the validation client logs.

The checkpoint counts compare the per-tick hash records in the seats'
`.takrep` files (every 30 ticks). The client-referee column compares the seats
with referee replays (every 300 ticks). Replay format 12 adds a position digest
to every checkpoint. The comparison script was updated for the 20-byte record,
and it now checks the digest as well as the state hash wherever both sides
recorded one. All 2,808 referee checkpoints and every seated checkpoint carried
a digest. Before the counts were trusted, the script was checked against a copy
of one game's replay with one hash bit flipped, and then one digest bit
flipped. It reported a mismatch both times. The referee counts are partial:
the harness stops each referee as soon as its clients exit, so only 28 Retail
games (26 seated) and 21 Legion games (19 seated, including the three seated
retries) wrote a referee replay. The referee's live check runs on every seated
game, whether or not a replay was written.

**Legion retries.** As in 0.7.25–0.7.27, the dense Ulasem Arena stress cases
ran past the default 900-second real-time limit in the first Legion pass
(`timeout -k 30`, rc=137) without any error or desync. This time there were
four: `h-stress` (seed 2003), `2h-stress` (seed 2021), `2h-orders-stress`
(seed 2023) and the spectator `w-allai-stress` (seed 2035). The harness labels
these `HIT ... no-completion rc=137` and lists them as NEVER RAN, because no
seat wrote a result line. The referee logs held no desync or suspect report.
Unlike 0.7.27, no host seat concluded early, so both seats of the two-player
cases were cut off. All four were rerun together with `TAK_WALL_TIMEOUT=3600`,
the same seeds and the same simulated-time limit. The rerun started with all
three gates, and every seat passed. All six seats reached 18,000 ticks after
about 1,050–1,160 s, with 5,864–7,297 units at the end:

- `h-stress`: the seat matched the referee on 60/60 checkpoints.
- `2h-stress`: the seats matched on 600/600 shared checkpoints, and each
  matched the referee on 60/60.
- `2h-orders-stress`: the seats matched on 600/600 shared checkpoints, and
  each matched the referee on 60/60. This is an order-autoplay case: live
  orders are bucketed by arrival tick, so the outcome differs between runs.
- `w-allai-stress`: the spectator reached 18,000 ticks (no hash comparison).

During the reruns, each referee was CPU-bound at about 100% of one core, while
the clients used about 70–74%. These games were limited by the referee's speed
and were not stalled. In Retail, the same cases finished inside the default
limit, and the stress games concluded at ticks 5,606–6,826. This shows
consensus and completion. It does not claim that Legion keeps 4× speed in
dense battles.

**Restarted Retail attempt.** The first Retail launch (01:46 UTC) stopped at
the build-identity gate with `could not read a build id (... server='')`
before any referee or game started. The cause was the harness `TMPDIR`: under
`~/tak-tmp/rr8` the ssh ControlPath socket was longer than the 108-byte Unix
socket limit, so the gate's ssh call failed. `TMPDIR` was changed to a short
symlink (`~/r8` → `~/tak-tmp/rr8`), and the Retail table then ran from scratch
with the gates. No harness change was made.

**Impaired networks.** All eight impaired scenarios ran under netem in both
modes: 50, 100 and 500 ms delay, 100 ms ± 30 ms jitter, 1% and 3% loss, live
orders, and two seated players over 100 ms. For every case, the harness
archived the `__SETUP_OK__` marker, the `prio 1:` root with `netem 30:` on band
1:3, and the u32 filter matching that case's exact game source port (Retail
7927–7934, Legion 8027–8034). Each teardown log ends with only the interface's
default qdisc (`fq_codel` on tak `ens18`, `noqueue` on vpn3 `wg0`). No case
recorded an unclean teardown.

**Isolation.** The referee was a separate test executable,
`~/takserver.sweep0728`, using test ports 7890–7936 and 7990–8036 (the public
service uses 7677). It used per-mode map caches
`~/tak_cache.sweep0728-{retail,legion}` and per-run replay directories
`~/tak_replay.sweep0728-*`. The public `/usr/bin/takserver` on tak.pgnet.us
(PID 1111, port 7677) was not replaced, stopped or restarted. No certificates
were touched. Local clients were pinned to CPUs 12–23.

**Cleanup.** After all runs, both hosts were checked:

- No process was executing `takserver.sweep0728`.
- No listener remained on the test ports.
- No `netem` or `prio` qdisc remained on any interface.
- No `/tmp/tak-netem-owner.*` claim file remained. One 1800-second watchdog
  from the last Legion impaired case (`net-2h-rtt100`) was still sleeping. Its
  claim file was already gone, so it would have exited without running `tc`.
  It was stopped by exact PID.
- The test binary, the sweep's map caches and replay directories, and the
  sweep's `/tmp/tak-{srv,val,neg}-*.log` files were removed from both hosts
  after the referee replays had been copied locally.

Locally, no sweep `takclient` or harness process remained. The public
tak.pgnet.us service was still `active`, with the same PID as before the sweep.

**Binaries.** All were built from `3a90d24c` (`origin/main`), with build id
`v0.7.27-391-g3a90d24ce5a7` and a clean tree. The harness was copied in only
after both builds finished.

- Referee: Ubuntu 24.04 container, GCC 13.3.0, optimized Debug (`-O2 -g`), max
  symbol version GLIBC_2.38. SHA-256
  `1058252c022498390e6fea670017a73620727ecbba3c78ca2dc3d8f6422b7025`
  (identical on both hosts, checked after deployment).
- Client: Fedora, GCC 16.2.1, optimized Debug (`-O2 -g`). SHA-256
  `e19561a52b371f0126fcd1856088fb1977f92ab861d21ce9e20c77fa482720f7`.
- `override_transfer_test`: SHA-256
  `da9d812f80ca1165a54157861a89191a48f15d9f85bafba17906777668959819`.

**Harness.** These runs used the committed `tools/desync-hunt-remote.sh`,
unmodified, from a clean detached worktree. SHA-256
`02d27068863de212b488e19ba8dbe14e9a386b74039dd709defabaabbd6db974`
(byte-identical to the file used for 0.7.25–0.7.27).

## CI and packages

Pending.

## Screenshots

Done on 2026-10-11 with the 0.7.28 release-preparation build (`takclient
--version` 0.7.28; Debug with `-O2`, OpenGL) and a capture-only patch that is
not committed. Each new or re-rendered image was inspected by eye against its
caption.

- **Gallery.** Re-rendered: `title.jpg` (shows VERSION 0.7.28) and `hud.jpg`
  (the stats panel's SIM row now reads NORMAL, a game-speed level). The other
  gallery images are unchanged; none shows the stats panel, the Options screen,
  or a build-menu portrait with Smooth GUI Art on.
- **User guide.** New: `guide/fire-on-the-move.jpg`, a two-player development
  scene of Archers and Mage Builders shooting on a plain move order, inserted in
  [Your army](user-guide.md#your-army) with alt text and a caption.
  Re-rendered: `guide/hud-annotated.jpg` (SIM reads NORMAL; the selected-Monarch
  marker moved to where Elsin stands) and `guide/options-graphics.jpg`, now
  showing the Display section with Max FPS greyed out under VSync above the
  Graphics settings, moved to the guide's [Display](user-guide.md#display)
  section. The Windows-only display-scaling row is absent from this Linux
  capture.
- **Labels.** The development scene is identified in its caption and in
  [the capture notes](img/README.md).
- **Size.** Total tracked image bytes rose from 15.48 MB to 15.78 MB (+0.30 MB,
  mostly the new picture).
- **Links.** All relative links, anchors and image paths in the changed
  Markdown resolve.
- **Not captured.** Smooth GUI Art build-menu portraits (off by default in
  every capture profile), the Windows display-scaling row, and the in-game
  speed banner.

## Limits

Local visual inspection covers Linux OpenGL screenshots only; the Windows and
macOS packages are checked from CI (signatures, sizes, build ids) and were not
played by hand for this release. Remote simulated-time sweeps do not substitute
for a multi-hour wall-time soak, and dense Legion stress battles still run below
the harness's speed on the Debug test referees (see above). The fire-at-will
sweep has documented deviations from retail (docs/retail-engine.md): FULL VISION
games still target by sight, ranged ground auto-slots keep a 2D clear-shot test,
and turn-rate terrain multipliers are not modelled. The Debug (`-O0`) builds
skip the nightly-labelled tests, as the nightly job only runs them optimized.
