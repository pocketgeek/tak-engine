# Legion pathfinding (mode 4)

> **Note (2026-10-06):** Retail+, Flowfield and Cooperative were removed by user
> decision; they remain in git history. Retail (mode 0, the default) and Legion
> (mode 4) are the only pathfinding modes. Comparisons below that mention the
> removed modes are kept as history.

Legion is a ground-navigation mode, the alternative to Retail, built for how Total Annihilation:
Kingdoms actually moves armies: a flat tile mosaic whose relief is baked into
16 px placement cells, square-ish footprints (shipped ground movers are 1x1 to
4x4, mostly 2x2 and 3x3), slope and water limits per unit type, features,
walls and buildings as blockers, and right-click orders that send dozens to
thousands of units at once. Code: `src/sim/legion.{h,cpp}`; hooks in
`src/sim/sim.cpp` next to the Retail dispatch, and the
group right-click in `src/client/gameview_hud.cpp`.

This document describes Legion as of ae3df2d (after the W0 instruments, before
the behaviour workstreams; per-commit history is in git, and each feature below
names its commit). The historical five-mode
comparison (Retail, Retail+, Flowfield, Cooperative, Legion) and its tables are
in `docs/navigation-comparison-2026-10-05.md`; they are not repeated here.

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

Potentials are 32-bit (`kUnreached`, all ones, means no route). Until the
2026-10-14 review they were 16-bit and saturated at 65,535, about 13,100
orthogonal steps from the nearest seed: a body farther than that along a
connected route never got a finite potential and stood as terrain-trapped
(section 4). `serplong.scn` is that case: two bodies at the head of a 30-lane
serpentine at potential 87,356 stood trapped, and now walk it and arrive
(about 168,000 ticks; Retail's orders complete 408 cells short).
`serplong-control.scn`, the same shape with 16 lanes (46,560), stays inside
the old range, and like every other existing fixture its hashes did not change
(`legion_longroute` checks the long case on every commit). The cell budget
below is unchanged, so a field costs 4 bytes a cell instead of 2: the 8-AI
Ulasem benchmark replay fills the budget (19.7 million cells on its 640 x 640
plane), and its peak heap grew by 39 MB, from 469 to 508 MB.

Field work runs at the start of `World::tick` under a deterministic quota of
384k relaxations per tick (`kFieldQuota`), shared by plane debt and all fields
in group-id order. A refresh of a group that already steers by a finished
field gets a quarter of it (`kRefreshQuota`; see "Refresh allowance"). A field resumes across ticks until done. Groups with no field start
before stale refreshes of older groups; served the other way round, constant
churn restarted the refreshes every tick and starved new groups.

A field covers only the bounding box of its seeds' static region, the only
area it can ever reach; cells outside read as unreachable. Live fields
(refreshes in progress included) are limited by total cells, equal to 48
whole-map fields, and by a count of 1,024. A field that has served its group
for 300 ticks can be displaced, oldest build first. Before regions bounded
fields, 1,000 single-unit orders each in its own walled pen competed for 48
whole-map slots: 633 evictions, 623 of 1,000 settled and 222 MB of fields.
Bounded, all 1,000 settle by tick 548 with no evictions and 2.3 MB (both
measured with 16-bit potentials, which now take twice the memory).
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

Progress is the integer distance still to go, in one measure at a time: the
field potential (x64) where the group's field reaches the body's cell above
its seeds, else squared cells to its own goal (the field does not reach the
cell yet, a bounded field's outside, a seed). Only a gain in the same measure,
on the same field, is progress: it restarts the stall clock that the arrival
on contact, the settle rule and B1's blocked re-request read (and the wake of
a rested held body, heldRest). A new measure (the field reaches the body, a
refresh swaps in, the body leaves a seed) re-baselines progress without
counting a gain: the stall clock runs on, so a body blocked with no field
still makes B1's re-request. A first step onto one of the field's seeds is a
gain on the field (potential 0, below the field's best so far) and counts, as
it does on the headway clock. Until 2026-10-14 the two measures were compared
as one number: a body 200 cells out that registered before its field arrived
read 40,000 squared cells, then 64,000 on the field, and made no progress
until it was within 125 cells, so its stall clock aged through most of a free
walk while its headway clock advanced (the observer's `stall_stale_ticks`:
9,401 member updates on `battle-field-2x60` at offset 0; now 0 on every
fixture at every gate offset; `legion_progressclock`).

The detour rules keep the record they were tuned on (`Member::nearest`): the
least distance still to go the body has read since its order began or it was
re-seated, in whichever measure it read, never re-based -- what `progress`
was until then, mix included. A new record restarts the detour back-off, and
the yield-first rule wants none since the last detour. Restarted on every
gain of the stall clock instead, `battle-assault`'s melee planned 11% more
detours (reversals +5%, Legion work +0.5%) for 2.3% fewer standing samples:
its Fight members register before their field, so their record stood still
through the melee and the back-off reached its 480-update wait, while the
stall clock restarts it with every creep. That trade is a retune of its own
(Known weaknesses). With one reader at a time on the new clock and the rest
on a shadow of the old one (the fixtures that move at all, every gate
offset), the arrival on contact decides the same everywhere, B1's re-request
fires less (battle-assault 103 to 57 over the offsets, its only move), the
held-body wake moves work accounting only, and the settle rule's stall gate
moves twelve fixtures at a few offsets each, both ways. Summed over the scenario set (133 fixtures, Legion, every gate
offset): arrivals 120,807 to 120,809, complete 132,107 to 132,118, left
behind 56,351 to 56,349, never 3,706 to 3,707, lane crossings 7,117 to 7,108,
stopped permille 288,524 to 288,538, Legion work flat.

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
  nearest reachable point. The walk is never cut short: its only limit is the
  5-minute age test (9000 ticks from the first time this order's goal was
  found unreachable), which bounds a walk that never arrives. On arrival or
  contact it enters the Trapped state (zero speed, constant heading) for one
  update, and then its order is dropped (`kApproachRetire = 0`, timed from the
  arrival; protocol 240): an unreachable order ends as soon as the unit
  reaches the nearest spot it can, and queued legs continue. A unit queued
  behind its own army is treated the same way (the holder rule): once it has
  gained nothing on its point for 12 ticks (`kApproachSettle`) and is held,
  it looks at the one-body ring round its footprint (staggered over 4 ticks
  by id) for a body of its own selection (same player and click, or one click
  the uplink split into several commands) that already stopped nearer the
  point -- an approach member just arrived there, or one whose approach
  order already dropped. If one is there, the spot it stands on is its
  nearest reachable spot, and its order drops the same way. A member still
  gaining, or one blocked only by other selections or by its own army still
  walking, never settles. Genuinely terrain-trapped orders (above) keep their
  5-minute wait. A static epoch change while the member still walks
  re-registers it only if its stand-in no longer holds (`approachValid`): the
  body left the point's region, the point became illegal or unreachable, or
  the real goal became illegal or joined the body's region. A gate that opens
  after the order dropped does not move the unit (the order is gone).
  `legion_approachhold`, `legion_approachopen`, `legion_approachchurn`,
  `legion_unreachable_orders` and the `unreach-200` / `mazeapproach`
  scenarios cover dropping at the point, the queue behind it, resuming when
  the wall goes during the walk, churn that must not reset the clock, and a
  ~8200-tick maze walk that must never be stopped mid-route.
* **Crowd-blocked**: a route exists but bodies occupy it. The unit first
  **flows around** the blockage by stepping to any free legal neighbour that
  strictly reduces its distance still to go. The step is committed until the
  new cell is entered. It moves only along the axis whose origin changes,
  never back toward a cell centre, and it does not turn the body; this is a
  shuffle. Speed follows the facing cap, except that a formation member on a
  move-like leg walks the shuffle at travel speed (`Member::detourFlow`, since
  2026-10-19: "Flow-around at travel speed" below). A flat half-speed shuffle was
  measured to gridlock opposing columns, and at travel speed two allied armies
  meeting head on knot as well, so a member whose point is in a head-on meet, a
  body admitted through a narrow passage, a body that has gained nothing for a
  crowd window, and fight, attack, guard and patrol legs keep the cap. If no such neighbour exists the unit
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
  cell by cell without turning the body: at travel speed while 4 or more of
  its cells are left (`kBriskRoute`; a move-like order only, a reach kind's
  detour round its own ring keeps the shuffle), otherwise at the unturned
  facing cap (an eighth of travel speed). It is attempted only when the
  blocking body will not move by itself (idle, arrived, trapped, or not a
  Legion mover), or when a formation member has been held for 60 updates in
  a standing jam of crossing lanes; never for a queue of members waiting on
  each other. Attempts back off geometrically (30, 60, ... 480 updates) while
  the body sets no new record of its distance to go (the detour rules'
  record, above).
* **Corridor mouths one body wide** (Legion ~100 round 3, 2026-10-20; "Corridor
  mouths and own-slot arrivals" below): a member beside a passage one origin
  wide waits for its way (`passageWay`, `updateClaim`), and no other member of
  its group may step onto an origin covering that way that is not past it; two
  that reach it at once ask the one farther up the field to step clear
  (`giveWay`). Retail has neither.
* **Settled arrivals yield**: when a unit completes, Legion records an anchor
  (the origin it completed on). A member held for 12 or more updates, with no
  detour or route, whose wanted cell is held only by settled, idle,
  friendly anchored bodies (its own player's or an ally's, never an enemy's),
  asks those bodies to step one cell aside; if
  that fails, it tries the same for a neighbour nearer its own goal. Every
  blocker must find a target or none moves. A target is within one cell
  (Chebyshev) of the blocker's anchor, legal for its footprint, clear of the
  requesting member's current and wanted footprints and of targets already
  chosen in the same request, and, for a formation arrival, inside the
  point's area and off cells other members claimed there. Yields run in
  `tick()` every tick, in id order: a straight step at ground speed without
  turning, ended by arrival, a refused step or 45 ticks. Each body yields at
  most 3 times. An anchor is forgotten when the unit dies or gets any new
  order (by event since W4: see "Upkeep on demand").
* **Heading**: the body turns only on a committed step toward its route,
  with a 5.6° dead band. A refused step, a hold, a shuffle, a pass, a
  side-step, a detour route, a yield and a trapped stop never turn it.
* **Arrival on contact**: once a unit has made no progress for 20 ticks, it
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
  that gets no cell (`slot=-2`) walks to the point and looks again only by the
  settle rule's re-choice. If fewer than two members can take part at the first attempt, the
  point retries every 16 ticks. Approach members take no slot and do not size
  the area.
* **Slots by arrival** (`entrance`, `enterArea`; 2026-10-14 review, item 4):
  the front-first order is the straight line's, not the way's. A narrowing on
  the way reorders the crowd (the bodies off the door's axis come through it
  last), and a room whose door is on its far side turns the order over: the
  first bodies through the door are the ones the formation gave the slots
  beside it, so they settle in the doorway and the rest of the order settles
  outside (`farroom.scn`: 22 to 37 of 60 outside at every gate offset). The
  order is not even front-first in practice for a click the server applies in
  one tick: the move mission's reset re-registers every member on its first
  update, just after the formation is taken, and each re-claims its own
  offset in update order. So a formation takes an *entrance* when its way in
  narrows just before the area. Open ground is told without a search: eight
  field reads round the point, `kEntryNear` (24) cells beyond the area's
  radius, each within a quarter of its octile distance, mean no wall near the
  area. Otherwise the descent from the member nearest the centroid is walked
  to its first cell inside the area, and the passage gate's cross-section
  (`gateSpan`) is read 24 chain cells either side of it: the narrowest must be
  under the area's radius, the run round it under twice that width must open
  out again within the probes (a closed dead-end corridor never does), and
  the area may not reach back past it into open ground (`doorplug-112`, whose
  area spills through the door, takes none). That first cell inside is the
  entrance; the search counts with the passage gate's (`lane_work`). A member
  that then comes through (its potential within `kEntryCells`, 12 cells, of
  the entrance's, after it was seen outside) takes the deepest slot that a
  member still outside holds -- farthest from the entrance, with 2 px of depth
  charged for each px the slot lies across the entrance-to-point axis from the
  body -- when that is deeper and scores above its own slot; the two exchange
  slots (one footprint and one movement plane only: a ground body and a
  hovercraft of one footprint share the point, not their slots, which are
  legal on the plane they were found on; a body already through when it got
  its slot keeps it). The slots fill from the far side in the order the bodies
  really arrive. Until the exchange checked the plane, a ground body coming
  through farroom's door with hovercraft in its formation took a hovercraft's
  slot in a pond inside the room, illegal for it, and gave it back on its next
  update (`ctest:legion_entryplanes`: 6 member-ticks on an illegal goal before,
  none now, the last body done at tick 3,409 instead of 3,966).
  The entrance is one footprint's: an origin of the member it was found for,
  on that member's group field. A member of another footprint reads the same
  cell on its own field, an origin whose centre lies (its foot less the
  finder's) x 8 px off on each axis, so in a mixed formation its "through"
  test, and the entrance it measures slot depth from, are that far off (a 4x4
  behind a 2x2's entrance: one cell, against the 12-cell margin); since only
  bodies of one footprint exchange, no slot moves across footprints. No fixture takes an entrance with
  mixed footprints (each of the 19 that take one, and `farroom`, has a single
  unit type).
  `farroom` completes at 9 of the 11 gate offsets (60 of 60 in at 9), where it
  never completed; over the existing fixtures it changes the 19 that take an
  entrance (the twelve dead-end rooms, `doorplug-124`, the six straits) and no
  other: 127 more bodies arrive, 7 fewer keys read never, stopped and held
  permille fall by 299 and 275 and crossings by 4, and Legion work rises by
  346,018 (0.02%: the bodies walk in to their slots instead of settling in
  the doorway). `doorplug-124` A completes at all eleven offsets (never at two
  before), and `strait-3x80`'s median arrivals rise from 48 to 80 (all 80
  through at offsets 0 and +-1).
* **Straight lanes**: a member with a formation slot probes the direct line
  out to 640 cells instead of 160, so the crowd does not funnel into a file
  by descending the shared field.
* **Settling** (`settleWindow`, PLAN 3.1 T1-B, one rule): once per 45-tick
  window, a body that gained less than half a body on its point over the
  window looks at the bodies touching it (a ring of at most 64 cells) that
  stand nearer the point: by the group field, a body of its own footprint at
  the potential of that body's origin (by pixel distance where the field does
  not reach it), a body of another footprint when the field reaches one of the
  waiting body's origins centred on it below the waiting body's own potential
  (a body the field reaches nowhere under it never counts). Until the
  2026-10-14 review the other footprint was read at its centre converted with
  the waiting body's footprint: half a cell off where the parities differ, and
  in the middle of a big body, which read a step above a 2x2 or 3x3 it held
  while its cells reached farther down the field, so that body never queued
  and its order never ended (`legion_mixedsettle`; mixedfootprints 500x4 left
  three such orders open for good, the parked W9 A1 attempt two in 200x1).
  Since round 3 (2026-10-20) a settled arrival of the point whose cells cover
  the waiting body's own slot counts as nearer, whatever its footprint: read at
  its origin, a body of its own footprint settled a step off its own slot,
  uphill, never did (`legion_mixedrandom`). It
  is *queued* when one of them is a settled arrival
  of its own order at that point, an idle body of the player, or a held member
  of the same point that is itself queued, or when (a shared point) it stands
  inside the destination area itself: the chain can only start at the
  destination's own crowd, so a jam on the way never queues. A settled crowd
  of an earlier order, or idle bodies, settles it in one window within the
  crowd's reach (four packed-disc radii of everyone settled there plus two
  bodies). Otherwise a queued body that is *pressed* (no free neighbour nearer
  the point, or no gain at all for a window) first **re-chooses** a slot: the
  free slot it can still reach past the bodies standing now (BFS radius 24)
  with the lowest potential below its own, ties to the lowest cell, then the
  lowest slot index; at most 3 times per member (`rechoose`), and never while
  it stands on its own slot (a re-choice from there walked a body across the
  front of its own formation for a lower cell). Then it settles
  where it stands once its field potential is within `settleP`, which starts
  at the destination area's bound (the potential of the last of the
  ceil(1.25n) footprints nearest the point, so a half-disc at a wall and a
  strip in a dead end) plus two bodies, a body plus 4 px for a distinct goal,
  and grows by Retail's foot*32 px per window. No settle is farther from the
  click than twice the crowd's reach, the bound of the old far rule (C29),
  unless the body stands within one row (two bodies of potential) behind a
  settled arrival of its own command (an arrival of another footprint at the
  middle of the lowest and highest potential of the origins centred on it,
  its centre without the half-cell bias): the cap is measured along a queue that
  is contiguous with the settled crowd, so a dead-end queue settles back from
  the crowd row by row however long it is, and a jam the settled crowd does
  not reach never does. Every settle is field-connected and outside any
  factory exit lane. A member on a distinct goal also arrives when pressed
  within a body of its goal and the goal is taken (`contactArrival`). There are
  no other time-outs: the 300/600/1800-tick waits are gone. The every-20-held
  re-choice is gone too; what remains of it is the lane re-sweep: a held
  formation member re-aims its own lane every 20 held ticks (`laneDue`).
* The older per-goal packed slots (built inside-out by field potential,
  claimed back to front along each member's approach) remain as the fallback
  when no formation applies. Claims skip slots that are now illegal or
  unreachable, so a corpse or building on a slot cannot cause a re-claim
  livelock (`legion_slotblock`). Approach members never claim slots: they
  queue to their stand-in point in the order they come.

**Alt+N formations** keep Legion's pacing rule (`World::tick`, `squad < 0`):
members walk at the slowest member's individual speed and stragglers rejoin at
rest. Only bodies that can walk count (protocol 240): a structure, a unit
still under construction or one with no speed stays in the formation (units
it produces still inherit it) but never sets its centre, area, busy count or
pace, so a building in an Alt+N selection no longer holds the army at a
standstill (`legion_structsquad`, `legion_factorysquad`, `structsquad*.scn`).
The centre sums are integers of the Fixed positions, and the pace is a Fixed.
The rest-time rejoin (W6) uses the formation's ground only (flyers are never
re-ordered) and gathers at P, the modal settled-arrival point of its members,
not at the centroid: a body settled at P is never re-ordered, and another is
sent to P only if it is beyond the settled crowd's reach of P and farther from
P than the centroid is (with no settled member, the centroid rule stands). A
dead-end queue is no longer pulled back on itself (`legion_flyers_deadendrejoin`:
holding 0, no anchored body re-ordered) and a factory's joiner still gathers.

The in-game right-click in Legion mode sends one shared point for ground
units, boats and hovercraft with footprints up to 8 whose order is not
queued behind existing orders. Flyers and queued legs keep Retail's per-unit
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
checksum, the orders' issue ticks that define groups, their convoy ticks and
the open convoys (see "Convoys"), only in Legion mode. Movement and yields run serially. The `legion_determinism` test and the
`navigation_determinism` goldens check that runs are repeatable and that
serial and `--workers` runs produce the same hash. `sweep` uses `__int128`,
which GCC, Clang and MinGW all provide.

## Scope

Supported: surface movers with a footprint of 1..8 whose current leg is one
of the **mission goals** below: ground units (`Domain::Ground`), and, on maps
with a placement plane (every real map), boats (`Domain::Water`) and
hovercraft (`Domain::Hover`). Non-square footprints also need the placement
plane. Flyers stay native: flight is not pathfinding.

**One deliberate flight difference: Legion flyers never land on other
flyers.** Retail's landing-site check `509400` (ported exactly for both
modes, see [pathfinding-port.md](pathfinding-port.md)) lets a group of
flyers ordered to one spot land on top of each other. A flyer that has
begun its descent is still airborne, so it is only in the airborne word
`+2`. The owner of a cell several flyers share there is redrawn every tick.
Eight or more overlapping flyers leave only the `0xffff` overflow value.
`509400` treats both as free, and it accepts an unexplored site without any
test. `tools/re/check_landing_overlap.py` runs this natively: over 512
scenarios, 366 end with landed flyers overlapping. In the user's replay
`legion-r8.takrep`, 176 of 287 landings ended on a landed flyer. All of them
were on explored sites. Nearly all came from stacks of 12 to 30 hovering
flyers whose shared cells had overflowed to `0xffff`.

`World::flightLandingFree` therefore adds `flyerLandingOccupied` in Legion
mode only, after the retail predicate. It refuses a footprint that overlaps
a live, landed flyer (mode 1), or a flyer whose landing mission is in
descent (stage 3), whatever the exploration. Both states are the units' own
hashed state, so no new state is added. The check runs in unit order, so a
simultaneous descent serializes: the later lander sees the earlier one's
stage 3 in the same tick. The refused flyer then runs retail's own
candidate search (`417188..4172ff`) to find another site. Test:
`retail_motion`, where 12 flyers stacked on one point land with no shared
cells (Retail: all 66 pairs overlap). The test also covers 8 flyers
arriving over a landed one, and the same at a site that is unexplored for
them.

**A second deliberate flight difference: flyers stay with a mixed
formation.** Retail (`417c20`, VTOL_Move) runs Move_Ground's two group
checks: a flyer far out of slot around the surface group's centre is sent
back to it by VTOL_Move_Formation (`417750`). A flyer that is ahead of the
centre a second time has a 1-in-4 chance to drop all its orders (`50c480`,
`4d6a50`). Retail never caps a flyer's speed: VTOL missions lack the
`0x2000000` pacing bit, and the group record holds only ground and boat
speeds. A fast flyer therefore shuttles between the goal and the group, or
is stranded where its orders were dropped. This is ported for Retail mode;
see [pathfinding-port.md](pathfinding-port.md). Legion has no group records.
In the user's replay `legion-r9.takrep`, 16 and later 51 flyers in an Alt+1
formation flew at about 4 px/tick against the ground's 0.6. They got up to
5,500 px ahead, landed at the goal, and waited there for up to 2,000 ticks.

`World::planLegionFlightStations` (Legion only) runs every tick. It covers a
flyer in a squad -- an Alt+N formation or (since W6) a Ctrl+N group -- that
has ground members still under way, when the flyer's head order is a plain
move, fight-move or patrol leg with no target or job. Since W6 the flyer is
paired with the ground of its own click: the busy ground members' last orders
fall into up to 4 buckets per squad keyed by (order class, `convoyTick`), and
a flyer whose last order carries the same key keeps station over that bucket
(a 5th click counts `stationOverflow` and flies free). The convoy table puts
a flyer's click, offset up to 60 px per axis, in its ground's convoy, so the
96 px point match and the "any patrol joins any patrol" capture are gone. Two
clicks of one class applied in the same tick share a `convoyTick` and so a
station: a known limit.

- **Station (over the front, user request 2026-10-12).** The flyers ride over
  the front of their ground, not its middle: `legionFrontSlots` lays rows of
  lanes `16 * foot + 16` px apart across the lateral extent of the bucket's
  busy ground members (a member that has arrived waits at its slot and no
  longer counts). A lane holds over the frontmost body within half a spacing
  of it, a body farther out counting that much farther back, so the front is
  continuous in the bodies' positions; the front row fills first, a short last
  row spreads over the whole width, and each further row sits a spacing
  behind. A row has enough lanes that the rows number at most twice the
  lanes: over ground narrower than that (one body, a column one or two wide)
  the row spreads that wide about the ground's centre line, a lane past its
  edge taking the front at that edge, so the flyers stand at most twice as deep
  as wide instead of in a file trailing behind it (8 flyers over one body: 2
  lanes, 4 rows, 192 px deep, where a lane per spacing of front had them in
  one file 448 px deep). Flyers take the rows in order of their distance behind the front
  and each row's lanes in order across, so a flyer in formation keeps its
  station and a turn re-ranks the flyers where they stand. Within a quarter
  spacing of a lane boundary they keep the layout of the lane count across it
  when they stand nearer it, so a formation whose width hovers at a boundary
  does not reshuffle them. With no direction of travel the stations fall back
  to the old square spiral over the ground members' centroid (a squad with
  one bucket: all its ground; with several, each bucket's busy members).
- **Release (W6, FL-04).** When the ground stops, the flyer is released for
  its order and flies it uncapped to land, as retail's VTOL_Move ends:
  release A when no busy member of its bucket has made headway for 450 ticks
  (`LegionNavigator::advancing`; a member with a weapon reloading counts as
  engaged, so a fight does not free the wing), release B when the bucket's
  centroid has stayed within 16 px for 900 ticks with nobody engaged. Both
  are sticky on the flyer's order (`Order::stationFree` and the B reference,
  hashed when set). A dead-end queue, a sealed wall or an unreachable goal no
  longer keeps the wing hovering for minutes (MIXED_SEAL 5149 -> 458 ticks).
- **No yaw on the spot (W6).** A station flyer with no horizontal velocity
  (the vertical take-off at the start of a leg, or held still by the ground's
  pace) does not turn toward its lead point until it moves.
- **Flight (formation-air2).** The flyer does not steer at the station
  itself. A point destination puts retail's flight model in its arrival
  regime. There the navigator keeps its old heading inside 16 px, and the
  body creeps after a target that moves a fraction of a pixel a tick. In
  `legion-r9.takrep` the formation's bats held one heading for a whole leg
  at about 0.45 px/tick and looked parked. Instead, the flyer steers at a
  point 160 px ahead of its station along the ground's direction of travel:
  the sum of the busy members' unit vectors -- the way a moving member steps
  (its heading), toward its current leg for one standing still -- so a
  column turning round a wall turns its front with it. It
  therefore cruises facing its way and turns and banks with the formation.
  Its script animation is untouched; the Zhon bat's `FlightControl`, for
  example, flaps regardless of speed.
- **Speed.** `tickFlightBody` caps the flyer's maximum at the busy ground
  members' mean speed:
  - rising linearly to the flyer's own speed when it is 192 px behind its
    station;
  - falling to zero when it is 128 px ahead;
  - at the flyer's own speed when it is more than 192 px from its station,
    so a joining or left-behind flyer can catch up.

  While travelling, the leg never completes.
- **Arrival (formation-air2; landing in front, 2026-10-12).** Once the
  ground's centroid is within the formation radius
  (`max(256, 32 * sqrt(area))` px) of a station flyer's destination, every
  flyer of the bucket has its order's point moved, once, to a landing point
  in front of the ground's goals (`LegionNavigator::goalPoint`: each busy
  member's formation slot; a squad of one bucket adds its idle ground where
  it stands): the same rows of lanes, `8 * (ground foot + flyer foot) + 16`
  px beyond the front and each further row a spacing farther on, clear of the
  slots. `Order::stationFree` 3 marks it. The flyer then flies that order, as
  in retail; the pace cap, measured from its front station but never below a
  quarter of its own speed, stays until it is within 160 px of its point. It
  arrives, completes the order and lands through the retail landing search
  and the Legion overlap refusal -- in front of the formation, where the old
  click point (the middle, give or take the 60 px offset) put it among the
  ground's slots and the landing search scattered the wing round them. The
  first version held flyers over the centroid until every ground member had
  finished, so they hovered while stragglers arrived.
- **Determinism.** Everything is derived each tick from hashed state with
  integer arithmetic; the only persistent state is the release (or the moved
  landing point) on the flyer's order, hashed when set.

Flyers ordered on their own, or to another destination, fly as before. Test:
`legion_world_test mixedformation` uses 20 ground bodies and 8 flyers at
4 px/tick, with a wall the ground detours around. Under move, fight-move and
patrol it checks:
- the three leading flyers ride within 80 px of the ground's leading body
  along its way (-19 to -49 px; the centroid spiral -187 to -241) and the
  flyers' mean is 60 px or more ahead of the ground's centroid (108 to 154;
  the spiral -18 to -24);
- an airborne flyer stays within 240 px of a ground body, and none lands with
  a ground body ahead of it in its lane;
- flyers keep the ground's pace in every 30-tick window (0.97 to 0.99 of
  the centroid's distance);
- flyers face the way they move (1-2% of moving ticks off by more than
  30 degrees, against 56-66% before formation-air2);
- no hover longer than the 18-tick arrival brake (before: up to 654 ticks);
- the landing descent starts in the tick the order ends.

`legion_world_test flyerfront` re-faces a 10 x 3 block of 30 bodies with 12
flyers (more than one row of lanes) from east to south mid-move: on both
headings the leading flyers ride over the leading body, east the flyers span
the full width with the rest in the second row, all land in front, and an
air-only formation still lands at its own points. It also moves 8 flyers with
one body and with columns 1 and 2 bodies wide and 10 deep: the leading flyers
ride over the leading body, at most 4 rows deep (193 to 220 px; the file of
one lane per spacing 448 to 455 px), none behind a column's middle.

**A third deliberate flight difference: idle landed flyers make way for an
allied ground group.** Retail stamps a landed flyer into the ground word
(`5066f0`), so every ground mover plans and steers round it as round any
standing body, and Legion does the same (the `grounded` overlay and the soft
obstacles). A parked air wing on a group's way therefore made the group
detour round it or file past it. In Legion, every 4 ticks (staggered by unit
id) each member still on its way looks 12 cells ahead along its planned
steps: its committed detour route, else the descent of its group's field,
else the straight line to its goal. Any landed flyer whose footprint those
steps cover is asked to lift (`World::requestLegionLift`) if it is idle (no
order, job, build queue, cargo, landing or retained flight controller) and
belongs to an allied player. Enemy flyers and busy flyers stay obstacles.
- **Take-off and hover:** the flyer takes off as VTOL_Move's stage 0 does
  (`416c50`: Activate, BeginFlight) and holds over the spot it left at
  cruise altitude through the retail flight kernel (`retailFlightNavigation`
  and `tickFlightBody`). The ground then walks under it: an airborne flyer
  is not in the ground word.
- **Planning:** the soft-obstacle scan stamps an idle landed flyer as kind 3.
  That kind is no obstacle to the fields and lines of an allied player's
  commands, so the group does not detour for flyers that will lift. It is
  still an obstacle to everyone else's.
- **Hysteresis:** every further request extends the hover by 90 ticks. So
  does any allied member within 6 cells of the flyer that is making headway
  (W6: a new best on its field within 120 ticks), or that is bound for a goal
  there and moving or held for under 60 ticks. A member that is not making
  headway asks only for a flyer on its next 2 planned cells. The flyer lands
  only into a settled area, not in front of the stragglers of a group that is
  still coming in. When the 90 ticks run out, the retail landing mission
  takes over and searches from the spot it holds over: the flyer lands on its
  own spot if that is free, else on the nearest free site. It cannot lift
  again for 240 ticks, which prevents bobbing up and down.
- **Cap (W6, 60 s):** an episode lasts at most 1800 ticks, the quiet period
  included, unless a member is blocked by the flyer itself (it covers the
  member's next 2 planned cells, and the member is advancing or only briefly
  held). A capped episode rests 600 ticks once landed.
- **Combat (W6):** a lifted flyer polls for a target every 8 ticks (staggered
  by id), the call the landed VTOL standby makes; a target ends the lift and
  it fights.
- **Own squad:** a squad's landed flyer does not lift for a member of its own
  squad that is making headway (it settles beside it); it does for a stalled
  one whose goal or next 2 planned cells it covers.
- **Determinism:** the lift state (`Unit::legionLift`, its spot, expiry and
  start tick, and the rest tick) is hashed. The overlays are rebuilt each
  tick from hashed state, and members are visited in id order.
- **Descent (W6, FL-03):** a flyer in the landing mission's stage 3 is
  stamped on its touchdown footprint as a landed one, so no member steps in
  under it while it comes down; a body already there may only step out.

Test: `legion_world_test liftflyers`. 40 ground bodies cross a block of 12
landed flyers: 4 columns by 3 rows, with a one-cell gap between them.
- With the player's own flyers, all 12 lift once. The group arrives in 1,740
  ticks (1,802 on open ground) with no sideways detour, and every flyer lands
  again on its own spot.
- Enemy flyers stay landed. The group goes round them in 2,226 ticks, with a
  7-cell detour.
- Flyers on guard orders are never asked to lift.
- With the flyers parked on the group's destination, 9 flyers lift once and
  land nearby after the group has settled, with no bobbing and no overlap.

**A deliberate animation difference: no walking on the spot.** A walker's
gait is a Create-started COB controller (araarch's `MoveWatcher` and
`MeleeControl`). Every 100 ms it reads GET 29, the horizontal speed as a
percentage of the maximum, and above 5 it plays a whole walk cycle, about
1.3 s. Retail reports a body stopped only after two refused steps in a row
(the refusal bit, `bodyBlockStreak`). Legion holds a blocked body at zero
speed, but it moves a jammed body in short, slow shuffles and side-steps
that do not turn it: an eighth of its speed while it faces away. Every
sample that lands on one of those creeping ticks starts another full cycle,
so the legs walk while the body all but stands.

In Legion, `World::updateLegionStill` therefore measures each ground body's
NET headway over a window of 8 ticks (`LegionHeadway`), from where the window
began. A window that ends without a displacement of 8 paces (a pace is a
quarter of the base speed: two full-speed steps) or a turn one way of 4 ticks
at the full turn rate makes it report stopped: GET 29 is 0 and MoveRate is 0,
as with the refusal bit, so it stands, and it requests no turn of its scripts
(`turnReqBam` 0, so the display VM sends no `TurnDirection`). The tick its
headway reaches either mark it walks again (a body pivoting on the spot steps
round). Until protocol 257 the rule was per tick: six ticks in a row under a
pace and without turning made a body stand, and two steps of a pace or any
turn made it walk. A body stepping back and forth over a cell edge, or
turning left and right toward two aims that swap, passed that on every tick
and walked on the spot for good (the 0.8.1 walk-in-place, "Livelock" below);
by net headway it gains nothing. A window that stands although the body
walked a whole reach in it, or turned a whole turn, three times what that
came to, also latches a livelock (`legionShuffle`): see "Livelock" below.
Flyers and Retail mode keep retail's rule.

The rule applies to every Legion ground body, whatever blocks it: another
body of any player (held, waiting, queued behind, pressed against, a crowd)
or terrain. Since protocol 257 its state is hashed for every Legion ground
unit (the navigator reads the livelock); before, only for units with a COB
script.

Measured as unit-ticks in which the body "walks" while it moves less than
8 px in 30 ticks. "Walks" means that a `walk*` script runs, or, where there
are no scripts (crowdbench), that the GET 29 signal the gait reads is above
5. Each tick is classed by contact: another body in the ring of cells round
the footprint, else terrain (a neighbouring origin the static grid refuses),
else other.
- The replay `legion-r9.takrep` on 02aa55a, where it replays exactly, to
  tick 65,000, scripts: unit contact 720,663 to 92,697, terrain 1,011 to 311,
  other 24,734 to 2,774.
- crowdbench, Legion, 6,000 ticks, seed 0, GET 29 signal, unit contact:
  doors 2000x1 804,090 to 47,904; sharedgoal 2000x1 511,179 to 18,774;
  opposingcolumns 2000x1 43,079 to 18,543; 500x4 617,889 to 55,443,
  398,296 to 10,755 and 121,452 to 30,926. Terrain-only contact is rare
  there: maze 200x1 38 to 12, jagged 10 to 6.
- `legion_group_motion`, 60 units, scripts: open 4,022 to 143, wall 2,564
  to 244, cross 1,597 to 158 (Retail: 405, 1,087 and 722).

Movement is unchanged, except through the game's single random stream:
walk scripts draw from it, so different walk timing shifts later draws.
The crowdbench screens are identical, because crowdbench has no scripts.

**A second, display-only difference: gait pace follows the distance
covered.** A body that is still reported moving can travel well under its
maximum: Legion caps a body that does not face its way at half speed (an
eighth past 67 degrees), and speed-matched following holds it down to half.
GET 29 stays above 5, so retail's keyframe walk played at full pace. In
corner-8x30, 16% of the moving-state unit-ticks travel at half speed (unit 9
holds it for 8 s round the corner). The client (`src/client/gaitpace.h`,
`GameView::captureUnit`) now measures each Legion ground body's distance per
tick against its terrain-adjusted maximum (90% and over counts as full pace),
smooths it (a quarter of the way per tick down, half of the way per tick back
up), and hands it to the display VM (`cob::Vm::setGaitRate`). Only the threads
playing a gait script (`cob::gaitCode`: `walk*`, `def_walk`, `swim*`,
`stumble*`, `row`/`slowrow`/`fastrow`, `drive`, `wheel_*`,
`SuperDynamicWheelSpinner`) and the piece motion they started step on that
fraction of the 30 Hz ticks. Controllers polling GET 29, aim, fire and build
threads keep full time. A body that reports stopped snaps back to full pace,
so its cycle plays out its static-gated frames and restores as before; so does
a body that dies (the pace is set before the death branch), or ten shipped
types (`vermage`, `zonkrak`, `npcwagon`, ...) kept a half-played stride through
`Dying`. Flyers and Retail mode always run at full pace. The authoritative
COB VM in `World` is untouched, so the state hash and every position, path and order are the
same: the `--mpai` Inner Circle hashes are unchanged in both modes. Debug
clients take `TAK_GAIT_LOG=<csv>` (per ground mover per tick: distance,
maximum, GET 29, refusal, pace) and `TAK_GAIT_OFF=1` (pace held at 1).

**Boats and hovercraft** are mobility classes like any other. The plane is
built from `World::mobilePlacement`'s own predicate with the type's limits:
a boat needs `minWaterDepth` (default 13) under every footprint cell and has
no maximum depth; a hovercraft has no depth limit either way and uses
`maxWaterSlope` over wet cells and `maxSlope` over dry ones. So a keel never
enters a shelf, a beach or a river mouth too shallow for it, and a hovercraft
crosses a shoreline wherever the slope allows. Everything else is shared with
ground units unchanged: incremental plane updates (piers, docks and their
yards), static components (two seas joined only by a strait are one
component; a lake is its own), goal resolution (a boat ordered onto land
stops at the nearest water it can float in, or approaches the nearest
reachable point of its own sea), fields, formation slots, settling,
following and passage lanes. Groups are per plane, so a mixed selection of
ground units, boats and hovercraft ordered to one point forms one group per
class sharing the point's area. Legacy terrain-only test worlds key their
nav-grid plane by footprint alone and cannot tell a keel from a foot, so
there boats and hovercraft stay Retail. Tests: `legion_navalclearance`
(plane equals `mobilePlacement` for keels of depth 4/13/30, footprints 2..4
and 3x2, and hovercraft with two water-slope limits, on an archipelago with
shelves, a strait, a river mouth and a beach; then 240 steps of piers,
buoys and docks with yards, incremental plane equal to a rebuild),
`legion_navalisland` (fleets of 12 3x3, 8 4x4 and 24 2x2 boats round an
island and its shelf and through a 10-cell strait: all arrive, legal every
tick, no spin, serial == workers), `legion_hovershore` (ten hovercraft from
land down a beach to sea and back up another beach; boats up a narrow river
mouth; boats ordered onto the beach stop at the water's edge) and
`legion_navalmissions` (boat patrol laps round the island, fight-move across
it, an attack approach and a guard escort, each served by Legion).

The mission goals (`LegionMission`):

| Kind | Legs | Goal | Ends by |
|---|---|---|---|
| Move | plain Move legs, group right-clicks, queued legs | the point | Legion arrival raises `0x500` |
| Fight | fight-move (`attackMove`) legs | the point, shared area per command | Legion arrival, or the native circle (`groundMissionAccepts`), raises `0x500` |
| Patrol | patrol legs, single or group | the waypoint, shared area per command | as Fight; the body passes through (no anchor, cells released) |
| Attack | explicit and auto-acquired attacks, chases | the target unit, re-seeded | combat: `tickCombat` stops the body in range; Legion never completes it |
| Guard | guard/follow | the guarded unit, re-seeded | guard: stops within 70 px; Legion never completes it |
| Build | MobileBuild approaches (`buildRectangle`): single, queued, mana build area | the rectangle's `navigationCell`, else its nearest legal reachable perimeter origin | the rectangle itself (`accepts`): `0x500`; anywhere else `0x200` |
| Repair | repair legs, and a patrolling builder's automatic repair detour (W2 RB-04) | the target's point | work: `tickRepair` starts at reach; Legion never completes it |
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
  by (kind, target id) instead of a controller, and the group by (player,
  plane, kind, target id, reach bucket, the target origin it was seeded at).
  On a 16-tick grid line, once the target's origin has moved
  max(2, min(D/8, 8)) cells (D: the member's Chebyshev distance to it), the
  group re-seeds in place (W5, AR-07): its seed or its ring moves, a refresh
  builds the new field while the old one steers, and its members keep their
  group, slot and clocks. See "Reach rings and engaged bodies (W5)".
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
A patrolling builder's automatic repair detour (`patrolRepair`) is an
ordinary Repair leg (W2 RB-04; it was delegated to Retail before). The leash
stays in `tickPatrolRepair`/`tickRepair`; when a mobile ally moves,
`tickRepair` refreshes the leg's point and its `cancelPath` re-registers the
member, so Legion follows it (`legion_patrolrepair`, `-moving` variant).
Boats and hovercraft are routed too (see above). Still delegated to Retail:
transport legs of flying or water carriers, and boats/hovercraft on legacy
terrain-only worlds. Flyers stay native: flight is not pathfinding. The
interface for adding
them is a `LegionMission` kind plus a `Policy` entry (see `legion.cpp`).
Retail behaviour is bit-identical to builds without Legion: per-tick traces
and hashes compared on each Legion change (Retail+ was also compared while it
existed).

The former corner hold at a wall's end (a single body or a 12-body column
on a legacy nav-grid world, and the jammed rally column of the generated-maze
Troll cohort, 64 of 100 unsettled) was the one-update corner step described
in section 3; it is fixed for every plane. The maze cohort's last 17 bodies
then stood pressed against the west face of a rally crowd that had grown
toward them, just past the "close enough" reach; they now settle there after
1800 still ticks (section 5). All 100 settle (tick 32,966), serial ==
workers (`legion_movement_orders_trolls_maze`).

## Tests

* `legion_world_test`, 38 cases: clearance, groupreuse, jagged, trapped,
  crowdhold, replace, unreachable, quota, pens, determinism, formation,
  slotblock, deaths, deathsshared, splitgoal, farclick, churn, approachhold,
  approachopen, churnfield, approachchurn, legacyyield, planeincremental,
  planeprebuild, penstale, lattice, wallend (one body and a 12-body column
  round a `nav().block` wall's end on a legacy plane, without turning in
  place), staticblock, squadformation, pinwheel, landedflyers, mixedformation,
  liftflyers and aware. All pass. Boats and hovercraft: navalclearance,
  navalisland, hovershore, navalmissions (see "Scope"). Their metrics come
  from the shared observer (see "Instruments"). Since protocol 257 also
  lonetrees, slideback, stillturn and forestedge, and `legion_still_test` for the
  headway window alone (see "Livelock").
* `legion_acceptance_test`, five checks run in all five modes; Legion
  asserts, the others report. singleunit, jagged, trapped and group pass for
  Legion; crowdheld is a ratchet (61 of 64 in goal or better, below).
* `navigation_determinism` and `legion_determinism`.
* `convoy_test`: one convoy per click (see "Convoys").
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
  the 5-minute grace. Approach orders (a reachable stand-in point exists) do
  drop at once on arrival since protocol 240 (the user's rule: an unreachable
  order ends at the nearest reachable spot); terrain-trapped orders keep the
  grace.
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
|---|---|---|---|---|---|---|
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

## Features added after round 4

Each entry names the commit that has the full description and the numbers.

### Pinwheel (2650c3e, protocol 234)

A formation sent round the end of a wall used to fold into one file at the
tip: every shortest way past a convex wall end touches it, so members
descending the shared field converge there whatever their place in the
formation. A formation member of a point with 16 or more members now takes a
rank when a turn of its descent chain beside a wall comes into its 48-cell
look-ahead. Its side offset from the group's live centroid across the way to the
turn, counted from the inner side (the inner file hugs as before), makes its
descent chain beside the wall, offset outward by that radius, its own
concentric arc round the end. It pursues the first arc point 6 or more cells
ahead that is in a straight legal line, and when blocked flows round toward it
rather than down the field. The arc shrinks to the free width beside the wall;
the look-ahead stops at passages (the passage lanes keep single file) and
within 32 cells of the destination (the area logic places the slots). The
group's field window gets the arc margin. Ranks and the aim memo are hashed;
the work is bounded. Seen in `corner-1x48:gate.top.files_x100` (files abreast
at the corner), `uturn:gate.top.files_x100` and the `pinwheel` world test.
Since protocol 257 a lone tree or rock is no wall end (see "Livelock" below).
Since Legion ~100 round 4 (2026-10-20) a part of 32 or more on a move-like leg
takes its radius from the turn's ribbon instead of the centroid rank, holds its
arc through the turn and ranks again at the next one (see "Lane order through
turns: the ribbon").

### Passage gate (W8, protocol 247)

A group used to take a short gap in single file even where the gap was wide
enough for two or three bodies, because every member descended the shared field
into the same cell column. A formation member of a point part of 16 or more (the
pinwheel's eligibility) now commits to a gate when a short passage comes within
12 cells of its descent chain. A passage is a strip narrower than the formation's
band (`B = 2*s0 + (K-1)*(foot+1) + foot`, `K` = clamp(3/4 of the square root of
the point's members, 2, 10)) and than the passage lane grid, two to 24 chain
cells long and not within the lane-near distance of the destination; the gate is
the wall-to-wall cross-section at its narrowest cell. The member picks the gate
point (points at footprint pitch, centred, as many as the free width holds) with
the fewest same-player bodies queued within 6 cells before the line, aims at it
while it is in a straight legal line, and is released once it crosses the gate
line. A gap of two or three bodies is therefore taken two or three abreast
(`gap6`: files 1.31 to 2.61, time to 90% 2972 to 2395 ticks). The static
geometry is memoised per plane epoch (`nearObstacle` and its vertex probe, which
steering does not read); soft narrowing is computed at use. The pinwheel is kept
and its look-ahead still ends at the old narrow cut; members committed to a gate
skip it. Hashed only when set: `gateCell`, `gateLane`, `gateAxis`, `gateSign`.
`gate_commits` / `gate_releases`, the `pivot_*`, `lane_*` and `vertex_*` work
counters and `pass_filter_holds` are instruments and never hashed.

The cost is lane swaps after the gap: bodies arriving abreast are turned by the
field at slightly different times, which the lane-crossing observer counts as a
flip where single file never does. User decision W8-1 judges the GAP fixtures
(`gap6`, `gap8`, `gap10`, `gap14`, `gap20`, `gapsweep`) per file, so
`legion_check` compares legion crossings / files abreast with Retail's
crossings / files abreast (`tools/legion_check.py`, `per_file_floor`); the corner
and head-on obligations are unchanged and still raw. Corner lanes (a rank
re-map at a turn), wake-following, a pass release and a door tie-break were
tried at W8 and dropped by their stop rules; the lane-crossing exceptions on the
corner and head-on fixtures, `attackring-wall-64`, `ringcross` and the jagged
arrivals moved to W9 (user decision W8-2; `docs/legion-exit-tables.md`, "W8 exit").

### Passage lanes (2026-10-14 review, item 3)

A formation took a short passage several abreast (through the passage gate
above, or because the gap was wide), and the bodies that came through after
the crowd had thinned walked straight for their slots, diagonally across the
strip, through the lanes the earlier bodies had held straight in the press.
That was most of the gap fixtures' lane swaps (`gap8:gate.top.crossings` 74
over the gate offsets, 65 of them at the straight-walk site; `gap14` 181, 150).
A census of the swapped pairs (a debug instrument, not kept): in 84% (`gap8`)
and 86% (`gap14`) of them the later body made the move, 5.1 and 6.2 cells
across inside the gate's 6-cell window, the other 0.9 and 1.6. Slots in the
gap's order would not have kept the lanes: pairs whose slots lay in that order
swapped as often as the rest in `gap8` (1.7% of them against 1.9%), and in
`gap14` 1.7% against 4.4%. So the slot assignment is unchanged, and no lane
record is shared: each body keeps the place it has.

A formation member (a point part of 16 or more, the pinwheel's eligibility;
not on a fight, attack, guard or patrol leg) inside a short passage keeps its
lateral place until its footprint is clear of the far end (`passageLane`): it
aims at the first origin past that end at its own lateral origin, over its
straight walk, the field and the pinwheel, and a blocked step flows round
toward that cell as round an arc point. A gate-committed member aims at its
gate point until it crosses the gate line, then keeps its lane through the
rest of the strip. The strip is read from the passage gate's static
cross-sections at the body's origin: narrower than the formation's band and
than 16 origins (`kPassageSpan`), two bodies abreast or more, open along the
other axis, and cut in a wall (the obstacle on either side runs on for the
strip's width, across the axis or along it: not the room between two rocks, as
on `gen1-route-54772`'s way); a run of 2 to 24 such origins along the axis at
the body's lateral place, with legal origins at both ends; the field's descent
there steps along the axis toward the end of lower potential, past which the
axis goes on for a footprint (not a pocket against a wall face, like the stray
rocks along the corner fixtures' wall); and not within 32 cells of the
destination. Nothing new is stored or hashed; the checks past the first
cross-section count in `lane_work`.

Over the 134 fixtures (Legion, every gate offset, serial equal to workers;
Retail untouched) 15 change: `gap6`, `gap8`, `gap10`, `gap14`, `gapsweep`, the
six straits, `mixed2`, `doorform`, `doorplug-124` and `mazeform`. Summed: lane
swaps 566 to 433, the gap fixtures' per-file swaps (x100) 2,723 to 596
(`gap6`, `gap8`, `gap10` and `gap14` none: the W8-1 floor is met), arrivals
+4, left behind -4, never and complete orders unchanged, stopped and held
permille -60 and -88, Legion work +0.005%.
`strait-2x150:gate.strait.crossings` 16 to 3 (Retail 8),
`mixed2:gate.mid.crossings` 46 to 8. On `gap14` t90 2,162 to 2,072 ticks, done
3,062 to 2,299, rounded90 1,333 to 1,128. Losses: `gap8:reversals` 6 to 11 and
`gap8:back` 54 to 81, at the destination, and its t90 2,316 to 2,387;
`gap14:aim_reversals` 86 to 101; `gapsweep:g.G8.done` never at 6 of the 11
offsets (one to four bodies settle outside the disc; `gap8`, the same corridor
alone, now completes at all 11 where it missed 3); `strait-3x48:g.B.done`
3,377 to 4,277. A 20-cell gap is wider than a 48-body formation's band:
`gap20:gate.top.crossings` keeps its 1.73 swaps per file.
`ctest:legion_passagelanes` runs `gap14` over the core five: 17 to 47 swaps on
the parent (median 27), none here.

The lane follows the field, not the member's own slot, and the guard against
the destination is the field's 32 cells. A destination area can reach farther
than that from its point (very large bodies, or a formation of many hundreds),
and then a member inside a strip whose slot lies behind it is led through to
the far end first and placed by the settle rule from there (an automated
review's probe: 64 8x8 bodies and a 22-cell gap). No fixture has such an area
at a passage; a guard of the area's own reach is the fix to try.

### Slot hand-out budget (2026-10-14 review, item 5)

A member's formation slot search (`formationCell`) walks Chebyshev rings round
its target, out to 48 cells: 9,409 cells when nothing in the area is free near
it. The budget of 131,072 ring cells a tick (`kHandOutCells`) bounded only the
hand-out of a point's queue, and only per point. A formation's first 32 slots
and the search of a member without a slot had none, and both run in bursts. A
trace of the parent (a debug probe, not kept): `deadend-w2-n120-closed-plain`
walked 411,965 ring cells in the tick its formation was taken (32 first slots,
then the 21 of them that found no free cell searched again in their own
update); `deadend-w2-n40-closed-plain` 527,351 (its 39 other members registered
again right after its provisional formation, each searching anew); and
`deadend-multi-4x120.scn`, four 120-body clicks to four dead ends in one tick
(the W3 probes `deadend-w2-n120-closed`, `-w4-n120-closed`, `-w4-n120-room` and
`-w6-n120-room` stacked on one map), 602,176: the 64 bodies of one click's second
uplink part searching, and failing, against the provisional formation its first
8 bodies had taken; then up to 402,881 a tick while three points handed out side
by side, each within its own 131,072.

Now one budget a tick covers every slot search. The tick's hand-out runs at its
first formation claim, before any other slot search of that tick
(`serveHandOut`): the queues of every waiting point, round robin in key order,
one member each a round, starting after the point served last the tick before
(`handNext`, hashed while two or more points wait). A point still takes at most
`kSlotsPerTick` (32) a tick, and a search starts only while the budget lasts. A
formation's first burst (`assignFormation`) takes its first slot at once and more
only while the budget lasts; a member without a slot searches while it lasts and
otherwise queues behind the others. The ring search also stops at the ring past
which no cell can lie inside the area: a cell r rings out has its centre at least
(r-1)*16 px from the target, so at least that less the target's distance from
the point, and the area is the point's limit. Its result is the same (the
`TAK_LEGION_VERIFY` square walk agrees), and a search that finds nothing stops
there instead of at ring 48: over the fixtures the formation searches walk 8%
fewer cells (129.1 million to 118.4 million), on the closed dead ends 67% to 86%
fewer.

`deadend-multi-4x120.scn` keeps its hashes at all eleven gate offsets: the
same moves, with its per-tick formation search falling from 602,176 ring cells
to 134,237, its total Legion work per tick (`work.legion_total.max`) from
694,537 to 322,489 and its Legion work over the run by 24% (the formation
search by 74%). Over the 134 other fixtures (Legion, every gate offset, serial
equal to workers; Retail untouched) 130 keep their hashes; 4 change:
`battle-assault`, `battle-field-2x250` and `-2x500` (two armies' formations now
share the budget) and `corner-8x56`. Summed: arrivals +8, complete orders +11,
left behind -1, never -2, lane swaps -1 (per file unchanged), stopped and held
permille -7 and -6, Legion work -0.7%; `walk_net` +34, from `corner-8x56`'s
core-five median (0 to 34; its runs over the nine offsets fall from 1,545 ticks
to 156). The largest per-tick
formation search over the fixtures falls from 527,351 ring cells to 139,991,
and `work.legion_total.max` on `deadend-w2-n40-closed-plain` from 601,062 to
149,758, `corner-8x56` 535,568 to 246,728, `densehead` 450,864 to 218,183 and
`deadend-w2-n120-closed-plain` 415,300 to 135,139; the most over every fixture
from 601,062 to 436,109, a battle tick that the field quota sets. In wall time
(`World::tick`, offset 0, the least of five interleaved runs on a shared core)
the slowest tick of `deadend-w2-n40-closed-plain` falls from 2.11 to 1.15 ms,
`deadend-w2-n120-closed-plain` 1.28 to 1.03 ms (its slowest is now the first
field build, not the formation's) and `deadend-multi-4x120.scn` 6.70 to 5.93
ms, the 99th percentiles within 4%; the battle fixtures stay within 4%
(`battle-field-2x60`, hash-identical, moves as much), and crowdbench `jagged`
2000x1, whose orders form no formation, keeps its hash. Search moved to later
ticks shows in the p99s: `battle-field-2x500`'s slot search 112,908 to 122,317,
`densehead`'s total Legion work 23,132 to 23,191.

The steering probes and detour searches stay unbudgeted: with the hand-out
bounded, the slowest ticks are the field quota's (`work.legion_all.max`, the
core-five median, 385,965 to 472,988 on the battles and the long routes);
awareness, lane and pinwheel work add at most 36,879 cells to a tick's peak
(`battle-assault`), line sweeps (`trace_cells`) at most 64,395 a tick, detours
3,880 and pass scans 12,108.
`ctest:legion_handout` runs `deadend-multi-4x120.scn` at offset 0: 602,176 ring
cells in one tick on the parent, at most 150,000 here (134,621).

### Head-on meets (W9 C1, protocol 248)

Two large columns walking at each other used to meet face to face and shoulder
through one another (`densehead`: 447 lane swaps in the middle gate against
Retail's 208, the groups never completing). A formation Point of 16 or more
members whose mission does not engage enemies (fight, attack, guard and patrol
never latch) now latches a meet (`Point::meet`) in two cases: group awareness
plans a head-on entry for its group (seen), or a member's lane scan
(`passAhead`) finds an oncoming body of another command's point and stamps it
(`Member::meetSeen`, unseen; the stamps are combined per point by a minimum, so
update order does not matter). `meetScan` runs serially once a tick after
`awareScan`, over the cued and latched points only. A latched point's members
narrow into the right half of their own lateral extent, shifted one body
further right, and aim six cells ahead at it (`meetAim`), aim-only: no field
charge, no re-plan, no stored lanes (W9-U2). The target is an increasing map of
each member's current lateral position driven by the group's progress, which
never falls, so members cannot cross each other; they move at most one cell
across per two along, only on a statically legal line that descends the field,
never where the destination area packs the member (within the pivot distance
of its goal and inside its point's area plus one lead, `Point::limit` + 6
cells round the requested point), and the target is cut before a wall. Narrowing waits until the gap between the groups is within twice the
span plus two leads. Both groups keep right. The latch is released when the
other group's tail is behind this group's rear, or after 90 ticks without
contact before the groups meet. Hashed only when set (`Point::meet`,
`Member::meetSeen`); the derived `pendingMeets` / `meetIds` indexes are checked
by `TAK_LEGION_VERIFY`; `meet_latches` is a never-hashed counter. Measured:
`densehead` lane swaps 447 to 39, arrivals 69 / 70 to 197 / 198 of 200,
`aware-unseen` click t90 2162 / 2207 to 1757 / 1792 ticks (Retail 1877 / 1929).
The 96-member awareness horizon (cliff edge) was pre-approved as round 2 and
was not needed. What W9 did not deliver: the step-0 traces refuted or weakened
the premises of the other lanes (A1-A5 slot re-choice and settling, B1-B4 and
B6 gate, ring and corner shaping); each was run for its two rounds, missed its
acceptance and was reverted (`docs/legion-exit-tables.md`, "W9 lane A" and
"W9 exit").

Near both goals (2026-10-19, no protocol change). Until then the band was
off anywhere within the pivot distance (32 cells) of the member's goal. Two
small orders sent to each other's positions (many-100: pairs of 25-body squads
40 cells apart, the second clicked 150 ticks after the first) meet within 32
cells of both goals, so neither group narrowed: the columns met face to face,
the arriving group's front bodies, pressed by the departing group, settled
off-slot at the near edge of their area (after their three re-choices), and the
departing group's last bodies were walled in behind them, some until the end.
Start-up (first move 5-8 ticks) and the head's walk (arrive t50 557 against
Retail's 627) were never the cost; the tail was. The band is now off only where
the area packs the member: within the pivot distance of its goal AND inside
the point's area (`Point::limit`) plus one lead. Large orders, whose areas
reach past 32 cells, keep the old rule. many-100 over the gate offsets
(median, Legion before / after / Retail): arrive.n 93 / 100 / 93, arrive.t90
1986 / 767 / 1057, complete.t90 1981 / 1215 / 1126, stuck at the end 1 / 0 / 0;
every other legion100 fixture is hash-identical except bridges-100, swap-100
and motion-cross-100 (within their spread) and densehead is level
(`ctest:legion_meetnear`).

Departures through an arrived crowd (2026-10-19, no protocol change). After
the near-goal meet fix, an order arriving in an area another order was still
leaving (or had not been sent from yet) settled among the bodies that would
leave, and the last of those were sealed in: many-100 ended with up to 7 stuck
bodies at start offsets 3-5, many-1000 with 30-73. Two causes. Their local
detours round the settled crowd were found (12 cells round, a real gain) but
walked unturned at the facing cap of an eighth of travel speed, so a 12-cell
route outlasted its 240 ticks, was dropped half way and the body shuffled
back, every 300 ticks, for good; and an ally's settled or idle bodies never
yielded or parted for them (only the mover's own player's did). Retail has no
step-aside for ground bodies (BUGGER_OFF is stored and never acted on,
docs/factory-output-2026-10-12.md): parked bodies are impassable and its mover
traces round them. Now a local detour route with 4 or more cells left
(`kBriskRoute`, move-like orders only) keeps travel speed, still unturned, and
the yield and parting rules (one cell round a settled body's anchor, one or
two cells aside for an idle body, each at most 3 times, never a body with an
order, never an enemy's) take friendly bodies of any allied player. Rejected
on the way: turning the body to face the route (sharedgoal 2000 spin 0 ->
3,809 unit-ticks, the round-2 finding again; unturned at speed: 28); a longer
route life at the slow cap (no stuck bodies, complete.t90 +11%); yields and
parting alone, at the slow cap (offset 5 still 6-10 stuck: a mover packed in
a crowd has no neighbour that can move); waiting instead of settling while
another order's mover presses (completions later). Measured (gate offsets,
median [range]): many-100 stuck 0 at every offset (before: up to 7),
arrive.t90 767 -> 677, complete.t90 1215 -> 965 (Retail 1057 / 1126);
many-1000 offsets 0/1/-1 arrive.n 746/833/796 -> 961/956/937, stuck 73/51/36
-> 6/17/6 (`ctest:legion_departure`). The other legion100 fixtures arrive as
many or more (bridges-100 arrive.n 100 at every offset, maze-100 89 -> 95, ringcross-100
38 -> 47); travel reversals rise (corner-1x100 38 -> 81, densehead 77 -> 155,
all still far under Retail's) and doorplug100-112's queue stands more
(stopped 628 -> 733 permille; arrivals 46 -> 47).

### Clustered static repair (4a99092a)

`refreshPlane` repairs component labels in a window round the bounding box of
the changes it is handed. A sync with a corpse in one corner and a building in
another handed it one box spanning most of the map, so each plane paid a near
whole-map flood for a few dozen changed cells. It now splits the changed
rectangles into clusters (boxes grown by the relabel margin plus the footprint
that overlap) and refreshes each cluster as if it had arrived alone, in the
order of each cluster's first rectangle. The labelling stays exact (the
`planeincremental`, `planeprebuild`, `staticblock`, `pens`, `penstale`, `churn`
and `churnfield` world tests). Exercised by
`refreshchurn:work.refresh_completed.total`.

### Shared fields (dec039e)

Commands sent to one point at different ticks (an AI's squads, an attack-move
order split into groups, repeated orders) each built their own whole-map
field. A group starting a field now takes another group's field instead when
it serves it as well: same plane at the current static epoch, same seeds, a
window holding this group's, the same soft rule (the same command unless
neither has settled arrivals of its own), started within one soft scan period.
A finished field comes first, else one still building (advanced once, whoever's
turn it is). Fields are `shared_ptr`; a shared field is never an eviction
victim, and its start tick is hashed. The count of fields served this way is
`battle-field-2x60:work.fields_shared.total`.

### Refresh allowance (b565029)

After a static change in a battle (a wreck, a dead building) every group whose
field it reaches rebuilds its field while still steering by the old one. A
refresh now takes at most a quarter of the field quota per tick
(`kRefreshQuota = kFieldQuota / 4`); first fields, which steer nobody yet, keep
the whole quota. Spread out, a refresh no longer restarts on the next static
change: it keeps building, as a first field does, and is installed still stale
so another follows. A refresh that a sharing group finished is installed
without spending any allowance. The widening of a bounded field that cannot
reach a body restarts as a first field, not as a refresh, so the body does not
steer by a field that cannot reach it. `refreshchurn:work.refresh_completed.total`
counts the installs.

### Group awareness (9254f69)

Opposing groups are aware of each other (when they can see each other) and path
round each other as whole groups, unless they are attacking the other group.
Every 30 ticks each moving formation of 8 or more (members of one command sent
to one point) is a mover: its centroid, spread, and measured velocity. A Legion
group whose way ahead meets a mover plans round it as a whole: its next field
charges the corridor like a soft obstacle, and its members' direct lines refuse
to cross it. A mover stays in the plan until it has been off the way for two
scans. Same-player and allied movers always count; an enemy mover only within
`World::sightDistance` of some member, never for a group whose mission engages
enemies (fight, attack, guard, patrol). One selection sent as several groups
(one player, within 90 ticks, to points within 32 cells) never plans round
itself, and a mover going the same way is followed, not avoided.

**Detection (W7, protocol 246).** A conflict is found by sampling, not by a
distance test. Every 15 ticks, up to 450 ticks ahead, the group's own descent
chain (walked at its slowest member's speed) is compared with each mover at its
measured velocity; a conflict is the group's disc meeting the mover's box, and
one under 45 ticks away is too late to plan round. The class of the meeting is an
integer dot-product test on each group's line to its destination (never its
measured heading, which reads a group already routing behind a stream as head
on): head on over 135 degrees, same way under 45, crossing in between. It is
stored as hashed `avoidKind`, folded into the checksum only when set. A shared
awareness field is shared only between groups with equal charge lists, and
aware refreshes are served first inside `kRefreshQuota`, at most 8 starts a tick.

**Crossing groups take the longer route (W7, user decision 5).** On a crossing
way the later group never waits for the other stream to pass. It plans round a
time-swept corridor that covers the stream's band from its tail to 450 ticks past
its head, but only where the stream will be when the group gets there (a lead of
twice the group's radius), so the cheapest way is just behind the tail. The
corridor charges 12x inside and is not a wall: if no detour exists at all the
group crosses as it did before, and no detour length leads to a wait. The entry
is sticky until the group has crossed or the stream's tail has passed, and the
corridor is re-placed when the stream drifts, at most every 90 ticks. The old
10-second wait at the stream's edge (give-way hold) does not exist, so no
`giveWay` / `waitSince` state exists and the `giveway_*` counters stay 0. Head on,
the groups still keep right. Measured by `aware-cross-behind` and `crosslong`
(`behind.*.extra_cells`, `detour_permille`, `wait_member_ticks`; the wait keys
`wait_ticks`, `wait_run_max`, `waits` read 0), `aware-cross`, `awarebig-cross` and
`crossthree`.

Measured by `aware-headon:pair.contacts.permille_x100` and the `aware-*` fixtures
(`aware-unseen` is the awareness-off control: the enemy is out of sight).

### Convoys: one order per click (W3 A1)

(Since protocol 267 a Legion client's click is an explicit selection order that joins this table once for all
its members -- see "Legion 10,000 stage 3" below; the per-unit inference described here remains for the AI's
and scripts' commands, and the table is the arrival-area table for both.)

The client lands a selection over 64 units over several ticks (64 commands
per tick, and at most 512 outstanding), so each tick's part used to be an
order of its own. In Legion mode `World::order()` now stamps every order with
`Order::convoyTick`, the tick its **convoy** opened: one value for every order
one click gave, ground and air, per player and order class (Move, Fight from
`attackMove`, Patrol from `patrolTo` and the flyer branch of `patrol()`). A
patrol's return leg (queued by `patrol()`, or appended by the patrol mission
when it starts) copies its outbound leg's value and never opens a convoy.
`issuedTick` stays the identity of the order's own 64-unit part.

The table (`sim/convoy.h`) is anchored on the convoy's first shared point:
`legionSharedClick` in `legion.h`, which the client's right-click also calls,
says which units get the click itself (surface movers with footprints 1..8);
the rest get the click plus their offset from the selection centroid, clamped
to 60 px per axis. Shared orders join within one cell of the anchor, offset
orders within 60 px + 1 cell; before an anchor exists, within 60 px + 1 cell
(shared) or 120 px + 1 cell (offset) of every point taken, tested on the
convoy's bounding box. All tests are per axis. A convoy stays open while the
tick is at most `last + 9` (up to 8 command-free ticks: the 512 window with a
16-tick round trip) and at most `first + 32`; closed convoys leave the hashed
table at the start of the next tick. Lookups go through two ordered indexes
(anchored convoys by anchor cell, unanchored ones by the 256 px tile of their
first point), and each order's probes plus candidates are counted as
`legion_convoy_tests` (crowdbench) and `work.convoy_tests` (the scenario
observer, inside `work.legion_total`); the declared bound is per order (p99 at
most 16, max at most 64), which `legion_scenario` reports as `convoy.tests_max`,
`convoy.over16_permille` and `convoy.over64` and baseline.json gates. `TAK_LEGION_VERIFY` checks every join
against a scan of the table and the indexes against a rebuild every tick.
Legion keys on the convoy (A2): the shared point (formation, area, settling),
the group issue and the soft-obstacle command key use `convoyTick`, so a
selection over 64 units is one point, one formation and one settle chain.
Formation slots wait until the convoy can no longer be joined, then are handed
out front first, at most `kSlotsPerTick` (32) a tick and within the tick's slot
search budget (see "Slot hand-out budget"); the rest steer by the shared field
meanwhile. A first part under one uplink tick (64 orders) is the whole click as
far as the server can tell, so it takes its formation at once, provisionally;
if another part joins the point before the convoy closes, every slot is given
back and the formation is rebuilt over all parts then (W3 round 4: waiting
without slots walked small groups the first stretch bunched on the line to the
click -- legion_landedflyers' stand against the flyers came from that). The pinwheel ranks a member within its 64-unit part (its order's
`issuedTick`, `Member::part`) against that part's live centroid. A2 was parked
once at W3 (the corner gate counted far tails that completed mid-route as
arrivals); measured again on top of the settle rule with physical arrival, it
lands. Later workstreams key on the convoy directly (W6 flight stations, W7
Trapped ownership, W8's lane cap). `convoy_test`
covers the click shapes (flyers first with saturated offsets, opposite
corners, all-air, the 974-unit window at round trips 0/8/16/24, patrol, two
clicks 48 px apart) and the index against the scan.

### Flyers and group records (W6, protocol 243)

W6 (T5 and T6) makes the flyers of a squad follow the click they belong to and
keeps a landing or lifting flyer from trapping ground units. All of it is in
the sections it changes (the rest-time rejoin under Formations, flight
stations, the lift area and landing); this is the list.

- **Flight stations pair on the click.** A Ctrl+N group's flyers keep station
  over the ground of their own click, like an Alt+N formation's; up to 4
  buckets a squad, keyed by (order class, `convoyTick`).
- **Release.** A station flyer is released for its order when the ground
  stops (no headway for 450 ticks, or a centroid still for 900), so a sealed
  wall or a dead end no longer keeps the wing hovering.
- **Landing is safe.** A descending flyer is stamped on its touchdown
  footprint from stage 3 (leave-only); no walker steps in under it.
- **Lift episodes are bounded.** At most 1800 ticks (60 s) and a 600-tick
  rest; kept up only by members making headway; a lifted flyer polls for a
  target every 8 ticks and fights (`work.lift_target_polls`, bound lifted
  flyers / 8 a tick). An own-squad landed flyer lifts only for a stalled
  squad member whose goal or next 2 cells it covers.
- **The rejoin gathers ground only,** at the formation's modal settled point;
  a settled body is never re-ordered, so a dead-end queue is not pulled back.
- **Not built:** the touchdown backstop (C5) and home-excluded landing (step 7,
  C20); the post-release cap floor (it moved Alt+1 start offsets).

Exit numbers: `docs/legion-exit-tables.md` "W6 exit"; the cases are in
`tools/legion_flyers_test.cpp` (`docs/legion-w6-step0.md`). Hashes: the
crowdbench Legion rows without flyers or squads, the Legion navigation golden
and the Inner Circle `--mpai` hash are unchanged from protocol 242.

### Upkeep on demand (W4, protocol 242)

W4 (T7 Stage B) makes Legion's per-tick upkeep follow demand instead of the
population: a stale field refreshes only for a group that needs it (B1), still
bodies are sampled a thirtieth of the population a tick (B2), and the
settled-arrival records are erased on the event that ends them (B3). All three
landed under one protocol bump (242).

**Event-driven records (B3).** The anchors (settled arrivals, kept with the
per-destination `anchorsAt` count), the approach-done records and the parting
records used to be walked in full by `serviceYields` every tick to drop the ones
whose unit had died or got a new order. Now `World::noteOrders(id)` reports every
change of a unit's orders -- the order helpers (`order`, `loadInto` for unit and
carrier, `unloadAt`, `patrol`, `orderWait`, `orderWaitAttack`, `guard`, `attack`,
`queueBuild`, `queueManaBuildArea`, `reclaimArea`, `reclaim`, `repair`,
`startEmote`, through a scoped reporter so every return path reports), `dropLeg`,
`acquireTarget`, the ground, flight and construction mission dispatchers, the
unload park leg and the two death edges -- and `LegionNavigator::ordersChanged`
applies the old per-entry tests to that one unit (an anchor stays while the unit
is alive and idle or still on its completed leg; the other records while it is
alive and idle). Anchors drop through `dropAnchor`, so `anchorsAt` stays an exact
count of live settled bodies for the settle rule. `registerMove` also erases the
unit's parting record. A dense per-unit hint makes the event two vector reads for
a unit with no record. `serviceYields` keeps only its yield-step loop over
`yielding`. No hook sits in `findTarget`, `forEachNear` or the Retail mover.

Writes to `Unit::orders` that bypass the helpers (morph and consume, a test's
direct write) are caught by the backstop: `prune`'s hashed 256-a-tick cursor now
walks the members and the three record maps in one id order (with no records it is
the old members-only cursor), so a stale record lives at most
ceil((members + records) / 256) ticks. `work.anchor_walk_iters` counts the
cursor's ids: at most 256 a tick (pocket-615's per-tick max 616 -> 256).
`legion_world_test b3events` checks that an order to a settled body drops its
anchor inside the order call, a stop keeps it, and a death or a direct order write
drops it within one tick.

**Demand-driven refresh (B1).** A static change still marks every field it reaches
stale, but the scheduler starts a refresh (the stale-done pass) only for an
**active** group: one whose member started its update Moving in the last 2 ticks,
one with a **blocked** member, or one carrying the `demand` flag (set by the aware
re-plan, cleared when the refresh swaps in; folded into the checksum with a tag
when set). An inactive stale group stays listed and steers by its finished field;
its refresh starts the tick it becomes active (`work.demand_resumes`), and each
skipped visit counts `work.refresh_suppressed`. A refresh already under way keeps
building (pausing it too cost crowdtrap arrivals), `kRefreshQuota` and swap-in at
done are unchanged. Blocked is Retail's re-request (`kBlockedRetry` = 120 ticks):
`stalledFor >= 120` (there is no give-way hold, W7 decision 5), and either no
finished field reaches it, or it stands outside its destination area with no legal
step down the field clear of soft bodies (a body held only by its own command or
by movers is not blocked: a refresh would give it the same way); each turn to
blocked counts `work.blocked_rerequests`. `movingTick` and `blockedTick` are
not hashed: they are functions of the last 2 ticks' hashed member updates, and no
path rebuilds Legion from a snapshot (a mid-game `--mprejoin` stays in sync).
`legion_staticidle`: parked guards beside continuous construction do no refresh
work (764640 relaxations -> 0), and a pair held at a plugged gap re-plans through
a gap that opens by the blocked rule alone. In the live 8-AI Ulasem benchmark
refresh is under 2% of field work (first builds are the rest), so B1 hardly moves
its totals there; paused solo first builds (C1, below) are the lever there.

**Striped still-body scan (B2).** `scanStill` samples each tick only the bodies
whose `id % 30 == tick % 30`, with the same two-sample stillness rule per body, and
adds or removes each stamp cell by cell: `soft`/`softKind` hold the lowest covering
id (overlaps kept in `softStack`), with `softCounts`, `softOwner` (and per-command
counts for `ownsArrivals`) and `liftSoftPlayers` kept in step. The whole-population
rebuild and sort on every 30th tick are gone: the scan's per-tick peak falls from
about 330 bodies in one tick of 30 to about 25 (Ulasem). `registerMove` and a
landed flyer's take-off lift the body's whole stamp (its sample stays).
`softHash` is a wrapping sum of `mix(cell, id, kind)` over stamped cells plus
`mix(id, command)` over owned ids, so it is order-independent and maintained by
add and remove; `softSerial` counts stamp changes. `softHashFull()` recomputes it
from the stamps: under `TAK_LEGION_VERIFY` (on in the Legion world-test ctests) it
is checked every tick, and the grid, owners, lift players and window counts once
a scan period. The aware scan (residue 15) now reads a soft view that changes
every tick (accepted; W7 re-checks). The stripe phase moves a few outcome keys
within their seed spread (aware headon contacts, landedflyers, tail wave p90).

### Reach rings and engaged bodies (W5)

* **Engaged.** An attacker that `tickCombat` brakes in reach (or a guard within
  70 px of its ward) is Engaged (member state 6) until its next `move()`: to
  local steering it is settled and still (bodies behind walk round it), it
  never yields or parts, and it is soft to other commands only (a passing
  group plans round a firing crowd; its own attack does not). A reach kind's
  held same-target peer (30 ticks) is settled too, and reach kinds never run
  the settle rule or contact arrival: the owner ends the approach.
* **Rings.** An attack group with two or more members when its first field
  starts plans to a ring: band 0 lies from 1.5 bodies inside `tickCombat`'s own
  reach out to the reach edge (melee at contact; 0.5-1.5 bodies inside until
  2026-10-19, see "Ring upkeep"), legal, in the members' component and
  in line of sight of the target, and is the field's seeds; outer bands wait.
  A guard group plans to its ward's point with no ring: its 70 px is a stance
  round the ward, and a ring there kept twelve guards re-choosing spots for
  ~2800 ticks (without it they are quiet by ~2000; the W4 head's settle rule
  parked them by 900, which reach kinds no longer take: `staticidle` warms up
  2400 ticks).
  The first claim hands out every spot (nearest members to band 0, matched in
  angle so lanes do not cross); late joiners take the nearest free spot; a
  member held a 45-tick window re-chooses (3 times) a free spot in a lower
  band or nearer in its own; an engaged body lets go of a spot it stopped short
  of. A group that grows to two after its first field keeps its point seed
  (re-planning cost the battles a refresh per target).
* **Illegal goals.** A member whose goal turns illegal re-claims a slot, or
  takes the nearest legal origin of its region, inside its group.
* **Late soft blocks (MV-05).** A block of soft cells 20+ cells across that
  forms after a group planned, on its 48-cell descent chain from its centroid
  or tail, re-plans it once (300-tick cooldown, refresh quota). Blocks leaving
  never re-plan.
* **Not built (MV-06).** Speed-matched commit budgets (S1) and the brisk tier
  (unturned detour/yield/part steps at cap/2), full and restricted, were each
  measured and reverted: S1 raised opposingcolumns 500x4 spin 1268 -> 1639 and
  cap/8 time everywhere; the full tier doubled corner reversals (corner-8x56
  306 -> 561) and lost cost-open's t90; the restricted tier lost maze /
  exploration / dynamicobstacle arrivals by 2.5-3.4%. The cap/8 statue stays
  a documented trade (PLAN 3.4 stop rule).

### Ring upkeep (2026-10-19, task-l100-ring)

Measured on the ~100-body fixtures (`tools/scenarios/legion100/`). Combat
brakes an attacker the moment it is in reach, so the bodies of a ring stand
on its reach edge, and a band-0 spot with a body firing on or beside it can
no longer be reached. The W5 ring did not know that:

* All band-0 spots stayed the field's seeds, so the rest of the army descended
  to the first arc it met and queued behind it, while the far side stood
  empty. 100 attackers against a target on the corner of a walled box
  (`attackring-wall-100`, offsets 3..-5) fired with 17 to 22 bodies against
  Retail's 34.
* Band 0 ended half a body inside the edge and band 1 straddled it, so members
  waited a few pixels out of reach in the gaps of the firing line; members
  owning spots behind the line held out of reach for good, standing in other
  groups' way (`ringcross-100`).
* An attack whose target stood outside the members' region (a target walled
  in, offsets 0..+-2) was an unreachable goal: the army walked to the one
  nearest reachable point, piled against one face and dropped its orders on
  arrival. Retail keeps the order and presses up to the walls on every side.

The changes, Legion only (Retail hashes unchanged):

* **Bands.** Band 0 runs from 1.5 bodies inside reach to the reach edge; band 1
  is the rest of reach out of the target's sight (behind its walls), where
  nothing brakes a body; bands 2+ wait beyond the edge, only on the half
  facing the members (waiting round the far side, they stood across the lanes
  to it). With no spot of the members' region in sight of the target, the
  whole of reach is band 0, planned without the sight test.
* **Attack on a target out of the region.** No approach member: the goal is
  the region's nearest point and the group plans a ring round the target as
  usual; the order is kept, as Retail keeps it.
* **ringScan** (every 30 ticks, a ring group with a finished field): a band-0
  spot is spent when a still body (engaged, held, settled, or no Legion mover)
  covers its footprint, or the footprint on the reach edge radially out from
  it. The seeds become the unspent spots (none left: unchanged) and a refresh
  plans to them (`work.ring_reseeds`). An unspent spot whose owner has been
  held elsewhere for 90 ticks is swapped with the outer spot of the nearest
  waiting member at least a body nearer to it (`work.ring_swaps`); members
  owning a spent spot, or waiting outside band 0, take the nearest free
  unspent band-0 spot. Re-choice never picks a spent spot.
* **Soft to its own attack.** An engaged attacker, or one held on its own ring
  spot, is a soft obstacle to its own attack too (guards are unchanged), so
  the refresh goes round the firing arc instead of into it.
* **Pressing in.** An attacker held within two bodies outside its reach steps
  into a free neighbouring origin whose centre lies 12 px inside reach (and in
  sight, where the shot needs it) as a committed side-step; combat brakes it
  there (`work.reach_presses`). The side-step ends when the origin changes,
  half a cell (on a diagonal under 12 px) short of the centre: tested at the
  centre itself, as first landed, a body stepped in, was not braked, walked
  back out and pressed again (attackring-wall-64 offset -4: 12 of 14 presses
  from the origin of the member's previous one). `work.reach_press_repeats`
  counts those; `ctest:legion_pressin` requires none.
* A buried owner swapped onto an outer spot is not handed a band-0 spot again
  in the same scan.

Medians over the gate offsets (Legion before -> after, Retail):

| fixture | key | before | after | Retail |
|---|---|---|---|---|
| attackring-wall-100 | reach.ring.damage_end | 865 | 1571 | 1922 |
| attackring-wall-100 | reach.ring.ever_end | 33 | 33 | 49 |
| attack-100 | reach.ring.ever_end | 6 | 21 | 18 |
| attack-100 | reach.ring.damage_end | 1500 | 2493 | 1880 |
| ringcross-100 | reach.ring.ever_end | 38 | 41 | 43 |
| ringcross-100 | reach.ring.damage_end | 5453 | 5463 | 6458 |
| ringcross-100 | g.M.t90 (5000 ticks) | -1 (all 11) | -1 (9 of 11) | 3926 |
| ringcross-100 | g.M.t90 (12000 ticks) | 5618 (10 of 11) | 5191 (all 11) | 3926 |
| ringcross-100 | g.M.arrived (12000 ticks) | 48 | 49 | 50 |
| ringcross (48/40) | g.M.t90 (12000 ticks) | 4696 | 4381 | 3880 |

attackring-wall-100's walled offsets (0, +-1, +-2) trade "ever in reach"
(93 / 89 / 44 / 34 / 58 -> 42 / 46 / 40 / 33 / 44): the old blob milled at the
approach point inside reach; now the ring stands round the walls, as Retail's
does, and keeps its orders. On the offsets with a ring, ever in reach is
28 / 33 / 16 / 23 / 15 / 21 -> 35 / 27 / 30 / 29 / 33 / 30 (Retail 40 / 49 /
32 / 44 / 35 / 35) and damage reaches 68-89% of Retail's (was 37-60%). attack-100
is a chase: Legion now kills its target as Retail does but sooner, with more
bodies in reach. The other 19 legion100 fixtures are hash-identical. In
battle-field-2x60 the contact window's line traces rise 11.5% while its Legion
work falls 11% and its field work 17% (baseline entry updated; group A loses
30 bodies instead of 33 over offsets 0,1,-1). `ctest:legion_ringupkeep`,
`ctest:legion_attackwalled` and `ctest:legion_ringcross` fail on the parent
and pass here. The Move group's crossing is sensitive to a handful of ring
steps: with the pressing-in reach tested at the step's centre (the shuffle
above) ringcross-100's t90 was 4771 at 12000 ticks, with it fixed 5191.

### Rings at ~100 bodies, round 2 (2026-10-19, task-l100-ring2, protocol 280)

Three changes, Legion only (Retail hashes unchanged on every legion100 fixture and
the attackring/ringcross originals; `--mpai` Inner Circle seed 1 60 s Retail
b353410003e98dd5, Legion 0825aeb9e8139203, both unchanged):

* **Sub-cell presses.** Retail's attacker stops at whatever point of a free origin
  combat brakes it; Legion's pressIn stepped to a neighbour's centre and only where
  that centre lay 12 px inside reach, so a body held 6 px out of reach in a cell
  that reaches in found no origin. Now an origin -- the body's own one included --
  is a candidate when the nearest point of its footprint-centre box to the target is
  in reach (16.16, a sixteenth of a pixel in) and in sight; the body walks to that
  point (`Member::pressX`, shortest walk first, unturned at the facing cap, ended
  after kPressTicks = 120 updates, a block, or leaving its origin's neighbours) and
  combat brakes it on the way. A body in reach but out of sight (behind a wall:
  combat does not brake it) presses on to a point in sight, as Retail's walks on
  at its target. Pressing at travel speed was measured and rejected: braked bodies
  glide on and ringcross-100's t90 rose ~300 ticks; with 45 updates at the cap a
  press ran out short of its point and the body walked back
  (`ctest:legion_pressin`).
* **The inside of a ring is charged in its own field** (`fieldAvoid`): a mover
  corridor disc (kSoftFactor inside, kSoftNear within 32 px) of radius just inside
  band 0, centred on the target. A way to a far spot that crosses the ring ends at
  the first arc it meets (combat brakes the body there); at kSoftFactor through its
  own firing arc the field still led the queued army straight across the ring into
  the back of that arc, and the far side filled one rescan at a time. Now the queue
  walks round the outside of the arc, as Retail's blocked bodies slide round it:
  ringcross-100 has half its 50 attackers in reach at tick 717 (850 before, Retail
  653; `ctest:legion_ringfill`).
* **Moving groups bend round other commands' firing rings** (`ringBend`). Every
  kRingScan ticks ringScan lists the rings round a still target as discs: the
  reach, or the farthest member within four bodies of it, plus a ring body's half.
  A formation member whose lane to its own slot passes within 1.5 clearance radii
  (disc + its own half + 16 px) of another command's ring, not one round its own
  side, steers by the potential flow round that clearance circle in a stream along
  its own lane: aim two bodies along the flow's heading, re-aimed at each new
  origin, when the line there is legal and clear of soft bodies. Lanes keep their
  order and side (a lane through the centre goes by the unit id), so the group
  parts round both sides as Retail's bodies, each routed to its own point, do.
  Down the shared field every body went round the side of the field's nearest
  slot, in one or two files along the ring's tangent, and the tail queued there.
  Aiming at the clearance circle's tangent point instead (first try) put every body
  of a side in one file again (t90 4111); a clearance of the reach alone, without
  the waiting bodies, crossed slower (4291). From inside the circle, with its slot
  inside it, or with the line refused, the body keeps the field.
  `ctest:legion_ringbend`.
  The bend looks no farther than the line probe (kFormationLineCells past the 1.5
  radii circle), only when its own lane up to that circle is clear of static
  blocks (the line probe's whole-lane sweep), and never for a livelocked body. The
  review of the first head found a livelock without these: a U-shaped wall on the
  lane, 180 cells short of the ring, took every bend into the pocket and the field
  back out (0 of 20 arrived in 20,000 ticks; now 20 of 20, t90 4485 at offsets
  0,1,-1, Retail 19-20 / 4497; `ctest:legion_ringbend_pocket`). A cap of three
  clearance radii was tried instead and cost the crossing: ringcross-100 t90 4426
  at the gate offsets (4066 with no cap), since the lanes part well before the ring.

Medians over the gate offsets (Legion before -> after, Retail):

| fixture | key | before | after | Retail |
|---|---|---|---|---|
| ringcross-100 | g.M.t90 | 4741 | 4066 | 3926 |
| ringcross-100 | g.M.done (5000 ticks) | -1 (done at 2 of 11) | 4528 (all 11) | 4166 |
| ringcross-100 | g.M.done (12000 ticks) | 5378 (10 of 11) | 4528 (all 11) | 4166 |
| ringcross-100 | reach.ring.t50 | 850 | 717 | 653 |
| ringcross-100 | reach.ring.ever_t1500 | 30 | 33 | 43 |
| ringcross-100 | reach.ring.ever_end | 41 | 40 | 43 |
| ringcross (48/40) | g.M.t90 | 4156 | 3796 | 3880 |
| ringcross (48/40) | g.M.done | 4606 (9 of 11) | 4370 (all 11) | 4293 (6 of 11) |
| attackring-wall-100 | reach.ring.damage_end | 1571 | 1748 | 1922 |
| attackring-wall-100 | reach.ring.ever_end | 33 | 34 | 49 |
| attackring-wall-64 | reach.ring.damage_end | 1301 | 1465 | 1909 |
| attackring-mob-100 | reach.ring.ever_end | 35 | 43 | 44 |
| attackring-mob-100 | reach.ring.damage_end | 2182 | 2683 | 2688 |
| attackring-f1-64 / squad-64 | reach.ring.ever_end | 40 | 42 | 45 |
| attackring-struct-60 | reach.ring.ever_end | 50 | 59 (target killed by 2823) | 59 (2847) |
| attackring-r200-64 | reach.ring.ever_end | 54 | 52 | 64 |
| attackring-mob-40 | reach.ring.ever_end | 39 | 36 | 39 |
| attack-100 | reach.ring.damage_end | 2562 | 2295 | 1880 |

attackring-wall-100's ring offsets (3, -3, 4, -4, 5, -5) now deal 82-101% of
Retail's damage (1938 / 1757 / 1781 / 1855 / 1748 / 2157 against 2307 / 2135 /
1922 / 2227 / 1991 / 2143; 68-89% before), with 35 / 29 / 31 / 30 / 33 / 34 ever in
reach (35 / 27 / 30 / 29 / 33 / 30 before, Retail 40 / 49 / 32 / 44 / 35 / 35).
The walled offsets still deal none, and offset 2 (the target in the box's corner)
has 28 in reach against 33 before (Retail 52). The other 19 legion100 fixtures
are hash-identical. attack-100 is a chase (no disc: the target moves); its ring
presses differently and lets more of the fleeing group through, nearer Retail
(g.E.arrived 2 -> 4 of 10, Retail 6; damage 2562 -> 2295, Retail 1880).

### Rings at ~100 bodies, round 3 (2026-10-20, task-l3-ring3)

**The 0.8.6 prep gate's attack-ring regressions, bisected** (v0.8.5 .. bd7f3f6e: 30a38c9e, the round 1 parents
5d697081 / 906339c1 / 7827f6e8 / 8e60cc60, 48c5b05c, the round 2 parents 0c820175 / 53863b04 / 96f301f6, aacb1f2c,
the blocked-shot commits c74c7f4e / 94fb3b17 / 575a70d2). The gate quotes the W5 step-0 base, not 0.8.5: the W5 exit
licensed these keys (`accepted_regressions`) at their exit values, so "290 -> 20,977" and "0 -> 1,669" read against
2026-10-09, not against the release.

* `attackring-r200-64:statue_ticks` (a flat map: every offset is one run) was 19,585 at 0.8.5 (the W5-exit licence),
  25,307 at 7827f6e8 (ring upkeep), 21,303 at 8e60cc60 and 48c5b05c, 20,977 from 0c820175 (round 2) on; blocked
  shots change nothing. A real, old cost: no body was frozen, they crept. Every statue tick was in the blocked branch
  (`SiteBlocked`): attackers queued outside the firing arc walked their local detour routes and flow-around shuffles
  unturned at the facing cap of an eighth (offset 0, counted with the closed-spot rule below already in: 6,921 capped detour-route updates, 6,612 shuffle updates, 381 press updates),
  and the owners of spots the arc had closed off took the next free spot behind it, scan after scan (one member swapped
  between two spent spots 13 times in 600 ticks). Retail's mover traces round a body at its own speed (155).
* `attackring-f1-16:statue_ticks` 683 at 0.8.5 (licence), 1,998 from 7827f6e8 (ring upkeep), 1,669 from round 2: two
  of the 16 bodies crept round the ring outside reach for ~750 and ~600 ticks, the same crawl. Real; fixed below (175).
* `attackring-wall-64:g.A.arrived` / `complete_n` 2 -> 0: drift toward Retail. The base 2 is the median of the first
  five offsets at W5 step 0; at 0.8.5 the offsets read 2, 2, 2, 0, 2, 0, 0, 0, 0, 0, 0 (gate median 0 already): at the
  walled offsets two attackers dropped their orders at the approach point. Since 7827f6e8 ("walled targets keep
  orders") none does, as in Retail (0 at every offset).
* `attackring-river-64:g.A.complete_n` 1 -> 0 (all offsets): the same 7827f6e8 change, the target across the river
  out of the attackers' region; Retail 0.
* `attackring-struct-60:g.T.arrived` 1 -> 0: from 0c820175 (round 2) the ring kills the 30,000-HP structure (by tick
  ~2,820, Retail ~2,850); a dead target is not "arrived". Retail 0.

**The changes**, Legion only (Retail hashes unchanged on every fixture checked):

* **Rings round still targets walk at speed** (`ringStanding`, since round 4 `stillTarget`, below). An attack member of a ring round a target standing
  still walks its local detour routes of `kBriskRoute` cells or more and its flow-around shuffles at travel speed,
  unturned, as a formation member's already did (rounds 1 and 2 kept the cap for attack legs). A chase (a moving
  target) keeps the cap: there the faster ring killed `attack-100`'s target with 5 bodies in reach instead of 22 and
  the rest of the fleeing group got away (Retail 18).
* **Closed-off spots are spent** (ringScan). The origins out to two bodies beyond the reach edge are flooded from the
  free origins a body or more beyond it, through origins no still body covers (the ring's own held members do not
  wall it: they move on or give their spot up), walking on only where nothing brakes a body: out of reach, in reach
  out of the target's sight (each origin's own sight test, band 1's; see the review below), or anywhere when band 0
  was planned without sight (`Ring::sight`). A band-0 spot whose stop point (the reach edge
  radially out from it; the spot itself without sight) is not flooded is spent, so its owner takes a spot it can get to.
* **Spare attackers wait outside.** A member whose spot is spent with no free unspent band-0 spot left takes the
  nearest free outer-band spot instead of standing behind the arc. With the speed change alone the spares milled at the
  top of `ringcross-100`'s ring, across the Move group's lanes (crossing t90 3,841 -> 4,089 at the gate offsets).
* **Pressing flush** (pressIn). Out of sight at the box point nearest the target, a press also tries the box's far
  edge toward it, the last sub-pixel of the origin. Retail's attacker pushing at its target against a wall is clamped
  there (`footprintClamp`, 4db010), its float centre rounds into the next cell, and the sight cast (`losBetween`)
  starts in that cell: pressed flush to a 1-cell wall's west or north face it fires through it
  (`attackring-wall-100` offset 0, Retail at tick 3,000: of its 61 bodies in reach, 24 stand on the last pixel of a cell in x and 23 in z, the box's west face at x = 2015.99 and north face at z = 1215.99 among them). A press now
  ends exactly at its point (it ended 1/16 px short, which a flush point does not survive), and a press walked out of
  the target's sight (`pressHidden`) keeps travel speed: nothing brakes the body before its point, and at the cap a
  press round a wall's end ran out of `kPressTicks` half way and repeated.

Medians over the gate offsets (Legion before -> after, Retail):

| fixture | key | before | after | Retail |
|---|---|---|---|---|
| attackring-r200-64 | statue_ticks | 20,977 | 480 | 155 |
| attackring-r200-64 | reach.ring.ever_end / damage_end | 52 / 3,392 | 59 / 4,069 | 64 / 3,717 |
| attackring-f1-16 | statue_ticks | 1,669 | 175 | 2 |
| attackring-f1-64 / squad-64 | ever_end / damage_end / statue | 42 / 2,591 / 25,891 | 43 / 2,840 / 910 | 45 / 2,612 / 195 |
| attackring-mob-40 | ever_end / damage_end | 36 / 2,154 | 40 / 2,638 | 39 / 2,438 |
| attackring-mob-100 | ever_end / damage_end | 43 / 2,683 | 40 / 2,762 | 44 / 2,688 |
| attackring-wall-64 | damage_end / t50 | 1,465 / 2,225 | 1,834 / 1,475 | 1,909 / 1,429 |
| attackring-wall-100 | damage_end / ever_end | 1,748 / 34 | 1,795 / 33 | 1,922 / 49 |
| attackring-wall-100 | damage at the walled offsets 0, 1, -1, -2 | 0 / 0 / 0 / 0 | 1,169 / 833 / 1,104 / 952 | 689 / 350 / 767 / 646 |
| attackring-wall-64 | damage at the walled offsets 0, 1, -1, -2 | 0 / 0 / 0 / 0 | 1,197 / 845 / 1,169 / 782 | 781 / 240 / 860 / 647 |
| ringcross-100 | reach.ring.ever_t1500 / t50 | 33 / 717 | 42 / 571 | 43 / 653 |
| ringcross-100 | damage_end | 5,700 | 6,405 | 6,458 |
| ringcross-100 | g.M.t90 / done | 3,841 / 4,252 | 3,841 / 4,365 | 3,926 / 4,166 |
| ringcross (48/40) | g.M.t90 / done | 3,686 / 4,156 | 3,751 / 4,195 | 3,880 / 4,293 |
| attack-100 | ever_end / damage_end | 22 / 2,152 | 20 / 2,116 | 18 / 1,880 |
| attackring-struct-60 | t50 (killed either way) | 1,057 | 917 | 1,216 |

Over the wide offsets -8..8 (Windows sweep, hashes equal to Linux; median before -> after, Retail):
`attackring-wall-64` damage 1,484 -> 2,031 (2,104), ever 34 -> 36 (42); `attackring-wall-100` damage 1,757 -> 2,017
(2,135), ever 35 -> 34 (41); `ringcross-100` t90 3,841 -> 3,841 (3,926; mean 3,853 -> 3,856), done 4,252 -> 4,337
(4,173), ever by 1,500 33 -> 42 (43); `ringcross` t90 3,745 -> 3,841 (3,880; mean 3,763 -> 3,813), ever by 1,500
36 -> 39 (45) but at the end 41 -> 39 (45); `attack-100` damage 2,254 -> 2,467 (1,880), ever 22 -> 19 (18).

Statue ticks fall by 90-98% on every ring round a still target (wall-100 19,968 -> 1,412, ringcross-100 24,877 ->
1,168), still above Retail's few hundred: presses toward reach keep the cap (pressing at speed, measured in round 2,
let braked bodies glide on). At the walled offsets Legion now outfires Retail: its presses look for the flush points
on every face that has one, Retail's bodies reach them only where they happen to push. Not changed: the ring offsets
of `attackring-wall-100` keep 29-36 bodies in reach against Retail's 32-49 (Retail packs the in-sight ground between
the arc and the wall from out of sight; a band-0 extension to spots in sight bordering band 1 was tried and moved
nothing there: wall-100 ever 33 -> 35, wall-64 damage 1,834 -> 1,747), `attackring-mob-100` loses 3 bodies in reach
(43 -> 40, damage up), and `attackring-river-64` (target across a river) still stands 6 against Retail's 21. Fight and
guard rings (`attackring-fight-60`, `attackring-guard-64`) are untouched: `ringStanding` is attack only, and their
statue ticks (32,402 and 42,133) are the next case (done in round 4, "Fight and guard rings round still targets"). `battle-field-2x60` and `battle-assault` move both ways: group losses by up to six
bodies either way (battle-field-2x60 A 34 -> 30 dead,
battle-assault B8 44 -> 50), travel reversals 1,290 -> 1,198 in the latter. `ctest:legion_ringstill` (r200: statue < 2,000 and ever >= 57;
20,977 / 52 on the parent), `ctest:legion_ringclosed` (ringcross-100 by tick 1,500: >= 41 in reach and t50 <= 600;
33 / 717 on the parent, 39 / 634 with the speed change alone), `ctest:legion_ringwait` (ringcross-100 gate median t90
under 3,950: 4,089 with the speed change alone, 3,841 on the parent and here) and `ctest:legion_attackflush`
(attackring-wall-100 offsets 0, 1, -1: median damage >= 400; 0 on the parent). `ctest:legion_pressin` keeps no
repeat at any gate offset.

**Review: the flood's shadow** (same branch). The closed-off-spot flood first took band 1's listed spots for the
in-reach shadow it may walk through. That list is a sample of the shadow, not the shadow: `buildRing` packs origins a
footprint apart and stops at the cells the ring wants (`min(256, max(2n, n+8))`), so most of the shadow braked the
flood, and bodies of two cells or more, whose band-1 origins are never 8-adjacent, crossed none of it. A target whose
in-sight spots are reached only through in-reach shadow then had every band-0 spot spent and was never attacked. The
flood now repeats band 1's sight test (the sight grid, target position and skip `buildRing` used, kept on the ring)
for every in-reach origin. New fixture `attackring-gatehouse-40` (tools/scenarios): a yard walled at 6 cells, open on
the east, inside an outer wall at 10 cells (in reach at the corridor's middles), open on the west; 40 2x2 attackers
with range 160 from the west. At offset 0 the old flood spent all 10 band-0 spots from the first scan (damage 0); now
1 (damage 238; Retail 557, round 2 224). Over the gate offsets the median damage is 244 (old flood 320, round 2 224,
Retail 584) and ever in reach 15 (20, 16, 29): with every spot spent, the old flood's members fell back to outer-band
spots, which here are band-1 spots in the corridor pressed flush to the yard's wall, and fired through it at the
offsets where they got in. With the spots open they walk for the east beam, and the first bodies in press flush to
the yard wall just inside the west gate and leave one cell of the 3-cell corridor, so the rest queue outside (Retail
fills the corridor). Every other ring fixture is hash-identical at all 11 gate offsets (r200-64, f1-16, f1-64,
squad-64, mob-40, mob-100, wall-64, wall-100, river-64, struct-60, ringcross, ringcross-100, attack-100): their
shadow was 1x1 and fully listed, or not on the way in. `ctest:legion_ringshadow` (gatehouse offset 0, damage >= 150;
0 before).

### Rings along walls, round 4 (2026-10-20, task-l4-ringwall)

Measured how Retail stands in the same fixtures first (dumps of every body at tick 2999, `--dump-units`). Retail's
ring is not deeper on its open arc: at `attackring-wall-100` offset 3 both modes stand one body deep on the reach
edge. Retail's extra bodies are all in two kinds of ground Legion left empty. (a) The shadow
by a wall and the in-sight ground just beyond it, inside band 0's inner bound: offset 3, 10 Retail bodies at 53-87 px
from the target along the box's east and south walls against 3; offset -3, 16 against 4 (bodies under 88 px; reach
96). Retail's bodies there walked
in out of sight along the wall, kept walking at the target, and stopped where they got the shot or a body stopped them.
(b) Rings with no spot in sight (the box's walled offsets 0, +-1, +-2, the river): Retail's bodies walk at the target
until a wall or a body stops them and stand two rows deep at sub-cell points (river offset 0: flush with the shore at
x = 1583, a second row at 1567-1572).

Why Legion did not fill that ground, each found by a ring dump (spot, band, spent, owner and its place, every 300
ticks; a scratch instrument, not committed):

* **The band-1 spots' owners never got there.** `attackring-wall-100` offset 3: of the 6 band-1 spots (in reach, out
  of sight) 3 stood empty from tick 1,200 to the end; their owners held 2-4 cells short behind members waiting on outer-band
  spots that lie in the only way into the shadow. ringScan handed the waiting members band-0 spots only, and swapped a
  buried owner's spot only for band 0.
* **The shadow pressed one origin on.** pressIn looks at the body's own origin and its 8 neighbours for a point in
  sight; offset -3's west pocket was reached only through two shadow origins, so its bodies stood in the shadow.
* **A blind ring spent its own spots.** ringScan spent a band-0 spot when a still body stood on the reach edge
  radially out from it, the stop point where combat brakes a body. A ring planned without the sight test has no such
  stop: nothing brakes a body there. `attackring-river-64` offset 0: every one of its 12 bank spots was spent, by the
  row standing behind them, and 6 bodies were in reach (Retail 14).
* **A blind ring's press needed sight**, so a body a pixel out of reach behind a wall found no press point; and a body
  that did get in off its spot's centre walked back out to it (spots on the edge's last pixel) and pressed again.
* **The gatehouse plug** is not a press: the first body through the west gate walks straight at its own band-0 spot
  beyond the yard wall (the "ring member near its own spot walks to it" rule: near any band-0 seed the potential reads
  band 0's), pushes flush into the wall in the corridor's mouth, sees through it (a 2x2 body's sight skips the cell
  beside it) and is braked there by combat.

The changes, Legion only (Retail hash-identical on `attackring-wall-100` and `attackring-gatehouse-40` at every gate
offset):

* **Band-1 spots are handed on** (ringScan). A band-1 spot is spent when a still body covers it or the flood reaches it
  by no way from outside reach; a member waiting on an outer band takes a free unspent band-1 spot when no band-0 spot
  is left; and the stale-owner swap (an owner held elsewhere 90 ticks) covers band-1 spots, taken only by members of
  the outer bands.
* **The walk on through the shadow** (`pressRoute`). A body held in reach out of sight with no point in sight a step
  away searches (radius 6 origins, every 16 held updates) over free origins whose centre is in reach and out of sight
  for the nearest origin whose centre is in reach and in sight, or failing one, the shadow origin nearest the target
  (half a cell nearer at least); the way is committed as a local route, walked at travel speed round a still target
  (nothing brakes the body in the shadow; at the facing cap the short walks crept, statue ticks 1,551 -> 2,570 on
  wall-100 before this). `work.reach_press_routes` counts them.
* **A body in reach stays in reach** (ringScan hand-out): it may move to a free band-0 spot but is never sent out to
  an outer band; of a blind ring it keeps its place.
* **Blind rings** (`Ring::blind`: the members' shot needs sight and no spot in reach has it, so band 0 was planned
  without the sight test): no spot is spent for a body standing radially out at the edge (the flood
  alone decides); a body held out of reach on its own spot, or whose spot's centre is out of reach, presses within its
  own origin to the first point `kPressDepth` (4 px) inside reach on its way toward the target (on the very edge the
  crowd's nudges put it back out; pressing flush to the wall raised wall touch, wall-64 28 -> 40 permille); and a
  member standing in reach holds there instead of walking to a goal centre out of reach. Presses into a neighbour
  origin and presses of a body on its way to a spot in reach were tried and made shuffles (`work.reach_press_repeats`
  8 and 16 at wall-64 offsets 0 and -2; `ctest:legion_pressin` requires none).
* (A "no press across a lane" rule, `sealsLane`, landed with the first cut and was taken out by the review fixes
  below: its measured gain was the frozen-body fault's doing.)

Medians over the gate offsets (after the review fixes below), Legion (base 30813a73 -> after), Retail; `now` is in reach at the end, `ever` ever in
reach (Retail's crowd jostles in and out, so its ever runs above its now):

| fixture | key | base | after | Retail |
|---|---|---|---|---|
| attackring-wall-100 | reach.ring.ever_end (11 / core five) | 33 / 46 | 36 / 58 | 49 / 82 |
| attackring-wall-100 | ever at the ring offsets 3, -3, 4, -4, 5, -5 | 30 / 32 / 29 / 33 / 32 / 33 | 37 / 35 / 29 / 36 / 32 / 33 | 40 / 49 / 32 / 44 / 35 / 35 |
| attackring-wall-100 | now at the walled offsets 0, 1, -1, 2, -2 | 42 / 38 / 40 / 27 / 38 | 57 / 55 / 58 / 29 / 59 | 61 / 62 / 63 / 36 / 68 |
| attackring-wall-100 | reach.ring.damage_end | 1,795 | 1,870 | 1,922 |
| attackring-wall-100 | statue_ticks / wall_touch_permille / reversals | 1,412 / 13 / 268 | 728 / 20 / 285 | 229 / 17 / 455 |
| attackring-wall-64 | reach.ring.ever_end (11 / core five) | 34 / 45 | 39 / 56 | 45 / 62 |
| attackring-wall-64 | reach.ring.damage_end | 1,834 | 1,836 | 1,909 |
| attackring-wall-64 | statue_ticks / wall_touch_permille / reversals | 1,131 / 28 / 252 | 585 / 32 / 305 | 173 / 20 / 195 |
| attackring-river-64 | reach.ring.ever_end / now_end | 6 / 6 | 17 / 17 | 21 / 14 |
| attackring-river-64 | ever at offsets 0, -1, -2, -3 | 6 / 15 / 33 / 47 | 17 / 29 / 45 / 62 | 21 / 39 / 61 / 64 |
| attackring-river-64 | statue_ticks | 1,340 | 22 | 85 |
| attackring-gatehouse-40 | reach.ring.ever_end / now_end | 15 / 10 | 16 / 15 | 29 / 24 |
| attackring-gatehouse-40 | reach.ring.damage_end | 244 | 320 | 584 |
| attackring-gatehouse-40 | statue_ticks / wall_touch_permille / reversals | 1,053 / 54 / 47 | 23 / 78 / 221 | 23 / 68 / 314 |

Over the wide offsets -8..8 (Windows sweep of the reviewed sources, WIP ref 397696bb, the gatehouse without
`sealsLane`; against 30813a73, median then mean; Retail in brackets): `attackring-wall-100` ever 34 -> 36 (41), mean
36.7 -> 40.5 (49.6), now mean 35.0 -> 40.1 (44.2), damage 2,017 -> 2,061 (2,135); `attackring-wall-64` ever 36 -> 37
(42), mean 37.4 -> 41.5 (45.4), damage 2,031 -> 2,052 (2,104); `attackring-river-64` ever 6 -> 17 (21), mean 24.2 ->
28.2 (29.9); `attackring-gatehouse-40` ever 22 -> 26 (36), now 18 -> 22 (31), damage 320 -> 653 (643), mean 428 ->
612 (711), statue ticks 1,053 -> 34 (25); `ringcross-100` identical at every offset (t90 3,841, Retail 3,926);
`attack-100` damage 2,467 -> 2,467 (mean 2,523 -> 2,538), ever 19 -> 19 (18), E arrived 3 -> 4 (6). Every other ring
fixture is hash-identical at all 11 gate offsets (r200-64, f1-16, f1-64, f2-64, squad-64, mob-40, mob-100, struct-60,
ringcross, ringbend-pocket; fight-60 and guard-64 too). Trades: wall touch on `attackring-wall-100` 13 -> 20 permille
(Retail 17; wide median 13 -> 20, Retail 11), `attackring-wall-64` 28 -> 32 (Retail 20), `attackring-gatehouse-40`
54 -> 78 (Retail 68); travel reversals up on wall-100 (268 -> 285), wall-64 (252 -> 305) and the gatehouse (47 ->
221), all but wall-64 still under Retail's; shadow walks are reversals by construction (in and along a wall, then
braked). `attack-100` (legion100) changes at 4 of 11 gate offsets: damage 2,116 -> 2,325, spins 1,011 -> 1,499,
statue ticks 10,196 -> 10,330, E t90 4,334 -> 4,325 at the gate median.

Still open: the box's walled offsets stand 2-7 fewer than Retail at the end (Retail fills sub-cell points of the
corners' origins, 70 origins reach against Legion's 48 cell centres); offset -3's west pocket keeps 2-3 empty cells
walled in by braked bodies (Retail's sit between cells); the gatehouse's west-gate offsets (0, +-1) are still plugged
by the first body walking straight at a spot beyond the yard wall. Requiring that walk's line to be statically legal
(the "near its own spot" rule) moved the plug, did not remove it: bodies then descended the field and were braked at
the in-sight ground in the mouth, and the offsets whose bodies had slid round the outer wall's corner to the south and
east (2, 4, 5) lost them (gatehouse ever 20 -> 9 at the gate median, damage 331 -> 328). A relaxed band-1 swap (any
nearer taker) moved nothing (wall-64 offset 2 40 -> 37). Holding every blind-ring body in reach where it stands cut the
walled offsets back (wall-100 63 / 59 / 62 / 68 -> 51 / 51 / 53 / 61): the inner row fills from the edge's walkers.
`ctest:legion_ringblind` (river offset 0: >= 15 in reach at the end; 6 on the parent), `ctest:legion_ringpocket`
(wall-64 offsets -3, -4, 4: median >= 35 in reach at the end; 32 on the parent), `ctest:legion_ringroute` and
`ctest:legion_ringnosight` (the review fixes, below). Fight and guard rings (`ringStanding`, the other round-4
track) are not touched; the overlap is ringScan's hand-out and pressIn, which both read.

#### Review fixes (2026-10-20)

An independent review of the first cut found two faults, both reproduced and fixed; fixing the first took away the
evidence for `sealsLane`, which went too:

* **The walk on from the shadow froze its body.** `pressRoute` committed a route but left the body held with its hold
  clock running, and `hold()` does not count a hold that pressIn served. On its next update the route branch dropped
  the route (held 30 ticks or more), and with `holdUpdates` unchanged (`% kPressRouteEvery == 4` still true) the
  search ran again: a body that first found its way in sight after its second check re-planned every update without a
  step, for good (`attackring-wall-64` offset 0: 3,674 walks, one body re-routed every tick for ~1,600 ticks).
  It now stops and restarts its hold clock, as a detour's route does (`m.state=Holding; restartHold(m)`). Walks at
  `attackring-wall-64` offsets 0 / 1 / -1 / -2: 3,674 / 4,075 / 1,798 / 4,319 -> 136 / 292 / 67 / 102; now in reach
  at offset 0 54 -> 60; the river's offsets -3 / -4 / -5 6,335 / 14,830 / 10,768 -> 241 / 1,710 /
  1,839. `ctest:legion_ringroute` (wall-64 offset 0: under 1,000 walks).
* **The blind-ring rules reached rings that need no sight.** They were keyed on `!Ring::sight`, which is also false
  for a ring whose shot needs no line of sight at all (`reachPx`: melee, lobbers, ranges of 64 px or less). Combat
  brakes those bodies on the reach edge, so the reach-edge spend, the in-reach hold, the 4 px press and the in-reach
  keep of the hand-out (a body in reach is never sent out to wait) were wrong there. They now key on `Ring::blind`
  (set only by buildRing's pass 1) and the press path on its own flag; the in-reach keep applies to rings that need
  sight (in sight or blind). `attackring-r200-64` with a 60 px gun (`tools/scenarios/sweeps/attackring-r60-64.scn`):
  statue ticks 611 -> 1,905 on the first cut, and every gate offset hash-identical to the parent again (611); with a
  48 px gun reversals 44 -> 60 and back to identical. `ctest:legion_ringnosight` (offset 0: under 1,000 statue
  ticks). Blind rings fold `Ring::blind` into the state hash.
* **`sealsLane` removed.** Its proof, `attackring-gatehouse-40` offset -2 (damage 320 -> 539), came from the frozen
  bodies above: with them fixed, offset -2 was 318 with the rule and 320 without it (the parent's 320; the first body
  through the west gate is braked flush in the yard's mouth by combat, which the rule never reached). Over -8..8
  without the rule against with it: damage mean 612 against 604 (no offset better with it; -5 926 against 860, 3 368
  against 305), now in reach 19.6 both (median 22 against 24), reversals 358 against 373, wall touch 87 against 84
  permille; `attackring-wall-100`, `attackring-wall-64` and `attackring-river-64` hash-identical with and without it
  at every gate offset. No clear win, so out, and its ctest (legion_ringseal) with it.

What the fixes moved over the gate offsets (first cut -> fixed, Retail): ever / now in reach unchanged at the median on
all four fixtures; `attackring-wall-64` reversals 340 -> 305 (195), wall touch 25 -> 32 permille (20), now at offset 0
54 -> 60; `attackring-wall-100` statue ticks 620 -> 728 (229; offset 2's value), reversals 305 -> 285 (455), wall
touch 18 -> 20 (17); `attackring-gatehouse-40` damage 305 -> 320 (584), reversals 164 -> 221 (314), wall touch 69 ->
78 (68), now at offset -2 24 -> 18 (29), damage there 539 -> 320 (584); `attackring-river-64` 17 / 17 unchanged. Retail hash-identical to the parent at every gate offset on
`attackring-wall-100`, `attackring-gatehouse-40` and `attackring-river-64`; every other ring fixture (fight-60 and
guard-64 among them) and the legion100 screen are hash-identical to the first cut (`sealsLane` acts only by the
gatehouse's walls), `attack-100` included (its first-cut
change against the parent: damage 2,116 -> 2,325 and spins 1,011 -> 1,499 at the gate median, E t90 4,334 -> 4,325,
at 4 of 11 offsets).

### Fight and guard rings round still targets (2026-10-20, Legion ~100 round 4, task-l4-fight)

**Where the creep was** (offset 0, every update that stepped at a facing-capped eighth of travel speed without turning,
counted by steering branch). `attackring-fight-60` (60 attackers on a fight order, attack-move, to a still target):
28,100 of its statue updates were attack members walking local detour routes at the cap and 1,635 their committed
shuffles. They are attack members because combat inserts an auto-target attack in front of the fight leg
(`World::acquireTarget`), and that leg is Legion's `Attack` kind. But round 3's cure was for rings, and this group
has none. The column comes into acquisition range one body at a time, so the group's first member joins alone
(t=694 at offset 0) and starts the field before a second member joins (t=732), and the 60th joins at t=1455.
`startField` builds a ring only for a group that already has two members, and a group that grows later keeps its
point seed (the cost note in `ringClaim`), so `ring.band0` stays 0 and `ringStanding` never applies.
`attackring-guard-64`: 32,193 guard route updates and 9,931 shuffle updates. A guard has no ring (`buildRing`
refuses it: its 70 px is a stance), and round 3's rule was attack-only. Retail's movers trace round a body at their
own speed whatever their order (statue ticks 38 and 194).

**Where round 3's ring reversals come from** (each return to a 16 px cell left under 90 ticks before, attributed to
the branches that made the step out and the step back). They come from a few bodies behind the firing arc, not from
detour routes backing out and not from spot re-choice. `attackring-r200-64` offset 0: 389 reversals, 307 of them by
three bodies, and one body (id 11) turns back and forth inside a 6x4-cell patch from tick 900 to tick 3,000.
The pairs are a brisk shuffle against a field step (194), field against field (49) and shuffle against shuffle (48).
Only 21 involve a detour route, and in 65% of all 389 the member's spot had not changed in the 90 ticks before.

The mechanism: blocked by the engaged bodies of the arc, the body flows round to the neighbour of lowest potential.
From there the field's string-pulled aim cuts back across the cells it left: in one body's trace (id 25) a field
step from a cell of potential 35 enters one of 42 (the aim's line is tested for legality, not for potential). It is
blocked again and flows back. Each flow-around reaches a cell of lower potential, which counts as a gain, so the stall clock restarts
every cycle and the body never holds long enough for a hold's way out (a detour route round the arc, a ring
re-choice after a held window). At the facing cap the same shuffles crept less than a cell before the next decision,
which is why round 3, by lifting the cap, raised reversals while it cured the creep. `ringcross` and `ringcross-100`
have the same pattern: 79 + 60 + 59 + 58 of ringcross's 423 come from four bodies. In the wall fixtures the largest share
(`attackring-wall-64` 85 of 252, `attackring-wall-100` 97 of 246) was between the field and a ring member's direct walk to its own spot ("A ring member near its own spot walks
to it": the threshold flips cell by cell). That pattern was there before round 3, and Retail's reversals there are
higher (attackring-wall-100 455).

**The changes**, Legion only (Retail hashes unchanged on attackring-fight-60, -guard-64 and -r200-64 at the gate
offsets; --mpai Inner Circle seed 1 60 s: Retail b353410003e98dd5, Legion 0825aeb9e8139203, both unchanged):

* **Any attack or guard member round a target standing still** (`stillTarget`, which was `ringStanding`) walks its
  detour routes and committed shuffles at travel speed, ring or not. A guard's target is its ward. A chase (a moving
  target or ward) keeps the cap.
* **A route round a still target keeps travel speed to its last cell.** Before, it did so only while `kBriskRoute`
  (4) or more cells were left. At the cap the short tails were most of what the rings had left of the creep
  (`attackring-wall-64` at offset 0: 1,329 statue ticks, 2 with this round's changes).
* **A member round a still target that its last crowd window found still** (`Member::stillSince`: it gained under
  half a body on its point, and on its own spot, over the window) **commits no new flow-around shuffle.** It holds,
  and the hold's ways out take over. This is the same rule as the one for a livelocked body
  (`Unit::legionShuffle`), but read from the settle rule's own window: the shuffles' potential gains kept the stall
  clock young, and the headway clock stopped the slide round the arc as well (below). A committed shuffle still runs to its end. No new state: `stillSince` is
  already hashed. (Since task-l4-fixring, not for a member out of the target's sight, nor for one holding a band-1
  spot of a shadowed ring or a band-0 spot of one after four still windows: see "Rings along walls with the
  still-window rule".)

Medians over the wide offsets -8..8 (Windows sweep of the experiment build, which is hash-identical to the commit on
five of these fixtures at the gate offsets; Retail is the gate median):

| fixture | key | before | after | Retail |
|---|---|---|---|---|
| attackring-fight-60 | statue_ticks | 32,402 | 372 | 38 |
| attackring-fight-60 | reach.ring.damage_end / ever_end / t50 | 1,662 / 36 / 2,472 | 2,398 / 40 / 1,512 | 2,814 / 44 / 1,206 |
| attackring-fight-60 | reversals / stopped_permille | 28 / 477 | 42 / 625 | 86 / 577 |
| attackring-guard-64 | statue_ticks | 42,133 | 1 | 194 |
| attackring-guard-64 | within 6 cells: ever_end / t50 | 61 / 1,837 | 64 / 1,271 | 63 / 1,191 |
| attackring-guard-64 | reversals / stopped_permille | 39 / 432 | 43 / 634 | 122 / 520 |
| attackring-r200-64 | statue_ticks / reversals | 480 / 413 | 380 / 85 | 155 / 45 |
| attackring-r200-64 | ever_end / damage_end / stopped | 59 / 4,069 / 660 | 61 / 4,164 / 679 | 64 / 3,717 / 576 |
| ringcross | statue_ticks / reversals | 6,663 / 420 | 780 / 200 | 150 / 61 |
| ringcross | ever_end / damage_end / stopped | 39 / 6,097 / 615 | 41 / 6,250 / 647 | 45 / 6,435 / 641 |
| ringcross | g.M.t90 / done (mean t90) | 3,841 / 4,214 (3,813) | 3,789 / 4,066 (3,802) | 3,880 / 3,723 |
| ringcross-100 | statue_ticks / reversals | 1,168 / 351 | 604 / 144 | 204 / 88 |
| ringcross-100 | ever_end / damage_end / stopped | 42 / 6,405 / 617 | 42 / 6,371 / 630 | 43 / 6,458 / 604 |
| ringcross-100 | g.M.t90 / done (mean t90) | 3,841 / 4,337 (3,856) | 3,841 / 4,194 (3,830) | 3,926 / 4,166 |
| attackring-wall-64 | statue_ticks / reversals | 1,140 / 335 | 98 / 133 | 173 / 195 |
| attackring-wall-64 | ever_end / damage_end | 36 / 2,031 | 35 / 2,022 | 45 / 1,909 |
| attackring-wall-100 | statue_ticks / reversals | 1,412 / 274 | 136 / 213 | 229 / 455 |
| attackring-wall-100 | ever_end / damage_end | 34 / 2,017 | 37 / 1,974 | 49 / 1,922 |
| attackring-f1-64 | statue_ticks / reversals | 910 / 489 | 228 / 131 | 195 / 111 |
| attackring-f1-64 | ever_end / damage_end / stopped | 43 / 2,840 / 610 | 39 / 2,672 / 640 | 45 / 2,612 / 511 |
| attackring-f1-16 | statue_ticks / reversals | 175 / 31 | 96 / 13 | 2 / 3 |
| attackring-mob-40 | statue_ticks / reversals / ever_end | 228 / 63 / 40 | 137 / 45 / 38 | 59 / 41 / 39 |
| attackring-mob-100 | statue_ticks / reversals / ever_end | 1,193 / 196 / 40 | 405 / 218 / 42 | 238 / 352 / 44 |
| attackring-struct-60 | statue_ticks / reversals | 381 / 76 | 267 / 61 | 78 / 45 |
| attackring-gatehouse-40 | statue_ticks / reversals | 1,053 / 49 | 3 / 41 | 23 / 314 |
| attackring-gatehouse-40 | ever_end (mean) / damage_end (mean) | 22 (20) / 320 (428) | 19 (19) / 320 (416) | 29 / 584 |
| attackring-river-64 | statue_ticks | 1,401 | 1 | 85 |
| fight-retarget | statue_ticks / g.F.t90 | 3,638 / 2,477 | 2,384 / 2,319 | 44 / 2,638 |
| attack-100 (a chase) | ever_end / damage_end / g.A.dead | 19 / 2,467 / 19 | 21 / 2,299 / 21 | 18 / 1,880 / 18 |

The three parts measured apart over -8..8 (statue / reversals). The speed alone (fight and guard) leaves the ring
fixtures unchanged, except `attackring-river-64`, whose group has no band-0 spot (statue 1,401 -> 139). The whole-route speed without the still-window rule raises reversals:
`ringcross` 420 -> 551, `ringcross-100` 351 -> 388, `attackring-wall-100` 274 -> 380, `attackring-mob-100`
196 -> 344. The still-window rule without the whole-route speed leaves the wall fixtures creeping:
`attackring-wall-64` statue 1,709, `ringcross` 3,711.

Trades. Stopped permille rises on the ring fixtures by up to 32 permille against the parent (`attackring-f1-64`
610 -> 640, `ringcross` 615 -> 647). Bodies reach the arc sooner and stand there firing, or hold behind it instead of
shuffling. On fight and guard it rises by 150-200 permille (477 -> 625, 432 -> 634), past Retail's 577 / 520.

`attackring-f1-64` (a flat map, the same run at every offset) gets 4 fewer bodies ever in reach: 43 -> 39 against
Retail's 45, with damage 2,840 -> 2,672 against Retail's 2,612. The free spots left at the end are on the far side
of the ring. The bodies that held behind the west arc used to reach them only by the slow shuffle round it.
`attackring-mob-40` and `attackring-gatehouse-40` also lose some (40 -> 38, 22 -> 19 at the median).

`battle-assault` (gate offsets, local) travel reversals rise 1,200 -> 1,444 against Retail's 376, its statue ticks
fall 177,564 -> 148,978 and deaths rise on both sides (428 / 429 -> 444 / 443). In `battle-field-2x60`, A loses 25
bodies instead of 30 (Retail 37), and its statue ticks fall 11,111 -> 4,196 (Retail 53). The fight orders' contact
window spends 5% more slot search (`tools/legion_cost_baseline.json`, battle-field-2x60 contact
`work.slot_search_cells`, moved 6,001,458 -> 6,863,753 with its reason: formation ring searches 6,409,716 ->
6,708,301 at offset 0). Its total Legion work falls.

Regression screen, all 22 tools/scenarios/legion100 fixtures at the gate offsets, Legion, against 30813a73: 19 are
hash-identical at every offset. The three ring fixtures move: `attackring-wall-100` (statue 1,412 -> 136,
reversals 268 -> 185, damage 1,795 -> 1,843, stopped 656 -> 663, `wall_touch_near_permille` 56 -> 63),
`ringcross-100` (statue 1,168 -> 721, reversals 264 -> 145, `g.M.done` 4,365 -> 4,231, `g.M.t50` 3,577 -> 3,584,
damage 6,405 -> 6,371, stopped 617 -> 630) and the chase `attack-100`, through attack groups without a ring whose target stands at the
moment (the guard part alone leaves it hash-identical). A loses 22 instead of 20 (Retail 18), 22 ever in
reach instead of 20 (Retail 18), damage 2,116 -> 2,059 (Retail 1,880), spins 1,011 -> 1,938 (Retail 29,764) and
reversals 13 -> 22 (Retail 390). E's arrivals go 3 -> 2 (Retail 6).

Tried and rejected:
* No flow-around once the stall clock or the headway clock (`Member::headTick`) has aged a crowd window. The stall
  clock restarts on every shuffle's potential gain, and the headway clock stopped the slide round the arc too
  (`attackring-f1-64` ever 43 -> 38).
* A clock of the best distance to the member's own spot (new state). It came out close to the chosen rule
  (`ringcross` reversals 278 against 200, `ringcross` t90 3,905 against 3,789), so the rule that needs no state was
  kept.
* A stalled member's shuffle measured toward its own spot instead of down the field: `attackring-gatehouse-40`
  reversals 47 -> 480 (gate offsets).
* Barring the step back onto the origin it last left (the `slideFrom` rule for every step): it moved the to-and-fro
  onto longer cycles (`attackring-mob-100` 196 -> 702, gate offsets).

Considered and not tried (built in round 5, "Fight rings and the reach edge"): a ring built late for a group that grows after its first field. A ring sized for its
first two members (`want` = 10 spots) would have to serve 60 attackers, re-planning it costs a refresh per target
(`ringClaim`'s note: +13-28% total Legion work in the battles), and ring spot generation is another track's work.

`ctest:legion_fightstill` (fight-60 offset 0: statue under 2,000 and damage at least 2,000; 30,229 / 1,483 on the
parent), `ctest:legion_guardstill` (guard-64 offset 0: statue under 2,000 and half within 6 cells by tick 1,500;
42,133 / 1,837 on the parent), `ctest:legion_ringroutespeed` (wall-64 offset 0: statue under 300; 1,329 on the parent and
without the whole-route speed) and `ctest:legion_ringshuffle` (r200 offset 0: reversals under 200 and ever at least
57; 413 on the parent and 337 without the still-window rule). Each fails on the parent and without its own part.

**Fight legs' shuffles (the review's follow-up, same day).** The changes above cured fight orders only where the
column meets its arc head on. A fight order's members still on their fight leg (before they acquire, and behind the
arc while the bodies ahead fire) kept the facing cap on every committed flow-around shuffle: `stillTarget` covers only
attack and guard members, and `Member::detourFlow` was set only for legs that do not engage enemies (round 2's
"rings and melees" exception, which was never measured for fight legs). `attackring-fight-60`'s straight approach
hid it (352 of its 353 statue ticks left at offset 0); with the target north-east of the column
(`attackring-fight-diag-60`, a new fixture: only the target moved) the reviewer's offset-0 count was 3,070 capped
updates from fight members with a committed shuffle, and the same happened against a target that walked off and
then stood (`fight-moved`, a scratch fixture). The change: a fight leg's flow-around shuffle is a `detourFlow` like a
move leg's, with the move leg's exclusions (head-on meet, admitted passage, a crowd window without gain). Attack,
guard and patrol shuffles are unchanged. No new state; `detourFlow` is already hashed while set. Retail hashes are
unchanged (the fight fixtures below and the --mpai game).

Medians over -8..8 (local, cores 19-23), 30813a73 / round 4 before this / after / Retail. The battles spawn off
the map past the gate offsets, so theirs are gate medians.

| fixture | key | 30813a73 | before | after | Retail |
|---|---|---|---|---|---|---|
| attackring-fight-diag-60 | statue_ticks | 28,331 | 3,918 | 3 | 23 |
| attackring-fight-diag-60 | ever_end / damage_end / t50 | 29 / 1,085 / never | 41 / 1,725 / 2,018 | 41 / 1,750 / 1,971 | 44 / 2,159 / 1,652 |
| attackring-fight-diag-60 | reversals / stopped_permille | 27 / 337 | 53 / 442 | 49 / 458 | 79 / 426 |
| fight-moved (scratch) | statue_ticks / ever_end / damage_end | 28,818 / 28 / 1,028 | 3,591 / 41 / 1,733 | 2 / 41 / 1,753 | 55 / 48 / 2,236 |
| attackring-fight-60 | statue_ticks | 32,402 | 372 | 3 | 31 |
| attackring-fight-60 | ever_end / damage_end / t50 | 36 / 1,662 / 2,472 | 40 / 2,398 / 1,512 | 40 / 2,382 / 1,476 | 44 / 2,810 / 1,210 |
| attackring-fight-60 | reversals / stopped_permille | 28 / 477 | 42 / 625 | 52 / 631 | 86 / 577 |
| fight-retarget | statue_ticks | 3,638 | 2,384 | 449 | 39 |
| fight-retarget | g.F.t90 / done (of the runs done) | 2,477 / 2,764 | 2,319 / 2,655 | 2,293 / 2,629 | 2,638 / 2,723 |
| fight-retarget | runs never done of 17 / bodies left behind | 7 / 8 | 4 / 5 | 7 / 10 | 0 / 0 |
| aware-attack | statue_ticks / click a00, b00 t90 | 2,544 / 1,813, 1,802 | 2,544 / 1,813, 1,802 | 1,941 / 1,757, 1,827 | 18 / 2,000, 1,893 |
| battle-field-2x60 (gate) | statue_ticks / reversals | 11,111 / 63 | 4,196 / 79 | 715 / 101 | 53 / 190 |
| battle-field-2x60 (gate) | A dead / A arrived | 30 / 12 | 25 / 16 | 27 / 17 | 37 / 3 |
| battle-assault (gate) | statue_ticks / reversals | 177,564 / 1,200 | 148,978 / 1,444 | 50,562 / 1,553 | 1,480 / 376 |
| battle-assault (gate) | dead A / B / stopped_permille | 428 / 429 / 553 | 444 / 443 / 552 | 462 / 458 / 583 | 214 / 221 / 14 |

The creep is gone, but a fight order's reach is not better for it: the diagonal approach still has 41 bodies ever
in reach against Retail's 44 and 81% of its damage, as the straight one does (40 against 44). That is the missing ring
(above), not the speed. Trades: `fight-retarget`, whose fighters re-join the convoy after the chase, has its
stragglers settle at the edge of the packed disc. The runs never done go back to 30813a73's 7 of 17 (4 before
this), and 10 bodies end outside the disc over the 17 runs (5 before). The outliers stand 2-8 px past the observer's
214 px limit; Legion's crowd is looser there than Retail's (offset 0: farthest body 216 against Retail's 164). Its t90
and its completed runs are slightly sooner. At offset 0 the left-behind body walks its last 100 px at travel speed with its stall clock past the crowd
window (108 ticks), so not on a fight shuffle: the speed changes the crowd's history rather than walking it out. In the battles, reversals rise (battle-assault
1,444 -> 1,553, Retail 376), stopped permille rises 552 -> 583, and both sides lose more bodies (444 / 443 -> 462 /
458, Retail 214 / 221). `aware-attack` b00's t90 is 25 ticks later, a00's 56 sooner. All 22 legion100 fixtures are
hash-identical to the change before it at the gate offsets (none gives a fight order). `legion_cost` passes.

Tried and rejected (gate medians):
* Speed only when the body in the way fires (`Engaged`) or stands round a still target: `attackring-fight-diag-60`
  2,988 -> 2,944. The capped shuffles are mostly round the column's own fight members, not the shooters.
* Speed only outside the point's area (`Point::limit`): the same as the chosen rule on the fight rings, and no better
  on `fight-retarget` (6 of 11 never done).
* The cap again for a fight member that its last crowd window found still (`stillSince`): `fight-retarget` 8 of 11
  never done (5 with the chosen rule).

`ctest:legion_fightshuffle` (attackring-fight-diag-60 at offset 0: statue ticks under 300; 2,988 before this).
The Legion lines of the navigation golden (`tools/navigation_checkpoints.json`) move from tick 2,400 on (10 of 24
checkpoints, serial equal to workers; the Retail lines agree), so `navigation_determinism` fails until the
integration retakes them with tools/scenarios/baseline.json; neither is retaken here.

### Rings along walls with the still-window rule (2026-10-20, Legion ~100 round 4, task-l4-fixring)

The two round-4 ring tracks interfered when they were merged (task-l4-land). Their two speed rules do not conflict:
fight's `stillTarget` covers ringwall's `ringStanding`, and ringwall's own route rule for the shadow walks too.
Fight's still-window rule does conflict. A member round a still target that its last crowd window found still
commits no new flow-around shuffle. Along a wall, the members found still are the queue on the crowd short of the
shadow, or short of the pockets between the firing arc and the walls. A held body there has no detour route round
the arc, so it stood. The band-1 hand-outs and the walks through the shadow (`pressRoute`) stopped short:
`attackring-wall-64`'s walks fell from 35 to 5 (gate median), it lost 4 bodies in reach, and offset +2 repeated one
press (`ctest:legion_pressin`). Toggling each rule of the merge back on its own showed this. Ringwall's route rule
for ring members gave statue 918 and no depth. Without the still-window rule for ring members, the merge had
ringwall's depth and fight's statue ticks, but `attackring-r200-64` reversals went back up to 337. With every
rule in ringwall's form, every ring fixture was hash-identical to the ringwall binary, so turnA plays no part.

**The change** (`shadowWalk`): the still-window rule skips a member that stands out of the target's sight
(`pressHidden`), a member holding a band-1 spot of a shadowed ring, and a member holding a band-0 spot of a
shadowed ring once its crowd windows have found it still for `kShadowStill` (4) windows running (180 ticks). A ring
is shadowed (`Ring::shadow`, set in `buildRing` from `blind` and the band list, so it is not hashed separately) when
it is blind or band 1 lists a spot, which means walls hide the target from part of the ground in reach. Waiting
members (band 2 and beyond) and band-0 members just found still keep the stop, as on open ground: they are the back
of the arc, and the hold's own ways out (a detour, a ring re-choice after a held window) come first. On open ground
(r200, ringcross, f1, f2, r60) nothing changes: those fixtures, the fight and guard fixtures, `fight-retarget`,
`aware-attack`, `goalblock-fight-200`, `battle-field-2x60` and `mixed2` are hash-identical to task-l4-land at every
gate offset. Retail is unchanged (attackring-wall-64 and -gatehouse-40 at the gate offsets).

*Review (first form).* As first written the rule skipped every member of a shadowed ring. That brought the round-3
back-of-arc shuffles back round any ring with a wall in reach, even where the exemption gains nothing: copies of
wall-64 with 24 and 32 attackers (every body reaches in both builds) went from 31 to 82 and from 66 to 124
reversals. In those copies the reversals come from band-0 members (exempting only band 1 and out-of-sight members
gave 39 and 84). Band 0 can't be left out altogether, though: `legion_ringpocket` then reads 34 (offset -4), the
queue bound for free band-0 spots in the pocket south of the box held behind the arc's engaged bodies. The
still-window delay sorts the two: on wall-64 only offsets 2, 3, -3 and -4 differ between the forms at all, by one
to three bodies.

Gate medians, ringwall alone / task-l4-land / first form / this change / Retail (the sums over the 11 offsets of
ever / now in brackets):

| fixture | key | ringwall | land | first form | this | Retail |
|---|---|---|---|---|---|---|
| attackring-wall-64 | ever_end / now_end | 39 / 37 (482 / 469) | 35 / 35 (464 / 462) | 38 / 37 (475 / 468) | 39 / 37 (479 / 469) | 45 / 42 |
| attackring-wall-64 | now_end at -3, -4, 4 (`legion_ringpocket`, median) | 37 | 35 | 37 | 37 | 42 |
| attackring-wall-64 | statue / reversals / press walks | 585 / 305 / 35 | 43 / 123 / 5 | 44 / 240 / 14 | 48 / 317 / 90 | 173 / 195 / - |
| attackring-wall-100 | ever_end / now_end | 36 / 36 (468 / 460) | 36 / 36 | 37 / 37 (473 / 465) | 38 / 37 (476 / 466) | 49 / 41 |
| attackring-wall-100 | statue / reversals | 728 / 285 | 133 / 233 | 131 / 362 | 143 / 292 | 229 / 455 |
| attackring-gatehouse-40 | ever_end / now_end | 16 / 15 (208 / 166) | 17 / 14 (203 / 170) | 16 / 15 (211 / 168) | 18 / 15 (213 / 171) | 29 / 24 |
| attackring-gatehouse-40 | statue / reversals | 23 / 221 | 3 / 169 | 11 / 221 | 17 / 221 | 23 / 314 |
| attackring-river-64 | now_end / statue / reversals | 17 / 22 / - | 17 / 1 / 20 | 17 / 1 / 31 | 17 / 1 / 31 | 14 / 85 / - |
| attackring-r200-64 | reversals / ever_end | 413 / 59 | 85 / 61 | 85 / 61 | 85 / 61 | 45 / 64 |
| battle-assault | statue / reversals / spins | - | 52,040 / 1,617 / 14,372 | 49,330 / 1,515 / 14,609 | not re-run | 1,480 / 376 / 40,060 |
| wall-64, 24 attackers (scratch copy) | reversals / statue / ever | - | 31 / 65 / 24 | 82 / 65 / 24 | 39 / 65 / 24 | - |
| wall-64, 32 attackers (scratch copy) | reversals / statue / ever | - | 66 / 74 / 32 | 124 / 56 / 32 | 86 / 74 / 32 | - |

`legion_pressin` passes again (no repeat at any gate offset), and so do `legion_ringpocket`, `legion_ringroute`,
`legion_ringroutespeed`, `legion_ringblind`, `legion_ringshadow`, `legion_ringshuffle`, `legion_fightshuffle`,
`legion_ringnosight` and the other ring tests. The trade is reversals by the walls, which come back toward
ringwall's: wall-64 123 -> 317 (ringwall 305, Retail 195; the first form 240), and wall-100 233 -> 292 (ringwall
285, Retail 455; the first form 362). The gatehouse stands 6 statue ticks more than the first form (17, Retail 23).
`attackring-river-64` keeps its 17 in reach but its reversals go 20 -> 31 against land (the out-of-sight and band-1
exemptions; the same in both forms). The press walks on wall-64 rise 14 -> 90 (gate median), with no press repeated
(`legion_pressin`). The delay is a knife edge on `legion_ringpocket`: at 3, 5 and 6 still windows instead of 4 its
median read 33, 34 and 34 (one body at offset -4), and at 8 windows 34.

Not cured here. `attackring-r60-64` (26 in reach, ringwall and the parent 28, Retail 27) and `attack-100` (spins
1,657, ringwall 1,499; E arrivals 2, ringwall 4) move because of fight's own rules on open ground, not because
of the interference. The r60 ring needs no sight, so it has no shadow and keeps the still-window rule. attack-100
is fight's ringless `stillTarget` (with `stillTarget` limited to attack members of an assigned ring, it went back to
spins about 1,400 and 3 arrivals, but the fight and guard cures went too). wall-64 ever in reach is 39, level with
ringwall's (sum 479 against 482).

Tried and rejected (gate medians):
* Only `pressHidden` exempt: wall-64 38 / 38, but wall-100 35 / 35.
* `pressHidden` or a band-1 spot (no shadowed-ring term): wall-64 40 / 38, wall-100 37, gatehouse 18 / 15, but
  `legion_ringpocket` 34 (offset -4: 34). Queued members of unspent band-0 spots in the south pocket held behind the
  arc's engaged bodies.
* Every ring member exempt, and members of unspent band-0 spots exempt: attackring-r200-64 reversals 337.
* Every member of a shadowed ring exempt (the first form, above): the 24- and 32-attacker copies 82 and 124
  reversals.
* A band-0 member exempt when walls cut its straight line to its spot: `legion_ringpocket` 34, wall-100 35 / 35,
  and press walks 70 and reversals 250 on the 32-attacker copy.
* A band-0 member exempt once its last local detour found nothing (`detourCount` > 0 or >= 2, no route):
  wall-64 37 / 37 or 39 / 38, but reversals 330, wall-100 36 or 35 now in reach, ringpocket 37 or 35.
* The exemption only out of reach (a body in the shadow in reach holds, so `pressRoute` gets its held updates):
  wall-64 37 / 35.
* Ringwall's route speed for ring members (short in-sight tails at the cap): statue 918 on wall-64 and 1,507 on
  wall-100, no depth gained.

### Pressing into the shadow (2026-10-21, Legion ~100 round 5, task-l5-ringB)

**Where the queue stood.** With every in-sight spot along a wall taken, the members waiting behind the firing arc of
a shadowed ring (`Ring::shadow`: walls hide the target from part of the ground in reach; blind rings too) stood a few
pixels out of reach for good. A held body's presses (pressIn) went only to a point in sight, or, on a blind ring,
within its own origin; the walk on through the shadow (`pressRoute`) starts only from a body already in reach. A
body whose own origin lay wholly beyond the reach edge, next to free in-reach shadow, had no way in
(`attackring-wall-64` at offset -3, ticks 2,950-2,999: of 30 held bodies that found no press, 22 stood out of reach,
most walled in by bodies; those beside free ground in reach out of sight had no way onto it). Retail's attacker walks on at its target
until a wall or a body stops it, so its crowd presses up into that ground.

**The change**, Legion only (Retail hash-identical on `attackring-wall-64`, `attackring-wall-100`,
`attackring-gatehouse-40` and `attackring-river-64` at every gate offset):

* **Press into the shadow** (pressIn). A body held out of reach of a shadowed or blind ring, with no point in sight
  a step away, that will stay there (on its own spot, or its spot's centre out of reach: the blind press's `stays`)
  presses to the first point `kPressDepth` inside reach of its own origin or of a free neighbouring origin. Into a
  neighbour only where no moving member stands in or next to that origin: a body walking past took it back before the
  press arrived, and the press was tried again from the same origin (`attackring-wall-64` offset 3: 46 repeats, one
  body). A press already at its point (out of reach by the sub-pixel the whole-pixel test drops) is no press (it ended
  at once and was tried every update: gatehouse offset -3, 29 repeats).
* **Hold in reach out of sight.** A member of a shadowed ring standing in reach out of the target's sight that has
  pressed in at the target during this order (`Member::shadowPressed`, set by any pressIn step on a shadowed ring)
  holds there rather than walk to a
  goal centre out of reach, as a blind ring's already did, and its hold walks on through the shadow to the shot
  (`pressRoute`). Without the hold the pressed bodies walked back to their wait spots: gatehouse reversals 267 -> 495
  and wall-100 227 -> 334 (gate medians), wall-64 now in reach 38 -> 37. Only a pressed body (review): holding every
  member standing in reach out of sight also parked bodies merely walking through the shadow strip toward spots beyond
  reach, for good, in the way of the bodies behind them. On `attackring-wall-mixed-56` (the wall-64 ring attacked by a
  40-body 1x1 group and a 16-body 2x2 group at once) the target's damage over the 11 offsets went 15,177 -> 15,033 with
  every such body held (offset 0 1,304 -> 1,224, its longest out-of-reach hold 400 -> 1,641) and is 15,199 with only the
  pressed ones (Retail 12,470; group A's now in reach 288 -> 286 -> 295); wall-64's damage sum rose with it, 18,646 ->
  18,724 (offset 2: 1,781, before round 5 1,843, now 1,819). The mark lasts the order: tied to the goal instead, a
  pressed body whose spot was handed on walked back out and pressed again from the same origin (wall-64 offsets 2 / 3:
  4 / 3 repeats, `ctest:legion_pressin`). `ctest:legion_ringshadowmixed` (offsets 0, 2, 3: damage median >= 1,330;
  1,295 with every body held, 1,355 now).

Gate medians (sums over the 11 offsets in brackets), Legion 47c09924 -> after, Retail:

| fixture | key | before | after | Retail |
|---|---|---|---|---|
| attackring-wall-64 | reach.ring.ever_end / now_end | 39 / 37 (479 / 469) | 39 / 38 (490 / 486) | 45 / 42 (532 / 480) |
| attackring-wall-64 | now_end at the walled offsets 1 / -1 | 55 / 54 | 61 / 58 | 55 / 54 |
| attackring-wall-64 | damage_end / damage_t2400 (sums) | 18,612 / 12,852 | 18,724 / 12,964 | 16,912 / 11,139 |
| attackring-wall-64 | t50 / reversals / statue / wall touch | 1,501 / 317 / 48 / 32 | 1,313 / 142 / 48 / 26 | 1,429 / 195 / 173 / 20 |
| attackring-wall-100 | reach.ring.ever_end / now_end | 38 / 37 (476 / 466) | 39 / 38 (489 / 479) | 49 / 41 (609 / 518) |
| attackring-wall-100 | damage_end (sum) / reversals / statue / wall touch | 18,440 / 292 / 143 / 15 | 18,532 / 227 / 161 / 18 | 17,027 / 455 / 229 / 17 |
| attackring-river-64 | reach.ring.ever_end / now_end | 17 / 17 (287 / 286) | 20 / 20 (309 / 309) | 21 / 14 (316 / 288) |
| attackring-river-64 | ever at offsets 0 / -1 / -2, reversals at -2 | 17 / 30 / 45, 1,911 | 20 / 36 / 53, 59 | -, - |
| attackring-gatehouse-40 | reach.ring.ever_end / now_end | 18 / 15 (213 / 171) | 19 / 16 (225 / 182) | 29 / 24 (298 / 264) |
| attackring-gatehouse-40 | damage_end (sum) / reversals / statue | 320 (4,834) / 221 / 17 | 320 (4,849) / 221 / 21 | 584 (7,172) / 314 / 23 |
| attackring-wall-mixed-56 | ever / now (sums), damage_end (sum) | 25 / 25 (294 / 288), 15,177 | 26 / 25 (303 / 295), 15,199 | 247 / 212, 12,470 (sums) |

Every other ring fixture is hash-identical at all 11 gate offsets (r200-64, f1-16, f1-64, f2-64, squad-64, mob-40,
mob-100, struct-60, fight-60, fight-diag-60, guard-64, ringcross, ringcross-100, attack-100): their rings have no
shadow. `ctest:legion_ringshadowpress` (wall-64 offsets 1, -1, -3: median now in reach >= 57; 54 before, 58 after).
`ctest:legion_pressin` keeps no repeat at any gate offset.

**Not cured: the gatehouse plug.** The damage gap is the west-gate offsets' plug (0, +-1, 2, 3: 167 / 332 / 84 / 152 /
368 against Retail's 557 / 482 / 590 / 750 / 526), where the corridor is single file for its 2x2 bodies. A sight map
of offset 0 (origins in reach and in sight, from ringScan's own test) shows why the first bodies stop in the mouth:
the origins flush with the yard's west wall see the target through it (the sight cast skips the cell beside a 2x2
body), the ones a cell off it do not, and the field's shortest way along the corridor is the flush column. Combat
brakes the first body there, before its spot, and the gate behind it is shut. Retail's bodies walk the corridor out
of sight and are braked further in. Tried and not kept ("shot avoidance"): a member of a shadowed ring on its way to
an unreached band-0 spot within two bodies of reach steered down the field, flowed round and walked straight lines
only through origins without the shot (its own spot excepted), and did not press into the shot for 90 held ticks.
The first body then walked the out-of-sight column, but met the next one head on: a body bound for a spot beyond the
yard reaches the nearest seed, where band 0's field is flat, and walks its seed set toward its own spot, which in a
walled ring points through the wall (offset 0: it walked in place for 2,400 ticks and plugged the south corridor).
Damage over the offsets 4,834 -> 5,190, but offset 0 167 -> 83, the median unchanged at 320, and wall-100 lost 2
ever in reach. A fix needs the ring's spots filled deepest first and a way to a far spot through a flat band-0 field,
not steering alone.

### Fight rings and the reach edge (2026-10-21, Legion ~100 round 5, task-l5-fight)

Judged by damage, not reversals (the user's ruling: fight jitter matters only where it costs damage). **Where the
damage was lost** (offset 0, per attacker per tick: in reach by the observer's test, moving, ordered at another
target, the weapon ready but not firing, and the shots counted from its reload). On `attackring-fight-60` Legion
had 69,083 body-ticks in reach and fired 2,319 shots, Retail 79,873 and 2,677. In both one shot per 30 ticks in
reach, the only in-reach ticks moving are the braking tick (40 against 50), none in reach on another target, none
with a ready weapon held. No damage was lost to shuffling, retargeting or reloads reset by movement; all of it was
time out of reach: bodies in reach by tick, sampled every 100 ticks from 800, Legion 3 6 10 14 18 20 25 28 32 37 39
40 (then 40 to the end) against Retail 1 4 10 16 22 29 36 41 44. Two causes:

* **No ring for a fight order.** Its column acquires the target one body at a time (round 4's note: the first
  joins at t=694 alone), so `startField` saw one member and built none. Down the point seed the column queued on
  the near arc. On `attackring-fight-diag-60` (offset 1) acquisition times match Retail's (the 15 quantiles 1,157 ..
  1,536 against 1,161 .. 1,549), but the later half took 201-435 ticks from acquisition into reach against 168-352.
* **A held body 1-2 px out.** At the end the ring stood one body thick at 94-96 px in both builds, but Legion held
  6-7 bodies at 96-98 px against Retail's in reach there. `pressIn` wanted a point a sixteenth of a pixel inside
  reach. A body flush on the edge of its origin, exactly at reach (the nearest point of the cell's box, an integer
  distance on an axis), found no point, while combat brakes and fires at that distance (`dist > reach` is the
  out-of-reach test). In the diagonal fixture at offset 1, two of Retail's south arc stand on the top edge of the cell
  row 96 px below the target. Legion's bodies held a cell further out.

**The changes**, Legion only (Retail hash-identical on every fixture of the screen below):

* **A late ring** (`ringScan`, `Ring::late`, hashed). An attack group of `kLateRingMembers` (8) or more round a
  target standing still, with no ring, a finished field and no refresh pending, plans its ring once (`buildRing`
  as before) and refreshes to band 0; members claim spots as they next move. A chase is left alone. At two members
  every small battle group re-planned (whole-run Legion work at offset 0: battle-field-2x60 +29%,
  battle-field-2x250 +48%, battle-assault +34%); at eight it is the parent's (+0.6%, +0.5%, -1%) and the cost gates
  pass unchanged.
* **The reach edge is in reach** (`pressIn`, `reachFloat`). A candidate point is tested as `tickCombat` tests it:
  its float distance to the target against the float reach. The blind press (nothing brakes the body) keeps its
  `kPressDepth`. A melee weapon is not radial (`meleeInRange`: footprint adjacency, its TDF range ignored), so
  `reachFloat` returns 0 for it and a melee body keeps the integer test against `reachPx`'s contact distance. As
  first landed it read the TDF range (300 px for araknigh, zonwolf, lifwolf, zontroll): every point was in reach,
  a held body re-pressed to its own origin for ever (`attackring-melee-30` at offset 0: 2,453 of 2,454 presses
  repeats, damage 28,153 -> 14,433, Retail 27,351), and a melee weapon ranged below contact (the Sailor's 10) found
  no point at all. With the melee carve-out the four melee fixtures of the review (attack/fight x 30/60 araknigh)
  are hash-identical to 47c09924 in both modes; `ctest:legion_meleepress` guards it.
* **The stop origin is the point's own origin** (`ringScan`'s spent test). The reach-edge point radially out from a
  band-0 spot was turned into an origin as if it were a cell's centre, `(e - f*8) >> 4`, which is half a cell toward
  -x/-z: on the north and west arcs the stop origin could lie a cell out, out of reach and free while the arc in front
  stood full, so the spot behind it never read spent. Now `footprintOrigin`'s `(e - (f-1)*8) >> 4`. With the edge
  change alone, the spares of `ringcross-100`'s ring owned such spots and held on the north edge in the Move
  group's lanes (gate median t90 3,841 -> 4,066, `ctest:legion_ringwait` failed); with this, 3,851.

Gate medians, before (47c09924) -> after, Retail:

| fixture | key | before | after | Retail |
|---|---|---|---|---|
| attackring-fight-60 | damage_end / damage_t2400 | 2,372 / 1,561 | 2,867 / 1,967 | 2,814 / 1,934 |
| attackring-fight-60 | ever_end / now_end / t50 | 40 / 40 / 1,505 | 44 / 44 / 1,212 | 44 / 44 / 1,206 |
| attackring-fight-diag-60 | damage_end / damage_t2400 | 1,729 / 910 | 2,070 / 1,172 | 2,167 / 1,287 |
| attackring-fight-diag-60 | ever_end / t50 | 41 / 2,002 | 44 / 1,733 | 44 / 1,631 |
| attackring-fight-60, -diag-60 | statue_ticks / reversals | 4 / 46, 3 / 51 | 178 / 83, 199 / 106 | 38 / 86, 23 / 84 |
| fight-retarget | g.F.t90 / done | 2,293 / 2,684 | 2,208 / 2,631 | 2,638 / 2,730 |
| attackring-f1-64 (= squad-64) | damage_end / ever_end | 2,672 / 39 | 2,919 / 44 | 2,612 / 45 |
| attackring-mob-100 | damage_end / ever_end | 2,775 / 42 | 3,026 / 47 | 2,688 / 44 |
| attackring-mob-40 | damage_end / ever_end | 2,639 / 38 | 2,640 / 40 | 2,438 / 39 |
| attackring-r200-64 | damage_end / ever_end | 4,164 / 61 | 4,207 / 63 | 3,717 / 64 |
| attackring-f1-16 | damage_end / reversals | 1,102 / 13 | 1,112 / 3 | 1,078 / 3 |
| attackring-f2-64 | damage_end / ever_end | 1,059 / 16 | 987 / 15 | 912 / 14 |
| attackring-wall-64 | damage_end / now_end / reversals | 1,896 / 37 / 317 | 1,988 / 38 / 144 | 1,909 / 42 / 195 |
| attackring-wall-100 | damage_end / ever_end / reversals | 1,911 / 38 / 292 | 2,027 / 37 / 173 | 1,922 / 49 / 455 |
| attackring-gatehouse-40 | damage_end / ever_end / now_end | 320 / 18 / 15 | 302 / 14 / 14 | 584 / 29 / 24 |
| ringcross | damage_end / ever_end | 6,250 / 41 | 6,816 / 46 | 6,435 / 45 |
| ringcross | g.M.t90 / done | 3,821 / 4,066 | 3,661 / 3,989 | 3,880 / 4,293 |
| ringcross-100 | damage_end / ever_end | 6,371 / 42 | 6,493 / 44 | 6,458 / 43 |
| ringcross-100 | g.M.t90 / done | 3,841 / 4,231 | 3,851 / 4,298 | 3,926 / 4,166 |
| attack-100 (a chase) | damage_end / g.E.arrived | 2,054 / 2 | 2,417 / 4 | 1,880 / 6 |

Hash-identical at every gate offset: `attackring-guard-64` (a guard has no ring and no damage to deal there; its
proximity keys are round 4's), `goalblock-fight-200`, `aware-attack`, `mixed2` and 19 of the 22 legion100 fixtures
(corner-1x100 and corner-4x25 differ at 2 offsets with equal medians; ringcross-100 and attack-100 above).
Trades: on the fight rings statue ticks and reversals rise toward and past Retail's (more bodies press the last
pixel; cosmetic by the ruling). `attackring-gatehouse-40`'s median ever in reach falls 18 -> 14 (its sums over the
11 offsets are level: damage 4,834 -> 4,854, now in reach 171 -> 171), `attackring-f2-64` (2x2 bodies) deals 72
less, still above Retail. `ringcross-100`'s Move group completes 67 ticks later (+1.6%), its t90 10 later.
`battle-assault` reverses 1,515 -> 1,531 and its group losses move by up to 6 bodies either way;
`battle-field-2x60` A loses 26 instead of 27 (Retail 37).

Overlap with other work: the edge test is inside `pressIn` (press-in, the settle-into-reach area) and the stop
origin inside `ringScan`'s spent test (ring upkeep). Ring planning (`buildRing`'s bands and spots) and
`shadowWalk` are untouched.

Tried and rejected (gate medians):
* The late ring at two members (the first form): fight-60 2,879, diag 2,140, but the battle costs above. At four:
  2,872 / 2,087, +4-5% battle work. At sixteen: fight-60 2,803 (43 in reach).
* The late ring sized for `kRingCells` spots instead of the members it has: the same medians (2,655 against 2,664).
* The stop point a pixel inside reach as well as the origin fix: diag 2,137, mob-100 3,050, but f1-64 2,833 (42 in
  reach), wall-64 1,983 and ringcross-100 41 in reach (damage 6,243).

`ctest:legion_fightring` (fight-60 offset 0: half in reach by tick 1,300; 1,526 on the parent, 1,510 without the
late ring) and `ctest:legion_ringedge` (f1-64 offset 0: 43 or more in reach at the end; 39 on the parent, 41 without
the edge test). `ctest:legion_ringwait` fails with the edge test but without the stop-origin fix.

### Paused first builds (W4 C1, protocol 244)

A first field used to build to done, its whole window, although its members
steer by it as soon as the A* frontier (aimed at the group's bodies) has passed
their cells. Now a first field of a group that claims no packed per-goal slots
(no goal shared by two of its members) and does not pinwheel (no member's part
of 16 or more) builds in slices of 4096 relaxations and **pauses**
(`Field::covered`, its frontier kept) once every live member's cell is settled
with a margin: `kCoverAhead` (40, eight orthogonal steps of key) plus two
diagonal steps per cell of footprint + 2 (the settle rule's ring), plus, for a
member of a formation, two orthogonal steps per cell of the formation's radius
+ 3 bodies (its lane runs that far off the shortest way); a goal several members
shared once also needs its packed disc settled. A member whose cell is under
its margin (it strayed, side-stepped, joined late, or shares the field with
another group) adds the cell to the field's `demand` and marks the group
waited, and the waited loop resumes the build until each demand cell has its
margin (`work.paused_resumes`; `legion_world_test pausedemand`: a body sharing a
paused field from outside it waits 2 ticks). A paused group leaves
`buildingIds`. Slot groups' first builds and every refresh still run to done;
a stale paused field is refreshed as a finished one would be.

A* with a consistent heuristic never changes a settled potential, so what a
paused build has settled is final (checked under `TAK_LEGION_VERIFY` in
`advance`; a soft view that changes between slices only raises step costs, so
it cannot lower one either). The readers that used to wait for `done` and now
take a paused build (`Field::usable`) read it through `Field::known`, which
returns a potential only where it is settled: the settle rule (`settleWindow`,
`pressed`, `areaBound` -- not cached for a paused build -- and `rechoose`), the
lift walk, the aim memo (keyed on the field's work count, so a resumed build
invalidates it), eviction, `sharedField`, and a formation's slot hand-out
(`claimSlot` for a member whose only slot is its formation's: it reads no
potential). The pinwheel, `awareScan` and the aware replan keep `done`: a
paused group plans round no crossing traffic until its field finishes. (Since
Legion 10,000 stage 5 none of them waits for `done`: they state the region they
need and read it through certainty checks; see that section below.) Field
descent (`aimCell`, the drive and detour code) may read a tentative potential
beyond the margin; descent stays sound on it, as on a half-built field.

With B1: a paused build is a finished field to the blocked re-request (read
through `known()`, `freeDescent` too) -- counting every member stalled on a
paused build as blocked refreshed `legion_staticidle`'s parked guards
(12728 relaxations instead of 0). And a group whose member waited on its paused
build in the last 2 ticks is **active** (that member neither moves nor counts as
blocked, and its stale paused build is what it waits for). B1's
`work.demand_resumes` keeps counting resumed suppressed refreshes only; C1's
resumes of a paused build count `work.paused_resumes`.

With W5 (protocol 245): an attack or guard group's first field is never paused. The ring of a reach
group is planned from a done field (`ringClaim`); a paused field never finishes while its members are
covered, so the ring came late or not at all (attackring-r200-64 `reach.ring.ever_end` 57 -> 37 on the
first merged head). Reach groups build to done, as on the W5 head.

### Livelock: the 0.8.1 walk-in-place (protocol 257)

The user's 0.8.1 recording (The Upper Humber, Legion, Crusades balance) showed
Zhon units walking on the spot for up to 72 s after 17:00: 24 episodes of a
second or more, 15 units, about 340 unit-seconds by the replay's rule (GET 29
above 5 while the body is under 8 px from where it was 30 ticks earlier, for
30 ticks or more). Each was a livelock of 1-6 tick cycles with 0.1-1 px steps:
- **Arc flips.** A formation member ranked at a real wall end (its `pivotR`
  lasts the order) took every scattered single tree in its 48-cell look-ahead
  for a wall end: the arc point `pivotR` cells off the tree switched sides
  between neighbouring origins, so the body turned left and right at an eighth
  of its speed, stepping back over the cell edge each time.
- **Side-steps that undo each other.** Blocked, a body flowed round toward
  its arc point; on the next cell `pivotAim` found none and the field's
  measure picked the cell just left, two or five ticks a cycle.
- Neither ever held, so the hold's ways out never started, and the old
  per-tick still rule counted every step of a pace and every turn as headway.

User decision (2026-10-13): "ignore single trees and rocks", with the
livelock backstop. What changed, Legion only (Retail is bit-identical):
- **Lone trees and rocks are no wall ends** (`wallOrigin`, `loneCell`). A
  blocked cell counts for the pinwheel's wall contact, outward normal and free
  width only when its run of blocked cells is not all blocking features of
  TDF category trees, plant(s) or rocks (`RetailMapFeatureType::treeOrRock`)
  inside `kLoneSpan` x `kLoneSpan` = 7 x 7 cells (the largest shipped tree or
  rock, ararock07, is 7x6; trees reach 5x5). Terrain, water, structures, the
  map edge or any other feature in the run make it a wall. A run links the
  blocked cells the plane's body cannot pass between (at most `footX` columns
  and `footZ` rows apart, so their illegal origins touch): a forest or hedge
  whose gaps are narrower than the body is one run and stays a wall, and so
  does a tree that close to a cliff, a building or the map edge. Most shipped
  trees stand apart (96% of the tree and rock cells on the 1,367 maps of the
  test install are lone by 8-connected runs), but 34% of those lie in such
  forests for a 2x2 body (59% for 3x3, 74% for 4x4): taken as 8-connected
  runs (the first cut of this change, never released) they made every such
  forest no wall at all, and `legion_forestedge`'s formation filed round the
  end of a strip of 2x2 trees with 1-cell gaps at 1.23 files abreast (5.49
  now, 5.37 on 9f677c87). Bodies path and collide round a lone tree as
  before. No hysteresis on the arc point: far
  flips (an arc point more than 12 cells from the one of the neighbouring
  origin just left) fell from 21,997 to 1,953 in the recording's situations
  below and from 1,812 to 0 in `legion_lonetrees`; what is left is real wall
  geometry at its old rate (the 131 fixtures 6,909 to 5,270, `mazeform`
  1,479 to 1,390; Legion, offsets 0 and +-1). Keeping the old arc point while
  it was still ahead in a straight legal line cut those flips to 1,255 but
  raised walk-in-place from 1,380 to 2,718 unit-ticks (`corner-8x56` 75 to
  1,018: a kept point pursued after the formation had wheeled past it), with
  arrivals equal, so the backstop alone handles the flips by real walls.
- **A side-step toward the arc is not undone straight back.** Blocked again on
  the cell such a flow-around took it to, a body does not flow into the cell
  it came from for `kSlideHold` = 60 hold updates (hashed `Member::slideCell`
  / `slideFrom`); it holds, and the hold's own ways out come first. Every
  livelock of this kind in the recording was arc against field. Barring every
  undo instead (any flow-around, or the previous origin of any step) cost
  `ringcross` its Move group's arrivals (40 -> 35 of 40) and stood bodies
  against landed flyers; keeping one measure per run of flow-arounds (the next
  one measured by the arc point the last one aimed at) tripled the corner
  fixtures' lane crossings (`corner-1x448` 69 to 195, `corner-8x56` 39 to 148,
  summed over the gate offsets); committing side-steps to the cell centre
  would only enlarge any cycle left to a whole cell. Without this guard the
  backstop alone leaves a 30-tick to-and-fro in `legion_slideback`.
- **The backstop** (`World::updateLegionStill`, `LegionHeadway`): a body
  stands after 8 ticks without NET headway (above), and a window that stands
  although the body walked or turned to and fro latches a livelock
  (`Unit::legionShuffle`). While it lasts the body neither flows round
  blockers, nor follows its arc, nor takes a lane pass (in the recording's
  situations a pass a cell over, undone by the field's next steps, made a
  42-tick cycle of its own), so it holds or walks down the field and the
  hold's ways out start; it ends when the body is a body length clear of where
  it began (16 px per footprint cell plus one cell), not on the next headway
  (a side-step crept for 50 ticks and undone by a dash back clears the
  window's reach on every dash), or after `kLock` = 64 ticks (a 120-tick latch
  stood the foot-3 landed-flyer crowd 235 member-ticks against the flyers, base
  159, past `legion_landedflyers`' band). A held or merely creeping body is
  still but no livelock. Steps that do not turn the body (side-steps, routes,
  shuffles) and a standing body request no turn of its scripts (`turnReqBam`
  0), so the display VM sends no `TurnDirection` (the zontroll / zongob
  `MoveWatcher` walks on its last argument).

Measured (synthetic 2x2 movers round the pinwheel fixture's wall end, the
`lonetrees` world on varied layouts in a scratch build; to-and-fro
member-ticks: the replay's rule, with a path of 8 px or more coming to a
third of it or less, or 90 degrees of turning keeping a third or less):
- Scattered single trees after a ranked wall end (112 layouts: a 1x1 or 2x2
  tree every 5-12 cells, 8 seeds): 50,029 on 9f677c87, 249 now; every body
  arrives in both; last arrival 3% earlier (median).
- The same with the trees made walls (the backstop alone, 96 dense layouts, a
  tree every 4-6 cells): 20,340 to 604; arrivals 4,600 of 4,608 in both,
  all-arrived in 93 of 96 in both.
- Wall end only, 168 layouts (16-64 bodies, 42 goals): 3,861 to 0; arrivals
  equal (167 of 168 all-arrived in both); last arrival median ratio 1.000.
- Regression tests: `legion_lonetrees` (offset 0 on 9f677c87: 2,325
  to-and-fro member-ticks, 1,841 walking, 9,225 of arc steering among the
  trees; now 0 / 0 / 0 at five offsets), `legion_slideback` (211 and 180, now
  0), `legion_stillturn` (22,008 standing member-ticks requested a turn, now
  0), `legion_forestedge` (a forest is a wall: above) and `legion_still` (the
  rule on the recording's motion and on genuine slow
  motion: a walker at a pace, a speed-matched follower, slow pivots, a start
  from rest; 9f677c87's rule stands none of the four walk-in-place cases).
- The recording's own moments, cut as situations (`TAK_SITUATION` on the
  0.8.1 build, ticks 30200 to 38200 = 16:46 to 21:13, each the world and its
  next 2700 ticks of commands, run 3000 ticks, Legion, offsets 0 and +-1, the
  observer's `walk_net`, the replay's rule): 25,719 walk-in-place unit-ticks in
  200 runs on 9f677c87 (857 unit-s), 1,007 in 16 now (34 unit-s). The cut at
  18:50 went from 9,793 to 36, 19:13 from 6,147 to 36, 19:43 from 3,948 to
  126; the most left is 503 at 17:16 (Goblins in a crowd, cell (153, 118)).
  The rule reads GET 29, which the stand now zeroes for every body without
  net headway, so it counts what the player sees, not motion. The motion keys
  of these combat cuts moved a few percent either way: arrivals 2,314 on
  9f677c87 and 2,261 now, left behind 2,933 and 2,984, reversals 2,384 and
  2,294.
- The scenario set (131 fixtures, Legion, offsets 0 and +-1): walk-in-place
  88,443 to 1,380 unit-ticks. At the gate offsets (both modes, every key)
  Retail is identical to origin/main record for record. Legion's motion keys
  move both ways within the old per-offset spread (summed medians: reversals
  7,266 to 5,709, backward steps 281,418 to 254,718, arrivals 11,525 to 11,542,
  left behind 5,145 to 5,117, completion distance max 6,803 to 6,894). 461
  Legion keys left their gates (389 bands, 59 offset spreads, 6 Retail floors,
  5 bounds, 2 new counters; 449 of the values inside the base's own range of
  start offsets) and 20 that failed now pass; the baseline licenses, excepts
  (cluster `LL-257`) or re-bounds each with its numbers. The cost is lane
  crossings: 6,578 to 7,117 over every fixture and gate offset, 529 of the
  rise in `island-1x150`, whose strip and abreast crossings go from 10 and 9
  to 55 and 43 (2,187 to 2,637 in the lane), from the side-step guard: a body
  that may not step back takes another field step inward or holds and
  side-steps (without the guard 3, 3 and 2,278). The six Retail-floor
  exceptions and the lane-crossing rise were accepted by user decision
  (2026-10-14).
- The crowdbench screen (86 rows): the 43 Retail rows are identical; Legion
  settles 29,300 arrivals against 29,219, spins 40,716 unit-ticks against
  41,981, and 65 units are ever terrain-stuck against 61.
- `--mpai` Inner Circle seed 1 (twice each): Legion 300 s `70e826bb0db70ced`,
  60 s `55b0eec316e45071`; Retail unchanged (`95b05c8290b55317`,
  `84fdef37bbb3606e`).

The observer gains `walk_net` / `walk_net_runs` (the replay's rule, exact GET
29) and its `walk_in_place` now waits two headway windows (16 ticks) before it
counts.

### The 2026-10-14 review (protocol 264)

Six items of a review of Legion's movement code, implemented one after the
other on one branch, each proved by a test that fails on its parent, and
landed together as protocol 264. Legion only: every Retail record of the
scenario set, the Retail navigation golden and the `--mpai` hashes of both
modes are unchanged.

| Item | Change | Proof (fails on the parent, passes here) | Legion fixtures moved |
|---|---|---|---|
| 6 | 32-bit field potentials (section 2) | `serplong.scn`, `legion_longroute`: two bodies at potential 87,356 stood terrain-trapped; they arrive | none of the 131 existing |
| 1 | progress in one measure at a time; the detour rules keep their own record (section 4) | `legion_progressclock`: `battle-field-2x60` `stall_stale_ticks` 9,401 -> 0 (0 on every fixture at every gate offset, now a baseline key) | 21 |
| 2 | the settle rule reads a body of another footprint where it stands (section 5, Settling) | `legion_mixedsettle`: one 2x2 among 4x4s and one among 3x3s never completed; all do | 4 |
| 4 | slots by arrival at a narrow entrance (section 5, Slots by arrival) | `farroom.scn`, `legion_farroom`: never done at any gate offset; done at 9 of 11 | 19 |
| 3 | passage lanes ("Passage lanes" above) | `legion_passagelanes`: `gap14` swapped 17 to 47 bodies across the gate (median 27); none | 15 |
| 5 | one formation slot-search budget a tick over every point, round robin ("Slot hand-out budget" above) | `legion_handout`: `deadend-multi-4x120.scn` walked 602,176 ring cells in one tick; 134,621 | 4 |

Judged together (Legion, the gate offsets as `legion_check` reads them; each
item against its parent over the fixtures both share, the whole review over
the 131 fixtures of the base):

| | arrivals | complete orders | left behind | never | lane swaps | per file x100 (gaps) | stopped permille | transit held permille | walk_net | Legion work |
|---|---|---|---|---|---|---|---|---|---|---|---|
| item 1 | +13 | +10 | -13 | 0 | -1 | 0 | +1 | 0 | 0 | -46,271 |
| item 2 | +1 | +1 | -1 | -1 | 0 | 0 | 0 | 0 | 0 | -1,029 |
| item 4 | +127 | 0 | -127 | -7 | -4 | 0 | -299 | -275 | 0 | +346,018 |
| item 3 | +4 | 0 | -4 | 0 | -133 | -2,127 | -60 | -88 | 0 | +78,806 |
| item 5 | +8 | +11 | -1 | -2 | -1 | 0 | -7 | -6 | +34 | -11,031,813 |
| review | 11,151 -> 11,304 | 12,176 -> 12,198 | 5,117 -> 4,971 | 336 -> 326 | 571 -> 432 | 2,723 -> 596 | 25,394 -> 25,029 | 20,023 -> 19,654 | 369 -> 403 | -0.68% |

Walk-in-place stays 0 on every fixture. Three of the W9 hard gates the user
accepted as known limits (W9-1) are now met: `strait-2x150`'s lane swaps (3,
Retail 8; gate 2), the per-file swaps of `gap6`, `gap8`, `gap10`, `gap14` and
`gapsweep`'s g6 to g14 (none; gate 5, which `gap20` and g20 still miss at 1.73
and 4.23 per file) and `doorplug-124` A at every gate offset (gate 9); 16
Retail-floor exceptions pass again. The losses inside the totals, which the
scenario baseline licenses at the head's values with their numbers and items
for the lead's ruling (`docs/legion-exit-tables.md`, "2026-10-14 review"): from
passage lanes, `gapsweep`'s G8 group done at 2,884 ticks before and never now
(it misses at 6 of the 11 offsets against 1, one to four bodies settling
outside the disc), `gap8` back steps 54 -> 81 and reversals 6 -> 11 at the
destination, `gap14` aim reversals 86 -> 101; from the stall clock,
`corner-1x448` B done 8,328 -> 8,958 ticks (offset -1 now leaves one body of B
short) and `pocket-615-wall` A's farthest completion 71 -> 72 cells; from slots
by arrival, a reversal more in three dead-end rooms and 14 -> 16 in
`deadend-w6-n120-room-squad`, `deadend-w4-n120-room-plain` wall touch 264 ->
290 permille, `deadend-w2-n120-room-plain` median completion distance 8 -> 9
cells and `doorplug-124` door files 3.17 -> 3.22 abreast; from the slot budget,
`corner-8x56` A's farthest completion 31 -> 34 cells and A done 7,116 -> 8,057
ticks (A no longer completes at offsets +2 and +3, while B and C, never on the
core five before, now complete there; walk_net 0 -> 34), and
`battle-field-2x500`'s fight, which reshuffles per offset (left behind 2 -> 7
for one more arrival; it passes its gates). The fixtures the review added fail
the Retail floor on wall touch (`farroom`, `serplong`, `serplong-control`: the
dead-end class MV-02) and `deadend-multi-4x120` on its B group's arrivals (13
against 21: the `deadend-w4-n120-closed` probe, whose own arrivals are the
AR-02 exception); they are excepted with the trades.

Not taken, with the evidence (each in its item's commit and above):
restarting the detour back-off on the stall clock, after a gain of 2 or 4
cells, or halving or stepping down its count on a gain (item 1: every variant
trades detours against standing samples in `battle-assault`; Known
weaknesses); reading a body of another footprint at every origin that overlaps
it (item 2: frees the same orders but settles more bodies short of the click,
`mixedfootprints` 500x4 67 outside the radius against 55), or at its potential
with a pixel tie-break (frees 1 of 15); a slot assignment fitted to the
passage's order and a shared lane record (item 3: the census found pairs whose
slots lay in the gap's order swapping as often as the rest), and an exchange
of slots at the passage exit (better tails, 12 more swaps); a budget for the
steering probes and detour searches (item 5: with the hand-out bounded, the
slowest ticks are the field quota's; awareness, lane and pinwheel work add at
most 36,879 cells to a tick's peak, detours 3,880); a saturation flag instead of
32-bit potentials (item 6: the widening's whole cost is 4 bytes a field cell,
39 MB of peak heap on the 8-AI Ulasem replay).

Review findings fixed before landing: `legion_check` kept `work.legion_all`
report-only in `check` but would have taken it into the baseline at a retake
(it now never does, `legion_baseline_reasons`); an entrance is one footprint's
(section 5, Slots by arrival: documented, as no fixture takes one with mixed
footprints); the slot budget's first count of moved fixtures came from a stale
parent sweep (4, not 6). The automated reviews of the rebased commits found
two more: slots by arrival exchanged slots across movement planes, so a
ground body could take a hovercraft's slot in water (now one plane only,
`ctest:legion_entryplanes`; no fixture changes), and `work.legion_all` left
out the pinwheel's own reads (now in); a third, the passage lanes' guard
against a very large destination area, is a documented limit ("Passage
lanes"). Baselines: the scenario baseline's Legion hashes
retaken from a full gate-offset run of both modes, the four new fixtures first
baselined, `stall_stale_ticks` gated at 0 on every Legion record, the moved
work and gate-instrument keys licensed and the offset spreads the review
opened excepted (cluster `LR-264`); the behaviour losses and the new fixtures'
floor failures in a commit of their own; the Legion navigation golden
regenerated (Retail's unchanged).

### Legion 10,000 stage 2: exact join index, awareness broad phase (fd34151a .. e290f0ed, no protocol change)

Four hash-identical optimisations (docs/legion-10000-progress.md, stage 2, has the proof runs and numbers):

* **Join index.** `registerMove` used to walk every live group for the first one a member may join. Groups are
  now indexed by the equality part of the join predicate, all fixed at creation: player, plane, kind, approach,
  and the ring target and bucket for a reach kind or the issue key otherwise (`joinIndex`). The member walks its
  entry in ascending id order and applies the whole predicate unchanged (component through `groupComp`, goal
  box, seeds, field), so the first group to pass is the first of all groups. `TAK_LEGION_VERIFY` runs the
  brute-force walk on every join and checks the index against a scan of the groups.
* **Awareness broad phase.** `awareScan` used to run its 31 conflict samples for every (group, mover) pair. A
  mover not yet planned round enters a plan only through a sample where the group's way point (on its descent
  chain, inside the chain's box of cell centres) and the mover's box centre (inside its swept box over the
  450-tick horizon) are within `(half + max(tail, head) + 66) + 2 rG` px in each coordinate: `s` and `q` divide
  by `L`, which is at most the heading's length, so the offset along and across the heading is under `B+1` and
  `A+1`. Movers are listed in a 1,024 px grid over their grown swept boxes; a group reads the cells its chain
  box grown by `2 rG` covers, and every mover already in its plan is a candidate whatever its place (a crossing
  stays planned off its box). Candidates are visited in the original mover order, so the plan and its order are
  unchanged. `TAK_LEGION_VERIFY` samples every dropped mover and fails on a conflict.
* **The vertex probe is a tool instrument.** The W8 step 1 vertex probe (`vertexInstrument`), which no decision
  reads, runs only while `LegionNavigator::setInstrumentProbes` is on: `legion_scenario` (unless
  `--no-vertex-probe`) and `legion_world_test` turn it on, the game does not. It was 5-7% of a 10,000-body tick.
* **Gate memo.** `gateCommit`, asked every tick for every uncommitted lane member, declines in open ground
  before it reads any soft body. Those checks read only the static plane, the finished field, the member's goal,
  origin, footprint and lane band; `Member::gateMemo` keeps them when a call declines there and the same inputs
  decline again without walking the chain (never hashed; `TAK_LEGION_VERIFY` recomputes every hit).

The ctests `legion_verify_{battle-assault,crosslong,gap8}` run the references; each file is one that a
deliberate mutation of the matching optimisation trips.

### Legion 10,000 stage 3: explicit selection orders (protocol 267)

A click is now one explicit **selection order** (`net::Cmd::Order`, `net/lockstep.h`): the player's intent over a
whole selection, with an identity (issuing player, serial), a template command (verb, target, point, queue flag),
a shape and the exact membership. Before, the client sent one command per unit through the 64-a-tick drain and the
sim inferred the click from proximity and timing (convoys, below); a 10,000-unit click took 157-308 ticks to land
and became up to 20 groups and several crowds. The record of the stage, with its measurements, is
docs/legion-10000-progress.md section 7.3.

* **Client** (`GameView::ClickScope`, `flushClick`). The commands one input event (or one autoplay step) issues
  are one click. In a Legion net game `net/orderpack.h`'s `packClick` turns each run of commands that differ only
  in their unit into one order; the right-click move is `OrderShape::Offsets` at the click (the sim recomputes the
  per-unit offsets, so no coordinates travel per member). Retail and offline play send the commands exactly as
  before, so Retail is bit-identical. The outbox is one FIFO of commands and orders: commands go under the rate
  credit and the 512 window, an order goes whole as its transaction under its own window (64 orders / 384 KiB
  outstanding, `orderSendWindow`), and nothing overtakes what was issued before it.
* **Transport** (`net/protocol.h`, `server/orders.h`). `OrderBegin` / `OrderMembers` (member ids as varint
  deltas, at most 2,048 a message, strictly increasing, so a duplicate cannot be encoded) / `OrderCommit`
  (count + FNV digest) / `OrderAbort`. The server validates the framing, the template, serials (increasing per
  connection and above the slot's highest activated serial, which a reconnect keeps), counts and the digest;
  malformed fails the connection, as does an order in a Retail game (its clients keep per-unit commands); over
  the byte budget or the window the transaction is dropped with a log line and the sender is told
  (`Msg::InputDropped`, which also counts dropped `PlayerCommands`): the byte budgets are shared with the
  account's other connections and the whole server, so an honest client can be dropped, and a dropped order
  never comes back in a bundle -- without the notice it held its place in the client's window (`OrderWindow`,
  by serial) for good, and enough of them closed the outbox, Stop included (review R1). The windows are
  tested before the budget is charged. A committed order is ONE item of its client's command FIFO; `closeTick` takes it whole while the
  tick's order budget (96 KiB of records, shared by the room, first pick rotating) holds it and the client's
  per-second command budget has room for its members (otherwise it WAITS, never dropped, so the client's
  window cannot leak), and that tick is its activation tick. An ordinary command behind it keeps its place and
  its own 64-a-tick budget, so a big order never makes it wait for command credit. Chunks never reach a
  bundle, so no partial order is ever visible; a disconnect drops the open transaction and the queue with the
  connection.
* **Sim** (`sim/matchsetup.cpp` `applySelectionOrder`). At activation every member gets the template command in
  increasing id order, not the player's selection order (the order is not carried: it would cost a second list
  as large as the members). The verbs whose outcome depends on that order -- Load (the transport's pickup list
  is built in application order) and GiveUnit (a recipient near its unit cap) -- are therefore not order verbs
  and keep the per-unit path in issue order (review R6); every order verb sets only its member's own state, so
  only the order of the members' path requests (registrations) follows id order; ownership
  and existence are checked per member by the ordinary path (a dead, gone or given-away member is skipped).
  Inside `World::beginOrder` / `endOrder` every leg the member being applied gets for itself carries the
  order's identity: `Order::orderSerial` (hashed when set) and, for an order with a click point (Move, Fight,
  Patrol), its arrival area, which the order joins ONCE per class at its own click (`World::stampIdentity`);
  a leg of another verb (an Assist's approach move) or of another unit keeps the per-unit identity. The convoy
  table is now the arrival-area table: several orders of one player and class to one point within its window
  (`kGapTicks`/`kSpanTicks`) still share one area
  -- the A1 decision, wall-4x50's four selections -- while one order is never split; an area an order opens is
  keyed `kExplicitArea | serial` (unique; per-unit commands, the AI's and scripts', still key by first tick).
* **Identity consumers.** Points, groups (issue key), soft-arrival ownership, formation slots, the A2 slot wait
  (`commandOpen`, now by area key), flight stations (bucket key), patrol return legs (copy the identity) and the
  hash key on the area key, which an order now gives all its members at once. The pinwheel's part (`Member::part`)
  stays the tick a leg's command was applied in, so an order (applied whole) is one part however big, and
  orders applied in one tick share one, as per-unit commands of one tick always did (making the part the order
  itself split 24 one-unit clicks of one tick into 24 parts and moved the motion-* fixtures; keeping 64-body rank
  groups inside an order changed nothing that mattered; both rejected). The awareness same-selection rule (one
  player, within `kConvoyTicks`, within `kConvoyCells`) is kept only between per-unit commands; two explicit
  areas are one selection exactly when they are the same area. The first-part provisional formation keeps its
  under-64 rule (taking it at once for any order size was measured and rejected, see `formationSlot`).
* **Settle bound of an explicit area** (`settleWindow`, `crowdBound`). One order is a whole selection, so the
  queue-settle rule (a body one row behind a settled arrival of its own command may settle, C29) let a
  10,000-body order's queue run back to where its bodies started: at the first stage-3 head maze-10000 retired
  7,595 of 8,500 orders far outside the arrival disc, at their starting rect. An explicit area's settle bound
  now never grows past the packed bound of the point's whole crowd (`Point::crowdCells`/`crowdPeak`: every
  footprint plane's bodies, packed in this body's footprint along the field, so a dead end's strip is its
  length) widened to AR-08's arrival disc (`kExplicitSettleNum`(foot+1) / (`kExplicitSettleDen` foot), the
  ratio of the disc's 20(foot+1) px to the packed bound's 16 foot px per sqrt(1.25 n/pi)), plus the slack;
  beyond it a queued body keeps its order and waits, an unarrived body. A crowd of one footprint reuses the
  destination area's own bound (`areaBound`'s cache). Per-unit areas keep the old rule. (A straight-line test
  against the disc itself was tried too and rejected: a dead end's queue is a strip longer than any disc, and
  it stopped those queues' bodies -- 1,174 gate keys failed against 380.)
* **Kept as inference** (`ConvoyTable`, unchanged for them): the AI's commands (server-run, all landing in one
  tick, so the table already gives one area per AI click at one point) and mission scripts' orders.

Tests: `selection_order_test` (codec, packing, Retail equivalence, Legion identity, membership, same-tick
replacement / Stop / queue / patrol, delivery), `server_orders_test` (the transaction state machine),
`server_orders_network` (a real server: whole activation, FIFO without starvation, abort, window, drop notices,
member-budget delay, malformed, Retail refusal),
`convoy_test` (each click shape both as an order and per unit), `issue_selection_test` (the mirror against the
transcribed client), `replay_test` (266 refused).

### Legion 10,000 stage 4: fair scheduling and complete budgets (protocol 268)

Every piece of navigation work that can grow with the army now has a deterministic per-tick budget in its own
units (`sim/navbudget.h`: `NavWork`, `NavBudget`), and the field quota is served by job, class and round robin
instead of group id order. The record, with the measurements, is docs/legion-10000-progress.md section 7.4.

* **Registration** (`requestRegister`, `drainRegistrations`). `registerMove` costs `kRegBase` units plus its join
  candidates and its goal-resolution cells; a window -- one batch of commands between two ticks, or one tick --
  may spend `kRegBudget` (4,096 plain registrations). While the budget lasts and nothing waits, a registration
  runs at once, exactly as before (every gate fixture is unchanged by this). Past it, registrations wait in a FIFO
  that the next `tick()` drains first; while any waits, later ones queue behind it; a waiting unit brakes with its
  order kept and reads as Waiting to the observers; a cancel runs at once and drops a waiting registration. The
  FIFO is hashed while not empty; the budgets never outlive their window, so they are not state.
* **First plane builds** (`planeReady`, `buildPlanes`). A plane whose whole build can never exceed one window's
  plane budget (`kPlaneBuildBudget`, 1 Mi units: maps up to 131k cells) is built at once inside the call, as
  before. A larger one is no longer built inside the registration that first needs it (one-10000: two planes,
  11.7M units, 30 ms of a 39 ms tick): the registration waits, and the same resumable build the background
  prebuild uses runs `kPlaneBuildBudget` units a tick for the wanted planes, in demand order, right after
  `syncStatic`; a plane finished there takes its waiting registrations in the same tick. The FIFO steps over
  entries that wait only for a plane (planes are per mobility class); when a plane is ready, its waiters' marks
  clear (they wait for the budget now, so nothing bypasses them). Neither a building plane nor one a waiting
  registration needs is evicted (`queuedNeeds`; with more classes than `kMaxPlanes` two queued classes used to
  evict each other's plane: `legion_planepin`). A destination area stays open while members of it wait to
  register (`commandOpen`, `regAreas`), so its formation is not sized without them; a waiting unit asked to
  register again keeps its place, and the area it holds open follows its current leg.
* **Awareness** (`awareScan`). The snapshot (movers, groups' accumulators, broad-phase grid) is taken on the scan
  tick as before; the groups are then judged in id order against it, `kAwareBudget` units a tick and at least one
  group, resuming on the next tick when a scan needs more. Under the budget (every measured workload) a scan ends
  in its own tick, exactly as before. A resumed scan judges every group at the snapshot's time: corridor ages and
  stamps use the scan tick, and a mover's displacement is scaled to `kAwareScan` ticks when its two samples are
  farther apart (a scan that ran past the next scan tick).
* **Field service** (`serveFields`). One quota a tick pays relaxations, field initialisation (cells /
  `kInitDiv`, charged when a build starts; a start whose initialisation does not fit the budget left waits for
  the next tick, the first start of a tick always fits) and reach-ring cells. Work is served by JOB -- a field being
  built, however many groups share it -- in four classes, most urgent first: Waiting (a member stands for want of
  it), FirstRoute, AwareRepair, StaleRefresh (the last two inside the refresh allowance, as before; a refresh's
  initialisation is charged to that allowance too). A job holds its field for the whole `serveFields` call: a
  field an install or an eviction drops mid-call is retired at once (out of the totals, no further visit) but
  freed only when the call ends, so no pointer the rings hold is left dangling. A shared paused build pauses only
  if every subscriber holding it may pause (`jobPausable`); an idle paused build takes no window slot. Within a
  class the jobs share the quota by deficit round robin over a window of the class's oldest jobs (`navDrr`:
  `kWindow` 12, the refresh classes `kRefreshWindow` 3; `kSlice` a visit; the cursor kept across ticks, a visit
  the budget cut short resuming first without a new slice, so no job of the window is always last); the next
  oldest enters as one leaves. A job that has gone `kAgeStep` ticks without work moves up a class (`navPromote`),
  so no class starves another for more than three steps. (Round robin over every job at once was built first
  and rejected: every in-flight awareness refresh progressed slowly, outlived the 30-tick awareness cadence and
  was discarded by the next re-plan -- many-1000 discards 41 -> 1,960, arrivals -5%.) Start requests are admitted at the head
  of their class, oldest first, and a new build only while the class's window has a free slot -- and, while more
  than half the byte allowance is in use, only while its kind has room over every class (`kFirstInflight` = 2
  `kWindow` first builds, `kRefreshInflight` = 2 `kRefreshWindow` refreshes: the two windows each kind can be
  served in). Beyond that a request may only take a field another group already has: a build past the windows
  would hold its potential array unserved, and an unfinished field is never an eviction victim. (The per-kind cap
  came in the fix round: a first build admitted as a first route is a Waiting job the next tick, its members
  standing for it, and a refresh promoted out of its class leaves its slot, so the per-class count alone let
  builds pile up past the windows -- 151 unserved first builds holding many-5000's whole allowance, 78 refreshes
  in many-2500. With memory to spare such builds cost nobody room, and capping them there cost outcomes --
  sched-load stuck 101 -> 162, many-1000 arrivals -9% -- so the cap applies only when memory is short; no gate
  fixture reaches it.) One refused for want of room is stepped over --
  at most `kWaitFitScan` (256) times a tick in the Waiting class, whose members stand still, `kFitScan` (8) in the
  others, so a large request no longer blocks every admission behind it -- and after `kReserveAge` ticks of
  refusals it reserves (also when its class's window is full): nothing after it takes new room, in its class or a
  later one, so the room that frees next is its (`NavAdmit`; letting smaller waiting requests start round it was
  measured and rejected: a large waiting request could be overtaken without end). Behind a stopped scan -- a
  reservation or the scan bound -- the remaining requests are still tried, but only for a share of a field that
  already exists: a share takes no room (and lands within `kStillScan` of the field's start, so it delays that
  field's eviction by no more), so the reserved request loses nothing it waits for (fix round; before, a
  reservation stopped every admission, shares included, in 84-92% of the overloaded rows' ticks).
* **Eviction** (`startField`, `evictionHolders`). A request that does not fit evicts finished fields past their
  tenure, oldest first: a group's own field as before, and (fix round) a shared one once every holder is past its
  tenure, dropped from all of them. Shared fields used never to be victims, so a few fields shared by groups that
  never finish could pin the room a reservation waited for: many-1000 under a two-map allowance stopped admitting
  at tick ~1,600 for the rest of the run. A share lands only within `kStillScan` of a field's start, so sharing
  extends no field's tenure by more; every finished field is therefore a victim `kFieldTenure + kStillScan` after
  it starts, which bounds how long a reservation waits (`legion_sched_pin`, `legion_sched_mix`).
* **Memory** (`NavBytes`, `fieldAllowance`). Deterministic bytes (element counts times fixed sizes) for every
  container, by category; the field cap is a fixed byte allowance (`kFieldBytes`, 128 MiB, never below two
  whole-map fields) over the potentials and the unfinished builds' frontiers, no longer 48 whole maps of cells.
  The process heap is measured beside it by the tools, never read by the sim.
* **Still synchronous** (reported by `accounting().syncPlane*` and in the record): a stale plane's rebuild on use
  (legacy nav-grid worlds, or after a whole-map change), the whole-plane relabel an incremental refresh falls back
  to, the incremental refresh itself (bounded by the changed rectangles), the awareness snapshot (one pass over
  the members every 30 ticks), a formation's slot ranking when its area closes, a single field's initialisation
  (at most one whole map).
* **Exact optimisation in the stage** (d190169c): `packedBound` and `slotsFor` sort their window's cells by a
  stable counting sort (the cells are collected in ascending order): a 10,000-body area was a 20 ms sort.

Tests: `navbudget_test` (the policy alone under continuous synthetic overload: fairness, service gaps, age
promotion, a shared job, the fit scan and reservation, shares behind a reservation; each mutated policy fails it),
`legion_sched_load` and `legion_sched_load_div` (the scheduler under continuous quota load in the sim; budgets
divided by 512 under `TAK_LEGION_VERIFY`, so registrations really defer and wait for plane builds; the accounting
counts the waits still open at the end of every tick, so ongoing starvation is measured, not only the waits that
ended), `legion_sched_mix` (memory pressure, `navbytes`: the fit scan, reservations, request promotion and shares
behind a stopped scan run in the sim, and every request's wait stays within a bound derived from the allowance,
the tenure and the measured queue -- removing the reservation or serving by id instead of age fails it),
`legion_sched_pin` (admission liveness under a two-map allowance: fails if shared fields are never victims),
`legion_sched_inflight` (many-2500: while memory is short no admission takes a kind past its windows; 78
refreshes in flight without the per-kind cap),
`legion_planepin`, `replay_test` (267 refused).

### Legion 10,000 stage 5: route queries (protocol 269)

Movement consumers no longer read a field's cells; they ask a **route** (`src/sim/legion_route.h`) what they need,
so stage 6 can replace the storage (sparse tiles) behind the same queries. The record, with the measurements, is
docs/legion-10000-progress.md section 7.5.

* **The interface** (`RouteView` over `RouteGrid`, exact: 8c6efbff). A `Field` is a `RouteGrid` (its window,
  potentials, build state, serial, epoch, work count) plus the builder's own state; only the builder (`startField`,
  `advance`, `serve`, `coveredFor`) touches the cells. A view answers, kept apart: `estimate` (direction guidance:
  exact where settled, a real way's length where reached but unsettled -- the descent stays sound on it, as on a
  half-built field), `known` (exact or none, never a tentative value), `covered(cell, margin)` (settled with that
  much key to spare: every cell within cost margin/2 of it is final), `reach` (Unknown / Reachable / Unreachable:
  Unreachable only on a complete route that spans the cell's static component and was built on the current
  geometry; Unknown never retires an order), `progress` (a distance with its metric, the route serial) and
  `geometryEpoch` (freshness, apart from all of the above). `legion_route_test` checks the answers on building,
  paused, complete, bounded and stale routes.
* **Demand** (`RouteDemand`, exact wherever the old list never filled: 5f60ce54). A paused build's demand is one
  entry per cell (the widest margin), in arrival order, indexed, with no cap; the 32-cell list dropped the oldest,
  so past 32 standing members some members' needs were never seen.
* **Stated regions** (662574f1). The consumers that read past their own settled cell -- the passage gate, passage
  lanes and pinwheel, awareness, MV-05's soft-block probe, packed slots, reach rings, the formation entrance, a ring
  member's walk to its spot -- run on a usable route (complete, or paused once it covered its group), not only a
  finished one. Each states the region it needs as `(cell, margin)` -- the lanes `kLaneCover` past the body (asked
  for only when a wall end or a passage lies on the way, `laneTrigger`), awareness `kAwareCover` ahead of the
  centroid, the entrance its open-ground test's radius round the point, a reach group its ring's spots (part of its
  first pause) -- and asks a paused build for it (`need`: a demand entry, served in the FirstRoute class through
  `needIds`, so a stated region does not take the Waiting class's place ahead of standing members and refreshes).
  It reads the region through certainty checks: `certainValue`, `certainReach` and `descend`'s check take a value
  as final only where the cell is settled, or proven unreachable (illegal, or outside the route's component on the
  current geometry). A consumer that meets any other read acts as on a route not yet built (no lane, slot, ring
  spot, corridor or entrance this time) and the route runs on to done (`Field::runOn`, hashed): no tentative or
  unknown value is acted on, and the worst case is the old wait for `done`. A formation's entrance waits for its
  region (`kEntryPending`, hashed) and is computed when it is settled. `serve`'s cover scan is incremental within
  one call (coverage only grows while a call advances the build), so a 10,000-body group costs O(members) per
  call, not per slice.
* **Which builds pause is unchanged.** Solo groups pause as before (C1); slot groups, pinwheel parts and reach
  groups still run to done. Letting them pause too was built and measured (three offsets of the scale files, and
  the gate fixtures with `--pause-all`): many-5000's longest field wait halves (1,189-1,234 -> 627-660 ticks) and
  its arrivals rise 8%, attack and battle fixtures spend a third less field work, but awareness on those paused
  routes asks for whole-window refreshes that dominate the work, many-2500's arrivals fall 6% and sched-load's
  stuck bodies double (101 -> 181-211) over the offsets, not diagnosed to the end; it is not taken here
  (`setPauseAllForTest` / `legion_scenario --pause-all` keep it for tests; stage 6's sparse tiles change the
  economics it was measured under). `legion_route_paused` runs the consumers on paused routes under
  `TAK_LEGION_VERIFY`.
* **Progress across metrics** (35cac631, 9bd3f5c3). An audit of the detour and
  back-off measures found two that compared values across metrics: the detour record (`Member::nearest`, the least
  of potential x64 on any field and squared cells to the own goal: a lost field read squared cells far below its
  potential as a record and restarted the back-off) and the approach holder's gain (across routes, its window
  restarted wherever the route gave no value). A metric transition is now never a record (a record needs a gain
  within the current metric, `nearestHere`, as well as a new low of `nearest`); the gain is per route serial with a
  clock that stops while the route gives no value; and a complete route that a static change has touched since
  its build no longer traps a body it does not reach (`RouteGrid::touched`, set by `staticChanged` for exactly the
  groups it marks stale, so a refresh decides; an epoch comparison was used first and left a body Waiting for ever
  when an unrelated change on another island or plane bumped the shared epoch, since nothing refreshes an untouched
  route). Re-basing `nearest` itself per metric was built first and rejected: it counted every gain
  after a refresh as a record, which the rules were not tuned on (battle-assault +9.7% detours; many-1000 -4.4%
  arrivals and +48% stuck over three offsets). The stall clock, the headway clock (already per metric), the hold
  clock, the crowd window and the settle bound are unchanged; the settle bound compares potentials across a refresh
  on purpose (a tolerance that must keep growing under churn).
* `Progress` is route-wide by definition: a provider built from tiles adds the exit's remaining distance and gives
  the tile chain its own metric, so a tile-local zero is never read as standing on a goal.

Tests: `legion_route_test` (the view and the demand alone), `legion_route_paused` (the consumers on paused routes:
verify on, serial == workers, every consumer asked for its region, no read uncertain), `replay_test` (268 refused).

### Legion 10,000 stage 6: hierarchical routes and shared tiles (protocol 270)

Long routes no longer have to be one field over a rectangle of the map. A route is planned over a hierarchy built on
the exact footprint-origin planes and built sparsely; groups whose demand is low read shared tiles. The record, with
the measurements and the sector-size experiment, is docs/legion-10000-progress.md section 7.6.

* **The hierarchy** (`src/sim/legion_hier.h`). Each plane is cut into K x K sectors (K = 32). A sector's legal origins
  fall into regions -- the 4-connected parts inside the sector, which under the step rule (8-connected, a diagonal
  needs both orthogonal cells) is exactly connectivity inside it -- and regions of neighbouring sectors are joined by
  portals, the maximal runs of boundary rows (or columns) legal on both sides. Portals carry every legal connection
  (a diagonal crossing needs an orthogonal one beside it), so two origins share a plane component exactly when their
  regions are joined by portals; clearance stays the plane's rectangle predicate. Sectors are built on first use and
  rebuilt on next use after a legality change touches them (the plane refresh invalidates them); identities are
  generation-aware: a relabelled sector gets a new generation, and a plane slot reused for another mobility class a
  new PlaneId generation. `legion_hier_test` (random planes, corner cases, incremental rebuilds, the search),
  `planeMatchesRebuild` (legion_planeincremental: the lazily maintained hierarchy joins exactly the plane's
  components after random churn) and a rotating sector check under `TAK_LEGION_VERIFY`.
* **Sparse storage** (`RouteGrid`). A route's potentials live in K x K tiles aligned to the sectors, allocated when the
  builder first writes one; an unwritten tile reads none. Exact for the window builds (78343da5).
* **Route plans** (`planRoute`, `routePlan`). A low-demand build (below) whose window has 128k cells or more is
  planned: the hierarchy's region search (Dijkstra over regions, portal midpoints as entries, at most 65,536 regions)
  links every member's region to the goals' regions. The route is
  * a **window** (as before): every high-demand route (one exact field the whole crowd shares), an approach group
    (its goals unreachable: churn-1000's approach group on a chain re-planned every ~40 ticks, -12% arrivals), small windows,
    a search that does not reach every member (`route.fallbacks`), or a corridor that would hold more than half the
    window;
  * a **corridor** (a low-demand group planning round movers while memory has room, a reach ring, a short trip): the
    same A* field restricted to the sectors round the tree (a 32-cell halo, more for a pinwheel formation) and round
    the destination (the crowd's packed disc plus the halo). Exact within the corridor, bounded (an origin outside it
    is Unknown, never Unreachable). Corridors for big crowds were built and dropped: churn-10000's army lost 20% of its
    arrivals in one;
  * a **tile chain** (low demand): the destination's own exact field over the sectors its crowd needs (the packed
    disc plus 8 cells), then one shared **tile** per transit region of the tree. A tile is the exact field of one
    region toward the far side of the portal it leaves by, each seed starting at a boundary cost, read with the
    chain's offset (`RouteGrid::offset`) so its value is route-wide (Progress stays a distance to the goals). A tile
    leading into another transit region is **aimed**: its boundary cost is linear along the portal, falling toward the
    next region's exit, so its lanes run parallel toward that exit (an octile distance to the exit's middle was tried
    first and drew every body of a squad into the middle of each portal: many-2500 stuck +16% over three offsets).
    One leading into the destination's field is **exact** (the field's values at its seeds). The offset is the
    largest (estimate - boundary cost) over the seeds, so a cell beside a seed reads more than the seed: every legal
    descent step strictly lowers the estimate, across tiles too, and ends on a goal -- no loop, no false minimum
    (`verifyDescent` under `TAK_LEGION_VERIFY` on every finished chain and repair). The regions one portal off the
    transit regions are the chain's halo: their tiles are attached when a member steps into one (a repair, queued
    and served before the field quota), so a body pushed off its way is back on the route without a rebuild.
* **The tile cache** (`Tile`, `tileFor`). Keyed by everything a tile's values depend on, compared whole after a hash
  match: the plane identity, the sector, region and both portal sectors' generations, the portal run, the boundary
  costs (an aimed tile's aim, an exact tile's list), the soft level of every origin of the region as the route's
  command sees it, and the route's mover corridors that reach the region (a group planning round movers gets local
  corrections there and shared tiles elsewhere). A tile leaving region R by portal P toward the same next exit is the
  same tile for every route, whatever its destination. Unreferenced tiles (an explicit route count, not shared_ptr
  use counts) are evicted oldest-use first past 64 MiB, after the requesting chain has attached its tile (so a new
  tile is never its own victim). A sector where a chain reads two regions gets a tile of the route's own, counted in
  the field allowance. Hashed: the cache's count and an insert/evict digest.
* **Search or field by demand** (`routeDemand`, `Group::routeMode`). The bodies that would read the route -- the
  group's members and those of every other group with the same goals -- at most 48 take a chain (or a corridor); a
  group that took the field keeps the field until that falls to 24 (hysteresis); a high-demand route needs no search.
  Chains have a count cap of their own (16,384; the byte allowance bounds their storage): under the shared 1,024-field
  cap, 1,024 one-body routes held every slot in scatter-10000 and no other route could start for the rest of the
  run (inherited: the stage-5 head starves the same way, request age 14,154 of 14,400 ticks). A group planning round movers (awareness corridors)
  takes a corridor while memory has room, so its detour has the corridor's width, and a chain with local
  corrections once more than half the field allowance is in use (stage 4's "memory short"): corridors for every
  awareness re-plan of 800 squads filled the allowance and evicted their own fields. Big orders (one-10000's 10,000 bodies) keep one exact
  field; many small ones (many-10000's 400 squads, scatter-10000's 10,000 single bodies) take chains over shared
  tiles. A route of fewer than two transit regions outside the destination is a short trip: its corridor or window.
* **Sharing.** A group with the same goals, soft rule and corridors takes another group's corridor that holds all of
  its plan's sectors, or a finished chain that already gives every one of its members an estimate -- also when its
  start may only share (a share allocates nothing).
* **Widening.** A body a corridor or chain does not reach (outside it, or unreached in it) first gets its halo
  repair (a chain), waits for the refresh its group is due if a static change touched the route (Unknown is not
  cut off), else the route is rebuilt round it (`Group::widens`, at most three); then the whole component's window
  takes over, as for any bounded window. The current solver stays the bounded fallback, never the default for a
  hard request.
* **Static changes** reach a corridor or chain only through the tiles it holds (`fieldTouched`); a chain refresh
  re-plans and finds every tile whose sectors did not change in the cache, so only the dirty tiles are rebuilt.
* `RouteGrid::fin`: an attached tile is final everywhere it reaches, whatever the build's key.

Tests: `legion_hier_test`, `legion_route_test` (sparse reads), `legion_chains` (eight squads cross a wall to eight
destinations: chains built, transit tiles shared between squads with different destinations -- hits by routes to
other goals than the tile's builder --, every body arrives, the descent check and the class check on),
`legion_chainschurn` (the same with a wall dropped across the routes while the chains build and lifted later),
`legion_sched_load` / `_div` now with `--no-hier-routes` (`setHierarchicalRoutesForTest`: sched-load's squads on chains
no longer saturate the quota the scheduler tests need), `legion_chainplane` (25 mobility classes take turns, the
25th reuses plane slot 0: no chain reads a tile of the previous class; fails if a reused slot keeps its generation),
`replay_test` (269 refused).

### Legion 10,000 stage 7: static navigation apart from traffic (protocol 271)

Terrain navigation (planes, the hierarchy, static tiles) is kept apart from what units do on it (soft bodies with
their own-order and allied-flyer exemptions, standing jams, moving streams, occupancy). A static change reaches only
the routes it can alter, as a closure or an improvement; traffic enters the coarse route choice as a cost and the
regions a route keeps as a local correction. The record, with the measurements and the variants rejected, is
docs/legion-10000-progress.md section 7.7.

* **Static changes per cluster, closures apart from improvements.** `refreshPlane` reports each cluster of changed
  origins as its own box (`StaticChange`), and whether any origin lost legality (a closure) or gained it (an
  improvement); `staticChanged` tests every route against each box at tile granularity (`fieldTouched`), so a corpse
  in one corner and a building in another no longer touch every route between them. A **closure** reaching a route
  makes it stale at once, as before (the plane and `commitGroundStep` still decide every step). An **improvement**
  only leaves every step the route describes legal and every estimate the length of a real way: the route keeps
  steering, its "unreached" stops proving anything (`RouteGrid::touched`: Unknown, never Unreachable), and its refresh
  waits `kImproveDefer` (150) ticks (`Group::improveTick`, hashed), coalescing churn; a member the touched route does
  not reach, or a member blocked by B1's rule (an improvement may be its way out, not a shortcut), asks for it at once
  (`legion_staticidle`'s second gap). A refresh installed after the build began is stale again only if a closure reached it
  while it built (`Field::closed`; until stage 7 any new epoch did, a change on another island included, so under
  churn anywhere every refresh was born stale).
* **Trapped orders and churn.** A trapped member re-registers only after an improvement whose box touches its
  component's box grown by its footprint (`trappedReopened`, over a per-plane log of recent improvements; a stale
  plane's rebuild counts as a whole-map improvement): a closure cannot open its way. Under never-ending churn
  elsewhere the 5-minute grace now runs out (`legion_trapped`; inherited from c2c25707 through stage 6).
* **Chain repair.** A chain region whose sector was relabelled since the plan is re-resolved on the current geometry
  (`resolveChainNode`: the portal run on the same boundary overlapping the old one most, still leading into the region
  the route continues in) and its tile rebuilt, instead of being left off the route -- a member there used to wait for
  a refresh that the next change abandoned again. A chain refreshed for a closure keeps its route: its plan is
  repaired (`repairPlan`: regions re-resolved, window and digest kept) and the build rebuilds only the dirty tiles, the
  clean ones coming from the cache; it is searched again when traffic or an improvement asks (`Group::reroute`:
  an awareness or MV-05 re-plan, a promoted improvement) or when the repair fails (a transit portal closed: the route
  itself is cut, a global consequence; or a member stands in a region the plan does not hold). A repaired or
  re-resolved chain is versioned, not exact: its tiles come from more than one geometry epoch, each descent-valid on
  its own build, the offsets taken from the values actually attached; it is touched and never proves Unreachable
  (`legion_chainresolve`).
* **Standing jams are traffic.** A member held `kRestAfter` (60) ticks on its way (`jamStill`) is stamped soft
  (kind 2) and owned by its group's command: other orders' routes plan round it, its own order never does (the parts
  of one selection share the command). It stops being soft the update it steps. Until stage 7 nothing that plans saw
  a held crowd -- the soft scan skipped every Legion member on its way and awareness skips a standing crowd -- so
  crossing walkers kept walking into the knots they formed. And a member held 60 updates against another order's jam
  (`blockedByJam`) may plan the short committed detour that formation members already could (the local correction);
  a queue of its own order is still waited for. The standing jams of each scan cycle are also MV-05 blocks
  (`jamBlockList`, rebuilt every cycle: a knot grows a few bodies a cycle and never made a dense tile of newly soft
  cells), so a group whose way meets another order's jam re-plans round it (at most once per 300 ticks) instead of
  steering into it on the field it built before the jam. Visibility: a jam is stamped for any player, as any still body is
  (the soft rule has no sight test); no route learns more of an enemy than the still bodies it already planned round.
  scatter-400 (400 single walkers to unrelated destinations) checks it (`ctest:legion_traffic`).
* **Traffic in coarse route selection.** The region search (`Hierarchy::search` with a traffic term) charges the way
  across each region by the share of its origins soft to the route's command: step x (kSoftFactor - 1) x soft /
  cells (`trafficPenalty`) -- the same soft rule the field and the tiles charge cell by cell, own-order arrivals and
  jams exempt, liftable allied flyers left out. The counts are cached per sector, keyed by its generation and the
  soft serials of it and its east, south and south-east neighbours. The search only picks the regions; the exact
  local correction inside them is the tiles' (never a sum of two fields). `legion_trafficlane`: the chain avoids the
  gap whose sector holds 121 idle bodies.
* **Static tiles apart from corrections.** Tiles with no soft level and no mover corridor in their key are terrain
  alone, the same for every command and player; those with them are the route's traffic correction of the region.
  They live in two caches (`staticTiles`, 48 MiB; `corrTiles`, 16 MiB), so traffic never evicts terrain; a correction
  is shared only by routes whose whole traffic view is identical (its key holds the view), so no route reads another
  player's view.
* **Eviction.** Each cache keeps its unreferenced tiles in a set ordered by (last use, insertion): a tile enters it
  when no chain holds it any more and leaves it when one attaches it; eviction pops the oldest -- the same victims, in
  the same order, as the whole-cache scan it replaced, which ran after every attach while referenced tiles alone held
  more than the allowance (half of scatter-10000's run time).

Window fields (high demand, small windows, the fallback) still charge soft bodies and corridors inside their own
build: a window is the whole route rebuilt, the "tile rebuild" end of the brief's corrections. Tests:
`legion_trapped` (never-ending churn, and the pocket opened under it), `legion_improvedefer`, `legion_chainresolve`,
`legion_trafficlane`, `legion_traffic`, each shown to fail on a scratch mutation of its mechanism (the record, 7.7).

### Legion 10,000 stage 8: chokepoints, subgroups and arrivals (protocol 272)

Narrow passages carry their queues, groups choose and split between passages by travel time plus queue delay, a
narrow passage is admitted one direction at a time, two orders to one point are one destination crowd, and the
arrival rules measure a formation member's headway on its own slot. Retail is untouched. The record, with the
measurements and the variants rejected, is docs/legion-10000-progress.md section 7.8.

* **Openings.** An opening is a narrow crossing of a hierarchy sector boundary: a portal run (sim/legion_hier.h)
  bounded at both ends by a pair of origins that do not cross the boundary (a wall, a bank, the map's edge), the run
  followed past sector corners (a door across one is one opening), fewer than `kOpeningSpan` (64) origins long. Its
  identity is its cells on the boundary line, so every movement class crossing one bridge shares one record
  (`Opening`, `openingIndex`; per plane, the openings of each sector side are cached by both sectors' generations,
  `Plane::sideOps`). Nothing is precomputed: records appear the first time a route's build or a chain walk crosses
  them.
* **Entrants and queue delay.** A window or corridor field of a group of `kOpeningMembers` (8) or more records, for
  every cell it reaches, the first opening its descent crosses and the direction (`Field::via`, 2 bytes a cell of a
  labelled tile, allocated only where a label is not 0; a tile chain's members read theirs from the chain's nodes).
  Every `kOpeningScan` (30) ticks each such member adds its queue area, (footX + 1) x (footZ + 1), to its next opening
  in its direction (`openingScan`). The delay of a crossing is the queue in front of it in cells of travel, area /
  width (`queueDelay`): on a narrow opening (`kNarrowWidth`, 16 cells) an opposing body counts twice (the direction
  turns, each turn clears the passage), on a wide one half (the streams keep to their halves). A group's own entrants
  (`Group::openArea`) are left out of the delays it is charged: a route never plans round itself.
* **Route choice.** A new window or corridor route carries the delays of its window's openings as charges on the
  crossing steps (`Field::charges`, route units, halved where the group already crosses -- a commitment), part of its
  sharing key; the build's bucket ring grows to a power of two above the largest charge. The region search charges
  them too (chains and corridors). Every scan, a group whose route is `kOpeningHold` (300) ticks old asks the region
  search where its members would go with today's queues (`predictEntrants`); if a quarter or more of its queue area
  would move (64 cells or more), it re-plans (`openingDrift`), and the move is committed in the table before the next
  group is judged -- one group at a time in id order -- and kept while its replacement builds. Without the
  commitment two groups traded places at every recount.
* **Subgroups for allocation.** A group of `kAllocMembers` (128) or more is cut by position into `kAllocCells` (64)
  squares of its members. For the opening with most of its queue it compares, per square, the region search's way
  through it and round it (closed), and moves squares, the cheapest detour first, to one other passage while the
  queue left behind still exceeds the other one plus the square's detour (`allocateQueue`). The moved members become
  a new group of the same order (`splitGroup`: seeds, command and kind copied; member state, formation slots and point
  kept), which plans its own route; each subgroup is judged on its own afterwards.
* **Admission at narrow passages.** An opening at most `kNarrowWidth` cells wide is admitted one direction at a time
  (`admissionHold`). Its strip is the run of cross-sections along the axis across the line no wider than the opening
  + 2 cells (a bridge's length, a door's wall; `ensureStrip`, per plane epoch). A member whose next opening it is and
  that stands within `kAdmitZone` bodies of the strip takes the token when the passage is free or open its way and no
  allied body the other way is in the strip, and otherwise waits (order kept, stall clock stopped). Admitted, it
  passes until it leaves the zone. Any member standing in a watched strip is an occupant whatever its route says
  (`watchStrips`). The token turns once the strip has no occupant its way and nothing was admitted for `kAdmitClear`
  (60) ticks; it closes after the other way waited `kAdmitGreen` (300) ticks. Only allies defer to each other; a group
  without labels neither waits nor counts. Open ground is never an opening, so nothing waits there. (Since
  2026-10-19 a tile chain's group of 8 or more is admitted too, one passage is admitted by one record, and admitted
  bodies on their way hold the token: "Passage admission at 100 bodies" below. Since 2026-10-20 a way closes after the
  other waited two of its crossings when that is longer than 300 ticks, and every group's route records labels, so a
  group of fewer than 8 waits too: "Passage turns and small groups" below.)
* **Shared destination areas.** An explicit order sent within a cell of the point where an earlier explicit area of
  the same player still gathers (its point live, or its arrivals settled there) takes that area's key and exact point
  (`sharedArea` over `destArea`, hashed): one command, one formation and crowd bound, and the first order's settled
  bodies and held queue are not obstacles or traffic to the second.
* **Arrivals.** A formation member's crowd window also measures its distance to its own slot against its best so far
  (`Member::windowSlotDist`): gaining half a body or more is not still. And a member with a free step that brings its
  origin nearer its slot is not pressed (`freeTowardSlot`). Both keep the front of a crowd going to the far side of its
  area, beyond the click, where every step climbs the field toward the click: before, it settled 20-60 cells short and
  the area filled from the click backwards.
* **An entrance per group.** Each group of a destination area finds its own entrance on its own plane and field from
  its own members (`Point::entries`); members exchange slots only with members coming through the same entrance (a
  room entered by several doors, a crowd of several footprints). The point's first entrance is computed as before.

Not changed (measured and rejected, section 7.8): awareness by streams (separate movers for spatially separate parts
of a destination area: swap-2500 lost 13% of its arrivals; split only where terrain separates the parts, a crosser
lost time on all three offsets); moving an off-slot arrival's claim to the cells it stands on; re-seeding a far
reach group only for a move of D/8 cells uncapped (the cap of 8 stays).

### Legion 10,000 stage 9: parallel planning (7b9428f8 .. 9064aa35, no protocol change)

Independent planning work runs on the sim's SimPool workers; movement commits stay serial. Everything here is exact:
the state hash and every observer key are those of the serial run on any worker count and in any job completion
order (the record, with measurements and the profile of the movement phase, is docs/legion-10000-progress.md
section 7.9).

* **The rule.** A parallel job's inputs and logical budget are chosen from sim state before it runs; each job reads
  immutable inputs (copies, or state nothing writes while it runs) and writes only its own slot; the serial pass
  consumes results by exact identity, in its own order, and waits for a job still running. Nothing a worker computes
  is published by the worker, no decision reads whether or when a job finished, and a slower machine waits rather than
  using a different subset. Results that are never consumed are dropped and never counted in `NavBytes`.
* **Chain tiles beside the serial pass** (`tilePrefetch`, `TileJob`, `takePrefetched`). Before a class's field jobs
  are served, the chain regions the class's window will attach next are picked in key order while their predicted
  attach work fits two DRR slices per job and the quota left. Each region's key is computed as `attachNode` does
  (`tileKey`), and a key neither the tile cache nor the staging holds becomes an asynchronous SimPool job
  (`SimPool::start`) over its own copies of the seed cells, the legality of the sector and the ring of origins round
  it, and the sector's region labels; the job and the serial `buildTile` share one body (`tileCore`). The serial pass
  then: takes a prepared key's soft levels and corridors in `attachNode` when the route, the node and the soft grid
  (`softSerial`) are unchanged, charging the work the scan would; and, where `tileFor` misses its cache, takes the
  staged tile with the equal key, charging the work its build counted (`help()`ing with unstarted jobs while it waits
  for a running one). The batch is joined at the end of the pass (`SimPool::finish`); a staged tile nobody took stays
  `kStagedTicks` (3) passes, then is dropped. With no workers nothing is prepared.
* **Plane first builds in row chunks** (`computeRows`). The cell layer of a first plane build (the per-tick budgeted
  `advanceBuild` stage 1 and `buildPlane`'s whole-map pass) runs in 16-row chunks as a fork-join; the structures are
  resolved once, in id order, before the fork.
* **Not parallelised.** Window and corridor field relaxation (`advance`): its slices share one quota with carry-over
  between jobs, so a parallel version needs per-job budgets fixed before execution -- a schedule change, i.e. a behaviour
  change, for at most about a millisecond a tick in the most planning-heavy row. Incremental plane refreshes
  (`refreshPlane`): measured per plane on workers in WE E4.2 -- exact, but the spike is one plane's relabel and the
  max got worse. Authoritative movement (`move`): serial by design (a unit observes earlier commits in the tick); its
  profile is in the record.
* **Instruments and test hooks.** `LegionNavigator::parallelStats()` and legion_scenario's `NAVPAR` stderr line
  (`--navpar`, and with `--scale`; observation only, and not the same on every worker count). `setParallelForTest` /
  `--par-force` (jobs inline with no workers) and `--par-perturb SEED` (debug builds also `TAK_LEGION_PAR_PERTURB`):
  jobs run in a shuffled order with random delays. Under `TAK_LEGION_VERIFY` every reused key is recomputed and every
  staged tile rebuilt from the plane. ctest `legion_parallel` (tools/legion_parallel_test.py) compares 1, 3 and 7
  workers, perturbed runs and a forced inline verify run with `--serial`; `simpool_test` covers the asynchronous API.

### Passage admission at 100 bodies (2026-10-19, no protocol change yet)

The ~100-body triage fixtures found head-on deadlocks at narrow passages that the 1,000-body ones hid:
`bridges-100` (two allied armies of 50 across a river; the 12-cell bridge is the nearest) gridlocked at offsets -5
and -7 (36 and 12 of 100 arrived, 64 and 88 stuck), and crowdbench `opposingbridges` 100x1 (half of a lane sent
each way through a 6-cell gap 64 cells long) arrived none, every body held. A dump of the stuck bodies (positions,
labels and the passage records' tokens every 25 ticks; a debug probe, not kept) gave three causes, none of them the
"exit lane" the 10,000 report guessed at.

* **A tile chain skipped admission.** Admission read the next opening from the field's labels (`Field::via`), which
  only window and corridor fields record. A group whose route was a tile chain -- both armies at -5 from their first
  plan, the west army's two groups at -7 after a re-plan at tick 2925 -- never asked: it walked into the strip while
  the other way held it, and the two streams met on the bridge. `nextOpening` now reads a chain member's next
  opening the way the recount does (`openingScan`): from the chain node of the body's region, the first exit portal
  along the parents that is an opening. It is cached per member by route serial, region key and plane epoch
  (`Member::viaSerial`, `viaKey`, `viaEpoch`, `viaLabel`; derived, never hashed), and charged as connectivity work. A
  chain's group of `kOpeningMembers` (8) or more is admitted, as a field's group of that size records labels.
* **One passage had several records.** An opening is one crossing of one sector boundary, so a passage longer than a
  sector is several records: `opposingbridges`' gap crosses lines 159 and 223, and the east-going stream took the
  token of the west record while the west-going stream took the east record's. Both were admitted into one strip.
  `passageOf` now maps a narrow record to the passage's holder. A passage is a connected component of narrow records
  on one side with overlapping spans, two records linked where either one's strip (`stripLo - 1 .. stripHi`) holds
  the other's crossing line. It is a closure because a strip is measured from its own record's width: on
  `taperbridge-100` (12-cell mouths, a 6-cell middle, records at five sector lines) the mouths' strips hold the
  middle's lines but the middle's strip holds neither mouth. The holder is the component's lowest number. It keeps
  the token, the waits, the occupants, the strip and the zone, and members are admitted with the holder's label.
  The holder is cached per record by strip epoch, plane and the count of narrow records (`Opening::head`,
  `headGen`, `passageGen`; derived, never hashed). A new record re-reads the components. If it joins two passages
  already in use, the lower holder takes over (no fixture does this).
* **A turn lapsed admissions on the apron.** The token turned 60 ticks after the last admission once no admitted
  body stood in the strip, and admitted bodies still walking to the mouth lost their admission where they stood. With
  chains admitted, `bridges-100` at offset 0 had 15 of the east army lapse 3-14 cells from the bridge when the token
  turned, hold there, and the west army came through into them and stood on the bridge (52 arrived, 48 stuck). Those
  bodies are now counted (`Opening::com`, derived per tick like `occ`; only bodies before the strip on their own way
  in, not those through it on the exit apron), and a passage does not turn under them until
  `kAdmitCommit` (120) ticks after its last admission (closing still stops new admissions; past that they lapse as
  before). 300 ticks cost `bridges-100` another 1,200 ticks of t90 for no fewer deadlocks. Also fixed: `occ` was
  reset only while the passage had a token or a waiter, so on a free passage it could carry an old count.
* **A chain group's stragglers did not ask.** Admission on a chain required the group to have 8 or more members at
  the time, and its last few bodies (the rest arrived) walked into the strip the other way held:
  `taperbridge-100` (a 100-cell bridge) ended with 21-25 bodies stuck head on in its middle at five of eleven
  offsets. A chain built for a group of 8 or more now admits its members for as long as they steer by it
  (`Field::chainAdmits`, hashed with the field). Also, a passage with a token stays watched (`activeOpenings`), so
  a body that never asks still counts as an occupant once it stands in the strip.

Measured (Legion, serial, 15,300 ticks; Retail untouched): `bridges-100` over offsets -8..7 now arrives 97-100 at
every offset (before 12-100, two offsets gridlocked) with at most 2 stuck at the end (before up to 88); the gate
median of `scale.arrive.n` 99 to 100, `reversals` 1,508 to 481, but `scale.arrive.t90` 10,237 to 11,928 and
`stopped_permille` 148 to 184: the chain groups that used to walk through head on, and on the 12-cell bridge usually
got away with it, now wait their turn. Crowdbench `opposingbridges` 100x1 (seeds 0, 2, 4) 0 to 98 / 99 / 91 in goal,
50x2 4 / 50 / 1 to 100 / 98 / 97. Every other legion100 fixture, and `swap-1000`, keeps its hashes at every offset
measured; `bridges-1000` (offsets 0, +-1) arrives 653 / 631 / 646 to 651 / 631 / 646 with 32 / 38 / 10 stuck to 34 /
1 / 15. `ctest:legion_passage_admission` (bridges-100 at offsets 0 and -5, at least 95 arrive and at most 2 stuck;
36 / 64 on the parent) and `ctest:legion_passage_records` (opposingbridges 100x1, at least 90 in goal; 0 on the
parent) cover the first three. `taperbridge-100` (tools/scenarios) at the gate offsets: at most 1 stuck at the end
(before 0-25), 64-92 arrived (before 63-95; the 6-cell middle passes one way at a time, so the crossing runs past
15,300 ticks). `ctest:legion_passage_taper` (offsets 0 and -4, at most 2 stuck; 21 and 25 on the parent). The
transitive holder and the approach-side count change no fixture measured (the hashes of all three fixtures and of
every legion100 fixture are those without them). The headless `--mpai` Inner Circle game (seed 1, 60 s) keeps both hashes (Retail
b353410003e98dd5, Legion 0825aeb9e8139203): nothing there waits at a narrow passage in its first minute.

### Passage lanes through wide gaps (2026-10-19, Legion ~100 round 2)

`gap20-100` (100 2x2 bodies through a 20-cell gap in a 4-cell wall) swapped lanes in the gap strip 21 times over the
gate offsets (median; 9 to 42), Retail 3: 3.5 swaps per file against Retail's 1.8, Legion taking the gap about six
files abreast to Retail's 1.6. The passage lanes (above) are what keeps a formation's order through a gap, and they
did not run there. A strip is one narrower than the formation's band and than `kPassageSpan` origins; a 100-body
formation's band is 25 cells (`laneCap` 8), but the gap's 19 origins across were over the old cap of 16, which no
measurement chose (the lanes were tuned on gaps of 14 cells and less, where 16 never bound). The cap is now 32: at or
above the widest band a 2x2 formation gets (laneCap 10, 31 cells), so for the common footprints the band alone decides,
and a gap wider than the band still has no lanes (a 48-body formation's band is 16 cells, so `gap20` keeps its 13
swaps; that formation is narrower than the gap and needs no folding). The same cap marks the cross-sections that make
a paused route serve the lanes their region (`laneTrigger`), so those reads now also start at strips of 16 to 31
origins.

Measured (Legion, gate offsets, serial; Retail untouched): of the 23 legion100 fixtures and `attackring-wall-64`,
`doorplug-112` and `gap20`, two change. `gap20-100`: `gate.top.crossings` 21 to 0 (0 at 9 of 11 offsets, 8 and 1 at
the others; Retail 3), `g.A.t90` 2,567 to 2,432 (Retail 4,667), done 3,259 to 3,204, `side.east.t90` 1,623 to
1,469, `stopped_permille` 96 to 76, wall touch 19 to 14 (Retail 35). `strait-2x100`: `g.B.t90` 2,807 to 2,645, done
3,518 to 3,172, `gate.strait.crossings` median 0 both (summed over the offsets 44 to 37). `ctest:legion_widegaplanes`
runs `gap20-100` over the core five: median 15 swaps on the parent, none here. The 57 nightly `legion_check_*` fixtures
that fail against `tools/scenarios/baseline.json` on the parent keep every Legion hash at the gate offsets, and the
headless `--mpai` Inner Circle game (seed 1, 60 s) keeps both hashes (Retail b353410003e98dd5, Legion
0825aeb9e8139203).

Wall touching was measured but not changed. A split of the moving wall touches by steering site (a scratch
instrument, not kept) gave, on `attackring-wall-100` (22 permille, Retail 17), 12 from the blocked branch (detour
routes 4, side-step walks 2, flow-round 3), 5 from straight walks and 4 from the field descent; on `doorplug100-112`
(15, Retail 7) 8 from straight walks to the slot and 5 blocked, nearly all within 6 cells of the door. Tried, each
over the same 26 fixtures, none kept:

* flow-round preferring a neighbour with a free cell to the wall when the body stands clear: one fixture moves by a
  permille (`attackring-wall-100` 22 to 21);
* the same preference also for a body already against the wall: `attackring-wall-100` 22 to 16 but reversals 371 to
  509, `corner-4x25` touch 58 to 65 and lane swaps 2 to 8, every group of it 3-7% later, `bridges-100` A 5% later;
  limited to attack and guard members only the two wall rings change (`attackring-wall-100` 22 to 16, summed over the
  offsets 256 to 237, reversals 371 to 509, ring damage 1,571 to 1,534; `attackring-wall-64` 29 both) -- a ring
  matter, left to the ring packing work;
* refusing a straight walk whose sweep touches a wall away from both its ends: doorplug fixtures 15 to 13, 18 to 14
  and 27 to 22, but `island-1x100`'s lane swaps 179 to 296 (the bodies passing its rock fold onto the field) and
  `attackring-wall-64` 29 to 31;
* the same only where the touched origin lies in a strip narrower than `kPassageSpan` (the door, not an open face):
  `doorplug100-112` 15 to 12 and complete at 9 of 11 offsets (before 4), `doorplug100-124` 27 to 22, `maze-100` 12
  to 8, `gap20` swaps 13 to 8, but `doorplug-112` arrives fewer (left behind median 1 to 3, t90 1,321 to 1,487) and
  `doorplug100-124` A done 1,430 to 1,715. A trade, not taken.

### Flow-around at travel speed (2026-10-19, Legion ~100 round 2, protocol 280)

After round 1 (passage admission, the head-on meet band, departures, ring upkeep) the ~100-body fixtures still lost
their tails round obstacles: gate medians on the parent 4fe78bdb against Retail, `island-1x100` (100 bodies round a
24x24 rock) t90 2,792 / 2,296 and last arrival 3,512 / 2,552; `idle-50x50` (50 movers past a block of 50 idle bodies)
t90 3,479 / 3,214 and t99 3,852 / 3,418; `doorplug100-124` group B t90 5,702 / 5,296; `corner-1x100` last arrival
5,619 / 5,366.

**The cause.** Position dumps every 20 ticks (a debug probe, not kept) give each body's speed between samples. In
front of `island-1x100`'s rock (offset 0, ticks 200-1,500) 38% of the samples moved under a fifth of travel speed,
against Retail's 9%, and at `idle-50x50`'s block corner (ticks 900-1,600) 34% against 3%; nearly all of them stood in
drive's blocked branch, and 16-19% of all samples moved at about an eighth. A body blocked by a body flows round it
(section 4): it commits a one-cell step into a free neighbour nearer its goal and walks it without turning, so at
the facing cap -- an eighth of travel speed once the step is more than 67.5 degrees off its heading. A sideways cell
took about 70 ticks, longer than the 45 a committed step lives, so a crowd folding round a wall end or past a block
stood in its own shuffles. Nothing else differed much: the walk inside the destination area (entering the packed
disc to arrival) was Retail's (`island-1x100` median 240 / p90 520 ticks against 220 / 480); the loss was in rounding
the rock (90% inside the disc at 2,539 against 2,099).

**The change.** A flow-around shuffle (`Member::detourFlow`, hashed while set) of a formation member on a move-like
leg keeps travel speed, still unturned, as lane-discipline passes and long local detours (`kBriskRoute`) already
did. Retail's mover traces round a body at its own speed. It keeps the facing cap where travel speed broke
something, each measured:

* a fight, attack, guard or patrol leg (rings and melees: no change wanted there). Round 4 (2026-10-20) gave the
  speed to fight legs too: their columns crept round their own members standing in reach ("Fight legs' shuffles");
* a member whose point is in a head-on meet (`Point::meet`, C1): `bridges-1000`'s two allied 500-body armies on the
  two-way 32-cell bridge interleaved into a knot, 97 bodies stuck at offset 0 (5 on the parent), and still 174 when
  only a shuffle round an oncoming blocker (its way to its goal pointing back) kept the cap;
* a body admitted through a narrow passage (`passLabel`): `bridges-100` at offset -5 and `taperbridge-100` at +2
  ended with 52 and 97 bodies stuck head on in a bridge's mouth;
* a body with a goal of its own (not a formation member): `legion_acceptance_crowdheld` (64 distinct goals packed
  at a 3-cell pitch behind a door) 60 of 64 in goal, under its 61 ratchet;
* a body that has gained nothing for a crowd window (`stalledFor` over `kCrowdWindow`, 45 ticks; the stall clock
  stands still while a committed step is walked): at speed its shuffles along a settled crowd's edge carried it out
  of the settle rule's reach, and `movement_orders_test`'s production rally ended with its last output standing 7
  cells off the point for good (`gen1-route-61923` 240 -> 221 arrived; the window also halves the lane-swap rise
  below). Shuffles only within the destination area at the cap, instead, cost 150 arrivals over the fixtures, and
  only for a part of 16 or more the tails (`island-1x100` last 2,433 -> 2,689).

Measured (gate offsets, Legion median before -> after, Retail). Retail is hash-identical: all 1,119 Retail
`hash@` entries of tools/scenarios/baseline.json (136 fixtures, gate offsets), the legion100 fixtures checked, and the
headless `--mpai` Inner Circle game (seed 1, 60 s: Retail `b353410003e98dd5`; Legion keeps `0825aeb9e8139203`:
nothing in its first minute walks a shuffle at speed).

| fixture | key | before | after | Retail |
|---|---|---|---|---|
| island-1x100 | t90 / last | 2,792 / 3,512 | 1,937 / 2,567 | 2,296 / 2,552 |
| island-1x100 | lane crossings | 179 | 260 | 450 |
| idle-50x50 | arrive t90 / t99 | 3,479 / 3,852 | 3,307 / 3,538 | 3,214 / 3,418 |
| doorplug100-124 | B t90 / B last | 5,702 / 6,242 | 5,432 / 5,747 | 5,296 / never |
| corner-1x100 | t90 / last | 4,907 / 5,619 | 4,187 / 4,862 | 5,084 / 5,366 |
| corner-4x25 | D t90 / D last | 4,685 / 5,216 | 4,190 / 4,460 | 5,018 / 5,203 |
| island-1x150 | t90 / lane crossings | 2,765 / 216 | 1,982 / 309 | 3,001 / 899 |
| corner-1x448 | top crossings | 42 | 21 | 5 |
| corner-8x56 | top crossings | 21 | 7 | 10 |
| bridges-100 | arrive t90 | 10,821 | 11,037 | (55 arrive) |

`island-1x100`'s last arrival is level with Retail's; `idle-50x50` and `doorplug100-124` B close most of the gap but
stay 3% behind. Over the 158 fixtures (tools/scenarios and tools/scenarios/legion100, Legion, every gate offset,
medians summed): arrivals 13,961 -> 14,340, groups never done 208 -> 183, summed t90 -8.3% and done -10.8%, gate
crossings 732 -> 851, stopped permille +3.6%, travel reversals +3.5%, spins equal. The crossings come mostly where
Legion stays far under Retail (`island-1x150` 216 -> 309 against 899); `mixed2` 13 -> 38 (Retail 0) and `sbend`'s
second turn 9 -> 27 (Retail 5) are the lane-order losses, while the corner fixtures swap fewer (`corner-1x448` 42 ->
21, `corner-8x56` 21 -> 7). Arrivals fall in `pocket-615-wall` 533 -> 474 (Retail 157): its denser crowd jams in the
pocket. Whole-run Legion work moves little (`cost-corner` N100 3,754,274 -> 3,605,669, N400 9,416,924 -> 9,546,699;
moves at N400 835,320 -> 740,413), but the counters of settled and flowing bodies rise with the earlier arrivals
(`cost-corner` still-scan samples +46-71%, slides +16%, holds +11%) and so does the steady window's work per tick
(`w_steady` N400 21% over its baseline): thirteen `legion_cost` entries moved, each naming protocol 280.
`ctest:legion_flowshuffle` (island-1x100 at 0, +-1: t90 at most 2,300 and done by 3,000; 2,747-2,882 and 3,468-3,550
on the parent) and `ctest:legion_flowshuffle_idle` (idle-50x50 t99 at most 3,650; 3,775-3,959). The scenario
baseline gate (tools/scenarios/baseline.json) already failed at the parent on 115 of its 136 fixtures (round 1's
behaviour, not retaken there); of the 21 that passed, six moved here (`aware-unseen`, `island-1x48`,
`mixed-spacing-07x`, `motion-wall`, `strait-2x48`, `strait-4x24`) and their 38 keys carry protocol-280 licences
(work counters of flowing and held bodies, contact permille, `island-1x48`'s lane-crossing bound 35 -> 37, one
hash). The Legion lines of the navigation golden (tools/navigation_checkpoints.json) are retaken (14 of 24
checkpoints moved; the Retail lines are unchanged).
Two test bounds move with the trade: `ctest:legion_landedflyers` (the stream reaches the parked flyers sooner and
stands against them longer while finishing sooner, as with round 1's brisk detours: foot-2 median stuck 1,760 and
ticks 2,207, the stuck base 1,400 -> 1,570) and `ctest:legion_choke` (the 32-cell bridge drains faster, so fewer
bodies take the 12-cell one: 39 crossings against the bound of 40 each, now 30; 843 of 1,000 arrive, none stuck).

Rejected on the way (same fixtures, gate medians):

* Walking the own line as far as it is legal, then the field (a Bug-like approach): bodies queue at the obstacle's
  face, `island-1x100` t90 2,792 -> 3,242, `corner-1x100` 4,907 -> 5,447, `gap10-100` 2,612 -> 3,085.
* Pinwheel walls that count idle bodies: no change at all (the turn under `idle-50x50`'s block is too gentle for a
  rank).
* Half speed sideways instead of the eighth: `island-1x100` t90 2,432. Full speed forward and half sideways kept
  `gen1-route-61923` (264 arrive) and `pocket-615-wall` (551) but lost `island-1x100`'s tail (2,818 against 2,433)
  and 2-3% of the summed t90 and done.
* Holding the passage token while an admitted body stands in the mouth (it fixes `bridges-100` -5 too): +1,000 ticks
  of t90 on `bridges-100`, +2,100 on `taperbridge-100`. Letting bodies that wait at a passage part for the stream
  coming out of it: the gridlock moved to `bridges-100` -1.

The island side choice was examined too: the shared field is seeded at the click, so 79 of the 100 bodies went
over the rock's near side (Retail 50 / 50, each body bug-tracing to its own offset goal). The selection given as two
orders at the halves' slot centroids split 50 / 50 and entered the disc 500 ticks sooner, but with the shuffles at
speed `island-1x100` is ahead of Retail at t90 and level at the last arrival without it, so no per-band field was
built.

### Passage turns and small groups (2026-10-20, Legion ~100 round 3)

After rounds 1 and 2 the narrow-passage admission still gridlocked by offset: on 0.8.6 (protocol 281),
`taperbridge-100` (tools/scenarios; a 161-cell strip, 12-cell mouths and a 6-cell middle) ended at offset +1 with 81 of
100 bodies stuck in the east mouth, at -5 and +8 it did not finish in 15,300 ticks (89 and 76 arrived), and over
-8..8 and five wider offsets (+-12, -16, +16, +17) it gridlocked at 4 (`bridges-100` at 1, -12); `bridges-1000`
(tools/scenarios/scale) at 5 of -8..8 (17-161 stuck). Retail has nothing to copy here: it gets neither army across
either bridge (`bridges-100` 36-64 of 100 arrive, `taperbridge-100` none, at each of offsets -8..8).

**The cause.** A dump of positions and of the passage's token every 50 ticks (a debug probe, not kept) at
`taperbridge-100` +1: the east army was admitted at tick 1,484 and its last bodies were through only at 3,592, the
west army waiting the while on the east apron, 18 cells out (the approach zone). Granted at 3,592, 22 west bodies
were admitted at once and walked toward the mouth through the east army's tail, which was coming out through their
crowd; none reached the strip in the 120 ticks of `kAdmitCommit`, the east army had waited the 300 ticks of
`kAdmitGreen` since 1,704, so the token turned back at 3,712, the 22 admissions lapsed where they stood -- in the east
mouth -- and the east army's next stream came out into them. From there one east body stood in the strip's last cell
for good, the token could not turn, and both armies waited. The rule behind it is the fixed green: a turn costs the
passage a whole crossing (the way let in next waits until the last body let in before is through), and each turn
puts the next way's crowd onto the apron the last way is coming out onto, so every turn is one more chance of a knot.
A 300-tick green turned a long passage every few bodies (`bridges-100`, a 49-cell strip: 6-10 turns at offsets -2..2;
`taperbridge-100` 2-3, each costing over 1,000 ticks of crossing).

**The change.** A way closes after the other has waited `kGreenTurns` (2) of its crossings, or `kAdmitGreen` (300)
ticks if that is longer. Its crossing is the longest among its admitted bodies of the approach zone plus the strip at
the body's top speed (`crossTicks`, derived every tick from the passers, like the occupant counts; the token fields it
decides were already hashed). A door or a short gap keeps the 300 ticks; with these fixtures' slowest bodies (1.6 px
a tick, 3x3) `bridges-100` closes after about 1,500 ticks of waiting and `taperbridge-100` after about 3,700 (in
practice it now turns once: the first army crosses whole, then the other).

Second, a group of fewer than `kOpeningMembers` (8) bodies obeys admission too. A window or corridor route recorded
the labels admission reads (`Field::via`) only for a group of 8 or more, so a few walkers sent separately walked into a
strip the other way held; a tile chain admitted only when built for 8 or more (`Field::chainAdmits`). Every soft
group's route now records them; `Field::queues` (hashed with the field) keeps the rest of stage 8 -- entrant counts,
queue charges, re-plans for drifting queues -- to groups of 8 or more as before, and a route of such a group is never
shared with a smaller group's field that does not count its members. `taperbridge-small` (tools/scenarios, new: the
same bridge, an army of 50 east against ten orders of 5 sent west 150 ticks apart) gridlocked at 16 of offsets -8..8
before (12-58 of 100 arrived).

Measured (Legion, serial, offsets -8..8 unless noted; medians; Retail hash-identical on both bridges at each of
-8..8; the 157 other fixtures of tools/scenarios and legion100 keep every observer key at offset 0, 49 of their Legion
hashes moving only by the field fold):

| fixture | key | before | after | Retail |
|---|---|---|---|---|
| taperbridge-100 | offsets gridlocked (>= 10 stuck) | 4 of 22 measured (+1: 19 arrive) | 0 of -16..17 | none arrive (-8..8) |
| taperbridge-100 | arrive.n min / t90 / stopped permille | 19 / 12,336 / 303 | 100 / 11,082 / 224 | 0 / never / 1 |
| bridges-100 | offsets gridlocked | 1 of 22 measured (-12: 38 arrive) | 0 of -16..17 | 36-64 arrive (-8..8) |
| bridges-100 | t90 / stopped permille (gate offsets) | 11,037 / 181 | 9,920 / 135 | never / 2 |
| bridges-100 | admission turns, offsets -2..2 | 6-10 | 2-4 | |
| bridges-1000 | arrivals summed / stuck summed / offsets with >= 10 stuck | 13,027 / 325 / 5 | 13,616 / 26 / 1 | |
| taperbridge-small | offsets gridlocked / arrive.n median | 16 / 37 | 0 / 98 | |
| bridges-small (scratch) | t90 / stopped permille | 8,233 / 43 | 8,546 / 62 | |

`bridges-100`'s t90 is now below the old two-way crossing's 10,237 (when it did not gridlock), so two-way flow on the
12-cell bridge was not brought back. The remaining `bridges-1000` offset (+6, 21 stuck) is its destination crowd, not a
bridge. `bridges-small` (a scratch fixture: `bridges-100`'s river with the west army as ten orders of 5) is the cost of
the small groups' waiting: on the 12-cell bridge they used to get through head on, and now wait their turn (+4% t90;
its few bodies left outside the authored radius are the ten small destination areas 4 cells apart filling at once).
`ctest:legion_passage_green` (taperbridge-100 at +1, -5 and +8: at least 98 arrive, at most 2 stuck; 19 / 81 on the
parent) and `ctest:legion_passage_small` (taperbridge-small at 0 and -8, at least 95 arrive; 17 / 83 and 12 / 88). The
Legion lines of tools/navigation_checkpoints.json move with the field fold (the small groups' fields now carry labels;
`navigation_determinism` fails until the integration retakes them).

Rejected on the way (same sweeps, each switched by a debug variable on the parent, none kept):

* Turning only once the old way's bodies have left the far apron (the zone beyond the strip) as well as the strip:
  they never could, because the other way's waiting crowd stands there; 7 (`bridges-100`) and 13 (`taperbridge-100`)
  of 17 offsets gridlocked.
* No lapse at all (an admitted body keeps the token its way however long it takes to reach the strip): `bridges-100`
  t90 +1,000 and gridlocked at -1, `taperbridge-100` gridlocked at 5 offsets and did not finish at 11 others (as the
  300-tick commit window measured in round 1).
* A commit window scaled to the zone walk (1.5 and 2 times the zone at the body's top speed, since 120 ticks is shorter
  than a slow 3x3's walk through its 24-cell zone): with the scaled green, neutral (`bridges-100` t90 9,930 to 10,013,
  `taperbridge-100` unchanged).
* Waiting bodies stepping out of the passage's lane sideways (a body held for admission in front of the mouth, inside
  the passage's span, steps one cell away from the span's centre at a time): the side crowds fill and the last bodies
  hold in the lane; the lottery moved (`taperbridge-100` +4 and +6 gridlocked instead of +1).

What is left is the apron itself: the waiting crowd still stands where the other way's stream comes out, and that
stream threads through it (most of a turn's time on `taperbridge-100`). With one or two turns it no longer knots at
any offset measured, but a passage whose two ways both keep arriving (more turns) is still exposed. A staging rule
that keeps the exit lane clear (the rejected step-out above, but holding bodies before they enter the lane rather than
moving them out of it) is the next thing to try.

### Lane order, walls and reversals at ~100 bodies, round 3 (2026-10-20, nothing landed)

The targets were the lane swaps where streams cross after a turn (gate medians on 0.8.6: `mixed2:gate.mid.crossings`
38, Retail 0; `sbend:gate.t2.crossings` 27, Retail 5; `corner-1x448:gate.top.crossings` 26, Retail 5), the reversals
and standing that round 1's brisk detours and round 2's travel-speed flow-around added, and wall touching
(`attackring-wall-64` 27 permille, Retail 20; `doorplug-112` 18, Retail 8). Every attempt below was measured
against 0.8.6 (Legion only; Retail stays hash-identical by construction), at the gate offsets and, for the final
candidates, over the 84 fixtures of the pinwheel's reach and at offsets -8..8 (-4..8 where the start leaves the map)
on the Windows box. None was a clear net win, so the simulation is unchanged; the turn fix below is kept on branch
`task-l3-lanes3-turnfix` (a8a90396) for a ruling, and the experiments' switches on `task-l3-lanes3-wip`.

**Where the crossings come from.** In `mixed2` (48 bodies round the tip of a long wall, a 20-cell gap below it) 36
of the 38 crossings are at the pinwheel site. Position dumps show the formation packed into the 7 rows under the tip
(z 80-86) instead of on its arcs: the inner files reach the wall's face one row above the tip and wait there, up to
180 ticks, for a gap in the stream of outer files passing under it on the passage lanes and their straight walks,
then leave on the outside. Round 2's flow-around at travel speed made that stream continuous: with arc-aimed
shuffles (a body flowing round a blocker toward its arc point or lane) back at the facing cap `mixed2` falls to 13,
the protocol-279 value, with its times unchanged, but `island-1x100` t90 1,937 -> 2,387, `corner-1x100` 4,187 ->
4,592 and `corner-1x448` 26 -> 35 crossings, so that was not taken either. `sbend`'s second-turn crossings are all
at the straight walk to the slots, and `corner-1x448`'s mostly in the blocked branch and the field.

**The pinwheel misses a turn at a wall end met across it.** `pivotAim` looked for the chain's turn from the first
chain cell beside a wall onward, so a turn's vertex lay at least 6 cells along the wall. A descent chain that meets a
wall END at an angle touches it where it turns; no turn is found until the body is round the end, so it is never
ranked there and hugs the end with the inner files. In `corner-1x100` most of the 100-body formation comes at the
block's corner across it: the bodies take their ranks only once round it (for the next turn, 40 cells on), and
round the corner on the field into one file along the rock-ragged edge. That is the standing: 2,062 of its 2,123
standing samples (every 30 ticks, offset 0) lie west of that corner between ticks 600 and 2,100, where a body that
reached the corner first stood 1,322 ticks (573 -> 1,895) waiting for a gap in the file passing in front of it.
Starting the search 6 cells earlier (the vertex may be the wall contact) ranks bodies that meet an end across it
(sbend's second turn, the generated maps' wall ends); `corner-1x100`'s funnel stays (standing 261 -> 258), since most
of its chains turn 10 to 40 cells before they first touch the rocks along the edge. At a bridge's mouth it
gridlocked `bridges-100` at 5 of the 11 gate offsets (none before: its 12-cell bridge's admission lottery,
"Passage admission at 100 bodies"), so an early vertex is no turn where the chain enters a passage strip (a
cross-section under `kPassageSpan`) within 6 cells after it. Measured with that guard (gate medians, 0.8.6 -> fix):

| fixture | key | 0.8.6 | fix | Retail |
|---|---|---|---|---|
| sbend | t2 crossings / t90 (offsets -8..8) | 26 / 3,126 | 4 / 3,251 | 5 / 3,602 |
| gen1-route-61923 | arrived per group / left behind | 93, 78, 70 / 50, 54, 98 | 143, 131, 111 / 0, 1, 57 | 9, 11, - |
| gen1-route-54772 | t90 per group | 7,611 / 7,195 / 9,051 | 7,071 / 6,412 / 8,376 | |
| mixed-100 | arrive t90 / t99 (-8..8) | 3,728 / 4,054 | 2,996 / 3,289 | |
| taperbridge-100 | stuck at the end / arrived, summed over the gate offsets | 81 / 1,003 | 0 / 1,087 | |
| corner-1x100 | t90 / done / standing (-4..8; Retail at the gate offsets) | 4,176 / 4,837 / 261 | 4,309 / 4,817 / 258 | 5,084 / 5,547 / 1 |
| island-1x100 | t90 / done (-8..8) | 1,937 / 2,477 | 1,982 / 2,540 | 2,296 / 2,552 |
| uturn, gap20 | top crossings | 5, 5 | 7, 20 | 2, 2 |
| slowturn | W400 / W1000 crossings | 8 / 8 | 15 / 22 | 3 / 0 |
| corner-1x448, corner-8x56 | top crossings | 26, 8 | 41, 14 | 5, 10 |
| wall-4x50 | top crossings / lane swaps | 19 / 1,008 | 27 / 1,487 | 3 / 426 |
| doubleturn, farroom, uturn | wall touch permille | 69, 214, 30 | 18, 170, 11 | -, 78, 34 |
| pocket-615-wall | arrived / reversals (-8..8) | 474 / 506 | 462 / 775 | 157 / 563 |

Summed over the 84 fixtures: arrivals 9,414 -> 9,563, groups never done 106 -> 103, t90 plus done -0.2%,
reversals -0.8%, standing -1.3%, moving wall touch -9.5%; but gate crossings 819 -> 894 and lane-probe swaps
1,673 -> 2,188, U-turn fixtures' t90 +4..+11% (outer files walk wider arcs; still far ahead of Retail), and
`ctest:legion_cost` fails on 19 `cost-corner` work counters (whole-run Legion work at N400 +22%: more slides,
holds and trace cells round the corner). More ranks mean more swaps on simple turns: the pinwheel's arcs are not
order-preserving there, and the missed turns had been hiding it. The candidate branch's test legion_turnrank
bounds sbend's median second-turn crossings over the core five at 9 (27 on 0.8.6, 4 with the fix).

Tried on top of it, each over the same crossing-sensitive set (gate medians):

* Ranking again, once, when the turn is within 24 or 32 chain cells (a rank taken 40 cells out reads the formation's
  lateral order across a way it does not keep): `mixed2` 38 -> 27 / 29 (-8..8: 29 median), `corner-1x448` 18 /
  33, `gap20` 7 / 9, sbend 2, the flow gains mostly kept; but uturn 9 / 11, gapsweep's 20-cell gap 18 / 16,
  `strait-2x100` 4 -> 13 (32), lane swaps still +27..35%, and `ctest:legion_mixedformation` fails either way (its 20 ground
  bodies finish 2,315 -> 2,945 / 2,593 because a late body shuffles at the settled crowd's edge, and the flyers then
  miss the facing bound, 5.1% against 5%, or stall one 30-tick window). Re-ranking only bodies that did not hug:
  `corner-1x448` 63. Only ranking turns within 24 cells: `corner-1x448` 14, slowturn 14 / 4, but gap20 19, uturn
  13 and the generated maps' gains halved.
* One frozen frame per part (the centroid and vertex of the part's first rank at that wall end, so every member is
  ranked across the same line): no better (`corner-1x448` 40-43, uturn 6-13).
* Ranking from the vertex instead of the centroid (the rank without its centre offset): nearly every body ranks 0
  and hugs, a single file -- every gate crossing key 0 (`mixed2`, sbend, uturn, gap20, slowturn, wall-4x50) at
  +22..29% of t90 on every U-turn fixture and `doubleturn` wall touch 69 -> 245. It shows the swaps are the arcs'.

**Other levers tried (gate medians, none kept):**

* The corner-hug removal of W9's B3 probe (every rank wheels at least a cell off the wall): `mixed2` 38 -> 32 (-8..8:
  38 -> 30), sbend 27 -> 19, `corner-1x448` 26 -> 20, `corner-8x56` 8 -> 4, `pocket-615-wall` wall touch 55 -> 35;
  but `corner-8x56` g.B.t90 6,573 -> 7,449 and groups E-H 24 fewer arrivals by tick 9,000.
* Travel speed for flow-around shuffles only within 45 or 67.5 degrees of the heading (arc-aimed, or all): `mixed2`
  38 -> 38 / 41 / 18, sbend 24 / 21 / 9, but `island-1x100` t90 1,937 -> 2,228..2,837 and `corner-8x56` g.B.t90
  +5..+28%. Arc shuffles at the cap only inside a passage strip: no change (mixed2's happen before the gap).
* Giving a long-held body the right of way at a merge (a moving body does not step into the cells a body held 60
  or 150 ticks is blocked from, when they are free but for the two of them): sbend 27 -> 10 / 12, `corner-1x448` 26
  -> 14 / 13, moving wall touch on the corners 56 -> 29 and `pocket-615-wall` 55 -> 17 (60 ticks), but the stream
  stops for every merge: `corner-1x100` t90 4,187 -> 5,132 / 4,997, `corner-4x25` D t90 +18..21%, `corner-1x448`'s group A never
  done. Only for a held body nearer the goal on the same field: sbend 17, `corner-1x448` 24, `corner-8x56` g.B.t90
  +6%.
* Turning off the passage gate or the passage lanes in `mixed2`: 31 and 77 crossings.

**Reversals and standing** are mostly what round 1's brisk routes are for (see Known weaknesses: a route backs out of
the pocket among settled bodies the body walked into, 1-6 updates after it starts); none of the changes above
lowered them without the time they bought, except the turn fix on the generated maps and `mixed-100` (reversals
-1..-53% on six of them, +20% on gen1-route-36057; standing -4..-60%). **Wall touching** in `attackring-wall-64` and `doorplug-112` (and their 100-body
versions) is untouched by every lever here (hash-identical runs): their touches are the ring's press-in and the
door's own lanes, left to the ring work; where formations round wall ends the turn fix lowers it (table).

### Corridor mouths and own-slot arrivals (2026-10-20, Legion ~100 round 3)

**The 0.8.6 drop, bisected.** The 0.8.6 scenario gate read `deadend-w2-n40-room-plain` (40 bodies to a room at the
end of a 2-cell, 100-cell corridor) `g.A.complete_n` 38 -> 32 (median of offsets 0, +-1, +-2). Built at v0.8.5
86db5d61, round 1 48c5b05c (and its parent 4fe78bdb), round 2's merges 4bef9dad (gap2), 8c729934 (ring2) and d6c559a7
(idle2), round 2 aacb1f2c and the blocked shots 575a70d2: gap2 and ring2 leave it bit-identical, and the drop comes
with idle2 (96f301f6, the flow-around shuffle at travel speed). Over offsets -12..36 the mean goes 28.04 -> 26.55
and the median 33 -> 29; the blocked shots change nothing. It is a lottery over a failure that was there all
along: at offsets +10..+16, where the column straddles the corridor's row, none of the 40 bodies ever entered, at
every commit from 0.8.5 on.

**The cause.** The corridor is one 2x2 body wide: its origins form a single row, and the origin in front of its
mouth (98,48) is the only way into it from the open side. Two bodies of the queue reach the mouth on either side of
that origin, at (98,47) and (98,49) -- or one beside it at (97,49) -- and each then stands on a cell of the way the
other needs: neither can step onto it, nobody behind them moves, and every order outside stays open
(`deadend-w2-n120-closed-plain` too: 3 of 120 complete over the gate offsets). Retail's bodies wedge there as well and
give their orders up where they stand (120 complete, 1-3 arrived); Retail has no give-way of any kind (its blocked
mover clamps, slows and presses on, docs/retail-engine.md).

**The change** (Legion only; Retail hash-identical):

* **Funnel claims** (`passageWay`, `updateClaim`, `claimedAgainst`). A member waits for a way when it stands beside
  a passage one origin wide: the lowest neighbour origin down the field that lies in such a passage (both its
  neighbours across the step illegal), or whose only way on does (the origin in front of a corridor's mouth). A
  seed is never a way, and a body hugging a long wall is not beside a passage. A member standing on a cell of a way
  its group found there waits for it too (`Group::funnelWays`, per field; a 3x3 or 4x4 covers a way from farther
  off). While a member waits for a way, no other member of its group may step onto an origin whose footprint covers
  that way and is not past it (nearer the goal), unless it waits for the same way and steps onto the way itself. The
  claim is taken on the step that reaches the origin (drive), so two bodies stepping beside one way on successive
  updates of a tick cannot both stand there; a queue in file is untouched (the body behind stands clear of the way
  of the body ahead). `Member::claimAt` and `Group::funnelWays` are hashed.
* **The funnel give-way** (`giveWay`, after 2 held updates). Where two bodies did reach a way at once, the one nearer
  the goal (lower potential; at one potential the one that gave way more often, then the lower id) asks each held
  body of its group on the way's cells, farther up the field, to step one cell clear of the way and of itself, to the
  free legal origin with the lowest potential, keeping its order (a part with its leg, as `partLane` parts a still
  member). All or none move; at most `kMaxGiveWays` (8) per leg, never straight back the way it last gave way. The
  turn-taking at one potential matters: with the id alone one body gave way to a whole lane.
* **Own-slot arrivals** (`settleWindow`, item 3 of the round). A settled arrival of the point whose cells cover the
  waiting body's own slot counts as nearer the point, whatever its footprint. The 2026-10-14 review read a body of
  another footprint over its cells; a body of the waiting body's own footprint was still read at its origin, so one
  settled a step off its own slot, uphill of the waiting body and across its line, never read nearer: the waiting
  body never queued, its yields were refused and its order stayed open. A random-formations screen
  (`legion_mixedrandom`, seeds 0..12,799: footprints 4x4/2x2, 4x4/3x3 or 3x3/2x2, 30-100 bodies, shuffled, column order
  or alternating rows, one click, 5,000 ticks) left 3 layouts open on the parent (two of them one layout; a 4x4 and a
  3x3, each beside a settled body of its own size on its slot) and none after. The crowdbench `mixedfootprints` rows
  (200x1 seed 0; 500x4 seeds 0/7/42 at 6,000 ticks) are unchanged: 200 and 2,000 complete and settled, none outside
  the radius, stalled unit-ticks 85,239 -> 85,149 on seed 42, equal on the others.

Measured on the Windows box (identical hashes to Linux), offsets -12..30 (43; deadend-multi the 11 gate offsets),
Legion before -> after as mean / median, and Retail's median:

| fixture | key | before | after | Retail |
|---|---|---|---|---|
| deadend-w2-n40-room-plain | complete_n (offsets all done) | 25.5 / 29 (5) | 37.7 / 40 (32) | 40 |
| deadend-w2-n40-room-plain | arrived | 21.9 / 27 | 30.1 / 32 | 0 |
| deadend-w2-n40-room-squad | complete_n (offsets all done) | 24.2 / 26 (7) | 36.1 / 39 (19) | 40 |
| deadend-w2-n40-closed-plain | complete_n (offsets all done) | 28.9 / 34 (8) | 40 / 40 (43) | 40 |
| deadend-w2-n40-closed-squad | complete_n (offsets all done) | 28.6 / 33 (13) | 40 / 40 (43) | 40 |
| deadend-w2-n120-closed-plain | complete_n (offsets all done) | 18.5 / 7 (0) | 119.2 / 120 (40) | 120 |
| deadend-w2-n120-closed-squad | complete_n (offsets all done) | 13.5 / 0 (0) | 119.2 / 120 (40) | 102 |
| deadend-w2-n120-room-plain | complete_n / arrived | 19.4 / 5, 11.0 / 5 | 119.2 / 120, 34.2 / 34 | 120, 2 |
| deadend-w2-n120-room-squad | complete_n / arrived | 18.8 / 10, 11.6 / 10 | 119.2 / 120, 30.7 / 30 | 101, 0 |
| deadend-multi-4x120 | A complete_n (offsets all done) | 8.2 / 3 (0) | 120 / 120 (11) | 120 |
| deadend-w2-n120-closed-plain | stopped permille | 873 / 880 | 710 / 717 | 1 |
| deadend-w2-n40-room-plain | stopped permille | 564 / 585 | 204 / 124 | 1 |
| deadend-w2-n120-closed-plain | wall touch permille | 135 / 72 | 299 / 302 | 33 |
| deadend-w2-n40-room-plain | wall touch permille | 414 / 478 | 554 / 565 | 32 |
| deadend-w2-n120-closed-plain | complete outside radius | 12.9 / 0 | 109.3 / 110 | 118 |

The gate median of `deadend-w2-n40-room-plain` `complete_n` is 40 (0.8.6: 32, 0.8.5 baseline 38). The trades are
the bodies now in the corridor: they touch its walls (wall touch permille about doubles, against Retail's 30s) and
the queue settles back along it outside the authored radius (by design since AR-08: Retail's give up there too).
The three offsets where 12 orders of the 120-body fixtures stay open (+28..+30) are a fixture artefact that the
parent shows too: the column's last row spawns two cells from the map's edge and its bodies never leave Waiting.
Screened at offset 0 over all 159 fixtures (tools/scenarios and legion100, Legion): 10 change, all of them the
2-cell dead ends above (`deadend-w2-*`, `deadend-multi-4x120`) plus four whose hash changes with every key equal
(`corner-1x448`, `corner-8x56`, `gen1-route-36057`, `gen1-route-65845`). Retail: identical `hash@` at every gate
offset of the two target fixtures and in the Windows sweep's Retail rows; the headless `--mpai` Inner Circle game
(seed 1, 60 s) keeps Retail `b353410003e98dd5` and Legion `0825aeb9e8139203`.
`ctest:legion_funnelmouth` (the closed 120-body corridor at start offsets 0 and -1, the 40-body room at +12 and +14:
27, 9, 0 and 0 settled on the parent, all now) and `ctest:legion_mixedrandom` (seeds 5717 and 9651).

**Ties between claims** (the round's review, 2026-10-20). As first landed, a claim closed every origin covering its way
at an equal or higher potential, to every other member. In a field of 1x1 pillars 3 cells apart (gaps 2 cells wide, one
origin wide for a 2x2, so every origin in the field is a way) two bodies wait for neighbouring ways at one potential,
each way covering the other: each body's step onto its own way was refused by the other's claim, no body stood on
either way so the give-way never ran, and both held for good (a review scratch fixture, 72 2x2 bodies through 27x31
pillars on one click: `complete_n` gate median 60 and 1-9 orders open at every gate offset; the parent 72 at all).
Now, at one potential, a claim yields to a member that waits for a way of its own and has the lower id; a member that
waits for no way still keeps off a claimed way at its potential (so a third body cannot slip onto it, the rejected
exclusive-claimant failure below). The order is total, so no two waiters can each hold the other. The same fixture:
72 at all 11 gate offsets, done at 2909-3540 ticks (Retail 2585 median). Over offsets -12..30 (Windows, hashes equal
to Linux) `complete_n` mean / minimum / offsets with orders open after 900 still ticks: 0.8.6 (bd7f3f6e) 71.6 / 68 / 7,
round 3 as first landed 66.1 / 45 / 35, now 72 / 72 / 0 (Retail 72 / 72 / 0); every order ends by tick 3910, at two
offsets one body settles outside the radius. Stopped permille 112 -> 179 -> 103 (median over those offsets), wall touch 443 -> 479
-> 441 (Retail 421). Every 2-cell dead end above
(`deadend-w2-n120-closed-plain`, `-n40-room-plain`, `-n40-closed-plain`, `deadend-multi-4x120`) is bit-identical at
all gate offsets, Retail too. The review's alternative, opening every equal potential (`here > v`), also lets a
member that waits for no way step onto the claimed way itself. `ctest:legion_funneltie` (the pillar field at offsets
0, +1 and -3: 68, 63 and 60 of 72 settled after 9,000 ticks on the parent, all now).

Rejected on the way (same fixtures):

* The give-way alone: `deadend-w2-n120-closed-plain` 3 -> 17 complete (gate median). Once the crowd behind packs
  round the mouth the losing body has no free cell, and the pair forms again with the next arrivals.
* One exclusive claimant per way, taken only by a body with a single way down: in the ticks between the claimant
  stepping onto the way and the next waiter's update a third body edged onto a cell of it from an origin with
  several ways down (`deadend-w2-n40-room-plain` offsets +10..+16: 2 to 29 of 40 complete; `deadend-w2-n120-closed-plain`
  median 22).
* A funnel as any origin against terrain with a single way down (this origin or its way beside an illegal origin;
  without that test a body beside a seed in open ground has a single way down too): the claims chained along walls,
  each closing a 7x7 block of origins round a 4x4 (`strait` 48 -> 13 arrived, `maze-100` 85 -> 44 with 41 bodies
  standing for good; offset 0). The passage test (both neighbours across the step illegal, at the way or the next
  one) touches only the 2-cell dead ends.

### Lane order through turns: the ribbon (2026-10-20, Legion ~100 round 4)

The targets were the lane swaps after turns (gate medians at 30813a73, Legion against Retail: `mixed2:gate.mid.crossings`
38 against 0, `sbend:gate.t2.crossings` 27 against 5, `corner-1x448:gate.top.crossings` 26 against 5, `corner-8x56` 7,
`uturn`, `gap20`, `slowturn`, `wall-4x50`) and the single file at `corner-1x100`'s corner (standing 267 permille,
Retail 1), without giving back arrival time (t90 and done within 3% on `island-1x100`, `corner-1x100`, `corner-4x25`,
`idle-50x50`, `many-100`, `gap10-100`, `gap20-100`, `strait-2x100`, `swap-100`).

**Retail first.** Retail has no turn logic at all: each body follows its own path to its own formation offset, a
blocked body clamps, slows and presses on, and nobody overtakes (docs/retail-engine.md, the local steerer). Its files
keep their order because they stay in the order they started in, at a much lower speed (`mixed2` t90 7,600 against
Legion's 6,486; `gap20-100` 4,667 against 2,207).

**Where the swaps came from** (position dumps and per-member traces, offset 0):

* *The straight walk cut the arcs.* `mixed2`'s ranked members had radii 4 to 17 cells, yet all of them passed under the
  tip within 7 rows: a member's straight walk to its slot (640 cells) opens the moment its line clears the wall end, so
  it grazes the tip, and every file left its arc there for that one line. The inner files reached the face and waited
  for a gap in the outer ones grazing the tip below them (round 3's finding), then left on the outside.
* *The ranks.* Each member ranked once per order, when a turn came into its look-ahead, by its offset from the part's
  live centroid across the way to the turn (cap/2 +- offset, clamped; under 2 bodies it hugged for the whole order). A
  strung-out or split crowd read its depth as width (`island-1x100`'s top half all ranked 15-30 cells out of an
  island it hugged), the clamps tied the inner and outer quarters, and a later turn met the first one's ranks:
  `sbend`'s second turn has the other inner side, so the first turn's inner files took the inner arc again, from the
  outside of the stream.
* *Turns.* An S-shaped octile chain or a bend away from a face (leaving it for the goal) read as a turn, and a U-turn
  whose end lay beyond the look-ahead read as its first bend: the hold let go before the wall end.
* *Arrival order.* After a turn of 90 degrees or more the files come in an order that is not the formation's (the
  slots are its shape, translated), so the bodies crossed at the destination and a late one walked round the settled
  crowd to a slot on its far side.

**The change** (legion.cpp `ribbon`, `ribbonHold`, `laneUncross`, the rank in `pivotAim`; Legion only):

* **The ribbon.** When a member of a part of 32 or more on a move-like leg (`ribbonLeg`) ranks for a turn, every
  member of its point that has no rank yet and whose own descent (96 steps) passes the turn's vertex within 2 cells
  is ranked at once: ordered by its place across the way in (the approach chord through the vertex), inner first,
  member k of n takes radius (2k+1)/2n of the ribbon's width, 1.2 bodies per sqrt(n) (`kRibbonNum/kRibbonDen`, at most
  `kPivotMaxCells`); under 1 cell it hugs (the inner file). A crowd that splits round an island ranks each side
  apart; the parts of a convoy rank as one (ranked apart their ribbons lay over each other from the same wall:
  `corner-8x56` 9 swaps against 3). The arc pursuit is the pinwheel's, down to a radius of 1.
* **The hold** (`ribbonHold`). While a member has a turn, a straight walk whose line passes within its radius (less a
  body) of the turn's vertex or end is refused, and it follows its arc. It is past the turn 2 cells beyond the end
  along the exit chord and not behind the vertex along the way in; then the turn is remembered and it ranks again at
  the next turn, keeping its radius when the inner side is the same (one file through `corner-1x100`'s two right
  turns) and taking a new rank where it flips (`sbend`).
* **Turns.** A turn (30 degrees or more between 6-cell chords, from the first chain cell beside a wall) needs the wall
  on its inner side within its 12 cells, is not within 12 cells of the last turn's vertex or end, and is ranked only
  once its end -- the first cell after which the chain runs straight for 12 cells -- is in the look-ahead.
* **Slots by lane** (`laneUncross`). Once, when a member that has rounded a turn comes within 24 cells of its slot, it
  exchanges slots with the member of its group within 16 cells (one footprint) for which that shortens their two
  straight ways the most, by more than a cell (a 2-opt step; crossing ways are always the longer pair). Without it
  `gap10-100` done rose 6% (one body 1,900 ticks behind the gate) and `wall-4x50`'s later groups' t90 12%. The two
  members exchange goal and slot index together (a formation member's index is always 0 and its claim is the cells
  under its goal, which stay claimed; the shared-goal slot tables indexed by it are never ribbon members').
* Fight, attack, guard and patrol legs and parts under 32 keep the pinwheel as it was (at 20 and 25 bodies the ribbon
  only slowed the formation: `legion_mixedformation`'s ground +13% with its flyers stalling, `corner-4x25`'s group A
  +10%, no swap to save). New hashed member state: the turn and last-turn records and the exchange flag.

Measured (Legion, gate medians; base 30813a73; Retail from the same runs):

| fixture | key | base | ribbon | Retail |
|---|---|---|---|---|
| mixed2 | gate.mid.crossings | 38 | 10 | 0 |
| sbend | gate.t2.crossings | 27 | 2 | 5 |
| corner-1x448 | gate.top.crossings | 26 | 0 | 5 |
| corner-8x56 | gate.top.crossings | 7 | 3 | 10 |
| uturn / gap20 | gate.top.crossings | 5 / 5 | 2 / 2 | 2 / 2 |
| slowturn | gate.topW400 / topW1000 crossings | 8 / 8 | 5 / 5 | 3 / 0 |
| wall-4x50 | gate.top / strip crossings | 19 / 7 | 10 / 1 | 3 / 4 |
| island-1x100 | gate.lane / strip crossings | 260 / 2 | 191 / 0 | 450 / 35 |
| bridges-100 | gate.bridge1.crossings | 5 | 0 | 15 |
| island-1x100 | g.U.t90 / done | 1,937 / 2,567 | 1,487 / 1,941 | 2,296 / 2,612 |
| corner-1x100 | g.A.t90 / done | 4,187 / 4,862 | 4,155 / 4,760 | 5,084 / 5,547 |
| corner-4x25 | g.D.t90 / done | 4,190 / 4,460 | 4,190 / 4,460 | 5,018 / 5,296 |
| gap10-100 | g.A.t90 / done | 2,387 / 2,919 | 2,387 / 2,963 | 4,597 / 5,163 |
| gap20-100 | g.A.t90 / done | 2,207 / 2,719 | 2,235 / 2,724 | 4,667 / 5,115 |
| strait-2x100 | g.B.t90 / done | 2,057 / 2,667 | 2,050 / 2,522 | never |
| idle-50x50, many-100, swap-100 | scale.arrive.t90 | 3,307, 687, 7,528 | equal | |
| mixed-100 / bridges-100 | scale.arrive.t90 | 3,743 / 9,920 | 3,203 / 9,273 | |
| mixed2 / sbend / uturn | t90 | 6,486 / 3,126 / 2,072 | 6,396 / 3,063 / 2,108 | 7,600 / 3,602 / 2,997 |
| slowturn | W1000 / C400 t90 | 2,033 / 3,296 | 2,123 / 3,377 | |
| wall-4x50 | A / B / C / D t90 | 1,847 / 2,388 / 3,064 / 3,678 | 1,937 / 2,388 / 3,244 / 3,470 | |
| doubleturn | g.A.t90 | 2,342 | 2,497 | 2,865 |
| corner-1x448 | arrived by 9,000 ticks, median of the per-offset totals (sum of group medians) | 389 (385) | 326 (328) | 106 (109) |
| corner-1x448 | g.C / g.F arrived | 53 / 28 | 28 / 4 | 5 / 1 |
| corner-8x56 | arrived by 9,000 ticks, median of the per-offset totals (sum of group medians) | 418 (420) | 368 (380) | 114 (106) |
| corner-8x56 | g.C / g.F arrived | 56 / 50 | 53 / 23 | 5 / 1 |

The wide sweep (Windows, offsets -8..8, -4..8 for the corner fixtures; hashes equal to Linux) agrees: medians
`mixed2` 38 -> 10, `sbend` 26 -> 1, `slowturn` 7/11 -> 5/5, `wall-4x50` 20 -> 11, `island-1x100` t90 -23%, `mixed-100`
-14%, `bridges-100` -7%; `gap10-100` t90/done +1.9%/+0.9%, `gap20-100` +1.5%/+4.2% (mean done +6%), `uturn` +1.7%,
`doubleturn` +5%. In the legion100 regression screen (all 23 fixtures, Legion, gate offsets) the fixtures not in the
table keep their keys but `doorplug100-124` (door swaps 3 -> 1), `maze-100` (done 8,748 -> 8,703) and
`doorplug100-112` (reversals 40 -> 41); `strait-2x100`'s swaps go 4 -> 0 and `island-1x100` stands 159 -> 54 permille.

**What it costs.** The two 448-body corner stress fixtures lose throughput: `corner-1x448` (one selection, seven parts)
arrives 326 of 448 by tick 9,000 against 389 (median of the per-offset totals; Retail 106), `corner-8x56` 368 against
418, for their swaps at the top gate (26 -> 0, 7 -> 3). (As first written this read 328/385 and 380/420: the sums of
the per-group medians.) The loss falls on the groups at the tail of the queue to A's top-left corner, the ones spawned
beside A's west face (its inner files): `corner-1x448` `g.F` 28 -> 4 of 56 (0 at offset -2, at most 12) and `g.C` 53
-> 28, so `side.east.t90` is never reached at 8 of 9 offsets (8,387 before); `corner-8x56` `g.F` 50 -> 23 (6 at -2).
They are not held by a separate rule: at ticks 8,800-9,000 `corner-1x448`'s F bodies walk A's top at about 1.5 px a
tick, and the queue's tail was the same before (F was the last group in, 28) and is in Retail (F 1, C 5). It is the
corner's drain rate, spent on whoever is last. The ribbon is capped at 32 cells, so a wave of more than 178 bodies (2x2) is compressed: falling
back to the old rank for a wave the cap compresses (measured, not kept) gives `corner-1x448` 392 arrivals and 23 swaps
and leaves `corner-8x56` at 375; a cap of 48 gives 338 and 395 (12 swaps). As first committed, `mixed2`'s second leg left one
body for good at gate offset +2 (`g.G.done` -1, `g.G.arrived` 47 of 48); fixed by the slot-wedge rule below. Legion work on
`cost-corner` rises (whole run N200 +11.5%, N400 +21.6%: more arc sweeps and traced cells; the scaling gates hold,
steady W(4N0)/W(N0) 0.54). `legion_forestedge` counts 4.47 files abreast at the forest's end against 5.49 (the ribbon
is narrower than the old centroid spread). The headless `--mpai` Inner Circle game (seed 1, 60 s) keeps both hashes
(Retail b353410003e98dd5, Legion 0825aeb9e8139203); Retail is hash-identical at every gate offset of `mixed2`, `sbend`
and `corner-1x100`.

**Not fixed: `corner-1x100`'s single file** (standing 267 -> 268 permille). The single file is not at a wall end: the
octile field's descent from the south-west climbs diagonally onto the line just above the block's top (z 55) some 20
cells before the block, every body by the same tie-break, and runs along it to the far corner (the turn the pinwheel
ranks for); the bodies queue to merge into it in open ground, before any turn is in their look-ahead. Lanes parallel
to the top (a ribbon member offset from chain cells within 3 cells of an obstacle on its inner side) gave no files
(standing +3.7%) and 29 swaps at `corner-8x56`. It needs lanes taken from the field before the merge, not at the turn.

Rejected on the way (gate medians unless noted):

* The hold alone, with the old ranks: `mixed2` 38 -> 11 at no cost, but `island-1x100` 260 -> 497 lane swaps (Retail
  450) and `sbend` 27 unchanged; ranking again at every turn with the centroid rule: `sbend` 10, `corner-1x448` 9, but
  `corner-1x100` t90 +10.7%.
* One lateral rank per order from a whole-part snapshot, ordinal (q), reused at every turn: crossings mostly down
  (width 0.7: `mixed2` 3, `sbend` 0, `corner-1x448` 2), but U-turn fixtures +6..+13% t90 (the snapshot read the
  formation's depth before its stream formed, and 30% of it hugged at width 0.7).
* Radius = the member's distance out of the line through the vertex (natural offset): every key 0-5 swaps, but
  `gap20-100` +55%, `wall-4x50` +66% (a diagonal approach puts half the stream inside the line: one hug file and one
  clamped outer file); the same measured from the innermost of the members near the vertex: waves of one, all hugging.
* Arcs offset from every chain cell to the turn's outer side (not only beside walls): the open-ground cells are the
  body's own path, so it drifted outward, `corner-1x448` 691 swaps. An exit lane past the turn's end: `sbend` t90
  -15% but `corner-1x448` 206-437 swaps in its jam.
* Files at footprint pitch instead of an even share: no better (`corner-4x25` A +8%, `uturn` +8%). Ribbon width 1.0
  instead of 1.2: `corner-4x25` A +10.6%; 1.4: `slowturn` 12 swaps, `island-1x100` 233.
* Round 3's contact-vertex search (a8a90396) on top: `island-1x100` lane swaps 48, but its t90 gain halved and
  `corner-1x100` never done at one offset; not taken. Its target, `sbend`'s second turn, is ranked at the first one's
  release. A turn met where the ribbon is wider than the passage left to the old rank: `gap10-100`/`gap20-100` t90 +4%
  and `bridges-100` 13 swaps.
* A ribbon across the orders of a player at the vertex: `gap20-100` t90 +20%; the old rank where another part or point
  is within 24/48 cells of the vertex: `corner-8x56` arrivals 416 but 23 swaps, `wall-4x50` C/D +4%.

`ctest:legion_ribbon` runs `sbend` over the core five: 18 to 29 swaps at the second turn on the parent (median 27), 1
here (1, 2, 1, 6, 1). The prototypes' switches are on branch `task-l4-turnA-wip` (b8aa8c26).

**The slot wedge** (the round's review; `settleWindow`, `beforeSlot`). At `mixed2`'s gate offset +2 the ribbon's new
arrival order left one body (id 39) at origin (46,56), 2.5 cells from its free slot (43,54), between two settled
arrivals of its own formation: one on the cell its straight step to the slot rounds onto (45,56), the other on the
other side of the diagonal step's corner. Neither stands nearer the click by the field (the formation's slot lies
across the crowd from the click), neither has a free cell within one of its own goal to yield to, so the body never
queued, its yields were refused every tick, it went to rest and stood there from tick 6,185 to the end of the run.
Now a settled arrival of the body's own point that is nearer the body's own formation slot (centre to centre), with
the body within 3 footprints of that slot, counts as nearer the point, as an arrival on the slot already did (round
3): the body queues, re-chooses a free slot or settles where it stands. Legion only; gate offsets, against the ribbon
as first committed: `mixed2` +2 is done at 6,618 (every other offset bit-identical); `motion-cross-100` completes
`g.u00`/`g.u02` at offset +2 that stayed open; `churn-100` B arrived median 32 -> 31 (left behind 13 -> 14; `scale.arrive.t90`
reached at +2, 1,605); `bridges-100` A done 11,056 -> 11,782 at offset -2 (medians equal); `mixed-100` T2 done 4,157 ->
4,415 at -1 (medians equal); `idle-50x50` offset 0 done 3,947 -> 4,053 (median equal). The other 25 screened fixtures
are bit-identical at every gate offset. Retail is hash-identical at every gate offset of `mixed2`,
`motion-cross-100` and `churn-100`. The wide sweep (Windows, offsets -8..8, against the ribbon's): `mixed2` done at all
17 offsets (16 bit-identical; median done 6,666 -> 6,650), `bridges-100` A t90 median 10,223 -> 10,345 (+1.2%, mean
+0.2%; 14 of 17 offsets identical, done equal), `idle-50x50` done median 3,947 -> 4,053 (+2.7%; only offset 0 changes,
mean +0.2%), `mixed-100` medians equal. The `--mpai` Inner Circle game (seed 1, 60 s) keeps Retail b353410003e98dd5 and
Legion 0825aeb9e8139203. `ctest:legion_slotwedge` (`mixed2` at offset +2: `g.G.done` -1 on the parent).

**The repair: big points and blocked files** (2026-10-20, task-l4-fixturn; `ribbonLeg`, `ribbonHold`, `kRibbonMaxPoint`,
`kRibbonStall`). As merged, the ribbon gridlocked `bridges-1000` (`ctest:legion_choke`, workers, offset 0: arrived/stuck
862/0 -> 581/155, bisected to the ribbon commit) and cost the 448-body corners the groups at the tail of the queue.
Why, from per-member turn records dumped every 1,000 ticks and the ribbon's waves logged as they rank:

* *A point too big for one ribbon ranks in many small waves, each pinned to the inner wall.* One ribbon is at most
  `kPivotMaxCells` (32) wide, so about 170 bodies of 2x2 at 1.2 bodies per sqrt(n). `bridges-1000`'s armies of 500 are
  ranked in 56 waves of 1 to 110 bodies, 2 to 24 cells wide, each starting again at its turn's inner wall: both armies
  took the 32-cell bridge's north half (their inner corners), met head on in its west mouth and stood there to the end
  (A 166 bodies Holding against the bank, B on the bridge). On `corner-1x448` the stream over A's top fell to 2.3 files
  against 3.7 (`gate.top.files_x100` 229 against 372, `spread_x100` 244 against 515): the corner's drain rate, 63 fewer
  arrivals by tick 9,000, spent on the groups last in the queue (F 28 -> 4). The 0 lane swaps at the top gate were
  that single file.
* *A file can lie across another stream of the same crowd.* Members whose way comes to the vertex from another side
  (the diagonal stream from the west along `corner-1x448`'s rock line) are ranked by the ranker's way in, take outer
  radii they never walk and descend the field along the rock line; the north-bound outer files queued below it stood in
  a lattice of Holding bodies 150 to 1,200 ticks, their arc points across that moving inner file.
* *A turn record outlives a route change* (`bridges-1000`: B members re-routed to the 32-cell bridge still held their
  12-cell bridge turn).

The fix: (1) a point of more than `kRibbonMaxPoint` (4 uplink parts, 256) members keeps the old pinwheel, and a member
whose point outgrows it (a convoy's later clicks joining after its first members ranked) drops its turn record and ranks
again by the old rule; (2) a ranked member that makes no progress on its field for `kRibbonStall` (120) ticks gives up its
file and hugs (the field's own way, as the inner file) until it is past the turn: a hold that cannot be honoured is let
go, it never freezes. Legion only; Retail hash-identical at every gate offset of `mixed2` and `corner-1x448`.

The stall is counted from the member's rank (`Member::rankTick`, stamped when `ribbon()` or the same-side `prevR`
copy gives a radius; the release reads the smaller of the stall and the time since the rank). `ribbon()` ranks
members up to `kRibbonChain` descent steps from the vertex, queued ones included, so measured from the last progress
alone a member that already stood 120 ticks when it was ranked lost its file at its first hold, and the wave's width
was spread over files nobody walked (`corner-1x448` offset 0: the run's first ribbon rank at t=5,924, and u=54, about
90 cells back, released at t=5,926 with `stalled=122`; with the rank clock it holds 414 ticks). Measured against the
repair commit 6ec272e6 over the gate offsets: hash-identical on `corner-8x56`, `mixed2`, `sbend`, `island-1x150`,
`wall-4x50`, `doubleturn`, `slowturn`, `uturn`, `gap20`, `gap10-100`, `corner-1x100` and `attackring-river-64`;
`corner-1x448` differs at 2 of 9 offsets (D done at 4 offsets against 3, D t90 8,012 -> 7,967; top crossings 28 and
arrivals 391 unchanged); `bridges-100` at every offset (B done 9,999 -> 9,932, B t90 9,461 -> 9,547, bridge1
crossings 1 -> 3, statue ticks 39,395 -> 37,526); `corner-4x50` at 2 offsets (medians within 1%); `bridges-1000`
(`legion_choke`) unchanged at 858/0.

Measured (Legion gate medians; 30813a73 / the round-4 merge 891b35a1 / now):

| fixture | key | 30813a73 | merge | now |
|---|---|---|---|---|
| bridges-1000 (workers, offsets 0, 1, -1, 2, -2) | arrived / stuck | 862/0, 853/0, 860/0, 795/1, 745/0 | 581/155 (offset 0) | 858/0, 849/0, 860/0, 795/1, 745/0 |
| corner-1x448 | arrived by 9,000 (median of totals) / top crossings | 389 / 26 | 326 / 0 | 391 / 28 |
| corner-1x448 | B / C t90; F arrived | 7,067 / 6,860; 28 | never / never; 4 | 7,067 / 6,860; 27 |
| corner-8x56 | arrived / top crossings | 418 / 7 | 368 / 3 | 421 / 7 |
| corner-8x56 | B / C done; E t90 | 8,186 / 7,609; 8,466 | never / never; never | 8,435 / 7,050; 8,601 |
| corner-1x100 | t90 / done / statue | 4,187 / 4,862 / 2,288 | 4,155 / 4,760 / 2,704 | 3,962 / 4,532 / 2,080 |
| slowturn | C400 t90 / statue | 3,296 / 2,785 | 3,377 / 3,433 | 3,287 / 2,991 |
| wall-4x50 | A done / C t90 / D t90 | 2,280 / 3,064 / 3,678 | 2,403 / 3,244 / 3,470 | 2,354 / 3,019 / 3,570 |
| sbend | t2 crossings / done / statue | 27 / 3,602 / 754 | 2 / 3,448 / 893 | 2 / 3,377 / 1,040 |
| gap10-100 / gap20-100 | t90 | 2,387 / 2,207 | 2,387 / 2,235 | 2,432 / 2,252 |
| mixed-100 | T2 / T3 done | 4,227 / 4,700 | 3,707 / 4,222 | 3,783 / 4,202 |
| bridges-100 | A / B done | 11,168 / 10,156 | 10,946 / 10,140 | 10,856 / 9,932 |

Unchanged from the merge (hash-identical at every gate offset or equal medians): `mixed2` (mid crossings 10),
`island-1x100` (t90 1,487, lane crossings 191), `uturn`, `gap20`, `doubleturn` (t90 2,497 against 2,342 at 30813a73),
`slowturn`'s W1000 (done 2,481 against 2,357), `strait-2x100`, `idle-50x50`, `corner-4x25`, and in the legion100
screen every fixture but those in the table (`maze-100` done +0.4%, `doorplug100-124` B done +0.5%). The trade is
the corners' lane order: `corner-1x448` 0 -> 28 swaps at the top gate (Retail 5), `corner-8x56` 3 -> 7 (Retail 10),
for their throughput back. Still open: `wall-4x50`'s B is done at 5 of 11 offsets (30813a73 7): one or two bodies
settle just outside the arrival disc behind the convoy's settled arrivals after a slot exchange
(`complete_outside_radius` 1); `sbend` stands 16% more. `legion_cost`: three cost-corner entries moved
(N100 slot search cells 30,888 -> 35,291, N200 re-choice BFS cells 14,819 -> 16,886 and slot search cells
48,592 -> 53,619); whole-run Legion work per unit N100 15.96 -> 15.98, N200 14.51 -> 14.57, N400 11.65 -> 9.94.

Tried and not taken (single offsets unless noted): a later wave's width counted over the turn's earlier waves
(`corner-1x448` arrivals median 310; `bridges-1000` 650/19); a wave limited to members within 12 cells beyond the ranker
or the nearest 169 (741/15, 541/264); ranking only members whose own way comes in within 45 degrees of the turn's way in
(`corner-1x448` 377, crossings 1, but `bridges-1000` 579/212); the old rank for a wave the cap compresses; a stalled
member released as past the turn rather than hugging (`corner-1x448` 418 arrivals but 41 swaps); these together held
the corners at 383-418 arrivals and 6-41 swaps but left `bridges-1000` at 589-800 arrivals with up to 252 stuck over
offsets -2..2. The stall at 60 ticks (`gap10-100` t90 +7%, `wall-4x50` C/D t90 +20%) and at 180 (`corner-1x100` 18
swaps) were worse. Without the slots-by-lane exchange `wall-4x50`'s B was done at 7 offsets, but `gap10-100` done +3%
and the top gate 14 swaps.

### Room at a merge: the corner funnel, round 4 (2026-10-20, task-l4-funnelB)

The target was the single file at `corner-1x100`'s corner (tools/scenarios/legion100: block A with stray rocks 1-3
cells off its top and west faces; the way meets A's top-left corner diagonally, runs along A's top, then turns round
wall B's south end): Legion stopped permille 267 against Retail's 1, front bodies waiting 1,000+ ticks to merge into
the file, `corner-4x25` 251 against 1, without giving back the arrival lead (t90 4,187 against Retail's 5,084) or more
than 3% of t90 or done on `corner-1x100`, `corner-4x25`, `island-1x100`, `gap10-100`, `gap20-100`, `idle-50x50`,
`many-100`, `strait-2x100`, `swap-100` and `maze-100`. This track's direction was the merging and hold rules.

**Retail first.** Retail has no merge rule: each body follows its own path to its own formation offset, a blocked
body clamps, slows and presses on. Its bodies never converge on one line: at offset 0 they cross the column x = 90 over
A's top on rows 18 to 47 (Legion: 72 of 100 in one row, the first clear of the face's rocks), 51 of them in the
300 ticks after tick 900, and none queues. It is slower over the whole way (t90 5,084).

**The cause** (offset 0; positions every 10 ticks, and every hold's blocking bodies with their state and speed, a
debug trace kept as the `hold` TRACE line). The formation reaches the corner at tick 599 as a block (x 47-80, z 55-74)
whose north edge is the shortest line past the corner, origin row 55. Its north rows walk that line east in one file;
every other body descends into the wedge between the line and the rock comb on A's west face, and packs there (a
lattice of held bodies, x 60-79, z 57-72, from tick 600 to 1,800). The corner's front bodies have one legal way down,
north into the file (the rocks close east and north-east): body 50 at origin (77,57) asked for (77,56) on every update
from tick 561 to 1,839, refused each time by a file body, moving or stopped. The file is a stop-and-go queue (1.1 px a
tick at x 60-80, a third of its samples held behind the body ahead), and it is fed at its open upstream end: after the
16 bodies already on the line, the next to pass the corner were 22, 32, 41, 51, 31, 61, 71, 81 -- the crowd's rear, which
stood at x 46-61 at tick 599 and stepped north onto the line at x 54-60, where nobody passes. The front bodies (50, 40,
30, 60, 80) passed last, at 1,899-1,969. Nothing in the hold rules favours them: a moving body never makes room, the
yield rules ask only settled bodies, and a local detour routes through moving bodies and is dropped after 30 held
ticks. One file passes the corner about every 30 ticks (8-13 per 300 until tick 1,500), the crowd behind it waits, and
the rows north of the line (z 53 and up) stay empty all the while. Turn B's "pocket among the stray rocks" is the east
end of this lattice: its bodies are the inner files, queued behind the front. (A second, smaller loss is the pinwheel
at (82,55): 54 of the 100 bodies rank there for the turn at A's far corner and aim 18 cells north for one cell, so each
turns north and back at the facing cap, 5-10 ticks each; that is turn work, not changed here.)

**The change: room at a merge** (`makeRoom`, `queueHead`, `Member::want`). A held formation member on a move-like leg
whose every way down the field is covered by members of its group that have waited less than it -- a stream passing,
or its own queue behind -- heads its queue and records the origin it is blocked from (hashed; indexed in `wants`).
Once it has waited `kRoomHeld` (60) ticks, a member of its group that passes it -- moving, outside twice its
destination area's bound, its step this update leading on within `kRoomLook` (24) origins onto that origin -- moves
over a lane instead: one step forward and outward (or straight outward), away from the head, no higher on the field,
as a committed flow shuffle at travel speed. The head steps into the lane the passer left, and the passers walk the
next lane out, so the file past the corner becomes lanes. Passage admissions, head-on meets and passage lanes keep
their own lanes. Retail is untouched (hash-identical: corner-1x100 and corner-4x25 at offsets 0, +1, -1; the
`--mpai` Inner Circle game, seed 1, 60 s, keeps Retail `b353410003e98dd5` and Legion `0825aeb9e8139203`).

**Preempting** (the user's idea: shape the stream before the corner is in a body's own look-ahead). The route ahead is
known static terrain (Legion plans on the whole plane; fog is local display and nothing here reads it), so a passer
reads the head's want 24 origins ahead along its step and moves over before it gets there. That look-ahead is most of
the effect: at `kRoomHeld` 60, gate medians (t90 and done against 30813a73, then stopped permille):

| look-ahead (origins) | corner-1x100 (standing 267) | corner-4x25, group means (standing 251) | corner-1x448 t90, group mean |
|---|---|---|---|
| 1 (the next origin only) | +0.0% / -0.8% / 264 | -2.6% / -0.7% / 235 | -4.8% |
| 12 | -6.4% / -4.8% / 225 | -6.1% / -4.7% / 215 | -10.5% |
| 24 (kept) | -5.4% / -6.5% / 232 | -8.0% / -7.1% / 216 | -12.1% |

**Measured** (Legion, Windows, offsets -8..8, corner fixtures -4..8, `corner-1x448`, `corner-8x56` and
`strait-2x100` at their gate offsets; medians; hashes equal to Linux):

| fixture | key | 30813a73 | room at a merge | Retail |
|---|---|---|---|---|
| corner-1x100 | g.A.t90 / done / stopped permille | 4,176 / 4,837 / 261 | 3,962 / 4,561 / 232 | 5,084 / 5,547 / 1 |
| corner-1x100 | bodies still 900 ticks (still900_ever) | 4 | 0 | |
| corner-4x25 | t90 / done (mean of the groups' medians) / standing | 3,813 / 4,127 / 251 | 3,552 / 3,806 / 217 | |
| corner-4x50 | t90 / done (group mean) / standing / still900 | 5,758 / 6,596 / 390 / 65 | 5,161 / 5,790 / 359 / 10 | |
| corner-8x56 | t90 / done / standing / top crossings | 6,644 / 7,663 / 544 / 7 | 6,333 / 7,069 / 522 / 11 | |
| corner-1x448 | t90 / done / standing / top crossings | 6,992 / 8,357 / 581 / 26 | 6,166 / 7,772 / 546 / 28 | |
| gen1-route-36057 | g_zonter t90 / done / standing / still900 | 7,926 / 8,556 / 238 / 67 | 7,341 / 7,935 / 201 / 3 | |
| gen1-route-45941 | g_zonter t90 / done / standing / still900 | 8,035 / 8,926 / 237 / 45 | 7,572 / 8,453 / 205 / 0 | |
| gen1-route-65845 | g_zonter t90 / standing / still900 | 8,286 / 197 / 46 | 7,701 / 166 / 0 | |
| sbend | t90 / done / t2 crossings | 3,126 / 3,547 / 26 | 2,927 / 3,262 / 17 | |
| island-1x100 | t90 / done / lane crossings | 1,937 / 2,477 / 233 | 1,952 / 2,480 / 236 | |
| gap10-100, gap20-100 | t90 | 2,387, 2,218 | 2,367, 2,236 | |
| strait-2x100 | g.B.t90 / done | 2,057 / 2,667 | 2,095 / 2,578 | |
| idle-50x50, many-100, swap-100, maze-100 | arrival t90 | 3,270, 687, 7,527, 8,290 | equal | |

The merge waits themselves, from the hold trace (the longest run of updates a body is held with a moving body among
its blockers; offsets 0, +1, -1): `corner-1x100` 705-938 -> 301-390 ticks, bodies over 500 ticks 11-18 -> 0;
`corner-4x25` 587-718 -> 357-507, over 500 6-12 -> 0-1. The longest standing of any body on its way (every 10 ticks)
falls from 1,230-1,510 to 750-830 on `corner-1x100`: those are the inner files' queue inside the rock comb, not merges.
In the legion100 regression screen (all 23 fixtures, gate offsets) the largest moves elsewhere are `mixed-100`'s T3
done (+3.1%) and `doorplug100-124`'s A t90 (+2.9%). `ctest:legion_cornermerge` (corner-1x100 at 0,
+1, -1: nobody still 900 ticks, stopped permille at most 245, t90 at most 4,100; the parent 28-36, 267-282 and
4,157-4,367).

**What it costs.** `wall-4x50` (four orders of 50 round a wall end to one point): groups A and B finish later (done
medians 2,280 -> 2,387 and 2,828 -> 3,108) while more groups finish at all (36 -> 31 of 68 group-offsets never done);
`gen1-route-54772`'s worst group done +4%; `corner-8x56`'s top crossings 7 -> 11 and `island-1x150`'s lane crossings
309 -> 318. One `legion_cost` entry moved: cost-corner N200 trace cells 4,646,605 -> 4,725,163 (+1.7%, the passers
walk more distinct origins, each a straight-line probe; the old band was already within 1%); whole-run Legion work
N400 9,546,699 -> 9,380,028. The standing is down 11-14% on the corners, not to Retail's: the crowd still packs into
the wedge before the corner, and the corner's head still passes in one file.

Rejected on the way (gate medians on Linux unless said; each switched by a debug variable, none kept):

* *A zipper* (a moving body does not step into the origin a queue head held 20 ticks waits for): the stream stops for
  each merge, as round 3's yield did; the longest standing stays 1,240-1,950 ticks (bodies over 500: 13-38 against
  44-52), t90 4,148-4,553 against 4,157-4,502 -- the crowd behind still drains through one file.
* *Moving over for any held body* (no head test, 10 ticks, no look-ahead): `corner-1x100` t90 -7.5%, standing 217;
  `many-100` arrival t90 +10.6% and `gap10-100` done +4.5%. With the head test and no look-ahead: -4.4% / 232.
* *A 10-tick head with a 12-origin look-ahead* (the first candidate, branch task-l4-funnelB-c3): the best corner
  numbers on the wide sweep (`corner-1x100` t90 -9.4%, done -7.9%, standing 261 -> 198; `corner-4x25` -12%;
  `corner-4x50` -20%; the generated maps -9..-10%; worst `wall-4x50` A done +4.7%), but two round-2 checks fail:
  `ctest:legion_mixedformation` (its 20 ground bodies at their wall end 2,315 -> 2,609 ticks, a flyer stalls one
  window) and `ctest:legion_landedflyers` (the 3x3 block's stand median 389 against the bound 276; the 2x2 block's
  1,760 -> 1,102). Moving over for 10-tick holds acts on the ordinary stop and go of a crowd. 30 and 45 ticks with
  24 origins: `ctest:legion_flowshuffle` (island-1x100 done 3,106 > 3,000 at one offset) and, at 45,
  `ctest:legion_lonetrees` (two to-and-fro runs) fail; at 45 `gap10-100` done +5.8% (wide). 50/32 passes the checks
  but `island-1x100` done +5.3%.
* *No moving over while on a pinwheel arc* (it fixes mixedformation at 10 ticks): `wall-4x50` B done +42% (wide).
* *Only heads pinned against terrain* (a statically closed neighbour): landedflyers as the parent, but `corner-1x100`
  t90 -6.2% and `gap10-100` done +4.7%, `wall-4x50` +8% (wide).
* *Keeping the new lane* (for 200 ticks a passer that moved over does not flow back inward): island-1x100 crossings
  -29..-56, but `wall-4x50` done +7%, `uturn` +6.7% (wide).
* *A yield when there is no room to move over*, *four times the destination area*, *a second free lane outward* and
  *no moving over next to a body outside the navigator*: no better, or no effect.
* *The pinwheel off* (a measurement): the file passes at a steady 12-14 per 300 ticks, all on one row, the last body at
  3,079 against 2,899 -- the ranks are what put a few bodies on outer rows late in the base run.

What is left: merge waits of 300-500 ticks (the ~200 aimed at is not met), the crowd's packing before the corner
(standing 232 against Retail's 1; lanes formed before the corner, or a clearance cost on the field that keeps the inner
files out of the rock comb, are the other directions), and the pinwheel's one-cell aim at (82,55).

### Legion 10,000: where the program left it

docs/legion-10000-report.md gives the measured result against the starting revision c2c25707:

* what is met and missed per workload;
* the regressions found in the final validation;
* the remaining bottlenecks: crowds at corners and narrow mouths, the queue for many unrelated destinations, and
  serial movement as the tick.

The stage sections above describe the code as built. The "Known weaknesses" section below predates the program.
Where it disagrees with the report on a 10,000-body workload, the report's numbers are the current ones.

## Instruments

What each tool sees, and what it cannot. A number is only as good as the
instrument that produced it; the "blind to" column is where an earlier audit
went wrong.

| Instrument | Sees | Blind to |
|---|---|---|
| `legion_world_test` (38 cases) | arrival, spins, cell oscillation, legality, hold and detour counters on synthetic 2x2 movers (instant acceleration, flat 16-px cells, feature walls); per-case metrics from the shared observer | real turn rates, combat, timing, multi-group selections |
| `legion_acceptance_test` | single unit, group, jagged, trapped, crowd-held, in all five-era modes | anything outside its seven fixtures |
| crowdbench (`crowdbench`, `crowdbench_matrix`, the nightly screen) | 18 scenarios x two modes x several populations, deterministic keys only, Retail on the same binary | formation play: its orders are per-unit goals, which form no formation |
| `.scn` scenarios and `legion_scenario` (`tools/scenarios/`) | the client's real order path (`issueSelection`: 64 commands per tick, the per-axis offsets for non-shared units, SetSquad), serial and `--workers`, three start offsets, the observer's keys; on stderr, per run, the wall time of the whole run and of `World::tick` per tick (`tick_max_ms=M@T`, the slowest tick and which it was, and `tick_p99_ms`; the observer's sampling is outside them, and none of it reaches the JSON) | the AI, fog and economy; production; anything not in the file |
| `tools/legion_observe.h` | progress, held-by-design vs no-progress age classes, the stall clock against the headway clock (`stall_stale_ticks`, Legion only), lane crossings and files, contact and clearance, flyer metrics, per-tick work counters (`work.legion_total`, and `work.legion_all`, which adds the awareness, lane and pinwheel work the W7 and W8 instruments report, `aware_work`, `lane_work` and `pivot_work` less its one unit per line sweep; report-only); a command is one convoy (W3 exit, lead ruling W3 round 4 (1)): `legion_scenario` runs the file's clicks through a `ConvoyTable` of its own, so selections clicked to one point a few ticks apart are one command for `contact_settled` and share one arrival disc sized for all of their bodies, in both modes | wall time (never read); it is const and adds nothing to hashed state (`observer_neutral`) |
| `tools/scenarios/baseline.json`, `--check` | the committed base: exact keys, one-sided bands, work counters, the Retail floor, exceptions with their clusters | nothing it was not asked to key; a new key needs a new base |
| `legion_cost` | work per member-tick W(4N)/W(N) and slot-assignment work per slot | wall time |
| situations (`tools/scenarios/situations/`, `scn_truth`) | a moment cut out of one of the user's recordings: exact bodies, the orders in flight, the next 600 ticks of commands, and the recording's own positions 300 ticks in | what is not in the snapshot: economy, scripts, AI, production, units in transports; the first 100 ticks follow the recording (two of six moments clear 90% of bodies within 2 cells) but by 300 none does (best 85%), because the Legion group and mission state of bodies already in flight is not in the file; so they gate nothing (README in that directory) |
| replay checkpoints (format 12) | the state hash and `posDigest` at every checkpoint; the verifier reports "state diverged at tick T1; positions/hp/orders match until tick T2" (an older recording gets its digests from `TAK_POSTRAIL` run on the recording build) | a replay plays back exactly only on the build that recorded it |
| `TAK_PACELOG`, `tools/pace_check.sh` | ticks published per rendered frame, sim inbox depth, the 512-tick fast-forward, bursts; the presentation clock (WE S4): displayed time per frame, its CV and rate CV, freeze frames, snaps, the lag target, at a fake vsync (`TAK_VSYNC_HZ`, `--vsync`) | the real renderer and GPU: a headless run draws nothing (see `TAK_FRAME_MS` / `TAK_VSYNC_HZ`); `TAK_QUIT_TICK` ends an interactive run for a real-display log; report-only |
| `tools/legion_timing.sh` | per-tick mean and p99 on an idle pinned core, A/B interleaved, N of 4 or more | never a gate: wall time is reported, not asserted |

Rules that came out of using them:

* **Retail and determinism checks are exact; a Legion hash is exact only for the
  steps declared hash-identical; outcome keys and work counters are one-sided
  bands against the current head's base; wall time is always reported and never
  gated.** The policy is in the Legion improvement plan, section 3.0.
* **A replay is not a regression medium.** It runs only on the build that
  recorded it. After a behaviour change it diverges at the first changed
  decision and the commands then address units that were never built. The
  situations exist so a later build can still run the moment.
* **A weakness without a scenario, key or test behind it is not a known
  weakness.** `tools/legion_doc_links.py` (ctest `legion_doc_links`) fails when a
  cited scenario, key or test does not exist, or when the field quota quoted
  here differs from `kFieldQuota`.

## Known weaknesses

* **The detour rules read a record across two measures.** The detour
  back-off restarts, and the yield-first rule looks for a gain since the last
  detour, on `Member::nearest`: the least distance still to go since the order
  began, in squared cells or potential x64, whichever the body read -- the
  scale mix the stall clock dropped on 2026-10-14 (section 4). A body that
  registered before its field sets no record until it is near its goal, so in
  a melee or a long jam its back-off grows to the 480-update wait. Restarting
  it on the stall clock, after a gain of 2 or 4 cells, or halving the count on
  a gain all trade detours and reversals against standing samples in
  `battle-assault` (11 offsets: detours +11% / +3% / -3% / +8%, standing
  -2.3% / -0.7% / +0.9% / -2.2%), so none was taken without a retune judged on
  its own. Measured by `battle-assault:work.detours.total` and
  `battle-assault:stopped_permille`.
* **Packed distinct goals: the crowdheld acceptance check is a ratchet, not a
  pass-all.** `legion_acceptance_crowdheld_legion` reaches 62 of 64 in
  goal and passes at 61 or more with 1 unit ever terrain-stuck (spin passes at 0; 59 and 1 before
  speed-matched following). Since 2026-10-06 the observer gives a unit one
  progress window after it stops being trapped (as after a command), so the
  gate's front unit, already moving at full speed, no longer counts; the
  remaining unit is sealed out: the door lets a shallower unit of the same
  goal row through first, and once it settles the deeper goal is reachable
  only through neighbouring goals. Shifting goals into one-body aisles reached
  64/64 but added spin and aisle deadlocks and regressed rapid replacement and
  doors; having settled bodies step aside peaked at 63/64 and was unstable.
  Group right-clicks use a shared point with re-choosable slots instead of
  fixed packed goals, so this layout does not arise from player orders.
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
  is not affected. Measured by `ctest:legion_acceptance_crowdheld_legion`
  (the 61/64 ratchet) and `ctest:legion_crowdhold`.
* **Limits accepted at the close of the 2026-10 plan (user decision W9-1).**
  W9 cleared the head-on jam; eleven harder gates resisted every local fix
  (each attempt broke more scenarios than it fixed, so the stop rule reverted
  it) and are accepted as known limitations, re-openable later. Lane order:
  `corner-1x448` still swaps 7 bodies across the top gate (Retail 0; median of
  the gate offsets), while `corner-8x56`'s 1 is within Retail's 2 since the
  2026-10-14 review. A long
  strait swapped 22 (Retail 8) and wider gaps 2-5 per file (Retail 0), because
  Legion takes gaps several abreast; since the 2026-10-14 review the passage
  lanes (above) bring the strait to 3 and gaps up to 14 cells to none, while
  a 20-cell gap, wider than a 48-body formation's band, still swaps 1.73 per
  file (`gap20:gate.top.crossings`); since the 2026-10-19 wide-gap lanes a
  100-body formation, whose band is wider than that gap, swaps none there
  (`gap20-100`, Retail 3). Walls: an attack ring against a wall and
  the bodies queued in doorplug-112 touch the wall about twice as often as
  Retail (since the 2026-10-19 ring upkeep the 64-body wall ring touches it
  29 permille against 58 before and Retail's 20). Tails: a Move group crossing
  its own firing ring arrived about 11% later than Retail (since round 2 within
  4%, see the next entry); one motion-cross body never finishes from some start
  offsets (a doorplug-124 body did too, until the review's slots by arrival:
  its group A now completes at all eleven gate offsets); a lifted flyer
  detours 5 cells against 2.
  The attempts are parked on branches task-w9-a-A1..A4 and
  task-w9-b-b1..b6-reverted with their numbers. The W9 probe that removed the
  corner hug (B3, every rank wheels at least a cell off the wall) was retried
  on 0.8.6 in round 3 (2026-10-20): corner-1x448 26 -> 20, corner-8x56 8 -> 4,
  but corner-8x56's later groups no longer reach t90 in 9,000 ticks (21 fewer
  arrivals over groups E-H), so it was not taken (see "Lane order, walls and
  reversals at ~100 bodies, round 3"). At 0.8.6 the gate medians are
  corner-1x448 26 (Retail 5), corner-8x56 8 (Retail 10), mixed2 38 (Retail 0)
  and sbend's second turn 27 (Retail 5). The pinwheel misses most turns at a
  wall end met across it (same section); a candidate fix is on branch
  task-l3-lanes3-turnfix. Since round 4 (2026-10-20, "Lane order through
  turns: the ribbon") mixed2 and sbend are 10 and 2. The 448-body corners went
  to 0 and 3 at first, at a cost in throughput (corner-1x448 326 of 448 arrived
  by tick 9,000 against 389); since the round's repair a point of more than 256
  keeps the old pinwheel, so they are back to 28 and 7 with 391 and 421
  arrivals ("The repair: big points and blocked files"). Measured by
  `corner-1x448:gate.top.crossings`, `corner-8x56:gate.top.crossings`,
  `strait-2x150:gate.strait.crossings`, `attackring-wall-64:wall_touch_permille`,
  `doorplug-112:wall_touch_permille`, `ringcross:g.M.t90`,
  `doorplug-124:g.A.done`, `motion-cross:click.u01.done` and
  `ctest:legion_liftflyers`.
* **Faster detours cost travel reversals (2026-10-19).** A local detour
  route with 4 or more cells left is walked at travel speed, unturned (the
  departure fix above), so a body that takes a detour and then rejoins its
  field turns its travel direction round more often: travel reversals over
  the gate offsets (median) corner-1x100 38 -> 81, corner-4x25 22 -> 68,
  densehead 77 -> 155, all still far under Retail (corner-1x100 441); the
  corner and island fixtures stand 10-16% more (`stopped_permille`) and
  doorplug100-112's queue 628 -> 733 permille. A stream reaches parked flyers
  before they have lifted and stands against them longer while finishing
  sooner (`ctest:legion_landedflyers` bounds moved), and one ordinary
  side-step shuffle runs out and back once among the lone trees
  (`ctest:legion_lonetrees` allows that one event). Faster only from the
  second detour, or only after 120 blocked ticks, kept both checks but left
  stuck bodies at some many-100 offsets. Measured by the reversals key of
  tools/scenarios/legion100/corner-1x100.scn and densehead.scn, and by
  `ctest:legion_departure`.
  Since protocol 280 a formation member's flow-around shuffle keeps travel
  speed too ("Flow-around at travel speed"): reversals over the 158 fixtures
  +3.5%, stopped permille +3.6%, and lane swaps where streams cross after a
  turn rise (`mixed2:gate.mid.crossings` 13 -> 38, Retail 0;
  `sbend:gate.t2.crossings` 9 -> 27, Retail 5). A denser crowd jams in
  `pocket-615-wall` (`g.A.arrived` 533 -> 474, Retail 157).
  Where the reversals come from (round 3, 2026-10-20, offset 0, each return
  to a 16 px cell left under 90 ticks before attributed to the steering
  branch the body was in): corner-1x100 82 = 33 walking a brisk local detour
  route, 13 a flow-around shuffle at travel speed, 10 one at the facing cap,
  10 field descent, 8 straight walks, 7 lane passes; corner-4x25 39 of 62 on
  brisk routes; island-1x100 30 on routes, 19 brisk shuffles, 18 pinwheel;
  densehead 73 straight walks (the head-on meet), 34 routes, 28 brisk
  shuffles. A route reverses 1 to 6 updates after it starts, with 15 to 43
  cells still to walk: it backs out of the pocket among settled bodies the
  body walked into, which is what the route is for. corner-1x100's standing
  is not at the goal: 2,062 of its 2,123 standing samples (every 30 ticks)
  lie more than 80 cells out, between ticks 600 and 2,100, west of the
  block's corner, where a body that reached the corner first waited 1,322
  ticks to merge into the single file passing it (see the round-3 section).
  Round 4's ribbon does not reach it: the file forms in open ground where the
  octile descent climbs onto the line along the block's top, before any turn
  is in the bodies' look-ahead (standing 268 permille).
  Round 4's room at a merge does ("Room at a merge"): members passing a
  queue head held 60 ticks move over a lane: corner-1x100 standing 261 -> 232 (wide sweep),
  merge waits 705-938 -> 301-390 ticks; the crowd still packs into the
  wedge before the corner, measured by `ctest:legion_cornermerge`.
* **A narrow passage's waiting crowd stands on the other way's exit apron.**
  Until 2026-10-20 a head-on knot in a bridge's mouth was a matter of timing
  (`taperbridge-100` +1: 19 of 100 arrived): every turn of the passage put the
  next way's crowd onto the apron the last way was coming out onto. A way now
  stays open for two of its crossings ("Passage turns and small groups"), so
  long passages turn once or twice, and over offsets -16..17 neither
  `bridges-100` nor `taperbridge-100` gridlocks (`bridges-1000` -8..8: no bridge
  knot). The crowd still waits there, though, and the stream threads through it;
  a passage whose two ways keep arriving turns more often and is still exposed.
  Holding the token for admitted bodies in the mouth, parting waiting bodies for
  the stream, waiting until the far apron is clear, and stepping waiters out of
  the passage's lane each moved the knot elsewhere or made it worse (above).
  Measured by `taperbridge-100` (tools/scenarios) and `bridges-100` (legion100)
  over the wide offsets; `ctest:legion_passage_admission`,
  `ctest:legion_passage_taper` and `ctest:legion_passage_green` hold their
  offsets.
* **Attack rings at ~100 bodies (after round 4, 2026-10-20).** Rings round a still target walk their detours at
  travel speed, spend the spots their firing arc closes off, send spare attackers to the outer bands and press flush
  against 1-cell walls the way Retail's clamped bodies end up ("Rings at ~100 bodies, round 3"). Since round 4 the
  shadow by a wall fills: band-1 spots are handed to waiting members, a body in reach out of sight walks on through
  the shadow to ground in sight, and blind rings (the shot needs sight, no spot has it) press into reach and stay
  ("Rings along walls, round 4"). Still open: attackring-wall-100 stands 36 ever in reach over the gate
  offsets against Retail's 49 (58 against 82 on the core five; Retail packs sub-cell points of the box's corner
  origins and jostles bodies in and out); the gatehouse's west-gate offsets are plugged by the first body walking
  straight at a spot beyond the yard wall and braked flush in the corridor's mouth (attackring-gatehouse-40: 16 ever
  in reach and 320 damage over the gate offsets against Retail's 29 and 584; over -8..8 its damage is level with
  Retail's at the median, 612 against 711 mean); a ring across a river stands 17 in reach against Retail's 21 ever and 14 now; statue ticks stay a few
  times Retail's (presses toward reach keep the facing cap), wall touch is near Retail's on wall-100 (20 permille against
  17) and above it by the box's walls and the gatehouse (wall-64 32 against 20, gatehouse 78 against 68). (These
  ring figures were measured on task-l4-ringwall alone. Merged with the fight track, its still-window rule stopped
  the queue short of the shadow, and wall-64 fell to 35 ever in reach. Since task-l4-fixring the members walls stand in the way of
  are exempt from that rule ("Rings along walls with the still-window rule"). Over the gate offsets wall-64 now
  stands 39 / 37 ever / now, wall-100 38 / 37 and the gatehouse 18 / 15, with wall reversals back near ringwall's:
  wall-64 317, wall-100 292. Since round 5, "Pressing into the shadow", bodies held just outside reach of such a ring
  press into the shadow and hold there: wall-64 39 / 38 ever / now at the gate median, sums over the offsets 490 / 486
  against 479 / 469, Retail 532 / 480; wall-100 39 / 38; the gatehouse 18 / 15 with its plug unchanged.) Against a
  target walled in on every side Legion outfires Retail (it
  seeks flush points on every face that has one). A Move group crossing a firing ring is level with Retail's t90
  (ringcross-100 3,841 against 3,926; completion 4,194 against 4,166 since round 4). Since round 4 (2026-10-20,
  "Fight and guard rings round still targets") fight and guard members round a
  still target walk at speed too (attackring-fight-60 statue ticks 32,402 -> 372, attackring-guard-64 42,133 -> 1;
  Retail 38 and 194), a route keeps speed to its end, and a member with no headway for a crowd window stops
  shuffling, which brings round 3's reversals down (attackring-r200-64 413 -> 85, ringcross 420 -> 200,
  ringcross-100 351 -> 144 over -8..8; Retail 45, 61, 88). Still open there: statue ticks and reversals are still
  above Retail's on the open rings (r200 380 against 155; ringcross 780 against 150); bodies stand more (stopped
  permille up to 32 above round 3 on the rings, 150-200 on fight and guard, past Retail); bodies held behind an arc
  no longer creep round it to the far side's free spots (attackring-f1-64 ever in reach 43 -> 39, Retail 45); and
  the fight orders' attack groups got no ring (their field starts with one member; see the round-4 section), so
  a fight order had fewer bodies in reach than Retail (attackring-fight-60 40 against 44,
  attackring-fight-diag-60 41 against 44). Since round 5 ("Fight rings and the reach edge") a group that grows to
  eight plans a late ring, and a press may end exactly on the reach edge: 44 in reach on both, fight-60 deals 2,867
  damage against Retail's 2,814 and the diagonal approach 2,070 against 2,167, still later into reach there (t50
  1,733 against 1,631), while statue ticks and reversals on the fight rings rise toward and past Retail's. A fight order's members on their fight leg now shuffle at travel speed
  too (round 4's follow-up: attackring-fight-diag-60 statue ticks 3,918 -> 3, attackring-fight-60 372 -> 3; Retail
  23 and 31); in exchange `fight-retarget`'s stragglers settle at the packed disc's edge more often (7 of 17 runs
  never done against 4; Retail 0) and the battles reverse and lose more bodies. Measured by
  `ctest:legion_fightstill`, `ctest:legion_fightshuffle`, `ctest:legion_guardstill`, `ctest:legion_ringroutespeed`,
  `ctest:legion_ringshuffle`, `ctest:legion_ringupkeep`, `ctest:legion_attackwalled`, `ctest:legion_attackflush`,
  `ctest:legion_ringcross`, `ctest:legion_ringfill`, `ctest:legion_ringclosed`, `ctest:legion_ringstill`,
  `ctest:legion_ringwait`, `ctest:legion_ringshadow`, `ctest:legion_ringbend`, `ctest:legion_ringblind`,
  `ctest:legion_ringpocket`, `ctest:legion_ringroute`, `ctest:legion_ringnosight`, `ctest:legion_fightring`,
  `ctest:legion_ringedge` and `ringcross:g.M.t90`.
* **The livelock fix costs some lane order (protocol 257).** A body blocked
  again after a side-step toward its arc may not step straight back, so it
  takes another field step inward or holds and side-steps: the 150 bodies
  round `island-1x150`'s rock swap files 55 and 43 times over the gate offsets
  in the strip and abreast windows (10 and 9 before). Gap, wall and corner
  fixtures swap a few more too (`gap10` 91 to 138), while summed over every
  fixture the motion keys moved both ways ("Livelock" above). Walk-in-place is
  not gone in crowds: 34 unit-seconds were left in the recording's situations
  (857 before). Measured by `island-1x150:gate.abreast.crossings`,
  `island-1x150:gate.lane.crossings`, `gap10:gate.top.crossings` and
  `ctest:legion_slideback`.
* **Approaching an unreachable goal counts as trapped movement.** The
  acceptance and matrix observers classify a unit as Trapped whenever its
  origin cannot statically reach its goal disc, and any move or turn more
  than 150 ticks after that counts in `trapped_units_moving_after_grace`.
  Walking to the nearest reachable point therefore counts for every unit in
  the unreachable scenario, and in recovery and recovery-passive before the
  wall clears: Legion scores 200/2000 there, as Retail and Retail+ do, while
  Flowfield and Cooperative score 0 because they never move (and arrive 0 in
  recovery 2000, where Legion arrives 2000 of 2000 since protocol 240, 1999
  before). Units already at their point have zero spin and do not move, and
  since protocol 240 their orders drop there: in recovery-passive (the wall
  removed at tick 2000, the original orders kept) Legion now reaches the goal
  with 0 units, as Retail does (before: 200 of 200 and 1999 of 2000).
  Pockets under 256 origins still hold in place; a region just above that
  threshold walks to its edge. Measured by
  `ctest:legion_unreachable` and `ctest:legion_acceptance_trapped_legion`.
* **CPU cost at large populations is 20-50% above the cheapest mode.**
  Five-mode timing at a78d194 (before the review-4 fixes), 8 parallel
  P-cores, noisy, mean ms per tick at 2000 units: doors 3.99 against 2.77
  (Cooperative), opposing columns 3.83 against 2.77 (Flowfield), shared goal
  3.40 against 2.45 (Flowfield). Legion delivers the most arrivals in those
  cases. At 200 units it is the cheapest mode or tied in most cases. Field
  work is not the cost; counters point to per-unit steering: line sweeps (640
  cells for formation members, rerun on every origin change), descent, held
  rechecks and the per-held-member slot re-choice BFS. Those counters are
  `cost-open:work.line_sweeps.total`, `cost-open:work.held_rechecks.total` and
  `cost-corner:work.slot_search_cells.total`; `ctest:legion_cost` gates the
  growth of that work from N to 4N bodies.
* **Remaining losses.** Opposing columns at 2000x1 cross about 1076 against
  about 1176 for Flowfield; opposing columns 500x1 and 250x8 at 50% moving
  also trail (in the latter, idle bodies stand between movers and their
  goals, which is idle-obstacle flow, not passing). Exploration at 2000
  units ends with 0 arrivals in every mode. The opposing-columns rows live in
  the crowdbench screen (`ctest:crowdbench_matrix`); the nearest `.scn`
  measure is `densehead:stopped_permille`.
* **Open items from the review-4 fixes.**
  * Under constant static churn a refresh no longer restarts on the next
    change (b565029): it keeps building, is installed still stale, and another
    follows. A refresh under a change every tick finishes about every 85 ticks
    (`refreshchurn:work.refresh_completed.total` is 10 in 900 ticks; the control
    without churn, `refreshchurn-control`, completes none).
  * A first field built across epoch changes mixes old and new plane
    legality. This is safe, because the mover re-proves every step, but it
    can misdirect until the clean rebuild. Not measured by a scenario of its
    own; `refreshchurn:spins` shows no spin while it happens.
  * The 640-cell sweep and the 49x49 re-choice BFS are bounded per member,
    not globally (`split-450-open:work.trace_cells.max`,
    `split-450-open:work.rechoice_bfs_cells.max`). The formation slot searches,
    `assignFormation`'s first burst among them, share one budget a tick since
    the 2026-10-14 review (item 5, `ctest:legion_handout`).
  * A member queued behind its own crowd settles where it stands, out to twice
    the crowd's reach, so it can stand outside the benchmark's authored radius:
    its order completes, but it does not count as arrived_settled
    (`corner-8x56:g.A.complete_outside_radius`).
  * Point cell claims do not follow a body that yielded
    (`corner-4x50:stopped_permille`).
  * The review-4 fixes were not screened at 500 or 1000 units, with 4 or 8
    players, at 12000 ticks, or on dynamicobstacle, rapidreplacement and
    exploration. Findings 2, 4, 5, 6, 7, 8, 10 and 11 have no dedicated test;
    `battle-field-2x60:spins` and `ctest:crowdbench_matrix` are what watch them.
* **A body held off its own slot by settled bodies of its own footprint**
  (resolved 2026-10-20, Legion ~100 round 3): a settled arrival covering the
  waiting body's own slot now counts as nearer, whatever its footprint
  ("Corridor mouths and own-slot arrivals"). The seeded screen in
  `legion_world_test mixedrandom` (TAK_MIXRANDOM=0,12799) leaves no layout open
  (3 on the parent; 5 in the 2026-10-14 review's own scratch screen, which is
  gone). Measured by `ctest:legion_mixedrandom`, `ctest:legion_mixedsettle` and
  the crowdbench `mixedfootprints` rows (`orders_complete`).
* **A 2-cell dead-end corridor's front can settle short of its room.** Since
  round 3 (2026-10-20) the mouth of a corridor one body wide no longer jams
  ("Corridor mouths and own-slot arrivals": every 120-body 2-cell dead end
  completes at 40 of 43 offsets, none did before). The 40-body room at the end
  of one still leaves orders open at 11 of 43 offsets (`deadend-w2-n40-room-plain`,
  24 of 43 for `-squad`): the first bodies through the corridor stop for a
  window a few cells before the room, settle where they stand (their potential
  is within the area's bound), and the queue settles back from them, while the
  room's side by the door stays empty; the bodies past the explicit area's
  ceiling then wait (the parent does the same at offset -3). In the
  120-body fixtures at offsets +28..+30 the column's last row, spawned two cells
  from the map's edge, never leaves Waiting (on the parent too). Measured by
  `deadend-w2-n40-room-plain:g.A.complete_n` over wide offsets and
  `ctest:legion_funnelmouth`. Two waiters whose ways cover each other at one
  potential (a pillar field with gaps one origin wide) no longer hold each
  other: the lower id goes first (`ctest:legion_funneltie`).
* **Group partitioning depends on registration order** and on field start
  timing (a started field takes no new seeds). The 256-goal cap chunks goals
  in registration order, so an order whose unit ids are not spatially
  coherent can get spatially interleaved groups
  (`split-450-open:g.A.arrived`, and the 8-group `corner-8x56:g.A.arrived`).
* **One-way admission costs small groups on wide-enough passages.** A narrow passage (16 cells or less) is admitted
  one direction at a time. With the green scaled to the crossing (2026-10-20) `bridges-100`'s one-way crossing beats
  the old two-way one (`scale.arrive.t90` gate median 9,920, the two-way 10,237 when it did not gridlock, 11,037 on
  0.8.6), so two-way flow on its 12-cell bridge, where two lanes each way fit, was not brought back. Since the same
  day groups of fewer than 8 (`kOpeningMembers`) wait their turn too (`taperbridge-small`: no gridlock where 16 of 17
  offsets knotted); on a 12-cell bridge, where a few walkers used to get through head on, they now wait (a scratch
  fixture of ten orders of 5 against an army of 50: t90 +4%). The other way can wait up to two crossings plus one
  (`taperbridge-100`: the second army waits for the whole first). Measured by `ctest:legion_passage_admission`
  (arrivals and stuck bodies; the time is in its run's JSON), `ctest:legion_passage_small` and `ctest:legion_choke`
  (bridges-1000).
* **Idle enemy units never step aside -- as in Retail.** Settled friendly
  Legion arrivals yield and idle friendly bodies part a lane (own player and
  allies, since 2026-10-19; the funnel give-way, 2026-10-20, asks only its own
  group); an enemy's idle body is a still body to route around. Retail moves no
  idle body at all, its own or another player's: a blocked mover only clamps,
  slows and presses on (docs/retail-engine.md, "retail genuinely wedges"), parked
  bodies are walls in its cell ratings, and BUGGER_OFF, the one script request to
  clear a space, is stored and never acted on ("The build yard was hung").
  Moving enemy bodies would be a gameplay change with no Retail behaviour to
  port, so Legion keeps Retail's rule there (2026-10-20, Legion ~100 round 3).
  `strait-3x80`, named here before, holds one player's fleet only; what
  watches it is `ctest:legion_staticidle` (its part 3: another player's idle
  body plugging the one gap in a wall stays put, and the group holds until a
  second gap opens).
* **Fog**: Legion plans on the static plane as Retail's mover sees it. It does
  not model unexplored terrain separately. The one place fog shows is group
  awareness, which sees an enemy mover only within sight
  (`aware-unseen:pair.contacts.permille_x100` against
  `aware-seen:pair.contacts.permille_x100`).

## Compatibility

* Network protocol: `kNetVersion` 228 introduced Legion. The mode travels as
  the pathfinding-mode byte, value 4 (`PathfindingMode::Legion`); a peer that
  does not know it is rejected by the version check. Since protocol 231 the
  bytes 1-3 (the removed modes) are invalid.
* Lobby: the pathfinding button switches between Retail and Legion. Campaign
  missions and Crusades always run Retail movement, whatever the lobby says.
* Retail hashes and goldens are unchanged by Legion (as were those of the
  since-removed modes). Legion state enters `World::stateHash` only in mode 4.
* Headless runs: the DEBUG client flag `TAK_LEGION=1` selects Legion for the
  `--mpai` harness and other headless games (release builds read no `TAK_*`
  variables).
