# Crusades Balance reference (audit in progress)

This is the working reference for Milestone 1 section 1.6 of the
[Darien Crusades plan](darien-crusades-plan.md). **The complete gameplay and
loader verification is not finished.** Counts below are source-field differences,
not a count of proven gameplay behavior changes.

## Reproduce the source comparison

```sh
cmake --build build --target crusades_balance_audit hpitool
build/crusades_balance_audit /path/to/retail-data > /tmp/balance.tsv
python3 -m unittest discover -s tools/re -p 'crusades_balance_audit_test.py'
```

The research tool uses the engine's existing retail VFS and TDF parser. It
excludes loose overrides and downloaded-map resources. Archive mount precedence
is therefore the same as the engine's base namespace, rather than whichever
archive happened to be extracted last. Output is tab-separated metadata;
backslashes, tabs and newlines within values are escaped. Descriptive name,
description and designation values are omitted.

For each winning unitscb definition it compares the corresponding units file,
including nested weapon/damage sections. A missing CB unit does not remove the
base unit. For build menus, however, a builder present in canbuildcb replaces
its entire canbuild menu: omitted entries are removals. Definition existence
rows retain additions/removals even if the menu file contains no explicit keys.
Repeated TDF sections have separate occurrence indices.

This is a **file/field diff**, not yet an effective UnitType diff. Unit aliases,
canonical object-name collisions, inherited movement classes, defaults and
numeric conversions must still be checked through the registry. Removed fields
may revert to defaults; a text difference need not change effective gameplay.

## First local survey

On 2026-09-30, the current `/home/pocket_geek/tak_data` installation yielded:

- 377 compared files, including base menu entries suppressed by CB replacement;
- 193 changed files, 15 CB-only files (all build-menu entries);
- 1,269 field differences plus 24 definition-existence rows;
- nine removed base-menu entries and fifteen added entries.

The survey uses the existing mixed Iron Plague-era installation; it is not a
claim to a pristine official 3.0 or Iron Plague installation. Raw output remains
in local scratch pending final provenance and coverage review.

Frequent differences include cost, build time, hit points, healing, experience
points, weapon ranges, category-specific damage, capture immunity, movement
classes, sight/radar, resource income and mana regeneration. The audit also
finds authored spelling differences such as cruisalt and firestandorder;
neither should be silently interpreted as a working gameplay key without
checking retail's reader.

## Verification still required

1. Fingerprint the exact effective source sets and establish the separate
   official-patch and Iron Plague comparison scopes.
2. Publish every gameplay-affecting field/menu delta with provenance, separating
   presentation-only changes and ignored authored keys.
3. Independently check registry selection, parsed values, defaults/conversions,
   weapon/category overrides and complete ordered build menus in both modes.
4. Fix demonstrated mismatches and run the required simulation validation and
   rebuilds for any engine changes.

The existing four synthetic audit tests validate report mechanics, not all
retail balance semantics. The broader Python research suite currently has
150 passing tests. No engine balance behavior has changed in this pass.

## Loaded registry checks

`crusades_registry_test` now loads both modes through `setupRegistry`, with
loose overrides and downloaded-map resources excluded. For the local corpus it
checks 404 canonical unit/mode cases and 66 complete ordered builder/mode menus.
It verifies twelve scalar fields (cost, build time, health, healing, worker rate,
income, storage, sight, radar, mana capacity/regeneration and build distance),
plus each authored weapon's range, reload time and projectile speed. Integer
fields are checked after truncation; float fields after their storage conversion.
Expected menus come from the selected source namespace, including whole-menu
replacement, priorities and alphabetical tie breaks.

```sh
cmake --build build --target crusades_registry_test
ctest --test-dir build -R '^crusades_registry$' --output-on-failure
```

This is loader coverage, not a retail execution comparison or complete balance
coverage. Alias-only definitions, category damage, movement-class inheritance,
bit fields and remaining authored fields still need explicit checks. The test
deliberately fails if either Crusades overlay is absent, so a base-only install
cannot silently pass as a two-mode audit.
