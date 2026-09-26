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
use protocol **180**. Released 0.7.1 uses **179**; clients and servers must update
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
