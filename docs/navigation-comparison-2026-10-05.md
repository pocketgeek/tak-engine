# Navigation performance investigation — 2026-10-05

**Stopped at the user's request before the final integrated benchmark.** This
checkpoint preserves completed implementation, regression work, measurements,
and rejected experiments. It is not a claim that all original performance and
movement goals are complete. Retail remains the default and compatibility
baseline; no new navigation mode was added.

## Source and scope

Work began from current main `86673b413a4747e2b19a128777c2223656cd0681`,
following review of `3255400007b3bda040c9b8aebe23a841bf5a5c63`. The newer main
change concerned Windows geometry macro naming and SDL test cleanup, not
navigation behavior. The existing touched-cell scratch reset and shared body
snapshot optimizations were preserved, not implemented again.

A detached checkout at `86673b4` supplied the frozen baseline. Its original
Release crowdbench SHA-256 is
`8a6191a517a9c3dece018e4abf16e126afb4a7665d94b787779a1b48fce9ac94`.
Measurements below ran on Linux x86-64, Intel Core Ultra 9 275HX (24 logical
CPUs), GCC 16.2.1, Release `-O3 -DNDEBUG`, with project static dependencies.
Other release-validation clients were running on this host. Exploratory
candidate timings also overlapped development activity; they must not be
presented as a controlled final speedup measurement.

The pre-existing changes to `docs/cooperative-pathfinding.md`,
`docs/release-0.7.24-validation.md`, and `tools/desync-hunt-remote.sh` were
excluded from this work and its commit.

## Retained implementation

- Retail+ can complete an unblocked, far-from-goal traffic update without
  constructing terrain/body callbacks. The guard requires compatible existing
  identity/state and declines around arrivals, congestion, reservations,
  retained routes and passage state. Successful updates still perform the
  same progress, group and admission bookkeeping. A declined fast path is
  side-effect free, including when later body-query work is deferred.
- Optional Retail+ profiling separates context, setup, policy and maintenance
  costs and counts fast/full updates. Wall-clock measurements do not affect
  simulation decisions; normal play does not enable those clocks.
- The common benchmark and runner now support Retail, Retail+, Flowfield and
  Cooperative with matching scenarios, seed, population, command and tick
  window. The existing 13 scenarios cover open ground, doors, bridges, mazes,
  opposing columns, shared destinations, mixed footprints, exploration,
  changing obstacles, rapid replacement, unreachable goals and both explicit
  and passive recovery.
- Optional lifecycle telemetry observes accepted queued requests and actual
  delivery callbacks, including admission wait. Duplicate requests preserve
  their original token. Cancellation, replacement, stale removal, clearing,
  failed/partial/empty deliveries and outstanding requests remain separate.
  Attaching or replacing an observer with outstanding requests or during
  dispatch is rejected, including callback reentrancy.
- The runner defaults to three repeats, reverses run order on alternate
  repeats, checks repeated deterministic outcomes, and records mean, sample
  standard deviation, median, range and censored observations. It records
  source revision, dirty status, source hashes/diff, build settings, compiler,
  hardware and binary SHA-256, and checks that binaries were not replaced
  during a matrix. These facilities require supplying the actual source and
  build directories; they do not establish that an arbitrary binary came
  from those sources.
- Linux, Windows and macOS navigation CI now includes Cooperative corridor,
  passage, freshness, clearance, grouping, memory and progress tests, plus
  the new telemetry/routing tests and four-mode serial/worker golden
  checkpoints. The determinism workflow also selects these tests.

The retained CPU change is confined to supported Retail+ ordinary ground Move
orders. Existing restrictions on combat, builders, transports, naval/flying
movement, special goals and unsupported footprints remain intact. Production
Flowfield/Cooperative routing and cache invalidation are unchanged. A possible
Cooperative use of the same fast path passed 26 trace-file comparisons but
was removed because its performance had not been measured at the stop point.
There is no protocol, replay or save version change: retained simulation
behavior is intended to be equivalent, and diagnostic tokens are not serialized
or included in simulation hashes.

## Measurements completed

The reviewed Retail+ overhead and doorway weakness were reproduced on current
main, not assumed to remain true. Three baseline runs, one player, all 2,000
units moving, seed 0, 6,000 ticks:

| Scenario | Mode | Median mean ms/tick | Run range | Sample SD | Midpoint crossings | Legal settled arrivals |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Open | Retail | 1.458343 | 1.457435–1.469535 | 0.006739 | 2,000 | 2,000 |
| Open | Retail+ | 1.950188 | 1.944237–1.956149 | 0.005956 | 2,000 | 2,000 |
| Doorway | Retail | 1.442612 | 1.436124–1.445923 | 0.004985 | 246 | 87 |
| Doorway | Retail+ | 2.495411 | 2.481867–2.539622 | 0.030204 | 85 | 34 |

Both open cases completed the whole group at tick 4,384. Retail+ performed zero
local searches, routes or waits there. The doorway performed 161,241 local
searches, produced 23,524 local routes and recorded 4,427,079 waits. Outcomes
and hashes matched across the baseline repeats.

Single exploratory fast-path runs recorded 1.759081 ms/tick on open ground and
2.386651 on the doorway. Their physical outcomes and hashes matched baseline
(`d2ffaad365e7d93d` and `2b2361edc93ce1a4`). **These are not repeated paired
measurements of the final integrated candidate; no percentage improvement is
claimed.** A separate diagnostic run counted 7,715,839 fast updates and 717,959
full updates on open ground. Earlier sampling identified always-on arrival,
group lookup, adapter and context costs; profiling itself changes overhead.

[Raw Retail measurements and baseline commands](navigation-evidence-2026-10-05/retail-measurements.json)
include tick percentiles, search work, physical stalls, path lengths and memory
fields available in those runs. The four-mode instrumentation smoke ran 32
cases (two seeds, two scenarios, two repeats, four modes), with no unmatched
lifecycle events and deterministic repeated outcomes. Its
[manifest](navigation-evidence-2026-10-05/instrumentation-smoke-manifest.json.gz),
[raw results](navigation-evidence-2026-10-05/instrumentation-smoke-results.jsonl.gz),
[summary](navigation-evidence-2026-10-05/instrumentation-smoke-summary.json.gz),
and [source/build provenance](navigation-evidence-2026-10-05/instrumentation-smoke-candidate-provenance.json.gz)
are compressed JSON, and describe an earlier development binary, not this
final checkpoint.

### Four-mode before/after status

| Mode | Retained production change | Completed evidence | Missing comparison |
| --- | --- | --- | --- |
| Retail | Optional diagnostics only | Baseline reproduction; per-tick compatibility traces | Final integrated repeated timing/allocation/latency matrix |
| Retail+ | Equivalent unblocked bookkeeping fast path | Reproduction, traces, focused tests, sanitizer, exploratory timing | Controlled final speedup and full matrix |
| Flowfield | Optional diagnostics only | Common benchmark smoke; rejected routing/cache experiments | Final full four-mode matrix; no production movement gain retained |
| Cooperative | Optional diagnostics only | Common benchmark smoke; expanded CI selection/golden tests | Final full matrix and platform CI results; no production movement gain retained |

## Rejected approaches

All six Retail+ congestion trials were removed. Their exact patches, controls,
results and reasons are in
[the rejected-experiment archive](retail-plus-rejected-experiments-2026-10-05.json)
and discussed in [Retail+ documentation](retail-plus-pathfinding.md).
Stable native-route grace, general failed-search backoff and admitting native
movement during local waits each traded a favorable case for regressions in
other populations, opposing traffic or order replacement. An idle-only retry
change had negligible effect.

The last trial retried fully failed searches less often only after they had
observed actual terrain refusal. Seed 0 at 6,000 ticks improved doorway
crossings/arrivals from 85/34 to 98/44. Seed 42 fell from 86/48 to 80/47;
seed 0 at 12,000 ticks fell from 222/142 to 215/134 and increased search work.
The longer and alternate-seed results invalidate the short-window improvement
as a sustained throughput fix. Focused wake-up/cancellation/fairness tests
passing did not justify retaining that regression. Existing Cooperative
passage admission requires fresh footprint-aware topology and bounded
clearance proof; it was not enabled in Retail+ without those prerequisites.

The bounded integer-weighted portal prototype remains **tools-only**, behind
its comparison executable. It has deterministic ties, explicit work/memory
caps and decreasing routing potential; tests cover weighted full-grid oracle
agreement, goal regions, footprints, corner legality, resource exhaustion and
serial/worker execution. Five synthetic repeats shortened the maze path from
756,600 to 741,000 cost units (678 to 652 steps), but increased work from
38,715 to 215,426 and cold median time from about 1.336 to 11.696 ms. Open
routes were unchanged while cold routing increased from about 0.731 to
12.473 ms. Production component-hop routing remains in place: this
per-destination prototype did not justify its cost. These are synthetic route
measurements, not World movement/completion gains. See
[raw routing CSV](navigation-evidence-2026-10-05/flow-routing-comparison.csv)
and [metadata](navigation-evidence-2026-10-05/flow-routing-metadata.json).
The CSV payload estimate excludes allocator/map-node overhead and is not a
peak-memory bound.

Dependency-scoped cache retention was implemented and tested experimentally,
then removed from production. It required unchanged source tiles and exact
exit-seed masks, checked under existing deterministic work quotas, with fresh
connectivity before reuse. Disconnect, cheaper-route opening, repeated
publication, goal variants, eviction, capacity, cancellation and serial/worker
checks passed. Synthetic rebuild savings did not translate consistently to
World movement. The final exploratory screen reduced dynamic-obstacle field
work (Flowfield 17.633M→16.887M; Cooperative 14.726M→13.904M), but Cooperative
exploration crossings fell 11→5 with slightly more field work. Passive recovery
reused no fields. The original invalidation is retained. Exact patches,
provenance, tests and raw screens are in
[the field-reuse archive](navigation-field-reuse-experiment-2026-10-05.json).
It distinguishes the measured frozen experimental binary from subsequent
minor cleanup; it does not claim the final checkpoint was benchmarked.

## Correctness, review and limitations

Completed checks:

- Retail/Retail+ baseline comparisons: 52 trace files across all 13 scenarios,
  16 units per player, two players, 75% moving, 600 ticks. Every-tick hashes,
  work counters, routes, controller/order changes and unit samples matched.
- Fast/full traffic equivalence across 12,000 updates, including pure declines;
  focused Retail+ World tests; standalone GCC ASan/UBSan/leak checks passed.
- Earlier focused Release navigation suite: 33 tests passed. Cross-build math
  checks matched `dcef618cd2e4d558` for GCC/Clang O0/O2/O3; local ARM emulation
  was unavailable because target headers were missing.
- Final pre-push rebuild and six selected tests passed: routing prototype,
  navigation telemetry (including callback rebinding and exception unwinding),
  Retail+ traffic, Retail+ World, Python matrix tests, and all four modes'
  serial/worker golden checkpoints. See the evidence directory's test logs.
- Independent review found and prompted a fix for state mutation on declined
  fast-path calls and observer rebinding during delivery. It found no remaining
  known blocker in retained changes, while explicitly rejecting favorable-seed
  selection and requiring the final benchmark before asserting a speedup.

Unfinished when stopped: a clean, exact integrated candidate versus frozen
baseline matrix across all scenarios/populations/seeds; repeated allocation,
latency and memory comparisons; full final CTest; Windows/macOS runtime results;
visual movement inspection; and a demonstrated saturated-bottleneck throughput
improvement. A trace plotting utility is present but its output has not been
visually reviewed. Do not treat its existence as completed visual validation.

The benchmark still requires a legal full footprint, empty orders, zero speed,
position within the authored goal area and 30 unchanged ticks for arrival.
Retired orders or blocked movement alone do not count. Missing completion and
latency population events remain censored, not zero-duration successes. Actual
route latency percentiles are received-only; cancellation and outstanding ages
must be read alongside them. Wall latency includes intervening harness events
and observation. Allocation counting covers tick-time C++ allocations, not all
`malloc` use; collect it separately from allocating latency/trace diagnostics.
Physical footprint legality is sampled periodically and at arrival/final state,
not a proof of every intermediate position. Path length measures actual unit
travel, including detours, and is not itself a global-route optimality measure.

Retail remains the recommended compatibility/default choice. Retail+ is an
experimental local-traffic alternative with reproduced dense-doorway weakness.
Flowfield and Cooperative should be chosen for their existing behavior and
validated scenarios, not for speedups or movement gains claimed by this
checkpoint. Maze/exploration completion, saturated doorways and passive recovery
remain important follow-up cases.
