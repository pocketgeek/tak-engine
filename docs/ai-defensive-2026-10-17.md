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
  every map measured. Other buildings stay within 600 px, and the Monarch stays
  home. `sendDefenders` already guards every building, remote ones included.
- Idle fighters inside the base walk out to the first free spot on rings
  around it, along their own bearing first, and stay there (`stationArmy`). A
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
income / units / structures / seconds the store sat at 90% or more, at 30
minutes:

| Map | Side | Defensive before | Defensive after | Absurdly Defensive |
| --- | --- | --- | --- | --- |
| Inner Circle | Aramon | 33 / 150 / 4 / 0 | 55 / 207 / 7 / 0 | 116 / 248 / 29 / 0 |
| Inner Circle | Taros | 33 / 76 / 3 / 0 | 56 / 79 / 6 / 0 | 112 / 123 / 18 / 0 |
| Inner Circle | Veruna | 33 / 153 / 7 / 0 | 55 / 213 / 9 / 0 | 118 / 251 / 30 / 0 |
| Inner Circle | Zhon | 34 / 139 / 13 / 11 | 55 / 220 / 21 / 0 | 116 / 219 / 21 / 0 |
| Inner Circle | Creon | 33 / 111 / 4 / 0 | 55 / 135 / 7 / 0 | 116 / 191 / 24 / 0 |
| Ulasem Arena | Aramon | 28 / 149 / 4 / 0 | 113 / 217 / 31 / 0 | 240 / 345 / 49 / 0 |
| Ulasem Arena | Taros | 28 / 112 / 3 / 0 | 112 / 102 / 22 / 0 | 146 / 139 / 25 / 0 |
| Ulasem Arena | Veruna | 28 / 164 / 3 / 0 | 113 / 215 / 31 / 0 | 240 / 406 / 50 / 0 |
| Ulasem Arena | Zhon | 28 / 169 / 15 / 0 | 113 / 175 / 23 / 0 | 240 / 277 / 33 / 0 |
| Ulasem Arena | Creon | 28 / 69 / 5 / 135 | 113 / 167 / 27 / 0 | 238 / 234 / 37 / 0 |
| Frey River Plain | Aramon | 28 / 163 / 3 / 0 | 103 / 185 / 27 / 0 | 216 / 396 / 41 / 0 |
| Frey River Plain | Taros | 28 / 117 / 3 / 0 | 103 / 114 / 23 / 0 | 212 / 292 / 41 / 0 |
| Frey River Plain | Veruna | 28 / 139 / 4 / 0 | 103 / 218 / 30 / 0 | 184 / 276 / 39 / 0 |
| Frey River Plain | Zhon | 28 / 174 / 15 / 0 | 103 / 240 / 26 / 0 | 218 / 330 / 35 / 0 |
| Frey River Plain | Creon | 27 / 58 / 7 / 0 | 103 / 100 / 21 / 0 | 194 / 197 / 32 / 0 |

Before, income never left the first deposit, and Creon on Ulasem Arena stopped
at 69 units with a full store. After, the store stays near empty (most samples
under 300 mana) and units grow throughout. Unit counts do not grow with income
for Taros because it spends on lodestones, castles and dearer troops. With a
unit cap of 150, an Absurdly Defensive Aramon stopped at exactly 150 and banked
the rest. That is the game rule, not an AI limit.

At 60 minutes (income / units / full-store seconds), nothing dies against an
idle opponent and the base corner fills up:

| Map | Side | Defensive before | Defensive after | Absurdly Defensive |
| --- | --- | --- | --- | --- |
| Inner Circle | Aramon | 33 / 184 / 1063 | 55 / 293 / 447 | 116 / 340 / 1688 |
| Inner Circle | Taros | 33 / 157 / 0 | 56 / 175 / 0 | 112 / 233 / 0 |
| Inner Circle | Veruna | 33 / 189 / 731 | 55 / 382 / 0 | 118 / 342 / 1524 |
| Inner Circle | Zhon | 34 / 156 / 1433 | 55 / 495 / 0 | 116 / 478 / 0 |
| Inner Circle | Creon | 33 / 143 / 770 | 55 / 279 / 0 | 116 / 329 / 832 |
| Ulasem Arena | Aramon | 28 / 174 / 990 | 113 / 511 / 272 | 240 / 646 / 949 |
| Ulasem Arena | Taros | 28 / 182 / 0 | 113 / 216 / 0 | 180 / 369 / 0 |
| Ulasem Arena | Veruna | 28 / 225 / 915 | 113 / 457 / 440 | 240 / 729 / 1685 |
| Ulasem Arena | Zhon | 29 / 197 / 1113 | 113 / 400 / 0 | 240 / 585 / 0 |
| Ulasem Arena | Creon | 28 / 69 / 1935 | 113 / 412 / 0 | 240 / 596 / 404 |

Known limit: with several hundred units and no losses, space limits production,
not the AI. Units take time to walk out through a crowded base, and pads stay
covered while they do. A few units end up walled in by buildings and cannot
leave. The store then fills, from about 25-40 minutes for Absurdly Defensive on
the smaller maps.

Reproduce with `TAK_FACTION=0..4` and the `SUM` line `aitool` now prints at
each report (`TAK_AI_EVERY` seconds, `TAK_UNIT_CAP`):

```sh
TAK_FACTION=4 build/aitool /path/to/tak_data 'Ulasem Arena' absurdly-defensive 1800
```

`TAK_AI_STATION=1` logs fighters that found no spot to walk out to.

## Validation

- `ai_behavior_test`: new checks cover factories past 32, builders and towers
  past their old caps and profile limits, a lodestone past its profile limit,
  every idle producer acting in one think within the store, stationing, the
  stalled-queue drop, and level 5 through the match config, slot record and
  replay header, with a 272 replay refused. Built against the previous
  `ai.cpp`, 22 of these checks fail.
- `ctest -L quick`: 392 of 392 tests pass, as do `ai_behavior_data`, `naval_ai`
  and `retail_ai` on retail data.
- `--mpai`, Inner Circle, seed 1, 60 s, at the default level: Retail
  `2c39430636a3dbda` and Legion `f75b2a11410f43b4`, the same as before. At
  `TAK_AI_LEVEL=5`, Retail `7c6dc9bccc9fab41` and Legion `6c8e0e1a42c4f981`,
  with err=none and the same result on two runs. At `TAK_AI_LEVEL=0`, Retail
  `b2bd8157780962cc` and Legion `cfc09cce047e87a6`, with err=none.
- `tools/desync-hunt.sh` gains an `ai-absurdly-defensive` scenario.
