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
- **Workstream A (telemetry) is implemented and verified** (commits
  `37f73c48` vocabulary + pool stats + attribution, `50ac99e6` subsystem
  counters, `97a5a1e8` GameView wiring, `f7f923c6` deferred GPU queries +
  timing-mode record, `9569bdbc` benchmark tool, then the docs commit).
  Every category and counter in the brief has a key (see "Telemetry
  reference"); captures are pixel-identical to p0base in 11 scenes; matched
  ABBA runs show no material cost (Experiment 3). Tests: `ctest -L quick`
  376/376 on build-o2; render_telemetry (also built with `NDEBUG`),
  render_benchmark_tool, render_texture_reuse, terrain_cache, shadow,
  geometrysubmit, distant_models pass. A Release `takclient` builds and
  contains no telemetry strings. `--mpai` not run: only `src/client` and
  `tools/` changed, and every benchmark run checks the world hash (identical
  between p0base and the candidate in every scene run).
- **Workstream B (animated-atlas dirty regions) is implemented and verified**
  (commits `bae9afd8` painter/tracker/test, `095e1fba` GameView integration +
  `TAK_ATLAS_VERIFY`, `42c1c4a6` cached region list + early return — fixes a
  measured regression of the first version, `ea2db865` copy-started colour
  atlases + `colour-reset` scene, then the docs commit). Tests: `ctest -L quick`
  377/377 on build-o2; `animated_atlas`, `render_atlas_verify`,
  `render_texture_reuse`, `render_benchmark_tool`, `distant_models`, `shadow`,
  `geometrysubmit` pass; the Release `takclient` builds without the verify
  strings. Measurements: Experiment 4. `--mpai` not run: only `src/client` and
  `tools/` changed, and every benchmark run checks the world hash (identical
  between the pre-B build and B in every scene).
- Workstreams C–N: not started.
- **Next concrete step:** workstream C (stable geometry ownership), measured
  with `tools/render_benchmark.py --a <base> --b <candidate>` against the B
  build (`~/tak-tmp/render-opt/takclient-B3-candidate`, code of `ea2db865`);
  the A build (`takclient-A-candidate`) and the pinned phase-0 baseline
  (`~/tak-tmp/wt/render-opt-p0base`, `7c5d9cb8`) remain available. Report
  pipelined and forced-completion runs separately (Experiment 3), and expect
  contention from the Legion program: use `--wait-quiet` and the
  `--exclude-contended` re-aggregation. Workstream B has no outstanding tests
  or failing cases.

## Status checklist (A–N)

Status words: not started · in progress · implemented · verified ·
experimental · blocked. "Present" notes what current code already does, with
evidence; nothing below is already fully satisfied.

| | Workstream | Status | Present in current code (evidence) |
|---|---|---|---|
| A | Renderer telemetry | **verified** | Phase 0: `FrameStats`, first phases/counters. Workstream A (`37f73c48`…`9569bdbc`): every category the brief lists — candidate/painter, geometry-key evaluation (`w_geometry_key`), geometry prep, shadow alloc/prep/raster/composite, body assembly/submit, atlas, terrain (`t_terrain`, `t_terrain_upload`, worker compose/decode time, job queued/completed/cancelled/discarded/used), worker pool dispatch/wait/busy/wake — and counters for miss causes per stage, owner reassignment, pose/material/placement/zoom/resource/settings invalidation, vertices generated/copied/submitted and bytes uploaded, shadow dirty tiles/area/causes, distant-image hits/held/bakes/refusals, reallocations, logical/native/SDL draws, flushes, target switches, CPU used vs retained bytes, GPU texture creates. Compiled out under `NDEBUG`; off in debug unless asked. GPU draw timeline read from a non-blocking query ring and labelled as timeline (busy time unmeasured); pipelined and forced-completion runs recorded and aggregated separately. Wired into `render_benchmark.py` reports. Cost: no material difference in matched runs (Experiment 3). Limits: draw/flush counts cover the shared world-pass paths only (HUD/effects direct SDL calls uncounted); SDL-internal flushes and driver draws invisible. |
| B | Animated atlas dirty regions | **verified** | Workstream B (`bae9afd8` painter + tracker + test, `095e1fba` GameView, `42c1c4a6` cached region list + early return, `ea2db865` copy-started colour atlases). `AnimatedAtlasTracker` (`client/animatedatlas.h`) records per colour atlas the frame each animated region holds under `rendererGeneration_` and the new `assetGeneration_`; `animateGlowTextures()` returns at once when no sequence changed, otherwise visits only atlases holding a stale frame and repaints only changed regions (content + gutter, or the transparent clear of an expired sequence). New/reset atlases are painted completely and recorded (`glowDirty_`, which forced a repaint of every atlas, is gone); a second colour starts as a 1:1 copy of a complete atlas and repaints only per-colour, animated and untracked regions. `AtlasPainter` removed the flush per region (blend is recorded per queued copy; one flush only protects a source not already NEAREST — none in shipped data). Cadence unchanged. Tests: `animated_atlas` (synthetic, three-way comparison + CPU oracle, mutation-checked), `render_atlas_verify` (in-game `TAK_ATLAS_VERIFY`, two colours, resets). Captures identical. Shipped data: all 44 animated regions advance on the same ticks, so the region count per repaint is unchanged; the measured savings are the flushes, the repaint-frame cost and multi-colour atlas creation (Experiment 4). |
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
| Colour atlases `atlasTex_` | GameView | per player-colour slot; layout fixed at first model load; `atlasTracker_` slot record = (renderer generation, asset generation, painted frame per animated region) | GPU target ≈ 2048×atlasH ×4 (≈11 MiB) per slot, ≤10; essential (bypasses cap) | destroyed on reset (tracker cleared); painted completely on creation |
| Animated texture state | `modelTextureAnimations_`, `animatedTex_`, `animatedRegions_` | frame from game tick, every draw; `animatedRegions_` (rect, frames, frame) rebuilt per update in registry order | CPU | `assetGeneration_` bumped on texture teardown and atlas layout; tracker records from another generation repaint every region |
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

Added by workstream A (same header unless noted):

- `kTelemetryCompiled` (false under `NDEBUG`): every `FrameStats` tally,
  `ScopedPhase` and submit counter folds away in release builds. The release
  `takclient` contains none of the telemetry key strings (checked with
  `strings`). In debug builds recording is off unless `TAK_RENDER_STATS=1` or
  the render study; `TAK_RENDER_STATS=0` switches it off inside the study.
- `Work` (`w_` keys): CPU time summed over worker threads, never compared with
  `t_` wall times. `FrameStats::addMs/addWorkNs/enabled`.
- `SubmitCounters gSubmit` + `countLogicalDraw/countNativeDraw/countSdlGeometry/
  countFlush/countTargetSwitch/countTextureUpload/countUploadBytes`: per-draw-
  call integer adds on the shared submission paths, folded once per frame.
- `client/geometrykeycause.h`: `geometryKeyChanges()` / `primaryMissCause()`
  read the exact geometry key's layout (26 fixed words, optional conjuring
  clock, texture frames, piece table + poses; `GeometryKeyLayout` per entry)
  and name the stages whose inputs changed. Telemetry only — reuse is still
  decided by the whole-key comparison. Workstream D must keep this table in
  step with its new keys, or replace it with per-stage revisions.
- `ThreadPool::Stats` (`client/threadpool.h`): optional, null by default;
  dispatch, caller-wait, participant-busy and worker-wake times. Changes no
  chunking or result (`render_telemetry_test` checks identical outputs).
- `GameView::foldRenderStats()` runs once when `draw()` returns (RAII guard
  after the lobby/loading early returns) and turns cumulative sources
  (terrain workers, section decode, gpuvram creates) into per-frame deltas.

Added by workstream B:

- `assetGeneration_` (`GameView`, a `GenerationCounter`) now exists: bumped
  when model textures load, when GPU textures are torn down and when the atlas
  layout is built. Anything caching a model-texture/atlas-layout derived value
  stores it with this generation.
- `client/animatedatlas.h`: `AtlasPainter` (queued atlas copies with the
  scale-mode protection, `clear`, `copy`, `duplicate`, `finish`),
  `AnimatedRegion`, `paintAnimatedRegion`, `AnimatedAtlasTracker` (per-slot
  painted frames under renderer + asset generations; `created`, `update`,
  `current`, `known`, `forget`, `clear`).
- `TAK_ATLAS_VERIFY` (debug): every atlas is compared after each update with a
  complete repaint; the render study prints `ATLAS_VERIFY checks=N regions=M`.

### Telemetry reference (RENDERSTATS keys)

`t_*` = render-thread wall ms (nested: `terrain_upload` in `terrain`;
`shadow_alloc/prep/raster` in `shadow_pack`; `pool_dispatch/wait` in whichever
phase ran the pass) — never sum them. `w_*` = CPU ms summed over threads.
Other keys are per-frame counts; `*_bytes` keys are gauges (`gpu_tracked_bytes`
is gpuvram's tracked textures + buffers, CPU byte gauges are separate and
exclude allocator overhead, assets and the simulation; whole-process RSS is
sampled by the benchmark from `/proc`).

| Brief category | Keys |
|---|---|
| Candidate selection, painter list/sort | `t_candidates`, `t_painter_sort`, `units_scanned`, `unit_candidates`, `feature_candidates`, `features_painted`, `painter_items` |
| Geometry key / revision evaluation | `w_geometry_key` (every visible unit, hits included) |
| Geometry preparation | `t_geometry` (wall incl. wait), `w_geometry_build`, `verts_generated`, `shadow_verts_generated` |
| Shadow allocation / packing / prep / submission | `t_shadow_alloc` (packing, page creation, dirty stamps), `t_shadow_prep` (tile transform, incl. pool wait), `t_shadow_raster` (clears + coverage submission), `t_shadow_composite`, `shadow_page_allocs`, `shadow_fallback`, `shadow_direct_units` |
| Body assembly / copying | `t_body_assembly`, `body_verts_copied`, `body_bytes_copied` |
| Body submission | `t_body_submit`, `body_submissions` |
| Animated atlas | `t_atlas`, `atlas_repaint_passes`, `atlas_regions`, `atlas_pixels`, `atlas_copies`, `atlas_flushes`, `atlas_created` |
| Terrain decode / composition / uploads | `t_terrain`, `t_terrain_upload` (CPU submission, no GPU sync), `w_terrain_compose`, `w_terrain_upload`, `w_section_decode`, `terrain_jobs_queued/completed/cancelled/discarded/used`, `terrain_section_uploads/decodes`, `texture_upload_bytes` |
| Worker dispatch and wait | `t_pool_dispatch`, `t_pool_wait`, `w_pool_busy`, `w_pool_wake`, `pool_dispatches`, `pool_serial_runs`, `pool_items` |
| Cache hits / misses by cause | `geometry_hits/misses`, `miss_{cold,owner,resource,settings,pose,material,zoom,placement,changed,reference}`; shadow tiles `shadow_tiles_reused/dirty`, `shadow_dirty_{new,owner,content,relocated,resized,moved,forced}`; distant images `distant_hits/held`, `distant_miss_{new,scale,content}`, `distant_reject_{budget,shape,pages}` |
| Owner reassignment | `geometry_owner_reassign` |
| Pose / material / placement / resource invalidation | `inval_pose`, `inval_material`, `inval_placement`, `inval_zoom`, `inval_resource`, `inval_settings` (a miss counts in every stage that changed) |
| Vertices generated / copied / uploaded | `verts_generated`, `body_verts_copied`, `verts_submitted`, `bytes_uploaded` (vertex bytes handed to GL client arrays, retained-buffer uploads and SDL geometry; `retained_draws` upload nothing) |
| Shadow dirty tiles and area | `shadow_tiles_dirty`, `shadow_dirty_pixels` |
| Allocation / reallocation | `geom_reallocs` (per-unit vector growth), `frame_reallocs` (painter list, draw ops, copy tasks, distant items, visible list, body buffer, shadow scratch), `gpu_texture_creates/destroys` |
| Submissions, native draws, flushes, target switches | `logical_draws`, `native_draws`, `sdl_geometry_draws`, `retained_draws`, `flushes`, `target_switches` |
| Used vs retained CPU bytes | `geom_used/capacity_bytes`, `body_used/capacity_bytes`, `shadow_scratch_used/capacity_bytes`, `frame_lists_used/capacity_bytes`, `terrain_mip_cpu_bytes`, `terrain_section_cpu_bytes` |
| GPU | render-study `gpu_timeline_ms` / `GPUTIMELINE` (draw timeline, not busy time), `gpu_tracked_bytes` |

Coverage limits (deliberate): `logical_draws`/`flushes`/`target_switches`
count the world pass's shared paths (GeometrySubmit, OpaqueShadowSubmit,
terrain chunk copies, feature bodies, special units, atlas and shadow-page
target changes, distant bakes); HUD, most effects and the per-piece calls
inside `drawUnit` call SDL directly and are not counted, so these are lower
bounds. SDL's own internal flushes and GL draws are invisible to the client.
`terrain_section_decodes` counts decode attempts (a key already decoded is a
cache hit inside the compositor). GPU busy time is not measured: there is no
portable per-process counter, and `GL_TIME_ELAPSED` is a timeline.

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
| colour-reset | Ulasem Arena | small-battle (two player colours) with a render-target reset every 120 frames, so both colour atlases are recreated (added by workstream B) |
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

Workstream A additions to the tool and the study:

- **GPU timeline without forcing completion.** The study keeps a ring of four
  `GL_TIME_ELAPSED` queries and reads a result only when the driver reports
  it available; results print as `GPUTIMELINE frame=<measured frame> ms=…`,
  possibly frames later (the last ones are drained after the final frame).
  `gpu_query_stalls` in RENDERSTATS counts the times the ring was full and the
  study had to wait (0 expected). The phase-0 client blocked on every frame's
  result after present, which made its "pipelined" runs partly forced; matrix
  runs used forced completion, where both behave alike.
- **Frame mode recorded and separated.** `RENDER_TIMING frame_mode=…
  gpu_timeline=… gpu_busy=unmeasured telemetry=on|off`; the tool refuses a run
  whose mode differs from the request and aggregates/compares pipelined and
  forced-completion runs in separate groups (`mode` column).
- **Telemetry in the report.** `report.json` carries every `TELEMETRY_*` key
  per label (mean of run means, spread); `report.md` adds phase, counter and
  retained-memory (MiB at the last frame) tables and A/B deltas for the
  phases under the same materiality rule.
- **Same-binary A/B.** `--a-env/--b-env KEY=VALUE` apply to one label only,
  e.g. `--a X --b X --a-env TAK_RENDER_STATS=0` measures the telemetry's cost.

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
view and full frame, all 12 frames, all 14 scenes. (The 7680×2160 PNGs, ~10 GB, were deleted
after comparison; the per-frame comparison JSON `captures-<scene>-uhd.json` is
kept. 1280×960 captures are kept under each run's `captures/`.)

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



### Experiment 3 — workstream A telemetry (2026-10-16)

Clients: A-candidate = commit `9569bdbc` code (`~/tak-tmp/render-opt/takclient-A-candidate`,
build-o2), p0base = `7c5d9cb8`. 1280×960 unless noted, offscreen EGL, SDL
`opengl` on the RTX 5070 Laptop GPU (confirmed by `RENDER_ENV` for every run),
900 frames, 300 warmup, client pinned to CPUs 16–23, `--wait-quiet 120`.
Load is recorded per run in each `results.json`; a Legion sweep (one or two
multi-threaded `legion_scenario` processes on CPUs 18/23) shared the pinned
cores for part of the session. Output: `~/tak-tmp/render-opt/runs/a{1..5}-*`,
`a-captures-normal` (not in Git).

**Captures (`a-captures-normal`, `--capture-only`, p0base vs candidate):**
0 differing pixels in the world view AND the full frame over all 12 captured
frames of small-battle, stationary-mixed, moving, pan, zoom, effects, churn,
total10k-visible, battle-sparse, naval and forest. World hashes identical in
every scene of every experiment below.

**A1 — cost of recording, same binary** (`--a C --b C --a-env TAK_RENDER_STATS=0`,
ABBA × 2: A = telemetry off, B = on; draw mean ms, B vs A, max A/A spread):

| Scene | Off | On | Δ | Noise | Material |
|---|---:|---:|---:|---:|---|
| stationary-mixed | 4.930 | 4.933 | +0.1% | 2.5% | no |
| moving | 10.081 | 10.004 | −0.8% | 17.8% | no |
| pan | 8.647 | 8.919 | +3.1% | 8.4% | no |
| total10k-visible | 39.289 (1 uncontended run) | 39.27 (3 uncontended runs: 39.283 / 39.527 / 39.009) | ≈0% | — | no |

total10k-visible: three of the four "off" runs ran with the pinned CPUs
63–97% busy (flagged contended) and are excluded (`report-uncontended.md`);
the remaining matched pairs differ by < 1%.

**A2 — candidate vs phase-0 build, both recording** (ABBA × 2, draw mean):

| Scene | p0base | A | Δ | Noise | Material | t_geometry Δ | t_shadow_pack Δ | t_body_submit Δ |
|---|---:|---:|---:|---:|---|---:|---:|---:|
| stationary-mixed | 5.158 | 5.123 | −0.7% | 14.4%* | no | −0.4% | −0.2% | −0.9% |
| moving | 12.121 | 12.403 | +2.3% | 10.4% (3 of 4 contended per label) | no | +0.4% | +4.0% (noise 9.0%) | +0.4% |
| pan | 8.586 | 8.631 | +0.5% | 4.7% | no | +0.2% | +0.9% | +0.7% |
| total10k-visible | 39.020 | 38.973 | −0.1% | 1.1% | no | +1.5% (3.6%) | −0.4% (1.4%) | −1.6% (2.1%) |

\* stationary-mixed: the first ABBA group ran at 47% pinned-CPU busy, the
second at 19%; within each group the B−A difference is −1.0% and −1.0%.

**A4/A5 — 7680×2160** (world view 7475×2088 confirmed): total10k-visible
ABBA × 1: draw 58.613 → 58.664 ms (+0.1%, noise 0.9%); stationary-mixed
ABBA × 3 (A5, after an ABBA × 1 run flagged +2.5% t_shadow_pack and +4.0%
t_shadow_composite on 2+2 runs with a 0.9% spread): draw 7.941 → 7.989 ms
(+0.6%, noise 2.8%), t_shadow_pack −0.7% (7.8%), t_shadow_composite +2.4%
(3.2%) — not reproduced, inside noise.

Conclusion: the telemetry is inside A/A noise in every matched comparison;
the dense 1280×960 scene, the lowest-noise case, bounds it at about 1% of
draw time. Release builds compile it out (not timed: the render study is
debug-only).

**A3 — pipelined frames** (candidate, A/A × 1, `--pipelined`): moving draw
5.127 ms / total 7.17 ms / GPU timeline 2.36 ms; total10k-visible draw
23.11 / total 31.84 / timeline 12.46 ms; `gpu_query_stalls=0` in every frame,
900 GPUTIMELINE results per run (all frames measured, none forced). The same
scenes under forced completion measure draw ≈ 10.0 and ≈ 39.0 ms. **The CPU
phases themselves run ~1.7–2× slower in forced-completion runs on this
laptop** — not only the present. Unverified hypothesis: with a glFinish per
frame the CPU idles for the GPU every frame and the laptop drops core clocks
or C-states. Matched A/B comparisons remain valid within one mode, but
absolute phase times differ between modes; later workstreams should report
both (the tool keeps them apart).

What the new telemetry already shows (A1 candidate runs, means per frame):

| Key | stationary-mixed | moving | pan | total10k-visible |
|---|---:|---:|---:|---:|
| t_shadow_pack (alloc / prep / raster) | 1.50 (0.29 / 0.18 / 1.02) | 5.72 (0.17 / 0.34 / 5.19) | 2.23 (0.27 / 0.39 / 1.54) | 22.66 (1.88 / 1.15 / 19.55) |
| shadow dirty tiles: owner / content / relocated | 148 / 64 / 646 | 1130 / 14 / 41 | 1428 / 155 / 0 | 1028 / 296 / 3045 |
| geometry misses: owner / pose / material / placement | 0 / 22 / 61 / 0 | 195 / 628 / 0 / 12 | 126 / 17 / 48 / 1393 | 0 / 100 / 292 / 0 |
| inval_placement (any miss whose anchor moved) | 0 | 624 | 1458 | 0 |
| w_geometry_key / w_geometry_build (CPU ms, all threads) | 2.77 / 0.54 | 1.82 / 10.88 | 2.24 / 17.93 | 13.89 / 2.66 |
| t_pool_dispatch / t_pool_wait (wall ms) | 0.19 / 0.15 | 1.62 / 0.17 | 1.32 / 0.17 | 2.69 / 0.20 |
| w_pool_wake (summed over 23 workers) | 11.4 | 40.9 | 54.4 | 72.7 |
| logical / native / SDL-fallback draws, flushes | 1114 / 70 / 856 / 70 | 230 / 42 / 1 / 42 | 1016 / 464 / 481 / 464 | 4435 / 781 / 2968 / 781 |
| verts submitted / vertex bytes handed to GL+SDL | 0.51 M / 6.9 MB | 0.98 M / 12.6 MB | 1.09 M / 15.2 MB | 2.56 M / 34.5 MB |
| distant hits / bakes | 913 / 0 | 0 / 0 | 0 / 0 | 3893 / 46 |
| geometry pool used / retained MiB | 20.2 / 29.7 | 15.3 / 20.1 | 18.4 / 64.2 | 97.1 / 143.1 |
| shadow scratch used / retained MiB | 7.3 / 8.8 | 4.8 / 5.1 | 5.9 / 8.4 | 35.3 / 39.2 |
| decoded terrain sections / mips (CPU MiB) | 63.8 / 5.4 | 63.8 / 5.4 | 63.8 / 0 | 63.8 / 16.9 |

Readings for later workstreams (measured, not yet acted on):

- **F**: shadow packing cost is almost all *raster* (clears + coverage
  submission), and stationary armies re-raster tiles mostly because the
  packer **relocated** them (646 of 858; 3045 of 4369 at 10k), not because
  their content changed. **C**: moving/pan tiles go dirty because the tile now
  holds a different geometry slot (`shadow_dirty_owner`).
- **D**: pan misses are 88% placement-only (1393 of 1584); moving misses are
  pose-led but 624 of 835 also moved anchor.
- **I**: dispatching the pool costs 1.3–2.7 ms of render-thread wall per frame
  in the benchmark, and summed worker wake latency is 40–70 ms: the pool has
  `hardware_concurrency()` = 24 threads but the benchmark pins the client to
  8 CPUs, so 23 workers contend for 8 cores on every wake. Unpinned play does
  not have this oversubscription; workstream I must measure both and must not
  tune thresholds on a pinned-only result.
- **J**: decoded terrain sections hold 63.8 MiB of CPU memory regardless of
  view (Ulasem Arena; all referenced sections decoded at load); geometry pool
  retains 3.5× its use after the pan scene.
- **H**: the dense scene hands 34.5 MB of vertex data to GL/SDL per frame;
  2968 of its 4435 counted draws take an SDL path (GeometrySubmit fallbacks,
  mostly batches below the 512-vertex native threshold, plus distant-image
  bakes and SDL-only paths); the counter does not yet split those reasons.

### Experiment 4 — workstream B animated atlas (2026-10-16)

Clients: **A** = pre-B build (`~/tak-tmp/render-opt/takclient-A-candidate`,
code of `9569bdbc` = workstream A), **B** = final B build
(`~/tak-tmp/render-opt/takclient-B3-candidate`, code of `ea2db865`); the
steady-state update path is identical in the second candidate
(`takclient-B-candidate`, code of `42c1c4a6`), which differs only in atlas
creation. Build-o2 (`-O2` Debug), offscreen EGL, SDL `opengl` on the RTX 5070
Laptop GPU — `RENDER_ENV` confirmed the renderer and output (1280×960, world
1075×888; 7680×2160, world 7475×2088) for every run. 900 frames, 300 warmup,
client pinned to CPUs 16–23, ABBA × 2 (four runs per label), `--wait-quiet 300`.
Legion `ls_gate`/`legion_scenario` jobs and builds shared the machine for much
of the session (load average 3–30); per-run load is in each `result.json`, and
runs whose pinned CPUs were > 50% busy are flagged contended and excluded in
the `*-uncontended` re-aggregations named below. Output (not in Git):
`~/tak-tmp/render-opt/runs/b*`. Scripts: `~/tak-tmp/render-opt/run-b3.sh`
(final) and `run-b.sh` (second candidate). Commands:

```sh
T="python3 tools/render_benchmark.py --a $A --b $B --data ~/tak_data --cpus 16-23"
$T --output runs/b3c-captures-normal --scenes all --capture-only
$T --output runs/b3c-captures-uhd --sizes uhd --scenes small-battle colour-reset churn --capture-only
$T --output runs/b3-normal --scenes small-battle stationary-mixed effects churn colour-reset --repeats 2 --wait-quiet 300
$T --output runs/b3-pipelined --pipelined --scenes small-battle effects colour-reset --repeats 2 --wait-quiet 300
$T --output runs/b3-uhd --sizes uhd --scenes small-battle colour-reset --repeats 2 --wait-quiet 300
python3 tools/render_benchmark.py --a x --data ~/tak_data --output <copy of a run dir> --reaggregate --exclude-contended
python3 tools/render_atlas_frames.py runs/b3-normal     # t_atlas split: repaint frames vs the rest
```

**Correctness.** Captures A vs B: 0 differing pixels in the world view AND the
full frame in all 360 comparisons at 1280×960 (15 scenes incl. colour-reset,
whose frames 360/480/720 are reset frames that recreate both atlases, the
second by the copy path) and all 72 at 7680×2160 (small-battle, colour-reset,
churn); the second candidate was also identical in 14 scenes at 1280×960 and
4 at 7680×2160 (small-battle, effects, churn, total10k-visible). World hashes
identical A vs B in every scene. In-game `TAK_ATLAS_VERIFY` (every atlas vs a
complete repaint after every update): 798 checks on GL (effects scene, two
colours, reset every 100 frames, 400 frames) and 258 on the software renderer
(ctest `render_atlas_verify`), all equal. `animated_atlas`: passes on software
(static SDL2, sdl2-compat) and GL, and under ASan/UBSan; mutations (expired
region not cleared, gutters dropped, tracker never repainting, missing blend
override, copied atlas without the per-colour clear) each fail it. Dropping
the NEAREST protection for the copy fails only the flush-count check: a 1:1
LINEAR copy at texel centres is exact on both renderers.

**Counters (exact, every run).** All 44 animated regions advance together every
10 ticks, so a repaint frame repaints 44 regions per colour atlas in A and B
(88 with two colours): per repaint frame **flushes 44–88 → 0**, copies and
target switches unchanged. Atlas creation: A paints every texture into every
colour (33,012 copies, 3,668 flushes per atlas); B paints the first atlas
completely without flushes and the second as a copy plus 1,396 copies.
colour-reset copies per repaint frame (including creation): 6,294 → 3,659;
flushes per run 41,960 → 0.

**Atlas maintenance time (`t_atlas`, ms per frame; spread = max−min over runs).**

| Scene, mode (runs) | A all frames | B all frames | Δ | Material | A repaint frames | B repaint frames | A other frames | B other frames |
|---|---:|---:|---:|---|---:|---:|---:|---:|
| small-battle 1280, forced (b2-normal, 4+4) | 0.0541 | 0.0454 | −16.1% | yes (noise 1.3%) | 0.516 | 0.429 | 0.0028 | 0.0028 |
| stationary-mixed 1280, forced (b2-normal) | 0.0580 | 0.0512 | −11.7% | yes (4.3%) | 0.344 | 0.276 | 0.0263 | 0.0262 |
| effects 1280, forced (b2-normal) | 0.0617 | 0.0530 | −14.1% | yes (3.2%) | 0.524 | 0.440 | 0.0103 | 0.0101 |
| churn 1280, forced (b2-normal, 1 contended B run excluded) | 0.110 | 0.104 | −9.5% | no (10.1%) | 0.818 | 0.740 | 0.031 | 0.033 |
| colour-reset 1280, forced (b3-normal) | 0.294 | 0.175 | **−40.4%** | yes (10.4%) | 2.908 | 1.721 | 0.0033 | 0.0032 |
| small-battle 1280, pipelined (b3-pipelined) | 0.0540 | 0.0454 | −15.8% | yes (5.3%) | 0.514 | 0.429 | 0.0028 | 0.0028 |
| colour-reset 1280, pipelined | 0.260 | 0.162 | −37.6% | yes (2.6%) | 2.573 | 1.596 | 0.0030 | 0.0030 |
| small-battle 7680×2160, forced (b3-uhd) | 0.0544 | 0.0466 | −14.3% | yes (5.8%) | 0.517 | 0.436 | 0.0030 | 0.0034 |
| colour-reset 7680×2160, forced (1 contended B run excluded) | 0.296 | 0.175 | −40.9% | yes (9.9%) | 2.926 | 1.713 | 0.0039 | 0.0040 |

**Reset frames (colour-reset, both atlases recreated, 5 per run):** `t_atlas`
28.6–29.1 ms (A, four runs) → 14.6–16.8 ms (B, four runs) at 1280×960 forced;
28.7–29.2 → 14.5–16.6 ms at 7680×2160 (one contended B run at 42.7 ms
excluded). A single-colour reset (churn) is unchanged at ~12.5 ms: the first
atlas is still painted completely (see Known limitations).

**Frame times.** Draw mean, B vs A: colour-reset −11.7% forced (material,
noise 5.7%) and −10.5% pipelined (material, 3.9%), −8.8% at 7680×2160 (not
material, threshold 11.8%); colour-reset pipelined draw p99 5.17 → 4.01 ms.
small-battle −1.7% pipelined, +0.5% forced, +2.1% at 7680×2160; stationary-mixed
+1.6% / +0.1%; churn −4.5% / −7.9% — none material (atlas maintenance is
0.05 ms of a 1–12 ms draw outside resets). effects: inside its 13–65% noise in
every session (the 1280×960 b3-normal effects runs were all contended).

**Memory.** No new GPU resources: atlases keep their size and count; the copy
path reads an existing atlas. CPU: the tracker holds one `size_t` per animated
region per colour atlas (44 × ≤10) plus the 44-entry region list — under
10 KiB. `TAK_ATLAS_VERIFY` allocates a scratch atlas per check (debug only).

**Earlier candidate (rejected, see Rejected experiments):** `runs/b1-normal`.

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
- Workstream A telemetry was exercised only on Linux / SDL `opengl`. The CPU
  counters are backend-independent code; the GPU timeline exists only where
  `GL_ARB_timer_query` does (Direct3D/Metal/software runs report
  `gpu_timeline=unavailable`). Not run on Windows or macOS.
- Forced-completion and pipelined runs differ ~2× in CPU phase times on this
  laptop (Experiment 3, A3); compare within one mode only.
- The benchmark pins the client to 8 CPUs while the worker pool sizes itself
  from all 24 logical CPUs; pool dispatch/wake telemetry under the benchmark
  overstates what unpinned play sees.
- Workstream B was exercised on SDL `opengl` (offscreen EGL, RTX 5070) and
  the SDL software renderer (static SDL2 2.32 and Fedora's sdl2-compat, in
  `animated_atlas` and `render_atlas_verify`). Direct3D/Metal/GLES are
  unvalidated; the code relies only on SDL's documented queue behaviour
  (blend recorded per queued copy, scale mode read at execution — checked in
  the SDL 2.32 sources for every backend) and keeps a flush wherever a scale
  mode changes.
- Workstream B late-atlas transient (deliberate, explained): the previous code
  painted a new atlas's animated regions with the source blend mode and
  repainted them with blend NONE at the next update; an atlas created after
  that frame's update (projectiles/debris of a colour with no unit in view)
  showed the source-blend version for one frame. B paints them with blend NONE
  at creation. The two differ only in transparent/partial-alpha texels
  (`animated_atlas` prints that the old creation frame differed for its
  synthetic textures); no captured frame in the matrix contains such a frame,
  and all captures are identical.
- In shipped data every animated model texture (44 regions in the atlas)
  advances on the same game ticks, so per-region dirtiness repaints the same
  number of regions per repaint as before; the per-region record still matters
  for expiry, atlas creation and for data whose sequences differ.
- Creating the FIRST colour atlas still paints every model texture (~33,000
  queued copies, ~12.5 ms at 1280×960 on this machine, per reset or at game
  start). Only further colours are cheaper. See "Rejected / not pursued".
- Telemetry draw/flush/target-switch counts are lower bounds (HUD, effects and
  per-piece calls inside `drawUnit` call SDL directly; SDL-internal flushes and
  driver-level draws are invisible). GPU busy time is not measured.

## Rejected experiments

- (Historical, 2026-10-04) Native-resolution 64/128/256-px building images:
  faster in isolated scenes but failed a crowded full-scene comparison (4,916
  differing pixels, max channel error 255); removed. Two-sample-per-axis
  building images: slower; removed. See `docs/render-reuse-2026-10-04.md`.
  Not reintroduced.
- (Historical) AA settings: removed after 0.7.24; not reintroduced.
- Phase 0 added no experiments.
- Workstream A added no experiments: it changes no draw, cache or scheduling
  decision (captures identical, world hashes identical).
- **B, first version (`095e1fba`) — measured regression, fixed in `42c1c4a6`.**
  It rebuilt the animated-region list (~130 string-keyed map lookups) every
  draw and lost the old early return: ABBA × 2 (`runs/b1-normal`) showed
  `t_atlas` on frames without a repaint rising from 0.003–0.03 ms to
  0.02–0.09 ms, so mean `t_atlas` rose +9% to +69% (material in effects and
  stationary-mixed) although repaint frames got 9–17% cheaper. The list is now
  built once per asset generation and the update returns before touching it
  when nothing changed; Experiment 4 measures the fixed version.
- **B, not pursued: copying changed animated regions from the first atlas to
  the others** (one copy per region instead of clear + 9) — the per-pass cost
  is ~0.4 ms every 10 frames for two colours, the copy would need a
  scale-mode switch of a LINEAR atlas (flush) when Bilinear Filtering is on,
  and per-colour regions would still need their own paint. Left for a later
  measurement with 8 colours.
- **B, not pursued: building the first atlas on the CPU and uploading it once.**
  It would replace ~33,000 queued copies, but the source-blend paint of
  partial-alpha texels is GPU arithmetic that a CPU composite does not
  reproduce bit-exactly, and it would keep (or rebuild) every frame's RGBA in
  CPU memory. Not attempted; recorded as an option for workstream M/J.
