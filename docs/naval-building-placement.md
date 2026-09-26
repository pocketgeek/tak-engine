# Water-yard building placement

The Sea Fort could not be placed in open water on Varro Passage because
`World::canPlace` treated its uppercase `C` yard cells as a land foundation.
It also checked `w` cells against the ship navigation grid, imposing the
minimum sailing depth on buildings.

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
features.

A headless check of the original executable covered 34 cases, including:

- The Sea Fort entirely in water versus `w` over water and `C` over land.
- Terrain at, below, and above `sea level - waterline`.
- Raised terrain beneath ignored cells.
- Different cell minimum and maximum heights, confirming that water placement
  tests the maximum.

This agrees with the earlier [live Sea Fort observation](transport-animation-audit-2026-09-21.md#retail-driver-and-live-sea-carrier-round-trip):
retail accepted and completed the building in open water on Aibel's Seaport.
No retail game launch was needed for this correction.

## Engine correction

`World::canPlace` now applies the waterline ceiling to wholly water-based yards,
while retaining obstacle and body checks. Mixed land/water custom yards retain
their previous handling; their complete foundation rules are outside this fix.
Ground-unit navigation and movement algorithms are unchanged.

The change affects authoritative build and AI decisions, so development builds
use protocol **181**. Released 0.7.1 uses **179**; clients and servers must update
together.

## Validation

All 174 local tests passed across Release, optimized Debug, and Debug. Repeated
client/server runs on Varro Passage agreed on state hash `3c4e5e85a939988c`.
GCC and Clang at O0/O2/O3 matched the deterministic-math golden
`dcef618cd2e4d558`; the optional local ARM cross-build was unavailable because
the target headers were missing.

The placement regression uses the shipped Varro Passage terrain and Sea Fort
at `(1472, 464)` in both Standard and Crusades balance. It rejects that site
against the pre-fix simulation library and checks construction by a nearby
Veruna Priestess after the correction. Controlled terrain also checks shallow
water, deep water, exposed ground, blocked `C` cells, ignored `.` cells, and
ordinary land-building placement. An offscreen client capture confirms the
Priestess actively conjuring the Sea Fort at the map-backed site.

## Map-edge production stall

The follow-up report was reproduced from the player's Varro Passage replay,
with every recorded state-hash checkpoint matching. The Sea Fort at `(1600,112)`
finished construction but remained at `ready=false`, `yardOpen=false`, and
`buggerOff=true`; eleven ships accumulated in its queue without an output site.
Its 6x18 footprint began at row -2. The four ignored leading rows had allowed
placement to pass even though the full yard extended beyond the map.

Retail building placement at `0x507400` checks the entire footprint before
examining yard masks: both origins must be at least 1, and the exclusive ends
must be less than the map dimensions. Yard transitions at `0x507ae0` require
the same bounds. The engine now applies those bounds to building placement and
to placement requiring feature clearance. This prevents constructing a factory
whose yard can never open; it does not relocate an already built factory.
Ground movement and pathfinding are unchanged.

Eleven boundary cases were checked against the original executable without
launching the game. The regression rejects the reported site and the next two
rows, then accepts `(1600,160)`, three tiles south. At that valid site it queues
and completes two of each Sea Fort ship in both balance modes, checking that the
first output clears enough space for the second. Protocol 181 distinguishes
this additional authoritative placement change from protocol 180.
