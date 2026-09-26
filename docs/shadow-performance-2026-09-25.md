# Shadow rendering performance

The reported slowdown was reproduced with a local rendering workload on Ulasem
Arena: 960 visible units, 1920×1080, accelerated OpenGL, 4× antialiasing
(3840×2160 internal target), vsync disabled. The mixed roster contains Hunters,
Archers, Aramon and Taros lodestones, Rocs, and Veruna Warriors. A second workload uses
960 Hunters. Units remain idle; these are rendering measurements, not simulation
capacity claims. Results are medians of one-second profiler samples after warmup.

| Workload | Original | Geometry + mask atlas | Single silhouettes | Shadows disabled |
| --- | ---: | ---: | ---: | ---: |
| Mixed 960 | 110 FPS | 134 FPS | 162 FPS | 239 FPS |
| 960 Hunters | 80 FPS | 93 FPS | 116 FPS | Not measured |

## Geometry and texture changes

The worker-side shadow collector emits opaque positions directly, avoiding body
texture lookup, lighting, depth, and full triangle records. It retains the old
piece traversal, transforms, triangle order, and corpse culling. The developer
`TAK_SHADOW_VERIFY=1` check compares its vertices, mask UVs, colors, and order with
the previous collector. Mixed-unit and ordinary build/death fixture captures
passed this check.

Cutout mask frames share padded static texture pages. Player-color and animated
frames retain distinct regions. Adjacent mask triangles are submitted together
on accelerated renderers. Software rendering keeps standalone textures and
individual triangle submissions because the bundled SDL software rasterizer
changes sampling when it recognizes a batched quad.

`shadow_test` checks transparent, opaque, and fractional coverage; ordered batching;
and atlas edges at fractional screen positions under nearest and linear filtering.
OpenGL nearest sampling is pixel-identical; linear sampling differs by at most
one channel value. The software fallback also passes.

## Single silhouettes

The accelerated path now rasterizes each unit into a separate transparent tile,
then applies the shadow darkness once. Opaque faces overwrite coverage; cutout
faces use black/original-alpha textures with ordinary alpha-over blending.
Distinct units retain distinct tiles, so their shadows can still darken each other.
This fixes the previous dark internal lines where one model overlaps itself.

Tiles retain the actual renderer scale, including supersampling. Height-ordered
shelf packing prevents tall models from wasting most of each row. Pages start
small for sparse scenes, grow to at most 2048×2048, and are reused. Four target
pages cap silhouette textures at 64 MiB; paired cutout coverage assets have a
separate 16 MiB cap. Allocations also obey the shared GPU budget. Unsupported
renderers, oversized silhouettes, or exhausted budgets use the optimized direct
path, which still has the old self-overlap limitation.

Each page draws all opaque geometry together and groups cutout geometry by
texture. Independent tiles make this reordering safe. Scratch buffers are reused;
shadow bounds are computed on geometry workers. Device resets discard the target
pages and cached texture references. Render target, viewport, scale, clipping,
blend mode, and draw color are restored before scene composition.

The existing ground-shadow prepass and deferred airborne shadow boundary remain.
Retail interleaves every unit's shadow with its body; our ground prepass remains
an ordering approximation. Scenery, projectile shadows, and baked terrain shading
are unchanged.

Tests check opaque self-overlap, transparent holes, partial coverage, independent
unit shadows, fractional coordinates, and 1×/2× renderer scale. The complete
geometry comparison still checks the fast collector against the previous one.

## Retail evidence

The native `KINGDOMS.icd` shadow walker and mode-15 span rasterizer were exercised
headlessly with `tools/re/check_shadow_texture.py`. They retain texture coverage,
including transparent texels. No retail game window was launched.

Projection shear, flight-altitude placement, animated pieces, current texture
frames, and shadow exclusions are retained. Ground pathfinding, simulation, and
network state are untouched.

## Validation

All 56 Release and 59 optimized-debug CTest cases pass. The separate accelerated
OpenGL shadow test passes in both builds. Geometry verification also passes in the
software build/death fixture and an accelerated saved-match replay using the
silhouette path. Both all-target builds are current. The native texture-coverage probe
passes; simulation and pathfinding code were not changed.

## Reproducing the workload

The optimized developer client accepts `TAK_SHADOW_BENCH=960` with `game
"Ulasem Arena" --testbuild --nofog`. `TAK_SHADOW_BENCH_TYPE=zonter` selects the
Hunter-only workload; `TAK_NOSHADOW=1` provides the disabled comparison. Use
`TAK_PROF=1`, identical window/AA settings, and disable vsync.
`TAK_SHADOW_DIRECT=1` selects the optimized direct fallback for comparison. The fixture honors
player and type limits and bounds the requested count at 16,000. Screenshot mode
normally selects software rendering, so it must not be mistaken for an accelerated
performance run.
