# Performance follow-up (2026-10-30)

Branch `perf-followup`, based on main `61f294ac` (the audit commit; main had not
moved when work started). Exact performance pass: gameplay, state hashes, AI
command streams, RNG use and rendering output must not change (one shadow-atlas
fidelity question is handled separately, see K).

## Rules for this work

- Timing runs only on cores 0-3 through `/home/pocket_geek/tak-tmp/tools/timing.sh`
  (flock); builds, tests and sweeps on 4-23. Optimized Debug builds (`-O2 -g`) for
  harness timing, as `docs/development.md` describes.
- Screening numbers taken during development are marked *screening*. Final claims
  come from the serial measurement phase on the integrated branch (section
  "Final measurements").
- Identity: `tools/legion_identity.sh`, the 7 `--mpai` gates, `tools/check-determinism.sh`
  and the scenario baseline must match the base exactly.

Base identity values (0.9.12, Inner Circle, seed 1, `TAK_SPEED=80`): Retail 60
`8690eaee7b16b4e6`, Retail 300 `d028e465c7676e68`, Legion 60 `b558575535124afc`,
Legion 300 `eab501625cd4381a`, Retail AI 60 `12003fba552b8696`, Crusades Retail
300 `def92d0b7d4a798e`, Crusades Legion 300 `fb33fb2edffd997e`; determinism golden
`dcef618cd2e4d558`.

Hardware: Intel Core Ultra 9 275HX (8 P-cores 0-7 at up to 5.3-5.4 GHz, 16
E-cores 8-23), 125 GB RAM, RTX 5070 8 GB, Fedora 44 (Linux 7.2).

## Plan

1. Measurement changes A (complete-tick timing in `simperf`) and B (interactive
   rendering benchmark cases), each on its own branch.
2. Workstreams A-L, each in its own worktree and branch off `perf-followup`, with
   focused correctness checks and screening measurements, then an adversarial
   review.
3. Integration into `perf-followup` in a fixed order, full identity and test gates.
4. Serial final measurements on a quiet machine, base `61f294ac` vs the branch.
5. Dispositions; any change that does not pay is reverted with its evidence kept here.

## Integration (2026-10-30)

Every item branch was reviewed adversarially before integration (verdicts: sound for
mA, D, E, F, G, I; fixed-up for mB, A, B, C, H, J, K, L, each fix a commit on the item
branch). None was unsound, so all fourteen are merged, one `--no-ff` merge per branch
(revert one item with `git revert -m 1 <merge>`), in the order measurement changes,
sim/AI, Legion, client:

| Item | Branch head | Merge |
|---|---|---|
| Measurement A | `pf-mA` `ecd3efeb` | `798ff29c` |
| Measurement B | `pf-mB` `ae7e86f0` | `fde659c6` |
| A Feature burning/fading | `pf-A` `38b303fb` | `96929ff8` |
| B AI resurrection | `pf-B` `f08ddb90` | `226f35b6` |
| D Exploration summaries | `pf-D` `9d1e9636` | `9f7b7fc4` |
| G View-count deltas | `pf-G` `233f08b4` | `ed366eeb` |
| L Auras | `pf-L` `ec1bc4d6` | `282119f9` |
| H Legion static refresh | `pf-H` `d0d11901` | `af56abb3` |
| J Legion clustering | `pf-J` `c09ee993` | `528998d2` |
| I Legion route invalidation | `pf-I` `7640cf8c` | `3a24bd76` |
| C Cursor picking | `pf-C` `2ab7f109` | `14dc2693` |
| E Build ghosts | `pf-E` `f95a6155` | `32992a18` |
| F Hidden-unit Fade | `pf-F` `a4150a21` | `4c2d0e15` |
| K Shadow atlas | `pf-K` `192f7c4b` | `6916a850` |

Not merged: `pf-B-base` (measurement-only base for B's counters) and `pf-H-base`
(H's bench on the base).

**Conflicts and how they were resolved** (both changes kept in every case):

- `CMakeLists.txt`, `tools/ctest_costs.txt`, `tools/legion_world_test.cpp` case list,
  `legion.cpp` includes: list unions.
- **C over measurement B** (`gameview_hud.cpp`): the cursor classification keeps B's
  `t_cursor` scope and `cursor_evals` counter and C's debug study timer and
  `TAK_PICK_INDEX_VERIFY` re-evaluation (outside the timed scope); `unitUnderCursor`
  counts B's `cursor_pick_tests` and C's debug pick tests.
- **E over B** (`gameview_render.cpp`, `rendertelemetry.h`): both named the ghost-loop
  phase; the one scope now feeds both `t_ghosts` (B) and `t_build_ghosts` (E).
  B's `ghost_preps` counts ghosts that found a model and reached preparation (now
  mostly retained geometry; `ghost_collects` counts real collections).
- **F over B** (`gameview_hidden.cpp`): F's `CoverMarks` code, with B's
  `hidden_cover_tests` (sprite/triangle tests run) counted inside F's `reaches()` and not
  in the `TAK_HIDDEN_VERIFY` reference pass. F's own "exact tests run" counter, which
  collided by name, is now `hidden_cover_reaches`; `hidden_cover_visits` unchanged.
- **K over B**: both defined the `shadow_demand_px` gauge; one counter, K's sum (B's
  telemetry-only duplicate removed).
- **Integration commits** (not in any item branch):
  - `d92ba9d9`, `600a5f52`: E's four `ghost-*` scenes and K's seven scenes had been added
    before `HISTORICAL = list(SCENES)`, so `--scenes all` grew past the 24 historical
    scenes (B's test checks it), and K's `shadow-cap-below/-above` collided with B's
    interactive scenes of the same names (B's silently won). They are now
    `GHOST_SCENES` and `SHADOW_PAGE_SCENES`, merged after `HISTORICAL`; K's scenes are
    renamed `shadow-pages-{below,moving,above,dense,pan,osc,constrained}`, definitions
    unchanged.
  - `8e45b03a`: J's `clusterchurn-*.scn` moved to `tools/scenarios/perf/` so
    `tools/scenarios/*.scn` stays the 145 baselined gate fixtures; a bench comment.
  - `fccacc61`: B's and E's render-study knobs both declared `ghostBuilders` /
    `ghostOrders` in `GameView::testBuild` (a compile error only on the combined
    branch); E's are `queueBuilders`/`queueOrders`/`queueTypes`.
- **Shared state between items, re-checked on the combined code:** D and G keep separate
  footprint caches (`Unit::sightFootprint` for exploration, `Unit::viewSight` for view
  counts) and separate derived planes; neither reads the other's cache. H's
  `StructureScope` wraps the pending refresh that calls J's `refreshPlane`; I's
  `staticChanged` runs after H's refresh loop in `syncStatic`, and the pending refresh
  passes no touch list (I's refresh-on-other-ground case). C, E, F and K touch disjoint
  caches (pick bounds, ghost geometry, cover marks, atlas pages); the shared telemetry
  enum holds 235 of 255 counters (every name present).

**Builds** (cores 4-23): `~/tak-tmp/build-pfint-o2` (Debug `-O2 -g`, all 1045 targets)
and `~/tak-tmp/build-pfint-rel` (Release, all targets), both
`TAK_TEST_DATA=/home/pocket_geek/TAK/assets/game`; base `~/tak-tmp/build-pfint-base-o2`
(`461e9c4b`, same flags; the shared `build-pf-base-o2` has no `TAK_TEST_DATA`). The
Release `takclient` and `takserver` contain none of the new debug knob strings
(`TAK_GHOST*`, `TAK_PICK_*`, `TAK_HIDDEN_VERIFY`, `TAK_AI_REVIVE_STATS`,
`TAK_RENDER_MOUSE/GHOST*/CURSOR*/SHADOWS_OFF_AT/GPU_CAP_AT`, `TAK_LCLUSTER`,
`TAK_SHADOW_PAGES_MAX`); no new warnings in the item files.

**Gates on the combined branch vs base `461e9c4b`:**

- `tools/check-detmath.sh`: OK. `tools/check-determinism.sh`: golden
  `dcef618cd2e4d558` on g++ and clang++ at -O0/-O2/-O3 (aarch64 legs skipped, no
  target headers).
- **`--mpai` gates**, Inner Circle, seed 1, `TAK_SPEED=80`, `--data ~/tak_data`, each
  twice: all fourteen runs equal the base values: Retail 60 `8690eaee7b16b4e6`, Retail
  300 `d028e465c7676e68`, Legion 60 `b558575535124afc`, Legion 300 `eab501625cd4381a`,
  Retail AI 60 `12003fba552b8696`, Crusades Retail 300 `def92d0b7d4a798e`, Crusades
  Legion 300 `fb33fb2edffd997e`, `err=none` in every run.
- **Full ctest** (821 tests, `-j 12`): 815 passed on the first run. Four `mapgen_*`
  tests failed because the run's scratch environment set an unusable `TMPDIR`; they
  pass on a rerun with it set, on both builds; `streaming_pacing` (frames dropped under load) passes on
  a rerun; `vashim_unavailable` fails the same way on the base build (the known local
  failure); `vashim_system_loader` skipped.
- **Scenario baseline** (nightly stage 2: all 145 `tools/scenarios/*.scn` plus the
  motion-wall wanderers probe, `--mode both --json --offsets gate --data`, one file per
  core on 4-23): 146 files exited 0; `legion_check.py check` against the unchanged
  `tools/scenarios/baseline.json` PASS (36,571 keys, 0 failed, 4,410 ratchets, 4,673
  licensed, 267 floor exceptions, 864 report-only), identical with `--cumulative`;
  `validate` PASS (0 problems). Every `hash@` key equals the baseline.
- **Campaign smoke** (takserver `--local --no-auth --seed 1`, headless client
  `--mpmission`, `TAK_SPEED=80`, base and combined side by side): takx04_ph 120 s
  `09c1a8465de44dd1` (defeat, as in 0.9.12), takx07_ph 120 s `d65dcd362c9a17f8`,
  takmission23_ph 120 s `97302177f370ac1d`, takx26_dh 60 s `aed7df75288713a7`: identical
  on both builds and equal to the hashes 0.9.12 recorded; no crash, desync, mismatch or
  referee-suspect line.
- **`tools/legion_identity.sh` FULL** (`--cores 4-23 -j 20`, replays L-bench, L-2h,
  R-bench, R-2h, `--data ~/tak_data`, own cache dir): 959 rows SAME, 4 "FAIL" that are the
  four new tests absent from the base (`legion_cluster_test`, `legion_routebins_test`,
  `legion_routeindex`, `legion_staticindex`; all pass on the candidate). SAME: the four
  replays to tick 18,001-18,004 (finals L-bench `46fa59babe251621`, L-2h
  `f1e8b9b0b04c44c3`, R-bench `dc333e8465471bbf`, R-2h `930412817c558085`, every
  checkpoint's section hashes, first checkpoint vs recording), both navigation goldens
  (24 checkpoints each), `--mpai` Legion and Retail 300 s twice per build
  (`eab501625cd4381a`, `d028e465c7676e68`, reproducible), determinism golden, all 504
  crowdbench matrix rows per build (both modes, seeds 0-2, serial and workers), and the
  436 shared ctest rows (pass/fail and every printed hash). Wall 5,170 s.

Scripts and logs: `~/tak-tmp/pfint/` (`gates.sh`, `campaign.sh`, `scenarios.sh`,
`gates-run{1,2}.txt`, `campaign-{base,cand}.txt`, `scen/`, `ctest-full.{log,xml}`,
`identity/`, `determinism.txt`).

## Disposition table (provisional)

Every item passed the identity gates on the combined branch (see Integration). The
performance part of each disposition is provisional until the serial measurement
phase; an item that does not show a repeatable gain there is "Measured and rejected"
and its PROD commits are reverted, evidence kept here.

| Item | Disposition (provisional) | Evidence |
|---|---|---|
| Measurement A: complete-tick timing | Implemented and verified (instrumentation) | `cli_tools` sum check; core p99 ~0.9 ms vs complete ~10.7 ms on Ultima Online B1 (screening) |
| Measurement B: interactive render cases | Implemented and verified (instrumentation) | 14 scenes refuse a client without the knobs; historical 24 unchanged; added cost ~0.001 ms (screening) |
| A Feature burning/fading | Exact; gain pending serial phase | differential `featgen`/`reclaim` tests; ~1 us/tick (Ulasem), ~10 us at 11.6k features (screening) |
| B AI resurrection corpses | Exact; gain pending serial phase | 400-scene reference test incl. apply-at-once; per call 0.014-0.39x of base (screening); no idle caster in gate games |
| C Cursor picking | Exact (after review fix `f9967026`); gain pending serial phase | `render_pick_index*`, `pick_late_model`; cursor eval 1.11 -> 0.031 ms at 10k units (screening) |
| D Exploration block summaries | Exact; gain pending serial phase | `exploration_parallel` kept-vs-rebuilt, `TAK_VERIFY_LISTS`; 4.7 instructions per plane cell per tick removed |
| E Build-queue ghosts | Exact (pixel-identical to the reference path); gain pending serial phase | `render_build_ghosts` bit-exact verify over every model; ghost-queues 5-6 -> 0.8 ms frame (screening) |
| F Hidden-unit Fade covers | Exact; gain pending serial phase | `hidden_units_policy`, in-game `TAK_HIDDEN_VERIFY`; pass -40..-50% (screening) |
| G View-count deltas | Exact; gain pending serial phase | delta vs rebuild tests and 3M-case fuzz; per move 0.25-0.42x; whole tick only on the march workload (screening) |
| H Legion static refresh | Exact; gain pending serial phase (base-heavy worlds only) | `legion_staticindex`; sync 0.35-0.54x at 2000-3000 structures; real games below the 64-structure gate |
| I Legion route invalidation | Exact; **conditional**: keep only on a repeatable whole-tick gain on L-routeinv-local-10000/5000 with no regression on L-2h/L-bench, else Measured and rejected (revert `e342a202`, `63a2a0e0`, `9dcbe1f8`) | syncStatic -55..-75%, tick -0.6..-0.9% (below resolution) (screening) |
| J Legion dirty-rect clustering | Exact; worst-case guard, gain pending serial phase | clustering 7x (960 rects) to 70-110x (4096); clusterchurn-big tick 0.954 (screening); recorded games <= 7 rects |
| K Shadow atlas capacity | `c3c312eb`, `0397c088`, `192f7c4b`: no visual change at 4 pages. **`98e01695` (4 -> 16 pages) is a visual change awaiting the owner's acceptance**; gain pending serial phase | dense uhd views -39..-44% frame, near capacity -13..-23% (screening); captures documented under K |
| L Auras | Exact; gain pending serial phase | `aura_recipient` serial/1/3/7 workers; pass -27..-69% (screening); whole tick below resolution |

## Items (design, screening, identity)

All timing below is *screening*: `timing.sh` on cores 0-3, short interleaved or
swapped A/B pairs, taken while up to 14 agents loaded the machine (load average
30-150). The reviewers re-derived or re-ran the key numbers; no figure here is a
final claim. "Identity" lists what each branch showed before integration; the
combined branch is checked again in "Integration" above.

### Measurement A: complete-tick timing in `simperf`

- **Change** (`pf-mA`, instrumentation only; `tools/simperf.cpp`, `cli_tools_test.py`,
  one clause in `docs/development.md`). At `61f294ac` the tick sample was pushed
  before the periodic `stateHash()`, so `wall_seconds` included hashing and the
  percentiles did not. The core sample (`ms=`, `median_ms`, `p95_ms`, `p99_ms`,
  `max_ms`) is unchanged; the due hash is now timed right after it (before the
  read-only diagnostic scan), and a complete sample = core + that tick's hash is
  added. New fields are appended only: per-second `hash_tick_ms`, `complete_ms`; a
  last line with `core_mean_ms`, `core_over_budget`, `complete_{mean,median,p95,p99,max}_ms`,
  `complete_over_budget`, `hash_ticks`, `service_seconds`, `ai_total_ms`,
  `world_total_ms`, `hash_total_ms`. No script parses the old output.
- **Test:** `cli_tools` checks complete = core + hash per tick and in totals; a build
  leaving the hash out fails it.
- **Screening:** Ultima Online B1, 8 units, 900 ticks: core p99 about 0.9 ms,
  complete p99 about 10.7 ms (reviewer, n = 3 pairs); about 10 ms per hash on a large
  map. Generated map, 2000 units: p99 34.8 -> 40.7 ms core -> complete. Checkpoint
  hashes identical base vs candidate.
- **Use:** quote `complete_*` and `service_seconds` for whole-service claims.

### Measurement B: interactive render cases

- **Change** (`pf-mB`, instrumentation only): telemetry phases `t_cursor`, `t_ghosts`
  and counters `cursor_evals`, `cursor_pick_tests`, `cursor_pick_hits`,
  `ghosts_queued`, `ghost_preps`, `hidden_cover_tests`, `shadow_demand_px` (debug
  telemetry, compiled out under NDEBUG); debug render-study knobs `TAK_RENDER_MOUSE`,
  `_SELECT`, `_ARM`, `_GHOSTS`, `_COVER`, `_FADE_CLOCK`; `render_benchmark.py --scenes
  interactive` with 14 scenes, each checked against what it must show (a client
  without the knobs is refused). `--scenes all` still means the 24 historical scenes.
  ctest `render_interactive_cases`.
- **Gap at `61f294ac`:** the in-game Benchmark returns before classifying the cursor
  (`gameview_hud.cpp:317`), `mouseX_` starts at -1 so the matrix never classifies it,
  nothing covers selections, long queues or units behind buildings, and the shadow
  tile gauge reads 0 on a fallback frame (`gameview_render.cpp:1443`).
- **Scenes:** hover-ground-10k, hover-units-10k, hover-armed-10k, select-large (C);
  ghosts-queue, ghosts-queue-pan (E); fade-forest, fade-buildings, dots-buildings (F);
  shadow-cap-below, -above, -cross, -below-pan, -above-pan (K, 7680x2160 only).
- **Screening:** added cost in matrix scenes `t_cursor` about 0.001 ms, `t_ghosts`
  0.0002 ms; matrix A/B within the 17-28% A/A spread. Captures that differ even A/A
  (judge against an A/A pair): select-large and moving (distant-image bakes on wall
  time), shadow-cap-cross (atlas page timers on wall time), fade-buildings (distant
  images; identical with `TAK_DISTANT_MODELS_OFF=1`).

### A. Feature burning and fading

- **Opportunity at `61f294ac`:** `World::tickBurning` (`sim.cpp:10172`) visits every
  feature every tick; `tickFeatureFades` (`:10546`) every feature on any tick with a fade.
- **Design** (`pf-A`, `168e80bb`): two bit vectors over feature indices
  (`burningMarks_`, `fadingMarks_`) plus a count of set burning bits; derived, never
  hashed, a superset of the active features. `igniteFeature` (the only route into
  "alive and burning") and `finishReclaim` (the only place `fadeLeft` becomes non-zero)
  set them; `addFeature` grows them; `clearFeatures` / `resetForReplay` clear them.
  The walk visits set bits in ascending index order and re-reads the current word
  after every visit, so a spark into a higher index burns the same tick and one into
  a lower index waits a tick, exactly as the index loop (no tick-start snapshot). The
  feature's own `alive`/`burn`/`fadeLeft` still decide; stale bits are dropped. No
  fire: no visit at all.
- **Tests:** `featgen_test` runs production against the old full passes kept verbatim
  (every field, every RNG draw, `fadingFeatures_`, `stateHash` after every step, 4
  seeds x 500 steps, plus directed same-tick spread cases and, from the review, growth
  mid-fire and world reuse through `clearFeatures` and `resetForReplay`); passes on
  base too. Broken variants (tick-start snapshot, single word read, no mark in
  `igniteFeature` / `finishReclaim`, count not reset) fail.
- **Screening** (`featburn_bench`, core 2, 3 swapped rounds x 2000 ticks, ns per tick,
  routine = the two passes alone): Ulasem Arena (1250 features) no fire 1040 -> 15,
  sparse fire 1055 -> 91, fire 1376 -> 330, fade 1655 -> 188; `--gen 1024` (11,595
  features) no fire 9886 -> 18, sparse 9991 -> 215, fire 15526 -> 3676, fade 17728 ->
  623. About 1 us per tick on Ulasem and 10 us at 11.6k features; a counter-only fast
  path loses as soon as anything burns (Ulasem sparse 1580 vs 97 ns). Bench hashes
  equal on all 16 workloads. Real games rarely burn (0 ignitions in the 300 s gates).

### B. AI resurrection (`Controller::reviveIdle`)

- **Opportunity at `61f294ac`:** `ai.cpp:3358`: every eligible idle caster scanned
  every unit record for a body within 56 px, and `idle()` (an order walk) ran before
  the cheap `canResurrect`/`canAnimate` test.
- **Design** (`pf-B`, `064219a4`, `639c89f1`): the raisable bodies are gathered once
  per think, lazily at the first eligible caster, in `world.units()` order with the
  old filter; none -> return. Same float distance expression and tie test in record
  order; the claim is a flag on the gathered body (ids are unique); `reviveKind`,
  `atTypeCap` and the claim keep their order. Caster flags are tested before
  `idle()` (all pure reads). Debug-only `TAK_AI_REVIVE_STATS` (made thread-safe in
  review, `f08ddb90`).
- **Tests:** `ai_behavior_test` compares with a verbatim copy of the 0.9.12 search
  over 400 seeded scenes (ties, competing casters, dying/fading/expired bodies, type
  caps, busy/frozen/enemy/pre-assigned casters, low allocation, unit cap), also with a
  sink that applies each command at once; commands, order, `assigned` and the state
  hash must match. Broken tie, claim and dying-filter variants fail.
- **Screening** (`ai_revive_bench`, pure base `bdbdae58`, 3 swapped rounds x 500
  calls, us per call, mean): zero bodies 138 -> 1.9, 16 bodies 157 -> 7.4, 300 near
  223 -> 20, 2000 near 562 -> 120, 2000 anywhere 528 -> 94, 4 casters/200 bodies 21.4
  -> 8.4. In simperf (4000 units) and in the gate games there are no eligible idle
  casters; `reviveIdle` per call drops about 2x from the flag reorder alone.
  Spatial buckets: exact but slower for realistic caster counts, not committed.

### C. Cursor picking

- **Opportunity at `61f294ac`:** full-roster loops in `desiredCursor`
  (`gameview_hud.cpp:355`, loops :419, :451), `hoverCursor` (:492, loops :508, :588),
  `reclaimTargetAt` (:81) and `raiseTargetAt` (`gameview_impl.cpp:297`).
- **Design** (`pf-C`, `a18ef286`, review fix `f9967026`): `pickCandidates(mx,my)`
  queries the snapshot `UnitGrid` with a conservative band (union of every type's hit
  box incl. the 9 px floor, Tactical Dots and HIDDEN UNITS dots, `Frame::maxInterpStep`,
  waterline sink, terrain lift, a flyer altitude envelope for a second flyer query)
  and returns ascending live indices; every loop keeps its exact test, filters and
  order. `bodyCandidates` does the same for Revive/Reclaim body picks. The grid is
  used only once every typed live record of the snapshot is registered: base caches
  `kFallbackHitBox` for a type whose model is not loaded yet when its full scan
  touches such a unit, so skipping it would change later hits (found in review,
  reproduced by `pick_late_model`). Debug knobs `TAK_PICK_FULL_SCAN`,
  `TAK_PICK_INDEX_VERIFY`.
- **Tests:** `unit_index_test` pick property test (flip points of each edge +- ulps;
  broken bands fail); `render_pick_index`, `_orders`, `_jump` under verify;
  `pick_late_model` (fails at `97e43e1a`, passes with the fix).
- **Screening** (`cursor_pick_study.py`, 1280x960, one ABBA group, 600 frames per
  side): 10,000 units small view, 20 selected: cursor evaluation median 1.110 ->
  0.031 ms (10,002 -> 56 exact tests per frame); 2,000-unit battle with flyers 0.229
  -> 0.041 ms; 200 units 0.0115 -> 0.008 ms. Whole frame not established (A's runs
  43 vs 21 ms). World hashes and per-frame cursor sequences identical A/B.

### D. Exploration block summaries

- **Opportunity at `61f294ac`:** `World::updateExplorationPlane` rebuilt
  `explorationBlocks_` from the whole plane every pass (`sim.cpp:13139-13141`).
- **Design** (`pf-D`, `ddbaf209`, `d7ae052a`): every writer keeps the per-block AND
  summaries: the reveal pass per changed block, power codes by the same bit operation
  (AND distributes over `|bit` and `&~bit`), `setTerrain` / `resetForReplay` /
  the new `editNavigationExploration()` (now the only harness write path; it replaced
  8 const_casts and the friend writes) empty them, and a pass rebuilds only empty or
  wrong-sized summaries. Workers publish with atomic OR (never an overestimate); the
  first summarizer of a block lists it once and the listed blocks are recomputed
  exactly after the join. Not hashed; `TAK_VERIFY_LISTS` compares with a rebuild at
  the start and end of every pass.
- **Tests:** `exploration_parallel` `checkKeptBlocks` (kept vs rebuilt worlds, serial
  and 3 workers, partial edge blocks, alliance change, Mapping on/off, NowISee,
  removing harness writes, `setTerrain`, `resetForReplay` with a new size). Broken
  variants V1-V4, V6 fail; the reviewer's 9,000-unit stress harness at 15 workers
  matched base serial every tick.
- **Screening:** the removed work is 4.6-4.7 instructions per plane cell per tick
  (TAK_PMU); idle explore phase on a 1M-cell plane 4.93M -> 7.4K instructions. Map-sized
  cost (`wall.sh`, us per tick base -> cand): 16K cells 10.3 -> 6.0, Ulasem 102K 40.0
  -> 10.7, 262K 90 -> 29, 1M 379 -> 103. Pooled explore phase -13..-16% on L-2h/R-2h;
  whole tick on shipped maps within the +-2-3% pair spread.

### E. Build-queue ghosts

- **Opportunity at `61f294ac`:** `drawGhostAt` (`gameview_render.cpp:7696`) collected
  and stable-sorted every ghost's model every frame, off-screen ones included.
- **Design** (`pf-E`, `66f94b1a`): per model, the collected and depth-sorted
  model-local triangles and bounds are retained (keys: renderer and asset generation,
  atlas texture pointer, `atlasW_`/`atlasH_`; cleared by `invalidateRenderTargets()`
  and above 256 entries). An entry textured from outside the colour atlas keeps only
  its bounds (re-collected per ghost). Placement and tint arithmetic run unchanged on
  a copy. A ghost whose placed bounds miss the clip rect by more than 1 px is skipped
  (`atlasFor()` still runs first). Debug `TAK_GHOST_REFERENCE`, `TAK_GHOST_VERIFY`,
  `TAK_GHOST_ALL_TYPES`.
- **Tests:** `render_build_ghosts` (software renderer, every registry type ghosted
  on, across and off the view, half red-washed, two render-target resets) requires the
  retained triangles to equal a fresh collection bit for bit and every culled ghost's
  reference bounds outside the clip; `collected>0 inexact>0`. Pixel identity: GL
  captures of the reference path vs the candidate identical apart from A/A-noisy frames
  (terrain not ready).
- **Screening** (1280x960, n = 2 runs per label, 300 frames): ghost-queues frame mean
  4.97/6.25 -> 0.80/0.89 ms (`t_build_ghosts` 3.9-4.7 -> 0.27-0.29); ghost-offscreen
  3.69/4.92 -> 0.30/0.29; ghost-visible 6.41/7.08 -> 2.21/2.53; ghost-few (200 units,
  27 ghosts) 3.22/3.13 -> 2.33/2.42. Retained memory 25,600 bytes.

### F. Hidden-unit Fade covers

- **Opportunity at `61f294ac`:** `gameview_hidden.cpp:141`: every eligible unit ran the
  full sprite or triangle test on every nearby cover, also on covers an earlier unit
  had already faded this frame, and called `hiddenFades_.cover(key)` again each time.
- **Design** (`pf-F`, `a17c27ab`): `hu::CoverMarks`, one byte per CoverGrid id per
  frame. A cover not yet found gets the exact test; on a hit it is marked and faded
  once. A found cover is skipped once the unit is known covered; otherwise deferred and
  retested in order only if no new cover hit the unit, so per-unit candidates (and
  telemetry) stay exact. DOTS is the same predicate. Debug `TAK_HIDDEN_VERIFY`.
- **Tests:** `hidden_units_policy` (300 random frames against testing every cover,
  fixed cases; mutants M1-M5 fail); the in-game `hidden_units` harness with verify;
  captures base vs candidate 0 px for forest-1200 and keeps.
- **Screening** (`t_hidden_units`, two ABBA rounds, n = 6 per build): fade-forest
  0.130-0.152 -> 0.073-0.087 ms (1057 -> 365 tests per frame); fade-forest1200
  0.738-0.898 -> 0.355-0.384 ms (8124 -> 1238); fade-keeps 0.463-0.561 -> 0.348-0.480
  ms (2919 -> 1575, overlapping). Whole frame within noise.

### G. View-count deltas

- **Opportunity at `61f294ac`:** `updateViewCounts` (`sim.cpp:13278`) ->
  `retailUpdateSight` (`retailexploration.h:125-135`) removes every admitted cell and
  re-adds every admitted cell. Audit probe reproduced (`viewcount_delta_test probe`):
  one-cell move 2,514 -> 82 counter updates (82 cells change); eye 100 -> 106 at the
  same cell 2,514 -> 0.
- **Design** (`pf-G`, `6eacf5f9`, new `src/sim/retailviewcounts.h`): same trigger and
  cached footprint fields; an active footprint whose new centre is on the map moves by
  `retailSightMove`: rows of both footprints walked together, each split into only-old
  (-1), both (untouched), only-new (+1), with exact per-row half-widths (integer sqrt,
  then corrected row by row) and per-eye-height admission tables built from the same
  double products as `retailSightAdmits`. Every other path (new footprints, leaving the
  map, ownership/distance/sight-height changes, construction and status locks, deaths
  and revealers) keeps the unchanged full traversal. Counts are modular sums, so the
  final plane is identical; workers keep relaxed atomic adds.
- **Tests:** `viewcount_delta_probe`, `_moves` (row spans, isqrt, limit tables
  exhaustively over 26.0M admissions, 57,600 random footprint updates), `_world` (four
  4,200-unit worlds, delta vs rebuild, serial and workers, equal counts and hash every
  tick). The reviewer's independent fuzz: 3M cases, 0 mismatches.
- **Screening:** per move (replayed real moves, ns, base -> cand): R-2h 7065 -> 2945,
  L-2h 4470 -> 1384, march 4936 -> 1723 (atomic 8822 -> 2223); counter updates 13-38x
  fewer. Explore phase -37..-58% on the march workload (233 moves per tick), -6..-12%
  on L-2h/R-2h/R-bench; whole tick -3..-5% on the march (pair ranges reaching zero),
  unchanged within noise on the WE situations.

### H. Legion static refresh

- **Opportunity at `61f294ac`:** `computeCells` (`legion.cpp:2716`, :2756-2760) walks
  every structure per call; `refreshCluster` calls it per rectangle per plane; a
  background build's pending refresh (:6297) and `computeRows`' row chunks too.
- **Design** (`pf-H`, `9cb77291`, `2eae31d7`): an immutable `StructureIndex` (16-cell
  buckets by clamped footprint origin, list positions ascending) built once per
  operation when there are 64+ structures and enough rectangle reads; a query reads the
  buckets an overlapping footprint could start in and stamps the candidates in list
  order with the unchanged `stampStructure`. `ListScope` / `StructureScope` bound its
  lifetime; workers only read it. `TAK_LEGION_VERIFY` compares every indexed call with
  the full walk.
- **Tests:** `legion_staticindex` (scan vs index, serial vs workers, plain and odd-sized
  maps with footprints larger than a bucket; every plane equal to a full-scan rebuild).
  A query clipped to 6 cells passed the developer's case and fails the reviewer's.
- **Screening** (`legion_staticsync_bench`, reviewer's serial interleaved run, n = 10,
  sync ms): 3000 structures mean 0.515 -> 0.275 (paired ratio 0.535); 2000 structures,
  12 changes, 8 planes 1.183 -> 0.414 (0.347); 50 structures (below the gate) 1.010.
  No real game reaches 64 structures, so ordinary games are unchanged.

### I. Legion route invalidation

- **Opportunity at `61f294ac`:** `staticChanged` (`legion.cpp:6131`, loop :6155) tests
  every group's field and next against every change box.
- **Design** (`pf-I`, `e342a202`, `63a2a0e0`, `9dcbe1f8`, new
  `src/sim/legion_routebins.h`): per plane, 32x32-cell bins list the groups whose field
  or next (grown by one) touches them; maintained in `listGroup` / `unlistGroup`, keyed
  by unique Field serials, lazily compacted. `staticChanged` gathers, dedupes and sorts
  candidates by id and runs the unchanged per-group body (`reachGroup`, always
  inlined) on them; boxless calls and whole-map boxes keep the full walk.
  `groupLoopIters` now counts groups visited (falls; one-sided keys). Not hashed;
  `TAK_LEGION_VERIFY` checks bins and candidates (read-only `collect()`).
- **Tests:** `legion_routebins_test` (property test; 6 of 7 broken variants fail, the
  7th is still correct); `legion_routeindex` (lanes and refresh parts). The reviewer's
  sweep: all scenario fixtures, index on vs off vs base, identical hashes and decision
  digests, verify clean; the Legion 300 gates under verify unchanged.
- **Screening:** syncStatic -55..-75% on L-routeinv-local-10000/5000; nav-phase
  instructions -0.65..-2.1%, whole tick -0.2..-0.45% instructions; tick time -0.6..-0.9%
  (below resolution). L-2h / L-bench: staticChanged about 0.01% of the tick,
  instructions +0.01..0.02%. Conditional: keep only if the serial phase shows a
  repeatable whole-tick gain on local-10000/5000 and no regression on L-2h/L-bench;
  otherwise remove `e342a202`, `63a2a0e0`, `9dcbe1f8`.

### J. Legion dirty-rectangle clustering

- **Opportunity at `61f294ac`:** `refreshPlane` (`legion.cpp:2856`) tests every pair
  (:2862) and rescans the list per cluster (:2871-2875).
- **Design** (`pf-J`, `d5b15d47`, `e50c0b0c`, new `src/sim/legion_cluster.h`): above 64
  rectangles, grown boxes go into 8x8-cell bins with pairwise-disjoint 64-bit masks
  (exact shared-cell test, unchanged grow margin); odd boxes fall back to the pairwise
  test. A lowest-root union-find plus a counting sort gives clusters in order of their
  first rectangle with index order inside, exactly the old lists. Debug-only
  `TAK_LCLUSTER` line (moved off `TAK_LPROBE` in review so LPROBE timings keep their
  meaning).
- **Tests:** `legion_cluster_test` (3000 random lists, limits, forced bin path; five
  broken variants fail); reviewer's independent BFS fuzz, 320,000 cases under ASan and
  UBSan, 0 failures; clusterchurn scenarios `--mode both` JSON byte-identical to base.
- **Screening:** clustering alone 1024 rectangles 1860-2740 -> 97-126 us, 4096
  rectangles 31-44 ms -> 0.4 ms; clusterchurn-big (960 rectangles per refresh) whole
  tick swapped-pair median 0.954; edge (90 rectangles) up to +1.6 us per call, whole
  tick 0.982; recorded 10k-unit battles never exceed 7 rectangles (no gain there).

### K. Shadow atlas capacity

- **Opportunity at `61f294ac`:** `kMaxPages=4` (`gameview_render.cpp:671`), `createPage`
  refuses at four (:812), and "tile area exceeds page limit" (:913) sends the whole
  frame to direct shadows; near the limit the atlas repacks most frames.
- **Design** (`pf-K`): `c3c312eb` (no visual change, 4 pages by default) adds extended
  2048^2 pages beyond the four base pages, created only when the base pages cannot hold
  the frame or the atlas is full but not fragmented; admitted up to 3/4 of the GPU cap
  (pending pages count as held, none while the cap is tightened), shed above 7/8 with a
  10 s cooldown; emptied extended pages kept 8 s. `192f7c4b` (review) gives extended
  pages back while unit shadows are off. **`98e01695` is the visual change**:
  `kShadowMaxPages` 4 -> 16, so overflow views draw cached silhouettes instead of the
  direct-shadow fallback (about 5-6% of world pixels lighter in the dense views: the
  direct path darkened model folds twice; against base with 4096^2 pages, which holds
  the same army in its atlas, 48-2550 px differ, mostly 1 LSB). Owner's decision.
- **Tests:** `shadow_atlas` `testPageBudget` (5 policy mutations fail);
  `render_shadow_atlas_{extended,yield,full,off}`; the base atlas tests reproduce F's
  record. With pages capped at 4 the `_band` scene's 16 counters equal base.
- **Screening** (7680x2160, n = 2 per build): dense (9783 shown) frame mean 106/100 ->
  60/65 ms; above (6600) 65/61 -> 37/33; below (5800) 42/42 -> 32/33 (forced repacks
  0.415/frame -> 0); moving (5400) 79/77 -> 67/68 (fallback 45% of frames -> 0);
  pan and osc neutral (timed on an older candidate); constrained 448 MiB neutral.
- **Pre-existing:** base `461e9c4b` crashes under a 384 MiB cap at 7680x2160 in the body
  arena's native draw (`GeometrySubmit::passDrawBuffer`).

### L. Auras

- **Opportunity at `61f294ac`:** `tickAuras` (`sim.cpp:12865`): pooled gather of
  AuraHit lists (:12934), all applied serially (:12964).
- **Design** (`pf-L`, `39ca6291`): from 2048 units, `prepareAuraSources` lists every
  Armor/Attack aura of each eligible emitter once (scan square with `scanNear`'s exact
  arithmetic, `r2`, owner side mask from `allied()`), bucketed into the grid's 8x8
  blocks in source order; `applyAuraTiles` lets one chunk own whole tiles and folds each
  unit's buffs from its cell's sources in emitter-then-aura order (row AND column
  bitmask). Recipients are `rebuildGrid`'s chains with the same eligibility test, so
  units revived after the grid receive nothing until the next grid, as before. Below
  2048 units the serial walk is unchanged; `AuraHit` / `auraChunks_` are removed.
  `TAK_VERIFY_POOL` re-runs every tiled pass serially.
- **Tests:** `aura_recipient` (buff/debuff order, ineligible emitters, deaths and
  boardings after the grid, a post-grid spawn, random scenes vs a written-out serial
  walk, a 2,700-body fight with a priest raising an emitter; review: four-word tile
  lists, edge clamps, owners outside 0..63) at serial and 1/3/7 workers. The gates stay
  below 2048 units; the tiled path is covered by a full L-2h replay at workers 0 and 3
  (final `f1e8b9b0b04c44c3` on both builds) and situations under `TAK_VERIFY_POOL`.
- **Screening** (`aura_bench`, per pass, us, base -> cand, median): R-2h 3 workers 147
  -> 83 (-47%), serial 335 -> 233 (-30%); L-2h 3 workers 163 -> 81, serial 368 -> 283;
  dense 12k units / 1000 emitters 3 workers 4749 -> 1459, serial 11499 -> 4505; sparse
  12k / 200 -13..-16%. Retained scratch with workers 129,840 -> 25,660 B (R-2h); serial
  mode now keeps 25-148 KB it did not before. Whole tick in the WE recordings (expected
  0.03-0.04 ms) below screening resolution.

## Final measurements (serial phase): commands

Run on a quiet machine, every timing run through `/home/pocket_geek/tak-tmp/tools/timing.sh`
(cores 0-3), from the integrated tree `~/tak-tmp/wt-perf-int` (it has every item's
tools; J's scenarios now live in `tools/scenarios/perf/`, K's scenes are named
`shadow-pages-*`). Scratch `HOME`/`XDG_*` under `~/tak-tmp` for every client and
server run, `TMPDIR=~/tak-tmp/tmp`, and a warm-up run before each series.

Builds (all Debug `-O2 -g`, all targets, `TAK_TEST_DATA=/home/pocket_geek/TAK/assets/game`):

- `BASE=~/tak-tmp/build-pfint-base-o2`: plain `461e9c4b` (= `61f294ac` code).
- `CAND=~/tak-tmp/build-pfint-o2`: `perf-followup` at the integration head.
- Items whose workload or counters need knobs the plain base lacks use an
  instrumented base (the item's INSTR/TEST commits on `461e9c4b`, no PROD): named per
  item below. Each item's own head (`pf-<key>`) is still built in
  `~/tak-tmp/build-pf-<key>-o2` for attribution: the integrated candidate carries every
  item, so a render scene, for example, sees C, E, F and K together.

Variables used below: `T=/home/pocket_geek/tak-tmp/tools/timing.sh`,
`D=/home/pocket_geek/TAK/assets/game`, `TD=~/tak_data`, `X=$TMPDIR/pf-final`.
`tools/we_timing.sh`, `perf-explore/wall.sh`, `aura_bench_ab.sh` and
`legion_staticsync_ab.sh` take the timing lock themselves; every other timed command
is wrapped in `$T` below.

**Measurement A** (simperf complete ticks). Both sides need the new fields: build a
base `simperf` from `461e9c4b` with `pf-mA`'s `tools/simperf.cpp`, or compare only the
old fields against `$BASE/simperf`. Pair driver `/home/pocket_geek/tak-tmp/tmp/mA-runs/pair.sh`
(edit its two binary paths; add a warm-up run):

```sh
$T /home/pocket_geek/tak-tmp/tmp/mA-runs/pair.sh $X/mA-uob1 8 --mode match --units 8 --ticks 1800 --map 'Ultima Online B1' --data $TD
$T /home/pocket_geek/tak-tmp/tmp/mA-runs/pair.sh $X/mA-gen2000 4 --mode match --units 2000 --ticks 900 --data $TD
python3 /home/pocket_geek/tak-tmp/tmp/mA-runs/summ.py $X/mA-uob1
```

**Measurement B** (interactive scenes). Base = a build of `pf-mB` (`ae7e86f0`; 461e9c4b
plus instrumentation only); a plain `461e9c4b` client is refused. These scenes are also
the C/E/F/K interactive workloads:

```sh
B="python3 tools/render_benchmark.py --data $TD --cpus 0-3 --repeats 2"
$T $B --a <pf-mB o2>/takclient --b $CAND/takclient --scenes hover-ground-10k hover-units-10k hover-armed-10k select-large --sizes normal uhd --output $X/mB-c
$T $B --a <pf-mB o2>/takclient --b $CAND/takclient --scenes ghosts-queue ghosts-queue-pan --sizes normal uhd --output $X/mB-e
$T $B --a <pf-mB o2>/takclient --b $CAND/takclient --scenes fade-forest fade-buildings dots-buildings --sizes normal uhd --capture --output $X/mB-f
$T $B --a <pf-mB o2>/takclient --b $CAND/takclient --scenes shadow-cap-below shadow-cap-above shadow-cap-cross shadow-cap-below-pan shadow-cap-above-pan --sizes uhd --capture --output $X/mB-k
```

With `--cpus 0-3` every run is flagged contended: do not pass `--exclude-contended`.
Captures that differ A/A (select-large, moving, shadow-cap-cross, fade-buildings) need
an A/A pair; `--capture-only --env TAK_DISTANT_MODELS_OFF=1` for exact identity.

**A** (feature burning/fading). The base predates the bench:

```sh
tools/featburn_build_base.sh ~/tak-tmp/wt-pf-base ~/tak-tmp/build-pfint-base-o2 $X/featburn-base
$T tools/featburn_timing.sh $X/featburn-base/featburn_bench $CAND/featburn_bench $D 5 3000 2 > $X/featburn.log
tools/featburn_summary.py $X/featburn.log            # hash column must say "same"
```

Workloads are built in: `--map "Ulasem Arena"` and `--gen 1024`, `--scene
nofire|sparse|fire|fade`, `--mode routine|tick`. Identity: `featgen_test` on both builds,
the `spread ... hash` lines must match.

**B** (AI resurrection). Pure base `bdbdae58` (461e9c4b + the bench commit), built in
`~/tak-tmp/build-pf-B-pure-o2` (`ai_revive_bench` target); the script exits 1 if the
command streams differ:

```sh
$T python3 -I tools/ai_revive_bench.py --base ~/tak-tmp/build-pf-B-pure-o2 --cand $CAND --rounds 5 --calls 2000
```

Counters in a full game, base = `pf-B-base` `5fcbac9b` (`~/tak-tmp/build-pf-B-base-o2`,
same hook), swapped pairs under `$T` (driver `/home/pocket_geek/tak-tmp/scratch-pf-B/simperf-pairs.sh`):
`TAK_AI_REVIVE_STATS=1 <build>/simperf --mode match --units 4000 --ticks 1800 --data $TD`
(AIREVIVE line on stderr). Gates with counters:
`/home/pocket_geek/tak-tmp/scratch-pf-B/gates/all.sh <build-o2> <absolute outdir>`.

**C** (cursor picking). A = `cd9a9f1c` (instrumentation only, old picking):
`git -C /home/pocket_geek/TAK worktree add --detach ~/tak-tmp/wt-pf-C-A cd9a9f1c` (link
`third_party/ffmpeg-bink`, `third_party/static-deps`, `assets`), Debug `-O2 -g`, target
`takclient`.

```sh
python3 tools/cursor_pick_study.py --a ~/tak-tmp/build-pf-C-A/takclient --b $CAND/takclient --data $D --output $X/C \
  --scenes subset10k-sel subset10k-nosel subset10k-attack visible10k-sel small-battle-sel effects-pan-sel \
  --frames 900 --warmup 300 --repeats 2 --wrap $T          # then again with --width 7680 --height 2160
python3 tools/cursor_pick_study.py --a $CAND/takclient --b $CAND/takclient --a-env TAK_PICK_FULL_SCAN=1 ...   # same-binary reference
$T python3 tools/render_benchmark.py --a ~/tak-tmp/build-pf-C-A/takclient --b $CAND/takclient --data $D \
  --scenes total10k-subset effects --env TAK_RENDER_CURSOR=1 --env TAK_RENDER_CURSOR_SELECT=20 --cpus 0-3 --output $X/C-frame
```

Correctness: `ctest -R "unit_index|render_pick_index|pick_late_model"`; any study run
with `--env TAK_PICK_INDEX_VERIFY=1` (not for timing). Suggested extra: a net
(presentation-clock) game with a large arrival, to count full-scan fallbacks.

**D** (exploration summaries). Plain base:

```sh
tools/scenarios/perf-explore/wall.sh --base $BASE --cand $CAND --reps 9 --ticks 3000 --data $D --out $X/D-wall
tools/scenarios/perf-explore/run.sh --base $BASE --cand $CAND --sits explore-idle-256,explore-idle-ulasem,explore-idle-1024,explore-idle-2048,explore-move-2048 --rounds 3 --warm 100 --ticks 400 --data $D --out $X/D-serial
TAK_SIM_WORKERS=1 tools/scenarios/perf-explore/run.sh --base $BASE --cand $CAND --sits explore-reveal-2048 --rounds 3 --warm 100 --ticks 400 --data $D --out $X/D-pooled
tools/we_timing.sh --base $BASE --cand $CAND --rounds 2 --data $D --out $X/D-we && python3 tools/scenarios/perf-explore/phase_stats.py $X/D-we --warm 1000
TAK_SIM_WORKERS=1 tools/we_timing.sh --base $BASE --cand $CAND --sits R-2h-1800,L-2h-1800 --rounds 2 --data $D --out $X/D-we1w
tools/we_timing.sh --base $BASE --cand $CAND --replays <patched replays> --sits L-bench-1900,R-bench-1900,L-2h-1800 --rounds 2 --data $D --out $X/D-rep
```

Work counters: `legion_scenario` with `TAK_PHASE=1 TAK_PHASE_MS=0 TAK_PMU=1` under `$T`,
`taskset -c 2`, on `explore-idle-1024.scn`, `explore-move-2048.scn` and the unzipped
`R-2h-1800.scn` (`--mode retail --data $D --ticks 500 --offsets 0 --no-observer --workers
--catchup 60`), then `python3 tools/scenarios/perf-explore/pmu_stats.py <errs> --warm 100`.
(The run scripts call `timing.sh` themselves where documented; otherwise wrap them in `$T`.)

**E** (build ghosts). Base `d8e63f3a` (base renderer + the workload knob):
`git -C /home/pocket_geek/TAK worktree add ~/tak-tmp/wt-pf-E-base d8e63f3a`, Debug `-O2 -g`, `takclient`.

```sh
$T python3 tools/render_benchmark.py --a ~/tak-tmp/build-pf-E-base-o2/takclient --b $CAND/takclient --data $D --output $X/E \
  --scenes ghost-queues ghost-offscreen ghost-visible ghost-few --sizes normal uhd --repeats 2 --frames 900 --warmup 300 --cpus 0-3
python3 tools/render_benchmark.py --a ~/tak-tmp/build-pf-E-base-o2/takclient --b $CAND/takclient --data $D --output $X/E-cap \
  --scenes ghost-queues ghost-visible ghost-offscreen ghost-few effects --sizes normal uhd --capture-only --frames 760 --warmup 100 \
  --cpus 4-23 --env TAK_RENDER_CAPTURE_EVERY=40      # and the same with --a $CAND --b $CAND (A/A)
python3 tools/render_benchmark.py --a $CAND/takclient --b $CAND/takclient --data $D --output $X/E-all --scenes ghost-offscreen \
  --sizes normal uhd --capture-only --frames 200 --warmup 50 --cpus 4-23 --env TAK_GHOST_ALL_TYPES=1 \
  --env TAK_RENDER_CAPTURE_EVERY=20 --a-env TAK_GHOST_REFERENCE=1 --allow-unverified-env
ctest -R '^render_build_ghosts$'
```

**F** (hidden-unit Fade). Plain base (the bench uses existing scenes plus env):

```sh
TMPDIR=~/tak-tmp/tmp $T tools/hidden_fade_bench.sh --a $BASE/takclient --b $CAND/takclient --data $TD \
  --output $X/F --repeats 4 --frames 900 --warmup 300 --cpus 0-3
tools/hidden_fade_bench.sh --a $BASE/takclient --b $CAND/takclient --data $TD --output $X/F-cap --capture --cpus 16-23 -- --capture-only
```

Compare `t_hidden_units` and the frame distributions; `hidden_cover_visits` (= the base's
exact tests) and `hidden_cover_reaches` (exact tests run) exist only in the candidate;
`hidden_cover_tests` (sprite/triangle tests) in clients with measurement B. Correctness:
`-- --env TAK_HIDDEN_VERIFY=1 --allow-unverified-env` (not for timing).

**G** (view-count deltas). Plain base:

```sh
for s in R-2h-1800 L-2h-1800 R-bench-1900 L-bench-1900; do
  zcat tools/scenarios/we-timing/$s.scn.gz > $X/$s.scn
  taskset -c 4-23 $CAND/viewcount_bench $X/$s.scn --data $D --warm 1000 --ticks 300 --save $X/$s.vcb
done
taskset -c 4-23 $CAND/viewcount_bench tools/scenarios/viewcount/march.scn --data $D --mode retail --warm 300 --ticks 300 --save $X/march.vcb
for f in $X/*.vcb; do $T $CAND/viewcount_bench --load $f --reps 15; $T $CAND/viewcount_bench --load $f --reps 15 --atomic; done
tools/we_timing.sh --base $BASE --cand $CAND --rounds 2 --out $X/G-we && python3 tools/we_timing_dist.py $X/G-we --warm 1000
mkdir -p $X/sits && gzip -c tools/scenarios/viewcount/march.scn > $X/sits/R-march.scn.gz && cp $X/sits/R-march.scn.gz $X/sits/L-march.scn.gz
tools/we_timing.sh --base $BASE --cand $CAND --sitdir $X/sits --sits R-march,L-march --rounds 3 --warm 300 --ticks 1000 --out $X/G-march
python3 tools/we_timing_dist.py $X/G-march --warm 300
```

Repeat the two `we_timing.sh` runs with `TAK_SIM_WORKERS=1` (worker path). Check
specifically whether untouched phases shift (one worker round showed +2-3%).
Operation counts: `$CAND/viewcount_delta_test probe`.

**H** (Legion static refresh). Base `pf-H-base` = `822c36c3` (461e9c4b + the bench),
`~/tak-tmp/build-pf-H-base-o2`:

```sh
tools/legion_staticsync_ab.sh --base ~/tak-tmp/build-pf-H-base-o2 --cand $CAND --rounds 5 --out $X/H -- --steps 1500
<build>/legion_staticsync_bench --structures 3000 --steps 300 --check      # same hash both, planes_differing=0
tools/we_timing.sh --base $BASE --cand $CAND --sits L-2h-1800,L-bench-1900 --out $X/H-we   # expect no change (gate not reached)
```

Counters: apply `/home/pocket_geek/tak-tmp/tmp/pfH-counters.patch` to a scratch tree, compare with and without `TAK_H_NOINDEX=1`.

**I** (Legion route invalidation). Plain base; decides keep or remove:

```sh
tools/we_timing.sh --base $BASE --cand $CAND --sitdir tools/scenarios/perf --sits L-routeinv-local-10000,L-routeinv-local-5000,L-routeinv-scatter-10000,L-routeinv-many-2500 --rounds 4 --warm 400 --ticks 1000 --catchup 0 --out $X/I
tools/we_timing.sh --base $BASE --cand $CAND --sits L-2h-1800,L-bench-1900 --rounds 4 --out $X/I-we
zcat tools/scenarios/perf/L-routeinv-local-10000.scn.gz > $X/l10k.scn
ROUNDS=4 $T tools/scenarios/perf/routeinv_ab.sh $BASE $CAND $X/l10k.scn 900 $X/I-sync && python3 tools/scenarios/perf/routeinv_ab.py $X/I-sync 400
CORES=4-7 tools/scenarios/perf/routeinv_pmu.sh $BASE $CAND $X/l10k.scn 900 $X/I-pmu && python3 tools/scenarios/perf/routeinv_pmu.py $X/I-pmu 400
$CAND/legion_scenario $X/l10k.scn --mode legion --offsets 0 --workers --ticks 900 --no-observer --json --routeidx
```

Repeat the PMU run for local-5000, scatter-10000, many-2500 and the recordings
(`EXTRA="--data $D --catchup 60"` on unzipped L-2h-1800 / L-bench-1900); cite instructions,
not cycles. `ARGS_B=--no-route-index` with `$CAND` on both sides isolates the index.

**J** (dirty-rectangle clustering). Plain base; pair driver
`/home/pocket_geek/tak-tmp/tmp/pfJ/timing/pair.sh` (set `BB`/`CB` to `$BASE`/`$CAND`; it
runs `legion_scenario --mode legion --serial --offsets 0` with `TAK_PHASE=1
TAK_PHASE_MS=0`, base on 0-1 and candidate on 2-3, then swapped):

```sh
for s in big:150 edge:300 mid:300 small:600; do
  $T /home/pocket_geek/tak-tmp/tmp/pfJ/timing/pair.sh $X/J 3 tools/scenarios/perf/clusterchurn-${s%:*}.scn ${s#*:}
  python3 /home/pocket_geek/tak-tmp/tmp/pfJ/timing/stats2.py $X/J clusterchurn-${s%:*} 20
done
$T taskset -c 0 $CAND/legion_cluster_test --bench
$T taskset -c 0 $CAND/legion_cluster_test --bench-scn tools/scenarios/perf/clusterchurn-{small,mid,edge,big}.scn
TAK_LCLUSTER=1 $CAND/legion_scenario tools/scenarios/perf/clusterchurn-big.scn --mode legion --serial --offsets 0 --ticks 200 2>&1 | grep LCLUSTER
```

Identity: `legion_scenario tools/scenarios/perf/clusterchurn-<s>.scn --mode both --json` on
both builds, `cmp`.

**K** (shadow atlas). Plain base client, the integrated `render_benchmark.py` (scenes
renamed `shadow-pages-*` at integration; the base client understands their settings):

```sh
$T python3 tools/render_benchmark.py --a $BASE/takclient --b $CAND/takclient --data $TD --cpus 0-3 --sizes uhd \
  --scenes shadow-pages-dense shadow-pages-above shadow-pages-below shadow-pages-moving shadow-pages-pan shadow-pages-osc shadow-pages-constrained \
  --repeats 2 --output $X/K
$T python3 tools/render_benchmark.py --a $BASE/takclient --b $CAND/takclient --data $TD --cpus 0-3 --sizes normal \
  --scenes stationary-mixed pan-snap moving total10k-visible zoom churn --repeats 2 --output $X/K-normal
$T python3 tools/render_benchmark.py --a $CAND/takclient --b $CAND/takclient --a-env TAK_SHADOW_PAGES_MAX=4 ... # same-binary pre-K reference
python3 tools/render_benchmark.py ... --capture-only --frames 730 --warmup 100 --env TAK_TERRAIN_SETTLE=1 --sizes uhd \
  --scenes shadow-pages-dense shadow-pages-above shadow-pages-below        # captures; A/A with --a only
python3 tools/render_benchmark.py ... --a-env TAK_SHADOW_PAGE_MAX=4096 --scenes shadow-pages-dense --capture-only ...   # fidelity control
```

Add `--env TAK_GPU_CAP_MIB=<n>` for constrained VRAM (not below 448: base crashes at
384). Helpers (outside the repo): `/home/pocket_geek/tak-tmp/pf-K/{capdiff,cmpstats,summ,srun}.py`.

**L** (auras). Base `a6fbc251` (461e9c4b + bench + test), `~/tak-tmp/build-pf-L-base-o2`:

```sh
tools/aura_bench_ab.sh --base ~/tak-tmp/build-pf-L-base-o2 --cand $CAND --rounds 6 --out $X/L
tools/we_timing.sh --base ~/tak-tmp/build-pf-L-base-o2 --cand $CAND --rounds 2 --sits R-2h-1800,L-2h-1800,R-bench-1900 --data $D --out $X/L-we
TAK_SIM_WORKERS=1 tools/we_timing.sh ... (same)             # pooled path; R-bench-1900 is the A/A control
```

Pooled-path identity on a full replay: `legion_identity` with 8+ cores per process, or
`TAK_SIM_WORKERS=3` as in `/home/pocket_geek/tak-tmp/pfL-replay/run.sh`.

**Whole branch.** `tools/we_timing.sh --base $BASE --cand $CAND --rounds 2` (four WE
situations, serial and `TAK_SIM_WORKERS=1`) and, for quoted numbers, `--replays` with the
replays patched by `legion_identity.py patch-replay`; `simperf --mode match --units 4000
--ticks 1800` pairs (report core and `complete_*`); the render matrix `--scenes all`
(historical 24) at both sizes.

### Results: simulation (A, D, G, L, measurement A, combined workloads)

Serial measurement, 2026-10-30 22:06 to 2026-10-31 03:33, one agent, every timed command through
`/home/pocket_geek/tak-tmp/tools/timing.sh` (flock, cores 0-3). Base = `461e9c4b`, candidate =
`perf-followup` at the integration (`fccacc61`, code identical to `dad93045`).

**Provenance**

- Builds (Debug, `CMAKE_CXX_FLAGS=-O2 -g`, `-ffp-contract=off` from the tree, GCC 16.2.1, mold,
  `TAK_TEST_DATA` set; all targets, see Integration): `BASE=~/tak-tmp/build-pfint-base-o2` (`461e9c4b`),
  `CAND=~/tak-tmp/build-pfint-o2`. Base tools the plain base lacks, built against the base's own objects
  and libraries with the same flags: `simperf` with `pf-mA`'s `tools/simperf.cpp` (`ecd3efeb`, identical
  in the candidate; `~/tak-tmp/tmp/pf-final/mAbase/simperf`), `featburn_bench`
  (`tools/featburn_build_base.sh`, `~/tak-tmp/tmp/pf-final/featburn-base/`). Aura base:
  `~/tak-tmp/build-pf-L-base-o2` (`a6fbc251` = `461e9c4b` + bench and test, no `src/` change).
  Attribution: the item heads alone, `~/tak-tmp/build-pf-{A,D,G,L}-o2` (`38b303fb`, `9d1e9636`,
  `233f08b4`, `ec1bc4d6`); every build checked up to date with `ninja -n` (only the build id restamps).
- Hardware: Core Ultra 9 275HX, cores 0-3 (P-cores) for timing. Pinning per harness: `we_timing.sh`
  runs each pair concurrently on 0-1 and 2-3 (with 2 CPUs `SimPool::defaultWorkers()` is 0: the serial
  pass; `TAK_SIM_WORKERS=1` runs the pooled path with one worker); `featburn_timing.sh`, `wall.sh`,
  `viewcount_bench` on core 2; `simperf` and `aura_bench` on 0-3. No documented harness has a full P-core
  (0-7) variant, and 4-7 are the build/test pool, so none was run.
- Host load. The machine was not quiet when the phase began: other workflows ran scenario sweeps and
  builds on 4-23 (load average 18-45; one unpinned compile reached cores 0-3 at 21:27). The task's
  "load below 3" did not come in 60 minutes of waiting, so **pass 1** (22:06-00:26) ran under load
  average 9-55. The load then fell to 1.5-3 (only this run), and **pass 2** repeated every timing step
  (01:06-02:53, the attribution runs again 02:53-03:33). **All numbers below are pass
  2** unless marked; pass 1 (`~/tak-tmp/tmp/pf-final/*`, pass 2 in `.../q/` and `.../q2/`) agrees in
  sign on every item-level number, with whole-tick pair spreads 5-20x wider (e.g. R-2h tick pair range
  -27.7..+30.4% in pass 1, -1.9..-0.4% in pass 2). A desktop backup (`restic`, unpinned, all 24 cores)
  ran from about 02:20 to 02:30; the runs it overlapped (the last `TAK_SIM_WORKERS=1` replay pair and the
  first attribution set) were repeated afterwards and only the repeats are quoted.
- Statistics: "pair d%" is cand/base - 1 per swapped or concurrent pair, median [min..max] over the
  pairs; n is the number of pairs. Distributions pool every measured tick of every run (warm-up dropped).
  SIMPHASE prints 0.01 ms steps. `explore` is inside `sep`; the aura pass is inside `sep` too
  (SIMPHASE `other=` subtracts `sep`).
- Correctness, separate from timing (cores 4-23): `ctest -R 'featgen|reclaim|exploration|viewcount|aura|sim_pool|simpool|cli_tools'`
  on the candidate, 15/15 passed; `featgen_test` on both builds, the four `spread ... hash` lines equal
  (higher/lower gap 1 and 70); `viewcount_delta_test probe` reproduces the audit probe (below); every
  timing run below that prints a state or buff hash prints the same one for base and candidate
  (`we_timing` final hashes, simperf's 30-60 checkpoint hashes per run, featburn hashes on all 12
  workloads, `aura_bench` buff digests, `viewcount_bench` "planes EQUAL").

**Measurement A: core vs complete tick** (simperf, base simperf with the new fields; `simperf_pair.sh`:
one discarded warm-up run per build, then alternating pairs; ms per tick)

| workload (n pairs) | build | core mean / median / p95 / p99 / max | complete mean / median / p95 / p99 / max | hash total | over 33.3 ms core / complete |
|---|---|---|---|---|---|
| Ultima Online B1, 8 units, 1800 t (8) | base | 0.145 / 0.129 / 0.171 / 0.215 / 7.9 | 0.318 / 0.130 / 0.207 / 5.40 / 8.1 | 311 ms (60 hashes) | 0 / 0 |
| | cand | 0.025 / 0.009 / 0.040 / 0.057 / 7.6 | 0.196 / 0.009 / 0.053 / 5.25 / 7.6 | 308 ms | 0 / 0 |
| generated map, 2000 units, 900 t (5) | base | 2.97 / 2.88 / 3.53 / 3.99 / 49.0 | 3.06 / 2.89 / 3.75 / 6.01 / 49.0 | 81 ms (30) | 1 / 1 |
| | cand | 2.87 / 2.79 / 3.39 / 3.95 / 49.2 | 2.96 / 2.81 / 3.69 / 6.03 / 49.2 | 83 ms | 1 / 1 |
| generated map, 4000 units, 1800 t (5) | base | 5.26 / 5.21 / 6.24 / 6.93 / 52.4 | 5.42 / 5.25 / 6.65 / 10.40 / 52.4 | 288 ms (60) | 1 / 1 |
| | cand | 5.09 / 5.04 / 6.00 / 6.72 / 52.0 | 5.25 / 5.06 / 6.43 / 10.12 / 52.0 | 287 ms | 1 / 1 |

The due `stateHash()` (every 30th tick) is the dominant p99 term whenever the core tick is small: Ultima
Online B1 core p99 0.215 ms vs complete p99 5.40 ms (base), 4000 units 6.93 vs 10.40 ms. The complete
distribution is the one to quote for service time. Pair d% of the candidate (whole service =
`complete_mean`, median [min..max]): Ultima Online B1 **-38.4% [-41.3..-33.6]** (world part 239 -> 23 ms
per run, -90.4%: the map-sized exploration and burn scans of D and A on a large retail map with 8
units; core p99 -74%); 2000 units -3.0% [-4.9..+1.5]; 4000 units -3.1% [-4.3..+5.0] (4 of 5 pairs negative; the first
pair's candidate run was the slow one, 5.71 ms). Hashes per tick are unchanged (no item touches
`stateHash`).

**A. Feature burning and fading** (`featburn_timing.sh ... 5 3000 2`, 5 swapped rounds per workload,
core 2, us per tick; routine = `tickBurning` + `tickFeatureFades` alone, tick = the complete
`World::tick` of a world with two idle monarchs; hashes identical on all 12 workloads)

| workload | features | base mean | cand mean | saved/tick | pair ratio median [min..max] | p99 base -> cand |
|---|---|---|---|---|---|---|
| Ulasem nofire routine | 1250 | 0.761 | 0.011 | 0.75 | 0.014 [0.014..0.015] | 0.77 -> 0.01 |
| Ulasem sparse routine (1.1 burning) | 1250 | 0.791 | 0.047 | 0.74 | 0.060 [0.052..0.065] | 2.33 -> 1.41 |
| Ulasem fire routine (9.8 burning) | 1250 | 0.964 | 0.195 | 0.77 | 0.201 [0.179..0.209] | 2.32 -> 1.32 |
| Ulasem fade routine | 1250 | 1.203 | 0.105 | 1.10 | 0.087 [0.079..0.143] | 2.11 -> 0.92 |
| gen 1024 nofire routine | 11,595 | 7.99 | 0.014 | 7.98 | 0.002 [0.002..0.002] | 9.0 -> 0.01 |
| gen 1024 sparse routine | 11,595 | 7.90 | 0.129 | 7.77 | 0.016 [0.015..0.017] | 11.0 -> 2.4 |
| gen 1024 fire routine (82 burning) | 11,595 | 10.31 | 1.95 | 8.36 | 0.190 [0.186..0.275] | 25.8 -> 1.6 |
| gen 1024 fade routine | 11,595 | 13.44 | 0.27 | 13.17 | 0.020 [0.020..0.021] | 15.8 -> 1.9 |
| Ulasem nofire tick (A+D) | 1250 | 18.22 | 4.61 | 13.61 | 0.250 [0.239..0.261] | 29.7 -> 7.4 |
| Ulasem fire tick (A+D) | 1250 | 18.43 | 4.85 | 13.58 | 0.263 [0.259..0.307] | 36.9 -> 10.5 |
| gen 1024 nofire tick (A+D) | 11,595 | 44.34 | 4.77 | 39.58 | 0.107 [0.106..0.108] | 72.5 -> 7.7 |
| gen 1024 fire tick (A+D) | 11,595 | 47.03 | 6.97 | 40.06 | 0.147 [0.144..0.151] | 255 -> 18.1 |

Forested maps with and without fires, whole tick: the integrated candidate cuts the idle-world tick to
0.11-0.26 of base, mostly D's exploration summaries. A alone (`pf-A` bench vs base, 3 rounds): Ulasem
nofire tick 18.08 -> 17.34 us, ratio **0.961 [0.953..0.976]**, fire tick 0.963 [0.957..0.989]; gen 1024
nofire tick 44.9 -> 36.5 us, 0.817 [0.796..0.827], fire 0.823 [0.823..0.851]. Every pair of every
workload is a gain, the counter-free routine never visits a feature without fires. Real games rarely burn
(0 ignitions in the 300 s gates), so in a recorded game A is worth about 0.75 us (Ulasem) to 8 us
(11.6k features) per tick.

**D. Exploration block summaries: map-size cost separated from unit cost**

Map-sized part (`wall.sh --reps 9 --ticks 3000`, 16 idle bodies, whole run on core 2, nothing but the
fixed per-tick cost changes):

| map (plane cells) | base us/tick | cand us/tick | saved us/tick, pair median [min..max] (n=9) |
|---|---|---|---|
| flat 256 (16,384) | 5.0 | 3.0 | 2.0 [1.7..2.3] |
| Ulasem Arena (102,400) | 18.0 | 4.7 | 13.3 [13.0..13.7] |
| flat 1024 (262,144) | 40.3 | 8.7 | 31.3 [31.0..31.7] |
| flat 2048 (1,048,576) | 176.3 | 46.0 | 131.0 [129.7..135.0] |

The saving is linear in plane size (about 0.125 us per 1000 cells). Work counters (`TAK_PMU`, explore
phase, median per tick, one run each on core 2, pass 1; instruction counts do not depend on load):
idle 1024 1,238,312 -> 7,490 instructions; explore-move-2048 8.03M -> 1.83M; the march (Ulasem Arena,
4,400 moving) 18.4M -> 6.5M; R-2h situation 5.80M -> 3.55M (D and G together).

SIMPHASE (`run.sh`, 6 concurrent pairs x 400 ticks, ms per tick, explore mean base -> cand, pair d%):

| situation | explore base -> cand | explore pair d% | tick base -> cand (mean / p99) | tick pair d% |
|---|---|---|---|---|
| explore-idle-ulasem | 0.0125 -> 0.0004 | -97.1 [-97.7..-95.9] | 0.020 -> 0.010 / 0.03 -> 0.01 | -50.3 [-51.5..-50.0] |
| explore-idle-1024 | 0.031 -> 0.0004 | -98.6 [-98.9..-98.4] | 0.040 -> 0.010 / 0.06 -> 0.01 | -75.0 [-75.2..-74.9] |
| explore-idle-2048 | 0.125 -> 0.0003 | -99.8 [-99.9..-99.7] | 0.133 -> 0.010 / 0.18 -> 0.02 | -92.3 [-92.5..-92.1] |
| explore-move-2048 (1000 units), D+G | 0.381 -> 0.184 | -51.8 [-52.1..-50.9] | 1.22 -> 1.00 / 2.14 -> 1.86 | -18.3 [-19.1..-15.4] |
| explore-reveal-2048 (4800, pooled, w1), D+G | 0.896 -> 0.536 | -40.3 [-41.2..-39.0] | 4.54 -> 4.17 / 5.25 -> 4.86 | -7.9 [-8.9..-7.6] |

Separating D from the unit-dependent work on explore-move-2048 (item heads alone vs base, 6 pairs each):
base explore 0.379 ms; D alone 0.256 (saves 0.124 ms, = the idle-2048 map cost of 0.125 ms, so D leaves
the unit-dependent reveal work unchanged), G alone 0.310 (saves 0.072 ms, all unit-dependent), both
0.184 (0.196 ms, the sum). Pooled reveal-2048: D alone explore -19.8% [-22.4..-15.4], tick -3.1%
[-5.7..-2.1]; G alone explore -22.3%, tick -3.5% [-4.0..-2.4].

**G. View-count deltas**

Audit probe (`viewcount_delta_test probe`): one-cell move, flat terrain, sight 320, height 100: rebuild
2,514 counter updates, delta 82, 82 cells changed; eye 100 -> 106 at the same cell: 2,514 updates rebuilt,
0 as deltas, 0 cells changed. Reproduced exactly (an operation count, not a speedup).

Per move, real recorded moves replayed through both paths in one binary (`viewcount_bench --load ...
--reps 15`, core 2; "plain" = serial adds, "atomic" = the worker path's relaxed atomics; planes equal
after every replay; the delta time includes the span and limit-table cost):

| capture (moves) | counter updates rebuild -> delta | ns plain rebuild -> delta (ratio) | ns atomic rebuild -> delta (ratio) |
|---|---|---|---|
| R-2h-1800 (9,061) | 866.6 -> 23.1 | 2466 -> 1014 (0.411) | 4217 -> 1185 (0.281) |
| L-2h-1800 (16,760) | 289.9 -> 22.8 | 2337 -> 948 (0.406) | 2761 -> 1102 (0.399) |
| R-bench-1900 (7,639) | 574.6 -> 26.8 | 3027 -> 1161 (0.384) | 4040 -> 1367 (0.338) |
| L-bench-1900 (7,230) | 359.7 -> 22.3 | 2273 -> 948 (0.417) | 2852 -> 1088 (0.381) |
| march (70,096; 234 per tick) | 1160.6 -> 49.3 | 2447 -> 907 (0.371) | 5073 -> 1152 (0.227) |

(The bench's "direct" path, the rejected per-cell admission variant, is 0.68-0.71x plain / 0.35-0.62x
atomic: the limit tables are worth a further 30-40%.)

Whole tick on the march workload (`we_timing.sh --sitdir ... R-march,L-march --rounds 3 --warm 300 --ticks
1000`, 6 pairs, 4,400 marching bodies, weapons off):

| run | R-march tick mean base -> cand, pair d% | L-march tick mean, pair d% | explore pair d% R / L |
|---|---|---|---|
| integrated, serial | 5.79 -> 5.28, **-7.8% [-15.8..-6.5]** | 9.12 -> 8.65, **-5.0% [-7.2..-3.8]** | -55.3 / -53.8 |
| integrated, `TAK_SIM_WORKERS=1` | 5.82 -> 5.25, -9.8% [-10.4..-8.9] | 9.26 -> 8.88, -5.4% [-7.9..+4.5] | -67.9 / -64.4 |
| G alone, serial | 5.74 -> 5.34, -7.3% [-8.0..-5.4] | 9.34 -> 8.73, -5.5% [-13.8..-2.5] | -52.4 / -50.7 |
| D alone, serial | 5.70 -> 5.71, -1.0% [-1.8..+6.3] | 9.19 -> 9.11, -0.3% [-5.2..+1.3] | -4.8 / -4.7 |

March tick distribution, integrated serial (ms): R base mean 5.79 / median 5.74 / p95 6.53 / p99 6.92 /
max 9.51 -> cand 5.28 / 5.25 / 5.66 / 5.96 / 6.32; pair-median of p95 -7.9%, p99 -7.2%. L base 9.12 /
8.92 / 10.72 / 13.11 / 15.97 -> 8.65 / 8.45 / 10.08 / 12.76 / 15.65. No tick over 33.3 ms on either side.
Untouched phases do not shift on the integrated build (scripts, movement, other within +-2%). On the WE
situations, see "Combined" below.

**L. Auras** (`aura_bench_ab.sh --rounds 6`, ABBA per round on 0-3, us per pass; buff digests identical
everywhere)

| workload | base mean / p99 | cand mean / p99 | per-round d% median [min..max] | retained scratch base -> cand |
|---|---|---|---|---|
| R-2h, 3 workers (11.9k units, 229 emitters) | 99.8 / 118.8 | 52.8 / 89.1 | -47.5 [-48.0..-45.4] | 129,840 -> 25,660 B |
| R-2h, serial | 232.6 / 245.4 | 172.0 / 182.1 | -26.2 [-27.5..-25.1] | 0 -> 25,204 B |
| L-2h, 3 workers | 108.8 / 127.8 | 56.7 / 82.8 | -47.9 [-49.1..-47.2] | 145,800 -> 25,772 B |
| L-2h, serial | 255.9 / 269.8 | 185.1 / 193.4 | -27.7 [-28.8..-26.8] | 0 -> 25,316 B |
| dense 12k / 1000 emitters + debuffs, 3 workers | 2904 / 3325 | 903 / 956 | -68.9 [-69.1..-68.5] | 3,078,000 -> 148,948 B |
| dense 12k / 1000, serial | 8801 / 8910 | 3382 / 3431 | -61.9 [-63.0..-60.7] | 0 -> 147,724 B |
| dense 4k / 100, 3 workers | 99.8 / 129.9 | 46.0 / 67.1 | -54.1 [-55.2..-51.8] | 114,264 -> 17,748 B |
| sparse 12k / 200, 3 workers | 80.3 / 118.0 | 73.0 / 96.6 | -9.3 [-10.4..-7.1] | 40,368 -> 27,064 B |
| sparse 12k / 200, serial | 248.4 / 259.5 | 209.6 / 218.0 | -15.6 [-16.8..-13.7] | 0 -> 26,608 B |
| dense 2000 / 40, serial (below the 2048 gate: unchanged code) | 33.8 / 41.3 | 35.6 / 41.7 | +4.9 [-4.5..+13.5] | 0 -> 0 |
| small 300 / 5, serial (unchanged code) | 0.9 / 1.0 | 0.9 / 0.9 | +0.0 [+0.0..+0.0] | 0 -> 0 |
| relax only, R-2h, 3 workers | 8.1 / 13.2 | 8.2 / 12.1 | -0.6 [-18.6..+20.5] | 0 -> 0 |

The dense-2000 row runs the unchanged serial walk on both sides; its spread straddles zero (noise). Work
counter: `near_scans` per tick on the 2-hour situations fall 1238 -> 1168 (L-2h) and 1230 -> 1171 (R-2h)
(`we_timing.sh --counters`, pass 1): the aura scans no longer go through `forEachNear`.

Aura-heavy whole tick (new workload `tools/scenarios/perf/aurafight.scn`: two battles on Ulasem Arena,
3,200 armed bodies of which 1,200 are Aramon Smiths with the retail AdjustArmor aura, fighting at once;
`we_timing.sh --sitdir <dir with R-/L-aurafight.scn.gz> --rounds 3 --warm 300 --ticks 1000 --catchup 0`,
6 pairs; units 3027 -> 2533 (R) over the window, so the tiled pass runs throughout):

| run | R tick base -> cand (mean / p99), pair d% | sep (aura pass inside) pair d% | L tick pair d% |
|---|---|---|---|
| integrated, serial | 3.97 -> 3.30 / 5.04 -> 4.13, **-16.6% [-18.5..-16.0]** | -48.0 [-48.9..-47.6] | -9.0% [-9.9..+2.1] |
| integrated, w1 | 3.68 -> 3.16 / 4.48 -> 4.10, -15.8% [-17.5..-9.4] | -55.6 | -10.7% [-11.9..-6.9] |
| L alone, serial | 4.00 -> 3.30 / 5.15 -> 4.00, **-15.7% [-21.3..-15.5]** | -46.2 [-49.5..-45.8] | -6.6% [-8.5..+1.3] |

Final hashes identical (`ebe4a7cfff986cee` R, `b4481fd4fca182f5` L). Memory (peak RSS, `/usr/bin/time`,
1300 ticks, 2 runs each): R-2h 196.6/196.7 MB both builds; L-2h 457.3 -> 459.0 MB (+1.7 MB, +0.4%;
not attributed); aurafight 133.4-133.5 MB both.

**Combined workloads** (integrated candidate vs base)

Large moving armies, exact replays (`we_timing.sh --replays` with the four WE recordings re-stamped to
protocol 305, `--rounds 3`, warm 1000 + 1000 measured ticks from the harvest tick; 6 concurrent pairs):

| replay | tick mean base -> cand | tick pair d% | p95 / p99 / max base -> cand | sep pair d% | explore pair d% |
|---|---|---|---|---|---|
| R-2h (10.4k units) | 11.08 -> 10.90 | **-1.9% [-2.2..-0.4]** | 12.31 / 12.90 / 15.5 -> 12.13 / 12.67 / 16.0 | -13.4 | -18.3 |
| L-2h (10.0k) | 23.88 -> 23.75 | -0.4% [-1.3..+0.1] | 30.21 / 31.51 / 32.9 -> 30.12 / 31.56 / 33.6 | -15.5 | -20.8 |
| R-bench (1.6k) | 0.947 -> 0.930 | -2.3% [-7.5..+6.2] | 1.14 / 1.42 / 3.5 -> 1.13 / 1.40 / 4.1 | -18.4 | -34.3 |
| L-bench (1.2k) | 4.05 -> 3.96 | **-1.4% [-5.1..-0.9]** | 5.90 / 6.57 / 7.9 -> 5.71 / 6.33 / 7.4 | -29.6 | -41.4 |

With `TAK_SIM_WORKERS=1` (2 rounds, 4 pairs): L-2h -0.4% [-1.3..+0.0], R-2h **-1.1% [-1.7..-0.8]**
(repeat after the backup; sep -23.8%). Ticks over 33.3 ms: 0 -> 0 except L-2h 0 -> 1 (33.55 ms).

Situations (`we_timing.sh --rounds 3`, warm 1000, 6 pairs; serial / `TAK_SIM_WORKERS=1`):

| situation | tick mean base -> cand | tick pair d% serial | tick pair d% w1 | sep pair d% serial / w1 |
|---|---|---|---|---|
| R-2h-1800 | 11.07 -> 10.94 | **-1.1% [-1.9..-0.4]** | **-1.0% [-1.5..-0.5]** | -12.5 / -23.3 |
| L-2h-1800 | 22.71 -> 22.59 | **-0.4% [-1.3..-0.2]** | -0.1% [-0.8..+4.5] | -13.0 / -20.9 |
| R-bench-1900 | 2.09 -> 2.02 | **-3.9% [-5.7..-0.6]** | **-3.8% [-5.8..-1.0]** | -34.3 / -36.7 |
| L-bench-1900 | 4.20 -> 4.12 | -1.8% [-3.3..+0.0] | -0.0% [-1.8..+0.9] | -30.7 / -32.0 |

A/A control (candidate vs itself, 6 pairs, two sets): R-2h -0.1% [-0.7..+0.6] and +0.1% [-1.0..+2.2],
L-2h -0.0% [-0.4..+0.3] and -1.0% [-6.4..+0.4]; per phase within +-1.5% except one pair. Tick
distributions of the situations (serial, ms): R-2h p95 12.38 -> 12.25, p99 12.91 -> 12.74, max 13.9 ->
16.1; L-2h p95 34.24 -> 34.24, p99 36.52 -> 36.41, ticks over 33.3 ms 384 -> 388 of 6,000 (L-2h's
over-budget ticks are Legion movement, which no sim item touches); R-bench and L-bench none over.

Late game with AI, combat, construction and deaths: `simperf --mode match` (8 Absurd AIs) above: 4000
units -3.1% complete mean (4 of 5 pairs), 2000 units -3.0% [-4.9..+1.5], Ultima Online B1 (8 starting
units, AI builds up) -38.4% [-41.3..-33.6]. Forested maps with and without fires: featburn tick rows above.
Aura-heavy: aurafight above.

Attribution on the 10k-unit situations (item heads alone vs base, `--rounds 3`, 6 pairs, 02:53-03:27,
quiet):

| build | R-2h tick pair d% | R-2h sep / other | L-2h tick pair d% | L-2h sep / other / scripts |
|---|---|---|---|---|
| D alone | -0.7 [-1.1..-0.4] | -5.4 / -0.6 | +0.6 [-2.1..+1.0] | -4.2 / +1.7 / +2.3 [+0.9..+2.4] |
| G alone | **+0.9 [+0.2..+1.1]** | -5.3 / **+3.5 [+2.9..+4.1]** | -0.2 [-0.5..-0.0] | -5.9 / +0.6 / +0.9 |
| L alone | -0.0 [-0.8..+1.0] | -2.6 / -0.0 | +0.8 [-0.7..+1.3] | -3.2 / **+3.9 [+2.0..+4.6]** / +1.9 |
| integrated (pass 2 above) | -1.1 [-1.9..-0.4] | -12.5 / -0.4 | -0.4 [-1.3..-0.2] | -13.0 / -0.3 / +0.8 |

The first quiet attribution set (02:20-02:53) agrees: G alone R-2h tick +0.9 [-0.3..+1.5], other +3.7
[+2.2..+4.2]; L alone L-2h tick +0.9 [+0.6..+6.5], other +3.9 [+3.6..+9.5]. Work counters settle what
that is (`TAK_PMU`, situations, ticks 1000-2000, one run per build on core 2, mean per tick):

| R-2h / L-2h | tick instructions | tick cycles | sep instructions | `other` instructions | `other` cycles |
|---|---|---|---|---|---|
| integrated | -1.5% / -0.9% | -0.2% / -2.2% | -25.6% / -26.8% | -0.1% / -0.2% | +0.2% / -2.9% |
| G alone | -0.8% / -0.5% | +0.9% / -1.5% | -14.5% / -15.8% | +0.1% / +0.1% | +2.5% / -1.3% |
| L alone | -0.2% / -0.1% | +2.9% / +0.8% | -4.2% / -4.8% | +0.3% / +0.0% | +3.3% / +2.1% |

The phases an item does not touch execute the same instructions in every build (`other` +-0.3%,
`scripts` +0.0..+0.2%); their extra cycles in the single-item builds are that binary's code and data
layout, not work, and they are absent from the integrated binary. The consequence for the dispositions:
on the 10k-unit recordings G alone and L alone are not a measurable whole-tick gain (their work there is
0.02-0.06 ms per tick, below the +-1% layout effects), so their verdicts rest on the workloads where their work is
large (the march, the aura fight) and on instructions; the integrated branch is a gain on all four
recordings.

**Unexpected**

- **COB scripts phase on the L-2h replay: +5.0% [+4.4..+5.8]** (0.096 ms per tick; `TAK_SIM_WORKERS=1`:
  +4.4% [+1.4..+5.5]) in the integrated `takclient`; the same build's `legion_scenario` on the L-2h
  situation shows +0.8% [+0.5..+1.0] and the R-2h replay -1.0%. No sim item touches the script VM and the
  scripts phase runs the same instructions in every build (+0.0..+0.2%), so this is layout in that
  binary; `other` falls 4.3% in the same runs and the whole L-2h replay tick is -0.4% [-1.3..+0.1] (one
  pair of six at +0.1%). Flagged for the Legion measurement, which owns the other L-2h changes.
- The single-item builds' layout effects above (G alone `other` +3.5% on R-2h, L alone `other` +3.9% on
  L-2h, D alone `scripts` +2.3% on L-2h) repeat across both quiet sets: an item measured alone on a
  small-gain workload can read as a +1% loss that is not its code.
- The 4000-unit simperf's first pair: candidate 5.71 ms vs base 5.44 ms (the other four pairs -1.9..-4.3%).

**Recommended dispositions (sim)**

| item | recommendation | deciding numbers (pass 2) |
|---|---|---|
| Measurement A | Implemented and verified (instrumentation) | complete p99 differs from core p99 by the hash: 0.215 vs 5.40 ms (UOB1), 6.93 vs 10.40 ms (4000 units); checkpoint hashes identical |
| A Feature burning/fading | **Implemented and verified** | routine 0.002-0.20x of base in every pair of every workload; A alone, whole idle tick Ulasem 0.961 [0.953..0.976], gen 1024 0.817 [0.796..0.827] |
| D Exploration summaries | **Implemented and verified** | map-sized cost removed: 2.0 / 13.3 / 31.3 / 131 us per tick at 16K / 102K / 262K / 1M cells (n=9, tight); unit-dependent work unchanged; UOB1 whole service -38% |
| G View-count deltas | **Implemented and verified** | per move 0.37-0.42x (plain), 0.23-0.40x (atomic); march whole tick -7.8% [-15.8..-6.5] (R), -5.0% [-7.2..-3.8] (L); G alone -7.3% [-8.0..-5.4]; explore instructions -15..-64%. Alone on the 10k recordings: within layout effects (R-2h +0.9%, same instructions) |
| L Auras | **Implemented and verified** | pass -26..-69% in every round of every tiled workload; aura-heavy whole tick -16.6% [-18.5..-16.0], L alone -15.7% [-21.3..-15.5]; worker scratch 129,840 -> 25,660 B (R-2h), 3.08 MB -> 149 KB (dense). Cost: serial mode keeps 25-148 KB it did not before. Alone on the 10k recordings (aura work 0.02-0.07 ms): R-2h -0.0%, L-2h +0.8% with unchanged instructions outside the pass (layout) |

None of the sim items is inside noise or negative on its own workload. On the recorded 10k-unit games the
whole-tick gain of the four together is small (R-2h -1.9% replay / -1.1% situation, L-2h -0.4% with one
of six replay pairs at +0.1%), as screening predicted; it is a gain, not a regression.

**Commands** (pass 2; `X=~/tak-tmp/tmp/pf-final`, `Q=$X/q`, drivers `$X/master.sh` (pass 1),
`$X/qrun.sh` (pass 2), `$X/attr.sh`, `$X/extra.sh`, `$X/rerun.sh`, `$X/rss.sh`; summaries
`$X/simperf_summ.py`, `$X/featburn_pairs.py`; all run from `~/tak-tmp/wt-perf-int` with scratch
`HOME`/`XDG_*`):

```sh
tools/we_timing.sh --base $BASE --cand $CAND --rounds 3 --data $D --out $Q/WE              # + TAK_SIM_WORKERS=1 ... --out $Q/WE-w1
tools/we_timing.sh --base $BASE --cand $CAND --replays $X/replays --sits R-2h-1800,L-2h-1800,R-bench-1900,L-bench-1900 --rounds 3 --data ~/tak_data --out $Q/REP
gzip -c tools/scenarios/perf/aurafight.scn > $X/sits-aura/R-aurafight.scn.gz; cp $X/sits-aura/R-aurafight.scn.gz $X/sits-aura/L-aurafight.scn.gz
tools/we_timing.sh --base $BASE --cand $CAND --sitdir $X/sits-aura --sits R-aurafight,L-aurafight --rounds 3 --warm 300 --ticks 1000 --catchup 0 --data $D --out $Q/aurafight
tools/we_timing.sh --base $BASE --cand $CAND --sitdir $X/sits --sits R-march,L-march --rounds 3 --warm 300 --ticks 1000 --data $D --out $Q/G-march
$T $X/simperf_pair.sh $Q/sp-match4000 5 --mode match --units 4000 --ticks 1800 --data ~/tak_data   # also --units 2000 --ticks 900 (5), --units 8 --map 'Ultima Online B1' (8)
$T tools/featburn_timing.sh $X/featburn-base/featburn_bench $CAND/featburn_bench $D 5 3000 2 > $Q/featburn.log
tools/scenarios/perf-explore/wall.sh --base $BASE --cand $CAND --reps 9 --ticks 3000 --data $D --out $Q/D-wall
tools/scenarios/perf-explore/run.sh --base $BASE --cand $CAND --sits explore-idle-256,explore-idle-ulasem,explore-idle-1024,explore-idle-2048,explore-move-2048 --rounds 3 --warm 100 --ticks 400 --data $D --out $Q/D-serial
TAK_SIM_WORKERS=1 tools/scenarios/perf-explore/run.sh --base $BASE --cand $CAND --sits explore-reveal-2048 --rounds 3 --warm 100 --ticks 400 --data $D --out $Q/D-pooled
tools/aura_bench_ab.sh --base ~/tak-tmp/build-pf-L-base-o2 --cand $CAND --rounds 6 --data $D --out $Q/L
$T taskset -c 2 $CAND/viewcount_bench --load $X/G-vcb/<capture>.vcb --reps 15 [--atomic]     # captures: viewcount_bench <scn> --save, on 4-23
python3 tools/we_timing_dist.py <out> --warm <warm> --phases tick,sep,explore                 # distributions
```

Attribution runs swap `--cand` for `~/tak-tmp/build-pf-<A|D|G|L>-o2`.

### Results: AI (B, AI resurrection)

Serial measurement, 2026-10-31 03:35 to 04:09, one agent; timing 04:01:57-04:08:58 with load
average 1.0-1.7 (no other workflow running; correctness runs had finished before it). Every timed
command through `/home/pocket_geek/tak-tmp/tools/timing.sh` (flock, cores 0-3).

**Provenance**

- Builds (Debug, `CMAKE_CXX_FLAGS=-O2 -g`, `-ffp-contract=off` from the tree, GCC 16.2.1 via ccache, all
  checked up to date with `ninja -n`: only the build id restamps):
  - candidate `CAND=~/tak-tmp/build-pfint-o2`: `perf-followup` integration (`fccacc61`, code identical to
    `dad93045` and `9070f50e`), all targets.
  - bench base `~/tak-tmp/build-pf-B-pure-o2`: `bdbdae58` = `461e9c4b` + the bench program only
    (`tools/ai_revive_bench.cpp`, identical in the candidate); only the `ai_revive_bench` target is built
    there (the bench links nothing else from that tree that matters).
  - plain base for the uninstrumented AI phase: `~/tak-tmp/tmp/pf-final/mAbase/simperf` (`461e9c4b` with
    `pf-mA`'s `simperf.cpp`, from the simulation results above).
  - counter base `~/tak-tmp/build-pf-B-base-o2`: `5fcbac9b` = `461e9c4b` + the `TAK_AI_REVIVE_STATS` hook on
    the 0.9.12 search (no behaviour change), all targets.
- Hardware: Core Ultra 9 275HX, timing on P-cores 0-3. `ai_revive_bench` is single-threaded (the scheduler
  picks one of 0-3); `simperf` uses its pool on 0-3. No documented AI harness has a 0-7 variant (and 4-7 are
  the build/test pool), so none was run.
- Statistics: "pair ratio" is cand/base per round (alternating order), median [min..max]; distributions
  are the per-run statistics' median over runs (max = largest). Correctness and counter runs (cores 4-23)
  are separate from the timing runs; the counter hook times every call, so its runs are never used as
  timing of the AI phase.

**Correctness, complete AI command streams** (cores 4-23, before timing)

- `ctest -R '^(ai_behavior|ai_behavior_data|revive_order|classic_ai_.*|ai_profile|ai_profile_data|server_ai_level)$'`
  on the candidate: 14/14 pass (`ai_behavior` holds the 400-scene reference comparison with the 0.9.12
  search, commands, order, `assigned` and hash). `ai_profile`, `ai_profile_data` and
  `classic_ai_map_profile` first failed on a bad `TMPDIR` in my scratch environment and pass on the rerun.
- Bench streams: every one of the 8 workloads emits the same command count and stream hash on base and
  candidate in every run (6 + 5 rounds, below; the bench also checks every call against the first).
- Real games, server replays compared bundle by bundle (`cmprep.py`: header fields, every tick bundle,
  the referee's hash trail): the 7 `--mpai` gates (Inner Circle, seed 1, `TAK_SPEED=80`) on the counter base
  and the candidate give the documented hashes on both, and every recorded tick bundle and every hash
  checkpoint is identical; in 4 of the 7 one side recorded one extra empty bundle (12 bytes) after the
  client's time limit, a wall-clock end effect, alternately on the base and the candidate side. A heavier
  game with live casters: the 8-AI Benchmark (`TAK_BENCH=4`, Ulasem Arena, all five sides, 600 s, 18,003
  bundles): replay body byte-identical, final hash `99a3f429852aa439` on both.
- Where the code runs in real games (`TAK_AI_REVIVE_STATS=1`; `visits` = caster-body distance tests, defined
  the same way in both hooks; `casters`/`records` are counted differently, the candidate stops counting
  after an empty gather):

| game | calls | eligible casters | visits | Resurrect commands | us/call base -> cand (mean / p99) |
|---|---|---|---|---|---|
| 7 `--mpai` gates | 61-301 each (0 for Retail AI) | 0 | 0 | 0 | 0.28-0.33 -> 0.20-0.25 / 0.5-1.3 -> 0.5-0.7 |
| Benchmark level 4, 600 s (8 AIs) | 4,803 | 974 | 93,337 | 0 | 6.71 -> 4.64 / 17.2 -> 16.5 |
| simperf match 4000 units, 1800 t (4 pairs, timed on 0-3) | 720 | 0 | 0 | 0 | 5.89 -> 3.00 / 10.2 -> 4.9 |

  No real game produced a Resurrect order (the bodies were never within 56 px of an idle caster), so the
  reference comparison in `ai_behavior` and the synthetic bench are the only places the raise path runs.
  The Benchmark row ran unpinned on 4-23 with the client and the other build's game beside it: a
  counter, not a timing.

**Timing: `reviveIdle` per call, zero / few / many bodies** (`ai_revive_bench`, 4,000 live units on a
12,288 px map unless noted, 2,000 timed calls after 100 warm-up calls per run, plus one discarded warm-up
run per build and workload; 6 rounds alternating order, us per call)

| workload | records | commands | base mean / median / p95 / p99 / max | cand mean / median / p95 / p99 / max | pair ratio median [min..max] (n=6) | saved us/call |
|---|---|---|---|---|---|---|
| zero: 64 casters, no body | 4,064 | 0 | 101.8 / 101.5 / 103.2 / 106.7 / 156 | 1.38 / 1.38 / 1.40 / 1.41 / 6.5 | 0.0135 [0.0134..0.0138] | 100.4 |
| few: 64 casters, 16 bodies | 4,080 | 0 | 114.0 / 113.8 / 115.4 / 115.7 / 392 | 5.52 / 5.51 / 5.59 / 5.70 / 10.7 | 0.0482 [0.0480..0.0489] | 108.4 |
| near: 64 casters, 300 bodies by them | 4,364 | 47 | 150.6 / 149.9 / 152.5 / 154.2 / 590 | 14.5 / 14.4 / 14.9 / 16.2 / 58.9 | 0.0962 [0.0924..0.0981] | 136.0 |
| many near: 64 casters, 2,000 bodies by them | 6,064 | 59 | 400.3 / 399.0 / 405.5 / 409.8 / 486 | 69.2 / 69.0 / 75.8 / 77.3 / 123 | 0.1737 [0.1712..0.1778] | 331.1 |
| many: 64 casters, 2,000 bodies anywhere | 6,064 | 6 | 376.8 / 376.0 / 384.5 / 393.3 / 3593 | 65.6 / 65.0 / 70.6 / 73.1 / 131 | 0.1741 [0.1625..0.1772] | 311.2 |
| small: 4 casters, 200 bodies by them | 4,204 | 4 | 14.9 / 14.8 / 15.4 / 16.3 / 72 | 6.19 / 6.17 / 6.31 / 6.46 / 78.5 | 0.4126 [0.4005..0.4355] | 8.7 |
| resurrection-heavy: 256 casters, 4,000 bodies by them (new) | 8,256 | 237 | 2859 / 2861 / 2970 / 2997 / 4921 | 413 / 411 / 436 / 455 / 4870 | 0.1457 [0.1394..0.1537] | 2446 |
| big army: 10,000 units, 64 casters, 300 bodies by them (new) | 10,364 | 46 | 539.3 / 538.9 / 546.3 / 549.5 / 4983 | 27.7 / 27.6 / 28.7 / 29.6 / 305 | 0.0523 [0.0483..0.0533] | 511.6 |

Every pair of every workload is a gain; the widest spread (small, 0.40-0.44) is far from 1. The documented
command (`tools/ai_revive_bench.py --rounds 5 --calls 2000`, exit 0, streams SAME) agrees: cand/base of the
round-median means 0.0137, 0.0481, 0.0909, 0.1642, 0.1751, 0.4028 for its six workloads. The occasional base
max of 3-5 ms is a one-off interruption inside a run (the p99 stays at 0.4-3.0 ms). Memory: the candidate
keeps a per-call vector of the raisable bodies (32 bytes each, at most 128 KB at 4,000 bodies, freed at
return; the base kept a vector of claimed ids); no retained state, RSS not measured.

**Timing: the AI phase in a whole game** (`simperf --mode match`, 8 Absurd AIs, uninstrumented; plain base
simperf vs candidate, one discarded warm-up run per build, then alternating pairs on 0-3; `ai_total_ms` =
the AI part of every tick over the run, windows = the AI time of each 30-tick hash interval, first dropped)

| workload (pairs) | AI total base -> cand, ms per run (mean) | AI pair d% median [min..max] | 30-tick AI window base -> cand: mean / median / p95 / p99 / max (ms) | core tick pair d% (all items) |
|---|---|---|---|---|
| 4000 units, 1800 t (8) | 304.8 -> 303.8 | -0.9 [-3.7..+4.3] | 4.26 / 3.71 / 6.33 / 7.30 / 7.54 -> 4.22 / 3.64 / 6.26 / 7.32 / 7.48 | -2.0 [-5.7..+2.6] |
| 2000 units, 900 t (6) | 112.8 -> 112.7 | +0.3 [-6.5..+3.9] | 2.25 / 1.73 / 3.82 / 4.04 / 4.05 -> 2.23 / 1.69 / 3.68 / 4.00 / 4.03 | -3.6 [-11.1..-1.1] |

Checkpoint hashes identical in every pair (60 resp. 30 per run). What B saves here is the per-call
difference counted in the same game (the counter row above): 2.9 us x 720 calls = 2.1 ms per 1800-tick
run, 0.7% of the AI phase and 0.02% of the tick. The AI-phase pair spread (-3.7..+4.3%) is about ten times
that, so **B is not measurable in the whole AI phase or the tick of any real game measured**; the core-tick
gain in the last column is the simulation items' (A, D, G, L), not B's.

**Recommendation (B)**

- **Implemented and verified** as a change to `reviveIdle`: exact (identical streams in the bench, in
  `ai_behavior`'s 400 reference scenes, and identical recorded commands in 8 real games), and a repeatable
  per-call gain on its workload: 0.013-0.17x of base with 64-256 casters and 0-4,000 bodies, 0.41x with 4
  casters, every pair a gain, worst case (256 casters, 4,000 bodies) 2.86 -> 0.41 ms per call.
- **Not a measurable gain in real games**: in every game measured the raise path never fires (0 Resurrect
  orders in the gates, the 8-AI Benchmark and simperf), the per-call saving there is 2-3 us, and the AI
  phase difference is inside its noise (4000 units -0.9% [-3.7..+4.3], 2000 units +0.3% [-6.5..+3.9]). It
  should not be quoted as an AI-phase or tick gain. If the disposition requires a whole-game gain, this item
  would be "Measured and rejected" on that criterion; I recommend keeping it, because it is exact, small,
  and removes a cost that grows as casters x unit records (2.9 ms per call at 256 casters / 8,256 records,
  about a tenth of a 33 ms tick per think).

**Commands** (`P=~/tak-tmp/tmp/pf-final-ai`; driver `$P/bin/qrun.sh`; scripts outside the repo:
`benchpairs.py`/`benchsumm.py` (bench pairs and table), `simperf_pair.sh`/`simperf_pair_stats.sh` (copies of
`pf-final/simperf_pair.sh` with the plain base resp. the counter base and `TAK_AI_REVIVE_STATS=1`),
`aisumm.py` (AI phase), `all.sh`/`gate.sh` (the B gate scripts, keeping the server replay), `game7.sh`
(8-AI game with counters and replay), `cmprep.py` (replay comparison); outputs `$P/q/`, `$P/correct/`):

```sh
$T python3 -I $P/bin/benchpairs.py --base ~/tak-tmp/build-pf-B-pure-o2 --cand $CAND --rounds 6 --calls 2000 --out $P/q/bench
python3 -I $P/bin/benchsumm.py $P/q/bench/raw.jsonl
$T python3 -I tools/ai_revive_bench.py --base ~/tak-tmp/build-pf-B-pure-o2 --cand $CAND --rounds 5 --calls 2000
$T $P/bin/simperf_pair.sh $P/q/sp4000 8 --mode match --units 4000 --ticks 1800 --data ~/tak_data      # and sp2000: 6 --units 2000 --ticks 900
$T $P/bin/simperf_pair_stats.sh $P/q/sp4000-stats 4 --mode match --units 4000 --ticks 1800 --data ~/tak_data
python3 -I $P/bin/aisumm.py $P/q/sp4000
$P/bin/all.sh ~/tak-tmp/build-pf-B-base-o2 $P/correct/gates-base; $P/bin/all.sh $CAND $P/correct/gates-cand   # cores 4-23
CPUS=4-13 $P/bin/game7.sh ~/tak-tmp/build-pf-B-base-o2 $P/correct/bench4 base 600 "Ulasem Arena" TAK_BENCH=4 &
CPUS=14-23 $P/bin/game7.sh $CAND $P/correct/bench4 cand 600 "Ulasem Arena" TAK_BENCH=4 & wait
python3 -I $P/bin/cmprep.py <base>.takrep <cand>.takrep
```

The two new bench workloads are plain `ai_revive_bench` options (`--casters 256 --bodies 4000 --layout near`;
`--units 10000 --bodies 300 --layout near`), no new scenario files.

### Results: Legion (H, I, J)

Serial measurement, 2026-10-31 04:12 to 05:46, one agent. Base = `461e9c4b`, candidate = `perf-followup` at the
integration (code identical to `dad93045`); every timed command through `/home/pocket_geek/tak-tmp/tools/timing.sh`
(flock, cores 0-3).

**Provenance**

- Builds (Debug, `CMAKE_CXX_FLAGS=-O2 -g`, `-ffp-contract=off` from the tree, GCC 16.2.1, all targets; `ninja -n`
  shows nothing stale but the build id): `BASE=~/tak-tmp/build-pfint-base-o2` (`461e9c4b`),
  `CAND=~/tak-tmp/build-pfint-o2`. H base: `~/tak-tmp/build-pf-H-base-o2` (`822c36c3` = `461e9c4b` + the
  `legion_staticsync_bench` commit; the bench source is identical in the candidate). Attribution (item heads alone):
  `~/tak-tmp/build-pf-H-o2` (`d0d11901`), `~/tak-tmp/build-pf-J-o2` (`c09ee993`). H work counters:
  `~/tak-tmp/build-pf-H-cnt` (`2eae31d7` + `/home/pocket_geek/tak-tmp/tmp/pfH-counters.patch`; same `src/` as
  `d0d11901`). I is isolated inside the candidate binary with `legion_scenario --no-route-index` (the bins are still
  maintained, only the query is off), which keeps code layout fixed.
- Hardware: Core Ultra 9 275HX, timing on cores 0-3 (P-cores). Pinning per harness: `legion_staticsync_ab.sh`
  base on 2 + candidate on 3, then swapped; J's pair driver and `routeinv_ab.sh` 0-1 + 2-3 concurrently, then
  swapped (with 2 CPUs `--workers` runs the serial pass); `we_timing.sh` the same; `legion_cluster_test --bench` on
  core 0; `routeinv_pmu.sh` on 4-7 after the timing (instruction counts, not time). No documented harness for these
  items has a full P-core (0-7) variant, so none was run.
- Host load: about 2-3 during every timed step (the runs themselves are 2-4 busy threads; a 10 s sampler wrote
  `load.log`), with one exception: at 05:04:48 something outside this run raised the load to 12 for about two
  minutes during the first routeinv `we_timing` set. That set is not quoted; it was repeated at 05:33-05:43 (load
  2.5-2.8) and only the repeat is quoted. Another session ran an unpinned build in `~/TAK/build-o2` at 04:34-04:50,
  during the correctness runs only; the timing waited for load < 3.
- Statistics: "pair d%" = cand/base - 1 per concurrent pair, median [min..max]; for swapped pairs the
  "round" value is the mean (or geometric mean) of the two swapped pairs. Distributions pool every measured tick.
  SIMPHASE has 0.01 ms steps. n = pairs.
- Correctness, separate from timing (cores 4-23), all on the candidate unless noted:
  `ctest -R 'legion_staticindex|legion_routebins_test|legion_routeindex|legion_cluster_test|legion_route_test|legion_staticblock|legion_staticidle|legion_route_paused'`
  10/10 passed. H: `legion_staticsync_bench --check` on base and candidate for w3000, w2000c12p8, w300, w50 and
  w3000 with `--churn 5 --workers`: identical final hashes, `planes_differing=0` everywhere. J:
  `legion_scenario clusterchurn-{mid,edge,big}.scn --mode both --json` byte-identical to base; `clusterchurn-small`
  identical hashes (all 3 offsets) and identical except the `work.group_loop_iters` / `legion_total` work keys
  (1995 -> 1095), which is I's documented counter change (groups visited), not J. I: the five `L-routeinv-*`
  scenarios, 1000 ticks, base / candidate / candidate `--no-route-index` / candidate with `TAK_LEGION_VERIFY=1`:
  JSON identical on all four, ROUTEIDX decision digests equal index on vs off (`ed9842757cda98dc` local-10000,
  `8bc5dde3ebca7017` local-5000, `c863609991566593` scatter-10000, `96338bd81945cd8b` many-2500, `8feff981a866d1eb`
  scatter-2500), verify clean. Every timing run below printed the same final hash for base and candidate.

**H. Legion static refresh** (`legion_staticsync_ab.sh --rounds 5 -- --steps 1500`: 10 swapped pairs per workload,
1500 steps each; a step = static sync + world tick; ms; one warm-up pair first)

| workload | base sync mean / median / p95 / p99 / max | cand sync mean / median / p95 / p99 / max | sync pair ratio | step (sync + tick) pair ratio |
|---|---|---|---|---|
| w3000 (3000 structures, 6 planes, 4 changes) | 0.260 / 0.224 / 0.547 / 0.807 / 5.5 | 0.122 / 0.086 / 0.414 / 0.677 / 4.7 | **0.471 [0.425..0.530]** | 0.733 [0.661..0.798] |
| w2000c12p8 (2000, 8 planes, 12 changes) | 0.681 / 0.649 / 0.926 / 1.386 / 4.4 | 0.182 / 0.134 / 0.428 / 1.033 / 3.5 | **0.269 [0.245..0.296]** | 0.417 [0.383..0.460] |
| w300 | 0.062 / 0.056 / 0.115 / 0.174 / 3.8 | 0.045 / 0.036 / 0.103 / 0.167 / 1.2 | **0.722 [0.635..0.794]** | 0.754 [0.679..0.825] |
| w50 (below the 64-structure gate) | 0.037 / 0.033 / 0.067 / 0.103 / 1.1 | 0.037 / 0.033 / 0.067 / 0.101 / 4.4 | 0.998 [0.915..1.169] | 0.849 [0.782..0.976] |

The integrated step ratio includes other items' work in the bench's world tick (w50's 0.849 is D, not H). H alone
(`pf-H` vs H base, 10 pairs): sync 0.473 [0.427..0.527] / 0.269 [0.248..0.297] / 0.721 [0.692..0.752] / 1.002
[0.962..1.042], step 0.747 [0.678..0.818] / 0.427 [0.392..0.472] / 0.830 [0.797..0.858] / 1.002 [0.966..1.045]
(w3000 / w2000c12p8 / w300 / w50). A/A control (candidate vs itself, 6 pairs): every ratio 0.995-1.006, ranges
0.957..1.049. Max values are single spikes on both sides (3-5 ms in 15,000 steps) and are not attributed.

Work counters (`HCOUNT`, H counter build, index vs `TAK_H_NOINDEX=1`, 1500 steps; equal `computeCells` calls,
overlaps and hashes both ways):

| workload | `computeCells` calls | structure tests index / walk | per call | index builds |
|---|---|---|---|---|
| w3000 | 42,450 | 571,218 / 127,604,544 (0.45%) | 13.5 / 3006 | 1500 |
| w2000c12p8 | 213,272 | 2,794,568 / 427,825,240 (0.65%) | 13.1 / 2006 | 1500 |
| w300 | 59,436 | 489,948 / 18,188,820 (2.7%) | 8.2 / 306 | 1500 |
| w50 | 70,428 | 3,945,678 both (gate) | 56 | 0 |
| L-2h-1800 situation (300 ticks) | 1,516 | 1,516 both | 1 | 0 |
| L-bench-1900 situation (300 ticks) | 672 | 4,704 both | 7 | 0 |

Recorded games never reach the 64-structure gate, so the index is never built there and the code path equals base;
L-2h / L-bench whole tick is in the I table below (no change). Peak RSS w2000c12p8: 37.3 -> 37.4 MB.

**I. Legion route invalidation**

The route index is isolated in one binary (index on vs `--no-route-index`, `routeinv_ab.sh`, warm 400, ms per tick;
"off/on - 1" > 0 means the index is faster). Two sets: 4 rounds x 900 ticks (500 measured) and 6 rounds x 1400 ticks
(1000 measured):

| workload | static sync per tick in window, off -> on | `staticChanged` per tick off -> on | whole tick off/on - 1, pairs: median [min..max] | per swapped round | nav off/on - 1 |
|---|---|---|---|---|---|
| L-routeinv-local-10000 (17.6 ms tick) set 1, n=8 | 284 -> 79 us | 224 -> 18.4 us | +0.35% [+0.0..+0.7] | +0.25 +0.20 +0.55 +0.35 | +2.8% [+1.7..+3.0] |
| local-10000 set 2, n=12 | | | +0.70% [-0.2..+1.2] | +0.60 +0.50 +0.25 +0.75 +0.30 +0.55 | +2.8% [+1.7..+3.6] |
| L-routeinv-local-5000 (9.0 ms) set 1, n=8 | 155 -> 82 us | 99 -> 24.6 us | +0.20% [-1.6..+0.7] | +0.25 +0.20 +0.40 -0.65 | +1.5% [-0.1..+2.0] |
| local-5000 set 2, n=12 | | | +0.40% [-1.0..+0.7] | +0.40 +0.40 -0.15 +0.25 +0.50 +0.30 | +1.4% [+0.1..+1.9] |
| L-2h-1800 situation (22.6 ms), n=6 | 17.1 -> 16.0 us | 3.5 -> 2.2 us | +0.05% [-2.2..+6.8] | -1.05 +0.10 +3.40 | +0.2% [-2.3..+6.9] |
| L-bench-1900 situation (4.0 ms), n=6 | 4.45 -> 4.2 us | 0.5 -> 0.3 us | +0.10% [-1.0..+9.0] | -0.30 -0.25 +4.90 | +0.1% [-0.9..+8.7] |

(The one +6.8% / +9.0% pair on the recordings is the same round on both; the load was 2.5-2.9 then and the other
five pairs are within +-2.2%: unexplained single pairs, not the index, whose work there is 1.3 / 0.2 us per tick.)
On local-10000 all 10 swapped rounds favour the index (+0.20..+0.75%, about 0.09 ms per tick), on local-5000 8 of 10
(median +0.3%). Tick p99 off/on: +0.7 / +0.8% (local-10000), +1.0 / +0.2% (local-5000). The time gained is smaller
than the 0.20 ms (10k) / 0.074 ms (5k) of static sync the LPROBE wall shows; movement reads 0.6-1.4% faster with
the index off in the same pairs (set 1).

Base vs integrated (`we_timing.sh --sitdir tools/scenarios/perf ... --rounds 4 --warm 400 --ticks 1000 --catchup 0`,
8 pairs, repeat set; the candidate also carries D/G in `sep`/`explore`, so `nav` is the phase I changes):

| situation | tick mean / median / p95 / p99 / max base -> cand | tick pair d% | nav pair d% | other pair d% |
|---|---|---|---|---|
| L-routeinv-local-10000 | 18.63 / 18.62 / 20.63 / 23.28 / 25.4 -> 18.26 / 18.27 / 20.32 / 22.90 / 25.1 | -1.9 [-2.4..-1.3] | **-2.6 [-3.0..-1.8]** | +3.2 [+2.8..+4.4] |
| L-routeinv-local-5000 | 9.52 / 9.53 / 11.12 / 12.03 / 19.5 -> 9.36 / 9.36 / 10.93 / 11.85 / 14.5 | -1.5 [-2.8..-1.2] | **-1.8 [-3.3..-1.3]** | +3.4 [+2.0..+4.4] |
| L-routeinv-scatter-10000 | 22.21 / 22.78 / 25.28 / 26.72 / 33.1 -> 22.05 / 22.67 / 25.11 / 26.61 / 28.5 | -0.6 [-2.0..+0.3] | -1.3 [-2.0..-0.6] | +10.3 [+4.2..+11.9] |
| L-routeinv-many-2500 | 9.57 / 9.94 / 13.16 / 16.25 / 18.5 -> 9.26 / 9.66 / 12.88 / 16.07 / 19.2 | -2.2 [-9.6..+1.7] | +0.0 [-7.0..+3.9] | +1.4 [-6.3..+5.8] |
| L-2h-1800 (`--sits`, 8 pairs) | 22.72 / 21.69 / 34.34 / 36.87 / 40.8 -> 22.64 / 21.59 / 34.26 / 36.46 / 40.1 | -0.3 [-1.2..+0.5] | +0.0 [-1.3..+0.6] | -0.5 [-1.1..+0.5] |
| L-bench-1900 (8 pairs) | 4.17 / 3.82 / 5.98 / 6.62 / 8.0 -> 4.14 / 3.79 / 5.95 / 6.62 / 11.0 | -1.8 [-4.0..+6.3] | -1.1 [-5.9..+8.7] | -0.5 [-4.4..+5.8] |

Ticks over 33.3 ms on L-2h: 519 -> 505 of 8,000 (Legion movement); none elsewhere. The `other` phase rises +3..+10%
on the routeinv worlds in the integrated build (not with the index toggled: there `other` is flat); it is outside
`nav` and outside I's code, and the whole tick still falls. Flagged, not attributed.

Work counters (`routeinv_pmu.sh`, serial, cores 4-7, ticks 400-900 / 1000-2000, user-mode instructions per tick;
ROUTEIDX from the correctness runs, 1000 ticks):

| workload | groups tested walk -> index (`candidates`) | nav instr base / cand-off / cand | index on vs off: nav instr | base vs cand: nav instr | index memory (peak RSS base -> cand) |
|---|---|---|---|---|---|
| local-10000 | 6,810,724 -> 66,021 | 30.32 / 30.41 / 29.70 M | -709k (-2.33%) | -629k (-2.07%) | 996 KB (167.1 -> 168.0 MB) |
| local-5000 | 3,405,174 -> 133,603 | 25.58 / 25.63 / 25.26 M | -376k (-1.47%) | -321k (-1.25%) | 854 KB |
| scatter-10000 | 5,385,557 -> 550,824 | 59.97 / 60.11 / 59.58 M | -529k (-0.88%) | -391k (-0.65%) | 3.33 MB (301.6 -> 304.8 MB) |
| many-2500 | 92,540 -> 64,054 | 33.66 / 33.70 / 33.70 M | +0.3k (0.00%) | +37k (+0.11%) | 1.10 MB |
| L-2h-1800 | | 54.56 / 54.57 / 54.57 M | -5.5k (-0.01%) | +3.9k (+0.01%) | |
| L-bench-1900 | | 27.20 / 27.21 / 27.20 M | -4.2k (-0.02%) | +2.3k (+0.01%) | |

"cand-off - base" (+37..+138k nav instructions per tick on the routeinv worlds, +6..+9k on the recordings) bounds
the bins' upkeep. On many-2500 the index saves nothing (few groups per bin) and the upkeep costs +0.11% nav
instructions, invisible in time (nav pair +0.0%).

**J. Legion dirty-rectangle clustering** (`jpair.sh` = `/home/pocket_geek/tak-tmp/tmp/pfJ/timing/pair.sh` with `BB`/`CB`
as arguments: `legion_scenario --mode legion --serial --offsets 0`, `TAK_PHASE=1 TAK_PHASE_MS=0`; 5 swapped rounds,
first 20 ticks dropped; `stats2.py`; ms per tick; rectangles per refresh from `TAK_LCLUSTER`)

| scenario (rects per refresh) | build | tick mean / median / p95 / p99 / max | swapped-round ratio cand/base: median [min..max] |
|---|---|---|---|
| clusterchurn-big (960, max 1920) | base | 10.68 / 10.53 / 11.50 / 12.15 / 20.8 | |
| | J alone | 10.01 / 9.90 / 10.76 / 11.18 / 14.6 | **0.938 [0.909..0.967]** (n=5; first set 0.940 [0.939..0.958], n=3) |
| | integrated | 9.72 / 9.69 / 10.09 / 10.26 / 18.3 | 0.921 [0.903..0.926] |
| clusterchurn-edge (90) | base / J alone | 2.278 / 2.250 / 2.78 / 3.12 / 3.7 -> 2.249 / 2.220 / 2.71 / 3.06 / 5.7 | 0.965 [0.964..1.045] (first set 0.999 [0.982..1.023]): noise |
| clusterchurn-mid (48, max 96) | base / J alone | 1.594 / 1.56 / 2.02 / 2.27 / 4.8 -> 1.594 / 1.56 / 2.02 / 2.29 / 5.9 | 0.998 [0.962..1.044]: noise |
| clusterchurn-small (8) | base / J alone | 0.466 / 0.41 / 0.90 / 1.16 / 3.4 -> 0.462 / 0.40 / 0.90 / 1.09 / 3.0 | 1.000 [0.967..1.003]: noise |

The integrated edge / mid / small ratios (0.918 / 0.884 / 0.596) are the other items (D's exploration summaries on
an idle map), not J. Worst tick per run on big: base 11.0-20.8 ms (J2 set; 11.1-11.7 in the first set), J alone 10.3-14.6,
integrated 9.9-10.5 with one run at 18.3 (a single tick); the large maxima are single ticks on both sides. Clustering cost per call (`legion_cluster_test --bench-scn`, core 0,
reference = the old pairwise loop in the same binary): big 353-363 -> 46-55 us (6.5-7.9x), edge 3.3-3.5 -> 3.6-4.5 us
(**0.74-0.98x: up to +1.2 us per call slower** just above the 64-rectangle switch; about 3 refreshes per tick, so
at most +3.5 us on a 2.25 ms tick), mid 1.10 -> 0.82 us, small 0.10 -> 0.04 us. Synthetic (`--bench`): 256 rects
4.8-7.2x, 1024 15-29x, 4096 74-119x; 16-96 rects 0.93-4.1x (n32-n64 dense 0.93-0.99x). In the candidate (`TAK_LCLUSTER`)
clustering is 1.9% of refresh time on big (55 us of 2.9 ms per refresh), 1.2% on edge. Recorded games: L-2h-1800
1,840 refreshes in 1000 ticks, at most 7 rectangles (median 2); L-bench at most 5: the bin path (> 64) never runs.

**Unexpected**

- The candidate's `other` phase reads +3..+10% on the routeinv worlds vs base (L-2h / L-bench: -0.5%), with `nav`,
  `sep` and `explore` all lower and the whole tick lower; it does not move when I's index is toggled inside one
  binary, so it is not I (likely the same layout effect the simulation results describe). Not investigated further.
- `clusterchurn-small` JSON is not byte-identical to base in the integrated build (it is for `pf-J` alone): I's
  `group_loop_iters` now counts groups visited. Hashes and every behaviour key are equal.
- I's measured whole-tick gain on local-10000 (about 0.09 ms) is about half the static-sync wall it removes
  (0.20 ms); the rest shows up as slightly slower movement in the index-on runs.

**Recommended dispositions (Legion)**

| item | recommendation | deciding numbers |
|---|---|---|
| H Legion static refresh | **Implemented and verified** (base-heavy worlds only) | sync 0.27 [0.25..0.30] (2000 structures, 8 planes), 0.47 [0.43..0.53] (3000), 0.72 [0.64..0.79] (300); H alone whole step 0.43 / 0.75 / 0.83, every pair a gain; structure tests 0.45-2.7% of the walk; below the gate (50) 1.00 and A/A 1.00. No recorded game reaches the gate (0 index builds), so ordinary games are unchanged |
| I Legion route invalidation | **Implemented and verified (small)**: the owner's condition is met on local-10000 (every one of 10 swapped rounds a whole-tick gain, +0.20..+0.75%, median +0.4%; nav 2.8% lower) and on local-5000 in 8 of 10 rounds (median +0.3%, range -0.65..+0.50); no regression on L-2h / L-bench (index on/off +0.05% / +0.10%, base vs cand -0.3% / -1.8%, nav instructions +0.01%) | groups tested 6.81M -> 66k (local-10000); staticChanged 224 -> 18 us per tick; cost: 0.85-3.3 MB of bins, +37k nav instructions per tick (+0.11%) on many-2500 where it saves nothing. If the owner wants a gain beyond the 1% resolution, it does not have one: then revert `e342a202`, `63a2a0e0`, `9dcbe1f8` |
| J Legion dirty-rect clustering | **Implemented and verified (worst-case guard)** | large churn (960 rectangles): whole tick 0.938 [0.909..0.967] J alone, p99 12.15 -> 11.18 ms; clustering 6.5-7.9x; ordinary sizes (8-90 rectangles) inside noise (0.965-1.000, ranges straddle 1); the 65-90-rectangle band costs up to +1.2 us per call (not visible in the tick); recorded games never use the bin path |

**Commands** (all from `~/tak-tmp/wt-perf-int`; `X=~/tak-tmp/tmp/pf-final-legion`; scratch `HOME`/`XDG_*` under `$X`;
drivers `$X/correct.sh` (correctness), `$X/timing.sh` (`RUN="H J I"`, then `RUN="J2 I2 PMU"`), helpers
`$X/hsum.py`, `$X/lprobe_win.py`):

```sh
tools/legion_staticsync_ab.sh --base ~/tak-tmp/build-pf-H-base-o2 --cand $CAND --rounds 5 --out $X/H -- --steps 1500   # also --cand ~/tak-tmp/build-pf-H-o2; A/A --base $CAND --cand $CAND --rounds 3
python3 $X/hsum.py $X/H
for s in big:150 edge:300 mid:300 small:600; do
  BB=$BASE CB=$CAND $T $X/jpair.sh $X/J 5 tools/scenarios/perf/clusterchurn-${s%:*}.scn ${s#*:}          # and CB=~/tak-tmp/build-pf-J-o2 -> $X/J2-alone
  python3 /home/pocket_geek/tak-tmp/tmp/pfJ/timing/stats2.py $X/J clusterchurn-${s%:*} 20
done
$T taskset -c 0 $CAND/legion_cluster_test --bench; $T taskset -c 0 $CAND/legion_cluster_test --bench-scn tools/scenarios/perf/clusterchurn-{small,mid,edge,big}.scn
ROUNDS=6 $T tools/scenarios/perf/routeinv_ab.sh $CAND $CAND $X/scn/ri-local-10000.scn 1400 $X/I2-onoff-local-10000 "" --no-route-index   # also local-5000; set 1: ROUNDS=4, 900 ticks
EXTRA="--data $D --catchup 60" ROUNDS=3 $T tools/scenarios/perf/routeinv_ab.sh $CAND $CAND $X/scn/L-2h-1800.scn 2000 $X/I-onoff-L-2h-1800 "" --no-route-index   # also L-bench-1900
python3 tools/scenarios/perf/routeinv_ab.py <out> 400; python3 $X/lprobe_win.py <out> 400
tools/we_timing.sh --base $BASE --cand $CAND --sitdir tools/scenarios/perf --sits L-routeinv-local-10000,L-routeinv-local-5000,L-routeinv-scatter-10000,L-routeinv-many-2500 --rounds 4 --warm 400 --ticks 1000 --catchup 0 --data $D --out $X/I2-we-ri
tools/we_timing.sh --base $BASE --cand $CAND --sits L-2h-1800,L-bench-1900 --rounds 4 --data $D --out $X/I-we-L
python3 tools/we_timing_dist.py <out> --warm 400 --phases tick,nav
CORES=4-7 tools/scenarios/perf/routeinv_pmu.sh $BASE $CAND $X/scn/ri-<s>.scn 900 $X/I-pmu-<s>   # recordings: EXTRA="--data $D --catchup 60", 2000 ticks, WARM 1000
```

### Results: client (C, E, F, K, measurement B)

Serial measurement, 2026-10-31 05:49 to 13:15, one agent, no other workflow running. Correctness and
verify runs 05:55-06:42, then the timing series 06:42:50-09:50:50 (load average 1.2-3.2, from the timed
client itself and the desktop; no build, test or capture ran during it), then captures 09:51-13:10 on
cores 16-23. Every timed client went through `/home/pocket_geek/tak-tmp/tools/timing.sh` (flock, cores 0-3).

**Provenance**

- Builds (Debug, `CMAKE_CXX_FLAGS=-O2 -g`, `-ffp-contract=off` from the tree, GCC 16.2.1 via ccache, mold,
  `TAK_TEST_DATA=/home/pocket_geek/TAK/assets/game`, all targets; `ninja -n` shows only the build-id restamp):
  - candidate `CAND=~/tak-tmp/build-pfint-o2`: `perf-followup` code at `dad93045` (the head `72bd6941` adds
    only docs); `takclient` sha256 `629e0f45738b9e7a...`.
  - plain base `BASE=~/tak-tmp/build-pfint-base-o2`: `461e9c4b` (= `61f294ac` code); `d8b563453bb86049...`.
  - measurement-B base `MB=~/tak-tmp/build-pf-mB-o2`: `ae7e86f0` = `461e9c4b` + measurement B's telemetry and
    render-study knobs, no PROD commit (a plain base client is refused by the interactive scenes);
    `5c71c726c84ed345...`.
  - C's A side `CA=~/tak-tmp/build-pf-C-A`: `cd9a9f1c` = `461e9c4b` + C's cursor-study instrumentation (old
    picking), built for this phase (worktree `~/tak-tmp/wt-pf-C-A`); `e3d295331c6ba137...`.
  - E's base `EB=~/tak-tmp/build-pf-E-base-o2`: `d8e63f3a` = `461e9c4b` + E's workload knob and telemetry (old
    ghost path), built for this phase (worktree `~/tak-tmp/wt-pf-E-base`); `2e58f1a6c5f532bf...`.
  - Attribution only: `~/tak-tmp/build-pf-E-o2` (`pf-E` head `f95a6155`, E alone).
- Hardware: Core Ultra 9 275HX, RTX 5070 Laptop GPU 8 GB (driver 615.78.08, OpenGL 4.6 core), Fedora 44,
  SDL offscreen video driver with the OpenGL renderer. Timed clients pinned to P-cores 0-3; the render
  harnesses have no 0-7 variant (4-7 are the build/test pool), so none was run.
- Harnesses: `tools/render_benchmark.py` (forced completion: `glFinish` before present, fixed 1/60 s frame
  clock, fixed-step local simulation; 900 frames per run, the first 300 discarded, 600 measured) and
  `tools/cursor_pick_study.py` (pipelined frames, 900 frames, 300 warm-up; each client wrapped in
  `timing.sh`). Runs are A B B A groups; a pair is (A, B) of legs 0/1 and 3/2 of each group. `--repeats 3`
  = 6 pairs (the measurement-B scenes, C at 1280x960, C whole frame, K at 7680x2160), `--repeats 2` = 4 pairs
  (C at 7680x2160, the C and K same-binary references, E, K at 1280x960), F's bench `--repeats 4` = 8 pairs.
  One discarded A/B warm-up group (240 frames) before each series.
- Statistics: per-run mean / median / p95 / p99 of the frame (FRAME `ms`, update + anim + draw + present),
  then the median over runs; max = largest frame of any run. Budget columns: share of all measured frames
  above 16.67 / 8.33 / 4.17 ms (60 / 120 / 240 Hz). Hitch = a frame above twice its run's median (meaningless
  below ~1 ms, where a 0.3 ms frame counts). Pair ratio = B/A of the per-run means, median [min..max] over the
  pairs: an item counts as a repeatable gain only when every pair is below 1. Phase columns are the
  RENDERSTATS render-thread walls (`t_*`, median of per-run means) and the FRAME phases.
- Every scene's runs gave one world hash (the harness refuses otherwise); the cursor study's per-frame
  cursor sequence was identical A vs B in every scene. The integrated candidate carries every item, so a
  whole-frame difference against an instrumented base includes the simulation items (the `update` phase,
  0.02-0.12 ms lower in these scenes) and the other client items; the per-item phase and the same-binary
  references isolate each item.

**Correctness** (not timed; cores 4-23 before the timing series, captures after it)

- `ctest -R` on the candidate: `unit_index`, `render_unit_index`, `render_unit_index_jump`, `render_pick_index`,
  `_orders`, `_jump`, `pick_late_model`, `gate_pick`, `reclaim_pick`, `cursor_enemy`, `cursor_roster`,
  `render_build_ghosts`, `hidden_units_policy`, `hidden_units`, `shadow`, `shadow_tiles`, `shadow_atlas`,
  `render_shadow_atlas` + `_zoom`, `_band`, `_extended`, `_yield`, `_full`, `_off`, `render_shadow_cache`,
  `render_interactive_cases`: 26/26 pass.
- C, `TAK_PICK_INDEX_VERIFY=1` (every cursor decision re-made by a scan over every unit; the client throws on
  a difference): the six cursor-study scenes at 1280x960 (3,600 decisions, 333k candidates for 25.1M live-unit
  visits, 1,627 body queries) and at 7680x2160 (2,400 decisions), and the four measurement-B pointer scenes at
  both sizes (6,400 decisions, 1,766 body queries): no difference.
- E, `TAK_GHOST_VERIFY=1` (retained triangles equal a fresh collection, culled ghosts' reference bounds
  outside the clip): 24 runs, 2.53M ghosts checked, 1.26M culled, `inexact=0`; `render_build_ghosts` passes.
- F, `TAK_HIDDEN_VERIFY=1` (aborts at the first unit whose cover decision, or the first frame whose fade set,
  differs from testing every cover): F's three bench workloads and fade-forest / fade-buildings /
  dots-buildings at both sizes, 18 runs: no abort.
- K, `TAK_SHADOW_CACHE_VERIFY=1` (every kept tile equals a complete re-bake), 7680x2160, 499 frames per run,
  2 runs each, identical counters run to run:

| scene | cap | pages held (max) | frames on extended pages | sheds | fallback frames | alloc failures |
|---|---|---|---|---|---|---|
| shadow-pages-dense | none | 9 | 499 | 0 | 0 | 0 |
| shadow-pages-above / -below / -osc | none | 6 / 6 / 6 (osc ends at 5) | 499 | 0 | 0 | 0 |
| shadow-pages-moving | none | 7 | 499 | 0 | 0 | 0 |
| shadow-cap-below / -above / -cross | none | 6 / 6 / 6 | 499 | 0 | 0 | 0 |
| shadow-cap-below-pan / -above-pan | none | 5 / 5 | 483 / 499 (one extended page given back) | 0 | 0 | 0 |
| shadow-pages-dense | 448 / 512 / 640 MiB | 9, then 0 | 23 / 21 / 33 | 1 | 476 / 478 / 466 | 0 |
| shadow-pages-below | 448 / 512 MiB | 6, then 4 | 20 / 30 | 1 | 0 | 0 |
| shadow-pages-below | 640 MiB | 6 | 499 | 0 | 0 | 0 |

  Under a tight cap the dense view sheds its extended pages once (in the first 21-33 frames, while the
  terrain loads) and then draws direct shadows exactly as the base does; the below view sheds back to the four
  base pages and keeps silhouettes. No re-growth, no further allocation and no repack churn after the shed
  (`regrow_gap=0`, `extended_releases=0`, `page_allocs` constant). A real texture-allocation failure cannot be
  injected in a client run (no knob); it is covered by `shadow_atlas` `testPageBudget` and
  `render_shadow_atlas_full` (pass).

**Measurement B: the interactive scenes, combined branch vs `MB`** (`render_benchmark.py --scenes <...>
--repeats 3`, 6 pairs; frame ms A then B as mean / median / p95 / p99 / max)

C scenes (pointer evaluation every frame; 10,000 own units, a corner in view; `select-large` 2,000 units all
selected; at 7680x2160 `select-large` runs as Tactical Dots):

| scene/size | A frame | B frame | % > 16.7 / 8.3 / 4.2 ms, A -> B | hitches A -> B | pair ratio mean | pair ratio p99 | `t_cursor` ms | pick tests/frame | present ms | update ms |
|---|---|---|---|---|---|---|---|---|---|---|
| hover-armed-10k/normal | 6.78 / 6.53 / 8.03 / 10.23 / 21.1 | 6.42 / 6.20 / 7.59 / 8.73 / 20.6 | 0.2 / 3.2 / 100.0 -> 0.2 / 1.5 / 100.0 | 7 -> 6 | 0.944 [0.923..0.957] | 0.904 [0.779..1.006] | 0.237 -> 0.00354 | 10,002 -> 37.8 | 0.602 -> 0.335 | 5.208 -> 5.088 |
| hover-armed-10k/uhd | 9.11 / 8.94 / 10.41 / 11.62 / 23.3 | 8.83 / 8.69 / 10.25 / 12.05 / 23.0 | 0.2 / 78.2 / 100.0 -> 0.2 / 63.7 / 100.0 | 6 -> 6 | 0.971 [0.961..0.986] | 1.028 [0.964..1.153] | 0.244 -> 0.00442 | 10,002 -> 62.8 | 0.656 -> 0.463 | 5.512 -> 5.393 |
| hover-ground-10k/normal | 7.40 / 7.30 / 8.25 / 10.14 / 19.0 | 6.98 / 6.89 / 7.77 / 9.52 / 18.5 | 0.2 / 4.1 / 100.0 -> 0.2 / 2.1 / 100.0 | 6 -> 6 | 0.945 [0.933..0.954] | 0.915 [0.869..0.976] | 0.305 -> 0.00318 | 10,002 -> 0 | 0.688 -> 0.370 | 5.533 -> 5.445 |
| hover-ground-10k/uhd | 7.98 / 7.87 / 8.89 / 11.16 / 19.4 | 7.62 / 7.52 / 8.56 / 10.83 / 19.6 | 0.2 / 19.2 / 100.0 -> 0.2 / 8.6 / 100.0 | 7 -> 6 | 0.958 [0.938..0.980] | 0.986 [0.907..1.090] | 0.306 -> 0.00319 | 10,002 -> 0 | 0.616 -> 0.313 | 5.504 -> 5.460 |
| hover-units-10k/normal | 6.66 / 6.41 / 7.89 / 8.53 / 20.7 | 6.39 / 6.15 / 7.56 / 8.69 / 20.5 | 0.2 / 1.8 / 100.0 -> 0.2 / 1.5 / 100.0 | 7 -> 6 | 0.961 [0.940..0.969] | 0.979 [0.883..1.181] | 0.245 -> 0.00528 | 10,002 -> 37.8 | 0.557 -> 0.341 | 5.142 -> 5.086 |
| hover-units-10k/uhd | 8.94 / 8.71 / 10.30 / 12.21 / 23.7 | 8.59 / 8.46 / 9.92 / 12.00 / 22.7 | 0.2 / 70.4 / 100.0 -> 0.2 / 54.3 / 100.0 | 6 -> 6 | 0.964 [0.946..0.993] | 0.964 [0.862..1.114] | 0.243 -> 0.00599 | 10,002 -> 62.8 | 0.679 -> 0.473 | 5.401 -> 5.346 |
| select-large/normal | 5.40 / 5.38 / 6.20 / 7.05 / 9.8 | 5.20 / 5.20 / 6.01 / 6.81 / 10.3 | 0.0 / 0.3 / 100.0 -> 0.0 / 0.2 / 100.0 | 0 -> 0 | 0.961 [0.953..0.978] | 0.957 [0.918..0.995] | 0.0614 -> 0.00864 | 2,002 -> 79 | 0.314 -> 0.251 | 0.965 -> 0.938 |
| select-large/uhd | 1.87 / 1.86 / 2.11 / 2.31 / 4.2 | 1.80 / 1.79 / 2.02 / 2.18 / 4.4 | 0.0 / 0.0 / 0.0 -> 0.0 / 0.0 / 0.1 | 7 -> 7 | 0.972 [0.907..1.019] | 0.941 [0.860..1.076] | 0.0443 -> 0.00536 | 2,002 -> 6.87 | 0.460 -> 0.407 | 0.688 -> 0.659 |

The cursor phase falls 0.24-0.31 ms -> 0.003-0.006 ms (10,002 -> 0-63 exact tests) in the 10k scenes and
0.044-0.061 -> 0.005-0.009 ms (2,002 -> 7-79) in `select-large`; the `present` phase (it holds the cursor
overlay) falls by the same amount. Every pair is a whole-frame gain in every scene except `select-large`
at 7680x2160 (0.972 [0.907..1.019], a 1.8 ms frame). At 120 Hz the share of frames over budget falls from
70 / 78 / 19% to 54 / 64 / 9% in the 7680x2160 hover scenes. p99 ranges straddle 1 except hover-ground-10k
(0.915 [0.869..0.976]) and select-large (0.957 [0.918..0.995]) at 1280x960.

E scenes (16 builders x 24 queued builds, 3 repeated types, half of each queue at the far map corner):

| scene/size | A frame | B frame | % > 16.7 / 8.3 / 4.2 ms, A -> B | hitches A -> B | pair ratio mean | pair ratio p99 | `t_ghosts` ms | ghosts culled | retained-geometry hits |
|---|---|---|---|---|---|---|---|---|---|
| ghosts-queue-pan/normal | 4.49 / 4.41 / 4.86 / 5.23 / 8.4 | 1.01 / 0.97 / 1.23 / 1.55 / 4.2 | 0.0 / 0.0 / 100.0 -> 0.0 / 0.0 / 0.0 | 0 -> 14 | 0.224 [0.214..0.229] | 0.300 [0.269..0.345] | 4.06 -> 0.654 | - -> 167 | - -> 149 |
| ghosts-queue-pan/uhd | 5.24 / 5.19 / 5.53 / 5.75 / 9.0 | 1.49 / 1.45 / 1.68 / 1.84 / 4.9 | 0.0 / 0.2 / 100.0 -> 0.0 / 0.0 / 0.2 | 0 -> 16 | 0.285 [0.282..0.294] | 0.322 [0.304..0.508] | 4.42 -> 0.74 | - -> 173 | - -> 163 |
| ghosts-queue/normal | 4.36 / 4.32 / 4.53 / 4.71 / 7.7 | 0.99 / 0.95 / 1.13 / 1.33 / 4.1 | 0.0 / 0.0 / 98.7 -> 0.0 / 0.0 / 0.0 | 0 -> 12 | 0.226 [0.223..0.231] | 0.280 [0.268..0.290] | 4.03 -> 0.73 | - -> 150 | - -> 166 |
| ghosts-queue/uhd | 5.03 / 4.97 / 5.28 / 5.69 / 8.6 | 1.46 / 1.42 / 1.76 / 2.19 / 4.5 | 0.0 / 0.1 / 100.0 -> 0.0 / 0.0 / 0.1 | 0 -> 12 | 0.286 [0.272..0.295] | 0.387 [0.343..0.391] | 4.28 -> 0.734 | - -> 170 | - -> 166 |

`ghosts_queued` = `ghost_preps` = 316 (1280x960) / 336 (7680x2160) on both sides; the candidate's retained
geometry is 46,080 bytes. The "hitches" on B are frames above twice a 1 ms median.

F scenes (Fade unless noted; fade clock on the study frame):

| scene/size | A frame | B frame | % > 16.7 / 8.3 / 4.2 ms, A -> B | hitches A -> B | pair ratio mean | pair ratio p99 | `t_hidden_units` ms | cover tests/frame | update ms |
|---|---|---|---|---|---|---|---|---|---|
| dots-buildings/normal | 1.66 / 1.73 / 2.22 / 2.51 / 4.1 | 1.62 / 1.69 / 2.20 / 2.42 / 4.2 | 0.0 / 0.0 / 0.0 -> 0.0 / 0.0 / 0.0 | 8 -> 10 | 0.983 [0.963..1.007] | 0.981 [0.922..1.082] | 0.0803 -> 0.0772 | 0 -> 0 | 0.386 -> 0.367 |
| dots-buildings/uhd | 2.48 / 2.55 / 3.17 / 3.47 / 10.4 | 2.44 / 2.51 / 3.14 / 3.48 / 4.5 | 0.0 / 0.0 / 0.3 -> 0.0 / 0.0 / 0.3 | 1 -> 0 | 0.982 [0.972..0.995] | 1.009 [0.968..1.080] | 0.137 -> 0.133 | 0 -> 0 | 0.420 -> 0.394 |
| fade-buildings/normal | 1.71 / 1.78 / 2.18 / 2.36 / 3.9 | 1.68 / 1.75 / 2.16 / 2.40 / 4.0 | 0.0 / 0.0 / 0.0 -> 0.0 / 0.0 / 0.0 | 8 -> 4 | 0.975 [0.940..1.073] | 0.982 [0.883..1.289] | 0.0905 -> 0.0838 | 297 -> 250 | 0.384 -> 0.363 |
| fade-buildings/uhd | 2.52 / 2.57 / 3.05 / 3.28 / 4.8 | 2.48 / 2.51 / 3.01 / 3.19 / 4.6 | 0.0 / 0.0 / 0.2 -> 0.0 / 0.0 / 0.2 | 0 -> 0 | 0.984 [0.981..0.987] | 0.987 [0.954..1.002] | 0.152 -> 0.143 | 443 -> 384 | 0.406 -> 0.382 |
| fade-forest/normal | 1.72 / 1.69 / 2.04 / 2.29 / 4.1 | 1.67 / 1.63 / 2.03 / 2.42 / 5.4 | 0.0 / 0.0 / 0.0 -> 0.0 / 0.0 / 0.0 | 12 -> 13 | 0.977 [0.950..0.992] | 1.043 [0.925..1.149] | 0.109 -> 0.09 | 692 -> 579 | 0.563 -> 0.535 |
| fade-forest/uhd | 2.57 / 2.54 / 2.85 / 3.02 / 4.8 | 2.50 / 2.48 / 2.77 / 3.01 / 4.9 | 0.0 / 0.0 / 0.2 -> 0.0 / 0.0 / 0.2 | 0 -> 0 | 0.977 [0.966..0.991] | 0.993 [0.955..1.046] | 0.159 -> 0.137 | 790 -> 676 | 0.605 -> 0.573 |

F's pass is 5-18% cheaper here (0.007-0.022 ms; every pair below 1 except fade-buildings and
dots-buildings at 1280x960), 2-3% in dots-buildings (Dots tests no cover). The whole-frame ratios (0.975-0.984)
split between the simulation items (`update` -0.02..-0.03 ms) and F (hidden pass -0.003..-0.022 ms).

K scenes (7680x2160 only; the 6-type roster 40 px apart at zoom 1):

| scene/size | A frame | B frame | % > 16.7 / 8.3 / 4.2 ms, A -> B | hitches A -> B | pair ratio mean | pair ratio p99 | direct-shadow units | atlas pages | atlas GPU bytes (max) | tracked GPU bytes (max) |
|---|---|---|---|---|---|---|---|---|---|---|
| shadow-cap-above-pan/uhd | 30.99 / 31.29 / 33.67 / 34.93 / 39.0 | 30.15 / 29.53 / 34.65 / 36.12 / 39.4 | 100.0 / 100.0 / 100.0 -> 100.0 / 100.0 / 100.0 | 0 -> 0 | 0.973 [0.966..0.981] | 1.031 [1.023..1.047] | 3.29e+03 -> 0 | 1.02 -> 4.75 | 64 MiB -> 80 MiB | 262 MiB -> 278 MiB |
| shadow-cap-above/uhd | 25.59 / 25.47 / 27.88 / 28.80 / 33.7 | 12.23 / 12.29 / 14.61 / 15.44 / 26.1 | 100.0 / 100.0 / 100.0 -> 0.4 / 100.0 / 100.0 | 0 -> 3 | 0.478 [0.477..0.480] | 0.534 [0.524..0.547] | 4.46e+03 -> 0 | 0 -> 5.93 | 0 MiB -> 96 MiB | 288 MiB -> 384 MiB |
| shadow-cap-below-pan/uhd | 25.40 / 26.19 / 28.65 / 29.97 / 33.0 | 25.20 / 25.96 / 28.39 / 29.85 / 33.4 | 100.0 / 100.0 / 100.0 -> 100.0 / 100.0 / 100.0 | 0 -> 0 | 0.993 [0.966..1.013] | 0.994 [0.970..1.027] | 53.2 -> 0 | 3.95 -> 4.01 | 64 MiB -> 80 MiB | 262 MiB -> 278 MiB |
| shadow-cap-below/uhd | 14.07 / 11.75 / 21.99 / 24.08 / 29.8 | 10.28 / 10.34 / 12.18 / 13.04 / 16.5 | 39.0 / 100.0 / 100.0 -> 0.0 / 100.0 / 100.0 | 68 -> 0 | 0.729 [0.724..0.735] | 0.543 [0.534..0.560] | 0 -> 0 | 4 -> 6 | 64 MiB -> 96 MiB | 339 MiB -> 371 MiB |
| shadow-cap-cross/uhd | 29.55 / 29.37 / 31.27 / 33.23 / 40.7 | 28.90 / 29.00 / 31.56 / 33.28 / 39.9 | 100.0 / 100.0 / 100.0 -> 100.0 / 100.0 / 100.0 | 0 -> 0 | 0.979 [0.968..0.986] | 1.006 [0.981..1.044] | 2.31e+03 -> 0 | 1.75 -> 4.56 | 64 MiB -> 96 MiB | 262 MiB -> 295 MiB |

Above capacity (`shadow-cap-above`) the frame halves; just below it (`-below`, the base repacks the full
four pages most frames: 1,401 relocated tiles per frame, 0.47 compactions) the mean falls 27% and p99 46%,
and 16.7 ms misses go from 39% of frames to none. Under the sinusoidal pan every tile is redrawn every frame on
both sides; `-above-pan` (base: 75% of frames direct) is 2.7% faster on the mean but **3.1% slower at p99
in every pair** (1.031 [1.023..1.047]), `-cross` 2.1% faster on the mean, p99 inside noise; `-below-pan` inside
noise. The extra GPU memory is the atlas: +16 to +32 MiB where the base kept its four pages, +96 MiB in
`-above` (the base held none there).

**C: cursor picking** (`cursor_pick_study.py`; A = `CA` (old picking, same study hooks), B = `CAND`; cursor
evaluation = `desiredCursor` wall per frame; ms as mean / median / p95 / p99 / max)

1280x960, 6 pairs:

| scene | exact tests/frame A -> B | cursor A | cursor B | cursor pair ratio | saved ms/frame | frame mean A -> B (p99) | frame pair ratio |
|---|---|---|---|---|---|---|---|
| subset10k-sel | 10002 -> 57.3 | 0.3332 / 0.3044 / 0.3833 / 0.4956 / 0.97 | 0.0134 / 0.0112 / 0.0197 / 0.0216 / 0.15 | 0.040 [0.040..0.041] | 0.320 | 7.28 -> 6.94 (p99 8.25 -> 7.88) | 0.952 [0.944..0.983] |
| subset10k-nosel | 4304 -> 9.6 | 0.1368 / 0.0180 / 0.3178 / 0.4076 / 0.51 | 0.0054 / 0.0056 / 0.0074 / 0.0082 / 0.14 | 0.039 [0.034..0.040] | 0.131 | 6.89 -> 6.70 (p99 7.89 -> 7.59) | 0.977 [0.963..0.990] |
| subset10k-attack | 10002 -> 57.3 | 0.3312 / 0.3026 / 0.3798 / 0.4667 / 0.81 | 0.0139 / 0.0118 / 0.0199 / 0.0221 / 0.15 | 0.042 [0.041..0.043] | 0.317 | 7.20 -> 6.84 (p99 8.12 -> 7.76) | 0.948 [0.936..0.959] |
| visible10k-sel | 10002 -> 179.0 | 0.4697 / 0.4665 / 0.5541 / 0.5832 / 0.74 | 0.0193 / 0.0193 / 0.0248 / 0.0286 / 0.10 | 0.041 [0.039..0.043] | 0.450 | 21.98 -> 20.88 (p99 25.54 -> 24.67) | 0.951 [0.946..0.966] |
| small-battle-sel | 102 -> 21.7 | 0.0082 / 0.0063 / 0.0138 / 0.0171 / 0.13 | 0.0055 / 0.0042 / 0.0093 / 0.0139 / 0.09 | 0.668 [0.658..0.685] | 0.003 | 0.99 -> 0.97 (p99 1.58 -> 1.58) | 0.976 [0.968..0.988] |
| effects-pan-sel | 1479 -> 162.8 | 0.0693 / 0.0687 / 0.0802 / 0.0920 / 0.23 | 0.0192 / 0.0141 / 0.0396 / 0.0448 / 0.16 | 0.277 [0.268..0.283] | 0.050 | 10.87 -> 10.69 (p99 12.92 -> 12.60) | 0.983 [0.978..0.985] |

7680x2160, 4 pairs:

| scene | exact tests/frame A -> B | cursor A | cursor B | cursor pair ratio | saved ms/frame | frame mean A -> B (p99) | frame pair ratio |
|---|---|---|---|---|---|---|---|
| subset10k-sel | 10002 -> 109.4 | 0.4790 / 0.4717 / 0.5919 / 0.6418 / 0.83 | 0.0153 / 0.0154 / 0.0200 / 0.0238 / 0.09 | 0.033 [0.031..0.034] | 0.464 | 31.58 -> 18.37 (p99 35.47 -> 22.46) | 0.585 [0.574..0.588] |
| subset10k-nosel | 4347 -> 35.5 | 0.1878 / 0.1240 / 0.5006 / 0.5421 / 0.74 | 0.0083 / 0.0086 / 0.0122 / 0.0135 / 0.02 | 0.044 [0.043..0.048] | 0.180 | 30.92 -> 18.09 (p99 34.99 -> 22.13) | 0.584 [0.581..0.589] |
| subset10k-attack | 10002 -> 109.4 | 0.4511 / 0.4438 / 0.5538 / 0.6098 / 0.77 | 0.0158 / 0.0158 / 0.0203 / 0.0240 / 0.10 | 0.035 [0.034..0.036] | 0.435 | 31.45 -> 18.34 (p99 35.47 -> 22.39) | 0.583 [0.581..0.591] |
| visible10k-sel | 10002 -> 97.9 | 0.2587 / 0.2321 / 0.2987 / 0.4029 / 0.43 | 0.0129 / 0.0118 / 0.0217 / 0.0241 / 0.14 | 0.050 [0.049..0.051] | 0.246 | 8.14 -> 7.82 (p99 8.98 -> 8.80) | 0.961 [0.951..0.980] |
| small-battle-sel | 102 -> 15.5 | 0.0090 / 0.0071 / 0.0133 / 0.0153 / 0.09 | 0.0055 / 0.0054 / 0.0080 / 0.0095 / 0.09 | 0.609 [0.606..0.632] | 0.004 | 1.27 -> 1.24 (p99 1.83 -> 1.81) | 0.978 [0.969..0.992] |
| effects-pan-sel | 1479 -> 6.5 | 0.0845 / 0.0837 / 0.0947 / 0.1128 / 0.21 | 0.0153 / 0.0151 / 0.0171 / 0.0231 / 0.05 | 0.182 [0.174..0.192] | 0.069 | 12.85 -> 12.68 (p99 15.03 -> 15.05) | 0.987 [0.984..0.990] |

The 7680x2160 subset10k frames (31.5 -> 18.4 ms) are K's shadow atlas, not C: C saves 0.18-0.46 ms of them.

Same binary, `TAK_PICK_FULL_SCAN=1` on A (isolates C from everything else), 1280x960, 4 pairs:

| scene | exact tests/frame A -> B | cursor A | cursor B | cursor pair ratio | saved ms/frame | frame mean A -> B (p99) | frame pair ratio |
|---|---|---|---|---|---|---|---|
| subset10k-sel | 10002 -> 57.3 | 0.3396 / 0.3125 / 0.3885 / 0.4291 / 0.80 | 0.0133 / 0.0111 / 0.0193 / 0.0216 / 0.16 | 0.039 [0.039..0.040] | 0.326 | 7.19 -> 6.85 (p99 8.35 -> 7.89) | 0.947 [0.938..0.958] |
| subset10k-nosel | 4304 -> 9.6 | 0.1388 / 0.0188 / 0.3224 / 0.3667 / 0.46 | 0.0054 / 0.0057 / 0.0076 / 0.0082 / 0.12 | 0.039 [0.038..0.040] | 0.133 | 6.93 -> 6.79 (p99 8.07 -> 7.86) | 0.979 [0.967..0.994] |
| subset10k-attack | 10002 -> 57.3 | 0.3444 / 0.3117 / 0.4155 / 0.5115 / 3.57 | 0.0141 / 0.0118 / 0.0200 / 0.0224 / 0.15 | 0.041 [0.040..0.042] | 0.331 | 7.23 -> 6.81 (p99 8.17 -> 7.70) | 0.941 [0.928..0.950] |
| visible10k-sel | 10002 -> 179.0 | 0.4794 / 0.4756 / 0.5583 / 0.5906 / 1.87 | 0.0193 / 0.0191 / 0.0241 / 0.0282 / 0.10 | 0.041 [0.036..0.042] | 0.460 | 21.32 -> 20.97 (p99 24.79 -> 24.56) | 0.985 [0.979..0.993] |
| small-battle-sel | 102 -> 21.7 | 0.0085 / 0.0064 / 0.0141 / 0.0184 / 0.10 | 0.0055 / 0.0042 / 0.0094 / 0.0142 / 0.07 | 0.646 [0.638..0.653] | 0.003 | 0.97 -> 0.96 (p99 1.56 -> 1.47) | 0.991 [0.988..0.994] |
| effects-pan-sel | 1479 -> 162.8 | 0.0713 / 0.0695 / 0.0831 / 0.1487 / 0.23 | 0.0191 / 0.0142 / 0.0396 / 0.0445 / 0.16 | 0.269 [0.261..0.272] | 0.052 | 10.62 -> 10.54 (p99 12.59 -> 12.46) | 0.993 [0.989..0.998] |

Whole frame with the in-game cursor on (`render_benchmark.py --scenes total10k-subset effects --env
TAK_RENDER_CURSOR=1 --env TAK_RENDER_CURSOR_SELECT=20`, A = `CA`, 6 pairs):

| scene/size | A frame | B frame | % > 16.7 / 8.3 / 4.2 ms, A -> B | hitches A -> B | pair ratio mean | pair ratio p99 | present ms | update ms |
|---|---|---|---|---|---|---|---|---|
| effects/normal | 3.53 / 3.49 / 4.15 / 4.63 / 5.8 | 3.48 / 3.43 / 4.12 / 4.64 / 6.2 | 0.0 / 0.0 / 5.1 -> 0.0 / 0.0 / 3.9 | 0 -> 0 | 0.987 [0.944..1.006] | 1.012 [0.880..1.056] | 0.178 -> 0.172 | 0.730 -> 0.695 |
| effects/uhd | 3.98 / 3.94 / 4.58 / 5.29 / 6.9 | 3.94 / 3.88 / 4.55 / 5.31 / 6.8 | 0.0 / 0.0 / 25.2 -> 0.0 / 0.0 / 20.3 | 0 -> 0 | 0.988 [0.982..0.990] | 0.997 [0.975..1.024] | 0.205 -> 0.197 | 0.729 -> 0.692 |
| total10k-subset/normal | 7.48 / 7.43 / 8.17 / 8.60 / 19.2 | 7.09 / 7.06 / 7.76 / 8.40 / 19.0 | 0.2 / 2.6 / 100.0 -> 0.2 / 1.2 / 100.0 | 6 -> 6 | 0.948 [0.931..0.951] | 0.965 [0.935..0.988] | 0.695 -> 0.369 | 5.509 -> 5.465 |
| total10k-subset/uhd | 8.29 / 8.20 / 8.98 / 10.74 / 20.2 | 7.88 / 7.81 / 8.56 / 9.26 / 19.7 | 0.2 / 38.7 / 100.0 -> 0.2 / 12.9 / 100.0 | 6 -> 6 | 0.949 [0.936..0.961] | 0.872 [0.835..1.028] | 0.660 -> 0.283 | 5.667 -> 5.622 |

C is a repeatable gain wherever many units are live: cursor evaluation 0.033-0.050x of A at 10,000 units
(0.13-0.46 ms per frame saved), 0.18-0.28x in the 2,000-unit battle with flyers (0.05-0.07 ms), 0.61-0.67x
in the 200-unit battle (0.003-0.004 ms, below any frame effect). With full scan toggled in one binary the
whole frame is 1.5-5.9% faster in the 10k scenes in every pair; in the two small scenes the frame ratio is
0.991-0.993 (inside noise). With the in-game cursor in the matrix scenes: total10k-subset 0.948 / 0.949 (every pair, `present` -0.33 /
-0.38 ms), `effects` (450 units in view) 0.987 / 0.988, every pair only at 7680x2160.

**E: build-queue ghosts** (`render_benchmark.py --scenes ghost-queues ghost-offscreen ghost-visible ghost-few
--repeats 2`, A = `EB`, B = `CAND`, 4 pairs)

| scene/size | A frame | B frame | % > 16.7 / 8.3 / 4.2 ms, A -> B | hitches A -> B | pair ratio mean | pair ratio p99 | `t_build_ghosts` ms | ghosts culled | retained hits |
|---|---|---|---|---|---|---|---|---|---|
| ghost-few/normal | 1.54 / 1.50 / 1.79 / 1.95 / 3.5 | 1.28 / 1.25 / 1.55 / 1.73 / 3.6 | 0.0 / 0.0 / 0.0 -> 0.0 / 0.0 / 0.0 | 4 -> 4 | 0.830 [0.806..0.857] | 0.879 [0.819..0.918] | 0.246 -> 0.0228 | 0 -> 15 | 0 -> 12 |
| ghost-few/uhd | 2.04 / 2.01 / 2.29 / 2.48 / 4.8 | 2.00 / 1.98 / 2.27 / 2.43 / 5.2 | 0.0 / 0.0 / 0.1 -> 0.0 / 0.0 / 0.1 | 2 -> 3 | 0.984 [0.947..1.000] | 0.986 [0.958..1.015] | 0.252 -> 0.0503 | 0 -> 0 | 0 -> 27 |
| ghost-offscreen/normal | 2.59 / 2.57 / 2.80 / 2.89 / 3.3 | 0.17 / 0.14 / 0.33 / 0.43 / 1.4 | 0.0 / 0.0 / 0.0 -> 0.0 / 0.0 / 0.0 | 0 -> 271 | 0.066 [0.065..0.068] | 0.147 [0.129..0.157] | 2.21 -> 0.00683 | 0 -> 249 | 0 -> 0 |
| ghost-offscreen/uhd | 3.27 / 3.28 / 3.40 / 3.59 / 6.3 | 0.86 / 0.83 / 0.97 / 1.18 / 4.1 | 0.0 / 0.0 / 0.2 -> 0.0 / 0.0 / 0.0 | 0 -> 5 | 0.264 [0.246..0.279] | 0.325 [0.268..0.375] | 2.23 -> 0.00668 | 0 -> 249 | 0 -> 0 |
| ghost-queues/normal | 3.63 / 3.62 / 3.78 / 3.99 / 6.6 | 0.56 / 0.54 / 0.66 / 0.78 / 3.6 | 0.0 / 0.0 / 0.4 -> 0.0 / 0.0 / 0.0 | 0 -> 7 | 0.155 [0.154..0.156] | 0.198 [0.183..0.212] | 3.03 -> 0.211 | 0 -> 218 | 0 -> 110 |
| ghost-queues/uhd | 3.74 / 3.72 / 3.95 / 4.10 / 6.9 | 1.15 / 1.13 / 1.31 / 1.56 / 4.2 | 0.0 / 0.0 / 0.8 -> 0.0 / 0.0 / 0.0 | 0 -> 4 | 0.308 [0.290..0.325] | 0.381 [0.371..0.434] | 2.97 -> 0.42 | 0 -> 104 | 0 -> 224 |
| ghost-visible/normal | 3.97 / 3.96 / 4.11 / 4.34 / 7.1 | 1.33 / 1.31 / 1.44 / 1.62 / 3.9 | 0.0 / 0.0 / 2.7 -> 0.0 / 0.0 / 0.0 | 0 -> 4 | 0.334 [0.331..0.340] | 0.374 [0.361..0.387] | 2.99 -> 0.498 | 0 -> 64 | 0 -> 264 |
| ghost-visible/uhd | 3.77 / 3.75 / 3.97 / 4.07 / 9.8 | 1.27 / 1.24 / 1.42 / 1.74 / 4.4 | 0.0 / 0.0 / 0.5 -> 0.0 / 0.0 / 0.1 | 1 -> 4 | 0.336 [0.327..0.339] | 0.427 [0.406..0.436] | 3 -> 0.573 | 0 -> 20 | 0 -> 308 |

The ghost pass falls 2.2-3.0 ms -> 0.007-0.57 ms in the long-queue scenes: off-screen ghosts (all 249 in
`ghost-offscreen`) are culled before any triangle work and visible ones reuse the sorted model-local
triangles (collections 328 -> 0 per frame). `ghost-few` (200 units, 27 ghosts) gains 17% at 1280x960 (every
pair) and 1.6% at 7680x2160 (0.984 [0.947..1.000]: one pair at 1.000, so not established there). Retained
memory: 25,600 bytes in these scenes (46,080 in measurement B's).

**F: hidden-unit Fade** (`tools/hidden_fade_bench.sh --repeats 4`, A = `BASE`, B = `CAND`, 8 pairs, 1280x960)

| workload | A frame | B frame | % > 16.7 / 8.3 / 4.2 ms, A -> B | hitches A -> B | pair ratio mean | pair ratio p99 | `t_hidden_units` ms | covers handed out (= base's exact tests) | exact tests run | update ms |
|---|---|---|---|---|---|---|---|---|---|---|
| fade-forest (300) | 0.91 / 0.91 / 1.13 / 1.31 / 4.7 | 0.88 / 0.87 / 1.10 / 1.33 / 4.6 | 0.0 / 0.0 / 0.0 -> 0.0 / 0.0 / 0.0 | 8 -> 9 | 0.960 [0.939..0.984] | 0.989 [0.919..1.084] | 0.0896 -> 0.0527 | - -> 1.06e+03 | - -> 365 | 0.123 -> 0.117 |
| fade-forest1200 | 2.90 / 2.92 / 3.27 / 3.55 / 6.7 | 2.56 / 2.59 / 2.95 / 3.26 / 8.1 | 0.0 / 0.0 / 0.6 -> 0.0 / 0.0 / 0.4 | 1 -> 2 | 0.888 [0.878..0.898] | 0.916 [0.764..0.965] | 0.478 -> 0.194 | - -> 8.12e+03 | - -> 1.24e+03 | 0.462 -> 0.440 |
| fade-keeps | 2.07 / 1.85 / 3.00 / 3.16 / 5.7 | 1.94 / 1.73 / 2.85 / 3.06 / 4.7 | 0.0 / 0.0 / 0.2 -> 0.0 / 0.0 / 0.2 | 17 -> 18 | 0.934 [0.908..0.949] | 0.969 [0.906..0.991] | 0.221 -> 0.141 | - -> 2.92e+03 | - -> 1.57e+03 | 0.381 -> 0.352 |

(Rows: fade-forest = `forest` 300 units; fade-forest1200 = 1,200 units 24 px apart under repeated trees;
fade-keeps = 1,000 units with every eighth a Keep.) The pass is 0.58x / 0.41x / 0.64x of base (0.04 / 0.28 /
0.08 ms per frame); the whole frame is a gain in every pair (0.888-0.960), most of it the hidden pass (the
`update` phase moves 0.006-0.03 ms).

**K: shadow atlas** (`render_benchmark.py`, A = `BASE`, B = `CAND`; 7680x2160 6 pairs, 1280x960 4 pairs)

7680x2160 (`shadow-pages-*`, zoom 0.82):

| scene/size | A frame | B frame | % > 16.7 / 8.3 / 4.2 ms, A -> B | hitches A -> B | pair ratio mean | pair ratio p99 | direct-shadow units | atlas pages | atlas GPU bytes (max) | tracked GPU bytes (max) | relocated tiles/frame |
|---|---|---|---|---|---|---|---|---|---|---|---|
| shadow-pages-above/uhd | 36.91 / 36.75 / 40.12 / 41.46 / 47.5 | 18.47 / 18.71 / 21.90 / 23.42 / 27.2 | 100.0 / 100.0 / 100.0 -> 71.6 / 100.0 / 100.0 | 0 -> 0 | 0.502 [0.497..0.503] | 0.566 [0.555..0.568] | 6.6e+03 -> 0 | 0 -> 6.01 | 0 MiB -> 112 MiB | 391 MiB -> 486 MiB | 0 -> 0 |
| shadow-pages-below/uhd | 21.72 / 17.40 / 33.05 / 35.46 / 43.3 | 15.17 / 15.38 / 18.26 / 19.42 / 22.6 | 52.0 / 100.0 / 100.0 -> 15.3 / 100.0 / 100.0 | 74 -> 0 | 0.698 [0.695..0.700] | 0.548 [0.531..0.563] | 0 -> 0 | 4 -> 6 | 64 MiB -> 96 MiB | 443 MiB -> 475 MiB | 2.33e+03 -> 0 |
| shadow-pages-constrained/uhd | 58.63 / 58.90 / 61.58 / 66.41 / 77.9 | 58.72 / 58.95 / 61.69 / 65.28 / 77.8 | 100.0 / 100.0 / 100.0 -> 100.0 / 100.0 / 100.0 | 0 -> 0 | 1.006 [0.992..1.011] | 0.980 [0.941..1.012] | 9.78e+03 -> 9.78e+03 | 0 -> 0 | 0 -> 0 | 327 MiB -> 327 MiB | 0 -> 0 |
| shadow-pages-dense/uhd | 54.48 / 54.42 / 58.94 / 60.49 / 72.3 | 29.10 / 29.11 / 34.04 / 35.23 / 46.2 | 100.0 / 100.0 / 100.0 -> 100.0 / 100.0 / 100.0 | 0 -> 0 | 0.536 [0.530..0.539] | 0.583 [0.574..0.595] | 9.78e+03 -> 0 | 0 -> 9 | 0 MiB -> 144 MiB | 391 MiB -> 535 MiB | 0 -> 0 |
| shadow-pages-moving/uhd | 41.74 / 42.04 / 52.83 / 55.96 / 63.9 | 35.55 / 34.71 / 48.27 / 50.86 / 57.0 | 100.0 / 100.0 / 100.0 -> 100.0 / 100.0 / 100.0 | 0 -> 0 | 0.853 [0.850..0.859] | 0.903 [0.885..0.928] | 2.43e+03 -> 0 | 2.2 -> 6.01 | 64 MiB -> 112 MiB | 490 MiB -> 538 MiB | 820 -> 7.18 |
| shadow-pages-osc/uhd | 35.09 / 35.12 / 37.70 / 40.31 / 50.1 | 34.56 / 34.61 / 37.67 / 39.68 / 46.7 | 100.0 / 100.0 / 100.0 -> 100.0 / 100.0 / 100.0 | 0 -> 0 | 0.992 [0.980..0.997] | 0.986 [0.964..0.993] | 2.5e+03 -> 0 | 1.97 -> 4.51 | 64 MiB -> 96 MiB | 344 MiB -> 376 MiB | 2.33e+03 -> 4.72e+03 |
| shadow-pages-pan/uhd | 55.72 / 55.97 / 59.34 / 60.56 / 68.2 | 55.23 / 54.78 / 60.53 / 63.26 / 67.7 | 100.0 / 100.0 / 100.0 -> 100.0 / 100.0 / 100.0 | 0 -> 0 | 0.996 [0.985..1.010] | 1.040 [1.026..1.056] | 7.58e+03 -> 0 | 0 -> 5.64 | 0 MiB -> 96 MiB | 247 MiB -> 342 MiB | 0 -> 7.16e+03 |

Same binary, `TAK_SHADOW_PAGES_MAX=4` on A (the candidate held to four pages = what `c3c312eb`, `0397c088`
and `192f7c4b` alone give, i.e. without the visual commit `98e01695`), 4 pairs:

| scene/size | A frame | B frame | % > 16.7 / 8.3 / 4.2 ms, A -> B | hitches A -> B | pair ratio mean | pair ratio p99 | atlas pages | relocated tiles/frame |
|---|---|---|---|---|---|---|---|---|
| shadow-pages-above/uhd | 36.51 / 36.38 / 39.73 / 41.17 / 45.1 | 18.54 / 18.79 / 21.84 / 23.15 / 27.1 | 100.0 / 100.0 / 100.0 -> 72.3 / 100.0 / 100.0 | 0 -> 0 | 0.507 [0.506..0.510] | 0.565 [0.556..0.567] | 0 -> 6.01 | 0 -> 0 |
| shadow-pages-below/uhd | 21.33 / 16.92 / 32.81 / 35.15 / 38.8 | 15.36 / 15.56 / 18.49 / 19.69 / 22.8 | 50.7 / 100.0 / 100.0 -> 19.0 / 100.0 / 100.0 | 64 -> 0 | 0.720 [0.716..0.726] | 0.562 [0.544..0.567] | 4 -> 6 | 2.33e+03 -> 0 |
| shadow-pages-constrained/uhd | 58.23 / 58.45 / 61.12 / 65.62 / 76.5 | 58.38 / 58.58 / 61.53 / 64.28 / 77.6 | 100.0 / 100.0 / 100.0 -> 100.0 / 100.0 / 100.0 | 0 -> 0 | 1.001 [0.992..1.012] | 0.977 [0.955..1.002] | 0 -> 0 | 0 -> 0 |
| shadow-pages-dense/uhd | 53.78 / 53.66 / 58.22 / 59.53 / 70.2 | 29.33 / 29.30 / 34.03 / 35.51 / 46.4 | 100.0 / 100.0 / 100.0 -> 100.0 / 100.0 / 100.0 | 0 -> 0 | 0.545 [0.538..0.555] | 0.597 [0.593..0.609] | 0 -> 9 | 0 -> 0 |
| shadow-pages-moving/uhd | 41.34 / 41.71 / 52.42 / 55.33 / 61.3 | 35.56 / 34.39 / 48.22 / 50.94 / 54.6 | 100.0 / 100.0 / 100.0 -> 100.0 / 100.0 / 100.0 | 0 -> 0 | 0.859 [0.858..0.861] | 0.926 [0.886..0.934] | 2.2 -> 6.01 | 820 -> 7.18 |
| shadow-pages-osc/uhd | 34.60 / 34.71 / 37.27 / 39.61 / 49.0 | 34.69 / 34.66 / 37.78 / 40.12 / 48.6 | 100.0 / 100.0 / 100.0 -> 100.0 / 100.0 / 100.0 | 0 -> 0 | 1.003 [0.981..1.005] | 1.011 [0.997..1.028] | 1.97 -> 4.51 | 2.33e+03 -> 4.72e+03 |
| shadow-pages-pan/uhd | 54.89 / 55.17 / 58.66 / 60.27 / 68.3 | 55.51 / 55.03 / 60.99 / 63.37 / 69.5 | 100.0 / 100.0 / 100.0 -> 100.0 / 100.0 / 100.0 | 0 -> 0 | 1.010 [1.009..1.012] | 1.050 [1.037..1.077] | 0 -> 5.64 | 0 -> 7.16e+03 |

1280x960 controls (no scene there needs more than two pages; `shadow_extended_pages` stays 0):

| scene/size | A frame | B frame | % > 16.7 / 8.3 / 4.2 ms, A -> B | hitches A -> B | pair ratio mean | pair ratio p99 | atlas pages | extended pages | `t_hidden_units` ms | update ms |
|---|---|---|---|---|---|---|---|---|---|---|
| churn/normal | 13.42 / 12.90 / 17.27 / 19.01 / 37.0 | 13.18 / 12.63 / 17.23 / 18.90 / 35.7 | 8.2 / 100.0 / 100.0 -> 8.2 / 100.0 / 100.0 | 8 -> 8 | 0.986 [0.974..0.994] | 0.994 [0.921..1.044] | 1.5 -> 1.5 | - -> 0 | 0.834 -> 0.803 | 2.886 -> 2.791 |
| moving/normal | 7.75 / 7.77 / 12.16 / 13.44 / 15.6 | 7.66 / 7.70 / 11.93 / 13.11 / 16.6 | 0.0 / 33.5 / 98.2 -> 0.0 / 31.0 / 97.8 | 0 -> 2 | 0.988 [0.984..0.995] | 0.983 [0.950..1.015] | 1 -> 1 | - -> 0 | 0.262 -> 0.256 | 1.567 -> 1.490 |
| pan-snap/normal | 5.70 / 5.66 / 6.26 / 6.67 / 10.5 | 5.55 / 5.53 / 6.09 / 6.63 / 10.3 | 0.0 / 0.2 / 100.0 -> 0.0 / 0.1 / 100.0 | 0 -> 0 | 0.976 [0.959..0.987] | 0.990 [0.949..1.064] | 2 -> 2 | - -> 0 | 0.497 -> 0.454 | 1.167 -> 1.122 |
| stationary-mixed/normal | 4.46 / 4.47 / 5.10 / 5.45 / 8.1 | 4.18 / 4.21 / 4.70 / 5.02 / 6.9 | 0.0 / 0.0 / 72.3 -> 0.0 / 0.0 / 54.7 | 0 -> 0 | 0.940 [0.883..0.976] | 0.915 [0.879..0.980] | 1 -> 1 | - -> 0 | 0.378 -> 0.257 | 0.992 -> 0.931 |
| total10k-visible/normal | 22.05 / 22.10 / 24.57 / 25.90 / 51.7 | 21.28 / 21.39 / 23.71 / 25.42 / 52.0 | 100.0 / 100.0 / 100.0 -> 100.0 / 100.0 / 100.0 | 4 -> 4 | 0.968 [0.959..0.971] | 0.976 [0.971..0.985] | 1 -> 1 | - -> 0 | 2.16 -> 1.55 | 6.284 -> 6.185 |
| zoom/normal | 10.81 / 11.58 / 12.90 / 15.75 / 18.3 | 10.69 / 11.42 / 12.82 / 15.48 / 17.7 | 0.4 / 86.2 / 100.0 -> 0.3 / 85.8 / 100.0 | 0 -> 0 | 0.990 [0.983..0.994] | 0.983 [0.952..0.997] | 1.14 -> 1.14 | - -> 0 | 1.28 -> 1.16 | 1.247 -> 1.204 |

- With four pages the candidate times like the base in every scene (the four-page A side is within 2% of
  `BASE`, measured in separate series): the whole gain is the visual commit `98e01695`.
- Gains, every pair: dense 0.54 (p99 0.58), above 0.50 (p99 0.57), below 0.70 (p99 0.55; 16.7 ms misses 52%
  -> 15%), moving 0.85 (p99 0.90; base fell back to direct shadows on 45% of frames).
- Neutral: constrained (448 MiB cap; pages shed, direct shadows as base) 1.006 [0.992..1.011]; osc 0.992
  [0.980..0.997] vs base but 1.003 [0.981..1.005] against the same binary at four pages.
- **Regression under the continuous sub-pixel pan:** `shadow-pages-pan` mean 0.996 [0.985..1.010] vs base,
  1.010 [1.009..1.012] same-binary; p99 1.040 [1.026..1.056] vs base and 1.050 [1.037..1.077] same-binary,
  every pair above 1 (+2.5-3.1 ms on a 60 ms p99). Here every tile is re-rasterized every frame (7,160
  relocated tiles per frame) and the shadow phases cost 1.53x the direct path; the cheaper composite does not
  pay for it. measurement B's `shadow-cap-above-pan` shows the same p99 (+3.1%, every pair).
- Memory: tracked GPU memory +32 to +144 MiB in the views that keep extended pages (atlas 96-144 MiB vs 0-64
  MiB); peak client RSS within +-60 MiB except the pan (+156 MiB). No pending-page growth: pending pages exist
  only inside a frame (no texture), `page_allocs` stays at the held count.
- 1280x960: every control is 0.94-0.99 (the hidden pass, the cursor-free sim phases); the shadow phases are
  inside noise (0.976-1.019), as expected with no extended page.

**Captures** (`--capture-only`, cores 16-23, 12 frames per scene for measurement B and K, 28 for E;
world-view pixels; A/A = two runs of one build)

- **C** (measurement B pointer scenes, `MB` vs `CAND`): 0 px in every frame of 7 of 8 scene/sizes;
  `select-large` at 1280x960 76-177 px (max channel error 1-2) in 1-3 frames, the A/A level of either build
  (76-207 px), also with distant images off. C also passes the stricter checks above (identical cursor
  sequences, `TAK_PICK_INDEX_VERIFY`).
- **E**: with the wall-clock-driven content off (`TAK_HIDDEN_UNITS=off TAK_DISTANT_MODELS_OFF=1
  TAK_TERRAIN_SETTLE=1`; 28 frames per scene) `EB` vs `CAND` is 0 px in every frame of ghost-queues,
  ghost-visible and ghost-offscreen at both sizes and ghost-few at 1280x960; ghost-few at 7680x2160 differs by
  55-130 px in 16 frames, as do A/A pairs of `EB` and of `CAND` and E alone (`build-pf-E-o2`) vs `EB` (same
  frames, same size). Same binary, the reference path (`TAK_GHOST_REFERENCE=1`: collect and sort every frame,
  no culling) vs the retained path: 0 px in ghost-queues and ghost-visible at both sizes, every model type
  ghosted on, across and off the view (`TAK_GHOST_ALL_TYPES`) 0 px, ghost-few at 1280x960 0 px in three
  repeats (both orders and a reference A/A; one earlier run differed by 23-127 px in 13 frames and did not
  repeat). measurement B's ghosts-queue: 0 px at 1280x960; at 7680x2160 one frame (720) differs by 3.6-5.6k
  px in a 100x110 px box around a swaying tree, where the candidate's own A/A also differs (3.2-4.8k).
  Without the exact environment the A/B differences exceed A/A (ghost-visible 1280x960 2.8-6.1k px vs at most
  1.6k): HIDDEN UNITS fades and distant-image bakes run on wall time and the candidate renders faster.
  **E is pixel-identical.**
- **F**: `hidden_fade_bench.sh --capture` `BASE` vs `CAND`: 0 px in every frame of all three workloads (A/A
  0). measurement B's fade-forest 0 px at both sizes; fade-buildings and dots-buildings show a two-state
  run-to-run difference (1.5-3k px around the towers, max error 46-86) that both builds produce: every
  candidate capture is 0 px against one of the base captures. **F is pixel-identical.**
- **K**, 7680x2160, `TAK_TERRAIN_SETTLE=1` (frame 480; world view 7475x2088 = 15.6M px):

| comparison | differing px | of view | max channel error | A/A (same frame) |
|---|---|---|---|---|
| shadow-pages-dense, `BASE` vs `CAND` | 997,708 | 6.39% | 144 | 4,285 (max 32) |
| shadow-pages-above, `BASE` vs `CAND` | 804,305 | 5.15% | 141 | 1,992 (max 13) |
| shadow-pages-dense, exact environment | 748,356 | 4.79% | 144 | 0 (cand), 3,396 (base) |
| shadow-pages-below (base 4 pages, cand 6), exact environment | 907 | 0.006% | 53 | 666 |
| shadow-pages-moving, `BASE` vs `CAND` (worst frame: base on direct shadows) | 1,526,580 | 9.8% | - | 4,078 |
| fidelity control: `BASE` with `TAK_SHADOW_PAGE_MAX=4096` (silhouettes there too) vs `CAND`, dense / above | 4,332 / 3,356 | 0.028 / 0.022% | 95 / 91 | 4,285 / 1,992 (max 32 / 13) |
| `CAND` held to 4 pages vs `BASE`, exact environment, dense / above | 2,528 / 1,952 | 0.016 / 0.013% | 15 / 7 | 0-3,396 / 1,827 |
| `pf-K` held to 4 pages vs `BASE`, exact environment, dense | 0 (all 12 frames) | 0 | 0 | 3,396 (base) |

  In the overflow views 99.9% of the differing pixels are lighter in the candidate (997,454 of 997,708 in
  dense; 30% exactly one shadow level): the base's whole-frame direct fallback darkened overlapping triangles
  inside a model twice, the atlas silhouette darkens once. measurement B's shadow-cap-above / -cross /
  -above-pan show the same (0.60-0.88M px, max error 124-133) and shadow-cap-below 8-287 px (max 27), -below-pan
  0.9-2.8k px of 1-2 LSB, within its A/A. Against silhouettes in the base (the 4096 control) the candidate
  differs by A/A-level counts with a few hundred edge pixels up to error 95 (atlas placement and filtering).
  Held to four pages the candidate equals the base (K alone: 0 px; the integrated build: inside the base's
  own A/A). At 1280x960 (no extended page) `BASE` vs `CAND` with the exact environment: 0 px in pan-snap,
  moving and churn, zoom 379 px = A/A 384; with the default environment the differences are fade timing.
  **The only visual change is `98e01695`'s, in views that need more than four pages.**

**Unexpected**

- The candidate's `t_ghosts` reads about 0.0002 ms in scenes with no ghost where `MB` reads 0.00001 ms: at
  integration one scope feeds both `t_ghosts` and `t_build_ghosts` and it now encloses E's per-frame setup
  (0.2 us per frame; not visible in any frame).
- `present` at 7680x2160 is higher on the candidate in some K views (`shadow-pages-pan` 0.31 -> 3.49 ms,
  `-osc` 1.14 -> 2.35 ms): with `glFinish` before present the GPU's atlas work is waited for there instead of
  inside draw; the frame total is what is compared.
- C's study at 7680x2160 shows the subset10k frame at 31.5 -> 18.4 ms: K's atlas (the base falls back to
  direct shadows in that view), not cursor picking.
- Capture A/A noise sources seen here (not investigated beyond attribution): a two-state run-to-run render
  around the towers in fade-buildings / dots-buildings (both builds), a swaying tree in ghosts-queue at
  7680x2160 (candidate runs differ from each other at frame 720), 55-130 px in ghost-few at 7680x2160 (both
  builds), and 3.4k px between two base runs of shadow-pages-dense even with the exact environment.
- `shadow-pages-below` (sized to fit about 95% of four pages) holds six pages on the candidate: the first
  frames after the spawn draw larger silhouettes (about 1.3x the settled area), the atlas grows past four
  pages and keeps them while demand stays near the limit. That is also where its gain comes from (base: 2,332
  relocated tiles and 0.46 compactions per frame, candidate: 0), at +32 MiB.
- Under a 448-640 MiB cap the dense view creates its extended pages during the first 21-33 frames and sheds
  them once (pending pages count as held, so the shed happens at the first frame over 7/8 of the cap); after
  that it behaves exactly like the base, with no further allocation.

**Recommended dispositions (client)**

| item | recommendation | deciding numbers |
|---|---|---|
| Measurement B: interactive render cases | **Implemented and verified** (instrumentation) | all 14 scenes ran at both sizes with their expectations checked by the harness in every run (pointer, selection, armed command, ghost counts, covers, fades, demand band); they expose C, E, F and K above with ranges outside noise. Its own cost was not re-measured here (screening: about 0.001 ms) |
| C Cursor picking | **Implemented and verified** | cursor evaluation 0.033-0.050x of the old scan at 10,000 units (0.13-0.46 ms per frame), 0.18-0.28x at 2,000 units with flyers; same-binary whole frame 0.941-0.985 in every pair of the 10k scenes; measurement B 10k pointer scenes 0.944-0.971 (every pair), 120 Hz misses at 7680x2160 70 -> 54% / 78 -> 64%; 200-unit battle inside noise (0.991). Decisions identical (verify, cursor sequences), pixels identical |
| E Build-queue ghosts | **Implemented and verified** | long queues: frame 0.066-0.336x (`ghost-offscreen` 2.59 -> 0.17 ms, `ghost-queues` 3.63 -> 0.56 ms at 1280x960), measurement B ghosts-queue 0.22-0.29x, every pair; ghost pass 2.2-3.0 -> 0.007-0.57 ms; ghost-few 0.830 at 1280x960, 0.984 [0.947..1.000] at 7680x2160 (not established there); retained memory 25-46 KB; pixel-identical (exact-environment captures, reference path, every model type) |
| F Hidden-unit Fade covers | **Implemented and verified** | pass 0.58 / 0.41 / 0.64x (forest / forest1200 / keeps), frame 0.960 / 0.888 / 0.934, every one of 8 pairs below 1; measurement B Fade scenes pass 0.82-0.95x (Dots 0.97-0.98x), whole frame 0.975-0.984 (half of it the simulation items); pixel-identical, verify clean |
| K Shadow atlas capacity | **Owner's decision on `98e01695`.** If accepted: **Implemented and verified**, with a documented regression under continuous sub-pixel pans. If not accepted: **Measured and rejected** for all of K (revert `c3c312eb`, `0397c088`, `192f7c4b` too), because held to four pages K measures exactly as the base | gains only with more than four pages: dense 0.536, above 0.502, below 0.698 (p99 0.548, 16.7 ms misses 52 -> 15%), moving 0.853, measurement B above 0.478 / below 0.729, every pair; held to four pages 1.00 (same binary) and 0 px (K alone). Costs: `shadow-pages-pan` p99 +4.0% vs base / +5.0% same-binary (mean +1.0% same-binary, every pair), shadow-cap-above-pan p99 +3.1% every pair; +32..+144 MiB tracked GPU memory in views that keep extended pages. Visual: 5-6% of the view lighter in views that overflowed four pages (direct path's double darkening gone); otherwise A/A-level. Constrained VRAM: sheds once, then identical to base (1.006 [0.992..1.011]); 1280x960 unaffected |

**Commands** (from `~/tak-tmp/wt-perf-int`; `X=~/tak-tmp/tmp/pf-final/client`; drivers outside the repo:
`~/tak-tmp/pf-final-client/timing-run.sh` (every timing series, warm-up group first), `correct-run.sh`
(verify runs and captures, `STAGES=...`), `capfew.sh`; summaries `summ.py rb|cs <dir>`, `table.py`,
`ctable.py`, `capcmp.py <AB dir> <AA dirs>`, K's `~/tak-tmp/pf-K/capdiff.py`):

```sh
T=/home/pocket_geek/tak-tmp/tools/timing.sh; TD=~/tak_data; D=/home/pocket_geek/TAK/assets/game
B="python3 tools/render_benchmark.py --cpus 0-3"
$T $B --a $MB/takclient --b $CAND/takclient --data $TD --repeats 3 --scenes hover-ground-10k hover-units-10k hover-armed-10k select-large --sizes normal uhd --output $X/mB-c
$T $B --a $MB/takclient --b $CAND/takclient --data $TD --repeats 3 --scenes ghosts-queue ghosts-queue-pan --sizes normal uhd --output $X/mB-e
$T $B --a $MB/takclient --b $CAND/takclient --data $TD --repeats 3 --scenes fade-forest fade-buildings dots-buildings --sizes normal uhd --output $X/mB-f
$T $B --a $MB/takclient --b $CAND/takclient --data $TD --repeats 3 --scenes shadow-cap-below shadow-cap-above shadow-cap-cross shadow-cap-below-pan shadow-cap-above-pan --sizes uhd --output $X/mB-k
python3 tools/cursor_pick_study.py --a $CA/takclient --b $CAND/takclient --data $D --output $X/C --scenes subset10k-sel subset10k-nosel subset10k-attack visible10k-sel small-battle-sel effects-pan-sel --frames 900 --warmup 300 --repeats 3 --wrap $T
#   the same with --repeats 2 --width 7680 --height 2160 (-> $X/C-uhd); and --a $CAND/takclient --a-env TAK_PICK_FULL_SCAN=1 --repeats 2 (-> $X/C-ref)
$T $B --a $CA/takclient --b $CAND/takclient --data $D --repeats 3 --scenes total10k-subset effects --sizes normal uhd --env TAK_RENDER_CURSOR=1 --env TAK_RENDER_CURSOR_SELECT=20 --output $X/C-frame
$T $B --a $EB/takclient --b $CAND/takclient --data $D --repeats 2 --frames 900 --warmup 300 --scenes ghost-queues ghost-offscreen ghost-visible ghost-few --sizes normal uhd --output $X/E
$T tools/hidden_fade_bench.sh --a $BASE/takclient --b $CAND/takclient --data $TD --output $X/F --repeats 4 --frames 900 --warmup 300 --cpus 0-3
$T $B --a $BASE/takclient --b $CAND/takclient --data $TD --repeats 3 --sizes uhd --scenes shadow-pages-dense shadow-pages-above shadow-pages-below shadow-pages-moving shadow-pages-pan shadow-pages-osc shadow-pages-constrained --output $X/K
$T $B --a $BASE/takclient --b $CAND/takclient --data $TD --repeats 2 --sizes normal --scenes stationary-mixed pan-snap moving total10k-visible zoom churn --output $X/K-normal
$T $B --a $CAND/takclient --b $CAND/takclient --a-env TAK_SHADOW_PAGES_MAX=4 --data $TD --repeats 2 --sizes uhd --scenes <the seven shadow-pages-*> --output $X/K-ref
# correctness (cores 16-23): cursor_pick_study / render_benchmark with --env TAK_PICK_INDEX_VERIFY=1, TAK_GHOST_VERIFY=1,
#   TAK_HIDDEN_VERIFY=1, TAK_SHADOW_CACHE_VERIFY=1 [--env TAK_GPU_CAP_MIB=448|512|640] --allow-unverified-env
# captures: render_benchmark --capture-only --cpus 16-23 (A/B and A/A), K with --env TAK_TERRAIN_SETTLE=1 --frames 730 --warmup 100;
#   exact identity: --env TAK_HIDDEN_UNITS=off --env TAK_DISTANT_MODELS_OFF=1 --env TAK_TERRAIN_SETTLE=1
python3 ~/tak-tmp/pf-final-client/summ.py rb $X/<series>; python3 ~/tak-tmp/pf-final-client/summ.py cs $X/C
```

## Progress log

- 2026-10-30: branch created at `61f294ac`; plan written. Next: launch the
  workstream branches.
- 2026-10-30: fourteen item branches developed and reviewed (none unsound); all merged
  into `perf-followup` in the fixed order with four integration commits (scene lists,
  scenario location, a harness name clash). Combined branch: detmath and determinism
  golden unchanged, the 7 `--mpai` gates identical twice, full ctest green apart from
  the known local `vashim_unavailable`, the 145-scenario baseline PASS unchanged, the
  campaign smoke identical to base, and the full identity harness (see Integration).
  Next: the serial measurement phase, then final dispositions (I conditional, K's
  `98e01695` for the owner).
- 2026-10-31: serial final measurement, simulation (A, D, G, L, measurement A, combined workloads):
  results and recommended dispositions under "Results: simulation". Every sim item shows a repeatable gain
  on its workload; the integrated branch is a whole-tick gain on all four WE recordings (R-2h replay -1.9%,
  L-2h -0.4%). Pass 1 ran under other workflows' load and is kept only as a sign check; pass 2 ran quiet.
  New workload `tools/scenarios/perf/aurafight.scn` (aura-heavy fight).
- 2026-10-31: serial final measurement, AI (B): results under "Results: AI". Exact everywhere (bench
  streams, `ai_behavior`, recorded commands of the 7 gates and an 8-AI Benchmark game); per call 0.013-0.41x
  of base in every pair; in real games the raise path never fires and the AI-phase change is inside noise.
- 2026-10-31: serial final measurement, Legion (H, I, J): results under "Results: Legion". H: static sync
  0.27-0.47x at 2000-3000 structures, 0.72x at 300, unchanged below the gate and in recorded games. I: the
  conditional keep is met (local-10000 every swapped round +0.2..+0.75% whole tick with the index toggled in one
  binary, local-5000 8 of 10, L-2h / L-bench unchanged); recommended keep, small. J: large churn whole tick 0.94x,
  ordinary sizes inside noise.
- 2026-10-31: serial final measurement, client (C, E, F, K, measurement B): results under "Results: client".
  C, E and F are repeatable gains in every pair of their workloads and pixel-identical (C: cursor evaluation
  0.033-0.050x at 10,000 units, whole frame 0.94-0.985 same-binary; E: long-queue frames 0.07-0.34x; F: pass
  0.41-0.64x, frame 0.89-0.96). K gains only through the visual commit `98e01695` (dense / above / below / moving
  at 7680x2160 0.50-0.85x) and costs +4-5% p99 under a continuous sub-pixel pan and +32..144 MiB GPU memory;
  held to four pages it equals the base, so without the owner's acceptance all of K is Measured and rejected.
