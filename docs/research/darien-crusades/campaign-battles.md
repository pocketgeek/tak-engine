# Authoritative battle issuance (Milestone 5)

This milestone creates a persisted battle definition before the server creates
its associated game room. It builds on [authenticated allegiance](campaign-allegiance.md)
and the [transactional store](campaign-store.md). It does not award points,
apply territory ownership changes, accept client-declared wins, or implement
Milestone 6's verified-result processing.

## Evidence boundary and modern match policy

Retail's [battle-entry trace](campaign-flow.md) establishes server-supplied
launch settings, allegiance participation and mandatory Crusades tuning. It
does not recover an authoritative Darien adjacency graph, complete map
assignments, or capture arithmetic. None is invented here.

The initial engine policy is an explicitly modern two-player duel: two
successfully authenticated accounts with opposite campaign allegiances, no AI
or spectators, Crusades Balance enabled, and a server-installed skirmish map
with at least two start positions. The selected territory must have an authored
or runtime-assigned map; a runtime assignment takes precedence. Generated-map
identifiers and authored mission scenarios are not accepted by this initial
server path.

The server fixes the match options, seed, slot teams and factions before issuing
the battle. The initial preset uses Aramon for Honor and Taros for Terror;
these are modern preset choices, not assertions that retail forbade other
factions. The invited opponent joins explicitly. Before launch, each player may
change only their own ready flag. Clients cannot replace the map, upload a new
package, change slots/options, kick participants, or add spectators.

## Persisted identity and immutable context

`IssuedBattle` records a generated ID, campaign ID/revision, territory, map
identifier/content digest, rules digest, sorted participant accounts with their
alliance and allegiance revision, issuance/launch-deadline timestamps, lifecycle
status, launch capability and optional room binding.

IDs use the reserved `issued:` prefix with 32 random bytes rendered as hex;
launch capabilities independently use 32 random bytes. Generation uses the
engine's existing OS-backed crypto random-byte implementation. Database unique
constraints reject collisions. Clients neither choose these values nor receive
the launch capability.

The live server derives the map digest from its verified map package. The rules
digest covers the named modern policy, seed, game options and complete slot
rules. Before starting, it recomputes context from the actual room and
successfully authenticated connections. Matching a map name alone is
insufficient: altered map content, options, factions, teams or roster fail
validation.

Room binding combines a random server-session identity with the room number.
It is stored uniquely and cannot be reassigned, even after the battle ends.
A recycled numeric room ID after server restart therefore does not inherit
campaign authorization. M5 does not restore interrupted battles after a server
restart: old Started records remain in storage but cannot attach to a new
server-session room. They are not automatically cancelled because ownership of
shared-database server sessions is not yet modeled. Unstarted tickets still
expire at their launch deadline. An invited opponent who disconnects before
joining leaves the invitation pending until that deadline; host departure or a
seated participant leaving the prestart room cancels it immediately.

## Lifecycle and authorization

| State | Permitted transition |
|---|---|
| Issued | Start once before the launch deadline; cancel; expire |
| Started | Complete or cancel |
| Cancelled, Completed, Expired | Terminal; cannot relaunch or report |

The live server currently supplies a ten-minute **launch** deadline. A battle
that started in time may continue past that deadline; this is not a match-duration
limit. `expireBattles` expires only unstarted tickets. Explicitly leaving a
campaign room cancels its campaign eligibility, as does a seated participant
disconnecting before launch. A temporary running-game disconnect instead uses
the existing grace period and reconnect flow: the original authenticated
account must supply its resume token, and the campaign binding/context must
still validate. Live rooms that end are cancelled rather than credited;
scored outcome-specific treatment belongs to M6.

`startBattle` checks the one-use launch capability, unique room binding, exact
context, current campaign revision and both current allegiance revisions.
`authorizeBattleReport` requires Started status, the persisted room binding,
matching context and those same current revisions. Switching allegiance away
and back still invalidates a ticket because its revision changed. These strict
freshness checks are modern policy, not a reconstructed orphan-result rule.

`completeBattle` performs the same authorization and records a terminal marker
only. It does not store a scored result, advance the campaign state revision,
or calculate credit. There is no network request for claiming that a battle was
won. The generic store `commit` rejects result IDs in the reserved `issued:`
namespace, preventing that older API from bypassing this future verified-result
boundary.

## API, schema and transactions

The trusted API in `store.h` provides `issueBattle`, `battle`, `startBattle`,
`authorizeBattleReport`, `completeBattle`, `cancelBattle` and `expireBattles`.
Callers supply server-generated context; the persistence library itself is not
an authentication or map-catalog service. Digests and room tokens are bounded
opaque strings in the store; the live server supplies actual cryptographic
digests and session-bound tokens.

Schema version 3 adds immutable issued-battle definitions, append-only lifecycle
events and immutable unique room bindings. Context snapshots are versioned
binary records containing the map/rules and account/allegiance references.
Issuance, room binding and every lifecycle transition are separate atomic
transactions. Failure rolls back all rows for that operation, leaving an
unconsumed ticket usable after a failed start.

Exact supported version-1 and version-2 databases migrate transactionally to
version 3. Existing campaign definitions, snapshots, audit events, binary
results and duplicate-result protection remain intact; version-2 allegiance
records and history are preserved. A failed migration retains the previous
version and schema. No battle is inferred from an existing ordinary match.

## Live server boundary

`CrusadesIssueBattle` names a campaign, territory and opponent account. The
server derives the caller from its authenticated session, confirms the opponent
is independently authenticated and available, selects the territory's map and
constructs the match rules. Its reply identifies the issued battle and room;
it contains no launch secret. Anonymous sessions and disabled campaign service
cannot issue tickets.

Joining a reserved room requires the invited account. Starting requires both
participants, their verified map packages and a successfully constructed server
referee. The launch capability is consumed before `GameStarting` is sent.
Ordinary rooms have no internal battle ID, launch capability or bound room token,
so matching players/map or forging a visible ID does not make them eligible.
Detailed storage failures stay in server logs, with generic rejection replies
to peers.

Protocol **206** adds battle issuance and invitation messages. Update clients
and servers together. The store and networking reuse the existing static crypto
code and OS random source; no new library must be shipped with the application.

This is server/API support, not a new player-facing campaign browser or
historical Boneyards-compatible protocol. Normal games and tactical hashes
remain separate from campaign persistence.

## Validation status

Both full builds passed. The complete suites passed **133/133 Release** and
**138/138 Debug** tests, including real-server battle issuance. The final
reconnect assertions were also rerun against both builds successfully.

The dedicated `crusades_battle_test` passed **166 checks** in a standalone GCC
build with `-Wall -Wextra -Werror`. It covers unique identities, malformed issue
contexts, missing maps, wrong roster/map/rules/room bindings, one-use launch,
terminal states, launch-only expiry, stale campaign and allegiance revisions,
reserved-result bypass rejection, rollback/restart and v1/v2 migrations.

| M5 acceptance criterion | Coverage |
|---|---|
| Unique battle IDs | Generated identities/capabilities checked across multiple issues; database uniqueness enforced |
| No expired/cancelled/completed reuse | Start/report rejection for terminal tickets and persisted unique room bindings |
| Wrong-map matches cannot report | Both map identifier and content-digest substitution rejected |
| Wrong participants cannot report | Exact canonical roster and pinned current allegiance revisions required |
| Ordinary matches cannot mutate campaign state | No public credit/result request, no ordinary-room binding, and generic result API rejects issued IDs |

The actual authenticated server/room test covers reserved joins, immutable map
and rules, readiness and launch, stolen-token rejection, successful same-account
reconnect, cancellation, restart isolation and ordinary-room isolation. It uses
real SCRAM sessions and a server-installed map, with synthetic campaign data.

The 166 storage checks also passed ASan/UBSan with leak detection and instrumented
SQLite. MinGW compiled and linked the storage test with static SQLite and the
existing bcrypt system API; Windows server syntax checking passed. Windows and
macOS runtime behavior was not exercised locally; all three CI workflows now
include the battle store test. GCC/Clang determinism hashes agree, and four local
multiplayer smoke runs (two standard, two Crusades) reached tick 1800 with the
same hash. These smoke runs validate the ordinary multiplayer path, not campaign
result scoring.

```sh
cmake --build build --target crusades_battle_test
ctest --test-dir build -R '^crusades_battle$' --output-on-failure
```
