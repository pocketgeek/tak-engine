# Darien Crusades evidence matrix

Classifications follow [the reconstruction plan](../../darien-crusades-plan.md).
CONFIRMED here applies only to the precisely worded observation, not an implied
implementation. Source IDs resolve through [sources.md](sources.md).

| ID | Observation / proposed behavior | Classification | Evidence and limits |
|---|---|---|---|
| E01 | Cavedog offered update downloads with and without Darien Crusades | CONFIRMED | WEB-PATCH and fingerprinted downloads |
| E02 | These two patch payloads share engine and Boneyards executable bytes | CONFIRMED | Inventories; includes EXE-3, BYMAIA and ROVER. Does not establish the earlier introduction date of code paths |
| E03 | The larger payload includes maps and extra metagame art/dialogs and five chunks of one movie | CONFIRMED | Generated payload diff and decoded HPI/KMP membership |
| E04 | Darien territory presentation data survives in a shipped definition file | CONFIRMED | [Validated format](territory-format.md): 313 parcels, unique names/IDs, complete observed schema. Declares 871 borders but provides no edge records; not a recovered graph |
| E05 | Occupied territory flames reflect player activity, including the minimap | CONFIRMED | README-3 line 5; exact rendering thresholds not recovered |
| E06 | Territories may change hands without a battle based on fatigue, side support and toughness | CONFIRMED | README-3 line 6 gives a qualitative comparison. Timing, accumulation, side selection, rounding and exact algorithm remain UNKNOWN |
| E07 | Hosts could choose original or Crusades unit balance | CONFIRMED | README-3 line 7; this is distinct from joining the persistent metagame |
| E08 | Recon templates display shared fatigue and side-specific toughness, support and battle victory points | CONFIRMED template schema; RECONSTRUCTED active use | RECON-HISTORY / RECON-0 lines 49–73; EXE-3 recon constructor references at `0x457da7`, `0x459061`, `0x460d8c`. [Parameter analysis](territory-parameters.md); placeholder zeroes are not campaign starting values |
| E09 | Downloaded patch engine and existing local engine differ | CONFIRMED | EXE-3 and LOCAL-MIXED have different SHA-256 values; version/lineage must be audited separately |
| E10 | All 20 added numeric GUI files parse with the existing numeric GUI reader | CONFIRMED tool observation | [Numeric GUI check](numeric-gui.md): 374 gadget records read to EOF; per-file counts in parser-fullchecks.json. Earlier TDF rejection was a wrong-format tool choice; full native UI semantics are not implied |
| E11 | Exact authoritative territory capture algorithm | UNKNOWN | RECON-HISTORY / RECON-0 lines 88–91 document momentum plus combined fatigue/support/battle VPs exceeding toughness. Parameter generation, equality/tie handling, battle-free momentum behavior and server mutation order are not recovered |
| E12 | Border definitions restrict attack eligibility | UNKNOWN | Definition labels alone are not gameplay evidence |
| E13 | Boneyards performs campaign calculations | CONFIRMED documentation claim | HELP-147 FAQ 5 explicitly assigns the math to servers. Exact protocol and division of individual operations remain UNKNOWN; see [rules evidence](campaign-rules-evidence.md) |
| E14 | Allegiance restrictions and battle sizes | CONFIRMED documentation claims; partial binary corroboration | HELP-147 FAQ 13–15, 19–20; entry callback has same-allegiance rejection. Exceptional race combinations and final-service rules unresolved; disconnected/invalid outcomes UNKNOWN |
| E15 | Exact standard-versus-Crusades gameplay value differences | CONFIRMED source/registry comparison with native corrections | [Balance reference](../../crusades-balance-reference.md): three fingerprinted source scopes, 177 changed field paths classified, complete raw/effective reports and final registry checks. [Validation](milestone-one-validation.md) records all tests and scope limits |
| E16 | Exactly 182 maps extracted from this Crusades payload | UNKNOWN | WEB-PATCH advertises 182; observed payload has 181 KMP containers, each with one TNT |
| E17 | A TAK-Engine persistent campaign database and server-issued battle/result objects | MODERN implementation | Milestones 4–10 implement authenticated store, room/result bindings and [territory matchmaking](campaign-matchmaking.md); no original Cavedog implementation claim |
| E18 | MMZ contains a Maia package/update manifest | CONFIRMED contents; RECONSTRUCTED role | [Manifest notes](updater-manifest.md); package versions 30BA/30BB, bundle/resource descriptors, companion update API. Not a campaign database |
| E19 | PreInit is a 313-byte table consumed in ascending territory-ID order, with C/H/T mapped to 0/1/2 | RECONSTRUCTED reader; CONFIRMED bytes | [Binary trace](binary-notes.md). Does not establish live campaign starting owners |
| E20 | wdhit is a decimal table read near crest setup | CONFIRMED bytes; RECONSTRUCTED reader | 80 integers; exact entry meaning UNKNOWN. No recovered campaign script here |
| E21 | Momentum tracks twenty recent battles | CONFIRMED documentation claim; RECONSTRUCTED display | HELP-147 FAQ 5b; client counts H/T history symbols. Server window policy not recovered from code |
| E22 | Results finishing after territory capture can be deferred until it becomes contested again | CONFIRMED documentation claim | HELP-147 FAQ 11 describes orphan battles and entrenchment. Exact persistence, deduplication and reset behavior UNKNOWN |
| E23 | Rank affects battle victory-point stakes | CONFIRMED documentation claim | HELP-147 FAQ 8; numeric weighting UNKNOWN |
| E24 | War objectives combine territorial control with designated locations and can vary between wars | CONFIRMED documentation claim | HELP-147 FAQ 22, 30; exact objective parameters and reset timing UNKNOWN |
| E25 | Territory shapes are flood-filled from image borders and fire anchors to produce artwork and hit testing | RECONSTRUCTED; isolated native check passed | EXE-3 `0x459da0`, `0x45b040`, `0x458e12`; [geometry trace](territory-parameters.md). The reduced buffer stores parcel vector indices, not adjacency edges |
| E26 | Battle entry applies supplied race permissions and forces Crusades balance in campaign mode | RECONSTRUCTED | [Battle contract](battle-contract.md), EXE-3 `0x48b070`; per-territory permissions rather than unconditional FAQ race lists |
| E27 | Per-player statistics are passed to Rover through interface slot 0x78 | RECONSTRUCTED | EXE-3 `0x501042`, `0x5ab719`; Rover initializer resolves slot to `0x10004132` |
| E28 | Rover builds a score_report object with area_id, game_id, dpid and conditional last fields, then adds score properties | RECONSTRUCTED | ROVER `0x10014de0`, `0x10004132`; message construction, not a verified wire packet or server capture algorithm |
| E29 | Score-report success initially means local enqueue, not server acceptance | RECONSTRUCTED | [Transport trace](report-transport.md), ROVER `0x1000df32`, worker `0x10011839` |
| E30 | Tagged messages use delimiter escaping and a rolling byte XOR before socket send | RECONSTRUCTED; isolated native codec checks passed | ROVER serializers and `0x10010e8b`; [transport trace](report-transport.md). No full-message/end-to-end compatibility claim; not cryptographic authentication |
| E31 | Incoming battle_report notifications are dispatched for a matching area/game | RECONSTRUCTED | ROVER `0x1000a378`, `0x1000a522`; acknowledgement and campaign-credit semantics UNKNOWN |
| E32 | Incoming recon properties can reach HTML parameters without explicit field-name strings in EXE-3 | RECONSTRUCTED | EXE-3 `0x4606b9`–`0x46094f` iterates typed values and forwards by name; RECON-HISTORY identifies the requested parameters. [Parameter analysis](territory-parameters.md) |
| E33 | `time_cont` is displayed as elapsed minutes decomposed into days/hours/minutes | RECONSTRUCTED; 1,012 isolated native cases passed | EXE-3 `0x45cd7c`–`0x45cdc6` and `0x45ce26`–`0x45ce73`; does not establish fatigue growth rate |
| E34 | Territory battle-map associations are supplied later through keyed properties rather than Darien.def | RECONSTRUCTED | EXE-3 initial empty strings at `0x459cfc`/`0x459d38`, subsequent `terr_id`, `map_name`, `map_type`, `map_script` callbacks; complete historical assignment table UNKNOWN |
| E35 | Selecting a contested territory routes through its Boneyards area before battle entry | RECONSTRUCTED | [Campaign flow](campaign-flow.md), EXE-3 `0x458e12` → `0x460d30` → `0x4609e0` → `0x486ea0`; does not compute server contest eligibility |
| E36 | Host/join operations queue GameManager commands and receive typed `launch_game` settings | RECONSTRUCTED | EXE-3 campaign host `0x43dfe9`; ROVER builders `0x10014a9b`/`0x10014b4e`, launch handler `0x1000aced`–`0x1000b153`; callback EXE-3 `0x48b070`. No live-server compatibility claim |
| E37 | Final score reporting has a local winner gate; periodic reports and teardown take distinct paths | RECONSTRUCTED | EXE-3 dispatcher `0x500b40`, event-7 winner test `0x500f17`–`0x500f47`, events 99 and 8; [campaign lifecycle](campaign-flow.md). Countdown is dispatcher invocations, not established seconds; losing clients do not automatically traverse the final loop |
| E38 | The audited score payload does not itself update fatigue, support, toughness or territory ownership | RECONSTRUCTED client boundary | EXE-3 tactical player fields and ROVER `score_report`; HELP-147 assigns campaign calculations to the service. Server verification, deduplication and durable credit remain UNKNOWN |
| E39 | Original Iron Plague disc data corroborates the base and expansion gameplay archives used locally | CONFIRMED byte comparison with bounded media provenance | IRON-PLAGUE-CD, GOG-2.0.0.22; [distribution provenance](distribution-provenance.md). Independently listed ISO SHA-1 matches the derived image; no publisher signature claim |
| E40 | Later packaged strategic PNGs match the local assets but their original delivery route is unresolved | CONFIRMED GOG/local equality; original route UNKNOWN | GOG-2.0.0.22, LOCAL-MIXED; no matching loose files in the surveyed original patch/CD cabinet |
| E41 | Original CD and patch Darien definitions differ in presentation fields while retaining the parcel identities/schema | CONFIRMED parsed comparison | IRON-PLAGUE-CD and DARIEN-DEF; 313 IDs, header, native races and terrain agree; 27 parcels differ in anchors/name/description. No adjacency table in either |
| E42 | Five numbered FTUI files assemble into one complete movie | RECONSTRUCTED assembly; CONFIRMED full decode | PATCH-CRUSADES and EXE-3 `0x485a10`; [asset sweep](asset-parse-sweep.md), joined stream size/hash and all 1,110 frames verified |
| E43 | All added maps/sprites/images and the assembled movie are parseable with the recorded exceptions | CONFIRMED tool observations, limited to enumerated scope | [Per-file results](inventories/parser-fullchecks.json): 181 TNT/CRT/OTA, 31 GAF, 60 PNG/PCX, three TDF/TSF; three OTA writes differ, 20 numeric GUI files parse through the existing numeric reader. No claim of all historical online behavior |
| E44 | Received session/address/password reach tactical create/join and common staging | RECONSTRUCTED | EXE-3 `0x48b623`–`0x48b694`, `0x4a47b0`, host `0x49c410`→`0x4e7400`→`0x544d30`, join `0x5454a0`→`0x4e78c0`, staging `0x49f860`; [flow](campaign-flow.md). No recovered authentication token or live first-combat-tick claim |

No production historical rule is inferred from the example formulas in the plan.
See [the acceptance audit](milestone-one-status.md) for unfinished accessible work
and the separate list of bounded historical unknowns. Milestone 1 is not marked
complete by this matrix.

## Balance correctness findings

| ID | Behavior | Classification | Evidence |
|---|---|---|---|
| E45 | Wind callback requires wind or windgenerator | RECONSTRUCTED and native-tested | EXE-3 `0x4bfe91`–`0x4bfed5`, `0x4d453c`; sixteen flag pairs/96 callback cases; corrected engine gate and two-mode roster checks |
| E46 | Damage overrides use the single DamageCategory and precomputed integer damage | RECONSTRUCTED and native-tested | EXE-3 `0x531813`, `0x531b57`, `0x531de0`, `0x531e50`; CRT 53-bit precision traced/executed; [native review](balance-field-review.md), 14 arithmetic/four lookup cases and synthetic/roster regressions |
| E47 | Misspelled watermultipliser is ignored, changing Crusades deer to default1.0 | RECONSTRUCTED and source/registry-tested | EXE-3 `0x4bfc62` reads only watermultiplier with 0x10000 default; original lifdeer/lifdeer2 deltas; corrected loader/research inputs |

## Modern service policy

| ID | Behavior | Classification | Evidence |
|---|---|---|---|
| E48 | Find opponent pairs opposite-alliance searches for one territory in arrival order, with fixed two-player server rules | MODERN implementation | [M10 policy and live validation](campaign-matchmaking.md); recovered territory/war-console resources do not establish the historical matching algorithm |
| E49 | Enrolled campaign members can read immutable verified territory outcomes and watch retained, digest-verified recordings; artifact loss does not change result metadata | MODERN implementation | [M11 history policy and integration checks](campaign-history.md); existing tactical replay layout is reused, historical archive/access policy remains unproven |
