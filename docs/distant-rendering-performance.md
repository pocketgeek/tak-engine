# Distant rendering and wide-map submission

The renderer now batches adjacent unit shadow composites from the same atlas
page, preserving painter order, silhouette coverage and overlapping darkness.
It does not lower shadow resolution or merge different units' shadows.

Tiny stationary unit bodies can use a cached image of their current geometry.
Eligibility requires zoom <= 0.6, at least 96 vertices, and projected bounds no
larger than 28 x 28 logical pixels. Selected units, moving/walking units,
construction, clipping, corpses and special effects retain full geometry.
Animation scripts still run normally. Changed cached poses refresh at roughly
15 Hz; position follows the current frame. Zoom/output-scale changes invalidate
cached images immediately. The cache is limited to four 2048-square pages
(64 MiB) and 128 refreshes per frame, with full-geometry fallback for unsupported
renderers, allocation failures, or exhausted budgets. Transparent textures use
premultiplied composition to avoid dark fringes.

Large maps have another cost independent of units: terrain quads and the
terrain-following fog mesh. Terrain and fog now use the existing direct geometry
submission helper on compatible OpenGL renderers, avoiding SDL's per-vertex
copy/reformat work. Other renderers keep SDL submission. Fog geometry is reused
while the camera, viewport and relevant visibility state stay unchanged. At
zoom <= 0.25 it retains transparent cells, allowing visibility texture updates
without rebuilding the mesh. Terrain relief and fog sampling are unchanged.
Adjacent fog cells over flat terrain become horizontal strips with the same
projection and texture mapping; hills and the clamped map-edge boundary keep
their original geometry.
Panning/zooming still rebuilds geometry; this is not terrain tessellation LOD.

## Local measurements

2026-09-27, Core Ultra 9 275HX, NVIDIA RTX 5070 Laptop, Linux SDL offscreen
OpenGL, optimized Debug build (`-O2 -g`), 1280 x 960, AA 4, shadows enabled,
vsync disabled. These are renderer workloads, not multiplayer simulation limits.
Timing excludes the first 15 seconds of each 32-second run.

- 2,000-unit stationary workload, Ulasem Arena, zoom 0.25: initial ABBA runs
  measured 114.5/115.7 FPS with old submission and 163.2/160.2 FPS with distant
  bodies and shadow batching, approximately 29% less frame time. Moving units
  were subsequently excluded after a patrol workload showed sprite rebaking
  could cost more than drawing their meshes.
- Final 16,000-unit patrol comparison: 54.4 ms/frame with body/shadow changes
  disabled versus 55.4 ms enabled (about 2%, with simulation time dominating).
  No distant body images were used for these moving units; this workload does
  not demonstrate a performance gain.
- Ultima Online B1 (1008 x 1008 terrain blocks), three live units, zoom 0.05,
  fog disabled: old terrain submission 22.04 ms/frame (45.4 FPS), revised paths
  10.21 ms/frame (97.9 FPS). This isolates the terrain improvement. Unit count is not the cause of this workload's cost.
- Same wide-map workload with fog enabled: 172.67 ms/frame (5.8 FPS)
  before versus 17.84 ms/frame (56.1 FPS) after, including exact flat-row
  merging and mesh reuse. This is a stationary camera; panning and zooming
  still incur mesh rebuilds.
- Isolated terrain submission at zoom 0.05: roughly 23.3 to 9.7 ms/frame.
  Accelerated readback comparison at zoom 0.05, 0.125 and 0.5 was byte-identical.

The hardware `distant_models_test` verifies cached transparent bodies and
shadow overlap against direct rendering (zero channel error locally), pose
refresh, camera translation, scale/zoom changes, and GPU-memory cleanup. Its
software run checks the fallback and shadow composition; all three platform
CI workflows include it.

Development-only controls for controlled comparisons:
`TAK_DISTANT_MODELS_OFF`, `TAK_SHADOW_BATCH_OFF`, `TAK_TERRAIN_SDL_SUBMIT`,
`TAK_FOG_SDL_SUBMIT`, `TAK_FOG_MESH_CACHE_OFF`. The existing shadow workload
accepts `TAK_PROFILE_ZOOM` and `TAK_PROFILE_FOG`. Release builds ignore these.

## Validation

Release CTest: 86/86; optimized Debug CTest: 89/89. Local accelerated body and
shadow comparisons had zero channel error, as did the terrain comparison.
An accelerated Ultima Online B1 fog screenshot comparison differed only in
three pixels on the animated monarch and the changing HUD, with the remaining
map viewport identical. The software screenshot path has pre-existing tiny-fog
rasterization artifacts, so it was not used to claim visual equivalence.
Both the Release client and optimized Debug client were rebuilt.
