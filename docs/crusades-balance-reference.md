# Crusades Balance reference

This records the original standard-versus-Crusades data changes required by
Milestone 1 section 1.6 of the [Darien Crusades plan](darien-crusades-plan.md).
Source reports, effective loaded-value reports, fingerprints and regression
coverage are linked below. Counts distinguish authored fields from effective
fields; neither is a count of independent balance-design decisions.

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

This command produces a **file/field diff**. The companion effective audit
below resolves canonical definitions, inherited movement, defaults, conversions
and complete menus through the registry. Removed fields can revert to defaults;
a text difference need not change effective gameplay.

## First local survey

On 2026-09-30, the current `/home/pocket_geek/tak_data` installation yielded:

- 377 compared files, including base menu entries suppressed by CB replacement;
- 193 changed files, 15 CB-only files (all build-menu entries);
- 1,269 field differences plus 24 definition-existence rows;
- nine removed base-menu entries and fifteen added entries.

The survey uses the existing mixed Iron Plague-era installation; it is not a
claim to a pristine official 3.0 or Iron Plague installation. The complete derived comparison is now published with source fingerprints below.

Frequent differences include cost, build time, hit points, healing, experience
points, weapon ranges, category-specific damage, capture immunity, movement
classes, sight/radar, resource income and mana regeneration. The audit also finds authored spelling differences such as cruisalt and
firestandorder. Their native reader behavior is classified explicitly in the
field review rather than silently repairing the original data.

## Verification boundary

All **177 distinct changed source-field paths** across the three scopes have
an explicit classification and check/reference in the
[coverage table](research/darien-crusades/balance/field-coverage.tsv) and its
[explanation](research/darien-crusades/balance/field-coverage.md). Gameplay
values/defaults, active weapon slots, native category damage, movement
inheritance and complete ordered menus are verified through the loaded registry.
Presentation-only and ignored authored keys remain visible instead of being
counted as gameplay changes. This is not a claim of pixel-perfect UI artwork,
audio output, or complete engine equivalence beyond these changed rules.

Five synthetic source-audit tests validate comparison mechanics and manifest
hashes. The synthetic native-derived damage regression and the retail-backed
registry tests validate the loaded rules. Native reader/lookup experiments
resolved mismatches; their evidence is in the
[native field review](research/darien-crusades/balance-field-review.md).

## Reproducible source scopes

The derived reports contain changed gameplay metadata, not original definitions
or game prose. Manifest rows identify all mounted root archives by SHA-256 and
all winning unit, menu and game-data members by decoded-byte SHA-256, size and
origin archive. The tool checks those source bytes against the actual audit VFS.

| Scope | Field report | Manifest | Compared files | Changed fields |
|---|---|---|---:|---:|
| GOG 2.0.0.22 packaged Iron Plague-era installation | [gog.tsv](research/darien-crusades/balance/gog.tsv) | [gog-manifest.tsv](research/darien-crusades/balance/gog-manifest.tsv) | 377 | 1,269 |
| Original Iron Plague CD composed data view | [iron-plague-cd.tsv](research/darien-crusades/balance/iron-plague-cd.tsv) | [iron-plague-cd-manifest.tsv](research/darien-crusades/balance/iron-plague-cd-manifest.tsv) | 377 | 1,269 |
| Base data/English archives plus official ordinary 3.0 patch archives | [official30.tsv](research/darien-crusades/balance/official30.tsv) | [official30-manifest.tsv](research/darien-crusades/balance/official30-manifest.tsv) | 352 | 1,053 |

The CD and official-patch scopes are explicitly assembled data views, not executed installers.
The CD view combines the disc's base/Data archives with the Required_Install_Files
and Crusades_Files cabinet groups. Its complete field report is byte-identical
to the GOG report. The official-patch view
uses `data.hpi` and `english.hpi` from the fingerprinted GOG package plus every
root HPI from the fingerprinted ordinary 3.0 patch, without IPData/IPEnglish.
The provenance investigation also found byte-identical original-CD base and
IP data archives; see [sources](research/darien-crusades/sources.md).
Both 3.0 installer distributions carry identical balance archives, so installing
the Crusades campaign package is distinct from selecting Crusades Balance.
The GOG field report is byte-identical to the current local-install report.

```sh
build/crusades_balance_audit /path/to/data --manifest > /tmp/balance-manifest.tsv
```

## Effective loaded-value differences

`crusades_effective_audit` loads both modes and compares the fields affected by
the original source deltas, including derived movement limits/timing, aura
parameters, native weapon-slot state, precomputed integer damage and ordered
menus. It also retains selected presentation references used downstream. It
does not claim to serialize every field or every rule of the engine.

| Scope | Effective report | Changed effective fields |
|---|---|---:|
| Original Iron Plague CD | [iron-plague-cd-effective.tsv](research/darien-crusades/balance/iron-plague-cd-effective.tsv) | 1,405 |
| GOG 2.0.0.22 | [gog-effective.tsv](research/darien-crusades/balance/gog-effective.tsv) | 1,405 |
| Base plus official 3.0 patch | [official30-effective.tsv](research/darien-crusades/balance/official30-effective.tsv) | 1,171 |

The original-CD and GOG effective reports are byte-identical. `fixed16` values
are the raw signed 16.16 storage (divide by 65,536); floating values use enough
digits to identify the stored float. Booleans are 0/1. An absent category-table
entry means **use that weapon's default damage**, not zero damage. Native
weapon slot numbers remain stable when an inactive slot disappears.

Enum values follow the public engine types: weapon kind 0 normal, 1 guided,
2 remote, 3 wandering, 4 dropped; remote subtype 0 plain, 1 earthquake, 2 hail,
3 mind control, 4 freeze; status 0 none, 1 frozen, 2 stoned, 3 paralyzed;
domain 0 ground, 1 water, 2 hover; aura kind 0 armor, 1 attack, 2 joy.
Visual family is 0 arrow, 1 lightning, 2 fire; that classifier is not itself
proof of the final rendered appearance.

```sh
cmake --build build --target crusades_effective_audit
build/crusades_effective_audit /path/to/data > /tmp/effective-balance.tsv
```

## Loaded registry checks

`crusades_registry_test` loads both modes through `setupRegistry`, excluding
loose overrides and downloaded-map resources. It checks canonical definitions,
complete ordered menus and active native-to-local weapon slot mappings.
A review caught that the initial weapon check searched the wrong TDF level and
therefore checked no weapons. This is corrected: weapon, explosion and category
counts are explicit, and a zero-coverage corpus fails.

Current coverage includes:

- costs, build times, health, healing, experience, worker rate, mana income,
  storage/capacity/regeneration, sight/radar and building reach;
- fixed-point speed/acceleration/braking/terrain multipliers, turn rates,
  movement-class footprint/depth/slope inheritance and flight altitude;
- capture/status immunity, transport counts/sizes/reach, floating, shadows,
  corpse/statue references, material and damage categories, joy aura parameters;
- weapon ranges, reloads, speed, mana cost, area/edge effects, aim tolerance,
  duration/buildup/variation and their tick conversions, emission, projectile
  steering, type/subtype, damage kind, air targeting and ignition;
- complete category-damage tables, effective multiplier damage, interned versus
  string-lookup parity, and unmatched-category fallback;
- projectile model/art, explosion art, light size and impact sound;
- whole ordered menu replacement, priorities and alphabetical tie breaks.

The GOG scope exercises 404 unit/mode cases, 66 complete builder/mode menus,
397 weapons, 12 death explosions and 426 category overrides. The official 3.0
scope exercises 344 unit/mode cases, 54 menus, 323 weapons, seven death
explosions and 348 category overrides. Final registry runs pass 126,413 checks for each IP-era scope and 93,337 for the
official 3.0 scope. Native investigation resolved the suspect keys and damage
semantics; no changed gameplay field is left unclassified.

```sh
cmake --build build --target crusades_registry_test
ctest --test-dir build -R '^crusades_registry$' --output-on-failure
```

## Corrected wind callback gate

Retail sets UnitDef offset `+0x260` bit 31 if `windgenerator` or `wind` is
nonzero (EXE-3 `0x4bfe91`–`0x4bfed5`). Its `WindChange` dispatcher tests this
bit at `0x4d453c` before calling the script. Previously the engine delivered the
callback whenever the script existed, irrespective of the authored flag.
It now loads that OR condition into `UnitType::receivesWind` and applies it to
the client animation callback. No gameplay state or pathfinding changes.

This matters to the balance comparison: `cregate` and `zonmana` gain `wind=1`
in the IP-era Crusades definitions. Other units also retain their authored
wind-gating behavior instead of receiving an unsolicited callback.

Isolated execution of the fingerprinted EXE-3 routines passed all sixteen
combinations of `wind`/`windgenerator` in {0, 1, 2, -1}, and 96 callback cases
combining those flags, the global wind-enabled bit and three relative headings.
It confirmed that bearing subtraction is not normalized. The harness and retail
machine code remain in scratch, outside Git; the roster test checks both modes'
loaded wind flags.

## Corrected damage interpretation

The native lookup uses the target's single `DamageCategory` token. The engine
previously searched its broader CATEGORY/TEDClass token list, which could select
an override that retail would not use. Both combat and cursor targeting now
use the single native category.

Retail reads default damage as an integer stored in an unsigned 16-bit field.
It multiplies each authored category multiplier by that default using 53-bit
binary floating-point precision, truncates toward zero, and stores the resulting
integer in the category table. Previously the engine retained float multipliers
and applied them during damage lookup. The corrected loader reproduces the
native stored values before later combat modifiers.

For example, native default damage 301 with multiplier 0.5 stores 150, and
100 with multiplier 0.29 stores 28. The runtime's floating-point precision was
checked rather than assumed: executing its CRT initializer selects 53-bit
precision; the discriminating 10 × 0.3 case produces 3 there and 2 at 64-bit
extended precision. The original code, tests and exact addresses are described
in [the native review](research/darien-crusades/balance-field-review.md).

The new `crusades_damage` regression exercises numeric truncation, wrapped
integer defaults, single-category selection, explicit immunity and cursor/combat
agreement. The roster test also checks every loaded weapon against every actual
target type in both modes.

## Corrected authored water-key fallback

The native unit reader accepts `watermultiplier` with a fixed-point default of
1.0. It does not accept `watermultipliser`. The engine previously added that
misspelled-key fallback. In the Crusades definitions for `lifdeer` and `lifdeer2`,
the correct key disappears and the misspelled key appears; the effective native
value therefore changes from fixed-point 53084 (authored 0.81) to 65536 (1.0).
The loader and research probes now preserve that native default instead of
silently repairing the authored spelling. This changes the affected units'
terrain-speed input; it does not replace the navigation algorithms.

The combat and terrain-input corrections require **development protocol 204**.
Clients and servers must be rebuilt/updated together. No release tag is created
by this milestone.
