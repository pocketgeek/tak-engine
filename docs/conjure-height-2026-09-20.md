# Flying conjurer rendered too far north

The simulation already carries absolute `Unit::flightY`, advanced by the retail
flight controller. The renderer instead used a separate terrain-height servo
plus a fixed 0.7-second ascent to `cruiseAlt` whenever the flyer was busy. Since
height projects northward (`screenY = z - y/2`), this could draw Thirsha far north
of her simulated position while approaching or working a conjure site.

A controlled Crusades test on Ulasem Arena holds a placed Hunter construction
open and advances simulation and cosmetics at 30 Hz. At tick 31 the monarch's
absolute Y is 246, with map reference 62. Her correct height lift is 92 pixels.
The old renderer adds 70 more map pixels of northward displacement (210 screen
pixels at the test's zoom of 3). At tick 151 the excess is still 30.5 map pixels.
Her X/Z trajectory is identical before and after the change.

The render snapshot now carries the simulated height and flight occupancy.
Body placement, selection/picking, shadows and effects share the resulting
datum/altitude split. The split cannot lift a low flyer to a nearby hill's
clearance datum. Flight animation activation follows the sim's airborne mode,
so stationary conjuring continues to flap and gesture without inventing a
second height trajectory. Simulation, conjuring range and pathfinding are
unchanged.

Validation:

- Controlled before/after client probes compare projected position at ticks 2,
  11, 31, 91 and 151 (`/tmp/tak-flight-height-{before,after}.log`).
- `retail_visual_test` covers snapshot isolation and projection during ascent,
  hover and landing, including a terrain datum above the actual flyer.
- `conjure_test assets/game` passes placed and queued construction checks in
  both balances and the shipped COB's repeated wing and arm animations.
- All three clients rebuilt; visual and COB animation tests pass in Release,
  Debug and optimized Debug.

The change removes a demonstrated height error; these checks do not establish
complete frame-by-frame retail animation parity.
