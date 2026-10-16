# Rendering optimization program — progress and baseline

This is the durable record of the rendering optimization program: checklist,
pipeline/ownership map, cache inventory, benchmark contract, measured results
and the resumable checkpoint. Read it first; update it before stopping.

The work is presentation-only. `World::stateHash`, the `--mpai` hashes and
replays must not move; nothing in this program may feed the simulation.

## Resumable checkpoint

- Branch `task-render-opt` (local only, never pushed), worktree
  `~/tak-tmp/wt/render-opt`, started from `origin/main` = `c2c25707`.
- Phase 0 (baseline + test contract) is **done**: telemetry/vocabulary header,
  render-study matrix knobs, `tools/render_benchmark.py`, BEFORE measurements,
  this document. Commits: `e08d7ccf` (telemetry, harness, tools, tests),
  `65230a7d` (benchmark contention gating / re-aggregation), `7c5d9cb8`
  (actual-zoom reporting, binding constrained scene), then the docs commit
  carrying these measurements. Tests: `ctest -L quick` 376/376 pass on
  build-o2 at `e08d7ccf`; focused render tests (render_texture_reuse,
  render_telemetry, render_benchmark_tool, distant_models, shadow,
  geometrysubmit, terrain_cache, tactical_dots, build_controls) pass. `--mpai`
  was not run: no file outside `src/client` (debug harness + presentation
  telemetry) and `tools/` changed, and every benchmark run checks the local
  world hash.
- Nothing from Phases 1–7 (workstreams A–N) is implemented yet. A has its
  shared interface and a first set of counters; the remaining items are listed
  under A below.
- **Next concrete step:** workstream B (animated-atlas dirty regions) and the
  rest of A, measured against the pinned phase-0 baseline
  (`~/tak-tmp/wt/render-opt-p0base`, commit `7c5d9cb8`) with
  `tools/render_benchmark.py --a <p0base> --b <candidate>`.

## Status checklist (A–N)

Status words: not started · in progress · implemented · verified ·
experimental · blocked. "Present" notes what current code already does, with
evidence; nothing below is already fully satisfied.

| | Workstream | Status | Present in current code (evidence) |
|---|---|---|---|
| A | Renderer telemetry | in progress | Phase 0 added `client/rendertelemetry.h` (`FrameStats`, phases, counters, miss causes) and wired candidates, painter sort, atlas, geometry, distant, shadow pack/composite, body assembly/submit phases; visible/drawable/special counts, geometry hits/misses (cold/owner/changed), vertices generated/copied, body submissions, shadow tiles reused/dirty/dirty pixels, atlas regions/pixels repainted, retained geometry-pool and body-buffer bytes vs capacity, terrain chunk/section bytes, tracked GPU bytes. Printed per frame as `RENDERSTATS` in the render study. Missing: per-stage miss causes (need D/E), terrain jobs completed/cancelled/discarded/used, worker dispatch vs wait split, allocation/reallocation counts, native draws/flushes/target switches outside the distant and shadow caches, shadow allocation vs prep split. Pre-existing: `TAK_PROF`/`DRAWPHASE` monotonic phase accumulators (`gameview.h` `prof*Ms_`), `TAK_DISTANT_STATS`, `TAK_CACHE_STATS`, GL timer query in the study. |
| B | Animated atlas dirty regions | not started | `animateGlowTextures()` (`gameview_render.cpp:2309`) computes every sequence's frame, and if ANY changed repaints EVERY animated region in EVERY colour atlas. `paintAtlasRect()` (`:2486`) issues the body copy, eight gutter copies and an `SDL_RenderFlush` per region (the flush protects the scale-mode change of the source texture). New atlases paint the current frame (`atlasFor`, `:2529`) and set `glowDirty_`, which forces a full repaint of all atlases. |
| C | Stable geometry ownership | not started | `geomPool_` is indexed by position in the depth-sorted visible list (`:246`, `:306`): slot i belongs to whichever unit sorts i-th. The exact key's first word is `u.id`, so a unit that moves one place in depth order misses (counted as `miss_owner`). Pool never shrinks. Unit ids are monotonic within one world lifetime (`World::nextId_`), but a world reset (rejoin fast-forward, replay restart) starts them again at 1, and replacement models/corpses keep the id — so ids alone are not a safe cache identity. |
| D | Separate preparation from placement | not started | One exact key (`:2707`–`:2737`) covers identity, pose, appearance AND `zm, ax, ay` (screen anchor). Any camera pan or interpolation step rebuilds the piece transforms, collection, triangle sort and screen transform. `preparePiece` already shares the transformed tree between body and shadow inside one build. |
| E | Split body/shadow invalidation | not started | Body and shadow are rebuilt together on any key miss (`buildUnitGeom` → `buildUnitShadow`, `:2871`). The shadow tile stamp compares the geometry revision AND the screen pixel position, so body-only changes and every pan re-rasterize coverage. |
| F | Persistent shadow allocation | not started | `buildShadowSilhouetteAtlas` (`:377`) repacks every visible shadow into height-sorted shelves each frame; tile stamps are compared by TILE INDEX, so one changed size or one unit entering view shifts and re-rasterizes every later tile on that page. Pages: ≤4, 2048² max, grown in powers of two, retained until reset. |
| G | Distant image lookup before generation | not started | `DistantModels::prepare` (`distantmodels.cpp`) runs after `buildUnitGeom` and needs the generated vertices (bounds, source pointer, revision). Entry key: unit id, geometry revision, texture tick, source pointer, zoom/scale; 67 ms refresh, 128 bakes/frame, 64-px tiles, 4 × 2048² pages; pages released only when ALL entries expire. Revision changes on every pan (D), so panning re-bakes. |
| H | Less full-scene vertex copying | not started (partly present) | `GeometrySubmit` (`geometrysubmit.h`) submits batches ≥512 vertices on the `opengl` backend through client arrays, skipping SDL's per-vertex conversion copy, with an optional retained VBO keyed by a caller revision (used by terrain tiles and fog, not bodies). Bodies are still copied every frame: per-unit `UnitGeom::verts` → `bodyVerts_` (`:782`–`:850`) → client-array upload. No ordered command representation. |
| I | Cost-based work scheduling | not started | `ThreadPool::parallelFor` (`threadpool.h`): fixed 8-item chunks, `minParallel` default 32; the geometry pass dispatches the whole pool for ≥32 visible units whether they are cache hits or misses; shadow prep goes parallel at ≥32768 points; body copy parallel at ≥32 tasks. |
| J | CPU cache budgets | not started | `terrain::Compositor` decodes every referenced section at load (`MapView::queueAllSections`) and keeps it until `clear()` (`terrain.h:40`, reference valid until clear); `TerrainChunks::mips_` (worker-only) is unbounded; `geomPool_`, `bodyVerts_`, shadow scratch keep peak capacity forever. Consumers of `sectionImage`: `terrainchunks.cpp:64`, `mapview.cpp:361/381/470`, Cartographer `validation.cpp:32`; `renderBlock` users: minimap (`gameview_hud.cpp:614`), Cartographer. |
| K | Camera-prioritized terrain | not started (partly present) | Jobs are queued only for chunks visible this frame, ≤64 pending, uploads ≤8 MiB and ≤64 jobs per frame, completed-but-no-longer-visible jobs are dropped at upload, nearest cached level stands in while a level bakes, `finishChunks()` for captures. Missing: priority order (FIFO), cancellation of queued jobs that left the view (only `invalidate`/`clear` cancel), guard/hysteresis region, section/chunk upload coordination. |
| L | Unit spatial index | not started | Features use `FeatureIndex` (`featureindex.h`) with a `TAK_FEATURE_FULL_SCAN` reference. Units: every frame scans all of `front().live` with fog + frustum tests (`:187`). |
| M | Indexed palette path | not started | `preparePaletteTexture` (`:2230`) expands each structure palette frame into 20 shade bands of RGBA in 2048² pages (≤16 pages, 256 MiB). Original per-texture SDL textures (`textures_`) also stay resident as atlas sources. |
| N | Native pass submission | not started | Each native batch (`GeometrySubmit::draw`, `OpaqueShadowSubmit::draw`) primes SDL state with a degenerate draw, flushes, and pushes/pops GL attribute state — once per body run, per shadow page and per mask batch. |

## Baseline record

| Item | Value |
|---|---|
| Commit | `c2c25707a78ec3cf276ed3d059213258ffff87eb` (origin/main, 0.8.3 validation) |
| Branch / worktree | `task-render-opt` @ `~/tak-tmp/wt/render-opt` (clean except the git-ignored `assets`/`third_party` links; the user's `~/TAK` working tree was not touched) |
| Build | optimized Debug: `CMAKE_BUILD_TYPE=Debug`, `CMAKE_CXX_FLAGS=-O2`, ccache + mold, static deps from `third_party/static-deps` (SDL2 2.32.x vendored static), Bink FFmpeg static |
| Compiler | GCC 16.2.1 (Red Hat 16.2.1-2), CMake 4.3.0, Ninja |
| Platform | Fedora 44, Linux 7.2.9-200.fc44.x86_64, Intel Core Ultra 9 275HX (24 logical CPUs), 128 GiB RAM |
| GPU / driver | NVIDIA GeForce RTX 5070 Laptop GPU (8151 MiB), driver 615.71.09 — confirmed by `RENDER_ENV gl_renderer` for every run |
| SDL video / renderer | `offscreen` video driver (EGL, never a visible window on the user's desktop), SDL `opengl` renderer, accelerated, target textures, vsync off, GL 4.6 compatibility, `GL_ARB_timer_query` available |
| Output sizes | 1280×960 (world view 1075×888) and 7680×2160 (world view 7475×2088), render scale 1.0 — read back from the renderer, not assumed |
| Settings | scratch profile per run: fullscreen 0, vsync 0, maxFps 0, uiScale 1, treeSway 1, unitShadows 1, healthBars 1, statsPanel 0, smoothMotion 1, edgeScroll 0, masterVol 0; texture filter default (off); GPU memory limit AUTO (1280 MiB) except the constrained scene (512 MiB); AA options no longer exist (removed after 0.7.24, not reintroduced) |
| Data | `~/tak_data` retail install (read-only); maps Ulasem Arena, Sea Dragon Spine, Athri Cay |

Not exercised on this machine: Direct3D, Metal, software renderer timing,
other GPUs, Wayland/X11 visible windows, high-DPI output scales, fullscreen.

## A/B mechanism and repeatability

- **Pinned baselines.** `~/tak-tmp/wt/render-opt-base` = `c2c25707` (the
  starting commit) and `~/tak-tmp/wt/render-opt-p0base` = `7c5d9cb8` (starting
  commit + phase-0 telemetry/harness, no draw changes; it was at `e08d7ccf`
  for experiments 1–2, whose code differs only by the zoom printout and the
  debug GPU-cap override). Each has its own
  `build-o2/takclient`. Later workstreams compare against **p0base**, because
  the new matrix scenes need the phase-0 harness knobs. Experiment 1 below shows
  p0base and the starting commit produce identical world hashes and identical
  captures with timing inside A/A noise.
- **Same-binary reference switches** already in the code (debug only):
  `TAK_GEOMETRY_NOCACHE`, `TAK_GEOMETRY_REFERENCE`, `TAK_GEOMETRY_VERIFY`,
  `TAK_TEXTURE_TICK_REFERENCE` (only restores tick-based texture invalidation
  — NOT a general baseline), `TAK_SHADOW_NOCACHE`, `TAK_SHADOW_CACHE_VERIFY`,
  `TAK_SHADOW_VERIFY`, `TAK_SHADOW_DIRECT`, `TAK_SHADOW_BATCH_OFF`,
  `TAK_SHADOW_SERIAL_PREP`, `TAK_SHADOW_SDL_SUBMIT`, `TAK_BODY_SDL_SUBMIT`,
  `TAK_TERRAIN_SDL_SUBMIT`, `TAK_TERRAIN_CACHE_OFF`, `TAK_FOG_*`,
  `TAK_DISTANT_MODELS_OFF`, `TAK_FEATURE_FULL_SCAN`, `TAK_NOSHADOW`,
  `TAK_PALETTE_VERIFY`. New workstreams add a reference switch only where it
  is a cheap branch.
- **Repeatability controls.** Standalone local simulation (no server, no
  network timing); fixed 1/60 s frame clock (`TAK_RENDER_STUDY`); motion
  interpolation fixed at α = 0.5; distant-image refresh on the animation clock;
  frame-indexed camera paths and event schedules; fixed scene seeds (spawn grid,
  headings, orders). Every run prints the world hash; the tool refuses a scene
  whose runs disagree. 900 frames, 300 warmup, 600 measured. Captures are
  separate runs at frames 360, 480–489 and 720.
- **Verification of what ran.** `RENDER_ENV` prints the video driver, SDL
  renderer, GL vendor/renderer/version, actual output and world-view size and
  render scale; the tool refuses a run whose renderer or output size differs
  from the request. `RENDERSTATS` prints visible/drawable counts, the zoom actually in force and
  the tracked-GPU cap per frame.
- **Timing hygiene.** The client is pinned (`taskset -c 16-23`); the tool
  records load average, the number of running `legion_scenario` processes and
  the busy fraction of the pinned CPUs for every run. A/B uses ABBA groups.
  Frame time includes a `glFinish` before present (forced-completion
  diagnostic timing; `--pipelined` measures normal pipelined frames). GPU time
  is the `GL_TIME_ELAPSED` draw **timeline** — it includes gaps where the GPU
  waits for CPU submission and is not device busy time. No captures, verifiers,
  profilers or compilation run inside timing runs.
- **Regression threshold rule.** A change is material only when it exceeds
  `max(2 × A/A relative spread of run means, 2%)` for that scene, size and
  metric (see the A/A table below). The tool marks `significant` accordingly.

## Pipeline and ownership map

Main loop (`main.cpp`): `presentFrame()` pins the newest published snapshot →
input → `prepare()` (camera clamp, terrain section intake, minimap) →
`cameraFrame` / sim step (local) or network pump → `animFrame` (COB VMs, every
frame) → `draw()` → cursor overlay → `endFrame()` → `SDL_RenderPresent`.

| Stage | Owner (current file:line) | Data / threads |
|---|---|---|
| Snapshot | `GameView::captureFrame` (`gameview_impl.cpp:848`) writes the triple-buffered `Frame` on the sim worker; `presentFrame` (`:2990`) picks the slot; `front()` | `UnitR` mirror of sim fields; render never reads live `World` except documented exceptions |
| Interpolation | `interpPose` (`gameview_impl.cpp:981`), α from presentation clock (`draw` `:256`) | per unit, worker-safe |
| Terrain | `MapView::draw` (`mapview.cpp:289`) → `TerrainChunks::prepare/draw` (`terrainchunks.cpp:118`), fallback tile batch, sections uploaded on demand | chunk composition on its own worker thread; section JPEG decode on a second worker; uploads on the render thread |
| Visibility / candidates | `draw` `:144`–`:230`: `FeatureIndex::query` broad phase + exact screen/fog cull for features; full scan of `front().live` with fog + frustum cull for units | render thread |
| Painter list | `paintItems_` stable sort by (layer, z) (`:233`); air units layer 1 | render thread |
| Slot assignment | `visUnits_` / `geomIndex_` from painter order (`:246`) | render thread |
| Atlas maintenance | `atlasFor` per visible colour slot (`:272`), `preparePaletteTextures` for structures (`:284`), `animateGlowTextures` (`:289`) | render thread, render-target draws |
| Geometry preparation | `pool_.parallelFor` → `buildUnitGeom` (`:306`, `:2614`): exact key, `preparePiece` (`:2574`), `collect` (`:4991`), index sort (`:2791`), screen transform + tint | `ThreadPool pool_`, thread-local scratch, writes only `geomPool_[i]` |
| Shadow preparation | `buildUnitShadow` (`:2871`) inside the same worker build | positions-only opaque verts + masked runs |
| Distant images | `distantModelCache_.prepare` (`:350`, `distantmodels.cpp`) | render thread, offscreen bakes |
| Shadow atlas | `buildShadowSilhouetteAtlas` (`:377`, called `:656`): pack, dirty tiles, parallel transform, `OpaqueShadowSubmit` + mask batches | render thread + pool for the transform |
| Shadow composite | ground: `drawShadowBatch` (`:767`, `:2593`); air: drained at the ground→air boundary inside the body replay | render thread |
| Body assembly | plan `drawOps_`/`copyTasks_` serially (`:782`), parallel copy into `bodyVerts_` | pool |
| Submission | replay `drawOps_` (`:875`): features (sprite copies, sway geometry, flames, smoke), `drawUnit` for special units (`:3044`, clip rects + interleaved particles), batched bodies via `bodySubmit_` (`:1037`) | render thread |
| Effects | dots, build ghosts, flames, storms, beams/shot models (`drawShotModel` runs `collect` live per projectile), projectiles, particles/effects/unit fx (`:1514`), fog (`drawFog`, `:3965`) | render thread |
| HUD | rings, order trails, bars, squad labels, production bars, minimap, panels, chat, menus, unit info (`:1599`–`:2162`) at native resolution | render thread |

## Cache, buffer and resource inventory

| Cache / buffer | Owner | Key / invalidation | Storage & budget | Lifetime / reset |
|---|---|---|---|---|
| Unit geometry (`geomPool_`, `UnitGeom`) | GameView | exact word vector: id, type, visual, atlas ptr, player, slot, pitch/roll, veteran, construction, ghost, corpse, shadows-on, conjuring, disco, zoom, screen anchor, occlusion, altitude, facing, bob/mix, opacity, anim clock (conjuring), animated texture frames, piece poses; `revision` per slot | CPU vectors per painter slot, never shrink; no budget | keys cleared by `invalidateRenderTargets` |
| Body buffer `bodyVerts_`, `copyTasks_`, `drawOps_` | GameView | rebuilt each frame | peak capacity retained | session |
| Shadow silhouette pages | GameView | per-tile stamp (geometry slot ptr, owner, screen pixel pos, slot pos, size, revision, scale) compared by tile index | ≤4 GPU target pages up to 2048² (≤64 MiB), grown, retained | destroyed on reset/teardown |
| Shadow scratch (`shadowAtlasOpaqueScratch_`, mask scratch, `shadowCompositeBatch_`) | GameView | per frame | capacity retained | cleared on reset |
| Distant images | `DistantModels` | unit id → entry (revision, texture tick, source ptr, zoom, scale), 67 ms refresh | ≤4 × 2048² pages (64 MiB), 1024 64-px tiles/page, ≤128 bakes/frame | entries expire after 120 unseen prepares; pages freed only when all expire; cleared on reset |
| Colour atlases `atlasTex_` | GameView | per player-colour slot; layout fixed at first model load | GPU target ≈ 2048×atlasH ×4 (≈11 MiB) per slot, ≤10; essential (bypasses cap) | destroyed on reset; repainted lazily |
| Animated texture state | `modelTextureAnimations_`, `animatedTex_` | frame from game tick | CPU | `glowDirty_` on atlas creation/reset |
| Palette pages | `paletteTextureSheets_/Pages_` | per texture name, 20 shade bands | ≤16 × 2048² static pages (256 MiB) | destroyed in `destroyGpuTextures` (not on target reset) |
| Model source textures `textures_` | GameView | per texture name, per colour/frame | GPU static | teardown |
| Shadow mask/coverage pages | `shadowMaskPages_`, `shadowCoverageTextures_` | per texture/frame | GPU | teardown |
| Feature art, effect anims, icons, fonts, GUI | GameView | by name | GPU static | teardown |
| Terrain chunks | `TerrainChunks::cache_` | (level, cx, cy); `revision`; LRU of chunks not drawn this frame | GPU static, 256 MiB budget and GPU cap; ≤8 MiB uploads/frame | `invalidate`/`clear`; reset clears |
| Terrain mips | `TerrainChunks::mips_` | (section key, level, stock) | CPU, worker-only, **unbounded** | `resetSource` |
| Terrain sections (GPU) | `MapView::sections_` | section key; idle 3 s, 64 MiB idle budget | GPU | trimmed; reset clears |
| Decoded sections (CPU) | `terrain::Compositor::cache_` | section key | CPU, whole referenced set decoded at load, **unbounded** | `clear()` only after consumers stop |
| Terrain fallback batch | `MapView::tileBatch_/apronBatch_` + `tileSubmit_` | camera/offset/zoom/size/dirty | CPU vectors + retained VBO via revision | rebuilt on change |
| Fog | `fogTex_`, `fogUpload_`, `fogSubmit_`, fog mesh | vis generation, camera | GPU texture + retained mesh | reset clears |
| Feature index | `FeatureIndex` | rebuilt when features change (`featureIndexDirty_`) | CPU | session |
| Minimap | `miniTex_`, async crunch | map | GPU | rebuilt |
| Fog LoS masks | `World::visMaskCache_` (client-local, never hashed) | (sight, radar, cell) | CPU, 8192 entries | session |

GPU accounting: every client `SDL_CreateTexture` goes through
`gpuvram::create/destroy` (`gpuvram.h`) — tracked texture bytes plus GL buffer
bytes (`addBuffer`), default cap 1.25 GiB, self-tightening on allocation
failure. Excludes driver-internal allocations, SDL's own vertex/command
buffers, the default framebuffer and GL client-array staging.

Worker pools: `GameView::pool_` (hardware_concurrency threads incl. caller,
used by geometry, shadow transform, body copy and the COB VM pass — never
called concurrently from two threads); `capturePool_` for the sim worker's
snapshot capture; the sim worker thread; the async fog compute worker; terrain
chunk worker; section decode worker; minimap crunch.

Fallback paths: shadow atlas → direct shadow geometry (`drawUnitShadow`) on
missing target support, budget, page limit or draw failure (whole frame);
native GL batch → `SDL_RenderGeometry` on non-`opengl` renderers, fractional
scale, non-normalized textures or <512 vertices; distant images → full
geometry when unsupported, over budget or ineligible; terrain chunks → section
tile batch → minimap underlay; atlas creation failure → retry after 1 s.

Renderer reset: `SDL_RENDER_TARGETS_RESET/DEVICE_RESET` → `invalidateRenderTargets`
(terrain targets, fog, distant cache, geometry keys, colour atlases, shadow
pages + scratch, `glowDirty_`; bumps `rendererGeneration_`). Static textures
(palette pages, model textures, feature art) are not recreated on a target
reset.

## Shared interfaces (fixed in phase 0)

`src/client/rendertelemetry.h` — use these, do not invent parallel ones:

- `FrameStats` (render-thread owned; workers leave per-item results that the
  render thread tallies), `Phase`, `Counter`, `MissCause`, `ScopedPhase`.
  Recording is enabled only by `TAK_RENDER_STATS` or the render study; release
  builds compile it off. Add new counters to the enums and the name tables
  (names are parsed by the benchmark).
- `Generation` / `GenerationCounter` (0 = never valid; starts at 1);
  `GameView::rendererGeneration_` is the render-target lifetime.
- Revisions: per-entry output counters per stage (pose, material, shadow
  coverage, placement). Never derived from the sim tick alone; never hashed.
- `RenderHandle {index, generation}` and `HandleSlots` (bounded, recycles
  indices with a new generation) for stable cache identity (workstream C).

`render_telemetry_test` covers handle aliasing, generation wrap, disabled
stats and the line format.

## Benchmark tooling

`tools/render_benchmark.py` (new; stdlib only):

```sh
# A/A variability or BEFORE run for one client
python3 tools/render_benchmark.py --a ~/tak-tmp/wt/render-opt-p0base/build-o2/takclient \
  --data ~/tak_data --output ~/tak-tmp/render-opt/runs/<name> \
  --scenes all --sizes normal uhd --repeats 2 --capture
# Matched A/B (ABBA groups), candidate vs pinned baseline
python3 tools/render_benchmark.py --a <p0base takclient> --b build-o2/takclient \
  --data ~/tak_data --output <dir> --scenes quick --repeats 2
python3 tools/render_benchmark.py --list    # scene matrix
```

Scenes (`--scenes all`; `quick` = small-battle, stationary-mixed, moving, pan):

| Scene | Map | Content |
|---|---|---|
| small-battle | Ulasem Arena | 200 Archers, two sides, zoom 1 |
| stationary-mixed | Ulasem Arena | 2000-unit 6-type roster, zoom 0.5 |
| moving | Ulasem Arena | 1200 Archers, continuous orders, zoom 0.5 |
| total10k-subset | Ulasem Arena | 10000 units, camera on a small corner (per-size offset) |
| total10k-visible | Ulasem Arena | 10000 units, zoom 0.25 |
| forest | Sea Dragon Spine | densest scenery region + 300 units |
| pan | Ulasem Arena | 2000 units, sinusoidal pan with reversals |
| zoom | Ulasem Arena | pan + continuous zoom 0.25–1 |
| jump | Ulasem Arena | camera jumps between four distant views every 60 frames |
| effects | Ulasem Arena | 600-unit battle + construction sites, ghosts, flyers, corpses, projectiles |
| naval | Athri Cay | 150 floating hulls on the nearest water |
| churn | Ulasem Arena | moving army + pan, 5% die/respawn every 60 frames, target reset every 300 |
| battle-sparse | Ulasem Arena | 2000-unit battle, empty view from frame 450 (segments reported separately) |
| constrained | Ulasem Arena | pan + zoom scene under a 128 MiB tracked-GPU cap (debug `TAK_GPU_CAP_MIB`) |

Harness knobs (debug-only, `gameview_devharness.cpp` / `main.cpp`):
`TAK_RENDER_CENTER`, `TAK_RENDER_OFFSET`, `TAK_RENDER_EXTRAS`,
`TAK_RENDER_NAVAL`, `TAK_RENDER_FOREST`, `TAK_RENDER_JUMP`,
`TAK_RENDER_CHURN`, `TAK_RENDER_RESET`, `TAK_RENDER_SPARSE_AFTER`,
`TAK_RENDER_SPARSE_OFFSET`, `TAK_GPU_CAP_MIB` (`runtimesettings.h`), plus the existing `TAK_SHADOW_BENCH[_TYPE]`,
`TAK_RENDER_SPACING/MOTION/BATTLE/CAMERA/ZOOM`, `TAK_PROFILE_ZOOM`.

`tools/render_reuse_benchmark.py` (the 2026-10-04 study) still runs with
current clients after a one-line fix: it required "AA ... requested 0x" lines,
which clients have not printed since the AA options were removed. Its
reference mode only restores tick-based texture invalidation and is not a
general baseline. The existing ctest `render_texture_reuse` (software
renderer, geometry verifier) passes.

Other existing instruments, checked and NOT used for matched renderer A/B:
the in-game **Benchmark** (`gameview_benchmark.cpp`, `TAK_BENCH`/`TAK_MPAUTO`
in debug) drives an 8-AI Ulasem game through the local server — end-to-end and
user-facing, but the AI's scene content is not frame-matched between builds,
and its "GPU %" is whole-device utilization from the driver, not this
process's GPU time. `TAK_PROF` one-second `PROF` lines and `TAK_SPIKES`
remain for live diagnosis. `crowdbench`/`crowdbench_screen.py` measure the
simulation only.

Offline test: `render_benchmark_tool` (ctest) checks parsing, warmup and
segment windows, the PNG comparer and A/B aggregation without a client.

## Measured results

### Experiment 1 — phase-0 instrumentation vs the starting commit

`--a` = `c2c25707` (`render-opt-base`), `--b` = `e08d7ccf` (`render-opt-p0base`),
ABBA × 2 (8 timing runs per scene), 1280×960, plus one capture run per binary.
Output: `~/tak-tmp/render-opt/runs/exp1-overhead/` (not in Git).

- **World hashes identical** between the two binaries in every scene.
- **Captures identical**: 12 frames per scene (360, 480–489, 720), 0 differing
  pixels in the world view AND the full frame, for small-battle,
  stationary-mixed, moving, total10k-visible, pan and zoom. So the phase-0
  telemetry/harness changes draw exactly the same pixels, and run-to-run
  captures of one scene are reproducible (the 2026-10-04 study's unexplained
  reference-to-reference differences did not appear in these scenes).
- **Timing**: no material difference in any scene (all deltas inside the A/A
  spread). Uncontended scenes: small-battle total mean +0.3% (A/A spread
  4.1%), stationary-mixed +0.8% (7.9%), moving −3.0% (3.6%, one contended B
  run excluded). total10k-visible, pan and zoom ran while a 16-process
  `crowdbench` sweep saturated the pinned CPUs (busy fraction 0.97–1.00), so
  their spreads are 7–34% and only show the absence of a large change.

| Scene (1280×960) | Visible | Geometry hits / misses per frame | Owner misses | Verts generated | Body verts copied | Body submissions | Shadow tiles reused / packed | Dirty shadow px | Atlas regions |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| small-battle | 198 | 171 / 27 | 3 | 8k | 56k | 93 | 93 / 198 | 154k | 8.8 |
| stationary-mixed | 1825 | 1742 / 83 | 0 | 11k | 160k | 922 | 967 / 1825 | 945k | 4.4 |
| moving | 1195 | 360 / 835 | 195 | 263k | 375k | 39 | 9 / 1195 | 414k | 4.4 |
| total10k-visible | 8752 | 8360 / 392 | 0 | 52k | 771k | 3699 | 4383 / 8752 | 1447k | 4.4 |
| pan | 1584 | 0 / 1584 | 126 | 419k | 419k | 939 | 0 / 1584 | 3684k | 4.4 |
| zoom | 1814 | 0 / 1814 | 96 | 480k | 451k | 1067 | 0 / 1814 | 2231k | 4.4 |

What the counters already show (BEFORE, measured, mean over frames 300–899):
any camera motion turns every unit into a geometry miss and re-rasterizes
every shadow tile (D, E, F); a stationary army with 95% geometry hits still
re-rasterizes ~47% of its shadow tiles every frame because tiles are compared
by shelf index (F); a moving army loses ~16% of its hits to depth-order owner
changes alone (C); the whole visible body set is copied every frame (H).

Retained CPU memory at the end of each run (frame 899): geometry pool used /
capacity — small-battle 1.8 / 3.5 MiB, stationary-mixed 20.2 / 29.7,
moving 15.3 / 20.1, total10k-visible 97.1 / 143.1, pan 18.4 / 64.2, zoom
22.5 / 76.2 MiB; body buffer capacity 2.3 / 8.4 / 13.6 / 43.8 / 16.3 /
17.5 MiB. Tracked GPU textures+buffers 76–187 MiB. Peak process RSS 496 MiB
(small-battle) to 1040 MiB (10k units; includes assets and simulation).

### Experiment 2 — BEFORE matrix (A/A, pinned phase-0 baseline)

`tools/render_benchmark.py --a <p0base> --scenes all --sizes normal uhd
--repeats 2 --capture --wait-quiet 1800`: four timing runs per scene and size
(waiting up to 30 min before each for the pinned CPUs to go idle), plus two
capture runs. Output: `~/tak-tmp/render-opt/runs/exp2-before-{normal,uhd,uhd-b}/`
and `exp3-{normal,uhd}/` (the remaining 7680×2160 scenes were re-launched with
`--wait-quiet 300` when Legion sweeps kept the pinned cores busy); merged per
size in `before-final-{normal,uhd}/` (`--reaggregate` rebuilds their reports).
Means over frames 300–899; spread = (max − min) / mean of the four run means.

**Captures: every scene at 1280×960 reproduced exactly** — 0 differing pixels
in the world view over all 12 captured frames (360, 480–489, 720) of both
capture runs, for all 14 scenes, including churn (deaths, respawns, target
resets), battle-sparse, effects (construction, ghosts, flyers, corpses,
projectiles), naval and forest. Pixel-identical output is therefore the
expected result for any change that should not alter appearance; any
difference is a real discrepancy, not run-to-run noise.

1280×960:

| Scene | Visible | Draw mean ms (A/A spread) | Draw p95 ms (spread) | Draw p99 ms (spread) | Frame total mean / p95 ms | GPU timeline ms | Contended runs | Material threshold, draw mean |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| battle-sparse | 465 | 3.254 (5.2%) | 14.240 (4.8%) | 15.337 (5.3%) | 7.00 / 20.13 | 1.34 | 0 of 4 | 10% |
| churn | 1794 | 11.496 (14.8%) | 16.681 (11.3%) | 19.805 (23.0%) | 16.35 / 22.89 | 4.85 | 0 of 4 | 30% |
| constrained | 1814 | 12.840 (3.4%) | 20.835 (15.1%) | 23.415 (11.3%) | 15.56 / 24.83 | 5.28 | 0 of 4 | 7% |
| effects | 332 | 2.378 (2.7%) | 3.596 (4.1%) | 4.228 (7.3%) | 3.34 / 5.37 | 0.70 | 0 of 4 | 5% |
| forest | 300 | 0.925 (1.4%) | 1.112 (0.5%) | 1.141 (0.8%) | 1.26 / 1.50 | 0.23 | 0 of 4 | 3% |
| jump | 220 | 0.886 (2.2%) | 1.220 (4.8%) | 1.599 (3.3%) | 2.40 / 5.21 | 0.15 | 0 of 4 | 4% |
| moving | 1195 | 8.777 (1.7%) | 12.050 (1.0%) | 12.603 (2.3%) | 11.88 / 16.17 | 2.18 | 0 of 4 | 3% |
| naval | 63 | 0.560 (24.5%) | 0.906 (83.0%) | 1.485 (64.7%) | 0.90 / 1.52 | 0.07 | 0 of 4 | 49% |
| pan | 1584 | 8.583 (4.2%) | 13.680 (14.1%) | 14.448 (13.3%) | 10.76 / 16.94 | 3.88 | 0 of 4 | 8% |
| small-battle | 198 | 0.792 (1.4%) | 1.171 (2.3%) | 1.242 (4.2%) | 1.50 / 2.66 | 0.22 | 1 of 4 | 3% |
| stationary-mixed | 1825 | 4.943 (3.7%) | 11.079 (1.9%) | 11.614 (3.3%) | 6.98 / 13.86 | 1.59 | 0 of 4 | 7% |
| total10k-subset | 108 | 2.397 (3.6%) | 2.832 (1.7%) | 4.640 (22.8%) | 10.43 / 20.35 | 0.16 | 0 of 4 | 7% |
| total10k-visible | 8752 | 38.123 (0.6%) | 61.939 (4.2%) | 64.875 (0.5%) | 49.35 / 76.25 | 20.04 | 0 of 4 | 2% |
| zoom | 1814 | 11.460 (3.2%) | 17.580 (3.3%) | 21.511 (5.3%) | 13.96 / 21.01 | 6.61 | 0 of 4 | 6% |

The threshold column is the materiality rule `max(2 × draw-mean A/A spread,
2%)`. Sub-millisecond scenes (naval, small-battle, forest, jump) and churn
have large spreads in percentiles; compare them with more ABBA groups or on
phase counters rather than claiming small changes. Frame `total` includes the
fixed-step local simulation (`update`) and `present` (which waits for the GPU
because of `glFinish`), so `draw` and the per-phase times are the renderer
metrics.

Counters (mean per frame, frames 300–899) and phase times, 1280×960:

| Scene | Geometry hits / misses (owner) | Verts generated | Body verts copied | Body submissions | Shadow tiles reused / packed | Dirty shadow px | Distant images / bakes | t_geometry | t_shadow_pack | t_body_assembly | t_body_submit |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| small-battle | 171 / 27 (3) | 8k | 56k | 93 | 93 / 198 | 154k | 0 / 0 | 0.13 | 0.14 | 0.04 | 0.28 |
| stationary-mixed | 1742 / 83 (0) | 11k | 160k | 922 | 967 / 1825 | 945k | 913 / 0 | 0.40 | 1.81 | 0.21 | 1.46 |
| moving | 360 / 835 (195) | 263k | 375k | 39 | 9 / 1195 | 414k | 0 / 0 | 1.71 | 4.76 | 0.37 | 1.26 |
| total10k-subset | 103 / 5 (0) | 1k | 29k | 72 | 76 / 108 | 126k | 0 / 0 | 0.25 | 0.15 | 0.19 | 0.20 |
| total10k-visible | 8360 / 392 (0) | 52k | 771k | 3699 | 4383 / 8752 | 1447k | 4019 / 46 | 2.09 | **21.55** | 1.34 | 5.78 |
| forest | 287 / 13 (0) | 2k | 79k | 181 | 210 / 300 | 386k | 0 / 0 | 0.10 | 0.12 | 0.05 | 0.47 |
| pan | 0 / 1584 (126) | 419k | 419k | 939 | 0 / 1584 | 3684k | 0 / 0 | 2.52 | 2.16 | 0.43 | 2.82 |
| zoom | 0 / 1814 (96) | 480k | 451k | 1067 | 0 / 1814 | 2231k | 80 / 80 | 2.88 | 3.19 | 0.50 | 3.31 |
| jump | 208 / 12 (3) | 2k | 58k | 140 | 159 / 220 | 144k | 0 / 0 | 0.06 | 0.10 | 0.04 | 0.33 |
| effects | 47 / 284 (77) | 88k | 98k | 199 | 5 / 325 | 959k | 0 / 0 | 0.61 | 0.41 | 0.16 | 0.41 |
| naval | 47 / 16 (0) | 7k | 28k | 223 | 40 / 57 | 144k | 0 / 0 | 0.12 | 0.05 | 0.04 | 0.19 |
| churn | 0 / 1794 (649) | 485k | 485k | 502 | 0 / 1794 | 4052k | 0 / 0 | 2.95 | 4.15 | 0.63 | 2.63 |
| battle-sparse | 280 / 185 (78) | 69k | 122k | 198 | 22 / 465 | 515k | 4 / 1 | 0.51 | 0.92 | 0.15 | 0.86 |
| constrained | 0 / 1814 (96) | 480k | 480k | 1081 | 0 / 1767 | 1619k | 0 / 0 | 2.94 | 3.10 | 0.60 | 3.22 |

Findings that set the order of work (measured, BEFORE):

- **Shadow atlas packing dominates the dense scene**: 21.6 of 38.1 ms draw in
  total10k-visible, with only half of the tiles reused although 95.5% of the
  geometry is reused. The per-frame shelf repack compared by tile index (F)
  is the largest single renderer cost measured.
- **Any camera motion is a full rebuild**: pan, zoom and churn have 0
  geometry hits and 0 reused shadow tiles (D, E, F); a moving army loses 16% of
  its hits to depth-order owner changes (C); churn loses 36%.
- **Memory does not recover**: in battle-sparse the visible count falls from
  1861 to 0 at frame 450 and stays there, but the retained geometry pool stays
  at 72.2 MiB capacity and the body buffer at 18.6 MiB to the end of the run
  (J, C); tracked GPU memory rises 131 → 149 MiB as the new view's terrain
  loads.
- **The Options GPU limit never binds in this matrix**: the smallest Options
  limit (512 MiB; policy floor 384 MiB) is above every scene's tracked GPU use
  (≤ 187 MiB at 1280×960, ≤ 422 MiB at 7680×2160). The first `constrained`
  definition (stationary-mixed at 512 MiB) therefore measured exactly like
  stationary-mixed. It was replaced (commit `7c5d9cb8`) by the zoom scene under
  a debug-only 128 MiB cap (`TAK_GPU_CAP_MIB`), which binds: tracked memory
  holds at 126–128 MiB, distant images and some shadow pages are refused, and
  the frame costs 12.8 ms draw vs 11.5 ms for the same scene uncapped. The rows
  above for `constrained` are from that definition (runs in `exp3-normal`).
  Workstream J needs its own CPU-side constrained configuration.

7680×2160 (world view 7475×2088, confirmed by `RENDER_ENV`). Runs in
`exp2-before-uhd`, `exp2-before-uhd-b` and, for zoom/effects/constrained (re-run
uncontended / with the binding cap), `exp3-uhd`; merged in `before-final-uhd`:

| Scene | Visible | Draw mean ms (A/A spread) | Draw p95 ms (spread) | Draw p99 ms (spread) | Frame total mean / p95 ms | GPU timeline ms | Contended runs | Material threshold, draw mean |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| battle-sparse | 499 | 4.373 (1.9%) | 15.595 (2.2%) | 16.334 (2.8%) | 8.60 / 21.96 | 2.96 | 0 of 4 | 4% |
| churn | 2294 | 17.790 (3.1%) | 22.625 (6.9%) | 26.387 (2.2%) | 23.52 / 29.27 | 11.53 | 0 of 4 | 6% |
| constrained | 2000 | 18.732 (4.6%) | 20.203 (6.9%) | 24.151 (7.3%) | 21.54 / 23.42 | 12.26 | 0 of 4 | 9% |
| effects | 334 | 3.449 (38.6%) | 5.069 (34.0%) | 5.816 (31.3%) | 4.58 / 6.52 | 1.34 | 0 of 4 | 77% |
| forest | 300 | 1.270 (6.9%) | 1.718 (41.7%) | 2.545 (45.1%) | 1.88 / 2.52 | 0.60 | 0 of 4 | 14% |
| jump | 1237 | 5.363 (4.4%) | 14.396 (16.0%) | 16.066 (27.3%) | 7.38 / 17.54 | 3.67 | 0 of 4 | 9% |
| moving | 1200 | 11.762 (8.4%) | 14.845 (23.1%) | 15.699 (22.5%) | 15.71 / 19.16 | 3.42 | 0 of 4 | 17% |
| naval | 116 | 0.889 (6.9%) | 1.186 (15.3%) | 1.431 (5.0%) | 1.58 / 1.98 | 0.44 | 2 of 4 | 14% |
| pan | 2000 | 16.055 (9.5%) | 18.521 (24.0%) | 19.347 (22.0%) | 19.67 / 22.51 | 10.25 | 0 of 4 | 19% |
| small-battle | 198 | 1.129 (14.0%) | 1.632 (29.6%) | 2.421 (71.3%) | 1.94 / 2.60 | 0.63 | 0 of 4 | 28% |
| stationary-mixed | 2000 | 8.111 (3.4%) | 13.857 (7.5%) | 16.439 (17.1%) | 10.54 / 17.60 | 5.51 | 0 of 4 | 7% |
| total10k-subset | 384 | 4.543 (15.3%) | 6.131 (21.4%) | 10.130 (12.5%) | 14.52 / 23.49 | 2.16 | 0 of 4 | 31% |
| total10k-visible | 9398 | 61.529 (6.4%) | 66.783 (8.6%) | 72.336 (13.4%) | 71.59 / 82.79 | 54.64 | 0 of 4 | 13% |
| zoom | 2000 | 16.532 (6.8%) | 18.586 (4.3%) | 19.209 (6.1%) | 20.30 / 22.75 | 11.12 | 0 of 4 | 14% |

**Captures at 7680×2160 also reproduced exactly**: 0 differing pixels, world
view and full frame, all 12 frames, all 14 scenes.

Counters and phases, 7680×2160 (zoom = actual zoom in force where recorded):

| Scene | Zoom | Geometry hits / misses (owner) | Verts generated | Body verts copied | Body submissions | Shadow tiles reused / packed | Distant images | t_geometry | t_shadow_pack | t_shadow_composite | t_body_submit | Geometry capacity MiB | Tracked GPU MiB | Peak RSS MiB |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| small-battle | 1.00 | 171 / 27 (3) | 8k | 56k | 98 | 93 / 198 | 0 | 0.18 | 0.18 | 0.02 | 0.48 | 3.5 | 227 | 526 |
| stationary-mixed | 0.73 | 1910 / 90 (0) | 12k | 529k | 1201 | 1086 / 2000 | 0 | 0.57 | 1.35 | 0.43 | 4.49 | 32.7 | 234 | 627 |
| moving | 0.73 | 360 / 840 (194) | 265k | 377k | 81 | 9 / 1200 | 0 | 1.85 | 7.35 | 0.11 | 1.57 | 20.1 | 202 | 612 |
| total10k-subset | 1.00 | 366 / 18 (0) | 2k | 102k | 230 | 261 / 384 | 0 | 0.42 | 0.34 | 0.05 | 0.82 | 6.3 | 194 | 784 |
| total10k-visible | 0.73 | 8978 / 420 (0) | 56k | 2487k | 5532 | 0 / 4028 | 0 | 2.32 | 1.26 | **34.06** | 14.69 | 153.6 | 307 | 984 |
| forest | 1.00 | 287 / 13 (0) | 2k | 79k | 181 | 180 / 300 | 0 | 0.14 | 0.16 | 0.03 | 0.64 | 4.9 | 170 | 529 |
| pan | 0.75 | 0 / 2000 (13) | 529k | 529k | 1201 | 0 / 2000 | 0 | 3.09 | 2.53 | 1.70 | 7.83 | 55.5 | 275 | 671 |
| zoom | 0.73–1 | 0 / 2000 (0) | 529k | 529k | 1201 | 0 / 2000 | 0 | 3.23 | 3.02 | 0.49 | 8.76 | 73.2 | 291 | 647 |
| jump | 0.75 | 1178 / 59 (4) | 9k | 328k | 744 | 721 / 1237 | 0 | 0.32 | 0.75 | 0.30 | 2.94 | 36.6 | 365 | 623 |
| effects | 0.75 | 48 / 286 (59) | 88k | 99k | 214 | 5 / 327 | 0 | 0.97 | 0.87 | 0.04 | 0.86 | 25.7 | 274 | 586 |
| naval | 0.97 | 88 / 28 (0) | 13k | 51k | 449 | 73 / 105 | 0 | 0.21 | 0.09 | 0.01 | 0.36 | 2.9 | 184 | 574 |
| churn | 0.75 | 0 / 2294 (312) | 627k | 627k | 665 | 0 / 2294 | 0 | 3.85 | 6.73 | 0.39 | 4.25 | 96.1 | 275 | 744 |
| battle-sparse | 0.73 | 298 / 201 (89) | 73k | 131k | 215 | 27 / 499 | 0 | 0.57 | 0.70 | 0.09 | 1.76 | 77.8 | 422 | 678 |
| constrained | 0.73–1 | 0 / 2000 (0) | 529k | 529k | 1201 | 0 / 0 | 0 | 4.06 | 0.26 | 7.74 | 3.44 | 73.2 | 128 | 645 |

Zooms marked 0.73 were requested lower (0.25/0.5); the map-fills-window clamp
(`MapView::minZoom` = world-view width / map width = 7475 / 10240 on Ulasem
Arena) raised them, which the phase-0 client did not report (fixed in
`7c5d9cb8`; RENDERSTATS now carries `zoom=`). Values inferred from the clamp
for the runs made before that commit.

Findings at 7680×2160 (measured, BEFORE):

- **No distant images at all**: every 7680×2160 view of these maps is at zoom
  ≥ 0.73, above the distant cache's 0.6 eligibility, so workstream G has no
  effect at this output size on Ulasem Arena; its benefit must be measured at
  1280×960 or on a larger map.
- **The dense scene falls off the shadow atlas**: total10k-visible packs only
  4028 of 9398 shadows before the 4-page limit, so the whole frame uses the
  direct shadow path: 34.1 ms of 61.5 ms draw is shadow composition, 14.7 ms
  body submission, 2.49 M body vertices copied per frame, GPU draw timeline
  54.6 ms. F (and E/H) must handle "more coverage than four pages" without the
  all-or-nothing fallback.
- **Memory after the battle**: battle-sparse keeps 77.8 MiB of geometry
  capacity and 20.5 MiB of body buffer with nothing visible, and tracked GPU
  memory rises from 246 to 422 MiB while the empty far-corner view loads its
  terrain (chunk budget 256 MiB).
- 7680×2160 spreads are larger (5–15% for most scenes; effects is bimodal,
  2.9 vs 4.3 ms per run with every phase scaling together — consistent with the
  Legion sweeps sharing the 16–23 E-core clusters and laptop power states, not
  with a render-path difference, since captures and counters are identical).
  Use 3+ ABBA groups for small scenes at this size.



## Known limitations and unsupported paths

- Only Linux / NVIDIA / SDL `opengl` (offscreen EGL) is measured. Direct3D,
  Metal, `opengles2`, software and other GPUs are unvalidated; Windows
  (`oden-win`) and macOS (`pocketgsmacbook`) were not used in phase 0.
- The offscreen driver never shows a window: no compositor, vsync or scanout
  cost is included. Visible-window/fullscreen behaviour is unmeasured.
- The render study advances one 30 Hz world tick per rendered frame, so the
  `update` column includes local simulation cost; compare `draw`/`present`
  and the phase times for renderer work.
- Concurrent Legion scenario sweeps and builds share the machine; every run
  records load. Results inside the A/A spread are not claimed as changes.

## Rejected experiments

- (Historical, 2026-10-04) Native-resolution 64/128/256-px building images:
  faster in isolated scenes but failed a crowded full-scene comparison (4,916
  differing pixels, max channel error 255); removed. Two-sample-per-axis
  building images: slower; removed. See `docs/render-reuse-2026-10-04.md`.
  Not reintroduced.
- (Historical) AA settings: removed after 0.7.24; not reintroduced.
- Phase 0 added no experiments.
