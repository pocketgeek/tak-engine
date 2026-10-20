# TAK Engine 0.8.6

0.8.6 follows 0.8.5. Units whose shots keep stopping on a hill, water or
indestructible scenery now walk to a spot with a clear shot, the attack cursor
follows the original game's rule, voice chat can send you whenever you speak,
and Legion pathfinding was tuned for armies of about 100 units in two rounds.
The clear-shot change and the Legion work alter the game simulation, so the
network protocol moves from 278 to **281**: **0.8.6 cannot play with 0.8.5 or
earlier, and cannot play their replays.** A server and all of its players must
update to 0.8.6 together.

## Combat: walking to a clear shot

In the original game a unit fires into whatever stops its shot: an archer
behind a hill crest, a rock or a row of indestructible scenery shoots it until
its orders change and never hurts its target. TAK Engine already checked most
ranged ground weapons for a clear shot across the movement grid, but that check
could not see a low crest that an arc clips, water, tall scenery that units can
walk through, or a corner.

- **Three blocked shots, then a short walk.** When three shots in a row stop on
  terrain, water or scenery the weapon cannot destroy, short of the target, a
  unit attacking that target walks to the nearest spot within about twelve
  cells that is in range, that it can reach and that has a clear shot, then
  attacks again. This works in Retail and Legion pathfinding alike.
- **Things it can destroy do not count.** Shots into a tree or other
  destructible scenery wear it down, as before.
- **Three walks per target** before it lands a hit; each hit gives it three
  more. If no spot is found it stays and fires on, as in the original, and
  looks again after three more blocked shots, in case the target has moved.
- **Who stays put.** Units on **Hold Position**, buildings, flyers and melee
  units never move for this.
- **Auto-targeting moves on.** A unit firing on its own (no attack order) stops
  shooting at a target it cannot reach with its shots and picks another one.
- This is a deliberate difference from the original game, listed in the user
  guide's [Differences](user-guide.md#differences-from-the-original-game) and
  explained under [Blocked shots](user-guide.md#blocked-shots).

## The attack cursor

- **Attack at any range, as in the original.** With a unit that can move
  selected, the pointer over an enemy shows the attack cursor whenever the
  unit's active weapon can hurt that enemy, however far away it is; the unit
  walks into range. Before, an archer or swordsman showed "too far" over every
  enemy out of reach.
- **"Too far"** now means no selected unit can attack that enemy from where it
  is: the weapon does no damage to it, the unit is never allowed to target that
  kind of unit, an anti-air weapon against a unit on the ground, a ground
  weapon against a flyer, a stoning or freezing weapon against an immune unit,
  or a tower or other building with the enemy out of range or inside its
  minimum range.
- **Right-click orders are unchanged**: every selected unit is ordered to
  attack. A tower keeps the order and fires once the enemy comes in range, and
  a selected factory sends the units it builds after that enemy.
- Only the pointer changed: the game and replays are not affected by it.

## Voice chat: Talk Mode

- **A new Talk Mode** on the Voice Chat page: **Push to Talk** (the default,
  hold Space as before) or **Voice Activation**, which sends you by itself
  whenever the **Mic Level** meter reaches the **Activation Level**. No key is
  needed, also while you type or the window is in the background.
- **Activation Level**, 0 to 100 (default 15), on the meter's own scale: a
  white tick on the meter marks it. Sending carries on for 0.4 seconds after
  you go quiet, so pauses between words are kept, and starts with the moment
  just before, so the first word is not cut. Raise it if background noise sets
  it off; lower it if your words are cut. 0 sends all the time.
- Under Voice Activation the microphone listens the whole time you are in a
  multiplayer room or game; the push-to-talk key does nothing.
- **Defaults** on the Voice Chat page returns to Push to Talk at level 15.
- Only the client changed; voice is still relayed by the server and never
  recorded. See [voice chat](user-guide.md#the-voice-chat-page).

## Legion pathfinding: armies of about 100

Legion's earlier work was judged on armies of thousands. This release looked
at about 100 units at a time, in the situations Legion was known to handle
worse than Retail. Retail pathfinding is unchanged by all of it.

- **Bridges and narrow passages.** A long bridge is now one passage with one
  turn at a time, so two armies meeting at it no longer both walk on and jam
  in the middle; units already let in keep their turn until they are through,
  and the last stragglers of a group ask for their turn too.
- **Head-on near the destination.** Two squads sent to each other's places
  now keep right and pass, instead of meeting face to face and walling each
  other in.
- **Leaving a crowd.** A group leaving a place where another army has just
  arrived gets all its units out: your own and your allies' settled or idle
  units step aside, and units going round them walk at full speed.
- **Attack rings.** A group attacking one target fills the whole ring round
  it instead of queueing behind the first arc, units waiting just short of
  reach step in until they can fire, a target in a walled-off place is
  surrounded from outside its walls instead of the order being dropped, and a
  group walking past another group's firing ring splits round both sides.
- **Wide gaps.** A formation of about 100 going through a wide gap (about 20
  cells) keeps its files instead of weaving.
- **Flowing round obstacles.** Formation units going round a rock, a wall end
  or a block of idle units keep their travel speed instead of creeping
  sideways.

See [Legion](user-guide.md#legion) in the user guide, and
[legion-pathfinding.md](legion-pathfinding.md) for the measurements.

## For contributors

- New tests: `blocked_shot` and `legion_blocked_shot` (`blocked_shot_test`:
  the shipped Archer against a Swordsman behind a passable ridge and a row of
  shipped `ZonFireD01` art, 11 of 11 shots short on Hold Position, 5 hits and a
  kill on Maneuver; and the case where the first search fails),
  `cursor_enemy` (the hover decision over shipped types, which fails 9 cases
  under the old range-for-all rule) with `tools/re/probe_cursor_enemy_hover.py`,
  the Legion ctests
  `legion_passage_admission`, `legion_passage_records`, `legion_passage_taper`,
  `legion_meetnear`, `legion_departure`, `legion_ringupkeep`,
  `legion_attackwalled`, `legion_ringcross`, `legion_pressin`,
  `legion_ringfill`, `legion_ringbend`, `legion_ringbend_pocket`,
  `legion_widegaplanes`, `legion_flowshuffle` and `legion_flowshuffle_idle`.
  `voice_test` (ctest `voice`) gains the voice-activation gate and page.
  `replay_test` pins protocol 281 and refuses 280, 279 and 278.
- New ~100-body fixtures in `tools/scenarios/legion100/` (written by
  `gen_legion100.py`) plus `tools/scenarios/taperbridge-100.scn` and `ringbend-pocket.scn`.
- The blocked-shot state (`Unit::blockedTarget` and its counters) is folded
  into `World::stateHash()` only once a blocked impact has been seen. Retail's
  behaviour and the deviation are written up in
  [retail-engine.md](retail-engine.md#blocked-shots-retail-fires-on-the-engine-walks-to-a-clear-shot-2026-10-19);
  the cursor rule (`0x4dd780`) is in the same file.
- The `--mpai` Inner Circle seed 1 hashes are unchanged from 0.8.5: Retail
  60 s `b353410003e98dd5`, 180 s `fe47c6bf443c5d37`; Legion 60 s
  `0825aeb9e8139203`, 180 s `1e87f71790f15023` (no blocked shot occurs in
  them, and the Legion changes are not met in them).

## Known limits

- **Blocked shots.** A unit standing still and choosing its own targets can
  keep ignoring an enemy it once found blocked, even after that enemy walks
  into the open; ordering it to attack, or moving it, clears this. The search
  looks only about twelve cells round the unit.
- **Legion bridges are still a lottery at about 100 units.** Two armies
  meeting at a bridge usually take turns now, but from some starting positions
  they still jam in its mouth (in the test fixtures, `taperbridge-100` at one
  offset: 19 of 100 arrive; `bridges-100` at another).
- **Legion attack rings** still stand thinner than Retail's along a wall, and
  a target walled in on every side is surrounded but never fired on (Retail's
  units pressed against a thin wall shoot through it). A ring forms more slowly
  than Retail's, and a group crossing a firing ring arrives a little later
  than in Retail.
- **Legion lane order.** The faster flow-around and detours make units swap
  places more often where two streams cross after a turn, and turn round more
  often, though still far less than in Retail.
- Carried over from 0.8.5: the Legion 10,000-unit workloads still struggle
  with congestion (crowds at corners and narrow passages, many unrelated
  destinations) and the largest of them run over the tick budget; in
  mixed-10000 the flyers hover over a slow ground army. Moving the game window
  between a Retina and an ordinary display does not rescale UI Scale. In the
  remote test sweep, Retail `h-cramped` ends early because the idle seat starts
  with only its Monarch.

## Compatibility

Protocol **281** (0.8.5: 278), replay format **13**, generator version **9**,
campaign payload **4** and Crusades SQL schema **9** (all four unchanged).
Checked in source against `v0.8.5`: `kNetVersion` in `src/net/protocol.h`,
`kReplayFormat` in `src/net/replayhdr.h`, the map generator's `formatVer`
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` (`src/net/crusades.h`)
and `kSchemaVersion` (`src/server/crusades/store.cpp`).

- **Multiplayer.** A server rejects a client whose protocol differs from its
  own, so **0.8.6 clients cannot join 0.8.5 or older servers, and 0.8.5 or
  older clients cannot join a 0.8.6 server**. Update the server and every
  player together. Protocols 279 and 280 were never released.
- **Replays.** Playback requires the recording's protocol to equal the
  engine's, so 0.8.6 does not play replays recorded by 0.8.5 or earlier, and
  they do not play 0.8.6 replays, although the file format is unchanged. Keep
  an old copy of the engine to watch old recordings. Crusades battle replays
  follow the same rule.
- **Maps.** Saved `.kmp` maps and generated-map recipes (generator version 8
  or 9) carry over unchanged.
- **Settings.** A 0.8.5 settings file reads the same in 0.8.6 and keeps its
  values; voice chat starts in Push to Talk. New keys: `voiceActivation` and
  `voiceActivationLevel`. 0.8.5 ignores them and drops them the next time it
  saves.

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.8.6-validation.md).
