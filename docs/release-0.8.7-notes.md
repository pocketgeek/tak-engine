# TAK Engine 0.8.7

0.8.7 follows 0.8.6. Players can now pick their own seat in the lobby, and
Legion pathfinding had two more rounds of tuning for armies of about 100 units:
formations keep their lanes through turns and merge round corners faster, and
attack, fight and guard rings round a still target stop creeping. The Legion
work alters the game simulation and the seat move adds a lobby message, so the
network protocol moves from 281 to **284**: **0.8.7 cannot play with 0.8.6 or
earlier, and cannot play their replays.** A server and all of its players must
update to 0.8.7 together. Retail pathfinding plays exactly as in 0.8.6.

New to the game? The [quick-start controls reference](quick-start.md) lists the
mouse actions, hotkeys and orders on one page, with pictures.

## Lobby: Move here

- **Pick your seat.** In a single-player or multiplayer room, every open slot
  row has a **Move here** button. Click it and you take that slot, keeping your
  side, colour, team and ready state; the slot you left opens.
- **Pick your start.** With **Start locations: By slot**, slot N starts at the
  map's position N, so moving picks where you begin. With **Fixed** it changes
  your place in the order; with **Random** it changes nothing about the start.
- **Everyone can move, the host too**, until the game starts. Only open slots
  inside the map's player count can be taken: closed, AI and taken slots
  cannot. Campaign missions and Crusades battles keep the seats they give.
- **The host's slot buttons no longer touch seated players.** The host can
  still open, close and fill empty slots with AIs and kick players, but cannot
  turn a player's slot into an AI or a closed slot, or make an empty slot a
  player. Before, a click that crossed a seat move could leave a ready
  "player" with nobody in the seat, or turn a player's new seat into an AI.
- See [your first skirmish](user-guide.md#your-first-skirmish) and
  [multiplayer](user-guide.md#multiplayer) in the user guide.

## Legion pathfinding: armies of about 100, rounds 3 and 4

As in 0.8.6, the target was about 100 units at a time, in the places Legion was
known to handle worse than Retail. Retail pathfinding is unchanged by all of it.

- **Lanes through turns.** A formation rounding a corner or a wall end keeps
  its units side by side, each in its own lane, instead of crossing over each
  other's paths. Very large crowds (over 256 units at one turn) turn as
  before, and a unit whose lane is blocked for a few seconds gives it up and
  goes on by itself.
- **Merging round corners.** Where a crowd funnels round a corner into a file,
  units walking past one that has waited a while now move over a lane to let
  it in, so the file becomes several lanes. In the test fixtures a 100-unit
  formation gets 90% of its units round a corner about 5-10% sooner than in
  0.8.6, well ahead of Retail.
- **Rings round a still target.** Attackers round a target that stands still
  no longer creep along the ring; they stand and fire. Spots that are closed
  off are skipped, spare attackers take the outer places, and a target walled
  in on every side is fired on from the nearest spots against its walls.
- **Deeper rings along walls.** Units that find no place at the front of a ring
  along a wall wait just behind it, walking round into the space behind the
  target, instead of standing back, and step in when they can fire.
- **Fight and guard orders.** Units on a fight (attack-move) or guard order
  round a target that stands still also stop creeping, and walk round their
  friends at full speed. In the worst test case the time units spent creeping
  fell from about 32,000 unit-ticks to about 4.
- **Bridges and narrow crossings.** The time each group is let across grows
  with the length of the crossing, and a small group (fewer than 8 units) waits
  its turn instead of pushing in among a big one. Two armies of about 100
  meeting at a bridge no longer jam in the tests.
- **Dead-end corridors.** Groups sent into a corridor one unit wide are let in
  one at a time and settle along it, instead of jamming at its mouth, and their
  orders complete.

See [Legion](user-guide.md#legion) in the user guide, and
[legion-pathfinding.md](legion-pathfinding.md) for the measurements.

## For contributors

- New tests: `server_seat_move` (`tools/server_seat_move_test.py`: moves,
  refusals and the two stale host edits, which fail without the fix), and the
  Legion ctests `legion_ribbon`, `legion_slotwedge`, `legion_cornermerge`,
  `legion_passage_green`, `legion_passage_small`, `legion_attackflush`,
  `legion_ringstill`, `legion_ringwait`, `legion_ringclosed`,
  `legion_ringblind`, `legion_ringnosight`, `legion_ringpocket`,
  `legion_ringshadow`, `legion_ringshuffle`, `legion_ringroute`,
  `legion_ringroutespeed`, `legion_fightstill`, `legion_fightshuffle`,
  `legion_guardstill`, `legion_funnelmouth`, `legion_funneltie` and
  `legion_mixedrandom` (23 new tests in all). `replay_test` pins protocol 284 and refuses 283, 282
  and 281.
- New fixtures: `tools/scenarios/attackring-fight-diag-60.scn`,
  `attackring-gatehouse-40.scn`, `taperbridge-small.scn` and
  `sweeps/attackring-r60-64.scn`. The Legion scenario baseline
  (`tools/scenarios/baseline.json`), the navigation checkpoints and the Legion
  cost baseline were retaken for rounds 1 to 4.
- Wire: `Msg::MoveSeat` (client to server, one byte: the slot). The server
  answers the mover with `JoinResult` (its new slot), then broadcasts
  `LobbyState`; anything it refuses is ignored. See
  [multiplayer-design.md](multiplayer-design.md).
- The `--mpai` Inner Circle seed 1 hashes are unchanged from 0.8.6: Retail
  60 s `b353410003e98dd5`, 300 s `a3d953803a064191`; Legion 60 s
  `0825aeb9e8139203`, 300 s `ce5c4ba7562e53b9` (the Legion changes are not met
  in that game), on Linux, Windows and macOS.

## Known limits

- **Legion units still stand and wait more than Retail's**, in crowds, at
  corners and in rings, though they usually arrive sooner. This is cosmetic.
- **Legion rings along walls and gatehouses** hold fewer attackers within reach
  than Retail's, so fewer fire at once. This is the next round's work.
- In a few test fixtures a Legion group still arrives a little later (under
  5%) than in Retail, and a group crossing a firing ring arrives a little later
  than in Retail.
- Carried over from 0.8.6: **Legion bridges are still a lottery at about 100
  units.** Two armies meeting at a narrow, tapering bridge can still jam in its
  mouth and take far longer than Retail to cross (in the test fixtures,
  `taperbridge-100` finishes in about 1.8 times Retail's time and
  `taperbridge-small` in about 1.2 times).
- Carried over from 0.8.6: a stationary unit choosing its own targets can keep
  ignoring an enemy it once found blocked; the clear-shot search looks only
  about twelve cells round the unit. The Legion 10,000-unit workloads still
  struggle with congestion and the largest run over the tick budget; in
  mixed-10000 the flyers hover over a slow ground army. Moving the game window
  between a Retina and an ordinary display does not rescale UI Scale. In the
  remote test sweep, Retail `h-cramped` ends early because the idle seat starts
  with only its Monarch.

## Compatibility

Protocol **284** (0.8.6: 281), replay format **13**, generator version **9**,
campaign payload **4** and Crusades SQL schema **9** (all four unchanged).
Checked in source against `v0.8.6`: `kNetVersion` in `src/net/protocol.h`,
`kReplayFormat` in `src/net/replayhdr.h`, the map generator's `formatVer`
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` (`src/net/crusades.h`)
and `kSchemaVersion` (`src/server/crusades/store.cpp`).

- **Multiplayer.** A server rejects a client whose protocol differs from its
  own, so **0.8.7 clients cannot join 0.8.6 or older servers, and 0.8.6 or
  older clients cannot join a 0.8.7 server**. Update the server and every
  player together. Protocols 282 and 283 were never released.
- **Replays.** Playback requires the recording's protocol to equal the
  engine's, so 0.8.7 does not play replays recorded by 0.8.6 or earlier, and
  they do not play 0.8.7 replays, although the file format is unchanged. Keep
  an old copy of the engine to watch old recordings. Crusades battle replays
  follow the same rule.
- **Maps.** Saved `.kmp` maps and generated-map recipes (generator version 8
  or 9) carry over unchanged.
- **Settings.** A 0.8.6 settings file reads the same in 0.8.7 and keeps its
  values; there are no new settings.

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.8.7-validation.md).
