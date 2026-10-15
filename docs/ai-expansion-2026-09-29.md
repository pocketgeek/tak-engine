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

The trigger's lodestone takes retail's radius with it: its deposit must lie
within `16 * max(5b, b*sqrt(n)/2)` map units of home (`0x40cf30`, ported as
`sim::retailAiBaseRadius`), n the summed footprint (footX + footZ) of the AI's
buildings and b 7 for the Monarch, 10 for any other builder (`0x40cfc0`) --
560 and 800 until n passes 100 -- and never beyond the tier's own reach
(Defensive's 600). With no free deposit there the trigger stands aside and the
ordinary ladder picks as before, with its own 1,600 (Monarch 900). The first
version searched the ladder's radius for the trigger too: on Inner Circle,
where one deposit per player keeps the store under 30% nearly all game, a
Normal builder walked 1,250 map units into an enemy base for a freed home
deposit and was lost there with the lodestone.

The ordinary ladder still reaches that far, as it did before the trigger:
Inner Circle's home deposits lie 830-850 from the start positions, beyond
retail's 800, so a retail-sized ladder would never build its first
lodestone there without retail's base centre, which moves with the base and is
not ported. In the same Inner Circle game (four Normal, seed 1, 300 s) every
lodestone the trigger placed was within 500 of home; the one 1,168-unit walk
into an enemy base (Creon, at 220 s, for the Aramon AI's freed home deposit)
came from the ladder, and the builder made the identical walk before either
change, when the deposit's owner happened to rebuild first.

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

`--mpai` (Inner Circle, seed 1) is unchanged with the radius in place: Retail
300 s `3785014928b7ab9a`, 60 s `8db23af1b0260ba1`; Legion 300 s
`bdcf4c85ab2fc01e`, 60 s `5bc7e8001c06611f` (protocol 262). The first version,
searching the ladder's radius for the trigger, had moved Legion 300 s to
`cc1c4b26954597df`. "4x4 team" is as in the table above: every deposit there
lies within the trigger's radius.

## Update 2026-10-15: the nearest ally, and stalled builders

Two changes to the rule above. Every difficulty runs the same code, so both
apply wherever an AI's builders reach an ally's base.

**The nearest ally takes an open home deposit.** Once the ally whose home a
deposit is in no longer holds it -- it has sat unclaimed for two minutes, or no
builder of that ally's can reach it -- the deposit goes to whichever other
living ally's start is nearest it, not to every ally. The ally whose home it is
may still build there. Under the old rule a far ally's builder could take such a
deposit and, once bound for it, hold it for its whole walk across the map while
the nearer allies, the owner included, could not race it: in the no-attack runs
the Aramon AI reserved Zhon's deposits from 6,000-11,500 map units away and
Zhon's builders went to the enemy's base instead. A defeated ally's home is open
to all, as before.

The cost of this: a deposit whose nearest other ally never builds there stays
empty, though a farther AI would take it. That ally may be a human who does not
expand, a Defensive AI (600 map units from home), or a Normal or Easy AI, whose
builders search 1,600 from home: on "4x4 team" a team's starts stand 2,200 to
6,300 apart, so the open deposits of an Absurd AI that does not expand would
wait for a Normal neighbour that never comes. Two human allies side by side keep
each other's open deposits from the AIs on their team.

**A stalled builder holds nothing.** A reservation now lasts only while its
builder is getting somewhere. Each think the AI follows every allied builder
whose orders hold a deposit on the task it is on now -- the lodestone itself for
an AI's builder, but a human's may wait behind a move, a repair or another
building: the nearest it has come to that task's site or goal, and the last
tick it came 64 map units nearer, was at work (building, or repairing or
reclaiming within reach), or started a new task. One that has not for a minute
of game time (1,800 ticks) is stalled. Its deposits are no longer reserved, nor
claimed for an ally; the claim ends when it last gained, so the deposit's owner
keeps it two minutes from then. The AI that owns the builder stops it, which
leaves it free for other work, and never sends it back to those deposits. A
builder walking across the map gains every few seconds and keeps its claim
however long it walks; one that arrives to find units on its site already gives
up within the sim's 31 retries (310 ticks). In the run that showed it (Absurd,
human, Absurd, human against Hard, Normal, Hard, Normal) a Zhon Beast Handler
sent to a deposit 3,300 map units away in a human ally's base moved 109 px in
440 s, and its reservation kept every ally off that deposit until the game
ended at 900 s.

Stalls are common on "4x4 team": 1,034 stops in the 13 games below. 666 of
those builders had moved less than 300 map units since their order (wedged in
their own base or in a crowd); most of the others stopped at the front on their
way to an enemy base. Only 22 had walked 300 or more while gaining less than
half of it, the signature of a long detour. A stopped builder
sent on to another deposit beyond the same jam may stall again; each time it
holds that deposit for a minute at most.

Over the same games, reservations held 180 s or more on deposits in an ally's
home (sampled every 30 s): 101 before, 2 after, both the nearest ally's builders
walking 3,000-3,900 map units at normal speed. Reservations held that long by a
builder that moved less than 300 map units: 22 before, none after. Team income
(`teamsim`, "4x4 team", Crusades, 900 s, Monarch Expendable), mean of each
team's income over the window, averaged over seeds, against main before both
changes:

| Teams (seeds) | 300-600 s | 600-900 s |
| --- | ---: | ---: |
| Absurd+Normal vs Defensive, no attacks (8) | +4% / -0% | +11% / -1% |
| Absurd+Normal vs Absurd+Normal (4) | +3% / +3% | +5% / +11% |
| Absurd, human, Absurd, human vs Hard+Normal (1) | +5% / -0% | -1% / -5% |

In the no-attack games Zhon now ends with 15-18 lodestones (7-11 before) and
Aramon with 18-19 (23-27). In the human-ally game the second human's home is
full at 900 s (3 of its 10 deposits free before). The first human is defeated
near the end in both runs and its base is fought over: the Aramon AI's
lodestones in allies' bases fall from 10 to none between 600 and 900 s (11 to 6
before).

`--mpai` (Inner Circle, seed 1; Normal AIs with no allies) is unchanged: Retail
300 s `bf6f1ac3081f9ee6`, Legion 300 s `7def7a0fa5dedfc6` (protocol 263). Its
60 s run ends before any builder could have stalled for a minute. The AI runs
on the server only, so the protocol is unchanged.
