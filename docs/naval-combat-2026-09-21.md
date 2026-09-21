# Naval combat correction — 2026-09-21

The 0.6.9 combat sight checks used the ground navigation grid for all non-flying
shooters and targets. Deep water is impassable on that grid, so a clear sea lane
looked blocked. In a focused reproduction, War Galleys, Flagships, and Scout Ships
in both balances ignored a target 200 units away; explicit attacks closed to
roughly 70–87 units before firing, despite weapon ranges of 450–580.

## Changes

- Acquisition, firing, and combat movement holding share `combatLineOfSight`.
- If either participant is a water-domain unit, sight uses a separate terrain
  mask computed with submerged heights clamped to sea level. It admits open
  water and ordinary shore transitions without interpreting seabed slopes as
  walls. Raised land still blocks direct shots, as does the live shared
  obstacle overlay. Existing lobbed-weapon exemptions remain unchanged.
- Water-domain movers with no authored `turninplacerate` use their `turnrate`
  while aiming at rest. Otherwise an off-axis ship could acquire and stop in
  range but never face its target. Types with a pivot rate keep that rate.
- Ground-only sight, movement grids, route search, and ground movement kernels
  are unchanged. This is a correction to this engine's combat controller, not a
  claim that the complete retail naval firing controller has been ported.
- Protocol 177 prevents mixed gameplay with published 0.6.9/protocol 176.

## Regression coverage

`naval_combat_test` loads both retail and Crusades registries. It exercises 202
scenarios across all 18 armed mobile water-domain types (including campaign
creatures): idle acquisition, attack-move, explicit attack, off-axis aiming,
open-water and shore targets for ranged units, and adjacency for melee units.
Fixtures respect minimum weapon range and the idle maneuver leash; tests do not
ask melee creatures to attack unreachable inland targets.

Additional cases cover shore defenders shooting at ships, explicit obstacle
walls, raised terrain, removal of a live obstacle, and unchanged ground/water
movement-grid classification. In-range idle ships must fire without collapsing
their attack distance. The test is registered with CTest when `TAK_TEST_DATA`
is configured.

## Validation completed

- All targets rebuilt in `build`, `build-dbg`, and `build-o2`.
- All 40 CTests passed in each configuration (120 total), including naval,
  flyer, pathfinding, placement, transport, and production regressions.
- GCC/Clang determinism checks at O0/O2/O3 agreed on `8adc4762a852fadd`.
  The optional ARM cross-build was skipped because target headers are missing.
- Two isolated local client/server runs reached tick 600 with identical
  `b879f25ebe4532bf` hashes and no desync. These were standard multiplayer
  smoke runs; the naval-specific scenarios are covered by the dedicated test.
