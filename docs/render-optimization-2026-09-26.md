# Crowded-scene rendering optimization — 2026-09-26

Follow-up to [the CPU/GPU profile](render-bottleneck-2026-09-26.md).
The retained changes reduce CPU rendering work without changing simulation,
pathfinding, animation timing, shadow coverage or depth ordering.

## Changes

- Model bodies sort stable triangle indices instead of moving full triangle
  records. Equal-depth ordering and texture runs are preserved.
- Large body batches can submit their existing packed vertices directly through
  SDL's compatibility OpenGL context, avoiding SDL's per-vertex conversion and
  copying. A degenerate SDL draw establishes its shader, blend, clipping and
  target state; client arrays, matrix and current attributes are restored after
  the direct draw. Small batches, other renderers, unsupported contexts,
  non-normalized textures and fractional output scales keep SDL submission.
  Direct submission supports 1× and 2× output scales, including 4× supersampling.
- Shadow atlas packing sizes the opaque page buffer once, then fills disjoint
  ranges with a vectorizable loop. Float operation order and masked geometry
  remain unchanged.
- Development-only `TAK_PROFILE_ZOOM` allows a repeatable initial camera in the
  normal network client. Release behavior is unchanged.

A parallel shadow-packing experiment was rejected: dispatch/synchronization
overhead raised shadow time from roughly 2.8 to 3.7 ms and reduced frame rate.
The final packing loop remains serial.

## Repeatable rendering comparison

Same hardware/settings as the earlier report: Core Ultra 9 275HX, RTX 5070
Laptop, accelerated offscreen OpenGL, optimized debug `-O2`, 1280×960, 4× AA,
shadows enabled, no VSync, 480 FPS cap. The corrected stationary fixture requests
1,998 units, giving **2,000 total and 1,788 visible**. Each run lasts 30 seconds;
the first 15 seconds are excluded. Timing runs had no profiler or concurrent
build. Before/after runs used an ABBA order.

| Run | Mean frame work | Approximate FPS | 95th-percentile frame work |
|---|---:|---:|---:|
| Before C | 10.92 ms | 91.6 | 12.41 ms |
| After C | 9.20 ms | 108.7 | 10.33 ms |
| After D | 9.14 ms | 109.4 | 10.44 ms |
| Before D | 11.06 ms | 90.4 | 12.45 ms |

These runs isolate compact sorting plus direct submission: approximately 17%
less frame work, or 20% more FPS. FPS is reciprocal mean measured frame work,
not monitor cadence. Direct submission moves work out of `present` into draw;
compare total frame work rather than drawing time alone.

A subsequent pair adding page-sized shadow allocation measured 9.53 → 9.18 ms
frame work and 2.86 → 2.68 ms shadow time. This smaller, single-pair result is
less robust than the ABBA comparison; do not add its percentage to the above
as a guaranteed combined gain. GPU use remained around 20% in these scenes.

## Normal network-client profile

A separate Release server and the optimized debug interactive client ran a
45-second local match with seven AI players, stress spawning and a 2,000-unit
per-player cap. No inline simulation benchmark was enabled. Maximum observed
living count was **15,209**; combat reduced it during the run. Camera zoom was
0.25, with roughly 3,045 visible units in the measured interval. No desync or
error appeared in the client log. This is a synthetic network-backed crowded
match, not a reproduction of the user's particular match.

After the first 15 seconds:

| Measurement | Mean |
|---|---:|
| Frame work | 35.88 ms (~27.9 FPS) |
| Main-thread CPU, 100% = one core | 96.7% |
| Simulation-worker CPU, 100% = one core | 97.5% |
| Whole-device GPU | 13.8% |
| Main update | 6.15 ms |
| Animation, included in update | 3.98 ms |
| Draw | 28.92 ms |
| Geometry preparation, included in draw | 3.91 ms |
| Submission, included in draw | 17.66 ms |
| Shadows, included in submission | 10.23 ms |
| Presentation | 0.81 ms |

The simulation timing counter averaged 29.47 ms on its **separate worker**;
it is not a component of main update. Both threads are busy. Native sampling
was enabled during this run, so use the uninstrumented stationary comparison
for optimization gains, not this diagnostic profile.

All-thread native sampling collected 12,018 samples. Model geometry building
accounted for 45.6% cumulatively, including collection (21.7%) and shadow
building (13.8%); simulation tick accounted for 18.2%. `memcpy` was 6.0% flat;
SDL geometry submission was 2.0% cumulative. These are overlapping process CPU
sample percentages, not main-thread wall-time shares.

Remaining worthwhile targets are model/pose transformation and shadow geometry
generation, followed by the simulation worker's script/state work. Animation
is measurable but smaller than rendering in this scene. Moving more work to
threads is not automatically a gain, as the rejected packing experiment shows.
Further simulation changes need independent deterministic validation; this pass
does not alter simulation or pathfinding.

## Validation and reproduction

`geometrysubmit_test` compares exact pixels and subsequent SDL draw state for
192 combinations of texture/no texture, nearest/linear filtering, clipping,
viewport, window/texture targets, four blend modes, and 1×/2×/fractional scales.
It passes on SDL software, NVIDIA OpenGL and Mesa llvmpipe OpenGL; the OpenGL
runs require that direct submission actually executes. NVIDIA shadow tests
also pass coverage union, padded atlas sampling, ordered mask runs, cutouts and
following SDL draws. A 2,000-unit scene ran 15 seconds with
`TAK_SHADOW_VERIFY=1`, comparing generated shadow vertices with the reference
collector without mismatch.

The client and geometry test are rebuilt in Release, optimized debug and Debug.
All **216 CTest executions passed**: 70 Release, 73 optimized debug and 73
Debug. Shared simulation/server source and network protocol are unchanged.

To reproduce the stationary scene with a development build, set
`TAK_SHADOW_BENCH=1998 TAK_PROF=1 TAK_PROF_FRAMES=1` and run
`build-o2/takclient game 'ulasem arena' --testbuild --nofog --data assets/game --winsize 1280 960 --novsync --maxfps 480`.
Enable shadows and 4× AA in the isolated settings directory. For offscreen
NVIDIA rendering, set `SDL_VIDEODRIVER=offscreen`, `SDL_AUDIODRIVER=dummy` and
`__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json`.

For the normal network scene, start
`build/takserver --port 17692 --data assets/game --no-auth --local --seed 1`.
Use the same client command without `--testbuild --nofog`, add
`--server 127.0.0.1 --serverport 17692`, and replace `TAK_SHADOW_BENCH` with
`TAK_MPAUTO=1 TAK_MP_AIS=7 TAK_PROFILE_ZOOM=0.25 TAK_FOG=2 TAK_STRESS=1 TAK_UNITCAP=2000 TAK_MP_WAIT=1`.
Do not combine stress spawning with an additional benchmark spawn fixture.

Local artifacts are under `/tmp/tak-bottleneck/`: `compare-submit.py` and
`before-c/after-c/after-d/before-d` logs/counters, `compare-pack.py`,
`network-normal.py`, `network-normal.log`, `network-normal.json`,
`network-normal.prof.0`, `network-top.txt`, and `ctest-*.log`. The saved baseline
binary is `takclient-before`. These temporary artifacts are not required by
the game or checked into the repository. `TAK_BODY_SDL_SUBMIT=1` retains the
original submission path for development comparisons.
