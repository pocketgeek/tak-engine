# Engine performance audit — 2026-09-26

## Scope and acceptance

Profile geometry/sorting, animation VMs, combat/targeting, shadows/effects,
crowded movement, and snapshot/hash/synchronization work. Test up to 16,000
living units, with at most 2,000 per player and authored type limits. Preserve
retail pathfinding, script timing, random-draw order and lockstep behavior.
No retail GUI launch is needed.

Run measured workloads sequentially, without builds or other benchmarks
competing for CPU/GPU resources. Distinguish sampled CPU activity from elapsed
frame time; record renderer/device, settings, population, movement, combat
activity, timing percentiles and state hashes. Software rendering is not GPU
performance evidence. A shrinking army is not a constant-population result.

## Work log

- Goal started from `c9729e7` (0.7.1 plus eight commits).
- Existing `simperf --units` counted troops plus eight extra monarchs. Corrected
  it to include monarchs and restore the requested per-player cap after setup.
  The rendered patrol fixture now also restores the 2,000-per-player cap.
- Added a destination-congestion mode to the explicit performance harness.
  Added population/activity samples, p99 tick cost and separate hash timing.
- Collected separate headless and accelerated-render baselines and worker-inclusive profiles.

## Accepted changes and boundaries

- Use the existing unit-ID script index for per-unit snapshot yard reads.
- Apply the exact zero-word FNV multiplication shortcut to all zero fields.
- Correct benchmark population/caps and expose frame, hash, congestion and
  combat activity measurements.
- Reject a cross-face vertex cache after two workloads show no draw-time gain.

All six requested areas were profiled or measured. Geometry/sorting, cosmetic
and authoritative VMs, targeting/projectile collision, shadows, and movement
retain their current behavior. Snapshot construction remains outside its short
publication lock; no new authoritative parallelism or changed callback ordering
was introduced. CPU sampling does not measure time asleep on worker condition
variables. End-to-end frame timings include those waits, but this pass does not
claim a separate lock-wait or worker-count speedup. Longer gameplay, other maps,
water-heavy armies and other hardware can expose different bottlenecks.

## Measurement environment

Intel Core Ultra 9 275HX (24 logical CPUs), NVIDIA RTX 5070 Laptop GPU,
NVIDIA 610.57.04, SDL accelerated OpenGL. Timed runs use CPU affinity 0–3
for comparison with earlier audits; this is a constrained-CPU workload, not
an unrestricted-machine capacity claim. The existing client pool still creates
workers from hardware_concurrency, so it has more workers than available CPUs
in these runs. Changing affinity is not an engine optimization.

Headless timings use Release `simperf`; visual timings use the optimized Debug
client (`-O2 -g`, needed for developer fixtures). GPU runs use SDL's offscreen
video driver, NVIDIA EGL, 1280×960 output, 4× AA (2560×1920 internal), vsync off,
480 FPS cap, bilinear filtering, and an isolated settings directory. They do not
use screenshot mode or the SDL software rasterizer.

Each visual run lasts 80 wall seconds; frame statistics discard the first 20.
`TAK_PROF_FRAMES=1` adds per-frame update, animation, drawing and presentation
times to `TAK_PROF`. `TAK_PATROL_PERF_ZOOM=0.25` selects the wider view. The
standalone patrol fixture runs simulation and rendering on the main thread,
unlike the multiplayer worker arrangement. Do not present its combined FPS as
normal multiplayer rendering throughput. It contains 16,000 living units, but
only about 2,000–3,000 are visible in the wide view (about 417 at default zoom).

## Initial headless baselines

Generated flat map, mixed authored land/air rosters, eight players. Each run
starts with exactly 16,000 units including monarchs. Patrol has allied armies
and no AI/combat. Match has eight Absurd AIs and real combat/deaths. These first
runs cover 900 ticks (30 simulation seconds); timing excludes asset/setup work.
Tick percentiles exclude hashing; wall time/simulation speed include hashing.

| Balance/workload | End alive | Speed | Median tick | p95 tick | p99 tick | Hash time total | Hits |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Standard patrol | 16,000 | 1.241× | 24.63 ms | 30.32 ms | 35.78 ms | 1.695 s | 0 |
| Crusades patrol | 16,000 | 1.216× | 25.04 ms | 31.91 ms | 37.21 ms | 1.715 s | 0 |
| Standard match | 15,030 | 1.201× | 26.41 ms | 31.18 ms | 39.60 ms | 1.666 s | 8,007 |
| Crusades match | 14,799 | 1.231× | 25.34 ms | 31.07 ms | 36.98 ms | 1.675 s | 8,794 |

The first crowd experiment sent units too far away and did not reach significant
destination congestion within 30 seconds. It is not congestion evidence. The
fixture now uses a nearby shared destination per player; longer results appear below.
Combat population declines, so the match results do not establish constant
16,000-unit combat capacity. Hashing every 30 ticks costs roughly 55–57 ms per
hash, despite reasonable amortized cost: an important source of individual
long ticks/frames that average simulation speed conceals.

## Worker-inclusive rendering profile

Separate sampling run, wide camera, all worker threads registered with
Google CPU Profiler via a temporary pthread_create wrapper calling
ProfilerRegisterThread. Per-thread timers are enabled; profiling starts after
20 seconds of warmup. The earlier unregistered per-thread profiles omitted
worker activity and must not be used to estimate geometry/animation shares.
Inclusive CPU samples overlap; they are not fractions of elapsed frame time.

| Operation | Inclusive sampled CPU |
| --- | ---: |
| World::tick | 42.3% |
| buildUnitGeom | 18.7% |
| tickNavigationMovement | 14.2% |
| captureFrame | 12.1% |
| Cosmetic Vm::tick | 10.7% |
| scriptYardOpen (inside captureFrame) | 8.1% |
| searchBodyRect | 6.3% |
| buildUnitShadow | 5.9% |
| Authoritative tickUnitScript | 4.9% |

Cosmetic native-piece export alone accounts for 5.2%. It exports movement,
rotation and control state, not just the visible pose. Callback/debris reads
can occur while scripts execute, so suppressing or lazily exporting this state
needs careful behavioral validation. Authoritative scripts and movement remain
ordered; this audit does not reduce their cadence or omit offscreen units.

## First optimization: snapshot yard lookup

Snapshots read each unit's script yard state. Reuse the existing node-stable
unit-ID index maintained by script creation/retirement instead of walking the
script map for every live unit. Keep the old lookup as a fallback for records
outside the index. No stored/hashed state, pathfinding, script scheduling or RNG
order changes. Regression coverage includes opening/closing, script retirement,
invalid IDs and replay reset with ID reuse.

| Wide view | FPS | p95 frame | p99 frame | Mean draw |
| --- | ---: | ---: | ---: | ---: |
| Before | 6.50 | 188.57 ms | 205.01 ms | 35.29 ms |
| Indexed yard read | 6.98 | 166.35 ms | 171.95 ms | 34.63 ms |

Both maintain approximately 1.0× simulation speed in this paced client fixture.
These are individual matched runs, not confidence intervals. The lookup change
mostly reduces update cost; the drawing bottleneck remains.

## Rejected geometry experiment and shadow comparison

Tried caching transformed model vertices across adjacent faces within each
piece. A developer differential check compared every triangle's order, depth,
position, color and UV with the uncached path; that check and the existing shadow
comparison passed. However, the moving-army draw average was 35.96 ms versus
34.63 ms with just the yard optimization. In a separate fixed 960-unit shadow
fixture (about 840 visible), mean drawing was 5.14 ms versus 5.09 ms before;
combined FPS was 156.1 versus 156.8. No demonstrated benefit, so the cache and
its temporary verification hook were removed. Triangle ordering, geometry
collection and animation export remain unchanged.

In the wide moving-army experiment, disabling shadows reduced drawing from
35.96 to 19.54 ms and combined frame p99 from 203.70 to 108.81 ms (FPS 6.88 to
10.36). Both sides used the experimental cache, so this comparison isolates
the shadow toggle, not a retained geometry speedup. Shadows are still expensive
at ~3,000 visible models. Their CPU profile share understates total cost:
submission, mask rasterization and GPU work also matter. The current silhouette
atlas preserves coverage and overlap; simply dropping triangles, freezing poses
or reducing update frequency would alter the look and was not accepted.

## Combat, movement and script findings

Worker-registered headless profiles cover Crusades crowd and combat through
1,800 ticks, starting after tick 30 to exclude setup. These use the optimized
Debug build; absolute timing is taken from separate uninstrumented Release
runs. Sampling overhead and build differences make their hash timings unsuitable
for a direct comparison with the Release numbers.

In combat, authoritative unit scripts account for about 9.8% of sampled CPU,
target acquisition about 4.0%, projectile air-grid work 2.0%, and projectile
collision 1.2%. AI Controller::tick is about 1.6%. These are inclusive symbols,
not additive independent phases. Navigation movement, path-service work and
occupancy searches are larger costs; searchBodyRect alone is 8.6% in combat
and 10.3% in the crowd profile. It already uses compact footprint bounds and
spatial buckets, with an optional full-scan differential check. Changing search
budgets, target cadence or body ordering would change retail behavior and is
not justified by these measurements. No such changes were made.

Hashing remains prominent (stateHash inclusive samples about 13–15%). The retained optimization extends the existing zero-word FNV shortcut beyond unused VM stack
words to all zero-valued hashed fields. Eight zero-byte steps are exactly
multiplication by the eighth power of the FNV prime modulo 2^64. The field list,
serialization width and ordering stay intact; checkpoint equality and explicit
hash-time measurements validate the change.

## Retained hash optimization results

Uninstrumented Release runs, same population, orders and seed as before.
All 300 checkpoints across the six workloads match exactly. The per-check
values below divide total hash time by the number of checkpoints. Overall
wall-time gains are smaller than the hash gains and subject to ordinary tick
timing variation; this change primarily shortens periodic checksum work.

| Balance/workload | Ticks | Hash before/check | Hash after/check | Reduction | Final speed | End alive | Final hash |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Standard patrol | 900 | 57.92 ms | 26.46 ms | 54.3% | 1.213× | 16,000 | `211f4c5ea4ff4cd9` |
| Standard crowd | 1800 | 58.08 ms | 26.01 ms | 55.2% | 1.343× | 16,000 | `cf46560430b35b3f` |
| Standard match | 1800 | 55.13 ms | 25.79 ms | 53.2% | 1.207× | 13,231 | `bd7f463f0e11ca60` |
| Crusades patrol | 900 | 57.24 ms | 26.34 ms | 54.0% | 1.242× | 16,000 | `af3cee7abc7bebbd` |
| Crusades crowd | 1800 | 57.83 ms | 26.25 ms | 54.6% | 1.335× | 16,000 | `e825c3c1c37f9031` |
| Crusades match | 1800 | 54.88 ms | 25.91 ms | 52.8% | 1.249× | 13,196 | `702b6617fd27a98c` |

Crowd keeps all 16,000 alive through 60 simulated seconds. At the last sample,
13,588 standard / 13,504 Crusades units did not change position over the preceding
second; 4,969 / 4,911 were within 512 world units of their shared per-player goal.
The `moving` counter means nonzero stored speed and does not by itself prove
physical progress; `stationary_1s` makes that distinction explicit.

The 60-second combat runs record 18,844 standard / 21,376 Crusades hit events,
ending with 13,231 / 13,196 living units. They exercise sustained combat and
attrition, not constant 16,000-unit combat. No benchmark army includes boats on
land. Stress setup retains authored type limits, and the requested total includes
monarchs; the player cap is restored to exactly 2,000 after setup.

## Reproduction

Use the dated tables as workload-specific evidence, not universal capacity
numbers. Keep timing runs separate from builds, tests and profiling.

```sh
cmake --build build
# Repeat with --mode patrol (900 ticks), crowd and match, in both balances.
taskset -c 0-3 build/simperf --mode crowd --units 16000 --ticks 1800 --crusades
```

The client fixture needs the optimized developer build; Release intentionally
ignores developer switches. With the display settings listed above:

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json \
TAK_PATROL_PERF=1 TAK_PATROL_PERF_ZOOM=0.25 TAK_PROF=1 TAK_PROF_FRAMES=1 \
taskset -c 0-3 build-o2/takclient game 'Ulasem Arena' --data assets/game \
  --winsize 1280 960 --novsync --maxfps 480
```

Select the appropriate EGL vendor on other hardware. Verify the renderer/device
rather than assuming an offscreen context is accelerated. Use an isolated
`XDG_DATA_HOME` when comparing settings; add `TAK_NOSHADOW=1` for the disabled
control. Stop after 80 seconds and discard samples before `wall_ms=20000`.
Raw run logs, saved comparison binaries and profiles for this audit are retained
locally under `/tmp/tak-perf26`; the numerical results and checkpoint hashes
above are the durable record.

## Remaining opportunities

- Visible model generation/sorting and shadow submission remain expensive.
  The rejected vertex cache demonstrates that fewer arithmetic operations do
  not automatically improve this workload. Further batching or persistent GPU
  geometry needs a distinct renderer change with silhouette/animation checks.
- Cosmetic native-piece export is a measurable animation cost. A dirty-state
  scheme must preserve query, callback and explosion-time pose reads; skipping
  whole animation updates would change behavior.
- Congestion/path-service work is substantial, but occupancy and search ordering
  are gameplay behavior. Preserve the existing spatial indexes and deterministic
  service budgets when investigating allocation or data-layout improvements.
- Geometry and ordinary cosmetic VMs already use workers. Authoritative scripts
  share ordered world/RNG effects; parallelizing them blindly would break
  lockstep. Thread-count/affinity and normal multiplayer render throughput need
  their own controlled scaling study before changing pool policy.

## Validation

- All-target builds completed for Release, optimized Debug and Debug.
- CTest: 56/56 Release, 59/59 optimized Debug, 59/59 Debug (174 total).
- All 300 recorded patrol/crowd/combat checkpoints in both balance modes match
  the pre-optimization states, including identical combat losses and hit counts.
- Deterministic-math guard and GCC/Clang -O0/-O2/-O3 checks pass with golden
  `dcef618cd2e4d558`. The aarch64 cross-build legs were skipped because the
  required target toolchain/headers are unavailable; this is not an ARM run.
- Two local multiplayer runs use the Release server and optimized Debug client,
  Varro Passage, seed 1, ten simulated seconds. Both finish at
  `3c4e5e85a939988c`, matching the earlier reference. This checks the rebuilt
  configurations together; remote servers were not redeployed for this audit.
- No retail GUI was launched. No assets, gameplay data, pathfinding rules,
  script cadence, targeting order, random draws or checksum field order changed.
