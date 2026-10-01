# Authenticated campaign allegiance (Milestone 4)

**Milestone 4 complete — 2026-09-30.** The four acceptance criteria in the
[plan](../../darien-crusades-plan.md) are verified below.

This milestone connects existing authenticated server accounts to per-campaign
Honor/Terror membership. It extends the [transactional store](campaign-store.md)
and adds server request handling. It does not issue campaign-valid battles,
award territory credit, change tactical state, or add a campaign selection UI.

## Historical evidence and explicit modern policy

The shipped FAQ describes switching allegiance at a one-rank cost, subject to
house compatibility. It does not recover rank-floor behavior or a switch
cooldown. It also describes normal race/allegiance associations **with
exceptions**; the native client consumes territory-supplied `map_races_allowed`
rather than enforcing an unconditional faction list. See the
[shipped rules evidence](campaign-rules-evidence.md) and
[native race restriction trace](battle-contract.md#race-restrictions).

The current engine policy is deliberately modern: an authenticated account may
join Honor or Terror and subsequently switch immediately without a rank charge.
There are no permanent-allegiance, house, cooldown, global-allegiance or fixed
race restrictions. This is not represented as historical parity. A repeated
request for the same allegiance is rejected rather than adding a duplicate
audit event. Separate campaigns may hold different allegiances for one account.
Historical rank/house enforcement requires further model and gameplay work;
this milestone does not invent substitutes.

## Authentication boundary

The live server derives the account exclusively from its successfully
authenticated connection state, then applies the existing username folding.
The request contains no target account or timestamp. A display name in Hello,
a pending authentication challenge, or an unknown account name is not an
authenticated identity. Both querying and changing allegiance require successful
authentication, including when campaign support is disabled.

Only the existing SCRAM sign-in or successful persisted account-registration
path populates the authenticated connection account. `--no-auth` cannot enable
the campaign service. An authenticated session can act only on its own campaign
membership; there is no client-supplied account selector or administrator
impersonation field.

`CampaignStore` is a trusted persistence API, not an authentication service. It
validates canonical account-ID syntax but does not load account credentials or
claim that any arbitrary string passed by trusted C++ code has authenticated.
The live server enforces account existence/identity before invoking it.

The campaign database contains references to accounts, never their passwords,
SCRAM salts, verifiers, keys or authentication transcripts. Startup rejects using
the account file as the campaign database or a reserved SQLite sidecar,
including resolved aliases and case-insensitive Windows/macOS names. Account
credentials remain in their existing separate account store.

## Storage API and audit

Canonical account IDs are lowercase ASCII, 3–20 characters, beginning with a
letter or digit, with letters, digits, underscore, dash and dot thereafter.
The API uses `Alliance::Honor` and `Alliance::Terror`; contested or unknown
territory ownership is not a valid player allegiance.

- `allegiance(campaignId, accountId)` returns the current `Allegiance`, or absence
  for an unenrolled account in an existing campaign.
- `setAllegiance(campaignId, accountId, alliance, expectedRevision, unixTime)`
  atomically joins or switches. Initial enrollment requires revision `-1` and
  creates revision zero. Later changes must match the current participant
  revision and increment it exactly once.
- `allegianceHistory` returns every successful enrollment/change in revision
  order. Each record contains account ID, side, original join time, change time
  and revision. Original join time remains unchanged after switching.

Timestamps are server-supplied nonnegative Unix seconds. A change cannot precede
its previous change time, but the same timestamp is allowed; this is monotonic
record validation, not a switch cooldown. Invalid side, account syntax, campaign,
revision, timestamp or same-side mutation leaves membership and audit unchanged.
Revisions are independent per campaign/account and do not advance the campaign's
tactical snapshot revision or alter territory ownership/metrics.

A write transaction contains the participant identity/current-revision row and
its immutable allegiance event. Reads use one coherent transaction. Stale
callers cannot overwrite a newer change, and failed writes roll back the
participant and audit together.

## Schema version 2 and migration

Milestone 4 introduced version 2, retaining the complete version-1
campaign/event/result schema and adding the tables below.
[Milestone 5](campaign-battles.md#api-schema-and-transactions) subsequently
adds battle issuance through migration to version 3.
[Milestone 6](campaign-results.md) subsequently adds authoritative results in
schema version 4. [Milestone 7](campaign-territory-rules.md) adds policy and
decision records in schema version 5. [Milestone 8](campaign-network.md) adds an
indexed battle-participant projection in schema version 6.
[Milestone 10](campaign-matchmaking.md) adds the account-wide reservation index
in schema version 7. [Milestone 11](campaign-history.md) adds the immutable
verified territory-history projection introduced in schema version 8.
Opening supported earlier versions upgrades atomically and backfills history
from existing verified results without generating new results or rules decisions.

| Table | Fields |
|---|---|
| `campaign_participants` | Campaign ID, canonical account ID, current participant revision |
| `allegiance_events` | Campaign/account/revision key, Honor/Terror side, original join and change timestamps |

Triggers prevent allegiance-event updates/deletions and participant identity
rewrites. Opening a supported version-1 database validates its exact old schema,
then, in the M4 implementation, created the new tables/triggers and published
version 2 in one transaction. Current opening includes the later tables and
publishes version 9 in the same atomic migration.
It creates no inferred participants and does not rewrite old definitions,
snapshots, history, binary result payloads or duplicate-result identities.
Interrupted migration rolls back the schema additions and version together.
Unknown versions and malformed old schemas are rejected rather than upgraded.

## Server setup and protocol scope

`--crusades-db <file>` enables the persistent service on an authenticated
`takserver`. `--crusades-definition <file>` imports a new independently authored
campaign definition or verifies it against the saved definition; it requires the
database option. No guessed Darien adjacency/map dataset is bundled.

```sh
build/takserver --data /path/to/retail-install --accounts accounts.conf \
  --crusades-db campaign.sqlite --crusades-definition authored.campaign
```

The definition uses the [Milestone 2 format](campaign-model.md). On restart the
definition flag may be omitted; supplying it again verifies equality without
resetting the campaign. The campaign service is disabled by default.
Protocol **205** adds these messages; update clients and servers together.

`CrusadesGetAllegiance` carries a campaign ID. `CrusadesSetAllegiance` additionally
carries the expected participant revision and side; `UINT64_MAX` represents the
initial revision `-1`. Requests have bounded lengths and reject trailing data.
Replies distinguish success, malformed request, unauthorized connection,
disabled service and rejected mutation. Successful replies carry side/revision
and server-recorded timestamps. Persistence failures do not expose local paths
or schema details to peers.

These messages establish identity-bound membership only. Ordinary skirmish
matches still cannot claim campaign credit; battle issuance and authoritative
results are later milestones.

## Validation status

`crusades_allegiance_test` uses synthetic accounts and an independently frozen
version-1 SQL/snapshot fixture. Its **160 checks** passed in a standalone GCC
build with `-Wall -Wextra -Werror`. Coverage includes noncanonical IDs, unknown
campaigns, invalid sides/revisions/times, immediate switches, per-campaign and
per-account isolation, rollback, restart, immutable audit, and preserved v1
state/history/binary results/deduplication through migration. A fault injected
after migration writes confirms that version 1 and its data survive rollback.
Future-version and damaged-v1 schemas are rejected unchanged.

The live `crusades_auth_network_test.py` starts real servers on loopback, performs
SCRAM registration/login, checks server signatures, and proves that Hello names,
pending registration and failed proofs cannot grant campaign access. It tests
truncated packets, injected account fields, invalid sides/revisions/campaigns,
case-folded account identity, separate players, process restart, disabled
service, unsafe startup paths and credential exclusion from database bytes.
It uses only temporary synthetic accounts and is registered when retail test
data and Python are available.

Validation on 2026-09-30:

- Full Release and Debug rebuilds passed; **131 Release** and **136 Debug**
  CTests passed, including the real-server test in both builds.
- All **160 allegiance/migration checks** also passed Clang ASan/UBSan, including
  instrumented SQLite. MinGW compiled the storage tests with warnings treated
  as errors and syntax-checked the Windows server/request implementation. Native
  Windows/macOS execution remains covered by the configured CI jobs, not these
  local cross-compilation results.
- GCC/Clang determinism variants agreed on `dcef618cd2e4d558`; unavailable ARM
  target-header legs were skipped. Four short multiplayer runs (two standard,
  two Crusades balance) reached tick 1800 without errors and agreed on
  `0e0f5c8f6d8c82fd`. These are compatibility smoke tests, not combat stress tests.
- Linux server dependency inspection found no SQLite shared library. SQLite
  remains static; macOS path comparison uses the system CoreFoundation framework.

| Acceptance criterion | Evidence |
|---|---|
| Anonymous clients cannot mutate campaign state | Live pre-Hello, pre-proof and failed-proof requests reject; open-server campaign startup rejects |
| Allegiance uses authenticated identity | Requests have no account field; two-account and differently cased login tests preserve identity isolation |
| Invalid changes reject | Strict packet validation, store validation and independent expected revisions; rollback keeps audit and current state together |
| Campaign state contains no credentials | Account-only schema and live credential-byte exclusion check; separate file/sidecar paths enforced |

```sh
cmake --build build --target crusades_allegiance_test
ctest --test-dir build -R '^crusades_allegiance$' --output-on-failure
```

## Milestone 13 operations

The current store schema is **9**, adding immutable administrative events.
[Public-server operations](campaign-operations.md) describes transactional
migrations from versions 1–8, audited start/reset/cancellation, interrupted-battle
recovery, offline inspection and tested backup restoration. These are modern
service policies; historical territory rules remain unchanged.
