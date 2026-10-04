# Independent terrain and model antialiasing

**Current application policy:** Terrain AA and Model AA are fixed off, along
with Bilinear Filtering, Smooth GUI Art and Smooth Movies. Their controls have
been removed, their saved values (including legacy `antiAlias`) are ignored,
and those keys are no longer written. Renderer implementations and direct
renderer tests remain available. The design, settings migration and measurements
below describe the retained implementation before this policy change.

Graphics options previously provided **Terrain AA: Off / 2x / 4x** and
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
ctest --test-dir build-dbg -R '^(settings|override_settings|selective_aa|geometrysubmit|terrain_cache|distant_models)$' --output-on-failure
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

The current implementation was compared with an executable saved immediately
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

The final direct OpenGL submission path accepts the exact integer 4× scale used
by Model 16x, as well as the existing 1× and 2× scales. Fractional scales and
other backends retain SDL submission. Single-tile model runs use an offset in the
OpenGL transform to avoid a CPU vertex copy; multi-tile runs cull/translate their
triangles into reusable scratch storage. No model vertices or rendering state
are mutated in the simulation.

Current validation includes all 15 independent combinations on software and
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
