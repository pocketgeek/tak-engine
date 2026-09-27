# Native death lifecycle audit — 2026-09-27

This audit uses the locally installed retail binary through Unicorn; it does
not launch the retail game. `tools/re/probe_native_set31_lifecycle.py` executes
retail's death-event builder (`0x512610`), dispatcher (`0x512860`), unit updater
(`0x51d3e0`), death timer (`0x51e380`), and retirement (`0x512ae0`). See the
probe's docstring for controlled allocation, map-stamping and graphics seams.
It tests lifecycle decisions, not rendered corpses or terrain placement.

## Script decision and dispatch

`0x512610` initializes the corpse output to zero and synchronously queries
`Killed(severity, corpseOutput, deathType)` via `0x56c720`. The event stores
the resulting corpse index in its low nibble. Nonzero construction remaining
(unit float `+0x108`) suppresses that corpse after the query.

The local dispatcher does not query Killed twice. On remote dispatch,
`0x512860` notifies Killed with the transmitted values instead. Dying is then
started with the death type if severity is positive and that callback exists.
A missing Dying callback causes immediate retirement unless the owner already
has a SET31 timer. There is no general four-second handoff in this path.

Self-destruct (death type 5) bypasses Killed, uses severity 1 and corpse 0,
and still runs Dying. Types 0, 6, 7, 8, 10, 11, 14 and 15 bypass Killed with
severity 0. Type 9 requests corpse 1 with severity 0. This is native control
flow, not a proposed replacement for separate statue/cargo handling.

## Retirement timing

SET26 makes the next outer unit update retire the owner. SET31 sets owner
byte `+0x13`, stops future VM updates and starts its one-second timer. The
retail timer subtracts float 0.03 per update. After 34 decrements, the next
outer update retires the owner. A SET31 written during an update is decremented
in that same update; one written by the initial death callback is first
decremented on update 1. Therefore SET31 at tick 0 retires at tick 35, whereas
SET31 at tick 33 inside the updater retires at tick 67, not 68.

A trace of all 204 COBs in the extracted shipped-script directory completed
without emulation errors or unretired owners. Retirement ranged from tick 0
to 147: 38 immediate cases, 39 at tick 1 and 34 at tick 35. This fixture starts
from fresh VM state and severity 100, so these are example timelines, not
constants to apply to live units. Live construction state, existing script
statics and RNG can change the authored branches.

| Script | Ordinary death | Explosion | Self-destruct | Ordinary corpse |
| --- | ---: | ---: | ---: | --- |
| araarch | 28 | — | — | 1 |
| araat | 27 | 27 | 27 | 1 |
| arakeep | 0 | 0 | 0 | 1 |
| vertrans | 0 | 0 | 0 | 1 |
| arafly | 1 | 1 | 1 | none |
| aragren | 1 | 1 | 1 | none |
| crefire | 1 | 1 | 1 | none |
| npcflag | 35 | 35 | 35 | none |
| npcthesh | 35 | 35 | 35 | none |
| zonamoe | 67 | 35 | 67 | none |
| zonbasil | 67 | 35 | 67 | none |
| crebomb | 147 | 147 | 147 | none |

Self-destruct leaves no corpse in every table case. Aramon Archer writes
SET26 on tick 27; Arrow Tower writes it on tick 26. Giant Orm and Basilisk
write SET31 on tick 33 during ordinary death. Fire Wagon writes SET26 at
tick 0. The seven previously identified corpse refusals are directly confirmed.

Changing GET28's fixture water-depth response to 100 did not change the nine
sampled timelines (araarch, arafly, arakeep, crebomb, crefire, vertrans, verman,
zondrake, zonhunt). This does not verify native corpse placement/sinking in
actual water. Construction remaining 0.5 suppresses corpses; with GET17=50,
Bomb Sprinkler's authored countdown shortens from 147 to 1 tick. Other sampled
lifetimes were unchanged. Flyers do not acquire a special generic delay:
Flying Builder/Drake retire at 1 and Thirsha at 35 in this fixture.

`--warm-create` additionally executes Create and one VM update before death.
Aramon Archer and the seven refusal cases retain the timings above in that
mode. Arrow Tower, Keep and Bomb Sprinkler currently enter an uncovered
fixture seam during Create, so the probe does not claim warmed native
coverage for those three. Giant Spider (`zonspide`) retires at tick 11 from
fresh state and tick 1 after Create: Dying branches on its initialized static
6. The simulation full-roster comparison has this same Create-dependent
result; every other ordinary-death retirement matched the fresh native trace.

## Reproduction

```
python3 tools/re/probe_native_set31_lifecycle.py \
  --native-death-state --trace-json --script zonamoe
python3 tools/re/probe_native_set31_lifecycle.py \
  --native-death-state --trace-json --script crebomb \
  --construction-remaining 0.5 --get-unit-value-17 50
```

JSON reports actual retirement ticks (including immediate tick 0), corpse
requests, SET writes, callback dispatch sites and VM update ticks. The existing
strict `--instant-corpse`, `--corpse-request` and SET26/31 checks remain available.
No retail assets or binary bytes are included in this change.

## Engine integration

The simulation keeps the existing script VM through Killed and Dying, consumes
the queried corpse output, and follows SET26/31 retirement. Corpse blocking and
decomposition begin at the resulting handoff, including immediate retirement.
The unfinished, self-destruct, statue and destroyed-cargo paths remain distinct.
The renderer uses that handoff instead of a separate four-second cutoff; death
sounds remain deliverable when a display frame misses a short-lived death body.
Attached death flames now use the authoritative emission/owner-removal stream,
without the former second display-side retirement clock. Display callbacks keep
the existing pose and script statics rather than resetting a guessed local flag.

`death_lifecycle_test` checks 846 cases across both balance modes, including all
registered script types under ordinary/explosion damage, the seven refusals,
SET26/31 timing, self-destruct, unfinished construction and delayed ruin blocking.
`building_death_test` additionally checks all 29 restored explosion corpses on
land and water and the sixteen immediate building ruins. Development protocol
189 prevents these simulation changes from mixing with older peers.

## Verification of the integrated change

All targets rebuilt in Release GCC, optimized Debug GCC, Debug GCC and Debug
Clang. Their full suites passed (81, 84, 84 and 84 tests respectively), including
campaign, transport, movement and projectile regressions. Final focused checks
were repeated after the renderer integration. GCC and Clang agree on the corpse
scenario hashes: `f276638aa9853823` (standard) and `af684d573857ff1b` (Crusades).
The deterministic-math sweep still produces `dcef618cd2e4d558`; local ARM legs
skip because the target libc/headers are unavailable.

Repeated seeded 1,800-tick client/server matches agree for normal sight/radar
(`669cbfb1ecb7164f`) and doubled sight/radar (`c03102628e9c7a1c`), without desync
or referee mismatches. All 45 explosion-corpse meshes pass cached/reference
body and shadow geometry checks in both balance modes. Native piece flags,
lighting, palette-upload readbacks and the full structure-texture roster are
covered separately in [the rendering audit](piece-render-flags-2026-09-27.md).
