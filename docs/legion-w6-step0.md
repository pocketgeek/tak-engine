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
