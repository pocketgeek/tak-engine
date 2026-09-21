# Large-army simulation performance

Current target (revised by the user): **16,000 units at sustained 1.0×**,
while preserving retail pathfinding. New performance tests are capped at
16,000 requested units; mixed-army fixtures also create eight monarchs.
The 40,000-unit target and measurements below are historical, superseded by
this test-scope change. Gameplay unit limits are unchanged.

Measurements below are local on an Intel Core Ultra 9 275HX. They exclude setup
and rendering, include periodic lockstep hashes, and are subject to system load.
1.0× is 30 simulation ticks per second (33.3 ms average budget).

## Changes

- Tick-local spatial buckets narrow body queries to nearby candidates. Exact
  footprint and yard checks remain unchanged; each cell retains the last
  eligible occupant in the original vector order, regardless of bucket order. Movement updates bucket memberships, spawning and
  allocation-slot sorting invalidate them, and queries outside the tick retain
  the full scan. Old bucket memberships can only add false positives.
- Ground neighbor scans and mobile placement reuse a body snapshot across their
  footprint cells rather than repeatedly scanning units.
- Integer square root starts Newton iteration with a bit-length upper bound,
  eliminating unnecessary divisions while retaining the exact integer result.
- Direction calculation stops only when an exact bound on all remaining integer
  CORDIC updates proves they cannot change the rounded angle. Steering and
  acceleration reuse their aim/direction when their inputs are identical.
- AI factory-door clearance builds an ordered factory list once per decision,
  instead of scanning the entire army for every idle unit.
- Per-unit script updates use a direct ID lookup into the existing script map.
  The map retains ownership and ordered hash traversal; the lookup is cleared
  on script deletion and replay reset. Script execution order is unchanged.

These are derived-data/algorithm optimizations, not changes to pathfinding
budgets, tick cadence, collision rules, terrain grades, or AI decisions.

## Results

| Workload | Result |
| --- | --- |
| 40,000 synthetic movers, 120 simulated seconds | 110.7 seconds wall time, **1.08×**; all 40,000 alive, moving, and displaced at completion |
| Same movement fixture, reusable tool, 20 simulated seconds | 16.9 seconds wall time, **1.18×**; all 40,000 still moving |
| Mixed retail armies, eight Absurd AIs, 40,008 initial units, 10 simulated seconds | 32.1 seconds wall time, **0.31×**; 31,091 survivors |
| Mixed battle before factory-list optimization | 67.2 seconds wall time; identical checkpoint hashes |

The battle loses units and therefore does **not** demonstrate sustained 40,000
live units. The movement fixture uses simple unarmed units and flat terrain; it
does **not** demonstrate 40,000 scripted units in combat. A preliminary patrol
fixture left its units idle and was discarded as performance evidence.

## Fidelity checks

- Debug, Release, and optimized Debug all-target builds; all three 36-test suites passed.
- Debug suites ran with `TAK_VERIFY_BODY_INDEX=1`, comparing indexed body queries
  against the original full scan cell-for-cell.
- All 1,800 per-tick hashes in the eight-AI spawn-ramp comparison matched the
  pre-optimization baseline through 7,697 allocated unit records.
- All ten 30-tick checkpoints of the mixed 40,008-unit battle matched across the
  AI optimization; the reusable tool reproduces them.
- The 450-Hunter Crusades crowd comparison matched native movement/mission fields
  through 1,800 ticks; all 15 search/movement oracle scripts passed.
- Three million random direction vectors plus axis/extreme combinations matched
  the full CORDIC reference. Permanent math tests cover 200,000 random vectors,
  exact square-root invariants, powers of two, square boundaries and UINT64_MAX.
- GCC/Clang O0/O2/O3 fixed-math golden remains `8adc4762a852fadd`. ARM was skipped
  because target headers/libc are unavailable.

## Reproduce

`simperf` is an explicit benchmark target, not part of CTest. Release is suitable
for it: it does not depend on the client's Debug-only headless CLI.

```sh
cmake --build build
build/simperf --mode movement --units 16000 --ticks 3600
build/simperf --mode match --units 16000 --ticks 300 --data assets/game
TAK_VERIFY_BODY_INDEX=1 build-dbg/simperf --mode movement --units 400 --ticks 60
```

`--crusades` selects that balance for the mixed match. The output reports actual
initial/remaining unit counts, moving units, speed, median/p95/max tick times,
and periodic hashes. It never labels a low-count or idle run as meeting the target.

Evidence logs: `/tmp/tak-40k-sustained-move.log`,
`/tmp/tak-simperf-movement-final.log`, `/tmp/tak-simperf-match-final.log`,
`/tmp/tak-ai-perf-{debug,release}-tests.log`, `/tmp/tak-body-index-crowd.log`,
`/tmp/tak-performance-search.log`, `/tmp/tak-performance-determinism.log`.

Remaining work is the mixed battle's simulation cost and validation with longer
battles, other terrain/balance settings, and client/referee runs on both remote
hosts. Earlier remote stress runs used the slower implementation; their timeouts
are not passes for this implementation.

Both hosts now also have an isolated optimized Debug server at
`/home/pocket_geek/takserver.perf170`. A one-simulated-minute six-scenario remote
stress check, including the spectator case and validation gates, completed with
those binaries: all eight seats completed and no desyncs were reported. The
spectator scenario checks flow only. Its log is `/tmp/tak-performance-remote.log`. This is a short
regression check, not a replacement claim for the earlier 45-minute sweep.

## Follow-up profiling

`TAK_PHASE=1 TAK_PHASE_MS=0` now separates script and movement time from the
remaining per-unit work. In the large mixed battle, representative late ticks
spend roughly 43–55 ms in movement and 16–28 ms in scripts. These are
instrumented measurements, with per-unit clock overhead, rather than an
uninstrumented speed claim. The direct script lookup has not yet demonstrated
a separately attributable speed improvement.

All ten mixed-battle hash checkpoints still match after this lookup change.
All 36 Release tests and all 36 Debug tests pass; the latter again enabled
`TAK_VERIFY_BODY_INDEX=1`. Evidence: `/tmp/tak-script-index-match.log`,
`/tmp/tak-script-index-phase.log`, and
`/tmp/tak-script-index-{release,debug}-tests.log`. The completed remote
suite used the preceding optimized build, without this script lookup change.
All five seated scenarios passed: `h-lake`, `h-everything`, `h-stress`,
`2h-stress`, and `2h-orders-stress`. The spectator stress case completed its
flow check, without a hash comparison. Both detector validation gates passed.

A subsequent uninstrumented mixed-battle run completed in 31.35 seconds
(**0.319×**, median 92.66 ms, p95 130.69 ms), with the same ten checkpoint
hashes. This is within the run-to-run variation of the previous result; it
does not establish a script-cache speedup. Log:
`/tmp/tak-script-index-serial.log`.

## Occupancy query follow-up

Body queries now retain the highest vector-position occupant directly per cell,
removing the temporary candidate vector and sort. Footprint scoring similarly
selects the lowest-ID failing body directly, preserving the distinction between
a mobile blocker (2) and a structure (0). A regression exercises both ID orders
against reversed spatial order. Both changes preserve the original winner rules.

A saved-binary comparison measured 29.88 seconds before the body-query change
and 29.22 seconds on its repeat run afterward. The combined follow-up measured
31.45 seconds during other build activity. These small differences are not
strong enough to establish a reliable improvement. All ten battle checkpoints
match in every run. All 36 tests pass in Release, Debug (with full-scan body
verification), and optimized Debug; all 15 native search/movement comparisons
pass. Logs are `/tmp/tak-body-stamp-{baseline,match,match-repeat}.log`,
`/tmp/tak-body-select-{match,release-tests,debug-tests,o2-tests,search}.log`.

A CPU sample profile identifies more specific remaining costs: `findTarget`
accounts for 14.1% of samples, `searchBodyRect` 12.7%, script updates 15.2%
including their callees, and navigation exploration 6.0%. This profile includes
setup and periodic state hashes, unlike tick-only phase timings. It suggests
examining target candidate traversal next, while preserving traversal order,
random draws, and decisions. Profile: `/tmp/tak-mixed-cpu.prof`; text report:
`/tmp/tak-mixed-cpu.txt`. No claim of 40,000-unit mixed-combat completion follows
from these results.

## Target acquisition follow-up

Target acquisition now rejects candidates by range and leash before its pure
weapon/category lookup. The spatial grid also records a conservative player
mask per cell; acquisition skips cells containing only allies while retaining
the existing linked-list order for all remaining candidates. Capturing a unit
invalidates the masks until the next grid rebuild. Unrepresentable player IDs
disable the shortcut. Candidate scoring, random draws, and acquisition cadence
are unchanged. Navigation exploration computes alliance masks once per player
per update instead of once per unit.

The isolated damage-lookup reorder measured 26.24 seconds against 26.76 seconds
for its saved-binary baseline. These measurements should be compared within
this pair, not against older runs under different system load. Combined-change
measurements and checks are recorded in `/tmp/tak-target-mask-*.log`.

The combined change measured 24.21 seconds, followed by a saved-baseline run
of 26.53 seconds and a repeat of 23.96 seconds (**0.417×**). That is about 10%
less wall time in this comparison. All ten checkpoints match; final population
remains 31,091 from 40,008 initial units. All 36 Release and Debug tests pass,
with full-scan body verification enabled in Debug. This improves the mixed
workload but still falls well short of the 1.0× target.

Optimized Debug also passes all 36 tests. The cross-compiler fixed-math check
retains golden `8adc4762a852fadd`; ARM remains skipped for missing target
headers/libc. A follow-up CPU profile records 221 self samples in `findTarget`
versus 452 before (100 Hz sampling), consistent with reduced acquisition cost.
Its relative share falls from 14.1% to 8.0%. Occupancy queries and scripts now
lead the profile. Logs: `/tmp/tak-target-mask-{o2-tests,determinism,cpu}.log`;
profile report: `/tmp/tak-target-mask-cpu.txt`.

The 40,008-initial-unit Crusades fixture also matches all ten saved-baseline
checkpoints through tick 300 (final hash `e4e226d74d6d2092`, 34,493 survivors).
Logs: `/tmp/tak-target-mask-crusades.log` and
`/tmp/tak-target-mask-crusades-baseline.log`. Its timing overlapped other work
and is used here for fidelity, not for attributing a speedup.

## Parallel exploration

The 4×4 occupancy-bucket experiment did not improve the repeat comparison
(24.01 seconds versus 23.86 seconds for 8×8), so it was reverted. Evidence:
`/tmp/tak-body4-{match,baseline,repeat}.log`.

Navigation exploration now partitions units across up to four workers for
large armies (at least 2,048 unit records per worker). Each worker updates
distinct sight-footprint records and writes its own reveal bitmap. After joining,
the main thread combines those bitmaps with bitwise OR. Worker completion order
cannot change the union; no shared bitmap writes occur during the worker phase.
The existing serial-thread option bypasses this, and `simperf --serial` exposes
it for direct comparisons. Script and movement execution remain ordered.

The new `exploration_parallel` test compares serial and parallel exploration
masks and complete world hashes for 4,096 units across varied terrain, map
edges, changing heights, overlapping sight, construction exclusions, and an
alliance change. It passes, including under ThreadSanitizer with no race report.
All 37 Release and Debug tests pass; Debug still verifies body queries against
a full scan. All ten large-battle checkpoints match the preceding implementation
and the serial run. Evidence: `/tmp/tak-exploration-parallel-{tests,debug-tests}.log`,
`/tmp/tak-exploration-tsan-test.log`, and
`/tmp/tak-exploration-{parallel-match,serial-match,parallel-repeat}.log`.

Initial unpinned measurements were 25.49 seconds parallel, 25.30 seconds serial,
and 23.23 seconds on a parallel repeat (**0.431×**). Builds overlapped the first
run; these results motivate a controlled comparison rather than establishing
an isolated speedup. The 40,000-unit mixed-battle 1.0× target remains unmet.

A controlled pair restricted both runs to the same four performance cores
(`taskset -c 0-3`) measured **24.22 seconds serial versus 23.09 seconds parallel**,
a **4.7% wall-time reduction** and **0.433×** simulation speed. All ten hashes
match both each other and the preceding implementation. Logs:
`/tmp/tak-exploration-pinned-{serial,parallel}.log`. This covers a battle starting
at 40,008 units and ending at 31,091; it is not sustained 40,000-unit evidence.

```sh
taskset -c 0-3 build/simperf --mode match --serial --ticks 300
taskset -c 0-3 build/simperf --mode match --ticks 300
```

Select appropriate CPU affinity for the test machine; CPU IDs above are specific
to this workstation. ThreadSanitizer verification was built with
`-O1 -g -fsanitize=thread` and run as
`/tmp/tak-exploration-tsan/exploration_parallel_test`.

All-target Release, Debug, and optimized Debug builds are current. Optimized
Debug also passes all 37 tests (`/tmp/tak-exploration-o2-tests.log`). The large
Crusades battle matches all ten pre-parallelization checkpoints as well
(`/tmp/tak-exploration-parallel-crusades.log`, final hash `e4e226d74d6d2092`).
The remote servers have not yet received this parallel exploration change;
the earlier remote suite remains evidence for its explicitly recorded build.

## Script slots and unit layout

The VM tick loop checks each thread slot's current flags before entering the
execution routine. It still checks slots in order, so a thread started by an
earlier slot runs at the same point. The native oracle matches 315 programs
and 6,554 complete thread/static/RNG boundaries; 6,000 native piece-command
comparisons also pass. The isolated speed difference was small: 22.94 seconds
on a repeat versus 23.16 seconds for its saved baseline. Logs:
`/tmp/tak-script-slots-{native-threads,native-pieces,match,baseline,repeat}.log`.

Frequently read broad-phase fields (type, position, death state, transport,
flight mode and construction state) now sit together near the front of `Unit`.
The field values and field-wise state hashing are unchanged. This avoids
fetching scattered parts of each large record during nearby-unit scans.
All-target builds and all 37 tests pass in Release, Debug (with full-scan body
verification), and optimized Debug. All ten battle checkpoints match in both
balance modes.

With both binaries restricted to CPU cores 0–3, the layout repeat measured
**20.74 seconds versus 23.69 seconds** for the saved baseline: **12.5% less wall
time**, **0.482×** simulation speed, median tick 58.97 ms. The first layout run
was 21.34 seconds. This remains a 40,008-initial-unit battle with 31,091 survivors,
not proof of sustained 40,000 units at 1.0×. Logs:
`/tmp/tak-unit-layout-{match,baseline,repeat,crusades}.log` and
`/tmp/tak-unit-layout-{release,debug,o2}-tests.log`.

Both remote hosts now have a separate optimized Debug test binary at
`/home/pocket_geek/takserver.perf170-layout`, SHA-256
`040095dbee1b26aabaa3cfe4ffa7cdfdda1f3842ec574c3ac7b0b89305c3097b`.
The six-scenario one-minute stress suite is running with the current optimized
client; results are pending in `/tmp/tak-unit-layout-remote.log`.

Preparing that suite uncovered a cleanup bug: the negative override test
searched for the literal executable name `takserver`, leaving separately named
test servers running. It now records and stops the exact spawned PID, including
through the existing cleanup trap. The previous idle mismatch-test server
(PID 52406, port 7891 on tak.pgnet.us) was identified by its start time and
matching negative-test log, then stopped. Shell syntax validation passes; the
new remote run will exercise this cleanup path.

## Zero-word hashing and script-index experiment

A fresh profile after the unit-layout change attributed 7.3% of CPU samples
to state hashing (`/tmp/tak-layout-current-profile.txt`). Script thread records
contain many zero words. Hashing each such word now multiplies by the FNV prime
to the eighth power modulo 2^64, exactly equivalent to its eight zero-byte hash
steps. Every word still participates, including unused stack words.

The repeat measured 22.54 seconds against 23.20 seconds for its saved baseline
(about 2.8% less wall time); these runs overlapped remote testing. All ten
checkpoints match. A later profile measured 125 hash samples versus 186 before,
supporting a reduction in hashing cost. Logs: `/tmp/tak-zero-hash-{match,baseline,repeat}.log`,
`/tmp/tak-slot-index-profile.txt`.

An occupied-thread-slot index was also prototyped. It passed all 6,554 native
thread/static/RNG boundaries, but its timing benefit was not reliable and the
profile did not show reduced inclusive script cost. **That index was removed.**
The existing sequential empty-slot check remains. A new saved-script regression
was retained: restoring a sleeping child and blocked parent must reproduce
uninterrupted execution, including the parent's wakeup tick.

The remote suite using `takserver.perf170-layout` has now completed: all eight
seats finished, five seated scenarios reported no desync, and spectator stress
passed its flow-only check. Both validation gates passed. No test-server
processes remained on either host; override-move and shaping records were empty.
This validates the accumulated layout/parallel-exploration/script-slot changes
in that build, before the subsequent zero-word hash optimization. Evidence:
`/tmp/tak-unit-layout-remote.log`, `/tmp/desync-remote-984535/`.

After removing the index experiment, all targets were rebuilt in Release, Debug,
and optimized Debug; all three 37-test suites pass. Debug again enables
`TAK_VERIFY_BODY_INDEX=1`. The saved-script continuation regression passes in
these final builds. Logs: `/tmp/tak-zero-hash-final-{release,debug,o2}-tests.log`.

With remote tests and builds finished, a controlled comparison on cores 0–3
measured **21.32 seconds before versus 20.45 seconds after** the hash change:
**4.1% less wall time**, **0.489×** simulation speed. All ten standard-balance
checkpoints match the preceding implementation. Logs:
`/tmp/tak-zero-hash-idle-{baseline,final}.log`. The battle still starts at 40,008
and ends at 31,091 live units, and the 1.0× goal remains unmet.

The final Crusades run also matches all ten checkpoints, ending at
`e4e226d74d6d2092` with 34,493 survivors. Evidence:
`/tmp/tak-zero-hash-final-crusades.log`.

## Active animation pieces

Script state now keeps a derived 64-bit index of active animation pieces. COB
commands maintain the index; completed motion clears its bit. Pieces are still
updated in ascending order, and pieces above index 63 retain the full scan.
Restoring raw state rebuilds the index. Zero-elapsed updates still execute the
VM and immediate commands; timed piece motion remains unchanged. Direct edits
to public piece records must call `rebuildPieceIndex()`.

The controlled standard-balance comparison on cores 0–3 measured 19.57 seconds
on the first run, 20.50 seconds for the saved baseline, and **19.15 seconds on
the repeat**: **6.6% less wall time**, **0.522×** simulation speed, median tick
55.99 ms. All ten battle checkpoints match; the Crusades checkpoints also match.
Logs: `/tmp/tak-piece-index-{match,baseline,repeat,crusades}.log`.

All 37 tests pass in Release, Debug, and optimized Debug. Debug enables both
`TAK_VERIFY_BODY_INDEX=1` and `TAK_VERIFY_PIECE_INDEX=1`. A 70-piece regression
compares every motion field against the full-scan implementation through 3,000
commands/updates, immediate motion, zero elapsed time, boundary pieces 63/64,
and saved-state restoration. Native emulation also matches 301 complete
thread/animation/RNG boundaries each for Thirsha (`zonhunt`) Go and StartBuilding,
Beast Lord (`zonlord`) StartBuilding, Hunter (`zonter`) walk, and `arakeep` StartBuilding, using synthetic valid saved state and controlled
engine callbacks. This checks those script traces, not complete rendered scenes.
Evidence: `/tmp/tak-piece-index-{release,final-debug,o2}-tests.log` and
`/tmp/tak-piece-index-native-{hunter,monarch,keep}.log`.

The index does not change hashed animation fields, script order, random draws,
pathfinding budgets, or movement cadence. The 40,000-unit 1.0× goal remains
unmet; these battles still lose units during measurement.

With builds finished, the controlled Crusades comparison measured 22.99 seconds
before versus **22.19 seconds after**, **3.5% less wall time** and **0.451×**
simulation speed. Both runs have identical checkpoints and end at 34,493 live
units. Logs: `/tmp/tak-piece-index-crusades-{baseline,final}.log`. The standard
balance repeat above reaches 0.522×; neither result meets the full target.

## Lazy AI visibility checks

The Crusades profile exposed `Controller::nearestVisibleEnemy` at 18.1% of
CPU samples. It tested every enemy against the AI's observers before sorting
visible positions, even though callers usually need only the nearest one.
Candidates now sort first using the identical distance/x/z key, then visibility
is checked until a target is found. The first 16 visible candidates remain the
reachability limit; visibility arithmetic, observer eligibility, target ties,
and path queries are unchanged.

A sequential before/after comparison on cores 0–3 measured Crusades at
**21.406 → 17.098 seconds**, **20.1% less wall time**, or **0.467× → 0.585×**.
Standard balance was effectively unchanged: **18.810 → 18.832 seconds**
(0.532× → 0.531×). Both modes match all ten baseline hashes exactly and end
with the same surviving units (34,493 Crusades; 31,091 standard). These are
10-second mixed battles starting at 40,008 units, not sustained 40,000-live-unit
evidence. The 1.0× goal remains unmet.

All targets rebuilt in Release, Debug, and optimized Debug; all 37 tests pass
in each, with body/piece index verification enabled in Debug. Evidence:
`/tmp/tak-ai-visible-{crusades,standard}-{baseline,final}.log` and
`/tmp/tak-ai-visible-{release,debug,o2}-tests.log`.

## Sustained mixed-army workload

`simperf --mode patrol --units 40000 --ticks 3600` uses the same retail
ground/air roster and generated flat map as `match`, but allies all eight
players and orders patrols across the map. Unit scripts, normal pathfinding,
collision handling, exploration, and periodic state hashes run normally.
There are no AI controllers or hostile combat in this additional workload;
it supplements rather than replaces the mixed battle. Water units and rendering
are not covered. The tool records the minimum live population every tick, and
reports final moving/displaced counts. Population accounting is included in
wall time, outside the individual tick durations.

The first full run retained all **40,008** units for 120 simulated seconds,
taking **177.694 seconds (0.675×)**. However, only **7,717** units moved or
displaced at completion, so this is NOT valid evidence of 40,000 active movers.
A small 16-unit reproduction likewise moves only four units. Investigation
points to the two-point patrol command's unmarked goal boundaries: the current
leg can resolve to the return point at the unit's starting position. This must
be resolved and the workload rerun before using it as a movement baseline.
Logs: `/tmp/tak-patrol-40000-standard.log`, `/tmp/tak-patrol-small.log`.
Debug and optimized-Debug harness builds also completed. No simulation code
changed in this benchmark addition. The unrelated Crusades roster report was
withdrawn by the user; its build tables remain unchanged.

### Patrol command correction (protocol 171)

The isolated ground-patrol regression fails before the fix and passes after:
it observes travel to the remote endpoint, return home, and another outbound
leg. Both orders now carry goal boundaries. No search, steering, collision,
or scheduler code changed. Protocol 171 prevents mixing this changed command
behavior with protocol 170 peers. All targets rebuilt in all three build trees;
all 37 tests pass in Release, Debug, and optimized Debug, with index verifiers
enabled. GCC/Clang math checks retain `8adc4762a852fadd`; ARM checks skip for
missing target headers/libc. Both mixed battles retain all ten prior hashes.
Evidence: `/tmp/tak-patrol171-*-tests.log`,
`/tmp/tak-patrol-regression-{before,after}.log`,
`/tmp/tak-patrol171-match-{standard,crusades}.log`.

The corrected 120-second patrol run is recorded in
`/tmp/tak-patrol171-patrol-standard.log`. Its wall timing overlaps brief client
builds and a rendered conjure check, so treat it as workload validation, not a
controlled speed comparison. Remote protocol-171 validation remains outstanding.

The conjure sparkle follow-up is display-only: a flying builder's active-site
flag now permits its build sparkle while it hovers. The old walking gate hid
Thirsha's effect during hover motion. A Crusades capture on Ulasem Arena at six
seconds shows sparkles over both Thirsha and the Hunter construction site:
`/tmp/tak-conjure-sparkles.png`. All three client builds include this fix.

The corrected patrol run completed: **40,008 minimum/final live units**,
**39,413 moving**, **39,971 displaced**, over 120 simulated seconds.
Wall time was **228.641 seconds (0.525×)**, median tick 57.461 ms and p95
77.135 ms. Final hash: `78c798db6de2231f`. Unlike the first run, this validates
a sustained large moving population. It still excludes hostile combat, water
units, AI, and rendering; the full goal is not met. Besides the brief client
builds/capture, the last portion overlaps an experimental Debug build and its
tests on other cores. Use this result for workload coverage, not an isolated
optimization comparison.

## Rejected occupancy deduplication experiment

Processing each body only through its first current bucket in a query was
semantically exact: all 37 tests passed in three builds, Debug compared indexed
occupancy against the full scan, and all ten hashes matched in standard battle,
Crusades battle, and the corrected patrol workload. It did not improve timings
on cores 0–3: standard battle **19.089 → 19.229 seconds**, Crusades
**17.353 → 17.453**, patrol **21.916 → 21.951** (300 ticks each). The extra
bucket-bound checks brought no measurable benefit, so the change was reverted.
The existing body index and exact last-unit-wins stamping remain unchanged.
Logs: `/tmp/tak-body-dedup-{match,patrol}-{standard,crusades}-{baseline,final}.log`
(patrol has only a standard-balance pair).

## Shared exact rotation coefficients

The corrected patrol profile attributes 2.5% of CPU samples to 128-bit division,
with `retailRotatePair` at 3.7% inclusive. Its Q60 sine/cosine evaluation now
runs once for each 16-bit heading and stores the original results in a shared,
immutable 1 MiB table. Pair multiplication and nearest/even rounding remain
unchanged; initialization is thread-safe and heap-backed. No approximation or
host trigonometric function was introduced.

A permanent regression covers all 65,536 headings with two coordinate pairs,
including signed boundaries, against golden `930f68c69f30a3b6` produced by the
preceding per-call calculation in GCC and Clang. All 37 tests pass in Release,
Debug, and optimized Debug, with body/piece index verifiers enabled in Debug.
Native emulation also matches 10,030 rotations including overflow/rounding.
The existing cross-compiler math golden remains `8adc4762a852fadd` (ARM skipped
for missing target headers/libc). Evidence:
`/tmp/tak-rotation-cache-{release,debug,o2}-tests.log`,
`/tmp/tak-rotation-cache-native.log`, and
`/tmp/tak-rotation-cache-determinism.log`.

Sequential comparisons on cores 0–3 show **2.5–2.9% less wall time**:
standard battle **20.266 → 19.749 seconds**, Crusades battle
**18.420 → 17.879**, and patrol **23.136 → 22.480** (300 ticks each).
All ten checkpoints match in each pair. Patrol retains 40,008 living units,
39,950 moving and 39,971 displaced at ten seconds. Absolute timings differ
from earlier sessions, so the comparison is against each run's paired baseline,
not the fastest earlier run. The cache is retained; these results still do not
meet 1.0×. Logs:
`/tmp/tak-rotation-cache-{match,patrol}-{standard,crusades}-{baseline,final}.log`
(patrol has only the standard-balance pair). All three build trees are current.

## Cached footprint bounds

The existing body index now also stores each unit's exact footprint bounds in
a compact derived array (16 bytes per unit). Movement updates those bounds even
when bucket membership stays unchanged. Queries reject nonintersecting bounds
before loading the full unit/type; eligibility, yard handling, and occupant
precedence remain unchanged. The Debug verifier independently recomputes live
bounds and compares the complete cell results against the uncached full scan.

All 37 tests pass in three builds; cross-compiler math checks agree. Every
checkpoint matches in standard combat, Crusades combat, and patrol. Comparisons
on cores 0–3 show a small consistent gain: standard **19.860 → 19.662 seconds**,
Crusades **18.023 → 17.925**, patrol **22.413 → 21.971** (300 ticks).
Reversing run order confirms patrol **22.453 baseline vs 22.016 cached** and
standard **19.807 vs 19.654**, again with identical hashes. The cache is retained:
roughly **2% less wall time in patrol**, under 1% in these battle repeats. This
does not establish sustained 1.0×. Evidence:
`/tmp/tak-body-bounds-{release,debug,o2}-tests.log`,
`/tmp/tak-body-bounds-{match,patrol}-{standard,crusades}-{baseline,final}.log`,
and `/tmp/tak-body-bounds-{match,patrol}-repeat-{baseline,final}.log`.

The Ubuntu-compatible protocol-171 server has been rebuilt with **Debug -O2 -g**
and copied to both hosts as `/home/pocket_geek/takserver.perf171-bounds`.
Local and both remote SHA-256 values agree:
`3a65fa577fbbc7ba5f83ea5938af990001e794173129c10d59a22c21e9666394`.
The six-scenario one-minute sweep (including detector validation) is running
against the current optimized-Debug client, ports starting at 19600. Results
remain pending in `/tmp/tak-perf171-remote.log` and
`/tmp/desync-remote-1221216/`; these are isolated test binaries, not a production
server replacement. Do not relink the client while that sweep is active.

A separate profile-guided compiler experiment is prepared in
`/tmp/tak-pgo-build`, with Release, `TAK_BUILD_CLIENT=OFF`, and
`-fprofile-generate=/tmp/tak-pgo-data -fprofile-update=atomic`.
The instrumented `simperf` target built successfully. Training and optimized
measurements have not run yet; normal build trees and remote test binaries are
unchanged. Atomic counters allow safe instrumentation of the parallel work;
training overhead must not be mistaken for normal simulation performance.

The protocol-171 remote sweep has completed: **all eight clients completed**,
five seated-player scenarios reported no desync/referee disagreement, and the
spectator scenario passed flow control (no hash comparison). The planted desync
and gameplay-data mismatch validations both passed. The two-player stress
clients ended together at tick 1810/hash `32ff16ef13bae8b4`. The live-orders
clients ended at different ticks (1891 and 1810), so their terminal hashes are
not comparable; this case proves agreement through the referee's common-tick
comparisons, not a reproducible final hash. Both used optimized `-O2` builds
on the servers, paired with the optimized-Debug local client.

Cleanup is verified: neither host retains a `takserver.perf171-bounds` process,
and the moved-aside override and shaped-interface records are empty. Evidence:
`/tmp/tak-perf171-remote.log` and `/tmp/desync-remote-1221216/`.

PGO training has started with the 40,000-unit standard battle for 90 ticks,
using `--serial` to avoid profiling-counter contention across worker threads.
This is training, not a speed measurement. Current log:
`/tmp/tak-pgo-training-standard.log`; generated counters go to
`/tmp/tak-pgo-data`. No profile-use build has been produced or adopted yet.

PGO training and evaluation completed. Trained standard/Crusades battles and
patrol for 90 serial ticks each, plus a 4,104-unit parallel patrol for 30 ticks.
All training checkpoint hashes matched normal Release. Rebuilt the same scratch
build with `-fprofile-use=/tmp/tak-pgo-data -fprofile-correction
-fprofile-partial-training`. All 29 registered client-off tests passed
(`/tmp/tak-pgo-tests.log`). Missing profiles for untrained modules are expected;
no coverage mismatch was observed.

Sequential paired 300-tick runs on cores 0–3 matched all ten checkpoint hashes
in each workload. Standard battle wall time: 18.737 → 18.255 s (2.57% reduction,
0.548×); Crusades: 16.805 → 16.430 s (2.23%, 0.609×); patrol: 21.022 → 20.312 s
(3.38%, 0.492×). Patrol retained all 40,008 units; battles lost units. These
10-simulation-second measurements do not establish sustained 1.0× performance.
Logs: `/tmp/tak-pgo-eval-*.log`. PGO remains experimental: normal builds and
remote binaries have not been replaced.

## Skip terrain checks for permanently revealed navigation cells

Current patrol profiling (`/tmp/tak-current-patrol.prof`) measured navigation
exploration at 14.9% of sampled CPU. The retail sight helper now accepts an
optional per-cell visit predicate, defaulting to the original full traversal.
World exploration skips removals (which never change permanent revelation) and
cells whose relevant viewer bits are already set. Footprint position, eye
height, and active-state updates are unchanged. Worker masks are seeded from
the persistent map before threads start; workers only read/write their own
mask and merge with the same OR afterward.

The exploration regression compares full and shortened traversal on uneven
terrain, off-map centers, changing heights and viewer masks, checking both
revealed cells and footprint state. Existing serial/parallel world checks
remain. All 37 tests pass in Release, Debug (body/piece verification enabled),
and optimized Debug. All targets rebuilt. Native exploration checks match
1,024 height planes, 8,192 footprints, 8,192 sight updates, and 2,048 model-top
cases. GCC/Clang determinism remains `8adc4762a852fadd`; ARM skipped for missing
target headers/libc. Logs: `/tmp/tak-explore-skip-*-tests.log`,
`/tmp/tak-explore-skip-native.log`, `/tmp/tak-explore-skip-determinism.log`.

Paired 300-tick runs on cores 0–3 matched all ten checkpoints in each workload:
patrol 21.917 → 20.826 wall seconds (5.0%, 0.480×, all 40,008 alive);
standard battle 19.573 → 19.182 (2.0%, 0.521×, 31,091 alive);
Crusades battle 17.823 → 17.697 (0.7%, 0.565×, 34,493 alive).
These are short comparisons, not sustained 1.0× evidence. Baseline binary:
`/tmp/tak-simperf-before-explore-skip`; logs:
`/tmp/tak-explore-skip-{patrol,match}-{standard,crusades}-{baseline,skip}.log`.
No protocol change: exploration and world checkpoint hashes are unchanged.

User-requested size comparison, same current binary/map/mix/300 ticks/cores 0–3:
16,008 patrol units: 8.297 wall seconds, **1.205×**, median 25.031 ms,
p95 33.295 ms, max 75.177 ms, 15,917 moving and 15,968 displaced.
40,008 patrol units: 20.646 wall seconds, **0.484×**, median 60.822 ms,
p95 86.889 ms, max 163.907 ms, 39,950 moving and 39,971 displaced.
All units survive in both. The 2.50× unit count takes 2.49× wall time here.
This comparison includes retail scripts/movement/exploration but excludes combat,
AI decisions and rendering, and runs only ten simulation seconds. Logs:
`/tmp/tak-explore-skip-compare-{16000,40000}.log`.

## Sustained patrol check after exploration shortcut

Completed 3,600 ticks (120 simulation seconds), pinned to cores 0–3, with no
concurrent builds or other benchmark jobs. Current Release retained all 40,008
units throughout; final moving count 39,413, displaced count 39,971. Wall time
224.225 seconds: **0.535×**, median 56.360 ms, p95 76.476 ms, max 167.238 ms.
All 120 checkpoints match the earlier protocol-171 patrol log exactly, including
final hash `78c798db6de2231f`. Evidence:
`/tmp/tak-explore-skip-patrol-sustained.log` compared with
`/tmp/tak-patrol171-patrol-standard.log`.

This establishes the current two-minute patrol result, not 1.0× or full-game
performance: combat, AI decisions, rendering and non-flat terrain are excluded.
The earlier 228.641-second run overlapped other work and predates several exact
optimizations, so its timing is not a controlled isolated-gain baseline. Current
short 0.484× and sustained 0.535× samples cover different portions of movement;
do not treat that difference as a new optimization gain. The 40,000-unit 1.0×
goal remains unmet.

## Profile after exploration shortcut; link-time optimization trial

Current 300-tick patrol profile (`/tmp/tak-after-explore.prof`) has 2,329 CPU
samples: searchBodyRect 17.1% self/17.9% inclusive, tickUnitScript 13.8%/19.4%,
stateHash 5.9%, rawSearchGrade 5.8%, RetailVm::run 5.1%, findTarget 4.7%.
Exploration is no longer among the largest costs; thread worker entry accounts
for 3.5%, updateNavigationExploration 1.8% self. Setup is included in these
samples and must not be interpreted as per-tick cost.

Tested current source in a separate Release/client-off build at
`/tmp/tak-lto-build`, configured with
`-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON`. Paired 300-tick 40,008-unit patrols
on cores 0–3: normal 20.662 wall seconds (0.484×), LTO 20.725 (0.483×).
All ten checkpoint hashes match. This gives no evidence of a useful speedup,
so LTO was not adopted and broader deployment testing was not warranted.
Normal client/server binaries and build configuration remain unchanged.
Evidence: `/tmp/tak-lto-configure.log`, `/tmp/tak-lto-build.log`,
`/tmp/tak-lto-patrol-{baseline,lto}.log`.

## Prefetch upcoming unit-script thread state

The script phase now hints the thread-flag cache lines and VM active count for
the unit eight entries ahead, using guarded GCC/Clang `__builtin_prefetch`.
All pointer expressions refer to currently live script objects; no pointer is
retained across script execution or production. Units, script threads, RNG,
construction and movement still execute in their original order. Compilers
without this intrinsic use the original loop. No simulation state or protocol
changes.

Initial paired 300-tick patrols on cores 0–3: 20.710 → 18.532 wall seconds
(10.5% reduction). Reversed-order confirmation: 20.555 → 18.562 (9.7%, 0.539×).
Standard battle: 18.765 → 17.508 (6.7%, 0.571×).
Crusades battle: 17.670 → 15.857 (10.3%, 0.631×).
All ten checkpoints in each repeated workload match the saved prefetch-free
binary. Patrol retains 40,008; standard/Crusades battles end at 31,091/34,493.
Evidence: `/tmp/tak-script-prefetch-patrol-*.log` and
`/tmp/tak-script-prefetch-repeat-{patrol,match}-{standard,crusades}-*.log`.
Baseline binary: `/tmp/tak-simperf-before-script-prefetch`.
These short results are not sustained 1.0× proof.

All-target Release, Debug and optimized Debug builds completed. All 37 tests
pass in each build; Debug uses body-index and piece-index full-scan checks.
Cross-compiler determinism stays `8adc4762a852fadd`; ARM skips for missing
headers/libc. Logs: `/tmp/tak-script-prefetch-{release,debug,o2}-tests.log`
and `/tmp/tak-script-prefetch-determinism.log`.

Sustained validation completed: 3,600 patrol ticks/120 simulation seconds in
193.823 wall seconds, **0.619×**, median 47.654 ms, p95 66.054 ms, max 164.791 ms.
All 40,008 units survive throughout; 39,413 moving/39,971 displaced at the end.
All 120 checkpoints exactly match the preceding prefetch-free sustained run,
including `78c798db6de2231f`. The preceding current-source baseline took 224.225
seconds (0.535×), making this 13.6% less wall time. Both runs used cores 0–3,
without concurrent builds/benchmarks, but were not immediate back-to-back
measurements; the repeated short paired tests independently establish the gain.
Evidence: `/tmp/tak-script-prefetch-sustained.log` and
`/tmp/tak-explore-skip-patrol-sustained.log`.
The full 40,000-unit 1.0× objective is still unmet; this patrol excludes combat,
AI decisions, rendering and non-flat terrain. Retain the cache hint.

## Occupancy rectangle comparisons

The post-script-prefetch profile (`/tmp/tak-after-script-prefetch.prof`) puts
searchBodyRect at 19.3% self/20.3% inclusive CPU samples. Its compact bounds
filter now combines the four pure integer comparison results with bitwise OR,
avoiding chained short-circuit comparisons. The rejection predicate, candidate
order, yard checks and last-unit stamping rule remain identical.

Initial 300-tick patrol comparison: 18.455 → 18.013 wall seconds (2.4%).
Reversed-order patrol: 18.251 → 18.137 (0.6%, 0.551×).
Standard battle: 17.491 → 17.330 (0.9%, 0.577×).
Crusades battle: 15.762 → 15.588 (1.1%, 0.642×).
All ten checkpoints match in every pair. Same cores 0–3, sequential runs,
no concurrent builds. The small spread does not justify claiming a 2.4%
sustained gain; repeated comparisons suggest roughly 1% here. Retain the small
change, with no change to pathfinding behavior or protocol.
Evidence: `/tmp/tak-body-comparisons-patrol-*.log` and
`/tmp/tak-body-comparisons-repeat-{patrol,match}-{standard,crusades}-*.log`.
Baseline: `/tmp/tak-simperf-before-body-comparisons`.

All targets rebuilt in Release, Debug and optimized Debug. All 37 tests pass
in each, with Debug full-scan body/piece verification enabled. GCC/Clang
checksum remains `8adc4762a852fadd`; ARM skips for missing target headers/libc.
Logs: `/tmp/tak-body-comparisons-{release,debug,o2}-tests.log` and
`/tmp/tak-body-comparisons-determinism.log`. No new sustained-speed claim:
the last measured 120-second patrol remains 0.619× before this small change,
and the full 40,000-unit 1.0× goal remains unmet.

## Finer occupancy index experiment (reverted)

Temporary query instrumentation measured 8,571,872 calls covering 151,745,368
requested cells during setup plus 300 patrol ticks. Indexed queries visited
162,215,206 bucket candidates; 42,925,530 passed rectangle intersection (26.5%).
Counts include repeated memberships, not unique unit counts; non-indexed
full scans do not contribute to bucket-candidate counts. Diagnostic source
was removed after building `/tmp/tak-body-query-profile-build/simperf`.
Evidence: `/tmp/tak-body-query-counts.log`.

Tested 2×2-cell buckets backed by an array of heads and packed `{unit,next}`
records, replacing the 8×8-cell vector buckets. Kept tick-local rebuilding,
conservative stale memberships, live eligibility/yard checks and last-unit
precedence. Paired patrols: baseline 17.990 s versus fine index 18.400 s
(2.3% slower). Then restricted each bucket visit to stamping only that bucket's
cells to avoid repeatedly stamping the whole intersection. Reversed-order
pair: clipped prototype 18.458 s versus baseline 18.004 s (2.5% slower).
All ten checkpoint hashes matched in both experiments. Logs:
`/tmp/tak-fine-bodies-patrol-*.log` and
`/tmp/tak-fine-bodies-clipped-patrol-*.log`.

Both prototypes were reverted. `sim.cpp` and `sim.h` were checked byte-for-byte
against their pre-experiment snapshots, preserving script prefetch and the
retained rectangle-comparison improvement. Fewer broad-phase misses alone did
not outweigh finer indexing and traversal costs here. No performance gain or
new sustained-speed claim from this experiment.

After restoring source, all targets rebuilt successfully in Release, Debug and
optimized Debug. Targeted Debug pathblock, conjure and parallel-exploration
checks passed with full-scan body/piece verification enabled. Evidence:
`/tmp/tak-fine-bodies-revert-{0,1,2}.log` and
`/tmp/tak-fine-bodies-revert-checks.log`.

## Test scope revised to 16,000 units; retail-map benchmark support

The user explicitly reduced testing from 40,000 to 16,000 units. This supersedes
the earlier 40,000-unit objective in this report. `simperf` now defaults to
16,000 requested units and rejects larger requests. Mixed-army match/patrol
fixtures retain their existing convention of eight additional monarchs (16,008
initial units). Local/remote multiplayer cap-test cases now use 2,000 per player
instead of 5,000. Gameplay limits are unchanged; historical measurements above
are retained.

`simperf --map NAME` now loads a retail map (or generated recipe ID) through the
same `setupMatch` path as the client/referee. Patrol destinations use the loaded
map's actual width. Movement mode rejects `--map`, missing maps fail explicitly,
and mixed workloads fail setup if they cannot place the requested army plus
monarchs, avoiding misleading underfilled performance claims. All three
simperf binaries rebuilt; both multiplayer harness scripts pass `bash -n`.

Prior to the scope reduction, Ulasem Arena/Crusades placed only 36,475 units for
a 40,000-unit request, so that exploratory timing is NOT a valid 40,000-unit
result. A 16,000 request placed all 16,008 units: 5.789 wall seconds for 10
simulation seconds (1.727×), median 16.438 ms, p95 23.316 ms, max 74.753 ms,
15,890 moving/15,984 displaced. All survive. This is a short patrol sample, not
a sustained full-game result. Evidence:
`/tmp/tak-real-map-ulasem-crusades-16000.log`.

A 408-unit Ulasem Arena/Crusades check agrees between Release and Debug with
full body/piece verification enabled. Invalid map/mode combinations and missing
maps reject correctly (`/tmp/tak-real-map-{small,small-debug,invalid-mode,missing-map}.log`).
Above-ceiling `--units 16001` rejects. The default-size rerun is recorded in
`/tmp/tak-16000-default-ulasem.log`.

## Sustained 16,000-unit Ulasem Arena patrols

Using the revised test scope, ran 3,600 ticks (120 simulated seconds) on Ulasem
Arena with 16,000 army units plus eight monarchs. Same Release binary and cores
0–3; balances run sequentially, no builds or other benchmarks during timing.

| Balance | Wall seconds | Simulation speed | Median / p95 / max tick ms | Final moving |
|---|---:|---:|---|---:|
| Standard | 61.025 | 1.966× | 14.649 / 18.619 / 67.267 | 15,676 |
| Crusades | 60.987 | 1.968× | 14.716 / 18.622 / 71.446 | 15,617 |

All 16,008 units survive throughout both runs. Displaced counts: 15,985 standard,
15,984 Crusades. Each log contains all 120 checkpoint hashes. Final hashes:
standard `f0d71e562543d7bd`, Crusades `42055412dc32d375`. Crusades' first ten
checkpoints also match the preceding short default-size run. Logs:
`/tmp/tak-16000-ulasem-sustained-{standard,crusades}.log`.

Both runs clear average 1.0× for the sustained retail-terrain patrol workload,
including scripts, navigation exploration and periodic state hashing. Occasional
ticks exceed 33.3 ms. Combat, AI decisions, rendering and networking remain
outside this workload; this is not yet full-game performance proof. No
simulation/pathfinding code changed for these runs.

## Sustained 16,000-unit-start Ulasem Arena AI battles

Ran 3,600 ticks with eight Absurd AIs, full combat, Ulasem Arena, sequential
balances on cores 0–3. Standard: 120 simulated seconds in 58.064 wall seconds
(2.067×), median 13.489 ms, p95 23.184 ms, max 68.016 ms. Crusades: 61.096 wall
seconds (1.964×), median 14.172 ms, p95 23.659 ms, max 53.355 ms.
Both start at 16,008, but finish at only 7,112/7,794 living units respectively;
these averages therefore do NOT prove sustained combat at 16,000 live units.
They validate complete two-minute battle execution and give workload timings,
excluding rendering/networking. Logs:
`/tmp/tak-16000-ulasem-battle-{standard,crusades}.log`.

The user additionally requested removing the 5,000-per-player lobby option.
Lobby choices are now 250/500/1,000/2,000; the server accepts the same set and
falls back to 2,000 for other values. Wire layout and protocol version remain
unchanged. Shared single-player/multiplayer room UI uses the same selector.

## Rendered client/server validation and snapshot lookup fix

Added completed snapshot tick, living-unit count and SDL wall timestamp to the
existing `TAK_PROF` output. Read both snapshot values while the frame is pinned,
before `endFrame`; living count sums the captured player counts (the snapshot's
`live` vector actually includes retained dead records, so its size is not a
living-unit count). All three client configurations rebuilt.

The first screenshot-driven measurement was discarded: `--shot` deliberately
forces SDL SOFTWARE rendering. Its low FPS is not GPU performance evidence.
Subsequent tests use normal OpenGL accelerated rendering, eight Absurd AIs,
Ulasem Arena, per-player cap 2,000 and stress setup (~15,208 initial units).
Server is isolated on loopback port 19650 with seed 2002, cores 0–3; client uses
cores 4–7, requested window 1280×960 and existing graphics settings. Processes
are owned by `/tmp/tak_render_perf.py` and terminated in its finally block.
Spectator tests validate flow/rendering, not seated-client hash consensus.

The timestamped pre-fix run advanced from tick 7 at 3,569 ms to tick 883 at
119,127 ms: **0.253×**. Found an O(N²) client snapshot operation: every unit
called `World::unit(0)` when both conjure target IDs were zero, which misses the
fast ID path and scans the entire army. Snapshot capture now tests the chosen
site ID before looking it up. Active construction uses exactly the same lookup;
no simulation or pathfinding code changes. Thirsha and the Hunter site's purple
conjuring effects remain visible in `/tmp/tak-conjure-lookup-check.png`.

The otherwise-identical fixed run reached tick 3384: from tick 40 at 3,571 ms
to tick 3384 at 114,012 ms is **1.009×** while advancing. It then stopped at that
tick despite thousands of units remaining: the client victory check treated a
spectator's fallback player 0 as their own team, ending observation when that
team was eliminated. Over the full observation (including the stopped tail)
speed is 0.965×, so do not report this run as sustained 1.0× completion.
Added `!spectating_` to the own-team-defeat branch; overall-winner detection is
unchanged. A repeat of the same seed is required to verify passage beyond 3384.

Evidence: `/tmp/tak-16000-render-metrics-standard-client.log` (pre-fix; its
original `live` field was allocated snapshot records),
`/tmp/tak-16000-render-metrics-standard-fixed-client.log` (lookup fix), and
matching `-server.log` files. Battles lose units, unlike the constant-population
patrol tests, so neither rendered average proves combat at a constant 16,000.

The spectator fix passes the seeded regression: standard progresses beyond the
old stop (tick 3412 at 114,177 ms), then reaches tick 3565 at 119,224 ms. Its
first positive sample is tick 41 at 3,661 ms, giving **1.016×** across the active
observation. Crusades reaches tick 3564 at 119,237 ms, from tick 40 at 3,692 ms:
**1.017×**. Both use normal accelerated OpenGL rendering, server AI/combat,
network delivery and the client's simulation/snapshot pipeline. The slight
amount over 1.0× reflects buffering/pacing and the server's integer clock.

First sampled living counts are 15,171 standard and 15,154 Crusades; stress
setup starts around 15,208 (~95% of the 16,000 total lobby cap). Final counts
are 6,739 and 7,720. These are near-cap battle-start tests with casualties, not
constant-16,000-population rendering tests. Frame rate varies with the visible
army; the standard log spans 15–120 FPS including loading/low-density views.
Do not conflate real-time simulation with a stable graphics frame rate.

Both clients exit cleanly after the 120-wall-second observation, both owned
servers stop, and port 19650 is confirmed closed. No logged desync/suspect/error,
but spectator hashes are only flow acknowledgements, so this is not seated
consensus validation. Evidence:
`/tmp/tak-16000-render-metrics-standard-spectator-fixed-{client,server}.log`,
`/tmp/tak-16000-render-metrics-crusades-fixed-{client,server}.log`.
Release, Debug and optimized Debug clients rebuilt with the profiler, null-site
lookup guard and spectator outcome guard. Simulation/pathfinding code unchanged.


## Arrow rendering follow-up

Aramon Archer and Taros Skeleton Archer both declare `model=araarrow`.
The mesh has 15 vertices and 7 primitives in its root, with no child pieces;
the Hunter spear likewise stores its geometry in the root. `drawShotModel`
called `collect` with its default `isRoot=true`, which suppresses unit ground
plates and therefore discarded these projectile meshes. Pass `isRoot=false`
for projectile collection. Unit collection is unchanged.

The arrow mesh's head is at +z and its fletching at -z. Its facing also needs
pi minus the trajectory heading, rather than the unit convention of minus
heading. Checked the projected +z direction against eight flight directions.
These changes affect display only, not simulation or pathfinding.

A live Debug `--firetest` under GDB confirmed the arrow emits 10 triangles;
`/tmp/tak-projectile-check.log` records the draw calls. The diagnostic capture
`/tmp/tak-arrow-magnified.png` shows the textured shaft, head and fletching:
GDB magnified and centered the actual projectile draw without altering assets.
That capture precedes the facing correction; a later timed capture missed the
short flight and is not evidence for orientation. No retail animation parity
claim follows from this geometry check alone.


## Refreshed remote synchronization sweep

Rebuilt the current server in the Ubuntu toolchain container, Debug with
`-O2 -g`, and uploaded the isolated `takserver.perf171-current` to both
`tak.pgnet.us` and `vpn3.pgnet.us`. Both remote SHA-256 checks match
`4c53d8ad084dab2fa25caaf89c011b1218d0b757ab543c61b0a8f46fc7ead379`.
The production server binary was not replaced.

`tools/desync-hunt-remote.sh --minutes 1 --validate` selected `h-stress`,
`h-lake`, `h-everything`, `2h-stress`, `2h-orders-stress`, and
`w-allai-stress`. All eight client runs completed through tick 1800.
The five seated cases passed consensus checks; the spectator case passed flow
only. Both clients in `2h-stress` ended with `dc8e8cf95b8ca25d`; both in the
live-order case ended with `12ce0139d1b405ca`. The latter is consensus evidence,
not a golden hash, because command arrival determines execution ticks.
The deliberately planted tick-900 desync and gameplay-data mismatch were both
caught. These are synchronization checks, not sustained-speed benchmarks.

The harness exited 0; no isolated test server processes remain on either host,
and both override-restoration and traffic-shaping records are empty.
Logs: `/tmp/tak-perf171-current-remote.log` and
`/tmp/desync-remote-1530341/`. The clients used the root-projectile fix; the
facing-only correction was rebuilt afterward to avoid relinking a running
client binary. Both changes are display-only.


## Constant-population rendered patrol fixture

`TAK_PATROL_PERF=1 TAK_PROF=1 build-o2/takclient game 'ulasem arena'
--data assets/game --winsize 1280 960` creates exactly 16,000 allied units
(1,999 troops plus one monarch per faction). Add `--crusades` for that balance.
It uses normal match terrain/setup, retail scripts, reflected patrol goals,
client snapshots, and rendering, with no AI, combat, networking or fog.
This standalone fixture runs simulation and rendering on the main thread;
it does not measure the normal multiplayer simulation worker. The fixture
rejects an underfilled map instead of reporting a smaller population as 16k.
It is developer-only and adds no lobby option or selectable unit cap.

Use the same four P cores (0–3), accelerated rendering, no `--shot`, and record
at least 120 seconds after setup. `PROF` supplies tick/wall time and living
population; `PATROL_PERF` supplies living/moving/displaced counts every 300 ticks.
The fixture accumulates real time and advances only complete 1/30-second steps.
An initial run using the standalone viewer's ordinary partial-step loop was
discarded because that loop rounds fractional ticks upward. Its log is
`/tmp/tak-16000-patrol-render-standard-discarded-clock.log`; do not use it to
claim a simulation speed result.

The benchmark results panel now reads the snapshot's living player counts,
rather than `front().live.size()`, which also contains retained dead units.


Completed both 135-wall-second observations with OpenGL accelerated rendering,
1280×960, existing graphics settings, and CPU affinity 0–3. Excluding the first
10 simulation seconds for setup/warm-up:

| Balance | Tick / wall-ms endpoints | Measured wall seconds | Simulation rate | Final movement checkpoint |
| --- | --- | --- | --- | --- |
| Standard | 311 / 11585 → 3993 / 134360 | 122.775 | 0.999661× | tick 3900: 15,674 moving, 15,977 displaced |
| Crusades | 305 / 11350 → 3995 / 134412 | 123.062 | 0.999496× | tick 3900: 15,631 moving, 15,975 displaced |

Every populated profiler sample and every 300-tick movement checkpoint retained
exactly 16,000 living units. Both clients exited 0 after their observation.
These results support approximately real-time simulation for this constant
population patrol/rendering workload; they do not establish constant-16k combat
or multiplayer performance. The crowded camera draws about 5,000 units and
settles around 10–11 FPS, so rendering remains a visible bottleneck even when
simulation keeps pace. No pathfinding or simulation implementation was changed.

Logs: `/tmp/tak-16000-patrol-render-{standard,crusades}.log`;
runner `/tmp/tak_patrol_render_perf.py`, parser `/tmp/tak_patrol_metrics.py`.
Release, Debug and optimized Debug clients all rebuilt successfully with the
fixed-clock fixture and corrected benchmark living count.


## Cache model primitive atlas lookups

The 60-second rendered patrol CPU profile has 9,282 samples. `collect` accounts
for 9.4% self / 28.2% inclusive; `buildUnitGeom` is 39.6% inclusive, including
collection, sorting and vertex emission. These are CPU samples across threads,
not fractions of wall time. `glxinfo -B` confirms NVIDIA RTX 5070 Laptop GPU,
not a software OpenGL fallback. The actual completed profile is
`/tmp/tak-patrol-render.prof_1618335` (the unsuffixed file is empty).

`PieceMeta` now caches each primitive's immutable atlas-rectangle pointer.
The layout is established once before model metadata is constructed; target
recreation retains the layout, and unordered-map rehashes preserve references.
The main model pass therefore avoids repeated texture-name hash lookups.
Transient metadata still uses live lookup, and the no-atlas/per-texture fallback
is unchanged. No triangle, UV arithmetic, draw order, or simulation code changes.

Sequential 60-wall-second standard-balance patrol runs, four P cores (0–3),
16,000 living units, same settings and camera; compare samples at ticks 900–1700:

| Metric | Before | Cached |
| --- | --- | --- |
| Samples | 25 | 26 |
| Mean projection phase | 16.824 ms | 14.392 ms |
| Mean total draw | 32.708 ms | 30.269 ms |
| Mean reported FPS | 10.680 | 11.077 |

This is ~14.5% less projection time, ~7.5% less draw time, and a modest ~3.7%
FPS gain, not a large overall simulation-speed gain. Both runs kept 16,000
living units and exited 0. Logs: `/tmp/tak-atlas-perf-{before,after}.log`.
Baseline executable: `/tmp/takclient-before-atlas-cache`.

Fixed-camera, frozen-update `--facetest` renders show the four Archer models
and are byte-for-byte identical before/after:
`/tmp/tak-atlas-{before,after}.png`, SHA-256
`58b7db0a834728c5e014c6d6d6b27f1a36822eb5bc5a6f178dbc7a6ed5fb5368`.
The first screenshot attempt did not fix camera motion and was not used as
fidelity evidence; the final GDB scripts fix dt, camera, zoom and edge scrolling.
