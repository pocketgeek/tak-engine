# W6 step 0: flyer instruments and the step-0 head

`tools/legion_flyers_test.cpp` (`ctest -R legion_flyers`, 17 cases, about 40 CPU-seconds, label `w6`) holds the
measurement cases of PLAN 4 W6 step 0. Each prints the numbers the W6 steps move and asserts determinism (the
workers run prints and hashes exactly as the serial run). `W6_REQUIRE=1` additionally asserts the PLAN targets, so
each case is the acceptance check of the step that meets it and fails on a head that does not (all but `auditlift`
and `mv08group`'s slow_done leg fail on this head; see "Status" below). `W6_VERBOSE=1` prints traces, `W6_SHIFT=n`
shifts every spawn by n cells (the baselined offsets 0, +1, -1, +2, -2).

Head measured: `6333e38d` (W4 land, protocol 242) plus the instrument-only counters below, optimized Debug (`-O2`),
`TAK_LEGION_VERIFY` clean on the lift and touchdown cases. Step 0 changes no simulated state: the counters are
observation only (never hashed, never read by a decision).

## Fidelity of the ports

The probes were throwaway patches in the audit. Reproduced exactly on this head, which validates the ports:
mixedformation Alt+1 Legion hash `8e5f5dd4d865661f` equals `legion_world_test mixedformation`'s; Retail
`1efc73d8803a5c8b` equals the Retail line (so the squad parameter and the overlap/convoy observers do not touch the
sim); MIXED_SEAL max hover 5149 (audit 5149); fl05match80 flyers done 2226-2339 (audit 2227-2337), match200 883-1046,
match0 2151-2286; fl05unreach max hover 8638 (audit 8528-8645), Retail landed by 3293; landunder own-squad overlap
1459 (audit 1460); Ctrl mixedformation away_max 2153, pace 2.51 (audit 2153).

## Counters (`LegionNavigator::Stats`, crowdbench keys `legion_<name>`)

| counter | live in step 0 | filled by |
|---|---|---|
| `relifts` | yes: a lift episode that begins within 600 ticks of the same flyer landing from its previous one | - |
| `cap_hits`, `station_releases_a`, `station_releases_b`, `go_arounds`, `station_overflow` | declared, 0 | steps 6, 5, 5, 2, 3 |

(A release or a cap hit is an event of behaviour that does not exist yet; counting one without the behaviour would
need per-flyer state that only those steps add.)

## Cases, head values and status

"Met by W3" = the PLAN target (written against `b8a4110`) is already reached on this head; the step keeps it as
"no worse than head" (C20). "Open" = still to do.

| case | what it measures | head | PLAN target | status |
|---|---|---|---|---|
| mixedsquad Ctrl+1 | 20 ground + 8 flyers behind a wall, Move | ground_done 3241 (was never), away_max 2153, landed_ahead 315, max_hover 18, pace 2.51 | ground_done <= 2650, away_max <= 300, landed_ahead 0, hover <= 60 | ground_done now finishes (W3); rest open (T5 steps 3-4) |
| mixedsquad Alt+1 / none | same | Alt: ground_done 3016, away_max 285, hover 18, hash `8e5f5dd4d865661f`; none: ground_done 2746, max_hover 1011, lifts 10359 | Alt within 2% | baseline; the "none" hover 1011 is the lift area rule (T6) |
| mixedsquad offsets | Alt+1 and Ctrl+1 at the 5 start offsets | Alt shift +1: **overlap 10 ticks, illegal footprint 10 ticks** (a body on a landed flyer at the destination); all others 0 | overlap 0 | open (FL-03); new finding, the stock test at offset 0 misses it |
| mixedseal | MIXED_SEAL: the same with the wall sealed, 6000 ticks | max_hover 5149, landed 0/8, hold_ticks 48000 | hover <= 1200, landed 8 | open (FL-04 release A) |
| mv08group | 10 slow + 10 fast + 8 flyers, 206 cells round a wall | Ctrl+1: slow_done 2670 (was 3061), max_fast_lead 1496 (1804), fly_away_max 1702, landed_ahead 251; untagged: slow_done 2656; Alt: lead 155, away 263 | Ctrl+1: away <= 300, landed_ahead 0, slow_done <= 2700 | slow_done met by W3; stations open (T5 step 3); lead is Q1, user decision 1 = no ground pacing |
| mv08deadend / deadendrejoin | 120 Alt+1 bodies into a closed dead end of width 4/6/8/12, run on after rest | holding 4/1/5/0, anchored_reordered 1/1/2/0, nearest-to-point 120/51/66/23 px (width 6: id 60 re-ordered off the point, as the audit's id 80); not in a squad: 0 | holding 0, no anchored body re-ordered, <= 32 px | open (T5 step 5) |
| ctrlmixed | Ctrl+1 group of 2 clicks (separated, closed convoy, close) through `issueSelection` | convoy_own 8/8, cross_pairs 0 (the convoy link exists since W3 A1); away_max 2048 (stations missing); distinct convoys in all three | away_max <= 300 from the click's own ground, 0 cross pairs | link met by W3; away open (T5 step 3) |
| ctrlmixedbig | 200 + 24 and 300 + 24 through `issueSelection` (4 and 6 parts), flyers first and last, opposite corners | away_max 3263-3282, convoy_own 24/24 in all four runs, station_overflow 0 | away <= 300, every flyer carries its ground's convoyTick | link met by W3; away open |
| factoryjoin | a squad factory's unit joins a resting 60-body Alt+1 formation | joiner reaches the crowd after 291 ticks, settles 611 ticks later, 1 order start, crowd re-ordered 0 | settles <= 600 after reaching, <= 1 order | 11 ticks over; open (T5 step 5) |
| landunder | one slow body walks under a flyer in stage 3 | enemy: overlap **394** ticks; own squad: overlap **1459**; a fast walker or a late start: 0 | overlap 0 | open (FL-03); the audit's trap, reproduced |
| descentwalkin | 9 bodies walk under a descending flyer | enemy 125 / own squad 130 overlap ticks, arrive 556 / 401; allied idle 1 / 209; Retail 115 / 492 | overlap 0, arrive <= 286, allied unchanged | the permanent trap is gone (all arrive, W3); overlap and arrival open |
| auditlift | 12 allied flyers, 120 bodies, three placements | max hover 1686 / 971 / 1703 (was 3658 / 951 / 4099), takeoffs 11 / 12 / 12, max 2 per flyer, relifts 1 | max hover <= 1800, <= 2 takeoffs per flyer | **met by W3** |
| auditlift40 | the 40-body case | still airborne 0 (was 4), ground idle 2662 (never), max hover 1807 (11016), takeoffs 4, max 1 | still 0, hover <= 1800, idle <= 4500, <= 2 takeoffs | met by W3 except hover 1807 (7 ticks over): the cap, rest and gating keep their job; step 7 (home-excluded landing) is dropped, ground idle needs no help |
| liftcombat | armed lifted flyers; an enemy walks into range | react **322** ticks (control, landed: 1); flyers fire only after landing again | acquire <= 16 ticks | open (FL-01 poll) |
| fl04 corridor | AR-02 geometry, 120 + 8 | max_hover **1134** (was 9630), ground_done 4096 (never), flyers_done 4637 but landed 0/8 and 1507 px from the point 600 ticks later | hover <= 1200, landed >= 7/8 | hover met by W3; landing open |
| fl04 box | point inside a closed box | landed 8/8, flyers_done 2412 (369 after the ground stopped), but 1023 px from the point | landed, <= 128 px | landing met; distance open |
| fl04 big440 | open map, 440 + 24 | max_hover 18, ground_done 4096; **9 of 24 flyers still airborne with orders at 7000** (moving, not still: circling a crowd-covered landing site) | arrival hover <= 300 | hover met; the circling flyers are a new finding for W6 step 7 / W5 |
| fl04 door | 3-wide door, slow start | ground_done 6076, flyers land 8/8, hover 113; neither release rule fires early (B at 5831 with the centroid inside the radius) | early releases < 5% | baseline for step 5's predicate |
| fl04 door2 | 2-wide door | 110 of 120 bodies hold for ever, flyers hover 7937 | - | reported only: a ground that never arrives (W5) |
| fl05match | separate flyer click 80 / 200 px from the ground's point | match80 done 2226-2339, match200 883-1046, match0 2151-2286 | match80 883-1046 +-10% | open (T5/T6 step 3) |
| fl05unreach | full wall, ground holds | max_hover 8638, hover after the first landing 8638, ends 1897 px off; Retail hover 0, landed by 3293 | hover <= 1200, no second hover, <= 128 px | open (step 4/5) |
| splitpatrol | an air patrol far from a separate ground patrol | station_ticks **12000** (every flyer-tick captured); same route 12000 | 0 | open (step 3) |

The fl04 "would release" columns evaluate the planned rules offline: A fires when no busy ground member sets a new
best distance to its point for 450 ticks, B when the ground centroid stays within 16 px of a reference for 900
ticks; `early` = it fires while the centroid is outside the release radius and the ground completes afterwards.
On this head only the corridor shows an early B (fires at 3590, the ground completes at 4096).

## Not built

- The "harvested situations" leg of FL-04's early-release statistic (the recorded `situation-*.scn` moments carry no
  formation flyers); big440, the door cases and the corridor are the sample.
- The behaviour counters above (`cap_hits`, releases, `go_arounds`, `station_overflow`) wait for their steps.

## Steps 1-7 (branch task-w6-steps): results against the PLAN targets

Measured with `legion_flyers_test` / `legion_world_test` (offset 0 unless stated) and `legion_scenario --offsets gate`
(11 offsets) on optimized Debug. "step-0 head" is the table above. Each step is its own commit with its numbers.

| step | what landed | how it differs from PLAN 3.5 / 3.6 |
|---|---|---|
| 1 | `LegionNavigator::advancing(id, limit)` (kLiftStall 120, kStationStall 450), `World::flyerGrounded` | reads a W6 headway clock (`Member::headTick`, not hashed, best potential per field serial, runs on through routes and side-steps), not `stallTick`: `progress` mixes two scales (squared cells before the field is done, potential x64 after), so a body that registered before its field was built read "stalled" for most of its walk |
| 2 | FL-03: a stage-3 descender is stamped into the grounded overlay; leave-only in `bodiesFree` | the bounded touchdown backstop (C5) is **not built**: with the stamp no fixture keeps any overlap, so a go-around would never fire (no gate can fail on it) |
| 3 | stations for Ctrl and Alt squads, paired on (order class, `convoyTick`), up to 4 per squad, `stationOverflow` | two clicks of one class applied in the same tick share a `convoyTick` and so a station key (splitpatrol now lands its air click one tick later); a squad with one bucket keeps all its ground in the centroid (Alt hash-identical) |
| 4 | rejoin: ground-only centre, modal settled point P, never an anchored body | re-order beyond 1x the settled crowd's reach of P, not 2x: 2x stranded the factory joiner |
| 5 | FL-04 releases A (no headway 450) and B (centroid static 16 px / 900), sticky on the order (hashed when set) | the post-release cap floor max(pace, base/2) is **not shipped**: it lands big440's circling flyers but moves Alt+1 start offsets +1/-1 by +21%/+53%, Ctrl+1 move by +10% and mv08group Alt by +3.3% (measured on steps 5 and 6) |
| 6 | FL-01 headway gating, 1800-tick cap (quiet included) and 600 rest, combat poll every 8 ticks staggered, escort yaw freeze | the bound-for-a-goal-under-it term keeps its old rule (gating it cost liftflyers' on-goal mode +26%); the own-squad exemption is narrowed to a **stalled** member whose goal or next 2 planned cells the flyer covers (T5's literal rule re-lifted the wing at every arrival: hover 18 -> 448-842); a station flyer without horizontal velocity does not yaw (all of mixed2's spins were take-off yaw) |
| 7 | home-excluded landing | **dropped** (C20): auditlift40's ground is idle at 2662 (PLAN <= 4500) and nothing is still airborne |

| case | step-0 head | after step 6 | PLAN target | met? |
|---|---|---|---|---|
| mixedsquad Ctrl+1 move | done 3241, away 2153, landed_ahead 315, hover 18 | done 2656, away 288, landed_ahead 0, hover 18 | done <= 2650, away <= 300, 0, <= 60 | away, ahead, hover yes; done 6 ticks over |
| mixedsquad Ctrl+1 offsets | away 2123-2175 | away 246-304, hover 18 (offset -1: 86) | away <= 300 | 4 of 5 |
| mixedsquad Alt+1 | done 3016, away 285, hash 8e5f5dd4d865661f | identical | within 2% | yes |
| mixedsquad Alt+1 offset +1 | overlap 10, illegal 10 | 0, 0 | 0 | yes |
| mixedsquad Ctrl+1 fight | done 3061, away 2151 | done 4636, away 279 | (Alt fight 2611) | away yes; ground_done +51% (open) |
| mixedseal | hover 5149, landed 0 | hover 458, landed 8 | <= 1200, 8 | yes |
| mv08group Ctrl+1 | slow_done 2670, away 1702, ahead 251 | slow_done 2343, away 307, ahead 0 | <= 2700, <= 300, 0 | away 7 px over |
| deadendrejoin w4/6/8/12 | holding 4/1/5/0, anchored re-ordered 1/1/2/0 | 0/0/0/0, 0 | 0, 0 | yes |
| mv08deadend (w6) nearest to point | 51 px | 36 px | <= 32 | 4 px over |
| ctrlmixed two-click | away 2048 | away 232/266/266, cross 0 | <= 300, 0 cross | yes |
| ctrlmixedbig 200/300+24 | away 3263-3282, overflow 0 | away 574-597 (en route 176-187), overflow 0, convoy 24/24 | <= 300 | no: arrival over a 200-body crowd |
| factoryjoin | settles 611 after reaching | 71, 1 order | <= 600, <= 1 | yes |
| landunder | overlap 394 (enemy) / 1459 (own) | 0 / 0 | 0 | yes; the enemy slow walker no longer arrives (as the landedFirst control on the step-0 head: W5) |
| descentwalkin | overlap 125/130, arrive 556/401 | 0/0, arrive 555/404 | 0, <= 286 | overlap yes; arrival no |
| auditlift where 0/1/2 | hover 1686/971/1703 | 1480/971/1588, max 1 takeoff | <= 1800, <= 2 | yes |
| auditlift40 | hover 1807, idle 2662 | 1807 (episode ~1720 + landing; cap not hit), idle 2662 | <= 1800, <= 4500 | hover 7 ticks over (landing descent) |
| liftcombat | react 322 | 1 | <= 16 | yes |
| fl04 corridor | hover 1134, landed 0/8 | 719, 8/8 | <= 1200, >= 7/8 | yes |
| fl04 box | far 1023 px | 168 px | <= 128 | no |
| fl04 big440 | 9/24 circling | 9/24 circling (max_hover 18) | arrival hover <= 300 | hover yes; the circling flyers remain (floor not shipped) |
| fl04 early releases | - | big440 0, door 0, corridor 1 (B at 3591, the dead-end tail) | < 5% | the gate set (big440, door) yes |
| fl05match80 | done 2226-2339 | 856-1016 | 883-1046 +-10% | yes |
| fl05unreach | hover 8638, end 1897 px | hover 458, no second hover, end 141 px | <= 1200, none, <= 128 | end 13 px over |
| splitpatrol separate | station 12000 | 0 | 0 | yes (air click a tick later) |
| mixed2 (11 offsets) | spins 101, illegal 3746, G done 7476 (never 3/11), F hover 56 | spins 0, illegal 0, G done 7386 (never 3/11), F hover 62 | spins down | yes; F hover +11% |

Gates: legion_world_test formation, squadformation, pinwheel, aware, landedflyers and liftflyers are
hash-identical to the step-0 head; stock mixedformation move and fight hash-identical (patrol: same numbers, the B
reference is hashed), max hover 18 / 18 / 0. Retail lines of every fixture are hash-identical.
