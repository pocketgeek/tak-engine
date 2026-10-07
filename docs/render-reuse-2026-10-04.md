# AA-off model reuse investigation — 2026-10-04

The retained improvement uses the existing exact geometry cache more effectively:
animated model textures invalidate it when their **frame changes**, rather than
on every simulation tick. It does not slow animations or change their poses.
Unused distant-image pages now expire even after zooming in. These measurements
used the 0.7.23 AA-off baseline, with filtering, GUI smoothing and movie smoothing
also off. Subsequent development restores their settings controls, including
live GUI-art reloads; the measurements below still describe that Off baseline.

A larger native-resolution building-image prototype was faster in some scenes,
but failed a crowded full-scene image comparison. It was removed. A small
supersampling experiment using those images was also removed. Neither is an
enabled feature or a new rendering fallback.

## Sources inspected

TAK started at `d9f3b4392ca59a1e4031b4e14a89fdda8ca3d03f`
(v0.7.23), with the preceding local gameplay fixes still present. They are shared
by every measurement and were not modified by this investigation.
Open Annihilation was inspected at
[`773f62a07fc6d787b32350c8ed21b54fb0a42a2c`](https://github.com/open-annihilation/open-annihilation/tree/773f62a07fc6d787b32350c8ed21b54fb0a42a2c).
Both projects carry GPLv3 licenses; OA's inspected files specify GPL-3.0-only.
No OA code was copied, and no retail assets or binaries were added to Git.

OA's CPU/Basic implementation prepares model images and retains finer sampled
images while their generation and required scale remain valid. It distinguishes
moving pieces from cached pieces, and commits model images at painter boundaries
before intervening scenery. Its Full GPU implementation instead renders the
world into supersampled targets and reduces sampling as zoom changes. These are
different designs, not interchangeable AA implementations. Sources:
[model drawing](https://github.com/open-annihilation/open-annihilation/blob/773f62a07fc6d787b32350c8ed21b54fb0a42a2c/src/present/model/src/model_draw.cpp),
[unit supersampling](https://github.com/open-annihilation/open-annihilation/blob/773f62a07fc6d787b32350c8ed21b54fb0a42a2c/src/present/model/src/unit_supersampling.cpp),
[painter submission](https://github.com/open-annihilation/open-annihilation/blob/773f62a07fc6d787b32350c8ed21b54fb0a42a2c/src/app/runtime_match_render.cpp),
[world supersampling](https://github.com/open-annihilation/open-annihilation/blob/773f62a07fc6d787b32350c8ed21b54fb0a42a2c/src/app/full_supersampling.cpp).

The labels must be normalized before comparing quality or cost:

| Label | OA samples per output pixel | TAK samples per output pixel |
|---|---:|---:|
| Off | 1 | 1 |
| 2x | 4 | 2 |
| 4x | 16 | 4 |
| 8x | 64 | 8 |
| 16x | 256 | 16 |

TAK's retained selective model-AA implementation uses reusable **screen-aligned
tiles**, not a packed per-unit AA atlas. It bypasses the distant body-image cache
and resolves at painter boundaries. That support remains in the engine, but
settings loading and the settings UI keep it off. The AA design was in
`docs/antialiasing.md`, removed with the AA options after 0.7.24 (see git history).

## Existing reuse and the measured bottleneck

TAK already prepares a shared animated piece tree, transforms shared vertices,
sorts triangle indices, and retains complete screen geometry in visible-unit
slots. An exact key protects pose, unit/model identity, camera, terrain anchor,
zoom, team colors, orientation, body attitude, construction, translucency,
veterancy and shadow state. Body submission combines compatible runs while
preserving scenery and special-effect boundaries. Shadow silhouette tiles also
reuse unchanged geometry.

The existing distant body-image cache is separate: zoom at most 0.6, at least
96 source vertices, bounds at most 28×28 logical pixels, 64-pixel tiles in
2048×2048 pages, at most four pages (64 MiB), and 128 bakes per frame. Moving,
walking, selected, construction, clipped, corpse and special-effect models stay
on their original geometry path. Changed eligible poses have an existing
approximately 67 ms refresh interval. This investigation does not extend that
interval or apply it to more units. Premultiplied image composition and painter
placement remain unchanged. See [the original measurements](distant-rendering-performance.md).

A separate AA-off CPU sample profile of the crowded Keep fixture attributed
49.6% cumulative process samples to `buildUnitGeom`, 30.4% to `collect`, 5.5%
self samples to `memcmp` and 5.0% to `memcpy`. These overlapping cumulative
percentages cannot be added, and are not a main-thread breakdown. Frame-phase
measurements confirmed unnecessary geometry and shadow work in that scene.
Towers instead spent much of their frame in body submission; adding another
geometry cache would not address that bottleneck.

The missed reuse was `meta.animated -> gameTick` in the exact key. An animated
texture may hold a frame for several ticks, yet that key rebuilt body and
shadow geometry every tick and changed its revision. The new key records the
actual frame indices of the animated textures used by that model. Other models'
texture animations do not invalidate it. Texture playback continues at the
original rate; a frame change, including expiry of a non-looping sequence,
invalidates it immediately.
The distant image key still includes the simulation tick for animated textures,
independently of the geometry revision. This preserves its existing refresh
schedule instead of accidentally changing the visible animation cadence when
geometry reuse improves.

Animation-frame addresses belong to stable `std::map` records. Metadata is
created when models load; its unique pointer lists allocate there, not per
frame or per unit. Override remounting discards models before rebuilding texture
records. Renderer-target invalidation clears geometry keys and image caches.
This adds small per-model CPU metadata, with no new image targets or per-unit
image memory. Independently moving pieces still contribute their exact pose
values to the key.

## Appearance, ordering and lifetime

The retained change does not move any draw operation between passes. Model
geometry, bitmap scenery, ground and airborne shadows, water placement,
terrain clipping, model projectiles, corpses, construction shimmer, particles,
additive effects and native-resolution UI follow the existing painter paths.
Lighting-dependent texture frame selection remains an input; no lighting or
color quantization was introduced. The same key inputs still invalidate on
construction/damage-related pose or tint changes, transparency, camera movement,
zoom and terrain anchoring. Unchanged geometry is submitted normally, rather
than flattened into a new overlay.

Distant entries unseen for more than 120 prepare calls are swept every 60
calls, including frames with no eligible bodies. When all entries expire,
their pages are released. Previously the eligibility early return prevented
this cleanup when zoomed in. A page with live entries remains allocated; this
is not per-page compaction. The four-page cap, backend capability checks,
allocation backoff and direct-geometry fallback are unchanged. Target reset
still clears the cache before rebuilding it.

The debug geometry verifier compares retained body/shadow vertices and runs
against fresh collection. The new regression additionally requires reuse of
an animated-texture model **across different simulation ticks**, so ordinary
same-tick cache hits cannot satisfy it.

## Controlled measurements

Means below average the two runs of each mode. Frame times are milliseconds;
memory is MiB. The final matched executable SHA-256 is
`337e03007b61d5c3f787aeb938f1286d9095767bfcbe5da3987f98cbf3c33f89`.

| Scene | Reference mean | Retained mean | Reference p95 | Retained p95 | Reference p99 | Retained p99 | Tracked GPU memory, both |
|---|---:|---:|---:|---:|---:|---:|---:|
| 24 towers, zoom 1 | 0.357 | 0.352 | 0.463 | 0.431 | 0.928 | 0.835 | 160.74 |
| 500 towers, zoom 1 | 3.023 | 3.184 | 4.193 | 4.294 | 6.288 | 7.340 | 175.74 |
| 1500 Keeps, zoom 0.5 | 14.360 | 11.609 | 17.946 | 17.771 | 19.360 | 19.190 | 181.72 |
| 200 spaced Keeps, zoom 0.5 | 3.124 | 2.237 | 3.797 | 3.518 | 4.474 | 4.167 | 165.72 |
| 2000 mixed units, zoom 0.25 | 7.071 | 7.074 | 10.694 | 10.477 | 11.892 | 11.916 | 197.77 |
| 1200 continuously moving Archers, zoom 0.5 | 8.068 | 7.847 | 10.789 | 10.078 | 12.227 | 11.665 | 137.72 |
| 1200 opposing Archers, zoom 0.5 | 7.437 | 7.562 | 9.954 | 10.109 | 11.584 | 11.805 | 164.68 |
| 500 towers, moving camera, zoom 0.75 | 4.008 | 4.065 | 5.107 | 5.161 | 6.332 | 6.207 | 183.77 |
| 500 towers, moving camera and zoom 0.25–1 | 4.546 | 4.548 | 5.957 | 6.322 | 7.450 | 7.393 | 249.06 |

The crowded Keep mean improves **19.2%** and the spaced Keep mean **28.4%**.
Two earlier controlled matrices also found substantial Keep benefits, with
roughly 15–21% reductions in the crowded scene. Its p95/p99 improvement is much
smaller: texture-frame changes still cause rebuild bursts. This is not a general
stutter fix. Towers are **5.3% slower** in the final matrix; earlier matched
runs were near neutral. Moving units, battles and camera/zoom changes show
small, inconsistent differences across runs, so no improvement is claimed for
them. The retained change is justified by repeated Keep gains and its small
metadata cost, not a promise that image reuse helps every workload.

| CPU/GPU phase, milliseconds | Crowded Keeps reference → retained | Spaced Keeps reference → retained |
|---|---:|---:|
| Geometry preparation | 1.910 → 0.993 | 0.577 → 0.408 |
| Shadow submission/preparation | 4.287 → 2.307 | 0.815 → 0.238 |
| Body submission | 3.411 → 3.500 | 0.788 → 0.791 |
| GPU draw timeline | 5.791 → 4.107 | 0.279 → 0.210 |

The benefit comes from preparing fewer identical vertices and retaining shadow
tiles, not reducing ordinary body submissions. Crowded Keeps submit approximately
782k body vertices and 26 body API runs per frame in either mode. Spaced Keeps
submit approximately 88k vertices and 12 runs. The sampled once-per-second
geometry logs show 0% reference reuse versus about 86% retained reuse for
crowded Keeps. These samples include warmup and can alias texture animation;
they are a diagnostic hit-rate estimate, not an exact per-frame percentage.

The mixed distant view uses about 1255 images/frame, 31–33 bakes/frame,
15–16k bake vertices/frame and 36–39 bake API calls/frame, with 2.3–2.5 cache
target switches/frame. Approximately 97% of image uses reuse a prior bake.
It retains 32 MiB of images in either mode. During continuous zoom, the cache
uses and bakes about 30.5 images/frame: effectively no prior-image hits, because
the required resolution/zoom changes every frame. That illustrates why
zoom-aware reuse cannot simply retain an old-resolution sprite indefinitely.

Peak process RSS ranges from about 485 to 603 MiB in the final matrix. Crowded
Keeps measured 589.5 → 602.6 MiB; spaced Keeps measured 499.6 → 499.3 MiB.
This includes assets, simulation and allocator behavior, and is not the size of
the new metadata. Tracked GPU memory is unchanged in every matched scene.
There are no new model-image targets in the retained implementation.

Hardware: Fedora 44, Intel Core Ultra 9 275HX, 24 logical CPUs, RTX 5070 Laptop
(8151 MiB), NVIDIA 615.71.09. Optimized Debug `-O2 -g`, SDL accelerated offscreen
OpenGL, AA off, shadows/tree sway/smooth motion on, 1280×960 output, VSync and
frame cap off. The isolated profile explicitly sets fullscreen off; SDL's
offscreen fullscreen-desktop default would otherwise produce 1024×768.

Each run advances 900 frames with a fixed 1/60 client-update input. Frames
0–299 warm up; frames 300–899 are measured. The same binary executes reference,
retained, retained, reference (ABBA). The debug-only reference flag restores
tick invalidation; every other rendering path is identical. No build, capture,
geometry verifier or sampling profiler runs during these timings. `glFinish`
includes completed rendering in frame work. Captures and verification use
separate runs. Fixed camera movement depends on frame index, not wall time.
The study also uses the fixed animation clock for distant-image refreshes;
normal games retain the wall clock. SDL's queued world draws are flushed before
ending the GPU query, so deferred world submissions are included.
Motion interpolation is fixed at the middle of the tick for study runs;
ordinary games still interpolate from wall time. The standalone harness advances
one 30 Hz world tick per rendered frame, so 900 frames represent 30 seconds of
world ticks and 15 seconds of client animation time. This is a controlled
renderer workload, not a simulation-speed benchmark.

The renderer fixture starts with two additional units. Crowded 40-pixel grids
can overlap buildings and are submission stress scenes, not claims about legal
base placement. A separate 160-pixel-spaced, 200-Keep fixture avoids that extreme.
The battle fixture uses two opposing Archer groups. Moving-fixture orders are
reissued every 240 frames to avoid measuring a mostly settled army.

`GL_TIME_ELAPSED` measures the draw timeline, including possible GPU idle gaps
waiting for CPU submission. It is **not** isolated GPU busy time or device
utilization. Phase times are CPU elapsed time, including worker waits where
applicable. Tracked texture memory excludes driver/internal allocations. RSS
is whole-process peak memory including assets and simulation. SDL body calls
and cache target switches are logical API work, not whole-frame hardware draw
calls. Percentiles are the average of each run's percentile, not a pooled
population percentile.

Reproduce with an optimized Debug client and legally owned data:

```sh
python3 tools/render_reuse_benchmark.py \
  --client build-o2/takclient --data /path/to/retail-install \
  --output /tmp/render-reuse-results

# Separate captures/vertex checks; their timings are not benchmark results.
python3 tools/render_reuse_benchmark.py \
  --client build-o2/takclient --data /path/to/retail-install \
  --output /tmp/render-reuse-visuals --pair --capture --verify \
  --scenes towers spaced-keeps animated battle zoom
```

The script records executable SHA-256, settings, world hashes, individual logs,
frame distributions, work counters, tracked GPU memory, and Linux CPU/RSS.
It uses only Python's standard library. GPU timing is unavailable on non-GL
backends and is reported as -1. The offscreen default is for this Linux study;
the isolated XDG preferences and process metrics are Linux-specific.

Final timing logs and JSON are in `/tmp/tak-model-reuse-study/fixed-measured/`;
every ABBA group has matching final world hashes. Other experiment directories
are separate runs and must not be pooled into these results.

## Verification and visual limits

- Release: 13 focused settings, geometry, shadows, terrain, AA fallback,
  model-transform, animation, construction/death and screen-lifetime tests pass.
- Debug: the same coverage plus `render_texture_reuse`, 14 tests, passes.
  The final interpolation change was followed by another passing reuse test.
- Optimized Debug: 15 focused tests pass, including naval combat; reuse and
  distant-cache regressions were rerun after the refresh-key change.
- Accelerated OpenGL: distant sprite transparency/camera/zoom/refresh and
  shadow composition comparisons pass with **zero maximum channel error**.
  The new expiration check releases all distant-page bytes while no models are
  eligible. Texture-tick refresh tests cover a held geometry revision and the
  original refresh interval.
- The unmodified GPU selective-AA suite passes on X11/OpenGL, including all
  15 combinations, resource limits, tile boundaries and target reset. Its
  offscreen run fails the window-resize tile comparison; this backend-specific
  test limitation was not repaired in this AA-off investigation. The regular
  software/fallback suite passes.
- Seven separate GPU capture pairs cover towers, crowded/spaced Keeps, mixed
  distant units, continuous movement, battle and camera/zoom changes. Each pair
  checks 12 frame captures, including ten consecutive motion frames. Geometry
  verification runs through every retained-model hit and compares fresh body,
  masked/unmasked shadow vertices, run ordering and bounds byte for byte.

Matched world-view captures were compared over the 1075×888 world rectangle,
excluding dynamic performance text and the bottom/right HUD. Many captures are
identical (**60 of 84**), but not every complete frame is. The crowded and spaced
Keep runs alone verified 837,845 and 95,182 unchanged animated-texture geometry
hits across simulation ticks, without a byte mismatch. Repeating the reference mode alone
also produces intermittent differences; fixing wall-clock interpolation reduced
one source of variation but did not explain all of it. Full-scene pixel
equivalence is therefore **not established**, and the source of the remaining
reference-to-reference variation remains unresolved. No new body/shadow vertex
discrepancy was found by the cache verifier. This evidence supports retaining
the exact-input geometry optimization, while leaving larger image substitution
disabled. It does not establish that every backend or visual case is unchanged.

Captures and comparisons remain in `fixed-visuals/`, `visuals/` and
`reference-repeats/` under the scratch directory. Actual water/boat, live
construction and every corpse/translucency variant were not given matched GPU
full-scene captures; their paths are unchanged and existing focused tests pass,
but that is narrower evidence. The motion comparison uses captured sequences,
not a long interactive play session. UI draw paths and native resolution are
unchanged; dynamic statistics prevent asserting a whole-screen pixel match.

## Larger image reuse: rejected prototype

The prototype extended the existing distant cache with native 64/128/256-pixel
tile classes, rather than adding a second image cache. Exact geometry revisions,
texture frames, scale and pixel phase invalidated images. Four stable frames
were required before admission; changed bodies immediately used live geometry.
It retained the four-page/64 MiB and 128-bake limits, texture capability checks,
premultiplied blending, original painter placement and unchanged special paths.
Changing construction, clipping and transparency cases used direct rendering.

In matched ABBA runs at the actual 1024×768 offscreen fullscreen output, the
500-tower fixture improved from 2.458 to 1.903 ms (23%), adding 32 MiB of images.
The 1500-Keep fixture went from 11.436 to 7.279 ms, adding 48 MiB of images;
actual-frame geometry invalidation alone reached 9.395 ms. Mixed zoomed-out and
animated battle scenes showed no consistent image-cache improvement. These
prototype numbers are a separate experiment, not comparable to the final
1280×960 table.

Basic transparency/subpixel tests were pixel-identical on the tested OpenGL
backend. The full crowded tower frame was not: 4,916 pixels differed, maximum
channel error 255, with a conspicuous overlap region and edge differences.
Forcing both routes through SDL and disabling shadows reduced the discrepancy
to approximately 113 pixels, but some channel errors still reached 223.
The cause was not conclusively isolated: direct versus tile rasterization,
rounding, body batching and shadow/scenery composition require further study.
Passing an isolated sprite test did not establish scene-level equivalence.
This is why the larger-image implementation was removed despite its speed.

Local evidence and the removed prototype are retained outside Git under
`/tmp/tak-model-reuse-study/`: `image-prototype.patch`, `prototype-client`,
`verify-tower-images/`, the CPU profile and per-run logs. The patch is an
investigation snapshot, not a patch to apply wholesale over unrelated work.
Retail-derived captures remain outside Git.

## Zoom and optional supersampling

The existing distant cache already avoids repeatedly rasterizing tiny eligible
bodies. It still needs geometry to validate/cache their image; panning changes
screen geometry and moving/animated bodies frequently lose exact reuse. The
zoomed-out measurements do not justify another sampled-pose cache or reduced
animation rate. No silhouettes, model triangles or animation updates were
quantized/pruned in the retained change.

A separate removed experiment rendered eligible unchanged building images at
two samples per axis: **four total samples**, equivalent to TAK 4x and OA 2x.
At the same 1024×768 prototype output, towers increased from 1.762 to 2.091 ms
(19%), with tracked textures increasing from 199.7 to 231.7 MiB. Keeps increased
from 7.588 to 7.808 ms (3%), from 227.7 to 243.7 MiB. Oversized/changing/special
bodies used native geometry, so this was partial coverage, not an implementation
of optional model AA. It cannot establish the cost of supersampling every model.

Reusable images may make AA affordable for some stationary bodies, but the
pixel-equivalence failure must first be solved. Consistent model AA would also
need coverage of moving pieces, translucency, projectiles, corpses and
construction, correct painter-boundary integration, padding/premultiplied
resolves, resolution admission and backend/allocation fallback. Higher sample
counts need appropriate reduction filtering rather than assuming one bilinear
resolve is sufficient. No whole-world supersampling or settings controls were
restored.

## Limits and next candidates

This study measures one Linux/OpenGL machine. Windows D3D, macOS Metal, AMD,
Intel, high-DPI and live fullscreen/resize interaction have not been performance
or motion-tested here. Automated lifecycle/capability tests cover only their
test backends. It is not a guarantee of a particular battle FPS or of pixel
equivalence on every driver.

Body submission, shadow submission and visibility-dependent geometry copying
remain substantial costs. Separating opaque shadow invalidation from body-only
texture changes may be useful, but cutout/animated alpha masks must remain
correct; it is a future candidate, not a measured improvement in this patch.
For active battles, exact model-image reuse offers few hits. Further work
should profile those submission paths before enlarging the cache or changing
animation appearance.
