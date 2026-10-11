# Legion scenario keys: audit definitions

The `.scn` fixtures in `tools/scenarios/` measure Legion with the shared observer
(`tools/legion_observe.h`). Several audit numbers came from throwaway probes with
their own definitions (the parked lanes2 `laneRun` and the `aware` world test).
This page says which `.scn` key reproduces which audit number, and which keys are
report-only because their definition differs. Grammar: `tools/scenarios/README.scn-format.txt`.

All values below are one run at start offset 0 in Legion mode on optimized Debug,
the same single run the audit quoted. The committed baseline gates the median of
five offsets (0, +1, -1, +2, -2), which can differ a lot from offset 0 on keys with a
large offset spread.

## Keys that reproduce an audit number

| Fixture | Key | Definition | Audit | `.scn` | Verdict |
|---|---|---|---|---|---|
| island-1x48 | `lane.cross.swaps` / `.pairs` | first entry into x=70 and x=130 (lateral z), pairs entering x=70 within 300 ticks of each other, swapped by >= 2 cells at both lines | 33 / 978 | 33 / 978 | exact |
| island-1x48 | `gate.strip.files_x100` | distinct z/2 files among members in x 95-105, mean over 10-tick samples with >= 3 inside | 3.02 | 3.01 | exact within rounding |
| wall-1x48 | `lane.cross.swaps` / `.pairs` | first entry into z=60 west of the wall (lateral 98-x) and east of it (lateral x-102), 300 ticks, >= 2 cells | 217 / 1123 | 208 / 1123 | -4%, comparable |
| wall-1x48 | `gate.strip.files_x100` | the same files measure over x 97-102, z < 48 | 4.48 | 4.47 | exact within rounding |
| wall-4x50 | `lane.cross.swaps` / `.pairs` | as wall-1x48 with `across=all`: pairs of members of different groups count (the probe ran all 200 bodies as one set) | 1223 / 6821 | 1308 / 7010 | +7%, comparable |
| wall-4x50 | `gate.strip.files_x100` | as wall-1x48 | 4.97 | 4.96 | exact within rounding |
| aware-headon | `pair.contacts.permille_x100` | live ordered a x b pairs sampled every 10 ticks whose centre cells are within 2 on both axes, per mille x 100 | 3.15 | 3.13 | -0.6% |
| aware-unseen | `pair.contacts.permille_x100` | as above | 7.45 | 7.54 | +1.2% |
| refreshchurn | `work.refresh_completed.total` | field refreshes installed in 900 ticks under a 2x2 feature placed in a fresh cell every tick | 11 on main (1 before b565029) | 10 | comparable; the control without churn gives 0 |

The three remaining aware fixtures (cross, seen, attack) have no audit number for
contacts; their `pair.contacts.permille_x100` (12.5, 2.8, 6.7 per mille at offset 0)
are new keys, not comparisons.

Why the wall crossings are not identical: the probe sampled the cell before and after
each tick and recorded a directional crossing of the line; the observer records the
first sample inside a one-cell strip (and a wall line is restricted by x to the side
being measured, so the return pass is not confused with the outbound one). Bodies
move less than one cell per tick, so both see almost the same entries; the 4-7%
difference is the sampling point. These keys are gated as ordinary bands on the
median of five offsets.

## Report-only keys

These keys exist and are in the baseline where listed, but they do not measure the
audit's quantity, so do not compare them with an audit number:

- `gate.NAME.crossings` on every gate (`gate.top`, `gate.lane`, `gate.abreast`,
  `gate.strip`, `gate.mid...`). This is the observer's `pw.py` strip count: two
  same-group members that traversed the strip the same way within `pairWindow`
  ticks flip lateral order at both end windows by at least one body width. The
  audit's crossings are the `lane.*` swaps above. On island-1x48 the gate counts
  29 / 1128 where the audit has 33 / 978; on wall-1x48 36 and on wall-4x50 30
  against 217 and 1223.
- `gate.abreast.files_x100` on island-1x48 (an 8-cell strip, 2.51 against 3.02):
  use `gate.strip.files_x100`.
- `stop_go` everywhere: the observer counts a start after a stop per tick, the
  audit counted moving to still transitions between 10-tick samples inside a
  region (`flips` is the nearest comparable key).
- `stopped_permille` on wall-4x50 (24.0% against 16.5%): the observer counts speed
  zero at a decision sample, the audit counted an unchanged position, which a
  shuffling body defeats.
- island-1x150 and strait-3x80, which depend on how a selection of more than 64
  bodies reaches the server: `.scn` orders go through the client's
  64-per-tick uplink (`issueSelection`), the probes issued every order in one
  tick. They are known-failing or report-only until W3 changes the order merge.
- gapsweep gaps 8, 14 and 20 (five groups share one navigator) and every fixture
  listed as KNOWN-FAILING in its header.

## Situations (`tools/scenarios/situation-*.scn`)

A situation is a moment cut out of one of the user's recordings by `TAK_SITUATION=<tick>:<path>` on
a debug replay (`src/client/situation.h`): every live body with its exact position, heading, hit
points, individual speed and recorded id, the orders in flight, the next 600 ticks of the human
commands (clustered back into clicks that go through the client's HUD split and uplink), and the
recording's own body positions 100 and 300 ticks in. It replaces the replay tick-switch A/Bs: the recording
plays back exactly only on the build that made it, a situation runs on whatever the head is.

Its keys beside the observer's:

| Key | Definition |
|---|---|
| `truth.tT.n` | bodies alive in the recording T ticks in (T is 100 or 300) |
| `truth.tT.within2_permille` | per mille of them whose centre is within 2 cells of the recording's on both axes |
| `truth.tT.moved_n` / `truth.tT.moved_within2_permille` | the same for the bodies the recording moved by more than 2 cells |

`truth.*` is a fidelity measure of the harvest, so it is meaningful on the build that recorded the
replay (`scn_truth`, built in that tree): there it is the 90% bar a situation had to clear to be
committed. On the head it is only a report of how far Legion has moved since the recording.

## Grammar added for these keys

- `gate NAME x0 z0 x1 z1 [lateral=x|z] [band=N] [mincount=N] [edge=N] [pairwindow=N] [flip=N]`
- `lane NAME BX0 BZ0 BX1 BZ1 AX0 AZ0 AX1 AZ1 [bsign=-1|1] [asign=-1|1] [window=N] [mincells=N] [across=all]`
- `pair NAME LISTA LISTB [cells=N]` (comma lists of groups, a trailing `*` is a prefix)
- `churn X Z W H every=N [walk=DX,DZ,ROW] [toggle] [from=T] [until=T]`

`churn` is the one directive that edits the world mid-run: `OrderFeed::apply`
places the feature before the tick's orders, so every consumer of the feed (the
runner, the tests) sees the same schedule.

## Tier-2 counter cost (W0 follow-up A2)

Release, crowdbench `--mode legion --units 2000 --players 1 --moving-percent 100 --ticks 600 --seed 1`,
cores 18-21 (no SMT siblings), 14 interleaved A/B pairs (alternating order), A = counters always on,
B = tier-2 counters (traceCells, passScanCells, groupLoopIters, shareScanIters, awarePairs) summed in
debug builds only. Identical hash for A and B in both scenarios (no sim state touched).

| scenario   | median A ms/tick | median B ms/tick | paired B/A median | 95% CI (bootstrap) |
|------------|------------------|------------------|-------------------|--------------------|
| open       | 1.652            | 1.652            | 0.990             | [0.974, 1.008]     |
| sharedgoal | 1.149            | 1.164            | 1.016             | [1.006, 1.026]     |

Gating the counters off did not make a tick cheaper (open is within noise, sharedgoal reads slightly
slower), so the always-on counters stay: the "> 1% and CI excludes 1%" rule for moving them behind
NDEBUG is not met.

## W7 step 0 keys (`behind`, `probe aware`; tools/legion_observe.h, tools/legion_scenario.cpp)

All are observation only (never hashed, never summed into `work.legion_total`); the aware / parting / give-way counters are
`LegionNavigator::Stats` fields that crowdbench exports as `legion_<name>` and the scenario runner as `aware.<name>` under `probe aware`.

| Key | Definition |
|---|---|
| `aware.parts` | bodies a parting shifted aside (one per body per part) |
| `aware.encounters`, `.replans` | (group, mover) corridors awareScan newly planned round; awareScan decisions that asked a group for a new field |
| `aware.builds` | fresh (unshared) field builds that carry a mover corridor; builds per encounter = builds / encounters (the plan's "2 -> 1") |
| `aware.latency_max`, `.latency_sum`, `.latency_n` | ticks from an awareScan replan to the install of a field started after it (max is a running maximum; mean = sum / n) |
| `aware.work`, `aware.work_max` | cells of the descent chains awareScan walked plus its corridor tests (cumulative); the most `aware_pairs + aware_work` any one tick spent. Neither is in `work.legion_total` yet (W7 step 6 declares the bound) |
| `aware.giveway_starts`, `.giveway_ticks`, `.giveway_timeouts` | member-ticks in a give-way hold, holds begun, holds ended by their timeout. 0 until W7 builds a hold; user decision 5 removes the wait, so after W7 they must stay 0 |
| `behind.NAME.members`, `.path_cells`, `.straight_cells`, `.extra_cells`, `.detour_permille` | the later group's route-behind deviation: members ever ordered, mean path walked, mean straight line to the click, their difference (cells) and path / straight - 1 (per mille) |
| `behind.NAME.wait_member_ticks`, `.wait_ticks`, `.wait_run_max`, `.waits` | the wait at the stream's edge: member-ticks standing with a moving EARLIER member within `cells` (8) cells; group-level wait ticks (>= 2 and a quarter of the ordered LATER members); the longest run; runs of >= 30 ticks. All four must be 0 after W7 |

Fixtures: `tools/scenarios/{aware-cross-behind,crosslong}.scn`; `legion_world_test` cases `rb02 awarebig awaredense crossthree crosslong`;
crowdbench scenarios `opposingdoors opposingbridges crossingcolumns`. Base values: `docs/legion-exit-tables.md` (W7 step 0).

## W5 step 0 keys (`reach`, `creep`, `probe claims`; tools/legion_scenario.cpp)

| Key | Definition |
|---|---|
| `reach.NAME.ever_end`, `.ever_tM` | attackers that had EVER been in reach of a target by the end / by mark tick M. In reach = centre within tickCombat's reach: the weapon range, plus 8 x footprint + 24 px for a structure target (`range=PX` replaces it, for a Guard's "within 6 cells") |
| `reach.NAME.now_end`, `.first`, `.t50` | in reach at the end; first tick any / half of the attackers were in reach (-1 never) |
| `reach.NAME.out600_end`, `.out600_peak`, `.farthest_held` | attackers out of reach whose centre cell has not changed for 600 ticks (the audit's "standing out of range"), at the end / the most on any tick; the farthest of them from its nearest target (cells) |
| `reach.NAME.hold_out_max`, `.hold0_max` | the longest run of ticks one attacker spent out of reach in Legion's Holding state; the same while its field potential was 0 (the wall ring's "Holding at potential 0") |
| `reach.NAME.damage_end`, `.damage_tM` | hit points the targets lost to damage, summed tick by tick (a regeneration is never credited back; a death takes what was left) |
| `creep.NAME.ticks`, `.inside_ticks` | MV-07's goal-area creep counter: member-ticks inside the click's arrival disc with orders and a step in (0, cap/8]; all member-ticks inside it with orders |
| `claims.bad_max` | the most overlapping + lost + dangling arrival-slot claims at any audit (every 10 ticks); the claims invariant, an eq-0 safety key. `claims.orphans_*` (point cells no slot covers) is report-only |
| `gauge.engaged_max`, `engaged.member_ticks` | `Stats::engagedNow`: the most bodies tickCombat braked in reach on one tick, and the member-ticks summed over the run (every Legion run) |

The fixtures are `tools/scenarios/{attackring-*,fight-retarget,ringcross,lateblock-*,chase-*,goalblock-*,keelturn-*,staticblock-*}.scn`,
generated by `tools/scenarios/gen_w5_fixtures.py`; base values and the PLAN targets are in `docs/legion-exit-tables.md` (W5 step 0).

## W9 step 0 keys (`probe w9` / `--w9`, wall and crossing splits; tools/legion_observe.h, tools/legion_scenario.cpp, src/sim/legion.cpp)

All are observation only: never hashed, never summed into `work.legion_total`, and no decision reads them.

| Key | Definition |
|---|---|
| `w9.seal_settles`, `.creep_presses`, `.deep_rechoices`, `.anchor_advances`, `.gate_lane_moves`, `.gate_holds`, `.fold_binds`, `.turn_guard_rejects`, `.clear_aims`, `.meet_latches`, `.order_inversions_avoided`, `.order_inversions_taken` | `LegionNavigator::Stats` counters, one per W9 mechanism (A2, A3, A4, A4 round 2, B2, B2, B3, B3, B4, C1, B6, B6): how often the mechanism DID fire. 0 until its step builds it; crowdbench exports them as `legion_<name>` |
| `census.*` (debug builds only) | how often each mechanism's trigger WOULD fire, counted where the mechanism will sit, with no change to any decision, hashed state or Stats counter. Definitions: `struct Census` in `src/sim/legion.cpp` (`a1_inside`, `a2_seal`, `a3_creep`, `a4_release_slotted`, `b1_engaged_wall`, `b2_lane_differs`, `b3_guard_rejects`, `b3_fold_binds`, `b4_clear_alt`, `b6_blocked`, `c1_headon_entries`, `c1_unseen`, ...). A flowing control (doubleturn, uturn, opentangent, wall-4x50) must read 0 for the B2 / B3 fold / C1 rows |
| `wall_still_samples`, `wall_touch_still_permille` | ordered ground samples that did NOT move (the bodies `wall_touch_permille` leaves out) and the permille of them with clearance 0 |
| `wall_touch_{gate,ring,corner,flat}_permille` | the moving touches (clearance 0) by place, in permille of `wall_samples`, summing to `wall_touch_permille` up to rounding. gate: within 6 cells of a configured `gate` region; ring: the body's current leg is an attack / guard (the reach ring round its target); corner: an illegal origin on both axes beside the body; flat: any other wall |
| `wall_touch_near_{gate,ring,corner,flat}_permille` | the same places as permille of the near-wall samples (`wall_touch_near_permille`) |
| `gate.NAME.cross_{none,field,gate,pivot,pass,blocked,direct}` | `gate.NAME.crossings` by flip SITE. Each member's lateral displacement between its two end-window entries is summed per steering site (`LegionNavigator::steerSite`: the field descent, the passage gate, the pinwheel, a lane-discipline pass, drive's blocked branch, a straight walk to the goal, none), and a flipped pair goes to the site whose displacement difference between the two bodies is largest in the direction the order changed (none when no site moved them that way). The seven sum to `crossings` |
| `gate.NAME.crossings_per_file_x100` (GAP fixtures, computed by `legion_check.py`) | W8-1's per-file key: crossings x 10000 / max(100, files_x100) at each offset, medianed over the eleven gate offsets |

`legion_scenario --trace ID|GROUP[,...]` (debug builds; the environment variable `TAK_LEGION_TRACE=ID,...` does the same in any tool or test) prints one `TRACE t=... u=...` line on
stderr for every settle window, re-choice, yield request, gate commit and release, pinwheel turn and gate width read of those members; a group name traces the whole group. With
`--w9`, a run also prints `TAIL <scenario> group <G> offset=<o>: n not arrived: ids...`, so a second run can trace the tail.
