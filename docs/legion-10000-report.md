# Legion 10,000: measured before/after report

The final report of the Legion 10,000 program: navigation for about 10,000 individually simulated units moving at once,
built by extending Legion, with Retail unchanged (brief: `LEGION-10000-CLAUDE-PROMPT.md`, copy in `~/tak-tmp/briefs/`).
Every stage's hypothesis, commits, exact commands, licences and rejected approaches are in
[`legion-10000-progress.md`](legion-10000-progress.md); the design is in the "Legion 10,000 stage N" sections of
[`legion-pathfinding.md`](legion-pathfinding.md). This file is the measured result.

**Verdict.**

* **Status.** All nine stages (brief phases 0-8) are implemented and validated on branch `task-legion-10000`. The branch
  is not merged.
* **Retail and determinism.** Retail is bit-identical to the starting revision wherever it was measured. Every hash agrees
  across worker counts, perturbed job completion order, gcc/clang, and Linux/Windows/macOS.
* **Responsiveness: done for single selections.**
  * A 10,000-body click is delivered whole in one tick. It was 308 ticks (10.3 s) in the scale suite, and 345-512
    ticks for 18,000 bodies on the real client/server path.
  * In one-10000 the last member starts within 92 ticks (was 652).
  * No body waits more than 41 ticks for a usable route.
* **Arrival: not done.** Physical arrival at 10,000 bodies rises in every workload but mixed-10000, from 1.06x
  (attack-10000) to 67x (scatter-10000), but it is **not** at the frozen targets: 128 of 212 targets are met (99 at
  the start). Large crowds still stand:
  * at static corners and beside idle blocks;
  * at the mouths of narrow passages;
  * in head-on exchanges;
  * in queues for many unrelated destinations (scatter-10000: up to 2,349 ticks without a route).
* **Tick cost.** The whole-simulation tick stays inside 33.3 ms in 7 of 10 10,000-body rows (A/B, three workers):
  * Not in many-10000 or scatter-10000. There, four to eight times as many bodies now actually move: 13 % and 2 % of
    ticks run over budget.
  * Not in mixed-10000 either: its flyer exploration phase is outside Legion, and the overrun is inherited.
  * Serial per-unit movement, the authoritative collision step, is most of the remaining tick.

This does **not** meet the brief's success criterion ("responsive, correct, individually simulated large armies")
for congested workloads, and this report does not claim it does. Section 13 lists what is left.

## 1. Revisions, machine and method

| | |
|---|---|
| Starting revision | `c2c25707` (origin/main, v0.8.3 validation; protocol 266) |
| "Before" binaries | `c4233fb9`, the stage-1 head: tools, tests and docs only on top of c2c25707, so the simulation is identical (gate hash-equal; identity harness at stage 2). It carries the scale probe the measurements need. |
| Final head | branch `task-legion-10000` = the stage-9 simulation (`9064aa35`, protocol **272**) + merge `29db147d` of origin/main `5774601a` (a tools-only change to `tools/desync-hunt-remote.sh`) + `83cf07a8` (cost baseline, section 5) + this report's record commit |
| Machine | Intel Core Ultra 9 275HX (24 cores, no SMT; 0-7 P-cores, 8-23 E-cores), 125 GB, Fedora Linux 7.2.9 |
| Compiler / build | GCC 16.2.1, `build-o2` (Debug with `-O2 -g`, harness hooks kept), ccache + mold; clang `-O2` on the stage-9 parallel check; MinGW GCC 16.2 (Windows), Apple clang (macOS arm64) |
| Workers | `TAK_SIM_WORKERS=3` (what `SimPool::defaultWorkers` picks on this machine); serial and 1/3/7 workers in the determinism checks |
| Cores | 4-15 (cores 16-23 belonged to a rendering program that was running during these measurements) |
| Game speed | 1x: 30 Hz, a 33.3 ms whole-tick budget. Faster speeds were not measured. Retail's maximum is 2x (16.7 ms); in the A/B below, only one-10000, attack-10000 and idle-10000 have a tick p99 under 16.7 ms. |
| Workloads | `tools/scenarios/scale/` (stage 1, constant-density scaling `one-N`/`many-N`; crowd-density stress `swap-N`; passages `bridges-N`/`maze-N`; mixed classes `mixed-N`; combat `attack-N`; churn `churn-N`; moving-versus-idle `idle-10000`; unrelated destinations `scatter-N` added at stage 6), one run each at offset 0; the real client/server path on Ulasem Arena |

Wall time is never gated. The scale-suite wall columns are single runs, three at a time on a shared machine. The
Retail rows show the noise: their simulation is bit-identical before and after, yet one-10000 Retail's tick mean reads
8.4 vs 9.7 ms and its max 15 vs 61 ms. The tick comparison that counts is the A/B in section 10. There, both binaries
run at the same moment on two equal core sets, then swap.

## 2. Phase status

| Brief phase | Stage | Status | What exists | Not done / limits |
|---|---|---|---|---|
| 0 acceptance criteria | 1 | **implemented and validated** | scale suite (25 files at the start, 31 now), integer scale probe (`legion_scale.h`), real-path delivery probe, 212 frozen targets | `.scn` cannot express production/factory exits/logistics (covered only by `--mpai`, Ulasem benchmark, stress runs) |
| 1 exact overhead reductions | 2 | **implemented and validated** | indexed `registerMove` join with brute-force reference; swept-bounds awareness broad phase with proof; profiled micro-work (vertex probe tools-only, etc.) | -- |
| 2 explicit logical orders | 3 | **implemented and validated** | `SelectionOrder` transaction (Begin/Members/Commit/Abort), server sequencing and activation tick, budgets per sender, every identity consumer, replay/rejoin; protocol 267 | Load/GiveUnit stay per-unit commands (order-dependent); AI and scripts keep inferred convoys |
| 3 fair scheduling, budgets | 4 | **implemented and validated** | job classes, deficit round robin, age promotion, fit scan, reservations, unique jobs, budgeted registration, NavWork/NavBytes accounting; protocol 268 | some stages stay synchronous and are reported in the record's 7.4: incremental plane refresh after a static change (charged afterwards as plane debt), a rare whole-plane relabel, `staticChanged`'s group walk, the awareness snapshot, a formation's slot ranking, one field's initialisation |
| 4 route-query interface | 5 | **implemented and validated** | `RouteView` (Unknown/Reachable/Unreachable, metric versions, coverage), consumers state their region, per-subscriber demand; protocol 269 | demand per cell; the pausing extension not re-measured with tiles |
| 5 hierarchy, sparse tiles | 6 | **implemented and validated** | sectors 32x32 (16/32/64 measured), regions, portals as legal runs, generation-aware ids, sparse tiles, tile chains with shared transit tiles, corridors, search-vs-field by demand with hysteresis, bounded fallback; protocol 270 | abstract search costs are portal midpoints (an underestimate in serpentine regions); sched-load's scheduler ctests still run with `--no-hier-routes` |
| 6 static vs traffic | 7 | **implemented and validated** | per-cluster static changes, closures vs improvements, local chain repair, jams as traffic, traffic in the coarse search, separate correction cache; protocol 271 | no sector-to-route reverse index; windows still bake soft costs |
| 7 subgroups, chokepoints, arrivals | 8 | **implemented and validated** | openings as shared queues with delay charges, re-plans with commitment, navigation subgroups across passages, admission at narrow openings, shared destination areas, slot-headway arrival rules, multi-entrance arrival; protocol 272 | lane reservation by direction on two-lane passages; spreading a crowd across the free width beside an obstacle; awareness by streams measured and rejected |
| 8 parallel planning, reassess movement | 9 | **implemented and validated** | chain tiles and plane first builds on the SimPool with deterministic publication; movement profiled | window relaxation stays serial (would be a schedule change); static look-ahead certificates not attempted; no movement-model change (no measured failure case) |
| Final integration and report | 9 | **implemented and validated** (this file) | full integrated validation, before/after measurements, cost-baseline repair, crowdbench bisect | the gaps in sections 5 and 13 |

"Implemented and validated" means: the stage's code is in, its own tests and mutations pass, the integrated checks below
pass, and its measured effect is recorded. It does **not** mean the stage met every frozen target (section 6).

## 3. Validation run on the final head (exact commands)

Scratch `~/tak-tmp/scratch/legion-10000/final/`; `after-bin/` = the final head's build-o2 binaries; `before-bin/` =
c4233fb9's. Scratch `XDG_DATA_HOME`/`XDG_CONFIG_HOME` everywhere; `TAK_SETTINGS_FILE` never exported for ctest.

| Check | Command | Result |
|---|---|---|
| Build, all targets | `cmake --build build-o2` and `cmake --build build` (Release) | both build |
| Legion scenario gate, both modes, gate offsets, serial and workers | `python3 ../sweep_both.py after-bin/legion_scenario gate 4-11` (each of the 135 `tools/scenarios/*.scn` x 2 modes: `legion_scenario F --mode M --json --offsets gate --data ~/TAK/assets/game`), then `python3 tools/legion_check.py check --cumulative --baseline tools/scenarios/baseline.json --results gate/scenarios.jsonl` | **PASS**: 31,859 keys, 0 failed (3,005 ratchets, 3,333 licensed, 220 floor exceptions, 864 report-only); 270/270 rows hash- and key-identical to the stage-9 gate; serial == workers in all 270 |
| Retail gate rows against the starting revision | the same 135 Retail rows against `base/sweep` (c2c25707) | 135/135 state hashes equal; every common key equal (the one difference is the observer key `orders`, added at stage 3) |
| ctest, everything not duplicated by the gate | `ctest --test-dir build-o2 -j12 --timeout 1800 -E "^(legion_motion_\|legion_gen1_route_\|legion_scenario_(<the gate's 141 file names>)$)"` (the nightly's exclusion) | 494/495; the one failure, `legion_cost`, diagnosed and fixed in 83cf07a8 (section 5); rerun `ctest -R 'legion_cost\|baseline_reasons'`: pass. Includes `legion_parallel` (405 s), `legion_sched_*`, `legion_scale_*`, `server_orders_network`, `selection_order`, `navigation_determinism`, `simpool*`, `replay_test` |
| Cross-build determinism | `sh tools/check-determinism.sh` | OK, golden `dcef618cd2e4d558` (unchanged) |
| `--mpai` Inner Circle seed 1, 60 s | `CORES=12-15 bash ../mpai.sh after-bin mpai60 60` (`takserver --port P --data D --no-auth --local --seed 1`; `TAK_HEADLESS=1 SDL_VIDEODRIVER=dummy takclient game "Inner Circle" --data D --server 127.0.0.1 --serverport P --mpai --time 60`, Legion with `TAK_LEGION=1`) | Retail **2c39430636a3dbda** (= c2c25707), Legion **f75b2a11410f43b4** (= the stage-4..9 baseline) |
| `--mpai` 300 s, twice | `mpai.sh after-bin mpai300 300`, again into `mpai300b` | Retail **7535cb08ee787b75** (= c2c25707) both runs; Legion **47b705a730bbc22f** both runs: **new 300 s baseline** (the last recorded 300 s Legion hash was b25e18fe6f015862 at protocol 268; stages 5-8 changed behaviour and did not rerun the 300 s game) |
| Windows (oden-win, MinGW GCC 16.2) | `~/tak-tmp/tools/wintest.sh task-legion-10000 --no-rel --no-tests --mpai --run "<scale hash commands>"` | `--mpai` 300 s Retail 7535cb08ee787b75, Legion 47b705a730bbc22f (= Linux); scatter-2500 (2,000 ticks; workers, perturbed, serial), mixed-1000 (1,200), churn-1000 (2,000), both modes: every hash = Linux (the first attempt failed to link `takserver.exe` because a stale harness server from an earlier session held the file; stopped, rerun) |
| macOS (MacBook, arm64) | `~/tak-tmp/tools/macrun.sh task-legion-10000 --mpai --run "<scatter-2500 workers / perturbed>"` | `--mpai` 300 s Retail 7535cb08ee787b75, Legion 47b705a730bbc22f; scatter-2500 Legion a57e87dfa56da04d, Retail 571fdaa79e9ce39b (= Linux) |
| Retail scale runs against the starting revision's binary | `before-bin/legion_scenario` on scatter-2500 (workers, serial), mixed-1000, churn-1000 `--mode retail` | equal to the final head's Retail hashes |
| Scale suite | `python3 tools/legion_scale.py run --bin after-bin/legion_scenario --out scale --cores 4-15 --per-job 4 --workers 3`; `check --targets tools/scenarios/scale/targets.json` | 43/43 rows hash- and key-identical to the stage-9 run; targets met **128**, missed 79, report-only missed 5 |
| Before extras | `legion_scale.py run --bin before-bin/legion_scenario --out scale-before-extra --files .../scatter-400.scn ... --files .../sched-load.scn --modes legion` | the five fixtures added after stage 1 measured on the starting revision (rows marked in section 6) |
| Wall A/B | `bash ab.sh ab 5000 one-10000 many-10000 swap-10000 scatter-10000 maze-10000 bridges-10000 attack-10000 churn-10000 idle-10000 mixed-10000` (before-bin vs after-bin, no observer, three workers, `TAK_PHASE`; arms at once on 8-11 / 12-15, then swapped) | section 10 |
| Crowdbench screen | `python3 tools/crowdbench_screen.py --binary build/crowdbench --output screen.jsonl --cpus 4-7 --quiet --baseline ~/.local/share/tak-legion-nightly/2026-10-16T093000Z/screen.jsonl` (the c2c25707 nightly's screen, same machine) | 86/86 ran; Retail 43/43 rows identical to c2c25707; Legion 43 rows: settled arrivals 29,301 -> 29,512, 9 cases better, 7 worse (three by more than 10 %: section 5) |
| Real client/server path, ~9,000 bodies a side | `CORES=4-15 UNITCAP=10000 bash ../stage3/e2e.sh after-bin e2e-10000 120` (Ulasem Arena, `TAK_STRESS`, `TAK_AUTOPLAY_FORMATION`) | section 7 |
| Rejoin | `CORES=4-7 bash ../stage3/rejoin.sh after-bin rejoin` | host and rejoined client end on the same hash **d3fa704568575311**; the rejoined client's later orders land whole; the server refuses nothing |
| Replay verify | `TAK_REPLAY_VERIFY=1 takclient replay <recording> --data D` for the rejoin host's and both e2e recordings | 4,518 / 3,620 / 3,602 ticks, 150 / 120 / 120 checkpoints, no divergence |
| `legion_cost` bisect | `tools/legion_cost.py --runner <stage head>/legion_scenario` for the stage 6, 7, 8 and 9 heads | section 5 |
| Crowdbench bisect | `final/bisect/run.sh` (Release crowdbench built at every stage head) | section 5 |
| Identity harness | not applicable | its replay fixtures are protocol 266 and are refused by 272; the merge of origin/main is tools-only and is proved exact by the hash- and key-identical gate and scale runs |

## 4. Determinism and Retail

* **Lockstep across execution.** All 270 gate rows: serial == workers. legion_parallel compares each arm against
  `--serial`: 1, 3 and 7 workers, perturbed job completion order at 3 and 7, and forced inline under
  `TAK_LEGION_VERIFY`. It passed, with every hash and key equal. On Windows, scatter-2500's state hash is the same
  serial, with workers (that machine picks its own worker count) and perturbed.
* **Across compilers and platforms.**
  * `check-determinism.sh` OK.
  * `--mpai` 300 s: Linux, Windows and macOS agree in both modes.
  * Scale hashes: Linux = Windows = macOS. At stage 9, gcc = clang on scatter-2500.
  * Run-to-run: two 300 s runs gave the same hashes.
* **Retail.** Bit-identical to c2c25707:
  * 135/135 Retail gate rows and 12/12 Retail scale rows, the frozen `hash` targets.
  * `--mpai` 60 s and 300 s.
  * 43/43 Retail crowdbench rows.
  * Retail scale hashes from the starting revision's own binary.

  No protocol-only field forced a change. Retail rejects explicit orders: the server fails a connection that sends
  one in a Retail room.
* **Protocol and replay.** Protocol 266 -> 272: one bump per behaviour stage, 3-8. Stage 2 and stage 9 are exact.
  `replay_test` refuses the previous protocol at each step. New recordings verify, and rejoin agrees with the host.

## 5. Inherited versus new failures

**Inherited** (present at c2c25707):

* The Legion nightly's crowdbench screen fails on main itself every night since 2026-10-12, c2c25707 included:
  2,669 differences against `crowdbench_screen_baseline.jsonl`. The committed screen baseline is stale on main.
  This program did not touch it. The final head was compared against the c2c25707 nightly's own screen instead.
* The frozen scale targets missed at the start: 110, plus 3 report-only.
* mixed-10000's wall time: its flyers' `explore` phase is about 62-70 ms a tick, outside Legion.
* `crusades_client_network`'s single parallel-load flake: seen once at stage 6, not here.

**New, found in this final validation:**

| Item | Bisected to | Status |
|---|---|---|
| `legion_cost`: cost-corner N200 / N400 and cost-open N100 `work.still_units_processed` over 1.10x the stage-4 baseline (2,434 / 4,871 / 1,783 against 2,175 / 3,613 / 1,584) | stage 7 (corner), stage 8 (open); legion_cost was not run at stages 7-8 | **Diagnosed and licensed** (83cf07a8). The counter counts bodies `scanStill` samples. Stage 7 made held members with an order (`jamStill`) soft bodies so planning sees knots; stage 8 keeps more members holding near their slots. Whole-run Legion work on the fixtures moves -0.6 % to +0.8 %; the scaling gates are unchanged. Only the three entries changed, each with its reason and old value. |
| Crowdbench opposingcolumns 500x4 settled arrivals 1,101 -> 955 (-13 %) | stage 4 (`c7fade44`: 952) | **open, not diagnosed** |
| Crowdbench jagged 2000x1 671 -> 521 (-22 %) | stage 6 (`15501f03`: 532; stage 8 521) | **open, not diagnosed** |
| Crowdbench dynamicobstacle 500x4 1,782 -> 1,540 (-14 %) | stage 8 (`16cd026b`) | **open, not diagnosed** |

The crowdbench cases use per-unit goals (no explicit orders). The same screen improved sharedgoal 2000x1
(1,125 -> 1,500), sharedgoal 500x4 (1,674 -> 1,897), jagged 500x4 (1,315 -> 1,402) and mixedfootprints 500x4
(1,942 -> 2,000). The three losses were found here, after the stages that caused them. They are recorded as open
rather than licensed. Reproduction: `final/bisect/<sha>/crowdbench --mode legion --scenario S --units U --players P
--moving-percent 100 --ticks 6000 --seed 0`.

**New relative to c2c25707, recorded in the stage records and still open.** These are frozen targets met at the start
and missed now:

| Target (Legion) | Frozen | c2c25707 | Final | Cause (stage record) |
|---|---|---|---|---|
| one-100 `arrive.t99` | max 2,588 | 2,465 | 2,787 | stage 3: the 100-body click now starts at once (no 64-a-tick drain), so the column packs differently; last body 2,627 -> 3,037 |
| many-100 `arrive.n` | min 98 | 98 | 96 | stage 8: shared areas; five-offset sum unchanged (7.8) |
| many-100 / many-2500 `complete_outside_n` | max 2 / 50 | 2 / 10 | 5 / 64 | stage 8: unrelated squads clicked to a tile another squad still gathers at share its area (7.8) |
| churn-1000 / churn-10000 `livelock_end` | max 0 | 0 / 0 | 3 / 75 | stage 3: explicit orders keep queued bodies' orders instead of settling them at the crowd's edge (fix round, AR-08) |
| churn-10000 `stuck_end` | max 1,000 | 0 | 1,766 | the same trade. At c2c25707 9,999 orders completed, **7,828 of them outside the disc**: bodies were declared done where they stood, which the brief forbids. Now 7,407 complete (1,847 outside, mostly the unreachable tenth) and the rest keep their orders. |
| many-10000 / maze-10000 tick (report only) | 0 over 33.3 ms | 0 / 0 | 3,440 / 199 | section 10: far more bodies move |

Also worse than at the start, in rows whose target was missed both before and after:

* mixed-10000: arrivals 2,430 -> 2,265; stuck 4,769 -> 6,459. Stage 3: its 13 convoys of mixed classes now start at
  once as one order.
* swap-1000: stuck 2 -> 50. Before, its completions outside the disc were 171; now 8. The bodies that used to be
  retired short of the disc now hold with live orders.

Section 6 shows the gains next to these.

## 6. 10,000-unit results by workload

### Frozen targets (`targets.json`, frozen 2026-10-15 from c2c25707, never re-frozen)

| target kind | what it asks | targets | met at c2c25707 | met at the final head |
|---|---|---|---|---|
| Retail state hash | Retail unchanged from c2c25707 | 12 | 12 | 12 |
| arrive.n | physical arrival of 99 % (one, swap, idle) or 97 % (the others) of the reachable bodies | 23 | 2 | 3 |
| arrive.t90 | small populations: no regression against c2c25707 (5 %) | 2 | 2 | 2 |
| arrive.t99 | one-N within 1.5x, swap-N within 2x the slowest straight run; one-100 no regression (5 %) | 9 | 1 | 0 |
| complete_outside_n | at most 2% of the reachable orders complete outside the arrival disc | 23 | 12 | 18 |
| deliver.max | a whole click delivered within 30 ticks | 23 | 10 | 23 |
| gate samples | traffic uses the alternative bridges, not only the nearest | 4 | 0 | 4 |
| livelock_end | no body walks in place at the end | 25 | 5 | 5 |
| reach.ring.ever_end | attack rows: no fewer attackers get a shot than at the start | 2 | 2 | 2 |
| start.last | the last member of a click starts moving within 3 s | 23 | 10 | 17 |
| stuck_end | no reachable body stuck at the end | 23 | 4 | 4 |
| wait.max | no body waits more than 2 s for usable navigation | 25 | 24 | 25 |
| wall.over_budget (report only) | report only: no tick over 33.3 ms | 9 | 7 | 6 |
| wall.tick_p99_ms (report only) | report only: within the 30 Hz budget on the reference machine | 9 | 8 | 7 |
| **all** | | 212 | 99 | 128 |

### Before / after, every scale row

"Before" = c2c25707 (`base/scale2`, stage 1). The scatter-*, room2door and sched-load rows come from `before-bin` in
this validation; sched-mix needs a stage-4 directive and has no "before". "After" = the final head. Values are ticks
from each body's click (30 ticks = 1 s).

#### Responsiveness: command delivery, first movement, navigation service (ticks; 30 ticks = 1 s)

| scenario | mode | bodies | deliver max | start t50 | start last | wait max (nav) | request age max (waiting / first route) |
|---|---|---|---|---|---|---|---|
| one-100 | legion | 100 | 1 -> 0 | 4 -> 4 | 9 -> 8 | 0 -> 0 | 0 / 1 |
| one-100 | retail | 100 | 1 -> 1 | 2 -> 2 | 4 -> 4 |  |  |
| one-1000 | legion | 1000 | 23 -> 0 | 11 -> 4 | 65 -> 41 | 0 -> 0 | 0 / 1 |
| one-1000 | retail | 1000 | 23 -> 23 | 9 -> 9 | 26 -> 26 |  |  |
| one-2500 | legion | 2500 | 71 -> 0 | 39 -> 7 | 90 -> 38 | 0 -> 3 | 1 / 0 |
| one-5000 | legion | 5000 | 150 -> 0 | 77 -> 10 | 219 -> 106 | 0 -> 6 | 1 / 0 |
| one-10000 | legion | 10000 | 308 -> 0 | 160 -> 14 | 652 -> 92 | 0 -> 11 | 1 / 0 |
| one-10000 | retail | 10000 | 308 -> 308 | 151 -> 151 | 310 -> 310 |  |  |
| many-100 | legion | 100 | 0 -> 0 | 5 -> 5 | 9 -> 9 | 0 -> 0 | 0 / 1 |
| many-100 | retail | 100 | 0 -> 0 | 2 -> 2 | 3 -> 3 |  |  |
| many-1000 | legion | 1000 | 0 -> 0 | 5 -> 5 | 19 -> 19 | 0 -> 0 | 0 / 1 |
| many-1000 | retail | 1000 | 0 -> 0 | 2 -> 2 | 4 -> 4 |  |  |
| many-2500 | legion | 2500 | 0 -> 0 | 5 -> 5 | 217 -> 18 | 13 -> 2 | 150 / 17 |
| many-5000 | legion | 5000 | 0 -> 0 | 6 -> 6 | 67 -> 27 | 73 -> 5 | 1062 / 1058 |
| many-10000 | legion | 10000 | 0 -> 0 | 6 -> 6 | 248 -> 80 | 25 -> 10 | 2346 / 2209 |
| many-10000 | retail | 10000 | 0 -> 0 | 2 -> 2 | 4 -> 4 |  |  |
| scatter-400 | legion | 400 | 0 -> 0 | 7 -> 5 | 17 -> 17 | 3 -> 1 | 1 / 1 |
| scatter-2500 | legion | 2500 | 0 -> 0 | 7 -> 7 | 17 -> 17 | 0 -> 688 | 787 / 788 |
| scatter-10000 | legion | 10000 | 0 -> 0 | 7 -> 7 | 17 -> 18 | 0 -> 2349 | 2641 / 2637 |
| swap-1000 | legion | 1000 | 23 -> 0 | 12 -> 11 | 45 -> 25 | 0 -> 8 | 1 / 0 |
| swap-1000 | retail | 1000 | 23 -> 23 | 9 -> 9 | 26 -> 26 |  |  |
| swap-2500 | legion | 2500 | 71 -> 0 | 40 -> 11 | 145 -> 72 | 0 -> 8 | 1 / 0 |
| swap-5000 | legion | 5000 | 150 -> 0 | 77 -> 11 | 243 -> 38 | 0 -> 8 | 1 / 0 |
| swap-10000 | legion | 10000 | 308 -> 0 | 167 -> 11 | 437 -> 164 | 0 -> 9 | 1 / 0 |
| bridges-1000 | legion | 1000 | 23 -> 0 | 14 -> 11 | 45 -> 25 | 0 -> 8 | 1 / 0 |
| bridges-1000 | retail | 1000 | 23 -> 23 | 9 -> 9 | 26 -> 26 |  |  |
| bridges-10000 | legion | 10000 | 308 -> 0 | 169 -> 11 | 526 -> 84 | 0 -> 9 | 1 / 0 |
| maze-1000 | legion | 1000 | 23 -> 0 | 11 -> 15 | 32 -> 24 | 0 -> 15 | 2 / 0 |
| maze-1000 | retail | 1000 | 23 -> 23 | 9 -> 9 | 25 -> 25 |  |  |
| maze-10000 | legion | 10000 | 308 -> 0 | 160 -> 17 | 546 -> 42 | 0 -> 16 | 1 / 36 |
| mixed-1000 | legion | 1000 | 53 -> 0 | 44 -> 8 | 60 -> 15 | 0 -> 6 | 1 / 0 |
| mixed-1000 | retail | 1000 | 53 -> 53 | 44 -> 44 | 54 -> 54 |  |  |
| mixed-10000 | legion | 10000 | 622 -> 0 | 473 -> 22 | 623 -> 49 | 0 -> 41 | 1 / 0 |
| attack-1000 | legion | 1100 | 33 -> 0 | 19 -> 11 | 61 -> 30 | 0 -> 6 | 1 / 1 |
| attack-1000 | retail | 1100 | 33 -> 33 | 18 -> 18 | 35 -> 35 |  |  |
| attack-10000 | legion | 11000 | 339 -> 0 | 176 -> 12 | 1084 -> 131 | 1 -> 9 | 1 / 1 |
| churn-1000 | legion | 1000 | 23 -> 0 | 7 -> 3 | 117 -> 93 | 0 -> 2 | 1 / 1 |
| churn-1000 | retail | 1000 | 23 -> 23 | 9 -> 9 | 726 -> 726 |  |  |
| churn-10000 | legion | 10000 | 498 -> 0 | 312 -> 6 | 1637 -> 1039 | 0 -> 13 | 1 / 1 |
| idle-10000 | legion | 1000 | 23 -> 0 | 12 -> 10 | 68 -> 41 | 0 -> 6 | 1 / 0 |
| room2door | legion | 100 | 1 -> 0 | 3 -> 3 | 12 -> 11 | 0 -> 0 | 0 / 1 |
| sched-load | legion | 480 | 0 -> 0 | 9 -> 9 | - -> - | 0 -> 1 | 153 / 138 |
| sched-mix | legion | 480 | (new) 0 | (new) 10 | (new) 23 | (new) 207 | 485 / 455 |

#### Arrival: physical arrival in the click's packed disc vs order completion

t-columns: ticks from each body's click until physical arrival; "-" = that quantile never arrived. "outside" = orders completed outside the disc (the unreachable share of churn/maze retires outside by design).

| scenario | mode | bodies | arrived | arrive t50 | t90 | t99 | last | completed (outside) | stuck / livelock at end | path p90 (x1000 of straight) | peak 300-tick throughput |
|---|---|---|---|---|---|---|---|---|---|---|---|
| one-100 | legion | 100 | 100 -> 100 | 1661 -> 1653 | 2235 -> 2186 | 2465 -> 2787 | 2627 -> 3037 | 100 (0) -> 100 (0) | 0 / 0 -> 0 / 0 | 1008 -> 1007 | 31 -> 30 |
| one-100 | retail | 100 | 100 -> 100 | 2037 -> 2037 | 2553 -> 2553 | 2704 -> 2704 | 2765 -> 2765 | 100 (0) -> 100 (0) | 0 / 0 -> 0 / 0 | 1179 -> 1179 | 28 -> 28 |
| one-1000 | legion | 1000 | 809 -> 999 | 2879 -> 2786 | - -> 4041 | - -> 5282 | - -> - | 999 (191) -> 971 (0) | 1 / 1 -> 1 / 0 | 1061 -> 1067 | 116 -> 119 |
| one-1000 | retail | 1000 | 261 -> 261 | - -> - | - -> - | - -> - | - -> - | 121 (0) -> 121 (0) | 94 / 5 -> 94 / 5 | 1257 -> 1257 | 40 -> 40 |
| one-2500 | legion | 2500 | 808 -> 2209 | - -> 3617 | - -> - | - -> - | - -> - | 2317 (1519) -> 2061 (5) | 60 / 17 -> 226 / 14 | 1080 -> 1089 | 119 -> 188 |
| one-5000 | legion | 5000 | 1532 -> 4075 | - -> 4776 | - -> - | - -> - | - -> - | 4316 (2790) -> 3755 (9) | 449 / 34 -> 777 / 26 | 1079 -> 1092 | 167 -> 261 |
| one-10000 | legion | 10000 | 1866 -> 7468 | - -> 6256 | - -> - | - -> - | - -> - | 6257 (4394) -> 6773 (11) | 1741 / 95 -> 2291 / 31 | 1084 -> 1093 | 186 -> 366 |
| one-10000 | retail | 10000 | 595 -> 595 | - -> - | - -> - | - -> - | - -> - | 71 (0) -> 71 (0) | 8387 / 0 -> 8387 / 0 | 1085 -> 1085 | 115 -> 115 |
| many-100 | legion | 100 | 98 -> 96 | 475 -> 475 | 1616 -> 1616 | - -> - | - -> - | 98 (2) -> 100 (5) | 1 / 0 -> 0 / 0 | 1459 -> 1408 | 32 -> 33 |
| many-100 | retail | 100 | 92 -> 92 | 689 -> 689 | 1144 -> 1144 | - -> - | - -> - | 100 (8) -> 100 (8) | 0 / 0 -> 0 / 0 | 2884 -> 2884 | 41 -> 41 |
| many-1000 | legion | 1000 | 794 -> 803 | 3149 -> 2966 | - -> - | - -> - | - -> - | 786 (25) -> 797 (23) | 92 / 22 -> 30 / 12 | 1521 -> 1685 | 95 -> 105 |
| many-1000 | retail | 1000 | 813 -> 813 | 3030 -> 3030 | - -> - | - -> - | - -> - | 993 (185) -> 993 (185) | 0 / 0 -> 0 / 0 | 1679 -> 1679 | 88 -> 88 |
| many-2500 | legion | 2500 | 785 -> 1425 | - -> 6511 | - -> - | - -> - | - -> - | 647 (10) -> 1434 (64) | 1395 / 48 -> 310 / 92 | 1446 -> 1509 | 57 -> 102 |
| many-5000 | legion | 5000 | 470 -> 2575 | - -> 10500 | - -> - | - -> - | - -> - | 275 (13) -> 2573 (70) | 3819 / 72 -> 1153 / 182 | 1242 -> 1437 | 37 -> 127 |
| many-10000 | legion | 10000 | 546 -> 4875 | - -> - | - -> - | - -> - | - -> - | 267 (15) -> 4864 (131) | 9164 / 100 -> 2363 / 284 | 1190 -> 1376 | 39 -> 169 |
| many-10000 | retail | 10000 | 587 -> 587 | - -> - | - -> - | - -> - | - -> - | 313 (0) -> 313 (0) | 8695 / 2 -> 8695 / 2 | 1161 -> 1161 | 43 -> 43 |
| scatter-400 | legion | 400 | 162 -> 398 | - -> 1303 | - -> 3006 | - -> 3762 | - -> - | 158 (1) -> 399 (2) | 232 / 3 -> 0 / 0 | 1130 -> 1213 | 49 -> 77 |
| scatter-2500 | legion | 2500 | 129 -> 1714 | - -> 5617 | - -> - | - -> - | - -> - | 122 (0) -> 1711 (4) | 2356 / 3 -> 206 / 54 | 1089 -> 1156 | 26 -> 117 |
| scatter-10000 | legion | 10000 | 76 -> 5068 | - -> 14021 | - -> - | - -> - | - -> - | 74 (0) -> 5061 (14) | 9858 / 8 -> 1373 / 194 | 1073 -> 1162 | 8 -> 209 |
| swap-1000 | legion | 1000 | 824 -> 863 | 8074 -> 8097 | - -> - | - -> - | - -> - | 990 (171) -> 845 (8) | 2 / 2 -> 50 / 14 | 1117 -> 1114 | 108 -> 100 |
| swap-1000 | retail | 1000 | 0 -> 0 | - -> - | - -> - | - -> - | - -> - | 0 (0) -> 0 (0) | 461 / 1 -> 461 / 1 | -1 -> -1 | 0 -> 0 |
| swap-2500 | legion | 2500 | 1721 -> 2052 | 8809 -> 8974 | - -> - | - -> - | - -> - | 2027 (396) -> 1741 (0) | 5 / 4 -> 26 / 10 | 1175 -> 1187 | 117 -> 172 |
| swap-5000 | legion | 5000 | 1308 -> 2115 | - -> - | - -> - | - -> - | - -> - | 1474 (195) -> 1876 (1) | 2521 / 46 -> 1637 / 51 | 1202 -> 1196 | 122 -> 159 |
| swap-10000 | legion | 10000 | 915 -> 1644 | - -> - | - -> - | - -> - | - -> - | 378 (0) -> 1296 (0) | 8037 / 86 -> 5919 / 136 | 1256 -> 1294 | 69 -> 81 |
| bridges-1000 | legion | 1000 | 10 -> 653 | - -> 13157 | - -> - | - -> - | - -> - | 8 (0) -> 587 (0) | 537 / 34 -> 32 / 2 | 1292 -> 1979 | 2 -> 51 |
| bridges-1000 | retail | 1000 | 2 -> 2 | - -> - | - -> - | - -> - | - -> - | 0 (0) -> 0 (0) | 883 / 0 -> 883 / 0 | 1664 -> 1664 | 1 -> 1 |
| bridges-10000 | legion | 10000 | 0 -> 284 | - -> - | - -> - | - -> - | - -> - | 0 (0) -> 204 (0) | 9020 / 79 -> 4738 / 76 | -1 -> 3429 | 0 -> 23 |
| maze-1000 | legion | 1000 | 365 -> 950 | - -> 10868 | - -> 17394 | - -> - | - -> - | 655 (291) -> 979 (50) | 29 / 15 -> 0 / 0 | 1168 -> 1654 | 18 -> 42 |
| maze-1000 | retail | 1000 | 140 -> 140 | - -> - | - -> - | - -> - | - -> - | 116 (44) -> 116 (44) | 414 / 1 -> 414 / 1 | 1824 -> 1824 | 11 -> 11 |
| maze-10000 | legion | 10000 | 1468 -> 2395 | - -> - | - -> - | - -> - | - -> - | 1142 (289) -> 1798 (312) | 6139 / 127 -> 4594 / 173 | 1299 -> 2237 | 208 -> 146 |
| mixed-1000 | legion | 1000 | 636 -> 630 | 7515 -> 7414 | - -> - | - -> - | - -> - | 448 (0) -> 468 (0) | 52 / 16 -> 39 / 19 | 1430 -> 1429 | 82 -> 47 |
| mixed-1000 | retail | 1000 | 100 -> 100 | - -> - | - -> - | - -> - | - -> - | 0 (0) -> 0 (0) | 371 / 5 -> 371 / 5 | 995 -> 995 | 41 -> 41 |
| mixed-10000 | legion | 10000 | 2430 -> 2265 | - -> - | - -> - | - -> - | - -> - | 1978 (0) -> 1911 (0) | 4769 / 174 -> 6459 / 112 | 1179 -> 1213 | 138 -> 136 |
| attack-1000 | legion | 1100 | 99 -> 99 | 3182 -> 3229 | 4264 -> 4055 | 5741 -> 4545 | 5741 -> 4545 | 98 (0) -> 99 (0) | 0 / 0 -> 0 / 0 | 998 -> 997 | 27 -> 30 |
| attack-1000 | retail | 1100 | 100 -> 100 | 3584 -> 3584 | 4487 -> 4487 | 4708 -> 4708 | 4739 -> 4739 | 97 (0) -> 97 (0) | 0 / 0 -> 0 / 0 | 1070 -> 1070 | 20 -> 20 |
| attack-10000 | legion | 11000 | 895 -> 946 | 3033 -> 3140 | - -> 4630 | - -> - | - -> - | 971 (90) -> 932 (10) | 5 / 3 -> 18 / 9 | 1057 -> 1058 | 127 -> 118 |
| churn-1000 | legion | 1000 | 465 -> 833 | - -> 2421 | - -> - | - -> - | - -> - | 1000 (539) -> 979 (200) | 0 / 0 -> 5 / 3 | 1085 -> 1058 | 81 -> 109 |
| churn-1000 | retail | 1000 | 351 -> 351 | - -> - | - -> - | - -> - | - -> - | 272 (29) -> 272 (29) | 38 / 13 -> 38 / 13 | 1403 -> 1403 | 40 -> 40 |
| churn-10000 | legion | 10000 | 2174 -> 5984 | - -> 6570 | - -> - | - -> - | - -> - | 9999 (7828) -> 7407 (1847) | 0 / 0 -> 1766 / 75 | 1088 -> 1114 | 213 -> 363 |
| idle-10000 | legion | 1000 | 32 -> 38 | - -> - | - -> - | - -> - | - -> - | 28 (0) -> 25 (0) | 685 / 25 -> 60 / 3 | 1089 -> 1097 | 8 -> 7 |
| room2door | legion | 100 | 84 -> 100 | 1674 -> 1491 | - -> 2154 | - -> 2344 | - -> 2378 | 100 (19) -> 100 (0) | 0 / 0 -> 0 / 0 | 1101 -> 1118 | 23 -> 27 |
| sched-load | legion | 480 | 31 -> 30 | - -> - | - -> - | - -> - | - -> - | 17 (0) -> 26 (1) | 164 / 33 -> 121 / 36 | 1087 -> 1093 | 16 -> 12 |
| sched-mix | legion | 480 | (new) 416 | (new) 296 | (new) - | (new) - | (new) - |  -> 412 (1) |  -> 0 / 0 | (new) 1050 | (new) 233 |

## 7. Command latency

* **Scale suite.** `deliver.max` is the ticks from a click until its last member's command is applied.
  * It is **0 in every Legion row**. Before, it was 23 at 1,000 bodies, 308 at one-10000, 498 at churn-10000 and
    622 at mixed-10000. Retail is unchanged (it keeps the 64-commands-a-tick path).
  * The last member starts moving (`start.last`) at 92 ticks in one-10000 (was 652), 164 in swap-10000 (437), 131 in
    attack-10000 (1,084), 42 in maze-10000 (546), 49 in mixed-10000 (623) and 1,039 in churn-10000 (1,637; four
    re-clicks).
  * 17 of 23 `start.last` targets are met. The misses are the biggest single clicks, where the front of the crowd
    blocks the rear's first step.
* **Real client/server path**, about 18,000 bodies, two players at a 10,000-unit harness cap. Each formation order is
  SetSquad + AttackMove for every body:

  | order | Retail: commands, first / last applied | Legion: first = last applied |
  |---|---|---|
  | issued 600 | 18,002 commands, 605 / 950 (345 ticks, 11.5 s) | 18,016 members, 603 (one tick) |
  | issued ~1,203 | 17,638, 1,232 / never (superseded after 13,696) | 17,682, 1,253 |
  | issued ~1,805 | 16,880, 1,811 / never (superseded after 11,392) | 16,840, 1,860 |
  | issued ~2,411 | 15,762, 2,411 / 2,986 | 15,654, 2,465 |
  | issued ~3,011 | 14,318, 3,014 / 3,431 | 14,214, 3,067 |

  At c2c25707 both modes used the Retail column's path. Delivering one order took longer than the interval between
  orders, so the next order replaced it before it landed. Now each Legion order lands whole in one tick.
  * The gap between issue and landing (3 ticks for the first order, 50-53 for the later ones) is the headless client's
    playout lead under an 18,000-body simulation, not queueing. The order lands in a single bundle.
  * The order is **not** split into separate formations or crowds. One click is one order and one arrival area at
    any size; before, the 33-tick convoy limit made 20 groups of it.
* **Unavoidable O(N) work.** The activation tick applies every member in id order with the ordinary per-member
  ownership and existence checks. That costs 1.8-3.1 ms in the 10,000-body single-click runs (`apply_max_ms`, one-10000,
  swap, maze, attack). At stage 3 it was 39 ms, mostly a synchronous plane build that stage 4 moved into budgeted
  background work. Registration past the tick's budget is deferred FIFO (`sched.reg_deferred`; one-10000 needed none).
  Within a sender nothing overtakes: a Stop behind a big order lands in the same tick when both fit the budgets.

## 8. Navigation waiting time and queue age

* `wait.max` (the longest any body stood in Waiting with no usable route) is at most 41 ticks in every
  single-selection row. It is 10 in many-10000, which was 25 at the start and 5,169-5,314 at stage 4 before tiles.
  25/25 `wait.max` targets are met.
* `sched.age_max.*` (the longest a navigation request sat in the scheduler) is 0-36 ticks in the single-click rows.
* In the **many unrelated destinations** rows the queue is the bottleneck:
  * scatter-10000: `wait.max` 2,349, `wait.p99` 547, request age 2,641.
  * scatter-2500: `wait.max` 688.
  * many-10000: request age 2,346 / 2,209 (waiting / first route).
  * A request's age there is the time to drain the queue ahead of it at the fixed deterministic quota (384k relaxations
    a tick). Service inside each class is fair (stage 4). Meanwhile a group with a previous route keeps steering by it,
    which is why many-10000's bodies rarely enter Waiting.
  * Raising the quota is a behaviour change. It was not made, so a slower machine never sees a different schedule.

## 9. Arrival tails and physical arrival versus completion

The tables in section 6 keep the two apart: arrival means the body is physically in the click's packed disc; completion
means the order finished.

* **The starting revision completed orders where bodies stood.** At 10,000 bodies:
  * one-10000: 6,257 completions, 4,394 outside the disc;
  * churn-10000: 9,999 completions, 7,828 outside the disc;
  * one-5000: 2,790 outside of 4,316.
* **Now completions outside the disc are rare.** one-10000: 11; one-5000: 9; swap-*: 0-8; maze-1000: 291 -> 50. The
  exceptions are the unreachable tenth of churn and maze, and the many-* / scatter-* squads that share a merged area
  (up to 131). 18 of 23 `complete_outside_n` targets are met (12 at the start).
* **Physical arrival at 10,000 bodies**, before -> after:

  | row | arrived (before -> after) | factor |
  |---|---|---|
  | one-10000 | 1,866 -> 7,468 | 4.0x |
  | many-10000 | 546 -> 4,875 | 8.9x |
  | scatter-10000 | 76 -> 5,068 | 67x |
  | swap-10000 | 915 -> 1,644 | 1.8x |
  | maze-10000 | 1,468 -> 2,395 | 1.6x |
  | churn-10000 | 2,174 -> 5,984 | 2.8x |
  | bridges-10000 | 0 -> 284 | -- |
  | attack-10000 | 895 -> 946 | 1.06x (of 996 reachable) |
  | idle-10000 | 32 -> 38 | of 1,000 |
  | mixed-10000 | 2,430 -> 2,265 | worse |

  Throughput (the best 300-tick window of arrivals) rises in one-10000 (186 -> 366), many-10000 (39 -> 169),
  scatter-10000 (8 -> 209) and churn-10000 (213 -> 363).
* **Smaller populations.**
  * one-1000 999/1,000 (809); one-2500 2,209 (808); one-5000 4,075 (1,532).
  * maze-1000 950 (365); bridges-1000 653 (10); scatter-400 398/400 (162); room2door 100/100 (84).
* **Tails.** t99 is reached only where nearly everyone arrives:
  * one-100: 2,787, a regression (section 5).
  * one-1000: 5,282, against 4,350 frozen.
  * attack-1000: 4,545 (was 5,741).
  * scatter-400: 3,762; room2door: 2,344.

  No 2,500-plus row reaches t99. 0 of 9 t99 targets are met; at the start one was (one-100).
* **Path length.** p90 path length against the straight line rises in the passage rows: bridges-1000 1.29 -> 1.98,
  maze-10000 1.30 -> 2.24, maze-1000 1.17 -> 1.65. Traffic now spreads over the alternative bridges and doors, and
  4/4 bridge-use targets are met (0 at the start). The single-click open rows stay within 1 % of before; many-10000
  (+16 %) and scatter-10000 (+8 %) now route round traffic.
* **Still standing at the end** (`stuck_end`):

  | row | stuck at the end (before -> after) |
  |---|---|
  | one-10000 | 1,741 -> 2,291 |
  | swap-10000 | 8,037 -> 5,919 |
  | many-10000 | 9,164 -> 2,363 |
  | bridges-10000 | 9,020 -> 4,738 |
  | maze-10000 | 6,139 -> 4,594 |
  | mixed-10000 | 4,769 -> 6,459 |
  | churn-10000 | 0 -> 1,766 (section 5) |

  4 of 23 `stuck_end` targets and 5 of 25 `livelock_end` targets are met, the same as at the start. The causes are in
  section 13.

## 10. Whole-simulation tick

**A/B, the same 5,000 ticks of each 10,000-body workload.** Before (c2c25707's simulation, with the vertex probe on as
in that revision's game) against after (the final head, probe off as in today's game). Both binaries run at once on
cores 8-11 / 12-15, then swap; values are the mean of the two runs. Settings: no observer, three workers, `TAK_PHASE`
per-phase means (ms per tick).

| scenario (5,000 ticks) | tick mean ms | tick p99 ms | tick max ms | ticks over 33.3 ms (of 5,000) | movement ms | nav ms | separation ms | explore ms |
|---|---|---|---|---|---|---|---|---|
| one-10000 | 15.80 -> 14.04 | 21.0 -> 16.2 | 24.6 -> 19.5 | 0 -> 0 | 11.58 -> 9.67 | 0.65 -> 0.54 | 0.95 -> 1.27 | 0.64 -> 0.95 |
| many-10000 | 12.75 -> 27.52 | 15.2 -> 39.0 | 17.5 -> 55.5 | 0 -> 657 | 6.75 -> 17.39 | 2.58 -> 5.06 | 0.85 -> 1.24 | 0.54 -> 0.84 |
| scatter-10000 | 12.24 -> 24.17 | 16.5 -> 40.3 | 21.3 -> 45.8 | 0 -> 114 | 5.84 -> 10.12 | 3.05 -> 9.30 | 0.71 -> 1.00 | 0.36 -> 0.59 |
| swap-10000 | 14.47 -> 13.73 | 18.4 -> 17.0 | 21.9 -> 21.3 | 0 -> 0 | 10.25 -> 9.60 | 0.83 -> 0.70 | 0.87 -> 0.89 | 0.58 -> 0.59 |
| bridges-10000 | 15.45 -> 13.28 | 20.3 -> 20.4 | 22.7 -> 24.8 | 0 -> 0 | 11.59 -> 8.97 | 0.74 -> 1.04 | 0.69 -> 0.77 | 0.43 -> 0.52 |
| maze-10000 | 17.59 -> 15.37 | 19.8 -> 23.0 | 22.9 -> 27.2 | 0 -> 0 | 13.57 -> 9.82 | 0.57 -> 1.92 | 0.86 -> 0.99 | 0.57 -> 0.71 |
| attack-10000 | 10.16 -> 12.47 | 16.2 -> 15.9 | 17.8 -> 20.5 | 0 -> 0 | 6.11 -> 7.88 | 0.77 -> 1.11 | 0.67 -> 0.75 | 0.41 -> 0.47 |
| churn-10000 | 17.08 -> 14.13 | 24.1 -> 18.3 | 31.9 -> 22.6 | 0 -> 0 | 12.09 -> 9.27 | 1.15 -> 1.08 | 1.18 -> 1.19 | 0.86 -> 0.89 |
| idle-10000 | 3.92 -> 3.60 | 4.7 -> 4.3 | 8.9 -> 11.1 | 0 -> 0 | 1.76 -> 1.38 | 0.08 -> 0.10 | 0.32 -> 0.32 | 0.15 -> 0.15 |
| mixed-10000 | 78.32 -> 83.06 | 129.5 -> 135.3 | 206.0 -> 209.3 | 4610 -> 4988 | 12.38 -> 8.12 | 0.62 -> 0.58 | 61.93 -> 70.77 | 61.56 -> 70.42 |

* **Same work, cheaper.** one-10000, swap-10000, bridges-10000, maze-10000, churn-10000 and idle-10000 move as many
  bodies or more and are 5-17 % cheaper per tick. Movement falls 0.4-3.8 ms a tick. Part of that is stage 2's exact
  reductions: the vertex probe alone was 5-7 % of the tick in the game. The rest was not attributed.
* **More moving, dearer.** many-10000 (2.2x) and scatter-10000 (2.0x) are dearer because far more bodies move. In their
  full scale runs, moving unit-ticks rose 15.8 M -> 62.5 M and 6.6 M -> 51.5 M. Movement cost per moving unit-tick fell
  6.45 -> 4.01 µs and 10.25 -> 3.19 µs. The starting revision was cheap because most of its bodies stood still.
  * many-10000 runs 13 % of its ticks over 33.3 ms (p99 39 ms, max 56 ms).
  * scatter-10000 runs 2 % over (p99 40 ms).
  * Its `nav` phase is 9.3 ms: tile and field service for 10,000 unrelated destinations, 86 % of the tile builds already
    on workers.
* **attack-10000 is +23 %, not explained.** Its moving unit-ticks fell over the full run (14.1 M -> 12.6 M) while
  movement per moving unit-tick rose 2.35 -> 3.05 µs. Not diagnosed; listed in section 13.
* **mixed-10000 is over budget in both** (78 -> 83 ms). Its separation and explore phases, outside Legion, rose
  62 -> 71 ms; Legion's movement there fell 12.4 -> 8.1 ms.

**Scale-suite wall columns** (single runs, three jobs at a time, indicative only):

nav est = the navigator's own capacity estimate (`scale.nav.bytes_peak`, both revisions); navbytes = stage 4's deterministic byte accounting (after only). Tick columns: whole `World::tick` wall time, three workers.

| scenario | mode | moving unit-ticks | nav est MB | navbytes MB | RSS MB | tick mean ms | tick p99 ms | tick max ms | ticks over 33.3 ms | movement mean ms | nav mean ms | movement us per moving unit-tick |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| one-10000 | legion | 41147618 -> 65618822 | 61 -> 15 | 58 | 180.9 -> 138.4 | 12.5 -> 10.6 | 18.6 -> 16.3 | 27.7 -> 19.2 | 0 -> 0 | 9.1 -> 6.8 | 0.4 -> 0.4 | 3.58 -> 1.68 |
| one-10000 | retail | 9919745 -> 9919745 |  |  | 123.2 -> 123.7 | 8.4 -> 9.7 | 9.4 -> 24.1 | 15.1 -> 61.2 | 0 -> 39 | 5.6 -> 6.2 | 0.0 -> 0.0 | 9.02 -> 10.05 |
| many-10000 | legion | 15789441 -> 62473380 | 180 -> 89 | 220 | 354.0 -> 321.3 | 13.2 -> 26.7 | 15.3 -> 45.1 | 16.9 -> 66.2 | 0 -> 3440 | 7.1 -> 17.4 | 2.7 -> 4.4 | 6.45 -> 4.01 |
| many-10000 | retail | 12760999 -> 12760999 |  |  | 173.3 -> 172.6 | 12.7 -> 13.3 | 15.4 -> 20.9 | 19.7 -> 74.7 | 0 -> 37 | 8.8 -> 9.0 | 0.0 -> 0.0 | 9.88 -> 10.11 |
| scatter-10000 | legion | 6571659 -> 51536876 | 222 -> 170 | 368 | 332.3 -> 495.5 | 10.5 -> 26.9 | 15.1 -> 44.9 | 59.0 -> 52.6 | 1 -> 478 | 4.7 -> 11.4 | 2.9 -> 10.5 | 10.25 -> 3.19 |
| swap-10000 | legion | 27158688 -> 31455107 | 47 -> 18 | 63 | 167.6 -> 141.8 | 12.1 -> 14.3 | 16.7 -> 19.0 | 20.5 -> 23.1 | 0 -> 0 | 8.7 -> 10.3 | 0.5 -> 0.6 | 3.65 -> 3.72 |
| bridges-10000 | legion | 19352396 -> 48397223 | 47 -> 36 | 100 | 164.8 -> 216.3 | 15.2 -> 15.6 | 20.5 -> 24.2 | 26.7 -> 30.3 | 0 -> 0 | 11.1 -> 11.0 | 0.6 -> 1.0 | 8.76 -> 3.48 |
| maze-10000 | legion | 38024290 -> 63345429 | 67 -> 52 | 175 | 236.5 -> 328.3 | 17.2 -> 18.6 | 20.4 -> 33.3 | 26.5 -> 135.1 | 0 -> 199 | 13.0 -> 12.0 | 0.5 -> 2.4 | 6.82 -> 3.80 |
| mixed-10000 | legion | 81083579 -> 65862591 | 65 -> 37 | 244 | 356.0 -> 351.3 | 61.9 -> 54.1 | 103.4 -> 107.7 | 198.9 -> 210.9 | 19013 -> 19160 | 12.0 -> 7.6 | 0.5 -> 0.5 | 2.86 -> 2.23 |
| attack-10000 | legion | 14138621 -> 12569444 | 66 -> 16 | 61 | 188.1 -> 143.8 | 10.1 -> 10.8 | 17.3 -> 15.9 | 19.3 -> 23.9 | 0 -> 0 | 5.5 -> 6.4 | 0.8 -> 1.0 | 2.35 -> 3.05 |
| churn-10000 | legion | 43884214 -> 70226105 | 96 -> 30 | 104 | 249.9 -> 209.3 | 10.7 -> 10.1 | 22.4 -> 17.6 | 33.6 -> 23.2 | 1 -> 0 | 7.0 -> 6.3 | 0.5 -> 0.6 | 3.03 -> 1.69 |
| idle-10000 | legion | 3140089 -> 3754476 | 5 -> 5 | 40 | 94.5 -> 100.1 | 4.1 -> 4.0 | 4.9 -> 5.0 | 9.1 -> 11.6 | 0 -> 0 | 1.8 -> 1.6 | 0.1 -> 0.1 | 7.31 -> 5.55 |

## 11. Memory

* **Deterministic navigation bytes** (`scale.navbytes.peak`, stage 4's accounting of fields, tiles, planes, portals,
  queues, scratch, metadata and in-flight builds) at 10,000 bodies:
  * 40-175 MB in the single-click rows (maze-10000 175 MB, one-10000 58 MB);
  * 220 MB in many-10000; 368 MB in scatter-10000 (132 MB fields, 198 MB tiles);
  * 244 MB in mixed-10000 (168 MB of planes for its five movement classes).
* **Not comparable with the starting revision.** That revision had no byte accounting. Its own capacity estimate
  (`scale.nav.bytes_peak`, kept in both) fell from 61 -> 15 MB (one-10000) and 180 -> 89 MB (many-10000). The two
  figures measure different things.
* **Peak RSS** (scale runs) at 10,000 bodies: 100-495 MB. scatter-10000 grows 332 -> 496 MB (now 10,000 live routes
  instead of 76 bodies arriving); one-10000 shrinks 181 -> 138, many-10000 354 -> 321.
* **Byte caps are deterministic**, never derived from free memory. They are the field allowance and the tile cache's
  64 MiB eviction; the `navbytes` scale directive and `legion_scenario --field-bytes` cap them in tests.
* Allocator overhead is reported separately as `heap_kb` on the `SCALE_WALL` line.

#### Navigation memory by component at the peak (after, MB; `scale.navbytes.*`)

| scenario | peak | fields | tiles | planes | portals | queues | scratch | metadata | inflight |
|---|---|---|---|---|---|---|---|---|---|
| one-10000 | 57.5 | 5.5 | 0.0 | 19.8 | 3.1 | 0.1 | 20.6 | 8.4 | 0.0 |
| many-10000 | 220.1 | 69.1 | 87.6 | 37.6 | 2.9 | 0.2 | 14.4 | 8.4 | 0.0 |
| scatter-10000 | 368.3 | 132.3 | 197.7 | 9.2 | 1.4 | 1.2 | 11.7 | 13.5 | 1.1 |
| swap-10000 | 63.3 | 8.4 | 0.0 | 29.3 | 2.3 | 0.1 | 14.7 | 8.5 | 0.0 |
| bridges-10000 | 100.0 | 40.9 | 0.0 | 34.9 | 2.4 | 0.1 | 13.2 | 8.4 | 0.0 |
| maze-10000 | 175.1 | 75.2 | 0.0 | 62.0 | 4.3 | 0.1 | 25.9 | 7.4 | 0.1 |
| mixed-10000 | 244.1 | 21.3 | 0.0 | 167.6 | 11.5 | 0.1 | 36.2 | 7.3 | 0.0 |
| attack-10000 | 61.0 | 11.8 | 0.0 | 22.9 | 2.5 | 0.1 | 12.9 | 10.8 | 0.0 |
| churn-10000 | 104.3 | 21.9 | 0.0 | 48.8 | 3.5 | 0.1 | 21.7 | 8.2 | 0.0 |
| idle-10000 | 40.5 | 5.1 | 0.0 | 22.5 | 1.9 | 0.1 | 10.1 | 0.9 | 0.0 |

## 12. Work by subsystem

Deterministic NavWork units (the same units the scheduler budgets), whole run / worst single tick:

| scenario | registration | awareness | field_init | field_relax | tile_build | route_search | connectivity | plane_build | plane_relabel | ring_build | slot_search | worst tick, all |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| one-10000 | 650k / 262k | 1.9M / 10k | 1.5M / 726k | 11.6M / 384k | 0 / 0 | 0 / 0 | 7.5M / 362k | 11.7M / 1.1M | 0 / 0 | 0 / 0 | 3.9M / 350k | 2.7M |
| many-10000 | 1.3M / 8k | 626.1M / 286k | 178.3M / 1.4M | 1.1G / 384k | 1.0G / 436k | 524.5M / 3.4M | 92k / 3k | 11.0M / 1.1M | 0 / 0 | 0 / 0 | 14.5M / 133k | 4.5M |
| scatter-10000 | 1.3M / 9k | 0 / 0 | 457.2M / 1.5M | 1.9G / 384k | 2.8G / 393k | 773.5M / 3.4M | 0 / 0 | 5.5M / 1.1M | 0 / 0 | 0 / 0 | 10.6M / 7k | 3.5M |
| swap-10000 | 650k / 262k | 207.8M / 61k | 6.1M / 1.1M | 40.9M / 384k | 0 / 0 | 0 / 0 | 5.7M / 266k | 9.3M / 1.1M | 0 / 0 | 0 / 0 | 2.2M / 235k | 3.0M |
| bridges-10000 | 650k / 262k | 291.5M / 36k | 21.3M / 1.1M | 159.3M / 384k | 0 / 0 | 297.0M / 1.4M | 10.4M / 308k | 9.1M / 1.1M | 0 / 0 | 0 / 0 | 3.4M / 157k | 3.0M |
| maze-10000 | 662k / 262k | 287.0M / 30k | 251.8M / 1.7M | 1.9G / 384k | 0 / 0 | 361.7M / 1.3M | 14.7M / 446k | 16.5M / 1.1M | 0 / 0 | 0 / 0 | 26.2M / 602k | 3.0M |
| mixed-10000 | 585k / 227k | 795k / 9k | 5.6M / 1.1M | 42.7M / 384k | 0 / 0 | 0 / 0 | 27.6M / 394k | 44.2M / 1.1M | 0 / 0 | 0 / 0 | 8.1M / 281k | 3.0M |
| attack-10000 | 722k / 262k | 138k / 1k | 117.7M / 1.2M | 399.9M / 384k | 0 / 0 | 0 / 0 | 6.0M / 384k | 9.5M / 1.1M | 0 / 0 | 35k / 18k | 1.0M / 99k | 2.5M |
| churn-10000 | 5.7M / 262k | 2.4M / 10k | 43.7M / 1.6M | 276.4M / 384k | 0 / 0 | 150.7M / 805k | 13.8M / 531k | 13.1M / 1.1M | 0 / 0 | 0 / 0 | 29.2M / 328k | 2.8M |
| idle-10000 | 65k / 33k | 11k / 1k | 2.7M / 443k | 21.2M / 384k | 0 / 0 | 0 / 0 | 4.6M / 447k | 7.2M / 1.1M | 0 / 0 | 0 / 0 | 544k / 63k | 2.3M |
| sched-load | 1.5M / 1k | 422k / 6k | 180.5M / 672k | 1.2G / 384k | 17.5M / 246k | 5.1M / 888k | 1.6M / 29k | 3.6M / 1.1M | 0 / 0 | 0 / 0 | 17.4M / 139k | 1.5M |
| sched-mix | 2.9M / 1k | 1.5M / 18k | 18.7M / 294k | 149.2M / 384k | 9.6M / 209k | 2.3M / 1.1M | 20k / 6k | 1.8M / 1.1M | 0 / 0 | 0 / 0 | 1.0M / 85k | 1.9M |

* **Single-click rows.** Registration of 10,000 members is 650k units, at most 262k in a tick: budgeted, not a spike.
  Plane builds peak at 1.1M a tick (the first build of a class, chunked on workers since stage 9).
* **Many-destination rows.** Work is dominated by field relaxation and tile builds (1-3 G units over the run), within
  the fixed quota per tick: the worst tick is 3.5-4.5 M units of all kinds.
* **Awareness** reaches 0.2-0.6 G units in the crossing rows (swap, bridges, maze, many). The stage-2 broad phase keeps
  its worst tick under 0.3 M units.
* **Wall time by phase** (A/B above): `movement` (serial per-unit steering and the authoritative collision step) is
  66-80 % of the tick in the 10,000-body rows. Stage 9's profile breaks it down as:
  * 15-27 % the collision-checked step itself;
  * 8-14 % soft-body reads;
  * 10-20 % route reads and static look-ahead.

## 13. Remaining bottlenecks

1. **Crowds round static corners and beside idle blocks** (arrival in one-N, idle-10000, swap-N). A big formation
   rounding an obstacle edge single-files along the shortest line and jams at the corner. Nothing spreads a crowd across
   the free width beside an obstacle; the pinwheel acts only at wall ends. idle-10000 moves 38 of 1,000 past 9,000 idle
   bodies.
2. **Head-on crowds at narrow passages** (bridges-10000, swap-10000). Each army waits massed at its mouth while the
   other's stragglers stand in the passage. Admission counts occupants, but they cannot leave into the waiting crowd.
   Keeping an exit lane clear at a mouth, or lane reservation by direction, is the open design item. bridges-10000 and
   maze-10000 are also capacity-bound as laid out; a feasible-layout version is needed to separate navigation from
   capacity.
3. **Many unrelated destinations queue** (scatter-10000, many-10000: request ages 2,000-2,600 ticks). The fixed quota
   drains the queue fairly but slowly. Faster service is either a quota change (behaviour, judged on outcomes, never on
   the machine) or still less work per request (abstract search costs, more sharing between unrelated routes).
4. **Serial movement is the tick** (66-80 % at 10,000 bodies; many-10000 and scatter-10000 over budget in 2-13 % of
   ticks).
   * The only exact candidate left is certificates for the static look-ahead (10-20 % of the tick), after separating it
     from its soft-body reads.
   * Snapshot-parallel movement would be a behaviour change and has no measured failure case behind it.
5. **mixed-10000's exploration and separation phases** (about 62-70 ms a tick): outside Legion, inherited.
6. **The regressions in section 5**: three crowdbench cases, one-100's tail, churn's held queues, mixed-10000's
   arrivals. Also attack-10000's per-body movement cost (+30 % per moving unit-tick in the scale run, +23 % tick in the
   A/B), which is undiagnosed.

## 14. Scope not completed

* **Unmet frozen targets.** 79 targets are missed (110 at the start), plus 5 report-only. The biggest groups:
  * `arrive.n`: 20 of 23 missed;
  * `stuck_end`: 19 missed;
  * `livelock_end`: 20 missed;
  * `arrive.t99`: 9 missed;
  * `start.last`: 6 missed.

  The targets stay as frozen.
* **Open items from the stage records.**
  * Brief phase 6: no sector-to-route reverse index; windows bake soft costs.
  * Brief phase 5: abstract search costs are portal midpoints.
  * Brief phase 4: demand per cell; the pausing extension not re-measured with tiles.
  * Brief phase 7: lane reservation by direction.
  * Brief phase 8: window relaxation stays serial; look-ahead certificates.
  * sched-load's scheduler ctests still run with `--no-hier-routes`.
* **Not covered by any instrument.** Production, factory exits and logistics at 10,000 units. `.scn` cannot express
  them; `--mpai` and the e2e stress run cover them only at their own scale.
* **Not measured.**
  * Game speeds above 1x.
  * Windows/macOS scale runs beyond the hash checks above. The Mac is limited to minimal hash checks by user decision.
  * A Windows ctest pass of this branch.
* **The three crowdbench regressions** (bisected) and attack-10000's per-body movement cost: not diagnosed.

## Appendix: state hashes of the scale rows

| scenario | mode | before | after |
|---|---|---|---|
| one-100 | legion | a3844927e3ac1e85 | e51d2665f44265cb |
| one-100 | retail | 446bbbf5d3f51499 | 446bbbf5d3f51499 |
| one-1000 | legion | 58890fe206c7cf56 | 23d570f682d27ecc |
| one-1000 | retail | b758ce6621dc13ea | b758ce6621dc13ea |
| one-2500 | legion | 4055100ef4f42148 | 157f9d5e4f2f663e |
| one-5000 | legion | f9df3b5755872565 | c96b9f9c6105110a |
| one-10000 | legion | dc17b51b29f7468a | e01323edda043716 |
| one-10000 | retail | 74a2897c431dde11 | 74a2897c431dde11 |
| many-100 | legion | 594333dff739a82a | cc1451d23c86cc10 |
| many-100 | retail | b8ecea6bacdf78b3 | b8ecea6bacdf78b3 |
| many-1000 | legion | 62dc992d2c93931b | 1dd171d4bb2b2ad2 |
| many-1000 | retail | dbc5de9f07bcee83 | dbc5de9f07bcee83 |
| many-2500 | legion | 00434c65e5fc708d | 6fbf486b09cb6a6b |
| many-5000 | legion | b09fa5dd10111282 | 590124d3bcc9b6bd |
| many-10000 | legion | 0f68299de58fd1b7 | 69e065027efd3965 |
| many-10000 | retail | 7c5caca15b168fab | 7c5caca15b168fab |
| scatter-400 | legion | 32b332c8d5d0cd36 | dab078bf067f6c7d |
| scatter-2500 | legion | fa892df06da6fafb | fb3575c3918aa51f |
| scatter-10000 | legion | 3494b87f93aac47e | bb909530f7d8efd5 |
| swap-1000 | legion | 03df413485cc7c75 | 9c0737b5c1213797 |
| swap-1000 | retail | 496b35bcb7e711f2 | 496b35bcb7e711f2 |
| swap-2500 | legion | faad77c150d8b35f | 6eae0204cfdbe9ea |
| swap-5000 | legion | dc0949bf8c53663e | 5ffbdf562b30cb12 |
| swap-10000 | legion | 790a75cf89aa2716 | e47dddc629bb9332 |
| bridges-1000 | legion | 273bd1b6da4d986b | 749403372fd54622 |
| bridges-1000 | retail | bb0d80c7623295e6 | bb0d80c7623295e6 |
| bridges-10000 | legion | 5b2638a07a276a61 | 3f9f5c8d8e36a257 |
| maze-1000 | legion | 25bd49a2270b63d7 | 758e6bc54444b3fa |
| maze-1000 | retail | dc471b116c1eab1b | dc471b116c1eab1b |
| maze-10000 | legion | ce7e1178f0f4bf10 | 84ee9f06264e1ab6 |
| mixed-1000 | legion | 5b589f41969607be | cad53b6f251f94b6 |
| mixed-1000 | retail | f83642718d7e5d23 | f83642718d7e5d23 |
| mixed-10000 | legion | eb0b491837a0c59c | 6d0ea4b53c590624 |
| attack-1000 | legion | 54935e48137bf877 | a24feacada5eac35 |
| attack-1000 | retail | 923e21b53997b3c1 | 923e21b53997b3c1 |
| attack-10000 | legion | afd276557bf7678d | 99ee4a3a4babbdae |
| churn-1000 | legion | dfcd5ab32065dad6 | ba68ed6265d398ed |
| churn-1000 | retail | a1492f752390c117 | a1492f752390c117 |
| churn-10000 | legion | 5a426305529db1c3 | b439e838e21fa2df |
| idle-10000 | legion | 87180fb3554912db | 71a8275fa770e080 |
| room2door | legion | 408257efda8e3ad4 | f988a13ceb33b361 |
| sched-load | legion | 56e066e1e4617d19 | 4135a56bdc3b7018 |
| sched-mix | legion |  | cb0eded99854eed2 |
