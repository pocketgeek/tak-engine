# Campaign system — design & RE notes

How the campaign implementation works, and the evidence for the retail Book of
Darien (base) and Iron Plague campaigns. Reverse-engineered by static analysis of `KINGDOMS.icd` + the
shipped mission data (`missions.hpi`, `IPMissions.hpi`, `camps/*.tdf`); see the
per-area findings below. Nothing here is copied from retail code/data.

## 0. `KINGDOMS.icd` vs `ironplague.icd` — settled

They are **the same engine at two patch levels**, not two games. `ironplague.icd`
is an orphaned older build (PE link Dec 1999, FileVersion **2.0.0.1**,
`...\Final_Release\Kingdoms.pdb`); `KINGDOMS.icd` is the shipped final patch (Feb
2000, **3.0.0.1**, `...\V3Final_Release\`) and a strict superset. The install's
`Kingdoms.exe` stub composes `Kingdoms.ICD` — nothing ever loads `ironplague.icd`.
These notes use the final patched `KINGDOMS.icd` for both campaigns. The
`SelectPlayerAndCampaign` / `PlayerCampaignDialogue` names refer to player/profile
selection, not a character-dialogue overlay. Crusades balance is selectable
through the engine setting.

## 1. Data model

A **campaign** is a flat, ordered mission list — pure data, no code:

```
camps/<name>.tdf
  [HEADER]   { campaignside=Core; }          # campaignside is vestigial (TA Arm/Core), ignore
  [MISSION0] { missionfile=<stem>.ota; missionname=<stem>; }
  [MISSION1] { ... }                          # play/unlock order = the section index
```

| Campaign file (archive) | Missions | Stems |
|---|---|---|
| `camps/book of darien.tdf` (`data.hpi`) | **48** | `takmission01_mt` … `takmission47_*` |
| `camps/the iron plague.tdf` (`IPData.hpi`) | **25** | `takx01_dh` … `takx25_dh` |
| `camps/ipalt.tdf` (`IPData.hpi`) | **25** | shares takx01–24, **alt finale `takx26`** instead of `takx25` |

`ipalt` is an *alternate ending* of Iron Plague (the two IP tdfs are byte-identical
through MISSION23, diverging only at the finale). The base list has a comparable
one-off (`takmission39a_mt` sequenced before `takmission39_mt`). The `_mt/_ph/_dh`
suffix is a **narrator/voice-track tag** (also names the movie + briefing wav); it
does **not** track `kingdom`.

A **mission** is a bundle keyed by the stem:

| File (in `missions.hpi` / `IPMissions.hpi`) | Role |
|---|---|
| `<stem>.ota`  | `[GlobalHeader]` (mission config + win/lose rules) + `[Map Data][units]` (placements) |
| `<stem>.tnt`  | terrain |
| `<stem>.cob`  | mission "god" script (COB VM) |
| `<stem>.tdf`  | **allowed unit-type set** for the mission (`[ARAARCH]{} [NPCEMEN]{} …`) — build restriction |
| `<stem>.txt`  | **objectives** text (bulleted lines) |
| `Movies/<stem>.bik` | fullscreen **intro movie** (voice+portraits baked in) |

### `.ota` `[GlobalHeader]` keys that matter

- `kingdom=` — the **terrain tileset / world** (aramon/veruna/taros/zhon/creon/caves/
  volcano). The victory plate follows the local player's faction, not this terrain key.
- `ismission=` (0/1) — a scripting-mode flag (most campaign missions are `0`), not
  the campaign marker.
- `lineofsight=` (fog on/off), `mapping=` (minimap pre-revealed), `maxunits=`.
- `Player<N>=[control] [role] logo <C> [kingdom] [massattack <T>]` — slot defs:
  - `control` — `strategic` (full AI economy+attack) | `passive` (inert NPC/wildlife) | *absent* (in the shipped missions only `Player1`, the human).
  - **`Player1` is always the human**, whatever its keywords: retail's campaign
    start (`0x4d290c`) makes game player 0 the local human from the first record
    and takes only its logo (`0x4d292a`); computer players come from records 2..10
    (the `0x4d2957` loop). `takx22_mt` and `takx24_mt` declare
    `Player1=strategic opponent logo 6 creon` for the city the player defends;
    those keywords are ignored for Player1. An `.ota` without `Player1` still
    reserves world slot 0 for the human.
  - `role` — `opponent` | `ally` | `neutral` (diplomacy toward the human).
  - `logo <C>` — colour/team index; the trailing kingdom word sets the faction (omitted for neutral owners). Units in `[Map Data]` reference the slot via their `Player=` field.
- **Header-embedded victory/defeat conditions** — see §3.

### Text / audio wiring

- Objectives: the `.txt`, displayed by the initial briefing and in-game objectives panel.
- Localized mission names/briefings: `translate/missions.tdf`, `unitmissions.tdf` (`english.hpi`).
- Narrative audio: movie soundtracks and explicit mission `PLAY_SOUND` events. A
  WAV sharing a mission stem is not automatically a request to play it at briefing.
- Per-mission AI tuning: `ai/mission<NN>.txt`.

## 2. Mission scripting (COB)

A mission `.cob` is an ordinary COB v6 module. `MAP_COMMAND` and `PLAY_SOUND`
resolve their inline operands through the module's name table. The current
`MissionScript` dispatches the authored command string and arguments. Debug's
`--mission` is an alias for the same server-driven campaign launch used by the
menu and `--campaign`.

### Engine-invoked entry points
`Start` (once at load) · `TriggerHit(regionId, unitId, ownerByte)` (a unit's
footprint-origin cell is in an armed region; once per unit per region; `ownerByte`
is the `.ota` Player-1, 0 = the human) · `UnitCreated(unitId, ownerByte)` (once per
unit, the first tick it is live and finished: placed, `Create`d and built units)
· `UnitDestroyed(unitId)` · `WindChange` (periodic). Both come from retail's
per-unit check `4d33a0`; see docs/retail-engine.md, Trigger regions.

### `MAP_COMMAND` verbs (retail dispatcher `0x4d35c0`)
`Create <TYPE>` (spawn; **returns unit id**; `(x,y)` or `(player,x,y)`, the type's
footprint corner at the cell) ·
`SetMission <unitId>` + order-queue string (see below) · `SetTrigger <id> …`
(arm region, in cells: `(id,x,y,r)` circle / any other count a rect `(id,x1,y1,x2,y2)`;
an armed slot is not re-armed) · `RemoveTrigger <id>`
· `GetUtype <TYPE>` (**returns** type id) · `WriteValue <name>` / `ReadValue <name>`
(persistent mission variables) · `SetAttribute <Health|Mana|Attack|Armor>Percentage
<unitId> <pct>` · `Capture <unitId>[,player]` · `ScreenShake`. (`Kill`/`Attack`
exist but shipped missions don't use them.) There is **no** message/movie map-command.

### `SetMission` order mini-language (parser `0x513fb0`)
The same language fills a placed unit's `.ota` `InitialMission=`. The string is
cut at every comma; each clause is read on its own (`sscanf`, a clause that does
not match its form does nothing), its first letter in either case picks the
order, and every order is queued behind what the unit already has:
`m X Y` move · `ma X Y` fight-move · `a X Y` fire at the ground there (SUPPRESS;
not ported, the unit goes until the point is in reach) · `a TYPE` AttackUType:
every unit of TYPE not its owner's, nearest first, until none is left · `a NAME`
attack the placed unit NAME · `b TYPE [N]` a factory builds N (default 1) of TYPE
(BuildingBuild) · `b TYPE N X Y` a mobile builder builds TYPE at cell X,Y
(MobileBuild; TYPE must be on its menu, N is unused) · `p X Y [T]` patrol ·
`w F [N]` wait F s, ended early by a visible enemy within N cells · `wa [NAME]`
WaitForAttack: hold until NAME (else the unit itself) is hit or gone · `o A`
standing order (2 offensive, 1 defensive, 0 passive) · `o A B` the move and fire
fields themselves · `v F` fixed veterancy multiplier (rank 10) · `i NAME` start
aboard the transport NAME · `g NAME` guard NAME · `u X Y` unload · `s` make
selectable (owner unchanged; not modelled) · `d` self-destruct when the order
comes up · `c` cloak · `bw…` BuildWeapon and `r` resurrect (no shipped uses).
A NAME is a placed unit's `Ident=`, else its `Unitname`, the first record that
matches; it resolves only in an `InitialMission` (retail hands SetMission an
empty unit table). Details and addresses: [retail-engine.md](retail-engine.md#mission-order-strings-icd-0x513fb0).
Example (`takmission01`): `SetMission s, w 4, m 133 68, m 129 68, w 3, m 133 68, a TARZOM`.

### GET / SET
`GET` and `GET_UNIT_VALUE` reach the same table (retail `4d4060`): 5 = a player's
population (0-based player index, as `Create`'s), 7 = the unit cap, 30 = a unit's type
(compare against `GetUtype`), 31 = a unit's owner, 35/36 = its footprint-origin cell,
40 = a player's score. Details: [retail-engine.md](retail-engine.md#the-mission-get-table-icd-0x4d4060).
`SET_UNIT_VALUE(id=2, value=1|0)` → **force victory / force defeat** (scripted override
of the data-driven rules).

### PLAY_SOUND
`PLAY_SOUND <nameIdx>` resolves `nameTable[nameIdx]` as a WAV and preserves its
authored flags. The native mission host submits global, nonlooping audio with
priority from the low three flag bits. Ordered events survive multiple requests
per tick and render snapshots that skip ticks. Some requested sounds are not
in the shipped data; no replacement clip is added for them.

## 3. Win / lose — **data-driven**

Win/lose is primarily evaluated by the engine from `.ota [GlobalHeader]` condition
keys. Some conditions poll current state; others latch creation/capture/death
events. Victory conditions combine with AND; defeat conditions combine with OR:

- **Victory:** `DestroyAllUnits`, `KillAllMobileUnits`, `KillAllOfType=<T>`,
  `KillUnitType=<T>,<n>`, `KillEnemyCommander`, `MoveUnitToRadius=<T>,<X>,<Z>,<r>`
  (escort), `UnitTypePassesX/Z=<T>,<v>`, `VictoryTimerRunsOut=<s>`, `CaptureUnitType`,
  `BuildUnitType`.
- **Defeat:** `CommanderKilled`, `AllUnitsKilled`, `AllUnitsKilledOfType`,
  `UnitTypeKilled=<T>,<n>`, `DeathTimerRunsOut=<s>`, `AnyUnitPassesX/Z`.
- **Scripted override:** the COB can force the result with `SET_UNIT_VALUE(2, 1|0)`.

The condition evaluators are checked by native comparisons against the retail
routines under controlled inputs. Passing a selection of missions or reproducing
a lockstep hash is not evidence that every mission has been played from
beginning to end against retail.

## 4. Campaign presentation

The retail book picker is `BOD.gui`, with chapter illustrations selected from the
`Story1` sequence. It is distinct from the player/profile chooser. The engine uses
that authored book art, typography, and chapter navigation, and lets the player
choose any chapter without unlocking it first. Completion is tracked in portable
settings. Iron Plague's alternate finale is selectable.

Chapter movies play before loading. Native in-game constructor `4b3360` creates
`Briefing.gui` after the game has loaded; `4b51e0` pauses and fills the chapter,
title, and objective lines. The engine presents that overlay on the loaded
battlefield. It withholds the initial network Loaded acknowledgement until the
briefing is dismissed, while polling keepalives. The referee therefore cannot
advance the mission while the player reads. Rejoins and spectators skip this
initial modal. The objectives panel is available during play.

Narration and dialogue use the authored movie soundtrack or explicit script sound
requests. Iron Plague has no in-engine portrait widget: `PlayerCampaignDialogue.gui`
is a profile chooser. No invented portrait, dialogue text, or replacement clips
are added.

The result screen uses the authored faction victory/defeat plate. Victory records
that chapter's completion; Next follows campaign-list order, Retry replays the
same chapter, and either finale is terminal. Direct Debug campaign launches
honor Next/Retry as menu launches do. Chapter titles and the alternate title
come from the shipped translations rather than generic mission-number labels.

## 5. Architecture and limits

Campaigns run through the shared authoritative server simulation. The client and
referee independently construct the same mission world and execute its script;
client-owned presentation consumes cosmetic events. Mission conditions, rather
than skirmish last-team-standing, determine the campaign result. Script-driven
reinforcements can revive otherwise empty factions. Strategic opponents play the
**Retail AI** (with no choice): a port of retail's strategic AI
over the mission's `aiprofile` file (`ai/mission<NN>.txt`, else
`ai/default.txt`), its `.ota` StartPos table and its Player lines' `massattack`.
A player who disconnects is taken over by the Normal level.

The mission `.tdf` restricts the player's build menu on the client; the server
does not enforce that restriction. The player is seated as retail seats them:
`Player1` is world slot 0, the campaign room seats its host at slot 0 and
refuses seat moves, so the server's command stamping, the referee's world and
the client's view all name the same player. This matters for
`takx22_mt`/`takx24_mt` (Player1 marked `strategic opponent`): the player
drives their own units and is not teamed with the Aramon invaders. Campaign
progress is kept in local portable settings, not retail's Windows
registry/profile format.

The retail comparisons cover mission conditions, presentation, and dialogue.
Not every scripting verb, AI decision, or complete campaign playthrough has
been compared to retail. Native tests run extracted routines under controlled
inputs; asset-backed tests establish loading and rendering of the installed data.
Mission-only score and condition state is not part of the ordinary skirmish
checksum.

## Tests

- CTest covers campaign conditions, mission audio events, authored chapter
  selection, scoring and mission regressions.
- `campaign_test <data> --startup` (optional, asset-backed) loads every chapter
  of both campaigns, Iron Plague's alternate finale included, in both balance
  modes (standard and Crusades), runs its first 30 ticks and requires that no
  mission has ended. Iron Plague chapter 13 has no explicit victory list and
  must wait for its script. This is an opening regression, not a complete
  campaign playthrough.
- Debug `TAK_SHOT_BRIEFING` runs assert that rendering and dismissing the
  loaded briefing leaves the tick-zero simulation hash unchanged ("PASS: loaded
  campaign briefing preserves tick-zero state"), and that the front-end
  campaign catalog survives GameView construction so command-line sessions
  keep it for result/progression and Next/Retry ("PASS: campaign catalog
  retained for result/progression"). The normal command-line launch and the
  `--mission` alias give the same campaign state hash.
- Native comparisons cover the condition evaluators, mission-audio routing and
  objective cues, chapter art, death scores and the score GET/SET host. They do
  not amount to an end-to-end retail GUI comparison.
