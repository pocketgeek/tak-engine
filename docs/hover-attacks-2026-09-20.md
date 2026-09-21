# Flying attack positioning — 2026-09-20

Drakes were using the generic weapon-range stop (95% of 500, plus any building
padding) instead of their authored hover attack distance of 150. Combat could
also hold a previously landed flyer at ground height. Both balances supply
hoverattack=1, hoverattackdistance=150 and hoverattackaltitude=150 for the Drake.

The shared flying combat path now loads unit and weapon hover-attack settings.
A nonzero weapon distance/altitude overrides the unit value; missing unit altitude
uses cruisealt. Ground units with incidental hoverattack fields do not enter this
path. Flyers without hoverattack retain their existing attack controller.

Hover attacks use a persistent flight goal rather than dropping their attack
order at arrival. The goal faces the target, follows target movement, and uses
the configured altitude above local terrain/water, capped at retail's height 511.
The existing native flight navigation and velocity routines do the movement.
Weapon range remains the firing limit; building footprint padding does not
inflate the hover ring. Arrival and subsequent position adjustments therefore
remain independent of projectile range. Ground navigation was not changed.

## Retail observations

- Unit loader 4c04d4..4c054d reads distance and altitude (altitude defaults to
  cruise altitude). Weapon loader 531074..5310a3 reads per-weapon overrides.
- Mission 41dc1a..41dc8e resolves the selected weapon's nonzero distance/altitude
  before falling back to the unit definition.
- Point selection 41de5d..41df64 keeps the current integer radius inside a
  16-pixel band; outside that band it draws distance-3..distance+4. Independent
  rare angular and radial adjustments retain retail's draw order.
- 4e44c0 sets explicit altitude using max(local terrain, water level), capped at
  511. The surrounding mission installs a four-pixel acceptance radius and
  wakes after 15..29 ticks.

The port reuses that measured point-selection calculation within the existing
combat adapter; this is not a claim that every stage of retail's attack mission,
weapon aim prediction, or target-selection policy is now emulated.

`tools/re/check_hover_attack.py` compares 4,096 point choices and final RNG states
with the original binary. It reads the user's installation; no binary bytes or
asset data are embedded in the test.

## Validation

- Existing data-backed combat checks: all 19 armed flying types, both balances,
  after actual VTOL landing, using automatic acquisition, Move Fight, and Attack.
- New position checks: all 14 hover-attack flying types, both balances, approached
  from far away and from too close; retained attack orders and followed a moving
  target. Drake weapon overrides additionally exercise water height and the
  height-511 cap. Conversion is disabled in these positioning fixtures.
- Native point oracle: 4,096/4,096 matches, including near-ring and zero-distance
  cases and random-call count/state.
- Fixed-math GCC/Clang golden agrees: 8adc4762a852fadd. ARM cross-toolchain headers
  remain unavailable.

All targets rebuilt in Release, Debug and optimized Debug. All 39 CTests passed
in each build (117 total). Both repeated ordinary network runs reached tick 600
with hash b879f25ebe4532bf. Two mixed-army stress runs reached tick 300 with
14,609 living units and hash 66d489a8b3d456b7, without client/referee errors.
These are local short network regressions, not a fresh remote soak.
Protocol 175 identifies this simulation change; 174 is incompatible.

Logs: `/tmp/hover-*-build.log`, `/tmp/hover-position.log`,
`/tmp/hover-determinism.log`, `/tmp/hover-*-tests.log`,
`/tmp/hover-network.log`, `/tmp/hover-stress-network.log`.
