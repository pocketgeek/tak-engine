# TAK Engine 0.9.21

0.9.21 follows 0.9.20 one day later. It is a small release: Legion columns and Legion
formation flyers move better, the Benchmark measures against the clock with a choice of
game speed, two new sound options, and a handful of display fixes.

**Compatibility.** 0.9.21 uses network protocol **315** (0.9.20 used 312; 313 and 314
were never released on their own) and replay format **15**. **0.9.21 does not play with
0.9.20 or any earlier version, and it does not play their replays.** A server and all of
its players update together: a 0.9.20 server turns away 0.9.21 players, and a 0.9.21
server turns away 0.9.20 players, with a *protocol version mismatch* message. Keep a
0.9.20 copy (or build it from its tag) to watch 0.9.20 replays. In Retail pathfinding
games play as in 0.9.20; only Legion pathfinding and the Benchmark changed. The random
map generator (version 9) is unchanged, so a random map's ID makes the same map as before.

## Legion pathfinding

- **Columns keep their files round corridor corners.** A column turning from one narrow
  corridor into another used to cut the inner corner, every unit walking its own straight
  line from there, so the second corridor carried only one or two files along its inner
  wall. Each unit now keeps to the lane it walks in, counted from the inner wall, round
  the corner and down the next corridor, so the column wheels round at full width. On the
  Industrial Zone causeways a unit comes down the second causeway every 23 to 25 ticks
  instead of every 31.5.
- **Formation flyers circle over the front.** The flyers of a moving Legion formation
  were held to the ground army's pace behind their places and seemed to hover. Each now
  circles a small ring just ahead of its place, at half its own speed or one and a half
  times the ground's, so the wing swarms over the front of the formation, as the original
  game's flyers loiter over a moving group. Near the destination they form up on their
  places again and land as before.

## Benchmark

- **A run lasts 60 real seconds at any game speed**, and units spawn at the intensity's
  rate per real second. The server keeps the time and tells every copy of the game when
  each wave of units is due and when the run is over, so a computer that cannot keep up
  gets through less of the battle instead of taking longer, and the results show by how
  much. A replay of a run plays exactly as it ran.
- **A Speed choice**: **+10** (twice normal speed, the default), **Normal** or **+100**
  (eleven times normal speed, a stress test). The game remembers it, and the results list
  it. +100 is possible only in the Benchmark.
- **No title screen flash.** After you pick an intensity the Benchmark screen stays up
  until the battle loads. Picking a replay in Load Replay works the same way.
- "Extra Absurd" has lost its "+WTH".

## Sound

- **Sound voices** (Options, 8 to 256, default 128): how many sound effects can play at
  once. The original game played 32; with more, the sounds of a big battle are not cut
  off. Past the limit a new sound still replaces a less important one, as in the original
  game.
- **Even out sound levels** (Options, on by default): an effect recorded much quieter
  than the rest is raised toward the others, by at most 10 dB and never to clipping;
  louder ones are left alone. The expansion's Creon units' cries were recorded far
  quieter than everything else and were lost in a fight; the Creon Automaton's death
  cries now play about 5 dB louder. Off plays every sound at its own level, as the
  original game did. Both options explain themselves when you hover over them.

## Display

- **A flyer's group number, health bar and self-destruct countdown stay with it** in the
  air instead of on the ground beneath it.
- **Game speed above +10 shows as a level** (+100), not a multiplier, and the stats
  panel's **SIM** row starts at the speed asked for instead of climbing from zero at the
  start of a game.
- **With Shift held, a queued lodestone or area-clear box sits where you dragged it** on
  raised ground instead of sliding below it.

## Under the hood

- Protocol 313 is the Legion corner lanes, 314 the formation flyers, 315 the Benchmark's
  clock (`Cmd::BenchClock`, a command only the server sends, carried in the tick stream
  and the replay). The `--mpai` gates keep the 0.9.20 hashes; see the
  [validation report](release-0.9.21-validation.md).
- Packages ship `TITLE-FONT-LICENSE.txt`, the SIL Open Font License 1.1 notice for the
  Cinzel font the client embeds.
