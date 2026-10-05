# Legion pathfinding (mode 4)

Legion is a fifth ground-navigation mode built for how Total Annihilation:
Kingdoms actually moves armies: a flat tile mosaic whose relief is baked into
16 px placement cells, square-ish footprints (shipped ground movers are 1x1 to
4x4, mostly 2x2 and 3x3), slope and water limits per unit type, features,
walls and buildings as blockers, and right-click orders that send dozens to
thousands of units at once. Code: `src/sim/legion.{h,cpp}`; hooks in
`src/sim/sim.cpp` next to the Retail+/Flowfield/Cooperative dispatch.

## Goals (from the request)

1. Single units and armies route around obstacles, and plan as a group.
2. No unit gets stuck against terrain, including jagged terrain, unless it is
   actually trapped.
3. A unit that is stuck stops. It never spins or rocks back and forth. The same
   holds inside large crowds.

## Design

### 1. One legality predicate: the footprint-origin plane

Legion works on the same coordinate the mover's legality test uses: the
footprint **origin** (`footprintOrigin(x, footX)`). For each mobility class
(footprint size, slope/water limits, domain) it builds a static plane:
`legal[origin]` is true if `World::mobilePlacement` would accept the footprint
there with no mobile bodies present.

* Each cell's legality is computed by calling `retailMobilePlacement` itself
  on a 1x1 footprint, with the same cell reader as `World::mobilePlacement`
  but with the entity slot empty. The retail predicate is a per-cell loop plus
  a bounds test, so a footprint is legal exactly when every covered cell is.
  Two integer run-length passes (rightward runs of at least `footX`, then
  `footZ` consecutive such rows) give the footprint plane. This is the
  "clearance transform", and it is exact for rectangles.
* Structures are static bodies. They are stamped with their yard maps, and a
  closable yard is treated as closed. This is conservative: the planner may
  avoid a cell the mover would accept, but never the reverse.
* Maps without a placement plane (legacy test worlds) use
  `NavGrid::fits`, the predicate the legacy mover uses. Only square
  footprints are supported there.
* The plane is rebuilt when `World::placementEpoch_` changes or the structure
  layout signature changes. `placementEpoch_` is bumped by
  `setMapPlacementFeatures`, `place/removeMapFeature`, `blockCells` and
  `setTerrain`.

The `legion_clearance` test checks that the plane matches `mobilePlacement`
at every origin, with slopes, water and jagged walls, for footprints 1x1 to
4x4 and one 3x2 footprint.

Moves on the plane are 8-connected. A diagonal step requires both
orthogonal neighbours to be legal, so there is no corner cutting.

### 2. Group planning: one integer field per group and footprint class

Every plain ground Move leg is registered with Legion. A **group** is the set
of legs with the same player, issue tick and mobility plane whose goal origins
are linked within 16 cells. A right-click on a selection is one group; so is
each leg of a queued group command. A single unit is a group of one.

Each group gets one **integer distance field**, seeded at every member's goal
origin. A goal on an illegal cell is replaced by the nearest legal origin
within 24 cells. The field is computed with Dial's bucket queue using step
costs of 5 (orthogonal) and 7 (diagonal) over the static plane, with no
corner cutting. All members share it: a 2,000-unit order costs one field per
footprint class present, not 2,000 searches.

Field work runs at the start of `World::tick` under a deterministic quota of
4M relaxations per tick, shared by all fields in group-id order. A field
resumes across ticks until done. At most 48 fields and 24 planes are live.
A field that has served its group for 300 ticks can be displaced, oldest
build first. Otherwise a new group waits for a slot; LRU eviction was tried
first and thrashed. Group state is in `std::map`, so iteration order is
deterministic.

### 3. Steering: descend the field, slide along walls

Each update a supported unit:

* goes **straight to its own goal** if the footprint sweep from its current
  position to the goal is legal. The sweep is an exact supercover in origin
  space using 16.16 integers, and an exact corner crossing must clear both side
  cells. Reach is 160 cells, and the line is rechecked only when the origin
  cell changes. A single unit on open ground therefore moves in a straight
  line, and a formation keeps its shape.
* otherwise **descends the field**: it takes the legal neighbour with the
  steepest descent per unit of path length (an orthogonal drop of 5 equals
  a diagonal drop of 7). Ties go to the neighbour nearest the unit's own
  goal, then a fixed direction order. A group field is "distance to the
  nearest goal of anyone", and plain first-found ties pulled bodies toward
  other members' goals, folding a formation into a file
  (`legion_formation` guards this). The unit then string-pulls up to three
  further descent cells that remain in a legal straight line, and aims there.
  Each aim point is on a segment that has already been proven legal, so the
  unit never aims into a wall. At a jagged edge the steepest legal descent
  runs along the wall, so the unit slides instead of pressing into it.
* **moves along the proven direction exactly.** The heading turns toward that
  direction at the authored rate, and speed is capped while the body still
  faces away from it (full speed within 22.5°, half within 67.5°, an eighth
  otherwise). Because turning lag never moves the footprint off the proven
  line, the units cannot orbit a waypoint, overshoot into terrain or rock on a
  step.

The step is committed with `commitGroundStep(..., strictDiagonal=true)`, the
authority Retail uses. Before committing, Legion checks the destination origin,
and both orthogonal origins on a diagonal, against occupancy and
`mobilePlacement`.

### 4. Stuck classification

Progress is the integer distance still to go: field potential, or squared
cells to the own goal while on a straight line.

* **Terrain-trapped**: the unit's origin has no finite potential once its
  group's field is complete, or its goal has no legal origin nearby. No legal
  route exists for this footprint. The unit stops on the same update and then
  never moves, turns or probes. It keeps its order: a new static epoch (a gate
  opening, a wall destroyed, a building removed) re-plans it, with no
  polling. If nothing opens within 5 minutes (9000 ticks), the leg is retired
  with `dropLeg`, as Retail does for an unreachable goal, and queued legs
  continue. `legion_trapped` checks both paths: a sealed pocket that stays
  sealed, where the unit never moves and its order retires at about 9000
  ticks, and the same pocket opened at tick 300, where the unit resumes and
  arrives.
* **Crowd-blocked**: a route exists but bodies occupy it. The unit first
  **flows around** the blockage by stepping to any free legal neighbour that
  strictly reduces its distance still to go. The step is committed until the
  new cell is entered. It moves only along the axis whose origin changes,
  never back toward a cell centre, and it does not turn the body; this is a
  shuffle. Speed still follows the facing cap. A flat half-speed shuffle was
  measured to gridlock opposing columns. If no such neighbour exists the unit **holds**:
  speed zero, heading constant (the heading only turns on a committed step),
  no creeping. A held unit rechecks its candidate cells against occupancy
  every update. This is change-driven, since nothing moves until a cell frees,
  and it never triggers a re-plan, so a long queue at a choke costs no search
  work.
* **Opposing traffic**: after 6 held updates against a moving body facing the
  other way, the unit commits to one lateral step to its own right (keep
  right). Both units do the same, so head-on pairs pass instead of pushing.
  The step runs to completion or times out after 45 ticks. The body turns
  with this step: when side-steps did not turn the body, opposing columns
  gridlocked.
* **Walled in by still bodies** (settled arrivals, a held queue, idle units of
  any player): after 8 held updates, and then every 30, the unit runs a
  bounded BFS (25x25 window) over legal origins whose footprint touches no
  still body. The target is its own goal if it is inside the window,
  otherwise the reachable cell that most reduces its distance to go. The
  resulting route is committed and walked cell by cell without turning the
  body. It is attempted only when the blocking body will not move by itself
  (idle, arrived, or not a Legion mover), never for a queue of members
  waiting on each other. Attempts back off geometrically (30, 60, ... 480
  updates) while no progress is made. Moving bodies are waited for, never
  planned around.
* **Heading**: the body turns only on a committed step, with a 5.6° dead
  band. A refused step, a hold, a shuffle, a detour route and a trapped
  stop never turn it.
* **Arrival on contact**: once a unit has made no progress for 20 updates, it
  arrives if it is inside its goal's area and touching a settled same-player
  body. With a distinct goal the area is one body width. When several members
  share one point, the area is the packed disc that many bodies occupy, and
  held members of the same shared point also count as its filled part. The
  destination is an area, so a blocked unit never arrives outside it.

### 5. Arrival slots for a shared point

When several members of a group share one goal point, Legion computes packed,
non-overlapping footprint slots around it, in order of field potential. A
member claims a slot when it comes within about 12 bodies of the slot ring.
It takes the free slot **farthest along its own approach direction** (ties go
to the slot nearest the approach axis), so the area fills back to front and
later arrivals find the cells between them and their slot empty. A member
walled off from its slot re-claims the nearest free slot every 20 held
updates.

### 6. Determinism

All decisions use integers: 16.16 positions, BAM headings, integer potentials,
squared distances and `isqrt64`. Work quotas are counts, never wall clock.
Containers that drive decisions are ordered (`std::map`, sorted vectors).
Everything persistent (groups, seeds, field progress counters, slot claims,
per-unit state, detours and routes) is folded into `LegionNavigator::checksum`.
`World::stateHash` mixes that checksum, and the orders' issue ticks that
define groups, only in Legion mode. Movement runs in the serial unit loop. The
`legion_determinism` test checks that a run is repeatable and that serial and
`--workers` runs produce the same hash.

## Scope

Supported: ground units (`Domain::Ground`, footprint 1..8, plus non-square
footprints when a placement plane is present) on **plain Move legs**,
including group right-clicks and queued Move legs. A leg is supported only if
it and every leg ahead of it are plain moves.

Delegated unchanged to Retail (the native search service and retry ladder
also run in Legion mode): attack, fight-move, patrol, guard, build, repair,
reclaim, load/unload and transports, production exits, parking, flying units,
boats and hovercraft. Retail and Retail+ behaviour is bit-identical: per-tick
traces compared against the checkpoint binary on 10 scenarios each.

Mission integration: arrival sets the native mission's `0x500` event, as
Retail does, so `retailGroundMove` retires the leg. Legion masks the
search-failure events (`0x2600`) that make Retail enlarge the goal circle. A
blocked Legion army therefore never "arrives" because the circle grew.

## Known weaknesses

* Distinct per-unit goals packed tighter than one body width apart (the
  benchmark's 3-cell lattice for 2x2 bodies leaves 1-cell gaps) can only fill
  in approach order. A unit whose goal is behind already-settled bodies relies
  on the local detour, which cannot open a gap of less than one body. Legion
  does not reassign goals, because the order names a point per unit. This is
  why the `group`, `trapped` and `crowdheld` acceptance checks still miss full
  arrival. The in-game group right-click sends one shared point in Legion
  mode, so the client path uses packed slots instead.
* Units that are idle (no orders) never step aside for a Legion mover. They
  are treated as still bodies to route around.
* Fog: Legion plans on the static plane as Retail's mover sees it. It does not
  model unexplored terrain separately.
