# Initial Darien Crusades evidence matrix

Classifications follow [the reconstruction plan](../../darien-crusades-plan.md).
CONFIRMED here applies only to the precisely worded observation, not an implied
implementation. Source IDs resolve through [sources.md](sources.md).

| ID | Observation / proposed behavior | Classification | Evidence and limits |
|---|---|---|---|
| E01 | Cavedog offered update downloads with and without Darien Crusades | CONFIRMED | WEB-PATCH and fingerprinted downloads |
| E02 | These two patch payloads share engine and Boneyards executable bytes | CONFIRMED | Inventories; includes EXE-3, BYMAIA and ROVER. Does not establish the earlier introduction date of code paths |
| E03 | The larger payload includes maps and extra metagame art/dialogs/movies | CONFIRMED | Generated payload diff and decoded HPI/KMP membership |
| E04 | Darien territory presentation data survives in a shipped definition file | CONFIRMED | DARIEN-DEF has 313 `command=parcel` declarations with 313 distinct names; observed field names include `chatareaid`, anchors, native race, terrain, `parcels`, `borders`. Not yet a fully validated graph |
| E05 | Occupied territory flames reflect player activity, including the minimap | CONFIRMED | README-3 line 5; exact rendering thresholds not recovered |
| E06 | Territories may change hands without a battle based on fatigue, side support and toughness | CONFIRMED | README-3 line 6 gives a qualitative comparison. Timing, accumulation, side selection, rounding and exact algorithm remain UNKNOWN |
| E07 | Hosts could choose original or Crusades unit balance | CONFIRMED | README-3 line 7; this is distinct from joining the persistent metagame |
| E08 | Profile templates request fatigue and side-specific toughness values | CONFIRMED | Both payloads: `Boneyards/Profile/TAK_reconhistory.htm` and `Tak_rec0.htm`, lines 50–59, placeholders `htoughness`, `ttoughness`, `fatigue`. Placeholder presence does not establish value authority |
| E09 | Downloaded patch engine and existing local engine differ | CONFIRMED | EXE-3 and LOCAL-MIXED have different SHA-256 values; version/lineage must be audited separately |
| E10 | Tested numeric Boneyards GUI files are rejected by the ordinary TDF parser | CONFIRMED | All 20 added GUI candidates rejected by current `tdftool`; 181 OTA, one TDF, one TNT passed |
| E11 | Exact territory capture algorithm | UNKNOWN | Readme is insufficient to implement historical arithmetic |
| E12 | Border definitions restrict attack eligibility | UNKNOWN | Definition labels alone are not gameplay evidence |
| E13 | Exact server/client division of campaign authority | UNKNOWN | Inventory locates components, but no code-path or protocol trace yet |
| E14 | Allegiance restrictions, matchmaking and invalid/disconnected battle outcomes | UNKNOWN | No verified original rule recovered in this pass |
| E15 | Exact standard-versus-Crusades gameplay value differences | UNKNOWN in this pass | Both packages share V3Rocket; the internal base/Crusades data needs a separate per-field audit |
| E16 | Exactly 182 maps extracted from this Crusades payload | UNKNOWN | WEB-PATCH advertises 182; observed payload has 181 KMP containers, each with one TNT |
| E17 | A TAK-Engine persistent campaign database and server-issued battle/result objects | MODERN design proposal | Reconstruction plan, not an implemented feature or an original Cavedog implementation claim |

No production historical rule is inferred from the example formulas in the plan.
The current work does not claim completion of Milestone 1's binary/protocol audit
or its Crusades Balance acceptance criteria.
