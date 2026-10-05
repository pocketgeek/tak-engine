# Independent terrain and model antialiasing

**Current application policy:** Version 0.7.24 restores Terrain
AA, Model AA, Bilinear Filtering, Smooth GUI Art and Smooth Movies to Graphics
options. All five default to Off and remember saved choices. Smooth GUI Art
reloads the current title-screen or in-game interface immediately, including
fonts and cursors; no restart is required. Version 0.7.23 fixed these options
off and omitted their preferences when saving.

Graphics options provide **Terrain AA: Off / 2x / 4x** and
**Model AA: Off / 2x / 4x / 8x / 16x**. The settings are independent. Samples
are relative to final drawable pixels: each dimension is rounded up after
multiplication by `sqrt(samples)`. Thus 16x uses four times the width and height,
not sixteen times either dimension. Terrain 4x / Model Off leaves models native;
Terrain Off / Model 16x leaves terrain native.

## Rendering design and ordering

The old whole-scene supersample target in `main.cpp` is removed. `SelectiveAA`
owns a reusable target for each enabled pass. Terrain resolves before scenery
or models are drawn. Model geometry resolves at its original painter-order
position. Consecutive body geometry runs share a resolve, but bitmap scenery,
special construction/occlusion draws and the airborne-shadow boundary end a run.
This is not a final overlay containing every model.

Terrain uses a drawable-sized target. Models use a reusable target covering at
most a 1024×1024-native-pixel tile, with a two-pixel filter guard. Larger geometry
runs are drawn tile by tile and fully resolved before proceeding to the next
painter-order operation. Triangles outside each padded tile are culled before
submission. Tile cells are anchored to the screen rather than each geometry
batch’s bounds. Vertex translation uses an integer supersample-grid origin
aligned to the intermediate resolve. Moving neighbours therefore do not change
a stationary face’s texture sampling or downsample phase. A packed-atlas
regression covers this with interleaved batches at every enabled model AA level.
Changing the tile size under memory pressure can still produce small silhouette
coverage differences from SDL’s floating-point rasterization. Only the used part of the target is cleared and
resolved; clipping remains the caller's clipping. Projection, animation,
texture selection, team colors, waterline offsets, terrain occlusion and picking
are unchanged. Distant cached body images are bypassed while Model AA is requested,
so all models use their current geometry rather than a lower-resolution impostor.

| Content | Pass |
| --- | --- |
| Terrain tiles, terrain chunks, overview underlay, water/shading baked into terrain art | Terrain AA |
| Units, buildings, model corpses/replacements, animated construction bodies, translucent placement ghosts and terrain-hidden model silhouettes | Model AA |
| 3DO projectile meshes and detached model debris | Model AA |
| Bitmap scenery, including tree/ruin sprites and their animations | Native, at the existing painter position |
| Ground, airborne, scenery, boat and projectile shadows | Native, with existing silhouette caches/filtering and ordering |
| Sprite water effects, particles, construction sparkles/glows, beams, sprite projectiles and explosions | Native, at their existing positions relative to models |
| Fog, selection rings, order indicators, HUD, menus, text and cursor | Native |
| Model portraits/icons inside the UI | Existing icon cache, outside either AA pass |

Transparent model targets store premultiplied color from ordinary source-alpha
geometry draws, then resolve with ONE / ONE_MINUS_SRC_ALPHA. The resolve does
not multiply edge alpha again. Native additive effects keep their existing blend
mode. 8x and 16x use a half-size intermediate downsample before the final resolve,
so 16x does not collapse to just four bilinear taps. The intermediate overwrites
its dirty area without an additional alpha blend.

## Settings and lifecycle

`settings.ini` stores `terrainAA` and `modelAA`. Legacy `antiAlias` values
Off/2x/4x initialize both missing new settings to that level. Explicit new keys
win independently, regardless of line order. Saving writes only the new keys.
Invalid numeric values round down to a supported step within the slider's range.
Defaults remain Off.

Changes are picked up by the next world draw. Targets are recreated on drawable
size changes, including fullscreen/high-DPI changes, and released on renderer
target reset and view teardown. World coordinates remain final drawable pixels;
camera zoom does not multiply AA. Native UI rendering requires no inverse-AA
scale correction.

## Resource limits and fallback

Terrain has a 512 MiB target ceiling, further bounded to half the client's
shared texture-memory cap. Model targets have a 128 MiB ceiling, further bounded
to one quarter of that cap. Both also respect the currently remaining shared
allowance. The shared cap still defaults to 1.25 GiB and can tighten after GPU
allocation failures; it is an application budget, not detected physical VRAM.
Terrain allocates first. Neither pass exceeds the backend's reported maximum
texture dimensions; the old arbitrary 7/8 dimension margin is removed.
Enabling Model AA releases dormant distant-model image pages before either AA
pass is admitted: those native-resolution images cannot be used for exact model
supersampling. Switching Model AA off rebuilds that cache as needed.

Model AA shrinks its native tile from 1024 to 512, 256, 128 or 64 pixels per
axis before reducing samples. This makes 16x available on large/wide outputs
without allocating a four-times-output-width texture. The largest 16x model
allocation (including its intermediate and guards) is about **80.70 MiB**,
independent of output dimensions above 1024 pixels per axis. At 4x it is about
16.16 MiB. Small windows use correspondingly smaller tiles. Under a 1 MiB
remaining allowance, the model planner still fits 16x using a 64-pixel tile
(about 0.36 MiB), at the cost of more submissions/resolves. Off allocates no AA
targets. These are RGBA texture byte counts, not driver overhead or total VRAM;
the CPU-side translated-vertex scratch buffer is retained and reused separately.

Unsupported accelerated target/custom-blend/filtering capabilities, real
texture-size limits or allocation failures can still step down to Off. Software
renderers use Off. Bind/resolve failure draws the unfinished tile and subsequent
tiles natively, without re-blending tiles already resolved. Preferences remain
unchanged; the graphics slider reports the effective level. Logs identify the
specific reason (software renderer, unsupported targets, backend dimensions,
shared/pass budget, allocation, blending/filtering, or bind/resolve failure),
plus target memory and model tile dimensions. A degraded pass retries at most
once every three seconds; resize/settings/reset allow immediate reconfiguration.
A working degraded target is retained during retry backoff. Successful targets
are reused, with no per-draw target allocation. One pass never multiplies the
other's sample count.
Rechecking an unchanged size/budget fallback also retains its existing textures,
rather than reallocating every three seconds. Budget growth can restore a higher
level; budget shrink triggers readmission after backoff. Unsupported software,
target, blending or filtering capabilities wait for reconfiguration/reset rather
than repeatedly retrying a capability that cannot change in place.

At 3840×2160, Terrain 4x needs 126.56 MiB; Model 16x needs about 80.70 MiB,
for about 207.27 MiB combined instead of a screen-sized model allocation of
632.81 MiB (which previously exceeded the model budget and fell back to 4x).
Terrain still uses a full-size target and can fall back at extreme dimensions.
The model tile planner supports 7680×4320 output at 16x with the same 80.70 MiB
allocation, including on an 8192-pixel-max-texture backend.

## Validation

Automated coverage exercises all 15 setting combinations, preference round trips,
legacy migration and explicit-key precedence, pixel-count scales, texture/memory
limits, fallback reporting, resize/reset recovery, cleanup and target reuse. Compact-target tests also compare
large and memory-constrained small tiles over a 1500×1100 translucent scene,
including nonzero viewport and clip rectangles, screen-edge coverage and shared
filter phase across tile boundaries. Interior pixels at all four enabled sample levels must agree within three
color levels per channel. Different tile sizes can shift a handful of silhouette
coverage samples: the OpenGL test observed 14 channel differences above three
levels at 8x, with a maximum of 11, all within one pixel of the silhouette.
The synthetic renderer test checks interleaved model/scenery painter order,
translucent blending, edge color, clipping, moving geometry and native one-pixel UI.
It runs under the software backend in CTest; `selective_aa_test --gpu` additionally
exercises accelerated targets and writes comparison BMPs to the temporary
`tak-selective-aa-results` directory.

Run the focused tests with:

```sh
ctest --test-dir build-dbg -R '^(settings|override_settings|selective_aa|geometry_tiles|geometrysubmit|terrain_cache|distant_models)$' --output-on-failure
SDL_VIDEODRIVER=x11 SDL_RENDER_DRIVER=opengl ./build-dbg/selective_aa_test --gpu
```

## Original full-screen-target measurements (2026-10-02)

These measurements predate compact model targets; they describe the original
selective-AA implementation for comparison. See the update below for the current
implementation.

Linux X11/OpenGL, NVIDIA RTX 5070 Laptop GPU (615.71.09), Core Ultra 9 275HX,
1920x1080 drawable. Both versions used Debug builds, the same retail data,
`Ulasem Arena`, and the existing `TAK_SHADOW_BENCH` fixture. The sparse fixture
spawns 24 models (plus the monarch), zoom 1.25; dense spawns 1,200, zoom 0.55.
They exercise animated mixed-size bodies, buildings, ground/air shadows and
scenery interleaving, but are not a sustained combat benchmark.

Each process ran for 13 seconds. Samples from wall time 5–12 seconds use
`TAK_PROF=1 TAK_PROF_FRAMES=1 TAK_PROF_FINISH=1`: completed GPU frame time
(including update, animation, draw and present), excluding the later frame-cap
sleep. The original executable was saved before this change. These are single
local runs, not a claim of portable speedups; scene scheduling and background
load affect the results, particularly the small Off-to-Off difference.

| Old whole-scene AA | Sparse median / p95 ms | Dense median / p95 ms | AA targets MiB |
| --- | ---: | ---: | ---: |
| Off | 2.60 / 4.95 | 13.44 / 20.09 | 0.00 |
| 2 | 2.59 / 6.52 | 13.40 / 19.84 | 15.83 |
| 4 | 2.65 / 5.53 | 13.74 / 20.46 | 31.64 |

| Terrain / Model | Sparse median / p95 ms | Dense median / p95 ms | AA targets MiB |
| --- | ---: | ---: | ---: |
| Off / Off | 2.13 / 5.57 | 12.09 / 16.64 | 0.00 |
| Off / 2 | 2.24 / 7.10 | 15.82 / 22.89 | 15.83 |
| Off / 4 | 2.21 / 5.79 | 15.51 / 22.67 | 31.64 |
| Off / 8 | 2.33 / 5.67 | 16.72 / 22.84 | 79.12 |
| Off / 16 | 2.43 / 5.85 | 16.53 / 22.13 | 158.20 |
| 2 / Off | 2.12 / 5.32 | 12.39 / 17.12 | 15.83 |
| 2 / 2 | 2.35 / 5.49 | 15.44 / 22.11 | 31.66 |
| 2 / 4 | 2.46 / 6.55 | 15.61 / 22.68 | 47.47 |
| 2 / 8 | 2.46 / 3.89 | 16.88 / 22.04 | 94.95 |
| 2 / 16 | 2.33 / 3.58 | 16.65 / 22.45 | 174.03 |
| 4 / Off | 2.25 / 5.14 | 12.10 / 16.70 | 31.64 |
| 4 / 2 | 2.44 / 3.63 | 15.28 / 22.10 | 47.47 |
| 4 / 4 | 2.48 / 5.53 | 15.56 / 23.26 | 63.28 |
| 4 / 8 | 2.51 / 3.96 | 16.79 / 22.23 | 110.76 |
| 4 / 16 | 2.51 / 4.23 | 16.73 / 22.13 | 189.84 |

For this dense scene, independent 4x/4x is about 13% slower than the old 4x,
and 4x/16x about 22% slower. It also needs 63.28/189.84 MiB versus the old
31.64 MiB. Terrain-only AA remains close to native frame time. Resolves at
scenery boundaries and full geometry in place of distant cached bodies are real
costs; the higher model settings are a quality option, not an optimization.

All 15 combinations produced actual game captures in both scenes. The local
comparison PNGs and timing JSON/logs are in `/tmp/tak-aa-benchmark/`; synthetic
BMPs are in `/tmp/tak-selective-aa-results/`. Retail imagery is not committed.
Representative captures were compared against the old 4x output and between
Terrain 4x / Model Off and Terrain Off / Model 16x. Additional Off/Off and 4x/16x
captures exercise Zhon conjuring (translucent body and sparkles) and naval combat
on Varro Passage (water, boat shadows and explosions). A separate small combat
capture includes dragon fire, model debris/death effects, two team colors,
terrain clipping and overlapping trees. The five focused CTests
passed in both Debug and Release; the accelerated synthetic test passed all 15
combinations on the same OpenGL backend. Both local clients were rebuilt.

To reproduce a scene, use an isolated `XDG_DATA_HOME` containing the desired
`TAKengine/TAKingdoms/settings.ini` values, then run a Debug client:

```sh
SDL_VIDEODRIVER=x11 SDL_RENDER_DRIVER=opengl SDL_AUDIODRIVER=dummy \
TAK_SHADOW_BENCH=1200 TAK_PROFILE_ZOOM=0.55 \
TAK_PROF=1 TAK_PROF_FRAMES=1 TAK_PROF_FINISH=1 \
TAK_PROFILE_CAPTURE=/tmp/aa-scene.png \
./build-dbg/takclient game "Ulasem Arena" --data /path/to/tak_data --novsync --testbuild
```

Windows Direct3D, macOS Metal, real high-DPI/fullscreen monitor transitions and
actual device-loss recovery require platform testing. Automated resize/reset
coverage recreates the targets, rather than forcing GPU device loss. Not every
corpse, construction animation, translucent asset or combat effect has been
visually reviewed. They share the covered submission paths; this is not a claim
of an exhaustive asset-by-asset visual audit. Tiny travelling arrows were not
isolated in the projectile capture; long-running dense combat remains unmeasured.


## Compact-model-target update (2026-10-02)

The compact-target implementation was compared with an executable saved immediately
before this update, on the same Linux/X11/OpenGL system and Debug configuration.
The 13-second runs use the same fixture, capture point and 5–12 second frame-time
sampling window described above. Actual drawable size was 1920×1080. These are
single-run observations; small differences are within normal run-to-run noise.

| Fixture models | Terrain / Model | Before median / p95 ms | After median / p95 ms | Model target MiB before → after |
| ---: | --- | ---: | ---: | ---: |
| 24 | 4 / 4 | 2.40 / 5.54 | 2.38 / 4.57 | 31.64 → 16.16 |
| 24 | 4 / 16 | 2.59 / 3.42 | 2.48 / 5.61 | 158.20 → 80.70 |
| 1200 | 4 / 4 | 16.72 / 23.63 | 16.46 / 24.17 | 31.64 → 16.16 |
| 1200 | 4 / 16 | 17.36 / 22.82 | 16.56 / 22.16 | 158.20 → 80.70 |

At **3840×2160**, requesting Terrain 4x / Model 16x previously produced effective
4x / 4x with 253.13 MiB of combined AA targets. It now produces effective
**4x / 16x with 207.27 MiB**. Sparse-scene median / p95 frame time was
3.60 / 4.57 ms before and 4.10 / 8.50 ms after. This comparison increases actual
model sample count fourfold; it is not an equal-quality speed comparison. Higher
quality can still cost frame time even while target memory falls. Very small
tiles under pressure also increase repeated submissions and resolves.

At that point, direct OpenGL submission accepted the exact integer 4× scale used
by Model 16x, as well as the existing 1× and 2× scales. Fractional scales and
other backends retain SDL submission. Single-tile model runs use an offset in the
OpenGL transform to avoid a CPU vertex copy; multi-tile runs cull/translate their
triangles into reusable scratch storage. No model vertices or rendering state
are mutated in the simulation.

Validation for that update included all 15 independent combinations on software and
OpenGL, 1,536 accelerated geometry pixel/state comparisons (including 4× scale,
texturing, transparency, offset, clip and offscreen rendering), and tile-boundary
comparisons at 2x/4x/8x/16x. Large versus deliberately memory-constrained tiles
agree internally; small silhouette coverage differences are described above.
No double-alpha seam or gap was observed. Packed-atlas tests also move batch
bounds around stationary geometry: all enabled sample levels remain unchanged
within two channel levels. Before screen-anchoring the tile cells this test
failed at 2x and 8x. The reported crowded-game flicker at 16x has not been
reproduced conclusively.
An 8K model-target allocation test keeps 16x below 81 MiB. The full 8K game view
was not benchmarked. Sparse/dense and 4K game screenshots were visually inspected.

Timing JSON, logs and captures remain local under `/tmp/tak-aa-compact-benchmark`,
`/tmp/tak-aa-compact-final` and `/tmp/tak-aa-compact-wide`. The **wide** directory
contains the confirmed 3840×2160 runs used above. Additional 4x/16x construction,
projectile and naval captures are under `/tmp/tak-aa-compact-special`; construction
and naval images were reviewed for body/effect placement, transparency and water
ordering. These are representative captures, not an exhaustive asset audit.
All six focused CTests passed in both Release and Debug, and both clients were
rebuilt. The independent-AA and geometry-submission accelerated tests passed on
this OpenGL driver; Windows Direct3D and macOS Metal still require native
platform testing.

### Screen-anchored tile follow-up

A 1,200-unit 1920×1080 Debug/OpenGL check at Terrain 4x / Model 16x
after anchoring the tile cells measured 18.83 ms median / 24.10 ms p95,
compared with the earlier compact-target run's 16.56 / 22.16 ms. These are
single runs, not an isolated timing study. Fixed cells can require additional
submissions when a small batch straddles a cell boundary. Model target memory
remained 80.70 MiB and effective model AA remained 16x. This change fixes a
reproduced sampling instability; it does not claim a speed improvement.
Follow-up logs and the crowded-scene capture are in
`/tmp/tak-aa-flicker-benchmark`. Windows and macOS remain untested locally.

## AA cost and lifecycle review (2026-10-04)

The review keeps the sample counts, filter kernels, screen-anchored sample grid
and painter boundaries unchanged. It does not reuse native-resolution distant
images as supersampled models or reduce animation frequency.

Model/material bounds are now calculated in the existing parallel body-copy
jobs. For a tiled model run, entire ranges outside a tile are rejected together,
ranges fully inside are copied in their original order, and only boundary ranges
need individual triangle tests. Outward-rounded range bounds conservatively
preserve the original triangle inclusion rules, including subpixel positions.
Single-tile runs also avoid the former serial scan of the whole vertex stream
for bounds. AA Off skips index construction. The index costs 32 bytes per
model/material copy range and 16 bytes per draw operation on 64-bit builds,
plus reusable translated-vertex scratch; it adds no texture targets.

Stable budget/size fallbacks no longer destroy and recreate the same targets
every three seconds. Working targets survive shared-allocation backoff, and
budget changes are readmitted. Native fallback draws reset stale tile-culling
state, including nested-target, scaled-renderer and nonfinite-bounds cases.
Unused distant-image pages are released when Model AA is enabled. Actual driver
allocation failures can still cause a bounded three-second retry of a larger
terrain target; this is distinct from the stable planner fallback now fixed.

Image comparisons also exposed a pre-existing race in terrain-height sampling:
parallel geometry workers shared a mutable one-entry height memo. It could
return another worker's height and change a unit's lift/terrain clipping. The
memo is removed, terrain height initialization runs before worker dispatch, and
the exact bilinear calculation is unchanged. The normal zero horizontal-lift
scale skips unnecessary sampling after initialization. Repeated 600-frame runs
now agree in every frame's geometry counts, resolves and cleared pixels; before
this fix, 26 frames in a repeated run differed by one unit's geometry.

### Measurements

Linux, NVIDIA RTX 5070 Laptop / driver 615.71.09, Core Ultra 9 275HX, vendored
SDL 2.32.10 OpenGL. Both clients use the same optimized Debug flags (`-O2 -g`),
fixed simulation/animation steps and camera paths, with vsync and frame limiting
off. Fixed-size measurements use SDL's offscreen OpenGL backend. Each run has
600 frames, discarding the first 180. GPU completion is awaited; the reported GL
timer measures the draw timeline including submission gaps, not GPU utilization.
Screenshot capture is measured separately because readback changes frame time.

The first 4K sweep, before fixing the height-sampling race, isolates the range
index in the same executable, alternating the
reference and optimized path in ABBA order. Values are means of two runs per
path. Animated means 1,200 moving Archers; buildings means 200 spaced Keeps.
All AA settings shown remained effective, and simulation hashes matched.

| Scene, Terrain / Model | Frame ms, reference → indexed | Body phase ms, reference → indexed | AA target MiB |
| --- | ---: | ---: | ---: |
| Animated, Off / Off (control) | 8.509 → 8.621 | 2.231 → 2.186 | 0 |
| Animated, 4 / 4 | 10.340 → 10.097 | 4.432 → 3.794 | 142.72 |
| Animated, 4 / 8 | 11.808 → 11.270 | 5.502 → 4.926 | 166.94 |
| Animated, 4 / 16 | 11.691 → 11.010 | 5.032 → 4.294 | 207.27 |
| Buildings, 4 / 4 | 4.356 → 4.508 | — | 142.72 |
| Buildings, 4 / 8 | 4.600 → 4.571 | — | 166.94 |
| Buildings, 4 / 16 | 4.830 → 4.832 | — | 207.27 |

That sweep shows approximately 2–6% lower overall frame time and 10–15% lower
body-phase time for the crowded fixture. The building fixture has no consistent overall improvement, despite
slightly cheaper submission; its 4x run is slower. The Off control and repeated
legs show run-to-run variation. This is a targeted reduction in repeated CPU
work, not a claim that AA becomes free or that every scene gets faster. Target
bytes are unchanged. Painter boundaries still require many resolves: roughly
73 per frame in the 1280×960 animated fixture. Flattening those boundaries would
change scenery/shadow occlusion and is not part of this optimization.
For the crowded 4/16 case, mean per-run p95 improves from 15.195 to 14.125 ms;
the Off control changes from 10.909 to 11.151 ms. Sampled peak process RSS across
these runs spans 456–550 MiB without a consistent increase from indexing. RSS
includes assets and driver allocations and is not the AA target-memory count.

A final ABBA check with the height race fixed in **both** paths produced:

| Animated 4K | Mean / p95 frame ms, reference → indexed | Body phase ms | GPU draw timeline ms |
| --- | ---: | ---: | ---: |
| Off / Off control | 8.495 / 10.649 → 8.481 / 10.529 | — | — |
| Terrain 4 / Model 16 | 11.362 / 15.031 → 11.710 / 15.518 | 4.953 → 4.424 | 5.734 → 5.741 |

The model-submission saving is repeatable, but an overall FPS or stutter
improvement is **not** established on this system. In the final run, shorter
draw submission was offset by longer present/GPU-completion waits. The small,
portable range index is retained for its reduced repeated CPU geometry work;
it adds no draws or targets. Full-frame behavior remains limited by other phases
and GPU synchronization. Average sampled peak RSS was 537 → 549 MiB in that
4/16 comparison; target memory remained exactly 207.27 MiB.

### Experiment not retained

A separate OpenGL prototype pre-scaled only positions for Model 2x/8x while
retaining existing color/UV arrays. It matched SDL bit-for-bit in 2,688 paired
pixel/state cases at both O0 and O3, and reduced isolated large-batch submission
times by 28–46%. However, repeated full-game comparisons were mixed: the final
4K 2x case improved 10.789 → 10.254 ms, while 8x regressed 11.204 → 11.768 ms.
An earlier round had the opposite pattern. That does not justify another GL
submission path and up to 16 MiB of position scratch per submission object.
The prototype is removed; fractional scales retain SDL submission. The expanded
geometry/state tests remain, including skewed silhouettes and anisotropic scales.

### Final validation

All 15 independent combinations pass the accelerated synthetic checks, including
transparent edges, painter order, native UI, viewport/clip handling, sample-grid
stability, resize/reset, fallback, cleanup and stable target reuse. The expanded
geometry suite passes 2,688 pixel/state comparisons on both software and OpenGL.
The range index passes 1,600 exact vertex-stream comparisons, also checked with
AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer.
Both full local build trees were rebuilt. All eight focused Release CTests and
nine Debug CTests passed (Debug additionally runs the real-asset geometry-reuse
check). The normal Release binaries also pass the accelerated AA and geometry
tests, independently of the optimized Debug benchmark build.

With the height fix common to both paths, **156 same-binary capture pairs are
pixel-identical**, including native UI: 72 at Terrain 4 / Model 16, 72 at Model
2/8 with Terrain 4, and 12 Off controls. These 1280×960 comparisons cover spaced
buildings, moving units, battles, changing zoom, scenery overlap, construction
transparency/sparkles and boats in actual deep water with shadows. They include
ten consecutive motion frames per case. All 26 capture clients released their
tracked texture/buffer resources on teardown. The projectile fixture activated,
but its sampled frames did not isolate a bolt in flight; this remains a visual
coverage gap, not a claim that every projectile/corpse asset was reviewed.

Linux/OpenGL is the accelerated backend tested here. Windows Direct3D, macOS
Metal, other GPUs, real high-DPI/fullscreen monitor transitions and actual GPU
device loss still require native testing. SDL's offscreen backend did not resize
its drawable correctly in the synthetic resize test, so that test uses X11;
fixed-size offscreen measurements and captures use their verified output sizes.

### Reproducing the review

The harness records frame-time distributions, CPU draw phases, GL timing,
process RSS, texture bytes, geometry counts, resolves, target switches, cleared
pixels and translated/culled vertices. It can reproduce the comparison using an
optimized Debug client:

```sh
python3 tools/aa_benchmark.py --client /path/to/optimized-debug/takclient \
  --data /path/to/tak_data --output /tmp/aa-matrix --frames 600 --warmup 180
python3 tools/aa_benchmark.py --client /path/to/optimized-debug/takclient \
  --tile-reference --data /path/to/tak_data --output /tmp/aa-4k \
  --scenes animated spaced-keeps --combinations 0:0 4:4 4:8 4:16 \
  --width 3840 --height 2160 --frames 600 --warmup 180
```

Use `--reference /path/to/saved-client` to compare executables built with the
same flags. `--capture --frames 721` adds fixed-frame stills and a ten-frame
motion sequence; those runs must not be used for timing comparisons. Logs and
retail captures from this review are local under
`/tmp/tak-aa-review-20261004/`, not committed assets.
