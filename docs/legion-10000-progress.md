# Legion 10,000: plan, shared interfaces and progress record

The resumable record of the Legion 10,000 program: navigation for about 10,000 individually simulated units
moving at once, built by extending Legion (`src/sim/legion.{h,cpp}`), with Retail unchanged. The brief is
the user's `LEGION-10000-CLAUDE-PROMPT.md` (copy: `~/tak-tmp/briefs/`); this file is the plan, the shared
interfaces every stage builds on, the per-stage records, and the next step. Read it first when resuming.

* Branch `task-legion-10000` (worktree `~/tak-tmp/wt/legion-10000`), from `origin/main` **c2c25707**
  (v0.8.3 validation; protocol `kNetVersion` 266).
* Status words: **implemented and validated**, **implemented, unvalidated**, **not implemented**.
* The whole program is **not complete** until stage 9's measured before/after report exists.

## 1. Stages

The user's nine stages map onto the brief's phases 0-8. Each stage lands as its own commits on the
branch, pushed after the stage (`git push origin task-legion-10000`; never main, never forced).

| Stage | Brief phase | Content | Kind | Status |
|---|---|---|---|---|
| 1 | 0 | Representative baselines and acceptance tests: scale suite, real-path delivery probe, frozen targets | tools/tests/docs only | implemented and validated (this record, section 6) |
| 2 | 1 | Indexed `registerMove` group matching; swept-bounds broad phase for `awareScan`; profiled micro-work | exact (hash-identical) | implemented and validated (section 7) |
| 3 | 2 | Explicit logical orders: client intent identity, bounded selection-order transaction, server sequencing, activation tick, every identity consumer | behaviour + protocol + replay | not implemented |
| 4 | 3 | Deterministic fair scheduling (classes, deficit round robin, aging, unique jobs), complete work budgets, byte accounting | behaviour (schedule) | not implemented |
| 5 | 4 | Route-query interface; consumers migrated; Unknown / Reachable / Unreachable kept apart; metric versions | exact first, then behaviour where stated | not implemented |
| 6 | 5 | Hierarchy over the footprint planes (sectors, regions, portals), sparse tiles, intermediate sharing, search vs field by demand | behaviour | not implemented |
| 7 | 6 | Static navigation apart from dynamic traffic; local dirty tiles; closures vs improvements | behaviour | not implemented |
| 8 | 7 | Navigation subgroups, shared chokepoint state and admission, multi-entrance mixed-footprint arrivals, local engagement | behaviour | not implemented |
| 9 | 8 | Parallel tile builds / per-plane preprocessing with deterministic publication; reassess movement; final 10k report | exact (parallel) + report | not implemented |

Per stage, the record (section 7) gives: hypothesis, touched code, exact-versus-behaviour classification,
exact commands and results, baseline comparison, work/memory change, commits, inherited versus new failures,
open issues. A behaviour or wire change bumps `kNetVersion` once for the stage (`replay_test` must refuse the
previous protocol) and records new `--mpai` baselines; Retail `--mpai` and Retail scenario hashes must stay
equal to the starting revision's unless a protocol-only field forces otherwise, and then the reason is written
here.

### Order of work and dependencies

* Stage 2 is independent of the rest and exact; it lands first (identity harness, `legion_check --cumulative`
  hash-equal).
* Stage 3 changes the wire and the sim's order identity. Stages 4-8 key on its identities, so it lands before
  them. Its transport part (client/server) and its sim part (activation, identity consumers) are separable
  sub-steps under one protocol bump.
* Stage 5 (the query interface) is the seam stage 6 needs: storage cannot change while consumers read
  `Field::potential` directly. Stage 5's migration is exact; anything that is not (for example reading a
  paused build's settled region where `done` was required) is split out and classified.
* Stage 6 and 7 share the tile/portal identities fixed in section 3.4; stage 7 can start once stage 6's
  connectivity layer is in, before its sparse tiles are.
* Stage 8 needs stage 3 (one logical order = one destination crowd) and stage 6 (portals as chokepoint keys).
* Stage 9 parallelises only work that stages 4-6 made into independent jobs with immutable inputs.

### Rules every stage follows (from the brief and the repo)

* Every movement goes through `commitGroundStep`; no position is written to resolve congestion.
* Missing cache data never retires an order; Unknown is never Unreachable.
* No simulation decision reads wall time, worker completion order, camera, free memory or a profiling counter.
  Stats stay observation-only; adaptive policy uses modelled, hashed state.
* Exact steps are proved with `tools/legion_identity.sh` and hash-equal `legion_check`; behaviour steps never
  silently retake a baseline (each licensed key gets its reason in `baseline.json`).
* Builds: `build-o2` (Debug `-O2`) for tests and harnesses, Release `build` for release checks; after a
  `src/sim`, `src/net` or `src/ai` change rebuild ALL targets. Heavy work on cores 4-23, timing on 0-3.

## 2. Assessment of the starting revision (c2c25707)

What the brief lists, against the code at the start (read from `legion.{h,cpp}`, `convoy.{h,cpp}`,
`sim.cpp`'s hooks, `net/{protocol,lockstep}.h`, `client/gameview_net.cpp`, `server/server.cpp`, the tools).
"Present" items are not to be rebuilt.

### Present (keep)

* **Footprint-origin legality planes** per mobility class (`Plane`: `legal`, `comp`, exact rectangle
  clearance by run lengths; `legion_clearance` test). At most 24 planes, LRU by use.
* **Incremental plane repair** (`refreshPlane`, clustered windows `refreshCluster`, local split proof
  `splitSearch`, `relabel`); background first builds (`prebuildStep`, staged, `kPrebuildCells` a tick).
  Labels are compared through `Group::compCell`, never stored raw.
* **Fields**: one integer field per group, A* toward the bodies with a consistent octile heuristic run to
  exhaustion (exact distances), 32-bit potentials (`kUnreached`), bounded windows (`fieldWindow`), widened when
  a member is uncovered. One 384k-relaxation quota a tick (`kFieldQuota`), a quarter for refreshes.
* **Shared fields** (`sharedField` over `routeIndex`): only for equal seeds, equal soft rule, window holding
  the asker's, started within one soft scan period. No sharing of intermediate route segments.
* **Demand-paused first builds** (C1, `Field::covered`, `demand`, `known()`): solo groups only (no shared
  goals, no pinwheel parts), never reach kinds. `demand` is a fixed 32-cell list that drops the oldest.
* **Scheduler indexes** (`buildingIds`, `waitedIds`, `needFieldIds`, `staleDoneIds`, `byBuilt`) checked under
  `TAK_LEGION_VERIFY`.
* **Spatial occupancy**: the soft-body grid (`soft`, striped `scanStill`, `softHash`), World's own occupancy.
* **Formation slots** (`Point`, `assignFormation`, hand-out budget `kHandOutCells`), per-goal packed slots,
  **reach rings** (`buildRing`), **passage behaviour** (gate, passage lanes, pinwheel, head-on meets),
  settle rule, lift areas for flyers, approach members for unreachable goals.
* **Deterministic checksums** (`checksum()`, folded in `World::stateHash` in mode 4) and the instruments:
  the shared observer `tools/legion_observe.h`, `legion_scenario` (+ `baseline.json`, `legion_check.py`),
  `legion_cost`, `legion_identity.sh`, crowdbench, the nightly (`tools/legion_nightly.sh`).

### Partial

* **Order identity is inferred, not explicit.** `net::Command` is one unit, one verb; the client sends one
  command per selected unit (`gameview_hud.cpp`, mirrored by `tools/legion_issue_selection.h`). The sim
  infers the click with `ConvoyTable` (`sim/convoy.h`) from point proximity and timing: a convoy stays open at
  most `kSpanTicks` = 32 ticks after its first order and `kGapTicks` = 9 after its last. At 64 commands a tick,
  33 ticks carry at most about 2,100 orders, so **a 10,000-unit click becomes several convoys** (several
  points, several formations, several destination crowds; the probe below saw 20 Legion groups for one click).
* **Command delivery is per unit and rate-limited.** `kCmdCapPerTick` = 64 per client per tick, server queue
  `kCmdQueueCap` = 512 (excess discarded), client window by acks. One 10,000-unit Move needs at least 157
  ticks; with the uplink window over a 16-tick round trip the last command lands 308 ticks (10.3 s) after the
  click (section 6). A Stop issued behind it waits in the same FIFO.
* **Budgets count field relaxations only.** Plane debt is charged after synchronous work (`planeDebt`);
  `registerMove`, `awareScan`, ring/slot searches (the slot hand-out has its own `kHandOutCells`), pruning
  and destination assignment are unbudgeted. When `startField` cannot fit a group the scheduler `break`s:
  every later admission that tick waits behind the first that does not fit (head-of-line).
* **Memory accounting** is an observation estimate (`Stats::bytes`, capacities); policy caps are cell counts
  (48 whole maps, 1,024 fields). No deterministic byte accounting of queues, scratch, metadata.
* **Group partitioning**: by player, plane, kind, issue/convoy, component, 16-cell goal clustering and the
  256-seed cap, in registration order; nothing spatial about the bodies (`split-450-open`).

### Missing

* Explicit logical orders (identity from client intent; transaction; activation tick; arrival-area,
  subgroup and route identities as separate things).
* Indexed group matching: `registerMove` scans every group (`joinIterations`); `awareScan` pairs every group
  with every mover every 30 ticks. (Done in stage 2: join index, awareness broad phase.)
* A route-query interface: steering, pinwheel, awareness, packing and rings read `Field::at` /
  `known` / `done` directly from the monolithic window.
* Any hierarchy (sectors, regions, portals), sparse tiles, intermediate-route sharing, search-versus-field
  choice by demand. Every route is a field over a window of the map.
* Static/dynamic separation: soft bodies and mover corridors are costs inside the same field build, so a
  traffic change is a field rebuild.
* Navigation subgroups, shared chokepoint traffic state and admission, multi-entrance arrival.
* Parallel planning: all Legion work runs on the sim thread inside the serial unit loop / `tick()`.

### Instruments' blind spots relevant here

* The `.scn` suite tops out around 1,000 bodies (`battle-field-2x500`); crowdbench at 2,000 with per-unit
  goals; `--mpai` Inner Circle moves a handful of units. Before this stage nothing measured 10,000 movers,
  command latency, first movement, queue age, t99, or memory per workload.
* `.scn` has no production, economy or AI; factory exits and logistics are covered only by `--mpai`, the
  Ulasem benchmark and the remote sweep's stress runs.

## 3. Shared interfaces (fixed by the lead)

These are the contracts later stages implement. A stage may refine a field or add one with a recorded reason,
but must not invent a parallel concept. Sketches are C++-shaped; names are binding, layouts are not.

### 3.1 Logical-order identity (stage 3)

```cpp
// sim/order_id.h (new), included by net and sim.
struct OrderId {            // identity of one player intent; hashed wherever stored
    uint8_t player = 0;
    uint32_t serial = 0;    // client-assigned, strictly increasing per player per game (0 = none)
    auto operator<=>(const OrderId&) const = default;
};
struct ArrivalAreaId {      // one shared destination area of an order (a queued leg is its own area)
    OrderId order; uint16_t leg = 0;
};
struct NavSubgroupId { uint32_t value = 0; };   // navigator-allocated, deterministic, never a packet chunk
struct RouteId { uint64_t key = 0; uint32_t planeGen = 0; };  // a route/tile cache identity (3.4)
```

* `Order` gains `OrderId order` (and its leg index). `convoyTick` stays as the compatibility key for
  commands that carry no `OrderId` (AI, scripts, old per-unit commands); everything Legion keys on a click
  (points, groups, soft-arrival ownership, formations, flight stations, patrol legs, awareness `command`,
  cancellation, hashing, observers) moves to `OrderId` when present, falling back to `(player, convoyTick)`.
* Wire (stage 3 owns the details; the shape is fixed): a bounded **selection-order transaction**
  `OrderBegin{OrderId, verb, queue, point/target, shape, memberCount}`, one or more
  `OrderMembers{OrderId, chunk, unit ids[, per-unit offsets for non-shared members]}`, `OrderCommit{OrderId,
  memberCount, membersDigest}` or `OrderAbort{OrderId}`. Chunks never form separate formations or crowds.
  The server validates framing, limits (members per order, open orders and bytes per sender) and sequence,
  and gives a committed order one **activation tick**; ownership/existence checks run in the sim, where that
  state is, deterministically, at activation. Ordinary commands are never starved behind a transaction.
* Limits are constants in `net/protocol.h` next to `kCmdCapPerTick`.

### 3.2 Work and byte accounting (stage 4)

```cpp
// sim/navbudget.h (new). Deterministic units only: cells touched, relaxations, members scanned.
enum class NavWork : uint8_t { FieldRelax, FieldInit, PlaneBuild, PlaneRelabel, Registration, Awareness,
                               SlotSearch, RingBuild, Connectivity, TileBuild, RouteSearch, Count };
enum class NavClass : uint8_t { Waiting, FirstRoute, AwareRepair, StaleRefresh, Count };  // service classes
struct NavJobKey { uint8_t kind; uint32_t planeGen; uint64_t inputKey; };   // ONE job per key, shared
struct NavJob { NavJobKey key; NavClass cls; uint32_t enqueued; uint64_t estimate, deficit; };
struct NavBudget { uint64_t perTick[size_t(NavClass::Count)]; uint64_t maintenance; };  // constants
struct NavBytes {           // element counts x fixed element sizes: platform-independent, hashed when a
    uint64_t planes, fields, tiles, portals, queues, scratch, metadata, inflight;   // policy reads it
};
```

* The scheduler serves jobs, not groups: several groups sharing a field share its job and its allocation.
* Within a class: bounded slices, deterministic deficit round robin, age-based promotion; a job that does not
  fit does not stop the scan (bounded look-ahead, aging guarantees the big job's turn).
* `NavBytes` drives cache admission/eviction; heap/allocator overhead is measured separately (the scale
  suite's `rss_peak_kb` / `heap_kb`, stderr only) and never read by the sim.

### 3.3 Route-query interface (stage 4/5 seam, stage 5 owns it)

```cpp
// sim/legion_route.h (new). Consumers state what they need; the provider may be a field window today,
// sparse tiles tomorrow.
enum class Reach : uint8_t { Unknown, Reachable, Unreachable };   // Unknown never retires an order
struct Progress { uint32_t value; uint16_t metric; };              // compare only within one metric
class RouteView {
public:
    Reach reach(int cell) const;                         // proven, from settled data only
    bool covered(int cell, uint32_t margin) const;       // neighbourhood settled with margin
    std::optional<Progress> progress(int cell) const;    // nullopt: not settled here
    int descend(int cell, int goal, int fx, int fz) const;   // next legal step, -1 none known
    uint64_t geometryEpoch() const;                      // freshness, separate from reach
};
// Demand is per subscriber (member id), deduplicated, unbounded in count but bounded in work per tick;
// a full list never silently drops a subscriber (the current 32-cell `demand` does).
```

* Metric transitions re-baseline progress explicitly (`Progress::metric`); a tile-local zero never means
  arrival; changing metric never manufactures headway.

### 3.4 Hierarchy identities (stages 6-8)

```cpp
struct PlaneId  { uint16_t slot; uint32_t generation; };          // a reused slot gets a new generation
struct SectorId { PlaneId plane; uint16_t sx, sz; };               // kSector cells square (32 to start; measure 16/64)
struct RegionId { SectorId sector; uint16_t local; uint32_t geometryGen; };   // one connected region in a sector
struct PortalId { RegionId a, b; uint16_t index; };                // a maximal legal boundary run, per class
struct TileKey  { PlaneId plane; SectorId sector; uint32_t geometryGen; uint64_t exits, boundaryCost, policy; };
```

* Regions and portals come from the exact footprint-origin plane and the mover's step rule (diagonal corners
  included); never circular clearance, never one node per sector.
* Tile caches compare the full key after a hash match; downstream boundary costs are part of the key.
* Chokepoint state (stage 8) hangs off `PortalId`: usable width per class, pressure, direction, committed
  entrants, estimated delay.

## 4. The scale suite (stage 1)

`tools/scenarios/scale/` (generated by `gen_scale.py`, committed) and the driver `tools/legion_scale.py`.
The files are NOT in the nightly or the `baseline.json` gate (they are too large for every commit); they are
run at each stage's end and at the final report. Every file says `probe scale` and `uplink 16`.

| Family | Sizes | What it stands for |
|---|---|---|
| `one-N` | 100, 1000, 2500, 5000, 10000 | one large selection through the uplink mirror; map grows with N, fixed 160-cell gap (constant-density scaling) |
| `many-N` | 100, 1000, 2500, 5000, 10000 | N/25 squads clicked to distinct distant tiles over 600 ticks, two allied players: many selections, independent destinations, low sharing |
| `swap-N` | 1000, 2500, 5000, 10000 | constant 768x768 map, two allied armies exchange sides head on: crowd-density stress at a constant map, opposing streams |
| `bridges-N` | 1000, 10000 | river with bridges 4/12/32 cells wide, armies crossing both ways: multiple passages, narrow bridge, crossings |
| `maze-N` | 1000, 10000 | walled compound, doors, central room (85%), island behind one bridge (10%), sealed box (5%, unreachable) |
| `mixed-N` | 1000, 10000 | one Alt+1 formation: footprints 1..4, speeds 1.4..2.6, hovers and flyers, across a lake |
| `attack-N` | 1000, 10000 | armed army attacks a smaller armed force walking away: moving target, rings, deaths |
| `churn-N` | 1000, 10000 | four re-clicks (replacement), half stopped and re-clicked (cancellation), a rock placed/lifted every 10 ticks on the route (static churn), a tenth sent into a sealed box |
| `idle-10000` | 10000 | 1,000 movers through 9,000 idle bodies (moving vs idle) |

Metrics (`tools/legion_scale.h`, keys `scale.*`, deterministic, integer): command delivery (`deliver.*`),
first useful movement and the last member to start (`start.*`), navigation service delay as the longest run
a body spent Waiting for a usable field (`wait.max`, `wait.p99`), physical arrival t50/t90/t99/last
(`arrive.*`) apart from order completion (`complete.*`, `complete_outside_n`, `open_inside_end`), path
length against the straight line to the disc edge (`path_x1000.*`), moving bodies (`moving.*`), deadlock and
livelock at the end (`stuck_end`, `livelock_end`), throughput (`throughput.peak300`), navigator containers
and bytes (`nav.*`). The observer's own keys (gate crossings, reversals, contact, work counters) are in the
same JSON line. Wall time per run (stderr `SCALE_WALL`): tick mean/p50/p99/max, ticks over 33.3 ms, peak RSS,
malloc heap; with `TAK_PHASE` the per-phase breakdown (combat, sep, vis, scripts, movement, nav, ...).

The **real client/server path** is measured separately: `takserver --local` plus a headless `takclient
--mpai` with `TAK_STRESS` (every slot filled to ~95% of the 2,000-unit cap) and `TAK_AUTOPLAY_FORMATION` (the
human seat sends its whole army as one Alt+1 AttackMove every 600 ticks, one command per unit through
`GameView::issue`); the debug-only `FORMATION_DELIVERY` line gives the issue tick and the ticks the first and
last of its commands came back applied. It is limited to one player's 2,000-unit cap; the 10,000-unit case
goes through the scale suite's uplink mirror (the same constants, `issue_selection_test`). Stage 3 adds a
10,000-unit real-path case (a cap override for the harness) when it changes the transport.

## 5. Commands

```sh
# builds (worktree): build-o2 = Debug -O2 (harness CLI kept), build = Release
cmake -S . -B build-o2 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-O2 -DCMAKE_C_FLAGS=-O2 \
  -DTAK_STATIC_DEPS_PREFIX=$HOME/TAK/third_party/static-deps -DTAK_FFMPEG_PREFIX=$HOME/TAK/third_party/ffmpeg-bink \
  -DTAK_TEST_DATA=$HOME/TAK/assets/game
taskset -c 4-23 cmake --build build-o2 -j20
# scale suite (one run per job, workers=3, TAK_PHASE on)
python3 tools/legion_scale.py run --bin build-o2/legion_scenario --out OUT --cores 4-23 --per-job 4
python3 tools/legion_scale.py report --results OUT/results.jsonl --markdown
python3 tools/legion_scale.py check --results OUT/results.jsonl --targets tools/scenarios/scale/targets.json
# Legion scenario gate (every tools/scenarios/*.scn, both modes, gate offsets), then
python3 tools/legion_check.py check --cumulative --baseline tools/scenarios/baseline.json --results RUN.jsonl
# --mpai (scratch XDG dirs; Legion with TAK_LEGION=1): takserver --port P --data D --no-auth --local --seed 1,
# then TAK_HEADLESS=1 SDL_VIDEODRIVER=dummy takclient game "Inner Circle" --data D --server 127.0.0.1
#   --serverport P --mpai --time 300   (and --time 60)
sh tools/check-determinism.sh
ctest --test-dir build-o2 -L quick -j20      # scratch XDG_DATA_HOME, no TAK_SETTINGS_FILE export
# exact stages (stage 2 on): identity against a build of the previous head (base cache under ~/.cache/tak-identity)
tools/legion_identity.sh --base BASE/build-o2 --cand build-o2 --replays ~/tak-tmp/scratch/land-we3/rp \
  --data ~/TAK/assets/game --cores 4-23 -j 20 [--verify]   # --verify: rerun the ctest section without it (7.2)
# the brute-force references: TAK_LEGION_VERIFY=1 legion_scenario F --mode legion --workers --offsets 0 (debug builds)
# wall-time A/B: compare arms on the SAME core set (0-7 P-cores, 8-23 E-cores), from copied binaries
```

## 6. Stage 1 record: baselines and acceptance tests

**Hypothesis.** Nothing measured Legion at thousands of simultaneously moving bodies; before any change,
build workloads and metrics that can tell responsive, correct large armies from bodies standing still, and
freeze numerical targets for them.

**Classification.** Tools, tests and docs only. No simulation change: the scale probe is observation-only
(`legion_scale_neutral`), the client probe is debug-only stderr, and every hash below equals the starting
revision's recorded value.

**Touched code.** `tools/legion_scale.{h,py}`, `tools/legion_scenario.cpp` (`probe scale`, `--scale`,
`SCALE_WALL`), `tools/legion_scn.h` (directive, per-unit issued-command counts and per-click delivery marks
in `OrderFeed`), `tools/scenarios/scale/` (`gen_scale.py`, 25 `.scn`, `targets.json`,
`baseline-c2c25707.jsonl`), `tools/scenarios/README.scn-format.txt`, `CMakeLists.txt` (two ctests),
`tools/ctest_costs.txt`, `src/client/gameview{.h,_net.cpp}` (debug-only `FORMATION_DELIVERY`).

**Commits.** aa91fb2f (suite and probe), dc772126 (fixes from its codex review: per-click delivery,
quantile populations, checker exit on missing rows, runner slot/marker handling, ctest exit status), and
the commit carrying this record (second review's fixes: delivery marks per click within a tick, report-only
targets with missing metrics; `targets.json`; the baseline file; this section).

### Machine and build

Intel Core Ultra 9 275HX (24 cores, no SMT, governor `performance`), 125 GB RAM, Fedora (Linux 7.2.9),
GCC 16.2.1, `build-o2` = Debug with `-O2` (ccache + mold). Scale runs: one run per (file, mode), offset 0,
workers exec with `TAK_SIM_WORKERS=3` (what `SimPool::defaultWorkers` picks on a 24-thread machine), each job
pinned to its own 4 cores of 4-23, five jobs at once, `TAK_PHASE=1 TAK_PHASE_MS=0` (per-phase wall time, which
itself costs a little). Game speed 1x (30 Hz, 33.3 ms a tick). Wall numbers are single runs under that load:
indicative, never gated; stage 9 repeats the ones it compares as A/B pairs.

### Checks on the starting revision (c2c25707; exact commands)

| Check | Command | Result |
|---|---|---|
| Legion scenario gate, every `tools/scenarios/*.scn` (135), both modes, gate offsets, serial and workers | `sweep_both.py build-o2/legion_scenario OUT 4-19` (the nightly's `legion_scenario FILE --mode M --json --offsets gate --data ~/TAK/assets/game` per file and mode), then `python3 tools/legion_check.py check --cumulative --baseline tools/scenarios/baseline.json --results OUT/scenarios.jsonl` | **PASS**: 31,753 keys, 0 failed (1,563 ratchets, 2,265 licensed, 216 floor exceptions, 864 report-only); no runner problems; every `hash@` equal |
| `--mpai` Inner Circle seed 1, 300 s | `takserver --port P --data ~/TAK/assets/game --no-auth --local --seed 1`; `TAK_HEADLESS=1 SDL_VIDEODRIVER=dummy takclient game "Inner Circle" --data ... --server 127.0.0.1 --serverport P --mpai --time 300` (Legion: `TAK_LEGION=1`), scratch XDG dirs | Retail **7535cb08ee787b75**, Legion **c707e1a8063bc0c7** (= the protocol-266 record) |
| `--mpai` 60 s | as above, `--time 60` | Retail **2c39430636a3dbda**, Legion **532539f51d70840b** (equal again with the stage-1 client) |
| Cross-build determinism | `sh tools/check-determinism.sh` | OK, golden **dcef618cd2e4d558** |
| ctest quick | `ctest --test-dir build-o2 -L quick -j20 --timeout 900` (scratch `XDG_DATA_HOME`/`XDG_CONFIG_HOME`, no `TAK_SETTINGS_FILE`) | **375/375 passed** (with the stage-1 tools built) |
| New ctests | `ctest --test-dir build-o2 -R legion_scale_` | `legion_scale_neutral` (66 s, cost-listed so not `quick`) and `legion_scale_one100` pass |

**Inherited failures.** None in the checks above. Not run at this stage: the non-quick ctests (the
`nightly` label and cost-listed tests), crowdbench, `legion_cost`, the identity harness, Windows/macOS.
The last nightly logs were not read here; a later stage that needs them reads
`~/tak-tmp/nightly-win/latest.log` and the Linux nightly's output first.

### Real client/server delivery (1,900 bodies)

`CORES=20-23 e2e.sh build-o2 OUT 120` (scratch script: the `--mpai` pair on **Ulasem Arena**, seed 1, client
`TAK_STRESS=1 TAK_MONARCH_EXPENDABLE=1 TAK_AUTOPLAY_FORMATION=1`; Inner Circle ended at tick ~160 when a
monarch died in the stress fight, so it cannot carry the probe). The human seat's army (1,900 bodies, 3,800
commands: SetSquad + AttackMove per body) every 600 ticks:

| order issued at | Legion first / last command applied | Retail first / last |
|---|---|---|
| 600 (3,800 commands) | 603 / 662 | 603 / 662 |
| 1200 (3,762) | 1204 / 1262 | 1203 / 1261 |
| 1800 (3,590) | 1804 / 1860 | 1803 / 1859 |
| 2400 (3,320) | 2403 / 2454 | 2403 / 2454 |
| 3000 (3,150) | 3004 / 3053 | 3003 / 3052 |

So the real path delivers exactly 64 commands a tick per client after a 3-4 tick start: about 2 s for a
1,900-body order (two commands each). The uplink mirror's 10,000-body click (one command each, 16-tick round
trip) takes 308 ticks (10.3 s). Run hashes: Legion dd5f2845da084390, Retail 625643300260e8cc (3600 ticks).

### Scale suite baseline (c2c25707)

`python3 tools/legion_scale.py run --bin build-o2/legion_scenario --out OUT --cores 4-23 --per-job 4`
(37 jobs: 25 Legion, 12 Retail, 21 minutes). Every job exited 0; a second full run with the reviewed probe
gave the same 37 state hashes (the probe cannot touch the sim). Trimmed results:
`tools/scenarios/scale/baseline-c2c25707.jsonl` (the `--base` for later stages); full results with every
observer key: `~/tak-tmp/scratch/legion-10000/base/scale2/results.jsonl`.

Times are ticks from each body's click (-1: fewer than the quantile ever arrived). "arrived" is physical
arrival in the click's packed disc; "completed" is order completion (in brackets, completions outside the
disc). Phase columns are per-tick p99.

| scenario | mode | bodies | deliver max | start last | wait max | arrived | arrive t50/t90/t99 | completed (outside disc) | stuck / livelock | groups | nav MB | tick mean / p99 / max ms | over 33 ms | RSS MB | movement / nav / explore p99 ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| one-100 | legion | 100 | 1 | 9 | 0 | 100/100 | 1661/2235/2465 | 100 (0) | 0 / 0 | 2 | 0 | 0.1 / 0.1 / 1.9 | 0 | 13 | 0.05 / 0.01 / 0.05 |
| one-100 | retail | 100 | 1 | 4 |  | 100/100 | 2037/2553/2704 | 100 (0) | 0 / 0 |  |  | 0.1 / 0.2 / 0.8 | 0 | 9 | 0.06 / 0.0 / 0.05 |
| one-1000 | legion | 1000 | 23 | 65 | 0 | 809/1000 | 2879/-1/-1 | 999 (191) | 1 / 1 | 2 | 2 | 0.6 / 1.1 / 3.2 | 0 | 27 | 0.63 / 0.05 / 0.27 |
| one-1000 | retail | 1000 | 23 | 26 |  | 261/1000 | -1/-1/-1 | 121 (0) | 94 / 5 |  |  | 1.0 / 1.1 / 2.4 | 0 | 32 | 0.65 / 0.0 / 0.18 |
| one-2500 | legion | 2500 | 71 | 90 | 0 | 808/2500 | -1/-1/-1 | 2317 (1519) | 60 / 17 | 6 | 7 | 2.1 / 3.6 / 5.6 | 0 | 55 | 2.6 / 0.11 / 0.43 |
| one-5000 | legion | 5000 | 150 | 219 | 0 | 1532/5000 | -1/-1/-1 | 4316 (2790) | 449 / 34 | 10 | 18 | 5.9 / 10.0 / 10.8 | 0 | 92 | 7.85 / 0.32 / 0.45 |
| one-10000 | legion | 10000 | 308 | 652 | 0 | 1866/10000 | -1/-1/-1 | 6257 (4394) | 1741 / 95 | 20 | 61 | 12.5 / 18.6 / 27.7 | 0 | 180 | 14.71 / 1.65 / 0.95 |
| one-10000 | retail | 10000 | 308 | 310 |  | 595/10000 | -1/-1/-1 | 71 (0) | 8387 / 0 |  |  | 8.4 / 9.4 / 15.1 | 0 | 123 | 6.14 / 0.0 / 0.89 |
| many-100 | legion | 100 | 0 | 9 | 0 | 98/100 | 475/1616/-1 | 98 (2) | 1 / 0 | 8 | 0 | 0.1 / 0.2 / 0.9 | 0 | 10 | 0.11 / 0.01 / 0.03 |
| many-100 | retail | 100 | 0 | 3 |  | 92/100 | 689/1144/-1 | 100 (8) | 0 / 0 |  |  | 0.1 / 0.2 / 0.4 | 0 | 7 | 0.07 / 0.0 / 0.03 |
| many-1000 | legion | 1000 | 0 | 19 | 0 | 794/1000 | 3149/-1/-1 | 786 (25) | 92 / 22 | 80 | 14 | 1.4 / 3.0 / 4.0 | 0 | 40 | 1.46 / 1.5 / 0.2 |
| many-1000 | retail | 1000 | 0 | 4 |  | 813/1000 | 3030/-1/-1 | 993 (185) | 0 / 0 |  |  | 0.8 / 1.2 / 1.5 | 0 | 20 | 0.65 / 0.0 / 0.2 |
| many-2500 | legion | 2500 | 0 | 217 | 13 | 785/2500 | -1/-1/-1 | 647 (10) | 1395 / 48 | 200 | 39 | 3.9 / 7.2 / 12.5 | 0 | 88 | 2.97 / 3.88 / 0.5 |
| many-5000 | legion | 5000 | 0 | 67 | 73 | 470/5000 | -1/-1/-1 | 275 (13) | 3819 / 72 | 400 | 110 | 6.8 / 9.0 / 15.5 | 0 | 196 | 4.13 / 3.77 / 0.65 |
| many-10000 | legion | 10000 | 0 | 248 | 25 | 546/10000 | -1/-1/-1 | 267 (15) | 9164 / 100 | 800 | 180 | 13.2 / 15.3 / 16.9 | 0 | 353 | 8.16 / 4.05 / 0.86 |
| many-10000 | retail | 10000 | 0 | 4 |  | 587/10000 | -1/-1/-1 | 313 (0) | 8695 / 2 |  |  | 12.7 / 15.4 / 19.7 | 0 | 173 | 10.61 / 0.0 / 0.85 |
| swap-1000 | legion | 1000 | 23 | 45 | 0 | 824/1000 | 8074/-1/-1 | 990 (171) | 2 / 2 | 4 | 12 | 1.1 / 2.7 / 5.6 | 0 | 83 | 1.95 / 0.46 / 0.28 |
| swap-1000 | retail | 1000 | 23 | 26 |  | 0/1000 | -1/-1/-1 | 0 (0) | 461 / 1 |  |  | 1.2 / 1.5 / 5.9 | 0 | 66 | 0.86 / 0.0 / 0.23 |
| swap-2500 | legion | 2500 | 71 | 145 | 0 | 1721/2500 | 8809/-1/-1 | 2027 (396) | 5 / 4 | 8 | 19 | 4.1 / 7.8 / 9.6 | 0 | 114 | 5.9 / 0.85 / 0.69 |
| swap-5000 | legion | 5000 | 150 | 243 | 0 | 1308/5000 | -1/-1/-1 | 1474 (195) | 2521 / 46 | 12 | 29 | 7.8 / 13.6 / 15.7 | 0 | 136 | 10.39 / 1.54 / 0.71 |
| swap-10000 | legion | 10000 | 308 | 437 | 0 | 915/10000 | -1/-1/-1 | 378 (0) | 8037 / 86 | 20 | 47 | 12.1 / 16.7 / 20.5 | 0 | 167 | 12.1 / 1.76 / 0.96 |
| bridges-1000 | legion | 1000 | 23 | 45 | 0 | 10/1000 | -1/-1/-1 | 8 (0) | 537 / 34 | 4 | 12 | 1.4 / 2.5 / 5.6 | 0 | 102 | 1.53 / 0.55 / 0.22 |
| bridges-1000 | retail | 1000 | 23 | 26 |  | 2/1000 | -1/-1/-1 | 0 (0) | 883 / 0 |  |  | 1.1 / 1.4 / 6.0 | 0 | 61 | 0.77 / 0.0 / 0.22 |
| bridges-10000 | legion | 10000 | 308 | 526 | 0 | 0/10000 | -1/-1/-1 | 0 (0) | 9020 / 79 | 20 | 47 | 15.2 / 20.5 / 26.7 | 0 | 164 | 15.21 / 1.74 / 0.85 |
| maze-1000 | legion | 1000 | 23 | 32 | 0 | 365/1000 | -1/-1/-1 | 655 (291) | 29 / 15 | 6 | 18 | 1.5 / 2.6 / 7.7 | 0 | 167 | 1.79 / 0.49 / 0.26 |
| maze-1000 | retail | 1000 | 23 | 25 |  | 140/1000 | -1/-1/-1 | 116 (44) | 414 / 1 |  |  | 1.1 / 1.4 / 10.5 | 0 | 88 | 0.75 / 0.0 / 0.26 |
| maze-10000 | legion | 10000 | 308 | 546 | 0 | 1468/10000 | -1/-1/-1 | 1142 (289) | 6139 / 127 | 20 | 67 | 17.2 / 20.4 / 26.5 | 0 | 236 | 15.85 / 1.88 / 0.89 |
| mixed-1000 | legion | 1000 | 53 | 60 | 0 | 636/1000 | 7515/-1/-1 | 448 (0) | 52 / 16 | 5 | 5 | 4.8 / 10.7 / 19.7 | 0 | 62 | 1.86 / 0.05 / 8.36 |
| mixed-1000 | retail | 1000 | 53 | 54 |  | 100/1000 | -1/-1/-1 | 0 (0) | 371 / 5 |  |  | 3.8 / 9.1 / 16.0 | 0 | 35 | 0.72 / 0.0 / 8.12 |
| mixed-10000 | legion | 10000 | 622 | 623 | 0 | 2430/10000 | -1/-1/-1 | 1978 (0) | 4769 / 174 | 13 | 65 | 61.9 / 103.4 / 198.9 | 19013 | 355 | 16.28 / 1.9 / 85.26 |
| attack-1000 | legion | 1100 | 33 | 61 | 0 | 99/99 | 3182/4264/5741 | 98 (0) | 0 / 0 | 8 | 12 | 0.9 / 2.0 / 3.5 | 0 | 76 | 0.97 / 0.58 / 0.19 |
| attack-1000 | retail | 1100 | 33 | 35 |  | 100/100 | 3584/4487/4708 | 97 (0) | 0 / 0 |  |  | 1.1 / 1.2 / 6.7 | 0 | 45 | 0.6 / 0.0 / 0.26 |
| attack-10000 | legion | 11000 | 339 | 1084 | 1 | 895/996 | 3033/-1/-1 | 971 (90) | 5 / 3 | 29 | 66 | 10.1 / 17.3 / 19.3 | 0 | 188 | 11.16 / 2.39 / 0.96 |
| churn-1000 | legion | 1000 | 23 | 117 | 0 | 465/1000 | -1/-1/-1 | 1000 (539) | 0 / 0 | 6 | 5 | 0.7 / 1.6 / 3.7 | 0 | 40 | 0.87 / 0.44 / 0.27 |
| churn-1000 | retail | 1000 | 23 | 726 |  | 351/1000 | -1/-1/-1 | 272 (29) | 38 / 13 |  |  | 1.1 / 1.3 / 3.2 | 0 | 35 | 0.73 / 0.0 / 0.23 |
| churn-10000 | legion | 10000 | 498 | 1637 | 0 | 2174/10000 | -1/-1/-1 | 9999 (7828) | 0 / 0 | 22 | 96 | 10.7 / 22.4 / 33.6 | 1 | 249 | 16.5 / 2.44 / 1.32 |
| idle-10000 | legion | 10000 | 23 | 68 | 0 | 32/1000 | -1/-1/-1 | 28 (0) | 685 / 25 | 2 | 5 | 4.1 / 4.9 / 9.1 | 0 | 94 | 2.39 / 0.13 / 0.23 |

What the baseline says:

1. **Command delivery dominates responsiveness at scale.** A 10,000-body click is delivered over 308 ticks
   (10.3 s); its last member starts moving after 652 ticks (21.7 s; churn's re-clicks 1,637). `wait.max` (the
   longest a body waited for a usable field) is 0-73 ticks: field service is not the bottleneck in these
   workloads, delivery and activation are.
2. **One click becomes many crowds.** The 33-tick convoy limit splits a 10,000-body click into 20 Legion
   groups (`groups` = 20), several points and formations.
3. **Arrival collapses with population.** One selection: physical arrival 100% at 100 bodies, 81% at 1,000,
   32% at 2,500, 31% at 5,000, 19% at 10,000, while most orders still complete (outside the disc: 191 at
   1,000, 4,394 at 10,000 settle short behind their own crowd). Many independent selections: 98%, 79%, 31%,
   9%, 5% (Retail 92%, 81%, -, -, 6%), with 9,164 bodies stuck at 10,000. Head-on exchange on one map: 82% at
   1,000 down to 9% at 10,000. Bridges: both armies take the nearest (12-cell) bridge, the 4- and 32-cell
   bridges carry nothing, and the crossing jams (1% and 0% arrive; Retail 0%). Idle obstacles: 3% of 1,000
   movers get past 9,000 idle bodies.
4. **Whole-tick cost is not yet the binding limit, except one non-navigation phase.** Legion at 10,000 bodies:
   tick mean 10-17 ms, p99 15-22 ms with three workers; `movement` (per-unit steering, serial for Legion) is the
   largest phase (p99 8-17 ms), `nav` (Legion's tick) p99 about 4 ms at most. `mixed-10000` blows the budget (mean
   62 ms, 19,013 of 19,400 ticks over 33.3 ms) in the `explore` phase (p99 87 ms) driven by its 1,000 flyers,
   outside Legion: recorded as a remaining non-navigation bottleneck for stage 9's report.
5. **Memory.** Peak RSS 94-356 MB per 10,000-body run; the navigator's own estimate 5-180 MB (`many-10000`:
   800 live groups, 180 MB).

### Frozen acceptance targets

`tools/scenarios/scale/targets.json` (212 targets, frozen 2026-10-15 from this baseline; change only with a
recorded reason; the one change so far, before any stage was evaluated, came from the review of a8cfc974:
unreachable orders get an allowance in the outside-completion cap, and `attack-*`'s point bodies, the fleeing
enemies, carry no arrival targets because their arrival is their survival). Checked with `legion_scale.py check`:

* Retail: every Retail row's state hash equal to the starting revision's.
* Legion, every file of 1,000+ bodies: a whole click delivered within 30 ticks; its last member moving within
  90 ticks; no body waiting more than 60 ticks for navigation; no livelock at the end.
* Legion physical arrival of at least 99% of the reachable bodies (one, swap, idle) or 97% (many, bridges,
  maze, mixed, churn), no reachable body stuck at the end, at most 2% of the reachable orders completing
  outside their disc (plus the unreachable ones, which retire outside it by design);
  `one-N` t99 within 1.5x the slowest body's straight run, `swap-N` within 2x; bridges using both alternative
  bridges; the attack ring at least as many shooters as at the start.
* Small populations (100 and 1,000): arrival count and t90/t99 no worse than the starting revision (5%).
* Report only: tick p99 within 33.3 ms and no tick over budget at 10,000 bodies on this machine.

At the starting revision: **99 met, 110 missed, 3 report-only missed** (`check` exit 1, as expected: the
program's targets). Unmet targets after stage 9 are reported as such, not re-frozen.

### Open issues from stage 1

* The 10,000-body real-path case needs a unit-cap override for the harness (one player's cap is 2,000);
  stage 3 adds it with the transport.
* `.scn` cannot express production, factory exits or logistics; those stay with `--mpai`, the Ulasem
  benchmark and the remote sweep.
* The `idle-10000` and `bridges-*` failures are behaviour limits (idle bodies never step aside; no
  congestion-aware passage choice) that stages 6-8 address; they are not capacity-infeasible layouts (open
  64-cell detours exist round the idle block, and two of three bridges stay empty).

## 7. Stage records

* Stage 1: section 6 (implemented and validated; branch commits aa91fb2f, dc772126 and the record commit).
* Stage 2: section 7.2 (implemented and validated; fd34151a, 1b8cc086, 946d6972, e290f0ed, 5b6daa11 and the
  record commit).
* Stages 3-9: not implemented.

### 7.2 Stage 2 record: exact registration, awareness and profiled micro-work (brief phase 1)

**Hypothesis.** `registerMove`'s walk of every group and `awareScan`'s every-group-with-every-mover sampling grow
with the number of groups and become a per-tick cost with hundreds of groups; both can be narrowed without changing
a decision. Then profile the 10,000-body workloads and remove demonstrated costs the same way.

**Classification.** Exact. No hashed state, no decision, no wire format changes; no protocol bump (still 266).
Only work counters (`join_iterations`, `group_loop_iters`, `aware_pairs`, `lane_work`, `legion_total`), the
in-game `vertex_*` Stats (now zero in the game) and the navigator's memory estimate move.

**Touched code.** `src/sim/legion.{h,cpp}` (join index, broad phase, `setInstrumentProbes`, `Member::gateMemo`),
`tools/legion_scenario.cpp` (`--no-vertex-probe`; turns the probe on otherwise), `tools/legion_world_test.cpp`
(probe on), `tools/legion_scale.py` (`run --no-vertex-probe`), `CMakeLists.txt` + `tools/ctest_costs.txt`
(three verify ctests), `docs/legion-pathfinding.md` (section "Legion 10,000 stage 2").

**What landed (commits).**

| Commit | Change | Reference kept under `TAK_LEGION_VERIFY` |
|---|---|---|
| fd34151a | `registerMove` join index: groups keyed by (player, plane, kind, approach, ring target + bucket for a reach kind, else issue key), all fixed at creation; the member walks its entry in id order with the whole predicate unchanged | brute-force walk of all groups on every join (same winner or abort); index against a scan of the groups each tick |
| 1b8cc086 | `awareScan` broad phase: movers in a 1,024 px grid over their swept boxes grown by `half + max(tail, head) + 66`; a group reads the cells of its descent-chain box grown by `2 rG`; planned movers (`avoidCmd`) always candidates; candidates in mover order | every dropped mover's 31 conflict samples are run; a hit aborts |
| 946d6972 | the W8 vertex probe (Stats only, read by no decision) runs only under `LegionNavigator::setInstrumentProbes`; on in `legion_scenario` (unless `--no-vertex-probe`) and `legion_world_test`, off in the game | hashes compared probe on / off |
| e290f0ed | `gateCommit` memo: a static decline (inputs: origin, goal, footprint, field serial, navigator and plane epochs, lane band) is remembered; the same inputs decline again without the walk | every memo hit is recomputed; passing the static checks aborts |
| 5b6daa11 | ctests `legion_verify_{battle-assault,crosslong,gap8}` (references on) | - |

**Why the broad phase cannot drop an accepted conflict.** A mover outside the plan is accepted only through a
conflict sample `t`: `|q| <= A = half + rG + 32` and `-(tail + rG + 32) <= s <= head + rG + 32`, where `s`, `q`
are the offset `P - Q` (group way point minus mover box centre) dotted with `(dx, dz)` and divided, truncated, by
`L = isqrtFloor(dx^2 + dz^2) <= |(dx, dz)|`. So the true components along and across the heading are under `B + 1`
and `A + 1` (`B = max(tail, head) + rG + 32`), `|P - Q| < (A + 1) + (B + 1)`, and each coordinate differs by less
than `(half + max(tail, head) + 66) + 2 rG`. `P` is an interpolation between consecutive chain cell centres
(`at`), inside the chain's box; `Q = (cx + trunc(dx t / 30), ...)`, `0 <= t <= 450`, inside the box from the
centroid to `centroid + 15 (dx, dz)`. Overlap of the two grown boxes is necessary; the grid lists a mover in
every cell its grown box covers and a group reads every cell its grown box covers, with the same monotone
clamped cell function, so overlapping boxes share a cell. Movers already in the plan are added explicitly
(a planned crossing stays planned off its box), and the candidate list is sorted, so decisions and the plan's
order are those of the full walk. The join index is exact because the keyed fields are set only at group
creation (`g.plane` cannot change while the group lives: a plane slot is reused only when no group uses it)
and every non-keyed condition is still evaluated, in the same id order.

**Mutation checks (each reference can fail).** A scratch build (`~/tak-tmp/wt/legion-10000-mut`, `TAK_MUT=...`)
walking each join entry in reverse fails `TAK_LEGION_VERIFY` on battle-assault (tick 260), battle-field-2x250
(701) and battle-field-2x500 (744) and nowhere else in the 143 files; zero broad-phase margins fail on crosslong
(tick 435); a memo key without the origin fails on gap8 (466), gapsweep (439) and corner-1x448 (872).

**Profile (gperftools CPU profiler, `LD_PRELOAD=libprofiler.so.0`, 200 Hz, 6,000 ticks, three workers; reader
`~/tak-tmp/scratch/legion-10000/stage2/prof/pprof.py`).** At the starting revision `registerMove` and `awareScan`
were each under 0.2% of samples on one-, many- and churn-10000: the two mandated optimisations remove growth,
not a present cost (counters below). The demonstrated costs were elsewhere: `gateCommit` (about 10% of
one-10000's samples: the descent walk and cross-sections of uncommitted lane members every tick) and the
vertex probe (3.8% one-10000, 5.0% churn-10000; 5-7% of the tick). After e290f0ed `gateAim` / `gateCommit`
left the top of the profile; what remains is flat per-unit steering (`drive` 11-24%: `stepFree`, `yieldLane`,
occupancy reads), field relaxation (`advance`, 6% of many-10000), and outside Legion `updateNavigationExploration`
(6-16%, sight/exploration, shared with Retail) and `rebuildSpatial` (3-5%). Investigated and left: World::unit
self time is cache misses on its fast path (the slow scan never showed); `ringSignature` (3.5% of many-10000) is
a cheap hash bound by occupancy reads; `descend` takes the member's goal for tie-breaks, so a per-field memo of
it would not be shared; loops over id high-water marks run only in `checksum()` and verify code, absent from
the profiles; awareness allocations are per scan (every 30 ticks) and below 0.2%.

**Work counters (scale suite, full runs, starting revision -> stage 2).**

| scenario | join_iterations total (max/tick) | group_loop_iters total | aware_pairs total (max/scan) |
|---|---|---|---|
| many-10000 (800 groups) | 8,008,800 (39,972) -> 19,200 (48) | 14,808,992 -> 6,819,392 | 513,064 (6,754) -> 186,608 (1,182) |
| many-5000 | 2,004,400 (19,972) -> 9,600 (48) | 2,957,970 -> 963,170 | 587,751 (8,200) -> 369,051 (2,255) |
| many-2500 | 502,200 (9,972) -> 4,800 (48) | 709,364 -> 211,964 | 456,259 (7,626) -> 310,495 (3,202) |
| churn-10000 | 1,892,810 (2,826) -> 100,898 (128) | 1,926,832 -> 134,920 | 40,976 -> 40,976 |
| one-10000 | 194,136 (2,500) -> 19,980 (128) | 206,216 -> 32,060 | 18,385 -> 18,385 |
| bridges-10000 | 203,060 (2,500) -> 19,980 (128) | 228,296 -> 45,216 | 1,867 -> 1,867 |

(`aware_pairs` now counts broad-phase candidates; where every mover was already near every group it is unchanged.)

**Wall time A/B** (`~/tak-tmp/scratch/legion-10000/stage2/ab2/run.sh`: 6,000 ticks, three workers, observer on,
no `TAK_PHASE`; the three arms of a scenario run at once on cores 4-7, 8-11 and 12-15, rotated over three
repetitions; each arm compared with the base on the SAME core set, because cores 0-7 are P-cores (5.3-5.4 GHz)
and 8-23 E-cores (4.7 GHz): a P/E difference of about 20% swamps a single unpaired run). Tick mean, base ->
stage 2 with the probe on (as the gate runs) / probe off (as the game):

| scenario | cores 4-7 | cores 8-11 | cores 12-15 |
|---|---|---|---|
| one-10000 | 13.24 -> 11.37 (-14%) / 10.59 (-20%) | 15.15 -> 13.83 (-9%) / 12.88 (-15%) | 15.29 -> 13.46 (-12%) / 13.20 (-14%) |
| churn-10000 | 13.74 -> 12.62 (-8%) / 11.34 (-17%) | 16.09 -> 15.10 (-6%) / 14.08 (-13%) | 16.25 -> 14.84 (-9%) / 14.21 (-13%) |
| swap-10000 | 11.88 -> 11.00 (-7%) / 10.62 (-11%) | 14.24 -> 13.17 (-8%) / 12.45 (-13%) | 13.35 -> 13.86 (+4%) / 12.89 (-3%) |
| many-10000 | 9.90 -> 9.64 (-3%) / 9.51 (-4%) | 12.04 -> 12.07 (0%) / 11.89 (-1%) | 12.17 -> 12.02 (-1%) / 12.05 (-1%) |

Every run of a scenario gave the same state hash. The full scale-suite runs (five jobs at once, mixed P/E slots)
are single runs and only indicative; their tick numbers are in `~/tak-tmp/scratch/legion-10000/stage2/scale/`.

**Memory.** `Member::gateMemo` adds 56 bytes a member: the navigator estimate (`scale.nav.bytes_peak`) rises by
exactly that (one-1000 +56,000 B, +3.3%; 10,000 members +560 kB; +0.07% to +1.9% elsewhere). The join index holds
one int per group plus one map node per key; the broad-phase grid is per scan.

**Checks (exact commands; base = `~/tak-tmp/wt/legion-10000-s2base` at c4233fb9, build-o2).**

| Check | Command | Result |
|---|---|---|
| Identity harness, full, references on | `tools/legion_identity.sh --base <s2base>/build-o2 --cand build-o2 --replays ~/tak-tmp/scratch/land-we3/rp --data ~/TAK/assets/game --cores 4-23 -j 20 --verify --out .../identity/run1` | mpai Legion 300 s **c707e1a8063bc0c7** x2 both builds, Retail **7535cb08ee787b75** x2; replays L/R-bench and L/R-2h final and checkpoint hashes SAME; golden legion/retail SAME; determinism dcef618cd2e4d558 SAME; crowdbench 504 rows SAME; ctest: 68 legion_check_* / legion_cost FAIL only on `work.soft_hash_verify_ticks`, a counter that exists only under `--verify` (an inherited harness limitation: `--verify` cannot be combined with the work-counter gates), plus the three new tests "missing in base" |
| Identity ctest section without references | same, `--skip mpai,replays,golden,crowdbench,determinism`, no `--verify`, `--out .../identity/run2-ctest` | 355 SAME, the only FAIL rows the three new `legion_verify_*` tests (absent from the base) |
| `TAK_LEGION_VERIFY` sweep | every `tools/scenarios/*.scn` (135) plus scale `{one,many,churn,swap,maze,bridges,mixed}-1000` and many-2500: `TAK_LEGION_VERIFY=1 TAK_SIM_WORKERS=3 legion_scenario F --mode legion --workers --offsets 0 --json --data ~/TAK/assets/game` | 144/144 exit 0 (join and broad-phase references at fd34151a+1b8cc086, all four at e290f0ed) |
| Legion scenario gate | `sweep_both.py build-o2/legion_scenario OUT 4-23` then `legion_check.py check --cumulative --baseline tools/scenarios/baseline.json --results OUT/scenarios.jsonl` | **PASS** 31,753 keys, 0 failed, 1,670 ratchets (1,563 at the start: the new ones are all work counters: join_iterations, group_loop_iters, aware_pairs, legion_total, lane_work, churn.bin0), 2,261 licensed (2,265: four licensed work keys now ratchet), 216 floor exceptions, 864 report-only; every `hash@` equal |
| `--mpai` 60 s | `CORES=16-23 mpai.sh build-o2 OUT 60` | Retail **2c39430636a3dbda**, Legion **532539f51d70840b** (equal) |
| Scale suite | `legion_scale.py run --bin build-o2/legion_scenario --out .../stage2/scale --cores 4-23 --per-job 4`; `check --targets tools/scenarios/scale/targets.json --base tools/scenarios/scale/baseline-c2c25707.jsonl` | 37/37 exit 0; every state hash equal to the baseline's; targets **99 met, 110 missed, 3 report-only missed** (identical to the start: an exact stage moves no behaviour key); keys that moved: only `work.legion_total.*`, `work.legion_all.*` and `scale.nav.bytes_peak` |
| ctest quick | `ctest --test-dir build-o2 -L quick -j20 --timeout 900` (scratch XDG, no `TAK_SETTINGS_FILE`) | **377/377** passed (375 + the two quick verify tests) |
| New ctests | `ctest -R legion_verify_` | 3/3 pass (battle-assault 11 s, cost-listed) |
| Release build | `cmake --build build` (all targets) | builds (the references compile out) |
| Automated commit reviews | `.git/worktrees/legion-10000/codex-reviews/{fd34151a,1b8cc086,946d6972,e290f0ed,5b6daa11}.md` | no actionable findings |

**Inherited versus new failures.** New: none. Inherited: none in these checks; the identity harness's `--verify`
mode fails the work-counter gates on a verify-only counter (above), a tool limitation recorded here, not fixed
(changing the harness is out of this stage's scope; run its ctest section without `--verify`, as done).
Not run: non-quick nightly ctests outside the identity selection, Windows/macOS, the nightly logs.

**Traps met.** The automated reviewer builds in the worktree's `build-o2` while it reviews, so a binary can be
replaced under a running profile (seen as "(deleted)" in the profile's maps): profile and A/B from a copied
binary. A `pkill -f` pattern that matched the shell's own command line killed it (memory note
`pkill-self-match`): kill by exact pid.

**Outstanding.** The two mandated optimisations remove growth that the present scale workloads barely exercise
(under 0.2% of samples at the start). The 10,000-body tick is now dominated by flat per-unit steering and, outside
Legion, sight/exploration; neither has an exact structural fix in reach here. Stage 4's budgets and stage 9's
parallel planning are where the per-tick movement cost is next addressed; stage 9's report also carries
`updateNavigationExploration` and `mixed-10000`'s `explore` phase.

## 8. Next step

Resume from branch `task-legion-10000` (pushed; stage 2 done). Build `build-o2` in the worktree, read sections 3
and 7.2. Stage 3 (brief phase 2, behaviour + protocol + replay): explicit logical orders per section 3.1 --
`OrderId` from client intent through the bounded selection-order transaction (`OrderBegin` / `OrderMembers` /
`OrderCommit` / `OrderAbort`), server framing/limits/sequencing and an activation tick, sim-side ownership and
existence checks at activation, every identity consumer moved to `OrderId` with `(player, convoyTick)` kept only
for commands without one; a unit-cap override for the harness so the real client/server path carries a
10,000-body click; one `kNetVersion` bump (267), `replay_test` refusing 266, new `--mpai` baselines, the scale
suite's `deliver.*` / `start.*` targets as the measure. Retail must stay bit-identical except where a
protocol-only field forces otherwise (then say why here).
