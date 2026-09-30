# Scenario unit actions: native comparison, 2026-09-29

The observations below execute the installed `KINGDOMS.icd` directly in Unicorn;
no game GUI, executable bytes, or game assets are included in the repository.
Run `python3 tools/re/check_scenario_actions.py` to repeat the 99 cases.

## Findings and engine changes

* CRT dispatch `4cad50`, cases 8–11 and 15, compares the selected type ID with
  each candidate and treats ID zero as a wildcard. Destroy, heal, damage and move
  iterate the executing player's units. Ownership iterates other active owners.
  Unknown region index `-1` skips the action; it does not select the whole map.
* Heal and damage are **absolute HP**, not percentages. The dispatcher forwards
  the operand unchanged to `51a140` with damage types 12 and 13. The receiver
  adds the unsigned 16-bit amount at `51a2f8` and caps at type maximum HP;
  damage subtracts the raw amount at `51a3d2`, bypassing ordinary armor math.
  Healing uses wide arithmetic before its maximum-HP clamp. Damage preserves
  native 16-bit HP word wrapping explicitly with unsigned arithmetic, including
  the odd retail boundary where HP 200 minus 65535 becomes HP 201. Native
  receiver probes cover 0, 1, 32767, 32768 and 65535 for both operations. Damage records native death type 13;
  explicit destroy records type 0, as requested by native `512610`. The zero-
  severity removal is immediate, skips death weapons/corpses, frees the owner's
  unit slot, and detaches builders from the removed site. Script damage to an
  unfinished site updates its authoritative HP and remaining work (`1-HP/max`);
  healing changes HP without advancing work, matching the native receiver.
  Lethal damage to an unfinished site retires it instead of leaving a zero-HP
  construction record that a later work tick could restore.
* Movement destination at `4cb3c4` is the integer region midpoint, multiplied by
  16 pixels. There is no extra half-cell offset. Native movement rejects
  unfinished construction (`Unit+108 != 0`), which the engine now does too.
  Native mission allocation remains represented by the engine's normal order
  submission and pathfinding.
* Creation uses `4cc3c0`: clip the region to legal map cells, scan from its
  top-left in row order until a cell is free of a live unit, then add half the
  created type's footprint to obtain its world position. If every cell is
  occupied, use the integer midpoint and the same footprint offset. This is
  distinct from movement. The engine formerly spawned everything at the region
  center. The native probe checks free, partially occupied and full regions.
  Composing the native primary-grid insertion (`5066f0`) with creation confirms
  that airborne mode 2 does not block, landed mode 1 and unfinished units do,
  and open/closed structure yards change occupancy. The footprint origin uses
  the existing verified `footprintOrigin` conversion. Trees, terrain grade and
  water eligibility do not participate in this primary-unit-grid query.
  Entirely off-map regions still fall back to their asymmetrically clipped
  midpoint in retail; two native cases verify this. Engine arithmetic uses wide
  midpoint intermediates and explicit fixed-point wrapping to avoid overflow.
  Explicit destroy passes native type 0 but preserves the prior attacker fields:
  `51279e..5127c7` includes them in the removal packet; this is not a new attack.
* Ownership calls `514da0`, rather than assigning the owner field alone. The
  local native path creates a replacement and copies HP, construction progress,
  facing and activation state. In particular, the HP copy at `514f8a` **does
  not heal the unit to half health**, unlike the engine's charm helper.
  The scenario transfer API preserves HP and construction state, clears old
  orders/production/group membership, updates live owner counts, and invalidates
  the spatial player cache. The engine retains stable unit IDs rather than
  reproducing retail's native allocation identity. Creation and transfer honor
  the destination player's unit cap, reflecting native pool allocation failure;
  construction whitelists and build-menu type restrictions remain bypassed.

`scenario_actions_test` covers wildcard targeting, ownership isolation, raw HP,
large values, no resurrection after damage, missing regions, movement midpoint,
creation occupancy, and transfer bookkeeping. Native comparisons observe action
selection, HP arithmetic, creation-position choice and the HP transfer block;
they do not claim full native mission or network object-allocation equivalence.
