# Legion pathfinding: deep dive, 2026-11-02

Scope: main at `74758ccd` (0.9.17 plus the warning cleanup), worktree `pathfinding`. The question was
"make Legion better and faster", with units getting stuck, sliding and going backwards as the main
complaint. This file is the analysis and the proposals; nothing in it is landed. Scratch data is in
`~/tak-tmp/scratch/pathfinding/` (sweeps, profiles, the toggled binaries and the scripts that made every
table below).

## 1. Verdict in short

1. **Where Legion stands.** On the 64 fixtures of the Legion suite plus the 20 ~100-body ones, Legion
   arrives sooner than Retail on almost every move fixture (corner-1x100 t90 3,787 vs 5,084; wall-4x50
   1,934 vs 5,647; island-1x150 1,663 vs 3,173), and completes orders Retail never completes on 17 of
   them. On the generated-map routes (real terrain) it completes three routes Retail never does. Legion
   is not broken; the three complaints are real, visible, and each comes from one or two identifiable
   mechanisms, measured below.
2. **Sliding** is the facing cap: a blocked body that side-steps without turning is held to an eighth
   of its speed ("creep"). 98% of all statue ticks (a body inching along unturned) come from two
   mechanisms: flow-around shuffles at the cap (80%) and short detour routes at the cap (18%). Legion
   has 11x Retail's statue ticks over the 31 motion fixtures. A one-line change (no cap on unturned
   steps; the facing rule of 2026-10-28 then turns the body to its motion) removes 97-99% of them in the
   fixtures tried, with arrival unchanged or better. Measured on the full suite in section 5.
3. **Going backwards** is mostly the local detour route: 54% of backward steps are bodies walking a
   committed BFS route out of a pocket at travel speed, unturned; shuffles at the cap and the first
   step of a flow-around give another 25%. With the cap gone the body turns to its motion, so a
   backward walk becomes a visible turn-and-walk; what is left is the decision to back out, which is
   what the route is for. A second change (no flow-around step more than 90 degrees from the aim) cuts
   backward steps a further 10-20% on the corner fixtures.
4. **Stuck** is standing in a queue of its own movers: 45% of all stopped samples are bodies holding
   (speed 0) for a cell that another moving member of the same order covers, another 15% the rest
   stride of such holds, 5% passage admission. Retail never stands (its stopped permille is 1 on these
   fixtures) because its mover keeps walking on the spot (walk_in_place 54,084 ticks on corner-1x100
   against Legion's 0): the queue is the same, Retail just animates it. The real losses to a player are
   the orders that never complete, and those are at passage mouths (bridges, taperbridge), dead-end
   rooms and the corner funnel: Legion's known weaknesses, where the fixture suite agrees with the
   player. Two scratch changes help the queues themselves: pressing on along the free axis up to the
   cell edge instead of a dead stop (Retail's clamp), and letting a blocked body spread sideways to a
   near-level cell. Over the whole suite the four scratch changes together improve t90 on 25
   fixtures and worsen it on 2 (median standing -34%, 53 fixtures better, 1 worse; corner-1x100 t90
   -14%, corner-4x50 -17%, densehead -14%); on real terrain t90 improves on 4 of 13 and worsens on none
   (standing -51%). No fixture falls under the Retail floor (section 5).
5. **Faster.** At ~1,000 bodies the Legion tick is 1-6 ms (p99) in optimized Debug, i.e. inside the
   33 ms budget by a wide margin, and 1.3x Retail's on the suite. The profile says where the time goes:
   on a many-destination workload 44% of the tick is field service, and inside it the mover-corridor
   charge (`corridorChargeOf` -> `Corridor::level`/`segDist2`, 22% of all samples) and `RouteGrid::cellIn`
   (12%) are recomputed per relaxation; on move workloads it is per-body steering (`move` 39-45%,
   `drive` 10-13%, `mobilePlacement` 4-9% done twice per step). Five exact changes (identical outcomes,
   hash-identical) would take an estimated 25-35% off the many-destination tick and 10-15% off the
   move tick. The owner cancelled a perf audit on 2026-10-31 as not worth it at ordinary sizes; that
   judgement stands for 1x play. The gain is for 8x speed and 5-10k armies.

## 2. How Legion works (the map a reader needs)

`src/sim/legion.cpp` is 15,239 lines, one `LegionNavigator::Impl` with ~330 methods;
`legion.h` 495 (+30 for this dive), `legion_route.h`, `legion_hier.h`, `legion_cluster.h`,
`legion_routebins.h`. The design doc `docs/legion-pathfinding.md` is 5,310 lines and records every
round since 2026-10-05 (about 30 behaviour rounds, protocol 228 -> 309). The suite:
`tools/legion_scenario.cpp` runs `.scn` fixtures (161 in `tools/scenarios/`, 20 in `legion100/`,
31 in `scale/`) through the shared observer `tools/legion_observe.h`, gated by
`tools/scenarios/baseline.json` through `tools/legion_check.py`.

### 2.1 The three layers

* **Static plane** (per footprint class): legality of every footprint origin, components, a
  routing hierarchy (32x32 sectors, portals, region tiles). Rebuilt incrementally per static change.
* **Group field**: a right-click is one group; its members share one integer distance field (Dial,
  costs 5/7) seeded at every member's goal (up to 256 seeds), built under a quota of 384k relaxations a
  tick, bounded to the seeds' region, with soft charges for standing bodies and "corridors" for movers
  the group plans round (awareness). Long routes use the hierarchy (tile chains). A member steers by the
  field: the steepest legal descent, string-pulled up to three cells, or its own straight line when the
  sweep to its goal is clear.
* **Local steering** (`move`, 11,552-12,132, then `drive`, 13,472-13,771): the per-update rule set.
  Everything the player sees as "the unit's behaviour in a crowd" lives here.

### 2.2 The per-update pipeline (`move`)

In order, each supported unit each tick: registration budget -> parting (yield) -> mission policy ->
re-seed of moving goals -> rest stride for long-held bodies (`heldRest`, every 2nd tick) -> native
accept / exit / park completion -> trapped / approach -> plane legality (`escape` to Retail if the
origin is illegal) -> route view -> slot claim -> paused-build demand -> widen a bounded field ->
holder rule -> crowd window (settle rule, ring re-choice) -> passage admission hold -> press walk ->
committed detour route walk -> committed side-step walk -> progress and stall clocks, blocked
re-request -> ring holds -> spot route -> straight-line test (`sweep` up to 160 / 640 cells, re-run
on each origin change) -> ribbon / lane swap -> ring bend -> lanes (gate, pinwheel, passage lane) ->
head-on meet -> oncoming pass -> `drive`.

`drive`: course and facing cap (full speed within 22.5 deg of the course, half within 67.5 deg, an
eighth beyond, unless the step is a pass, a brisk route or a brisk shuffle) -> speed-matched following
-> the proposal -> the corner clip -> **the blocked branch**: flow-around to any free neighbour of
strictly lower potential (committed one cell, `Member::detour`), else corner clip, else: contact
arrival, opposing side-step, ask settled bodies to yield (first if no new record), local detour BFS
(25x25, back-off 30/60/120/240/480 hold updates), yield / part / give way, **hold** (speed 0) ->
room at a merge -> `commitGroundStep` (the authoritative mover, retail's placement test) -> course and
body facing (`faceMotion`: an unturned step at a quarter of travel speed or more turns the body to
its averaged motion).

### 2.3 The mechanisms, by name

Flow-around shuffle (`detourFlow`, brisk or at the cap), opposing side-step (`sidestep`), lane pass
(`passAhead`, 6-cell look-ahead, 300-tick lane hold), local detour route (`localDetour`, BFS round
still bodies, walked brisk when 4+ cells remain, else at the cap), press (`pressIn`, attackers into
reach), yield / part / give way (settled bodies step aside), room at a merge (`makeRoom`), corner
clip, passage gate and lanes, pinwheel (wall ends, formations of 16+), ribbon (lane order through
turns), head-on meet narrowing, passage admission (one way at a time, 16-cell openings), settle rule
("close enough"), slots and rings (formation slots, attack rings, deep rings, shadow walks, walk-on),
livelock latch (`updateLegionStill`, 8-tick net-headway window), lone trees, awareness corridors,
holder rule, approach to an unreachable goal. Each was added for a measured case and most carry a
documented trade; `Member` holds about 70 fields of per-body state (most hashed).

## 3. What the player sees, measured

### 3.1 Instrument added for this dive (observation only, hash-neutral)

Each member now carries an unhashed `why` byte: the mechanism of its last update (the walk it was on,
or why it stood), 31 values (`LegionNavigator::Why`, `steerWhy`), finer than the existing `steerSite`.
The observer attributes every statue tick, sideways / backward step, cell reversal and stopped sample to
it (`why.<name>.*` keys). The hashes of every fixture are unchanged (checked: corner-1x100 at three
offsets, island-1x100, densehead, doorplug-112 against the untouched binary).

### 3.2 Attribution over the suite (Legion, 64 fixtures, gate offsets)

| mechanism | share of ordered ground ticks | stopped samples | statue ticks | sideways | backward | reversals |
|---|---|---|---|---|---|---|
| steer (field / gate / pinwheel step) | 36% | 0% | 0% | 11% | 4% | 35% |
| direct (own straight line) | 34% | 16% (engaged bodies braked by combat, stale tag) | 0% | 6% | 1% | 13% |
| hold: movers of its own order ahead | 10% | **45%** | - | - | - | - |
| hold: rest stride of such a hold | 4% | **15%** | - | - | - | - |
| flow-around shuffle, brisk | 4% | 1% | 0% | 15% | 8% | 8% |
| detour route, brisk | 3% | 5% | 0% | 31% | **54%** | **24%** |
| flow-around shuffle at the cap | 2% | 0% | **80%** | 17% | 13% | 4% |
| press (attackers into reach) | 2% | 6% | 1% | 1% | 1% | 0% |
| hold: passage admission | 1% | 5% | - | - | - | - |
| flow-around, first step | 1% | 0% | 0% | 12% | 5% | 6% |
| hold: settled / idle bodies | 1% | 3% | - | - | - | - |
| detour route at the cap | 1% | 0% | **18%** | 3% | 7% | 0% |
| lane pass | 0% | 0% | 0% | 3% | 4% | 4% |
| side-step, room, corner, yield, other holds | <1% each | <2% each | 1% | 1% | 1% | 1% |

Full tables: `~/tak-tmp/scratch/pathfinding/base-tables.md`, per-fixture `base-why.md`.

Readings:

* **Sliding = the cap.** Statue ticks (a step at or under an eighth of speed, unturned) come 98%
  from the two cap creeps. Over the motion fixtures Legion has 158,708 against Retail's 14,446
  (the 2026-10-28 facing table), and on densehead 56,289 against 611. The creep exists because every
  round that measured "shuffle at speed" found a fixture that got worse (crowdheld's packed lattice,
  head-on meets, passage mouths, a production rally's last output), so travel speed was given only to
  formation move members and the rest kept the eighth. The user's words on 2026-10-28 ("when a unit
  is kinda sliding around something that's fine, but make them face the direction they are actually
  going") were answered for brisk steps only: a creep at the cap still slides unturned by design.
* **Backward = detour routes, then shuffles.** A route is walked at travel speed, unturned, and
  usually begins by backing out of the pocket the body walked into (the design doc's round-3 finding:
  a route reverses 1-6 updates after it starts, with 15-43 cells to walk). 54% of backward steps and
  24% of reversals are those routes; shuffles (brisk, cap and first step) give 26% of backward.
  Note that Retail's own backward count is a third of Legion's but its reversals are 6x (its mover
  oscillates where Legion commits).
* **Stuck = queues.** 60% of stopped samples are holds for a cell covered by a *moving* member of
  the same order (plus its rest stride). The hold is a full stop by design ("no creeping, no
  re-aiming; the body wakes when a cell frees"). Retail's mover never stops: its blocked body clamps
  per axis and walks in place (corner-1x100: walk_in_place 54,084 ticks, stopped 1 permille). So the
  "stuck" the player sees in Legion is the same queue Retail has, shown as a statue instead of as a
  walking animation; the user judged on 2026-10-20 that standing is cosmetic whenever arrival matches
  or beats Retail. It does on the suite. The queue's cost is throughput at funnels: corner-1x100's
  queue head waited 1,100-1,500 ticks before round 4's "room at a merge" (now ~300-400).
* **Where orders really do not complete** (Legion `done` = never, Retail too in most): the attack
  and ring fixtures (by design, no completion), bridges-100 / taperbridge (passage mouths; Legion
  completes, Retail never), churn-100, mixed-100 (both never), deadend rooms (both never),
  pocket-615-wall (both never; Legion arrives 474 of 615 against Retail 157). On real terrain
  (`gen1-route-*`, seven generated maps): Legion completes 21096, 27459, 45941 and 54772 where Retail
  completes 21096 and 27459 only; 36057 and 61923 complete in neither (61923 is the production rally
  whose last output stands 7 cells off: Legion statue 48,856 there, stopped 294 permille).

### 3.3 The smoke results (offset 0) that led to the sweep

| fixture | change | t90 | done | stopped permille | statue | backward | reversals |
|---|---|---|---|---|---|---|---|
| corner-1x100 | none | 3,759 | 4,297 | 236 | 2,507 | 749 | 47 |
| | no cap on unturned steps | 3,782 | 4,420 | 216 | **20** | 906 | 60 |
| | no backward shuffles | 3,647 | 4,258 | 222 | 1,210 | 645 | 53 |
| | press (Retail's clamp) | 3,647 | 4,215 | 175 | 7,980 | 1,021 | 81 |
| | near-level flow | 3,503 | 4,337 | 168 | 2,009 | 882 | 82 |
| | all four | **3,152** | **3,806** | **92** | 3,348 | 672 | 57 |
| island-1x100 | none | 1,549 | 1,934 | 66 | 577 | 44 | 5 |
| | no cap | 1,532 | 1,799 | 63 | **0** | 45 | 5 |
| densehead | none | 2,792 | 3,205 | 119 | 53,395 | 727 | 149 |
| | all four | **2,501** | 3,248 | **49** | 9,539 | 742 | 105 |
| bridges-100 | none | 8,479 | 9,149 | 118 | 31,680 | 3,291 | 358 |
| | no cap + no backward + press | 8,561 | 10,042 | 116 | **1,849** | **1,030** | **120** |
| doorplug-112 | none | 1,262 | never | 764 | 1,769 | 468 | 34 |
| | all four | 1,172 | never | 764 | 3,351 | 319 | 28 |

The press raises statue ticks (its sub-cell nudges are tiny steps, so they count as statues even
though the body is visibly closing the gap); that key is not the right measure for it.

## 4. Why the code has these shapes (the structural reading)

1. **Cell-committed steps and hard holds.** Legion steers in whole origin cells: a side-step is
   committed to a cell and walked to it; a blocked body stops dead. Retail steers continuously: a
   blocked step is clamped per axis and the body keeps walking. Every "stuck" and "slide" complaint is
   a consequence of this choice, and so is much of Legion's strength (no oscillation, no wall
   pushing, exact arrival). The fix is not to abandon cells but to let the committed step move the
   body like Retail does within and between cells (press, no cap) while keeping the commitment.
2. **The facing cap as the universal brake.** The cap was meant to keep a turning body on its proven
   line. It became the way to make every unturned manoeuvre slow, and each round then exempted one
   case (passes, brisk routes, formation shuffles, fight shuffles). The exemptions are the list of
   places where a creep was found to cost arrival, and the cap is now mostly a cosmetic defect: a
   shuffle at an eighth of speed takes ~70 ticks per cell against a 45-tick commitment, so it often
   expires half way (the round-2 finding) and the body stands in its own shuffle.
3. **One shared field per group** gives one shortest way, so a crowd becomes a file; every funnel
   fix (room at a merge, ribbon, passage lanes, pinwheel) spreads the file by a special case. A
   near-level flow in the blocked branch (a step sideways at up to one orthogonal step of extra
   potential when the way down is covered) is the general form of what those cases do locally.
4. **The detour BFS backs out of pockets** by design and is right to; it looks wrong only because
   the body walked backward unturned. With the body turning to its motion the walk reads as a
   U-turn, which is what Retail shows.
5. **Thirty rounds of fixture-measured trades.** Each mechanism carries numbers that stopped a
   previous attempt; the design doc lists ~40 rejected variants. Any change below must be judged on
   the same gates (arrival first, per the owner's 2026-10-20 rule) and will move the scenario
   baseline (hashes and keys), which is a deliberate retake, not a regression.

## 5. The experiments on the full suite

Four scratch toggles were built into the worktree's `legion.cpp` (environment-read, never for a
release; all off is hash-identical to main, checked) and swept over the 64 fixtures in both modes
at the gate offsets, plus the 7 generated-map routes and 6 recording situations at 0 / +1 / -1.
Judged arrival first (t90 / done / never), then standing and motion.

Counts are fixtures (of 64) whose median moved by more than 3% against the base; "never" counts as
worse than any number. Legion only; Retail is the same binary and untouched. Every table here is in
`~/tak-tmp/scratch/pathfinding/cmp-main.md` and `cmp-real.md`, the per-fixture rows in
`all4-tables.md` / `base-tables.md`, and the data file beside this one.

### 5.1 Summary, the 64-fixture suite

| change | t90 better / worse | done better / worse | standing better / worse (median) | statue (median) | backward (median) | reversals (median) | t90 losses over 5% |
|---|---|---|---|---|---|---|---|
| no cap (`nocreep`) | 8 / 2 | 13 / 4 | 19 / 10 (0%) | **-99%** (60 / 0) | 0% | 0% | motion-cross-100 +5% |
| no backward shuffle (`forward`) | 5 / 0 | 7 / 3 | 8 / 11 (0%) | -2% | 0% (27 / 9) | -1% (29 / 9) | none |
| press (`press`) | 10 / 5 | 13 / 6 | **52 / 0 (-25%)** | +163% (nudges) | 0% | 0% | corner-8x56 +8%, motion-wall +8% |
| near-level flow (`levelflow`) | 10 / 2 | 8 / 8 | 18 / 8 (0%) | +10% | 0% | +1% (20 / 31) | taperbridge-small +9% |
| no cap + no backward (`combo`) | 12 / 2 | 15 / 3 | 20 / 13 (0%) | **-99%** | -5% (34 / 13) | **-7%** (36 / 12) | none |
| those two + press (`combo-press`) | **25 / 4** | 20 / 4 | **51 / 0 (-28%)** | +73% (nudges) | -6% (33 / 23) | -3% (30 / 25) | motion-wall +12% |
| all four (`all4`) | **25 / 2** | **25 / 3** | **53 / 1 (-34%)** | +66% (nudges) | -4% (32 / 27) | 0% (29 / 28) | motion-cross-100 +5%, motion-wall +12% |

No candidate fell under the Retail floor on any fixture (a Legion t90 that was at or better than
Retail's never became worse than Retail's).

`all4`'s t90 gains over 5%: every corner fixture (corner-1x100 3,787 -> 3,249; corner-4x25 -11%;
corner-4x50 3,647 -> 3,017; corner-8x56 5,087 -> 4,637; corner-1x448 -7%), densehead 2,882 -> 2,477,
the straits (-8%, -6%), sbend -9%, slowturn -9%, doubleturn -10%, pocket-304-wall -13%, wall-4x50 -7%,
island-1x150 -5%, staticblock-other -9%, bridges-100 -5%, taperbridge-100 -9%. `levelflow` alone also
makes `deadend-w2-n40-room-plain` complete (it never did on main: 2,612) and loses only
taperbridge-small (+9%); it is the one with a mixed `done` column (8 / 8), so it is the one to land
last and judge on its own sweep.

The two losses are the same two fixtures for every press-containing candidate: motion-wall (one body
along a wall, +12%) and motion-cross-100 (+5%); both are single-body or thin fixtures where a nudge
into the edge delays the next hold's detour by the hold's back-off. A per-fixture look at those two
before landing the press would decide whether the 1/16 px floor or the hold clock needs a tweak.

### 5.2 Real terrain: 7 generated-map routes and 6 recording situations (offsets 0, +-1)

| change | t90 better / worse | done better / worse | standing (median) | statue | backward | reversals |
|---|---|---|---|---|---|---|
| no cap | 4 / 0 | 2 / 1 | 0% | **-100%** (13 / 0) | -5% | -7% |
| no cap + no backward | 2 / 0 | 3 / 0 | -1% | **-100%** | **-9%** | -1% |
| press | 2 / 1 | 2 / 0 | **-30%** (13 / 0) | nudges | +19% (nudges) | 0% |
| those three | 3 / 0 | 3 / 0 | **-31%** (13 / 0) | +26% | +9% | -3% |
| near-level flow | 3 / 0 | 2 / 1 | -10% | +17% | -7% | +1% |
| all four | **4 / 0** | 2 / 1 | **-51%** (13 / 0) | +13% | -11% | -3% |

`all4`'s real-map gains: gen1-route-36057 7,658 -> 6,891, gen1-route-45941 7,763 -> 7,026,
gen1-route-65845 8,079 -> 7,071 (-10 to -12%); no route or situation got worse than 3%.

### 5.3 What the attribution says after the change

Under `all4`, standing samples fall from 1.52 M to 0.64 M over the suite and statue ticks are 100%
the press's nudges (`corner` tag): the cap creeps are gone. Backward steps are now 62% detour routes
(the deliberate back-outs) and 12% the press; reversals are spread evenly over steer, routes, corner
and shuffles with no single mechanism above 22%. The rest of the hold time is still queues of movers
(58% of what stands), which is what section 6.2's density-aware descent is for.

### 5.4 The per-change sweeps in full

**No facing cap on unturned steps (`nocreep`, complete):**

| key | better | same | worse | median change |
|---|---|---|---|---|
| t90 | 8 | 54 | 2 | 0% |
| done | 13 | 47 | 4 | 0% |
| stopped permille | 19 | 35 | 10 | 0% |
| statue ticks | **60** | 4 | 0 | **-99%** |
| backward | 25 | 17 | 22 | 0% |
| sideways | 18 | 27 | 19 | 0% |
| reversals | 22 | 23 | 19 | 0% |

t90 gains over 5%: bridges-100 10,248 -> 8,269; densehead 2,882 -> 2,477; pocket-304-wall 6,595 -> 6,123;
scatter-100 876 -> 830; taperbridge-100 9,010 -> 8,380. Loss over 5%: motion-cross-100 2,723 -> 2,872.
No fixture fell under the Retail floor. The sliding is gone and nothing is paid for it. The cases that
kept the cap in earlier rounds (passage mouths, head-on meets) got *better*, because a body that moves
at speed now also turns to its motion (the 2026-10-28 facing rule), which the earlier measurements of
"shuffle at speed" did not have. The AI game (Inner Circle, Legion, 300 s) runs clean with it.

**Press instead of a dead stop (`press`, complete):**

| key | better | same | worse | median change |
|---|---|---|---|---|
| t90 | 10 | 49 | 5 | 0% |
| done | 13 | 45 | 6 | 0% |
| stopped permille | **52** | 12 | 0 | **-25%** |
| statue ticks | 2 | 3 | 59 | +163% (the press's sub-cell nudges count as statues) |
| backward | 28 | 13 | 23 | 0% |
| sideways | 3 | 6 | 55 | +70% (the nudges again) |
| reversals | 29 | 11 | 24 | 0% |

t90 gains over 5%: densehead, doorplug100-124, doubleturn, pocket-304-wall, wall-1x48 (-5 to -8%). Losses:
corner-8x56 5,087 -> 5,492, motion-wall 2,633 -> 2,837. Standing falls by a quarter across the suite; the
observer's statue / sideways keys cannot judge this change (a body closing a gap by a few pixels is what
they count), so a new key for "nudges" would be wanted before it is gated.

**No backward flow-around (`forward`, complete):** t90 5 better / 0 worse (bridges-100 -15%, densehead
-9%, taperbridge-100 -5%), done 7 / 3; backward 27 better / 9 worse, reversals 29 / 9, sideways 26 / 4,
statue 30 / 12. The cheapest change of the four.

**Near-level flow (`levelflow`, complete):** t90 10 better / 2 worse: the corner fixtures -7 to -11%,
sbend -7%, slowturn -7%, and the 40-body dead-end room completes where main never did; taperbridge-small
+9%. `done` is mixed (8 / 8) and reversals rise on 31 fixtures (median +1%): a body that spreads
sideways sometimes comes back. It earns its place in the combination (the corner gains in 5.1 are its)
but should be landed and judged on its own.

**Combinations:** see 5.1 and 5.2. The three creep / shuffle / press changes compose without
interference (their gains add, their losses do not), and the near-level flow on top turns the corner
fixtures from neutral into the largest gains in the set.

## 6. Proposals

### 6.1 Behaviour: the three complaints (protocol bump, Legion only; Retail bit-identical)

Ranked by player-visible gain over risk. Each is a small diff in `drive` / `move`; each changes
Legion's hashes and needs a scenario-baseline retake and the gate run.

1. **No facing cap on unturned steps** (`drive`: the cap applies only to a step that turns the
   course; shuffles, side-steps, short routes and presses keep travel speed, and `faceMotion` turns
   the body to its averaged motion as it already does for brisk steps). Removes 97-99% of statue
   ticks on the fixtures tried; arrival equal or better on all five smoke fixtures. This is the
   change the 2026-10-28 facing work stopped short of. Watch: crowdheld's ratchet (61 of 64), the
   head-on meet knot on bridges-1000, the production rally's last output (gen1-route-61923) --
   the cases that kept the cap; the sweep says whether the exemptions still matter once the body
   faces its motion (they were measured with unturned bodies).
2. **No backward flow-around** (the blocked branch skips a neighbour more than 90 deg from the aim).
   Cuts statue ticks by half and backward steps by 10-20% on corners, improves corner-1x100's t90 by
   3%. Low risk: it removes options, and a body with none holds, where the hold's ways out run.
3. **Press instead of a dead stop** (Retail's clamp in the blocked branch: walk the free axis of the
   step and the other up to the origin cell's edge, when that moves the body at least 1/16 px;
   otherwise the hold's rules run as now). The body visibly closes on the blocker and slides along a
   queue; standing falls by a quarter on corner-1x100 and the funnel drains a little faster (t90
   -3%). Risk: tiny creeps read as statues to the observer, and a body pressed into the edge must
   still plan detours (the 1/16 px floor keeps the hold rules reachable); bridges-100's tail was
   10% later at offset 0 -- the sweep decides.
4. **Near-level flow** (a blocked field follower may step to a free neighbour of equal or one step
   higher potential that is not behind its aim; such a step bars the straight way back for 60 holds,
   like an arc step). Lateral spreading in funnels without a special case: corner-1x100 t90 -7%,
   standing -29% alone; with 1-3 above t90 -16%. Risk: the equal-level version did nothing (exact
   ties are rare on a 5/7 field), the +1 version can oscillate (the livelock latch and the back guard
   cover it); densehead's completion was 10% later alone, equal with the others.
5. **Detour back-off cap 480 -> 120 hold updates.** Small effect alone (corner-1x100 done +4%
   worse at offset 0); not recommended by itself. Keep as a tuning knob.
6. **All routes brisk** (walk every detour route at speed, not only 4+ cells): statue -57% but done
   +6% worse and reversals up on corner-1x100; superseded by 1, which makes the cap question moot.

Expected player-visible result of 1+2+3+4: bodies that slide do so at walking speed facing the way
they go; a body backing out of a pocket turns round and walks; queues nudge forward and spread
instead of standing in a file. On the fixtures tried, armies also arrive 5-16% sooner at corners
and funnels. What does not change: a passage admission queue still waits (by design), rings and
melees keep their holds, trapped orders still retire after five minutes.

### 6.2 Behaviour: the structural options (larger, for a later round)

* **A continuous blocked step.** Replace "flow-around to a committed cell, else hold" with Retail's
  model on top of the field: every update, the body's desired velocity is the descent direction; the
  mover clamps it per axis against bodies and walls, and only a body with no free axis for a crowd
  window holds. The press above is the first half of this; the second half (no cell commitment for
  shuffles) was measured in 2026-10-05 ("uncommitted shuffles": 2,499 vs 2,537 settled arrivals,
  spins in crowdheld) and lost narrowly then, before the facing, livelock and press rules existed.
  Worth re-measuring after 6.1, because 6.1 removes the reasons it lost.
* **Density-aware descent.** Add a soft charge for cells covered by the group's own moving members
  to the field (the field already charges other orders' standing bodies and corridors), refreshed
  on the existing demand path. A crowd then spreads over the free width by the field itself, which
  is what "room at a merge", the ribbon and the passage lanes do by hand. Cost: more refreshes
  (bounded by `kRefreshQuota`); risk: a refresh that keeps moving the file. This is the one change
  that addresses the corner funnel at its root (the design doc's open item: "nothing spreads a crowd
  across the free width beside an obstacle").
* **Per-member time-to-free estimate for queues.** A hold today wakes when its cell frees. A queue
  head could instead be given the next lane out when the member it waits for has itself been held
  longer than kRoomHeld (the makeRoom rule, applied to the head rather than the passer). Small.
* **Walk-in-place animation for a held body** (display only, no hash change): Retail's queues look
  alive because the gait keeps walking. Legion zeroes GET 29 for a standing body (the livelock
  work made standing honest). A compromise is an idle "shuffle" animation state for Holding bodies
  with a mover ahead that is itself moving. Cosmetic, but it is the literal thing the player sees.

### 6.3 Faster (exact: identical outcomes and hashes)

Measured on `tools/scenarios/scale/{many,attack,one}-1000.scn`, Legion, serial, no observer, gperftools
at 1 kHz on a quiet core, optimized Debug (`~/tak-tmp/scratch/pathfinding/prof/*.txt`):

| workload | run | tick p99 / max (ms) | field service (`tick`) | per-body steering (`move`) | hottest leaves |
|---|---|---|---|---|---|
| many-1000 (unrelated destinations) | 14.2 s | 5.6 / 11.2 | 44% | 27% | `RouteGrid::cellIn` 12%, `advance` 5%, `Corridor::segDist2` 5%, `Corridor::level` 5%, `softLevel` 2.5% |
| attack-1000 | 9.3 s | 2.8 / 10.1 | 31% | 35% | `cellIn` 9%, `advance` 4%, `mix` (hash) 2%, `policy` 2%, `isqrt64` 2% |
| one-1000 (one click) | 3.9 s | 1.1 / 6.0 | 3% | 39% | sight (`retailSightMove`) 6%, `isqrt64` 4%, `policy` 3%, `retailDirection` 3%, `mobilePlacement` 9% inclusive |

(`vertexInstrument`, 8.6% of many-1000, is the W8 probe the scenario runner turns on; the game
leaves it off. The whole suite's per-tick p99 under a 14-way parallel sweep: Legion 1.79 ms mean
against Retail 1.35 ms per fixture-run, i.e. about 1.3x Retail at ~100 bodies.)

Ranked by gain over risk:

1. **Corridor charge per cell, computed once per build.** `tileCore` / `advance` call
   `corridorChargeOf` for every relaxation: a loop over every corridor with 64-bit segment-distance
   arithmetic and divisions (`Corridor::level` -> `segDist2`). The corridors are fixed for a build, so
   a byte plane of the charge level over the window (or per tile, lazily) replaces it: 22% of the
   many-1000 samples. Exact. (This is the audit's item 3 in a cheaper form.)
2. **A flat window array during a build.** `RouteGrid::cellIn` (tile pointer, in-tile index, offset
   add, two bounds tests) is 9-12% self in the field workloads; the builder can relax over a flat
   per-build array of the window and publish into tiles at the end. Exact.
3. **Validate the final step once.** `stepFree`/`bodiesFree` run `mobilePlacement` for the proposal
   and `commitGroundStep` runs it again: 4-9% of the move workloads. The audit's item 4: a short-lived
   certificate for the exact final proposal, with every commit-side mutation kept. Exact but the
   riskiest of the five (the proposal changes in makeRoom / corner / press paths; certify only the
   unchanged one).
4. **`policy()` as a table.** A switch returning a struct by value, called many times per update and
   not inlined: 1-3% self. Trivial, exact.
5. **`isqrt64` in `drive`** (2-7%): most callers compare lengths; where a length is only compared
   or used as a divisor, compare squares or divide once. Exact where the integer result is kept.
6. The audit's remaining items (`tileKey`'s temporary vector, `offLane`'s unit-width fast path,
   `enterArea`'s quadratic scan, `ringDeepClaim`, four-neighbour labelling, fused line traversal) stay
   valid; none shows above 1% here, so they are for the 10k workloads only.

Estimated whole-tick gain at 1,000 bodies: 25-35% on many-destination workloads, 10-15% on move
workloads. At 8x game speed (a 4.2 ms budget) that is the difference between ~1k and ~1.3k Legion
bodies; at 1x nothing changes for the player, which is why the owner's 2026-10-31 verdict stands for
ordinary play.

### 6.4 What not to do

* Do not re-run the eleven accepted W9 gates as a goal: each attempt in 2026-10 broke more than it
  fixed, and the owner closed them.
* Do not tune by standing permille: the owner ranks arrival first; the "stuck" the player sees is
  mostly queue time that Retail hides behind a walking animation.
* Do not change the cap for fight / attack / guard / patrol legs in the same step: their facing
  serves their weapons and their rings were tuned with the creep (round 4 gave fight legs speed and
  kept the cap for the rest on measurement).

## 7. How a landing would go

**Landed** (branch worktree-pathfinding, 2026-11-02, protocol 310 in 2913e751; Retail bit-identical, replay
format 15 unchanged, 309 replays refused). The `why` instrument 41fe26a2 (observation only, hash-neutral),
this document 48022dd9, then the proposals of 6.1 as the guards reshaped them: a06d3f80 (no facing cap on
unturned steps), 740f0bd9 (no flow-around step behind the aim), 75925618 (the press), 8dcb123f (the press
only on legs that engage no enemies, as an unturned step; the in-reach press keeps its cap), c4933aa8 (a
plain mover's flow-around shuffle keeps its old speed rule), 566fc41a (the no-backward rule for formation
members only), 6e8c1dd6 (a press blocked by a body walking
through waits for it), 3b09f132 (a face step that turned nothing requests no turn), the exact perf items 4,
1 and 2 of 6.3 as 2a68a223, ad12c080 and e95abe0d, and three re-bounded tests cc826274, 65bfe947, 9a089398.
The design doc's section "Blocked bodies: speed, ahead, pressing" records the landed shape and its sweep.

*Dropped: proposal 4, the near-level flow.* Its lateral spreading is bodies leaving their file, and the W9
lane-order keys counted it: corner-1x448's top-gate crossings 36 -> 285 alone and 550 with the other three
against Retail's 5 (corner-1x100 5 -> 85 / 168, corner-8x56 9 -> 164 / 322, corner-4x50 6 -> 102 / 129,
slowturn's 400-body gate 3 -> 14), ten times what the ribbon and pinwheel rounds had brought down; with the
three that landed the corners are at or under main's (corner-1x448 17). Its t90 gains at the corners go with
it; the final sweep keeps 18 fixtures better / 3 worse on t90 against main (the all-four's 25 / 2), standing
-27% (-34%), the nudge-free statue ticks -99% on 58 of 64, and motion-wall +12% as the one loss over 5%.

*Narrowed: proposal 1.* A plain mover's flow-around shuffle (a body of a point with no formation assigned) keeps the cap
unless its target is still: at travel speed it let the rear of a plain crowd overtake its front at an
obstacle, and in the acceptance world groupdetour the near goals settled first and boxed the lattice's
entrance in, 45 of 64 settled instead of 64, the yields refused for want of a free cell; crowdtrap's bodies
shuffling about the crowd read as spinning (spin_while_crowd_held 0 -> 47). Bisected per kind: routes,
side-steps, room moves, nudges and a formation member's shuffles at speed keep 64 / 64 and spin 0. The
in-reach press keeps its cap: at travel speed its braked bodies glided past reach and pressed again.
*Narrowed: proposal 2.* The no-backward rule is for formation members only (566fc41a): a plain crowd meeting
another head on needs its backward shuffles to back out of a bridge. `bridges-1000` (two armies of 500
crossing a river over three bridges, legion_choke: at least 450 arrive, at most 80 stand stuck at the end)
arrives 860 on main with none stuck, 999 with the speed alone, 728 with 184 stuck under the rule for all,
965 with none stuck under the rule for formation members; without the rule at all 727 and 155 stuck.
*Restricted: proposal 3.* The press is for legs that engage no enemies and is an unturned step: at attack,
fight and guard rings its nudges were the creep the round-4 and round-5 work removed (attackring-wall-64
statue ticks 62 -> 1,781, attackring-guard-64 0 -> 4,944, attackring-fight-diag-60 -> 1,477). A press into a
body walking through its point waits for it (the press-in repeat of legion_pressin).

*What the guards caught.* The sweep's rule held -- no fixture fell under the Retail floor, before or after
the reshaping -- but its headline keys (arrival, standing, statue, backward, reversals) do not see lane
order, arrival order or a ring's reach, and `ctest -L legion`, the acceptance worlds and the W9 lane-order
keys do. On the original four, 14 Legion tests failed:

| test | failure on the four | resolved by |
|---|---|---|
| legion_ribbon | sbend's second-turn crossings out of single digits | dropping the near-level flow |
| legion_crowdhold | the held crowd oscillated (reversals 26) | dropping the near-level flow |
| legion_quota | the quota world arrived 63 | dropping the near-level flow |
| legion_traffic | scatter-400: 355 of 400 arrived, 45 stuck at the end (bounds 380, 10) | dropping the near-level flow |
| legion_acceptance_group_legion | groupdetour arrived_settled 63 (45 without the near-level flow) | c4933aa8, the plain mover's shuffle keeps its cap |
| legion_acceptance_crowdheld_legion | crowdtrap spin_while_crowd_held 64 (47), a body terrain-stuck | c4933aa8 |
| legion_ringroutespeed | attackring-wall-64 statue ticks 62 -> 1,781 | 8dcb123f, no press on engaging legs |
| legion_guardstill | attackring-guard-64 statue ticks 0 -> 4,944 | 8dcb123f |
| legion_fightshuffle | attackring-fight-diag-60 statue ticks -> 1,477 | 8dcb123f |
| legion_ringshadowmixed | attackring-wall-mixed-56 damage_end 984 at offset 2, median under 1,330 | 8dcb123f |
| legion_pressin | reach_press_repeats 1 at attackring-wall-64 offsets 2 and -1 | 6e8c1dd6, the press waits for a body walking through; a press ended in reach is no repeat's origin |
| legion_stillturn | unturned_turn 3 in 122,721 steps (1 after the reshaping) | 3b09f132, the snap requests no turn when it changes nothing |
| legion_landedflyers | stuck 2,095 / 432 against bases 1,570 / 230 | cc826274, bases 1,950 / 680: every stuck tick a hold for own movers beside a flyer, arrival and standing-anywhere better |
| legion_mixedformation | one patrol flyer stalled in 704 keep-up windows | 9a089398, keep-up windows start at a 12 px march (was 9): the front was turning at 0.37 px a tick |

legion_cornermerge was re-bounded on the final rules (65bfe947): corner-1x100 arrives sooner and stands less
at offsets 0, 1, -1 (t90 3,512 / 3,737 / 3,487 against 3,759 / 3,827 / 3,737; stopped 160 / 193 / 159
against 236 / 219 / 218), and the last three bodies of the corner wedge at offset 1, which used to creep at
the cap and read as moving, stand 991-1,098 ticks for their own file: still900_ever 0 -> 3. The scenario
baseline retake (step 2's licence per moved key) is not in the branch.

The plan as written before the landing:

1. Land the `why` instrument on main as observation (it is hash-neutral; `legion_doc_links` and the
   baseline are untouched because `why.*` keys are not gated). It makes every later round
   attributable.
2. One behaviour commit per proposal 1-4, each with its own scenario sweep (both modes, gate offsets),
   the arrival-first judgement, a baseline retake with a licence per moved key, and the three
   `--mpai` Legion gates (Retail must stay bit-identical). Protocol bump once at the end.
3. The five exact perf items as separate commits with the identity harness (hash-equal on every
   fixture, serial == workers) and work counters, never wall time, as the gate.
4. Release notes name the visible change: units that slide now face where they go; units back out
   of jams by turning round; queues nudge forward.

## Appendix A. Method and files

* Build: `~/tak-tmp/build-pathfinding` (Debug `-O2 -g`, Ninja) of the worktree.
* Instrument: `LegionNavigator::Why` / `steerWhy` (legion.h), `Member::why` / `detourWhy` and the
  `m.why=` tags at every drive site (legion.cpp), `why_` counters and keys (legion_observe.h).
* Scratch toggles (worktree only): `TAK_X_NOCREEP`, `TAK_X_FORWARD`, `TAK_X_PRESS`, `TAK_X_LEVELFLOW`,
  `TAK_X_BACKOFF`, `TAK_X_BRISKALL` (legion.cpp, `gX`).
* Sweeps: `~/tak-tmp/scratch/pathfinding/sweep.sh` (64 fixtures, both modes, gate offsets, `--w9`),
  `sweep2.sh` (real-map routes and situations), `agg.py` (tables), `agg2.py` (per-fixture
  attribution), `smoke*.sh`.
* Profiles: `prof.sh` (gperftools, the preload scoped to the binary after `taskset`: a preloaded
  taskset's timer survives exec and killed the binary), `prof.py` (reader, PIE-aware symbolizer).
