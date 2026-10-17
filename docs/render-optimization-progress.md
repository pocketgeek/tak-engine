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
- **Workstream C (stable geometry ownership) is implemented and verified**
  (commits `81a29b58` entry pool + `HandleSlots` changes + `geometry_owners`
  test, `b180e477` GameView integration + epoch + handle-keyed distant images
  + telemetry + `render_geometry_ownership` ctest, `50acac44` owner-index
  hash, then the docs commit). Tests: `ctest -L quick` 378/378 on build-o2
  (at `b180e477`); `geometry_owners` (also ASan/UBSan and TSan),
  `render_geometry_ownership`, `render_telemetry`, `distant_models` (GPU path
  run too), `render_texture_reuse`, `render_atlas_verify`,
  `render_benchmark_tool` pass on the final code; the Release `takclient`
  builds without the verifier/telemetry strings. `--mpai` Retail 60 s
  Inner Circle seed 1 = `2c39430636a3dbda` (unchanged; run because
  `gameview_net.cpp` and `captureFrame` were touched, both presentation-only).
  Captures identical in all 648 comparisons; measurements: Experiment 5.
  Outstanding: none for C. Known residual: ~1.5% draw on a dense stationary
  army (entry access order, see Experiment 5), to revisit with D's layout.
- **Workstream D (separate preparation from placement) is implemented and
  verified** (commits `361484d2` stage vocabulary + exact stage arithmetic +
  `geometry_stages` test, `08d027d4` GameView local/placement/material
  stages + verifiers + `render_geometry_stages` ctest, `32c5d71a` UnitGeom
  layout, `ed8e6e2a` exact shadow-coverage revision, `dc04c856` verifier
  compiled only in debug, `aae1a9fa` MapView section trimming on the study
  clock (harness repeatability, see Experiment 6), then the docs commit).
  Tests: `ctest -L quick` 380/380 on build-o2 at `aae1a9fa`;
  `geometry_stages`, `render_geometry_stages`, `render_telemetry`,
  `geometry_owners` among them; GL verifier runs (`TAK_GEOMETRY_VERIFY` +
  `TAK_GEOMETRY_OWNERSHIP_VERIFY`) on effects, churn and pan pass. Captures
  identical in all 168 comparisons (9 scenes at 1280×960, 5 at 7680×2160).
  Measurements: Experiment 6. `--mpai` not run: only `src/client` changed
  (presentation), and every benchmark run checks the world hash (identical
  A vs B in every scene).
- **Workstream E (split body and shadow invalidation) is implemented and
  verified** (commits `0af89d63` tile keys + exact translation check +
  `shadowInputsEqual` + `shadow_tiles` test, `1979d413` GameView integration
  + telemetry + verifier extensions + software-atlas debug switch,
  `71a71e9b` harness knobs (`TAK_RENDER_CAMERA_SNAP`, `TAK_RENDER_VETERAN`,
  `TAK_RENDER_ROSTER`), `pan-snap`/`veteran` scenes and the
  `render_shadow_cache` ctest, `15da2bff` decision pre-pass with pooled
  block-wise checks (fixes the measured check cost of the first version),
  then the docs commit). Tests: `shadow_tiles`, `render_shadow_cache`
  (software renderer), GL verifier runs (`TAK_SHADOW_CACHE_VERIFY` +
  `TAK_GEOMETRY_VERIFY`) on effects, stationary-mixed, churn, colour-reset,
  pan-snap (both sizes) and two 80-unit cause scenes; `ctest -L quick`
  381/381 on build-o2 at `71a71e9b` and again on the final code `15da2bff`
  (381/381; `render_shadow_cache`, which is in `tools/ctest_costs.txt` and so
  not `quick`, passes too). Captures identical in all 348 comparisons over the
  three measured candidates. Measurements: Experiment 7. `--mpai` not run:
  only `src/client` (presentation) and `tools/` changed, and every benchmark
  run checks the world hash (identical A vs B in every scene). Baseline
  for later comparisons: `~/tak-tmp/render-opt/takclient-E3-candidate`
  (code of `15da2bff`); DFH (D + harness) in `~/tak-tmp/wt/render-opt-dfh`.
- **Workstream F (persistent shadow allocation) is implemented and verified**
  (commits `3565f3fe` allocator + `shadow_atlas` test, `5febc00f` GameView
  integration + telemetry + `render_shadow_atlas` / `render_shadow_atlas_zoom`
  ctests, `6c7b93df` cheap failure path + page-ordered best fit, `ec3df732`
  free repack when every tile is redrawn, `fbe21589` sub-pixel phase counts as
  redrawn, `3fd2c8de` all-redraw frames use the former packer with exact
  sizes, then the docs commit). Tests: `shadow_atlas` (also ASan/UBSan;
  five header mutations fail it), `shadow_tiles`, `render_shadow_atlas`,
  `render_shadow_atlas_zoom`, `render_shadow_cache` (three in-game mutations
  fail the new ctests), GL verifier runs (`TAK_SHADOW_CACHE_VERIFY`: every kept
  tile vs a full re-bake, allocator validated every frame; + `TAK_GEOMETRY_VERIFY`)
  on effects, churn, colour-reset, zoom, stationary-mixed, pan-snap 7680×2160
  and four small-page stress scenes; `ctest -L quick` 382/382 on build-o2 at
  `3fd2c8de`; the Release `takclient` builds without the new debug/telemetry
  strings. Captures: see Experiment 8 — identical to E3 in the all-redraw
  scenes, elsewhere 1–14 single shadow-edge pixels per differing frame, every
  difference explained (cell position changes the raster rounding). `--mpai` not run: only `src/client` (presentation) and
  `tools/` changed; every benchmark run checks the world hash (identical A vs
  B in every scene). Baseline for later comparisons:
  `~/tak-tmp/render-opt/takclient-F6-candidate` (code of `3fd2c8de`).
- Workstreams G–N: not started.
- **Next concrete step:** workstream G (distant-image lookup before body
  generation). F is done; its follow-ups (not required for G): the dense
  7680×2160 scene still exceeds four pages and takes the whole-frame direct
  path (Experiment 8), and the raster's sub-pixel dependence on the cell
  position (see Known limitations) means any future relocation must redraw.
  Measure G against F6 (`~/tak-tmp/render-opt/takclient-F6-candidate`,
  code of `3fd2c8de`). Captures: compare G only with builds that carry F
  (the shadow raster differs from pre-F builds at single edge pixels).
- (Superseded, kept for the record) Former next step, workstream F: E's
  measurements show what blocks tile reuse now: in pan-snap 868 of 1105
  dirty tiles per frame at 7680×2160 are `shadow_dirty_owner` (the
  height-sorted repack gives the index to another unit) and in stationary
  scenes `relocated` dominates (646 of 858 per frame in veteran 1280×960).
  F should give each entry (handle) a persistent slot and compare the new
  slot's retained stamp with `classifyShadowTile` (keep `ShadowTileStamp`,
  `tilePoint`, `sameTilePoints`; drop only the "same tile index"
  precondition), clear recycled tile interiors + padding, and keep the
  pre-E/pre-F differential check (`originalRuleReuses`) under
  `TAK_SHADOW_CACHE_VERIFY`. Also from E: the dense 7680×2160 scene's
  all-or-nothing fallback beyond four pages (Experiment 2). Measure F
  against E3 (`~/tak-tmp/render-opt/takclient-E3-candidate`); the harness
  knobs and `pan-snap`/`veteran` scenes exist in every build from DFH on.
  Cores 16–23 are reserved for this program (Legion on 4–15); lean testing
  applies (affected scenes only, ABBA × 2 per size, one `ctest -L quick` per
  workstream). Earlier builds remain: DF (`takclient-DF-candidate`,
  `aae1a9fa`), C2F, C2, B3, A, p0base (see Experiment 6). **Compare
  `constrained` captures only between builds that both carry `aae1a9fa`.**

## Status checklist (A–N)

Status words: not started · in progress · implemented · verified ·
experimental · blocked. "Present" notes what current code already does, with
evidence; nothing below is already fully satisfied.

| | Workstream | Status | Present in current code (evidence) |
|---|---|---|---|
| A | Renderer telemetry | **verified** | Phase 0: `FrameStats`, first phases/counters. Workstream A (`37f73c48`…`9569bdbc`): every category the brief lists — candidate/painter, geometry-key evaluation (`w_geometry_key`), geometry prep, shadow alloc/prep/raster/composite, body assembly/submit, atlas, terrain (`t_terrain`, `t_terrain_upload`, worker compose/decode time, job queued/completed/cancelled/discarded/used), worker pool dispatch/wait/busy/wake — and counters for miss causes per stage, owner reassignment, pose/material/placement/zoom/resource/settings invalidation, vertices generated/copied/submitted and bytes uploaded, shadow dirty tiles/area/causes, distant-image hits/held/bakes/refusals, reallocations, logical/native/SDL draws, flushes, target switches, CPU used vs retained bytes, GPU texture creates. Compiled out under `NDEBUG`; off in debug unless asked. GPU draw timeline read from a non-blocking query ring and labelled as timeline (busy time unmeasured); pipelined and forced-completion runs recorded and aggregated separately. Wired into `render_benchmark.py` reports. Cost: no material difference in matched runs (Experiment 3). Limits: draw/flush counts cover the shared world-pass paths only (HUD/effects direct SDL calls uncounted); SDL-internal flushes and driver draws invisible. |
| B | Animated atlas dirty regions | **verified** | Workstream B (`bae9afd8` painter + tracker + test, `095e1fba` GameView, `42c1c4a6` cached region list + early return, `ea2db865` copy-started colour atlases). `AnimatedAtlasTracker` (`client/animatedatlas.h`) records per colour atlas the frame each animated region holds under `rendererGeneration_` and the new `assetGeneration_`; `animateGlowTextures()` returns at once when no sequence changed, otherwise visits only atlases holding a stale frame and repaints only changed regions (content + gutter, or the transparent clear of an expired sequence). New/reset atlases are painted completely and recorded (`glowDirty_`, which forced a repaint of every atlas, is gone); a second colour starts as a 1:1 copy of a complete atlas and repaints only per-colour, animated and untracked regions. `AtlasPainter` removed the flush per region (blend is recorded per queued copy; one flush only protects a source not already NEAREST — none in shipped data). Cadence unchanged. Tests: `animated_atlas` (synthetic, three-way comparison + CPU oracle, mutation-checked), `render_atlas_verify` (in-game `TAK_ATLAS_VERIFY`, two colours, resets). Captures identical. Shipped data: all 44 animated regions advance on the same ticks, so the region count per repaint is unchanged; the measured savings are the flushes, the repaint-frame cost and multi-colour atlas creation (Experiment 4). |
| C | Stable geometry ownership | **verified** | Workstream C (`81a29b58` pool + test, `b180e477` GameView integration, `50acac44` index hash). Each drawn unit owns one entry in `geomEntries_` (`StableEntryPool<UnitGeom>`, `client/geometryowners.h`) keyed by `UnitOwner` = (id, world epoch, type, replacement model); the painter passes use `visGeom_`, ordered pointers to those entries, so depth order no longer decides ownership and a unit entering ahead evicts nobody. Destroyed units are released the frame they leave the snapshot; reused ids (rejoin: `Frame::worldEpoch`), replacement models and type changes get a fresh cold entry with a new generation (`HandleSlots`: one table-wide generation counter); renderer reset keeps owners and drops content. Absent units keep their entry ≤ 2 s / ≥ 30 frames within 32 MiB / 8192 entries; released capacity ≤ 8 MiB / 4 s, then freed with all-free tail chunks; id index sized from live entries. Chunks never move (worker-safe growth). Revisions are globally unique; distant images keyed by the entry handle. Tests: `geometry_owners` (unit: reorder, churn/expiry, budgets, destruction, epoch/type/model reuse, reset, growth, parallel writes, memory recovery, randomized; mutation-checked; ASan/UBSan/TSan clean), `render_geometry_ownership` (in game, two verifiers), `distant_models` (new identity never held). Captures identical (648 comparisons). Measured (Experiment 5): `miss_owner` → 0 in every scene; rebuild work −5 to −8% where units move; draw time unchanged within noise; a consistent ~1.5% (≈0.07 ms) residual on a dense stationary army, not material; battle-sparse retained geometry 72.2 → 0.09 MiB after the view empties. |
| D | Separate preparation from placement | **verified** | Workstream D (`361484d2` vocabulary + test, `08d027d4` GameView stages, `32c5d71a` layout, `ed8e6e2a` shadow-coverage revision, `dc04c856` debug-only verifier, `aae1a9fa` harness clock fix). `buildUnitGeom` keeps three exact stage keys on the unit's stable entry (`client/geometrystages.h`): LOCAL (identity/resources, pose incl. heading/emote spin, pitch/roll, piece poses with hidden/shadow flags, corpse/statue, palette slot, texture frames, construction, shadow toggle) → piece transforms, collection, depth sort, UVs, shadow silhouette in model-projected coordinates; PLACEMENT (zoom, interpolated anchor with lift/sink/altitude, emote bob, flyer altitude, occlusion line) → screen positions; MATERIAL (veterancy, ghost, conjuring progress + clock, tint, opacity) → vertex colours. Only stages whose inputs changed are redone; placement and material use the former single pass's expressions in the same order (bit-identical). Per-stage revisions (`localRevision`, `placementRevision`, `materialRevision`, exact `shadowRevision`); `revision` (the screen output) still advances on any stage, so downstream consumers are unchanged. No hashes; no sim tick as a revision; resets make entries cold. Tests: `geometry_stages` (arithmetic vs the c2c25707 single pass, partial = complete, six header mutations fail it), `render_geometry_stages` (in game, both verifiers), `TAK_GEOMETRY_VERIFY` (every reuse vs a complete rebuild, every local build vs the former single-pass transform, shadow-kept detection vs a copy). Captures identical (168 comparisons). Measured (Experiment 6): in pan/zoom 95% of units are re-placed instead of rebuilt — vertices generated −98%, worker build time −76 to −77%, geometry phase wall −57 to −61%; draw time −17.7% / −16.1% in zoom (material at both sizes), −14 to −20% in pan (every ABBA group, inside pan's spread), −7 to −9% in churn (not material); moving and stationary unchanged within noise. Costs: a local rebuild +7–12% worker time, the key check +28–32% on a pan, geometry memory +18–22% capacity. |
| E | Split body/shadow invalidation | **verified** | Workstream E (`0af89d63` vocabulary + `shadow_tiles` test, `1979d413` GameView, `71a71e9b` harness + `render_shadow_cache` ctest, `15da2bff` decision pre-pass with pooled block-wise point checks). A silhouette tile is stamped with the unit's exact shadow coverage (`shadowRevision`) and the exact `ShadowPlacement` its points were placed with (`client/shadowtiles.h`), no longer with `revision` (any stage) and the screen pixel: material changes (veterancy, tint, ghost, conjuring fade), emote bob, occlusion line and body-only local rebuilds keep the tile; a placement change keeps it only when every tile point recomputes bit-identical (`sameTilePoints`: whole-pixel translations within a binade; sub-pixel phase, zoom and altitude-lean changes redraw). A local rebuild whose shadow inputs are unchanged (colour atlas, frames of animated textures WITHOUT shadow masks) keeps the silhouette without collecting it (`shadowInputsEqual`, `PieceMeta::animationFrameMasks`); animated masks, shadow flags, pose, corpse, construction and the shadow toggle still invalidate immediately. Tests: `shadow_tiles` (kept ⇔ raster inputs bit-identical under the pre-E expressions, randomized over every cause; ten mutations fail it), `render_shadow_cache` (software renderer, every kept tile = complete re-bake, pre-E kept ⊆ E kept, skipped silhouette = fresh collection; three in-game mutations fail it), GL verifier runs. Measured (Experiment 7, final code vs D + harness): pan-snap kept tiles 0–4% → 37–41%, dirty raster area −41/−43%, `t_shadow_pack` −20.0% / −9.6%, GPU timeline −11.5% (1280×960, material), draw −7.4% / −3.0% (consistent, inside threshold); point-check cost `t_shadow_alloc` +0.065 / +0.21 ms there, the stamp loop 5–14% cheaper elsewhere; veteran, pan, stationary-mixed draw unchanged; captures identical (108 comparisons in the final run). Limit: in most matrix scenes tiles go dirty because the per-frame repack moves them (`owner`/`relocated`, F), so E's reuse shows mainly where slots stay put (pan-snap, veteran). |
| F | Persistent shadow allocation | **verified** | Workstream F (`3565f3fe` allocator + `shadow_atlas` test, `5febc00f` GameView integration + `render_shadow_atlas[_zoom]` ctests, `6c7b93df` cheap failure path + page-ordered best fit, `ec3df732` / `fbe21589` / `3fd2c8de` all-redraw frames use the former packer). Each unit's silhouette tile keeps a cell of `ShadowAtlasAllocator` (`client/shadowatlas.h`: logical pages → shelves with free span lists, class-height shelves, exact widths, generational handles, occupancy/fragmentation statistics, `validate()`) across frames; `UnitGeom::shadowTile` holds the handle, validated against the cell's recorded entry handle. E's stamp is kept per cell and carried when a cell is reallocated (verdict `Relocated`, never a stranger's pixels); the stamp gained the page. Policy: keep while the tile fits (cell ≤ 1.5× a fresh one); absent cells retained 2 s / 30 frames, evicted first under pressure; forced repack (the former packer, class heights then exact sizes = former slots, so the atlas falls back only where the former code did) for a failed allocation in a fragmented atlas, ≤ once per 60 frames; free repack when ≥ 90% of tiles are redrawn anyway and the atlas is loose; frames where every tile is redrawn anyway (zoom, sub-pixel pan, new phase) use the former packer with exact sizes (the former slots and pixels); bounded migration (≤ 64 tiles/frame) off a sparse last page; empty pages destroyed after 2 s / 60 frames; page size shrinks after 5 s / 120 frames of low demand; more tile area than four pages falls back at once. 4 pages / 2048² / VRAM budget and the whole-frame direct fallback unchanged. Recycled cells cleared with their padding. Captures: identical to E3 in all-redraw scenes (churn, pan, zoom) and whenever slots match (`TAK_SHADOW_REPACK`, 96 comparisons); with persistent cells 1–14 single shadow-edge pixels per frame differ (208 pixels in 49 of 156 frames) — the former packer shows the same differences when only its slots change (raster rounding depends on the cell position; Experiment 8). Measured (Experiment 8): draw −14% to −51% in every reuse scene (stationary-mixed, total10k-visible, moving, pan-snap; both sizes, material), shadow packing −58% to −91%, p99 −50% to −71% in stationary/dense; all-redraw scenes +0.14–0.20 ms `t_shadow_alloc`, draw −0.3% to +3.0% (not material); dense 7680×2160 fallback −27% shadow phase and 64 MiB released. Memory: tracked GPU equal or lower at the end of every timed scene; one extra page (16 MiB) during the battle-sparse battle, whole atlas freed after the view empties. |
| G | Distant image lookup before generation | not started | `DistantModels::prepare` (`distantmodels.cpp`) runs after `buildUnitGeom` and needs the generated vertices (bounds, source pointer, revision). Entry key: unit id (the geometry entry's generational handle since C), geometry revision, texture tick, source pointer, zoom/scale; 67 ms refresh, 128 bakes/frame, 64-px tiles, 4 × 2048² pages; pages released only when ALL entries expire. Revision changes on every pan (D), so panning re-bakes. |
| H | Less full-scene vertex copying | not started (partly present) | `GeometrySubmit` (`geometrysubmit.h`) submits batches ≥512 vertices on the `opengl` backend through client arrays, skipping SDL's per-vertex conversion copy, with an optional retained VBO keyed by a caller revision (used by terrain tiles and fog, not bodies). Bodies are still copied every frame: per-unit `UnitGeom::verts` → `bodyVerts_` (`:782`–`:850`) → client-array upload. No ordered command representation. |
| I | Cost-based work scheduling | not started | `ThreadPool::parallelFor` (`threadpool.h`): fixed 8-item chunks, `minParallel` default 32; the geometry pass dispatches the whole pool for ≥32 visible units whether they are cache hits or misses; shadow prep goes parallel at ≥32768 points; body copy parallel at ≥32 tasks. |
| J | CPU cache budgets | not started | `terrain::Compositor` decodes every referenced section at load (`MapView::queueAllSections`) and keeps it until `clear()` (`terrain.h:40`, reference valid until clear); `TerrainChunks::mips_` (worker-only) is unbounded; `bodyVerts_` and shadow scratch keep peak capacity forever (per-unit geometry entries are bounded and trimmed since C: retention, byte/count budgets, warm-free trim, tail chunks). Consumers of `sectionImage`: `terrainchunks.cpp:64`, `mapview.cpp:361/381/470`, Cartographer `validation.cpp:32`; `renderBlock` users: minimap (`gameview_hud.cpp:614`), Cartographer. |
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
| Entry claim (since C) | `claimGeometryEntries` (`gameview_render.cpp`): each painter-ordered unit claims the entry its identity owns in `geomEntries_` (`StableEntryPool<UnitGeom>`, `client/geometryowners.h`); `visUnits_` / `visGeom_` (ordered entry pointers) / `geomIndex_`; then `reclaim` (destroyed, expired, budget, trim) | render thread, before the parallel pass |
| Atlas maintenance | `atlasFor` per visible colour slot (`:272`), `preparePaletteTextures` for structures (`:284`), `animateGlowTextures` (`:289`) | render thread, render-target draws |
| Geometry preparation | `pool_.parallelFor` → `buildUnitGeom`: three exact stage keys (since D), then `runUnitGeomStages` redoes only the changed stages — LOCAL (`preparePiece`, `collect`, index sort, local shadow), PLACEMENT (`placeBody`/shadow placement, bounds), MATERIAL (vertex colours) | `ThreadPool pool_`, thread-local scratch, writes only its unit's entry `*visGeom_[i]` |
| Shadow preparation | `buildUnitShadow` inside the LOCAL stage (local points retained); the placement stage re-places them | positions-only opaque verts + masked runs |
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
| Unit geometry (`geomEntries_`, `UnitGeom`; was `geomPool_` before C) | GameView | ownership: `UnitOwner` = (unit id, world epoch, type, replacement model), generational handle per acquisition; content validity (since D): three exact stage keys — local (id, type, visual, atlas ptr, player, slot, pitch/roll, construction, corpse phase, statue, shadows-on, facing, animated texture frames, piece-name table, piece poses), placement (zoom, anchor x/y, emote bob, altitude, occlusion line), material (veteran, ghost, conjuring, emote, tint colour/mix, opacity, anim clock while conjuring); `revision` and the per-stage revisions from one GameView-wide atomic counter | one 512-byte (64-byte aligned) entry per visible or recently visible unit in 256-entry chunks that never move, plus its vectors (screen output and, since D, retained local positions: 8 B per body and per shadow vertex, base colours only for untextured faces); absent units kept ≤ 2 s and ≥ 30 frames, within 32 MiB / 8192 entries (oldest first); released capacity kept ≤ 8 MiB / 4 s then freed; all-free tail chunks deallocated; id index sized from live entries | `invalidateRenderTargets` → `makeAllCold` (owners kept, content dropped); world reset → new epoch; destroyed units released the frame they leave the snapshot |
| Body buffer `bodyVerts_`, `copyTasks_`, `drawOps_` | GameView | rebuilt each frame | peak capacity retained | session |
| Shadow silhouette pages | GameView + `ShadowAtlasAllocator` (since F) | each unit's tile keeps a cell (handle on `UnitGeom::shadowTile`, validated against the cell's entry handle); stamp per cell (entry, owner, coverage = `shadowRevision`, exact `ShadowPlacement`, screen pixel, page, slot, size, scale), carried when the cell moves; a placement change is reused only when every tile point recomputes bit-identical in the same cell | ≤4 GPU target pages up to 2048² (≤64 MiB); class-height shelves, exact widths; absent cells retained 2 s / 30 frames; empty pages destroyed after 2 s; page size shrinks after 5 s of low demand | destroyed on reset/teardown (`resetShadowAtlas`); handles never alias across resets |
| Shadow scratch (`shadowAtlasOpaqueScratch_`, mask scratch, `shadowCompositeBatch_`) | GameView | per frame | capacity retained | cleared on reset |
| Distant images | `DistantModels` | geometry-entry handle (index + generation; unit id before C) → entry (revision, texture tick, source ptr, zoom, scale), 67 ms refresh | ≤4 × 2048² pages (64 MiB), 1024 64-px tiles/page, ≤128 bakes/frame | entries expire after 120 unseen prepares; pages freed only when all expire; cleared on reset |
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
missing target support, budget, page limit or draw failure (whole frame;
since F only after a full repack with the former packer also failed);
native GL batch → `SDL_RenderGeometry` on non-`opengl` renderers, fractional
scale, non-normalized textures or <512 vertices; distant images → full
geometry when unsupported, over budget or ineligible; terrain chunks → section
tile batch → minimap underlay; atlas creation failure → retry after 1 s.

Renderer reset: `SDL_RENDER_TARGETS_RESET/DEVICE_RESET` → `invalidateRenderTargets`
(terrain targets, fog, distant cache, geometry keys, colour atlases, shadow
pages + cells + stamps via `resetShadowAtlas`, scratch, `glowDirty_`; bumps
`rendererGeneration_`). Static textures
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
  decided by the whole-key comparison. (Removed by workstream D: the
  attribution now reads the stage keys, `localKeyChanges` /
  `materialKeyChanges` / `placementKeyChanges` in `client/geometrystages.h`.)
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

Added by workstream C:

- `client/geometryowners.h`: `UnitOwner` (unit id, world epoch, type,
  replacement model), `OwnerIndex` (open-addressing id → entry index sized
  from live entries), `EntryPoolConfig`, `EntryPoolStats` and
  `StableEntryPool<Payload>` (`beginFrame`, `claim(owner, &handle)`,
  `reclaim(alive)`, `makeAllCold`, `clear`, `forEachPayload`, `ownerOf`,
  `handleOf`). Any later per-unit render cache (D's stages, F's shadow
  tiles, G's image metadata) should hang off this entry or key on its
  handle — never on the painter index or the bare unit id.
- `HandleSlots` now draws generations from one table-wide counter (never
  reused, even across `trimTrailing`), reuses free indices lowest first and
  can shrink (`trimTrailing`).
- `GameView::geomEntries_`, `visGeom_` (ordered entry pointers, parallel to
  `visUnits_`), `visGeom(i)`, `UnitGeom::handle` / `handleKey()`,
  `geometryRevision_` (revisions are unique across all entries),
  `worldEpoch_` + `Frame::worldEpoch` (bumped before `resetForReplay`).
- `DistantModels::Item::id` is a 64-bit identity; GameView passes the entry
  handle.
- Debug: `TAK_GEOMETRY_OWNERSHIP_VERIFY` (prints `OWNERSHIP_VERIFY …` at the
  end of a render study and throws at the first violation),
  `TAK_RENDER_EPOCH=N` (world-epoch bump every N frames, the id reuse of a
  rejoin), `TAK_GEOMETRY_RETAIN_MS` / `TAK_GEOMETRY_TRIM_MS` (shorter
  windows for verification runs).

Added by workstream D:

- `client/geometrystages.h`: the local key layout (`kLocalKeyFixedWords`,
  `kLocalKeyWordCause`, `LocalKeyLayout`), `MaterialKey`, `PlacementKey`,
  `planStages(localValid, placementSame, materialSame, materialReadsPlacement)`
  → `StageBit` set, the placement arithmetic (`BodyPlacement`/`placeBody`,
  `ShadowPlacement`, bounds) and material arithmetic (`BodyMaterial`), and the
  per-stage miss attribution. Everything compares exact words/arrays; nothing
  is hashed.
- `UnitGeom` (`gameview.h`): `placementKey`, `materialKey`, `localKey` +
  `localLayout`; retained local output `localPos`, `baseColor`,
  `shadowLocal`, `maskedShadowLocal`; revisions `localRevision`,
  `placementRevision`, `materialRevision`, `shadowRevision` (exact: advanced
  only when the local shadow output differs) next to `revision` (any stage,
  what current consumers compare); `stages` (redone this frame). Layout: the
  fields every visible unit touches every frame share cache line 0; 64-byte
  aligned, 512 bytes.
- `GameView::runUnitGeomStages(u, g, stages, inputs, scratch)` — the stage
  executor; `UnitGeomInputs` carries the placement/material parameters.
- Later workstreams: E keys shadow coverage on (`shadowRevision`,
  placement); F/G key on the entry handle plus the stage revision they
  depend on, never on `revision` when a narrower one suffices.
- Debug: `TAK_GEOMETRY_VERIFY` now checks every complete hit, every
  placement-only and material-only reuse against a complete rebuild, every
  local build against the former single-pass transform
  (`verifyMonolithicBody`) and the shadow-kept detection against a copy;
  the study prints `GEOMETRY_STAGES hits=… placement=… material=…
  single_pass=…`. `TAK_GEOMETRY_NOCACHE` / `TAK_GEOMETRY_REFERENCE` rebuild
  every stage every frame.
- `MapView::setClockMs` (`aae1a9fa`): the render study drives section idle
  trimming from its presentation clock (play keeps wall time).

Added by workstream E:

- `client/shadowtiles.h`: `ShadowTileStamp` (entry, owner, `coverage` =
  `shadowRevision`, `revision` for diagnostics, `ShadowPlacement`, pixel,
  slot, size, scale), `tilePoint` (THE atlas transform, used by the builder
  and the check), `sameTilePoints`, `samePlacement`, `sameBits`,
  `TileVerdict` + `reuses`, `classifyShadowTile`, `originalRuleReuses` (the
  pre-E rule, for differential checks). F should keep these: its stable
  slots replace the "same tile index" precondition, not the content key.
- `geometrystages.h`: `kLocalKeyAtlasWord`, `shadowInputsEqual(before,
  layout, after, layout, frameMasks)`.
- `UnitGeom::shadowPlacement` (set by every placement stage; checked by
  `TAK_GEOMETRY_VERIFY` on every reuse), `UnitGeom::shadowSkipped`
  (telemetry), `UnitGeomInputs::shadowValid`, `PieceMeta::animationFrameMasks`
  (parallel to `animationFrames`: 1 = that animation's texture has shadow
  masks). Entry size unchanged (512 bytes; checked with gdb).
- Debug: `TAK_SHADOW_CACHE_VERIFY` now also throws if a tile the pre-E key
  kept is redrawn and prints `SHADOW_CACHE_VERIFY frames tiles reused body
  translated original_reused`; `GEOMETRY_STAGES` gains `shadow_skipped`
  (skipped silhouettes proven equal to a fresh collection);
  `TAK_SHADOW_ATLAS_SOFTWARE` runs the atlas (and mask coverage textures) on
  SDL's software renderer. Harness: `TAK_RENDER_CAMERA_SNAP`,
  `TAK_RENDER_VETERAN`, `TAK_RENDER_ROSTER` (see Benchmark tooling).

Added by workstream F:

- `client/shadowatlas.h`: `shadowCellClass` / `shadowCellHeight` (shelf
  height classes), `shadowCellKeeps` (keep rule), `ShadowCell`,
  `ShadowAllocation` (cell + caller identity/retention fields),
  `ShadowAtlasStats`, `ShadowAtlasAllocator` (`reset`, `addPage`,
  `releasePage`, `releaseAll`, `allocate(w, h, sequential, pageLimit)`,
  `release`, `get`, `valid`, `forEach`, `trimHandles`, `stats`, `validate`,
  `retainedBytes`). The sequential mode is the former per-frame packer.
- `ShadowTileStamp::page` (`client/shadowtiles.h`); `Relocated` and the pre-E
  rule compare it.
- `UnitGeom::shadowTile` (entry size still 512 bytes, checked with gdb);
  `GameView::shadowAtlas_`, `shadowCellStamps_` / `shadowCellStampValid_`
  (by cell handle index), `shadowAtlasPolicy_`, `resetShadowAtlas()`,
  `ShadowSilhouettePage{texture, emptySince…}` indexed by allocator page slot.
- Telemetry: `shadow_cell_allocs/kept/freed/expired/evicted`,
  `shadow_compactions`, `shadow_compactions_free`, `shadow_migrated`,
  `shadow_page_releases`, `shadow_page_shrinks`; gauges `shadow_cells`,
  `shadow_cells_retained`, `shadow_atlas_capacity_px/cell_px/tile_px/
  shelf_free_px`, `shadow_atlas_gpu_bytes`. CPU bytes of the allocator and
  per-cell stamps are counted in `shadow_scratch_*`.
- Debug: `TAK_SHADOW_REPACK=1` (the former packer every frame — the capture
  reference: slot for slot equal to the pre-F build), `TAK_SHADOW_PAGE_MAX=N`
  (smaller pages), `TAK_SHADOW_ATLAS_DELAY_MS=N` (short policy windows);
  `TAK_SHADOW_CACHE_VERIFY` also runs `ShadowAtlasAllocator::validate()` and
  checks every tile fits its cell each frame, and its summary line appends
  `kept allocs compactions migrated evicted page_releases shrinks validated`.

### Cache ownership and invalidation (workstream C)

Two questions are answered separately:

1. **Whose entry is this?** — ownership. Since C, a per-unit geometry entry
   belongs to a unit identity, `UnitOwner{id, worldEpoch, type,
   replacementModel}`, not to a position in the depth-sorted list.
   `claimGeometryEntries()` walks the painter list once; each unit looks its id
   up in the `OwnerIndex`, and either gets its own entry back (same identity)
   or releases the old one and acquires a fresh, **cold** entry (no key, no
   content) with a new generation. `visGeom_` then holds entry pointers in
   painter order: sorting decides the order of the pointers only.
2. **Is its content still valid?** — the exact geometry key, unchanged by C
   (D will split it into stages). A cache hit still requires the whole key to
   match, and `TAK_GEOMETRY_VERIFY` still checks every hit against a rebuild.

Lifecycle of an entry (render thread):

```
acquire (cold) -> visible --(not claimed)--> retained --(back in view)--> visible
                     |                          |  expired: > 2 s and > 30 frames
                     |                          |  budget: > 32 MiB / 8192 absent entries, oldest first
                     |                          v
                     +--(unit left snapshot / identity changed)--> released (cold, capacity kept "warm")
                                                                     |  > 4 s, or warm > 8 MiB (oldest first)
                                                                     v
                                                                   trimmed (memory freed)
                                                                     |  all-free tail chunk
                                                                     v
                                                                   chunk deallocated, index space shrinks
```

What invalidates what:

| Event | Ownership | Content | Downstream |
|---|---|---|---|
| Depth reorder, unit entering/leaving ahead in the list | unchanged | unchanged (key decides) | stable entry address, revision unchanged on a hit |
| Unit absent ≤ retention | unchanged | kept | — |
| Unit destroyed (gone from the snapshot) | released that frame | cold | distant entry under the old handle expires unused |
| World reset (rejoin: ids restart) | new epoch → new entry per unit | cold | new handle → distant image re-baked, never held |
| Replacement model / type change under the same id | new entry | cold | new handle |
| Render-target / device reset | kept | `makeAllCold` | revisions advance on rebuild |
| GPU teardown / asset reload | kept | cold (through `invalidateRenderTargets`) | — |
| New model type loaded (`assetGeneration_` bump) | kept | key decides (visual/atlas pointers are in it) | deliberately NOT tied to `assetGeneration_`, which bumps on every first-seen type's texture load and would rebuild every unit |

Revisions: one atomic counter per GameView, so a revision value is never
repeated by another occupant of an entry or by a chunk re-allocated at the
same address. Shadow-tile stamps (entry pointer + owner + revision) and
distant images (handle + revision + source) therefore cannot mistake a new
occupant for the old one.

Threads and lifetime: claims and `reclaim` run on the render thread before
the parallel geometry pass; workers only write the entry they were handed,
one worker per entry (duplicate claims are refused); chunks are fixed
256-entry arrays that never move, so growth during the claim loop does not
invalidate pointers already handed out; trimming and chunk release happen
only inside `reclaim`, for entries no visible unit holds.

Memory: storage is bounded by the peak of visible + retained entries (plus
a warm-free tail), not by the number of units ever spawned: the id index is
sized from live entries and shrinks (`compact`), the tail chunks and the
`HandleSlots` index space shrink after demand falls. `geom_capacity_bytes`
counts every payload vector's capacity, the entry objects, the id index and
the metadata.

### Staged content validity (workstream D)

C answered *whose* entry it is. D splits *is its content still valid?* into
three stages, each with an exact key on the entry, and reruns only the
stages whose inputs changed:

```
inputs ──► LOCAL key ──► piece transforms, collect, depth sort, UVs, mask choice,
              │          shadow silhouette  →  localPos, baseColor, shadowLocal,
              │          maskedShadowLocal (model-projected, pre-zoom, pre-anchor)
              ▼
         PLACEMENT key ─► verts[i].position = localPos[i]*zoom + anchor (- bob);
              │           shadow points, shadow bounds, ax/ay/occY/alt
              ▼
         MATERIAL key ──► verts[i].color from baseColor (veterancy, conjuring
                          fade/pulse — reads the placed y, so conjuring units
                          also redo material on placement — tint, ghost, opacity)
```

The asset/topology stage of the brief is the identity and resource words at
the head of the local key (type, visual, atlas pointer, piece-name table)
plus entry ownership (C): a resource change rebuilds local; a reload or
renderer reset makes the entry cold. Draw-only clipping state (the wall
occlusion line) is part of placement.

| Brief input | Stage (key word) | Notes |
|---|---|---|
| Interpolated position | placement (anchor x/y) | `interpPose` / replacement position → anchor |
| Interpolated piece poses | local (piece poses) | captured pose or VM pieces, every move/rot float + visible/cached/shaded/rendered |
| Heading, emote spin/whip | local (facing) | |
| Body pitch / roll | local | |
| Terrain / flyer lift, water sink, altitude lift | placement (anchor) | folded into ax/ay before the key |
| Flyer altitude (shadow lean) | placement (altitude) | |
| Emote bob | placement (bob) | |
| Hidden pieces, shadow-casting flags | local (piece poses) | |
| Player palette / colour slot | local (player, slot, atlas ptr) | UVs depend on the atlas |
| Texture frames | local (each animated frame index) | `TAK_TEXTURE_TICK_REFERENCE` substitutes the game tick |
| Construction | local (shadow suppression) + material (conjuring progress = HP fraction) | |
| Tint (emote, statue), opacity, veterancy, ghost | material | statue state also in local (corpse phase / statue) |
| Corpse / replacement model | local (corpse phase, statue; type/visual) + ownership (replacement model ⇒ new entry) | replacement opacity in material, replacement position in placement |
| Wall occlusion clip line | placement (occlusion) | draw-only clip state |
| Zoom | placement (zoom) | |
| Shadow toggle | local (shadows on) | |
| Resource reload, renderer generation | entry made cold (`invalidateRenderTargets` → `makeAllCold`) | type/visual/atlas pointers also in the local key |

What is never a revision: the sim tick (pose inputs are the poses
themselves; presentation can change between ticks), a hash of the inputs, or
the painter index. Exact validation is retained: `TAK_GEOMETRY_VERIFY`
compares every reuse with a complete rebuild from the same inputs.

What each event redoes:

| Event | Local | Placement | Material | `revision` | `shadowRevision` |
|---|---|---|---|---|---|
| Camera pan / zoom, interpolation step that only moves the anchor | — | yes | only while conjuring | +1 | kept |
| Piece pose / heading / pitch-roll / corpse change | yes | yes | yes | +1 | +1 only if coverage differs |
| Texture frame or palette change (no mask change) | yes | yes | yes | +1 | kept (`geom_shadow_kept`) |
| Veterancy / tint / opacity / ghost change | — | — | yes | +1 | kept |
| Nothing changed | — | — | — | kept | kept |
| Reset / teardown / new owner | cold: all | all | all | +1 | +1 |

Downstream consumers (shadow tile stamps, distant images, body assembly,
`drawUnit`) still compare `revision`, so their behaviour is unchanged by D;
the narrower revisions are for E/F/G. Screen coordinates stay in the
placement key because the cached output is still screen-space vertices (the
brief's rule); D removes them from the LOCAL key only.

### Split body and shadow invalidation (workstream E)

D gave each entry an exact `shadowRevision` but every consumer still keyed
on `revision`. E makes the shadow path depend only on what it reads, at
both levels where shadow work happens:

```
                 body-only inputs                 shadow inputs
LOCAL key   ─┬─ colour atlas (word 3)        ─┬─ everything else in the local key:
             │  frames of animated textures   │  identity, pose, facing, pitch/roll,
             │  WITHOUT shadow masks          │  corpse/statue, construction, shadows-on,
             │                                │  player + colour slot (mask frame choice),
             │                                │  frames of MASKED animated textures
             ▼                                ▼
   body rebuilt                     silhouette collected again only when one of
   (piece transforms, sort, UVs)    these differs (shadowInputsEqual); otherwise
                                    kept, shadowRevision kept
PLACEMENT    zoom, anchor, bob, occlusion  →  shadow uses ShadowPlacement{zoom, sx, sy}
                                              = anchor + altitude lean (not bob/occlusion)
MATERIAL     veterancy, tint, ghost, conjuring  →  never a shadow input
```

Silhouette tile (`buildShadowSilhouetteAtlas`), per tile index:

| Verdict | Condition | Tile |
|---|---|---|
| Owner / New | another entry or owner, or no tile there | redrawn |
| Content | `shadowRevision` differs | redrawn |
| Relocated / Resized | slot, size or output scale differs | redrawn |
| Reuse | same coverage, same placement bits, same pixel, same `revision` | kept |
| ReuseBody | as Reuse, but `revision` moved (a body-only change) | kept (pre-E: redrawn) |
| ReuseTranslated | placement differs, every tile point recomputes bit-identical | kept (pre-E: redrawn) |
| Moved | placement differs and some tile point differs | redrawn |

Why it is exact: a tile's pixels are a function of its rectangle, its
tile-relative points and the cutout runs (textures, UVs, colours). Same
coverage ⇒ identical local points, UVs, colours and runs (`shadowRevision`
moves whenever any of them differs, compared element by element by D).
Same placement bits ⇒ the placement stage produced identical placed points
(same function, same inputs, `-ffp-contract=off`). For a changed placement
the old tile points are recomputed from the unchanged local points with the
old placement through the same two functions (`placeShadowPoint`,
`tilePoint`) and compared with this frame's tile points bit for bit; only
identical inputs are kept, so no translation is assumed exact. In practice
whole-pixel translations at zoom 1 keep every point whose coordinate stays
in its floating-point binade; any sub-pixel phase change, zoom or altitude
change redraws (first point differs, so the rejection costs one point). Not
hashed; no sim tick; nothing reaches the simulation.

| Event | Silhouette collected | `shadowRevision` | Tile |
|---|---|---|---|
| Veterancy / tint / ghost / opacity / conjuring fade | no | kept | kept (ReuseBody) |
| Emote bob, occlusion line only | no | kept | kept (same shadow placement) |
| Colour atlas change, frame of an unmasked animated texture | no (skipped, `geom_shadow_skipped`) | kept | kept |
| Frame of a MASKED animated texture (alpha-mask animation) | yes | +1 if the cutout UV/run differs (always, frames have their own rects) | redrawn |
| Pose / heading / pitch-roll / corpse / statue / construction / shadow toggle | yes | +1 if coverage differs | redrawn if +1 |
| Whole-pixel camera pan (zoom 1), unit stationary | no | kept | kept if every point identical and slot unchanged |
| Sub-pixel pan, zoom, flyer altitude | no | kept | redrawn (Moved/Resized) |
| Entry cold / owner change / reset | yes | +1 (globally unique) | redrawn; reset drops pages |
| Repack moves the tile (F) | — | — | redrawn (Owner/Relocated) |

### Persistent shadow tile cells (workstream F)

E made a tile's *content* key exact (coverage + placement). F makes its
*place* stable: before F every visible shadow was repacked into
height-sorted shelves every frame and compared with whatever tile had the
same index on its page last frame, so one unit entering, leaving or growing
by a pixel shifted every later tile of the page (`shadow_dirty_owner` /
`relocated`). Now each unit's tile lives in a cell it keeps:

```
UnitGeom::shadowTile ──► ShadowAtlasAllocator cell {page, x, y, w, h}   (client/shadowatlas.h)
   (handle, validated       │  owner = entry handleKey, entry ptr, lastFrame/lastMs
    every frame)            ▼
                     shadowCellStamps_[handle.index]  = ShadowTileStamp of the pixels in the cell
                                                       (coverage, placement bits, pixel, PAGE, slot,
                                                        size, output scale)  -> classifyShadowTile (E)
```

Two questions, answered separately:

1. **Where does the tile go?** (destination placement) — the allocator. A
   unit keeps its cell while its tile (width+2 × height+2, the padding
   included) fits and the cell is at most 1.5× the area a fresh cell would
   take; otherwise its old cell is freed and it gets a new one. A unit
   arriving, leaving or resizing moves nobody else.
2. **Are the pixels in the cell still what this frame would rasterize?**
   (coverage validity) — E's stamp, now stored per cell. When a unit gets a
   new cell, the stamp of its previous cell is *carried* to the comparison,
   so the verdict is `Relocated` (redrawn), never a stranger's pixels. The
   stamp gained the page: a carried stamp must not match the same slot on
   another page.

Allocator (`ShadowAtlasAllocator`): logical pages (the caller owns the GPU
textures, indexed by page slot; a released page leaves a hole that the next
page reuses), each cut into horizontal shelves; free vertical space is a
sorted list of row spans, free space in a shelf a sorted list of x spans;
freeing merges spans, an empty shelf returns its rows. Best fit, page by page
from the first: the lowest shelf ≤ 1/8 (≥ 2 px) taller than the request with
a free span wide enough, else a new shelf in that page's first free rows that
fit; only then the next page. Widths are exact; shelf
heights come in classes (`shadowCellHeight`: steps of 2 px up to 32, 4 px
up to 64, then 8 per octave). Handles are generational from one counter that
`reset()` never rewinds. A *sequential* mode is the former packer verbatim
(a cursor per page, row grows to the tallest tile); after `releaseAll()` it
reproduces every former slot (tested over 400 random scenes).

Fractional phase, rounding, scale and filtering, explicitly:

- The tile's pixels are a function of the tile-relative points
  `tilePoint(placed, scale, pixel, slot)` = `placed*scale − pixel + slot + 1`
  (float, `-ffp-contract=off`) and the cutout runs. The slot is part of the
  rounding, so a moved cell is always redrawn (`Relocated`), never copied or
  assumed equal; a translated placement is kept only when every tile point
  recomputes bit-identical in the SAME cell (`sameTilePoints`, E) — integer
  translations that keep each coordinate in its binade; any sub-pixel phase,
  zoom or altitude change redraws.
- Output scale is in the stamp (`Resized`).
- Filtering: the composite samples the page with the unit filter
  (`unitScaleMode`); every tile keeps its cleared 1-px padding inside its
  cell, so a LINEAR sample never reads a neighbour. The coverage textures the
  raster samples are created with SDL's default (nearest) mode and never
  change with the texture filter, so tile content does not depend on the
  filter setting.
- The destination (composite source rect `{slot+1, w, h}`, destination
  `pixel/scale`) is recomputed every frame from the current cell and pixel;
  it does not enter coverage validity.

Policy (presentation clock: the study's fixed clock, wall time in play):

| Event | Rule |
|---|---|
| Tile fits its cell, cell ≤ 1.5× a fresh one | kept (`shadow_cell_kept`) |
| Tile outgrew / shrank under 2/3 of its cell, new unit, entry changed owner | old cell freed, new cell, best fit, new tiles sorted by height (`shadow_cell_allocs`) |
| Unit not in this frame's atlas | cell retained 2 s and 30 frames, then freed (`shadow_cell_expired`) |
| Frame's tile area > four full pages | whole-frame direct fallback at once (no packer can fit it); the unused cells expire and their pages go |
| Allocation fails | absent cells evicted first (`shadow_cell_evicted`); then, if the frame's cells need ≤ 3/4 of the pages (fragmented) and no forced repack in 60 frames: full repack; else a new page (≤ 4, VRAM budget); else full repack |
| Full repack (`shadow_compactions`) | releaseAll, the former packer with class heights; if that does not fit, the former packer with exact sizes = the former slots, so the atlas falls back to direct shadows only where the former code did; after a failed frame the next goes straight to the exact pass |
| EVERY tile of the frame is redrawn anyway (new, other coverage, size, zoom, scale or sub-pixel phase of the placement) | free repack with the former packer and exact sizes (`shadow_compactions_free`): O(1) per tile, the former slots and pixels, one page clear |
| ≥ 90% are redrawn anyway and the atlas is loose (tiles on more pages than a ~80% pack needs, or ≥ 1/4 of shelf area free) | free repack (class heights first) |
| Every cell would fit in half of the other pages for 2 s / 60 frames | bounded migration: ≤ 64 tiles per frame from the last page into lower pages (`shadow_migrated`); its absent cells freed |
| Page empty 2 s / 60 frames | destroyed (`shadow_page_releases`); no page left → page size chosen from scratch |
| Demand fits pages of half the size (with 2× area headroom) for 5 s / 120 frames | pages recreated smaller (`shadow_page_shrinks`; every tile redrawn once) |
| Page size growth | unchanged rule (largest tile, ≤ 3/4 of a page), at once; every page recreated |
| Renderer reset / teardown | `resetShadowAtlas`: pages destroyed, cells and stamps dropped, generations keep counting |
| Target draw failure | every stamp invalidated, whole-frame direct fallback (unchanged) |

Clearing: every redrawn tile clears its rectangle including the padding
(`SDL_RenderFillRects`), so a recycled cell never shows its old occupant;
the whole page is cleared only when every live cell on it is redrawn (a
retained cell's pixels must survive). Cells never overlap
(`ShadowAtlasAllocator::validate`, run every frame under
`TAK_SHADOW_CACHE_VERIFY`).

Preserved: one silhouette per unit (one tile per unit, folds inside a model
still form one coverage), independent darkening (the composite still draws
each unit's tile once, in painter order, MOD blend), the ground/air boundary
(airborne shadows still drained between ground and air bodies), masked and
unmasked cutouts (unchanged raster path), the existing 4-page / 2048² /
VRAM budget and the all-or-nothing direct fallback. Never one shared army
mask.

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
| Persistent shadow cells (F) | `shadow_cell_allocs`, `shadow_cell_kept`, `shadow_cell_freed` (of which `shadow_cell_expired`, `shadow_cell_evicted`), `shadow_compactions` (forced), `shadow_compactions_free`, `shadow_migrated`, `shadow_page_releases`, `shadow_page_shrinks`, `shadow_page_allocs`; gauges `shadow_cells`, `shadow_cells_retained`, `shadow_atlas_capacity_px`, `shadow_atlas_cell_px`, `shadow_atlas_tile_px`, `shadow_atlas_shelf_free_px`, `shadow_atlas_gpu_bytes`. `shadow_pages` = pages holding this frame's tiles (pre-F builds: the same; their ALLOCATED pages never shrank) |
| Body/shadow split (E) | `geom_shadow_skipped` (local rebuilds that kept the silhouette without collecting it), `shadow_reused_body` (tiles kept although the body changed), `shadow_reused_translated` (tiles kept after a placement change, points bit-identical); `shadow_dirty_content` now counts coverage changes only, `shadow_dirty_moved` placement changes whose points differ |
| Staged geometry (D) | `geom_local_builds`, `geom_placement_only`, `geom_material_only`, `verts_placed`, `verts_recoloured`, `geom_shadow_kept`; `verts_generated` counts local rebuilds only since D |
| Geometry entry ownership (C) | `geom_entries` (live), `geom_entry_acquired/replaced/released_destroyed/released_expired/released_budget/trimmed/refused` (per frame), `geom_entries_retained/warm_free/allocated`, `geom_retained_bytes`, `geom_warm_free_bytes` (gauges) |
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
| pan-snap | Ulasem Arena | 2000 units, zoom 1, the pan's path rounded to whole world units: every camera step is a whole-pixel translation (added by workstream E) |
| veteran | Ulasem Arena | stationary-mixed with every fifth unit changing veteran level every 30 frames: material-only body changes (added by workstream E) |

Shadow-atlas ctests (workstream F, software renderer, `TAK_SHADOW_CACHE_VERIFY`):
`render_shadow_atlas` (the render_shadow_cache roster, 288-px pages,
`TAK_SHADOW_ATLAS_DELAY_MS=100`, churn every 30 frames, camera moved at frame
100 so part of the army stays in view: cells kept, repacked, migrated, pages
released) and `render_shadow_atlas_zoom` (continuous zoom + pan: free
repacks, evictions). Debug knobs `TAK_SHADOW_REPACK`, `TAK_SHADOW_PAGE_MAX`,
`TAK_SHADOW_ATLAS_DELAY_MS` (see Shared interfaces).

Harness knobs (debug-only, `gameview_devharness.cpp` / `main.cpp`):
`TAK_RENDER_CENTER`, `TAK_RENDER_OFFSET`, `TAK_RENDER_EXTRAS`,
`TAK_RENDER_NAVAL`, `TAK_RENDER_FOREST`, `TAK_RENDER_JUMP`,
`TAK_RENDER_CHURN`, `TAK_RENDER_RESET`, `TAK_RENDER_SPARSE_AFTER`,
`TAK_RENDER_SPARSE_OFFSET`, `TAK_GPU_CAP_MIB` (`runtimesettings.h`), `TAK_RENDER_CAMERA_SNAP=1|N` (whole-pixel pan; N = N moving / N still frames with every other cycle 0.375 px off the grid), `TAK_RENDER_VETERAN=N`, `TAK_RENDER_ROSTER=a,b,…` (E), plus the existing `TAK_SHADOW_BENCH[_TYPE]`,
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

### Experiment 5 — workstream C stable geometry ownership (2026-10-16)

Clients: **A** = pre-C build (`~/tak-tmp/render-opt/takclient-B3-candidate`,
code of `ea2db865`), **B** = C1 (`~/tak-tmp/render-opt/takclient-C1-candidate`,
code of `b180e477`); the confirmation run uses **C2**
(`takclient-C2-candidate`, code of `50acac44`, which only changes the owner
index hash). Build-o2, offscreen EGL, SDL `opengl` on the RTX 5070 Laptop GPU;
`RENDER_ENV` confirmed renderer and output (1280×960 → world 1075×888;
7680×2160 → world 7475×2088) for every run. 900 frames, 300 warmup, client
pinned to CPUs 16–23, ABBA × 2 (× 3 for the confirmation), `--wait-quiet 180`.
A Legion sweep held CPUs 16–19 or 20–23 for much of the session (one or two
`legion_scenario` processes, load average 3–23, recorded per run); runs whose
pinned CPUs were > 50% busy are flagged and excluded in `c1-normal-uncontended`.
Output (not in Git): `~/tak-tmp/render-opt/runs/c1-*`, `c2-confirm`. Scripts:
`~/tak-tmp/render-opt/c/{captures,timing,confirm}.sh`, summary
`~/tak-tmp/render-opt/c/ctable.py`. Commands:

```sh
T="python3 tools/render_benchmark.py --a $A --b $B --data ~/tak_data --cpus 16-23"
$T --output runs/c1-captures-normal --scenes all --capture-only
$T --output runs/c1-normal --scenes moving churn effects battle-sparse total10k-visible stationary-mixed small-battle pan --repeats 2 --wait-quiet 180
$T --output runs/c1-pipelined --pipelined --scenes moving churn total10k-visible stationary-mixed --repeats 2 --wait-quiet 180
$T --output runs/c1-uhd --sizes uhd --scenes moving total10k-visible churn --repeats 2 --wait-quiet 180
$T --output runs/c1-captures-uhd --sizes uhd --scenes moving churn total10k-visible stationary-mixed effects --capture-only
python3 tools/render_benchmark.py --a x --data ~/tak_data --output <copy of c1-normal> --reaggregate --exclude-contended
```

**Correctness.** Captures A vs B: 0 differing pixels in the world view AND the
full frame in all 360 comparisons at 1280×960 (all 15 scenes × 12 frames) and
all 120 at 7680×2160 (moving, churn, total10k-visible, stationary-mixed,
effects). World hashes identical A vs B in every scene. The distant-image key
change (unit id → entry handle) could in principle bake earlier where the old
build held an image; no captured frame shows a difference. In-game
`TAK_GEOMETRY_OWNERSHIP_VERIFY` + `TAK_GEOMETRY_VERIFY`: 600 frames of the
churn scene with epoch bumps (2000 units, 1280×960, GL: 6319 acquisitions,
3543 identity replacements, 406 destroyed, 314 expired, no violation) and the
ctest `render_geometry_ownership` (software renderer: 638 acquisitions, 414
replacements, 16 expiries, 31 trims). A client whose pool skips the cold step
fails that ctest ("entry carries another unit's geometry"); four mutations of
the pool (no cold step, identity = id only, destroyed never released,
per-index generations) each fail `geometry_owners`. `geometry_owners` is clean
under ASan/UBSan and TSan. `--mpai` Retail 60 s Inner Circle seed 1:
`2c39430636a3dbda` (unchanged). `ctest -L quick` 378/378 on build-o2.

**What changed in the counters (exact, mean per frame, frames 300–899, 1280×960).**

| Scene | Visible | Hits A → B | miss_owner A → B | Verts generated A → B | shadow_dirty_owner A → B | Retained geometry capacity at frame 899, MiB A → B | Absent entries kept (B) | Released destroyed / expired per frame (B) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| small-battle | 198 | 171 → 174 | 3 → 0 | 8.1k → 7.3k | 81 → 80 | 3.5 → 3.56 | 1 | 0.01 / 0.00 |
| stationary-mixed | 1825 | 1742 → 1742 | 0 → 0 | 11.3k → 11.3k | 148 → 148 | 29.7 → 30.32 | 0 | 0.00 / 0.00 |
| moving | 1195 | 360 → 422 | 195 → 0 | 263.1k → 243.7k | 1130 → 1120 | 20.1 → 20.55 | 2 | 0.00 / 0.01 |
| effects | 332 | 47 → 57 | 77 → 0 | 87.6k → 85.4k | 253 → 238 | 25.5 → 6.74 | 17 | 0.34 / 0.01 |
| battle-sparse | 465 | 280 → 300 | 78 → 0 | 68.8k → 64.6k | 342 → 333 | 72.2 → 0.09 | 366 | 0.04 / 3.02 |
| churn | 1794 | 0 → 0 | 649 → 0 | 485.0k → 485.0k | 1692 → 1670 | 76.4 → 43.25 | 191 | 0.43 / 1.22 |
| pan | 1584 | 0 → 0 | 126 → 0 | 419.1k → 419.1k | 1428 → 1424 | 64.2 → 33.25 | 165 | 0.00 / 1.18 |
| total10k-visible | 8752 | 8360 → 8360 | 0 → 0 | 52.1k → 52.1k | 1028 → 1028 | 143.1 → 146.64 | 0 | 0.00 / 0.00 |

Readings:

- Depth-order owner misses are gone in every scene. They become hits only
  where nothing else in the key changed: moving +62 hits (−7.4% vertices
  generated), battle-sparse +20, effects +10. In churn and pan every unit
  still misses on placement or pose (the camera pans), which D addresses.
- `shadow_dirty_owner` barely moves: tiles are still assigned by the
  per-frame repack, so a moving army's tile index changes occupant every frame
  (F). Ownership was the prerequisite: a tile can now be tied to a stable
  entry.
- **Memory follows demand.** In battle-sparse the view empties at frame 450;
  absent entries expire at frame 570 (2 s on the study clock), all but 8 MiB
  of released capacity is freed at once, and the rest at frame 810 together
  with every chunk: retained geometry capacity at the last frame 72.2 → 0.09
  MiB, final process RSS 613–629 → 401–503 MiB (4 runs each). While the
  battle is in view the entries also hold less (32 MiB vs 72 MiB): each unit's
  entry fits that unit, where a painter slot kept the peak of every unit that
  ever passed through it. Same in pan (64.2 → 33.3), churn (76.4 → 43.3;
  7680×2160: 96.1 → 45.4) and effects (25.5 → 6.7). B's figure also counts
  the entry objects, id index and metadata, which A's did not (about
  +3.5 MiB at 8752 entries, the whole difference in total10k-visible).
- The body buffer (18.6 MiB in battle-sparse) is untouched by C (J).

**Timing (draw mean, B vs A).** No scene changes materially (threshold
`max(2 × A/A spread, 2%)`):

| Scene, mode | Runs A/B | Draw mean ms A → B | Δ | Noise | Material | t_geometry A → B | w_geometry_build A → B | GPU timeline A → B |
|---|---:|---:|---:|---:|---|---:|---:|---:|
| small-battle 1280 forced | 4/4 | 0.801 → 0.799 | −0.3% | 1.8% | no | 0.130 → 0.125 | 0.50 → 0.46 (−7.7%, material) | 0.22 → 0.22 |
| stationary-mixed 1280 forced | 4/4 | 5.092 → 5.171 | +1.6% | 3.5% | no | 0.418 → 0.437 | 0.53 → 0.54 | 1.73 → 1.71 |
| moving 1280 forced | 4/4 | 10.471 → 10.675 | +1.9% | 16.8% | no | 1.948 → 1.889 | 11.20 → 10.55 (−5.7%) | 2.78 → 2.80 |
| effects 1280 forced | 4/4 | 2.932 → 3.117 | +6.3% | 6.5% | no (see below) | 0.759 → 0.815 | 4.32 → 4.33 | 0.85 → 0.85 |
| battle-sparse 1280 forced | 4/4 | 3.313 → 3.323 | +0.3% | 8.8% | no | 0.540 → 0.528 | 3.08 → 2.92 (−5.1%) | 1.40 → 1.40 |
| churn 1280 forced | 4/4 | 12.390 → 12.876 | +3.9% | 19.3% | no | 3.223 → 3.332 | 21.68 → 22.41 | 5.20 → 5.39 |
| pan 1280 forced (1 contended B run excluded) | 4/3 | 10.264 → 9.493 | −7.5% | 18.8% | no | 2.922 → 2.714 | 18.56 → 18.12 | 4.45 → 4.21 |
| total10k-visible 1280 forced (1 contended A run excluded) | 3/4 | 42.864 → 40.254 | −6.1% | 6.4% | no | 2.683 → 2.312 | 2.70 → 2.61 | 22.10 → 20.83 |
| moving 1280 pipelined | 4/4 | 5.690 → 5.655 | −0.6% | 1.7% | no | 1.829 → 1.755 | 10.89 → 10.18 (−6.5%) | 2.61 → 2.64 |
| churn 1280 pipelined | 4/4 | 10.612 → 10.773 | +1.5% | 6.8% | no | 3.259 → 3.313 | 21.97 → 22.26 | 5.27 → 5.29 |
| stationary-mixed 1280 pipelined | 4/4 | 5.214 → 5.332 | +2.3% | 1.9% | no | 0.551 → 0.581 | 0.54 → 0.54 | 2.19 → 2.24 |
| total10k-visible 1280 pipelined | 4/4 | 25.426 → 25.520 | +0.4% | 3.1% | no | 2.405 → 2.425 | 2.60 → 2.63 | 13.42 → 13.31 |
| moving 7680×2160 forced | 4/4 | 12.317 → 12.262 | −0.5% | 20.2% | no | 1.948 → 1.869 | 11.31 → 10.59 (−6.3%, material) | 3.61 → 3.66 |
| churn 7680×2160 forced | 4/4 | 19.050 → 18.231 | −4.3% | 18.1% | no | 4.298 → 3.948 | 32.04 → 30.20 | 12.15 → 11.90 |
| total10k-visible 7680×2160 forced | 4/4 | 58.774 → 59.039 | +0.5% | 2.5% | no | 2.339 → 2.334 | 2.73 → 2.74 | 52.44 → 52.50 |

The only material changes are the worker rebuild time where owner misses
became hits (small-battle −7.7%, moving at 7680×2160 −6.3%; −5 to −7% in the
other moving/battle cases, below threshold). The geometry pass's wall time
is dominated by dispatch and the remaining misses, so draw time does not move
measurably.

**effects at 1280×960** was slower in every B run (+0.14 to +0.23 ms per
ABBA pair, +6.3% mean, below the 13% threshold). Investigated: allocation is
lower in B (`geom_reallocs` 0.96 → 0.13 per frame); the claim + reclaim
work for 332 units is about 2 µs (CPU microbenchmark: 3.6 ns per claim at
10,000 units in shuffled painter order, 0.007 ms reclaim scan); the entry
set (~100 KB) stays in L2, so scattered entries cost at most ~0.02 ms; and
phases C does not touch were also slower in the B runs (`t_effects` +3%,
`t_body_submit` +2%, `t_candidates` +3.7%, worker wake latency), which points
to binary layout or machine state rather than the pool. The confirmation run
(C2 vs A, ABBA × 3, `runs/c2-confirm`) does not reproduce it: per ABBA group
effects reads +1.1%, +16.6% (that group straddles the machine's bimodal
state, frames of 2.35 vs 3.3 ms in both labels) and −1.8%; moving +1.9%,
−1.2%, −6.9%; total10k-visible 0.0%, −4.3% and one contended group.

**Residual cost on a dense stationary army (measured, not material).**
stationary-mixed, where C has nothing to gain (0 owner misses), reads
slightly slower in every clean ABBA group of every session: C1 forced +1.6%,
C1 pipelined +2.6% / +2.0%, C2 forced +1.0% / +1.4%, final-code pipelined
(C3 run below) +0.4% / +3.0% / +1.6% — about +0.07 ms at 1825 visible
units, below the materiality threshold (3.8–7%). The extra time sits only in
the passes that read entries (geometry +10–41 µs, body assembly +13–33,
shadow pack +5–31, distant +5–11 per frame); untouched passes are flat. The
likely cause is access order: the old pool was one array walked in painter
order (hardware-prefetch friendly); stable entries sit in acquisition order,
so each pass reaches them through a pointer in a non-sequential order. A
software-prefetch hint did not remove it (rejected, see below). Scenes with
motion recover more than this (moving −1.5% / −2.1% in clean pipelined
groups, rebuild work −0.57 to −0.67 ms per frame). Workstream D, which
splits `UnitGeom` into stages, is the place to revisit the entry layout
(hot fields per pass, 64-byte alignment).

Final-code captures (`runs/c2-captures-normal`, C2 vs A): 0 differing pixels
in all 168 comparisons (churn, moving, effects, stationary-mixed,
total10k-visible, battle-sparse, colour-reset).

**Overhead bound.** CPU microbenchmark of the pool alone (`-O2`, one E-core):
claim 2.0 / 2.8 / 3.6 ns per unit at 200 / 2000 / 10,000 units in shuffled
painter order; reclaim scan 0.001 / 0.007 ms per frame at 2000 / 10,000
entries.

### Experiment 6 — workstream D staged geometry (2026-10-16)

Clients: **A** = C2F (`~/tak-tmp/render-opt/takclient-C2F-candidate`: code
of `50acac44` = workstream C, plus the MapView clock fix `aae1a9fa`
cherry-picked, built in `~/tak-tmp/wt/render-opt-c2fix`), **B** = DF
(`takclient-DF-candidate`, code of `aae1a9fa`). A and B differ only by
workstream D. Build-o2 (`-O2` Debug), offscreen EGL, SDL `opengl` on the
RTX 5070 Laptop GPU; `RENDER_ENV` confirmed renderer and output (1280×960 →
world 1075×888; 7680×2160 → world 7475×2088) for every run. 900 frames, 300
warmup, forced-completion frames, client pinned to CPUs 16–23 (reserved for
this program; the Legion program ran on 4–15, load average 2–21 recorded
per run, pinned-CPU busy fraction 0.19–0.35, no run contended), ABBA × 2 per
scene and size, `--wait-quiet 60`. Lean testing: only the scenes D affects
were timed — pan, zoom, churn (camera motion: placement-only reuse), moving
(pose-led: the worst case for D's local-build overhead) and
stationary-mixed (regression guard for the larger entry and C's residual).
Pipelined frames were not measured for D. Output (not in Git):
`~/tak-tmp/render-opt/runs/df-*`; script `~/tak-tmp/render-opt/d/final.sh`,
summary `~/tak-tmp/render-opt/d/dtable.py`. Commands:

```sh
T="python3 tools/render_benchmark.py --a $C2F --b $DF --data ~/tak_data --cpus 16-23"
$T --output runs/df-captures-normal --scenes pan zoom churn moving effects constrained stationary-mixed total10k-visible small-battle --capture-only
$T --output runs/df-captures-uhd --sizes uhd --scenes pan zoom churn moving effects --capture-only
$T --output runs/df-normal --scenes pan zoom churn moving stationary-mixed --repeats 2 --wait-quiet 60
$T --output runs/df-uhd --sizes uhd --scenes pan zoom churn moving stationary-mixed --repeats 2 --wait-quiet 60
VCPUS=16-23 python3 ~/tak-tmp/render-opt/d/vrun.py $DF <scene> normal 400 TAK_GEOMETRY_VERIFY=1 TAK_GEOMETRY_OWNERSHIP_VERIFY=1
python3 ~/tak-tmp/render-opt/d/dtable.py runs/df-normal/results.json runs/df-uhd/results.json
```

**Correctness.** Captures A vs B: 0 differing pixels in the world view AND
the full frame in all 108 comparisons at 1280×960 (pan, zoom, churn, moving,
effects, constrained, stationary-mixed, total10k-visible, small-battle × 12
frames) and all 60 at 7680×2160 (pan, zoom, churn, moving, effects). World
hashes identical A vs B in every scene. (An earlier, interrupted D run had
already found all 15 scenes identical at 1280×960 against C2 except
`constrained`, whose discrepancy is explained under Known limitations and
gone once both builds carry `aae1a9fa`.) GL verifier runs on the final code
(400 frames each, `TAK_GEOMETRY_VERIFY` + `TAK_GEOMETRY_OWNERSHIP_VERIFY`,
which throw at the first mismatch): effects 33,436 complete hits, 83,117
placement-only and 1,969 material-only reuses, 201,292 local builds checked
against the single-pass transform; churn 448,847 placement-only / 661,120
single-pass; pan 607,386 / 638,417 — no mismatch, no ownership violation;
effects had 2 local rebuilds whose coverage was detected unchanged
(`geom_shadow_kept`). `ctest -L quick` 380/380 (incl. `geometry_stages`,
`render_geometry_stages` on the software renderer).

**What changed in the counters (exact, mean per frame, frames 300–899).**

| Scene, size | Visible | Local builds (B) | Placement-only (B) | Verts generated A → B | Shadow verts generated A → B | Verts placed (B) | Shadow tiles dirty A → B | Geometry used / capacity at last frame, MiB A → B |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| pan 1280 | 1584 | 72 | 1512 | 419.1k → 9.9k | 658.2k → 14.1k | 1.05 M | 1584 → 1584 | 19.0 / 33.3 → 28.0 / 39.8 |
| zoom 1280 | 1814 | 83 | 1731 | 480.0k → 11.5k | 754.0k → 16.3k | 1.21 M | 1814 → 1814 | 20.8 / 37.5 → 30.6 / 44.9 |
| churn 1280 | 1794 | 564 | 1229 | 485.0k → 216.9k | 768.3k → 327.5k | 0.71 M | 1794 → 1794 | 23.7 / 43.3 → 34.7 / 52.5 |
| moving 1280 | 1195 | 757 | 16 | 243.7k → 238.6k | 389.7k → 381.7k | 13k | 1185 → 1185 | 15.2 / 20.6 → 22.6 / 24.5 |
| stationary-mixed 1280 | 1825 | 83 | 0 | 11.3k → 11.3k | 16.0k → 16.0k | 0 | 858 → 858 | 20.2 / 30.3 → 29.7 / 36.0 |
| pan 7680×2160 | 2000 | 90 | 1910 | 529.2k → 12.3k | 831.2k → 17.4k | 1.33 M | 2000 → 2000 | 22.2 / 33.4 → 32.7 / 39.6 |
| zoom 7680×2160 | 2000 | 90 | 1910 | 529.3k → 12.3k | 831.3k → 17.5k | 1.33 M | 2000 → 2000 | 22.2 / 33.4 → 32.7 / 39.6 |
| churn 7680×2160 | 2294 | 752 | 1542 | 627.1k → 292.1k | 991.2k → 440.4k | 0.89 M | 2294 → 2294 | 29.0 / 45.4 → 42.5 / 55.3 |
| moving 7680×2160 | 1200 | 762 | 17 | 245.2k → 240.0k | 392.2k → 383.9k | 14k | 1190 → 1190 | 15.3 / 20.6 → 22.6 / 24.5 |
| stationary-mixed 7680×2160 | 2000 | 90 | 0 | 12.3k → 12.3k | 17.5k → 17.5k | 0 | 914 → 914 | 22.2 / 33.4 → 32.7 / 39.6 |

Geometry hits/misses and every miss cause are identical A vs B (the
attribution is unchanged; a "miss" now often costs only a re-placement).
Shadow tiles, dirty pixels and distant bakes are identical: D leaves every
downstream consumer on `revision` (E/F/G).

**Timing (forced completion, draw mean unless noted; spread = max−min over a
label's four run means; material = beyond `max(2 × spread, 2%)`; groups =
B−A per ABBA group).**

| Scene, size | Draw mean ms A → B | Δ | Spread A / B | Material | Groups | Draw p99 A → B | t_geometry A → B | w_geometry_build A → B | w_geometry_key A → B | t_pool_dispatch A → B | GPU timeline A → B |
|---|---:|---:|---:|---|---|---:|---:|---:|---:|---:|---:|
| pan 1280 | 10.015 → 8.014 | −20.0% | 17.1 / 16.3% | no* | −19.3%, −20.6% | 16.06 → 13.94 | 2.831 → 1.113 (−60.7%) | 18.32 → 4.23 (−76.9%, material) | 2.10 → 2.77 (+32%, material) | 1.52 → 0.76 | 4.40 → 4.12 |
| zoom 1280 | 12.136 → 9.987 | **−17.7%** | 4.0 / 4.3% | **yes** | −17.6%, −17.8% | 20.35 → 16.99 | 3.260 → 1.264 (−61.2%, material) | 23.94 → 5.77 (−75.9%, material) | 3.36 → 3.77 | 1.96 → 1.04 (material) | 7.09 → 6.98 |
| churn 1280 | 12.365 → 11.203 | −9.4% | 6.7 / 4.6% | no | −10.6%, −8.2% | 19.93 → 18.94 | 3.284 → 2.249 (−31.5%, material) | 24.09 → 13.88 (−42.4%, material) | 3.65 → 4.01 | 2.23 → 1.77 (material) | 5.12 → 5.01 |
| moving 1280 | 9.701 → 9.683 | −0.2% | 12.3 / 9.3% | no | −6.6%, +6.7% | 15.25 → 15.20 | 1.779 → 1.908 (+7.2%) | 11.11 → 12.16 (+9.5%) | 2.32 → 2.47 | 1.63 → 1.74 | 2.59 → 2.53 |
| stationary-mixed 1280 | 5.205 → 5.179 | −0.5% | 3.8 / 1.6% | no | −0.9%, −0.1% | 12.60 → 12.45 | 0.471 → 0.465 | 0.537 → 0.581 (+8.1%, material) | 2.83 → 2.71 | 0.22 → 0.23 | 1.91 → 1.94 |
| pan 7680×2160 | 16.891 → 14.539 | −13.9% | 9.6 / 12.5% | no | −13.7%, −14.1% | 20.19 → 18.02 | 3.427 → 1.465 (−57.2%) | 24.24 → 5.89 (−75.7%, material) | 2.82 → 3.65 (+30%, material) | 1.69 → 0.98 | 10.68 → 10.11 |
| zoom 7680×2160 | 15.473 → 12.981 | **−16.1%** | 4.4 / 7.3% | **yes** | −16.9%, −15.3% | 20.77 → 18.48 | 3.719 → 1.567 (−57.9%, material) | 25.97 → 5.98 (−77.0%, material) | 2.80 → 3.59 (+28%, material) | 1.69 → 0.90 (material) | 8.96 → 8.61 |
| churn 7680×2160 | 18.135 → 16.954 | −6.5% | 11.4 / 5.4% | no | −4.7%, −8.3% | 27.38 → 26.21 | 3.918 → 2.787 (−28.9%, material) | 30.01 → 17.09 (−43.1%, material) | 3.65 → 3.89 | 1.98 → 1.75 | 11.92 → 11.66 |
| moving 7680×2160 | 11.389 → 11.485 | +0.8% | 14.4 / 7.2% | no | −1.0%, +2.8% | 14.19 → 14.08 | 1.783 → 1.851 (+3.8%) | 10.38 → 11.07 (+6.7%) | 1.96 → 1.90 | 1.53 → 1.57 | 3.58 → 3.50 |
| stationary-mixed 7680×2160 | 8.135 → 8.084 | −0.6% | 3.5 / 5.1% | no | −3.3%, +2.1% | 14.90 → 14.79 | 0.685 → 0.642 | 0.595 → 0.667 (+12.0%, material) | 3.10 → 2.90 | 0.51 → 0.47 | 5.29 → 5.30 |

\* pan at 1280×960: the second ABBA group ran after a Legion sweep started
on CPUs 4–15 (load average 2 → 14), and every phase, including passes D does
not touch (`t_shadow_pack` 2.4 → 3.0 ms), rose ~20% in both labels between
the groups. Within each group B is 19.3% and 20.6% faster, and all four B
runs (7.48–8.79 ms) are below all four A runs (9.29–11.01 ms); by the fixed
rule the change is still inside the spread, so it is reported as consistent
but not material. pan at 7680×2160 likewise: −13.7% / −14.1% per group,
inside its 25% threshold.

Readings:

- **Camera motion no longer rebuilds models.** In pan and zoom 95% of the
  visible units are re-placed from retained local geometry; vertices
  generated fall 98% (only the units whose pose or material changed are
  rebuilt), worker build time falls 76–77% at both sizes, the geometry
  phase's wall time 57–61%, and pool dispatch roughly halves. Zoom's draw
  time falls 17.7% / 16.1% (material at both sizes); pan reads −14 to −20%
  in every ABBA group; churn −6.5 to −9.4% (not material) with its geometry
  phase −29 to −32% (material).
- **The key check costs more on a pan** (`w_geometry_key` +28–32%, ≈ +0.7
  ms CPU summed over threads at 2000 units, ≈ 0.4 µs per unit): the former
  single key stopped comparing at the first changed word, the screen anchor
  (word ~16), so a pan miss was cheap to detect; the local stage must now
  compare its whole key (every piece pose) to prove it still valid — that
  comparison is what replaces the rebuild. Stationary scenes, where every
  key compares to the end in both builds, are unchanged (−4 to −6%).
- **A local rebuild costs 7–12% more worker time** (stationary-mixed +8.1%
  / +12.0%, moving +9.5% / +6.7%): the staged build keeps local positions,
  runs placement as a separate loop and compares the new shadow output with
  the old for the exact `shadowRevision`. In moving (pose-led: 757 of 773
  misses rebuild locally) this is the whole effect of D; draw time is
  unchanged within noise (−0.2% / +0.8%), `t_geometry` +7.2% / +3.8%, below
  threshold.
- **Stationary armies are unchanged** (−0.5% / −0.6%, identical counters).
  C's residual (measured against the pre-C build) was not re-measured; D
  vs C shows no further cost, and the entry layout change (`32c5d71a`) was
  not separately timed.
- **Memory.** Geometry used bytes +45–50%, retained capacity +18–22% at the
  last frame (entries 280 → 512 bytes, local positions 8 B per body and per
  shadow vertex, base colours only for untextured faces); peak RSS within
  run-to-run noise (A/B ranges overlap; largest shift +13 MiB in moving
  1280). No new GPU resources.

### Experiment 7 — workstream E split body/shadow invalidation (2026-10-16)

Clients: **A** = DFH (`~/tak-tmp/render-opt/takclient-DFH-candidate`: code
of `aae1a9fa` = workstream D plus only the harness commit — cherry-picked as
`9b01c67f` in `~/tak-tmp/wt/render-opt-dfh` — so the new `pan-snap` and
`veteran` scenes run identically in both builds), **B** = E3
(`takclient-E3-candidate`, code of `15da2bff`, the final E code); the first
measured candidate **E1** (`71a71e9b`, serial point checks) and **E2**
(pre-pass, stamps inside the sorted work items) are reported for the record.
Build-o2, offscreen EGL, SDL `opengl` on the RTX 5070 Laptop GPU;
`RENDER_ENV` confirmed renderer and output (1280×960 → world 1075×888;
7680×2160 → world 7475×2088) for every run. 900 frames, 300 warmup,
forced-completion frames, client pinned to CPUs 16–23 (the Legion program on
4–15: 0–15 `legion_scenario` processes, load average 2.3–18.8, recorded per
run; pinned-CPU busy fraction 0.17–0.26, no run contended), ABBA × 2 per
scene and size, `--wait-quiet 60`. Lean testing: timed only the scenes E
changes — `pan-snap` (translation reuse), `veteran` (body-only reuse),
`pan` (sub-pixel pan: any tile that keeps its slot runs the point check and
is rejected — the cost guard) and `stationary-mixed` (dense army whose lodestones animate their
masks: no gain expected, guard for the per-tile classification). Pipelined
frames not measured. Output (not in Git): `~/tak-tmp/render-opt/runs/e-*`
(E1), `e2-*`, `e3-*`; scripts `~/tak-tmp/render-opt/e/final{,2,3}.sh`,
summary `e/etable.py`, stats helper `e/srun.py`. Commands:

```sh
T="python3 tools/render_benchmark.py --a $DFH --b $E3 --data ~/tak_data --cpus 16-23"
$T --output runs/e3-captures-normal --scenes pan-snap veteran pan effects churn total10k-visible --capture-only
$T --output runs/e3-captures-uhd --sizes uhd --scenes pan-snap veteran pan --capture-only
$T --output runs/e3-normal --scenes pan-snap veteran pan stationary-mixed --repeats 2 --wait-quiet 60
$T --output runs/e3-uhd --sizes uhd --scenes pan-snap veteran pan stationary-mixed --repeats 2 --wait-quiet 60
python3 ~/tak-tmp/render-opt/e/etable.py runs/e3-normal/results.json runs/e3-uhd/results.json
VCPUS=16-23 python3 ~/tak-tmp/render-opt/e/srun.py $E3 <scene> normal 300 WARM=0 TAK_SHADOW_CACHE_VERIFY=1 TAK_GEOMETRY_VERIFY=1
```

**Correctness.** Captures A vs B: 0 differing pixels in the world view AND
the full frame in every comparison — E1: 96 at 1280×960 (pan-snap, veteran,
pan, stationary-mixed, effects, churn, colour-reset, total10k-visible) + 48 at
7680×2160 (pan-snap, veteran, pan, effects); E2: 72 + 24; E3 (final): 72
(pan-snap, veteran, pan, effects, churn, total10k-visible) + 36 (pan-snap,
veteran, pan). World hashes identical A vs B in every scene. GL verifier runs
(`TAK_SHADOW_CACHE_VERIFY`: every kept tile compared with a complete re-bake
of the frame, pixel for pixel, and every tile the pre-E key kept must be kept;
`TAK_GEOMETRY_VERIFY`: every reuse vs a complete rebuild incl. the recorded
shadow placement, every skipped silhouette vs a fresh collection) — all
passed: effects (299 frames, 156,316 tiles), stationary-mixed (545,675),
churn (499,342), colour-reset (59,800; 4 body-only), an 80-unit whole-pixel
pan with veterancy (23,920 tiles, 20,446 translated, 928 skipped
silhouettes), a static 80-unit scene with veterancy (1,131 body-only kept
tiles the pre-E key redrew), pan-snap at 1280×960 (274,463 tiles, 119,125
translated) and 7680×2160 (280,514; 118,689 translated; E3 code).
`render_shadow_cache` (software renderer): 12,038 tiles, 88 body-only, 4,568
translated, 296 skipped silhouettes. `shadow_tiles` passes; ten header
mutations fail it; three in-game mutations fail `render_shadow_cache`.
`ctest -L quick` 381/381 on build-o2 at `71a71e9b` and at `15da2bff`. Release `takclient` builds and contains none of
the new debug/telemetry strings.

**What changed in the counters (exact, mean per frame, frames 300–899).**

| Scene, size | Visible | Tiles dirty A → B | Dirty pixels A → B | Kept: translated / body-only (B) | Dirty causes B: owner / relocated / content / moved |
|---|---:|---:|---:|---:|---|
| pan-snap 1280 | 1094 | 1094 → 695 (−36.5%) | 4.39 M → 2.60 M (−40.8%) | 399.7 / 0 | 403 / 188 / 30 / 60 |
| pan-snap 7680×2160 | 1941 | 1873 → 1105 (−41.0%) | 7.51 M → 4.27 M (−43.1%) | 767.5 / 0 | 868 / 126 / 60 / 44 |
| veteran 1280 | 1825 | 866 → 858 | 0.953 M → 0.946 M | 0 / 8.1 | 148 / 646 / 64 / 0 |
| veteran 7680×2160 | 2000 | 923 → 914 | 2.127 M → 2.112 M | 0 / 8.9 | — |
| pan, stationary-mixed (both sizes) | 1584–2000 | unchanged | unchanged | 0 / 0 | pan: the sub-pixel phase changes tile sizes, so the repack reassigns nearly every index (`owner`); only 3.7 (1280) / 31.6 (7680×2160) tiles per frame reach the point check, all rejected |

Geometry counters (hits, stages, vertices) are identical A vs B: E changes
no stage decision; `geom_shadow_skipped` is 0 in every timed scene (no
roster unit has an unmasked animated texture) and `geom_shadow_kept` 0
(lodestone mask animation, see Known limitations).

**Timing (forced completion; spread = max−min over a label's four run
means; material = beyond `max(2 × spread, 2%)`; groups = B−A per ABBA
group).**

| Scene, size | Draw mean ms A → B | Δ | Groups | Material | Draw p99 A → B | t_shadow_pack A → B | t_shadow_raster | t_shadow_alloc | t_shadow_prep | GPU timeline A → B |
|---|---:|---:|---|---|---:|---:|---:|---:|---:|---:|
| pan-snap 1280 | 5.394 → 4.993 | −7.4% | −7.0%, −7.9% | no (thr 16.9%) | 9.46 → 7.25 | 1.387 → 1.110 (−20.0%; −20.1/−19.9) | −30.2% | 0.223 → 0.288 (+29%, material) | −28.0% | 2.596 → 2.299 (**−11.5%, material**) |
| pan-snap 7680×2160 | 11.137 → 10.808 | −3.0% | −3.5%, −2.5% | no (10.4%) | 16.53 → 17.56 | 3.292 → 2.977 (−9.6%) | −14.0% | 0.359 → 0.570 (+59%, material) | −28.4% (material) | 6.908 → 6.671 (−3.4%) |
| veteran 1280 | 5.707 → 5.646 | −1.1% | −0.3%, −1.9% | no | 13.26 → 13.48 | −1.8% | +2.3% | −14.0% (material) | −4.4% | −6.4% |
| veteran 7680×2160 | 7.962 → 7.942 | −0.2% | +1.3%, −1.8% | no | 15.51 → 15.29 | −4.8% | −1.4% | −9.1% (material) | −8.2% | +0.8% |
| pan 1280 | 7.358 → 7.329 | −0.4% | −1.3%, +0.5% | no | 13.54 → 13.35 | +0.3% | +2.9% | −10.4% (material) | −1.2% | −1.4% |
| pan 7680×2160 | 14.061 → 13.866 | −1.4% | −3.4%, +0.7% | no | 16.24 → 15.89 | −1.3% | +1.5% | −5.4% | −5.8% | −1.8% |
| stationary-mixed 1280 | 5.294 → 5.287 | −0.1% | −0.5%, +0.3% | no | 13.00 → 12.95 | −1.2% | +2.2% | −12.5% (material) | −1.9% | +0.7% |
| stationary-mixed 7680×2160 | 8.191 → 8.245 | +0.7% | −0.5%, +1.8% | no | 14.50 → 14.45 | +1.3% | +2.9% | −8.3% | +7.7% | +0.2% |

Readings:

- **Translation reuse works where the camera moves in whole pixels**: in
  pan-snap 37–41% of all tiles are kept (A: 0–4%), dirty raster area falls
  41–43%, shadow raster time 14–30%, shadow packing 10–20% (consistent in
  both ABBA groups, inside the threshold), and the GPU draw timeline 11.5%
  at 1280×960 (material). Draw time −7.4% / −3.0%, consistent per group,
  not material by the fixed rule. More tiles would be kept if they stayed in
  place: 868 of 1105 remaining dirty tiles at 7680×2160 are `owner`
  (the repack gave the index to another unit) — that is F.
- **The point check has a price**: `t_shadow_alloc` +0.065 ms (1280) and
  +0.21 ms (7680×2160) in pan-snap, where ~460 / ~810 tiles are checked
  in full; it is smaller than the raster time saved. In every other scene the
  stamp loop is cheaper than before (−5 to −14%, material in five cases),
  mostly because the old loop read `TAK_SHADOW_NOCACHE` from the environment
  once per tile.
- **Body-only reuse is real but small in the matrix**: veteran keeps 8–9
  extra tiles per frame (the veterancy cycle touches ~13 units per frame,
  most of whose tiles the repack moves anyway); draw unchanged.
- **Sub-pixel pans and stationary armies are unchanged** (pan, stationary-
  mixed: identical counters, draw within ±1.4%). The rejected check costs
  one point per moved tile.
- `t_shadow_composite`, which E does not touch, reads +3–4% (≈ 5 µs of
  0.12–0.15 ms) at 1280×960 in veteran, pan and stationary-mixed (material
  only in veteran, whose spread is 0.7–1.6%), and ±1% at 7680×2160. The
  entry fields the composite reads keep their offsets in E3 (checked with
  gdb); not explained, not material in draw time.
- **Memory.** `UnitGeom` stays 512 bytes. New CPU scratch: one 80-byte stamp
  per packed tile, tiles per page and the check list (counted in
  `shadow_scratch_*`; pan-snap total shadow scratch capacity 4.8 / 7.9 MiB at
  the last frame). `PieceMeta` gains one byte per animated texture record.
  No GPU resources. Peak RSS within run-to-run noise in every scene.

**E1 and E2 (superseded, measured).** E1 ran each translation check serially
in the stamp loop: pan-snap `t_shadow_alloc` 0.191 → 0.607 ms (+217%) at
1280×960 and 0.428 → 1.331 ms (+211%) at 7680×2160, both material, so
shadow packing gained nothing (+5.7% / −4.7%) although raster time fell
29.6% / 27.5%. E2 moved the decisions into a pre-pass with pooled, block-wise
checks: `t_shadow_alloc` +0.24 ms at 7680×2160 (+61%), shadow packing
−18.3%, draw −7.5% (1280×960 session too noisy to read, A/A spreads
20–50%); in pan its pre-pass cost +5% `t_shadow_alloc` because the stamps
lived in the height-sorted work items. E3 keeps stamps in a separate
scratch vector and reuses the pre-pass buffers (the numbers above).

### Experiment 8 — workstream F persistent shadow tile cells (2026-10-16)

Clients: **A** = E3 (`~/tak-tmp/render-opt/takclient-E3-candidate`, code of
`15da2bff` = workstream E, the pre-F build), **B** = the final F code
(`takclient-F6-candidate`, code of `3fd2c8de`) for the all-redraw scenes and
captures, and F3 (`takclient-F3-candidate`, code of `6c7b93df`) for the reuse
scenes (F4–F6 change only frames where every tile is redrawn; counters
checked equal). F2 (code of `5febc00f`) was measured first; its numbers are
given where they motivated a change or come from a quieter session. Build-o2
(`-O2` Debug), offscreen EGL, SDL `opengl` on the RTX 5070 Laptop GPU;
`RENDER_ENV` confirmed renderer and output (1280×960 → world 1075×888;
7680×2160 → world 7475×2088) for every run. 900 frames, 300 warmup,
forced-completion frames, client pinned to CPUs 16–23 (reserved for this
program; the Legion program on 4–15, load and `legion_scenario` count
recorded per run), ABBA × 2 per scene and size, `--wait-quiet 60`. Lean
testing: timed only the scenes F affects — stationary-mixed (pre-F tiles
redrawn because the repack relocated them), total10k-visible (the largest
shadow cost; at 7680×2160 the whole-frame fallback), pan-snap (translation
reuse needs a stable cell), moving (pose-led: content redrawn every frame,
cells resized every frame — the allocator's cost guard), zoom (every tile
redrawn: allocation churn and free repacks, nothing to gain) and churn
(deaths, respawns, retention, resets). Pipelined frames not measured.
Output (not in Git): `~/tak-tmp/render-opt/runs/f2-*`, `f3-*`,
`f2-repack-*`; scripts `~/tak-tmp/render-opt/f/{timing3,captures,explain,verify,verify3}.sh`,
summary `f/ftable.py`, counter runs `f/srun.py` / `f/srunt.py`, parameter
study `f/study{1,2,3,4}.txt`. Commands:

```sh
T="python3 tools/render_benchmark.py --a $E3 --b $F3 --data ~/tak_data --cpus 16-23"
$T --output runs/f3-captures-normal --scenes stationary-mixed pan-snap total10k-visible churn effects moving zoom colour-reset --capture-only
$T --output runs/f3-captures-uhd --sizes uhd --scenes stationary-mixed pan-snap zoom --capture-only
$T --output runs/f3-normal --scenes stationary-mixed total10k-visible pan-snap moving zoom churn --repeats 2 --wait-quiet 60
$T --output runs/f3-uhd --sizes uhd --scenes stationary-mixed total10k-visible pan-snap moving zoom churn --repeats 2 --wait-quiet 60
# capture explanation (F code running the former packer = same slots; former packer with other slots)
python3 tools/render_benchmark.py --a $E3 --b $F2 --b-env TAK_SHADOW_REPACK=1 --scenes churn effects pan-snap total10k-visible zoom moving --capture-only ...
python3 tools/render_benchmark.py --a $F2 --b $F2 --a-env TAK_SHADOW_REPACK=1 --b-env TAK_SHADOW_REPACK=1 --b-env TAK_SHADOW_PAGE_MAX=4096 --scenes pan-snap churn --capture-only ...
VCPUS=16-23 python3 ~/tak-tmp/render-opt/f/srun.py $F3 <scene> <size> 300 WARM=0 TAK_SHADOW_CACHE_VERIFY=1 TAK_GEOMETRY_VERIFY=1
python3 ~/tak-tmp/render-opt/f/ftable.py runs/f3-normal/results.json runs/f3-uhd/results.json
```

**Correctness.**

- Verifiers (GL, `TAK_SHADOW_CACHE_VERIFY`: every kept tile compared with a
  complete re-bake of the frame pixel for pixel, every tile the pre-E key kept
  must be kept, `ShadowAtlasAllocator::validate()` every frame, every tile
  inside its cell; with `TAK_GEOMETRY_VERIFY`). Final code (F6): pan-snap
  (325,789 tiles, 269,639 kept after whole-pixel translations), churn
  (499,342; a repack every frame), zoom (396,627; a repack every frame),
  effects (156,316; 32,587 kept), colour-reset (39,800; 28,956 kept, target
  resets). F5: pan (486,122), stationary-mixed (363,175; 344,283 kept). F3:
  pan-snap 7680×2160 (380,514; 333,115 kept), churn 7680×2160 (406,988).
  F1/F2: zoom with best-fit re-fitting, the dense 7680×2160 pan-snap, and the
  small-page stress scenes (cells kept, evicted, expired, repacked, migrated,
  pages released and shrunk). No mismatch, no allocator inconsistency.
- ctests: `shadow_atlas` (ASan/UBSan clean; five header mutations fail it),
  `shadow_tiles`, `render_shadow_cache`, `render_shadow_atlas`,
  `render_shadow_atlas_zoom` pass on the final code; three in-game
  mutations (no clear of recycled cells, a moved cell treated as the same
  slot, an outgrown cell kept) each fail the stress scene with a pixel
  mismatch or "tile exceeds its cell".
- World hashes identical A vs B in every scene of every run.
- **Captures are not pixel-identical to E3, and the cause is established.**
  F3 vs E3, 12 frames per scene: identical in colour-reset and stationary-
  mixed (1280×960); in the other 9 scene/size pairs 1–12 pixels differ per
  differing frame (of 0.95 M / 15.6 M), 69 differing frames of 132, 260
  differing pixels in all. Every one is a single silhouette edge pixel: the
  pixel is one shadow level darker in one build (ratio 140/255 per channel;
  2 near-black pixels match per channel after 8-bit rounding, 2 in effects
  sit under a translucent orange overlay whose blue channel drops by exactly
  that ratio). Controls: (1) the F code running the former packer
  (`TAK_SHADOW_REPACK=1`, the same slots as E3) is identical to E3 in all 96
  comparisons (churn, effects, pan-snap, total10k-visible, zoom, moving at
  1280×960; pan-snap, stationary-mixed at 7680×2160); (2) the former packer
  against itself with only its slots changed (4096-px pages; one page, no
  fallback) shows the same kind of difference — 1–10 single-level pixels per
  frame in pan-snap and churn. So the raster of a tile depends on its cell
  position below pixel precision (float `placed*scale − pixel + slot + 1`,
  then GPU snapping), already in the pre-F code; any allocator that places a
  tile elsewhere than the former packer differs at this level. See Known
  limitations. (A first control at 1024-px pages was invalid: those scenes
  overflow four small pages and fall back to direct shadows.)
  The F6 code (all-redraw frames use the former packer with exact sizes)
  makes churn, pan and zoom identical to E3 at both sizes; the final
  capture run (`runs/f6-captures-*`, 156 comparisons) differs only in
  pan-snap, stationary-mixed 7680×2160, total10k-visible, effects and moving:
  208 pixels in 49 frames, every one a single-level edge pixel (the 2 effects
  pixels under the overlay as above).

**What changed in the counters (exact, mean per frame, frames 300–899; A = E3, B = final F).**

| Scene, size | Visible | Tiles kept A → B | Tiles dirty A → B | Dirty pixels A → B | Dirty causes A: owner / relocated → B | Cells allocated per frame (B) | Pages (B) |
|---|---:|---:|---:|---:|---|---:|---:|
| stationary-mixed 1280 | 1825 | 967 → 1742 | 858 → 83 | 945 k → 151 k | 148 / 646 → 0 / 0 | 6.3 | 1 |
| stationary-mixed 7680×2160 | 2000 | 1086 → 1910 | 914 → 90 | — → 333 k | 359 / 476 → 0 / 0 | 7.2 | 2 |
| total10k-visible 1280 | 8752 | 4383 → 8346 | 4369 → 406 | 1447 k → 207 k | 1028 / 3045 → 0 / 14 | 41 | 1 |
| total10k-visible 7680×2160 | 9398 | whole-frame direct path in both | — | — | — | 0 | 0 (released) |
| pan-snap 1280 | 1094 | 400 → 905 | 695 → 190 | 2602 k → 808 k | 403 / 188 → 0 / 0 | 8–10 | 2 |
| pan-snap 7680×2160 | 1941 | 836 → 1715 | 1105 → 227 | — | 868 / 126 → 0 / 3 | 11 | 3 (A 2.3) |
| moving 1280 | 1195 | 10 → 419 | 1185 → 776 | 414 k → 279 k | 1120 / 48 → 0 / 3 | 172 | 1 |
| moving 7680×2160 | 1200 | 10 → 419 | 1190 → 781 | — | 1127 / 43 → 0 / 4 | 180 | 1 |
| zoom, churn, pan (both sizes) | 1584–2294 | 0 → 0 | all → all | equal | owner → relocated (repacked every frame) | = tiles | = A |

Every dirty tile left in the stationary scenes is a genuine coverage change
(lodestone mask animation, `shadow_dirty_content`); in pan-snap the rest are
sub-pixel `moved` tiles from the scene's off-grid cycles; in moving, the
walking units' pose changes.

**Timing (forced completion; spread = max−min over a label's four run means;
material = beyond `max(2 × spread, 2%)`; groups = B−A per ABBA group).**
Reuse scenes from the F3 session (code `6c7b93df`; F4–F6 change only frames
where every tile is redrawn, and F3 vs F6 counters for these scenes agree
within 0.1% of dirty tiles); 1280×960 also from the earlier, quieter F2
session. All-redraw and pan scenes from the F6 session (final code).

| Scene, size (session) | Draw mean ms A → B | Δ | Groups | Material | Draw p99 A → B | t_shadow_pack A → B | t_shadow_raster | t_shadow_alloc | t_shadow_composite | GPU timeline A → B |
|---|---:|---:|---|---|---:|---:|---:|---:|---:|---:|
| stationary-mixed 1280 (F2) | 5.184 → 3.496 | **−32.6%** | −30.8, −34.3 | yes (thr 7.7%) | 15.29 → 4.37 | 1.756 → 0.307 (−82.5%) | −94.9% | −25.1% | −5.6% | 1.76 → 1.18 (−32.8%) |
| stationary-mixed 1280 (F3) | 5.864 → 4.594 | −21.7% | −20.5, −23.0 | no (thr 68.5%, noisy session) | 13.55 → 6.30 | 1.805 → 0.465 (−74.3%, material) | −93.0% | −17.1% | −4.2% | 2.04 → 1.34 |
| stationary-mixed 7680×2160 (F3) | 7.895 → 6.175 | **−21.8%** | −21.4, −22.2 | yes (6.0%) | 17.15 → 7.30 | 1.355 → 0.449 (−66.9%) | −86.6% | −23.1% | −10.4% | 5.25 → 3.81 (−27.5%) |
| total10k-visible 1280 (F2) | 39.541 → 19.531 | **−50.6%** | −50.5, −50.7 | yes (3.0%) | 67.35 → 25.79 | 21.94 → 1.89 (−91.4%) | −98.4% | −16.7% | −7.4% | 20.51 → 10.13 (−50.6%) |
| total10k-visible 1280 (F3) | 41.020 → 20.520 | **−50.0%** | −50.5, −49.5 | yes (24.1%) | 68.97 → 26.65 | 22.48 → 2.08 (−90.8%) | −98.2% | −16.0% | −6.4% | 21.21 → 10.55 |
| total10k-visible 7680×2160 (F3) | 61.366 → 60.675 | −1.1% | −1.7, −0.5 | no | 68.05 → 67.69 | 1.342 → 0.977 (−27.2%, material; fallback frames) | — | −27.2% | −0.8% | 54.10 → 53.92 |
| moving 1280 (F2) | 10.094 → 5.964 | **−40.9%** | −40.5, −41.4 | yes (22.4%) | 15.08 → 14.65 | 5.78 → 1.69 (−70.8%) | −76.2% | +2.2% | +24% (n.s.) | 2.54 → 1.66 (−34.4%) |
| moving 1280 (F3) | 10.295 → 6.486 | **−37.0%** | −37.0, −37.0 | yes (26.3%) | 14.83 → 13.10 | 5.45 → 1.77 (−67.5%) | −74.1% | +1.0% | +3.7% | 2.75 → 1.76 (−35.9%) |
| moving 7680×2160 (F3) | 13.205 → 8.731 | **−33.9%** | −34.1, −33.6 | yes (3.5%) | 15.93 → 15.64 | 7.36 → 2.43 (−67.0%) | −75.5% | +5.2% | +47% (+0.06 ms, material) | 4.08 → 2.55 (−37.5%) |
| pan-snap 1280 (F6) | 4.882 → 4.192 | **−14.1%** | −16.1, −12.1 | yes (7.2%) | 10.31 → 5.13 | 1.086 → 0.714 (−34.3%) | −59.4% | +29.6% (+0.08 ms) | −7.2% | 2.05 → 1.68 (−18.3%) |
| pan-snap 7680×2160 (F6) | 10.257 → 7.410 | **−27.8%** | −27.3, −28.2 | yes (4.2%) | 16.17 → 12.78 | 2.804 → 1.162 (−58.5%) | −83.7% | +13.6% | +34.4% (+0.14 ms, material) | 6.16 → 4.22 (−31.6%) |
| zoom 1280 (F6) | 10.662 → 10.978 | +3.0% | +5.9, +0.1 | no (20.6%) | 20.94 → 19.45 | 3.497 → 3.755 (+7.4%) | +3.7% | +56.6% (+0.18 ms, material) | −2.3% | 7.14 → 7.38 |
| zoom 7680×2160 (F6) | 13.950 → 14.309 | +2.6% | +2.4, +2.8 | no (5.6%) | 16.09 → 16.44 | 3.242 → 3.471 (+7.1%) | +1.7% | +52.0% (+0.19 ms, material) | +1.1% | 9.45 → 9.64 |
| churn 1280 (F6) | 13.080 → 13.220 | +1.1% | +1.6, +0.6 | no | 22.36 → 22.13 | 4.931 → 5.094 (+3.3%) | +0.5% | +54.3% (+0.17 ms, material) | −1.5% | 5.46 → 5.54 |
| churn 7680×2160 (F6) | 16.072 → 16.336 | +1.6% | +1.7, +1.6 | no | 23.95 → 24.18 | 6.316 → 6.578 (+4.1%) | +1.4% | +49.6% (+0.20 ms, material) | −4.2% | 10.37 → 10.46 |
| pan 1280 (F6) | 8.339 → 8.311 | −0.3% | −1.2, +0.5 | no | 14.69 → 14.89 | 2.777 → 2.831 (+2.0%) | −4.0% | +52.7% (+0.14 ms, material) | +0.7% | 4.11 → 4.15 |

Load per session (recorded per run in each `results.json`): F2 0–12
`legion_scenario` processes on CPUs 4–15, load average 1.4–4.9 for the
1280×960 runs used above, pinned-CPU busy fraction 0.18–0.27 (the F2
7680×2160 runs are not used: F3/F6 superseded them); F3 1–4 processes, load
1.5–12.4, busy 0.16–0.28; F6 0–3 processes, load 2.8–13.2, busy 0.20–0.26.
No run in the tables was contended.

Readings:

- **Stable cells turn E's exact key into reuse.** Wherever tiles were redrawn
  only because the per-frame repack moved them, they now stay: stationary
  armies keep 95% of their tiles (A: 53%), the dense 1280×960 army 95% (A:
  50%), moving armies keep their idle units' tiles (419 per frame), and
  whole-pixel pans keep 83–88% (A: 37–43%). Shadow packing falls 58–91% in
  those scenes and draw time 14–51% (material in every reuse scene and size
  except stationary-mixed in the noisy F3 1280×960 session, which is
  material in the quieter F2 session). p99 frame draw falls 50–71% in the
  stationary and dense scenes.
- **All-redraw scenes (zoom, churn, sub-pixel pan) gain nothing and cost
  +0.14–0.20 ms of `t_shadow_alloc`** — the per-tile cell bookkeeping
  (validated handle, carried 80-byte stamp, repack binds, ~0.1 µs per tile).
  Draw time +3.0% / +2.6% (zoom), +1.1% / +1.6% (churn), −0.3% (pan): not
  material, and the zoom groups are consistently positive, so read it as a
  small real cost. Earlier F builds that re-fitted cells one by one in these
  scenes cost more (zoom 7680×2160 +15–23% in noisy sessions, F3/F4; see
  Rejected experiments).
- **The dense 7680×2160 scene** still overflows four pages; F falls back at
  once (−27% shadow phase) and releases the pages it no longer uses
  (tracked GPU at the last frame 306.5 → 242.5 MiB).
- **Composite cost** rises where tiles spread over more pages than before
  (pan-snap 7680×2160: 3 pages vs 2.3, +0.14 ms; moving 7680×2160 +0.06 ms):
  painter-order neighbours on different pages break composite batches.
  `t_body_submit` also rose in moving 7680×2160 (+0.6 ms, material; F does not
  touch body submission, total draw −34%); a shift of driver waits, not
  investigated further.

**Memory.**

| | Tracked GPU (last frame), A → B | Notes |
|---|---|---|
| stationary-mixed, total10k-visible 1280, pan-snap, moving, churn (both sizes) | equal | same allocated pages at the end |
| total10k-visible 7680×2160 | 306.5 → 242.5 MiB | fallback frames: F releases the unused pages (A kept 4) |
| zoom 7680×2160 | 290.5 → 274.5 MiB | A kept a peak page |
| pan 1280 | 138.0 → 122.0 MiB | |
| battle-sparse 1280 (frame 440, battle / frame 899, empty view) | 131.3 → 147.3 / 124.4 → 108.4 MiB | F holds a 2nd page in the battle (repack at 155, a failed allocation inside the 60-frame cooldown at 212 adds a page), and frees the whole atlas 2 s after the view empties (A keeps it) |
| battle-sparse 7680×2160 (same frames) | 245.7 → 261.7 / 378.3 → 346.5 MiB | the last page shrinks to 256² for the one remaining shadow |

CPU: `shadow_scratch_capacity_bytes` +0.4 to +5.4 MiB (cell records,
shelves, per-cell stamps, carried stamps: total10k-visible 39.9 → 45.3 MiB);
`UnitGeom` unchanged (512 bytes); peak RSS within run-to-run ranges (B lower
in most scenes). A 30-frame compaction cooldown was tried against the
battle's extra page (`takclient-F7-candidate`): average pages in the battle
1.56 → 1.28 at 1280×960, unchanged at 7680×2160 and in pan-snap — not
adopted (shelf fragmentation, not the cooldown, decides it).

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
- Workstream C changes ownership only. Content validity is still one exact
  key that includes the screen anchor, so any camera motion still rebuilds
  every visible unit (D), and shadow tiles are still assigned by the per-frame
  repack, so a moving army's tiles still change occupant every frame
  (`shadow_dirty_owner` is nearly unchanged; F). The gains of C alone are the
  hits that depth-order changes used to destroy (`miss_owner` → 0) and the
  memory behaviour.
- The retention windows are fixed constants (absent ≤ 2 s and ≥ 30 frames,
  32 MiB / 8192 entries; released capacity ≤ 8 MiB / 4 s), measured on the
  presentation clock (fixed 1/60 s steps in the render study, wall time in
  play). They are not yet part of a CPU budget (J).
- The world epoch is bumped on the client's rejoin/spectate reset, the only
  client caller of `World::resetForReplay`. A future world-reset path must bump
  `worldEpoch_` too. In game the id-reuse path is exercised by
  `TAK_RENDER_EPOCH` (an epoch bump without a real reset, so every unit is
  re-acquired cold); a real network rejoin was not run under the verifier.
- `StableEntryPool::ownerOf` (debug verifier only) is linear in chunks.
- Workstream C has no backend-specific code. Exercised on SDL `opengl`
  (offscreen EGL, RTX 5070) and the SDL software renderer (ctest
  `render_geometry_ownership`, dummy video driver); Direct3D/Metal/GLES not run.

- Workstream D caches screen-space vertices, so a pan still writes every
  vertex position (`verts_placed`, ~1.05 M per frame in pan) and bumps
  `revision`; shadow tiles therefore still re-rasterize on every pan
  (`shadow_tiles_dirty` unchanged) and distant images still re-bake on a
  zoom. D removes the piece transforms, collection, sort and shadow
  silhouette collection from that path; the remaining per-pan work belongs
  to E/F (coverage keyed on `shadowRevision` + placement) and H (placement
  at submission instead of in the cached vertices).
- A local rebuild costs 7–12% more worker time than the former single pass
  (`w_geometry_build`: stationary-mixed +8.1% / +12.0%, moving +9.5% / +6.7%
  in Experiment 6): it keeps the local positions, runs placement as a
  separate loop and compares the shadow output element by element for the
  exact `shadowRevision`. Scenes made of local rebuilds (a moving army whose
  every unit animates) gain nothing from D; their draw time is unchanged
  within noise.
- Memory: entries grew from 280 to 512 bytes and keep local positions (8 B
  per body and per shadow vertex): `geom_used_bytes` +45–50% and
  `geom_capacity_bytes` +18–22% at the last frame of the timed scenes
  (e.g. pan 33.3 → 39.8 MiB capacity; Experiment 6). Peak RSS moves within
  run-to-run noise (≤ +13 MiB). The existing retention budgets (C) bound
  it; they are not yet part of a CPU budget (J).
- Material reuse is exercised only where veterancy, tint, opacity or
  conjuring change without a pose change (effects: 1,969 material-only
  reuses in 400 verified frames); the timed scenes have none.
- `TAK_SHADOW_VERIFY` skips lodestones: its reference collector does not
  model `buildUnitShadow`'s lodestone shadow deviation, and it already failed
  on `c2c25707` in the mixed-roster scenes. Not a D regression; the
  geometry verifier covers lodestones.
- D has no backend-specific code. Exercised on SDL `opengl` (offscreen EGL,
  RTX 5070) and the SDL software renderer (`render_geometry_stages`, dummy
  video driver); Direct3D/Metal/GLES not run.
- **Harness: `constrained` captures depended on build speed before
  `aae1a9fa`.** `MapView` section idle trimming used wall time, so under the
  binding 128 MiB cap a faster build freed idle sections later (frames
  362/372 instead of 231), which changed when a terrain chunk job had room
  and which terrain detail frame 360 showed (10,956 pixels at the left edge,
  max channel error 63, in every C2-vs-D comparison; C2-vs-C2 identical, also
  with C2 slowed by `TAK_SHADOW_NOCACHE`/serial prep; D with
  `TAK_GEOMETRY_NOCACHE` still differed; `TAK_TERRAIN_CACHE_OFF` removed
  the difference). Fixed for the study by driving the trim from the
  presentation clock; play keeps wall time. The earlier `constrained`
  comparisons (workstreams A, B, C) were identical, i.e. those builds freed
  the sections in the same frames.

- **E's tile reuse needs the tile to stay at its index and slot.** Stamps are
  still compared by tile index of the per-frame height-sorted repack, so a
  tile whose content E proves unchanged is still redrawn when the repack
  hands its index to another unit (`shadow_dirty_owner`) or moves it
  (`shadow_dirty_relocated`). In the measured matrix scenes these dominate
  (e.g. stationary-mixed 93 owner + 452 relocated of 616 dirty tiles per
  frame; churn 1543 owner of 1664). E's own gains therefore show only where
  slots stay put; F (stable tile ownership) is what turns them on broadly.
  The E key and `classifyShadowTile` are written to carry over to F.
- **Alpha-mask animation is a genuine shadow dependency and stays one.** The
  lodestones (araplainlode, tarlode, …) animate textures with shadow masks:
  each frame has its own mask rect, so the cutout UVs change and the tile is
  redrawn — in stationary-mixed and total10k-visible every local rebuild is
  of this kind (`geom_shadow_kept` 0). E saves nothing there by design.
  Shipped data has 9 masked animated textures whose frames share an
  identical alpha channel (e.g. zonmanalodestone, zhonlode, arastonesolo);
  see "not pursued" below.
- **Unmasked animated textures** (where E skips the silhouette collection)
  are used only by tarnecro, tarwitch, tarhel, crepris and zonglyph (3DO
  scan of data.hpi/IPData.hpi); none is in the timed matrix rosters, so
  `geom_shadow_skipped` is 0 there. Exercised by `render_shadow_cache` and the
  verifier runs.
- **Translation reuse is limited to provably identical points.** Ordinary
  camera motion (edge scroll speed × frame time, the zoom-0.75 pan scene,
  any zoom other than 1 or powers of two) changes the sub-pixel phase, so
  every tile is redrawn exactly as before; only whole-pixel steps at zoom 1
  (drag and keyboard pans at zoom 1, the pan-snap scene) keep tiles. The
  rejected check costs one recomputed point per moved tile; an accepted one
  recomputes every point of the tile (on the pool once the checks hold
  ≥ 32768 points). Measured cost: pan-snap `t_shadow_alloc` +0.065 ms
  (1280×960) / +0.21 ms (7680×2160), less than the raster time it saves.
- The pixel verifier (`TAK_SHADOW_CACHE_VERIFY`) can only see real pixel
  differences: with whole-pixel steps only, a mutation that assumed every
  translation exact passed it (the points it wrongly kept differed below
  pixel resolution). The ctest scene therefore adds sub-pixel jumps; the
  bit-level property is pinned by `shadow_tiles`.
- `TAK_SHADOW_ATLAS_SOFTWARE` (debug) enables the atlas and mask coverage
  textures on SDL's software renderer for tests only; release builds keep
  the accelerated-only rule.
- E has no backend-specific code. Exercised on SDL `opengl` (offscreen EGL,
  RTX 5070) and the SDL software renderer (static SDL2 2.32,
  `render_shadow_cache`); Direct3D/Metal/GLES not run.

- **F: the shadow raster depends on the cell position below pixel
  precision** (all-redraw frames reproduce the former slots, so churn, pan
  and zoom match pre-F captures exactly; reuse scenes do not). A tile's points are `placed*scale − pixel + slot + 1` in float
  and then snapped by the GPU; the slot changes the rounding, so the same
  coverage rasterized in another cell can flip single silhouette edge pixels.
  This was already true of the former packer (its slots depended on every
  other tile of the frame); it was invisible in comparisons only because the
  compared builds used the same packer. Consequences: (1) F's persistent
  cells give 1–14 differing pixels per frame against pre-F captures, each
  exactly one shadow level (Experiment 8); (2) a moved cell is always redrawn,
  never copied, and translated reuse is checked in the same cell only.
  Making the raster slot-invariant (quantizing tile points so adding the slot
  is exact) was not attempted: it would change every tile's rounding relative
  to every earlier build and buy nothing F needs.
- **F does not lift the four-page limit.** The dense 7680×2160 scene
  (total10k-visible, 9398 shadows) still overflows four 2048² pages and takes
  the whole-frame direct path, exactly as before (the forced repack falls back
  only where the former packer did). Mixing atlas and direct shadows in one
  frame changes appearance (the direct path darkens folds twice), so it was
  not done; a multi-pass atlas (re-using pages between composite batches) is
  the option left.
- F's cell padding is only the shelf height class (≤ 12.5%) and the open
  tail of each shelf: measured cell area 1.11–1.25× the tile area (incl. retained
  cells) and shelf free space 0.17–1.67 M px at 1280×960 (`f/study4.txt`).
  Shelf fragmentation in a churning dense scene can still add a page the
  former packer did not need: battle-sparse holds 2 pages instead of 1
  (1280×960) and 3 instead of 2 (7680×2160) during the battle, and pan-snap at
  7680×2160 uses 3 pages vs the former 2.3 (same allocated bytes at the end:
  the former code kept its peak). The extra page is released only when demand
  falls (migration needs cells ≤ half of the other pages; empty pages after
  2 s). Within the unchanged 4-page ceiling.
- All-redraw scenes (zoom, churn, sub-pixel pans) pay +0.14–0.20 ms of
  `t_shadow_alloc` for the per-tile cell bookkeeping and gain nothing (every
  tile is redrawn wherever it sits); draw within ±3% (not material).
- Where tiles spread over more pages, painter-order neighbours on different
  pages break composite batches (pan-snap 7680×2160 +0.14 ms composite).
- Migration is exercised only by the `render_shadow_atlas` ctest scene
  (small pages, partial army left in view); in the matrix scenes cells of
  departed units expire and whole pages empty instead, and on all-redraw
  frames free repacks pre-empt it.
- Policy windows are fixed constants on the presentation clock (study: 1/60 s
  steps; play: wall time). Not yet part of a CPU/GPU budget beyond the existing
  VRAM cap (J).
- F has no backend-specific code. Exercised on SDL `opengl` (offscreen EGL,
  RTX 5070) and the SDL software renderer (static SDL2 2.32, the two
  `render_shadow_atlas` ctests); Direct3D/Metal/GLES not run.

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
- **C, rejected design: tying geometry entries to `assetGeneration_`.** It is
  bumped whenever a first-seen unit type's model textures load, so every
  entry would go cold (a full rebuild of the visible army) each time a new
  type appears. The exact key already carries the visual, type and atlas
  pointers; asset teardown goes through `invalidateRenderTargets`, which makes
  every entry cold. Not implemented.
- **C, rejected design: an id-indexed flat table for the owner lookup** (like
  `Frame::unitSlots`). Its size follows the largest unit id ever issued, i.e.
  every unit ever spawned in the match. Replaced by `OwnerIndex`, an
  open-addressing table sized from the live entries that shrinks again.
- **C, rejected experiment: software prefetch of the entries a few places
  ahead in the painter-order passes** (`__builtin_prefetch` of the whole
  `UnitGeom`, distance 6, in the geometry build, distant items, shadow work,
  shadow composite and body-assembly loops; debug switch
  `TAK_GEOMETRY_PREFETCH=0`). Aimed at the stationary-mixed residual cost.
  Same-binary A/B, pipelined, ABBA × 3 (`runs/c3-prefetch-same`, first group
  contended and discarded): stationary-mixed −0.3% / −1.7%, total10k-visible
  −1.7% / −1.7% with an untouched pass (body submit) moving as much — noise
  level. Against the pre-C build (`runs/c3-vs-pre`) stationary-mixed still
  read +0.4% / +3.0% / +1.6%. It did not remove the cost it targeted, so it
  was removed; not in any commit.
- **C, superseded: low-bit multiplicative hashing in `OwnerIndex`** (the
  measured C1 build, `b180e477`). Ids differing only above the table's bit
  width shared a bucket; sequential unit ids never hit this. `50acac44` uses
  the high bits (Fibonacci hashing); confirmation run in Experiment 5.
- **B, not pursued: building the first atlas on the CPU and uploading it once.**
  It would replace ~33,000 queued copies, but the source-blend paint of
  partial-alpha texels is GPU arithmetic that a CPU composite does not
  reproduce bit-exactly, and it would keep (or rebuild) every frame's RGBA in
  CPU memory. Not attempted; recorded as an option for workstream M/J.
- **D added no rejected experiments.** One capture discrepancy (the
  `constrained` scene, frame 360) was investigated and traced to the
  harness's wall-clock section trimming, not to D (see Known limitations);
  it was fixed in the harness, not by relaxing the comparison.
- **E, not pursued: sharing identical mask frames.** 9 masked animated
  textures have the same alpha in every frame, so their coverage never
  changes, but each frame occupies its own mask rect. Pointing every frame
  at the first one would make their shadows invariant (and save mask page
  space), but it changes the sampling location of every such cutout: UV
  interpolation rounds relative to the texel offset, so NEAREST/LINEAR
  results can differ at texel boundaries and captures would no longer be
  pixel-identical by construction. Left for M (indexed palette path), which
  rebuilds the mask/coverage representation anyway.
- **E: the first in-game test scene could not catch an "every translation is
  exact" mutation** (whole-pixel steps only; see Known limitations). Fixed
  by adding sub-pixel jumps to `TAK_RENDER_CAMERA_SNAP=N`, after which the
  mutation fails the ctest with a pixel mismatch.
- **F, rejected after measurement: padded size classes with slack.** The
  first F build rounded both cell dimensions up to classes (≥ 2 px slack,
  then ≤ 12.5% steps) and let shelves host tiles up to a quarter shorter, so
  a one-pixel bounds change would stay in its cell. Cells averaged 1.35–1.65×
  the tile area, and the atlas took one more 2048² page than the pre-F build
  in pan-snap (2 → 3), total10k-visible (1 → 2), zoom and churn (+16 MiB
  tracked GPU each); in zoom, where every tile is redrawn, shadow packing got
  slower (3.10 → 3.96 ms) from the extra page clears. A parameter study over
  slack, width quantum, class steps, shelf tolerance and keep ratio
  (`~/tak-tmp/render-opt/f/study{1,2,3}.txt`; final policy `study4.txt`) showed dirty-tile counts flat
  (178–192 in pan-snap) across all variants: a bounds change implies a
  coverage or placement change, so the tile is redrawn whether it moves or
  not — slack only reduces allocator churn. Final policy: exact widths,
  class-height shelves (tolerance 1/8), keep ≤ 1.5× (Experiment 8).
- **F, superseded: re-fitting cells one by one when every tile is redrawn
  anyway** (F2–F5). With persistent cells, zoom (every tile outgrows or
  shrinks), sub-pixel pans and churn re-fitted up to ~1,170 cells per frame by
  best fit, kept tiles spread over the pages, and the dense 7680×2160 fallback
  tried an incremental pass and two repacks per frame. Measured: zoom
  `t_shadow_alloc` +0.2–0.4 ms; zoom 7680×2160 draw +1.2% (F2), +22.6% (F3),
  +15.1% / +14.1% (F4, ABBA × 2 and × 3, every group positive but untouched
  phases also higher); churn composite +0.15–0.18 ms; total10k-visible
  7680×2160 `t_shadow_alloc` 1.3 → 3.0 ms. Fixed in `6c7b93df` (area check,
  failure memory), `ec3df732` (repack when all tiles are redrawn),
  `fbe21589` (a changed sub-pixel phase counts as redrawn) and `3fd2c8de`
  (that repack uses exact sizes: the former slots and pixels). Final: zoom
  +3.0% / +2.6%, churn +1.1% / +1.6%, pan −0.3% (not material).
- **F, not adopted: 30-frame compaction cooldown** (against the extra page in
  the battle-sparse battle): average pages in the battle 1.56 → 1.28 at
  1280×960, unchanged at 7680×2160 and in pan-snap; shelf fragmentation, not
  the cooldown, decides it. Kept at 60.
- **F, invalid control (recorded so it is not repeated):** comparing the
  former packer at 1024-px pages with itself at 2048 px to show slot
  dependence; pan-snap and churn overflow four 1024² pages and fall back to
  direct shadows, so 80,000+ pixels differ for an unrelated reason. Redone at
  4096 px (one page, no fallback).
- **E, superseded: serial translation checks in the stamp loop** (`71a71e9b`,
  measured as E1 in Experiment 7). Correct (captures identical, verifiers
  clean) but the checks cost +0.42 ms (1280×960) / +0.9 ms (7680×2160) of
  render-thread time in the whole-pixel pan, as much as the raster they
  saved. Replaced in `15da2bff` by the pre-pass with pooled, block-wise
  checks and stamps outside the sorted work items.

