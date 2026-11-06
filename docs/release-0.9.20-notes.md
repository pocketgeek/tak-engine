# TAK Engine 0.9.20

0.9.20 follows 0.9.19. It is the largest batch of original-game behaviour since 0.9.0:
groups and formations, the clicks and the cursor, how units pick their targets, how
units die and who gets the credit, the opening mana and every unit's mana pool,
abandoned building sites, bodies in water and the campaign missions all now work as
they did in the original game, each checked against the original program. Both Retail
and Legion pathfinding play differently from 0.9.19.

**Compatibility.** 0.9.20 uses network protocol **312** (0.9.19 used 311) and replay
format **15**. **0.9.20 does not play with 0.9.19 or any earlier version, and it does
not play their replays**, because almost every game now plays differently. A server and
all of its players update together: a 0.9.19 server turns away 0.9.20 players, and a
0.9.20 server turns away 0.9.19 players, with a *protocol version mismatch* message.
Keep a 0.9.19 copy (or build it from its tag) to watch 0.9.19 replays. The random map
generator (version 9) is unchanged, so a random map's ID makes the same map as before.

## Groups and formations

- **A group is a saved selection; a formation keeps together.** In Retail pathfinding a
  Ctrl group (Ctrl+number) no longer moves at its slowest unit's pace, as in the
  original game: only a formation (Alt+number) keeps together, waits for its stragglers
  and sends them back. The Retail computer player's attack waves and flyer squads keep
  together as formations, as the original game's do.
- **Flyers keep to their formation, whatever they are doing.** In Retail pathfinding a
  flyer left behind on patrol, in an air fight, hunting for a target or guarding flies
  back to its formation and carries on; one that has caught up while the formation is
  still moving circles near it, attacking what it finds, until the formation stops. An
  idle member that ends up away from its formation goes back by itself. Ground units in
  a formation do the same while guarding.
- **Troops riding a transport stay in their formation** (Retail): the formation keeps
  together around them where the transport carries them, but never waits for them.
- **Legion formations re-form.** In Legion pathfinding an idle unit in a formation that
  ends up out of its place (pushed, stepped aside, back from a fight or sent somewhere
  on its own) walks back to its own spot, and waits if something stands on it. The
  computer player's attack squads re-form and gather their stragglers the same way. A
  formation you stop on the way stays where it stopped, and units in a group are left
  where they are.
- **A factory's new unit joins the factory's group or formation when it leaves the
  factory**, as in the original game, not when it is started. A unit a builder conjures
  joins the builder's group when it is finished (that one is TAK Engine's own).

## Mana

- **A skirmish opens with 5000 mana**, the Monarch's whole store, as in the original
  game (it was 2800). A campaign mission opens each player with the store of the units
  it gives them (it was 500).
- **New units start with an empty mana pool.** A mage, priest, dragon or Monarch that is
  trained, built, conjured, raised or placed at the start of a game used to arrive full;
  it now starts empty and fills at its recharge rate once finished, so its costly spells,
  and a spy's cloak, wait until it has the mana.
- **A unit short of mana after a shot falls back to a cheaper weapon** at once (every
  Monarch and dragon), whether it is yours or the computer's, and mana no longer drops
  below zero.

## Building sites

- **An abandoned site fades away and gives its mana back.** A site nobody works on now
  stands for a second after its last work (eleven seconds if nobody ever worked on it),
  then fades at a steady pace set by the kind of building, and every bit that fades
  returns its share of the mana spent on it. It used to start fading the moment the
  builder stopped, and gave nothing back. A unit a factory or a Zhon builder is making
  does not fade while its maker holds on to it, even when the maker is paused or busy:
  it waits with its progress, as before. An unfinished building that changes sides
  never fades away, as in the original game; its new owner can still finish it.
- **A finished wall becomes part of the scenery**, as in the original game: it still
  blocks the way and can be smashed, but it is no longer a unit to select, count or
  target.
- **A factory given orders the moment it is finished opens and builds.** A Navy Yard,
  Barracks, Keep, Academy or other factory told to build a unit right as its own
  construction finished could stay shut and never build it. The computer player's
  first Navy Yard often did, so with the new 5000-mana opening its navy could fail to
  appear at all. A finished factory now switches on once it has settled in, a fraction
  of a second later.
- **Idle units make way at a factory's door.** Your idle units and guards standing in
  front of a land factory or a Navy Yard step aside while a new unit leaves, and one on
  the pad steps off it, so a crowd no longer stops production.

## Death, kills and veterans

- **Losing your Monarch loses your army.** With Monarch Expendable off, outside
  campaign missions, the moment a Monarch dies (or the transport carrying it is
  destroyed) every other unit and building of that player dies too, as in the original
  game. Before, the player was out but the army stayed on the field. Scenario games keep
  their own rules.
- **A transport's passengers die with it** and count as kills for whoever destroyed it.
  A loaded transport that changes sides loses its passengers too.
- **Kill credit and veterancy as in the original game.** Each kill earns a unit the
  experience the victim is worth, up to rank 10, so killing a stronger unit counts for
  more; towers and buildings become veterans too. A shot fired by a unit that has since
  died no longer steals the kill. A selected unit shows its kill count, and the rank mark
  on its picture follows the original game. A modded unit's `mogriumbounty` pays its
  killer's player.
- **Self-destruct keeps the original timing**: after the two-second countdown each unit
  goes up to half a second later, at random. The notice at the top of the screen counts
  the real seconds down (it always said "SELF-DESTRUCT IN 5...").
- **Deadly water hurts once a second**, ships and carried troops included.
- **Units that explode when they die burst when their death ends** (the Bomb Sprinkler
  about five seconds after it falls), and the burst is nobody's kill.
- **A unit that changes sides starts afresh**: empty mana pool, its type's stance, no
  reload, and it counts as a loss for its old owner.

## Bodies in water

Bodies and statues in water settle on the bottom and stay, as in the original game: a
sunken ship, a body in the shallows, and now a unit turned to stone or ice there, sinks
slowly to the sea bed and stays to be raised or reclaimed. Only one sinking towards the
deepest sea floor fades from view. Before, every body in water vanished after two and a
half seconds and a statue stood on the water for good. Whether a body at the water's
edge sinks depends on the ground right under it.

## Clicks, targets and orders

- **Clicks work as in the original game.** Orders go out when you let go of the button;
  every click picks the unit the cursor shows; each selected unit decides for itself
  what a right-click means. A loaded transport moves where you right-click and unloads
  with Alt+right-click. **Ctrl+click puts an order at the front** of a unit's queue, and
  Shift- or Ctrl-clicking a queued order again takes it back.
- **Units pick their own targets as in the original game**: only enemies your side can
  see, from the original's weighted draw, and units on standby, patrol or an
  attack-move take only enemies already in weapon range (hand-to-hand fighters still go
  after any enemy they see). Long-range units need a spotter; they no longer fire into
  the fog.
- **Fire at a point.** The original game's fire at a point is the new Attack Ground
  order: an armed Attack clicked on the ground fires at the spot, for ground units,
  towers and flyers.
- **Attack-move stops, fights and comes back** to its path, the original game's
  move-fight; Fight-move (F) gives it.
- **Allies share sight**, computer allies included.
- **Flyers on Hold Position stay at their post** instead of flying off after enemies.
- **A unit no longer ignores an enemy for good** after three of its shots hit a rock.

## Campaign missions

- **Mission 4 can be won.** Missions that end when the enemy is down to a few units
  counted *your* army instead, so the mission declared a victory once your own army was
  down to a few, and beating the enemy did not end it.
- The units a mission starts with stand on the original squares and face the original
  way; scripted moves go to the exact spots; siege units and towers scripted to bombard
  a spot fire at it; trigger areas fire once per unit and count carried troops; wave
  reinforcements (the drakes of mission 15) keep coming; a mission hears of a death
  when the death animation ends.

## Also

- Units, trees and effects are drawn at their own height, so units on a road beside
  lava are no longer drawn in the lava; ships ride the water surface.
- Trees and scenery knocked down play their falling animation first; reclaimed trees
  and rocks fade out as a white silhouette.
- Holding Shift shows a cloaked unit's decloak range. Area boxes are drawn yellow and
  target brackets are gone, as in the original game. A multiplayer Monarch shows its
  player's name.
- Auras keep working while a priest is paralysed or a Sacred Fire is switched off.
- Gates are built closed and switched off; a Pillar of Light and a Sacred Fire light up
  as in the original game.
- Sound and music mix at 44.1 kHz: the original's music plays in stereo at its own rate.
  The result screen and menu pages play their button clicks.
- Fedora 45 has its own x86-64 and ARM64 packages.

The [CHANGELOG](../CHANGELOG.md#0920) has the full list, with every detail.

## Known limits

- With Retail pathfinding Kirenna on an attack-move still often stops after one shot
  and waits: her own animation script holds her fire when she stops in the middle of a
  step, and the original game has the same race. With Legion pathfinding she keeps
  firing.
- A ship leaving a Navy Yard door that faces a shore still sails out slowly, as in the
  original game.
- All of 0.9.19's limits carry over (see
  [release-0.9.19-notes.md](release-0.9.19-notes.md#known-limits)).

Validation: [release-0.9.20-validation.md](release-0.9.20-validation.md).
