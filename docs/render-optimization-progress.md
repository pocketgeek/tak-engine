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
  sizes, `b74f499e` docs; then, after the verifier's rejection, `ce2a3466`
  page textures created only after a frame's tiles fit — no page
  create/destroy churn when a frame fits four pages by area but not by
  packing — + `render_shadow_atlas_band` ctest, and the docs commit with
  Experiment 8b). Tests: `shadow_atlas` (also ASan/UBSan;
  five header mutations fail it), `shadow_tiles`, `render_shadow_atlas`,
  `render_shadow_atlas_zoom`, `render_shadow_cache` (three in-game mutations
  fail the new ctests), GL verifier runs (`TAK_SHADOW_CACHE_VERIFY`: every kept
  tile vs a full re-bake, allocator validated every frame; + `TAK_GEOMETRY_VERIFY`)
  on effects, churn, colour-reset, zoom, stationary-mixed, pan-snap 7680×2160
  and four small-page stress scenes; `ctest -L quick` 382/382 on build-o2 at
  `3fd2c8de` and again at `ce2a3466`; `render_shadow_atlas_band` (fails with
  the former immediate page creation: 47 creations / 44 releases, passes with
  3 / 0); GL verifier on the fix (battle-sparse, stationary-mixed in and out
  of the band); captures F6 vs F8 identical (60 comparisons); the Release
  `takclient` builds without the new debug/telemetry strings. Final-code
  timing of the reuse scenes: Experiment 8b. Captures: see Experiment 8 — identical to E3 in the all-redraw
  scenes, elsewhere 1–14 single shadow-edge pixels per differing frame, every
  difference explained (cell position changes the raster rounding). `--mpai` not run: only `src/client` (presentation) and
  `tools/` changed; every benchmark run checks the world hash (identical A vs
  B in every scene). Baseline for later comparisons:
  `~/tak-tmp/render-opt/takclient-F8-candidate` (code of `ce2a3466`; F6 =
  `3fd2c8de` differs only in the band fix).
- **Workstream G (distant-image lookup before body generation) is
  implemented and verified** (commits `6d56ac8d` cache side + differential
  test vs the pre-G cache (`tools/distant_models_legacy.h`), `b59cdd8a`
  GameView lookup before body generation + `render_distant_lookup` ctest,
  `b084644e` pan-distant / distant-sparse scenes, `2d755167` per-frame switch
  read and no empty catch-up scan (fixes a measured 7680×2160 guard cost of
  G1), then the docs commit). Tests: `distant_models` (software and offscreen
  GL; five mutations fail it), `render_distant_lookup` (an in-game mutation
  fails it), `geometry_stages`, GL verifier runs (`TAK_DISTANT_VERIFY` +
  `TAK_GEOMETRY_VERIFY`) on stationary-mixed, total10k-visible, pan-distant,
  veteran; `ctest -L quick` 383/383 on build-o2 at `2d755167`. Captures
  identical to F8 in all 168 comparisons (world view and full frame). Release
  `takclient` builds without the G debug strings. `--mpai` not run: only
  `src/client` (presentation) and `tools/` changed; every benchmark run checks
  the world hash (identical A vs B in every scene). Measurements: Experiment
  9. Baseline for later comparisons: `~/tak-tmp/render-opt/takclient-G2-candidate`
  (code of `2d755167`).
- **Workstream H (less full-scene vertex copying) is implemented and
  verified** (commits `97e42721` planner + GL buffer + `drawBuffer` + tests,
  `8d96905f` ordered body commands + arena integration + `render_body_arena`,
  `37ecd884` staged writes instead of a mapped window (measured fix),
  `d8744efe` rollover with two storages + `render_body_arena_still`,
  `e311cae5` release-build fix, then the docs commit; after the verifier's
  rejection — the record claimed `bodyVerts_` never exceeded its pre-H peak
  while H4 doubled it — `0cbc4401` bounded growth (need + 1/16), the
  double copy of runs in small commands counted (`arena_verts_twice`), the
  write-failure fallback re-assembling in place of the staged uploads,
  `drawBuffer` SDL errors counted as fallbacks, assembled commands verified,
  `render_body_arena_fail`; measured in Experiment 10b). Tests: `vertex_arena`
  (also ASan/UBSan; twelve mutations fail it), `geometrysubmit` (software
  and accelerated GL), `render_body_arena`, `render_body_arena_still`, GL
  verifier runs; `ctest -L quick` 384/384 on build-o2 at `d8744efe` (the
  release fix after it only guards debug counters) and again at `0cbc4401`
  (384/384; `render_body_arena[_still|_fail]` are costed, not `quick`, and
  pass). Captures identical to G2
  (Experiment 10). Release `takclient` builds and contains none of the H
  debug strings. `--mpai` not run: only `src/client` (presentation) and
  `tools/` changed; every benchmark run checks the world hash (identical A vs
  B in every scene). Measurements: Experiments 10 and 10b. Baseline for later
  comparisons: `~/tak-tmp/render-opt/takclient-H5-candidate` (code of
  `0cbc4401`; H4 = `d8744efe` superseded).
- **Workstream I (cost-based work scheduling) is implemented and verified**
  (commits `a5ba14e4` ThreadPool — affinity size, targeted wakes, chunk
  claiming, the caller waits for chunks not workers — + `WorkPlan` / cost
  model + `work_schedule` test, `68021102` GameView planned geometry, shadow
  check, shadow transform and body copy passes + `TAK_RENDER_SCHEDULE=uniform`
  / `TAK_RENDER_POOL_THREADS` / `TAK_SCHEDULE_*` / `TAK_SCHEDULE_VERIFY` /
  `TAK_SCHEDULE_SAMPLES` + telemetry + `render_schedule` ctest, `590db308`
  hit estimate without vector headers, no copy cap, dense cost copy +
  `addUnits` (both regressions found by measurement), then the docs commit).
  Tests: `work_schedule` (ASan/UBSan, TSan; ten header mutations fail it),
  `render_telemetry`, `render_schedule` (four in-game mutations fail it);
  `ctest -L quick` 386/386 at `68021102` and again at `590db308` (final
  code, all targets rebuilt). Captures identical (I2: 180 comparisons, I4: 204, each
  incl. serial vs parallel). Release `takclient` builds without the new
  debug strings. `--mpai` not run: only `src/client` (presentation) and
  `tools/` changed; every benchmark run checks the world hash (identical A vs
  B in every scene). Measurements: Experiment 11. Baseline for later
  comparisons: `~/tak-tmp/render-opt/takclient-I4-candidate` (code of
  `590db308`).
- **Workstream J (CPU cache budgets) is implemented and verified** (commits
  `dac6e399` pinned section handles + consumer audit + `terrain_compositor`
  test, `469f743a` decoded-section budget, `8cb58b59` mip budget + idle trim,
  `2cb7cdf5` retained-buffer and worker-scratch trims + CPU accounting +
  `cpu_budget`/`render_cpu_budget` tests, `3dc315c4` working-set rule (a
  measured fix of the pure LRU budget) + `bigmap-zoom` scene, then the
  release-build fix and the docs commit). Lifetimes were fixed and every
  consumer converted before any eviction was enabled. Tests:
  `terrain_compositor` (ASan/UBSan, TSan; four mutations fail it),
  `terrain_cache` (budgeted = unbounded texel for texel), `cpu_budget` (five
  mutations), `work_schedule` (participant indices; TSan; two mutations),
  `render_cpu_budget` (in game); `ctest -L quick` 388/388; the J tests
  re-run on the final code. Captures: identical except terrain-readiness
  races that A/A and B/B pairs reproduce in both builds. Timing: nothing
  material (Experiment 12). Release `takclient` builds without the J debug
  strings. `--mpai` Retail 60 s = `2c39430636a3dbda`. Baseline for later
  comparisons: `~/tak-tmp/render-opt/takclient-J3-candidate` (code of
  `3dc315c4`; the release-build fix only moves a struct declaration).
- **Workstream K (camera-prioritized terrain) is implemented and verified**
  (commits `4c7a018d` harness + readiness gauges + terrain scenes, `01cd7a01`
  policy `client/terrainwork.h` + `terrain_work` test, `85bbb1eb` TerrainChunks
  integration (priority, cancellation with keep band and level grace,
  stand-ins last, off-view room, calm gate, upload budget) + `terrain_schedule`
  test, `61be4a96` sections join the frame budget, `5eebec8b` settle harness,
  `b98183b1` no per-frame passes when nothing is missing, `df330946` docs;
  then the verifier fixes `918300a4` (calm gate scope, `readyHeld_` stored
  under the lock, holes-first test), `b96fe8fb` (jump storms gated at every
  level), `6cf541b0` (a storm lasts until 1 s without a jump) and the docs
  commit). The verifier rejected `df330946`: the calm gate, timed only at
  zoom 1, made zoomed-out fast pans 1.4× / 3.2× slower; a lost wakeup was
  possible; holes-first ordering was untested. All three are fixed
  (Experiment 13c). The gate now holds new work for fast motion only at
  level 0 and for jump storms at every level. Tests: `terrain_work` (816
  checks), `terrain_schedule` (scenarios 1–9 incl. 8b gate scope, 8c level-1
  storm, 9 holes first; mutations of the gate scope and of holes-first
  ordering each fail it), `terrain_cache` (re-run at `6cf541b0`), TSan on the integration
  (scenarios 1–7, at `b98183b1`), `ctest -L quick` 389/389 on build-o2 at
  `6cf541b0`; the Release `takclient` builds without the K debug or
  telemetry strings. Captures: settled captures identical (Experiment 13 at
  K4; Experiment 13c K0 vs K8 at zoom 0.3/0.4: 48 of 48 frames identical).
  Timing (Experiment 13 + 13c): no material frame-time regression in the
  sweeps at zoom 0.3/0.5/0.75/1.0 at either size; jump storms at 7680×2160
  −5% to −49% total mean (material). Recorded trade-offs: jump storms at
  1280×960 draw +10–40% (+0.02–0.11 ms, total within noise; K0's FIFO caches
  all five revisited views there, K8 composes nothing during a storm);
  terrain-zoom-osc 7680×2160 total p95 +31.8% (13b); still-view `t_terrain`
  +6 µs. `--mpai` not run: only `src/client`, `tools/` and CMake changed;
  every benchmark run checks the world hash (identical A vs B). Baseline for
  later comparisons: `~/tak-tmp/render-opt/takclient-K8-candidate` (code of
  `6cf541b0`); K0 (`takclient-K0-baseline`, `4c7a018d`) = J + the K harness.
- **Workstream L (unit spatial index, compact records) is implemented and
  verified** (commits `63aa38f4` `UnitGrid` + `anchorBandToWorld` +
  `unit_index` test, `11753fdb` painter candidates from `Frame::unitGrid`
  (built by `captureFrame`) + `TAK_UNIT_FULL_SCAN` / `TAK_UNIT_INDEX_VERIFY` /
  `TAK_RENDER_SHAKE` + telemetry + `render_unit_index[_jump]` ctests,
  `895e629a` per-unit overlays through the grid + `unitpass::*` shared pass
  geometry + `TAK_RENDER_OVERLAYS`, `74bb3556` `UnitTypeTable` (dense
  registration record) + whole-list answers, `a42da929` queued-build ghosts
  from `Frame::buildGhostUnits`, `e90c79a0` release-build fix + report keys,
  then the docs commit). Tests: `unit_index` (28 of 33 header mutations fail
  it; the 5 that pass keep the bands conservative), `render_unit_index` /
  `render_unit_index_jump` (costed, not `quick`; a band mutation and a lift
  mutation fail them), `spectator_health_bars`, `tactical_dots`,
  `render_telemetry`, `render_benchmark_tool`; `ctest -L quick` 390/390 on
  build-o2 at the final code. Release `takclient` builds and contains none of
  the L debug or telemetry strings. `--mpai` Retail 60 s Inner Circle seed 1 =
  `2c39430636a3dbda` (unchanged; run because `captureFrame` changed).
  Captures identical (84 frames, Experiment 14). Measured (Experiment 14):
  `total10k-subset` draw −39.4% / −30.8% (material), `total10k-visible`
  unchanged within noise (the index cannot help; the record halves
  `t_candidates`), small-battle / jump within noise. Baseline for later
  comparisons: `~/tak-tmp/render-opt/takclient-L4-candidate` (code of
  `a42da929`; `e90c79a0` changes only release-build declarations).
- **Workstream M (indexed structure palette) is implemented and verified**
  (commits `b3cafd0d` layout + indexed form + GL program + native pass +
  `palette_sheets` / `palette_indexed` tests, `5e3f1bfa` GameView integration
  (shared banks, one form at a time, native body spans, telemetry, debug
  knobs) + `render_palette_indexed` ctest, `cf7eebd3` `structures` scene,
  `92d54e0d` + `789126df` release-build fix, `3c9becf7` ctest cost,
  `ab9def88` special units' runs in one pass, `49fc8541` docs; verifier
  fixes `502e45d1` + `fc13a7fb`, then the 15b docs commit). The verifier
  rejected `49fc8541` for:
  - a **release-only defect**: the distant-image run drawer was installed
    in a debug-only block, so release bakes drew indexed runs grey. Now it
    is installed in `updatePaletteMode()` in every build (confirmed in the
    release disassembly), and `TAK_REQUIRE_PALETTE=bakes` is the GL check;
  - a wrong coverage claim (pan-distant bakes no indexed run; structures at
    zoom 0.4 does);
  - a stale limitation;
  - `TAK_BODY_SDL_SUBMIT` still drawing natively (it now selects baked);
  - the out-of-sheet pixel class awaiting a ruling. That class is
    **eliminated**: owner texels let the shader decode a neighbouring
    sheet's texel exactly like the baked page.

  The sheet layout is held baked (pre-M pages, every renderer, bilinear
  units, any failure) or indexed (SDL `opengl`): full roster 144 → 9.26 MiB
  GPU, uploads 130.8 → 6.7 MiB, sources 39.4 → 6.6 MiB CPU. Captures are
  pixel-identical to L4 in structures (also at zoom 0.4, with indexed runs in
  distant bakes), stationary-mixed, pan-distant and effects at 1280×960 and
  7680×2160 (Experiment 15b).

  Timing (Experiment 15, vs L4): stationary-mixed 7680×2160 draw −20 %
  (significant; from the native spans), structures 1280×960 −5.6 %
  (significant), others within noise. Recorded cost: effects 7680×2160
  `t_body_submit` +0.08 ms (+11 %, significant), with draw and total within
  noise. M5's owner path vs M4: no material change in structures or
  stationary-mixed at either size (15b).

  Tests: `palette_sheets`, `palette_indexed` (GL + software), `geometrysubmit`
  (both), `render_palette_indexed` (software ctest; GL by hand, incl. the
  zoom-0.4 `bakes` study), `distant_models`, `ctest -L quick` 392/392 at
  `fc13a7fb`. The Release build has none of the M debug strings. `--mpai`
  not run (presentation only; world hashes identical in every run).

  Baseline for later comparisons: `~/tak-tmp/render-opt/takclient-M5-candidate`
  (code of `fc13a7fb`).

  Not validated: non-GL backends take the baked form (pre-M output; captures
  identical with `TAK_PALETTE_BAKED`); Windows/macOS not run; GL on Mesa not
  run; the no-owner case assumes never-written baked page memory reads zero
  (true on NVIDIA).
- Workstream N: not started.
- **Next concrete step:** workstream N (native pass submission). M left a
  working, scoped native pass (`GeometrySubmit::beginPass/passDraw/
  passDrawBuffer/endPass` + `PaletteIndexedGL` pass state: SDL-identical
  plain program on unit 4, premultiplied blend switching, state restored
  once) that opens only at indexed palette runs. N can open it for every run
  of body commands (removing the per-command priming of ≥512-vertex
  commands, the source of M's stationary-mixed 7680×2160 gain) and decide
  whether features (sprite copies) can join; that would also remove M's
  effects 7680×2160 span cost. M follow-ups (not required for N):
  distant-image bakes still prime per indexed run; indexed bilinear stays
  experimental.
- (Superseded, kept for the record) Former next step, workstream M (indexed palette path): inspect
  `preparePaletteTexture` / `paletteTextureSources_` / `paletteTextureSheets_`
  and the shade tables; a capability-gated indexed-texture path with the baked
  RGBA pages as the lazy fallback. L follow-ups (not required for M): the
  unit-effect pass (`drawUnitFx`, 0.45 ms over `anims_` with 10,000 units —
  needs an order-preserving design or an accepted order change) and the
  minimap dots (0.39 ms, every unit drawn); splitting the grid build over
  `capturePool_` if the capture thread becomes the bottleneck.
- (Superseded, kept for the record) Former next step: workstream L (renderer-owned spatial index for unit
  candidates). Today every frame scans all of `front().live` with fog and
  frustum tests (`gameview_render.cpp`, candidate selection); features
  already use `FeatureIndex` with a `TAK_FEATURE_FULL_SCAN` reference. Start
  from `total10k-subset` (10,000 units, small visible subset) and keep
  `total10k-visible` as the case the index cannot help. K follow-ups (not
  required for L): whether a jump storm should be gated only when its views
  cannot all stay cached (the 1280×960 storm cost, Experiment 13c); the driver stall on some chunk uploads (cause unknown;
  splitting uploads into strips over frames not tried); the level rule that
  lets a 7680×2160 view at zoom just above 0.5 need more chunks than the
  256 MiB budget (pixels would change); whether stand-in retention should
  yield to the shadow atlas under very small GPU caps.
- (Superseded, kept for the record) Former next step, workstream K: J left
  what K needs: chunk jobs pin their inputs (cancellation is lifetime-safe:
  a cancelled job only drops pins), the decode worker already has demand vs
  speculative requests, and `TerrainChunks::prepare` gets the owner's clock.
  J follow-ups (still open, not required for L): whether to call
  `malloc_trim` after large trims (RSS, unmeasured); the possible small
  positive draw bias in Experiment 12 (inside noise).
- (Superseded, kept for the record) Former next step, workstream J (CPU cache budgets): fix the
  reference lifetimes of `terrain::Compositor::sectionImage` consumers
  (terrain chunk worker, `MapView`, minimap `renderBlock`, Cartographer
  `validation.cpp`) before any eviction; then budgets/trimming for decoded
  sections, `TerrainChunks::mips_`, `bodyVerts_` and the shadow scratch
  (none is trimmed today; Experiment 10b). I follow-ups (not required for J):
  measure the pool under a concurrently running simulation (a real game:
  sim worker + `capturePool_`; the render study runs the sim inline) and
  decide whether to reserve CPUs for it; the COB VM tick still uses uniform
  `parallelFor` (minParallel 1500) — planning it by VM count was not
  measured.
- (Superseded, kept for the record) Former next step, workstream I: the
  geometry pass still dispatches the whole pool for ≥ 32 visible units
  whether they are hits or misses (Experiment 3: 1.3–2.7 ms dispatch, 40–70 ms
  summed wake latency under the 8-CPU pin), and H's per-frame bookkeeping
  (planner add/plan/end, ~0.05 ms on a streaming pan) and the parallel copy
  pass are candidates for cost-based thresholds. H follow-ups (not required
  for I): small commands (< 512 vertices; 856 per frame in stationary-mixed
  1280×960) stay on SDL's path because priming a native draw costs a flush —
  workstream N's native passes are what could move them into the arena
  (with per-draw priming `TAK_BODY_ARENA_MIN=1` is +45–65% draw, rejected); the forced-completion present cost in
  the GPU-bound dense 7680×2160 view (Known limitations).
- (Superseded, kept for the record) Former next step, workstream H (less full-scene vertex copying):
  trace `UnitGeom::verts` → `bodyVerts_` → GeometrySubmit client arrays,
  introduce ordered commands (geometry range + material + placement + clip /
  layer) with the assembled SDL path as fallback. Measure H against G2
  (`~/tak-tmp/render-opt/takclient-G2-candidate`). G's follow-ups (not
  required for H): a pixel-grid bake would make distant images equal to live
  geometry (Experiment 9 investigation; changes appearance, needs a decision);
  ~1700 bodies per frame stay live in the 10k view because four image pages
  are full (limit unchanged by design).
- (Superseded, kept for the record) Former next step, workstream G: F is
  done; its follow-ups (not required for G): the dense
  7680×2160 scene still exceeds four pages and takes the whole-frame direct
  path (Experiment 8), and the raster's sub-pixel dependence on the cell
  position (see Known limitations) means any future relocation must redraw.
  Measure G against F8 (`~/tak-tmp/render-opt/takclient-F8-candidate`,
  code of `ce2a3466`). Captures: compare G only with builds that carry F
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
| F | Persistent shadow allocation | **verified** | Workstream F (`3565f3fe` allocator + `shadow_atlas` test, `5febc00f` GameView integration + `render_shadow_atlas[_zoom]` ctests, `6c7b93df` cheap failure path + page-ordered best fit, `ec3df732` / `fbe21589` / `3fd2c8de` all-redraw frames use the former packer, `ce2a3466` page textures created only after a frame fits — the verifier's churn finding). Each unit's silhouette tile keeps a cell of `ShadowAtlasAllocator` (`client/shadowatlas.h`: logical pages → shelves with free span lists, class-height shelves, exact widths, generational handles, occupancy/fragmentation statistics, `validate()`) across frames; `UnitGeom::shadowTile` holds the handle, validated against the cell's recorded entry handle. E's stamp is kept per cell and carried when a cell is reallocated (verdict `Relocated`, never a stranger's pixels); the stamp gained the page. Policy: keep while the tile fits (cell ≤ 1.5× a fresh one); absent cells retained 2 s / 30 frames, evicted first under pressure; forced repack (the former packer, class heights then exact sizes = former slots, so the atlas falls back only where the former code did) for a failed allocation in a fragmented atlas, ≤ once per 60 frames; free repack when ≥ 90% of tiles are redrawn anyway and the atlas is loose; frames where every tile is redrawn anyway (zoom, sub-pixel pan, new phase) use the former packer with exact sizes (the former slots and pixels); bounded migration (≤ 64 tiles/frame) off a sparse last page; empty pages destroyed after 2 s / 60 frames; page size shrinks after 5 s / 120 frames of low demand; more tile area than four pages falls back at once; new pages are placed logically and their textures created only once every tile of the frame is placed, so a frame that fits four pages by area but not by packing creates no texture (no create/destroy churn while it keeps falling back; Experiment 8b). 4 pages / 2048² / VRAM budget and the whole-frame direct fallback unchanged. Recycled cells cleared with their padding. Captures: identical to E3 in all-redraw scenes (churn, pan, zoom) and whenever slots match (`TAK_SHADOW_REPACK`, 96 comparisons); with persistent cells 1–14 single shadow-edge pixels per frame differ (208 pixels in 49 of 156 frames) — the former packer shows the same differences when only its slots change (raster rounding depends on the cell position; Experiment 8). Measured on the final code (Experiment 8b, vs E3): draw −33% / −50% / −33% in stationary-mixed / total10k-visible / moving at 1280×960 (material), −12% in pan-snap 1280×960 and −18% to −32% in the reuse scenes at 7680×2160 (every ABBA group faster, inside that noisier session's thresholds; material in the F3 session, Experiment 8), shadow packing −66% to −91% (material except pan-snap), p99 −58% / −61% in stationary/dense 1280×960; band fix: 30 → 0 page creations per run, frame time unchanged; all-redraw scenes +0.14–0.20 ms `t_shadow_alloc`, draw −0.3% to +3.0% (not material); dense 7680×2160 fallback −27% shadow phase and 64 MiB released. Memory: tracked GPU equal or lower at the end of every timed scene; one extra page (16 MiB) during the battle-sparse battle, whole atlas freed after the view empties. |
| G | Distant image lookup before generation | **verified** | Workstream G (`6d56ac8d` cache: retained bounds, decide→bake-by-page→quads, page occupancy + release, same-position migration, `holds()`; `b59cdd8a` GameView: lookup before body generation, deferred body stages, catch-up, verifier, `render_distant_lookup`; `b084644e` scenes; `2d755167` per-frame switch read, no empty catch-up scan). The geometry workers ask the cache whether a unit's image will be shown without a repaint (`DistantModels::holds`: valid, same zoom/output scale, baked < 67 ms ago — the case where `prepare()` reads neither revision nor vertices); then the BODY stages stay pending on the entry (`UnitGeom::bodyPending`) while the shadow stages run and the revision advances; a pending body is generated as soon as anything needs it (next non-held build, a bake via callback, a catch-up). Exact eligibility: deferred only for appearance-only local changes (`bodyVertexCountStable`; the body collector culls back faces, so pose changes the ≥ 96 vertex count). Bounds/opacity retained per identity + revision + source + count, so refused bodies are not rescanned every frame; bakes run grouped by page after painter-order decisions; per-page occupancy, pages released when their last image expires (others live), tile POSITIONS kept in the pre-G order on the lowest live page (pixels and quads depend only on the position), bounded same-position migration. Cadence, 128 bake budget, eligibility, live fallback, 4-page limit unchanged. Tests: `distant_models` (vs the pre-G cache kept verbatim, 900 frames, decisions/quads/tile pixels identical, `validate()` every frame; five mutations fail it), `render_distant_lookup` (6,902 deferred bodies regenerated and matched; shadow-skip mutation fails it), `geometry_stages`, GL verifier runs. Captures identical to F8 (168 comparisons). Measured (Experiment 9): total10k-visible 1280 draw −12.4% and GPU timeline −8.5% (material), `t_distant` −67% / −83% (10k / stationary, material), pan-distant worker build −15% (material); 7680×2160 unchanged (no images there); distant-sparse −32 MiB tracked GPU after the view moves. Investigation of the failed larger-image prototype: today's images differ from live geometry by ~87k px/frame (bilinear resample at fractional phase); a pixel-grid bake cuts that to 44 px (uncommitted experiment). |
| H | Less full-scene vertex copying | **verified** | Workstream H (`97e42721` planner + GL buffer + `drawBuffer`, `8d96905f` ordered commands + arena integration, `37ecd884` staged writes (mapping rejected), `d8744efe` rollover with two storages, `e311cae5` release-build fix; after the verifier's rejection `0cbc4401` bounded `bodyVerts_` growth, double copies counted, write-failure fallback fixed + `render_body_arena_fail`). Vertex journey traced (see "Retained body vertices"): before H every visible body was copied twice per frame (per-unit verts → `bodyVerts_` → driver client-array copy, or SDL's conversion for batches < 512). Now the body pass is ordered commands (texture + ranges of per-unit sources; features, special units and the ground/air boundary as boundaries; order and state as before). On the `opengl` backend, commands of native size draw from a bounded retained arena (`client/vertexarena.h`): sources keyed by entry handle + `revision` (quads by value) keep their range; changed ones are copied whole (staged in `bodyVerts_`' tail, one `glBufferSubData`; their runs in small assembled commands are copied twice, `arena_verts_twice`, 0.4% of copies in a steady dense frame) into storage no pending draw reads; a full storage drains while a new one fills (rollover, budgeted migration of whole commands), so no frame rewrites everything; one native draw per command (`glMultiDrawArrays` over merged ranges). Frames with mostly new content (camera motion) stream exactly as before; the assembled SDL path stays the portable fallback (other backends, fractional scale, texture checks, failures). GL 1.5 buffer objects only; no fences, persistent mapping or unsynchronized writes. Every storage counted in gpuvram incl. 3 frames in flight; ≤ 3/4 of the GPU cap, 64 MiB per storage; released after 2 s idle, under GPU pressure, on resets, on failures (then 120 frames of streaming); context loss forgets the buffers. Tests: `vertex_arena` (simulated buffer, rollover scenario, 12 mutations fail it, ASan/UBSan), `geometrysubmit` (+72 GL arena-vs-client-arrays-vs-SDL comparisons), `render_body_arena` / `_still` / `_fail` (software renderer, simulated storage, every assembled command checked too, injected write failures; a constant-revision mutation fails the first, a gap-leaving fallback the last), GL verifier runs (`TAK_BODY_ARENA_VERIFY`). Captures identical to G2 (Experiments 10, 10b). Measured (Experiment 10; "x/y" = Experiment 10 / 10b): body vertices copied −91% / −94% and bytes handed to GL −91% in the dense / stationary 7680×2160 views; body submit −26% to −37% in total10k-visible (both sizes) and stationary-mixed 7680×2160 (material), only −8.6% / −9.8% in stationary-mixed 1280×960 (most of its body vertices stay in small, assembled commands); stationary-mixed 7680×2160 draw −26/−28%, total −24.5/−25%, GPU timeline −28.5/−31% (material); total10k-visible pipelined 7680×2160 total −7.8/−6.7%, GPU −9.9/−9.1% (material), 1280×960 draw −12.6%, GPU −17.8/−20.9% (material in 10b); pan/moving neutral. Memory: tracked GPU +1.6 to +64 MiB (the arena); `bodyVerts_` capacity +5.9% over the pre-H buffer in the retained scenes (growth slack 1/16; peak need +0.5% from the double copies) and −43% in moving (pre-H doubled there) — H4 had doubled it to 2× the need (Experiment 10b). Limitation: forced-completion total10k-visible 7680×2160 frame total +3.9% (material in Experiment 10, +3.6% not material in 10b; present waits ~1.8 ms longer; pipelined frames −7.8%). |
| I | Cost-based work scheduling | **verified** | Workstream I (`a5ba14e4` pool + `WorkPlan` + `work_schedule` test, `68021102` planned GameView passes + `TAK_SCHEDULE_VERIFY` + `render_schedule` ctest + telemetry, `590db308` cheaper plans + no copy cap, both measured fixes). Before: `ThreadPool` of `hardware_concurrency` threads, every pass woke every worker (`notify_all`) in fixed 8-item chunks above an item-count threshold and the caller waited for every worker, including those that woke after the work was gone. Now (`client/threadpool.h`): sized by the process's CPU affinity (SimPool's rule); a pass posts only the workers it asks for (one condition variable per worker); participants claim chunks off one atomic word and the caller waits for the CHUNKS, so late wakers never delay it and never touch a finished pass (job fields read only by a thread holding an unfinished chunk). `WorkPlan` (`client/workschedule.h`) cuts contiguous chunks of ~10 µs estimated work, one participant per 40 µs, serial below 30 µs; estimates calibrated on serial per-unit / per-pass timings: geometry from each entry's previous build (key words, stages, body vertices, shadow points; 16-bit `UnitGeom::nextCost`, a dense copy taken at claim), shadow point checks by points, shadow tile transform by points over the page's dirty tiles only, body copy by bytes + per range. Every item writes only its own outputs, so results are independent of the plan and of completion order (deterministic output; captures identical, serial reference identical). Tests: `work_schedule` (ASan/UBSan, TSan; ten mutations fail it), `render_schedule` (four in-game mutations fail it), `render_telemetry`. Measured (Experiment 11, vs H5): pool dispatch −87% to −99%, caller wait −44% to −96%, wake latency −89% to −99%; stationary-mixed 7680×2160 draw −11.4% and total p99 −13.8%, total10k-visible 1280×960 total p95 −19.4% / p99 −13.8% (material); other draw deltas −1.3% to −7.6% inside noise; nothing regressed materially. Planning alone (same binary vs uniform passes) is neutral on frame time at both sizes (−5.4% to +2.1%, inside noise) and halves woken workers on small passes; the gain comes from the pool mechanics. Not measured: competition with a concurrently running simulation (the study runs it inline); non-Linux pool sizing (no affinity query). |
| J | CPU cache budgets | **verified** | Workstream J (`dac6e399` pinned section handles + consumer audit, `469f743a` section budget, `8cb58b59` mip budget + idle trim, `2cb7cdf5` retained-buffer and worker-scratch trims + CPU accounting, `3dc315c4` working-set rule, a measured fix of pure LRU). **Lifetimes first:** `Compositor::sectionImage` returned a reference valid until `clear()`; it now returns a `SectionRef` (shared, immutable) that pins the image. Every consumer was audited and converted before eviction was enabled: the chunk worker pins per job, MapView per upload, renderBlock (minimap crunch, Cartographer) per copy, validators/tools transiently. The decode runs outside the cache lock with in-flight dedup. **Sections:** 128/112 MiB budget over unpinned sections cold for 10 s; LRU; prefetch only into free budget; the render thread never decodes in play; `trim()` per job and per frame. 97.7% of the 1371 shipped maps stay under budget (median 21 MiB, Ulasem 64, max 402) and behave as before. **Mips:** worker-owned LRU, 64/56 MiB, idle trim after 30 s + 120 frames posted by the render thread and done by the worker. **Buffers:** `TrimTracker` (two half-window buckets, 5 s and 120 frames; capacity > 2× window peak, floor 64 KiB; to peak + 1/16; regrowth backoff up to 8×; never per frame) over the body buffer, frame lists, shadow scratch and F's cell stamps, model scratch, and the per-participant worker scratch (`ThreadPool::participant`; trimmed by the render thread between passes). **Accounting:** used/retained/pinned/evictable/peak per component and totals (`cpu_cache_*`), eviction/redecode/regeneration/trim counts; GPU and RSS separate, exclusions listed. Tests: `terrain_compositor` (ASan/UBSan, TSan; mutations fail it), `terrain_cache` (budgeted = unbounded texel for texel), `cpu_budget`, `work_schedule` (participants; TSan), `render_cpu_budget`. Measured (Experiment 12): no material frame-time change in any timed scene; battle-sparse releases 19 MiB 300 frames after the view empties (the trim frame is not measurably slower); total10k-visible releases 81 MiB of first-frame capacity at frame 450 at 1280×960 (no regrowth); bigmap-zoom ends at 146 vs 259 MiB retained CPU (37 redecodes at 1280×960), the peak unchanged; captures identical except terrain-chunk readiness races right after camera changes, which A/A and B/B pairs reproduce in both builds (Experiment 12). |
| K | Camera-prioritized terrain | **verified** | Workstream K (`4c7a018d` harness: sweep / zoom-oscillation / jump-list camera paths, `terrain-sweep` / `terrain-zoom-osc` / `terrain-jumps` scenes, readiness gauges, `TAK_TERRAIN_SETTLE`; `01cd7a01` policy `client/terrainwork.h` + `terrain_work` test; `85bbb1eb` TerrainChunks integration + calm gate + `terrain_schedule` test; `61be4a96` sections join the frame budget; `5eebec8b` settle harness; `b98183b1` no per-frame passes when nothing is missing, a measured fix; verifier fixes `918300a4` gate scope + `readyHeld_` under the lock + holes-first test, `b96fe8fb` storms gated at every level, `6cf541b0` storms last 1 s). Before: FIFO queue of ≤64 visible-cell jobs, no cancellation outside clear/invalidate, completed off-view results dropped at upload, separate caps (8 MiB chunks, 32 sections), plain LRU. Now: the worker takes the most urgent queued job (holes > upgrades over a stand-in > kept work; centre first); pending work is re-ranked every frame and obsolete jobs cancelled (outside a 2-cell keep band, or a level the zoom left past a 1 s / 30-frame grace) — cooperative for running jobs, lifetime-safe through J's pins; a full queue gives kept slots to visible work; stand-ins of visible cells are evicted last; off-view results only use free or cold room; one per-frame upload budget for chunk images and fallback sections (holes first, 32 MiB / 6 ms est. urgent, 2 ms est. for the rest, measured CPU submission cost, no GPU sync, first upload always proceeds, finishChunks unlimited); a calm gate queues no new work during fast motion at level 0 (the fallback draws those cells texel-identically; uploading a unique 4 MiB chunk per passing cell cost more — K3a, rejected) or during jump storms at any level (until 1 s without a jump); not for motion from level 1, where chunks are small and the fallback dear (gating there made zoomed-out fast pans 1.4×/3.2× slower — K5 scope, rejected by the verifier, Experiment 13c); leading-edge prefetch implemented but opt-in (GPU +56–80 MiB in pans, no gain). Tests: `terrain_work` (816 checks, nine + four mutations), `terrain_schedule` (scenarios 1–9; nine in-game mutations incl. gate scope and holes-first ordering), `terrain_cache`, TSan on the integration (scenarios 1–7). Settled captures identical in all comparable frames; normal captures differ only by terrain readiness (Experiment 13). Measured (Experiment 13, 13c): no material frame-time change in any of 9 scenes × 2 sizes nor in terrain-sweep at zoom 0.3/0.5/0.75/1.0; terrain-jumps 7680×2160 total −28% / −49% / −4.9% (zoom 0.4 / 0.3 / 0.75, material); wasted worker compose −80% to −100% in the terrain scenes, −83% bigmap-zoom 7680×2160; terrain-zoom-osc 7680×2160 hole frames 93% → 37%, `t_terrain` −28% (p99 −68%), total p99 −10%; constrained `t_terrain` −10/−12% (material). Costs (recorded, not hidden): terrain-jumps 1280×960 draw +25% / +40% / +10% (+0.02–0.11 ms, total within noise: K0 caches all five revisited views there); terrain-zoom-osc 7680×2160 total p95 +4.7% (Exp. 13) / +31.8% material in the quieter 13b session — K uploads ~1 chunk per frame where the stale FIFO dropped them; still views +6 µs `t_terrain` at 7680×2160 (material for the phase, frame time unchanged); under the debug 128 MiB cap stand-in retention leaves the shadow atlas without its page in 28 more of 600 frames (existing fallback). Calm-gate-less version rejected (K3a); nearby prefetch opt-in. |
| L | Unit spatial index | **verified** | Workstream L (`63aa38f4` grid + band-to-world + `unit_index` test, `11753fdb` painter candidates from the snapshot's grid + verifier + `render_unit_index[_jump]`, `895e629a` per-unit overlays + shared pass geometry `unitpass::*`, `74bb3556` dense registration record `UnitTypeTable` + whole-list answers, `a42da929` queued-build ghosts from a snapshot list, `e90c79a0` release fix + report keys). Before: the painter pass, four per-unit overlays and the ghost pass each walked every live unit per frame. Now `captureFrame` builds `Frame::unitGrid` (snapshot-owned, 256-px cells, counting sort, ascending live order) from the positions it copies; `unitCandidates(band)` maps a pass's screen band for the lifted anchor to a world rectangle (lift bounded per map by the highest height above the datum, 1 world px + 1 screen px slack) and returns a superset in live order (bitset, no sort; whole list when the view holds every unit); every pass keeps its exact test, so items, keys, equal-depth order and overlay order are a full scan's. Interpolation, altitude and extents are not inputs of these culls (their margins stand for them); shake is in the offset; fog stays in the exact test. Compact records: `UnitTypeTable` (dense registration flag mirroring the map; `t_candidates` −50% with 10,000 visible) and `Frame::buildGhostUnits`; a visual record was rejected (visual names change in place). Tests: `unit_index` (grid vs brute force; every pass's band vs its exact float test at ulp-located edges; movement/churn; the table; covers; 28 of 33 header mutations fail it, the 5 that pass only enlarge a band or shrink it by less than the slack, i.e. stay conservative), `render_unit_index` / `_jump` (in game, `TAK_UNIT_INDEX_VERIFY`: painter list, all four overlays and ghosts equal a full scan every frame; shake, flyers, corpses, construction, fog, churn, epochs; two in-game mutations fail them). Captures identical (84 frames). Measured (Experiment 14): `total10k-subset` draw −39.4% / −30.8% (1280×960 / 7680×2160, material), p95 −37.5% / −29.6%, `t_candidates` 0.55 → 0.03 ms; `total10k-visible` draw −2.7% / −1.4% (not material — the index cannot help there), `t_candidates` −52% / −50% (material, the record); small-battle and jump unchanged within noise. Costs: grid build 0.07 ms per snapshot per 10,000 units on the capture thread; ≈ 0.55 MiB of grids over the 12-slot ring at 10,000 units. `--mpai` Retail 60 s unchanged. |
| M | Indexed palette path | **verified** (Experiment 15; verifier fixes in Experiment 15b) | Workstream M (`b3cafd0d` layout + indexed form + GL program + native pass + `palette_sheets` / `palette_indexed` tests, `5e3f1bfa` GameView integration + native body spans + `render_palette_indexed` ctest, `cf7eebd3` `structures` scene, `92d54e0d`/`789126df` release-build fix, `ab9def88` special units in one pass, `502e45d1` run drawer in every build + `TAK_BODY_SDL_SUBMIT` → baked, `fc13a7fb` owner texels: exact out-of-sheet decode). The sheet layout (the former packer, `client/palettesheets.h`) is held ONE way: indexed on SDL `opengl` (a 512² RGBA (index, owner, alpha) page per layout page, a LUT, an identity texture; sheet id in the texture coordinates; the GLSL program resolves palette[shade[level][index]] at the texel the texture unit picks, through the owner's entry when that texel is a neighbouring sheet's), baked 2048² RGBA pages elsewhere (other renderers, bilinear unit filtering, any failure; lazily rebuilt from the CPU sources, never kept beside the indexed form). Full structure roster: 144 → 9.26 MiB GPU, sheet uploads 130.8 → 6.7 MiB; sources 39.4 → 6.6 MiB CPU (shared banks, 2 B/texel). Indexed runs open native spans in the body replay (one priming for the following body commands): stationary-mixed 7680×2160 draw −20 %. Captures pixel-identical to pre-M in structures (incl. zoom 0.4 distant bakes), stationary-mixed, pan-distant and effects at both sizes; bilinear stays baked (experimental indexed linear not bit-identical, max error 3). |
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
  is a cheap branch (since I: `TAK_RENDER_SCHEDULE=uniform`,
  `TAK_RENDER_POOL_THREADS=N`).
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
| Terrain | `MapView::draw` → `TerrainChunks::prepare/draw`, fallback tile batch, sections uploaded on demand; since K `prepare` re-ranks/cancels pending work by camera demand (`terrainwork.h`), uploads hole fills first under one frame budget shared with the sections, and `uploadDeferred` runs after the fallback | chunk composition on its own worker thread (takes the most urgent job since K); section JPEG decode on a second worker; uploads on the render thread |
| Visibility / candidates | `draw` `:144`–`:230`: `FeatureIndex::query` broad phase + exact screen/fog cull for features; units (since L): `Frame::unitGrid` broad phase (`unitCandidates`, built by `captureFrame`) + the unchanged exact registration/fog/frustum test, in live order; the four per-unit overlays query the same grid with their own bands; queued-build ghosts read `Frame::buildGhostUnits` | grid built on the capture thread (sim worker or inline), queried on the render thread |
| Painter list | `paintItems_` stable sort by (layer, z) (`:233`); air units layer 1 | render thread |
| Entry claim (since C) | `claimGeometryEntries` (`gameview_render.cpp`): each painter-ordered unit claims the entry its identity owns in `geomEntries_` (`StableEntryPool<UnitGeom>`, `client/geometryowners.h`); `visUnits_` / `visGeom_` (ordered entry pointers) / `geomIndex_`; then `reclaim` (destroyed, expired, budget, trim) | render thread, before the parallel pass |
| Atlas maintenance | `atlasFor` per visible colour slot (`:272`), `preparePaletteTextures` for structures (`:284`), `animateGlowTextures` (`:289`) | render thread, render-target draws |
| Geometry preparation | `pool_` pass planned by estimated cost (since I; `parallelFor` before) → `buildUnitGeom`: three exact stage keys (since D), then `runUnitGeomStages` redoes only the changed stages — LOCAL (`preparePiece`, `collect`, index sort, local shadow), PLACEMENT (`placeBody`/shadow placement, bounds), MATERIAL (vertex colours) | `ThreadPool pool_`, thread-local scratch, writes only its unit's entry `*visGeom_[i]` |
| Shadow preparation | `buildUnitShadow` inside the LOCAL stage (local points retained); the placement stage re-places them | positions-only opaque verts + masked runs |
| Distant images | lookup `holds()` inside the geometry workers (since G: a held image's body stays pending), then `distantModelCache_.prepare` (`distantmodels.cpp`): decide in painter order, bake grouped by page, quads in painter order; catch-up of deferred bodies not shown | workers read-only; render thread bakes |
| Shadow atlas | `buildShadowSilhouetteAtlas` (`:377`, called `:656`): pack, dirty tiles, parallel transform, `OpaqueShadowSubmit` + mask batches | render thread + pool for the point checks and the transform (since I planned by points, dirty tiles of the page only) |
| Shadow composite | ground: `drawShadowBatch` (`:767`, `:2593`); air: drained at the ground→air boundary inside the body replay | render thread |
| Body assembly | plan `drawOps_`/`copyTasks_` serially (`:782`), parallel copy into `bodyVerts_` | pool (since I planned by bytes + per range) |
| Submission | replay `drawOps_` (`:875`): features (sprite copies, sway geometry, flames, smoke), `drawUnit` for special units (`:3044`, clip rects + interleaved particles), batched bodies via `bodySubmit_` (`:1037`) | render thread |
| Effects | dots, build ghosts, flames, storms, beams/shot models (`drawShotModel` runs `collect` live per projectile), projectiles, particles/effects/unit fx (`:1514`), fog (`drawFog`, `:3965`) | render thread |
| HUD | rings, order trails, bars, squad labels, production bars, minimap, panels, chat, menus, unit info (`:1599`–`:2162`) at native resolution | render thread |

## Cache, buffer and resource inventory

| Cache / buffer | Owner | Key / invalidation | Storage & budget | Lifetime / reset |
|---|---|---|---|---|
| Unit geometry (`geomEntries_`, `UnitGeom`; was `geomPool_` before C) | GameView | ownership: `UnitOwner` = (unit id, world epoch, type, replacement model), generational handle per acquisition; content validity (since D): three exact stage keys — local (id, type, visual, atlas ptr, player, slot, pitch/roll, construction, corpse phase, statue, shadows-on, facing, animated texture frames, piece-name table, piece poses), placement (zoom, anchor x/y, emote bob, altitude, occlusion line), material (veteran, ghost, conjuring, emote, tint colour/mix, opacity, anim clock while conjuring); `revision` and the per-stage revisions from one GameView-wide atomic counter | one 512-byte (64-byte aligned) entry per visible or recently visible unit in 256-entry chunks that never move, plus its vectors (screen output and, since D, retained local positions: 8 B per body and per shadow vertex, base colours only for untextured faces); absent units kept ≤ 2 s and ≥ 30 frames, within 32 MiB / 8192 entries (oldest first); released capacity kept ≤ 8 MiB / 4 s then freed; all-free tail chunks deallocated; id index sized from live entries | `invalidateRenderTargets` → `makeAllCold` (owners kept, content dropped); world reset → new epoch; destroyed units released the frame they leave the snapshot |
| Body buffer `bodyVerts_`, `copyTasks_`, `drawOps_` | GameView | rebuilt each frame | peak capacity retained; since J trimmed to peak + 1/16 after a whole window (5 s and 120 frames) below half the capacity | session |
| Shadow silhouette pages | GameView + `ShadowAtlasAllocator` (since F) | each unit's tile keeps a cell (handle on `UnitGeom::shadowTile`, validated against the cell's entry handle); stamp per cell (entry, owner, coverage = `shadowRevision`, exact `ShadowPlacement`, screen pixel, page, slot, size, scale), carried when the cell moves; a placement change is reused only when every tile point recomputes bit-identical in the same cell | ≤4 GPU target pages up to 2048² (≤64 MiB); class-height shelves, exact widths; absent cells retained 2 s / 30 frames; empty pages destroyed after 2 s; page size shrinks after 5 s of low demand | destroyed on reset/teardown (`resetShadowAtlas`); handles never alias across resets |
| Shadow scratch (`shadowAtlasOpaqueScratch_`, mask scratch, `shadowCompositeBatch_`) | GameView | per frame | capacity retained; trimmed like the body buffer since J (peaks noted before each per-page refill) | cleared on reset |
| Distant images | `DistantModels` | geometry-entry handle (index + generation; unit id before C) → entry (revision, texture tick, source ptr, zoom, scale), 67 ms refresh; since G also retained bounds per handle (revision, source ptr, count) and the early lookup `holds()` | ≤4 × 2048² pages (64 MiB), 1024 64-px tiles/page, ≤128 bakes/frame (bakes grouped by page since G); per-page occupancy since G | entries/bounds expire after 120 unseen prepares; since G a page is released when its last image expires (before: only when all expired), bounded same-position migration off a sparse highest page; cleared on reset |
| Colour atlases `atlasTex_` | GameView | per player-colour slot; layout fixed at first model load; `atlasTracker_` slot record = (renderer generation, asset generation, painted frame per animated region) | GPU target ≈ 2048×atlasH ×4 (≈11 MiB) per slot, ≤10; essential (bypasses cap) | destroyed on reset (tracker cleared); painted completely on creation |
| Animated texture state | `modelTextureAnimations_`, `animatedTex_`, `animatedRegions_` | frame from game tick, every draw; `animatedRegions_` (rect, frames, frame) rebuilt per update in registry order | CPU | `assetGeneration_` bumped on texture teardown and atlas layout; tracker records from another generation repaint every region |
| Palette pages | `paletteLayout_` (layout), `palettePages_`, `paletteTextureSheets_`, `paletteIndexed_` (since M) | per texture name, 20 shade bands; one form at a time (`paletteMode_`): baked 2048² RGBA pages, or indexed key texture + 512² RGBA (index, owner, alpha) page + shared LUT / identity | baked ≤16 × 16 MiB; indexed ≤16 × 1 MiB + 264 KiB | both forms released on a form switch (filter, failure, GL context) and in `destroyGpuTextures`; not on a target reset (static) |
| Palette sources (CPU) | `paletteTextureSources_`, `paletteBanks_` (since M) | per texture name: bank + (index, alpha) frames | 6.6 MiB for 3008 textures (39.4 MiB before M: palette + shade copies + RGBA per texture) | session |
| Model source textures `textures_` | GameView | per texture name, per colour/frame | GPU static | teardown |
| Shadow mask/coverage pages | `shadowMaskPages_`, `shadowCoverageTextures_` | per texture/frame | GPU | teardown |
| Feature art, effect anims, icons, fonts, GUI | GameView | by name | GPU static | teardown |
| Terrain chunks | `TerrainChunks::cache_` | (level, cx, cy); `revision`; LRU of chunks not drawn this frame; since K eviction ranks (cold first, stand-ins of visible cells last) and off-view uploads only into free/cold room | GPU static, 256 MiB budget and GPU cap; ≤8 MiB chunk uploads/frame, since K inside one frame budget with the sections (urgent 32 MiB / 6 ms est., non-urgent 2 ms est.) | `invalidate`/`clear`; reset clears; pending jobs cancelled when obsolete (K) |
| Terrain mips | `TerrainChunks::mips_` | (section key, level, stock) | CPU, worker-only; since J LRU of unpinned mips, 64 / 56 MiB, all dropped after 30 s + 120 frames without terrain work | `resetSource` |
| Terrain sections (GPU) | `MapView::sections_` | section key; idle 3 s, 64 MiB idle budget | GPU | trimmed; reset clears |
| Decoded sections (CPU) | `terrain::Compositor::cache_` | section key | CPU; since J pinned `SectionRef`s, 128 / 112 MiB budget over unpinned sections cold for 10 s, load-time prefetch only into free budget (whole set on 97.7% of maps) | `clear()` safe with pins (images live until released) |
| Terrain fallback batch | `MapView::tileBatch_/apronBatch_` + `tileSubmit_` | camera/offset/zoom/size/dirty | CPU vectors + retained VBO via revision | rebuilt on change |
| Fog | `fogTex_`, `fogUpload_`, `fogSubmit_`, fog mesh | vis generation, camera | GPU texture + retained mesh | reset clears |
| Feature index | `FeatureIndex` | rebuilt when features change (`featureIndexDirty_`) | CPU | session |
| Unit grid (since L) | `Frame::unitGrid` (`UnitGrid`, `client/unitindex.h`) + `Frame::buildGhostUnits` | rebuilt by `captureFrame` for every snapshot from the copied positions; indices into that frame's `live` | CPU, per ring slot (12): 4 B per live unit + 8 B per cell (cells ≤ 256², 256-px cells over the units' bounding box: 40×40 on Ulasem Arena); capture scratch 12 B per unit; render scratch (identity table, query list, bitset) ≈ 8 B per unit | lives and swaps with its snapshot; never read by the simulation |
| Unit registration record (since L) | `UnitTypeTable` (`client/unittypetable.h`, was `std::unordered_map<int,std::string> unitType_`) | one flag per non-negative unit id, changed only with the map (`set`/`erase`) | CPU, 1 B per id ever registered (the map keeps a string per id; neither is pruned, as before) | session |
| Minimap | `miniTex_`, async crunch | map | GPU | rebuilt |
| Fog LoS masks | `World::visMaskCache_` (client-local, never hashed) | (sight, radar, cell) | CPU, 8192 entries | session |

GPU accounting: every client `SDL_CreateTexture` goes through
`gpuvram::create/destroy` (`gpuvram.h`) — tracked texture bytes plus GL buffer
bytes (`addBuffer`), default cap 1.25 GiB, self-tightening on allocation
failure. Excludes driver-internal allocations, SDL's own vertex/command
buffers, the default framebuffer and GL client-array staging.

Worker pools: `GameView::pool_` (hardware_concurrency threads incl. caller
until workstream I; since I the CPUs in the process's affinity mask; used by
geometry, shadow checks, shadow transform, body copy and the COB VM pass —
never called concurrently from two threads; since I each renderer pass is
planned by estimated cost and wakes only the workers it pays for); `capturePool_` for the sim worker's
snapshot capture; the sim worker thread; the async fog compute worker; terrain
chunk worker; section decode worker; minimap crunch. Since J the render
pool's scratch is one `WorkerScratch` slot per participant, trimmed by the
render thread between passes.

Fallback paths: shadow atlas → direct shadow geometry (`drawUnitShadow`) on
missing target support, budget, page limit or draw failure (whole frame;
since F only after a full repack with the former packer also failed);
native GL batch → `SDL_RenderGeometry` on non-`opengl` renderers, fractional
scale, non-normalized textures or <512 vertices; indexed structure palette
(since M) → baked palette pages on any renderer without the program, bilinear
units, a failed indexed draw or a lost GL context (whole form, next frame); distant images → full
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

Added by workstream G:

- `client/distantmodels.h`: the rules as constants (`kMaxZoom` 0.6,
  `kMinVertices` 96, `kRefreshMs` 67, `kBakeBudget` 128) and `candidate(item)`;
  `holds(id, zoom, sx, sy, now)` (read-only, called by the geometry workers);
  `Item::bodyDeferred` and the `Materialize` callback of `prepare()`;
  `validate()`, `livePages()`, `liveTiles()`, `metadataBytes()`; telemetry
  fields `boundsReused`, `lateBodies`, `pageReleases`, `migrated`; test/debug
  switches `allowSoftware`, `sweepEveryPrepare`.
- `geometrystages.h`: `bodyVertexCountStable(before, layout, after, layout)`.
- `UnitGeom::bodyPending` / `bodyRun` / `bodyDeferred` (entry still 512 bytes:
  the three bytes fill the padding after `shadowSkipped`);
  `buildUnitGeom(u, g, scratch, mayDeferBody)`;
  `runUnitGeomStages(u, g, stages, bodyStages, in, scratch)`;
  `GameView::specialUnit`, `distantEligible` (one eligibility function for the
  workers and the item fill), `materializeDistantBody`, `distantNow_`,
  `distantScaleX_/Y_`, `distantEarly_`.
- Debug: `TAK_DISTANT_LOOKUP_LATE=1` (pre-G order: every body generated before
  the lookup — the same-binary reference), `TAK_DISTANT_VERIFY` (prints
  `DISTANT_VERIFY frames deferred checked catchup late`), `TAK_DISTANT_SOFTWARE`,
  `TAK_DISTANT_SWEEP_EVERY_FRAME`.
- `tools/distant_models_legacy.h`: the pre-G cache verbatim (test reference).

Added by workstream H:

- `client/vertexarena.h`: `ArenaSource` (data, count, slot, generation,
  revision; revision 0 = by value, 6 vertices), `ArenaRange` (source, offset,
  count), `ArenaPolicy` (capacity, headroom, stable fraction, migration
  budget, idle/shrink/retry windows), `ArenaMode` (`Stream`/`Append`/
  `Rebuild`), `ArenaStreamReason`, `ArenaUpload`, `ArenaFrameStats` (incl.
  `rollover`, `migratedVerts`, `oldCommands`), `kArenaSlots` = 2,
  `VertexArenaPlanner` (`begin`, `add`, `addCommand`, `plan(allowed,
  budget)`, `curSlot`, `releaseSlot`, `allocate`, `newCapacity`,
  `writeBegin/End`, `uploads`, `commandSlot`, `commandFirstRange`,
  `first(command, range)`, `written(source)`, `end(ok)`, `shouldRelease`,
  `release`, `cpuBytes`), `VertexArenaBuffer` (`ready`, `allocate` (orphans),
  `write(first, data, count)` (one glBufferSubData), `release`, `forget`,
  `endFrame`, `setSimulated`, in-flight accounting `kInFlightFrames` = 3) and
  `appendArenaRange` (merge consecutive ranges). Any later retained GPU
  geometry should use this planner rather than a parallel scheme.
- `GeometrySubmit`: `kNativeMinVertices` (512), `nativeCapable(renderer)`,
  `drawBuffer(renderer, texture, buffer, firsts, counts, vertices)` (0 drawn,
  1 = not drawable natively → caller draws through `draw()`, −1 SDL error),
  `multiDraw()`; the readiness checks and state prime are shared private
  helpers of `draw()` and `drawBuffer()`. Workstream N builds on
  `drawBuffer`'s single prime per command.
- `GameView`: `BodySource`, `BodyRange`, `CopyTask` (now pointer + count +
  destination), `DrawOp::firstRange/ranges/arenaFirst/arenaRanges/
  arenaCommand/arenaSlot`, `bodySources_`, `bodyRanges_`,
  `bodyArenaFirsts_/Counts_`, `bodyArenaRanges_`, `bodyArena_`,
  `bodyArenaBuffers_[2]`, `bodyArenaMirror_[2]`, `bodyVertexTotal_`,
  `releaseBodyArena()`, `bodyArenaBudget()`, `bodyArenaPressure()`,
  `verifyBodyArena()`.
- Debug: `TAK_BODY_ARENA_OFF` (pre-H streaming, same binary),
  `TAK_BODY_ARENA_VERIFY` (prints `BODY_ARENA_VERIFY frames commands
  vertices retained uploaded appends rebuilds streams fallbacks rollovers
  drains old_commands stream_{disallowed,empty,retry,unstable,budget}`;
  throws at the first command that differs from its sources),
  `TAK_BODY_ARENA_SOFTWARE`, `TAK_BODY_ARENA_MIN=N` (arena command threshold),
  `TAK_BODY_ARENA_HEADROOM=x` (tight storage for tests: rollovers).

Added by workstream J:

- `terrain/terrain.h`: `SectionRef` (`shared_ptr<const jpeg::Image>`: holding
  it pins the image); `Compositor::sectionImage` returns one (decode outside
  the lock, in-flight dedup); `cachedSection(key, stock, &absent)` (never
  decodes); `setBudget(high, low, minIdleMs)`, `setClockMs`, `trim()`,
  `admitsPrefetch`, `Stats` (used, pinned, young, peak, entries, decodes,
  redecodes, evictions). `clear()` no longer needs stopped readers (VFS swaps
  still quiesce). Any new consumer must hold a `SectionRef` for as long as it
  reads the pixels.
- `TerrainChunks`: `mips_` holds `SectionRef`s with LRU metadata;
  `setMipBudget(high, low, idleMs)`, `prepare(..., nowMs)`; `Stats::mip*`.
  `MapView::kSectionCpuHigh/Low`, `kSectionMinIdleMs`, `setMipBudget`,
  `SectionStats::cpu` / `prefetchSkipped`.
- `client/cpubudget.h`: `TrimPolicy`, `TrimTracker::observe(need, capacity,
  frame, nowMs, policy)` → bytes to shrink to (0 = keep), `shrinkVector`. Any
  later retained per-frame buffer should be added to
  `GameView::trimRetainedBuffers` (enum `Retained`); if it is refilled more
  than once per frame, call `noteNeed` before each refill.
- `ThreadPool::participant(pool)`; `GameView::WorkerScratch` +
  `workerScratch()` (replaces the geometry/shadow/collector `thread_local`s;
  `CollectVertex`, `ShadowCollectVertex` hoisted).
- Debug: `TAK_TERRAIN_SECTION_BUDGET_MIB`, `TAK_TERRAIN_SECTION_MIN_IDLE_MS`,
  `TAK_TERRAIN_MIP_BUDGET_MIB`, `TAK_TERRAIN_MIP_IDLE_MS`, `TAK_CPU_TRIM_MS`,
  `TAK_CPU_TRIM_FRAMES`, `TAK_CPU_TRIM_FLOOR_KIB`, `TAK_CPU_TRIM_VERIFY`
  (prints `CPU_TRIM_VERIFY frames trims worker_trims released content_checks
  regrowths peak_retained final_retained recovered`).

Added by workstream L:

- `client/unitindex.h`: `UnitGrid` (`build(xs, zs, scratch)`, `query(l, t,
  r, b, bits, out, stats)` → ascending superset, `covers`, `all`,
  `capacityBytes`), `LiftBounds`, `anchorBandToWorld`, and per renderer pass
  (`unitpass::painter`, `healthBar`, `selfDestruct`, `squadMarker`,
  `productionBar`) the screen-position expression, its `…InView` test and its
  `…Band` (`ScreenBand`, `ScreenPoint`, `AnchorView`). A new per-unit pass that
  culls a unit's lifted anchor should add its triple here and query through
  `GameView::unitCandidates(band)` rather than walk `front().live`.
- `client/unittypetable.h`: `UnitTypeTable` (map read API + `registered(id)`,
  `set`, `erase`); `GameView::unitType_` is one. A unit's visual name changes in
  place on corpse/veteran mesh swaps: never cache a resolved visual by unit
  identity.
- `Frame::unitGrid`, `Frame::unitGridBuildNs`, `Frame::buildGhostUnits`
  (`client/renderframe.h`); `CaptureSlot::x/z/buildGhosts`;
  `GameView::unitCandidates`, `unitLiftBounds`, `unitFootZRange`,
  `unitIndexVerifyTotals()`.
- Debug: `TAK_UNIT_FULL_SCAN=1` (same-binary pre-L reference),
  `TAK_UNIT_INDEX_VERIFY=1` (prints `UNIT_INDEX_VERIFY frames passes live
  candidates accepted health selfdestruct squads production ghosts`; throws
  at the first difference); harness `TAK_RENDER_SHAKE=N` (camera shake every N
  study frames), `TAK_RENDER_OVERLAYS=1` (control squads, factories with long
  queues, builders with queued builds).

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
| Full repack (`shadow_compactions`) | releaseAll, the former packer with class heights; if that does not fit, the former packer with exact sizes = the former slots, so the atlas falls back to direct shadows only where the former code did; after a failed frame the next goes straight to the exact pass (the failure mark survives the atlas reset that follows the last page's release) |
| New page needed (any path) | added to the allocator **logically** (VRAM budget checked for every page added this frame); its GPU target is created only after every tile of the frame is placed (`realizePages`). A frame that does not fit creates no texture and drops its logical pages, so a scene that fits four pages by area but not by packing falls back every frame without creating or destroying a page (pages it held are released once, 2 s after they empty) |
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

### Distant-image lookup before body generation (workstream G)

Before G, every visible unit's body was generated (collection, sort,
placement, colours) and only then did `DistantModels::prepare` decide whether
the unit would be drawn from its cached image at all; bodies the cache refused
(too large, translucent, no page room) were rescanned vertex by vertex every
frame to find that out again. G moves what can be decided early to before the
generation and keeps what was decided:

```
render thread, before the parallel pass:  lookup inputs fixed (clock, output scale),
                                           selSet_ filled
worker (buildUnitGeom, per unit):          keys -> stages  (D, unchanged)
   eligible (distantEligible: not special/moving/walking/corpse/replacement/ghost/selected)
   && zoom <= 0.6 && pending body >= 96 vertices
   && local key changed only in appearance words (bodyVertexCountStable)
   && cache.holds(handle, zoom, sx, sy, now)      <- image valid, same zoom/scale, baked < 67 ms ago
      yes: body stages left pending (bodyPending |= stages); shadow stages run; revision advances
      no : body stages = this frame's stages | pending
render thread:  prepare(items)            decide in painter order (unchanged rules)
                                          -> bakes grouped by page -> quads in painter order
                materialize callback       a deferred body needed for a bake (prediction missed)
                catch-up                   a deferred body whose image was not shown
```

Why a held image needs no body: `prepare()` reads an item's revision and
vertices only when it repaints. When `holds()` is true the decision is "show
the existing image" whatever the content (`!valid || scaleChanged ||
(contentChanged && age >= 67 ms)` is false), so the body would be generated
only to rediscover that. The revision still advances with every stage change
(D's rule, unchanged), so the image's own 67 ms freshness rule sees every
change and repaints on the same frame as before; the bake then needs the body,
which is generated in that frame's build because `holds()` is false by then.

What stays exact:

- **Eligibility.** The one rule that reads the body is the vertex count
  (≥ 96). The body collector culls back faces against the view direction, so
  every pose word can change the count; a body is deferred only when the local
  key changed in appearance words alone (colour atlas, player and colour slot,
  shadow toggle, texture frames), so the pending body has exactly this frame's
  count. In practice G defers placement-only changes (a pan at zoom ≤ 0.6) and
  material-only changes inside a held window; pose changes generate the body
  as before. (The first version also deferred pose changes; the verifier
  rejected it at once — see Rejected experiments.)
- **Shadow and picking.** The shadow stages run every frame as before (the
  piece transforms are prepared for the silhouette alone when the body is
  held back); picking (`pickWorld`/`unitScreen`) reads no geometry.
  `TAK_GEOMETRY_VERIFY` compares a pending entry's shadow output with a
  complete rebuild every frame and its body when it is generated.
- **Decisions.** `prepare()` makes every decision in painter order as before;
  a missed prediction (scale changed between the pass and `prepare()`, an
  early return) is caught by the callback or the catch-up. Measured: 0 misses
  in every scene and test.
- **Retained bounds.** A body's bounds and opacity are kept per identity with
  the revision, source address and vertex count they were measured on; the
  revision changes whenever the vertices do, so a refused body is not rescanned
  until its content changes. The map is separate from the image entries so
  their insertion/erase history — and the order expired tiles are reused in —
  is the pre-G one.
- **Grouped bakes.** Each bake clears and fills only its own 64-px tile, so
  baking page by page changes no pixel; target switches fall.
- **Pages.** A tile's pixels (bake positions `(v - minX)*sx + tx + 2`) and its
  composite texture coordinates (`(tx + 1)/2048`) depend on its position in
  the page, never on the page. The free list keeps the pre-G order of tile
  positions; a new image takes that position on the lowest live page where it
  is free, a page whose last image expired is released at the next sweep
  (every 60 frames; images expire after 120 unseen) even while other pages are
  live, and migration moves the images of a highest page with ≤ 256 images to
  the SAME position on a lower page by a 1:1 texel copy, blending off, all or
  none, then releases it. Every bake and quad is therefore the pre-G one, on
  at most the pre-G number of pages (`validate()`: for every position the free
  list plus the images there equal the pages the pre-G code would hold).
  Same-position migration is exact by construction and bounded, but applies
  only when those positions are free below; in the matrix scenes the
  survivors' positions overlap, so the release of emptied pages does the work.

| Event | Body | Shadow | Image |
|---|---|---|---|
| Pan at zoom ≤ 0.6, image baked < 67 ms ago | pending (re-placement skipped) | re-placed | shown, moved with the anchor (pre-G: same) |
| Same, ≥ 67 ms | generated (pending stages + this frame's) | re-placed | re-baked within the 128 budget, else live (pre-G: same) |
| Veterancy/tint change inside a held window | pending | kept (E) | held (pre-G: same) |
| Texture frame / palette change inside a held window | pending (collection, sort skipped) | collected if a masked frame changed | held (pre-G: same) |
| Pose / facing / pitch / corpse change | generated | as D/E | held or re-baked (pre-G: same) |
| Unit selected / starts moving / special | generated (not eligible) | as before | live |
| Refused for shape or pages, content unchanged | as before | as before | bounds from metadata, no vertex scan |
| Renderer reset | entry cold (C) | cold | cache cleared |

### Retained body vertices (workstream H)

**The vertex journey before H** (traced in the code at `908cf088`):

```
UnitGeom::verts (per unit, screen space; D's placement/material stages write it)
  │  ① every frame: parallel copy of EVERY visible body run (and each distant-image
  ▼     quad) into one assembled buffer, GameView::bodyVerts_
bodyVerts_ ──► GeometrySubmit::draw per texture segment
  │  ② segment ≥ 512 vertices, opengl backend, integer scale: GL client arrays —
  │     the driver copies the arrays again when glDrawArrays is issued
  │  ③ smaller segments / other backends: SDL_RenderGeometry — SDL converts and
  ▼     copies every vertex into its own queue, uploads at flush
GPU
```

A stationary army was therefore copied twice per frame although almost
nothing changed: total10k-visible at 7680×2160 copied 2.49 M vertices
(49.7 MB) into `bodyVerts_` and handed the same 49.6 MB to GL every frame.

**After H:**

```
UnitGeom::verts / distant quad ──► sources (identity + content key)
            │
 painter order ──► ordered commands: texture + ranges of sources
            │       (features, special units, the ground/air shadow boundary
            │        stay command boundaries — order and state as before)
            ▼
 command ≥ 512 vertices on a capable renderer, frame mostly resident/unchanged
     ├─ resident source: nothing copied, nothing uploaded (keeps its range)
     ├─ changed/new source: copied WHOLE into the tail of bodyVerts_ (behind the
     │   assembled commands), then one glBufferSubData into arena storage nobody
     │   reads; its runs that sit in small (assembled) commands this frame are
     │   therefore copied twice (`arena_verts_twice`, see below)
     └─ one drawBuffer per command over its merged ranges (glMultiDrawArrays),
         from the ONE storage the command is placed in
 every other command / frame: assembled into bodyVerts_ and streamed (② / ③,
     the pre-H path, unchanged)
```

Ownership and keys (`client/vertexarena.h`):

| Thing | Owner | Key / rule |
|---|---|---|
| Source identity | geometry entry (C) | `slot` = handle index, `generation` = handle generation |
| Body content | entry | `UnitGeom::revision` — advances whenever any stage is redone (D), unique across all entries and occupants, never the sim tick; a body left pending behind a held image (G) gets no retained identity |
| Quad content | distant cache item | compared by value (6 vertices, `memcmp`); the last seen value kept per slot |
| Range | `VertexArenaPlanner` record per slot | valid while the record's storage epoch is the current one and the key matches |
| Storage | two `VertexArenaBuffer`s (GL buffer objects): the CURRENT one takes every write, a DRAINING one (after a rollover) is only read | epoch per allocation; epochs never reused |
| Command placement | planner, per frame | a command draws from exactly one storage |
| Commands | `drawOps_` + `bodyRanges_` + `bodySources_` (per frame) | painter order; arena commands carry `arenaFirst/arenaRanges` into `bodyArenaFirsts_/Counts_` |

Frame modes and storage roles (planner):

| Mode | When | Copies / uploads |
|---|---|---|
| Append | ≥ 50% of the frame's arena-candidate vertices resident or unchanged since the last frame, and the current storage holds this frame's forced writes + the migration budget | sources of commands that changed (each once), plus migrated unchanged commands within the budget |
| Rebuild — rollover | as Append, but the current storage would overflow (or must grow/shrink) and no storage is draining | the full storage becomes the DRAINING one; a new current storage is allocated (frame × 2, rounded, ≤ 64 MiB, ≤ GPU budget); changed commands are written to it, unchanged commands keep drawing from the draining storage and migrate within the budget |
| Rebuild — full | no storage yet (first fill, after a reset/release), or the current storage fills while another still drains, or the budget allows no second storage | everything into fresh storage (the pre-H copy volume, once) |
| Stream | content mostly new (camera motion, moving army), no capable renderer, budget, retry after a failure | none into the arena; pre-H assembled path; records still note what was seen |

Per command (painter order): drawn from the current storage when all its
sources are resident there; else from the draining storage when all are
resident there and the migration budget (1/8 of the frame's vertices, at
least 32k) cannot cover moving it; else it moves to the current storage
(its non-resident sources copied). A changed source anywhere in a command
moves the whole command. When no command drew from the draining storage,
it is released. The forced writes are counted exactly before any decision,
so a frame never writes past its storage.

Why no GPU-in-use overwrite is possible: a write only goes to vertices of
the current storage not written since it was allocated (Append writes past
`top`, a rollover or full rebuild writes fresh storage), a draining storage
is never written, and every draw reads only ranges written in the storage
it names. Released or orphaned storage belongs to the driver until its
pending draws have executed. So no fence, no persistent mapping and no
unsynchronized write are needed; buffer objects (GL 1.5) are the only
requirement (`glMultiDrawArrays`, GL 1.4, optional), and without them the
assembled path runs.

Why the output is identical: an arena command draws the very vertices the
assembled command holds (the verifier checks it every frame in debug), with
the same GL state (`drawBuffer` shares `draw()`'s readiness checks, state prime
and client-state setup; only the array source differs), and ranges execute in
order (`glMultiDrawArrays` = consecutive `glDrawArrays`). Placement stays in
the screen-space vertices: moving it into a GPU transform per range would
change rounding (D's arithmetic is the CPU's, bit for bit) and need one draw
per unit — the "thousands of tiny draws" the brief excludes. So H removes
copies and uploads, never draws: one native draw per command, as before.

Lifetime and memory (all on the render thread):

| Event | Effect |
|---|---|
| Storage (re)allocated / released | its bytes counted in gpuvram while live; an orphaned or released store stays counted 3 more frames (in flight — an estimate: the driver's retention is not observable, never undercounted while the GPU may read it). During a drain both storages are live and counted |
| Budget | a new storage only within 3/4 of the GPU cap minus everything held (both storages + in flight), max 64 MiB each; without room for a second storage the frame streams; nothing while the cap is tightened/blocked after a failure |
| GPU pressure | tracked total > 7/8 of the cap, or tightened/blocked → storage released (yields to caches that obey the cap) |
| Idle | no arena frame for 120 frames and 2 s → storage released (cold reclaim); records past the highest live slot trimmed |
| Low demand | capacity > 4 × the frame's vertices for 300 frames and 5 s → next stable frame rebuilds smaller |
| Allocation failure (`GL_BUFFER_SIZE` mismatch / `GL_OUT_OF_MEMORY`) | `gpuvram::noteFail`, storage dropped, frame streams, arena off for 120 frames |
| Write failure (upload error) | both storages dropped, that frame's arena commands re-assembled in place of the staged uploads (no gap; `render_body_arena_fail`) and streamed, arena off for 120 frames |
| Render-target / device reset, GPU teardown | `invalidateRenderTargets` → storage released, every record non-resident |
| New GL context | `ready()` sees another context and forgets the old buffer (no delete, accounting dropped) |
| Texture fails the native check at draw time | that command is drawn from its sources through `draw()` (SDL path) |

CPU memory: planner records (two locations per slot, slots ≤ live handle
index space; trimmed when no storage drains after an allocation, and on
release), last-seen quads (120 B per slot that showed an image), the
planner's per-frame scratch, the debug mirrors; reported as
`arena_cpu_bytes`. Uploads are staged in `bodyVerts_` behind the assembled
commands (`body_used_bytes` / `body_capacity_bytes`). A source is uploaded
whole, because its slot must hold every run for a later frame to draw any of
them from the arena; the runs it also has in small (< 512-vertex, assembled)
commands that frame are copied twice (`arena_verts_twice`: about 0.4% of the
copies in a steady dense frame, ~21k of 2.53 M vertices on a full rebuild
frame at 7680×2160). Not avoided: removing it needs one upload call per such
source instead of one per frame. So a rebuild frame's need is the frame's
vertices plus that overlap — slightly above the pre-H need (50.52 vs 50.28 MB
peak used at 7680×2160). `bodyVerts_` grows by need + 1/16, never by
`std::vector` doubling: H4 grew by doubling and held 2× its need from the
first rebuild frame on (Experiment 10b). Like the pre-H buffer, it is not
trimmed after demand subsides (workstream J). Per-frame command lists are in
`frame_lists_*`.

### Cost-planned worker-pool passes (workstream I)

**What was there.** `GameView::pool_` was a `ThreadPool` of
`std::thread::hardware_concurrency()` threads (24 on the reference machine,
whatever the process's CPU affinity). Every parallel pass split its items into
fixed 8-item chunks, woke EVERY worker with one `notify_all`, and the caller
waited until every worker had checked in — including workers that woke after
all chunks were gone. Four renderer passes used it, with thresholds on item
counts, not on work:

| Pass | Before I | Items | Per-item cost |
|---|---|---|---|
| Geometry (`buildUnitGeom`) | parallel at ≥ 32 visible units | visible units | key check ~1 µs (hit) … full local rebuild ~10–40 µs |
| Shadow point checks | parallel at ≥ 32768 points and ≥ 16 tiles | tiles needing the check | ~2 ns per point (stops at the first differing point) |
| Shadow tile transform | parallel at ≥ 32768 points (per page) | EVERY packed tile, clean or on another page (skipped inside) | ~1.6 ns per opaque point, ~3.2 ns per masked vertex |
| Body copy | parallel at ≥ 32 copy tasks | copy ranges | ~0.1 ns per byte + ~40 ns per range |

The COB VM tick (`animFrame`, minParallel 1500) shares the pool but is not a
render pass; the snapshot capture uses its own 4-thread `capturePool_` on the
sim worker; the simulation's `SimPool` (min(3, CPUs/8) workers, affinity-aware),
the terrain chunk worker, the section decode worker and the fog worker are
separate threads. The pool is never called from two threads at once (by
construction: only the render thread drives `pool_`).

**What changed.**

`ThreadPool` (`client/threadpool.h`):

- Sized by the CPUs the process may run on (`sched_getaffinity` on Linux,
  `hardware_concurrency` elsewhere) — the rule `SimPool` already follows. A
  24-thread pool in an 8-CPU process preempts itself and the render thread.
  Debug `TAK_RENDER_POOL_THREADS=N` overrides it (1 = serial reference).
- A pass wakes only the workers it asks for: each worker sleeps on its own
  condition variable; the caller posts `participants − 1` of them.
- The caller waits for the CHUNKS, not the workers. Participants claim chunk
  indices from one atomic word (chunk count | next index); the last finished
  chunk wakes the caller. A worker that wakes after every chunk was claimed
  finds the index exhausted and goes back to sleep without the caller waiting
  for it (`pool_idle_wakes`). Lifetime: the job fields and the caller's
  functor are read only by a thread holding an unfinished chunk, so the pass
  cannot end under it; the next pass rewrites them only after every chunk of
  the previous one finished (late claims on the old word read an exhausted
  index). The caller polls ~20 µs for the last chunks before sleeping.
- `parallelFor` (uniform 8-item chunks) keeps its contract for the VM tick and
  tests; it now wakes at most one worker per chunk.
- `parallelChunks(bounds, participants, f)` runs a planned pass.

`WorkPlan` (`client/workschedule.h`): items are added in pass order with an
estimated cost in ns; the plan cuts contiguous chunks of ~`chunkNs` and picks
`participants = clamp(⌈estimate / workerNs⌉, 1, min(pool, chunks, cap))`, or 1
below `serialBelowNs`. Pure function of the estimates (no clocks), so a run is
reproducible; it never decides what is computed.

Estimates (fitted to ~950k serial per-unit samples and serial per-pass timings
over the matrix scenes, Experiment 11):

| Pass | Estimate per item | Policy (serial below / per worker / chunk / cap) |
|---|---|---|
| Geometry | `GeometryCostModel::estimateNs` of what the entry's PREVIOUS build redid (key words, local/placement/material stages, body vertices, shadow points; a hit's estimate is the key check alone, so it reads no vector header), stored per entry as a 16-bit `UnitGeom::nextCost` (64 ns units) on its first cache line and copied into the dense `visCost_` at claim time; a cold entry carries a full-build estimate (16 µs); planned with `WorkPlan::addUnits` in integer units | 30 µs / 40 µs / 10 µs / pool |
| Shadow point checks | 2 ns × points | 30 / 40 / 10 / pool |
| Shadow transform | only this page's dirty tiles: 1.6 ns × opaque points + 3.2 ns × masked vertices | 30 / 40 / 10 / pool |
| Body copy | 40 ns + 0.1 ns × bytes per range | 30 / 40 / 10 / pool (a 4-thread cap measured slower) |

The prediction "this unit redoes what it redid last frame" is only a
load-balance hint: moving units keep rebuilding, still ones stay hits; a
misprediction costs balance (claiming absorbs it), never correctness.

Determinism and output order: every item writes only its own outputs (its
geometry entry, its tile's slice of the page batch, its destination range of
`bodyVerts_`), so a pass's result is independent of which thread ran which
chunk and of completion order. The plan itself depends only on per-entry
estimates. Revisions (`geometryRevision_`) are drawn from one atomic counter
in completion order — their numeric values can differ run to run (as before
I), but they are only compared for equality. Nothing here reaches the
simulation.

Reference and verification switches (debug): `TAK_RENDER_SCHEDULE=uniform`
runs the pre-I uniform passes (8-item chunks, every thread, the former
thresholds) on the new pool; `TAK_RENDER_POOL_THREADS=N`;
`TAK_SCHEDULE_<GEOMETRY|SHADOW_CHECK|SHADOW_PREP|COPY>=serialBelowNs,workerNs,chunkNs[,max]`
(tuning); `TAK_SCHEDULE_VERIFY` (every planned pass well formed, each visible
unit built exactly once, shadow checks / tile transforms / body copies equal a
serial re-run bit for bit); `TAK_SCHEDULE_SAMPLES=<file>` (calibration
samples: per unit and per pass).

### CPU cache budgets and reference lifetimes (workstream J)

**Order of work.** The brief requires reference lifetimes to be fixed before any
eviction. J1 (`dac6e399`) changed the section contract and converted every
consumer with no budget active. J2 (`469f743a`) then turned eviction on, and
J3–J5 added the mip budget, the buffer trims and the working-set rule.

**The old contract.** `terrain::Compositor::sectionImage` returned
`const jpeg::Image&` into a `std::map`, valid "until clear() or destruction".
That was safe only because nothing was ever evicted, and because every caller
of `clear()` stopped the readers first (`MapView::reload` → `quiesce()`, which
waits for the chunk worker and the decode worker). Any eviction would have
freed images in use.

**Consumer audit** (every use of a decoded section, at `c2c25707`… `31c5ce79`):

| Consumer | Thread | Held for | Before J | Since J |
|---|---|---|---|---|
| `TerrainChunks::compose` | chunk worker | one compose job (the job's tiles × levels) | raw `const Image*` per tile key, from `mip()` (level 0 = a reference into the compositor) | job-scoped pins: `std::map<key, SectionRef>` for the whole job (level-0 sections and mips) |
| `TerrainChunks::mip` | chunk worker | deriving level n from n−1 | reference to level n−1 | n−1 pinned while it is read; mips are `SectionRef`s too |
| `MapView::uploadWantedSections` | render thread | one `SDL_UpdateTexture` | reference | pin until the upload returns; `cachedSection` (never decodes) in play; `finishChunks` frames decode in place |
| `MapView::uploadReadySections` | render thread | reading a byte count | reference | no image access (absent keys only) |
| `MapView::sectionWorkerLoop` | section decode worker | decode only | reference discarded | transient pin; prefetch admission |
| `Compositor::renderBlock`: minimap crunch (`GameView::buildMinimap` thread), Cartographer symmetry tools, `newmap` minimap generation/preview, thumbnails (`renderMap`), `tnttool render`, tests | any | one 32×32 copy | cache lock held across decode **and** copy | pin; decode outside the lock; copy without the lock |
| Cartographer `validateTerrainResources` | UI | decode check | own compositor, `clear()` after each key | unchanged (a discarded pin) |
| Cartographer thumbnails `clear()` every 8 decodes | UI | — | safe: after the async job finished | safe without that condition: pins outlive `clear()` |
| `MapView::reload` → `quiesce()` + `clear()` | render thread | — | `clear()` required stopped readers | still quiesces (the decode reads the VFS being swapped); images held elsewhere survive |

**Ownership model.** A decoded image is a `SectionRef =
std::shared_ptr<const jpeg::Image>`. A consumer pins exactly what it uses for
as long as it uses it (a compose job, a texture upload, a block copy). The
cache drops only its own reference: an image the cache evicts or clears stays
valid until its last holder releases it. So even a wrong eviction decision
could only cost accounting and a redecode, never memory under a reader.
"Pinned" (not evictable) is `use_count() > 1` read under the cache lock. That
read is exact: a new reference can only come from the cache, which needs the
lock, and copying a consumer's reference requires one to exist. The JPEG
decode runs outside the lock; a second caller wanting the same key waits for
the decode in flight rather than repeating it. As a result the render
thread's `cachedSection` never waits behind a decode (before J,
`sectionImage` and `renderBlock` held the lock through the decode).

**Decoded sections** (`Compositor`, owned by `MapView`):

| Rule | Value | Why |
|---|---|---|
| Budget | 128 MiB high / 112 MiB low (evict to low when an insertion or `trim()` finds the cache above high) | Measured over the 1371 shipped maps: median 21 MiB, p90 72, p99 173, max 402; Ulasem Arena 64, Athri Cay 79. 97.7% never reach the budget and behave exactly as before J (all referenced sections decoded at load and kept) |
| Evictable | unpinned **and** unused for 10 s of the owner's clock | The budget bounds cold images, never the camera's working set; pure LRU cycled sections on a map whose working set exceeds the budget (rejected below) |
| Order | least recently used (a use counter, not time) | |
| Prefetch at load | only into free budget (below low); never evicts | A map larger than the budget is not decoded whole and then trimmed; demand requests (the fallback needs a section now) go first |
| Render thread | never decodes in normal play; asks the decode worker; `finishChunks` frames decode in place | Captures and the briefing backdrop stay complete |
| `trim()` | after each compose job, once per frame | Releasing a pin does not reach the cache |
| Clock | `MapView::nowMs()`: wall time, or the render study's presentation clock | Repeatable study runs; a restarted clock resets use times |

Tools, validators and the Cartographer's own compositors keep the default
(unbounded). The Cartographer's `MapView` uses the game's budget.

**Mips** (`TerrainChunks::mips_`, the chunk worker's box-filtered levels):
worker-owned least-recently-used cache, 64 MiB high / 56 MiB low. All levels
of Ulasem Arena are about 21 MiB and Middle Earth V1's measured use is 34 MiB,
so the budget is a ceiling, not a working limit. Pins are the running job's.
**Idle trim:** after 30 s and 120 frames with no terrain job queued or
completed, the render thread posts a request and the worker drops its mips
between jobs; a job that arrives first cancels the request. The cache is
touched only by the worker, so the trim is thread-safe by ownership.
Composed chunks are unaffected; mips regenerate on demand.

**Retained renderer buffers** (`client/cpubudget.h`, `GameView::trimRetainedBuffers`):

| Buffer group | Members | Need measured |
|---|---|---|
| Body buffer | `bodyVerts_` | size at frame end (assembled + staged uploads) |
| Frame lists | `paintItems_`, `drawOps_`, `copyTasks_`, `distantModelItems_`, `bodySources_`, `bodyRanges_`, arena firsts/counts/ranges, `visUnits_`, `visGeom_`, `visCost_`, `geomIndex_`, `airShadows_` | size at frame end; peak before each refill for `bodyArenaRanges_` (per command) and `airShadows_` |
| Shadow scratch | opaque page scratch, mask batches (map, trimmed whole), composite batch, prep items, tile decision arrays, F's per-cell stamps (sized by index bound, capacity never released by F) | peak before each per-page / per-texture refill |
| Render-thread model scratch | `tris_`, `triBatch_` (drawUnit, debris, shot models) | peak before each refill |
| Worker scratch | one `WorkerScratch` per pool participant: collector triangles, sort order, prepared piece tree, stage key, shadow runs/vertices, collector vertices (were `thread_local`s) | after every unit build |

Policy, per buffer (`TrimTracker`): the frame's peak need is folded into
half-window buckets (each ≥ 2.5 s **and** ≥ 60 frames by default). Decisions
happen only at bucket boundaries, over the last two buckets (one whole
window). A trim happens when capacity > 2 × the window's peak (floor 64 KiB),
and it shrinks to peak + 1/16, keeping the contents. A buffer that regrows
within four windows of a trim doubles its window (up to 8×); a trim that
holds four windows halves it. The shrink runs at the end of the frame, after
the buffer's last use and with no pool pass running. Its cost is one
allocation plus a copy of the live contents. Sustained demand never trims, a
single quiet frame never trims, and oscillation shorter than a window never
trims (all unit-tested).

**Worker scratch ownership.** `ThreadPool::participant(pool)` is 1..n−1 on
the pool's workers and 0 on every other thread, i.e. on the pool's caller, the
render thread. Worker k touches slot k only inside a chunk. The pass protocol
orders those accesses with the render thread: the caller acquires the chunk
count before a pass returns and releases the claim word before the next pass
starts. Between passes the render thread may therefore read, account and
trim every slot, including the slots of workers that never run again, which a
`thread_local` could not offer. A debug check throws if slot 0 is used off
the render thread. The debug verifiers' own `thread_local` copies allocate
only while a verifier is on and are excluded.

**Accounting** (all CPU, all gauges at frame end; GPU and RSS separate):

| Component | used | retained | pinned (not evictable now) | evictable (what budgets/trims may release) |
|---|---|---|---|---|
| Geometry entries (C) | vector sizes | capacity + entry storage + index/metadata | entries of units in view (`geom_pinned_bytes`) | absent retained entries + warm free capacity (C's 2 s / 32 MiB / 8192 and 8 MiB / 4 s rules) |
| Body buffer, frame lists, shadow scratch, model scratch, worker scratch | size × element | capacity × element (F's allocator records included in shadow scratch) | this frame's contents | capacity above them (J's trims; F's allocator records follow F's policy) |
| Decoded sections | image bytes | the same | pinned + young (`terrain_section_pinned_bytes`, `_young_bytes`) | the rest (budget) |
| Mips | image bytes | the same | the running job's (`terrain_mip_pinned_bytes`) | the rest (budget, idle trim) |
| Arena records (H), distant-image metadata (G) | as reported | as reported | — | their own policies |

`cpu_cache_used/retained/pinned/evictable_bytes` sum these.
`cpu_cache_peak_bytes` is the highest retained total seen at a frame end.
**Excluded, and why:** GPU memory (`gpu_tracked_bytes`: tracked textures and
GL buffers, a separate budget); whole-process RSS (sampled by the benchmark
from `/proc`, including everything below); assets and their CPU copies
(model/texture/palette sources, feature and effect art, fonts: static per
load); the snapshot frames (`Frame` triple buffer: the simulation mirror, not
a cache); effect batches bounded by effect counts (particles, debris,
overlays, fog mesh and upload buffer, minimap pixels); the simulation;
SDL's and the driver's own queues and mappings; allocator overhead and
fragmentation (the numbers are capacities, not malloc usable sizes);
transient peaks inside a frame, such as old and new storage during a
reallocation (the peak is sampled at frame end).

### Camera-driven terrain work (workstream K)

**What terrain work is.** Terrain is drawn from 1024-world-px chunk images
(`TerrainChunks`, one per cell and level; level n has 1/2^n the texels of the
same world area) composed on one worker thread from decoded sections and
uploaded on the render thread. Where a visible cell has no image at its
wanted level, `draw()` shows the nearest cached level (a stand-in); where it
has none at all, `MapView`'s section fallback draws the cell's 32-px tiles
from the section textures. The fallback is **texel-identical** to a chunk
(`terrain_cache` checks it on GL), so a "hole" is invisible as long as its
sections are resident; only a missing section shows the blurry minimap
underlay (`terrain_underlay_tiles`). A chunk is therefore a draw-cost cache:
one copy per cell instead of ~1024 tile quads.

**Before K** (`c2c25707` … `4515fbab`): jobs for the visible cells were queued
in row order, the worker took them oldest first (FIFO), at most 64 pending;
nothing cancelled queued work except `clear()` / `invalidate()`; completed
results for cells no longer visible were composed in full and then dropped
at upload; uploads were capped separately (chunks 8 MiB per frame, sections
32 per frame); eviction was LRU over every image not visible at its level,
stand-ins included. Under fast camera motion the queue filled with stale
work: in the new `terrain-sweep` scene 82–186 of 83–186 completed jobs per
run were thrown away (worker time 640–1400 ms per run), and the visible cells
of the moment were starved (64 stale jobs ahead of them).

**Policy** (`client/terrainwork.h`, pure, unit-tested in `terrain_work`):

| Rule | Value | Why |
|---|---|---|
| Tiers | hole (visible, no image of any level) < upgrade (visible, a stand-in exists) < nearby (guard ring, wanted level) < speculative (keep band, or the level a zoom just left) < obsolete | the cells without any image first; the camera's centre first within a tier (squared distance in 1/64 cell²) |
| Guard / keep band | 1 / 2 cells around the view | spatial hysteresis: work for a cell that leaves the view by one or two cells stays queued (last), so small reversals do not cancel and requeue it |
| Level grace | the previous level stays speculative until **both** 1000 ms and 30 frames have passed since the change; only one level away | a zoom oscillating across a level boundary keeps both levels' work; ms makes it frame-rate independent, frames make it robust to a stall |
| Cancellation | pending jobs classified obsolete are cancelled every frame; a queued job is removed, a running one stops at its next row check (cooperative; it drops only its pins, J); completed ones are discarded | lifetime-safe by J's job-scoped pins; nothing is invalidated wholesale on camera motion |
| Full queue | visible work may displace the worst nearby/speculative job | stale work never blocks the view |
| Calm gate | no new job while the camera moves faster than half a view per second (owner's clock, measured between frames of the same view size) **at level 0** (`calmGateMaxLevel` = 0), or jumps again within 1 s of a jump **at any level** (`stormGateMaxLevel` = 5); until 250 ms calm | fast motion, level 0 (measured): a unique 4 MiB chunk per passing cell costs the render thread more (upload + driver work) than the fallback that already draws the cell at full detail, and is evicted before reuse on a large map. Not from level 1 (zoom ≤ 0.5): a chunk image is 4^level times smaller there while the fallback still draws every 32-px tile (4^level times more tiles per screen pixel, resampled, so not texel-identical either), and a panned-over cell stays in view long enough for its image to pay off; gating motion at every level (K5) made zoomed-out fast pans 1.4× / 3.2× slower (verifier). Jump storms, every level: a storm's views are left within a second; without the gate at level 1 (K6) a 7680×2160 storm composed and uploaded every view and ran 1.7× slower (Experiment 13c). A single jump (minimap click) is never gated |
| Eviction ranks | 1 cold (not visible, not a stand-in) < 2 the uploaded cell's own stand-in < 3 another visible cell's stand-in; LRU within a rank | keep useful lower-detail stand-ins while their replacement is prepared |
| Off-view uploads | nearby / speculative results go only into free room or replace cold images outside the keep band | they never evict anything the view may need; the 256 MiB chunk budget and the GPU cap are unchanged |
| Upload budget | one per frame, shared by chunk images and fallback sections: urgent (hole fills: chunk images of holes, sections) up to 32 MiB and 6 ms estimated; non-urgent (upgrades, off-view) only while the frame's estimate stays under 2 ms; chunks keep the 8 MiB cap; the first upload of a frame always proceeds; finishChunks frames: sections unlimited | holes first, and upgrades wait for the next frame instead of adding to a hole-filling frame's spike |
| Cost estimate | CPU submission time of texture create + upload per byte, exponential average per kind (sections, chunks) | measured cost, no GPU synchronization (the estimate prices the render thread's time) |
| Nearby prefetch | experimental, off (`TAK_TERRAIN_SCHEDULE=nearby`) | +56–80 MiB tracked GPU in ordinary pans with no measured gain |

**Order inside a frame** (`MapView::draw`): `UploadBudget::beginFrame` →
`TerrainChunks::prepare` (level, visible cells, demand, stand-in marks, cold
bytes, re-rank / cancel pending, upload hole fills; if no hole remains, the
rest too; queue new work unless gated) → `draw` → the fallback (its sections
as urgent uploads of the same budget) → `uploadDeferred` (upgrades and
off-view results the frame left, shown from the next frame).

**Threading.** The render thread owns every decision; per job it shares a
`Control` (atomic cancel flag + atomic priority) with the worker. The worker
picks the queued job with the smallest priority (oldest among equals, so the
legacy policy is FIFO) under the queue lock, after dropping cancelled jobs.
Completed results move to the render thread's `ready_` list; the worker
stops composing while `done_` + `ready_` hold 8 results (`readyHeld_`), so
deferred results cannot pile up. Every store to `readyHeld_` happens under the
queue lock (the worker reads it in its wait predicate; an unlocked store
between the worker's check and its wait would lose the wakeup, and nothing
else notifies while the camera rests — found by the verifier, fixed in
`918300a4`).

**Reference switch.** `TAK_TERRAIN_SCHEDULE=legacy` (debug) restores the pre-K
policy in the same binary (FIFO, no cancellation, no gate, plain LRU, separate
caps); `fifo`, `nocancel`, `nogate`, `nostandin`, `nocoord` switch single
parts off, `nearby` switches the prefetch on, `gateall` applies the calm
gate's fast-motion rule at every level (the rejected K5 scope) and
`stormlevel0` limits its jump-storm rule to level 0 (the rejected K6 scope). Tests pin the policy with
`MapView::setScheduleParams`.

### Unit broad phase and compact records (workstream L)

**Before L** (`c2c25707` … `6cf541b0`): every frame the painter pass walked
all of `front().live` and ran, per unit, the dead/embarked test, a
`unitType_` hash lookup ("is it registered?"), the fog test, the lifted
anchor's frustum test and the flyer layer test. The same walk over every
unit was repeated by four per-unit overlays (health bars, self-destruct
numbers, control-squad markers, production bars) and by the queued-build
ghost pass. With 10,000 units and ~100 in view (`total10k-subset` 1280×960)
the painter walk was 0.61–0.70 ms, the overlays 0.30 ms and the ghost walk
0.16 ms of a ~2.4 ms draw. Features already had `FeatureIndex` (with the
`TAK_FEATURE_FULL_SCAN` reference); units had nothing.

**Ownership.** The index is snapshot data. `captureFrame` (sim worker, or
the main thread when the sim runs inline) already copies every unit into the
`Frame` it is about to publish; `captureUnit` now also leaves the copied
position in its `CaptureSlot`, and the serial tail -- which already walks
every slot in order to build `Frame::live` -- collects the positions of the
live units and builds `Frame::unitGrid` (`client/unitindex.h`). The grid
therefore lives in the same ring slot as `live`, swaps with it, is read only
by the render thread through the pinned `front()`, and never touches the
simulation (`World` is not read, nothing is hashed). Its build time is
recorded in the frame (`unitGridBuildNs`, tallied as `w_unit_grid_build` on
the first frame that shows the snapshot).

**Grid.** 256-px cells over the bounding box of the snapshot's finite
positions (doubled until each side has at most 256 cells), built by a
counting sort: per cell the live indices in ascending order, cells
row-major, so one grid row of a query rectangle is one contiguous run.
`query(rect)` marks the runs of the rectangle's rows in a bitset and emits
the set bits, i.e. the candidates in ascending live order without a sort;
`covers(rect)` (no non-finite position, stored exact extremes inside) lets
the caller take the whole list without a query. The cell of a position is a
monotone function of it clamped to the grid, so every point of a rectangle
falls in the rectangle's cell range: a query is a superset, never a subset.
Non-finite positions (never produced) are returned by every query, a NaN
rectangle returns everything.

**From a screen test to a world rectangle.** Every unit pass that culls by
screen position tests the lifted anchor of the snapshot position,
`sx = (x - offX)·zoom - liftX·zoom`, `sy = (z - offY)·zoom - liftY·zoom`,
against a band (shifted by the pass's own offset). `anchorBandToWorld`
turns the band into the world rectangle the position must lie in for some
lift in `LiftBounds`, widened by 1 world px + 1 screen px for the exact
test's float evaluation (its error is far below 0.01 px at map scale).
`unitpass::*` holds, for the painter cull and the four overlays, the
renderer's position expression, its acceptance test and its band; the
renderer calls these, so `unit_index_test` checks the arithmetic the
renderer runs.

**Why each requirement of the brief is covered** (the query must be
conservative for the exact pass that follows):

| Input | How |
|---|---|
| Interpolation | No pass reads the interpolated position: all cull the snapshot `x`/`z` the grid is built from. The painter cull's margins (160 px left/right, 260 top, 120 bottom) are what stand for interpolation and model extent, as before. |
| Terrain lift | `unitLiftBounds()`: ground units lift by `heightAbove · scale` (heights above the modal datum, floored at 0); flyers by their smoothed datum, which never exceeds the dilated sector maximum of the same heights. Both lie in `[0, (max height − datum + 1) · scale]` (scales may be negative or zero under debug settings), cached per map, heights, datum and scales. |
| Airborne altitude | Not an input of any culled pass (the painter cull's 260-px top margin stands for it); a flyer's lift is its datum, covered above. Tactical Dots (which add altitude and interpolation) keep their full scan. |
| Large extents | Unchanged margins; footprint-dependent overlay offsets (squad markers, production bars) use a band covering every type's footprint depth in the registry. |
| Replacement models and corpses | Culled by the unit's own `x`/`z` like before (the replacement is drawn elsewhere, but the cull never looked there); corpses are live snapshot records and so grid entries. |
| Camera shake | The shake is applied to the map offset before the passes; the query reads the same offset. |
| Viewport edges and clipping | The bands are the passes' own constants and view sizes; edges are tested ulp by ulp. |
| Fog of war, registration, dead/embarked, layers | Stay in the exact per-unit test. |
| Order | Candidates come in ascending live order and the exact test runs in that order, so painter items, depth keys, the stable sort's equal-depth order and overlay batch order are exactly a full scan's. |

**Compact records.** With every unit in view the index cannot narrow
anything; there the painter pass's per-unit `unitType_` hash lookup was half
of the pass (removing it in an experiment: `t_candidates` 0.75 → 0.39 ms).
`unitType_` became `UnitTypeTable` (`client/unittypetable.h`): the same map
and read API plus a dense registration flag per non-negative id, changed only
through `set()`/`erase()` together with the map, so `registered(id) ==
(count(id) != 0)` always. Making it a class exposed two writers a grep had
missed: the corpse and veteran mesh swaps give an existing unit a new visual
name. A unit's visual therefore cannot be cached by identity (and a per-thread
memo of the visual lookup in `buildUnitGeom` measured nothing), so the visual
and anim lookups stay where they are. The queued-build ghosts read
`Frame::buildGhostUnits` (live indices whose captured orders hold a build
order the pass draws, listed by the capture's serial tail): any unit not
listed has no such order and drew nothing before.

**What is still a full scan per frame** (not a spatial question, or
already exact): the minimap dots (every unit is drawn, 0.39 ms at 10,000),
the unit-effect pass (iterates `anims_` -- 10,000 entries -- for active
fire/smoke; its draw order is the hash map's iteration order, which a
separate list could not reproduce, 0.45 ms with 10,000 units), Tactical Dots
(far zoom, nearly everything visible; adds altitude and interpolation), the
HUD's unit count and squad label, and input picking (on events).

**Reference and verification.** `TAK_UNIT_FULL_SCAN=1` (debug) answers
every query with the whole list -- the pre-L behaviour in the same binary.
`TAK_UNIT_INDEX_VERIFY=1` re-runs every indexed pass over all live units each
frame (overlays and ghosts without drawing) and throws unless the same units
come out in the same order with the same keys, and checks the registration
record for every live unit; the study prints `UNIT_INDEX_VERIFY frames passes
live candidates accepted health selfdestruct squads production ghosts`.

### Structure palette: baked or indexed (workstream M)

**Before M** (`c2c25707` … `e90c79a0`): structures are lit per polygon the
way retail lights them, through the faction shade table
(`palettes/<bank>_textures.shd`): colour = palette[shade[level][index]] for
the polygon's light level 5..24 (`retailModelShadeLevel`). The renderer drew
that from "sheets": each palette-capable texture frame expanded into its 20
reachable bands, laid out 4 across and 5 down (a 4w×5h RGBA rectangle), packed
best-fit with a guillotine split into 2048² pages; a polygon chooses its band
through its texture coordinates (`collect`, `paletteLighting` = structure).
That is 80 bytes per source texel: the 51 structure types' 1163 frames need
130.8 MiB of expanded texels on 9 pages = **144 MiB of GPU textures** (≤ 16
pages, 256 MiB ceiling), uploaded when a structure type first comes into view.
CPU side, each of the 3008 palette-capable textures kept its own copy of its
bank's palette (1 KiB) and shade table (8 KiB) and its frames as RGBA decoded
through an index-identity palette: 39.4 MiB. The original per-texture SDL
textures (`textures_`, colour-atlas sources) are a separate store, unchanged
by M.

**Layout once, two forms** (`client/palettesheets.h`, pure). `PageLayout` is
the former packer verbatim, as a layout only: `find` (best-area free
rectangle), `addPage` (the caller decides whether a page may be added: GPU
budget), `commit` (the same guillotine split). `palette_sheets_test` runs the
pre-M loop next to it on 40 random rosters incl. oversized frames and the page
cap: identical placements. The layout is held in exactly ONE form
(`GameView::paletteMode_`):

| | Baked (pre-M form) | Indexed |
|---|---|---|
| Per layout page | 2048² RGBA page (16 MiB) | 1×1 key texture (the runs' texture) + 512² RGBA page: index, owner sheet (16 bit), alpha (1 MiB) |
| Per frame | 20 bands expanded (80 B/texel) | the frame once at (x0/4, y0/5) (4 B/texel) + a 4-texel sheet-table entry |
| Shared | — | 2048×1 identity texture (8 KiB), LUT 256×256 RGBA (8 banks × 32 levels, 256 KiB) |
| Drawn by | SDL (or the native batch with SDL's program) | the indexed GLSL program (native only) |
| Where | every renderer; bilinear unit filtering; any failure | SDL `opengl`, GL ≥ 2.0 program builds, native draws possible (integer output scale), units point-sampled |

The packer only ever advances x by sheet widths (4w) and y by heights (5h),
so every sheet origin has x0 % 4 = 0 and y0 % 5 = 0: (x0/4, y0/5, w, h) is
exact, disjoint from every other sheet's rectangle and inside rows [0, 410)
(tested); rows 410..511 hold the sheet table (x0/4, y0/5, w, h, bank; 13,056
entries per page). The same arithmetic maps EVERY baked page texel (X, Y)
to index texel (X/4, Y/5), which lies in the indexed rectangle of exactly
the sheet whose baked rectangle holds (X, Y): floor(X/4) ∈ [x0/4, x0/4 + w)
⇔ X ∈ [x0, x0 + 4w) because x0 % 4 = 0 (likewise for Y and 5). Each index
texel therefore records its owner (sheet id + 1; 0 where no sheet was
written), and the indexed page knows what the baked page holds at every
texel, not only inside the polygon's own sheet.

**Exactness of the indexed draw** (`client/paletteindexed.h`):

- *Coordinates.* A baked coordinate is k/2048 (sheet origin and band offsets
  are whole texels; the expression `uv.x + (band + corner)·uv.w` is exact in
  float, tested for random origins). `collect` adds 2·s (s = the sheet id, u
  and v carrying s mod 4096 and s / 4096): c + 2·s needs at most 13 integer
  and 11 fraction bits, so it is exact, and the vertex shader's
  `s = floor(c'/2), c = c' − 2s` recovers SDL's coordinate bit for bit
  (tested for every k and 140 sheet ids). The interpolated coordinate is
  therefore the very value SDL's program would have sampled the baked page
  with.
- *Which texel.* NEAREST texel selection is left to the texture unit: a
  2048×1 texture holding each column's own number is sampled at (u, ½) and
  (v, ½), so the shader learns the baked-page texel the hardware would have
  read, whatever its sub-texel rounding. (`floor(u·2048)` in the shader would
  depend on the GPU's rounding at texel edges.)
- *Which colour.* `palette::decodeTexel` names the band containing the texel
  and the texel inside it (bands next to each other in the sheet: a sample on
  a band's edge reads the neighbouring band, exactly like the baked page);
  colour = LUT[bank][level][index] (an RGBA8 texel: the same normalized bytes
  as the baked texel), alpha = the index page's alpha byte; output =
  colour × vertex colour, SDL's `SHADER_RGBA` expression. The CPU decode is
  tested against the baked expansion at every texel of 120 random sheets
  (2.87 M texels).
- *Outside the polygon's own sheet.* The interpolated coordinate can land on
  (or, by float overshoot, just past) a sheet's outer edge: right/bottom edges
  of the 4th-column/5th-row bands, left/top edges of the 1st. The baked page
  then shows a neighbouring sheet's texel, or page memory no sheet was written
  to. The shader does the same: for a texel outside its own sheet it reads
  the owner at (X/4, Y/5), decodes with THAT sheet's table entry (its origin,
  size and bank) and returns the neighbour's colour and alpha; with no owner
  it returns transparent black (0, 0, 0, 0). `palette::IndexPageModel` is
  the CPU version; `palette_sheets_test` checks it against full baked pages
  at every one of 8.4 M texels (own sheet, neighbouring sheet and unused
  area), and `palette_indexed_test` requires pixel identity in every GL case
  while a marked program set counts the pixels that took the owner path
  (1326 over 48 cases). The first M builds lacked the owner and repeated the
  sheet's edge texel there: 7 px/frame in `structures` 1280×960, 30 at
  7680×2160, 40 at zoom 0.4 (Experiment 15); that difference is gone
  (Experiment 15b: 0 px everywhere against pre-M). One assumption remains:
  never-written baked page memory reads zero. SDL's GL backend creates the
  page without data (GL leaves it undefined); NVIDIA returns zero, so the
  forms agree here. On a driver that returns garbage the baked form shows that
  garbage, and the indexed form stays transparent.
  `TAK_PALETTE_MARK_EDGES=1` (debug) paints those decodes magenta to show
  where the owner path runs.
- *Bilinear* (Texture Filtering Units/Everything) is not bit-identical: the
  shader filters four resolved COLOURS (never indices; neighbouring sheets
  included, as the baked page bleeds them) with 8-bit weights, and the
  texture unit's arithmetic differs in the last bit (max channel error 3). So
  bilinear unit filtering keeps the baked form; `TAK_PALETTE_INDEXED_LINEAR=1` (debug) runs the experimental
  linear program. No gamma/colour-space change (8-bit UNORM in, the same
  multiply), no premultiplication (straight alpha, BLEND as before).

**Native submission.** Indexed runs have no SDL equivalent, so they always
draw natively (`GeometrySubmit::NativeProgram`: SDL primes target, viewport,
scissor, projection and blend with the run's key texture, the program
replaces SDL's, then the saved program, units 1-3 and active unit are
restored -- SDL's cached program and unit-0 binding stay true). Per-run
priming cost stationary-mixed +1.5 ms of `t_body_submit` (311 primes + flushes
per frame; rejected, Experiment 15), so in the body replay an indexed run
opens a **native span**: the commands after it, up to the next feature,
special unit, the air-shadow boundary or a texture that cannot join, draw
under ONE priming (`GeometrySubmit::beginPass/passDraw/passDrawBuffer/endPass`,
`PaletteIndexedGL::beginPass/usePage/usePlain/usePremultiplied/endPass`).
Joining textures: BLEND (the primed mode; palette keys are BLEND) or the
distant-image pages' premultiplied custom mode (ONE, ONE_MINUS_SRC_ALPHA; the
primed factors are queried and restored), no colour/alpha mod (SDL folds those
into vertex colours, the native draw would not), normalized coordinates.
Plain runs draw with SDL's own `TEXTURE_VERTEX_SHADER` + `texture2D × colour`
sampling unit 4 (unit 0 is never touched). Arena commands (H) draw from their
GL buffer ranges inside the span. Spans open only at an indexed run, so a
scene without structures, and every baked-form frame, replays exactly as
before. A special unit (`drawUnit` → `drawRunsNative`) with an indexed run
draws all its runs in one pass the same way (`ab9def88`). Distant-image
bakes draw each run through DistantModels' run drawer (`drawModelRun`: an
indexed run as one native draw with its own priming, any other run with
`SDL_RenderGeometry` as before). GameView installs that drawer in
`updatePaletteMode()` in every build, next to the decision that may choose
the indexed form (the first M builds installed it inside a debug-only block,
so a release build baked indexed runs with plain SDL: grey structures in
distant images; fixed in Experiment 15b). Debug `TAK_BODY_SDL_SUBMIT` (the
pure-SDL submission reference) selects the baked form, so a reference run
opens no span and draws nothing natively.

**Switching, reset, failure.** `updatePaletteMode()` runs once per frame
before the structures' sheets are prepared. A change of form releases the
held form's pages (never both resident), restarts the layout and makes every
geometry entry cold, clears the distant-image cache and releases the body
arena (all captured the old textures/coordinates); sheets are rebuilt lazily
from the CPU sources as structures come into view (the baked fallback is
reconstructible, never kept beside the indexed form). Triggers: Texture
Filtering crossing Off ↔ Units, the GL program/entry points missing, native
draws impossible (other renderer, fractional output scale), an indexed draw
failing (sticky for the session, logged once), a GL context change (the
index pages are forgotten, not deleted, and the keys that named them rebuilt
like a switch). A render-target reset touches neither form (static textures,
as before). Allocation: an index page counts 1 MiB against the gpuvram cap
(`addBuffer`; GL errors after creation are checked and noted as a failure); a
refused page leaves the frame on the colour atlas exactly like a refused
baked page did.

**Animated frames, player colours, expiration.** The frame choice is the
collector's, unchanged (animated textures: the sequence's current frame;
10-frame logos: the player-colour slot; a frame without a sheet falls back to
the atlas as before) -- each frame is its own sheet in both forms, so the
same triangle gets the same sheet. Shadow coverage does not read palette
sheets (shadow masks come from `textures_`), so it is unchanged.

### Telemetry reference (RENDERSTATS keys)

`t_*` = render-thread wall ms (nested: `terrain_upload` in `terrain`;
`shadow_alloc/prep/raster` in `shadow_pack`; `pool_dispatch/wait` and (since
I) `schedule_plan` in whichever phase ran the pass) — never sum them. `w_*` = CPU ms summed over threads.
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
| Distant-image lookup (G) | `distant_body_deferred` (bodies left pending behind a held image), `distant_body_resumed` (pending bodies generated by the pass), `distant_body_catchup` / `distant_body_late` (missed predictions, generated after/inside `prepare()`), `distant_bounds_reused`, `distant_page_releases`, `distant_migrated`; gauges `distant_pages`, `distant_tiles`, `distant_metadata_bytes`. `verts_generated`/`verts_placed`/`verts_recoloured` count body vertices only for body stages that ran |
| Retained body vertices (H) | `arena_append`, `arena_rebuild`, `arena_stream` (0/1 per frame), `arena_verts_retained` (resident, nothing copied), `arena_verts_uploaded` (also in `body_verts_copied` and `bytes_uploaded`), `arena_verts_twice` (of those, vertices also copied into a small assembled command the same frame), `arena_draws`, `arena_ranges`, `arena_fallbacks`, `arena_releases`; gauges `arena_capacity_bytes` (also in `gpu_tracked_bytes`), `arena_inflight_bytes`, `arena_cpu_bytes`. Since H `body_verts_copied` = vertices actually copied (assembled + arena uploads) and DRAWPHASE `body_vertices` = all body vertices drawn |
| Cost-planned pool passes (I) | `t_schedule_plan` (nested: building the plans), `pool_posted` (workers woken), `pool_idle_wakes` (woken after every chunk was taken), `pool_chunks`, `sched_geometry_threads` (participants of the geometry pass; 1 = render thread alone), `sched_shadow_threads` (check + transform passes, summed), `sched_copy_threads`, `sched_geometry_estimate_us` (the plan's estimate, to compare with `w_geometry_key` + `w_geometry_build`). `w_pool_wake` / `pool_dispatches` keep their meaning; since I a wake is counted only for a worker that ran a chunk |
| CPU budgets (J) | sections: `terrain_section_cpu_bytes` (used), `_pinned_bytes`, `_young_bytes`, `_peak_bytes`, per frame `terrain_section_decodes` (real decodes by any thread since J), `_evictions`, `_redecodes`, `_prefetch_skipped`; mips: `terrain_mip_cpu_bytes`, `_pinned_bytes`, `_peak_bytes`, per frame `terrain_mip_evictions`, `_regens`, `_trims`; buffers: per frame `cpu_trims`, `cpu_trim_bytes`; gauges `worker_scratch_used/capacity_bytes`, `geom_pinned_bytes`, totals `cpu_cache_used/retained/pinned/evictable/peak_bytes` (see the J accounting table; GPU and RSS are separate). `body_capacity_bytes` is reported after the frame's trim |
| Camera-driven terrain (K) | gauges of the last terrain draw `terrain_chunk_visible`, `_ready` (wanted level), `_standin` (another level's image), `_holes` (no image: the section fallback draws the cell), `terrain_underlay_tiles` (fallback tiles without a resident section: the blurry underlay shows); per frame `terrain_jobs_obsolete` (cancelled: keep band, level grace, displaced), `terrain_jobs_aborted` (running jobs stopped by any cancel), `terrain_jobs_nearby`, `terrain_jobs_offview` (results uploaded for cells not visible then), `terrain_upload_deferred` (results the frame budget postponed), `terrain_gated_frames` (1 = the calm gate queued nothing); `w_terrain_wasted` (worker time of jobs never uploaded). The benchmark's `report.json` adds per run `terrain.hole_frames` / `underlay_frames` / `standin_frames` (share of measured frames with any) |
| Unit broad phase (L) | `units_live` (snapshot), `units_scanned` (since L: units the painter's exact test examined, i.e. the candidates), `unit_index_queries`, `unit_index_returned` (summed over the painter + overlay queries), `unit_index_rows`, `unit_index_full_scans` (whole-list answers by the reference switch or a grid/live mismatch; never expected otherwise), `unit_grid_bytes` (gauge, the shown snapshot's grid), `w_unit_grid_build` (capture-thread build of the shown snapshot's grid, once per snapshot) |
| Structure palette (M) | gauges `palette_mode` (0 none, 1 baked, 2 indexed), `palette_pages` (layout pages), `palette_gpu_bytes` (the held form: baked pages, or key textures + index pages + identity + LUT; also in `gpu_tracked_bytes`), `palette_cpu_bytes` (sources, banks, sheet records); per frame `palette_upload_bytes` (texture bytes uploaded for sheets and LUT rows), `palette_indexed_draws` (runs drawn by the indexed program, spans included), `palette_fallbacks` (indexed runs that failed to draw: the form turns baked), `palette_switches`, `palette_spans` (native spans opened, one priming each), `palette_span_commands` (body commands drawn inside them) |
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
| pan-distant | Ulasem Arena | stationary-mixed (zoom 0.5) under the sinusoidal pan: held distant images re-placed every frame (added by workstream G; 1280×960 only acts — 7680×2160 clamps to zoom 0.73) |
| distant-sparse | Ulasem Arena | 6000 units at zoom 0.25 (three image pages), then a corner of the army from frame 450: images expire, pages are released (added by workstream G; segments army / corner) |
| bigmap-zoom | Middle Earth V1 | pan + continuous zoom 0.25–1 on a map whose decoded sections (238 MiB) exceed the CPU budget (added by workstream J) |
| terrain-sweep | Middle Earth V1 | fast elliptical sweep 9000×6000 px, period 480 frames (up to 118 px/frame), reversals in both axes (added by workstream K) |
| terrain-zoom-osc | Middle Earth V1 | zoom 0.42–0.6, period 90 frames: the chunk level flips 0↔1; at 7680×2160 both levels exceed the 256 MiB chunk budget (added by workstream K) |
| terrain-jumps | Middle Earth V1 | jumps between five far-apart views every 20 frames (0.33 s per view: shorter than a view takes to compose; adversarial) (added by workstream K) |

Body-arena tooling (workstream H): ctests `vertex_arena`, `geometrysubmit`
(run with `TAK_GEOMETRY_TEST_ACCELERATED=1 SDL_VIDEODRIVER=offscreen` for the
GL comparisons), `render_body_arena`, `render_body_arena_still`; debug knobs
`TAK_BODY_ARENA_OFF` (same-binary pre-H reference), `TAK_BODY_ARENA_VERIFY`,
`TAK_BODY_ARENA_SOFTWARE`, `TAK_BODY_ARENA_MIN`, `TAK_BODY_ARENA_HEADROOM`.
Per-frame analysis by arena mode: `~/tak-tmp/render-opt/h/permode.py
<run>/stdout.log` (assembly mean/p99/max, submit, uploads per Append / Rebuild
/ Stream frame).

Schedule tooling (workstream I): ctests `work_schedule`, `render_schedule`;
debug knobs `TAK_RENDER_SCHEDULE=uniform` (pre-I uniform passes, same
pool), `TAK_RENDER_POOL_THREADS=N`, `TAK_SCHEDULE_GEOMETRY|SHADOW_CHECK|
SHADOW_PREP|COPY=serialBelowNs,workerNs,chunkNs[,maxThreads]`,
`TAK_SCHEDULE_VERIFY`, `TAK_SCHEDULE_SAMPLES=<file>`. Experiment 11
commands (scripts in `~/tak-tmp/render-opt/i/`: `measure.sh`, `chain.sh`,
`fit.py` for the per-unit fit, `mdrows.py` for the tables):

```sh
S="small-battle stationary-mixed moving total10k-visible"
B="python3 tools/render_benchmark.py --data ~/tak_data --cpus 16-23 --wait-quiet 600 --repeats 2"
$B --a ~/tak-tmp/render-opt/takclient-H5-candidate --b ~/tak-tmp/render-opt/takclient-I4-candidate \
   --scenes $S --sizes normal --output ~/tak-tmp/render-opt/runs/i4-normal      # and uhd
# planning alone (same binary):   --a I4 --b I4 --a-env TAK_RENDER_SCHEDULE=uniform
# pool size alone (I2):           --a I2 --b I2 --a-env TAK_RENDER_POOL_THREADS=24
# captures:                       --capture-only (H5 vs I4; I4 serial vs I4: --a-env TAK_RENDER_POOL_THREADS=1)
# calibration (serial per-unit/per-pass samples):
python3 tools/render_benchmark.py --a build-o2/takclient --data ~/tak_data --cpus 16-23 --repeats 1 \
   --scenes moving --sizes normal --frames 420 --warmup 120 --output <dir> \
   --env TAK_RENDER_POOL_THREADS=1 --env TAK_SCHEDULE_SAMPLES=<file>
```

CPU budget tooling (workstream J): tests `terrain_compositor` (synthetic
JPEGs: pins, LRU + watermarks, working set, redecodes, concurrency),
`terrain_cache` (retail data: a 4 MiB section / 1 MiB mip budget renders every
finished frame texel-identical to unbounded caches; idle mip trim and
regeneration), `cpu_budget` (trim policy), `work_schedule` (participant
indices), `render_cpu_budget` (in game, software renderer, battle then empty
view with 400 ms trim windows and 1 s terrain windows, `TAK_CPU_TRIM_VERIFY`).
Scene `bigmap-zoom` (Middle Earth V1: 255 sections, 238 MiB decoded, above the
budget). Section sizes of every shipped map: `~/tak-tmp/render-opt/j/secsize.cpp`
→ `secsize.tsv` (size MiB, sections, decode ms, blocks, name). Experiment 12
script: `~/tak-tmp/render-opt/j/measure.sh`.

Terrain work tooling (workstream K): tests `terrain_work` (policy, no SDL:
tiers, priority, keep band, level grace, calm gate, eviction ranks, upload
budget, queue simulations of pan/reversal/zoom oscillation/jumps/large map),
`terrain_schedule` (real MapView on a generated 512×512-block map, software
renderer, game data), `terrain_cache` (existing; runs the K policy).
Harness knobs (debug, `main.cpp`): `TAK_RENDER_SWEEP=cx,cz,ax,az,period`,
`TAK_RENDER_ZOOM_OSC=lo,hi,period`, `TAK_RENDER_JUMP_AT="x,z;x,z;…"`;
`TAK_TERRAIN_SETTLE=1` (capture runs: every frame waits until the visible
chunks are ready, gate lifted; gives up for good after one 10 s wait);
`TAK_TERRAIN_SCHEDULE=legacy|fifo,nocancel,nogate,nostandin,nocoord,nearby`
(same-binary reference / ablation). Experiment 13 script:
`~/tak-tmp/render-opt/k/measure.sh`; summaries `abcmp.py` / `tq.py` (scratch,
copied to `~/tak-tmp/render-opt/k/`).

Unit broad-phase tooling (workstream L): tests `unit_index` (pure: grid vs
brute force, every pass's band vs its exact float test at ulp-located edges,
movement/churn, `UnitTypeTable`), `render_unit_index` and
`render_unit_index_jump` (software renderer, `TAK_UNIT_INDEX_VERIFY`; costed,
not `quick`). Debug knobs `TAK_UNIT_FULL_SCAN`, `TAK_UNIT_INDEX_VERIFY`,
`TAK_RENDER_SHAKE`, `TAK_RENDER_OVERLAYS`. Experiment 14 script:
`~/tak-tmp/render-opt/l/measure.sh`.

Palette tooling (workstream M): tests `palette_sheets` (pure: layout vs
the pre-M packer, alignment, coordinate round trips, decode vs baked
expansion, `IndexPageModel` vs full baked pages at every texel),
`palette_indexed` (GL with `TAK_GEOMETRY_TEST_ACCELERATED=1
SDL_VIDEODRIVER=offscreen`: baked vs indexed NEAREST pixel-identical in every
case, outside-sheet decodes counted by a marked program set, linear reported,
native pass vs SDL, release/rebuild, budget; without the variable: the
software renderer is refused), `render_palette_indexed` (in game; the ctest's
dummy driver checks the baked form; run it with `SDL_VIDEODRIVER=offscreen
SDL_RENDER_DRIVER=opengl` for the GL form). Debug knobs `TAK_PALETTE_BAKED`
(same-binary baked reference), `TAK_PALETTE_INDEXED_LINEAR`,
`TAK_PALETTE_MARK_EDGES` (paints decodes outside their sheet magenta),
`TAK_RENDER_ROSTER=structures`, `TAK_RENDER_FILTER=N` (Texture Filtering
Off↔Units every N frames), `TAK_REQUIRE_PALETTE` (study check, prints
`PALETTE_STUDY`; `=bakes` also requires indexed runs baked into distant
images through the run drawer, `bake_runs` > 0, on OpenGL), `TAK_PALETTE_ROSTER_TEST` (`--firetest`: every structure's
sheets; prints pages, GPU/CPU/upload MiB and the pre-M source bytes). Capture
classification: `~/tak-tmp/render-opt/m/classify.py <baseline> <indexed>
<marked>` (every baseline/indexed difference must be a marked pixel).
Experiment 15 script: `~/tak-tmp/render-opt/m/measure.sh`; Experiment 15b:
`~/tak-tmp/render-opt/m5/caps.sh` (captures) and `m5/time.sh` (timing,
single runs, roster).

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
(The reuse scenes were re-timed on the final code after the verifier's
review: Experiment 8b. The rows below are kept as history.)
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

### Experiment 8b — workstream F verifier fix: no page churn when a frame does not fit (2026-10-16)

The verifier rejected F6 for one defect (page churn in the packing-failure
band, see Rejected experiments) and noted that the reuse-scene timings came
from F3, not the final code. Fix: `ce2a3466` (pages placed logically,
textures created after the frame fits; failure mark kept across the atlas
reset; verifier totals `page_allocs` / `alloc_failures`; ctest
`render_shadow_atlas_band`). Clients: **F6** (`takclient-F6-candidate`, code
of `3fd2c8de` = `b74f499e`), **F8** (`takclient-F8-candidate`, code of
`ce2a3466`), **E3** (pre-F). Build-o2, offscreen EGL, SDL `opengl`, RTX 5070
Laptop; `RENDER_ENV` confirmed renderer and output for every run (1280×960 →
world 1075×888, 7680×2160 → world 7475×2088, zoom clamp 0.73 as before);
client pinned to CPUs 16–23, 900 frames after 300 warmup, forced completion,
ABBA × 2, `--wait-quiet 60`. Load per run is in each `results.json`: load
average 2.2–17.2, 0–12 `legion_scenario` processes (on CPUs 4–15),
pinned-CPU busy fraction 0.17–0.28, no run contended. A code-review agent ran
short `build-o2` ctests unpinned at 20:05–20:06 — during the capture runs
only (captures are deterministic pixel comparisons, not timed); an earlier
timing attempt that overlapped its first test run was discarded and
restarted. Output (not in Git): `~/tak-tmp/render-opt/runs/f8-*`, script
`f/timing8.sh`, log `f/timing8.log`.

```sh
T="python3 tools/render_benchmark.py --data ~/tak_data --cpus 16-23 --wait-quiet 60"
# the verifier's repro (counters) and its control
KEYS="shadow_page_allocs shadow_page_releases shadow_fallback shadow_compactions" VCPUS=16-23 \
  python3 ~/tak-tmp/render-opt/f/srun.py <client> stationary-mixed normal 400 WARM=0 TAK_SHADOW_PAGE_MAX=720   # and =768
$T --a $F6 --b $F8 --output runs/f8-band --scenes stationary-mixed --env TAK_SHADOW_PAGE_MAX=720 --repeats 2
$T --a $F6 --b $F8 --output runs/f8-captures-normal --scenes stationary-mixed pan-snap churn --capture-only
$T --a $F6 --b $F8 --output runs/f8-captures-uhd --sizes uhd --scenes stationary-mixed pan-snap --capture-only
$T --a $E3 --b $F8 --output runs/f8-normal --scenes stationary-mixed total10k-visible moving pan-snap --repeats 2
$T --a $E3 --b $F8 --output runs/f8-uhd --sizes uhd --scenes stationary-mixed total10k-visible moving pan-snap --repeats 2
python3 ~/tak-tmp/render-opt/f/ftable.py runs/f8-normal/results.json runs/f8-uhd/results.json
```

**The defect and the fix (counters).**

| Run | Page textures created | Pages released | Fallback frames | Repack attempts per frame | Shadow-atlas GPU bytes |
|---|---:|---:|---:|---:|---:|
| verifier repro, F6, `PAGE_MAX=720`, 400 frames | ~16 | ~12 | all | 0.97 | 4 × 2 MiB pages, cycling |
| same, F8 | **0** | **0** | all | 0.98 | 0 |
| band benchmark (900 frames per run), F6 | 30 per run (every run) | 30 per run | all | 0.99 | 7.9 MiB at the last frame |
| band benchmark, F8 | **0** (every run) | **0** | all | 1.00 | 0 (tracked GPU 111.9 → 104.0 MiB) |
| control `PAGE_MAX=768` (fits), F6 and F8 | 0.01/frame (equal) | 0 | 1% | 0.10 | equal (dirty tiles 255.04 both) |

In the band one former-packer attempt per frame remains, as before F (the
pre-F code ran the packer every frame too). Band timing F6 → F8: draw mean
10.849 → 10.842 ms (−0.1%, A/A spread 2.5%), p99 16.83 → 15.93 (−5.4%,
spread 15%), t_shadow_alloc +1.6% (spread 8.2%): no material difference —
on this GPU four 720² texture creations every 2 s are not a measurable
hitch; the fix is about VRAM churn (this machine's flicker history), not
frame time.

**Correctness of the fix.** `render_shadow_atlas_band` (software renderer,
288-px pages: a still army in the band for 80 frames, then a sparse view):
77 failed frames, `page_allocs=3`, `page_releases=0`, every kept tile equal to
a re-bake; with the old immediate page creation patched back in it reports
`page_allocs=47 page_releases=44` and fails. `render_shadow_atlas`
reproduces the verifier's F6 numbers exactly (kept=9493, compactions=6,
migrated=12, page_releases=1, validated=169). `render_shadow_atlas_zoom`,
`render_shadow_cache`, `shadow_atlas`, `shadow_tiles` pass. GL verifier
(`TAK_SHADOW_CACHE_VERIFY`) on F8: battle-sparse 1280×960 700 frames with
300 ms windows (834,299 tiles, 514,991 kept, 3 page releases, no mismatch)
and stationary-mixed with `PAGE_MAX=768` (2 failed frames then active,
538,375 tiles, 470,130 kept, no mismatch). Captures F6 vs F8 identical in all
60 comparisons (stationary-mixed, pan-snap, churn at 1280×960;
stationary-mixed, pan-snap at 7680×2160; 12 frames each). World hashes
identical A vs B in every run. `ctest -L quick` 382/382 on build-o2 at
`ce2a3466`. Release `takclient` builds without the new debug strings (the
two fallback reason strings are plain constants, as before).

**Reuse scenes on the final code (F8 vs E3)** — replaces the F3-session
numbers as the record of the final code (Experiment 8's F2/F3 rows stay as
history).

| Scene, size | Draw mean ms E3 → F8 | Δ | Groups | Material (thr) | Draw p99 | t_shadow_pack | GPU timeline |
|---|---:|---:|---|---|---:|---:|---:|
| stationary-mixed 1280 | 5.966 → 4.012 | **−32.8%** | −32.6, −32.9 | yes (11.5%) | 13.34 → 5.60 (−58%, yes) | −78.6% (yes) | −41.7% (yes) |
| total10k-visible 1280 | 40.140 → 20.276 | **−49.5%** | −50.0, −49.0 | yes (6.2%) | 67.43 → 26.40 (−61%, yes) | −90.5% (yes) | −49.5% (yes) |
| moving 1280 | 9.399 → 6.277 | **−33.2%** | −32.9, −33.5 | yes (11.2%) | 13.15 → 12.63 (n.s.) | −66.2% (yes) | −36.5% (yes) |
| pan-snap 1280 | 4.409 → 3.890 | −11.8% | −13.0, −10.3 | no (28.6%) | 9.82 → 4.94 (n.s., spread 35%) | −34.5% (n.s.) | −11.9% (n.s.) |
| stationary-mixed 7680×2160 | 8.326 → 6.804 | −18.3% | −19.9, −16.7 | no (25.5%) | 14.60 → 12.56 (n.s.) | −68.3% (yes) | −18.2% (n.s.) |
| total10k-visible 7680×2160 | 60.619 → 59.785 | −1.4% | −1.4, −1.4 | no (3.2%) | 67.91 → 67.84 | −26.5% (yes; fallback frames) | −1.0% |
| moving 7680×2160 | 13.193 → 8.915 | −32.4% | −27.2, −37.3 | no (38.2%) | 17.10 → 16.29 (n.s.) | −67.3% (yes) | −37.5% (n.s.) |
| pan-snap 7680×2160 | 10.973 → 8.495 | −22.6% | −28.1, −16.9 | no (40.1%) | 16.82 → 13.72 (n.s.) | −43.3% (n.s.) | −30.1% (yes) |

Counters equal the Experiment 8 table (kept/dirty tiles per frame within
0.1%; e.g. stationary-mixed 1280 1742.0 kept / 83.0 dirty, pan-snap 7680×2160
1714.5 / 226.9); `shadow_page_allocs` 0 in every reuse scene; tracked GPU
at the last frame equal except total10k-visible 7680×2160 (306.5 → 242.5 MiB,
as before); peak RSS equal or lower. Readings: every ABBA group of every
reuse scene is faster on the final code; draw time is material at 1280×960
in stationary-mixed, total10k-visible and moving; at 7680×2160 this session
was noisier (A/A spreads 6–20%) and the −18% to −32% draw differences stay
inside its thresholds, while shadow packing is material in stationary-mixed
and moving. The earlier F3 session measured the same scenes at 7680×2160 as
material (−22% to −34%, Experiment 8), on code whose reuse frames match the
final code's (same counters). Composite cost still rises where tiles spread
over more pages (pan-snap 7680×2160 +0.14 ms, moving 7680×2160 +0.07 ms,
stationary-mixed 7680×2160 +0.04 ms; material).

### Experiment 9 — workstream G distant-image lookup before body generation (2026-10-16)

Clients: **A** = F8 (`~/tak-tmp/render-opt/takclient-F8-candidate`, code of
`ce2a3466` = workstream F, the pre-G build), **B** = G2
(`takclient-G2-candidate`, code of `2d755167`, the final G code); G1
(`takclient-G1-candidate`, code of `b084644e`) was measured first and differs
only in the two costs `2d755167` removed (a catch-up scan of every entry and a
per-unit `getenv`). Build-o2 (`-O2` Debug), offscreen EGL, SDL `opengl` on the
RTX 5070 Laptop GPU; `RENDER_ENV` confirmed renderer and output (1280×960 →
world 1075×888; 7680×2160 → world 7475×2088, zoom clamp 0.73) for every run.
900 frames, 300 warmup, forced-completion frames, client pinned to CPUs 16–23,
ABBA × 2 per scene and size, `--wait-quiet 60`. Load per run in each
`results.json`: G2 session load average 1.5–6.6, 0 `legion_scenario`
processes, pinned-CPU busy 0.18–0.26; G1 session load 2.2–10.7, 0–10
processes (on CPUs 4–15), busy 0.18–0.26; no run contended. Lean testing:
timed only the scenes G changes — stationary-mixed (913 images, 298
shape-refused bodies per frame: retained bounds), total10k-visible (4019
images, ~1800 shape/page-refused bodies per frame), pan-distant (new: held
images under a pan, deferred bodies), zoom (every image re-baked: guard) and
distant-sparse (new: page release) at 1280×960; stationary-mixed and
total10k-visible at 7680×2160 as guards (no distant image exists there: the
zoom clamp is above the 0.6 limit). Pipelined frames not measured. Output (not
in Git): `~/tak-tmp/render-opt/runs/g1-*`, `g2-*`; scripts
`~/tak-tmp/render-opt/g/timing{,2}.sh`, summary `g/gtable.py`, counter runs
`f/srun.py`.

```sh
T="python3 tools/render_benchmark.py --data ~/tak_data --cpus 16-23"
$T --a $F8 --b $G1 --output runs/g1-captures-normal --scenes stationary-mixed total10k-visible pan-distant zoom distant-sparse battle-sparse veteran effects --capture-only
$T --a $F8 --b $G1 --output runs/g1-captures-uhd --sizes uhd --scenes stationary-mixed total10k-visible pan-distant --capture-only
$T --a $F8 --b $G2 --output runs/g2-captures-normal --scenes pan-distant total10k-visible stationary-mixed --capture-only
$T --a $F8 --b $G2 --output runs/g2-normal --scenes stationary-mixed total10k-visible pan-distant --repeats 2 --wait-quiet 60
$T --a $F8 --b $G2 --output runs/g2-uhd --sizes uhd --scenes stationary-mixed total10k-visible --repeats 2 --wait-quiet 60
$T --a $F8 --b $G1 --output runs/g1-normal --scenes stationary-mixed total10k-visible pan-distant zoom distant-sparse --repeats 2 --wait-quiet 60
$T --a $G1 --b $G1 --a-env TAK_DISTANT_LOOKUP_LATE=1 --output runs/g1-late --scenes pan-distant total10k-visible --repeats 2 --wait-quiet 60
python3 ~/tak-tmp/render-opt/g/gtable.py runs/g2-normal/results.json runs/g2-uhd/results.json
VCPUS=16-23 python3 ~/tak-tmp/render-opt/f/srun.py $G pan-distant normal 400 WARM=200 TAK_DISTANT_VERIFY=1 TAK_GEOMETRY_VERIFY=1
```

**Correctness.**

- Captures: 0 differing pixels in the world view AND the full frame in all
  132 G1-vs-F8 comparisons (stationary-mixed, total10k-visible, pan-distant,
  zoom, distant-sparse — incl. frame 720 after its page releases —,
  battle-sparse, veteran, effects at 1280×960; stationary-mixed,
  total10k-visible, pan-distant at 7680×2160; 12 frames each) and in all 36
  G2-vs-F8 comparisons (pan-distant, total10k-visible, stationary-mixed at
  1280×960). World hashes identical A vs B in every run. Pixel-identical
  output was expected (every decision is the pre-G one) and is what was
  measured.
- `distant_models` (software renderer and offscreen GL): the cache against the
  pre-G cache kept verbatim (`tools/distant_models_legacy.h`) over 900 scripted
  frames: 688,281 shown images, 10,007 tile pixel comparisons, 124,906
  early-lookup predictions (their vertices poisoned), 187,045 reused bounds, a
  page release, 76 migrated images — identical decisions, quads and tile
  pixels; page bookkeeping `validate()` every frame. Five mutations fail it
  (position ignored when a page is chosen, bounds kept across revisions,
  `holds()` ignoring the image age, migration without the copy, released
  page's tiles dropped from the free list).
- `render_distant_lookup` (new ctest, software renderer): 120 frames, 6,902
  deferred bodies each regenerated in a copy (same revision, vertex count and
  shadow output, image shown), every generated body equal to a complete
  rebuild, 0 missed predictions. A mutation that also skips the shadow stages
  of a deferred unit fails it. GL verifier runs (`TAK_DISTANT_VERIFY` +
  `TAK_GEOMETRY_VERIFY`): stationary-mixed, total10k-visible, pan-distant
  (426 deferred bodies per frame), veteran — no mismatch, 0 catch-ups, 0 late
  bodies. `geometry_stages` covers `bodyVertexCountStable` (word census).
- `ctest -L quick` 383/383 on build-o2 at `2d755167` (incl. `render_distant_lookup`, `distant_models`, `geometry_stages`). Release
  `takclient` builds and contains none of the G debug strings.

**What changed in the counters (exact, mean per frame, frames 300–899, 1280×960).**

| Scene | Visible | Images (hits / held / bakes) | Refused: shape / pages / budget | Bounds reused (B) | Bodies deferred (B) | Verts placed A → B | Image pages (B, last frame) |
|---|---:|---:|---:|---:|---:|---:|---:|
| stationary-mixed | 1825 | 913 / 0 / 0 | 298 / 0 / 0 | 276 | 0 | 0 → 0 | 1 |
| total10k-visible | 8752 | 3893 / 79 / 46 | 111 / 1702 / 3 | 1763 | 0 | 0 → 0 | 4 |
| pan-distant | 1944 | 0 / 427 / 128 | 150 / 0 / 592 | 0 | 427 | 1.294 M → 1.142 M | 1 |
| zoom | 1814 | 0 / 0 / 80 | 22 / 0 / 723 | 0 | 0 | 1.206 M → 1.206 M | 1 |
| distant-sparse (corner segment) | 1024 | — | — | — | 0.1 | — | 2 (A: 4 held to the end) |

Every image, bake and refusal count is identical A vs B (the decisions are
unchanged); what G removes is the work around them. In the stationary scenes
no held image has a deferrable change (their held images change pose, and
lodestone texture frames belong to bodies too large for an image), so they
gain only from retained bounds; in pan-distant every held image defers its
body's re-placement.

**Timing, final code (G2 vs F8; spread = max−min over a label's four run
means; material = beyond `max(2 × spread, 2%)`; groups = B−A per ABBA group).**

| Scene, size | Draw mean ms A → B | Δ | Groups | Material (thr) | Draw p99 A → B | t_distant A → B | t_geometry | w_geometry_build | w_geometry_key | GPU timeline |
|---|---:|---:|---|---|---:|---:|---:|---:|---:|---:|
| total10k-visible 1280 | 19.127 → 16.761 | **−12.4%** | −11.9, −12.8 | **yes** (5.6%) | 24.87 → 22.66 (−8.9%, n.s.) | 2.724 → 0.906 (**−66.7%**, yes) | +2.1% | +0.9% | +0.9% | −8.5% (yes) |
| stationary-mixed 1280 | 3.959 → 2.878 | −27.3% | −30.6, −23.9 | no (74.8%: one noisy A run) | 5.20 → 3.45 | 0.461 → 0.080 (**−82.7%**, yes) | −31% (n.s.) | +2.9% | +10.8% (n.s.) | −4.9% |
| pan-distant 1280 | 8.589 → 8.477 | −1.3% | −2.0, −0.6 | no (5.6%) | 14.50 → 15.00 (n.s.) | 1.283 → 1.237 (−3.6%, yes) | **−6.0%** (yes) | **−15.0%** (yes) | +5.3% (n.s., both groups) | −1.2% |
| stationary-mixed 7680×2160 | 6.155 → 6.031 | −2.0% | −2.4, −1.7 | no (7.9%) | 8.70 → 8.48 | 0.003 → 0.004 (+0.001 ms) | +0.9% | −0.1% | +0.4% | +0.2% |
| total10k-visible 7680×2160 | 59.462 → 58.811 | −1.1% | −0.9, −1.3 | no (8.1%) | 66.57 → 65.76 | 0.057 → 0.068 (+0.011 ms) | −0.7% | +0.8% | −0.4% | −0.4% |

G1 session (first measured build; also F8 vs G): stationary-mixed draw
3.449 → 2.927 (−15.1%, groups −13.2 / −17.0, threshold 20.3%), `t_distant`
−82.2% (material); total10k-visible draw 19.846 → 17.784 (−10.4%, groups −5.2
/ −15.4, threshold 24.9%), `t_distant` −64.9% (material); pan-distant draw
−1.5%, `w_geometry_build` −14.7% (groups −16.3 / −13.2); zoom draw −1.6%
(groups +2.7 / −5.7), `w_geometry_key` +3.9% (groups +3.6 / +4.2, threshold
6.4%); distant-sparse draw +1.6% (groups +3.7 / −0.6, threshold 28.3%),
army segment 8.6 vs 8.7 ms, corner segment 2.9 vs 3.0 ms. At 7680×2160 G1
measured `t_distant` +0.002 / +0.04 ms, which `2d755167` reduced to the
+0.001 / +0.011 ms above. The deferral alone (same G1 binary,
`TAK_DISTANT_LOOKUP_LATE=1` vs default, pan-distant): `w_geometry_build`
5.609 → 4.746 (−15.4%, material), `t_geometry` −6.4% (material),
`w_geometry_key` +6.3% (material: the eligibility check and the cache lookup
for every unit whose stages changed), draw −4.1% (groups −6.1 / −1.9, not
material), p99 −8.4% (threshold 8.5%); total10k-visible unchanged (no
deferrable body: −0.8%).

Readings:

- **Retained bounds remove the rediscovery cost.** In the 10k view the cache
  scanned ~1800 refused bodies per frame (1702 refused for page room alone,
  every frame, because four pages hold 4096 images); with their bounds kept
  `t_distant` falls 2.72 → 0.91 ms and draw time 12.4% (material). In the
  stationary army 298 shape-refused bodies per frame cost 0.38 ms. The
  remaining `t_distant` is the per-item lookup, hits and quads.
- **Deferring held bodies works but is narrow.** Where it applies (a pan at
  zoom ≤ 0.6) it removes 15% of the worker build time — the body
  re-placement of the ~430 held images per frame — at +5–6% key-check cost;
  draw time does not move measurably because the pass is dispatch-bound at
  this size (Experiment 3). It never applies to pose changes (the vertex-count
  rule) and only inside the 67 ms window, so in stationary scenes it is idle.
- **Grouped bakes** cannot show in these scenes: their bakes go to one page
  (target switches 1.8 / 5.7 / 4.2 / 3.6 per frame, equal A vs B — mostly the
  enter/leave switches). The differential test shows fewer switches in 159 of
  900 frames of its multi-page scene.
- **Memory.** Image pages: distant-sparse holds 4 pages during the army view
  in both builds and 2 in G after the images of the left view expire (2
  pages released at the frame-600 sweep); tracked GPU at the last frame
  160.1 → 128.1 MiB in all four B runs (−32 MiB). Every other scene: tracked
  GPU equal at the last frame (no page empties). Same-position migration did
  not fire in the matrix (the survivors' positions overlap on the lower
  pages). CPU: `distant_metadata_bytes` 0.18 MiB (stationary-mixed), 0.81 MiB
  (total10k-visible); `UnitGeom` stays 512 bytes; peak RSS within run ranges.
- **Unchanged:** zoom (every image re-baked on a scale change, budget-bound)
  and every 7680×2160 scene (no image eligible) — draw within ±2%.

**Investigation: why the earlier larger-image experiments failed full-scene
comparisons** (the brief: investigate that evidence; G did not enlarge the
images). The prototype itself (`/tmp/tak-model-reuse-study/image-prototype.patch`,
2026-10-04) no longer exists — `/tmp` has been cleared since — so it could not
be re-run; the question was instead asked of the shipped 28-px image path,
which the prototype extended. Same G2 binary, captures over the 1280×960 world
view, 12 frames per scene (`runs/g-invest-*`, histogram `g/diffhist.py`):

| Comparison | stationary-mixed differing px per frame (max channel error 1–2 / 3–16 / 17–64 / 65–255) | total10k-visible |
|---|---|---|
| live geometry (`TAK_DISTANT_MODELS_OFF=1`) vs the current images | 87,101 (4% / 31% / 50% / 14%) | 253,148 (5% / 27% / 51% / 17%) |
| live geometry vs images baked on the screen pixel grid (uncommitted experiment, `g/experiment-pixel-align.patch`) | 44 (0 / 34% / 39% / 27%) | 3,712 (0 / 22% / 52% / 27%) |

- **The current images are not pixel-equal to the geometry they stand for, by
  construction.** A body is baked at its fractional screen phase
  (`(v − minX)·sx + tx + 2`) and composited from a LINEAR page whose texel
  grid is offset by that fraction, so every image pixel is a bilinear
  resample: ~95 differing pixels per image, errors up to 255 at silhouette
  edges. The isolated sprite test (`distant_models`) places its body at
  integer coordinates, where the resample is exact — which is why it passes
  with "max channel error 0–2" while a scene does not.
- **Baking on the pixel grid removes 99.95% / 98.5% of the difference** (bake
  origin `floor(minX·sx)/sx`, composite at that integer origin, so the
  composite samples texel centres). The remainder is consistent with the
  intentional freshness rule: total10k-visible holds 79 images per frame whose
  pose changed less than 67 ms ago (shown stale by design; 3,712 px ≈ 47 per
  held image); stationary-mixed holds none, and its 44 residual pixels per
  frame were not traced (frames 360–720 only; candidates: a held image after a
  texture-frame change, an edge texel of the 1-px border). Errors 1–2
  (premultiplied rounding) are absent.
- **Consequence for larger images:** a larger image resamples more pixels the
  same way, so a crowded frame differs by thousands of pixels with errors up
  to 255 — the recorded failure of the 64/128/256-px prototype (4,916 pixels,
  max error 255, edge differences) has the profile this mechanism predicts,
  though without the patch that cannot be confirmed. Any future larger-image
  work must bake on the pixel grid and re-bake (not translate) whenever the
  anchor's sub-pixel phase changes, and must state its policy for held stale
  poses, before full-scene equality can be expected. Not pursued here: it
  would change the appearance of today's distant images (sharper, equal to the
  live geometry), which G was required to preserve, and the brief excludes
  expanding into full-size building sprites. Recorded under Rejected
  experiments as an uncommitted experiment.

### Experiment 10 — workstream H retained body vertices (2026-10-16/17)

Clients: **A** = G2 (`~/tak-tmp/render-opt/takclient-G2-candidate`, code of
`2d755167`, the pre-H build); **B** = H4 (`takclient-H4-candidate`, code of
`d8744efe`, the final H code). Intermediate builds, measured and superseded:
H2 (`8d96905f`, mapped write window), H3 (`37ecd884`, staged writes, single
storage). Build-o2 (`-O2` Debug), offscreen EGL, SDL `opengl` on the RTX 5070
Laptop GPU; `RENDER_ENV` confirmed renderer and output (1280×960 → world
1075×888; 7680×2160 → world 7475×2088) for every run. 900 frames, 300
warmup, client pinned to CPUs 16–23, ABBA × 2 per scene and size,
`--wait-quiet 60`; forced-completion frames unless marked pipelined. Load per
run in each `results.json`: H4 session load average 2.0–9.2 (one pan group at
14–17), 0–12 `legion_scenario` processes (on CPUs 4–15), pinned-CPU busy
0.17–0.27; no run contended. Lean testing: timed only the scenes H changes —
stationary-mixed and total10k-visible (the retained case), moving (partly
stable: append, rollover and stream frames alternate) and pan (every frame
streams: the guard for the bookkeeping cost). Output (not in Git):
`~/tak-tmp/render-opt/runs/h2-*`, `h3-*`, `h4-*`; scripts
`~/tak-tmp/render-opt/h/timing.sh`, `h/htable.py` (summary), `h/permode.py`
(per-frame costs by arena mode), `h/srun.py` (counters + verifier lines).

```sh
T="python3 tools/render_benchmark.py --a $G2 --b $H4 --data ~/tak_data --cpus 16-23 --repeats 2 --wait-quiet 60"
$T --output runs/h4-normal --scenes stationary-mixed total10k-visible moving pan
$T --output runs/h4-uhd --sizes uhd --scenes stationary-mixed total10k-visible moving pan
$T --pipelined --output runs/h4-pipe2-uhd --sizes uhd --scenes total10k-visible moving
$T --pipelined --output runs/h4-pipe2-normal --scenes total10k-visible moving
python3 tools/render_benchmark.py --a $X --b $X --b-env TAK_BODY_ARENA_OFF=1 --allow-unverified-env ...   # same binary, arena off
python3 ~/tak-tmp/render-opt/h/htable.py runs/h4-uhd/results.json
python3 ~/tak-tmp/render-opt/h/permode.py runs/h4-uhd/total10k-visible-uhd-01-B/stdout.log
VCPUS=16-23 python3 ~/tak-tmp/render-opt/h/srun.py $H4 total10k-visible normal 900 WARM=300 TAK_BODY_ARENA_VERIFY=1
```

**Correctness.**

- Captures: H1 (the first integration; the drawn vertices and draw calls are
  the same in every later build) vs G2: 0 differing pixels in the world view
  AND the full frame in all 216 comparisons (stationary-mixed,
  total10k-visible, moving, effects, churn, veteran, colour-reset,
  battle-sparse, distant-sparse at 1280×960, 12 frames each), and final code
  H4 vs G2: 0 differing pixels in all 288 comparisons (the same nine scenes
  at 1280×960; stationary-mixed, total10k-visible, moving at 7680×2160; world
  view and full frame, 12 frames each; `runs/h4-captures-*`, 7680×2160 PNGs
  deleted after comparison, JSON kept). World hashes identical A vs B in every
  run.
- `geometrysubmit` (accelerated, offscreen GL): 72 comparisons of arena
  ranges (written in reverse order at scattered offsets, one
  `glMultiDrawArrays`) vs client arrays vs SDL over scale, texture, clip,
  target and blend, with follow-up SDL draws detecting leaked state — pixel
  identical; arena GPU bytes back to baseline after release + 3 frames.
- `vertex_arena` (planner vs a simulated buffer, ASan/UBSan clean): 2600
  scripted frames, 666k commands / 89 M vertices gathered from their storage
  equal to the assembled commands, no write to a vertex written since its
  storage was allocated, upload-free steady frames, 54 rollovers / 53 drains,
  6 full rewrites, budget/failure/reset/shrink/idle paths; plus a
  deterministic rollover scenario (drain in 7 frames, ≤ 300 vertices per still
  frame against a 450 budget). Twelve mutations fail it (revision, generation
  or value ignored, overlapping append, rebuild skipping resident sources,
  failure keeping storage, migration budget ignored, drained storage never
  released, wrong first, rollover into the same slot …).
- `render_body_arena` / `render_body_arena_still` (software renderer,
  simulated storage, every command in the arena): verifier clean; the still
  variant draws 1,565 commands from draining storages with 3 rollovers and 3
  drains. A constant-revision mutation fails the first.
- GL verifier runs (`TAK_BODY_ARENA_VERIFY`, every arena command gathered
  from a CPU mirror equals its sources): stationary-mixed (incl. resets every
  100 frames and `TAK_BODY_ARENA_MIN=1`), colour-reset, churn and effects
  (stream throughout: unstable), total10k-visible 1280×960 (894 frames, 21
  rollovers, 46,953 commands from draining storages) and 7680×2160 (27
  rollovers, 210,379), moving (62 rollovers), staged writes at 7680×2160 —
  no mismatch, 0 fallbacks.

**What changed in the counters (exact, mean per frame, frames 300–899).**

| Scene, size | Body verts copied A → B | Bytes handed to GL A → B | Arena verts retained / uploaded | Arena frames (append / rebuild / stream) | Arena capacity at the end | Tracked GPU at the end A → B |
|---|---:|---:|---:|---|---:|---:|
| stationary-mixed 1280 | 160k → 129k | 3.63 → 3.00 MB | 31.7k / 2.6k | 1.00 / 0.00 / 0 | 1.6 MiB | 120.0 → 121.5 MiB |
| total10k-visible 1280 | 771k → 118k | 17.9 → 4.9 MB | 651k / 32k | 0.98 / 0.02 / 0 | 26.6 MiB | 168.0 → 194.6 MiB |
| moving 1280 | 375k → 324k | 10.9 → 9.9 MB | 51k / 55k | 0.2 / 0.1 / 0.7 | 28.8 MiB (+14.4 in flight) | 76.0 → 119.1 MiB |
| pan 1280 | 419k → 418k | 15.2 → 15.2 MB | 0 / 0 | 0 / 0 / 1 | 0 | 122.0 → 122.0 MiB |
| stationary-mixed 7680 | 529k → 30k | 11.0 → 1.07 MB | 498k / 21k | 1.0 / 0 / 0 | 40.0 MiB | 234.4 → 274.4 MiB |
| total10k-visible 7680 | 2,487k → 222k | 49.6 → 4.4 MB | 2,259k / 165k | 0.9 / 0.1 / 0 | 64.0 MiB | 242.5 → 306.5 MiB |
| moving 7680 | 377k → 326k | 10.9 → 9.9 MB | 51k / 55k | 0.2 / 0.1 / 0.7 | 14.4 MiB (+14.4 in flight) | 202.4 → 231.1 MiB |
| pan 7680 | 529k → 528k | 19.2 → 19.2 MB | 0 / 0 | 0 / 0 / 1 | 0 | 274.5 → 274.5 MiB |

Native draws, SDL draws and body submissions are identical A vs B in every
scene (one native draw per command, as before; `arena_ranges` 970 / 3,366
merged ranges per frame for 778 / 2,729 arena draws in total10k-visible
1280 / 7680). In stationary-mixed at 1280×960 most body vertices are in
commands below 512 vertices (856 SDL draws per frame, distant-image pages
interleaved with live bodies), which stay assembled.

**Timing, final code (H4 vs G2, forced completion; spread = max−min over a
label's four run means; material = beyond `max(2 × spread, 2%)`; groups =
B−A per ABBA group).**

| Scene, size | Draw mean ms A → B | Δ | Groups | Material (thr) | Draw p99 Δ | Frame total Δ | GPU timeline Δ | t_body_submit Δ | t_body_assembly Δ |
|---|---:|---:|---|---|---:|---:|---:|---:|---:|
| stationary-mixed 1280 | 3.060 → 2.965 | −3.1% | −4.4, −1.9 | no (45.7%) | −5.3% | −3.0% | +5.2% (n.s.) | −9.8% (n.s., 11.1%) | +18% (n.s.; +0.03 ms) |
| total10k-visible 1280 | 17.825 → 16.212 | −9.0% | −8.2, −9.9 | no (13.0%) | −13.6% | −5.0% | **−17.8%** | **−28.1%** | −11.4% |
| moving 1280 | 6.059 → 6.001 | −1.0% | −3.4, +1.5 | no | +1.4% | +0.3% | −5.6% | −19.5% | **+53.9%** (+0.24 ms) |
| pan 1280 | 7.909 → 7.653 | −3.2% | −4.8, −1.6 | no | −2.1% | −3.3% | −2.3% | −5.6% | **+9.5%** (+0.05 ms) |
| stationary-mixed 7680 | 6.216 → 4.574 | **−26.4%** | −26.8, −26.0 | **yes** (3.2%) | −37.4% | **−24.5%** | **−28.5%** | **−36.0%** | **−27.4%** |
| total10k-visible 7680 | 59.157 → 59.821 | +1.1% | +1.2, +1.1 | no (2.8%) | +2.5% | **+3.9%** (thr 3.3%) | +0.9% | **−35.7%** | −9.5% |
| moving 7680 | 8.045 → 8.456 | +5.1% | +10.2, +0.4 | no (55%) | +6.9% | +3.3% | −2.9% | **−31.5%** | +76% (n.s.) |
| pan 7680 | 14.480 → 14.259 | −1.5% | +0.1, −3.2 | no | −2.2% | −1.4% | −2.2% | −3.5% | +7.9% (n.s.) |

**The one material regression: total10k-visible at 7680×2160, forced
completion, frame total +3.9%.** Draw is unchanged (+1.1%) although body
submission is 4.9 ms shorter: the scene is GPU-bound (timeline ≈ 53 ms,
34 ms of it the direct-shadow fallback), so the render thread waits for the
GPU elsewhere. The added time is in `present` — the forced `glFinish` — at
0.66 → 2.42 ms, uniformly in every arena frame (per-frame median 2.14 vs
0.32 ms; not on rollover frames only, so not allocation). The same binary
with `TAK_BODY_ARENA_OFF=1` reproduces A (present 2.80 → 0.78 ms). The
buffer usage hint makes no difference (`GL_STATIC_DRAW` / `GL_STREAM_DRAW` /
`GL_DYNAMIC_DRAW`: present 2.50 / 2.89 / 2.80 ms). **In normal pipelined
frames the same scene is faster**: see the pipelined table. Hypothesis
(consistent with the data, not proven — the driver's transfers are not
observable here): under forced completion the final `glFinish` also waits
for the GPU-side transfer of the frame's `glBufferSubData` uploads (3–8 MB
with migration), which pipelined frames overlap with the next frame; and/or
the GPU fetches the 45 MB of arena vertices from host-visible memory at
PCIe rate (~2 ms at ~25 GB/s). Recorded as a known limitation, not hidden.

**Pipelined frames (normal play timing; H4 vs G2, ABBA × 2,
`runs/h4-pipe2-*`, load 4.5–11, no run contended).**

| Scene, size | Draw mean ms A → B | Δ | Groups | Material (thr) | Frame total Δ | Draw p99 Δ | GPU timeline Δ | t_body_submit Δ | t_body_assembly Δ |
|---|---:|---:|---|---|---:|---:|---:|---:|---:|
| total10k-visible 7680 | 60.267 → 54.975 | **−8.8%** | −9.4, −8.1 | **yes** (4.1%) | **−7.8%** (material) | −6.0% (n.s.) | **−9.9%** | **−35.1%** | **−10.8%** |
| total10k-visible 1280 | 17.112 → 15.620 | −8.7% | −7.4, −10.1 | no (12.1%) | −5.3% | +0.3% | **−20.5%** | **−26.0%** | −10.2% |
| moving 7680 | 5.850 → 5.879 | +0.5% | +0.1, +0.9 | no | +0.8% | +5.6% (n.s.) | −3.9% | **−17.8%** | **+48.4%** (+0.26 ms) |
| moving 1280 | 4.999 → 5.022 | +0.4% | −0.5, +1.3 | no | +0.4% | +4.6% (n.s.) | −4.5% | **−22.2%** | +61% (n.s.) |

One pipelined ABBA group of stationary-mixed 7680×2160 (`runs/h4-pipe-uhd`):
frame total −19.2%, draw −22.4%, GPU timeline −23.9%.


**How the final design was reached (measured on the way).**

| Build | What | total10k-visible 1280 | total10k-visible 7680 | moving 1280 |
|---|---|---|---|---|
| H2 | mapped write window (`glMapBufferRange`, unsynchronized), single storage, orphan rebuild | draw −7.2% (material) but **p99 +29% (material)**: rebuild frames 16.2 ms (`glMapBufferRange` on freshly orphaned storage: 23 ms per 14 MB) | draw +9.5%, **p95/p99 +63%** (rebuild frames 50.5 ms) | **draw +14% (material)**: rebuild / append frames 8.75 / 2.24 ms against 0.68 ms for a streaming frame's assembly |
| H3 | staged writes in `bodyVerts_`' tail + one `glBufferSubData`, single storage | rebuild frames 5.3 ms, append 1.02 ms (probe) | rebuild 13.2 ms, append 1.58 ms (probe) | rebuild / append 2.0 / 0.7 ms (probe) |
| H4 | + rollover: two storages, command placement, budgeted migration | rebuild (rollover) frames 1.6 ms = a streaming frame's 1.7 ms; draw p99 **−13.6%** | rollover frames 3.7 ms = streaming's 3.7 ms; append p99 7.5 ms (migration), still below a baseline frame | append / rollover / stream 0.76 / 1.97 / 0.53 ms |

Readings:

- **Where the arena applies, the copies and uploads are gone.** A stationary
  or slowly changing army keeps 90–95% of its arena vertices resident: the
  dense 7680×2160 view copies 222k instead of 2.49 M vertices per frame and
  hands GL 4.4 instead of 49.6 MB; body submission falls 28–36% in every
  retained scene and both sizes (material everywhere but the noisy
  stationary-mixed 1280 session).
- **It shows in frame time where the frame is not GPU-bound**:
  stationary-mixed 7680×2160 −26% draw / −24.5% total / −28.5% GPU timeline
  (material), total10k-visible 1280×960 −9% draw (both groups, inside that
  noisier session's threshold), GPU timeline −17.8% (material).
- **Camera motion streams, as designed** (pan: 0 arena frames): the cost is
  the per-frame bookkeeping, `t_body_assembly` +0.05 ms (+9.5%, material in
  that phase; draw −3.2% and −1.5%, not material).
- **A moving army is neutral**: 70% of frames stream, the rest append or roll
  over; assembly +0.24–0.47 ms is repaid by submission (−19% / −31%); draw
  −1.0% / +5.1% (neither material).
- **Memory**: the arena adds tracked GPU memory — at the end of the timed
  runs +1.6 MiB (stationary-mixed 1280), +26.6 (total10k 1280), +40
  (stationary-mixed 7680), +64 MiB (total10k 7680, the 64 MiB per-storage
  ceiling), and during a drain both storages are live (up to 2 × 64 MiB plus
  3 frames of in-flight storage). It is released after 2 s without an arena
  frame (pan: 0 at the end), under GPU pressure and on resets. Planner
  records ≤ 0.9 MiB (`arena_cpu_bytes`). **Corrected (verifier, see
  Experiment 10b):** this reading first said the CPU copy buffer
  `bodyVerts_` stays at its pre-H peak. In H4 it did not: the first full
  rebuild frame needed slightly more than the buffer held (whole-source
  uploads re-copy runs that sit in small commands) and `std::vector`
  doubled it — total10k-visible 7680×2160 47.95 → 95.55 MiB, stationary-mixed
  7680×2160 10.21 → 20.34 MiB, held to the end of the run (the untouched half
  is probably not resident, but the retained capacity doubled). Fixed in
  `0cbc4401`. Peak RSS +13 to +170 MiB in the dense scenes: driver-side
  mappings of the GL buffers (RSS includes them) and, in H4, possibly part of
  the doubled `bodyVerts_` (not separated).

### Experiment 10b — workstream H memory-record fix (2026-10-17)

The verifier rejected H4's memory record: the record said uploads are staged
in `bodyVerts_` "never above the pre-H peak", but H4's own logs show the
buffer doubling at 7680×2160. Confirmed from the saved runs
(`~/tak-tmp/render-opt/h/bodycap.py`: first / last `body_capacity_bytes`,
peak `body_used_bytes` per run):

| Scene, size | Pre-H (G2) capacity | H4 capacity: first frame → rest of run | H4 peak used |
|---|---:|---:|---:|
| total10k-visible 7680 | 50,283,000 B | 50,095,080 → **100,190,160 B** | 50,521,200 B |
| stationary-mixed 7680 | 10,704,360 B | 10,664,400 → **21,328,800 B** | 10,778,280 B |
| moving (both sizes) | 7,110,000 → 14,220,000 B | 7,110,000 → 14,220,000 B | 7,814,100 B |
| pan 1280 | 8,563,920 → 17,127,840 B | 8,530,320 → 17,060,640 B | 8,798,700 B |

Cause: an arena upload copies the WHOLE source (all of a unit's
`UnitGeom::verts`), because its slot must hold every run for a later frame to
draw any of them from the arena. The runs that source also has in small
(< 512-vertex) commands are assembled separately that frame, so they are
copied twice. On the first full rebuild frame (2,526,060 vertices copied
against 2,504,754 on the streamed frame before it) the need passed the
capacity and `std::vector` doubled it; nothing trims it. The moving and pan
rows show the pre-H buffer doubling the same way whenever a frame's vertex
count rose past the first frame's.

**Fix (`0cbc4401`, H5):**

- `bodyVerts_` grows to need + 1/16 when a frame exceeds its capacity
  (contents discarded first unless a write failure re-assembles behind kept
  commands), never by doubling.
- The double copy is kept and counted (`arena_verts_twice`): avoiding it
  needs the upload split into one `glBufferSubData` per such source (hundreds
  of calls on a rebuild frame) instead of one per frame; not measured,
  rejected on call count. Size: 814 of 222k copied vertices per frame (0.37%)
  in steady total10k-visible 7680×2160, 117 of 118k at 1280×960, 61 of 30k in
  stationary-mixed 7680×2160, 0 in moving; ~21k of 2.53 M on a full rebuild
  frame.
- Write-failure fallback: the arena commands are re-assembled in place of the
  staged uploads (H4 placed them behind, leaving a gap and overcounting
  `body_verts_copied`); a `drawBuffer` SDL error (-1) counts as
  `arena_fallbacks`, not `arena_draws` (nothing is drawn, exactly as `draw()`
  fails on the same error).
- Verification: `TAK_BODY_ARENA_VERIFY` now also checks every assembled
  command against its sources and that assembled commands + staged uploads
  fill `bodyVerts_` exactly; new ctest `render_body_arena_fail`
  (`TAK_BODY_ARENA_FAIL_WRITE=40`: the first write with uploads in every 40
  frames fails) passes; with the in-place reset removed it fails ("gap or
  overrun in the copy buffer"). `render_body_arena`, `_still`, `vertex_arena`,
  `render_telemetry` pass; GL verifier run total10k-visible 7680×2160 (894
  frames, 42 rollovers, 2.44 M arena commands and 2.54 M assembled commands
  checked, 0 fallbacks). `ctest -L quick` 384/384. Release `takclient`
  builds without the new debug strings.

**Measurements** (A = G2, pre-H; B = H5 = `takclient-H5-candidate`, code of
`0cbc4401`). Build-o2, offscreen EGL, SDL `opengl` on the RTX 5070 Laptop
GPU, `RENDER_ENV` confirmed the backend and actual output for every run;
900 frames, 300 warmup, pinned to CPUs 16–23, ABBA × 2, `--wait-quiet 60`,
nothing built during the runs. Load: average 1.2–15.0 (one moving 7680 group
at 15), 0–4 `legion_scenario` processes (CPUs 4–15), pinned-CPU busy
0.17–0.28, no run contended. Scenes: the three H changes most
(stationary-mixed and total10k-visible = retained; moving = alternating
append / rollover / stream, where the growth policy matters most); pan
streams every frame and is untouched by the fix. Script
`~/tak-tmp/render-opt/h/h5.sh`, output `runs/h5-*` (not in Git; 7680×2160
PNGs deleted after comparison, JSON kept).

```sh
A=~/tak-tmp/render-opt/takclient-G2-candidate B=~/tak-tmp/render-opt/takclient-H5-candidate
T="python3 tools/render_benchmark.py --a $A --b $B --data ~/tak_data --cpus 16-23 --wait-quiet 60"
$T --capture-only --output runs/h5-captures-normal --scenes stationary-mixed total10k-visible moving
$T --capture-only --output runs/h5-captures-uhd --sizes uhd --scenes stationary-mixed total10k-visible moving
$T --repeats 2 --output runs/h5-normal --scenes stationary-mixed total10k-visible moving
$T --repeats 2 --output runs/h5-uhd --sizes uhd --scenes stationary-mixed total10k-visible moving
$T --repeats 2 --pipelined --output runs/h5-pipe-uhd --sizes uhd --scenes total10k-visible
python3 ~/tak-tmp/render-opt/h/htable.py runs/h5-uhd/results.json; python3 ~/tak-tmp/render-opt/h/bodycap.py runs/h5-uhd
```

Captures H5 vs G2: 0 differing pixels in all 144 comparisons (3 scenes × 2
sizes × 12 frames, world view and full frame).

CPU body buffer (`body_capacity_bytes` at the end of every run; peak
`body_used_bytes`; identical in all four runs of a label):

| Scene, size | Pre-H capacity | H4 capacity | H5 capacity | H5 vs pre-H | Peak used pre-H → H5 |
|---|---:|---:|---:|---:|---:|
| total10k-visible 7680 | 47.95 MiB | 95.55 MiB | 50.76 MiB | +5.9% | 47.95 → 48.18 MiB |
| total10k-visible 1280 | 43.78 MiB | 43.62 MiB | 46.35 MiB | +5.9% | 43.78 → 43.62 MiB |
| stationary-mixed 7680 | 10.21 MiB | 20.34 MiB | 10.81 MiB | +5.9% | 10.21 → 10.28 MiB |
| stationary-mixed 1280 | 8.41 MiB | 8.38 MiB | 8.90 MiB | +5.9% | 8.41 → 8.38 MiB |
| moving (both sizes) | 13.56 MiB | 13.56 MiB | 7.67 MiB | −43% | 7.45 → 7.45 MiB |

So H5's retained capacity is the growth slack above the first frame's need
(+5.9%) in the still scenes and well below the pre-H buffer wherever the
pre-H buffer doubled (moving). The per-frame used bytes fall from the whole
scene to the copies actually made (e.g. 47.5 → 2.9 MiB at the end of
total10k-visible 7680). Peak used exceeds the pre-H buffer only by the
double copies of a rebuild frame (+0.5% at 7680×2160). `bodyVerts_` is still
never trimmed after demand subsides — true before H as well (workstream J).

Peak RSS (whole process): total10k-visible 7680 A 1023–1040 / H5 1133–1149
MiB (H4 1131–1148 in its session: the doubled half was evidently never
resident), stationary-mixed 7680 A 630–641 / H5 661–663 (H4 677–683),
moving 7680 A 606–613 / H5 607–645; the H increase is the arena's GL
buffers mapped by the driver (tracked GPU +64 / +40 / +29 MiB at the end).

Timing (forced completion unless marked; material = beyond
`max(2 × spread, 2%)`):

| Scene, size | Draw mean A → B | Δ (groups) | Material (thr) | Frame total Δ | GPU timeline Δ | t_body_submit Δ | t_body_assembly Δ |
|---|---:|---|---|---:|---:|---:|---:|
| stationary-mixed 1280 | 2.811 → 2.714 | −3.4% (−3.1, −3.8) | no (12.7%) | −1.6% | +3.6% (n.s.) | **−8.6%** | +24% (n.s.; +0.03 ms) |
| total10k-visible 1280 | 16.835 → 14.713 | **−12.6%** (−12.5, −12.8) | **yes** (4.7%) | **−9.7%** | **−20.9%** | **−25.7%** | **−15.6%** |
| moving 1280 | 5.638 → 5.621 | −0.3% (+0.6, −1.2) | no | +0.4% | **−6.8%** | −19.7% (n.s.) | **+61%** (+0.23 ms) |
| stationary-mixed 7680 | 6.048 → 4.381 | **−27.6%** (−26.6, −28.5) | **yes** (8.3%) | **−25.3%** | **−31.1%** | **−36.9%** | **−25.4%** |
| total10k-visible 7680 | 59.298 → 60.076 | +1.3% (+1.7, +1.0) | no (3.3%) | +3.6% (thr 4.5%, n.s.) | +1.1% | **−36.1%** | −8.0% |
| moving 7680 | 8.428 → 8.322 | −1.3% (−6.0, +4.1) | no | −0.8% | −6.2% | **−32.4%** | +57% (n.s.) |
| total10k-visible 7680 pipelined | 58.790 → 54.224 | **−7.8%** (−7.6, −7.9) | **yes** (4.7%) | **−6.7%** | **−9.1%** | **−33.9%** | −6.9% |

Readings: the same picture as Experiment 10, nothing regressed by the fix
(the fix changes only allocation events, which happen in the first frames,
and adds one pass over the frame's sources on arena frames). The
forced-completion total10k-visible 7680×2160 frame total is +3.6% again
(not material in this session, threshold 4.5%; material +3.9% in Experiment
10) — the known `present` limitation below. In stationary-mixed 1280×960 body
submission improves only 8.6% (most body vertices sit in small commands,
which stay assembled) — the checklist no longer claims −28% to −36% there.

### Experiment 11 — workstream I cost-planned pool passes (2026-10-17)

**Binaries.** A = H5 (`takclient-H5-candidate`, code of `0cbc4401`, the
pre-I build); I2 = `68021102` (first planned version); I3 = I2 + the hit
estimate and copy-cap fixes; **I4 = `590db308`, the final code**
(`takclient-I4-candidate`). Build-o2, offscreen EGL, SDL `opengl` on the RTX
5070 Laptop GPU; `RENDER_ENV` confirmed the backend and actual output
(1280×960 / 7680×2160, world views 1075×888 / 7475×2088; at 7680×2160 the
zoom-0.5/0.25 scenes run at the map-fills-window clamp 0.73) for every run.
900 frames, 300 warmup, forced completion (`glFinish` before present),
pinned to CPUs 16–23, ABBA × 2 per scene and size, `--wait-quiet 600`,
nothing built during timing runs. Load (whole machine; CPUs 4–15 belong to
the Legion program): final runs load 2.6–12.4, 0–9 `legion_scenario`
processes, pinned-CPU busy below the contention threshold in every run (0
contended). Several groups still show other-process interference (one
stationary-mixed 1280 A run at 4.40 ms draw against 2.62–2.80 for the
other seven), so A/A spreads are wide; the materiality rule (max(2 × A/A
spread, 2%)) is applied as is.

**Scenes** (lean testing, the passes I touches): small-battle (small passes:
fewer workers), stationary-mixed (hit-dominated geometry, copy-heavy),
moving (mixed rebuild/hit costs, shadow transform), total10k-visible
(largest passes, plan cost). Pan/zoom/churn exercise the same passes with
the costs in between; not timed (captures of churn below).

**Calibration** (`TAK_SCHEDULE_SAMPLES`, serial `TAK_RENDER_POOL_THREADS=1`
runs, `~/tak-tmp/render-opt/i/cal*`): 952k per-unit samples over
stationary-mixed, moving, pan, zoom, total10k-visible, effects, churn,
small-battle (key 580 ns + 2.3 ns/word, local 750 ns + 31.5 ns/vertex +
5 ns/shadow point, placement 350 + 2.5 ns/vertex + 0.9 ns/point, material
8 ns/vertex; median relative error 9–21%); per-pass serial timings over six
scenes at both sizes: the geometry estimate is 0.80–1.28 × the measured
pass, shadow transform 0.64–1.15 ×, body copy 0.77–1.95 × (small tasks:
hence the per-range term), shadow checks 0.46 × on full scans (tiles kept)
and ~0.06 × where the first point differs.

**Final result, A = H5 vs B = I4** (ms per frame, mean of four runs per
label; Δ = B vs A; noise = max A/A spread of the two labels; bold =
material):

| scene | size | draw A → B (ms) | Δ draw (noise) | Δ total p99 (noise) | t_geometry | t_shadow_prep | t_body_assembly | t_pool_dispatch | t_pool_wait | w_pool_wake | workers woken / idle (B) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| moving | normal | 6.337 → 5.853 | -7.6% (19.2%) | -2.9% (12.8%) | 1.951 → 1.862 | 0.451 → 0.290 | 0.805 → 0.689 | 1.562 → 0.026 | 0.447 → 0.018 | 43.9 → 4.1 | – → 20.6 / 3.7 |
| small-battle | normal | 0.703 → 0.655 | -6.9% (7.2%) | +30.6% (157.8%) | 0.117 → 0.103 | 0.012 → 0.011 | 0.073 → 0.055 | 0.055 → 0.006 | 0.056 → 0.025 | 1.6 → 0.1 | – → 7.3 / 0.5 |
| stationary-mixed | normal | 3.138 → 2.684 | -14.5% (54.8%) | -30.8% (100.4%) | 0.417 → 0.324 | 0.054 → 0.020 | 0.292 → 0.136 | 0.189 → 0.013 | 0.210 → 0.012 | 7.4 → 0.1 | – → 14.6 / 1.0 |
| total10k-visible | normal | 14.803 → 14.580 | -1.5% (2.1%) | **-13.8%** (5.1%) | 2.122 → 2.251 | 0.143 → 0.115 | 1.191 → 1.085 | 1.399 → 0.175 | 0.201 → 0.037 | 39.4 → 2.9 | – → 17.4 / 2.1 |
| moving | uhd | 7.312 → 6.991 | -4.4% (3.2%) | +1.0% (14.2%) | 2.039 → 1.946 | 0.436 → 0.334 | 0.864 → 0.775 | 1.672 → 0.024 | 0.325 → 0.021 | 47.3 → 5.2 | – → 20.7 / 2.2 |
| small-battle | uhd | 1.056 → 0.983 | -6.9% (6.8%) | -8.7% (16.8%) | 0.181 → 0.168 | 0.015 → 0.012 | 0.110 → 0.069 | 0.067 → 0.007 | 0.135 → 0.043 | 2.2 → 0.1 | – → 7.3 / 2.5 |
| stationary-mixed | uhd | 5.178 → 4.585 | **-11.4%** (1.6%) | **-13.8%** (5.6%) | 0.643 → 0.580 | 0.079 → 0.036 | 0.683 → 0.308 | 0.129 → 0.015 | 0.497 → 0.022 | 14.8 → 1.2 | – → 9.5 / 4.7 |
| total10k-visible | uhd | 61.665 → 60.846 | -1.3% (1.9%) | -4.5% (5.1%) | 2.542 → 2.737 | 0.000 → 0.000 | 3.517 → 3.384 | 1.634 → 0.183 | 0.178 → 0.099 | 47.5 → 3.6 | – → 14.0 / 3.6 |

- Material at 7680×2160: stationary-mixed draw −11.4%, total p99 −13.8%;
  at 1280×960 total10k-visible total p95 −19.4% / p99 −13.8% (draw −1.5%,
  not material). Every other draw delta is a reduction inside its noise
  (small-battle −6.9% at both sizes, moving −4.4% / −7.6%, stationary-mixed
  1280 −14.5% with one interfered A run). No metric regressed materially.
- Pool overhead: dispatch −87% to −99% and caller wait −44% to −96% in
  every scene and size (material in most), summed worker wake latency
  −89% to −99%; CPU time inside chunks (`w_pool_busy`) −2% to −35%. The total-frame p95/p99 gains in total10k-visible come
  from the frames that also run the COB VM tick, which shares the pool.
- `t_geometry` in total10k-visible: +6.1% (1280) / +7.7% (7680), not
  material: H5's dispatch time is inside that phase as well; the remaining
  cost of planning is `t_schedule_plan` 0.017–0.030 ms (0.099 ms in I3,
  before the dense cost array).

**What gives the gain** (decomposition, same binary, ABBA × 2):

*Planning vs the uniform passes* (A = I4 with `TAK_RENDER_SCHEDULE=uniform`:
fixed 8-item chunks, every thread, the former thresholds, on the new pool;
B = I4):

| scene | size | draw A → B (ms) | Δ draw (noise) | Δ total p99 (noise) | t_geometry | t_shadow_prep | t_body_assembly | t_pool_dispatch | t_pool_wait | w_pool_wake | workers woken A → B / idle (B) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| moving | normal | 5.625 → 5.319 | -5.4% (9.7%) | -5.3% (15.4%) | 1.716 → 1.658 | 0.215 → 0.219 | 0.573 → 0.572 | 0.020 → 0.018 | 0.086 → 0.016 | 2.9 → 2.9 | 21.0 → 20.6 / 1.2 |
| small-battle | normal | 0.663 → 0.675 | +1.8% (6.0%) | +37.8% (140.5%) | 0.104 → 0.112 | 0.011 → 0.011 | 0.054 → 0.059 | 0.010 → 0.007 | 0.036 → 0.031 | 0.1 → 0.1 | 13.9 → 7.3 / 0.5 |
| stationary-mixed | normal | 3.097 → 2.987 | -3.6% (43.0%) | -19.4% (100.1%) | 0.429 → 0.398 | 0.024 → 0.022 | 0.198 → 0.180 | 0.020 → 0.013 | 0.024 → 0.020 | 0.3 → 0.2 | 15.4 → 14.6 / 2.1 |
| total10k-visible | normal | 14.999 → 15.237 | +1.6% (12.8%) | +6.0% (39.5%) | 2.261 → 2.341 | 0.129 → 0.123 | 1.136 → 1.140 | 0.180 → 0.193 | 0.030 → 0.053 | 3.2 → 3.1 | 17.5 → 17.4 / 3.0 |
| moving | uhd | 7.611 → 7.570 | -0.5% (5.4%) | -2.4% (5.2%) | 2.203 → 2.143 | 0.378 → 0.393 | 0.879 → 0.899 | 0.040 → 0.041 | 0.124 → 0.024 | 6.1 → 6.3 | 21.0 → 20.7 / 3.2 |
| small-battle | uhd | 0.931 → 0.905 | -2.9% (15.8%) | -5.1% (48.2%) | 0.130 → 0.130 | 0.011 → 0.011 | 0.057 → 0.058 | 0.012 → 0.007 | 0.051 → 0.029 | 0.1 → 0.1 | 14.1 → 7.3 / 1.9 |
| stationary-mixed | uhd | 4.969 → 5.072 | +2.1% (5.5%) | +3.9% (8.3%) | 0.662 → 0.728 | 0.036 → 0.039 | 0.342 → 0.359 | 0.020 → 0.017 | 0.021 → 0.025 | 1.4 → 1.7 | 14.8 → 9.5 / 5.2 |
| total10k-visible | uhd | 61.287 → 61.329 | +0.1% (1.1%) | -0.1% (6.6%) | 2.788 → 2.797 | 0.000 → 0.000 | 3.430 → 3.440 | 0.171 → 0.175 | 0.126 → 0.118 | 3.7 → 3.4 | 14.0 → 14.0 / 4.1 |

Planning alone changes no draw time materially at either size (−5.4% to
+2.1%, all inside noise). It halves the woken workers where the work is
small (small-battle 13.9 → 7.3 per frame; stationary-mixed 7680 14.8 → 9.5),
which these benchmarks cannot turn into frame time: the render study runs
the simulation inline, so no other thread competes for the CPUs (see Known
limitations).

*Pool size* (I2; A = `TAK_RENDER_POOL_THREADS=24`, the pre-I size on this
machine; B = the affinity size, 8 under the pin):

| scene | size | draw A → B (ms) | Δ draw (noise) | Δ total p99 (noise) | t_geometry | t_shadow_prep | t_body_assembly | t_pool_dispatch | t_pool_wait | w_pool_wake | workers woken A → B / idle (B) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| moving | normal | 5.821 → 5.692 | -2.2% (12.0%) | -1.4% (21.6%) | 1.728 → 1.635 | 0.201 → 0.188 | 0.686 → 0.691 | 0.750 → 0.020 | 0.058 → 0.018 | 2.6 → 1.7 | 41.4 → 16.8 / 2.5 |
| small-battle | normal | 0.720 → 0.712 | -1.0% (7.9%) | +1.6% (102.0%) | 0.134 → 0.130 | 0.011 → 0.011 | 0.064 → 0.063 | 0.020 → 0.007 | 0.050 → 0.052 | 0.1 → 0.1 | 12.5 → 7.3 / 0.6 |
| stationary-mixed | normal | 3.077 → 2.989 | -2.8% (2.8%) | -8.0% (5.8%) | 0.454 → 0.393 | 0.022 → 0.021 | 0.201 → 0.197 | 0.119 → 0.012 | 0.068 → 0.015 | 0.5 → 0.2 | 26.6 → 10.6 / 1.4 |
| total10k-visible | normal | 16.352 → 16.308 | -0.3% (11.9%) | -1.7% (12.8%) | 2.518 → 2.510 | 0.115 → 0.113 | 1.259 → 1.245 | 1.051 → 0.153 | 0.098 → 0.060 | 3.2 → 2.3 | 29.9 → 13.4 / 3.7 |

With the new wake/claim scheme an oversubscribed pool costs little (draw
−0.3% to −2.8%; dispatch drops because fewer threads are posted). So the
gains against H5 come mostly from the pool mechanics — no `notify_all` of
every worker, no wait for workers that wake after the work is gone — and
only partly from the affinity size. **The affinity size is a pinned-benchmark
artefact for an unpinned desktop**: there the pool is 24 threads before and
after; the mechanics apply to both.

**Iterations (measured against the same binary's uniform passes, 1280×960,
`runs/i2-uniform-normal`, `runs/i3-uniform-normal`):**

- I2: a hit's next-build estimate read the entry's vector sizes, cache
  lines a hit never touches: stationary-mixed `t_geometry` 0.521 → 0.612 ms
  (+17%), `w_geometry_key` +9%; and a 4-thread cap on the body copy:
  `t_body_assembly` 0.248 → 0.317 ms (+28%). Both fixed in `590db308`.
- I3 (both fixed): geometry and copy equal to uniform (stationary-mixed
  0.312 vs 0.308 ms, 0.128 vs 0.128 ms), but the plan loop walked the
  entries: `t_schedule_plan` 0.099 ms at 8,752 units.
- I4 (final): dense cost copy taken at claim + integer `addUnits`:
  0.030 ms.

**Captures** (separate runs, frames 360, 480–489 and 720; world view and
full frame): I2 vs H5 — 5 scenes at 1280×960 and 5 at 7680×2160 (small-battle,
stationary-mixed, moving, total10k-visible, effects), and I2 serial
(`TAK_RENDER_POOL_THREADS=1`) vs I2 default at 1280×960: 0 differing pixels
in all 180 comparisons. I4 (final) vs H5 — small-battle, stationary-mixed,
moving, total10k-visible, effects at both sizes and churn (moving army,
pan, deaths/respawns, render-target resets) at 1280×960 — and I4 serial
(`TAK_RENDER_POOL_THREADS=1`) vs I4 default in the same six scenes at
1280×960: 0 differing pixels in all 204 comparisons (`runs/i4-captures-*`).

**Tests:** `work_schedule` (plans; serial equivalence over 300 random
plans; targeted wakes; 20,000 back-to-back passes; `addUnits` = per-item
`add`; ASan/UBSan and TSan clean; ten header mutations fail it),
`render_telemetry`, `render_schedule` (in game, software renderer, 4-thread
pool, every pass parallel; four in-game mutations fail it: a geometry
range, body-copy range, shadow-transform range or point-check range that
skips its last item), `ctest -L quick` (386/386 at `68021102` and at
`590db308`). Release
`takclient` builds without the new debug strings. `--mpai` not run: only
`src/client` (presentation) and `tools/` changed; every benchmark run checks
the world hash (identical A vs B in every scene).

### Experiment 12 — workstream J CPU cache budgets (2026-10-17)

**Binaries.** A = I4 (`takclient-I4-candidate`, code of `590db308`, pre-J);
B = J3 (`takclient-J3-candidate`, code of `3dc315c4`, the final J code).
Probes on J1 (= `2cb7cdf5`, pure LRU section budget) and J2 (working-set rule,
an earlier build of the same code) are cited where they matter. Build-o2,
offscreen EGL, SDL `opengl` on the RTX 5070 Laptop GPU; `RENDER_ENV`
confirmed the backend and actual output for every run (1280×960 / 7680×2160;
at 7680×2160 the zoom-0.5/0.25 scenes run at the map-fills-window clamp
0.73). 900 frames, 300 warmup, forced completion, pinned to CPUs 16–23, ABBA
× 2 per scene and size, `--wait-quiet 120`, nothing built during the runs.
Load: average 1.9–14.3, 0–2 `legion_scenario` processes (CPUs 4–15), pinned-CPU busy 0.18–0.30, no run
contended. Script `~/tak-tmp/render-opt/j/measure.sh`, analysis
`~/tak-tmp/render-opt/j/jtable.py`, output `runs/j3-*` (not in Git).

**Scenes** (lean testing, what J touches): battle-sparse (buffers trimmed
after the view empties), total10k-visible (the largest buffers: no thrash,
per-frame tracker cost), zoom (chunk composition under job pins, the changed
compositor locking, mips), bigmap-zoom (new; the only scene where the section
budget evicts: Middle Earth V1, 255 sections, 238 MiB decoded).

```sh
T="python3 tools/render_benchmark.py --data ~/tak_data --cpus 16-23 --a $I4 --b $J3"
S="battle-sparse total10k-visible zoom bigmap-zoom"
$T --capture-only --output runs/j3-captures-normal --scenes $S --sizes normal   # and uhd
$T --wait-quiet 120 --repeats 2 --output runs/j3-normal --scenes $S --sizes normal   # and uhd
$T --capture-only --a $I4 --b $I4 ... ; --a $J3 --b $J3 ...   # A/A capture checks (battle-sparse, bigmap-zoom, uhd)
python3 ~/tak-tmp/render-opt/j/jtable.py runs/j3-normal runs/j3-uhd
```

**World hashes** identical A vs B in every scene (the tool refuses otherwise).

**Captures** (12 frames per scene: 360, 480–489, 720; world view and full
frame). 1280×960: 0 differing pixels in all 4 scenes. 7680×2160: 0 in
total10k-visible and zoom. battle-sparse frame 480 differs by 344,240 px
(max channel error 85) and bigmap-zoom frames 480–487 by 5,184–301,727 px;
every other frame is identical. Both are terrain-chunk readiness at a capture
frame right after a camera change, not J content:

- battle-sparse frame 480 is 30 frames after the jump to the empty view.
  The differing pixels form one rectangle of 747×739 px, exactly one
  1024-px chunk at zoom 0.73, with equal mean colour. At that frame A held
  60 chunk images and B 59: B received that chunk one frame later (one chunk
  completes per frame). From frame 481 on the two are identical. Across the
  eight timing runs, the frame by which every chunk of the empty view is
  ready is 483 in both builds at 7680×2160, and 454 (A) / 453–455 (B) at
  1280×960, so composition does not lag systematically.
- bigmap-zoom frames 480–487 fall in a burst of chunk composition (A 34 / B 32
  chunks pending at frame 480); they are identical from frame 489, when
  nothing is pending. B had evicted no section yet: all 249 MB of decoded
  sections were young (the first evictions come at frame 720). So the budget
  played no part in these frames.
- A/A and B/B capture pairs of the same two scenes (`runs/j3-aa-captures-I4`,
  `-J3`): I4 against I4 differs in bigmap-zoom frames 480–489 (up to
  713,660 px), J3 against J3 in frames 480–486 (up to 293,312 px);
  battle-sparse is identical in both pairs. So the pre-J build shows the same
  race, and battle-sparse's frame 480 is an occasional instance of it. No
  unexplained difference remains.

**Timing** (ms per frame, mean of four runs per label; Δ = B vs A; noise =
max A/A spread of the two labels; nothing is material):

| scene | size | draw A → B | Δ draw (noise) | Δ total mean | Δ total p99 (noise) | Δ GPU timeline | per-run draw, ABBA order |
|---|---|---|---|---|---|---|---|
| battle-sparse | 1280×960 | 2.675 → 2.724 | +1.8% (7.7%) | +0.2% | +0.5% (7.8%) | +4.0% | A2.68 B2.70 B2.66 A2.61 · A2.64 B2.66 B2.87 A2.77 |
| total10k-visible | 1280×960 | 15.638 → 15.833 | +1.2% (2.2%) | +1.3% | +0.9% (5.1%) | +2.2% | A15.71 B15.69 B15.67 A15.48 · A15.65 B16.02 B15.95 A15.71 |
| zoom | 1280×960 | 9.350 → 9.298 | −0.6% (12.1%) | −0.5% | −1.4% (13.2%) | +0.2% | A10.00 B9.18 B9.54 A9.32 · A9.21 B9.06 B9.42 A8.87 |
| bigmap-zoom | 1280×960 | 1.977 → 2.015 | +1.9% (14.8%) | −0.9% | −9.7% (35.9%) | +1.2% | A1.79 B2.02 B2.04 A2.00 · A2.03 B2.00 B2.00 A2.08 |
| battle-sparse | 7680×2160 | 3.328 → 3.517 | +5.7% (30.6%) | +6.4% | +10.5% (43.1%) | +0.2% | A4.03 B4.17 B3.25 A3.01 · A3.08 B3.43 B3.22 A3.19 |
| total10k-visible | 7680×2160 | 58.841 → 59.358 | +0.9% (2.7%) | +0.8% | −2.2% (14.0%) | +0.9% | A59.02 B60.19 B59.57 A58.54 · A58.36 B59.06 B58.61 A59.44 |
| zoom | 7680×2160 | 13.488 → 13.670 | +1.4% (12.6%) | +1.4% | +5.4% (28.3%) | +3.5% | A13.18 B13.28 B13.00 A13.50 · A13.30 B13.68 B14.71 A13.97 |
| bigmap-zoom | 7680×2160 | 3.392 → 3.500 | +3.2% (6.1%) | +1.3% | +7.7% (19.8%) | +1.9% | A3.37 B3.42 B3.63 A3.42 · A3.31 B3.49 B3.47 A3.46 |

Six of the eight draw deltas are positive (+0.9% to +5.7%), all inside the
noise threshold, and no scene shows B slower in both ABBA groups at both
sizes (total10k-visible: B slower in group 2 at 1280×960 and in group 1 at
7680×2160). The most J-specific per-frame cost, the scratch-slot lookup on
every unit build, appears as `w_geometry_key` +2.1% (12.91 → 13.18 ms summed
over workers, about 30 ns per unit) in total10k-visible 1280×960, +0.7% at
7680×2160, and −1.8% in zoom 1280×960; `t_geometry` moves −7.5% to +15% (the
largest in bigmap 7680×2160, 0.34 → 0.39 ms) with no consistent sign.
Recorded as no measurable cost, with a possible small positive bias that this
session cannot resolve.

**Trim events and their cost** (B; deterministic, identical in all four runs):

| scene | size | trims (frames) | released | buffers |
|---|---|---|---|---|
| battle-sparse | 1280×960 | 5 (frame 750) | 19.06 MiB | body 9.9 → 0.1, shadow scratch 9.7 → 1.1, lists 1.1 → 0.5 MiB |
| battle-sparse | 7680×2160 | 7 (frames 450, 750) | 19.05 MiB | body 10.9 → 0.1, shadow scratch 8.3 → 0.5, lists 1.0 → 0.6 MiB |
| total10k-visible | 1280×960 | 5 (frame 450) | 81.0 MiB | body 46.3 → 5.2, shadow scratch 45.3 → 42.9, lists 4.6 → 3.9 MiB |
| total10k-visible | 7680×2160 | 2 (frame 450) | 36.6 MiB | body 50.8 → 14.3 MiB |
| zoom | both | 0 | — | demand sustained |
| bigmap-zoom | both | 1 (frame 750) | 0.9 MiB | — |

total10k-visible trims although it is a steady scene. The released capacity
was the first frames' peak: the initial full rebuild assembles every body,
and every shadow tile is dirty once. Steady frames need under half of that,
and nothing regrew in the remaining 450 frames (retained bytes unchanged
from frame 450 to 899). A later full rebuild, such as a camera jump or a GPU
reset, regrows the body buffer once. The trim frame is not measurably
slower: frame 750 in battle-sparse costs 1.07–1.21 ms of draw in B and
1.03–1.16 ms in A. An unrelated event lands on that frame in both builds;
it is about 0.45 ms above the neighbouring frames, not caused by the trim.

**Decoded sections and mips** (B):

| scene | size | decodes | evictions | redecodes | sections at end | section peak | mips at end |
|---|---|---|---|---|---|---|---|
| battle-sparse, total10k-visible, zoom | both | 118 (= the map's sections) | 0 | 0 | 63.8 MiB | 63.8 MiB | 0–16.9 MiB |
| bigmap-zoom | 1280×960 | 292 | 170 | 37 | 124.2 MiB (A: 237.8) | 237.8 MiB | 12.3 MiB |
| bigmap-zoom | 7680×2160 | 292 | 137 | 37 | 136.2 MiB (A: 237.8) | 237.8 MiB | 34.1 MiB |

No mip was evicted or trimmed in any timed scene (the mip budget is a
ceiling; the 30 s idle window exceeds a 15 s run). The J1 probe (pure LRU,
same scene) re-decoded 176 / 298 sections (Rejected experiments).

**CPU memory at the last frame**: B's `cpu_cache_*` totals, plus
"comparable" bytes from keys that both builds report (geometry, body, shadow
scratch, lists, sections, mips, arena records, distant metadata). MiB:

| scene | size | comparable A → B | B retained / pinned / evictable | B peak (retained) |
|---|---|---|---|---|
| battle-sparse | 1280×960 | 96.1 → 77.0 (at frame 449: 129.1 both) | 77.4 / 27.1 / 50.1 | 135.5 |
| battle-sparse | 7680×2160 | 84.3 → 65.3 (125.4 both) | 65.7 / 49.6 / 15.9 | 125.8 |
| total10k-visible | 1280×960 | 352.9 → 308.7 | 309.0 / 182.4 / 124.4 | 353.4 |
| total10k-visible | 7680×2160 | 308.2 → 271.6 | 272.0 / 193.7 / 76.7 | 308.5 |
| zoom | 1280×960 | 144.5 → 144.5 | 144.9 / 94.1 / 50.2 | 144.9 |
| bigmap-zoom | 1280×960 | 259.4 → 145.8 | 146.2 / 57.8 / 88.3 | 259.7 |
| bigmap-zoom | 7680×2160 | 281.4 → 179.9 | 180.3 / 145.1 / 35.1 | 281.8 |

"Evictable" counts unpinned decoded sections that the budget would release
only under pressure (64 MiB on Ulasem). Tracked GPU memory is identical
A vs B in every scene. J changes no GPU allocation. Whole-process RSS
(sampled by the tool): peak equal within a few MiB everywhere except
bigmap-zoom 1280×960, where B's peak is 940–970 MiB against A's 908–940. A
plausible cause is two decodes now running at once outside the lock, plus
heap fragmentation from evict/redecode, but this is not established.
End-of-run RSS varies by 150–300 MiB run to run (it is sampled during
teardown) and cannot show the trims. glibc may also keep freed memory in the
process (dynamic mmap threshold), so released capacity is not necessarily
returned to the OS (Known limitations).

**Tests (final code).** `terrain_compositor`, `terrain_cache`, `cpu_budget`,
`work_schedule`, `render_cpu_budget`, `render_telemetry`,
`render_benchmark_tool` pass; `ctest -L quick` 388/388 (on the code before the release-build fix below; the J tests and `render_schedule`, `render_distant_lookup` re-run after it); sanitizer
builds: `terrain_compositor` and `work_schedule` under ASan/UBSan and TSan;
mutation checks: compositor (pins, watermark, redecode count, age rule),
trim policy (five), participant index (two). Release `takclient` builds (after moving the verifier totals out of a debug-only header section: the first release build failed on it) and contains none of the J debug or telemetry strings.
`--mpai` Retail 60 s Inner Circle seed 1 = `2c39430636a3dbda` (unchanged; run because `src/terrain` is shared code, although neither the server nor the simulation uses the compositor or the render pool).

### Experiment 13 — workstream K camera-driven terrain work (2026-10-17)

**Binaries.** A = K0 (`takclient-K0-baseline`, code of `4c7a018d`: J plus the
K harness and readiness gauges, terrain behaviour pre-K); B = K4
(`takclient-K4-candidate`, code of `5eebec8b`). K5 (`b98183b1`, which only
skips per-frame passes when no visible chunk is missing; decisions identical)
was re-timed against A in the scenes where that overhead showed (13b below).
Build-o2, offscreen EGL, SDL `opengl` on the RTX 5070 Laptop GPU;
`RENDER_ENV` confirmed the backend and the actual output for every run
(1280×960 / 7680×2160; Ulasem zoom-0.5/0.25 scenes run at the 0.73 clamp at
7680×2160). 900 frames, 300 warmup, forced completion, pinned to CPUs 16–23,
ABBA × 2 per scene and size, `--wait-quiet 120`, nothing built during the
timing runs. Load: average 6.7–17.2 (1280×960) / 5.3–8.8 (7680×2160), 3–7
`legion_scenario` processes on CPUs 4–15, pinned-CPU busy 0.18–0.28; no run
contended. Script `~/tak-tmp/render-opt/k/measure.sh`, tables
`~/tak-tmp/render-opt/k/ktable.py`, runs `runs/k4-*` (not in Git).

**Scenes** (what K touches): the three new terrain scenes (Middle Earth V1:
sweep, zoom oscillation across a level boundary, jump storm); `jump`, `zoom`,
`pan` (camera motion on Ulasem); `bigmap-zoom` (large map above the section
budget); `battle-sparse` (a jump to an empty view: sections and chunks);
`constrained` (128 MiB GPU cap: memory pressure, stand-ins). Static unit
scenes were not timed: their terrain is settled after warmup, so K only adds
its per-frame bookkeeping, measured in the still scenes below.

**World hashes** identical A vs B in every scene (the tool refuses otherwise).

**Timing** (ms per frame, mean of four runs per label; Δ = B vs A; noise =
max A/A spread of the two labels; "hole / underlay frames" = share of
measured frames with any visible cell drawn by the section fallback / with
any fallback tile left to the blurry underlay):

| scene | size | draw A → B | Δ draw (noise) | Δ total mean (noise) | Δ total p99 (noise) | t_terrain A → B ms | Δ t_terrain p99 | wasted compose ms/run A → B | tracked GPU MiB A → B | hole / underlay frames A → B |
|---|---|---|---|---|---|---|---|---|---|---|
| battle-sparse | 1280×960 | 3.128 → 3.066 | -2.0% (6.6%) | -1.9% (6.4%) | -8.1% (26.2%) | 0.062 → 0.065 | +25.1% (29.2%) | 0 → 0 | 150 → 150 | 0.01/0.00 → 0.01/0.00 |
| bigmap-zoom | 1280×960 | 1.732 → 1.739 | +0.4% (6.3%) | +0.8% (19.5%) | -1.3% (52.8%) | 0.073 → 0.074 | -1.3% (7.2%) | 0 → 0 | 176 → 177 | 0.03/0.00 → 0.02/0.00 |
| constrained | 1280×960 | 11.646 → 11.841 | +1.7% (3.4%) | +1.4% (4.0%) | -0.3% (19.4%) | 0.106 → 0.095 | -3.9% (12.2%) | 91 → 80 | 126 → 125 | 0.42/0.15 → 0.27/0.14 |
| jump | 1280×960 | 0.867 → 0.897 | +3.5% (16.3%) | +11.7% (23.2%) | +39.7% (68.4%) | 0.018 → 0.021 | +8.1% (20.0%) | 0 → 0 | 171 → 171 | 0.00/0.00 → 0.00/0.00 |
| pan | 1280×960 | 8.217 → 8.400 | +2.2% (6.1%) | +2.6% (7.4%) | +4.4% (6.8%) | 0.029 → 0.032 | +10.4% (6.9%) | 0 → 0 | 122 → 122 | 0.00/0.00 → 0.00/0.00 |
| terrain-jumps | 1280×960 | 0.285 → 0.305 | +7.3% (8.1%) | +2.5% (14.3%) | +5.8% (90.9%) | 0.018 → 0.043 | +241.8% (51.1%) | 0 → 79 | 205 → 156 | 0.00/0.00 → 0.60/0.00 |
| terrain-sweep | 1280×960 | 0.292 → 0.239 | -18.3% (64.7%) | -21.3% (39.6%) | -46.0% (73.2%) | 0.167 → 0.120 | -14.5% (44.7%) | 601 → 0 | 241 → 108 | 0.90/0.00 → 0.98/0.00 |
| terrain-zoom-osc | 1280×960 | 2.178 → 2.133 | -2.1% (5.6%) | -1.7% (6.5%) | -5.1% (34.1%) | 0.033 → 0.035 | -7.5% (33.5%) | 0 → 0 | 158 → 158 | 0.00/0.00 → 0.00/0.00 |
| zoom | 1280×960 | 10.589 → 10.792 | +1.9% (13.6%) | +2.4% (15.8%) | +0.1% (16.9%) | 0.077 → 0.082 | -12.3% (14.3%) | 0 → 0 | 176 → 177 | 0.01/0.00 → 0.01/0.00 |
| battle-sparse | 7680×2160 | 3.543 → 3.507 | -1.0% (3.8%) | -1.0% (5.9%) | -3.7% (8.6%) | 0.216 → 0.250 | -1.8% (7.2%) | 0 → 0 | 369 → 367 | 0.07/0.00 → 0.09/0.00 |
| bigmap-zoom | 7680×2160 | 2.969 → 2.946 | -0.8% (5.5%) | -0.1% (4.8%) | +2.8% (14.4%) | 0.661 → 0.644 | +23.5% (45.6%) | 360 → 60 | 397 → 405 | 0.36/0.00 → 0.33/0.00 |
| constrained | 7680×2160 | 15.824 → 15.713 | -0.7% (3.2%) | -0.3% (3.6%) | +0.5% (2.8%) | 1.532 → 1.354 | -10.5% (7.5%) | 0 → 0 | 128 → 128 | 1.00/1.00 → 1.00/1.00 |
| jump | 7680×2160 | 3.302 → 3.288 | -0.4% (4.6%) | +0.0% (5.9%) | -0.1% (7.7%) | 0.060 → 0.068 | +13.1% (10.6%) | 0 → 0 | 387 → 388 | 0.00/0.00 → 0.00/0.00 |
| pan | 7680×2160 | 14.824 → 14.664 | -1.1% (6.6%) | -1.1% (5.9%) | -1.1% (15.8%) | 0.081 → 0.091 | +11.5% (5.2%) | 0 → 0 | 275 → 275 | 0.00/0.00 → 0.00/0.00 |
| terrain-jumps | 7680×2160 | 1.219 → 1.240 | +1.7% (12.9%) | +1.5% (6.9%) | +15.7% (45.3%) | 0.879 → 0.904 | +13.9% (43.9%) | 1472 → 100 | 303 → 314 | 1.00/0.00 → 1.00/0.00 |
| terrain-sweep | 7680×2160 | 0.864 → 0.867 | +0.3% (6.4%) | -1.0% (3.1%) | -11.9% (30.3%) | 0.646 → 0.655 | -14.4% (11.8%) | 1377 → 0 | 218 → 205 | 1.00/0.00 → 1.00/0.00 |
| terrain-zoom-osc | 7680×2160 | 4.141 → 3.901 | -5.8% (9.8%) | -3.3% (9.3%) | -10.4% (16.4%) | 1.284 → 0.925 | -68.1% (43.0%) | 1464 → 293 | 403 → 402 | 0.93/0.00 → 0.37/0.00 |
| zoom | 7680×2160 | 15.106 → 15.167 | +0.4% (7.8%) | +1.0% (9.2%) | +1.7% (12.8%) | 0.081 → 0.092 | +13.7% (9.6%) | 0 → 0 | 275 → 275 | 0.00/0.00 → 0.00/0.00 |

**Materiality** (the tool's rule, max(2 × A/A spread, 2%)): no frame-level
metric (total mean, total p99, draw mean, GPU timeline) is material in any
scene at either size. Material phase/worker deltas: constrained `t_terrain`
−10.4% / −11.6% (1280×960 / 7680×2160); bigmap-zoom worker compose −11.2% /
−20.6% and wasted −83%; terrain-sweep worker compose and wasted −100%;
terrain-jumps 7680×2160 compose −79%, wasted −93%; terrain-zoom-osc
7680×2160 wasted −80%; and two costs: terrain-jumps 1280×960 `t_terrain`
+140% (0.018 → 0.043 ms: the jump storm leaves 60% of frames to the
fallback, which draws the same pixels), and pan 7680×2160 `t_terrain` +12.8%
(+0.010 ms: per-frame bookkeeping, fixed in K5, 13b).

**What K changed, read from the counters:**
- *Wasted work:* the pre-K build composed 601 / 1377 ms of terrain per
  terrain-sweep run that it then threw away, 1472 ms in terrain-jumps
  7680×2160, 1464 ms in terrain-zoom-osc 7680×2160, 360 ms in bigmap-zoom
  7680×2160; K: 0 / 0 / 100 / 293 / 60 ms. That worker time competes with
  the simulation and render pool for the same CPUs.
- *Zoom oscillation at 7680×2160* (both levels exceed the chunk budget):
  frames with holes 93% → 37%, `t_terrain` 1.28 → 0.93 ms, its p99 −68%
  (7.2 → 2.3 ms, inside that metric's 43% spread), draw −5.8% (noise 9.8%).
  The level grace keeps both levels' work; stand-ins survive. The price is
  upload frames: K uploads ~900 chunks per run vs 365–601, total p95 +4.7%
  here and +31.8% (material) in the quieter 13b session (below).
- *Fast sweep* (zoom 1 only — the gate's scope at other zooms was wrong and is fixed in Experiment 13c): the calm gate queues nothing while the camera crosses more
  than half a view per second, so the fallback draws the new cells (as it
  effectively did before, when every composed chunk arrived too late). At
  1280×960 the pre-K build uploaded 0–94 late chunks per run (timing
  dependent): total −21%, p99 −46%, tracked GPU 241 → 108 MiB, all inside that
  scene's large spread.
- *Memory pressure (constrained):* hole frames 42% → 27% at 1280×960,
  `t_terrain` −10% (material), underlay frames 15.3% → 14.2%; at 7680×2160 the
  128 MiB cap leaves no chunk room in either build (holes and underlay in
  every frame), `t_terrain` −11.6% (material).
- *Jump storm (terrain-jumps):* views last 0.33 s. K cancels the previous
  view's work and gates new work during the storm, so it composes and uploads
  little: wasted −93%, tracked GPU at 1280×960 205 → 156 MiB. At 1280×960 the
  pre-K build had every view cached from warmup and K keeps more frames on the
  fallback (+0.025 ms `t_terrain`); at 7680×2160 total p99 +15.7% (noise 45%).
- *Ordinary scenes* (jump, pan, zoom, battle-sparse, bigmap-zoom): frame
  times within noise; the per-frame bookkeeping was visible only in
  `t_terrain` of still views (+3–10 µs), fixed in K5.

**13b — K5 vs K0 in the still and zoom scenes** (same protocol, quieter
machine: load 1.2–2.1, no `legion_scenario`; `runs/k5-*`,
`~/tak-tmp/render-opt/k/measure2.sh`):

| scene | size | draw A → B | Δ draw (noise) | Δ total mean (noise) | Δ total p99 (noise) | t_terrain A → B ms | Δ t_terrain p99 | wasted compose ms/run A → B | tracked GPU MiB A → B | hole / underlay frames A → B |
|---|---|---|---|---|---|---|---|---|---|---|
| jump | 1280×960 | 0.744 → 0.737 | -0.9% (2.6%) | -0.1% (22.0%) | +7.3% (74.9%) | 0.014 → 0.014 | +16.0% (9.9%) | 0 → 0 | 171 → 171 | 0.00/0.00 → 0.00/0.00 |
| pan | 1280×960 | 7.132 → 7.261 | +1.8% (3.5%) | +1.8% (3.1%) | +1.0% (8.8%) | 0.023 → 0.025 | +5.1% (5.8%) | 0 → 0 | 122 → 122 | 0.00/0.00 → 0.00/0.00 |
| terrain-zoom-osc | 1280×960 | 1.777 → 1.765 | -0.7% (1.2%) | +1.5% (14.2%) | -1.3% (87.9%) | 0.018 → 0.019 | +5.4% (16.0%) | 0 → 0 | 158 → 158 | 0.00/0.00 → 0.00/0.00 |
| zoom | 1280×960 | 9.282 → 9.233 | -0.5% (13.7%) | -0.6% (15.4%) | -0.4% (8.4%) | 0.066 → 0.064 | -8.2% (15.9%) | 0 → 0 | 176 → 177 | 0.01/0.00 → 0.01/0.00 |
| jump | 7680×2160 | 2.870 → 2.829 | -1.5% (1.7%) | -0.9% (1.8%) | +0.1% (6.3%) | 0.040 → 0.042 | +6.4% (9.5%) | 0 → 0 | 387 → 389 | 0.00/0.00 → 0.00/0.00 |
| pan | 7680×2160 | 14.571 → 14.562 | -0.1% (7.2%) | +0.2% (7.3%) | +0.7% (16.4%) | 0.067 → 0.073 | +7.6% (5.0%) | 0 → 0 | 275 → 275 | 0.00/0.00 → 0.00/0.00 |
| terrain-zoom-osc | 7680×2160 | 3.488 → 3.434 | -1.5% (4.2%) | +0.6% (3.5%) | -8.8% (29.8%) | 1.008 → 0.804 | -55.4% (77.3%) | 1658 → 96 | 399 → 402 | 0.97/0.00 → 0.35/0.00 |
| zoom | 7680×2160 | 13.426 → 13.225 | -1.5% (2.7%) | -1.5% (3.2%) | -0.9% (4.4%) | 0.065 → 0.070 | +5.5% (16.3%) | 0 → 0 | 275 → 275 | 0.00/0.00 → 0.00/0.00 |

No frame-level metric is material except terrain-zoom-osc 7680×2160 total
p95 +31.8% and draw p95 +46.8% (tool rule; present +0.08 ms): in this quieter
session K0 delivered only 365 chunk uploads per run (601 in the busier K4
session; its FIFO gets fewer jobs done in time when frames are faster), K
~890 in both. K uploads about one chunk per frame there because it completes
the visible cells the stale FIFO dropped (hole frames 97% → 35%, `t_terrain`
−20%, wasted −94%, total mean +0.6%, total p99 −8.8%); a frame that uploads a
4 MiB chunk is a p95 frame on this driver (Known limitations). A smaller
non-urgent upload budget (`TAK_TERRAIN_DEFER_US=500`, a removed debug knob;
`runs/k6-d500-uhd`, `runs/k6-d2000v500-uhd`, ABBA × 2) changed neither the
upload count (896 vs 901) nor p95: the first upload of a frame always
proceeds, and these frames upload one. Recorded as a trade-off, not a gain.
`t_terrain` in still views: pan 7680×2160 +6 µs (67 → 73 µs, +9.5%,
material for that phase; frame time unchanged); same binary, legacy policy vs
K (`runs/k6-panlegacy-uhd`): 70.6 vs 72.9 µs, so ~2.3 µs is the policy's
per-frame work and ~4 µs is present in both modes (larger chunk records,
the deferred-upload pass and budget calls). K5 removed ~4 µs of the K4
bookkeeping (K4 session: 80.7 → 91.0 µs).

**Captures.** Two kinds, A vs B, 12 frames per scene (360, 480–489, 720),
world view and full frame:
- *Settled* (`TAK_TERRAIN_SETTLE=1`: every frame waits until the visible
  chunks are ready, so terrain readiness is equal in both builds): at
  1280×960 0 of 108 frames differ (9 scenes, no settle give-up); at
  7680×2160 0 differing frames in the 6 scenes that can settle (battle-sparse,
  jump, pan, terrain-jumps, terrain-sweep, zoom). terrain-zoom-osc, bigmap-zoom
  (views at zoom just above 0.5 need ~75 level-0 chunks, more than the 256 MiB
  budget) and constrained (128 MiB cap) cannot settle in either build, so they
  gave up and raced: 31 differing frames, 30 with different readiness counts
  at that frame, one (zoom-osc 489) with equal counts and a different set of
  ready cells. A/A and B/B pairs of the same two scenes differ just as much
  (K0 vs K0: 19 of 24 frames, one with equal counts; K vs K: 22 of 24). So K
  changes when terrain becomes ready, never what finished terrain looks
  like.
- *Normal* (readiness races included): 1280×960 12 of 108 frames differ:
  terrain-jumps 11 (K leaves the storm's views to the fallback, which at zoom
  0.75 resamples slightly differently from a chunk; frame 360 also differs in
  the minimap, built a few frames later by the asynchronous crunch over the
  map's ~1M blocks — K0 vs K0 shows exactly that minimap difference in the same
  frame, world view identical; K vs K differs in 11 terrain-jumps frames),
  constrained 1 (frame 360: the shadow atlas had fallen back in B, the side
  effect in Known limitations; identical within each build). 7680×2160 52 of
  108: battle-sparse 10 (frames 480–489, right after the jump to the empty
  view), bigmap-zoom 6, constrained 12, terrain-jumps 12, terrain-zoom-osc 12
  — 50 with different readiness counts, 2 (zoom-osc 488/489) with equal counts
  as above; jump, pan, zoom and terrain-sweep identical. No unexplained
  difference remains.

**Ablation (exploratory, one run per variant, same binary K3a = K without
the calm gate).** `TAK_TERRAIN_SCHEDULE=legacy | full | nonearby | nocancel |
fifo,nocancel,nonearby` on terrain-sweep, terrain-jumps, pan, zoom at both
sizes (`runs/k3a-ablate`): the extra chunk uploads that made K3a slower in the
fast scenes came from prioritization (uploads per run 87–223 with `nocancel`
or nearby off, 0–5 with `fifo,nocancel,nonearby` at both sizes), and nearby
prefetch added 56–80 MiB of tracked
GPU in pan/zoom. Then legacy vs K3a and legacy vs K3b (gate added), ABBA × 1
(`runs/k3a-legacy-ab`, `runs/k3b-legacy-ab`): see Rejected experiments.

**Tests (final code).** `terrain_work` (718 checks; nine header mutations
fail it), `terrain_schedule` (4 of 4 stable runs plus repeats; four in-game
mutations fail it: FIFO worker, no obsolete cancel, stand-ins not marked, no
nearby; upload order FIFO survives because only one 4 MiB chunk uploads per
frame and results complete in priority order), `terrain_cache` (the existing
edits / reset / fallback / budget test runs the K policy), `ctest -L quick`
390/390 on build-o2 at `b98183b1`. ThreadSanitizer: the test, `mapview.cpp`
and `terrainchunks.cpp` instrumented (the library uninstrumented): no report
over scenarios 1–7; scenario 8 cannot run under TSan (its fast pan relies on
frames under 0.9 s). Release `takclient` builds and contains none of the K
debug or telemetry strings. `--mpai` not run: only `src/client`, `tools/` and
CMake changed; every benchmark run checks the world hash.

### Experiment 13c — workstream K verifier fixes: calm-gate scope, lost wakeup (2026-10-17)

**What the verifier found** (K5 = `b98183b1`, the committed default):
1. The calm gate (on by default) was justified and timed only at zoom 1
   (level 0). At zoom 0.3 a fast pan over Middle Earth V1 was 1.4× slower at
   1280×960 and 3.2× slower at 7680×2160 than K0 (total mean 3.82 → 12.06 ms,
   `t_terrain` 0.80 → 8.15 ms, fallback holes 8 → 80 of 202 visible cells,
   vertices 116k → 509k per frame; `~/tak-tmp/kverify/sweep-z03`). The same
   binary with `TAK_TERRAIN_SCHEDULE=nogate` matched K0: the gate caused all
   of it. From level 1 a chunk image is 4^level times smaller, while the
   fallback still draws every 32-px tile and is no longer texel-identical.
2. `uploadReady` stored `readyHeld_` without the queue lock while the worker
   reads it in its wait predicate: a lost wakeup could leave visible cells
   pending while the camera rests (found by reading the code; not reproduced).
3. No in-game test covered holes-first upload ordering (a `prepare()` that
   uploads everything at once passed `terrain_schedule`).

**Fixes** (each its own commit):
- `918300a4`: the gate applies only up to `calmGateMaxLevel` (0);
  `readyHeld_` is stored under `mutex_`; `terrain_schedule` scenario 9
  (holes first: completed upgrades wait for `uploadDeferred` while holes
  remain) and 8b (a level-1 fast pan queues work); `terrain_work` checks the
  scope per level. Debug `gateall` = the K5 scope.
- `b96fe8fb`: timing 918300a4 (K6) showed the zoomed-out sweep back at
  baseline but a **jump storm at level 1 1.7× slower at 7680×2160**
  (terrain-jumps at zoom 0.4: K6 composed and uploaded every storm view, ~120
  one-MiB chunks each, more than the 256 MiB chunk budget holds for five
  views; K0's FIFO threw that work away). The gate now has two scopes: fast
  motion up to level 0 (`calmGateMaxLevel`), jump storms at every level
  (`stormGateMaxLevel` = 5). Debug `stormlevel0` = the K6 scope.
  `terrain_schedule` 8c: a level-1 storm is gated.
- `6cf541b0`: timing b96fe8fb (K7): the storm gate reopened 250 ms after each
  storm jump, so with views 333 ms apart every view still queued work for its
  last ~80 ms, which the next jump cancelled after part of it had uploaded:
  terrain-jumps 7680×2160 zoom 0.4 stayed +21% (every B run slower than
  every A run). A storm now ends only after `stormCalmMs` = 1000 ms without a
  jump (the storm window); fast motion keeps 250 ms. Final candidate K8 =
  `6cf541b0` (`~/tak-tmp/render-opt/takclient-K8-candidate`).

**Protocol.** A = K0 (`takclient-K0-baseline`), B = K6 / K7 / K8 as labelled;
`python3 tools/render_benchmark.py --data ~/tak_data --cpus 16-23 --a <K0>
--b <B> --wait-quiet 30 --repeats 2 --scenes <scene> --sizes normal uhd
[--env TAK_PROFILE_ZOOM=<z>]` (ABBA × 2 per scene and size), 900 frames,
300 warmup, forced completion, pinned to CPUs 16–23, nothing built during
timing; the tables show the zoom actually run. Offscreen EGL, SDL `opengl`,
RTX 5070 Laptop GPU, outputs 1280×960 / 7680×2160 confirmed per run by
`RENDER_ENV`. Load average 0.9–6.2 (the high end right after a build
finished, before the first runs of a session), pinned-CPU busy 0.00–0.02, no
`legion_scenario` process in any session; no run contended. World hash
`34b0f422b960ea97` in every run of both labels. Scripts
`~/tak-tmp/render-opt/k/measure6.sh` … `measure9.sh`, tables `k/ktable6.py`,
runs `runs/k6-*`, `k7-*`, `k8-*`, `k8f-*` (not in Git).

**Scenes chosen** (what the change can affect): the gate changed only at
level ≥ 1 and for jump storms. In K5 the gate fired only in terrain-sweep,
terrain-jumps, jump and battle-sparse (7680×2160, 2.5% of frames); zoom,
pan, bigmap-zoom, constrained and terrain-zoom-osc never gated (0 frames in
every K4/K5 run), so their behaviour is unchanged and they were not
re-timed. Timed: terrain-sweep at zoom 0.3 / 0.5 (level 1) and 0.75 / 1.0
(level 0, gate unchanged); terrain-jumps at 0.3 / 0.4 (level 1) and 0.75
(level 0, where K8's storm rule changed); the Ulasem `jump` scene (one jump a
second: not a storm); battle-sparse. terrain-sweep on Middle Earth V1 (32×32
chunks, the largest shipped map) at zoom 0.3 is also the "fast pan on a large
map while zoomed out" case.

Columns: total mean A → B; Δ = B vs A with noise = the larger A/A or B/B
spread; `t_terrain` mean; vertices submitted per frame; share of frames the
gate held new work (B); share of frames with fallback holes; worker time of
discarded jobs per run; tracked GPU memory.

*K6 (gate at level 0 only) vs K0:*

| run (zoom) | size | total mean A → B ms | Δ total mean (noise) | Δ total p95 (noise) | Δ total p99 (noise) | Δ draw (noise) | t_terrain A → B ms | verts submitted A → B | gated frames B | hole frames A → B | wasted ms/run A → B | GPU MiB A → B |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| k6-sweep-z0.3 (0.30) | 1280×960 | 1.161 → 1.176 | +1.4% (18.8%) | +15.2% (79.6%) | +12.5% (87.1%) | -1.8% (5.9%) | 0.090 → 0.086 | 1k → 1k | 0.00 | 0.27 → 0.20 | 18 → 0 | 313 → 321 |
| k6-sweep-z0.3 (0.30) | 7680×2160 | 3.841 → 3.819 | -0.6% (3.9%) | -28.1% (7.0%) | +2.6% (12.2%) | -1.0% (2.5%) | 0.809 → 0.785 | 116k → 115k | 0.00 | 0.57 → 0.57 | 5 → 10 | 424 → 424 |
| k6-sweep-z0.5 (0.50) | 1280×960 | 1.132 → 1.075 | -5.0% (25.7%) | -8.3% (65.0%) | -20.7% (105.2%) | +1.7% (6.6%) | 0.060 → 0.061 | 0k → 0k | 0.00 | 0.22 → 0.17 | 24 → 0 | 230 → 238 |
| k6-sweep-z0.5 (0.50) | 7680×2160 | 2.478 → 2.461 | -0.7% (4.7%) | -2.5% (4.8%) | -4.1% (15.6%) | -0.8% (2.0%) | 0.399 → 0.402 | 46k → 45k | 0.00 | 0.70 → 0.69 | 14 → 0 | 413 → 414 |
| k6-sweep-z0.75 (0.75) | 1280×960 | 1.175 → 1.108 | -5.8% (21.3%) | -10.3% (45.2%) | -2.8% (85.1%) | -0.0% (5.6%) | 0.131 → 0.133 | 11k → 11k | 1.00 | 0.98 → 0.98 | 688 → 0 | 125 → 121 |
| k6-sweep-z0.75 (0.75) | 7680×2160 | 2.886 → 2.889 | +0.1% (9.6%) | +3.5% (30.3%) | +1.5% (19.2%) | -1.4% (5.2%) | 1.065 → 1.053 | 180k → 181k | 1.00 | 1.00 → 1.00 | 1719 → 0 | 257 → 259 |
| k6-sweep-z1.0 (1.00) | 1280×960 | 1.195 → 1.160 | -2.9% (24.2%) | +22.1% (117.9%) | -15.9% (75.1%) | +0.6% (1.4%) | 0.097 → 0.100 | 6k → 6k | 1.00 | 0.97 → 0.98 | 697 → 0 | 118 → 108 |
| k6-sweep-z1.0 (1.00) | 7680×2160 | 2.048 → 2.063 | +0.8% (2.3%) | +0.3% (4.4%) | +6.0% (37.4%) | +1.7% (4.4%) | 0.617 → 0.633 | 86k → 88k | 1.00 | 1.00 → 1.00 | 1213 → 0 | 209 → 206 |
| k6-jumps-z0.4 (0.40) | 1280×960 | 1.182 → 1.186 | +0.3% (25.1%) | -21.1% (106.2%) | +0.8% (92.6%) | -0.9% (1.9%) | 0.018 → 0.018 | 6k → 6k | 0.00 | 0.00 → 0.00 | 0 → 0 | 169 → 169 |
| k6-jumps-z0.4 (0.40) | 7680×2160 | 4.284 → 7.319 | +70.9% (10.1%) | +10.6% (3.6%) | -2.8% (10.7%) | +88.4% (9.2%) | 2.390 → 4.846 | 382k → 276k | 0.00 | 1.00 → 1.00 | 2210 → 25 | 327 → 443 |
| k6-sparse (0.50) | 1280×960 | 6.258 → 6.185 | -1.2% (6.9%) | +1.2% (29.1%) | -1.3% (5.9%) | +0.1% (4.6%) | 0.048 → 0.049 | 222k → 222k | 0.00 | 0.01 → 0.01 | 0 → 0 | 150 → 150 |
| k6-sparse (0.73) | 7680×2160 | 6.835 → 6.870 | +0.5% (5.1%) | -0.8% (13.6%) | +1.6% (4.1%) | +1.4% (2.3%) | 0.186 → 0.216 | 240k → 246k | 0.03 | 0.07 → 0.11 | 0 → 0 | 369 → 367 |

The sweeps are back at baseline at every zoom and size (the verifier's 3.2×
case: 3.84 → 3.82 ms, p95 −28% material). The one regression is the level-1
jump storm at 7680×2160 (+70.9% total mean, material).

*K7 (motion gate level 0, storm gate every level, storms calm after 250 ms) vs K0:*

| run (zoom) | size | total mean A → B ms | Δ total mean (noise) | Δ total p95 (noise) | Δ total p99 (noise) | Δ draw (noise) | t_terrain A → B ms | verts submitted A → B | gated frames B | hole frames A → B | wasted ms/run A → B | GPU MiB A → B |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| k7-jumps-z0.4 (0.40) | 1280×960 | 1.122 → 1.180 | +5.2% (26.1%) | +13.5% (72.6%) | +1.6% (98.8%) | +8.2% (5.9%) | 0.018 → 0.036 | 6k → 8k | 0.75 | 0.00 → 0.26 | 0 → 8 | 168 → 179 |
| k7-jumps-z0.4 (0.40) | 7680×2160 | 4.080 → 4.926 | +20.7% (22.4%) | +10.2% (30.9%) | +22.5% (41.5%) | +28.3% (32.7%) | 2.120 → 2.838 | 347k → 282k | 0.45 | 1.00 → 0.92 | 2163 → 18 | 353 → 441 |
| k7-jumps-z0.3 (0.30) | 1280×960 | 1.028 → 1.219 | +18.6% (20.5%) | +39.9% (66.4%) | +67.7% (84.0%) | +17.3% (6.2%) | 0.022 → 0.067 | 5k → 12k | 0.75 | 0.00 → 0.49 | 0 → 18 | 203 → 205 |
| k7-jumps-z0.3 (0.30) | 7680×2160 | 16.750 → 10.852 | -35.2% (10.9%) | -18.5% (8.8%) | -0.9% (5.1%) | -36.9% (10.4%) | 13.076 → 7.992 | 686k → 633k | 0.45 | 1.00 → 1.00 | 1754 → 23 | 452 → 447 |
| k7-sweep-z0.3 (0.30) | 1280×960 | 1.043 → 1.205 | +15.6% (23.4%) | +37.3% (60.3%) | +135.5% (68.2%) | -2.1% (2.8%) | 0.088 → 0.083 | 2k → 1k | 0.00 | 0.33 → 0.19 | 23 → 0 | 312 → 322 |
| k7-sweep-z0.3 (0.30) | 7680×2160 | 3.867 → 3.854 | -0.3% (2.8%) | -7.8% (31.3%) | -2.3% (13.9%) | -0.9% (2.5%) | 0.803 → 0.807 | 115k → 115k | 0.00 | 0.57 → 0.56 | 6 → 11 | 424 → 424 |
| k7-sweep-z0.5 (0.50) | 1280×960 | 1.066 → 1.007 | -5.6% (23.4%) | -7.1% (38.5%) | -24.4% (126.1%) | -0.2% (3.7%) | 0.059 → 0.060 | 1k → 0k | 0.00 | 0.25 → 0.19 | 25 → 0 | 229 → 239 |
| k7-sweep-z0.5 (0.50) | 7680×2160 | 2.467 → 2.475 | +0.3% (3.2%) | -0.9% (6.3%) | -2.0% (15.0%) | -1.5% (2.7%) | 0.407 → 0.412 | 47k → 45k | 0.00 | 0.70 → 0.69 | 15 → 0 | 413 → 414 |

*K8 (storms calm after 1000 ms; the final candidate) vs K0, jump scenes:*

| run (zoom) | size | total mean A → B ms | Δ total mean (noise) | Δ total p95 (noise) | Δ total p99 (noise) | Δ draw (noise) | t_terrain A → B ms | verts submitted A → B | gated frames B | hole frames A → B | wasted ms/run A → B | GPU MiB A → B |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| k8-jumps-z0.4 (0.40) | 1280×960 | 1.197 → 1.162 | -3.0% (24.9%) | -32.5% (94.7%) | -32.3% (99.2%) | +25.4% (4.3%) | 0.018 → 0.083 | 6k → 30k | 1.00 | 0.00 → 0.80 | 0 → 0 | 168 → 169 |
| k8-jumps-z0.4 (0.40) | 7680×2160 | 4.451 → 3.213 | -27.8% (10.8%) | -71.7% (24.0%) | +6.7% (18.9%) | -37.4% (14.4%) | 2.456 → 1.379 | 375k → 240k | 1.00 | 1.00 → 1.00 | 2282 → 0 | 336 → 440 |
| k8-jumps-z0.3 (0.30) | 1280×960 | 1.164 → 1.228 | +5.5% (20.3%) | +0.8% (31.3%) | -23.5% (74.7%) | +40.4% (2.4%) | 0.022 → 0.128 | 5k → 47k | 1.00 | 0.00 → 0.75 | 0 → 0 | 202 → 184 |
| k8-jumps-z0.3 (0.30) | 7680×2160 | 16.607 → 8.472 | -49.0% (6.2%) | -44.6% (10.0%) | -7.3% (5.7%) | -52.8% (6.3%) | 12.969 → 5.802 | 685k → 549k | 1.00 | 1.00 → 1.00 | 1750 → 0 | 452 → 449 |
| k8-jumps-z0.75 (0.75) | 1280×960 | 1.059 → 1.170 | +10.5% (24.0%) | +14.1% (34.9%) | +32.6% (120.8%) | +9.8% (1.9%) | 0.017 → 0.041 | 17k → 24k | 1.00 | 0.02 → 0.70 | 0 → 0 | 204 → 150 |
| k8-jumps-z0.75 (0.75) | 7680×2160 | 2.565 → 2.440 | -4.9% (1.7%) | -24.9% (29.4%) | +32.0% (30.2%) | -5.8% (4.6%) | 0.872 → 0.815 | 130k → 165k | 1.00 | 1.00 → 1.00 | 900 → 0 | 390 → 256 |
| k8-jump (0.75) | 1280×960 | 2.096 → 1.899 | -9.4% (21.1%) | -42.6% (77.1%) | -6.4% (63.4%) | -0.7% (1.0%) | 0.013 → 0.014 | 63k → 63k | 0.05 | 0.00 → 0.00 | 0 → 0 | 171 → 171 |
| k8-jump (0.75) | 7680×2160 | 4.189 → 4.158 | -0.7% (4.1%) | -1.5% (4.9%) | -5.1% (10.0%) | -0.2% (2.6%) | 0.043 → 0.044 | 348k → 348k | 0.12 | 0.00 → 0.00 | 0 → 0 | 387 → 388 |

*K8 vs K0, sweeps and battle-sparse (the final binary on the verifier's repro):*

| run (zoom) | size | total mean A → B ms | Δ total mean (noise) | Δ total p95 (noise) | Δ total p99 (noise) | Δ draw (noise) | t_terrain A → B ms | verts submitted A → B | gated frames B | hole frames A → B | wasted ms/run A → B | GPU MiB A → B |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| k8f-sweep-z0.3 (0.30) | 1280×960 | 1.102 → 1.195 | +8.5% (16.8%) | +19.1% (43.6%) | +66.1% (90.2%) | +1.4% (4.9%) | 0.086 → 0.089 | 1k → 1k | 0.00 | 0.29 → 0.21 | 18 → 0 | 313 → 321 |
| k8f-sweep-z0.3 (0.30) | 7680×2160 | 3.766 → 3.841 | +2.0% (5.3%) | -14.2% (39.3%) | +4.0% (17.4%) | +1.5% (3.0%) | 0.771 → 0.809 | 116k → 115k | 0.00 | 0.58 → 0.57 | 5 → 13 | 424 → 424 |
| k8f-sweep-z0.5 (0.50) | 1280×960 | 0.988 → 0.934 | -5.5% (16.9%) | -5.9% (7.2%) | -42.0% (122.2%) | -1.1% (4.4%) | 0.060 → 0.059 | 1k → 0k | 0.00 | 0.26 → 0.19 | 25 → 0 | 229 → 238 |
| k8f-sweep-z0.5 (0.50) | 7680×2160 | 2.456 → 2.438 | -0.7% (5.6%) | -1.6% (6.2%) | -1.3% (7.1%) | -0.1% (4.3%) | 0.397 → 0.407 | 47k → 45k | 0.00 | 0.71 → 0.69 | 19 → 0 | 413 → 414 |
| k8f-sweep-z1.0 (1.00) | 1280×960 | 1.090 → 1.099 | +0.8% (21.3%) | +5.8% (50.2%) | -11.6% (88.5%) | +1.4% (2.9%) | 0.097 → 0.101 | 6k → 6k | 1.00 | 0.97 → 0.98 | 637 → 0 | 121 → 108 |
| k8f-sweep-z1.0 (1.00) | 7680×2160 | 2.080 → 2.052 | -1.3% (3.0%) | -2.6% (2.6%) | -5.0% (6.3%) | +0.3% (3.9%) | 0.610 → 0.620 | 86k → 88k | 1.00 | 1.00 → 1.00 | 1231 → 0 | 209 → 208 |
| k8f-sparse (0.50) | 1280×960 | 5.953 → 5.775 | -3.0% (5.1%) | -4.2% (18.2%) | -16.2% (33.4%) | -2.7% (6.8%) | 0.043 → 0.046 | 223k → 222k | 0.00 | 0.01 → 0.01 | 0 → 0 | 150 → 150 |
| k8f-sparse (0.73) | 7680×2160 | 6.628 → 6.721 | +1.4% (2.1%) | +0.9% (1.1%) | +3.2% (27.4%) | +2.1% (2.0%) | 0.182 → 0.225 | 240k → 246k | 0.03 | 0.08 → 0.11 | 0 → 0 | 369 → 367 |

**Reading.**
- *Zoomed-out fast pans* (the verifier's case): no frame-level metric is
  material at any zoom or size with K8 (7680×2160 zoom 0.3: 3.77 → 3.84 ms,
  +2.0% within a 5.3% spread; K5 was 12.06 ms). Wasted worker time −100%
  where K0 had any. Vertices submitted are equal (the fallback draws the same
  cells in both builds; K0's late chunks rarely arrive in time either).
- *Jump storms, 7680×2160:* K8 −28% (zoom 0.4), −49% (0.3), −4.9% (0.75)
  total mean, all material, p95 −72 / −45 / −25%; no worker time wasted
  (K0: 0.9–2.3 s per run).
- *Jump storms, 1280×960 — a cost, recorded:* total mean within noise, but
  draw mean +25.4% (zoom 0.4), +40.4% (0.3), +9.8% (0.75), material
  (`t_terrain` +0.065 / +0.106 / +0.024 ms). Here all five storm views fit the
  chunk budget, so K0's FIFO, which never cancels, eventually caches every
  view and draws chunks (hole frames 0.00); K8 composes nothing during a
  storm that never ends, so the fallback draws (hole frames 0.70–0.80). The
  level-0 part (zoom 0.75, +0.024 ms) is the trade-off already recorded for
  K4/K5 in Experiment 13 (+0.025 ms). Once jumps stop, the gate lifts within
  1 s and the view composes. Not tried: gating a storm only when its views
  cannot all stay cached (would keep K0's behaviour here; a heuristic on the
  number of views a storm revisits).
- *Ordinary scenes:* `jump` (one jump a second) and battle-sparse unchanged
  in frame metrics; battle-sparse 7680×2160 `t_terrain` +0.04 ms (material
  for that phase; also present in K4, 0.216 → 0.250 ms in Experiment 13; it
  runs at level 0 at the 0.73 clamp, where the gate is unchanged).
- The phase deltas the tool flags at 1280×960 in the sweeps (`t_shadow_pack`,
  `t_shadow_alloc` +16–30%) are sub-microsecond (0.000 → 0.001 ms with
  almost no unit on screen) and appear at zoom 1.0 too, where K8's decisions
  equal K5's.

**Captures** (`--capture-only --env TAK_TERRAIN_SETTLE=1`, K0 vs K8, 12
frames each, world view): terrain-sweep at zoom 0.3 and terrain-jumps at zoom
0.4, both sizes: 0 differing pixels in all 48 frames, no settle give-up
(`runs/k8-cap-settle-sweep03`, `runs/k8-cap-settle-jumps04`). Unsettled
captures were not repeated: K changes when terrain becomes ready, which
Experiment 13 showed differs between runs of the same build too.

**Tests (final code `6cf541b0`).** `terrain_work` 816 checks;
`terrain_schedule` PASS (new scenarios 8b, 8c, 9; three repeat runs at
918300a4 and two at b96fe8fb stable). Mutations, each rebuilt and run
against both tests: `prepare()` uploads everything (killed by scenario 9),
storm gate at level 0 only, motion gate at every level, storms counted as
motion, gate never (each killed by both tests). `terrain_cache` PASS at `6cf541b0`. `ctest -L quick` 389/389 on build-o2 at `6cf541b0` (`terrain_schedule` is no longer in `quick`: `tools/ctest_costs.txt` lists it since `df330946`; run directly). The lost-wakeup
fix has no deterministic test (the window lies between the worker's
predicate check and its wait). The Release `takclient` builds and contains none of `TAK_TERRAIN_SCHEDULE`, `TAK_TERRAIN_SETTLE`, `TAK_RENDER_SWEEP`, `terrain_gated_frames`, `gateall`, `stormlevel0`. `--mpai` not run: only
`src/client` and `tools/` changed; every benchmark run checks the world hash.

### Experiment 14 — workstream L unit broad phase (2026-10-17)

**Builds.** A = `takclient-K8-candidate` (code of `6cf541b0`, the last code
before L; `14da6214` only adds docs). B = `takclient-L4-candidate` (code of
`a42da929`: grid, painter candidates, overlays, registration record,
whole-list answers, ghost list). The final commit `e90c79a0` only moves
debug members under `NDEBUG` and adds report keys (the debug binary's code
is the same). Pinned `taskset -c 16-23`, `offscreen` video driver, SDL
`opengl` renderer, RTX 5070 Laptop (confirmed by `RENDER_ENV`), forced
completion, 900 frames, 300 warmup.

**Scenes and why.** `total10k-subset` (10,000 units, small visible subset:
the case L targets), `total10k-visible` (10,000 units in view: the case the
index cannot solve; the registration record is for it), `small-battle`
(ordinary 200-unit game: the index's overhead), `jump` (2,000 units, views
jumping between corners: partial subsets). Other matrix scenes have at most
2,000 units, nearly all visible.

```sh
~/tak-tmp/render-opt/l/measure.sh          # ABBA x2 per size, --wait-quiet 120
# re-run of the two 10k scenes at 1280x960 (one A run per scene contended, see below):
python3 tools/render_benchmark.py --a ~/tak-tmp/render-opt/takclient-K8-candidate \
  --b ~/tak-tmp/render-opt/takclient-L4-candidate --data ~/tak_data --cpus 16-23 --wait-quiet 300 \
  --repeats 2 --scenes total10k-subset total10k-visible --sizes normal --output ~/tak-tmp/render-opt/runs/l4-normal-b
# captures (separate runs, frames 360, 480-489, 720):
python3 tools/render_benchmark.py --a <K8> --b <L4> --data ~/tak_data --cpus 16-23 --capture-only \
  --scenes total10k-subset total10k-visible small-battle effects churn --sizes normal --output .../l4-cap-normal
python3 tools/render_benchmark.py ... --capture-only --scenes total10k-subset effects --sizes uhd --output .../l4-cap-uhd
```

Output: `~/tak-tmp/render-opt/runs/l4-{normal,normal-b,uhd,cap-normal,cap-uhd}`.
**Load.** Legion sweeps ran on cores 4–15 throughout (load average 2–36,
0–7 `legion_scenario` processes). In `l4-normal` one A run of
`total10k-visible` had the pinned cores 90% busy (7 sweeps running) and one
A run of `total10k-subset` 51%: both scenes were re-run (`l4-normal-b`, every
run 26–27% busy = the client itself, 0–6 sweeps); the 1280×960 numbers for
them below are from the re-run. Every other run: pinned cores 16–25% busy.
World hashes identical A vs B in every scene.

Captures: **identical** — 0 differing pixels in all 84 compared frames
(5 scenes at 1280×960, 2 at 7680×2160, 12 frames each; effects includes
flyers, corpses, construction, ghosts, projectiles; churn deaths/respawns and
target resets).

Material = `|Δ| > max(2 × the larger A/A spread of the two labels, 2%)`.
Means of run means; spread in brackets is the A/A spread of that label.

| Scene | Size | Draw mean A → B | Δ | Draw p95 Δ | Total mean Δ | `t_candidates` A → B | Material |
|---|---|---|---:|---:|---:|---|---|
| total10k-subset | 1280×960 | 2.238 (10.2%) → 1.357 (3.9%) | **−39.4%** | **−37.5%** | −12.5% (noise 11.6%) | 0.554 → 0.029 ms | draw mean, p95, `t_candidates` |
| total10k-subset | 7680×2160 | 3.251 (7.9%) → 2.249 (3.8%) | **−30.8%** | **−29.6%** (p99 −34.7%) | −11.3% (noise 7.9%) | 0.665 → 0.058 ms | draw mean/p95/p99, `t_candidates` |
| total10k-visible | 1280×960 | 15.553 (8.1%) → 15.130 (4.7%) | −2.7% | −1.5% | −0.9% | 0.769 → 0.368 ms | `t_candidates` only |
| total10k-visible | 7680×2160 | 60.272 (1.8%) → 59.416 (1.3%) | −1.4% | −1.3% | −0.9% | 0.771 → 0.388 ms | `t_candidates` only |
| small-battle | 1280×960 | 0.794 (12.2%) → 0.804 (5.9%) | +1.2% | +3.5% | +6.0% (noise 14.5%) | 0.011 → 0.013 ms | none |
| small-battle | 7680×2160 | 1.080 (21.2%) → 1.100 (17.7%) | +1.9% | −1.9% | +3.5% (noise 17.2%) | 0.026 → 0.028 ms | none |
| jump | 1280×960 | 0.940 (16.1%) → 0.841 (7.1%) | −10.5% | −9.8% | −4.8% | 0.075 → 0.026 ms | none (inside noise) |
| jump | 7680×2160 | 3.647 (29.6%) → 3.405 (25.2%) | −6.6% | −5.4% | −5.4% | 0.154 → 0.083 ms | none (inside noise) |

Counters (B, per frame): `total10k-subset` 10,002 live units, the painter's
exact test examines 224 (1280×960) / 585 (7680×2160) instead of 10,002; the
five grid queries return 784 / 3,380 units in 29 / 53 rows. `total10k-visible`:
every query covers the whole army (whole-list answers), the painter examines
8,752 / 10,000 as before; the gain there is the registration record
(`t_candidates` −52% / −50%). `small-battle`: 198 of 200 examined.
`unit_index_full_scans` = 0 everywhere.

**Costs.**

- Grid build on the capture thread: `w_unit_grid_build` 0.069–0.072 ms per
  snapshot with 10,002 units, 0.015 ms with 2,002, 0.002 ms with 200. In the
  render study the simulation runs inline, so this is inside each frame's
  `update` (every frame shows a new snapshot there); in play it runs on the
  sim worker once per tick (30 Hz), not per rendered frame.
- `small-battle` `t_candidates` +0.002 ms (the query and lift-bounds lookup
  for 200 units); draw within noise at both sizes.
- Memory: `unit_grid_bytes` 46 KB per snapshot at 10,002 units (5 KB at 200),
  times the 12 ring slots ≈ 0.55 MiB; capture scratch 12 B per unit
  (≈ 117 KB), render-side identity table + query list + bitset ≈ 8 B per
  unit; registration flags 1 B per unit id ever registered (the map they
  mirror holds a string per id). Not included in `cpu_cache_*` (snapshot data,
  not a cache with a budget). `frame_lists_capacity_bytes` unchanged.
- The remaining per-frame full scans in `total10k-subset` 1280×960 (measured
  with temporary timers on the L2 code, not committed): unit effects 0.45 ms,
  minimap dots 0.39 ms; neither is a visible-subset question (see the
  architecture section).

**What the index does not solve.** With 10,000 units genuinely visible the
frame is the per-unit work of drawing them (geometry keys, shadows, bodies,
GPU): draw 15.1 ms (1280×960) and 59.4 ms (7680×2160) on L4, unchanged within
noise. The index returns everything there; only the registration record
shaves 0.4 ms off the candidate pass, and the whole-list shortcut keeps the
five queries free.

**Experiments recorded (not committed).**

- Duplicating the type/visual/anim lookups of `buildUnitGeom` and the
  registration lookup (non-inlined helpers): no measurable change
  (`w_geometry_key` 13.07 vs 13.09 ms, `t_candidates` 0.736 vs 0.757 ms) — but
  the duplicate warms the cache for the real lookup, so this bounds only the
  warm cost.
- Removing the registration lookup (upper bound): `t_candidates` 0.753 →
  0.392 ms in `total10k-visible` 1280×960 → the dense record was built.
- A per-thread memo of the visual lookup by type in `buildUnitGeom`:
  `w_geometry_key` 13.44 vs 13.75 ms (inside noise; the roster interleaves
  types in painter order, so the memo rarely hits) → no visual record. It
  would also be unsafe by identity alone (corpse/veteran swaps rename a
  unit's visual in place).

### Experiment 15 — workstream M indexed structure palette (2026-10-17)

**Builds.** A = `takclient-L4-candidate` (code of `a42da929`, pre-M). M3 =
`takclient-M3-candidate` (`789126df`: layout + indexed form + native body
spans); M4 = `takclient-M4-candidate` (`ab9def88`: + special units' runs in
one pass). Captures of the first span build M1 (identical code for NEAREST
apart from the release fix and the later `usePage` filter argument).
Optimized Debug (`build-o2`, -O2 -g), SDL `opengl` offscreen EGL, NVIDIA
GeForce RTX 5070 Laptop GPU, GL 4.6.0 NVIDIA 615.71.09; every run's
`RENDER_ENV` checked by the tool (renderer, output size); forced-completion
frames; pinned to CPUs 16-23 (reserved for this program), 120 s quiet wait;
load average 0.9-2.5 and 0 `legion_scenario` processes in every timing run.
World hashes identical A vs B in every scene. Scenes chosen because they draw
palette-lit structures: `stationary-mixed` (a third of the roster are
lodestones; distant images at 1280×960), `structures` (new: all 51 types, all
nine pages, live bodies), `effects` (construction sites = special units,
flyers, ghosts), `pan-distant` (distant images at 1280×960; 7680×2160 clamps
to zoom 0.73). **Correction (verifier, Experiment 15b):** `pan-distant` does
NOT bake indexed runs. Its structures exceed the 28 px bake limit and stay
live, so captures and `palette_indexed_draws` are identical with and without
the run drawer. In these scenes no bake containing an indexed run was ever
compared against pre-M. The scene that bakes them is `structures` at
`TAK_PROFILE_ZOOM=0.4`: L4 vs M4 there gave 40-41 px per frame (490 over
12 frames, max channel error 140), every one a marked out-of-sheet decode,
which shows the Debug bake path was correct. Experiment 15b covers it. Scenes without structures run no
new code beyond a per-frame mode check (not timed, per the lean rule; the
final report's full matrix covers them).

```sh
# timing (ABBA x2 per scene and size), captures, marked captures, single runs:
~/tak-tmp/render-opt/m/measure.sh          # A=L4, B=M3; also the marked structures/uhd capture
~/tak-tmp/render-opt/m/m4.sh               # effects again with M4 (captures + timing)
python3 tools/render_benchmark.py --a L4 --b M3 --b-env TAK_PALETTE_BAKED=1 --allow-unverified-env \
   --capture-only --scenes structures stationary-mixed --sizes normal ...     # baked fallback vs pre-M
python3 tools/render_benchmark.py --a M3 --capture-only --scenes structures --sizes normal uhd \
   --env TAK_PALETTE_MARK_EDGES=1 --allow-unverified-env ...                  # marked
~/tak-tmp/render-opt/m/classify.py <A capture> <B capture> <marked capture>
~/tak-tmp/render-opt/m/roster.sh build-o2/takclient [TAK_PALETTE_BAKED=1]  # full roster residency
```

**Captures** (frames 360, 480-489, 720; world view and full frame):

| Comparison | Scenes | Result |
|---|---|---|
| L4 vs M1 (indexed) | stationary-mixed, effects, veteran, pan-distant, colour-reset @1280×960; stationary-mixed, effects, veteran, colour-reset @7680×2160 | identical (0 pixels) |
| L4 vs M1 (indexed) | structures @1280×960 | 7 pixels per frame (84) — all 84 marked out-of-sheet decodes |
| L4 vs M1 (indexed) | structures @7680×2160 | 30 pixels per frame (360) — all 360 marked |
| L4 vs M1 (indexed) | pan-distant @7680×2160 | 1 pixel in frame 489 — marked (148 marked decodes in the run, 147 invisible: the neighbour texel equals the edge texel or is overdrawn) |
| L4 vs M4 | effects @1280×960, @7680×2160 | identical |
| L4 vs M3 `TAK_PALETTE_BAKED` | structures, stationary-mixed @1280×960 | identical (the baked fallback is the pre-M output) |

"Marked" = the pixel differs between the indexed capture and a
`TAK_PALETTE_MARK_EDGES=1` capture of the same frame, i.e. an indexed decode
outside its sheet covers it (`classify.py`): 0 unexplained pixels. The unit
test proves the mechanism per triangle (`palette_indexed`: 2122 differing
pixels over 96 GL cases, every one a baked sample outside its sheet; the
native pass identical in 16 cases with interior bands).

**Residency and uploads** (gauges at the last frame; same M3 binary baked
vs indexed, single runs; RSS from the ABBA runs, L4 vs M3, peak per run):

| Scene | Palette pages | Palette GPU baked → indexed | Tracked GPU baked → indexed | Sheet uploads baked → indexed | Peak RSS L4 → M3 (4 runs each) |
|---|---|---|---|---|---|
| stationary-mixed 1280×960 | 1 | 16.00 → 0.76 MiB | 121.5 → 106.3 MiB | 12.50 → 0.38 MiB | 597-600 → 544-549 MiB |
| stationary-mixed 7680×2160 | 1 | 16.00 → 0.76 | 274.4 → 259.1 | 12.50 → 0.38 | 664-674 → 606-620 |
| structures 1280×960 | 9 | 144.00 → 4.76 | 252.9 → 113.6 | 130.81 → 3.40 | 648-684 → 485-498 |
| structures 7680×2160 | 9 | 144.00 → 4.76 | 404.1 → 264.9 | 130.81 → 3.40 | 688-713 → 517-556 |
| effects 1280×960 | 1 | 16.00 → 0.76 | 142.6 → 127.4 | 12.65 → 0.38 | 554-574 → 510-530 |
| effects 7680×2160 | 1 | 16.00 → 0.76 | 276.3 → 261.1 | 12.65 → 0.38 | 567-593 → 520-547 |
| pan-distant 1280×960 | 1 | 16.00 → 0.76 | 126.0 → 110.8 | 12.50 → 0.38 | 607-615 → 564-569 |
| pan-distant 7680×2160 | 1 | 16.00 → 0.76 | 274.5 → 259.3 | 12.50 → 0.38 | 631-645 → 581-596 |

Palette CPU (`palette_cpu_bytes`): 6.69-6.73 MiB in both forms (sources,
banks, sheet records); the pre-M source records for the same 3008 textures
held 39.39 MiB (`TAK_PALETTE_ROSTER_TEST`'s count of the former structure:
palette + shade copy + RGBA frames), so −32.8 MiB CPU. The whole-process RSS
drop is larger than that (−45…−60 MiB with one page, −150…−195 MiB with
nine); the remainder is consistent with the driver's host-side copies of the
baked pages and the expansion scratch, but that attribution is not measured.
Full roster (`--firetest`): 9 pages, 144.00 → 4.76 MiB GPU, uploads 130.81 →
3.40 MiB.

**Timing** (ABBA ×2, A = L4, B = M3; "sig." = beyond max(2 × A/A run spread,
2 %) — the tool's rule):

| Scene | Size | draw mean A → B | Δ draw | total Δ | `t_body_submit` A → B | flushes A → B | GPU timeline Δ |
|---|---|---|---|---|---|---|---|
| stationary-mixed | 1280×960 | 2.60 → 2.53 | −2.8 % (ns) | −4.0 % (ns) | 1.22 → 1.17 ms (−3.9 % **sig.**) | 69 → 31 | +3.2 % (ns) |
| stationary-mixed | 7680×2160 | 4.14 → 3.30 | **−20.3 % sig.** | **−17.6 % sig.** (p99 −33.6 %) | 2.14 → 1.37 ms (**−36 % sig.**) | 591 → 56 | **−28.9 % sig.** |
| structures | 1280×960 | 5.25 → 4.96 | **−5.6 % sig.** (p99 −18.4 % sig.) | −2.5 % (ns) | 4.00 → 3.71 ms (−7.3 % **sig.**) | 15 → 17 | −4.4 % (ns) |
| structures | 7680×2160 | 11.29 → 11.01 | −2.5 % (ns) | −2.4 % (ns) | 8.98 → 8.71 ms (−3.1 % ns) | 11 → 24 | −4.0 % (ns) |
| effects | 1280×960 | 1.92 → 1.90 | −1.2 % (ns) | −4.2 % (ns) | 0.39 → 0.38 ms (−3.8 % **sig.**) | 45 → 33 | +5.1 % (ns) |
| effects | 7680×2160 | 2.30 → 2.35 | +2.2 % (ns) | +0.1 % (ns) | 0.69 → 0.77 ms (**+11.4 % sig.**) | 41 → 50 | +4.8 % (ns) |
| pan-distant | 1280×960 | 8.18 → 7.84 | −4.1 % (ns, bimodal in both) | −3.0 % (ns) | 2.22 → 1.84 ms (−17.2 % ns) | 290 → 40 | −4.9 % (ns) |
| pan-distant | 7680×2160 | 13.32 → 12.09 | −9.3 % (ns, spread 9.9 %) | −7.7 % (ns) | 7.77 → 6.58 ms (−15.3 % ns) | 598 → 63 | −12.5 % (ns) |

M4 (special units in one pass) re-timed on effects: 1280×960 draw +1.3 %
(ns), `t_body_submit` −2.9 % (ns); 7680×2160 draw +4.9 % (ns, spread 10.6 %),
`t_body_submit` 0.705 → 0.788 ms (+11.8 % sig.). In effects at 7680×2160 no
special unit is in view; its 584 features end the spans (21 indexed runs
need 19 spans: 39 flushes where L4 had 31, because pre-M the small palette
runs rode in SDL's batch). That +0.08 ms is the recorded cost of M; draw and
total stay within noise. The gains elsewhere come from the spans, not from
the palette form itself: a span draws the atlas and distant-image commands
between two palette runs natively too, under one priming, where pre-M each
command of 512+ vertices primed (and flushed) on its own -- 591 flushes per
frame in stationary-mixed 7680×2160. That is a narrow, M-scoped piece of
workstream N; spans open only at indexed runs.

**Tests.** `palette_sheets`, `palette_indexed` (GL and software),
`geometrysubmit` (software and GL, unchanged behaviour), `render_palette_indexed`
(ctest: software; by hand on GL: 160 indexed + 140 baked frames, 4 switches,
0 fallbacks, `TAK_GEOMETRY_VERIFY` on, same world hash as software),
`TAK_PIECE_VISIBILITY_TEST` (Aramon Keep palette bands, both forms),
`TAK_PALETTE_ROSTER_TEST` (both forms), `render_texture_reuse`; `ctest -L
quick` 393/393 at `3c9becf7` and 392/392 on the final code `ab9def88`
(`render_palette_indexed` became costed). Release `takclient` builds and holds
none of the M debug strings. `--mpai` not run: only `src/client`
(presentation), `tools/` and CMake changed; every benchmark run checks the
world hash.

### Experiment 15b — workstream M verifier fixes: release bakes, exact out-of-sheet decode (2026-10-17)

The verifier rejected M (head `49fc8541`) for five reasons. This entry
covers each one; commits `502e45d1` and `fc13a7fb`.

1. **Release-only visual defect (blocking), fixed in `502e45d1`.**
   `distantModelCache_.setRunDrawer(...)` sat inside the `#ifndef NDEBUG`
   block with the `TAK_DISTANT_*` switches. Release builds still choose the
   indexed form on SDL `opengl`, so without the drawer DistantModels baked
   indexed runs with plain `SDL_RenderGeometry`. That draws the 1×1 white
   key texture at the sheet-encoded coordinates: grey, vertex-lit
   structures in distant images. The verifier reproduced it with a
   release-equivalent mutant: 6.5k px per frame, max error 247, `structures`
   at zoom 0.4. My Debug-only evidence could not see it.
   - **Fix.** `updatePaletteMode()` installs the drawer in every build, next
     to the decision that may choose indexed. In the release binary
     (`build-rel`, `-O3 -DNDEBUG`), `GameView::updatePaletteMode` now
     stores `drawModelRunThunk` (0x5e82a0) into the cache
     (`movq $0x5e82a0,0x658(%rdi)` at 0x5e9270, objdump).
   - **Audit.** I checked every `#ifndef NDEBUG` block in the files M
     touches. None holds M functionality any more: only harness accessors,
     `renderStudyToggleFilter`, and the debug switches read through
     `devEnv`.
   - **Coverage.** The drawer counts the indexed runs it bakes
     (`PaletteTotals::bakeRuns`, printed as `PALETTE_STUDY ... bake_runs=`).
     `TAK_REQUIRE_PALETTE=bakes` fails an OpenGL study that baked none. On
     GL (offscreen, NVIDIA, actual output 1024×768) the `render_palette_indexed`
     scene at `TAK_PROFILE_ZOOM=0.4` with `TAK_REQUIRE_PALETTE=bakes`
     passed: `indexed_frames=160 baked_frames=140 switches=4 fallbacks=0
     bake_runs=7150`, with the geometry verifier on. The release binary cannot
     run the harness (`devEnv` is a constant `nullptr` there), which is why
     the fix removes the build dependence instead of testing around it.
2. **Coverage claim corrected.** `pan-distant` never bakes an indexed run
   (Experiment 15 now says so). The scene that does is `structures` at zoom
   0.4, 1280×960. At 7680×2160 the map-fills-window clamp raises that request
   to 0.73, so no bakes happen there. It is in the captures below.
3. **Doc contradiction fixed.** The limitation now says a special unit's
   runs draw in one pass (`ab9def88`), and only distant-image bakes draw
   each indexed run with its own priming.
4. **`TAK_BODY_SDL_SUBMIT` fixed** (`502e45d1`). The reference switch now
   selects the baked form, so a pure-SDL reference run opens no native span
   and draws no indexed run.
5. **Out-of-sheet pixel class: removed, not ruled on** (`fc13a7fb`). Since
   sheet origins are multiples of (4, 5), every baked page texel (X, Y)
   maps to index texel (X/4, Y/5) inside the one sheet whose baked rectangle
   holds it.
   - **Owner texels.** Index pages are now 512² RGBA (index, owner = sheet
     id + 1, alpha). For a texel outside its own sheet, the shader decodes
     with the owner's entry, exactly as the baked page shows the neighbour.
     No owner gives transparent black (never-written baked memory reads zero
     on this driver; see Known limitations).
   - **Tests.** `palette_sheets` checks the CPU model against full baked
     pages at every one of 8,388,608 texels: 3.67 M own sheet, 3.64 M
     neighbouring sheet, 1.08 M unused. `palette_indexed` on GL (NVIDIA RTX
     5070 Laptop) is now pixel-identical in all 48 NEAREST cases (1326
     pixels took the owner path) and in all 16 native-pass cases, which now
     sample any band. With the owner lookup mutated
     (`if (owner > -1.0) return vec4(0.0)`) the NEAREST cases fail
     (`FAIL nearest ... 69 pixels differ`, ...). The experimental linear
     program improved as a side effect: max channel error 3 (was larger at
     sheet edges).
   - **Status.** No pixel class waits for a ruling any more.

**Captures**, A = `takclient-L4-candidate` (pre-M), B =
`takclient-M5-candidate` (code of `fc13a7fb`). Optimized Debug; SDL
`opengl` offscreen, NVIDIA GL 4.6.0 615.71.09; frames 360, 480-489, 720;
CPUs 16-23; 0 `legion_scenario` processes. Load was 1.3-10 during the
captures because my ctest ran at the same time on cores 4-15; captures only,
no timing claims from them. World hashes were identical between A and B:

| Scenes | Size | Worst world-view differing pixels |
|---|---|---|
| structures, stationary-mixed, pan-distant, effects | 1280×960 | 0 (12 frames each) |
| structures, stationary-mixed, pan-distant, effects | 7680×2160 | 0 |
| structures `TAK_PROFILE_ZOOM=0.4` (indexed runs in bakes) | 1280×960 | 0 (M4: 40-41 per frame) |
| structures `TAK_PROFILE_ZOOM=0.4` (clamped to 0.73) | 7680×2160 | 0 |

`structures` at its normal zoom was 7 px/frame (1280×960) and 30
(7680×2160) with M1-M4; both are now 0.

**Timing**, M4 (A) vs M5 (B), to isolate the cost of the owner path and the
larger page. ABBA ×2, CPUs 16-23, 30 s quiet wait, forced-completion
frames. Load 1.2-4.0 and 0 `legion_scenario` processes. Scenes are the ones
that draw palette-lit structures live; pan-distant and effects run the same
shader on fewer pixels and were not re-timed:

| Scene | Size | draw mean A → B (ms) | Δ | noise (max A/A) | GPU timeline A → B | material |
|---|---|---|---|---|---|---|
| structures | 1280×960 | 5.048 → 4.992 | −1.1 % | 6.7 % | 3.73 → 3.63 | no |
| structures | 7680×2160 | 11.289 → 11.350 | +0.5 % | 5.4 % | 8.81 → 8.91 | no |
| stationary-mixed | 1280×960 | 2.917 → 2.960 | +1.5 % | 14.3 % (A drifted with load 2.6-4.0) | 1.37 → 1.39 | no |
| stationary-mixed | 7680×2160 | 3.350 → 3.362 | +0.3 % | 3.2 % | 1.87 → 1.87 | no |

`drawphase.body` +0.0 / +0.1 / +1.1 / −0.2 %, all inside noise. The owner
path costs nothing measurable here. The stationary-mixed 1280×960 pair is
noisy (A/A 14.3 %) and supports only "no material change". The Experiment
15 results against L4 (draw −20.3 % in stationary-mixed 7680×2160 from the
spans) are not re-measured: the spans are unchanged.

**Residency and uploads (M5, single runs; full roster with `roster.sh`):**
- **Full roster:** 9 pages. Palette GPU 9.26 MiB (M4: 4.76; baked 144.00).
  Sheet uploads 6.68 MiB (M4: 3.40; baked 130.81). Palette CPU 6.73 MiB
  (sources 6.55; pre-M 39.39).
- **Tracked GPU at the last frame, M3/M4 → M5:** +0.5 MiB per page in use,
  nothing else. structures 113.6 → 118.1 (1280×960) and 264.9 → 269.4
  (7680×2160). stationary-mixed 106.3 → 106.8 and 259.1 → 259.6. effects
  127.4 → 127.9 and 261.1 → 261.6. pan-distant 110.8 → 111.3 and 259.3 →
  259.8 MiB.
- **Against baked:** the indexed form still holds 15.6× less palette GPU
  memory (144 → 9.26 MiB) and uploads 19.6× fewer sheet bytes.

**Tests:**
- `palette_sheets` and `palette_indexed` (GL and software), both above.
- `render_palette_indexed`: the ctest (software) passed; the GL zoom-0.4
  `bakes` study passed (above).
- `distant_models`.
- `ctest -L quick` 392/392 on the final code (`fc13a7fb`; the docs
  commit changes nothing compiled).
- The Release `takclient` builds. It holds none of the `TAK_PALETTE*` /
  `TAK_REQUIRE_PALETTE` / `TAK_BODY_SDL_SUBMIT` strings, and it does hold
  the owner-lookup shader.
- `--mpai` not run: only `src/client` and `tools/` changed, and the world
  hashes are identical in every run.

## Known limitations and unsupported paths

- **M: never-written baked page memory is assumed to read zero.** A sample
  outside its own sheet resolves exactly like the baked page (neighbouring
  sheet through the owner texels; Experiment 15b: 0 px against pre-M). Where
  NO sheet was written, the indexed form returns transparent black, and the
  baked page returns whatever GL left there: SDL creates it without data,
  which GL leaves undefined. NVIDIA reads zero, so both forms agree on this
  machine. Another driver could show garbage in the baked form there, which
  pre-M builds would show too. (The first M builds repeated the sheet's
  edge texel instead: 7-40 px per frame, Experiment 15. Fixed in 15b.)
- **M: bilinear unit filtering keeps the baked pages** (144 MiB for the
  roster). The experimental linear program filters resolved colours but is
  not bit-identical (`palette_indexed` at 15b: 549k differing pixels over 48
  cases, 99.1 % by 1, max 3; before the owner texels 592k with larger errors
  where the baked page bleeds a neighbour). `TAK_PALETTE_INDEXED_LINEAR=1`
  (debug) runs it.
- **M: only SDL `opengl`.** Direct3D 11/12, Metal, OpenGL ES 2 and the
  software renderer use the baked form, whose output and cost are the pre-M
  ones (captures identical to L4 with `TAK_PALETTE_BAKED`). The indexed
  program needs GL ≥ 2.0 shaders in SDL's compatibility context (the native
  batch's own requirement), an integer output scale, and `GL_CURRENT_PROGRAM`
  reporting SDL's ARB program objects (true on NVIDIA and Mesa; exercised on
  NVIDIA only). Windows/macOS not run (no GL path is selected there by
  default).
- **M: native spans are scoped.** Opened only by an indexed palette run and
  ended by features, special units, the air-shadow boundary and textures
  with another blend mode or a colour/alpha mod. A special unit with an
  indexed run draws all its runs in one pass (`drawRunsNative`, `ab9def88`).
  Distant-image bakes still draw each indexed run with its own priming
  (`drawModelRun` through the run drawer). A general native pass
  (workstream N) would extend the spans to every body command.
- **M: the colour atlas sources stay resident.** `textures_` (one SDL
  texture per model texture frame, the colour atlases' source) is unchanged;
  palette-capable textures are among them. Not part of the shade-variant
  duplication M removes.
- **M: the index pages are not compacted.** A layout page keeps its 512² page
  while any sheet lives on it (sheets are never released before a form switch
  or teardown, as before); worst case 16 × 1 MiB.

- **L: the index does not make 10,000 visible units cheaper.** It narrows
  the units the passes examine; with the whole army in view it returns all of
  them (whole-list answer) and the frame is the per-unit drawing work
  (Experiment 14: `total10k-visible` draw unchanged within noise, only the
  registration record's −0.4 ms in the candidate pass).
- **L: full scans that remain.** The minimap dots (every unit is drawn on it),
  the unit-effect pass over `anims_` (0.45 ms with 10,000 units; its draw
  order is the hash map's iteration order, which a separate active-effect list
  could not reproduce without changing overlapping-sprite order), Tactical
  Dots (far zoom; would need altitude and interpolation bounds), the HUD's unit
  count and squad label, and pointer picking. `geomIndex_.assign(units)` (a
  40 KB fill) stays per frame.
- **L: the grid is built per snapshot on the capture thread** (0.07 ms per
  10,000 units): in the render study that is inside `update`, in play on the
  sim worker at the tick rate. Not split across `capturePool_`.
- **L: overlay bands for footprint-dependent offsets** (squad markers,
  production bars) cover every type in the registry; a type registered after
  the cache key was taken (registry size) would be missed — the registry is
  fixed for a game, and the verifier would throw.
- **L: in-game tests are coarse.** The 256-px cells hide band errors smaller
  than a cell in `render_unit_index` (40-px shrinks of a band survive it);
  band precision is proven by `unit_index` (ulp-located edges), which the
  renderer shares through `unitpass::*`.
- **L: backends.** No backend-specific code; exercised on SDL `opengl`
  (offscreen EGL) and the software renderer (ctests). Direct3D/Metal not run
  (not needed: CPU-only change).

- **K: "missing terrain" is mostly invisible in this engine.** A visible
  cell without a chunk image is drawn by the section fallback, texel-identical
  at zoom 1 (resampled slightly differently at fractional zoom); only a
  missing section shows the blurry underlay. In every timed scene except
  `constrained` the underlay never showed in either build. K's benefits are
  therefore wasted worker time, `t_terrain` where holes were heavy (zoom
  oscillation, memory pressure), and fewer/cheaper uploads under motion, not
  a visible quality change. "Missing visible terrain first" is implemented as
  holes-first ordering of chunk work and of uploads (chunk hole fills, then
  fallback sections, then upgrades).
- **K: the zoom-oscillation p95 trade-off.** In terrain-zoom-osc 7680×2160
  K uploads about one chunk per frame (the visible cells the pre-K FIFO
  dropped), so total p95 rises (+4.7% in Experiment 13, +31.8% material in
  13b) while mean is unchanged and p99, holes and `t_terrain` fall. Pacing
  non-urgent uploads harder did not help (the first upload of a frame always
  proceeds); a cheaper upload path (strips, or the driver stall understood)
  would.
- **K: the jump-storm trade-off.** In `terrain-jumps` (views of 0.33 s) K
  composes little, so at 1280×960 60% of frames use the fallback where the
  pre-K build had every view cached from warmup (`t_terrain` +0.025 ms,
  material by the rule, frame time not), and at 7680×2160 total p99 is +16%
  (inside its 45% spread). A storm only gates while jumps keep coming within
  1 s; a single jump is served at once, centre first.
- **K: view sets larger than the chunk budget.** At 7680×2160 with zoom just
  above 0.5 the level-0 view needs ~75 chunks (316 MB), more than the 256 MiB
  chunk budget; the level choice only coarsens for supersampling, so neither
  build can complete such a view (terrain-zoom-osc, bigmap-zoom), and the
  fallback draws the rest. K reduces the churn there (wasted −80% / −83%) but
  does not change the level rule (not in scope; a coarser level would change
  pixels).
- **K: driver stalls on some chunk uploads** (5–6 ms, cause not established;
  see Rejected experiments). They are in both builds; K avoids most uploads
  that do not pay off, it does not remove the stall.
- **K: the calm-gate thresholds** (half a view per second, 250 ms, 1 s storm
  window) were chosen from these scenes on this machine and the owner's clock;
  under the render study's fixed 1/60 s clock and in play they mean the same
  wall-clock behaviour. A machine whose uploads are much cheaper relative to
  the fallback would profit from composing during motion; not measured.
- **K: section uploads per frame** are now bounded by 32 MiB and 6 ms of
  estimated CPU submission as well as the count of 32. On this GPU the byte
  cap binds as before (~0.14 ns/byte); on a machine with slow uploads a large
  jump fills over more frames (an extra underlay frame is possible). Not
  measured on other GPUs or backends.
- **K: ThreadSanitizer** covered `terrainchunks.cpp` / `mapview.cpp` with the
  library uninstrumented (the compositor itself passed TSan in J); scenario 8
  (calm gate) cannot run under TSan's slowdown.
- **K: stand-in retention competes for GPU memory under the debug 128 MiB
  cap.** In `constrained` (128 MiB, below the 512 MiB Options minimum) K keeps
  the finer stand-ins that LRU would evict (chunk images 20.6 → 22.2 MiB on
  average at 1280×960, sections 8.0 → 6.7 MiB), and the optional shadow atlas
  then finds no room for its page in 28 more of 600 measured frames (86 → 114,
  deterministic in all eight runs), which draw through the existing
  direct-shadow fallback (draw +1.7%, noise 3.4%; the one constrained capture
  difference at 1280×960, frame 360). At 7680×2160 both builds fall back in
  every frame. Not measured at the Options caps (512 MiB and up). The chunk
  budget and the cap are unchanged: the effect is on which images are kept.
- **K: jump storms at 1280×960 draw the fallback.** While jumps keep coming
  less than 1 s apart, no new chunk work is queued at any level, so a storm
  over views that would all fit the chunk budget is drawn by the section
  fallback until 1 s after the last jump (terrain-jumps 1280×960: draw
  +10–40%, +0.02–0.11 ms, total within noise; the pre-K FIFO eventually
  cached all five views there). At 7680×2160, where the views do not fit, the
  same rule is 5–49% faster. Not tried: gating only storms whose views cannot
  stay cached.
- **K: Cartographer** uses the same MapView and policy (calm gate included:
  terrain after a fast pan is composed once the view rests; edits invalidate
  as before, covered by `terrain_cache`); the Cartographer application itself
  was not run.
- **K: not exercised:** Direct3D/Metal/software timing, other GPUs, visible
  windows; renderer-reset during heavy terrain work is covered by the tests
  (software renderer) and the `churn`/`colour-reset` scenes were not re-timed
  (their terrain is static).
- **J: the peak on a map above the section budget is unchanged.** The
  working-set rule lets a zoomed-out view that uses every section hold all of
  them (Middle Earth V1: 238 MiB, the same as before J) for as long as it
  keeps using them. The budget bounds the steady state: sections cold for 10 s
  are evicted down to 112 MiB. Maps under 128 MiB (97.7%) are unaffected.
- **J: redecodes remain on maps above the budget.** bigmap-zoom re-decodes
  37 (1280×960) / 58 (7680×2160) sections in 900 frames (about 1 ms of worker
  time each), all of them sections that had been cold for over 10 s.
  Panning back into such an area can show the blurry underlay for a frame or
  two where the fallback needs an evicted section and the chunk is not
  composed yet (demand decodes go first). Not observed in the bigmap-zoom
  captures (their first evictions come at frame 720, after the capture
  frames 480–489) and not otherwise measured; pre-J builds kept every
  section decoded.
- **J: decoded sections are not trimmed below the budget.** An idle trim like
  the mips' would be cheap in memory terms, but an evicted section costs a
  visible underlay frame in the fallback; only the budget evicts sections.
- **J: trims lag.** A buffer shrinks 5–7.5 s (and ≥ 120 frames) after its
  demand falls; demand that alternates more slowly than the window can trim
  and regrow once per cycle, and the backoff then widens the window (up to
  8×). Buffers below 128 KiB of capacity (floor 64 KiB × 2) are never
  trimmed, which includes each worker slot (~45 KB in the matrix scenes) and
  the smaller frame lists. A trim costs one allocation plus a copy of the live
  contents at the end of a frame (measured in battle-sparse, Experiment 12).
- **J: not trimmed by J.** F's shadow allocator records, H's arena records
  and G's image metadata follow their own policies (counted in the totals).
  The `Frame` snapshot triple buffer, effect batches and assets are outside
  the CPU totals (see the accounting exclusions).
- **J: process RSS does not necessarily fall.** J releases capacity to the
  allocator; glibc may keep it (dynamic mmap threshold, heap top only), so
  whole-process RSS can stay at its peak. End-of-run RSS in the benchmark is
  too noisy (150–300 MiB run to run) to show the 19–81 MiB released, and
  bigmap-zoom's peak RSS at 1280×960 is ~30 MiB higher with J (cause not
  established). `malloc_trim` was not added: its cost on a large heap is a
  render-thread hitch, and it was not measured.
- **J: the CPU peak is sampled at frame end**, so a reallocation's transient
  old-plus-new peak inside a frame is not seen. Byte counts are capacities,
  not allocator usable sizes.
- **J: thread-safety evidence.** The compositor's pins, decode dedup and
  eviction pass TSan in a 8-thread stress (`terrain_compositor`), and the
  participant-slot protocol passes TSan in `work_schedule`. The TerrainChunks
  mip trim (a mutex-guarded flag plus a worker-owned map) is covered only
  functionally (`terrain_cache`, `render_cpu_budget`), because the test needs
  SDL; it was not run under TSan.
- **J: not exercised.** A real game with the simulation worker (the study
  runs the simulation inline); Windows and macOS (`ThreadPool::participant`
  is a portable `thread_local`; unvalidated there); release-build costs of
  the trackers (debug-only verifier code compiles out; the trackers run in
  every build).

- **I: competition with the simulation is not measured.** The render study
  steps its local simulation inline on the main thread, so no simulation
  worker, `capturePool_` or server process competes with the pool during the
  timed frames. Planned passes wake fewer workers where the work is small
  (small-battle 13.9 → 7.3 per frame), which is what should help a real game,
  but no frame-matched benchmark exists for that case; no CPUs are reserved
  for the simulation (would need that measurement).
- **I: the affinity-sized pool only differs under a CPU restriction.** On an
  unpinned desktop the pool is `hardware_concurrency` threads before and
  after I; the benchmark's 8-CPU pin made H5's 24-thread pool oversubscribed.
  Experiment 11 separates this (pool 24 vs 8 with the new mechanics: draw
  −0.3% to −2.8%); the mechanics account for most of the gain and apply
  unpinned too. Windows and macOS use `hardware_concurrency` (no affinity
  query; unvalidated there).
- **I: estimates are calibrated on the optimized-Debug build** that every
  benchmark uses (per-unit key checks there include debug-only environment
  reads). A release build's per-item costs were not measured; a lower real
  cost only makes the plans wake slightly more workers than needed.
- **I: planning alone is neutral on frame time** in the timed scenes (same
  binary vs the uniform passes, Experiment 11): with 8 CPUs and hundreds of
  units every large pass still uses every thread; the plan's own cost is
  0.007–0.030 ms per frame (`t_schedule_plan`).

- **H: forced-completion cost in the GPU-bound dense 7680×2160 view.**
  total10k-visible at 7680×2160 with a `glFinish` per frame: frame total
  +3.9% (material; +3.6%, not material, in Experiment 10b), all of it in `present` (0.66 → 2.42 ms, uniform over
  arena frames; `TAK_BODY_ARENA_OFF=1` restores it; the buffer usage hint
  does not change it). Normal pipelined frames of the same scene are 7.8%
  faster (material). Explanation not proven (Experiment 10). A real game at
  that size and density is GPU-bound either way (34 ms of the frame is the
  direct-shadow fallback, F's four-page limit).
- **H: small commands stay assembled.** Commands below 512 vertices (in
  stationary-mixed 1280×960: 856 of 922 per frame, distant-image pages
  interleaved with live bodies) keep SDL's path and its copies, because a
  native draw costs a state prime + flush (`GeometrySubmit`); H removes the
  copies of native-size commands only. Moving small commands into the arena
  needs workstream N's native passes (one prime for a run of commands);
  with today's per-draw prime it is far slower (`TAK_BODY_ARENA_MIN=1`: draw
  +45% to +65%, Rejected experiments).
- **H: camera motion and moving armies stream.** Placement stays in the
  screen-space vertices (D's CPU arithmetic, bit for bit), so a pan changes
  every vertex and the frame streams exactly as before; the per-frame
  bookkeeping costs ~0.05 ms (`t_body_assembly` +9.5% on pan 1280×960, draw
  not material). A GPU-side placement transform would change rounding and need
  one draw per unit; not done.
- **H: GPU memory.** The arena adds tracked GPU memory equal to up to twice
  the retained body vertices (headroom), 64 MiB per storage, and during a
  drain both storages are live (≤ 128 MiB + 3 frames in flight in the densest
  view). It obeys 3/4 of the GPU cap, yields at 7/8 and is released 2 s after
  the last arena frame; with the binding 128 MiB `constrained` cap it never
  allocates (that scene streams). Its presence can make other cap-obeying
  caches meet their limit earlier only within that 3/4 margin; no matrix scene
  comes near it.
- **H: in-flight accounting is an estimate** (3 frames per orphaned/released
  storage); the driver's real retention is not observable.
- **H: backends.** The arena runs only on SDL's `opengl` renderer at integer
  output scales (as `GeometrySubmit`'s native path); everything else uses the
  assembled path unchanged. Exercised on SDL `opengl` (offscreen EGL, RTX 5070)
  and, simulated, on the SDL software renderer (`render_body_arena*`);
  Direct3D, Metal, GLES2, contexts without `glMultiDrawArrays` (the
  one-draw-per-range path is code-reviewed only) and real context loss were
  not run. Renderer resets were exercised (`TAK_RENDER_RESET`).

- **G: deferral covers placement- and material-only changes of held images.**
  Pose changes (idle animations, facing) and texture-frame changes of bodies
  that are not images still generate the body; in the stationary matrix
  scenes no held image has a deferrable change (`distant_body_deferred` 0), so
  G's gain there is the retained bounds. The deferral shows where a distant
  view pans (pan-distant: 427 bodies per frame).
- **G: distant images exist only at zoom ≤ 0.6.** On Ulasem Arena the
  7680×2160 view clamps to zoom 0.73, so G changes nothing there (guard runs:
  draw −1% to −2%, `t_distant` +0.001 / +0.011 ms — the per-frame debug-switch
  reads and the counters); measured at 1280×960 only. Other maps or larger
  windows were not measured.
- **G: the four-page image limit still refuses ~1700 bodies per frame in the
  10k view** (they stay on live geometry, as before; only the per-frame
  rescan is gone). The limit was not raised (the brief: no larger ceilings).
- **G: the distant images are not pixel-equal to live geometry** (they never
  were; Experiment 9, investigation): bilinear resampling at a fractional phase
  and the 67 ms freshness rule. G preserves that output exactly.
- **G: page release is per sweep** (every 60 prepares; images expire after 120
  unseen), so pages go ~2–3 s after their last image left the view, as images
  did before. Same-position migration rarely applies (Rejected experiments).
- **G: error-path accounting.** The bake budget now counts a bake when it is
  decided, before its target switch and tile clear run; if `SDL_SetRenderTarget`
  or the clear fails, the pre-G code did not count it. Only renderer errors
  reach that path.
- G has no backend-specific code. Exercised on SDL `opengl` (offscreen EGL,
  RTX 5070) and the SDL software renderer (`distant_models` with
  `allowSoftware`, `render_distant_lookup`; on the software renderer, which
  has no custom blend modes, pages composite with plain blending — tests
  only); Direct3D/Metal/GLES not run.

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
- **F: the packing-failure band.** A frame whose tiles fit four pages by
  area but not by the former packer falls back to direct shadows, as before
  F, and makes one former-packer attempt per frame (as the pre-F code did:
  `shadow_compactions` ≈ 1 per frame there). Since `ce2a3466` such a frame
  creates no page texture; pages held from before are released 2 s after
  they empty and stay released while the view stays in the band. (Before
  `ce2a3466` every failed repack created pages up to four, so they were
  destroyed and recreated every ~2 s — the verifier's finding; Experiment 8b.)
  At the real 2048² page size this band is dense 7680×2160 views of roughly
  6–9k visible units; no matrix scene sits in it, so it is reproduced with
  `TAK_SHADOW_PAGE_MAX=720` on stationary-mixed and covered by the
  `render_shadow_atlas_band` ctest.
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
  RTX 5070) and the SDL software renderer (static SDL2 2.32, the three
  `render_shadow_atlas` ctests); Direct3D/Metal/GLES not run.

## Rejected experiments

- **M, rejected after measurement: one priming per indexed run** (the first
  integration, uncommitted). Every indexed run drew through
  `GeometrySubmit::draw` with the program: stationary-mixed 1280×960 issued
  311 primes + flushes per frame, `t_body_submit` 1.19 → 2.67 ms and the GPU
  timeline 1.07 → 2.65 ms (single runs). Replaced by native spans (one
  priming for the following body commands): 28 spans, 32 flushes per frame,
  `t_body_submit` back to 1.20 ms. The first span version accepted only BLEND
  textures and stopped at every distant-image quad (311 spans): the
  premultiplied page mode was added.
- **M, not adopted: indexed bilinear by default.** Not bit-identical (see
  Known limitations); bilinear unit filtering keeps the baked form.
- **M, not adopted: the shader's own `floor(u·2048)` for NEAREST.** Would tie
  the texel choice to the GPU's sub-texel rounding at texel edges; the 2048×1
  identity texture makes the texture unit choose, as it does for the baked
  page.
- **M, not adopted: a page-sized sheet-id map** (to decode a texel without
  coordinates carrying the sheet id): 4-8 MiB per page, against 2 B per
  source texel + an exact integer offset in the existing coordinates.

- **L, not adopted: a resolved-visual record per unit** (cache the
  `visuals_` entry on the geometry entry). The corpse and veteran mesh swaps
  rename a registered unit's visual in place, so a record keyed by unit
  identity would go stale; a per-thread memo of the lookup measured nothing
  (Experiment 14). The visual and anim lookups stay in `buildUnitGeom`.
- **L, not adopted: an active-effect list for `drawUnitFx`.** Would save
  most of 0.45 ms with 10,000 units, but the pass draws in `anims_`' hash
  iteration order; a separate list changes the order of overlapping smoke and
  fire sprites (pixels), so it was not attempted under the "identical output"
  rule.

- **K, rejected by the verifier: the calm gate at every level** (K5 scope,
  `b98183b1`/`df330946`, the committed default until `918300a4`). Justified
  and timed only at zoom 1. At zoom 0.3 a fast pan on Middle Earth V1 was
  1.4× (1280×960) / 3.2× (7680×2160) slower than K0: from level 1 a chunk is
  4^level times smaller while the fallback draws every 32-px tile (vertices
  116k → 509k per frame). Debug `TAK_TERRAIN_SCHEDULE=gateall` restores it.
  Experiment 13c.
- **K, rejected after measurement: the calm gate at level 0 only** (K6,
  `918300a4`). Fixed the sweeps, but a level-1 jump storm at 7680×2160 was
  1.7× slower (every storm view composed and uploaded, more than the chunk
  budget holds). Debug `stormlevel0`. Experiment 13c.
- **K, rejected after measurement: storms calm after 250 ms** (K7,
  `b96fe8fb`). The gate reopened between a storm's jumps, queued work the next
  jump cancelled and uploaded what had finished: terrain-jumps 7680×2160 zoom
  0.4 +21% in every run. Replaced by `stormCalmMs` = 1000 ms (`6cf541b0`).
- **K, rejected after measurement: the policy without the calm gate** (K3a,
  an uncommitted build of the integration before the gate was added in
  `85bbb1eb`; binary `takclient-K3a`; Experiment 13, "ablation"). With
  priority + cancellation alone, chunk jobs finish while their cells are
  still visible, so they are uploaded, where the pre-K FIFO composed them too
  late and threw them away. Wasted worker time fell to ~0, but in the fast
  scenes the render thread paid for uploads that the section fallback did
  not need (it already draws those cells texel-identically, from sections
  shared by many cells): same binary, legacy vs K3a, terrain-sweep draw +82%
  / +14% (1280×960 / 7680×2160), total p99 +62% / +99%, terrain-jumps
  7680×2160 total +29% (all material except the 1280×960 p99), tracked GPU
  +192 / +157 MiB (the chunk cache filled to its budget). The ablation
  (one run each) showed the extra uploads come from prioritization itself
  (`nocancel` and nearby off still uploaded 87–223 chunks per run, FIFO 0–5). The calm
  gate replaced it.
- **K, opt-in (experimental): leading-edge prefetch** (`nearby`). While the
  camera moves, up to 4 jobs for the guard cells it moves toward, into free
  or cold room only. Ordinary pans (Ulasem `pan`, `zoom`) never had holes to
  prevent, and tracked GPU rose 56–80 MiB (prefetched chunks); with the calm
  gate a fast camera queues nothing anyway. Kept behind the debug token;
  `terrain_schedule` covers it.
- **K, fixed during development: upload starvation on slow uploads.** The
  first version admitted a non-urgent upload only within 2 ms of estimated
  cost; on the software renderer one 4 MiB upgrade alone exceeded that, so
  upgrades were refused forever (`terrain_cache` ran 5 min and failed instead
  of 23 s). The first upload of a frame now always proceeds.
- **K, fixed during development: prefetch never ran.** Nearby and kept work
  were first admitted only into free cache room; the cache sits at its
  256 MiB budget on a large map, full of cold LRU images, so no prefetch was
  ever queued (`terrain_jobs_nearby` = 0 in terrain-sweep). Off-view work may
  now replace cold images (not visible, not a stand-in, outside the keep
  band).
- **K, investigated, not solved: render-thread stalls of 5–6 ms in some
  frames that upload a 4 MiB chunk while the fallback draws holes** (about
  one upload in ten in terrain-sweep 7680×2160; the pre-K build shows them in
  terrain-zoom-osc too). Ruled out by experiment (each built, measured, and
  reverted): texture creation (refilling an evicted same-size texture: same
  spikes), RGBA vs native BGRA channel order (same), the native client-array
  submit of the fallback (`TAK_TERRAIN_SDL_SUBMIT=1`: same), uploading hole
  fills after the fallback draw instead of before (same). With a diagnostic
  `glFinish` after each upload, the GPU work of an upload averaged 0.56 ms
  (max 4.9 ms) and the spikes still appeared later in the frame. Cause not
  established (driver-side); no GPU profiler on this machine. The calm gate
  avoids most of the uploads that trigger it; splitting an upload into
  strips over several frames was not tried.
- **K, considered and not done:** composing a coarse level first as a quick
  stand-in for holes (extra composition; holes already show full detail from
  the sections); hashing or caching the fallback tile batch differently
  (outside K); a GPU-synchronized upload timer (the brief forbids it; the
  estimate prices CPU submission only).
- **J, rejected after measurement: a pure LRU section budget** (J2,
  `469f743a`, superseded by `3dc315c4`). Evicting the least recently used
  unpinned section whenever the cache passed the budget cycled sections
  through decode and eviction once the camera's working set exceeded the
  budget. On Middle Earth V1 (bigmap-zoom), 553 decodes of 255 sections
  (298 redecodes) in 900 frames at 7680×2160, and 176 redecodes at 1280×960.
  Under a deliberately small budget it also left the fallback without its
  sections: `render_cpu_budget` ran in 9 s instead of 17 s because the
  terrain was missing (the pre-J build takes 17.2 s on the same scene).
  Sections used within the last 10 s are now not evictable; redecodes fell to
  58 / 37.
- **J, considered and not done:** an idle trim of decoded sections under the
  budget (an eviction costs a visible underlay frame; see Known limitations);
  trimming buffers every frame or on every dip (the brief excludes it, and
  the tests prove it cannot happen); trimming worker slots from their own
  threads through an epoch flag (a slot whose worker never runs again would
  keep its memory, whereas the render thread can trim every slot between
  passes); per-buffer trims of F's allocator, H's arena records and G's
  metadata (they have their own policies); memory-mapped or compressed
  sections (would change the decode path and its pixels).
- **I, fixed after measurement: a hit's estimate read the entry's vector
  sizes** (I2 `68021102`). Those headers sit on cache lines a geometry hit
  never reads otherwise: stationary-mixed 1280×960 `t_geometry` 0.521 →
  0.612 ms (+17%), worker key time +9% against the same binary's uniform
  passes. A hit's estimate is now the key check alone (`590db308`).
- **I, rejected after measurement: capping the body copy at 4 threads**
  (I2; meant to leave memory bandwidth to others): stationary-mixed
  `t_body_assembly` 0.248 → 0.317 ms (+28%) against all threads. Removed.
- **I, superseded: planning the geometry pass by walking the entries** (I3):
  0.099 ms per frame at 8,752 visible units for the plan loop alone. Now a
  dense copy of the estimates is taken at claim time, where the entry's
  first cache line is written anyway, and planned in integer units (0.030
  ms).
- **I, considered and not done: tree/chained wake-up of workers, workers
  spinning before they sleep, CPUs reserved for the simulation.** With up to
  7 posted workers per pass the dispatch costs 0.006–0.026 ms per frame, and
  0.18 ms in total10k-visible (cause not established; Experiment 11) — a tree
  wake could only save part of that; spinning workers would hold CPUs between
  passes; a reservation needs the unmeasured simulation-concurrent case
  above.

- **H, rejected after measurement: a mapped write window**
  (`glMapBufferRange` with `GL_MAP_INVALIDATE_RANGE_BIT |
  GL_MAP_UNSYNCHRONIZED_BIT`, H2 `8d96905f`). Correct (captures identical,
  verifier clean) but mapping freshly orphaned storage took ~23 ms per 14 MB
  on the reference driver: total10k-visible 1280×960 draw p99 +29%, 7680×2160
  p95/p99 +63%, moving draw +14% (all material; Experiment 10). Replaced by
  staging in `bodyVerts_`' tail + one `glBufferSubData` (H3 `37ecd884`), which
  also drops the GL 3.0 dependency.
- **H, superseded: a single storage rewritten on overflow** (H3). Rebuild
  frames rewrote every arena vertex in one frame (5.3 / 13.2 ms against 1.7 /
  3.7 ms of streaming assembly in total10k-visible 1280 / 7680). Replaced by
  rollover with budgeted migration (H4 `d8744efe`).
- **H, measured and not adopted: buffer usage hints and chunked uploads.**
  `GL_STATIC_DRAW` / `GL_STREAM_DRAW` vs `GL_DYNAMIC_DRAW`, and 4 MiB upload
  chunks, on total10k-visible 7680×2160: within run-to-run noise in single runs
  and in a matched same-binary comparison (present 2.50 / 2.89 / 2.80 ms).
  `GL_DYNAMIC_DRAW` kept.
- **H, rejected after measurement: small commands drawn natively from the
  arena** (`TAK_BODY_ARENA_MIN=1`, same H4 binary, one ABBA group,
  `runs/h4-min1`). Every command drawn from the arena (native draws 69 → 925
  in stationary-mixed, 780 → 3,702 in total10k-visible at 1280×960; body
  vertices copied 129k → 18k and 118k → 50k) costs a state prime + flush per
  draw: draw +64.7% / +45.0%, GPU timeline +109% / +91%, body submit +118% /
  +164% (all material). The 512-vertex threshold stays; moving small commands
  needs workstream N (one prime for a run of native commands).
- **H, not done: GPU-side placement** (local positions in the arena, a
  per-range transform). It would let a pan re-use the arena, but needs one
  draw (or one matrix change) per unit and the GPU's arithmetic is not D's
  CPU arithmetic bit for bit — the brief's "no tiny-draw explosion" and the
  pixel-identity requirement both rule it out here.

- **G, rejected by its own verifier: deferring bodies after pose changes.** The
  first G build deferred any held body whose local key kept the pieces'
  visibility flags (assuming the body's vertex count depends only on hidden
  pieces). `TAK_DISTANT_VERIFY` failed on the first deferred unit ("vertex
  count differs from the pending body"): the body collector culls back faces
  against the view direction, so rotations change the count — and the count
  decides the ≥ 96-vertex eligibility. The rule now allows only appearance
  changes (`bodyVertexCountStable`); pose changes generate the body as before.
- **G, investigation only (uncommitted): baking distant images on the screen
  pixel grid** (`~/tak-tmp/render-opt/g/experiment-pixel-align.patch`,
  binary `takclient-G1-align-experiment`). Brings the image path within 44 /
  3,712 pixels per frame of live geometry (from 87,101 / 253,148; Experiment 9)
  but changes today's appearance; not adopted. Evidence for a future
  larger-image design, not a change G could make.
- **G, kept but rarely applicable: same-position migration.** Exact by
  construction (the image keeps its tile position, so pixels and texture
  coordinates are unchanged; 1:1 copy, blending off), bounded (≤ 256 images,
  all or none, one page per sweep), exercised by `distant_models` (76 images,
  identical pixels). In the matrix it never fires: after a view change the
  survivors' tile positions overlap on the lower pages. Moving an image to
  another POSITION would make the composite's texture-coordinate interpolation
  and every later bake position differ from the pre-G cache, so it was not
  done; releasing pages that empty does the reclamation in practice.

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
- **F, defect found by the verifier and fixed (`ce2a3466`): page churn in
  the packing-failure band.** Up to F6 a full repack created GPU pages as it
  went; in a frame that fits four pages by area but not by packing it
  created pages up to four, failed and released every cell, so 2 s later the
  empty-page rule destroyed the pages and the next repack created them again
  (stationary-mixed, `TAK_SHADOW_PAGE_MAX=720`: ~16 page creations and ~12
  releases in 400 frames, every frame falling back). Fixed by placing on
  logical pages and creating textures only after the frame fits
  (Experiment 8b). The matrix scenes never entered that band, so Experiment
  8 did not see it.
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

