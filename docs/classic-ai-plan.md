# Classic AI level: retail AI synthesis and implementation plan

**Status:** P0, phase C, P1 and P2 are on `task-classic-ai` (not yet on main). Since 2026-10-22 the
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
`tarnecr2` table entry. The brain still issues no command. P3 onward are open.

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
**Factory planner 0x40dcb0** (repo-E, check_ai_factory_plan):
- It does nothing if allocation < 0.7/3 (~0.2333) or anything is queued (one unit at a time).
- It picks with 0x412d00(special=0). If allocation < 0.7 and cost > 500: b = int((cost-500)/300+2),
  and it defers when rand(b) != 0 AND population < rand(30)+30. Otherwise it queues one.
- There is **no rally point**. The unit leaves by its exit, joins the base, and patrols (§1.8).

**Constructor planner 0x40c890(unit, threat)** (E via an emulation of the planner, plus S):
- lowFunds = allocation < 0.7. Allocation = min(1, stored / this tick's construction demand) (0x51d837,
  already ported as `RetailConstructionResources`). When nothing is being built it is 1.
- **Commander branch**: retreat threshold = maxHP x k / (4 x m), with k = 2 if its own mana/maxMana <= 0.5
  (const 0.5 @0x5ebb38) and m = 2 if its current weapon is castable now. So with full mana and able to cast
  it flees below 1/8 HP; with one of the two below 1/4; with neither below 1/2 (E). This applies only when
  the base has ground firepower (cat 4). Below the threshold with a threat it cancels its build and
  retreats (0x40d010). If it is fighting, it is left alone. With a threat and no ground army it cancels
  and fights itself (0x40d370). It builds only while cat-2 builders < p1 and the tick is past the
  **monarch alarm** (`mgr+0xd`). Otherwise it acts as a combat member on the tight ring 3.
- **Monarch alarm** 0x409a50 (from the damage path 0x51a340): when an AI commander is hit, deadline =
  now + 30 + rand(300)+rand(300)+rand(300) (30..927, mean ~478 ticks) and its build is cancelled. (E)
- Every builder: if it already has a build mission, return. Otherwise (unless lowFunds) the ordinary pick:
  - A factory type is dropped if the base already has an idle one of that type (S/E).
  - For the Monarch, an armed non-factory structure (a "tower") is dropped at >= 4 of that type (E).
  - Site radius category: Monarch 2 for the first of a type, 3 for later ones; others 1 for a tower and
    2 otherwise (S).
  - Site = 0x412f80 (§1.10). Reachability = 0x4e1570 within builddistance (ported `retailreach.h`).
- **Lodestone pick, drawn every think**: 0x412d00(special=1), radius category 2 for the Monarch and 1
  otherwise. It is used unless a base builder is already building one (0x40c830). The site is the
  nearest free deposit (0x411510). If found and reachable, it **replaces** the ordinary pick.
- It issues build order kind 14 (0x4de530/0x4d78a0), then **always** calls 0x40d370.
- The AI issues no repair, assist, reclaim or guard orders anywhere (S, order-kind census).

### 1.7 Waves: launch, strike, backup, VTOL
- **Wave-size roll, ground 0x40c240** (S, re-read at the listing):
  E = max(10, the largest population among non-allied active players).
  p5 = rand(max(p2,2))+1. With 3/4 chance (rand(4)!=0) add rand(max(E/10,2)), and then, **only inside
  that branch**, with 2/3 chance (rand(3)!=0) add rand(max(E/5,2)) (so the E/5 term has 1/2 overall).
  **[verify-edit: nesting made explicit, 0x40c2ca/0x40c302 both skip to 0x40c32c]** Then: if rand(60)==0, x(rand(10)+5); else if rand(6)==0, x5; else if
  rand(2)==0, x(rand(2)+2). If > 50 -> rand(10)+41. With 1/2 chance negate.
  Air 0x40c3c0 and naval 0x40c490 have the same shape with p3/p4 in place of E and multiplier chances
  1/100, 1/10, 1/2 (air) and 1/100, 1/20, 1/3 (naval). **The attack size tracks the biggest enemy, capped
  at 50.** (S; the squads distribution figures were E)
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
- **massattack** (campaign .ota only, stored x30, per player): from that tick on, **every assignment
  pass** sends each armed, mobile, menu-less unit not already in 97/98 to slot 98 (Stance 2, missions
  cleared) -> 97, so later units follow too. It never fires in skirmish. (S) **[verify-edit: was "at that
  tick"; 0x40f1a1..0x40f239]**

### 1.8 Base defence and idle units: 0x40d370 (S; patrol points repo-E)
- idle-ish = no mission, or mission flag 0x2000. A busy unit with no armed threat re-plans only with
  1/50 chance.
- Ring by role: commander 1 (or 2 if it can't fly) with a threat, 3 without; builder 2; flyer 0;
  others 0 when armed and threatened, else 1. The radius falls back to 320 px. Below 1/4 HP it uses
  max(R/5, 160).
- With a threat and armed: candidates = visible enemies within R + max(sight, range) of the centre
  (0x411ae0). If it is already on one, it keeps it with 49/50 chance. Otherwise it picks two points (up
  to 20/30 tries): the first 9 at rand(range) around a random candidate, later ones at rand(max(sight,
  range)). It issues MOVE centre -> MOVE p1 -> PATROL p2. **Defenders patrol through the intruders.**
- No threat: two points at rand(R/2)+rand(R) from the centre, >= 100 px apart, placeable. It issues MOVE
  centre -> p1 -> PATROL p2 (the ported `retailAiPatrolPoints`).
- **Monarch retreat 0x40d010** (S, re-read): 20 points at rand(R0) px and a random angle around the
  centre, kept if placeable (flyers always). With no enemy within R0 it takes the first valid one;
  otherwise the valid point that **maximises the minimum squared distance** to the listed enemies. MOVE
  (flag 0x10). A flyer also gets a queued patrol leg at (rand(5)+4)x16 px.

### 1.9 Expansion 0x40bc20 (S, partly I)
A random ready free builder with HP >= 3/4 (cat 3 first, then 2) heads for the nearest sacred site that
is on its nav component, is not an existing anchor, and is not within the ring-2 radius of an occupied
base. The move is refused if 3 x enemiesNear(site) > escort size. The builder moves into a new base
slot anchored at that site (anchor -(i+1)). Later placements scatter around the site. Base-to-base
transfers (0x40b9e0) happen only between anchored bases on a shared zone.

### 1.10 Placement 0x412f80 (S + E, five placement emulation runs)
- **Block grid 0x4110f0**, drawn once per AI at the profile reset with 8 draws. Land grid A: stepX
  14+rand(10), stepZ 14+rand(3), offset rand(step)-step/2, margin 6. Water grid B (minwaterdepth >= 0):
  17+rand(20), 17+rand(3), margin 9.
- **Ordinary 0x411200**, up to 30 attempts. d = rand(R), or for a tower (a building with bmcode 0, armed
  (+0x260 0x10000), first weapon range < 1000, no menu) d = rand(R/2)+rand(R/2)+rand(R/2) (the SUM of
  three draws, not 3 x one draw), redrawn while > R, which rings the base. **[verify-edit: was
  "rand(R/2)x3", ambiguous; re-read at 0x4112a0..0x4112c6]** The angle is rand(65536) BAM and the point is
  centre + (-d sin a, -d cos a). Snap the top-left cell to the grid lot and jitter by rand(step-foot-m),
  which leaves "streets" of >= 7 free cells on land and >= 10 on water. Validate with the yardmap test
  (0x507d10 -> 0x507400) and reject any footprint covering a sacred deposit. Distance is uniform, not by
  area, so buildings cluster near the centre. There is no outward search, no memory of failures, and no
  growth of R on failure.
- **Lodestone 0x411510**: the nearest deposit cell (measured from the top-left footprint cell of a
  building centred on the base centre) within R that still holds a sacred feature and passes 0x507d10.
  Its top-left cell goes on the deposit. No randomness.

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
| lowFunds quantity: stored fraction (targeting) vs allocation (build, placement) | **allocation (`player+0x10c -> +8`)** | 2 of 3, and it matches the ported 0x51d837 object. Still open: U4 |
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
| U1 | Mission class bytes 1/2 (attack-type), 7; flags 0x2000 (idle-ish), 0x10000 (interruptible), 8 (build), 0x200/0x400 | contact test, 1/50 re-plan, busy builders | Read the mission descriptor table (0x4df87c, getter 0x4d4cc0 -> +0x10) statically, and emulate the mission constructors the AI calls to see which class bytes and flags they set (no retail run). Then write a mapping table to our `Order` kinds and `autoTarget` |
| U2 | Whether the 1/50 guard at the end of 0x40c890 can replace the build just issued | builders abandoning sites | Emulate 0x40c890 -> 0x40d370 with 0x4d78a0 instrumented (the planner emulation); check 0x4d78a0's replace/queue argument |
| U3 | Factory guard byte at 0x604914 (which mission counts as "busy factory") | duplicate-factory rule | Read the data at 0x604914 and match it against the mission table (static, minutes) |
| U4 | Confirm that `[player+0x10c]+8` is the allocation (min(1, stored/demand)), not stored/capacity | lowFunds 0.7 gate, factory 0.2333 gate | Emulate 0x51d837's store (and the economy step that calls it) on synthetic stores and demands |
| U5 | Expansion gate details 0x40b5c0..0x40b695; units of the "radius 50" in 0x411ae0 for the enemy check | expansion frequency | Static read, then emulate 0x40bc20 on synthetic sites/enemies (new check_ai_expand) |
| U6 | Unit `+0x130 & 0x2000` (excluded from threats and targets) and `+0x104` (must be 0 for a member) | filters | Find the setters by xref and read them statically; emulate a setter where the static reading is unclear (I: dying/being-built-by-transport, or teleporting) |
| U7 | List +0x25 (enemies with +0x130 0x100) used when the AI owns an `istargetingupgrade` unit | defence candidates | `tdftool` grep shipped FBIs for istargetingupgrade. If none ship, ignore it |
| U8 | Where the AI meets the unit cap (not in the planner) | late-game behaviour | Static: the build/queue path 0x429810 and 0x4d78a0 for a cap check; emulate the check; a Classic game test near the cap |
| U9 | Whether `-AutoStop` + a GDB seed poke gives tick-identical retail games per seed | comparison noise floor | **SETTLED (no):** with `-AutoStop`'s per-tick reseed off, same-seed runs diverge from about tick 180-300 (the first building's entity slot follows the unseeded C-runtime `rand`); with it on, every seed plays one game. Retail games are no longer run (§5) |
| U10 | Our `visibleEnemies` excludes `underConstruction` enemies; retail's list +5 excludes 0x2000 instead | threat test, defence | Confirm what retail does with unfinished enemy buildings in list +5 (static 0x411700). If retail includes them, the Classic brain filters its own copy (not the sim list, which is hashed) |
| U11 | A live weight debug print vs our `retailBuildPriority` (e.g. why the mana building shows no lodestone +60) | priority fidelity | **RESOLVED (E, no retail run):** `tools/re/check_ai_printweights.py` reads the 20 captured PrintWeights dumps (5 factions x 4 ticks, Abnar's Terrace) and the manager polls around them. All 1176 rows match: pref 1176/1176, limit 1176/1176, priority 1176/1176, and every dump is reproduced exactly by our port (classic_ai_test --printweights: the brain's type facts, profile limits and priority pass over retail's table) from the polled state and seed plus 5..1238 unknown in-between draws; retail's 0x410810, emulated on the same tables, equals the port on 1300/1300 table passes. Three things had to be right for that, two of them port bugs now fixed: (1) retail's table keeps `tarnecr2` (unitname TARNECR2, objectname TARNECRO) as its own entry, which draws for its 100000 cost at every refresh; the registry now keeps such files in a Classic-only side table and the catalogue is keyed by unitname; (2) the creation pass runs before the profile and before any unit or store exists: every limit -1, counts and population 0 (any population under 10 gives the same draws), fill 0 and an empty history (so a shortfall), which is why Taros's tick-16 table wants lodestones (100) and gives builders +5; the brain now runs it in its constructor that way; (3) the dump's table can be older than the poll before it (Taros 5400: it came in at tick 4590), so the check finds the refresh in the polls itself. Aramana: both 'S' flags set (bmcode default 0), so it is a mana site and a lodestone-class type, as the P2 reading said |
| U12 | Unit cap used by the picker divisor and comparisons: v4.0 default 150 vs v4.1 500 | target bias | Decision: use our lobby cap; comparisons set it explicitly on both sides |
| U13 | How an AI-issued MOVE behaves in our sim compared with retail's MOVE_GROUND under standing fire orders (fire while moving, stop and engage?) | the strike's whole fighting style | Static reading of retail's MOVE_GROUND mission and the standing-fire-order checks it makes (emulate the per-tick mission step where unclear); then our own scripted 10-v-10 game test shows what our Move / AttackMove does. Map retail MOVE accordingly. No retail run |
| U14 | Retail's per-player kind on disconnect / whether the AI ever ignores limits in skirmish (+0xe3 always 1) | none for skirmish | Static only, low priority |
| U15 | The `unsupported("strike recruitment")` and other sim-port branches: whether the server brain reuses the same templates unchanged | code reuse | Code reading while implementing P3 |

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
- **Disconnect takeover** stays Normal; **campaign missions** stay Normal until P8 acceptance (D3).

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
| **P3 Squads + assignment** | 100-slot model, base ctor rolls, scheduler rand(7)+3, tick<=30 Monarch->base 1 anchored at the start location, nearest-base assignment + Stance 0 | check_ai_scheduler / check_ai_squads / check_ai_base_selection reused | an oracle for the assignment pass (0x40eebb..0x40f2be: one-shot anchor, nearest base, slot 2); game test: every finished unit is in a base squad within 10 ticks, member counts per kind over a Classic --mpai game |
| **P4a Factory planner** | 0x40dcb0 (allocation gates, cost deferral, one queued) + the menu draw with faction rejection | check_ai_factory_plan + check_ai_build_choice + check_ai_build_weight (shared templates) | those oracles on the brain's own call path; game test: Classic factories queue one unit at a time, stay within limits, and field a first army unit |
| **P4b Constructor planner + placement** | 0x40c890 (Monarch gates, p1 builders and growth, duplicate-factory, 4-tower cap, lodestone pass every think, reachability), block grid 0x4110f0, scatter 0x411200, deposits 0x411510 | new `check_ai_placement.py` (grid draws, lot windows, tower distance, 30-attempt failure, deposit choice) + `check_ai_constructor.py` (the planner's cases) | those two + check_ai_base_radius; game test: a Classic seat builds lodestones, a keep and a castle on Abnar's Terrace and Inner Circle, placements inside the ring radius, no duplicate factory while one is busy |
| **P5 Base defence, idle, Monarch** | 0x40d370 (rings, wounded radius, threat candidates, patrol points), the 0x412000 threat test, Monarch retreat 0x40d010, alarm 0x409a50 (hp tracking) | check_ai_patrol_points; new `check_ai_defence.py` (candidate points) and `check_ai_retreat.py` (0x40d010 point choice) | those + a threat-test oracle (0x412000); game test: a scripted raid into a Classic base, defenders react inside the ring radius, the Monarch retreats below its HP threshold |
| **P6 Waves** | wave rolls 0x40c240/3c0/490, launch, backup 0x40ea30, strike 0x40e180 (cohesion, dissolve, re-pick, withdrawals, contact/march), picker 0x40a200, VTOL 0x40eb40, naval strikes | `check_ai_wave_roll.py`, `check_ai_target_pick.py`, check_ai_squads strike cases; U13 settled first | those + a strike-think oracle (0x40e180); game tests: waves launch, march by MOVE, dissolve and retreat; Classic AI-vs-AI games end |
| **P7 Expansion + transfers** | 0x40bc20, 0x40b9e0/0x40b050/0x40b140/0x40b230 | new `check_ai_expand.py` after U5 | that + transfer oracles; game test: bases and lodestones grow over a 20-minute Classic game |
| **P8 Acceptance + docs** | Tune nothing that retail doesn't do; user guide, README level table, memory; Classic for campaign opponents (the campaign switch) | every oracle and game test above | **acceptance = every ported routine has a passing emulation oracle (each once shown to fail on a planted bug) + the engine game tests pass + docs and user guide + the campaign switch.** Retail-run distributions (phase C) are optional evidence, not a gate |

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
per player, not one-shot). **Still UNCONFIRMED** (unchanged from §3): lowFunds is `[player+0x10c]+8`
(U4), the meaning of unit flag 0x8000000 ("armed"), whether the own player counts in the wave-size E, and
the exact factory "busy" mission kind (0x605064, see U3). The wave roll, the picker and placement were not emulated
end to end in this pass. Static reading only for those, so they match the area notes'
emulations without independently replacing them.
