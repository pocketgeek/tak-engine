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
| 3 | 2 | Explicit logical orders: client intent identity, bounded selection-order transaction, server sequencing, activation tick, every identity consumer | behaviour + protocol + replay | implemented and validated (section 7.3) |
| 4 | 3 | Deterministic fair scheduling (classes, deficit round robin, aging, unique jobs), complete work budgets, byte accounting | behaviour (schedule) + exact parts | implemented and validated (section 7.4) |
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

**Amended at stage 3 (as implemented; reasons).**

* `OrderId` is not a separate struct in `Order`: the issuing player is the member's owner at activation (the
  sim refuses any other player's unit), so a leg stores only `Order::orderSerial` (cold tail; line 1 is full),
  and the wire carries `net::SelectionOrder{serial, tmpl, shape, members}` inside `net::Cmd::Order`.
* **The arrival area is the key the click consumers use, not the order.** The A1 decision (wall-4x50's four
  selections to one point on ticks 1-4 are one destination) makes an area something several orders can share,
  so `ArrivalAreaId = {OrderId, leg}` would have split those crowds. The area is the convoy table's convoy, now
  joined ONCE per explicit order (per class) at its click, and keyed `kExplicitArea | serial` when an explicit
  order opens it (`ConvoyTable::Convoy::key`, `Order::convoyTick`). Points, groups, soft-arrival ownership,
  formation slots, the A2 wait, flight stations and patrol legs keep keying on that value, which every member of
  an order now gets in its activation tick; the order serial is used where "the same order" is meant (the
  pinwheel part, hashing, observers). A queued leg is its own order, so its own area.
* No per-member coordinates travel: the right-click move is `OrderShape::Offsets`, and the sim recomputes the
  clamped offsets from the members' positions at activation (integer Fixed arithmetic).
* `NavSubgroupId` and `RouteId` are not used yet (stages 6 and 8).

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

**Amended at stage 4 (as implemented; reasons).**

* `sim/navbudget.h` holds `NavWork`, `NavClass`, `NavBytes` (with `NavSize`, the fixed element sizes),
  `NavAccounting` (observation), `NavBudget` (constants) and the service policy (`navDrr` with `NavCursor`,
  `navPromote`, `NavAdmit`), header-only so that `navbudget_test` drives the scheduler's own code.
* **A job is a Field** (one per field serial, however many groups share it); no separate `NavJobKey`/`NavJob`
  structs: the field already is the unique key, and its scheduler state (`deficit`, `lastServed`) lives in it and is
  hashed with it. Stage 6's tiles will be jobs the same way (the key then is the tile key of 3.4).
* **Budgets are per subsystem, each in its own deterministic unit**, not one `perTick[class]` array: the field quota
  (relaxations + initialisation + ring cells, with the refresh allowance capping the two refresh classes),
  registration (`kRegBudget`), first plane builds (`kPlaneBuildBudget`) and awareness (`kAwareBudget`), each per
  window (a tick, or a command batch between ticks). Class shares inside the field quota come from the class order,
  the refresh allowance and the windows, not from fixed per-class amounts (a fixed share idles when its class is empty).
* **Round robin over a window**: `navDrr` serves a class's oldest `kWindow` (12; refresh classes `kRefreshWindow` 3)
  jobs by deficit round robin and admits the next oldest as one leaves. Plain round robin over every job was built
  first and measured worse: every in-flight awareness refresh progressed slowly at once, outlived the 30-tick
  awareness cadence and was discarded by the next re-plan (many-1000: discards 41 -> 1,960, arrivals -5%); the
  window keeps concurrency low enough for jobs to finish, and age promotion bounds the wait behind it.
* `NavBytes` drives one policy, the field cap (`fieldAllowance`: the potentials plus the unfinished builds' frontiers,
  kept incrementally in `FieldTotals`); the other categories are observation. Heap and capacity are measured beside
  it by the tools (SCALE_WALL `heap_kb`, `nav_capacity_bytes`), never read by the sim.

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
| `sched-load` | 480 | (added at stage 4) 48 squads re-clicked to far tiles every 160 ticks and a rock walking across the map: field requests faster than the quota serves them for the whole run (service fairness and request age under load; ctest `legion_sched_load`) |

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
* Stage 3: section 7.3 (implemented and validated; commits listed there).
* Stage 4: section 7.4 (implemented and validated; commits listed there).
* Stages 5-9: not implemented.

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

### 7.3 Stage 3 record: explicit selection orders and scalable command delivery (brief phase 2)

**Hypothesis.** The 10,000-body click is slow and split because the client sends one command per unit through a
64-a-tick drain (157-308 ticks) and the sim re-infers the click from proximity and timing (20 groups for one
click). Carrying the click as ONE explicit order with its exact membership, sequenced whole into one tick by the
server, removes both: command latency becomes one tick whatever the size, and the click's identity is exact.

**Classification.** Behaviour + protocol + replay (protocol 267; `replay_test` refuses 266). Retail is
bit-identical: Retail clients keep per-unit commands, the sim stamps nothing outside Legion, and the hash folds
the new fields only when set (convoy key only when it differs from its first tick, `orderSerial` only when
non-zero). The AI's and scripts' per-unit commands go through the unchanged inference (`ConvoyTable`), so an
AI-only Legion game is bit-identical too (`--mpai` below).

**Design (as built).** docs/legion-pathfinding.md "Legion 10,000 stage 3" and section 3.1's amendment. In short:
`net::Cmd::Order` + `net::SelectionOrder{serial, tmpl, shape, members}`; uplink `OrderBegin` / `OrderMembers`
(varint deltas, at most 2,048 a frame, strictly increasing) / `OrderCommit` (count + FNV digest) / `OrderAbort`;
server `server/orders.h` (`OrderInbox`) validates framing, template, serials (per connection and above the slot's
highest activated serial), counts, digest; malformed -- or an order in a Retail game -- fails the connection,
over the byte budget or the window drops the transaction (logged, and the sender told: `Msg::InputDropped`, fix
round below); a committed order is ONE FIFO item drained
whole by a per-tick order budget (96 KiB of records per room, first pick rotating) once the client's per-second
command budget has room for its members (it waits for it, never dropped), that tick being the activation tick;
the sim (`applySelectionOrder`) applies every member in increasing id order (so Load and GiveUnit, whose outcome
depends on that order, stay per-unit commands: fix round, R6) with the ordinary per-unit
ownership/existence checks, inside a `World::beginOrder` scope that stamps the member's own legs with the
order's serial and, for Move/Fight/Patrol, the arrival area the order joins once per class at its click.
Client: `GameView::ClickScope` + `packClick`; one outbox FIFO; orders under their own window (`OrderWindow`, by
serial, released by the order's bundle or by the server's drop notice), serials assigned
when an order leaves (a click held through a rejoin's catch-up still gets a fresh serial).

**Limits (per sender).** One open transaction; 16,384 members an order; 64 orders and 384 KiB of records
queued at the server or in flight from the client; the existing per-second byte budgets (charged at receipt)
and the command budget (an order's members count as commands, charged when the order is drained: over it the
order waits); 512 queued ordinary commands, 64 a tick. All budgets are taken all-or-nothing now (a refused
frame no longer spends part of them, PlayerCommands included). Server memory per sender is
bounded by one staging buffer (16,384 ids) and 384 KiB of queued records (at most about 390,000 ids, 1.6 MB, with
the densest encoding); decode work per frame by 2,048 members. (The window was 8 orders until the scale suite
showed many-10000 -- 400 squads clicked over 600 ticks through one 16-tick round trip -- waiting up to 196 ticks
for it: the count is generous now and the bytes bound the memory.) No head-of-line starvation: an order never takes command credit and a command never takes order
window, a big order of one sender never holds another sender's commands, and within a sender nothing overtakes
what was sent before it (a Stop behind a Move stays behind it, in the same tick when both fit).

**Worst case one sender can force into one tick** (verifier, non-blocking (a)). Before this stage a sender put at
most 64 unit commands into a tick. Now the bound is the room's order budget (`kTickOrderBytes`, 96 KiB of records a
tick, about 98,000 members at the densest encoding) and the per-second member budget (65,536 per client and per
account, 524,288 for the server): one sender can land 65,536 members in one tick (four 16,384-member orders of
consecutive ids), several senders together up to the 96 KiB. At the measured registration cost (one-10000: about
39 ms for 10,000 members in `World::order`) that is about 250 ms of `applyCommand` for one sender's 65,536 members,
and about 380 ms for a tick filled to 96 KiB by several. Stage 4's resumable, budgeted registration (section 8)
must bound it; nothing in stage 3 does.

**Edge cases and where they are tested.**

| Case | Behaviour | Test |
|---|---|---|
| Incomplete transaction (disconnect mid-stream) | discarded with the connection (`Client::clearCommands`); nothing reaches a bundle | `server_orders_test` (clear), rejoin run |
| Malformed (second Begin, foreign serial, zero delta = duplicate, decreasing across chunks, too many members, bad digest/count, old serial, serial under the floor, forbidden verb, Offsets on a non-Move, unit id in the template, off-range coordinate, truncated/trailing, nested order, unknown shape, id past INT32_MAX) | connection failed, as for invalid PlayerCommands; other accounts unaffected | `server_orders_test`, `server_orders_network` |
| Abort | discarded; the serial is not reused | `server_orders_test`, `server_orders_network` |
| Over the byte budget or the window | transaction dropped and logged; the rest activate in serial order; the sender is told (`Msg::InputDropped`, serial) and its window releases the order, so a drop never holds the outbox (the windows are tested before the byte budget is charged) | `server_orders_test` (dropsReleaseTheWindow, the uncharged refusals), `server_orders_network` (200-order burst: every cut serial named once) |
| Ordinary commands dropped (queue or byte budget) | dropped whole and logged; the sender is told the count and its command window releases them (before this fix round the same leak existed for commands: inherited from c2c25707, fixed here with the same notice) | `server_orders_network` (600 commands past the 512 queue) |
| Over the per-second member budget | the order waits at the head of its client's FIFO (so does what the client sent after it) until the budget's second turns; never dropped, so the client's window never leaks | `server_orders_network` (7 x 10,000 members) |
| An order in a Retail game | connection failed (a modified client cannot dodge the 64-a-tick drain) | `server_orders_network` |
| Legs a member's command gives another unit, or a verb without a click point (Assist's approach move) | per-unit identity, no shared area at the template's (0, 0) | `selection_order_test` (membership) |
| Dead, gone or given-away member | skipped by the sim's per-unit check at activation | `selection_order_test` (membership) |
| Order-dependent verbs (Load, GiveUnit) | not order verbs: sent per unit in issue order, as before (an order applies members in id order) | `selection_order_test` (issueOrder) |
| Several orders in one tick | applied in bundle order: a Stop behind a Move wins, a second Move replaces, a queued (Shift) order appends its own leg and area | `selection_order_test` (sameTick) |
| Two orders to one point within the area window | one arrival area (decision A1), separate serials | `selection_order_test`, `convoy_test` (sharedArea) |
| Patrol | outbound and return legs (ground and flyer) carry the order's identity | `selection_order_test`, `convoy_test` |
| Replay | orders recorded whole in the bundles; strict read validates every order; 266 refused | `selection_order_test` (codec), `replay_test`, replay verify of a recorded Legion game (below) |
| Rejoin | the bundle log replays the orders; serials continue above the slot's activated floor; a click made during catch-up is sent afterwards with a fresh serial | rejoin run (below) |

**Commits.** 3312368f (wire + server), 2920bd1c (sim application and identity), 887fd663 (client, tools mirror,
tests, debug harness unit cap), 3b70aea1 (scenario and cost baselines), 956d8dce (order window 8 -> 64 orders, found
by the scale suite's many-10000), and the commit carrying this record and the docs.

**Review.** An automated high-effort review of the uncommitted diff (read-only) found six bugs and four cleanups;
all were fixed before the measurements below except one kept by decision: (1) an order the server dropped over the
per-second member budget never came back, so the client's order window leaked and could lock the outbox -- the
member budget is now charged at the drain and the order waits instead; (2) explicit Assist/Repair orders opened an
area at the template's (0, 0) -- only Move/Fight/Patrol orders join an area now; (3) the situation recorder skipped
`Cmd::Order`; (4) the server accepted orders in Retail rooms -- refused now; (5) legs a member's command gave other
units got the order's identity -- only the member's own now; (6) members apply in id order rather than selection
order -- kept and documented (the selection order was incidental). **Both (1) and (6) were reopened by a later
review and are resolved in the fix round below**: (1) covered only the member budget, and the byte budgets and the
windows could still drop an order the client never heard of; (6) the order matters for Load and GiveUnit. Cleanups: the record size is cached, the serial is
set in place at send, `orderSerial` has its line assertion, the budget chain and coordinate check are shared.

### Checks (exact commands; base = stage 2 head 54be13b8, its binaries in `~/tak-tmp/scratch/legion-10000/stage3/base-bin`)

Scratch scripts are in `~/tak-tmp/scratch/legion-10000/` (`mpai.sh`, `sweep_both.py`, `sweep_legion.py`,
`stage3/e2e.sh` = `e2e.sh` with `TAK_UNITCAP=${UNITCAP}`, `stage3/rejoin.sh`, `stage3/outcome_ab.py`,
`stage3/license.py`).

| Check | Command | Result |
|---|---|---|
| ctest quick | `ctest --test-dir build-o2 -L quick -j4 --timeout 900` (scratch XDG, no `TAK_SETTINGS_FILE`), after rebuilding every target | **380/380** passed (377 + `server_orders`, `selection_order`, `server_orders_network`) |
| Intermediate commits | each of 3312368f and 2920bd1c checked out in a scratch worktree and built (takserver, takclient, legion_scenario and the order tests) | both build; `server_orders_test`, `replay_test`, `convoy_test`, `issue_selection_test` and `server_orders_network` pass at both |
| Unit and transaction tests | `selection_order_test`, `server_orders_test`, `convoy_test`, `issue_selection_test DATA`, `replay_test`, `server_commands_test`, `python3 tools/server_orders_network_test.py --server build-o2/takserver --data DATA` | all pass |
| `--mpai` Inner Circle seed 1 | `CORES=16-19 mpai.sh <bin> OUT 60` and `300` | 60 s Retail **2c39430636a3dbda**, Legion **532539f51d70840b**; 300 s Retail **7535cb08ee787b75**, Legion **c707e1a8063bc0c7**: all four equal to the starting revision (an AI-only game issues no orders, the per-unit path is unchanged) |
| Cross-build determinism | `sh tools/check-determinism.sh` | OK, golden **dcef618cd2e4d558** (g++/clang++ x O0/O2/O3; aarch64 skipped: no toolchain) |
| Real path, 1,900 bodies | `CORES=20-23 UNITCAP=2000 stage3/e2e.sh <bin> OUT 120` (Ulasem Arena, seed 1, `TAK_STRESS`, `TAK_AUTOPLAY_FORMATION`) | Retail hash **625643300260e8cc** (= stage 1) and delivery unchanged (603/662, 1203/1261, ...); Legion: each formation order (SetSquad + AttackMove, 3,052-3,800 unit commands) lands whole: first = last = 603, 1203, 1804, 2404, 3003 (was 603/662, 1204/1262, 1804/1860, 2403/2454, 3004/3053) |
| Real path, about 9,000 bodies a side | `CORES=4-15 UNITCAP=10000 stage3/e2e.sh <bin> OUT 120` (debug harness cap; final binaries) | Legion: each order pair (SetSquad + AttackMove, 14,590-18,036 unit commands) lands whole, first = last every time: issue 600 -> 605, 1201 -> 1237, 1806 -> 1846, 2410 -> 2452, 3015 -> 3055 (the gap is the client's playout lead under an 18,000-body sim; the server activates an order in the first tick it closes after receiving it). Retail: the first order lands over 605..950 (345 ticks), the next two are still landing when the following click is issued (superseded with 11,392 of 17,632 and 16,320 of 16,910 landed), the last two take 480 and 271 ticks. No desync either way (`err=none`). At this load the run is not reproducible run to run in either mode -- the autoplay client's issue ticks follow its wall-clock lag, so its inputs differ (an earlier run: Legion lands at 628, 1258, 1861, 2466, 3074; Retail's first order over 604..1116) |
| Rejoin | `CORES=20-23 stage3/rejoin.sh <bin> OUT` (host + joiner + AI, joiner killed after its second order, rejoins with the ticket) | the rejoined client's orders keep activating (serials above the floor; a click made during catch-up goes out afterwards with a fresh serial), host and rejoined client end on the same hash (**f7675ae2b1c7964b**), the server refuses nothing. (Before serials were given at send time, the click made during catch-up would have reused serial 1 and failed the connection: found by this run.) |
| Replay | `TAK_REPLAY_VERIFY=1 takclient replay <the Legion e2e recording> --data DATA` | 3,603 ticks, 120 hash checkpoints, no divergence |
| Legion scenario gate | `sweep_both.py` (gate1, both modes) then, after the review fixes, `sweep_legion.py <bin> gate2 4-19 gate1` (Legion jobs again, Retail lines reused: the fixes cannot reach Retail); `legion_check.py check --cumulative --baseline tools/scenarios/baseline.json --results gate2/scenarios.jsonl` | before the baseline edits: Retail 0 failures, every Retail hash@ equal; Legion 2,011 failures (742 hash@, the rest in the 46 click-heavy fixtures and motion-cross). After 3b70aea1: **PASS**, 31,768 keys, 0 failed, 2,160 ratchets, 3,053 licensed, 218 floor exceptions; `validate` 0 problems |
| Identity of the per-unit paths (AI, Retail) | `tools/legion_identity.sh --base ~/tak-tmp/wt/legion-10000-s2base/build-o2 --cand build-o2 --replays ~/tak-tmp/scratch/land-we3/rp --data DATA --cores 20-23 -j 4 --skip mpai,ctest` (base cached) | replays L-bench, L-2h, R-bench, R-2h (protocol byte patched to 267): final hashes and all 61 checkpoint section hashes identical to the stage-2 base (3c0b297f441dde75, b14d12d4cc0ac939, 346cf77e91fe3ec7, fe702dcac891b13d); golden legion and retail SAME. The crowdbench matrix was interrupted (1,008 jobs on four shared cores, 100 done in 22 minutes) and is not reported |
| legion_cost and the battle windows | `ctest -R '^legion_cost$|^legion_scenario_battle-'` | pass after 3b70aea1 (34 cost-corner/cost-open and 4 battle counters retaken with reasons); the scaling gates hold unchanged (W(4N0)/W(N0) 0.41 / 0.72, slot cells per slot 0.67 / 0.48, upkeep 1.00) |
| Scale suite | `legion_scale.py run --bin <bin> --out .../stage3/scale --cores 4-19 --per-job 4` (37 jobs; many-10000 and many-5000 rerun on 956d8dce with `--files`, merged into `scale/results-final.jsonl`); `check --targets tools/scenarios/scale/targets.json --base tools/scenarios/scale/baseline-c2c25707.jsonl`; `stage3/scale_ab.py stage2/scale/results.jsonl stage3/scale/results-final.jsonl` | 37/37 exit 0; every Retail state hash equal to stage 2's; targets **118 met, 86 missed, 8 report-only missed** (stage 2: 99 / 110 / 3; the report-only misses are wall-clock `over_budget` / p99 rows, see below). Table below |

**What moved in the Legion fixtures, and why.** 88 of the 89 fixtures whose clicks fit one uplink tick are
behaviour-identical (every key equal but the convoy lookup counters, which an order now costs once instead of per
unit, and the hash). motion-cross changes for a reason of its own: its two crossing streams (24 one-body clicks each,
all on tick 1) opened two convoys with the same first tick, so the per-unit key made them ONE command that never
planned round itself; explicit areas are keyed per order, so they are two commands and awareness plans the crossing
(offset 0: fields built 4 -> 10, backward 469 -> 85, flips 48 -> 13, u09 now arrives). The 46 fixtures that issue more
than 64 unit commands in one tick change because their clicks now start at once instead of in 64-body waves. That is
proven, not assumed: a scratch build that splits each explicit order into 64-member orders landing one a tick
reproduces every stage-2 behaviour key on 14 of them (attackring-mob-100, corner-1x448, cost-corner,
deadend-w4-n120-closed-squad/-room-plain, deadend-w6-n120-room-squad, gen1-route-36057, goalblock-200, liftstream,
split-450-pocket, strait-3x80, tail-open304, tail-wave, unreach-200; battle-field-2x250 cannot be reproduced exactly,
because the old mirror sent both players through one FIFO), and splitting into 64-member orders landing in the SAME
tick reproduces this stage's values (deadend-w4/w6-n120). Keeping 64-body pinwheel parts inside an order changed
nothing that mattered and was rejected. Over the 46 fixtures, against the stage-2 run (keys moved more than 10%,
`outcome_ab.py`): 304 keys better, 359 worse; 17 fixtures mostly better (tail-wave 23 better / 5 worse, cost-corner
22 / 3, liftstream 21 / 4, battle-assault 17 / 21, island-1x150 15 / 9, strait-2x150 11 / 2), 25 mostly worse, the
worst in narrow dead ends and long routes where a whole column now starts into the bottleneck at once:
deadend-w4-n120-room-plain arrivals 50 -> 29 (below Retail's 34: a Retail-floor exception), -closed-squad parked_held
13,732 -> 52,272, corner-1x448 group B never finishing (8,958 -> never), gen1-route-36057 two groups' t90 -> never,
pocket-615-open 14 keys worse. These are licensed as the cost of the simultaneous start the brief requires, and
recorded as stage 7-8 inputs (chokepoint admission for genuinely narrow friendly passages, coherent subgroups), not
fixed here by re-introducing a per-unit release queue (which phase 2 forbids).

**Scale suite, Legion rows, stage 2 -> stage 3** (`a -> b` where the value moved, one value where it did not;
`deliver.max` = ticks from a click's issue tick to its last member's command, `start.last` = ticks to the last
member moving, `apply max ms` = the longest single tick spent applying commands, new in this stage; wall columns are
reported only and were measured on a shared machine).

| scenario | deliver.max | start.last | arrived | completed | stuck | livelock | tick mean ms | tick p99 ms | apply max ms |
|---|---|---|---|---|---|---|---|---|---|
| attack-1000 | 33 -> 0 | 61 -> 23 | 99 | 98 -> 99 | 0 | 0 | 0.818 -> 1.161 | 1.778 -> 1.791 | 21.77 |
| attack-10000 | 339 -> 0 | 1084 -> 42 | 895 -> 882 | 971 -> 954 | 5 -> 8 | 3 -> 7 | 10.038 -> 13.113 | 17.361 -> 28.286 | 36.598 |
| bridges-1000 | 23 -> 0 | 45 -> 15 | 10 -> 12 | 8 -> 11 | 537 -> 539 | 34 -> 44 | 1.114 -> 1.206 | 1.988 -> 2.079 | 29.89 |
| bridges-10000 | 308 -> 0 | 526 -> 45 | 0 | 0 | 9020 -> 9053 | 79 -> 88 | 12.463 -> 12.747 | 17.906 -> 22.381 | 35.343 |
| churn-1000 | 23 -> 0 | 117 -> 46 | 465 -> 436 | 1000 | 0 | 0 | 0.668 -> 0.646 | 1.623 -> 1.632 | 8.319 |
| churn-10000 | 498 -> 0 | 1637 -> 1230 | 2174 -> 3587 | 9999 -> 9968 | 0 -> 5 | 0 -> 4 | 10.59 -> 8.114 | 22.29 -> 16.646 | 42.836 |
| idle-10000 | 23 -> 0 | 68 -> 22 | 32 -> 33 | 28 -> 27 | 685 -> 666 | 25 -> 31 | 3.95 -> 5.944 | 5.01 -> 9.123 | 26.854 |
| many-100 | 0 | 9 | 98 | 98 | 1 | 0 | 0.057 -> 0.049 | 0.152 -> 0.132 | 0.513 |
| many-1000 | 0 | 19 | 794 | 786 | 92 | 22 | 1.631 -> 1.561 | 3.439 -> 3.161 | 4.851 |
| many-10000 | 0 | 248 | 546 | 267 | 9164 | 100 | 13.706 -> 12.337 | 16.667 -> 16.398 | 32.933 |
| many-2500 | 0 | 217 | 785 | 647 | 1395 | 48 | 4.295 -> 3.808 | 7.842 -> 7.049 | 8.222 |
| many-5000 | 0 | 67 | 470 | 275 | 3819 | 72 | 8.072 -> 6.784 | 10.559 -> 8.911 | 18.613 |
| maze-1000 | 23 -> 0 | 32 -> 10 | 365 -> 335 | 655 -> 627 | 29 -> 42 | 15 -> 19 | 1.299 -> 1.302 | 2.303 -> 2.279 | 53.91 |
| maze-10000 (misread: see the fix round) | 308 -> 0 | 546 -> 15 | 1468 -> 1070 | 1142 -> 8947 | 6139 -> 843 | 127 -> 25 | 15.051 -> 10.516 | 18.136 -> 21.818 | 56.281 |
| mixed-1000 | 53 -> 0 | 60 -> 11 | 636 -> 667 | 448 -> 515 | 52 -> 32 | 16 -> 20 | 4.676 -> 4.818 | 10.351 -> 11.812 | 21.731 |
| mixed-10000 | 622 -> 0 | 623 -> 11 | 2430 -> 2254 | 1978 -> 868 | 4769 -> 6227 | 174 -> 154 | 60.661 -> 88.642 | 112.142 -> 177.118 | 145.941 |
| one-100 | 1 -> 0 | 9 -> 8 | 100 | 100 | 0 | 0 | 0.059 -> 0.054 | 0.12 -> 0.11 | 2.039 |
| one-1000 | 23 -> 0 | 65 -> 39 | 809 -> 847 | 999 | 1 -> 0 | 1 -> 0 | 0.551 -> 0.601 | 0.992 -> 1.107 | 6.231 |
| one-10000 | 308 -> 0 | 652 -> 15 | 1866 -> 6973 | 6257 -> 9964 | 1741 -> 13 | 95 -> 9 | 12.322 -> 9.549 | 18.138 -> 15.861 | 38.906 |
| one-2500 | 71 -> 0 | 90 -> 29 | 808 -> 1970 | 2317 -> 2495 | 60 -> 2 | 17 -> 2 | 2.078 -> 1.78 | 3.543 -> 3.287 | 12.197 |
| one-5000 | 150 -> 0 | 219 -> 49 | 1532 -> 3684 | 4316 -> 4988 | 449 -> 0 | 34 -> 0 | 5.481 -> 4.823 | 9.49 -> 9.573 | 23.086 |
| swap-1000 | 23 -> 0 | 45 -> 20 | 824 -> 781 | 990 -> 965 | 2 -> 5 | 2 -> 5 | 1.191 -> 1.063 | 2.825 -> 2.587 | 29.694 |
| swap-10000 | 308 -> 0 | 437 -> 15 | 915 -> 1405 | 378 -> 1294 | 8037 -> 5841 | 86 -> 103 | 11.722 -> 17.972 | 15.35 -> 29.125 | 44.31 |
| swap-2500 | 71 -> 0 | 145 -> 48 | 1721 -> 1859 | 2027 -> 1929 | 5 -> 11 | 4 -> 7 | 3.319 -> 3.463 | 5.977 -> 6.276 | 32.312 |
| swap-5000 | 150 -> 0 | 243 -> 15 | 1308 -> 2319 | 1474 -> 3181 | 2521 -> 907 | 46 -> 41 | 6.96 -> 6.312 | 11.779 -> 12.841 | 43.713 |

* **The 10,000-body one-selection case** (one-10000): command latency for the whole selection (`deliver.max`) 308 -> **0**
  ticks, last member moving (`start.last`) 652 -> **15** ticks; with every member starting at once the outcome moves
  with it: arrived 1,866 -> 6,973, completed 6,257 -> 9,964, stuck 1,741 -> 13, livelock 95 -> 9, groups 20 -> 2,
  nav memory 62 -> 13 MB, tick mean 12.3 -> 9.5 ms. The cost is the activation tick: **38.9 ms** applying the order
  at tick 2 (the 10,000 registrations inside `World::order`), the run's only tick over the 33.3 ms budget. That is
  stage 4's budget work, recorded in section 8.
* Every single-selection 10,000 row now delivers in 0 ticks (was 308-622) and starts its last member within 11-45
  ticks (was 437-1,637; churn-10000, whose eight re-clicks keep cancelling members, 1,230).
* many-* (selections of 25 bodies, one order each) keep every behaviour key of stage 2 (only the hash moves: the order
  serial is hashed). many-10000 was first run with a window of 8 orders: 400 squads clicked over 600 ticks through a
  16-tick round trip waited for it (`deliver.max` 196, `start.last` 348); 956d8dce makes the window 64 orders (the
  384 KiB still bounds the server's memory) and the rerun restores 0 / 248. No gate fixture enables the uplink
  window (`uplink` appears only in the scale files) and no other scale file has more than three orders inside a
  round trip, so the window change reaches only this row (and many-5000, rerun with it: identical keys).
* Outcomes that moved the wrong way: mixed-10000 completed 1,978 -> 868 and stuck 4,769 -> 6,227 (its 13
  convoys of mixed classes now start at once), attack-10000 arrived 895 -> 882, bridges-10000 stuck 9,020 -> 9,053 (no
  body arrives either way: the bridges are the stage 7 chokepoint input), churn-1000 arrived 465 -> 436, maze-10000
  arrived 1,468 -> 1,070 and its room held 218 -> 54 bodies at the end. **Correction (fix round):** maze-10000's
  "completed 1,142 -> 8,947, stuck 6,139 -> 843" is not an improvement: 7,595 of group A's 8,500 orders were retired
  far outside the arrival disc (median 469 cells from the click, at the starting rect; `complete_outside_n` 289 ->
  7,878, frozen max 690), so "stuck" fell because orders were retired where the bodies stood, not because bodies
  arrived. Same cause as the gate fixtures above; licensed on the same
  basis and listed as stage 7-8 inputs.
* Wall clock (report only): mixed-10000's tick mean rose 60.7 -> 88.6 ms and its p99 112 -> 177 ms; its job ran
  next to the 10k real-path run, so the size of the rise is not established by this run. The other 10,000 rows hold
  within the noise of the shared machine except swap-10000 (p99 15.4 -> 29.1 ms, 75 ticks over budget) and
  attack-10000 (17.4 -> 28.3 ms): both now plan their whole crowd in the first ticks. Re-measure on a quiet machine
  in stage 4.


### 7.3 fix round: verifier rejection and review R1 / R6 (2026-10-16)

The verifier rejected the first stage-3 record (head 2711702e) for a records gap: one red-line regression was misread
and three frozen targets that stage 2 met were missed without being named. A separate code review left two findings
open (R1, R6). Everything below is on the branch after 2711702e.

**Findings and what was done.**

1. **maze-10000: orders retired far outside the arrival disc (the verifier's blocking item).** At 2711702e group A's
   `complete_outside_radius` was 0 -> 7,595, `complete_dist_median` 117 -> 469 cells, `work.outside_area_completions`
   445 -> 8,183, `region.room.inside` 218 -> 54, `scale.complete_outside_n` 289 -> 7,878 (frozen max 690). Mechanism
   (confirmed): the queue-settle rule (C29: a body one row behind a settled arrival of its own command may settle, so
   a queue settles back row by row) is bounded only by the reach of everyone settled at the destination. Explicit
   orders made all 8,500 bodies one area, so the queue ran back out through the compound doors to the starting rect;
   at stage 2 the same bodies were 20 convoys. That is the forbidden pattern (distant stationary units declared
   arrived), so it is **fixed, not licensed** (f611b163, 1a74fbb0): an explicit area's settle bound never grows past
   the packed bound of the point's whole crowd (`Point::crowdCells` / `crowdPeak`: every footprint plane's bodies,
   packed in this body's footprint along the field, so a dead end's strip is its length), widened to AR-08's arrival
   disc (9(foot+1)/(8 foot): the disc's 20(foot+1) px against the packed bound's 16 foot px per sqrt(1.25 n/pi)), plus
   the usual slack. Beyond it a queued body keeps its order and waits. Per-unit areas (AI, scripts, Retail-mode tools)
   keep the old rule, so `--mpai` and Retail are unchanged. `crowdPeak` is hashed for explicit-area points. Tried and
   rejected: a 3/2 factor (same scale-suite result, tighter than the disc), and a straight-line test against the
   disc itself (a dead end's queue is a strip longer than any disc: 1,174 gate keys failed against 380).
2. **Three targets met at stage 2 and missed at 2711702e, now named as new regressions** (`legion_scale.py check`
   on stage 2's and 2711702e's results, MISS lists diffed):

   | target (Legion) | frozen | stage 2 | 2711702e | fix round | status |
   |---|---|---|---|---|---|
   | maze-10000 `scale.complete_outside_n` | max 690 | 289 | 7,878 | 295 | fixed (item 1) |
   | churn-10000 `scale.livelock_end` | max 0 | 0 | 4 | 106 | **open**, new regression (section 8) |
   | one-100 `scale.arrive.t99` | max 2588 | 2465 | 2608 | 2608 | **open**, new regression: the 100-body click now starts at once instead of 64 + 36 (section 8) |

3. **R1 (serious): a dropped order could wedge the client's outbox for good** (6fbce6bd). The member budget waited at
   the drain already, but the per-second byte budgets (shared with the account's other connections -- or the
   address's, without accounts -- and the whole server), the order window and the record-byte window could still drop
   an order the client never heard of: it never came back in a bundle, so it held its place in the client's window,
   and after 64 of them the order at the outbox head never went and every command behind it (Stop, Build, Train)
   waited for the rest of the game. Now the server tells the sender (`Msg::InputDropped`: a dropped transaction's
   serial at its Commit, or the count of a dropped `PlayerCommands` message -- the same leak existed for ordinary
   commands since before the program, fixed by the same notice), and the client's `OrderWindow` (`net/orderpack.h`,
   per serial) and command window release it, so both always recover. The inbox now tests the order window before
   charging a Begin, and the record-byte window per chunk before charging it, so a frame refused for a window spends
   none of the byte budget. Tests: `server_orders_network` gains "every cut serial named once" and the 600-command
   drop -- it **fails on the 2711702e server** (`AssertionError: ('drop notices', [], 0, 64)`) and passes now;
   `server_orders_test` `dropsReleaseTheWindow` drives the real `OrderInbox` and `OrderWindow` with a spent budget
   (192 orders, all dropped): a client that hears of each drop keeps sending and recovers; one that does not wedges
   after `kOrderWindow` drops; plus the uncharged-refusal checks. `issue_selection_test`'s transcribed client uses
   `OrderWindow`. The wire change stays inside protocol 267 (unreleased; `InputDropped` is appended to `Msg`).
4. **R6: members apply in id order, not selection order** (6fbce6bd). Decision: the order is not carried (it would
   cost a second list as large as the members); the verbs whose outcome depends on it are taken out of `orderVerb`
   and keep the per-unit path in issue order -- **Load** (the transport's pickup list is built in application order:
   who boards a short-capacity transport first) and **GiveUnit** (which gifts a recipient near its unit cap takes).
   Every remaining order verb sets only its member's own state; the id order decides only the order of the members'
   path requests (registrations), part of the stage's licensed behaviour change. `selection_order_test` `issueOrder`:
   Load/GiveUnit pass through `packClick` per unit in issue order; a shuffled Squad click (evict + assign, and the
   append form) plus Stop/Stance/SetActive/Cloak/Destroy over a shuffled selection give, packed, exactly the state
   hash of the per-unit commands in issue order, in Retail and in Legion, and after 90 ticks. No scenario issues Load
   or GiveUnit, so no gate key moved for R6.
5. **Non-blocking items.** (a) the per-tick worst case one sender can force is now written under "Limits" above
   (about 250 ms of registration for 65,536 members in one tick; stage 4 must bound it). (b) the awareness crossing
   tie-break across key kinds is documented at legion.cpp's crossing rule (an explicit key always has the top bit
   set, so for one player an explicit order gives way to an AI or script per-unit command whichever came first;
   deterministic) -- kept, not reordered, since no fixture exercises the mixed case and reordering would move
   hashes for nothing measured. (c) `tools/check-determinism.sh` covers only the detmath shim (g++/clang++ x
   O0/O2/O3); its pass says nothing about `Cmd::Order` across compilers. The explicit-order path is covered only by
   same-binary runs at different worker counts (`serial_eq_workers` on every gate line; one-10000 equal at 1 and 3
   workers). **Missing:** a gcc-vs-clang run of an explicit-order fixture (open, section 8). (d) the verifier's dry
   run truncated build-o2's ninja state; this round rebuilt every target there from scratch (369 steps).
6. **Review cleanups** (cached `orderRecordBytes`, no deep copy for provisional serials, `orderSerial`'s line
   assertion) were already in 2711702e (`SelectionOrder::bytes`, the serial set in place at send, `TAK_ORDER_LINE(
   orderSerial, 4)`); nothing further.

**The trade the settle bound makes, read honestly.** Bodies that 2711702e retired outside the disc now keep their
order and stand waiting, so `stuck_end` (and, where the waiting bodies keep pressing, `livelock_end`) rise exactly
where `complete_outside_n` falls; arrivals inside the disc rise a little everywhere. Summed, the bodies that are not
honestly arrived (stuck + completed outside) fall in every row against 2711702e (scale suite, Legion,
stage 2 -> 2711702e -> fix round):

| scenario | arrived | completed | outside disc | stuck | livelock | stuck + outside |
|---|---|---|---|---|---|---|
| one-1000 | 809 -> 847 -> 875 | 999 -> 999 -> 831 | 191 -> 156 -> 4 | 1 -> 0 -> 90 | 1 -> 0 -> 15 | 192 -> 156 -> 94 |
| one-2500 | 808 -> 1970 -> 2008 | 2317 -> 2495 -> 1851 | 1519 -> 526 -> 4 | 60 -> 2 -> 407 | 17 -> 2 -> 19 | 1579 -> 528 -> 411 |
| one-5000 | 1532 -> 3684 -> 3735 | 4316 -> 4988 -> 3418 | 2790 -> 1306 -> 6 | 449 -> 0 -> 1097 | 34 -> 0 -> 29 | 3239 -> 1306 -> 1103 |
| one-10000 | 1866 -> 6973 -> 7050 | 6257 -> 9964 -> 6347 | 4394 -> 2999 -> 5 | 1741 -> 13 -> 2665 | 95 -> 9 -> 38 | 6135 -> 3012 -> 2670 |
| swap-1000 | 824 -> 781 -> 798 | 990 -> 965 -> 785 | 171 -> 192 -> 9 | 2 -> 5 -> 71 | 2 -> 5 -> 13 | 173 -> 197 -> 80 |
| swap-2500 | 1721 -> 1859 -> 1914 | 2027 -> 1929 -> 1715 | 396 -> 177 -> 0 | 5 -> 11 -> 57 | 4 -> 7 -> 9 | 401 -> 188 -> 57 |
| swap-5000 | 1308 -> 2319 -> 2329 | 1474 -> 3181 -> 2162 | 195 -> 874 -> 3 | 2521 -> 907 -> 1610 | 46 -> 41 -> 61 | 2716 -> 1781 -> 1613 |
| swap-10000 | 915 -> 1405 -> 1420 | 378 -> 1294 -> 1186 | 0 -> 29 -> 0 | 8037 -> 5841 -> 5782 | 86 -> 103 -> 105 | 8037 -> 5870 -> 5782 |
| churn-1000 | 465 -> 436 -> 488 | 1000 -> 1000 -> 574 | 539 -> 567 -> 111 | 0 -> 0 -> 219 | 0 -> 0 -> 14 | 539 -> 567 -> 330 |
| churn-10000 | 2174 -> 3587 -> 3702 | 9999 -> 9968 -> 4500 | 7828 -> 6390 -> 1045 | 0 -> 5 -> 4402 | 0 -> 4 -> 106 | 7828 -> 6395 -> 5447 |
| maze-1000 | 365 -> 335 -> 392 | 655 -> 627 -> 433 | 291 -> 296 -> 57 | 29 -> 42 -> 123 | 15 -> 19 -> 32 | 320 -> 338 -> 180 |
| maze-10000 | 1468 -> 1070 -> 1109 | 1142 -> 8947 -> 1189 | 289 -> 7878 -> 295 | 6139 -> 843 -> 7695 | 127 -> 25 -> 82 | 6428 -> 8721 -> 7990 |
| attack-10000 | 895 -> 882 -> 884 | 971 -> 954 -> 869 | 90 -> 87 -> 18 | 5 -> 8 -> 40 | 3 -> 7 -> 9 | 95 -> 95 -> 58 |

maze-10000 stays worse than stage 2 in arrivals (1,468 -> 1,109) and room occupancy (218 -> 54): that is the
simultaneous start into the compound doors, the stage 7-8 chokepoint input recorded above, no longer hidden by
retired orders.

**Frozen targets, fix round** (`legion_scale.py check`): **124 met, 82 missed, 6 report-only missed** (2711702e:
118 / 86 / 8; stage 2: 99 / 110 / 3). Against 2711702e, 12 met now: `complete_outside_n` on churn-1000/-10000,
maze-1000/-10000, one-1000/-2500/-5000/-10000, swap-1000/-2500/-5000, and many-5000 `wait.max`. **New misses
against 2711702e (new regressions of this round, recorded, not fixed):** `stuck_end` on churn-10000 (max 1,000:
4,402), churn-1000 (max 100: 219), maze-1000 (max 50: 123), one-1000 (max 0: 90), one-5000 (max 0: 1,097);
`livelock_end` on churn-1000 (14), one-1000 (15), one-5000 (29). Every one is a body that 2711702e retired outside
its disc and that now waits; the `livelock_end` ones are waiting bodies that keep pressing the crowd (walking in
place) rather than standing. Against stage 2 the new misses are churn-10000 `livelock_end` and `stuck_end`,
churn-1000 `livelock_end` and `stuck_end`, maze-1000 `stuck_end` and one-100 `arrive.t99`; one-1000's and
one-5000's `stuck_end`/`livelock_end` were missed at stage 2 too (one-1000 1 / 1, one-5000 449 / 34), met at
2711702e only because those bodies were retired outside the disc. Retail: every Retail row's state hash equal to stage 2's.

**Gate licences (fix round, `tools/scenarios/baseline.json`).** Run on the final scenario binary (byte-identical to
the one swept): 380 failures before the edits -- 270 `hash@` (41 fixtures), 105 bands, 5 offset spreads. By
fixture: 18 hash-only (the hash folds `crowdPeak`; no behaviour or work key moved: chase-*, deadend-w2-n120-*,
deadend-w2-n40-closed-*, corner-8x30, deadend-multi-4x120, fight-retarget, goalblock-200/-fight-200, mazeapproach,
mixed-spacing-04x/-05x, ringcross), 3 work-only (motion-cross/-open/-wall: the crowd bound of a crowd of several
footprints is its own packed-bound search, `slot_search_cells`; `work.legion_total` moves by at most 0.3%), 20 behaviour
(corner-4x50, deadend-w2/w4/w6-n40-room-*, doorplug-112, gen1-route-45941/-54772/-65845, goalblock-near-200,
pocket-304/-615-open/-wall, strait, strait-3x80, tail-corner380, tail-open570, tail-wave, wall-4x50). The
behaviour fixtures move the same way as the scale suite: completions outside the disc fall (tail-wave 147 -> 131,
pocket-304-wall 60 -> 20, wall-4x50 C 13 -> 9), arrivals rise a little (tail-open570 525 -> 534, wall-4x50 C 37 ->
41, D 36 -> 39), and the waiting rises (`parked_held` tail-wave 9,631 -> 22,534, strait 6,603 -> 53,117;
`still900_ever`, reversals and the held-recheck/detour work of the waiting bodies). strait's B loses 6 of 48
completions that were inside the observer's disc (its potential runs along the walls, so the packed bound is
tighter than the disc there): the one place the bound is stricter than the acceptance disc. Each key carries its
reason (scratch `fix3/license.py`; cluster S3F-267 for the five spreads); a stage-3 licence on the same key was
updated, not stacked. After the edits: `legion_check check --cumulative` **PASS** (31,768 keys, 0 failed, 2,184
ratchets, 3,082 licensed, 218 floor exceptions), `validate` 0 problems.

**Fix-round checks** (scratch `~/tak-tmp/scratch/legion-10000/fix3/`; final binaries in `fix3/final`; `build-o2`
rebuilt for every target after the last src change):

| Check | Command | Result |
|---|---|---|
| Unit and transaction tests | `selection_order_test`, `server_orders_test`, `convoy_test`, `replay_test`, `server_commands_test`, `issue_selection_test DATA` | all pass |
| Drop notices, real server | `python3 tools/server_orders_network_test.py --server build-o2/takserver --data DATA`; the same test against the 2711702e server (`~/tak-tmp/wt/legion-10000-verify3/build-o2/takserver`) | PASS; the 2711702e server FAILS at the drop notices (none sent) |
| ctest quick | `ctest --test-dir build-o2 -L quick -j12 --timeout 900` (scratch XDG, no `TAK_SETTINGS_FILE`) | **380/380** passed |
| legion_cost, battle windows | `ctest --test-dir build-o2 -R '^legion_cost$|^legion_scenario_battle-'` | pass |
| `--mpai` Inner Circle seed 1 | `CORES=20-23 mpai.sh fix3/final OUT 60` and `300` | 60 s Retail **2c39430636a3dbda**, Legion **532539f51d70840b** (both unchanged); 300 s Retail **7535cb08ee787b75**, Legion **c707e1a8063bc0c7** (both unchanged) |
| Legion verifier | `TAK_LEGION_VERIFY=1 legion_scenario F --mode legion --workers --offsets 0 [--data DATA]` for wall-4x50, motion-cross, deadend-w4-n120-room-plain, corner-1x448 (behaviour-identical build `bin2`) | no failure |
| Legion scenario gate | `sweep_legion.py fix3/bin3/legion_scenario fix3/gate3 4-15 stage3/gate2` (Legion jobs; Retail lines reused: the change cannot reach Retail, and the verifier re-ran all 135 Retail fixtures on 2711702e), `fix3/license.py ... --apply`, `legion_check.py check --cumulative` | PASS as above |
| Scale suite | `legion_scale.py run --bin fix3/final/legion_scenario --out fix3/scale-final --cores 4-23 --per-job 4`; `check --targets tools/scenarios/scale/targets.json` | 37/37 exit 0; 124 / 82 / 6; Retail hashes equal; one-10000 Legion `deliver.max` 0, `start.last` 15, apply max 36.9 ms (the activation tick, 2 ticks over budget in the run) |
| Rejoin | `CORES=20-23 stage3/rejoin.sh fix3/final fix3/rejoin` | host and rejoined client end on the same hash (**5b62dfb66c61a1fc**), server refuses nothing, the rejoined client's orders keep landing whole |
| Replay | `TAK_REPLAY_VERIFY=1 takclient replay <the rejoin host's recording> --data DATA` | 4,518 ticks, 150 checkpoints, no divergence |
| Cross-build determinism | `sh tools/check-determinism.sh` | OK, golden **dcef618cd2e4d558** (g++/clang++ x O0/O2/O3; aarch64 skipped, no toolchain; the detmath shim only, see (c) above) |

**Commits (fix round).** f611b163 (explicit-area settle bound), 6fbce6bd (drop notices + `OrderWindow`; Load and
GiveUnit per-unit), 1a74fbb0 (a one-footprint crowd reuses `areaBound`), 4e97d133 (scenario baseline) and the
commit carrying this record.

### 7.4 Stage 4 record: fair scheduling and complete budgets (brief phase 3)

**Hypothesis.** After stage 3 a click lands whole, so the navigator's own per-tick work became the next bound on
responsiveness and on the tick: a 10,000-body order's activation tick spent 39 ms (30 ms of it a synchronous first
plane build), the 10,000-body area's bound a 20 ms sort, registration had no bound at all (one sender could land
65,536 members in a tick: about 250 ms), and the field quota went to groups in id order, so under continuous load
the lowest ids took it and one start that did not fit stopped every admission behind it (`break`). Budget every
resumable stage in deterministic units, serve fields by job with classes, round robin and age promotion, account
bytes deterministically, and report what stays synchronous.

**Classification.** e74a2794 (accounting), d190169c (counting sort) and a77536e8 (resumable awareness) are exact;
d487d65d (registration budget) is exact under budget -- all 135 gate fixtures hash- and key-identical -- and a
behaviour change past it; b052597e + 2635d913 (incremental first plane builds) change behaviour only on maps over
131k cells (22 gate fixtures); 80599fdc (the scheduler, the byte allowance) changes behaviour wherever two field
jobs compete, a start's initialisation does not fit, or the allowance differs from the old 48-map cap. Protocol
268 (in d487d65d), the stage's one bump; replay_test refuses 267. Retail is untouched: no Retail code path reads
any of it (World::inTick_ is read by Legion only).

**Design (as built).** docs/legion-pathfinding.md "Legion 10,000 stage 4" and section 3.2's amendment.

**Budgets (per window: one tick, or one batch of commands between two ticks; constants in sim/navbudget.h).**

| Work | Unit | Budget a window | Over budget |
|---|---|---|---|
| Field service | relaxation (neighbour test); a field cell initialised = 1/16; a ring cell = 1 | `kFieldQuota` 384,000 (refresh classes at most `kRefreshQuota` 96,000; plane debt at most half) | the rest waits, served next tick by class / window / round robin |
| Registration | `kRegBase` 64 + join candidates + goal-resolution cells | `kRegBudget` 262,144 (4,096 plain registrations) | FIFO, drained first next tick; the unit brakes, order kept |
| First plane builds | advanceBuild cell units (3 W H passes + 1 per cell scanned + 4 per cell flooded) | `kPlaneBuildBudget` 1,048,576; a plane whose whole build fits (8 W H, maps <= 131k cells) is built at once while any is left | resumes next tick; its registrations wait |
| Awareness | candidate pairs + conflict samples + descent cells | `kAwareBudget` 262,144 (always at least one group) | resumes next tick on the same snapshot |
| Slot hand-out | formation ring cells | `kHandOutCells` 131,072 (2026-10-14 review, unchanged) | members wait a tick, steering by the field |

**Still synchronous (reported, not hidden).** Each is bounded by something other than the army's size or is
rare, and `accounting()` counts the whole-plane ones (`syncPlaneCalls`, `syncPlaneMax`):

* the incremental plane refresh after a static change (`refreshPlane`): bounded by the changed rectangles
  (clustered), charged to the quota afterwards as plane debt;
* its fallback to a whole-plane relabel when dead labels dominate (rare; `syncPlaneCalls`);
* a stale plane's rebuild on use: legacy nav-grid worlds (no placement map) after every static change, and a
  placement map after a whole-map change (`placementDirtyAll_`, the map load);
* `staticChanged`'s walk of every group (one pass, per static change);
* the awareness snapshot (one pass over the points' members and the members, every 30 ticks);
* a formation's slot ranking when its area closes (`assignFormation`: a sort of its members) and the
  destination-area bound (`packedBound`, now linear: d190169c);
* one field's initialisation (`potential.assign`, at most one whole map: 1.0M cells on refreshchurn's 1024 x 1024
  map), charged to the quota;
* `scanStill`'s striped soft-body sampling and `prune`'s 256-id cursor (unchanged, bounded per tick).

**Commits.**

| Commit | Content | Kind |
|---|---|---|
| e74a2794 | `sim/navbudget.h`; `LegionNavigator::accounting()` (work per NavWork class, per-window maxima, NavBytes, synchronous stages); scale probe keys; SCALE_WALL whole-tick and bytes | exact (hashes and every key unchanged) |
| d487d65d | budgeted registration, FIFO, `World::inTick_`; review fixes to e74a2794's accounting; protocol 268 | exact under budget (gate 135/135 identical); behaviour past it |
| b052597e | first plane builds as incremental jobs (`planeReady`, `buildPlanes`) | behaviour |
| d190169c | stable counting sort in `packedBound` / `slotsFor` | exact |
| a77536e8 | resumable awareness scan; `setBudgetDivisorForTest` / `--budget-div` | exact under budget (gate unchanged against b052597e) |
| 2635d913 | a plane whose whole build fits one window is still built at once (per-window plane budget) | behaviour on maps over 131k cells only (22 fixtures) |
| 80599fdc | the field scheduler (jobs, classes, windowed DRR, promotion, fit scan, init charge, byte allowance); navbudget_test, sched-load + ctests | behaviour |
| 44d08559 | scenario and cost baselines (protocol 268) | baselines |
| 86b409bd | legion_quota keeps exercising eviction through the byte allowance (`setFieldBytesForTest`); the Legion navigation golden retaken (gcc = clang) | tests |
| f5f7ea3b | admission under memory pressure: class-aware fit scan, strict reservation, new builds only into free window slots | behaviour |
| 2ca032e9 | scenario baseline for f5f7ea3b (battle-assault only) | baselines |
| 136ab4a1 | review fixes: jobs hold their fields through `serveFields`; planes queued registrations need are never evicted; plane-ready refreshes the FIFO's marks; areas stay open while members wait to register; pause and window count over every subscriber; aged reservation when the window is full; refresh initialisation charged to the refresh allowance; open waits counted; awareness snapshot timing; sort buffers in NavBytes | behaviour (the refresh charge only: 4 fixtures) |
| e3e85716 | a field dropped mid-call leaves the totals at once and its job is retired (follow-up to 136ab4a1); chase-50's two work licences | behaviour-neutral against 136ab4a1 (gate identical); baselines |
| bc5b23c2 | `legion_planepin` (fails on 2ca032e9: 36 plane builds for 25 classes; 25 now) | tests |
| bfa36fd9 | this record (first version) | docs |
| 1117fa31, d31ee31a, ba919843, ad1db07e, f932937f | fix round 2 (verifier rejection): shares behind a stopped scan, shared fields as victims, the per-kind in-flight cap while memory is short, the queued area recheck; sched-mix and legion_sched_mix / _pin / _inflight; see "7.4 fix round 2" | behaviour under memory pressure only (gate identical to e3e85716) |

**Measured costs per unit** (this machine, build-o2, single runs; reported, never read by the sim): one plane-build
window (1 Mi units) about 5.8 ms; one registration window (4,096 plain registrations) about 2 ms (0.48 ms per 1,000
measured at the start); the field quota (384k relaxations) 1.5-4 ms at the nav phase's p99 under load.

**The activation tick of a 10,000-body order (one-10000, offset 0, single runs on a shared machine).**

| state | apply (commands) max | whole sim tick max (apply + tick) | last member moving | longest wait |
|---|---|---|---|---|
| stage-3 head feef1b09 | 35-39 ms (tick 2) | 51.6 ms (tick 2) | tick 15 | 0 |
| d487d65d (registration budget) | 39 ms | 51.6 ms | 18 | 3 |
| b052597e (incremental plane builds) | 2 ms | 27.7 ms (tick 102: the area bound) | 21 | 11 |
| d190169c (linear area bound) | 2 ms | 15.7 ms (tick 35) | 21 | 11 |

The registration budget alone barely moves the activation tick because 30 ms of it was the plane build inside the
first registration; with the plane an incremental job (one-10000 needs two planes, 11.7M units) the tick is bounded
and the order's members start moving over ticks 14-21 instead of 3-15.

**The scheduler: how the window was chosen (5 gate offsets 0, +-1, +-2, median; scale files at offset 0..+-2).**
Arms against 2635d913 (the plane fix, the old scheduler): plain deficit round robin over every job ("drr"), the
window 12 / refresh 3 (chosen), 12 / 1.

| file | key | base | drr | 12 / 3 | 12 / 1 |
|---|---|---|---|---|---|
| many-1000 | arrive.n / stuck_end | 776 / 84 | 715 / 140 | 758 / 82 | 755 / 108 |
| many-2500 | arrive.n / stuck_end / wait.max | 652 / 1,495 / 24 | 1,223 / 836 / 21 | 1,242 / 835 / 7 | 1,233 / 844 / 8 |
| sched-load | stuck_end | 126 | 96 | 118 | 118 |
| swap-1000 | arrive.n / stuck_end | 818 / 72 | 850 / 63 | 860 / 62 | 839 / 71 |
| churn-1000 | arrive.n / stuck_end | 482 / 191 | 600 / 144 | 600 / 144 | 615 / 159 |

Plain round robin lost many-1000 (every in-flight awareness refresh progressed slowly, outlived the 30-tick cadence
and was discarded: refresh_discards 41 -> 1,960, refreshes completed 1,021 -> 675) and was rejected. Slice sizes
(32k, 128k, 384k at one offset) moved many-1000 arrivals non-monotonically (749 / 762 / 718): kept 32k, the
smallest that keeps paused builds' slice boundaries.

**Admission under memory pressure (found by the scale suite, fixed in f5f7ea3b).** The first full scale run
of 80599fdc showed many-5000's longest field wait at 982 ticks (stage 3: 2; frozen max 60) and start requests waiting
up to 1,206 ticks (many-10000: 5,613). Diagnosis, by variants (`stage4/exp/`):

* the Waiting class stopped scanning after `kFitScan` (8) capacity refusals, so waiting groups behind eight refused
  ones were never tried even when they fit (an unbounded scan: many-5000 982 -> 5 ticks; but then every fitting
  non-waiting request started too and filled memory first: many-10000 10 -> 1,377);
* a new build was admitted even when its class's round-robin window was full: it allocated its whole potential
  array and then got no service, and an unfinished field is never an eviction victim -- memory filled with idle,
  unevictable builds.

Fix: the Waiting class scans up to `kWaitFitScan` (256) refusals, the others `kFitScan` (8); a request refused for
`kReserveAge` reserves -- every later class stops, and outside the Waiting class the rest of its own class; and a
new build is admitted only while its class's window has a free slot (beyond it a request may only take a field
another group already has). Results, offset 0 against 80599fdc: many-1000 arrivals 766 -> 830, stuck 80 -> 39;
many-2500 1,184 -> 1,288, stuck 871 -> 785, evictions 629 -> 0; many-5000 1,575 -> 1,605; many-10000 1,024 ->
1,194. **The request ages were not fixed by this** (the paragraph above reports only arrivals and stuck counts): at
e3e85716 the longest start-request wait was still 5,641 ticks in many-10000 (first route 5,569) and 1,183 in
many-5000. Fix round 2 (below) says what those numbers are and gives the final values.

**The longest field wait in the overloaded rows is a lottery, also for the old scheduler.** At offsets 0, 1, -1, 2
the stage-3 scheduler's many-5000 `wait.max` is 2 / 449 / 41 / 5 and many-10000's 25 / 10 / 114 / 265: its pass of
the 60-tick target at offset 0 is luck. In those rows the demand is far beyond the quota and the byte allowance (800
squads each wanting a whole-map field: 4.4G relaxation units against 384k a tick) and a few groups that cannot head
straight for their goal stand until their field comes; which ones is decided by small differences.

Variants measured (`stage4/exp/`, offset 0 unless stated): an unbounded scan (many-5000 wait.max 982 -> 5, many-10000
10 -> 1,377); window-slot admission alone (many-1000 766 -> 830, many-2500 1,184 -> 1,288, many-10000 wait.max ->
3,177); Waiting windows 2 / 4 / 12 (non-monotone in wait.max); backfill round a Waiting reservation against a strict
one, at offsets 0, 1, -1, 2:

| policy | many-5000 wait.max | arrivals (median) | many-10000 wait.max | arrivals (median) |
|---|---|---|---|---|
| stage-3 scheduler (2635d913) | 2 / 449 / 41 / 5 | ~595 | 25 / 10 / 114 / 265 | ~700 |
| backfill in the Waiting class | 560 / 233 / 449 / 519 | 1,599 | 3,177 / 3,181 / 10 / 2,972 | 1,191 |
| strict reservation (chosen) | 5 / 5 / 991 / 949 | 1,531 | 10 / 10 / 4,156 / 10 | 1,051 |

The strict reservation was chosen as the starvation-free one (with backfill a large waiting request can be overtaken
without end); its tail in these rows remains worse than the stage-3 scheduler's at some offsets, while arrivals more
than double. Recorded as an open item (section 8): the overloaded many-squad rows need stage 6's memory.

**Review round (136ab4a1, e3e85716, bc5b23c2).** The automated reviews of d487d65d .. f5f7ea3b
(`.git/worktrees/legion-10000/codex-reviews/<sha>.md`) raised 15 findings; every one was checked against the code
and all were fixed except the eviction fixture, already fixed in 86b409bd:

| Finding (review) | Verdict | Fix |
|---|---|---|
| P1 field freed during its DRR visit: an install or an eviction drops a field the ring still points into (80599fdc, 2635d913; ASan-confirmed by the reviewer) | real (a focused reproduction; none of the 135 gate fixtures reaches it: ASan sweeps of the pre-fix binary at `--budget-div` 1 and 64, `stage4/asan/`, are clean) | the job holds the field (shared_ptr) until `serveFields` returns; a dropped field is retired at once (`orphan`: out of the totals, no visit, no window slot) |
| P1 plane eviction loop with more than 24 classes (b052597e, d487d65d) | real (`legion_planepin`: 36 builds for 25 classes before, 25 after) | `queuedNeeds`: a plane a waiting registration needs is no victim |
| plane-ready leaves waiters marked plane-blocked, so a new registration bypasses them (b052597e) | real | `planesFinished` clears their marks (also after a background prebuild) |
| formation sized before deferred members register (d487d65d) | plausible (not reproduced at the current head, where small planes are built at once; gate-neutral) | an area stays open while members of it wait (`regAreas`) |
| shared paused job paused by one subscriber's rule (80599fdc); idle shared fields take window slots (f5f7ea3b) | real | `jobPausable` over every subscriber; `jobIdle` jobs take no slot |
| aged reservation lost when the class window is full (f5f7ea3b) | real | reserve on the window-full path too |
| refresh initialisation not charged to the refresh allowance (2635d913) | real: a documented cap not kept | debit both budgets -- **the round's one behaviour change** |
| starvation not measured until it ends (2635d913) | real (accounting only) | open waits counted at the end of every tick |
| resumed awareness mixes snapshot and processing times (a77536e8) | real past the budget only | snapshot-relative stamps; displacement scaled to `kAwareScan` |
| sort buffers missing from NavBytes (b052597e) | real | counted |
| eviction fixture under the byte cap (80599fdc, 2635d913) | already fixed | 86b409bd |

Attribution: the gate at e3e85716 equals 136ab4a1's on every fixture; against 2ca032e9 aware-cross and
aware-cross-behind (offset -4), battle-assault (offset 2) and refreshchurn (every offset, no behaviour key) moved,
and chase-50's visit counters rose by 2 with every hash equal. Each review fix was toggled separately in a
diagnostic build (`stage4/c8/ls_xv*`): turning off only the refresh-allowance debit restores 2ca032e9's hashes on
all of them; no other toggle moves anything. The four fixtures stay inside their bands; chase-50's two work
licences moved (464 -> 466, 573 -> 575) with that reason.

**Checks (exact commands; scratch `~/tak-tmp/scratch/legion-10000/stage4/`; binaries `c6f/` = 80599fdc, `c7/` =
f5f7ea3b, `c9/` = e3e85716, the final code).**

| Check | Command | Result |
|---|---|---|
| Unit test of the policy | `build-o2/navbudget_test` | all 7 sections pass (fair share within one slice over 1,000 ticks; oldest first across windows; bursty load: longest gap 218 <= 456; waiting flood: the stale job served at 450 ticks; shared job; fit scan: 8 refusals a tick at most, no later request past a reservation). Mutations (cursor ignored, promotion off, reservation off; `stage4/mut/`) each fail it |
| Continuous load in the sim | `ctest -R legion_sched_load` (`tools/legion_sched_load_test.py`) | pass (c7): quota saturated 3,918 consecutive ticks; longest service gaps waiting 1 / first route 138 / refreshes 0 (bound 539); request ages waiting 104 / first route 150 (bound 900: requests now wait for a window slot, bounded by promotion); field wait 0; serial == workers. (Fix round 2: this file never runs short of memory, so promotion, the fit scan and the reservation never ran in it; legion_sched_mix now covers them) |
| Over-budget paths under the verifier | `ctest -R legion_sched_load_div` (budgets / 64, `TAK_LEGION_VERIFY=1`), and many-1000 at `--budget-div 64` and `4096` | pass; serial == workers. (Fix round 2: at / 64 no registration was deferred; the ctest now divides by 512 and asserts deferrals) |
| Exactness of the exact commits | gates at d487d65d, b052597e, a77536e8 (`sweep_legion.py <bin> gate-cN 4-19 ../fix3/gate3`) | d487d65d: 135/135 Legion fixtures hash- and key-identical to feef1b09; a77536e8 = b052597e on all 135; d190169c: one-10000, many-1000, swap-1000, maze-1000 identical |
| Legion scenario gate | `sweep_legion.py c6f/ls_gate gate-c6f 4-19 ../fix3/gate3`; `license.py ... --apply`; `legion_check.py check --cumulative --baseline tools/scenarios/baseline.json --results gate-c6f/scenarios.jsonl`; `validate`; again for c7 (`gate-c7`), c8 (136ab4a1) and c9 (`gate-c9`) | PASS at c9: 31,772 keys, 0 failed, 2,273 ratchets, 3,410 licensed, 218 floor exceptions; validate 0 problems |
| AddressSanitizer | `build-asan` (Debug, -O1 -fsanitize=address); `stage4/asan/sweep.py` over all 135 fixtures at `--budget-div` 1 and 64 with the pre-fix binary; ten workloads (refreshchurn, sched-load at 1 and 64, battle-assault, battle-field-2x250, many-1000, many-2500, churn-1000, staticblock-own, aware-cross) with c9 | all clean |
| Gate ctests, cost, battle windows | `ctest -R 'legion_sched_load|^legion_cost$|^legion_scenario_battle-|legion_baseline_reasons|legion_check'` | 75/75 pass at c6f (after the 7 cost entries in 44d08559); 77/77 at c9 |
| ctest quick | `ctest --test-dir build-o2 -L quick -j12 --timeout 900` (scratch XDG, no `TAK_SETTINGS_FILE`) | **381/381** at c7 and at c9, plus `legion_planepin` (bc5b23c2); the first run at c6f failed legion_quota and navigation_determinism, fixed in 86b409bd (above) |
| Rejoin | `CORES=20-23 stage3/rejoin.sh c6f rejoin` | host and rejoined client end on the same hash (**08d104c5c8f0991d**), the rejoined client's orders keep landing whole, the server refuses nothing |
| Replay | `TAK_REPLAY_VERIFY=1 takclient replay <the rejoin host's recording> --data DATA` | 4,517 ticks, 150 checkpoints, no divergence |
| `--mpai` Inner Circle seed 1 | `CORES=20-23 mpai.sh c6f OUT 60` (twice) and `300`; again with c7 and c9 (`CORES=16-23`) | 60 s Retail **2c39430636a3dbda** (unchanged), Legion **f75b2a11410f43b4** (three runs at c6f, again at c7 and c9; was 532539f51d70840b); 300 s Retail **7535cb08ee787b75** (unchanged), Legion **b25e18fe6f015862** (twice at c6f, again at c9; was c707e1a8063bc0c7): the new Legion baselines |
| gcc vs clang | clang 22 build of `legion_scenario` (`build-clang`, Debug -O2) against gcc 16's, `--mode both --offsets 0 --ticks 2000` | one-1000, wall-4x50, motion-cross, sched-load: every hash and key equal in both modes (closes the stage-3 coverage item: explicit orders and the scheduler under load agree across compilers) |
| replay_test | `build-o2/replay_test` | pass (268 kept, 267 refused) |
| Release build | `cmake --build build` (all targets) | builds |

**Scale suite (final code, e3e85716).** `python3 tools/legion_scale.py run --bin stage4/c9/legion_scenario --out
stage4/scale3 --cores 4-23 --per-job 4`; `python3 tools/legion_scale.py check --results stage4/scale3/results.jsonl
--targets tools/scenarios/scale/targets.json`; `stage4/scale_ab4.py fix3/scale-final/results.jsonl
stage4/scale3/results.jsonl`. 38/38 jobs exit 0; every Retail state hash equal to stage 3's. Targets **117 met, 87
missed, 8 report-only missed** (stage 3: 124 / 82 / 6). Stage 3 -> stage 4, Legion rows, offset 0:

| file | start.last | wait.max | arrived | completed | outside | stuck | livelock | fields peak | nav bytes peak (MB, deterministic) |
|---|---|---|---|---|---|---|---|---|---|
| attack-1000 | 23 -> 30 | 0 -> 6 | 99 | 99 | 0 | 0 | 0 | 15 -> 6 | 31 |
| attack-10000 | 42 -> 131 | 1 -> 9 | 884 | 869 -> 889 | 18 -> 28 | 40 -> 50 | 9 -> 14 | 31 -> 17 | 65 |
| bridges-1000 | 15 -> 25 | 0 -> 8 | 12 -> 4 | 11 -> 4 | 0 | 539 -> 535 | 44 -> 35 | 5 -> 7 | 63 |
| bridges-10000 | 45 -> 84 | 0 -> 9 | 0 | 0 | 0 | 9053 -> 9029 | 88 -> 117 | 5 -> 7 | 72 |
| churn-1000 | 46 -> 93 | 0 -> 2 | 488 -> 600 | 574 -> 735 | 111 -> 168 | 219 -> 144 | 14 -> 13 | 6 -> 7 | 18 |
| churn-10000 | 1230 -> 795 | 10 -> 13 | 3702 -> 5013 | 4500 -> 5916 | 1045 -> 1327 | 4402 -> 2626 | 106 -> 102 | 7 -> 11 | 112 |
| idle-10000 | 22 -> 41 | 0 -> 6 | 33 -> 27 | 27 -> 21 | 0 | 666 -> 654 | 31 -> 23 | 2 | 37 |
| many-100 | 9 | 0 | 98 | 98 | 2 | 1 | 0 | 8 | 1 |
| many-1000 | 19 | 0 | 788 -> 822 | 780 -> 812 | 21 -> 13 | 84 -> 54 | 13 -> 19 | 81 -> 97 | 25 |
| many-10000 | 248 | 25 -> 10 | 546 -> 944 | 267 -> 645 | 15 -> 40 | 9164 -> 7300 | 100 -> 89 | 150 -> 100 | 186 |
| many-2500 | 217 -> 18 | 13 -> 2 | 786 -> 1278 | 646 -> 1266 | 9 -> 49 | 1400 -> 703 | 50 -> 86 | 92 -> 274 | 125 |
| many-5000 | 67 | 2 -> 5 | 469 -> 1523 | 271 -> 1258 | 11 -> 44 | 3802 -> 2675 | 67 -> 107 | 99 -> 172 | 161 |
| maze-1000 | 10 -> 24 | 0 -> 15 | 392 -> 405 | 433 -> 455 | 57 -> 60 | 123 -> 95 | 32 -> 29 | 6 | 102 |
| maze-10000 | 15 -> 42 | 0 -> 16 | 1109 -> 1135 | 1189 -> 1367 | 295 -> 435 | 7695 -> 7291 | 82 -> 74 | 6 | 108 |
| mixed-1000 | 11 -> 15 | 0 -> 6 | 667 -> 640 | 514 -> 460 | 0 | 32 -> 39 | 20 -> 19 | 5 | 39 |
| mixed-10000 | 11 -> 49 | 0 -> 41 | 2254 -> 2256 | 868 -> 1910 | 0 | 6227 -> 6307 | 154 -> 97 | 5 | 232 |
| one-100 | 8 | 0 | 100 | 100 | 0 | 0 | 0 | 2 | 2 |
| one-1000 | 39 -> 41 | 0 | 875 -> 893 | 831 -> 854 | 4 -> 2 | 90 -> 80 | 15 | 2 | 8 |
| one-10000 | 15 -> 92 | 0 -> 11 | 7050 -> 7081 | 6347 -> 6377 | 5 -> 10 | 2665 -> 2657 | 38 -> 61 | 2 | 54 |
| one-2500 | 29 -> 38 | 0 -> 3 | 2008 -> 1983 | 1851 -> 1830 | 4 -> 5 | 407 -> 410 | 19 -> 20 | 2 | 17 |
| one-5000 | 49 -> 106 | 0 -> 6 | 3735 -> 3744 | 3418 -> 3389 | 6 -> 1 | 1097 -> 1087 | 29 | 2 | 30 |
| sched-load | -1 | 0 | 29 | 22 | 0 | 101 | 22 | 96 | 56 |
| swap-1000 | 20 -> 25 | 0 -> 8 | 798 -> 854 | 785 -> 833 | 9 -> 13 | 71 -> 66 | 13 -> 15 | 5 -> 7 | 44 |
| swap-10000 | 15 -> 164 | 0 -> 9 | 1420 -> 1476 | 1186 -> 1276 | 0 | 5782 -> 5893 | 105 -> 98 | 5 -> 7 | 61 |
| swap-2500 | 48 -> 72 | 0 -> 8 | 1914 -> 2032 | 1715 -> 1836 | 0 -> 6 | 57 -> 86 | 9 -> 16 | 5 -> 6 | 43 |
| swap-5000 | 15 -> 38 | 0 -> 8 | 2329 -> 2330 | 2162 -> 2133 | 3 -> 1 | 1610 -> 1542 | 61 | 5 -> 7 | 54 |

**Targets.** Newly met: many-1000 `complete_outside_n` (21 -> 13), many-2500 `start.last` (217 -> 18). Newly
missed: `start.last` in attack-10000 (131, max 90), one-10000 (92), one-5000 (106), swap-10000 (164), churn-1000
(93), and `complete_outside_n` in churn-1000 (168, max 118) and churn-10000 (1,327, max 1,180); report-only:
many-10000 `over_budget` and `tick_p99_ms` (this run's wall columns, see below). many-5000 `wait.max` (982 at
80599fdc) meets its target again at offset 0 (5), but is a lottery at other offsets (above).

* **start.last** (the last member of a click to start moving): concurrent first fields of one class now start
  together instead of one after another, and the members of a 10,000-body order, released over the same few
  ticks, crowd each other at the start (one-10000 members moving over ticks 14-21 and a long tail behind the
  front). A window of 1 restores it but serves fields first-come-first-served, the starvation this stage removes;
  building planes concurrently was measured and did not help. It is a crowd-start problem for stages 7-8
  (subgroups, admission into the moving crowd), recorded as a must-fix below.
* **complete_outside_n** in the churn rows: more orders complete (churn-1000 735 vs 574, churn-10000 5,916 vs
  4,500) and the share outside the disc is close to stage 3's (23% vs 19%, 22% vs 23%); the count crosses the
  frozen max because there are more completions. Recorded, not licensed.
* Outcomes improve where fields compete: arrivals many-2500 786 -> 1,278, many-5000 469 -> 1,523, many-10000 546
  -> 944, churn-10000 3,702 -> 5,013; stuck at the end many-2500 1,400 -> 703, churn-10000 4,402 -> 2,626.

**Wall time (report only).** The suite's wall columns are not comparable run to run (five jobs share the machine;
this run overlapped a 300 s `--mpai` and builds on cores 0-3 for part of its length, and one-10000's World::tick
showed 152 ticks over 33 ms where the same binary alone shows 0-1). The instrument for wall time is the same-core
A/B (`stage4/ab/run3.sh`: stage-3 head against e3e85716, 6,000 ticks, three workers, the arms on cores 4-7 and
8-11 at once and swapped between the two repetitions; rep0 / rep1):

| file | arm | tick mean ms | p99 ms | max ms | ticks over 33 ms |
|---|---|---|---|---|---|
| one-10000 | stage 3 | 10.41 / 11.53 | 14.05 / 15.37 | 35.5 / 40.0 | 2 / 2 |
| one-10000 | stage 4 | 11.31 / 10.14 | 14.55 / 12.79 | 19.5 / 16.6 | 0 / 0 |
| many-10000 | stage 3 | 9.85 / 12.12 | 12.04 / 14.54 | 13.6 / 15.8 | 0 / 0 |
| many-10000 | stage 4 | 13.27 / 10.19 | 20.50 / 12.42 | 22.4 / 16.4 | 0 / 0 |
| churn-10000 | stage 3 | 11.43 / 13.03 | 17.25 / 20.54 | 36.0 / 37.9 | 3 / 3 |
| churn-10000 | stage 4 | 12.62 / 11.00 | 20.20 / 17.03 | 24.6 / 21.1 | 0 / 0 |
| swap-10000 | stage 3 | 9.29 / 11.44 | 16.02 / 19.93 | 29.7 / 33.8 | 0 / 1 |
| swap-10000 | stage 4 | 10.86 / 10.58 | 14.94 / 14.41 | 18.0 / 17.1 | 0 / 0 |
| sched-load | stage 3 | 1.22 / 1.49 | 1.86 / 2.31 | 4.1 / 2.8 | 0 / 0 |
| sched-load | stage 4 | 1.60 / 1.32 | 2.49 / 2.09 | 5.6 / 5.3 | 0 / 0 |

On the same cores (stage 3's rep0 against stage 4's rep1, and the reverse) the mean moves -3% to +9% (many-10000
+3% / +9%: it admits and serves more fields and moves more bodies; swap-10000 +14% / -5%), and the worst tick
falls everywhere a big order lands (one-10000 35-40 ms -> 17-19 ms, churn-10000 36-38 -> 21-25): no tick over
33 ms in stage 4's arms, 1-3 per run in stage 3's. The whole-tick columns (commands applied + `World::tick`)
equal the tick columns in stage 4: applying a 10,000-body order now costs 2.6 ms (35-39 ms at stage 3). A
same-core check of f5f7ea3b against e3e85716 (one-10000 and many-10000, 3,000 ticks, `stage4/ab79/`) shows the
review fixes cost nothing measurable (means within 1 ms, 0-2 ticks over 33 ms in either).

**What moved in the gate fixtures, and why** (gates at each commit against feef1b09; outcome balance = keys moved
more than 10%, direction from legion_check, `stage3/outcome_ab.py --all`).

* d487d65d (registration budget) and a77536e8 (resumable awareness): nothing (no fixture exceeds either budget).
* b052597e + 2635d913 (incremental plane builds): exactly the 22 fixtures whose maps exceed 131k cells (gapsweep,
  gen1-route-*, pocket-*, refreshchurn*, serplong*, split-450-*, tail-*). b052597e alone moved 43 (attack rings,
  battles): every registration that found its plane unbuilt ran at tick T instead of at command time T-1, and every
  stamp moved; 2635d913 builds a plane whose whole build fits one window at once, as before. Balance over the 22:
  81 better / 57 worse (pocket-615-open 15 / 1, tail-open570 12 / 2, split-450-pocket 5 / 0; tail-wave 2 / 13,
  pocket-304-open 2 / 7, pocket-615-wall 3 / 7). **Diagnosis of the worse ones**: not a time shift -- the stage-3
  binary with the order issued one tick later (`shift/*.scn`) shifts every time by exactly one tick, at all 11
  offsets -- but a change of initial conditions: the order's members now register once, in the tick their plane is
  ready, after the Retail mission's first stage, instead of twice (at command time and again in that stage), and
  some offsets then crowd differently (pocket-304-open offset 0: t90 4,457 -> 6,978, left behind 6 -> 30; core-5
  median 4,664 -> 5,493; median of all 11 4,664 -> 4,683; tail-wave W.done core-5 median 2,479 -> 2,838, 8 of 11
  offsets slower). Accepted as the cost of not building a whole plane inside one call, with the numbers here; the
  crowd sensitivity is a stage 7-8 input (subgroups, admission), not hidden by building the plane synchronously again.
* 80599fdc (scheduler): every Legion fixture's hash (its state is hashed; a start's initialisation is charged, so
  field timing shifts by a little everywhere). 21 fixtures moved keys more than 10%: 90 better / 88 worse --
  the gate's fixtures rarely have competing field jobs, so the scheduler mostly perturbs them (its systematic effect
  is under contention: the scale table). The ones outside the stage-3 base's own core-offset spread:
  battle-field-2x250 (the symmetric battle's winner flips: B used to win at most offsets, now A does -- g.B.arrived
  median 36 -> 0, g.A.arrived 0 -> 19 -- with reversals 240 -> 269), battle-field-2x500 (spins +1.4%, B left behind
  0 -> 3), gen1-route-61923 (zongob completions 77 -> 71) and -54772 (reversals 57 -> 63).
* Retail: no line moved (every Retail `hash@` equal; 0 Retail failures).

Every failing key was judged against the stage-3 base's own core-offset spread (`stage4/judge.py`) before its
licence; `stage4/license.py` wrote 854 `hash@` retakes, 638 band licences, 7 offset-spread exceptions (S4-268), 2
bounds moved inside the base's spread (aware-seen `contact_other_permille` 622 -> 632, split-450-pocket
`g.A.arrived` 27 -> 26; both looser, both with the reason) and 4 new work entries, each with the reason of its
cause(s), in 44d08559.

### 7.4 fix round 2: verifier rejection (2026-10-16)

The verifier reproduced most of the stage at bfa36fd9 (tests, the legion_planepin failure before its fix, the
`--mpai` hashes, the gate, the 10,000-body runs with serial == workers) and rejected it on three findings. Each is
answered below; writing the memory-pressure test the second finding asked for exposed two further scheduler
defects, fixed in the same round. Scratch: `~/tak-tmp/scratch/legion-10000/stage4/r2/` (binaries `f0`..`f11`; `f11`
/ `c11` = the final code; mutants of it `m6_*`, `m5_nocap`; earlier variants and toggles `f*`, `m2_*`..`m4_*`,
`n_*`).

**Finding 1 -- a reservation blocked requests that need no new room. Real; fixed (1117fa31).** Once a request
reserved, `serveFields` left the admission loop (`if(budget==0||admit.stopped())break;`) and `nextClass()` stopped
every later class, so nothing could start for the rest of the tick -- not a share of an existing field
(`Start::Shared` allocates nothing) and not a refresh that could share. Now, once the room scan has stopped (a
reservation or the class's fit-scan bound), the remaining requests are still tried, but share-only: a share takes
no room, so the reserved request loses nothing it waits for (a share lands within kStillScan of a field's start,
so it can delay that field's eviction by at most kStillScan; a refresh that shares even drops its stale field). A refresh that
needs a new build still waits: it needs room. `NavAdmit::stopped()` now means "the room scan ended", with
`reserve()`; the window-full reservation no longer breaks out of the class either. Counted as
`scale.sched.stopped_shares`. The verifier's measurement stands for the old code: the reservation was active in
13,240 of many-10000's 14,400 ticks and 9,533 of many-5000's 11,300, and in those ticks no admission of any kind
could happen; the fix-round values are in the scale table below.

**Finding 2 -- the longest request wait under load was misreported and the load ctests did not exercise the
anti-starvation mechanisms. Real; answered with a new fixture, three ctests and two fixes.**

* *Misreporting.* 7.4 cited "start requests waiting up to 1,206 ticks (many-10000: 5,613)" at 80599fdc and its
  "Fix:" paragraph reported only arrivals and stuck counts, so a reader took the request age as fixed. It was not:
  at e3e85716 `scale.sched.age_max.waiting` was 5,641 in many-10000 (first route 5,569) and 1,183 in many-5000;
  neither 7.4, section 8 nor the stage report gave them. A note now says so at that paragraph; the final values and
  what they mean are below and in section 8.
* *Coverage.* `legion_sched_load` and `_div` ran with promotions 0, fit_skips 0, reservations 0 and (in _div)
  reg_deferred 0: sched-load never runs short of memory, so the fit scan and the reservation never ran in the sim,
  and the 900-tick bound it asserted would fail in many-5000/10000. New: `tools/scenarios/scale/sched-mix.scn`
  (gen_scale.py) -- 42 short-hop squads re-clicked every 120 ticks round 6 squads crossing the map, one footprint,
  under a field allowance of two whole-map fields (the new scn directive `navbytes`, or `legion_scenario
  --field-bytes`), so large requests compete with a stream of small ones for memory. `legion_sched_mix` checks:
  serial == workers; the mechanisms ran (fit skips 27,648, reservations 5,686 ticks, request promotions 47,375
  request-ticks, shares behind a stopped scan 12); builds in flight within their windows (13 / 1); admission liveness
  (200 <= 563 ticks, below); and every class's longest request wait within a bound DERIVED FROM CAPACITY:
  `steps(class) x kAgeStep + kReserveAge + rounds x (kFieldTenure + kStillScan)`, `rounds = ceil(4 x
  request_cells_max / (A - 4WH))`. Why: requests are served oldest first (a lower class rises one class per
  kAgeStep); one refused for room reserves after kReserveAge and nothing takes new room before it; every finished
  field is a victim kFieldTenure after it starts (a share lands within kStillScan of a field's start, so sharing
  extends that by no more), so in one round of kFieldTenure + kStillScan ticks all the memory held at its start can
  be freed, which admits at least A - 4WH (one largest request short of the allowance) of the queue's cells. The
  queue is measured (`request_cells_max`: the most cells the requests listed in one tick need together); the test
  also checks from the file that every build finishes within its tenure (kFirstInflight whole-map builds at half
  the quota: 233 <= 300 ticks). Values: waiting 889 <= 2,790, first route 983 <= 2,940, aware repair 289 <= 3,090,
  stale refresh 0 (queue peak 7.92 whole-map fields, 8 rounds).
* *The test can fail* (mutants of the final code, `r2/mut6/`): the reservation removed -- first route 4,497 >
  3,930 (and reservations 0, shares 0); requests by id instead of age -- waiting 4,604 > 2,790; request promotion
  removed -- fails only its counter (the ages stay inside the bound: promotion is not what bounds them in this
  file; navbudget_test's promotion section fails); head-of-line blocking (scan bound 1) -- passes (navbudget_test
  section 6 catches it); shares behind a stopped scan removed -- shares 0. The two defects below each have their own
  test that fails without the fix.
* *Defect A, found by the new fixture: shared fields were never eviction victims* (fixed in 1117fa31). A victim had
  to be a group's exclusive field (`ownRefs==1`: "dropping a shared one frees nothing"). Fields shared by groups
  that never finish therefore pinned memory for good, and a reservation waiting for that memory stopped every
  admission: many-1000 under a two-map allowance (`--field-bytes 778752`) made 15 admissions in 6,300 ticks, none
  after tick ~1,600 (two shared fields held 617 KB of 779 KB; the reserved request needed ~300 KB). Now a shared
  field is a victim once every holder (the groups under its route key whose field is it) is past its tenure, and it
  is dropped from all of them (`evictionHolders`; `TAK_LEGION_VERIFY` compares the holders with a walk of every
  group). `legion_sched_pin` (many-1000, two-map allowance, 3,000 ticks) checks admission liveness -- no run of
  ticks with a refusal for room and no new build longer than kFieldTenure + kStillScan + the longest build (429):
  299 with the fix, 1,797 without (fails).
* *Defect B: builds piled up past the windows* (1117fa31, refined in f932937f). f5f7ea3b admitted a new build only
  while "its class's window has a free slot", counted per class. A first build admitted as a first route is a
  Waiting job the next tick (its members stand still for it), and a refresh promoted out of its class leaves its
  slot, so the windows looked free again and admitted more every tick while each served only its oldest: 151
  unfinished, unserved builds held many-5000's whole 128 MiB at tick 525 (memory dump, `r2/exp/dbg-many5000.err`),
  so its reservations waited for builds nobody was serving; many-2500 had 78 refreshes in flight, battle-assault
  40 first builds. Fix: a new build is also admitted only while its kind has room over every class --
  kFirstInflight = 2 kWindow first builds, kRefreshInflight = 2 kRefreshWindow refreshes, the two windows each kind
  can be served in -- but only while more than half the byte allowance is in use. The unconditional cap (1117fa31)
  was measured first and cost outcomes where memory is plentiful: sched-load stuck 101 -> 162 (consistently, 9
  offsets), many-1000 arrivals -9% (5 offsets, the refresh cap) -- an early-admitted build costs nobody room
  there, and refreshes promoted out of their class are served from the main quota; capping first builds only kept
  the sched-load cost. Gated on memory (f932937f) it changes no gate fixture (battle-assault never reaches it) and
  many-1000 and sched-load equal the head, while many-2500's refreshes in flight at such admissions fall 78 -> 6
  and its arrivals rise 1,278 -> 1,456 (offset 0; offsets 1 / -1: 1,250 / 1,355 against 1,259 / 1,234, stuck 911
  / 699 against 824 / 823: better at two of three). `legion_sched_inflight` (many-2500) checks that no build
  admitted while memory is short takes its kind past the cap: 6 with the fix, 78 without (fails).
* *Deferred registration* (also finding 3): `legion_sched_load_div` now divides the budgets by 512 (64 never
  deferred a registration in sched-load: its 480 bodies arrive as small clicks) and asserts it: reg_deferred 6,986,
  plane waits 580, longest registration wait 135 ticks, under `TAK_LEGION_VERIFY`, serial == workers.

**Finding 3 -- smaller record items.**

* The two `bound` rules moved inside the stage-3 base's own core-offset spread are now named in 7.4's licence
  paragraph: aware-seen `contact_other_permille` 622 -> 632, split-450-pocket `g.A.arrived` 27 -> 26.
* A queued unit asked to register again kept its first `RegEntry` area: real (deterministic, minor); fixed in
  1117fa31 -- `requestRegister` rechecks the area against the current leg (`legArea`) and moves the `regAreas`
  count when it changed. No gate fixture reaches it (no gate fixture defers a registration).
* The _div ctest now reaches deferred registration (above). The verifier's own run, many-1000 at `--budget-div
  4096` under `TAK_LEGION_VERIFY`, at the final code: 1,815 deferred registrations (as the verifier measured at
  bfa36fd9), serial == workers.

**Classification and attribution.** All four fixes are behaviour changes, but only where memory runs short or a
registration is deferred; the accounting added with them is observation only (never read by the sim). Protocol
stays 268: the branch is unreleased and the stage-3 fix round set the precedent of staying inside the stage's
protocol; replay_test still refuses 267. The 135-fixture Legion gate at the final code equals the stage-4 head's on
every fixture, hashes AND keys (`r2/gate-f11` against `stage4/gate-c9`): no gate fixture runs short of memory or
defers a registration. (The unconditional cap of 1117fa31 moved battle-assault -- five work counters licensed in
ba919843 -- and those licences were reverted in ad1db07e once the cap was gated; `baseline.json` equals
bfa36fd9's.) The four `--mpai` hashes are unchanged, so no new baselines. Where the fixes act (memory pressure,
the overloaded rows), each was toggled alone on the affected fixtures: shares behind a stopped scan (`m6_noshare`:
sched-mix shares 0), shared victims (`m6_noshareevict`: pin liveness 1,797), the gated cap (`m5_nocap`: many-2500
refreshes 78). Retail is untouched: every sim change is in `LegionNavigator` (the rest is test tooling); every
Retail state hash in the scale suite and both Retail `--mpai` hashes are unchanged.

**Commits.** 1117fa31 (the four fixes, accounting, `--field-bytes`, `navbytes`), d31ee31a (sched-mix,
legion_sched_mix / _pin / _inflight, _div at 512, navbudget_test section 6), ba919843 (battle-assault licences for
the unconditional cap) and ad1db07e (their revert), f932937f (the cap gated on memory; legion_sched_inflight on
many-2500), the record commit at the branch head (this section, section 8, docs/legion-pathfinding.md).

**Checks (final code = f932937f; binaries `r2/c11`, `r2/f11`).**

| Check | Command | Result |
|---|---|---|
| Policy unit test | `build-o2/navbudget_test` | all pass, incl. "shares behind the reservation still start, at once" |
| Scheduler ctests | `ctest -R 'legion_sched\|navbudget'` | legion_sched_load, _div (budgets / 512, VERIFY), _mix, _pin, _inflight, navbudget_test: pass |
| Mutation proofs | `tools/legion_sched_load_test.py r2/<mutant>/legion_scenario <file> [args]` (`r2/mut6/`) | each fix's test fails without it: no reservation (mix: 4,497 > 3,930), by id (mix: 4,604 > 2,790), no promotion (mix: counter 0), no shares behind a stop (mix: 0), no shared victims (pin: 1,797 > 429), no cap (inflight: 78 > 6); head-of-line (scan bound 1) passes the sim tests, navbudget_test catches it |
| Verifier mode | `TAK_LEGION_VERIFY=1 TAK_SIM_WORKERS=3 legion_scenario <file> --both-exec --scale` (`r2/verify2/`): many-1000 `--field-bytes 778752 --ticks 3000`, sched-mix, many-2500, many-1000 `--budget-div 4096` | no verify failure, serial == workers in all four (hashes 2213ac5991c7b9d6, 5b9b80cddfd9f177, 9653bb97b4493ceb, f092e707fd6dffe6); shared evictions 2 / 1; deferred registrations 1,815 in the div-4096 run |
| Legion gate | `sweep_legion.py r2/f11/legion_scenario r2/gate-f11 4-19 ../gate-c9`; `legion_check.py check --cumulative`; `validate` | every Legion fixture's hashes and keys equal gate-c9's; PASS: 31,772 keys, 0 failed, 2,273 ratchets, 3,410 licensed; validate 0 problems |
| `--mpai` Inner Circle seed 1 | `CORES=20-23 mpai.sh r2/c11 OUT 60` and `300` | 60 s Legion **f75b2a11410f43b4**, Retail **2c39430636a3dbda**; 300 s Legion **b25e18fe6f015862**, Retail **7535cb08ee787b75**: all four unchanged (no new baselines) |
| ctest quick | `ctest --test-dir build-o2 -L quick -j12 --timeout 900` (scratch XDG, no `TAK_SETTINGS_FILE`) | **383/383** (381 + legion_planepin + legion_sched_pin) |
| Legion ctests | `ctest -R 'legion_sched\|^legion_cost$\|^legion_scenario_battle-\|legion_baseline_reasons\|legion_check\|legion_planepin\|replay_test\|navbudget\|legion_doc_links\|legion_quota\|navigation_determinism'` | **86/86** (every per-fixture gate check, cost, battle windows, replay_test 268 kept / 267 refused) |
| AddressSanitizer | `build-asan` legion_scenario `--workers`: sched-mix, many-1000 `--field-bytes 778752 --ticks 3000`, many-2500, battle-assault, sched-load, sched-load `--budget-div 512` (`r2/asan/`) | no report; hashes equal the -O2 build's |
| Release build | `cmake --build build` (all targets) | builds (all targets, f932937f) |
| Scale suite | `legion_scale.py run --bin r2/c11/legion_scenario --out r2/scale5 --cores 4-23 --per-job 4`; `legion_scale.py check --results r2/scale5/results.jsonl --targets tools/scenarios/scale/targets.json`; `stage4/scale_ab4.py stage4/scale3/results.jsonl r2/scale5/results.jsonl` | 39/39 jobs exit 0; Retail hashes equal; targets 120 met / 90 missed / 2 report-only (below) |

**Scale suite (final code, offset 0, `r2/scale5`).** 39/39 jobs exit 0; every Retail state hash equals the stage-4
head's; every Legion row except the three overloaded many-squad rows is identical to the head's (all keys). Targets
**120 met, 90 missed, 2 report-only missed** (stage-4 head: 117 / 87 / 8). Newly met: six report-only wall targets
(the head run's wall columns were contaminated, 7.4); no target the head met is lost except three new misses:
many-10000 and many-5000 `wait.max` and many-2500 `complete_outside_n` (below). The changed rows (head -> final):

| file | wait.max | arrived | completed | outside | stuck | livelock | fields peak | request age waiting / first route / aware repair | reservations (ticks) | fit skips | quota spent on relaxations |
|---|---|---|---|---|---|---|---|---|---|---|---|
| many-2500 | 2 | 1,278 -> 1,456 | 1,266 -> 1,459 | 49 -> 61 | 703 -> 589 | 86 -> 57 | 274 -> 205 | 2 / 2 / 154 -> 2 / 1 / 957 | 0 | 0 | 33% -> 37% |
| many-5000 | 5 -> 1,234 | 1,523 -> 1,639 | 1,258 -> 1,615 | 44 -> 86 | 2,675 -> 2,572 | 107 -> 89 | 172 -> 186 | 1,183 / 1,173 / 1,149 -> 1,578 / 1,576 / 630 | 9,533 -> 0 | 325,043 -> 0 | 99% |
| many-10000 | 10 -> 5,203 | 944 -> 1,226 | 645 -> 985 | 40 -> 73 | 7,300 -> 6,859 | 89 -> 69 | 100 -> 119 | 5,641 / 5,569 / 5,542 -> 5,895 / 5,894 / 632 | 13,240 -> 0 | 266,551 -> 0 | 99% |

* Memory is no longer the limit in these rows: with no unserved builds holding the allowance, no start is refused for
  room (reservations and fit skips 0); what remains is the quota, saturated 99% of the run in many-5000 and
  many-10000. Arrivals and completions rise (many-10000 completions +53%, many-5000 +28%, many-2500 +15%), stuck
  and livelocked bodies fall.
* Aware repairs wait far less where the quota is saturated (5,542 -> 632 in many-10000): their refresh builds are
  no longer queued behind piles of unserved first builds. In many-2500, where memory is short but the quota is not,
  they wait longer (154 -> 957): the per-kind cap keeps refreshes to 6 in flight, and an aware repair's stale field
  steers meanwhile; arrivals there still rise 14%.
* First routes' request ages rise in the saturated rows (many-5000 1,173 -> 1,576, many-10000 5,569 -> 5,894). Part
  of that is measurement: the age runs from the request to the build's admission, and at the head most builds were
  admitted at once and then waited unserved past the windows, so the head's ages stopped counting long before
  service began; now admission is close to service. What the age is, is below.
* `wait.max` (the longest run of ticks a body stood in the navigator's Waiting state -- no field reaches it and the
  straight heading is blocked) is a new miss at offset 0 in many-5000 and many-10000. A group whose request waits
  in the drain queue heads straight for its goal while it can; a body blocked meanwhile waits as long as its
  group's request. At the head the same rows already reached 949-991 (many-5000) and 4,156 (many-10000) at other
  offsets (7.4); at the final code, offsets 1 / -1 / 2: many-5000 1,193 / 1,189 / 1,173 and many-10000 5,169 / 5,233 / 5,314 (`r2/offs/`) -- no longer a lottery: within
  5% of the offset-0 value at every offset, as is the request age (many-5000 1,578-1,603, many-10000 5,822-5,895);
  arrivals many-5000 1,533 / 1,638 / 1,686, many-10000 1,117 / 1,290 / 1,209 (head medians ~1,531 and ~1,051, 7.4).
  It is the drain time below seen by the
  unluckiest body, not a scheduling stall: the fix that removes it is less work per request (stage 6's tiles and
  shared routes), not a different order.
* many-2500 `complete_outside_n` 61 (max 50): more completions (1,459 vs 1,266) at about the same share outside the
  disc (4.2% vs 3.9%); recorded, not licensed (the churn rows' item of 7.4 is the same pattern).

**Request ages under continuous load: what the overloaded rows' numbers mean.** Measured at the final code (offset 0):

* many-10000: the queue at its peak held 761 start requests needing 283M field cells -- 408 whole-map fields of
  692k cells. At the run's measured 6.92 relaxations per initialised cell that is 5,098 ticks of the whole quota
  (kFieldQuota 384,000), and the quota was spent on relaxations in 14,264 of the run's 14,400 ticks. The oldest
  request waited 5,895 ticks: the drain time of the peak queue plus the requests that arrived while it drained (aware
  re-plans, groups whose field was evicted). Service inside the scheduler stayed fair meanwhile: no job that asked
  for work waited more than 148 ticks for a slice in its class (`gap_max`), requests are admitted oldest first, and
  nothing was refused for room. In a work-conserving FIFO the last of N equal requests on a saturated server waits
  N x (work per request) / capacity whatever the order; that is what 5,895 is -- about 400 whole-map fields of about
  14 quota-ticks each, as the verifier estimated -- not starvation.
* many-5000: 366 requests, 196 whole-map fields, 1,439 quota-ticks at 7.04 relaxations a cell; quota saturated 99%;
  oldest request 1,578 ticks; longest slice gap 159.
* The service-age bound that holds there is the drain bound, `(work queued ahead) / kFieldQuota` plus promotion,
  which grows with the army and the map, not a constant. sched-load's 900-tick constant holds only because its queue
  is small (47 requests, 87 quota-ticks); legion_sched_mix checks the capacity-derived bound where memory is the
  limit, and section 8 carries the overloaded rows as stage-6 work (less work per request).

## 8. Next step

Resume from branch `task-legion-10000` (pushed; stages 1-4 done, stage 4 including its fix round 2 at f932937f).
Build `build-o2` in the worktree, read sections 3 (3.2 as amended, 3.3), 7.4 (and its fix round 2) and the stage-4
section of docs/legion-pathfinding.md. Stage 5 (brief phase 4): the
route-query interface (`RouteView`, section 3.3), consumers migrated one at a time, exact first. Stage 4 leaves it:

* **Demand is still the fixed 32-cell list** (`Field::demand`, `demand()`): stage 5's per-subscriber demand (3.3)
  replaces it; the scheduler's Waiting class (a member standing for want of its field or of a paused build) is
  where that demand is served, so a demand never needs its own scheduler.
* **Jobs are fields, keyed by serial**, their scheduler state (`deficit`, `lastServed`) in the Field. Stage 6's tiles
  must become jobs of the same `serveFields` loop (a job kind with its own `NavVisit serve(allowance)`), not a
  second scheduler; `NavBytes::tiles` / `portals` are waiting for them.
* **The quota, not memory, now limits the many-squad workloads** (fix round 2): with unserved builds no longer
  holding the allowance, nothing is refused for room in many-2500/5000/10000; many-5000 and many-10000 spend 99% of
  the quota on relaxations and their requests wait the queue's drain time (must-fix list below). Stage 6's sparse
  tiles and shared intermediate routes -- less work per request -- are the fix.
* The slot hand-out keeps its per-point rate (`kSlotsPerTick` 32 a tick, after the area closes; the cell budget
  `kHandOutCells` is per tick and round robin): a 10,000-body area hands out its last slot ~300 ticks after
  activation. Unchanged by stage 4 on purpose (it is the formation's progressive assignment, stage 8's to replace).
* The Retail mission's first stage re-registers every member of an order in the tick after it is applied (the
  order's own registration is then wasted work: 2 registrations a member, 20,000 for one-10000). Under the budget
  that doubles the registration windows a big order needs; skipping the first would move every fixture by a tick,
  so it is left, recorded here for stage 9's report.

**Must-fix inputs for stages 6-8 (frozen targets that still miss; numbers from the final stage-4 scale run, 7.4 fix
round 2, `r2/scale5`).**
* `start.last` within 90 ticks for big single clicks: attack-10000 131, one-10000 92, one-5000 106, swap-10000
  164, churn-1000 93 (and churn-10000 795, many-10000 248, misses since stage 1). Cause (7.4): simultaneous starts
  of a big order crowd each other; stage 7-8's subgroups and admission into the moving crowd.
* `complete_outside_n` in churn-1000 (168, max 118), churn-10000 (1,327, max 1,180) and many-2500 (61, max 50):
  more completions at about the same share outside the disc; stage 8 (arrival areas).
* **Request age and `wait.max` in the quota-saturated rows** -- many-5000: request age 1,578-1,603, `wait.max`
  1,173-1,234; many-10000: request age 5,822-5,895, `wait.max` 5,169-5,314 (offsets 0, 1, -1, 2; frozen `wait.max`
  max 60). Not starvation: the quota is spent on relaxations 99% of these runs and the age is the drain time of the
  queue ahead (many-10000: 408 whole-map fields, ~5,100 quota-ticks at the peak), with service inside each class
  fair (longest slice gap 148-159 ticks). The fix is less work per request -- stage 6's sparse tiles and shared
  intermediate routes -- not another order; the scheduler keeps it a FIFO drain (legion_sched_mix proves the bound
  where memory is the limit).
* `stuck_end` and `livelock_end` in every 1,000-plus row, `arrive.n` in 21 rows, `arrive.t99` in the one-* and
  swap-* rows, the bridges rows' gate samples (0): inherited from stage 1 (frozen targets that never passed);
  stages 6-8.
* Wall time: mixed-10000 stays far over budget (tick mean ~60 ms, inherited). The fix round's wall columns are
  single runs on a shared machine (report only).

**Open items the fix round leaves (recorded, not hidden).**
* The per-kind in-flight cap applies only while more than half the field allowance is in use; with memory to spare
  builds may still pile up past the windows (sched-load 90 first builds in flight, many-1000 ~20 refreshes) because
  capping them there cost outcomes (sched-load stuck 101 -> 162, many-1000 arrivals -9%: early-admitted refreshes
  are promoted out of their class and served from the main quota). The half threshold is a judgement, not a tuned
  value; stage 6's tile jobs should revisit it (a build past its window holding a whole potential array is the
  thing tiles remove).
* Under that cap many-2500's aware repairs wait up to 957 ticks (head 154) while its arrivals rise 14%: the stale
  field steers meanwhile. A per-class refresh window for aware repairs separate from stale refreshes would shorten
  it; not done here.
* The queued-area recheck (finding 3) has no in-sim test: no gate or scale fixture defers a registration whose leg
  then changes without a cancel. It is covered by review and by the verifier-mode runs (`--budget-div 4096`).
* Head-of-line blocking (fit-scan bound 1) is caught by navbudget_test only; the sim fixtures pass with it.
* Request promotion is not what bounds the ages in sched-mix (its mutant fails only the promotion counter there);
  navbudget_test section 4 is the test that fails without it.

**Coverage.** The gcc-vs-clang check of explicit orders and the scheduler under load is now done (7.4); a
Windows/macOS run of the scale files has not been made (remote hosts: memory note `remote-test-hosts`).
