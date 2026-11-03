# TAK Engine 0.9.19

0.9.19 follows 0.9.18. It changes how units move in **Legion** pathfinding: allied units
that meet head on where neither can get round now swap places, and units held up by
their own army press on instead of stopping dead, as they do in Retail. The **Passive**
AI now stays at home. Retail pathfinding plays exactly as in 0.9.18.

**Compatibility.** 0.9.19 uses network protocol **311** (0.9.18 used 309) and replay
format **15**. **0.9.19 does not play with 0.9.18 or any earlier version, and it does
not play their replays**, because Legion games now play differently. A server and all
of its players update together: a 0.9.18 server turns away 0.9.19 players, and a 0.9.19
server turns away 0.9.18 players, with a *protocol version mismatch* message. Keep a
0.9.18 copy (or build it from its tag) to watch 0.9.18 replays. The random map
generator (version 9) is unchanged, so a random map's ID makes the same map as before.

## Legion: allies swap places

Two columns of one side meeting head on in a lane only one unit wide, such as a bridge,
used to stand nose to nose, each waiting for the other to make room. Now, when two
units of yours or your allies' on move orders block each other and neither can get
round, they trade places and walk on. A unit that has just swapped waits a moment
before it swaps again, and units on attack, fight, guard or patrol orders never swap.

The test case is two allied armies of 500 crossing a river in opposite directions over
three bridges. In 0.9.18 they gridlocked at five of seven starts: a median of 741 of the
1,000 arrived and 182 stood stuck at the end. In 0.9.19 a median of 983 arrive and none
are stuck at any start.

On screen, the two units pass side by side over a fraction of a second (8 game ticks), instead of
jumping through each other in one frame. This is drawing only; the game itself moves
them in one step.

## Legion: blocked units press on

- **Queues close up.** A unit queued behind its own army on a move order closes up on
  the one ahead and slides along the file, as in Retail, instead of stopping dead
  while the front moves.
- **Formations flow round at speed.** Units in a formation flow round a rock, a wall
  end or a block of idle units at their travel speed, facing the way they go, instead
  of creeping sideways at a crawl. They no longer step backwards behind where they are
  heading, which the next step had to undo.
- **What keeps its old behaviour.** Units on attack, fight, guard or patrol orders
  still stand and fire from their places round a target and wait in their queue.
  Builders going to a site and newly built units leaving their factory hold as before,
  so they do not push onto a yard they must keep clear. A unit moving on its own,
  outside a formation, still edges round an obstacle carefully, which keeps large
  crowds with different destinations in order.

Legion is also a little quicker with 1,000 units on the move: three optimisations of
its route search that make exactly the same moves in a shorter time.

## Passive AI stays at home

A Passive AI sends no scouts and never explores, but it still sent waves at anything it
had seen anywhere on the map, such as your lodestone in the middle of it. It now sends
its army only against enemies within 800 pixels of one of its buildings: it defends its
base and never goes out after what it saw elsewhere. The AI runs on the server, so
this needs no change to replays or the protocol.

## Other

- The Slackware package build tries three package mirrors in turn, so one mirror's
  outage no longer fails it.

## Known limits

- All of 0.9.18's limits carry over (see
  [release-0.9.18-notes.md](release-0.9.18-notes.md#known-limits)).

Validation: [release-0.9.19-validation.md](release-0.9.19-validation.md).
