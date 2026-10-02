# Independent terrain and model antialiasing

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

Each target uses the drawable's coordinate grid, avoiding changes in sampling
phase as models move. Only a padded rectangle enclosing the geometry is cleared
and resolved; clipping remains the caller's clipping. Projection, animation,
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

Each pass has a 256 MiB target limit, including its intermediate. Allocations also
respect the renderer's maximum texture dimensions (with the existing 7/8 safety
margin) and the client's shared texture-memory allowance. Terrain allocates first;
model AA uses the remaining allowance. These are RGBA target byte counts, not a
measurement of driver allocation overhead or total VRAM consumption.

Unsupported accelerated target/custom-blend/filtering capabilities, texture-size
limits or allocation failures step down through supported sample levels to Off.
Software renderers use Off. Bind/resolve failures abandon that pass and draw
natively. Preferences are retained; the graphics slider shows an ACTIVE suffix
when the effective level differs, and the log reports requested/effective levels
and target MiB. A degraded pass retries at most once every three seconds; resize,
setting changes and reset allow immediate reconfiguration. Successful targets
are reused without per-frame allocation. Failure of one pass does not enable or
multiply the other.

At 1920x1080 the target costs are approximately 15.83 MiB for 2x, 31.64 MiB for
4x, 79.12 MiB for 8x, and 158.20 MiB for 16x. Add the two independent pass costs.
At 3840x2160, Model 16x exceeds the per-pass budget and falls back to 4x
(126.56 MiB); 8x including its intermediate also exceeds the budget. Larger
outputs may fall back further. Off allocates no AA targets.

## Validation

Automated coverage exercises all 15 setting combinations, preference round trips,
legacy migration and explicit-key precedence, pixel-count scales, texture/memory
limits, fallback reporting, resize/reset recovery, cleanup and target reuse.
The synthetic renderer test checks interleaved model/scenery painter order,
translucent blending, edge color, clipping, moving geometry and native one-pixel UI.
It runs under the software backend in CTest; `selective_aa_test --gpu` additionally
exercises accelerated targets and writes comparison BMPs to the temporary
`tak-selective-aa-results` directory.

Run the focused tests with:

```sh
ctest --test-dir build-dbg -R '^(settings|override_settings|selective_aa|terrain_cache|distant_models)$' --output-on-failure
SDL_VIDEODRIVER=x11 SDL_RENDER_DRIVER=opengl ./build-dbg/selective_aa_test --gpu
```

## Local measurements (2026-10-02)

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
