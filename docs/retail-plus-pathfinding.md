# Retail+ pathfinding

Retail+ is an experimental fourth choice on skirmish and multiplayer creation
screens, ordered Retail, Retail+, Flowfield, Cooperative. Retail remains the
default and compatibility baseline. The host's
choice is authoritative, the lobby displays it, and settings and replays retain
it. Campaigns and Crusades battle rooms continue to use Retail.

## Design and scope

Retail+ uses the existing Retail search worker, goal geometry, terrain grading,
player scheduler, route delivery and native movement controller. It does not
allocate Flowfield terrain fields or replace long-distance routes with a new
planner. The local adapter reuses the existing bounded traffic implementation
instead of maintaining a second reservation/search implementation.

New local behavior applies only to ordinary ground Move missions, including
queued Move legs, for mobile units that are neither builders nor transports,
with footprints up to 4-by-4 cells (covering the shipped ground movers).
Larger modified units use native movement; their footprint queries could
otherwise exhaust a local search's quota before it can make useful progress.
Combat approaches, fight-move, patrol, guard, construction, production exits,
transport interactions, flying units, boats, and other special goals retain
native behavior. A transition to an unsupported order releases local claims.
This scope is deliberately narrower than Flowfield and Cooperative.
Formations retain native Retail pacing, including its straggler catch-up rule;
the stricter shared-mode formation cap remains specific to Flowfield/Cooperative.

Group right-clicks give supported units the same destination center; unsupported
units retain their native destination offsets. Friendly traffic can yield
through short, collision-checked detours. A chosen
detour is retained across ticks. Shared destinations use bounded standing places
and verified settled neighbors, with a terrain connection to the original
goal; being blocked alone is not arrival. Unknown terrain cannot prove arrival.
Native movement continues to enforce footprint, water, slope and obstacle rules.
Arrival slots stay within a compact radius derived from cohort footprint area,
with a small minimum maneuvering radius. Contact chains cannot authorize
settling indefinitely far back along an approaching column.
Diagonal local detours check both side footprints. Native route following keeps
its existing collision rules; imposing the detour rule on native route corners
was tested and rejected because it could reject corners the native solver allows.
Terrain is never made passable by rounding away a jagged edge.

Supported moves retain native corner pruning and throttle new native requests
to at least 30 ticks while locally handling congestion. A physically reached
second corner can retire an obsolete preceding corner instead of causing an
out-and-back turn. Genuine exhaustion or
lack of progress can request another native route. A failed search does not
enlarge a supported Move's mission radius, disable a still-live route prefix,
or discard its queued destinations.
Unsupported orders retain Retail's retry and mission-completion behavior.

The native search scheduler still uses deterministic player/slot selection,
priority handling and simulation-work accounting. Retail+ does not share a
route merely because destinations match. Footprint, movement profile,
exploration, heading, current bodies and goal semantics remain inputs to each
native search. No timing measurement affects scheduling or simulation decisions.
No new shared-destination route cache or worker-completion scheduling policy was
introduced. The behavior-equivalent search changes reduce CPU spent doing the
same admitted work; Retail+ separately changes retry/crowd behavior and therefore
must be judged on physical outcomes as well as its tick cost.

### Unblocked traffic bookkeeping

An ordinary unblocked mover far from arrival can now update its existing traffic
records without constructing the body-query and arrival-proof callbacks. The
shortcut requires exact command, player, alliance, footprint and goal identities,
no retained detour, passage, arrival slot or yielding state, and an empty local
reservation table. Both coordinators still advance their deterministic work
cursors, refresh positions and progress, and maintain group membership. A declined
shortcut leaves all state untouched: the full adapter may subsequently defer on
its body-query quota without calling the traffic policy. Geometric arrival and
every retained claim continue through the complete validation path.

The optional `--profile` observations separate context/bookkeeping, callback and
body-query setup, traffic-policy execution, and per-tick maintenance. These
clocks never affect movement, work quotas, or checksums. A 2,000-unit open-ground
diagnostic over 6,000 ticks used the shortcut for 7,715,839 updates and the full
path for 717,959 updates; both paths retained the reference completion tick 4,384.
This is workload coverage, not a speedup estimate. Final four-mode timing results
must use the exact integrated candidate and its recorded baseline.

## Shared CPU improvements

Two optimizations also apply to unmodified Retail behavior:

- Search scratch storage clears previously touched cells and retains vector
  capacities. Imported or untracked scratch takes the conservative full-clear
  path. Search flags, raw directions, node order, work accounting and phase
  transitions retain their original values.
- Search grading reuses one sorted body snapshot within a scheduler batch.
  Movement and body mutations cannot occur inside that batch. Calls outside it
  rebuild the snapshot, including multiple calls in the same simulation tick.

Optional diagnostic timers measure preparation, initialization and execution
separately. They are disabled by default, have no effect on work quotas or
checksums, and are not part of gameplay state.

## Resource limits and determinism

The local coordinator retains its 16 MiB cap and 16,384-record limit. Arrival
storage has a conservative additional 32 MiB reservation; these figures are
ceilings, not startup allocations. There is no extra full-map traffic grid.
Local search uses a 25-by-25-cell window, at most 384 visits and 48 retained
direction steps. At most 64 new local-search callers are admitted per tick;
following already committed detours is not limited to those 64 callers.
Footprint and reservation work has separate fixed quotas. An additional
arrival-proof budget allows 8,192 footprint-cell charges
per tick. A proof too large to fit a fresh quota is declined, rather than
deferred forever. Body queries use the existing spatial index, with a separate budget
of 32,768 candidate/bucket charges per tick and at most 256 candidates per query.
Neighbor output is capped at 64 bodies. Overfull spatial buckets or a disabled
index release optional local claims and fall back to native movement. This
avoids permanent waiting beneath a dense airborne crowd. At geometric arrival,
that fallback still uses one ordinary authoritative native placement query to
reject an illegal standing footprint; the existing native collision/search
costs are separate from the optional local-work quotas.

Exhausted quotas mean deferred evidence, not a blocked cell, a failed route, or
permission to settle. Budget cursors rotate deterministically to avoid repeatedly
favoring the same low IDs. Claims and partially completed work, including those
cursors and queued command identities, are checksummed. Elapsed wall time and
worker completion order never choose movement outcomes.

## Compatibility

The mode byte retains `0=Retail`, `1=Flowfield`, `2=Cooperative`, and adds
`3=Retail+`. Version 0.7.24 uses protocol 227 and requires matching clients and servers.
Replay format 11's layout is unchanged, but playback requires the current
simulation protocol; use an older engine for its older recordings. Unknown
network/replay mode bytes are rejected. Missing or invalid saved preferences
fall back to Retail. No generated-map format changes are needed.

## Reproducing measurements

`crowdbench` retains its original interface and also accepts parameterized
scenarios. `--units` is per player, not total population. For example:

```sh
build/crowdbench --mode retail-plus --scenario doors --units 200 \
  --players 1 --moving-percent 100 --ticks 6000
python3 tools/crowdbench_matrix.py --binary build/crowdbench \
  --output /tmp/retail-plus-matrix --units 200 500 1000 2000 \
  --populations 1:100 4:25 8:10 4:100 --ticks 6000
```

Run timing comparisons serially on an otherwise idle host. Use separate
`--profile`, `--allocations`, and `--trace` runs for diagnostics. The matrix runner
records exact commands and the executable hash. Its trace comparator checks
each tick's hash/work/pending counters and the unit/controller/route trace.

The headline arrival metric requires a living unit at rest for 30 ticks, with
empty orders, a legal full footprint, and a position inside the scenario's
pre-authored goal area. Raw order retirement is reported separately. Unfinished
percentiles are censored rather than inferred from completed members. CPU
timings cover elapsed `steady_clock` wall time inside `World::tick`, excluding
fixture setup, hash/legality observation and trace output. The CPU-cost comparisons
below use this tick-time proxy, not a process CPU-time clock. RSS is
whole-process peak memory, not pathfinding-only memory;
C++ allocation counters report requested bytes rather than retained heap size.

Scenarios include open long moves, doors, long narrow bridges, alternating maze
walls, opposing columns, shared goals, mixed footprints, exploration, obstacle
changes, rapid replacements, unreachable destinations and recovery with and
without reissuing commands. A lower tick time with less movement is not counted
as an equivalent-throughput improvement.

## Measurements, 2026-10-05

The final timing sweep ran 120 cases serially on Fedora 44, x86-64, Intel Core
Ultra 9 275HX. Frozen and new binaries used the same benchmark source, `-O3`,
`-DNDEBUG`, `-ffp-contract=off` and static C++ runtime linkage. The reference was
HEAD `d9f3b4392ca59a1e4031b4e14a89fdda8ca3d03f` **plus the preexisting working-tree
changes**, frozen before this task. It is not a pristine release/main baseline.
Compilation, other tests and profiling were kept outside the timing window.
No warm-up ticks were discarded. Only the open 2,000-unit case was repeated
three times, including reversed executable order. Most other timings are one
run per configuration, not confidence intervals.

The [machine-readable results](retail-plus-benchmark-2026-10-05.json) contain
all cases, binary hashes, CPU percentiles, request latency indicators, stalls,
crossings, paths and memory observations. Local full logs, commands and frozen
sources are under `/tmp/tak-retail-plus-20261005/`; they are not game assets or
release artifacts. Earlier exploratory runs with different runtime linkage
were excluded from CPU comparisons.

Mean milliseconds per simulation tick, open terrain, one player, all units
moving, 6,000 ticks:

| Units | Frozen Retail | New Retail | Retail+ | Flowfield | Retail and Retail+ whole-group arrival tick |
|---:|---:|---:|---:|---:|---:|
| 200 | 0.111 | 0.073 | 0.097 | 0.211 | 3,406 |
| 500 | 0.356 | 0.242 | 0.335 | 0.457 | 3,801 |
| 1,000 | 0.586 | 0.446 | 0.626 | 0.712 | 3,505 |
| 2,000 | 1.739 | 1.453 | 1.909 | 1.182 | 4,384 |

At 2,000 units, the medians of the three runs' p50/p95/p99 tick times were
2.117/3.068/3.587 ms for frozen Retail, 1.777/2.556/2.975 ms for new Retail, and
2.416/3.216/3.633 ms for Retail+. Thus the shared Retail optimization saves about
16% mean CPU in this case with identical movement; Retail+ adds about 31% to the
new Retail mean without improving open-ground completion. Flowfield finished
142/200, 4/500, 0/1,000 and 0/2,000 within this horizon, so its lower large-case
CPU is not an equivalent-throughput win.

Strict physical arrivals after 6,000 ticks in the paired obstacle cases:

| Scenario | Movers | Retail | Retail+ | Flowfield |
|---|---:|---:|---:|---:|
| Door | 200 | 38 | 103 | 130 |
| Door | 500 | 65 | 72 | 3 |
| Door | 1,000 | 52 | 69 | 0 |
| Door | 2,000 | 87 | 34 | 0 |
| Maze | 1,000 | 10 | 6 | 0 |
| Maze | 2,000 | 0 | 0 | 0 |
| Opposing columns | 1,000 | 1 | 27 | 0 |
| Opposing columns | 2,000 | 0 | 0 | 0 |
| Shared goal | 200 | 119 | 133 | 53 |
| Shared goal | 500 | 86 | 85 | 120 |
| Shared goal | 1,000 | 45 | 46 | 180 |
| Shared goal | 2,000 | 5 | 47 | 330 |

Even the smaller door cases improve endpoint arrivals at a traversal/CPU cost:
at 200/500/1,000 movers, Retail-to-Retail+ midpoint crossings are
200→154 / 273→113 / 257→108, while mean CPU increases about 107%/88%/97%.
More completed destinations in those cases is not improved door throughput.
The 2,000-unit door is a regression: Retail+ crosses 85 bodies past the midpoint
versus Retail's 246 (0.425 versus 1.230 crossings per simulated second), while
mean tick CPU rises from 1.444 to 2.449 ms. Stalled unit-ticks fall from 7.75M to
7.31M, illustrating why that counter alone cannot establish improvement.
At the 2,000-unit shared goal, arrivals improve from 5 to 47 but midpoint
crossings fall from 424 to 293 and CPU rises from 1.860 to 2.706 ms. All
incomplete whole-group arrival times remain censored.

Separate untimed 200-mover correctness runs cover the remaining scenarios:

| Scenario, 6,000 ticks | Retail arrivals | Retail+ arrivals | Flowfield arrivals |
|---|---:|---:|---:|
| Long narrow corridor (bridge fixture) | 34 | 84 | 85 |
| Maze | 43 | 39 | 11 |
| Opposing columns | 72 | 166 | 135 |
| Mixed footprints | 91 | 89 | 93 |
| Initially unexplored | 3 | 19 | 0 |
| Dynamic obstacle | 159 | 170 | 138 |
| Rapid order replacement | 180 | 160 | 175 |
| Unreachable | 0 | 0 | 0 |
| Recovery with reissued commands | 15 | 164 | 99 |
| Recovery with original commands | 0 | 144 | 0 |

These finite-window counts show both gains and losses. They do not prove
unfinished members are permanently stuck, and raw order retirement outside the
authored destination area does not count as success.
The [observed-position sequence](img/retail-plus-sequence-2026-10-05.png) and
[trajectory detail](img/retail-plus-trajectories-2026-10-05.png) were visually
inspected. They plot actual simulated positions, not planned routes or rendered
game screenshots. Maze/door queues still advance late in the run; local
circulation remains visible around crowds. The rapid-replacement case remains
at 160 arrivals, with 39 sampled-stationary active members and one short circuit
at the end. No interactive rendered-game movement video was reviewed for this
change, so these geometry plots do not replace a hands-on visual playtest.

Population controls used 200/500/1,000/2,000 units **per player**, with four
players/25% moving, eight/10%, and four/100%, for 1,200 ticks. No complete-group
arrival claim is made at that short horizon. With 16,000 units and 1,600 moving,
mean CPU was 8.499 ms frozen Retail, 6.756 ms new Retail, 7.447 ms Retail+, and
3.382 ms Flowfield. Retail/Plus first-move p95 was tick 1; Flowfield's was
censored. At 8,000 movers, the corresponding means were
17.731/16.958/19.943/3.656 ms. Map dimensions grow with population, so these rows
do not isolate unit-count cost from map size.

Admission-stamp and pending-clear p95 in the 16,000-unit control were 860/861
ticks for Retail and 732/732 for Retail+. These are scheduler-observation
indicators, not exact route-delivery latency: a pending clear can include failure
or cancellation, and units can already follow their initial direct segment.
The benchmark labels initial-segment availability separately for that reason.

Separate allocation diagnostics over 1,200 open-ground ticks preserved the
Retail hashes. At 200 units, requested C++ allocation bytes fell from 101.88 MB
to 21.19 MB and calls from 226,236 to 181,768. At 2,000, bytes fell from
660.31 MB to 127.61 MB and calls from 1,338,294 to 1,289,512. These are cumulative
requested bytes, **not live memory** or direct `malloc` coverage. Optional timers
on the new 2,000-unit Retail run measured 1.57 ms initialization, 21.89 ms grading
preparation and 14.80 ms search execution in total; the old engine lacks these
separate timers, so no before/after phase-speed ratio is inferred.

The largest observed retained Retail+ local state in the matrix was 6.17 MiB,
native search scratch 6.25 MiB, body snapshot 0.50 MiB and grade plane 0.78 MiB.
These are logical/capacity observations, not allocator peak measurements.
Whole-process high-water RSS in the 16,000-unit control was 55.13 MiB for new
Retail, 56.68 MiB for Retail+ and 56.47 MiB for Flowfield. Flowfield's measured
navigation peak there was 32.11 MiB. Do not subtract RSS values to infer precise
cache ownership.

Retail+ is retained as an opt-in experiment because several endpoint-arrival,
opposing-traffic and recovery outcomes improve while keeping native
long-distance routing. It is **not a general large-army performance upgrade**.
In particular, 2,000-unit doors and large mazes still need work; the local
policy's overhead and limited throughput at saturation do not justify changing
the default. The behavior-equivalent Retail CPU changes have the stronger,
more consistent measured benefit.

## Regression coverage and limits

The shared Retail scratch regression records every callback, work charge,
notification, scratch cell, heap entry and route through 96 reused-worker
requests, including cancellation and imported scratch. Its pre-change digest
is `11705273632564964854`. Whole-world comparisons also check every tick's
checksum/work/pending counters and native movement/controller transitions
against a frozen copy of the pre-change working tree. Final-state comparisons
include physical arrivals, raw order retirement, path length, stalled ticks
and request/completion/failure counts.
All 52 old-mode trace files match, covering 15,626 per-tick checkpoints across
13 scenarios for Retail and Flowfield. All 26 old-mode long runs and all 30
paired Retail timing runs retain their reference hashes and physical metrics.
All 39 mode/scenario serial-versus-worker comparisons agree, and the final
Retail+ repeat runs agree. Instrumented/trace observers were checked separately
for simulation neutrality. No sampled illegal footprint was found in the matrix;
this sampling is not a proof about every intermediate placement.

Retail+ regressions exercise queued mixed groups, Stop/replacement, a jagged
obstacle with 3-by-3 movers, arrivals on the correct side of a goal wall, boxed
unreachable goals, obsolete route corners, formations, dense airborne neighbors,
and a newly blocked standing footprint. The last case combines illegal standing
with the dense-query fallback, then removes the obstruction and requires both
queued destinations to complete. Scope tests compare builder, transport,
Fight and oversized-body movement against native Retail. Production, transport,
builder automation, protocol, replay and settings tests cover mode integration.
The 84-test relevant Release sweep passed, followed by focused Release and
Debug reruns as the implementation settled. Final scratch, traffic and World
tests pass in both builds. The Debug client `build_controls` test exercises
actual input dispatch, including the common destination versus native fallback
offsets. Two private, seated-client/referee games with seven AIs reach tick
5,400 with hash `4b3d90b235f6e602` and no desync; spectators alone would not exercise
that client checksum path. Both complete local builds are rebuilt.

An actual generated maze TNT supplements the synthetic fixtures. On explored
terrain, the solo route finishes at tick 20,730 and groups of 32 Hunters and
16 Warriors finish legally near their destination. The solo serial/worker
hashes agree. This does **not** establish universal maze reliability: the same
fixture with initially unknown terrain, and its low-pivot Beast Rider case,
remain unfinished at 75,000 ticks in both Retail and Retail+. Retail+ deliberately
keeps the native long-distance planner and inherits these limitations. It does
not claim a global clearance-smoothing solution or guarantee every crowded
precise destination is reachable.

Testing uses Linux x86-64 Release and Debug builds. GCC/Clang deterministic-math
checks agree at O0/O2/O3; the isolated traffic checks agree across both compilers
and pass AddressSanitizer/UndefinedBehaviorSanitizer with leak detection. This
is not a complete cross-platform multiplayer certification. Windows, macOS,
and native ARM execution were unavailable; the ARM cross-compiler check skipped
because target headers were missing. No public server was probed or changed.

## Follow-up saturation experiments, 2026-10-05

Current main `86673b4` was inspected before this follow-up; its simulation matches
reviewed commit `3255400`. Three runs each of the 2,000-unit open and doorway
fixtures reproduced all physical outcomes and hashes. Median open-ground means
were 1.458343 ms/tick for Retail and 1.950188 for Retail+, with all 2,000 legally
arrived at tick 4,384. Median doorway means were 1.442612 and 2.495411 ms/tick.
Other release-validation clients and desktop processes were running, so these are
paired, reversed-repeat measurements on a loaded host, not an idle-host
reproduction of the historical timing numbers above.

The saturated doorway again produced 246 crossings/87 legal arrivals in Retail
and 85/34 in Retail+. Retail+ performed 161,241 local searches, retained 23,524
local routes and returned 4,427,079 waits. Native requests were 9,465 versus
Retail's 19,353. These observations motivated testing whether local detours and
waiting were interfering with the native body-aware route controller. They do
not establish that increasing the native request count alone fixes the queue.

Six deterministic policy trials were evaluated with unchanged collision,
arrival and search-budget rules. Numbers below are crossings/legal arrivals
at 6,000 ticks. All trial changes were removed.

| Trial | Door 2,000 | Door 200 | Opposing 1,000 arrivals | Replacement 200 arrivals | Reason rejected |
|---|---:|---:|---:|---:|---|
| Reference Retail+ | 85/34 | 154/103 | 27 | 160 | Comparison control |
| Give a progressing same-way native queue 30 ticks before local avoidance | 77/44 | 159/99 | 27 | 160 | Fewer saturated-door crossings and more local searches |
| Delay completed failed retries eight ticks behind an unchanged moving peer | 103/46 | 159/102 | 21 | 156 | Door gain accompanied by worse opposing and replacement completion |
| Continue native movement after a completed local failure, if reservations permit | 97/45 | 136/86 | 35 | 156 | Large losses at the smaller doorway and in replacement recovery |
| Continue native movement during admission waits, if reservations permit | 95/38 | 138/80 | 28 | 150 | Large smaller-door and replacement regressions |
| Apply the eight-tick retry delay only after 180 stationary ticks | 85/34 | 169/109 | 26 | 160 | No saturated-door movement gain; only 12 searches saved there |
| Delay moving-peer retries only after a completed search observes an actual terrain refusal | 98/44 | 154/103 | 27 | 160 | Initial gain reversed at a longer horizon and another seed |

The terrain-refusal trial used existing charged terrain queries, without new
probes, a guessed passage, or an open-terrain assertion. Its initial seed-zero
gain at 6,000 ticks did not persist: at 12,000 ticks, 2,000-unit doorway crossings
and arrivals fell from 222/142 to 215/134. At 6,000 ticks, seed 42 fell from 86/48
to 80/47, while seed 7 improved from 67/42 to 83/44. Smaller seed-42 maze and
exploration cases also lost some crossings and arrivals. This evidence was
insufficient to retain a congestion-policy change. Targeted tests did verify the
mechanism's eight-tick expiry, recovery after terrain opened, command and observed
peer changes, cancellation, and the distinction between completed, deferred and
body-only failures; safe retry scheduling alone did not establish better movement.

The [experiment data](retail-plus-rejected-experiments-2026-10-05.json) preserve
all seven initial scenario/population controls for each trial, the terrain
trial's additional seeds and longer horizon, binary hashes, and experimental
source/test patches. Trials also covered 200-unit mazes, opposing traffic
and recovery without replacing the original order. Timings from these screening
runs are excluded because builds and diagnostic profiling overlapped. No sampled
illegal footprint occurred; this does not prove absence of starvation for every
unfinished member or constitute a continuous collision audit.

Cooperative's passage coordinator was inspected for reuse. Its permits depend on
fresh footprint-eroded topology, canonical passage endpoints, generation-aware
invalidation, and bounded background proof progress. Retail+ supplies none of
those topology proofs. Turning on the open-terrain flag or installing guessed
passage descriptors would therefore be unsound; neither was done. A future
passage integration needs those prerequisites and its own measured memory/work
cost. Existing committed-detour and clearance behavior remains intact.

Only the behavior-equivalent unblocked bookkeeping shortcut is retained from
this local-policy work. Its regression compares results and checksums after
12,000 individual updates, including replacements, cancellation, stalls, retained
routes and arrivals, and verifies that declined shortcuts change no state.
The final implementation matches 52 frozen-baseline trace files byte for byte:
Retail and Retail+, 13 scenarios, 600 ticks, two players with 16 units each and
75% moving. Traffic and World regression tests pass. The 2,000-unit open and door
runs retain their reference final hashes and outcomes. The standalone traffic
suite also passes AddressSanitizer/UndefinedBehaviorSanitizer with leak detection.
Saturated doorway
throughput remains a weakness; none of the rejected endpoint gains establishes
a general congestion improvement.

A separate Cooperative adapter prototype called the same bookkeeping-only
shortcut after refreshing settled members and checking pending passage proofs.
All 26 trace files matched its frozen control across the same 13 scenarios.
It was removed because its performance comparison was not completed; no
Cooperative speedup is claimed or shipped from that prototype.

The stopped investigation and remaining validation work are summarized in
[navigation comparison, 2026-10-05](navigation-comparison-2026-10-05.md).
