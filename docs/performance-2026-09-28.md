# Moving armies, scenery, snapshots and combat

This pass follows the [large-map terrain/fog work](distant-rendering-performance.md).
The baseline is `e7e83ca`. Measurements use a Core Ultra 9 275HX (24 logical
CPUs), NVIDIA RTX 5070 Laptop GPU, Linux, and the existing optimized builds.
Runs are sequential, with no concurrent builds or performance tests. CPU
affinity is unrestricted; older measurements pinned to four CPUs are not
directly comparable.

The follow-up retained active-thread indexing, parallel shadow-atlas preparation
and sparse occupancy-bucket clearing. It also resolved both legacy feature-effect
probes. Detailed paired measurements and rejected experiments appear below;
the original measurements in the first sections describe `41f9e72`.

## Changes

- Render snapshots carry 56-byte display orders instead of 320-byte simulation
  orders. Every waypoint, rally order and field used by the HUD, cursors and
  overlays is retained. A single order per unit at 16,000 units saves about
  12 MiB across three frame buffers; longer routes save more.
- Script yard flags have a dense derived cache, updated at spawn, successful
  yard transitions, script retirement and replay reset. Snapshot capture avoids
  scattered reads into large script objects. Canonical script state remains the
  source of the simulation checksum.
- Scenery uses a spatial index of terrain-projected anchors. Candidate indices
  retain the original painter order, and exact screen/fog tests still decide
  visibility. Additions and footprint changes invalidate the index. Views
  covering most scenery use a straight scan. Unchanged feature generations also
  skip the visual-state apply loop; smoke and animation keep their tick queues.
- Masked shadows use the existing direct OpenGL geometry submission path,
  including its SDL fallback on other backends. This preserves the coverage
  textures, atlas layout, filtering, blending and geometry.
- The path scheduler maintains per-player pending/priority counts when requests
  change. This replaces a full traversal of its request map each tick. The
  scheduler still receives the same counts and uses its existing budgets,
  ordering, search algorithm and callbacks.

These CPU-side changes apply on Linux, Windows and macOS. Direct shadow
submission is specific to SDL's OpenGL backend; D3D, Metal and software continue
through SDL. No dependencies were added.

## Measurements

The 16,000-unit moving-army fixture uses Ulasem Arena, Crusades balance, mixed
retail units, zoom 0.25, 1280 × 960, AA 4, shadows enabled and vsync disabled.
Each run lasts 80 seconds; frame means exclude the first 20 seconds.
`TAK_PROF_FINISH=1` waits for GPU completion each frame, so offscreen submission
throughput is not mistaken for completed frames. About 2,000–3,000 units are
visible. Unit/type caps are respected and all 16,000 remain alive.

| Version | Frame | Update | Draw |
| --- | ---: | ---: | ---: |
| Baseline | 92.53 ms | 63.95 ms | 25.79 ms |
| Compact snapshots and yard cache | 81.38 ms | 53.37 ms | 25.44 ms |
| Plus scenery and masked-shadow submission | 80.48 ms | 52.64 ms | 25.07 ms |
| Final, including scheduler counts | 86.47 ms | 57.97 ms | 25.61 ms |
| Baseline repeated after final build | 98.50 ms | 69.56 ms | 26.14 ms |

The final run improves frame time by 6.6–12.2% against the two baseline runs
(about 9.5% against their mean). The variation is included rather than reporting
only the fastest intermediate build. The largest gain is in update work. Shadow
submission averaged 10.69 ms after the snapshot change and 10.12 ms after the
masked-shadow change; this smaller single-pair result should not be treated as
a universal improvement. The patrol harness runs simulation on the main thread,
unlike the normal network client, so its frame times combine both workloads.

An offscreen-scenery check on Ultima Online B1, zoom 1, uses the same graphics
settings and 1,165 map features outside the viewport. After a 15-second warmup
in each 28-second run, mean completed frames fell from 0.3606 to 0.3402 ms.
Update fell from 0.0164 to 0.0108 ms and drawing from 0.2140 to 0.2005 ms.
This is a small absolute saving on this map. The world-view screenshots match
pixel-for-pixel; differences are confined to changing HUD counters.

A final fully zoomed-out Ultima Online B1 check (zoom 0.05, fog enabled, same
28-second run and 15-second warmup) was effectively unchanged: 1.0364 ms before
and 1.0414 ms after. The earlier terrain/fog improvement remains intact.

The 900-tick combat measurements show no clear overall speedup: standard balance
was 23.326 seconds before and 23.340 after; Crusades was 22.684 before and 22.781
after. The scheduler change removes repeated queue traversal, but that alone
does not establish a wall-clock improvement in the complete combat workload.
The final 3,600-tick Crusades run completed 120 game seconds in 83.688 seconds
(1.434×), ending with 11,236 living units. Its sampled baseline profile is not
used as a timing comparator because profiling adds overhead.

## Reproduce

Use optimized Debug for the development renderer switches; Release deliberately
ignores them. Use a temporary `XDG_DATA_HOME` with AA 4 and shadows enabled to
avoid modifying ordinary play settings.

```sh
SDL_VIDEODRIVER=offscreen SDL_RENDER_DRIVER=opengl SDL_AUDIODRIVER=dummy \
TAK_PATROL_PERF=1 TAK_PATROL_PERF_ZOOM=.25 \
TAK_PROF=1 TAK_PROF_FRAMES=1 TAK_PROF_FINISH=1 \
build-o2/takclient game 'Ulasem Arena' --data /path/to/tak_data \
  --crusades --winsize 1280 960 --novsync --maxfps 0

build/simperf --mode match --units 16000 --ticks 900 \
  --data /path/to/tak_data --crusades
```

Omit `--crusades` for standard balance. Match mode includes eight Absurd AIs,
combat and deaths, so its population falls below the initial 16,000. It excludes
rendering and includes periodic lockstep hashing.

`TAK_FEATURE_FULL_SCAN=1` restores full scenery candidate scanning;
`TAK_FEATURE_FULL_SYNC=1` restores repeated application of cached feature state.
`TAK_SHADOW_SDL_SUBMIT=1` selects SDL submission for all shadow batches, not just
the newly converted masked batches. Profiling samples are collected separately
from timing runs.

## Remaining costs

The baseline all-thread patrol profile attributed 7.3% of CPU samples to snapshot
capture. Within that function, yard-state lookup was 49% and full order copies
were 30%. In the final profile snapshot capture is 5.1%, and yard lookup has
disappeared as a measurable hotspot. Path service drops from 1.0% to 0.3% in
this patrol profile. The other large costs are actual simulation movement and retail script
execution, animated model projection/sorting, and shadow-atlas preparation.

Animation-state conversion already updates only changed pieces. This pass
retains those export boundaries; changing animation cadence would also change
callback timing and visible behavior. Piece conversion is 1.2% of final CPU
samples, versus 16.1% for cosmetic VM ticking and 19.4% for model geometry
preparation. Animation workers remain useful, but
their combined CPU samples do not represent main-thread elapsed time.

The separate 120-second-game-time combat profile found navigation movement at
20.9% of samples, combat at 14.1%, path service at 12.4%, and authoritative unit
scripts at 9.8% (inclusive categories overlap). Target acquisition, body overlap
queries and exact fixed-point direction calculations remain substantial costs.
Parallelizing those mutable simulation phases requires preserving their order
and shared random streams. This pass targets redundant queue bookkeeping first.

Linux/NVIDIA hardware measurements do not establish Windows D3D or macOS Metal
performance. The portable optimizations and SDL fallback need measurement on
those systems. No claim of 4× simulation at 16,000 units is implied.

## Validation

- All targets rebuilt in Release and optimized Debug; CTest passes 88/88 and
  91/91 respectively.
- OpenGL passes 1,152 geometry pixel/state comparisons and the shadow coverage,
  union, atlas, ordered-mask and compositing checks. Software fallback is covered
  by CTest. The tests include scale, render target, clipping, filtering, blend
  mode and following SDL draws.
- Scenery candidates match a full exact visibility scan in original order over
  960 views, six zooms, negative coordinates, bucket edges, terrain lifts,
  additions, changed anchors and reset. Full-map queries retain the scan path.
- Render-order tests preserve coordinates, identifiers, order flags, work status,
  rally selection and queue clearing. Existing script tests compare cached yard
  results with canonical state, including blocked transitions and replay reset.
- Scheduler tests cover priority promotion, replacement, changed owner,
  cancellation, reset and completion. Long combat checkpoint comparisons provide
  the additional gameplay regression check.
- All 30 checkpoints in each 900-tick standard/Crusades run match the baseline;
  the 3,600-tick Crusades run also matches all 120 checkpoints. Final hashes are
  `e81714c1d141460c`, `ff287b04444d045e`, and `07ba752e5ce5e06b` respectively.
- GCC and Clang at O0/O2/O3 retain math golden `dcef618cd2e4d558`. The ARM cross
  leg skips because the target headers are unavailable; no native Windows or Mac
  test was performed here.

The initial dragon/tree flame screenshot gate did not complete on Ulasem Arena
within 30 seconds in either the baseline or new build. It is inconclusive for
live ignition visuals on that map. The optional `TAK_FEATURE_SMOKE_QUEUE_TEST`
also fails its delayed tick/retirement motion assertion in both binaries; that
legacy probe remains unresolved. Feature generation/lifecycle and smoke checks
in CTest pass, as does the real-art `TAK_FEATURE_CACHE_TEST`. Neither the timed
capture nor the failing legacy probe is counted as successful validation of
the initial pass. The follow-up corrections below resolve both probes.

### Follow-up: smoke probe corrected

The smoke-queue probe omitted active-feature tick events from its two delayed
ticks. The renderer advances feature-owned smoke when those events arrive;
events with `emit=false` advance existing particles without creating new ones.
The probe now supplies those events for its surviving feature. It passes its
capacity, motion, single-consumption and retirement-isolation assertions with
native animation export verification enabled. This corrects the old probe's
input, not engine smoke behavior. The dragon/tree flame capture remains open.

The `41f9e72` Linux, Windows, macOS and determinism CI runs all completed
successfully. Native Windows/macOS performance measurements remain outstanding.

### Follow-up: live flame capture

The flame probe now chooses the nearest burnable feature with authored flame
art, rather than silently attacking bare ground when the map-center search
finds nothing. Its capture gate uses that feature's authored front/back layer
count. Black Forest's `tarplant03` defines one front layer and no back layer;
the old two-layer requirement could never complete for this target.

The corrected probe completes on Black Forest through ordinary dragon attacks.
The captured frame shows the dragon's fire, burning plant and smoke, with native
animation export verification enabled. No engine ignition or rendering change
was needed. Both legacy feature probes are now resolved. All targets were
rebuilt in Release and optimized Debug. The `24b08d6` Linux, Windows and macOS
CI builds also completed successfully.

### Follow-up: active script threads

Retail VM scheduling now maintains a derived 16-bit active-slot index. It visits
the same slots in ascending order and rechecks after each executed thread, so
new children in later slots still execute during the same tick. Reset and raw
state restoration rebuild the index; it is neither serialized nor hashed.
Animation safety queries also avoid reading inactive thread records.

The differential scheduling regression compares all thread words, statics,
host events and random state against the original full-slot scan through 300
steps of saturation, reuse, calls, sleeps, wakeups and resets. Both full suites
pass (88 Release / 91 optimized Debug), with index and native-export checks
enabled. Every checkpoint matches in 900-tick standard and Crusades combat.

An accompanying active-only simulation prefetch experiment was rejected:
combat wall time regressed about 2%. With the existing full-thread prefetch
restored, reversed-order paired runs measured 22.663 → 22.430 seconds for
standard combat and 22.106 → 21.847 for Crusades, approximately 1% less time.
These are individual paired observations, not confidence intervals.

A separate triangle-sort experiment replaced stable sorting with indexed
tie-breaking. Drawing remained 25.11 ms/frame in both 80-second patrol runs;
it was removed because it did not demonstrate a benefit.

### Follow-up: parallel shadow-atlas preparation

Large opaque shadow batches now transform their vertices into atlas coordinates
on the existing worker pool. The main thread assigns disjoint output slices in
the original order before dispatching work. Packing, arithmetic, masks, coverage
and compositing stay unchanged. Small batches stay serial; SDL and GPU calls
remain on the main thread. This optimization is independent of renderer backend.

Four sequential 80-second 16,000-unit patrol runs, excluding the first 20 seconds,
compare serial → workers → workers → serial with the same executable:

| Preparation | Frames sampled | Frame ms | Drawing ms |
|---|---:|---:|---:|
| Serial, first | 727 | 82.666 | 25.894 |
| Workers, first | 770 | 77.922 | 24.102 |
| Workers, second | 774 | 77.583 | 24.028 |
| Serial, second | 730 | 82.303 | 26.047 |

The paired means show approximately 7% less drawing time and 6% less frame time
in this fixture. Simulation work per rendered frame also varies with frame
cadence; these results do not establish a separate simulation speedup.
`TAK_SHADOW_SERIAL_PREP=1` restores serial preparation for comparisons.

`TAK_SHADOW_CACHE_VERIFY=1` now forces serial preparation for the reference bake.
A 25-second moving-army observation compared used atlas pixels, including tile
padding, against that bake without mismatches. Geometry and projected shadow
verifiers were also enabled. The observation ended at its time limit, not a
scripted completion gate. Both full suites pass (88 Release / 91 optimized
Debug), including software geometry and shadow checks. Native Windows/macOS
performance measurements remain unavailable; the script scheduling/restoration
regression passes on both Windows x64 and macOS ARM64 CI at `6870a64`.

The scatter-targeting experiment reused its maximum-damage calculation for the
eligibility gate and scorer. Every 900-tick checkpoint matched in both balances,
but wall times were 22.992 → 23.099 seconds (standard) and 22.506 → 22.528
(Crusades). It was removed: fewer category lookups did not improve these
workloads. Target selection and random-draw ordering remain unchanged.

### Follow-up: sparse body buckets

The occupancy broad phase records which spatial buckets contain entries and
clears only those buckets when rebuilding. Previously every bucket was visited,
including empty regions of a large map. Previous entries are cleared before a
terrain-size change; candidate insertion, current footprint checks and
last-unit-wins occupancy remain unchanged.

Ultima Online B1 (2,016 × 2,016 terrain cells), eight Absurd AIs, Crusades, 512
initial units and 900 ticks: baseline 5.529 / 5.556 seconds; sparse clearing
5.333 / 5.403 seconds, with the second pair run in reverse order. That is about
3% less wall time in this early-match fixture, not a guarantee of sustained 4×
in a developed match. All 30 checkpoints match across all four runs.

Dense 16,000-unit combat is essentially unchanged: standard 23.017 → 22.980
seconds; Crusades 22.439 → 22.498. Every checkpoint matches in both balances.
Both full suites pass with the body full-scan verifier enabled in optimized
Debug, as does a separate 400-unit live combat/movement check. Accelerated
OpenGL validation passes all 1,152 geometry comparisons plus shadow coverage,
atlas, ordered-mask and compositing checks on the NVIDIA GPU.

### Follow-up: exact axis directions

Axis-aligned vectors now return their exact BAM heading directly. Other vectors
still use the unchanged bounded integer CORDIC. The full reference comparison
covers 200,000 signed input pairs and boundary cases, including zero vectors and
positive/negative axes. No direction quantization or trigonometric approximation
was introduced.

Paired 900-tick results: standard 16,000-unit combat 23.059 → 22.898 seconds;
Crusades 22.409 → 22.280; the 512-unit huge-map fixture 5.364 → 5.386. The dense
combat gain is small (about 0.6%); the huge-map result is essentially unchanged.
Every checkpoint matches in all three comparisons. Both full suites pass and
GCC/Clang O0/O2/O3 retain math golden `dcef618cd2e4d558`. The local ARM cross
leg skips for unavailable target headers; native ARM64 checks run in macOS CI.

Reverse-order repeats measured 23.011 → 22.951 seconds for standard combat and
22.331 → 22.353 for Crusades. The direction shortcut is a small improvement,
close to run-to-run noise, rather than a substantial general speedup.

## Follow-up integration and remaining costs

The accepted changes retain all 120 baseline checkpoints in the final
3,600-tick Crusades run (`07ba752e5ce5e06b`). It completes 120 game seconds in
82.139 seconds, ending with 11,236 living units from an initial 16,000. This is
workload validation, not a sustained-16,000-unit speed guarantee. All affected
targets are rebuilt in Release and optimized Debug.

A fresh all-thread patrol profile, collected separately from timing runs,
attributes 20.9% of CPU samples to model geometry preparation, 11.6% to cosmetic
VM ticking, 10.5% to navigation movement, 5.0% to snapshot capture, 4.9% to body
rectangle queries and 1.0% to animation-state export. These inclusive categories
overlap; worker CPU percentages are not main-thread elapsed time. Model
preparation uses the existing worker pool; its sort experiment did not improve
drawing time. Actual interpreter instructions, movement/collision work, model
processing and shadow rendering remain substantial costs. This pass does
not change their cadence, search budgets, RNG order or visual fidelity.

The retained follow-up optimizations are portable CPU work; the existing SDL
fallback remains in use on D3D, Metal and software renderers. Runtime GPU
comparisons and performance numbers here come from Linux/NVIDIA. Windows x64
and macOS ARM64 CI provide compilation, script and math coverage, not native
GPU performance measurements. No new dependencies were added.

At code commit `2929f21`, Linux package CI, Windows Release/Debug builds,
macOS ARM64 and the determinism workflow all pass. Both native platforms pass
the retail script scheduling/restoration regression and their math/driver
equivalence gates. The final local suites pass 88/88 in Release and 91/91 in
optimized Debug. All retained changes are committed and pushed; unproven
triangle-sort and target-damage experiments are absent from the final engine.


## Six-area optimization pass

This pass starts from `a47a715`. It investigates all six remaining areas rather
than assuming that fewer instructions or allocations necessarily make the game
faster. Measurements use the same Linux/NVIDIA machine and sequential workloads
as above; builds, tests and other benchmarks do not overlap timing runs.

A fresh 3,600-tick, 16,000-unit Crusades combat profile attributes 22.0% of CPU
samples to navigation movement, 10.6% to unit script ticking, 8.7% to path service,
8.4% to body rectangle queries and 5.2% to target acquisition. These inclusive
categories overlap. The harness's periodic hashing also costs 5.7%; it is not
rendering work. The previous all-thread patrol profile supplies the rendering
and snapshot baseline.

### Retained changes

- **Shadow preparation:** resolve masked coverage textures and allocate output
  batches on the render thread, then transform opaque and masked vertices in the
  same worker jobs. Output ranges are disjoint and preserve original run order.
  SDL calls remain on the render thread. Small batches and forced reference
  bakes remain serial. Masks, geometry, blending and animation cadence are
  unchanged.
- **Movement and path requests:** use a hash table for ID lookup. Scheduling still
  follows the retail player's entity-slot traversal, never container iteration.
  Reacquire iterators after finish callbacks, which may insert other requests
  and rehash the table. Completion and cancellation regressions exercise this.
- **Heavy-combat pathfinding:** hand the tracer's existing cell plane directly to
  cost search. Previously cost search allocated and cleared a second map-sized
  plane, then immediately discarded it in favor of the tracer's plane. Preserve
  flags, directions, start-node state, heap order and all work charges. On
  Ultima Online B1, the discarded allocation was about 31 MiB per handoff.

The search handoff regression compares cell state and expansion against the
former reset-then-replace sequence, including cells outside the explored route.
No search budget, route heuristic, movement rule, target selection, script
schedule or unit cap changes in this pass.

### Measurements

The request lookup change alone took 22.445 / 22.366 seconds to 22.199 / 22.222
in paired 900-tick Crusades combat runs: approximately 0.9% less wall time.
Adding cell-plane handoff measured 21.514 / 21.680 seconds. Standard-balance
combat measured 22.942 to 22.137 seconds. All 30 checkpoints match in every run.
These fixtures start with 16,000 units and include combat deaths.

The actual Ultima Online B1 fixture (2,016 × 2,016 cells, eight Absurd AIs,
Crusades, 512 initial units, 900 ticks) measured 5.478 / 5.299 seconds before the
pass and 3.130 seconds with the retained changes. A candidate with additional,
subsequently rejected changes measured 3.199 seconds. Every checkpoint matches.
This approximately 40% reduction is an early-match result, not a promise of
sustaining 4× in a developed match.

For the 16,000-unit patrol rendering fixture, each run lasts 80 seconds and
excludes its first 20 seconds. It uses 1280×960, zoom 0.25, AA4, shadows enabled,
no vsync and completed-frame timing. Roughly 2,000–4,000 units are visible; all
16,000 remain alive. Full local simulation also runs on this fixture's main
thread, so frame-time improvements include simulation savings.

| Build | Frame ms | Drawing ms |
|---|---:|---:|
| Original, first | 76.883 | 24.056 |
| Original, repeat | 76.400 | 24.120 |
| Retained changes, first | 67.957 | 23.249 |
| Retained changes, repeat | 64.548 | 23.022 |
| Retained changes, later control 1 | 68.935 | 23.499 |
| Retained changes, later control 2 | 68.419 | 23.422 |

Across these runs the improvement is approximately 12% in frame time and 3% in
drawing time. Run-to-run variation remains visible; the figures are workload
measurements, not general FPS guarantees. The simulations within the rendering
fixture are not isolated simulation-speed benchmarks.

### Experiments removed

The other requested areas were profiled and tested; no benefit is assumed just
because an operation was removed:

- **Model geometry:** cached fan UVs, conditional trigonometry for already
  prepared pieces, and constructing temporary metadata only on fallback paths.
  No reliable overall benefit survived the paired rendering comparisons.
- **Animation execution:** a special case for division by the usual 30 Hz rate
  was slightly slower in an isolated piece-command workload. Compact opcode
  dispatch was approximately 9% faster in a small interpreter workload but
  essentially identical in the actual 16,000-unit patrol simulation (20.961
  versus 20.971 seconds). The interpreter changes were removed.
- **Snapshot conversion:** sharing the exact flight-vector magnitude between
  animation queries and avoiding unused ground-step arithmetic did not establish
  a repeatable frame-time benefit in the tested batch; removed.
- **Collision storage:** 16 inline occupant pointers avoided small allocations
  but added storage and access costs. Focused movement runs measured
  7.214 / 7.274 seconds before versus 7.212 / 7.221 after—within practical noise.
  The custom storage was removed.

The first combined rejected batch measured 65.029 ms/frame against a subsequent
64.548 ms control. A later geometry/dispatch batch also failed the paired game
comparison despite its favorable interpreter microbenchmark. All rejected
variants retained the recorded simulation hashes; correctness alone was not a
reason to keep them. Frame copying, collision scans, interpreter instructions,
model preparation and target acquisition therefore remain optimization targets.

### Verification and portability

Both build trees are rebuilt for all shared-code changes. The retained shadow
path passed a 30-second, 16,000-unit moving-army observation with geometry,
projected-shadow and atlas-pixel verifiers enabled. The forced reference atlas
bake uses serial preparation. This is a bounded observation, not a scripted
completion gate. Accelerated OpenGL tests pass 1,152 geometry comparisons and
shadow coverage, ordered masks and compositing checks.

Windows x64 and macOS ARM64 CI now additionally run the asset-independent path
scheduler and retail render-query regressions, alongside the existing script
regressions. Runtime GPU performance and pixel validation here are Linux/NVIDIA
measurements; no native Windows/macOS GPU speedup is claimed. No dependencies
were added.

The final retained source passes 88/88 Release and 91/91 optimized Debug tests,
including the added callback-growth and cell-plane handoff regressions. The
optimized Debug suite enables the body, script-thread and animated-piece index
verifiers. The deterministic-math guard passes; GCC/Clang O0/O2/O3 retain golden
`dcef618cd2e4d558`. The local ARM cross leg skips for missing target headers;
native ARM64 coverage comes from macOS CI.

The final retained executable completes the 3,600-tick Crusades combat run in
79.008 seconds (120 game seconds, 1.519×), retaining all 120 baseline checkpoints
and final hash `07ba752e5ce5e06b`. It ends with 11,236 living units from an initial
16,000; this is not a sustained-16,000-unit speed guarantee.
