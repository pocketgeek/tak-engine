# Legion pathfinding (mode 4)

Retail (mode 0, the default) and Legion (mode 4) are the two pathfinding modes. Legion is a ground-navigation mode
built for how Total Annihilation: Kingdoms actually moves armies: a flat tile mosaic whose relief is baked into 16 px
placement cells, square-ish footprints (shipped ground movers are 1x1 to 4x4, mostly 2x2 and 3x3), slope and water
limits per unit type, features, walls and buildings as blockers, and right-click orders that send dozens to thousands
of units at once. Code: `src/sim/legion.{h,cpp}` (the navigator), `src/sim/legion_hier.h` (sectors, regions and
portals), `src/sim/legion_route.h` (route queries), `src/sim/legion_routebins.h` (the route index),
`src/sim/navbudget.h` (budgets and the field scheduler) and `src/sim/convoy.h` (arrival areas); hooks in
`src/sim/sim.cpp` next to the Retail dispatch, and the group right-click in `src/client/gameview_hud.cpp`.
Retail's own movement is described in [pathfinding-port.md](pathfinding-port.md) and
[retail-engine.md](retail-engine.md).

This document is organised by mechanism: the legality plane (1), goals, groups and routes (2), steering (3),
traffic (4), formations (5), passages (6), rings (7), flyers (8), scheduling (9) and determinism (10), then the
tests, the instruments and the known weaknesses. Each mechanism names the rules, constants, functions and tests
that hold it; where an alternative was measured and not taken, the section says so in a line, because the same idea
tends to come back.

## Goals

1. Single units and armies route around obstacles, and plan as a group.
2. No unit gets stuck against terrain, including jagged terrain, unless it is actually trapped.
3. A unit that is stuck stops. It never spins or rocks back and forth. The same holds inside large crowds.

Legion is judged by arrival time first, then by how Retail-like the motion is (standing, sliding, reversals, lane
order).

## Scope

Supported: surface movers with a footprint of 1..8 whose current leg is one of the **mission goals** below: ground
units (`Domain::Ground`), and, on maps with a placement plane (every real map), boats (`Domain::Water`) and
hovercraft (`Domain::Hover`). Non-square footprints also need the placement plane. Flyers stay native: flight is not
pathfinding (section 8 lists where Legion changes flight). Still delegated to Retail: transport legs of flying or
water carriers, and boats and hovercraft on legacy terrain-only test worlds. Retail behaviour is bit-identical to a
build without Legion: per-tick traces and hashes are compared on each Legion change.

**Boats and hovercraft** are mobility classes like any other. The plane is built from `World::mobilePlacement`'s own
predicate with the type's limits: a boat needs `minWaterDepth` (default 13) under every footprint cell and has no
maximum depth; a hovercraft has no depth limit either way and uses `maxWaterSlope` over wet cells and `maxSlope` over
dry ones. So a keel never enters a shelf, a beach or a river mouth too shallow for it, and a hovercraft crosses a
shoreline wherever the slope allows. Everything else is shared with ground units: incremental plane updates (piers,
docks and their yards), static components (two seas joined only by a strait are one component; a lake is its own),
goal resolution (a boat ordered onto land stops at the nearest water it can float in, or approaches the nearest
reachable point of its own sea), fields, formation slots, settling, following and passage lanes. Groups are per
plane, so a mixed selection of ground units, boats and hovercraft ordered to one point forms one group per class
sharing the point's area. Legacy terrain-only test worlds key their nav-grid plane by footprint alone and cannot tell
a keel from a foot, so there boats and hovercraft stay Retail. Tests: `legion_navalclearance` (plane equals
`mobilePlacement` for keels of depth 4/13/30, footprints 2..4 and 3x2, and hovercraft with two water-slope limits, on
an archipelago with shelves, a strait, a river mouth and a beach; then 240 steps of piers, buoys and docks with
yards, incremental plane equal to a rebuild), `legion_navalisland` (fleets of 12 3x3, 8 4x4 and 24 2x2 boats round an
island and its shelf and through a 10-cell strait: all arrive, legal every tick, no spin, serial == workers),
`legion_hovershore` (hovercraft from land down a beach to sea and back up another beach; boats up a narrow river
mouth; boats ordered onto the beach stop at the water's edge) and `legion_navalmissions` (boat patrol laps round the
island, fight-move across it, an attack approach and a guard escort, each served by Legion).

### Mission goals

The mission goals (`LegionMission`):

| Kind | Legs | Goal | Ends by |
|---|---|---|---|
| Move | plain Move legs, group right-clicks, queued legs | the point | Legion arrival raises `0x500` |
| Fight | fight-move (`attackMove`) legs | the point, shared area per command | Legion arrival, or the native circle (`groundMissionAccepts`), raises `0x500` |
| Patrol | patrol legs, single or group | the waypoint, shared area per command | as Fight; the body passes through (no anchor, cells released) |
| Attack | explicit and auto-acquired attacks, chases | the target unit, re-seeded | combat: `tickCombat` stops the body in range; Legion never completes it |
| Guard | guard/follow | the guard ring's point on the guard's bearing (`World::guardRing`), re-seeded | guard: stops in Follow_Ground's ring; Legion never completes it |
| Build | MobileBuild approaches (`buildRectangle`): single, queued, mana build area; a ground builder's HelpBuild leg (`helpSite`) | the rectangle's `navigationCell`, else its nearest legal reachable perimeter origin | the rectangle itself (`accepts`): `0x500`; anywhere else `0x200` |
| Repair | ground repair legs on RepairUnit's rectangle round the target (`installRepairApproach`), and a patrolling builder's automatic repair detour | as Build | as Build; `tickGroundRepair` re-installs the rectangle (a new controller, so a new member) when the target moves or changes cell |
| Reclaim | feature and corpse reclaims (also those an area reclaim queues) | the feature's point | work: `tickReclaim` starts at reach; Legion never completes it |
| Load | passenger to its carrier, ground carrier to its passenger | the transport circle's centre | the native circle: `0x500` on `transportMission`; elsewhere `0x200` |
| Unload | ground carrier's surface-unload approach | the drop point | as Load |
| Exit | automatic production exit step (a mobile producer's output with no rally to start, or an idle unit sent out of its next output's way) | the generated exit point | arrival `0x500`; or, without headway for a crowd window, once the whole birthplace is clear (or after two windows when an order is queued behind it) |
| Park | PARK sequences (factory output, released cargo) | a factory output's own spot over the ring band (`World::legionParkSpot`: two footprints apart, nearest the door first, never in the door lane, reachable, free of claims and bodies; growing past the band when full), else the ring's navigation point | the ring (`park->ring`): `0x500`, a spread PARK only at its spot; elsewhere `0x200` and the next spot (`ctest:legion_park`) |

Legion is the route provider only. The mission handlers keep every mission rule: when to fire, when the unit is in
range, target acquisition during a fight-move, patrol rotation, retirement. Legion replaces the native search and
steering for these legs; the native mover never runs for them. A melee attacker's leg to a building is taken by the
native navigator.

* **Arrival.** A kind that completes raises the same `0x500` event on the leg's mission that the native mover does.
  Attack and guard never complete in Legion: reaching the goal or a crowd there is a hold (zero speed, order kept),
  and combat or guard ends the approach. Legion never declares arrival outside the rules of sections 4 and 5 or the
  native goal predicate.
* **Moving goals.** Attack and guard goals follow a unit. The member is keyed by (kind, target id) instead of a
  controller, and the group by (player, plane, kind, target id, reach bucket, the target origin it was seeded at). On
  a 16-tick grid line, once the target's origin has moved max(2, min(D/8, 8)) cells (D: the member's Chebyshev
  distance to it), the group re-seeds in place: its seed or its ring moves, a refresh builds the new field while the
  old one steers, and its members keep their group, slot and clocks (section 7).
* **Groups and areas.** Groups never mix kinds. Fight-move shares a destination area (formation slots) per command,
  as Move does. Each patrol lap gets its own group and field, because the outbound and return legs of one patrol
  share an issue tick and a field seeded at both ends would pull bodies toward the wrong end; the waypoint area is
  still shared per command, and an arriving patrol body releases its cells. Attack and guard take no area and no
  packed slots.
* **Events.** Legion masks the search-failure events (`0x2600`) that make Retail enlarge the goal circle, for Move,
  Fight and Patrol. A blocked Legion army therefore never "arrives" because the circle grew.
* **Work and logistics.** Build, Load, Unload and Park are *exact*: the only arrival is the mission's own geometry
  (build rectangle, transport circle, park ring). Reaching Legion's seed or settling against a crowd outside it
  raises the native failed-approach event `0x200` instead, and the handler decides exactly as after a failed native
  search (build if within reach, re-roll the park ring, retry the pickup). Load/Unload events go to the leg's
  `transportMission`, as in the native mover. Help, repair and reclaim legs approach on the same rectangle controller
  as a build. These kinds are *solo*: each body gets its own group and field (a teammate's seed is not on this body's
  rectangle or within its reach). Embarked units are never members. A work/logistics body that cannot accelerate
  (accel 0) is not crept forward, so a released passenger stays on its landing point. Production exits carry their
  birthplace (`Order::productionExit`), so a held-up exit never blocks the factory lane: it ends only once clear of
  the birthplace, or hands over to an order queued behind it. Mission semantics (build start, placement, reach, park
  rotation, transfers, retirement) stay in the handlers.
* A leg is supported only if the legs ahead of it are route corners (for Move, plain corners).
* Area reclaim and the mana build area need no kind of their own: their approach steps are plain Moves and the work
  they queue is Reclaim/Build. A patrolling builder's automatic repair detour (`patrolRepair`) is an ordinary Repair
  leg. The leash stays in `tickGroundRepair`; when a mobile ally moves, RepairUnit's look re-installs the leg's
  rectangle round where it now stands (a new controller, which re-registers the member), so Legion follows it
  (`legion_patrolrepair`, `-moving` variant).
* The interface for adding a kind is a `LegionMission` kind plus a `Policy` entry (see `legion.cpp`). Member and
  group state of non-Move kinds is folded into the checksum only for those kinds.

## 1. The legality plane

Legion works on the same coordinate the mover's legality test uses: the footprint **origin**
(`footprintOrigin(x, footX)`). For each mobility class (footprint size, slope/water limits, domain) it builds a
static plane: `legal[origin]` is true if `World::mobilePlacement` would accept the footprint there with no mobile
bodies present.

* Each cell's legality is computed by calling `retailMobilePlacement` itself on a 1x1 footprint, with the same cell
  reader as `World::mobilePlacement` but with the entity slot empty. The retail predicate is a per-cell loop plus a
  bounds test, so a footprint is legal exactly when every covered cell is. Two integer run-length passes (rightward
  runs of at least `footX`, then `footZ` consecutive such rows) give the footprint plane. This is the "clearance
  transform", and it is exact for rectangles.
* Structures are static bodies, stamped with their yard maps by the mover's own rule: a `.` yard cell is open, and a
  closable `c`/`C` cell is open while the unit script holds the yard open (`yardOpen`), as in
  `World::mobilePlacement`. The yard state is folded into the structure signature, so opening or closing a yard
  updates the plane.
* Maps without a placement plane (legacy test worlds) use `NavGrid::fits`, the predicate the legacy mover uses. Only
  square footprints are supported there.
* Each plane labels its **static components**: a flood fill under the same step rule the field and the mover use.
  Each component records its origin count and bounding box. Labels are union-find roots: two origins share a
  component when their roots are equal, never when their raw labels are, and nothing stores a raw label across
  changes (section 2 compares components through a stored seed cell).
* Moves on the plane are 8-connected. A diagonal step requires both orthogonal neighbours to be legal, so there is no
  corner cutting.

`legion_clearance` checks that the plane matches `mobilePlacement` at every origin, with slopes, water and jagged
walls, for footprints 1x1 to 4x4 and one 3x2 footprint.

**Static changes.** World records the rectangles of changed feature cells (corpses placed or decayed, features
installed or removed, structures built or destroyed, yards opening or closing; `placementEpoch_` is bumped by
`setMapPlacementFeatures`, `removeMapFeature`, `placeMapFeature` when the install succeeded or replaced something,
`blockCells` and `setTerrain`). `refreshPlane` recomputes only those cells and the footprints covering them, and
repairs component labels locally (union-find merges compared by root; a bounded interleaved search settles possible
splits). It splits the changed rectangles into clusters (boxes grown by the relabel margin plus the footprint that
overlap) and refreshes each cluster as if it had arrived alone, in the order of each cluster's first rectangle, so a
corpse in one corner and a building in another do not cost a whole-map flood. A new static epoch starts only when
some plane's legality actually changes, so a non-blocking corpse triggers nothing. `refreshPlane` reports each cluster
as its own box (`StaticChange`) and says whether any origin lost legality (a **closure**) or gained it (an
**improvement**); section 2 routes them. The incremental refresh is checked against a fresh rebuild after every one
of 1,600 random changes (`planeincremental`), and the labelling by `planeprebuild`, `staticblock`, `pens`,
`penstale`, `churn` and `churnfield`; `refreshchurn:work.refresh_completed.total` exercises the clustered repair.

**Plane builds and plane debt.** A class's first plane build is one-time setup: a plane whose whole build fits one
window's plane budget (`kPlaneBuildBudget`, 1 Mi units: maps up to 131k cells) is built at once inside the call;
a larger one is built by the resumable background build at `kPlaneBuildBudget` units a tick for the wanted planes,
in demand order, right after `syncStatic`, while the registrations that need it wait (section 9). Its cell layer runs
in 16-row chunks on the SimPool workers (`computeRows`; the structures are resolved once, in id order, before the
fork). Later plane work is charged as **plane debt** against the field quota (section 9): an incremental refresh
its own work, a stale plane's whole rebuild on use (legacy nav-grid worlds, or after a whole-map change) `2*W*H`
(build plus labelling). A class's first build is never charged: charging it delayed the first fields by a tick,
which split orders into different groups and cost jagged 2000 about 30% of its arrivals. The carried debt is clamped to half a
tick's quota, and at most half the quota pays debt in one tick, so field work always gets at least half. At most
`kMaxPlanes` (24) planes are live; at the cap, the least recently used plane that no group refers to and no waiting
registration needs (`queuedNeeds`) is replaced, and a building plane is never evicted (`legion_planepin`).

**The hierarchy** (`src/sim/legion_hier.h`). Each plane is cut into K x K sectors (K = 32). A sector's legal origins
fall into regions -- the 4-connected parts inside the sector, which under the step rule is exactly connectivity
inside it -- and regions of neighbouring sectors are joined by portals, the maximal runs of boundary rows (or
columns) legal on both sides. Portals carry every legal connection (a diagonal crossing needs an orthogonal one
beside it), so two origins share a plane component exactly when their regions are joined by portals; clearance stays
the plane's rectangle predicate. Sectors are built on first use and rebuilt on next use after a legality change
touches them; identities are generation-aware: a relabelled sector gets a new generation, and a plane slot reused for
another mobility class a new PlaneId generation. Tests: `legion_hier_test` (random planes, corner cases,
incremental rebuilds, the search), `planeMatchesRebuild` (the lazily maintained hierarchy joins exactly the plane's
components after random churn) and a rotating sector check under `TAK_LEGION_VERIFY`.

**Lone trees and rocks** (`wallOrigin`, `loneCell`). For the turn and passage logic (sections 5 and 6) a blocked cell
counts as wall contact, outward normal and free width only when its run of blocked cells is not all blocking features
of TDF category trees, plant(s) or rocks (`RetailMapFeatureType::treeOrRock`) inside `kLoneSpan` x `kLoneSpan` = 7 x
7 cells (the largest shipped tree or rock, ararock07, is 7x6; trees reach 5x5). Terrain, water, structures, the map
edge or any other feature in the run make it a wall. A run links the blocked cells the plane's body cannot pass
between (at most `footX` columns and `footZ` rows apart, so their illegal origins touch): a forest or hedge whose
gaps are narrower than the body is one run and stays a wall, and so does a tree that close to a cliff, a building or
the map edge. Bodies path and collide round a lone tree as round any blocker; only the arcs ignore it. Taking runs as
plain 8-connected runs made forests of small trees no wall at all (`legion_forestedge`).

## 2. Goals, groups and routes

**Goal resolution.** A requested goal on an illegal origin is replaced by the nearest legal origin within 24 cells.
Past that radius the search widens over square ring perimeters to the whole map, but accepts only origins in the
unit's own start component, so a click deep inside a lake or a cliff mass walks to the nearest point the unit can
actually reach, as Retail does. The goal is unresolved (`-1`, trapped at once) only when nothing reachable exists or
the unit starts on an illegal origin. Goal resolutions are cached per (plane, requested origin, start component) for
the current static epoch. The cache is a pure function of the plane, so it is not hashed; it is cleared on an epoch
change, on plane reuse and at 4096 entries.

**Nearest reachable approach.** If the resolved goal lies in a different static component from the unit, and the
unit's own component has at least 256 origins (`kApproachRegion`), the unit becomes an **approach member**. Its goal
becomes the component's nearest origin to the requested one (`nearestReachable`: exact minimum squared distance, ties
to the lower cell index, square rings clipped to the component's bounding box). It walks there and its order then
drops (section 4). A unit in a component smaller than 256 origins, a sealed pocket, holds where it stands instead.
The real goal is kept in `Member::real`. An attack on a target outside the members' region is not an approach: the
goal is the region's nearest point, the group plans a ring round the target as usual and the order is kept, as
Retail keeps it (section 7).

**Groups.** Every supported leg is registered with Legion (`registerMove`). A member joins an existing group only if
all of these hold:

* same player, issue key and mobility plane (the issue key is the order's arrival-area key, below);
* its goal is in the same static component as the group, compared live through a stored seed cell
  (`Group::compCell`, falling back to the first seed still legal), never through a stored label;
* the group and the member are both approach groups, or both are not;
* its goal origin is within 16 cells of the group's goal bounding box;
* the group's field has not started, or the goal is already a seed;
* the group has fewer than 256 distinct goal origins (`kGroupSeeds`), or the goal is already a seed.

A right-click on a selection is normally one group per footprint class; so is each leg of a queued group command. A
single unit is a group of one. A member whose own goal it cannot reach therefore never shares a field with a
reachable teammate's goal, and a stand-in approach point never shares a field with real goals. Groups are indexed by
the equality part of the join predicate, all fixed at creation (player, plane, kind, approach, and the ring target
and bucket for a reach kind or the issue key otherwise: `joinIndex`); a member walks its entry in ascending id order
and applies the whole predicate, so the first group to pass is the first of all groups. `TAK_LEGION_VERIFY` runs the
brute-force walk on every join and checks the index against a scan of the groups. The 256-goal cap chunks goals in
registration order (a known weakness when unit ids are not spatially coherent).

**Pruning.** `tick()` checks up to 256 members per tick, in id order from a hashed cursor, and drops any whose unit is
missing, dead, embarked, orderless or no longer supported. The death edge also calls `cancelPath`, which reaches
`LegionNavigator::cancel`.

### Convoys: selection orders and arrival areas

In a Legion net game a click is one explicit **selection order** (`net::Cmd::Order`, `net/lockstep.h`): the
player's intent over a whole selection, with an identity (issuing player, serial), a template command (verb, target,
point, queue flag), a shape and the exact membership.

* **Client** (`GameView::ClickScope`, `flushClick`). The commands one input event (or one autoplay step) issues are
  one click. `net/orderpack.h`'s `packClick` turns each run of commands that differ only in their unit into one
  order; the right-click move is `OrderShape::Offsets` at the click (the sim recomputes the per-unit offsets, so no
  coordinates travel per member). Retail and offline play send per-unit commands, so Retail is bit-identical. The
  outbox is one FIFO of commands and orders: commands go under the rate credit and the 512 window, an order goes
  whole as its transaction under its own window (64 orders / 384 KiB outstanding, `orderSendWindow`), and nothing
  overtakes what was issued before it.
* **Transport** (`net/protocol.h`, `server/orders.h`). `OrderBegin` / `OrderMembers` (member ids as varint deltas,
  at most 2,048 a message, strictly increasing, so a duplicate cannot be encoded) / `OrderCommit` (count + FNV digest)
  / `OrderAbort`. The server validates the framing, the template, serials (increasing per connection and above the
  slot's highest activated serial, which a reconnect keeps), counts and the digest; malformed fails the connection,
  as does an order in a Retail game. Over the byte budget or the window the transaction is dropped with a log line and
  the sender is told (`Msg::InputDropped`, which also counts dropped `PlayerCommands`), so the client's window
  (`OrderWindow`, by serial) never holds a dropped order; the windows are tested before the budget is charged. A
  committed order is one item of its client's command FIFO; `closeTick` takes it whole while the tick's order budget
  (96 KiB of records, shared by the room, first pick rotating) holds it and the client's per-second command budget has
  room for its members (otherwise it waits, never dropped), and that tick is its activation tick. An ordinary command
  behind it keeps its place and its own 64-a-tick budget. Chunks never reach a bundle, so no partial order is ever
  visible; a disconnect drops the open transaction and the queue with the connection.
* **Sim** (`sim/matchsetup.cpp` `applySelectionOrder`). At activation every member gets the template command in
  increasing id order. The verbs whose outcome depends on application order -- Load (the transport's pickup list is
  built in application order) and GiveUnit (a recipient near its unit cap) -- are not order verbs and keep the
  per-unit path in issue order. Ownership and existence are checked per member by the ordinary path (a dead, gone or
  given-away member is skipped). Inside `World::beginOrder` / `endOrder` every leg the member gets for itself carries
  the order's identity: `Order::orderSerial` (hashed when set) and, for an order with a click point (Move, Fight,
  Patrol), its arrival area, which the order joins once per class at its own click (`World::stampIdentity`); a leg of
  another verb (an Assist's approach move) or of another unit keeps the per-unit identity.
* **Arrival areas** (`sim/convoy.h`, the convoy table). Every order is stamped with `Order::convoyTick`, the key of
  its arrival area. An explicit order's area is keyed `kExplicitArea | serial` (unique). Several orders of one player
  and class to one point within the table's window (`kGapTicks`/`kSpanTicks`) still share one area, and one order is
  never split. Per-unit commands (the AI's, which all land in one tick, and mission scripts') are grouped by
  inference: one value for every order one click gave, ground and air, per player and order class (Move, Fight from
  `attackMove`, Patrol from `patrolTo` and the flyer branch of `patrol()`), keyed by the tick the area opened. A
  patrol's return leg copies its outbound leg's value and never opens an area. `issuedTick` stays the identity of
  the order's own uplink part. The table is anchored on the area's first shared point: `legionSharedClick` in
  `legion.h`, which the client's right-click also calls, says which units get the click itself (surface movers with
  footprints 1..8); the rest get the click plus their offset from the selection centroid, clamped to 60 px per axis
  (flyers: retail's unclamped spread within sqrt(3000 n) px, `sim/retailflyerspread.h`). Shared orders join within
  one cell of the anchor, offset orders within 60 px + 1 cell; before an anchor exists, within 60 px + 1 cell
  (shared) or 120 px + 1 cell (offset) of every point taken, tested on the area's bounding box, per axis. An area
  stays open while the tick is at most `last + 9` (up to 8 command-free ticks: the 512 window with a 16-tick round
  trip) and at most `first + 32`; closed areas leave the hashed table at the start of the next tick. Lookups go
  through two ordered indexes (anchored areas by anchor cell, unanchored ones by the 256 px tile of their first
  point); each order's probes plus candidates count as `legion_convoy_tests` (crowdbench) and `work.convoy_tests`
  (the scenario observer); the declared bound is per order (p99 at most 16, max at most 64), which
  `legion_scenario` reports as `convoy.tests_max`, `convoy.over16_permille` and `convoy.over64` and baseline.json
  gates. `TAK_LEGION_VERIFY` checks every join against a scan of the table and the indexes against a rebuild every
  tick.
* **Identity consumers.** Points, groups (issue key), soft-arrival ownership, formation slots, the slot wait
  (`commandOpen`, by area key), flight stations (bucket key), patrol return legs (copy the identity) and the hash key
  on the area key. The pinwheel's part (`Member::part`) is the tick a leg's command was applied in, so an order
  (applied whole) is one part however big, and orders applied in one tick share one. The awareness same-selection
  rule (one player, within `kConvoyTicks`, within `kConvoyCells`) applies only between per-unit commands; two
  explicit areas are one selection exactly when they are the same area.
* **Shared destination areas.** An explicit order sent within a cell of the point where an earlier explicit area of
  the same player still gathers (its point live, or its arrivals settled there) takes that area's key and exact point
  (`sharedArea` over `destArea`, hashed): one command, one formation and crowd bound, and the first order's settled
  bodies and held queue are not obstacles or traffic to the second.

Tests: `selection_order_test` (codec, packing, Retail equivalence, Legion identity, membership, same-tick replacement /
Stop / queue / patrol, delivery), `server_orders_test` (the transaction state machine), `server_orders_network` (a
real server: whole activation, FIFO without starvation, abort, window, drop notices, member-budget delay, malformed,
Retail refusal), `convoy_test` (each click shape both as an order and per unit: flyers first with saturated offsets,
opposite corners, all-air, the 974-unit window at round trips 0/8/16/24, patrol, two clicks 48 px apart, and the
index against the scan) and `issue_selection_test` (the mirror against the transcribed client).

### Fields

Each group gets one **integer distance field**, seeded at every member's goal origin, computed with Dial's bucket
queue using step costs of 5 (orthogonal) and 7 (diagonal) over the static plane, with no corner cutting. All members
share it: a 2,000-unit order costs one field per footprint class and per 256 distinct goals, not 2,000 searches. The
256-goal cap exists because a field is "distance to the nearest seed of anyone": seeded with an army's whole goal
lattice it pulls every body to the lattice's near edge (a 64-goal cap gave a row drift of 4 cells in
`legion_formation`).

* **Potentials** are 32-bit (`kUnreached`, all ones, means no route), 4 bytes a cell: a connected route of any length
  on any map gets a finite potential. `serplong.scn` (two bodies at the head of a 30-lane serpentine at potential
  87,356) walks it and arrives; `legion_longroute` checks the long case on every commit, `serplong-control.scn` (16
  lanes) is its control.
* **Growth.** Fields grow outward from the group (A* aimed at the group's bodies, a consistent heuristic), units may
  steer on a partly built field once their own cell is final, a unit no field reaches yet follows a proven straight
  segment, and group orders start moving on the first tick.
* **Windows.** A field covers only the bounding box of its seeds' static region grown round the group, the only area
  it can ever reach; cells outside read as unreachable (Unknown to the route queries). A body the window does not
  reach widens it: the widening restarts as a first field, not as a refresh, so the body never steers by a field
  that cannot reach it.
* **Sparse storage** (`RouteGrid`). A route's potentials live in K x K tiles aligned to the hierarchy's sectors,
  allocated when the builder first writes one; an unwritten tile reads none.
* **Soft costs.** A field charges soft obstacles of other commands (still bodies, standing jams, mover corridors,
  ring discs: sections 4 and 7) at `kSoftFactor` (4) per step inside and `kSoftNear` (3) within a cell of one; its own
  command's arrivals and jams are exempt, and liftable allied flyers are left out (section 8).
* **Shared fields.** A group starting a field takes another group's field when it serves it as well: same plane at
  the current static epoch, same seeds, a window holding this group's, the same soft rule (the same command unless
  neither has settled arrivals of its own), started within one soft scan period. A finished field comes first, else
  one still building (advanced once, whoever's turn it is). Fields are `shared_ptr`; the share's start tick is hashed.
  `battle-field-2x60:work.fields_shared.total` counts them.

### Route queries

Movement consumers do not read a field's cells; they ask a **route** (`src/sim/legion_route.h`) what they need. A
`Field` is a `RouteGrid` (its window, potentials, build state, serial, epoch, work count) plus the builder's own
state; only the builder (`startField`, `advance`, `serve`, `coveredFor`) touches the cells. A `RouteView` answers,
kept apart:

* `estimate` (direction guidance: exact where settled, a real way's length where reached but unsettled -- descent is
  sound on it, as on a half-built field);
* `known` (exact or none, never a tentative value);
* `covered(cell, margin)` (settled with that much key to spare: every cell within cost margin/2 of it is final);
* `reach` (Unknown / Reachable / Unreachable: Unreachable only on a complete route that spans the cell's static
  component and was built on the current geometry; Unknown never retires an order);
* `progress` (a distance with its metric and the route serial: route-wide by definition, so a tile chain gives the
  chain its own metric and adds the exit's remaining distance, and a tile-local zero is never read as standing on a
  goal);
* `geometryEpoch` (freshness, apart from all of the above).

**Stated regions.** The consumers that read past their own settled cell -- the passage gate, passage lanes and
pinwheel, awareness, the late-soft-block probe, packed slots, reach rings, the formation entrance, a ring member's
walk to its spot -- run on a usable route (complete, or paused once it covered its group). Each states the region it
needs as `(cell, margin)` -- the lanes `kLaneCover` past the body (asked for only when a wall end or a passage lies on
the way, `laneTrigger`), awareness `kAwareCover` ahead of the centroid, the entrance its open-ground test's radius
round the point, a reach group its ring's spots -- and asks a paused build for it (`need`: a demand entry, served in
the FirstRoute class through `needIds`, so a stated region does not take the Waiting class's place ahead of standing
members). It reads the region through certainty checks: `certainValue`, `certainReach` and `descend`'s check take a
value as final only where the cell is settled, or proven unreachable (illegal, or outside the route's component on
the current geometry). A consumer that meets any other read acts as on a route not yet built (no lane, slot, ring
spot, corridor or entrance this time) and the route runs on to done (`Field::runOn`, hashed): no tentative or unknown
value is acted on. A formation's entrance waits for its region (`kEntryPending`, hashed). A paused build's demand
(`RouteDemand`) is one entry per cell (the widest margin), in arrival order, indexed, uncapped. `serve`'s cover scan
is incremental within one call, so a 10,000-body group costs O(members) per call, not per slice. Tests:
`legion_route_test` (the view and the demand on building, paused, complete, bounded and stale routes) and
`legion_route_paused` (the consumers on paused routes: verify on, serial == workers, every consumer asked for its
region, no read uncertain).

### Hierarchical routes and tiles

A long route need not be one field over a rectangle of the map. A low-demand build whose window has 128k cells or
more is **planned** (`planRoute`, `routePlan`): the hierarchy's region search (Dijkstra over regions, portal midpoints
as entries, at most 65,536 regions) links every member's region to the goals' regions. The route is then

* a **window** (one exact field over the bounding box): every high-demand route (one exact field the whole crowd
  shares), an approach group (its goals unreachable), small windows, a search that does not reach every member
  (`route.fallbacks`), or a corridor that would hold more than half the window;
* a **corridor** (a low-demand group planning round movers while memory has room, a reach ring, a short trip): the
  same field restricted to the sectors round the region tree (a 32-cell halo, more for a pinwheel formation) and round
  the destination (the crowd's packed disc plus the halo); exact within the corridor, bounded (an origin outside it is
  Unknown, never Unreachable). Big crowds never take corridors (a 10,000-body army lost a fifth of its arrivals in
  one);
* a **tile chain** (low demand): the destination's own exact field over the sectors its crowd needs (the packed disc
  plus 8 cells), then one shared **tile** per transit region of the tree. A tile is the exact field of one region
  toward the far side of the portal it leaves by, each seed starting at a boundary cost, read with the chain's offset
  (`RouteGrid::offset`) so its value is route-wide. A tile leading into another transit region is **aimed**: its
  boundary cost is linear along the portal, falling toward the next region's exit, so its lanes run parallel toward
  that exit (an octile distance to the exit's middle drew every body of a squad into the middle of each portal). One
  leading into the destination's field is **exact** (the field's values at its seeds). The offset is the largest
  (estimate - boundary cost) over the seeds, so a cell beside a seed reads more than the seed: every legal descent
  step strictly lowers the estimate, across tiles too, and ends on a goal -- no loop, no false minimum (`verifyDescent`
  under `TAK_LEGION_VERIFY` on every finished chain and repair). The regions one portal off the transit regions are
  the chain's halo: their tiles are attached when a member steps into one (a repair, queued and served before the
  field quota), so a body pushed off its way is back on the route without a rebuild. `RouteGrid::fin`: an attached
  tile is final everywhere it reaches.

**Search or field by demand** (`routeDemand`, `Group::routeMode`). The bodies that would read the route -- the
group's members and those of every other group with the same goals -- at most 48 take a chain (or a corridor); a
group that took the field keeps it until that falls to 24 (hysteresis); a high-demand route needs no search. Chains
have a count cap of their own (16,384; the byte allowance bounds their storage), so one-body routes cannot hold every
field slot. A group planning round movers takes a corridor while memory has room, and a chain with local corrections
once more than half the field allowance is in use. Big orders keep one exact field; many small ones take chains over
shared tiles. A route of fewer than two transit regions outside the destination is a short trip: its corridor or
window.

**The tile caches** (`Tile`, `tileFor`). A tile is keyed by everything its values depend on, compared whole after a
hash match: the plane identity, the sector, region and both portal sectors' generations, the portal run, the boundary
costs (an aimed tile's aim, an exact tile's list), the soft level of every origin of the region as the route's command
sees it, and the route's mover corridors that reach the region. A tile leaving region R by portal P toward the same
next exit is the same tile for every route, whatever its destination. Tiles with no soft level and no mover corridor
in their key are terrain alone, the same for every command and player; those with them are the route's traffic
correction of the region. They live in two caches (`staticTiles`, 48 MiB; `corrTiles`, 16 MiB), so traffic never
evicts terrain, and a correction is shared only by routes whose whole traffic view is identical (its key holds the
view), so no route reads another player's view. Each cache keeps its unreferenced tiles (an explicit route count) in a
set ordered by (last use, insertion) and evicts the oldest, after the requesting chain has attached its tile. A
sector where a chain reads two regions gets a tile of the route's own, counted in the field allowance. Hashed: the
caches' counts and an insert/evict digest.

**Sharing and widening.** A group with the same goals, soft rule and corridors takes another group's corridor that
holds all of its plan's sectors, or a finished chain that already gives every one of its members an estimate (a share
allocates nothing). A body a corridor or chain does not reach first gets its halo repair (a chain), waits for the
refresh its group is due if a static change touched the route (Unknown is not cut off), else the route is rebuilt
round it (`Group::widens`, at most three); then the whole component's window takes over, as for any bounded window.

Tests: `legion_hier_test`, `legion_route_test` (sparse reads), `legion_chains` (eight squads cross a wall to eight
destinations: chains built, transit tiles shared between squads with different destinations, every body arrives, the
descent check and the class check on), `legion_chainschurn` (the same with a wall dropped across the routes while the
chains build and lifted later), `legion_chainplane` (25 mobility classes take turns, the 25th reuses plane slot 0: no
chain reads a tile of the previous class).

### Static changes and routes

A static change reaches only the routes it can alter. `staticChanged` tests the routes against each changed box at
tile granularity (`fieldTouched`); the routes tested are the candidates of a route index
(`src/sim/legion_routebins.h`: per plane, 32-cell bins listing the groups whose field or next covers a bin, windows
and corridor / chain tiles grown by the one cell `fieldTouched` reaches), visited in ascending group id order; changes
without boxes (legacy worlds, the first epoch) walk every group. `legion_routebins_test`, `legion_routeindex` and
`TAK_LEGION_VERIFY` check that every route a change reaches is a candidate.

* A **closure** reaching a route makes it stale at once. A finished field keeps steering its group while its
  replacement builds in `Group::next`, and is swapped out when the replacement is done. This is safe because a stale
  potential can only misdirect: the plane and `commitGroundStep` decide legality. A group's first field, if half
  built, keeps building on the new plane and is marked stale so that a clean rebuild follows (a first field built
  across epoch changes mixes old and new legality until then). Arrival slots are marked stale and rebuilt on the new
  plane around the cells already claimed.
* An **improvement** leaves every step the route describes legal and every estimate the length of a real way: the
  route keeps steering, its "unreached" stops proving anything (`RouteGrid::touched`: Unknown, never Unreachable),
  and its refresh waits `kImproveDefer` (150) ticks (`Group::improveTick`, hashed), coalescing churn; a member the
  touched route does not reach, or a member blocked by the blocked rule (section 9: an improvement may be its way
  out), asks for it at once (`legion_staticidle`'s second gap, `legion_improvedefer`).
* A refresh installed after its build began is stale again only if a closure reached it while it built
  (`Field::closed`), so churn elsewhere does not make every refresh born stale.
* **Chains.** A chain region whose sector was relabelled since the plan is re-resolved on the current geometry
  (`resolveChainNode`: the portal run on the same boundary overlapping the old one most, still leading into the region
  the route continues in) and its tile rebuilt. A chain refreshed for a closure keeps its route: its plan is repaired
  (`repairPlan`: regions re-resolved, window and digest kept) and the build rebuilds only the dirty tiles, the clean
  ones coming from the cache; it is searched again when traffic or an improvement asks (`Group::reroute`: an
  awareness or late-soft-block re-plan, a promoted improvement) or when the repair fails (a transit portal closed, or
  a member stands in a region the plan does not hold). A repaired or re-resolved chain is versioned, not exact: its
  tiles come from more than one geometry epoch, each descent-valid on its own build; it is touched and never proves
  Unreachable (`legion_chainresolve`).
* **Trapped orders** re-register only after an improvement whose box touches their component's box grown by the
  footprint (`trappedReopened`, over a per-plane log of recent improvements; a stale plane's rebuild counts as a
  whole-map improvement): a closure cannot open their way, so under churn elsewhere the 5-minute grace still runs out
  (`legion_trapped`).
* Under a change every tick a refresh keeps building, is installed still stale, and another follows: one finishes
  about every 85 ticks (`refreshchurn:work.refresh_completed.total`; `refreshchurn-control` completes none).

## 3. Steering

Each update a supported unit:

* goes **straight to its own goal** if the footprint sweep from its current position to the goal is legal. The sweep
  is an exact supercover in origin space using 16.16 integers, and an exact corner crossing must clear both side
  cells. Reach is 160 cells, or 640 cells (`kFormationLineCells`) for a member with a formation slot (section 5), and
  the line is rechecked only when the origin cell changes. A single unit on open ground therefore moves in a straight
  line, and a formation keeps its shape instead of funnelling into a file down the shared field.
* otherwise **descends the field**: it takes the legal neighbour with the steepest descent per unit of path length (an
  orthogonal drop of 5 equals a diagonal drop of 7). Ties go to the neighbour nearest the unit's own goal, then a fixed
  direction order (first-found ties pull bodies toward other members' goals and fold a formation into a file;
  `legion_formation` guards this). The unit then string-pulls up to three further descent cells that remain in a
  legal straight line, and aims there. Each aim point is on a segment that has already been proven legal, so the unit
  never aims into a wall. At a jagged edge the steepest legal descent runs along the wall, so the unit slides instead
  of pressing into it. Inside the flat zero-potential goal region, a member whose own goal is not in line steps to the
  legal neighbour that most reduces its distance to that goal.
* **moves along the proven direction exactly.** The member's course (`Member::course`) turns toward that direction at
  the authored rate, and a step that turns the course is capped while the body still faces away from it (full speed
  within 22.5 deg, half within 67.5 deg, an eighth otherwise). Because turning lag never moves the footprint off the
  proven line, the units cannot orbit a waypoint, overshoot into terrain or rock on a step.

**The facing cap** applies to three steps only: a step that turns the course, a plain mover's flow-around shuffle (a
body of a point with no formation assigned, `plainFlow`; brisk round a still target) and an attacker's in-reach
press. Every other unturned step -- a lane pass, a side-step, a detour route of any length, a formation member's
shuffle, a press against a queue, a move over at a merge -- keeps travel speed. A plain mover's shuffle at travel
speed lets the rear of a plain crowd overtake its front at an obstacle: near goals settle first and box the far ones
in (the acceptance world groupdetour settles 45 of 64 instead of 64, and crowdtrap reads spin), so it keeps the cap;
an in-reach press at speed glides past reach (section 7).

**Committing a step.** The step is committed with `commitGroundStep(..., strictDiagonal=true)`, the authority Retail
uses. Before committing, Legion checks the destination origin, and both orthogonal origins on a diagonal, against
occupancy and `mobilePlacement`. Every Legion motion path, including pass steps, shuffles, detour routes, presses,
swaps and yield steps, goes through `commitGroundStep` (a swap through its own exact exchange, section 4); none writes
a position directly. An update's step that would change both origins at once where one side origin is illegal (a
proven line passing a wall's end a fraction of a pixel from the corner) is blocked like a body; when neither
flow-around nor anything else frees the unit, it crosses only the legal axis that update, the other coordinate
stopping at its origin's edge, exactly as the line itself passes (`legion_wallend`: one body and a 12-body column
round a `nav().block` wall's end on a legacy plane, without turning in place). Applied first instead of last, the same
clip changes motion at corners that flow-around already resolves. A unit standing on an origin the plane rejects
(inside a yard, or under a new structure) leaves it through Retail's own steering.

**Speed-matched following.** In `drive()`, a body that has a slower body of the same player one or two cells ahead
along its step, heading within 45 deg of its own, matches that body's speed (never below half its own) instead of
running up and stopping dead. A leader slower than half this body's speed cap is not followed: matching a creeping
body chains whole queues down to a crawl at door corners. No state; nothing to fold into the checksum.

### Facing follows motion

Retail's mover (4dba80, with the turn clamp at 4d91b0 and the step at 4dab0d / 4dab2b, ported as
`World::steerGround` and `retailGroundStep`) turns the heading toward its steering point and steps along the heading,
so a Retail body always faces where it goes; the only exception is the blocked step's per-axis clamp (a slide along a
wall). Legion keeps the route decision in the member's *course* and turns the body to its motion (`drive` and
`faceMotion` in legion.cpp):

* The course sets the facing cap, the leader test and the opposing-traffic test, so pacing and every decision read
  the course, not the body. A committed step toward the route turns the body toward it, as it turns the course, and a
  body back within a turn of its course takes the course itself; a new leg starts its course from the body's heading.
* An unturned step at a quarter of travel speed or more (`kFacePace`: a travel-speed flow-around shuffle, a pass, a
  detour walk, a move over at a merge) turns the body, at its turn rate and with a 5.6 deg dead band, toward its
  actual motion averaged over about 4 updates, when that average shows a way (at least 1/32 px, `kFaceFloor`, and at
  least half the mean step length: a dodge one way and back nets nothing and turns nothing). It turns only on a step
  it made, so a held, stuck or trapped body never turns in place.
* A body aiming a weapon that needs its facing (`Unit::faceRequest`) and a member of a fight, attack, guard or patrol
  leg keep the route rule: their facing serves their weapons (shots wait on it, and retail scores targets by the turn
  from it, 412a77). A creep sideways (a side-step, a shuffle at the cap), a yield step (`serviceYields`) and a body on
  an engaging leg still slide unturned.
* Readers that judge the route by the body's heading read the course instead: `World::updateLegionStill`'s livelock
  window (`courseTurnCorrection`: a turn toward a sideways step is no headway and no to-and-fro) and the formation
  flyers' direction of travel (`planLegionFlightStations`).
* Rejected: averaged motion for every step, committed ones included (a body reversing its route creeps backward at
  the cap for about 8 updates before its average flips; `legion_ringroutespeed`), turning on creeps at the cap (a
  side-step in a jam turns the body 90 deg and back: crowdheld spin), and facing motion on engaging legs (battles plan
  more, past the combat-lag tolerance). Turning the body on detours, passes or shuffles by the route rule instead of
  by its motion reads as spinning.

`legion_facing` (a crowd round a wall end, a group round a standing block, a turn-400 group round a wall's top at five
start shifts) bounds sideways and backward steps and walk-in-place.

### Standing, gait and livelock

A walker's gait is a Create-started COB controller (araarch's `MoveWatcher` and `MeleeControl`). Every 100 ms it
reads GET 29, the horizontal speed as a percentage of the maximum, and above 5 it plays a whole walk cycle, about
1.3 s. Retail reports a body stopped only after two refused steps in a row (the refusal bit, `bodyBlockStreak`), so a
refused Retail mover's gait stops in its queue even though its internal speed stays above 0.

**Net headway** (`World::updateLegionStill`, `LegionHeadway`). For every Legion ground body Legion measures NET
headway over a window of 8 ticks, from where the window began. A window that ends without a displacement of 8 paces
(a pace is a quarter of the base speed: two full-speed steps) or a turn one way of 4 ticks at the full turn rate makes
it report stopped: GET 29 is 0 and MoveRate is 0, as with the refusal bit, so it stands, and it requests no turn of
its scripts (`turnReqBam` 0, so the display VM sends no `TurnDirection`). The tick its headway reaches either mark it
walks again (a body pivoting on the spot steps round). A body stepping back and forth over a cell edge, or turning
left and right toward two aims that swap, gains nothing by net headway, so it stands. The rule applies to every Legion
ground body, whatever blocks it (another body of any player, terrain), and its state is hashed for every Legion ground
unit (the navigator reads the livelock). Whether the body stands in its scripts (`Unit::legionStill`: GET 29,
MoveRate, the turn request) has a window of its own, `World::legionBodyWindow_`, judged by the steps and the turn the
body shows, so a body that turned to face a slide and walks on while its course turns round is not counted as making
headway. Flyers and Retail mode keep retail's rule. Walk scripts draw from the game's single random stream, so the
stand rule shifts later draws. `legion_still` tests the window alone: the to-and-fro motions stand, while genuine slow
motion (a walker at a pace, a speed-matched follower, slow pivots, a start from rest) walks.

#### Livelock

A livelock is a body that steps to and fro without net progress: a 1-6 tick cycle of 0.1-1 px steps, typically a
formation member's arc point flipping between neighbouring origins, or a side-step toward an arc that the field's next
step undoes. Three rules handle it.

* **Lone trees and rocks are no wall ends** (section 1, `wallOrigin`, `loneCell`): scattered single trees do not rank
  a formation member for an arc, so its arc point does not flip round them. What is left is real wall geometry. A kept
  arc point while it is still ahead in a straight legal line cuts flips but raises walk-in-place (`corner-8x56`: a
  kept point is pursued after the formation wheels past it), so the backstop alone handles flips by real walls.
* **A side-step toward the arc is not undone straight back.** Blocked again on the cell such a flow-around took it
  to, a body does not flow into the cell it came from for `kSlideHold` = 60 hold updates (hashed `Member::slideCell` /
  `slideFrom`); it holds, and the hold's own ways out come first. Barring every undo instead (any flow-around, or the
  previous origin of any step) costs `ringcross` its Move group's arrivals and stands bodies against landed flyers;
  one measure per run of flow-arounds triples the corner fixtures' lane crossings; committing side-steps to the cell
  centre only enlarges a cycle to a whole cell. Without this guard the backstop alone leaves a 30-tick to-and-fro in
  `legion_slideback`.
* **The backstop** (`World::updateLegionStill`, `Unit::legionShuffle`). A headway window that stands although the
  body walked a whole reach in it, or turned a whole turn, three times what that came to, latches a livelock. While it
  lasts the body neither flows round blockers, nor follows its arc, nor takes a lane pass (a pass a cell over, undone
  by the field's next steps, makes a cycle of its own), nor bends round a firing ring, nor presses, so it holds or
  walks down the field and the hold's ways out start. It ends when the body is a body length clear of where it began
  (16 px per footprint cell plus one cell), not on the next headway (a side-step crept and undone by a dash back clears
  the window's reach on every dash), or after `kLock` = 64 ticks (a 120-tick latch stands a landed-flyer crowd too long,
  `legion_landedflyers`). A held or merely creeping body is still but no livelock. Steps that do not turn the body and
  a standing body request no turn of its scripts, so the display VM sends no `TurnDirection` (the zontroll / zongob
  `MoveWatcher` walks on its last argument).

Tests: `legion_lonetrees` (scattered trees after a ranked wall end: no to-and-fro, no walking, no arc steering among
the trees at five offsets; one ordinary side-step shuffle may run out and back once), `legion_slideback`,
`legion_stillturn` (no standing member-tick requests a turn; a face step whose course lands exactly on the body's
heading requests none: `faceMotion`'s snap returns 0 when it changes nothing), `legion_forestedge` (a forest is a
wall) and `legion_still`. The scenario observer counts `walk_net` / `walk_net_runs` (GET 29 above 5 while the body is
under 8 px from where it was 30 ticks earlier, for 30 ticks or more: what the player sees) and `walk_in_place`, which
waits two headway windows (16 ticks) before it counts and must read 0 on every fixture. For Retail `walk_in_place`
counts the hidden internal speed of a refused mover, not its gait; `walk_net` is the gait key in both modes.

A walk-in-place animation for a held body whose blocker moves is not a display-only change (GET 29 and MoveRate feed
hashed gameplay COB threads and gate VERMAGE's AimWeapon), and Retail stands in its queues too, so Legion does not
add one.

### Gait pace follows the distance covered (display only)

A body that is still reported moving can travel well under its maximum: the facing cap and speed-matched following
hold it down. GET 29 stays above 5, so a keyframe walk would play at full pace. The client (`src/client/gaitpace.h`,
`GameView::captureUnit`) measures each Legion ground body's distance per tick against its terrain-adjusted maximum
(90% and over counts as full pace), smooths it (a quarter of the way per tick down, half of the way per tick back up),
and hands it to the display VM (`cob::Vm::setGaitRate`). Only the threads playing a gait script (`cob::gaitCode`:
`walk*`, `def_walk`, `swim*`, `stumble*`, `row`/`slowrow`/`fastrow`, `drive`, `wheel_*`, `SuperDynamicWheelSpinner`)
and the piece motion they started step on that fraction of the 30 Hz ticks. Controllers polling GET 29, aim, fire and
build threads keep full time. A body that reports stopped snaps back to full pace, so its cycle plays out its
static-gated frames and restores; so does a body that dies (the pace is set before the death branch, so types such as
`vermage`, `zonkrak` and `npcwagon` do not keep a half-played stride through `Dying`). Flyers and Retail mode always
run at full pace. The authoritative COB VM in `World` is untouched, so the state hash and every position, path and
order are the same. Debug clients take `TAK_GAIT_LOG=<csv>` (per ground mover per tick: distance, maximum, GET 29,
refusal, pace) and `TAK_GAIT_OFF=1` (pace held at 1).

## 4. Traffic

### Progress and the stall clock

Progress is the integer distance still to go, in one measure at a time: the field potential (x64) where the group's
route reaches the body's cell above its seeds, else squared cells to its own goal (the route does not reach the cell
yet, a bounded window's outside, a seed). Only a gain in the same measure, on the same route, is progress: it
restarts the stall clock that the arrival on contact, the settle rule and the blocked re-request read (and the wake
of a rested held body). A new measure (the route reaches the body, a refresh swaps in, the body leaves a seed)
re-baselines progress without counting a gain: the stall clock runs on, so a body blocked with no route still makes
the blocked re-request. A first step onto one of the route's seeds is a gain (potential 0, below the best so far), as
it is on the headway clock. `legion_progressclock` checks it, and the observer's `stall_stale_ticks` (a stall clock
that ages while the headway clock advances) must read 0 on every fixture.

The detour rules keep the record they were tuned on (`Member::nearest`): the least distance still to go the body has
read since its order began or it was re-seated, in whichever measure it read. A record needs a gain within the
current metric (`nearestHere`) as well as a new low of `nearest`, so a metric transition is never a record. A new
record restarts the detour back-off, and the yield-first rule wants none since the last detour. The approach holder's
gain is per route serial with a clock that stops while the route gives no value. A complete route that a static
change has touched since its build does not trap a body it does not reach (`RouteGrid::touched`, set by
`staticChanged` for exactly the groups it marks stale). The stall clock, the headway clock (`Member::headTick`, per
metric), the hold clock, the crowd window and the settle bound are separate clocks; the settle bound compares
potentials across a refresh on purpose (a tolerance that must keep growing under churn). Restarting the back-off on
every stall-clock gain, or re-basing `nearest` per metric, trades detours against standing in battles (a known
weakness).

### Stuck classification

* **Terrain-trapped**: the unit's origin has no finite potential once its group's route is complete, or its goal
  could not be resolved. No legal route exists for this footprint. The unit stops on the same update and then never
  moves, turns or probes. It keeps its order: an improvement that can open its way (section 2) re-plans it, with no
  polling. If nothing opens within 5 minutes (9000 ticks), the leg is retired with `dropLeg`, as Retail does for an
  unreachable goal, and queued legs continue. Retiring at once loses the army when a gate reopens. `legion_trapped`
  checks a sealed pocket that stays sealed (the unit never moves and its order retires at about 9000 ticks), the same
  pocket opened at tick 300 (the unit resumes and arrives), and the same under never-ending churn elsewhere.
* **Approaching an unreachable goal**: an approach member walks to its nearest reachable point. The walk is never cut
  short: its only limit is the 5-minute age test (9000 ticks from the first time this order's goal was found
  unreachable), which bounds a walk that never arrives. On arrival or contact it enters the Trapped state (zero speed,
  constant heading) for one update, and then its order is dropped (`kApproachRetire = 0`, timed from the arrival): an
  unreachable order ends as soon as the unit reaches the nearest spot it can, and queued legs continue. A unit queued
  behind its own army is treated the same way (the holder rule): once it has gained nothing on its point for 12 ticks
  (`kApproachSettle`) and is held, it looks at the one-body ring round its footprint (staggered over 4 ticks by id) for
  a body of its own selection that already stopped nearer the point -- an approach member just arrived there, or one
  whose approach order already dropped. If one is there, the spot it stands on is its nearest reachable spot, and its
  order drops the same way. A member still gaining, or one blocked only by other selections or by its own army still
  walking, never settles. A static epoch change while the member still walks re-registers it only if its stand-in no
  longer holds (`approachValid`): the body left the point's region, the point became illegal or unreachable, or the
  real goal became illegal or joined the body's region. A gate that opens after the order dropped does not move the
  unit. Approach members never claim slots: they queue to their stand-in point in the order they come (claiming
  shared-goal slots walls in the deep ones). `legion_approachhold`, `legion_approachopen`, `legion_approachchurn`,
  `legion_unreachable_orders` and the `unreach-200` / `mazeapproach` scenarios cover dropping at the point, the queue
  behind it, resuming when the wall goes during the walk, churn that must not reset the clock, and a long maze walk
  that must never be stopped mid-route.
* **Crowd-blocked**: a route exists but bodies occupy it. The ways out below run in this order.

### Ways round a blocked step

* **Allies swap places** (see below) is tried first.
* **Flow-around**: the unit steps to any free legal neighbour that strictly reduces its distance still to go. The
  step is committed until the new cell is entered. It moves only along the axis whose origin changes, never back
  toward a cell centre, and it does not turn the body's course (at travel speed the body faces its steps); this is a
  shuffle. A formation member's shuffle (`Member::detourFlow`, hashed while set) is walked at travel speed, and no
  shuffle of a formation member steps more than 90 deg behind the aim as given to `drive` (`aimDx`, `aimDz`): such a
  step is a backward walk the next descent undoes, so a body with no option ahead holds and the hold's ways out run.
  A plain mover (a point with no formation assigned, as a 500-body army's is) keeps its backward shuffles: meeting
  another crowd head on, it backs out of a bridge with them (`ctest:legion_choke`). A plain mover's shuffle keeps the
  facing cap (section 3) unless its target is still. A member round a still target that its last crowd window found
  still commits no new shuffle (section 7).
* **Corner clip**: the one-axis crossing at a wall's end (section 3).
* **Press**: with no flow-around and no corner clip, a body on a leg that completes at a point and engages no enemies
  (a move leg: not fight, attack, guard or patrol, never an exact mission goal -- Build, Repair, Reclaim, Load,
  Unload, Park -- or a production exit: `pressKind`, `engagesEnemies`) and not livelocked clamps its blocked step per
  axis as Retail's mover clamps its own (`edge`): the free axis is walked at travel speed, unturned, and the other
  stops at the body's own origin cell's edge, when that moves the body at least 1/16 px (`WhyNudge`). A queued body
  closes on the one ahead and slides along a file; at the edge it holds. On rings, fights and guards the nudges are
  creep, and a mission leg pressing against a yard stands on it, hence the restriction.
* Otherwise the unit **holds**: speed zero, heading constant, no creeping. A held unit rechecks its candidate cells
  against occupancy every update. This is change-driven, since nothing moves until a cell frees, and it never
  triggers a re-plan, so a long queue at a choke costs no search work. The hold's ways out (below) start from the hold
  clock.
* Rejected for blocked steps: own-goal-first descent (any lower neighbour: maze crossings fall), a flat half-speed cap
  on non-turning shuffles (gridlocks opposing columns), uncommitted shuffles (fewer settled arrivals, or spin in the
  crowdheld check), same-direction "purposeful" side-steps, walking the own line as far as it is legal and then the
  field (a Bug-like approach: bodies queue at the obstacle's face), half speed sideways instead of the eighth, and a
  continuous blocked step with no cell commitment for shuffles. A field seeded at the click sends most of a crowd round
  an island's near side where Retail's bodies, each routed to its own offset, split; with shuffles at travel speed the
  crowd is not slower for it, so no per-band field is built.

### Allies swap places

`swapPartner`, `swapBodies` in src/sim/legion.cpp, tried first in `drive`'s blocked branch, before any way round:
when this body's blocked step is covered by exactly one other body of an allied player, that body's route (else the
line to its own goal) descends into this body's origin and this body's into its, their courses are at least 90 deg
apart (`kSwapAngle`) and the other body's course points at this one (within 67.5 deg), the two trade places in one
exact step -- positions, occupancy, ground stamps and body index. Both must have one footprint and one plane (each
footprint then lands exactly on the cells the other leaves, so no legality question arises), both must be on move
legs (no fight, attack, guard or patrol leg, no exact or exit-clear mission leg), and neither may be settled, engaged,
trapped, approaching, livelocked, parting, pressing in or walking a route. A body that swapped does not swap again for
45 ticks (`kSwapCool`). The swap ticks (`swapTicks`) are folded into the navigator's checksum and carried by
`World::clone`; `Stats::swaps` counts the swaps (`work.swaps`). Integer only. It resolves two opposing streams of one
side that meet head on in a one-body lane, where neither has a way round (`bridges-1000`: two allied armies of 500
crossing a river in opposite directions over three bridges; `ctest:legion_swap` runs offsets -2, -1, +1 and +2 and
needs at least 850 arrived, at most 10 stuck and a swap). The client plays the one-tick jump of 16-32 px out over 8
ticks and sidesteps each body to the right of its jump, so the two pass side by side (`src/client/swapshow.h`, display
only, never hashed; `World::legionSwapTick` tells it which bodies swapped).

### Oncoming and opposing traffic

* **Before contact** (`passAhead`): a mover scans its own swept lane 6 cells ahead. If a Legion mover is there whose
  way to its own goal points back at this body by more than about 112 deg (judged by goal direction, not heading, so a
  body that has not turned yet is not oncoming; judged by heading, rapid replacement loses arrivals and spins), the
  body commits to a one-cell step to its right, forward-right first, then straight right. The step does not turn the
  course and keeps travel speed. For the next 300 ticks (`kPassHold`) a direct-line body keeps walking straight in its
  new lane, and the flow-around shuffle may not step back across the lane it left. No pass happens within 12 cells of
  either body's goal, where bodies are packing into place. Two enemies' fight legs (attack-move or patrol) are never
  oncoming traffic to each other: two fight-moving armies that march at each other do not step aside. A plain move
  still makes way for any body, enemy or ally, and a fight leg still makes way for an enemy on a plain move. The
  lane-scan skip mask keeps one sector mask per player and leg kind, so a fight leg's scan whose lane holds only enemy
  fight-leg goals is skipped. A pass turns the unit toward its step only when the lane ahead is nearly empty; in dense
  opposing blocks it stays an unturned one-cell dodge.
* **In contact**: after 6 held updates against a body with orders facing more than 90 deg away, the unit commits to
  one lateral step to its own right. The step does not turn the course (turning 90 deg and back in a jam reads as
  spinning) and times out after 45 ticks. Same-direction queues never side-step: a unit steps right only when the
  blocker is genuinely heading the other way and neither is inside its destination area.
* Rejected for passes: passes that turn the body (spin), non-turning passes under the facing cap (fewer crossings),
  fast passes without a lane hold (travel reversals; shorter holds of 45 and 150 ticks spin more than 300), only
  diagonal passes at full speed, a lane hold for field followers too, and passing inside destination areas.

### Detours round still bodies

* **Walled in by still bodies** (settled arrivals, a held queue, trapped or approach-parked members, idle units of any
  player, standing jams of other orders): after 8 held updates, the unit runs a bounded BFS (25x25 window) over legal
  origins whose footprint touches no still body. The target is its own goal if it is inside the window, otherwise the
  reachable cell that most reduces its distance to go, and only a gain of about two cells is committed. The route is
  walked cell by cell, at travel speed, the body turning to its motion; a reach kind's detour round its own ring keeps
  the shuffle. It is attempted only when the blocking body will not move by itself (idle, arrived, trapped, or not a
  Legion mover), when a formation member has been held for 60 updates in a standing jam of crossing lanes, or when a
  member has been held 60 updates against another order's standing jam (`blockedByJam`); never for a queue of its own
  order's members waiting on each other. Attempts back off geometrically (30, 60, ... 480 updates) while the body sets
  no new record of its distance to go. A route reverses 1 to 6 updates after it starts: it backs out of the pocket
  among settled bodies the body walked into, which is what the route is for (most of Legion's backward steps).
* **Settled arrivals yield**: when a unit completes, Legion records an anchor (the origin it completed on). A member
  held for 12 or more updates, with no detour or route, whose wanted cell is held only by settled, idle, friendly
  anchored bodies (its own player's or an ally's, never an enemy's), asks those bodies to step one cell aside; if that
  fails, it tries the same for a neighbour nearer its own goal. Every blocker must find a target or none moves. A
  target is within one cell (Chebyshev) of the blocker's anchor, legal for its footprint, clear of the requesting
  member's current and wanted footprints and of targets already chosen in the same request, and, for a formation
  arrival, inside the point's area and off cells other members claimed there. Yields run in `tick()` every tick, in id
  order: a straight step at ground speed without turning, ended by arrival, a refused step or 45 ticks. Each body
  yields at most 3 times. A unit blocked by settled arrivals asks them to yield before planning another detour away
  (`lattice`). An anchor is forgotten when the unit dies or gets any new order (section 9: upkeep on demand). A
  settled body's yield carries its formation slot with it (section 5). A yield cannot open an entrance the settled
  bodies have sealed: it parts a lane through bodies with somewhere to go.
* **Idle bodies part.** An idle friendly body (own player or ally) one or two cells beside a mover's way steps aside
  for it, at most 3 times, never a body with an order and never an enemy's (`partLane`). Retail moves no idle body at
  all (a blocked mover only clamps, slows and presses on, and BUGGER_OFF, the one script request to clear a space, is
  stored and never acted on), so Legion does not move enemy bodies either.
* Rejected: a yield-aware detour (a BFS that treats yieldable settled bodies as passable and asks them to yield
  along the route: displaced bodies form new diagonal blocks), yielded bodies returning to their goal 90 ticks later,
  speed-matched commit budgets, and pushing the blocker forward (about 85% of holds behind movers wait on a body that
  is itself boxed: a real queue draining as fast as cells free).

### Arrival on contact

Once a unit has made no progress for 20 ticks, it may arrive inside its goal's area while touching a settled
same-player body. With a distinct goal the area is one body width and the goal cell must itself be occupied by another
body, so settling short never plugs a neighbour's lane (`contactArrival`). The rules for shared points are in section
5. The destination is an area, so a blocked unit never arrives outside it.

### Room at a merge

`makeRoom`, `queueHead`, `Member::want`. A held formation member on a move-like leg whose every way down the field is
covered by members of its group that have waited less than it -- a stream passing, or its own queue behind -- heads
its queue and records the origin it is blocked from (hashed; indexed in `wants`). Once it has waited `kRoomHeld` (60)
ticks, a member of its group that passes it -- moving, outside twice its destination area's bound, its step this
update leading on within `kRoomLook` (24) origins onto that origin -- moves over a lane instead: one step forward and
outward (or straight outward), away from the head, no higher on the field, as a committed flow shuffle at travel
speed. The head steps into the lane the passer left, and the passers walk the next lane out, so a single file past a
corner becomes lanes. The route ahead is known static terrain, so a passer reads the head's want 24 origins ahead
along its step and moves over before it gets there (the look-ahead does most of the work). Passage admissions,
head-on meets and passage lanes keep their own lanes. `ctest:legion_cornermerge` (corner-1x100 at offsets 0, +1, -1:
nobody still 900 ticks, stopped permille at most 245, t90 at most 4,100). Rejected: a zipper (a moving body does not
step into the origin a queue head waits for: the stream stops for each merge), moving over for any held body (costs
open-ground crowds), a 10-tick head (acts on a crowd's ordinary stop and go; fails `legion_mixedformation` and
`legion_landedflyers`), no moving over while on a pinwheel arc, only heads pinned against terrain, keeping the new
lane for 200 ticks, and a shorter `kRoomHeld` (15, 30 or 40 tip `bridges-1000` into gridlock).

### Group awareness

Opposing groups are aware of each other (when they can see each other) and path round each other as whole groups,
unless they are attacking the other group. Every 30 ticks (`kAwareScan`) each moving formation of 8 or more (members
of one command sent to one point) is a mover: its centroid, spread, and measured velocity. A Legion group whose way
ahead meets a mover plans round it as a whole: its next field charges the corridor like a soft obstacle, and its
members' direct lines refuse to cross it. A mover stays in the plan until it has been off the way for two scans.
Same-player and allied movers always count; an enemy mover only within `World::sightDistance` of some member, never
for a group whose mission engages enemies (fight, attack, guard, patrol). One selection sent as several groups never
plans round itself, and a mover going the same way is followed, not avoided.

* **Detection.** A conflict is found by sampling, not by a distance test. Every 15 ticks, up to 450 ticks ahead, the
  group's own descent chain (walked at its slowest member's speed) is compared with each mover at its measured
  velocity; a conflict is the group's disc meeting the mover's box, and one under 45 ticks away is too late to plan
  round. The class of the meeting is an integer dot-product test on each group's line to its destination (never its
  measured heading, which reads a group already routing behind a stream as head on): head on over 135 deg, same way
  under 45, crossing in between. It is stored as hashed `avoidKind`, folded into the checksum only when set. A shared
  awareness field is shared only between groups with equal charge lists, and aware refreshes are served first inside
  `kRefreshQuota`, at most 8 starts a tick.
* **Broad phase.** A mover not yet planned round enters a plan only through a sample where the group's way point (on
  its descent chain, inside the chain's box of cell centres) and the mover's box centre (inside its swept box over the
  450-tick horizon) are within `(half + max(tail, head) + 66) + 2 rG` px in each coordinate. Movers are listed in a
  1,024 px grid over their grown swept boxes; a group reads the cells its chain box grown by `2 rG` covers, and every
  mover already in its plan is a candidate whatever its place. Candidates are visited in the original mover order, so
  the plan and its order do not depend on the grid. `TAK_LEGION_VERIFY` samples every dropped mover and fails on a
  conflict.
* **Crossing groups take the longer route.** On a crossing way the later group never waits for the other stream to
  pass. It plans round a time-swept corridor that covers the stream's band from its tail to 450 ticks past its head,
  but only where the stream will be when the group gets there (a lead of twice the group's radius), so the cheapest
  way is just behind the tail. The corridor charges 12x inside and is not a wall: if no detour exists at all the group
  crosses, and no detour length leads to a wait. The entry is sticky until the group has crossed or the stream's tail
  has passed, and the corridor is re-placed when the stream drifts, at most every 90 ticks. There is no give-way hold
  at the stream's edge (the `giveway_*` counters stay 0). Measured by `aware-cross-behind` and `crosslong`
  (`behind.*.extra_cells`, `detour_permille`, `wait_member_ticks`; the wait keys `wait_ticks`, `wait_run_max`,
  `waits` read 0), `aware-cross`, `awarebig-cross` and `crossthree`.
* Measured by `aware-headon:pair.contacts.permille_x100` and the `aware-*` fixtures (`aware-unseen` is the
  awareness-off control: the enemy is out of sight). Awareness is the one place fog shows in Legion
  (`aware-unseen:pair.contacts.permille_x100` against `aware-seen:pair.contacts.permille_x100`).
* Awareness by streams (separate movers for spatially separate parts of a destination area) costs arrivals and is
  not used.

### Head-on meets

A formation point of 16 or more members whose mission does not engage enemies (fight, attack, guard and patrol never
latch) latches a meet (`Point::meet`) in two cases: group awareness plans a head-on entry for its group (seen), or a
member's lane scan (`passAhead`) finds an oncoming body of another command's point and stamps it
(`Member::meetSeen`, unseen; the stamps are combined per point by a minimum, so update order does not matter).
`meetScan` runs serially once a tick after `awareScan`, over the cued and latched points only. A latched point's
members narrow into the right half of their own lateral extent, shifted one body further right, and aim six cells
ahead at it (`meetAim`), aim-only: no field charge, no re-plan, no stored lanes. The target is an increasing map of
each member's current lateral position driven by the group's progress, which never falls, so members cannot cross
each other; they move at most one cell across per two along, only on a statically legal line that descends the field,
never where the destination area packs the member (within the pivot distance, 32 cells, of its goal AND inside its
point's area plus one lead, `Point::limit` + 6 cells round the requested point), and the target is cut before a wall.
Large orders, whose areas reach past 32 cells, therefore narrow only outside the pivot distance; two small orders sent
to each other's positions narrow until their areas pack them (`ctest:legion_meetnear`). Narrowing waits until the gap
between the groups is within twice the span plus two leads. Both groups keep right. The latch is released when the
other group's tail is behind this group's rear, or after 90 ticks without contact before the groups meet. Hashed only
when set (`Point::meet`, `Member::meetSeen`); the derived `pendingMeets` / `meetIds` indexes are checked by
`TAK_LEGION_VERIFY`; `meet_latches` is a never-hashed counter. Measured by `densehead` (lane swaps in the middle gate,
arrivals).

**Departures through an arrived crowd.** An order arriving in an area another order is still leaving settles among
the bodies that will leave. Their local detours round the settled crowd are walked at travel speed, and the yield and
parting rules take friendly bodies of any allied player, so the last of the departing bodies are not sealed in
(`ctest:legion_departure`).

### Standing jams and late soft blocks

* **Standing jams are traffic.** A member held `kRestAfter` (60) ticks on its way (`jamStill`) is stamped soft (kind
  2) and owned by its group's command: other orders' routes plan round it, its own order never does (the parts of one
  selection share the command). It stops being soft the update it steps. The standing jams of each scan cycle are also
  late-soft-block candidates (`jamBlockList`, rebuilt every cycle), so a group whose way meets another order's jam
  re-plans round it (at most once per 300 ticks). A jam is stamped for any player, as any still body is (the soft
  rule has no sight test); no route learns more of an enemy than the still bodies it already plans round. scatter-400
  (400 single walkers to unrelated destinations) checks it (`ctest:legion_traffic`).
* **Late soft blocks.** A block of soft cells 20+ cells across that forms after a group planned, on its 48-cell
  descent chain from its centroid or tail, re-plans it once (300-tick cooldown, refresh quota). Blocks leaving never
  re-plan.
* **Traffic in coarse route selection.** The region search (`Hierarchy::search` with a traffic term) charges the way
  across each region by the share of its origins soft to the route's command: step x (kSoftFactor - 1) x soft / cells
  (`trafficPenalty`) -- the same soft rule the field and the tiles charge cell by cell, own-order arrivals and jams
  exempt, liftable allied flyers left out. The counts are cached per sector, keyed by its generation and the soft
  serials of it and its east, south and south-east neighbours. The search only picks the regions; the exact local
  correction inside them is the tiles' (never a sum of two fields). `legion_trafficlane`: the chain avoids the gap
  whose sector holds 121 idle bodies. Window fields (high demand, small windows, the fallback) charge soft bodies and
  corridors inside their own build.
* A crowd-density cost in the group field (a soft charge for cells covered by the group's own moving members) spreads
  crowds and gains arrival time but multiplies lane crossings and breaks `bridges-1000`, as does a near-level flow (a
  blocked body stepping one orthogonal step uphill). Nothing yet spreads a crowd across the free width beside an
  obstacle except the special cases (room at a merge, the ribbon, the passage lanes).

## 5. Formations: shared points and slots

When two or more bodies are sent to one point in one command, Legion treats the point as a destination area for the
whole command, across every footprint class (mixed footprints form one group per class but share one `Point`). The
in-game right-click in Legion mode sends one shared point for ground units, boats and hovercraft with footprints up to
8 whose order is not queued behind existing orders. Flyers and queued legs keep Retail's per-unit offsets.

* **Formation assignment** (`assignFormation`): once at least two members take part, each member's target is its
  offset from the members' centroid, scaled so that the crowd's RMS spread matches the packed disc those bodies
  occupy, placed around the point. The packed area is the sum of side² per body, or (side+1)² when sizes are mixed,
  since different sizes never tile. The area limit is that disc plus three widths of the smallest body, minus 8 px.
  Members are served front-first along the centroid-to-point direction, so the front of the crowd takes the far side.
  Each takes the nearest origin to its target that is statically legal, in the group's component, clear of every cell
  already claimed at the point, and inside the limit. A member that joins later maps its own offset with the stored
  transform. A member that gets no cell (`slot=-2`) walks to the point and looks again only by the settle rule's
  re-choice. If fewer than two members can take part at the first attempt, the point retries every 16 ticks.
  Approach members take no slot and do not size the area.
* **The slot wait.** Formation slots wait until the area can no longer be joined (`commandOpen`), then are handed out
  front first, at most `kSlotsPerTick` (32) a tick and within the tick's slot search budget (below); the rest steer by
  the shared field meanwhile. A first part under one uplink tick (64 orders) is the whole click as far as the server can
  tell, so it takes its formation at once, provisionally; if another part joins the point before the area closes,
  every slot is given back and the formation is rebuilt over all parts then (waiting without slots walks small groups
  the first stretch bunched on the line to the click). A destination area stays open while members of it wait to
  register (`regAreas`), so its formation is not sized without them.
* **Slots by arrival** (`entrance`, `enterArea`): the front-first order is the straight line's, not the way's. A
  narrowing on the way reorders the crowd (the bodies off the door's axis come through it last), and a room whose door
  is on its far side turns the order over: the first bodies through the door would take the slots beside it and settle
  in the doorway, and the rest of the order would settle outside (`farroom.scn`). The order is not even front-first in
  practice for a click the server applies in one tick: the move mission's reset re-registers every member on its first
  update, just after the formation is taken, and each re-claims its own offset in update order. So a formation takes
  an *entrance* when its way in narrows just before the area. Open ground is told without a search: eight field reads
  round the point, `kEntryNear` (24) cells beyond the area's radius, each within a quarter of its octile distance,
  mean no wall near the area. Otherwise the descent from the member nearest the centroid is walked to its first cell
  inside the area, and the passage gate's cross-section (`gateSpan`) is read 24 chain cells either side of it: the
  narrowest must be under the area's radius, the run round it under twice that width must open out again within the
  probes (a closed dead-end corridor never does), and the area may not reach back past it into open ground
  (`doorplug-112`, whose area spills through the door, takes none). That first cell inside is the entrance; the search
  counts with the passage gate's (`lane_work`). A member that then comes through (its potential within `kEntryCells`,
  12 cells, of the entrance's, after it was seen outside) takes the deepest slot that a member still outside holds --
  farthest from the entrance, with 2 px of depth charged for each px the slot lies across the entrance-to-point axis
  from the body -- when that is deeper and scores above its own slot; the two exchange slots. Only bodies of one
  footprint and one movement plane exchange (a ground body and a hovercraft of one footprint share the point, not
  their slots, which are legal on the plane they were found on: `ctest:legion_entryplanes`); a body already through
  when it got its slot keeps it. The slots fill from the far side in the order the bodies really arrive
  (`legion_farroom`). Each group of a destination area finds its own entrance on its own plane and field from its own
  members (`Point::entries`), and members exchange slots only with members coming through the same entrance (a room
  entered by several doors, a crowd of several footprints). The entrance is one footprint's: an origin of the member it
  was found for, on that member's group field; a member of another footprint reads the same cell on its own field, an
  origin whose centre lies (its foot less the finder's) x 8 px off on each axis, so in a mixed formation its "through"
  test is that far off (no slot moves across footprints, and no fixture takes an entrance with mixed footprints).
* **Straight lanes**: a member with a formation slot probes the direct line out to 640 cells instead of 160, so the
  crowd does not funnel into a file by descending the shared field.
* **Settling** (`settleWindow`, one rule): once per 45-tick window (`kCrowdWindow`), a body that gained less than half
  a body on its point over the window looks at the bodies touching it (a ring of at most 64 cells) that stand nearer
  the point: by the group field, a body of its own footprint at the potential of that body's origin (by pixel distance
  where the field does not reach it), a body of another footprint when the field reaches one of the waiting body's
  origins centred on it below the waiting body's own potential (a body the field reaches nowhere under it never
  counts; read at its centre converted with the waiting body's footprint, a big body reads a step above a smaller one
  it holds, and that body never queues: `legion_mixedsettle`). A settled arrival of the point whose cells cover the
  waiting body's own slot counts as nearer, whatever its footprint (`legion_mixedrandom`), and so does a settled
  arrival of the body's own point that is nearer the body's own formation slot (centre to centre) while the body is
  within 3 footprints of that slot (`beforeSlot`: a body between two settled arrivals of its formation, 2.5 cells from
  its free slot, is not left standing; `ctest:legion_slotwedge`). It is *queued* when one of them is a settled arrival
  of its own order at that point, an idle body of the player, or a held member of the same point that is itself
  queued, or when (a shared point) it stands inside the destination area itself: the chain can only start at the
  destination's own crowd, so a jam on the way never queues. A settled crowd of an earlier order, or idle bodies,
  settles it in one window within the crowd's reach (four packed-disc radii of everyone settled there plus two
  bodies). Otherwise a queued body that is *pressed* (no free neighbour nearer the point, or no gain at all for a
  window) first **re-chooses** a slot: the free slot it can still reach past the bodies standing now (BFS radius 24)
  with the lowest potential below its own, ties to the lowest cell, then the lowest slot index; at most 3 times per
  member (`rechoose`), and never while it stands on its own slot (a re-choice from there walks a body across the
  front of its own formation for a lower cell). Then it settles where it stands once its field potential is within
  `settleP`, which starts at the destination area's bound (the potential of the last of the ceil(1.25n) footprints
  nearest the point, so a half-disc at a wall and a strip in a dead end) plus two bodies, a body plus 4 px for a
  distinct goal, and grows by Retail's foot*32 px per window. No settle is farther from the click than twice the
  crowd's reach, unless the body stands within one row (two bodies of potential) behind a settled arrival of its own
  command (an arrival of another footprint at the middle of the lowest and highest potential of the origins centred on
  it): the cap is measured along a queue that is contiguous with the settled crowd, so a dead-end queue settles back
  from the crowd row by row however long it is, and a jam the settled crowd does not reach never does. Every settle is
  field-connected and outside any factory exit lane. A member on a distinct goal also arrives when pressed within a
  body of its goal and the goal is taken (`contactArrival`). There are no other time-outs. A held formation member
  re-aims its own lane every 20 held ticks (`laneDue`).
* **Own-slot headway.** A formation member's crowd window also measures its distance to its own slot against its best
  so far (`Member::windowSlotDist`): gaining half a body or more is not still. And a member with a free step that
  brings its origin nearer its slot is not pressed (`freeTowardSlot`). Both keep the front of a crowd going to the far
  side of its area, beyond the click, where every step climbs the field toward the click; without them the area fills
  from the click backwards.
* **Settle bound of an explicit area** (`settleWindow`, `crowdBound`). One order is a whole selection, so the
  queue-settle rule alone would let a 10,000-body order's queue settle back to where its bodies started. An explicit
  area's settle bound never grows past the packed bound of the point's whole crowd (`Point::crowdCells`/`crowdPeak`:
  every footprint plane's bodies, packed in this body's footprint along the field, so a dead end's strip is its length)
  widened to the benchmark's arrival disc (`kExplicitSettleNum`(foot+1) / (`kExplicitSettleDen` foot), the ratio of
  the disc's 20(foot+1) px to the packed bound's 16 foot px per sqrt(1.25 n/pi)), plus the slack; beyond it a queued
  body keeps its order and waits, an unarrived body. A crowd of one footprint reuses the destination area's own bound
  (`areaBound`'s cache). Per-unit areas keep the plain rule. A straight-line test against the disc itself stops dead-end
  queues and is not used.
* **Packed per-goal slots** (built inside-out by field potential, claimed back to front along each member's approach)
  are the fallback when no formation applies (distinct per-unit goals sharing a point). Claims skip slots that are now
  illegal or unreachable, so a corpse or building on a slot cannot cause a re-claim livelock (`legion_slotblock`).
  A member whose goal turns illegal re-claims a slot, or takes the nearest legal origin of its region, inside its
  group. Point cell claims do not follow a body that yielded.
* Rejected for packed distinct goals: sideways lane steps, lane steps for distinct goals only or near the own goal,
  deep-first ordering gates, and goal exchange (goals 48 px apart against a 32 px arrival radius: a swapped goal never
  counts as an arrival). Settled-crowd compaction and claiming slots in approach order with facing are worse too (late
  units are not short of spots: units of different speeds arrive out of order).

### Slot hand-out budget

A member's formation slot search (`formationCell`) walks Chebyshev rings round its target, out to 48 cells: 9,409
cells when nothing in the area is free near it. One budget a tick, `kHandOutCells` (131,072 ring cells), covers every
slot search. The tick's hand-out runs at its first formation claim, before any other slot search of that tick
(`serveHandOut`): the queues of every waiting point, round robin in key order, one member each a round, starting after
the point served last the tick before (`handNext`, hashed while two or more points wait). A point still takes at most
`kSlotsPerTick` (32) a tick, and a search starts only while the budget lasts. A formation's first burst
(`assignFormation`) takes its first slot at once and more only while the budget lasts; a member without a slot
searches while it lasts and otherwise queues behind the others. The ring search also stops at the ring past which no
cell can lie inside the area: a cell r rings out has its centre at least (r-1)*16 px from the target, so at least that
less the target's distance from the point, and the area is the point's limit. Its result equals the full square walk
(`TAK_LEGION_VERIFY` checks it), and a search that finds nothing stops there instead of at ring 48.
`ctest:legion_handout` runs `deadend-multi-4x120.scn` (four 120-body clicks to four dead ends in one tick) at offset 0
and bounds a tick's ring cells at 150,000. The steering probes and detour searches stay unbudgeted per member: with the
hand-out bounded, the slowest ticks are the field quota's. `packedBound` and `slotsFor` sort their window's cells by a
stable counting sort (the cells are collected in ascending order).

### Alt+N pacing and the rejoin

**Alt+N formations** keep Legion's pacing rule (`World::tick`, `squad < 0`): members walk at the slowest member's
individual speed and stragglers rejoin at rest. Only bodies that can walk count: a structure, a unit still under
construction or one with no speed stays in the formation (units it produces still inherit it) but never sets its
centre, area, busy count or pace, so a building in an Alt+N selection does not hold the army at a standstill
(`legion_structsquad`, `legion_factorysquad`, `structsquad*.scn`). The centre sums are integers of the Fixed
positions, and the pace is a Fixed. The rest-time rejoin uses the formation's ground only (flyers are never
re-ordered) and gathers at P, the modal settled-arrival point of its members, not at the centroid: a body settled at P
is never re-ordered, and another is sent to P only if it is beyond the settled crowd's reach of P and farther from P
than the centroid is (with no settled member, the centroid rule stands). So a dead-end queue is not pulled back on
itself (`legion_flyers_deadendrejoin`: holding 0, no anchored body re-ordered) and a factory's joiner still gathers.
The rejoin also takes the AI's flagged squads: its aggregates key on the formation group, Alt+N formation N at N, and
only Alt+N is paced. A member with a slot is left to the re-form below instead; the rejoin gathers the members that
have none (a fresh formation, a factory's joiner, a member stopped alone mid-move).

### Formation re-form

Retail mode walks an idle formation member that has drifted out of its slot back to its group with Standby's group
check (`407779`, `51d1e0(unit, 1, 2)`, Move_Ground_FormationStandby; [pathfinding-port.md](pathfinding-port.md)).
Legion's counterpart returns it to its own slot instead of the group centre, since every member of a formation's move
has one:

* *The formations* are Retail's (`legionFormationGroup`, `retailGroupSlot` with the formation flag): Alt+N
  (`squad < 0`) and a classic AI squad while the AI flags it one (`aiGroup` / `aiFormation`: a strike squad while it
  attacks), groups 1..99. Ctrl groups and unflagged AI squads are not formations.
* *The slot* (`World::legionFormationSlots_`, hashed, folded only while any member has one): for each ground or boat
  member, the footprint it stood on when the settled arrival of its last *formation move* began
  (`LegionNavigator::anchorGoal` names the arrival): its own place on the formation's point, or the place short of it
  where Legion settled it. It is never the goal itself: a member Legion settled short of its goal is not to press on
  to it (a slot walled in by two settled arrivals, `legion_slotwedge`). A formation move is one at least two members
  of the formation walk (one order identity: one click, one area) or arrive on (one requested point,
  `arrivalPoint`). It drops the old slots under way (a formation the player stops short keeps none to walk on to or
  back to) and gives the new ones on arrival. A member moved on its own keeps its slot, as Retail keeps it in its group
  record (Retail's reference is the group centre; ours is the member's place in the group), and walks back to it once
  idle out of it. Any other order -- an attack, a step-out, a build, a factory door clearing -- leaves the slot too. A
  settled body's Legion yield (at most a cell from its goal) carries the slot with it, so a body that comes in behind
  the step does not stand on the slot it left. Two members moved on their own within the area window to nearby points
  share an area, so their moves count as a formation move and give them new slots there. A slot whose point no other
  slot of the formation shares any more (the formation moved on without the member, or it is alone;
  `legionSlotDests_`) is not walked back to. Kept each tick before the unit loop (`updateLegionFormationSlots`: one
  walk of the units, map writes only on a new arrival). A unit that changes owner (`changeOwner`: given, charmed, a
  mission's transfer) leaves its formation with its squad and drops its slot at once, and `resetForReplay` clears
  every slot (`legion_formationslotreset`).
* *The trigger* runs where Retail's does, at each wake of the member's ground Standby (every 7 + rand(7) ticks; no new
  draws), with Retail's tolerance measured from the slot (`legionStandbyOutOfSlot`, `src/sim/retailgroup.h`):
  `(octdist/16)^2/4` against the class's footprint area -- every member's when none of its class moves (level 2
  relaxed to 3), the movers' plus its own, halved, while some do -- at least 2. Six 2x2 bodies: 9.8 cells; 120: 44.
* *The walk* is a plain Legion move to the slot footprint's centre (`World::order`), so it paces, yields and settles
  as any other; the Standby retires and restarts when the move is done. A body or the ground on the slot
  (`mobilePlacement`): no walk, the next wake asks again. A member that walked back and still stands out of its slot
  (Legion settled it short, or the way is shut) gives that slot up until it carries another order: one walk, never a
  to-and-fro.
* Flyers keep their flight stations over the ground and are never re-ordered.
* In AI games it rarely acts: an AI strike squad's members are nearly always walking or fighting.
* `legion_formationreform`: a member shoved 12 cells (past the 9.8-cell tolerance, inside the rejoin's reach) walks
  back once and settles in its slot; a Ctrl member shoved alike stays; a body on the slot holds it back until it
  leaves; a member moved 20 cells on its own walks back once idle; a formation stopped short of its point stays put;
  walled off, it walks once and gives up. `legion_formationreformai`: a flagged AI squad's member shoved alike walks
  back and its member that never moved with it is gathered to its point by the rejoin; an unflagged squad's stay where
  they are.

### Turns: the pinwheel and the ribbon

Every shortest way past a convex wall end touches it, so members descending the shared field converge on the tip
whatever their place in the formation, and a formation sent round the end of a wall folds into one file there. Legion
ranks formation members across the turn and gives each its own arc.

**The pinwheel.** A formation member of a point with `kPivotMembers` (16) or more members takes a rank when a turn of
its descent chain beside a wall comes into its 48-cell look-ahead (`pivotAim`). Its side offset from its part's live
centroid across the way to the turn, counted from the inner side (the inner file hugs), makes its descent chain beside
the wall, offset outward by that radius, its own concentric arc round the end. It pursues the first arc point 6 or more
cells ahead that is in a straight legal line, and when blocked flows round toward it rather than down the field. The
arc shrinks to the free width beside the wall; the look-ahead stops at passages (the passage lanes keep their own
lanes) and within 32 cells (`kLaneNear`) of the destination (the area logic places the slots). The group's field
window gets the arc margin. Ranks and the aim memo are hashed; the work is bounded. A member ranks within its part
(`Member::part`, the tick its leg's command was applied). A lone tree or rock is no wall end (section 1). Seen in
`corner-1x48:gate.top.files_x100` (files abreast at the corner), `uturn:gate.top.files_x100` and the `pinwheel` world
test.

**The ribbon** (`ribbon`, `ribbonHold`, `laneUncross`, the rank in `pivotAim`). A part of 32 or more on a move-like
leg (`ribbonLeg`) whose point has at most `kRibbonMaxPoint` (256, four uplink parts) members ranks by the ribbon
instead:

* **The wave.** When a member ranks for a turn, every member of its point that has no rank yet and whose own descent
  (`kRibbonChain`, 96 steps) passes the turn's vertex within 2 cells is ranked at once: ordered by its place across the
  way in (the approach chord through the vertex), inner first, member k of n takes radius (2k+1)/2n of the ribbon's
  width, 1.2 bodies per sqrt(n) (`kRibbonNum/kRibbonDen`, at most `kPivotMaxCells`, 32); under 1 cell it hugs (the
  inner file). A crowd that splits round an island ranks each side apart; the parts of one area rank as one. The arc
  pursuit is the pinwheel's, down to a radius of 1.
* **The hold** (`ribbonHold`). While a member has a turn, a straight walk whose line passes within its radius (less a
  body) of the turn's vertex or end is refused, and it follows its arc (a straight walk to the slot otherwise opens the
  moment its line clears the wall end and grazes the tip). It is past the turn 2 cells beyond the end along the exit
  chord and not behind the vertex along the way in; then the turn is remembered and it ranks again at the next turn,
  keeping its radius when the inner side is the same (one file through two right turns) and taking a new rank where it
  flips (`sbend`).
* **Turns.** A turn (30 deg or more between 6-cell chords, from the first chain cell beside a wall) needs the wall on
  its inner side within its 12 cells, is not within 12 cells of the last turn's vertex or end, and is ranked only once
  its end -- the first cell after which the chain runs straight for 12 cells -- is in the look-ahead. An S-shaped
  octile chain or a bend away from a face is no turn.
* **Slots by lane** (`laneUncross`). Once, when a member that has rounded a turn comes within 24 cells of its slot, it
  exchanges slots with the member of its group within 16 cells (one footprint) for which that shortens their two
  straight ways the most, by more than a cell (a 2-opt step; crossing ways are always the longer pair). The two
  exchange goal and slot index together.
* **Blocked files.** A ranked member that makes no progress on its field for `kRibbonStall` (120) ticks, counted from
  its rank (`Member::rankTick`: a member ranked while it already stood keeps its file), gives up its file and hugs (the
  field's own way, as the inner file) until it is past the turn: a hold that cannot be honoured is let go, it never
  freezes.
* **Big points.** A point of more than 256 members keeps the pinwheel, and a member whose point outgrows it (an area's
  later clicks joining after its first members ranked) drops its turn record and ranks again by the pinwheel: one
  ribbon is at most 32 cells wide, so a bigger point would rank in many small waves, each pinned to the inner wall
  (`ctest:legion_choke`'s `bridges-1000` gridlocks that way, and the 448-body corners lose throughput).
* Fight, attack, guard and patrol legs and parts under 32 keep the pinwheel (at 20 and 25 bodies the ribbon only slows
  the formation). New hashed member state: the turn and last-turn records, the rank tick and the exchange flag.
* `ctest:legion_ribbon` runs `sbend` over the core five start offsets and bounds the median second-turn swaps in single
  digits.
* Rejected for turns: the hold alone with centroid ranks (island lane swaps), one lateral rank per order from a
  whole-part snapshot (U-turns slower), the radius as the member's distance out of the line through the vertex (gaps
  and walls much slower), arcs offset from every chain cell (bodies drift outward), files at footprint pitch, ribbon
  widths of 1.0 or 1.4, a contact-vertex search that ranks bodies meeting a wall end across it, and a ribbon across
  the orders of a player at the vertex. The corner-hug removal (every rank wheels at least a cell off the wall) cuts
  swaps but makes later groups of `corner-8x56` miss t90.

A part's lanes through a corridor corner are section 6's corner lanes.

## 6. Passages

### Openings and queue delay

* **Openings.** An opening is a narrow crossing of a hierarchy sector boundary: a portal run (`sim/legion_hier.h`)
  bounded at both ends by a pair of origins that do not cross the boundary (a wall, a bank, the map's edge), the run
  followed past sector corners (a door across one is one opening), fewer than `kOpeningSpan` (64) origins long. Its
  identity is its cells on the boundary line, so every movement class crossing one bridge shares one record
  (`Opening`, `openingIndex`; per plane, the openings of each sector side are cached by both sectors' generations,
  `Plane::sideOps`). Nothing is precomputed: records appear the first time a route's build or a chain walk crosses
  them.
* **Labels.** Every soft group's window or corridor field records, for every cell it reaches, the first opening its
  descent crosses and the direction (`Field::via`, 2 bytes a cell of a labelled tile, allocated only where a label is
  not 0); a tile chain's members read theirs from the chain's nodes (`nextOpening`: from the chain node of the body's
  region, the first exit portal along the parents that is an opening, cached per member by route serial, region key
  and plane epoch: `Member::viaSerial`, `viaKey`, `viaEpoch`, `viaLabel`, derived, never hashed, charged as
  connectivity work).
* **Entrants and queue delay.** For a group of `kOpeningMembers` (8) or more (`Field::queues`, hashed with the field),
  every `kOpeningScan` (30) ticks each member adds its queue area, (footX + 1) x (footZ + 1), to its next opening in
  its direction (`openingScan`). The delay of a crossing is the queue in front of it in cells of travel, area / width
  (`queueDelay`): on a narrow opening (`kNarrowWidth`, 16 cells) an opposing body counts twice (the direction turns,
  each turn clears the passage), on a wide one half (the streams keep to their halves). A group's own entrants
  (`Group::openArea`) are left out of the delays it is charged: a route never plans round itself. A route of such a
  group is never shared with a smaller group's field that does not count its members.
* **Route choice.** A new window or corridor route carries the delays of its window's openings as charges on the
  crossing steps (`Field::charges`, route units, halved where the group already crosses -- a commitment), part of its
  sharing key; the build's bucket ring grows to a power of two above the largest charge. The region search charges
  them too (chains and corridors). Every scan, a group whose route is `kOpeningHold` (300) ticks old asks the region
  search where its members would go with today's queues (`predictEntrants`); if a quarter or more of its queue area
  would move (64 cells or more), it re-plans (`openingDrift`), and the move is committed in the table before the next
  group is judged -- one group at a time in id order -- and kept while its replacement builds (without the commitment
  two groups trade places at every recount).
* **Subgroups for allocation.** A group of `kAllocMembers` (128) or more is cut by position into `kAllocCells` (64)
  squares of its members. For the opening with most of its queue it compares, per square, the region search's way
  through it and round it (closed), and moves squares, the cheapest detour first, to one other passage while the queue
  left behind still exceeds the other one plus the square's detour (`allocateQueue`). The moved members become a new
  group of the same order (`splitGroup`: seeds, command and kind copied; member state, formation slots and point kept),
  which plans its own route; each subgroup is judged on its own afterwards. `ctest:legion_choke` (`bridges-1000` and
  `maze-1000`: at least 450 arrive, at most 80 stand stuck at the end, maze-1000 splits a subgroup).

### Admission at narrow passages

An opening at most `kNarrowWidth` (16) cells wide is admitted one direction at a time (`admissionHold`). Retail gets
neither army across a long bridge in the fixtures (`bridges-100`, `taperbridge-100`), so there is no Retail rule to
copy here.

* **The passage.** An opening is one crossing of one sector boundary, so a passage longer than a sector is several
  records. `passageOf` maps a narrow record to the passage's holder: a passage is a connected component of narrow
  records on one side with overlapping spans, two records linked where either one's strip (`stripLo - 1 .. stripHi`)
  holds the other's crossing line. It is a closure because a strip is measured from its own record's width (a tapered
  bridge's mouths' strips hold the middle's lines but not the other way round). The holder is the component's lowest
  number. It keeps the token, the waits, the occupants, the strip and the zone, and members are admitted with the
  holder's label. The holder is cached per record by strip epoch, plane and the count of narrow records
  (`Opening::head`, `headGen`, `passageGen`; derived, never hashed). A new record re-reads the components; if it joins
  two passages already in use, the lower holder takes over.
* **The strip** is the run of cross-sections along the axis across the line no wider than the opening + 2 cells (a
  bridge's length, a door's wall; `ensureStrip`, per plane epoch).
* **Admission.** A member whose next opening it is and that stands within `kAdmitZone` bodies of the strip takes the
  token when the passage is free or open its way and no allied body the other way is in the strip, and otherwise waits
  (order kept, stall clock stopped). Admitted, it passes until it leaves the zone. Any member standing in a watched
  strip is an occupant whatever its route says (`watchStrips`), and a passage with a token stays watched
  (`activeOpenings`), so a body that never asks still counts once it stands in the strip. Every soft group obeys
  admission, small groups and tile chains included (`Field::chainAdmits`, hashed with the field: a chain admits its
  members for as long as they steer by it). Only allies defer to each other; a group without labels neither waits nor
  counts. Open ground is never an opening, so nothing waits there.
* **Turns.** The token turns once the strip has no occupant its way and nothing was admitted for `kAdmitClear` (60)
  ticks. Admitted bodies still walking to the mouth (`Opening::com`, derived per tick like `occ`: only bodies before
  the strip on their own way in) hold the way until `kAdmitCommit` (120) ticks after its last admission; past that
  they lapse. A way closes after the other has waited `kGreenTurns` (2) of its crossings, or `kAdmitGreen` (300) ticks
  if that is longer. Its crossing is the longest among its admitted bodies of the approach zone plus the strip at the
  body's top speed (`crossTicks`, derived every tick from the passers; the token fields it decides are hashed). A door
  or a short gap keeps the 300 ticks; a long bridge with slow bodies turns about once (the first army crosses whole,
  then the other), because every turn puts the next way's crowd onto the apron the last way is coming out onto.
* Tests: `ctest:legion_passage_admission` (bridges-100 at offsets 0 and -5: at least 95 arrive, at most 2 stuck),
  `ctest:legion_passage_records` (crowdbench opposingbridges 100x1: at least 90 in goal), `ctest:legion_passage_taper`
  (taperbridge-100 at 0 and -4: at most 2 stuck), `ctest:legion_passage_green` (taperbridge-100 at +1, -5 and +8: at
  least 98 arrive, at most 2 stuck) and `ctest:legion_passage_small` (`taperbridge-small`, an army of 50 against ten
  orders of 5 sent 150 ticks apart, at 0 and -8: at least 95 arrive).
* Rejected: turning only once the far apron is clear too (the other way's waiting crowd stands there), no lapse at all,
  a commit window scaled to the zone walk (neutral), holding the token while an admitted body stands in the mouth
  (slower), waiting bodies parting for the stream coming out or stepping out of the passage's lane (the knot moves),
  and short reservations at chokepoints (admission already serialises the passage; bookings that bind allies across
  groups make two armies refuse each other's cells).

### The passage gate

A formation member of a point part of 16 or more (the pinwheel's eligibility) commits to a gate when a short passage
comes within 12 cells of its descent chain (`gateCommit`), so a gap two or three bodies wide is taken two or three
abreast instead of in single file down one cell column of the shared field. A passage is a strip narrower than the
formation's band (`B = 2*s0 + (K-1)*(foot+1) + foot`, `K` = clamp(3/4 of the square root of the point's members, 2,
10), `laneCap`) and than the passage lane grid, two to `kGateRun` (24) chain cells long and not within the lane-near
distance of the destination; the gate is the wall-to-wall cross-section at its narrowest cell (`gateSpan`). The member
picks the gate point (points at footprint pitch, centred, as many as the free width holds) with the fewest same-player
bodies queued within 6 cells before the line, aims at it while it is in a straight legal line, and is released once it
crosses the gate line. The static geometry is memoised per plane epoch (`nearObstacle` and its vertex probe, which
steering does not read); soft narrowing is computed at use. Members committed to a gate skip the pinwheel. `Member::gateMemo`
keeps the inputs of a call that declined in open ground (the static plane, the finished field, the member's goal,
origin, footprint and lane band) and the same inputs decline again without walking the chain (never hashed;
`TAK_LEGION_VERIFY` recomputes every hit). Hashed only when set: `gateCell`, `gateLane`, `gateAxis`, `gateSign`.
`gate_commits` / `gate_releases`, the `pivot_*`, `lane_*` and `vertex_*` work counters and `pass_filter_holds` are
instruments and never hashed. The vertex probe (`vertexInstrument`), which no decision reads, runs only while
`LegionNavigator::setInstrumentProbes` is on (`legion_scenario` unless `--no-vertex-probe`, and `legion_world_test`;
the game does not).

Bodies arriving abreast are turned by the field at slightly different times, which the lane-crossing observer counts
as a flip where single file never does, so `legion_check` judges the gap fixtures (`gap6`, `gap8`, `gap10`, `gap14`,
`gap20`, `gapsweep`) per file: legion crossings / files abreast against Retail's crossings / files abreast
(`tools/legion_check.py`, `per_file_floor`); the corner and head-on obligations are raw.

### Passage lanes

A formation member (a point part of 16 or more; not on a fight, attack, guard or patrol leg) inside a short passage
keeps its lateral place until its footprint is clear of the far end (`passageLane`): it aims at the first origin past
that end at its own lateral origin, over its straight walk, the field and the pinwheel, and a blocked step flows round
toward that cell as round an arc point. A gate-committed member aims at its gate point until it crosses the gate line,
then keeps its lane through the rest of the strip. Without this, the bodies that come through after the crowd has
thinned walk straight for their slots, diagonally across the strip, through the lanes the earlier bodies held. Slots
in the gap's order would not keep the lanes (pairs whose slots lie in that order swap as often as the rest), so the
slot assignment is unchanged and no lane record is shared: each body keeps the place it has.

The strip is read from the passage gate's static cross-sections at the body's origin: narrower than the formation's
band and than `kPassageSpan` (32) origins, two bodies abreast or more, open along the other axis, and cut in a wall
(the obstacle on either side runs on for the strip's width, across the axis or along it: not the room between two
rocks); a run of 2 to 24 such origins along the axis at the body's lateral place, with legal origins at both ends; the
field's descent there steps along the axis toward the end of lower potential, past which the axis goes on for a
footprint (not a pocket against a wall face); and not within 32 cells of the destination. At 32 origins the band
alone decides for the common footprints (a 2x2 formation's widest band, laneCap 10, is 31 cells), and a gap wider than
the band has no lanes. The same cap marks the cross-sections that make a paused route serve the lanes their region
(`laneTrigger`). Nothing new is stored or hashed; the checks past the first cross-section count in `lane_work`.
`ctest:legion_passagelanes` (`gap14` over the core five: no swaps across the gate) and `ctest:legion_widegaplanes`
(`gap20-100` over the core five: no swaps). The lane follows the field, not the member's own slot, and the guard
against the destination is the field's 32 cells: a destination area that reaches farther than that from its point
(very large bodies, or a formation of many hundreds) leads a member whose slot lies behind it through to the far end
first, and the settle rule places it from there. No fixture has such an area at a passage; a guard of the area's own
reach is the fix to try.

### Corner lanes

A corridor narrower than `kLaneSpan` (8) origins and longer than `kGateRun` is left to the field and its lane grid
(the pinwheel's look-ahead ends at it, the passage gate and the passage lanes take only short strips), so round a
corner between two such corridors every body would descend round the inner corner and walk the straight line to its
slot along the inner wall. `cornerCommit`, `cornerAdvance`, `Member::gateCorner`: a lane member (`laneMember`: a
formation member of a part of `kPivotMembers` or more; any leg kind, fight included) inside a gate strip of two lanes
or more, whose descent turns into a strip across it within `kCornerLook` (12) chain cells, commits as to a passage
gate: its point lies `kCornerLead` (3) chain cells into the new strip, in the lane counted from the new strip's inner
wall that equals its own lane counted from this strip's inner wall (a column wheeling, so no file crosses another),
clamped to the new strip's lanes (`laneCap`, footprint pitch, centred). The point is held over the straight walk too
(`gateCorner`: a gate point otherwise steers only a field follower). When the member crosses the point's line the
point moves `kCornerLead` origins ahead of it in the same lane while it is in that strip, the field still leads along
it and it is `kLaneNear` or more from the destination; otherwise it is released and the field, the line and the area
rules take over. The decision reads only the static plane, the finished field, the goal, the footprint and the band; a
decline is memoised per member as `gateCommit`'s is (`Member::cornerMemo`), and a member off every strip declines
before walking a chain. `gateCorner` is hashed with the gate point. `ctest:legion_corner` (`corridor-corner-40`: files
past the corner).

### Corridor mouths one body wide

A corridor one body wide has a single row of origins, and the origin in front of its mouth is the only way into it.
Two bodies of a queue that reach the mouth on either side of that origin each stand on a cell of the way the other
needs, and nobody behind them moves. Retail's bodies wedge there as well and give their orders up where they stand.

* **Funnel claims** (`passageWay`, `updateClaim`, `claimedAgainst`). A member waits for a way when it stands beside a
  passage one origin wide: the lowest neighbour origin down the field that lies in such a passage (both its
  neighbours across the step illegal), or whose only way on does (the origin in front of a corridor's mouth). A seed is
  never a way, and a body hugging a long wall is not beside a passage. A member standing on a cell of a way its group
  found there waits for it too (`Group::funnelWays`, per field; a 3x3 or 4x4 covers a way from farther off). While a
  member waits for a way, no other member of its group may step onto an origin whose footprint covers that way and is
  not past it (nearer the goal), unless it waits for the same way and steps onto the way itself. At one potential a
  claim yields to a member that waits for a way of its own and has the lower id, while a member that waits for no way
  still keeps off a claimed way at its potential: the order is total, so no two waiters hold each other (a field of
  pillars with gaps one origin wide, `ctest:legion_funneltie`). The claim is taken on the step that reaches the origin
  (drive), so two bodies stepping beside one way on successive updates of a tick cannot both stand there; a queue in
  file is untouched. `Member::claimAt` and `Group::funnelWays` are hashed.
* **The funnel give-way** (`giveWay`, after 2 held updates). Where two bodies did reach a way at once, the one nearer
  the goal (lower potential; at one potential the one that gave way more often, then the lower id) asks each held body
  of its group on the way's cells, farther up the field, to step one cell clear of the way and of itself, to the free
  legal origin with the lowest potential, keeping its order (a part with its leg, as `partLane` parts a still member).
  All or none move; at most `kMaxGiveWays` (8) per leg, never straight back the way it last gave way. The turn-taking
  at one potential matters: with the id alone one body gives way to a whole lane.
* `ctest:legion_funnelmouth` (the closed 120-body corridor at start offsets 0 and -1, the 40-body room at +12 and +14:
  all settle).
* Rejected: the give-way alone (the pair forms again with the next arrivals), one exclusive claimant per way (a third
  body edges onto it from an origin with several ways down), and a funnel as any origin against terrain with a single
  way down (claims chain along walls).

Wall touching in doors and along ring walls is measured (`wall_touch_permille`) but not steered: preferring a
neighbour with a free cell to the wall, or refusing a straight walk whose sweep touches a wall, each trade arrivals or
lane order for a few permille.

## 7. Rings: attack and guard

### Engaged bodies

An attacker that `tickCombat` brakes in reach (or a guard within 70 px of its ward) is Engaged (member state 6) until
its next `move()`: to local steering it is settled and still (bodies behind walk round it), it never yields or parts,
and it is soft to other commands; an engaged attacker, or one held on its own ring spot, is soft to its own attack too,
so a refresh goes round the firing arc instead of into it (guards are soft to other commands only). A reach kind's
held same-target peer (30 ticks) is settled too, and reach kinds never run the settle rule or contact arrival: the
owner ends the approach.

### Ring planning

An attack group with two or more members when its first field starts plans to a ring (`buildRing`) and builds its
first field to done (a ring is planned from a done field, `ringClaim`). An attack group of `kLateRingMembers` (8) or
more round a target standing still, with no ring, a finished field and no refresh pending, plans its ring once later
(`ringScan`, `Ring::late`, hashed) and refreshes to band 0; members claim spots as they next move. A fight order's
column acquires its target one body at a time, so its attack group starts with one member and gets its ring this way;
at two members every small battle group would re-plan. A chase (a moving target) is left alone, and a group that grows
to two after its first field keeps its point seed (re-planning costs a refresh per target).

* **Bands.** Band 0 runs from 1.5 bodies inside `tickCombat`'s own reach out to the reach edge (melee at contact),
  legal, in the members' component and in line of sight of the target, and is the field's seeds. Band 1 is the rest of
  reach out of the target's sight (behind its walls), where nothing brakes a body. Bands 2+ wait beyond the edge, only
  on the half facing the members (waiting round the far side, they stand across the lanes to it). With no spot of the
  members' region in sight of the target, the whole of reach is band 0, planned without the sight test (`Ring::blind`,
  set only by `buildRing`'s pass 1 and folded into the state hash), and no spot of a blind ring is spent for a body
  standing radially out at the edge. A ring is shadowed (`Ring::shadow`, derived in `buildRing`) when it is blind or
  band 1 lists a spot: walls hide the target from part of the ground in reach. The rules keyed on the shot needing
  sight key on `Ring::blind` and the press path's own flag, never on `!Ring::sight`, which is also false for a ring
  whose shot needs no line of sight at all (`reachPx`: melee, lobbers, ranges of 64 px or less; `ctest:legion_ringnosight`).
* **Deep firing rings** (see below) thicken band 0 when it is short of spots.
* **Deep rings** (narrow approaches, see below) hand spots out on arrival, back to front.
* **Guards.** A guard group plans to its ward's ring point (`World::guardRing`) with no reach ring: its stance is
  Follow_Ground's own ring round the ward (a ring there keeps guards re-choosing spots).
* **The inside of a ring is charged in its own field** (`fieldAvoid`): a mover corridor disc (kSoftFactor inside,
  kSoftNear within 32 px) of radius just inside band 0 (inside the thick band's inner bound), centred on the target. A
  way to a far spot that crosses the ring ends at the first arc it meets (combat brakes the body there); with the disc
  the queue walks round the outside of the arc, as Retail's blocked bodies slide round it (`ctest:legion_ringfill`).
* **Moving targets.** A ring round a chase re-seeds in place (section 2's moving goals) and is never thick.

### Spot hand-out and upkeep

* **The first claim** hands out every spot (nearest members to band 0, matched in angle so lanes do not cross;
  innermost layer first on a thick ring, below); late joiners take the nearest free spot (`ringNearest`); a member held
  a 45-tick window re-chooses (`ringRechoose`, 3 times) a free spot in a lower band or nearer in its own (never on a
  deep ring: a nearer spot is a shallower one); an engaged body lets go of a spot it stopped short of. A buried owner
  swapped onto an outer spot is not handed a band-0 spot again in the same scan.
* **ringScan** (every `kRingScan`, 30, ticks, a ring group with a finished field): a band-0 spot is **spent** when a
  still body (engaged, held, settled, or no Legion mover) covers its footprint, or the footprint where a body walking in
  would stop (the reach edge radially out from it, as an origin by `footprintOrigin`'s `(e - (f-1)*8) >> 4`; an inner
  spot of a thick ring stops at the spot itself). A spot is spent too when it is **closed off**: the origins out to two
  bodies beyond the reach edge are flooded from the free origins a body or more beyond it, through origins no still
  body covers (the ring's own held members do not wall it), walking on only where nothing brakes a body: out of reach,
  in reach out of the target's sight (each origin's own sight test, with the sight grid, target position and skip
  `buildRing` used, kept on the ring), or anywhere when band 0 was planned without sight; a band-0 spot whose stop
  point is not flooded is spent, and a band-1 spot is spent when a still body covers it or the flood reaches it by no
  way from outside reach (`ctest:legion_ringshadow`). The seeds become the unspent spots and a refresh plans to them
  (`work.ring_reseeds`). An unspent spot whose owner has been held elsewhere for 90 ticks is swapped with the outer
  spot of the nearest waiting member at least a body nearer to it (`work.ring_swaps`; band-1 spots too, taken only by
  members of the outer bands); members owning a spent spot, or waiting outside band 0, take the nearest free unspent
  band-0 spot, else a free unspent band-1 spot, else the nearest free outer-band spot (spare attackers wait outside
  instead of standing behind the arc). Re-choice never picks a spent spot. A body in reach stays in reach: it may move
  to a free band-0 spot but is never sent out to an outer band, and of a blind ring it keeps its place.
* **Moving groups bend round other commands' firing rings** (`ringBend`). Every `kRingScan` ticks ringScan lists the
  rings round a still target as discs: the reach, or the farthest member within four bodies of it, plus a ring body's
  half. A formation member whose lane to its own slot passes within 1.5 clearance radii (disc + its own half + 16 px)
  of another command's ring, not one round its own side, steers by the potential flow round that clearance circle in
  a stream along its own lane: aim two bodies along the flow's heading, re-aimed at each new origin, when the line
  there is legal and clear of soft bodies. Lanes keep their order and side (a lane through the centre goes by the unit
  id), so the group parts round both sides. From inside the circle, with its slot inside it, or with the line refused,
  the body keeps the field. The bend looks no farther than the line probe (`kFormationLineCells` past the 1.5 radii
  circle), only when its own lane up to that circle is clear of static blocks, and never for a livelocked body (a
  U-shaped wall on the lane short of the ring otherwise takes every bend into the pocket and the field back out:
  `ctest:legion_ringbend_pocket`). `ctest:legion_ringbend`. Rejected: aiming at the clearance circle's tangent point
  (one file per side again), a clearance of the reach alone, and a cap of three clearance radii.

### Pressing into reach

Combat brakes an attacker the moment it is in reach, so the bodies of a ring stand on its reach edge, and a body held
a few pixels out needs a way in.

* **pressIn.** An attacker held within two bodies outside its reach looks at its own origin and its 8 neighbours. An
  origin is a candidate when the nearest point of its footprint-centre box to the target is in reach as `tickCombat`
  tests it -- its float distance to the target against the float reach (`reachFloat`; `dist > reach` is the
  out-of-reach test, so the reach edge itself is in reach) -- and in sight where the shot needs it. A melee weapon is
  not radial (`meleeInRange`: footprint adjacency, its TDF range ignored), so `reachFloat` returns 0 for it and a melee
  body keeps the integer test against `reachPx`'s contact distance (`ctest:legion_meleepress`). The body walks to that
  point (`Member::pressX`, shortest walk first, unturned at the facing cap, ended after `kPressTicks` = 120 updates, a
  block, or leaving its origin's neighbours) and combat brakes it on the way; a press ends exactly at its point
  (`work.reach_presses`). A body in reach but out of sight presses on to a point in sight. Out of sight at the box
  point nearest the target, a press also tries the box's far edge toward it, the last sub-pixel of the origin: Retail's
  attacker pushing at its target against a wall is clamped there (`footprintClamp`, 4db010), its float centre rounds
  into the next cell, and the sight cast (`losBetween`) starts in that cell, so pressed flush to a 1-cell wall it fires
  through it (`ctest:legion_attackflush`). A press walked out of the target's sight (`pressHidden`) keeps travel speed:
  nothing brakes the body before its point. Pressing toward reach at travel speed lets braked bodies glide on and
  press again, so it keeps the cap.
* **Blind and shadowed rings.** A body of a blind ring held out of reach on its own spot, or whose spot's centre is out
  of reach, presses within its own origin to the first point `kPressDepth` (4 px) inside reach on its way toward the
  target (on the very edge the crowd's nudges put it back out), and a blind ring's member standing in reach holds there
  instead of walking to a goal centre out of reach (`ctest:legion_ringblind`). A body held out of reach of a shadowed or
  blind ring, with no point in sight a step away, that will stay there (on its own spot, or its spot's centre out of
  reach) presses to the first point `kPressDepth` inside reach of its own origin or of a free neighbouring origin; into
  a neighbour only where no moving member stands in or next to that origin (a body walking past takes it back before
  the press arrives). A press already at its point is no press. A member of a shadowed ring standing in reach out of the
  target's sight that has pressed in at the target during this order (`Member::shadowPressed`) holds there rather than
  walk to a goal centre out of reach, and its hold walks on through the shadow to the shot (`pressRoute`); only pressed
  bodies hold, so bodies merely walking through the shadow strip are not parked in the way
  (`ctest:legion_ringshadowpress`, `ctest:legion_ringshadowmixed`).
* **The walk on through the shadow** (`pressRoute`). A body held in reach out of sight with no point in sight a step
  away searches (radius 6 origins, every `kPressRouteEvery` = 16 held updates) over free origins whose centre is in
  reach and out of sight for the nearest origin whose centre is in reach and in sight, or failing one, the shadow origin
  nearest the target (half a cell nearer at least); the way is committed as a local route, walked at travel speed round
  a still target, and the body stops and restarts its hold clock as a detour's route does (`m.state=Holding;
  restartHold(m)`), so it never re-plans every update (`ctest:legion_ringroute`). `work.reach_press_routes` counts them.
* **The press wait.** A press waits while every body on its point's origin is a member on the move (`pressWaits`),
  bounded by `kPressTicks` like the walk, and a press that ended in reach is no repeat's origin (`pressEnd`). The
  press-repeat count (`Member::pressFrom`, Stats only, read by no decision) marks an origin pressed from again; a press
  dropped blocked before the body left its origin does not mark it. `ctest:legion_pressin` requires no repeat at any
  gate offset (`work.reach_press_repeats`).
* `ctest:legion_pressin`, `ctest:legion_ringedge` (f1-64: the reach edge counts as in reach), `ctest:legion_ringpocket`.

### Rings round still targets

* **Speed.** Any attack or guard member round a target standing still (`stillTarget`; a guard's target is its ward)
  walks its detour routes to their last cell and its committed shuffles at travel speed, ring or not. A chase (a moving
  target or ward) keeps the cap: there a faster ring kills a fleeing target with fewer bodies in reach and lets the
  rest of its group get away (`attack-100`).
* **The still-window rule.** A member round a still target that its last crowd window found still
  (`Member::stillSince`: it gained under half a body on its point, and on its own spot, over the window) commits no new
  flow-around shuffle; it holds, and the hold's ways out take over. Behind an engaged arc a blocked body otherwise flows
  round to the neighbour of lowest potential, the field's string-pulled aim cuts back across the cells it left, and it
  flows back: each flow-around is a potential gain, so the stall clock never lets a hold's way out start. A committed
  shuffle still runs to its end. No new state: `stillSince` is hashed. The rule skips (`shadowWalk`) a member that
  stands out of the target's sight (`pressHidden`), a member holding a band-1 spot of a shadowed ring, and a member
  holding a band-0 spot of a shadowed ring once its crowd windows have found it still for `kShadowStill` (4) windows
  running (180 ticks): along a wall the members found still are the queue short of the shadow or of the pockets between
  the arc and the walls, and with no detour round the arc they would stand. Waiting members (band 2 and beyond) and
  band-0 members just found still keep the stop, as on open ground. The delay is a knife edge on
  `ctest:legion_ringpocket`.
* **Fight legs.** A fight order's members still on their fight leg (before they acquire, and behind the arc while the
  bodies ahead fire) walk their flow-around shuffles as a formation move leg's (`detourFlow`). Attack, guard and patrol
  shuffles follow the rules above.
* Tests: `ctest:legion_ringstill` (r200: statue ticks and bodies ever in reach), `ctest:legion_ringclosed`
  (ringcross-100 by tick 1,500), `ctest:legion_ringwait` (ringcross-100's Move group t90), `ctest:legion_fightstill`,
  `ctest:legion_guardstill`, `ctest:legion_ringroutespeed`, `ctest:legion_ringshuffle`, `ctest:legion_fightshuffle`,
  `ctest:legion_fightring` (fight-60: half in reach by tick 1,300).
* Rejected: no flow-around once the stall or headway clock aged a crowd window (the slide round the arc stops too), a
  clock of the best distance to the member's own spot (new state for the same result), a stalled member's shuffle
  measured toward its own spot, barring the step back onto the origin it last left (longer cycles), speed only when the
  body in the way fires or only outside the point's area, the cap again for a fight member found still, a late ring at
  two, four or sixteen members, a late ring sized for `kRingCells` spots, and a stop point a pixel inside reach.

### The walk on past the shot

A body braked flush in a corridor's mouth plugs it: the sight cast skips the cell beside a big body, so a 2x2 body
flush to a 1-cell wall sees the target through it, combat brakes it as soon as it has the shot, and the queue behind it
stands out of reach. Retail's corridor fills deep-first: its first body walks the corridor's out-of-sight lane before
it comes flush, and the next is braked behind it. `walkOnShot`, called by `tickCombat` in Legion mode just before it
would brake a ground attacker in reach and in sight: a member of an attack group with a ring, round a still target,
whose weapon needs sight and is radial, walks on instead of braking when all of the following hold.

* **Walls pinch it.** The legal origins of the ring round its footprint (origins touching it without overlapping it)
  fall in two or more runs. This test uses static legality only, so a body braked in the open, or against a single
  wall, is never tested further (on open firing lines the walk on is not Retail's behaviour and costs Legion work).
* **Its queue comes up to it.** A first round search finds which members of its own group -- moving, waiting or held,
  within a `kWalkOn` (10) origin box -- and of other groups on the same target have ground that touches the ring round
  this footprint; in that search the braked attackers of this target are passable (they are the queue's front, not a
  wall), each looked at in turn. Only those members are the queue.
* **It plugs ground with the shot.** A second search starts from the queue and goes round this footprint over
  statically legal origins that no still body covers (the group's own held members are the queue, not still; a
  diagonal step that cuts its corner is refused too); a third starts from this body. A shot origin that only this body
  reaches is ground it would plug. The shot test is pressIn's: the point of the origin's footprint-centre box nearest
  the target is in reach as `tickCombat` tests it, and the target is in sight from that point or from the box's far
  edge (tested at origin centres, a column standing exactly at reach finds no shot origin).
* **Onward only.** A walk records the origin it set out from (`Member::walkOnFrom`, hashed); a later walk never ends
  nearer that origin (Chebyshev) than the body stands now, so a body in a column met by the queue from either end does
  not walk back (`ctest:legion_walkononward`).

The body walks to the farthest such origin (breadth-first) as a local route at travel speed, and combat does not brake
it until the route's last origin. The next body comes up behind it and does the same, so the corridor fills from its
far end. A look is repeated from the same origin every `kWalkOnEvery` (30) ticks, since the queue arrives after the
first brake. A member takes at most `kWalkOnMax` (6) walks per order (`work.walk_ons`; `Member::walkOn*` are hashed).
`ctest:legion_gatewalkon` (attackring-gatehouse-40 offsets 0, 1, -1) and `ctest:legion_gateyard` (attackring-gateyard-36,
a second gatehouse with a 4-cell corridor, gates on two sides and mixed 2x2 and 3x3 attackers).

### Rings through narrow approaches

* **Deep rings** (`ringDeep`, `Ring::deep`, hashed). `buildRing` floods its static box from the ground beyond reach
  (ringScan's flood start) through origins where nothing brakes a body; a band-0/1 spot's walking depth is the steps to
  where a body walking in stops. The ring is deep when some spot's depth exceeds its straight-line depth (the reach
  edge plus a body, less its distance) by `kRingDeepExcess` (3) bodies: a gatehouse entered through its outer gate is
  deep, a walled box is not.
* **Spots on arrival, back to front** (`ringDeepClaim`). A deep ring hands nothing out at its first claim; each member
  claims within `kRingWaitReach` (8) bodies of reach, and ringScan re-hands spent spots and waiting members the same
  way. The member walks (`ringWalk`: a breadth-first walk over the box from its own origin, through origins no still
  body covers, on only where nothing brakes it, an origin with the shot reached but ending the walk) and takes the
  nearest free unspent band-0 spot it reaches (band 1 when band 0 has none) that lies on no other candidate's way: an
  origin of another's walk overlaps it *and* a body standing on it would leave that other unreached or more than
  `kRingDetour` (3) bodies farther (a confirming walk with its footprint closed; a 4-cell corridor passes a body, a
  3-cell one does not). Only when no spot is reached so, the walk through the shot (a yard entered through its gap: the
  body is braked where it gets the shot, as Retail's). Reaching none, it waits on an outer band, never on a spot beyond
  a wall by straight line. On open ground nothing lies on another's way and this is the nearest spot by walk.
* **Churn.** A deep ring's member gives its band-0/1 spot up to wait outside only when the spot read spent on the scan
  before too (the previous scan's `Ring::spent`); the owner of an unspent band-1 spot keeps it unless the walk reaches
  a band-0 spot, or one of its own band strictly nearer by walk than its own, and it looks only every
  `kRingDeepRechoose` (2) scans, by id (`ctest:legion_ringdeepchurn`: the busiest tick stays under 400,000 work units).
* **The walk to the spot** (`spotRoute`). Within two bodies of reach, a deep ring's member whose straight line to its
  band-0/1 spot crosses a wall or the shot commits the walk's route (`m.route`, at travel speed round a still target)
  instead of the direct rule or the field; through the shot only when there is no other way; with still bodies walling
  the spot off it re-claims or holds (`Member::spotFrom`, hashed, keeps the search from repeating at the same origin
  except every `kPressRouteEvery` held updates). The shot test is conservative: any corner of the origin's
  footprint-centre box, or its centre, in reach and in sight, and half way across a step. `work.ring_spot_routes`
  counts the routes.
* `ctest:legion_ringdeep` (gatehouse-40 offsets 0, 1, -1: median damage at least 400).
* Rejected: the spot walk on every shadowed ring (the walled boxes lose bodies in reach), spots handed at the first
  claim deepest first (the front goes to shadow spots in a corridor with no shot), the on-way test without the
  confirming walk (a 4-cell corridor reads as one body wide), a body held on its way to a deeper spot not pressing to
  the shot (the hidden press is most of a gatehouse's damage), and ring-planning changes for walled targets (band 0
  widened to all in-sight ground, deepest-first filling, hand-out by walking distance, a legal straight line for the
  near-spot walk), none a net win.

### Deep firing rings

A ring of ranged attackers stands at least three rows deep when it has the bodies for it, so the ranged units in the
back can shoot too; melee units still go round.

* **Thick band 0** (`buildRing`, `Ring::thick`, hashed). When band 0 holds fewer spots than the group has members, it
  is re-planned `kRingThick` (3) bodies deep, then one body deeper at a time while still short, never nearer the target
  than its touch distance. Only a depth that adds spots is kept (the shallowest of those with the most): round a walled
  target the deeper ground is often all behind the walls. Not a melee ring (its reach is at touch: no room), not a
  blind ring, not a narrow approach (a thick ring that `ringDeep` finds deep is planned thin again), and not round a
  target on the move (a chase re-seeds its ring in place).
* **Inner spots** (`ringInner`: band 0 more than half a body inside the reach edge) are where their owner stops: the
  stop for `ringDeep` and ringScan's spend is the spot itself, not the reach edge out from it, and the flood walks on
  through the thick band.
* **Innermost first.** `assignRing` hands band 0 out by layers a body deep, the innermost layer to the members nearest
  the target, so the first bodies walk deepest and the ground behind them stays open; a fresh claim (`ringClaim`'s
  `ringNearest(..., deepFirst)`) takes the innermost free unspent layer too.
* **The walk in** (`walkInRing`, from `walkOnShot`, so in `tickCombat` just before the brake): the owner of an inner
  spot is not braked while it walks in to it, in reach the whole way. Whether it fires on the way depends on the
  weapon: a unit whose shot needs it to face the target rarely aligns while walking, but the deeper ring still pays in
  damage. It is braked once its centre is as deep as its spot, once held 30 ticks (`kWalkInHeld`), or once a crowd
  window found it still; `engaged` then lets the spot go for the members behind. A body making a press step (`pressIn`)
  is braked by combat. `work.walk_ins` counts the walking ticks.
* `fieldAvoid`'s disc shrinks to the thick band's inner bound.
* Melee and ranged sent together at one target: the melee arrive first and take the contact ground; ranged sent after
  them still leave the melee their reach to the target.

## 8. Flyers

Flyers stay native: Retail's flight model and missions fly them in both modes. Legion changes flight in three
deliberate ways, all Legion only.

### Legion flyers never land on other flyers

Retail's landing-site check `509400` (ported exactly for both modes, see [pathfinding-port.md](pathfinding-port.md))
lets a group of flyers ordered to one spot land on top of each other. A flyer that has begun its descent is still
airborne, so it is only in the airborne word `+2`; the owner of a cell several flyers share there is redrawn every
tick, and eight or more overlapping flyers leave only the `0xffff` overflow value. `509400` treats both as free, and it
accepts an unexplored site without any test (`tools/re/check_landing_overlap.py` runs this natively: most scenarios
end with landed flyers overlapping). Retail's air traffic (unit +0x12a, [retail-engine.md](retail-engine.md)) makes
each flyer of a stack step out of it from VTOL_Standby or VTOL_LandIfCan before it looks for a site, in both modes, so
the hovering stacks behind most overlapping landings do not form; `509400` itself is unchanged.

`World::flightLandingFree` adds `flyerLandingOccupied` in Legion mode only, after the retail predicate. It refuses a
footprint that overlaps a live, landed flyer (mode 1), or a flyer whose landing mission is in descent (stage 3),
whatever the exploration. Both states are the units' own hashed state, so no new state is added. The check runs in
unit order, so a simultaneous descent serializes: the later lander sees the earlier one's stage 3 in the same tick.
The refused flyer then runs retail's own candidate search (`417188..4172ff`) to find another site. Test:
`retail_motion`, where 12 flyers stacked on one point land with no shared cells; it also covers 8 flyers arriving over
a landed one, and the same at a site that is unexplored for them.

### Flyers stay with a mixed formation

Retail (`417c20`, VTOL_Move) runs Move_Ground's two group checks: a flyer far out of slot around the surface group's
centre is sent back to it by VTOL_Move_Formation (`417750`). A flyer that is ahead of the centre a second time has a
1-in-4 chance to drop all its orders (`50c480`, `4d6a50`). Retail never caps a flyer's speed: VTOL missions lack the
`0x2000000` pacing bit, and the group record holds only ground and boat speeds. A fast flyer therefore shuttles between
the goal and the group, or is stranded where its orders were dropped. This is ported for Retail mode (see
[pathfinding-port.md](pathfinding-port.md)). Legion has no group records; it keeps a formation's flyers with its
ground instead.

`World::planLegionFlightStations` (Legion only) runs every tick. It covers a flyer in an Alt+N formation that has
ground members still under way, when the flyer's head order is a plain move, fight-move or patrol leg with no target
or job. A Ctrl+N group is not a formation: its flyers fly their own orders at their own speed, as a plain selection's
do. The flyer is paired with the ground of its own click: the busy ground members' last orders fall into up to 4
buckets per formation keyed by (order class, `convoyTick`), and a flyer whose last order carries the same key keeps
station over that bucket (a 5th click counts `stationOverflow` and flies free). The arrival-area table puts a flyer's
click in its ground's area. Two clicks of one class applied in the same tick share a key and so a station: a known
limit.

- **Station (over the front).** The flyers ride over the front of their ground, not its middle: `legionFrontSlots`
  lays rows of lanes `16 * foot + 16` px apart across the lateral extent of the bucket's busy ground members (a member
  that has arrived waits at its slot and does not count). A lane holds over the frontmost body within half a spacing
  of it, a body farther out counting that much farther back, so the front is continuous in the bodies' positions; the
  front row fills first, a short last row spreads over the whole width, and each further row sits a spacing behind. A
  row has enough lanes that the rows number at most twice the lanes: over ground narrower than that (one body, a column
  one or two wide) the row spreads that wide about the ground's centre line, a lane past its edge taking the front at
  that edge, so the flyers stand at most twice as deep as wide instead of in a file trailing behind. Flyers take the
  rows in order of their distance behind the front and each row's lanes in order across, so a flyer in formation keeps
  its station and a turn re-ranks the flyers where they stand. Within a quarter spacing of a lane boundary they keep
  the layout of the lane count across it when they stand nearer it, so a formation whose width hovers at a boundary
  does not reshuffle them. With no direction of travel the stations fall back to a square spiral over the ground
  members' centroid (a squad with one bucket: all its ground; with several, each bucket's busy members).
- **Release.** When the ground stops, the flyer is released for its order and flies it uncapped to land, as retail's
  VTOL_Move ends: release A when no busy member of its bucket has made headway for 450 ticks
  (`LegionNavigator::advancing`; a member with a weapon reloading counts as engaged, so a fight does not free the
  wing), release B when the bucket's centroid has stayed within 16 px for 900 ticks with nobody engaged. Both are
  sticky on the flyer's order (`Order::stationFree` and the B reference, hashed when set). A dead-end queue, a sealed
  wall or an unreachable goal does not keep the wing hovering.
- **No yaw on the spot.** A station flyer with no horizontal velocity (the vertical take-off at the start of a leg, or
  held still by the ground's pace) does not turn toward its lead point until it moves.
- **Flight.** The flyer does not steer at the station itself (a point destination puts retail's flight model in its
  arrival regime, where the navigator keeps its old heading inside 16 px and the body creeps). It steers at a point
  160 px ahead of its station along the ground's direction of travel: the sum of the busy members' unit vectors --
  each member's course (the way a moving member steps), toward its current leg for one standing still -- so a column
  turning round a wall turns its front with it. It cruises facing its way and turns and banks with the formation. Its
  script animation is untouched; the Zhon bat's `FlightControl`, for example, flaps regardless of speed.
- **Circling.** Within `kCatchUp` (192 px) of its station a flyer circles instead of creeping at the ground's pace: a
  64 px ring (`kOrbit`) about a point 48 px (`kOrbitAhead`) ahead of the station along the way, steering at the ring's
  point 30 deg on from where it stands, the way round it is already flying (clockwise from a standstill), at half its
  own speed or 1.5 times the ground's, whichever is more. The station carries the ring forward; the neighbouring rings
  overlap, so the wing wheels over the front as a swarm. Retail's in-slot flyer of a moving group loiters too
  (VTOL_Move_FormationStandby `41797e`: a point off its heading, radius 64). Within twice the formation radius of the
  ground's goals the wing forms up on its stations again for the landing. Rejected: a 96 px ring, a ring about the
  station itself, a lead point 60 deg on (flyers fly skewed), and one fixed way round (a flyer told to circle against
  its own way brakes to turn). The pace windows judge the way flown: a circling flyer can end a window where it began.
- **Speed.** `tickFlightBody` caps the flyer's maximum at the busy ground members' mean speed: rising linearly to the
  flyer's own speed when it is 192 px behind its station; falling to zero when it is 128 px ahead; at the flyer's own
  speed when it is more than 192 px from its station, so a joining or left-behind flyer can catch up. While
  travelling, the leg never completes.
- **Arrival (landing in front).** Once the ground's centroid is within the formation radius (`max(256, 32 *
  sqrt(area))` px) of the centroid of its busy members' goals, every flyer of the bucket has its order's point moved,
  once, to a landing point in front of the ground's goals (`LegionNavigator::goalPoint`: each busy member's formation
  slot; a squad of one bucket adds its idle ground where it stands): the same rows of lanes, `8 * (ground foot + flyer
  foot) + 16` px beyond the front and each further row a spacing farther on, clear of the slots. `Order::stationFree`
  3 marks it. If the lanes do not all lie on the map with a lane's room (`16 * flyer foot + 16` px) between each
  flyer's footprint and the map's edge, no flyer's point moves: the wing keeps its own points, where it was sent, as
  in retail (clamping lanes onto the edge pins flyers there to crawl and land). The flyer then flies that order, as in
  retail; the pace cap, measured from its front station but never below a quarter of its own speed, stays until it is
  within 160 px of its point. It arrives, completes the order and lands through the retail landing search and the
  Legion overlap refusal -- in front of the formation, not among the ground's slots.
- **Determinism.** Everything is derived each tick from hashed state with integer arithmetic; the only persistent
  state is the release (or the moved landing point) on the flyer's order, hashed when set.

Flyers ordered on their own, or to another destination, fly as Retail's. Tests:

- `legion_world_test mixedformation`: 20 ground bodies and 8 flyers at 4 px/tick, with a wall the ground detours
  around, under move, fight-move and patrol: the three leading flyers ride within 80 px of the ground's leading body
  along its way and the flyers' mean is 60 px or more ahead of the ground's centroid; an airborne flyer stays within
  240 px of a ground body, and none lands with a ground body ahead of it in its lane; flyers keep the ground's pace in
  every 30-tick window (the keep-up windows start at a 12 px march: under 0.4 px a tick the front is turning, not
  marching; the lead measurement keeps its 9 px floor); flyers face the way they move (at most 5% of moving ticks off
  by more than 30 deg); no hover longer than the 18-tick arrival brake; the landing descent starts in the tick the
  order ends.
- `legion_world_test flyerfront`: a 10 x 3 block of 30 bodies with 12 flyers (more than one row of lanes) re-faced
  from east to south mid-move: on both headings the leading flyers ride over the leading body, east the flyers span
  the full width with the rest in the second row, all land in front, and an air-only formation lands at its own points.
  It also moves 8 flyers with one body and with columns 1 and 2 bodies wide and 10 deep: the leading flyers ride over
  the leading body, at most 4 rows deep, none behind a column's middle.
- `legion_flyers_test ctrlfree` and `formationedge`: 20 ground bodies (1.8 px/tick) and 6 flyers (4 px/tick) through the
  HUD's click path west across an open map. `ctrlfree`, as a Ctrl+1 group, fight-moves and moves to a point 288 px from
  the west edge: no flyer keeps station, none is held below its own speed, every flyer reaches its own point and lands
  near it. `formationedge`, as an Alt+1 formation, checks that the flyers keep the ground's pace; sent 640 px from the
  edge they land in front of the ground, sent 224 px from it the lanes would not fit, so the points stay. The cases are
  in `tools/legion_flyers_test.cpp`.

### Idle landed flyers make way for an allied ground group

Retail stamps a landed flyer into the ground word (`5066f0`), so every ground mover plans and steers round it as round
any standing body, and Legion does the same (the `grounded` overlay and the soft obstacles). In Legion, every 4 ticks
(staggered by unit id) each member still on its way looks 12 cells ahead along its planned steps: its committed detour
route, else the descent of its group's field, else the straight line to its goal. Any landed flyer whose footprint
those steps cover is asked to lift (`World::requestLegionLift`) if it is idle (no order, job, build queue, cargo,
landing or retained flight controller) and belongs to an allied player. Enemy flyers and busy flyers stay obstacles.

- **Take-off and hover:** the flyer takes off as VTOL_Move's stage 0 does (`416c50`: Activate, BeginFlight) and holds
  over the spot it left at cruise altitude through the retail flight kernel (`retailFlightNavigation` and
  `tickFlightBody`). The ground then walks under it: an airborne flyer is not in the ground word.
- **Planning:** the soft-obstacle scan stamps an idle landed flyer as kind 3. That kind is no obstacle to the fields
  and lines of an allied player's commands, so the group does not detour for flyers that will lift. It is still an
  obstacle to everyone else's.
- **Hysteresis:** every further request extends the hover by 90 ticks. So does any allied member within 6 cells of
  the flyer that is making headway (a new best on its field within 120 ticks), or that is bound for a goal there and
  moving or held for under 60 ticks. A member that is not making headway asks only for a flyer on its next 2 planned
  cells. The flyer lands only into a settled area, not in front of the stragglers of a group that is still coming in.
  When the 90 ticks run out, the retail landing mission takes over and searches from the spot it holds over: the flyer
  lands on its own spot if that is free, else on the nearest free site. It cannot lift again for 240 ticks, which
  prevents bobbing up and down.
- **Cap:** an episode lasts at most 1800 ticks (60 s), the quiet period included, unless a member is blocked by the
  flyer itself (it covers the member's next 2 planned cells, and the member is advancing or only briefly held). A
  capped episode rests 600 ticks once landed.
- **Combat:** a lifted flyer polls for a target every 8 ticks (staggered by id; `work.lift_target_polls`, bound lifted
  flyers / 8 a tick), the call the landed VTOL standby makes; a target ends the lift and it fights.
- **Own squad:** a squad's landed flyer does not lift for a member of its own squad that is making headway (it settles
  beside it); it does for a stalled one whose goal or next 2 planned cells it covers.
- **Descent:** a flyer in the landing mission's stage 3 is stamped on its touchdown footprint as a landed one, so no
  member steps in under it while it comes down; a body already there may only step out.
- **Determinism:** the lift state (`Unit::legionLift`, its spot, expiry and start tick, and the rest tick) is hashed.
  The overlays are rebuilt each tick from hashed state, and members are visited in id order.

Tests: `legion_world_test liftflyers` (40 ground bodies cross a block of 12 landed flyers, 4 columns by 3 rows with a
one-cell gap: the player's own flyers all lift once, the group arrives with no sideways detour, and every flyer lands
again on its own spot; enemy flyers stay landed and the group goes round them; flyers on guard orders are never asked
to lift; with the flyers parked on the group's destination, they lift once and land nearby after the group has
settled, with no bobbing and no overlap) and `ctest:legion_landedflyers` (a stream of ground bodies past parked flyers
of footprints 2 and 3: bounds on stuck member-ticks and arrival).

## 9. Scheduling, budgets and memory

Every piece of navigation work that can grow with the army has a deterministic per-tick budget in its own units
(`sim/navbudget.h`: `NavWork`, `NavBudget`). Work quotas are counts, never wall clock.

### The field quota

Field work runs at the start of `World::tick` under a deterministic quota of 384k relaxations per tick
(`kFieldQuota`, about 2 ms worst case), shared by plane debt, field initialisation and reach-ring cells. A field
resumes across ticks until done. One quota a tick pays relaxations, field initialisation (cells / `kInitDiv` (16),
charged when a build starts; a start whose initialisation does not fit the budget left waits for the next tick, the
first start of a tick always fits) and reach-ring cells.

* **Jobs and classes** (`serveFields`). Work is served by JOB -- a field being built, however many groups share it --
  in four classes, most urgent first: Waiting (a member stands for want of it), FirstRoute, AwareRepair, StaleRefresh.
  The last two run inside the refresh allowance, `kRefreshQuota = kFieldQuota / 4`: a refresh of a group that already
  steers by a finished field takes at most a quarter of the quota per tick, while first fields, which steer nobody
  yet, keep the whole quota; a refresh's initialisation is charged to that allowance too, and a refresh that a sharing
  group finished is installed without spending any. Within a class the jobs share the quota by deficit round robin
  over a window of the class's oldest jobs (`navDrr`: `kWindow` 12, the refresh classes `kRefreshWindow` 3; `kSlice`
  (32,768) a visit; the cursor kept across ticks, a visit the budget cut short resuming first without a new slice, so
  no job of the window is always last); the next oldest enters as one leaves. A job that has gone `kAgeStep` (150)
  ticks without work moves up a class (`navPromote`), so no class starves another for more than three steps. Round
  robin over every job at once makes every in-flight awareness refresh slow enough to be discarded by the next
  re-plan, so it is not used. A job holds its field for the whole `serveFields` call: a field an install or an eviction
  drops mid-call is retired at once but freed only when the call ends, so no pointer the rings hold is left dangling.
* **Admission.** Start requests are admitted at the head of their class, oldest first, and a new build only while the
  class's window has a free slot -- and, while more than half the byte allowance is in use, only while its kind has
  room over every class (`kFirstInflight` = 2 `kWindow` first builds, `kRefreshInflight` = 2 `kRefreshWindow`
  refreshes); beyond that a request may only take a field another group already has (`legion_sched_inflight`). One
  refused for want of room is stepped over -- at most `kWaitFitScan` (256) times a tick in the Waiting class, whose
  members stand still, `kFitScan` (8) in the others -- and after `kReserveAge` (150) ticks of refusals it reserves
  (also when its class's window is full): nothing after it takes new room, in its class or a later one, so the room
  that frees next is its (`NavAdmit`; letting smaller requests start round a waiting large one lets it be overtaken
  without end). Behind a stopped scan the remaining requests are still tried, but only for a share of a field that
  already exists (a share takes no room).
* **Eviction** (`startField`, `evictionHolders`). Live fields are limited by a byte allowance and a count
  (`kMaxFieldCount`, 1,024; chains have their own cap, section 2). A request that does not fit evicts finished fields
  past their tenure (`kFieldTenure`, 300 ticks), oldest first: a group's own field, and a shared one once every holder
  is past its tenure, dropped from all of them. A share lands only within `kStillScan` (30 ticks) of a field's start,
  so sharing extends no field's tenure by more; every finished field is therefore a victim `kFieldTenure + kStillScan`
  after it starts, which bounds how long a reservation waits. An unfinished field is never a victim. Otherwise a new
  group waits for a slot (LRU eviction thrashes, because every live group uses its field every tick).
* **Memory** (`NavBytes`, `fieldAllowance`). Deterministic bytes (element counts times fixed sizes) for every
  container, by category; the field cap is a fixed byte allowance (`kFieldBytes`, 128 MiB, never below two whole-map
  fields) over the potentials and the unfinished builds' frontiers. The process heap is measured beside it by the
  tools, never read by the sim.
* **Ordering.** Groups with no field start before stale refreshes of older groups (served the other way round,
  constant churn restarts the refreshes every tick and starves new groups). A refresh keeps building across static
  changes, as a first field does, and is installed still stale so another follows.

### Paused first builds

A first field of a group that claims no packed per-goal slots (no goal shared by two of its members), does not
pinwheel (no member's part of 16 or more) and is not a reach group builds in slices of 4096 relaxations and **pauses**
(`Field::covered`, its frontier kept) once every live member's cell is settled with a margin: `kCoverAhead` (40, eight
orthogonal steps of key) plus two diagonal steps per cell of footprint + 2 (the settle rule's ring), plus, for a
member of a formation, two orthogonal steps per cell of the formation's radius + 3 bodies (its lane runs that far off
the shortest way); a goal several members shared once also needs its packed disc settled. A member whose cell is under
its margin (it strayed, side-stepped, joined late, or shares the field with another group) adds the cell to the
field's `demand` and marks the group waited, and the waited loop resumes the build until each demand cell has its
margin (`work.paused_resumes`; `legion_world_test pausedemand`). A paused group leaves `buildingIds`. Slot groups' and
reach groups' first builds and every refresh run to done (a reach group's ring is planned from a done field); a stale
paused field is refreshed as a finished one would be. A shared paused build pauses only if every subscriber holding it
may pause (`jobPausable`); an idle paused build takes no window slot.

A* with a consistent heuristic never changes a settled potential, so what a paused build has settled is final (checked
under `TAK_LEGION_VERIFY` in `advance`; a soft view that changes between slices only raises step costs, so it cannot
lower one either). The readers that take a paused build (`Field::usable`) read it through `Field::known`, which
returns a potential only where it is settled: the settle rule (`settleWindow`, `pressed`, `areaBound` -- not cached for
a paused build -- and `rechoose`), the lift walk, the aim memo (keyed on the field's work count, so a resumed build
invalidates it), eviction, `sharedField`, and a formation's slot hand-out. The consumers that read further state their
region (section 2). Field descent (`aimCell`, the drive and detour code) may read a tentative potential beyond the
margin; descent stays sound on it, as on a half-built field. A paused build is a finished field to the blocked
re-request (read through `known()`, `freeDescent` too), and a group whose member waited on its paused build in the
last 2 ticks is active. Letting slot, pinwheel and reach groups pause too (`setPauseAllForTest`,
`legion_scenario --pause-all`) halves the longest field waits at 5,000 bodies but makes awareness ask for whole-window
refreshes that dominate the work; it is kept for tests only.

### Upkeep on demand

* **Event-driven records.** The anchors (settled arrivals, kept with the per-destination `anchorsAt` count), the
  approach-done records and the parting records are erased on the event that ends them. `World::noteOrders(id)`
  reports every change of a unit's orders -- the order helpers (`order`, `loadInto` for unit and carrier, `unloadAt`,
  `patrol`, `orderWait`, `orderWaitAttack`, `guard`, `attack`, `queueBuild`, `queueManaBuildArea`, `reclaimArea`,
  `reclaim`, `repair`, `startEmote`, through a scoped reporter so every return path reports), `dropLeg`,
  `acquireTarget`, the ground, flight and construction mission dispatchers, the unload park leg and the two death
  edges -- and `LegionNavigator::ordersChanged` applies the per-entry tests to that one unit (an anchor stays while the
  unit is alive and idle or still on its completed leg; the other records while it is alive and idle). Anchors drop
  through `dropAnchor`, so `anchorsAt` stays an exact count of live settled bodies for the settle rule. `registerMove`
  also erases the unit's parting record. A dense per-unit hint makes the event two vector reads for a unit with no
  record. `serviceYields` keeps only its yield-step loop over `yielding`. No hook sits in `findTarget`, `forEachNear` or
  the Retail mover. Writes to `Unit::orders` that bypass the helpers (morph and consume, a test's direct write) are
  caught by the backstop: `prune`'s hashed 256-a-tick cursor walks the members and the three record maps in one id
  order, so a stale record lives at most ceil((members + records) / 256) ticks (`work.anchor_walk_iters`, at most 256 a
  tick). `legion_world_test b3events` checks that an order to a settled body drops its anchor inside the order call, a
  stop keeps it, and a death or a direct order write drops it within one tick.
* **Demand-driven refresh.** A static change marks every route it reaches stale, but the scheduler starts a refresh
  (the stale-done pass) only for an **active** group: one whose member started its update Moving in the last 2 ticks,
  one with a **blocked** member, or one carrying the `demand` flag (set by the aware re-plan, cleared when the refresh
  swaps in; folded into the checksum with a tag when set). An inactive stale group stays listed and steers by its
  finished field; its refresh starts the tick it becomes active (`work.demand_resumes`), and each skipped visit counts
  `work.refresh_suppressed`. A refresh already under way keeps building (pausing it costs arrivals). Blocked is
  Retail's re-request (`kBlockedRetry` = 120 ticks): `stalledFor >= 120`, and either no finished field reaches it, or
  it stands outside its destination area with no legal step down the field clear of soft bodies (a body held only by
  its own command or by movers is not blocked: a refresh would give it the same way); each turn to blocked counts
  `work.blocked_rerequests`. `movingTick` and `blockedTick` are not hashed: they are functions of the last 2 ticks'
  hashed member updates, and no path rebuilds Legion from a snapshot. `legion_staticidle`: parked guards beside
  continuous construction do no refresh work, a pair held at a plugged gap re-plans through a gap that opens by the
  blocked rule alone, and another player's idle body plugging the one gap in a wall stays put while the group holds
  until a second gap opens.
* **Striped still-body scan.** `scanStill` samples each tick only the bodies whose `id % 30 == tick % 30`
  (`kStillScan`), with a two-sample stillness rule per body, and adds or removes each stamp cell by cell:
  `soft`/`softKind` hold the lowest covering id (overlaps kept in `softStack`), with `softCounts`, `softOwner` (and
  per-command counts for `ownsArrivals`) and `liftSoftPlayers` kept in step. `registerMove` and a landed flyer's
  take-off lift the body's whole stamp (its sample stays). `softHash` is a wrapping sum of `mix(cell, id, kind)` over
  stamped cells plus `mix(id, command)` over owned ids, so it is order-independent and maintained by add and remove;
  `softSerial` counts stamp changes. `softHashFull()` recomputes it from the stamps: under `TAK_LEGION_VERIFY` (on in
  the Legion world-test ctests) it is checked every tick, and the grid, owners, lift players and window counts once a
  scan period. The aware scan (residue 15) reads a soft view that changes every tick.

### Registration, planes and awareness

* **Registration** (`requestRegister`, `drainRegistrations`). `registerMove` costs `kRegBase` (64) units plus its
  join candidates and its goal-resolution cells; a window -- one batch of commands between two ticks, or one tick --
  may spend `kRegBudget` (4,096 plain registrations). While the budget lasts and nothing waits, a registration runs at
  once. Past it, registrations wait in a FIFO that the next `tick()` drains first; while any waits, later ones queue
  behind it; a waiting unit brakes with its order kept and reads as Waiting to the observers; a cancel runs at once and
  drops a waiting registration. A waiting unit asked to register again keeps its place, and the area it holds open
  follows its current leg. The FIFO is hashed while not empty; the budgets never outlive their window, so they are not
  state.
* **First plane builds** (`planeReady`, `buildPlanes`): section 1. The FIFO steps over entries that wait only for a
  plane (planes are per mobility class); a plane finished there takes its waiting registrations in the same tick, and
  when a plane is ready its waiters' marks clear (they wait for the budget now, so nothing bypasses them).
* **Awareness** (`awareScan`). The snapshot (movers, groups' accumulators, broad-phase grid) is taken on the scan tick;
  the groups are then judged in id order against it, `kAwareBudget` (262,144) units a tick and at least one group,
  resuming on the next tick when a scan needs more. A resumed scan judges every group at the snapshot's time: corridor
  ages and stamps use the scan tick, and a mover's displacement is scaled to `kAwareScan` ticks when its two samples
  are farther apart.
* **Still synchronous** (reported by `accounting().syncPlane*`): a stale plane's rebuild on use, the whole-plane
  relabel an incremental refresh falls back to, the incremental refresh itself (bounded by the changed rectangles), the
  awareness snapshot (one pass over the members every 30 ticks), a formation's slot ranking when its area closes, and a
  single field's initialisation (at most one whole map).

Tests: `navbudget_test` (the policy alone under continuous synthetic overload: fairness, service gaps, age promotion,
a shared job, the fit scan and reservation, shares behind a reservation; each mutated policy fails it),
`legion_sched_load` and `legion_sched_load_div` (`tools/scenarios/scale/sched-load.scn`: field requests faster than the
quota serves them for the whole run; service gaps and start-request ages within the policy's bounds, no body waiting
over 60 ticks for a field, serial == workers; the `_div` run divides the registration, plane-build and awareness
budgets by 512 under `TAK_LEGION_VERIFY`, so registrations really defer and wait for plane builds; both run with
`--no-hier-routes`, `setHierarchicalRoutesForTest`, so the squads stay on window fields that load the quota),
`legion_sched_mix` (memory pressure: the fit scan, reservations, request promotion and shares behind a stopped scan
run in the sim, and every request's wait stays within a bound derived from the allowance, the tenure and the measured
queue), `legion_sched_pin` (admission liveness under a two-map allowance: fails if shared fields are never victims),
`legion_sched_inflight` and `legion_planepin`.

### Parallel planning

Independent planning work runs on the sim's SimPool workers; movement commits stay serial. The state hash and every
observer key are those of the serial run on any worker count and in any job completion order.

* **The rule.** A parallel job's inputs and logical budget are chosen from sim state before it runs; each job reads
  immutable inputs (copies, or state nothing writes while it runs) and writes only its own slot; the serial pass
  consumes results by exact identity, in its own order, and waits for a job still running. Nothing a worker computes is
  published by the worker, no decision reads whether or when a job finished, and a slower machine waits rather than
  using a different subset. Results that are never consumed are dropped and never counted in `NavBytes`.
* **Chain tiles beside the serial pass** (`tilePrefetch`, `TileJob`, `takePrefetched`). Before a class's field jobs
  are served, the chain regions the class's window will attach next are picked in key order while their predicted
  attach work fits two DRR slices per job and the quota left. Each region's key is computed as `attachNode` does
  (`tileKey`), and a key neither the tile cache nor the staging holds becomes an asynchronous SimPool job
  (`SimPool::start`) over its own copies of the seed cells, the legality of the sector and the ring of origins round
  it, and the sector's region labels; the job and the serial `buildTile` share one body (`tileCore`). The serial pass
  then takes a prepared key's soft levels and corridors in `attachNode` when the route, the node and the soft grid
  (`softSerial`) are unchanged, charging the work the scan would; and, where `tileFor` misses its cache, takes the
  staged tile with the equal key, charging the work its build counted (`help()`ing with unstarted jobs while it waits
  for a running one). The batch is joined at the end of the pass (`SimPool::finish`); a staged tile nobody took stays
  `kStagedTicks` (3) passes, then is dropped. With no workers nothing is prepared.
* **Plane first builds in row chunks** (`computeRows`, section 1).
* **Not parallelised.** Window and corridor field relaxation (`advance`): its slices share one quota with carry-over
  between jobs, so a parallel version needs per-job budgets fixed before execution, which changes the schedule and so
  the behaviour. Incremental plane refreshes (`refreshPlane`): exact on workers, but the spike is one plane's relabel.
  Authoritative movement (`move`): serial by design (a unit observes earlier commits in the tick).
* **Instruments and test hooks.** `LegionNavigator::parallelStats()` and legion_scenario's `NAVPAR` stderr line
  (`--navpar`, and with `--scale`; observation only, and not the same on every worker count). `setParallelForTest` /
  `--par-force` (jobs inline with no workers) and `--par-perturb SEED` (debug builds also `TAK_LEGION_PAR_PERTURB`):
  jobs run in a shuffled order with random delays. Under `TAK_LEGION_VERIFY` every reused key is recomputed and every
  staged tile rebuilt from the plane. ctest `legion_parallel` (`tools/legion_parallel_test.py`) compares 1, 3 and 7
  workers, perturbed runs and a forced inline verify run with `--serial`; `simpool_test` covers the asynchronous API.

### Hot-path tables

A flat id-indexed member table pointing into the ordered member map (which still drives ordered iteration) and a cached
point pointer per member keep the move update off ordered-map lookups. The verify references
`legion_verify_{battle-assault,crosslong,gap8}` run `TAK_LEGION_VERIFY` over the join index, the awareness broad phase,
the vertex probe switch and the gate memo; each file is one that a deliberate mutation of the matching optimisation
trips.

## 10. Determinism

All decisions use integers: 16.16 positions, BAM headings, integer potentials, squared distances and `isqrt64`. Work
quotas are counts, never wall clock. Containers that drive decisions are ordered (`std::map`, `std::set`, sorted
vectors with id tie-breaks); group state is in `std::map`, so iteration order is deterministic. Everything persistent
is folded into `LegionNavigator::checksum`: groups (seeds, bounding box, region cell, sharing and peak counts, field
and refresh progress, route plans and tile caches, slots), points (formation transform, limit, claimed cells,
entrances, meets), rings (bands, spots, the late, blind, thick and deep flags), anchors, yields, swaps, plane debt, the
prune cursor, the registration FIFO, the opening tokens and every member's state, timers, detours, routes, pass lane,
ranks, gate and corner points, claims and approach fields. The only unhashed inputs are values derived again from World
each tick (structure signature, plane labels, opening holders and occupant counts, `nextOpening`'s cache), the
goal-resolution cache, which is a pure function of the plane, and observation-only counters (`Stats`, the `why` tag).
`World::stateHash` mixes that checksum, the orders' area keys that define groups and the open arrival areas, only in
Legion mode. Movement and yields run serially; parallel planning (section 9) is exact. The `legion_determinism` test
and the `navigation_determinism` goldens (`tools/navigation_checkpoints.json`) check that runs are repeatable and that
serial and `--workers` runs produce the same hash. `sweep` uses `__int128`, which GCC, Clang and MinGW all provide.
Retail hashes and goldens are unchanged by Legion: Legion state enters `World::stateHash` only in mode 4.

## Tests

* `legion_world_test`, synthetic worlds, among them: clearance, groupreuse, jagged, trapped, crowdhold, replace,
  unreachable, quota, pens, determinism, formation, slotblock, deaths, deathsshared, splitgoal, farclick, churn,
  approachhold, approachopen, churnfield, approachchurn, legacyyield, planeincremental, planeprebuild, penstale,
  lattice, wallend, staticblock, squadformation, pinwheel, landedflyers, mixedformation, flyerfront, liftflyers, aware,
  lonetrees, slideback, stillturn, forestedge, pausedemand, b3events, mixedrandom, formationreform; boats and
  hovercraft: navalclearance, navalisland, hovershore, navalmissions (see "Scope"). Their metrics come from the shared
  observer (see "Instruments"). `legion_still_test` tests the headway window alone (section 3), and
  `legion_flyers_test` the flight stations and the rejoin (section 8).
* `legion_acceptance_test`, five checks run in Legion and Retail; Legion asserts, Retail reports. singleunit, jagged,
  trapped and group pass for Legion; crowdheld is a ratchet (Known weaknesses).
* `navigation_determinism` and `legion_determinism`.
* `convoy_test`, `selection_order_test`, `server_orders_test`, `server_orders_network`, `issue_selection_test`
  (section 2).
* `legion_movement_orders` (`movement_orders_test --legion`): attack approach around terrain, chase of a moving
  target, guard follow, group patrol, fight-move and patrol laps, combat resume, partial routes, repair and build
  approaches around a water wall; each case also checks that Legion actually served the mission
  (`LegionNavigator::mission`).
* `legion_production` (`production_test --legion`), `legion_transport` (`transport_test --legion`: boarding, ground
  unload approach, PARK of the released passenger), `legion_movement_orders_trolls` (100 and 256 Trolls on open
  ground), `legion_movement_orders_trolls_maze` (100 Trolls through a generated maze to a rally point: all settle,
  serial == workers) and `clear_build` (Legion mode: reclaim then build) assert that Legion served the leg.
* `navbudget_test`, `legion_route_test`, `legion_hier_test`, `legion_routebins_test`, `simpool_test`: the scheduler,
  route, hierarchy, route-index and worker pieces alone.
* The scenario ctests (`legion_scenario` over `tools/scenarios/`, gated by `tools/scenarios/baseline.json`), each
  named in the section of its mechanism, and `ctest -L legion` for all of them. `legion_cost` gates the growth of
  Legion work from N to 4N bodies (`tools/legion_cost_baseline.json`). `legion_doc_links` and
  `legion_doc_links_selftest` keep this document tied to the instruments (below).

## Instruments

What each tool sees, and what it cannot. A number is only as good as the instrument that produced it.

| Instrument | Sees | Blind to |
|---|---|---|
| `legion_world_test` | arrival, spins, cell oscillation, legality, hold and detour counters on synthetic 2x2 movers (instant acceleration, flat 16-px cells, feature walls); per-case metrics from the shared observer | real turn rates, combat, timing, multi-group selections |
| `legion_acceptance_test` | single unit, group, jagged, trapped, crowd-held, in Legion (asserted) and Retail (reported) | anything outside its five fixtures |
| crowdbench (`crowdbench`, `crowdbench_matrix`) | 18 scenarios x two modes x several populations, deterministic keys only, Retail on the same binary | formation play: its orders are per-unit goals, which form no formation |
| `.scn` scenarios and `legion_scenario` (`tools/scenarios/`) | the client's real order path (`issueSelection`: the selection order, or per-unit commands with their offsets for non-shared units, SetSquad), serial and `--workers`, the gate's start offsets, the observer's keys; on stderr, per run, the wall time of the whole run and of `World::tick` per tick (`tick_max_ms=M@T`, the slowest tick and which it was, and `tick_p99_ms`; the observer's sampling is outside them, and none of it reaches the JSON) | the AI, fog and economy; production; anything not in the file |
| `tools/legion_observe.h` | progress, held-by-design vs no-progress age classes, the stall clock against the headway clock (`stall_stale_ticks`, Legion only), lane crossings and files, contact and clearance, flyer metrics, the `why` attribution (below), per-tick work counters (`work.legion_total`, and `work.legion_all`, which adds the awareness, lane and pinwheel work, `aware_work`, `lane_work` and `pivot_work` less its one unit per line sweep; report-only); a command is one arrival area: `legion_scenario` runs the file's clicks through a `ConvoyTable` of its own, so selections clicked to one point a few ticks apart are one command for `contact_settled` and share one arrival disc sized for all of their bodies, in both modes | wall time (never read); it is const and adds nothing to hashed state (`observer_neutral`) |
| `tools/scenarios/baseline.json`, `--check` | the committed base: exact keys, one-sided bands, work counters, the Retail floor, exceptions with their clusters | nothing it was not asked to key; a new key needs a new base |
| `legion_cost` | work per member-tick W(4N)/W(N) and slot-assignment work per slot | wall time |
| situations (`tools/scenarios/situations/`, `scn_truth`) | a moment cut out of a recording: exact bodies, the orders in flight, the next 600 ticks of commands, and the recording's own positions 300 ticks in | what is not in the snapshot: economy, scripts, AI, production, units in transports; the first 100 ticks follow the recording but by 300 none does, because the Legion group and mission state of bodies already in flight is not in the file; so they gate nothing (README in that directory) |
| replay checkpoints | the state hash and `posDigest` at every checkpoint; the verifier reports "state diverged at tick T1; positions/hp/orders match until tick T2" | a replay plays back exactly only on the build that recorded it |
| `TAK_PACELOG`, `tools/pace_check.sh` | ticks published per rendered frame, sim inbox depth, the 512-tick fast-forward, bursts; the presentation clock: displayed time per frame, its CV and rate CV, freeze frames, snaps, the lag target, at a fake vsync (`TAK_VSYNC_HZ`, `--vsync`) | the real renderer and GPU: a headless run draws nothing (see `TAK_FRAME_MS` / `TAK_VSYNC_HZ`); `TAK_QUIT_TICK` ends an interactive run for a real-display log; report-only |
| `tools/legion_timing.sh` | per-tick mean and p99 on an idle pinned core, A/B interleaved, N of 4 or more | never a gate: wall time is reported, not asserted |

**The `why` attribution** (observation only). Each member carries an unhashed `why` byte (`LegionNavigator::Why`,
`steerWhy`, `Member::why`, `whyName`; 32 values, `none` among them): the mechanism of its last update, finer than
`steerSite` -- the walk it was on (`steer`, `direct`, `pass`, `flow_first`, `flow_brisk`, `flow_cap`, `sidestep`,
`room`, `corner`, `route_brisk`, `route_cap`, `press`, `yield`, `nudge`) or why it stood (`hold_movers`, `hold_rest`,
`hold_settled`, `hold_admission`, `hold_detour`, `hold_ring`, `hold_livelock`, `hold_still`, ..., `wait`, `trapped`).
The scenario observer attributes its statue ticks, sideways and backward steps, cell reversals and stopped samples to
it (`why.<name>.ticks`, `.moved_ticks`, `.samples`, `.stopped`, `.statue`, `.sideways`, `.backward`, `.reversals`),
report-only and gated by nothing. Never hashed, never read by a decision. The press's nudges count as statue,
sideways and backward steps in the observer (a few pixels at a time), which those keys cannot tell from a creep;
subtract `why.nudge.*` to read the creeps alone.

**The terrain-stuck verdict** (`tools/crowdbench_matrix.h` / `crowdbench_acceptance.h`, all modes, observation only).
A unit that would be terrain-stuck (no progress, free static path, no body on its static line, touching terrain) is
crowd-held when it is queued behind a moving leader: a same-player mobile member whose centre is within two body
widths, lies within 60 deg of the unit's goal or static travel direction, and made at least 16 px of progress over the
same 90-tick window. A unit against a wall with nobody moving ahead of it is still terrain-stuck. The observer gives a
unit one progress window after it stops being trapped (as after a command).

Rules that come out of using them:

* **Retail and determinism checks are exact; a Legion hash is exact only for the steps declared hash-identical;
  outcome keys and work counters are one-sided bands against the current head's base; wall time is always reported
  and never gated.**
* **A replay is not a regression medium.** It runs only on the build that recorded it. After a behaviour change it
  diverges at the first changed decision and the commands then address units that were never built. The situations
  exist so a later build can still run the moment.
* **A sweep is not a measurement until the guards pass**: `ctest -L legion`, the acceptance worlds and the lane-order
  keys. A change judged on arrival alone can break lane order, arrival order or the rings, which only those see; and
  any speed change for plain movers is judged on `ctest:legion_acceptance_group_legion` first.
* **A weakness without a scenario, key or test behind it is not a known weakness.** `tools/legion_doc_links.py` (ctest
  `legion_doc_links`) fails when a cited scenario, key or test does not exist, or when the field quota quoted here
  differs from `kFieldQuota`.

## Known weaknesses

* **The detour rules read a record across two measures.** The detour back-off restarts, and the yield-first rule looks
  for a gain since the last detour, on `Member::nearest`: the least distance still to go since the order began, in
  squared cells or potential x64, whichever the body read (section 4). A body that registered before its field sets no
  record until it is near its goal, so in a melee or a long jam its back-off grows to the 480-update wait. Restarting it
  on the stall clock, after a gain of 2 or 4 cells, or halving the count on a gain all trade detours and reversals
  against standing samples in `battle-assault`, so none is taken without a retune judged on its own. Measured by
  `battle-assault:work.detours.total` and `battle-assault:stopped_permille`.
* **Packed distinct goals: the crowdheld acceptance check is a ratchet, not a pass-all.**
  `legion_acceptance_crowdheld_legion` reaches 62 of 64 in goal and passes at 61 or more with 1 unit ever
  terrain-stuck (spin passes at 0). The remaining unit is sealed out: the door lets a shallower unit of the same goal
  row through first, and once it settles the deeper goal is reachable only through neighbouring goals. The goals form
  an 8x8 lattice of 2x2 bodies at a 3-cell pitch, so the gaps between goals are one cell, narrower than a body. A goal
  can only be entered along its own row or column from one face of the lattice. Units come off the detour or out of
  the door in a single file in random order of depth, and the late ones find all four of their lanes sealed by settled
  bodies. A one-cell yield cannot clear a 2-cell footprint centred on the blocker's own goal. Shifting goals into
  one-body aisles reaches 64/64 but adds spin and aisle deadlocks and regresses rapid replacement and doors; having
  settled bodies step aside peaks at 63/64 and is unstable. A real fix needs parking outside every lane of the deeper
  unsettled goals and a way for deep owners to pass parked bodies, or an entry face chosen per late unit from the lanes
  still free. The in-game group right-click sends one shared point, so it uses formation slots instead and does not
  produce this layout; `legion_acceptance_group_legion` passes 64/64. Measured by
  `ctest:legion_acceptance_crowdheld_legion` (the 61/64 ratchet) and `ctest:legion_crowdhold`.
* **Lane order, walls and tails.** Legion takes gaps several abreast and ranks formations round turns (sections 5
  and 6), but some lane swaps remain where Retail's slower files keep their order: the 448-body corner stress fixture,
  whose points are too big for the ribbon, swaps more bodies across its top gate than Retail, and a 20-cell gap, wider
  than a 48-body formation's band, swaps about 1.7 bodies per file (`gap20:gate.top.crossings`). An attack ring
  against a wall and the bodies queued in a door touch the wall more often than Retail. A Move group crossing its own
  firing ring arrives about level with Retail; one motion-cross body does not finish from some start offsets; a lifted
  flyer detours 5 cells against 2. The pinwheel misses most turns at a wall end met across it (the ribbon covers move
  legs of parts of 32 or more). Measured by `corner-1x448:gate.top.crossings`, `corner-8x56:gate.top.crossings`,
  `strait-2x150:gate.strait.crossings`, `attackring-wall-64:wall_touch_permille`, `doorplug-112:wall_touch_permille`,
  `ringcross:g.M.t90`, `doorplug-124:g.A.done`, `motion-cross:click.u01.done` and `ctest:legion_liftflyers`.
* **Detours cost travel reversals, and queues of own movers stand.** Every detour route is walked at travel speed and
  the body turns to its motion, so a body that takes a detour and then rejoins its field turns its travel direction
  round more often than one that waits (still far under Retail's reversals): a route reverses 1 to 6 updates after it
  starts, backing out of the pocket among settled bodies the body walked into. Most of what still stands is queues of
  the body's own movers (`hold_movers` and its `hold_rest`): the same queue Retail has, and Retail stands in it as
  well; the press closes the gaps in it but does not shorten it. A crowd still packs into the wedge before a corner and
  passes the corner's head in few files (room at a merge turns the single file into lanes). Nothing spreads a crowd
  across the free width beside an obstacle except the special cases; a density-aware descent (a soft charge for cells
  covered by the group's own moving members) is the structural option, and its risk is a refresh that keeps moving the
  file. A stream reaches parked flyers before they have lifted and stands against them while finishing sooner
  (`ctest:legion_landedflyers` bounds), and one ordinary side-step shuffle runs out and back once among the lone trees
  (`ctest:legion_lonetrees` allows that one event). Measured by `densehead:g.A.t90`, `densehead:stopped_permille`,
  `densehead:reversals`, `densehead:gate.mid.crossings`, `pocket-304-wall:g.A.t90`, `motion-wall:click.u00.t90`,
  `taperbridge-100:statue_ticks`, `taperbridge-100:backward`, `ctest:legion_departure` and `ctest:legion_cornermerge`.
* **A narrow passage's waiting crowd stands on the other way's exit apron.** A way stays open for two of its
  crossings, so long passages turn once or twice and neither `bridges-100` nor `taperbridge-100` gridlocks at any
  start offset measured. The crowd still waits on the apron, though, and the other way's stream threads through it; a
  passage whose two ways keep arriving turns more often and is still exposed. A staging rule that keeps the exit lane
  clear (holding bodies before they enter the lane rather than moving them out of it) is the next thing to try.
  Measured by `ctest:legion_passage_admission`, `ctest:legion_passage_taper` and `ctest:legion_passage_green`.
* **Two opposing streams in a one-body lane are bistable.** Where two streams of one side meet head on and neither has
  a way round, the meet either threads or gridlocks, depending on the start offset. Allies swap places (section 4) and
  `bridges-1000` arrives with none stuck at every offset measured, but the margin is thin: `kRoomHeld` 15 or 40, or a
  detour back-off starting at 45, put an offset back into gridlock. The swap covers allied bodies of one footprint on
  move legs only; enemies, mixed footprints, routed bodies and mission legs still meet with the hold's ways out.
  Measured by `ctest:legion_swap` and `ctest:legion_choke`.
* **Attack rings against Retail.** Round a box walled on several sides Retail packs sub-cell points of the corners'
  origins and jostles bodies in and out, so a few more of its bodies stand in reach at the end; a ring across a river
  stands fewer in reach than Retail's ever-in-reach count; statue ticks stay above Retail's (presses toward reach keep
  the facing cap) and wall touch is above Retail's by a box's walls and in a gatehouse. Bodies held behind an arc do
  not creep round it to the far side's free spots. On the fight rings statue ticks and reversals run past Retail's
  (more bodies press the last pixel). `fight-retarget`'s stragglers settle at the packed disc's edge more often than
  Retail's, and the battles reverse more. Against a target walled in on every side Legion outfires Retail (it seeks
  flush points on every face that has one). Measured by `ctest:legion_fightstill`, `ctest:legion_fightshuffle`,
  `ctest:legion_guardstill`, `ctest:legion_ringroutespeed`, `ctest:legion_ringshuffle`, `ctest:legion_ringupkeep`,
  `ctest:legion_attackwalled`, `ctest:legion_attackflush`, `ctest:legion_ringcross`, `ctest:legion_ringfill`,
  `ctest:legion_ringclosed`, `ctest:legion_ringstill`, `ctest:legion_ringwait`, `ctest:legion_ringshadow`,
  `ctest:legion_ringbend`, `ctest:legion_ringblind`, `ctest:legion_ringpocket`, `ctest:legion_ringroute`,
  `ctest:legion_ringnosight`, `ctest:legion_fightring`, `ctest:legion_ringedge`, `ctest:legion_ringshadowpress`,
  `ctest:legion_ringshadowmixed`, `ctest:legion_meleepress`, `ctest:legion_gatewalkon`, `ctest:legion_gateyard`,
  `ctest:legion_walkononward`, `ctest:legion_ringdeep`, `ctest:legion_ringdeepchurn` and `ringcross:g.M.t90`.
* **The livelock guard costs some lane order.** A body blocked again after a side-step toward its arc may not step
  straight back, so it takes another field step inward or holds and side-steps: the bodies round `island-1x150`'s rock
  swap files more often in the strip and abreast windows, and gap, wall and corner fixtures swap a few more too.
  Walk-in-place is not gone in dense combat crowds. Measured by `island-1x150:gate.abreast.crossings`,
  `island-1x150:gate.lane.crossings`, `gap10:gate.top.crossings` and `ctest:legion_slideback`.
* **Approaching an unreachable goal counts as trapped movement.** The acceptance and matrix observers classify a unit
  as Trapped whenever its origin cannot statically reach its goal disc, and any move or turn more than 150 ticks after
  that counts in `trapped_units_moving_after_grace`. Walking to the nearest reachable point therefore counts for every
  unit in the unreachable scenario, and in recovery and recovery-passive before the wall clears: Legion scores
  200/2000 there, as Retail does. Units already at their point have zero spin and do not move, and their orders drop
  there: in recovery-passive (the wall removed at tick 2000, the original orders kept) Legion reaches the goal with 0
  units, as Retail does. Pockets under 256 origins hold in place; a region just above that threshold walks to its
  edge. Measured by `ctest:legion_unreachable` and `ctest:legion_acceptance_trapped_legion`.
* **Per-unit steering is the CPU cost at large populations.** Field work is not the cost; counters point to per-unit
  steering: line sweeps (640 cells for formation members, rerun on every origin change), descent, held rechecks and
  the per-held-member slot re-choice BFS. The 640-cell sweep and the 49x49 re-choice BFS are bounded per member, not
  globally. Those counters are `cost-open:work.line_sweeps.total`, `cost-open:work.held_rechecks.total`,
  `cost-corner:work.slot_search_cells.total`, `split-450-open:work.trace_cells.max` and
  `split-450-open:work.rechoice_bfs_cells.max`; `ctest:legion_cost` gates the growth of that work from N to 4N
  bodies. At 10,000 bodies the remaining bottlenecks are crowds at corners and narrow mouths, the queue for many
  unrelated destinations, and serial movement as the tick (`ctest:legion_traffic`).
* **Opposing columns and exploration.** Opposing columns at 2000x1, and 500x1 and 250x8 at 50% moving, leave bodies
  uncrossed (in the latter, idle bodies stand between movers and their goals: idle-obstacle flow, not passing).
  Exploration at 2000 units ends with 0 arrivals in every mode. The opposing-columns rows live in the crowdbench screen
  (`ctest:crowdbench_matrix`); the nearest `.scn` measure is `densehead:stopped_permille`.
* **Open items.**
  * Under a static change every tick a refresh finishes about every 85 ticks
    (`refreshchurn:work.refresh_completed.total`; the control without churn, `refreshchurn-control`, completes none).
  * A first field built across epoch changes mixes old and new plane legality. This is safe, because the mover
    re-proves every step, but it can misdirect until the clean rebuild. `refreshchurn:spins` shows no spin while it
    happens.
  * A member queued behind its own crowd settles where it stands, out to twice the crowd's reach, so it can stand
    outside the benchmark's authored radius: its order completes, but it does not count as arrived_settled
    (`corner-8x56:g.A.complete_outside_radius`).
  * Point cell claims do not follow a body that yielded (`corner-4x50:stopped_permille`).
  * `battle-field-2x60:spins` and `ctest:crowdbench_matrix` watch the rules that have no dedicated test.
* **A 2-cell dead-end corridor's front can settle short of its room.** The mouth of a corridor one body wide does not
  jam (funnel claims, section 6), but the 40-body room at the end of one still leaves orders open at some start
  offsets: the first bodies through the corridor stop for a window a few cells before the room, settle where they
  stand (their potential is within the area's bound), and the queue settles back from them, while the room's side by
  the door stays empty; the bodies past the explicit area's ceiling then wait. In the 120-body fixtures at offsets
  +28..+30 the column's last row, spawned two cells from the map's edge, never leaves Waiting (a fixture artefact).
  Measured by `deadend-w2-n40-room-plain:g.A.complete_n` over wide offsets and `ctest:legion_funnelmouth`.
* **Group partitioning depends on registration order** and on field start timing (a started field takes no new
  seeds). The 256-goal cap chunks goals in registration order, so an order whose unit ids are not spatially coherent
  can get spatially interleaved groups (`split-450-open:g.A.arrived`, and the 8-group `corner-8x56:g.A.arrived`).
* **One-way admission costs small groups on wide-enough passages.** A narrow passage (16 cells or less) is admitted
  one direction at a time, also where two lanes each way would fit (a 12-cell bridge), because the one-way crossing
  beats the two-way one on the bridge fixtures. Groups of fewer than 8 wait their turn too, where a few walkers could
  get through head on. The other way can wait up to two crossings plus one. Measured by
  `ctest:legion_passage_admission` (arrivals and stuck bodies; the time is in its run's JSON),
  `ctest:legion_passage_small` and `ctest:legion_choke` (bridges-1000).
* **Idle enemy units never step aside -- as in Retail.** Settled friendly Legion arrivals yield and idle friendly
  bodies part a lane (own player and allies; the funnel give-way asks only its own group); an enemy's idle body is a
  still body to route around. Retail moves no idle body at all, its own or another player's: a blocked mover only
  clamps, slows and presses on, parked bodies are walls in its cell ratings, and BUGGER_OFF is stored and never acted
  on (see [retail-engine.md](retail-engine.md)). Moving enemy bodies would be a gameplay change with no Retail
  behaviour to port. Watched by `ctest:legion_staticidle` (its part 3: another player's idle body plugging the one gap
  in a wall stays put, and the group holds until a second gap opens).
* **Fog**: Legion plans on the static plane as Retail's mover sees it. It does not model unexplored terrain
  separately. The one place fog shows is group awareness, which sees an enemy mover only within sight
  (`aware-unseen:pair.contacts.permille_x100` against `aware-seen:pair.contacts.permille_x100`).

## Network, lobby and checksum

* Network protocol: the mode travels as the pathfinding-mode byte, value 4 (`PathfindingMode::Legion`); the bytes 1-3
  name no mode and are invalid. Explicit selection orders (section 2) travel only in Legion games.
* Lobby: the pathfinding button switches between Retail and Legion. Campaign missions always run Retail movement,
  whatever the lobby says.
* Retail hashes and goldens are unchanged by Legion. Legion state enters `World::stateHash` only in mode 4.
* Headless runs: the DEBUG client flag `TAK_LEGION=1` selects Legion for the `--mpai` harness and other headless
  games (release builds read no `TAK_*` variables).
