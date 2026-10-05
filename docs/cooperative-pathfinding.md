# Cooperative pathfinding

Cooperative is an experimental navigation option for skirmish and
multiplayer creation. Retail remains the default. The host chooses the mode;
the lobby reports it, every participant uses it, and the replay records it.
Campaigns continue to use Retail. Version 0.7.24 uses protocol 227 and displays
Retail, Retail+, Flowfield, Cooperative in that order. The stored mode values
remain Retail=0, Flowfield=1, Cooperative=2, Retail+=3. Earlier recordings need
their original engine version.

This backend combines the existing shared terrain planner with separate group
and traffic policies. It is not a claim of a universally better or optimal
pathfinding algorithm. Retail does not allocate the new coordinator. Existing
Flowfield sampling, traffic decisions and scheduling remain separate.

## Terrain routes and group movement

The shared planner builds immutable, footprint-aware terrain tiles and a graph
of their actual connected components. Groups reuse destinations and local
integration fields. Water depth, slopes, placement restrictions, known static
obstacles and exploration continue to determine legal terrain. It does not
treat a tile containing a passage as entirely traversable.

Cooperative prefers an initial group heading and individual lateral lanes where
the field permits them. When recovering lateral spacing on a predominantly cardinal route, lane
alignment uses exact integer comparisons so a helpful diagonal is not rounded
into a tie with a forward step. Other route choices keep the existing scoring. Lane restoration can choose a slightly longer route;
it still decreases the local field potential, and boundary transitions retain
the planner's decreasing component distance. When the route leads sideways or
away from the group's final heading, lane restoration gives way to the terrain
route. Solo units need no formation lane. Terrain-proved corners are retained
instead of using Retail's opportunistic corner pruning.
If a traffic detour physically reaches the following intermediate corner, the
route cursor advances to it. This avoids the native acceleration controller
braking forever on an obsolete out-and-back segment. It does not skip mission
goals or queued commands, move the body, or cut across terrain.

Move, fight-move, patrol, guard and production rallies keep their owning orders.
The shared arrival policy assigns collision-checked standing places within a
finite destination area. It also retains factory exit requirements and handles
mobile producers. Explicit formations retain their slowest member's speed cap.
Flying units keep the engine's existing flight movement; terrain corridors and
passage permits apply to ground and surface-water movement.

## Traffic and narrow passages

The new coordinator reserves short swept-footprint segments, retaining a local
detour across ticks instead of picking a different side each frame. Local
search checks diagonal side footprints and a terrain connection back to the
installed route for forward detours. Passing shoulders are legal short moves
aside; right is preferred, with a proved left exit available when that search
finds no right exit. Their standing endpoint must clear the blocking ally's
projected lane through the local window, including for larger bodies; moving
backward into that same lane is insufficient. A finite overlap calculation
projects the ally's next swept footprint only far enough to pass all candidate
footprints in the search window. Diagonal sweeps remain strips, rather than
one large bounding rectangle. Physical placement still checks each actual
movement step.
After reaching a passing shoulder, the mover holds it for a bounded 120 ticks,
including when the immediate obstruction disappears, before resuming its route.

After 180 ticks without changing its footprint anchor, a same-direction mover
may use a proved lane-clearing shoulder when the same bounded search finds no
connected forward exit. This allows a stationary queue to make room without
replacing a usable terrain route or launching a second search. This fallback
requires an active peer: an enclosed mover waits for parked bodies to move,
while any proved connected forward bypass remains eligible.

An exhaustive ordinary friendly-blockage failure can defer an identical local
search for eight ticks when its published terrain tile is fresh and open, or
when the blocking peer is idle. Moving blockers on constrained terrain retain
immediate retry eligibility. Own movement, command/controller, aim and relevant peer
changes invalidate the delay at the next admission. Opposing traffic, arrival
placement and registered passages are excluded. Quota exhaustion, visit limits
and truncated search paths remain deferred work, not evidence of failure.
Other bodies, terrain or reservations can change during that delay; they are
rechecked after at most eight ticks plus normal admission latency. The existing
180-tick terrain reanchor policy is unchanged.

On a fresh, open published terrain tile, Cooperative can save one
already-computed native step behind up to four allied moving blockers. Constrained
or stale tiles keep their ordinary traffic movement. An optimistic unexplored
tile is navigation guidance, not permission to bypass live collision checks. At the end of the unit phase, a bounded dependency graph tries
followers after their recorded leaders have actually moved. Current terrain,
endpoint and diagonal-side footprints, reservations and ownership are checked
again before committing. It neither promises a future vacancy nor gives a unit
a second steering update. Closed cycles and failed dependencies wait. Stop,
capture, death, freezing, transport or changed orders invalidate saved steps.

Failed or deferred local work does not authorize crossing a wall or another
unit. Stop, replacement orders, death, transport and ownership changes retire
obsolete coordination state.

An arrival slot's grid proof does not guarantee that a straight steering ray
from an off-centre body will clear the same terrain corner. After a physical
refusal, Cooperative retains the slot and approaches it through short proved
legs. Deferred proof work or a crossing-reservation wait preserves arrival
ownership; loss of the slot cancels those retained legs. Targeted movement
orders also register for passage admission before their first route is delivered,
while their interaction or combat mission keeps control of arrival.

Sparse, resumable terrain probes identify straight cardinal passages up to 16
raw cells wide. Short, fully bounded, centered passing bays can join matching
narrow sections into one passage. Minimum width controls admission; the wider
bay extent controls when a body has actually left. Opposing allied streams take
turns, and a late entrant can retreat from the mouth to leave the outlet clear.
Merged bays span at most 16 raw axial cells. A waiting opposing stream closes
the current admission batch after eight entrants or 120 ticks; the next stream
still waits for admitted bodies to clear the passage. A complete footprint
must leave the retained strip or bay before stale ownership can be released.
Curved and offset passages continue through local traffic coordination; they
are not assumed to be equivalent to the rectangular cases.

The retail registry without overrides has ground movers up to 4-by-4 cells
and surface vessels up to 5-by-5 cells. The detector's 16-cell width ceiling
is not a promise of passage arbitration for wider or modified geometry.

Passage and movement reservations are shared by allies. Enemies remain physical
obstacles and combat targets rather than participants in that traffic policy.
New route requests wait for their clearance and passage checks. Cooperative
does not use the older local-route shortcut that could bypass those checks.
While distant terrain tiles rebuild, a new prefix may proceed if all of its
steps, diagonal sides and open-width screening cells still use current
published tiles. A small per-profile bitmask retains the tiles owned by an
unfinished rebuild; clearing the next dirty list does not make those tiles
current. Narrow-passage admission, blocked-start recovery and terminal firing
failure still require the complete current profile. This avoids stopping an
unrelated open route without granting a passage permit from stale terrain.
Open-width screening reuses an all-positive-cost proof from each immutable
terrain tile. Published and preparing proofs remain separate, and dirty tiles,
footprint halos, boundaries and ray ordering retain their original checks.
Tiles without an all-positive-cost proof use exact cell reads. Proofs are derived during existing tile
publication, with no separate lookup cache or whole-map cell array.
A proved passage also needs space in the gate table before its route can be
published. Capacity rejection leaves the request pending and preserves an
existing lease; it does not permit uncoordinated entry. Requests can reuse an
already registered gate even when the table is full.

## Work and storage limits

The existing 512 MiB navigation admission ceiling includes the new coordinator's
16 MiB reservation, the passage cache's 512 KiB reservation, and a 2 MiB
movement-dependency reservation. These are
budgets, not allocations made when a match starts. No additional whole-map
occupancy or flow grid is allocated for Cooperative.

Arrival tables remain covered by the existing per-request reservation; the
additional 16 MiB covers the coordinator's own records, gates and claim index.

- Shared terrain and field jobs retain deterministic work quotas and workers.
- Traffic keeps at most 16,384 records. Local planning uses a 25-by-25 window,
  at most 384 visits and 48 retained direction steps. At most 64 callers are
  admitted per tick, sharing 8,192 footprint-cell charges and 8,192 reservation
  comparisons. A footprint query is charged its full area; it is not one cell
  of work. When work is exhausted, the next admission window resumes after the
  last caller that consumed work, so repeated windows of expensive callers
  cannot continually skip the same members. Unsupported footprint dimensions
  outside 1 through 64 are rejected before registration or query callbacks;
  old movement claims for such a body are released.
  Reserved steering legs span at most four grid steps; their sparse index has
  at most 8,192 buckets and 32,768 membership links. Passage admission keeps at
  most 4,096 shared gates. Clear travelling units allocate no movement claims.
- Passage discovery allows 4,096 terrain probes per tick, at most 128 per job
  call, and 512 cached entries. Nearby route anchors reuse proved descriptors.
  The adapter's separately counted, bounded transverse screening reads are
  additional to that discovery quota. Pending proofs advance independently of
  route-delivery visits, using only current, fully published terrain profiles;
  stale or unavailable profile generations cannot authorize entry.
- Movement dependencies keep at most 16,384 attempts and 65,536 edges, with
  65,536 weighted footprint-cell checks per tick. Each callback also examines
  at most 128 existing reservation entries; this separate bound is additional
  to the traffic planner's comparison quota. The derived 32,768-slot ID index
  uses at most 64 probes before exact sorted lookup. Storage is reused and the
  batch is cleared each tick. Legacy worlds without a placement plane reject
  footprints wider than 15 cells from this helper rather than truncating them.
- Open-tile proof bits fit the existing per-tile metadata allowance. Preparing
  and published proof digests are included in the Cooperative checksum; their
  storage is included in navigation memory reporting.
- Scheduling, cache eviction, reservations and partially completed jobs are
  checksummed. Worker availability changes execution scheduling, not the
  selected work or its publication order.

## Follow-up validation (2026-10-05)

The bounded retry schedule, vacancy-following batch, shared open-tile proofs and
cardinal lane recovery described above are now integrated. The original
180-tick terrain-reanchor throttle remains; no additional suppression was
added to that recovery path. Retry and follower optimizations deliberately have different scopes;
an idle blocker can justify delaying a repeated failed search on constrained
terrain, while conditional follower movement requires the fresh open-tile proof.
No Retail or Flowfield movement policy was changed in this follow-up.

The final relevant suite passes **38 Release and 16 Debug tests**, including
all 13 physical group scenarios. Ninety-six Retail/Flowfield ground and naval
checkpoints match the saved baseline across serial and worker runs. The new
far-corridor fixture measures each body's crossing outside the arrival area:
all 64 cross both planes, the group's interquartile width grows from 32 to
47 pixels, and every member completes and remains at rest. The baseline stayed
at 32 pixels at both crossings. Formation pacing, queued commands, factory and
mobile production rallies, ownership changes and footprint legality pass.

Two final private authenticated games with seven Absurd AIs and Crusades balance
agree at tick 10,800: `44d24a0f48a438c3`. Each replay verifies all 360 checkpoints
in both Debug playback and an independent Release runner. Selection, joining
and spectator propagation pass for all three modes. No public server was used.
Both local builds are rebuilt; development protocol 226 requires matching
clients and servers and does not play earlier simulation protocols.

GCC and Clang agree on the final coordinator's six diagnostic state checkpoints.
AddressSanitizer, UndefinedBehaviorSanitizer and leak checks pass for the new
traffic, movement and lane helpers. The published-proof tests include a formerly
open tile becoming constrained, footprint-halo changes and publication failures.
The deterministic-math check passes all native compiler/optimization combinations;
the ARM cross-build legs remain unavailable because target headers are missing.
Windows, macOS and native ARM were not tested locally.

The controlled original-replay comparisons preserve all 855/1,216 pre-takeover
checksums, initial unit snapshots and queued orders. Both final cohorts finish
legally and remain at rest:

| Recorded cohort | Baseline last arrival | Updated last arrival | Updated maximum radius | Minimum final rest |
| --- | ---: | ---: | ---: | ---: |
| 60 mixed units | 44,403 | 44,090 | 408.83 px | 12,364 ticks |
| 211 mixed units | 63,967 | 70,579 | 787.98 px | 32,501 ticks |

All 271 are within the fixed 800-pixel comparison area. This is not a uniformly
faster result: the larger cohort finishes later, although its early arrival count
at tick 43,079 improves from 63 to 71. The smaller cohort has 31 arrivals at tick
36,453 versus 35 previously, despite finishing slightly sooner overall.

The fully unexplored giant maze reaches **8/8 at tick 943,030**, followed by
300 settling ticks and 120 ticks of exact rest. All footprints remain legal;
exploration grows from 216 to 207,969 cells, and peak accounted navigation memory
is 55.24 MiB. Its final checksum is `4522a100d0d9c6d5`. This crossing takes
**7.4% more simulation ticks** than the 878,137-tick baseline. The original
900,000-tick observation window ends at 6/8, with the remaining two still
exploring. The fixture retains that default and accepts an explicit longer
budget; the longer run preserves every physical arrival/rest assertion.

```sh
build/cooperative_maze_test /path/to/retail-data cooperative soak 1200000
```

This is an exploration and reliability result, not a shortest-path claim.
Physically enclosing a mover with parked bodies can still make its destination
unreachable; the coordinator waits rather than inventing a vacancy or consuming
its order. No optimization attempts to solve that by overlapping units.

The isolated tile-proof comparison at 16,000 units reduces screening reads by
95.6% in touching crowds and 99.2% in sparse crowds. Adjacent matched Release runs
measure 18.573 to 18.077 ms and 19.535 to 19.292 ms per tick, respectively
(2.7% and 1.2% lower). Physical progress is identical; diagnostic equivalence
omits only the additional proof digest, which production checksums include.
The measured proof bitmaps occupy 256 bytes, excluding metadata already covered
by the existing tile allowance. Separate exact-anchor and row/column caches were not retained; the matched
row/column candidate reduced cell reads but increased CPU time.

Broader retry suppression and unrestricted lane/follower changes were rejected
after physical regressions. No extra post-passage state, shortened global
lookahead, separate clearance table or changed terrain-reanchor delay remains.

## Follow-up performance measurements

Final measurements use the same machine, retail Swordsman definition, 1,200
simulation ticks, worker policy and layouts described below. Both binaries use
GCC Release `-O3 -DNDEBUG -ffp-contract=off`. The saved pre-change executable and
the final executable alternate order between cases, with no overlapping builds
or correctness jobs. Values are **baseline → updated Cooperative**. Timing
covers `World::tick`; RSS covers the whole headless process. No renderer, weapons
or AI are running in this movement comparison.

| Units / layout | Mean / p95 tick ms | Mean / median forward px | Peak nav MiB | Peak RSS MiB |
| --- | --- | --- | --- | --- |
| 1,000 / touching | 1.20 / 1.72 → 1.25 / 1.84 | 466.0 / 352 → 476.4 / 357 | 43.63 → 45.28 | 142.14 → 142.07 |
| 4,000 / touching | 3.77 / 5.50 → 3.86 / 5.26 | 243.5 / 58 → 269.4 / 76 | 45.72 → 47.67 | 170.14 → 170.36 |
| 16,000 / touching | 18.50 / 22.61 → 19.79 / 24.04 | 97.4 / 14 → 153.0 / 46 | 54.48 → 56.87 | 288.05 → 289.25 |
| 16,000 / spaced | 20.64 / 25.29 → 22.07 / 28.08 | 846.1 / 710 → 862.7 / 732 | 55.97 → 58.05 | 288.23 → 289.52 |
| 16,000 / sparse | 20.16 / 26.71 → 20.44 / 27.99 | 1155.5 / 1168 → 1154.2 / 1170 | 60.80 → 61.29 | 291.75 → 293.45 |

The repeated 16,000-unit touching case reproduces every state hash, movement
counter and work counter within each build. Its second timing pair is
18.647 → 19.549 ms. Across both pairs, **forward progress improves 57.1% for
4.8–7.0% more tick time**; median progress rises from 14 to 46 pixels and pending
routes fall from 76 to 11. Peak accounted navigation memory rises by 2.38 MiB
in that scene. The movement batch itself occupies about 1.59 MiB when allocated,
within its 2 MiB reservation; changed routes also affect terrain/field storage.
The overall navigation admission ceiling remains 512 MiB.

The result is workload-dependent. At 4,000 touching units, progress improves
10.6% for 2.3% more tick time. At 1,000 units it improves only 2.2% for 4.8% more
time; the 16,000-unit spaced case gains 2.0% progress for 6.9% more time. Sparse
progress is effectively unchanged (0.1% lower), with 1.4% more tick time. These
small timing deltas are single-pair observations, not confidence intervals.
Cooperative still advances tightly packed armies less than Retail or Flowfield
in the earlier comparison below. It remains experimental, with a substantial
improvement over its own crowded baseline rather than a general speed advantage.

## Initial implementation validation

The focused fixtures exercise lane descent, real placement, opposing streams,
mixed footprints, passage lifetime, shared work fairness, cancellation, allied
versus enemy coordination, queued orders, formation speed and production
rallies. Giant generated-maze runs additionally measure actual arrivals,
exploration, stationary behavior and serial/worker checksum agreement.

Performance and replay comparisons must report completion and physical progress
alongside tick time. A unit standing still because it is physically enclosed by
idle bodies is different from a reachable unit repeatedly failing to move.
Neither a successful pure grid route nor an empty order list alone proves that
a whole group reached a legal, compact destination and remained at rest.

The implementation was validated locally on Fedora 44, x86-64, using Release
and Debug builds. The final focused suite has 13 CTest entries, including 13
physical group scenarios. The broader relevant regression run passed 39 Release
tests and 14 Debug tests; after the last fixes, all 13 focused Release tests
and 9 focused Debug tests passed. Settings and replay tests cover all
three values, backward cycling, saved preferences, invalid values and campaign
forcing. Comparison with the saved pre-addition binaries preserves all 48
Retail/Flowfield ground and naval checkpoints and all 27 flight checkpoints.

Two private authenticated loopback games with seven Absurd AIs and Crusades
balance completed without client/referee desync. Every recorded checkpoint
verified in both Debug and the independent Release replay
runner. Both runs agree at tick 10,800: `31329c144139f1af`. The headless clients
stopped at ticks 10,800 and 10,801, recording 360 and 361 checkpoints respectively;
all 360 common checkpoint pairs match. Their final hashes are not compared as
though they represented the same tick. All three mode selections also passed
against the current Release server. No public server was probed.

GCC 16 and Clang 22 agree on the pure corridor, traffic and passage tests and
checksummed traces. AddressSanitizer, UndefinedBehaviorSanitizer and leak checks
pass for the new coordinator. GCC and Clang builds of the final navigator also
agree on all 24 observed hashes in the four freshness scenarios, including
serial and worker runs. Capacity regressions include 256 expensive
callers competing for the shared work quota, 4,096 occupied passage gates,
invalid footprints, stale topology, canceled orders and pending proof jobs.
Windows, macOS and native ARM builds were not run locally. The optional ARM
cross-compiler determinism leg could not build with the available target headers.

Two orders from the reported maze replay were reproduced from their original
recorded prefix, with identical units, order queues and initial positions before
readmitting routes in each mode. This is a controlled comparison, not support
for playing protocol-223 recordings in the new engine:

| Recorded order | Cooperative completion | Physical result |
| --- | --- | --- |
| 60 mixed units | 60/60 by tick 44,403 | All legal, within 457.21 px of the destination; at least 12,051 ticks at rest |
| 211 mixed units | 211/211 by tick 63,967 | All legal, within 791.21 px; at least 39,113 ticks at rest |

The Flowfield control finished 59/60 and 210/211 in those observation windows.
Cooperative was slower early: the first order had 35 arrivals versus
Flowfield's 51 at tick 36,453, and the larger order had 63 versus 86 at tick
43,079. Later, at tick 63,079, the larger order had 210 versus 207 arrivals.
These results do not establish uniformly faster travel or tighter formations.
A separate controlled test proves that a mover completely enclosed by parked
bodies retains its mission and waits instead of
repeatedly stepping between the remaining cells.

The fully unexplored 64-by-64 Taros maze fixture completed with all eight
zero-pivot Beast Riders at tick **878,137**. All stayed legal and inside the
destination area through 300 settling ticks and another 120 ticks of exact
rest. Exploration grew from 216 to 226,087 cells; peak accounted navigation
storage was **53.75 MiB**. Its final checksum after rest was
`fb67147ae0176dee`. The shorter 90,000-tick discovery run matches serial and
worker execution at every 300-tick checkpoint. Fully known maze fixtures also
complete for Hunters and Swordsmen. The unexplored run takes substantial
backtracking; it is a reliability test, not a shortest-route claim. The final
local-freshness optimization improved open-map progress and the larger recorded
order, but this maze crossing took about 12% more ticks than the prior candidate.

To run the committed fixtures with legally owned game data:

```sh
ctest --test-dir build -R '^cooperative_' --output-on-failure
build/cooperative_maze_test /path/to/retail-data cooperative soak 1200000
build/cooperative_benchmark /path/to/retail-data 16000 1200 cooperative workers touching
```

## Initial implementation performance

The comparison fixture uses the same retail Aramon Swordsman type, initial
positions, destination, worker setting and 64-by-64 flat map for all three
backends, with base Kingdoms balance. It tests 1,000, 4,000 and 16,000 units,
with additional 16,000-unit layouts spaced 48 and 96 pixels apart. The touching
layout spaces the two-cell
footprints 32 pixels apart. Weapons and AI are disabled to isolate movement;
these results are not full battle or renderer benchmarks.

Timing covers `World::tick` after setup. The initial 1,200 ticks include cold
navigation preparation. Longer diagnostic runs report each successive
1,200-tick window separately, so initial preparation cannot hide persistent
congestion. Forward progress is the change in world X plus world Z toward the
common southeast destination, not path length or time spent animating. Median
progress and the count with positive forward progress accompany the average. Memory
reports distinguish accounted navigation storage from process resident memory.
The fixture samples physical legality and checks the common 512 MiB navigation
ceiling; the group tests separately check every member and actual arrival.

The pre-improvement comparison ran on 2026-10-05 on Fedora 44, an Intel Core Ultra 9
275HX with 24 cores and 125 GiB RAM, using one GCC Release executable for every
mode. All 13 cases ran serially without overlapping builds or other validation
jobs. This table is one final pass; the earlier candidate comparison had two
passes, and the three repeated final Cooperative 16,000-unit runs reproduced
their exact physical progress and state hashes. All eight final Retail and
Flowfield hashes also match the preserved controls.

| Units | Layout | Mode | Mean / p95 tick ms | Mean / median forward px | Positive forward | Pending | Nav / RSS MiB |
| ---: | --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1,000 | Touching | Retail | 1.42 / 2.69 | 1,344.2 / 1,454 | 1,000 | 0 | — / 165.3 |
| 1,000 | Touching | Flowfield | 1.07 / 1.47 | 1,240.9 / 1,251 | 1,000 | 0 | 43.4 / 141.1 |
| 1,000 | Touching | Cooperative | 1.22 / 1.77 | 466.0 / 352 | 959 | 0 | 43.6 / 142.2 |
| 4,000 | Touching | Retail | 3.52 / 5.74 | 1,427.5 / 1,446 | 4,000 | 0 | — / 188.4 |
| 4,000 | Touching | Flowfield | 4.56 / 5.42 | 912.3 / 831 | 4,000 | 0 | 44.5 / 168.5 |
| 4,000 | Touching | Cooperative | 3.58 / 4.79 | 243.5 / 58 | 3,566 | 10 | 45.7 / 170.2 |
| 16,000 | Touching | Retail | 13.84 / 19.07 | 724.3 / 494 | 16,000 | 0 | — / 283.4 |
| 16,000 | Touching | Flowfield | 19.61 / 23.27 | 478.3 / 286 | 15,987 | 0 | 51.4 / 283.9 |
| 16,000 | Touching | Cooperative | 17.87 / 21.34 | 97.4 / 14 | 15,820 | 76 | 54.5 / 287.8 |
| 16,000 | Spaced | Flowfield | 20.31 / 24.28 | 1,303.6 / 1,310 | 16,000 | 0 | 51.9 / 284.2 |
| 16,000 | Spaced | Cooperative | 19.70 / 23.32 | 846.1 / 710 | 16,000 | 6 | 56.0 / 288.0 |
| 16,000 | Sparse | Flowfield | 17.39 / 22.35 | 1,357.2 / 1,384 | 16,000 | 0 | 56.8 / 292.5 |
| 16,000 | Sparse | Cooperative | 19.40 / 25.03 | 1,155.5 / 1,168 | 16,000 | 0 | 60.8 / 291.6 |

Nav is the peak accounted shared-navigation storage. Retail does not expose
that counter; its dash does not mean it uses no pathfinding memory. RSS is the
peak resident memory of the whole headless process. These are large synthetic
single-destination cohorts, including sizes above the normal per-player lobby
cap. The final Cooperative cases used at most 60.8 MiB of accounted navigation
storage, well below the 512 MiB admission ceiling.

The lower tick time in some rows is not a throughput win: Cooperative makes
less forward progress than Flowfield in every layout here. Retail also advances
the touching groups farther. These measurements support retaining Retail as
the default and treating Cooperative as an experiment in reliable arrival and
traffic handling, rather than recommending it as a general performance upgrade.

The local-freshness optimization was compared separately with the preceding
Cooperative candidate. Over the first 1,200 ticks at 16,000 units, forward
progress increased 43% in touching, 24% in spaced and 30% in sparse layouts;
tick time increased 23%, 7% and 17%, respectively. Pending routes fell from
7,515/654/2,166 to 76/6/0. A longer touching run gained 31% total progress for
15% more tick time over 3,600 ticks, but its last 1,200-tick window advanced
6.5% less than the earlier candidate. This removes an unrelated-rebuild delay;
it is not a general steady-state congestion fix. Those longer diagnostic runs
include identical observational counters in both binaries.

A separate experiment allowing same-direction queues to keep pushing through
the native movement controller was rejected: it used 13.5% more CPU for only
8.5% more total progress, and its final window advanced 10% less. That change
is not part of the engine.

Cooperative remains experimental. Densely touching two-cell bodies can block
every adjacent footprint, leaving the group dependent on vacancies propagating
from its edge. The bounded local planner checks present occupancy. The follower helper can
propagate actual vacancies within a tick, but does not reserve space that
another body is merely expected to vacate later. It can therefore
advance less than Retail or Flowfield in tightly packed open-ground crowds,
even when its tick time is lower. The replay and maze results demonstrate
specific reliability improvements, not a universally faster or better
pathfinder.
