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
