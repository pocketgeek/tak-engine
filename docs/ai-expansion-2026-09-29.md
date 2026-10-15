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

## Update 2026-10-14: reservations, and allies' home deposits

The reservation of deposits that builders are already walking to never held
for a real order. It compared `Order::x/z`, which for a MobileBuild order is
the builder's approach point beside the site (32-70 px from a lodestone's
deposit), against a 32 px match; the regression test above built its order by
hand with the site in `x/z`. The planner now reserves `buildX/buildZ`, so a
Hard or Absurd AI never sends a builder to a deposit that one of its own or an
ally's builders is already bound for.

With builders no longer piling onto one deposit, the faster factions moved on
to the next free one, which on a team map is usually inside an ally's base.
Retail's AI never builds there (its lodestone search, icd `0x411510`, takes the
nearest free sacred site within its own base radius), but simply forbidding it
left the deposits of an ally that does not expand -- a Normal or Defensive AI,
or a human -- empty for the whole game. Hard and Absurd now leave a free deposit
within 900 map units of a living ally's start, and nearer that start than their
own, to that ally only while the ally is claiming it:

- a builder is bound for it (never raced, whoever's it is), or
- the ally has a builder that can reach it (finished, not carried or disabled,
  with a lodestone in its menu), and the deposit has not sat unclaimed for two
  minutes of game time (3,600 ticks).

Unclaimed means no lodestone on the deposit and no builder bound for it,
counted from the start of the game or from the end of its last claim, so a
lodestone lost in a raid gives the ally the same two minutes to rebuild. A
defeated ally, or one left with no builder that can reach the deposit (its
Monarch lost under Monarch Expendable), holds nothing. Enemy deposits, and the
Normal, Easy and Defensive AIs, are unchanged.

Both 4v4 Absurd replays behind this were re-run through the server's AI
controllers. With the old code the harness reproduces each recording command
for command (same final hash). Counted at the end of each game: lodestones
standing inside another ally's home area, and lodestone orders sent while
another builder of the same AI, or of an ally, was already bound for that
deposit:

| Game | Recorded | Strict rule | This rule |
| --- | ---: | ---: | ---: |
| Ally-held home lodestones, newest game | 3 | 0 | 9 |
| Ally-held home lodestones, earlier game | 9 | 0 | 23 |
| Duplicate / ally-raced orders, newest game | 71 / 41 | 3 / 0 | 1 / 0 |
| Duplicate / ally-raced orders, earlier game | 98 / 64 | 0 / 0 | 0 / 0 |

The remaining duplicate in the newest game is a Defensive AI's, which keeps no
reservations. Team income, `teamsim` on "4x4 team" (Crusades, 900 s), mean of
each team's income over the window, averaged over seeds, against the code
before these changes:

| Teams (seeds) | Window | Strict rule | This rule |
| --- | --- | ---: | ---: |
| Absurd+Defensive vs Hard+Normal (6) | 300-600 s | -32% / -20% | -3% / +10% |
| | 600-900 s | -29% / -31% | +6% / +2% |
| Absurd+Normal vs Absurd+Normal (4) | 300-600 s | -34% / -21% | -1% / +8% |
| | 600-900 s | -33% / -28% | -21% / +4% |
| Absurd+Normal vs Defensive, no attacks (8) | 300-600 s | -32% | -1% |
| | 600-900 s | -35% | -11% |
| 8 Absurd, 4v4 (4) | 600-900 s | +6% / -2% | +12% / -5% |

The cost of the two minutes falls on a slow AI ally. Zhon and Taros first
expand past their opening lodestone four to five minutes in, so their unclaimed
home deposits go to faster allies: with 8 Absurd AIs Zhon ends with 1-5
lodestones (5-8 before, 9-10 under the strict rule), and in the earlier replay
Taros ends with 2 (8 recorded) while Aramon and Zhon hold 8 of its home
deposits. And a far ally's builder, once bound for a slow ally's deposit, holds
it for its whole walk: in the no-attack runs the Aramon AI reserved Zhon's and
its Normal neighbour's deposits from 6,000-11,500 map units away, Zhon's own
builders went to the enemy's base instead, and team income fell 11% late in
the game.

No AI ever built on a deposit an ally's lodestone occupied; placement refuses
that for every player. `--mpai` hashes are unchanged (its AI plays Normal).

## Update 2026-10-14: Normal, Easy and Defensive

Normal, Easy and Defensive now keep the same reservations -- a deposit that one
of their own or an ally's builders is walking to, and a site planned earlier in
the same think -- and leave a living ally's home deposits to it under the rule
above. Their builders search 1,600 map units from home (Defensive's 600, the
Monarch's 900), which reaches a close ally's base; on "4x4 team" no ally starts
that close, so there the ally rule changes nothing.

They also build a lodestone when they run short of mana, as retail does.
Retail's lodestone priority (icd `0x410810`, ported as
`sim::retailBuildPriority`) is zero unless the store is under 30% full,
spending has outpaced income over the last second, and none of that lodestone
is unfinished. Its constructor planner (`0x40c890`) then draws among the menu's
lodestones separately and builds one instead of its ordinary pick wherever the
deposit search (`0x412f80`/`0x411510`) finds a free deposit in the base radius
(`0x40cf30`: 560 map units for the Monarch, 800 for other builders, growing
with the base), unless a builder of the base is already on one (`0x40c830`).
Below 70% allocation it skips the ordinary pick but still builds that
lodestone. These AIs keep their own ladder and add that trigger; Hard and
Absurd are unchanged.

The ladder alone held Normal to one lodestone for a whole game on "4x4 team".
Every deposit there pays 100 mana a second (sacred multiplier 10), and Normal
wants economy only while income is below 25 + 20 per factory, three factories
at that income, so the first lodestone met the target for good, even when the
store ran dry. The search radius was not the cause: every start has 6-10
deposits within 560 map units and all 10 within 965. Completed lodestones per
AI at 300/600/900 s (`teamsim`, Crusades, seed 1):

| Game | Before | After |
| --- | --- | --- |
| Eight Normal, 4v4 | 1/1/1 each | 2/2/2, 1/1/1, 1/1/1, 3/3/3, 3/4/4, 1/1/1, 1/1/1, 2/3/3 |
| Absurd+Defensive vs Hard+Normal: Normal | 1/1/1, 1/1/1 | 1/1/2, 1/1/1 |
| Same game: Defensive | 1/1/1, 1/1/1 | 1/1/1, 1/1/1 |

In the mixed game retail's rule seldom asks: the Creon Normal AI, traced there,
kept its store at least 45% full for ten minutes, and the Taros one ran short
once, near the end. With two builders and three factories, one deposit paying
100 a second outruns what these AIs spend. Retail's own AI would spend more --
Aramon's `ai/default.txt` allows four builders, four flying builders and four
priests -- and so expand more often.

`--mpai` (Inner Circle, seed 1, 300 s): Retail unchanged at `3785014928b7ab9a`;
Legion `cc1c4b26954597df` (was `bdcf4c85ab2fc01e`). The 60 s hashes were not
retaken.
