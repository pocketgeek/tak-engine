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
