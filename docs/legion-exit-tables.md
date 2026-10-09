# Legion workstream exit tables

Every Legion workstream exit (LEGION-PLAN section 3.0) records each key that
moved by more than 5% against its base, old -> new, marked *intended* or
*incidental (within band)*, so the next workstream's step 0 can reproduce it
instead of stopping on it. The base is the W0 committed base
(`tools/scenarios/baseline.json` at 90457fd), or, for a fixture a later step 0
added, that step 0's base. Scenario keys are medians over the start offsets
0, +1, -1, +2, -2 (optimized Debug, both modes, serial == workers); the
crowdbench rows are the committed screen (`crowdbench_screen_baseline.jsonl`,
seed 0, 6000 ticks, turn rate 2500).

## W3 step 0b: the base after the tick stamps

Head: `task-w3-stamps` dfeb51ca (W3 step 0a, the T1-T tick stamps, on W2's 7c490b2f)
plus the step 0b instruments. No simulation change in 0b; the rebase is the stamps' own
movement: 974 of about 9,000 baselined keys moved by more than 5% (all inside
their bands; Legion work x0.985, t90 x0.92, done x0.69, arrived x1.05, stopped x0.87), and
the values were retaken (`since: W3`, same key set as W0, medians over five offsets).

New fixtures (all Legion medians at this head; Retail is the floor, `legion <= retail x1.1`):

| Fixture | W3 target it records | Base (Legion, tick 8000 unless noted) |
|---|---|---|
| pocket-615-wall / -open | MV-01, acceptance A (open >= 600 of 615) | 155 / 479 orders complete (Retail 114 / 297) |
| pocket-304-wall / -open | MV-01 | 150 / 242 complete (Retail 269 / 304) |
| deadend-w{2,4,6}-n{40,120}-{closed,room}-{plain,squad} | AR-02, acceptance B (settled == n by 5500) | w4 n120 closed plain: 49 of 120 settle, 71 bodies stand still 900+ ticks with open orders |
| tail-open304 / open570 / corner380 / wave | AR-01, acceptance B (`open_still900` == 0) | 303 / 570 / 380 / 304 complete; `still900_ever` 0 / 2 / 0 / 0 |
| doorplug-112 / -124 | AR-02 and the 3.1 gate "a later order still passes" | B crosses 0 of 60 / 59 of 60 (`side.past.high` 25 / 120) |

New instrument keys: `open_still900` and `still900_ever` (observer; open orders on bodies
that stood still 900+ ticks), `work.midroute_completions.*` (always present; `eq` 0 on
mazeform and mazeapproach, the base value elsewhere), and the AR-08 completion keys on the
`jagged` probe. The AR-08 keys on sharedgoal 200/500/2000 are the crowdbench screen's
`complete_*` fields; squadformation, deadend, jagged, pocket, tail and doorplug carry them
in `legion_world_test` output or the scn keys `g.*.complete_*`.

Not fixture-expressible and left to the probes (`legion_world_test <probe>`): the cap-0
pocket case, settlelatency (the rest-stride test hook), squadformation's `last_ordered`.

Known state of the head, for W3's own steps: `legion_squadformation`,
`legion_acceptance_crowdheld_legion` and `legion_cost` fail on it (AR-01's tail and the
stamps' wake-up cost), and `legion_check --cumulative` reports the four battle
combat-cost keys above their W0 base (`w0` is kept on those entries).

## W2: stop the freezes (protocol 240)

Head: legion-w2 at the W2 exit (steps 0-3 and 5; protocol 240). Landed:

- **AR-03**: structures, units under construction and speed-0 units never pace
  or anchor an Alt+N formation; `FormAgg` sums Fixed raw values.
- **AR-11**: an approach order drops on arrival at its approach point
  (`kApproachRetire = 0`, timed from arrival), and by the holder rule (a unit
  held for 12 ticks behind bodies of its own selection that already stopped
  nearer its point drops its order there). Genuinely Trapped orders keep 9000.
- **RB-04**: a patrolling builder's repair detour is a Legion `Kind::Repair` leg.
- **Dropped**: 2.1b (formation pace over members with orders) failed the mixed2
  work gate.
- **Step 4 tick stamps: deferred to W3.** The attempt converted the C8 readers
  but missed T's acceptance (squadformation p90 4679 vs <= 3600; settlelatency
  stride spread 690 vs <= 45) and moved 354 keys (corner-1x448 reversals
  234 -> 397, strait-4x24 files 214 -> 0, mixed2 hover 47 -> 88; held_rechecks
  cost 45 -> 558, because rest started at 60 ticks instead of 60 updates). A
  resting unit acts up to stride-1 ticks late on everything its skipped update
  would do, so the stamps need a deadline-aware `heldRest` first (W3 step 0/1).
  The cadence A/B was not neutral within 3% and was reverted.

Gate: `legion_check.py check --require-all`, per step and `--cumulative`, on
all 56 scenarios: 9263 keys, 0 failed, 16 licensed by `accepted_regressions`
(the AR-11 idle-body moves on unreach-200 and mazeapproach, each with its
reason), 56 ratchets written (references raised, history line "W2 exit").
Retail-floor exceptions 156 -> 155 (unreach-200 `approach.complete.t90` now
516 vs Retail 2419); the spread exceptions stay. Retail state hashes equal on
every scenario; navigation goldens (Retail and Legion) unchanged; `--mpai`
Inner Circle 300 s seed 1 unchanged (Legion 136218d83cbf7af8, Retail
b240750e5765c02b: the AI forms no Alt+N squads and its builders' patrol
repairs do not occur in that game). The head reproduces the committed
crowdbench screen exactly (86 cases, 0 differences).

**Against the W0 base: no key of the 52 W0 scenarios moved by more than 5%**
(including battle-field 2x60/2x250/2x500 and battle-assault total Legion work,
max / p99 / total, which are equal to W0). Every move is on a W2 fixture.

### Headline keys

| Fixture | Key | Base | W2 exit | Kind |
|---|---|---|---|---|
| structsquad (Alt+1: 20 movers + a 4x4 structure) | arrived / t90 | 0/20, never | 20/20, 1291 (Retail 20 by 1262) | intended |
| structsquad-keep (Keep-like, speed 0) | arrived / t90 | 0/20, never | 20/20, 1291 | intended |
| structsquad, -keep | total Legion work | 1650717 | 299447 (-82%) | intended |
| `legion_structsquad` probe | modes 1/3/5 (structure, Keep, factory) | 0/20 | 20/20 by 1299 (= no structure) | intended |
| `legion_factorysquad` probe (factory in formation) | produced 1 / 4 / 12 | 0/1, 0/4, 0/12 | 660, 822, 861 (no squad: 660, 876, 811) | intended |
| unreach-200 (200 movers, one click, unreachable) | orders complete (done) | 9005 | 573 (Retail 3039) | intended |
| unreach-200 | reach their approach point | 57 | 200 | intended |
| unreach-200 | wait, arrival -> completion (t50) | 8345 | 1 | intended |
| unreach-200 | total Legion work | 1940759 | 838393 (-57%) | intended |
| crowdbench unreachable 200x1, Legion | orders complete | 0 by 6000 (all at 9002 in 10000) | 200, last 1610 (Retail 2318) | intended |
| crowdbench unreachable 200x1, Legion | approach arrived / wait max | 94 of 200 at 10000 | 200 / 1 tick | intended |
| crowdbench unreachable 500x4, Legion | orders complete | 0 by 6000 | 2000, last 1782 | intended |
| crowdbench recovery 200x1 / 500x4, Legion | all arrived | 4597 / never | 3926 / 4299 | intended |
| crowdbench recovery-passive 200x1 / 500x4, Legion | at the goal at 6000 | 200 / 1999 | 0 / 0 (Retail 0; orders drop at the wall, open question 13) | intended |
| mazeapproach (~8200-tick walk) | orders complete (done) / wait | 9001 / 1333 | 8391 / 1 (Retail 8361) | intended |
| mazeapproach | dropped before arrival | 0 | 0 (no walk cut short) | guard |
| `legion_patrolrepair` | Retail ticks / Legion ticks | 23 / 0 | 0 / 23 | intended |
| `legion_patrolrepair` -moving | Retail ticks / Legion ticks / Retail searches | 277 / 0 / 14 | 0 / 260 / 0 | intended |

`legion_factorysquad` with `W2_REQUIRE=1` still fails on its 12-mover row: the
formation arrives at 861 against 811 without a squad. That is the existing
slowest-member pacing over the individual speed spread (pacing off: 811), not
the structure. It is not a ctest gate.

### Every scenario key moved by more than 5% (against the W2 step-0 base)

All 137 moves are intended (the W2 fixtures). structsquad-keep moved exactly
as structsquad on every key.

| key | old | new | change |
|---|---|---|---|
| mazeapproach/legion/approach.arrive.t50 | 8203 | 7685 | -6.3% |
| mazeapproach/legion/approach.complete.done | 9001 | 8391 | -6.8% |
| mazeapproach/legion/approach.complete.t50 | 9001 | 7686 | -14.6% |
| mazeapproach/legion/approach.complete.t90 | 9001 | 8391 | -6.8% |
| mazeapproach/legion/approach.wait.done | 1333 | 1 | -99.9% |
| mazeapproach/legion/approach.wait.t50 | 642 | 1 | -99.8% |
| mazeapproach/legion/contact_own_permille | 171 | 63 | -63.2% |
| mazeapproach/legion/contact_settled_permille | 0 | 2 | new |
| mazeapproach/legion/g.A.complete_dist_max | 12 | 14 | +16.7% |
| mazeapproach/legion/stopped_permille | 103 | 1 | -99.0% |
| mazeapproach/legion/work.group_loop_iters.total | 310 | 290 | -6.5% |
| mazeapproach/legion/work.holds.total | 6 | 2 | -66.7% |
| mazeapproach/legion/work.move_calls_by_state_2.p99 | 1 | 0 | -100.0% |
| mazeapproach/legion/work.move_calls_by_state_2.total | 581 | 7 | -98.8% |
| mazeapproach/legion/work.move_calls_by_state_5.max | 4 | 1 | -75.0% |
| mazeapproach/legion/work.move_calls_by_state_5.p99 | 4 | 0 | -100.0% |
| mazeapproach/legion/work.move_calls_by_state_5.total | 3104 | 4 | -99.9% |
| mazeapproach/legion/work.moves.total | 35773 | 31995 | -10.6% |
| mazeapproach/legion/work.still_units_processed.total | 132 | 267 | +102.3% |
| mazeapproach/legion/work.trace_cells.max | 99 | 148 | +49.5% |
| mazeapproach/legion/work.trace_cells.p99 | 71 | 75 | +5.6% |
| structsquad/legion/contact_own_permille | 594 | 322 | -45.8% |
| structsquad/legion/contact_settled_permille | 0 | 9 | new |
| structsquad/legion/g.A.arrived | 0 | 20 | new |
| structsquad/legion/g.A.complete_n | 0 | 20 | new |
| structsquad/legion/g.A.done | never | 1300 | +130100.0% |
| structsquad/legion/g.A.left_behind | 20 | 0 | -100.0% |
| structsquad/legion/g.A.t50 | never | 1272 | +127300.0% |
| structsquad/legion/g.A.t90 | never | 1291 | +129200.0% |
| structsquad/legion/region.goal.inside | 0 | 20 | new |
| structsquad/legion/stopped_permille | 845 | 11 | -98.7% |
| structsquad/legion/work.detour_cells.max | 544 | 1164 | +114.0% |
| structsquad/legion/work.detour_cells.total | 7857 | 8360 | +6.4% |
| structsquad/legion/work.group_loop_iters.total | 90 | 83 | -7.8% |
| structsquad/legion/work.holds.max | 12 | 2 | -83.3% |
| structsquad/legion/work.holds.p99 | 1 | 0 | -100.0% |
| structsquad/legion/work.holds.total | 55 | 15 | -72.7% |
| structsquad/legion/work.legion_total.p99 | 9556 | 2250 | -76.5% |
| structsquad/legion/work.legion_total.total | 1650717 | 299447 | -81.9% |
| structsquad/legion/work.line_sweeps.p99 | 5 | 20 | +300.0% |
| structsquad/legion/work.line_sweeps.total | 799 | 2643 | +230.8% |
| structsquad/legion/work.move_calls_by_state_1.max | 8 | 20 | +150.0% |
| structsquad/legion/work.move_calls_by_state_1.p99 | 8 | 20 | +150.0% |
| structsquad/legion/work.move_calls_by_state_1.total | 4681 | 25159 | +437.5% |
| structsquad/legion/work.move_calls_by_state_2.max | 19 | 7 | -63.2% |
| structsquad/legion/work.move_calls_by_state_2.p99 | 19 | 4 | -78.9% |
| structsquad/legion/work.move_calls_by_state_2.total | 25279 | 170 | -99.3% |
| structsquad/legion/work.moves.total | 18942 | 25348 | +33.8% |
| structsquad/legion/work.pass_scans_skipped.total | 14520 | 22865 | +57.5% |
| structsquad/legion/work.slot_search_cells.max | 24972 | 719 | -97.1% |
| structsquad/legion/work.slot_search_cells.p99 | 8748 | 0 | -100.0% |
| structsquad/legion/work.slot_search_cells.total | 1382003 | 719 | -99.9% |
| structsquad/legion/work.trace_cells.p99 | 658 | 2250 | +241.9% |
| structsquad/legion/work.trace_cells.total | 105398 | 179964 | +70.7% |
| structsquad-keep/legion/contact_own_permille | 594 | 322 | -45.8% |
| structsquad-keep/legion/contact_settled_permille | 0 | 9 | new |
| structsquad-keep/legion/g.A.arrived | 0 | 20 | new |
| structsquad-keep/legion/g.A.complete_n | 0 | 20 | new |
| structsquad-keep/legion/g.A.done | never | 1300 | +130100.0% |
| structsquad-keep/legion/g.A.left_behind | 20 | 0 | -100.0% |
| structsquad-keep/legion/g.A.t50 | never | 1272 | +127300.0% |
| structsquad-keep/legion/g.A.t90 | never | 1291 | +129200.0% |
| structsquad-keep/legion/region.goal.inside | 0 | 20 | new |
| structsquad-keep/legion/stopped_permille | 845 | 11 | -98.7% |
| structsquad-keep/legion/work.detour_cells.max | 544 | 1164 | +114.0% |
| structsquad-keep/legion/work.detour_cells.total | 7857 | 8360 | +6.4% |
| structsquad-keep/legion/work.group_loop_iters.total | 90 | 83 | -7.8% |
| structsquad-keep/legion/work.holds.max | 12 | 2 | -83.3% |
| structsquad-keep/legion/work.holds.p99 | 1 | 0 | -100.0% |
| structsquad-keep/legion/work.holds.total | 55 | 15 | -72.7% |
| structsquad-keep/legion/work.legion_total.p99 | 9556 | 2250 | -76.5% |
| structsquad-keep/legion/work.legion_total.total | 1650717 | 299447 | -81.9% |
| structsquad-keep/legion/work.line_sweeps.p99 | 5 | 20 | +300.0% |
| structsquad-keep/legion/work.line_sweeps.total | 799 | 2643 | +230.8% |
| structsquad-keep/legion/work.move_calls_by_state_1.max | 8 | 20 | +150.0% |
| structsquad-keep/legion/work.move_calls_by_state_1.p99 | 8 | 20 | +150.0% |
| structsquad-keep/legion/work.move_calls_by_state_1.total | 4681 | 25159 | +437.5% |
| structsquad-keep/legion/work.move_calls_by_state_2.max | 19 | 7 | -63.2% |
| structsquad-keep/legion/work.move_calls_by_state_2.p99 | 19 | 4 | -78.9% |
| structsquad-keep/legion/work.move_calls_by_state_2.total | 25279 | 170 | -99.3% |
| structsquad-keep/legion/work.moves.total | 18942 | 25348 | +33.8% |
| structsquad-keep/legion/work.pass_scans_skipped.total | 14520 | 22865 | +57.5% |
| structsquad-keep/legion/work.slot_search_cells.max | 24972 | 719 | -97.1% |
| structsquad-keep/legion/work.slot_search_cells.p99 | 8748 | 0 | -100.0% |
| structsquad-keep/legion/work.slot_search_cells.total | 1382003 | 719 | -99.9% |
| structsquad-keep/legion/work.trace_cells.p99 | 658 | 2250 | +241.9% |
| structsquad-keep/legion/work.trace_cells.total | 105398 | 179964 | +70.7% |
| unreach-200/legion/approach.arrive.done | never | 572 | +57300.0% |
| unreach-200/legion/approach.arrive.t50 | never | 478 | +47900.0% |
| unreach-200/legion/approach.arrive.t90 | never | 515 | +51600.0% |
| unreach-200/legion/approach.arrived_n | 57 | 200 | +250.9% |
| unreach-200/legion/approach.complete.done | 9005 | 573 | -93.6% |
| unreach-200/legion/approach.complete.t50 | 9002 | 479 | -94.7% |
| unreach-200/legion/approach.complete.t90 | 9004 | 516 | -94.3% |
| unreach-200/legion/approach.dropped_unarrived_n | 143 | 0 | -100.0% |
| unreach-200/legion/approach.wait.done | never | 1 | +200.0% |
| unreach-200/legion/approach.wait.t50 | 8345 | 1 | -100.0% |
| unreach-200/legion/contact_own_permille | 992 | 847 | -14.6% |
| unreach-200/legion/contact_settled_permille | 0 | 24 | new |
| unreach-200/legion/g.A.complete_dist_median | 101 | 107 | +5.9% |
| unreach-200/legion/reversals | 4 | 0 | -100.0% |
| unreach-200/legion/stopped_permille | 942 | 114 | -87.9% |
| unreach-200/legion/wall_touch_near_permille | 21 | 16 | -23.8% |
| unreach-200/legion/wall_touch_permille | 1 | 0 | -100.0% |
| unreach-200/legion/work.aware_pairs.total | 257 | 223 | -13.2% |
| unreach-200/legion/work.detour_cells.max | 632 | 697 | +10.3% |
| unreach-200/legion/work.detour_cells.p99 | 1 | 0 | -100.0% |
| unreach-200/legion/work.detour_cells.total | 21827 | 11977 | -45.1% |
| unreach-200/legion/work.detours.max | 1 | 2 | +100.0% |
| unreach-200/legion/work.detours.total | 28 | 34 | +21.4% |
| unreach-200/legion/work.group_loop_iters.p99 | 4 | 0 | -100.0% |
| unreach-200/legion/work.group_loop_iters.total | 2032 | 904 | -55.5% |
| unreach-200/legion/work.held_rechecks.max | 160 | 12 | -92.5% |
| unreach-200/legion/work.held_rechecks.p99 | 157 | 4 | -97.5% |
| unreach-200/legion/work.held_rechecks.total | 1209346 | 888 | -99.9% |
| unreach-200/legion/work.holds.total | 1539 | 921 | -40.2% |
| unreach-200/legion/work.legion_total.total | 1940759 | 838393 | -56.8% |
| unreach-200/legion/work.line_sweeps.max | 64 | 94 | +46.9% |
| unreach-200/legion/work.line_sweeps.p99 | 28 | 31 | +10.7% |
| unreach-200/legion/work.move_calls_by_state_1.total | 104429 | 87100 | -16.6% |
| unreach-200/legion/work.move_calls_by_state_2.max | 162 | 81 | -50.0% |
| unreach-200/legion/work.move_calls_by_state_2.p99 | 160 | 47 | -70.6% |
| unreach-200/legion/work.move_calls_by_state_2.total | 1243868 | 10821 | -99.1% |
| unreach-200/legion/work.move_calls_by_state_5.max | 57 | 7 | -87.7% |
| unreach-200/legion/work.move_calls_by_state_5.p99 | 57 | 0 | -100.0% |
| unreach-200/legion/work.move_calls_by_state_5.total | 456332 | 200 | -100.0% |
| unreach-200/legion/work.moves.total | 1196634 | 95406 | -92.0% |
| unreach-200/legion/work.pass_scan_cells.max | 48 | 144 | +200.0% |
| unreach-200/legion/work.pass_scan_cells.total | 2856 | 4800 | +68.1% |
| unreach-200/legion/work.pass_scans.max | 2 | 6 | +200.0% |
| unreach-200/legion/work.pass_scans.total | 119 | 200 | +68.1% |
| unreach-200/legion/work.pass_scans_skipped.total | 698238 | 77792 | -88.9% |
| unreach-200/legion/work.slides.p99 | 8 | 7 | -12.5% |
| unreach-200/legion/work.slides.total | 2483 | 1933 | -22.2% |
| unreach-200/legion/work.still_units_processed.p99 | 0 | 200 | new |
| unreach-200/legion/work.still_units_processed.total | 4000 | 60893 | +1422.3% |
| unreach-200/legion/work.trapped.max | 51 | 7 | -86.3% |
