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
pad. Every other ranged weapon keeps our structure pad (`8*max(foot)+24` px,
so short-range attackers do not grind a building's edge), except, since
0.9.12, one whose shot dies at its range (`Weapon::shotEndsAtRange`: a
Line-of-Sight bolt or lightning and a Guided shot are given one range of travel
at launch, a flame front is scanned out to the range). Retail's lightning
(`5f374c`), LOS (`5f39f4`), fire (`5f3780`) and Guided (`5f37e4`) vtables all
use the base `530580`, centre to centre, so these get no pad in `slotInRange`,
`combatReach` (approach, fire gate, findTarget's hold-post filter), the
per-slot fire gate, `repositionForShot`'s band, the structure/flyer hold and
Legion's mirrored reach ring. With the pad a Crusades Death Totem (lightning,
range 550) took a Taros lodestone 584.5 px off (emulated `530580`: out at 550,
first in at 585) and its bolts died ~540 px out without a hit (the user's
0.9.12 replay at 21:00); a mobile Crossbowman, Shaman or Fire Demon ordered
onto a building parked up to the pad past its range and never hit it. 38-40
unit types carry such a weapon (`shot_range_test` lists them). Before, the pad let
a Creon Submersible 822-848 px from an 800-range Divine Lodestone count as in
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
  10..250 in shipped data — is dead weight.)
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
  squared, `fireatwillrandom`, turn penalty, x10 multipliers, bad-target
  fallback. Auto slots fire en route without braking or steering; only a
  stopped, order-less ground unit pivots (at `turninplacerate`). Only
  hover-attack flyers and ground units with `turninplacerate > 0` must face
  the target to fire.
- **Vision** for the sweep is a deterministic per-player view-count plane
  rebuilt on a 7-tick stamp (hashed; the local display fog reads the same
  plane, see "Fog of war display").
- **Hit response** (`409a50`): (a) an idle unit runs a leashed retaliation
  attack and walks back; (b) otherwise the attacker goes to every auto slot that
  can reach it unless the current target is still good. AI owners also switch
  weapons (`51a8b0`) and sweep `commandfire` weapons; human owners do not.

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
unit it was fired at, `FlameShot::targetId`, hashed). Melee, beam and other
instant hits land on the target itself, and Remote Effect spells and
wandering storms are not aimed shots, so none of them can stop on scenery:

- An impact on terrain, water or scenery the weapon cannot destroy
  (`indestructible`, no `damage`, `unitsonly`; a fire starter still counts a
  flamable feature as destructible) that lies short of the target and outside
  its body plus one body plus half the weapon's splash counts against the
  target. A hit on the target, or a miss beside or past it, clears the count;
  so does moving more than 16 px from where the count started. Lobbers are
  included: an arc that clears never counts.
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
(retail flies it on attack runs instead; World does too since protocol 263, see
"Attack runs" below, except against a flying target). That hold skipped the flight
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
  inside it, as each dying unit retires, before later units have moved. A
  Remote Effect lands on its target's position at fire time; retail uses the
  slot's aim point (+8), lead included.

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
brakes and turns to the request of its last aiming slot. `tickHoverAttack` rings
the selected weapon's led SweetSpot (`retailLeadPoint`) and heads at the
target's SweetSpot. Of the 202 unit types, 60 post requests and carry a weapon
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
- Retail's hover-attack target controller holds its ring point fixed between
  wakes: flag 0x80 makes `4e41d0` return the stored point. World moves the
  point with the target. It also accepts at radius 4 where World's persistent
  controller never accepts.
- A non-hover flyer held in range of a flying target turns to its centre.
  Retail posts no request for it and would be flying AirToAir (`41ca50`, not
  ported). Against a target that cannot fly it flies attack runs (protocol 263).
- World releases a Dropped bomb without `52fff0` (no SET 22 acknowledgement and
  no facing test). `52ae90` runs aim start and the gate for every class, and
  `DroppedBallisticWeapon`'s angle virtual (+0x28, `4d4450`) is a bare `ret
  0xc`, so its angle test always passes, but a Tarbeak's Egg Bomb would still
  wait on its facing request (static reading).

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
- **Stage 1** `519b10(3)`: every slot automatic.
- **Stage 2** `519b70`/`51a700`: the selected slot (`519ac0`: a switcher's choice,
  else the first slot) manual, aimed at the target.
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
- AirToAir (`41ca50`) is not ported. A flyer attacking a flyer is still held in
  range, facing its centre.
- AirStrike (`41b5e0`) is not ported; no shipped weapon reaches it.
- When the target is lost, AirToGround pushes `VTOL_SEEKATTACK` (`4192f0`), which
  circles the target's last point at range + 160 px, scanning for targets.
  World drops the order instead.
- The group check (`51d1e0`, VTOL_Move_Formation) is not ported. Neither is
  stage 4's air-traffic step-out: unit +0x12a, a counter `51dc4b..` keeps
  from the overlap list, pushes `VTOL_StepOut` at 5 or more, and World keeps no
  such counter.
- Retail makes the other slots automatic and only the selected one manual.
  A multi-weapon non-switcher's other weapons (Dirigible, Juggernaut) pick
  their own targets. World fires them all at the order's target.
- Off the map, World flies at the clamped sector's cruise height (retail: the
  off-map cell's height 0 plus cruisealt), and its velocity kernel keeps the
  vertical control that `4dab71` skips for an off-map unit.

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
fire (otherwise a hit could swap it into a stage that outlives the fade);
the idle-reclaimer corpse clear in `tickAbilities` is unverified; the
fade is not drawn (the feature simply goes at its end). Not emulated: the turn
child and the COB time to INBUILDSTANCE (both add to the table above), what
else raises 0x400, and retail's per-wake lookup by cell (a new feature on the
same cell would be carried on with; ours follows the feature id).

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
when a next order is ready, and re-rolls on failure without limit. A mobile
builder's output gets nothing.

Port (protocol 295): `World::parkProductionOutput` for every output of a
structure, flyers included (flight branch below); mobile producers keep the
exit step.
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
mobile builder repeating production (retail gives its output nothing) holds
the same rally plan: while `repeatType` is set, Move, Patrol, Attack and
fight-move all set the rally and leave the builder producing in place
(`applyCommand` rejects its other orders); with finite or no production they
command the builder itself. Ending the repeat (Stop, the +++ toggle, or
subtracting the last copy) clears a mobile builder's rally, so a later limited
queue never inherits a plan the player can no longer see. Tests: `production_test` (factory and mobile
plans, attack-then-move, replace/Stop/hash) and `conjure_test` (the shipped
Zhon builders through the command path).

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
structure builder's output (bit 0x2000000) and gives a mobile producer's output
nothing, so it would stand where it was made. Ours keeps the exit step there,
because a mobile producer (a Zhon builder, Ctrl+click repeat production)
builds its next unit on the same spot. (The Legion column behind the first
ground output is gone: see "Legion: a spot of its own" above.)

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
  4. **Everything else last** (`0x4fcbab..0x4fcc27`): the units with
     `+0x130 & 3 != 1`, inferred to be the airborne ones.

So a stump, crop row, smudge or mana decal is never painted over a unit, a tree
paints over every ground unit of its own row and the rows above it, and a flyer paints over everything.

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
  * `kLayerAir` (1): airborne flyers, after everything -- retail's step 4.

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
  to 2x from high ground, less uphill. `0x4f59c0` (shared vision with a
  computer ally) re-points that ally's sight objects at the sharing player,
  so its units stamp into his plane. The Mapping plane is `game+0x19ef4`, one
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

The selector is display-only: order dispatch (`0x5214b0`) does not call it. Our
HUD had applied the range test to every unit, so a selected archer or swordsman
showed TooFar over any enemy out of reach. `tools/re/probe_cursor_enemy_hover.py`
runs those branches natively; `cursorCanAttackEnemy` (src/client/cursorrange.h)
ports them for the cursor only (`cursor_enemy` test, shipped types). The
right-click on an enemy still orders every selected unit to attack, as the
order code does not consult the selector; `World::attack` then rallies a
factory's output at the target, keeps an out-of-range tower's order until the
target comes in reach, and drops the order for a unit with no weapon.

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
normal). Our hover shows Repair over a site or damaged unit a selected builder
can work on (the left-click interface's order cursors), Select over another own
unit and Green over an ally's; a guard keeps Select, as retail's does.

Ours, by `src/client/friendlyorder.h` (`friendly_order` test): per unit,
repair when a mobile builder (not repeating production) and the target is
damaged, else Guard when `canguard`. Differences kept: a unit that can neither
repair nor guard does nothing (retail moves it to the point), a non-builder over
an unfinished unit does nothing (retail guards it), and an ally's unit is
offered the guard but `World::guard` accepts only its own side's units.

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
`computerOwned`. Ours is still an idle caster's automatic search, not an
order. Retail's revive is only ever an order (a click, or a caster's area
reclaim 0x406670 through 0x509cc0), so nothing takes an abandoned one up
again; our search took the same corpse up on the next tick and abandoned it
at every end while the economy stayed short. Since protocol 302 the search
starts no revive the end would refuse: none for a computer caster while its
allocation is under 0.7, none past the unit limits below. Ours also has no
walk, turn or INBUILDSTANCE wait (stages 0-2) and takes its first allocation
the tick after the corpse is found, where retail's stage 3 returns 1 and the
dispatcher runs stage 4 in the same wake; the end (stage 5) follows the last
subtraction in the same wake in both.

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
  cloak block (`0x4060bb`, table above).
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
  (event 0x10) is ported since protocol 302 (next section). Not ported: the landed step-out (+0x12a >= 5), the stage-2 attack scan
  (`0x51d1e0`, whose work our target acquisition does) and the optional leash.
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
the attacker dies, and the guard order under it resumes. The step-out and the
attack scan (`0x51d1e0`) stay unported; the latter is our target acquisition.

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
`attackAimTick`); the body faces from the tick after. World runs the COB pass
before the weapon update, so World's aim reaches the script at the next tick's
pass, the same pass in which retail's runs. `World::notifyTurnDirection`
reports the mover's turns to the unit's own VM before MoveRate, with the idle
navigator's zero request ahead of a pivot from `brakeGround`. The same grid
run on World (36 order delays x 4 distances, a scratch probe) gives the same
answer as retail for every phase the two share, and she fires in 58 of 144
(12 before: only when her controller looked in the very pass after the stop). `attack_stop_test` (Retail and `--legion`) checks the timing
for Kirenna and an Archer, that Kirenna ordered from 664 px fires (before:
none in a minute), and that her script hears a pivot through TurnDirection.
In the attack sweep (19,501 cases, both rosters), 29 ordered cases fire that
did not: 24 of Kirenna's (of her orders that walked more than 40 px, 9 of 52
fired, now 26) and five of a Wolf, a Mind Mage and a Berserker against a
circling flyer; one fire-at-will case lost its only shot (a Crusades Stone
Giant at a flyer circling at its full range, which it hit once, late).

Not ported: the approach. World stops at the 2D range, so it never makes
retail's second, deeper walk, and its slots are manual for the whole order, so
fire at will never aims during the walk. Both make retail fire in more of
these approaches than World does. The idle auto-slot pivot
(`tickSlotWeapons`) turns before the mover and `brakeGround`'s zero request
follows it, so that pivot is not reported.
