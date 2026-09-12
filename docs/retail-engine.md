# Retail engine notes (`KINGDOMS.icd`)

Findings from a static analysis of the retail *Total Annihilation: Kingdoms*
executable — `Kingdoms.exe` is a small stub loader; `KINGDOMS.icd` is the real
2.27 MB PE32 (i386, MSVC release build, ImageBase `0x400000`). The build kept its
C++ RTTI and a number of debug-`printf` strings, which together expose the class
model, the config-key schema, and several gameplay formulas. `ironplague.icd` is
the Iron Plague expansion on the same engine.

Analysis was static only — strings, RTTI names, and disassembly of specific code
paths read against the game's own data files. No engine code or assets are
reproduced here; this documents an interface for a clean-room recreation.

## Technology stack (imports)

- **DirectDraw** (`DDRAW.dll`) — a *software* 2.5D renderer; no Direct3D. An
  optional **Glide** path (`glide2x/3x`) targets 3dfx Voodoo. The 3DO models are
  CPU-rasterised to sprites, which is the source of the isometric look.
- **DirectPlay** (`DPLAYX`) + WinSock — networking. The sim is deterministic
  lockstep over discrete *game ticks* (`"…sent more than %lu game ticks ago"`).
- **Miles Sound System** (`mss32`) — audio. **Bink** (`binkw32`) — cutscene video.
- A hot-loaded `Fire.dll` renders flame/particle surfaces (`FireSurfHandler`).

## Object model (from RTTI — 226 game classes)

**Weapons** — `WeaponType` maps one subclass per FBI `type`/`subtype`:
`Melee`, `Ballistic`, `DroppedBallistic` (bombs), `Guided` (homing), `Wandering`,
`RemoteEffect`, `LineOfSight` (hitscan: `Fire`/`Lightning`/`MindControl`/
`TurnToFrozen`/`TurnToStone`), `Earthquake`, `Hailstorm`, `AreaMindControl`,
`AreaTurnToFrozen`, and `ATTRIBUTE_ADJUSTER` (stat buffs). **Status effects and
capture are weapon subtypes**, not unit flags (`turntostone`, `turntofrozen`,
`paralyze`, `mindcontrol` are real data values).

**Navigation** — `PathNavigator` / `VTOLNavigator` with `Local*` vs `Remote*`
variants (the lockstep prediction split); `NavGoalCircle/Rect/Ring` are
area-shaped move goals.

**AI** — role-based squads: `AIBaseSquad` (defence), `AIBackupSquad`,
`AIStrikeSquad`, `AIVTOLSquad` (air).

**Particles/SFX** — `BuildEnergy`, `DamageFlame`, `DeadSpray`, `Flame`, `Smoke`,
`Spray`, `Circle`, `Hail`, `Wake` (ship wakes), plus texture emitters.

**Missions** — one class per victory/defeat condition (`KillEnemyCommander`,
`MoveUnitToRadius`, `CaptureUnitType`, `DeathTimerRunsOut`, …).

## Formulas (disassembled)

**Veterancy** (`getLevel @0x519310`, `getMult @0x5193c0`), constants read from
`.data`/`.rdata`:

```
level = min(kills, 10)                 // one level per kill, cap 10
mult  = 1.0 + 0.10 * level             // base 1.0, +10%/level → up to 2.0×
```

The multiplier scales the instance's **attack** and **armour** up and **divides**
a reload/cooldown field (veterans hit harder, tank more, fire faster); max HP is
unchanged. `noveteran` opts out; `veteranmodel` swaps the mesh at max rank.

**Build/repair rate** (`@0x4d4b30`, the `hp=%i buildtime=%i buildrate=%i` system):

```
rate = round( vetMult * targetMaxHP * workertime * param * (1/3000) ), min 1
```

**Damage** has no single formula — each `WeaponType` subclass applies damage
through its own virtual method; all paths route through the attacker's
veteran-scaled attack and the victim's veteran-scaled armour, on top of the
per-target `DAMAGE` category table.

## Config schema

The TDF parser (`readInt @0x543190`, `readFloat @0x5431c0`, `readString @0x5432c0`)
recognises **336 keys**. The recreation's stat model was validated field-for-field
against the engine's own debug dumps (`Damage/Reload/ManaPerShot`,
`Range/Velocity/AreaOfEffect`, `MaxMana/ManaRecharge`,
`MaxVel(water,road)/ManeuverLeash`, `hp/buildtime/buildrate`). Some `UNIT_DEF`
offsets: `maxdamage +0x1be`, `experiencepoints +0x1c2`, `workertime +0x21a`;
weapon `range +0x90`, `damage +0x126`. Keys the shipped data never uses but the
engine supports: `kamikaze`, `digger`, `teleporter`, `amphibious`, `antiweapons`,
`toairweapon`, `immunetoparalyzer`, `burst`/`burstrate`, `weaponswitching`, …

## Data layout the engine reads

`*.hpi` (SQSH archives, checksummed) · `units/*.fbi` · `MOVEINFO.tdf` (movement
classes → the real `maxslope`/`max·minwaterdepth`) · `gamedata/SIDEDATA.tdf` ·
`gamedata/Gods.tdf` (god timing) · `gamedata/{explosions,effects,damageflames,
soundclasses}/*.tdf` · `CanBuild/<builder>/*.tdf` · `Maps/*.ota` + `*.tnt` ·
`*.gaf`/`*.taf`/`*.3do`/`*.cob`.

## Weapon in-range model (icd, 2026-09-10)

The in-range predicate is a per-weapon-subclass virtual (vtable slots 3/4;
MeleeWeapon vtable `0x5f3af8`, base WeaponType `0x5f3710`):

- **Base `WeaponType::inRange` @0x530580** (point variant @0x530500): `range==0`
  → always true; else strictly **2D centre-to-centre** from the two units'
  position dwords (`unit+0x68` x, `+0x70` z, 16.16 fixed; y at `+0x6c` is
  skipped), 64-bit squared compare `(dx²+dz²)>>32 <= range²` (inclusive). Used
  unchanged by Guided/LineOfSight/Wandering weapons. No footprint subtraction,
  no weapon-piece origin.
- **BallisticWeapon @0x52bc60**: solves the launch arc first (helper `0x52bd10`,
  the only user of dy; `0x8000` = no solution → out of range), then falls
  through to the base centre test.
- **MeleeWeapon @0x52b980** (point @0x52b8a0): **never reads `range`**. Each
  unit's footprint box is position ∓ `foot<<19` (∓ 8px·footCells, cell-snapped);
  in range iff on BOTH axes `|centreΔ| − halfA − halfB < 0x80000` (= 8 px) —
  i.e. the boxes are within half a cell of touching. (`range` in melee TDFs —
  10..250 in shipped data — is dead weight.)
- Weapon TDF parse @0x530780: `range` readInt → `[WeaponType+0x90]`, clamped
  min 2; `minrange` → `+0x94`. Unit-def weapon-type pointers at `+0x1aa`
  (3 dwords). Range-ring UI strings @0x4d6823; debug dump @0x4fbf6b.

## Ship / no-walk mover animation (icd + COB, 2026-09-10)

Ships have no `walk` script. The engine drives them with the **`MoveRate`**
callin on move start/stop (arg > 0 = moving); the COB's `Create` ambients
(`MotionControl`, wake/oar controllers) poll the static that `MoveRate` sets
and select `slowrow`/`row`/`fastrow` from **`GET_UNIT_VALUE 29`** (current
speed, thresholds 25/75 ⇒ percent of max). `TurnDirection(deg)` steers the
rudder/sail trim; `WindChange` orients sails/flags (FBI `wind=1`). Resetting
such a unit's VM kills the Create ambients permanently — nothing restarts them.

## Movement: retail has NO global pathfinder (icd, 2026-09-12)

Asked because our own movement had become the dominant sim cost. It turns out
we diverged from retail badly here, and the divergence is what costs us.

**No pathfinding class exists.** Of the 356 RTTI class names in the binary, the
only movement-related ones are `Navigator`, `PathNavigator`,
`RemotePathNavigator`, `VTOLNavigator`, `LocalVTOLNavigator`,
`RemoteVTOLNavigator`, and the goal shapes `NavGoal`, `NavGoalCircle`,
`NavGoalRect`, `NavGoalRing`, `NavGoalVTOL`, `NavGoalVTOLm`. There is no
`PathFinder`, `Route`, `Waypoint`, `Region`, `Sector` or graph class of any
kind. (`Local*`/`Remote*` is the lockstep prediction split.)

**`PathNavigator` contains no search.** Its vftable is at `0x5f2a24`; every
method is small (11-365 instructions) and nearly loop-free. The largest, slot 8
`0x4e5700`, is a bit-stream serialiser (word/bit cursor, `1 << bit`, wrap at 32,
grow buffer) -- navigator state being saved, not a solver.

**The navigator object**, as read off slots 1 and 2:

| offset  | meaning                                                        |
|---------|----------------------------------------------------------------|
| `+0x04` | the `NavGoal` (area goal: circle / rect / ring)                |
| `+0x08` | unit                                                            |
| `+0x08 + 4*i` | a SHORT array of 16-bit (x,z) points, i indexed by +0x10c |
| `+0x10c`| how many points are live (tested against 2 and 3)              |
| `+0x110`| tick stamp; cleared when older than `globalTick - 6`           |
| `+0x114`| flag bits (bit0 = target valid, bit3, bit4)                    |

**Per-tick update** is slot 2, `0x4e5150`: ask the goal via its vtable whether
it is satisfied (`[goal+0x10]`, `[goal+0x2c]`), then

```
movsx edx, word [esi+0x12]   ; target z        movsx eax, word [unit+0x72] ; unit z
movsx ecx, word [unit+0x6a]  ; unit x          sub / imul / imul / add     ; dx^2 + dz^2
cmp   edx, 0x19              ; <= 25  ->  arrived (radius 5)
```

**Steering** is slot 1, `0x4e54e0`: fetch the goal point through the NavGoal
vtable (`[goal+0x20]`), compute float deltas to it, and set the current target
point, stamping `+0x110` with the tick. The stamp's 6-tick expiry is the whole
re-plan cadence.

**So retail steers each unit toward an AREA GOAL, keeping at most two or three
intermediate points, refreshed about every 6 ticks.** No global search, no
precomputed connectivity, nothing shared between units. That is why it ran
hundreds of units on a 1999 Pentium, and it matches the TA-family feel: fluid
local movement that occasionally wedges.

**What we do instead** (and what it costs): full-map Dijkstra FLOW FIELDS per
(domain, footprint, goal block), plus per-unit A*. Flow fields are OUR
invention -- added 2026-09-03 to fix a crowd/corner jam -- not retail. Measured
server-side on a 120s 8-player game they were 68% of all stalled time, and even
after four rounds of optimisation (e3228e1 and before) they remain ~59%, with
A* another ~23%. The crowd jam they were built to fix is very likely a symptom
of OUR movement model (solid units + separation), which retail did not have in
that form.

NOT established: whether some free function (not a class) does a coarse global
search somewhere. None was found, and the absence of any search-shaped routine
in the navigator region is strong negative evidence, but it is not proof.

## Movement: the local steerer (icd, 2026-09-12, PARTIAL)

Follow-on from the section above. Retail has no path search, so everything that
gets a unit around an obstacle lives in the mover, and this is what that mover
is built out of. Addresses are entry points; the direction-choosing logic that
consumes them is only partly mapped, so treat the last section as a lead rather
than a spec.

**`0x507fb0` -- area rater.** Walks a RECTANGLE of map cells (14-byte records,
stride `0xe`) in a nested row/column loop and takes the MINIMUM score over it:

```
test byte [esi+0xd], 0x80    ; per-cell flag (road)
jne  keep                    ; road keeps the higher score
cmp  dword [edi], 6          ; running minimum
jle  keep
mov  dword [edi], 6          ; ordinary ground clamps it to 6
add  esi, 0xe                ; next cell
```

So ordinary ground scores 6 and road 7 -- the 1.2x road preference our
NavGrid::setRoads comment already cites. The score is GRADED, not boolean.

**`0x4db640` -- passability query.** Calls the rater, returns early if it comes
back `-1` (impassable terrain), then walks the live-unit list checking flag
`0x1000000` at `+0x130`. So one call answers "can a body of this size stand
here, and what does it cost", terrain and occupancy together. This is where
"a parked unit is impassable, a moving one merely expensive" is expressed: the
occupant lowers the score rather than vetoing the cell.

**The threshold is 4.** Every caller compares the result against it
(`cmp eax, 4` / `jle` / `jl`), so the scale is: -1 impassable, below 4 refused,
4..5 passable but costly, 6 ground, 7 road.

**`0x4dba80` -- the mover.** Two uses of the query are clear:

  * Forward probe: take the unit's heading from `+0x7e`, project one step of
    `0x100000` through the sin/cos tables (`0x5360bf` / `0x5360f3`), and query
    the resulting position. `cmp eax,4 / jle` -> blocked.
  * A 3x3 neighbourhood scan: outer loop over x offset -1..+1, inner over z
    offset -1..+1 in `0x100000` steps, querying each and bailing on the first
    below threshold.

**Blocked -> re-read own waypoints, clamp, slow.** When the forward probe fails
the mover sets one of two refusal states (flags `0x100` / `0x200` at navigator
`+0x36`, each accumulating a per-type value from `UnitType+0x249` into `+0x30`
-- the clamp-and-slow already ported in 080d288), runs the 3x3 scan, and then
calls navigator vtable slot 3 with `(outBuf, 3)`:

```
mov  ebx, [ebp+0xc]              ; arg2 = 3, how many points wanted
mov  eax, [ecx+0x10c]            ; the waypoint COUNT
cmp  esi, eax / lea edi,[eax-1]  ; clamp the index to count-1
movsx eax, word [ecx+edi*4+0xc]  ; point[i].x, the same 16-bit array
shl  eax, 0x10                   ; -> 16.16 world units
movsx eax, word [ecx+edi*4+0xe]  ; point[i].z
```

That is an ACCESSOR: it exports the navigator's own two-point segment as world
coordinates. `Navigator`'s base version is `ret 8`, a stub, and all four
NavGoal shapes share an unrelated `mov eax,1; ret`. So the goal object is NOT
consulted here and is not an active participant -- an earlier revision of this
document said it was, on a misread of which object the vtable belonged to.

CONCLUSION SO FAR, and it is uncomfortable: no distinct obstacle-avoidance
algorithm has been found. What retail has is the graded passability score, the
forward probe, the 3x3 scan, and clamp-and-slow -- all of which we now have.
Either the avoidance is subtler than these pieces suggest (the 3x3 scan's
result is used somewhere not yet traced), or retail genuinely wedged as much
as we do and the difference we think we remember is not there. Resolving that
needs the rest of 0x4dba80's control flow read properly, not more guessing.

## RETRACTED: the "wall-follower" is not in the movement path (2026-09-12)

An earlier version of this section claimed that `0x4140d4`..`0x414eb0` is
retail's answer to a blocked unit -- a wall-following obstacle tracer -- and
that implementing it would fix units stopping dead at a parked body. **That
claim is withdrawn.** The code exists and does what was described, but it is
not reachable from unit movement, so it is no evidence about how retail moves
anything.

What the routine genuinely is: a contour tracer. `0x5f304c` / `0x5f3054` really
are the 8-direction offset tables
(`dx = {0,-1,-1,-1,0,1,1,1}`, `dz = {-1,-1,0,1,1,1,0,-1}`, compass order),
the direction at `+0x108` really is rotated and masked `& 7`, candidates really
are scored against the threshold 4, and it really does give up on returning to
its start cell facing its start direction, within a time-sliced step budget
(`+0x48` against the cap at `+0x165`, phase at `+0x60`). All of that held up.

What was never checked: whether anything in the movement path calls it. It does
not. An upward reachability sweep of the whole call graph gives `0x4146e0` just
NINE ancestor functions in the entire binary --
`0x415b10, 0x416430, 0x4261f0, 0x4f6c70, 0x526310, 0x526740, 0x527060,
0x527360` -- and the mover (`0x4dba80`), the navigator update (`0x4dc800`) and
the passability query (`0x4db640`) are none of them. `0x4261f0` is a handler in
the CHEAT dispatch table at `0x605c2c`, whose neighbouring entries are the
strings "HalfShot", "NowISee", "MakePoster" and "ManaMe". This is map/poster
rendering or a similar offline analysis, not steering.

The methodological error is worth naming, because it is the second time in one
day: the routine was found by searching for callers of a function believed to be
movement-related, and its shape (direction tables, passability threshold, give-up
test) was so convincingly "obstacle avoidance" that the shape was accepted as
proof of purpose. Shape is not provenance. Establish the CALL PATH from the
subsystem you care about before concluding a routine belongs to it.

Still true: `0x4139d0` is a per-cell query rather than a search, so the old
`sim.h` note calling it "its path search" remains wrong.

### What a second, top-down pass established (same day)

Working DOWN from the navigator instead of up from a suggestive routine:

  * **The navigator holds up to 64 waypoints, not two.** `0x4e4ea0` is its
    point-list setter: it clamps the incoming count at `0x40` and block-copies
    that many dwords into `+0xc`, storing the count at `+0x10c`. So the
    "two-point segment `[position, goal]`" description -- which I restated as
    recently as the retraction above -- is wrong as a statement about the
    navigator. `movl $0x2,0x10c` at `0x4e5635` is just ONE caller installing
    two points. The navigator is a general multi-waypoint follower, and the
    mover's `pop_front(1)`-then-recurse (`0x4dbf36` / `0x4dbf7e`, vtable slot
    11 = `0x4e50a0`) is it advancing along that list.
  * **The mover's refusal flags are write-only.** Nothing in the binary reads
    bit `0x20`, `0x100` or `0x200` of navigator `+0x36`. The mover clears
    `0x07E0` on entry and sets them on refusal, and no consumer exists. They
    are status, not control -- so the response to being blocked is entirely
    inside the mover: clamp, slow (the `+0x30` budget against the global at
    `[0x62d55c+0x19f44]`), return.
  * **A real route producer exists** and feeds the navigator: `0x414450`,
    `0x415040`, `0x415b10` and `0x416430` all call the setter above, and that
    cluster contains the contour tracer retracted earlier. So the tracer IS
    part of route production; it simply is not reached FROM the mover, which
    is why the reachability test that produced the retraction was the wrong
    test.

### RESOLVED: retail HAS a pathfinder, reached through a singleton pointer

The link is not a call at all, which is why a direct-call graph could never
find it. It is a GLOBAL POINTER.

  * `0x4e6060` does `new(0x22b)` and runs `0x415f80` on the result -- a
    constructor, zeroing `+0x0..+0x28` -- then stores the object into the
    global slot `[0x62d55c + 0x19e70]`. That is a SINGLETON of the
    `0x414450 / 0x415040 / 0x415b10 / 0x416430` class, the same class that
    owns the contour tracer at `0x4146e0`.
  * The navigator's `setDestination` (vtable slot 1, `0x4e54e0`) loads that
    same singleton at `0x4e5502` and calls `0x415f30` on it -- cancel/re-register
    the pending request for this navigator. `0x416430` calls `0x415f30` too,
    which is the completion side.
  * The singleton installs results back into a navigator through `0x4e4ea0`,
    the point-list setter that accepts up to 64 waypoints.
  * When no route is available, `setDestination` falls back at `0x4e5632` to
    `count = 2` -- the unit's own cell and the goal -- and that fallback is
    SKIPPED when bit 0 of `+0x114` says a real route is already in hand.

So the shape is: an asynchronous path request against a singleton pathfinder,
a multi-waypoint route installed when it completes, and a straight two-point
segment as the interim fallback.

**This overturns "retail has no path search of any kind."** That claim was
made earlier today, written into this file, and used to justify setting
`pathBudget_ = 0` in our sim -- i.e. no route search at runtime, units steer
straight at the goal. It is wrong. The earlier `sim.h` note, which said retail
bakes per-class grids and "its path search reads that", was closer to the truth
than the claim that replaced it.

The two-point segment reading was not wrong, but it was the FALLBACK, not the
mechanism -- observed at the one call site that installs it and generalised.

Not yet established: what the search actually optimises, how requests are
scheduled and throttled, and how the tracer participates. Those need their own
pass. What IS established is that runtime routing exists, so our
`pathBudget_ = 0` is a deviation from retail rather than fidelity to it.

### The pathfinder, end to end (2026-09-12)

Complete architecture, established by following the singleton rather than the
call graph. Addresses are entry points unless noted.

**The object.** `[0x62d55c + 0x19e70]` holds a singleton, `new(0x22b)` +
constructor `0x415f80`, created at `0x4e6060` and cleared at `0x4e60b6`.

**Configuration** (`0x4252e0`): reads a setting, then
`+0x221` = base work budget, `+0x225` = `base * clamp(pct, 5, 1000) / 100`,
with two booleans at `+0x229` / `+0x22a` for the -2 / -1 / 0 special values.
So pathfinding effort is a user-facing quality knob.

**Per-frame scheduler** (`0x416430`): walks the request lists of all 10
players, counting pending requests into two buckets -- `B` for those flagged at
`+0x24e7`, `A` for the rest. If none, it bumps an idle counter at `+0x1a5` and
returns. Otherwise the per-request quantum is

    quantum = +0x225 / (A + 5*B)

so a flagged request gets FIVE TIMES the share. It zeroes `+0x165`, which is
the work cap the search meters itself against, then iterates the players.

**Per-request step** (`0x415b10`): runs one search step and switches on it --
`-2` = failed (sets `+0x5c`), `-1` = not finished (charges 30 to the work
counter `+0x48`, expands, retries via `0x414450` / `0x415f10`), `0` = done,
build the route and install it.

**The search itself** (`0x4146e0`) is NOT A*. There is no open list, no
priority queue and no cost-to-goal ordering anywhere in it. It is an
incremental obstacle-boundary tracer:

  * state machine at `+0x60` (0 = init, then 1..3);
  * `0x413e50` returns distance to the goal, and `+0xcc` keeps the BEST
    (minimum) distance reached so far -- the characteristic bookkeeping of a
    "bug" algorithm that follows an obstacle outline and leaves it when it can
    improve on its closest approach;
  * movement uses the 8-direction tables `0x5f304c` / `0x5f3054`
    (`dx = {0,-1,-1,-1,0,1,1,1}`, `dz = {-1,-1,0,1,1,1,0,-1}`, compass order)
    with the direction at `+0x108` rotated and masked `& 7`;
  * every candidate cell is scored by `0x4139d0` against the threshold 4;
  * it terminates on returning to its start cell facing its start direction;
  * it is time-sliced: work accumulates into `+0x48` and the step returns once
    it passes the cap the scheduler wrote to `+0x165`, resuming next frame.

**Hand-off to the navigator.** On success the route is installed through
`0x4e4ea0`, which clamps at 64 waypoints. The navigator's `setDestination`
(vtable slot 1, `0x4e54e0`) cancels/registers with the singleton via
`0x415f30`, and installs a straight TWO-POINT segment -- own cell plus goal --
at `0x4e5632` as the interim, skipping it when bit 0 of `+0x114` says a real
route is already in hand. The mover walks the list, popping via slot 11
(`0x4e50a0`).

So: asynchronous, budgeted, incremental boundary-tracing search; straight-line
movement until it returns; up to 64 waypoints when it does.

**Two of my own claims this file carried are now settled.** "Retail has no path
search of any kind" was wrong -- routing exists. But the observation behind it
was right: there is no A*, no open list, no priority queue. Both halves matter,
and conflating "no A*" with "no pathfinding" is what produced the error. And
the retraction that said the tracer at `0x4146e0` is not in the movement path
was itself wrong: it is the search. It is not reached FROM the mover, which is
what I tested; it is reached from the scheduler, and its output is handed to
the navigator that the mover then follows.

### Earlier unresolved note, kept for the record

By direct-call graph, that whole route cluster is reachable only from
`0x4261f0` -- which references the strings `"%s\screenshots"` and `"BIGSHOT"`
and is the poster/screenshot handler -- and from `0x527360`, which has no
callers and no pointers to it anywhere in the image. Function sizes along the
chain are all 74..795 instructions, so this is not an artifact of merged
boundaries, and the `0x4261f0 -> 0x526740` edge was confirmed by hand.

A route generator wired only to the screenshot path is not a credible reading.
The likely gap is that the graph follows direct `call 0xADDR` only and misses
an indirect/vtable invocation from the order system. Until that path is found,
HOW GAMEPLAY INVOKES THE ROUTE PRODUCER IS UNKNOWN, and with it the answer to
what retail does when a body is parked in a unit's way.

Three conclusions about this area have now been published and two retracted in
a single day. The next claim here should come with a demonstrated call path
from the order or unit tick, not from a routine's shape or from a partial graph.

## Retail-faithful body collision: sub-cell solidity (2026-09-12)

First attempt reverted, then landed once the real obstacle was named. Kept in
full because the obstacle is the interesting part.

Retail's configuration is three things that only work together:
  1. PARKED bodies impassable, MOVING bodies merely expensive (graded score).
  2. The step test scores the body's whole footprint RECT, not one cell.
  3. No separation pass at all -- bodies share space briefly and nothing shoves.

Ours currently has hard-blocking movers and a full-footprint separation push.
Implementing (1) and (2) is straightforward and passes every test: an
`areaScore()` returning -1 impassable / 4 passable / 6 ground / 7 road,
thresholded at 4, with occupancy stamping only parked bodies.

(3) is where it stops. Demoting separation to de-overlap lets a walker
interpenetrate a parked body -- measured closest approach 13px where the
footprints should touch at 32px. The cause is in our mover: it skips the step
test entirely while a unit stays INSIDE ITS OWN CELL, so sub-cell motion is
unchecked and a unit can creep into a neighbour between cell transitions. The
full-radius separation push was silently providing that containment.

So the blocker for retail-faithful collision is SUB-CELL collision handling,
not the score. Retail does not need separation because its refusal happens on
the rect at whatever resolution the mover steps at; ours only happens on cell
entry. Until the mover tests every step rather than every cell crossing,
removing separation trades visible shoving for units sinking into each other.

### What landed

Sub-cell step testing first, as that order implied.

`World::bodyPenetration(u, nx, nz)` asks the same question in PIXEL space that
`cellFree()` answered in 16px cells: how deep would this body sit inside another
if it stood at (nx,nz)? It reads candidates out of `occ_` -- any body we could
overlap has a stamped cell within our own half-extent, so the scan is a small
fixed rect, and a footprint stamps a run of identical ids that dedupes away.
The mover's `free()` is now `bodyPenetration(...) <= 0`, and the "unit stays
inside its own cell, skip the test" hatch is gone.

Two rules make that usable rather than a freeze:

  * Only PARKED bodies are consulted. A moving one is merely expensive, which
    is retail's graded score. Gate on movers too and any crowd wider than its
    lane deadlocks -- measured 16 of 24 arriving where 24 of 24 should.
  * A body you are ALREADY inside is skipped. Units spawn in tight ranks, a
    building finishes under its builder; phrase the rule on the deepest overlap
    and nobody can satisfy it, so nobody moves, and each frozen (speed 0) body
    then blocks its neighbours in turn. De-overlapping stays separation's job.
    The mover's job is the narrow one: never ENTER a body you are clear of.

`rebuildOccupancy` also stamps in two passes, movers first, so a PARKED body
wins a contested cell. A cell holds one id; let a mover overwrite a parked
stamp and the parked body goes invisible to the test that exists to respect it.

Measured: a walker sent straight through a parked body closes to exactly 32px,
its footprint touching and never overlapping. With the cell hatch restored it
reaches 6px in. That ablation is the evidence the hatch was the whole defect.

### The occupancy predicate, and why retail needs no separation pass

Read off `0x4db640` -- the unit half of the passability query -- at
`0x4db767..0x4db7c9`. For each unit found in the target rect it decides whether
that unit blocks. An occupant is IGNORED, letting the step pass straight
through, only when ALL THREE hold:

  1. it is genuinely under way -- retail dereferences a movement object at its
     `+8` and bails to "blocked" (`0x4db8d4`) when there is none;
  2. it is not slower than us -- `[[+8]+0x20]` compared against a computed floor
     AND against our own `[[+8]+0x20]`;
  3. its heading (`+0x7e`) is within `0x4000` of ours, i.e. 90 degrees.

Anything else falls through to `0x4db893`, which returns 2. The threshold at
every call site is 4, so that is a refusal. The return set is: 0 and 2 blocked,
4 blocked, and the terrain score (6 ground / 7 road) when no occupant objected.

In English: **you may close up behind someone going your way who is not slower
than you.** Head-on traffic blocks. Slower traffic ahead of you blocks. Parked
blocks.

That single rule is why retail ships no separation pass and no "don't come to
rest inside another body" check. Overlap barely forms, and the one case that
does create it -- tucking in behind a faster leader -- unwinds itself as the
leader pulls away. Ours now implements the predicate and the separation pass is
DELETED. Two systems enforcing spacing at different resolutions, which is what
we had, is what read in play as units shoving each other around.

Known and accepted: a follower can still end up overlapping its leader if that
leader stops, and nothing now pushes them apart. Retail behaves the same way.
Do not reintroduce a push to "fix" it.

## Headless in-game screenshots (dev harness)

`--shot` alone captures the LOBBY and exits: it forces the dummy video driver and
the menu path shoots before the game starts. To capture a real in-game frame use
the auto-lobby driver plus a delayed capture, all with the DEBUG binary:

```sh
TAK_MPAUTO=4 TAK_SHOT_MS=35000 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build-dbg/takclient game "Inner Circle" --data <install> --shot out.png
```

- `TAK_MPAUTO=4` drives the lobby the way `--mpai` does, but through the
  INTERACTIVE (rendering) loop instead of the headless one. Without it the client
  sits at NOT READY forever and the shot is a lobby frame.
- `TAK_SHOT_MS=<ms>` delays the capture by wall-clock ms (default is 3 frames).
- Add `TAK_STRESS=1` to spawn each AI at ~95% of the unit cap for instant mass
  combat -- the quickest way to see projectile art, beams and storms on screen.
- No real window is needed; the dummy driver renders correctly.
