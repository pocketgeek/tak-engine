# Legion pathfinding (mode 4)

Legion is a fifth ground-navigation mode built for how Total Annihilation:
Kingdoms actually moves armies: a flat tile mosaic whose relief is baked into
16 px placement cells, square-ish footprints (shipped ground movers are 1x1 to
4x4, mostly 2x2 and 3x3), slope and water limits per unit type, features,
walls and buildings as blockers, and right-click orders that send dozens to
thousands of units at once. Code: `src/sim/legion.{h,cpp}`; hooks in
`src/sim/sim.cpp` next to the Retail+/Flowfield/Cooperative dispatch, and the
group right-click in `src/client/gameview_hud.cpp`.

This document describes Legion as of 4edb009. The five-mode comparison
(Retail, Retail+, Flowfield, Cooperative, Legion) and its tables are in
`docs/navigation-comparison-2026-10-05.md`; they are not repeated here.

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
* Structures are static bodies, stamped with their yard maps by the mover's
  own rule: a `.` yard cell is open, and a closable `c`/`C` cell is open while
  the unit script holds the yard open (`yardOpen`), as in
  `World::mobilePlacement`. The yard state is folded into the structure
  signature, so opening or closing a yard rebuilds the plane.
* Maps without a placement plane (legacy test worlds) use
  `NavGrid::fits`, the predicate the legacy mover uses. Only square
  footprints are supported there.
* Each plane also labels its **static components**: a flood fill in cell
  order under the same step rule the field and the mover use. Each component
  records its origin count and bounding box. Labels are renumbered on every
  rebuild, so nothing stores a raw label across rebuilds (see section 2).
* The plane is rebuilt when the static epoch changes: `World::placementEpoch_`
  or the structure signature (structure ids, positions and yard states).
  `placementEpoch_` is bumped by `setMapPlacementFeatures`, `removeMapFeature`,
  `placeMapFeature` (only when the install succeeded or replaced something),
  `blockCells` and `setTerrain`. A rebuild is whole-map; there is no dirty
  rectangle.
* A class's first plane build is one-time setup and is free. Each later
  rebuild charges `2*W*H` (build plus labelling) as **plane debt** against the
  field quota (section 2). The carried debt is clamped to half a tick's quota,
  and at most half the quota pays debt in one tick, so field work always gets
  at least half. At most 24 planes are live; at the cap, the least recently
  used plane that no group refers to is replaced.

The `legion_clearance` test checks that the plane matches `mobilePlacement`
at every origin, with slopes, water and jagged walls, for footprints 1x1 to
4x4 and one 3x2 footprint.

Moves on the plane are 8-connected. A diagonal step requires both
orthogonal neighbours to be legal, so there is no corner cutting.

### 2. Goals, groups and fields

**Goal resolution.** A requested goal on an illegal origin is replaced by the
nearest legal origin within 24 cells. Past that radius the search widens over
square ring perimeters to the whole map, but accepts only origins in the
unit's own start component, so a click deep inside a lake or a cliff mass
walks to the nearest point the unit can actually reach, as Retail does. The
goal is unresolved (`-1`, trapped at once) only when nothing reachable exists
or the unit starts on an illegal origin.

**Nearest reachable approach.** If the resolved goal lies in a different
static component from the unit, and the unit's own component has at least
256 origins (`kApproachRegion`), the unit becomes an **approach member**. Its
goal becomes the component's nearest origin to the requested one
(`nearestReachable`: exact minimum squared distance, ties to the lower cell
index, square rings clipped to the component's bounding box). It walks there
and then holds with the order kept (section 4). A unit in a component smaller
than 256 origins, a sealed pocket, holds where it stands instead. The real
goal is kept in `Member::real`.

Goal resolutions are cached per (plane, requested origin, start component)
for the current static epoch. The cache is a pure function of the plane, so
it is not hashed; it is cleared on an epoch change, on plane reuse and at
4096 entries.

**Groups.** Every plain ground Move leg is registered with Legion. A member
joins an existing group only if all of these hold:

* same player, issue tick and mobility plane;
* its goal is in the same static component as the group, compared live
  through a stored seed cell (`Group::compCell`, falling back to the first
  seed still legal), never through a stored label;
* the group and the member are both approach groups, or both are not;
* its goal origin is within 16 cells of the group's goal bounding box;
* the group's field has not started, or the goal is already a seed;
* the group has fewer than 256 distinct goal origins (`kGroupSeeds`), or the
  goal is already a seed.

A right-click on a selection is normally one group; so is each leg of a
queued group command. A single unit is a group of one. A member whose own goal
it cannot reach therefore never shares a field with a reachable teammate's
goal, and a stand-in approach point never shares a field with real goals.

**Fields.** Each group gets one **integer distance field**, seeded at every
member's goal origin, computed with Dial's bucket queue using step costs of 5
(orthogonal) and 7 (diagonal) over the static plane, with no corner cutting.
All members share it: a 2,000-unit order costs one field per footprint class
and per 256 distinct goals, not 2,000 searches. The 256-goal cap exists
because a field is "distance to the nearest seed of anyone": seeded with an
army's whole goal lattice it pulls every body to the lattice's near edge.

Field work runs at the start of `World::tick` under a deterministic quota of
4M relaxations per tick, shared by plane debt and all fields in group-id
order. A field resumes across ticks until done. Groups with no field start
before stale refreshes of older groups; served the other way round, constant
churn restarted the refreshes every tick and starved new groups.

A field covers only the bounding box of its seeds' static region, the only
area it can ever reach; cells outside read as unreachable. Live fields
(refreshes in progress included) are limited by total cells, equal to 48
whole-map fields, and by a count of 1,024. A field that has served its group
for 300 ticks can be displaced, oldest build first. Before regions bounded
fields, 1,000 single-unit orders each in its own walled pen competed for 48
whole-map slots: 633 evictions, 623 of 1,000 settled and 222 MB of fields.
Bounded, all 1,000 settle by tick 548 with no evictions and 2.3 MB.
Otherwise a new group waits for a slot; LRU eviction was tried first and
thrashed. Group state is in `std::map`, so iteration order is deterministic.

**Static changes.** On an epoch change, a finished field keeps steering its
group while its replacement builds in `Group::next`, and is swapped out when
the replacement is done. This is safe because a stale potential can only
misdirect: the plane and `commitGroundStep` decide legality. A group's first
field, if half built, keeps building on the new plane and is marked stale so
that a clean rebuild follows. Arrival slots are marked stale and rebuilt on
the new plane around the cells already claimed.

**Pruning.** `tick()` checks up to 256 members per tick, in id order from a
hashed cursor, and drops any whose unit is missing, dead, embarked,
orderless or no longer supported. The death edge also calls `cancelPath`,
which reaches `LegionNavigator::cancel`.

### 3. Steering: straight lanes, field descent, walls

Each update a supported unit:

* goes **straight to its own goal** if the footprint sweep from its current
  position to the goal is legal. The sweep is an exact supercover in origin
  space using 16.16 integers, and an exact corner crossing must clear both side
  cells. Reach is 160 cells, or 640 cells for a member with a formation slot
  (section 5), and the line is rechecked only when the origin cell changes. A
  single unit on open ground therefore moves in a straight line, and a
  formation keeps its shape.
* otherwise **descends the field**: it takes the legal neighbour with the
  steepest descent per unit of path length (an orthogonal drop of 5 equals
  a diagonal drop of 7). Ties go to the neighbour nearest the unit's own
  goal, then a fixed direction order. Plain first-found ties pulled bodies
  toward other members' goals, folding a formation into a file
  (`legion_formation` guards this). The unit then string-pulls up to three
  further descent cells that remain in a legal straight line, and aims there.
  Each aim point is on a segment that has already been proven legal, so the
  unit never aims into a wall. At a jagged edge the steepest legal descent
  runs along the wall, so the unit slides instead of pressing into it. Inside
  the flat zero-potential goal region, a member whose own goal is not in line
  steps to the legal neighbour that most reduces its distance to that goal.
* **moves along the proven direction exactly.** The heading turns toward that
  direction at the authored rate, and speed is capped while the body still
  faces away from it (full speed within 22.5°, half within 67.5°, an eighth
  otherwise). Because turning lag never moves the footprint off the proven
  line, the units cannot orbit a waypoint, overshoot into terrain or rock on a
  step. A lane-discipline pass (section 4) is exempt from the cap.

The step is committed with `commitGroundStep(..., strictDiagonal=true)`, the
authority Retail uses. An update's step that would change both origins at
once where one side origin is illegal (a proven line passing a wall's end a
fraction of a pixel from the corner) is blocked like a body; when neither
flow-around nor anything else frees the unit, it crosses only the legal axis
that update, the other coordinate stopping at its origin's edge, exactly as
the line itself passes. Before this, a body at such a corner held forever: on
a placement plane the corner step read as a body block with no way round, on
a legacy plane the mover refused it every update (`legion_wallend`). Applied
first instead of last, the same clip changed motion at corners that
flow-around already resolves (bridges and crowdtrap 2000 lost about 30
crossings, maze 200 lost 12, seeds 0/7/42).

Before committing, Legion checks the destination origin,
and both orthogonal origins on a diagonal, against occupancy and
`mobilePlacement`. Every Legion motion path, including pass steps, shuffles,
detour routes and yield steps, goes through `commitGroundStep`; none writes a
position directly. A unit standing on an origin the plane rejects (inside a
yard, or under a new structure) leaves it through Retail's own steering.

### 4. Traffic and stuck classification

Progress is the integer distance still to go: field potential, or squared
cells to the own goal while on a straight line.

* **Terrain-trapped**: the unit's origin has no finite potential once its
  group's field is complete, or its goal could not be resolved. No legal
  route exists for this footprint. The unit stops on the same update and then
  never moves, turns or probes. It keeps its order: a new static epoch (a gate
  opening, a wall destroyed, a building removed) re-plans it, with no
  polling. If nothing opens within 5 minutes (9000 ticks), the leg is retired
  with `dropLeg`, as Retail does for an unreachable goal, and queued legs
  continue. `legion_trapped` checks both paths: a sealed pocket that stays
  sealed, where the unit never moves and its order retires at about 9000
  ticks, and the same pocket opened at tick 300, where the unit resumes and
  arrives.
* **Approaching an unreachable goal**: an approach member walks to its
  nearest reachable point and, on arrival or contact, enters the same Trapped
  state (zero speed, constant heading, order kept) instead of completing. On a
  static epoch change it re-registers only if its stand-in no longer holds
  (`approachValid`): the body left the point's region, the point became
  illegal or unreachable, or the real goal became illegal or joined the
  body's region. Otherwise its walk, detours and timers are kept. The 5-minute
  retire counts from the first time this order's goal was found unreachable,
  and also applies while the member is still walking. `legion_approachhold`,
  `legion_approachopen` and `legion_approachchurn` cover holding, resuming
  when the wall goes, and churn that must not reset the clock.
* **Crowd-blocked**: a route exists but bodies occupy it. The unit first
  **flows around** the blockage by stepping to any free legal neighbour that
  strictly reduces its distance still to go. The step is committed until the
  new cell is entered. It moves only along the axis whose origin changes,
  never back toward a cell centre, and it does not turn the body; this is a
  shuffle. Speed still follows the facing cap. A flat half-speed shuffle was
  measured to gridlock opposing columns. If no such neighbour exists the unit
  **holds**: speed zero, heading constant, no creeping. A held unit rechecks
  its candidate cells against occupancy every update. This is change-driven,
  since nothing moves until a cell frees, and it never triggers a re-plan, so
  a long queue at a choke costs no search work.
* **Oncoming traffic, before contact** (`passAhead`): a mover scans its own
  swept lane 6 cells ahead. If a Legion mover is there whose way to its own
  goal points back at this body by more than about 112° (judged by goal
  direction, not heading, so a body that has not turned yet is not
  oncoming), the body commits to a one-cell step to its right, forward-right
  first, then straight right. The step does not turn the body and keeps
  travel speed. For the next 300 ticks (`kPassHold`) a direct-line body keeps
  walking straight in its new lane, and the flow-around shuffle may not step
  back across the lane it left. No pass happens within 12 cells of either
  body's goal, where bodies are packing into place.
* **Opposing traffic, in contact**: after 6 held updates against a body with
  orders facing more than 90° away, the unit commits to one lateral step to
  its own right. The step does not turn the body (turning 90° and back in a
  jam read as spinning) and times out after 45 ticks. Same-direction queues
  never side-step.
* **Walled in by still bodies** (settled arrivals, a held queue, trapped or
  approach-parked members, idle units of any player): after 8 held updates,
  the unit runs a bounded BFS (25x25 window) over legal origins whose
  footprint touches no still body. The target is its own goal if it is inside
  the window, otherwise the reachable cell that most reduces its distance to
  go, and only a gain of about two cells is committed. The route is walked
  cell by cell without turning the body. It is attempted only when the
  blocking body will not move by itself (idle, arrived, trapped, or not a
  Legion mover), or when a formation member has been held for 60 updates in
  a standing jam of crossing lanes; never for a queue of members waiting on
  each other. Attempts back off geometrically (30, 60, ... 480 updates) while
  no progress is made.
* **Settled arrivals yield**: when a unit completes, Legion records an anchor
  (the origin it completed on). A member held for 12 or more updates, with no
  detour or route, whose wanted cell is held only by settled, idle,
  same-player anchored bodies, asks those bodies to step one cell aside; if
  that fails, it tries the same for a neighbour nearer its own goal. Every
  blocker must find a target or none moves. A target is within one cell
  (Chebyshev) of the blocker's anchor, legal for its footprint, clear of the
  requesting member's current and wanted footprints and of targets already
  chosen in the same request, and, for a formation arrival, inside the
  point's area and off cells other members claimed there. Yields run in
  `tick()` every tick, in id order: a straight step at ground speed without
  turning, ended by arrival, a refused step or 45 ticks. Each body yields at
  most 3 times. An anchor is forgotten when the unit dies or gets any new
  order.
* **Heading**: the body turns only on a committed step toward its route,
  with a 5.6° dead band. A refused step, a hold, a shuffle, a pass, a
  side-step, a detour route, a yield and a trapped stop never turn it.
* **Arrival on contact**: once a unit has made no progress for 20 updates, it
  may arrive inside its goal's area while touching a settled same-player
  body. With a distinct goal the area is one body width and the goal cell
  must itself be occupied by another body, so settling short never plugs a
  neighbour's lane. The rules for shared points are in section 5. The
  destination is an area, so a blocked unit never arrives outside it.

### 5. Shared points: formation slots

When two or more bodies are sent to one point in one command, Legion treats
the point as a destination area for the whole command, across every
footprint class (mixed footprints form one group per class but share one
`Point`).

* **Formation assignment** (`assignFormation`): once at least two members
  take part, each member's target is its offset from the members' centroid,
  scaled so that the crowd's RMS spread matches the packed disc those bodies
  occupy, placed around the point. The packed area is the sum of side² per
  body, or (side+1)² when sizes are mixed, since different sizes never tile.
  The area limit is that disc plus three widths of the smallest body, minus
  8 px. Members are served front-first along the centroid-to-point direction,
  so the front of the crowd takes the far side. Each takes the nearest origin
  to its target that is statically legal, in the group's component, clear of
  every cell already claimed at the point, and inside the limit. A member
  that joins later maps its own offset with the stored transform. A member
  that gets no cell (`slot=-2`) walks to the point and looks again only while
  held. If fewer than two members can take part at the first attempt, the
  point retries every 16 ticks. Approach members take no slot and do not size
  the area.
* **Straight lanes**: a member with a formation slot probes the direct line
  out to 640 cells instead of 160, so the crowd does not funnel into a file
  by descending the shared field.
* **Slot re-choice**: a member held for 20 updates (and every 20 after)
  re-chooses. A BFS (radius 24) over origins that no other body covers finds
  the reachable free cell nearest the point. This stays active for the last
  member of an assigned point too. A member's own re-registration pins its
  point, so the formation and the cells arrived bodies claimed survive it.
* **Settling**: inside the limit, a member that has been still for 300 ticks
  (`kAreaSettle`) settles, as does one pressed against settled bodies or
  against still members of the same point. A member walled out at the ring
  edge settles after 600 still ticks if it is within the limit plus two body
  widths.
* **Close enough** (`crowdSettle`): a body that stopped gaining on its point
  settles where it stands when it touches a same-player body already settled
  for that destination (or idle) and nearer the point, within four packed-disc
  radii of that crowd plus two bodies, on a field-connected spot outside any
  factory exit lane. Out to twice that reach, touching a settled arrival of
  the destination (not merely an idle body) also settles it, after 1800
  still ticks (`kFarSettle`): a crowd fed from one side grows toward its
  arrivals, and its last ones otherwise held forever against its face.
* The older per-goal packed slots (built inside-out by field potential,
  claimed back to front along each member's approach) remain as the fallback
  when no formation applies. Claims skip slots that are now illegal or
  unreachable, so a corpse or building on a slot cannot cause a re-claim
  livelock (`legion_slotblock`). Approach members never claim slots: they
  queue to their stand-in point in the order they come.

The in-game right-click in Legion mode sends one shared point for ground
units with footprints up to 8 whose order is not queued behind existing
orders. Boats, hover units, flyers and queued legs keep Retail's per-unit
offsets.

### 6. Determinism

All decisions use integers: 16.16 positions, BAM headings, integer potentials,
squared distances and `isqrt64`. Work quotas are counts, never wall clock.
Containers that drive decisions are ordered (`std::map`, `std::set`, sorted
vectors with id tie-breaks). Everything persistent is folded into
`LegionNavigator::checksum`: groups (seeds, bounding box, region cell,
sharing and peak counts, field and refresh progress, slots), points
(formation transform, limit, claimed cells), anchors, yields, plane debt, the
prune cursor and every member's state, timers, detours, routes, pass lane and
approach fields. The only unhashed inputs are values derived again from
World each tick (structure signature, plane labels) and the goal-resolution
cache, which is a pure function of the plane. `World::stateHash` mixes that
checksum, and the orders' issue ticks that define groups, only in Legion
mode. Movement and yields run serially. The `legion_determinism` test and the
`navigation_determinism` goldens check that runs are repeatable and that
serial and `--workers` runs produce the same hash. `sweep` uses `__int128`,
which GCC, Clang and MinGW all provide.

## Scope

Supported: ground units (`Domain::Ground`, footprint 1..8, plus non-square
footprints when a placement plane is present) whose current leg is one of
these **mission goals** (`LegionMission`):

| Kind | Legs | Goal | Ends by |
|---|---|---|---|
| Move | plain Move legs, group right-clicks, queued legs | the point | Legion arrival raises `0x500` |
| Fight | fight-move (`attackMove`) legs | the point, shared area per command | Legion arrival, or the native circle (`groundMissionAccepts`), raises `0x500` |
| Patrol | patrol legs, single or group | the waypoint, shared area per command | as Fight; the body passes through (no anchor, cells released) |
| Attack | explicit and auto-acquired attacks, chases | the target unit, re-seeded | combat: `tickCombat` stops the body in range; Legion never completes it |
| Guard | guard/follow | the guarded unit, re-seeded | guard: stops within 70 px; Legion never completes it |
| Build | MobileBuild approaches (`buildRectangle`): single, queued, mana build area | the rectangle's `navigationCell`, else its nearest legal reachable perimeter origin | the rectangle itself (`accepts`): `0x500`; anywhere else `0x200` |
| Repair | repair legs | the target's point | work: `tickRepair` starts at reach; Legion never completes it |
| Reclaim | feature and corpse reclaims (also those an area reclaim queues) | the feature's point | work: `tickReclaim` starts at reach; Legion never completes it |
| Load | passenger to its carrier, ground carrier to its passenger | the transport circle's centre | the native circle: `0x500` on `transportMission`; elsewhere `0x200` |
| Unload | ground carrier's surface-unload approach | the drop point | as Load |
| Exit | automatic production exit step | the generated exit point | arrival `0x500`; or, without headway for a crowd window, once the whole birthplace is clear (or after two windows when a rally follows) |
| Park | PARK sequences (factory output, released cargo) | the ring's navigation point | the ring (`park->ring`): `0x500`; elsewhere `0x200` |

Legion is the route provider only. The mission handlers keep every mission
rule: when to fire, when the unit is in range, target acquisition during a
fight-move, patrol rotation, retirement. Legion replaces the native search
and steering for these legs; the native mover never runs for them.

* **Arrival.** A kind that completes raises the same `0x500` event on the
  leg's mission that the native mover does. Attack and guard never complete
  in Legion: reaching the goal or a crowd there is a hold (zero speed, order
  kept), and combat or guard ends the approach. Legion never declares arrival
  outside the rules of section 4/5 or the native goal predicate.
* **Moving goals.** Attack and guard goals follow a unit. The member is keyed
  by (kind, target id) instead of a controller. The field is re-seeded when
  the tick crosses a 16-tick grid line and the target's origin has moved at
  least 2 cells (Chebyshev). Every chaser of one target that re-seeds in the
  same 16-tick window joins one group and shares one field.
* **Groups and areas.** Groups never mix kinds. Fight-move shares a
  destination area (formation slots) per command, as Move does. Each patrol
  lap gets its own group and field, because the outbound and return legs of
  one patrol share an issue tick and a field seeded at both ends would pull
  bodies toward the wrong end; the waypoint area is still shared per command,
  and an arriving patrol body releases its cells. Attack and guard take no
  area and no packed slots.
* **Events.** Legion masks the search-failure events (`0x2600`) that make
  Retail enlarge the goal circle, for Move, Fight and Patrol. A blocked
  Legion army therefore never "arrives" because the circle grew.
* **Work and logistics.** Build, Load, Unload and Park are *exact*: the only
  arrival is the mission's own geometry (build rectangle, transport circle,
  park ring). Reaching Legion's seed or settling against a crowd outside it
  raises the native failed-approach event `0x200` instead, and the handler
  decides exactly as after a failed native search (build if within reach,
  re-roll the park ring, retry the pickup). Load/Unload events go to the
  leg's `transportMission`, as in the native mover. Repair and reclaim legs
  have no movement controller; the member is keyed by (kind, issue tick,
  target). These kinds are *solo*: each body gets its own group and field
  (a teammate's seed is not on this body's rectangle or within its reach).
  Embarked units are never members. A work/logistics body that cannot
  accelerate (accel 0) is not crept forward, so a released passenger stays on
  its landing point. Production exits carry their birthplace
  (`Order::productionExit`) in Legion mode too, so a held-up exit never
  blocks the factory lane: it ends only once clear of the birthplace, or
  hands over to its rally. Mission semantics (build start, placement, reach,
  park rotation, transfers, retirement) stay in the handlers.

A leg is supported only if the legs ahead of it are route corners (for Move,
plain corners exactly as before). Plain Move behaviour and hashes are
unchanged by the mission interface: crowdbench Legion hashes are identical on
all 18 scenarios. New member and group state is folded into the checksum
only for non-Move kinds.

Area reclaim and the mana build area need no kind of their own: their
approach steps are plain Moves and the work they queue is Reclaim/Build.
Still delegated to Retail: boats and hovercraft (a separate change), and
transport legs of flying or water carriers. Flyers stay native: flight is not
pathfinding. The interface for adding
them is a `LegionMission` kind plus a `Policy` entry (see `legion.cpp`).
Retail and Retail+ behaviour is bit-identical to builds without Legion:
per-tick traces and hashes compared on each Legion change.

The former corner hold at a wall's end (a single body or a 12-body column
on a legacy nav-grid world, and the jammed rally column of the generated-maze
Troll cohort, 64 of 100 unsettled) was the one-update corner step described
in section 3; it is fixed for every plane. The maze cohort's last 17 bodies
then stood pressed against the west face of a rally crowd that had grown
toward them, just past the "close enough" reach; they now settle there after
1800 still ticks (section 5). All 100 settle (tick 32,966), serial ==
workers (`legion_movement_orders_trolls_maze`).

## Tests

* `legion_world_test`, 27 cases: clearance, groupreuse, jagged, trapped,
  crowdhold, replace, unreachable, quota, determinism, formation, slotblock,
  deaths, deathsshared, splitgoal, farclick, churn, approachhold,
  approachopen, churnfield, approachchurn, legacyyield, planeincremental,
  planeprebuild, penstale, lattice, wallend (one body and a 12-body column
  round a `nav().block` wall's end on a legacy plane, without turning in
  place). All pass.
* `legion_acceptance_test`, five checks run in all five modes; Legion
  asserts, the others report. singleunit, jagged, trapped and group pass for
  Legion; crowdheld is a known failure (below).
* `navigation_determinism` and `legion_determinism`.
* `legion_movement_orders` (`movement_orders_test --legion`): attack approach
  around terrain, chase of a moving target, guard follow, group patrol,
  fight-move and patrol laps, combat resume, partial routes, repair and build
  approaches around a water wall; each case also checks that Legion actually
  served the mission (`LegionNavigator::mission`).
* `legion_production` (`production_test --legion`), `legion_transport`
  (`transport_test --legion`: boarding, ground unload approach, PARK of the
  released passenger), `legion_movement_orders_trolls` (100 and 256 Trolls
  on open ground) and `clear_build` (Legion mode: reclaim then build) assert
  that Legion served the leg.

## Rejected approaches

Each was measured and dropped. Unless noted, numbers are Legion means over
seeds 0/7/42 at 6000 ticks, 1 player, 100% moving. Patches named here were
parked outside the repository with the measurement notes.

**Core steering (first Legion commit)**

* Steepest-only descent: open ground 181 of 200 arrivals; it folded
  formations into a file.
* Own-goal-first descent (any lower neighbour): open 198 and doors 152, but
  maze fell to 91/50 crossed/arrived.
* Same-direction "purposeful" side-steps: opposing columns 173/169 fell to
  132/90, shared goal 126 to 106.
* A half-speed cap on non-turning shuffles: gridlocked opposing columns
  (found by hunk bisection).
* Uncommitted shuffles (variant I): 2499 settled arrivals over the 200-unit
  screen against 2537 for the retained version. Variants D and G (no shuffle
  commit; D plus heading rules) had better opposing columns but failed the
  crowdheld spin check with 156 and 226 spin ticks.
* Immediate retirement of trapped orders: failed the crowdheld and trapped
  acceptance checks, since a gate that reopens loses the army. Replaced by
  the 5-minute grace.
* LRU field eviction: thrashed, because every live group uses its field
  every tick. Replaced by the 300-tick tenure.

**Packed distinct goals (the group and crowdheld checks)**

* Sideways "lane steps" (`v8-lane-steps-bd51dc6.patch`, reverted): group
  passes 64/64, but crowdtrap 2000 arrivals fell 188 to 154, maze 200 fell
  78 to 74 arrivals and 150 to 135 crossings, and sharedgoal 2000 fell 94 to
  77 arrivals and 889 to 316 crossings.
* Lane steps for distinct goals only (`v11-distinct-only-on-merged.patch`):
  fixed sharedgoal but kept the crowdtrap and maze losses, and crowdheld
  terrain-stuck rose to 4.
* Lane steps limited to near the own goal (`v12-lanereach-rejected.patch`):
  group 61/64 at 8 cells, 57/64 at 16 cells.
* Yield-aware detour, a BFS that treats yieldable settled bodies as passable
  and asks them to yield along the route
  (`v5-formation-plus-yielddetour-rejected.patch`): groupdetour seed 7 fell
  61 to 53; displaced bodies formed new diagonal blocks.
* Yielded bodies returning to their goal 90 ticks later
  (`v10-yielddetour-returnhome-rejected.patch`): 56/62/60 with the yield
  detour and 55/63/57 alone, against 61/63/61 for the base.
* Deep-first ordering gates (`order-gate-experiments-rejected.patch`, a
  getenv-switched experiment, not commit-ready): shallow members hold at the
  lattice face while a deeper goal in their lane still has a moving owner.
  The best variant reached group 62/64 and crowdheld 59/64 with 1
  terrain-stuck, so both checks still failed. Longer hold budgets dropped
  group to 49-59, seal-avoidance gave 54-61, and a wide still-body detour
  (radius 24-40) gave 53-62. Holders stand in the lane entries their deep
  owners need, and holders next to walls count as terrain-stuck.
* Goal exchange: ruled out by the benchmark rule. Goals are 48 px apart and
  the authored arrival radius is 32 px, so a swapped goal can never count as
  an arrival.

**Fields and planning**

* Charging a class's first plane build to the field quota: delayed the first
  fields by a tick, which split orders into different groups and cost jagged
  2000 about 30% of its arrivals.
* A 64-goal group cap instead of 256: jagged 2000 reached 671 arrivals
  against 584, but `legion_formation` showed a row drift of 4 cells.
* A byte-mask mirror of the claimed-cell sets in formation searches
  (`rejected-claimed-mask.patch`): identical hash and no measurable time
  change (3.43/3.38 ms against 3.36/3.34 ms on sharedgoal 2000).

**Traffic** (opposing columns at 3000 ticks, 200 / 1000 units)

* Passes that turn the body (v1): 179/468 crossed, spin 2244/10759 ticks.
* Non-turning passes under the facing cap (v2a/v2b): 141-143/466 crossed.
* Fast passes without a lane hold (v2c/v2d): spin 4910/78039 ticks, almost
  all travel reversals. Lane hold without the no-step-back rule (v3): spin
  4412/75145. A 45-tick hold (v4): 1735/46807. A 150-tick hold (v5): 85/817.
  The retained 300-tick hold measured 74/395.
* Only advancing (diagonal) passes at full speed (v2e): 132/474 crossed.
* Lane hold for field followers too (`v9-fieldlane.patch`): every opposing
  case got worse (2000: 979/194 against 991/194; 500x1: 449/277 against
  483/311).
* Oncoming judged by heading instead of goal direction: rapidreplacement
  500x4 fell 2000 to 1878 arrivals with 8x the spin ticks.
* Passing inside destination areas: doors 2000 at 12000 ticks fell 439.7 to
  428.3 arrivals.

**Arrival and approach**

* Approach members claiming shared-goal slots: recovery 200 fell 200 to 181
  arrivals once the wall opened, because the near goals filled first and
  walled in the deep ones. Approach members now queue.
* Approach points sharing a field with real goals: the reachable member of
  `splitgoal` descended to the stand-in seed and never arrived.
* Settling a walled-out formation member after 300 ticks: sharedgoal 200
  lost an average of 4 arrivals. The retained value is 600.

## Round 3: lag spikes and group-move jank (2026-10-05)

A user replay (8x speed, protocol 228, Legion, 49,000 ticks, generated
768 x 768-cell map, up to 463 units) showed hitches on group moves. Profiling
it found two Legion causes; both are fixed:

- **Whole-map plane rebuilds.** Any static change (corpse placed or decayed,
  feature change, structure built or destroyed, yard open/close) rebuilt every
  footprint plane in use synchronously: 427 rebuilds in 168 ticks, about
  10.6 ms each, up to three in one tick. World now records the rectangles of
  changed feature cells; each plane recomputes only those cells and the
  footprints covering them, and repairs region labels locally (union-find
  merges compared by root; a bounded interleaved search settles possible
  splits). A new static epoch starts only when some plane's legality actually
  changes, so non-blocking corpses no longer trigger anything. A unit class's
  first plane builds in the background at about 1 ms per tick. The
  `planeincremental` test checks the plane and labels against a fresh rebuild
  after every one of 1,600 random changes.
- **Field work per tick.** The quota fell from 4M to 384k relaxations per
  tick (about 2 ms worst case, measured). Fields grow outward from the group,
  units may steer on a partly built field once their own cell is final, a
  unit no field reaches yet follows a proven straight segment, and fields are
  bounded to a box around the group and its goals. Group orders now start
  moving on the first tick (p95 order-to-motion 1 tick, from 2–3).
- **Livelock.** A unit blocked by settled same-player arrivals asks them to
  yield before planning another detour away (test `lattice`); acceptance
  `group` now passes 64/64.

Replay result (whole-tick profiler, which now includes navigator upkeep):
ticks over the 4.17 ms 8x budget fell from 127 (as first measured, without
Legion's own upkeep) to 2, both match-start setup; p999 is 1.8 ms and only 8
of 49,000 ticks exceed 2 ms. Cost: jagged-wall arrivals fall 1–6% (field
builds finish later), and one unit in dynamicobstacle 2000 (one seed) is
stuck against terrain for about 1,400 ticks.

**Not fixed: sliding.** With real unit types (tools/legion_group_motion.cpp,
60 units, 3,000 ticks), Legion units rarely change heading (793–5,693
heading-change ticks against Retail's 112,000–121,000) but move sideways or
backward relative to their facing more often: open ground 2,685 sideways /
987 backward / 2,585 stop-go restarts (Retail 2,150 / 151 / 780); along a
wall 10,729 sideways (Retail 4,115). Detours, blocked shuffles and passing
steps deliberately do not turn, to avoid spinning. Every tested remedy —
turning on detours or passing steps, a swerve, minimum restart holds, and
seven gated variants of refusing backing-away detours — either reintroduced
spinning, cost crossings, stranded units, or failed acceptance `group`.
Patches and tables are archived with the session reports. The sliding comes
mostly from late units that cannot reach their slot inside a packed settled
crowd; the promising next step is upstream (claim slots in approach order,
or let settled arrivals compact forward), not gating detours.

## Round 4: CPU, chokepoints and sliding (2026-10-05)

- **CPU (behaviour-equivalent).** The hot path was ordered-map lookups on every
  unit's move update (own member, neighbours in the lane scan, shared points),
  not the line checks or searches. A flat id-indexed member table pointing into
  the existing map (which still drives ordered iteration) and a cached point
  pointer per member cut Legion's tick cost 10–29% at 2,000 units. Traces and
  hashes are byte-identical.
- **Passage lanes.** In straight-walled passages narrower than 8 cells, units
  following the field prefer cells on a lane grid set by one wall, so a 6-cell
  door carries three 2x2 lanes instead of two. Doors 2000: 400/188 to 435/215
  crossed/arrived (12,000 ticks: 397 to 526 arrived, now above Cooperative's
  487); bridges 2000: 361 to 394 crossed (above Cooperative's 380); doors 500:
  371/166 to 408/200 (Flowfield 419/197). A 16-cell span cost maze 15%.
- **Side-steps only for real oncoming traffic.** A unit steps right only when
  the blocker is genuinely heading the other way and neither is inside its
  destination area; it now waits behind same-way units.
- **Pass-facing.** A pass turns the unit toward its step while it walks, but
  only when the lane ahead is nearly empty; in dense opposing blocks it stays
  an unturned one-cell dodge.

Five-mode timing, same binary, 2,000 units, 3,000 ticks, median of three
pinned runs on idle P-cores (ms/tick; crossed in parentheses):

| Scenario | Retail | Retail+ | Flowfield | Cooperative | Legion |
|---|---|---|---|---|---|
| open | 2.16 (2,000) | 2.28 (2,000) | 2.61 (1,711) | 2.68 (1,267) | 2.48 (2,000) |
| doors | 1.96 (96) | 2.75 (41) | 2.35 (111) | 2.47 (127) | 2.15 (182) |
| shared goal | 3.41 (377) | 6.50 (245) | 2.52 (2,000) | 2.65 (2,000) | 2.16 (1,499) |
| opposing columns | 1.98 (773) | 3.47 (761) | 2.06 (823) | 2.77 (918) | 2.37 (922) |
| maze | 1.20 (0) | 2.18 (0) | 2.38 (0) | 2.00 (0) | 1.77 (0) |

Legion is now cheaper than Retail+, Flowfield and Cooperative in every case
and the cheapest overall on shared goals; Retail remains 10–48% cheaper on the
others.

**Sliding: partly fixed.** Measured with real unit types, motion counts summed
over 52/56/60/64/68 units (single runs vary ±40%), sideways / backward /
stop-go:

| Case | Retail | Before round 4 | After round 4 |
|---|---|---|---|
| open | 12,426 / 755 / 4,774 | 10,487 / 3,193 / 14,087 | 9,528 / 3,388 / 13,913 |
| wall | 18,849 / 729 / 6,265 | 52,294 / 3,015 / 4,091 | 51,118 / 2,895 / 4,039 |
| cross | 18,681 / 858 / 6,342 | 27,575 / 5,675 / 6,320 | 17,926 / 3,710 / 6,154 |

Crossing traffic now slides less than Retail; open ground and walls do not.
Rejected this round (patches archived with the session reports): approach-
order slot claiming with facing (spin rose to about 159k unit-ticks in packed
shared-goal crowds), settled-crowd compaction (worse motion and arrivals; late
units are not short of spots — units of different speeds arrive out of order),
facing during shuffles (that is visible spinning), facing on detour routes
(spin about 77k in shared-goal crowds), idle units stepping aside (cost
opposing traffic). **Speed-matched following** fixes stop-go — below Retail
in all three cases — but every variant leaves 1–5 units flagged terrain-stuck
by acceptance `group`: a follower held behind a leader on a different path,
next to a wall, with no body on its own direct line, trips that check. Whether
to change that check or the mechanism is an open decision. (Decided: see the
follow-up below.)

### Round 4 follow-up: speed-matched following ships (2026-10-06)

**User decision:** ship speed-matched following, and narrow the acceptance
terrain-stuck classification it tripped.

- **Following (Legion only).** In `drive()`, a body that has a slower body of
  the same player one or two cells ahead along its step, heading within 45° of
  its own, matches that body's speed (never below half its own) instead of
  running up and stopping dead. A leader slower than half this body's speed
  cap is not followed: matching a creeping body chained whole queues down to a
  crawl at door corners (acceptance `group` then counted 4 terrain-stuck units,
  and maze 200 lost 15% of its arrivals with a quarter-cap floor). The parked
  "close-up within the cell" half of the old patch is NOT shipped: it alone
  kept 1–4 units creeping at a door corner. Route facing and shuffle facing
  stay rejected (spin). No new state; nothing to fold into the checksum.
- **Narrowed terrain-stuck verdict (all modes; observation only).** In
  `tools/crowdbench_matrix.h` / `crowdbench_acceptance.h`, a unit that would be
  terrain-stuck (no progress, free static path, no body on its static line,
  touching terrain) is now crowd-held when it is queued behind a moving leader.
  A leader is a same-player mobile member whose centre is within two body
  widths, lies within 60° of the unit's goal or static travel direction, and
  made at least 16 px of progress over the same 90-tick window. A unit against
  a wall with nobody moving ahead of it is still terrain-stuck. A temporary
  mutation that froze every Legion body touching a wall still failed `group`
  with 1 unit terrain-stuck. Every crowdbench hash in every mode is unchanged,
  and so is every mode's acceptance verdict. Other modes' terrain-stuck
  unit-ticks fall a little on doors/sharedgoal/jagged/groupdetour/crowdtrap
  at 300 units (Retail 993→881, Retail+ 799→440, Flowfield 320→296,
  Cooperative 295→284). The shipped following also passes `group` under the
  OLD verdict, so the verdict change is not what lets it pass.

Motion (52–68 units summed; sideways / backward / stop-go / tool arrivals):

| Case | Retail | Before | After |
|---|---|---|---|
| open | 12,426 / 755 / 4,774 / 137 | 9,528 / 3,388 / 13,913 / 86 | 5,860 / 1,636 / 391 / 80 |
| wall | 18,849 / 729 / 6,265 / 41 | 51,118 / 2,895 / 4,039 / 66 | 33,105 / 1,984 / 1,992 / 55 |
| cross | 18,681 / 858 / 6,342 / 141 | 17,926 / 3,710 / 6,154 / 182 | 17,567 / 4,084 / 1,106 / 166 |

Stop-go is now well below Retail in all three cases, and open/wall sliding
roughly halves. Backward remains above Retail everywhere. The motion tool's own
arrival count falls 7–17% in these 3,000-tick runs. The crowdbench scoreboard
does not show that drop. Across 18 scenarios × 200 / 2,000 / 500x4 × seeds
0/7/42 at 6,000 ticks, against 0f577f5:

- crossed: +2.1%
- arrived: +4.8% (bridges, doors, crowdtrap, sharedgoal and mixedfootprints
  gain 8–34%)
- spin: −41% (5,075 → 2,984 unit-ticks)

Two cases rose in spin on every seed but stay small: opposingcolumns 2000 went
688 → 932 (inside the base's 398–1,106 seed range) and mixedfootprints 500x4
went 6 → 82. The worst arrival change is maze/exploration 200 at 86.7 → 82.0,
inside the base seed range of 78–93.

Acceptance:

- `legion_acceptance` group 64/64, terrain-stuck 0.
- crowdheld (still disabled) now reaches 62 of 64 in goal, up from 59, with 2
  units ever terrain-stuck, up from 1.

## Known weaknesses

* **Packed distinct goals: one acceptance check is registered DISABLED as a
  known failure.** `legion_acceptance_crowdheld_legion` reaches 62 of 64 in
  goal with 2 units ever terrain-stuck (spin passes at 0; 59 and 1 before
  speed-matched following).
  `legion_acceptance_group_legion` passed 61/64 before the round-3 livelock
  fix and now passes 64/64. The
  thresholds are unchanged, and no other mode passes either check. The goals
  form an 8x8 lattice of 2x2 bodies at a 3-cell pitch, so the gaps between
  goals are one cell, narrower than a body. A goal can only be entered along
  its own row or column from one face of the lattice. Units come off the
  detour or out of the door in a single file in random order of depth, and
  the late ones find all four of their lanes sealed by settled bodies. A
  one-cell yield cannot clear a 2-cell footprint centred on the blocker's own
  goal, and every variant tried above made other scenarios or these checks
  worse. A real fix needs parking outside every lane of the deeper unsettled
  goals and a way for deep owners to pass parked bodies, or an entry face
  chosen per late unit from the lanes still free. The in-game group
  right-click sends one shared point, so it uses formation slots instead and
  is not affected.
* **Approaching an unreachable goal counts as trapped movement.** The
  acceptance and matrix observers classify a unit as Trapped whenever its
  origin cannot statically reach its goal disc, and any move or turn more
  than 150 ticks after that counts in `trapped_units_moving_after_grace`.
  Walking to the nearest reachable point therefore counts for every unit in
  the unreachable scenario, and in recovery and recovery-passive before the
  wall clears: Legion scores 200/2000 there, as Retail and Retail+ do, while
  Flowfield and Cooperative score 0 because they never move (and arrive 0 in
  recovery 2000, where Legion arrives 1999 of 2000). Units already at their point
  have zero spin and do not move. Pockets under 256 origins still hold in
  place; a region just above that threshold walks to its edge.
* **CPU cost at large populations is 20-50% above the cheapest mode.**
  Five-mode timing at a78d194 (before the review-4 fixes), 8 parallel
  P-cores, noisy, mean ms per tick at 2000 units: doors 3.99 against 2.77
  (Cooperative), opposing columns 3.83 against 2.77 (Flowfield), shared goal
  3.40 against 2.45 (Flowfield). Legion delivers the most arrivals in those
  cases. At 200 units it is the cheapest mode or tied in most cases. Field
  work is not the cost; counters point to per-unit steering: line sweeps (640
  cells for formation members, rerun on every origin change), descent, held
  rechecks and the per-held-member slot re-choice BFS.
* **Remaining losses.** Opposing columns at 2000x1 cross about 1076 against
  about 1176 for Flowfield; opposing columns 500x1 and 250x8 at 50% moving
  also trail (in the latter, idle bodies stand between movers and their
  goals, which is idle-obstacle flow, not passing). Exploration at 2000
  units ends with 0 arrivals in every mode.
* **Open items from the review-4 fixes.**
  * Under constant static churn a group's stale refresh restarts every tick
    and never finishes; the group keeps steering by its last finished field.
  * A first field built across epoch changes mixes old and new plane
    legality. This is safe, because the mover re-proves every step, but it
    can misdirect until the clean rebuild.
  * `assignFormation` is still a one-tick burst per point (up to 97x97 rings
    per member for a large shared click), and the 640-cell sweep and the
    49x49 re-choice BFS are bounded per member, not globally.
  * A walled-out formation member settling after 600 ticks can stand just
    outside the benchmark's authored radius: its order completes, but it does
    not count as arrived_settled.
  * Point cell claims do not follow a body that yielded.
  * The review-4 fixes were not screened at 500 or 1000 units, with 4 or 8
    players, at 12000 ticks, or on dynamicobstacle, rapidreplacement and
    exploration. Findings 2, 4, 5, 6, 7, 8, 10 and 11 have no dedicated test.
* **Whole-plane rebuilds.** Every static change rebuilds every plane in use
  over the whole map. The work is quota-charged and the debt is clamped, but
  a single rebuild is not split across ticks.
* **Group partitioning depends on registration order** and on field start
  timing (a started field takes no new seeds). The 256-goal cap chunks goals
  in registration order, so an order whose unit ids are not spatially
  coherent can get spatially interleaved groups.
* **Idle units of other players never step aside.** Only settled same-player
  Legion arrivals yield; everything else idle is a still body to route
  around.
* **Fog**: Legion plans on the static plane as Retail's mover sees it. It does
  not model unexplored terrain separately.

## Compatibility

* Network protocol: `kNetVersion` 228 introduced Legion. The mode travels as
  the pathfinding-mode byte, value 4 (`PathfindingMode::Legion`); a peer that
  does not know it is rejected by the version check.
* Lobby: Legion is the fifth entry of the pathfinding cycle. Campaign
  missions and Crusades always run Retail movement, whatever the lobby says.
* Retail, Retail+, Flowfield and Cooperative hashes and goldens are unchanged
  by Legion. Legion state enters `World::stateHash` only in mode 4.
* Headless runs: the DEBUG client flag `TAK_LEGION=1` selects Legion for the
  `--mpai` harness and other headless games (release builds read no `TAK_*`
  variables).
