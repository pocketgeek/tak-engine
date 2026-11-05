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

Current executable movement checks, including the 24:10 direction threshold,
trace-to-cost handoff and 64-corner reconstruction, are recorded in
[pathfinding-port.md](pathfinding-port.md). Earlier static interpretations
below do not supersede those comparisons. Whole-game parity remains unproven. Protocol 103 integrates the verified flight
velocity/navigator and unarmed positional patrol into World; the initial-state
replay matches all 61 captured frames of unit 821, including altitude. See the
linked movement notes for remaining flight and full-scene limitations.

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
`RemoteEffect`, `LineOfSight` (a straight shot that flies: plain, `Fire`,
`Lightning`, `MindControl`, `TurnToFrozen`, `TurnToStone`; none is hitscan,
see "Mind control, stone and freeze fly"), `Earthquake`, `Hailstorm`, `AreaMindControl`,
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
on a kill (512ae0 -> 512ce0):  killer.xp += victimType.experiencepoints   // 32-bit wrap
level = noveteran ? 0 : vF != 0 ? 10 : min(xp / ownType.experiencepoints, 10)   // idiv
mult  = noveteran ? 1.0 : vF != 0 ? vF : 1.0 + 0.10 * level   // up to 2.0x
authored level L (setLevel 519370): xp = trunc((L + 0.5) * ownType.experiencepoints)
```

Experience (+0xf0) is a sum of the victims' `experiencepoints` (default 666),
not a kill count, and the level is worked out from it on every read: a cheap
unit that kills a valuable one can jump several levels, a valuable one needs
several cheap kills for one. The credit goes to the victim's last hitter
(+0xf8, set by 51a240 only while the hitter lives) for death kinds 1, 2, 3, 7,
8, 14 and 15 (switch 512b71; not self-destruct 5, paralyzer 4, a scripted 0 or
a site's 13), when the victim was finished (+0x108 == 0) and its owner (+0xfd)
differs from the hitter's player (+0xfc) -- no alliance test. Nothing about
the killer is tested: buildings gain experience, and noveteran units collect it
but stay at level 0. A change of owner (ShareUnits, CRT transfer, charm, all
via 514da0, and the mission `Capture` verb at 0x4d3c57 as well) makes a new
object with +0xf0 = +0xf4 = 0, then the type's scenario level. Two deaths no
attack causes credit nobody: deadly water hurts through 51a140 with no
attacker and damage kind 13 (51e13f), which keeps the last hitter but becomes
the death kind (+0x10f, 512610 at 51e36c); the mission `Kill` verb (0x4d36eb ->
513130) sends kind 10. (Retail's `Kill` also ignores its arguments and takes
every unit this machine runs, owner +0xea 1 or 2; ours still kills the one unit
named, and no shipped mission uses it.) Saves store the level, not the points
(51f04c). Ours: `src/sim/retailveterancy.h`; `tools/re/check_veterancy.py` runs
512ce0's gate, 519310/5193c0 and 519370 natively against `retail_script_test
--veterancy`, and the water, Kill and Capture paths natively.

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
  unchanged by 14 of the 17 handler classes (vtable +0xc, named by their RTTI):
  `WeaponType`, `LineOfSightWeapon` and its Lightning, Fire, MindControl,
  TurnToStone and TurnToFrozen subclasses, `GuidedWeapon`,
  `RemoteEffectWeapon`, `HailstormWeapon`, `EarthquakeWeapon`, both area
  spells and `WanderingWeapon`; `DroppedBallisticWeapon`'s `532290` is a bare
  call of it. No footprint subtraction, no weapon-piece origin.
  `tools/re/check_inrange_classes.py` runs all 17 natively against a 7x12
  building at 299-420 px for range 300 (and on the diagonal): every class but
  melee is in range exactly at centre distance <= 300, none in our old
  120 px footprint allowance.
- **BallisticWeapon @0x52bc60**: solves the launch arc first (helper `0x52bd10`,
  the only user of dy; `0x8000` = no solution → out of range), then falls
  through to the base centre test. The arc runs origin to origin (target minus
  shooter `+0x68/+0x6c/+0x70`, so a submerged or flying body's own y), each
  delta and the speed `+0xd0 * +0xcc` (per-substep velocity times substeps,
  our `retailAimSpeed`) scaled to float by `1/65536`, under the map's gravity
  (`game+0x19ecc`). The base test it falls into is `530580`: weapon flags
  `+0xc8` bits `0x4000`/`0x8000` against the target's air state, `range==0`
  in range, else the per-axis high dwords of `dx²` and `dz²` summed against
  `range²`. Ported 2026-10-25 (0.9.2) as `tak::retailBallisticInRange` /
  `World::ballisticInRange`; `tools/re/check_ballistic_inrange.py` emulates
  `52bc60` on 6000 cases (half of them the Marine Mortar around 750/800 px)
  and matches every answer.

**Engine use (0.9.2).** For a Ballistic weapon (not Dropped, flame or beam:
`Weapon::ballisticClass`) the sweep and auto slots (`slotInRange`), the attack
order's fire gate and its approach all use that predicate, with no footprint
pad. Since 0.9.12 every other ranged weapon has none either
(`Weapon::centreReach`; our structure pad, `8*max(foot)+24` px so short-range
attackers did not grind a building's edge, is kept only for a melee approach,
where it stands in for the box contact below). 0.9.12 first dropped it for a
shot that dies at its range (a Line-of-Sight bolt or lightning and a Guided
shot get one range of travel at launch, a flame front is scanned out to the
range): with the pad a Crusades Death Totem (lightning, range 550) took a
Taros lodestone 584.5 px off (emulated `530580`: out at 550, first in at 585)
and its bolts died ~540 px out without a hit (the user's 0.9.12 replay at
21:00), and a mobile Crossbowman, Shaman or Fire Demon ordered onto a building
parked up to the pad past its range and never hit it. A Dropped bomb, a Remote
Effect, Hailstorm, Earthquake or area spell, a Wandering storm and the
mind-control, stone and freeze Line-of-Sight shots kept the pad until the
per-class check above showed retail measures them centre to centre too: they
were cast at a building from up to the pad past their range (the attack sweep:
its band cases fired and hit), where retail never casts. 38 shipped weapons
(37 in Crusades) moved, all on mobile units; `shot_range_test` lists them by
class. There is no pad in `slotInRange`, `combatReach` (approach, fire gate,
findTarget's hold-post filter), the per-slot fire gate, `repositionForShot`'s
band, the structure/flyer hold or Legion's mirrored reach ring. Retail's
ground attack mission (`Attack_Chase`, `403bb0`; melee switches to
`Attack_Melee`, `404690`) heads for the same reach: a ring of the weapon's
range (`52aba0`: WeaponType `+0x90`; inner radius `+0x94`, `52abd0`) round the
target's origin (`4d4da0` / `4d4e10`), stops when `52ad60` (vtable +0xc)
passes, and after a failed approach retries with narrower rings and then a
random point within range (its counter at mission `+0x52`, cases 0-5 of the
table at `404608`; it cycles between 4 and 5, and no exit for a target that
stays out of range was found, static reading). So a unit whose range is
shorter than it can stand from a building's centre never fires at it in
retail: a Lich (Death Aura, 60) or an Injured Thirsha (Wind Wave, 60) against
most buildings. Ours (no pad) keep moving at the building with the order kept
and never fire either (`shot_range_test`: a Lich against a 7x12 Barracks
stood 88 px off, 0 damage). Before, the pad let a Creon Submersible 822-848 px from an 800-range Divine Lodestone count as in
range; the arc had no solution, `queryWeaponAim` turned `0x8000` into pitch 0
(what `52bdf0` stores) and the sub lobbed every shell flat into the water at
its bow (44% of all sub shots in the user's 0.9.1 game). Now an ordered
attacker closes in until the arc reaches; a boat that cannot get nearer (a
land target past the shore) holds without firing, as retail. The aim still
reports pitch 0 for no arc, but a slot whose muzzle-to-SweetSpot solve has
no arc (possible just inside the origin solve's reach) does not start
FireWeapon (`tickScriptWeapon`) and a scriptless shot is not launched
(`fire`); a unit free to move keeps closing there. A unit that may not leave
its post (a building, or Hold Position on an auto target; an AI squad attack
mission, which may chase, is exempt) drops a target out of its reach, so
`findTarget` does not acquire one for it (it would be acquired and dropped on
every acquisition turn, and each acquisition cancels the unit's path and takes
its slots off auto, clearing the fire-at-will sweep's targets). Both sides use
one test, `World::combatReach`: the selected slot (`approachSlot`; the longest
weapon when all fire), its range plus the structure pad, or the origin arc for
a Ballistic weapon. This holds for every ranged weapon since `6c3e8359` (only
Ballistic ones before; non-ballistic towers and switching Monarchs on Hold
churned thousands of times a game); melee keeps the old acquisition reach. The muzzle solve turns with the
aiming pose, so it is judged only once the weapon is aimed and loaded: each
reload spent that way counts as a blocked shot (`World::noteNoArc`, see
"Blocked shots"), so after three the target is written off from that spot;
the sweep then picks another, and a unit that may not move drops it (before:
it held it for ever without a shot). Retail lobs that shell flat instead. The
reposition search
(`repositionForShot`) keeps only spots the arc reaches. `naval_combat_test`
covers 740/785/795 px (hits), 830-848 px in open water (closes in, hits, no
splash within 300 px of the sub) and behind a shore (no shot, no churn).
- **MeleeWeapon @0x52b980** (point @0x52b8a0): **never reads `range`**. Each
  unit's footprint box is position ∓ `foot<<19` (∓ 8px·footCells, cell-snapped);
  in range iff on BOTH axes `|centreΔ| − halfA − halfB < 0x80000` (= 8 px) —
  i.e. the boxes are within half a cell of touching. (`range` in melee TDFs —
  10..250 in shipped data — is dead weight.) The boxes are the footprints'
  cells: each position is snapped to its footprint origin
  (`(pos - foot<<19 + 0x80000) >> 20`, our `footprintOrigin`) before the
  compare, so overlap and contact anywhere in the origin cell pass. Ported
  exactly (`meleeInRange`) since protocol 307; before, raw centres let a body
  standing on the near edge of a perimeter cell test out of range. Before the
  boxes it reads the target's flight layer (`+0x130 & 3`, `52b98c..52b997`):
  a target in the air (2) is never in range. See "Melee never reaches a flyer
  in the air".
- **Attack_Melee @0x404690** (registry row `0x5eb8ea`, flags `0x1000280`):
  the ground attack mission of a melee selected weapon (any other weapon hands
  the order to Attack_Chase, `4d4bf0`/`4d4ef0`). Stage 0 (`404cfa`): the slot
  goes manual with the target (`519b70`, `51a700`); in range it stops the
  navigator (`4d4d40(0)`); otherwise a target WITH a mover (unit +8) gets a
  radius-0 circle on its position (`4d4da0`), and a target with NONE (a
  building: a mover exists only for `bmcode=1` with a turn rate and a
  velocity, so a factory with `canmove=1` and no velocity has none) gets
  `4d4e80` with its footprint-origin cell (+0x74) and footprint (+0x78): the
  rectangle controller RECLAIM, HelpBuild and RepairUnit use (`4e3590`:
  minX = ax - bw .. maxX = ax + fw, the same in z), whose arrival is any
  origin where the attacker's footprint touches the building's, edge or
  corner. Stage 1 (`404b61`) re-installs a circle only when a mobile
  target's cell changed, sets the wait mask `0x40068` (`|0x100000` in range)
  and sleeps `rand(5)+rand(5)+4` ticks when the target is more than
  `3*maxFoot/2+1` cells off (`535e90`), else a short cadence sleep; stage 2
  (`40496a`) goes back to stage 1 (a mobile target in range may stop the
  navigator first). No give-up: a building's attacker loops until it dies.
  The mission never fires: the weapon update `52ae90` does, every tick in
  range (`52fe30` AimWeapon and the facing request toward the target's
  origin, `52fff0` readiness, `530140` FireWeapon). Emulated end to end
  (scratch probe: `52ae90`, the shipped `zonwolf.cob` in retail's VM,
  `4d8450` -> `404690`, `4dc800` per tick, a Spirit Wolf against a building
  with no mover): head-on at a 2x2 building, `4d4e80(cell 37,79, foot 2x2)`
  at the order, contact t=75, AimWeapon t=76, FireWeapon t=77, the bite (SET
  23, attack1's wind-up) t=99; at the side of a 7x14 one, stopped t=55 on
  its west face, pivoted 38 degrees toward its centre, FireWeapon t=61,
  bite t=83. A second attacker whose nearest perimeter origin a first one
  holds: retail's whole route-search attempt (`415170`, tracer, cost slice,
  `414450`, delivery `4e4ea0`) on that rectangle, the first attacker's
  origin blocked, ends complete on the free perimeter corner beside it
  (a radius-0 circle on the centre ends partial). Ours through protocol 306
  chased the building's centre: Retail mode by best-effort routes to the
  nearest reachable origin, Legion by a point seed every lone or late
  attacker shared (a second Spirit Wolf held behind the first for good) and,
  for groups, a radial ring of `8*(maxFootA+maxFootB)+8` px whose short-side
  spots on a long building (every factory) stood out of contact. Since 307
  `World::installMeleeApproach` installs the rectangle on the attack leg;
  Legion does not route a leg with a rectangle it does not own, so the
  native navigator takes it in both modes. `melee_building_test` runs the
  `melee-*.scn` fixtures in both modes with the target topped up and counts
  each attacker's hits. On protocol 306 Legion's second wolf on a lodestone
  (`melee-lode-late`) never bit and four wolves on a Keep (`melee-castle-4`)
  never touched it; twenty Swordsmen round a Keep (`melee-castle-20`): 11 of
  20 bit in both modes, 87 hits in Retail and 106 in Legion. Now every wolf
  bites in every fixture, in both modes, and 16 of the 20 Swordsmen bite
  (about 150 hits); Retail and Legion take the same routes. Still deviating:
  Legion's melee ring round a MOBILE target has no corner places (at most
  four attackers round a 2x2 unit). (An idle unit's target scan skipped
  construction sites through 307's first landing; since 307 it takes them
  as retail does, see "Enemies with construction left" below.)
- Weapon TDF parse @0x530780: `range` readInt → `[WeaponType+0x90]`, clamped
  min 2; `minrange` → `+0x94`. Unit-def weapon-type pointers at `+0x1aa`
  (3 dwords). Range-ring UI strings @0x4d6823; debug dump @0x4fbf6b.

## Fire-at-will sweep and weapon slots (icd, ported 2026-10-11, protocol 252)

Retail does not acquire targets per unit per tick. Each player runs a
round-robin **sweep** (`40f330`) over its units, N/30+1 per call (cursor wraps
and skips its start slot), right after the AI schedule. Each of a unit's three
weapon slots is **auto** or **manual**:

- **Manual** while an attack order is at the front, after a build placement
  succeeds, during help-build / repair / reclaim / resurrect work, and while
  incapacitated, carried or under construction. Entering manual drops every
  slot target (TargetCleared once per slot that had one). Our orders are not
  retail missions, so `syncSlotMode` returns the slots to auto as soon as the
  reason ends (retail: the mission destructor's setAuto `4d6da0`).
- **Auto** slots are filled by `retailSelectTarget` (port of `412620`): 50
  `gameRand` draws over the player's visible list (melee 5), integer distance
  squared, `fireatwillrandom`, turn penalty, x10 inside the minimum range and
  x10 for a target with construction left, bad-target fallback; a drawn
  candidate leaves the list with the last one taking its place (`50b2c0`).
  See "Enemies with construction left" below. Auto slots fire en route without braking or steering; only a
  stopped, order-less ground unit pivots (at `turninplacerate`). Only
  hover-attack flyers and ground units with `turninplacerate > 0` must face
  the target to fire.
- **Vision** for the sweep is a deterministic per-player view-count plane
  rebuilt on a 7-tick stamp (hashed; the local display fog reads the same
  plane, see "Fog of war display").
- **Hit response** (`409a50`): (a) an idle unit runs a leashed retaliation
  attack and walks back; (b) otherwise the attacker goes to every auto slot that
  can reach it unless the current target is still good. AI owners also switch
  weapons (`51a8b0`) and sweep `commandfire` weapons; human owners do not. A
  unit with construction left (`+0x108 != 0`, tested at `409b56..409b67`,
  after the AI-Monarch threat roll, the attacker check and the armed check)
  skips both parts; before 0.9.12 an unfinished site under fire took its
  attacker as an attack order it could not carry out until finished.
  (Re-read 2026-11-03: `409b56` loads `+0x108`, compares it with the rdata
  float at `0x5eb524`, 0.0, and `409b67` jumps to the exit `409f6f` unless it
  is equal.) This was the 0.9.3 notes' unexplained hold: on duel seed
  `0x6666` (Creon against Aramon, Edmont's Field) a Prismatic Mirror still
  being conjured was shelled by an Aramon Trebuchet 1469 px away, took it as
  an auto target from the hit response and held it 176 s without a shot,
  until it was finished; with the 0.9.12 gate applied to 0.9.3 the site takes
  no target, and 16 seeds on 0.9.19 show no hold of 60 s or more by any unit.
  Pinned by `structure_combat_site` (the real pair at that distance) and
  `fire_sweep_retaliation` case 5.

Deviations: auto slots of ranged ground weapons whose shot flies the line
(not a plain Ballistic shell, `Weapon::needsClearLine`) keep our 2D clear-shot
test (retail has none); parts of the stone/freeze/paralyze
immunity checks are not modelled. `shootme` is: a human-owned shooter never
auto-picks a type without it (protocol 253).

**Turn penalty's turn rate (emulated 2026-10-22, protocol 289).** The score's
turn penalty (`412a77..412ae2`) does not use the raw `turnrate` (UnitDef
`+0x18e`, a word). With mover `+0x36` bit 11 (on road) set it uses
`turnrate x roadmultiplier` (`+0x172`), else with bit 12 (in water)
`turnrate x watermultiplier` (`+0x16e`); both are 16.16, scaled by the rdata
1/65536 at `0x5eba78`, cut by `_ftol` (chop) and masked to 16 bits before the
`< 1000` test -- so a negative multiplier wraps to a large rate and drops the
penalty. Emulating the snippet on synthetic units gives 300 -> 359 on a 1.2
road, 150 in 0.5 water, road over water when both bits are set, 700 x -0.5 ->
65186. It is the same scaling the cost search applies (`4151ef`); both now
share `retailEffectiveTurnRate` (pinned by `fire_sweep_turnrate`).

## Enemies with construction left (icd, emulated 2026-10-30, protocol 307)

A unit with construction left (`+0x108`, a float: 1.0 for a fresh site, 0 once
finished) is a site still being conjured, or a unit still being built at a
factory door or beside a Zhon builder. Retail's target scans take one, behind
finished targets:

- **The list.** `411700` puts on the visible-enemy list +5 every live, not
  dying unit of a non-allied owner that the player sees (`4f7210`), dropping
  only the scenario flag `0x2000` (never set at creation, U16 in
  [the classic AI plan](classic-ai-plan.md)). There is no `+0x108` test for
  an enemy; allied and own sites go on +0x15.
- **The picker** (`412620`) multiplies a target's score by 10 unless it is
  alive, not dying and has no construction left (`412b52..412b77`; alive and
  not dying were tested at the draw, so this is construction left alone), so
  a finished target comes first and a site is taken when nothing finished is
  near enough. It looks at no paralysis there. Stone, freeze and charm
  classes skip a site (`4127eb..4128db`). A drawn candidate leaves the list
  with the last one moved into its place (`50b2c0`), which decides which
  candidate the later draws land on.
- **Which scans.** Every scan over the list. The fire-at-will sweep (`40f330`
  -> `40f2e0` -> `412620` with the slot's range) and the mission scan
  `4d8370`: Fire At Will and armed only, `412620` on the scanning slot
  (`519ac0`: a switcher's selected slot, else the first weapon the unit has)
  gathering within max(sightdistance, range); a ranged slot then keeps only
  what is within its range (see "Mission target scan"). `4d8370` is called by Standby
  (`407770`), Standby_Wander, Patrol (`403600`), VTOL_Patrol, Move_Ground's
  attack response (`402e2d`, a move with a response mode),
  Move_Ground_Formation, Attack_Chase and Attack_Melee's retargets,
  VTOL_Standby, VTOL_Move(_Formation), VTOL_SeekAttack, AirStrike,
  AirToGround, AirToAir and AirToGroundHover. Follow_Ground and VTOL_Follow
  (guard) run no scan: they answer the guarded unit's attacker (`+0xf8`),
  which a site never is, and the guard's auto slots take a site in range
  through the sweep. The hit response `409a50` answers an attacker, never a
  site (and a site under fire answers nothing, above). The scripted wait
  `401480` and the AI weapon switch's counts (`411ae0`) count sites too.
- **The sweep's budget** (`40f3a1..40f3c9`): the player's own dead, dying
  and unfinished units cost nothing; every other unit costs one, a paralyzed
  one included (its slots are manual).
- **The AI weapon switch** (`40f50e..40f5ea`, `40f7b8..40f8ab`): a stone,
  freeze or charm slot is no use against a target with construction left.
- The Retail AI's own picker (`40a200`) divides an unfinished target's score
  by rand(3)+1, and its threat test and defence planner count sites (U16):
  both read the same list.

Evidence, from the user's own binary: `tools/re/probe_site_targets.py` runs
`411700` over eleven units (own, ally and enemy; finished, a fresh site and
one a quarter left; seen and unseen; `0x2000`; dying), `4d8370` (no scan on
Hold Fire, Return Fire or unarmed; a lone site returned; the radius and the
scanning slot), `40f330`'s budget (two own sites passed, a paralyzed unit
counted) and its switch block (a stone slot taken against a finished target,
not against one with 1.0 or 0.25 left): 14 observations.
`tools/re/check_fire_sweep_picker.py` runs `412620` on 4,000 random cases
(plain, melee, stone, freeze and charm classes, `fireatwillrandom`, the sweep
and the mission radius, one to six candidates finished or not, paralyzed or
not, bad targets, immune types, AI-owned or not) against `fire_sweep_test
--picker`: the same pick and the same RNG state in every case; with the old
x10 test it fails 119 of 600 cases, with the old erase 65 of 600.

Before: World left enemies with construction left off its lists (the Retail
AI's brain added them back for itself), its acquisition (`findTarget`)
skipped them, the picker gave the x10 to a paralyzed target instead and
erased a drawn candidate keeping the list's order, the sweep charged its own
sites and passed paralyzed units for free, and the AI switch counted a stone,
freeze or charm slot as usable against a site. Units standing beside an
enemy building going up never touched it (a user's replay: three Spirit
Wolves idle 153 to 186 px from an unfinished Aramon castle until ordered).
Rebuilt with the real Crusades types on a flat map, none ever targeted it
before; now all three take it at their first scan.

Now: `rebuildVisibleLists` lists them, `retailSelectTarget` gives the x10 to
`underConstruction` and moves the last candidate into a drawn one's place,
`tickFireSweep` passes its own unfinished units for free, and
`retailAiWeaponSwitch` counts a stone, freeze or charm slot unusable against
one. `findTarget`, the engine's stand-in for `4d8370` (nearest first rather
than the picker's draws, within the longest range + 90 px), takes a site with
its rank, the squared distance, x10 (the scatter score x10 for
`fireatwillrandom`), except in a guard's scan and in a scan whose slot is a
stone, freeze or charm class. The Classic brain takes the World's list as it
is. Test: `fire_sweep_sites`.

Not ported: a retail site at HP 0 or below is live to retail's scans; only
restored saves have such sites here, and World's lists and slot checks test
HP > 0. The picker's paralyzer tests (`4128e1..4128ff`: a paralyzer skips
an `immunetoparalyzer` type; `412905..41292c`: and a paralyzed unit) are in
`retailSelectTarget` since 2026-11-03 (see "Mission target scan").

### A computer's Sea Dragon on Hold Position never fires at 472 px (2026-11-03)

The 0.9.12 attack sweep found a computer-owned Sea Dragon on Hold Position,
fire at will, that takes an enemy 472 px off (inside Fire Breath's 525 and
Water Ball's 800, outside the Tsunami's 450) and never fires at it, unit or
building. Retail does the same; the engine is unchanged.

The AI weapon switch (`40f411..40fb92`) looks at no range: its only calls
are the slot target `51a9a0`, the mana cost `531ea0`, the class tests
(vtable +0x1c/+0x20/+0x24), the enemy and ally counts `411ae0`/`411dd0`,
`rand` `535cc0` and the switch `51a8b0`. With Fire Breath holding a lone
enemy that it cannot half-kill, it takes the Tsunami (aoe 180: enemies 1 >
5 x allies 0) before it reaches the Water Ball. The Tsunami's own sweep finds
nothing in 450, and a costly slot with no target goes back to the primary on
`rand(10) == 0`. Fire Breath takes the enemy again in that same visit and
gives it up for the Tsunami on the next one. `51a8b0` gives the new slot its
`switchreloadtime` (weapon +0x9e at `51a978`; the FBI parser sets it at
`530817..53082b` and defaults it to `reloadtime` x 30, 90 ticks for Fire
Breath). Each return to Fire Breath therefore restarts its reload, and a
sweep visits each unit at least once every 30 ticks, so Fire Breath is never
selected long enough to fire. A player's Sea Dragon runs no AI switch and
fires Fire Breath. So does a computer's that cannot afford the Tsunami (it
takes the Water Ball, which reaches) or whose Fire Breath does at least half
the target's HP.

`tools/re/probe_seadragon_switch.py` runs `40f330`, `51a8b0` and `51a7f0`
natively for the Sea Dragon's slots, 3000 visits on a fixed seed: the
Tsunami is taken with no range call, Fire Breath comes back 273 times, never
stays selected for more than one visit, and gets a reload of 90 each time.
With 600 mana the block takes the Water Ball (7 observations). The engine's
`seadragon_hold` test is the guard: 0 shots from the computer's Sea Dragon
against an Archer and a lodestone, about 85 returns to Fire Breath in 900
ticks, each with a full reload; the player's Sea Dragon fires Fire Breath 10
times.

### A flyer on Hold Position does not leave its post (2026-11-03)

Established (static, `4d7de0` and its callers): `4d7de0(unit, target,
explicit, point, auto, forced)` first refuses a unit's own target, then
returns 0 on Hold Position (`+0x130 & 0x30000 == 0`) unless `explicit`
(arg 3) or `forced` (arg 6) is set (`4d7e0a..4d7e19`), and on Hold Fire
(`& 0xc0000`) unless `explicit` (`4d7e24..4d7e2d`). VTOL_Standby's stage 1
(`4176bb`) scans with `4d8370` and calls `4d7de0(unit, found, 0, 0, 1, 0)`
(`4176cc`); refused, it falls to `417736` (return 1) and stage 2 lands an
airborne flyer whose type can land (UnitDef `+0x260 & 0x800`, layer 2,
`4175e4..417605`). So an idle flyer on Hold Position gets no attack mission:
no attack run, no hover ring, no dogfight. Its auto slots stay with the
fire-at-will sweep (`40f330`), which takes a seen enemy in the slot's range and
fires from where the flyer stands by. The other callers: ground Standby
(`40784e`, args `0, &pos, 1, 0`) refuses the same way; VTOL_Patrol
(`41a25e`, forced 1) and the guards' alarm (`4081e0`, `418c46`, explicit 1)
are not held; the air attack missions' retargets (`41963d`, `41bb08`,
`41c440`, `41ce80`, `41d125`, `41e046`: `0, 0, 1, 0`) refuse on Hold
Position, which `retailIssueAutoAttack` already did.

Ours before: VTOL_Standby's `tryOrder` (and the Legion lift's poll and
`tickCombat`'s idle acquisition) called `World::acquireTarget`, which
inserted the auto attack with no Hold Position test; `findTarget` only held a
Hold unit to targets already in reach, so a Hold Position flyer took a target
in range and flew its attack mission off its post (190-960 px in
`hold_flyer_test`). Now `acquireTarget` returns false for an idle flyer
(`orders.empty()`) on Hold Position. `hold_flyer_test` runs every armed
shipped flyer with a seen enemy in weapon range (450 px where the weapon
reaches), on the ground and hovering: on Hold Position none takes an attack
or moves, 31 of 37 fire from their post (the six that do not: the Taros
priests' selected Lightning reaches 275 px, and the Fallen Angel takes no auto target
on Maneuver either); on Maneuver 35 of 37 take the attack. Inferred, not
emulated: that the sweep's fire from a landed flyer matches retail shot for
shot.

This is the shipped default for some flyers: `standingunitorder = 1` (the
Thirsha `zonhunt`, the Ghost Ship `tarship`, the Dirigible `verball`, both Taros
priests) sets UnitDef `+0x260` move bits to 0 (`4c005d..4c00a1`: 2 -> move 1
fire 2, 1 -> move 0 fire 2, 0 -> move 0 fire 0), and the unit takes them into
`+0x130 & 0x30000` (`511c2a..511c3e`). So an idle Thirsha, as built, stands by
and shoots from where it is. `flyer_combat_test` now expects no automatic
attack for those types, and `blocked_shot_test`'s Thirsha is put on Maneuver
so it is still hovering in when its order comes.

Ground Standby is not changed: ours inserts an auto attack that a Hold unit
may not chase (`tickCombat`'s `mayChase`, `findTarget`'s reach test), where
retail leaves the target to the sweep. Both stand and shoot what is in reach;
the target choice (nearest vs the sweep's sampled score) and the order state
differ. Left for the target-search port.

Since the target-search port ("Mission target scan"), `findTarget` and
`acquireTarget` are gone: VTOL_Standby and ground Standby run the mission scan
and issue through `retailIssueAutoAttackAllowed(unit, found, 0)`, which refuses
on Hold Position as `4d7de0` does, so the Hold Position return above now lives
there. `hold_flyer_test` and `flyer_combat_test` still check it.

## Blocked shots: retail fires on, the engine walks to a clear shot (2026-10-19)

Retail has no line-of-fire test and no reposition. The common weapon update
(`52ae90`) admits AimWeapon/FireWeapon on target lookup and the base inclusive
2D range virtual alone (`52fe30`; `probe_weapon_gate_loss.py` drives it with a
zeroed visibility grid and it still fires). In flight, `52a4d0` stops the shot
at the first terrain or feature cell it meets, before the unit behind it
(`probe_ballistic_blocked_target.py`: an Arabow arrow hits a feature at
`(452.987,20.647,400)` and calls `529c10` with no unit; without the feature
the same arrow hits the enemy). Nothing feeds that impact back to the shooter,
so a retail archer behind a hill crest or a rock fires into it until the order
changes. A destructible feature wears down and then lets the shots through.

The engine already deviates with a 2D clear-shot test on the nav grid for
ranged ground weapons whose shot flies the line (`slotClearShot`,
`tickCombat`'s `los`; since 0.9.3 not a plain Ballistic shell, see below).
That test cannot see a passable crest a low arc clips, water, a tall feature
that does not block movement, or the corner cells its Bresenham walk skips,
so since this change the sim also watches where shots land
(`World::noteShotImpact`, every ballistic, straight and guided shot, and
since 0.9.2 every flame: `tickFlames` reports a flame's first impact with the
unit it was fired at, `FlameShot::targetId`, hashed; since protocol 306 the
mind-control, stone and freeze shots too). Melee hits land on the target
itself, and Remote Effect spells and wandering storms are not aimed shots, so
none of them can stop on scenery:

- An impact on terrain, water or scenery the weapon cannot destroy
  (`indestructible`, no `damage`, `unitsonly`; a fire starter still counts a
  flamable feature as destructible) that lies short of the target and outside
  its body plus one body plus half the weapon's splash counts against the
  target. A hit on the target, or a miss beside or past it, clears the count;
  so does the shooter or the target moving more than 16 px from where the
  last counted shot was fired from or aimed at (`blockedSpotLeft`; the
  target's spot is `Unit::blockedTX/TZ`, hashed). Retail
  keeps no such memory, so the write-off is of the line between the two
  spots and never of the target: before, only the shooter's spot was kept, so
  a unit standing still and choosing its own targets skipped an enemy it once
  found blocked even after it walked into the open (`blocked_shot_test`'s
  walk-out case: 0 hits before, hits after). Lobbers are
  included: an arc that clears never counts. The body is a circle of its
  footprint's half-extent; a building longer one way (a castle, a gate) is
  also held to its footprint rectangle (`nearBody`, since 2026-10-30), since
  retail reaches a unit through the cells it owns: `52a4d0` emulated on an
  Arabow arrow at a 4x12-cell body 128 px off
  (`probe_long_footprint_blocked.py`) hits it at its near face (497.1) on
  open ground, stops on terrain with no unit at 453.0 and 488.3 with a ridge
  two cells or one cell before that face (75 and 40 px short of the centre,
  inside a circle of its 96 px long half-extent), and still hits the body
  with the ridge under its own near column. With the circle alone, a crest in
  front of a Taros Castle's long face (8x20) never counted, so the attacker
  never walked; the clear-chord test below skips only the cells near the
  footprint for the same reason.
- After three in a row (`shotBlockedHere`), an auto weapon slot treats the
  target as having no clear shot from here (the sweep drops it and picks
  another), and auto-acquisition (`findTarget`) does not pick it again from
  this spot. An attacking ground mover that is allowed to leave its post
  (not Hold Position), or a Hold Position unit whose auto target interrupted
  a movement order (an attack-move, a patrol or a move queued behind it: the
  Creon Fire Wagon ships Hold Position, `standingunitorder=1`, and the AI
  attack-moves it), searches the cells within 12 of it for the nearest spot
  in the weapon's range band of the target that it fits on, can reach, is away
  from the blocked spot, passes the 2D test, and has a clear straight chord
  from its muzzle height to the target's SweetSpot through `52a4d0`'s map
  test (a ballistic arc lies above its chord). It puts a ground-mission move
  to that spot in front of the attack, so Retail and Legion each walk it their
  own way, and the attack resumes on arrival (`repositionForShot`).
- Three walks per target between hits on it (a hit renews them); when none
  is left the unit fires on as retail does, unless its target is an auto
  target with a movement order behind it: that unit drops the target and
  goes on with the movement (also when a search finds no spot). A search that finds no spot
  writes off the moment, not the target: the unit fires on (still counted
  blocked, so the sweep prefers another target) and searches again after
  three more blocked shots (`Unit::searchFailed`), so a target that moves
  behind a different obstacle, or a long-lived one attacked later from a new
  approach, is searched again. Buildings, melee and flyers on attack runs
  never move, and neither does Hold Position with nothing queued behind its
  attack (it fires on, as retail). The state (`Unit::blockedTarget` and its
  counters) is hashed, with a tag, only once a blocked impact has been seen.
- A `hoverattack` flyer (since 0.9.11) moves round its hover ring instead
  (`World::hoverForShot`). Retail's hover mission keeps the flyer's bearing
  from the target at every wake (`41de5d`: only a 1-in-100 draw turns it, by
  up to 2048), so a Thirsha whose Lightning stops on a standing stone between
  it and its target fires into the stone for as long as the order lasts. In
  the user's 0.9.10 game two Thirshas did that for minutes: one at a Hunter
  behind a `ZonHenge11` (height 94; every bolt stopped about 148 px up, on
  the stone's top), one at a lodestone behind a `ZonHenge09` (height 140).
  At the first wake with the shots written off, the flyer takes the nearest
  bearing on the ring, in 1/32 turns either way, whose chord from the hover
  point's muzzle height to the target's SweetSpot is clear, and keeps that
  point until it can have flown there. The walk budget and the failed-search
  rule are the ground mover's.

`blocked_shot_test` (Retail and `--legion`) uses the shipped Aramon Archer and
Swordsman: behind a passable ridge and behind a row of shipped `ZonFireD01`
art, the archer on Hold Position lands 11 of 11 shots short in 30 s for no
damage (retail); on Maneuver it lands 3 short, walks 49-88 px and kills the
target with 5 hits. Its sticky case: an archer whose first search fails (a
full-height campfire wall out of reach) walks once and lands 16 hits after the
target moves behind the short row; before the fix it never walked again.
Since 0.9.2 it also replays the user's Fire Wagon: a shipped `crefire`
344 px from an `aralode` with a shipped `ZonHenge04` (1x2, indestructible,
height 60) on the line of fire that the Bresenham walk passes beside. Idle on
Hold Position it burns the stone 10 times for no damage with the count at 10
(before: count 0); with the AI's attack-move queued behind the auto attack,
Hold Position or Maneuver, it lands 3 flames on the stone, moves about 38 px
and destroys the lodestone (before: 20 flames on the stone, no damage, no
move).

### The 2D line test is decided per weapon; Ballistic shells are exempt (0.9.3)

Until 0.9.3 the 2D test exempted a unit only if one of its weapons was
`lobpreferred` (`UnitType::lobs`). That held every plain Ballistic shell to
it: 42 shipped unit types in `units/` (the same set in Crusades' `unitscb/`)
carry a Ballistic weapon that is not lobpreferred -- every bow, arrow tower,
cannon, ballista, spear and musket, both trebuchets; 38 of them ground units,
7 of those structures (`araat`, `aratre`, `cregatl`, `tarcage`, `verat`,
`verfltwr`, `verlight`) -- while the Creon Sage's line-of-sight Blue Flame was
exempted by its lobbing Mortar slot. Nothing in retail tests a line: the weapon update
admits on range (`probe_weapon_gate_loss.py`: with an all-hidden visibility
grid `52ae90` still reaches AimWeapon/FireWeapon), `BallisticWeapon::inRange`
(`52bc60`) solves only the arc, and the shell's 3D flight meets whatever it
meets (`52a4d0`). A shipped Aramon Trebuchet (`aratre`: Ballistic, range 2700,
minrange 500, not lobpreferred) whose line crossed its own buildings held an
auto target for 36 game-minutes without a shot in the `classic_ai_duels` seed
0x4444 game.

Now the test is a property of the weapon (`Weapon::needsClearLine`: ranged,
not melee, not `ballisticClass`), decided where the shot is decided: each slot
on its own in the sweep, the hit response and the weapon update
(`slotClearShot`, `tickCombat`'s `slotAims`), the reposition search on the
slot it repositions for, and the approach slot (`approachSlot`: the selected
weapon, or the longest when all fire) for the unit as a whole -- closing in,
`findTarget`'s acquisition, the flyer/structure hold and Legion's reach ring
(`World::approachNeedsClearLine`). A Ballistic shell is fired on range and the
blocked-shot count above catches one that keeps landing short.

A unit that may not leave its post (a building, Hold Position on an auto
target) and whose approach weapon needs the line drops an AUTO target it has
no line to (before: it held it for ever without a shot, and nothing else got
shot at); `findTarget` applies the same test from the same spot, so it is not
picked up again until the line clears (an explicitly ordered target is
kept). `ballistic_line_test` (Retail and
`--legion`): a Trebuchet 1200 px from a target behind a wall of shipped
`CAVStalag01` stalagmites (blocking, indestructible, height 60) and an `araat` arrow tower 350 px from one
fire (before: neither acquired); a Veruna tower (`vertower`, line of sight)
with one enemy behind the stalagmites and one in the open shoots only the open
one, and one whose target a row of its owner's walls comes to hide drops it
once and shoots an enemy in the open (before: held it blind for the rest of
the run, 60 s).

## After an unload the carrier stands by and lands (icd, 2026-10-12, protocol 256)

`VTOL_UNLOAD` (`41ae20`) flies its carrier along a step-out point controller
aimed `2*(transportdistance+400)` px ahead while the passengers beam out
(`retailUnloadStepOut`), and it retires after its one-tick empty tail with that
controller still attached. The dispatcher (`4d8450`) removes the mission and,
finding the list empty, builds the FBI default mission in the same call
(`4d856a` -> `4d83b0`): for owner types (+0xea) 1 and 2, whenever the type's
default (+0x24b) is nonzero. The mover is never consulted. `VTOL_Standby`
(`417350`/`4175c8`) resets weapons, sleeps one tick, finds no target and, being
airborne, installs `VTOL_LANDIFCAN`, whose stage 1 replaces the step-out
controller with a 32 px brake point. A retail carrier therefore lands at its drop
point. Checked by emulation:
`tools/re/probe_transport_air_unload_default_mission.py` runs the real
dispatcher and unload over the existing native fixture and sees the default
mission built on the retiring tick with the controller attached, plus the gate
over ten owner-type/default cases.

World used to suppress standby until the step-out controller finished, because
the earlier integrated native trace ran with no default mission set: the Roc,
Ghost Ship and Aerial Juggernaut all flew 1100-1500 px past their drop points
(user replay `game-1-1791860558`). `World::standbyEligible` no longer waits for
an active retained controller, and the unload's own retirement starts standby
in the same tick, as the dispatcher does. Still deviating: after an ordinary
flyer move's point controller has arrived, World lets the detached coast settle
before standing by. Retail does not wait there either, but landing during the
coast scatters a packed flyer formation's touchdowns (`legion_flyerfront`
air-only 56 -> 171 px), so that is left for a separate decision. Computer
players (owner type 3) get no default mission in retail; World gives every
player one, as before. `transport_post_unload` covers every shipped carrier.

## Melee never reaches a flyer in the air (icd + emulation, 2026-10-30, protocol 307)

The owner asked whether Titans are supposed to hit flying things. In retail a
melee weapon never strikes a flyer in the air, and strikes a landed one like
any other unit. Every test reads the target's flight layer, unit `+0x130 & 3`
(1 on the ground, 2 in the air, 0 off the map; World's `flightGroundMode`,
with `embarked()` for 0), never its type. Emulated (`tools/re/check_melee_air.py`,
synthetic units, the checks native, the enemy scan, damage and navigator
calls hooked):

- **In range.** `52b980` returns 0 for a layer-2 target before it boxes the
  footprints (`52b98c..52b997`): two 2x2 bodies in contact are in range in
  layers 0, 1 and 3, never in 2. The weapon update (`52ae90` -> `52ad60` ->
  vtable +0xc) therefore never aims or swings a melee slot at a flyer in the
  air, whoever gave it the target. It reaches the release (`530220`, SET 23)
  only after aim start `52fe30` has passed the same test, so a swing begun on
  a landed flyer that then takes off does not land (World's release sits
  behind `meleeInRange` too).
- **Picker.** `412620` asks the slot's handler (vtable +0x14, true only for
  `MeleeWeapon`) and then keeps layer-1 candidates only (`412932..412945`);
  the sweep's keep test does the same (`40fcb3..40fcc6`). Sweep and idle-scan
  (`4d8370`) variants: a lone enemy in the air -> nothing, one in the air and
  one on the ground -> the grounded one, a landed flyer -> taken; a plain
  ranged slot takes the one in the air.
- **Hit response.** `409a50`'s retaliation answers a melee victim's attacker
  in layer 1 only (`409db4..409dcc`): an attacker in the air gets no `4d7de0`,
  a grounded or landed one does.
- **Orders.** `4de530` kind 3 gives a walker whose weapon (`519ac0`) is melee
  `ATTACK_MELEE` whatever the target's layer: it tests the air bits only
  against `toairweapon`/`noairweapon`. Attack_Melee's head (`404778..4047ee`)
  returns 5 (the mission is removed) for a carried target (+0x114 bit 4) or
  one in layer 0 or 2, at every dispatch, before any navigator or weapon
  call; a grounded or landed target
  gets the manual slot, its target and the chase circle (`519b70`, `51a700`,
  `4d4da0`). So a right-click on a flyer in the air is taken and dropped at
  once, and a chase ends when its target takes off. The cursor selector
  (`0x4dd780`) does not special-case melee: the attack cursor shows.
- **Splash.** A melee shot sits on the struck unit's origin (`52bac0`) and its
  area pass (`529dc0`, see "Impact damage") measures to the unit boxes in 3D.
  The Titan's Hammer (aoe 31, edge 0.7) striking a ground unit with a flyer
  straight overhead hits the flyer only while its box is within 15 px of the
  struck unit's height (fraction 1.0 at 0 px, 0.70 at 14, nothing from 15);
  shipped flyers cruise 50 to 200 px up. `retailSplashUnits` already matched.

World boxed the footprints in 2D only and filtered flyers by type. A Titan
told to attack a Gryphon hovering over it hammered it (4073 HP in 10 s; 5703
in Crusades), and an idle one 120 px away acquired it (`findTarget`, World's
`4d8370`), walked under it and hit it. Of the 48 shipped mobile melee types
(47 in Crusades) only the five whose weapon has `noairweapon` (the Knight,
both wolves, the Giant Orm and the Giant Barracuda) left it alone; 38 others
hammered it from under it (37 in Crusades), and so did the four gods. The
sweep skipped every flyer type, so a landed one was never swept. In the
owner's 0.9.12 replay (`titan-0912.takrep`, Crusades) a Titan struck flyers
ten times: two Gryphons, an Iron Beak and a Parrot in the air, six landed
Parrots.

Since protocol 307 (no bump: 307 is unreleased): `meleeInRange` returns
false for a target with `flightGroundMode == 2`; `tickCombat` drops the
attack of a walker whose selected weapon is melee while its target is in the
air or carried (Attack_Melee's head; World kept chasing a unit that boarded a
transport); `findTarget` skips a target off the ground when
the scan's weapon (`519ac0`) is melee; the sweep picker, its keep test and
the retaliation read the layer instead of the type, so a landed flyer is a
melee target (a Titan walking a move into a landed Gryphon swings at it). Our
AI levels do not order a melee unit at a flyer in the air (the order would go
at once): a defender leaves it alone, a strike unit goes for its site.
`melee_air_test` (both balances) covers the Titan and every shipped mobile
melee type: no kept order, no hit, no acquisition and no walk after a held
Gryphon, ordered or idle; the landed Gryphon hit ordered, idle and by the
sweep; the attack dropped on the tick it takes off and when its target
boards a Roc; the Hammer's splash missing a Gryphon 150 px over its target.
Before the change 16 of its checks failed (8 in each balance).

Still deviating: World drops the attack on the tick the target is airborne,
retail at Attack_Melee's next dispatch (stage 1 sleeps
`rand(5)+rand(5)+4` ticks while the target is far); the weapon cannot swing
meanwhile in either.

**Air flags by layer, carried targets (2026-10-31, protocol 307).** Every
`noairweapon` / `toairweapon` test in the binary reads the target's layer,
not its type (static: the only `+0xc8` bit 0x4000/0x8000 tests are
`530580`'s head, Attack_Chase `403d8f`, the hit response `409dd2`, the hover
selector `4ddaae..4ddbf5`, the order resolver `4de7f7..4de817` and the mission
at `4030d4`, each against `+0x130 & 3 == 2`): toairweapon takes a target in the
air only, noairweapon never one, and a landed flyer is a ground target. World
now tests `flightGroundMode` in `canAttackTarget` (which also gained the
toairweapon refusal of `4de7f7`), the picker, the hit response, the
fire-at-will scorer and the slot aim/fire gates; the Retail AI's enemy list
(`ClassicBrain`) reports layer 2 from `flightGroundMode`. No shipped weapon has
`toairweapon`; the Knight, the Grenadier and the wolves have `noairweapon` and
now attack a landed Gryphon (`melee_air_test`). Attack_Chase's head
(`403c9c..403cb0`) ends a walker's ranged attack on a carried target as
Attack_Melee does; World now drops it too.

## Shooting at flyers: lead, hit band, and the held attacker (icd, 2026-10-14, protocol 262)

A leading shooter aims at the target's SweetSpot (a model point, at the flyer's
altitude) plus 80% of the lead (`52fd80` -> `51aa50`, factor 0xcccc), and the
lead comes from the target mover's velocity at +8/+0xc/+0x10 (`51ac3b..51ac82`),
vertical included. The angles freeze when the aim gate passes (`52fff0`); the
shot is made at the cast's release frame (`530220` -> `52c540`) with those
angles and the body's current heading, so a Lokken's `attack1` Fire Ball pitch
is about 29 ticks old. `52a4d0` hits a flyer only when the shot passes through
its cell between its altitude (type +0x13e is zeroed at `4c1419`) and its model
top (+0x14a, `546f40`); a shot that runs out of range vanishes without splash.

World held a non-`hoverattack` flyer still once its attack target was in range
(retail flies it on attack runs instead, and dogfights a flying target; World
does too since protocols 263 and 305, see "Attack runs" and "AirToAir" below).
That hold skipped the flight
body but kept `flightVelocity`, so every leading shooter aimed ahead of a flyer
hanging still: a Lokken's Fire Ball missed a held Gryphon, Fallen Angel or Wrath
of Tammuz every time (0/47, 0/50, 0/50). Since protocol 262 the hold zeroes the
velocity and the speed (speed is |velocity| in `tickFlightBody`), as a retail
mover with no velocity is still. `flyer_combat_test` covers the five flyers that
hold (zongryp, tarang, zongod, verball, creaeri) in both balances.

Side effect, retail arithmetic: the Gryphon's SweetSpot sits 0.05 px above the
bottom of its hit band, so an unled straight shot at a still one arrives about
2 px low and passes underneath. (The Dirigible's did too, 0.08 px, until
protocol 295 made SweetSpot the drawn piece bounds: it is 37 px up now, see
"SweetSpot is the drawn piece bounds".) Mage Archers, Mage
Towers, Strongholds, Gatling Crossbows, Beast Riders, Heket and the Beast Lord
stopped hitting a held Gryphon (they hit before only because the stale climb
velocity raised their aim). The low Crusades Wisp is missed the same way.
Against moving flyers the unguided Fire Ball hits about 29% (Gryphon 11/34, Bat
0/39) and the Guided Fire Ball 70-95%; every part probe (aim, SweetSpot, lead,
gate, launch, shot update, hit test) matches native, so retail missing moving
flyers at similar rates is an inference from the parts, not an emulated whole
engagement.

Splash, facing and attack runs, the items this section listed as not ported,
are ported: see "Impact damage", "The facing request" and "Attack runs" below (all
protocol 263). The run mission is AirToGround
(`41bf80`), not `VTOL_SeekAttack`: `41b6e8` is a `VTOL_SEEKATTACK` push inside
AirStrike, and the `41bc3b` read belongs to AirStrike too.

### World's other flyer holds (protocol 263)

World holds a flyer still, skipping its flight body, in six more places:
SetMission's wait (`w N`) and ambush (`wa`), an emptied order list until
standby lands it, a guard within 70 px of its charge, a flying builder at its
repair or reclaim point, and FlyingBuild waiting out mobile units on its site.
None stopped both. The wait, the ambush, the guard and FlyingBuild's retry kept
the velocity the flyer came in with and only braked its speed; the emptied list
zeroed the velocity but only braked the speed; the repair or reclaim point kept
a velocity with no speed. A Gryphon told to wait mid-flight hung still reporting
3.4 px/tick (and 1 px/tick of climb), a Dirigible's 0.005 brake left its speed
up for about 16 s, and MoveRate stayed at 1-3. Since protocol 263 every hold
zeroes both (`holdFlight`), as the attack hold does. `flyer_combat_test` checks
all six for the Gryphon, the Dirigible and the Aramon flying builder in both
balances, with MoveRate 0 where the flyer is not turning and `moving()` false
after a stop: 50 failures before, none after.

Still deviating: retail does not hold a flyer in these states at all; its mover
is independent of the mission (static reading). The unit update runs the mission
dispatcher (`4d8450`) and then the mover (`4dc800`, `51e1e4..51e215`) for every
unit with one; `4dc800` updates the navigator (VTOLNavigator `524af0`) and then
runs the VTOL velocity kernel `4da7d0` for any `canfly` type, controller or not.
`524af0` returns at once when no controller is attached, so the destination
(+0xc) and its velocity (+0x18) stay as the last controller left them, and
`524ab0` hands them to the kernel. Clearing a mission's controller (`4d4d40` ->
`4e4de0`) posts 0x400 and stores the new one without touching the destination.
The Wait (`401480`) and WaitForAttack (`4017a0`) handlers only sleep or wait on
events. So a retail VTOL in a wait, an ambush or FlyingBuild's retry keeps
flying to its navigator's last destination and settles there. World stops it
where the hold began: with `m 1300 1000 w 5` a Gryphon stops about 123 px short
of the move point, where VTOL_Move's coarse controller (radius 80-144 px) arrived
and retired because another order follows; FlyingBuild's retry hovers at its
first controller's 2 x `builddistance` ring (about 290 px out for the Aramon
flyer) instead of over the site. Porting that means running the flight body on
the retained navigation during these holds, as `tickRetainedFlightMovement`
does for an empty list (and `pathblock_test`'s flying-builder case expects the
flyer to stay where it arrived).

**Since protocol 301 (0.9.7)** two of these holds are narrower. A flyer's
guard runs VTOL_Follow (see "Helping to build and guarding" below) and flies
its orbit; the 70 px hold is left only for a guard whose look has not started
(a flying builder still busy with its own job). A flying helper or repairer
approaches to 2 x `builddistance` and then circles the job at about
`builddistance` (`tickConjureHover`, as FlyingBuild did), so it no longer
hovers on its repair point; the reclaim point hold is unchanged.

### Impact damage: the direct hit or the 3D area pass (protocol 263)

Every weapon class ends in impact dispatch `529c10`: Melee, Ballistic and
DroppedBallistic, Line of Sight and its subtypes, Guided (through `52c6d0`),
Remote Effect, Earthquake, Hailstorm, both area spells and Wandering. A death
weapon goes through it too: `52ac00`, from retirement `512ae0`, builds a shot at
the dying unit's position (+0x68..+0x70, so a flyer's blast is at its altitude)
with no owner and no unit struck. A feature's explosion (`52aca0`) calls the
area pass directly. `529c10` applies only the direct hit `52a460` (`52a330` at
fraction 1.0) when the shot struck a unit and the weapon's `areaofeffect`
(WeaponType +0x8a, u16) is at most 16. Of the shipped unit files' 138
`areaofeffect` entries (133 in Crusades) only the Fire Mage's Fire Swirl, 10,
is that small; weapons without one are 0. Every other impact, a bigger
blast or one with no unit struck, runs the area pass `529dc0`. The struck unit
gets no separate hit there and takes the same falloff as anyone else.

`529dc0`'s unit half: radius r = aoe >> 1, impact cell = whole pixels divided
by 16 toward zero, window r/16+1 cells either side clamped to the map with an
exclusive far edge. It walks rows, then columns. Each cell gives its ground
occupant (map word +0) and then its airborne one (word +2; 0 and 0xffff
skipped), so a flyer that owns no cell of the window, such as one of a stack
whose cells were drawn for its neighbours, is not hit. Its checks:

- **Liveness:** the unit's +0x130 bit 0x1000000 must be set.
- **Owner:** the shot's owner (+0x7c) is skipped. Its own side is not, so a
  catapult shell hurts the catapult's allies.
- **Repeats:** the first 20 units met are remembered and skipped if met again.
  Past 20, a unit filling several cells is met once per cell.
- **Distance:** from the 3D impact to the unit's box, per axis zero inside the
  box. The box is type +0x13a..+0x14e, set by `4c1410` to +-footprintx*8 and
  +-footprintz*8 around the position, from the position's height (+0x13e is
  0) up to the model top (+0x14a). The 16.16 deltas are summed as
  dz*dz + dy*dy, then dx*dx, on the x87 at double precision. The square root
  is truncated (`_ftol`) and the whole-pixel word kept.
- **Falloff:** a unit is hit if d < r, with fraction (d/r - 1)^2 * (1 - edge)
  + edge (`52a073..52a0a9`, double precision, stored as a float), or exactly 1
  when d is 0.

`tools/re/check_splash_units.py` runs native `529dc0` on 4000 random layouts:
units in both words, owners, more than 20 units, arbitrary boxes, impacts off
the map, any aoe and edge. It matches `retailSplashUnits`
(`src/sim/retailsplash.h`, which `World::applyHit` runs) on every recipient,
in order, and every fraction's bits (4245 recipients, 3287 below full damage).
Copies of the port each broken one way fail it: 2D distance, an inclusive
window edge, no owner skip, no repeats past 20, a float falloff, a floored
impact cell.

World measured flat distance to the unit's centre over its spatial grid. It
damaged the struck unit in full and then the others, and ran the scenery pass
even with a unit struck. A ground blast therefore hit a flyer overhead.
`flyer_combat_test` (both balances) shows the differences:

- A Catapult shell bursting on the ground under a Gryphon cruising 150 px up:
  1063 before, now 0. A burst at its height still hits it in full.
- A dummy at the burst point still takes the full blast. One 30 px away, whose
  box edge is 14 px from the burst, took a 0.244 share (flat centre distance)
  and now takes retail's 0.5666.
- Fire Swirl striking a unit no longer splashes a unit 3 px away (766 before)
  or the tree under it (800).
- Fire Swirl on open ground now hurts a unit whose box is 4 px away (it
  missed before).

41 of the 48 shipped melee weapons have an areaofeffect of 20-200 (the rest
have none), so most melee hits are area passes too. Retail's melee shot sits on the target's origin (`52bac0`
copies +0x68..+0x70). Neighbours whose boxes come within aoe/2 of that point,
allies included, are hit as well. The attacker, as owner, is not.

Still deviating:
- `52a330` multiplies base x fraction x attack x (1/armour) at double precision
  and stores a float. World's `hurt` computes base x attack / armour x fraction
  in float, so the result can differ by one HP at a rounding edge.
- `529c10` also skips damage for a shot with no player (+0x92 == 10) or whose
  player record's +0xea is 3. On a `waterdoesdamage` map (rules +0xd3d) it drops
  an impact in water that struck no unit. None of these is ported.
- The feature half keeps World's radius floor of 8 px and runs before the units
  rather than cell by cell.
- World detonates death weapons after the whole unit pass; retail does so
  inside it, as each dying unit retires, before later units have moved.
  (A Remote Effect landed on its target's position at fire time until 0.9.12;
  it now uses the slot's aim point, lead included -- see below.)

### Remote Effect timing and aim (emulated, 0.9.12)

`RemoteEffectWeapon`, `EarthquakeWeapon`, `AreaMindControl` and
`AreaTurnToFrozen` share init `52e890` and update `52e940`, each with its own
per-tick hook (`52ec90`, `52ecd0`, `52f000`, `52f360`); the Hailstorm has
`52ed60` / `52ee80`. Release `530220` copies the slot's aim point into the shot
before init, and the shot loop `52afd0` runs after the unit updates in the
fire tick T. Emulated tick by tick for every shipped configuration (and a
caster killed at T+0/1/2/10/40):

- **Aim point.** For a unit target `52fd80` takes it from `51aa50` for all
  five classes (vtable +0x14 = `42a6b0`, false): the SweetSpot plus 80%
  (`0xcccc`) of the target's velocity times the 3D distance over
  `weaponvelocity`. The Hailstorm is centred on it too (`52ee03`). The
  engine's `World::fire` uses `retailLeadPoint`.
- **Start.** `52e890` sets the start to T+1, later by WeaponType+0x7c
  (`nimbustime` x 30, parsed at `0x53101d`) only when the weapon's nimbus bit
  (+0xc8 & 0x40000) is set and the unit type has nimbus art (+0xdc).
  `builduptime` is pushed once in the whole binary, by the RemoteEffect
  constructor `52e780`, so it never delays a launch; no shipped weapon sets
  `nimbustime`. Before the start tick a plain spell or Earthquake dies with
  its caster (`52e95e`); the engine does not port that one-tick edge.
- **Flight.** At the start tick a `shotart` spell computes its flight:
  whole-pixel deltas from the caster's origin to the aim point (the 16.16
  high words), `fsqrt` truncated, `<<16`, divided by the weapon's per-tick
  speed (substeps x per-substep). It lands that many ticks later (L); without
  shotart L is the start tick. A flight of 0 with shot art leaves the art
  running and defers the effect a whole buildup+decay (not copied).
- **Timers.** On landing: impact = L + `builduptime`, expiry = impact +
  `decaytime` (both x 30 truncated by `52e780`). Each later tick the shot is
  destroyed if expiry <= now, BEFORE the hook runs; the hook never runs on the
  landing tick. Plain and area hooks hit when now == impact (so no hit for a
  zero buildup or a zero decay); `52ecd0` pulses whenever (now - L) %
  `shakeduration` ticks == 0 (WeaponType+0x84, truncated), so pulses fall at
  L + kP strictly between landing and expiry. The area charm and freeze
  hooks destroy the shot when the caster is dead or dying, at any tick.
- **Hailstorm.** `52ed60` places the storm on the aim point; its countdown
  starts at 20 in the fire tick, so the first stone lands at T+19, then one
  every `30 / particlespersecond` ticks (`idiv` at `0x52efcd`) until the last
  stone of `duration` (truncated) lands, which ends it: floor(D/p)+1 hits.
  It never checks its caster.

| Spell | Original (ticks after T) | Engine before 0.9.12 |
|---|---|---|
| Plain, 1 / 0.9 s | hit 31, expiry 58 | 29 |
| Earthquake, Avatar of Anu (1.5 / 1.5 s, shake 1) | 31, 61 | 29, 59, 89 |
| Earthquake, Acolyte (3 / 3 s, shotart, 200 px) | 61 .. 181, five | six |
| Hail, 1.8 s at 8/s | 19 hits, every 3 ticks | 14, every 4 |

`tools/remote_timing_test.cpp` checks each shipped timeline, the caster-death
abort and the lead against these figures.

### A hit on an unfinished site rewrites its progress

In the same routine (`51a3c2..51a408`), after the 16-bit HP subtraction, a
unit whose construction remaining (+0x108, a float) is nonzero gets
`remaining = 1 - hp / maxdamage` from the new HP word read signed (`movsx`
at `51a3e0`), whatever the damage source (weapon, fire, script). The work
routine `429af0` derives a site's HP from that fraction, so retail
construction HP is bounded by maxdamage by construction. We mirror this in
`applyHit` for retail sites, and cap the legacy `tickConstruction` growth at
maxdamage in 64 bits (a very short buildtime or a tall type used to wrap the
16.16 word).

A lethal hit leaves remaining above 1, so the next work call's
`(1 - remaining) * maxdamage` is negative; `429af0` stores `_ftol`'s low word
(`429c93`), as `retailConstructionWork` now does through `retailFtol`. Retail
then sets the site's dying bit (`51a639`, owner record +0xea 1 or 2), as for
any unit at HP <= 0. Our death sweep still exempts a retail site (a fresh one
sits at HP 0), so a site hit to HP <= 0 stays up here; it only arises in the
restored-save probe, the one place retail sites (`retailResources`) exist.

### A dying unit takes no more hits

`52a330` draws its CRT spread and hands the integer damage to `51a140`, which
returns at once unless the victim is alive (+0x130 & 0x1000000) and not
already dying (+0x130 & 0x1000). `51a240` subtracts the damage from the
16-bit HP word (+0x10c, a wrapping `sub word`). When HP reaches zero or less
on a unit its own machine runs (player record +0xea 1 or 2), `51a639` sets
the dying bit then and there; a remote player's unit is clamped to 0 and
the owner decides. Every later hit in the same frame is dropped before the
HP update: no damage, no kill credit (+0xf8), no death type (+0x10f), no
hit response (`409a50`), no HitByWeapon. The CRT draw has already been made.

World's hp<=0 sweep runs later in the tick, so `hurt` used to keep
subtracting. Two units killing the same target in one tick gave the kill to
the second, and a big enough second hit took HP past -32768: the 16.16
subtraction overflowed (undefined behaviour) and wrapped to about +30000,
so the death sweep let the unit live. `hurt` now returns after the CRT draw
for a unit at hp <= 0 (a retail construction site excepted, as in the
sweep), and subtracts in 64 bits clamped to the Fixed range, since one hit
can exceed 32767 (armour and veterancy gave 33619). `maxdamage` is capped
at 32767 when a type loads, the native word's limit; shipped data peaks at
29999. `hp_overflow_test` covers it.

### The facing request: the led SweetSpot (protocol 263)

Aim start `52fe30` (called for every slot by the weapon update `52ae90`, which
runs before the unit's missions and mover) posts a facing request on the mover
before it aims. With a unit target that passes the minimum-range (`5306a0`)
and range (vtable +0xc) tests, the handler's vtable +0x14 picks the point:
true answers the target's origin (+0x68), false answers `51aa50`, the target's
SweetSpot plus 80% of its mover's lead for this weapon (`52ff67`). The RTTI
vtables give the split. Only `MeleeWeapon` (`5f3af8`, +0x14 = `531f00`, `mov
al,1`) answers true. All sixteen other classes answer false (`42a6b0`, `xor
al,al`): `WeaponType`, `BallisticWeapon`, `DroppedBallisticWeapon`,
`GuidedWeapon`, `LineOfSightWeapon` and its Lightning, Fire, TurnToStone,
TurnToFrozen and MindControl subclasses, `RemoteEffectWeapon`, `HailstormWeapon`,
`EarthquakeWeapon`, both area spells and `WanderingWeapon`. `52fd80` makes the
same split for the aim point itself. `51b340` turns the point into a heading
(`53612a` of the unit's origin minus the point) and `4dc0b0` stores it at mover
+0x34 with +0x36 bit 0x10, telling the navigator (vtable +0x38) only when the
request is new or changed. Only a ground mover with a `turninplacerate` (type
+0x190) or a `hoverattack` flyer posts one. The readiness gate `52fff0` checks
the same units and holds fire until `|heading - request|` is at most
max(`aimtolerance`, 512).

Who turns to it differs. The ground kernel `4d9ad0` turns the body to the
request at the pivot rate while its navigator is idle and its speed is zero
(`4d9b6a`). `VTOLNavigator`'s +0x38 is a bare `ret` (`42c280`), and the VTOL
kernel `4da7d0` turns the body toward the navigator's heading (`524ab0`), so a
hover attacker's request only gates. Its heading comes from the hover mission's
controller. `41d820` stage 3 (`41de3f`) draws the ring around `51aa50` for the
selected slot, refreshed at every wake (15 + rand(15) ticks), and installs a
`NavGoalVTOL` target controller (`4e4140`, flags 0xa3, radius 4, the attack
altitude). That controller's heading (`4e4330`, flags 0x84 and 2) is the
direction from the flyer to the target's SweetSpot (`4dd4f0`), unled, taken
whenever the navigator asks (`524af0`, within 320 px). So the body heads at the
SweetSpot and fires once the led SweetSpot is within tolerance.

World faced the target's centre in all three places. Since protocol 263,
`queryWeaponAim` returns the request (`retailSlotFacing`), the gate checks it,
an auto slot posts it as `faceRequest`, and an attacking pivoting ground unit
brakes and turns to the request of its last aiming slot. The hover attack
(`tickAirToGroundHover` since protocol 307, which ports the whole of `41d820`:
"Air traffic" below) rings the selected weapon's led SweetSpot
(`retailLeadPoint`) and heads at the target's SweetSpot. Of the 202 unit types, 60 post requests and carry a weapon
that is not melee, in both balances.

Checked:
- `tools/re/check_animation_readiness.py` runs 52fe30's unit-target branch and
  then 52fff0 natively, 8192 times across all 17 handler vtables (named by
  their own RTTI) and every unit kind, with the led SweetSpot and range tests
  controlled. 4dc0b0 runs natively and its argument is observed. The port's
  `--aim-facing` matches each request, mover flag word, gate result and aim
  flag word (4590 requests). Copies of the port that face the centre for every
  class, face the led point for melee too, or take the point minus the origin
  each fail it.
- `flyer_combat_test`, both balances. A Lokken attacking a Gryphon crossing its
  front: 0 of 572 aiming ticks on the led SweetSpot before (571 on the centre),
  571 of 572 after. A Wisp hover-attacking an Aramon Barracks, whose SweetSpot
  is 22.9 px off its centre along the approach: its controller headed at the
  SweetSpot on 2 of 1200 ticks before and 1200 after. Its ring point sat within
  16 px of the hover distance from the SweetSpot (a structure: no lead) on 30
  of 55 wakes before and 55 of 55 after. 6 failures before, none after. The
  rest of the output only changes the hashes of fights that involve units that
  face.
- Lokken Fire Ball (WEAPON1) against ten flyers, three modes, 3600 ticks each,
  Crusades: against flyers crossing and orbiting it, 153 of 770 shots hit before
  and 198 of 779 after. The total across all three modes rose from 388/1103 to
  432/1108. The moving-flyer counts match the earlier trial exactly, flyer by
  flyer.

Still deviating:
- World's attack controller turns the body before the weapon gate runs, and
  the request it turns to is solved from the positions before the brake step.
  Retail gates first and turns after.
- A request stays posted in retail until something clears it (`4dc0e0`), so a
  pivoting unit whose slots stop aiming keeps turning to the last point. World
  turns it to the target's centre.
- (Retail's hover-attack target controller holds its ring point fixed between
  wakes: flag 0x80 makes `4e41d0` return the stored point; it posts 0x100
  within radius 4 and stays. World moved the point with the target and never
  posted; both are ported since protocol 307, with the hover mission, "Air
  traffic" below.)

A flyer attacking a flyer is no longer held facing its centre: it flies
AirToAir (protocol 305, "AirToAir" below). Against a target that cannot fly it
flies attack runs (protocol 263). Since protocol 305 a Dropped bomb also goes
through aim start and the gate (`52fe30`, `52fff0`), as every class does in
`52ae90`; see "AirToAir".

### Attack runs (protocol 263)

The attack order's mission comes from `4de530` (action 3). A flyer gets
AIRSTRIKE (`41b5e0`) when its selected weapon is `dropped` (WeaponType +0xc8
bit 0x20), AIRTOAIR (`41ca50`) against a flying target, AIRTOGROUNDHOVER
(`41d820`) when its type has `hoverattack` (+0x260 bit 0x8000000), and
AIRTOGROUND (`41bf80`) otherwise (registry rows `5ebd69`..`5ebdb4`). No shipped
weapon sets `dropped`. A Dirigible's or a Juggernaut's dropped-subtype cannonball
does not count, so every non-hover armed flyer (Gryphon, Wrath of Tammuz,
Dirigible, Fallen Angel, Aerial Juggernaut) attacks ground targets with
AirToGround. It never stops to shoot:

- **Stage 0** (`416c50`) Activate, BeginFlight, take-off.
- **Stage 1** `519b10(3)` (`41c308`): every slot automatic.
- **Stage 2** `519b70`/`51a700` (`41c31f`): the selected slot (`519ac0`: a
  switcher's choice, else the first slot) manual, aimed at the target (`51a770`
  at a ground point when the mission has no target unit). Stage 7 sends the
  run back here, which re-aims the slot. A multi-weapon non-switcher's other
  weapons (Dirigible, Juggernaut) pick their own targets for the rest of the
  mission (ported 2026-10-30 with AirToAir's, below: `Order::mixedSlots`).
- **Stage 3** a point controller (`4e40e0`, flags 0x20) at the target, radius
  (`4e4540`, flag 0x10) `attackrunlength`/2 (UnitType +0x234, a word read at
  `4c082e`), or half the selected weapon's range (+0x90) when that is 0. Wait
  mask 0x76a, sleep 15.
- **Stage 4** a controller event (0x700) moves on; otherwise, a tick later,
  stage 3 re-aims the approach at the target's new position. So the approach is
  refreshed every 16 ticks.
- **Stage 5**, on arrival: the run. `53612a` gives the heading of the flyer as
  seen from the target; the run point is the target minus that direction times
  `attackrunlength` + 128 + rand(128) px, or 3 x range when the length is 0.
  That is past the target, on the flyer's line through it. Radius 128 +
  rand(128), drawn first. Until the flyer has once been within range/2 of the
  target (mission +0x4e; x87 at double precision, `_ftol`), it sleeps 15 and
  re-aims the run from wherever it is.
- **Stage 6**, on arrival: the turn. rand(2) picks 90 degrees left or right of
  the body's heading (+0x7e); the point is rand(128) + `attackrunlength`/2 px
  out (the range when the length is 0), radius 64 + rand(128).
- **Stage 7**, on arrival: back to stage 2 and the approach. A flyer off the map
  (+0xa4 is the off-map cell `50e740` makes at game +0x19f30; `5066f0` links it
  when the footprint does not fit the cell grid) adds one to +0x4e instead,
  which a run within range/2 set to 1 and a wake on the map resets to 1. When
  it reaches 3, the flyer first flies 800 px toward the point 192 px inside the
  playable edge: game +0x19e90/+0x19e94, the map's pixels less 32 and 128
  (`50ef03`). The radius is 128.

Every draw is `535cc0`, the shared game random number generator (World's
`gameRand`). The controller is the ordinary non-persistent point controller.
Its target getter (`4e41d0`) answers the flyer's cruise height when it has no
altitude, so runs are flown at cruise height. It accepts (`4e43d0`) when the 2D
distance is under the radius; `524c10` then posts 0x100 to the mission
(`4e2470`, mission +0x6a) and the navigator lets it go, posting 0x400 (`4e4de0`).
The next install (`4d4d40`) clears the mission's pending 0x3700. The weapons are
not the mission's: the weapon update (`52ae90`) fires the selected slot whenever
it has the target in range.

World runs it as `retailAirToGround` (`src/sim/retailairattack.h`).
`tickAirToGround` dispatches it on the attack order's own mission state, after
the order's weapons have fired, then flies the order's controller. A move's
coast (`retainedFlightGoal`) is dropped, and the unit-level controller events
that coast posts are cleared at stage 0, because retail posts controller events
to the controller's mission and never to the unit. World held these flyers
still in range (and faced the target) instead.

Checked:
- `tools/re/check_air_attack_run.py` runs native `41bf80` 6000 times over every
  stage. Positions are on and off the map, every run length and range is
  covered, and the stage-5 range boundary is hit exactly. Native `535cc0`,
  `4d6a10`, `519ac0`, `53612a`, `5360bf`/`5360f3` and `_ftol` are used, and the
  controller calls are recorded. The port (`retail_motion_test --air-run`)
  matches every return, stage, wait mask, deadline, pending word, flag word,
  counter, controller point and radius, slot call, draw count and final
  generator state (about 1960 controllers). Ten copies of the port fail it:
  the run's two draws swapped, the turn's side swapped, the approach at the
  full run length, the map edge off by 0x40, `count>=0`, `reach>=` at the
  boundary, a fourth off-map wake, a 16-tick approach sleep, the turn at the
  full run length, and the run measured from the flyer.
- `flyer_combat_test`, both balances, each of the five flyers attacking a dummy
  for 1800 ticks: it never holds still in flight, comes within half its range
  of the target, every run point lies past the target on its line through it
  at the retail distance, and it fires. 30 failures before (held 363-1763
  ticks, never within range/2, no runs), none after.
- A two-AI Zhon game (`aitool`, Inner Circle, hard, 900 s): the AI built up to
  eight Gryphons. All 36 samples of a Gryphon on an attack order were flying
  (1.2-4.1 px/tick), and the game ran to the end.

Measured in the standard balance (Crusades within 6% unless noted), 3600 ticks
against an immobile dummy with real weapons (shots / damage, the dummy topped
up every tick):

| flyer | held (before) | attack runs |
| --- | --- | --- |
| Gryphon | 36 / 11405 | 12 / 998 |
| Wrath of Tammuz | 82 / 0 | 31 / 8.5 M (Crusades 120 / 0, now 55 / 650 k) |
| Dirigible | 35 / 5612 | 53 / 28733 |
| Fallen Angel | 30 / 260802 | 11 / 8286 (Crusades: 0 damage both ways) |
| Aerial Juggernaut | 66 / 133898 | 121 / 160486 |

The runs bring the short-range Dirigible, Juggernaut and Wrath over the target.
The Gryphon and the Fallen Angel lose most of their damage because their shots
from a run land along their path: a Gryphon spear thrown 497 px out came down
180 px from the dummy, past it, and the Fallen Angel's Death Sword, released
about 31 px out, lands 15-25 px past. The angles freeze at the gate and the
shot leaves from the moved muzzle at the release frame, and each step of that is
retail's, checked natively: the readiness gate `52fff0` writes the aim record's
heading and pitch (+0x16/+0x18) only when it passes, and while the reload runs
it neither recomputes the geometry (`52fd80`) nor touches them; aim start
`52fe30` refreshes them only when no aim is pending (flags & 0xe0 clear)
(`check_animation_readiness.py`); and the launch builds the shot from those
stored angles, the body's heading at release (+0x7e) and the QueryWeapon muzzle
at release (`52c540`, `probe_straight_projectile_launch.py`; `52be80` is wired
the same way). The Gryphon's gate passes 15 ticks before its FireWeapon script
sets 23 (traced on the 497 px throw: gate at tick 65, spear at tick 80), over which it
closes about 54 px and climbs 15, so its pitch is the one for 497 px from a
lower muzzle and the spear lands long. Retail's spear from a closing Gryphon
should land as far off; a whole native engagement has not been run.

Shooting at a Gryphon on its runs (3 trials x 1800 ticks, the shooter starting
150 px beside the target, free to chase), hits / shots:
Gatling Crossbow 16/19 (Crusades 20/21), Beast Rider 15/15 (12/19), Mirazi
Heket 20/37, Dirigible arrows 18/21, Beast Lord 6/35 (27/27), Mage Archer
Paralyze Arrow 38/41 but Standard Arrow 0/20, Stronghold 0/4, Mage Tower
lightning 0/13 (0/48 against a held Gryphon too). A Lokken's Fire Ball against
flyers on their runs: Gryphon 3/21, Wrath 3/18 (Crusades 22/65), Dirigible
43/52, Fallen Angel 17/52 (5/50), Juggernaut 45/64. Against the held flyers it
hit 72-75 of 72-75.

Still deviating:
- (AirStrike (`41b5e0`), for a selected weapon with the `dropped` key, is
  ported since protocol 307: see "Air traffic" below. No shipped weapon
  reaches it. AirToAir, for a flying target, is ported since protocol 305:
  next section.)
- (When the target is lost, AirToGround pushes `VTOL_SEEKATTACK` (`4192f0`):
  ported since protocol 305, see "VTOL_SeekAttack" below.)
- (The group check (`41c19a`, `51d1e0(unit, 1, 5)` and VTOL_Move_Formation)
  is ported since protocol 312: see "Flyers at a point" under the SUPPRESS
  port. Stage 4's air-traffic step-out is, since protocol 307: see "Air
  traffic" below.)
- Off the map, World flies at the clamped sector's cruise height (retail: the
  off-map cell's height 0 plus cruisealt), and its velocity kernel keeps the
  vertical control that `4dab71` skips for an off-map unit.

### AirToAir: a flyer attacking a flyer (icd + emulation, 2026-10-30, protocol 305)

`4de530` gives a flyer whose target's type can fly (UnitType +0x260 bit 0x800,
the type, so a landed target too) AIRTOAIR (`41ca50`, registry row `5ebd82`)
when its selected weapon (`519ac0`) lacks the `dropped` key (WeaponType +0xc8
bit 0x20); with the key, a flyer gets no attack mission against a flyer
(`4de6ed`). No shipped weapon has the key (`subtype=Dropped`, the
`DroppedBallisticWeapon` class, is not it), so every armed flyer dogfights a
flyer, `hoverattack` or not: World had held a non-hover flyer still in range
and hover-ringed a Tarbeak or a dragon round it. The handler:

- **Stage 0** `416c50` (Activate, BeginFlight, take-off), sleep 1, mission
  +0x4e = 0. **Stage 1** every slot automatic, the selected one manual and
  aimed at the target (`519b10(3)`, `519b70`, `51a700`), sleep 1.
  `416c50`'s first call is `519b70(3)` (every slot manual); `519b10`/`519b70`
  flip WeaponRecord +0x1a bit 4 per slot (a slot holding a weapon, +0xc) and
  drop the target (`51a7f0`) only of a slot whose mode changes. `519ac0` is a
  switcher's selected slot, else the first slot that has a weapon. So for the
  rest of the mission the other slots are swept like any auto slot (`40f330`,
  `412620`) and fire at their own targets (`52ae90`): an Aerial Juggernaut
  (arrows slot 0, Dropped cannons 1 and 2, no switching) bombs the ground
  while its arrows fight. Nothing in the mission undoes it; the destructor
  `4d6da0` (`4d6e26`) makes every slot automatic unless mission +0x5a has
  0x8000, which drops only the selected slot's target. VTOL_Evade's
  destructor does the same, and AirToAir, back at stage 0, re-runs `416c50`
  and stage 1. World keeps the mixed mode on the attack order, as for
  AirToGround (`Order::mixedSlots`; `Unit::slotAuto` 6 for the Juggernaut,
  `slotManual` attack): the order's
  fire skips auto slots, the order's end clears only the manual slot, and
  `syncSlotMode` makes every slot manual again for any other attack at the
  front.
- **Stage 2**, every 7 + rand(15) ticks (the draw comes before the stage
  switch): P = the selected slot's led SweetSpot (`51aa50`), R =
  `airtoairdistance`: the selected weapon's (+0xc6, `5310b7`) or else the
  type's (+0x25a, `4c0548`), whose default is the hypotenuse of
  `hoverattackdistance` and `hoverattackaltitude` (+0x256, +0x258, the
  altitude defaulting to `cruisealt` +0x23a) when the distance is set, else
  100. No shipped file sets the key: the Iron Beak, the Gold, Neo, Black and
  Sea Dragons, the Drake, the Thirsha and the Wisp get 212 (the Crusades Wisp
  156), the Ancient Dragon and the Harpy 282, the Sky Knight 230, the Dark and
  Darkest Priests 158, the Ghost Ship 70, and the five without a hover
  distance (Aerial Juggernaut, Dirigible, Fallen Angel, Gryphon, Wrath of
  Tammuz) 100. `41d780` tests whether A lies in the half plane ahead of B's
  heading turned by n x 45 degrees: with n = 0 it gives *ahead* (the target in
  front of the flyer) and *seen* (the flyer in front of the target), with n =
  2 and 6 each side, and n = 7, 0 and 1 together the 45 degree *sights* cones.
  The whole-pixel distance d to P (`_ftol`'s low word) is measured against R.
- An auto attack (mission +0x52, `4d7de0`'s auto argument) more than 5R out
  rescans on rand(2) == 0 (`4d8370`, then `4d7de0`) and ends if it put a new
  attack ahead.
- **Threatened** when the target is behind the flyer and the flyer in front
  of it (the target on its tail); else, with the target in its sights, only
  if the flyer is in the target's; else d < R/4, or seen and d < R/2, or,
  unless d > 3R/2, each lies on the same side (n = 2, or n = 6) of the other.
  Unthreatened, it pursues and resets mission +0x4e when the target is ahead
  or both fly apart.
- Threatened, it adds the wake to +0x4e (only on rand(5) == 0 when each has
  the other in its sights) and, once that reaches rand(60) + rand(60),
  repositions instead of pursuing. With the target behind it and the flyer
  in its sights (rand(2) == 0), the target on its tail (rand(3) == 0), or on
  rand(30) == 0 in any threatened case, the wake is added unconditionally and
  the same count decides a break-off instead: it lets its controller go,
  pushes VTOL_EVADE (`41e3d0`) on the target, sets +0x4e = 0, and starts
  again at stage 0 when the evade ends.
- **Pursuit**: the point R px short of P on the line from the flyer;
  **reposition**: R - rand(R/2) + rand(R/2) px from P at a random angle within
  90 degrees of the line back to the flyer. Either way P first moves 32 px
  along the flyer's heading when its speed (mover +0x20) is over half its top
  speed (`421ae0`). The point is installed in the moving controller below,
  with the target mover's velocity and, as its heading, the line from the
  flyer to P (`4e4c60`); wait mask 0x68 plus the sleep.
- Off the map (unit +0xa4 the off-map cell) it flies 800 px toward 192 px
  inside the playable edge, radius 128, on wait mask 0x700, at once
  (AirToGround's stage 7 waits three wakes).

**The moving controller** (vtable `5f29a8`, 0x2c bytes, built by `4e4560`)
answers its point and then moves it by its velocity, x and z, every time the
navigator asks (`4e48d0`, every tick in `524af0`); with a heading it turns the
velocity toward it by at most turnrate / 8 (`536143`) and drops its vertical
part. It is type 3, so the navigator flies it at cruise height at any
distance. Within 48 px (`4e4be0`) its heading answer (`4e4b70`) is the stored
heading, farther out the line to the point; it posts 0x100 there and is never
let go (+0x2c answers 1). So a pursuer follows a point that runs on with the
target and swings toward the line it attacked along.

**VTOL_Evade** (`41e3d0`, registry row `5ebe63`) runs on the same target with
+0x4e (the maneuver) and +0x52 (its ticks) zero. Stage 0 picks a maneuver:
from a previous turn maneuver, rand(4) != 0 picks a straight one (rand(3) + 1)
and otherwise only maneuver 4 is redrawn (rand(6) + 1); a flyer whose 0.8 x
maxvelocity (UnitType +0x162) beats the target's picks rand(6) + 1 on rand(5)
== 0, else rand(3) + 1; one whose 0.8 x turnrate (+0x18e) beats the target's
picks rand(6) + 1 and redraws a 1 or a 4 once; any other picks either on
rand(2). Its ticks are rand(30) + rand(30) + rand(15) + rand(15) + 5, less
rand(30) + rand(15) when half its maxvelocity beats the target's; below 10
they become rand(10) + 3. Stage 1 ends the evade when rand(d) + rand(d) +
rand(d) > 3R (d to the target), restarts it or ends it on rand(2) once the
ticks run out, and otherwise flies up to 33 ticks (19 for 4..6) of: 1, a
straight line within 45 degrees of its heading; 2 and 3, a line 45 to 165
degrees to one side or the other (point controllers, budget x 100 px x top
speed out, the 32-bit product wrapping as retail's does, radius 16); 4..6,
the same arcs as the heading of a moving controller held still 32 px ahead:
brake and turn hard. Wait mask 0x28 plus the sleep.

The weapons are not the mission's. The weapon update (`52ae90`) fires the
selected slot whenever its target is in range, and runs aim start (`52fe30`)
and the readiness gate (`52fff0`) for every class. For a `subtype=Dropped`
weapon (`DroppedBallisticWeapon`, vtable `5f3ac4`): its in-range virtual
(`532290`) is the base 2D centre test `530580` (with `noairweapon` /
`toairweapon`), so it fires at a flyer in range; its angle virtual (+0x28,
`4d4450`) is a bare `ret 0xc`, so the record keeps its angles, AimWeapon
starts with them and the gate's angle test passes, but the bomb waits for the
script's SET 22 and, on a `hoverattack` flyer, for the body to face the led
SweetSpot within max(`aimtolerance`, 512) (World released it on reload alone
before protocol 305); and its launch (`52c3b0`) gives it no vertical speed and
a fall time from |dy|, so it falls |dy| on its way to the target's SweetSpot
and only reaches a point below its muzzle. An Iron Beak's egg therefore hits
a flyer cruising below the Beak (a dragon at 150, a Fallen Angel at 50) and
passes under a Roc at its own cruise height, whose SweetSpot sits about 45 px
above the egg. Dogfighting 212 px out, the Beak still comes inside its 200 px
range: the pursuit point is 32 px nearer while it flies above half speed,
and a SweetSpot sits off its unit's centre.

World runs it as `retailAirToAir` and `retailAirEvade`
(`src/sim/retailairtoair.h`), on the attack order (`Order::mission`, and
`Order::airToAir` for +0x4e, the moving controller and a pushed evade), from
`World::tickAirToAir` after the order's weapons have fired; a point
controller rides in `Order::flightGoal` as AirToGround's does.

Checked:
- `tools/re/check_air_to_air.py` runs, natively, `41d780` 20000 times, the
  controller's `4e48d0` / `4e4be0` / `4e4b70` 20000 times (7999 turns, 12156
  inside 48 px), `41ca50` 12000 times (3712 pursuits, 1033 repositions, 399
  break-offs, 548 off-map returns and every early return: the events, the
  retarget scan, stages 0, 1 and past 2) and `41e3d0` 12000 times over every
  stage and maneuver, with native `535cc0`, `53612a`, `5360bf` / `5360f3`,
  `536143`, `51b340`, `421ae0`, `519ac0`, `4d6a10` and `_ftol`. The port
  matches every return, stage, wait mask, deadline, pending and flag word,
  counter, controller point, velocity, heading and radius, draw count and
  final generator state. Twelve mutated ports fail it (the half-range test
  inclusive, the reposition arc off centre, pursuit at R/2, the wake 6..20,
  *on its tail* as *in its sights*, the speed shift at exactly half, the
  facing octant mirrored, a turnrate/4 turn, a 47 px accept, the evade's
  redraw on 2 mod 3, its ticks + 4, its turn point 16 px out).
- The same script runs `52fe30` and `52fff0` through `DroppedBallisticWeapon`'s
  own vtable with the real geometry `52fd80` 6000 times: the angles never
  move, AimWeapon starts with the old ones, and the bomb is released exactly
  when acknowledged and, for a hover attacker, facing (2607 released, 2977
  held by SET 22, 416 by facing).
- `air_to_air_test`, both balances: all 19 armed flyers that may attack a flyer
  run AirToAir against one and never hold still once flying (stages 0 and 1
  leave no controller for a tick each), none hover-rings it, and attacks on a
  ground unit do not run it. Against a holding Roc and one circling at 500 px
  the Aerial Juggernaut's arrows hit 27-31 times a minute (it used to park at
  its cannons' 500 px, outside its 450 px arrows). The Iron Beak's eggs hit a
  holding Fallen Angel 14 of 14 (Crusades, cruising at 100: 3 of 5) and a
  holding Gold Dragon 4 of 6, and pass under a holding Roc (0 of 2). A
  Gryphon and a Sea Dragon attacking each other break off 11 times in a
  minute and every evade is handed back.
- `attack_sweep` (task-attack-sweep `50bb6fbb`), the sweep's G7 cases (Iron
  Beak, Aerial Juggernaut, Dirigible against a hovering and a circling Roc,
  both balances): cases with 4+ shots and no damage 33 -> 18 (Juggernaut 13
  -> 0, Beak 20 -> 18), shots 924 -> 581, hits 198 -> 242, damage 50.8 k ->
  70.2 k. All 19 armed flyers against those two flyers: 51 -> 38 futile, 143
  -> 127 with no damage, damage +3.6%. The Beak, Dirigible and Juggernaut on
  every target: the bomb gate takes shots 3409 -> 3345 and damage -2%; a Beak
  on Hold Position no longer drops eggs it does not face at a castle or a sub
  (no damage before either).

Not ported or still deviating:
- The group check (`51d1e0`, VTOL_Move_Formation). (AirToGround's is ported
  since protocol 312. Stage 2's air-traffic step-out is ported since protocol 307: see "Air
  traffic" below.)
- (A lost target pushes VTOL_SEEKATTACK (`41d654`): ported since protocol 305,
  next section.)
- The retarget scan is World's target scan (`findTarget`, as the ground
  response's) and its gates (`retailIssueAutoAttack`'s), the new attack put
  ahead after the dispatch.
- The leash (+0x56) is checked by the attack handler before the mission;
  retail checks it after the off-map return.
- A flyer whose selected weapon has the `dropped` key (none shipped) dogfights
  a flyer too; retail gives it no attack mission (`4de6ed`).
- A whole native engagement has not been run: the hit rates above are those
  of the ported parts flown together.

### VTOL_SeekAttack: a lost target (icd + emulation, 2026-10-30, protocol 305)

AirToGround and AirToAir end on a lost target and may first push
VTOL_SEEKATTACK (registry row `5ebe7c`: class 19, flags `0x1000600`; handler
`4192f0`). AirToGround's head sends events `0x2a`, a target pointer (+0x16)
that is null, not alive (+0x130 `0x1000000`), dying (`0x1000`), carried
(+0x114 bit 4), off the map (+0x130 & 3 == 0) or whose type can fly to
`41c8f6`; AirToAir's sends events `0x28` and the same states to `41d654`.
Both return 5 (the attack is removed), first pushing the seek when:

- `41c8f6` only: the unit is alive and not dying (`41d654` does not look);
- both: the attack is the last mission in the list (+0x66 is the list's next
  link; `4d7640` walks it, `4d77b0` inserts at its end), and the unit is not
  on Hold Fire (+0x130 & `0xc0000`).

The seek is built (`4d6c40`) with the attack's point (mission +0x22: the
target's position, which AirToGround refreshes at every wake, `41c13c`; the
led SweetSpot at AirToAir's stage 2, `51aa50`) and, when the target is alive
and not dying, the target itself; +0x4e/+0x52/+0x56 are 0. `4d77b0` puts it
right behind the attack, which then goes, so it runs at once.

`4192f0`: a unit without a weapon (+0x130 `0x8000000`) clears its list (7);
event `0x200` (its controller failed) ends the seek (5); off the map it flies
800 px toward the map, radius 128, waiting on `0x700` (as AirToGround's
stage 7). Past stage 0 the air-traffic step-out (+0x12a >= 5, ported since
protocol 307: "Air traffic" below) and the group check `51d1e0` (not
ported) come next, then the group flag's rand(25). Stage 0 (`41972e`): with a
carried target, `4d7de0(unit, target, 0, 0, 0, 0)`; an attack put ahead
clears the wait mask and returns 0, a refusal goes straight on to stage 1.
Without one, a zero point becomes the unit's position, +0x4e = rand(0x10000)
(+0x52 its low bit, never read), and `416c50` takes off. Stage 1 (`419620`):
`519b10(3)`; `4d8370` (which scans only on Fire At Will) and `4d7de0(unit,
found, 0, 0, 1, 0)`: an attack put ahead returns 5. Otherwise, on a controller
event (`0x700`), +0x4e -= `0x5555` + rand(`0x2000`); a point controller
(`4e40e0`, `4e4540(0x80)`) at the point less (sin, cos)(+0x4e) x (the
selected weapon's +0x90 + 160) (`5360bf`/`5360f3` read the angle's low word),
`4d6a10(30 + rand(30))`, wait mask |= `0x700`, return 2. Any other stage
clears the list. The seek never ends by itself: a queued order waits.

Ours (`retailairseek.h`: `retailAirLostTarget`, `retailVtolSeekAttack`;
`World::seekLostTarget`, `World::tickSeekAttack`): tickCombat's lost-target
branches (target gone, dead, dying, an ally after a conversion, or one
`canAttackTarget` refuses) replace a flyer's AirToGround or AirToAir attack
with a seek order (`Order::seekAttack`, the point in x/z, the angle in
`guardOrbitAngle`) and run its first dispatch in the same tick. +0x66 == 0 is
"the attack is the only order and not `aheadOfDefault`": an idle flyer's own
auto attack (the standby's scan, the hit response, and AirToAir's retarget of
one) goes in ahead of its default mission VTOL_Standby, which World does not
queue, so it ends without a seek (the classic AI's first-mission flag
`0x2000` follows the same bit). The classic AI reads a seek order as class
19, flags `0x1000400`: the ctor (`4d6d66`) clears the target bit `0x200` for
a seek without a live target, which World's always is. `tools/re/check_air_seek_attack.py` runs
`4192f0` on 6,000 random states of the head and both stages and `41bf80` /
`41ca50` on 4,000 lost targets against the port: every result, stage, wait
mask, deadline, pending event, angle, point, controller, radius, call and
random draw is equal. `air_seek_attack_test` (both balances, every armed
flyer without hoverattack): the kill pushes the seek at the target's last
point, the flyer hops round it at range + 160 without landing and attacks a
new enemy at the point on Fire At Will; Return Fire circles without
retargeting; Hold Fire, a queued order and an idle flyer's own attack get no
seek; AirToAir kills circle the led point.

Not ported or deviating:
- The scan is World's (`findTarget`, as AirToAir's retarget), not `4d8370`.
- World does not keep the carried target: the stage-0 offer runs in the tick
  the attack let it go, for the reason tickCombat let it go, so it always
  fails; a seek that starts off the map takes off and draws its angle when it
  first wakes on the map, where retail would offer the target first.
- The point's height is not kept (a point controller flies the cruise height).
- Before the attack's first wake World takes the target's own position as
  the point.
- (AirToGroundHover (`41d820`) pushes the seek too (`41e301`, `41e35f`):
  ported since protocol 307, with the mission, "Air traffic" below.)
- The group check (only its flag's rand(10) runs; AirToGround's is ported
  since protocol 312).

## Air traffic: flyers step out of a stack (icd + emulation, 2026-10-31, protocol 307)

A flyer's secondary occupancy list (unit +0x117 count, -1 when it overflowed,
+0x118 seven 16-bit ids: "Air occupancy" above, `RetailAirOccupant`) holds the
flyers whose footprint cells it shares. After the rebuild (`51da6a..51dbbe`)
every live, not dying flyer of a player of type 1 or 2 steps a signed byte at
unit +0x12a (`51dc4b..51dce4`): capped at 10, it rises by the list's count, by
14 when the list overflowed, or falls by 2 with no overlap; a negative result
is 0. Every flyer mission reads it at its wake and, at 5 or more (8 for a
flying builder at work), pushes a step-out ahead of itself (`4d7750`) with a
distance D in the step-out's +0x4e:

| mission | where | pushes | D (x16 px) | then |
|---|---|---|---|---|
| VTOL_Standby `417350` | every wake | VTOL_StepOutStandby | rand(15) + footX | sleep 1, stage 0 |
| VTOL_LandIfCan `416cd0` | after the +0x66 / 0x200 exits | VTOL_StepOut | rand(15) + footX | sleep 1, stage 0 |
| VTOL_Move_Formation `417750` | past stage 0 | VTOL_StepOut (StepOutStandby under 0x4000000) | rand(15) + footX | sleep 1, stage 0 |
| VTOL_Move `417c20` | past stage 0 | VTOL_StepOut | rand(15) + footX | sleep 1, stage 0 |
| VTOL_Follow `418860` | past stage 0 | VTOL_StepOut | rand(15) + footX | sleep 1, stage 0 |
| VTOL_SeekAttack `4192f0` | past stage 0 | VTOL_StepOut | rand(15) + footX | sleep 1, stage 0 |
| VTOL_SeekGuard `4197d0` | every wake | VTOL_StepOut | rand(15) + footX | sleep 1, stage 0 |
| VTOL_Patrol `419db0` | past stage 0 | VTOL_StepOut | rand(15) + footX | stage 1, sleep 1 |
| VTOL_Pickup `41a680`, VTOL_Unload `41ae20` | stage 1 | VTOL_StepOut | rand(15) + footX | stage 1, sleep 1 |
| AirToGround `41bf80` | stage 4 | VTOL_StepOut | rand(10) x 3 + footX | auto rescan first; stage 1, sleep 1 |
| AirToAir `41ca50` | stage 2 | VTOL_StepOut | rand(15) + both footX | auto rescan first; sleep 1, stage 0 |
| AirToGroundHover `41d820` | stage 4 | VTOL_StepOut | rand(10) x 3 + footX | below |
| AirStrike `41b5e0` | stage 4 | VTOL_StepOut | rand(10) x 3 + footX | auto rescan first; wait cleared, stage 1, sleep 1 |
| VTOL_MobileBuild `41ef00`, VTOL_HelpBuild `41f7f0`, VTOL_RepairUnit `420e60` | every wake, at 8 | VTOL_StepOutNoFormation | rand(15) + footX | sleep 1, StopBuilding (`51e4d0(8, 0)`), stage 0 |

These are all seventeen readers of +0x12a in the icd. "Auto rescan" is
`4d8370` (the idle scan, `412620` on the selected slot) and `4d7de0(unit,
found, 0, 0, 1, 0)` on rand(2) == 0 for an auto attack (+0x52): a new attack
put ahead ends the mission. The three step-outs share handler `4214b0` and
differ only in their registry flags (0x1000000, 0x5000000, 0). Off the map it
flies 800 px back toward the map (radius 128) and ends. Stage 0: eight octant
scores round the flyer's heading (ahead 100, 45 degrees off 0, abeam 100, 135
degrees 200, behind 300, each plus rand(80)), 100 more for every octant a
live listed flyer lies ahead of (`41d780`), the first lowest wins; the point is
D - rand(D/4) + rand(D/4) px out at a random angle within that octant, a point
controller (`4e40e0`, flags 0x30) of radius (rand(5) + 5) x 16, waiting on
0x700. Stage 1 (woken by the controller) ends it. The mission behind it runs
again from the state it set. To the Retail AI the step-out is the unit's
first mission, read from its registry row as for any other: class 14 with
the step-out's own flags (`0x5ebee0` / `0x5ebef9` / `0x5ebf12`), not a
move's `0x3000400`, so a wave does not take a stepping-out flyer for one
moving to a goal (`ClassicBrain`'s mission summary). That holds for a
flying builder stepping out of its job too: World keeps its site
(`buildSiteId`) while the step-out leads, and the summary reads the step-out
(class 14, flags 0), not a build. Our own AI levels (`waveFree`, `idle`, the
stalled-builder watch) and the Monarch's self-defence look past a leading
step-out to the order behind it, so a Drake stepping out of its hover is not
taken for a free fighter, nor an idle flyer stepping out for a busy one.

**AirToGroundHover** (`41d820`) is ported whole since then
(`retailAirToGroundHover`), where World had ported only stage 3's ring
point. Head: events 0x28, a lost target or a `dropped` weapon go to `41e28f`
(VTOL_SEEKATTACK as AirToAir's exit: no look at the unit's own state, the
attack last, not Hold Fire); off the map, 800 px back (0x700); 0x40 restarts
after a tick; the group check `51d1e0(unit, 1, 5)`: out of its formation,
once flagged (0x8000000), it may leave on rand(2) == 0 (return 8, 50c480 first
unless the next mission moves with the group), else it flags itself and pushes
VTOL_Move_Formation (+0x56 = -4); back in, rand(25) == 0 clears the flag.
Stage 0 takes off and zeroes +0x4e; stage 1 (the target's position in +0x22,
every slot automatic) installs a point controller of radius D + 144 on it, wait
0x768 and sleep 15, while the flyer is more than D + 160 whole pixels away;
stage 2 makes the selected slot manual on the target and reads its led
SweetSpot; stage 3 reads it again, draws the ring point (41de5d, as before),
installs the target controller there (`4e4140`: flags 0xa3, radius 4, the
attack altitude over the ground under the point: a fixed point that posts
0x100 within its radius and stays, headed at the target's SweetSpot) and
waits on 0xe70768 with a sleep of 15 + rand(15). Stage 4 (that wake: the
sleep, the controller, a shot fired (0x70000) or a shot's outcome
(0xe00000)): congested, it counts the step-out in +0x4e, rescans an auto
attack, pushes VTOL_StepOut (D = (rand(10) x 3 + footX) x 16) and ends the
attack past rand(5) + rand(5) + 1 step-outs, else goes back to stage 1 a tick
after the step-out; otherwise a wasted shot (0xe00000) on rand(2) == 0
against a target without a mover or one standing still (+0x130 & 0xc) sends
it to stage 5, a random point of the ring (rand(8) + D - 3 px at angle
rand(0x10000)), and anything else back to stage 3 after a tick.

**Shot outcome** (`4099e0`, from `529dc0` and `52a460`): the area pass sums
each recipient's damage callback (52a330's amount, or a status class's
answer) by the recipient's player against the shot's (+0xfd vs +0x92); the
direct hit takes its one recipient's, as a word. The shooter (+0x7c) gets
0x1000000 when the other side took more than twice what its own did, else
0x200000 << slot (the shot's +0xd8 & 0xc, the WeaponRecord's slot). Only
AirToGroundHover and the ground Attack_Chase (`403bb0`, mask 0x1f40068, not
ported) wait on these.

Ours: `retailairtraffic.h` (`retailAirTrafficStep`, `retailStepOutGoal`,
`retailVtolStepOut`, and `retailAirTrafficCheck`: the plain check of the ten
missions that share it, `RetailTrafficMission`), `retailAirToGroundHover` and
`retailAirStrike` in `retailairattack.h`, `retailVtolSeekGuard` in
`retailairseek.h`, and the checks in `retailAirToGround`, `retailAirToAir`
and `retailVtolSeekAttack`. VTOL_Standby, VTOL_LandIfCan, VTOL_Move(_Formation),
VTOL_Patrol, VTOL_Follow and the flying builders' jobs call
`retailAirTrafficCheck`; VTOL_Pickup's and VTOL_Unload's keep theirs inline
in their stage 1. `Unit::airTraffic` is stepped in
`World::updateAirOccupancy` after the insert pass and hashed. A step-out is
its own order at the front (`Order::stepOut`, D in `airRunCount`), pushed
after the pushing mission's dispatch (`World::pushStepOut`) and run at once
(`tickStepOutMission`), flown by `flyMissionGoal`; the mission behind it runs
again when it ends. While it leads, the attack behind it keeps firing its
manual slots at its target (`fireBehindStepOut`: the weapon update `52ae90`
fires whatever mission leads). The hover runs on the attack order
(`tickAirToGroundHover`, after the order's weapons fire) and flies with
`flyHoverGoal`. `applyHit` sums the damage per side and posts the outcome to
the shot's slot for the shots World resolves from a slot (instant and melee
shots, projectiles, flames, bombs).

**Fight-move legs, patrols and rally steps (protocol 307).** Retail has
no fight-move mission (none of the registry's 77 rows; its fight-move order,
the unbound `UnitCommand MoveFight`, is a Move_Ground / VTOL_Move / QMove with
response mode -1: "Mission target scan", 2026-11-03): a flyer's leg of one
is VTOL_Move (`Order::flightMoveMission` kept beside `attackMove`), the
move's mission with its air-traffic check, its coarse and refining
controllers and, for a Ctrl group in Retail mode, its group checks;
since fx-tsearch step 6 it carries MoveFight's response mode -1
(`World::markFightLeg`), so VTOL_Move's own scan puts its attacks ahead of
it, as Move_Ground's does on the ground. Every
flyer's patrol is VTOL_Patrol, armed or a builder (`plainFlightPatrol`;
until now only an unarmed flyer that builds nothing ran it): its stage-2
target scan and job search are World's acquisition and `tickPatrolRepair`. A
patrol from idle gives its return leg the anchor's flags (0x1000412) and the
outbound leg the row's flags marked 0x4000 (0x1004412), so VTOL_Patrol does
not append a second anchor and a Shift-patrol joins the loop at its tail, as
a ground patrol's does (before, an unarmed flyer lapped start, B, start, C).
E (`check_air_point_mission.py`, 2026-11-04): VTOL_PATROL's row (`0x5ebd50`)
is class 7, 0x1000412, which its ctor keeps with a point and no target;
stage 0 (`41a64f`, `4d6b80`) walks the list for a mission flagged 0x4000,
appends a VTOL_PATROL at the unit's position (0x1000412) only when none is,
and ORs 0x4000 into its own flags either way. Until 2026-11-04 the outbound
leg carried 0x4000 alone and a Shift-queued leg or a flyer output's rally
patrol step the move's 0x3000400, so a Ctrl group's patrolling flyers left
its moving aggregate on the way out (the group check reads 0x1000000 and
0x4000000; `retailMissionFlags`). Every flyer patrol leg now carries the
row's flags (`kVtolPatrolFlags`); `suppress_test` checks the legs and the
group's moving count. (VTOL_Patrol's own group checks, `419fa0` / `41a162`,
are still not ported: docs/pathfinding-port.md.) A flyer output's rally steps are
VTOL_Move and VTOL_Patrol too: GetBuilt resolves each QMove and QPatrol for
the output (`4de530` actions 2 and 9, `0x4022ec` / `0x402319`); they used to
fly with no mission. The group spread (`0x5214b0`, "Group move spread"
below) reads the resolved mission's flag 0x2 at +0x11, which VTOL_Move
carries, so a fight-move's flyers keep their offsets as on a move: the client
(`gameview_hud.cpp`), `OrderShape::FlyerOffsets` (now valid for `AttackMove`)
and the tools' mirror (`legion_issue_selection.h`). Before, every flyer of a
fight-move flew to the clicked point and piled onto it. Legion's flight
stations keep their lanes, as before.

**VTOL_SeekGuard** (`4197d0`, `retailVtolSeekGuard`), ported since then.
VTOL_Follow's lost-ward exit (`4191d7`): with its ward gone, dead or dying
(or events 0x208), the guard, when it is the last mission (+0x66 is 0),
inserts VTOL_SEEKGUARD (`4d77b0`) carrying its point (+0x22, the ward's
position read at each look past the head, `418b72`) and the ward when alive
and not dying, and ends. The seek: event 0x200 ends it; off the map, 800 px
back; the air-traffic check at every wake, stage 0 included; the group check
`51d1e0(unit, 1, 4)` and its flag's rand(10); stage 0 takes the flyer's own
position for a zero point and draws the orbit angle rand(0x10000); stage 1
looks for allied units that cannot fly within its sightdistance (`509ac0`
with the filter `421910`: alive, not dying, allied to its owner, not a flyer,
not itself; structures included) and offers the first a guard (`4de530`
action 7: VTOL_FOLLOW for a type with `canguard`, nothing otherwise): a guard
goes ahead (the seek sleeps 15 + rand(30) behind it), no guard ends the seek
(8). Found nothing: woken by its controller the angle steps back 0x4000 +
rand(0x2000); a point controller (radius 128) at the point less (sin,
cos)(angle) x (the selected weapon's range + 160 when armed, else 160), a
sleep of 7 + rand(7) and a wait on 0x718. World inserted nothing: the guard
just ended and the flyer stood down. Ours: `World::seekLostWard` (from
`tickCombat`'s guard), `tickSeekGuard`, `Order::seekGuard`; the guard keeps
the ward's position in `attackPointX/Z` at each look (`tickFlyingGuard`).

**The flying builders** (VTOL_MobileBuild `41ef00`, VTOL_HelpBuild `41f7f0`,
VTOL_RepairUnit `420e60`) check at every wake, at 8, before their stages:
VTOL_StepOutNoFormation goes ahead, a tick's sleep, StopBuilding
(`51e4d0(8, 0)`: the StopBuilding script when the build bit was set) and
stage 0, which takes off again and approaches to 2 x builddistance; a
MobileBuild whose site already stands turns into VTOL_HelpBuild at its stage
2 (`41f2d6`). Their wakes: the first dispatch, the approach's controller
events (no timer), and every tick of the work (each work stage sleeps one).
World runs these jobs as an approach and a work stage, not as stages, so ours
(`World::flyingJobTraffic`) runs the check at the matching ticks:
`prepareBuildApproach`'s stage 0 and its arrival (a MobileBuild before its
site stands), `tickConstruction`'s first approach tick and every work tick
(a flyer's help or build on a standing site), `tickFlyingRepair`'s approach
install and every work tick. A help or build restarts from a help leg on the
site (`restartFlyingHelp`: the approach goal at 2 x builddistance); a repair
from its approach. A job waits while the step-out leads (its mission is not
the head). The Zhon conjurers' production is BuildingBuild in retail, which
has no check.

**AirStrike** (`41b5e0`, `retailAirStrike`), ported since then: the attack of
a flyer whose selected weapon has the `dropped` key (WeaponType +0xc8 bit
0x20; mods only, no shipped weapon sets it) on a target that cannot fly,
before the hover and the attack runs (`4de530`'s order); World flew
AirToGround. Head: no weapon, 7; events 0x2a, a dead, dying, carried or
off-map target, or a selected weapon without `dropped`: the lost-target exit
(`41be1b`: VTOL_SEEKATTACK inserted at the point when the flyer is alive and
not dying, the strike is last and not on Hold Fire, carrying the target when
alive and not dying; 5); a strike without a target whose flags keep 0x200:
the seek at the flyer's own position when last, 5; event 0x40: a tick's
sleep, stage 0; the target's position becomes the point; a leash (+0x56)
ends it once the flyer's whole-pixel distance from its origin reaches it;
the group check (`51d1e0(unit, 1, 5)`) and its flag's rand(25). Stage 0
takes off; stage 1 makes every slot automatic and the selected one manual
and, within 480 px of the point (its 16.16 distance through the x87 and
`_ftol`), installs a pass 2240 px from the flyer through the point (radius
960, wait 0x742); stage 2 a point half the distance off at rand(0x4000) -
0x2000 round the bearing (radius 480, wait 0x768); stage 3 goes on; stage 4 is the air-traffic check (table), else on a
controller event stage 5, else the release ring: trunc(sqrt(2 cruisealt /
(gravity x 2^-16)) x 30 x the navigator's +0x22) + attackrunlength + 1 px,
round the target (`4e3f70`, the point controller following it) or the point,
a tick's sleep and a wait on 0x768; stage 5 aims the selected slot at the
point (`51a770`) and flies attackrunlength + 960 px through it (radius 960);
stage 6 clears the slot (`51a7f0`) and flies 1440 px ahead on its heading
(radius 128), then stage 3. The navigator's +0x22 is the high word of its
destination's z step (+0x18 is the step, `524af0`): what the binary reads.
Ours: `World::tickAirStrike` from `tickCombat`.

Checked:
- `tools/re/check_air_traffic.py` runs the counter step natively for all
  65,536 counter/overlap pairs (and its three gates), `4214b0` on 3,000
  random states (429 off the map, 1,604 at stage 0, 655 at stage 1, 312
  past it; up to seven listed flyers, live and dead) and `41d820` on 8,000
  (1,185 off the map, 534 / 1,099 / 518 / 1,058 / 2,033 / 1,070 at stages
  0-5, 503 past them; 1,549 lost targets, 186 leaves, 609 formation pushes,
  1,139 approaches, 1,358 ring points, 173 rescans, 728 step-outs, 472
  give-ups), against `retail_motion_test --air-traffic`, `--step-out` and
  `--air-hover`: every result, stage, wait mask, deadline, pending and flag
  word, count, point, controller, radius, slot call, push, D, random draw and
  generator state. `check_air_attack_run.py`, `check_air_to_air.py` and
  `check_air_seek_attack.py` now run with the counter below, at and above 5
  (AirToGround: 90 step-outs in 6,000 calls; AirToAir: 2,188 in 12,000;
  VTOL_SeekAttack: 947 in 6,000), and the last also runs AirToGroundHover's
  lost-target exit (1,932 of its 6,000 exits).
- The same script runs `retailAirTrafficCheck` against each of its ten
  missions' own handlers (`--traffic-check`): 16,000 calls (1,600 each, the
  counter at -4..13 and the byte's extremes, every stage, the push built or
  not), each handler called past its head's exits and stopped where it goes
  on without a step-out: every result, stage, wait mask, deadline, flag word,
  step-out name, D, push, StopBuilding, draw and generator state agree
  (VTOL_SeekGuard's check runs at every wake, stage 0 included, where
  `retailairtraffic.h` had it past stage 0). `4197d0` on 8,000 random states (1,182 off the map,
  1,968 / 3,851 at stages 0 / 1, 999 past them; 1,931 orbit points, 1,606
  searches, 603 guards offered, 441 put ahead, 2,658 step-outs, 162 ended)
  against `--air-seek-guard`, and `41b5e0` on 12,000 (every stage; 2,037 point
  and 245 following controllers, 560 aims, 541 step-outs, 2,992 seeks)
  against `--air-strike`, with the x87 release ring and `_ftol` native. Five
  planted bugs fail them: the release ring's 30 as 31, the strike's check at
  6, its half distance as a shift, the seek's check past stage 0 only, its
  sleep one tick longer.
- `air_traffic_test` (data, both modes, the air-stack-*.scn fixtures, the
  target topped up): the airborne Drake-ticks near the fight that share
  footprint cells with another flyer, per mille. On protocol 306: 992 (five
  Drakes on an Aramon Barracks), 980 (two groups of five), 282 (three Drakes
  on a patrolling Spyhawk), 277 (six Drakes moved to one point). Now 44, 58,
  86 and 141, the same in both modes; the Drakes still hurt the target
  (36,811 -> 34,837 and 62,254 -> 57,146 hit points on the Barracks, 5,605 ->
  9,251 on the Spyhawk). The test fails on 306 (eight of its checks).
- The rest of the port adds `air_traffic_test` cases (new fixtures, both
  modes alike), 20 of whose checks fail on its parent: six Drakes
  fight-moved to one point (436 -> 141 per mille, as the move), five
  fight-moved from one cell with a click each (298 -> 44; no step-out -> 10),
  five armed Drakes on one patrol (245 -> 49; none -> 19), three Flying
  Builders repairing one Keep (361 -> 78; none -> 14; the Keep still mended)
  and building one (99 -> 71; none -> 7; still built), three Drakes whose
  guarded Goblin dies inside a ring of Goblins (all three stood down; each
  now guards one of the ring) and the five Drakes on the Barracks given a
  `dropped` weapon (no pass -> 11 passes turned). The duel's Spyhawk no
  longer appends a second anchor to its patrol, so that fight moves: 86 ->
  75 per mille, 7,461 hit points left.
- `retail_motion_test`'s stacked landing (twelve flyers sent to land on one
  spot) now lands them apart in Retail too: each steps out of the stack from
  VTOL_Standby or VTOL_LandIfCan before it searches for a spot (the landing
  test itself, `509400`, still lets a lone flyer touch down on a ground unit
  at an unexplored site).

Not ported or still deviating:
- The group check (`51d1e0`) is ported for AirToGroundHover (with
  VTOL_Move's) and, since protocol 312, AirToGround, AirStrike and SUPPRESS
  ("Flyers at a point" under the SUPPRESS port); not for AirToAir,
  VTOL_SeekAttack or VTOL_SeekGuard: those fly as if in formation (only the
  flag's clearing draw runs, rand(10) for the seek).
- The flying builders' jobs keep World's approach and work stage: the check
  runs at the ticks that are retail's wakes (above), and the job restarts
  from World's approach (a help leg accepting inside 2 x builddistance, or
  the repair's own approach) rather than from a retail stage 0.
- VTOL_SeekGuard's search takes the lowest unit id within a 128-px sector:
  retail walks each sector's own unit list, whose order World does not keep.
- AirStrike: World's attack always has a unit, so its lost-target exits are
  `tickCombat`'s (`seekLostTarget`) before the dispatch; stage 5 aims the slot
  at the target unit where `51a770` aims at the point (the target's position
  at that wake). AirToAir still takes a `dropped` weapon against a flyer
  (above).
- A step-out runs its first wake as soon as it is pushed, as retail's
  dispatcher runs a new head at once. One that ends hands over in the same
  tick to the missions World dispatches after it (VTOL_Move, VTOL_Patrol, the
  attacks), as retail does, but to VTOL_Standby and VTOL_LandIfCan, which
  World dispatches before it, a tick later.
- Shots World ends without an impact (an arrow landing with no
  areaofeffect), storms, pending area effects and death blasts post no
  outcome.
- Legion's flight stations (Legion's design, not retail: each flyer of a
  Legion move holds its own lane over the ground) take the place of the
  check on VTOL_Move and VTOL_Patrol: a flyer holding a station does not
  step out of its lane (that broke `legion_mixedformation` and
  `legion_flyerfront`). Its standby and landing still step out, so a Legion
  formation's flyers take longer to land (`legion_flyerfront` waits 900
  ticks for the last landing instead of 150).

## SweetSpot is the drawn piece bounds (icd + live capture, 2026-10-24, protocol 295)

`SweetSpot` (`4dd4f0` -> `4dd2a0`) names a piece through the COB and returns
the centre of that piece's vertex bounds, the extrema starting at zero, plus
the unit's position (+0x68/+0x6c/+0x70). The vertices are not the model's: they
are the unit's own buffer at `[unit+0xc0]+0x1cc+piece*0x38+0x24`, set up at
creation as a copy of the definition (`4ee7a0`). The draw path rewrites that
buffer in place: "draw unit" `4ee700` -> `4ee620`, which copies the definition
back in (`4ee920`) and transforms it (`4eea20`) by the piece chain plus the
body's pitch, heading and roll, unit-relative (position zero, no final Z flip).
`QueryBlood` (`4dd590` -> `4ee620`) refreshes it too, and the draw counter at
node +0x28 counts passes. So a unit that has been drawn aims at the centre of
its drawn piece; one never drawn since it was created or loaded aims at the
authored (local) bounds.

Evidence from the live capture `0d8e.../next-device-state-01.json` (tick
11134), with `4dd4f0` emulated on the captured memory:
- 26 drawn Hunters (counter above zero, buffer unlike the definition) have a
  SweetSpot 18.99 px up, e.g. (1.54, 18.99, -1.55) at retail heading 8192 and
  (-1.51, 18.99, 1.56) at 42005. The undrawn Hunters 49 and 70 give
  (-0.03, -1.24, 1.24): the local torso bounds, below the feet.
- `tools/re/check_captured_aim.py` now tells the two apart. Drawn Hunters 17,
  44, 53 and 117 match `retailPieceDrawnCenter` on all 31 pieces to at most
  357 raw units (0.005 px; retail transforms in x87 floats, the port in
  integers), most to 1 raw unit. The undrawn Thirsha 308 (the old PASS, all
  47 pieces) and Hunter 49 still match the local bounds exactly. The reload
  captures have every model dirty and unposed, so the earlier check (local
  vertices) only ever covered undrawn units.

A lockstep simulation cannot depend on what one machine drew, so World always
uses the drawn geometry from its own current pose (`retailPieceDrawnCenter`,
`queryUnitScriptPoint`): retail for any unit that has been on screen. Every
SweetSpot caller follows: the lead point (`retailLeadPoint`), the ballistic
pitch, the facing request, the hover ring, the clear-shot chord and guided
homing. The client's blood spray homes to the same point. By inference, two
retail Hunters that were never drawn would miss each other the way World's did.

Before protocol 295 World used the local bounds for every unit. A Hunter's aim
point sat 1.24 px under its feet, so spears fell short into the ground: in a
0.9.0 all-Hunter game 727 Hunter-on-Hunter shots hit 0 times (4.2% over all
1175). With the drawn bounds the same replay hits 89%. Of 202 unit types, 136
have a different SweetSpot height at rest and 115 sit more than 10 px higher
(both balances), e.g. zonter -1.24 -> 18.99, zonhunt 6.13 -> 25.94, verball
0.08 -> 37.00. The Gryphon's (a root piece whose bounds already span zero)
keeps its height. Pieces now turn with the body, so an off-centre SweetSpot
such as the Creon Submersible's 20.3 px rotates with its heading.

Checked: `sweetspot_test` (both balances) asserts the four captured Hunter
offsets to 0.03 px and that a Hunter hits a standing Hunter in the 0.9.0
geometry (dx -272, dz +192): 0 of 16 impacts before, 17 of 17 after.

**Lightning at the Creon Submersible.** In a 0.9.0 game a Thirsha's lightning
hit two parked subs 0 times in 47 bolts. The sub's SweetSpot (its `body` piece)
lies about 20.3 px from its centre, past its 2x2 footprint cells, and
`52a4d0` only tests the quad of the unit that owns the bolt's cell (the port's
collision matched retail on all 594 sub-steps of those bolts). With the drawn
bounds the offset turns with the heading (at heading 4616 it is (-10.16, 9.87,
19.61), at 14493 (-20.50, 9.87, 3.50)), so the result depends on where the
bow points:
- `52a4d0` emulated on the game's own 47 bolts (same muzzles, drawn aim point
  per recorded heading): sub 106 (heading 4616) 0 of 20, sub 328 17 of 27
  (headings 14493 7/7, 12198 1/1, 49448 9/18, 56994 0/1); with the old local
  aim, 0 of 47.
- Native sweep over 16 headings x 16 approach directions at the Thirsha's
  150/150 hover (156 px out, muzzle 203): 31% hits with the local aim point,
  28% with the drawn one on a cell-aligned sub; 300 px out it rises from 34%
  to 62%.
- The simulation scene (Thirsha attacks a parked sub, 16 headings x 8
  approaches): 32% -> 28% cell-aligned. In the replay setups it hits sub 328
  at headings 14493, 12198 and 49448 (12/14, 12/14, 10/16) and misses sub 106
  and heading 56994, as retail does.

So a hovering Thirsha often misses a parked Submersible in retail too; this is
retail behaviour, kept. `sweetspot_test` asserts both replay setups (sub 328
hit, sub 106 missed); the 328 case fails on the local aim point.

## Map feature install and area clear (icd, 2026-10-24, protocol 294)

**Overlapping TNT features.** The loader (`0x50f6c0..0x50f948`) initialises
every 14-byte cell (+8 = 0xffff, +7 = 0xa), installs 0xfffc blockers, then
calls `0x495360(cell, index, ..., 0xa)` for every TNT feature in row-major
order. `0x495360` first removes whatever the new footprint covers through
`0x496380` (anchor and 0xfffe tails cleared, any object torn down and its pool
slot freed, a sacred record at the same spot deactivated); an indestructible
(0x100) or marker cell refuses the install, keeping removals already made. The
temporary TNT buffer is then freed, so a removed feature exists nowhere: the
draw scan (`0x4fc8d9..0x4fc963`), movement grades and the area selector all read
the installed plane. Emulating the native install loop and draw scan over 1,360
maps matched `retailMapInstall`'s plane exactly (16 cells on 5 maps differ, all
TNT names without a TDF); 481 maps lose 18,660 features, including 88 Sacred
Stones and 345 mana features. We now register only the survivors
(`World::installedMapFeatures`, `retailInstalledMapFeatures`) in matchsetup and
the client.

**Cell +7 is the feature owner.** `0x495360` writes its fifth argument at the
anchor only, on success. Every map-feature caller passes 0xa (no player); the
corpse spawn `0x512ee0` passes the dying unit's owner. The area selector
(`0x509d9f..0x509e6d`) accepts an unseen cell when +7 == 0xa and the Mapping
plane (`game+0x19ef4`) has the player's bit; `0x4c6540` fills that plane with
0xffff when MappingOn is off. Emulated over 4,000 cases with the real
`0x50e660` and `0x4223f0`. The port reads the builder owner's bit of our hashed
`navigationExplored_` (retail reads the local human's, which is not
deterministic).

**Exits** (`0x406670` RECLAIMAREA, `0x406250` RECLAIM; static reading). The
area approach is a radius-4 circle (`0x4d4da0`) waited on mask 0x700, so
arrival (0x500) and failure (0x200) both lead to the next pick. 0x200 is raised
only at `0x4e4ec4`: a zero-point route delivery whose controller rejects the
unit. A pick in the same `0x50e660` cell as the last target retires the order
(8); RECLAIM returns 8 on 0x200 in stage 1. The reclaim child itself is
below. Details: [guessed-area-clear-2026-09-26.md](guessed-area-clear-2026-09-26.md).

## Reclaim (icd `0x406250`, emulated 2026-10-24)

RECLAIM is one mission for every reclaim in the sim: a click on a feature or a
corpse, an area-clear child, and the clearing reclaims a build queues ahead of
itself. Retail has no unit reclaim (the mission table beside the Reclaim and
ReclaimArea handlers, `0x5eba01`/`0x5eba1a`, has none), and the AI issues none.
VTOL_RECLAIM (`0x420xxx`) is not ported; flying reclaimers keep our old
centre-distance reach.

**Reach.** Stage 0 resolves the mission point's cell (`0x4930d0`, tails walked
back through +0xb/+0xa to the anchor) and the footprint from the feature def
(+0xb0). `0x4d4e80` then installs a `0x4e3590` rectangle controller with
minX = ax − bw, maxX = ax + fw, minZ = az − bh, maxZ = az + fh, for builder
footprint (bw, bh) at unit+0x78/+0x7a; wait mask 0x700. Its acceptance
(`0x4e24a0` -> `0x4e39e0`) takes only footprint-origin cells on that
perimeter: the builder's footprint touches the target at an edge or a corner,
with no gap and no overlap (a builder standing on a non-blocking feature is
steered out). Builddistance (type+0x230), Y and height are never read. The
steering cell is `0x4e38e0`, our `RetailRectGoal::navigationCell`, the
controller MobileBuild already used. Stage 1 starts work on 0x100/0x400 and
ends silently on 0x200, with no builddistance fallback. After that no distance
is ever tested: a builder pushed 40 cells away kept working. A corpse is a
feature, reached through its installed corpse footprint. Emulated: 19,044
origin cells over all 144 combinations of 1..4 x 1..4 builders and 1..3 x 1..3
features, random clicked cell and builddistance 0..500, all on the perimeter
rule; `tools/re/check_reclaim_reach.py` re-checks a sample against the port.

**Time and payment.** On arrival the countdown at mission+0x4e is
`ftol(type+0x128 + 15.0f)` (energy as a float). Stage 2 waits for COB
INBUILDSTANCE (after a FacingNoFormation turn child unless turninplacerate is
0). The work stages 3 and 4 (`0x406529`) wake every 2 ticks (mask 2|1) and
subtract 2; a worker particle while more than 15 remain; at <= 0 stage 4 runs
once more and stage 5 removes the target. So the order ends
2·ceil(C/2) + 2 ticks after arrival: 18 for energy 0 (rocks, every shipped
corpse), 92 for 75, 268 for a 250-energy tree, 542 for 525. Workertime,
builddistance, metal and damage play no part, and every builder keeps its own
count: the first to finish removes the feature, the rest quit on their next
wake. Each wake re-checks the target (`0x496ea0`: reclaimable flag and the
object's +0x5d bit 0x10, cleared by a fade or a fire), and holds a corpse's
decay (object+0x3c = decomposetime·30). `0x494fd0` starts a fade (`0x494ce0`,
alpha 1.0 less 0.03 a tick: 34 ticks) and removes the feature with no
featuredead replacement; it stays in the plane, blocking but no longer a
target, until the fade ends. **Reclaim pays no mana**: a write watch on the
player and its economy saw nothing, the only other reader of type+0x128 is the
VTOL countdown, and the economy loop has no reclaim term. Event 0x2 is the
cancel (the order destructor), not a speed-up.

Port: `sim/retailreclaim.h` (rectangle, countdown, work wake) driven by
`World::prepareReclaimApproach` (stage 0, on the leg's `buildRectangle`) and
`World::tickReclaim`; `Feature::fadeLeft` / `Unit::corpseFading` hold the fade.
Ours, not retail: a MobileBuild retry treats a fading feature on its site like
a mobile unit (the engine's own placement-clearing queues reclaims ahead of a
build); a fading feature or corpse takes no weapon damage and cannot catch
fire (otherwise a hit could swap it into a stage that outlives the fade); a
reclaimed corpse's fade is not drawn (it simply goes at its end). Not emulated: the turn
child and the COB time to INBUILDSTANCE (both add to the table above), what
else raises 0x400, and retail's per-wake lookup by cell (a new feature on the
same cell would be carried on with; ours follows the feature id).

**How the fade is drawn** (static analysis, 2026-11-03). Reclaim is the only
fade: `494ce0` is called with mode 1 only from `494fd0` (the reclaim removal)
and from net event 3 (`4e9c2c`); damage (`49630a`, `496339`) and net event 1
(`4e9c60`) pass mode 0. Mode 1 takes a feature object slot (0x60 bytes at
gameState+0x19e74, 0x800 of them; with none free it calls `494f00` and the
feature goes at once) and sets +0x2e = 1, alpha +0x30 = 1.0f. The object tick
`4959c0` subtracts 0.03f (`5f03cc`) at `495b3c` and at <= 0 calls `494f00`
(`495b6f`): 34 steps, the last drawn alpha 0.0100004f. The feature draw
`4fd430` (Glide is driven through the renderer vtable `5f67c0`) takes, for a
sprite feature (type+0x13c bit 0) whose object fades, the current frame
(`536400`, animating) or seqname frame 0 (`5367a0`), copies it, sets every
texel that is not the frame's transparent index to 0xff (`4fd5fe`), makes a
paletted texture of it (vtable +0x7c, `5b8400`), and draws one quad with
vertex colour `(ftol(alpha * 255.0f) << 24) | 0xffffff` (`4fd688`,
255.0f at `5ee95c`) under renderer state 6 (`5b77b0`: blending from the
vertex alpha; `5b7f70` sets render states 0x13 = 5 and 0x14 = 6, source
alpha / inverse source alpha in Direct3D's numbering -- inferred from the
numbers) and state 8 (render state 0x29 = 1, colour key, with the
transparent index set through vtable +0x58). Palette index
255 is white in every shipped palette (`palettes/*.pal`, `*.pcx`, but for
`veruna_newpalette`), so a reclaimed tree or rock turns into a white
silhouette that fades out over 34 ticks. That path jumps past the shadow,
flame and smoke draws (`4fd7c7` -> `4fda73`). TDF `seqnamereclamate` is not a
string in the icd: retail never reads it. Port: `client/reclaimfade.h`, drawn
in the feature body pass from `Feature::fadeLeft` (display only). Area reclaim
and the placement clearing both reclaim, so they fade the same way.

Not a fade: a feature DESTROYED by damage (`494ce0` mode 0) with a
`seqnamedie` plays that animation (object +0x4, its shadow `seqnamedieshad`
at +0x10) and at its end (`495ef7`) calls `494f00` with 0, which places the
`featuredead` stage (type+0x12c, `494f50`); without a `seqnamedie` it swaps at
once (`494ee3`). A burn plays seqnameburn and swaps through `495300`. 3DO
features (statues, corpses) hand +0x2e and +0x30 to the model draw
(`4fd850`/`4fd85c` -> `4ee700`; `4ec9ba` picks draw mode 2 when +0x13 is
set); ours does not draw that one yet.

**The death playback** (static analysis, 2026-11-03; established unless
marked). The loader resolves `seqnamedie` (string `611520`, read at `493ed3`)
and `seqnamedieshad` (`611510`, `493f23`) in the feature's own `filename` GAF
through `493700`, as it does `seqnameburn`, and forces both nonlooping
(`493eff`, `493f4f`: sequence byte +2 = 0); they sit at type+0xe8 and +0xec.
`494ce0` mode 0 takes that path only for a sprite feature (type+0x13c bit 0;
a 3DO one swaps at once, `494e00`) whose cell has no object yet (`494d78`:
cell+0xd bit 8 -- a burning, fading or already dying feature ignores it), and
only when an object slot is free (`4949c0` < 0x800; with none it returns and
the feature stays, not ported: ours has no slot limit). The slot gets the
feature type (+0x2c), the cell (+0x28/+0x2a), the clock `537390(+0x4, die, 0)`,
the shadow clock at +0x10 only with a `seqnamedieshad` (+0x5d bit 4), and the
cell gets bit 8 and the slot index in its word +0xa (the damage counter until
then). The cell keeps the feature (word +8): it still blocks and still owns
its footprint. Each tick `4959c0` steps the clock (`495edf` -> `5373d0`: a
frame is held for max(1, delay) ticks, its duration the low word at +0x2c of
the frame entry, `5367e0`) and the tick the last frame's hold runs out the
clock's sequence pointer is cleared and `495ef7` calls `494f00(x, z, 0)`:
`496380` removes the feature and `495360` places `featuredead` (none when the
object's burn bit +0x5d&2 is set, which mode 0 clears). The same clock times a
burn-out, so ours counts the death like `Feature::burnLeft`: `dieLeft` starts
at the sequence's length and the feature is replaced on the tick it would
reach 0. While it dies:
- **No damage.** `4961a0` skips a sprite feature whose cell has an object
  (`4962b6` -> `4962bc`), fire or not: a dying -- and a burning -- feature
  takes no weapon damage and is not lit (that path is also how a firestarter
  ignites, `4962ac`).
- **No fire.** StartBurning `494b40` returns for a cell with an object
  (`494b6d`), and BurnSpread `495110` passes such a cell over before its
  rand(100) roll (`49515d`, and `49525c` for the downwind sparks).
- **Not a reclaim target.** `496ea0` wants the type's reclaimable flag (0x40)
  and, with an object, that object's +0x5d bit 0x10, which `4949c0` clears and
  mode 0 never sets. A reclaimer already on it quits on its next wake.
- **Drawn** (`4fd430`): with an object the body is the clock's current frame
  (`4fd821` -> `536400`) and the shadow only the `seqnamedieshad` clock's
  (`4fd7cc`); no shipped feature has one, so a dying feature casts no shadow.

Shipped data (the extracted feature TDFs and GAFs): 382 feature sections
carry a non-empty `seqnamedie`, every one with a `featuredead`, all sprites,
none with a `seqnamedieshad`: every tree (Aramon, Creon, Taros, Veruna, Zhon)
and its dead stage, and buildings, huts, walls, towers, crops, fences,
inventions and other props. Rocks have none (`zhon/zonrock.tdf`), so they go
at once. Lengths: 298 are 10 frames of 2 ticks (20 ticks, the trees), 55 are
3 frames (6), the rest 4 to 38 ticks, and one 10 frames of 10 (100 ticks).
So an Aramon tree hit for its 1000 falls for 20 ticks, becomes AraTree01a, and
a further 600 plays AraTreek01a (20) and leaves AraSmudge01.

Port: `World::destroyFeature`/`tickFeatureDeaths` and `Feature::dieLeft`
(hashed) in the sim, `FeatType::dieTicks` from
`gaf::FeatureBurnTiming::dieDuration` (also in the gameplay-data hash), and
`client/featuredie.h` for the drawn frame; `tools/featdie_test.cpp`. Inferred,
not checked: the order of a burn-out, a death's end and a fade in one tick
(retail walks one object list; ours takes burns, then deaths, then fades).

## A factory's output: rally, else PARK (icd `0x402220`, emulated 2026-10-25)

GetBuilt's completion branch (`0x402243..0x4023ef`) releases a finished unit
when the unit and its builder both carry `+0x130` bit 0x1000000 without 0x1000,
the unit has a mover (+8) and the builder is a structure builder (bit
0x2000000). It walks the factory's mission list (+0x60) and copies every
`QMove` and `QPatrol` entry onto the new unit (`0x4de530`, then `0x4d78a0`);
nothing else is copied. An AI owner (`[+0xb8]` set, +0xea 1) then gets
`0x50b4f0`, the AI unit assignment. Only when nothing was copied does it add
`PARK` (`0x604ae8`, definition 13: handler `0x407ca0`, class 14) with the
factory as target, permanent 1 and padding `(rand(6)+rand(3)+3)*16`, the 3
being 6 for a type with flag 0x800 at +0x260 (canfly). Emulated with the
callees hooked: rally kinds copy and suppress PARK, other kinds do not, the AI
call happens either way, and all 36 padding draws match. PARK's ring (already
ported in `retailpark.h`, 8,192 cases in `check_ground_park.py`, re-run against
this change) is footX*16 + padding + half the factory's footprint diagonal,
out to three times that, around the factory; it ends on arrival (0x100) or
when a next order is ready, and re-rolls on failure without limit.

A mobile builder's output never reaches this branch, and a mobile builder has
no rally to copy. MobileBuild has its own release (icd, emulated): on a repeat
(quantity 10000000, the test at `0x405c12`) the block at `0x405c1f` gives the
output `PARK` with no target, permanent 0 and padding
`(rand(3)+rand(3)+rand(3)+1)*16`, the 1 being 5 for a canfly builder. A limited
quantity gives it nothing.

Port (protocol 295): `World::parkProductionOutput` for every output of a
structure, flyers included (flight branch below). Mobile producers keep the
exit step, which is ours, for an output with no rally; MobileBuild's PARK is
not ported. Since protocol 309 a rally on a mobile producer, also ours, starts
straight away with no exit step ahead of it, as a factory's always has. An
output with nothing else to do still takes the step, and that includes an output
every step of whose rally dropped: a dead target, or one it has no weapon for,
which is tested at the hand-over (a leg the first tick would drop would leave it
standing where it was made). A structure output keeps the plan in its own rally.
Ours, not retail: retail tests no reachability, so a Navy Yard on a pond could
park its ship in the next body of water, and a permanent PARK that cannot be
reached never ends, holding the pad. The ring is kept when any cell of it is
connected to the birth point (component roots for the unit's movement class);
otherwise the output moves to the farthest free connected cell clear of the
factory within the ring's outer radius. Our rally also carries attack and
fight-move orders; when every entry drops (its attack targets are dead), the
output parks, as retail's does when nothing was copied. Under Legion a ground
output's PARK is spread over the band instead (see "Legion: a spot of its own"
below); a Legion PARK still held after that is completed once clear of the
factory, as its exit step was. Our AI counts a unit holding only PARK as
idle (retail's AI takes it through `0x50b4f0`). Tests:
`tools/shipyard_park_test.cpp` (the reported pond, a Barracks, a rally point,
a rally of only a dead target),
and an emulation probe of `0x402220` kept outside the repo (`~/tak-tmp/scratch/fix-091/shipyard-fix/probe_getbuilt_release.py`).

### Which orders a producer keeps (retail: QMove/QPatrol only)

Retail's mission-definition table (the name/code pairs from `0x5eb605`) holds
exactly two factory-side kinds, `QMove` and `QPatrol`, both under
`UNITMISSIONCODE_READYORDERS`; there is no queued attack or fight kind.
The order code (`0x5214b0`) resolves each selected unit's mission through
`0x4de530` and adds it with `0x4d7a30`; `0x521c08..0x521c5b` compares the
resolved definition against `Move_Ground`, `VTOL_Move` and `QMove`, so a Move
on a structure builder resolves to `QMove`. GetBuilt copies only those two
kinds onto the output (above), so a retail factory's rally is moves and
patrols, Shift-queued like any mission list.

Ours, not retail (user request, 2026-10-26): `World::setRally` also takes
**Attack** (a target id; an output that cannot attack, or a dead target,
drops the step) and **fight-move**, from the HUD's ATTACK button, the attack
hotkey or a right-click on an enemy. A **Shift-queued patrol** rally step is
stored open (`Order::patrolOpen`) and copied that way, so an output closes
the loop from where it starts that leg, as a unit's own queued patrol does
("move A, Shift-patrol B" patrols A <-> B). **Stop** clears the rally with
the queue. The rally plan is folded into `World::stateHash` when set. A
mobile builder repeating production (retail has no rally there and gives its
output a PARK of its own, above) holds the same rally plan: while `repeatType` is set, Move, Patrol, Attack and
fight-move all set the rally and leave the builder producing in place
(`applyCommand` rejects its other orders); with finite or no production they
command the builder itself. Ending the repeat (Stop, the +++ toggle, or
subtracting the last copy) clears a mobile builder's rally, so a later limited
queue never inherits a plan the player can no longer see. Tests: `production_test` (factory and mobile
plans, attack-then-move, replace/Stop/hash) and `conjure_test` (the shipped
Zhon builders through the command path).

**Armed ATTACK at a point on a producer (checked 2026-11-04, static only).**
Retail has no Attack rally, and a factory gets no order at all there:

* S: at type load `canattack` (UnitDef `+0x264` bit 0x20) is set from the
  first weapon slot (`+0x1aa != 0`, `4c1672..4c1698`), not from the FBI's
  `canattack`. `+0x260` bit 0x10000 is set when a `WEAPON%i` section loads
  (`4c15b2..4c1609`), and `4c18d7..4c18ff` then clears `canattack` for a type
  with a weapon, `bmcode` 0 (`+0x24a`, a building) and a canbuild menu
  (`+0x12e > 0`); the reload path `4c1f6c..4c1f7c` does the same, and
  `canguard` (bit 0x40) is cleared alike (`4c1905..4c1925`). So no
  unit-producing structure has `canattack`, armed or not (the Barracks'
  `canattack=1` has no effect), and no shipped structure with a canbuild list
  has a `[WEAPON1]` block (merged units and unitscb).
* S: the ATTACK button is enabled only when a selected unit has `canattack`
  (`4b0429..4b0438` sets the summary's `+0x1d`, `4b0597..4b05b6` picks state 0
  or 2), so with only factories selected it is disabled (I: it cannot be
  armed). The cursor selector returns Normal without `canattack`
  (`4dd8ac..4dd8b6 -> 4de4e0`, 0x13), and an armed click under Normal issues
  nothing and stays armed (`527dd0`); neither the aggregator (`521cd0`) nor the
  selector tests repeat production.
* S: the mode-3 resolver (`4de623`) returns no mission without `canattack`
  (`4de62d -> 4de6ed`); its point branch (`4de65f..4de7e3`) has no structure,
  producer or queued kind (mode 2 gives QMOVE to a unit with no mover,
  `4df0cb..4df0e9`, and mode 9 QPATROL, `4deb8a..4deba8`; mode 3 has nothing
  alike), and GetBuilt copies only QMove and QPatrol (above). An Attack at a
  point never becomes a retail rally leg.
* S: the same `canattack` test is the first of mode 3 for a unit target too
  (`4de623..4de62d`, before the target branch `4de7e6`), and every attack
  World gives goes through mode 3: a click or `Cmd::Attack` (`521a79`,
  `4d7a30`), a mission's `a NAME` (`513ec0`) and an automatic attack from a
  scan or a hit (`4d7de0`, `4d7e46`). So a stripped type never attacks a
  unit itself; its weapons still fire through the sweep (`40f330` sets slot
  targets, not missions). Ours: `World::attackStrippedAtLoad` gates
  `World::attack` (after the producer's rally), `retailIssueAutoAttackAllowed`
  and `retailBuiltAttack`, as it gates `suppressRule`. For a factory (a
  producer) a unit attack stays its output's rally (the rally rule, below);
  a stripped type that holds no rally (a mod's mobile type with a canbuild
  menu and no `bmcode`, 0 being a building's) gets nothing, where it used to
  attack. No shipped type is stripped. `mission_orders_test`.
* S: for an armed repeating mobile builder (Beast Lord: ballistic; Shaman:
  line of sight; Beast Handler: melee) the resolver ignores the repeat:
  SUPPRESS, for melee only over a clearable blocking feature. A plain click
  calls `4d6a50(unit, 0)` (`4d79b9`; I: that clears the mission list, so the
  repeating MobileBuild and its repeat end and the builder fires). An order with
  0x4 set and 0x8 clear is skipped for a builder whose MobileBuild quantity is
  at least 10,000,000 (`52162d..5216c5`, the `cmp [edi+0x52], 0x989680`; I: a
  Shift order is dropped, a Ctrl order goes ahead of the build). An unarmed
  repeating builder (Beast Tamer) has no `canattack`: Normal, nothing.

Ours keeps the rally rule (owner, C9): a factory's Attack-at-a-point rally is
ours (retail gives nothing), and for a repeating mobile builder retail's
SUPPRESS is overridden on purpose: armed or not, a factory or a repeating
builder shows the Attack cursor and gets one `Cmd::AttackMove`, which puts a
fight leg on its rally (`World::setRally`, `markFightLeg`) and leaves it
producing; `applyCommand` refuses `Cmd::AttackGround` while `repeatType` is
set, plain, Shift or Ctrl. In a mixed selection the other units fire at the
point. A click inside the factory's own footprint hovers the factory, which is
left out as in retail, so it gets nothing. Tests: `click_input` (C1 rally
holders: factory alone, factory + archer, repeating Beast Lord and Beast
Tamer, the footprint click; applied to the sim) and `conjure_test`
(AttackGround refused, hash and `repeatType` unchanged).

### Legion: a spot of its own (ours, not retail, protocol 295)

Retail's ground PARK steers to the ring's mid-radius along the unit's own
bearing from the factory (`4e33b0`, `RetailRingGoal::navigationPoint`) and
accepts arrival anywhere in the band. Every output of one door shares that
bearing, and Legion serves PARK as an exact solo goal, so the outputs queued
behind each other: a column 130-240 px out of the door, 3 of 8 Swordsmen
inside the inner radius (191 px) once the held release completed them, and
with fast production (twenty times the worker time) the Barracks jammed for
good after 21 of 40. Retail mode keeps retail's point, bit for bit.

Under Legion, `World::parkProductionOutput` marks a factory's ground PARK
`spread` and records the door bearing (birth point seen from the factory's
centre), and each time PARK's stage sets its goal `World::legionParkSpot`
picks a spot instead of the bearing's midpoint:

* Candidates in layers from the inner radius + a footprint outward, centres
  two footprints apart along and across the layers (a footprint's gap, so a
  later output passes between parked ones); in each layer nearest the door
  bearing first, alternating sides (the body's own side first), inner layer
  before outer. At a Barracks the first layer holds 22-34 spots for a 2x2
  (the padding draw sets its radius), 13-19 for a 4x4.
* Never in the door lane: a corridor straight out of the exit, half width
  `legionParkLaneHalf` (8 px times the factory's narrow side, at least 48; the
  Barracks door is its full 7 cells) plus the body's reach and a cell, from the
  factory out without end. A new output always has a free way out.
* A spot is taken while another unit's current PARK leg has claimed one
  within a footprint of it (any factory), or while any body stands within a
  footprint of it. Claims end with the leg; a parked output holds its spot by
  its body, so moving it away frees the spot.
* Reachable only: the spot's cell must share the unit's component root (the
  test `parkProductionOutput` uses), pass `mobilePlacement` on a loaded map,
  and lie on the map.
* A full band grows outward: up to 16 layers in all, past the outer radius.
  **Deliberate difference:** a spread PARK arrives only at its own spot (its
  footprint origin within a cell, `groundMissionAccepts`), in the band or
  beyond it, so arriving beyond the band completes it, which retail's ring
  never would; arriving elsewhere in the band does not (it would stop a body
  in the lane or on another's spot).
* A refused approach (the spot found blocked, the crowd settled outside it)
  raises 0x200 as before, PARK's stage re-runs and picks the next free spot.
  None free at all: retail's point, as in Retail mode.
* The held release stays as the last resort, so nothing can jam: a spread
  PARK held 4 crowd windows (6 s) and clear of the factory is completed, but
  only outside the door lane until it has been held 8. "Held" is under half a
  body gained on the spot per window, which a slow, big body walking round
  its factory also is: one still walking (Moving, above a quarter of its top
  speed) waits 16 windows. Without that, 100 Catapults from a Keep (four times
  the worker time) were completed beside the Keep on their way to spots
  behind it, walled the door and jammed it after 24 (Retail mode's PARK jams
  after 52; 4c22fbe3's Legion after 9); now all 100 park, 13% behind a rally.

Deterministic: integer and retail sine-table math, the claims are a set test
over all units (order does not matter), and the new state (`spread`, `door`,
`spot`) is hashed when set. Tests: `tools/legion_park_test.cpp` (40 Swordsmen
in both balances, a cliff with an unreachable plateau beside the factory, a
map corner, 8 Catapults, a queue of 100 Catapults, 8 Submersibles at a Navy
Yard; checked every tick
that no parked output stands in the door lane, and production against a
rally run that takes every output away), and `shipyard_park_test`'s Legion
Barracks cases (4 of 4 in the band; 8 all in the band or beyond, none inside
the inner radius, spread round the door). On 4c22fbe3 the 40-Swordsman run
jams after 21 (2,125 refused sites, 12 inside the inner radius, a parked body
in the lane on 29,804 of 30,000 ticks); now all 40 park, none in the lane.
Time from completion to the end of the PARK (four times the worker time,
Barracks, 40 Swordsmen): Legion mean 286 / max 628 ticks, Retail mode 286 /
538; 8 Catapults 415 / 583 against Retail 371 / 621. Turning off towards a
spot frees the pad later than walking straight out (30 ticks against a
rally's 21 for a Swordsman, 101 against 69 for a Catapult), so production
runs behind a rally that sends every output straight away: 6% for the
Swordsmen, 12% for the Catapults, 5% for the Submersibles (Retail mode's PARK:
1% and 7%); by the cliff and in the corner it keeps up.

### A factory's flyer: PARK's flight branch (icd `0x407ca0`, emulated 2026-10-25)

GetBuilt treats a flyer like any output: the factory's `QMove`/`QPatrol`
entries are copied with nothing inserted before them, and only when none was
copied does it add PARK, padding `(rand(6)+rand(3)+6)*16`
(`0x4023b5..0x4023d3`: the base is 6 for canfly, 3 otherwise). PARK's stage 0
splits on canfly (`test ch,8` on type `+0x260` at `0x407e51`). The flight
branch (`0x407e5a..0x407f62`) takes no ring: radius r = footX*16 + padding +
trunc(sqrt(fx^2+fz^2)*16)/2 for the factory's footprint fx,fz (the constant at
`0x5ebaa8` is -16.0, subtracted), angle a = rand(65536) drawn by the stage
itself when the mission runs, and the target is the factory position minus
(r sin a, r cos a) through `0x5360bf`/`0x5360f3`, the factory's y kept. It is
pushed as ONE `VTOL_MOVE` child (`0x4d4ef0`); stage 1 ends on arrival
(0x100), and a failed approach (0x200) re-rolls through `0x4d4e10`. When the
factory is gone or lacks `+0x130` bit 0x1000000 (`0x407f0a`) the circle is
centred on the unit itself with no diagonal term. Emulated with the callees
hooked (probe outside the repo,
`~/tak-tmp/scratch/fix-091/exit-research/probe_park_flyer.py`): Spyhawk
(2x2) at a Barracks (7x12), padding 96, gives r = 239 at every angle; angle 0
is straight north (-z), 16384 west (-x).

Port (protocol 295): `World::queueFlyerPark` queues the plain VTOL_Move
carrying the park; `World::resolveFlyerPark` fixes its point when that move's
stage 0 first runs (radius and offset in `retailpark.h`:
`retailParkRadius`, `retailFlightParkOffset`; the radius matches the
emulation for every factory footprint 1..24 x 1..24). The leg keeps its park
(attempts 1: the point is fixed) until it retires on arrival, as retail's PARK
lasts until its flight arrives, so the AI (`AI::idle`) still sees a parked
newborn, free to command, while it flies. The game RNG supplies the
draws; their order against other draws is not claimed to be retail's. Ours,
not retail: the point is kept on the map (an axis that would leave it is
mirrored through the centre, then clamped), and the retail-resources
GetBuilt host now releases flyers the same way. No reachability test is
needed: an airborne flyer holds no ground, so it can neither be left on the
pad nor block the factory's next output. Flyers are not Legion-routed, so
both pathfinding modes release them identically.

Mobile producers are deliberately unchanged: retail's GetBuilt releases only a
structure builder's output (bit 0x2000000), and a repeating MobileBuild gives
its output a PARK of its own (`0x405c07`, above). Ours keeps the exit step for
an output with no rally, because a mobile producer (a Zhon builder, Ctrl+click
repeat production) builds its next unit on the same spot. With a rally the
output flies or walks straight to it (protocol 309). (The Legion column behind the first
ground output is gone: see "Legion: a spot of its own" above.)

Placement over scenery (S yards, icd + emulation, 2026-10-30): the FBI parse
(`4c0f59..4c101f`) advances the yardmap pointer only while a next character
exists, so a one-character `yardmap=S` -- every Lodestone and Divine
Lodestone -- fills the whole footprint, and `S` decodes to mask `0x8f`
(`4c0fe3`, which also sets type `+0x264` bit 31). In `507400` that mask tests
sacred-site coverage (bit `0x80`: the covered cells of a `sacredsite` feature
must reach its fx x fz), the footprint's height range against `maxslope`
(bit 8), water depth and unit occupancy (bits 2|4), but no feature bit
(`0x20`/`0x40`), so features under the yard are never tested. Emulated: a 3x3
`0x8f` yard over a 2x2 sacred feature passes at all four covering origins,
one with a blocking feature on its corner; two of four sacred cells is
refused, an `o` yard (`0x2f`) on the blocker is refused, and a live lodestone
on the site refuses every origin. Ours (`World::placementCheck`, protocol 305,
0.9.12): an S cell of a structure tests the terrain-only grid and leaves
buildings and units to the occupancy test, ignoring the obstacle overlay, so
on Inner Circle the Divine Lodestone is admitted at the three covering origins
whose corner holds a ZonHenge05 Standing Stone (north site 127,36, west site
23,76 and 24,76), which it refused before. Deviations: a blocker the
simulation can remove later (a registered feature, a wreck) still refuses an S
cell, so a build order reclaims it first, because the obstacle overlay keeps
one bit per cell and its removal would clear the lodestone's own cells; the
height-range rule is not ported for any yard (cells use the movement grid's
per-cell test); the in-place upgrade over the player's own Lodestone is kept
(retail refuses it); on a map without sacred sites, where the engine lets a
lodestone stand on open ground (retail's coverage bit refuses it everywhere),
nothing ties the footprint to a site, so its cells keep the overlay test and a
lodestone never lands on a wall cell or a rock there. A lodestone built over
fixed obstacles records them (`World::sacredUnderlay_`) and leaves them
blocked when it is destroyed. Test: `sacred_yard`.

Placement: retail's structure placement (`507400`) tests the ground word,
which holds a mobile body only while landed (`5066f0`). Ours matches:
`searchBodyRect` skips a flyer whose `flightGroundMode` is not 1, and
MobileBuild's retry test ignores mobile units, so an airborne flyer parked
over a base never refuses a building site in `canPlace` or the AI's site
search; a landed one does, as in retail. Tests: `tools/shipyard_park_test.cpp`
(four factory/flyer pairs: radius within the formula's range, all four
quadrants including behind the factory, none left on the pad; a rally flyer
flies straight to the rally; the self-centred circle once the factory is gone;
airborne vs landed flyer over a site; the park held until arrival), both
pathfinding modes and both balances.

## A dying factory kills its unfinished output (icd, emulated 2026-10-28)

Death clears every mission of the unit: the death handler `0x512860` stores
the killer (`+0xf8`, `+0xfc`) and calls `0x4d6a50(unit, 1)`, which frees the
whole mission list (`+0x60`) through the mission destructor `0x4d6da0`. That
destructor calls the mission's handler with flag 2 when the mission has
started (`[mission+6] & 2`). Which handler that is depends on the producer:
`0x429810` queues `BuildingBuild` (`0x401c20`) for a unit without a mover
and `MobileBuild` (or `VTOL_MobileBuild` for canfly) for one with a mover;
the mover exists only when the type's `bmcode` (`0x4c004d` -> type `+0x24a`,
default 0) is 1 (`0x511fc5` -> `0x511f20`). Every shipped factory leaves
`bmcode` out (all of them say `canmove=1`, which this does not read), so
factories run BuildingBuild and Zhon's builders run MobileBuild.

Emulated with flag 2 (callees hooked): `BuildingBuild` with an output
(`[mission+0x16]`) calls `0x429990(factory, output, 0)` -- the completion
call -- then `0x51a140(factory, output, 30000, 11, 0)`, 30000 damage of type
11 from the factory, then `0x51e4d0(9, 0)` on the factory; with no output only
the last. `MobileBuild` (`0x405560`), `VTOL_MobileBuild` (`0x41ef00`) and
`HelpBuild` (`0x405cf0`) call only `0x51e4d0(8, 0)`, with or without a target.
30000 is past every armour scale (`0x51a17a` skips scaling from 30000 on), and
type 11 has severity 0, so the output runs no Killed, leaves no corpse and
fires no death weapon; the factory is the attacker, so no one is credited.
A finished output is no longer the mission's target (`0x402099` clears it),
so it is untouched.

So in retail a destroyed factory takes its unfinished unit with it, while an
unfinished unit of a mobile producer and a builder's unfinished structure
stay behind and decay (their GetBuilt lifecycle).

Cancelling (static, 2026-10-28). One BuildingBuild holds a count of one
type (`+0x4e` type, `+0x52` count including the unit in work; 10,000,000 is
repeat): the build menu's `addbuild`/`subbuild` reach `0x429810` with a
positive or negative count, and `0x4d7ca0` adds to the type's last
BuildingBuild or, for a negative count, takes it off. Only when the count
does not cover the cancel (or the mission repeats, or the cancel is "all",
-10,000,000) is the mission freed with `0x4d6ad0` -> `0x4d6da0` -> the flag-2
abort above, which kills the output; a smaller cancel just lowers the count
and the unit in work goes on. So retail kills the unfinished unit when its
last queued copy is cancelled (high confidence for the mission side; the menu
click itself not traced). Work is paid tick by tick (`0x429af0`) and the
abort refunds nothing. Stop and fresh orders clear missions with
`0x4d6a50(unit, 0)`, which keeps every mission with `+0x5a & 4`;
BuildingBuild's descriptor flags are `0x8010c`, so a retail Stop leaves
factory production running (medium confidence: the Stop command's route to
`0x4d6a50(unit, 0)` is inferred from its callers, not traced end to end).
Ownership changes (`0x514da0`, called from Diplomacy ShareUnits `0x4b6b33`
and from `0x4cb0c4`, `0x4d3d40`, `0x4e9ebd`, `0x52db08`) make a new object and kill the old one with 30000 of
type 6 (`0x514e93` / `0x514fc2`), so the old factory's death aborts its
BuildingBuild and its output dies too (high confidence, static).

Port: `World::killProductionOutput`, for a producer that `isStructure()`.
The death sweep sets the output's hp to 0 with damage type 11 and the
factory as `lastHitBy`, and takes it later in the same pass (an output is
spawned after its factory). Every other caller retires it at once through
`scenarioDestroy(output, 11)` (counted as the owner's loss, nobody's kill, no
corpse, no refund): `scenarioDestroy` of the factory, `dequeue` of the unit in
work (ours removes the type's newest copy, so that is the front only when it
is the last copy, as retail's mission count), `setRepeat` toggled off,
`stop` (user rule: ours clears a factory's queue on Stop, retail does not),
and the ownership changes `giveUnit`, `scenarioTransfer` and `captureUnit`.
Cancelling a queued entry that has not started only removes it. Also through
`stop`: a player who leaves or forfeits has every unit stopped, so his
factories' unfinished outputs go too, and CRT defeat removes them first. The
completion call is not replayed: it would only finish the unit an instant
before it dies. Ours, before: the output stood unprotected and un-conjured at
its build rate, vanishing without counting as a loss. Probe:
`~/tak-tmp/scratch/factory-death/probe_buildingbuild_abort.py` (outside the
repo). Test: `tools/factory_death_test.cpp`.

## Capture is the mind-control weapon only (icd, static, 2026-10-30)

`cancapture` (only `zonharp.fbi` sets it) is parsed into UnitDef `+0x264` bit
`0x2000` (`0x4c07de..ed`). Nothing in the simulation reads that bit: the
command-panel summary reads it (`0x4b049f`, selection `+0x24`) to show a
`CAPTURE` control that no shipped `guis/*.gui` has, and that button's order
mode 13 falls to the default entries of both mode tables (no command);
`0x519cf0` tests it but has no caller. There is no capture by contact, timer
or low-HP rule. A unit changes sides only through the mind-control impact
`0x52d8d0` and its roll `0x52da10`, whose gates are, in order: not the
caster's player, `[+8]` set (the unit's mover, so no building), not a
commander (`0x40000`), **construction remaining `+0x108` not above 0**, not in
a transport, not `cantbecaptured` (`0x100000`); then `min(99,(level+16)*5)`
scaled by the area falloff against a CRT `rand()` scaled to 0..99, and on
success `0x514da0`. Emulated with the stone and freeze callbacks in
"Mind control, stone and freeze fly" below, which corrects two readings
here: the first gate compares players, not alliances, and the roll is
`rand()*100/32768` (`_allmul`, `_alldiv`), not `rand()%100`.

`0x514da0` creates a new object for the new owner (`0x512060`, same type and
position) and copies onto it the HP raw (`0x514f8a/514f94`, no healing),
construction remaining, `+0x7c` (6 bytes), `+0x12b`, the movement bits of
`+0x130` from the UnitDef and the activation byte `+0x114` (`0x51e4d0`), then
kills the old object with 30000 of type 6. Everything else starts fresh:
`0x511990` zeroes `+0xf0` and `+0xf4` (the experience `getLevel 0x519310`
reads) and the orders are new. If the object cannot be created, nothing
changes.

Ours before: a `canCapture` unit took its target after ~3 s in reach, or at
once below 25% HP, free of mana, beside its weapon, and was kept out of the
slot sweep and hit response; `captureUnit` raised HP to half and kept
veterancy; nothing refused an unfinished target. Now the Harpy is an
ordinary mind-control caster, `captureUnit` keeps HP and resets `xp` /
`veteran` to a fresh unit's (the scenario's rank for the type, as `spawn`),
and `canCaptureTarget` and the roll refuse `underConstruction`. Deviation
kept: the unit keeps its id (and its mana, status timers and cloak) rather
than becoming a new object. (`scenarioTransfer` and `giveUnit`, which also go
through `0x514da0`, kept veterancy until the veterancy fix; they now restart
it the same way.) A mind-control slot does not select an
unfinished unit (`retailSelectTarget`, `sweepKeepsTarget`); this was listed
as a deviation, but retail's picker skips one too (`4128ca..4128db`, see
below). Probes:
`~/tak-tmp/retail-checks/capture/` (outside the repo). Test:
`tools/capture_combat_test.cpp`.

## Mind control, stone and freeze fly (icd, emulated, 2026-10-30)

The weapon parser picks the Line-of-Sight class by subtype with `_stricmp`
(`530935..530b16`): `lightning`, `fire`, `bluefire`, `dieselflame`, then
`mindcontrol` -> `LineOfSightMindControl` (vtable `5f3a90`), `turntostone` ->
`LineOfSightTurnToStone` (`5f3a5c`), `turntofrozen` ->
`LineOfSightTurnToFrozen` (`5f3a28`); any other subtype, empty or not, is the
plain class (`5f39f4`, `530afd`). The three status classes share the plain
class's aim (`52c4e0`), init (`52c540`) and draw (`52c8c0`): their shot leaves
the QueryWeapon muzzle on the stored aim angles plus the body heading, starts
on the release tick plus one (plus `nimbustime` for a nimbus caster with
nimbus art, which `52c540` also starts), expires one range of travel later
(see "Live ordinary straight-shot integration" in the transport audit), and is
drawn at its real position like any straight shot. Only the update differs
(`52d8d0`, `52db40`, `52dda0`; `tools/re/probe_los_status.py` runs the three
natively over 1,080 timelines against the model World ports):

- **Flight.** From the tick after activation, every substep turns the shot by
  its spin, steps its wrapped XYZ and calls the collision `52a4d0`. The first
  code 2 calls `529c10(shot, struck unit, 1, 0, callback)` and the shot is
  gone (the third argument retires it). On its expiry tick the shot still
  flies its substeps, then retires; the plain class does not move on that
  tick. Activation adds no body heading to the shot's yaw (the plain class
  does; only a model would show it, and no shipped status shot has one).
- **Caster.** The mind-control update checks its caster on every update: dead
  or dying, the shot is retired after that update's flight, which can still
  land. Stone and freeze check only up to activation, like the plain class:
  once flying, they land after their caster has died. (A caster that changes
  sides dies too: `514da0` kills the old object.)
- **What it strikes.** `52a4d0` stops on any unit of another player whose
  collision quad the shot enters, an ally's included, or on ground and
  scenery. The shot is aimed once, at the target's led SweetSpot, and never
  follows it: a target that moved, died or changed sides is missed and the
  shot flies on to its expiry, striking whatever stands behind.
- **Impact.** `529c10` with its effects flag clear plays the hit sound
  (`50a7d0` / `50b140`: `soundhit`, or `soundhitclass` by the struck body;
  the water sound in water) but creates no explosion (`492c80` runs only with
  the flag set). A struck unit (none of these weapons has an `areaofeffect`
  above 16) goes to the callback with scale 1.0 (`52a460`) instead of the
  damage `52a330`; with no unit struck, the area pass (radius `aoe/2` = 0)
  reaches no unit and treats scenery as any shot does.

The callbacks are `52da10` (charm; the Area Mind Control's too, pushed at
`52f041`), `52dca0` (stone) and `52df00` (freeze; the Area Freeze's too,
`52f3a1`); the probe runs them over 20,160 cases. None deals damage: the
Crusades freeze weapons' 450 is a targeting value only. Each returns 0 for a
refused unit, 1 for a failed roll and 2 for a spell that takes:

- **Charm:** the caster's player (unit `+0xfd` against the shot's `+0x92`:
  players, so an ally's unit is not spared), no mover (`+8`, made at birth
  only for `bmcode` 1, `511fcf`: no building), a commander (`+0x264`
  `0x40000`), construction left (`+0x108` above 0), in a transport (`+0xa8`),
  `cantbecaptured` (`+0x264` `0x100000`) refuse. Then the roll; on success
  `514da0` (which leaves a dying unit alone and changes nothing when the new
  owner's object cannot be made) and the weapon's explosion class on the
  unit (`492c80(unit +0x68, +0x6c)`).
- **Stone / freeze:** the caster's player, no mover, a commander,
  `cantbestoned` / `cantbefrozen` (`+0x268` bit 2 / bit 4, `4c0cff` /
  `4c0d60`), construction left refuse. Then the roll; on success
  `51a140(caster, unit, 1, 14 / 15)`, whose `51a240` skips a dying unit and
  otherwise runs the hit response `409a50`, sets the death type and the last
  hitter (only while the caster's object lives) and zeroes HP at `51a61a`:
  a statue. Then the explosion as for the charm.
- **Roll:** chance `min(99, (getLevel + 16) * 5)` (80 for a green unit, 99
  from level 4), one CRT `rand()` scaled to 0..99 by `rand()*100/32768`
  (`_allmul 5d3c60`, `_alldiv 5d4470`), which must fall below
  `(int)(chance * scale)` (the x87 product, `_ftol`).

The target picker (`412620`, `4127eb..4128db`) asks the same classes (vtable
`+0x1c` stone, `+0x20` freeze, `+0x24` charm) and skips a unit immune to the
spell, not alive or dying, or with construction left; it tests neither
Monarchs, buildings nor paralysis. A Basilisk or a Mechanic set on a Monarch
or a building spends its shots for nothing.

**Engine (protocol 306).** `Weapon::losStatus` names the three classes
(any other subtype is the plain class, `Weapon::straight`); `fire` launches
them through the straight-shot launch, and `tickStraightProjectiles` flies
them by the updates above; `applyHit`'s `hurt` runs the callbacks for these
and the two area spells (`World::retailStatusRoll` draws the CRT stream the
damage spread uses); `retailSelectTarget` has the picker's filters. `HitFx`
marks a status impact `explosion = false` and adds a silent event with the
explosion on a unit a spell takes; the viewer draws the shot as a straight
shot (the faction nimbus starts on its caster) and no longer adds its old
beam. Before: the three spells took their target on the release tick while
the viewer drew a ball flying to it for up to 1.5 s; stone and freeze dealt
the weapon's damage (to immune units too), never failed and took buildings and
Monarchs (every shipped Monarch is immune anyway); the charm took buildings
whose category its table did not zero (walls, the Trebuchet, the Veruna
Mortar), rolled on the game stream and spared allies; the picker skipped
paralyzed units and not unfinished ones for stone and freeze. Still
deviating: World updates its flames and straight shots (these three
included) before its other projectiles, where retail runs one pool in
creation order (`52afd0`), so in a tick with a status roll and another shot's
damage spread the two CRT draws can come in the other order; and a charmed
unit keeps its id (see above). Test: `tools/los_status_flight_test.cpp`.

## The Crusades balance swaps only the unit roster (icd, 2026-10-26)

With the Crusades balance on, the unit lookup `0x518080` searches only the
`UnitsCB\*.fbi` dictionary (flag byte `0x641144`) and never falls back to
`Units\`, so a unit with no `unitscb/` FBI does not exist (Iron Plague data:
`monelem`, `monmum`, `tarcship`, `tarhel`, `tarnecr2`, `zonspide`). Build menus
always come from `CanBuild\<builder>\*.tdf` (`0x4c1950`), entries filtered by
the roster (`0x515ac0`) and bubble-sorted by ascending `[Menu] Priority`
(default 9999999999, ties in enumeration order). The executable has no
`CanBuildCB` path: the shipped `canbuildcb/` tree is never read. The engine
loaded `canbuildcb/` with a `units/` fallback until 0.9.4, which, among other
things, offered the Trapdoor Spider from the Crusades Beast Tamer. Details in
[the balance reference](crusades-balance-reference.md).

## Benchmark switches and RNG seeding (icd, 2026-10-22)

Found while building the retail AI comparison harness (`tools/re/README-retail-ai.md`).
- **`-AutoStop[=N]`** (parser 0x524f60, argument cached at 0x6411cc, default 60). At
  start-up it latches an "unpaced" flag (0x641806). With that flag set, each frame
  advances by a fixed frame time (0x6411a4) instead of the measured one, and the
  game runs as fast as it can. While N is non-zero, the per-tick step (0x526310)
  reseeds both the game RNG (0x535d30: `(seed ^ 0x66e29572) | 1` into 0x64186c) and
  the C runtime's `srand` with 1234 before every tick. After N wall seconds it writes
  `c:\TAKTimings.txt` and quits. Without the switch, the game seeds once at start from
  QueryPerformanceCounter (0x527060) and never reseeds.
- **`-LockRatio=N`** (0..5, default 2, 0x525fd0) is the fixed frame time used when
  unpaced. The console command `+lockupdates [1..5]` (0x426050) sets the same value
  and the unpaced flag at run time.
- Clearing the cached N after start keeps the unpaced ticks and stops the reseeding.
  The harness does that at a tick boundary and writes its own seed.
- Retail's sim also draws from the C runtime's `rand` (129 call sites; one is a
  `random_shuffle` at 0x4d44a0). Inferred: the entity slot a new building gets
  follows that stream. With the per-tick reseed, slots repeat across runs and
  seeds. Without it they differ from run to run, even for runs made one after
  another, so something consumes the stream a varying number of times. As a
  result, same-seed games drift apart once the first building is placed. Game
  speed also changes the sim: the same seed at speed 0 and at +10 diverges within
  10 s.

## Ship / no-walk mover animation (icd + COB, 2026-09-10)

Ships have no `walk` script. The engine drives them with the **`MoveRate`**
callin on move start/stop (arg > 0 = moving); the COB's `Create` ambients
(`MotionControl`, wake/oar controllers) poll the static that `MoveRate` sets
and select `slowrow`/`row`/`fastrow` from **`GET_UNIT_VALUE 29`** (current
speed, thresholds 25/75 ⇒ percent of max). `TurnDirection(deg)` steers the
rudder/sail trim; `WindChange` orients sails/flags (FBI `wind=1`). Resetting
such a unit's VM kills the Create ambients permanently — nothing restarts them.

## SUPERSEDED: "retail has NO global pathfinder" (icd, 2026-09-12)

> **Read the RESOLVED section further down before believing this one.** The
> conclusion here -- that retail does no global search -- was drawn from the
> RTTI class list and the `Navigator` vtables, and it is WRONG. Retail does have
> a pathfinder; it is reached through a singleton pointer rather than a call, so
> a direct call-graph search could not see it. The open question this section
> ends on ("whether some free function does a coarse global search") is the one
> that turned out to be yes. The observations below about the navigator object
> and its 6-tick target refresh are still accurate; only the headline is not.


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

**What we used to do instead, and no longer do.** This paragraph described
full-map Dijkstra FLOW FIELDS per (domain, footprint, goal block) plus per-unit
A*, flow fields being our own invention (added 2026-09-03 for a crowd/corner
jam) and costing ~59% of server stalled time after four rounds of optimisation.
THE FLOW FIELDS ARE GONE (cf316fb). The search is now a port of retail's own
tracer -- see src/sim/pathsearch.h, which carries the icd entry points -- and
the crowd jam they existed to paper over turned out to be a symptom of our
movement model, which has since been replaced by retail's.

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

### RESOLVED (2026-09-16): retail genuinely wedges. There is no avoidance.

The second reading was right, and the 3x3 scan's result goes nowhere.

Follow the scan to its blocked exit. `0x4dbcf9` (`cmp eax,4` / `jl`) leaves the
loop for `0x4dbe2c`, and that block is five instructions long: load the
navigator, read `+0x36`, `and $0xff3f`, `or $0x20`, store it back, and RETURN.
No position is touched, no route is requested, nothing is queued.

So the scan's only product is bit `0x20` at navigator `+0x36` -- and nothing
reads it. Every access to `+0x36` in the navigator's own address range
(0x4db000-0x4e6000) was checked: the reads there are the mover's own
clear-on-entry (`andw $0xf81f` at 0x4dbaa8) and its refusal writes. The three
`test $0x20,%al` sites in the range are a different structure entirely --
`0x13c(%edx,%eax,1)`, not a navigator field.

That completes the picture the rest of this section describes. A blocked retail
unit sets refusal bits, clamps, slows (0.5x on the first
refusal, 0.2x once refused twice; corrected by the executable comparison in
`pathfinding-port.md`), and presses on. It does not dodge, it does
not sidestep, and it does not ask for a new route -- re-requests are on their
own 120-tick cadence and only fire when the path did not fail.

The practical consequence, for anyone tempted to add avoidance back: our own
sideways-teleport "unstick" was not restoring a retail behaviour, it was
inventing one, and it was hiding a real defect (a degenerate axis slide that
displaced -0.0002px and so never registered as blocked). Both are gone as of
10f1e1c. Head-on columns wedging is retail behaviour, not a regression.

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
  * **CORRECTED 2026-09-17: these flags are read.** Mover `+36` bits 5..7
    select 80/16-pixel steering look-ahead at `0x4d9b9a`; bits 8..10 affect
    speed caps at `0x4d96ee`. Actual refusal uses low bits 0x8 then 0x4
    (`0x4daf9f`), and repeated refusal bypasses the navigator's random retry
    cadence at `0x4e51d4`. Mover `+30` is the next local-scan tick, checked
    at `0x4dc89a`, not a movement budget. The previous write-only claim was wrong.
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

**Per-frame scheduler** (`0x416430`): walks all 10 player slots, counting into
two buckets -- `B` for players flagged at `+0x24e7`, `A` for the rest. If none,
it bumps an idle counter at `+0x1a5` and returns. Otherwise the quantum is

    quantum = +0x225 / (A + 5*B)

so a flagged player gets FIVE TIMES the share.

CORRECTED 2026-09-16: this paragraph used to say "counting pending REQUESTS"
and "per-request quantum". It is PER PLAYER. `0x634674[p]` holds a player's
pending count but is only ever tested `> 0`, never summed, and the bucket is
incremented once per player. Emulated to settle it: two players with one
pending each split 500/500, and so do two players with 50 each, and so does
1-pending against 99-pending. A player with 99 queued units gets the same frame
budget as one with a single unit. See `docs/pathfinding-port.md`. It zeroes `+0x165`, which is
the work cap the search meters itself against, then iterates the players.

**Where we deliberately depart** (2026-09-12): retail lets every pending request
hold a live search. We cap the concurrent searches at `kMaxActiveSearches` (12)
and queue the rest. Two reasons, neither of which costs throughput:

**2026-09-17 correction:** the throughput/parity claims in this historical
paragraph are unproven. `0x416430` operates on a singleton with current entity
at `+58` and per-player iteration cursors at `+115`; it does not establish
that each pending navigator owns a separate complete search. The port's pool,
admission order and frame completion timing need direct scheduler comparison.
Matching arithmetic or total work does not prove matching route-delivery ticks.

  * The per-cell scratch is the expensive part of a search -- 10 bytes per map
    cell, measured at 360 KiB on a 192x192 map -- so one search per pending
    request makes memory a product of map area and how many units happen to be
    ordered at once (~176 MB at 500 pending, on the referee and every client
    alike). Only active searches hold scratch now, out of a reused pool.
  * `quantum = max(1, budget / (A + 5B))` is not a cap. Once `A + 5B` exceeds
    the budget every request still receives 1, so the total work per tick grows
    without limit as requests pile up. Bounding the active set bounds `A + 5B`,
    which is what makes the budget a cap.

Throughput is unchanged because the budget is fixed either way: 100 searches at
1/100th speed each and 12 at a time finish the whole set on the same tick.
Bounding changes only the ORDER, and it improves early latencies. Admission
rotates (`admitCursor_`) so a busy low unit id cannot starve a high one, and it
is integer state walked in map order, so every peer admits the same requests on
the same tick. This changes the sim hash ONLY in scenarios that exceed 12
concurrent searches; the `--mpai` baseline is unaffected.

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

### The search algorithm in detail, and the port plan (2026-09-12)

`0x4146e0` is a two-phase incremental "bug" search over the 16px cell grid,
driven by a state machine at `+0x60`.

**State 0 -- init.** `0x413e50(start)` gives distance to the goal, kept as the
running BEST in `+0xcc`; zero means already there. Score the start cell with
`0x4139d0`; below 4 it gives up immediately. Otherwise seed `+0xd0/+0xd4` with
the start cell and go to state 1.

**State 1 -- greedy march** (8 work per step). Take the delta from the current
cell to the goal, convert it to one of 8 compass directions with `0x415040`,
step, and score the destination with `0x4139d0`:

  * `< 4` -- blocked. Record the current cell as the trace origin in
    `+0xf0/+0xf4` and switch to state 2.
  * `== 4` -- passable but occupied. Bump `+0xd8`; on the 3rd, switch to
    tracing as above.
  * `== 7` -- road. Bump `+0xe0`.
  * otherwise -- ordinary ground. Bump `+0xdc`; on the 3rd, switch to tracing.

Reaching the goal (`+0xcc == 0`) writes the cell to `+0x34/+0x36` and returns
-1, which the caller treats as "a waypoint was produced, keep going".

**State 2 -- cardinal march** (7 work per step). NOT a trace setup, which is
how I first read it. From the current origin `+0xf0/+0xf4` it steps one cell in
the cardinal direction toward the goal, and on each cell it lays a BREADCRUMB:
a bit in the map at `+0x2c`, and a 4-byte record at `+0x1c` whose second byte
is the direction it entered from. Reaching a cell flagged `0x4` -- the goal --
ends the whole search. When the next cell is blocked it starts the traces.

**State 3 -- TWIN boundary traces** (9 work per step). Retail runs two cursors
at once, `+0xf8/+0xfc` and `+0x100/+0x104`, with directions `+0x108` and
`+0x10c`, sweeping in OPPOSITE senses: `0x414c52` rotates by -2/-3 and
`0x414e23` by +2/+3. Each probes with the 8-direction tables against the usual
threshold 4 and lays the same breadcrumbs. Whichever cursor first regains the
straight line from the origin to the goal wins: `0x414fde` and `0x414ff8` copy
that cursor's cell into `+0xf0/+0xf4` and drop back to state 2. The M-line test
itself is at `0x414dc4..0x414df4` -- normalise the signs so the goal delta is
positive, then accept a cell lying along the first leg or at the far x with z
in range. Running both hands at once is what lets it round a wall from whichever
end is nearer.

**The route is reconstructed from the breadcrumbs**, not accumulated as it
goes: `0x414450` walks back from the goal through the per-cell direction bytes.
That per-cell "first visit wins" discipline is load-bearing -- overwrite the
direction on a revisit and the parent map grows cycles, which in our port
produced a 4-cell loop repeated out to the 64-waypoint clamp while still
reporting success.

**Two limits.** Work accumulates in `+0x48` and the step returns once it
crosses the per-request cap in `+0x165`, resuming next frame with all state
intact. Separately `+0xe4` counts cells visited and bails against `+0xe8`.

**Return protocol** (consumed by `0x415b10`): `0` done, `-1` a waypoint was
emitted at `+0x34/+0x36` (the caller charges a further 30 work and appends it),
`-2` OUT OF QUANTUM -- suspend and resume next frame. An earlier note in this
file called `-2` "failed"; it is not. `0x415028` returns it from the work-cap
check, and the caller's `+0x5c` is a still-pending marker, not an error.

**The budget is an INTEGER, not wall-clock.** `0x41617b` seeds both `+0x221`
and `+0x225` with `0x2ee0` = 12000 work units per frame, and the quality
setting scales it by a percentage. Nothing here reads a timer. That matters
enormously for us: a faithful port is deterministic by construction and safe
for lockstep, provided the budget is a match-replicated constant and requests
are visited in a fixed (player, unit id) order.

### Port status (2026-09-12): ON by default

`setupMatch` enables it. What it does, measured end to end on Inner Circle with
the service off vs on, same start and goal:

    goal + 40 cells:  35% of the way  ->  97% of the way
    goal + 20 cells:  70%             ->  70%   (unchanged)
    goal + 80 cells:  19%             ->  19%   (unchanged)

So it works where the search succeeds, and changes nothing where it fails --
the unit keeps the straight segment it always had.

**The failures are largely inherent, not bugs.** March-plus-wall-follow cannot
solve NESTED obstacles, and the 20- and 80-cell cases on Inner Circle run into
exactly that: a mazey field where the outline a cursor is following contains
further obstacles. Retail's own `(w+h)*20` visit limit (`0x414797`) abandons
those too. A bug algorithm is not a planner and was never going to be one.

**Wandering routes are retail's too.** The 30-cell route comes back with 64
corners looping well past the goal, which looked like a defect until reading
`0x414450`: the original records a waypoint only on a direction change, exactly
as we do, applies no smoothing, keeps the last 64 in a RING buffer (`& 0x3f`),
and merely sets a "this route is a detour" flag on the unit when the step count
exceeds the manhattan estimate. Retail tolerates wandering routes; so do we, and
the unit still gets there -- 97% of the way on a route that wanders.

**Failure backoff matters more than it sounds.** A search that failed from
roughly here will fail again, so a unit stuck against a maze sat re-requesting
every 30 ticks and ate the whole budget. Backing off 150 ticks after a failure
cut the benchmark from 23.2ms/tick to 22.1 while keeping the +40 result at 97%.

Cost: 22.1ms/tick at 2217 units against a ~19.5ms baseline at ~2000, so roughly
+1 to +2.5ms depending on how you weigh the unit-count difference. That buys
units that actually arrive. The `--mpai` hash moves to 9179fe05b490297b,
reproducible run to run, and the cross-compiler golden is untouched.

Still open: whether our cursors wander further than retail's. The route builder
and the termination test are now confirmed to match, so if there is a remaining
divergence it is inside the trace's stepping, and it would show up as route
quality rather than as failure.

### The unstick pass is gone, and what is still broken (2026-09-12)

Retail has no "unstick" pass, and neither do we any more. Ours nudged any ground
unit standing on a blocked cell toward the nearest walkable one, to rescue
bodies "spawned by a building, shoved by a crowd, or clipped a corner" -- and
two of those three causes were our own (the crowd-shoving one was the separation
pass, also deleted). Worse, the nudge moved a unit every tick, which reads as
progress, which reset the wedged timer, so a unit that could not reach its goal
never gave up.

Three real bugs fell out of chasing a unit that walked for ever without
arriving, and all three are fixed:

  * **The no-headway test compared SQUARED distances** and subtracted 400 for
    "20px closer". At 2900px the squared distance is ~8.6 million, so a
    sub-pixel gain cleared the bar and the timer never accumulated at range.
    Now linear.
  * **A clipped route threw the destination away.** `replaceLeg` marks the last
    waypoint of the installed path as the leg's goal, so a route cut off at the
    64-waypoint limit replaced the player's destination with wherever the route
    happened to stop. The unit walked to each route's end, asked for another, and
    shuffled between them. The true goal is now appended when a route falls
    short.
  * **The final waypoint was snapped to the exact goal unconditionally**, even
    for a clipped route, sending the unit charging at a distant point through
    whatever lay between. Now only when the route really reached the goal cell.

RESOLVED, and my first reading of it was wrong. Appending the destination fixed
the circling; what looked like a livelock afterwards was a 60-SECOND HARNESS
CUTOFF being misread. The unit was converging the whole time -- distance to goal
falling 3282 -> 3220 -> 3126 -> 3023 -> 2931 -> 2861 -> 2741 -- and I read "1873px
travelled, no arrival" as evidence of circling when it was evidence of walking.
Run the same case for 300s and it ARRIVES: 124.3s, 5181px travelled, from a unit
spawned inside rock to the far corner of a 192x192 map.

The lesson worth keeping: total distance travelled is not a stuck-detector. Plot
distance TO THE GOAL over time, or the harness will lie to you.

The retry cadence is also now retail's rather than a guess. `0x4e545b`
re-requests only once the tick counter has passed the stamp at navigator+0x110
by `0x78` -- 120 ticks -- and only when the path did NOT fail, testing the
failed/detour bits first and doing nothing at all if either is set. We had been
re-asking every 30 ticks, four times retail's rate.

Still not attempted: nothing here needs a per-destination timer. I proposed one
while the circling was still unexplained; it was a fix for a bug that turned out
not to exist.

### Clicking somewhere unreachable (2026-09-12)

Reported from play: a Monarch ordered at a mountain shoves at the cliff
indefinitely. My harness said otherwise, because it was testing the wrong thing
-- it ordered a unit at a goal whose whole neighbourhood was rock, and the
reachability test correctly reported "unreachable" and dropped the leg.

A click on a mountain is not that. It lands on a cell no ground unit fits in,
but the reachability test resolves an unstandable goal to the nearest WALKABLE
cell, which is normally on the unit's own side of the mountain -- so it answers
"reachable", quite correctly, and nothing ever declares the order impossible.
The unit then walks up to the rock and pushes.

`order()` now snaps a destination the unit cannot stand on to the nearest cell
it fits in, before the order is queued. The unit walks as close as it can get
and ARRIVES, which is what retail does (observed in play: ordered at a mountain,
retail's Monarch goes as near as it can and stops, with no pause first).

That change exposed a second bug, this one entirely mine. Path requests are
keyed by UNIT ID, so queueing a second move cancelled the pending request for
the leg in progress and then installed the queued destination's route into that
leg -- the unit set off for the last thing you queued and skipped everything
before it. `order()` now only requests a route when the new order is the one
about to be walked; orders behind it get theirs when they become current.
"the unit walks the FIRST queued leg" is the test that catches it.

### Port plan

> **Status:** done and since superseded. `pathBudget_` and the inline A* are
> gone; gameplay routes now run the executable-verified `RetailSearchWorker`
> phases (`src/sim/pathsearch.h`, `PathSearch` kept only as a comparison
> harness). Current state: [pathfinding-port.md](pathfinding-port.md).

1. `PathService` owning a request queue, replacing nothing at first -- run it
   alongside the current mover behind a flag.
2. Per-tick scheduler: count pending requests, `quantum = budget / (A + 5*B)`,
   visit in deterministic order. Budget from match config, default 12000.
3. `PathSearch` as a resumable struct: state, current cell, trace origin and
   direction, best distance, the three cell-quality counters, work counter.
   Costs 8 / 7 / 9 / 30 exactly as above.
4. Cell scoring reuses our existing per-class `NavGrid` + `bodyPenetration`
   occupancy, mapped onto retail's grades: impassable / 4 occupied / 6 ground
   / 7 road, threshold 4.
5. Navigator side: extend orders to carry up to 64 waypoints with pop-on-
   arrival, and install the straight two-point segment while a request is
   outstanding -- which is exactly today's behaviour, so this is the fallback
   path we already have.
6. Retire `pathBudget_` and the inline A* once 1-5 are in and the Monarch
   harness passes.

Determinism gates at every step: `tools/check-determinism.sh` plus the `--mpai`
hash must be reproducible, and the state hash only changes when intended.

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

## Retail stores no float in its unit state (2026-09-16)

Asked while porting our own positions to fixed point: which of our remaining
float fields are float in retail? Answer: none of them are.

Across the mover/unit region (0x4da000-0x4dffff) the FPU is used 275 times
against 3556 integer ops, and the split of what it touches is decisive:

    fildl  (load INTEGER, convert)   68
    flds   (load float)              13
    fldl   (load double)              7
    fstps  (store float)              6
    fistp  (store integer)            0

Every one of the 6 float stores, and all but one of the float loads, is
`%ebp`-relative -- stack locals and parameters, not struct fields. The single
exception is `flds 0x5f2830`, a global constant. So retail's persistent unit
state is integer throughout; the FPU is scratch for intermediate arithmetic and
never writes a float back into a unit.

Two fields confirmed directly rather than inferred:

  * **Speed** is fixed-point. The occupancy test compares two units' speeds with
    an integer `cmp`/`jl` (0x4db79e), and the scaling either side calls
    `0x5d3dc0`, which is a 64-bit arithmetic shift invoked with `cl = 0x10` --
    a 16.16 multiply.
  * **Position** is 0x100000 per 16px cell, which is 65536 per pixel. The same
    16.16, so our Fixed matches retail's resolution exactly rather than by
    coincidence.

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

**Correction (2026-09-17): that query alone does not authorize a movement
commit.** `0x4daf8f` separately calls the hard footprint placement test
`0x507d10`, which refuses both parked and moving occupants. The older claim
that persistent overlap was established retail behavior was unsupported.
The traffic predicate also requires 75% of the querying unit's terrain-adjusted
base speed, not just its current speed. Both checks are now exercised under
emulation in `tools/re/emutraffic.py`; see the current correction in
[`pathfinding-port.md`](pathfinding-port.md) for the implementation and limits.

## Self-destruct: the unit quits, it does not explode (2026-09-12)

Ctrl+Shift+D in retail does not kill a unit the way a weapon does. It leaves
your command and fades, with no explosion and no wreck.

The mission is `SelfDestruct` / `UNITMISSIONCODE_SELFDESTRUCT`, whose handler is
at `0x4017e0` -- found through the command table at `0x5eb650`, which pairs
those two name strings with it. What it does:

  * On the first run it seeds a countdown at mission `+0x52` from the unit
    TYPE's `selfdestructcountdown`, tagged `0xF0000000` so a zero count is still
    distinguishable from "not started". Retail packs that key into three bits of
    `UnitType+0x264` (`0x4c09e8` masks it `& 7` and shifts it 21), so its whole
    range is 0..7.
  * Each run decrements and reschedules itself 30 ticks out (`0x4d6a10` sets
    `+0xa = now + arg`), so the countdown steps once a second.
  * At zero it flags `+0x4e` and reschedules a random 0..14 ticks later; that
    next run applies **30000 damage of TYPE 5** to the unit itself, via the
    damage entry at `0x51a140`.

Type 5 is the interesting part. It is not 3, and 3 is the explosion type -- the
one that makes the death handler refuse a corpse and blow every piece apart. And
in that handler type 5 takes its OWN branch (`0x5126a9`), setting a flag in the
death event that no other damage type sets. Nothing in the shipped data gives
any weapon damage type 5; it is the engine's way of marking "this unit quit"
rather than "this unit was killed".

**`selfdestructcountdown` is never set in the shipped data** -- not one of the
558 FBIs across every archive declares it. Which makes the DEFAULT the only
value that matters, and the default is not zero: when the key is absent the
parser clears bit 23 and sets bit 22 of the three-bit field (`0x4c0a1f`),
leaving **2**. Every unit in the game therefore has a two-second countdown.

Running the mission settles it rather than arguing from the disassembly
(`tools/re/emuself.py`, hooking the reschedule, damage and rand calls):

    selfdestructcountdown = 0
      run 0 -> ret=5   expired=0   ('damage', 30000, 5)

    selfdestructcountdown = 2
      run 0 -> ret=1   1 left      ('reschedule', 30)
      run 1 -> ret=1   0 left      ('reschedule', 30)
      run 2 -> ret=1   expired=1   ('reschedule', 0)
      run 3 -> ret=5               ('damage', 30000, 5)

Two reschedules of 30 ticks, then the damage: two seconds, which is what the
game gives you. At 0 it fires on the spot, so the default is doing real work.

Retail also LABELS it, and the label is the whole explanation. The command table
entry carries two strings, `SelfDestruct` and `UNITMISSIONCODE_SELFDESTRUCT`,
and the second is a key into `translate/unitmissions.tdf` -- the table of what a
unit is currently doing, the text the InfoPanel shows:

    [UNITMISSIONCODE_SELFDESTRUCT]
        English = Leaving Your Command;
        German  = Einheit wird Aufgel"ost;
        French  = Abandonne votre camp.;

So the unit's status while the countdown runs is "Leaving Your Command". It is
not a chat line or a notification -- it is the mission label, sitting where
"Moving" or "Guarding" would. (I first put it in the chat overlay, which was
wrong.)

That table also corrects several of our other labels, which were plausible
paraphrases of missions retail names differently:

    RECLAIM      -> "Clearing"          (we said RECLAIMING)
    RECLAIMAREA  -> "Clearing Area"
    LOAD         -> "Loading"           (we said BOARDING)
    BEINGBUILT   -> "Intangible Mass"   (we said UNDER CONSTRUCTION)
    CLOAK        -> "Cloaking"          (we said CLOAKED)
    SEEKATTACK   -> "Seeking to attack" (we said ADVANCING)

## Retail does not sort 3DO primitives (2026-09-12, corrected 2026-10-28)

This section first said retail "z-buffers" 3DO primitives, settled by the Glide
depth API in the import list. The imports alone did not settle it, and the
2026-10-28 study (static analysis of `KINGDOMS.icd`) found the Glide depth
buffer is not shown in use. What is established now, and how firmly:

  * **No primitive sort (established, both renderers share the walk).**
    `0x4eea20` is the piece-tree walk -- it recurses over child/sibling, applies
    the 3x3 piece rotation to each vertex, and hands the result on. There is no
    depth comparison, no insertion into an ordered list, no swap loop in it, nor
    in the per-primitive loop below.
  * **Software path: a per-unit 8-bit depth plane (established, high
    confidence).** Each model is drawn into its own offscreen image the size of
    its screen rectangle (at most 512 x 512): `0x4ec430` -> `0x4ebb00(rect, 1)`
    -> `0x4d47e0`, which allocates `2*w*h + 0x18` bytes, colour at `+0x10`
    (filled with 9, the key colour also stored at `+8`) and a depth plane at `+0x14` (zeroed). The
    plain image allocator `0x4d4750` leaves `+0x14` null. `0x4ed2d0` then walks
    the pieces (records of `0x38` at `+0x1cc`, last to first) and each piece's
    primitives in file order: it projects every vertex to
    `(x, -z - y/2, base + y)` with `base` 50, or 125 when UnitDef `+0x260` bit
    `0x40000000` is set (`0x4ed46b..0x4ed4c3`); culls by the sign of the 2-D
    winding (`0x4ed79b`, `js` skips); lights the face; and rasterises with
    `0x5415a0` (or `0x542080`). The span loops there (`0x541819..0x54188a`
    textured, `0x5418b4..0x541907` and kin) interpolate that third value and,
    when the target has a `+0x14` plane, draw a pixel only where the value is
    `>=` the byte already there, then store it (`cmp cl,al / ja skip`, write
    colour, write depth). So inside one model visibility IS resolved per pixel
    -- but the key is the vertex's HEIGHT (plus the base), not depth along the
    view ray, and ties go to the later primitive. The finished image is then
    blitted to the screen like a sprite; the depth plane never leaves it.
  * **Glide path: depth buffer not shown in use (medium-high confidence).**
    `grDepthBufferMode`/`grDepthMask` (driver slots `+0x130`/`+0x134`, resolved
    by name from `Glide3x.dll`) are reached only from the clear method
    `0x5b7730` and a D3D-style render-state shim `0x5b7fe0` (depth states
    7/14/23/47). Every clear call found (`0x423258`, `0x5293a5`, ...) pushes
    depth off (`0,1,0`), and no constant depth-enable was found. Some call sites
    pass the state in a register, so this is a scan, not a proof. How the Glide
    path orders the primitives inside one model -- whether it also renders to a
    per-unit image, or relies on draw order and the cull -- has NOT been traced.
  * The DirectDraw error text
    "...because there is no hardware support for zbuffer blting. (NOZBUFFERHW)"
    is a capability complaint about z-buffered blitting; it shows the API was
    probed, not that the game draws with one.

**So our triangle sort stands in for a per-model depth test, not for a retail
sort**, and no choice of sort weights can match a per-pixel test exactly,
because a single key per triangle cannot express what it does for
interpenetrating geometry. (Retail's software key is height, ours is depth along
the view ray -- see below; the two agree on most convex pieces once back faces
are culled.) Between units, retail has no per-pixel test at all: see "The world
draw order" below.

That reframes `kSortZ`/`kSortY`: they are our own stand-in for a per-model depth
test, not a retail constant. But a stand-in can still be WRONG, and ours was.

The sort key has to be depth along the view ray, and the projection hands that
over directly: `screenY = z - y/2` means points differing by `(y,z) = (2,1)`
land on the same pixel, so that direction is the ray and depth along it is
proportional to `2y + z`. Larger y is higher and nearer the camera; larger z is
further down-screen and also nearer -- so both terms carry the SAME sign.

The old weights were `cos`/`sin` of the 0.72 rad tilt, which gave y and z
OPPOSITE signs. Every triangle pair separated mainly in z was ordered
backwards, which is what tore layered pieces like a Monarch's cape. Now
`-(2y + z)`, derived rather than fitted.

A correct key was still not enough, because we were not CULLING. Retail does --
it imports `_grCullMode@4` -- and without it both faces of a two-sided piece are
drawn at almost the same depth, so which one wins is arbitrary: a Monarch's cape
came out as a patchwork of its own front and back. The cape is four segments,
each carrying a front quad and a back quad (`capelogoA1`/`A2` and friends), which
is exactly the shape that fails.

The cull is tested against the VIEW DIRECTION, not a screen-space winding sign,
because the transform negates y and may mirror x and reasoning about the
resulting handedness is how sign errors get in. Two measurements settle the
direction without guessing:

  * 3DO polygons are wound so `(v1-v0) x (v2-v0)` points OUTWARD -- 88-97% of
    primitives agree, across araking, arasword, tarnecro and zonhurt.
  * With the cull as written, kept faces sit toward the camera and culled faces
    away: mean offset along the view ray `+2.0..+2.9` for kept against
    `-1.8..-3.4` for culled, at every facing. An inverted sign would swap those.

56-65% of triangles survive, a little over half because flat pieces favour one
side.

What remains beyond this is two triangles that genuinely interpenetrate, which
one key per triangle cannot resolve. That is the real depth-buffer case, and it
means taking models off `SDL_RenderGeometry` -- worth doing only if artifacts
survive a correct key and a cull.

## The world draw order: 16-pixel rows, no depth test between sprites (icd, 2026-10-28)

How retail orders units and features against each other, from the world pass
`0x4fc648..0x4fcc46`. It paints by 16-pixel map rows, not by a per-object depth
key:

  1. **Bucket the units.** Every unit, buildings included, goes into the bucket
     of its row: `((short)unit+0x72 >> 4) - camRow + 0x20` (`0x4fc748..0x4fc7d5`).
  2. **Flat features first.** A full-screen cell scan (`0x4fc8d9..0x4fc963`)
     draws at once (call `0x4fd430`) every feature whose def byte `+0x138` is
     below 10. That byte is the TDF `height` key (parsed at `0x4939ef`, stored at
     `0x493a0e`). Taller features are deferred: the scan sets cell `+0xd` bit
     `0x40` (`0x4fc954`).
  3. **Rows, top to bottom** (`0x4fc9c1..0x4fcb2c`): that row's ground units
     (`+0x130 & 3 == 1`, drawn by `0x4ee700`), then that row's deferred tall
     features (`0x4fca83..0x4fcaf6`).
  4. **Shots and effects** (2026-10-30): `421d00` (the transport effect),
     `52b210` (the shot pool at `0x641208`, each weapon class's draw virtual),
     then `492cb0` -> `492070`, the effect manager, whose first call (`492590`)
     draws the debris and then the explosions.
  5. **Everything else last** (`0x4fcbab..0x4fcc27`): the units with
     `+0x130 & 3 != 1`, inferred to be the airborne ones.

So a stump, crop row, smudge or mana decal is never painted over a unit, a tree
paints over every ground unit of its own row and the rows above it, debris and
explosions paint over every ground unit and feature, and a flyer paints over
everything, debris, explosions and shots included.

There is no per-pixel depth test between sprites and models (medium-high
confidence): see the Glide depth-buffer scan in "Retail does not sort 3DO
primitives" above, and the software path's depth plane lives inside each unit's
own image. Nor does retail show a unit hidden behind scenery in any way -- no
indicator was found (inferred from absence; the feature bit `0x400`
`nodrawundergray` is a fog check at `0x4f7070`, not an occlusion one).

**Our port** (`src/client/gameview_render.cpp`, layers in
`src/client/hiddenunits.h`) builds one painter list per frame and stable-sorts
it by layer, then by map z:

  * `kLayerLowFeature` (-1): features of TDF height below 10 -- retail's step 2.
    Before 2026-10-28 every feature sorted with the units by z, so a flat
    feature just south of a unit painted over its feet; the `-24` bias that
    kept mana decals under lodestones went with the change.
  * `kLayerGround` (0): ground units and tall features together, by plain depth
    (map z), not by 16-pixel rows. Inside one row retail draws all the units
    before all the tall features; we interleave them by z, which differs only
    when a unit and a tall feature share a row.
  * `kLayerAir` (1): airborne flyers, after everything -- retail's step 5.
    The body replay stops at the first air item (`GameView::airOp_`) and draws
    the air bodies after the shots and `drawParticles` / `drawEffects` /
    `drawUnitFx`, before the fog. Before 0.9.12 they were drawn in the body
    pass, so a ground fight's debris, explosions and shots painted over a
    flyer passing above. The airborne flyers' shadows stay at the ground ->
    air boundary of the body pass, under the shots and effects (retail draws a
    unit's shadow inside its own draw, so a flyer's shadow comes after the
    effects there). Test: `air_over_fx` (`TAK_AIR_OVER_FX_TEST`).

HIDDEN UNITS (Options > Graphics, default FADE) is a non-retail extra on top
of this order: a ground unit that a tall feature or a building paints over is
shown as a tactical dot or by fading the cover. OFF is the retail look.

## Unit shadows are PROJECTED SILHOUETTES -- and the Glide path is the one that matters (2026-09-12)

Reported: units have no shadows, then retail screenshots of an Aramon and a
Taros Monarch on open sand, both plainly casting one, the Aramon shadow carrying
the sword as a long thin streak. A blob cannot do that; a projection can.

**The renderer we target is Glide, not the software one.** The icd ships both,
and they shadow units by completely different means. Chasing the software path
first cost a full pass of wrong conclusions, so start here: `Glide3x.dll` is
resolved by name at runtime (hence nothing in the import table), 83 entry points
from a string block at `0x621fe0` into a driver object -- `grDrawTriangle` at
`+0xbc`, `grCullMode` at `+0x120`, and so on, resolved by the stride of stores
at `0x5b8617`.

The giveaway that shadows are GEOMETRY, not sprites, is the renderer's own
counter: `"Max polys rendered : %d ( %d shadow )"`. The shadow tally is the
global `0x62a800`, incremented at `0x4edc67`, inside a rasteriser reached only
from the model draw:

    0x4ee700  draw unit
      0x4ee620   build the piece rotation, walk the piece tree (0x4eea20) ONCE
      0x4ec720 -> 0x4ec7b0 -> 0x4ede50 -> 0x4eda80   the shadow polys

`0x4ede50` projects the same geometry through `0x4ec250` and hands it to the
rasteriser. That routine is the whole answer. It works in 16.16 fixed point and
takes a flag choosing between two shears:

    ecx = -z,  edx = y,  esi = x                     (each >>16, sign-extended)
    flag 0:  edx = y>>1;  ecx -= edx             ->  ( x,        -z - y/2 )
    flag 1:  edx = y>>2;  esi += edx; ecx -= edx ->  ( x + y/4,  -z - y/4 )

`0x4ecf21` passes 0 for the model; `0x4ede50` passes 1 for the shadow. So:

  * Flag 0 independently re-derives our model projection and `kProjY = 0.5`.
  * **The light constants are both a quarter**: `kShadowLX = kShadowLZ = 0.25`.
    We shipped `0.55 / 0.35` first, fitted by eye to a screenshot; they were
    wrong and are now read out of the binary.
  * The shadow is NOT flattened to y=0. It is a second shear of the same
    vertices, so a vertex sits exactly `(+y/4, +y/4)` from its own body vertex
    in screen space -- down and to the right. A flyer at altitude A therefore
    casts its silhouette A/4 right of and A/4 below its body, which is the
    offset shadow every TA-engine flyer has.

The exclusions are only two, both bits of `UnitDef+0x260`: `noshadow`
(`0x2000000`, tested at `0x4ec8d8`) and `floater` (`0x80000`, at `0x4ecac6`).
There is no `canfly` test and no building test, so flyers and keeps both cast.

**Engine enhancement (2026-09-26):** TAK Engine now allows floaters to cast
animated silhouette shadows too, while retaining the `noshadow` exclusion and
the global Shadows option. This intentionally differs from retail Glide; it
does not change the retail findings above.

`ShadowScale` (a 0..3 setting at settings+0x19) maps to 1/2/4 at `0x4ecb83` and
becomes the divisor `0x4ec250` applies to the projected coordinates -- a shadow
resolution knob. `DrawShadows` is the bool at settings+0xf, reaching the draw as
`gameState+0x19c70`.

### The sprite shadows are the SOFTWARE renderer's answer

`0x4c12b0` parses `shadowgaf` and `shadowart` and sets the type's shadow
sequence at `+0x288` only when BOTH keys are present. There is no fallback: the
4th argument of the key reader `0x5432c0` is a default string, but the default
for `shadowart` is empty AND that path returns 0 (`0x543319`), so a missing key
skips the store either way. `shadows.gaf` holds 17 sequences of 6-12 frames --
pre-rendered directional blobs, one frame per facing.

That art is blitted by `0x4ee310`, which is reached only from `0x511d00` and
never from the model draw. **In Glide it is dead for units.** 104 of 203 unit
FBIs declare a `shadowart` and none of them use it; `araking` declares only
`shadowgaf = shadows` and no `shadowart`, which is exactly why the sprite path
could never explain the Monarch.

This also overturns an earlier, well-evidenced but software-only conclusion: we
excluded `canfly` from casting because `0x4ee340` installs shadow art only when
noshadow and canfly are both clear. True of the sprite; false of the renderer we
target. The 24 flying types carrying a `shadowgaf` are not dead data.

Two wrong answers on the way, both worth naming:

  * A fixed 14x5 blob, our own invention, the same size for every unit and at
    alpha 70 effectively invisible.
  * The model's GROUND PLATE -- the flat untextured unit-sized quad every root
    carries (`AraGP`, +-17.6 for a Monarch, +-14.4 for a swordsman). It looks
    like the answer, and it is not: a rectangle cannot be a silhouette, and
    `arasword` carries the same plate while using a sprite. It is a footprint
    marker.

### It is a flat multiply, not per-triangle alpha

Retail's shadow is one uniform tone with no internal structure -- no darkening
where a wing crosses a body. Sampling a retail screenshot gives shadowed/unshadowed
ratios of 124/225, 106/192, 92/170: a flat multiply to **0.55**, equal on all
three channels, so it multiplies the ground rather than blending toward a grey.

That is what the rasteriser's `alloca` at `0x4eda83` is for -- 0x5f08 bytes of
span/coverage buffer. The whole silhouette is rasterised into it and blended
ONCE. Drawing the triangles individually with alpha instead stacks them wherever
the silhouette folds over itself, which reads as obviously wrong next to retail.

The accelerated renderer now rasterizes independent per-unit silhouettes into
bounded, reused atlas pages and composites each silhouette once. Animated cutout
coverage and the renderer's antialiasing scale are preserved. Texture batching
within disjoint tiles reduces GPU state changes. Software rendering and bounded
allocation failures retain the direct MOD fallback, whose overlapping faces can
still darken repeatedly. See `shadow-performance-2026-09-25.md` for measurements,
limits, and the remaining ground-unit ordering approximation.

### Probe the shear sign on a GROUND unit, never on a flyer

Retail's shadow is `(+y/4, +y/4)` from the body in screen space: down AND right.
In our renderer that is `rx + kShadowLX*w[1]` and `rz + kShadowLZ*w[1]` -- plus
on both.

The trap, which cost a wrong commit: a flyer's shadow carries TWO x terms, the
per-vertex shear and the altitude offset applied at the anchor, and the altitude
term can outweigh the vertex one. Probing the sign on flyers (set `kShadowLX` to
2.0 and see which way the silhouettes smear) therefore reports the sign of the
wrong term -- it said "negate x", and negating x sent every GROUND unit's shadow
leaning left, which is what the player then saw.

A ground unit has `alt = 0`, so it isolates the vertex expression exactly. Probe
there. The exaggerated constant is still the right technique: with `+` a ground
unit smears hard right, with `-` hard left, unambiguous in one screenshot.

Shadow triangles skip the backface cull -- a silhouette is the union of both
faces, and culling half of it punches holes. Anchor the shadow on the BODY
anchor plus retail's delta, never on a re-derived ground point: flyers are
exempt from our terrain lift, so a ground-derived anchor picks up a lift the
body never had and the horizontal lean cancels to nothing.

Cost: every casting unit walks its model a second time.

## Fog of war display: one sight plane, LosOn, minimap-only radar (icd, 2026-10-29)

Retail has **one sight plane per player** and both its logic and its screen
read it. Found by static analysis and by emulating `0x4c6800`; ported in
`client/retailfog.h` (the plane and stamp were already the simulation's:
`World::viewCounts_`, `src/sim/retailexploration.h`).

- **The plane.** `player+0x88` view counts (width `+0x8c`, height `+0x90`)
  on 32 px cells of the projected map. A unit's sight object (`unit+0x84`) is
  stamped by `0x4c6800` from `0x4c6a70`/`0x4c6c00`, which `0x4f6a40` calls for
  a player of kind 1 or 2 or one sharing vision with the viewer (`+0xf7[]`):
  the centre is the WORLD cell (`x>>5`, `z>>5`), the disc reaches twice the
  sight distance, and each cell is admitted under a cone from the eye through
  the coarse projected height plane (`0x50ea58`): about 1x on flat ground, up
  to 2x from high ground, less uphill. `0x4f59c0` (a remote ally's
  ShareMapAndLOS message) re-points that ally's sight objects at the local
  player, so its units stamp into his plane; who shares by default is in
  "Mission target scan" (allied humans on one team, never a computer). The Mapping plane is `game+0x19ef4`, one
  bit per player.
- **LosOn** (`0x4f7210`): the owner always; otherwise a unit is seen when one
  of its four projected bounding-box corners (`0x4c1410`) is on a cell whose
  count is nonzero. The main view's unit draw list (`0x51f970`) holds other
  players' units only when it passes.
- **Minimap.** `0x4f9970` marks each frame, in `unit+0x130` bit `0x100`: own
  units (all units for a viewer with `player+0x50`->`+0x9b` bit 1); every
  uncloaked other unit within `type+0x228` radar distance of one of the local
  player's units (built, `+0x108` clear) by the circle query `0x509ac0` with
  callback `0x4f98e0`; otherwise a unit whose projected centre cell has a
  count (LosOn on), or whose Mapping bit is set (LosOn off). `0x4f9520` draws
  the marked dots (all units with LOS and Mapping both off). The minimap image
  (`0x4f93a0`) shows each cell as it is (count nonzero), through a darkening
  remap table (`game+0x19efc`, `+0xc`; explored, no count) or as palette 0
  (Mapping bit clear). **Radar never clears the plane and never puts a unit
  in the main view.**
- **The overlay.** `0x4fbc73` calls `0x497a40`, which asks the 3D device for
  capability 4; Glide's `0x5b7060` answers yes, so `0x498270` runs (else the
  vertex-alpha twin `0x497ae0`: per-cell-centre corners at alpha 0xff / 0x78 /
  0, Gouraud-blended). `0x498270` writes one byte per cell of a 64-cell window:
  Mapping bit clear `0xf0`, count zero `0x8f` (`0x0f` when LosOn is off),
  else `0x0f`; `0x498b60` expands each cell into 2 x 2 texels of a 128 x 128
  ARGB4444 texture (`0x5b9719`: format 3 = `GR_TEXFMT_ARGB_4444`), each
  nibble `(19 own + 5 + 5 edge neighbours + 3 diagonal) >> 5`. Render states:
  `0x11` (mag filter) 2 = bilinear unless **FogDebug** (`+0x801c`, console
  toggle `0x425ed0`) makes it 1; modulate, alpha blend SRCALPHA/INVSRCALPHA,
  clamp. The window is drawn as 6 x 6 quads (`0x498c00` interpolates them).
  **RollingFog** (`+0x801e`, `0x425f10`, on) colours each vertex FogColor x
  `(sin((camY+y)/32*0.31 + 0.17t) * sin((camX+x)/32*0.11 - 0.4t) * sin(0.5t) + 1) / 2`
  (`0x498c70`); **WavyFog** (`+0x801d`, `0x425ef0`, on) adds `10 * sin(..) *
  sin(..) * sin((t+1.2)/2)` to x then y of the inner vertices (`0x498d40`,
  `0x498db0`); `t` is QueryPerformanceCounter seconds (`0x5ba760`), constants
  set by `0x497900`. FogColor (`+0x801f..21`, setter `0x497a90`) is the local
  player's side's `FogColor` from `sidedata.tdf` (parsed at `0x4c2e0c`,
  default 255 255 255).

## The projection is a SHEAR, not a tilt (2026-09-12)

`0x421dad` spells retail's world-to-screen transform out in seven instructions:

    movswl 0x2(%esi),%ecx     ; x
    sub    cameraX,%ecx       ; screenX = x - cameraX
    movswl 0xa(%esi),%eax     ; z
    movswl 0x6(%esi),%edx     ; y
    sar    $1,%edx            ; y / 2
    sub    %edx,%eax          ; z - y/2
    sub    cameraY,%eax       ; screenY = z - y/2 - cameraY

So screen Y takes ALL of z and HALF of y, and screen X takes no y at all.

The important part is what that is NOT: 1.0 and 0.5 are not the cosine and sine
of any angle, so the transform cannot be written as a tilt. We had modelled it
as one for a long time -- `cos(gTilt)=0.75`, `sin(gTilt)=0.66` -- which is wrong
in both terms at once, and wrong in a way no amount of tuning the angle could
fix. `kProjY = 0.5` and `kProjZ = 1.0` replace it.

This is the same halving as the terrain lift, which is the point: one projection
governs models, the terrain relief, flyer altitude, the selection ring's mid
height, and the hit box (derived from the same `collect()` walk, so it follows
for free and stays consistent with what is drawn).

`gTilt` is gone entirely, along with the `--tilt` flag that tuned it: there is
no camera angle here to tune. What remains is a triangle depth-sort key inside a
model (`kSortZ`/`kSortY`, now the derived weights 1 and 2), needed only because
we draw without a depth buffer. Retail's software renderer sorts nothing: it
depth-tests each model inside its own image by vertex height (see "Retail does
not sort 3DO primitives"); the Glide path's order inside a model is still open.

## The terrain-height lift is height/2 (2026-09-12)

Units are drawn lifted up-screen to sit on relief that is painted into flat
tiles. We had been lifting `1.1` px per height unit, picked by eye. Retail's
figure is **0.5**, and the arithmetic is explicit:

  * `0x511140(point)` reads the point's cell through `0x50e600` and returns byte
    `+4` of the 14-byte cell record -- the terrain height.
  * Its caller `0x426820` forms
        `screenY = (z << 4) - cameraY - (height >> 1)`
        `screenX = (x << 4) - cameraX`
    so the lift is height/2 in Y, and there is NO height term in X.

`tools/re/emulift.py` drives both with a stubbed cell lookup to confirm which
byte comes back, since that was the load-bearing assumption. At height 120 that
is 60px of lift against the 132px we were applying -- a unit on raised ground
sat most of four tiles too far up-screen, which is why it looked like it was
standing on terrain beside it rather than on its own.

Our `kHeightScaleX_` was already 0, which the same instruction sequence
confirms is right.

Two other details fall out of the same neighbourhood. `0x511170` is the
BILINEAR sampler: it takes a sub-cell position (`sar $4` for the cell, `and
$0xf` for the fraction), reads four neighbouring cells and interpolates -- so
retail smooths the lift across a slope. Its arithmetic is not identical to
the floating-point `heightAbove`: it discards the fixed-point subpixel part
and truncates each signed interpolation separately (see the surface-height
notes below). And
the cell record really is 14 bytes with the height at `+4`, which the same code
shows twice over (`+0x4` and `+0x12` are the same field one cell apart).

### Surface movement height (2026-09-19)

The mover alone does not produce the complete next-tick ground position.
In the tick-14468 captured process, `4dc800` produces the next X/Z for units
672, 1307, 2866 and 3239 while leaving their old Y. A complete native tick
then updates their Y through `51b2a0`, called from `51e220` during each unit's
update, not at the simulation-batch epilogue. Memory-write hooks distinguish
upright writes at `51b26b`/`51b28f` from the support-plane write at `51b183`.
The complete native one-tick replay matched the next captured frame; this is
phase evidence, not a claim that the port's AI or complete movement matches.

`51b2a0` runs when unit flag `4000` is dirty or type flag `1000` (canhover)
is set. It clears the dirty bit, requires a mover and grounded mode 1, then
dispatches upright (`100000`), floater (`80000`), or model-supported height.
Upright units use `511170`; upright hoverers clamp that result to
sea minus unsigned-byte waterline. Floaters use sea minus waterline without
a terrain clamp. These branches clear the fractional half of Y.

`511170` reads signed integer X/Z from the 16.16 position. Cell coordinates
are integer position shifted right by four; fractions are the low four bits.
Each horizontal lerp truncates toward zero, followed by a separately
truncated vertical lerp. A missing full quad returns -1, including the last
map row/column. Floating-point interpolation or a single weighted sum is
not equivalent on descending slopes.

`51ad20` reads the first four indices of the root model's selection primitive
and rotates each X/Z support point by the native heading through `536143`.
Native model X/Z are the negatives of the authored 3DO coordinates, verified
for the four captured tilting types VERPULT, CRETORT, CREFIRE and ARAPULT.
Samples are at world X plus rotated X, world Z minus rotated Z. Any missing
quad leaves Y unchanged. Otherwise it averages corners 0/1 and 2/3 separately,
then averages those two results, truncating each division. The write replaces
only Y's integer half, preserving its old fractional half. The routine also
updates pitch/roll. Protocol 139 ports those outputs because `4d95f0` uses
signed pitch, shifted by 11 and clamped to -5..5, to limit ground speed.
The pitch denominator is the integer absolute Z span of support corners 0/3
scaled by pitchscale; roll uses the X span of corners 0/1 and bankscale.
Each scaled span truncates before `53612a` computes the angle. Native parser
defaults are bankscale 0.5, ground pitchscale 0.5, and flyer pitchscale zero.

Tilting hoverers have an additional corner adjustment gated by unit flags
`1000000` and absence of `1000`. It includes sea clamping and bobbing using
`53ff20`, which reads elapsed wall-clock time through `5ba760` and the runtime
speed scale. It is not a random draw. TARCAN is the shipped non-upright,
non-floater surface hoverer that needs this branch. Its corner adjustment is
now ported with an explicit clock input; World supplies simulation tick, not
retail's wall-clock animation phase. Both shipped TARCAN definitions have roadmultiplier and
watermultiplier 1.0, making its water flag irrelevant to those speed/turn-cost
multipliers. That does not remove pitch coupling: native controlled TARCAN
inputs on a slope of seven height units per cell produce pitch -2002 with
clock 0, versus -2059 with clock 26, crossing a speed-limit bucket boundary.
Standard uses HOVER3; Crusades uses HOVER2. Complete trajectory checks must
account for this clock input along with speed and acceleration.

`check_ground_height.py` executes original `511170` and the complete `51b2a0`
dispatcher on 15,000 controlled cases. The sole substitution supplies the
clock at `53ff20`; all height calculations execute natively.
Each upright, upright-hover, floater, non-hover support, and hover-bobbing branch
has 3,000 cases. Both Debug and Release kernels match, including signed/fractional
positions, map boundaries, random headings, waterlines, old Y fractions,
road/water priority, speed-dependent amplitude, and decay at 60 ticks.
Zero effective speed exercises native 0/0 conversion: the float-to-int helper
stores an int64 indefinite value and the caller retains its low 32 bits (zero),
rather than producing int32 minimum.
These are roster-independent numeric inputs, not standard/Crusades route
tests. Protocol 137 connects model support data, stored ground Y, and the
post-mover update phase to World. `check_world_height.py` additionally compares
asset-loaded World heights on the captured maps; see `pathfinding-port.md` for
coverage and remaining complete-trajectory gaps.

## Retail never lets a unit stand behind terrain (2026-09-12)

**Superseded by the 2026-09-17 movement capture.** The claim below confused
screen projection with navigation. Hunter 17 occupies and traverses flat cells
the port's seven-row projection heuristic marked impassable. Its terrain-only
grid allows them and no nearby feature supplies the block. The heuristic has
been removed; terrain slope/depth and explicit blockers remain. The old
projection observations below do not establish an extra movement exclusion.

A tall cell's painted face leans up-screen over the lower ground to its north,
so a unit stopping there is drawn inside the rock. Retail does not fix that at
draw time by painting the face back over the unit -- it keeps units OUT of those
cells. The occlusion block is a movement rule, not a rendering one.

This engine had that pass, and I deleted it earlier the same day for blocking
12% of Inner Circle and stranding an army (2 of 24 arriving). It was right all
along; the PROJECTION CONSTANT inside it was wrong. It leaned the face north by
1.1 px per height unit, where retail's figure is 0.5 -- proven separately off
`0x511140` and `0x426820`. At more than twice the real lean it condemned more
than twice the ground:

    Inner Circle     at 1.1: 4554 cells (12.4%)   at 0.5: 2294 (6.2%)
    Two Castles      at 1.1: 7238 ( 4.9%)         at 0.5: 4325 (2.9%)
    Angvir's Maze    at 1.1: 4458 ( 4.4%)         at 0.5: 2047 (2.0%)

Restored at 0.5 and writing into the SHARED overlay -- the per-class grids are
what `navFor()` actually hands a unit, and the legacy grids are not -- the army
test passes 24 of 24, and routing is unchanged at 97/78/31% for 40/80/160-cell
goals.

Two lessons in one bug. A constant that is merely "a bit off" can look like a
design being wrong: I concluded the whole pass was misconceived when it was one
number. And a rendering constant turned out to belong to the movement rules,
which is why it mattered twice over.

## Dynamic analysis: emulating icd routines (2026-09-12)

Static reading gets a routine's shape; it does not tell you whether YOUR port
behaves like it. Emulating the original function and diffing the two does.

The harness (kept outside the repo, in the session scratch dir) is about 90
lines of Python on Unicorn: map the PE's `.text`, `.rdata` and `.data` at their
virtual addresses, give it a stack and a heap, then call a function with the
stdcall / `__thiscall` conventions the binary uses. Sub-calls that need the live
game -- the cell query, the allocator -- are hooked and answered from a
synthetic grid, so a routine can be driven over test data the real game could
never produce on demand.

It observes; it copies nothing. CLAUDE.md is updated accordingly: the binary is
for reverse-engineering, static and dynamic, and never for lifting code or data
into the engine.

First result, checking `pathDirFromDelta` against `0x415040` over 81 deltas:
80 agree, and the single disagreement is the degenerate zero delta, where the
icd answers 5 and we answered 0. The search should never ask for a direction to
where it already stands, so it changes no behaviour -- but it took one run to
find something no amount of re-reading the disassembly had.

The harness lives in `tools/re/` (`emu.py` loads the image and calls into it;
`emusearch.py` drives the path search over a synthetic grid).

### What it found in the search, immediately

Driving `0x4146e0` over a 24x12 grid with a wall across the path and one gap,
hooking the cell query to answer from that grid, and logging every probe:

    c(2,6)...c(7,6)   cardinal march east, blocked by the wall at x=8
    A(7,5) B(8,7)     the cursors split; B steps diagonally into the gap row
    A(7,4) B(9,6)     B is through and back on the goal line
    c(10,6)...c(20,6) march resumes to the goal

27 cell visits for the whole thing. The probe log then gave the exact rule:

    A: probes 6 -> 7 -> 0  ACCEPT      (starts at the BLOCKED dir, rotates up)
    B: probes 6 -> 5      ACCEPT       (starts at the BLOCKED dir, rotates down)
    A next, dir=0: probes 6 -> 7 -> 0  (starts at dir-2, rotates up)
    B next, dir=5: probes 7            (starts at dir+2, rotates down)

So each cursor's FIRST probe begins on the direction that was just refused, and
only afterwards does the +-2 offset apply. Our port seeded both cursors with the
blocked direction, so their first probes opened at blocked+-2 and skipped
straight past it -- turning a two-step detour into a long wander. Seeding
`dirA = blocked-2` and `dirB = blocked+2` makes the offsets land on the blocked
direction and fixes it.

Measured before and after on Inner Circle, unit travelling toward a goal N cells
away, as a percentage of the distance closed:

    goal      +20    +40    +80   +160
    before    70%    97%    19%     9%
    after     95%    97%    78%    31%

and searches that used to abandon at the visit limit (20, 60, 80, 120, 160 cells)
now all arrive. Hours of re-reading the disassembly had not found this; the
emulator found it in one run, because it could show what the original DOES
rather than what it looks like it should do.

### The cost of it working, and the budget

At retail's own budget of 12000 work units a tick, the 8-AI benchmark costs
42.2ms/tick at ~2040 units against a ~19.5ms baseline -- over the 33ms a 30Hz
tick has.

I first put that down to units which used to wedge now marching, and reasoned
that budget-capped searches could not account for 20ms. That was wrong, and one
run settled it: at a budget of 1500 the same benchmark costs 23.5ms at ~2010
units. The searches ARE the cost. A work unit is not a cell query -- a trace
step charges 9 and can probe up to seven directions -- so query count runs well
ahead of the budget.

**Routing does not suffer for it.** Measured at 12000, 3000 and 1500 on Inner
Circle, the percentage of the distance closed is identical for 40-, 80- and
160-cell goals. The budget decides how FAST a search finishes, not whether it
can: the unit walks its straight segment meanwhile and the route lands a few
ticks later either way.

So `setupMatch` runs at 1500. That is 12.5% of retail's default and sits well
inside the 5%..1000% band retail's own quality setting covers (`0x4252e0`), so
it is a supported configuration rather than a deviation.

Two micro-fixes are also in, neither of which moved the number much: the
per-cell scratch is generation-stamped instead of cleared (it was zeroing ~110KB
per request), and the retry sweep does its cheap tests before the O(orders)
`currentLeg` scan.

## Unit animation: the engine drives very little of it (2026-09-12)

Audited against the Glide renderer and the 204 shipped COBs. The headline is
that retail's engine does NOT call the animations. It calls a small set of entry
points, and the SCRIPT runs its own state machines from there.

**How to enumerate the entry points.** `0x56c640` / `0x56c5c0` / `0x56c720` /
`0x56c4a0` / `0x56c680` are call-script-by-name; scanning .text for a
`push <string constant>` feeding any of them gives the authoritative list:

    Create  Killed  Dying  HitByWeapon  Activate  Deactivate
    StartBuilding  StopBuilding  StartCloaking  StopCloaking
    AimWeapon  FireWeapon  TargetCleared  SetMaxReloadTime  MoveRate
    TurnDirection  WindChange  setSFXoccupy  BeginFlight  BeginLanding
    QueryWeapon  QueryBlood  QueryBuildInfo  QueryNanoPiece  QueryLandingPad
    AimFrom  SweetSpot

Note what is NOT there: `walk`, `attack1`, `death`, `startbuild`, `OpenYard`,
`Go`, `Stop`. The strings "OpenYard", "CloseYard" and "Go" do not occur in the
binary at all. Those are called BY THE SCRIPT. `Create` ends by starting the
control threads (araarch: `START_SCRIPT 8/9/10` = MoveWatcher, MeleeControl,
StatusControl), which poll unit values and pick the animation themselves --
MeleeControl is the movement dispatcher, choosing walk_legs / walk_water /
walk_road / walk from unit values 28 and 34.

So a unit animates correctly only if `GET_UNIT_VALUE` answers correctly. That
is the real contract, not the list of scripts we remember to call.

**The getter table** is at `0x50d394`, dispatched at `0x50ceb0` as
`index = id - 1`, valid 1..46, anything else returning 0. Ids the shipped
scripts actually ask for, by use count:

    17 (282)  29 (142)  28 (122)  32 (106)  34 (94)  4 (85)
    18 (30)   33 (9)    27 (9)    46 (2)    30 (1)

We answer 17, 29, 28, 32, 34, 4, 27 (plus 1, 6, 9, which nothing asks for).
Still unanswered: 33, 46 (`(unit+0x130 >> 20) & 3`), 30.

### The build yard was hung, and the comment said it was fine

Id 18 is YARD_OPEN, used only by OpenYard/CloseYard -- the castle/factory build
doors. It is a BLOCKING handshake (aracastl OpenYard):

    SET 18,1                      request the yard
  loop:
    GET 18; NOT; JUMP_IF_FALSE done     leave only on NONZERO
    SET 19,1                      BUGGER_OFF: shove units out of the yard
    SLEEP 1500; SET 18,1; JUMP loop

We answered 0, with a comment claiming that was "benign/correct ... yard treated
clear". It is the opposite: 0 means REFUSED, so the script retries every 1.5s
for ever. And `Go` CALLs startbuild (the doors) and then OpenYard, so that
thread never returns and the state machine never reaches Stop/stopbuild -- the
doors open and never close.

Measured on the real COBs with our own VM (`tools/cobyard_test.cpp`, a one-off
probe since removed): answering
0 issues BUGGER_OFF 25-27 times in 40 simulated seconds and rises for ever;
answering 1 issues it 0 times. We now grant it unconditionally, since our sim
has no notion of a unit blocking a yard and we never act on BUGGER_OFF anyway.

**Update (protocol 125, then 255).** Yard transitions now wait for bodies on the
cells the requested state blocks (507ae0). Neither does retail act on
BUGGER_OFF: the setter (0x50d50e) only stores bit 3 of unit +0x12f, which GET 19
(0x50d1fd) and save/load (0x513528) alone read. A body in the door is never
shoved; BUILD stage 2's footprint test (401deb) holds the next output until it
leaves. See [factory-output-2026-10-12.md](factory-output-2026-10-12.md).

**Build sites (protocol 303, a deliberate deviation).** Retail never shoves a
body off a MobileBuild/FlyingBuild site either: the placement test (`507d10`
-> `507400`) has no side effect, and the order retries every 10 ticks, says
WAITFORTARGETTOCLEAR on the 7th and TARGETBLOCKED on the 31st, and is dropped
(`405824..405911`, `41f301..41f3e5`). Its build cursor (`429787` -> `507400`
with the local player and no moving-unit pass) refuses a site any visible body
stands on. At the user's request ours, on each of those retries, sends the
builder player's own idle and guarding units on the site just off it
(`World::clearBuildSite`; a guard keeps its guard under the move), and the
build order and cursor accept such a site (`canPlaceOrder`). Other players'
units and own units with other orders still make the builder wait as retail.
`pathblock_test` checks both modes.

**Room for the next output (protocol 309, a deliberate deviation).** The exit
step used to carry every output of a mobile producer out of the 12-cell area its
next output may be made in. A rally that starts at once does not: one that ends
close by, or lies out of reach (a ship rallied on land), leaves the output in that
area, and when it was full the producer stopped for good, repeat on and no error.
Retail cannot say what to do here, as its mobile builder has no rally and BUILD
stage 2 never shoves a body. With no room left, a mobile producer that has a rally
sends one idle unit away every 15 ticks (`World::clearProductionRoom`): the units
`yieldsToSite` allows, on the output's plane, the outermost in the area first,
away from the area's centre, at least the exit step's distance and past its edge.
One with a body right ahead of it stays, or it would only hold an order where it
stands; when that is all of them, the nearest idle unit beyond the area goes, which
opens their way. The move is tagged `productionExit`: held up once clear of where
it stood, it is done. A scripted pad is cleared with `clearBuildSite`. A producer
with no rally never searches, so it stops where it always did. Outputs on a short
patrol are never idle and do not yield: a patrol rally about 64 px away still stops
production sooner than the exit step did. A factory with a rally on its pad is
unchanged: retail's rule, nothing moves the body. `production_test` pins all of
this.

**A Navy Yard's door (a deliberate deviation, user request after 0.9.19).** How
retail moves a finished ship out of a Navy Yard (established, icd): GetBuilt
(`0x402220`) gives it the yard's QMove/QPatrol rally or else PARK (`0x407ca0`'s
ground branch, ships are not canfly) round the yard, exactly as a land factory's
output; the ring's point lies along the ship's own bearing from the yard, which for
the Creon Navy Yard (the only shipped one; its pad is 25 px south of its centre) is
straight out of its south door. Its boat scan (`4dba80`, `retailGroundScan`,
protocol 143) probes ahead in 16 px steps to `(footprintX+1)*16 + sightdistance`: a
shore cell or a body (grade below 4) within the probe counter's 160 (176 px ahead)
runs it at a third of its speed, a later obstacle, grade 4 or unexplored water at
two thirds. Nothing moves an idle body (BUGGER_OFF is never acted on, BUILD stage
2's footprint test holds the next site, above). So a ship leaving a door that opens
partly onto a shore crawls out at a third of its speed until its probe clears the
land: `navyyard_exit_test` measures a median 120 (Iron Clad) and 152 (Stern Wheeler)
ticks to clear the yard in Retail mode with two of the door's five columns facing
land, against 74 and 78 on open water (Legion: 31 and 46, against 35 and 81). That
time comes from the ported boat scan and is kept. Inferred, not observed in the
running original: a crowd of idle ships a few cells off the door holds the yard in
retail too, since nothing moves them. Observed in ours: the first 3x3 output wedges
against the crowd just out of the yard, the next is born behind it, and the yard
stops; an idle ship on the pad stops it before the first. On 0.9.19 eight idle Iron
Clads parked a body's gap apart 40 px off the door let the yard build 2 of 6 Iron
Clads or Stern Wheelers in 7 of the 8 combinations of mode and layout (Legion on
open water got all six Iron Clads out; Submersibles 5 or 6 of 6), and one Iron Clad
on the pad left the yard building nothing in all 12 combinations of mode, layout and
ship. Now `World::clearYardDoor` runs for every scripted production building every
15 ticks: while one of its ships is on its way out (in front of the yard on its PARK
round it, or still in the yard on any order) or its next ship's site was refused,
the owner's idle ships whose footprint is in front of the yard (its rectangle grown
8 cells on every side) move to just outside that area across the door's axis (the
axis from the yard's centre to the leaving ship), on their own side, the door's side
last (`pointOutside`: free, reachable, not where another mover was sent), tagged
`productionExit` so a held-up move ends once clear of where it stood. Only
water-domain ships of the yard's owner move, and only idle ones: other players'
ships, ships with any order (guards too), land units and flyers stay as before, and
a land factory is unchanged. The crowd at the door now costs the yard at most 3.4%
of its build time for six ships at four times the worker time (clear-door runs:
1,483-2,201 ticks), the ship on the pad at most 8.4%. `navyyard_exit_test` (both
modes; open water and a door facing a shore; Submersible, Iron Clad, Stern Wheeler;
no crowd, eight at the door, one on the pad) fails 42 of its 60 checks without the
change.

**A land factory's door (the same deviation, user request after 0.9.19).** Retail
treats a land factory's door exactly as a Navy Yard's (established, icd, re-read
2026-11-03): BUILD stage 2 (`0x401c20` stage 2, the call at `0x401deb`) admits the
next site only through `0x507d10(type, self 0, origin, factory +0x130 & 3,
allowMoving 0)`, and a refusal (`0x401df2`) sleeps the mission `rand(15)+7` ticks
(`0x401df4..0x401e01`) and sets mission flag 2; nothing is moved. BUGGER_OFF's setter
(`0x50d50e`) masks bit 3 of unit `+0x12f` in and out; of the 42 accesses to that byte
only GET 19 (`0x50d1fd`) and save/load (`0x513528`, `0x513cf8`) read it (the masked
tests at `0x4d4b9b`, `0x4d4bcb`, `0x506355`, `0x5139ec` and `0x5206e1` are bits 0, 1
and 2). The finished unit gets GetBuilt's rally or PARK (`0x402220`, the same ground
branch of `0x407ca0`), and a body in its way only clamps and slows it
(`0x4dbe2c`). So in retail a new unit waits on the pad for the body there, and once
out presses on, or re-rolls its PARK point, round a crowd; no idle body ever steps
aside. Inferred, not observed in the running original: one idle unit on a land
factory's pad stops it for good there too. Observed in ours on fixes-0920 before the
change (`landdoor_exit_test`: Aramon Barracks, Veruna Keep, Taros Dungeon, Creon
Academy; a small and a large output, six each; both modes): one idle unit on the pad
left every factory building nothing in all 16 combinations; a dozen idle units two
rows deep 24 px off the door held the Academy to one or two of six in all four of its
combinations, while the other factories' outputs found a way round. `clearYardDoor`
now serves every scripted production building: the outputs' plane decides who moves
(ships at a yard, ground and hover units at a land factory), and at a land factory
only the half of the area on the leaving output's side of the factory's centre is
cleared, so units parked behind it stay (at a yard the half area kept a Legion Stern
Wheeler out of a door onto a shore for 300 ticks, so a yard keeps the whole area).
Each finished output now remembers its factory (`Unit::builtBy`, unhashed: set once
at completion, from hashed state): an output in front of its factory on its way out (on its PARK round it, on a
plain move such as its rally, or still on the pad) is the door's traffic; an older
output guarding, building, patrolling or attacking there is not, or it would clear
the area for as long as it stayed; and the factory's own parked outputs move only off the pad, or,
under the Retail navigator, when one stands in the cells round a later output of the
factory (2 deep) that is held up by a body (refusal streak 2) on its PARK. That rule
moves any other idle unit of the owner there too, past the area: crowd ships moved
just outside the area held an Iron Clad there, and the yard's own parked Stern
Wheelers wedged the next one off a shore. Legion gives each output a spot of its own
out of the door's lane and has no such rule: moving its parked outputs put them
back in the lane (`legion_park`). The first Navy Yard rule moved every own parked
unit in the area each time the next one left: 87 re-orders of parked outputs over
`navyyard_exit_test`'s 36 runs and 127 over `landdoor_exit_test`'s 64; now 2 and
26. Owner only, allies
included: nothing in the sim orders another player's units, as `clearBuildSite` and
`clearProductionRoom` move only the builder's own. Units with any other order,
units with production of their own (a Zhon builder), flyers and other players'
units stay; a Zhon producer itself is mobile and keeps `clearProductionRoom`. A
crowd of guards at the door held a factory (Legion: the Academy built none of six,
its guards standing on the refused site just outside the pad), so by the owner's
decision the owner's guards now step aside too, at land factories and the yard,
unless fighting (`World::doorGuard`): the move goes ahead of the guard, which
resumes after it, once per factory event (`Unit::doorSerial`: next even number per
finished output, odd per refusal episode; the guard keeps the factory and serial it
stepped aside for, so a key earned at one factory never skips it at another), and a
refused site's own footprint is cleared with the pad (`Unit::refusedSiteX/Z`). With
guards the Legion Academy lets all six out. With the change the crowd costs the factory at most 13%
of its clear-door build time and the unit on the pad at most 14% (six outputs at four
times the worker time, 576-1,306 ticks clear-door); medians to clear the factory with
no crowd, Retail / Legion, small then large output: Barracks 57/61 and 92/124, Keep
42/45 and 83/99, Dungeon 33/43 and 62/80, Academy 25/27 and 20/35 ticks.
`landdoor_exit_test` (quick label, 128 checks) fails 40 of them on the code before
the change.

Two false starts worth recording, because both are easy to repeat: thread counts
do NOT work as the test observable. A factory legitimately keeps a thread open
while active (Creon's `Go` ends with `START_SCRIPT FactoryFun`, the machinery
loop), and the deactivate path has a handshake of its own -- so "a thread is
still running" flags healthy units. Counting BUGGER_OFF reads the protocol
itself and is faction-independent.

### Known remaining gaps

  * `MeleeAttack` was in our fire-animation fallback chain and is defined by
    ZERO of the 204 COBs -- it could only ever fail. Removed. The melee
    animation comes from MeleeControl, which Create starts.
  * "We call walk/attack1/startbuild DIRECTLY where retail lets the scripts
    choose" -- OVERSTATED, corrected below. For `walk` it is false outright.
  * `QueryNanoPiece` -- RETRACTED, see below. I claimed our sparkle "does not
    originate where retail's does". It does. Chased it down and the opposite is
    true.
  * Unit value 30 is still answered with 0, and staying that way deliberately.
    Its handler (`0x50d2e0` -> `0x4dc1f0`) is flyer-only: it bails unless
    UnitDef+0x260 bit 0x800 (canfly) is set, then computes
    `typeField * unit[+0x12b] / 16`, picking the type field (`+0x172` or
    `+0x16e`) off a state bit -- a signed, scaled velocity of some kind. The
    only consumer in the shipped data is ONE script in ONE unit (lifbird's
    FlightControl), which compares it against -50 to pick a third animation
    branch; 0 takes the hover branch, which is the right look for a bird at
    rest. Not worth pinning down the exact quantity for one NPC bird's dive
    pose, but that is the shape of it if it ever matters.

## The build sparkle: QueryNanoPiece is vestigial (2026-09-12)

Retracting a claim I made in the animation audit above: that our build effect
"originates in the wrong place" because retail queries the builder's nano piece
and we do not. Chasing it down, retail queries that piece and THROWS THE ANSWER
AWAY.

The build mission is `0x401c20`. It does:

    lea  eax,[ebp-0x18]
    push eax ; push edi          edi = the BUILDER
    call 0x4dd530                QueryNanoPiece -> piece -> 0x4dd250 -> world xyz
    mov  ecx,[esi+0x16]          esi = the MISSION, +0x16 = the unit being built
    push 0 ; push 1
    mov  ecx,[ecx+0xc0]          ...its drawable
    call 0x4eec20                play the effect ON THE TARGET

`[ebp-0x18]` -- the nano piece's world position -- is written there and never
read again anywhere in the function, and `0x4dd530` has no other caller. So the
position is computed and discarded. This is TA-1997 vestigial: TA drew a
nanolathe BEAM from that piece, TA:Kingdoms does not.

What actually draws is `0x4eec20` -> `0x4f1430`, a rand-driven particle spawner
run against the emitter hanging off the target's drawable (`+0x174`), one
particle per build-mission tick, the mission rescheduling itself as it runs.
`0x4eec20` is generic -- ten mission handlers use it -- so it is "play this
drawable's effect", not a build-specific thing.

So the sparkle belongs on the UNIT BEING BUILT, which is what we do. Building a
builder-to-target beam would have been inventing an effect retail does not have,
which is exactly what the retail-first rule exists to prevent.

One real divergence remains, unverified: we sprinkle the BUILDER as well as the
site (`gameview_render.cpp`, "the worker end"). The build mission emits only on
the target -- the builder appears there solely as the argument to the discarded
nano query. I have not removed ours, because one call site is thin evidence that
no other path lights the builder up.

## Following up the animation audit: two of my own claims were wrong (2026-09-12)

**Unit value 33 is the signed TURN RATE, in 16-bit angle units.**
SuperDynamicWheelSpinner (9 wheeled units) reads it, tests it against +910 and
-910, and uses the sign to spin the two sides of the vehicle at different rates
-- the differential of a vehicle in a turn. 910 is what fixes the scale: it is
5 degrees in 16-bit angle units (65536/360*5 = 910.2). We now answer it from the
snapshot's heading against its previous-tick heading. Verified by running
aracan's real script: 2 distinct wheel rates going straight, 4 when turning.

**Unit value 46 is "have I got a target".** Only HolsterControl (verbers,
vercrus) reads it and only ever against 0 -- zero holsters the weapon, non-zero
draws it. Retail's is `(unit+0x130 >> 20) & 3`; nothing reads the other bits.

**The builder does not sparkle.** Removed. Retail's build mission plays the
effect on the unit being BUILT and on nothing else; the builder's contribution
is its StartBuilding animation. See the nano-piece section above.

**RETRACTION: "we drive walk directly instead of letting the scripts choose".**
This is false for every shipped unit. Our mover branch already hands off to the
script's own threads when the COB has MoveWatcher / MeleeControl / DemonControl,
and the number of shipped units that have a `walk` cycle but none of those three
is ZERO. The direct-walk fallback is dead code against this data set. I wrote
the claim from the call sites without checking which units could reach them.

The rest of that claim is narrower than I made it sound:

  * `attack1` / `fire` are FALLBACKS after `FireWeapon`, which is a real engine
    entry point, and exactly one shipped unit (targarg) has attack1 without
    FireWeapon.
  * `startbuild` is called directly rather than through retail's
    Activate -> RequestState -> Go chain. Visually equivalent: Go's contribution
    is CALL startbuild (the doors, which we run) plus CALL OpenYard, and
    OpenYard moves no piece -- it is the pathfinding yard handshake, which our
    sim does not model. Deliberate, too: the direct call avoids a VM reset that
    would kill a building's Create ambients (flags, smoke).

The lesson for the next audit: "the engine calls X directly" is a claim about
CALL SITES, and the question that matters is which units can actually reach
them. Check the data before writing it down.

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
  The fill arrives over the first 30 ticks (one second), not all at setup.
- No real window is needed; the dummy driver renders correctly.

## Mana is a double, and the economy is floating point (2026-09-15)

Asked while converting our remaining floats: is mana fixed point in retail, or an
integer? Neither -- it is 64-bit floating point, and this was worth chasing
because two plausible-sounding wrong answers came first.

The evidence is the end-of-game stats screen. At 0x500586 and 0x5005ba the
builder loads two values with `fldl` (a 64-bit load) from +0x18 and +0x20 of the
player's stats object and passes each to the ftol helper at 0x5d3d54 before
storing the result as the displayed integer. The labels resolve through
0x617ca8 / 0x617cac to "Mana Produced" and "Excess Mana". So the STORED
accumulator is a double and the integer is only the rendering.

Two more sites agree:

  * The accumulate is a double read-modify-write: `faddl 0x18(%eax)` followed by
    `fstpl 0x18(%eax)` (0x425d5a, 0x429d45, 0x4cb61c, 0x4ea1cc).
  * The affordability check at 0x46e85f loads an integer cost with `fildl` and
    subtracts the pool with `fsubl 0xd1(%edi)` -- again 64-bit.

Two wrong answers to avoid repeating:

  * "Retail stores no float in its unit state" is true, but it was measured over
    the mover/unit region only and says nothing about the economy. Binary-wide
    there are 718 float stores to non-stack destinations.
  * The player table at +0x2404 (stride 0x110) really is touched by 463 integer
    movs and zero FP instructions -- but its first field is a POINTER, null-checked
    and then dereferenced (0x9b(%ecx), 0xea(%edi)). Counting instructions against
    a pointer table says nothing about the fields it points at. The stats copies
    `total_mana` (+0x2c) and `mana_wasted` (+0x34) ARE integers, which is what
    makes the wrong conclusion easy to reach: they are the snapshot, not the pool.

This also explains the range problem from the porting side. Our Fixed is 16.16
in an int32 and saturates at 32768; mana routinely runs far past that. Retail did
not use fixed point here either, so matching it is not a compromise.

## Timers are integer ticks, and how to tell a field's type (2026-09-15)

Asked while converting the last of our sim floats: for each remaining field, is
it a float, 16.16 fixed point, or an integer? Reading the load opcode answers
half of it -- `flds`/`fldl` load a float, `fildl` loads an integer -- but what
says which KIND of integer is the scale the engine multiplies by afterwards.
Two constants, both .rdata doubles, decode nearly everything:

    0x5f25d8 = 0.03333... = 1/30       -> a count of 30Hz TICKS
    0x5eba78 = 1.52587890625e-05 = 1/65536 -> 16.16 FIXED POINT

`tools/re/emufields.py` confirms both by emulation rather than by eye: it plants
known values in a synthetic struct and runs the engine's own display sequences
over them. A reload field holding 90 comes out as "3.000000" seconds; a position
field holding 6586368 comes out as 100.5 px.

So the weapon dump at 0x617770 ("Damage = %i, Reload Time = %f, ManaPerShot =
%f") reads its reload from +0x9c as a 16-BIT INTEGER (`mov 0x9c(%esi),%cx`) and
scales it by 1/30 purely to print it. Retail stores a cooldown as a tick count.
The same shape covers the other countdowns.

Worth noting what this does NOT say. The def also holds genuine floats --
ManaRechargeRate is `readFloat` into +0x1a6 and MaxMana into +0x1a2, both stored
with `fstps` -- so retail's type data mixes all three representations, while
maxdamage at +0x1be is `readInt`. The rule is per field, not per struct.

## Two more field types, and a build accumulator that is 16.16 TICKS (2026-09-16)

Finishing the float sweep, two fields had no settled answer. Both were decided by
running retail's own code (tools/re/emu.py), not by reading it.

**Aura adjustments are genuine floats.** [AdjustArmor]/[AdjustAttack]/[AdjustJoy]
parse at 0x4c1487 into 20-byte sub-objects on the unit def (+0x1c6, +0x1da,
+0x1ee) via 0x5182b0, whose layout is:

    +0x04  Adjustment         readFloat, default 1.0   (fstps -- a float)
    +0x08  Radius             readInt
    +0x0c  EdgeEffectiveness  readFloat
    +0x10  AffectsEnemy       readInt != 0

Emulating the default-init at 0x518320 writes 0x3f800000 to +0x04 -- the IEEE
pattern for 1.0f. A 16.16 one would be 0x00010000. So a live aura multiplier is
a float in retail, which is what ours already were.

**Build progress is 16.16, and it counts TICKS.** At 0x406ce1 the engine computes

    buildtime / (workertime * (1/30)) * 65536

and passes it through the ftol helper to store an INTEGER at +0x52. The 65536 is
the giveaway: this is a fixed-point tick count, not a plain one, so a partial
tick of work survives instead of being truncated away. Emulated to be sure:
buildtime 170 / workertime 1 comes out as 5100 ticks = 170 seconds exactly, and
170/2 gives 2550 = 85s.

That also pins the units, which were ambiguous from our side: `buildtime` is
SECONDS at workertime 1, and workertime divides it.

Nearby, for the record: buildcost (+0x20e), buildtime (+0x212) and workertime
(+0x21a) are all `readFloat` in the def. As with the rest of retail's type data,
the representation is per field -- ints, floats and 16.16 sit side by side in one
struct -- so the only reliable way to read one is to find where it is CONSUMED
and look at the scale it is multiplied by.

## Retail's own data types, field by field (2026-09-16)

Asked which of our types still differ from retail's. The binary has FOUR TDF
readers, not the three named above: `readInt @0x543190`, `readFloat @0x5431c0`,
`readString @0x5432c0`, and `0x5431f0`, which parses into 16.16 FIXED POINT and
stores the raw dword. That last one is how the movement constants are read --
maxvelocity (+0x162), acceleration (+0x16a), brakerate, watermultiplier (+0x16e)
and roadmultiplier (+0x172) -- and the debug dump reads them back with `fildl`
scaled by 1/65536, confirming the format.

Where we still differ:

  * 16.16 in retail, float in ours: maxVel, accel, brake, roadMult, waterMult.
  * INT in retail, float in ours: maxHp (maxdamage), sight (sightdistance),
    radar, turnRate, turnInPlaceRate, buildDist, transportDist, minCloakDist,
    leash (maneuverleashlength), cruiseAlt, maxSlope, maxWaterSlope,
    maxWaterDepth, minWaterDepth; and on Weapon: range, damage, aoe
    (areaofeffect). `damage` is corroborated by the dump's "%i" format.
  * Narrower than retail: Unit::mana is a float where Player::mana is a double
    and the affordability check at 0x46e85f uses `fsubl`.

Already matching: storage/income (mogrium*), buildCost, buildTime, workerTime,
healTime, maxMana, manaRegen, cloakCostMove, projVel (weaponvelocity), reload
(reloadtime), edge (edgeeffectiveness), and the live aura multipliers
(Adjustment, a float defaulting to 1.0 -- see the aura section).

None of this is a determinism hazard: type data is parsed once from identical
bytes on every peer. It is a fidelity gap.

### RESOLVED (2026-09-16): converting only HALF the group is what cost arrivals

An earlier attempt moved the movement constants to 16.16 per-tick and left
turnRate as float rad/s. That cost crowdbench's opposing columns 32/32 -> 23/32,
and a counter on the mover's refusal branch showed why the symptom appeared
where it did: refusals went 56,650 -> 88,518 in opposing columns and
5,571 -> 15,333 at the chokepoint, while uncongested scenarios were untouched.

The cause was the half-conversion itself. `bamFromRadians(turnRate * dt)` is an
identity round trip -- the FBI value is COB angle units, Bam is the same unit,
so scaling to rad/s at load and back per tick returns the input -- but it goes
through float in both directions. With speed per-tick and the turn still doing
that round trip, the two disagreed at the step test.

Converting turnRate to the stored integer removes the round trip, and opposing
columns does not merely recover, it improves sharply: t50 65.6 -> 25.2 with
searches 669 -> 230, all five scenarios at full arrival. The lesson is the
general one for this port: a representation change that is an identity on paper
still has to be carried all the way through, because the intermediate state is
where the two conventions meet.

Both unit bugs found on the way are worth keeping: arcDist divided a per-tick
speed by a per-second turn rate, and the UnitType struct defaults for
accel/brake are px/s^2 values used only by synthetic test types -- replacing
them with the FBI default of 0.5 made them 30x stronger and collapsed stopDist
from ~26px to 0.87px.

## Two tests on how close our movement now is (2026-09-16)

Asked how closely pathfinding matches retail, two things were flagged as ours
rather than retail's. Both were measured rather than argued.

### The route horizon already matches -- we were closer than claimed

Retail's navigator holds at most two or three intermediate points and re-anchors
about every 6 ticks (0x4e5150; the count at +0x10c is tested against 2 and 3).
We install a route, which looked like a divergence. It is not: the string-pull
collapses routes to almost nothing, and a histogram of what actually gets
installed says

    scenario            1 wp   2    3    4    9-16
    opposing columns    100%   -    -    -    -
    open field          100%   -    -    -    -
    group order         100%   -    -    -    -
    chokepoint           81%  19%   -    -    -
    serpentine maze      44%  12%  23%  12%   8%

so outside a synthetic maze essentially every installed "route" is a single
waypoint plus the destination -- retail's shape, reached by a different road.
Truncating the install to a 3-point horizon is a literal no-op: byte-identical
crowdbench output, same search and work counts, because there is nothing to
truncate. Do not "fix" this divergence; it is not one.

### The A* fallback is inert on open ground and load-bearing in a maze

Disabling the escalation entirely (kDetoursBeforeAStar effectively infinite)
leaves four of five crowdbench scenarios BYTE-IDENTICAL -- it never fires there
-- and the harness hash on Inner Circle is unchanged too. The maze is the whole
of its effect:

    serpentine        with A*   without
    t50                  72.8     226.
    travel              x1.89    x6.19
    searches              263      747

All twelve units still arrive without it, so this is path QUALITY, not
correctness. And it is not hypothetical on real maps: instrumented over a 10
minute 8-AI game on Ulasem Arena, the fallback escalated 37 times.

What is NOT established is whether retail escalates at all. Nothing like it has
been found in the binary, but that is absence of evidence: the tracer is a bug
algorithm and would be just as bad in a maze, so "retail is bad here too" is the
likelier reading. The reason it has not simply been deleted is that the measured
cost is a 3x arrival time and a 6.19x path on the one layout that exercises it,
and that trade deserves a decision rather than a default.

## The steering layer: why our units stack and will not go around each other

RE'd 2026-09-16, static analysis only, after a report that units pile up and
drive into obstacles instead of around them. Nothing here was implemented yet
when it was written -- this section exists so the port has a spec before any
sim change lands.

> **Status:** the unit-aware grades (parked 0, moving 1, a same-way mover the
> search may pass, 2 for a body the search may not pass) have since been
> ported into the search (`src/sim/pathsearch.h`); the tracer now routes
> around bodies. See [pathfinding-port.md](pathfinding-port.md) for what is
> verified and what still differs.

### There are TWO pathfinding subsystems, and we ported one

The boundary tracer at `0x4139d0`-`0x416xxx` is the one this document already
covers and the one `src/sim/pathsearch.*` ports. It routes around TERRAIN and
knows nothing about units.

There is a second, separate cluster -- the mover at `0x4dfd70` and the cell
raters at `0x5086xx`-`0x5094xx` -- that we never ported at all. That is the
unit-aware layer, and its absence is exactly the reported behaviour: bodies
stack because nothing makes a moving unit solid, and routes drive into crowds
because the tracer cannot see units.

### Historical rating notes (superseded feature/body interpretation)

The following early notes misidentified the feature table as units. Cell +8
and the 0x140-byte table at game+0x19edc describe map features; their 0x20000
flag is clearability through replacement chains, not unit motion. Actual
unit occupancy is cell +0 and the 0x138-byte entity table. For current native
comparisons and live versus cached grading, see
[pathfinding adapter audit](pathfinding-port.md#active-scope-pathfinding-parity-2026-09-19).

### Cells are rated 0..7, and units are part of the rating

`0x509020` returns a passability rating, NOT a cost -- higher is better. The
same structure appears independently at `0x404ff9`, which is how the reading was
confirmed rather than assumed.

    7   default / open ground        (the initial value at 0x50904d)
    6   clamped when a terrain flag is clear   (+0xd bit 0x80, at 0x509348)
    4   clamped by the slope tests             (0x509318 / 0x509322)
    1   cell held by a MOVING unit             (0x50917c)
    0   cell held by a PARKED unit -- blocked  (the xor eax,eax exits)

The occupant is read from the cell record: word at `+8` is the occupant id,
`0xffff` means empty, and `0xfffe` means "this is a tail cell of a multi-cell
body" -- follow the back-pointer built from the bytes at `+0xa`/`+0xb` and the
map stride at `[0x62d55c]+0x19e98`. Byte `+6` is the height used by the slope
test. Units live in a table at `[0x62d55c]+0x19edc` with stride `0x140` and a
count at `+0x19ec0`; the flag word is at unit `+0x13c`, where bit 5 (`0x20`)
means "a unit occupies this cell" and bit 17 (`0x20000`) means "it is moving".
Every exit path reports its rating through `0x509390`.

So a parked body is a wall and a moving body is passable at 1/7 the rating of
open ground. Body-blocking a bridge is a real tactic, and a crowd is something a
route bends around rather than through, without either being special-cased.

### The rating is consumed as a per-unit LOCAL WINDOW, rebuilt every step

`0x509020` has exactly one caller (`0x508fc5`, inside the wrapper `0x508f30`),
found by scanning for `E8 rel32` rather than by linear disassembly -- a naive
linear sweep desyncs on data and reports zero callers, which is what it did at
first and what made this look indirect.

Above that, `0x508e50` rates a cell together with its orthogonal neighbours and
requires `> 4` and `>= 6` of them: a corner-clearance test, so a unit will not
cut a diagonal through the gap between two bodies. `0x508cd0` is the same shape
against a second rater at `0x5088f0`. `0x508e20` initialises the working set --
it clears `0x121` dwords at `0x6405d4` and stores its two parameters at
`0x6405d0`/`0x6405cc`.

The mover at `0x4dfd70` (reached from `0x4c0e22`) is what ties it together: each
step it fills a local buffer at `ebp-0x1ec` with the ratings over the unit's
footprint window, dispatching on footprint size (`<= 5`, `<= 11` at `0x4e0260`)
between a batched fill (`0x508db0`) and a per-cell one (`0x508cd0`), writing
each result through `0x4dfe40`. The hard "may I stand here at all" test is
`0x507d10`, which has nine callers.

This is the piece `src/sim/sim.h` means when it says "there is no local obstacle
avoidance in the steering". Retail rebuilds a small unit-aware cost window per
mover per step and steers on it; we steer straight at the order point.

### What we have, what we lack

We already have more than the old comment implies: the tracer is ported and
`PathService` owns a queue, a bounded pool of concurrent searches and a per-tick
budget. The gap is specifically:

  1. cell ratings that include units (parked 0, moving 1, open 7);
  2. the per-unit local rating window and the corner-clearance test;
  3. solidity for MOVING units -- ours records only stationary ones, and the
     separation relaxation resolves overlap after the fact so it cannot stop
     anyone, which is precisely why they stack.

### NOT established -- do not build on these

`src/sim/sim.h` asserts retail affords hard solidity through "continuous
short-hop replanning, randomised repath delays and an age-weighted per-player
path budget". Those three were NOT confirmed in this pass. They may well be
real -- the claim came from somewhere -- but no routine for any of them has been
identified, and the implementation should not assume them. The consumer of the
rating window (the function containing `0x4c0e22`) also resisted a prologue
scan and has not been read.

Establishing those is the remaining work before the rating port is designed,
because they are what decides whether hard solidity flows or gridlocks.

## BiLinearFilter: units and unit shadows only, default off (2026-10-06)

Static analysis of `KINGDOMS.icd`; this is what the engine's Texture Filtering
**Units** level follows (until 0.7.28 it was a single Bilinear Filtering toggle).

- **Storage and default.** `HKCU\Software\Cavedog Entertainment\Kingdoms\VisualOptions\BiLinearFilter`,
  a bool at VisualOptions +0x13 (options container `[0x62d558]`+0x18). The load
  routine `0x499e80` first calls the reset `0x49a030`, which clears it
  (`0x49a05a mov [esi+0x13],bl`, bl = 0), so a missing key means **off**. The
  shipped `v4readme.txt` agrees: it "affects how shadows and units are scaled up
  after being rendered at a reduced resolution" and "defaults to being off"
  because many cards drew a purple halo.
- **What reads it.** Apart from load/save and the Visual Options menu, only the
  3D-hardware unit pass: `0x4ebdcc` (stretch a unit or unit-shadow image that
  was rendered at scale 2 or 4 back to full size: `SetMode(2)` = bilinear when
  +0x13 is set, restored with `SetMode(1)` at `0x4ebed7`), and `0x4ec9ba` /
  `0x4ecb43`, where auto ShadowScale shrinks shadows further when it is on.
- **Glide path.** `grTexFilterMode` is called only from the driver's
  SetRenderState emulation (`0x5b8088` mag, `0x5b80e3` min), reset to point
  sampling at `0x5b7db0`. The only other bilinear draws are the fog-of-war
  overlay (`0x4982d5`, unless FogDebug) and line-of-sight lightning
  (`0x52d18a`), neither of which reads the option. Terrain, features, other
  effects, minimap, fonts, GUI and cursor are always point-sampled; the software
  renderer never reads +0x13.
- **The menu** greys the checkbox outside Direct3D (`0x534240` gates on
  renderer type 3), but nothing in the unit pass checks the renderer, so a
  stored value applies on Glide too.

The engine renders units at full resolution, so there is no reduced-size image
to stretch. Its Units level applies the same scope directly: unit model-texture
pages and unit shadow silhouettes sample LINEAR; everything else is nearest.
Off is retail's default.

**Everything is a non-retail extension.** Retail had no camera zoom, so its
point-sampled terrain, scenery and effects were never magnified; this engine
zooms to 4x, where every terrain or feature texel becomes a 4x4 block. The
Everything level therefore also samples terrain (chunks, fallback tiles), feature
sprites and shadows, effect/projectile sprites and the shadows.gaf ground
shadows LINEAR, but only above 1.5x zoom (off again below 1.4x; one constant,
`client/texturefilter.h`). At 1.0-1.25x a nearest texel is one or two screen
pixels and filtering would only soften the view. Off and Units render
pixel-identically to the old toggle's off and on.

## The InfoPanel shows only what has data -- a DELIBERATE departure (2026-10-13)

Retail's bottom bar (the `.gui` InfoPanel) is a fixed layout that is always
fully present: UnitInfo1 (the selected unit's UnitImage portrait, name,
HealthBar and ManaBar) on the left, ActionText (the status), and UnitInfo2 (the
conjured target's name, HealthBar and ManaBar) on the right. With nothing
selected it still shows an empty black portrait box and four empty gauges.

Ours keeps every element at its `.gui` spot, so nothing moves as the selection
changes, but draws only what carries data (`drawGuiInfoBar`):

- the portrait only with something selected -- never the empty black box;
- the left HealthBar only with something selected, the left ManaBar only for a
  pool (a single unit with `maxmana > 0`, or a multi-selection with casters);
- UnitInfo2's name and progress gauge only while there is a conjure target (a
  site under construction or a build queue's head); its ManaBar never (it never
  carried data);
- ActionText stays centred between the two gauge pairs' SPOTS, drawn or not.

A multi-selection's front portrait is one of the type strip, the same size as
the rest in every layout state (full, shrunk, folded, "+K"), anchored at the
single-unit portrait's right edge (`client/selportraits.h`). A single selection
draws exactly as before. The fallback stone bar follows the same strip rule.

## Start positions: the k-th occupied row takes the k-th start (icd, 2026-10-14)

Asked because a player put an AI in slot 8 with slots 2-7 empty, expected it at
StartPos8, and got StartPos2. Retail does the same.

- **Rows are player records.** The battle room's eight rows are the first eight
  of ten player records (`gs+0x2404`, stride `0x110`). The PlayerName click
  (`0x49f2b2`) reads `record[row]` kind `+0xea` through `0x49d890` and edits it
  with `0x49ce50` (set kind) or `0x49d0a0` (clear). Clearing a row zeroes its
  active dword and moves no other row, so empty rows between seats exist in
  retail too. Kinds 1 and 2 are this machine's human and AI, 3 a remote player.
- **Skirmish** (`0x4d2e70`, vtable slot `0x5f26dc`, map mode 2). Fixed starts walk
  the ten records and call `0x4d3200(player, k++)` for each one that is active,
  of kind 1, 2 or 3, with a player number (`+0xeb`) other than 10. `0x4d3200`
  looks up the OTA special numbered k and stores k at `+0xec`. The `.ota` parse
  (`0x4c8fc0`..`0x4c903a`) stores `StartPosN` as N-1, so the k-th occupied row
  gets StartPos(k+1). Random builds the list `0..n-1` over the same rows and
  `std::random_shuffle`s it (`0x4d44a0`; with n <= 2 a coin flip decides whether
  to shuffle at all), so a random game also uses only StartPos1..n.
- **Multiplayer** (map mode 3). The host fills a per-row table at `gs+0x3030` in
  `0x4ea950`: fixed gives the k-th occupied row k, other rows 0xff; random uses
  the same shuffle of `0..n-1`. It sends the table out, and each machine spawns
  its own players at `+0xec` in `0x4d2e70`'s mode-3 branch. Rows flagged
  `+0x50 -> +0x9b & 1` take no start.
- **Emulated** (`tools/re/check_start_positions.py`: `0x4d2e70` with `0x4d3200`
  hooked, and `0x4ea950` with the network sends stubbed) over synthetic
  records. Human row 1 + AI row 8 gives StartPos1 and StartPos2 on both paths,
  and all 255 skirmish seatings of the eight rows and all 128 multiplayer ones
  (host in row 1) follow the rule. The host's table for rows 1 and 8 is
  `1:1 2:- ... 7:- 8:2 9:- 10:-`.

Ours: `setupMatch` hands the k-th used slot the k-th start, the same rule, for
skirmish, multiplayer and replays alike (`start_positions_test`). One difference
is left as it is. Our Random shuffles all of the map's starts, so two players on
an eight-start map may land on any two of them. Retail's only permutes StartPos1
and 2.

Start locations **By slot** (`GameOptions.randomStarts` 2) is an engine extension
with no retail counterpart: slot N takes StartPos N whichever rows are empty, and
a slot past the map's last start takes the lowest start no seated slot claims by
number, then a ring spot (`sim::startSpotsFor`). Fixed stays the retail rule
above and remains the default.

## healtime is a worker time: self-repair (icd, 2026-10-14)

`healtime` is not seconds per hit point. Retail reads it as a float into
`UnitType+0x21e` (`0x4bff94`), next to `buildtime` at `+0x212` (a nonpositive
value reads as 100) and that value's reciprocal at `+0x216`. Its one consumer is
the per-unit update at `0x51e152..0x51e1bc`: when healtime is nonzero, the unit
is alive and not dying, its construction is finished (`+0x108 == 0`) and it is
below maxdamage, then on every eighth tick (`tick & 7 == 0`) it calls the shared
repair routine `0x429d90` on itself, UNPAID, with `healtime * 0.26666664`
(eight thirtieths of a second of work). `0x429d90` multiplies that by
1/buildtime, makes it hit points as a 16.16 product of maxdamage, adds the
unit's carried remainder (byte `+0x10e`: bits 8..15 of the last sum; bits 0..7
are dropped on every call) and hands the whole points to `0x51a140` as damage
type 12, which `0x51a240` adds to the 16-bit hit points, clamping at maxdamage.

So a unit repairs itself as if it were its own builder with worker time =
healtime: it regains `maxdamage * healtime / buildtime` hit points a second.
The data is authored on exactly that reading: in the standard data 117 of
the 153 mobile units are exactly healtime = buildtime/240 (full health in four
minutes: araarch 0.52083/125, arasword 0.4125/99) and 28 of the 51 buildings
exactly buildtime/480 (eight minutes: the Keep 3.17083/1522, Barracks
1.25/600, both towers, the gate); Crusades, 86 of 147 and 22 of 50. The
monarchs explain the oddest numbers in the Crusades balance: araking is
buildtime 10 / healtime 0.0417 in the standard data and 100000 / 216.67 in
Crusades, i.e. 1/240 and 1/461.5 of its health a second. Read as seconds per
point, the same fields gave the Crusades Keep 0.32 HP/s instead of 49.1 and the
Crusades monarch one point every 3.6 minutes. Ported as
`retailSelfRepairPoints`/`retailAddHitPoints` (`sim/retailconstruction.h`) and
checked bit-exact against the emulated call path, carry included, by
`tools/re/check_self_repair.py`; `duration_test` holds the rates.

**What actually makes Aramon buildings hard to kill is the priest, and that is
retail.** `arapries`/`araprie2` carry `[AdjustJoy]` adjustment 250, radius 200,
edgeeffectiveness 0.6. The parser stores Adjustment raw (`0x5182b0`; AdjustJoy
passes flag 0 at `0x4c1552`, so it is additive). `0x51d3e0` runs the Joy list
only when `tick % 30 == 0`, counting every eligible unit in range and then
calling `0x51f7b0`, which repairs each through the same `0x429d90`, PAID, with
`250 * falloff / N`. A damaged building therefore regains
`maxdamage * 250 * falloff / (N * buildtime)` a second while the owner can pay:
the Crusades Arrow Tower up to 4333 HP/s, the Keep 3871, the Barracks 7151. In
the reported game (Zhon against an Aramon AI, Crusades, Legion) the AI's
buildings took 548,000 damage and were healed 341,500 by priests against 1,500
by their own healtime. One Divine Lodestone spent 75 one-second windows above
16000 of its 17092 HP while taking more than 300 damage a second (up to 1800),
because a priest returned 1066 a second. Our `tickHealAuras` matches that
formula, so nothing there changed.

## The enemy-hover cursor: a unit that can move never shows "too far" (icd, 2026-10-19)

Selector `0x4dd780` (sole caller `0x521de6`, inside the selected-unit
aggregator `0x521cd0`, which keeps the lowest slot) handles default hover
(mode 1) over an enemy by re-entering as ATTACK (mode 3) when the unit has
`canattack` (UnitDef+0x264 bit 0x20). Mode 3 then branches on the unit's mover
pointer (unit+8):

- **Mobile** (mover present, locally `!isStructure()`): NO range test. Attack
  (slot 1) unless the active weapon (`0x519ac0`) cannot hurt the target: damage
  `0x531e50` <= 0, notargetcategory (UnitDef+0x9a), a stoning/freezing weapon
  class (vfuncs +0x1c/+0x20) against cantbestoned/cantbefrozen (UnitDef+0x268
  bits 2/4), toairweapon (WeaponType+0xc8 0x4000) against a target that is not
  airborne, or noairweapon (0x8000) against one in flight mode 2. Those are
  TooFar (slot 3). Melee is not special-cased.
- **Immobile** (no mover): the same immunity gates, then minrange `0x52adf0`
  (`0x5306a0`) and damage+inRange `0x52ad60`: TooFar unless in range.

This mode-1 re-entry is the LEFT-click interface's (options+5 = 1, the code
default set at `0x499940`); with `UseLeftClickIface=0`, the shipped setting,
options+5 = 0 jumps to `0x4de03b` before the attack test and default hover over
an enemy is Red for any selection (see "The pointer cursor" below). Mode 3 itself
-- the armed ATTACK -- is as described here.

The selector is display-only: order dispatch (`0x5214b0`) does not call it. Our
HUD had applied the range test to every unit, so a selected archer or swordsman
showed TooFar over any enemy out of reach. `tools/re/probe_cursor_enemy_hover.py`
runs those branches natively; `cursorCanAttackEnemy` (src/client/cursorrange.h)
ports them for the cursor only (`cursor_enemy` test, shipped types). The
right-click on an enemy still orders every selected unit to attack, as the
order code does not consult the selector; `World::attack` then rallies a
factory's output at the target, keeps an out-of-range tower's order until the
target comes in reach, and drops the order for a unit with no weapon.

## Group move spread: flyers keep their place, ground converges (icd + emulation, 2026-10-30)

The order code `0x5214b0` spreads one click over the selection itself; there is
no formation shape and no per-axis clamp:

1. **Centre** (`0x521510..0x5215ca`): the truncated integer mean (`idiv`) of the
   selected units' whole-pixel positions (the high words of unit+0x68/+0x70),
   leaving out a selected unit under the cursor, which gets no order.
2. **Reach** (`0x5215cd..0x5215dc`): `3000 * n` squared pixels, n the units
   counted: sqrt(3000 n) is 55 px for one unit, 122 for 5, 173 for 10, 274 for
   25, 548 for 100.
3. **Who keeps an offset** (`0x5217b1..0x521857`): only a unit whose resolved
   mission descriptor has bit 0x2 at +0x11 -- the only reader of that bit. Set
   on `VTOL_Move`, `VTOL_Move_Formation`, `VTOL_Move_FormationStandby` and
   `VTOL_Patrol`; clear on `Move_Ground`, ground `Patrol` and
   `Move_Ground_Formation`. Such a unit goes to the click plus (its position -
   centre) -- the offset keeps the sub-pixel part -- when
   `(ox*ox >> 32) + (oz*oz >> 32) <= 3000 n` (whole squared pixels per axis),
   else to the click exactly. Every other unit goes to the click.

An emulation of `0x5214b0` (probe `~/tak-tmp/retail-checks/group-move-friendly/
probe_group_offsets.py`, outside the repo) confirms it: five units 40 px apart
all reach (1000,1000) as `Move_Ground` and keep -80, -40, 0, +40, +80 as
`VTOL_Move`; 80 px apart the two end flyers (160 px out) take the point; a 5x5
ground block gets one destination; 300 random flyer groups match the formula
with no mismatch.

Ours (protocol 305): **flyers** take this rule exactly
(`src/sim/retailflyerspread.h`, `FlyerGroupCentre`), on a right-click move and
on the armed MOVE and PATROL buttons and, since protocol 307, on a
fight-move (its flyer legs are VTOL_Move, which has the bit: see "Air
traffic"; before, everyone took the point). The client computes it from its
frame in 16.16 integers; a Legion net game sends the click as an explicit
order -- `OrderShape::Offsets` (right-click) or `OrderShape::FlyerOffsets`
(armed move/patrol, fight-move) -- and `applySelectionOrder` gives the flyers
the same goals from the hashed positions. **Ground units keep our own rule** (a deliberate
difference): a right-click gives each the click plus its offset from the
selection centroid clamped to +-60 px per axis (Legion: the click itself for
the surface movers it plans, `legionSharedClick`), and armed MOVE/PATROL give
the point. Before, flyers were clamped to +-60 px like ground units and the
armed buttons sent them all to the point. Not ported: the unit-under-cursor
exclusion (our right-click on an own unit is the friendly-unit order below).
Tests: `issue_selection_test` (`flyerSpread`: retail's probe groups, a
sub-pixel centre, mixed groups on move/patrol/fight, both modes),
`selection_order` and `convoy_test`.

## Right-click on a friendly unit: per-unit, Guard by `canguard` (icd, 2026-10-27)

The default-order resolver `0x4de530` (action mode 1, jump table `0x4df87c`)
runs once per selected unit. With `UseLeftClickIface` clear (options+5, the
right-click interface; the string's only reference is `0x499891`) mode 1
tries, in order: ATTACK (re-entered as mode 3) over an enemy when the type has
`canattack` (UnitDef+0x264 bit 0x20); HELPBUILD when `0x519e60` allows it;
REPAIRUNIT when `0x519d40` does; a load (`0x519f50`); then FOLLOW_GROUND
(VTOL_FOLLOW for a flyer), the guard mission, when the target is allied and the
type has `canguard` (UnitDef+0x264 bit 0x40, set by the FBI parser at
`0x4c0685`); then resurrect/animate/reclaim at the point; then MOVE_GROUND when
the type has `canmove` (bit 0x100). The guard test never looks at a weapon, so
a builder over a healthy unit guards it, and in a mixed selection over a
damaged unit the builders repair while the others guard. Every shipped mobile
builder has `canguard=1`; the five gods do not. With `UseLeftClickIface` set,
the same mode has no guard step (an own selectable unit resolves to nothing:
the click selects it).

The hover cursor for that mode (`0x4dd780`, right-click interface branch
`0x4de03b`) never shows the defend glyph: over a friendly unit it is Select
(slot 15) for an own selectable unit, else Grn (18); slots follow the
`cursors` GAF order at `0x4bdc0e` (1 attack, 2 airstrike, 3 toofar, 4 capture,
5 defend, 6 repair, 7 patrol, ..., 14 move, 15 select, 17 red, 18 grn, 19
normal). Since 2026-11-03 ours matches: see "The pointer cursor" below (before,
our hover showed the left-click interface's Repair over a site or damaged unit a
selected builder could work on).

Ours, by `src/client/friendlyorder.h` (`friendly_order` test): per unit,
repair when a mobile builder (not repeating production) and the target is
damaged, else Guard when `canguard`, else -- since 2026-11-03, as retail -- a
Move to the point (a repeating builder's Move sets its output's rally). The
whole mode-1 resolver now runs per unit in `src/client/clickrules.h`; see
"What a click orders" below. An ally's unit is offered the guard but
`World::guard` accepts only its own side's units.

## The pointer cursor: the right-click interface's four hover cursors (icd + emulation, 2026-11-03)

Once a frame `0x528040` sorts the pointer (`0x528430`: main view, minimap, or
neither) and `0x525d30` decides: build placement (mode 14) in the main view
draws the ghost and sets Normal -- an invalid site shows only on the ghost; any
interface area outside the main view and the minimap is Normal; otherwise
`0x521260` picks ONE hovered unit (the nearest drawn sprite, or on the minimap
the nearest dot within 2 px) and the aggregator `0x521cd0` runs the selector
`0x4dd780` for each selected unit of the local player except the hovered one,
keeping the lowest slot. With that list empty: Select over an own selectable
unit, else Normal. Neither stage reads Shift, Ctrl or Alt.

In the right-click interface (`UseLeftClickIface=0`, what shipped installs
run, and what our right-click orders follow) nothing-armed hover (`0x4de03b`)
has four results: Select over an own finished selectable unit; Red over an
enemy, whatever is selected; Green over an ally's unit or one of ours that is
not selectable -- under construction, the site the selected builder is building
included (emulated), or carried; Green over ground holding a body a selected
unit can resurrect/animate or a feature a selected reclaimer can reclaim;
Normal elsewhere. Attack, TooFar, Repair, Load, Unload, Revive, Reclaim and
Move appear only with a command armed. Fight-move (`UnitCommand MoveFight`)
is armed MOVE plus the bit `game+0x17633` 0x400, which the selector never
reads, so it shows armed MOVE's cursors: Repair, Load, Defend over friendly
units, Revive over a body, Move elsewhere (see "What a click orders").

Ours (`src/client/cursorrules.h`, test `cursor_rules`): those rules for hover
and placement, the interface/minimap split, armed ATTACK per unit with the
lowest slot (over ground or a friendly unit: see "Fire at a point"), armed CLEAR
showing Reclaim over a reclaimable feature, armed MOVE and fight-move showing
Repair/Load/Defend over friendly units (their clicks do those), and fight-move
otherwise as an untinted Move.
Kept ours: the watcher's Select over units, the right-drag box's Reclaim (our
box only reclaims), a rally holder's Attack, armed MOVE and fight-move showing
Move over a body (the click moves), armed GUARD not testing the flyer
rule (ours guards a flyer from the ground), CLEAR's Revive over a body that is
not a reclaimable feature, and Normal for placement over the minimap (our
minimap click moves the camera; retail's selector gives FindSite there). The
minimap with nothing armed is always Normal (our left-click there moves the
camera and our right-click is a plain Move, so no dot is selected or
attacked), and an armed ATTACK there takes the dot as the hovered unit: an
enemy dot is attacked, ground or a friendly dot is a fire at that point, as in
the main view. The left-click interface is not offered.

## What a click orders: the resolver per unit, on release (icd, 2026-11-03)

Static reading (S) unless marked I (inferred). Retail issues every order through
`0x5214b0`, which runs the resolver `0x4de530` for each selected unit except the
hovered one (`0x521562`, `0x521625`) and voices one random selected unit
(`0x5215df`). The hovered unit is ONE unit for cursor and click (`+0x17624`):
among the units whose rotated footprint contains the pointer (`0x520530`), the
smallest (height/2 + depth) x width of the type's model box (UnitDef
`+0x152/+0x156/+0x15a`, set at `0x4c14c4`) wins, a tie to the earlier entry
(`0x52133e..0x521381`); on the minimap the nearest dot within 2 px
(`0x5213ae..0x52141f`). Main-view clicks act on the RELEASE (`0x5281ad`,
`0x527bf0`); the minimap acts on the press (`0x52835b`). An armed left-click
issues only when the cursor is an order cursor (`0x527dd0`, `0x525ce0`).

- Mode 1 (right-click): an enemy -> ATTACK when `canattack` and the target
  resolves, nothing when it does not (`0x4df215`), MOVE without `canattack`;
  an ally -> HELPBUILD, REPAIR, pickup (single weaponless transport), FOLLOW,
  MOVE; ground -> MOVE (a loaded transport too: no unload); a body or feature
  -> RESURRECT, RECLAIM, MOVE. A passenger over its own transport FOLLOWs it.
- Mode 2 (armed MOVE, `0x4df0b9..0x4df1d2`): the ally branch as above over an
  own or allied unit, MOVE elsewhere; its cursor shows Revive over a body while
  the click moves.
- Fight-move (`UnitCommand MoveFight`, the name dispatcher `0x4b0e40`) is
  mode 2 plus a bit: it presses the MOVE button (`0x4b0df0` sends the "MOVE"
  control a 0x201/0x202 click, which arms mode 2 through `0x429950`, clearing
  `game+0x17633` 0x400 and 0x8) and then sets `game+0x17633 |= 0x400`
  (`0x4b0f4a..0x4b0f53`). That word is the pending command's flags, the same
  word that holds the placement bit 0x80 and the Shift-keeps-armed bit 0x40.
  Neither the resolver `0x4de530` nor the selector `0x4dd780` reads it (its
  only readers in the order path are the order code's two tests), so every
  target resolves and shows as under armed MOVE: HELPBUILD, REPAIR, pickup or
  FOLLOW over a friendly unit, MOVE over an enemy, a body or ground (Revive
  cursor over a body, the click moves). After the resolver, `0x5214b0` gives
  the resolved mission response mode -1 only when the bit is set, the mode
  byte `game+0x1762c` is 2 and the mission is Move_Ground, VTOL_Move or QMove
  (`0x521bf4..0x521c5b`; on the flyer group-spread path VTOL_Move alone,
  `0x521867..0x521892`); every other mission goes as it was resolved. It is
  one-shot like any armed command: after the click `0x526620` keeps the mode
  (and so the bit) armed while Shift is down (`0x40`) and otherwise
  `0x5265d0` writes mode 1 directly, which leaves the 0x400 bit stale but
  inert (only mode 2 reads it, and mode 2 is armed only through `0x429950`,
  which clears it). The minimap's armed press runs the same `0x527dd0` ->
  `0x5214b0` path on the nearest dot, so it behaves the same there; an armed
  MOVE box (`0x527d00` -> `0x4dd6a0` mode 2: 0) selects, fight or not; Shift
  and Ctrl queue and toggle as for any order (`0x4d7a30`, the bit is not read
  there). The acknowledgement plays the resolved mission's sound (`0x5217a5`
  calls `0x50aa30` with the mission's +0x10 byte, before the 0x400 test), so
  a fight-move voices as armed MOVE does, never as an attack; ours does too.
- Mode 3 (armed ATTACK, `0x4de65f..0x4de7cc`): ground or a friendly unit is a
  fire-at-point (SUPPRESS for a ranged ground unit, AIRSTRIKE / AIRTOGROUND
  for a flyer, nothing for anti-air units, nothing for melee units unless a
  clearable feature stands there; see "Fire at a point: SUPPRESS").
- Mode 5 (armed GUARD): the cursor needs `canguard` and (the unit flies or the
  target does not); the resolver (`0x4dec78`) has no flyer rule, so a mixed
  selection with one flyer guards a flyer with every unit.
- Mode 12 (armed CLEAR, `0x4decd6..0x4deef6`): RESURRECT first, else RECLAIM;
  the cursor (`0x4ddcf4`) shows Reclaim over a raisable reclaimable body.
- The right-drag box (`0x520e30`, `0x527d00` for armed boxes): with exactly one
  weaponless transport and no attacker, reclaimer or raiser selected, it loads
  every own unit in the box; else each attacker attacks the visible enemies in
  the screen box (3n random draws); a unit with no attack order and
  `canreclaim` clears the box (RECLAIMAREA); with exactly one enemy a unit
  without a weapon does nothing. Armed ATTACK / LOAD / CLEAR boxes do the
  attack, load or clear step alone; any other armed box selects.
- Minimap placement (mode 14, `0x527eb0`): MOBILEBUILD at the minimap point
  with the sound "oktobuild", gated by the placement flag `+0x17633` 0x80 that
  only the main-view ghost (`0x429690`, `0x4297a3`) sets -- stale on the
  minimap (I) -- else "notoktobuild".
- Modifiers: Shift appends and keeps the mode armed; Ctrl inserts at the front;
  Shift/Ctrl on the same order kind within 16 px of a queued one removes it;
  Alt is never read.

Ours (`src/client/clickrules.h`, `click_rules` test with a cursor parity check;
`src/client/gameview_clicks.cpp`): the same per-unit resolver, on release, with
the one picker (our hit region's screen area stands in for the model box).
Kept or chosen differently, by the owner's decisions: our ground-move spread;
an own transport a passenger fits in is boarded (the rest guard it);
Alt+right-click unloads a loaded transport; a rally holder's Move / Attack set
its output's rally; armed ATTACK on ground or a friendly is the fire at the
point (Cmd::AttackGround, cursor Attack where a unit acts); the box
attacks every visible enemy once, nearest first, and a unit without a weapon
always clears; minimap placement is checked at the minimap point and falls
back to our clear-then-build; a left-click on an enemy or ally with nothing of
ours selected selects it for inspection (with a selection it deselects, as
retail); Ctrl removes from the selection and the box keeps our army filter.
Fight-move ('F', our attack-move, which is retail's MoveFight Move with
response mode -1) resolves exactly as armed MOVE and turns only a resulting
Move into a fight-move: over a friendly unit it helps build, repairs, boards,
picks up or guards it with armed MOVE's cursors; over ground, an enemy or a
body, and over a friendly unit it can do none of those for, it fight-moves to
the point under the Move cursor; a rally holder's rally step fights (QMove);
its box selects. (The 2026-11-03 click pass left F a plain fight-move over
friendly units, reading the 0x400 tests as something other than the pending
command; corrected the same day.) Our client builds F in and does not read
`Keys.TDF`'s `UnitCommand` bindings (the owner's file binds `LOWER_F` /
`UPPER_F` to `UnitCommand MoveFight`; the shipped one binds no key to it):
reading them would let such a file rebind it, not done here.
Where retail's cursor and click disagree (armed MOVE over a body, armed CLEAR
over a raisable reclaimable body, Airstrike over an enemy it cannot hurt, Attack
over an empty right-drag box) ours shows what the click does.

The order modifiers (C9, sim since 2026-11-03). Retail's order call `0x4d78a0`
links a new mission at the head of the list (`0x4d7750`, ahead of the running
one, which sleeps behind it) when Ctrl is down (`[ebp+0x24]`, `0x4d7a09`) and at
the tail (`0x4d77f0`) for Shift; Ctrl+Shift is Ctrl. Before that, `0x4d7a30`
walks the list from the head (`+0x60`, next `+0x66`) for a mission of the same
kind (`+4`), the same target unit (`+0x16`) when the order names one, and a
point (`+0x22`/`+0x2a`) within 16 px on both axes (`d + 0x100000 <= 0x200000`)
when it has one, and removes the first match (`0x4d6ad0`) instead of adding
(`0x4d7a5b..0x4d7ab2`) -- a toggle. Retail also drops a Shift order for a builder
repeating production (count >= 10,000,000, `0x52162d..0x5216c5`); ours does not:
a producer, or a mobile builder repeating production, takes Move / Patrol /
Attack / fight-move as RALLY orders (rally rule), and Ctrl and the toggle work on
that rally list. Ours (`Command::queue`, `net/lockstep.h`): `kQueueFront` (2)
inserts, `kQueueToggle` (4) marks a player's click so that the toggle applies --
the AI and scripts send 0/1 only, so their queued routes are never toggled
(`World::insertOrderFront`, `World::removeQueuedMatch`, `queue_modifiers` test).
The interrupted leg is rewound to its queued state (its route and what its run
pushed ahead of it go; its mission restarts at stage 0 on a fresh controller);
a builder whose site is down keeps its build leg, which remembers the site
(`Order::resumeSite`; retail resumes the MobileBuild mission itself). When the
leg runs again (`World::resumeBuild`) it becomes a HelpBuild on the site while
that stands unfinished, ends if the site was finished meanwhile, and otherwise
places the site again: our matches run the legacy construction path, where an
unattended site loses hp at its build rate and goes at zero, and Ctrl takes back
an unstarted site (`cancelBuilds`), so a HelpBuild in the leg's place used to
lose the build (I: what retail's resumed mission does once its site has gone is
not established). A Ctrl Load front-inserts on the
carrier as well as the passenger (its pickup is the carrier's mission), a tower
takes Ctrl on its own list, and Ctrl on an empty list is the plain order. A
Shift/Ctrl Patrol on a patrol point takes back only that leg: retail's patrol
initializer appends the anchor (return) as a mission of its own, so the toggle's
first match is the outbound leg alone (I, from `0x4d7a30`'s match rule; not
emulated) -- ours keeps that.

## Drag boxes: world-anchored corners and the 32-pixel test (icd, 2026-10-21)

Retail keeps a map drag box as two WORLD points (game state `+0x175fc..+0x17614`,
16.16 x/y/z each) and projects them to the screen every time it needs the rectangle
(`0x5202c0`: subtract the camera, lift by height/2). A pan during the drag therefore
leaves the press corner on its ground. The "is this a box, not a click" predicate
(`0x527bf0`) is true when the drag flag is set and either a press held 25 game-clock
units (`0x53ff20`) has elapsed, or the two corners differ by 32 or more world pixels
on EITHER axis (retail has no zoom, so world and screen pixels agree). Our client
(`src/client/areadrag.h`) keeps the world anchoring and the 32-pixel distance, but
requires it on BOTH axes and from a real pointer movement, and drops the held-time
rule, so a twitch or a pause never becomes an area clear.

## Cloaking spends the unit's own mana (icd, 2026-10-26)

The cloak step is `0x401160`, called for every unit from `0x51e1c2` right
after the self-repair check. In order:

1. Regeneration: when the unit is complete (`+0x108`, the construction
   remainder, is 0) it adds ManaRechargeRate (type `+0x1a6`) to the unit's
   pool at `+0xd8` through `0x401100`, which caps it at the pool's maximum
   (`+0xdc`).
2. Cloak: if the cloak order bit (`+0x130 & 0x200`) is set and `+0x130 & 0x400`
   is clear, and the tick counter (`game+0x19f44`) has reached `+0xcc`, the
   cost is cloakcostmoving (type `+0x19e`) when the unit is moving
   (`+0x130 & 0xc`) and cloakcost (`+0x19a`) otherwise. When cost <= the pool,
   `0x4010d0` subtracts it from `+0xd8` and `0x51e4d0(4,1)` sets the cloaked
   bit (`+0x114 & 4`). When the pool is short, nothing is paid,
   `+0xcc = max(+0xcc, now + 90)` and the unit decloaks (`0x51e4d0(4,0)`).
   While `+0xcc` lies ahead, or the order bit is clear, the unit decloaks.

So the player's store never pays for cloaking, and nothing reaches the
resource object's request total (`+0x10`), so the mana bulb never shows it.
A unit with no pool and a positive cost can never cloak (every shipped
cloaker has maxmana 20-5000). The loader (`0x4c01a1`) reads cloakcost and
scales it by 1/30 to a per-tick cost, then passes that already scaled value as
cloakcostmoving's DEFAULT before scaling the result by 1/30 again: a unit that
omits cloakcostmoving (zonspide, the Trapdoor Spider) pays cloakcost/30 per
second while moving. Both costs, like ManaRechargeRate (`0x4bffe3`), are per
second in the FBI and per tick in the engine.

Ours matched none of this: it charged the PLAYER's store (`debitMana`), kept
cloaking while the store could pay, had no recloak delay, and defaulted
cloakcostmoving to cloakcost. `World::tick` now takes the cost from
`Unit::mana` after the unit's regeneration, and `Unit::cloakBlockedUntil` is
`+0xcc` (hashed when set).

### What else blocks cloaking (icd, 2026-10-26)

Every writer of `+0xcc` sets `max(+0xcc, now + N)` on the unit doing the work;
the cloak step then keeps it visible until the tick counter reaches it. The
handlers come from the mission registry (rows of code, handler, 13 bytes,
name; stride `0x19`, from `0x5eb7f0`):

| Site | Task (handler) | Stage | N |
| --- | --- | --- | --- |
| `0x52af41` | weapon step `0x52ae90` | each shot: the slot's release bit (`+0x1a & 0x10`, set by `0x50d5b8`), just before `0x530220`, the only caller of the release | 600 |
| `0x405bc8` | MobileBuild `0x405560` | every work tick, after `0x429af0`, paid or not | 300 |
| `0x4060bb` | HelpBuild `0x405cf0` | the same | 300 |
| `0x406576` | Reclaim `0x406250` | every 2-tick work wake that leaves countdown > 0 | 300 |
| `0x406e45` | Resurrect `0x4068b0` | every channel tick that leaves work > 0 | 300 |
| `0x4076ae` | RepairUnit `0x407190` | every work tick (target not full, not moving), before `0x429d90` | 150 |
| `0x420477` | VTOL_Resurrect `0x41fe70` | as Resurrect | 300 |
| `0x420b02` | VTOL_Reclaim `0x4207d0` | as Reclaim | 300 |

FlyingBuild, VTOL_HelpBuild and VTOL_RepairUnit write nothing, nor does a
factory's or a mobile producer's output (GetBuilt `0x401c20`). The other
`mov [reg+0xcc]` stores in `.text` belong to other structures, unit creation
(`0x511b58`, 0) and the save loader (`0x513606`). In the per-unit update
(`0x51d3e0`) missions (`0x4d4520`) and the weapon step (`0x51d9d4`) run before
`0x51de30`, which calls the cloak step: a block written on tick T makes the
unit visible from tick T, for exactly N ticks.

Port: `World::blockCloak` at each of these points -- `World::fire` after the
manapershot test, `tickConstruction` (and the restored-save MobileBuild work
stage, beside its own copy `activityDeadline`) and `tickRepair` for ground workers,
`tickReclaim` after a wake with countdown left, and the revive channel. It is
written only for cloakers, the step's only reader. Our cloak step runs before
combat and revive, so `blockCloak` also clears `cloaked` at once; that is the
same visible span.

The step's other inputs, checked at the same time:

- **Moving** is `+0x130 & 0xc`, the MoveRate tier (bits 2-3) that `0x4db350`
  writes after the mover: 0 only when speed and turn are both 0 (or the unit
  is refused/carried). Ours tested speed above 3 px/s; it now reads the
  tier our `notifyMovementRate` keeps for the unit's script (speed != 0 for a
  unit without one). Our tier is the one from the unit's previous update,
  since our cloak step runs before our mover.
- **`+0x130 & 0x400`** vetoes cloaking, but no store in `.text` sets that bit
  (every `mov [reg+0x130], reg` was traced back for an or-mask, and the save
  loader skips it): nothing to port.
- **mincloakdistance** (`+0x22a`, default 80 when cloakcost > 0, `0x4c17c4`)
  is read only at `0x4d6542`, in `0x4d6530`, called from `0x4d6340` under the
  renderer `0x4fc5b0`: it builds a circle of that radius (`0x4d5c40`,
  radius x 2pi x 0.125 segments) round a cloaked unit for display. No enemy
  near a cloaked unit decloaks it, and nothing but the cloak step clears the
  cloaked bit (`0x51e4d0(4, x)` has no other caller). Our "an enemy that close
  decloaks" rule is removed.

Every other mana drain was checked for a cost retail charges to the unit:
weapon `manapershot` already spends the unit's pool (`0x530220` -> `0x4010d0`
at `0x5302d6`; ours in `World::fire`); construction, repair, production and
the healing aura go through the player's resource object (`0x429af0`,
`0x429d90`), as ours do. No other `0x4010d0` caller touches a unit pool except
the debug drain at `0x4280e0`.

## Revive costs no mana; the allocation paces it (icd, 2026-10-26)

The ground task is RESURRECT, `0x4068b0` (registry row `0x5eb9e4`: code
`UNITMISSIONCODE_RESURRECT`, name `Resurrect`; jump table `0x407168`, seven
states). `VTOL_Resurrect`, `0x41fe70` (row `0x5ebe18`, jump table `0x42079c`),
is the same machine with flight states in front. The registry row layout is
code, handler, 13 bytes, name (stride `0x19`); the other handlers named below
come from the same table.

- Stage 0 picks the kind: `+0x56 = 0` when the corpse is resurrectable
  (`0x497100`) and the caster's type has canresurrect (`+0x264 & 0x1000`,
  loader `0x4c0752`); `+0x56 = 1` when `0x496fd0` holds and the caster has
  cananimate (`+0x264 & 0x20000000`, `0x4c0778`).
- Stage 3 (`0x406c2e`) takes the output type -- the caster's animatetype
  (`+0x24c`) for an animate, the corpse's unit (its feature name up to `_`)
  for a resurrect -- and sets the work counter `+0x52 = ftol(buildtime x
  65536 / (workertime x 0.03333333))`, times 0.3 (`0x5ebaa0`) only for a
  RESURRECT (`+0x56` clear). The constant at `0x5eb780` is the double
  0.03333333, not 1/30. `_ftol` keeps the low word of a 64-bit `fistp`, so a
  counter past 2^31 wraps: araking (workertime 10) resurrecting a
  buildtime-100000 vermage starts at 1603273293, about 24464 ticks, and the
  stage-4 `sub` is a plain 32-bit one.
- Stage 4 (`0x406d3b`) subtracts `ftol(allocation x 65536)` every tick, the
  allocation being the owner's resource object `+8` (stored / last demand,
  capped at 1). While work is left it sets the caster's `+0xcc` cloak block
  to `max(+0xcc, now + 300)` and sleeps one tick; at <= 0 it advances.
- Stage 5 (`0x406e79`) is the end. An active owner of kind 2 (computer)
  with a player number under 10, the player record's `+0xe3` flag (seated)
  and the caster's `+0x130 & 0x20` (set at creation by `0x511b18`, cleared
  only by a scenario path) abandons it, unraised, when the allocation is under
  0.7 (`0x5ebb10`). Otherwise it creates the unit: full health for an animate
  (`+0x10c = +0x1be`), a tenth (at least 1) for a resurrect.

Nothing in it pays or requests mana: it never calls `0x429af0` or `0x429d90`
and writes neither the resource object's store nor its request total, and
its completion notifier `0x429990` does not either. So a revive is free, a
starved economy slows it (an empty store with construction asking stops it),
and it never shows in the mana bulb.

Ours charged the output's buildcost (x0.3 for an animate) across a channel of
buildtime / workertime seconds -- the 0.3 on the wrong kind: an animate took
0.3 of the time and a resurrect the whole of it. `World::tickAbilities` now
keeps retail's 16.16 work counter (`Unit::reviveWork`, hashed while
channelling) from `retailReviveWork`, takes `Player::allocation()` x 65536 a
tick, pays nothing, and applies the computer-player rule through
`computerOwned`. Since 0.9.12 the revive is an order, as retail's: see the
next section. Stage 3 sets the work counter in `World::tickReclaim` and the
channel (stage 4) takes its first allocation in the same wake, as retail's
stage 3 returns 1 and the dispatcher runs stage 4 at once; the end (stage 5)
follows the last subtraction in the same wake in both.

The end creates the unit with `0x512060(player, type, ..., +0x28 = 1)`
(`0x406f0c`), which refuses a type at its totalallowed (`+0x136`, live units
of the player), a computer player's ai-profile limit for the type (`0x409890`,
also checked when stage 3 starts) and a player at its unit limit
(`game +0x14e7c` against the player's `+0x24ec` count). Refused, the revive
ends unraised -- `UNITCHATTER_NOMOREUNITS` (`0x604ac0`) when the profile limit
was not the cause -- and the corpse stays (only the success path removes it).
Ours spawned past both limits; since protocol 302 the end refuses at the unit
limit and at totalallowed (the ai-profile limit is the server AI's, not the
sim's, and is not checked).

## Bodies are raised and cleared only by order (icd, 2026-10-30, 0.9.12)

Every unit task is created by name through `0x4d4bf0` (a case-insensitive
lookup in the sorted registry `0x62db84`, rows `0x19` bytes, filled from
`0x5eb7f0` by `0x4d86a0`: class 20 `Resurrect` `0x4068b0`, 21 `Reclaim`
`0x406250`, 22 `ReclaimArea` `0x406670`). The names the callers pass --
`RESURRECT` `0x615cbc`, `VTOL_RESURRECT` `0x615cc8`, `RECLAIM` `0x613094`,
`RECLAIMAREA` `0x615c8c` and the other `VTOL_*` -- are referenced only inside
the order resolver `0x4de530` (and one GUI button-name lookup, `0x4b0cbc`).
An exhaustive cross-reference search (probe scripts kept outside the repo):

- **Mode 1** (a right-click, `0x4df1e2..`) tries, after attack, help-build,
  repair, load and guard: RESURRECT when the type has canresurrect
  (`+0x264 & 0x1000`) and `0x497100` holds, then RESURRECT when it has
  cananimate (`+0x264 & 0x20000000`) and `0x496fd0` holds, then RECLAIM
  (`0x800`, `0x496ea0`), then MOVE_GROUND. Each test resolves the feature
  under the point (`0x50e660`) and, unless the call says otherwise, needs the
  point's cell explored by the player. A flyer gets the VTOL_ name.
- **Mode 12** (`0x4decc4..`, the CLEAR button, and the ReclaimArea children
  `0x406727` ground / `0x420ca5` VTOL) is the same, gated on canreclaim, and
  with no point falls back to RECLAIMAREA.
- **The kinds.** `0x497100` is the feature type's `resurrectable` (flag word
  `+0x13c` bit `0x800`, loader `0x4941fe`) and the object's `+0x5d` bit
  `0x20`; `0x496fd0` is `animatable` (`0x1000`, loader `0x494223`) and bit
  `0x40`. Neither looks at an owner: a caster raises any side's body, and
  the unit rises as the caster's. Every shipped animatable corpse is also
  resurrectable (73 against 256 sections in the extracted data), not the
  other way round.
- **Resurrect's stages 0-2** are RECLAIM's: stage 0 calls the script's
  `MaybeBuilding` (`0x604ef8`) and installs the `0x4d4e80` rectangle with
  wait mask `0x700`; stage 1 ends on a failed approach (`0x200`), stops and
  turns through a `FacingNoFormation` child (`0x604b10`); stage 2 waits for
  INBUILDSTANCE (`0x4d4b90`). Every wake through stage 5 re-resolves the
  body (`0x4930d0`) and ends with `UNITCHATTER_RESURRECTFAILED` when it is
  gone; stage 0 picks the kind (`+0x56`) and later wakes re-check it.
  Stage 4 checks no distance.
- **Who calls the resolver in modes 1 and 12:** the player's command path
  `0x5214b0` (mouse `0x527d00`, the GUI buttons `0x4b11xx..0x4b19xx`, the
  console order `0x424dc0`), the scripted-mission parser `0x513fb0` (from
  `0x514b40` and `0x4d35c0`), and the ReclaimArea children. Nothing idle:
  Standby (`0x407770`) runs only the fire-at-will scan `0x4d8370` and
  `0x4d7de0` (modes 3 and 2), Standby_Wander mode 3, Guard and Patrol mode 8
  (HELPBUILD or REPAIRUNIT only, `0x4debed`), and the AI's group order
  `0x50b5d0` only modes 2 and 9 -- the AI never orders a reclaim or a revive.
- **`autoreclaimable`** (feature flag `0x80`, loader `0x494151`) is tested
  in one place, `0x509d91`, inside the ReclaimArea target picker
  `0x509cc0`: it shapes area orders only.

Ours through 0.9.11: an idle caster searched 56 px each tick and raised a
body (its own side's for a resurrect, any for an animate, judged by
`resurrectable` alone), an idle reclaimer cleared every body within 56 px in
one tick, and there was no revive order -- a right-click on a body with a
builder caster issued RECLAIM, and an area reclaim always reclaimed. Since
0.9.12: `Cmd::Resurrect` (protocol 305) runs `World::resurrect`, a RECLAIM
leg on the body with `Order::reviveKind` (1 resurrect, 2 animate; the kind is
`reviveKindFor`, from the two types alone, so it is fixed at the order).
`World::tickReclaim` runs stages 0-2 as RECLAIM's, then stage 3; the channel
(`Unit::reviveTarget`) runs in `World::tickAbilities`, and its end sets the
leg's stage to 5. The client resolves a click on a body per unit as mode 1
does (`GameView::issueRaiseOrReclaim`), the CLEAR button as mode 12, and
`World::tickReclaimArea` gives a caster's child RESURRECT where it can.
`tickAbilities` searches for nothing. Our AI levels give their idle casters
the order themselves (`Controller::reviveIdle`: a body within the old 56 px,
none the end would refuse); the Retail AI gives none. Not ported: the
explored-cell test at the point (the client offers only what it can pick),
the object bits `0x20`/`0x40` (ours uses the fade alone), the AI-profile
limit at stage 3, and VTOL_RESURRECT's own flight states (a flying caster
uses VTOL_RECLAIM's stand-in, as its reclaim does).

## Helping to build and guarding (icd, 2026-10-27, protocol 301)

Found by emulating the routines; ported in 0.9.7. Details and the remaining
differences are in [the 0.9.7 notes](release-0.9.7-notes.md).

- **Eligibility (`0x519e60`).** The target is alive, unfinished, not the
  helper and has the helper's owner (an ally's site does not qualify). The
  helper is a builder with a mover, so a structure never helps; a
  builderlimited one needs the target's type on its own build menu. Nothing
  about the helper's queue, repeat build or current job is consulted. Ours:
  `World::canHelpBuild`, checked by `World::assist` and the command handler.
- **HelpBuild (`0x405cf0`).** Every worker on a site works through the same
  construction step (`0x429af0`): it takes its workertime-scaled share off the
  site's remaining fraction (+0x108) and pays for exactly that. A factory's or
  mobile producer's output therefore has one progress, finishes once and costs
  its buildcost however many help. Ours keeps the producer's tick count as
  that progress (`World::workProduction`); each helper's work is converted
  into it, the producer is freed the tick the unit completes, and the output's
  PARK, rally or exit step is given once. Its work stage sets the 300-tick
  cloak block (`0x4060bb`, table above). How a ground helper gets there and
  when it gives up: the next section but one.
- **Follow_Ground (`0x407f90`), a mobile builder's guard.** Each look: the
  guarded unit damaged and repairable (`0x519d40`, allies included) ->
  RepairUnit on it; else its own unfinished body, if eligible -> HelpBuild;
  else the target of its MobileBuild, BuildingBuild, HelpBuild or RepairUnit
  (`0x408431..0x408486`; no VTOL mission is in the list, so a flying producer
  is not helped from the ground). The job is inserted ahead of the guard,
  which resumes when it ends; no distance is checked. A look that finds
  nothing sleeps 15 + rand(15) ticks. Ours: `World::tickGuardHelp`,
  `guardHelpPick`, `guardInsertJob`.
- **Follow_Ground's escort ring (protocol 303, emulated 2026-10-27).** Stage 0
  (`0x4087fd`) stores an inner radius (max(ward footX, footZ) + guard footX
  + 2) x 16 at mission +0x4e and an outer one 3 cells more at +0x52. Every
  look that inserts nothing installs `0x4d4e10(ward +0x68, outer, inner)`:
  the `0x4e28f0` ring controller PARK uses (vtable `0x5f290c`), centred on
  the guard-footprint origin under the ward, outer tolerance squared as
  `0x4e2500`; a look woken by a controller event round a ward with no mover
  (+8 null) only sleeps (`0x408783`). The guard steers to the ring's point on
  its own bearing (`0x4e33b0`) and stands anywhere in the band. Ours walked
  to the ward's centre and stopped within 70 px: for a factory that is its
  yard (a Barracks pad lies 65 px from its centre), so a guarding builder
  parked on the pad and BUILD stage 2 (`0x401deb`) refused every later output.
  Ours: `World::guardRing`, read by `tickCombat` and the ground mover each
  tick (retail re-aims at each look; for a structure the ring is the same).
  The ring controller's only arrival is the band itself, so the ground mover
  never stops a guard on its footprint-wide final-goal radius: that radius
  (32 px for a 4x4) is wider than half the 48 px band and left siege engines
  and flagships walking in place just outside it.
  Measured by `doorway_test` (216 factory cases, every shipped ground factory,
  guard and assist, both modes and balances: 77 stalled or left the guard in
  the yard before; plus ring-rest cases for 1x1 to 4x4 guards round a Barracks
  and a Navy Yard, and a Navy Yard guarded by a Flagship). Flyers keep
  VTOL_Follow's orbit and their 70 px hold.
- **VTOL_Follow (`0x418860`), every flyer's guard** (the order dispatch
  `0x4de530` picks it by canfly alone). Off the map it first flies to a point
  800 px toward the map. A flying builder runs Follow_Ground's checks with the
  VTOL build and repair missions added, so it also helps flying producers; a
  repair target whose model top is under the sea is refused (`0x519d40`'s
  flying branch). Its jobs are VTOL_HelpBuild (`0x41f7f0`) and
  VTOL_RepairUnit (`0x420e60`): approach to 2 x builddistance, then orbit at
  builddistance -/+ 7. A job sleeps the guard 15 + rand(30) ticks. With
  nothing to do it orbits the guarded unit (`0x419053`): 320 px, or the
  active weapon's range + 160 when armed, the angle stepping back
  0x4000 + rand(0x2000) on each arrival, looking again after 15 ticks. Ours:
  `World::tickFlyingGuard`. The guarded unit's attacker check
  (event 0x10) is ported since protocol 302 (next section), its air-traffic
  step-out and its lost ward (`0x4191d7`: the last mission goes on as
  VTOL_SeekGuard round the guarded unit's last position) since protocol 307
  ("Air traffic"). Not ported: the group check (`0x51d1e0(unit, 1, 4)`, which
  this note used to call an attack scan) and the optional leash.
  Our draws come from the sim's RNG over the same ranges, not retail's stream.

## A guard answers its guarded unit's attacker (icd, 2026-10-27)

The hit response `0x409a50` first sends event `0x10` to the victim's listener
list (`0x519a90(victim, 0x10)`: unit `+0xc4`, each node's object vtable[0]),
before any of its own gates and whether or not there is an attacker; a
guard's Follow mission is one such listener. After it the damage routine
(`0x51a35a`) stores the live attacker at victim `+0xf8`. Heals (damage type
12) and type 13 never call it.

Follow_Ground (`0x407f90`) sleeps between looks on wait mask `0x718`, so the
event wakes the look at once; VTOL_Follow (`0x418860`) idles on the same mask.
The look's first check (Follow_Ground stage 1 `0x40814b`, VTOL_Follow stage 2
`0x418bb1`), only when the wake's events hold `0x10`: the guarded unit's
`+0xf8` attacker, alive and not dying, whose player does not count the
guard's as an ally, and whose type is not in the guard's notargetcategory set
(UnitDef `+0x9a`), gets `0x4d7de0(guard, attacker, explicit 1, 0, auto 0,
0)`. With explicit set, 4d7de0 skips both the Hold Position (`+0x130 &
0x30000`) and the Hold Fire (`& 0xc0000`) gates and never takes the leash /
return-move path; with an attack mission for the pair (`0x4de530` kind 3) it
inserts a plain attack ahead of the guard, the look clears its wait mask and
returns 3 (the dispatcher's `15 + rand(30)` sleep). Otherwise a guard not on
Hold Fire hands the attacker to each auto weapon slot (`+0x1a & 4`, not
commandfire, a switcher's selected slot only) whose current target is gone,
out of reach (`0x52ad60`; Follow_Ground also `0x52adf0`, the minimum range)
or a bad target (UnitDef `+0x8e[slot]`) -- 409a50's part (b) without its test
that the attacker itself is in reach -- and the look goes on to the help and
repair checks. An unarmed guard (no attack mission) does nothing here.
Emulated with `0x4d7de0` and the slot calls hooked (scratch probe): the event
bit, an ally, a dying attacker, no attacker and the notargetcategory each
skip it; an insert returns 3 with the mask clear and comes before
MobileBuild's help; a failed insert retargets slots and then helps.

Ours (protocol 302): `World::retailHitResponse` stamps the victim's
`attackedTick`; a guard whose guarded unit's stamp is newer than its own
`guardHeard` hears the alarm at its next look (`tickGuardHelp` for every ground
guard, `tickFlyingGuard` as event `0x10` in the wait mask), and
`World::guardDefend` takes `lastHitBy` as `+0xf8`. The attack is
`retailIssueAutoAttack(..., forced)`, not an auto target, so it chases until
the attacker dies, and the guard order under it resumes. The attack scan
(`0x51d1e0`) stays unported; it is our target acquisition.

## Helping and repairing on the ground: the approach (icd, 2026-10-30, protocol 305)

Ground HelpBuild (`0x405cf0`) and RepairUnit (`0x407190`), read and run stage
by stage in emulation through the dispatcher's result codes
(`sim/retailmission.h`), the routines they call stubbed. Probe:
`~/tak-tmp/retail-checks/repair-approach/probe_repair_help.py` (outside the
repo). Flying helpers and repairers (VTOL_HelpBuild `0x41f7f0`,
VTOL_RepairUnit `0x420e60`) are unchanged since 0.9.7.

- **Where they walk.** Both install `0x4d4e80`, the rectangle controller
  RECLAIM and MobileBuild use (see "Reclaim" above): HelpBuild at stage 0
  with the site's footprint-origin cell (+0x74) and its type's footprint
  (+0x126/+0x128), RepairUnit at stage 1 with the target's cell and its own
  footprint (+0x78), after saving where the target stands (mission +0x22).
  The builder walks until its footprint touches the target's, on the
  nearest side, never stopping at builddistance. Ours walked every helper
  to one point south of the site and every repairer to where the target
  stood at the order.
- **Reach.** HelpBuild (wait mask 0x708, no timer) starts work on arrival
  (0x100/0x400); on a failed approach (0x200) only when
  distance - trunc(8 x |builder footprint|) - trunc(8 x |site footprint|) <=
  builddistance (`retailBuildWithinReach`, MobileBuild's test), else it ends
  with UNITCHATTER_CANNOTGETTHERE. There is no timer: ours gave up after
  8 s without 20 px of progress. RepairUnit's look (stage 2, after a
  7 + rand(15) tick sleep or an approach event) applies the same reach test
  on every approach event: in reach the work starts, out of reach the
  approach is installed again at once, so a still target it cannot reach is
  never given up on (30 failed approaches emulated: no chatter, no count).
- **No distance once working.** Neither work stage measures distance: a
  helper moved 2,900 px away kept calling the construction step, a repairer
  1,900 px away kept paying for the repair. Ours re-measured every tick and
  stopped a bumped builder.
- **A moving target** (+0x130 & 0xc, the MoveRate tier: walking or turning)
  is never repaired. Each look while it moves adds 1 to mission +0x52, or 3
  when it is in reach; past 10 the repair ends with REPAIRSUNSUCCESSFULL
  (emulated: look 11 out of reach, look 5 in reach); otherwise the approach
  is installed again after 1 tick (out of reach) or 7 + rand(15) (in reach).
  A target that starts moving during the work stops it (`0x4d4b10`), adds 3
  and re-aims next tick. The count starts at 0 and is never reset while the
  job lasts. A still target's timer look re-aims only when its cell changed
  since the install (`0x407588`). Ours healed a walking unit while in reach,
  and only a patrol's or guard's repair re-aimed, once a second.
- **The leash** (mission +0x56; only Patrol sets it, to sightdistance, with
  its origin where the builder left the patrol, `0x403afc`) is tested on
  every wake: the repair ends silently once the builder's own integer
  distance from the origin is >= the leash (emulated: 199 continues, 200
  ends). A guard's repair has none. Ours measured the target's distance from
  the origin, with >.
- **Ends.** RepairUnit: the target dead or changed (REPAIRSUNSUCCESSFULL),
  not eligible at stage 0 (`0x519d40`, REPAIRMISSIONFAILED), full health at
  the work stage (silently), no mover (stage 0 returns 7: a building never
  repairs). HelpBuild: the site finished or gone (CONSTRUCTIONTERMINATED,
  unless the count is a repeat's).

Ours since protocol 305: a ground builder's help or repair leg is the job
itself, run on the leg's `mission` with its `buildRectangle`
(`World::prepareHelpApproach` / `tickGroundHelp`,
`World::prepareRepairApproach` / `installRepairApproach` /
`tickGroundRepair`; the give-up count is `Order::repairLooks`, the saved
position `buildX`/`buildZ`). Not ported: the turn child and the
INBUILDSTANCE wait before the first work tick (work starts on arrival, as
before), the patrol scan's random pick (ours takes the nearest), and the
unit chatter (no channel: every give-up is silent). A repair whose target is
healed to full by someone else ends at once (retail walks on until it
arrives). Engine deviation (the 0.9.8 doorway report): a helper whose help
ends with nothing else to do, standing in a factory's rectangle (it walked
in to touch the output), walks out past the nearest side
(`World::leaveFactoryYard`, the point `clearBuildSite` would pick); a
repeating or producing conjurer stays (its Move would be its output's
rally). A help queued on the site the builder is building itself waits
behind that MobileBuild (`groundHelpLeg` defers to a current build order).

## Attack_Chase: the stop comes before the aim (icd + emulation, 2026-10-30)

Kirenna (VERMAGE) ordered onto a target out of reach walked in, stopped about
399 px off and never fired; ordered from in reach she fired. Her AimWeapon
waits while `!static7 || static6` (100 ms sleeps). Static 7 belongs to her
walk controller, MoveControl, a loop Create starts: it sets 7 after
`walk_legs` (moving while aiming) and after `restore_x` (stopped and not
aiming), and clears it after a plain `walk`, so a walk of a whole cycle leaves
it 0. MoveControl decides only between walk frames (about 100 ms each; a frame
loop quits early once its moving flag drops), and its moving flag is
MoveWatcher's static 3 = GET 29 > 5 or static 5, sampled every 100 ms; static 5
is the last TurnDirection argument. Once she stops, the controller plays
`restore_x` (static 7 = 1) if AimWeapon has not raised static 2 yet, and
`restore_legs` if it has; from `restore_legs`, stopped and aiming, it sleeps in
a loop that never sets 7 again. Only TargetCleared or moving again does.
Among the 129 shipped COBs whose AimWeapon has a wait loop, VERMAGE is the only
one that waits on a static its walk controller writes (the others wait on
their FireWeapon flag, or on activation, burrow or holster state).

World started AimWeapon on the tick she stopped, so the controller nearly
always looked after it (only when MoveWatcher and the walk frame both came due
in the very next pass did it look first). Retail starts it three ticks later:

- **Attack_Chase** (`0x403bb0`, registry row `0x5eb8d1`). Stage 2 chases: in
  mode 0 (mission +0x52 % 100) it installs a circle controller on the target
  (`0x4d4da0` -> `0x4e2500`, radius = the slot's range, accepted in cells) and
  sleeps on 0x700 or `rand(6)+3`. The navigator stops the body when the circle
  accepts and posts 0x100/0x400 to the mission. The next dispatch takes the
  event, sets stage 1 and sleeps a tick (`0x403f2c..0x403f4f`). Stage 1
  (`0x404406`): with the target in reach (`0x52ad60`) and not inside the
  minimum range (`0x52adf0`) it stops the navigator (`0x4d4d40(m,0)`), makes
  the selected slot manual if its target differs (`0x519b70`), sets the target
  (`0x51a700`) and sleeps `rand(6)+3` on mask 0x1f40068; out of reach it bumps
  +0x52 and returns to stage 2, which then walks it to a random point deeper in
  (modes 2-4). An order that starts in reach runs stage 0 and stage 1 in its
  first dispatch. Nothing else in the mission touches the slots: they stay
  automatic through the chase (the previous mission's destructor, `0x4d6da0`,
  set them so).
- **The per-unit update** (`0x51d3e0`) runs the weapon update `0x52ae90`
  before `0x51de30`, which runs the unit's COB pass (`0x56c870`), then the
  missions (`0x4d8450`), then the mover (`0x4dc800`). `0x52fe30` starts
  AimWeapon (not immediate) and posts the facing request (`0x4dc0b0`); the COB
  pass right after runs it, after the threads already there; the mover then
  pivots.
- **The mover reports every turn** to the same VM: `0x4d91b0`'s tail
  (`0x4d9593..0x4d95de`) sends TurnDirection(request / 182, truncated)
  whenever its sign differs from the last one sent, a turn starting or ending
  included, immediately (`0x56c640` -> `0x56c5f0` -> `0x56c870(0)`). The idle
  navigator (`0x4d9ad0`) asks for no turn before braking, then pivots at
  turninplacerate when a facing request is posted and the speed is 0, so a
  pivot sends TurnDirection(0) and TurnDirection(d) on every tick it lasts.
  World sent these to the display VM only (`retailmovementcallbacks.h`).

So in retail she stops on tick A, stage 1 runs on A+2, and AimWeapon runs in
A+3's COB pass, after MoveControl.

**Emulated end to end** (`tools/re/probe_attack_chase_stop.py`): the native
weapon update, VERMAGE's COB in retail's VM with the real GET/SET host, the
mission dispatcher and Attack_Chase, and the mover with the ground navigator
and the mission's own circle controller, run per tick in 51d3e0's order on a
flat map; only the aim angles, the led point, the damage lookup, the group
check, the retarget scan and the display sinks are controlled. Every
engagement shows the same timeline (stop on A, stage 1's target on A+2,
AimWeapon on A+3), and she fires exactly when her walk controller takes
`restore_x`. It can only do that in a pass up to A+3 (in A+3's it runs before
AimWeapon's new thread): the walk frame it sleeps in must end by then, with
MoveWatcher already reporting the stop. Otherwise it takes `restore_legs`
(A+5 to A+7) and she never fires. Over 36 order ticks x 4 distances, the
arrival forced inside the range (`--grid`):

    MoveWatcher sees the stop   restore_x      restore_legs   fires   of 144
    A+1                         A+3 or A+4     -              yes     36
    A+1                         -              A+5            no      12
    A+2                         -              A+5 or A+6     no      48
    A+3                         -              A+5 to A+7     no      48

(from a start facing away, MoveWatcher on A+2 with `restore_x` on A+4 fires
too). So retail stalls too: she fires in 36 of 144, and on the plain approach
(620-760 px, no fire at will) in 34 of 141. With fire at will the slot is
automatic during the chase and the sweep aims it while she walks, so the next
walk cycle plays `walk_legs` and sets static 7: modelling the sweep's visit
every 1, 4, 10 or 30 ticks she fires in 129, 130, 133 and 133 of 141. Most of
those approaches first arrive just outside her 400 px reach (the circle
accepts in cells), and stage 1 sends her on a second, deeper walk.

**Ported** (protocol unchanged: 305 is unreleased). `World::tickCombat` holds
an Attack_Chase order's weapons, the aim start and the facing request, until
its stage 1: the tick the order first stands in reach when it starts in reach,
two ticks after the stop when it walked (`Order::attackChased`,
`attackAimTick`, on the front order); the body faces from the tick after. World
runs the COB pass before the weapon update, so World's aim reaches the script
at the next tick's pass, the same pass in which retail's runs.
`World::notifyTurnDirection` reports the mover's turns to the unit's own VM
before MoveRate, with the idle navigator's zero request ahead of a pivot from
`brakeGround`. The same grid run on World (36 order delays x 4 distances, a
scratch probe) gives the same answer as retail for every phase the two share,
and she fires in 58 of 144 (12 before: only when her controller looked in the
very pass after the stop). `attack_stop_test` (Retail and `--legion`) checks
the timing for Kirenna and an Archer, that Kirenna ordered from 664 px fires
(before: none in a minute), and that her script hears a pivot through
TurnDirection. In the attack sweep (19,501 cases, both rosters), 29 ordered
cases fire that did not: 24 of Kirenna's (of her orders that walked more than
40 px, 9 of 52 fired, now 26) and five of a Wolf, a Mind Mage and a Berserker
against a circling flyer; one fire-at-will case lost its only shot (a Crusades
Stone Giant at a flyer circling at its full range, which it hit once, late).

Not ported: the approach. World stops at the 2D range, so it never makes
retail's second, deeper walk, and its slots are manual for the whole order, so
fire at will never aims during the walk. Both make retail fire in more of these
approaches than World does. The idle auto-slot pivot (`tickSlotWeapons`) turns
before the mover and `brakeGround`'s zero request follows it, so that pivot is
not reported. The state rides the front order, which `replaceLeg` does not
carry to a new route's legs: a route that lands on a walk's last tick leaves
that arrival unheld, and one that lands during a later chase of the same order
arms the hold again (retail's slot keeps its target through it). Kept on the
goal leg instead (tried: it changed no shot of Kirenna's in 36 walk-off
chases), the hold also follows the single step an order takes when the
fire-at-will sweep switches her weapon on its first tick, and she stalls in 26
attack-sweep cases that she fires in now (all of them that step, 14 against the
castle or the lodestone); retail's automatic slot would aim during that walk.

## Kirenna on an attack-move (icd static + emulation, 2026-11-03)

**Closed by fx-tsearch step 6.** A fight-move leg is now retail's MoveFight
order (`World::markFightLeg`: Move_Ground / VTOL_Move response mode -1, the
leg's point as the response goal), so its acquisition is the Move's own scan
(`4d8370`, every stance) and not `findTarget`. She takes the Archer only once
her player sees it, inside her selected slot's range, stops and fires
(`vermage_fight_test`, kirenna_attack_move_fires). The text after the next
subsection is the finding as it stood.

### Why she fired once in Legion and kept firing in Retail mode (2026-11-03)

Not the pathfinding mode: the phase of her walk cycle when she stops (I,
from a per-tick trace of her script, `vermage_fight_test --trace`; the
script and the engine order are the ones emulated below). In the test's
start phase Legion's Move woke (its scan, rand(5)+5 ticks apart, on a
random stream the two modes draw differently) on the tick the sweep
started her aim (t541), Retail mode's two ticks later (t543). Varying the
order tick (30-32) and her start point (0-117 px back), 120 phases, she
kept firing in 8 in Retail mode and 13 in Legion, fired once in 95 and
84, and never in 17 and 23.

The sequence, in both modes:

1. Walking on the fight-move, the 7-tick visible list takes the Archer
   once she comes within her sight (215 px, inside Water Ball's 400); the
   sweep (`40f330`) gives her automatic slot the target and the weapon
   update starts AimWeapon (static 2 = 1). Her walk controller is inside a
   plain `walk` cycle it chose before she aimed (it decides only between
   cycles; a cycle quits early once MoveWatcher's moving flag, static 3,
   drops), and `walk` ends with static 7 = 0.
2. The Move's next wake (0 to 9 ticks later) takes the Archer
   (`retailHostScan`), the attack in reach stops her at once (brakerate 10).
3. MoveWatcher sees the stop within 100 ms; the `walk` cycle returns and
   writes static 7 = 0; the controller, stopped and aiming, plays
   `restore_legs` (static 9) and from then on sets 7 no more. Only a whole
   walk cycle ending while she still walked with static 2 set (`walk_legs`,
   7 = 1) avoids it: those are the phases that keep firing.
4. Her one shot in most phases comes from a TargetCleared that World sends
   when the attack makes her slot manual (`setSlotsManual` -> 51a7f0 with a
   target set: static 7 = 1), which the `walk` cycle's end then clears
   again (before her second aim). Retail does not send it here (S):
   Attack_Chase's stage 1 calls `519b70` (manual, and so 51a7f0) only when
   the slot's target (`51a9a0`) differs from the mission's
   (`404476..404483`); with the sweep already on the Archer the slot stays
   automatic and keeps it. Ported, that rule turns her one shot into none:
   in the same 120 phases she fired in none of 112 in Retail mode and of
   110 in Legion. So retail, by its own script and engine order, would
   stall her on most of these attack-moves (I: not emulated end to end, the
   chase probe has no fight-move). Left as it is (not ported): an owner
   call.

**Legion clears the stalled aim** (deliberate, not retail;
`World::legionAimStall`): a loaded ground unit standing still whose started
aim (flags 0xe0) its script has not accepted (SET 22) for
`World::kLegionAimStall` ticks (90, 3 s) gets TargetCleared on that slot
once, as if the target had been retired and taken again; the order and the
slot keep the target. Her script then sets static 7 and fires, and standing
she keeps it. The count (`Unit::aimWait`, hashed when set, always 0 in
Retail) restarts only when an aim is accepted, the body moves or the slot's
target is retired, so a script that waits on purpose (activation, burrow,
holster) hears it once per stall. In the 120 phases she now keeps firing
in all 120 in Legion (10 shots, 14958 damage in 60 s at the test's phase;
Retail mode 11, 15859). Retail mode keeps the race (8 of 120).

The 0.9.3 known limit "a Vermage on attack-move with Maneuver dealt no
damage" reproduces on 0.9.19: Kirenna attack-moving past an Archer 1100 px
off acquires it about 680 px off (inside 690), walks in, stands 400 px off aiming (her slot's
aim flags 0xe0) and never fires while it hits her (`vermage_fight_test`,
Retail and `--legion`). It is the Attack_Chase race above, reached through
an order retail does not have:

- **Retail has no fight-move mission** (see "Fight-move legs" above; but it
  has a fight-move ORDER, the unbound key command `UnitCommand MoveFight`: a
  Move_Ground with response mode -1, see "Mission target scan"). Its plain
  move with fire at will fires through the automatic slots: the
  sweep (`40f330`) picks only enemies on the player's visible list (`412620`,
  `fire_sweep_test vision`). World does that on a plain move too: past the
  same Archer she walks on until she sees it (sight 215), the sweep aims her
  slot while she walks, and she fires (the same test).
- **World's attack-move acquisition is not retail's scan.** `findTarget`
  takes an enemy within the longest weapon's range + 90 px (Water Burst 600:
  690 px) whether or not the player sees it; retail's scan (`4d8370` ->
  `412620`) takes only visible enemies, and for a ranged slot only those
  within the selected slot's own range (`52ad60` on every candidate; the
  max(sight, range) radius is only the gather and binds a melee or kamikaze
  slot alone: corrected 2026-11-03, E, `tools/re/probe_mission_scan_vision.py`).
  With Water Ball (400) selected she would take the Archer only inside 400 px
  and only once she or another of her player's units sees it. So she starts an Attack_Chase on an Archer she cannot see, stops in
  reach of Water Ball (400) before seeing it, and the sweep never aims for
  her. Her AimWeapon then waits on static 7 for ever, as retail's does in the
  stalled cases of the grid above.

**Attack_Chase's approach, read (static, `403bb0`).** Stage 2's new
approach is picked by mission `+0x52` (the mode) through the byte table at
`404620` (106 entries) into the jump table at `404608`: modes 0-1 and
100-101 case 0, 2 and 102 case 1, 3/103 case 2, 4/104 case 3, 5/105 case 4,
any other mode case 5 (`403c47`, return 7: the mission ends). With R =
`52aba0` (range), r = `52abd0` (minimum range), span = R - r and d the whole
pixel distance from the unit to the goal (`+0x22`):

- case 0 (`4040dd`): r = 0 ? circle(goal, R) (`4d4da0` -> `4e2500`) :
  ring(goal, R, r) (`4d4e10` -> `4e28f0`; an inner radius at or past the
  outer widens the outer to 2 x inner + 100, `4e2906`);
- case 1 (`40410c`): outer = R - rand(span/3) - rand(span/3), inner =
  rand(span/8) + r; if inner <= d <= outer and rand(3) != 0 the mode steps
  on; r = 0 ? circle(goal, outer) : outer - inner >= 50 ? ring(goal, outer,
  inner) : ring(goal, span >= 75 ? R - span/3 : R, r);
- case 2 (`404208`): outer = R - 3 x span/4 - rand(span/4), the same band
  test against r, then circle(goal, outer) or ring(goal, outer, r) when 50
  apart, else ring(goal, span < 100 ? R : R - span/2, r);
- case 3 (`4042dc`): the mode steps on; a circle of 16 px on the point d'
  from the target toward the unit (heading `51b340`(target, unit) +
  rand(0x5555) - 0x2aaa; d' = rand(span/2) + r, or rand(span) + r, or
  rand(span/2) + span/2 + r on two rand(2) draws; offsets `5360bf`/`5360f3`
  negated);
- case 4 (`4041fc`): keeps the controller it has.

Stage 1 (`404406`) out of reach steps the mode on when rand(2) != 0 and
mode % 100 < 4 (`4044ba`), adds 1000 and runs stage 2 at once, which takes
the 1000 off and installs the case's approach; an arrival at mode 5 steps it
back to 4 (`403f3e`). Stage 2 on a wake re-reads the target only when its
footprint origin left the goal's (`403f62`), and then goes to stage 1 if the
target is in reach and rand(mode % 100 + 1) == 0 (`404011`, a sixth of those
also drop the mode to its hundred). The chase's slots are left alone until
stage 1 (`519b70` is called only at `404483`, for the selected slot whose
target differs). Emulated (`probe_attack_chase_stop.py`, 1100 px, sweep
every 4 ticks): the first circle accepts 403.5 px off, out of reach; stage 1
runs at +2, the re-installed circle reports at once, stage 1 at +4 steps to
mode 2 and the narrower ring walks her to 242.5 px; in reach while walking,
the sweep aims her slot at 400 px and she fires at once.

**Not changed.** A port of that approach and of the automatic chase slots
was tried on `fx-vermage` and kept out: it does not make Kirenna fire here
(the sweep still cannot see the Archer), and on a plain-approach grid of
explicit attacks with hold fire (620-760 px, one per pixel, this World's
geometry) she fired in 59 of 141 with it and 72 without, against retail's 34
of 141 in the emulator's own geometry; where the circle accepts depends on
the cell alignment, so the two grids are not the same cases. Matching retail
here means a retail-faithful fight-move acquisition (visible enemies, the
selected slot's reach), not this mission alone.

## Mission target scan (icd static + emulation, 2026-11-03)

Where every automatic target a retail unit picks comes from, read for the
port of `4d8370` / `412620` to all of World's automatic picks. Marks: **E**
emulated (the probe named), **S** static reading, **I** inferred. The probes
are in `tools/re/`; each runs the user's own `KINGDOMS.icd` and copies
nothing.

**Two scans, one picker.** `412620` has exactly two call sites in the binary
(S, a byte scan of every `call rel32`): `40f301`, the fire-at-will sweep
(`40f330` -> `40f2e0`, radius = the slot's range), and `4d839b`, the mission
scan `4d8370`. Both draw only from the owner's visible-enemy list +5
(`411700`, rebuilt every 7 ticks; `4f7210` reads the owner's own sight plane,
so an enemy lit only in an ally's plane is not on it; a cloaked enemy never
is) (E, `probe_mission_scan_vision.py`, 10 observations):

- `4d8370` scans only for an armed unit (`+0x130 & 0x8000000`) on Fire At
  Will (`(+0x130 & 0xc0000) == 0x80000`); Return Fire and Hold Fire return 0.
  It takes the scanning slot (`519ac0`: a switcher's selected slot, else the
  first present slot of 0..2) and gathers within max(sightdistance
  `def+0x226`, the slot's range).
- The gather is `411ae0` (S, `411b2c..411b95`): it walks list +5 from
  front to back and keeps each unit that is alive (`+0x130 & 0x1000000`),
  not dying (`& 0x1000`) and within the radius by live centre distance. So a
  scan costs the length of a list that is rebuilt only every 7 ticks.
  World's `retailSelectTarget` reads the same list. It used to gather the
  spatial grid over the radius and keep the listed units, which gave the
  same candidates in the same order. Only when nothing is found does
  `411ae0` look further: if the owner has a live, finished, switched-on
  unit whose type has `istargetingupgrade` (`def+0x260` bit 10; build
  manager `+0x79`, set at `4118de..4118f7`, read by `413290`), it runs the
  same test over list +0x25. 411700 fills that list with enemy units that
  have `+0x130 & 0x100`, seen or not (`411825..411841`). No shipped FBI
  sets `istargetingupgrade`, so World leaves this fallback out.
- The picker then tests every candidate with `52ad60` (in range) unless the
  slot is melee (weapon class vtable `+0x14`) or the unit has
  `def+0x260 & 0x10000000` (kamikaze, I) (`412783..4127b8`). **So a ranged unit only ever
  takes a visible enemy already within its scanning slot's range**: range 300
  and sight 500 never take a seen enemy at 400; range 700 and sight 200 take
  one at 650. A melee slot reaches max(sight, range): sight 500 takes 250 and
  400. The sweep is the same picker on the slot's range alone.
- The picker's other gates: a target needs `shootme` (`def+0x260 & 0x8000`)
  unless the owner is computer-run (World's `automaticGates`) or the
  **ShootAll** console toggle is on (`[[0x62d558]]+7`: ConsoleOptions, set
  only by `+shootall` at `0x427c70`, the console table at `0x605a60`; reset
  to 0 by `0x499440`, never loaded from the registry: S). World keeps it unset.

**The seventeen callers of `4d8370`** (S: call sites by byte scan, each
mapped to its handler through the mission registry's rows):

| Call | Mission (handler) | When it scans | What a hit becomes |
|---|---|---|---|
| `402988` | Move_Ground_Formation(/Standby) (`402880`) | stage 3, the parent move's response mode non-zero | the formation point's leash on Maneuver (pathfinding-port.md) |
| `402e2e` | Move_Ground (`402b00`) | response mode < 0 on every stance; mode > 0 off Hold Position; mode 0 never | an attack built in place (below) (E) |
| `40395e` | Patrol (`403600`) | a timer wake (no controller event `0x700`, no `0x2000`) | `4d7de0(u, t, 0, &pos, 1, 1)`: forced leash and return move |
| `404075` | Attack_Chase (`403bb0`) | stage 2, an automatic chase (mode >= 100), off Hold Position, rand(mode % 100 + 1) != 0 | a different hit becomes the target (`5199f0`), stage 0 (E) |
| `4049a0` | Attack_Melee (`404690`) | stage 2's timer wake (stage 1 far from the target sleeps 4 + rand(5) + rand(5)), automatic (`+0x52` != 0), off Hold Position, rand(2) == 0 | a different hit, kept only within `+0x56` of the origin when a leash is set, and then only on rand(10) == 0 (`4049fc`): `5199f0`, stage 0 (S) |
| `407839` | Standby (`407770`) | stage 1 | `4d7de0(u, t, 0, &pos, 1, 0)`; none: sleep 7 + rand(7) |
| `40797e` | Standby_Wander (`4078e0`) | stage 1, off Hold Position, then a sleep of 2 + rand(30) on `0x700` | Maneuver: only within the leash of the post `+0x2e/+0x30`; then `4de530` kind 3 and `4d6c40(kind, t, 0, 0, Maneuver, Maneuver ? leash : 0)`, origin the post, put ahead: no return move, mode 0 on Roam (S) |
| `4176bc` | VTOL_Standby (`417350`) | every scan wake | `4d7de0(u, t, 0, 0, 1, 0)`: stage 0, return 3 |
| `4179f0` | VTOL_Move_Formation(/Standby) (`417750`) | only in the `+0x5a & 0x4000000` standby branch (`41797e`), after its group tests (`50c140`, `50bd20`, `50b900`) | `4d7de0(u, t, 0, 0, 1, 0)`, stage 0, return 3 (S) |
| `4181b1` | VTOL_Move (`417c20`) | stage 2's timer wake, as Move_Ground: mode < 0 always, > 0 off Hold Position (`418193..4181b0`) | the same `+0xf8` fallback (`4181c1..4184a5`) and Maneuver goal leash (`4184ab..41850f`); the attack is Move_Ground's (mode 1, leash `def+0x232`); mode < 0: origin the unit, kept in the Move's `+0x2e/+0x30`, stage 5 (`4186f5`: a return move when past the origin by more than leash/3, as Move_Ground's stage 3); mode > 0: origin the goal, stage 0 (S) |
| `41962a` | VTOL_SeekAttack (`4192f0`) | stage 1 | `4d7de0(u, t, 0, 0, 1, 0)`: an attack put ahead ends it |
| `41a24d` | VTOL_Patrol (`419db0`) | stage-2 wake | `4d7de0(u, t, 0, 0, 1, 1)` |
| `41baf0` | AirStrike (`41b5e0`) | an automatic attack's rescan, rand(2) == 0 (see its section) | `4d7de0(u, t, 0, 0, 1, 0)` |
| `41c428` | AirToGround (`41bf80`) | an automatic attack's rescan (see its section) | as AirStrike |
| `41ce68`, `41d10d` | AirToAir (`41ca50`) | an automatic attack's rescans (see its section) | as AirStrike |
| `41e02e` | AirToGroundHover (`41d820`) | an automatic attack's rescan (see its section) | as AirStrike |

No other mission scans. Follow_Ground and VTOL_Follow (guard) answer the
ward's attacker (`+0xf8`); the hit response `409a50` answers the unit's own;
the scripted AttackUType (`401580`) walks the whole unit table with no sight
test on purpose; Wait (`401480`) counts with `411ae0`. **Guard_NoMove picks
nothing** (below), so a retail structure targets only through the sweep and
the hit response.

**The attack-issuing gate** `4d7de0(unit, target, explicit, point, auto,
forceLeash)` (E, `probe_issue_gate.py`: 54 of 54 combinations of mover or
not, stance, fire state, explicit / auto / auto + force). Explicit: a plain
attack (mode 0, no leash). Auto on Hold Fire: nothing. Auto on Hold Position
without force: nothing. Auto with force, or on Maneuver: a mover gets a
return move (kind 2) and an attack with mode 1 and leash `def+0x232`
(maneuverleashlength); a non-mover a plain attack with mode 1. Auto on Roam
without force: a plain attack with mode 1.

**Move_Ground's response** (`402e10..4032a8`, E, `probe_move_ground_response.py`,
18 observations; entered mid-function with a hand-built frame):

- The scan runs on a wake when the response mode (`+0x52`) is below 0 on
  every stance, above 0 off Hold Position, and never at 0. No hit: the
  fallback below; nothing: stage 1, return 4.
- **The fallback** is the stored attacker `+0xf8`: alive, not dying,
  finished, of a non-allied player (owner `+0xac[]`); the unit has a first
  weapon (`def+0x264 & 0x20`), is alive, not dying, selectable (`+0x130 &
  0x20`), finished, not carried; the attacker passes the scanning slot's
  category masks (`def+0x9a`, `+0x8e[slot]`). Then it is taken when `52ad60`
  puts it in range, or when it is nearer than 9/10 x maneuverleashlength,
  plus the slot's range for a non-melee slot (`402f9f..40302b`, a strict
  `<`). The weapon tests follow (`+0x268` bits 2 and 4, melee needs a ground
  target, `+0xc8` air bits, damage > 0). **No visibility test, no cloak test
  and no fire-state test**: a Hold Fire unit on a move with a response mode
  answers its attacker; only `4d8370` gates on Fire At Will. **No carried
  test on the attacker either** (S, `402e40..402e9b` reads only `+0x130`
  alive/dying, `+0x108` and the alliance byte; the `+0xa8` test at `402efb`
  is the mover's own carrier, and `4de530`'s head, `4de53c..4de583`, tests
  only alive/dying): an attacker since carried off is taken, and the attack
  it gets is removed at its first dispatch by its own head (Attack_Chase
  `403c9c..403cb0`, Attack_Melee `404778..40478d`, AirToGround/AirToAir
  `41c8f6`/`41d654`: `+0x114 & 4`). World skips a carried attacker in the
  fallback instead (`retailMoveFallback`, an engine guard): the outcome, no
  chase, is the same, but the Move's state after retail's round trip was not
  read and World's flyer attack does not drop a carried target.
- Mode > 0 on Maneuver drops a hit (scanned or fallback) at or beyond
  maneuverleashlength from the move's goal (`+0x24/+0x2c`), with no rescan;
  Roam keeps it at any distance.
- The attack is the same in both branches: ctor mode 1 (so it becomes an
  automatic chase, below) and leash `+0x56` = maneuverleashlength **on every
  stance, Roam included**; Attack_Chase ends when the unit is that far from
  the attack's origin (`403d1d..403d5e`, S). Mode < 0: the origin is the
  unit's position, the Move keeps it in `+0x2e/+0x30` and goes to stage 3
  (the return to it before the leg resumes). Mode > 0: the origin is the
  goal and the Move goes to stage 0.

**Retail's fight-move is a key command** (S). `UnitCommand MoveFight` (the
name dispatcher `0x4b0e40`, reached from the `UnitCommand` key binding
`0x48e0b0`, table entry `0x610934`) arms the Move cursor (`0x4b0df0`) and
sets `game+0x17633 |= 0x400`; changing the command mode (`0x429950`) clears
it. The order code `0x5214b0` then gives a resolved Move_Ground, VTOL_Move or
QMove response mode -1 when that bit is set and the command mode
(`game+0x1762c`) is 2 (`0x521bf4..0x521c5b`, and `0x521867..0x521892` on the
group-spread path). The shipped `Keys.TDF` binds no key to it, so few
players had it; its behaviour is the mode < 0 branch above: scan on every
stance at Move_Ground's cadence, attack with a leash from where it stopped,
return there, go on. This is the "mode < 0, producer not found" of the
pathfinding port.

**Automatic chases** (E, `probe_attack_chase_mode.py`, 13 observations).
`4d7de0` builds an explicit attack with mode 0 and an automatic one with
mode 1; Attack_Chase's stage-0 set-up (`404583..4045ba`) turns any non-zero
mode into 100 for an armed mover (a non-mover, or an unarmed unit, ends:
result 7). So "mode >= 100" is exactly "automatic". The stage-2 retarget
(`40404d..40409b`) runs off Hold Position and only when rand(mode % 100 + 1)
!= 0, so never at mode 100 itself (rand(1) is 0): only after stage 1 has
stepped the mode on (101..105). It runs whether the target is in reach or
not, except on the in-reach draw that goes to stage 1. A hit equal to the
current target changes nothing.

**Guard_NoMove** (`401bb0`, S; `401a90` E, `probe_guard_nomove_slots.py`, 15
observations). It runs only with `+0x130 & 0x2000000`; an armed unit calls
`401a90` and sleeps 7 + rand(15), an unarmed one sleeps 150. `401a90` never
calls `4d8370` or `412620`:

1. If any of the per-slot bits `+0xd0 & 0xe00000` (0x200000 << slot) is set,
   it draws rand(3); on 0 it releases each flagged slot; the bits are
   cleared on either draw. No bit, no draw. The bits come from `4099e0(shot,
   damage to an enemy, damage to an own unit)`, which the impact dispatch
   calls after a hit: enemy damage at most twice the own damage sets the
   shot's slot bit (`+0xd8` bits 2-3), more sets `+0xd0 & 0x1000000`.
2. Then each present slot (a switcher only its selected one) whose target
   (`51a9a0`) is missing, inside its minimum range (`52adf0`) or out of
   reach (`!52ad60`) is
   released.

"Release" is `519b70(i)` then `519b10(i)`: `519b70` clears the slot's
automatic bit (slot flags `+0x1a & 4`) and, if it was set, its target
(`51a7f0`); `519b10` sets the bit and, if it was clear, clears the target.
Net: the slot is automatic with no target, back to the sweep (a manual slot
is cleared once, an automatic one twice).

**Shared sight** (E, `probe_shared_vision.py`, 13 observations). A player
record (`game+0x2404 + 0x110 x i`) has `+0xf7[j]` ("j's sight comes to me")
and `+0x101[j]` ("mine goes to j"), both set for itself (`0x4f6290`). At game
start `0x4f6770` sets both, both ways, for every pair of players on the same
lobby team (`+0x50`->`+0x8a`, 4 = none; the iterator `0x50e490`) unless
either is a computer (`+0x50`->`+0x84` == 2, "LOCALLY/REMOTELY_HOSTED_AI"):
**allied humans on one team share by default; a computer shares with nobody
and nobody with it, allied computers included**. A unit's sight object
(`0x4c66b0`) stamps into the local player's plane when the local record's
`+0xf7[owner]` is set, else into its owner's, so each player's plane holds
his own units and his sharing allies' (each machine runs its own units'
scans), and `4f7210` reads only that plane. In game the Diplomacy dialog's
ShareMapAndLOS box (`0x4b6b60`, disabled for a computer row) sets the local
`+0x101[j]` and, for a remote human, sends message 0x15 (`0x4f5960`), whose
handler `0x4f59c0` sets the receiver's `+0xf7[sender]` and re-points the
sender's units' sight objects.

**Ported, then widened by the owner** (fx-tsearch step 8, then the owner's
D2 override of 2026-11-03: every ally shares, beyond retail).
`World::updateViewCounts` counts each unit's footprint in every plane of
`World::viewShareMask(owner)`: the owner's and every allied player's, human
or computer (human+human as retail; human+computer and computer+computer are
ours). Each footprint records its planes (`Unit::viewPlanes`, hashed), so a
team change moves the sight on the next pass (an AI takeover changes nothing:
the ally is still an ally), and a dying unit's 60-tick revealer (`4c6750`)
stays in the planes it was counted in (`RetailDeathSight::planes`, hashed).
Retail stamps an ally's units into the *local* plane on each machine; our
every-plane-on-every-peer sim gives each ally the same union. The client's
fog display ORs the local player's allies' planes (`captureFog`), which is
now exactly the local plane. Not ported: the in-game ShareMapAndLOS toggle
(no Diplomacy checkbox here).

**Death sight** (E, `tools/re/check_death_sight.py`, 2026-11-04). The unit
retire `0x512ae0` compares the owner's index (record `+0xeb`) with
`game+0x306f`: equal, it calls `0x4c6750(unit, 0x3c)`; otherwise
`0x4c6800(sight, 0, 0)` removes the footprint at once, whatever `0x306e`
holds. `0x306e` is the local player and `0x306f` the viewed one (S): both are
set to the local player at start (`0x527018`/`0x527024`,
`0x4f6504..0x4f651d`, `0x4f7704`/`0x4f7717`), and only the watch-player path
`0x4257d0` moves `0x306f` alone, to any present slot of kind 1, 2 or 3. So
each machine keeps revealers only for its own human's dying units: a
computer's never, and a human ally's not on the other ally's machine.
`0x4c6750` runs only with LosOn on (`[[0x62d558]+8]+0x15`; off, it neither
keeps nor removes). Its list is one global per machine (count `0x62da20`,
32-byte slots at `0x62d7a0`): below 20 it copies the unit's sight object
(`unit+0x84`, which keeps the plane `0x4c66b0` stamped it into), sets the
slot's expiry (`+4`) to tick (`game+0x19f44`) + 60 and clears `unit+0xa3`;
at 20 the footprint is removed at once, nothing evicted. `0x4c6b30`, once a
tick (`0x526553`, S), removes every slot whose expiry is before now (expiry
== now stays) and compacts the list in order. Since only the viewed player's
units enter a machine's list, its 20 is one owner's. Ported in
`World::updateViewCounts`: 20 per owner, past the cap at once, `tick + 60`
kept while `expiry >= tick`, in the planes the footprint was counted in.
Widened with D2: a human owner keeps one as retail, and so does a computer
whose footprint was shared into another plane (it has a non-passive ally);
a lone computer keeps none, as retail, and a passive neutral never.
`shared_sight` (`death_revealer_*`) covers it. Not ported: LosOn off's
no-op.

**Passive neutrals in campaigns** (S, 2026-11-04; owner decision of
2026-11-03). A mission's villagers and wildlife (`Player%i=passive neutral`,
put on the human's side) are computers to retail. The campaign start
`0x4d2750` (called from the game start `0x5253a0` at `0x5258db`) makes player
0 the local human with `0x4f6290(0, 1, ...)` (`0x4d290c`) and every other
record, strategic or passive alike, with `0x4f6290(i, 2, ...)` (`0x4d298f`);
`0x4f6290` passes the kind to `0x4f5940`, which stores it at `+0xea` and,
for any kind but 3 (remote), at the lobby slot's `+0x84` (`0x4f5954`). The
record's role byte only sets the alliance bytes `+0xac`/`+0xb7`
(`0x4d29a1..0x4d2a08`: neutral marks it allied by everyone, ally pairs it
with the human, opponent nothing), which no sight code reads. The same game
start then calls `0x4f6770` unconditionally (`0x525a4a`), and its
`+0x50`->`+0x84 == 2` tests (`0x4f67c7`, `0x4f6806`) leave every mission
computer out both ways. The only other writers of `+0xf7[]`/`+0x101[]`
(`game+0x24fb`/`+0x2505`) are `0x4f6290` (self), the Diplomacy box
(`0x4b6bdd`, `0x4b6c55`: computer rows disabled) and the 0x15 handler
(`0x4f5a84`, sent by humans only); no mission-script path. So in a retail
campaign the human never sees through a passive neutral, it never sees
through the human, and neither explores for the other (`0x4c6800` follows the
same planes). Its dying units get no revealer: `0x512ae0` calls `0x4c6750`
only when the owner's index equals `game+0x306f` (S: the viewed player,
the local one in play; see Death sight above). Ported: `setupMission` sets
`Player::passiveNeutral` (hashed) for every record but the human without an
effective `strategic` (the same test as the path-budget class);
`viewShareMask` gives such a player its own bit only and leaves it out of
every other mask, and `updateViewCounts` keeps no revealer for it. The
strategic ally keeps the D2 rule (retail would leave it out too). Outside
campaigns the owner's rule is the same -- a passive neutral shares nothing,
allied or not -- so one rule serves both; no skirmish or multiplayer setup
makes one today. The client's fog and unit display (`captureFog`,
`UnitR::sightShared`) and the AI's `team_` read the same mask, so the
wildlife shows only where the human's own side sees it.

**Exploration follows the same planes** (S). `0x4c6800` takes the Mapping bit
from the sight object's player (`[esi]`, `+0xeb` -> `1 << index`,
`0x4c685f..0x4c6871`) and ORs it into `game+0x19ef4` at every admitted cell
(`0x4c6a11..0x4c6a1f`) of the same pass that adds to that player's `+0x88`
counts (`0x4c69f1..0x4c6a0f`). So a footprint explores for exactly the
player whose plane it counts in. Ported in `updateExplorationPlane` with the
same `viewShareMask`, so under D2 every ally's units explore for every ally;
neutral scenery keeps the allied rule. A status-locked unit (stone, freeze)
whose share set changes keeps its footprint where it stands and moves it to
the new planes.
Guard: `target_search_test shared_sight`.

**Double sight is ours** (S). Retail has no such option: `def+0x226` is
written only by the FBI loader (`0x4bfa50`, `sightdistance`, default 0) and
two whole-definition copies (`0x4c37f0`, `0x4c51c0`); the sight object copies
it as authored (`0x4c66eb`); the stamp's reach of twice the distance is
unconditional (`0x4c68c1`); `412620` reads it raw (`0x4126d5`). World's
`doubleSight` option doubles sight and radar for skirmish and multiplayer;
which sight the ported scan's gather radius uses with it on is World's
choice, not retail's. World gathers over `sightDistance(type)` (the doubled
sight with the option on), so a melee unit's mission scan reaches as far as
the plane it stamps; with the option off it is retail's `def+0x226`.

**World before this work** (2f04e112): `findTarget` stands in for `4d8370`
and is not it (no visibility, the longest weapon's range + 90 px, nearest
first, a home leash), and it also runs from a per-tick stride scan of idle
armed units, guard fronts and `tickGuardNoMove` (which picks for
structures). Move_Ground's fallback has a cloak test and a range + 90 test,
and the stride scan is what makes fight-move legs fight.

**The ported pieces** (fx-tsearch step 1; not yet called by any mission):
`World::retailScanSlot` (`519ac0`), `World::retailMissionScan` (`4d8370`:
armed and Fire At Will, then `retailSelectTarget` on the scanning slot with
the mission radius) and `World::retailMissionAttack` (a mission's tail:
an optional leash reject on the pick, then `4d7de0` through
`retailIssueAutoAttack`). Oracles, each against retail's own code:

- `tools/re/check_mission_scan.py` (E, 4,000 cases against `fire_sweep_test
  --scan`): the real `4d8370`, `519ac0`, `412620`, `411ae0` and `52ad60` with
  the real weapon-class vtables, over armed or not, the three fire states,
  one to three slots (ranged or melee), switchers (selected slot 0..2, or 3:
  `519ac0` then takes the first slot), kamikaze, AI owners, fireatwillrandom
  and paralyzers, with candidates listed on +5 or not, in the air or not,
  shootme or not, paralyzed, paralyzer-immune, inside, between and beyond the
  slot's range and the sight: the same pick and RNG state.
- `tools/re/check_issue_gate.py` (E, 108 calls against `fire_sweep_test
  --gate`): the gate's result and missions (kind, mode, leash, the attack's
  origin `+0x2e/+0x30`, the return Move's goal and mode) for every mover,
  stance, fire state and call, with and without a point. **The point** (S,
  `4d7ec8..4d7fe7`): the return Move's goal and the leashed attack's origin
  are the point, else the unit's position (`+0x68`), and the return Move's
  response mode is 1 with a point, 0 without. Every caller that passes one
  passes the unit's own position (`40784e`, `403972`, `409e4b`: `lea
  [unit+0x68]`); the VTOL callers pass none, and World's flight return has no
  response mode. So `retailMissionAttack` needs no point argument.
- `tools/re/check_fire_sweep_picker.py` now also draws a paralyzer weapon
  (WeaponType `+0xc8 & 0xf == 4`, the FBI `damagetype = paralyzer`) and
  `immunetoparalyzer` targets (UnitDef `+0x260` bit 26). **Both paralyzer
  tests key on the damage type** (S, E), so `retailSelectTarget` tests
  `Weapon::dmgType == 4` for the paralyzed-target skip too (it tested the
  name-derived `Status::Paralyzed`; the shipped weapons are the same set, so
  no game changes) and adds the immunity skip.

**Idle coverage audit** (fx-tsearch step 2, the shipped `units/` FBIs and
World's hosts at 7bf30019). Every mobile type names one of three default
missions, and only structures name Guard_NoMove:

| FBI `defaultmissiontype` | Armed | Unarmed | World's host for an idle unit |
|---|---|---|---|
| Standby (mobile) | 98 | 11 | Standby (`standbyEligible`) |
| VTOL_Standby (every flyer) | 19 | 8 | VTOL_Standby (`standbyEligible`, `vtolStandby`) |
| Standby_Wander (mobile) | 9 | 7 | Standby_Wander's scan (`retailStandbyWander`, step 4; the wander movement is still World's 240-tick stroll order) |
| Guard_NoMove (structures) | 15 | 0 | Guard_NoMove (`tickGuardNoMove`, any structure) |
| none (structures) | 0 | 36 | Guard_NoMove (unarmed: it only sleeps) |

The armed Standby_Wander types (lifsaber, lifwolf, monboar, mondev,
monghoul, monpiran, npchunt, npcsail, npctribe) have no mission host: what
engages them today is tickCombat's stride scan, so the Standby_Wander scan
(`4078e0`) has to be hosted before that scan goes. `target_search_test
idle_coverage` (and its `--legion` twin) puts each of the 126 armed mobile
types idle on Fire At Will beside a seen enemy inside its scanning slot's
range (an Archer, else a Skeleton Archer or a Cannoneer for a weapon that
spares Humans) and needs an attack order, a sweep target or a hit within 60
ticks. At 7bf30019 all 126 engage at tick 2 with an attack order: the 19
flyers from VTOL_Standby's own scan call, the 107 others from the stride scan
before their Standby has run (and the wanderers have no host at all). The
ported `retailMissionScan` takes the same enemy for all 126 two list rebuilds
in. The test prints the table per type.

**The hosts on the port** (fx-tsearch steps 3 and 4). Standby, VTOL_Standby,
the air missions' retargets, the Legion lift (VTOL_Standby's rule) and
Move_Ground's response pick through `retailHostScan` (the host guards, then
`4d8370`) and hand the pick to `4d7de0` as their call site does. Move_Ground's
`+0xf8` fallback is retail's (`retailMoveFallback`, sharing the hit
response's reach and weapon tests), and the attack it builds is an automatic
chase leashed by maneuverleashlength on every stance (`retailBuiltAttack`).
Step 4 adds Patrol's (`40395e`) and VTOL_Patrol's (`41a24d`) scans with the
forced leash (World's stride scan no longer runs for a patrol leg),
Standby_Wander's scan (above; World's post is the unit's home, since its
stroll orders end the mission and start it again), Move_Ground_Formation's
(`40296c`: off Hold Position, the formation point's leash on Maneuver, no
fallback), VTOL_Move's response and its stage 5, and Attack_Melee's retarget
(`retailMeleeRetarget`, on a ground walker's automatic melee attack while it
closes in; the wake tick rides on the order's `airRunCount`, which a flyer's
attack alone uses otherwise). Not ported: Attack_Chase's retarget (`404075`)
needs the chase's approach sub-state in `+0x52 % 100`, which stage 2's steps
(`4041af`, `40428b`, `4042ec`) advance as the walker closes in band by band;
World's chase has no such steps, so its mode stays at 100, where the retarget
never fires (rand(1) is 0). VTOL_Move_Formation's scan sits in the
FormationStandby branch World does not run. `target_search_test
mission_ports` guards the rules: no pick of an unseen enemy in range, a pick
once an own spotter sees it, none past the range, no Standby attack on Hold
Position while its patrol gets the forced leashed attack and return move,
and Standby_Wander's leash on Maneuver and plain attack on Roam.

**Structures** (fx-tsearch step 5). `tickGuardNoMove` calls
`World::retailGuardNoMoveSlots` (`401a90`) in place of a pick: the shot bits
(0x200000 << slot, already posted to `Unit::missionEvents` by `applyHit`'s
`4099e0` port) release their slots on rand(3) == 0 and clear on either draw,
and each present slot (a switcher's selected one) whose target is gone,
inside its minimum range or out of reach is released (`setSlotMode` off then
on: automatic, no target). World adds its line-of-fire deviation
(`slotClearShot`, D4) to "in reach", as the sweep and the weapon update do,
so a tower lets go of a target its owner's new wall hides. Guard_NoMove runs
only with no order in front: an attack (a hit response's, an order's) is the
head mission in retail. So a structure fires only through the sweep and the
hit response, at what its player sees: a Trebuchet (sight 200, range 2700)
needs a spotter. `tools/re/check_guard_nomove.py` (E, 3,000 random calls
against `fire_sweep_test --guard`: one to three slots, switchers, the shot
bits and the draw, each slot automatic or manual with no target, one inside
the minimum range, one out of reach or a good one): the same final slot
modes, cleared targets and bits, and a draw exactly when a bit is set.

**Fight-move** (fx-tsearch step 6, D1 = AM-1). `World::attackMove` marks
each leg with `markFightLeg`: `attackMove`, and (off a patrol) Move response
mode -1 with the leg's point as the response goal, which is what retail's
MoveFight gives its Move_Ground, VTOL_Move or QMove (`5214b0`); a rally step
of a fight-move is marked the same way for each output. The leg's Move then
scans at its wake on every stance, falls back to the stored attacker, puts an
automatic chase leashed by maneuverleashlength from where the unit stood
ahead of itself, and after it returns there if the unit ended up more than a
third of the leash further from the goal (Move_Ground stage 3, VTOL_Move
stage 5) and goes on. The stride scan no longer runs for a fight-move leg. A
ranged walker so takes only a seen enemy inside its selected slot's range (it
used to take one unseen within its longest range + 90 px) and walks on past
one it sees beyond it. `target_search_test attack_move` (and `--legion`)
guards the leg: the attack and the mode -1 leg behind it on all three
stances, an unseen enemy walked past, the goal reached after the kill.

**No stride scan** (fx-tsearch step 7, D3). `findTarget`, `acquireTarget`
and the per-tick stride scan (`acqStride_`) are gone, and with them the guard
fronts' own hunting (plan item 15b): retail has no per-tick scan, and a guard answers
only its ward's attacker (`407f90` / `418860`) while its weapons fire through
the sweep. Every automatic pick now comes from a mission's `4d8370` or the
sweep. `target_search_test idle_coverage` still engages all 126 armed mobile
types, each first by the sweep (an idle unit beside a seen enemy in reach),
then by its mission. A switcher scans with its selected slot alone: the
Crusades Angel (Soul Stealer selected, no damage to Undead) leaves a zombie
on a fight-move, as retail's does. The SIMSTATS fields `acq_scans` and
`acq_gate_walks*` went with the scan; `mission_scans` counts the ported one.

**Ledger** (fx-tsearch, protocol 312; every automatic pick and where it now
comes from):

| Site | Retail | State | Mark | Guard |
|---|---|---|---|---|
| Fire-at-will sweep | `40f330` -> `412620` | ported before this work; paralyzer immunity added | E | `check_fire_sweep_picker` |
| Mission scan | `4d8370`, `519ac0` | ported (`retailMissionScan`, `retailScanSlot`); radius uses `sightDistance` (our doubleSight) | E | `check_mission_scan` |
| Scan gather | `411ae0` over the 7-tick list (`411700`/`4121c0`) | ported: `retailSelectTarget` walks the owner's visible-enemy list (same candidates and order as the grid gather it replaced); the `istargetingupgrade` fallback over list +0x25 is not (no shipped FBI sets it) | S | `target_search_test` hashes unchanged |
| Attack gate | `4d7de0` | ported before; no point argument needed | E | `check_issue_gate` |
| Standby | `407839` | ported | E (host) | `check_standby`, `mission_ports` |
| Standby_Wander | `40797e` | scan ported; the stroll is World's | S | `mission_ports` |
| VTOL_Standby, Legion lift | `4176bc` | ported | E (host) | `check_vtol_standby` |
| Patrol, VTOL_Patrol | `40395e`, `41a24d` | ported (forced leash) | S, E (host) | `check_ground_patrol`, `check_flight_patrol` |
| Move_Ground / VTOL_Move response, `+0xf8` fallback | `402e2e`, `4181b1` | ported; the fallback skips a carried attacker (engine guard: retail takes it and its attack's head drops it, 402e40..402e9b, 403c9c, 404778) | E (`probe_move_ground_response`), S | `attack_move` |
| Move_Ground_Formation | `402988` | ported | S | `mission_ports` |
| VTOL_Move_Formation | `4179f0` | not ported: FormationStandby branch not run | S | -- |
| Air retargets | `41962a`, `41baf0`, `41c428`, `41ce68`/`41d10d`, `41e02e` | ported | E (hosts) | the air oracles |
| Attack_Melee retarget | `4049a0` | ported | S | `mission_ports` |
| Attack_Chase retarget | `404075` | not ported: needs the approach sub-states; never fires at mode 100 | E (`probe_attack_chase_mode`) | -- |
| Guard_NoMove | `401bb0` -> `401a90` | ported; no pick (D3) | E | `check_guard_nomove` |
| Fight-move | `4b0e40` / `5214b0` mode -1 | ported as the attack-move (D1 = AM-1) | S | `attack_move`, `vermage_fight` |
| Attack_Chase stage 1 manual slot | `404476..404483` -> `519b70` | not ported: World's `setSlotsManual` always retires the slot's target (a TargetCleared retail sends only when the slot's target differs from the mission's); porting it makes Kirenna fire less (owner call) | S | `vermage_fight` |
| Stalled aim | none in retail | Legion only: `legionAimStall` sends TargetCleared once after 90 ticks of an unaccepted aim while standing (`Unit::aimWait`) | -- | `legion_vermage_fight` |
| Legion `passAhead` oncoming traffic | none in retail (no `passAhead`) | Legion only: two enemies' fight legs are never oncoming traffic to each other (a plain move still makes way for an enemy) | -- | `legion_scenario_battle-field-2x60`, `aware-*` baselines |
| Shared sight and exploration | `4f6770`, `4c66b0`, `4c6800` | ported, widened to every ally, human or computer (owner's D2 override; retail: same-team humans) | E, S | `shared_sight` |
| Passive neutrals' sight (campaign computers) | `4d2750`, `4f5940`, `4f6770`, `512ae0` | ported: share nothing, allied or not, campaign or skirmish | S | `shared_sight` (`campaign_passive_*`, `skirmish_passive_no_share`) |
| ShareMapAndLOS toggle | `4b6b60`, `4f59c0` | not ported (no Diplomacy box) | S | -- |
| Line-of-fire test in the picker | none in retail | kept as an engine extension (D4) | E (`probe_weapon_gate_loss`) | `blocked_shot` |
| Hit response, guard alarms, AttackUType | `409a50`, `407f90`/`418860`, `401580` | unchanged (no vision, as retail) | E, S | -- |
| Stride scan, `findTarget`, `acquireTarget` | none in retail | removed (D3) | -- | `idle_coverage` |

Known consequence (Legion, accepted by the owner 2026-11-03 as the retail
rule: attackers convert at weapon range, not range + 90; `legion_fightring`
retaken on that reason): a Legion fight leg ends at its formation
slot round the goal, so on `attackring-fight-60` a third or more of the
attackers can stop 110-180 px from the target, past their 96 px gun, where
Standby (in range only, as retail) never picks; Retail's mover keeps pressing
to the goal and gets them in range. Before this work the stride scan's
range + 90 px picked them up.
## Fire at a point: SUPPRESS (icd static, 2026-11-03; ported, see "SUPPRESS port" below)

Armed ATTACK on ground or on a friendly unit, and a campaign `a X Y`, is
`4de530` kind 3 with a point and no target unit (`4de65f..4de7cc`). The
resolver reads the selected slot's weapon (`519ac0`, slot record `+0xc`):

- `toairweapon` (WeaponType `+0xc8` 0x4000): no order, flyer or not.
- A flyer (type `+0x260` 0x800): AIRSTRIKE with a `dropped` weapon (`+0xc8`
  0x20), AIRTOGROUNDHOVER with `hoverattack` (`+0x260` 0x8000000), else
  AIRTOGROUND, each at the point with no target.
- A ground unit or a structure whose weapon is not melee (handler vtable
  `+0x14` false): SUPPRESS.
- A melee weapon: SUPPRESS only when the point's cell (`50e660`, a tail cell
  `0xfffe` followed back to its anchor) holds a feature that is blocking
  (feature `+0x13c` 0x20) and clearable (0x20000, see pathfinding-port.md);
  else no order.

SUPPRESS is registry row `0x5eb8b8` (`UNITMISSIONCODE_SUPPRESS`, handler
`0x404d70`, jump table `0x405548`, five stages). Mission fields: point
`+0x22`, slot `+0x4e`, reach left `+0x52`, feature target `+0x56`. Result
codes are the dispatcher's (`sim/retailmission.h`).

- **Head.** For a weapon switcher the slot is the selected one (`519ac0`).
  Event 0x40000 ends the order (5). Then the group check, on every handler
  call (chained stage calls in one dispatch included): see "SUPPRESS port"
  below.
- **Stage 0.** A flyer: 8 (the order goes). Else reach left = the slot's
  range minus its minimum range (`52aba0` - `52abd0`).
- **Stage 1.** Melee: no clearable blocking feature at the point, 5. Else
  the feature's anchor and footprint (`4930d0`) give the rectangle approach
  (`4d4e80`, as RECLAIM's), feature target = 1, wait 0x740. Ranged: a
  clearable blocking feature at the point sets feature target = 1; one that
  was there and has gone ends the order (5); else on.
- **Stage 2** (melee only; ranged goes straight on). On arrival (0x100) the
  point is walked toward the unit 8 px at a time (heading `51b340`, sine and
  cosine `5360bf`/`5360f3`) until the slot reaches it (vtable `+0x10`
  through `52ad20`), at most 100 steps, else 8. Event 0x40: a tick's sleep,
  stage 0. Anything else (the approach failed): 9.
- **Stage 3.** Every slot manual (`519b70(3)`); the selected slot aimed at
  the point (`51a770`); a non-switcher also aims each other slot it has, a
  melee one only when the selected weapon is melee. Wait 0x150040.
- **Stage 4.**
  - 0x10000, the selected (or first) slot's shot (`530140`, as its reload
    starts): with a feature target still there and clearable, stage 3 after
    a tick, else 5; without a feature target, stage 3 and result 6: the
    order goes to the BACK of the queue. Alone it fires again; Shift-queued
    points take turns, one shot each.
  - A `commandfire` weapon's shot raises 0x40000 instead (`5301a1`), so the
    order ends after that one shot.
  - 0x40 (a weapon switch, `51a8b0`): a tick's sleep, stage 0.
  - 0x100000, the aim start's refusal (`52ffd3`: the point inside the
    minimum range or out of reach): every slot automatic (`519b10(3)`). No
    mover (a structure): 9, so a tower keeps a lone order and looks again
    after `rand(30)+15` ticks. While reach left is more than
    (range - minimum) % 3 (sic, the remainder): with a minimum range, the
    ring `4d4e10(point, left + min, rand(span/6) + min)` and left -= span/3;
    without, the circle `4d4da0(point, left)` and left -= rand(range/3).
    Wait 0x700, stage 3. No reach left: 9.

The weapon side, for a point target (slot record `+6` not 0x8000; `+4/+6`
the point in pixels):

- The aim point (`51aa50`): the point, with y the terrain height there or the
  sea level (`+0x19ef8`), whichever is greater.
- Reach (vtable `+0x10`): the base `530500` is `530580`'s 2D centre test
  against the point (range 0: always); BallisticWeapon `52bbb0` first solves
  the arc from the unit's origin to the point (`52bd10`, `0x8000` = out of
  reach); DroppedBallistic `532270` is the base; MeleeWeapon `52b8a0` tests
  the point's cell against the unit's footprint box. The minimum range for a
  point is `530630` (strictly inside).
- A shot carries no target unit (`51a9a0` gives 0) and flies to the aim
  point.

The order lasts until another order replaces it, its feature target is gone,
the approach runs out of reach, or a `commandfire` shot. Not emulated yet.

The selector for the same click (`0x4dd780` mode 3, `0x4dd8ac..0x4ddc1b`,
S): no `canattack`, Normal; a `dropped` selected slot, Airstrike; with
neither a hovered unit nor a point, Attack. A mover: over ground or a friendly
unit with a melee slot, Attack when the point's cell holds a blocking,
clearable feature, else TooFar; a ranged one, Attack over ground and TooFar
over a friendly unit. A structure: TooFar over a friendly unit; over ground
TooFar out of reach (`52ad20`) or for a `toairweapon`, else TooFar inside the
minimum range (`52adb0`, I: read by its use) and Attack outside it. The resolver takes the
pointer's point (`esi`), not a hovered friendly unit's position.

Ours (the click, 2026-11-03): armed ATTACK on ground or on a friendly unit, in
the main view or on the minimap, sends `Cmd::AttackGround` at the click point
to each selected unit the resolver gives an order (`World::suppressRule`,
shared by `World::suppressAccepts` and `clickrules.h`; a melee slot reads the
sim's `clearableBlockerAtPoint`), none to the rest; a Legion net game packs
the run as one explicit order. Shift appends and Ctrl front-inserts; both
toggle a queued SUPPRESS / flyer point order within 16 px of its `+0x22`
(`QueuedKind::AttackGround`), taking its approach with it, and a Ctrl order
drops a running SUPPRESS's approach (stage 4 puts it back). A refused unit
neither toggles nor front-inserts. The cursor, so that it describes the click
(I, owner's rule): Attack (Airstrike for a `dropped` slot) for a unit given
the order -- where retail shows TooFar for a tower out of reach and for any
unit over a friendly one -- and TooFar for an attacker given nothing -- where
retail shows Attack for an anti-air mover over ground. A rally holder keeps
our attack-move rally. Fight-move (F) stays our attack-move. A campaign
`a X Y` goes through the same `World::suppress` (see "Mission order strings").

## Mission order strings (icd 0x513fb0, 2026-10-30)

A campaign mission gives its units orders in a one-letter language: a placed
unit's `.ota` `InitialMission=` and the god script's `SetMission <unit>`. Both
go through the parser `0x513fb0(unit, string, unit table)`, read here by static
analysis (the probes are in `~/tak-tmp/retail-checks/ai-mission-names/` and
`mission-orders/`). Through 0.9.11 our parser read most of the letters wrong.

**Clauses.** The string is cut with `strcspn(",")`: after leading blanks a
clause runs to the next comma, one of 256 characters or more is skipped, and
its first letter (in either case, a jump table at `0x514ac8` / `0x514b04`)
picks the handler, which `sscanf`s the rest. Each form needs exactly the
conversions it lists, else the clause does nothing, so a clause never reads
into the next (ours skipped commas as blanks: `ma, a ARATRE` became a fight-move
to cell 0,0, and `w 900 40 b ARAPRIES 1` a wait plus a reinforcement, where
retail reads one wait and ignores its tail). Every mission is pushed with
`0x4d78a0`'s queue flag set, so it goes behind what the unit has (after the
script's own last mission, `0x4d77f0`'s 0x1000 mark; the default mission at the
head goes, `0x4d79d3`); ours cleared the unit's orders at the first clause.

**Names.** `0x513ec0(name, table, from)` walks the `.ota` `[units]` records in
file order (record +0 `Unitname`, +4 `Ident`, +8 `InitialMission`, read at
`0x4c8c13`..`0x4c8eb1`) and answers the first record whose `Ident`, else whose
`Unitname`, matches case-insensitively and whose unit was made. `0x514b40`
makes every placed unit first and only then parses each record's
`InitialMission` with the table of those units; `SetMission` (`0x4d37f6`)
passes an empty table (a match there would read a null pointer), so names
resolve only in an `InitialMission`. Ours registered a unit under the name an
`i` clause gave it and never consulted the records.

**The letters** (handler addresses in the parser):

- `o A` (`514086`): `0x5198a0(unit, A)`, the Standing_UnitOrder setter: 2
  Offensive (move 1, fire 2), 1 Defensive (0, 2), 0 Passive (0, 0), for any
  live unit (no `unitstandorders` test). `o A B` with both in 0..2 writes the
  move field (unit +0x130 bits 16-17) and the fire field (18-19) themselves,
  leaving the shown order (20-21). Ours passed A to our stance, whose 0 is
  Offensive: the 987 `o 2` clauses made units Passive.
- `b` (`51451d`): `bw…` pushes BuildWeapon (stockpiling; no shipped mission
  uses it). A unit with no mover (+8) gets `" %[a-zA-Z0-9_.] %d"`, N preset to
  1 and no menu test: BUILDINGBUILD with type and count (+0x4e, +0x52), which
  builds N and ends (`401c20`, 0 ends at once, `401cf6`). A mobile unit needs
  all of `" %[a-zA-Z0-9_.] %d %f %f"`, TYPE on its own build menu (`0x519850`)
  and gets MOBILEBUILD (VTOL_MobileBuild for a flyer) at X*16,Y*16 with count 1:
  N is read and unused. Ours conjured every `b` for free after N seconds (the
  N was taken for a delay), which flooded some missions: takx26 went from 146
  to 636 units in 30 seconds.
- `i NAME` (`514893`): `0x51b4f0(unit, carrier, -1, 0, 1)`, an immediate
  attach (`51b5a0`), then `0x4d49f0`: the queue cleared but for the missions
  whose descriptor has flag 4, and BeCarried pushed in front, so the clauses
  after `i` wait aboard. No owner, capacity or distance test.
- `g NAME` (`514265`): `4de530` kind 7 (Follow_Ground / VTOL_Follow) on the
  named unit, appended.
- `w F [N]` (`514787`): `" %f %d"`, WAIT with +0x4e = F x 30 and +0x52 = N x 16.
  Wait (`401480`) with +0x52 set looks with `0x411ae0` (the owner's visible
  enemies, live and not dying, within +0x52 pixels; see
  [classic-ai-plan.md](classic-ai-plan.md)) and ends if one is there, else
  ends if its count is spent, else takes `rand(7)+7` off the count and sleeps
  that long. Ours ignored N.
- `wa [NAME]` (`514806`): `" %[a-zA-Z0-9.]"` (no underscore) names the unit to
  watch, a name that resolves to nothing drops the clause, none watches the
  unit itself. WAITFORATTACK (`4017a0`, descriptor flags 0x204) keeps a listener
  on it from construction (`4d6c40` -> `519990`, owner +0x1e the mission, whose
  `4d4d00` ORs an event into +0x6a), so a hit while the order still queues
  counts; it waits on mask 0x18 and ends: 0x10 is the hit response's event
  (`409a50`), 0x08 its unit's listeners let go (`519a60`). No watched unit:
  done at once. Ours released when any enemy came within sight and ignored
  NAME.
- `a` (`5143b5`): `" %f %f"` is `4de530(out, 3, unit, 0, &point, 0)`, the
  click's kind 3 with a point (SUPPRESS for a ranged ground unit or a tower,
  AIRSTRIKE / AIRTOGROUNDHOVER / AIRTOGROUND for a flyer, nothing for a
  `toairweapon`; a melee unit only with a feature there), pushed by `4d78a0`
  with the queue flag 1. E (`tools/re/check_mission_attack_point.py`): the
  point is (`ftol(X*65536)` * s, 0, `ftol(Y*65536)` * s) with s the scenario's
  `+0xdc5` (`513fd6`), which is 16 for an `.ota` with a `[Map Data]` section
  (`4c86eb`; 1 without, `4c8748`; S) and all 74 shipped missions have one, so
  X*16, Y*16 px with no half cell; `m X Y` (kind 2), `p X Y [T]` (kind 9)
  and `u X Y` (kind 5) scale the same way (E). When the
  resolver gives no order the clause pushes nothing and the next clause
  still reads (E). Else `" %[a-zA-Z0-9_.]"`: a unit
  type (`0x515ac0`) pushes ATTACKUTYPE, else the named unit gets kind 3.
  AttackUType (`401580`): stage 0 fails without a first weapon (UnitDef +0x264
  & 0x20, set from weapon slot 0 at `0x4c1672`), else sleeps `rand(90)+1`;
  stage 1 walks the whole unit table for live, not dying units of the type
  whose player record is not the unit's and that `4de530` kind 3 accepts,
  scores each hi(dz^2) + hi(dx^2) - `rand(score / 2)` (a later tie wins), and
  puts an attack on the least in front (`4d7750`; `4d7640` sets AttackUType
  back to stage 0), or ends when there is none. No sight test. Ours attacked
  the nearest enemy of the type once, and fight-moved to a point.
- `v F` (`5148e1`): unit +0xf4 = F. While it is not 0, getMult (`5193c0`)
  answers F, getLevel (`519310`) answers 10 and the level setter (`519370`)
  does nothing; a `noveteran` type (+0x268 & 0x10) answers 1 and 0 first. Ours
  ignored it ("movement scalar").
- `s` (`514876`): MAKESELECTABLE (`401450`: unit +0x130 |= 0x20, &= ~0x2000).
  The owner does not change; ours gave the unit to the player while parsing,
  which handed 44 placed enemy units in 10 missions to the player at the start
  (takx04's Aramon keep and castle among them).
- `d` (`514858`): SELFDESTRUCTFG with +0x4e = 1, so `4017e0` deals the 30000
  type-5 damage when the mission comes up. Ours killed the unit while parsing:
  takmission23's three troopships (`wa leim, w 60, u ..., m 0 192, d`) died at
  the start.
- `m X Y` / `ma X Y` (`514120`), `p X Y [T]` (`514307`, +0x4e = T x 30),
  `u X Y` (`5141d3`), `c` (`514a48`: the cloak bit when cloakcost > 0) and
  `r` (`514910`, resurrect; no shipped uses).

**Points (2026-11-04; E = emulated, S = static).** Every handler that reads
`" %f %f"` -- `m`/`ma`, `p`, `u`, `a X Y` (`5143d6`) and the mobile `b`
(`514573`) -- builds its point the same way: x = `ftol(X * 65536.0) * ebx`, the
same for z from Y, y 0, a 16.16 pixel value (E). `ftol` (`5d3d54`) is `fistp`
with the control word's rounding set to chop into 64 bits, the low dword kept,
so `X * 65536` is cut toward zero before the multiply: 7.3 is raw 478412 x 16
(116.7998 px), -0.3 is -19660 x 16 (E, both signs, integers and fractions). `ebx`
is the scenario's +0xdc5, read once at `513fd6` from `[0x62d55c]+0x175dc`: 16
when the `.ota`'s GlobalHeader has a `[Map Data]` child, set at `4c86eb` after
`542940` (the child-by-name search, `542f80`, case-insensitive) finds the name
`4c86b0` copies from `0x615548`; else 1, set at `4c8748` (E, `4c8710` on a fake
TDF with and without the child). All 74 shipped missions have `[Map Data]`, so
their points are X*16, Y*16: a cell's corner, not its centre. The `.ota`'s own
unit `XPos`/`ZPos` take the same `<< 16` times +0xdc5 (`4c8cf7`, `4c8d2b`, S);
`SetMission` reaches the same parser (`4d37f6`, S); the god script's `Create`
puts a type's footprint corner at the cell, ((X*2 + footprintx) << 19, then
`5085c0`, S). Ours used the cell centre (X*16+8) for `m`/`ma`/`p`/`u` through
0.9.19; `MissionScript::orderPoint` is retail's for them and `b`, the scale
from the header's `[Map Data]` (`mission_orders_test` points,
`tools/re/check_mission_points.py`), and for `a X Y`, whose SUPPRESS order
`check_mission_attack_point.py` also checks. The
shipped strings hold 1160 `m`, 248 `ma`, 3480 `p` and 81 `u` points in 69
missions, all whole numbers, so there the change is the half-cell shift alone.

**Placed units (2026-11-04; E = emulated, S = static).** `4c8ce1..4c8d64` reads
a `[units]` record's `XPos`/`ZPos` with `atoi` (`543190` -> `5d47af`, S) and
stores x = X*16, z = Z*16 (E): no half cell. y is `YPos << 16`, unscaled, and
effectively unused: `51b2a0` recomputes a mobile's y (hover / floater `51b220`
/ ground fit `51ad20`, S). Angle is stored as the word `(A << 16) / 360`
chopped toward zero (180 -> 0x8000, 359 -> 0xff49, E). `514b40` finds the type
(`5159f0`), runs `5085c0(type, &pos)`, then `512060(Player-1, typeid, pos,
1,1,0,0,1)` and writes the Angle word over the birth heading `+0x7e`
(`514d02`), all E. `5085c0` snaps a type whose bmcode is 0 (`4c1768` turns any
bmcode but 0 or 1 into 0): origin cell o = `(p - (f << 19) + 0x80000) >> 20`,
position `(2o + f) << 19`, y the terrain (`507990`) at the origin cell. So a
building stands at X*16 + 8*(f & 1), XPos its centre cell; a mobile (bmcode 1)
stays at X*16, and a 2x2 one at X=10 has origin cell 9 (E, footprints 1..5,
2x3, 1x4). `511990` keeps the position as given and stores the origin cell in
`+0x74/+0x76` (E); `512060` neither moves the unit nor searches for room nor
checks occupancy (S), `5066f0` only stamps the footprint, and every unit starts
in move mode 1, so a flyer starts on the ground (`511b18`, S). Ours used X*16+8
for every unit through 0.9.19 (mobiles off by 8,8, even footprints by 8 on that
axis) and turned Angle into a port heading directly, a half turn from retail's
(our port heading 0 is retail 0x8000, Angle 180; 5418 of the 7341 placed units
use 180). `MissionScript::placedPoint` / `placedHeading` and `footprintSnap`
port it (`mission_orders_test`, `tools/re/check_mission_regions.py`). Skirmish
start positions are unaffected: this path only reads a mission's `.ota`.

**`Create` (2026-11-04, E).** `4d35c0` takes 3 args (player = a0's low byte,
X = int16 a1, Z = int16 a2) or 2 (the player byte from `[game+0x306e]`); any
other count makes nothing and returns 0. All 1851 shipped calls pass 3. The
point is x = (X*2 + footprintx) << 19 = X*16 + footprintx*8, the same for z:
the type's footprint corner at the cell, for mobiles and buildings alike.
`5085c0` leaves it unchanged (for a building it only sets y to the terrain at
X,Z); a mobile's y is recomputed by `51b2a0`. Then `512060(player, typeid, pos,
1,1,0,0,1)`, the flags placement uses: no search, no free-spot check and no
heading, so the unit keeps 511990's birth heading (0x8000 + orientation -
buildangle/2 + rand(buildangle)). It returns the unit id (`+2`). Ours used
X*16+8 and heading 0 through 0.9.19, right only for a 1x1; 154 of the shipped
calls were off, an 8x18 building by 56 px in x and 136 in z.
`MissionScript::createPoint` ports it.

**Trigger regions and UnitCreated (2026-11-04; E = emulated, S = static,
I = inferred).** `SetTrigger` (`4d3a53`) ignores the call and returns 0 when
16 regions are armed (`+0xaa4`) or slot a0 already is; with 4 args it stores a
circle, centre (int16 a1, int16 a2), radius a3 (`4d2b00`), with any other count
a rectangle, int16 a1..a4 (`4d2ae0`), as given, and returns the id (E).
`RemoveTrigger` (`4d3b22`) frees an armed slot, lowers the count and clears bit
`1 << id` of `+0xd4` on every live unit; an empty slot does nothing (E). Every
tick `51d3e0` -> `51de30` -> `[0x62da3c]` slot 7 (`4d43f0`) runs `4d33a0(unit)`
for every live unit of players 0..9 in pool order (S). `4d33a0` (E) looks only
at live (`0x1000000`), not dying (`0x1000`), finished (`+0x108 == 0.0`) units.
It first fires `UnitCreated(unit id, owner byte +0xfd)` once (bit 31 of
`+0xd4`), then for regions 0..15 `TriggerHit(id, unit id, owner byte)` once per
region (bit `1 << id`, cleared only by `RemoveTrigger`; leaving and coming back
does not fire again). A rectangle (`4d2b20`) holds the footprint-origin cell
`+0x74/+0x76` when x1 <= x <= x2 and z1 <= z <= z2, inclusive with no swap, so
an inverted one never fires (no shipped script has one); a circle when
max(|dx|,|dz|) + (min >> 2) (`535e90`) <= r, in cells from the origin cell:
(34,30), (26,27), (30,26) and (33,33) fire for centre (30,30) r 4, (35,30)
does not. The carried bit (`+0x114` bit 4) is not tested; whether a carried
unit's `+0x74` follows its transport is not established (I), so ours keeps
skipping embarked units for regions. Ours through 0.9.19 compared the pixel
position with [x1*16+8, x2*16+8] (min/max), used a Euclidean circle of r*16
px, fired on every re-entry, looked at unfinished and dying units, re-armed an
armed slot, and fired `UnitCreated` only when a build finished, with owner 0:
mission 20's handler wants owner 1 and never ran, and takx05, takx22, takx23
and mission 06 test for owner 0. `MissionScript::sweepTriggers` ports
`4d33a0`, the owner byte being the `.ota` Player-1 the slot came from
(`retailTriggerRect` / `retailTriggerCircle`; `mission_orders_test`,
`tools/re/check_mission_regions.py`). Ours runs the sweep after the god
script's VM tick, so a handler it starts runs on the next tick (S, ours).
`51d3e0`'s outer loop is the player rows 0..9 (`bl`, rows of 0x110 at
`+0x2404`), each walking its own pool `+0x2478..+0x247c` and calling `51de30`
for every live unit, carried ones too (S); `51de30` makes the call right after
the unit's own COB tick (`56c870`). So the handlers start owner by owner; ours sorts the sweep by
owner byte, and within an owner keeps spawn order (retail's pool slot is a
random free one, `retailAllocateSlot`). A circle with radius 0 is a rectangle
to `4d2b20` (it tests `+8 != 0`), whose far corner `4d2b00` never writes
(uninitialised heap); ours keeps it a one-cell circle, and no shipped call
passes r 0 (S, scan of the 153 constant `SetTrigger` calls).
`MoveUnitToRadius` and `Unit/AnyUnitPasses{X,Z}` already used retail's cells.

When the parser has pushed a mission it clears the unit's selectable bit 0x20,
and unless any of them was a patrol, an attack on the ground that got an order,
`s` or `d` it appends MAKESELECTABLE (`514a7d`; `[ebp-0x20]`, set by those
clauses at `5143ad` / `51443f`, is never cleared during the parse; E: a given
`a X Y` and a `p X Y` set it, an `m X Y` and a `u X Y` do not; `a NAME` and
`a TYPE` do not set it): a player's unit under a script cannot be
selected until its script is done.

**Ours (0.9.12).** `MissionScript::applyOrders` reads clauses as above, with
the `.ota` records as its name table (`MissionScript::Placement`). The orders
with state of their own are World orders with a `ScriptedOrder` entry
(`Order::scripted`, hashed): AttackType (`tickScriptedOrder`, the scan and
draws as retail's on the game stream), Train (a factory queues N in its build
queue and holds the next order until they are built,
`noteScriptedProduction`), WaitAttack (the watched unit's `attackedTick` from
when the order was given), WaitNear and SelfDestruct. Behind such a hold, as
behind a wait, the next leg does not start (`World::scriptHold`): a route
installed for a build or a move behind it would replace every order up to it,
the hold included. `Unit::vetOverride`
holds `v`, and `World::embarkInto`, `scriptStandingOrder` and
`scriptStandingFields` do `i` and `o`. The Retail AI reads such a head order
as retail's (class 0x13, its descriptor flags, never 0x2000), so it counts the
unit busy. `mission_orders_test` (fixtures, and `--data` on takx04 and takx07)
and `ambush_test` cover it; a 30-second run of all 74 shipped missions loads and
runs every one. `a X Y` (since 0.9.20) is `World::suppress` at X*16, Y*16
(`MissionScript::orderPoint`) with
the queue flag, the click's order; the Retail AI reads a head SUPPRESS as
row `0x5eb8b8` (class 1, 0x1000410), and a flyer's air attack at the point as
its row less the target bit the ctor clears (`4d6d66`): AIRSTRIKE class 2,
0x1000400; AIRTOGROUNDHOVER and AIRTOGROUND class 1, 0x1000000 (E,
`tools/re/check_air_point_mission.py`). A kamikaze type's ATTACK_KAMIKAZE from
the resolver is not ported (no shipped use). `m`, `p`, `u` and `b` take the
same `orderPoint` (since 0.9.20; `m`, `p` and `u` took the cell centre, X*16+8). Not ported: the patrol's time, the selectable bit,
BuildWeapon and resurrect; SetMission's owner test (`0x4d37be`: a player of
kind 1 or 2 only) is not applied.

## Selection ring (`4fd0d0`)

Both draw passes (`4fca0f`, `4fcbef`) call one routine, and it draws through the
renderer's vtable (`[esi]+0x64`, 24 vertices as lines), so Glide and software share it.

- **Box.** `546fa0(model, &min, &max, 0)`. The last argument is the recursion flag, so
  `546fe0` returns at `5470e4` without visiting siblings or children: the box is the
  ROOT piece alone, the ground plate in every shipped model checked (447 extracted
  models, 391 with a 4-vertex root). Both corners start at zero, so the origin is always
  inside, and a root of 2 vertices or fewer adds nothing.
- **Size and centre.** Radii are 1.2 (`5f03f0`) times the half extents; the centre is the
  box's centre on all three axes. So a tall unit's ring lies at its feet at the plate's
  size: the Death Totem's is 38.3 by 38.6 at the origin, not its 34.8 by 25.2 body at
  mid height 72.
- **Turn.** Each point goes through `535d50` with the three angles at `unit+0x7c`
  (heading `+0x7e`), Ry Rx Rz, so the ring turns and tilts with the body. `unit+2` is only
  the dash phase.

`client/selectionring.h` is the port; `selection_ring_test` checks it.

## SUPPRESS port: what was re-read and what is ours (icd static, 2026-11-03)

The order itself is read stage by stage in "Fire at a point: SUPPRESS" (on
`fx-clicks`). For the port the details below were read again in the code (S);
none was emulated (no E). What World does differently is marked I.

- **S, canattack.** The resolver gives nothing without `canattack`
  (`4de62d -> 4de6ed`), which load clears for an armed type with `bmcode` 0
  (`+0x24a`, read at `4c0057`) and a canbuild menu (`4c18d7..4c18ff`). Ours:
  `World::suppressRule` answers None for such a type, so neither a click, a
  `Cmd::AttackGround` nor a mission's `a X Y` puts SUPPRESS on an armed
  factory (no shipped one; a mod's). `mission_orders_test`.
- **S, resolver** `4de65f..4de7a6`: the selected slot's weapon (`519ac0`: a
  switcher's selected slot, else the first present). `+0xc8` 0x4000
  (toairweapon): no order; a flyer (`+0x260` 0x800) goes to the airstrike
  branch; vtable `+0x14` false (not melee): SUPPRESS (`0x615db8`); melee:
  SUPPRESS only over a blocking (0x20), clearable (0x20000) feature.
- **S, head.** For a switcher the slot is `519ac0`'s, kept in a local: `+0x4e`
  is never written back. Event 0x40000 returns 5 before the group check.
- **S, stage 2** (melee): the reach test before the walk; the walk stops at 100
  steps, and reaching on the 100th still returns 8.
- **S, stage 4.** The span is `52aba0 - 52abd0`; the ring is `4d4e10(point,
  left + min, rand(span/6) + min)` (span/6 and span/3 by `0x2aaaaaab` and
  `0x55555555`, truncated toward zero), then left -= span/3; the circle is
  `4d4da0(point, left)`, then left -= rand(range/3). The test is left > span % 3.
  A feature target still there: stage 3 and a tick's sleep (4); gone: 5.
- **S, 51a770** (a point for a slot): a switcher takes it on its selected slot
  only; the point is stored in whole px (`sar 16`; a z of 0x8000 becomes 0x8001);
  the unit's events are masked with 0xfe08ffff, as `51a700` does for a unit.
- **S, 52fe30's point branch** (`52ff7d`): `51aa50` gives the aim point, then
  `530630` (inside the minimum range, strict) and vtable `+0x10` (reach); a
  refusal ORs 0x100000 into unit `+0xd0`, for every aimed slot. Then the
  facing request as for a unit (`52feaa`). `530500` (the base reach for a
  point) has no air-flag tests, range 0 reaches anywhere, and is inclusive;
  `52bbb0` solves the arc from the origin to the aim point and then calls it;
  `52b8a0` boxes the point's cell (16 px) against the footprint as
  `52b980` boxes two units. `52ad20` (stage 2) is vtable `+0x10` on the raw
  mission point.
- **S, 51aa50** for a point (record `+6` not 0x8000): x and z the point's px
  << 16, y the terrain height there (`511170`) or the sea-level byte
  (`+0x19ef8`), whichever is greater.

Ours (`World::suppress`, `tickSuppress`, `suppressStage`, `tickPointWeapons`):

- I: stage 4's ring or circle is a ground leg pushed ahead of the order
  (`Order::suppressApproach`, run like RECLAIMAREA's approach: arrival or
  failure ends it) instead of a controller on the order; the order resumes at
  stage 3 when it ends. In Legion it stays on the native navigator (a Legion
  move does not stop on a circle or ring). The melee rectangle is on the order
  itself, as RECLAIM's.
- I: a slot's point (`Unit::slotPoint`) is dropped, with TargetCleared, as soon
  as the SUPPRESS that aimed it is not the current leg, including when the
  order has gone to the back behind another kind of order; but not while the
  group check's formation leads with SUPPRESS right behind it (below).
- E (`probe_mana_fallback.py`, 2026-11-04), the mana fallback after a shot:
  in `52ae90` (`52af6c..52afa1`), after the release `530220` of a slot whose
  record holds SET 23 (flag 0x10), whether or not a shot could be allocated
  and whatever the reload, a switcher steps down from the fired slot while
  the slot's cost (`531ea0`: manapershot `+0xd4` over `5193c0`, 1.0 for
  noveteran, else `+0xf4` when set, else 1 + 0.1 x level) is above the mana
  left, and `51a8b0` selects the slot it lands on: the highest cheaper slot it
  can afford, or slot 0 when none is (a dearer slot in between is skipped);
  slot 0 never switches. A unit without weaponswitching has selector 3
  (`511a4f..511ab0`) and never switches. It reads no mission (`+0x60`) and no
  owner (`+0xb8`): a player's switcher falls back as an AI's does, at a point
  or on a unit. The release pays through `4010d0`, which subtracts only a cost
  the mana covers and whose result is ignored: the shot leaves unpaid and mana
  never goes negative (readiness `52fff0` asks for the cost). World's
  `manaFallback` follows the script release (`tickScriptWeapon`) and every
  non-script shot; `fire` pays as `4010d0` and uses the divided cost, as does
  readiness. `tools/re/check_mana_fallback.py` compares 1466 fixtures with
  the native routine, 852 of them on `5193c0`'s level path (`+0xf4` 0: the
  experience over experiencepoints). Until 2026-11-04 World had no fallback (a player's switcher
  waited for mana, an AI's dropped only when the weapon sweep `40f411` next
  visited it), a script release took mana below zero and the cost ignored the
  veteran multiplier.
- E, `51a8b0` raises 0x40 (`+0xd0`) on every switch and no other bit. Its
  readers sleep a tick and restart at stage 0: SUPPRESS (stage 4), and
  AirToGround (`41c122`), AirToGroundHover (`41da9d`), AirStrike (`41b728`)
  and AirToAir (`41ccc4`) at a point or on a unit; Attack_Chase (stage 1,
  mask 0x1f40068) and Attack_Melee (0x40068) take it in the same dispatch
  (missions run after `52ae90`) and aim the new slot. I: World raises it for
  a unit on SUPPRESS or on a flyer's attack only, the walk back its group
  check put ahead of either included (the formation waits on no 0x40, so the
  bit stays until the attack, from stage 0 again, takes it). A walker's unit attack
  runs per tick on the selected slot (`tickCombat`), with no mission to take
  the bit; set there it would linger and wake a later order. A commandfire
  shot's 0x40000 is passed only for a point target.
- E (`probe_attack_chase_stop.py --switch-tick`, 2026-11-04), a walker's
  switch: Attack_Chase waits in its fire stage on 0x1f40068 (`404493`), its
  approach stages on 0x700 only. A switch right after `52ae90` (as the mana
  fallback makes it) wakes it in the same dispatch: the head reads the new
  selected slot (`403bed`) and the stage gives it the target at once
  (`51a700`, `40448e`) when it is in reach (`52adf0`, `52ad60`). With the bit
  taken away the slot waits for the stage's own wake, 3 to 8 ticks on. The
  probe runs VERMAGE natively (52ae90, the COB VM, 4d8450, the mover) with a
  second slot and switches at six ticks of an engagement: the target goes
  to slot 1 in the switch's own tick every time. Ours: `tickCombat` aims the
  new slot (`tickScriptWeapon`) in the update its weapons switched it, as the
  stage-1 aim (World's aim start reaches the script at the next COB pass, as
  52ae90's does); until 2026-11-04 it waited for the next update
  (`retail_script_test`). I: a switch to a melee slot hands over to
  Attack_Melee (`403c17`) in the same dispatch in retail, in World on the
  next update.
- E (`check_retail_group.py` suppress mode, 2026-11-04), the group check
  (`404e12..404f1b`), after the 0x40000 exit, on EVERY handler call, chained
  stage calls in one dispatch included: `51d1e0(unit, 1, 4)`. Out of the slot
  on a mission already flagged 0x8000000, `rand(3) == 0`: `50c480` unless the
  next mission (`+0x66`) is a group mover (0x1000000 without 0x4000000), and 8
  (the order goes). Otherwise out of the slot: the flag set,
  Move_Ground_Formation (row `0x604d70`, handler `402880`) pushed in front
  (`4d7750`) at level -3 with response mode 0 (no target scan), and 0: SUPPRESS
  starts again at stage 0, with a fresh reach budget, when the formation
  retires at level 3. In the slot, a flagged mission clears on `rand(15) ==
  0`. All draws are `535cc0`. `4d7640` discards the push unless the owner's
  player kind is 1 or 2; World's AI units are never grouped, so it does not
  matter. The row's flags are 0x1000410 (a group mover), Move_Ground_Formation's
  0x11000400.
- E (`walkback/probe_walk_fire.py`, `probe_resume.py`, 2026-11-04), firing on
  the walk back: the push touches no weapon slot, and the per-tick weapon
  update (`52ae90`) reads no mission. The manual slot stays aimed at the point
  and fires per reload: a turret type (turninplacerate 0) whatever the body's
  heading, a turninplacerate type only while |body heading - direction to the
  point| <= max(aimtol, 512); the body does not turn to face while walking
  (`4d9b6a` turns only a stopped unit). The walk's 0x10000 / 0x100000 stay in
  the event word (SUPPRESS does not wait on them) until the stage-3 re-aim
  (`51a770`, mask 0xfe08ffff). The formation's retirement: `4d6da0`,
  `519b10(3)` (every slot automatic) and `51a7f0`, one TargetCleared per slot
  holding the point; SUPPRESS then runs stages 0..4 in the same dispatch and
  re-aims, its reload carried over (no second shot, no gap).
- Ours, Retail pathfinding only (Legion has no group record):
  `retailAttackGroupCheck` (`sim/retailgroup.h`) is the head, shared with the
  flyer attacks; `suppressStage` runs it after the 0x40000 exit and
  `pushSuppressFormation` puts the formation leg ahead (deferred as the stage-4
  approach is). The leg runs on the Move_Ground host (`retailGroundFormation`,
  level -3, mode 0) from `tickCombat`, after the weapons, as retail's dispatch
  follows `52ae90`; the main loop's call skips it. `syncSlotMode` keeps the
  slots manual and their point while it leads with SUPPRESS right behind it
  (`World::formationAttack`, which knows the leg by `Order::attackFormation`,
  set at the push: a Move's formation left ahead of a queued attack when the
  Move is taken back stays a Move's and retires); `tickPointWeapons` fires
  them and posts the facing request, and SUPPRESS's brake does not apply. A
  body turns to the request only stopped with an idle navigator: with no
  orders, or (`tickAttackWalk`) with the formation's controller let go (at the
  centre, asleep in stage 2). Its retirement sets every slot automatic
  with TargetCleared, and SUPPRESS runs from stage 0 in the same tick. The
  pushed leg takes SUPPRESS's melee rectangle and route with it (stage 1 makes
  them again). The leg is not an issued one: Shift on the point removes
  SUPPRESS and the leg together; Ctrl drops it (the order behind starts again
  at stage 0, as from an approach); result 6 rotates only SUPPRESS, which
  never runs while the leg leads. `retailMissionFlags` reports 0x1000410 for
  SUPPRESS and for its approach leg (the approach is its own controller in
  retail). `suppress_test` checks a spread Ctrl group: a turret outlier
  fires on the walk back and again once back, a shot due on the tick the
  formation retires leaves then and once, the outlier counts as a moving
  member, one that must face holds fire on the move and turns once stopped
  at the centre, a weapon switch on the walk leaves 0x40 for SUPPRESS, a
  patrolling group makes it leave the order (flagged and out of its slot),
  Shift and Ctrl on the walk, and a Move's formation left ahead of an attack
  still retires; Legion: none of it.
- E, as retail (`tools/re/probe_guided_projectile.py`, 2026-11-04): a Guided
  shot at a point flies straight. `52e030` steers only when `+0x78` holds a
  target with 0x1000000 (alive) and without 0x1000 (dying); a point shot has
  none. World passes the shot's own position for a point shot or a dead
  target, so `retailGuidedTick` makes no turn either. A modelled shot
  (`+0x93`; only the Tracking Arrow, `araarrow2`, has a model) still rebuilds
  yaw and pitch from its velocity on every update (`52e23d`), with no target,
  a dead or dying one, or a zero turn angle; World did so only after a turn
  until 2026-11-04. The probe checks all four cases against the native
  routine.
- Campaign `a X Y` gives this order too (`mission.cpp`, `mission_orders_test`).

`suppress_test` checks it in both pathfinding modes.

### Flyers at a point: AIRSTRIKE, AIRTOGROUNDHOVER, AIRTOGROUND (icd static, 2026-11-03)

The same resolver's flyer branch, and the three missions' paths with no
target (mission `+0x16` 0), read in the code (S); none emulated (no E). The
missions' stages are the ported ones (`sim/retailairattack.h`); only what
differs without a target is listed.

- **S, resolver** `4de770..4de7e3`: after the `toairweapon` test, the
  selected slot's `dropped` (`+0xc8` 0x20) gives AIRSTRIKE (`0x615dac`); else
  the TYPE's hoverattack (`+0x260` 0x8000000; the weapon's is not read)
  AIRTOGROUNDHOVER (`0x615d98`); else AIRTOGROUND (`0x615d8c`).
- **S, mission ctor** `4d6c40`: the point is copied to `+0x22`; without a live
  target `4d6d66` clears the target bit 0x200 from the row's flags, so the
  heads' "no target and 0x200" seek (`41c08c`, `41d8ef`, `41b68a`) is never
  taken by an order at a point.
- **S, AirToGround head** (`41bf80`): no weapon (`+0x130` 0x8000000) or a
  type with hoverattack: 7; a selected `dropped` weapon or events 0x2a: the
  lost exit `41c8f6`, which without a target pushes VTOL_SEEKATTACK
  (`0x6055a0`) at `+0x22` when the flyer is alive and not dying, the attack is
  last (`+0x66` 0) and not on Hold Fire (`+0x130 & 0xc0000`); 5 either way.
  `41c13c` copies the target's position to `+0x22` only with a target, so the
  point stays as issued. Stage 2 (`41c357`): `51a770(unit, +0x22, slot)`, the
  point for the selected slot, in place of `51a700`. Stage 4's rescan reads
  `+0x16` first (`41c415`): never without a target.
- **S, AirToGroundHover head** (`41d820`): the type's hoverattack is required
  (else 7); `dropped` or events 0x28: the exit `41e28f`, which without a
  target pushes the seek at `+0x22` when last and not on Hold Fire (no alive
  test there). Stages 0, 1 and 3 read the target's position or led SweetSpot
  into `+0x22` only with a target; stage 2 (`41de1c`) aims the slot at the
  point (`51a770`). Stages 3 and 5 (`41df8f`, `41e225`) build a POINT
  controller (`4e40e0`, flags 0x20) where a target gets the target controller
  (`4e4140`, 0xa3 with the entity), give it a fixed heading (`4e4520`:
  `51b340(ring point, +0x22)`, the ring point as seen from the point), then
  radius 4 and the attack altitude as with a target. With no entity
  `4e41b0` lets it go on arrival (`524c10` 0x100, the navigator's 0x400).
- **S, AirStrike** (`41b5e0`) needs nothing new: its no-target branches were
  already ported (`retailAirStrike`, `in.target`); stage 5 aims the slot at
  the point (`51a770`), stage 4 installs the release ring on the point.

Ours (`World::suppress`, `tickAirPoint`, the three missions' hosts with a
null target; `Order::airPoint`):

- I: World's unit-target hover also takes a weapon's hoverattack; the point
  order follows the resolver (the type's only).
- I: the slots' point is held only while the attack is the front order: a
  step-out ahead of it drops it (TargetCleared), and the mission's restart at
  stage 1 aims it again.
- The weapon-switch event 0x40 is raised for a flyer on an attack at a
  point or on a unit, as for SUPPRESS (it restarts the mission); see the
  SUPPRESS port.
- E (`check_air_attack_run.py`, `check_air_traffic.py`, 2026-11-04), the
  group check of AirToGround (`41c19a`) and AirStrike (`41b7a4`) is the hover's
  (`41daff`): after the `+0x56` leash, `51d1e0(unit, 1, 5)`; out of the slot
  and flagged, `rand(2) == 0` leaves (8, `50c480` by the next mission's flags
  as SUPPRESS's); else the flag set, VTOL_Move_Formation (`0x6054b8`) pushed at
  level -4, and 0; in the slot `rand(25)` clears the flag. For a unit and a
  point alike. Row flags: AirToGround and AirToGroundHover 0x1000200,
  AirStrike 0x1000600 (at a point the ctor clears 0x200). Ours: the three
  templates share `retailAirGroupCheck`, the hosts `World::AirAttackGroup`;
  `retailMissionFlags` counts the three missions as group movers. The checks
  answer `51d1e0` at random and compare every result, flag, draw, leave and
  push with the drivers. The formation leg keeps the attack's slots as they
  are (the push touches none): a slot on a point fires on the way
  (`tickPointWeapons`), and a manual slot the attack gave its unit target
  (`51a700`, `Unit::slotTarget`) aims and fires on it per reload
  (`tickAttackWalkWeapons`); one the push came before has no target and does
  not. The leg is dispatched from `tickCombat` after the weapons
  (`tickAttackWalk`), as SUPPRESS's is, so a shot due on the tick it retires
  leaves before `4d6da0` drops the aim (TargetCleared on a unit target's
  slots). `suppress_test`: an all-flyer Ctrl group's outlier flies back on
  VTOL_Move_Formation and attacks again, at a point and at a unit, for each
  of the three; one left behind after it has aimed fires on the way back
  (AirToGround at either, the hover at a point), and a shot due as the
  formation retires leaves then, once. The dispatcher (`4d8450`) re-reads the
  head (`+0x60`) after the attack returns 0, so the formation `4d7750` put
  there runs its first stages in the same dispatch (S, as SUPPRESS's):
  `AirAttackGroup::push` dispatches it (`tickFlightPatrol`) and the mover
  flies its controller that tick; until 2026-11-04 it first ran at the next
  tick (`suppress_test`: no formation waits a tick at stage 0).
- S, the registry rows (read statically, 2026-11-04): VTOL_SEEKATTACK
  (`4192f0`) and VTOL_SEEKGUARD (`4197d0`) both carry
  0x1000600, so a seek is a group mover; whether their ctor clears 0x200
  without a target is not read, and the group check reads only 0x1000000
  and 0x4000000. `retailOrderFlags` and `retailMissionFlags` give World's
  seeks (which carry no target unit) that row.
- E (`tools/re/check_air_point_mission.py`, retail's `4d4bf0` and `4d6c40`):
  with no target the mission's flags are the row's less 0x200, so the Retail
  AI reads AIRSTRIKE as class 2, 0x1000400 and AIRTOGROUNDHOVER and
  AIRTOGROUND as class 1, 0x1000000 (`ClassicBrain`'s `firstMission`; it read
  a move before 0.9.20).
- E (the same check), a flyer's attack on a unit (the resolver's unit branch,
  `4de8b0..4de9ea`: AIRTOAIR at a flyer, AIRSTRIKE on a selected `dropped`
  weapon at a unit that cannot fly, else AIRTOGROUNDHOVER / AIRTOGROUND):
  with a live target each keeps 0x1000200, class 1; AIRSTRIKE is class 2,
  0x1000600 with a point and 0x1000200 without. A player's order passes the
  target's position as the point (S, `521a66..521abc` -> `4d7a30` ->
  `4d78a0`), an automatic attack none (S, `4d7de0`: `4d8027..4d8035`).
  `firstMission` read every unit attack as Attack_Chase (1, 0x1000280),
  flyers included; it now reads the air rows (AirStrike's goal its `+0x22`,
  which follows the target), and a flyer's patrol as VTOL_PATROL (0x1000412,
  0x4000 once anchored) instead of Patrol's 0x3000410. The AI's readers
  (0x200, 0x400 with the goal, 0x2000, class 1 or 2) see the same for every
  shipped type; only a mod's `dropped` flyer (class 2, 0x400) reads
  differently. `mission_orders_test`.

`suppress_test` (both modes): `creaeri` flies runs over the
point and hits it (24 shots, 4 px off); a Tarbeak hovers ~134 px off a point
(distance 150) and hits it; a `dropped` stand-in strikes it; a weapon that
turns `dropped` ends the order, with the seek at the point on Fire At Will
and none on Hold Fire; a flyer's `toairweapon` gets no order.

## Shift order overlay: area boxes and range circles (icd static + emulation, 2026-11-03)

The order overlay runs only while Shift is down: `4fcc46` calls `544430(0x198)` (the
key poll) and, when it reports the key held, `5210a0`. That routine walks the local
player's own units (`[player+0x74]..[+0x78]`, stride `0x138`), skipping dead ones
(`+0x130 & 0x1000000` clear) and `+0x130 & 0x1000`, and calls `4d6340(unit, mask, ...)`:

- mask `0x3f` for a selected unit (`+0x130 & 0x10`) and for three special units: the
  pointer at `g+0x14ea4`, the unit id from `4b3d40` (`62d734` while `62d730` is 1) and
  the unit `527cb0` returns (id `g+0x17624`, written at `525da4` from `521260`, the
  pick under the mouse: the hovered unit);
- for every other own unit, `0x01` when one of the three special units' types has
  `+0x132` set and `0x20` when one of them has `canreclaim` (`+0x264 & 0x800`, parsed
  at `4c073a`); never `0x10`.

`4d6340` walks the unit's mission list (`+0x60`, next `+0x66`) and, per mission, ANDs
the mask with the kind's descriptor flags (`[0x62db84] + kind*25`, `+0xc`): `0x01`
`4d4f40`, `0x20` `4d5180` (area box), `0x02` `4d5700` (order line), `0x04` `4d5f60`,
`0x08` `4d5930` (end marker), and `0x10` `4d6530` once per unit. Every kind has `0x10`
except park, load, unload, area reclaim (`0x22`) and the two facing kinds.

**Area box (`4d5180`).** Colour white (`-1`), or yellow `0xffffff00` when the ordering
unit is selected (`+0x130 & 0x10`).

**Range circles (`4d6530`).** Up to three rings through `4d5c40`, none labelled:

- `mincloakdistance` (UnitDef `+0x22a`, signed word, `4c01e9`) in white, when it is
  not 0 and the unit is cloaked (`+0x114 & 4`).
- For a `kamikaze` type (`+0x260 & 0x10000000`, `4c0856`) with an `[EXPLODEAS]` weapon
  (`+0x1b6`, `4c16e3`), two red (`0xffff0000`) rings: a pulse of radius
  `(tick % 60) * half * 2 / 60`, raised to 8 and capped at `half` = areaofeffect / 2
  (weapon `+0x8a`, `530f48`; tick `g+0x19f44`), then `kamikazedistance` (`+0x236`,
  unsigned, `4c0876`) for a unit with a mover (`+8`), or `sightdistance` (`+0x226`)
  for one without. No shipped FBI sets `kamikaze` (the Kamikaze Rat does not), so only
  the white ring is ever seen with the shipped data.

**Ring (`4d5c40`).** `n = (int)(r * 2pi * 0.125)` line segments (D3D line list); point
`k` at angle `k * (0x10000 / n)`, offset `+sin*r` in x and `+cos*r` in z through the
512-entry table at `61a590` (`5360bf` / `5360f3`). Each point's height is the larger of
the unit's own (`+0x6e`) and the ground under it (`511170`), drawn at screen
`z - height/2`; a segment is drawn when either end is in the view (`540150`).

`client/orderoverlay.h` is the port; `range_circles_test` checks it, and
`tools/re/check_range_circles.py` runs `4d6530` (4000 random unit states) and `4d5c40`
(15 radii) under emulation against it. Not ported: the `+0x130 & 0x1000` skip and the
other two special units, whose meaning is not established; the `0x01`/`0x20` overlay
for non-selected units.
