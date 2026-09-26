# Rendering submission and animation conversion — 2026-09-26

Follow-up to the [engine profiling audit](performance-2026-09-26.md), starting
from `a7d8af4`. Work proceeded in the requested order: shadow submission first,
then cosmetic animation-state conversion. No retail GUI launches.

## Changes

Opaque shadow atlas triangles use a constant black color. On SDL's compatible
OpenGL backend, submit their existing position array directly instead of
expanding/copying a color for every vertex into SDL's geometry queue. A degenerate
SDL triangle establishes draw state, and `SDL_RenderFlush` drains queued work
before native calls. Restore current color, client arrays and buffer binding
before SDL resumes. This follows SDL's requirement to
[flush before mixing native rendering](https://wiki.libsdl.org/SDL2/SDL_RenderFlush).

Other backends, unsupported contexts and non-unit SDL render scales retain the
existing SDL submission. Atlas antialiasing remains baked into the coordinates.
Triangle order, silhouettes, cutout coverage, filtering and opacity are unchanged.
`TAK_SHADOW_SDL_SUBMIT=1` forces the original submission in developer builds for
same-binary comparisons. No persistent GL objects or additional GL link dependency.

The display VM now records which pieces changed and converts only those pieces
from the retail integer representation to the renderer's floating-point state.
Commands, visibility changes and motion updates (including the final settling
step) mark pieces dirty. Initial export still converts all pieces. Exports happen
at exactly the previous boundaries; callbacks during script execution still see
the previous exported pose. There is no reduced script cadence, offscreen skipping,
changed RNG ordering or changed pathfinding. Gameplay hosts compile without the
optional display notification hook.

Debug `TAK_VERIFY_NATIVE_EXPORT=1` compares every exported field with a full
conversion after every export. CTest enables it for animation roster, COB animation
and explosion-callback tests. A high-index piece regression covers motion beyond
index 63, final settling state, immediate movement, visibility and callback/query
export boundaries.

## Measurements

Same hardware and settings as the earlier audit: Core Ultra 9 275HX,
RTX 5070 Laptop GPU, driver 610.57.04, accelerated SDL OpenGL through offscreen
NVIDIA EGL, optimized Debug `-O2 -g`. Installed SDL2 interface version 2.32.70.
1280×960 output, 4× AA, bilinear filtering, shadows on, vsync off, cap 480 FPS.
Timed processes pinned to CPUs 0–3, with the existing worker pool unchanged.
Runs are sequential and do not compete with builds, tests or another benchmark.
These are constrained-CPU comparisons, not unrestricted-machine capacity claims.

Static shadow fixture: 960 spawned units plus two monarchs, about 840 visible,
40 wall seconds, first 20 discarded. Same binary, original/native/original order:

| Opaque submission | Mean draw | FPS | p99 frame |
| --- | ---: | ---: | ---: |
| Original SDL | 5.273 ms | 153.00 | 12.188 ms |
| Native constant color | 4.857 ms | 162.83 | 11.979 ms |
| Original SDL repeat | 5.246 ms | 153.68 | 12.582 ms |

Drawing improves about 7–8%; combined FPS improves about 6%.

Moving wide-view fixture: 16,000 living units, eight players, at most 2,000 each,
authored type limits, about 2,000–3,000 visible. `TAK_PATROL_PERF=1`,
`TAK_PATROL_PERF_ZOOM=0.25`, 80 wall seconds, first 20 discarded.

| Version | Mean draw | Mean animation | FPS | p99 frame |
| --- | ---: | ---: | ---: | ---: |
| Before both changes | 35.469 ms | 8.888 ms | 6.58 | 221.466 ms |
| Shadow submission only | 31.283 ms | 8.633 ms | 8.32 | 137.702 ms |
| Both changes | 31.267 ms | 5.837 ms | 8.91 | 133.598 ms |
| Shadow submission only, repeat | 31.215 ms | 8.812 ms | 8.29 | 145.506 ms |
| Both changes, repeat | 31.278 ms | 5.728 ms | 8.89 | 133.308 ms |

Shadow submission reduces mean drawing by about 12%. Sparse conversion reduces
mean animation time by about 32–35% across two comparisons. Animation time is part of update time, not an
additional cost to add to it. The patrol fixture runs simulation and rendering
on the main thread, so combined FPS is sensitive to simulation pacing and is
not normal multiplayer throughput. Individual timings are not confidence intervals.

## Remaining limits

Geometry transformation, depth sorting, cutout shadows and atlas compositing still
cost time. Closed-solid face culling was considered but not implemented: only
about 8.3% of model triangles belonged to completely closed pieces in a topology
scan, limiting safe savings. The earlier vertex-cache experiment was already
rejected for lack of measured gain. Neither optimization claims to solve every
rendering bottleneck or guarantee 16,000-unit performance on other hardware.

## Validation

- Rebuilt all targets in Release, optimized Debug and Debug. All 174 CTests
  passed (56 + 59 + 59), including full-field sparse-export checks in Debug
  configurations and the 202-script animation roster.
- Accelerated OpenGL shadow tests passed in Release and optimized Debug, with
  exact pixel comparisons against SDL submission. Tests cover repeated and
  overlapping triangles, fractional positions, baked AA coordinates, render
  scale fallback, viewport/clipping, and subsequent colored/textured SDL draws.
  The software fallback and existing cutout/atlas tests also pass.
- A live 16,000-unit client run passed with `TAK_SHADOW_VERIFY=1` and
  `TAK_VERIFY_NATIVE_EXPORT=1` together. This was a correctness run, not timing
  evidence.
- All 60 checkpoint hashes from 900-tick patrols in standard and Crusades
  balance match the previous build. Final hashes are `211f4c5ea4ff4cd9` and
  `af3cee7abc7bebbd`, respectively.
- Deterministic-math guard and GCC/Clang optimization-level checks passed with
  golden hash `dcef618cd2e4d558`; ARM cross-build legs were skipped by the
  harness after their test builds failed.
- Two local multiplayer runs with the Release server and optimized Debug client
  produced the same pre-change hash, `3c4e5e85a939988c`. Public servers were not
  changed by this pass.
