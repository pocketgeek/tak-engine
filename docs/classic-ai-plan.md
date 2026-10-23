# Classic AI level: retail AI synthesis and implementation plan

**Status:** P0 to P8 are done, on `task-classic-ai` (not yet on main); P8's acceptance ledger is [classic-ai-acceptance.md](classic-ai-acceptance.md). Since 2026-10-22 the
retail game is no longer run: each routine is checked by an emulation oracle and our own game tests
(§5); phase C stays as optional tooling. P0 (plumbing, protocol
290): `Difficulty::Classic` = 6, the lobby's CLASSIC entry, and a `ClassicBrain` stub
(`src/ai/classicbrain.{h,cpp}`) that takes every tick and does nothing yet, so a Classic Monarch
idles. Phase C (comparison harness): `tools/re/retail_ai_harness.py`, `retail_ai_poll.py`,
`tak_ai_run.py`, `compare_ai.py` and `README-retail-ai.md`, plus a debug-only takserver roster
dump (`TAK_ROSTER_DUMP`, `src/server/rosterdump.{h,cpp}`); its teardown no longer leaks the
display-holder (ctest `retail_ai_harness_teardown`). C's original self-test gate (two same-seed
retail runs give the same build sequence) cannot be met: plain `-AutoStop` re-seeds retail every tick (every seed plays the same game), so the
harness turns that off at tick 1, and after that same-seed retail runs agree only until the first
building is placed. A seed varies retail runs but cannot replay one (U9), which is one reason
whole-game retail runs are no longer a gate. P1 (profile) is on the branch too: `src/ai/classicprofile.{h,cpp}` bakes
retail's pref/limit tables from the map's `aiprofile` file (server-side, read once per game), checked
by `classic_ai_test` and by `tools/re/check_ai_profile.py`, which runs retail's own script executor
over random scripts (8000 scripts, 0 mismatches). Notes from that oracle: retail's tokenizer also ends
a line at `#`; the weight product is truncated at 53-bit x87 precision (Unicorn's default control
word would round `100 x 0.29f` up to 29, so the oracle sets the C runtime's 0x027f, which is an
assumption about retail's live control word; shipped weights are all integers, where it cannot
matter). `ClassicBrain::seedFor` also steps off the Park-Miller fixed point 0x7fffffff, a deliberate
divergence from retail's seeding. P2 (type info + priority clock) is on the branch as well:
`src/ai/classictypes.{h,cpp}` derives retail's per-type facts from UnitType plus a block of raw FBI
words (`UnitType::classicFbi`, read by nothing in the sim), and the brain keeps retail's bookkeeping
clock: the creation pass as the brain is built (empty caches, every limit -1, population 0, an
empty store), then each tick the >= 7-tick window's rand(30) (0 recomputes every priority from the
PREVIOUS tick's counts, as 0x411a90 recounts last), then the recount. Priorities go through `sim::retailBuildPriority` unchanged via an adapter
(`ClassicTypeInfo::priorityInputs`; the template's field names are explained in a comment, not
renamed, so no existing caller changes). A debug-only `TAK_CLASSIC_WEIGHTS` dump writes the table in
PrintWeights' layout. All 20 captured retail PrintWeights tables (5 factions) are reproduced exactly,
pref, limit and priority, without running retail (U11: `tools/re/check_ai_printweights.py`, opt-in
ctest `classic_ai_printweights`); that check found and fixed the creation pass's inputs and retail's
`tarnecr2` table entry. P3 (squads + assignment) is on the branch too: `src/ai/classicsquads.h` holds
retail's AI manager -- the constructor's 98 squads, base quotas and wave rolls (built before the
build manager, as 0x4f6660 does), the dying-unit cleanup and the assignment pass -- and the brain runs
retail's scheduler through the shared `retailTickAiSchedule`/`retailTickAiSquad` templates before its
bookkeeping each tick. The Monarch joins base 1 anchored on its start (no order); every other
finished unit joins the nearest base with a Stance 0 command (retail's standing orders); the campaign
massattack path (Stance, Stop, slot 98) is in (wired to the campaign at P8). Checked by
`tools/re/check_ai_squad_ctor.py` (0x40fed0, 3000 constructions) and `check_ai_assignment.py`
(0x40fe80 + 0x40eeb0 with retail's own set-squad/nearest-base/centre/radius code, 4000 passes), each
shown to fail on planted bugs, plus the reused scheduler/squads/base-selection oracles (opt-in ctest
`-DTAK_CLASSIC_ORACLES=ON`), and by an Abnar's Terrace game test (classic_ai_data). The squads' own
thinks still only reschedule, so a Classic seat still builds nothing. P4a (factory planner) is on the
branch too: each occupied base's think runs retail's member loop (`classicBaseMembers` in
`classicsquads.h`: stale anchors dropped, no centre or ring-0 radius -> nothing, then the ready-member
filter and the dispatch by menu/structure/navigator), and a ready factory goes to retail's factory planner
(`src/ai/classicproduction.h`: `classicPlanFactory` over `classicMenuDraw`, the shared
`retailAiPlanFactory`/`retailAiChooseBuild`/`retailAiBuildWeight` underneath) on the brain's stream, which
queues one unit at a time with a Train command. The brain keeps each FBI builder type's menu in canbuild
[Menu] Priority order. Checked by `tools/re/check_ai_factory_think.py` (retail's economy step 0x51d837 then
0x40b320 to the end of its member loop, with the real 0x40dcb0/0x412d00/0x412c90/0x409890/0x410de0 under
it; 4000 cases, shown to fail on four planted bugs), the reused check_ai_factory_plan/build_choice/
build_weight (now in the opt-in oracle set), and an Abnar's Terrace game test (classic_ai_data: an
Aramon keep, a Veruna keep and a Taros dungeon given at the start train menu units one at a time within
the profile limits; a custom profile limiting all but two Swordsmen to 0 gets exactly two Swordsmen). U3
and U4 are settled. P4b (constructor planner + placement) is on the branch too: a ready mobile builder of a
base (the Monarch, builders, Zhon's and Creon's conjurers) runs retail's constructor planner
(`classicPlanConstructor` in `src/ai/classicproduction.h`: the Monarch's retreat threshold, threat and
firepower branches, the builder gate against p1 and the alarm deadline the sim already keeps
(`Player::aiThreatUntil`, 0x409a50), the ordinary draw with the duplicate-factory rule and the 4-tower cap,
the lodestone draw on every think, reachability, a Build command), and the picked type is placed by
retail's placers (`src/ai/classicplacement.h`: the block grids the build manager's constructor draws, the
30-attempt scatter with the tower distance and the sacred-deposit refusal, the nearest free deposit).
Checked by `tools/re/check_ai_placement.py` (0x4110f0 + 0x412f80 -> 0x411510 / 0x411200 with retail's own
trig and cell lookup; 4000 cases) and `tools/re/check_ai_constructor.py` (0x40c890 with the real centre,
radius, counts, firepower, shot cost, menu draw, busy-deposit test and placers under it; 4000 cases), each
shown to fail on planted bugs, plus check_ai_base_radius in the opt-in set, and by an Abnar's Terrace game
test of all five factions from a bare start (classic_ai_bases, 10 game-minutes each: lodestones, structures
and units, sites within the ring radius, no stall). U1 is settled for the class bytes and the build flag.
A review of P3-P4b then fixed two glue gaps: an order now updates the base think's member snapshot at once, as retail's does (two builders were sent to one deposit in one think), and the reachability walk reads retail's static bit, set at map load only (`classicStaticPlane`, checked by `tools/re/check_ai_static_grade.py`), not the live nav grid. P5 (base defence, idle units, the Monarch) is on the branch too: `src/ai/classicdefence.h` holds retail's threat test 0x412000 and the enemy query 0x411ae0 under it (over the World's visible-enemy list), the combat-member planner 0x40d370 (ring by role, the wounded radius, the threat candidates and the keep-what-you-are-doing test, the candidate and centre points, the shared `retailAiPatrolPoints` without candidates, and the centre / first-point / patrol orders) and the Monarch's retreat 0x40d010. The base think now answers its threat test, and plans every mobile menu-less member through 0x40d370; the constructor planner's two calls (the retreat, and the defence planner it always calls last) are wired, so U2 holds as retail has it: 1/50 of a builder's thinks (and every think of an armed one under threat whose candidates are all out of its reach) replace the build just ordered with a patrol. The orders are Move and Patrol commands: retail's queue-0 order clears the missions (a plain Move), a queued one appends (queue 1); a Move whose mission +0x4e is r (the centre leg) or 16 (the retreat) ends within that distance, so the brain drops it when the unit is already that close and otherwise moves it to the circle's edge. The alarm 0x409a50 is the sim's (`Player::aiThreatUntil`, read by the constructor planner since P4b). A unit's first mission now carries its descriptor's flag dword, target and goal (U1), with run-time 0x2000 on an empty queue and on a queue of the World's own auto-attacks. Checked by `tools/re/check_ai_threat.py` (0x412000 + 0x411ae0, 4000 cases), `check_ai_defence.py` (0x40d370 with the real centre, radius, enemy query, trig and weapon class under it; 4000 calls) and `check_ai_retreat.py` (0x40d010; 4000 calls), each shown to fail on a planted bug, plus check_ai_patrol_points in the opt-in set, and by a raid game test (classic_ai_raid, Abnar's Terrace: idle soldiers patrol inside the base ring; Veruna raiders inside the ring-0 radius send them at the raiders; the Monarch at full HP stays, below its threshold it retreats to a point inside the ring-0 radius away from them). P6 (waves) is on the branch too: `src/ai/classicwaves.h` holds the rest of the base think after its member loop (quota growth, the ready counts 0x40a7b0, the transfer and expansion calls, the launch into the first free strike's backup, a VTOL squad or a naval strike through the member mover 0x40b8e0, the wave re-rolls), the strike think 0x40e180 with its cohesion pass 0x40abd0 and retreat base 0x40e120 (dissolve, target keep / re-pick, withdrawals home, contact and march moves), the backup's group move 0x40ea30, the VTOL squad's random patrols 0x40eb40, and the omniscient target picker 0x40a200; the brain wires them into the scheduler. Strikes, backups and VTOL squads give only Move and Patrol (and Stop for retail's mission clear) commands: U13 is settled, our Move is retail's MOVE_GROUND. The P7 routines the base think calls (transfer targets 0x40b050 / 0x40b140 / 0x40b230, the transfer 0x40b9e0, the expansion 0x40bc20) answered "none" until P7 ported them. Checked by `tools/re/check_ai_target_pick.py` (0x40a200, 4000 picks), `check_ai_strike.py` (0x40e180 / 0x40ea30 / 0x40eb40 with the real cohesion, set-squad, retreat base, firepower, base centres, threat test, set-active and group order under them; 4000 thinks each) and `check_ai_launch.py` (the whole 0x40b320 with its planners stubbed; 4000 thinks), each shown to fail on a planted bug, and by game tests: classic_ai_u13 (a column on Move walks through enemies firing and never stops; on AttackMove it stops to fight), classic_ai_waves (Abnar's Terrace: a wave launches, the strike picks a target and marches on it by Move only, a wounded member is sent home, the wave cut down to its gate dissolves and its survivors rejoin the base) and classic_ai_duels (Classic against Classic, the Monarch not expendable, on Edmont's Field: Aramon v Veruna, Taros v Zhon and Creon v Aramon all end with a winner, after 73.4, 24.2 and 27.4 game-minutes). P7 (expansion and transfers) is on the branch too: `src/ai/classicexpand.h` holds the zone labels retail gives each start location and sacred site of a navigator grid at map load (0x4e0a20, U5), the three transfer-target searches 0x40b050 / 0x40b140 / 0x40b230 (the nearest other base short of builders, ground or air units), the transfer 0x40b9e0 and the expansion 0x40bc20 (a free builder, else a builder, founds a new base anchored on the nearest free sacred site in its zone, refused when three times the enemies within 50 px exceed the escort), wired into the base think's hooks; the server now hands the brain the map's whole start table (U5's labels read every record). Checked by `tools/re/check_ai_zone_labels.py` (0x4e0a20 with the real reachability walk; 3000 grids), `check_ai_transfer.py` (the three searches and 0x40b9e0; 4000 cases) and `check_ai_expand.py` (0x40bc20; 4000 cases), each shown to fail on a planted bug, and by a game test, classic_ai_expansion (Abnar's Terrace, all five factions, 20 game-minutes: 2-4 expansion bases each from minute 5-10, lodestones from 1-2 at 5 minutes to 3-6 at 20, every site inside its builder's base ring); with the expansion in, classic_ai_duels' three pairs now end after 54.3, 24.9 and 40.9 game-minutes (Aramon, Zhon and Aramon win). A review of P5-P7 then found check_ai_expand blind to its own radius (a planted 48 or 52 px passed: few enemies stood near the chosen site); half its cases now ring every site with enemies at 44..56 px, and a 49 or 51 px radius fails. It changed no engine code; classic_ai_test --duels gained hand-run options (TAK_DUEL_PAIRS, TAK_DUEL_LEVEL for a Normal seat, TAK_DUEL_LOG / TAK_DUEL_WATCH, per-seat command counts). P8 (acceptance) is done: every ported routine's oracle passes and was shown, in one re-run at one head, to fail on a planted bug (39 plants; one missed plant made check_ai_assignment stronger, one is a tie the random cases cannot reach), listed with the routines that have no oracle of their own and every remaining open item in [classic-ai-acceptance.md](classic-ai-acceptance.md). U8 is settled statically (the unit cap is met at unit creation, 0x512060, not in any AI routine; the sim refuses at the same point). U16 is settled statically and changed the port: retail creates every unit, a site still being built included, with 0x20 and without 0x2000 (0x511990), so 0x2000 means a scenario unit not yet made selectable, not "unfinished" (U6's reading); the brain now keeps its own visible-enemy list (the World's plus the enemy sites still being built, rebuilt with the World's) and never sets 0x2000, so such sites threaten, are defence candidates and are picked with the unfinished divisor. The Classic --mpai 60 s hash is unchanged by it (14849c00bc6e2a19); classic_ai_duels' pairs now end after 31.6, 15.5 and 24.1 game-minutes (Aramon, Zhon and Aramon win). The static-plane and zone-label caches are deques (no reference into them moves). Campaign: the campaign book's **Opponent AI** line (Classic by default since 0.8.11's release, or Normal; `Settings::campaignAi`, saved as `campaignAi = classic | normal`, a missing or unknown value reading Classic) is sent with the mission room (an optional byte after CreateGame's private flag; the server accepts Classic and plays anything else as Normal); with Classic every strategic computer player of the mission gets a Classic brain over the mission's aiprofile file, the .ota's StartPos table, scenario mode 1 (the first pass's pre-placed rules, base 1 never anchored) and its Player line's `massattack` (slots 97/98). the server still plays a missing or unknown room byte as Normal; Normal missions (chosen on the book) are byte-identical (takmission08_dh 300 s 68a4b1588012db76 and takx11_mt 240 s c5e3b08d1f08ba9e before and after); a Classic takmission08_dh plays 300 s without a fault, spending its 900 mana and building, and in takx13_mt (massattack 600) the 82 monsters hold their ground for 600 s, then all march on the player. The disconnect takeover stays Normal.

2026-10-22. The research behind this plan (the phase-1 audit) was written before any code. The new
level is called **Classic** (lobby name CLASSIC, `Difficulty::Classic`, `ClassicBrain`); "retail"
below always means the original game, and **Retail** pathfinding (`PathfindingMode::Retail`) is
unrelated. Decisions taken: the brain runs server-side (§4.1); it keeps retail's AI bugs
faithfully; it is offered for skirmish and multiplayer AI seats, and for campaign opponents only at
P8 acceptance; the disconnect takeover stays Normal; the comparison harness is committed under
`tools/re`.

Sources: seven area notes (config, build, placement, squads, targeting, our port, comparison) and
the emulation scripts named below (`verify/`, `comparison/`, `emu_*`, placement runs). Those are
research scratch kept **outside the repo**; they are named only to say where a finding came from.
Repo oracles (`tools/re/check_ai_*`, `tools/re/emu.py`) are in the repo.
Nothing from the binary is reproduced here: routines are named by address and their behaviour is
described in our own words, as in [retail-engine.md](retail-engine.md).
Binary: `assets/game/KINGDOMS.icd` v4.0BB, ImageBase 0x400000. Retail runs 30 ticks/s.
`rand(n)` = the game RNG 0x535cc0: Park-Miller minimal standard (x16807 mod 2^31-1, Schrage). It
returns a value in [0,n), and it **returns 0 without advancing when n < 2**. One stream is shared by the
whole sim, so the order of draws is part of gameplay state.

Confidence: **S** = STATIC (read from disassembly), **E** = EMULATED (run in Unicorn or observed live in
retail under Proton), **I** = INFERRED. "repo-E" means one of the 13 existing `tools/re/check_ai_*`
oracles, which all pass today on build-o2 (ourport §7).

Where the area notes disagree, the plan settles it below and says how (see §1.12).

---------------------------------------------------------------------------------------------------
## 1. How the retail AI works, end to end

### 1.1 Who runs it, and when
| Item | Routine | Behaviour | Conf |
|---|---|---|---|
| Brain creation | 0x4f6910 -> 0x4f6660 | At game start, every player of kind 1/2/3 with an index (not 10) gets an AI manager (0x1a7 bytes, ctor 0x40fed0) at `player+0x80` and a build manager (0x121 bytes, ctor 0x4102f0, `0x62a33c[p]`). Humans have one too, so the debug `+AI` command can take a seat over in mid-game. | S, E (live `+AI`) |
| Per-tick entry | 0x40fe00 (from the player loop at 0x4f6d99) | It runs only for kind 2 (AI), when `player+0xe3` (limits/active, set for every skirmish player) is on and the global AI byte is on. It calls the scheduler 0x40fd80, then **every** seat runs the fire-at-will sweep 0x40f330 (already ported in the sim, protocol 252). | S, repo-E |
| Bookkeeping clock | 0x4121c0 -> 0x411a90 | Every tick it recounts own units by type (count and completed) in 0x4119d0. Every >= 7 ticks it rebuilds the visibility lists (0x411700). In that same window, `rand(30)==0` recomputes the dynamic build priorities (0x410810), giving a geometric gap with a mean of about 210 ticks. | S, repo-E |
| Scheduler | 0x40fd80 | An assignment countdown starts at 30. At <= 0 it resets to rand(7)+3, drops dying units (0x40fe80) and runs assignment (0x40eeb0). Then each squad 0..98 whose deadline has passed runs its think. | repo-E |
| Difficulty | none | There is no difficulty anywhere. The only mention of difficulty (a string at 0x605190) is in the weight debug print. The 14/10/7/5/1 table (0x40cfc0) is a **ring index**, not a difficulty. | S, E (lobby has no setting) |

### 1.2 Configuration: ai profile file -> per-type preference and limit
| Item | Routine | Behaviour | Conf |
|---|---|---|---|
| File choice | map loader 0x4c8608..0x4c868e, loader 0x4f6860 | The profile is chosen **per map/mission** from the `.ota` `aiprofile=` key and applied to every AI player. It is `ai\<name>.txt`, or `ai\<name>_<variant>.txt` when a language variant exists (none ship). An empty key gives `ai\default.txt`. If the named file can't be read, it falls back to `ai\default.txt` (@0x617080). There is **no per-faction file**. All 55 skirmish maps and the 44 campaign .ota files say DEFAULT. | S |
| Effective default.txt | HPI precedence | IPData.hpi (12-17-99, 146 weight + 146 limit lines, all 5 factions) shadows data.hpi and V3Rocket. Confirmed live: the weight debug print shows IPData's limits. | S, E |
| Executor | 0x538f00 | Lines and whitespace tokens, case-insensitive, `//` comments. It runs with mask -1, so any console command would execute (none ship). Weight takes a float, limit an int; a missing value counts as 0. `plan` is a no-op. | S, E |
| Name -> types | 0x517240 | An exact unitname sets one bit and **locks** the type. Otherwise the word is a FBI `category=` word (or `ALL`): it ORs the category in and does NOT lock. An unknown word is a no-op. | S |
| weight | 0x409760 -> 0x410c80 | For every player with a brain: `pref[t] = clamp(trunc(pref[t] * w), 0, 100)` for unlocked types. **pref starts at 100** (reset 0x410670). Explicit unit lines are first-writer-wins. Category lines compound. Every shipped weight is an integer, so **every non-zero weight gives 100: the file's weights are an on/off switch.** | E (config emulation; the live weight debug print agrees) |
| limit | 0x4097f0 -> 0x410d50 | For kind-2 players: `limit[t] = N`, first-writer-wins. Default -1 = unlimited. | S, E |
| Unlisted types | | They keep pref 100 and limit -1. So `araseige` (a typo) leaves **arasiege unlimited**, and `aratrans`, `vercen` and `npctemp` are also unlisted. | S, E |
| FBI ai_weight/ai_limit | 0x410f20 / 0x410fe0 | Only for `downloadable=1` types. Retail bug: both passes execute ai_weight. No shipped FBI uses them, so it is dead on stock data. | E |
| Limit check | 0x409890 -> 0x410de0 | Allowed if the player index >= 10, or `+0xe3 == 0`, or limit == -1, or count < limit (count includes unfinished units, from the cache). | S, repo-E |

### 1.3 Build choice
| Item | Routine | Behaviour | Conf |
|---|---|---|---|
| Menu | type+0x132/+0x12e, loaded at 0x4c1d35 | The builder type's canbuild list, sorted ascending by `[Menu] Priority`. The order only changes which RNG value maps to which type, not the odds. | S |
| Entry weight | 0x412c90 | 0 if over its limit, else `pref * priority / 100` (integer division, priority a signed byte). | repo-E |
| Draw | 0x412d00(profile, unit, special) | Reservoir sampling: for each entry with w > 0 and of the requested class (`special` = yardmap[0]&0x80, i.e. lodestones), `total += w; if rand(total) < w, pick it`. That is one draw per kept entry. A winner from a different side (+0x8a) gives 0 (a wasted think). No cost, cap or tech check. | repo-E |
| Tech | | **Retail has no tech tree** beyond the canbuild menus. | S |
| Dynamic priority | 0x410810 (every type, about every 210 ticks) | Base 21 if armed, else 1. **Builder types** (FBI builder) with none unfinished get a bonus against their limit L: L<=2 (including -1) gives +15; count<=L/4 +30; count<=L/2 +10; else +6. In shortfall these bonuses are /3, /2, /2, /2. **Lodestone types** (yardmap 'S'): fill<0.3 gives +30, and fill<0.01 another +30. **Cost > 500**: X = trunc((cost-500)*0.01), with the population P compared to rand(30)+10, then rand(40)+40, then rand(80)+80. Below the first gate it subtracts rand(min(p,X)) three times (twice for builders); below the second, twice (once); below the third, rand(min(p,X))/2 once. Then count==0 gives x4 and count==1 x2. **Naval** (minwaterdepth >= 0) gives x3. isfeature gives 0. A lodestone gets 0 unless fill < 0.3, shortfall is on and none is unfinished. Clamp to 100. | repo-E (check_build_priority, 8192 cases) |
| Shortfall | 0x401330/0x401350 | The mean of the oldest 29 of 30 per-tick (income - usage) samples is < -0.01, or it is within ±0.01 with stored < 0.01. Fill = stored/capacity (0 when capacity <= 0.01). | repo-E |

Consequences: the **adaptive part is the priority**, not the file weight. The AI rebuilds what it lost
(x4/x2), keeps builders up to their limits, defers expensive items until its population grows (gone at
about 160 units), and seeks lodestones only while the store is under 30% and draining. A live
weight debug print at the start of a game is consistent with the formula (an armed type with none
built: 21 x 4 = 84).

### 1.4 Squads (the army/base object model)
| Item | Routine | Behaviour | Conf |
|---|---|---|---|
| Slots | ctor 0x40fed0 | 1..20 Base (think 0x40b320). Odd 21..49 ground Strike (0x40e180, cohesion p1=200000, backup = slot+1). Even 22..50 Backup (0x40ea30). Odd 51..79 **naval** Strike (cohesion 500000) with their backups. 81..95 VTOL (0x40eb40). 96 plain AISquad ("homeless", think only reschedules +1800). 97/98 campaign mass-attack strike (1000000) and its backup. That is 98 allocated, matching the live count 20/31/31/15/1. | S, E (live squad poll) |
| Base ctor | 0x40c560 | Slot 1: p1 = rand(2)+1 desired builders, p2 = rand(4)+2 ground reserve, p3 = rand(4) air reserve, p4 = rand(4) naval reserve. Other slots: rand(2)+1, rand(2)+1, rand(2), rand(2). Then it rolls the three wave sizes p5/p6/p7. | E (2000 seeds) |
| Categories | 0x40a5f0 | 0 all, 1 structures, 2 constructors (excluding the commander), 3 free builders, 4 ground combat, 5 air combat, **6 naval combat** (armed, floater, minwaterdepth > 0). | repo-E (check_ai_categories) |
| Assignment (P3 detail, E: check_ai_assignment) | 0x40eeb0 | The first pass gives the commander **no** standing orders (only later units get 0x5198a0). Leaving a squad swaps its last member into the leaver's place (0x50b2c0), joining appends, so member order is not join order. Dying units go to squad -1 (0x40fe80) and are never reassigned. Campaign mode 1's first pass: sacred-site buildings, then unassigned builders, join the nearest base if within its ring-1 radius (the distance's whole-pixel part after `_ftol`), else the first empty base 2..20, else 96; all bases empty -> every unit to 96. massattack takes every live armed unit with a navigator and no menu not in 97/98, **finished or not, the Monarch included**. Unit flag +0x130 0x20 is the MakeSelectable mission's "selectable" bit (0x401450 sets it and clears 0x2000) | S, E |
| Assignment | 0x40eeb0, every rand(7)+3 ticks | **Once at tick <= 30**: commanders go to base 1, and outside campaign mode base 1 is **anchored on the player's start location** (`player+0xec`, 0x40c670). Campaign mode 1 has its own pre-placed-unit rules. **Every pass**: each live, finished, unassigned unit gets standing orders 0x5198a0(u,2) (= our Stance 0: move 1 / fire 2) and joins the **nearest occupied base by centre** (0x40af70), else slot 2. | S, repo-E |
| Base centre | 0x40c610 | The anchor cell centre if anchored (a start location, or a sacred site for expansions), else the member centroid (0x40aa50), else the profile home (+0x45: the weighted mean of own finished units, 40 per building + 20 if it has a menu). | S, repo-E |
| Base radius | 0x40cf30 | `16 * max(trunc(sqrt(F)*s)/2, 5s)` px, where F = Σ(footX+footZ) of the base's structures and s = {14,10,7,5,1} for ring 0..3/other. At F <= 100: 1120/800/560/400/80. | repo-E (7168) |

### 1.5 Base think 0x40b320 (every rand(30)+15 ticks)
1. Reschedule. An empty base clears its anchor and returns. **threat** = 0x412000: any unit on the
   player's **visible-enemy list** within the ring-0 radius of the centre. (S)
2. Each ready member (alive, built, not carried) goes to: a structure with a menu -> factory planner
   0x40dcb0; a mobile builder (the commander always, others unless fighting) -> constructor planner
   0x40c890; everything else -> combat planner 0x40d370. (S)
3. Quota growth: if ownPopulation/(20 for slot 1, else 30) > p1, each of p1..p4 += rand(p/2)+1. (S)
4. Surpluses count only members with HP >= 3/4: freeBuilders = cat3 - (slot 1 ? 0 : 1);
   builders = cat2 - p1; ground = cat4 - (p5<0 ? p2 : 0); the same for air (p6, p3) and naval
   (p7, p4). Ready = surplus >= |wave size|. A **negative wave size means "keep the reserve home and
   send only the surplus"**. (S; the port's `tickRetailAiBase` computes these)
5. If builders > 0 and rand(2)==0, send one builder to the nearest other base below its p1 (0x40b050
   -> 0x40b9e0). Else, if max(builders, freeBuilders) > 0 and a free base slot exists, try to
   **expand** (0x40bc20, §1.9). If that succeeds, the escort surplus moves too and the used wave sizes
   are re-rolled. (S)
6. If ground > 0 and rand(10)==0, send one ground unit to the nearest under-quota base (0x40b140) and
   re-roll p5. The same for air. (S)
7. **Launch** (§1.7) when groundReady or airReady; naval separately. (S)

### 1.6 Production planners
**Factory planner 0x40dcb0** (repo-E, check_ai_factory_plan; P4a E: check_ai_factory_think):
- It does nothing if allocation < 0.7/3 (~0.2333) or the factory has a `BuildingBuild` mission anywhere in
  its mission list (the name at 0x605064, resolved by 0x4d4bf0; one unit at a time). Allocation is the
  owner's resource object +8, which the economy step 0x51d837 sets to stored / (demand x 0.03333333) when
  that is below 1, else 1 (U4). The port reads it as our World's `constructionAllocation` (the store over
  the tick's full-rate construction demand, capped at 1) and takes "a BuildingBuild mission" as a
  non-empty production queue.
- It picks with 0x412d00(special=0). If allocation < 0.7 and cost > 500: b = int((cost-500)/300+2),
  and it defers when rand(b) != 0 AND population < rand(30)+30. Otherwise it queues one: 0x429810(type,
  factory, 1), the same call a player's build button makes, which for a unit without a navigator adds one
  BuildingBuild mission of one unit (our Train command, count 1). It has no unit-cap check (U8 is
  elsewhere).
- It is reached from the base think's member loop (0x40b320) for a ready member (live, not dying,
  selectable 0x20, +0x108 == 0 where an unordered compare counts as equal, +0x104 == 0, not carried by a
  non-holder) whose type has a menu (+0x12e != 0) and that is a structure. A mobile member with a menu goes
  to 0x40c890 unless it is a non-commander whose first mission has class 1 or 2; one without a menu goes to
  0x40d370 if it has a navigator. Before the loop: a stale anchor (0x40c750 fails) is cleared, and with no
  centre or ring-0 radius the think ends; the threat test 0x412000 runs once, after the radius.
- The menu (0x4c1d35) exists only for FBI builder types: CanBuild\<unitname>\*.tdf, each file name looked
  up as a unit name (unknown ones dropped), a missing [Menu] Priority read as 9999999999, then a bubble sort
  ascending by Priority that keeps ties in listing order. Our build tree sorts by Priority with ties by name
  and defaults a missing Priority to 99 (the order only maps RNG values to types, not the odds).
- There is **no rally point**. The unit leaves by its exit, joins the base, and patrols (§1.8).

**Constructor planner 0x40c890(unit, threat)** (S; P4b E: check_ai_constructor, 4000 calls with the real
centre, radius, counts, firepower, shot cost, draw, busy-deposit test and placers under it):
- lowFunds = allocation < 0.7 (a double compare, so 0.7f itself and NaN count as low). Allocation =
  min(1, stored / this tick's construction demand) (0x51d837, U4), the factory planner's quantity.
- **Commander branch**: retreat threshold = maxdamage x k / (4 x m) (unsigned, maxdamage the type's dword),
  with k = 2 if its own mana/maxMana <= 0.5 (const 0.5 @0x5ebb38; NaN counts) and m = 2 if its current
  weapon is castable now (mana per shot over the cost multiplier 0x531ea0 below 0.01, or within its mana).
  So with full mana and able to cast it flees below 1/8 HP; with one of the two below 1/4; with neither
  below 1/2 (HP compared unsigned, so a negative HP is never low). This applies only when the base has
  ground firepower (0x40a9b0(4) > 0: damage per reload tick over its armed cat-4 members). Low with a
  threat: it cancels a build (0x4d6a50 if its first mission has flag 8) and retreats (0x40d010). Low
  without one: straight to the builder gate. Not low: a first mission of class 1 or 2 (attack, AirStrike)
  returns at once (no 0x40d370); with a threat and no ground firepower it cancels a build and defends
  (0x40d370). **Builder gate**: with cat-2 builders >= p1 it defends; before the **monarch alarm**
  (`mgr+0xd`) it retreats if it was low, else defends. Past both it builds like any builder.
- **Monarch alarm** 0x409a50 (from the damage path 0x51a340): when an AI commander is hit, deadline =
  now + 30 + rand(300)+rand(300)+rand(300) (30..927, mean ~478 ticks) and its build is cancelled. (E) The
  sim already runs this for every computer-owned Monarch (`World::retailHitResponse`, `Player::aiThreatUntil`,
  on the shared game stream); the brain reads the deadline.
- Every builder: if its first mission has flag 8 (a build), return (no 0x40d370). Otherwise (unless
  lowFunds) the ordinary pick 0x412d00(special=0):
  - A factory type (bmcode 0, menu count > 0) is dropped if a base member of that type has no first
    mission, or a first mission other than BuildingBuild: an idle factory of that type (S, E).
  - For the Monarch, a "tower" (bmcode 0, armed, menu count 0; no range test here) is dropped at >= 4 of
    that type (the owner's count 0x410e60, unfinished included) (E).
  - Site ring: Monarch 2 for the first of a type (owner's count 0), 3 for later ones; others 1 for a tower
    and 2 otherwise (S, E).
  - Site = 0x412f80 (§1.10). Reachability = 0x4e1570 (ported `retailreach.h`) from the builder's cell
    (unit +0x74, 1 x 1) to the site's footprint less builddistance/16 cells, sized footprint +
    (2 x builddistance)/16 cells, mode 1 (static grade), unbounded; a builder without a ground navigator
    (unit +8 -> +4 == 0, a flyer) skips it. Mode 1 passes a cell unless bit 8 of its navigator-grid nibble
    is set, and that bit is written only by the grid build 0x4dfee0 at map load (0x4e017c -> 0x4dfe90):
    where the footprint's minimum rating (0x508660) is 0 -- a footprint [x, x+footX) x [z, z+footZ) holding
    a cell rated 0, or running past the right or bottom edge. Buildings and units placed later never set
    it (S, E: check_ai_static_grade, 3000 grid builds). Port: `classicStaticPlane` over the World's search
    rating without bodies (feature, then terrain), built once per nav grid and builder footprint.
- **Lodestone pick, drawn every think** (lowFunds too): 0x412d00(special=1), ring 2 for the Monarch and 1
  otherwise. It is skipped while a base member's first mission has flag 8 and builds a mana-site type
  (0x40c830: type +0x264 bit 31, so a mana building counts, not only the lodestone class). The site is
  the nearest free deposit (0x411510). If found and reachable, it **replaces** the ordinary pick.
- It issues build order kind 14 (0x4de530/0x4d78a0) for the pick, then **always** calls 0x40d370.
- The AI issues no repair, assist, reclaim or guard orders anywhere (S, order-kind census).
- Port (P4b): a Build command at the site's footprint centre; the cancel is a Stop command; 0x40d370 and
  0x40d010 are P5. A unit's first mission is its first issued goal order (path waypoints spliced in front
  belong to it): a build order or a site it is building is MobileBuild (class 0, flag 8), an attack order
  class 1, a non-empty factory queue BuildingBuild (flag 0x0c), a patrol 7, guard 5, load 12, unload 13,
  reclaim 11, repair 6, a move 14 (U1). The weapon's reload for the firepower sum is the FBI reloadtime x 30
  truncated to a 16-bit tick count, as 5307e9 stores it. 0x4de530 is taken to resolve kind 14 for any
  builder. Retail's orders are immediate (0x429810 and 0x4d78a0 put the mission on the unit at
  once), so the brain updates a member's first mission in its per-tick snapshot as it orders it: a later
  member of the same base think sees that build (the busy-deposit test 0x40c830, the idle-factory rule).

### 1.7 Waves: launch, strike, backup, VTOL
- **Wave-size roll, ground 0x40c240** (S, re-read at the listing):
  E = max(10, the largest population among non-allied active players).
  p5 = rand(max(p2,2))+1. With 3/4 chance (rand(4)!=0) add rand(max(E/10,2)), and then, **only inside
  that branch**, with 2/3 chance (rand(3)!=0) add rand(max(E/5,2)) (so the E/5 term has 1/2 overall).
  **[verify-edit: nesting made explicit, 0x40c2ca/0x40c302 both skip to 0x40c32c]** Then: if rand(60)==0, x(rand(10)+5); else if rand(6)==0, x5; else if
  rand(2)==0, x(rand(2)+2). If > 50 -> rand(10)+41. With 1/2 chance negate.
  Air 0x40c3c0 and naval 0x40c490 are simpler: p6 = rand(max(p3,2))+1, then with 3/4 chance one more
  rand(max(p3,2)) (no second term, no E), then multiplier chances 1/100, 1/10, 1/2 (air) and 1/100, 1/20,
  1/3 (naval), the same cap and sign (p7 the same from p4). **[P3: emulated, check_ai_squad_ctor; the
  earlier "same shape" wording was wrong about the added terms.]** **The ground attack size tracks the
  biggest enemy, capped at 50.** (S; the squads distribution figures were E)
- **Launch** (0x40b320): take the first empty ground strike 21..49 (0x40deb0), and **reset its p2 and p3
  to 0** (0x40b77f..0x40b788) **[verify-edit]**. Move `ground` random cat-4
  members (HP >= 75%) into its **backup**; strike.p3 += n, strike.p2 += rand(2n/3); re-roll p5. Air: with
  2/3 chance it goes to a new VTOL squad 81..95, otherwise into the same backup. Retail bug, kept: the VTOL
  path writes p3/p2 onto the ground strike. Naval goes to 51..79 the same way. (S)
- **Backup 0x40ea30** (every 150 ticks): a group MOVE of all members to the strike's centroid. (S)
- **Strike 0x40e180** (every rand(60)+15 ticks; E for timing, reroll and dissolve):
  1. Cohesion 0x40abd0: seed it from the backup if empty. Demote the farthest member while
     dist² >= n x p1; promote backup members with dist² < n x p1 (radius ~447·√n px for ground).
  2. **Dissolve** when strike+backup count <= p2: everyone joins the retreat base (0x40e120: the nearest
     base with firepower >= max(100, own), else the nearest base, else slot 2). No move order, but each
     member's missions are **cleared** first (0x4d6a50(u,0); only missions with +0x5a bit 4 survive), so
     the units stop where they are until the base think re-plans them. **[verify-edit: added the clear,
     0x40e24c..0x40e2bc]**
  3. **Target**: keep it unless it is gone or dead, or rand(250)==0 (drawn only if still valid). Re-pick
     with 0x40a200(centroid, random member). If none is found, wake = now + rand(900)+150 (5..35 s) and the
     squad **stays where it is**.
  4. **Withdraw** a member that is unarmed or has no mover, or has HP < maxHP/(rand(3)+3) (20..33%), or
     (when changed or rand(20)==0) fails 0x4e1570 reachability to the target within weapon range. It is
     sent to the retreat base with a MOVE.
  5. R = max(10, max over armed members of max(sight, range)). **Contact** if any member is on an
     attack-class mission (1/2) or a visible enemy is within 1.5R: re-send MOVE to the target position to
     members with no mission, the interruptible flag 0x10000, or always if changed; others with 1/10.
     Otherwise **march**: MOVE_GROUND/VTOL_MOVE to the target's current position, with the 1/50 and 1/10
     refresh rules (target within 50 px of a goal).
  6. It only ever issues **MOVE**, never ATTACK. The fighting comes from the standing fire orders and the
     fire-at-will sweep.
- **Target picker 0x40a200** (S + E, an emulation of the picker matches a Python model): it scans **every** unit of
  every non-allied active player. Each must be on the map, not 0x2000, not cloaked, and damageable by the
  member's active weapon (0x531e50 > 0). Score = INT_MAX / ((rand(d1/8)+rand(d1/8))/4+1) /
  ((rand(d2/2)+rand(d2/2))/4+1), with d1 = octagonal distance from the centroid and d2 from the member
  (octagonal = max + min/4). Then /(rand(10)+rand(10)+1) if not visible to the AI; /(rand(5)+rand(5)+1)
  unless the type is a **mana site** (yardmap 'S': lodestones and mana buildings); /(rand(3)+1) if
  unfinished; /(rand(3)+1) if unarmed; /(rand(q)+1) with q = min(20, unitCap/max(20, ownerPop)). The
  first strictly higher score wins, and **only a new best is checked for reachability** (0x4e1570, range =
  weaponRange/16, min 1), if the member has a navigator. **The AI knows where every enemy is**:
  visibility only weights the pick. It strongly prefers enemy lodestones and mana buildings, the
  biggest enemy, and near targets.
- **VTOL 0x40eb40** (every rand(30)+15 ticks, S): the wounded go home. A busy squad keeps its task with
  29/30 chance. Otherwise the whole squad gets a PATROL to a random point: 1/2 anywhere on the map,
  1/4 a top or bottom edge point, 1/4 a left or right edge point. **Air never targets; it sweeps.**
- **P6 port notes** (S + E: check_ai_target_pick, check_ai_strike, check_ai_launch). The strike's centroid is
  taken once, before the dissolve test, and serves the retreat base, the picker and the contact radius
  (withdrawals do not move it). The cohesion pass sums the members' whole-pixel positions with no liveness
  test, and after a demotion it subtracts the position of the member set-squad has just swapped into the
  leaver's vector slot, not the leaver's (a retail slip, kept: a planted "fix" differs in 343 of 4000 thinks).
  A withdrawn member joins the retreat base, has its missions cleared, then moves to that base's centre with
  mission +0x4e = its ring-0 radius (the same "end within r" Move as P5's centre leg). The dissolve clears and
  re-squads members until the strike and its backup are empty; a member set-squad refuses (a dying one) would
  spin retail's loop forever, so the port stops there. The backup's group move and the VTOL squad's patrol
  (0x50b5d0) walk the player's unit table, not the member list. The launch's member mover clears each mover's
  missions; with no free VTOL squad the air surplus goes to squad 0 (it leaves every squad and the next
  assignment pass sends it to a base with standing orders); an air surplus that is not a ready wave rides in
  the ground strike's backup. The VTOL patrol's far edges are (size - 1) px. The ground roll's E skips every
  record allied to the AI, its own included (a player is allied to itself). The picker's (+0x130 & 3) == 1
  leaves flyers (2) and carried units (0) out, as P5's enemy reading has it; its "unfinished" divisor never
  applies in the brain, which reads 0x2000 as unfinished (U6 / U10) and so drops such units first.
- **massattack** (campaign .ota only, stored x30, per player): from that tick on, **every assignment
  pass** sends each armed, mobile, menu-less unit not already in 97/98 to slot 98 (Stance 2, missions
  cleared) -> 97, so later units follow too. It never fires in skirmish. (S) **[verify-edit: was "at that
  tick"; 0x40f1a1..0x40f239]**

### 1.8 Base defence and idle units: 0x40d370 (S; P5 E: check_ai_defence, check_ai_retreat, check_ai_threat)
- **Threat 0x412000** (from the base think, ring-0 radius): a live, not dying unit of the visible-enemy
  list (build manager +5, 0x411700) with hi(dz²) + hi(dx²) <= r² (the high dwords of the 64-bit squares of
  the 16.16 differences, added and compared as 32-bit ints, so far pairs wrap). **0x411ae0** lists those
  units the same way; when none is and the player owns a targeting-upgrade unit (+0x79) it scans list
  +0x25 instead. No shipped FBI has istargetingupgrade (U7).
- idle-ish = no mission, or mission flag 0x2000 (the type's default mission, and anything later pushed in
  front of it; U1). Unless armed under a threat, a busy unit re-plans only on rand(50) == 0.
- Ring by role: 0 when armed and threatened, else 1; commander 1 (2 if it can't fly) with a threat, 3
  without; builder 2; flyer 0. The radius falls back to 320 px. Below 1/4 HP (unsigned, maxdamage >> 2)
  it is max(R/5, 160).
- Armed with a threat: candidates = the units 0x411ae0 lists within max(sight, range) + R of the centre
  that are live, ready (no 0x2000), uncloaked, on the ground for a melee weapon (MeleeWeapon's vtable
  +0x14) and take damage (0x531e50 > 0). None and busy: rand(50) != 0 ends it. A busy unit then keeps
  what it is doing (rand(50) != 0) as soon as one candidate is its attack target (0x200), or within
  max(sight, range) of its mission's goal point (0x400), or of the unit itself.
- Points: the first, with candidates, tries 1-9 at rand(range) and 10-19 at rand(max(sight, range)) from a
  random candidate (the draw order: distance, angle, candidate), kept within max(sight, range) + R of the
  centre (3D, the centre at the unit's height), then centre points up to try 20 (30 when idle). The
  second, and the first without candidates (`retailAiPatrolPoints`), 10 tries at rand(R/2) + rand(R) from
  the centre. Each must be on the map and placeable (0x507d10, the unit's own footprint; flyers skip
  it), the second >= 100 px from the first. A point that fails all its tries ends the plan, no orders.
- Orders: 0x4de530 resolves a move (2) to the centre and to the first point (either 0: nothing); MOVE
  centre (queue 0, so its missions are cleared; mission +0x4e = R) then MOVE p1 queued (+0x52 = -1); a
  flyer already within R of the centre gets MOVE p1 alone, unqueued. Then a queued PATROL p2 if its kind
  resolves. **Defenders patrol through the intruders.**
- **Monarch retreat 0x40d010** (from 0x40c890): 20 points at rand(R0) px and a random angle around the
  centre (no bounds test of its own), kept if placeable (flyers always); none kept: nothing. With no
  enemy within R0 it takes the first kept one; otherwise the kept point whose nearest listed enemy is
  farthest (hi-dword squares; the best starts at 0 and needs strictly more). MOVE there, unqueued, mission
  +0x4e = 16. A flyer also gets a queued patrol leg at (rand(5)+4)x16 px.
- Port: `classicThreat`, `classicEnemiesNear`, `classicPlanCombat`, `classicRetreat` in
  `src/ai/classicdefence.h`. The centre's fallback 0x412c10 (the profile home) is never reached from the
  base think, which stops without a centre. A Move with +0x4e = d is dropped when the unit is within d of
  its point and otherwise aimed at the circle's edge (our Move has no arrival radius).

### 1.9 Expansion and transfers: 0x40bc20, 0x40b9e0, 0x40b050 / 0x40b140 / 0x40b230 (S; P7 E: check_ai_expand, check_ai_transfer, check_ai_zone_labels)
- **Zone labels 0x4e0a20** (U5; at map load, from 0x4e0810 after each navigator grid is built, or after a
  scenario script's load): every grid (movement class) keeps one label per start record (+0x1c, always ten
  records) and per sacred-site record (+0x2c). An empty record gets -1, the rest 0. While a record is left at 0
  (the starts first, then the sites, in table order) it seeds a new zone (counter +0x14), and every record
  still at 0 that the reachability walk 0x4e1570 (mode 1, the static bit, unbounded) connects from the seed
  joins it: the starts, then the sites. A start counts as its cell less 6, 13 x 13 cells; a site as its cell
  less 6 and the footprint of the sacred feature there plus 12 (1 x 1 plus 12 when the cell holds none). The
  walk is not symmetric, so the seeding order matters. The 24 dwords each record carries (0x4e1c90) are not
  labels: they are the last cell of the same land/water status along 24 directions, which nothing here
  reads.
- **Targets** 0x40b050 (category 2 against the base's p1), 0x40b140 (4 against p2), 0x40b230 (5 against p3):
  with a centre of its own, the other occupied base 1..20 whose ready count (0x40a7b0) is below that quota
  and that has a centre, nearest by hi(dz²) + hi(dx²) of the 16.16 differences (the first on ties), or 0.
- **Transfer 0x40b9e0(to, n, category, clear)**: both bases must be anchored. Up to n members in the
  category with hp >= 3/4 are drawn by rand(remaining); a flyer moves always, a unit with a ground grid
  only when its grid's labels give both anchors the same zone (a mismatch still uses up one of the n; a grid
  whose label counts differ from the tables ends the transfer). No order: the unit joins `to` (its missions
  cleared when `clear`, which the base think never sets) and that base's next think plans it.
- **Expansion 0x40bc20(newSlot, escort)**: the founder is a random member that is live, not dying,
  selectable, finished, not held, not carried by a non-holder, hp >= 3/4, of category 3 (a free builder),
  else of category 2 (the Monarch is in neither). Its grid's label of this base's anchor is the zone (a
  flyer: any zone; an unanchored base, or label counts unlike the tables: no expansion). Of the sacred-site
  records still valid and in the zone, the nearest to the base's centre by trunc(sqrt) of whole-pixel
  differences (**the last one on ties**) that no occupied base is anchored on and that lies outside every
  occupied base's ring-2 radius (trunc(sqrt) <= radius is inside) is taken. **U5: the enemy test's radius is
  0x40cfa0(1) = 5 x ring 1's scale = 50 px**, around the site's cell centre, over the visible-enemy list
  (0x411ae0); the expansion is refused when 3 x that count > escort (unsigned). Otherwise the founder joins
  `newSlot` with no order and the base is anchored on the site (0x40c6e0: p0 = -(k+1)); its next think
  builds around the site. The base think then moves the escort's ground and air surplus there (§1.5).
- U5's other half, the gate in the base think 0x40b5c0..0x40b695, is as §1.5 step 5 has it and was already
  checked by check_ai_launch: the builder transfer on builders > 0 and rand(2) == 0 when a target is found,
  else the expansion when max(builders, freeBuilders) > 0 and an empty base 2..20 exists, with escort =
  ground > 0 ? ground : air > 0 ? air : 0.
- **P7 port notes.** The brain takes the sacred-site table from `World::sacredSites()` once (retail clears a
  record's valid byte when its feature is deleted, 0x496380; the World never removes a sacred feature, so
  every record stays valid), and the start table from the server: the map's starts in StartPos order
  (parseStartPositions; a generated map: the spots setupMatch used), padded to ten, with the seat's own
  record the one it opened at. A grid's labels are built once per navigator grid and footprint over the
  same static plane the constructor's walk reads (`classicStaticPlane`), and a site's footprint from the
  World's feature plane (`classicSacredFeature`). A flyer (no ground grid) never checks a zone. Retail
  indexes a label vector unchecked at a stale anchor; the port reads such an anchor as a label no record
  has.

### 1.10 Placement 0x412f80 (S + E, five placement emulation runs; P4b E: check_ai_placement, 4000 cases)
- **Block grid 0x4110f0**, drawn once per AI by the build manager's constructor (0x4102f0 at 0x41048b, after
  the AI manager's squads and before the creation priority pass; the profile reset 0x410670 draws nothing),
  8 draws. Land grid A: stepX 14+rand(10), stepZ 14+rand(3), offset rand(step)-step/2 per axis, margin 6.
  Water grid B (minwaterdepth >= 0): 17+rand(20), 17+rand(3), margin 9. Draw order: stepX, stepZ, offX,
  offZ of A, then of B.
- **Ordinary 0x411200**, up to 30 attempts. d = rand(R), or for a tower (a building with bmcode 0, armed
  (+0x260 0x10000), first weapon range < 1000, no menu) d = rand(R/2)+rand(R/2)+rand(R/2) (the SUM of
  three draws, not 3 x one draw), redrawn while > R, which rings the base. **[verify-edit: was
  "rand(R/2)x3", ambiguous; re-read at 0x4112a0..0x4112c6]** The angle is rand(65536) BAM and the point is
  centre + (-d sin a, -d cos a) through 0x5360bf/0x5360f3 (ported `retailScaledSine/Cosine`). The top-left
  cell of a footprint centred there, (p - foot x 8 px + 8 px) >> 4 px, is snapped to its grid block:
  trunc(cell/step) x step + offset + rand(step-foot-m) per axis (X's jitter drawn before Z's), which leaves
  "streets" of >= 7 free cells on land and >= 10 on water. Validate with the yardmap test (0x507d10 ->
  0x507400) and then reject any footprint cell holding a sacred feature (a feature index below 0xfffa and
  below the type count, or a 0xfffe tail resolved to its anchor; sacredsite 0 or NaN is not sacred).
  Distance is uniform, not by area, so buildings cluster near the centre. There is no outward search, no
  memory of failures, and no growth of R on failure. The result is the footprint centre
  ((2 x cell + foot) x 8 px).
- **Lodestone 0x411510** (a type whose yardmap[0] & 0x80): over the sacred-site records (0x62dbcc, one per
  sacred feature anchor), the nearest whose distance from the top-left footprint cell of a building centred
  on the base centre, trunc(sqrt(dx^2+dz^2) x 16 px), is <= R, whose cell still holds a sacred feature
  (0x50e600; off the map: none), and that passes 0x507d10 (asked only when nearer than the best so far,
  which starts at 2R; ties keep the first record). Its top-left cell goes on the deposit. No randomness.
- Port (P4b): the placement test is `World::canPlace` at the footprint centre; the feature plane is the
  World's map placement cells, with the sacred types those at the anchors of `World::sacredSites()` (in a
  World without a feature plane, the sites' footprints).

### 1.11 Retail sim behaviour the AI relies on (not AI code)
- The threat lists 0x411700, rebuilt every >= 7 ticks: **visible** enemies (32-px vision grid,
  height-projected bbox corners, cloaked never visible). Our sim has `World::visibleEnemies(p)` on the
  same clock (hashed `viewCount` plane).
- Return fire on hit 0x409a50: generic, both humans and AI (already sim). The fire-at-will sweep 0x40f330
  (sim, protocol 252). The weapon switch for AI owners 51a8b0 (sim, via `automaticGates`).

### 1.12 Conflicts between the area notes and how they were settled
| Conflict | Ruling | How |
|---|---|---|
| Wave roll ground terms: E/5 then E/2 (squads) vs E/10 then E/5 (targeting) | **E/10, then E/5** | S: re-read at 0x40c2cc..0x40c31e: the two divisions are by 10 and by 5 |
| 0x40d010 pick: "fewest enemies" (squads) vs "maximise min distance" (placement) | **max-min squared distance; the first valid point when no enemies** | S: re-read at 0x40d1be..0x40d263 |
| Monarch retreat: can cast -> 1/4 (squads) vs 1/8 (build, placement) | **castable halves the threshold (1/8 at full mana)** | the build note's planner emulation is E, the squads note is S only |
| lowFunds quantity: stored fraction (targeting) vs allocation (build, placement) | **allocation (`player+0x10c -> +8`)** | 2 of 3, and it matches the ported 0x51d837 object; settled by emulation in P4a (U4) |
| Category 6 = transports (targeting) vs naval combat (squads, build) | **naval combat** | squads decoded the FBI flag map |
| The "reserve" squad is slot 0 (targeting) vs slot 96 (squads) | **96** | live poll: one kind-4 squad; the ctor loops in squads |
| Weight debug print columns: the comparison note read col2 = pref, col3 = priority | **col2 = priority, col3 = pref** | the config note read the printer 0x413030; col3's on/off values match the weight-switch rule, col2 matches 0x410810 |

---------------------------------------------------------------------------------------------------
## 2. Gap table

### 2.1 Retail behaviours vs our current AI (`src/ai` Controller, all levels)
| # | Retail behaviour | Ours | Status |
|---|---|---|---|
| 1 | Profile from the map .ota `aiprofile` (skirmish = default) | default.txt always (missions: aiprofile) | different (equivalent on shipped skirmish maps) |
| 2 | pref = 100 x weight, clamped (weights are on/off) | raw file weight is the draw weight | **different** |
| 3 | First-writer-wins locks; category/ALL lines | last line wins, no categories | different |
| 4 | Unlisted type = weight 100, unlimited | missing weight = never built | **different** (arasiege, aratrans, vercen, npctemp) |
| 5 | Dynamic priority 0x410810 (~210-tick refresh) | none (static desire ladder per category) | **missing** |
| 6 | One weighted draw over the whole menu, then reject another faction's pick | category planner, draw within the top category | **different** |
| 7 | Separate lodestone draw every think, replacing the ordinary pick | manaShort trigger, priority 96, home radius gate | different |
| 8 | Hard limits (count incl. unfinished >= limit) | limits x limitScale; Hard/defensive lift them | different |
| 9 | No affordability check; allocation gates (0.7, 0.7/3, cost deferral) | affordability gate, over-commit throttle (mana < 50), income < 20 rule | **different** |
| 10 | Squads: base/strike/backup/VTOL/naval; assignment every rand(7)+3 | none (pools computed per think) | **missing** |
| 11 | Event clocks: base rand(30)+15, strike rand(60)+15, backup 150, VTOL rand(30)+15 | a fixed thinkPeriod of 20/30/90 | different |
| 12 | Base anchored at the start location; scatter around it | ring search around the builder/home | **different** |
| 13 | Block-grid random scatter, towers ring the base, deposits never covered | deterministic ring search r = 70..340 step 30, 48 px lanes | **different** |
| 14 | Lodestone = nearest free deposit to the base centre within R (560/800) | nearest to the builder; reservations, ally rule, stall bans | different |
| 15 | Factory: one queued unit, deferral of expensive units | producersPerThink round-robin, Train | different |
| 16 | Constructor: Monarch builds only while builders < p1 and past the alarm | Monarch always builds | **missing** |
| 17 | Monarch alarm on hit (30..927 ticks build pause) | none | **missing** |
| 18 | Monarch retreat by HP/mana/castable thresholds, to the safest of 20 points | Monarch "fights back" (Attack lastHitBy) + 520 px home leash | **different** |
| 19 | Desired builders p1 = rand(2)+1, growing with population/20 | builderCap tables per level | different |
| 20 | Duplicate-factory rule; Monarch 4-tower cap | desiredFactories per income | different |
| 21 | Idle units: MOVE centre -> p1 -> PATROL p2 inside the base ring; re-plan 1/50 | door clearing; stationArmy (defensive only) | **missing** |
| 22 | Base threat = visible enemy in the ring-0 radius; defenders patrol through intruders | sendDefenders (defensive levels only), radius 900/400/1500 | **missing** for attack levels |
| 23 | Wave size from the biggest enemy population, x multipliers, cap 50, sign = keep reserve | bigPush = waveSize + income/4, raids, cooldown 750 | **different** |
| 24 | Strike: cohesion recruit, dissolve at a random 1/3..all loss, re-pick target on kill | AttackMove pool committed to the objective | **different** |
| 25 | Target = scored over all enemy units, mana-site/visible/armed/big-player bias, new-best reachability | nearest visible enemy to home, else enemy start; flow-reachable | **different** |
| 26 | Strike issues MOVE (fight by standing orders) | AttackMove | different (approximation needed) |
| 27 | Withdraw wounded (HP < 1/(rand(3)+3)) and unreachable members | none | **missing** |
| 28 | VTOL squads patrol random map/edge points | flyers join waves | **missing** |
| 29 | Naval strikes 51..79 like ground waves; naval x3 priority | naval planner, fleets of 3 (Easy 2), cooldown 750 | different |
| 30 | Expansion to sacred sites with a builder + escort; builder/army transfers | none (single home; Defensive claims deposits to 1600) | **missing** |
| 31 | Standing orders 0x5198a0(u,2) on assignment | spawn defaults; Stance only on defensive levels | different |
| 32 | No repair/assist/reclaim/guard orders | none either | same |
| 33 | RNG: Park-Miller, n < 2 does not advance | glibc-style LCG, mislabelled "retail-style" | different |
| 34 | One seed per game; draws shared | every skirmish AI gets the room seed | different (irrelevant server-side) |
| 35 | No difficulty, no income multiplier | 6 levels, Absurd x2 income | n/a: the Classic level must have none |
| 36 | Fire-at-will sweep, AI weapon switch, return fire | sim (protocol 252), automaticGates for type 2 | **same** (sim) |
| 37 | Visible-enemy lists every >= 7 ticks | `World::visibleEnemies` (hashed plane) | same source; see U10 for an exclusion difference |

### 2.2 Non-retail behaviours our AI has that the Classic level must NOT have
Category planner and desire ladder; `assessNeeds`; affordability/"economy-first" gate; the income < 20
economy rule; the over-commit throttle (mana < 50); the full-store rule (90%); the priority-96 lodestone
trigger and its home-radius gate; `terrainWeight`; limitScale and limit lifting; "missing weight = never
build"; builderCap/desiredFactories/army targets per income; the Zhon allowance (`mobileProduction`);
ring-search `placeSite` with 48 px lanes, approach clearance and enemy-proximity avoidance; deposit
reservations / `plannedManaSites_`; the nearest-ally home rule; the stalled-builder timeout and bans;
refused-site memory and `unqueueStalled`; the category retry; the naval planner with private preview
Worlds; Monarch "fights back" via `lastHitBy` (omniscient about unseen attackers); the Monarch 520 px
leash; door clearing; `sendWaves` (objective, raids, lone scout, cooldowns, bigPush); `sendDefenders`;
`stationArmy`; the defensive Stance; the fixed thinkPeriod and producer round-robin; manaMult != 1;
defensiveAi; and the Controller LCG.

---------------------------------------------------------------------------------------------------
## 3. Open unknowns and how to resolve each
| ID | Unknown | Matters for | How to resolve |
|---|---|---|---|
| U1 | Mission class bytes 1/2 (attack-type), 7; flags 0x2000 (idle-ish), 0x10000 (interruptible), 8 (build), 0x200/0x400 | contact test, 1/50 re-plan, busy builders | **PARTLY RESOLVED (S, P4b):** the mission table is four static arrays of 25-byte descriptors registered at start-up (0x5f27d8, 0x5eb7f0, 0x5ebc88, 0x5eb528; name at +0x15, class byte at +0x10, flags dword at +0x11), and a mission's +0x5a flags are copied from its descriptor's +0x11 when it is made (0x4d6d3b). Class 1 = Suppress, Attack_Chase/Melee/Kamikaze, AttackSpecial, AirToAir, AirToGround(Hover), Attack_NoMove; class 2 = AirStrike; MobileBuild 0, HelpBuild/RepairUnit 6, Patrol 7, Guard 5, moves 14, BuildingBuild and the command missions 19. Flag 8 is set by MobileBuild, HelpBuild (both kinds) and BuildingBuild (0x0c). No descriptor sets 0x2000 at +0x11; **P5 (S):** it is set at run time on the type's default mission (FBI defaultmissiontype, type +0x24b), which the mission stepper pushes (0x4d8597 -> 0x4d83b0) onto a unit of a human or AI owner left with no mission, and a mission later inserted in front of a 0x2000 one inherits it (0x4d7709), so an idle unit's auto-engagement is idle-ish too; 0x200 / 0x400 (a target unit / a goal point) are cleared by the mission ctor 0x4d6c40 when it gets none. 0x10000 is the Standby family's. Port: an empty queue, and a queue of only the World's auto-attacks (with the return move behind a leashed one), read as idle-ish; the first mission keeps its descriptor's whole flag dword, its target and goal (P5: Move 0x3000400, Patrol 0x3000410, attack 0x1000280, MobileBuild 0x80508, BuildingBuild 0x8010c ...). The port's mapping from our orders is in §1.6 |
| U2 | Whether the 1/50 guard at the end of 0x40c890 can replace the build just issued | builders abandoning sites | **RESOLVED (S + E, P5):** yes. The build makes MobileBuild the first mission (flags 0x80508, no 0x2000), so 0x40d370 goes on only on rand(50) == 0, or always for an armed builder under a threat (the Monarch); its orders start with a queue-0 move (0x4d78a0's arg 2 = 0 calls 0x4d6a50 first), which clears the build. An armed builder under a threat keeps its build only when a candidate is within max(sight, range) of the site or of itself (49/50). Checked by check_ai_defence (MobileBuild missions among its cases) |
| U3 | Factory guard byte at 0x604914 (which mission counts as "busy factory") | duplicate-factory rule | **RESOLVED (S, P4a):** 0x604914 and the factory planner's 0x605064 are both the mission name "BuildingBuild", resolved to its kind by 0x4d4bf0 (a binary search of the 25-byte mission table at 0x62db84); a busy factory is one with a BuildingBuild mission, which 0x429810 adds for every queued unit of a unit without a navigator |
| U4 | Confirm that `[player+0x10c]+8` is the allocation (min(1, stored/demand)), not stored/capacity | lowFunds 0.7 gate, factory 0.2333 gate | **RESOLVED (S + E, P4a):** 0x51d837 writes resource +0 stored, +4 capacity (storage, or 1), +8 = stored / (demand x 0.03333333) when that demand exceeds the store, else 1. `check_ai_factory_think.py` runs that block and then 0x40dcb0 reading the same object: 4000 cases match the port's allocation (sim::RetailConstructionResources), and the same port gated on stored/capacity instead differs in 110 of 1000. The constructor planner's lowFunds reads the same field (0x40c890) |
| U5 | Expansion gate details 0x40b5c0..0x40b695; units of the "radius 50" in 0x411ae0 for the enemy check | expansion frequency | **RESOLVED (S + E, P7):** the radius is 0x40cfa0(1) = 5 x the ring-1 scale (10) = **50 pixels** around the site's cell centre (0x411ae0 compares whole-pixel squares), and the expansion is refused when 3 x the listed enemies > escort (unsigned). The gate 0x40b5c0..0x40b695 is §1.5 step 5 (escort = the ground surplus if > 0, else the air surplus if > 0, else 0), already under check_ai_launch. The zone the founder's site must share with its base is the navigator grid's map-load label (0x4e0a20, §1.9), not a live component. Checked by check_ai_expand (4000 expansions, with the real 0x411ae0 under it), check_ai_transfer and check_ai_zone_labels |
| U6 | Unit `+0x130 & 0x2000` (excluded from threats and targets) and `+0x104` (must be 0 for a member) | filters | Find the setters by xref and read them statically; emulate a setter where the static reading is unclear (I: dying/being-built-by-transport, or teleporting). **P3 (S):** 0x2000 is cleared and 0x20 ("selectable") set by the MakeSelectable mission handler 0x401450, so 0x2000 reads as "not yet selectable" (a unit still leaving its factory). The port takes 0x20 as "finished" and +0x104 as always 0, and treats a carried unit as never in a holder (+0xa8 -> 0x4000000); still open: +0x104's setter and the holder bit. **P8 (S, U16):** 0x2000 is not "not yet selectable after a factory": every unit is created with 0x20 and without 0x2000 (0x511990), and 0x2000 is set only by the scenario placement loop (0x514c92..0x514cb3, from its record) and a loaded game's flag restore; the port now never sets it. +0x104 and the holder bit stay open (accepted limits) |
| U7 | List +0x25 (enemies with +0x130 0x100) used when the AI owns an `istargetingupgrade` unit | defence candidates | **RESOLVED (S, P5):** 0x411700 sets build manager +0x79 when an own unit's type has +0x260 0x400 (and +0x114 bit 1), and 0x411ae0 scans list +0x25 only when +0x79 is set and the visible-enemy list gave nothing. No shipped FBI has istargetingupgrade (or any 'targeting' key), so it never fires on stock data; `classicEnemiesNear` has the branch (check_ai_threat exercises it) and the brain passes it off |
| U8 | Where the AI meets the unit cap (not in the planner) | late-game behaviour | **RESOLVED (S, P8):** no AI routine compares with the cap except the picker's divisor (game +0x14e7c, 0x40a46a). Unit creation 0x512060 (16 callers: factories, conjures, placements, scripts) refuses when the owner's population word (record +0x24ec, counted at creation, unfinished units too) equals the cap, after the profile limit 0x409890 when its caller asks. The planners order freely and the order fails at creation; our sim refuses at the same point (`World::atUnitCap`, >= where retail tests ==). No change; no near-cap game test (accepted) |
| U9 | Whether `-AutoStop` + a GDB seed poke gives tick-identical retail games per seed | comparison noise floor | **SETTLED (no):** with `-AutoStop`'s per-tick reseed off, same-seed runs diverge from about tick 180-300 (the first building's entity slot follows the unseeded C-runtime `rand`); with it on, every seed plays one game. Retail games are no longer run (§5) |
| U10 | Our `visibleEnemies` excludes `underConstruction` enemies; retail's list +5 excludes 0x2000 instead | threat test, defence | **RESOLVED (S, P5):** 0x411700 puts on +5 every live, not dying unit of a non-allied owner that the player sees (0x4f7210) and that lacks 0x2000. Under the port's U6 reading (0x2000 = not yet selectable = unfinished) that is our list; the brain reads `World::visibleEnemies` unchanged (no copy) and re-checks life at query time as 0x412000 / 0x411ae0 do. (The World also leaves neutral players off; skirmish has none.). **P8 (U16):** a site still being built does not carry 0x2000, so retail lists it; the brain now keeps its own list, the World's plus those sites, rebuilt with the World's (`ClassicBrain::refreshVisible`) |
| U11 | A live weight debug print vs our `retailBuildPriority` (e.g. why the mana building shows no lodestone +60) | priority fidelity | **RESOLVED (E, no retail run):** `tools/re/check_ai_printweights.py` reads the 20 captured PrintWeights dumps (5 factions x 4 ticks, Abnar's Terrace) and the manager polls around them. All 1176 rows match: pref 1176/1176, limit 1176/1176, priority 1176/1176, and every dump is reproduced exactly by our port (classic_ai_test --printweights: the brain's type facts, profile limits and priority pass over retail's table) from the polled state and seed plus 5..1238 unknown in-between draws; retail's 0x410810, emulated on the same tables, equals the port on 1300/1300 table passes. Three things had to be right for that, two of them port bugs now fixed: (1) retail's table keeps `tarnecr2` (unitname TARNECR2, objectname TARNECRO) as its own entry, which draws for its 100000 cost at every refresh; the registry now keeps such files in a Classic-only side table and the catalogue is keyed by unitname; (2) the creation pass runs before the profile and before any unit or store exists: every limit -1, counts and population 0 (any population under 10 gives the same draws), fill 0 and an empty history (so a shortfall), which is why Taros's tick-16 table wants lodestones (100) and gives builders +5; the brain now runs it in its constructor that way; (3) the dump's table can be older than the poll before it (Taros 5400: it came in at tick 4590), so the check finds the refresh in the polls itself. Aramana: both 'S' flags set (bmcode default 0), so it is a mana site and a lodestone-class type, as the P2 reading said |
| U12 | Unit cap used by the picker divisor and comparisons: v4.0 default 150 vs v4.1 500 | target bias | Decision: use our lobby cap; comparisons set it explicitly on both sides |
| U13 | How an AI-issued MOVE behaves in our sim compared with retail's MOVE_GROUND under standing fire orders (fire while moving, stop and engage?) | the strike's whole fighting style | **RESOLVED (S + our game test, P6):** the strike's command 2 resolves (0x4de530) to MOVE_GROUND / VTOL_MOVE, a point move whose descriptor flags are 0x3000400: no 0x10000, class 14. The hit response 0x409a50 gives its retaliation attack only to a unit with no mission, a 0x10000 one or a class-7 one, and the fire-at-will sweep 0x40f330 fires a unit's automatic slots whatever its mission; nothing in the move stops for an enemy. So a strike member walks to the target's position firing what the sweep gives it and fights only once it arrives (idle, on its default mission). Our Move is that mission (`World::order`: flags 0x3000400, a ground mission; the acquisition gate skips plain moves; the retaliation takes idle and patrolling units only), so the brain gives Move. classic_ai_u13 (Abnar's Terrace, 10 araarch ordered 1000 px through 10 verarch, both on Stance 0): on Move none stops to engage, the enemy loses 338 of 16000 hp to the walking column, its one survivor arrives; on AttackMove all ten stop to fight |
| U14 | Retail's per-player kind on disconnect / whether the AI ever ignores limits in skirmish (+0xe3 always 1) | none for skirmish | Static only, low priority |
| U15 | The `unsupported("strike recruitment")` and other sim-port branches: whether the server brain reuses the same templates unchanged | code reuse | **RESOLVED (P3):** the brain calls `retailTickAiSchedule`, `retailTickAiSquad`, `retailNearestAiBase`, `RetailAiCentroid`, `retailAiCategory` and `retailAiBaseRadius` unchanged, with its own callbacks; the in-sim port's assignment differs from retail in two places the brain does not copy (it skips structures, and it skips incapacitated units instead of +0x104), and none of its callers changed |
| U16 | Whether a structure being conjured carries unit flag 0x2000 (P6) | the picker's "unfinished" divisor; which sites the visible-enemy list holds | **RESOLVED (S, P8): no.** 0x512060 -> 0x511f60 -> 0x511990 creates every unit with 0x20 (0x511b18..0x511b2d), and a site still to be built with +0x108 = 1.0; nothing there sets 0x2000. Its only writers are the scenario placement loop (0x514c92..0x514cb3, bit 7 of the record's byte +0x27) and the flag restore of a loaded game (0x513xxx); MakeSelectable 0x401450 clears it. So an enemy site being built is on list +5, threatens, is a defence candidate and is scored by the picker with its unfinished divisor. Port changed: `enemyView` never sets 0x2000, the brain's list adds those sites, own units are always 0x20 |

---------------------------------------------------------------------------------------------------
## 4. Design

### 4.1 Where it runs: server-side brain (recommended), not in-sim
- **Option A (chosen): `src/ai/classicbrain.{h,cpp}`**, a `ClassicBrain` that `ai::Controller::tick`
  hands off to **first**, when `diff_ == Difficulty::Classic` (`classic_->tick(world, simTick, sink);
  return;`). It runs on the takserver like every AI (memory rule "client-server only: no AI in client"),
  reads the const World, and emits `net::Command`s. Replays replay commands, so the brain never re-runs
  on playback.
- It reuses the pure, already-emulated templates in `src/sim/retailai.h`, `retailplayer.h`,
  `retailaipatrol.h`, `retailreach.h` and `retailrng.h` (`retailAiChooseBuild`, `retailAiBuildWeight`,
  `retailAiPlanFactory`, `retailBuildPriority`, `RetailResourceHistory`, `retailAiBaseRadius`,
  `retailNearestAiBase`, `RetailAiCentroid`, `retailAiCategory`, `retailTickAiSchedule/Squad`,
  `retailAiPatrolPoints`, `retailRectangleReachable`), called with the brain's own `Random` callable.
  New pure helpers (block grid, scatter, deposit search, wave rolls, target score, strike cohesion,
  retreat point) go in **new headers** under `src/sim/` (or `src/ai/retail/`) as templates over
  `Random`, so the same code is unit- and emulation-testable. Existing headers are not changed in a way
  that alters any current sim caller.
- **Why not in-sim (Option B)**: it would give retail's exact draw order on the shared `gameRand`, but
  our sim is not retail-exact anyway, so bit-parity with a retail game is unreachable. It would also put
  AI on every client (against the standing rule), require `ai/*.txt` in the gameplay-data hash, and change
  hashed state. The in-sim `tickRetailAiBase/Strike` stays as the capture-replay probe. **Decided
  (D2): server-side.**

### 4.2 Plumbing
- `Difficulty::Classic = 6`, `kAiLevels = 7`. `paramsFor` returns Normal's
  params (unused). `incomeMultFor(Classic) = 1` and `defensiveLevel(Classic) = false`, so `matchConfigFor`
  derives exactly the sim config a Normal AI gets: manaMult 1, defensiveAi false, automaticGates true
  (type 2). **No sim state differs from an existing AI seat.**
- Lobby: add the 7th `diffName`/`diffCol` entry, CLASSIC (the arrays are sized by kAiLevels, so it
  won't compile without it). The host cycles through 7 levels. Hidden in campaigns as now. `aitool` name map,
  `TAK_AI_LEVEL=6`, and `ai_behavior_test levelWiring` (out of range -> Normal is now `>= 7`).
- **Protocol**: kNetVersion 290 with the plumbing (the label and semantics change; precedent 275). There is no replay format change: `slotAiLevel[]` already
  records the byte, and level-6 replays from the new protocol replay their commands.
- **RNG**: the brain owns a Park-Miller stream, `retailSeed(roomSeed ^ (0x9e3779b9 * (slot+1)))`, so
  two Classic AIs don't mirror. It is server-local and unhashed, and its draw order follows retail's order
  inside each routine. The Controller LCG stays untouched for the other levels.
- **Profile**: a new `ClassicProfile` parser (pref bytes, locks, categories incl. ALL, limits,
  first-writer-wins). It is chosen from the map's `aiprofile` (`matchsetup` already reads it; skirmish
  gives default) with retail's fallback chain, and is server-side only, so the data hash is unchanged.
  `ai::loadProfile` stays as is for the other levels.
- **Byte-identity gate**: every phase must keep `--mpai` 60 s/300 s hashes unchanged in both
  pathfinding modes (they seat a Normal AI), `tools/check-determinism.sh` unchanged, `ai_behavior_test`
  all PASS, and `naval_ai` PASS. Classic runs: `TAK_AI_LEVEL=6 --mpai --seed 1` twice -> same hash.
- **Disconnect takeover** stays Normal; **campaign missions** are Normal unless the player picks Classic in the campaign book (P8, D3).

### 4.3 World/sim support the brain needs
Most of it exists. "new" means an addition. Any new **sim** field must be unhashed or derived, or every
baseline moves. Prefer brain-side derivation.
| Need | Source today | Gap |
|---|---|---|
| Per-type flags: armed, builder, commander, canfly, floater, minwaterdepth, bmcode/isStructure, isfeature, yardmap 'S' and yardmap[0]&0x80, buildcost (fallback buildtime), footprint, builddistance, maxdamage, sight, weapon range/damage/reload, menu | UnitType (mostly). The restore tools map flags by hand | new `ClassicTypeInfo` derived on the brain side from UnitType + FBI. Verify the 'S' and minwaterdepth sources (U11) |
| Per-armour weapon damage > 0, and the active weapon | sim weapon tables | a const query helper if not public |
| Visible enemies on retail's clock | `World::visibleEnemies(p)` | filter differences (U10) handled brain-side |
| Player allocation, shortfall history, stored/capacity, population, unit cap | `constructionAllocation`, `displayResources.shortfall`, `manaCapacity()`, `unitCap` | check that `constructionAllocation` = retail's min(1, stored/demand) for a World-pool player (U4) |
| Unit mana/maxMana, castable current weapon | sim unit fields | a const accessor if needed |
| Sacred-site (deposit) list with occupancy; start-location table; nav component per movement class | `manaSpots`, `enemyStarts`/start spots, `navFor`, `pathExists`, `retailRectangleReachable` | per-site occupancy test = `canPlace(lodeType, site)` + feature presence |
| Yardmap placement validation (0x507400) | `canPlace(type,x,z,player)` | confirm it equals the yardmap test (no extra lane rule in the sim) |
| Mission state: building, attacking (auto or ordered), idle-ish/patrol, factory queue busy | `orders`, `autoTarget`, `buildQueue`, `workScriptWorking` | a U1 mapping table |
| Monarch hit event | `lastHitBy` / hp | brain-side hp-drop tracking per tick (no new sim field) |
| Standing orders 0x5198a0(u,2) | `Stance` command, targetId 0 -> `setStance(0)` -> standingOrder 2 | none |
| Commands: Build, Train, Move, Patrol (two-leg), Stop/clear | all exist | the two-leg patrol (Move centre, Move p1 queued, Patrol p2): confirm queued commands (shift) exist on the wire |
| Group move (backup -> centroid) | per-unit Move | none |
| Tech unlock | **not needed**: retail has none | — |

### 4.4 Fidelity limits accepted (documented in the level's docs)
Decisions arrive one tick later than retail's in-tick position (they are applied in tick N's bundle).
RNG interleaving differs (a private stream). Unit scan order differs (affects ties only). Mission flags
are approximated through commands (U1/U13). Our economy, build times and pathing differ from retail's,
so outcomes, not only decisions, will differ. Fidelity is checked routine by routine, by emulation
oracles, and shown by our own game tests (§5); retail games are not run.

---------------------------------------------------------------------------------------------------
## 5. Phased implementation plan
Each phase is one landable commit (on a task branch, merged by the lead). Each phase gate =
its own test + `ai_behavior` + **--mpai baselines unchanged** (both pathfinding modes) + one
`TAK_AI_LEVEL=6 --mpai --seed 1` repeatability pair (from P1 on). Per memory, use build-o2 and
minimal testing.

**How fidelity is checked (user direction, 2026-10-22): the retail game is not run any more.** No
Proton or whole-game retail runs and no new captures. Each ported routine is checked by static
reading of the disassembly plus an **emulation oracle**: the retail routine run in Unicorn
(`tools/re/emu.py`) against our port on random inputs, the `tools/re/check_ai_*` pattern, each shown
to fail once on a planted bug (then reverted) and run like the existing oracles (by hand against the
gitignored binary; opt-in ctest where it has one). Behaviour is then shown by **our own engine game
tests** (ai_behavior / classic_ai / headless `--mpai` Classic games). The PrintWeights dumps already
captured (`~/tak-tmp/retail-ai/baseline/printweights`, 5 factions) stay usable read-only
(`check_ai_printweights.py`). The comparison harness (phase C) is kept in `tools/re` as optional
tooling and is not a gate. Why whole-game retail runs could not be a gate anyway: plain `-AutoStop`
reseeds both RNGs with 1234 before every tick, so every seed plays the same game; with that turned
off at tick 1, same-seed runs still diverge from about tick 180-300 (the first building's entity slot
follows the unseeded C-runtime `rand`), so a seed varies retail games but cannot replay one (U9).

| Phase | Content | Its test | Emulation oracle(s) + our game tests |
|---|---|---|---|
| **P0 Plumbing** (landed on task-classic-ai) | Difficulty::Classic, kAiLevels 7, lobby label CLASSIC, aitool, TAK_AI_LEVEL, docs, protocol 290; `ClassicBrain` stub (does nothing, so the Monarch idles) | levelWiring updated + `classicLevel` (ai_behavior_test); `server_ai_level` (a lobby round-trip of level 6) | none (no ported routine); the --mpai Classic pair repeats |
| **C Comparison harness** (on the branch; **optional tooling, not a gate**) | `tools/re`: kwin virtual + umu + `-AutoStop=999999`, the GDB seed poke at tick 1, the `+AI`-equivalent poke for AI-vs-AI, a JSONL poller (units, mana, squads, the build manager); a debug-only matching roster dump in takserver; `compare_ai.py` metrics. Scripts only, nothing from the binary in the repo | `retail_ai_harness_teardown` (fake kwin: nothing of a run is left) | none. Its captured PrintWeights dumps feed `check_ai_printweights.py` read-only |
| **P1 Profile** (on the branch) | `ClassicProfile`: pref/locks/categories/limits/first-writer-wins/unlisted defaults; aiprofile selection | unit tests from config's emulated cases (classic_ai, classic_ai_data) | `check_ai_profile.py` (0x538f00 -> 0x410c80/0x410d50 on random scripts); `check_ai_printweights.py` pref and limit columns of all 20 captured tables |
| **P2 Type info + priority clock** (on the branch) | `ClassicTypeInfo`, the 7-tick count/completed cache, the rand(30) priority refresh, shortfall/fill from World, the creation pass | classic_ai (a fixed World: creation pass, clock, recount, priorities) | check_build_priority (0x410810), check_player_cache (0x4121c0/0x411a90), check_resource_history; `check_ai_printweights.py` (every captured table reproduced exactly; 0x410810 emulated over whole tables = port) |
| **P3 Squads + assignment** (on the branch) | 100-slot model, base ctor rolls, scheduler rand(7)+3, tick<=30 Monarch->base 1 anchored at the start location, nearest-base assignment + Stance 0 | check_ai_scheduler / check_ai_squads / check_ai_base_selection reused; classic_ai (constructor, rolls, assignment cases) | `check_ai_squad_ctor.py` (0x40fed0) and `check_ai_assignment.py` (0x40fe80 + 0x40eeb0), opt-in ctest `-DTAK_CLASSIC_ORACLES=ON`; game test (classic_ai_data, Abnar's Terrace): every finished unit is in a base squad within 10 ticks (the Monarch by tick 30, anchored on its start), Stance 0 applied, a destroyed unit leaves its squad, member counts per kind |
| **P4a Factory planner** (on the branch) | 0x40dcb0 (allocation gates, cost deferral, one queued) + the menu draw with faction rejection, reached from the base think's member loop (0x40b320 to 0x40b4cf); FBI-builder menus in Priority order; Train commands | check_ai_factory_plan + check_ai_build_choice + check_ai_build_weight (shared templates, now in the opt-in set); classic_ai --factory-think | new `check_ai_factory_think.py` (0x51d837 + 0x40b320's member loop with the real planner, draw, weight and limit code; settles U4), shown to fail on planted bugs (side rejection dropped, the commander exception dropped, gates on the store fill, the stale-anchor rule dropped); game test (classic_ai_data, Abnar's Terrace): Aramon/Veruna keeps and a Taros dungeon given at the start train same-side menu units one at a time within the profile limits and they join the base; a 2-Swordsmen/0-others profile gets exactly two Swordsmen |
| **P4b Constructor planner + placement** (on the branch) | 0x40c890 (Monarch gates, p1 builders, duplicate-factory, 4-tower cap, lodestone pass every think, reachability), block grid 0x4110f0, scatter 0x411200, deposits 0x411510; Build commands | classic_ai (the construction mirror now includes the grid draws); classic_ai_test --placement / --constructor | new `check_ai_placement.py` (grid draws, lot windows, tower distance, 30-attempt failure, sacred refusal, deposit choice) + `check_ai_constructor.py` (the planner's branches) + `check_ai_static_grade.py` (the static bit the reachability reads; added at review) + check_ai_base_radius, opt-in; each new oracle shown to fail on planted bugs; game test classic_ai_bases (Abnar's Terrace, all five factions from a bare start, 10 game-minutes): lodestones, structures and units, sites within the ring radius plus a block, lodestones onto free deposits, no factory type ordered while an idle one is in the base |
| **P5 Base defence, idle, Monarch** (on the branch) | 0x40d370 (rings, wounded radius, threat candidates, keep test, points, orders), the 0x412000 threat test and 0x411ae0, Monarch retreat 0x40d010, alarm 0x409a50 (the sim's `Player::aiThreatUntil`); first-mission flags/target/goal and run-time 0x2000 (U1); U2, U7, U10 settled | classic_ai_test --threat / --defence / --retreat | new `check_ai_threat.py` (0x412000 + 0x411ae0), `check_ai_defence.py` (0x40d370) and `check_ai_retreat.py` (0x40d010), opt-in, each shown to fail on a planted bug; check_ai_patrol_points (the shared template) in the opt-in set; game test classic_ai_raid (Abnar's Terrace): idle soldiers patrol inside the base ring, raiders inside the ring-0 radius send them at the raiders (first points within reach of one), the Monarch at full HP stays, below its HP threshold it retreats inside the ring-0 radius, away from them |
| **P6 Waves** (on the branch) | the base think's tail (quota growth, ready counts, transfer / expansion calls answered "none" until P7, launch into a strike backup / VTOL squad / naval strike, wave re-rolls 0x40c240/3c0/490), backup 0x40ea30, strike 0x40e180 (cohesion 0x40abd0, retreat base 0x40e120, dissolve, keep / re-pick, withdrawals, contact / march), picker 0x40a200, VTOL 0x40eb40 (and its retreat base 0x40ee50), naval strikes (the same think); U13 settled first | the empty-squad clocks stay with check_ai_squads; the rolls themselves were already checked by check_ai_squad_ctor (there never was a separate check_ai_wave_roll), and run again under the launch oracle | new `check_ai_target_pick.py` (0x40a200), `check_ai_strike.py` (0x40e180, 0x40ea30, 0x40eb40) and `check_ai_launch.py` (0x40b320 with its planners stubbed), opt-in, each shown to fail on a planted bug; game tests classic_ai_u13 (Move vs AttackMove), classic_ai_waves (launch, march by Move, a member sent home, dissolve, back to a base) and classic_ai_duels (Classic v Classic, 3 faction pairs on Edmont's Field end within the 90-minute cap: 73.4, 24.2 and 27.4 min) |
| **P7 Expansion + transfers** (on the branch) | zone labels 0x4e0a20 (U5 settled first), transfer targets 0x40b050 / 0x40b140 / 0x40b230, transfer 0x40b9e0, expansion 0x40bc20, wired into the base think's hooks; the server hands the brain the map's start table | classic_ai_test --zone-labels / --transfer / --expand | new `check_ai_zone_labels.py` (0x4e0a20 with the real 0x4e1570; 3000 grids), `check_ai_transfer.py` (the three searches + 0x40b9e0; 4000 cases) and `check_ai_expand.py` (0x40bc20 with the real 0x411ae0; 4000 cases, half with enemies on the edge of its 50 px query since the review), opt-in, each shown to fail on a planted bug; game test classic_ai_expansion (Abnar's Terrace, all five factions, 20 game-minutes: every faction founds 2-4 bases on sacred sites, lodestones grow from 1-2 at 5 minutes to 3-6 at 20, every site inside its builder's base ring, no site shared) |
| **P8 Acceptance + docs** (done) | Tune nothing that retail doesn't do; user guide, README level table, memory; Classic for campaign opponents (the campaign switch); U8 and U16 settled; the plane caches as deques | every oracle and game test above; settings (campaignAi) | **acceptance = every ported routine has a passing emulation oracle (each once shown to fail on a planted bug) + the engine game tests pass + docs and user guide + the campaign switch.** Done: [classic-ai-acceptance.md](classic-ai-acceptance.md) (all oracles pass, 39 plants in one re-run, check_ai_assignment strengthened); campaign game test: Classic takmission08_dh 300 s and takx13_mt 660 s (the massattack marches at 600 s), Normal missions byte-identical before and after. Retail-run distributions (phase C) are optional evidence, not a gate |

Ordering: P0 -> P1 -> P2 -> P3 -> P4a -> P4b -> P5 -> P6 -> P7 -> P8. After P4b the level already
builds a retail-like base, which is a usable checkpoint.


---------------------------------------------------------------------------------------------------
## 6. Corrections to the earlier retail AI notes (agent memory)
1. "weight = clamp(unitIntrinsicValue x fileWeight)": pref **starts at 100**, so `clamp(trunc(100*w))`,
   first-writer-wins with category compounding. Any non-zero integer weight gives 100, so **weights
   are an on/off switch**. Final = pref x priority/100, where the priority (0x410810) is dynamic.
   (E + the live weight debug print)
2. "per-faction filename, falls back to default.txt": the profile is **per map/mission** (.ota
   `aiprofile`), one file for all AIs, and no faction filename exists. "10 profile slots" = one manager
   per player slot.
3. "Unit at its limit EXCLUDED; unlisted?": unlisted or misspelled types are **built** (weight 100,
   unlimited); `araseige` leaves arasiege unlimited.
4. "rand(30)+15 per builder (0x40b320)": that is the **base squad** wake. Strike rand(60)+15, backup 150,
   VTOL rand(30)+15, homeless 1800, assignment rand(7)+3.
5. "master econ scheduler 0x4ea6c0", "brain update 0x49e180", "army coordinator 0x4e91d0", "assign
   0x4e8ce0": all **network/frame code**. The AI chain is 0x40fe00 -> 0x40fd80 (scheduler) -> 0x40eeb0
   (assignment); the bookkeeping is 0x4121c0 -> 0x411a90.
6. "mana reserve >= 0.5 doubles ARMY weight (0x40c890, 0x5ebb38)": wrong. 0.5 is the **Monarch's own
   mana ratio** in its retreat threshold. The economy effect is the allocation gates (0.7, 0.7/3) and the
   cost deferral.
7. "Placement is random scatter, 0x40d010": 0x40d010 is the **Monarch retreat**. Placement is 0x412f80 ->
   0x411200 (block-grid scatter around the base centre, the start location) / 0x411510 (nearest deposit).
8. "massattack = wave timer / army-size threshold": it is a **campaign .ota key** only (x30 ticks); it
   never fires in skirmish. Waves are sized by 0x40c240 from the biggest enemy population (cap 50).
9. "AISquad = Reserve 0x4101e0": the plain homeless squad, slot 96. Category 6 = **naval combat**, not
   transports.
10. "Target: random enemy player, tracked group, 11 threat slots, no reachability, no structure
    priority": wrong. It scores **every enemy unit**, biased to near/visible/armed-or-not/**mana sites**
    (lodestones and mana buildings)/the biggest player, and a new best is **reachability-checked**. The
    "11 threat slots" are the 10 player records.
11. "Home defense radius base x sqrt(enemyCount), difficulty table 14/10/7/5/1": the radius is
    `16*max(trunc(sqrt(F)*s)/2, 5s)` with F = the structure footprint sum and s a **ring index**. No
    enemy count, no difficulty.
12. "wave gated on own+enemy force > sizeGate (0x40de60)": 0x40de60 just counts; p2 is the **dissolve**
    threshold (rand(2n/3)).
13. "LCG 0x535cc0": Park-Miller minimal standard; n < 2 returns 0 without advancing.
14. "economy-first gate stands in for retail's tech unlock": retail has **no tech unlock**; the gate is
    ours and must not be in the Classic level.
15. "Our port: viewer main.cpp 2ee15cb loadAiProfile/tickAiWeighted": stale. It is `src/ai/ai.cpp
    Controller` (server-only, a category planner since 75837d9). Also add the in-sim restored port
    (`Player::retailAi`, `tickRetailAiBase/Strike`, sim/retailai.h, retailplayer.h) and the 13 passing
    `tools/re/check_ai_*` oracles.
16. Add: the retail comparison method (`-AutoStop` = fixed seed 1234 + unthrottled ~140-280 ticks/s;
    the debug-tier poke `[[0x62d558]+4]+4 = 1`; the weight debug print; the `+AI` poke) and the observation that
    the retail AI is economically weak (Taros: one lodestone in 7.7 min, mana near 0).
17. The faction-bug note (tarnecr2) stays valid.

---------------------------------------------------------------------------------------------------
## 7. Verification (adversarial re-check, 2026-10-22)

Method: every routine below was re-read from `KINGDOMS.icd` independently (a disassembler script in
the scratch `verify/` folder, outside the repo), without reusing the area notes' listings. Where it
was cheap it was emulated with `tools/re/emu.py` (scripts in `verify/`), and six repo oracles were
re-run against `build-o2/retail_ai_test` (all PASS). E = emulated in this pass, S = read statically in
this pass. In-place edits to §1 are tagged **[verify-edit]**. The pre-edit plan is kept in the scratch
folder.

| # | Claim | Verdict | Evidence |
|---|---|---|---|
| V1 | rand 0x535cc0 is Park-Miller (x16807 mod 2^31-1), returns a value in [0,n), returns 0 **without advancing** when n<2, one global seed @0x64186c | **CONFIRMED (E)** | `verify/e_rand.py` (scratch): 5000 random (seed, n) cases, n in {0,1,2,3,7,30,...,65536, random}. 0 mismatches against the Python model, return value and new seed both checked. Static: an early out for n < 2. |
| V2 | Timers: base think now+rand(30)+15; strike rand(60)+15; backup +150 fixed; VTOL rand(30)+15; scheduler countdown starts at 30 (ctor 0x40feeb), then rand(7)+3; squads at mgr+0x11, 99 slots, think when deadline <= tick | **CONFIRMED (S)** | rand(30)+15 at 0x40b32b; rand(60)+15 at 0x40e18b; +150 at 0x40ea47; rand(30)+15 at 0x40eb4a; rand(7)+3 at 0x40fd98; a 99-slot loop at 0x40fdb8. Scheduler checks re-ran PASS (check_ai_scheduler 4096). |
| V3 | Bookkeeping: when tick >= last+7, rebuild lists (0x411700), stamp, then rand(30)==0 -> priority refresh 0x410810 (mean gap about 210 ticks). Count cache 0x4119d0 runs every tick | **CONFIRMED (S)** | 0x411a99..0x411ad9. The rand(30) is drawn only inside the 7-tick window. check_build_priority re-ran PASS (8192). |
| V4 | Weight bake: pref starts at 100, `pref = clamp(trunc(pref*w),0,100)` for masked unlocked types; an exact unit name locks, a category word ORs its mask in without locking; integer weights act as an on/off switch | **CONFIRMED (E)** | Reset 0x4106f4 writes pref 100, lock 0, limit -1. `verify/e_weight.py` (scratch) on 0x410c80: w=0.5->50, 0.999->99, 0.019->1, 0.0099->0, 2->100, -1->0, 25->100, 0->0 (locks); pref 50 x 0.5 -> 25 (compounds); a locked type is untouched; only types whose mask bit is set change. Name resolution 0x517240: an exact name sets the lock (0x51727b); a category ORs the mask and clears it (0x5172a8). All three shipped default.txt copies (data/ip/v3) contain only integer weights (0,1,2,3,4,5,8,9,10,11,12,15,25). |
| V5 | Limit: first-writer-wins, kind-2 players only; check allows when player>=10, `+0xe3`==0, limit==-1, or count<limit | **CONFIRMED (S)** | 0x410d50 (lock at +0xf5), 0x4097f0 loops only over `[rec+0x24ee]==2`; 0x409890/0x410de0. |
| V6 | Weighted pick 0x412d00: one pass over the canbuild menu, entry weight `pref*priority/100` (0x412c90, 0 at the limit), `total+=w; if rand(total)<w pick`; class filter yardmap[0]&0x80 vs `special`; a winner from another side returns 0 | **CONFIRMED (S + E)** | 0x412cd9..0x412cf7: an unsigned-byte pref times a signed-byte priority, /100, truncated toward 0. Draw at 0x412d8f..0x412da1. rand(1) does not advance, so the first kept entry costs no draw. The side compare is at 0x412df9 (a string compare of the side name, +0x8a). check_ai_build_choice (4096) and check_ai_build_weight (8192) re-ran PASS. |
| V7 | 14/10/7/5/1 (0x40cfc0) is a **ring index**, not a difficulty. Radius = `16*max(trunc(sqrt(F)*s)/2, 5s)`, F = Σ(unit+0x78 + unit+0x7a) over category-1 members | **CONFIRMED (S + E)** | Jump table @0x40d000 -> 0x40cfd2 (14), cfdb (10), cfe4 (7), cfed (5), other 1. All 11 call sites of 0x40cf30 pass a constant ring (0, 1 or 2) or a role-derived value (0x40cd8b). None reads a player or difficulty field. The footprint sum is 0x40a940. check_ai_base_radius re-ran PASS (7168). |
| V8 | Ground wave roll 0x40c240: E = max(10, the largest word at rec+0x24ec over active, non-allied kind-1/2/3 players); E/10, then E/5; multipliers 1/60 x(rand(10)+5), 1/6 x5, 1/2 x(rand(2)+2); >50 -> rand(10)+41; 1/2 negate | **CONFIRMED (S)**, wording tightened | The two divisions are by 10 and by 5. **Nesting:** the E/5 term is only reached when the E/10 term was taken (both branches skip to 0x40c32c). §1.7 now says so **[verify-edit]**. Not checked: whether the own player counts in E (it needs `own+0xac+self` = 0). |
| V9 | Base think surplus/ready/launch: quota growth when pop/(20 or 30) > p1; surplus counts members at HP>=3/4; negative wave = keep the reserve; launch moves the whole surplus into the backup, strike.p3 += n, p2 += rand(2n/3); VTOL path overwrites p3/p2 on the ground strike | **CONFIRMED (S)**, one addition | 0x40b4e6..0x40b562, 0x40b565..0x40b63a, the 0x40a7b0 HP test `hp >= 3*max/4` (0x40a83a). Launch 0x40b762..0x40b87c. **Added:** the free strike's p2 and p3 are zeroed first (0x40b77f/0x40b788) **[verify-edit]**. The VTOL path is a plain store of both fields, so "writes" is right. |
| V10 | Strike: dissolve when strike+backup <= p2 -> retreat base (firepower >= max(100, own)); keep the target unless gone/dead or rand(250)==0 (drawn only if still valid); re-pick via 0x40a200(centroid, random member); none -> wake now+rand(900)+150; withdraw at HP < max/(rand(3)+3); reachability re-test when changed or rand(20)==0 | **CONFIRMED (S)**, one addition | 0x40e222 compares the count with `S+0x14`, 0x40de60 sums both squads, 0x40e120. Reroll 0x40e318 (rand(250)). Member pick 0x40e359. Empty pick 0x40e3b8 (rand(900)+150). Withdraw 0x40e431..0x40e459 and 0x40e47f. **Added:** dissolve also **clears every member's missions** (0x4d6a50(u,0), keeping only the +0x5a&4 ones) **[verify-edit]**. That matters for how the units look afterwards: they stop. |
| V11 | Target picker 0x40a200 score: INT_MAX/((r(d1/8)+r(d1/8))/4+1)/((r(d2/2)+r(d2/2))/4+1); /(r10+r10+1) if not visible; /(r5+r5+1) unless the type has yardmap 'S'; /(r3+1) if unfinished; /(r3+1) if unarmed; /(r(q)+1), q=min(20, cap/max(20,pop)); strictly-greater wins; reachability only on a new best | **CONFIRMED (S)**, 2 sub-facts settled | Read 0x40a2f3..0x40a548 instruction by instruction; every divisor and draw order matches. *Unfinished*: compares unit+0x108 with the double at 0x5eba68 = **0.0**. +0x108 is the remaining build fraction (the build tick 0x429c0a subtracts from it; docs/retail-engine.md: finished <=> +0x108 == 0), so "unfinished" is right. *Mana site*: type+0x264 bit 31 is set by the yardmap 'S' case of the FBI parser (0x4c0fe3..0x4c0ff9, which also writes cell code 0x8f, so 'S' also sets the &0x80 lodestone class when it is the first yardmap char). Reach range = weapon(+0xc)+0x90 / 16, min 1. UNCONFIRMED: that unit+0x130 bit 0x8000000 means "armed". No direct setter was found (it is likely a byte or word write); 5 other AI routines use it the same way. |
| V12 | Placement 0x411200: 30 attempts; d=rand(R), towers a redrawn sum; angle rand(65536); point = centre + (-d sin a, -d cos a) in 16.16 | **CONFIRMED (S)**, wording **REFUTED/fixed** | 0x4112a0..0x4112c6: the tower distance is `rand(R/2)+rand(R/2)+rand(R/2)` (three draws summed, redrawn while > R). "rand(R/2)x3" in §1.10 read as three times one draw, which gives a different distribution (uniform on [0,1.5R) vs a peaked sum), so it was rewritten **[verify-edit]** (the placement note already had it right). The tower test also needs bmcode (+0x24a) == 0. The 30-attempt counter is at 0x4114d8. |
| V13 | Factory planner 0x40dcb0: nothing if alloc < 0.7/3; one unit at a time; cost>500 and alloc<0.7 -> b=int((cost-500)/300+2), defer when rand(b)!=0 AND pop < rand(30)+30 | **CONFIRMED (S + E)** | Constants decoded: 0x5ebb10 = 0.7, 0x5eb788 = 1/3, 0x5ebb48 = 500f, 0x5ebb44 = 1/300f, 0x5ebb40 = 2f. rand(30) is drawn only after rand(b)!=0. check_ai_factory_plan re-ran PASS (8192). "Anything queued" is really "any mission whose kind byte equals the descriptor at 0x605064". The plan's wording stands, but the exact mission kind belongs with U1/U3. |
| V14 | Assignment: one-shot first pass (flag mgr+0x1a6) only if tick<=30: commanders -> base 1, anchored at player+0xec when mode != 1; each pass: live, finished, uncarried, unassigned (+0xc8==0) units get 0x5198a0(u,2) and join the nearest base (0x40af70) else slot 2 | **CONFIRMED (S)** | 0x40eebb..0x40ef43, 0x40f250..0x40f2be. Note: the one-shot flag is set on the first call even when tick > 30, so a late-starting brain never anchors base 1. |
| V15 | Monarch: alarm 0x409a50 = now+30+3 draws of rand(300) (30..927), cancelling a build mission; retreat threshold maxHP*k/(4m), k=2 when mana/max <= 0.5, m=2 when castable; applies only with ground firepower; 0.5 @0x5ebb38 is used **only** here in the AI | **CONFIRMED (S)** | 0x409ab1..0x409b04 (commander bit +0x264 0x40000, owner kind 2). 0x40c92e..0x40c978. Castable = weapon cost < 0.01 (0x5eb790) or cost <= mana. 0x5ebb38's only reference in 0x409000..0x413000 is 0x40c93f, so the memory's "0.5 doubles army weight" is refuted. |
| V16 | Lodestone pick every think, even when lowFunds, replacing the ordinary pick | **CONFIRMED (S)** | 0x40ca92: lowFunds skips straight to the special=1 pick at 0x40cd20; 0x40c830 gate follows. |
| V17 | Threat = a visible-enemy-list unit within the ring-0 radius | **CONFIRMED (S)** | 0x40b3a0 passes ring edi=0 (zeroed at 0x40b33b) to 0x40cf30. 0x412000 walks the build-manager list (+9..+0xd) and uses Euclidean squared distance (64-bit) against r². |
| D1 | Design: existing levels stay byte-identical | **CONFIRMED (code reading)**, with 3 conditions | (a) No sim code reads aiLevel/Difficulty. It enters the sim only through `matchConfigFor` (src/net/matchconfig.cpp:28-31): manaMult, automaticGates (= type 2) and defensiveAi. For a new level 6, `incomeMultFor` = 1 and `defensiveLevel` = false give exactly a Normal seat's config. kNetVersion is not hashed (no use in src/sim). (b) AI runs only on the server (`simulateRoomTick`, src/server/roomtick.cpp:15). Each Controller has its own `rng_`, so adding a member or a branch does not move another seat's draws. Bundles are built from emitted commands only. **Conditions:** (1) the Classic hand-off must come **before** the `thinkPeriod` gate in `Controller::tick` (ai.cpp:1744), and before `home_`/ally bookkeeping, or the Classic seat would run Normal's prologue (P0 does this); (2) `paramsFor` default -> Normal must stay as it is (ai.cpp:62); (3) the places that size or clamp by kAiLevels move together: server.cpp:2825 (clamp), gameview.h:479 (TAK_AI_LEVEL clamp), gameview_lobby.cpp:1126 (name/colour arrays). Replays: the header already stores slotAiLevel; the referee does not re-run AI on playback. Classic games themselves are not byte-identical to anything, and a burst of Classic commands can hit `replayBudget` (roomtick.cpp:28) only in a game that seats a Classic AI. Line numbers are as of main ca6b493a. |

**Refuted text, fixed in place:** V12 (tower distance wording). **Additions to §1:** V8 nesting, V9 strike
p2/p3 reset, V10 dissolve clears missions, and massattack repeats every assignment pass (0x40f1a1..0x40f239,
per player, not one-shot). **Still UNCONFIRMED** (as of this pass): the meaning of unit flag 0x8000000 ("armed") and whether the
own player counts in the wave-size E. (lowFunds = `[player+0x10c]+8` and the factory "busy" mission
kind, BuildingBuild, were settled in P4a: U4, U3; placement 0x4110f0/0x412f80/0x411200/0x411510 and the
constructor planner 0x40c890 were emulated end to end against the port in P4b.) The wave roll and the picker
were not emulated end to end in this pass. Static reading only for those, so they match the area notes'
emulations without independently replacing them.
