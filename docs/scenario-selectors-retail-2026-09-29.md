# CRT selector and condition behavior

Static inspection and targeted emulation of the installed `KINGDOMS.icd` on
2026-09-29 establish the behavior below. No retail GUI was launched and no retail
code or assets are distributed. Run `python3 tools/re/check_scenario_selectors.py`
for the independent native checks; `scenario_selectors_test` covers the integrated
engine behavior.

## Types and locations

The compiler resolves unit operands through the type-registry lookup
`0x515ac0`/`0x5159f0`, not through a placed unit's custom display name. Failed
lookups return type ID zero. In control queries and non-Create unit actions,
zero is the wildcard: an unknown unit name therefore behaves like **Any Unit**.
This is an observed retail quirk, not a guessed fallback. Create cannot create a
zero type and skips it. Authors should use a valid type ID or the explicit
wildcard rather than relying on misspellings.

Region lookup `0x4cbe50` compares complete names without case sensitivity; empty,
partial and unknown names return -1. Count helper `0x4cbf30` returns zero for that
missing-region ID. Consequently, “control fewer than 1 at a missing region” can
be true; the location does not become the whole map.

The count helper uses inclusive low/high cell bounds and the unit's footprint
origin (`+0x74/+0x76`), not its center or visual position. It excludes unallocated,
retiring and unfinished units. Reversed region bounds are not normalized.
The loader synthesizes **Anywhere** before authored regions with bounds
`(-10000, -10000, mapWidthCells+10000, mapHeightCells+10000)`
(`0x4ccd74`–`0x4ccddd`), so the wildcard includes temporarily off-map units.
Location midpoints used by movement are integer cells multiplied by 16, without
an extra half-cell offset. Signed half-distances truncate toward zero, and the
cell-to-16.16 conversion preserves native 32-bit wrapping for extreme authored
coordinates.

## Counts and comparisons

Most/least conditions compare strictly: tied counts fail for both. They compare
only other occupied, eligible, undefeated players. Zero is valid; with no other
eligible players both most and least hold, even at zero. The native probe calls
the real condition dispatcher for all six kill/loss/control most/least opcodes
across 36 cases, including ties, zero counts and a defeated competitor.

Native kill/loss queries `0x4cbdd0` and `0x4cbe10` have another quirk: type zero
returns the **first inserted type counter**, not the sum of all types. The probe
uses two records with counts 3 and 7 and observes wildcard count 3. The engine
preserves the first death-type key and hashes it, rather than depending on sorted
map order. A named type still returns its own count. Kill credit follows the
player stored on the damaging hit (`0x4cd626`), so capture or retirement of the
attacker afterward does not redirect the credit; self-kills do not increment the
kill counter.

## Flags, timers and random conditions

Flags use the first byte of their operand and are case-sensitive. For example,
`alpha` and `apple` address the same flag, while `A` is different. Set creates a
flag; Add/Sub only affect an existing flag (`0x4cae24`–`0x4cae6f`). An unset flag
or timer fails either comparison. Display-with-flag skips an unset flag
(`0x4cb9f8`–`0x4cba05`) and does not create it. Flag arithmetic and timer steps
retain native signed 32-bit wrapping rather than floating-point counters.

Random conditions draw from the existing MSVCRT stream at call origin
`0x4cac22`, then compare `threshold > draw * (100 / 32768)`.
The multiplier stored at `0x5f269c` is exactly representable. The engine uses the
same shared deterministic stream, including its state hash, instead of a private
scenario-only generator.

These checks do not validate every surrounding native gameplay subsystem. Cargo
lifecycle and other action behavior have their own tests and evidence; this note
covers the selectors and comparisons described above.
