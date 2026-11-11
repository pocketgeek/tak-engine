# Legion scenario keys: definitions

The `.scn` fixtures in `tools/scenarios/` measure Legion with the shared observer
(`tools/legion_observe.h`). This page defines the keys that are not obvious from
their names, and says which keys measure a different quantity than their name
might suggest. Grammar: `tools/scenarios/README.scn-format.txt`.

The committed baseline (`tools/scenarios/baseline.json`) gates the median of five
start offsets (0, +1, -1, +2, -2), which can differ a lot from a single offset-0
run on keys with a large offset spread.

## Lane, strip and contact keys

| Fixture | Key | Definition |
|---|---|---|
| island-1x48 | `lane.cross.swaps` / `.pairs` | first entry into x=70 and x=130 (lateral z), pairs entering x=70 within 300 ticks of each other, swapped by >= 2 cells at both lines |
| island-1x48 | `gate.strip.files_x100` | distinct z/2 files among members in x 95-105, mean over 10-tick samples with >= 3 inside |
| wall-1x48 | `lane.cross.swaps` / `.pairs` | first entry into z=60 west of the wall (lateral 98-x) and east of it (lateral x-102), 300 ticks, >= 2 cells |
| wall-1x48 | `gate.strip.files_x100` | the same files measure over x 97-102, z < 48 |
| wall-4x50 | `lane.cross.swaps` / `.pairs` | as wall-1x48 with `across=all`: pairs of members of different groups count |
| wall-4x50 | `gate.strip.files_x100` | as wall-1x48 |
| aware-headon, aware-unseen, aware-cross, aware-seen, aware-attack | `pair.contacts.permille_x100` | live ordered a x b pairs sampled every 10 ticks whose centre cells are within 2 on both axes, per mille x 100 |
| refreshchurn | `work.refresh_completed.total` | field refreshes installed in 900 ticks under a 2x2 feature placed in a fresh cell every tick; the control without churn (refreshchurn-control) gives 0 |

A `lane` line records the first sample inside a one-cell strip (and a wall line is
restricted by x to the side being measured, so the return pass is not confused
with the outbound one). Bodies move less than one cell per tick, so this sees
almost the same entries as a directional crossing test between consecutive
ticks; the two differ by a few percent at most. These keys are gated as ordinary
bands on the median of five offsets.

## Keys that measure something else than their name suggests

- `gate.NAME.crossings` on every gate (`gate.top`, `gate.lane`, `gate.abreast`,
  `gate.strip`, `gate.mid...`). This is the observer's `pw.py` strip count: two
  same-group members that traversed the strip the same way within `pairWindow`
  ticks flip lateral order at both end windows by at least one body width. Lane
  order swaps across a whole obstacle are the `lane.*` keys above.
- `gate.abreast.files_x100` on island-1x48 is an 8-cell strip; for the file
  count across the gap use `gate.strip.files_x100`.
- `stop_go` everywhere counts a start after a stop per tick, not moving-to-still
  transitions between 10-tick samples inside a region (`flips` is the nearest
  key to that).
- `stopped_permille` counts speed zero at a decision sample, not an unchanged
  position (which a shuffling body defeats).
- island-1x150, strait-3x80 and the other fixtures over 64 bodies depend on how a
  selection reaches the server: `.scn` orders go through the client's
  64-per-tick uplink (`issueSelection`), and the server merges them into one
  explicit selection order per click (`docs/legion-pathfinding.md` "Convoys").
  They run as nightly-labelled `legion_scenario_*` tests; strait-3x80 is
  KNOWN-FAILING in its header.
- gapsweep gaps 8, 14 and 20 (five groups share one navigator) and every fixture
  listed as KNOWN-FAILING in its header are report-only.

## Situations (`tools/scenarios/situations/situation-*.scn`)

A situation is a moment cut out of one of the user's recordings by `TAK_SITUATION=<tick>:<path>` on
a debug replay (`src/client/situation.h`): every live body with its exact position, heading, hit
points, individual speed and recorded id, the orders in flight, the next 600 ticks of the human
commands (clustered back into clicks that go through the client's HUD split and uplink), and the
recording's own body positions 100 and 300 ticks in. A recording plays back exactly only on the
build that made it; a situation runs on whatever the head is.

Its keys beside the observer's:

| Key | Definition |
|---|---|
| `truth.tT.n` | bodies alive in the recording T ticks in (T is 100 or 300) |
| `truth.tT.within2_permille` | per mille of them whose centre is within 2 cells of the recording's on both axes |
| `truth.tT.moved_n` / `truth.tT.moved_within2_permille` | the same for the bodies the recording moved by more than 2 cells |

`truth.*` is a fidelity measure of the harvest, so it is meaningful on the build that recorded the
replay (`scn_truth`, built in that tree): there it is the 90% bar a situation has to clear to be
committed. On the head it only reports how far Legion has moved since the recording.

## Grammar for these keys

- `gate NAME x0 z0 x1 z1 [lateral=x|z] [band=N] [mincount=N] [edge=N] [pairwindow=N] [flip=N]`
- `lane NAME BX0 BZ0 BX1 BZ1 AX0 AZ0 AX1 AZ1 [bsign=-1|1] [asign=-1|1] [window=N] [mincells=N] [across=all]`
- `pair NAME LISTA LISTB [cells=N]` (comma lists of groups, a trailing `*` is a prefix)
- `churn X Z W H every=N [walk=DX,DZ,ROW] [toggle] [from=T] [until=T]`

`churn` is the one directive that edits the world mid-run: `OrderFeed::apply`
places the feature before the tick's orders, so every consumer of the feed (the
runner, the tests) sees the same schedule.

## Counter cost

`LegionNavigator::Stats` counters are always on. The tier-2 counters
(traceCells, passScanCells, groupLoopIters, shareScanIters, awarePairs) were
measured summed in debug builds only against always on: Release, crowdbench
`--mode legion --units 2000 --players 1 --moving-percent 100 --ticks 600 --seed 1`,
cores 18-21 (no SMT siblings), 14 interleaved A/B pairs (alternating order),
A = always on, B = debug-only. Identical hash for A and B in both scenarios (no
sim state touched).

| scenario   | median A ms/tick | median B ms/tick | paired B/A median | 95% CI (bootstrap) |
|------------|------------------|------------------|-------------------|--------------------|
| open       | 1.652            | 1.652            | 0.990             | [0.974, 1.008]     |
| sharedgoal | 1.149            | 1.164            | 1.016             | [1.006, 1.026]     |

Gating them off does not make a tick cheaper, so they stay always on: the
"> 1% and CI excludes 1%" rule for moving a counter behind NDEBUG is not met.

## Awareness and route-behind keys (`behind`, `probe aware`; tools/legion_observe.h, tools/legion_scenario.cpp)

All are observation only (never hashed, never summed into `work.legion_total`); the aware / parting / give-way counters are
`LegionNavigator::Stats` fields that crowdbench exports as `legion_<name>` and the scenario runner as `aware.<name>` under `probe aware`.

| Key | Definition |
|---|---|
| `aware.parts` | bodies a parting shifted aside (one per body per part) |
| `aware.encounters`, `.replans` | (group, mover) corridors awareScan newly planned round; awareScan decisions that asked a group for a new field |
| `aware.builds` | fresh (unshared) field builds that carry a mover corridor; builds per encounter = builds / encounters |
| `aware.latency_max`, `.latency_sum`, `.latency_n` | ticks from an awareScan replan to the install of a field started after it (max is a running maximum; mean = sum / n) |
| `aware.work`, `aware.work_max` | cells of the descent chains awareScan walked plus its corridor tests (cumulative); the most `aware_pairs + aware_work` any one tick spent. `aware_work` is outside `work.legion_total` and inside the report-only `work.legion_all` |
| `aware.giveway_starts`, `.giveway_ticks`, `.giveway_timeouts` | member-ticks in a give-way hold, holds begun, holds ended by their timeout. Movers never wait for a crossing group, so these must stay 0 |
| `behind.NAME.members`, `.path_cells`, `.straight_cells`, `.extra_cells`, `.detour_permille` | the later group's route-behind deviation: members ever ordered, mean path walked, mean straight line to the click, their difference (cells) and path / straight - 1 (per mille) |
| `behind.NAME.wait_member_ticks`, `.wait_ticks`, `.wait_run_max`, `.waits` | the wait at the stream's edge: member-ticks standing with a moving EARLIER member within `cells` (8) cells; group-level wait ticks (>= 2 and a quarter of the ordered LATER members); the longest run; runs of >= 30 ticks. All four must be 0 |

Fixtures: `tools/scenarios/{aware-cross-behind,crosslong}.scn`; `legion_world_test` cases `rb02 awarebig awaredense crossthree crosslong`;
crowdbench scenarios `opposingdoors opposingbridges crossingcolumns`.

## Reach, creep and claims keys (`reach`, `creep`, `probe claims`; tools/legion_scenario.cpp)

| Key | Definition |
|---|---|
| `reach.NAME.ever_end`, `.ever_tM` | attackers that had EVER been in reach of a target by the end / by mark tick M. In reach = centre within tickCombat's reach: the weapon range, plus 8 x footprint + 24 px for a structure target (`range=PX` replaces it, for a Guard's "within 6 cells") |
| `reach.NAME.now_end`, `.first`, `.t50` | in reach at the end; first tick any / half of the attackers were in reach (-1 never) |
| `reach.NAME.out600_end`, `.out600_peak`, `.farthest_held` | attackers out of reach whose centre cell has not changed for 600 ticks ("standing out of range"), at the end / the most on any tick; the farthest of them from its nearest target (cells) |
| `reach.NAME.hold_out_max`, `.hold0_max` | the longest run of ticks one attacker spent out of reach in Legion's Holding state; the same while its field potential was 0 (the wall ring's "Holding at potential 0") |
| `reach.NAME.damage_end`, `.damage_tM` | hit points the targets lost to damage, summed tick by tick (a regeneration is never credited back; a death takes what was left) |
| `creep.NAME.ticks`, `.inside_ticks` | the goal-area creep counter: member-ticks inside the click's arrival disc with orders and a step in (0, cap/8]; all member-ticks inside it with orders |
| `claims.bad_max` | the most overlapping + lost + dangling arrival-slot claims at any audit (every 10 ticks); the claims invariant, an eq-0 safety key. `claims.orphans_*` (point cells no slot covers) is report-only |
| `gauge.engaged_max`, `engaged.member_ticks` | `Stats::engagedNow`: the most bodies tickCombat braked in reach on one tick, and the member-ticks summed over the run (every Legion run) |

The fixtures are `tools/scenarios/{attackring-*,fight-retarget,ringcross,lateblock-*,chase-*,goalblock-*,keelturn-*,staticblock-*}.scn`,
generated by `tools/scenarios/gen_w5_fixtures.py`.

## Mechanism counters and wall / crossing splits (`probe w9` / `--w9`; tools/legion_observe.h, tools/legion_scenario.cpp, src/sim/legion.cpp)

All are observation only: never hashed, never summed into `work.legion_total`, and no decision reads them.

| Key | Definition |
|---|---|
| `w9.seal_settles`, `.creep_presses`, `.deep_rechoices`, `.anchor_advances`, `.gate_lane_moves`, `.gate_holds`, `.fold_binds`, `.turn_guard_rejects`, `.clear_aims`, `.meet_latches`, `.order_inversions_avoided`, `.order_inversions_taken` | `LegionNavigator::Stats` counters, one per mechanism (the field comments in `src/sim/legion.h` define each): how often it DID fire; crowdbench exports them as `legion_<name>`. Only `meet_latches` (a Point latched a head-on meet) is counted; the others always read 0 |
| `census.*` (debug builds only) | how often each mechanism's trigger WOULD fire, counted where the mechanism sits, with no change to any decision, hashed state or Stats counter. Definitions: `struct Census` in `src/sim/legion.cpp` (`a1_inside`, `a2_seal`, `a3_creep`, `a4_release_slotted`, `b1_engaged_wall`, `b2_lane_differs`, `b3_guard_rejects`, `b3_fold_binds`, `b4_clear_alt`, `b6_blocked`, `c1_headon_entries`, `c1_unseen`, ...). A flowing control (doubleturn, uturn, opentangent, wall-4x50) must read 0 for the B2 / B3 fold / C1 rows |
| `wall_still_samples`, `wall_touch_still_permille` | ordered ground samples that did NOT move (the bodies `wall_touch_permille` leaves out) and the permille of them with clearance 0 |
| `wall_touch_{gate,ring,corner,flat}_permille` | the moving touches (clearance 0) by place, in permille of `wall_samples`, summing to `wall_touch_permille` up to rounding. gate: within 6 cells of a configured `gate` region; ring: the body's current leg is an attack / guard (the reach ring round its target); corner: an illegal origin on both axes beside the body; flat: any other wall |
| `wall_touch_near_{gate,ring,corner,flat}_permille` | the same places as permille of the near-wall samples (`wall_touch_near_permille`) |
| `gate.NAME.cross_{none,field,gate,pivot,pass,blocked,direct}` | `gate.NAME.crossings` by flip SITE. Each member's lateral displacement between its two end-window entries is summed per steering site (`LegionNavigator::steerSite`: the field descent, the passage gate, the pinwheel, a lane-discipline pass, drive's blocked branch, a straight walk to the goal, none), and a flipped pair goes to the site whose displacement difference between the two bodies is largest in the direction the order changed (none when no site moved them that way). The seven sum to `crossings` |
| `gate.NAME.crossings_per_file_x100` (GAP fixtures, computed by `legion_check.py`) | the per-file key: crossings x 10000 / max(100, files_x100) at each offset, medianed over the eleven gate offsets |

`legion_scenario --trace ID|GROUP[,...]` (debug builds; the environment variable `TAK_LEGION_TRACE=ID,...` does the same in any tool or test) prints one `TRACE t=... u=...` line on
stderr for every settle window, re-choice, yield request, gate commit and release, pinwheel turn and gate width read of those members; a group name traces the whole group. With
`--w9`, a run also prints `TAIL <scenario> group <G> offset=<o>: n not arrived: ids...`, so a second run can trace the tail.

## Mechanism attribution keys (`why.*`; src/sim/legion.h, tools/legion_observe.h)

Every Legion member carries an unhashed `why` byte, the mechanism of its last update: the walk it was on, or why it
stood (`LegionNavigator::Why`, read through `steerWhy`; finer than `steerSite`). At each per-tick sample the observer
reads it and splits five of its motion keys by it, so a fixture says which mechanism makes bodies slide, back up or
stand. Observation only: never hashed, never read by a decision, not in the baseline and not gated by `legion_doc_links`.
A mechanism with no ticks and no samples prints no keys. Retail bodies are no members, so everything of theirs is
`why.none.*`. The `<mechanism>` names, in the enum's order:

| `<mechanism>` | the member's last update was |
|---|---|
| `none` | not a member (Retail), or no update yet |
| `steer` | an ordinary step toward its route: the field descent, the passage gate, the pinwheel aim |
| `direct` | a straight walk to its goal (its own line, or the heading while its field builds) |
| `pass` | a lane-discipline pass, or walking its new lane after one |
| `flow_first` | the first step of a flow-around shuffle, committed in drive's blocked branch |
| `flow_brisk`, `flow_cap` | walking a committed flow-around shuffle: `flow_brisk` is a formation member's on a move-like or fight leg, or a side-step round a still target; `flow_cap` any other. Both walk at travel speed unless the body is a plain mover (no formation slot), whose shuffle keeps the facing cap |
| `sidestep` | an opposing-traffic side-step (`sidestep`) |
| `room` | a move over at a merge (`makeRoom`) |
| `corner` | the corner clip (`cornerStep`) |
| `route_brisk`, `route_cap` | walking a local detour route: a long one (`kBriskRoute` cells or more, a still target, a spot route), or a short one. Both at travel speed |
| `press` | a `pressIn` walk toward reach (attackers); it keeps the facing cap |
| `yield` | a settled body stepping aside (`serviceYields`) |
| `hold_movers` | held: the wanted cell is covered by bodies of its own order that will move (a queue) |
| `hold_settled` | held: by settled, idle or engaged bodies (`blockedBySettled`) |
| `hold_jam` | held: by another order's jam (`blockedByJam`) |
| `hold_corner` | held: the corner clip found no legal axis |
| `hold_refused` | held: `commitGroundStep` refused the step |
| `hold_detour` | held this update: a local detour was just planned (walked from the next) |
| `hold_ask` | held: asked bodies to yield, part or give way |
| `hold_admission` | held: a narrow passage is open the other way |
| `hold_noaim` | held: no legal neighbour nearer (`aimCell < 0`) |
| `hold_ring` | held: a blind or shadowed ring member standing in reach |
| `hold_spot` | held: `spotRoute` found no way to its spot yet |
| `hold_rest` | held: a long-held body's rest stride (`heldRest`) |
| `hold_yielding` | held: parting a lane for another body |
| `hold_livelock` | held: the livelock latch forbids a flow-around |
| `hold_still` | held: an attacker round a still target gaining nothing (no new shuffle) |
| `wait` | waiting for its field, its registration or a repair |
| `trapped` | trapped |
| `nudge` | the press: on a leg that engages no enemies, blocked by bodies with no way round, the free axis of the step walked and the other to the origin cell's edge (Retail's clamp); an unturned step at travel speed |

The `<metric>` suffixes; each is the mechanism's share of the fixture-wide key named, and the shares sum to it:

| Key | Definition |
|---|---|
| `why.<mechanism>.ticks`, `.moved_ticks` | ordered ground member-ticks tagged with the mechanism, and those of them on which the body moved (no fixture-wide key; `ticks` over all mechanisms is the number of ordered ground member-ticks) |
| `why.<mechanism>.samples`, `.stopped` | ordered member decision samples (every `decisionEvery` = 10 ticks) tagged with the mechanism, and those at speed 0: the split of `decision_samples` and of `stopped_permille`'s numerator |
| `why.<mechanism>.statue` | the split of `statue_ticks`: ticks on which the body moved, did not turn, and stepped at most a ninth of its travel speed (the facing cap's eighth plus slack). A press's sub-cell nudge is a statue by this definition, so `why.nudge.statue` is the press, not a creep |
| `why.<mechanism>.sideways`, `.backward` | the split of `sideways` and `backward`: steps whose direction is more than 45 / more than 90 degrees off the body's facing |
| `why.<mechanism>.reversals` | the split of `reversals`: entries into a cell the body left within the last 90 ticks |

The ctests `legion_nocap` (`why.flow_cap.statue`, `why.route_cap.statue` at most 10 each on island-1x100) and `legion_press`
(`why.hold_movers.stopped` at most 5,200 on corner-1x100) bound the cap and queue keys; `legion_flowahead` bounds the
fixture-wide `backward`. Nudges count as statue, sideways and backward steps (`why.nudge.*`), so a fixture's nudge-free
creep is `statue_ticks` minus `why.nudge.statue`.
