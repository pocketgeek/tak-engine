# Milestone 6: authoritative battle results

Milestone 6 adds server-generated results to the modern authenticated duel path
from [Milestone 5](campaign-battles.md). It does not award territory points,
change ownership, or implement the unknown historical support/fatigue formula.
Implemented and validated 2026-09-30.

## Authority and bindings

The live server obtains the winner, defeats, statistics and final state hash
from its mandatory referee. Accounts come from authenticated room participants;
map contents, gameplay options and roster remain bound to the issued battle.
There is no client result-upload or winner-claim packet. Normal skirmish results
cannot enter this path. The database checks exact battle/room/context identity
and a Started lifecycle record before accepting a result.

`VerifiedMatchResult` stores an outcome, winner account, final tick, final state
hash, gameplay-data fingerprint, engine build, replay identity and SHA256, plus
each participant's faction, team, kills, losses, score, built count, current unit
count and defeated flag. The two participant rows are sorted by canonical account
ID and must match the issued roster. Eligible results require one living winner,
a defeated opponent and distinct teams. Counts cannot be negative; score may be.

The store is a trusted persistence API, not a replay simulator or authentication
service. It cannot prove that a caller-supplied hash is the referee's true hash.
That authority resides in `takserver`, which creates this object directly from
the referee and does not expose its construction to clients. Hash fields retain
all 64 bits; replay digests require 64 lowercase hexadecimal characters.

## Explicit modern outcome policy

| Outcome | Terminal status | Winner / future credit eligibility |
| --- | --- | --- |
| Victory | Completed | Exactly one winner; positive tick; durable replay required |
| Resignation | Completed | Same requirements; explicit loser resignation |
| Disconnect | Cancelled | No winner or credit |
| Timeout | Cancelled | No winner or credit |
| Server abort | Cancelled | No winner or credit |
| Draw | Cancelled | No winner or credit |
| Referee failure | Cancelled | No winner or credit |
| Desync | Cancelled | No winner or credit |
| Invalid client | Cancelled | No winner or credit |
| Participant substitution | Cancelled | No winner or credit; substitution is not supported |

These are conservative modern policies. The historical service's precise draw,
disconnect, timeout, invalid-report and credit rules remain unproven. Recording a
Draw enum does not establish that the original service accepted draws. Temporary
running disconnects retain the existing reconnect grace period; they do not
become explicit resignations. Campaign rooms disable manual pause; bounded
disconnect/reconnect pause behavior remains separate.

Ordinary skirmish `Leave` stops the departing player's units and does not itself
force defeat. Campaign resignation instead uses the server-sequenced
`CampaignForfeit` event (kind 3), applied through the shared simulation event
handler in the referee, clients and replay playback. It calls `forceDefeat`, so
the referee can derive the winner instead of accepting a client winner claim.
Protocol version 207 identifies this added event. Ordinary `Leave` behavior is
unchanged.

Victory/resignation also require the campaign revision and both allegiance
revisions to remain unchanged since issuance. Switching away and back still
invalidates eligibility. A stale battle may record a no-credit abort, preserving
the audit without inventing campaign changes. All outcomes leave campaign state
and its revision untouched under the default historical policy.
[Milestone 7](campaign-territory-rules.md) adds audited policy decisions and
separate, explicitly enabled synthetic fixture state updates.

## Replay and transaction behavior

The server freezes the terminal referee/replay snapshot. Eligible outcomes wait
two seconds for already-in-flight integrity reports before persistence. Detected
integrity faults downgrade eligibility. The server hashes the exact replay bytes,
writes a temporary file, flushes it, publishes an immutable content-addressed
artifact, and verifies its digest before storing an eligible result. On POSIX it
also syncs the containing directory and its ancestors, including when retrying
an existing verified artifact. Windows uses write-through publication.
Replay publication failure becomes a no-credit abort. Abort records can omit the
replay, but cannot contain only one member of the replay identity/digest pair.
Campaign replays are saved under `crusades-replays` beside the campaign database,
or in the server's configured `--replaydir` directory.

M6 introduced schema version 4 with an immutable `verified_match_results` table. The result,
its metadata and terminal lifecycle event commit in one SQLite transaction.
Replay identities are unique across recorded results. A second report, including
one with a different final hash, cannot replace the first. The legacy untyped
`completeBattle` entry point now always fails closed. Generic opaque campaign
results still cannot use the reserved issued-battle ID prefix.

Filesystem publication and SQLite commit are ordered, not a single cross-resource
transaction: a crash may leave an unreferenced replay artifact. The live server
requires confirmed replay publication before committing an eligible result. If
database persistence fails, the server retains its frozen result and retries.
A database I/O failure alone does not downgrade an otherwise verified victory;
only failed eligibility or integrity
checks remove credit eligibility. Hard process death still
does not restore a running referee. Since [M13](campaign-operations.md), startup
cancels interrupted battles with a system audit while retaining their history;
no verified result or territory credit is invented. Old M5 terminal records migrate
without fabricated verified results.

M6 migrated versions 1, 2 and 3 transactionally to version 4, preserving definitions,
state/history, opaque legacy results, existing allegiances and issued battles.
Failed migration leaves the previous schema and data intact. M7 subsequently
adds policy/decision tables in schema version 5. [M8](campaign-network.md) adds
an indexed participant projection in schema version 6.
[M10](campaign-matchmaking.md) adds the account-wide reservation index in
schema version 7. [M11](campaign-history.md) adds the immutable verified
territory-history projection introduced in schema version 8. Migration
backfills that projection from existing verified results; it does not manufacture
results or rules decisions. Supported earlier versions upgrade atomically and
old results are not retroactively scored.

## Validation

`tools/crusades_result_test.cpp` passes 135 standalone checks covering malformed
results, wrong room/map/rules/roster, unchanged state on rejection, all ten
outcomes, stale revisions, exact hash persistence, replay collision, duplicate
reports, immutable records, transaction rollback/restart and migrations from
versions 1–3. The prior allegiance and issuance regressions are updated for the
new schema and fail-closed untyped completion. The result, allegiance, battle and baseline store tests also pass ASan/UBSan
with leak detection and instrumented SQLite (135/160/167/284 checks). The result
test compiles and links under MinGW with strict warnings; that is not a Windows
runtime test. The final server also passed Windows syntax checking.

Both full builds completed. The full sweeps covered 135 Release and 140 Debug
tests; their sole remaining failure was an overly strict duplicate-event test
assertion. Repeating a forfeit also repeats the ordinary Stop operation, which
may reset hashed mission state. The corrected test verifies persistent defeat,
unchanged unit counts and matching direct/wire simulation hashes; both failed-test
reruns passed, completing validation of all tests.

Real authenticated server tests passed for victory, resignation, desync, invalid
client, abandonment, forged win packets and replay publication failure. They
check atomic terminal status, unchanged territory state, and successful replay
digest/header/bundle-count/final-hash association. Four standard/Crusades
multiplayer smoke runs reached tick 1800 with the unchanged hash
`0e0f5c8f6d8c82fd`. GCC/Clang determinism checks agree; ARM cross-checks were
skipped because the toolchain headers are unavailable locally.

Source: `src/server/crusades/store.{h,cpp}`, `src/server/server.cpp`.
Historical boundaries: [campaign rules evidence](campaign-rules-evidence.md),
[battle contract](battle-contract.md), [report transport](report-transport.md).

## Milestone 13 operations

The current store schema is **9**, adding immutable administrative events.
[Public-server operations](campaign-operations.md) describes transactional
migrations from versions 1–8, audited start/reset/cancellation, interrupted-battle
recovery, offline inspection and tested backup restoration. These are modern
service policies; historical territory rules remain unchanged.
