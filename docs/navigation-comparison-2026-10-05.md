# Navigation comparison — 2026-10-05

This report closes the navigation pass that started from main `86673b4` after
review of `3255400`. It covers the four existing modes (Retail, Retail+,
Flowfield, Cooperative) against a frozen baseline, and Legion, a fifth mode
added at the user's request during the pass. Retail remains the default and
the compatibility baseline: its simulation is byte-identical to the baseline
in every measured case.

Measured results and expected benefits are kept apart. Every number below
comes from a recorded run unless it is labelled otherwise.

## Method

**Binaries.** Both binaries come from `tools/navigation_baseline_build.sh`.
The baseline is main `86673b4` plus observation-only telemetry hooks
(`tools/navigation_baseline_telemetry.patch`) and the candidate's benchmark
harness files. The candidate is built from committed sources with the same
script. The harness is byte-identical in both, so only the simulation differs.
Per-tick Retail traces of the two binaries are byte-identical.

**Host.** Fedora 44, Intel Core Ultra 9 275HX (8 P-cores, 16 E-cores), GCC
16.2.1, Release `-O3 -DNDEBUG`, static C++ runtime and dependencies.

**Outcome matrix.** All 18 scenarios × 4 modes × 6 populations (200, 500,
1000 and 2000 units × 1 player at 100% moving; 500 × 4 players; 250 × 8
players at 50% moving) × seeds 0, 7 and 42, 6,000 ticks, plus 12,000 ticks
for the 2,000-unit doors, maze, shared-goal and opposing-column cases. That is
2,688 runs. Outcomes are deterministic, so these ran in parallel; repeated
runs gave 0 outcome mismatches. Diagnostics (route latency, allocations,
acceptance metrics) were on. Candidate binary sha256 `db5507ba…`, baseline
`8dd26af5…`. The four-mode candidate was later rebuilt at `d9d2999`; its
final hashes match the matrix binary on all 12 spot-checked cases.

**Timing matrix.** 10 scenarios × 4 modes × 3 populations (200 × 1,
2000 × 1, 500 × 4), seed 0, 6,000 ticks, 4 repeats each, so 960 runs. Each
case's repeats and both binaries ran back to back on one pinned P-core, with
the leading binary alternating per repeat. Eight cases ran at once on the
eight P-cores, while other work compiled on the E-cores. **This makes the
timing noisier than an idle serial run.** Retail's simulation is unchanged,
yet its per-case ratio ranged from 0.86 to 1.13; treat differences under
about 15% as noise. Candidate sha256 `01c2f0eb…`, baseline `cc1bf6f2…`.

**Arrival rule (unchanged).** A unit counts as arrived only when it is alive,
has empty orders, zero speed, a legal full footprint, stands inside the
scenario's authored goal area, and has been unchanged for 30 ticks.
Unfinished completion times are censored, never counted as successes.

**Route latency** is measured from the accepted request to the actual
delivery callback, in simulation ticks, separately from admission and pending
counters. Percentiles cover delivered requests only, so requests still
outstanding at the end are reported beside them.

## Four-mode before/after

### Totals

Sums over all cases and seeds. The 72 runs of the `singleunit` fixture at
2000 × 1, 500 × 4 and 250 × 8 failed identically in both binaries (the fixture
cannot lay out that many U-obstacles) and are excluded.

| Mode | Crossed (before → after) | Arrived (before → after) | Requests outstanding at end |
|---|---|---|---|
| Retail | 195,385 → 195,385 | 116,622 → 116,622 | 181,688 → 181,688 |
| Retail+ | 167,904 → 169,150 | 120,185 → 121,031 | 155,912 → 155,787 |
| Flowfield | 70,748 → 215,820 | 42,633 → 74,244 | 241,813 → 120,420 |
| Cooperative | 71,733 → 208,690 | 49,569 → 65,795 | 236,173 → 131,428 |

Final state hashes: Retail equal in all 327 cases. Retail+, Flowfield and
Cooperative differ, as intended.

### Per scenario

Crossed and arrived, summed over populations and seeds.

| Scenario | Retail+ crossed | Retail+ arrived | Flowfield crossed | Flowfield arrived | Cooperative crossed | Cooperative arrived |
|---|---|---|---|---|---|---|
| bridges | 5,245→5,302 | 3,220→3,258 | 1,097→9,754 | 333→5,025 | 1,375→10,094 | 454→4,408 |
| crowdtrap | 7,909→7,807 | 5,393→5,262 | 2,674→6,726 | 425→2,251 | 2,614→5,884 | 395→1,749 |
| doors | 6,105→6,390 | 3,964→4,098 | 2,097→13,283 | 551→6,664 | 1,875→12,315 | 455→5,891 |
| dynamicobstacle | 18,398→18,336 | 13,891→13,879 | 1,814→19,235 | 461→5,753 | 2,126→18,780 | 408→3,694 |
| exploration | 284→309 | 50→53 | 9→2,430 | 0→167 | 18→387 | 0→45 |
| groupdetour | 12,054→12,117 | 8,824→8,929 | 3,298→15,094 | 798→5,972 | 4,125→15,793 | 903→5,217 |
| jagged | 12,454→12,451 | 7,801→7,801 | 2,134→16,688 | 510→5,367 | 2,416→14,904 | 572→4,173 |
| maze | 3,053→2,989 | 1,207→1,186 | 322→4,763 | 32→1,662 | 223→4,227 | 91→1,557 |
| mixedfootprints | 7,322→7,376 | 2,059→2,302 | 20,100→20,100 | 7,356→7,288 | 20,100→20,100 | 11,497→11,405 |
| open | 20,100→20,100 | 20,100→20,100 | 1,860→20,100 | 451→5,857 | 2,136→19,762 | 412→3,482 |
| opposingcolumns | 14,721→15,037 | 3,346→3,403 | 1,746→17,693 | 406→4,256 | 2,049→17,052 | 403→3,694 |
| rapidreplacement | 0→0 | 14,409→14,551 | 0→0 | 19,461→8,732 | 0→0 | 19,491→4,739 |
| recovery | 19,151→19,193 | 12,127→12,113 | 868→20,100 | 326→2,350 | 891→19,600 | 352→1,202 |
| recovery-passive | 16,248→16,275 | 7,640→7,748 | 793→12,961 | 0→0 | 865→17,056 | 0→0 |
| sharedgoal | 9,170→9,817 | 2,551→2,727 | 26,100→26,100 | 5,526→5,526 | 26,100→26,100 | 8,574→8,574 |
| trapped | 13,221→13,182 | 8,503→8,521 | 3,367→8,324 | 897→2,274 | 2,351→4,167 | 462→865 |

**Rapid replacement is a benchmark artifact, not a pathfinding loss.** The
scenario's final command sends units back to their start. In the baseline,
Flowfield and Cooperative units barely moved (about 170,000 px of travel per
run), so they "arrived" where they stood. The candidate's units really travel
(1 to 2.5 million px) and are often still settling at tick 6,000. It still
shows that the shared modes settle more slowly than Retail after rapid
replacement.

### Timing

Candidate/baseline ratio of median mean tick time, geometric mean over 30
cases per mode:

| Mode | Ratio | Range |
|---|---|---|
| Retail | 0.978 | 0.86–1.13 (unchanged code: this is the noise) |
| Retail+ | 0.867 | 0.71–1.07 |
| Flowfield | 1.230 | 0.62–2.08 |
| Cooperative | 1.423 | 0.67–2.90 |

Selected 2,000-unit cases (median mean ms/tick over 4 repeats, with p95/p99):

| Case | Mode | Before | After | Crossed before → after |
|---|---|---|---|---|
| open | Retail | 2.60 (p95 4.69, p99 5.93) | 2.24 (3.93, 5.52) | 2,000 → 2,000 |
| open | Retail+ | 3.74 (7.39, 8.43) | 3.60 (7.76, 9.55) | 2,000 → 2,000 |
| open | Flowfield | 2.19 (2.82, 3.06) | 3.45 (4.73, 5.15) | 0 → 2,000 |
| open | Cooperative | 1.88 (3.30, 4.25) | 4.51 (6.91, 8.08) | 0 → 1,878 |
| doors | Retail+ | 7.33 (10.08, 11.97) | 6.66 (10.45, 12.28) | 85 → 113 |
| doors | Flowfield | 2.53 (3.71, 5.33) | 4.65 (6.70, 10.20) | 0 → 397 |
| doors | Cooperative | 1.62 (2.02, 2.35) | 3.58 (4.56, 4.96) | 0 → 381 |

Flowfield and Cooperative cost more per tick because their crowds now move.
The baseline was cheap only because 1,000+ independent destinations froze.
Retail+'s 13% saving fits the earlier exploratory measurements on the open
case; the exploratory pinned-core measurement there was 0.85×. Raw results
are in the [evidence directory](navigation-evidence-2026-10-05/final/).

## Changes retained

### Flowfield and Cooperative: the large-army stall

At 1,000+ movers with individual destinations, both modes made almost no
progress, even on open ground. Each destination built its own 64 × 64 field
per tile, the field-work quota allowed about one field per tick, and the
32-slot cache evicted destinations before their fields were used. Route
latency p50 was 700–2,500 ticks.

Tiles two or more tiles away from a destination's goal tiles now share one
field among all destinations with the same goal tiles. A test proves the
shared field equals what each destination would build. Goal tiles and their
neighbours keep exact per-destination fields. After a topology publication, a
finished shared field is reused when its source tile is unchanged; a hit also
checks the source-tile identity. Latency p50 at 1,000+ units fell to 1–30
ticks. See [Flowfield](pathfinding-port.md).

### Retail+: saturated doorway

The native search scheduler is the real limit in both Retail modes. Retail+
searches started deep in a shuffling crowd and cost about four times as much
as Retail's, and a body-query budget refused units while most of it was still
unspent. Blocked Retail+ movers now read the existing occupancy grid for the
obstruction ahead, falling back to the charged query only when the grid has
no owner. Doorway results at 2,000 units:

| Seed / ticks | Crossed | Legal arrivals |
|---|---|---|
| 0 / 6,000 | 85 → 113 | 34 → 43 |
| 7 / 6,000 | 67 → 89 | 42 → 51 |
| 42 / 6,000 | 86 → 138 | 48 → 53 |
| 0 / 12,000 | 222 → 241 | 142 → 172 |
| 7 / 12,000 | 166 → 189 | 96 → 136 |
| 42 / 12,000 | 201 → 264 | 127 → 180 |

Retail+ still crosses about half as many as Retail at the 2,000-unit door. It
lost 4–11% of arrivals in recovery-1000×4 and 11–30% in sharedgoal-1000×4. See
[Retail+](retail-plus-pathfinding.md).

### CPU-only, behaviour-equivalent

- Traffic records are indexed by unit id, `supports()` runs once per update,
  and the unblocked fast path now also serves Cooperative and Flowfield.
  Per-tick traces are byte-identical in all four modes.
- The prior pass's Retail search scratch reset and shared body snapshot are
  unchanged.

### Measurement and CI

- Four- and five-mode benchmark driver with outcome and paired timing phases,
  alternating leaders, provenance and a reproducible baseline build.
- Acceptance scenarios and metrics for every mode: jagged walls, sealed
  pockets, gated crowds, single-unit U-obstacles and group detours; spinning,
  terrain-stuck vs crowd-held vs trapped classes, and path optimality.
- CI fails when a selected navigation test is not registered. Linux, Windows,
  macOS and determinism workflows run the Cooperative, Retail+, Flowfield,
  Legion, telemetry, routing and five-mode golden checkpoint tests.

## Rejected

- **Six earlier Retail+ congestion trials** and **dependency-scoped field
  retention**: see the [rejected-experiment archive](retail-plus-rejected-experiments-2026-10-05.json)
  and [field-reuse archive](navigation-field-reuse-experiment-2026-10-05.json).
- **Retail+ in this pass:** retrying failed local searches only on input
  change (skipped almost nothing), a rotating body budget (more deferrals),
  removing the stalled-queue hold (seed-dependent), pressing on while a
  replacement search is pending (fewer completions).
- **Weighted portal routing.** A cached destination-independent portal graph
  routes 0.6–4.4% shorter in the maze only, bends open-ground routes up to 4%
  longer with sparse portals, and costs 22–420% more search work per route.
  Its one clear win, weighted terrain, has no World scenario. Component-hop
  routing stays; the evaluation is in `tools/flow_portal_graph.h`.
- **Sharing goal-neighbour fields** roughly doubled 1,000-unit arrivals but
  broke group spreading after a passage and stranded one queued unit.

## Legion

Legion (mode 4) was added during this pass at the user's request: armies plan
routes as a group, units do not catch on jagged terrain, and a unit that is
genuinely stuck stops instead of spinning. Its design, scope, rejected
approaches and known weaknesses are in [Legion](legion-pathfinding.md). In
short: one integer route field per group and footprint class on the exact
footprint-legality rule the mover enforces; steering that only descends the
field over proven-legal cells (so units slide along walls); formation slots
for shared destinations; committed lane passing for opposing traffic;
settled units that step aside for late arrivals; and walking to the nearest
reachable point, then holding, when a goal is cut off.

Legion has no baseline to compare against, so it is scored against the
**best of the four other modes' final results** for each case (mean over
seeds 0, 7, 42; 2% tolerance). Final Legion matrix: 336 cases on the exact
shipped code, candidate sha256 `5ca8e070…`; nine `singleunit` runs failed on
the fixture limit described above. The `singleunit` rows come from a rerun
after the last Legion fix (region-bounded fields). That fix took 1,000
separate orders from 623 to 1,000 settled, all by tick 548 (Cooperative's
whole group settled at 1,346), and cut field memory there from 222 MB to
2.3 MB. Every other Legion hash is unchanged by it.

| Metric | Legion equal or better | Legion worse |
|---|---|---|
| Midpoint crossings | 94 | 15 |
| Legal arrivals | 101 | 8 |
| Spinning unit-ticks | 109 | 0 |
| Terrain-stuck unit-ticks | 107 | 2 |
| Units ending terrain-stuck | 108 | 1 |
| Units ending idle in the open | 94 | 15 |
| Units with no route still moving after grace | 91 | 18 |
| Path length vs shortest legal path | 17 | 0 |

2,000 units, one player, 6,000 ticks (crossed / arrived; spinning in
unit-ticks):

| Scenario | Legion | Best other crossed | Best other arrived | Legion spin | Lowest other spin |
|---|---|---|---|---|---|
| open | 2,000 / 2,000 | 2,000 | 2,000 (Retail) | 0 | 0 |
| doors | 399 / 180 | 399 | 129 (Cooperative) | 0 | 4,476 |
| bridges | 364 / 172 | 380 | 120 (Cooperative) | 0 | 5,682 |
| maze | 45 / 0 | 27 | 0 | 0 | 1,827 |
| opposing columns | 991 / 194 | 1,145 | 67 (Cooperative) | 997 | 6,234 |
| shared goal | 2,000 / 797 | 2,000 | 494 (Cooperative) | 0 | 7,125 |
| mixed footprints | 2,000 / 1,067 | 2,000 | 816 (Cooperative) | 3 | 4,437 |
| jagged walls | 1,226 / 589 | 1,248 | 258 (Retail) | 794 | 7,675 |
| gated crowd | 556 / 170 | 353 | 70 (Retail) | 0 | 4,736 |
| group detour | 1,069 / 908 | 1,168 | 795 (Retail) | 0 | 10,381 |
| recovery | 2,000 / 1,999 | 2,000 | 646 (Retail+) | 0 | 0 |
| recovery, original orders | 2,000 / 1,999 | 2,000 | 136 (Retail+) | 0 | 0 |
| dynamic obstacle | 1,999 / 1,479 | 1,936 | 1,302 (Retail) | 0 | 8,258 |
| sealed pockets | 1,856 / 1,547 | 1,588 | 748 (Retail) | 36 | 8,065 |
| rapid replacement | 0 / 2,000 | 0 | 1,843 (Retail) | 1,087 | 63,177 |

**Where Legion is worse:**

- *Units with no route still moving* (18 cases, recovery/unreachable): by
  design, a unit whose goal is cut off walks to the nearest reachable point
  and holds there. The acceptance metric counts that walk. Flowfield and
  Cooperative score 0 because their units stand still. Units in genuinely
  sealed pockets stay still in Legion too.
- Crossings at 500–1,000 units through doors and bridges, where Flowfield
  leads by 10–30%; opposing columns at 2,000 × 1, 500 × 1 and 250 × 8 (half
  the units idle in the way); group-detour crossings by up to 12%.
- Jagged walls with 8 players at 50% moving: the idle half blocks the lanes.
- Units ending idle in the open: up to 31 at shared-goal 500 × 1, a few
  elsewhere.
- Two packed-goal acceptance checks are disabled as known failures.

**Visual inspection** (contact sheets at 200 and 2,000 units): on jagged walls
Legion units slide past the teeth and settle; held units are stationary
(red) rather than jittering. At the 2,000-unit doorway the waiting crowd holds
still in a funnel while Retail's whole crowd keeps pushing, but Legion feeds
the six-cell door in single file, under-using it. In 2,000-unit opposing
columns Legion's meeting block stays still instead of churning, yet about
1,550 units remain deadlocked there and only the edges flow.

**CPU.** One five-mode timing session at `a78d194` (before the last review
fixes; same pinned-P-core method and noise as above): Legion's mean tick time
was 0.96× the median of the other modes, and it was cheapest or within 2% of
cheapest in 8 of 30 cases, mostly at 200 units. At 2,000 units it costs
20–50% more than the cheapest mode per tick: doors 3.99 ms vs 2.77
(Cooperative), opposing columns 3.83 vs 2.77 (Flowfield), shared goal 3.40 vs
2.45 (Flowfield), while delivering the most arrivals in each. Raw results:
[five-mode timing](navigation-evidence-2026-10-05/final/).

**Real data.** Two headless 9,000-tick games on Inner Circle with Legion gave
the same hash with no errors; the armies there were small.

### Legion round 3 (after user play-testing)

Play-testing at 8x found hitches on group moves. A whole-tick profile of the
user's replay traced them to whole-map plane rebuilds on every static change
and to up to 4M field relaxations in one tick. Both are fixed (incremental
planes with local region repair; a 384k per-tick field quota with near-first
growth); ticks over the 8x budget fell from 127 to 2 (match-start setup only).
The `TAK_PHASE` profiler previously excluded navigator upkeep and zeroed burn
time; it now times the whole tick. Visible sliding in group moves remains;
see [Legion](legion-pathfinding.md#round-3-lag-spikes-and-group-move-jank-2026-10-05).

### Legion round 4

Legion's per-tick cost fell 10–29% at 2,000 units with identical behaviour
(flat member lookups); it is now cheaper than Retail+, Flowfield and
Cooperative in all five timed 2,000-unit cases and cheapest overall on shared
goals, with Retail 10–48% cheaper elsewhere. A passage lane grid lets doors
carry three lanes: Legion now beats every mode on doors and bridges at 2,000
units and on doors at 12,000 ticks. Sliding is reduced in crossing traffic but
not on open ground or along walls; see
[Legion](legion-pathfinding.md#round-4-cpu-chokepoints-and-sliding-2026-10-05).

## Remaining weak scenarios

- **Exploration at 2,000 units:** no mode settles anyone in 6,000 ticks.
  Legion crosses a few units; the others none.
- **Maze at 2,000 units** is congestion-bound in every mode (11–43 crossed,
  0 arrived).
- **Retail+ saturated doorway:** improved, but still about half of Retail's
  crossings at 2,000 units; recovery-1000×4 and sharedgoal-1000×4 arrivals fell.
- **Flowfield/Cooperative open ground at 2,000 × 1 and 500 × 4:** everyone
  crosses, but only 88–375 settle; per-destination fields near goals and the
  32-destination cap now limit them. They also settle more slowly after rapid
  order replacement.
- **Legion opposing columns** at 2,000 × 1, 500 × 1 and 250 × 8 still trail
  the best mode on crossings, and Legion's two packed-goal acceptance checks
  are disabled as known failures (61/64 and 58/64).
- **Legion CPU** at 2,000 units is 20–50% above the cheapest mode per tick,
  while it delivers the most arrivals.
- The `singleunit` fixture cannot lay out 2,000 × 1, 500 × 4 or 250 × 8.

## Correctness and review

Three independent reviews ran. The first found the timing driver never
alternated leaders and that `combine` could pair separate timing sessions;
both are fixed and tested. The second found no simulation defect but showed
two new guards untested; both now have tests that fail when the guard is
removed. The third reviewed Legion; its findings were fixed or, in one case,
disproved (dead units were already cancelled). Checks on the integrated
candidate:

- Full Release CTest on the final head: 281/281 enabled tests pass; the two
  Legion packed-goal checks are disabled as documented known failures.
- `navigation_determinism`: all five modes match their golden checkpoints,
  serial and worker runs agree. Retail's golden never changed.
- Retail per-tick traces are byte-identical to the frozen baseline on every
  compared scenario, including the five acceptance scenarios.
- Each behaviour-equivalent CPU change was checked with 52 per-tick trace
  comparisons (13 scenarios × 4 modes) plus large-population hashes.
- `tools/check-detmath.sh` passes.
- Real-data headless game (Inner Circle, AI on both sides, server-side
  referee) in Legion mode: two 9,000-tick runs reached the same hash
  `a3914c2d03bdd772` with no errors. Only 14 units were alive at the end, so
  this checks stability and determinism, not large-army behaviour. An earlier
  stall in that harness came from the host's saved 7680 × 2160 display
  settings and reproduced identically in Retail; a clean settings folder fixed
  both.
- Movement was inspected on contact sheets for doors, maze, opposing columns,
  shared goals, exploration, jagged walls, sealed pockets and gated crowds.
- Not run: Windows/macOS runtime (CI only), an idle-host serial timing pass,
  and a large real-data Legion battle.

## Compatibility

Protocol **228**: mode byte 4 is Legion, and the Retail+, Flowfield and
Cooperative simulations changed. Clients and servers must match; protocol 227
peers and replays are refused. Replay format 11's layout is unchanged.
Unknown mode bytes (5 and above) are rejected everywhere. Saved preferences
accept 4; invalid values fall back to Retail.

## Recommendation

- **Keep Retail as the default.** It is the compatibility baseline and is
  unchanged.
- **Legion** is the strongest choice for moving armies: it leads or ties on
  arrivals in almost every measured case, keeps trapped units still, slides
  along jagged walls and nearly eliminates spinning. Choose it when movement
  quality matters more than CPU at very large populations. It is experimental
  and new; it has had one light real-data game, not a full match.
- **Flowfield / Cooperative** are now usable with large independent armies
  and lead on bridge and large doorway crossings, at higher tick cost than
  before because their crowds now actually move.
- **Retail+** suits medium crowds that want native routes with local yielding;
  avoid it for very dense chokepoints.
