# Classic AI: P8 acceptance ledger

Players see this level as **DEFAULT** (the lobby's name for AI level 6 since 2026-10-23, after
the `ai/default.txt` profile that drives it); "Classic" remains its internal and developer name.

2026-10-23, branch `task-classic-ai`. The acceptance rule of the plan
([classic-ai-plan.md](classic-ai-plan.md) §5, P8): every ported routine has a passing
emulation oracle that was once shown to fail on a planted bug, the engine game tests
pass, the docs are written and the campaign switch is in. Retail is not run: an oracle
runs the retail routine in Unicorn (`tools/re/emu.py`, against the gitignored
`assets/game/KINGDOMS.icd`) and our port on the same random inputs, and compares
every output and the RNG state.

## How this ledger was made

- **Oracle run:** the whole opt-in set (`-DTAK_CLASSIC_ORACLES=ON` and
  `TAK_CLASSIC_PRINTWEIGHTS`, 26 ctests) on build-o2 at the P8 head, plus by hand the six
  oracles of the shared templates and the profile that have no ctest
  (`check_ai_profile`, `check_ai_categories`, `check_ai_centroid`,
  `check_build_priority`, `check_player_cache`, `check_resource_history`) and the two
  sim routines the brain calls (`check_rectangle_reachability`,
  `check_construction_economy`). All pass with 0 mismatches.
- **Planted bugs:** every oracle was run again with one bug planted in the port (in a
  separate worktree of the same head, rebuilt for each plant, then reverted): 39 plants,
  one or more per routine. The earlier phases had each oracle fail on planted bugs once
  (their commit messages name them); this re-run checks all of them at one head, the
  shared-template oracles included, which had no recorded plant before. Two plants were
  missed at first, and what was done about each is in the table: one led to a stronger
  oracle, the other is a gap in what random cases can reach (exact ties).

## Ledger

"Cases" is the oracle's default run; a FAIL count is how many cases differed with the
plant in (an oracle that stops at the first difference shows the failing case instead).

| Retail routine | Port | Oracle | Cases, mismatches | Planted bug: result |
|---|---|---|---|---|
| 0x538f00 executor, 0x410c80 weight, 0x410d50 limit | `classicprofile.cpp` | check_ai_profile | 1496 scripts over 8 tables, 0 | an exact weight line no longer locks: FAIL (first script) |
| PrintWeights tables (0x410810 over the type facts, pref, limit) | `classictypes.cpp`, brain creation pass | check_ai_printweights | 20 dumps, 1176 rows x 3 columns, 0; 1300/1300 emulated table passes | builder flag dropped for structures: FAIL (priority 812/1176, 0 dumps reproduced) |
| 0x410810 dynamic priority | `sim::retailBuildPriority` | check_build_priority | 8192, 0 | naval x2 instead of x3: FAIL |
| 0x411a90 bookkeeping clock | `sim::retailTickPlayerCache` | check_player_cache | 4096, 0 | 8-tick window: FAIL |
| 0x401330 / 0x401350 shortfall | `sim::RetailResourceHistory` | check_resource_history | 4096, 0 | threshold -0.02: FAIL |
| 0x40fed0 manager ctor (98 squads, 0x40c560 quotas, wave rolls 0x40c240 / 0x40c3c0 / 0x40c490) | `classicsquads.h` | check_ai_squad_ctor | 3000, 0 | base 1 air and naval reserve draws swapped: FAIL 2752/3000 |
| 0x40fe80 cleanup + 0x40eeb0 assignment (campaign mode 1, massattack) | `classicsquads.h` | check_ai_assignment | 4000, 0 | campaign first pass on ring 2: FAIL 37/4000. massattack re-pulling units already in 97: **missed** (no case had a unit in 97), so the oracle now starts a twentieth of the units in 97/98 (commit f6ab0b92): FAIL 26/4000 |
| 0x40fd80 scheduler | `sim::retailTickAiSchedule` | check_ai_scheduler | 4096, 0 | countdown rand(7)+2: FAIL |
| squad clocks (0x40b320 / 0x40e180 / 0x40ea30 / 0x40eb40 / 0x4101e0 reschedules) | `sim::retailTickAiSquad` | check_ai_squads | 4096, 0 | backup clock 149: FAIL |
| 0x40af70 nearest base | `sim::retailNearestAiBase` | check_ai_base_selection | 4096, 0 | ties keep the last: FAIL |
| 0x40cf30 ring radius | `sim::retailAiBaseRadius` | check_ai_base_radius | 7168, 0 | ring 3 scale 4: FAIL |
| 0x40a5f0 categories | `sim::retailAiCategory` | check_ai_categories | 16384, 0 | category 6 ignores capacity: FAIL |
| 0x40aa50 centroid | `sim::RetailAiCentroid` | check_ai_centroid | 4096, 0 | X sum divided unsigned: FAIL |
| 0x40b320 member loop + 0x51d837 allocation | `classicBaseMembers`, `classicPlanFactory` | check_ai_factory_think | 4000, 0 | commander exception dropped: FAIL 58/4000 |
| 0x40dcb0 factory planner | `sim::retailAiPlanFactory` | check_ai_factory_plan | 8192, 0 | allocation gate 0.25: FAIL |
| 0x412d00 menu draw | `sim::retailAiChooseBuild` | check_ai_build_choice | 4096, 0 | draw kept on <= weight: FAIL |
| 0x412c90 entry weight | `sim::retailAiBuildWeight` | check_ai_build_weight | 8192, 0 | priority read unsigned: FAIL |
| 0x40c890 constructor planner | `classicPlanConstructor` | check_ai_constructor | 3807, 0 | lowFunds compared in float: FAIL 115/3807; lodestone pass skipped under low funds: FAIL 337/3807 |
| 0x4110f0 grids, 0x412f80 -> 0x411200 scatter / 0x411510 deposit | `classicplacement.h` | check_ai_placement | 4000, 0 | tower distance one draw x3: FAIL 464/4000; sacred-deposit refusal dropped: FAIL 348/4000 |
| 0x4dfee0 static bit (the reachability's mode 1) | `classicStaticPlane` | check_ai_static_grade | 3000 grids, 0 | last footprint row blocked: FAIL 2233/3000 |
| 0x4e1570 rectangle reachability | `sim::retailRectangleReachable` | check_rectangle_reachability | 20000, 0 | (sim oracle from before the branch; not re-planted here) |
| 0x51d837 construction allocation | `sim::RetailConstructionResources` | check_construction_economy | 8192, 0 | (sim oracle from before the branch; P4a planted "gates on the store fill" through check_ai_factory_think) |
| 0x412000 threat, 0x411ae0 enemy query | `classicThreat`, `classicEnemiesNear` | check_ai_threat | 4000, 0 | threat counts dying units: FAIL 195/4000 |
| 0x40d370 combat-member planner | `classicPlanCombat` | check_ai_defence | 4000, 0 | wounded radius below 1/2 HP: FAIL 342/4000 |
| 0x40d010 Monarch retreat | `classicRetreat` | check_ai_retreat | 4000, 0 | points drawn within r - 1: FAIL 4000/4000. Ties keep the last: **missed** (two points never tie on random input; accepted, see below) |
| `retailAiPatrolPoints` (0x40d370's second point) | `sim::retailAiPatrolPoints` | check_ai_patrol_points | 4000, 0 | second point 95 px apart: FAIL |
| 0x40b320 tail: quota growth, 0x40a7b0, launch, re-rolls, transfer/expansion gate | `classicwaves.h` | check_ai_launch | 4000, 0 | VTOL launch adds to the strike's p3: FAIL 85/4000 |
| 0x40a200 target picker | `classicPickTarget` | check_ai_target_pick | 4000, 0 | cloaked targets scored: FAIL 1881/4000 |
| 0x40e180 strike (0x40abd0 cohesion, 0x40e120 retreat base) | `classicwaves.h` | check_ai_strike --routine strike | 4000, 0 | cohesion's slip "fixed": FAIL 343/4000 |
| 0x40ea30 backup | `classicwaves.h` | check_ai_strike --routine backup | 4000, 0 | moves to its own centroid: FAIL 2157/4000 |
| 0x40eb40 VTOL squad (0x40ee50) | `classicwaves.h` | check_ai_strike --routine vtol | 4000, 0 | busy squad re-plans 29/30: FAIL 1492/4000 |
| 0x4e0a20 zone labels | `classicZoneLabels` | check_ai_zone_labels | 3000 grids, 0 | site rectangle sized like a start: FAIL 1949/3000 |
| 0x40b050 / 0x40b140 / 0x40b230 targets, 0x40b9e0 transfer | `classicexpand.h` | check_ai_transfer | 4000, 0 | zone mismatch no longer uses up one of n: FAIL 147/4000; target ties keep the last: FAIL 262/4000 |
| 0x40bc20 expansion | `classicExpand` | check_ai_expand | 4000, 0 | site ties keep the first: FAIL 21/4000; enemy radius 49 px: FAIL 8/4000 |

## Ported code without an oracle of its own

Each is an accepted limit; why:

| What | Where | Why there is no oracle |
|---|---|---|
| Profile file choice (.ota `aiprofile`, `ai\<name>.txt`, fallback to `ai\default.txt`) | `classicMapAiProfile`, `classicProfileScript` | file selection, read statically (0x4c8608, 0x4f6860); classic_ai_data checks it on the shipped data; classic_ai_map_profile checks it is read from the room's layers (a game pack's `ai/default.txt` over a map archive's over the base's) |
| The canbuild menu (0x4c1d35) and its order | brain `menu_` | our build tree sorts ties by name and defaults a missing Priority to 99 (retail: listing order, 9999999999); the order only maps RNG values to types, not the odds (plan §1.6) |
| The type facts (armed, builder, yardmap 'S', minwaterdepth, cost ...) | `ClassicTypeInfo::from` | not a routine: derived from the FBI; check_ai_printweights reproduces all 20 captured tables from them |
| The count recount 0x4119d0 | brain `bookkeeping` | a count of own units by type; covered by the PrintWeights reproduction |
| `massattack` keyword (0x4c8541..0x4c8575, sscanf `%i` x 30) | `classicMassAttackTicks` | parser, read statically; classic_ai_test checks it on the forms `%i` takes |
| The visible-enemy list 0x411700 | `World::visibleEnemies` + `ClassicBrain::refreshVisible` | it rests on our sim's vision, which is not retail's (plan §1.11); the brain only adds the sites still being built the World leaves off (U16) |
| The Monarch alarm 0x409a50 | sim `World::retailHitResponse` | sim code from the fire sweep work; emulated in the research scratch (plan §1.6), no repo oracle |
| The order glue (Build / Train / Move / Patrol / Stop / Stance commands, a Move's arrival radius, the first-mission snapshot) | `ClassicBrain` | our commands are not retail's missions (U1, U13); shown by the game tests below |
| Seeding (`seedFor`) | `ClassicBrain` | a deliberate divergence (plan §4.2) |

## Game tests

On build-o2, with the engine's own data (`TAK_TEST_DATA`): classic_ai, classic_ai_data,
classic_ai_bases, classic_ai_raid, classic_ai_u13, classic_ai_waves, classic_ai_duels and
classic_ai_expansion pass, with ai_behavior, ai_behavior_data, naval_ai, retail_ai and
settings. With U16 in (below), classic_ai_duels' three pairs end after 31.6, 15.5 and 24.1
game-minutes (Aramon, Zhon and Aramon win; 54.3, 24.9 and 40.9 before). `--mpai` Inner
Circle seed 1 60 s: Retail b353410003e98dd5 and Legion 0825aeb9e8139203 unchanged;
Classic (`TAK_AI_LEVEL=6`) 14849c00bc6e2a19 twice, unchanged. The campaign test is in
[the campaign section](#campaign) below.

## Unknowns at P8

**U8 (where the AI meets the unit cap) — settled statically.** No AI routine compares
anything with the unit cap except the target picker's divisor (the cap word at game
+0x14e7c, 0x40a46a). The cap is applied where a unit is created: 0x512060 (called from
the factory and conjure paths, scenario placement and scripts, 16 call sites) refuses
when the owner's population word (player record +0x24ec, counted at creation, unfinished
units included) equals the cap, after the profile limit 0x409890 when its caller asks
for it. So the AI's planners order freely and the order fails when the unit would be
created. Our sim does the same at the same point (`World::atUnitCap` when production,
a conjure or a spawn starts); it refuses at population >= cap where retail tests ==,
which is the same while the population cannot pass the cap. No change; no near-cap
Classic game test was run (accepted).

**U16 (does a structure being built carry unit flag 0x2000) — settled statically: no;
the port is changed.** Unit creation (0x512060 -> 0x511f60 -> 0x511990) sets 0x20 on every
unit, finished or not (0x511b18..0x511b2d), and leaves +0x108 at 1.0 for a site still to
be built; nothing on that path sets 0x2000. The only writers of 0x2000 in normal play are
the scenario placement loop (0x514c92..0x514cb3, from bit 7 of the placement record's
byte +0x27) and the flag restore of a loaded game (0x513xxx); MakeSelectable (0x401450)
clears it. So 0x2000 is "a scenario unit not yet made selectable", not "unfinished" as
P3 read it (U6). Consequences, now in the brain: an enemy site still being built is on
the visible-enemy list (0x411700 drops only 0x2000), counts as a threat, is a defence
candidate, and the target picker scores it with its /(rand(3)+1) unfinished divisor. The
World's own list leaves such sites off (it serves the sim's other auto-targeting), so the
brain keeps its own copy, rebuilt when the World's is (`ClassicBrain::refreshVisible`):
the World's list plus the unfinished enemies it skipped, under the same test. Our units
never carry retail's 0x2000. The Classic `--mpai` 60 s hash is unchanged by it (no enemy
site is in sight that early); longer Classic games change (the duels above); the other
levels are untouched.

## Accepted limits

Everything still open at P8, in one place:

- **Exact ties in the Monarch's retreat** (0x40d010 keeps the first of equally safe
  points): random cases never produce two points at the same squared distance from the
  nearest enemy, so check_ai_retreat cannot see the tie rule; read statically.
- **U6 remainder**: unit +0x104 is taken as always 0 and the holder bit (+0xa8 ->
  0x4000000) as never set for a carried unit; their setters were not traced.
- **U8**: no Classic game test near the unit cap; the sim refuses at >= where retail
  tests == (above).
- **U9**: a retail game cannot be replayed from a seed, so whole-game comparison with
  retail is not a gate (plan §5).
- **U12**: the picker's unit cap is our lobby's (default 2000), not retail's 150 / 500.
- **U14**: retail's player kind on disconnect, and whether a skirmish AI can ever ignore
  its limits (+0xe3), were not read; our disconnect takeover is Normal by decision.
- **V8 / V11 leftovers** (plan §7): whether the wave roll's E counts the player itself
  (the port skips every allied record, its own included, as P6 read it), and the setter
  of unit +0x130 0x8000000 ("armed"; the port derives it from the type's weapons).
- **Campaign start records**: for a mission the brain's start table is the .ota's
  StartPos specials (as for a skirmish map); a mission seat has no start of its own
  (base 1 is never anchored in scenario mode 1, so retail reads none). Inferred, not
  read from the scenario loader.
- **The sim is not retail's** (plan §4.4): economy, build times, vision, pathing and
  combat are ours, decisions land one tick later than in retail and the brain draws from
  its own RNG stream, so games differ from retail's even where every decision rule
  matches; the oracles check rules, not outcomes.
- **Order glue** (U1, U13): retail's missions are mapped onto our commands (a Move with
  an arrival radius, a Stop for a mission clear, a Stance 0 for standing orders).
- **The canbuild menu order** and **seeding** (table above).
- **The visible-enemy list** follows our vision (table above).

## Campaign

Every mission's strategic computer players play Classic: there is no choice (the
0.8.11 prereleases' campaign-book **Opponent AI** line, its `campaignAi` setting and
CreateGame's optional AI byte are gone; an old `campaignAi` key in settings.ini is
ignored). Each strategic player gets a Classic brain over the mission's `aiprofile` file
(looked up, like a skirmish map's, in the server's view of the room), the .ota's
StartPos table, scenario mode 1 (the first assignment pass's pre-placed rules: sacred-site
buildings, then builders, join the nearest base within its ring-1 radius, else the first
empty base, else 96; base 1 is never anchored) and its Player line's `massattack`, which
from that tick sends every armed mobile unit to slots 98 / 97. The disconnect takeover
stays Normal.

Headless runs (`takclient --mpmission`, takserver `--seed 1`, build-o2, no settings and no
`TAK_AI_LEVEL`: the harness plays Classic like the book). The Normal mission hashes
recorded before (takmission08_dh 300 s 68a4b1588012db76, takx11_mt 240 s
c5e3b08d1f08ba9e) no longer apply: missions no longer play Normal.

| Mission | Opponent | Time | Result |
|---|---|---|---|
| takmission08_dh | Classic (no choice) | 300 s | hash 5c35c83f41cbae5f, the reference; server log `... profile=mission08 CLASSIC (ai/mission08.txt)` (since 2026-10-23 `... DEFAULT AI (ai/mission08.txt)`) |
| takmission08_dh | Classic | 300 s | no fault (err=none); spends its 900 mana in 30 s, builds 5 units, sends 18 units out |
| takx13_mt (`massattack 600`) | Classic | 660 s | no fault; the 82 monsters patrol their ground until 600 s, then all 76 left march on the player (mean distance 3804 -> 2604 px in 60 s) |
