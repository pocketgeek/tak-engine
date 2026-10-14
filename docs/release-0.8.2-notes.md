# TAK Engine 0.8.2

0.8.2 follows 0.8.1. It fixes Legion units walking on the spot. The fix changes
the Legion simulation, so the network protocol moves from 256 to **257**:
**0.8.2 cannot play with 0.8.0 or 0.8.1, and cannot play their replays.** A
server and all of its players must update to 0.8.2 together. Retail pathfinding,
the default, plays exactly as in 0.8.1.

- **Legion: no more walking on the spot.** A recording of a 0.8.1 Legion game
  (The Upper Humber, Crusades balance) showed Zhon units playing their walk on
  the spot for up to 72 seconds at a time after the 17-minute mark: 24 episodes
  of a second or more, 15 units. Each unit was stuck in a cycle of tiny steps
  or turns that never got it anywhere. Three changes, all in Legion only:
  - **A single tree or rock is not a wall end.** When a large group goes round
    the end of a wall it swings round in ranks. It used to take every scattered
    tree ahead for another wall end and turned left and right between them. Now
    a lone tree or rock, or a small clump (up to 7 × 7 cells, the size of the
    largest shipped tree or rock), is ignored for this, and the group walks past
    it. Units still path round and bump into the tree as before. A wood whose
    gaps are too narrow for the unit to pass, or a tree right next to a cliff, a
    building or the map edge, still counts as a wall.
  - **A side-step is not undone at once.** A blocked unit that stepped aside
    toward the way round no longer steps straight back into the cell it came
    from for 60 hold updates; it waits and tries its other ways out first.
  - **A unit that gets nowhere stands still.** Progress is now judged over
    8-tick windows by how far the unit really got, not by each step. A unit
    without progress stands, and its walk animation stops instead of playing on
    the spot. One that keeps stepping back and forth or turning left and right
    is held (no side-steps, no swinging round, no lane changes) until it is a
    body length clear or 64 ticks pass, so it tries another way round.

  Measured on the recording's own moments, cut out as test situations (seven
  cuts from 16:46 to 21:13, each run for 3,000 ticks in Legion at three start
  offsets): walking on the spot fell from 25,719 to 1,007 unit-ticks, that is
  from 857 to 34 unit-seconds. Over the 131 scenario fixtures (Legion, three
  start offsets) it fell from 88,443 to 1,380 unit-ticks. In a synthetic test world
  (112 layouts of scattered single trees after a wall end), units moving to and fro fell from
  50,029 to 249 member-ticks, every unit still arrived, and the last arrival
  came 3% earlier (median). Retail games are bit-identical to 0.8.1: every
  Retail record of the full scenario run and the Retail `--mpai` hashes are
  unchanged.

  Known cost, accepted: units cross between lanes more often (6,578 to 7,117
  lane crossings over every fixture and start offset, most of the rise in one narrow island
  fixture, `island-1x150`), and six Legion scenario keys fall below their Retail
  floor; both are recorded as accepted exceptions (cluster `LL-257`). See
  [the user guide](user-guide.md#legion) and the
  [Legion pathfinding notes](legion-pathfinding.md#livelock-the-081-walk-in-place-protocol-257)
  for every number and the alternatives that were tried and rejected.
- **For contributors.** New tests, each failing on 0.8.1: `legion_lonetrees`
  (48 bodies round a wall end into single 2×2 trees), `legion_slideback`
  (side-steps undoing each other), `legion_stillturn` (a standing unit must not
  request a turn), `legion_forestedge` (a strip of 2×2 trees with 1-cell gaps is
  still a wall) and `legion_still` (the new progress rule on the recording's
  motion and on genuine slow motion; `tools/legion_still_test.cpp`, now built by
  the Linux, Windows and macOS CI). The scenario observer gains `walk_net` /
  `walk_net_runs` (the replay's walk-in-place rule) and its `walk_in_place` now
  waits two progress windows. The scenario, cost and navigation-checkpoint
  baselines were retaken for Legion.

**Compatibility:** protocol **257** (0.8.0 and 0.8.1: 256), replay format
**12**, generator version **8**, campaign payload **4** and Crusades SQL schema
**9**; only the protocol changed. Checked in source: since `v0.8.1` the only
source changes are in `src/sim/` and `src/net/protocol.h` (no client change), and
since `v0.8.0` nothing under `src/server/`, `src/ai/`, `src/tnt/` (the map
generator), `src/campaign/`, the Crusades store or the replay header changed. A server rejects a client whose protocol differs from its own,
so **0.8.2 clients cannot join 0.8.0 or 0.8.1 servers, and 0.8.0 or 0.8.1
clients cannot join a 0.8.2 server**. Replay playback requires the recording's
protocol to equal the engine's, so 0.8.2 does not play replays recorded by 0.8.0
or 0.8.1, and they do not play 0.8.2 replays; keep an old copy of the engine to
watch old recordings. Generated-map recipes and saved `.kmp` maps carry over,
and so do saved settings (no setting was added or removed).

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.8.2-validation.md).
