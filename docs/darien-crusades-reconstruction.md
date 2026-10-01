# Darien Crusades: historical validation and implementation boundaries

Milestone 12 audit, 2026-09-30. Implementation baseline: `d298430aba88`
(0.7.17, multiplayer protocol 211, campaign payload 4, store schema 8).

TAK-Engine has evidence-backed strategic data and presentation, plus a modern
authenticated battle, referee and history service. It does **not** yet reproduce
the original server's territory calculations or complete war lifecycle. A
verified tactical win is recorded, but the default historical policy leaves
territory ownership and recon values unchanged. This audit completes M12's
evidence and decision record; it does not declare historical gameplay parity.

The Darien strategic service is separate from the Book of Darien single-player
missions and from the Crusades Balance option in ordinary skirmishes. It does
not add campaign state to the RTS state hash or change retail pathfinding.

## Reading the classifications

| Classification | Meaning in this audit |
| --- | --- |
| **CONFIRMED** | A direct observation of fingerprinted original documentation, resources or data. A documented rule is classified as a documentation claim, not proof that every released server enforced it. |
| **RECONSTRUCTED** | Behavior derived from identified retail instructions, sometimes checked with isolated native emulation. The claim covers the named client operation, not an unavailable original server. |
| **INFERRED** | A plausible interpretation that remains unproved. It cannot supply authoritative campaign rules. |
| **MODERN** | An explicit clean-room implementation or service decision, including how the engine preserves missing evidence. Passing engine tests validates this decision, not retail parity. |
| **UNKNOWN** | The original behavior or required data has not been recovered. Missing does not mean zero, contested, an empty graph or an invented default. |

HELP-144 explicitly describes the December 1999 open beta. HELP-147 also
contains beta-era and future-expansion language. Their inclusion in later
packages does not prove unchanged final-service behavior. README-3 supplies
later 3.0 observations; differences in wording are retained rather than silently
merged into one alleged rule set.

## Evidence and reproducibility

The [source ledger](research/darien-crusades/sources.md) records provenance,
package hashes and artifact hashes. The [evidence matrix](research/darien-crusades/evidence-matrix.md)
and [M1 acceptance report](research/darien-crusades/milestone-one-status.md)
index the earlier research. This audit re-read the locally retained primary
help/readme/templates and rechecked their hashes; it did not launch the retail
game or contact the original Boneyards service.

| Evidence ID | Artifact and locator used below |
| --- | --- |
| HELP-144 | `Boneyards/Help/Help144.htm`: beta introduction and sophisticated rules. |
| HELP-146 | `Boneyards/Help/Help146.htm`: strategic map, recon, overview, history and Crusades Room descriptions. |
| HELP-147 | `Boneyards/Help/Help147.htm`: numbered FAQ. Numbers below are question numbers, not HTML line numbers. |
| README-3 | `v3readme.txt`, lines 5–7: player-count flames, battle-free capture and the host balance selector. |
| DARIEN-DEF | `Boneyards/Metagame/Darien.def`: strict whole-file parse; [schema and aggregates](research/darien-crusades/territory-format.md). |
| PreInit | `Boneyards/Metagame/PreInit.jje`: 313-byte companion; provenance in the territory-format report. |
| RECON-HISTORY / RECON-0 | `Boneyards/Profile/TAK_reconhistory.htm` / `Tak_rec0.htm`, lines 49–79 and 88–91: supplied victory-point fields and capture explanation. |
| EXE-3 | Patch `KINGDOMS.icd`, SHA-256 `6ddc7fae0cbf3ee13d530614b64608a3aeefca7deb73719310a4f68fe1f22e96`. All executable addresses below belong to this binary. |
| ROVER | `Rover.dll`, SHA-256 `ffedddf9b615e7a60303231c98541ab0147d55fd8d2e1de510272dff0481ffd6`. DLL addresses below belong to this binary. |

Help, templates, definition and PreInit match between the retained standard and
Crusades 3.0 patch payloads. Four strategic PNGs in the local installation also
match the independent retained GOG extraction. These are **CONFIRMED byte
observations**. Mirror provenance is documented, but no authenticated publisher
signature was recovered; packaged byte equality does not establish the original
online service version or how strategic images were delivered in 1999.

## Alliance rules

| Behavior | Classification and evidence | Engine behavior and reason |
| --- | --- | --- |
| Campaign sides are the Order of Honor and Council of Terror, distinct from tactical races. | **CONFIRMED** documentation: HELP-147 FAQ 1, 12, 15. | The [campaign model](research/darien-crusades/campaign-model.md) separates owner/allegiance from native faction. Short UI labels are Honor and Terror. |
| Honor commonly permits Aramon/Zhon/Veruna and Terror Taros/Zhon/Veruna, with exceptions. | **CONFIRMED** documentation: FAQ 15, 20. | **MODERN** live duels use Aramon for Honor and Taros for Terror. This is a restricted server roster, not reconstruction of the original race-selection policy. |
| Native entry consumes supplied `map_races_allowed`; A/T/V/Z/C map to kingdom slots. | **RECONSTRUCTED**: EXE-3 `0x48b43b`, `0x48b487–0x48b507`, `0x634c65–0x634c69`; [battle contract](research/darien-crusades/battle-contract.md). | The current campaign context has no equivalent historical race-permission table. Exact fifth-slot eligibility and final-service exceptions remain **UNKNOWN**. |
| Changing allegiance costs one rank; a House requires matching allegiance. | **CONFIRMED** beta-era documentation: FAQ 19. Exact demotion/floor/House rules are **UNKNOWN**. | **MODERN** per-campaign switches are immediate and free. There is no implemented rank or House system. [Allegiance contract](research/darien-crusades/campaign-allegiance.md) makes this explicit. |
| Authenticated accounts enroll separately per campaign, with revision-checked durable changes. | **MODERN** identity and persistence policy. | [`handleAllegiance`](../src/server/crusades/allegiance.cpp) and [`CampaignStore::setAllegiance`](../src/server/crusades/store.cpp) bind canonical account identity and audit changes. Tests: `crusades_allegiance`, `crusades_auth_network`. |

## Territory graph and starting ownership

| Behavior | Classification and evidence | Engine behavior and reason |
| --- | --- | --- |
| Darien has 313 unique named parcels, noncontiguous IDs 8193–8506, and a header declaring 871 borders. | **CONFIRMED** DARIEN-DEF observations; [strict format report](research/darien-crusades/territory-format.md). | [`crusades_import`](../tools/crusades_import.cpp) preserves IDs, names, native factions and terrain. It creates an unowned, unmapped definition, not a complete playable historical dataset. |
| Exact historical neighbor relationships. | **UNKNOWN**: DARIEN-DEF contains no edge records; 871 is a count, not an edge list. | **MODERN** optional authored neighbors must be valid reciprocal relationships. Absent neighbors mean unknown; an explicitly empty list means isolated. [`campaign.cpp`](../src/server/crusades/campaign.cpp), `crusades_campaign`. |
| Border-image flood fill provides parcel painting and selection. | **RECONSTRUCTED**: EXE-3 `0x459dcf`, `0x459e66–0x459f2e`, `0x45b040`, `0x45b0cb–0x45b0de`; [geometry trace](research/darien-crusades/territory-parameters.md). | **MODERN bounded reconstruction** uses full-resolution stable territory IDs and one seed per parcel. Native lookup uses a reduced buffer of vector indices and special extra seeds. Geometry never generates authoritative adjacency. |
| Current simple fill maps every parcel anchor. | **CONFIRMED** local asset observation: 313 mapped anchors, 15 unreached color-key pixels. | Unmapped pixels stay gray and unselectable; the list still exposes every parcel. [`crusadesmap.cpp`](../src/client/crusadesmap.cpp), `crusades_presentation`. This is not pixel-exact native hit testing. |
| First crusade broadly begins with Honor west, Terror east and contested land between. | **CONFIRMED** beta-era documentation: HELP-147 FAQ 4. | The precise parcel owner table and subsequent-war defaults remain **UNKNOWN**. Native faction does not supply them. |
| PreInit contains 119 H, 165 T and 29 C bytes. Reader maps C/H/T to 0/1/2 in ascending unsigned territory-ID order. | **CONFIRMED** bytes; **RECONSTRUCTED** reader: EXE-3 `0x46b200`, `0x46b244–0x46b2d8`, `0x4735c0`, `0x4737e0`; [binary notes](research/darien-crusades/binary-notes.md). | A movie/replay role is **INFERRED**; its relationship to live initial ownership is **UNKNOWN**. Import does not apply it as a starting-owner table. |
| Unknown ownership is represented separately from contested. | **MODERN** preservation of uncertainty; no historical Unknown owner state is claimed. | [`CampaignState`](../src/server/crusades/campaign.h) uses optional owner/metrics; fresh state stays unknown, including when native faction is supplied. `crusades_campaign` verifies the distinction. |
| Territories receive map/restriction associations. | **CONFIRMED** documentation: HELP-146 recon description, HELP-147 FAQ 4. **RECONSTRUCTED** runtime callbacks: EXE-3 `0x45cb0d–0x45cb24`, `0x45cbd6`, `0x45cbf4`, `0x45e9d2`, `0x45e9ef`. | Complete historical map/restriction tables are **UNKNOWN**. **MODERN** authored map preferences and optional assigned-map overrides determine effective map. Matching names to packaged maps is insufficient evidence. |

## Fatigue, support and toughness

The [parameter trace](research/darien-crusades/territory-parameters.md) separates
supplied display values from authoritative calculation. The
[rules evidence register](research/darien-crusades/campaign-rules-evidence.md)
explains why qualitative descriptions cannot supply a numeric rule engine.

| Behavior | Classification and evidence | Engine behavior and reason |
| --- | --- | --- |
| Toughness is required victory points, separately for Honor and Terror (`htoughness`, `ttoughness`). | **CONFIRMED** template bindings, lines 49–52. | Optional `requiredVictoryPoints` for each side in [`ReconMetrics`](../src/server/crusades/campaign.h). No guessed initial toughness. |
| Fatigue is one shared victory-point field used in both side columns (`fatigue`). | **CONFIRMED** template bindings, lines 56–59. | One optional `fatigueVictoryPoints`; it is not two side-specific timers. |
| Support is side-specific victory points (`hinfluence`, `tinfluence`). | **CONFIRMED** template bindings, lines 63–66. | Optional `supportVictoryPoints` per side. Bare `influence` is a separate ownership-label display in EXE-3 `0x45ee30–0x45efb5`, **RECONSTRUCTED**, and is not used as numerical support. |
| Battle victory points are side-specific (`hvictory_pnts`, `tvictory_pnts`). | **CONFIRMED** template bindings, lines 70–73. | Optional `battleVictoryPoints` per side; the tactical score is a different value. |
| Original template zeroes define initial server values. | **UNKNOWN**, unsupported interpretation: these are display fallback values. | **MODERN** unknown values remain absent. Finite double storage is an explicit representation choice, not a claim about original precision or ranges. `crusades_campaign`, `crusades_rules`. |
| Native recon forwards server-supplied string and numeric properties. | **RECONSTRUCTED**: EXE-3 `0x4606b9–0x46094f`; templates are referenced at `0x457da7`, `0x459061`, `0x460d8c`. | Literal field-name absence in the executable does not make template fields unused. The trace establishes display plumbing, not client-side territory mathematics. |
| Time contested affects resistance; neighboring ownership supplies support; traffic and rank affect campaign dynamics. | **CONFIRMED** beta-era descriptions: HELP-144 rules, HELP-147 FAQ 5, 8. Exact rates, weights, reset rules, ranges and order are **UNKNOWN**. | Default historical evaluation calculates none of these. No metric grows from elapsed wall time or an inferred image graph. |
| `time_cont` is formatted from minutes into days/hours/minutes. | **RECONSTRUCTED**: EXE-3 `0x45cba4`, `0x45cd7c–0x45cdc6`, `0x45ce26–0x45ce73`; earlier isolated 1,012-case native check. | This supplies a display unit, not fatigue growth. Modern matchmaking Unix-second deadlines are unrelated. |
| Momentum considers twenty recent games. | **CONFIRMED** documentation: HELP-147 FAQ 5b. Exact server window maintenance is **UNKNOWN**. | There is no fabricated twenty-entry authoritative history algorithm. |
| Native momentum display counts uppercase H/T until NUL, ignores other symbols, and has no twenty-entry cap; ties skip both side-formatting branches. | **RECONSTRUCTED**: EXE-3 `0x4603ce–0x460410`, `0x460473`, `0x460524`, `0x46054d–0x46062f`. | [`countDisplayedMomentum`](../src/server/crusades/rules.cpp) reproduces counts only; it does not choose server momentum. Existing text on native ties is **UNKNOWN**. `crusades_rules` checks case, NUL and over-twenty input. |
| Momentum plus combined fatigue/support/battle points exceeding required points explains capture. | **CONFIRMED** presentation: template lines 88–91. Exact authoritative equality, rounding, simultaneous-side, timing and reset semantics are **UNKNOWN**. | **MODERN** `inspectHistoricalCapture` is advisory only: requires seven finite supplied fields and an explicit side; equality, overflow or both sides qualifying are indeterminate. It never mutates campaign state. |
| Fatigue/support can take territory without a battle. | **CONFIRMED** later documentation: README-3 line 6. Its relationship to momentum is **UNKNOWN**. | No invented automatic-capture timer or momentum exemption. The historical evaluator preserves state. |

## Battle eligibility and Crusades Balance

| Behavior | Classification and evidence | Engine behavior and reason |
| --- | --- | --- |
| Campaign battles occur on contested territories, whose gatherings represent territory areas. | **CONFIRMED** documentation: HELP-146 map/room descriptions, HELP-147 FAQ 4. **RECONSTRUCTED** entry sequence: EXE-3 `0x458e12 → 0x460d30 → 0x4609e0 → 0x486ea0`; [flow trace](research/darien-crusades/campaign-flow.md). | **MODERN deviation**: current availability checks an authored usable map and server requirements, not owner==Contested. Unknown owners are not silently promoted to contested. |
| Two- and four-player battles, including two allies together, are offered. | **CONFIRMED** documentation: FAQ 13–14 and original host resources. Final-service special modes are **UNKNOWN**. | **MODERN** live contract accepts exactly two opposing human accounts, no campaign AI or spectators. Original four-player authorization is not implemented. |
| Native client can communicate same-side/capacity rejection. | **RECONSTRUCTED**: EXE-3 `0x48b070–0x48b128`; original precise status/limit policy is **UNKNOWN**. | **MODERN** opposite sides and two-player capacity are explicitly enforced; matching concepts do not imply wire compatibility. |
| Crusades battle launch requires Crusades Balance. | **RECONSTRUCTED** native launch/balance boundary in [battle contract](research/darien-crusades/battle-contract.md) and [balance reference](crusades-balance-reference.md). | **MODERN enforcement**: `Server::createCampaignBattle` requires CB data, loads the CB registry, forces `crusades=1` and clears overrides; store context also requires CB. Ordinary skirmishes still choose their balance mode separately. |
| Changed balance data and native rule effects are identified. | **CONFIRMED** source-scoped data comparisons; **RECONSTRUCTED** native behavior where documented. | [Field coverage](research/darien-crusades/balance/field-coverage.md), [field review](research/darien-crusades/balance-field-review.md), and [M1 validation](research/darien-crusades/milestone-one-validation.md) cover changed fields/native damage, wind and water-key fixes. `crusades_registry` and `crusades_damage` validate current code, not strategic rules. |
| Live modern eligibility requires installed CB data, a server-loadable non-generated/non-mission effective map with at least two starts, enrolled opposite sides and available accounts. | **MODERN** explicit policy. | [`Server::campaignMatchmaking`](../src/server/server.cpp) / battle creation and [`CampaignStore::issueBattle`](../src/server/crusades/store.cpp); [matchmaking contract](research/darien-crusades/campaign-matchmaking.md). Directory presence is not cryptographic proof of an authentic historical installation. |
| FIFO pairing uses campaign, territory and revision; older account hosts; one account-wide search/reservation; ten-minute search and launch-offer deadlines. | **MODERN** scheduling and concurrency policy. Original matchmaking order, expiry and hosting policy are **UNKNOWN**. | [`MatchQueue`](../src/server/crusades/matchmaking.cpp) preserves arrival order/deadlines on repeat searches; cancellation, disconnect and restart invalidate waiting searches. Started battle reservations remain until terminal status. Tests: `crusades_matchmaking`, `crusades_matchmaking_network`, actual-client matchmaking UI gate. |
| Launch binds map/data/options/seed/roster, revisions and one-use room/session authority. | **MODERN** safety policy. | [Battle issuance contract](research/darien-crusades/campaign-battles.md), `crusades_battle`, `crusades_battle_network`. No original Boneyards ticket/authentication protocol is claimed. |

## Result handling

The [original battle/report contract](research/darien-crusades/battle-contract.md)
and [Rover transport trace](research/darien-crusades/report-transport.md) are
separate from the [modern authoritative results contract](research/darien-crusades/campaign-results.md).

| Behavior | Classification and evidence | Engine behavior and reason |
| --- | --- | --- |
| Original client builds `score_report` player data behind winner/event checks and supplies area/game/player routing (`area_id`, `game_id`, `dpid`). | **RECONSTRUCTED** EXE-3/ROVER path in battle contract. | The clean-room server does not accept this historical report protocol or trust client-provided winners. These fields do not independently prove campaign-territory/account binding. |
| Rover serializes typed tagged sections, escapes delimiters by indexed percent codes and applies a rolling counter XOR before socket submission. | **RECONSTRUCTED**: ROVER `0x10010fc6`, `0x10012df7`, `0x10013be5`, `0x10013efe`, `0x10010e8b`; earlier isolated codec checks. | Local enqueue/send success is not original campaign credit, cryptographic integrity or durable server acknowledgement. Original acceptance, retries, deduplication and dispute policy remain **UNKNOWN**. |
| Incoming `battle_report` carries routed player statistics/callbacks. | **RECONSTRUCTED** handlers `0x1000a378`, `0x1000a522`. Whether they acknowledge submitted results or award territory credit is **UNKNOWN**. | Modern integrity/results use a separate protocol and referee; matching game IDs do not prove original final commit. |
| Rank changes the victory points wagered; Boneyards performs campaign calculations. | **CONFIRMED** documentation: HELP-147 FAQ 8 and FAQ 5 closing paragraph. Exact weighting/table is **UNKNOWN**. | Tactical kills/score in history are not rank-weighted campaign victory points. |
| Referee derives tactical winner/statistics from server-ordered commands and shared deterministic simulation; no client winner upload. | **MODERN** authority decision. | `Server::finalizeCampaign`, store `validateResult` / `recordVerifiedResult`; campaign victory/resignation require a living winner and durable replay, with failure/no-winner outcomes separately recorded. `crusades_result`, `crusades_result_network`, protocol tests. |
| Explicit resignation defeats the resigning player through a server event; temporary disconnect uses grace/resume; campaign manual pause is disabled. | **MODERN** match lifecycle policy. | Server `CampaignForfeit`, shared event handler and result validation. These policies are not recovered original abandonment rules. |
| Capture during an unfinished battle does not discard its result; orphan credit contributes to later entrenchment when the territory becomes contested again. | **CONFIRMED** beta-era documentation: FAQ 11. Exact deferred-credit arithmetic/duration/deduplication remains **UNKNOWN**. | **MODERN deviation**: stale campaign/allegiance revisions prevent credit; server retains a no-credit abort. Immutable history is not an implementation of deferred entrenchment. |
| Eligible default historical results record `UnknownRules` and preserve ownership, metrics and campaign revision. | **MODERN** fail-closed evidence policy, justified by missing original calculations. | [`evaluateRules`](../src/server/crusades/rules.cpp) and `recordVerifiedResult`; [territory rules](research/darien-crusades/campaign-territory-rules.md). Winnerless outcomes are ineligible. Tests: `crusades_rules`, `crusades_rules_store`. |
| Explicit fixture policy captures after an authored cumulative win threshold; reserved modern rules are unsupported. | **MODERN** test policy, never historical evidence. | `allowFixtureRules` defaults false and live server never opts in. Fixture rules do not compute neighbor pressure, fatigue, rank or reset. The fixture-only mutation restriction applies to result evaluation; trusted store `commit` can accept validated authored state and is not exposed as a client mutation API. |
| Result, policy decision, terminal status and verified-history projection commit atomically and immutably. | **MODERN** SQLite durability/idempotency policy, schema 8. | Started crash records stay retained; untyped completion cannot bypass verification. Replay file publication precedes the DB transaction, so file and DB are not one cross-resource transaction. An orphan artifact after interruption is possible. `crusades_result`, `crusades_store`, `crusades_history`. |

## UI terminology and activity

| Behavior | Classification and evidence | Engine behavior and reason |
| --- | --- | --- |
| Original views distinguish map, recon, overview, history and Crusades Room/war console. | **CONFIRMED** help/resource observations; **RECONSTRUCTED** entry callbacks in [UI evidence](research/darien-crusades/campaign-ui-evidence.md). | **MODERN** integrated screen provides territory search/list/details, allegiance, direct requests/FIFO search, invitations and history. It does not reproduce all houses, ranking, chat/gathering, persistent-unit controls or native window layouts. |
| Honor uses natural artwork, Terror brown, contested blood-red; selected territory has crossed swords. | **CONFIRMED** documentation: FAQ 12, 29. | Original owner artwork is used only when local IDs/names match the server. **MODERN** white selection square and blue/red/gold schematic fallback replace unsupported native presentation. Unknown state is gray. |
| Header dimensions and decoded image dimensions have differing axis labels. | **CONFIRMED** file observations; intended header-axis interpretation is **INFERRED**. | Renderer uses actual decoded image dimensions, matching the native image-size read boundary. No fabricated geographical adjacency. |
| Fatigue/required/support/battle fields represent victory points. | **CONFIRMED** recon templates. | Current short detail labels omit units on some rows; this audit identifies their units above. Missing values display Unknown rather than a fake zero. |
| Flames indicate territory player activity on full map/minimap. | **CONFIRMED** README-3 line 5 describes player counts in occupied territories; FAQ 29 describes fires in contested territory. | These version/scope statements do not define one exact algorithm. Flame thresholds, traffic measurement and final-service update cadence are **UNKNOWN**. |
| Current activity shows waiting accounts, offered rooms and active rooms. | **MODERN** service-derived counters. | `Server::campaignActivity`, `CrusadesScreen::details`; no flame/minimap rendering and no claim that rooms equal historical player traffic. Volatile counters do not mutate campaign pressure or results. |
| Campaign payload version 4/protocol 211, bounded requests/caches, authenticated privacy and reconnect selection. | **MODERN** protocol/UI policy. | [`net/crusades.h`](../src/net/crusades.h), [`MpClient`](../src/net/client.cpp), [`CrusadesScreen`](../src/client/crusadesscreen.cpp), [network contract](research/darien-crusades/campaign-network.md). Earlier milestone protocol numbers are introduction records, not current compatibility claims. |

## Campaign transitions and history

| Behavior | Classification and evidence | Engine behavior and reason |
| --- | --- | --- |
| Capturing territory weakens neighboring land into contested state. | **CONFIRMED** beta-era documentation: FAQ 4. Original graph, threshold, propagation order and resets are **UNKNOWN**. | Historical policy cannot advance fronts without those rules. Image contact and fixture win thresholds are insufficient. |
| War victory involves substantial control and changing victory locations, followed by reset/new war. | **CONFIRMED** documentation: FAQ 22, 30. Exact objective tables, thresholds, timing, initial reset state and ties are **UNKNOWN**. | No complete historical war-ending/reset/objective-selection implementation. Preserving records or accepting authored state is not an automatic historical war transition. |
| Original history presents a strategic movie of battle lines/conquest, plus greatest conquerors/Houses. | **CONFIRMED** documentation: HELP-146 history paragraph, HELP-147 FAQ 22, 31. Original recording/access protocol is **UNKNOWN**. | The current tactical replay archive is a separate **MODERN** feature; it does not recreate that movie or ranking system. |
| Territory history lists immutable verified terminal tactical results, newest first, with stable bounded pagination. | **MODERN** M11 contract. | [`CampaignStore::territoryHistory`](../src/server/crusades/store.cpp), [history guide](research/darien-crusades/campaign-history.md). Unplayed cancelled offers are absent; recorded no-winner/failure results remain labeled as such. |
| Enrolled campaign accounts can read history/replays, including nonparticipants; the separate battle-status API remains participant-only, including terminal battles. | **MODERN** access policy. | [`campaignReadResponse`](../src/server/crusades/network.cpp), server replay service; safe opaque IDs, 512 MiB artifact cap, 64 KiB chunks and a separate quota. No original archive-access parity claim. |
| Server validates retained artifact identity/regular-file handle/header; client verifies the full content digest before publishing its cache. | **MODERN** artifact policy. | [`ReplayFiles`](../src/server/crusades/replayfiles.cpp), MpClient download path. Availability metadata is not full body verification. Missing/corrupt files never remove immutable result/history. `crusades_replay_files`, `crusades_history_network`, client tests. |
| Watch Replay uses normal tactical loader/viewer, preserves the campaign session and territory, restores audio and returns on Esc. | **MODERN** integration policy. | Replay format 9 accepts protocol 210/211 because their tactical format is compatible; matching gameplay data/map is required. No live client is attached to playback and replay cannot apply territory credit. Actual-client `crusades_history_ui_network` verifies this boundary. |

## What remains unrecovered or deliberately different

The following prevents a claim of full historical Darien gameplay. None is
resolved by passing modern service tests:

1. Exact parcel adjacency, live initial owners, reset owners and complete
   territory-to-map/race-restriction assignments.
2. Fatigue/support/toughness generation, rank-weighted battle credit, momentum
   maintenance, numeric precision, equality/ties, capture ordering and neighbor
   propagation, including battle-free capture.
3. Orphan deferred entrenchment accounting and original report
   acceptance/retry/deduplication/dispute rules.
4. War-winning thresholds, changing objective tables, reset timing and the
   original strategic history movie/rank/House lifecycle.

Known documentation-level differences also remain explicit: free allegiance
switches; fixed two-player Aramon/Taros duels; map-based eligibility instead of
contested-owner gating; stale-revision no-credit aborts; numeric room/queue
activity instead of player-count flames; and tactical archives instead of the
strategic movie. These are bounded modern service decisions, not evidence that
the original worked that way. The historical policy identifier names the
intended reconstruction target, not a completed original server algorithm.

Future evidence must name its artifact/version and distinguish supplied fields
from computed authority. An original server table, source, or captured
authoritative response could narrow the unknowns; a template default, image,
forum recollection, local queue success or tactical score cannot. Public-server
hardening is the separate M13 task in the [plan](darien-crusades-plan.md).

## Validation for this audit

Independent rules, battle and presentation reviews covered all twelve M12
categories against current source and the primary files above. Original hashes
matched, Darien parsed to 313 parcels/zero recovered edge records, and PreInit
counts and local/GOG strategic-image equality were rechecked. Earlier native
emulation findings are cited as earlier evidence, not claimed as newly executed
M12 experiments. No proprietary assets or executable bytes are committed.

The 28 Python research tests pass:

```sh
python3 -m unittest discover -s tools/re -p 'crusades*_test.py'
```

Both complete Release and Debug builds pass. The fresh-build checks passed
**29 Release campaign/regression tests**, **32 Debug campaign/regression tests**
and **9 Release packaging-related tests**, with no failures:

```sh
cmake --build build --parallel 6
cmake --build build-dbg --parallel 6
ctest --test-dir build --output-on-failure -j 3 \
  -R '^(crusades_.*|replay|replay_file|retail_script|retail_visual|pathsearch|conn)$'
ctest --test-dir build-dbg --output-on-failure -j 3 \
  -R '^(crusades_.*|replay|replay_file|retail_script|retail_visual|pathsearch|conn)$'
ctest --test-dir build --output-on-failure -j 3 \
  -R '^(streaming|streaming_overload|streaming_native|menu_music|audio_device|nvml|windowsgpu|cartographer_document|cartographer_validation)$'
```

The campaign suites include real authenticated server issuance, matchmaking,
results, restart, history and protocol flows. Linux Debug additionally runs the
actual SDL client through allegiance/invitation, FIFO matchmaking and
history-to-replay-to-campaign return. Replay viewing checks exact tactical
completion, restored stereo audio, unchanged durable records and unavailable
recording behavior. These use the clean-room engine, not a retail session.
Retail-script/visual, path-search and connection regressions also pass.

This validation exposed a separate packaging defect in the baseline
[Fedora Linux CI run](https://github.com/pocketgeek/tak-engine/actions/runs/36799270341):
CMake appended C's `gcc_s_asneeded` implicit runtime when linking the static
SQLite archive into C++ products, defeating `-static-libgcc`. The
[Linux-only implicit-library correction](../cmake/SQLite.cmake) removes only
shared GCC runtime names and preserves the other implicit libraries. A control
reproduced the leak; isolated GCC and Clang mixed C/C++ exception-unwind checks
passed after correction. `readelf -d` now shows **only libc/libm** for `takclient`,
`takserver` and `cartographer` in both builds. The existing CI dependency gate
remains unchanged; Windows and macOS build rules are unaffected.

All changed Markdown links and heading references resolve, and
`git diff --check` passes. These checks establish current implementation and
documentation correctness within the audited scope. They cannot establish
missing original server rules or replace native Windows/macOS runtime testing.
