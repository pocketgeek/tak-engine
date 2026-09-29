# Hard/Absurd mana expansion — 2026-09-29

Hard and Absurd now recruit expansion builders earlier after establishing income
and production. Their constructor target scales from 4 to 12 with income
(previously 3 to 8). Zhon can commit more mobile producers to expansion while
retaining its opening-army priority and a production reserve.

The planner reserves mana deposits as soon as it emits a build order. It also
recognizes allied construction destinations queued behind movement waypoints.
Previously several idle builders could choose the same unoccupied spot before
any arrived. Reservations spread them across distinct sites. New deposits take
priority over upgrading a nearby existing lodestone when both are available.

Placement, movement, reachability, the monarch's home range, mana affordability,
and actual unit/type caps still apply. Normal and Passive policy is unchanged.
This changes AI decisions, not retail movement or pathfinding.

## Comparison

Ten before/after runs used Ulasem Arena, Crusades balance, all five factions,
Hard and Absurd, 900 simulated seconds, and the same deterministic harness seed.
The opponent was idle. These are completed mana buildings, excluding unfinished
construction:

| Faction | Hard: before → after | Absurd: before → after |
| --- | ---: | ---: |
| Aramon | 19 → 30 | 20 → 37 |
| Taros | 18 → 22 | 19 → 27 |
| Veruna | 13 → 28 | 21 → 30 |
| Zhon | 20 → 26 | 28 → 37 |
| Creon | 19 → 28 | 11 → 27 |

All ten cases expanded further by deposit count and still produced armies.
Army sizes and composition also changed: investing in expansion can reduce the
army at a particular checkpoint, so this is not a claim that every force grows
larger immediately. Another ten 900-second runs passed with standard balance. Six additional
Zhon runs passed on generated 8×8 maps (seeds 1, 42, 777; Hard and Absurd,
Crusades), each retaining an army.

Regression tests verify that Hard and Absurd can expand outside Normal's home
range, choose distinct deposits in one decision batch, and honor a destination
behind movement waypoints. The retail-data policy suite covers both balances,
all factions' stance defaults, naval feasibility, and Passive's no-attack rule.
`aitool` now reports completed/remote deposits and furthest expansion distance.
"Remote" means more than 900 map units from the starting position.

Local logs: `/tmp/tak-ai-expansion-logs-before`,
`/tmp/tak-ai-expansion-logs-after`, and `/tmp/tak-ai-expansion-standard-after`.
