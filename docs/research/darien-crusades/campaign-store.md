# Transactional campaign store (Milestone 3)

**Milestone 3 complete — 2026-09-30.** All five acceptance criteria in the
[plan](../../darien-crusades-plan.md) are covered below.

This milestone persists the clean-room [campaign model](campaign-model.md).
It is separate from player credentials, the existing single-player campaign
progression, and tactical simulation state. It does not add a playable Crusades
mode, authenticate campaign allegiance, generate battles, award campaign points,
or implement a guessed historical capture formula.

The store and its schema are modern engine design. The recovered retail
client's report queue did not establish server-side durable acceptance,
deduplication or campaign mutation; this implementation must not be presented as
a reconstruction of that missing service. See the historical
[report boundary](report-transport.md) and [battle lifecycle](campaign-flow.md).

## Required persistence guarantees

A successful mutation must publish one complete campaign revision and its
corresponding audit entry atomically. When a mutation carries a battle result,
that result and its identity must be part of the same transaction. Failure or
interruption must leave the previous revision intact, with neither partial
ownership/metric changes nor a consumed result identity.

The caller supplies already-computed state. Persistence does not decide which
side won, how fatigue/support change, or whether a territory should change hands.
A stale caller must not overwrite a newer revision. Duplicate result identity
must not apply another update, even after reopening the database.

All seven optional recon metrics must preserve both value and absence across
restart. Missing is not zero, and known contested ownership is not unknown
ownership. Definitions and mutable state remain separate, with the persisted
state tied to its definition.

## API and state history

The public API is in `src/server/crusades/store.h`, namespace
`tak::srv::crusades`. `CampaignStore` opens a separate database; SQLite's
`:memory:` path uses the same schema and transaction implementation for tests.
One store object is used by one thread at a time. Separate connections may
access the same database.

- `create` persists a definition and caller-supplied initial state with an audit
  reason at revision zero.
- `load` returns `StoredCampaign`: the definition, current state and revision.
- `commit` accepts the expected current revision, the complete proposed next
  state, an audit reason and optionally a `BattleResult`. It returns the new
  revision after successful commit.
- `history` returns ordered `CampaignEvent` entries, including revision,
  reason, optional battle identity and full state snapshot.
- `battleResult` returns the opaque payload associated with a campaign/battle
  identity, or absence when no such completed result is stored.

A result payload is caller-owned evidence; storing it does not authenticate a
player's report or calculate a tactical-score-to-campaign conversion. The
append-only application API explains changes by retaining each full state and
its reason. It is an audit trail, not a cryptographic tamper-proof ledger or
proof of historical server behavior.

## Schema and transaction boundaries

The database identifies itself with application ID `0x54414b43` (`TAKC`) and
schema version 1 as introduced by Milestone 3. [Milestone 4](campaign-allegiance.md#schema-version-2-and-migration)
adds authenticated allegiance through an atomic version-1-to-2 migration;
[Milestone 5](campaign-battles.md#api-schema-and-transactions) adds battle
issuance in version 3. [Milestone 6](campaign-results.md) adds authoritative
results in schema version 4. [Milestone 7](campaign-territory-rules.md) adds
explicit policy bindings and rules decisions in current schema version 5,
migrating versions 1–4 atomically. The
three tables below describe the original persistence layer. An empty unclaimed
database may be initialized; unrelated or
unsupported-version databases are rejected. There is no migration from a
historical service database or from account credentials.

| Table | Stored information |
|---|---|
| `campaigns` | Campaign ID, canonical immutable definition text, current revision |
| `campaign_events` | Campaign/revision primary key, reason, optional battle ID, complete state snapshot |
| `battle_results` | Campaign/battle primary key, opaque payload and reference to the corresponding event revision |

The current state is the snapshot at the campaign's current revision, not a
second independently updated copy. Revision zero records the initial state.
Every successful mutation appends exactly one revision. Triggers reject updates
or deletions of existing events/results and modifications of campaign
identity/definition through ordinary SQL. Reopening validates the exact supported table/constraint/trigger definitions
in a coherent read transaction before changing persistent settings, and checks
again under the initialization write lock. Missing, extra or replaced schema
objects are rejected; version labels alone are insufficient.

`commit` acquires a write transaction with `BEGIN IMMEDIATE`, loads the current
revision, checks the caller's expected revision, and validates the proposed
complete state. It inserts the event snapshot, inserts any battle result, and
advances the campaign revision before committing. The unique campaign/battle
key prevents duplicate application; a duplicate insertion rolls back the whole
transaction, including the newly inserted event. Battle IDs are scoped per
campaign. Revision overflow is rejected.

Separate connections serialize writers through SQLite; a stale revision fails
rather than overwriting the winner's changes. The connection waits up to five
seconds for a busy database before reporting failure. There is no hidden replay
of a stale mutation. `load` obtains definition, revision and current snapshot in
one joined query; history and result reads use a read transaction to avoid
mixing revisions from concurrent commits.

History validates contiguous revisions starting at zero and ending at the
current revision. Each snapshot is validated against the persisted definition.
A caller can reconstruct any retained state directly and compare successive
snapshots to explain ownership and metric changes. Full snapshots favor simple
recovery/auditing over minimum storage; no history pruning or compaction policy
is introduced in this milestone.

## Exact values and input bounds

State snapshots use a versioned `TAKCS1` binary encoding, with explicit
little-endian integers, length-prefixed strings and optional-value markers.
Finite recon metrics store their IEEE-754 binary64 bits directly, preserving
signed zero, subnormal values and missing-versus-present values. SQLite REAL
conversion and locale-sensitive decimal formatting are not involved. This
encoding is an engine storage format, not a recovered retail wire format.

The reader rejects unsupported headers, truncated or trailing data, duplicate
territories, invalid owner/optional markers and state that fails model
validation. Definitions are stored in canonical version-1 text and reparsed
before creation and after reading. The model's 8 MiB definition bound remains
in force; stored snapshots, payloads and individual strings are capped at
16 MiB. Result payloads are BLOBs and may include zero bytes. Audit reasons and
battle IDs must be nonempty and contain no zero bytes.

## Static dependency and durability policy

The persistence library is separate from the dependency-free campaign model.
`tak-crusades-store` links `tak-crusades` and pinned static SQLite; no system
SQLite shared library or new SQLite runtime binary is required. The build uses
the official amalgamation with verified source hashes. The parent build
configuration documents the offline source-directory override.

The selected policy uses rollback journaling (`journal_mode=DELETE`),
`synchronous=EXTRA` and enabled foreign keys. EXTRA adds directory synchronization
after deleting the rollback journal. SQLite's atomic commit relies on the
platform's locking and storage flush behavior; this is not a claim that the
engine can overcome faulty storage. See SQLite's
[synchronous documentation](https://www.sqlite.org/pragma.html#pragma_synchronous)
and [atomic commit design](https://www.sqlite.org/atomiccommit.html).

## Validation and acceptance

The dedicated `crusades_store_test` uses only synthetic campaigns. It exercises
both the in-memory SQLite path and filesystem databases, complete reopen/state
comparison, history reconstruction, duplicate result rejection, independent
campaign result identities, stale revisions through separate connections,
exception rollback, schema rejection and Unicode database paths.

Its crash case starts a separate process, mutates state and writes a 4 MiB
result, then terminates inside the `beforeCommit` diagnostic hook. The child
checks the actual rollback-journal header before exiting, so the test covers
recovery after dirty database pages have spilled to disk. The reopened store
must retain the old state/revision/history, omit the interrupted result, and
accept a later retry using the same battle ID. This is process-interruption
coverage, not a physical power-loss experiment.

| Plan acceptance criterion | Verification |
|---|---|
| Restart preserves complete state | Reopen compares immutable definitions, owners, assigned maps and all optional metrics, including exact signed zero and extreme finite values |
| Interrupted transactions do not partially apply | Exception injection after writes and separate-process exit before commit both require unchanged state/history/result visibility |
| Duplicate result application is rejected | Same or changed payload under an existing campaign/battle ID fails, including after reopen |
| History explains ownership changes | Revision-zero and subsequent complete snapshots preserve every changed owner with a reason and optional battle identity |
| Tests reconstruct a known state | Ordered historical snapshots and the current snapshot reproduce the synthetic expected states |

```sh
cmake --build build --target crusades_store_test
ctest --test-dir build -R '^crusades_store$' --output-on-failure
```

Validation on 2026-09-30:

- Full Release and Debug builds succeeded; **129 Release** and **134 Debug**
  CTests passed. The store test performs **284 checks**.
- Standalone GCC and Clang builds passed with warnings treated as errors.
  The Clang run instrumented the model, store, tests and SQLite with
  AddressSanitizer/UndefinedBehaviorSanitizer and passed all 284 checks.
- MinGW cross-compiled the complete test and static SQLite for Windows with
  warnings treated as errors in engine/test code. Its only DLL imports are
  `KERNEL32.dll` and `msvcrt.dll`; Linux dependency inspection likewise found no
  SQLite shared-library dependency. The Windows executable was not run locally.
- Linux, Windows and macOS CI workflows now build and run both Crusades model
  and store tests, including the native child-process recovery test. Local
  cross-compilation does not substitute for those native CI executions.

No account, gameplay, network or simulation integration is required to exercise
this persistence library. SQLite's source archive is pinned in
[`cmake/SQLite.cmake`](../../../cmake/SQLite.cmake); build/offline instructions are
in the [development guide](../../development.md).
