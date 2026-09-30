# CRT scenarios and trigger execution

The shared format is defined in `src/crt/crt.h` and read/written by
`src/crt/crt.cpp`. Cartographer retains placements, custom types, per-player
condition/action groups, and named regions. Its optional rule names live in a
separate `.editor` companion; they do not change retail CRT bytes.

## File layout

All numeric fields are little-endian. Counts are signed 32-bit integers.

1. Float version (1.0), custom-type count, 272-byte custom-type records.
2. Placement count, 568-byte unit records.
3. Rule-set count (retail writes nine): All Players, then Player 1 through 8.
   Each set has a group count; each group has a condition count and records,
   followed by an action count and records. This differs from placement owner IDs:
   placement owner zero is Player 1; placement owner eight is neutral.
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
Messages are filtered for the local viewer without changing that state.
Released **0.7.13 uses protocol 198**. The implementation described below is the
**current checkout, protocol 199**; update every client and server together.

Rules execute at startup and whenever the integer game second advances (30 ticks).
A true group executes again at the next evaluation; Disable Rule makes it a
one-time event. All Players groups are copied into each seated player's rule list,
before that player's own groups. Enabled state is independent per player. See the
[native evidence](scenario-runtime-retail-2026-09-29.md) for ownership, cadence,
outcome recipients, and stat handling.

Authored placements replace default skirmish monarchs. Their owner, position,
heading, starting health, armor/weapon percentages and veterancy apply on every
peer. Player-owned placements require occupied lobby seats; neutral owner eight
requires none and is excluded from participant counts and results. Neutral units
do not auto-acquire players or appear as ordinary AI targets; explicit attack and
eligible capture still work. The initial camera uses the player's unit centroid.
Rule/region-only maps retain the normal monarch starts. Campaigns retain their
separate OTA/mission-script setup.

A map-local **Use Only** file limits construction menus, build commands, queued
production, infinite production and AI construction choices. It does not remove
preplaced units or prevent scripted creation. An absent restriction leaves the
normal roster available; an explicitly empty restriction permits no construction.
Missing or unknown restriction data is a visible setup error.

Custom-type armor and weapon percentages establish permanent factors on each
new unit; placement percentages multiply them. Aura buffs remain separate.
Type veterancy is the default for later spawns; a placement's veteran field
replaces it. `noveteran` units reject these ranks. Placement health sets starting
HP as a percentage of current HP, truncated to an integer and clamped to the type
maximum; it does not increase maximum HP. The custom-type health field is retained
but unused by retail's runtime, and Check Map warns when it differs from 100.

Victory and Defeat actions apply independently to the owner, teammates, or
opponents specified by the opcode. Winning does not force opponents to lose.
The first terminal action fixes each player's displayed result; a later defeat
can still mark that player defeated in gameplay. Explicit defeat immediately
removes that player's units. Standard monarch/unit-elimination
rules are bypassed while the authored scenario runner is installed. A scenario
without terminal rules can remain a sandbox instead of ending automatically.

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

Cartographer's optional **Log Test Map triggers** setting adds a local diagnostic
request to its temporary snapshot. Only the client explicitly launched with
`--play-map` installs the trace sink; a map received through multiplayer cannot
enable diagnostics. Logs record firing groups and attempted actions, are bounded
to 4 MiB of records, and live outside the temporary snapshot directory. Trace
callbacks are not hashed; a sink exception disables logging without interrupting
rule execution. `scenario_test` compares traced, untraced and failing-sink rule
hashes, as well as log ordering, escaping and truncation.

Unseated player owners, missing types, off-map placements and invalid owner IDs
produce visible setup errors. Unique names and vertical placement fields remain
preserved in CRT data but are not runtime unit identity/altitude overrides. The
ordinary lobby requires at least two participants; a verified authored scenario
can start with one. Not every wildcard/action
combination or neutral lifecycle has been compared with retail; this is not a
claim of complete mission-runtime parity.

The scenario tests cover malformed input, control operands and wildcards,
authored setup and peer hashes, construction restrictions, permanent combat stats,
neutral targeting/capture, and explicit outcomes. The retail-stat probe runs
native custom-default and placement arithmetic without launching the game.
