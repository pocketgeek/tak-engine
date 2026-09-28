# Moving armies, scenery, snapshots and combat

This pass follows the [large-map terrain/fog work](distant-rendering-performance.md).
The baseline is `e7e83ca`. Measurements use a Core Ultra 9 275HX (24 logical
CPUs), NVIDIA RTX 5070 Laptop GPU, Linux, and the existing optimized builds.
Runs are sequential, with no concurrent builds or performance tests. CPU
affinity is unrestricted; older measurements pinned to four CPUs are not
directly comparable.

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

The optional dragon/tree flame screenshot gate did not complete on Ulasem Arena
within 30 seconds in either the baseline or new build. It is inconclusive for
live ignition visuals on that map. The optional `TAK_FEATURE_SMOKE_QUEUE_TEST`
also fails its delayed tick/retirement motion assertion in both binaries; that
legacy probe remains unresolved. Feature generation/lifecycle and smoke checks
in CTest pass, as does the real-art `TAK_FEATURE_CACHE_TEST`. Neither the timed
capture nor the failing legacy probe is counted as successful validation.

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
