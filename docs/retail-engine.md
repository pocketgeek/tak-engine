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
