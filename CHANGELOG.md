# Changelog

Every published TAK Engine release, newest first.

The current version is always at
[the latest release](https://github.com/pocketgeek/tak-engine/releases/latest).
Releases from 0.9.0 on stay on the GitHub releases page; versions before 0.9.0
are kept only as git tags (each entry links its source): to run one, build it
from source with the [development guide](docs/development.md) of that tag. No release includes retail game assets: every version needs your own
*Total Annihilation: Kingdoms* installation.

**Protocol** decides who can play together. A server and all of its players
must use the same protocol, and a replay plays only in a version with the
protocol that recorded it. Each entry gives its protocol and replay format; an
entry says "plays with" only where that release was checked against its
neighbours. **Upgrading** lines appear only where you have to do something
yourself.

Releases 0.7.11 and later link full release notes, and 0.7.6 and later link
their validation reports; both live in [`docs/`](docs/).

## Unreleased
*protocol 312 · replay format 15 · does not play with 0.9.19 or earlier, nor their replays*

- **Clicks do what the original game's clicks do, and the cursor says so.** Orders now
  go out when you let go of the button, as in the original game, and every click
  picks the same unit the cursor shows: where units overlap, the smallest one under
  the pointer. Each selected unit decides for itself:
  - Right-clicking an enemy: units that can hurt it attack, units with no weapon walk
    there, and a unit that cannot hurt it stays put. Right-clicking one of your own or
    an ally's units helps build it, repairs it, boards it (when it is your transport
    and the unit fits), guards it, or else walks there. A right-click on a body or a
    tree raises or reclaims it, and the other units walk there.
  - A loaded transport now just moves where you right-click; **Alt+right-click**
    unloads it there (or use the Unload button).
  - Right-dragging a box attacks the visible enemies in it, nearest first; units with
    no attack order and the ability to clear it clear the box; a lone transport loads
    your units in it. The cursor shows what the box will do.
  - With Attack, Load or Clear armed, a left-drag box attacks, loads or clears too.
  - An armed button acts only where its cursor shows an order; elsewhere the click
    does nothing and the button stays armed. Guard no longer lets ground units guard
    a flyer (unless a flyer is selected too), Attack over your own units or the
    ground fires at that spot (see Fire at a point below), Move over a friendly unit helps,
    repairs, loads or guards it, and Clear shows Revive where it will raise a body.
    Fight-move (F) is the original game's move-fight: over a friendly unit it does
    what Move does there (helps, repairs, loads or guards it, with the same cursor),
    and everywhere else it moves to the point, fighting on the way.
  - On the minimap, a right-click attacks, helps or repairs the unit under the
    pointer, armed orders aim at it, and a building can be placed by clicking the
    minimap (with the found-site cursor where it would be accepted).
  - Left-clicking an enemy or ally clears your selection, as in the original game;
    with nothing of yours selected it still selects the unit so you can look at it.
    Shift-clicking a selected unit now removes it from the selection.
  - A random unit of the selection acknowledges an order, as in the original game.
  - **Ctrl+click puts an order at the front** of a unit's queue without clearing it,
    as in the original game: the unit does it first, then carries on with what it
    was doing. A builder you Ctrl-click away from a building it has started goes back
    to finish it, and puts the site down again if it fell apart while the builder was
    away. Shift- or Ctrl-clicking the same order again (on the same target, or
    within 16 pixels of a queued point) takes that queued order back. For a factory,
    or a Zhon builder repeating production, both work on its rally orders.
- **A unit no longer ignores an enemy for good after its shots hit a rock.** A unit
  that wrote off an enemy because three shots in a row struck a hill or rock used to
  keep skipping it, even after that enemy walked into the open, until you gave it an
  attack or move order. Now the write-off lapses as soon as either unit moves off its
  spot, and the unit opens fire as the original game would. A target that keeps
  moving behind cover no longer makes your unit walk to a clear spot; it fires on, as
  in the original game.
- **Idle ships make way at a Navy Yard's door.** Your own idle ships parked in front of
  a Navy Yard now move aside while a new ship is leaving, and an idle ship on the
  yard's pad moves off it, so a crowd of Iron Clads no longer stops the yard (it used
  to finish two ships of six, or none with a ship on the pad). The original game moves
  no idle ship. A ship leaving a door that faces a shore still sails out slowly: that
  is the original game's own behaviour.
- **Idle units make way at a land factory's door too.** Your own idle units standing in
  front of a land factory (a Barracks, a Keep, a Dungeon, an Academy and the rest) now
  step aside while a new unit is leaving, and one on the factory's pad steps off it, so
  they no longer stop the factory: one idle unit on the pad used to stop it before its
  first unit, and a crowd at a Creon Academy's door let it finish one of six. Your
  guards step aside for the factory's new units too, at land factories and at a Navy
  Yard, keep their Guard order and go back to guarding afterwards, once for each new
  unit (a dozen guards at a Legion Academy's door used to let none of six out); a guard
  that is fighting stays where it is. Units with other orders of their own and your
  allies' units stay put, as in the original game, which moves no idle unit. When a
  factory cannot start its next unit, units standing where that unit would appear,
  just outside the factory's pad, step aside as well. A factory's own new units, parked
  round it, now move again only when they stand on its pad or hold up the next one out;
  at a Navy Yard too, and with Legion pathfinding as well (there, at a Navy Yard whose
  door faces a shore with guards at it, the yard's own parked Iron Clads used to hold
  the sixth one in the yard). With Legion pathfinding, a factory's own new unit told
  to step aside that is boxed in where it stood gives up the step and stays there.
  A factory whose rally point is on its own pad no longer stalls: its new unit steps
  off the pad. A unit guarding or building beside its own factory moves nobody.
- **Reclaimed trees and rocks fade out.** As in the original game, a reclaimed tree or
  rock turns into a white silhouette of itself that fades away over about a second,
  instead of vanishing at once.
- **Trees and scenery destroyed by damage play their death first.** A tree, hut,
  wall or other scenery knocked down by weapons now plays its falling or collapsing
  animation (two-thirds of a second for a tree) before its stump or rubble appears,
  as in the original game, instead of changing at once. While it falls it still
  blocks its ground, takes no more damage, cannot catch fire and cannot be reclaimed.
  A burning tree takes no weapon damage either, as in the original game; it burns
  down instead of being knocked into its stump. Rocks, which have no death animation,
  still go at once.
- **Flyers on Hold Position hold their post.** An idle flyer on Hold Position used to
  take off after an enemy that came into range and fly attack runs at it, hundreds of
  pixels from where you left it. Now it stays where it is and shoots what it can reach
  from there, as in the original game. The Thirsha, the Ghost Ship, the Dirigible and
  the Dark and Darkest Priests start on Hold Position, so idle ones now stay put: set
  them to Maneuver to have them go after enemies.
- **Units stand where the ground is painted.** Every unit, tree, effect and the fog
  used to be drawn too far down the screen by half the height of the map's most common
  ground: 110 pixels on a map whose ground is high, such as INDUSTRIAL Zone, enough to
  draw units on a road beside lava in the lava itself. Now everything is drawn at its
  own height, as in the original game, on every map. Ships and wading units ride at
  the water surface over deep water, with their health bars and markers on them, and
  an order clicked on water sends a ship to where you clicked. The camera centres on
  where a unit is drawn.
- **The result screen's buttons click.** The two buttons on the victory and defeat
  screen now play their clicks when pressed, as in the original game (the left one
  its cancel click, the right one its OK click), like the title menu's buttons. The
  SETTINGS, LOAD REPLAY, BENCHMARK and server pages click too, with the clicks the
  original game gives the same kinds of button. Menu clicks now also follow the
  Options MASTER and SOUND EFFECTS volumes; at the default full volumes they sound as
  before.
- **Units pick their own targets the way the original game does.** Every target a
  unit chooses for itself now comes from the original game's own search:
  - A unit only goes after an enemy that your side can see, with its own sight or
    another of your units'. Catapults, trebuchets and other long-range units need a
    unit near the enemy to spot for them; they no longer fire into the fog.
  - Units on standby, patrol or an attack-move take only enemies already in weapon
    range. They no longer walk up to an enemy just outside it. Units that fight hand
    to hand still go after any enemy they see.
  - Among the enemies in reach, the choice is the original game's weighted draw:
    nearer, weaker and easier targets are likelier, instead of always the nearest.
  - Towers and other buildings fire only at enemies your side can see in their range,
    and let go of a target that walks out of range or behind a wall. A tower still
    answers an enemy that hits it.
  - Guards no longer hunt on their own. They defend the unit they guard when it is
    hit, and fire at enemies that pass within range.
  - Units on Hold Position break off a patrol for an enemy and come back, as in the
    original game; idle units on Hold Position never chase.
  - A unit hit by an enemy it cannot see still fights back, as in the original
    game. A moving unit answers the last unit that hit it, even a cloaked one.
  - Wandering monsters attack enemies they see while they roam.
- **Attack-move stops, fights and comes back.** A unit on an attack-move stops for an
  enemy it sees in range, fights it, returns to where it stopped and carries on to its
  destination. This is the original game's own move-fight, which it had no key for;
  here Fight-move (F) gives it too.
  Kirenna on an attack-move now fires at the enemy in front of her (a known limit since
  0.9.3), and so does any unit whose longest weapon outranges the one it has selected.
  With Legion pathfinding she keeps firing. With Retail pathfinding she still often
  stops after one shot and waits: her own animation script holds her fire when she
  stops in the middle of a step, and the original game has the same race.
- **Legion: a unit whose animation script never lets it fire tries again.** A unit
  standing still and ready to fire whose weapon has waited 3 seconds for its script
  is told its target was cleared, once, and aims again. Kirenna is the unit this
  catches.
- **Allies share sight.** Every player on your team, human or computer, sees for
  you: your units now attack an enemy that only an ally's units can see, and computer
  allies act on what you and the rest of the team see. The original game shared sight
  only between human teammates; here computer allies share too. Villagers, wildlife
  and other passive players in a campaign mission share nothing, as in the original
  game, even though missions put them on your team; outside campaigns a passive
  player never shares sight either.
  A computer ally's dying units now keep their sight for a moment, like your own.
- **Fire at a point.** The game now has the original game's fire at
  a point (SUPPRESS), as the new Attack Ground command: a ground unit or tower fires
  at the spot itself, a walker steps into range (or out of its minimum range) first
  and fires at will on the way, a tower out of range keeps the order and waits, and
  Shift-queued points take turns one shot each. A melee unit fells a tree or other
  clearable obstacle there; anti-air weapons get no order. A flyer flies attack runs
  over the spot, hovers off it if it has hover attack, or bombs it with a dropped
  weapon. An armed Attack (A) clicked on the ground or on a friendly unit, in the
  view or on the minimap, now gives it to each selected unit that can, under an
  Attack cursor; Shift queues it, Ctrl puts it first, and Shift or Ctrl on a
  queued point takes it back. As in the original game, a unit in a formation that
  finds itself left behind walks back to its formation first, still firing at the spot
  when its weapon can aim without turning the body, and then fires on from there; now
  and then it gives up the order instead. Flyers attacking in a formation, at a spot
  or at a unit, fly back to their formation the same way, and keep firing at what they
  had aimed at while they do; they now turn back at once, not a moment later.
  Patrolling flyers in a formation now count as moving with it on the way out as
  well as on the way back, as in the original game.
- **In Retail pathfinding a Ctrl group no longer moves at its slowest unit's pace.**
  As in the original game, a group (Ctrl+number) is only a saved selection; only a
  formation (Alt+number) keeps together, waits for its stragglers and sends them back
  to it. Before, both did. The Retail computer player's attack waves and flyer squads
  now keep together the same way, as the original game's do (without slowing to
  their slowest unit, which the original never does for a computer player).
- **A factory's new unit joins the factory's group or formation when it leaves the
  factory**, as in the original game, not when it is started, in Retail and Legion
  pathfinding alike. A unit a builder conjures joins the builder's group when it is
  finished (the original game leaves it out of any group; this one is TAK Engine's
  own).
- **In Retail pathfinding, flyers keep to their formation whatever they are doing**,
  as in the original game. A flyer left behind on patrol, in an air fight, while
  hunting for a new target, or while guarding flies back to its formation and then
  carries on; one that keeps falling behind may give up guarding. An idle member,
  on the ground or in the air, that ends up away from its formation goes back to it
  by itself, and a flyer that lands, or waits, does so with its formation. A flyer
  that has caught up while the formation is still moving circles near it, attacking
  what it finds, until the formation stops. Ground units in a formation do the same
  while guarding: one left behind walks back to its formation first and then guards
  on, and one that keeps falling behind may give up guarding. A formation's guards
  and attackers, its troops walking to board a transport and its transports on their
  way to pick them up or set them down now count as moving with it, and its guards
  keep to its pace.
- **In Legion pathfinding, an idle unit in a formation that ends up out of its place
  walks back to it by itself.** This is TAK Engine's own Legion behaviour, matching
  how a formation re-forms in Retail pathfinding and the original game: when a unit
  standing in a formation (Alt+number) has been pushed, stepped aside, has come back
  from a fight, or was sent somewhere on its own, and ends up farther from its place
  than the original game allows, it walks back to its own spot in the formation. The
  computer player's attack squads re-form the same way, and gather their stragglers as
  your formations do. If another unit or a building stands on that spot it waits until
  the spot is free, and if it cannot get there it stays where it stopped instead of
  trying again and again. A formation you stop on
  the way stays where it stopped. Flyers keep to their formation as before, and units
  in a group (Ctrl+number) are left where they are.
- **An armed factory from a mod gets no Attack Ground order**, as in the original
  game, which takes the attack command away from any factory. It never attacks an
  enemy unit itself either, ordered or on its own (an Attack on a unit still sends
  its new units there), though its weapons still fire at what comes in range.
- **Holding Shift shows a cloaked unit's decloak range.** While Shift is held, each
  of your selected units, and your unit under the pointer, that is cloaked now shows a
  white circle at its decloak distance, as in the original game. A `kamikaze` unit
  from a mod shows the original's two red circles: a pulse out to half its blast and
  a ring at its trigger distance.
- **Order display matches the original game.** The red corner brackets on a unit you
  ordered your units to attack are gone (the original never marked it; hold Shift and
  the target shows the Attack pointer). Area reclaim and area mana-building boxes are
  drawn in the original's yellow instead of gold and cyan.
- **Veterans rank up as in the original game.** A unit used to gain one veteran level
  per kill. Now, as in the original game, each kill earns it the experience the victim
  is worth, and it reaches a level each time that adds up to its own worth again, up
  to level 10: killing a unit worth more than itself counts for more than one level,
  and killing a cheaper one for less. Towers and other buildings now become veterans
  too. Killing an ally's unit counts as well; killing one of your own, a unit not yet
  finished, or a unit that self-destructs or drowns in deadly water earns nothing. A
  unit given to an ally, converted by an enemy or captured by a mission starts again
  without experience.
- **Kills are credited as in the original game.** A shot still in the air when its
  shooter was destroyed, or one fired before the shooter changed sides, no longer
  takes the kill from whoever hit the target before, and lands without the dead
  shooter's veteran bonus. The Kills count and score now go to the player whose unit
  landed the killing blow for the same kills that earn a unit experience, so killing
  an ally's unit now counts there too.
- **Units that explode when they die do it when their death ends.** A Bomb
  Sprinkler, Kamikaze Rat, Grenadier, Fire Wagon, Shock Trooper or Dirigible now
  bursts once its death animation is over, as in the original game (the Bomb
  Sprinkler about five seconds after it falls), not the moment it is killed. The burst
  belongs to nobody: its kills earn no player a kill or score and no unit experience,
  and it is not stronger for a veteran. It bursts before its own body is left behind,
  so it never damages that body. On a map made with the sea-level trigger switched
  off (none of the original maps), a burst, or any shot that hits nothing, landing in
  the water does nothing at all, as in the original game.
- **Bodies at the water's edge sink or stay as in the original game.** Whether a
  fallen unit's body sinks now depends on the height of the ground right under it,
  as in the original game, so bodies lying level with the water sink, and bodies on
  dry ground beside a deep spot stay to be raised or reclaimed.
- **Wrecks in the water settle on the bottom and stay**, as in the original game: a
  sunken ship or a body in the shallows sinks slowly to the sea bed and stays there
  to be raised or reclaimed, and only a wreck sinking towards the deepest sea floor
  fades away on the way down. Before, every body in the water vanished after two
  and a half seconds. Bodies on land that rot away last about two seconds longer, as
  in the original game.
- **How badly a unit is wrecked when it dies follows the original game.** The death
  animation and body a unit leaves depend on how hard the last blow hit it and on
  its health a second earlier. That earlier health is now taken in whole numbers and
  just before the death, as in the original game, so a unit dying on the second
  reads its health of one second before rather than two.
- **Campaign objectives ignore units still carrying out their mission orders**, as
  in the original game: a unit the mission is walking somewhere does not count for
  "move a unit to this spot" until its orders are done, and does not count as a
  survivor for "all your units are killed".
- **A mission hears of a death when the death animation ends**, as in the original
  game: objectives such as "kill the enemy Monarch" or "kill so many of a unit" are
  met, and the mission's own reaction to a death starts, once the fallen unit has
  finished dying, not the moment it was struck down.
- **Deadly water hurts once a second**, as in the original game: the map's whole
  water damage each second to anything standing at or below the water line that
  cannot hover, ships included, and troops carried on a ship too (a flyer's
  passengers high above the water are safe). A unit the water kills dies that
  moment, not a moment later.
- **A player who loses their Monarch loses their whole army with it.** With Monarch
  Expendable off, the moment a Monarch dies (or a transport carrying it is destroyed)
  every other unit and building of that player dies too, as in the original game;
  before, the player was out but their army stayed on the field. Scenario games keep
  their own rules.
- **Troops aboard a destroyed transport count as its killer's kills.** When a
  transport is destroyed, everyone aboard dies with it at once, as before, and now,
  as in the original game, each of them counts for whoever destroyed the transport:
  Kills and score for that player and veteran experience for that unit. A loaded
  transport that changes sides (charmed or captured) loses its passengers the same
  way, as in the original game; they used to stay aboard.
- **Self-destruct keeps the original game's timing.** After its two-second countdown
  a unit now leaves your command up to half a second later, at random, as in the
  original game. Pressing self-destruct while some of the selected units already
  count down now cancels theirs and starts none, as in the original game; it used
  to start the others too.
- **Gates are built closed and switched off**, as in the original game. A new gate
  used to count as switched on with its doors shut, so the first click on its ON
  button did nothing.
- **New units start with an empty mana pool and fill up, as in the original game.**
  A mage, priest, dragon or Monarch that is trained, built, conjured, raised or
  placed at the start of a game used to arrive with a full pool; it now starts
  empty and regains its recharge rate each second (only once it is finished), so
  its costly spells, and a spy's cloak, wait until it has the mana. Mission units
  start with the mana the mission gives them (usually none), and a mission's
  health setting gives whole hit points as in the original.
- **Every game opens with the mana its starting units can store, as in the original
  game.** A skirmish or multiplayer game used to open with 2800 mana a player; it now
  opens with 5000, the Monarch's whole store. A campaign mission opens each player
  with the store of the units the mission gives them (it used to be 500), and a
  unit a mission or a map scenario creates during play brings its store with it
  full. Units you train or build still arrive with nothing.
- **A finished wall becomes part of the scenery, as in the original game.** A wall
  you build, and every wall a campaign mission places, now turns into a wall feature
  as soon as it is finished: it still blocks the way and can be smashed, but it is
  no longer a unit you can select, that counts against your units, or that enemies
  pick as a target. Building one no longer counts as a loss.
- **Priests, the Lihr, the Zhon Shaman and the Aramon Smith have their auras again.**
  Earlier changes in this release left every healing and armour or attack aura
  switched off. They work as in the original game: a paralysed priest still
  heals, and so does a Sacred Fire you switch off.
- **A selected unit of yours shows how many enemies it has killed**, beside its health
  and mana bars, as in the original game (once it has a kill). A unit that changes
  sides starts again from none.
- **In a multiplayer game a selected Monarch shows its player's name**, as in the
  original game, instead of the Monarch's own name.
- **The rank mark on a unit's picture follows the original game**: none until rank 4,
  then one mark for ranks 4 to 6, the next for 7 to 9 and the last at rank 10. It
  used to show from the first rank and reach the last at rank 7.
- **A Pillar of Light and a Sacred Fire light up as in the original game.** Both
  switch themselves on when they are finished, whether you build one or a mission
  or map scenario places it, and the original game runs their flames twice over
  from then on. They used to count as switched on while still being built and their
  extra flames never started.
- **A modded unit with `init_cloaked` starts with its cloak switched on**, as the
  original game does (no unit of the original game uses it).
- **A modded unit's `damagemodifier` works as in the original game:** while the unit
  is switched on, every hit it takes (deadly water too, heals not) is scaled by it.
  No unit of the original game sets it.
- **A modded unit's `mogriumbounty` is paid as in the original game:** the player
  credited with killing such a unit gains that much mana. No unit of the original
  game sets it.
- **A unit that changes sides starts afresh, as in the original game.** A unit
  converted by an enemy, given to an ally or taken over by a mission or a map
  scenario keeps its health and the way it faces, but is a new unit to its new
  owner: its mana pool starts empty, it is no longer paralysed, cloaked by order or
  set to its old owner's stance (it takes its type's), its weapons start with no
  reload, and it loses a mission-given name. Its old
  owner loses the unit: missions and map scenarios count it as a loss for the old
  owner (a mission's "kill N of these" goal counts a converted enemy, and the last
  player to hit it is credited in a scenario's tallies), but no one is credited a
  kill. A unit no longer changes sides to a player who already has the one copy of
  it a player may have (such as a dragon).
- **Sound and music at full quality.** In-game music used to be turned into 11 kHz
  mono. The game now mixes at 44.1 kHz: the original's music plays in
  stereo at its own rate, a mod's 44.1 kHz or surround sounds and music keep their
  quality, and the original's 11 kHz unit sounds keep their pitch and length with a
  cleaner top end. On 6.1 speakers the surrounds of 5.1 music now play at full level.
  Video streams carry the same 44.1 kHz sound.
- **Homing shots fly as in the original game.**
  - A homing shot in the air keeps after its target when the target boards a
    transport, and flies at the transport carrying it; it used to fly straight on.
  - A fast homing shot (such as a Tracking Arrow) almost dead ahead of its target
    could double or triple its speed a few moments early or late; it now speeds up
    exactly when the original game's does.
  - A homing shot with a model always faces the way it flies. It used to turn only
    when the shot turned, so a mod's spinning homing shot kept spinning once its
    target died.
- **A unit short of mana after a shot falls back to a cheaper weapon.** As in the
  original game, a unit that switches weapons (every Monarch and dragon) and has too
  little mana left for another shot of its weapon now switches at once to the best
  cheaper one it can afford, or to its first, whether it is yours or the computer's.
  Your units used to wait on the dear weapon until their mana came back, and the
  computer's switched only later. A shot the unit cannot quite pay for still leaves
  without taking mana, so mana no longer drops below zero, and veterans pay less per
  shot, as in the original game. A flyer attacking a unit now restarts its attack on
  the new weapon at once, and a unit on foot aims the new weapon at once instead of
  a moment later.
- **Campaign missions play out as in the original game.**
  - The units a mission starts with stand on the same squares as in the original
    and face the same way: they stood half a square off, and most of them faced the
    other way. Units a mission brings in mid-mission appear where the original put
    them; large ones could be several squares off.
  - A mission's scripted moves, fight-moves, patrols and unloads go to the exact
    spot it gives, the corner of the named map square, not its middle, and a point
    with a fraction keeps it.
  - Siege units and towers scripted to bombard a spot fire at it, instead of only
    walking toward it (or, for a tower, doing nothing). A flyer scripted the same
    way makes its air attack on the spot instead of flying to it, and a computer
    player sees it as attacking.
  - Trigger areas cover the same squares as in the original and fire once for each
    unit: a unit that walked out of one and back set it off again. Troops carried
    in a transport set them off too, not only the transport.
  - Missions hear about every unit that enters play, and who owns it, so mission
    20's scripted event can happen.
  - A captured or given unit takes its type's mission bonuses, rather than keeping
    the armour and weapon bonuses its old owner's mission placed it with.
  - A unit that changes sides is announced to the mission again for its new owner,
    and sets off the trigger areas it stands in again, as in the original game; the
    mission also hears that the old owner's unit is gone.
  - The mission Kill command kills every unit on the map, whatever unit it names.
    No original mission uses it.
  - A unit the mission is giving orders to cannot be selected until those orders
    are done, as in the original game; a unit sent on a patrol by the mission stays
    under its control. While its orders run, a moving unit does not turn on whoever
    shoots at it, and a computer opponent leaves the unit to the mission instead of
    drafting it into its own army, whose orders replaced the mission's. Troops a
    mission's transport puts ashore set off on their own orders at once, instead of
    first finishing them and then shuffling aside where they ended up.
- **Fedora 45 packages.** Fedora 45 now has its own x86-64 and ARM64 RPMs, beside
  Fedora 44's.
- **Investigated, no change needed:**
  - A computer's Sea Dragon on Hold Position with an enemy just outside Tsunami's range
    keeps switching weapons and never fires: the original game does exactly the same.
  - A Prismatic Mirror holding a target without firing (0.9.3) was a Mirror still being
    built; this was fixed in 0.9.12, and the original game does the same as now.
  - An armed Attack clicked on the ground with a factory, or a Zhon builder repeating
    production, selected sends its new units there fighting on the way, and the
    builder keeps producing (the original game gives a factory nothing there). A
    click on the factory itself still gives it nothing, as in the original game.

## 0.9.19
*2026-11-03 · protocol 311 · replay format 15 · does not play with 0.9.18 or earlier, nor their replays*

- **Legion: allied units meeting head on swap places.** Two of your (or your allies')
  units on move orders that block each other where neither can get round now trade
  places and walk on, instead of both standing. Two allied armies of 500 crossing a
  river in opposite directions over three bridges used to gridlock at most starts (a
  median of 741 of 1,000 arrived and 182 stood stuck); now a median of 983 arrive and
  none are stuck. On screen the two units pass side by side over a fraction of a second
  instead of jumping through each other.
- **Legion: blocked units press on, as in Retail.** A unit queued behind its own army
  on a move order closes up on the one ahead and slides along the file instead of
  stopping dead. Units in a formation flow round a rock, a wall end or a block of idle
  units at their travel speed, facing the way they go, instead of creeping sideways at
  a crawl, and no longer step backwards behind where they are heading. Units on attack,
  fight, guard or patrol orders, builders and newly built units leaving their factory
  behave as before, so attackers still stand and fire from their places round a target.
- **Passive AI stays at home.** It now strikes only at enemies within 800 pixels of one
  of its buildings, so it defends its base and never sends waves after something it saw
  elsewhere, such as your lodestone in the middle of the map.
- **Legion is quicker with 1,000 units:** three exact optimisations of its route search
  (the same moves in a shorter tick).
- **Protocol 311:** Legion games play differently, so 0.9.19 plays with neither 0.9.18
  nor any earlier version, nor their replays. Retail pathfinding plays exactly as in
  0.9.18. Replay format 15 and the random map generator (version 9) are unchanged.
- **Technical:** protocol 310 (Legion: an unturned step keeps travel speed, except a
  plain mover's flow-around shuffle and the in-reach press; a formation member's
  flow-around shuffle never steps behind its aim; a body blocked by bodies on a move leg
  is clamped per axis and presses on) and protocol 311 (allied head-on swap in one exact
  step, a 45-tick cool-down, the swap ticks hashed). The swap's 8-tick pass is drawn by
  the client only and never hashed. A per-member mechanism tag for the scenario observer
  (observation only); proving tests `legion_swap`, `swapshow` and one per steering
  change; Legion goldens retaken for 310 and 311; the Slackware CI job tries three
  package mirrors in turn.

**Upgrading (server operators):** update `takserver` together with your
players.

Full notes: [release-0.9.19-notes.md](docs/release-0.9.19-notes.md) ·
validation: [release-0.9.19-validation.md](docs/release-0.9.19-validation.md)

## 0.9.18
*2026-11-02 · protocol 309 · replay format 15 · plays with 0.9.17 and 0.9.16, and their replays*

- **A clean build.** Every compiler warning on every platform is gone: about 390
  distinct warnings at 0.9.17, none on Linux, Windows or macOS now. None of them was
  a bug in the game. Most were style warnings, now silenced, and the rest are fixed
  in the code without changing behaviour; the deprecated C++ path call is replaced
  everywhere.
- **Windows: a single-player game's server now closes with the game.** If the game
  was killed or crashed, its private server kept running in the background, holding
  its network port and files (it could block an update). It also writes its log to
  the same place as the game now.
- **Password hashing refuses an absurdly long salt** instead of risking an overflow
  that real salts can never reach.
- **Protocol 309, unchanged:** nothing that games or replays depend on changed, so
  0.9.18, 0.9.17 and 0.9.16 play together and play each other's replays. Servers
  need no update.

Full notes: [release-0.9.18-notes.md](docs/release-0.9.18-notes.md) ·
validation: [release-0.9.18-validation.md](docs/release-0.9.18-validation.md)

## 0.9.17
*2026-11-02 · protocol 309 · replay format 15 · plays with 0.9.16, and its replays*

- **Options: more dropdowns.** Texture Filtering, Hidden Units, Health Bars, Build
  Menu and Chat Position are dropdowns like Renderer, with the same choices as before.
- **UI Font is now Modern UI Font, a switch:** on is the modern face (the default),
  off the classic one. Your current choice carries over.
- **No Voice Chat row in Options.** Open the Voice Chat page from Settings on the main
  menu, or from the game menu.
- **One footer everywhere:** on Options, Voice Chat and Controls, Defaults sits at
  the left edge and Back at the right.
- **YouTube Streaming has a Back button,** and its window is laid out like Options:
  same size and text, and it grows with the window. Its messages show beside Back,
  and the Esc / F9 hint is gone.
- **Settings, Benchmark and Load Replay grow with the window** like Options, from 1x
  at 1280 × 720 to 3x at 4K and up, each with an Options-sized Back; the "ESC - BACK"
  hints are gone.
- **Protocol 309, unchanged:** all of this is interface only, so 0.9.17 and 0.9.16
  clients and servers play together in either direction, and each plays the other's
  replays. Servers need no update.

Full notes: [release-0.9.17-notes.md](docs/release-0.9.17-notes.md) ·
validation: [release-0.9.17-validation.md](docs/release-0.9.17-validation.md)

## 0.9.16
*2026-11-01 · protocol 309 · replay format 15 · does not play with 0.9.15 or earlier, nor their replays*

- **Replays have proper controls.** A clean bar across the top with a clickable
  timeline, rewind, play/pause, fast forward and speeds from 0.25x to 256x (the label
  shows the real speed when a machine cannot keep up). Space pauses, **+** and **−**
  change speed, **[** and **]** jump 30 seconds, Shift with them 5 minutes. A jump lands
  on exactly the recorded game.
- **Watching stays watching.** In replays and when spectating you can select any unit,
  with no sound or acknowledgement, and give it no orders. The right side shows that
  unit's player, with mana, buttons and build queues. T tracks, and Ctrl and Alt number
  groups are your own.
- **An infinite-queue builder's units start their rally orders straight away,** without
  walking to an exit spot first. When it has no room for the next unit, it moves one
  idle unit out of the way, so parked units near it can be nudged.
- **An attack rally step a unit cannot do is skipped,** instead of leaving it on the pad.
- **Surrender** in the game menu (click twice) ends your part in a game and shows the
  defeat screen. It replaces Main Menu and Quit while you play.
- **Result screens:** the left door says Title Screen. The right door says New Game in a
  single-player game, Game Select in multiplayer, and Next Mission or Retry in a campaign.
- **Game Select signs you back in without your password,** using a temporary token held
  in memory for up to 12 hours and revoked when you quit. Encrypted servers only; on a
  plaintext server you type your password.
- **The Benchmark screen can run Retail or Legion,** remembers your choice, and shows the
  mode on its results. The results panel now fits small windows.
- **The streaming panel's key can be rebound** in Controls; it was fixed on F9.
- **START says STARTING...** as soon as you click it.

Full notes: [release-0.9.16-notes.md](docs/release-0.9.16-notes.md) ·
validation: [release-0.9.16-validation.md](docs/release-0.9.16-validation.md)

## 0.9.15
*2026-11-01 · protocol 308 · replay format 15 · plays with 0.9.14, and its replays*

- **Selection rings stay rings at every zoom.** Zoomed far out, a unit with a
  small ring got a small filled box instead. That was our own shortcut, not the
  original's, and 0.9.14's plate-sized rings made it common. Every selected unit
  now draws its two dashed rings.
- **Arrows, bolts, harpoons and spears draw at their own size.** They were
  enlarged at most zooms, up to 1.5 times longer and 3.4 times wider, so a
  Hunter's spear looked too big zoomed out. That was our own addition too. They
  are now the size of their models, as in the original, and zoomed out they can
  be thin streaks.
- **Protocol 308, unchanged:** both changes are display only, so 0.9.15 and 0.9.14
  clients and servers play together in either direction, and each plays the
  other's replays. Servers need no update.

Full notes: [release-0.9.15-notes.md](docs/release-0.9.15-notes.md) ·
validation: [release-0.9.15-validation.md](docs/release-0.9.15-validation.md)

## 0.9.14
*2026-11-01 · protocol 308 · replay format 15 · does not play with 0.9.13 or earlier, nor their replays*

- **New AI level: Passive.** It plays exactly like Easy, with the same build
  pace and the same small waves on the same timing, except that nothing of its
  goes looking for you. It sends no scouts, and its army and fleet never explore
  or head for a start position, so it attacks only what it has seen. Its
  builders never leave home: it takes mana deposits and builds only within 900
  pixels of its base. Leave it alone and it sits at home building.
- **The lobby's AI levels are in a new order:** Passive, Easy, Normal, Hard,
  Absurd, Defensive, Absurdly Defensive, Retail. Cartographer's preview level
  follows it. A new AI seat still starts on Normal.
- **The selection ring lies at a unit's feet, as in the original.** It sits on
  the unit's ground plate, at the plate's size, and turns with the unit. It used
  to float at the model's mid height, so a tall unit such as the Death Totem wore
  it round its crown.
- **The system arrow no longer shows through loading screens** with Hardware
  Cursor on. Each screen put the default cursor back as it closed, and it stayed
  up until the next screen's first frame.

Full notes: [release-0.9.14-notes.md](docs/release-0.9.14-notes.md) ·
validation: [release-0.9.14-validation.md](docs/release-0.9.14-validation.md)

## 0.9.13
*2026-10-31 · protocol 307 · replay format 15 · does not play with 0.9.12 or earlier, nor their replays*

- **Melee units cannot hit flyers in the air**, as in the original game. A
  Titan, Swordsman, Troll or any other unit fighting with a melee weapon now
  drops an order to attack a flyer that is in the air (or a unit that boards a
  transport), does not go after one on its own and does not answer one that
  attacks it. Titans used to walk under a slow or hovering flyer, such as a
  Gryphon, and hammer it. A flyer that has landed is fair game, as in the
  original: melee units attack it, ordered or not, also when they pass one on
  a move, and stop when it takes off. A melee blow's splash still never
  reaches a flyer at its flying height.
- **"Not against air" weapons judge whether the target is flying right now**,
  as in the original game. The Knight, the Grenadier and the wolves now attack
  a flyer that has landed; before, any unit of a flying type was off limits to
  them. A ranged attack on a unit that boards a transport now ends, as a melee
  one does. The Retail AI sees landed flyers as ground units too.
- **Melee units reach the building they attack**, as in the original game:
  each attacker walks to its own free place touching the building, corners
  included, and bites from there. They used to head for the building's
  centre: with Legion pathfinding a second Spirit Wolf on a Lodestone waited
  behind the first for good, and wolves sent at a Keep, a Barracks or any
  other long building (every factory and gate) stood out of reach on its short
  sides and never bit. Swordsmen, Knights, Trolls, the gods and every other
  melee unit get the same, in both pathfinding modes.
- **Flyers spread out instead of piling up**, as in the original game: a
  flyer that keeps sharing its spot in the air with others steps a short way
  out of the crowd, to its least crowded side, and then carries on with what
  it was doing. Drakes and other flyers attacking one target or chasing one
  enemy flyer used to sit on top of each other for the whole fight; groups
  sent to one point and flyers coming in to land now settle apart. With
  Legion pathfinding, flyers moving with a formation keep their own lanes.
- **Flyers on a fight-move or a patrol step out of a crowd too, and a
  fight-move keeps a flying group's shape**, as in the original game.
  Drakes and other flyers sent on a fight-move flew the whole way stacked on
  one another and piled onto the clicked point; armed flyers on patrol lapped
  as one stack. They now fly a move's course, keep their places in the group
  as on an ordinary move, and step aside when they crowd together. A
  factory's flyers follow their rally the same way, and a Shift-patrol given
  to a patrolling flyer joins its loop at the end, as on the ground.
- **A flying guard whose charge dies looks for another**, as in the original
  game: it circles where its charge last stood and guards the first friendly
  unit on the ground (a building too) that comes within its sight. It used to
  stand down at once.
- **Flying builders at work step out of a crowd**: Flying Builders, Dark and
  Darkest Priests and Thirsha building, helping or repairing on one job
  hovered on top of each other; one crowded in now steps aside, stops working
  for a moment and comes back to the job.
- **Flyers with a `dropped` weapon make bombing passes** (mods only: no unit
  of the game has one), as in the original game, instead of ordinary attack
  runs.
- **Hovering attackers move round their target as in the original game.** A
  Drake (and every flyer that hovers to attack) comes in to a point beside
  its target, takes a new point round a target that stands still after a
  wasted shot, gives up an attack it keeps being crowded out of, and goes
  back to its formation when it strays too far from it. One that loses its
  target circles the spot and looks for another, as the other air attacks
  do.
- **Mind control, stone and freeze take effect when their shot arrives**, as
  in the original game. The Harpy's and Mind Mage's charm, the Basilisk's and
  Acolyte's Turn To Stone and the Mechanic's, Chief Engineer's and Neo
  Dragon's freeze took their target the moment they were cast, while the ball
  of energy was still drawn flying towards it. The ball is now a real shot: the
  spell works when it strikes, on the first unit it strikes (one that steps
  into its path, or the one behind a target that died, moved off or changed
  sides meanwhile; the ball is never steered). A charm in flight is lost if
  its caster dies; a stone or freeze shot still lands.
- **Stone and freeze follow the original's rules.** Like the charm they can
  fail: a unit without veteran rank falls 80 times in 100, one of rank 4 or
  more 99 times. They never take a Monarch, a building, a unit still being
  built or one of the caster's own, and they do no damage at all (in Crusades
  the freeze weapons hit immune units and buildings for 450). The same holds
  for the Area Freeze. Their roll, and the charm's, now draws on the
  original's random stream.
- **A charm no longer takes a building** (a wall, a Trebuchet or a Mortar,
  and in Crusades a Bomb Sprinkler, Gatling Crossbow or Prismatic Mirror), as
  in the original game, which charms only units that move; nor does the Area
  Mind Control.
- **A charm, stone or freeze shot that strikes an ally's unit takes it**, as in
  the original game, which spares only the caster's own units; the Area Mind
  Control and Area Freeze take allies' units in their area too. Casters still
  never pick an ally as a target. A stone or freeze caster no longer passes
  over a paralyzed enemy, and no longer picks one still being built, as in the
  original.
- **A spell's explosion shows on the unit it takes**, and only when it takes;
  the hit sound plays where the shot strikes.
- **Units attack enemy buildings still being built**, as in the original
  game. A building still being conjured, and a unit still being built at a
  factory, were invisible to every automatic target choice: idle, patrolling
  and moving units stood or walked past an enemy castle going up beside them.
  They now take one as a target, after any finished enemy near enough (the
  original scores it ten times worse). A guard keeps to the unit it guards
  but shoots one in range, and stone, freeze and charm casters still leave it
  alone.
- **Automatic targeting follows the original more closely in three more
  places:** a paralyzed enemy is no longer passed over for a healthy one, the
  random choice among several targets lands on the same one as in the
  original, and a computer player's caster no longer switches to stone,
  freeze or charm against a building still being built.
- **Legion: flyers keep the army's pace only in a formation.** Flyers in a
  group (Ctrl+number) with ground units were held to the ground units' pace,
  as in a formation, and set down in front of them; they now fly at their own
  speed to where you send them, as they do in no group. A formation's
  (Alt+number) flyers still keep to the army's pace and land just in front of
  it, but no longer on the edge of the map when the army stops near it: where
  there is no room in front, they land where they were sent.
- **Faster with big armies, the same game.** A performance pass changed how
  much work the game does, not what it does: every game plays out exactly as
  before (the same lockstep checks, AI orders and random draws), and the
  screen shows the same picture except for unit shadows in very large views
  (below). Measured on an Intel Core Ultra 9 275HX:
  - **Very large views draw much faster.** Unit shadows have room for four
    times as many models (up to 16 shadow pages instead of 4), so a
    7680x2160 view of 5,000-10,000 units keeps its soft shadows instead of
    falling back to slow per-triangle shadows: a 10,000-unit view drew in
    18.4 ms a frame instead of 31.5, and frames over 16.7 ms fell from 52% to
    15% in a view at the edge of the old limit. Those views' shadows now look
    like every smaller view's (lighter where a model overlaps itself); the
    extra pages cost 32 to 144 MiB of video memory, and a slow continuous
    pan across such a view is about 4% slower at its worst frames.
  - **The mouse pointer** finds the unit under it about 25 times faster with
    10,000 units (0.3-0.5 ms a frame down to 0.01-0.02 ms).
  - **Long build queues** draw their ghost buildings 3 to 15 times faster,
    and **Hidden Units: Fade** costs about half as much (whole frames 4-11%
    faster on forested and walled maps).
  - **Auras** (such as the Aramon Smith's armour aura) cost about half as
    much: a battle of 3,200 units with 1,200 Smiths took 17% less time a tick.
  - **Exploration and sight** cost far less on big maps (on a
    1-million-cell map 0.18 ms a tick down to 0.05) and update only the
    squares a moving unit's sight gains or loses: a marching army's tick
    takes 5-8% less time. The first minute of an 8-AI game on a large map
    took 38% less time a tick.
  - **Burning and fading scenery** is tracked by itself instead of scanning
    every tree each tick: an idle tick on a map with 11,600 features takes
    18% less time.
  - **Legion pathfinding** refreshes its view of buildings, finds the routes
    a new building or wall touches, and merges map changes faster on maps
    with very many buildings or changes (a static refresh with 2,000
    buildings takes about a quarter of the time).
  - **Whole games:** a late game of 8 Absurd AIs at 4,000 units ran 3% faster
    a tick, and recorded 10,000-unit games 0.4-1.9% faster.
  - **`simperf`** (a benchmark tool) also reports whole ticks including the
    lockstep check, and the render benchmark has new interactive cases.
- **Protocol 307:** the flying status spells, the melee and air-traffic rules
  and the new targets change what players compare, so this release does not
  play with 0.9.12 or earlier, nor their replays. A 0.9.12 client or server
  is turned away with a *protocol version mismatch* message, and a 0.9.12
  replay is refused with the version message. (Protocol 306 was never
  released.)

**Upgrading (server operators):** update `takserver` together with your
players.

[Release notes](docs/release-0.9.13-notes.md) · [Validation report](docs/release-0.9.13-validation.md) · [Source at v0.9.13](https://github.com/pocketgeek/tak-engine/tree/v0.9.13)

## 0.9.12
*2026-10-29 · protocol 305 · replay format 15 · does not play with 0.9.11 or earlier, nor their replays*

- **Fog of war looks and works as in the original game**: what your units see
  is the same sight the game plays by (further from high ground, less uphill,
  on 32-pixel squares), ground seen before lies under your side's rolling,
  drifting mist in its own colour, unseen ground is black, and radar marks
  enemies on the minimap only, never in the main view. Another player's unit
  shows, and can be clicked, only when a corner of it is in sight. The fog also
  costs less: at 3,000 units its overlay and minimap draw in 0.05 ms a frame
  instead of 0.17, and the separate fog pass and its thread are gone.
- **Our AI levels attack only what they have seen** (not in the original game,
  whose AI weighs unseen units too; the Retail AI is unchanged): their own and
  their allies' sight and radar, units shooting at them, and buildings
  remembered where they were seen; with nothing known they scout and explore
  the map instead. They follow the room's fog of war and start locations as a
  player does (Explored: the map's ground and deposits are known, enemies are
  not; Random hides whose start is whose), pick lodestone deposits only where
  they know the ground, judging one taken by what they have seen there, steer
  scouts away from where scouts died, and send their ships out when the army's
  target is inland. A beaten player's base, an enemy's or an ally's, is looked
  in on again once out of sight five minutes, in case someone moves in (and
  attacked if someone has). A game between AIs may now not end when a last
  unit stays hidden, and Not Explored games run longer (the AI no longer knows
  every deposit).
- **Weapons no longer aim at buildings past their range**, as in the
  original game: every weapon but melee measures a building's distance centre
  to centre, with no allowance for its size. A Death Totem no longer empties
  its lightning into the air at a lodestone just out of range, a Crossbowman,
  Shaman, mage or dragon ordered onto a building walks into range and hits it
  instead of stopping short, and bombers, earthquakes, storms, hail, freeze,
  stone and mind-control spells are no longer cast at a building from past
  their range.
- **Kirenna no longer always stands without firing after walking up to a
  target.** Her aim waits for her walk to settle, and the engine aimed the
  instant she stopped. Now, as in the original game, an attack aims a moment
  after the attacker stops, and a unit's own scripts hear it turn. Like the
  original, she still stalls when she stops mid-stride (until she moves
  again).
- **Flyers dogfight flyers**, as in the original game: a flyer attacking a
  flyer no longer hangs still or hovers beside it but chases it, swings out
  when threatened and now and then breaks off and turns before coming back.
  The Aerial Juggernaut now closes in so its anti-air arrows reach (it stopped
  at its cannons' range, and cannonballs miss flyers), and its cannons pick
  their own targets meanwhile, bombing the ground. Attacking a ground unit,
  the Juggernaut's and the Dirigible's other weapons likewise pick their own
  targets, as in the original. An Iron Beak drops an
  egg only once it faces its target, as in the original; the egg hits a flyer
  below the Beak and falls past one at its own height.
- **An attack flyer that kills its target circles the spot**, as in the
  original game, instead of stopping there and landing: when the attack you
  gave it was its last order and it is not on Hold Fire, it flies round the
  target's last position at its weapon's range plus 160 pixels and, on Fire
  At Will, attacks the next enemy that comes near, so an air raid carries on
  after each kill. On Return Fire it circles without looking; with orders
  queued behind the attack, or after an attack it picked up by itself while
  idle, it goes on as before. Give it a new order to call it off.
- **A big air group keeps its shape when moved**, as in the original game. A
  right-click move, MOVE or PATROL gives each selected flyer the clicked point
  plus its place in the group, as long as it is within about √(3000 × the
  number selected) pixels of the group's centre (173 for 10 units, 548 for
  100); a straggler farther out flies to the point itself. Flyers used to be
  squeezed into a 120-pixel square, and MOVE and PATROL sent them all to one
  spot. Ground units keep the engine's own spread (the original sends them
  all to the point).
- **A shot stopped in front of a castle counts as blocked.** A castle, keep,
  gate or other long building is hit through the ground it stands on, as in
  the original game, not anywhere within a circle of its length, so shots that
  land on a ridge or scenery in front of its long face count as blocked. An
  archer told to manoeuvre now walks to a clear line and hits it, where it
  used to fire into the crest for ever.
- **Campaign units follow their mission orders as in the original game.**
  The orders a mission gives its units were misread: units told to be
  Offensive turned Passive and Passive ones Offensive; factories and builders
  conjured their waves for free (one mission grew from 146 to 636 units in
  30 seconds) instead of building them, and a builder's building appeared at
  the start; troops meant to start aboard their ships stood on the shore;
  guards and ambushers watched the wrong unit, and an ambush sprang when an
  enemy came into sight instead of when it was hit; a unit told to attack
  every unit of a type attacked one; a wait meant to end when the enemy comes
  near never did; bosses lost their veterancy bonus (up to 9 times attack and
  armour); enemy units with a "make selectable" order were handed to you at
  the start (takx04's keep and castle among them); a unit told to
  self-destruct after a delay did so at once; and one order could swallow the
  next. Each is now read as the original reads it, so many missions play
  differently, and most get harder.
- **Builders help and repair as in the original game.** A builder told to
  help build or repair (or doing it on guard or patrol) walks up until it
  touches the unit, on the side it comes from, so helpers spread round a
  building instead of all making for one spot south of it and working from
  their build distance; once working, a builder that is pushed aside keeps
  working. A helper no longer gives up after 8 seconds held up in a crowd,
  only when no route gets it within its build distance, and an assist queued
  behind other orders starts when its turn comes. A walking unit is no longer
  repaired: the repair waits while it moves and is given up after a few
  seconds of walking, and a patrolling builder gives a repair up once it is
  its sight distance from its route (it used to measure the unit). A helper
  that finishes a unit inside a factory walks out of it afterwards, so it
  never blocks the door (not in the original).
- **The dead rise only when you raise them**, as in the original game.
  Right-click a body with Elsin, a Dark or Darkest Priest, a Chief Engineer,
  a Dark Hand or a Spirit Wolf and it walks over and raises it (a right-click
  used to make the priests and Elsin clear the body instead); the cursor
  shows the raise, and a selected builder that cannot raise it clears it. A
  caster clearing an area raises the bodies there that it can. Idle casters
  no longer raise nearby bodies on their own, and idle builders no longer
  clear every body around them: a body lies until it decays or is cleared or
  raised by order. As in the original, a body of any side can be raised (it
  rises as yours), and only a body that can be animated rises as a ghoul,
  creauto, lich or wolf. Our AI levels order their idle casters to raise the
  bodies beside them; the Retail AI never raises the dead, as in the original.
- **The Harpy converts only with its mind-control spell**, as in the original
  game: it no longer takes any enemy it stays beside for three seconds, or at
  once below a quarter of its health, for free. Each charm costs 300 mana and
  can fail, and the Harpy now casts on its own like the Mind Mage. **A charmed
  unit keeps its damage** (it used to come over at half health or more) and,
  as in the original, starts without its veteran stars. **A unit still being
  built cannot be charmed.**
- **Spells land when and where they do in the original game.** Earthquakes,
  hail, area charm and freeze and the other Remote Effect spells are cast
  ahead of a moving target, where the original aims them; for the slow ones
  (Mind Mage, Lich, Injured Thirsha) that is up to a second and a half of the
  target's walk. An Earthquake no longer strikes once too often: the Avatar of
  Anu's and the Ghost of Garacaius's hit twice, not three times, and the
  Acolyte's five times, not six. Hail at eight stones a second (the Ghost's
  Ice Storms, the Fallen Angel's Wrath) falls as often as in the original:
  19 and 21 hits, not 14 and 16. Spells hit a tick or two later than before,
  as in the original, and the storms of the Spawn of Belial, Angel of Lihr,
  Wrath of Tammuz and Weather Witch no longer wait 1.2 to 2.25 seconds before
  setting off.
- **A Divine Lodestone may cover the Standing Stones beside a sacred site**, as
  in the original game: a lodestone's footprint ignores the stones and other
  fixed scenery, so on Inner Circle it is no longer refused where it overlaps
  them (at two of the four sites). Scenery that can be cleared, such as a tree,
  is still cleared first.
- **Flyers draw over the explosions, debris and shots of a fight below them**,
  as in the original game, instead of under them; their shadows stay where
  they were.
- **An unfinished building under fire no longer takes its attacker as its
  target**, as in the original game, so once finished it picks its own.
- **Big late games keep their speed.** With 8 Absurd AIs on a 64x64 random map
  at +10, 0.9.11 fell to -2 at about 2,700 units, with 219 ticks over 250 ms;
  0.9.12 held +9 to +10 to 3,160 units with no tick over 100 ms:
  - **The AI's lodestone search** no longer tests every deposit on a large map
    for each site (300-840 ms in one tick for an Absurd AI, which dropped the
    speed below +0); it takes the nearest usable one (the same choice).
  - **A building that might cut off part of the map** is checked around itself
    instead of re-mapping the whole map (~90 ms each, up to 24 times a minute
    on a 64x64 map).
  - **A lodestone's mana bonus and Legion's narrow-passage lookups** no longer
    scan the whole map each tick.
  - **The lockstep state check** costs about 5 ms instead of 33 with 2,500
    units on a 64x64 map, and a game with no human player (AIs you watch)
    skips it on the server.
- **Streaming keeps to YouTube's bitrate**, above which YouTube could connect
  but never start the broadcast: the panel caps it there (a saved higher value
  is lowered, with a note), adds **Recommended**, and warns when the upload
  can't keep up.
- **SETTINGS, BENCHMARK and LOAD REPLAY have a BACK button** beside "Esc - Back".
- **Options, Voice Chat and Controls save every change as you make it**; the
  SAVE button is gone. A slider saves when let go, and **DEFAULTS** asks first
  (and puts the sound device back to the system default at once). The
  Controls hint has a band of its own, so it no longer covers the last row (a
  right-click there used to unbind that key).
- **Protocol 305:** the AI's sight-only targets, the combat fixes, the raise
  order and the new state check change what players compare, so this release
  does not play with 0.9.11 or earlier, nor their replays. A 0.9.11 client or server is turned away with a
  *protocol version mismatch* message, and a 0.9.11 replay is refused with the
  version message.

**Upgrading (server operators):** update `takserver` together with your
players.

[Release notes](docs/release-0.9.12-notes.md) · [Validation report](docs/release-0.9.12-validation.md) · [Source at v0.9.12](https://github.com/pocketgeek/tak-engine/tree/v0.9.12)

## 0.9.11
*2026-10-29 · protocol 304 · replay format 15 · does not play with 0.9.10 or earlier, nor their replays*

- **Hovering flyers no longer fire into scenery for ever** (not in the
  original game). A Thirsha or other hover attacker whose shots keep
  stopping on a standing stone or other indestructible scenery between it
  and its target now moves round the target to a clear line and hits it.
- **Full zoom-out shows the whole map width.** The zoom-out limit now leaves
  out the right-hand panel and the bottom bar, so the map's right edge no
  longer stays hidden under the panel, and the bottom edge scrolls into view
  above the bar.
- **Fade Opacity** (Hidden Units) now defaults to 50% (was 65%). A value in
  your settings file is kept, and 0.9.10 saved 65% there, so set the slider
  to 50% yourself for the new look.
- **A factory's unfinished unit dies with it**, as in the original game: when
  a factory is destroyed or changes hands, the unit it is building dies at
  once (no wreck, no kill for anyone, the owner's loss, no mana back);
  before, it stood unprotected and slowly faded away. Cancelling the unit in
  work does the same: from the build menu when it is the last queued unit of
  its type, by turning repeat off, or by **Stop** (beyond the original, whose
  Stop keeps production running), and a leaving or forfeiting player's
  factories lose theirs too. Cancelling a unit still waiting in the queue
  only removes it. A Zhon builder's unfinished unit and a builder's
  unfinished building still stay behind (the original keeps those).
- **Legion pathfinding:** units sliding round other units and walls face
  the way they actually move instead of their route's direction (units on
  fight, attack, guard and patrol orders keep facing their way to fight);
  sideways and backward walking falls by about two thirds.
- **AI attacks go for everything** (beyond the original game; Retail unchanged):
  our AI levels choose attack and raid targets from every enemy unit and
  building by value, threat and defences (unseen ones count for less, as the
  original AI's do), and move on from one to the next, so a winning AI
  finishes off a player's last lodestone or unit and the game ends.
- **Protocol 304:** the hover-for-shot move, the factory's unfinished unit
  dying, Legion facing and the AI's target choice change the game state the
  players compare, so this release does not play with 0.9.10 or earlier, nor
  their replays. A 0.9.10 client or server is turned away with a *protocol
  version mismatch* message, and a 0.9.10 replay is refused with the version
  message.

**Upgrading (server operators):** update `takserver` together with your
players.

[Release notes](docs/release-0.9.11-notes.md) · [Validation report](docs/release-0.9.11-validation.md) · [Source at v0.9.11](https://github.com/pocketgeek/tak-engine/tree/v0.9.11)

## 0.9.10
*2026-10-28 · protocol 303 · replay format 15 · plays with 0.9.9, and its replays*

- **Hidden Units** (Options → Graphics; not in the original game): units
  behind trees, rocks and buildings stay visible. **Fade**, the default,
  fades the whole tree, rock or building out while a unit is behind it (to
  65% by default; **Fade Opacity** slider, 0-100%); **Dots** puts the
  unit's tactical dot on top; **Off** hides it as before. Only ground units
  you can see anyway are marked.
- **Flat scenery is drawn under units**, as in the original game: features
  whose height is below 10 (stumps, crops, smudges, mana deposits) no longer
  paint over the feet of a unit standing just north of them.
- **macOS: Auto renderer now picks OpenGL**, as Windows and Linux do, so the
  game's OpenGL-only fast drawing paths run on a Mac too (2.6 times as fast as
  Metal at 2,000 units on a MacBook Neo). Apple's software OpenGL is passed
  over; Metal follows if OpenGL cannot start. A saved **Metal** choice is
  kept. If OpenGL misbehaves, choose **Metal** under Options → Graphics →
  Renderer.
- **Metal: no stray drawing from off-screen clips.** A part of the screen
  clipped away entirely (a unit behind a wall at the view's edge, the chat
  box) no longer draws over the view on the Metal renderer, and the Options
  and Hotkeys lists in a window too small for them no longer draw outside
  their frame (that one on every renderer).
- **Protocol 303, unchanged:** every change in 0.9.10 is in the game's
  display and options, and nothing the players compare changed (checked in
  the source against 0.9.9), so 0.9.10 and 0.9.9 clients and servers play
  together in either direction, and each plays the other's replays. Servers
  need no update.

[Release notes](docs/release-0.9.10-notes.md) · [Validation report](docs/release-0.9.10-validation.md) · [Source at v0.9.10](https://github.com/pocketgeek/tak-engine/tree/v0.9.10)

## 0.9.9
*2026-10-28 · protocol 303 · replay format 15 · does not play with 0.9.8 or earlier, nor their replays*

- **The AI upgrades to divine lodestones only when it pays.** Defensive,
  Normal, Hard, Absurd and Absurdly Defensive now put a divine lodestone only
  on one of their own lodestones, once no free deposit near home is left, and
  only where the upgrade pays for itself within 15 minutes (20 for Absurdly
  Defensive). They save up for it first, do one at a time, and train a priest
  for it when they have none. Hard and Absurd no longer start divine lodestones
  on free deposits far from home with an empty store, and a priest no longer
  starts a dragon it cannot pay for. From 15 minutes on Defensive (at most 5),
  10 on Normal (2), 8 on Hard (6), 6 on Absurd and 8 on Absurdly Defensive (no
  limit); Easy never upgrades.
- **Make Poster is Shift+P.** The default key for MAKE POSTER (power codes) is
  now **Shift+P** instead of Print Screen, which many desktops take for their
  own screenshots and Mac keyboards lack. A key you set yourself in CONTROLS is
  kept.
- **Back from a Settings screen returns to Settings.** Options, Controls,
  Voice Chat, Benchmark and YouTube Streaming opened from the title screen's
  SETTINGS menu now go back to it on BACK or Esc, as Load Replay already did,
  instead of to the title screen.
- **Max FPS can be unlimited.** The Options slider runs from 30 to 240 frames
  a second, and its far right end is **Unlimited**. A saved cap above 240 (the
  settings file used to allow up to 480) now reads as Unlimited.
- **Builders only place what they can reach.** When a walking builder can
  never get close enough to a spot to build there, such as a shipyard out in
  open sea or on a lake it has no way to walk to, the placement ghost turns
  red and the click is refused. Before, the order was accepted and the builder
  set off, then gave up. This goes beyond the original game, which accepted
  such sites. Flying builders still place anywhere they could build.
- **Your units get out of the way of a build.** When a builder reaches its
  site and some of your own idle units, or units guarding something (the
  builder itself, say), stand on it, they now step just off the site, and the
  builder starts as soon as it is clear. A guard keeps its Guard order and goes
  back to guarding. You can also place a building on top of such units now;
  before, the site was refused. Units of other players, and your units that are
  moving, fighting or working, still make the builder wait and give up after
  about ten seconds, as in the original game, where a builder only ever waits.
- **A builder guarding a factory no longer blocks its door.** A unit on
  **Guard** used to walk up to the middle of what it guarded and stop about 70
  pixels from it, which for a factory is its yard: the guard stood on the spot
  where the next unit is made, and the factory could not start another one.
  As in the original game, a ground unit now guards from a ring round the unit
  or building it guards: measured from its centre, the guarded unit's full
  longer side plus the guard's own width plus two cells, and three cells deep
  (a Barracks guard waits about 256 to 304 pixels from the Barracks' centre),
  so a guarding builder walks in to help each unit being made and back out
  between them; a ship guarding a shipyard does the same. Guards of soldiers
  keep a little further off than before for the same reason. Protocol 303.
- **The Benchmark always runs the plain game, with no mods.** It used to load
  your ticked cosmetic and game mods and the packs in `always_enabled`, so a
  mod could change the units, the AI or the art being measured, and two
  machines with different mods gave numbers that could not be compared. Now
  the Benchmark leaves every mod pack out, and its private server plays the
  original game data. Your mod choices are unchanged and apply again in your
  next game.
- **The Retail AI no longer upgrades its own Lodestones.** As in the original
  game, the Retail AI level builds a Divine Lodestone only on a free sacred
  site: one where its own Lodestone already stands is passed over for the next
  free site in reach, and with none it builds nothing. Before, it could order a
  Divine Lodestone on top of its own finished Lodestone. You can still upgrade
  a Lodestone in place yourself, and the other AI levels are unchanged.
- **A build icon's tooltip is readable while something is training.** The
  TRAINING line above the build menu and the tooltip of the icon under the
  mouse were drawn in the same place and overlapped; the tooltip now takes the
  line's place while it is shown.
- **Protocol 303:** units stepping off a build site and the guard ring are
  part of the game state the players compare, so 0.9.9 plays only with 0.9.9
  and plays only replays recorded by 0.9.9. A 0.9.8 client or server is turned
  away with a *protocol version mismatch* message, and a 0.9.8 replay is
  refused with the version message.

**Upgrading (server operators):** update `takserver` together with your
players.

[Release notes](docs/release-0.9.9-notes.md) · [Validation report](docs/release-0.9.9-validation.md) · [Source at v0.9.9](https://github.com/pocketgeek/tak-engine/tree/v0.9.9)

## 0.9.8
*2026-10-27 · protocol 302 · replay format 15 · does not play with 0.9.7 or earlier, nor their replays*

- **Guards defend what they guard,** as in the original game. When something
  attacks the unit a guard is guarding, the guard goes after the attacker,
  however far away it is shooting from, and comes back to guarding once it is
  dead. Before, a guard only fought what came within its own reach, so an
  archer or a tower outranging it could shoot the guarded unit unanswered.
  Flying guards do the same. As in the original, the guard answers even on
  Hold Fire or Hold Position, and an unarmed guard (a builder) keeps helping
  and repairing instead.
- **A computer player short of mana no longer tries the same raising over and
  over.** A computer priest gives up a resurrect or animate at its end when
  its side is well short of mana; it then took the same corpse up again at
  once and gave it up again, for as long as the shortage lasted. It now waits
  until the economy recovers before it raises anything.
- **Raising the dead respects the unit limit.** A resurrect or animate that
  would take a side past the game's unit limit (or a unit past its own limit,
  such as a second dragon) now ends without raising anything and leaves the
  corpse, as in the original. Before, it raised the unit anyway. A priest
  already at the limit does not start one.
- **Builders guard on a right-click,** as in the original game. Right-clicking
  one of your finished, undamaged units with a builder selected now tells the
  builder to Guard it, as the G key does; before, a right-click guarded only
  with armed units and a builder did nothing. Each selected unit decides for
  itself: a builder repairs a damaged unit while the soldiers selected with it
  guard it (before, they did nothing), and any unit that can guard does so,
  armed or not. An unfinished unit is still helped to build, an enemy still
  attacked and a transport still boarded. A god, which the original cannot
  tell to guard, no longer guards on a right-click either.
- **Release builds read no diagnostic environment variables.** The remaining
  `TAK_*` logging and tuning hooks in the server, simulation, AI, network code
  and Cartographer now work only in debug builds, as the client's already did.
  A release `takserver` still honours its four test timers (`TAK_TIMEOUT_MS`,
  `TAK_LOAD_TIMEOUT_MS`, `TAK_STALL_MS`, `TAK_DROP_WAIT_MS`).
- **Documentation checked against the game.** The README, user guide, quick
  start, Cartographer guide and the developer, server, multiplayer, pathfinding,
  AI, campaign and research documents were checked against 0.9.7 and brought
  up to date. Among the fixes: the user guide no longer says a guarding
  builder repairs an ally's unit it guards (you cannot guard an ally's unit;
  the game refuses it), and the development guide lists the release client's
  options correctly.
- **Protocol 302:** a guard's answer to an attack on what it guards is part of
  the game state the players compare, and so are the new raising rules, so
  0.9.8 plays only with 0.9.8 and plays only replays recorded by 0.9.8. A 0.9.7
  client or server is turned away with a *protocol version mismatch* message,
  and a 0.9.7 replay is refused with the version message.

**Upgrading (server operators):** update `takserver` together with your
players.

[Release notes](docs/release-0.9.8-notes.md) · [Validation report](docs/release-0.9.8-validation.md) · [Source at v0.9.8](https://github.com/pocketgeek/tak-engine/tree/v0.9.8)

## 0.9.7
*2026-10-27 · protocol 301 · replay format 15 · does not play with 0.9.6 or earlier, nor their replays*

- **Helping to build now works the way the original game does.** A mobile
  builder can help finish any unfinished unit of its own side (not an ally's),
  including a unit a factory or a Zhon builder is still producing. The helpers
  and the producer work on one shared build: the unit finishes sooner, the mana
  is spent faster, and the total cost is still its build cost. Before, a helper
  on a produced unit finished it early while its producer kept its slot and was
  charged the full cost again (a Zhon builder paid 108 mana too much on a unit
  costing 311) and kept healing it for free. Now the producer is freed the
  moment the unit is finished, the unit's finishing orders (a Zhon builder's
  exit step, or a factory's PARK or rally) are given once, and orders you gave
  the unit while it was unfinished are kept. A factory with no helper finishes
  on exactly the tick it did before.
- **Guarding builders lend a hand.** A mobile builder told to Guard a unit
  repairs it when it is damaged (allies too), helps build it when it is
  unfinished, and otherwise helps with what that unit is building, producing,
  helping with or repairing. The help goes ahead of the guard order, which
  resumes afterwards; a guarding builder repairing a unit that walks off follows
  it.
- **Flying builders guard the same way,** as the original's flyers do: a
  guarding Flying Builder, Dark Priest, Darkest Priest or Thirsha repairs and
  helps build, and also helps flying producers (a Thirsha's output), which a
  ground guard does not. Flying builders now fly in to twice their build
  distance before they start helping or repairing, then circle the job at about
  their build distance; before, a flyer helping a site someone else had begun worked on it
  from any distance, and a flying repairer hovered on top of its target. A
  flying builder does not repair a unit under the sea.
- **Flyers on guard circle what they guard,** as the original's do. Any flyer
  told to Guard (armed, unarmed or a builder with nothing to help) now circles
  the guarded unit about 320 pixels out, or its weapon's range plus 160 when it
  is armed, and follows it when it moves; before, it hovered close by. A
  flyer given the guard while off the map flies back onto it first and keeps
  the guard.
- **Who may help is checked by the game, not just the screen.** A builder helps
  only a unit of its own side, only if it can move (buildings never help), and,
  when it may build only what is on its own menu, only a type on that menu. A
  modified client cannot get around this.
- **Builders on a repeat queue can help.** A Zhon builder repeating a unit can
  be told to help with an unfinished unit; its own product waits until it has
  finished helping. Move, Patrol and Attack orders still set its rally.
- **For testers:** a debug build's `TAK_AUTOPLAY` now also builds, trains, puts
  conjurers on a repeat queue, guards busy units and assists unfinished ones,
  and sends idle flying builders to guard damaged or busy units, so the desync
  sweeps cover these orders. `TAK_START_FLYERS=N` (set on every peer) starts
  each player with N flying builders, every other one damaged, and
  `TAK_AUTOPLAY_SIDE` picks the autoplay seat's side. Release builds are
  unchanged.
- **Protocol 301:** the shared build progress, the guard's next look, a flying
  guard's circling and the help walk are part of the game state the players
  compare, so 0.9.7 plays only with 0.9.7 and plays only replays recorded by
  0.9.7. A 0.9.6 client or server
  is turned away with a *protocol version mismatch* message, and a 0.9.6 replay
  is refused with the version message.

**Upgrading (server operators):** update `takserver` together with your
players.

[Release notes](docs/release-0.9.7-notes.md) · [Validation report](docs/release-0.9.7-validation.md) · [Source at v0.9.7](https://github.com/pocketgeek/tak-engine/tree/v0.9.7)

## 0.9.6
*2026-10-27 · protocol 300 · replay format 15 · does not play with 0.9.5 or earlier, nor their replays*

- **Slow loaders are no longer dropped.** The server now allows 30 s of
  silence, up from 15 s, while a player's game is loading, so a slow PC or a
  huge map no longer drops the seat before it finishes. Play and the lobby keep
  15 s. The server's own start-up work (building its referee world) no longer
  counts as player silence either.
- **Double-click selects every unit of that type.** Double-click one of your
  units (or buildings) to select all units of the same type that are finished
  and visible on screen. Hold Shift while double-clicking to add them to your
  selection, or Ctrl to remove them. Double-clicking an enemy or allied unit
  selects just that one. The original game has no double-click select; this
  follows the player request.
- **Rally orders for production.** Production buildings, and builders producing
  on an endless queue (such as the Zhon mobile builders), now accept Move,
  Patrol, Attack and Move-fight as rally orders, and Shift queues them in the
  same way as a unit's own orders. Every unit produced then runs the whole list
  in order; a Shift-queued patrol loops. Stop clears the rally. The original
  game gives produced units only Move and Patrol rallies, and only from
  buildings; Attack and Move-fight rallies, and rallies from mobile builders,
  are additions.
- **The mana bulb shows mana requested.** The red number on the right of the
  mana bulb is now what all your builders and producers are asking for each
  second, as in the original game, instead of only what could be paid. When
  your mana runs short it can be larger than the green income number on the
  left; both are averaged over the last second.
- **Crusades and scenario maps.** A scenario map that gives stats to a custom
  unit type the Crusades balance does not have now simply skips that type, as
  the original does, instead of refusing to start. A scenario that places a unit
  Crusades lacks is still refused, and the message now names the unit and tells
  you to turn CRUSADES off. Test Map accepts Use Only entries from the other
  balance. Cartographer's Check Map warns about units a Crusades game will not
  have.
- **Construction hit points.** An unfinished building's hit points can no longer
  grow past its maximum, and a hit on an unfinished retail-style site now
  rewrites its build progress as the original does.
- **Native Wayland on Linux.** Options, Display has a new NATIVE WAYLAND setting
  (Auto, On, Off; restart required). On a Wayland desktop (GNOME, KDE Plasma,
  Sway, Hyprland and others) the game and Cartographer now open native Wayland
  windows, sharp and at the right size at 150% or 200% scaling; GNOME without
  the libdecor library keeps X11. Switching keeps UI Scale and Cursor Size the
  same size on screen: the game measures whether its old X11 window had real
  pixels or was enlarged by the desktop and converts them only in the second
  case, so on KDE Plasma's default they stay as set. Cartographer follows the
  same setting, converts its UI Scale and window size the same way and
  re-centres its window after the conversion. Set NATIVE WAYLAND to Off to
  return to the X11 window; the sizes are converted back.
- **Command-line tools.** `cartographer --version` and `--help` work, the dev
  tools print their usage when given a missing or bad argument, and `simperf`
  works with the current random-map recipe.
- **Test harness reliability.** Headless runs keep their server seat while the
  simulation is slow, and report the exact tick the result landed. A mod
  transfer test no longer drops a player while loading, and the server log now
  says why a client was dropped.
- **Cloaking spends the unit's own mana.** As in the original game, a cloaked
  unit pays for cloaking from its own mana (its blue bar), not from your mana
  pool, so cloaking no longer shows in the mana bulb. When the unit's mana runs
  out it becomes visible for three seconds and then cloaks again. The Trapdoor
  Spider, which has no separate moving cost, now pays the original game's much
  smaller cost while it moves.
- **Fighting and working reveal a cloaked unit, as in the original game.**
  Each shot keeps the unit visible for 20 seconds. Building, reclaiming or
  raising the dead keeps it visible while it works and for 10 seconds after,
  and repairing for 5 seconds after. Enemies close by no longer make a cloaked
  unit visible (the original game has no such rule), and a cloaked unit pays
  its moving cost for any movement, even turning on the spot.
- **Raising the dead costs no mana.** As in the original game, resurrecting
  and animating are free: they no longer drain your mana pool or show in the
  mana bulb. A short mana supply still slows them, as it slows construction.
  Resurrection now takes three tenths of the unit's build time, and animation
  the creature's whole build time (until now they were the other way round).
  A computer player whose mana is well short abandons a raising at its end, as
  in the original.
- **Computer players judge a mana shortage as the original game does.** The
  Retail AI, and the Passive, Easy, Normal and Absurdly Defensive levels, now
  compare their income with the mana their builders and producers ask for,
  not with what a short pool could pay them, when deciding whether they need
  another lodestone.
- **Protocol 300:** rally plans, the cloak block and the raising-the-dead
  channel are now part of the game state the players compare, so 0.9.6 plays
  only with 0.9.6 and plays only replays recorded by 0.9.6. A 0.9.5 client or
  server is turned away with a *protocol version mismatch* message, and a
  0.9.5 replay is refused with the version message.

**Upgrading (Linux players on a Wayland desktop):** the first start of 0.9.6
opens a native Wayland window instead of an X11 one, on KDE Plasma as well as
GNOME, Sway, Hyprland and the others; GNOME without the `libdecor` library
keeps X11. UI Scale and Cursor Size keep the same size on screen. If the
native window misbehaves on your desktop, set Options, Display, NATIVE WAYLAND
to Off and restart.

**Upgrading (server operators):** update `takserver` together with your
players. A server without `--map-cache-dir` keeps its maps in
`TAKengine/takserver` in its per-user folder (since 0.9.5). A 0.9.6 server
allows a loading seat 30 s of silence and logs why it dropped a client.

[Release notes](docs/release-0.9.6-notes.md) · [Validation report](docs/release-0.9.6-validation.md) · [Source at v0.9.6](https://github.com/pocketgeek/tak-engine/tree/v0.9.6)

## 0.9.5
*2026-10-26 · protocol 299 · replay format 15 · does not play with 0.9.4 or earlier, nor their replays*

- **Dead units stay dead.** A unit that was killed could take more hits in the
  same instant, for example from the rest of a big splash or a volley landing
  together. Each extra hit was still subtracted from its health, and with
  enough of them the health counter ran past its lowest value and wrapped round
  to nearly full: the "dead" unit stood back up with around 30,000 hit points.
  As in the original game, a unit that has just been killed now ignores every
  further hit: the player whose hit killed it keeps the kill, and the way it
  dies, its overkill and its last reaction come from that killing blow alone.
  A single hit stronger than any unit's health (over 32,767 damage, possible
  with armour and veteran bonuses) also no longer wraps round. Health is capped
  at 32,767, the most the original game can hold; no shipped unit has more
  than 29,999. Unfinished buildings under heavy fire, and units standing in
  deadly water, are protected the same way.
- **The game no longer writes into your game data folder.** Until now, every
  map a game room accepted, every map downloaded from another player, every
  downloaded game mod, every random map you played and every Cartographer Test
  Map copy was saved inside the folder holding your *Total Annihilation:
  Kingdoms* install (in its `MapCache`, `ModCache` and `Maps` folders). They now
  go in your own per-user folder, beside `settings.ini`:
  `%APPDATA%\TAKengine\TAKingdoms\` on Windows,
  `~/Library/Application Support/TAKengine/TAKingdoms/` on macOS and
  `~/.local/share/TAKengine/TAKingdoms/` on Linux. The game folder can now be
  read-only, for example a shared or system-wide install. A `MapCache` or
  `ModCache` folder an earlier version left in the game folder is ignored and
  left alone (you can delete it); a map you had downloaded is downloaded again
  the first time a room uses it. Random maps an earlier version saved in the
  game folder's `Maps` still show up as ordinary maps, and Cartographer still
  lists your downloaded maps and Test Map copies. Screenshots and the maps and
  mods you save from Cartographer still go where they did.
- **A game whose map cannot be built says so:** creating a game room on a map
  that cannot be built (for example one whose terrain art is missing) now stays
  on the setup screen and shows *CANNOT CREATE* with the reason, instead of
  silently doing nothing.
- **Windows test fixes and test tooling:** eight developer tests that
  failed on Windows are fixed (they needed symbolic links, which the Windows
  build cannot make, and `hpitool cat` wrote its output in text mode, which
  corrupted extracted maps). `hpitool cat` now writes exact bytes on Windows.
  The developer test harness stops at once with an error when its map cannot
  be built, instead of waiting for its time limit; the desync-hunt script no
  longer mistakes its own folder name for a desync and can use any build
  folder; and an out-of-bounds read in a Cartographer test is fixed.
- **The Legion scenario gate is re-baselined.** The developer gate that replays
  145 Legion pathfinding scenarios and compares them against a stored baseline
  had been left out of release checks since 0.8.11, and its stored game-state
  checksums had gone stale in 0.9.0, when reclaiming changed what the checksum
  covers. Only those checksums are retaken; every movement and timing limit is
  unchanged, and the gate passes again.
- **Server operators:** without `--map-cache-dir`, `takserver` now keeps
  uploaded and random maps in `TAKengine/takserver` in the per-user data folder
  (`$XDG_DATA_HOME` or `~/.local/share` on Linux, `%APPDATA%` on Windows,
  `~/Library/Application Support` on macOS) instead of its data folder, and it
  refuses to start if it has neither a home folder nor `--map-cache-dir`.
- **Protocol 299:** the dead-unit fix changes how fights play out, so 0.9.5
  plays only with 0.9.5 and plays only replays recorded by 0.9.5. A 0.9.4
  client or server is turned away with a *protocol version mismatch* message,
  and a 0.9.4 replay is refused with the version message.

**Upgrading (server operators):** update `takserver` together with your
players. A server started with `--map-cache-dir` behaves as before; one started
without it now caches maps in its per-user folder (above), and its data folder's
old `MapCache/` is no longer read.

[Release notes](docs/release-0.9.5-notes.md) · [Validation report](docs/release-0.9.5-validation.md) · [Source at v0.9.5](https://github.com/pocketgeek/tak-engine/tree/v0.9.5)

## 0.9.4
*2026-10-26 · protocol 298 · replay format 15 · does not play with 0.9.3 or earlier, nor their replays*

- **The Darien Crusades meta game is removed.** Everything built for the online
  campaign is gone from the client and the server: the strategic map of Darien
  and its campaign screens, the online campaigns themselves, allegiance to a
  side, territory matchmaking (searching for and issuing a battle over a
  territory), the issued Crusades battles with their forfeit-on-disconnect rule,
  the server's campaign database and its archive of battle replays, and the
  `crusades_admin` tool that managed it. The meta game was never offered in the
  game browser, so ordinary skirmish, multiplayer and campaign play is
  unchanged: rooms, accounts, spectating, reconnecting, replays, voice chat and
  game mods work as before. The client no longer asks a server for Crusades
  campaigns after signing in, and the lobby, the in-game menu and replay
  playback lose their Crusades-battle labels and rules (no more "ESC: RETURN TO
  CAMPAIGN").
- **What stays: the Crusades balance and accounts.** The **Crusades** option of
  a game room (the alternative unit balance from the final retail patch, and
  `--crusades` in debug launches) is kept, and so are server accounts and
  sign-in, which never depended on the meta game.
- **Smaller downloads and fewer moving parts:** `crusades_admin` is no longer in
  any package, `takserver` no longer links SQLite (the static SQLite build is
  gone from the source tree), and the client no longer mounts the Boneyards
  campaign art it used only for the strategic map. With issued battles gone,
  the server's rules have no exceptions left: every room writes its replay to
  the replay directory (when one is set) and allows spectators by its ALLOW
  SPECTATORS option, and a player whose connection runs out of time is always
  replaced by a server AI.
- **Crusades balance as in the original game.** The original game never reads
  the `canbuildcb/` build menus shipped with the final patch, and it does not
  fall back to the standard unit files for a unit the Crusades set lacks. With
  Crusades on, the units are now only those of the Crusades unit set
  (`unitscb/`), and every build menu is the standard one, limited to those units
  and in the same order. In practice:
  - **Six Iron Plague units do not exist in a Crusades game:** the Trapdoor
    Spider, the Heligrin, the Cult of Lokken Ship, the Sea Serpent, the Mummy
    and the Iron Plague's second Lokken unit (`tarnecr2`; the Taros monarch
    Lokken is unaffected). Until now a Crusades Beast Tamer could train the
    Trapdoor Spider, which the original game never offers.
  - **Gods are built by the monarchs again:** Elsin, Kirenna, Lokken and Thirsha
    build their side's god, as in the standard game, instead of the Acolyte,
    the Priest of Lihr, the Dark Priest and the Shaman.
  - **The Zhon Shaman builds the Giant Orm and the Giant Barracuda**, and the
    **Beast Handler builds the Swamp Beast**, as in the standard game.
  - **The Beast Tamer has its nine standard entries** (Lodestone, Kraken,
    Gryphon, Spirit Wolf, Basilisk, Harpy, Death Totem, Beast Handler, Beast
    Lord) instead of thirteen.
  - **The Ark has no build menu** (it built the Spyhawk, Swordsman and Archer
    in Crusades games).
  - The other menus that had moved are standard again: the Grenadier comes
    from the Acolyte, not the Keep; the Flying Builder no longer builds the
    Rolling Tower; the Flagship no longer builds the Lighthouse; and the
    Ballista comes from the Priest of Lihr, not the Citadel, whose menu is back
    in its standard order.

  Unit costs and statistics, which is what the Crusades balance is really
  about, are unchanged. Standard games are bit-identical to 0.9.3. A menu entry
  without a `[Menu] Priority` now sorts last, as in the original game, and a
  unit without `builder=1` gets no build menu; this matters only for mods,
  since the shipped data has neither. Because `canbuildcb/` is no longer game
  data, the game-data check value every player and server compare changes for
  every install; this only matters together with the protocol change below.
- **Protocol 298:** removing the Crusades messages renumbers the network
  messages after them, and the balance fix changes Crusades games, so 0.9.4
  plays only with 0.9.4 and plays only replays recorded by 0.9.4. A 0.9.3
  client or server is turned away with a *protocol version mismatch* message,
  and a 0.9.3 replay is refused with the version message. A Crusades-balance
  replay containing one of the six removed units could not be played anyway.

**Upgrading (server operators):** update `takserver` together with your
players. `takserver` no longer has `--crusades-db` or `--crusades-definition`;
a server still started with either (and its value) prints a warning that the
option was removed and is ignored, then starts as usual, so remove them from
your service drop-in or script at your convenience. The campaign database (`campaign.sqlite` and its `-journal`, `-wal`,
`-shm` and `.service-lock` files) and the `crusades-replays/` folder are never
read and can be deleted. Accounts, the account file and the replay directory
are untouched. See [public-server.md](docs/public-server.md#versions).

**Upgrading (players):** nothing to do. A client's old `ReplayCache/` folder
beside `settings.ini` (downloaded Crusades battle replays) is no longer used and
can be deleted; your settings, including the remembered Crusades choice of the
game setup screen, are kept.

[Release notes](docs/release-0.9.4-notes.md) · [Validation report](docs/release-0.9.4-validation.md) · [Source at v0.9.4](https://github.com/pocketgeek/tak-engine/tree/v0.9.4)

## 0.9.3
*2026-10-26 · protocol 297 · replay format 15 · does not play with 0.9.2 or earlier, nor their replays*

- **Trebuchets fire again:** a Trebuchet no longer sits idle because a wall,
  hill, tree line or other scenery lies between it and its target. Its
  shell flies in an arc, so it fires, as in the original game, which never
  checks for a clear line before firing a lobbed shell. In a test game
  between two computer players an Aramon Trebuchet held a target for 47
  minutes without a single shot.
- **Every arrow, spear and cannon ball does the same:** the same fix applies
  to 42 unit types that shoot a plain arcing shell: the bows, cannons and
  both Trebuchets, the Arrow Tower and other towers with arrow weapons. They now fire at targets behind scenery instead of waiting
  for a clear line, and a unit whose shots keep landing short walks to a clear
  spot as described under Blocked shots in the user guide. Units with
  direct-fire weapons (flames, magic bolts, beams) still need a clear line,
  and a unit that mixes both kinds (the Cre'on Sage) now tests each weapon on its own.
- **Towers stop holding targets they cannot hit:** a tower or other building,
  or a unit on Hold Position, that has picked a target it has no clear line to
  now drops it and looks for another, instead of holding it without firing
  while an enemy in the open shoots it. In a test game a tower whose own new
  walls blocked its target ignored an enemy in the open for a whole minute.
  Orders you give directly are unchanged.
- **Fewer idle standoffs:** in 16 test duels between two computer players
  (Creon against Aramon on Edmont's Field), the times a unit stood holding a
  target for over a minute without firing fell from 80 to 4, and the one duel
  that ended with no winner within 90 minutes now has one.
- **macOS streaming uses OpenSSL:** streaming to RTMPS services (YouTube,
  Twitch and the like) from a Mac now uses the same OpenSSL as multiplayer,
  instead of Apple's deprecated SecureTransport, which needed a patch in 0.9.1
  to keep uploads alive. Certificates are still checked against the macOS system root
  certificates, and a certificate for the wrong host or from an unknown
  authority is still refused with the TLS CERTIFICATE reason. Windows and
  Linux are unchanged. Certificates you added to the Mac keychain yourself
  (for example a company proxy) are not used.
- **Streaming panel remembers its settings:** the YouTube streaming panel
  now keeps its resolution, frame rate, bitrate and encoder between sessions
  in `settings.ini`. A value that is not one of the panel's choices falls back
  to the default (MAX 3840, 60 FPS, 6,000 Kbps, Automatic), a saved bitrate
  below the saved resolution's minimum is raised to it, and Options' reset to
  defaults leaves them alone, like Remember Key and Save a Local Copy.
- **Stream banner no longer covers the scorecard:** the STREAM LIVE banner in
  the top-left corner hid the F4 scorecard's header. The banner, game clock,
  scorecard, objectives and voice-chat speaker list now stack in that corner
  instead of overlapping, and at large scorecard scales the scorecard stays
  clear of the minimap. The banner is not part of the streamed video, but the
  overlays below it move down by its height there too.
- **Not compatible with 0.9.2:** the firing changes change the game itself,
  so 0.9.3 uses protocol 297 (replay format 15 is unchanged). It does not play
  with 0.9.2 or any earlier version, nor their replays.
- **Upgrading:** a server and all of its players update to 0.9.3 together;
  public-server operators update `takserver` as well. Keep 0.9.2 (it stays on
  the releases page, or build it from its tag) to watch replays recorded
  before 0.9.3.

[Release notes](docs/release-0.9.3-notes.md) · [Validation report](docs/release-0.9.3-validation.md) · [Source at v0.9.3](https://github.com/pocketgeek/tak-engine/tree/v0.9.3)

## 0.9.2
*2026-10-25 · protocol 296 · replay format 15 · does not play with 0.9.1 or earlier, nor their replays*

- **Flames blocked by scenery:** flames that stop on a rock, a standing
  stone or other indestructible scenery now count as blocked shots, like
  arrows and spears. A Fire Wagon or other flame unit that picked its target
  while on the move (an attack-move, a patrol or a queued move) walks to a
  clear shot, even on Hold Position, or drops the target and carries on. In a
  reported game a Fire Wagon burned a standing stone 101 times in a row
  without ever reaching the lodestone behind it.
- **Lobbing weapons stay within their arc:** units that lob their shots (the
  Creon Submersible, catapults and the like) no longer fire at targets their
  arc cannot reach, as in the original game. They close in until they can hit,
  or hold their fire when they cannot get nearer. Before, such a unit lobbed
  the shell flat into the ground or water in front of it: in a reported game
  44% of all Submersible shells landed beside the Submersible itself.
- **Towers no longer flicker between targets:** towers, other buildings and
  Monarchs holding position no longer pick a target they cannot hit, only to
  drop it again a moment later, over and over. They keep firing at what is in
  range. In a test game between two Retail AI players, the Creon Monarch,
  Prismatic Mirrors and Aramon Strongholds spent their time switching
  targets instead of shooting, and the game never ended.
- **AI factory doors:** the computer players (every level except Retail) keep
  the ground in front of their factory doors clear and no longer park units in
  another factory's doorway, so new units are not trapped inside.
- **AI Navy Yards:** the computer players build Navy Yards where ships can sail
  straight out to open water, not at the head of a narrow inlet where new ships
  stayed stuck.
- **Not compatible with 0.9.1:** the flame, lobbing and targeting changes
  change the game itself, so 0.9.2 uses protocol 296 (replay format 15 is
  unchanged). It does not play with 0.9.1 or any earlier version, nor their
  replays.
- **Upgrading:** a server and all of its players update to 0.9.2 together;
  public-server operators update `takserver` as well. Keep 0.9.1 (it stays on
  the releases page, or build it from its tag) to watch replays recorded
  before 0.9.2.

[Release notes](docs/release-0.9.2-notes.md) · [Validation report](docs/release-0.9.2-validation.md) · [Source at v0.9.2](https://github.com/pocketgeek/tak-engine/tree/v0.9.2)

## 0.9.1
*2026-10-25 · protocol 295 · replay format 15 · does not play with 0.9.0 or earlier, nor their replays*

- **Aiming at the body:** units now aim at the middle of the target's body, as
  in the original game. Before, most units aimed too low, some below the
  target's feet, so spears and other shots that fly in a curve hit the ground in
  front of it: Zhon Hunters almost never hit each other (4% of shots in a
  reported game, 89% now). The aim point also turns with the target, as in the
  original.
- **Lightning at the Creon Submersible:** whether the Thirsha's lightning hits
  a parked Submersible now depends on which way the Submersible faces, as in
  the original, and it still misses often, as the original does: the
  Submersible's aim point lies off its centre, outside the cells it stands on.
  In the reported game it missed 47 bolts out of 47; the original hits 17 of
  those same shots, and we now aim the way it does.
- **New units leaving their building:** a new unit with no rally point moves
  off the building's pad to a spot a short way from the building and waits there, as in
  the original. A ship only picks a spot it can sail to, so ships no longer stay
  stuck on the pad of a yard by a small lake. The AI no longer builds a
  shipyard on a pond that does not reach the sea.
- **Flyers leaving the factory:** a new flyer with no rally point flies to a
  random spot on a circle around the factory, a few hundred pixels out and on
  any side of it, as in the original, instead of stopping a short way in front
  of it. A flyer with a rally point flies straight to it, with no stop on the
  way.
- **New units under Legion:** with Legion pathfinding, each new unit with no
  rally point takes a free spot of its own around the building, nearest the
  door first, on alternating sides and never in front of the door, so units
  from a busy Barracks do not queue in a column for one spot and block its
  door. When every spot is taken, they spread further out. Retail pathfinding
  is unchanged.
- **Game speed:** raising the speed to +10 now takes effect at once. A player
  or spectator whose game had paused for a moment (while it loaded, or after
  switching windows) could stay at about +4 for most of a minute.
- **Streaming on Intel and AMD (Linux):** YouTube streaming now uses the
  computer's own VA-API library (`libva`) and graphics driver for hardware
  encoding. The game used to carry an older copy of libva that could not start
  current drivers, such as Intel's on Fedora 44, and did not look in Fedora's
  RPM Fusion driver folders, so streams fell back to the CPU (**LIVE - CPU
  FALLBACK**) even where `vainfo` worked. Without libva the game still streams
  on the CPU as before; libva is not a package dependency. This changes nothing
  about who can play together.
- **YouTube streaming on macOS:** streams from a Mac now reach YouTube. Before,
  every upload stopped after a few frames: the panel went RECONNECTING and then
  FAILED, and only the local copy was saved. A bug in FFmpeg's macOS TLS code
  froze the upload for 5 seconds the first time it checked for a reply from
  YouTube; the built-in FFmpeg is now patched. When streaming fails, the panel
  now says why: the certificate was not trusted, the connection could not be
  made, or it kept dropping. A stream that stayed up for a minute gets three
  fresh retries when the connection drops, instead of sharing three retries
  across the whole stream.
- **Streaming from a large window:** a stream smaller than the game window now
  keeps up. Streaming a 7680x2160 window at MAX 3840 (3840x1080) and 60 FPS on
  Windows encoded only about 18 frames a second: the panel counted the rest as
  DROPPED, and the bitrate stayed near a third of the setting. Under Direct3D
  9, what Windows used to pick by default, capturing the window also cut the
  game itself from 240 FPS to about 30. The stream's resize now runs on several
  threads, Direct3D 9 and 11 shrink the frame on the graphics card as OpenGL
  already did, and the encoder no longer oversleeps on Windows. On the reporter's setup the stream
  now encodes all 60 frames a second at about 6,000 Kbps, and the game stays at
  240 FPS. This changes nothing about who can play together.
- **Hardware cursor while streaming:** streaming no longer switches the cursor
  to the one the game draws itself. The pointer stays the system's hardware
  cursor on your screen, and the stream draws the same cursor into the video:
  the same art, animation frame and size, at the pointer, scaled to the stream,
  clipped at the picture's edges and left out while the pointer is outside the
  window. With Hardware Cursor off nothing changes: the game draws the cursor and
  the stream captures it. This changes nothing about who can play together.
- **Which cursor is in use:** the Options screen now shows beside Hardware Cursor
  what the game gave the system, such as `OS CURSOR 64X100`, or
  `UNAVAILABLE - SOFTWARE` when the system refused that size and the game is
  drawing the cursor itself; the log says the same. On the Windows and macOS
  machines where the cursor was reported to feel like a software cursor, the
  hardware cursor was in use throughout, with nothing drawn by the game, so a
  report can now say which one ran.
- **Windows draws with OpenGL by default:** with the Renderer option on Auto,
  Windows now uses OpenGL, where some of the game's drawing shortcuts work,
  instead of Direct3D 9. Auto passes over Windows' software OpenGL (a machine
  with no graphics driver) and Microsoft's OpenGL compatibility layer and uses
  Direct3D 11 instead, then Direct3D 9. The Renderer row shows the pick, such
  as **Auto (OpenGL)**; choose **Direct3D 11** or **Direct3D 9** there if
  OpenGL misbehaves. OpenGL was faster on the test machine with 2,000 units on
  screen; with 10,000, Direct3D 11 was about 10% faster. Linux and macOS pick
  as before; their Auto row now names the pick too, such as **Auto (Metal)**.
- **Not compatible with 0.9.0:** the aiming and the way new units leave their
  building change the game itself, so 0.9.1 uses protocol 295 (replay format
  15 is unchanged). It does not play with 0.9.0 or any earlier version, nor
  their replays.
- **Upgrading:** a server and all of its players update to 0.9.1 together;
  public-server operators update `takserver` as well. Keep 0.9.0 (it stays on
  the releases page, or build it from its tag) to watch replays recorded
  before 0.9.1.

[Release notes](docs/release-0.9.1-notes.md) · [Validation report](docs/release-0.9.1-validation.md) · [Source at v0.9.1](https://github.com/pocketgeek/tak-engine/tree/v0.9.1)

## 0.9.0
*2026-10-24 · protocol 294 · replay format 15 · does not play with 0.8.14 or earlier, nor their replays*

- **AI and the Rolling Tower:** the shipped AI profiles misspell the Aramon
  Rolling Tower (`araseige`). The original game ignores the name, so the Retail
  AI built Rolling Towers without limit, and our levels (Easy to Absurd) never
  built one. Both now read either spelling as the Rolling Tower: the Retail AI
  keeps to the profile's limit (10 in `ai/default.txt`), and our levels build up
  to 10. Only the server runs the AI, so this changes nothing about who can play
  together.
- **Build sparkles:** the sparkles over a building going up now match the
  original. The ring sits on the edge of the building's footprint and holds the
  original number of sparkles; buildings whose unit file has no `bmcode` line
  were sized like mobile units, with a wider ring and a quarter of the sparkles.
  Rising sparkles on high ground or water also climb as far as they did in the
  original before they fade. The sparkles are display only, so this changes
  nothing about who can play together.
- **Gate doorways:** clicking the doorway of an open gate now works like
  clicking the ground, as in the original: it selects nothing, and a right-click
  moves the selected units through or into the opening instead of guarding,
  repairing or attacking the gate. A closed gate, the gate's posts and units
  standing in the doorway are still selected as before. This is a mouse change
  only, so it changes nothing about who can play together.
- **Area clear (right-drag box) and reclaim:** every selected builder that can
  reclaim now takes the order, each working through the same box, as in the
  original game; before, only the first one did. The same goes for a
  right-click or **CLEAR** click on a single tree or rock.
- **Area clear no longer skips trees:** where two of a map's trees, rocks or
  other objects overlap, the original game removes the earlier one when the map
  loads. We kept it, drawn and blocking but never picked by an area clear. It
  is now gone, as in the original, which can open small gaps in some forests
  (Inner Circle loses one tree; 481 of 1,360 maps checked lose some). Such an
  object no longer makes a sacred site or a lodestone deposit either.
- **Area clear under the fog:** trees and rocks on ground your side has
  explored are cleared even while you cannot see them, as in the original;
  wrecks still have to be in sight. The room's FOG OF WAR option counts:
  **EXPLORED** and **FULL VISION** make the whole map count as explored.
- **Unreachable objects:** a builder now gives up on a tree or rock it cannot
  reach, and an area clear ends when the nearest object left is one it could
  not reach, as in the original. Before, the builder could keep trying forever.
- **Reclaim works as in the original:** a builder now walks right up to a tree,
  rock or wreck and works standing next to it; before, it started from as far
  away as its build distance. Each object takes as long as the original game
  gives it, whatever the builder: about 9 seconds for a big tree (it was about
  2), half a second for a rock or wreck, and builders on the same object no
  longer share the work. Reclaiming gives **no mana**, as in the original; it
  gave the object's value before. The object fades for about a second before
  it is gone and leaves no rubble. Area clears and the clearing a builder does
  before building take longer as a result.
- **Not compatible with 0.8.x:** these changes alter the game itself, so 0.9.0
  uses protocol 294 and replay format 15. It does not play with 0.8.14 or any
  earlier version, nor their replays.
- **Upgrading:** a server and all of its players update to 0.9.0 together;
  public-server operators update `takserver` as well. Keep an older engine
  (built from its tag) to watch replays recorded before 0.9.0.
- **Documentation:** the release history moved out of the README into this
  changelog; the README, quick start, user guide, map editor guide, server
  guide and streaming guide were rewritten and their pictures retaken.

[Release notes](docs/release-0.9.0-notes.md) · [Validation report](docs/release-0.9.0-validation.md) · [Source at v0.9.0](https://github.com/pocketgeek/tak-engine/tree/v0.9.0)

## 0.8.14
*2026-10-24 · protocol 293 · replay format 14 · plays with 0.8.12 and 0.8.13, and their replays*

- **Always-enabled mods, in folders:** `mods/cosmetic/always_enabled/<pack>/`
  (loaded in every game, campaigns included, never sent) and
  `mods/game/always_enabled/<pack>/` (applied in every game you host and sent
  like ticked game packs; never in campaigns or Crusades battles). The room
  shows them, locked, on **ALWAYS ON** lines.
- **No GAME MODS switch:** in multiplayer, as in single player, the host ticks
  game packs in the room. A room hosted by 0.8.12 or 0.8.13 with the switch off
  still plays without game packs.
- **Cartographer** edits ordinary game packs only; **TEST IN GAME** and **Test
  Map** name the always-enabled game packs that apply in the test game.
- **Upgrading:** a `.hpi` placed directly in `mods/cosmetic/` (such as
  `mods/cosmetic/click.hpi`) is no longer loaded. Move it into its own folder,
  for example `mods/cosmetic/always_enabled/click/click.hpi`. The saved GAME
  MODS choice is ignored.

[Release notes](docs/release-0.8.14-notes.md) · [Validation report](docs/release-0.8.14-validation.md) · [Source at v0.8.14](https://github.com/pocketgeek/tak-engine/tree/v0.8.14)

## 0.8.13
*2026-10-24 · protocol 293 · replay format 14 · plays with 0.8.12, and its replays*

- **No order text on screen:** an armed order shows only its cursor and its
  pressed command-panel button, as in the original game (no **ATTACK: CLICK
  TARGET** banner).
- **Cartographer starts with no map:** an empty **NO MAP OPEN** window; start
  from **File → New map** (Ctrl+N), **Open map** (Ctrl+O), **New AI mod**
  (Ctrl+Shift+N) or **Open AI mod** (Ctrl+Shift+O).

[Release notes](docs/release-0.8.13-notes.md) · [Validation report](docs/release-0.8.13-validation.md) · [Source at v0.8.13](https://github.com/pocketgeek/tak-engine/tree/v0.8.13)

## 0.8.12
*2026-10-23 · protocol 293 · replay format 14 · does not play with 0.8.11 or earlier*

- **AI profiles in the map editor:** **Scenario → AI Profiles...** (A) edits a
  map's AI profiles for the Retail AI and our six levels, with presets, a text
  view, checks, and saving to or opening from a game pack. Maps keep their
  `ai/` files when opened and saved.
- **AI mods without a map:** **File → New AI mod...** and **Open AI mod...**,
  with **TEST IN GAME...** on a map you pick.
- **One file for our six AI levels:** `ai/takengine.txt`, from a ticked game
  mod or the map, applied on top of each level's built-in profile.
- **Each map's own AI files:** two maps with an AI file of the same name each
  play their own.
- **Campaigns:** game speed can always be changed in a mission.
- **Flyer patrols:** an armed flyer told to move, then Shift-patrol, patrols
  instead of hovering at the point.
- **Upgrading:** the old `ai/d.txt` ... `ai/ad.txt` files are no longer read;
  Cartographer's **MERGE INTO TAK ENGINE...** moves one into
  `ai/takengine.txt`. AI level 6, DEFAULT in 0.8.11, is now shown as **RETAIL**.

[Release notes](docs/release-0.8.12-notes.md) · [Validation report](docs/release-0.8.12-validation.md) · [Source at v0.8.12](https://github.com/pocketgeek/tak-engine/tree/v0.8.12)

## 0.8.11
*2026-10-23 · protocol 290 · replay format 14 · does not play with 0.8.10 or earlier*

- **The original game's own AI** as a seventh level (shown as DEFAULT, renamed
  RETAIL in 0.8.12): the 1999 AI driven by `ai/default.txt`, ported routine by
  routine, quirks included. It plays weaker than Normal. New AI seats still
  start on Normal, and a dropped player is still replaced by Normal.
- **Campaign opponents** always use this AI, with each mission's own profile and
  timed mass attack.
- **Long build menus wrap** into extra rows instead of running under the
  command panel.
- **No blue production bar** over factories and conjurers, as in the original;
  the information panel shows progress.
- The other AI levels and the game itself play as in 0.8.10.

[Release notes](docs/release-0.8.11-notes.md) · [Validation report](docs/release-0.8.11-validation.md) · [Source at v0.8.11](https://github.com/pocketgeek/tak-engine/tree/v0.8.11)

## 0.8.10
*2026-10-22 · protocol 289 · replay format 14 · does not play with 0.8.9 or earlier*

- **Disconnects: the game waits, then a computer takes over.** Everyone sees a
  **WAITING FOR** *name* countdown; each player has 30 seconds for the whole
  game. A player back in time carries on where the game stopped; otherwise a
  Normal AI takes the seat with its units, buildings and mana.
- **Save a Local Copy** of your YouTube stream to
  `videos/TAK-YYYYMMDD-HHMMSS.mp4` (off by default).
- **Campaign seat:** you always play the mission's first player (fixes *Iron
  Plague* missions 22 and 24).
- **Cartographer Test Map on macOS** finds the game from the app, DMG or ZIP.
- **Retail target choice** scales a shooter's turn rate by the terrain under it.
- **Public servers:** Let's Encrypt renewal follows the CA's renewal window
  (ARI) and renews at once after a revocation; clients check revocation lists.

[Release notes](docs/release-0.8.10-notes.md) · [Validation report](docs/release-0.8.10-validation.md) · [Source at v0.8.10](https://github.com/pocketgeek/tak-engine/tree/v0.8.10)

## 0.8.9
*2026-10-21 · protocol 288 · replay format 14 · plays with 0.8.8, and its replays*

- **Tight build lines:** a Shift-drag line packs buildings edge to edge; the
  mouse wheel during the drag adds a gap of up to 12 cells (**GAP** label).
- **Deliberate drag boxes:** reclaim and lodestone boxes need a drag of about
  32 pixels; boxes stay on the ground you pressed when the view moves.
- **Chat box** always sits above the build menu.
- Neater setup and room screens; **GAME MODS VERIFIED** shows only when game
  packs are in play.

[Release notes](docs/release-0.8.9-notes.md) · [Validation report](docs/release-0.8.9-validation.md) · [Source at v0.8.9](https://github.com/pocketgeek/tak-engine/tree/v0.8.9)

## 0.8.8
*2026-10-21 · protocol 288 · replay format 14 · does not play with 0.8.7 or earlier*

- **Mods replace overrides:** packs go in `mods/cosmetic/<pack>/` (art and
  sound, each player's own) or `mods/game/<pack>/` (units and rules, picked by
  the host).
- **Power codes** match option: the original cheat codes (+ATM, +ManaMe,
  +DoubleShot, +HalfShot, +LOS, +Mapping, +NowISee, +Radar, +MakePoster) typed
  in chat. Off in campaigns and Crusades battles.
- **Readable chat:** bold text on a dark panel, **Chat Scale** (50–300%) and
  **Chat Position** (Left or Centered).
- **New interface font,** Noto Sans Bold; **UI Font: Classic** restores the
  earlier look.
- **Legion** rings along walls and through gates hold about as many attackers
  in reach as Retail's.
- Replay format 14 records the Power Codes option.
- **Upgrading:** the old `overrides/` folder is no longer read and nothing in it
  is moved: move your packs into `mods/` yourself and tick them again in the
  room.

[Release notes](docs/release-0.8.8-notes.md) · [Validation report](docs/release-0.8.8-validation.md) · [Source at v0.8.8](https://github.com/pocketgeek/tak-engine/tree/v0.8.8)

## 0.8.7
*2026-10-20 · protocol 284 · replay format 13 · does not play with 0.8.6 or earlier*

- **Pick your seat:** every open lobby slot has a **Move here** button; with
  **Start locations: By slot** it also picks where you start.
- **Legion:** formations keep their lanes through turns (an army of 100 rounds
  a corner clearly sooner than Retail), steadier attack, fight and guard rings,
  and bridges and one-unit corridors taken in turn.

[Release notes](docs/release-0.8.7-notes.md) · [Validation report](docs/release-0.8.7-validation.md) · [Source at v0.8.7](https://github.com/pocketgeek/tak-engine/tree/v0.8.7)

## 0.8.6
*2026-10-20 · protocol 281 · replay format 13 · does not play with 0.8.5 or earlier*

- **Clear shots:** a unit whose shots keep hitting a hill, water or scenery
  walks to a spot with a clear shot (not in the original game).
- **Attack cursor as in the original** over any enemy the weapon can hurt.
- **Voice activation:** voice chat's **Talk Mode** with an **Activation Level**
  slider; Push to Talk stays the default.
- **Legion** with armies of about 100: bridges taken in turn, squads that pass
  each other, full attack rings, formations that keep their files.

[Release notes](docs/release-0.8.6-notes.md) · [Validation report](docs/release-0.8.6-validation.md) · [Source at v0.8.6](https://github.com/pocketgeek/tak-engine/tree/v0.8.6)

## 0.8.5
*2026-10-18 · protocol 278 · replay format 13 · does not play with 0.8.4 or earlier*

- **Scattered Mana** for random maps: neutral spots across open ground, None to
  Max, by map area.
- **Sharp on Retina Macs:** **Native Retina Resolution** (on by default).
- **Tactical Dots** no longer draw fire, smoke and projectiles over the armies.
- **New defaults:** Tactical Dots on (zoom 0%), Smooth GUI Art and Smooth
  Movies on, Texture Filtering Everything; new **Screen Shake** option (off).
  UI Scale and F4 Scorecard Scale go down to 50%.
- Death cries heard as in the original; queued-building outlines leave the site
  once it is being built.
- **Upgrading:** a settings file you already have keeps its values; **Defaults**
  in Options applies the new ones.

[Release notes](docs/release-0.8.5-notes.md) · [Validation report](docs/release-0.8.5-validation.md) · [Source at v0.8.5](https://github.com/pocketgeek/tak-engine/tree/v0.8.5)

## 0.8.4
*2026-10-18 · protocol 277 · replay format 13 · does not play with 0.8.3 or earlier*

- **Voice chat:** hold Space to talk in multiplayer rooms and games; off by
  default, set up under **Settings → Voice Chat**, relayed by the server and
  never recorded.
- **Faster rendering:** a still army of 2,000 drew in 4.5 ms instead of 10.5 ms
  at 7680×2160 (measured on Linux with an NVIDIA GPU).
- **Legion takes much bigger armies:** one click reaches 10,000 units in the
  same tick ([Legion 10,000 report](docs/legion-10000-report.md)).
- **New AI level: Absurdly Defensive;** Defensive keeps building as fast as its
  income allows.
- **Replays are saved however a game ends,** crash or power cut included.
- **Short mana is shared:** every construction slows together, as in the
  original.

[Release notes](docs/release-0.8.4-notes.md) · [Validation report](docs/release-0.8.4-validation.md) · [Source at v0.8.4](https://github.com/pocketgeek/tak-engine/tree/v0.8.4)

## 0.8.3
*2026-10-15 · protocol 266 · replay format 13 · does not play with 0.8.2 or earlier*

- **New match options:** **Allow Spectators**, **Map Scenario** (a gold **S**
  marks maps with a scenario) and **Start Locations: By Slot**; the preview
  numbers every start.
- **Retail self-repair** rates (most units heal fully in about four minutes).
- **Retail air combat:** attack runs, no leading of hovering flyers, area
  damage measured in 3D.
- **Smarter computer players:** shared deposits, a lodestone when short of
  mana, the original per-type limits.
- **Map editor:** a **Blocked** tool, a shared **Game data folder** setting,
  borderless fullscreen and edge scrolling.
- Replay format 13 records the Map Scenario option.

[Release notes](docs/release-0.8.3-notes.md) · [Validation report](docs/release-0.8.3-validation.md) · [Source at v0.8.3](https://github.com/pocketgeek/tak-engine/tree/v0.8.3)

## 0.8.2
*2026-10-14 · protocol 257 · replay format 12 · does not play with 0.8.1 or earlier*

- **Legion: no more walking on the spot** near scattered trees (in the 0.8.1
  recording, walking on the spot fell from 857 to 34 unit-seconds). Retail
  plays as in 0.8.1.

[Release notes](docs/release-0.8.2-notes.md) · [Validation report](docs/release-0.8.2-validation.md) · [Source at v0.8.2](https://github.com/pocketgeek/tak-engine/tree/v0.8.2)

## 0.8.1
*2026-10-13 · protocol 256 · replay format 12 · plays with 0.8.0, and its replays*

- **The information bar shows only what has data.**
- **Less video memory for terrain:** peak use at 3840×2160 fell from 498 to
  309 MiB zoomed out.
- **GPU Memory Limit** option (512 MB to 4 GB or Unlimited; default 1.25 GB).
- **Streaming: Remember Key** in the system password store.
- Options dropdowns open upward when there is no room below.

[Release notes](docs/release-0.8.1-notes.md) · [Validation report](docs/release-0.8.1-validation.md) · [Source at v0.8.1](https://github.com/pocketgeek/tak-engine/tree/v0.8.1)

## 0.8.0
*2026-10-13 · protocol 256 · replay format 12 · does not play with 0.7.28 or earlier*

- **Signed and notarized on macOS:** the app and DMG open on the first launch.
- **The information bar shows the whole selection:** **N SELECTED**, one
  portrait per unit type with its count, **GROUP** / **FORMATION** numbers.
- **Texture Filtering** (Off, Units, Everything) replaces Bilinear Filtering.
- Factories no longer jam their door with large units; flying transports land
  at the drop point.
- **Cartographer:** symmetry tools, terrain copy and paste, a grouped terrain
  browser and a wider built-in font.
- **Upgrading:** a saved Bilinear Filtering choice loads as Off or Units.

[Release notes](docs/release-0.8.0-notes.md) · [Validation report](docs/release-0.8.0-validation.md) · [Source at v0.8.0](https://github.com/pocketgeek/tak-engine/tree/v0.8.0)

## 0.7.28
*2026-10-11 · protocol 253 · replay format 12 · does not play with 0.7.27 or earlier*

- **Units fire as in the original:** the round-robin target sweep, firing on
  the move, armed builders defending themselves, and retaliation within a leash.
- **Original game speed:** −9 (0.1×) to +10 (2×), host-only with Allow speed
  change.
- **Blocked building sites** retried for about ten seconds when only units block
  them.
- **Better AI defence** and a Monarch that fights back.
- **Legion plan W0–W9:** one order per click, attack rings, flyers that stay
  with the army.
- Steadier frame pacing, VSync pacing that fixes macOS stutter, and **Ignore
  Windows Display Scaling** on Windows.

[Release notes](docs/release-0.7.28-notes.md) · [Validation report](docs/release-0.7.28-validation.md) · [Source at v0.7.28](https://github.com/pocketgeek/tak-engine/tree/v0.7.28)

## 0.7.27
*2026-10-07 · protocol 238 · replay format 11 · does not play with 0.7.26 or earlier*

- **Faster large battles:** an order-heavy Legion battle went from about 1.1×
  to 1.6× real time in exploratory replays; Retail movement about 16% cheaper.
- **Tactical Dots** option: far zoomed out, units are drawn as player-coloured
  dots.
- **Renderer** option: Auto or any backend SDL offers, with fallback to Auto.
- Linux and Windows builds report their real build id.

[Release notes](docs/release-0.7.27-notes.md) · [Validation report](docs/release-0.7.27-validation.md) · [Source at v0.7.27](https://github.com/pocketgeek/tak-engine/tree/v0.7.27)

## 0.7.26
*2026-10-07 · protocol 237 · replay format 11 · does not play with 0.7.25 or earlier*

- **Legion flyers stay with their formation,** holding stations over the ground
  units.
- **Retail grouped flyers** follow the original's group checks, including its
  1-in-4 chance to drop orders.

[Release notes](docs/release-0.7.26-notes.md) · [Validation report](docs/release-0.7.26-validation.md) · [Source at v0.7.26](https://github.com/pocketgeek/tak-engine/tree/v0.7.26)

## 0.7.25
*2026-10-06 · protocol 236 · replay format 11 · does not play with 0.7.23 or earlier*

- **Two pathfinding choices:** Retail (the default) and experimental **Legion**,
  which plans one route per army.
- **Closer to retail movement:** flyer landing sites, group pacing and
  route-search budgets.
- **Shift queues every order,** patrols included.
- **One graphics filter, as in retail:** **Bilinear Filtering** (off by default)
  replaces the AA options.
- First retail route search for a new unit shape about 9–14 times faster.
- **Extra Mana Spots** (0–6 per player) and `takserver --status` JSON.
- **Upgrading:** the experimental Retail+, Flowfield and Cooperative modes were
  removed; a saved choice of one falls back to Retail.

[Release notes](docs/release-0.7.25-notes.md) · [Validation report](docs/release-0.7.25-validation.md) · [Source at v0.7.25](https://github.com/pocketgeek/tak-engine/tree/v0.7.25)

## 0.7.24
*Prepared but never published; its surviving changes shipped in 0.7.25.*

[Release notes](docs/release-0.7.24-notes.md) · [Validation report](docs/release-0.7.24-validation.md)

## 0.7.23
*2026-10-04 · protocol 221 · replay format 11 · does not play with 0.7.22 or earlier*

- Experimental **Flowfield** pathfinding beside Retail (removed in 0.7.25).
- **Lodestone area jobs:** drag a lodestone box to build on every eligible mana
  spot; patrolling builders repair nearby allies.
- **Defensive AI** meets threats near its base, then recalls its army.
- **Named generated maps** and themed layouts: Aramon Riverlands, Taros Maze,
  Veruna Ports, Zhon Clearings, Creon Highlands.
- **F4 Scorecard Scale,** team rows in F4, **Shift+Z** retail zoom, live speeds
  up to 8× (replaced by the original's 2× in 0.7.28).
- World sounds fade with distance outside the view; all lodestones cast shadows.
- **Linux ARM64, openSUSE Leap and Slackware64 packages.**

[Release notes](docs/release-0.7.23-notes.md) · [Validation report](docs/release-0.7.23-validation.md) · [Source at v0.7.23](https://github.com/pocketgeek/tak-engine/tree/v0.7.23)

## 0.7.22
*2026-10-02 · protocol 213 (unchanged from 0.7.21) · replay format 10*

- Terrain AA and Model AA options (replaced by one filter in 0.7.25).
- **Server hardening:** bounded map parsing, memory admission, command budgets,
  ACME retry recovery.
- **Linux service setup:** packaged systemd service and configuration
  templates.
- Builders clear several reclaimable obstructions from a site.
- Signed Windows executables and installer.

[Release notes](docs/release-0.7.22-notes.md) · [Validation report](docs/release-0.7.22-validation.md) · [Source at v0.7.22](https://github.com/pocketgeek/tak-engine/tree/v0.7.22)

## 0.7.21
*2026-10-01 · protocol 213 · replay format 10 · does not play with 0.7.20 or earlier (still plays 0.7.20 replays)*

- **Selectable override packs** in `overrides/<pack>/` folders (replaced by
  `mods/` in 0.8.8).
- **Clearer lobby:** game-create rules shown read-only.
- Each running match has its own simulation worker on the server.
- **Public-server hardening:** verified TLS, authenticated accounts, upload
  limits, built-in Let's Encrypt; players can type just a server name.

[Release notes](docs/release-0.7.21-notes.md) · [Validation report](docs/release-0.7.21-validation.md) · [Source at v0.7.21](https://github.com/pocketgeek/tak-engine/tree/v0.7.21)

## 0.7.20
*2026-10-01 · protocol 212 · replay format 9 · does not play with 0.7.19 or earlier*

- **Naval AI** for all five sides in both balances: coastal production, usable
  shipyard launch routes and smaller fleets.

[Release notes](docs/release-0.7.20-notes.md) · [Validation report](docs/release-0.7.20-validation.md) · [Source at v0.7.20](https://github.com/pocketgeek/tak-engine/tree/v0.7.20)

## 0.7.19
*2026-10-01 · protocol 211 (unchanged from 0.7.18) · replay format 9*

- **Native Windows ARM64** portable ZIPs.
- **Native Intel macOS** packages; both macOS builds need macOS 14 or later.

[Release notes](docs/release-0.7.19-notes.md) · [Validation report](docs/release-0.7.19-validation.md) · [Source at v0.7.19](https://github.com/pocketgeek/tak-engine/tree/v0.7.19)

## 0.7.18
*2026-10-01 · protocol 211 · replay format 9 · does not play with 0.7.17 or earlier*

- **Darien Crusades service:** allegiance, territory browsing, matchmaking,
  tactical battles, verified history and replays, on servers that enable it.
- **Crusades balance corrections** from the retail data.
- Veruna establishes fleets; converters reject targets they cannot capture.
- Victory opens results after three seconds.

[Release notes](docs/release-0.7.18-notes.md) · [Validation report](docs/release-0.7.18-validation.md) · [Source at v0.7.18](https://github.com/pocketgeek/tak-engine/tree/v0.7.18)

## 0.7.17
*2026-09-30 · protocol 203 (unchanged from 0.7.16) · replay format 9*

- Right-click cycles settings backward.
- Game-create choices and random-map recipes are remembered (not passwords).
- A local 24-hour clock in the stats panel.

[Release notes](docs/release-0.7.17-notes.md) · [Validation report](docs/release-0.7.17-validation.md) · [Source at v0.7.17](https://github.com/pocketgeek/tak-engine/tree/v0.7.17)

## 0.7.16
*2026-09-30 · protocol 203 · replay format 9 · does not play with 0.7.15 or earlier*

- **Larger generated maps** up to 64×64, with ruins round every mana spot.
- **Gentler Easy AI;** Passive renamed **Defensive**.
- Conversion targeting and production exits fixed; results use the F4 emblems.

[Release notes](docs/release-0.7.16-notes.md) · [Validation report](docs/release-0.7.16-validation.md) · [Source at v0.7.16](https://github.com/pocketgeek/tak-engine/tree/v0.7.16)

## 0.7.15
*2026-09-29 · protocol 201 · replay format 9 · does not play with 0.7.14 or earlier*

- Expired unit records are removed from the simulation, so long games stay
  fast (a synthetic test went from 6.26 ms to 0.022 ms per tick and checksum).

[Release notes](docs/release-0.7.15-notes.md) · [Validation report](docs/release-0.7.15-validation.md) · [Source at v0.7.15](https://github.com/pocketgeek/tak-engine/tree/v0.7.15)

## 0.7.14
*2026-09-29 · protocol 200 · replay format 9 · does not play with 0.7.13 or earlier*

- **Authored scenarios:** Use Only restrictions, neutral placements,
  armour/weapon and veteran defaults, per-player victory and defeat.
- **Retail trigger behaviour** for rule ownership, timing and selectors.
- Authored names in game (31-byte limit); Display gameclock works.
- **Cartographer:** browsers, undo/redo, recovery, background saving, map
  checks, regions and rules, F6 model inspector, F5 Test Map.

[Release notes](docs/release-0.7.14-notes.md) · [Validation report](docs/release-0.7.14-validation.md) · [Source at v0.7.14](https://github.com/pocketgeek/tak-engine/tree/v0.7.14)

## 0.7.13
*2026-09-29 · protocol 198 · replay format 9 · does not play with 0.7.12 or earlier*

- **Cartographer desktop workflow:** launchers, recent maps, searchable
  browsers, background KMP saving, recovery snapshots, map checks, named
  regions, objective templates and an F5 Test Map handoff.
- Diplomacy hides gifting and mana sharing for non-allies.

[Release notes](docs/release-0.7.13-notes.md) · [Validation report](docs/release-0.7.13-validation.md) · [Source at v0.7.13](https://github.com/pocketgeek/tak-engine/tree/v0.7.13)

## 0.7.12
*2026-09-29 · protocol 195 · replay format 9 · does not play with 0.7.11 or earlier*

- **Diplomacy on D:** unit gifting, outgoing mana sharing and chat recipients.
- Hard and Absurd expand earlier.
- Memory and resource fixes.

[Release notes](docs/release-0.7.12-notes.md) · [Validation report](docs/release-0.7.12-validation.md) · [Source at v0.7.12](https://github.com/pocketgeek/tak-engine/tree/v0.7.12)

## 0.7.11
*2026-09-29 · protocol 194 (unchanged from 0.7.10) · replay format 9*

- **Defensive AI** keeps growing near home without attacking.
- **Hard and Absurd** keep expanding past the former ceilings.
- Zhon producers build an opening army.

[Release notes](docs/release-0.7.11-notes.md) · [Validation report](docs/release-0.7.11-validation.md) · [Source at v0.7.11](https://github.com/pocketgeek/tak-engine/tree/v0.7.11)

## 0.7.10
*2026-09-28 · protocol 194 · replay format 9 · does not play with 0.7.9 or earlier*

- **Large-map performance:** terrain and fog reuse, cheaper distant models,
  shadows, pathfinding and collision grid.
- **Random maps** with validated routes and balanced resources, saved on every
  participant.
- **Automatic map sharing** in multiplayer.
- **Allied unit gifting** (Alt+G; moved to the D screen in 0.7.12), surplus
  mana to the emptiest ally first.
- **Options → Player Name;** unit limit and Allow Speed Change at game creation.

[Validation report](docs/release-0.7.10-validation.md) · [Source at v0.7.10](https://github.com/pocketgeek/tak-engine/tree/v0.7.10)

## 0.7.9
*2026-09-27 · protocol 189 (unchanged from 0.7.8) · replay format 8*

- Windows: map-list crash with accented filenames fixed; no console windows.
- NVIDIA statistics through NVML; Windows AMD/Intel GPU statistics.
- The retail F4 score table.
- Double Sight/Radar chosen at game setup.

[Validation report](docs/release-0.7.9-validation.md) · [Source at v0.7.9](https://github.com/pocketgeek/tak-engine/tree/v0.7.9)

## 0.7.8
*2026-09-27 · protocol 189 · replay format 8 · does not play with 0.7.7 or earlier*

- Explosion corpses and scripted death timing restored.
- Building palette shading and scripted shadow flags, as in retail Glide.
- A streaming shutdown race fixed.

[Validation report](docs/release-0.7.8-validation.md) · [Source at v0.7.8](https://github.com/pocketgeek/tak-engine/tree/v0.7.8)

## 0.7.7
*2026-09-27 · protocol 187 · replay format 8 · does not play with 0.7.6 or earlier*

- Builders clear trees and rocks from building sites.
- **In-place lodestone upgrades** for all five sides.
- Building ruins as authored; explored terrain stays mapped under fog;
  **Double Sight/Radar.**
- Defeated players go to results without pausing the others.

[Validation report](docs/release-0.7.7-validation.md) · [Source at v0.7.7](https://github.com/pocketgeek/tak-engine/tree/v0.7.7)

## 0.7.6
*2026-09-26 · protocol 184 (unchanged from 0.7.5) · replay format 7*

- Title and lobby music in YouTube streams.
- The retail monarch-under-attack warning.
- Group recall: one number-key press selects, a second tracks.

[Validation report](docs/release-0.7.6-validation.md) · [Source at v0.7.6](https://github.com/pocketgeek/tak-engine/tree/v0.7.6)

## 0.7.5
*2026-09-26 · protocol 184 · replay format 7 · does not play with 0.7.1 or earlier*

- **Built-in YouTube streaming** (F9): Max 3840 / 60 FPS defaults, hardware
  encoding with CPU fallback, VSync-aware pacing.
- Clearer performance statistics (Real Time, Game Time, client and server CPU,
  GPU use).
- Campaign presentation and objective fixes; Sea Fort placement fixed.

[Source at v0.7.5](https://github.com/pocketgeek/tak-engine/tree/v0.7.5)

## 0.7.1
*2026-09-26 · protocol 179 · replay format 7 · does not play with 0.7.0 or earlier*

- One animated shadow silhouette per unit (960 mixed units: 110 to about 162
  FPS in local tests).
- Flyers no longer land on other aircraft.
- Transport pickup and unload, and animation lifecycles, corrected.

[Source at v0.7.1](https://github.com/pocketgeek/tak-engine/tree/v0.7.1)

## 0.7.0
*2026-09-21 · protocol 177 · replay format 7 · does not play with 0.6.9 or earlier*

- **Naval combat fixes:** ships fire across open water and shorelines; shore
  defenders can hit ships.

[Source at v0.7.0](https://github.com/pocketgeek/tak-engine/tree/v0.7.0)

## 0.6.9
*2026-09-20 · protocol 176 · replay format 7 · does not play with 0.6.8 or earlier*

- Better ground, boat and flyer movement; flyers attack after landing.
- Better AI force building.
- Lobby limit of 2,000 units per player; Benchmark targets up to 16,000 units.
- A multiplayer desync fixed.

[Source at v0.6.9](https://github.com/pocketgeek/tak-engine/tree/v0.6.9)

## 0.6.8
*2026-09-15 · protocol 80 · replay format 6*

- Every build is fully static (no runtime libraries beyond the system's).
- The Monarch no longer starts inside a cliff.
- An honest simulation-speed readout.

[Source at v0.6.8](https://github.com/pocketgeek/tak-engine/tree/v0.6.8)

## 0.6.7
*2026-09-15 · protocol 80 · replay format 6*

- Crowds reach their goals instead of giving up.
- Battle stutter removed: over-budget ticks on an 8-AI benchmark fell from 148
  to 1.
- Software-renderer crash with anti-aliasing fixed.

[Source at v0.6.7](https://github.com/pocketgeek/tak-engine/tree/v0.6.7)

## 0.6.6
*2026-09-14 · protocol 80 · replay format 6*

- Two desyncs fixed (map features for display were blocking the simulation's
  grid; a spectator whose game ended froze the referee).
- Rejoin, spectate and replay fixes.

[Source at v0.6.6](https://github.com/pocketgeek/tak-engine/tree/v0.6.6)

## 0.6.5
*2026-09-13 · protocol 70*

- **Pathfinding rebuilt on the original game's algorithm.**
- **Shadows as the Glide renderer drew them;** Smooth GUI Art and Smooth
  Movies options.
- The fog pass runs off the simulation thread.

[Source at v0.6.5](https://github.com/pocketgeek/tak-engine/tree/v0.6.5)

## 0.6.0
*2026-09-12 · protocol 65*

- **Cartographer,** a clean-room port of the original map editor.
- **Multiplayer accounts** with SCRAM-SHA-256 sign-in.
- **One order queue** for every mobile unit, with the retail order line.
- Weapons, unit footprints and campaign scripting re-derived from the original.

[Source at v0.6.0](https://github.com/pocketgeek/tak-engine/tree/v0.6.0)

## 0.5.8
*2026-09-10 · protocol 35*

- The full retail animation audit: death cries, gaits, debris, turrets.
- Roads, burning forests and the complete retail corpse model.
- Terrain drawn from per-section textures.

[Source at v0.5.8](https://github.com/pocketgeek/tak-engine/tree/v0.5.8)

## 0.5.7
*2026-09-10 · protocol 28*

- Retail melee range by footprint.
- Generated coastlines, ship animation, the macOS app launching without a
  Terminal window.

[Source at v0.5.7](https://github.com/pocketgeek/tak-engine/tree/v0.5.7)

## 0.5.6
*2026-09-09 · protocol 27*

- **Random map generator** with a live preview.
- Water drawn the retail way.
- Per-distribution Linux packages, a Windows installer and a macOS DMG.
- A server list on the multiplayer connect screen.

[Source at v0.5.6](https://github.com/pocketgeek/tak-engine/tree/v0.5.6)

## 0.5.5
*2026-09-09 · protocol 26*

- **Benchmark** (Settings → Benchmark).
- First-run game data folder picker.
- A Settings menu; explored-terrain fog option.

[Source at v0.5.5](https://github.com/pocketgeek/tak-engine/tree/v0.5.5)

## 0.5.0
*2026-09-09 · protocol 22*

- Statically linked libraries on every platform.
- The simulation runs on its own thread, apart from drawing.

[Source at v0.5.0](https://github.com/pocketgeek/tak-engine/tree/v0.5.0)

## 0.4.1
*2026-09-08 · protocol 22*

- Bink door videos decoded by a built-in FFmpeg.
- Sound device choice and live switching.

[Source at v0.4.1](https://github.com/pocketgeek/tak-engine/tree/v0.4.1)

## 0.4.0
*2026-09-08 · protocol 22*

- Servers run several games in parallel.
- Single-player spectating of AI games; **Monarch Expendable** option.
- Rebindable hotkeys.

[Source at v0.4.0](https://github.com/pocketgeek/tak-engine/tree/v0.4.0)

## 0.3.0
*2026-09-07 · protocol 20*

- Five AI levels (Passive, Easy, Normal, Hard, Absurd).
- Control groups and formations.
- Campaign progress tracking, with no locked missions.
- A large performance pass.

[Source at v0.3.0](https://github.com/pocketgeek/tak-engine/tree/v0.3.0)

## 0.2.1
*2026-09-07 · protocol 17*

- Windows build fix; no gameplay changes.

[Source at v0.2.1](https://github.com/pocketgeek/tak-engine/tree/v0.2.1)

## 0.2.0
*2026-09-07 · protocol 17*

- **The campaigns:** *Book of Darien* and *Iron Plague*, with movies,
  briefings, objectives and the alternate ending.

[Source at v0.2.0](https://github.com/pocketgeek/tak-engine/tree/v0.2.0)

## 0.1.0
*2026-09-07 · protocol 16*

- The retail front end, animated cursors and build and reclaim effects.

[Source at v0.1.0](https://github.com/pocketgeek/tak-engine/tree/v0.1.0)

## 0.0.1
*2026-09-06 · protocol 16*

- First versioned build: skirmish against the AI, client-server multiplayer,
  the retail HUD, and Linux, Windows and macOS builds.

[Source at v0.0.1](https://github.com/pocketgeek/tak-engine/tree/v0.0.1)
