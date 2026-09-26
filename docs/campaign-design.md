> Historical design notes below include superseded implementation inventories.
> For the current native-backed screen behavior, see
> [campaign presentation](campaign-presentation-2026-09-26.md).
> Results already use the authored victory/defeat plates; the 2026-09-26 pass
> replaces the old list picker and front-end briefing with the retail book and
> loaded-world initial pause, and replaces the guessed score formula.

# Campaign system — design & RE notes

Current campaign implementation and evidence for the retail Book of Darien
(base) and Iron Plague campaigns. Updated 2026-09-26. Reverse-engineered by static analysis of `KINGDOMS.icd` + the
shipped mission data (`missions.hpi`, `IPMissions.hpi`, `camps/*.tdf`); see the
per-area findings below. Nothing here is copied from retail code/data.

## 0. `KINGDOMS.icd` vs `ironplague.icd` — settled

They are **the same engine at two patch levels**, not two games. `ironplague.icd`
is an orphaned older build (PE link Dec 1999, FileVersion **2.0.0.1**,
`...\Final_Release\Kingdoms.pdb`); `KINGDOMS.icd` is the shipped final patch (Feb
2000, **3.0.0.1**, `...\V3Final_Release\`) and a strict superset. The install's
`Kingdoms.exe` stub composes `Kingdoms.ICD` — nothing ever loads `ironplague.icd`.
This audit uses the final patched `KINGDOMS.icd` for both campaigns. The
`SelectPlayerAndCampaign` / `PlayerCampaignDialogue` names refer to player/profile
selection, not a character-dialogue overlay. Crusades balance remains selectable
through the existing engine setting.

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
  - `control` — `strategic` (full AI economy+attack) | `passive` (inert NPC/wildlife) | *absent* = human/interactive. Slot 1 is normally the human.
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
`MissionScript` dispatches the authored command string and arguments. The old
Debug-only numeric interpreter was removed; `--mission` now aliases the same
server-driven campaign launch used by the menu and `--campaign`.

### Engine-invoked entry points
`Start` (once at load) · `TriggerHit(regionId, unitId, ownerByte)` (unit enters an
armed region; `ownerByte==0` = the human) · `UnitCreated(unitId, 0)` (build finish)
· `UnitDestroyed(unitId)` · `WindChange` (periodic).

### `MAP_COMMAND` verbs (retail dispatcher `0x4d35c0`)
`Create <TYPE>` (spawn; **returns unit id**; `(x,y)` or `(player,x,y)`) ·
`SetMission <unitId>` + order-queue string (see below) · `SetTrigger <id> …`
(arm region: `(id,x,y,r)` circle / `(id,x1,y1,x2,y2)` rect) · `RemoveTrigger <id>`
· `GetUtype <TYPE>` (**returns** type id) · `WriteValue <name>` / `ReadValue <name>`
(persistent mission variables) · `SetAttribute <Health|Mana|Attack|Armor>Percentage
<unitId> <pct>` · `Capture <unitId>[,player]` · `ScreenShake`. (`Kill`/`Attack`
exist but shipped missions don't use them.) There is **no** message/movie map-command.

### `SetMission` order mini-language (parser `0x513fb0`)
Comma-separated queue; dispatch on first letter:
`m X Y` move · `ma X Y` move-attack · `a <TYPE>` attack-all-of-type · `a X Y`
attack ground · `p X Y[ Z]` patrol · `w N[ M]` wait N s · `wa` wait-for-attack ·
`s` **make selectable** (hand the unit to the player) · `d` self-destruct ·
`b <TYPE> H X Y` timed reinforcement drop · `o A[ B]` stance · `v F` scalar · `c` flag.
Example (`takmission01`): `SetMission s, w 4, m 133 68, m 129 68, w 3, m 133 68, a TARZOM`.

### GET / SET
`GET_UNIT_VALUE id=7` → a unit's type id (compare against `GetUtype`). `GET valId=30`
→ roster/build-list index (already implemented). `SET_UNIT_VALUE(id=2, value=1|0)` →
**force victory / force defeat** (scripted override of the data-driven rules).

### PLAY_SOUND
`PLAY_SOUND <nameIdx>` resolves `nameTable[nameIdx]` as a WAV and preserves its
authored flags. The native mission host submits global, nonlooping audio with
priority from the low three flag bits. Ordered events survive multiple requests
per tick and render snapshots that skip ticks. See the
[campaign dialogue audit](campaign-dialogue-2026-09-26.md), including missing assets.

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

The implementation and focused native comparisons are recorded in the
[condition audit](campaign-conditions-2026-09-26.md). Passing a selection of
missions or reproducing a lockstep hash is not evidence that every mission has
been played from beginning to end against retail.

## 4. Campaign presentation

The retail book picker is `BOD.gui`, with chapter illustrations selected from the
`Story1` sequence. It is distinct from the player/profile chooser. The engine uses
that authored book art, typography, and chapter navigation, while retaining its
existing ability to choose any chapter without unlocking it first. Completion is
tracked in portable settings. Iron Plague's alternate finale remains selectable.

Chapter movies play before loading. Native in-game constructor `4b3360` creates
`Briefing.gui` after the game has loaded; `4b51e0` pauses and fills the chapter,
title, and objective lines. The engine now presents that overlay on the loaded
battlefield. It withholds the initial network Loaded acknowledgement until the
briefing is dismissed, while polling keepalives. The referee therefore cannot
advance the mission while the player reads. Rejoins and spectators skip this
initial modal. The existing objectives panel remains available during play.

Narration and dialogue use the authored movie soundtrack or explicit script sound
requests. The earlier claim that Iron Plague requires a missing in-engine portrait
widget was wrong: `PlayerCampaignDialogue.gui` is a profile chooser. No invented
portrait, dialogue text, or replacement clips are added.

The result screen uses the authored faction victory/defeat plate. Victory records
that chapter's completion; Next follows campaign-list order, Retry replays the
same chapter, and either finale is terminal. Direct Debug campaign launches now
honor Next/Retry as well as menu launches. Chapter titles and the alternate title
come from the shipped translations rather than generic mission-number labels.
See the [presentation audit](campaign-presentation-2026-09-26.md) for native
addresses, rendering checks, and remaining differences.

## 5. Architecture and limits

Campaigns run through the shared authoritative server simulation. The client and
referee independently construct the same mission world and execute its script;
client-owned presentation consumes cosmetic events. Mission conditions, rather
than skirmish last-team-standing, determine the campaign result. Script-driven
reinforcements can revive otherwise empty factions. Strategic opponents use this
project's AI with mission tuning; this is not a port of retail strategic AI.

The mission `.tdf` restricts the player's build menu. Server-side enforcement of
that restriction and a general mapping between arbitrary room seats and mission
human slots are separate limitations. The obsolete client-only mission runner is
gone. Campaign progress remains local portable settings, not retail's Windows
registry/profile format.

This audit targets mission conditions, presentation, and dialogue. It does not
claim that every scripting verb, AI decision, or complete campaign playthrough
has been compared to retail. Native tests run extracted routines under controlled
inputs; asset-backed tests establish loading and rendering of the installed data.
No retail GUI was launched for this pass.

## Integration validation

Final builds completed for all targets in Release, optimized Debug and regular
Debug. All **207 CTest runs passed** (67/70/70), including campaign conditions,
audio events, authored chapter selection, scoring and existing mission regressions.

The optional `campaign_test assets/game --startup` sweep loaded and ran the first
30 ticks of all **74 chapters in both balance modes**: 148 successful openings.
This found and fixed Iron Plague chapter 13's erroneous immediate victory; that
mission has no explicit victory list and must wait for its script. This sweep is
an opening regression, not a complete campaign playthrough.

Release-server/optimized-Debug-client checks exercised loaded briefings in base
chapter 1, Iron Plague chapter 1 and Iron Plague chapter 13. Each screenshot path
asserted that rendering and dismissing the briefing left the tick-zero simulation
hash unchanged. Both campaign book screenshots and the battlefield briefing were
visually reviewed. The normal base-chapter launch and legacy `--mission` alias
repeated the same ten-second state hash, `d10fe3f010796c06`, without desync.

Native comparisons include 7,205 condition cases, 512 mission-audio routing
cases plus objective-cue checks, 112 chapter-art cases, 216 death-score cases and
full score GET/SET host checks. Their controlled scope is detailed in the linked
reports; they do not imply an end-to-end retail GUI comparison.

GCC/Clang determinism checks retained golden `dcef618cd2e4d558`. The harness skipped
ARM cross-build checks because target build dependencies were unavailable.
Protocol **183** requires matching client/server builds and rejects earlier
protocol recordings under the existing replay-version policy.

Two fresh ordinary skirmish multiplayer runs also retained the pre-change hash
`3c4e5e85a939988c`. Mission-only score and condition state does not change the
ordinary skirmish checksum. No pathfinding implementation was changed.
