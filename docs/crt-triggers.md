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

`src/sim/scenario.cpp` implements the CRT runner. Shared match setup attaches it
to `World` when the map OTA declares `hasscenario=1` and explicitly opts in with
`[TAKPlaytest] { authoredscenario=1; }`; the world ticks it and hashes its rule state.
Messages are filtered for the local viewer without changing that state. The
current runner fires on a condition group's false-to-true transition and supports
explicit rule disabling. This describes the implementation, not a claim that all
retail trigger execution semantics have been verified.

Protocol 198 loads authored placements instead of default skirmish monarchs,
including ownership, position, heading, health and veterancy. All authored owners
must have occupied lobby slots. The initial camera position uses their units'
centroid. Rule/region-only maps retain the normal monarch starts. Campaigns retain their separate OTA/mission-script setup.

Cartographer's **Test Map (F5)** saves a temporary KMP and launches
`takclient --data <install> --play-map <snapshot.kmp>`. This documented production
handoff validates the snapshot through the normal map-package whitelist, caches
its verified revision with that opt-in section, and opens a private local-server lobby.
Ordinary retail maps and normal exports keep their existing skirmish setup even
when they contain CRT companions; cached Test Map snapshots retain the opt-in. It does not enable
any standalone/debug simulation. The editing session stays open. Temporary source
files are removed when the editor observes game exit; closing the editor first
leaves the running game and its temporary source undisturbed. Verified MapCache
copies remain selectable, as with downloaded maps.

Unsupported ninth/neutral owners, unseated owners, missing types, off-map units,
nondefault custom-type stats and per-placement armor/weapon overrides produce a
visible start error rather than being clamped, discarded or silently ignored.
Out-of-range rule groups are never assigned to another player. Unique names and
vertical placement fields remain preserved in the CRT but are not runtime unit
identity/altitude overrides. Standard match elimination rules still apply; this
is not yet a complete custom single-player mission/outcome system. The normal
lobby requires at least two participants. Use Only restrictions, every wildcard/
action combination, and retail trigger timing are not fully implemented/verified;
this is not a claim of complete authored-scenario or retail parity.

`scenario_test` covers truncation/version rejection, control-count operand order,
region/owner/alive filtering, `Any Unit`, flag interpolation, and matching rule
hashes despite different message recipients.
