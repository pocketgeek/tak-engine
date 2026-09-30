# Native balance field review

This review separates raw authored fields from the retail reader and effective
combat behavior. It uses the official ordinary/Crusades 3.0 `KINGDOMS.icd`,
SHA-256 `6ddc7fae0cbf3ee13d530614b64608a3aeefca7deb73719310a4f68fe1f22e96`.
Addresses are image virtual addresses. No retail UI was launched. The companion
field-coverage inventory accounts for the complete source-report field set;
this document records the native findings behind the exceptional cases.

## Damage category and integer conversion

The unit loader reads `damagecategory` at `0x4bfbd4` using the string getter
`0x5432c0`, lowercases/interns it at `0x4bfbfb–0x4bfc04`, and writes its key to
UnitDef `+0x9e` at `0x4bfc09`. An absent category becomes zero at `0x4bfc11`.
It does not derive this selector from generic CATEGORY or TEDClass.

Combat lookup `0x531e50` loads the target definition through Unit `+0xb4`, then
loads precisely that `+0x9e` selector at `0x531e64` and calls `0x531de0`.
The latter searches the weapon override tree (`+0x44`, native search `0x473ad0`)
and returns the matching node's integer at `+0x10`; otherwise it returns the
unsigned 16-bit default at weapon `+0x88`. This is also the cursor selector's
native damage contract. Broader generic-category matching was an engine bug.

The weapon loader reads DAMAGE/default through integer getter `0x543190`
(`atoi`) and stores its low 16 bits at `0x531813`. Other DAMAGE keys are parsed
as binary64 multipliers by `0x5431c0` at `0x531887`. At `0x531b4b–0x531b65`, the
unsigned default is multiplied by that value and converted through `0x5d3d54`
to an integer before insertion into the override tree. Conversion changes only
rounding mode to truncation; combat later loads this already-rounded integer
(`0x52a34e–0x52a359`). Thus fractional effective damage must not be retained
until application of later attack/armor modifiers.

Precision is not inferred from Unicorn's initial state. CRT initializer pointer
`0x6287d8` points to `0x5d3875`; its call at `0x5d3884` invokes `0x5dac26`.
That routine calls `_controlfp(0x10000, 0x30000)` through `0x5d8d8c/0x5d8d57`,
selecting 53-bit significand precision with `fldcw` at `0x5d8d84`. Executing that
initializer from CW `0x037f` produces CW `0x023f` in the probe (precision bits
`0x0200`). An explicit CW `0x027f` uses the same arithmetic precision. The
53-bit setting matters: `10 * binary64(0.3)` becomes integer **3**, whereas
CW `0x037f` (64-bit significand) gives **2**. Host `long double` is therefore
an incorrect portable implementation of this operation.

The unhooked native arithmetic block, native integer conversion and native tree
lookup are reproducible without a game launch:

```sh
python tools/re/probe_crusades_damage.py /path/to/KINGDOMS.icd
```

The script requires `pefile` and Unicorn, verifies the executable fingerprint,
executes the CRT precision initializer, checks seven arithmetic cases at each
of two precision settings, and checks four real target-category/tree lookups.
The 53-bit cases include `301*0.5 -> 150`, `100*0.29 -> 28`,
`65535*1.1 -> 72088`, `91*(-0.5) -> -45`, and `180*0.04 -> 7`.
No lookup or arithmetic function is hooked.

The engine correction stores precomputed integer override damage, preserves
binary64 product rounding before integer truncation, interns the single target
DamageCategory, and shares the combat lookup with cursor eligibility. The
synthetic `crusades_damage_test` exercises the actual FBI loader, uint16 default
wrapping, decimal integer input, native arithmetic goldens, interning parity,
and immunity versus generic-category non-matches. The expanded registry test
checks every loaded attacker against every actual target category in each mode.
These are simulation changes, requiring matching updated clients and servers.

The reviewed shipped inputs have finite multipliers and in-range signed integer
products. This audit does not claim faithful CRT overflow/NaN behavior for
arbitrary malformed third-party weapon values, or newly prove every later
armor/experience/AOE calculation.

## Ignored authored spellings and wrong scope

Retail's key search `0x543110` performs a full case-insensitive string comparison
inside its sorted key array; it does not accept prefixes or typo aliases.

| Authored field | Native reader evidence | Effective interpretation |
|---|---|---|
| `unitinfo/cruisalt` | No such string; `CruiseAlt` at `0x614cf0`, read at `0x4c0237` through integer getter, default zero, stored at `0x4c0241` | Ignored typo. Arafly's corrected CB `cruisealt=200` is a real zero-to-200 altitude change. |
| `unitinfo/firestandorder` | No singular string; plural `FireStandOrders` at `0x614b90`, read at `0x4c0634–0x4c0649`, default one | Ignored singular field; zonflies' added singular zero does not change the native standing-order bit. |
| `unitinfo/damagetype` | `damagetype` string `0x619c5c` is referenced by weapon reader `0x531bad`; unit selector uses `damagecategory` as traced above | Wrong scope. Zhon Harpy's move from unitinfo/damagetype to damagecategory activates the category only in CB. |
| `unitinfo/watermultipliser` | No misspelled string; `watermultiplier` at `0x614f94`, requested at `0x4bfc62`, fixed getter `0x5431f0` at `0x4bfc6b`, default `0x10000`, stored at `0x4bfc75` | Ignored typo. CB lifdeer/lifdeer2 lose the working 0.81 key and therefore use 1.0. |

The engine previously treated `watermultipliser` as an alias, silently preserving
0.81 for those CB deer; that alias is removed to match the native default.
The other ignored keys must remain raw-report entries, not silently disappear
from evidence or become working compatibility aliases.

## Coverage boundaries

Whole-menu replacement, canonical object-name selection and aliases are checked
through the expanded registry audit rather than by interpreting each raw source
file as an independently spawnable unit. Presentation fields and ignored keys
remain visible in the source comparison. Name-based status/effect heuristics
must not be called presentation-only merely because their source key is `name`;
the field-coverage review separately checks their current consumers.

This report establishes local balance parsing and combat-selection behavior.
It supplies no evidence for missing historical server capture/ranking formulas.

## Later packaged executable corroboration

The mapped code bytes in the official EXE-3 and the fingerprinted GOG/local
`assets/game/KINGDOMS.icd` agree exactly in the following audited ranges:
unit readers `0x4bfb00–0x4c1000`, damage loading `0x531810–0x531be0`, damage
lookup `0x531de0–0x531e80`, wind callback `0x4d4520–0x4d4580`, and CRT precision
setup `0x5dac26–0x5dac40` (half-open endpoints). This corroborates these specific
native findings for the later package; it is not a claim that the executables
are identical or that addresses transfer to the original CD executable.
