# Water-yard building placement

How `World::canPlace` decides where a building with a water yard (Sea Fort,
Creon naval factory, Floating Tower) may stand. Where the computer players
*choose* to put a Navy Yard is separate: every level except Retail prefers a
site with open water straight out of the yard's door (`navalDoorSteps` in
`src/ai/ai.cpp`).

## Retail evidence

The original yard decoder at `0x4c0f59` maps `w` to `0x37`, `C` to `0x35`, and
`Y` to `0x31`. All carry the water bit `0x10`; none carries the land bit `0x08`.
The Sea Fort (`verasy`) and Creon naval factory (`crenavy`) have only `w`, `C`,
and ignored `.` cells. The mounted Floating Tower (`verfltwr`) has a water yard
as well.

For a wholly water-based yard, building placement (`0x507400`, reached through
`0x507d10`) uses `sea level - waterline` as the foundation height. Each active
cell's maximum terrain height must be no greater than that height. Ignored
`.` cells do not impose a terrain requirement. `w` and `C` both check body
occupancy; `Y` lacks those occupancy bits. Water cells also check blocking
features. Uppercase `C` cells are therefore water cells, not a land foundation,
and `w` cells are not checked against the ship navigation grid (no minimum
sailing depth applies to buildings).

A headless check of the original executable covered 34 cases, including:

- The Sea Fort entirely in water versus `w` over water and `C` over land.
- Terrain at, below, and above `sea level - waterline`.
- Raised terrain beneath ignored cells.
- Different cell minimum and maximum heights, confirming that water placement
  tests the maximum.

A live retail game also accepts and completes a Sea Fort in open water on
Aibel's Seaport.

## Engine rule

`World::canPlace` (the wholly-water-yard branch in `src/sim/sim.cpp`) applies
the waterline ceiling to wholly water-based yards, while retaining obstacle and
body checks. Mixed land/water custom yards (shoreline structures) take a
simpler path: their `w` cells must be on the ship navigation grid and their
other active cells on the land grid; their complete retail foundation rules are
not modelled.

## Map bounds

Retail building placement at `0x507400` checks the entire footprint before
examining yard masks: both origins must be at least 1, and the exclusive ends
must be less than the map dimensions. Yard transitions at `0x507ae0` require
the same bounds. The engine applies those bounds to building placement and to
placement requiring feature clearance, so ignored leading `.` rows cannot let a
yard extend beyond the map. Without this a factory could be built whose yard can
never open: it would finish construction but stay not ready, with its output
queue growing and no output site. Eleven boundary cases were checked against the
original executable.

## Tests

`placement_test` (ctest `placement`, needs the game data) runs in both Standard
and Crusades balance:

- The shipped Varro Passage terrain and Sea Fort at `(1472, 464)`: open-water
  placement is accepted and a nearby Veruna Priestess builds it. Controlled
  terrain also checks shallow water, deep water, exposed ground, blocked `C`
  cells, ignored `.` cells, and ordinary land-building placement.
- Map bounds: the Sea Fort at `(1600,112)` (a 6x18 footprint starting at row -2)
  and the next two rows are rejected, then `(1600,160)`, three tiles south, is
  accepted. At that site it queues and completes two of each Sea Fort ship,
  checking that the first output clears enough space for the second.
