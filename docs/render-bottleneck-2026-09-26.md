# Crowded-scene CPU/GPU profile — 2026-09-26

Measured development revision `19ee799`, optimized debug (`-O2`), on an Intel
Core Ultra 9 275HX (24 logical CPUs) and NVIDIA RTX 5070 Laptop GPU, driver
610.57.04. SDL used accelerated OpenGL through the offscreen EGL backend,
1280×960 output, 4× supersampling, VSync disabled, 480 FPS cap. No CPU affinity
restriction. This diagnoses a reproducible crowd, not a recording of the user's
particular slow match or desktop presentation path.

## Stationary rendering comparison

The mixed shadow fixture requested 2,000 units, plus its two initial units;
1,790 were visible. Shadows on/off used the same scene and settings. Each run
lasted about 35 seconds; the first 15 seconds were excluded. Timings are means
from existing FRAME/PROF instrumentation. FPS below is reciprocal mean measured
frame work, not a claim about monitor presentation cadence.

| Measurement | Shadows on | Shadows off |
|---|---:|---:|
| Frame work | 11.70 ms | 8.25 ms |
| Approximate FPS | 85.5 | 121.2 |
| 95th-percentile frame work | 13.39 ms | 9.71 ms |
| Main-thread CPU (100% = one core) | 88.2% | 80.7% |
| Whole-device GPU | 21.8% | 18.8% |
| Update, including animation | 2.28 ms | 2.20 ms |
| Animation (subset of update) | 0.92 ms | 0.90 ms |
| Draw | 7.97 ms | 4.58 ms |
| Model projection/preparation (subset of draw) | 1.43 ms | 1.10 ms |
| Geometry submission (subset of draw) | 5.83 ms | 2.88 ms |
| Shadows (subset of submission) | 2.96 ms | 0.00 ms |
| Presentation | 1.45 ms | 1.47 ms |

Do not add nested timing rows together. Simulation cost was about 1 ms/frame.
CPU counters came from per-thread `/proc/<pid>/task/<tid>/stat` samples; GPU
utilization came from `nvidia-smi`, once per second. System GPU activity can
include other applications.

A separate instrumented shadows-on run used gperftools native stack sampling.
The 4,945 samples showed these prominent costs (flat unless marked cumulative):

- `memcpy`: 9.7%.
- `SDL_RenderGeometry_REAL`: 4.8% flat, 10.4% cumulative.
- `GL_QueueGeometry`: 4.4% flat, 5.6% cumulative.
- Shadow atlas coordinate packing in `gameview_render.cpp:428`: 3.5%.
- `memcmp`: 2.4%.
- Model transforms and `GameView::collect` across multiple source locations.

These are process samples, not exclusively main-thread samples, and should not
be added to wall-clock timing percentages. They identify geometry preparation,
copying and submission as worthwhile optimization targets. They do not establish
that all copies can safely be removed or that every slow match has this cause.

## Moving 16,000-unit fixture

The existing patrol fixture kept exactly 16,000 living units, with roughly
2,800 visible. Main-thread CPU averaged 97–98%; GPU averaged 10–11%. With
shadows, measured frame work averaged 94.75 ms: update 68.33 ms (simulation
52.38 ms, animation 3.87 ms), draw 22.37 ms (shadows 8.03 ms), presentation
4.06 ms. Without shadows, frame work averaged 68.37 ms.

**This developer fixture runs simulation inline on the main thread.** Normal
network-backed play uses a separate simulation worker. Do not interpret its
simulation timing as evidence that normal rendering executes the simulation on
the main thread. The runs also reach different simulation ticks per wall-clock
second, so their moving-scene difference is not a controlled shadow-only cost.
The stationary comparison above isolates rendering more reliably.

## Next optimization priorities

1. Reduce repeated shadow atlas vertex packing/copying while preserving current
   silhouettes, cutout masks and draw order.
2. Reduce transient triangle/vertex copies between model collection and SDL
   submission; preserve depth order and texture grouping.
3. Profile a recorded slow multiplayer scene before assigning additional work
   to animation or simulation. Animation was under 1 ms in this stationary case.

No rendering algorithms or simulation behavior were changed in this profiling
pass. Logs, per-second thread/GPU counters, runner scripts, and the native CPU
profile are under `/tmp/tak-bottleneck/`. `run.py` measures the moving crowd;
`static.py` measures the stationary crowd and collects the profile;
`summarize.py` computes the tables. All profiling clients have exited.
