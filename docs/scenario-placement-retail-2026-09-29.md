# CRT placement fields: native observations, 2026-09-29

These observations use the locally installed `KINGDOMS.icd`; no game executable
bytes or assets are included. Reproduce with
`python3 tools/re/check_scenario_placement.py` (Unicorn required).

The probe executes 24 native coordinate conversions and five native name cases.
It substitutes the terrain-height query and allocator/copy boundaries, not the
coordinate arithmetic or name-length decisions.

## Names

The CRT loader copies the serialized custom name at record offset `0x100` into
its placement record (`0x4cc8b9`–`0x4cc8d8`). After creating the unit, setup calls
`0x51f4c0` for nonempty names (`0x4cd316`–`0x4cd328`). That rename routine retains
at most **31 bytes**, allocating space for the terminating zero; it also emits a
rename notification when requested. This is a unit display name, not a distinct
unit type. CRT rule operands resolve through the type registry, not these names.

The engine stores this cosmetic label on the placed unit and copies it into the
render snapshot for selected-unit name displays. It does not affect type IDs,
construction menus, targeting, or simulation hashes. An empty name retains the
normal type label. The byte cap follows retail's byte-string behavior; it is not
a promise of 31 Unicode characters. Cartographer retains the complete authored
name and warns when the runtime truncates it. The editor labels it Display Name
and accepts duplicates, since it is not a unique trigger identifier.

## Vertical field

The serialized Y at record `0x204` is **not read** by native placement conversion
at `0x4cc8e6`–`0x4cc953`. The probe varies it between -100, 0, 200, and 999 and
observes identical converted positions; a memory-read hook independently checks
that this field is not accessed. Static-type positioning (`0x5085c0`) obtains Y
from the terrain sampler. For mobile types that helper leaves the intermediate
Y untouched; this does not make the serialized CRT Y an altitude override.

Accordingly the engine continues its terrain/flight initialization, without
inventing a map-authored altitude. Cartographer now preserves this unused field
through editing/history/save rather than replacing every value with 200, and
warns about nondefault values.

## Footprint origin

Native conversion interprets X and Z as **footprint-origin cells**. It computes
`x*16 + footprintX*8` and `z*16 + footprintZ*8`, in 16.16 coordinates. The former
engine/editor conversion added eight unconditionally, shifting larger units.
Scenario setup and Cartographer now use matching footprint centers, including
save/reload and unit dragging. The CRT cells remain unchanged across round trips.

The engine's authored angle support remains a useful extension: the native CRT
conversion does not consume the serialized angle in this record. Ordinary
skirmish placement is unaffected by these scenario-specific corrections.
