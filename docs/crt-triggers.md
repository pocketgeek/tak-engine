# CRT scenarios and trigger execution

The shared format is defined in `src/crt/crt.h` and read/written by
`src/crt/crt.cpp`. Cartographer retains placements, custom types, per-player
condition/action groups, and named regions. Its optional rule names live in a
separate `.editor` companion; they do not change retail CRT bytes.

## File layout

All numeric fields are little-endian. Counts are signed 32-bit integers.

1. Float version (1.0), custom-type count, 272-byte custom-type records.
2. Placement count, 568-byte unit records.
3. Player count (retail writes nine); for each player, a group count, then for
   each group a condition count and records followed by an action count and records.
4. Region count and 272-byte region records.

Conditions and actions have **separate opcode spaces**, each with 26 entries.
Both use 324-byte records: a 32-bit opcode followed by five 64-byte operand
strings. Unused bytes after string terminators may contain retail memory residue;
the parser ignores that residue and the writer zero-fills it.

Malformed/truncated input or an invalid/non-finite version returns an empty
scenario with `version == 0`. Cartographer refuses to open a damaged scenario
rather than treating it as an empty editable document.

## Operand definitions

`src/cartographer/triggers.cpp` contains the complete condition/action wording
and parameter kinds recovered from Cartographer.exe's tables at `0x51c190`.
The `<...>` placeholders determine operand order, not the type of an operand.
For example:

| Record | Opcode | Operands |
|---|---:|---|
| Condition: control more than | 15 | count, unit type, location |
| Condition: control less than | 16 | count, unit type, location |
| Action: create unit | 7 | unit type, location |
| Action: set flag | 2 | flag, value |
| Action: display text | 13 | recipient, text |
| Action: display text with flag | 25 | recipient, prefix, flag, suffix |

The control-count operand order is also present in shipped scenarios, for
example Ulin's Folly condition 15 with `2`, `ARAAT`, `hill`. The runner now uses
that order, handles `Any Unit` in control-count comparisons, and includes both
the flag value and suffix in action 25 messages.

## Current engine integration and limits

`src/sim/scenario.cpp` implements the CRT runner. The standalone debug scenario
path attaches it to `World`; the world ticks it and hashes its rule state.
Messages are filtered for the local viewer without changing that state. The
current runner fires on a condition group's false-to-true transition and supports
explicit rule disabling. This describes the implementation, not a claim that all
retail trigger execution semantics have been verified.

Ordinary skirmish client/server setup does **not yet attach CRT rules or spawn
CRT placements**. Campaign missions use a different OTA/mission-script path.
Protocol 196 preserves CRT and editor companions in transferred/cached maps,
but preservation alone does not enable their execution. Wiring the production
Cartographer Test Map workflow remains part of the active editor goal.

Other runtime limitations remain: out-of-range CRT player groups are currently
folded into the last available world slot; the ninth/neutral slot needs a proper
policy; per-placement/custom armor and weapon overrides are not fully applied;
not every wildcard/action combination is supported. These need resolution before
claiming complete authored-scenario playtest support or retail parity.

`scenario_test` covers truncation/version rejection, control-count operand order,
region/owner/alive filtering, `Any Unit`, flag interpolation, and matching rule
hashes despite different message recipients.
