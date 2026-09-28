# Authored model-piece flags

The retail interpreter forwards four distinct switches to the unit model. The
`RENDER_ON` / `RENDER_OFF` disassembler names do **not** mean body visibility.
Treating them as SHOW/HIDE would remove parts of fifteen shipped unit models.

## Native evidence

`python3 tools/re/check_piece_render_flags.py` executes the local retail binary
without launching the game. Synthetic model data stays in memory. The native
body and shadow walkers run normally; their final polygon submission is captured.

| COB switch | Native model bit | Effect |
| --- | --- | --- |
| SHOW / HIDE | 0 | This piece's body and shadow visibility |
| CACHE / DONT_CACHE | 1 | Selects cached versus dynamic body pass; only cached pieces cast shadows |
| SHADE / DONT_SHADE | 2 | Enables directional palette shading when owner lighting is enabled |
| RENDER_ON / RENDER_OFF | 3 | Enables this piece's projected shadow; does not hide its body |

The virtual callbacks at `0x50d7f0`, `0x50d860`, `0x50d8c0`, and `0x50d910`
write those bits in model-node `+0x2a`. The probe checks every starting bit
combination and both new values, including preservation of unrelated bits.

The body walker `0x4ed2d0` admits visible pieces matching the requested cache
pass (`0x4ed3d0`). Across the two passes every visible piece draws once. The
shadow walker `0x4eda80` requires bits 0, 1, and 3 together (`0x4edab8`).
SHADE does not affect shadow admission. The probe checks all sixteen combinations.
These are the shared model rasterizers reached by the Glide model path described
in [retail-engine.md](retail-engine.md), not the separate software unit-shadow
sprite path.

With lighting enabled, `0x4ed7b4` selects directional intensity for SHADE pieces;
DONT_SHADE uses shade row 15. With lighting disabled, the final span rasterizer
bypasses the shade table entirely. The distinction matters because authored row
15 may contain palette aliases rather than an exact identity mapping.

The native default light vector is `(-0.464991, 0.813733, -0.348743)`. The shade
index is ambient 5 plus truncation of 19 times the nonnegative negated dot product
with the normalized native face normal. The native rasterizer computes a reversed face normal. For the engine's
outward normal, use the positive dot product with that same light vector.
The final engine body transform already includes the authored/native half-turn;
negating the light's X/Z components again is incorrect. That mistake darkened
the camera-facing sides of structures, particularly the Death Totem, and was
corrected on 2026-09-28. Owner lighting admission at
`0x4ec4e6` uses the structure flag or the two visual overrides, then the shading
option. The synthetic horizontal quad returns index 20 with lighting and SHADE,
and index 15 otherwise. The oracle now passes the normals of all 289 sloped faces to the actual
production C++ shading helper and compares its intensity with native output.
The earlier oracle checked a separately written expected equation, which missed
the extra coordinate conversion in the production renderer.

The indexed lookup is the **shade** table, not the light table: `0x4c2220` loads
`palettes/<name>.shd`; `0x5477d0` copies its 8192 bytes to active palette `+0x28`.
The span rasterizer (`0x541d2f`, `0x541e54`) indexes that table with
`shadeLevel * 256 + sourcePaletteIndex`. `.lht` occupies the separate `+0x2c`
slot. The probe executes all 32 rows against a nonlinear synthetic lookup,
checks the unlit bypass, and verifies transparent texels leave the target intact.

## Engine handling

Both VM backends retain all four switches in exported `PieceState`. Sparse
animation conversion copies them, and the geometry cache key includes each flag.
Prepared geometry and the reference collector apply the same shadow rule;
hidden or shadow-disabled parents still transform and visit their children.
Caching itself remains an engine implementation choice: merging retail's two
body passes is valid provided DONT_CACHE pieces stay visible and shadow admission
is preserved. `cobanim_test` checks all combinations and restoring flags in both
VM backends.

Structure body lighting now uses the authored faction `.shd` table and original
palette indices. The client keeps an indexed copy when decoding model textures,
so aliases and transparent pixels do not depend on reverse RGB matching. Needed
texture frames receive the 20 reachable shade bands (levels 5–24) in shared, lazily allocated pages; changing
light intensity changes UVs within the same page. The additional GPU allocation
is capped at 256 MiB and respects the shared GPU budget. Mobile units retain the unlit path. DONT_SHADE selects the
neutral authored row when owner lighting is on, rather than fabricating an RGB
multiplier.

`TAK_PIECE_VISIBILITY_TEST` exercises all sixteen flag combinations in both
production body/shadow collectors, plus authored Keep shade-band selection and
prepared/reference parity. `TAK_PALETTE_VERIFY` checks uploaded shade-sheet texels
through SDL readback. The full native oracle remains independent of those engine
checks.

The complete shipped structure-texture check covers 51 structures, 757 texture
names and 1,163 frames with no missing shade variants: nine 2048×2048 pages
(144 MiB) if all are loaded. Lazy allocation uses only one page (16 MiB) for
the Keep. Nearest-filtered packing needs no gutters, and each model is prepared
once rather than walking its texture list every frame.

This is a bounded routine comparison, not a claim of complete screenshot parity.
No retail assets or executable code are included in the repository.
