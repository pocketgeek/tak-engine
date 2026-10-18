# Uncapped Defensive AI and Absurdly Defensive — 2026-10-17

The request: Defensive should "just keep building relative to its mana income
with no limits", and a new level, Absurdly Defensive, plays like Defensive with
double the mana income from all sources, as Absurd does.

## What changed

**Defensive (aiLevel 0)** keeps every AI-imposed limit off its production; its
spending follows its income instead.

- The planner's targets scale with income without a ceiling: one factory per 25
  income (was capped at 32), builders at the Monarch plus one and one more per
  50 income (was the Monarch plus one, whatever the income), towers with income
  and army (was capped at 48). When the store passes 60%, it also wants one more
  factory and one more tower.
- Profile limits from `ai/default.txt` no longer apply to it (lodestones,
  builder types, army). An entry the profile disables (limit 0, or no weight)
  stays disabled.
- Every idle producer may act each think (was three). The orders one think gives
  are held against the store by the affordability gate, so 25 mana starts two
  10-mana soldiers, not twelve. The existing low-mana throttle (no new start
  below 50 mana while something is under construction) keeps spending at
  income.
- Builders other than the Monarch claim mana deposits out to Normal's 1600 px
  from home (was 600). Without this, income stayed at the one home deposit on
  every map measured. The Monarch stays home. `sendDefenders` already guards
  every building, remote ones included.
- The base grows with its buildings. Other buildings used to stay within a
  fixed 600 px of home; that disc filled at about 31 buildings, after which
  every builder answered "no site" and the store filled (Absurdly Defensive
  Aramon, Inner Circle: 1688 s full by 60 minutes, buildings frozen at 31 from
  32 minutes). The build radius is now retail's AI base radius
  (`retailAiBaseRadius`, 8·b·√n for n the summed footprint of its buildings, b 7
  for the Monarch and 10 for other builders), never under 600. When nothing fits
  within 340 px of the builder, the whole base is searched outward from home.
- Past the old fixed radii, the grown base stays on its own side of the map: a
  site nearer any enemy start than home is refused. Without this, an Absurdly
  Defensive Aramon on Frey River Plain built its way to the idle enemy, and its
  troops stood 140 px from the enemy start.
- Buildings keep a 48 px lane between them (lodestones excepted: a deposit is
  where it is). Packed buildings walled two Ulasem Arena builders into pockets
  by 46 minutes, every site unreachable, the store full.
- Sites already spoken for are not given out again: a site ordered this think,
  one an own builder is walking to, and, for a minute, one whose order came to
  nothing (the builder idle again with nothing started there). On Frey River
  Plain builders went back to one keep site think after think.
- Idle fighters inside the base walk out to the first free spot on rings
  around it, along their own bearing first, and stay there (`stationArmy`). A
  spot is free when no own fighter stands idle within 40 px of it or is walking
  there, and it lies on the AI's own side of the map. The base, for this, ends
  at its outermost building other than a remote lodestone. A
  growing home army otherwise covered the factories' output pads: a Creon
  Defensive AI on Ulasem Arena built nothing after 25 minutes, with 62 units in
  its base and a full store.
- A factory whose queued unit has found no site for 30 s (output pad blocked)
  drops that order and trains other types for two minutes (`unqueueStalled`).
  A Creon Workshop on Frey River Plain held one Mechanic from 200 s to the end
  of the game.

Game rules still apply to it as to every player: the room's unit cap, FBI
`totalallowed`, side and build permissions, and placement. It still never
attacks or scouts, keeps its Monarch home, and builds income first (income
below 20 is the top priority, and it keeps retail's short-of-mana lodestone).

**Absurdly Defensive (aiLevel 5)** has Defensive's parameters and
`incomeMultFor` 2.0, the same `Player::manaMult` Absurd uses. That multiplier
covers every income source the sim has: summed recurring income (lodestones,
income units, the Monarch), which also feeds allied surplus sharing and god
favour; feature reclaim; ordered corpse reclaim; and automatic corpse reclaim.
Scripted scenario grants are not income and are not doubled. The lobby lists it
after ABSURD (DEFENSIVE → EASY → NORMAL → HARD → ABSURD → ABSURDLY DEFENSIVE), in
violet, on two lines. `TAK_AI_LEVEL=5` selects it in headless runs.

Every peer derives the income multiplier and the defensive stance from the
level, so a peer that does not know level 5 would clamp it to Normal and desync.
**Protocol 273**; 272 replays are refused. The sim itself is unchanged; Normal,
Easy, Hard and Absurd play exactly as before (identical `aitool` runs on all 15
map/side cases below).

## Measurements

`aitool`, one AI against an idle opponent, default unit cap 2000. Cells are
income / units / structures / seconds the store sat at 90% or more.

Before this change (27066ebe), at 30 minutes, Defensive's income never left its
first deposit (27-34 on every map and side) and a Creon Defensive AI on Ulasem
Arena stopped at 69 units with the store full for 135 s. Now, at 30 minutes:

| Map | Side | Defensive | Absurdly Defensive |
| --- | --- | --- | --- |
| Inner Circle | Aramon | 56 / 246 / 6 / 0 | 118 / 178 / 25 / 0 |
| Inner Circle | Taros | 55 / 68 / 5 / 0 | 116 / 130 / 19 / 0 |
| Inner Circle | Veruna | 56 / 188 / 6 / 0 | 118 / 225 / 28 / 0 |
| Inner Circle | Zhon | 55 / 224 / 22 / 0 | 116 / 218 / 22 / 0 |
| Inner Circle | Creon | 56 / 153 / 7 / 0 | 116 / 142 / 21 / 0 |
| Ulasem Arena | Aramon | 115 / 192 / 32 / 0 | 264 / 372 / 53 / 0 |
| Ulasem Arena | Taros | 115 / 151 / 27 / 0 | 242 / 138 / 34 / 0 |
| Ulasem Arena | Veruna | 114 / 216 / 34 / 0 | 306 / 364 / 56 / 0 |
| Ulasem Arena | Zhon | 113 / 154 / 21 / 0 | 240 / 304 / 35 / 0 |
| Ulasem Arena | Creon | 113 / 121 / 23 / 0 | 240 / 231 / 40 / 0 |
| Frey River Plain | Aramon | 187 / 211 / 34 / 0 | 930 / 445 / 92 / 0 |
| Frey River Plain | Taros | 205 / 105 / 28 / 0 | 976 / 552 / 107 / 0 |
| Frey River Plain | Veruna | 187 / 299 / 41 / 0 | 996 / 855 / 137 / 0 |
| Frey River Plain | Zhon | 187 / 257 / 30 / 0 | 398 / 455 / 48 / 0 |
| Frey River Plain | Creon | 187 / 207 / 35 / 0 | 392 / 221 / 48 / 0 |

At 60 minutes, where 58806575 (this change's first version, with the fixed
600 px build disc) filled its store and froze its building count, the store now
stays near empty on every map and side:

| Map | Side | Defensive 58806575 | Defensive now | Absurdly Defensive 58806575 | Absurdly Defensive now |
| --- | --- | --- | --- | --- | --- |
| Inner Circle | Aramon | 55 / 293 / 22 / 447 | 56 / 381 / 26 / 0 | 116 / 340 / 31 / 1688 | 120 / 408 / 43 / 0 |
| Inner Circle | Taros | 56 / 175 / 6 / 0 | 55 / 140 / 5 / 0 | 112 / 233 / 29 / 0 | 116 / 257 / 31 / 0 |
| Inner Circle | Veruna | 55 / 382 / 25 / 0 | 56 / 357 / 13 / 0 | 118 / 342 / 39 / 1524 | 122 / 479 / 49 / 0 |
| Inner Circle | Zhon | 55 / 495 / 42 / 0 | 55 / 484 / 41 / 0 | 116 / 478 / 44 / 0 | 116 / 468 / 40 / 0 |
| Inner Circle | Creon | 55 / 279 / 8 / 0 | 56 / 242 / 14 / 0 | 116 / 329 / 34 / 832 | 116 / 308 / 34 / 0 |
| Ulasem Arena | Aramon | 113 / 511 / 43 / 272 | 115 / 448 / 52 / 0 | 240 / 646 / 69 / 949 | 460 / 966 / 117 / 0 |
| Ulasem Arena | Taros | 113 / 216 / 30 / 0 | 115 / 366 / 46 / 0 | 180 / 369 / 33 / 0 | 386 / 381 / 66 / 0 |
| Ulasem Arena | Veruna | 113 / 457 / 40 / 440 | 114 / 494 / 55 / 0 | 240 / 729 / 50 / 1685 | 524 / 1111 / 136 / 0 |
| Ulasem Arena | Zhon | 113 / 400 / 40 / 0 | 113 / 335 / 35 / 0 | 240 / 585 / 57 / 0 | 240 / 685 / 66 / 0 |
| Ulasem Arena | Creon | 113 / 412 / 35 / 0 | 113 / 271 / 36 / 0 | 240 / 596 / 48 / 404 | 266 / 562 / 68 / 0 |
| Frey River Plain | Aramon | 187 / 608 / 56 / 1178 | 205 / 501 / 61 / 0 | 384 / 669 / 42 / 2499 | 1162 / 1557 / 197 / 0 |
| Frey River Plain | Taros | 187 / 314 / 42 / 0 | 708 / 347 / 76 / 0 | 380 / 610 / 48 / 2194 | 1354 / 1619 / 221 / 0 |
| Frey River Plain | Veruna | 187 / 722 / 60 / 543 | 391 / 775 / 103 / 0 | 356 / 774 / 58 / 1768 | 1578 / 2000 / 261 / 716 |
| Frey River Plain | Zhon | 187 / 653 / 60 / 0 | 187 / 558 / 55 / 0 | 366 / 904 / 81 / 0 | 398 / 1057 / 93 / 0 |
| Frey River Plain | Creon | 187 / 418 / 51 / 0 | 187 / 492 / 55 / 0 | 398 / 622 / 49 / 480 | 1008 / 646 / 119 / 0 |

The one full store at 60 minutes is Frey River Plain Veruna, which reached the
2000 unit cap at about 48 minutes; banking past the cap is the game rule. Unit
counts do not follow income for every side: Taros, and Creon once it builds
academies and dragons, spend on dearer units and on lodestones and castles. Both
versions spend all their income in those cases (store near 0); the composition
differs. The base growing past 600 px also lets income grow where a map has
deposits on the AI's own side (Frey River Plain, Ulasem Arena).

Inner Circle has one deposit per player, so income stays at the home deposit:
56 for Defensive, 116-122 for Absurdly Defensive.

Not changed, and seen on 58806575 too: on Inner Circle, where the starts are
2950 px apart, an occasional fighter is well past the middle of the map at a
5-minute sample -- 340-930 px from the idle enemy's start in 7 of the 10 runs
(58806575: 340-1100 px in 8 of 10). These are fighters given a short move near
the base (stationing, or clearing a factory door) that the movement code routes
round the map. On Frey River Plain (starts 4400 px apart) the closest fighter in
an hour was 1450-3050 px from the enemy start (58806575: 2230-3250), the larger
army walking to stations round a larger base. Sampling the station rings more
densely brought it closer to home but filled the store again (Frey River Plain
Veruna Defensive, 280 s; Aramon Absurdly Defensive, 546 s), so the rings are as
they were.

Reproduce with `TAK_FACTION=0..4` and the `SUM` line `aitool` prints at each
report (`TAK_AI_EVERY` seconds, `TAK_UNIT_CAP`):

```sh
TAK_FACTION=4 build/aitool /path/to/tak_data 'Ulasem Arena' absurdly-defensive 3600
```

`TAK_AI_STATION=1` logs fighters that found no spot to walk out to.

## Validation

- `ai_behavior_test`: new checks cover factories past 32, builders and towers
  past their old caps and profile limits, a lodestone past its profile limit,
  every idle producer acting in one think within the store, stationing, the
  stalled-queue drop, and level 5 through the match config, slot record and
  replay header, with a 272 replay refused. Built against the previous
  `ai.cpp`, 22 of these checks fail. The second revision adds checks for the
  grown base (a site past a full 600 px disc, inside the base radius, on the
  AI's own side, with lanes), stationing (not onto a spot a fighter holds, not
  onto the enemy's side), and sites (two builders in one think get two; a site
  whose order came to nothing is not given out again); all 12 fail against
  58806575's `ai.cpp`.
- `ctest -L quick`: 392 of 392 tests pass, as do `ai_behavior_data`, `naval_ai`
  and `retail_ai` on retail data.
- `--mpai`, Inner Circle, seed 1, 60 s, at the default level: Retail
  `2c39430636a3dbda` and Legion `f75b2a11410f43b4`, the same as before. At
  `TAK_AI_LEVEL=5`, Retail `7c6dc9bccc9fab41` and Legion `6c8e0e1a42c4f981`,
  with err=none and the same result on two runs. At `TAK_AI_LEVEL=0`, Retail
  `b2bd8157780962cc` and Legion `cfc09cce047e87a6`, with err=none.
- `tools/desync-hunt.sh` gains an `ai-absurdly-defensive` scenario.
- Second revision, `--mpai` Inner Circle seed 1, 300 s: Retail
  `7535cb08ee787b75`, Legion `47b705a730bbc22f` (60 s: `2c39430636a3dbda`,
  `f75b2a11410f43b4`), all unchanged since --mpai plays Normal.
  `TAK_AI_LEVEL=5` gives `b5d550a65085a912` on every run (three), and
  `TAK_AI_LEVEL=0` `eb5e6284c848f6c8`, with no desync lines.
