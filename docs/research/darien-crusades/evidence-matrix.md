# Initial Darien Crusades evidence matrix

Classifications follow [the reconstruction plan](../../darien-crusades-plan.md).
CONFIRMED here applies only to the precisely worded observation, not an implied
implementation. Source IDs resolve through [sources.md](sources.md).

| ID | Observation / proposed behavior | Classification | Evidence and limits |
|---|---|---|---|
| E01 | Cavedog offered update downloads with and without Darien Crusades | CONFIRMED | WEB-PATCH and fingerprinted downloads |
| E02 | These two patch payloads share engine and Boneyards executable bytes | CONFIRMED | Inventories; includes EXE-3, BYMAIA and ROVER. Does not establish the earlier introduction date of code paths |
| E03 | The larger payload includes maps and extra metagame art/dialogs/movies | CONFIRMED | Generated payload diff and decoded HPI/KMP membership |
| E04 | Darien territory presentation data survives in a shipped definition file | CONFIRMED | [Validated format](territory-format.md): 313 parcels, unique names/IDs, complete observed schema. Declares 871 borders but provides no edge records; not a recovered graph |
| E05 | Occupied territory flames reflect player activity, including the minimap | CONFIRMED | README-3 line 5; exact rendering thresholds not recovered |
| E06 | Territories may change hands without a battle based on fatigue, side support and toughness | CONFIRMED | README-3 line 6 gives a qualitative comparison. Timing, accumulation, side selection, rounding and exact algorithm remain UNKNOWN |
| E07 | Hosts could choose original or Crusades unit balance | CONFIRMED | README-3 line 7; this is distinct from joining the persistent metagame |
| E08 | Profile templates request fatigue and side-specific toughness values | CONFIRMED | Both payloads: `Boneyards/Profile/TAK_reconhistory.htm` and `Tak_rec0.htm`, lines 50–59, placeholders `htoughness`, `ttoughness`, `fatigue`. Placeholder presence does not establish value authority |
| E09 | Downloaded patch engine and existing local engine differ | CONFIRMED | EXE-3 and LOCAL-MIXED have different SHA-256 values; version/lineage must be audited separately |
| E10 | Tested numeric Boneyards GUI files are rejected by the ordinary TDF parser | CONFIRMED | All 20 added GUI candidates rejected by current `tdftool`; 181 OTA, one TDF, one TNT passed |
| E11 | Exact territory capture algorithm | UNKNOWN | Readme is insufficient to implement historical arithmetic |
| E12 | Border definitions restrict attack eligibility | UNKNOWN | Definition labels alone are not gameplay evidence |
| E13 | Boneyards performs campaign calculations | CONFIRMED documentation claim | HELP-147 FAQ 5 explicitly assigns the math to servers. Exact protocol and division of individual operations remain UNKNOWN; see [rules evidence](campaign-rules-evidence.md) |
| E14 | Allegiance restrictions and battle sizes | CONFIRMED documentation claims; partial binary corroboration | HELP-147 FAQ 13–15, 19–20; entry callback has same-allegiance rejection. Exceptional race combinations and final-service rules unresolved; disconnected/invalid outcomes UNKNOWN |
| E15 | Exact standard-versus-Crusades gameplay value differences | UNKNOWN in this pass | Both packages share V3Rocket; the internal base/Crusades data needs a separate per-field audit |
| E16 | Exactly 182 maps extracted from this Crusades payload | UNKNOWN | WEB-PATCH advertises 182; observed payload has 181 KMP containers, each with one TNT |
| E17 | A TAK-Engine persistent campaign database and server-issued battle/result objects | MODERN design proposal | Reconstruction plan, not an implemented feature or an original Cavedog implementation claim |
| E18 | MMZ contains a Maia package/update manifest | CONFIRMED contents; RECONSTRUCTED role | [Manifest notes](updater-manifest.md); package versions 30BA/30BB, bundle/resource descriptors, companion update API. Not a campaign database |
| E19 | PreInit is a 313-byte table consumed in ascending territory-ID order, with C/H/T mapped to 0/1/2 | RECONSTRUCTED reader; CONFIRMED bytes | [Binary trace](binary-notes.md). Does not establish live campaign starting owners |
| E20 | wdhit is a decimal table read near crest setup | CONFIRMED bytes; RECONSTRUCTED reader | 80 integers; exact entry meaning UNKNOWN. No recovered campaign script here |
| E21 | Momentum tracks twenty recent battles | CONFIRMED documentation claim; RECONSTRUCTED display | HELP-147 FAQ 5b; client counts H/T history symbols. Server window policy not recovered from code |
| E22 | Results finishing after territory capture can be deferred until it becomes contested again | CONFIRMED documentation claim | HELP-147 FAQ 11 describes orphan battles and entrenchment. Exact persistence, deduplication and reset behavior UNKNOWN |
| E23 | Rank affects battle victory-point stakes | CONFIRMED documentation claim | HELP-147 FAQ 8; numeric weighting UNKNOWN |
| E24 | War objectives combine territorial control with designated locations and can vary between wars | CONFIRMED documentation claim | HELP-147 FAQ 22, 30; exact objective parameters and reset timing UNKNOWN |
| E25 | Borders.png is read and converted into an image-sized buffer | RECONSTRUCTED | EXE-3 `0x459da0` onward. This does not recover the campaign adjacency graph |
| E26 | Battle entry applies supplied race permissions and forces Crusades balance in campaign mode | RECONSTRUCTED | [Battle contract](battle-contract.md), EXE-3 `0x48b070`; per-territory permissions rather than unconditional FAQ race lists |
| E27 | Per-player statistics are passed to Rover through interface slot 0x78 | RECONSTRUCTED | EXE-3 `0x501042`, `0x5ab719`; Rover initializer resolves slot to `0x10004132` |
| E28 | Rover builds a score_report object with area_id, game_id, dpid and conditional last fields, then adds score properties | RECONSTRUCTED | ROVER `0x10014de0`, `0x10004132`; message construction, not a verified wire packet or server capture algorithm |
| E29 | Score-report success initially means local enqueue, not server acceptance | RECONSTRUCTED | [Transport trace](report-transport.md), ROVER `0x1000df32`, worker `0x10011839` |
| E30 | Tagged messages use delimiter escaping and a rolling byte XOR before socket send | RECONSTRUCTED; isolated native codec checks passed | ROVER serializers and `0x10010e8b`; full-message and end-to-end validation pending |
| E31 | Incoming battle_report notifications are dispatched for a matching area/game | RECONSTRUCTED | ROVER `0x1000a378`, `0x1000a522`; acknowledgement and campaign-credit semantics UNKNOWN |

No production historical rule is inferred from the example formulas in the plan.
The current work does not claim completion of Milestone 1's binary/protocol audit
or its Crusades Balance acceptance criteria.
