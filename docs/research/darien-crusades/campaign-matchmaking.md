# Milestone 10: modern territory matchmaking

The strategic screen now supports **Find opponent** for a selected eligible
territory and **Cancel search** for the authenticated player's current search.
This is an explicitly modern rendezvous policy around the recovered campaign
model. Historical matchmaking, campaign pressure arithmetic, ranks and capture
rules have not been recovered. The queue does not invent any of those rules.

## User flow

Open Darien Crusades from the authenticated multiplayer browser, select a
campaign, join Honor or Terror, then select a territory. The details pane shows
server-reported availability and aggregate waiting counts for each alliance.
Find opponent is enabled only when the authoritative board confirms that the
player and selected territory are eligible and the displayed campaign snapshot
has the same revision. Unknown availability stays unknown and disables search.

The server pairs the oldest available search with the opposite alliance for the
same campaign, territory and campaign revision. The older waiter hosts the
issued room; the other player receives an invitation and uses **Join invitation**.
The screen selects an incoming issued invitation by default when no battle is
already selected. Both players still receive the original battle invitation and
status workflow. Entering the room follows the existing map-verification and
ready/start sequence, with campaign map and rules locked by the server.

The original opponent-account **Request battle** remains available. It uses the
same authoritative issuance and participation checks; it is not a separate path
to campaign mutation. Search and cancellation wait for server confirmation
instead of changing local counters or claiming a match before issuance.

Every territory remains accessible through the map/list, including ones without
an eligible authored map. Matching does not allow arbitrary client map selection.
Open/active battle counts describe this server's issued rooms. They do not expose
another player's account, launch token, queue position or private battle history.

## Server policy and authority

A territory is eligible only with Crusades Balance installed and an effective
server-authored map that the server can load as a non-generated, non-mission
battle map with at least two starts. Effective assignment comes from the
persisted territory override when present, otherwise its definition. Map
package validation and issuance checks are shared with direct invitations.
Neither decorative map fires, native race nor an inferred neighbor graph
establishes eligibility.

Each account has at most one waiting search across campaigns. A search expires
after ten minutes. Repeating the identical request preserves both FIFO position
and the original expiry; polling, reconnecting or keepalive traffic does not
extend it. Changing a search moves it to the back. Disconnect, changed enrollment
or eligibility and server restart invalidate volatile waiting state. A waiting
entry does not itself create a durable battle.

Issuance revalidates both authenticated live accounts, opposite enrollment,
territory, current campaign revision and map. Busy accounts cannot acquire a
second active authoritative battle, including through another campaign or a
second connection. Issuance reserves both accounts in one store transaction,
so racing requests cannot create duplicate participation. Starting a battle
transactionally binds its unique room capability; an unplayed cancelled offer
does not create a durable started-room binding.
An account with multiple authenticated connections is unavailable for matching,
including while a failed connection awaits deferred cleanup. The replacement
session cannot inherit an old search or have its new search erased by that
cleanup.
Both search entries are removed only after successful battle issuance. A failed
room creation preserves otherwise eligible entries. A bounded retry considers
one eligible pair every five seconds, rotating by host sequence; normal
revalidation removes stale enrollment or unavailable players before retry.

Each issued room carries its one durable battle ID and unique launch/room
capability. The existing reference referee supplies results; clients cannot
submit a winning territory snapshot or authoritative result. The store permits
at most one verified completed result for a battle. Invalid or tampered lobby
configuration cannot override the issued context. Cancelling an unplayed room
records its terminal battle status and releases participation without changing
territory ownership, recon metrics or campaign revision.

M10 originally retained reservations after a server crash. Since
[M13](campaign-operations.md), startup cancels interrupted Issued and Started
battles with immutable system audit events and releases their reservations.
The old records remain; no running referee or verified outcome is fabricated.

Persisted historical rule policy remains unchanged. An actual tactical victory
can be recorded with its verified replay while unknown historical capture rules
remain blocked. A queue match is not permission to invent territory credit.

## Protocol and client cache

Network protocol 210 adds `CrusadesGetMatchmaking`, `CrusadesSearchBattle`,
`CrusadesCancelSearch` and `CrusadesMatchmakingStatus`. Version 3 strategic
payloads retain request IDs: nonzero IDs correlate requests; zero denotes
server updates. Requests contain the campaign and, for search, the territory.
The authenticated connection supplies the acting account.

The board contains campaign revision, volatile server generation, own
eligibility/search/expiry and per-territory eligibility plus Honor/Terror
waiting, offered and active counts. It contains no account list or launch
secrets. Strict decoding bounds payloads, counts, optional flags and complete
message consumption before publication. A malformed, mismatched, stale or
conflicting response preserves the previous validated cache.

`MpClient` exposes `getCampaignMatchmaking`, `searchCampaignBattle` and
`cancelCampaignSearch`. Cache state clears when changing subscriptions or
connections; late responses for an old subscription cannot restore its board.
The UI reads once when enrollment or campaign revision changes, and uses server
updates instead of requesting the board each render frame. Refresh remains an
explicit read action. Search does not add a tactical command, touch `World`, or
change simulation hashes. The queue and UI add no dynamic dependency.

## Evidence boundary

The recovered retail strategic flow enters a server-controlled territory area
and then a war console. Its GUI resources include people, gatherings, battles
and hosting controls; the historical host dialog supports two/four players and
persistent-unit choices. Those resources establish a broader historical
interface, not the retired server's exact matching algorithm. See
[campaign interface evidence](campaign-ui-evidence.md),
[native campaign flow](campaign-flow.md),
[territory policy boundaries](campaign-territory-rules.md) and
[network snapshots](campaign-network.md).

The current fixed two-player FIFO service and Aramon/Taros alliance duel setup
are modern implementation choices. They are labeled as such in the screen and
must not be described as recovered Boneyards rules.

## Focused verification

`crusades_ui_test` drives the actual screen's mouse handlers through an
SCRAM-authenticated `MpClient` over a test peer. It covers selected-territory
Find, authoritative queue confirmation, cancellation, unavailable/unenrolled
and mismatched-revision disabling, render-loop read suppression, direct invites
and default invitation selection. It also preserves unknown ownership/metrics
and checks parent render scale/clip state.

`tools/crusades_matchmaking_network_test.py` exercises the real `takserver`
with temporary synthetic accounts and campaign definitions. Its server-only
mode checks territory eligibility, opposite-alliance FIFO, no duplicate waiting
entry or expiry renewal, cancellation, disconnect/reconnect/restart behavior,
privacy, duplicate account connections and forged server-only messages. It
creates and cancels one actual
issued room, then completes a second through the reference referee and verifies
its unique result/replay linkage and unchanged historical territory state.

The optional `--client` mode runs actual Debug `takclient`/GameView SDL clicks
using a dummy video/software renderer and isolated Linux preferences. A second
account observes Find and Cancel while the UI process is still alive, so process
teardown cannot imitate cancellation. A second run pairs the UI as a guest and
joins its default-selected invitation. Screenshot artifacts remain local.

Loopback socket access, a suitable locally installed retail map and rebuilt
binaries are prerequisites. The initial managed sandbox prevented live checks;
after permissions were restored, the real-server and authenticated SDL gates
below all passed. No retail game launch was required.

### Validation recorded on 2026-09-30

The completion audit combines store-level proofs with real authenticated server
and SDL workflow checks:

| M10 requirement | Store/code evidence | Passing live evidence |
|---|---|---|
| One authoritative battle per campaign-affecting match | `campaignContext`, `startBattle` and `recordVerifiedResult` check the issued identity, roster, map/rules digests and unique room capability; battle/result tests reject wrong contexts and replayed launches | The matchmaking server test issues two distinct battles/rooms and completes one with its unique result/replay binding |
| At most one completed result per battle | The verified-result primary key, immutable triggers and terminal lifecycle reject duplicate/replaced results, including after reopening the store | Referee completion and a forged duplicate leave exactly one verified result |
| Invalid lobby configuration cannot mutate campaign state | Campaign room handlers reject map/rules/roster edits; store tests reject changed launch/report contexts and prove rollback | Battle and matchmaking network tests reject edits, verify authoritative ready/start settings and unchanged historical campaign revision |
| Cancelling an unplayed battle preserves territory state | Battle tests verify terminal cancellation, unchanged campaign revision and absent territory credit/result payload | Live cancellation preserves one initial campaign event, zero revision and no verified result |
| Eligible territories, opposite-alliance FIFO and duplicate prevention | Strict board codec, queue tests, indexed transactional account reservations, cross-campaign checks and a simultaneous two-handle issuance race pass | Real server map eligibility/counts/FIFO, duplicate-login unavailability and restored fresh-search lifecycle; actual SDL Find/Cancel/invitation entry |
| Retail pathfinding and deterministic tactical simulation remain intact | No simulation/pathfinding source changes; focused regressions and cross-compiler golden hashes pass | The real matchmaking referee completes a battle and its replay/digest checks pass |
| Cross-platform static dependencies and builds | Both native build configurations and MinGW compilation/static codec/queue linking pass; no dependency added | Linux real-server and actual SDL checks pass; full Windows/macOS runtime coverage is not claimed here |

All targets build in both Release (`build`) and Debug (`build-dbg`). Twenty
focused CTests pass in each build: the eleven campaign/presentation/store/wire
checks plus deterministic math, path search/geometry/blocking, visibility,
replay, fixed point, command flow and map transfer regressions. The queue has
65 checks, including retained failed-pair FIFO priority; the battle store covers
simultaneous issuance from separate handles and schema 6-to-7 rollback.

ASan/UBSan checks pass for the codec, battle store and queue with leak detection
disabled during the initial sandbox checks. This is not a leak-check
result. GCC and Clang builds at O0/O2/O3 agree on the simulation golden hash
`dcef618cd2e4d558`; AArch64 checks skip because target headers are unavailable.
MinGW compiles the server, client, codec and store changes and statically links
the standalone codec/queue tests. The server compile suppresses existing UTF-8
path deprecation and Windows main-argument warnings; no Windows runtime run is
claimed.

The final Release run passes 27 CTests: those twenty focused checks plus client
network, authentication, issued battles, results, protocol, matchmaking and
authenticated screen tests. The final Debug run passes the same twenty focused
checks and both actual-client SDL workflows. Those two workflows verify
allegiance/reconnect/direct invitations and observed Find/Cancel/default-selected
matched invitations against the real server. Reproduce the live checks with:

```sh
ctest --test-dir build --output-on-failure \
  -R '^crusades_(client_network|auth_network|battle_network|result_network|protocol_network|matchmaking_network|ui)$'
ctest --test-dir build-dbg --output-on-failure \
  -R '^crusades_(ui_network|matchmaking_ui_network)$'
```
