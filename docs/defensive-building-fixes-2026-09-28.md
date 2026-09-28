# Defensive building orientation and lighting

The combat aim controller already excluded structures from body rotation, but
the later navigation controller could still receive their attack orders. Its
combat-hold radius was 95% of weapon range. A target in the remaining band (or
an out-of-range target for a structure with `canmove=1`) could reach steering,
which turns a body even when its maximum speed is zero.

`tickNavigationMovement` now excludes structures from its navigation branch.
Wait and ambush mission handling remain available, and COB-controlled weapon
pieces still animate and aim. The rule uses `isStructure()` because shipped
buildings do not reliably omit `canmove`. Protocol 190 prevents old and new
simulation behavior from sharing a match.

The synthetic regression reproduced three rotation failures before the fix.
The shipped defensive-building test now covers 360 cases: both balance modes,
four target directions, and distances at 60%, 98% and 110% of weapon range.
All bodies remain fixed and both in-range groups fire. The test map was enlarged
so even the Trebuchet's longest test distances stay inside its boundaries.

The Death Totem's dark camera-facing surfaces had a separate renderer cause.
Directional palette shading negated the native light's X/Z components a second
time, after the body transform had already accounted for the authored/native
half-turn. The corrected helper uses the native direction with the outward
normal of the final transformed vertices. It matches native rasterizer output
on 289 sloped faces; cardinal-face checks also run in `retail_visual_test`.
The correction applies to structure lighting generally and retains each
texture's authored palette/shade table and each piece's SHADE/DONT_SHADE flag.
See [piece render flags](piece-render-flags-2026-09-27.md) for the oracle and
why its earlier independent expected equation did not catch this mismatch.

Validation: Release 87/87 CTests and optimized Debug 90/90 passed. The
cross-compiler determinism sweep retained `dcef618cd2e4d558` across the available
GCC/Clang optimization levels (ARM cross-builds lacked target headers). All
Release and optimized Debug targets, including client and server, were rebuilt.
No retail game process was launched; native comparison used isolated emulation.
