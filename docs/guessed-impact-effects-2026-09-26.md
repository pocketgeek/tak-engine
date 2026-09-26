# Authored impact effects and airborne placement

This pass removes the generated fire/lightning/dust bursts previously substituted
when a weapon explosion class or its animation could not be loaded. An absent
selected class now creates no explosion. Real authored effects still use their
existing variant selection and frame timing.

## Native evidence

`tools/re/probe_impact_effect_route.py` executes retail `529c10`, intercepting
explosion creation at `492c80` while reusing the bounded map/damage services from
`probe_ballistic_impact_body.py`. Its 32 cases vary land/water, environmental/unit
contact, missing/present land and water classes, and the effect-enable argument.
All pass:

- Explosion creation receives the projectile's exact contact XYZ, including Y.
- Environmental water impacts choose the water class.
- Direct unit impacts choose the ordinary class even when the map cell is water.
  This covers boats and aircraft above water.
- A selected class of -1 creates nothing. In particular, missing water art does
  not select the land class.

This is a native dispatch comparison, with creation itself intercepted; it is
not a new Glide image comparison. Static inspection of `529c87` also distinguishes
camera-shake dispatch from explosion creation; those are separate operations.
The existing explosion-variant oracle passes 4,096 cases. The existing native
`52c8c0` rendering oracle passes 4,096 cases covering signed height projection,
activation/visibility gates, separate terrain shadows, and model/sprite order.
The prior weapon vtable audit establishes that stone, mind-control and frozen
LOS subclasses share `52c8c0`.

## Engine changes

`HitFx` carries optional captured absolute contact/source coordinates. Straight,
flame, guided and ballistic contact sites supply the existing projectile XYZ.
The event captures coordinates before damage callbacks, so later target movement
or removal cannot move the explosion. Authored explosion rendering uses the same
absolute coordinate path as other native effects.

The nearest-aircraft search and its 0.8 altitude multiplier are removed. That
search could lift a ground impact onto a completely unrelated nearby aircraft.
Blood's initial position uses the captured contact height, while its existing
victim-specific SweetSpot handling remains intact.

The remaining status-LOS presentation retains the muzzle already obtained by the
existing aim query, alongside its existing captured aim point, per weapon slot.
Release copies these into the cosmetic event; no additional script query or RNG
call is introduced. Its authored sprite projects absolute XYZ rather than an
interpolation of terrain-relative offsets. Legacy synthetic 2D projectile
fixtures no longer guess aircraft height; production weapons use native XYZ.

These changes do not replace the separate legacy instant status-damage delivery
or its cosmetic travel-time calculation. This pass fixes geometry and authored
impact selection, not that broader status-weapon simulation boundary.

## Regression coverage

The existing `weapon_impact_effect_route` client fixture now checks land, water,
and direct-unit-over-water class selection; missing-class silence; exact captured
airborne XYZ; retained status muzzle/target coordinates; authored variant timing.
The dropped-ballistic scenario in `retail_script_test` additionally checks that
its impact event preserves an above-ground collision point and survives a later
target-height change. The parent task runs builds and these tests with the rest
of the changes; native probes above passed independently.

No pathfinding, gameplay RNG, damage calculations or script scheduling is changed
by this impact/placement work. The added geometry is display-only and excluded
from the simulation checksum.
