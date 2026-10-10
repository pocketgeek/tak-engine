# Legion workstream exit tables

Every Legion workstream exit (LEGION-PLAN section 3.0) records each key that
moved by more than 5% against its base, old -> new, marked *intended* or
*incidental (within band)*, so the next workstream's step 0 can reproduce it
instead of stopping on it. The base is the W0 committed base
(`tools/scenarios/baseline.json` at 90457fd), or, for a fixture a later step 0
added, that step 0's base. Scenario keys are medians over the start offsets
0, +1, -1, +2, -2 (optimized Debug, both modes, serial == workers; from W3 round 4 the
small-count keys -- crossings, wall touch, the t90 of a group under 10 bodies -- on the
median of the gate offsets 0, +-1 .. +-5); the
crowdbench rows are the committed screen (`crowdbench_screen_baseline.jsonl`,
seed 0, 6000 ticks, turn rate 2500).

## W7 exit (2026-10-10): crossing groups take the longer route -- awareness detection and pass-behind (protocol 246)

Head: `task-w7-land` = W7 steps 1-3 and 5-7 (`task-w7-b` `fb6f8612` on `task-w7-a` `aa73c084` on step 0 `7dbc218e`) with `origin/main`
`9564266f` (the WE batch and the timing tools, protocol 245, hash-identical) merged in, plus the exit commits: protocol 246 note, docs,
baselines. Base = `task-w7-s0` (`7dbc218e`), the W7 step-0 sim (instruments and fixtures only; sim = `2d671644`). User decision 5
holds: the later crossing group always routes behind the other stream, the 10 s wait at the stream's edge does not exist, and with no
detour at all it crosses as before; no `giveWay` / `waitSince` state exists and the `giveway_*` counters read 0 everywhere.
Steps **4 (`blockedByStill`), 8 (half-bands) and 9 (the MV-03 experiment) are not in the chain**: each failed its exit or stop rule
(sections "W7 steps 1-5" and "W7 steps 6-9" below). Step 2 (parting) is in with its restricted set (stuck members only), so its
MV-11 targets are not met. Step 10 was out of scope. **Not every W7 gate is met** -- see "Hard gates" and "What the lead has to decide".

### Gates on the exit head

| Gate | Result |
|---|---|
| Scenario set (`legion_check check --cumulative --require-all`, all 122 files, both modes, gate offsets, serial == workers on all 224 runs; Windows, optimized Debug; the 10 data-gated `gen1-route-*` / `motion-*` files re-run on Linux with `--data`) | **PASS** after licensing: 26827 keys, 0 failed, 1767 licensed, 439 ratchets left unapplied outside the W7 fixtures (W4 ruling (q)), 171 floor exceptions in the run. Before licensing 129 failed keys (below) |
| What moved | 11 of the 112 scenarios swept change state (Legion only): `aware-cross`, `aware-cross-behind`, `aware-headon`, `aware-seen`, `aware-attack`, `crosslong`, `densehead`, `ringcross`, `unreach-200`, `mazeapproach`, `battle-assault`. Every other scenario (`aware-unseen`, the awareness-off control, included) and every Retail line is hash-identical to the base. 1017 keys moved by more than 5%: [legion-exit-w7-moves.md](legion-exit-w7-moves.md) |
| Baseline update | `ringcross` hashes retaken; 8 ringcross counters first non-zero baselined; `crosslong` and `aware-cross-behind` baselined for the first time (1577 entries, both modes; no Retail-floor failure; 245 offset-spread exceptions, the tool's median-of-5 gating of the two fixtures); 104 losses licensed (`accepted_regressions`, pinned at the exit values: aware-cross 34, densehead 18, aware-attack 11, aware-headon 11, aware-seen 10, ringcross 10, unreach-200 10; reasons by cause below); 2 declared bounds raised (densehead and aware-seen `contact_other_permille`); 5 offset-spread exceptions on the aware fixtures; ratchet on the W7 fixtures only, beyond the offset spread (40 references raised, 2 Retail-floor exceptions cleared) |
| Retail-floor exceptions | **not grown**: 173 on main's baseline -> 171. Cleared: `aware-cross` and `aware-seen` `click.a00.t90` (ruling (i); `aware-headon` `click.a00.t90` was cleared at W5/W6). No new fixture fails the floor. **Still open: `aware-unseen` `click.a00.t90` (legion 2162 vs Retail 1877) and `click.b00.t90` (2207 vs 1929)**, below |
| Cumulative combat gate (Windows run keys) | at or below the W0 base on all twelve keys: battle-field-2x60 max 393549 / p99 107911 / total 19.7M, 2x250 396040 / 173200 / 86.5M, 2x500 405958 / 226189 / 213.9M, battle-assault 444460 / 399516 / 211.1M (W0 values in the W5 exit table: 398804 / 116526 / 23.6M; 438589 / 255750 / 119.9M; 528461 / 393727 / 271.6M; 484465 / 416681 / 394.7M -- 2x250 max 396040 vs 438589). Only battle-assault changes against the step-0 sim: max 440956 -> 444460, p99 398723 -> 399516, total 229.4M -> 211.1M (-8%). `legion_cost battle --cumulative` ok on all four battle files |
| `legion_cost` | scaling ok (ctest); no baseline retake needed |
| Aware work (declared cap) | `aware.work_max` crosslong 388, aware-cross-behind 212 against legion_total per-tick max 389105 / 209272: inside the cap (per-tick max of aware work <= the per-tick max of total Legion work on aware-*) |
| crowdbench screen (Release, seed 0, 86 rows, both modes) | the 80 rows without unreachable orders are hash-identical to main's baseline; **6 Legion rows change hash**: `unreachable/{200x1,500x4}`, `recovery/{200x1,500x4}`, `recovery-passive/{200x1,500x4}` (step 3, approach clearance). The 11 new never-hashed counters (`legion_parts`, `_aware_*`, `_giveway_*`) read 0 on every row (no formation mover forms); baseline retaken, re-run: 0 differences |
| Exact | Retail navigation golden and Legion navigation golden both **unchanged** (24 of 24 each, serial == workers; no checkpoint meets a crossing); `check-determinism.sh` OK (dcef618cd2e4d558); `check-detmath` OK; serial == workers on all scenario runs; Retail byte-identical on all Retail scenario lines |
| `--mpai` Inner Circle 300 s seed 1 (takserver --local, clean XDG_DATA_HOME, `TAK_LEGION_VERIFY` on, build-o2) | Legion **b2261dc31dc84cdf** twice on Linux and once each on Windows and the Mac; Retail **b240750e5765c02b** on all three. **Unchanged from protocols 242 - 245**: the 16-unit AI game forms no mover of 8 and no stuck approach member, so no W7 path runs |
| ctest | optimized Debug non-nightly 356 tests: 352 pass; fail: `legion_acceptance_crowdheld_legion` (W9 gate), `cobanim` (no extracted assets in the worktree), `crusades_hardening_network` and `crusades_history_ui_network` (campaign-server quota timing under load; `crusades_hardening_network` fails identically on `origin/main` `9564266f`, `crusades_history_ui_network` passes alone). Release `-L quick` 300: 298 pass (the same first two). Windows (touched areas, 61): 60, the same crowdheld. Mac: `fp_contract`, `fixed`, `detmath`, `navigation_determinism` pass |

### Theme targets (Legion, run keys, Windows; Retail on the same binary)

| fixture | key | step-0 base | exit | Retail |
|---|---|---|---|---|
| crosslong | `behind.wait.wait_ticks` / `wait_run_max` / `waits` | 1049 / 759 / 3 | **0 / 0 / 0** | - |
| crosslong | `behind.wait.extra_cells` (giver's extra path) | 7 | 28 (detour 165 permille) | - |
| aware-cross-behind | `behind.wait.wait_ticks` / `wait_run_max` / `waits` | 87 / 47 / 1 | **0 / 0 / 0** | - |
| aware-cross-behind | `behind.wait.extra_cells` | 1 | 16 (detour 151 permille) | - |
| aware-cross | `pair.contacts.permille_x100` | 1090 | **0** | 769 |
| aware-cross | `click.a00.t90` (the group that goes behind) | 1947 | **1517** | 1753 |
| aware-cross | `click.b00.t90` (the group that crosses first) | 1332 | 1542 | 1418 |
| aware-headon | `click.a00.t90` / `click.b00.t90` | 2027 / 2037 | 1909 / 1947 | 1870 / 2013 |
| aware-headon | `pair.contacts.permille_x100` | 326 | 254 | 783 |
| aware-seen | `click.a00.t90` / `click.b00.t90` | 2046 / 1937 | 2027 / 1871 | 1940 / 1951 |
| aware-seen | `pair.contacts.permille_x100` | 372 | 248 | 819 |
| aware-unseen (control, awareness off) | `click.a00.t90` / `click.b00.t90` | 2162 / 2207 | 2162 / 2207 (hash-identical) | 1877 / 1929 |
| rb02 ring N=104 / 208 done@3000 | arrivals | 17 / 16 | 38 / 33 (all three sizes finish 40/40) | - |
| rb02 plug | arrivals by 6000, p50 | 0/40 | 40/40, 2627 | - |
| unreach-200 | statue ticks / wall touch near | 2293 / 25 | 1746 / 0 | - |
| awarebig cross | waits / ON vs OFF done | 283 waits | 0 waits; ON 7571 <= OFF 8426 | - |

(The crossing-time columns of the aware fixtures are core-five medians for the floor and the run keys here are the gate-offset medians; the
two differ by a few ticks.)

### Hard gates

| Gate | State |
|---|---|
| Ruling (i): aware-headon / aware-seen / aware-cross `click.a00.t90` within Retail x1.1 | **met**; the three floor exceptions are removed (aware-headon at W5/W6, the other two by this ratchet) |
| Ruling (i): `aware-unseen` `click.a00.t90` / `click.b00.t90` | **NOT met** (2162 vs 1877 x1.1 = 2065 on the core five; b00 2207 vs 1929 x1.1 = 2122). The enemy is never seen (sight 16 px), so awareness is off and no W7 step reaches it; the only lever found, half-bands without the too-late rule, gives 2072 / 2072 and wrecks `awaredense` (312/400 arrived). The two exceptions stay, cluster MV-09, with W9 (decision 7, the head-on jam) or W8 lanes as the owner |
| W7 step 2 MV-11 targets (250x8@50 crossed >= 950; held <= 120; 250x1@50 >= 125) | NOT met (restricted set: 892 -> 889 and no better); step 2 kept for rb02 only |
| MV-03 (2000x1 crossed >= 1500 at 12000) | NOT met (step 9 reverted); W9 |
| Step 4 `blockedByStill` | dropped (doors 2000x1 arrived 243 -> 183 in the plan form; the other variants failed 347 sweep keys or other gates) |

### Licences by cause

(1) **Decision 5 (aware-cross, 34 licences + 2 spread exceptions)**: the group that goes behind gains (a00 t90 1947 -> 1517, contacts 1090 -> 0,
waits 87 -> 0); the group that crosses first is delayed (b00 t90 1332 -> 1542, t50 1134 -> 1367, done 1459 -> 1812; the Retail floor holds:
1542 vs 1418 x1.1 = 1560), crosses the lanes in fewer files (`gate.cross.files_x100` 664 -> 385) and the re-placed corridor and its refreshes add
field work (788k -> 995k), line sweeps (9015 -> 40037), pass scans and trace cells (total Legion work 1.32x, per-tick p99 1402 -> 2096).
(2) **Step 6 detection (aware-headon / seen / attack, 32 licences, 1 bound)**: time-sampled conflicts plan round sooner; line sweeps x1.6-1.7,
trace cells +30%, p99 of total Legion work +20-30%; aware-seen a00 `done` 2161 -> 2608 (b00 `done` 2702 -> 2205), aware-attack a00 `done` 2152 -> 2387.
(3) **Step 6 on a dense head-on stream (densehead, 18 licences, 1 bound)**: contact_other 140 -> 199 (bound 164) while the lane crossings (MV-02)
fall 603 -> 350 (W7-b's measure; the gate-offset median reads 447 against Retail's 208, still a floor exception); legion_total p99 15612 -> 19886. A shorter horizon fixes densehead but puts aware-headon back over
the floor.
(4) **Aware re-plans on ringcross (hashes retaken, 10 licences, 8 counters first non-zero)**: field work 529k -> 971k; outcomes unmoved.
(5) **Step 3 (unreach-200, 10 licences)**: detours 34 -> 46, pass scans 200 -> 239; total Legion work 0.91x.

### What the lead has to decide

1. **aware-unseen click t90 (ruling (i) not met).** Accept as a W9 / W8 gate or rule otherwise; the W7 steps cannot reach it.
2. **The giving group's cost under decision 5** (aware-cross b00 t90 +16%, `done` +24%, files 664 -> 385, reversals 1 -> 4): licensed above;
   decision 5 asks for exactly this trade.
3. **densehead contact_other 199 vs its bound 164** and the aware-seen / aware-attack `done` times: licensed; the alternative (a shorter
   head-on horizon) breaks ruling (i) on aware-headon, so it was not taken.
4. `crossthree`: the third stream routes behind both others (+39 cells, done 2926 -> 3751) and still records 2 short waits behind A (87 ticks) --
   behind-wait keys are 0 on crosslong and aware-cross-behind, the plan's two fixtures.

## W7 steps 6-9 (2026-10-10, task w7-b): measured state under user decision 5 (not an exit)

Head chain on `aa73c084` (W7 steps 1-5): step 6 detection (`f66dd096`), step 7 pass-behind (`17cccf5c`). **Step 8 (half-bands)
and step 9 (the MV-03 experiment) are not in the chain**: each failed its exit / stop rule (below). User decision 5 replaces
G1: no give-way hold exists, so no `giveWay` / `waitSince` state, no C8 reader changes, `giveway_*` 0 on every fixture.
Numbers: optimized Debug; scenarios = legion, 11 gate offsets (median; the floor reads the core five); sweep = every
`tools/scenarios/*.scn`, both modes, `legion_check check` on Windows (sims equal Linux). Base = the w7-a head.

| Step | Target (PLAN 3.3 / W7, decision 5) | Result |
|---|---|---|
| 6 | integer class test (C28), time-sampled detection | kept. Class from each group's line to its destination (measured heading read a group routing behind a stream as head on). aware-headon a00/b00 t90 2027/2037 -> 1909/1947, aware-seen 2046/1937 -> 2027/1871; contacts headon 326 -> 254, seen 372 -> 248 |
| 7 | route behind, 0 edge waits; no wait for any detour length | kept. Every `behind.*.wait_ticks / wait_run_max / waits` 0 on crosslong (1049/759/3 at base) and aware-cross-behind (87/47/1); awarebig-cross waits 0 (283), ON 7571 <= OFF 8426; crosslong done 4546 -> 3748; giver's extra path crosslong 7 -> 28 cells, aware-cross 1 -> 16 |
| 7 | aware cross contacts <= 5.0 permille, stopped <= 7% (world test) | 10.90 -> 0.00, 9.8% -> 2.3% |
| 8 | H half-bands: aware headon contacts <= 3.15, stopped <= 5.1%; awarebig head-on reversals <= OFF + 2 | dropped by its exit rule. Midpoint frame, width r/2+32: headon 3.06 / 2.3%, aware-seen click t90 1638/1717, awaredense done 8838 -> 8233, but awarebig-headon reversals 7 (OFF 3) on every width / length / charge variant (6-21); the band behind each group turns its back-left bodies round as it is placed (t 2656-3321). Forward-only frame: reversals 4 but aware headon done 1998 -> 2431, seen 2215 -> 3511 |
| 9 | J (knob): 2000x1 crossed @12000 >= 1500 (3-seed mean), >= 1400 @24000; 500/600/800 per side >= 85% | stop rule fired, reverted. J1+usable fields: mean 1382 (base 1229), @24000 1596 (1194), density 79/50/54% (76/53/57%), cross_gap_max @24000 11283, spin up (dens 761 -> 1379); J1+J2: 1339, 80/52/54%; J2 alone: no change. MV-03 needs the group-level lengthwise re-shape (W9, decision 7) |

**Ruling (i) floor keys** (core-five medians, Legion vs Retail x1.1): aware-headon a00 / b00 pass (no exception left); aware-seen
a00 passes (its exception can be removed), b00 passes; aware-cross a00 1947 -> 1517 vs 1759 passes (exception removable).
**aware-unseen a00 2162 vs 1877 and b00 2207 vs 1929 still fail**: the enemy is never seen (sight 16 px), so no awareness step
reaches it; half-bands applied without the too-late rule (the only lever found: 2072 / 2072) wreck awaredense (312/400 arrived,
105 re-plans). This W7 gate is open; it belongs with the head-on jam (W9, decision 7) or W8 lanes.

Sweep of the chain head (Windows, 17cccf5c code): 129 failed keys (w7-a head: 11). Besides the 11 carried:
- decision-5 cost on the giver (licence candidates, floor holds): aware-cross b00 t90 1332 -> 1542 [band 1465], t50, done,
  gate.cross.files 664 -> 385, reversals 1 -> 5;
- step 6 head-on detection: densehead contact_other 140 -> 199 (bound 164) while its MV-02 lane crossings fall 603 -> 350;
  work.legion_total.p99 15612 -> 19886 (steering counters: line sweeps, pass scans); aware-attack click a00 done 2117 -> 2387;
  aware-seen a00 done 2250 -> 2608 (b00 2702 -> 2205); aware-* work classes up (legion_total p99 +20%). A shorter head-on
  horizon (120-330 ticks) or a meeting-point corridor fixes densehead (contacts 114, crossings 205) but puts aware-headon
  back over the floor (2072-2252);
- ringcross: outcomes unmoved, field work 529k -> 971k (aware re-plans), its pinned hashes change (behaviour step);
- battle-assault mixed, total Legion work -9%. Aware work (`aware.work_max`): crosslong 116 -> 388, aware-cross-behind
  115 -> 212, under each scenario's legion_total per-tick max (389105 / 209272): within the declared cap.
Retail byte-identical (122 rows); serial == workers everywhere; crowdbench rows hash-identical (no formation movers);
ctest `-L quick` and every non-nightly `legion_*` / navigation test: only `legion_acceptance_crowdheld_legion` (W5/W9 gate)
fails, plus `cobanim` (no extracted assets in the worktree).

## W7 steps 1-5 (2026-10-10, task w7-a): measured state (not an exit)

Head chain on `7dbc218e` (W7 step 0): step 1 Trapped soft, step 2 parting, step 3 approach clearance, step 5 awareness
correctness. **Step 4 (`blockedByStill`) is not in the chain**: its exit rule ("if it fails, revert E") fired on every variant.
All numbers: optimized Debug; scenario sweep = every `tools/scenarios/*.scn`, both modes, gate offsets, `legion_check check`
on Windows (sims equal Linux); crowdbench = the 43-row Legion screen, seed 0.

| Step | Target (PLAN 3.3 / W7) | Result |
|---|---|---|
| 1 | rb02 ring N=104 done@3000 >= 30/40; N=208 >= 36/40 by 6000 | 17 -> 36; 17 -> 40/40 (p90 3166). On this head the clot is approach members Holding one cell short of an unreachable approach point (AR-11), not Trapped: those are stamped (reach kinds and sealed-pocket Trapped members are not; measured) |
| 1 | Holding-queue corridor unchanged; MV-05 gates (C9) | identical; crowdbench hash-identical, softReplans / crowdtrap / rapidreplacement spin unchanged |
| 2 | 250x8@50 crossed >= 950, held at end <= 120; 250x1@50 crossed >= 125 | not met: the wide set (any member held >= 60) gave 892 -> 889, held 344 -> 368 and new spin (groupdetour 200x1 0 -> 38, jagged 200x1 0 -> 2, sharedgoal 2000x1 0 -> 12); restricted by the exit rule to stuck members (crowdbench hash-identical) |
| 2 | deadend, tail, attackring/ar06bench, ringcross unchanged (C15) | unchanged (sweep) |
| 3 | plug >= 38/40 with p50 <= 2940 | 0 -> 40/40, p50 2627; unreach-200 statue 2293 -> 1746, wall touch near 25 -> 0, total work 0.94x; small classes up (detours 34 -> 46): licences needed |
| 4 | doors 2000x1 within 3%; formation tests within 5%; no zero-crossing window > 3000 | dropped. Plan form: doors 2000x1 arrived 243 -> 183, crossed 497 -> 405; opposingcolumns 2000x1 @24000 arrived 603 -> 392 (crossed 1194 -> 1210, gap 13933 -> 497). Other commands only, formation gate deleted: mixedfootprints 500x4 arrived 1940 -> 1895, squadformation far 325 -> 392; same player, formation gate own-only: 347 failed sweep keys (ringcross g.M.done never, corner-1x448, battles, gen1 routes); narrowed (non-reach holders, formation gate kept): aware-cross click a00 t90 2072 -> 2207, no MV-03 effect |
| 5 | builds per encounter 1; latency reported (C7); index identity (C34) | builds already 1 per encounter; latency awarebig cross 48, headon 7; TAK_LEGION_VERIFY index-vs-scan passes. Kept: equal charge lists, aware refreshes first inside kRefreshQuota (outcome-neutral on every aware fixture). Not kept: the cap raise (headon legion_total.max 1.95x), the plain swap (aware-cross contacts 548 -> 731), live corridor + line re-probe (contacts +15-26%, densehead crossings 566 -> 571) |

Sweep of the chain head: 11 failed keys -- 10 unreach-200 work classes (step 3, total work 0.91x) and the
pre-existing `aware-cross-behind` click a00 t90 floor (as at the base). Retail byte-identical on every row.
Ruling (i) floor keys (aware-headon / seen / unseen click t90) are unchanged from the base by this chain; the
live-corridor + re-probe A/B clears aware-seen's (1952 vs Retail 1877 x1.1) but not aware-unseen's (awareness off there).
ctest (`-L quick` and every non-nightly `legion_*`): only `legion_acceptance_crowdheld_legion` fails (W5/W9 gate, as at the base).

## W7 step 0 (2026-10-10): instruments, fixtures and the base for crossing / opposing traffic (no sim change, protocol 245)

Base head 2d671644 (W5 landed over C1 and W6). Every row below is measured on it with the step-0 instruments
(optimized Debug, offset 0 unless noted; the world-test fixtures run the awareness code ON and OFF through
`LegionNavigator::setAwareOffForTest`). **Hash-identical:** `tools/legion_identity.sh --quick` base vs candidate -- 181 rows SAME
(replays L-bench / R-bench final and 61 / 62 checkpoints, both navigation goldens, `--mpai` Legion 4d8c06c9747f5a66 and
Retail 56cfcbf8ef57181e at 60 s, check-determinism golden dcef618cd2e4d558, the 12 quick crowdbench rows, every existing ctest);
the 8 FAIL rows are the 7 new tests ("missing in one build") and `legion_acceptance_crowdheld_legion`, which fails in both builds
(W5/W9 hard gate). Windows crowdbench rows below ran on the same commit.

What was added (plan W7 step 0 list):

| Plan item | Where |
|---|---|
| `legion_detours` (existing), `legion_parts`, `legion_aware_pairs` (existing), `legion_aware_builds`, `legion_aware_latency_max` (+ `_sum`, `_n`), `legion_giveway_ticks`, `legion_giveway_timeouts` | `Stats` fields exported by crowdbench's `forEachStat` loop as `legion_<name>` and by the scenario runner (`probe aware`) as `aware.<name>`; additionally `aware_encounters`, `aware_replans`, `aware_work` (chain cells + corridor tests), `giveway_starts`. Never hashed, not in `work.legion_total` |
| `last_cross_tick` | already in every crowdbench row (`first_cross_tick`, `last_cross_tick`, `cross_gap_max`) |
| aware work counter | `aware_work` plus the per-tick maximum of `aware_pairs + aware_work` (`aware.work_max`, `NavWork::awareWorkMax`) |
| promote probe_rb02 | `legion_world_test rb02` (`legion_w7_rb02`): ring clot N = 52 / 104 / 208, the Holding-queue corridor, the plug; `TAK_RB02_FULL=1` adds the Stopped control and the no-clot rows |
| awarebig (768x768), awaredense, crossthree, the long-stream crossing | `legion_world_test awarebig awaredense crossthree crosslong` (`legion_w7_*`), and as scenarios `crosslong.scn`, `aware-cross-behind.scn` |
| crowdbench opposing doors / bridges, crossingcolumns | scenarios `opposingdoors`, `opposingbridges`, `crossingcolumns` (second stream a separate command 100 ticks after the first; outside the default matrix) |
| route-behind metric (decision 5) | `behind NAME LATER EARLIER` shape (`legion_observe::BehindProbe`): `behind.*.extra_cells` / `.detour_permille` (how far the later group deviates) and `.wait_ticks` / `.wait_run_max` / `.waits` (waits at the stream edge; **must be 0 after W7**), plus `giveway_ticks` / `giveway_timeouts` (must stay 0: W7 builds no hold under decision 5) |

**Base values.** The audit's figures (b8a4110) are not reproduced on this head: W2-W6 moved them, so these are the W7 base.

| Fixture | ON (awareness as today) | OFF (`setAwareOffForTest`) |
|---|---|---|
| aware cross (24 v 24, `aware`) | done 2409, stopped 9.8%, contacts 10.90 | (no OFF arm in `aware`) |
| aware headon | done 2251, stopped 5.7%, contacts 4.85 | |
| aware enemy-seen / unseen / attack | 2701 / 5.0% / 2.76; 2476 / 11.2% / 7.22; 2611 / 8.2% / 6.87 | |
| awarebig cross | done 8426, stopped 1.9%, contacts 1.83; 1 encounter, 1 build, latency 48; later group waits 283 ticks (2 waits, longest 221) | identical (hash equal: awareness changed nothing) |
| awarebig headon | done 8156, stopped 1.8%, contacts 0.20; 2 encounters, 2 builds, latency max 7 / mean 4; extra path 11 cells | done 7580, contacts 1.42 (OFF finishes sooner: **ON > OFF on done**) |
| awarebig enemy | same as headon (enemy B): 8156, contacts 0.20 | 7580, contacts 1.42 |
| awaredense 200 v 200, 17 across in 40 cells (85%) | arrived 394/400 at 9000, stopped 26.5%, contacts 1.34, hold_max 1610, 2 encounters, 4160 detours | arrived 342/400, stopped 29.9%, contacts 2.17, hold_max 2318 |
| crossthree (3 x 24, lines 60 degrees apart, commands 60 ticks apart) | done 2926, hold_max 301, 5 encounters, 7 builds; stream C waits at A: 3 waits (run 224), at B: 1 (318) | done 3046, hold_max 481 |
| crosslong (200 long x 24 crossing, command at tick 400) | done 4546, hold_max 471; later group waits 771 ticks in 3 waits (longest 549), extra path 6 cells; 1 encounter | done 4282; waits 921 ticks (3, longest 682) |
| rb02 ring N = 52 / 104 / 208 (followers 40, 6000 ticks) | done 40 / 32 / 17 of 40; done@3000 23 / 17 / 16; p50 2812 / 2774 / 2211 | Stopped control and no-clot rows: `TAK_RB02_FULL=1` |
| rb02 Holding-queue corridor N = 208 | 40/40, p50 2521, p90 3376 | |
| rb02 plug (six 4x4 at a 3-cell gap, 9000 ticks) | 0/40 done, 40 holding, 6 Trapped | |

Route-behind on the scenario fixtures (`legion_scenario ... --offsets 0`): `aware-cross-behind` -- the later group b* waits 365 ticks in one wait
(path 108 cells for a 107-cell straight line, 16 per mille); `crosslong.scn` -- 3 waits, longest 759, 1049 wait ticks, extra path 6 cells (35 per mille),
`g.B.t90` 3461. Aware counters on `crosslong.scn`: 1 encounter, 1 build, latency 4 ticks, `aware.work_max` 116. After W7 every `wait_ticks`, `wait_run_max`
and `waits` above must read 0 and `giveway_*` must stay 0 (the later group routes behind; no hold).

The aware-headon / aware-seen / aware-unseen floor exceptions (lead ruling (i)) at offset 0, Legion vs Retail click t90:
headon a00 2031 vs 1925, b00 2262 vs 2013; seen a00 1932 vs 1877, b00 2162 vs 1968; unseen a00 2297 vs 1877, b00 2252 vs 1968. (The gate judges the
median over the 11 gate offsets; W7's exit must bring all of them within Retail.) Contacts per mille (pair a-x-b, 2 cells): headon 4.90 vs 8.78, seen 2.72 vs 8.15,
unseen 7.26 vs 8.15, cross 11.04 vs 6.99 (Legion worse than Retail only on the crossing).

crowdbench rows (Windows, hashes equal Linux on the three rows re-run there: opposingcolumns 250x1@100 5719085a1cd8649b, crossingcolumns 250x1@100 854ba1aa7ab58424, opposingdoors 50x2@100 7a7efbf53b7a3c17; 6000 ticks unless noted, seed 0; seeds 1 equals 0 where no randomness; Legion / Retail):

| Row | crossed | arrived | other |
|---|---|---|---|
| opposingcolumns 250x8@50 | 892 / 1000 | 626 / 493 | spin 2884 (4 units) vs 8583 (444); held at end 344 vs 214; cross_gap_max 1243 vs 157; seed 2: 875 / 1000, arrived 621 / 488 |
| opposingcolumns 250x1@50 | 112 / 125 | 67 / 63 | gap 1990 vs 175 |
| opposingcolumns 250x1@100 | 250 / 250 | 239 / 67 | gap 22 vs 54 |
| opposingcolumns 2000x1@100, 12000 ticks | 1194 / 968 | 597 / 4 | last cross 10067 vs 11998; gap 1933 vs 732 |
| opposingdoors 250x2@100 | 107 / 74 | 68 / 8 | gap 3765 vs 4005; held at end 430 vs 459 |
| opposingdoors 50x2@100 | 69 / 30 | 65 / 0 | |
| opposingbridges 250x2@100 | 53 / 52 | 0 / 0 | held at end 500 vs 492 (the 64-cell bridge is one lane: head-on jam in both) |
| opposingbridges 50x2@100 | 33 / 22 | 4 / 0 | |
| (doors / bridges 250x2@100, one-way, for reference) | 500 / 500 | 351 / 135; 389 / 103 | |
| crossingcolumns 100 / 250 / 500 x1 | 100 / 250 / 500 (all arrive at 2877 / 3342 / 4376) | 100 / 250 / 500 | Retail: arrived 99 / 236 / 486, spin 221 / 517 / 2225 |

Legion counters on those rows: `legion_parts` 763-869 on opposingcolumns 250x8@50 (0 on the others), `legion_aware_encounters` 0 on every crowdbench
row (per-unit goals never form an aware mover of 8), so MV-09 is exercised by the scenario and world-test fixtures, not crowdbench.

## W5 land over C1 (2026-10-10): reach rings on the C1 + W6 head (protocol 245)

Head: `task-w5-land` = W5 (merged over W6, was protocol 244) + `origin/main` `35e03191` (C1 over W6, protocol 244) merged,
renumbered **245**, plus one sim change found by the combination (below), the Legion navigation golden, the baselines and
docs. User decision **W5-1** (2026-10-10): W5 lands with the three new Retail-floor results and the six open W5/W9 hard gates;
the gates move to W9's exit (`w5-w9-gates.md`).

**What the combination broke, and the fix.** C1 pauses a first field once it covers its members; W5's ring planning
(`ringClaim`) reads a *done* field. On the first merged sweep every attack fixture planned its ring late or never:
`attackring-r200-64` `reach.ring.ever_end` 57 -> 37 and `damage_end` 3448 -> 1844 (Retail 3648), `struct-60` `damage_end` 23846 -> 12714,
f1-64 2326 -> 1719, mob-40 1961 -> 1571, 15 new Retail-floor failures (`ever_t1200` / `damage_*` of f1-16, f1-64, f2-64, mob-40, r200-64,
squad-64, struct-60) and 764 failed keys. Fix (`pausable`, one line): an attack or guard group's first field is never paused.
With it the twelve attackring fixtures and `ringcross` are **hash-identical to W5 alone** again (all 13 theme tables above hold).
Cost: the battle armies' attack groups lose C1's saving on those keys (battle-assault `refresh_completed` C1 alone 40, here 335 = W5 alone 347; 2x250 `detours` 642 -> 955 = W5 alone 956); the battle cost gates still pass (cost row).

| Gate | Result |
|---|---|
| Scenario set (`legion_check check --cumulative --require-all`, all 120 files, both modes, gate offsets, serial == workers on all 240 lines; Windows, optimized Debug, head `af0f9ac3`) | **PASS** after licensing (25242 keys, 0 failed, 392 ratchets left unapplied (ruling (q)), 1708 licensed, 175 floor exceptions). Before licensing 55 failed: 33 hash lines of the three chase rows, 12 `fields_paused` first non-zero, 10 battle keys |
| What the combination moved (per-row hashes, same sweep, against W5 alone `merged-all` and against C1 `c1sweep`) | against W5: 14 of 120 scenarios, Legion only: the four battles, `chase-1`, `chase-50`, `chase-line` (C1's paused first solo builds: `field_work_first_solo` 751760 -> 308552 on chase-1 / chase-line, 820160 -> 378032 on chase-50, `fields_built` 21 -> 19 / 24 -> 22, registrations and every theme key unchanged), and the seven C1-over-W6 rows already in main's baseline (the six squad dead-end rows and `mixed2`). Retail lines and the other 106 Legion rows are identical, including every attackring fixture, `ringcross`, `fight-retarget`, goalblock, lateblock, staticblock and keelturn. Against C1: the four battles (W5) only |
| Licensing by cause | (1) chase-1 / chase-50 / chase-line hashes retaken (C1 cause: first solo builds pause; the keys that moved improved). (2) `fields_paused` first non-zero on keelturn-0/90/180 and chase-1/50/line baselined at the measured value (C1's own counter on W5 fixtures). (3) 10 battle keys re-pinned at the combination's measured values, reason "C1 over W5": battle-assault `field_work_refresh_idle` (p99, total), `field_work_refresh_moving.total`, `refresh_completed.total`, `rechoice_bfs_cells`; 2x250 `detours`, `trace_cells`; 2x500 `formation_ring_cells`, `slot_search_cells` -- all at the W5-alone level within about 5% (C1's low base values for these keys came in with its baseline; its saving does not apply to ring-planning attack groups) |
| Retail-floor exceptions (W5-1) | the three new ones are floor exceptions citing W5-1 with deadline W8/W9: attackring-wall-64 `wall_touch_permille` 58 vs Retail 25 and `wall_touch_near_permille` 112 vs 81 (AR-06), ringcross `g.M.t90` 4291 vs 3854 (MV-06). Unchanged by the combination |
| W5/W9 hard gates (W5-1) | all six moved to the **W9 exit**: baseline exceptions of doorplug-112 (wall touch, near), tail-corner380 (near), doorplug-124 `g.A.done` are now cluster `W9`; motion-cross `click.u01.done` licence cites W5-1; `legion_liftflyers` bound stays open + 3 (`kLiftDetourSlack`, W9 restores 1); `legion_acceptance_crowdheld_legion` stays a known failure (W9) |
| `legion_cost` scaling and `legion_cost battle --cumulative` | ok / ok (run per-tick max / p99: 2x60 393639 / 120818; 2x250 396709 / 168523; 2x500 405292 / 225758; battle-assault 488796 / 397731). No baseline retake needed: the existing W5 retakes hold |
| crowdbench screen (86 rows, seed 0, both modes) | **0 differences** against main's C1 baseline (hashes and outcomes); the five W5 counters (`legion_engaged_now`, `_reach_slots_built`, `_soft_replans`, `_reseeds_in_place`, `_brisk_steps`) are the only new keys; baseline retaken with them |
| Legion navigation golden | regenerated: Retail 24 of 24 unchanged, Legion 8 of 24 differ from main (serial == workers); the pause fix does not move it |
| ctest | optimized Debug non-nightly 263 tests: 262 pass, `legion_acceptance_crowdheld_legion` fails (known, W9; `units_ever_terrain_stuck`); Release `-L quick` 246: the same one. Before the pause fix `legion_scenario_battle-field-2x60` also failed |
| `--mpai` Inner Circle 300 s seed 1 (takserver --local, clean XDG_DATA_HOME, `TAK_LEGION_VERIFY` on, build-o2) | Legion **b2261dc31dc84cdf**, Retail **b240750e5765c02b**: unchanged from protocols 242 - 244 |

## W4 C1 land (2026-10-10): paused solo first builds on W4 with B1 + B2, merged over W6 (protocol 244)

Head: `task-c1-land` = origin/main `6333e38d` (W4 with B1 + B2, protocol 242) + C1's code (`588f80ea` from
`task-w4-c1`, which was built on the B3-only exit `55a83ee2`), then protocol 243, the Legion navigation golden and
the baselines redone on this base. Lead rulings applied: (r) the battle licences, (s) jagged accepted. What C1 is:
`legion-pathfinding.md`, "Paused first builds"; its gates on the old base are in `task-w4-c1` (`3c986230`,
margin round 40 / 80 / 160 / 320, kept 40).

**Interaction with B1 / B2 (new on this base).** (1) B1 already counts its resumed suppressed refreshes in
`demand_resumes` and checks it against `refresh_suppressed`; C1's resumes of a paused build now count in their own
class, `paused_resumes`. (2) B1's blocked re-request counted a member stalled on a paused build as blocked (`!done`),
so `legion_staticidle`'s parked guards refreshed again (refresh_idle 12728 vs 0): a paused build is now a finished
field there, read through `known()` (`freeDescent` too). (3) B1's active rule also counts a group whose member waited
on its paused build in the last 2 ticks (such a member neither moves nor counts as blocked); in the runs below it
changes no hash (Ulasem seeds 1 / 2 identical with and without it). (4) B2's soft view: relaxations only ever add
soft cost, so a resumed slice cannot lower a settled potential; the debug invariants (`TAK_LEGION_VERIFY`: no
relaxation lowers a settled potential, member ids, `verifySoft`) hold in every Legion world test, the scenario
ctests, an `--mpai` Legion run, crowdbench jagged / rapidreplacement / open / doors 500x4 and churn 200x1, and
battle-field-2x60 with verify on.

**Merge over W6 (origin/main `17e42209`, protocol 243; this land is protocol 244).** The tables below were taken on the
W4 head before the merge; W6 landed first, so C1 now follows it. The combination was measured on the merged head `830d3cbc`
(code identical to the final head, which adds only baselines and docs):

| Gate | Result |
|---|---|
| Legion navigation golden | **regenerates to the same file** (24 of 24 checkpoints, serial == workers, Retail identical): W6's 24-body cohort has no flyers or squads and C1's golden already holds |
| Scenario set (`legion_check check --cumulative --require-all`, all 91 files, both modes, gate offsets; Windows sweep) | **PASS**: 16999 keys, 0 failed, 290 ratchets (not applied, ruling (q)), 1283 licensed, 112 floor exceptions, 768 report-only. No new licence and no re-take: the union of C1's and W6's licences covers the combination |
| What the combination moved | Per-row state hashes of the merged head against the C1 land head `00f51041` and against W6 `17e42209` (same sweep, 91 files): Legion hashes differ from C1 on exactly W6's rows (the squad dead-end rows deadend-w2-n40-closed-squad, deadend-w4-n120-closed-squad / room-squad, deadend-w4-n40-closed-squad, deadend-w6-n120-closed-squad / room-squad, and mixed2); they differ from W6 on exactly C1's four battle files; no row differs from both, Retail rows are identical to both |
| `legion_cost` scaling and `legion_cost battle --cumulative` | ok / ok (2x60, 2x250, 2x500, battle-assault; run per-tick max / p99: 2x60 393639 / 74137; 2x250 396709 / 104880; 2x500 405292 / 141812; assault 434165 / 393379) |
| crowdbench screen (86 rows, seed 0) | 0 differences against C1's retaken baseline; the 7 W6 counters (`legion_relifts`, `legion_cap_hits`, `legion_station_releases_a/b`, `legion_go_arounds`, `legion_station_overflow`, `legion_lift_target_polls`) are the only new keys; baseline retaken with them |
| `--mpai` Inner Circle 300 s seed 1 | Legion **b2261dc31dc84cdf** (twice on Linux, build-o2; Mac), Retail **b240750e5765c02b** (Linux, Mac): C1 and W6 each left both, and so does the combination |

C1 alone, on the W4 head:

| Gate | Result |
|---|---|
| rapidreplacement 2000x1 `legion_field_work` <= base x0.85 | **0.669** (161549344 -> 108002304); waiting 0 -> 0; arrived 2000 / 2000 (identical to C1 on the old base) |
| Ulasem 8-AI `fieldWorkFirstSolo` down >= 30% (seeds 1 and 2, reproducible) | **-45.6% / -47.6%** (54.48 -> 29.62, 57.14 -> 29.92 per unit-tick); seeds 3 / 4 -38.8% / -40.6% |
| `waitingMemberTicks` <= base +5% | crowdbench screen (86 rows, seed 0): Legion total 613831 -> 614700 (+0.14%); singleunit +0.1% / -0.7%; **churn 2000x1 0 -> 240** (one member waits ticks 1918-2150 for a field in a run whose field quota is pegged all 6000 ticks in both builds, ~1100 groups queued); every other row equal. Ulasem per unit-tick: s1 0.0295 -> 0.0286 (-3%), **s2 0.0191 -> 0.0304 (+59%)**, s3 0.0258 -> 0.0277 (+7%), s4 0.0411 -> 0.0247 (-40%); mean of the four 0.0289 -> 0.0279 (-3.6%). The waits are members of groups with no field while the field cell budget is full and nothing is past tenure (instrumented, seed 2: both base and C1 at the 48-map cell cap from tick ~1200 with `novictim`), so they follow each game's own trajectory |
| pausedemand | **PASS** (waits 2 ticks, paused_resumes 1) |
| crowdbench matrix (13 scenarios + jagged, 200x1 / 500x4 / 2000x1, seed 0, Legion) | every outcome key within 2% except jagged (ruling (s)): 2000x1 arrived_settled 694 -> 660, orders_complete 779 -> 754, spin 53 -> 62 -- the same numbers as on the old base; maze 500x4 crossed 473 -> 474; field work 0.40-1.00x |
| Scenario set (`legion_check check --cumulative --require-all`, 91 files, both modes, gate offsets; oden-win) | after the baseline commits **PASS**: 16987 keys, 0 failed, 259 ratchets (not applied, ruling (q)), 1292 licensed, 113 Retail-floor exceptions (unchanged). Before them: 24 unbaselined `fields_paused*` / `paused_resumes*` (battles only) and 28 battle-scenario keys (below) |
| 10-stripe-phase sweep (battles, TAK_X_STILL_PHASE experiment on both heads, Windows) | C1's systematic moves: `sched_group_visits` (battle-assault total median 6923 -> 102183) and `group_loop_iters` (x1.07-1.42) -- ruling (r)'s class. Every other failing key's C1 median lies inside the base's phase range (e.g. battle-assault `parked_no_progress` 263.5 [170..309] -> 245 [201..396]; 2x500 `pass_scan_cells` 780494 -> 776802). Combat keys at all 10 phases at or below W0; total Legion work per run 0.55-0.60x the base's median, field work 0.26-0.45x, per-tick p99 0.59-0.98x, max 0.92-1.00x |
| `legion_cost` scaling / battle (`--cumulative` and the ctest form) | ok / ok, after the contact windows' `group_loop_iters` of 2x60 (15870 -> 23254) and 2x250 (157510 -> 178521) were re-taken under ruling (r) (run max / p99: 2x60 393639 / 74137; 2x250 396709 / 104880; 2x500 405292 / 141812; battle-assault 434165 / 393379) |
| Exact | Retail navigation golden unchanged, Legion golden regenerated (16 of 24 move, serial == workers). `--mpai` Inner Circle 300 s seed 1: Legion **b2261dc31dc84cdf** (unchanged from protocol 242: as on the old base, C1 moves no Inner Circle hash) on Linux (twice, plus once with `TAK_LEGION_VERIFY`), Windows and the Mac; Retail b240750e5765c02b everywhere |
| Suites | Linux build-o2 full ctest 307 / 308; Release `ctest -L quick` 203 / 204; Windows (legion_, navigation, replay, fp/fixed/detmath, crowdbench) 194 / 195; Mac fp_contract / fixed / detmath / navigation_determinism 4 / 4. The one failure everywhere is `legion_acceptance_crowdheld_legion` (units_ever_terrain_stuck 1), which fails identically on main `6333e38d` (recorded for W5) |

**Baseline changes.** Work classes: `fields_paused` / `paused_resumes` max / p99 / total for the four battle files
(Legion), the only scenarios where they are non-zero. Licences (28, battles only): 17 of ruling (r)'s class
(`sched_group_visits`, `group_loop_iters`), re-taken at this head; 10 licences and one bound under the B2 stripe
phase (rulings (k)/(p): this head's phase-0 draw exceeds what main's phase-0 values license, but C1's 10-phase median
is inside the base's own range): `formation_ring_cells`, `still_units_processed`, `completion_dist_3/4/6` and
`completion_dist_sum.max`, `pass_scan_cells`, `mission_legs_4.max`, 2x60 `field_work_first_slot`, and battle-assault
`parked_no_progress` (a bound, which licences do not cover: 346 -> 396; **lead to confirm**). The crowdbench screen
baseline is retaken on this head.

## W6 exit (2026-10-10): flyers and group records (protocol 243)

Head: `task-w6-land` = `task-w6-steps` (`66ac838c`: steps 0-6, step 7 dropped by C20) on the W4 land head `6333e38d`
(origin/main had not moved: no other lane or C1 had landed, so the step-0-relative baselines are the W4 land ones).
Protocol 242 -> 243 with its note in `src/net/protocol.h`; `replay_test` asserts 243 and refuses 242. Verified on Linux
(optimized Debug, Release), Windows (scenario set, quick ctest, `--mpai`) and the Mac (hash check); see the table.

W6 is a behaviour step for flyers and group records only. Its Declared hash-identity claim -- every crowdbench Legion row
without flyers or squads is unchanged -- holds: the committed screen (86 rows, both modes, seed 0) shows **0 differences**
against the W4 land baseline (the 7 new W6 counters are the only new keys, and the screen baseline is retaken with them).
Of the 91 scenario files only **seven Legion rows change their state hash** (the six dead-end squad rows, `mixed2`); the
other 175 result lines, every Retail row included, are identical in every key.

### Gates on the land head

| Gate | Result |
|---|---|
| Scenario set (`legion_check check --cumulative --require-all`, all 91 files, both modes, gate offsets, serial == workers on all 182 lines; Windows) | **PASS** after licensing: 16975 keys, 0 failed, 1279 licensed, 112 floor exceptions in the run, 768 report-only. Before licensing it failed 32 keys, all on the seven changed rows (below) |
| Retail-floor exceptions (baseline file, non-spread) | **124 -> 123**: `aware-headon/click.a00.t90` (MV-09) passes the floor again (2086 vs Retail 1925) and is removed; none added. Three offset-spread exceptions are added on `mixed2` (`g.F.relifts`, `g.F.relifts_max`, `g.F.takeoffs_max`, gated on the median of 5 offsets) |
| W5/W9 hard gates (`w5-w9-gates.md`) | not touched by W6: none worsened, none cleared (they belong to W5/W9); `legion_acceptance_crowdheld_legion` still fails as at the W3 step-0 head |
| Cumulative combat gate (battle-field-2x60/2x250/2x500, battle-assault; `work.legion_total` max / p99 / total) | all four rows are **hash-identical to the W4 land head** (W6 has no flyers or squads there), so the work equals the W4 land numbers: 2x60 393549 / 101065, 2x250 396376 / 173083, 2x500 418206 / 236732, battle-assault 508959 / 403764 -- every per-tick max and p99 is at or below the W0 base (x1.00) except battle-assault max, which carries the ruling (p) phase gate (its 10-phase median 476843 <= W0 484465) exactly as at the W4 land |
| New work class `lift_target_polls` (FL-01 combat poll, declared bound <= lifted flyers / 8 a tick) | bound entries `<= 3` (liftstream, 24 flyers) and `<= 2` (mixed2, 16 flyers) on max and p99; measured 3 and 1. Fault injection: the same bound set to 2 and 0 fails both (`bound <= 2, got 3`, `bound <= 0, got 1`). Total Legion work on liftstream, mixed2 and the battle rows passes its 1.10x bands (scenario set PASS) |
| Exact | Retail navigation golden and hashes unchanged; the Legion navigation golden **regenerates to the same file** (24 of 24 checkpoints agree, serial == workers: the 24-body cohort has no flyers and no squads); `check-determinism.sh`, `check-detmath` OK; serial == workers on every scenario line; `TAK_LEGION_VERIFY` clean on the lift and touchdown cases |
| `--mpai` Inner Circle 300 s seed 1 (`takserver --local`, clean `XDG_DATA_HOME`) | Legion **b2261dc31dc84cdf** twice on Linux, Retail **b240750e5765c02b**: both equal the W4 land values (that match has no squads and no lifted flyers in 300 s), so the recorded hashes stay |
| crowdbench screen | 86 rows, 0 differences vs the W4 land baseline; baseline retaken (172 lines: the new W6 counters `relifts`, `cap_hits`, `station_releases_a/b`, `go_arounds`, `station_overflow`, `lift_target_polls`) |
| ctest | see the list below the table |

### What W6 delivers (`legion_flyers_test`, 17 cases; step 0 head -> land)

| case | step-0 head | land | PLAN target | met? |
|---|---|---|---|---|
| mixedsquad Ctrl+1 move | done 3241, away 2153, landed_ahead 315 | done 2656, away 288, landed_ahead 0, hover 18 | done <= 2650, away <= 300, 0 | away and ahead yes; done 6 ticks over |
| mixedsquad Alt+1 offset +1 | illegal footprint 10 ticks | 0 | 0 | yes |
| mixedseal | hover 5149, landed 0/8 | hover 458, landed 8/8 | <= 1200, 8 | yes |
| mv08group Ctrl+1 | away 1702, ahead 251 | away 307, ahead 0 | <= 300, 0 | 7 px over |
| deadendrejoin w4/6/8/12 | holding 4/1/5/0 | 0/0/0/0, no anchored body re-ordered | 0 | yes |
| ctrlmixed two-click | away 2048 | away 232-266, 0 cross pairs | <= 300 | yes |
| factoryjoin | settles 611 after reaching | 71, 1 order | <= 600 | yes |
| landunder / descentwalkin | overlap 394 / 1459, 125 / 130 | 0 / 0, 0 / 0 | 0 | yes (descentwalkin arrival 555 vs <= 286: no) |
| liftcombat | react 322 | 1 | <= 16 | yes |
| fl04 corridor | hover 1134, landed 0/8 | hover 719, 8/8 | <= 1200, >= 7/8 | yes |
| fl05match80 | done 2226-2339 | 856-1016 | 883-1046 +-10% | yes |
| fl05unreach | hover 8638, 1897 px off | hover 458, 141 px | <= 1200, <= 128 px | 13 px over |
| splitpatrol | station ticks 12000 | 0 | 0 | yes |
| mixed2 (scenario) | spins 101, illegal overlap 3746 | spins 0, illegal overlap 0 | down | yes |

Open at the land (carried, none a regression): `ctrlmixedbig` arrival over a 200-body crowd (away 574-597), `fl04 box` far
168 px (<= 128), `fl04 big440` 9 of 24 flyers still circling a crowd-covered site (the post-release cap floor is not shipped:
it moved Alt+1 start offsets +21% / +53%), `mv08deadend` nearest-to-point 36 px (<= 32), the Ctrl+1 fight run's ground
done 4636 vs Alt 2611, `auditlift40` hover 1807 (7 ticks over). The touchdown backstop (C5) and step 7 are not built.

### The seven changed rows and their licences

Full list of the 165 moved keys (old -> new, intended or incidental): [legion-exit-w6-moves.md](legion-exit-w6-moves.md).

- **Dead-end squad rows** (`deadend-w2-n40-closed`, `w4-n40-closed`, `w4-n120-closed`, `w4-n120-room`, `w6-n120-closed`,
  `w6-n120-room`, all `-squad`): step 4 (merged rejoin) no longer pulls a settled dead-end queue back on itself. Outcome
  *improves*: `g.A.arrived` 8 -> 10, 19 -> 24, 4 -> 6, 17 -> 22, 31 -> 34; `open_still900` and `still900_ever` 2-4 -> 0 (seven
  of them ratcheted); `parked_held` -50% to -95%; `stopped_permille` -13% to -41%; `held_rechecks` -30% to -80%; one group
  and one built field instead of two. The cost: members settled in the dead end stay still, so `work.still_units_processed`
  *total* rises (8027 -> 22298, 12508 -> 22803, 5905 -> 7830, 18099 -> 23598, 19123 -> 23859; per-tick max and p99 fall from
  120 to 4); it is licensed per row as a step 4 consequence. `deadend-w4-n120-room-squad` `wall_touch_near_permille`
  590 -> 666 (settled bodies stand at the wall) is licensed too; it is not a Retail-floor key. Total Legion work is within 1.10x.
- **mixed2** (Legion): `spins` 101 -> 0 and `g.F.illegal_overlap_ticks` 3746 -> 0 (ratcheted) are the gain of FL-03 and the
  no-yaw rule. The price: `g.F.takeoffs_max` 2 -> 3, `g.F.relifts_max` 1 -> 2, `g.F.hover_max` 56 -> 62 (+11%), and the
  follow-on ground work (detours 85 -> 93, held rechecks 466 -> 576, slides +11%, lift members walked +11%, completion
  distances +1-2) are licensed with their observed maxima; the takeoff and relift counts are small-integer keys, so they are
  also spread exceptions (2..4, 1..3, 16..21 over the gate offsets). Retail takes off 2 times per flyer on this fixture.
  `gate.mid.crossings` 24 -> 35 and `gate.mid.files_x100` 291 -> 307 move inside their bands.

### Ratchets and baseline

Seven references move (outcome keys of the changed rows: `open_still900`, `still900_ever`, `g.F.illegal_overlap_ticks`) and the
`aware-headon/click.a00.t90` floor exception is cleared. The other 253 ratchets `legion_check` prints are the W4 B2
stripe-phase moves on rows W6 did not touch (hash-identical to the W4 land) and the work-counter improvements of the changed
rows; they are not applied (lead ruling W4 (q): no blanket ratchet, and only improvements beyond the offset spread count).
`accepted_regressions` +22 (six dead-end `still_units_processed.total`/wall-touch entries and the `mixed2` flyer entries, each
with its reason); exceptions +3 (spread), -1 (floor). New entries: `work.lift_target_polls.*` bounds, `work.lifts.*` and
`work.relifts.*` bases for `liftstream` and `mixed2`.

## W5 exit (2026-10-09): units that never arrive -- reach rings, re-seed in place, late soft blocks (protocol 244)

Head: `task-w5-land` = `task-w5-steps` (`fbe9d019`: steps 1 AR-06, 2 AR-07, 3 MV-05, 6 AR-10 claims; MV-06 built three ways and
reverted under its stop rule; MV-18 out of scope) + the exit commits: a guard plans to its ward without a ring and `staticidle` warms up
2400 ticks (`4114ea08`), the protocol note and the Legion navigation golden, the baseline / legion_cost / docs commits, then
**`origin/main` merged in** (`b36151fb`: W6 landed on main as `17e42209` with protocol 243 while this exit was being prepared, so the W5
note became **protocol 244**; conflicts were only `legion.h` / `legion.cpp` (both sides' hooks kept), `protocol.h`, `replay_test` and the
two baselines) and everything below re-run on the merged head. Base = `task-w5-base` (`25406740`) = the W5 step-0 sim (`2b2dc14c` = W4 land
`6333e38d` + instruments) with W6's land merged in. The merge changes the state of seven scenarios only (the six dead-end squad rows and
`mixed2`, all already in W6's baseline); W5 on top of W6 moves exactly the 26 scenarios it moved on top of W4.
**Not every W5/W9 hard gate is cleared** -- see "Hard gates" and "What the lead has to decide".

### Gates on the exit head

| Gate | Result |
|---|---|
| Scenario set (`legion_check check --cumulative --require-all`, all 120 files, both modes, gate offsets, serial == workers on all 240 lines; Windows, optimized Debug, merged head) | **PASS**, 25206 keys, 0 failed, 1708 licensed, 319 ratchets left unapplied outside the W5 fixtures (W4 ruling (q)), 175 floor exceptions in the run. The same sim on Linux (before the merge) is hash-equal to Windows on all 240 lines |
| What moved | 26 of 120 scenarios change state (Legion only): the 21 W5 fixtures, the four battles, `unreach-200` (hash only, no key moves by more than 5%). Every other scenario and every Retail line is hash-identical to the base. 1609 keys moved by more than 5%: [legion-exit-w5-moves.md](legion-exit-w5-moves.md) |
| Baseline update | 21 scenarios' hashes retaken; 195 counters first non-zero or new baselined (the W5 classes `reach_slots_built` 5.9, `reseeds_in_place` 14, `soft_replans` 0.0051, `move_calls_by_state_6` 0.56 per unit as declared bounds; the rest band entries at the measured value); 478 losses licensed (`accepted_regressions`, pinned at the exit values, reasons by cause: the W5 fixtures 1 reason, the battles 1, MV-05 `group_loop_iters` 1); 50 offset-spread exceptions; ratchet on the W5 fixtures only (823 references raised, 26 Retail-floor exceptions cleared) |
| Retail-floor exceptions | the 123 that exist on `origin/main` are unchanged (cluster `W5/W9` still holds the 3 open hard gates); the W5 fixtures went 86 (step 0) -> 63, of which **3 are new floor failures** (below). Total non-spread exceptions 209 -> 186 |
| Cumulative combat gate (core-five medians, Windows) | below the W0 base on all twelve keys: battle-field-2x60 max 393639 / p99 108982 / total 19.8M (W0 398804 / 116526 / 23.6M); 2x250 396121 / 178117 / 88.1M (W0 438589 / 255750 / 119.9M); 2x500 405958 / 226189 / 214.8M (W0 528461 / 393727 / 271.6M); battle-assault 457355 / 403641 / 240.0M (W0 484465 / 416681 / 394.7M). Whole-run total work is 0.61-0.84x the W0 base, 0.85-0.91x the step-0 sim. AR-06's declared 1.25x per-tick tolerance is not used up: per-tick max is unchanged on 2x60 and below the step-0 sim elsewhere |
| `legion_cost` | scaling ok; battle files ok after two retakes: 2x60 `run` p99 121654 (the offset-0 draw; the check runs only offset 0, where the step-0 sim is 90329; core-five median 108982 vs W0 116526) and battle-assault `contactA` `aware_pairs` 18555 / `slot_search_cells` 8286803 (both ring work, +14% / +12%) |
| Field-quota peg run | unchanged (battle-assault 47 = base, 2x500 9, 2x250 8, 2x60 2): the 8-member ring fallback is not needed |
| crowdbench screen (Release, seed 0, 86 rows, both modes, merged head) | **0 differences** against W6's committed baseline, hashes included (the new fields fold only when set; no row sets one); the baseline is retaken only to add the five never-hashed W5 counters (0 on every row) |
| Exact | Retail navigation golden and hashes unchanged; Legion navigation golden regenerated (8 of 24 moved against W4's, serial == workers, Retail 24 of 24 equal; W6's golden was the same file, so the merged file passes unchanged); `check-determinism.sh` OK (dcef618cd2e4d558); `check-detmath` OK; serial == workers on all 240 scenario lines; observer_neutral passes |
| `--mpai` Inner Circle 300 s seed 1 (takserver --local, clean XDG_DATA_HOME, `TAK_LEGION_VERIFY` on) | Legion **b2261dc31dc84cdf** twice on Linux (merged head), and on Windows and the Mac; Retail b240750e5765c02b on all three. **Unchanged from protocols 242 and 243**: the 16-unit AI game never has two attackers on one target and no squads or lifted flyers, so no W5 or W6 field is ever set |
| ctest | merged head: optimized Debug non-nightly 262 tests: 261 pass, `legion_acceptance_crowdheld_legion` fails (hard gate, below); Release `-L quick` 244 of 245, the same one; Windows (touched areas, 247): 246, the same one (the `legion_scenario_battle-assault` window checks pass after the contactA retake); the nightly-labelled `legion_check_*` / scenario ctests were not run (the scenario sweep above is the same check) |
| Reconnect | Ulasem host + joiner + 6 AIs, `TAK_LEGION_VERIFY` on, joiner killed at 150 s and rejoined with `--mprejoin`: host and joiner both 2a209aaaaed61523 at tick 9000 (120 units), no DESYNCED / suspect in the server log (before and after the merge) |

### Theme targets (Legion, core-five medians, base = the step-0 baseline; Retail on the same binary)

| fixture | key | base | exit | Retail |
|---|---|---|---|---|
| attackring-f1-64 | `reach.ring.ever_end` | 3 | 39 | 44 |
| attackring-f1-64 | `reach.ring.damage_end` | 217 | 2326 | 2618 |
| attackring-f2-64 | `reach.ring.ever_end` | 5 | 9 | 15 |
| attackring-squad-64 | `reach.ring.ever_end` | 3 | 39 | 44 |
| attackring-f1-16 | `reach.ring.ever_end` | 1 | 16 | 16 |
| attackring-r200-64 | `reach.ring.ever_end` | 3 | 57 | 64 |
| attackring-mob-40 | `reach.ring.ever_end` | 2 | 34 | 40 |
| attackring-mob-100 | `reach.ring.ever_end` | 3 | 33 | 46 |
| attackring-struct-60 | `reach.ring.ever_end` | 6 | 42 | 60 |
| attackring-struct-60 | `reach.ring.damage_end` | 4237 | 23846 | 30000 |
| attackring-fight-60 | `reach.ring.ever_end` | 11 | 34 | 44 |
| attackring-guard-64 | `reach.ring.ever_end` | 15 | 61 | 64 |
| attackring-wall-64 | `reach.ring.ever_end` | 19 | 53 | 61 |
| attackring-wall-64 | `reach.ring.hold0_max` | 0 | 0 | 0 |
| attackring-wall-64 | `reach.ring.hold_out_max` | 2111 | 1482 | 0 |
| attackring-river-64 | `reach.ring.ever_end` | 6 | 12 | 16 |
| ringcross | `g.M.t90` | 3616 | 4291 | 3854 |
| ringcross | `g.M.done` | 3796 | 4876 | 4040 |
| ringcross | `reach.ring.ever_end` | 2 | 40 | 44 |
| fight-retarget | `g.F.arrived` | 37 | 38 | 40 |
| fight-retarget | `g.F.done` | -1 | -1 | 2721 |
| fight-retarget | `claims.bad_max` | 0 | 0 | None |
| chase-1 | `work.registrations.total` | 53 | 3 | None |
| chase-1 | `work.fields_built.total` | 53 | 21 | None |
| chase-1 | `work.field_work.max` | 349360 | 349360 | None |
| chase-50 | `work.registrations.total` | 2552 | 52 | None |
| chase-50 | `work.fields_built.total` | 53 | 48 | None |
| goalblock-200 | `g.A.arrived` | 131 | 129 | 177 |
| goalblock-near-200 | `g.A.arrived` | 193 | 191 | 167 |
| goalblock-fight-200 | `g.A.arrived` | 130 | 123 | 87 |
| lateblock-late-16 | `g.A.arrived` | 30 | 40 | 39 |
| lateblock-late-16 | `g.A.done` | -1 | 3440 | -1 |
| lateblock-before-16 | `g.A.done` | 3599 | 3599 | 3037 |
| lateblock-none | `g.A.done` | 2297 | 2297 | 2431 |
| staticblock-open | `g.A.done` | 1486 | 1486 | 1674 |
| staticblock-own | `g.A.done` | 2521 | 2521 | 2062 |
| staticblock-other | `g.A.done` | 2521 | 2521 | 2062 |
| staticblock-own | `creep.goal.ticks` | 1592 | 1592 | 83 |
| staticblock-open | `creep.goal.ticks` | 124 | 124 | 207 |

Reach (AR-06): every ring fixture reaches 31-107% more bodies than the base and closes most of the gap to Retail
(f1-64 3 -> 39 of 44, r200-64 3 -> 57 of 64, struct-60 6 -> 42 of 60 with damage at 80 s 4237 -> 23846 of 30000, guard-64
15 -> 61 of 64, mob-40 2 -> 34 of 40); **not met (improved but below Retail, legal improvement, the keys stay exceptions)**:
mob-100 33 (>= 40), fight-60 34 (>= 40), f2-64 9, wall-64 53, river-64 12. AR-07 (chase-1 registrations 53 -> 3 met; fields built 53 -> 21,
plan <= 15 not met and `field_work.max` unchanged: it is the first build). MV-05 (lateblock-late-16 30 -> 40 of 40, done 3440,
met). fight-retarget: 38 of 40 arrive at the median offset, `g.F.done` still never at the median (3332 at the offsets where it
completes; Retail 2721): "every member resumes and settles" is not met at the median. goalblock-200 131 -> 129 of 177 (Retail): the
AR-07 block-on-the-goal gap is not closed (that is an AR-07 follow-up, not a regression). MV-06 (brisk tier, S1) and MV-07 are not built
(stop rule / T2 A1 in W8): staticblock-own creep 1592 vs Retail 83.

### Hard gates carried from W3 (w5-w9-gates.md)

| Item | W5 exit | Cleared |
|---|---|---|
| doorplug-112 wall touch / near | 11 vs Retail 8, 64 vs 42 (unchanged) | **no** |
| tail-corner380 wall touch near | 2 vs 1 (unchanged) | **no** |
| doorplug-124 `g.A.done` at every gate offset | never at +1 and -4 (unchanged) | **no** |
| `legion_liftflyers` own detour <= open + 1 | 5 vs 2 (bound open + 3, unchanged) | **no** |
| `legion_acceptance_crowdheld_legion` | fails: 62 of 64 in goal, `units_ever_terrain_stuck` 1 | **no** |
| motion-cross g.u11 arrives at every gate offset | never at 7 of 11 (unchanged); `click.u01.done` never | **no** |

What was found (all measured on this head; the first four are W3's settle rule seen from the fixtures, the tails of a crowd, which no
W5 step touches):

* **crowdheld.** The last two of 64 units (13 and 29) leave the gate at tick 3600 after 32 units have filed through its 6-cell door and
  walk ~290 cells: they reach the lattice at ticks ~6500-7050 of a 7200-tick run and stand against the row of settled bodies that fills
  their goals' row (goals at a 3-cell pitch, one-cell gaps, the row against the divider wall). The settle rule asks the touching
  settled body to lie within two bodies of the *member's own point* (`sameDestination`), so a lattice member never touches a settled
  arrival "of its destination" and never settles; it creeps at cap/8 until the run ends. Tried and not landed: (a) same command but
  another destination counts as foreign after one still window and within twice the reach: unit 13 completes at 3.6 cells, unit 29
  (12.6 cells) does not, 62 / stuck 1; (b) a "sealed lattice" completion (5 still windows pressed against a nearer arrival of the
  same command): crowdheld passes (62 in goal, stuck 0) but `legion_acceptance_group_legion` falls to `arrived_settled` 62 of 64 (its
  late units are shut in the same way and arrive in the step-0 sim); with the stopped bodies as walls of a free-ground path search it is
  the same 62, and at 7 to 10 windows the group check still loses one while crowdheld stops passing. The two cases cannot be told apart
  by a still-window count.
* **motion-cross g.u11.** The unit stands at the edge of the west crowd 5-7 cells from the click, queued, with a free neighbour nearer
  the point (so `pressed` is false) and `stalledFor` < one window because it creeps 0.2 px/tick (cap/8) -- yet the window test calls it
  still. Treating two still windows as stalled makes g.u11 arrive at all 11 gate offsets and `click.u01.done` complete at 8 of 11
  (core-five median passes) -- but it changes 85 of 120 scenarios and adds ~750 failures (tails settle farther from their points:
  `complete_dist_max` / `complete_outside_radius` up in corner-*, gen1-*, deadend-*, doorplug-*); not landed.
* **doorplug-124.** The two bodies outside the 262 px disc at the offsets that never complete are the end of the queue through the door
  (263 and 288 px, 1-26 px outside): the chain rule lets each body settle one row behind the previous settled one (the dead-end corridor
  requirement). Not touched.
* **liftflyers.** Body 13 of the group (start z 44) ends at z 49 (5 cells, open run 2): the slot re-choice along the formation's west
  face after the flyers lift. Not touched.
* **doorplug-112 / tail-corner380** wall touches are moving bodies at clearance 0 in the door and at the map corner; unchanged by the
  ring and re-seed work.

### New Retail-floor failures (need a lead ruling; listed as `AR-06` exceptions in baseline.json)

| Key | Legion | Retail | floor x1.1 |
|---|---|---|---|
| attackring-wall-64 `wall_touch_permille` | 58 | 25 | 27.5 |
| attackring-wall-64 `wall_touch_near_permille` | 112 | 81 | 89.1 |
| ringcross `g.M.t90` | 4291 | 3854 | 4239 |

wall-64: 64 attackers go round a closed 8x8 wall box to reach spots on its far side (53 get a shot, 19 at the step-0 sim, Retail 61): the
ring puts every band-0 spot against the wall (the box's outer face is 4 cells from the target, band 0 lies 4.5-5.5 cells out, reach is 6), so
bodies on the way touch it. Widening band 0 to the reach edge for spots with wall clearance (tried, not landed) changed none of the numbers. ringcross:
a Move group walks through a ring of 48 Engaged bodies (own friends, never yielding): it goes round them at cap/8 in the gaps
(MV-06, not built): core five t90 3955..4921, +19% over the step-0 sim's 3616; `g.M.done` 4876 (never at 8 of the 11 offsets by tick 5000).
PLAN gate "ring-crossing within +10%": **not met** (+19%).

### Can fail

* The ratcheted references trip: a record with attackring-f1-64 `reach.ring.ever_end` 10 (exit 39) and chase-1 `registrations.total` 80
  (exit 3) fails `legion_check check --cumulative` with both keys (2 failed).
* `legion_world_test staticidle` fails at the old 900-tick warm-up (7 of 12 guards holding, 764640 refresh relaxations) and passes
  at 2400.
* The crowdheld and group acceptance checks trip on the experiments above (62 / 60 / 59 in goal, `arrived_settled` 60-63).

### Other findings

* `legion_staticidle` (W4 B1): with the reach kinds no longer taking the settle rule, twelve guards at a ward in a corridor take until
  tick ~2000 (ring: ~2800) to stop re-routing, where the W4 head's settle rule parked them by 900; the test now warms up 2400 ticks
  (at 900 ticks it fails, measured). A guard plans without a ring (guard-64 61 of 64 in 6 cells, was 63 with it).
* `unreach-200`: hash differs from the step-0 sim, keys do not (AR-07's illegal-goal re-claim path).
* The 15 `legion_check_*` nightly ctests read the baseline this commit updates; they pass for the same data as the sweep above.

### What the lead has to decide

1. (DECIDED, user W5-1, 2026-10-10: land; floor exceptions AR-06 / MV-06, deadline W8/W9) Land with the 3 new floor failures as AR-06 exceptions (ringcross `g.M.t90`, wall-64 touches), or hold W5 for them.
2. (DECIDED, user W5-1, 2026-10-10: land; the gates move to W9's exit) Land with the six open W5/W9 hard gates carried to W9 (crowdheld's ctest still fails), or hold W5 until a design for the lattice /
   queue-tail settle rule exists (it is the same rule in crowdheld, g.u11 and doorplug-124, and W3 B's).
3. The `staticidle` warm-up change (900 -> 2400 ticks) and the `legion_cost` retakes (2x60 p99 121654 at offset 0; battle-assault contactA
   aware_pairs / slot_search_cells) under the combat-lag reasons recorded in the baselines.

## W4 exit with B1 + B2 (2026-10-09): gates re-run on the combined head -- B2 fails the scenario set

Head: `task-w4-exit` = the B3-only exit (`55a83ee2`) + `task-w4-b1` (merge `cfeef687`) + `task-w4-b2` (merge
`4cb18d69`; the conflict was the NDEBUG invariant block: B1 wired `blocked_rerequests` / `demand_resumes` /
`refresh_suppressed`, B2 wired `soft_hash_verify_ticks` and replaced the softOwnerCmds check with `verifySoft`;
only `fields_paused` is still unbuilt) + `f445a44c` (B1's `freeDescent` read B2's removed `softCells`; now
`softCellCount`) + `7be70dab` (protocol 242 note covers B1/B2, Legion navigation golden, docs). Lead rulings
applied: (j) `legion_staticidle` without WILL_FAIL (passes); (k) B2's balance bound read as "per-tick max <= 10%
of the base's 30th-tick spike", its three world-test moves licensed as "B2 stripe phase" (the world-test ctests
pass under `TAK_LEGION_VERIFY`); (l) battle-assault `quota_peg_run_max` 47 is the base (unchanged: 47).
**Not passed: the scenario set fails on B2** (below). The licensing that would pass it is on branch
`task-w4-exit-b2lic` for a lead decision; this branch carries no B2 license.

### Gates on the combined head

| Gate | Result |
|---|---|
| Scenario set (`legion_check check --cumulative --require-all`, all 91 files, both modes, gate offsets; Windows, two lines re-run on Linux after a pipe interleave, equal hashes) | **FAIL: 113 keys** (16961 checked, 222 ratchets, 1183 licensed). All from B2: B1 + B3 alone (`cfeef687`) passes with 0 failed and 0 ratchets; `task-w4-b2` alone fails the same 113 (plus B3's anchor keys). 6 scenarios carry every outcome failure -- battle-field-2x60 (`g.A.arrived` 7 vs bound >= 10), 2x250, 2x500 (`g.B.arrived` 9 -> 4), battle-assault, mixed2 (`spins` 138 vs bound <= 117, `g.F.illegal_overlap_ticks` 2470 -> 5260), aware-headon (`pair.contacts` 384 -> 431, ruling (k)'s move) -- and the cumulative combat gate fails on battle-assault `work.legion_total.max` 511216 vs W0 484465. The rest are W3's tight work licenses (`still_units_processed` totals +0.1-2% over W3's pinned factors; its p99 0 -> 1-11 because the scan now runs every tick) |
| Phase sweep (why: is it B2 or the draw?) | Base (B1 + B3, whole scan moved to tick % 30 == k) and B2 (stripe shifted by k), k = 0..9, the six scenarios, gate offsets: every outcome key's B2 phase median lies inside the base's phase range (2x60 arrived 8 vs base 8.5 [7..10]; mixed2 spins 138 vs 135 [117..172], overlap 3360 vs 3173 [1346..5265]; 2x500 B arrived 7 vs 9 [2..21]); the old bounds were the base's phase-0 draw. Total Legion work per run, phase median, B2 / base: 0.985-1.012; per-tick p99 0.98-1.015; per-tick max 1.000-1.035; battle-assault max 476843 (B2) vs 460893 (base), both below W0 484465 -- the head's 511216 is B2's worst of ten phases. Systematic B2 work moves (phase medians): battle-field-2x60 `field_work_first_slot` x1.29 and `fields_started_by_kind_2` 8 -> 15, battle-assault `field_work_refresh_moving` x1.22, aware-headon contacts x1.13 (ruling (k)). An owner-set snapshot at tick % 30 == 0 (experiment `797faee3`, never landed) changes nothing |
| `legion_cost battle --cumulative` | run-level per-tick max / p99 / total at or below the W0 base on all four files (2x60 393639 / 90329; 2x250 419719 / 182698; 2x500 452070 / 243657; battle-assault 471347 / 402638, offset 0); one window fails x1.00: battle-field-2x60 `start` total 1767130 vs 1767129 (B2 samples one still body in the first 200 ticks) -- retaken on `task-w4-exit-b2lic`. Scaling ok (0.56 / 0.67) |
| Field-quota peg run | unchanged from the B3 exit: battle-assault 47 (= base, ruling (l)), 2x500 9, 2x250 8, 2x60 2, refreshchurn* 21 |
| B2 balance bound (ruling (k)) | per-tick `still_units_processed` max <= 10% of the base's 30th-tick spike on 81 of 89 Legion scenarios with still bodies (pocket-615-wall 530 -> 20, tail-wave 454 -> 16, corner-8x56 286 -> 13; Ulasem per B2: ~330 -> 25); not on 8 with one or two still bodies per residue (battle-assault 23 -> 35, battle-field-2x500 65 -> 31, 2x250 21 -> 14, 2x60 11 -> 2, split-450-pocket 31 -> 12, mazeapproach 4 -> 1, deadend-w2-n120 closed/room plain 4 -> 1 / 2 -> 1). Totals 0.90-1.02x except the battles (phase) |
| crowdbench screen, seeds 0-2, both modes (258 rows; base on the Mac, head on Windows) | Retail rows identical. Every Legion hash moves (softHash); 127 of 129 Legion rows outcome-identical (doors, bridges, dynamicobstacle all identical); churn 2000x1 differs: s0 spin -3.3%, s1 spin +4.2% and final_at_goal 1 -> 0, s2 spin +8.8%, crossed 69 -> 65, final_at_goal 3 -> 1 (mixed sign, over 2% on s1/s2). `still_units_processed` median 0.993x, field work identical |
| Exact | Retail navigation golden and hashes unchanged; Legion golden regenerated (24 of 24 moved, serial == workers); `check-determinism.sh` OK (dcef618cd2e4d558); `check-detmath` OK; serial == workers on all 182 scenario lines; observer_neutral and convoy verify pass |
| softHash verify (C31) | `TAK_LEGION_VERIFY` on: Legion world tests in ctest (all pass), `--mpai` x2 and the reconnect run: no verify failure |
| `--mpai` Inner Circle 300 s seed 1 | Legion **b2261dc31dc84cdf** (was 136218d83cbf7af8) twice on Linux (verify on), and on Windows and the Mac; Retail b240750e5765c02b everywhere |
| Cross-platform | merged head: the 240 scenario lines are the Windows sweep; the 34 scenarios that change state (the 26 W5 ones and the 7 W6 ones, plus `liftstream`) re-run on Linux are hash-equal to Windows on all 68 lines, serial == workers; eight combat fixtures (attackring-f1-64, -struct-60, -fight-60, -wall-64, fight-retarget, chase-50, ringcross, goalblock-200) at offsets 0 and 1 hash-equal on the Mac (arm64, before the merge); the Mac's navigation golden, `fixed`, `detmath`, `fp_contract` pass on the merged head |
| Reconnect | Ulasem host + joiner + 6 AIs, verify on, joiner killed at 150 s and rejoined with `--mprejoin`: host and joiner both 99b6fee23f78ab7e at tick 9000, no DESYNCED / suspect |
| ctest, full | optimized Debug 391 / 409, Release 378 / 396. Failing: the 15 `legion_check_*` nightly scenario checks (the scenario-set failures above), `legion_acceptance_crowdheld_legion` (W5), `cobanim` (no assets), `crusades_hardening_network` (flake). `legion_staticidle` passes |
| Retail-floor exceptions | no floor failure in the run; 124, unchanged |

Every key that moved by more than 5% against the B3-only exit (411: 191 intended, 220 incidental; no Retail key):
[legion-exit-w4-moves.md](legion-exit-w4-moves.md).

### What the lead has to decide

1. Land B2 with `task-w4-exit-b2lic` (its two baseline commits: scenario licenses, bounds 10 -> 7 and 117 -> 138, three
   spread exceptions; and the legion_cost window), reading every scenario-set move as ruling (k)'s stripe
   phase; or hold B2 and ship B1 + B3 (that head passes every gate).
2. The cumulative combat gate has no license path in `legion_check`: battle-assault `work.legion_total.max`
   511216 (core-five median at the one phase the head has) vs W0 484465. B2's phase median (476843) passes;
   the base's own range reaches 488795. A ruling is needed (read it on a phase median, raise W0 with a
   combat-lag reason, or treat as a fail).
3. Whether the 222 ratchets (improvements at the phase-0 draw) should be applied at all, given the phase spread.

### Rulings applied at the land (2026-10-09 18:30)

(o) B2 lands with the licence branch `task-w4-exit-b2lic` (105 accepted regressions "B2 stripe phase (ruling k)", 3 spread
exceptions, bounds 10 -> 7 and 117 -> 138, the legion_cost 2x60 start window +1). (p) The cumulative combat gate for a
phase-sensitive change is judged on the MEDIAN over 10 stripe phases: `legion_check` reads a `phase_gate` on a combat key's
baseline entry (>= 10 per-phase values and a reason; it passes a value above the W0 base when the median of the phases is
at or below W0 and the value is within the recorded range); battle-assault `work.legion_total.max` carries it. (q) No blanket
ratchet: the 222 phase-0 ratchets are not applied. Tooling to judge "improvement beyond the 10-phase spread" per key does not
exist (the stripe-phase switch `TAK_X_STILL_PHASE` is an experiment that never landed, so a phase sweep is not reproducible
from this tree), so NO key is ratcheted at this land.

battle-assault `work.legion_total.max`, core-five median of the gate offsets (Windows, optimized Debug, legion mode), per phase
k of the whole-scan / stripe residue:

| k | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | median | range |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Base (B1 + B3, whole scan at tick % 30 == k) | 480635 | 452202 | 452160 | 456740 | 457041 | 456501 | 467693 | 488795 | 471289 | 464745 | 460893 | 452160..488795 |
| B2 (stripe shifted by k) | 511216 | 489858 | 461683 | 467983 | 488210 | 450785 | 493734 | 468726 | 462327 | 484960 | 476843 | 450785..511216 |

W0 base 484465: B2's phase median 476843 passes (x0.984); the head's own draw (phase 0) is 511216, the worst of the ten.

## W4 exit (2026-10-09): demand-driven upkeep -- B3 only (protocol 242)

Head: `task-w4-exit` = `task-w4-s0` (`3891dac7`) + `task-w4-b3` (`a24717a4`, merged `--no-ff`) + step 4 + this exit.
**Landed: B3** (event-driven erasure of anchors, approach-done and parting records through
`World::noteOrders`; prune's hashed 256-a-tick backstop cursor walks members plus records). **Not landed:**
B1 (demand-driven refresh, `task-w4-b1`: missed its live-Ulasem bounds -- relaxations 0.94-1.03x base vs
<= 0.55, refresh_idle 0.35-0.42x vs <= 0.30 -- because refresh is under 2% of the benchmark's field work) and
B2 (striped `scanStill`, `task-w4-b2`: the 1.1x residue-balance bound and three world-test moves over 2%);
both wait for lead decisions, so `legion_staticidle` stays `WILL_FAIL` and the incremental-softHash verify
gate does not apply yet. C1 is out of scope. Measured on Linux (local, optimized Debug) and the Mac
(scenario-set half, base crowdbench screen); oden-win not run (host unavailable). Scenario results are
platform-independent.

Step 4: protocol 241 -> 242 with its note (`src/net/protocol.h`), `replay_test` asserts 242 and refuses 241.
The Legion navigation golden is **unchanged** (all 24 Legion checkpoints and all 24 Retail checkpoints equal
the file, serial == workers: none of its runs has more than 256 members plus records, so the backstop cursor
walks exactly what the members-only cursor did). `--mpai` Inner Circle 300 s seed 1 (`takserver --local`,
clean `XDG_DATA_HOME`): Legion **136218d83cbf7af8** twice, Retail b240750e5765c02b -- both unchanged, so the
recorded hashes stay. `docs/legion-pathfinding.md` "Upkeep on demand" documents the event records and the
(unchanged) refresh policy and still-body scan.

### Gates on the exit head

| Gate | Result |
|---|---|
| Scenario set (`legion_check check --cumulative --require-all`, all 91 files, both modes, gate offsets, serial == workers on all 182 lines) | **PASS**: 16949 keys, 0 failed, 43 ratchets (all `work.anchor_walk_iters`), 1269 licensed, 113 floor exceptions in the run, 768 report-only. Every result line is identical to `task-w4-b3`'s sweep (hashes included). Against the step-0 sweep every observer, work, gauge and churn key is identical except `work.anchor_walk_iters`; Retail hashes identical; Legion hashes differ on 17 of 91 files (members + records over 256: the hashed cursor) |
| Ratchet + baseline | ratchet applied (43 references); the 108 `work.anchor_walk_iters` entry values retaken with a W4-exit reason; re-check PASS, 0 ratchets. No `accepted_regressions` added (no loss to license) |
| Retail-floor exceptions | 124 (non-spread), the same as at the W3 close (`0767dfab`) and step 0: not grown, none added or cleared |
| Cumulative combat cost (`legion_cost.py battle --cumulative`, first gated here) | **PASS**, per-tick max / p99 vs the W0 base (x1.00): 2x60 393639 / 107751 vs 393639 / 107751 (1.000 / 1.000); 2x250 396709 / 188095 vs 2287518 / 1256335 (0.173 / 0.150); 2x500 405292 / 230724 vs 2775448 / 1318758 (0.146 / 0.175); battle-assault 480635 / 403369 vs 2766914 / 1139626 (0.174 / 0.354). Identical to step 0 |
| `legion_cost` scaling | ok (whole-run ratio 0.56 / 0.67); cost-corner / cost-open N400 `anchor_walk_iters` 720322 -> 613760 and 625732 -> 511360 (retaken) |
| Field-quota peg run | equal to step 0 on every scenario: battle-assault **47** (= base; over the 30 of brief section 2, pre-existing at step 0 and untouched by B3), 2x500 9, 2x250 8, 2x60 2, refreshchurn* 0-21 |
| crowdbench screen (86 rows, seed 0, both modes) | head (local) vs the step-0 head `3891dac7` (the Mac): every outcome and counter key identical in all 86 rows except `legion_anchor_walk_iters` (36 rows) and the state hash of 16 Legion rows; every Retail row identical |
| Exact | Retail golden and hashes unchanged; `check-determinism.sh` OK (golden dcef618cd2e4d558); `check-detmath` OK; serial == workers everywhere; observer_neutral and convoy verify in ctest pass |
| Reconnect | Ulasem host + joiner + 6 AIs (Legion), joiner killed after 150 s and rejoined with `--mprejoin`: no DESYNCED/suspect; host and joiner both end at 76d4181251c4c264 at tick 9000 |
| ctest, full | Release 393 / 396 and optimized Debug 406 / 409. Failing: `legion_acceptance_crowdheld_legion` (pre-existing, W5 hard gate), `cobanim` (the worktree has no `assets/extracted`), `crusades_hardening_network` (passes when re-run alone; load flake, triage pending). `legion_b3events` passes |

### Post-W4 cost baseline (C21)

The caps 3.0 declares against the post-W4 head read: `tools/scenarios/post-w4-cost.json` (every scenario's
Legion `work.legion_total` max / p99 / total, gauges and churn bins on this head; e.g. aware-headon per-tick max
99210, aware-cross 209270, battle-field-2x60 393549, 2x250 396040, 2x500 406021, battle-assault 491285 as the
runner reports them), the battle window tables above, and the live 8-AI Ulasem benchmark
`tools/scenarios/ulasem-w4-exit.json` (`TAK_BENCH=4`, Legion, 7200 ticks, referee == client, no desync):

| seed | units max | field_work / unit-tick (x step 0) | refresh idle | refresh moving | quota_peg_run_max | anchor_walk_iters / unit-tick (x step 0) | still_per_residue_max |
|---|---|---|---|---|---|---|---|
| 0 | 3549 | 136.55 (0.98) | 0.501 | 1.101 | 1098 | 0.0973 (0.89) | 28 |
| 1 | 3555 | 144.61 (1.02) | 0.842 | 0.896 | 3517 | 0.0983 (0.90) | 26 |
| 2 | 3507 | 143.74 (1.01) | 1.032 | 0.983 | 3521 | 0.0975 (0.88) | 25 |

Churn bins flat (max/min of bins 2-4: 1.06 / 1.01 / 1.00). The live game is not reproducible run to run (the
same tree gave seed 0 hash bb99707028c0036b here and d29171633db05dde in B3's run), so per-seed ratios to step
0 compare two different games: field work is within 2%; the quota peg run of seeds 1 and 2 (3517 / 3521 vs
1479 / 2506) is game-dependent, not a B3 effect (B3 does no field work).

### Every key that moved by more than 5% against the step-0 base

All 44 are `work.anchor_walk_iters` (Legion), all intended (B3: the cursor bounds the walk at 256 a tick).
No Retail key and no outcome key moved.

| key | old | new | change | kind |
|---|---|---|---|---|
| corner-1x448/legion/work.anchor_walk_iters.max | 448 | 256 | -42.9% | intended |
| corner-1x448/legion/work.anchor_walk_iters.p99 | 448 | 256 | -42.9% | intended |
| corner-1x448/legion/work.anchor_walk_iters.total | 3173634 | 2303360 | -27.4% | intended |
| corner-8x56/legion/work.anchor_walk_iters.max | 449 | 256 | -43.0% | intended |
| corner-8x56/legion/work.anchor_walk_iters.p99 | 448 | 256 | -42.9% | intended |
| corner-8x56/legion/work.anchor_walk_iters.total | 3165014 | 2303280 | -27.2% | intended |
| densehead/legion/work.anchor_walk_iters.max | 397 | 256 | -35.5% | intended |
| densehead/legion/work.anchor_walk_iters.p99 | 397 | 256 | -35.5% | intended |
| densehead/legion/work.anchor_walk_iters.total | 1192870 | 1023360 | -14.2% | intended |
| gen1-route-61923/legion/work.anchor_walk_iters.max | 443 | 256 | -42.2% | intended |
| gen1-route-61923/legion/work.anchor_walk_iters.p99 | 443 | 256 | -42.2% | intended |
| gen1-route-61923/legion/work.anchor_walk_iters.total | 2120687 | 1663360 | -21.6% | intended |
| gen1-route-65845/legion/work.anchor_walk_iters.max | 443 | 256 | -42.2% | intended |
| gen1-route-65845/legion/work.anchor_walk_iters.p99 | 443 | 256 | -42.2% | intended |
| gen1-route-65845/legion/work.anchor_walk_iters.total | 3126323 | 2303360 | -26.3% | intended |
| pocket-304-open/legion/work.anchor_walk_iters.max | 305 | 256 | -16.1% | intended |
| pocket-304-open/legion/work.anchor_walk_iters.p99 | 304 | 256 | -15.8% | intended |
| pocket-304-open/legion/work.anchor_walk_iters.total | 2270999 | 2047360 | -9.8% | intended |
| pocket-304-wall/legion/work.anchor_walk_iters.max | 305 | 256 | -16.1% | intended |
| pocket-304-wall/legion/work.anchor_walk_iters.p99 | 304 | 256 | -15.8% | intended |
| pocket-304-wall/legion/work.anchor_walk_iters.total | 2239175 | 2047360 | -8.6% | intended |
| pocket-615-open/legion/work.anchor_walk_iters.max | 616 | 256 | -58.4% | intended |
| pocket-615-open/legion/work.anchor_walk_iters.p99 | 615 | 256 | -58.4% | intended |
| pocket-615-open/legion/work.anchor_walk_iters.total | 3681741 | 2047360 | -44.4% | intended |
| pocket-615-wall/legion/work.anchor_walk_iters.max | 616 | 256 | -58.4% | intended |
| pocket-615-wall/legion/work.anchor_walk_iters.p99 | 615 | 256 | -58.4% | intended |
| pocket-615-wall/legion/work.anchor_walk_iters.total | 3113104 | 2047360 | -34.2% | intended |
| split-450-open/legion/work.anchor_walk_iters.max | 450 | 256 | -43.1% | intended |
| split-450-open/legion/work.anchor_walk_iters.p99 | 450 | 256 | -43.1% | intended |
| split-450-open/legion/work.anchor_walk_iters.total | 1133421 | 1023360 | -9.7% | intended |
| split-450-pocket/legion/work.anchor_walk_iters.max | 287 | 256 | -10.8% | intended |
| split-450-pocket/legion/work.anchor_walk_iters.p99 | 283 | 256 | -9.5% | intended |
| tail-corner380/legion/work.anchor_walk_iters.max | 380 | 256 | -32.6% | intended |
| tail-corner380/legion/work.anchor_walk_iters.p99 | 380 | 256 | -32.6% | intended |
| tail-corner380/legion/work.anchor_walk_iters.total | 3916458 | 3071360 | -21.6% | intended |
| tail-open304/legion/work.anchor_walk_iters.max | 304 | 256 | -15.8% | intended |
| tail-open304/legion/work.anchor_walk_iters.p99 | 304 | 256 | -15.8% | intended |
| tail-open304/legion/work.anchor_walk_iters.total | 2365776 | 2047360 | -13.5% | intended |
| tail-open570/legion/work.anchor_walk_iters.max | 571 | 256 | -55.2% | intended |
| tail-open570/legion/work.anchor_walk_iters.p99 | 570 | 256 | -55.1% | intended |
| tail-open570/legion/work.anchor_walk_iters.total | 6402848 | 3071360 | -52.0% | intended |
| tail-wave/legion/work.anchor_walk_iters.max | 455 | 256 | -43.7% | intended |
| tail-wave/legion/work.anchor_walk_iters.p99 | 454 | 256 | -43.6% | intended |
| tail-wave/legion/work.anchor_walk_iters.total | 4176028 | 2670014 | -36.1% | intended |

## W5 steps 1-4, 6 (2026-10-09, task-w5-steps): measured, NOT an exit -- the gates below still fail

Head: `task-w5-steps` = `task-w5-s0` + step 1 (AR-06 A+B), step 2 (AR-07), step 1 follow-up (no late ring),
step 3 (MV-05), step 6 (AR-10 claims check), docs + protocol note placeholder. Step 4 (MV-06) was built three
ways (S1; full brisk tier; restricted brisk) and reverted under its stop rule (numbers in docs/legion-pathfinding.md
"Reach rings and engaged bodies (W5)"); MV-18 out of scope. 3-offset medians, legion, base -> head (Retail):

| fixture | key | base | head | Retail | plan |
|---|---|---|---|---|---|
| attackring-f1-64 | reach ever | 3 | 39 | 44 | >= 35 met |
| attackring-mob-40 / mob-100 | reach ever | 2 / 3 | 34 / 33 | 40 / 46 | >= 30 met / >= 40 NOT met |
| attackring-struct-60 | reach ever / damage@80s | 6 / 3093 | 42 / 15584 | 60 / 22037 | >= 40 / >= 15k met |
| attackring-fight-60 | reach ever | 10 | 34 | 44 | >= 40 NOT met |
| attackring-guard-64 | within 6 cells ever | 15 | 63 | 64 | >= 50 met |
| fight-retarget | g.F.done | never | 3582 | 2768 | settled by Retail time NOT met (40/40 settle) |
| ringcross | g.M.t90 / done | 3639 / 3796 | 4021 / 4722 | 3864 / 4057 | +10% NOT met |
| chase-1 | registrations / fields built | 53 / 53 | 3 / 21 | | 1 / <= 15: fields NOT met; field_work.max unchanged (first build) |
| chase-50 | registrations | 2552 | 52 | | |
| lateblock-late-16 | arrived / done | 30 / never | 40 / 3422 | 39 / never | >= 39 by 4500 met |
| sweep late-24x20 | arrived / done | 21 / never | 40 / 3827 | 40 | >= 38 by 4800 met |

crowdbench screen (seed 0, all 43 Legion rows): identical to `crowdbench_screen_baseline.jsonl`, hashes included
(every new hashed field is folded only when set, C27; no row sets one). Scenario set (`legion_check check`, all 120
files with data, gate offsets, oden-win, head `5f4d3966`): 957 failures, all in the attack/battle/chase/goalblock
fixtures W5 targets plus `work.group_loop_iters` (MV-05's per-cycle group walk while a block is listed: staticblock
161 -> 221, liftstream 383 -> 443, lateblock-before-16 196 -> 255, deadend-w6-n120-room-squad 348 -> 391) -- none in
any other scenario. Battles: battle-field-2x500 spins 8061 -> 9897, reversals 352 -> 593, A complete_n 52 -> 36;
2x250 reversals 183 -> 241, B arrived 2 -> 0; 2x60 reversals 35 -> 61, work.legion_total.p99 108982 vs W0 107751
(cumulative combat gate FAILS by 1.1%). ctest -L quick: staticidle (engaged guards shuffle round the ring at
cap/8, refresh work), crowdheld (W5 hard gate, unchanged), battle-field-2x60 (work), navigation_determinism
(Legion golden: regenerated at the land) fail. W5/W9 hard gates: none cleared on this head (doorplug-124 only
completed at every offset under the reverted brisk tier).

## W5 step 0 (2026-10-09): instruments, fixtures and the base for AR-06 / AR-07 / MV-05 / MV-06 / MV-07 / MV-18 (no sim change)

Head: `task-w5-s0` = `6333e38d` (W4 land, protocol 242) + four instrument-only commits. Every Legion and Retail state hash is
unchanged: the Windows scenario sweep (all 129 files, both modes, the 11 gate offsets, serial == workers) passes
`legion_check check` on the 91 baselined scenarios with 0 failures (all 455 `hash@` entries equal, so the counters change
no state, here and across platforms), and the crowdbench screen (Windows, 86 rows, both modes) reproduces the W4-land baseline with 0
differences; the five new `legion_*` counters read 0 on every row and join `crowdbench_screen_baseline.jsonl`.
`tools/legion_identity.sh --quick` (base `6333e38d`, candidate this head, Debug -O2, Linux): 130 rows SAME -- L-bench and R-bench replays (final hash and
every checkpoint), both navigation goldens (24 checkpoints), `--mpai` Inner Circle 60 s seed 1 Legion `4d8c06c9747f5a66` / Retail
`56cfcbf8ef57181e` (each reproducible run-to-run), the 12 crowdbench matrix rows, `check-determinism.sh` (`dcef618cd2e4d558`) and the
ctest selection. Its 28 FAIL rows are the 27 tests only the candidate has and `legion_acceptance_crowdheld_legion`, failing in both builds.

**Added (observation only, never hashed).**
- `Stats`: `engagedNow` (cumulative like every counter: member-ticks `tickCombat` braked an in-reach ground attacker or a guard
  within 70 px and Legion routes it; the runner reports it as `gauge.engaged_max` and `engaged.member_ticks`, not as work),
  and `reachSlotsBuilt`, `softReplans`, `reseedsInPlace`, `briskSteps`, which read 0 until W5 steps 1-4 exist.
- `LegionNavigator::claimsAudit()` (the claims invariant): every member's claimed arrival slot against its point's claimed
  cells. `overlaps` (a cell two members claim), `missing` (a claimed footprint cell the point lacks) and `dangling` (a slot on
  a point that is gone) must be 0 (`claims.bad_max` in `.scn` files that say `probe claims`, an eq-0 safety key);
  `orphans` (point cells no slot covers) is report-only, a settled body that left the navigator keeps its cells by design.
- `.scn`: `reach NAME ATTACKERS TARGETS [range=PX] [marks=T,T]` (AR-06: attackers that ever get a shot, when, damage dealt, bodies
  standing out of range 600 ticks, the longest Legion Holding run out of reach and at field potential 0), `creep NAME GROUPS`
  (MV-07's goal-area creep counter), `probe claims`. legion_check: `reach.*` ever/now/damage keys are higher-is-better, ever/damage
  are Retail-floor keys, `reach.*.first` is a time key; `worse`/`better` no longer read a key's -1 ("none") against itself as a loss.
- 38 fixtures written by `tools/scenarios/gen_w5_fixtures.py` (`legion_w5_fixtures` keeps them equal to its output); 29 are baselined
  (`legion_check_*`, nightly), the 9 lateblock size-sweep members live in `tools/scenarios/sweeps/`.

**Base values** (W4 head, 3-offset median 0/+1/-1 unless noted; Legion vs Retail on one binary; "plan" is the PLAN section 3.4 target.
These keys are Retail-floor exception keys, cluster named; every "plan" value FAILS on this base by construction -- the theme
targets are the exits of W5 steps 1-4.)

| fixture | key | Legion | Retail | plan (W5 exit) |
|---|---|---|---|---|
| attackring-f1-64 (64 foot-1 vs mobile) | in reach ever (`reach.ring.ever_end`) | 3 | 44 | >= 35 |
| | out of reach 600 ticks (`out600_end`) / farthest (cells) | 61 / 58 | 20 / 7 | |
| | longest Holding run out of reach (`hold_out_max`) | 2192 | 0 | |
| | damage at 80 s / at 100 s | 157 / 217 | 1738 / 2618 | |
| attackring-f2-64 (foot 2) | ever | 5 | 15 | |
| attackring-squad-64 (Alt+1) | ever | 3 | 44 | identical to the plain row |
| attackring-f1-16 | ever | 1 | 16 | |
| attackring-r200-64 (range 200) | ever | 3 | 64 | |
| attackring-mob-40 | ever | 2 | 40 | >= 30 |
| attackring-mob-100 | ever | 3 | 46 | >= 40 |
| attackring-struct-60 (4x4 structure, 10-damage shots) | ever by 2400 | 6 | 60 | >= 40 (Retail 50 in the audit) |
| | damage at 80 s | 3093 | 22037 | >= 15k (audit 1565) |
| attackring-fight-60 (Fight order) | ever | 10 | 44 | >= 40 |
| attackring-guard-64 (`range=96`: within 6 cells of the guarded ally) | within 6 cells ever | 15 | 64 | >= 50 |
| attackring-wall-64 (closed 8x8 wall ring) | ever (distance only) / damage | 33 / 0 | 61 / 616 | no Holding at potential 0 > 60 ticks |
| | `hold_out_max` / `hold0_max` | 2040 / 0 | 0 / 0 | `hold0_max` <= 60 (0 today: no reach seeds exist yet) |
| attackring-river-64 (6-cell river, range 200) | ever / damage | 6 / 0 | 16 / 0 | |
| ringcross (Move group through the firing ring, tick 1500) | `g.M.t90` / `g.M.done` | 3639 / 3796 | 3864 / 4057 | within +10% of this base |
| | ring ever in reach / `gauge.engaged_max` | 2 / 2 | 44 / - | no worse |
| fight-retarget (40 fighters, target dies mid-approach) | `g.F.arrived` / `g.F.done` | 37 / never | 40 / 2768 | 40 / settled by the Retail time |
| | `claims.bad_max` / `claims.orphans_max` | 0 / 156 | | 0 (safety key) |
| chase-1 (target walks away at equal speed, 900 ticks) | registrations / fields built | 53 / 53 | | 1 / <= 15 |
| | `work.field_work.max` / total | 349360 / 3.51M | | max <= 40% of base |
| chase-50 | registrations / fields built / field_work total | 2552 / 53 / 8.40M | | |
| chase-line (15 idle friendly bodies across the way) | registrations / fields built | 53 / 53 | | |
| goalblock-200 (2x2 block ON the goal at tick 300, 3000 ticks) | `g.A.arrived` / groups / fields built | 131 / 2 / 3 | 183 | arrives like the control |
| goalblock-near-200 (block 6 cells off, the control) | `g.A.arrived` / groups / fields built | 194 / 1 / 2 | 167 | |
| goalblock-fight-200 | `g.A.arrived` / groups / fields built | 130 / 6 / 4 | 87 | |
| lateblock-none / before-16 / late-16 (40 movers, 16x16-body block) | `g.A.done` | 2297 / 3808 / never | 2228 / 3111 / never | late-16: 39/40 by 4500, 0 permanent |
| | `g.A.arrived` | 40 / 40 / 30 | 40 / 40 / 39 | |
| sweep late-4x4 .. 24x20 (`tools/scenarios/sweeps/`) | `g.A.arrived` | 40, 37, 40, 39, 36, 30, 21 | 40, 39, 40, 40, 40, 39, 40 | block sizes 4x4, 6x6, 8x8, 10x10, 12x12, 16x16, 24x20 |
| keelturn-180 / -90 / -0 (ship, no turn-in-place) | `g.S.done` | 641 / 395 / 538 | 934 / 655 / 799 | |
| | `sideways` / `crawl_samples` | 19 / 10, 19 / 2, 0 / 0 | 0 / 5, 0 / 8, 0 / 0 | arc turning: sideways -> 0 |
| staticblock-open / -own / -other | `g.A.done` | 1486 / 2341 / 2341 | 1795 / 1911 / 1911 | <= 2500 (already met on this head) |
| | goal-area creep (`creep.goal.ticks`) | 135 / 1186 / 1186 | 235 / 78 / 78 | own: -70% (<= 356) |

**Findings the W5 steps must absorb.**
- AR-06 reproduces the audit's numbers (3/64 ever in reach vs Retail 44; 61 standing out for 600 ticks, one body 58 cells back, 2192
  ticks of Holding out of reach). `hold0_max` is 0 on every row: the wall-ring gate ("no member Holding at potential 0 for more than
  60 ticks") can only trip once the reach seeds of step 1 put potential 0 at band-0 cells, so today `hold_out_max` is the key that
  shows the hold (the wall row 2040, Retail 0).
- The river row's damage is 0 in both modes (no shot crosses the river in either), so its gate is the in-reach count and the hold only.
- MV-07: `staticblock` is already at 2341 (< 2500) on this head (the audit's 3269 predates W2-W4): the absolute gate becomes "no worse
  than the head base"; the creep counter (1186 vs the open ground's 135) is the live MV-07 key. `landedflyers` layout 0 (foot 2)
  takes 3287 ticks on this head (`legion_world_test landedflyers`; audit 2566, Retail 2163): target <= 2200 stays.
- AR-07: a block ON the shared goal cell halves arrivals by 3000 ticks (131 vs 194 with the block 6 cells off, Retail 183) and
  spawns an extra group and field (groups 2 vs 1, fields built 3 vs 2); the claims audit stays clean (0) through the re-registration.
- The claims invariant holds on every fixture that probes it (fight-retarget, goalblock x3, ringcross, lateblock x3, attackring-fight-60):
  `claims.bad_max` 0. Orphan cells appear (up to 156 in fight-retarget, 480 in goalblock) while arrived bodies stand on claimed slots.
- fight-retarget fails its own acceptance on the base: 3 of 40 fighters never settle by tick 3600 (`g.F.done` never at the median
  offset; Retail settles all 40 by 2768).
- AR-09: `legion_acceptance_crowdheld_legion` still fails as at the W3 step-0 head (`units_ever_terrain_stuck` 1, `physical_in_goal`
  62 of 64); it is W5's.

**The W5/W9 hard gates** (w5-w9-gates.md) all have fixtures and baseline entries; their base on the 11 gate offsets (Legion vs
Retail): doorplug-112 `wall_touch_permille` 11 vs 8, `wall_touch_near_permille` 64 vs 42; tail-corner380 `wall_touch_near_permille` 2 vs 1;
doorplug-124 `g.A.done` never at offsets +1 and -4 (others 1847..2432, Retail 1921); motion-cross `g.u11.done` never at 7 of 11 offsets and
`click.u01.done` never (t90 2702 vs 2887, arrived 23 vs 22); `legion_liftflyers` own-run detour 5 vs open 2 (`kLiftDetourSlack`);
`legion_acceptance_crowdheld_legion` fails as above.

**Can fail.** A gate is shown to fail once before it is trusted:
- Theme targets: every "plan" value above is missed by the base (the table), so a W5 step that does nothing fails its exit.
- `claims.bad_max` (eq 0): fault injection (a debug edit of `takeFormation` that skips the claim of every fifth unit, never committed)
  gives `bad_max` 34 on fight-retarget, 169 on goalblock-200 and 56 on ringcross (overlaps 20/107/18, lost claims 18/62/38).
- Banded keys: a record with `reach.ring.ever_end` 1 (base 3), `damage_end` 100 (217), chase-1 registrations 80 / fields built 70
  (53 / 53) and `claims.bad_max` 3 each fail `legion_check check` against the new baseline (`legion_check` exit 1).
- Hash identity: the existing 455 `hash@` entries (below) are the check that this step moved nothing.

**Gauges the later steps compare with.** `gauge.engaged_max` (most bodies braked in one tick) and `engaged.member_ticks` on the fixtures
that fight (max / member-ticks, 3-offset median): attackring-f1-64 3 / 6462, struct-60 6 / 13244, fight-60 10 / 20072, guard-64 7 / 14216,
fight-retarget 5 / 488, ringcross 2 / 9370 -- the engaged count equals the bodies in reach, as it should. `softReplans`, `reseedsInPlace`,
`briskSteps`, `reachSlotsBuilt` are 0 everywhere.

**MV-06 and opposingcolumns rows** (crowdbench screen baseline `61cb12e6`, turn rate 2500, reproduced row for row by this head):
doors 200x1 Legion `cap8_moving_samples` 5557 of 71662 moving samples (7.8%), `route_crawl_samples` 1253; doors 2000x1 108916 of
594072 (18.3%), 480; doors 500x4 81413 of 741450. opposingcolumns 200x1 Legion crossed 200, `spin_unit_ticks` 177; 500x4 crossed 1830, spin 1268;
2000x1 crossed 1050, spin 1228. The observer's `statue_ticks` / `backward` on motion-open / -wall / -cross are baselined with the scenario set.

## W4 step 0 (2026-10-09): instruments, the staticidle fixture and the base for B1/B2/B3 (no sim change)

Head: `task-w4-s0` = `task-w3-b2` (through the W3 close-out rulings (e)-(i), `0767dfab`) + instrument-only commits.
Every Legion and Retail state hash is unchanged: the Windows scenario sweep (all 91 files, both modes, the 11 gate
offsets, serial == workers) passes `legion_check check` with 0 failed, 0 ratchets after the baseline commit, and
`--mpai` Inner Circle 300 s seed 1 matches the W3 exit (below).

**Added (observation only, never hashed).** `Stats`: `stillPerResidueMax` (most bodies one `id % 30` residue of a
scanStill pass holds; B2 stripes by it) and `quotaPegRunMax` (longest run of ticks whose field quota was spent to
zero), both running maxima reported by the runner as `gauge.*` keys, not per-tick work; `anchorWalkIters`
(serviceYields' map entries + prune's ids; a `work.*` key outside `legion_total`, baselined for every scenario);
`softHashVerifyTicks` and `refreshSuppressed` (0 until B2 / B1; the NDEBUG invariant that the unbuilt-mechanism
counters are zero now covers them). The runner adds `churn.bin<k>`: total Legion work per 1500-tick bin.
`TAK_LPROBE` lines carry `units` and the new counters. `tools/ulasem_counters.py` reduces a live Ulasem run's LPROBE
lines to per-unit-tick values.

**staticidle** (`legion_world_test staticidle`, ctest `legion_staticidle`, registered WILL_FAIL until B1 lands): twelve
guards hold beside an idle friend with a finished field while a cell inside the field's reach is toggled every 10
ticks for 600 ticks. Step-0 head: 60 refreshes, 764640 relaxations, all `field_work_refresh_idle`; B1 must make it
0. (A group needs live members to exist, so "parked" means holding with orders: groups die with their last member.)
Part 2 (passes): a parked group whose route a new wall cuts, ordered on again, re-plans and arrives (field work
133920, 883 ticks).

**W0-base comparison, battle-field / battle-assault** (`legion_cost.py battle --cumulative`, exits 0): total Legion work
per-tick max / p99 / run total as a fraction of the W0 base. 2x60 1.000 / 1.000 / 1.000 (its base was retaken equal);
2x250 0.173 / 0.150 / 0.219; 2x500 0.146 / 0.175 / 0.279; battle-assault 0.174 / 0.354 / 0.376. All at or below
x1.00, so the cumulative combat gate passes at the step-0 head. Field-quota peg runs (`gauge.quota_peg_run_max`):
battle-assault **47** (over 30; the gate is "not longer than base" -- this is the base), 2x500 9, 2x250 8, 2x60 2;
churn scenarios (`refreshchurn*`) 0-21; all in `tools/scenarios/w4-step0-gauges.json` with every scenario's churn bins.

**Live 8-AI Ulasem benchmark** (`TAK_BENCH=4`, Legion, 240 s = 7200 ticks, seeds 0/1/2, client and referee equal;
`tools/scenarios/ulasem-w4-s0.json`, reproduce with `ulasem_counters.py`). Hashes at tick 7200 (seed 0/1/2):
b4cf5e143fecdd5d / ecc7c95e6fcf140d / 989a2274822c0c1a (this seeding, not the --mpai game).

| seed | units max | unit-ticks | field_work / unit-tick | first builds | refresh idle | refresh moving | quota_peg_run_max | anchor_walk_iters / unit-tick |
|---|---|---|---|---|---|---|---|---|
| 0 | 3571 | 18.65M | 139.09 | 98.4% (84.98 slot + 51.62 solo) | 1.33 | 1.17 | 1149 | 0.109 |
| 1 | 3543 | 18.60M | 142.43 | 98.7% | 0.85 | 0.89 | 1479 | 0.110 |
| 2 | 3499 | 18.73M | 142.41 | 98.6% | 1.03 | 0.92 | 2506 | 0.111 |

Churn (field_work per 1500-tick bin, seed 0): 474M, 544M, 549M, 567M (bins 2-4 within 4%).

**Findings the B1/B2 steps must absorb.**
- Refresh work is about 1.7% of the benchmark's relaxations (refresh idle + moving = 1.2-2.5 per unit-tick of 139-142);
  98%+ is first builds, and refreshes almost stop after the first ~1000 ticks (seeds 0 and 2: no refresh work after
  probe tick 1100; seed 1: last at 5700): the field quota is pegged for 1100-2500 consecutive ticks and the first-build
  loops are served before the refresh loops. So B1's acceptance "relaxations per unit-tick <= 55% of base" and "refresh idle <= 30% of
  base" cannot be met by suppressing refreshes in this benchmark: the relaxation total is first builds (C1's target,
  `fieldWorkFirstSolo` -30%). The 55% bound needs a lead decision (re-aim at C1, or measure B1 on staticidle and the
  scenario set).
- `stillPerResidueMax` is 26-27 against a mean of about 11 bodies per residue in the benchmark (2.4x; B2's bound is
  1.1x): `id % 30` is not an even split of the still population. B2 needs a different stripe key (or the bound a
  different reading).
- `anchorWalkIters` is about 0.11 per unit-tick (about 2.0M over the run, the four per-tick map walks); prune's
  backstop cursor is at most 256 a tick.

## W3 exit (2026-10-09): one order, one army; tails settle (protocol 241)

Head: `task-w3-b2` = round 4 + the exit commits below (no sim change after round 4's `05513052`: every
scenario state hash, Legion and Retail, equals round 4's on all offsets -- re-checked on the ruling (f)-(h)
head, all 182 lines; `--mpai` unchanged). Measured on
oden-win (scenario results are platform-independent), optimized Debug, all 91 scenarios, both modes, the gate
offsets (small-count keys on the median of 0,+-1..+-5 or the file's `gateoffsets`, every other key on the core
five), serial == workers on all 182 lines. Base: the W3 step-0 sim `7a01f7bb` with the exit observer (branch
`w3b4-base` = `w3b3-base` + the convoy-keyed command).

Landed in W3 (protocol 241; details in the round sections below): A1 one convoy per click
(`Order::convoyTick`, the hashed open-convoy table); A2 Legion keys on the convoy -- one point, formation and
settle chain per click, slots handed out 32 a tick once the convoy closes (a first part under 64 orders takes
its formation at once); B one settle rule -- a queued, pressed unit settles where it stands within its capped
reach after up to 3 re-choices (none while on its own slot); tick-stamp hold and stall clocks with stride-free
rest; a held formation unit re-aims its lane every 20 held ticks.

Exit commits: `27c04ef2` observer (ruling (1)), `0d2dcd58` legion_liftflyers bound (W3-4), `28c07765`
legion_cost windows (ruling (3)), the baseline commit (rulings (1), (2), (4); W3-3, W3-4), this section and
`docs/legion-exit-w3-moves.md` (the full move table); then the final-exit rulings (f)-(h): `2f28ebc0` observer
(click-level keys), `a858c231` legion_check (click-level floor, ratchet keeps unmasked spreads), the baseline
retake of the click keys and this update.

### Lead rulings W3 final exit (e)-(h), applied

| Item | Applied |
|---|---|
| (e) the arrival disc keyed by convoy | accepted as landed in `27c04ef2` (decision 2); nothing to change |
| (f) one-body groups of a multi-body click judged at click level | the runner names every group of a convoy that joins two or more groups, one of them a single body, with the click of its first group; the observer reports `click.<first>.n / arrived / t50 / t90 / done` over all their bodies (each against its convoy-sized disc) and `g.<name>.click_n`, in both modes. Eight scenarios carry clicks: aware-attack / cross / headon / seen / unseen (`click.a00`, `click.b00`, 24 bodies each), motion-cross (`click.u00` east, `click.u01` west, 24 each), motion-open and motion-wall (`click.u00`, 48). `legion_check` reads a one-body group's own arrived / t50 / t90 / done as report-only (768 key reads in the run): no band, no floor, no retake entry. Baseline: the 48 floor exceptions on those per-body keys dropped (the 13 open failures with them); the click keys' 112 entries retaken from the step-0 sim (branch `w3b5-base` = `w3b4-base` + the click observer, oden-win, the gate offsets, serial), with 47 spread exceptions and 3 floor exceptions for clicks that fail the floor at that base (aware-attack `click.b00.t90` 2166 vs 1954 -- passes at the head, cleared at the next ratchet; aware-cross `click.a00.t90` 1947 vs 1759; aware-unseen `click.a00.t90` 2206 vs 1877; MV-09). At the head: 5 more spread exceptions (MV-09 / MV-12), aware-seen `click.b00.done` 2098 -> 2702 licensed as A2/B shape (incidental; its t90 passes its band) |
| (g) motion-cross g.u11 | at click level the floor passes (`click.u01.t90` 2702 vs Retail 2887, `arrived` 23 vs 22), so no floor exception; `click.u01.done` is never (base 2860, Retail never) -- licensed as a W3-4-class steering gap citing ruling (g); W5 hard exit gate (below) |
| (h) apply_ratchet unmasking spreads | `check()` records every offset spread an exception covered; when the ratchet clears a Retail-floor exception, the spread it masked becomes a spread exception (same cluster, the modes it covered) instead of failing the next check. Unit tests for (f) and (h) in `legion_baseline_reasons` |

### Lead rulings W3 round 4 (1)-(6) and user decisions W3-3 / W3-4, applied

| Item | Applied |
|---|---|
| (1) key commands by convoy, both modes | `legion_scenario` runs the file's clicks through a `ConvoyTable` of its own (never the world's), each as one shared order at its click on its directive tick; the observer keys contact_settled's own command AND a group's packed arrival disc by that convoy (the disc sized for every body of the convoy). 20 scenarios read differently (wall-4x50, corner-4x50 / 8x56 / 1x448, mixed2, the gen1 routes, aware-* and motion-*, whose one-body groups share one click); no state hash moves. 179 entries retaken from the step-0 sim (oden-win), 26 spread exceptions added / 30 dropped, 36 floor exceptions dropped (the per-group discs undercounted arrivals: corner-* and gen1 `g.*.arrived`, wall-4x50 B), 59 added for keys that fail the floor at that base (per-body aware-* / motion-* t90s first read on a disc, MV-09 / MV-12; corner-4x50 D and wall-4x50 C / D t90 never, AR-08). wall-4x50 now: A arrived 49 vs Retail 50, contact_settled 0 (one command) |
| (2) deadend-w6-n120-closed-plain `g.A.arrived` 19 vs Retail 26 | floor exception AR-08, user decision 3 + the dead-end AR-08 decision |
| (3) legion_cost battle-assault march / contactA, battle-field-2x60 windows | retaken with reasons (battle-assault's base was field-budget starved; 2x60 after round 4 fix (2)); the W0-flagged combat entries keep their W0 values. `legion_cost` scaling and all four battle files pass |
| (4) exit licensing redone on the final head | round 3's pinned licensing stripped (1310 licenses, 8 moved bounds restored, 106 first-non-zero entries, 158 W3-exit spread exceptions); every remaining loss re-licensed by cause: 1267 `accepted_regressions` (1113 work-class moves under the W3 B declared tolerance, the rest AR-08 / decision 3, the dead-end decision, A2/B shape as incidental; strait-2x48 `g.B.done` never under ruling (d)), 13 bounds, 186 spread exceptions, 97 entries for counters first non-zero here; ratchet: 3717 references raised, 47 floor exceptions cleared (13 spread exceptions they also carried re-added) |
| (5) crowdheld | pre-existing at the step-0 head; recorded as a W5 hard gate (below) |
| (6), W3-4 | doorplug-112 `wall_touch_permille` 11 vs 8 and `wall_touch_near_permille` 64 vs 42, tail-corner380 `wall_touch_near_permille` 2 vs 1: floor exceptions, cluster W5/W9; doorplug-124 `g.A.done` (never at +1 and -4 of 11): spread exception, W5/W9; legion_liftflyers own-run detour bound open + 3 (`kLiftDetourSlack`, own 5 vs open 2). All W5/W9 hard exit gates |
| W3-3 | corner-1x448 `gate.top.crossings` 4 vs 0 and corner-8x56 3 vs 2: floor exceptions MV-02, until W8 (W8 must bring ALL lane crossings within Retail, W3-2's densehead / strait-2x150 included) |

### Gates on the final head

| Gate | Result |
|---|---|
| `legion_check check` (gate offsets, both modes, serial == workers; oden-win, head `a858c231`) | **PASS** after ruling (i) (oden-win, head `f938ccd6` = `a858c231` + the retaken baseline + the three ruling-(i) exceptions; all 91 scenarios, both modes, serial == workers on all 182 lines): 16676 keys, 0 failed, 1269 licensed, 113 floor exceptions in the run, 768 per-body reads report-only; ratchet applied (3 references), re-run PASS with 0 ratchets. At `a858c231`, before ruling (i): 3 fail (the click-level floor, below); before rulings (f)-(h): 13 fail (per-body keys of the one-body groups) |
| Retail-floor exceptions (non-spread, whole file) | 185 -> 167 at the round-4 licensing (59 first-read keys added at the step-0 base, 6 decision exceptions, 36 dropped by (1), 47 cleared by the ratchet) -> 122 under ruling (f) (48 per-body dropped, 3 click keys added at the base) |
| Legion ctests | Linux after (f)-(h) (optimized Debug, `a858c231` + the retaken baseline, same selection): 213 of 214 -- only crowdheld (W5). Before, Linux (optimized Debug, the exit baseline; legion, navigation_determinism, replay, observer, convoy, issue_selection, movement_orders): 213 of 214 -- only legion_acceptance_crowdheld_legion fails (pre-existing, W5); every `legion_check_*` passes. oden-win (`0d2dcd58`, before the legion_cost retake): 173 of 176 -- battle-assault / battle-field-2x60 (fixed by the retake) and crowdheld |
| `legion_cost` | scaling ok (W(4N)/W(N) 0.39 / 0.72, slot cells 0.44 / 0.61, upkeep 1.00); battle-assault, 2x60, 2x250, 2x500 ok |
| `--mpai` Inner Circle 300 s seed 1 | oden-win: Legion 136218d83cbf7af8, Retail b240750e5765c02b (unchanged); the Mac: Retail b240750e5765c02b |
| crowdbench screen | the Mac (`a858c231`): identical to the exit run's screen -- 1576 differences against the committed W0 screen baseline, the same rows and values (no sim change) |
| Retail | every Retail state hash equal to round 4's; no Retail key moved against the step-0 base |

### Floor exceptions of lead ruling (i): 3 click-level failures, accepted until W7

Ruling (f) retires the 13 per-body failures; read at click level, three clicks that pass the floor at the step-0
base fail it at the head (the same A2 cause the per-body keys showed: one formation for the whole click walks
some bodies to slots farther round than Retail's stop-where-near; crossing-group arrival about 5% slower than
Retail). Lead ruling (i), 2026-10-09: they are Retail-floor exceptions, cluster MV-09, "accepted until W7
(crossing/opposing traffic)"; **W7's exit must bring them within Retail** (x1.1). Core-five median; the eleven
gate offsets for reference (a 24-body t90 is not a small-count key, so the gate reads the core five):

| Key | base Legion | head Legion | Retail | floor (x1.1) | head / Retail on the eleven |
|---|---|---|---|---|---|
| aware-headon `click.a00.t90` | 2066 | 2162 | 1925 | 2117 | 2072 / 1870 (fails, 2057) |
| aware-seen `click.a00.t90` | 1968 | 2065 | 1877 | 2064.7 | 2046 / 1940 (passes) |
| aware-unseen `click.b00.t90` | 2055 | 2207 | 1929 | 2121.9 | 2128 / 1951 (passes) |

The aware-attack `click.b00.t90` exception cleared (it passes the floor at the head) and is removed. With the
three exceptions in `tools/scenarios/baseline.json` the check passes (below); the ratchet was then applied through
`legion_check` (3 references raised: aware-attack `click.b00.done` never -> 2150, aware-headon `click.a00.done`
2563 -> 2297, aware-unseen `click.b00.done` 2889 -> 2397) and the check re-run: PASS, 0 ratchets.

### W5 / W9 hard exit gates carried from W3

| Item | W3 exit | W5 / W9 must |
|---|---|---|
| doorplug-112 wall touch / near (W3-4) | 11 vs Retail 8, 64 vs 42 | within Retail x1.1; exceptions gone |
| tail-corner380 wall touch near (W3-4) | 2 vs 1 | within Retail x1.1 |
| doorplug-124 `g.A.done` (W3-4) | never at 2 of 11 offsets | completes at every gate offset |
| legion_liftflyers own-run detour (W3-4) | 5 vs open 2 (bound open + 3) | open + 1 (`kLiftDetourSlack` back to 1) |
| crowdheld_legion (ruling (5)) | `units_ever_terrain_stuck` 1, one unit 5 cells short at tick 7198 (as at the step-0 head) | the ctest passes |
| motion-cross g.u11 (ruling (g), W3-4 class) | never inside its click's disc at 7 of 11 offsets; `click.u01.done` never (base 2860), licensed | u11 arrives at every gate offset; the license removed |

### Headline keys (Legion, step-0 base -> exit; Retail on the same binary)

| Fixture | Key | Base | W3 exit | Retail | Kind |
|---|---|---|---|---|---|
| pocket-304-open | g.A.arrived | 93 | 284 | 304 | intended (T1 pocket) |
| pocket-615-open | g.A.arrived | 152 | 606 | 297 | intended |
| tail-open570 | g.A.t90 | never | 2793 | 7790 | intended (tails settle) |
| tail-wave | g.A.arrived | 100 | 172 | 288 | intended |
| deadend-w4-n120-closed-plain | g.A.complete_n | 49 | 120 | 120 | intended (settled == n) |
| doorplug-124 | g.A.done | 3268 | 1982 | 1921 | intended (never at 2 of 11, W3-4) |
| wall-4x50 | g.D.arrived | 18 | 34 | 49 | intended (one convoy) |
| corner-8x56 | reversals | 271 | 307 | 237 | incidental, licensed |
| densehead | gate.mid.crossings | 146 | 523 | 208 | W3-2 exception (W8) |
| mixed2 | spins | 30 | 117 | 4855 | incidental, licensed (escort flyers, W6) |

Every key that moved by more than 5% (7068: 2934 intended, 4134 incidental; no Retail key):
[legion-exit-w3-moves.md](legion-exit-w3-moves.md).

## W3 round 4 (2026-10-09): the lead's measurement rulings applied; two fixes -- the exit is still not passed

Head: `task-w3-b2` = round 3 + origin/main 41fa2d7a (merged) + round 4. Measured on `05513052`, optimized
Debug, all 91 scenarios, both modes, on the gate offsets (below), serial == workers on all 182 lines, every
Retail state hash equal to round 3's. Base: the W3 step-0 sim `7a01f7bb` with the round-3 observer (branch
`w3b3-base`), retaken on oden-win on the gate offsets. Scenario results are platform-independent: the
Windows run of the final head equals the Linux arm run hash for hash.

Commits: `aa57431a` legion_check / legion_scenario (rulings (a)-(c), unit tests), `aaf911fb` baseline retake,
`e8b41820` fix (1), `05513052` fix (2) (Legion navigation goldens regenerated; protocol stays 241).

### Rulings applied (lead, W3 round 3 (a)-(c))

| Ruling | Applied |
|---|---|
| (a) small-count keys on 11 offsets | `gate.*.crossings`, `wall_touch(_near)_permille` and the t90 of a group under 10 bodies (its t90 is the last body's time) are gated on the median of the gate offsets 0,+-1..+-5, both modes and the Retail floor; every other key stays on the core five. `legion_scenario --offsets gate` (the default of `--check` / `--baseline`, and the nightly's) runs them; a file whose spawns leave the map names its own (`gateoffsets`: cost-open / cost-corner -4..6, corner-8x56 / corner-1x448 -4..4, nine: their blocks fill the map to 4 cells of two edges). The spread check skips these keys (gated on the median already). 286 entries retaken (102 band / 15 bound values moved), their 109 spread exceptions and 13 five-offset W3-exit licenses dropped; densehead / strait-2x150 (W3-2) keep theirs |
| (b) contact_settled floor only where >= 10 arrive in both modes | `floor_applies`; motion-wall's MV-12 exception dropped (fewer than 10 arrive). The band still applies |
| (c) a spread exception is not a floor exception | `floor_exception_for` (check and retake); unit test fails on the old lookup. It unmasked 8 floor failures that the BASE has too (listed as exceptions: deadend-w4-n120 closed/room plain, pocket-304 open/wall and tail-wave `g.A.arrived`, tail-wave `g.W.t90`, doorplug-124 `g.A.t90`, deadend-w4-n40-closed-squad `contact_own`) and 33 base floor failures of small-count keys (deadend wall touch, aware-cross `g.b04.t90`, motion-cross `g.u05.t90`, mazeapproach, doorplug-124 wall touch) |

### Fix

| Fix | Numbers |
|---|---|
| (1) a body standing on its own slot never re-chooses one (`e8b41820`) | legion_liftflyers' open run: the 9-cell detour was a body AT its slot (166,51) re-choosing (159,43); now detour 2, ticks 1706 -> 1471. Suite (legion, gate offsets, vs round 3, W3 licenses stripped): 9 band failures fixed, 6 new (noise-level counts), no floor change; navigation goldens unchanged; `--mpai` unchanged |
| (2) a click under one uplink tick takes its formation at once (`05513052`) | A2 held every point's formation until its convoy closed (9-plus ticks), the members walking by the shared field meanwhile; a first part under 64 orders now takes it at once, provisionally, rebuilt over every part if another part joins before the close (wall-4x50 / corner-4x50 / corner-8x56 keep A2's one formation). legion_landedflyers passes again (the stand against the flyers bisected to A2, 13c9265d). Suite vs (1), W3 licenses stripped: 25 fixed, 11 new, no floor change; doorplug-124 `g.B.t90` completes at all 11 offsets (was never at 4), `g.A.done` at 9 (never at +1 and -4; was 8); aware-cross a09 at 10 of 11 (was 8); corner-8x56 top crossings 5 -> 3; losses: deadend-w6-n40-room wall touch x1.10-1.16, corner-8x56 `g.F.complete_n` 35 -> 23, battle-field-2x60's windows over the round-3 retake (below). All 24 Legion navigation checkpoints moved (serial == workers), Retail's unchanged |

### The real failures (ruling (d) and the list), measured

| Item | Finding |
|---|---|
| doorplug-124 `g.A.done` never | At the round-3 sim: all 60 A orders complete at all 11 offsets; at 3 of 11 one or two tail bodies settle 16.6-19 cells from the click (disc 16.4) in the door-exit jam, queued behind their own settled crowd and pressed by their own tail (trace: id 1 re-chose a free slot (125,57) at 1577, held, settled at 1622 behind). Retail itself never completes A at 5 of 11. Not fixed: 14 env-gated variants (settle deferral while a free slot is reachable, by area / disc / relaxed BFS through the own moving tail, bounded / unbounded; extended re-choice; "pressed" ignoring own moving bodies) each move the never to other offsets or cost elsewhere (best: relaxed BFS, 90% disc, unbounded -- doorplug A and strait-2x48 B complete at the core five, but doorplug B t90 never at 6 of 11 and cost-corner `complete_n` 88 -> 72). The jam needs steering round the settled crowd (W5 / W9), not a settle tweak. After fix (2): never at 2 of 11 (+1, -4), the core-five median 1982 passes |
| aware-cross `g.a09` never | After fix (2) a09 arrives at 10 of 11 offsets (never at +5). Observer geometry: each of the 24 bodies is its own observer group (n = 1, disc 33 px) at ONE shared point, so `g.aNN.t90` reads "this body ended within 2 cells of the click". a09's order completes at all 11 offsets (complete_dist 1-9 cells); 11-offset median 1651 vs base 1687, Retail never (a09 arrives at 4 of 11 in Retail). Passes under ruling (a) |
| liftflyers | Fixed: the open-run detour (1) and, with (2), the own run's time (1799 -> 1531 ticks, under 1.1 x open 1471; A2's deferred formation, bisected: 358fc350 own 1531, 13c9265d 1711). Still fails "own flyers that lift" on its detour: 5 vs open 2 + 1 -- the last body re-chooses along the formation's west face AT the destination (151.5, z 47 -> 49), not round a flyer. Re-choice by walk (round 3's variant) on top of (1)+(2) passes the test, but the suite then loses strait-3x48 `g.B.t90` (never) and mixed2 hover x1.8 (14 fixed, 17 new): not landed |
| landedflyers | Fixed by (2). Bisected: the stand-against-flyers rise arrived with A2 (13c9265d), not the lane re-aim. foot 2 stuck median 1651 (bound 1411; base per shift 1039..1548, head 792..2160); foot 3 230 vs 224 is one sample (shifts 143, 66, 290, 230, 1100; base 143, 81, 290, 582, 159). Lane re-aim only for engaging orders (round 3's variant) brings foot 2 to 1292; foot 3 unchanged; suite: 7 fixed, 7 new, wall-1x48 strip crossings newly fail the floor (7 vs 4). Not landed |
| battle-assault windows | Cause isolated: identical triggers (Attack legs re-seed every 16 ticks: 38 base / 42 head registrations in the march), but the base starved. Its 57 Fight fields (3.03M cells) left the 48-whole-map cell budget no room (3.069M of 3.07M cells live from tick 260; 4-8 groups waiting; 234 evictions, 376 deferred refreshes in the run), so its chase fields were deferred or never built; A2's 40 Fight fields leave room (2.37M cells, none waiting, 0 evictions) and every chase gets its field on time. march fields 33 vs 18, contactA 1803 vs 1598 bound; whole run 0.69x base (261M vs 378M), per-tick max 480635 vs 500694, p99 403369 vs 417513. No sim change made: the windows' W0/base counts are budget-starved numbers |
| mixed2 spins | Escort-flyer turning is noisy: base per offset 1..205 (11-offset median 62, core-five 30 = the bound), head 34..224 (75, core 64); head is shifted up on 8 of 11 offsets: real, ~1.2x on 11 offsets. Flyer pacing, W6 |
| corner-4x50 reversals | 11-offset median 119 -> 142 (x1.19, inside the 1.20 band); the core-five base 93 was a low draw. Not a W3 regression on the wider read |
| wall-4x50 / motion-cross contact | wall-4x50: the 4 clicks at ticks 1-4 to one point join ONE convoy (A1: within kGapTicks 9 of each other, the server cannot tell them from one click), so later groups' slots lie past A's settled bodies; per arrived unit the raw contacts are below Retail (head ~3200, base ~1550, Retail ~3900 samples) but arrivals are fewer (per-group discs: head ~60, Retail ~96). motion-cross: 1-3 arrive (ruling (b): no floor); raw contacts ~320 vs base ~230, Retail ~270 -- the band fails on the denominator |
| strait-2x48 `g.B.done` never | Recorded (Retail never too) |
| crowdheld_legion | `units_ever_terrain_stuck` 1 (as at the step-0 head); `physical_in_goal` 62 passes. Not fixed |

### Gates on the round-4 head (`05513052`)

| Gate | Result |
|---|---|
| `legion_check check` (gate offsets, serial == workers) | 16573 keys, 251 failed: 173 work counters and 19 offset spreads that the round-3 exit licenses pinned at round 3's exact values, 51 band / bound keys, 8 Retail floor. At the round-3 sim the same baseline fails 39 (non-work); with the W3 licenses stripped the round-3 sim fails 199 non-work keys, this head 201 (28 fixed, 30 new) |
| Retail floor | 8 fail: corner-1x448 top crossings 4 vs 0 and corner-8x56 3 vs 2 (gate offsets; A2 reorders the lanes, as W3-2's densehead / strait); deadend-w6-n120-closed-plain `g.A.arrived` 19 vs 26 (base 32: the queue settles where it stands, outside the disc -- the AR-08 dead-end decision's other face); doorplug-112 wall touch 11 vs 8 / 64 vs 42 (base 5 / 26); tail-corner380 wall touch near 2 vs 1 (base 0); wall-4x50 `g.A.arrived` 14 vs 36 and contact_settled 50702 vs 40052 (the merged convoy, above) |
| Legion ctests (oden-win) | 172 of 176: legion_liftflyers (own-run detour, above), legion_scenario_battle-assault (as round 3), legion_scenario_battle-field-2x60 (new, below), legion_acceptance_crowdheld_legion (as round 3: one unit 5 cells short of its sealed goal is still at tick 7198, units_ever_terrain_stuck 1); legion_landedflyers passes |
| Linux `legion_check_*` ctests (gate offsets) | 20 of 38 pass; 18 fail (deadend room / w6 / w4-n40-room, doorplug-112/124, pocket-304-wall / 615-wall, tail-*, unreach-200), mostly the round-3 licenses above |
| `legion_cost` | scaling passes; 2x250 / 2x500 pass; battle-assault fails its march / contactA window counters (above); battle-field-2x60 (60-body groups, fix (2)) is over the round-3 retake of its windows (contact field work 13.42M vs 12.04M x1.10, run p99 107751 vs 96196 x1.10) but within the step-0 base (contact 1.015x, p99 107751 vs 113237, max 393639 vs 399196) |
| crowdbench screen (oden-win after (1), the Mac after (2): identical rows) | every Retail row equal to round 3's; 4 Legion rows moved by fix (1) (mixedfootprints, sharedgoal), none by (2); jagged 200x1 185, opposingcolumns 194, doors 2000 247 as round 3 (within 3%) |
| `--mpai` Inner Circle 300 s seed 1 (oden-win) | Legion 136218d83cbf7af8, Retail b240750e5765c02b: unchanged |
| Retail | every scenario's Retail state hash equal to round 3's on all offsets |

### Experiments (env-flagged, branch `w3r4-x`, never landed)

| Variant (TAK_W3X) | Effect (legion, gate offsets, vs the head; W3 licenses stripped) |
|---|---|
| 64 walk-nearest lower re-choice (round 3's) | liftflyers open detour 3 but own run 1591 > 1.1 x 1396; corner-8x56 crossings floor passes; 19 fixed / 42 new (reversals, complete_n, strait-3x48 B t90 never) |
| 128 potential + walk re-choice | as 64; 22 fixed / 42 new |
| 1 lane re-aim only for engaging orders | landedflyers foot 2 1292; 7 fixed / 7 new; wall-1x48 strip crossings floor |
| 256 (+bound) no out-of-area settle while a free slot is reachable | doorplug all 11 with a 10-window bound, but gap6 / gapsweep t90 3602 (Retail floor 3391) and sbend 3962 (3908) |
| 256 + relaxed BFS + slack / disc gate | doorplug A completes at the core five (1 never in 11), strait-2x48 B completes, tail-corner380 floor passes; cost-corner arrived / complete_n and gen1 complete_n drop; doorplug B t90 never moves |
| 32768 formation at once for every click | landedflyers passes; floors: fixes deadend-w6-n120-closed-plain, tail-corner380 and wall-4x50, breaks corner-4x50 crossings / wall touch, deadend-w6-n120-room-plain arrived, liftstream crossings (105 new / 76 fixed) |
| 65536 at once under 64, no rebuild | corner-8x56 crossings 14, corner-4x50 crossings / wall touch floors: multi-click convoys need the rebuild that (2) does |

## W3 round 3 (2026-10-09): instrument decisions, two fixes, licensing -- the exit is still not passed

Head: `task-w3-b2` = round 2 + origin/main e8f87161 (merged) + round 3. Measured on `e0050a7f` / `0e95e856`
(identical scenario hashes), optimized Debug, all 91 scenarios, both modes, offsets 0,1,-1,2,-2, serial ==
workers on all 182 lines, every Retail state hash equal to the step-0 base. Base: the W3 step-0 sim `7a01f7bb`
with the round-3 observer (branch `w3b3-base`, run on oden-win: scenario results are platform-independent).

Commits: `1c55e022` observer (W3-1), `5dd205b6` W3-1 / observer-fix retake, `1c0d6deb` W3-2 exceptions,
`108458e7` slot hand-out ring-cell budget, `0e95e856` protocol note, `17c22b58` exit licensing,
`3c60ea5c` legion_cost retake, `e0050a7f` patrol waypoint queue.

### Decisions applied

| Decision | Applied |
|---|---|
| W3-1 (user): contact_settled per arrived unit, own command excluded | `contact_settled_permille` = member samples touching a body without orders of another command (a command is one selection; the runner notes each non-queued directive), x1000 / members arrived at the end (at least 1). All entries, references and exceptions of the key retaken for both modes on the base; the old measure's 104 exceptions and its two W2 licenses dropped |
| Observer fix 6cee9294 (lead): completed-not-retired leg is held | parked/waiting held classes retaken on the base (58 entries) |
| W3-2 (user): densehead gate.mid / strait-2x150 gate.strait crossings | floor exceptions, cluster MV-02, "accepted until W8" |
| Dead-end AR-08 distances (lead) | licensed with the lead's reason (settled == n) |

The six former contact_settled floor failures on the new measure (exit vs Retail): split-450-open 0 vs 0,
pocket-615-wall 0 vs 0, battle-field 2x250 / 2x500 0 vs 0, corner-4x50 24284 vs 32562 -- pass;
motion-cross 172500 vs 93667 -- fails (base 77667). New on the new measure: wall-4x50 55766 vs 40052 (base
22217: later groups of the 4-click wave walk past the earlier groups' settled bodies). Both are masked in
`legion_check` by offset-spread exceptions of the key (the floor check counts any exception); the motion-*
scenarios have 1-3 arrived units in both modes (every unit its own group, a 34 px disc), so the per-arrived
measure is noise there.

### Fixes

| Fix | Numbers |
|---|---|
| (1) slot hand-out also stops at 131072 ring cells a tick | dead-end n120 room work max x1.11-1.21 -> x0.50-0.70 of base; every hash and other key unchanged |
| (2) patrol waypoint: a held member of the same destination nearer the waypoint is the queue | `legion_movement_orders` group patrol least laps 1 -> 7 (B had all 12 holding round the waypoint for good; bisected to 358fc350, base passes); no scenario hash moves |

### Gates on the round-3 head

| Gate | Result |
|---|---|
| `legion_check check` (5 offsets, serial == workers) | 16573 keys, **12 failed**, 1323 licensed, 3727 ratchets (not applied), 307 floor exceptions |
| Retail floor | **3 fail**: aware-cross g.a09.t90 1767 vs 1601 (one body of a 24-body click: base [1687 .. 1723], exit [never, 1636, 1767, never, 1651]); corner-8x56 gate.top.crossings 3 vs 1 (base per offset 0..8, Retail 0..6); tail-corner380 wall_touch_near_permille 2 vs 1 (8 touching samples at the map edge of a corner goal vs Retail's 28; Legion's denominator is 3804 near-wall samples, Retail's 28181) |
| Band, not licensed | doorplug-124 g.A.done never at 3/5 offsets (one A body settles 1 cell outside the disc; base 3268); mixed2 spins 64 (bound 30; base per offset 1..144: the airborne escort flyers turn in place); corner-4x50 reversals 142 (93 x1.20); strait-2x48 g.B.done never at 4/5 (Retail never too); contact_settled band losses (motion-cross, motion-open, wall-4x50; corner-8x56 spread) |
| `legion_cost` scaling | passes (retaken, reasons in the file); steady W(4N)/W(N) 0.39 / 0.72, slot cells per slot 0.44 / 0.61, upkeep 1.00 |
| `legion_cost` battle windows | 2x60 (retaken), 2x250, 2x500 pass; **battle-assault fails**: march fields_built 18 -> 33, field_work 702416 -> 1289808, contactA fields_built -> 1803 (W0 1453 x1.10); whole-run per-tick max 480635 (base 500694), p99 403369 (base 417513) |
| crowdbench screen (oden-win) | all Retail hashes identical; jagged 200x1 190 -> 185 (-2.6%), opposingcolumns 200x1 199 -> 194 (-2.5%), doors 2000 247 -> 247; sharedgoal 500x4 units_spinning 0 -> 2 |
| Legion ctests (oden-win, e0050a7f; legion, navigation_determinism, replay, observer, convoy, issue_selection, movement_orders) | 172 of 176 pass; fail: legion_landedflyers, legion_liftflyers, legion_scenario_battle-assault (cost windows), legion_acceptance_crowdheld_legion (fails as at the step-0 head); legion_movement_orders fixed by (2) |
| `--mpai` Inner Circle 300 s seed 1 | Legion 136218d83cbf7af8, Retail b240750e5765c02b on oden-win (e0050a7f) and on the Mac (3c60ea5c): unchanged under 241 |
| landedflyers | layout 0 stuck median foot 2 1651 (bound 1411), foot 3 230 (bound 224); base 1107 / 159 |
| liftflyers | open run detour 9 (a destination re-choice walks a body 8 cells across the front: `rechoose` takes the lowest-potential free cell) vs enemy 5 |
| navigation goldens | unchanged (both modes, serial == workers) |

### Experiments (env-flagged probes, never committed; 5 offsets, full suite on oden-win / the Mac)

| Variant | Effect |
|---|---|
| lane re-aim only for engaging orders (Fight/Patrol) | landedflyers foot 2 1651 -> 1292 (foot 3 unchanged 230); corner-1x448 gate.top.crossings 4 -> 9 (floor); 65 new / 34 fixed band failures |
| lane re-aim every 40 / 60 held ticks | landedflyers foot 2 1292; battle-assault fields unchanged |
| pressed ignores passing traffic of another destination | aware-cross unchanged (a09 settles behind its own crowd, C29 "behind") |
| re-choice takes the BFS-nearest lower cell (or least potential + walk) | liftflyers open detour 9 -> 3, doorplug-124 A done never -> 2001, 164 band failures fixed / 94 new; but corner-1x448 top crossings 9-10 and wall-4x50 contact_own 718 vs 652 newly fail the floor, liftflyers own-vs-open ticks fail |
| slot hand-out 16 a tick (round 2) / ring-cell budget | budget landed as (1) |

The crossing and t90 floor keys above flip with any perturbation (corner-1x448 top crossings per offset: Retail
0..10, base 0..10, variants 0..19). Deadend w2 n120 (2-cell mouth) still jams: complete_n 0-15 of 120 by tick
8000 (open_still900 83-104), w2 n40 complete 24-40 of 40 -- recorded as a W5 item (units that never arrive).

## W3 B round 2 (2026-10-09): B fixed with A2 on top; the Retail floor still fails on A2's arrivals

Head: `task-w3-b2` = the exit attempt plus round 2: (1) a held formation member re-aims its lane
every 20 held ticks, (2) A2 re-landed on the settle rule (convoy-keyed point, 32 slots a tick),
(3) C29 measured along the queue (one row behind a settled arrival of the same command), an
observer fix (a completed, unretired leg is a stand, not parked without progress), (4) the slot
re-choice never searches more than a row uphill, (5) a distinct goal's member still for a window
within body + 4 px of its point has arrived; Legion golden regenerated, protocol 241 note updated.
Measured against the W3 step-0 base (`baseline.json`), optimized Debug, all 91 scenarios, both
modes, offsets 0,1,-1,2,-2, serial == workers on all 182 lines, Retail state hashes equal to the
exit attempt's on every scenario. Not an exit: `baseline.json` is not ratcheted.

`legion_check check`: 16460 keys, 1666 failed, 3770 ratchets; 169 floor exceptions now pass the
floor (132 with B and the lane re-aim alone, before A2). Diagnosis: `scratchpad/shared/w3-b2-diagnosis.md`.

### The exit attempt's failures, now

| Exit-attempt failure | Round 2 |
|---|---|
| Retail floor: doorplug-124 B arrived 47 (Retail 52) | 57 (base 59); B t90 never -> 6962 (base 7196); A done never (one A body settles 1 cell outside the disc; base 3268) |
| tail-wave W arrived 136 (150) | 150 |
| corner-1x448 top crossings 6 (4) | 4 |
| motion-cross / split-450-open contact_settled 28 / 7 (23 / 6) | 32 / 22: still failing, now from A2 (below) |
| parked_no_progress aware-unseen / island-1x48 / strait-2x48 1 / 1 / 2 (0) | 0 / 0 / 0 (observer: the completing tick of a long-still body) |
| battle-field-2x500 total x1.15, 2x250 p99 x1.12 | x0.895 / x0.749 (lane re-aim: the battle ended later without it; A lost 454 instead of 376) |
| cost-open / motion-open / smoke-open p99 x1.17-1.20 | x0.95 / x1.06 / x0.83 (bounded re-choice search) |
| legion_cost battle mop-up 2x500 field work 4.36M -> 48.4M | 0 (Legion total 268k in the window); battle-assault march / contactA fields built over 1.10x |
| doorplug B t90 never, A done never | B 6962; A never (above) |
| crowdbench jagged / opposingcolumns 200x1 -5.3% / -3.0% | 185 / 194 (-2.6% / -2.5%, within 3%); every Retail row identical |
| deadend w4 n120 and w4 n40 closed short | every w4 and w6 case settled == n (w4 n120 closed 49 -> 120 by tick 5500); w2 n40 25 -> 33-40; w2 n120 still jams at the 2-cell mouth (4 / 0 / 2 / 0) |
| pocket t90 never, midroute 46 -> 72 | 615-open arrived 152 -> 606, t90 4144, midroute 0; 615-wall arrived 453 (>= 400) |
| mixed2 spins 30 -> 140 | 64 (bound 30 x1.20: still over) |

### New Retail-floor failures (A2's arrivals)

| Key | Base / now / Retail | Cause |
|---|---|---|
| contact_settled_permille: split-450-open, pocket-615-wall, corner-4x50, motion-cross, battle-field 2x250 / 2x500 | 6 / 22 / 6; 7 / 28 / 12; 31 / 45 / 34; 22 / 32 / 23; 0 / 1-6 / 0 | ordered bodies touching bodies without orders: with one point per click 3x more bodies arrive and settle (split arrived 104 -> 342, Retail 55), and the tail walks past them |
| densehead gate.mid.crossings | 157 / 643 / 204 | one formation per 200-body click reorders the lanes in the head-on crossing (per-part groups only halve it) |
| strait-2x150 gate.strait.crossings | 0 / 20 / 11 | same, 150 boats |
| corner-8x56 gate.top.crossings | 1 / 3 / 1 | noise-level key (per-offset 0..25 in both modes) |
| aware-cross g.a09.t90 | - / 1767 / 1601 (floor 1761) | |
| tail-corner380 wall_touch_near_permille | 0 / 2 / 1 | |

Other open items: deadend room work max x1.11-1.21 (A2's slot hand-out, 32 a tick); corner-4x50
reversals 93 -> 136-150 (band 1.20); `pivot_part_ids` (A2's work class) has no baseline entry;
`legion_landedflyers` now fails (layout 0 stuck median 1651 vs 1411, foot 3 230 vs 224) and
`legion_liftflyers` still fails; `legion_cost`'s held_rechecks / detours were over at the step-0
head already. `--mpai` not re-run on this head.

## W3 exit attempt (2026-10-09): the gates are not met

Head: `task-w3-exit` = W3 steps 0a/0b (T1-T tick stamps, holder rule in one unit, fixtures),
step 1 (A1 convoy table), step 2 (A2 parked), step 3 (B one settle rule) and protocol 241.
Measured against the W3 step-0 base (`baseline.json` values, retaken at step 0b; A1 reproduced
it with 0 failures, so every move below is B's). Optimized Debug, all 91 scenarios, both
modes, offsets 0,1,-1,2,-2, serial == workers on all 182 lines; Retail state hashes equal on
every scenario. **The exit is not taken**: `baseline.json` is not ratcheted and carries no new
`accepted_regressions`, because some failures cannot be licensed (below).

`legion_check check --require-all`: 16460 keys, **1192 failed** (1161 distinct keys), 1996
ratchets, 348 floor exceptions. By kind: 975 band losses (849 work counters, 126 outcome
keys), 150 new offset-spread exceptions, 29 unbaselined work counters (zero at the base:
`rechoice_bfs_cells`, `held_rechecks`, `outside_area_completions`, completion-distance bins), 30 outcome bounds, and **8 Retail-floor failures**.
3649 keys moved by more than 5% (every scenario; `legion_check exit-table` against the
step-0 base regenerates the full list).

### Gates that fail

| Gate (PLAN) | Result |
|---|---|
| Retail floor: a passing key may not start failing (3.0, not licensable) | 8 keys: doorplug-124 `g.B.arrived` 47 vs Retail 52; tail-wave `g.W.arrived` 136 vs 150; corner-1x448 `gate.top.crossings` 6 vs 4; motion-cross `contact_settled_permille` 28 vs 23; split-450-open `contact_settled_permille` 7 vs 6; `parked_no_progress` on aware-unseen 1, island-1x48 1, strait-2x48 2 (Retail 0) |
| B declared tolerance: total Legion work within 1.10x on every scenario | battle-field-2x500 total 266.0M -> 306.9M (+15%: field work +32%, first-solo +24%, first-slot x2.4); battle-field-2x250 p99 251230 -> 282389; cost-open p99 5063 -> 5945; motion-open p99 1125 -> 1347; smoke-open p99 1611 -> 1893 |
| 3.1 gate: doorplug, a later order still passes | doorplug-124: B arrived 59 -> 47, B t90 7196 -> never, A done 3268 -> never, still900_ever 0 -> 1 (`side.past.high` 120 unchanged) |
| 3.1 gate: crowdbench jagged/opposingcolumns within 3% | jagged 200x1 orders complete 190 -> 180 (-5.3%); opposingcolumns 200x1 199 -> 193 (-3.0%); doors 2000 unchanged (the screen has no bridges 2000 row) |
| `legion_cost` counter baseline | fails; scaling (C19) passes. Battle mopup windows: 2x500 field work 4.36M -> 48.4M, fields built 73 -> 373, Legion total 35.9M -> 52.5M; 2x60 slot search 23654 -> 282546. cost-corner/cost-open held_rechecks and detours were already over at the step-0 head (the stamps) |
| B acceptance: deadend settled == n | not met (scn, orders complete at 8000, base -> head): w2 n120 4/0/8/0 -> 4/0/8/0; w4 n120 49/44/79/71 -> 50/45/79/74; w4 n40 closed 40/39 -> 37/32 (worse; `open_still900` 0/1 -> 3/8); every w6 case and the other n40 cases complete |
| A / MV-01 acceptance: pocket open >= 600 arrived, t90 <= 5800 / 4500 | not met: needs A2 (parked). Orders complete 615-open 479 -> 608, 615-wall 155 -> 182, 304-open 242 -> 297, 304-wall 150 -> 170, but arrived 152 -> 154, 122 -> 113, 93 -> 90, 78 -> 74 and t90 never (Retail 297 / 114 / 304 / 263) |
| B: `midroute_completions == 0` in pocket | not met: 615-open 46 -> 72, 615-wall 10 -> 17, 304-open 10 -> 13, 304-wall 6 -> 8 (maze, mazeform, doorplug, crowdbench groupdetour stay 0) |
| AR-08 no worse (deadend) | deadend-w6 `complete_dist_max` 86 -> 94 (n120 closed plain), 28 -> 37 (n40 closed squad), 25 -> 30; pocket max 79 -> 104 (304-open), 149 -> 163, 58 -> 72, 60 -> 76 |
| Other losses named in the B commit, still present | mixed2 spins 30 -> 140 (bound 30); battle-field-2x60 `g.A.arrived` 10 -> 7; 2x250 `g.B.arrived` 2 -> 0; `legion_liftflyers` fails ("group did not go round the enemy flyers"). `legion_acceptance_crowdheld_legion` fails as at the step-0 head |

Report-only for W3 (gated from the W4 exit): `--cumulative` puts battle-field 2x60/2x250/2x500
and battle-assault total Legion work above the W0 base (2x500 total 271.6M W0 -> 306.9M;
battle-assault max 484465 -> 487871).

### What passes

| Check | Result |
|---|---|
| T (tick stamps) | squadformation p90 2746 (<= 3600; B's <= 3500 too), max 5086, 120/120; settlelatency strides 1/2/4 byte-identical, serial == repeat == workers |
| A1 convoy checks (`convoy_test`, `issue_selection_test`) | pass: flyers first and saturated, opposite corners, all-air 100, 974 units at round trips 0/8/16 one convoy and 24 split, patrol with return legs, two clicks 48 px apart, index == scan on 1e4 streams; `convoy.tests_max` <= 15 per order everywhere |
| B: tail `open_still900 == 0` | 0 on open304 / open570 / corner380 / wave (`still900_ever` 0 / 2 -> 1 / 0 / 0) |
| B: sharedgoal 2000 pending@6000 <= 120 | 230 -> 80 (orders complete 1770 -> 1920) |
| AR-08 sharedgoal no worse | 2000x1 outside 765 -> 788, median 52.4 -> 49.0, max 113.0 -> 105.8 (b8a4110 bar 110); 500x4 outside 518 -> 250, max 53.6 -> 45.3; 200x1 outside 7 -> 5, max 29.0 -> 26.5 |
| crowdbench screen | all 43 Retail rows identical; 35 of 43 Legion rows moved; orders complete, arrived settled and crossings unchanged on doors 2000, bridges 200, crowdtrap, groupdetour 200 and maze 500/2000 |
| C19 scaling (`legion_cost`) | steady W(4N)/W(N) 0.38 corner, 0.77 open (<= 1.5); slot cells per slot 400/100 0.47 / 0.26 (<= 2.2); upkeep 1.00 |
| Determinism | navigation goldens (Legion regenerated at 75883677, Retail unchanged), serial == workers on every scenario; `--mpai` Inner Circle 300 s seed 1 unchanged under 241: Legion 136218d83cbf7af8 (twice), Retail b240750e5765c02b |

### AR-04 probe after W3

The audit's probes (`auditpocket`, `auditfactory`, and `ar04lone`, a lone unit ordered into
the centre of its own idle crowd), never committed, run on W2's head 7c490b2f and on this head:

| Case | W2 head | This head |
|---|---|---|
| member packed inside its idle crowd, 7x7 / 5x9 / 3x5, stride 2, with or without flyers | never leaves (Holding) | unchanged: never leaves (Retail drops the order at 167-182) |
| same, loose (stride 3) / edge member of a 7x1 row | done 556 / 377 | unchanged |
| lone unit into its idle crowd, stride 2, 4x4 .. 7x7 | done 631-721 | unchanged |
| lone unit into a 9x9 crowd (stride 2) / a 7x7 crowd at stride 3 | never / never | **1081 / 1081** |
| factory exit lane, rally 6 / 10 cells below, every 150 ticks: orders held in the lane | 13 / 12 of 60 | 12 / 12 |
| same, rally on the factory / every 40 ticks / diagonal (+10,+8) / 14 cells below | 14 / 13 / 6 / 1 | 13 / 13 / **12** / **3** |

The idle-crowd pocket is still open on this head (the packed member and the exit lane).

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
