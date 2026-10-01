# Milestone 13: campaign operations and public-server hardening

This is the modern TAK-Engine service contract, not recovered Boneyards policy.
It keeps the historical territory evaluator unchanged: missing original capture
and war-winning rules never become guessed state updates. Protocol **211** and
campaign payload **4** remain compatible; database schema is now **9**.

## Service and administrative authority

Enable campaigns with authenticated accounts, a separate campaign database and
your authored definitions. `--no-auth` is incompatible with Crusades. Existing
SCRAM authentication binds account identity; no request can nominate an acting
account, upload a winner or alter campaign state directly. Administrative access
is local OS access to the database directory and `crusades_admin`, not a network
privilege or client account flag.

```sh
takserver --data /srv/tak/game --accounts /srv/tak/state/accounts.conf \
  --crusades-db /srv/tak/state/campaign.sqlite \
  --crusades-definition /srv/tak/state/darien.campaign \
  --replaydir /srv/tak/state/replays
```

Use a private local filesystem directory for state. The server and every admin
command take the same nonblocking, exclusive process lease. **Stop takserver
before every admin command**, including inspection and backup. A second service
or an admin process refuses to open a database owned by a running service. The
kernel releases the lease when a process crashes; the `.service-lock` file stays
on disk and must not be deleted to bypass a busy service. Canonical path aliases
share the lease; hard-linked databases and unsafe lock-file aliases are rejected.
Do not rename or replace a database or lock file while its owner is running.
Network filesystems and separate hosts mounting one database are unsupported.

The packaged command is `crusades_admin` (`crusades_admin.exe` on Windows).
Windows installers/archives include it without a menu shortcut. macOS archives
include the standalone command; the app bundle also carries it under
`Contents/MacOS`. Linux packages install it in `bin`; native CMake installation
also installs it on macOS. No new dynamic engine dependency is introduced.

Commands print bounded JSON lines, escape controls and omit launch/resume/room
capability tokens. Actor names are explicit canonical lowercase IDs, 3–20
characters. **`--actor` is audit attribution, not authentication**: local OS
access grants administrative authority. State mutations require an actor and
nonblank reason of at most 512 bytes. Reset/cancel also require explicit expected
revision/state; start implicitly expects a new campaign at revision -1.

## Inspect and check health

```sh
crusades_admin /srv/tak/state/campaign.sqlite health
crusades_admin /srv/tak/state/campaign.sqlite health --limit 100000
crusades_admin /srv/tak/state/campaign.sqlite campaigns --limit 16
crusades_admin /srv/tak/state/campaign.sqlite campaign darien --limit 32
crusades_admin /srv/tak/state/campaign.sqlite events darien --after -1 --limit 32
crusades_admin /srv/tak/state/campaign.sqlite audit darien --after 0 --limit 32
crusades_admin /srv/tak/state/campaign.sqlite battle BATTLE_ID
```

`status` is an alias for `health`. Reports contain schema/version, campaign,
event, membership, battle/result/admin counts and battle phases. Health checks
the exact supported schema/immutable triggers, SQLite integrity/foreign keys,
and bounded decoded state, history, lifecycle, projection, rules and audit
bindings in a coherent snapshot. The default semantic budget is 4,096 rows,
maximum 100,000. An incomplete semantic scan reports `complete=false`, is not
healthy and returns a failing exit status; it does not certify an unchecked
archive. SQLite structural/aggregate checks still traverse the full snapshot.
Replay files and account credentials require separate checks; database health
does not certify those external resources.

List pages are 1–64 rows and return an exclusive next cursor. Campaign inspection
pages territory IDs; event inspection pages revisions; audit inspection pages
sequence IDs. Display strings are clipped at 1,024 UTF-8 bytes. Missing databases
fail without creating one except for the explicit `start` command. Invalid
arguments are rejected before opening or migrating the store.

## Start, reset and cancel

```sh
crusades_admin /srv/tak/state/campaign.sqlite start /srv/tak/state/darien.campaign \
  --actor operator --reason "Start authored Darien campaign"
crusades_admin /srv/tak/state/campaign.sqlite cancel-battle BATTLE_ID \
  --expected-revision 0 --expected-state started \
  --actor operator --reason "Abandoned battle after service incident"
crusades_admin /srv/tak/state/campaign.sqlite reset darien \
  --expected-revision 0 --actor operator --reason "Begin a new authored round"
```

`start` requires a new campaign ID and chooses `historical-darien-v1`; definitions
must use the [engine-owned format](campaign-model.md). Initial owner/recon values
stay unknown. Definitions must also pass the complete network encoder, including
UTF-8, identifier/text/count limits and space for activity fields, before a new
database is created. Startup validates every stored campaign before admission
or recovery. Existing IDs cannot be overwritten. Initial server-definition
bootstrap is also audited with system actor `takserver`.

`reset` appends one revision containing unknown initial state. It preserves the
definition, authored map preferences, memberships, all old events, verified
results and archive identities. It clears runtime state values such as assigned
map overrides; it does not invent original starting owners or reset historical
metrics to zero. All Issued/Started battles must first be explicitly cancelled,
including expired offers not yet swept. Revision conflicts refuse the operation.
The trusted store API can additionally accept validated authored state; the CLI
deliberately offers only the unambiguous unknown-state reset.

`cancel-battle` expects the **current campaign revision**, not its issuance
revision, and the exact current `issued` or `started` phase. It records a terminal
Cancelled transition without manufacturing a verified result or credit. Terminal
repeats, completed results, wrong phase/revision and invalid timestamps refuse
without partial writes. Original launch/room identities remain permanently
reserved, so cancellation cannot revive old capabilities.

Start/reset/cancel and automatic recovery append immutable `admin_events` in the
same transaction as their changes. Audit rows retain actor, reason, target,
expected/before/after revision, time and any before/after battle phase. An actor
does not become a campaign member merely by administering it. Inspection and
backup do not append state mutations; preserve their JSON output in operator logs
when an access/backup record is needed.

## Backup and tested restore procedure

Stop the server cleanly, then create a **new** backup path:

```sh
crusades_admin /srv/tak/state/campaign.sqlite backup /srv/tak/backups/campaign-2026-09-30.sqlite
crusades_admin /srv/tak/backups/campaign-2026-09-30.sqlite health --limit 100000
```

Opening a supported older database first commits its automatic schema migration;
this can change the source even if a later copy fails. For a current-schema
source, backup does not change application rows or source bytes.
Backup uses SQLite's backup API with durability settings, checks structural
integrity/foreign keys, closes and independently reopens the copy for exact
schema and bounded semantic validation. Existing destinations, symlinks and
reserved source/target companion paths are refused. Failure removes only the
newly created destination; it does not undo an already committed source migration.
For archives exceeding
100,000 semantic rows, structural checks still pass but the copy's semantic
inspection can be incomplete; a successful copy alone does not certify every
historical payload. Do not make a live SQLite backup by copying only its main
file while a service or other SQLite writer is running.

Keep the following together in the same stopped-service backup set:

- The campaign SQLite backup, account credential file and authored definitions.
- The configured replay directory, needed to retain Watch Replay availability.
- Matching gameplay data/build information needed to execute retained recordings.

The SQLite command does **not** copy accounts or replay artifacts. Treat the
account file as private credential material. Missing replay files leave verified
results/history intact but recordings unavailable. Keep an operator record of
the backup date, build/schema version and paths; verify the backup before use.

Restore into a **new directory/path**, leaving the original service state intact:

1. Stop the service. Copy the validated backup to the new campaign path and restore
   matching accounts, definitions and replay files into that directory.
2. Run `crusades_admin NEW_DATABASE health --limit 100000` and inspect campaigns,
   audit and known completed battle records. Address any reported incomplete or
   unhealthy checks before switching the service.
3. Start `takserver` with the new database/accounts/definition/replay paths and
   sign in through a normal client. Verify the campaign, allegiance and history.

No force-overwrite/restore command deletes a live database. Do not copy old
service-lock files or journal/WAL/SHM files into the restored set. The new service
creates/acquires its own lease. Completed results remain immutable. Old Issued
and Started runtimes cannot survive a restart: before admitting clients, startup
atomically cancels them with explicit `recover-battle` audit rows, preserving
identity/history and issuing **no guessed winner or credit**. Reservations are
released, old room/resume tokens fail, and fresh battles can be issued. Failure
to persist recovery prevents service startup. Opening a store for offline
inspection/migration alone does not run this service recovery.

## Migration policy

Schema 9 adds only immutable admin audit records/indexes. Supported exact schemas
1–8 upgrade transactionally, retaining campaign events, memberships, rosters,
verified outcomes, rules decisions and history. Earlier mutations receive no
fabricated admin actor/reason. A failed migration rolls back the added objects
and version; unknown newer versions, unrelated databases and altered schema
definitions fail closed. Older binaries cannot open schema 9.

Back up with the version that already understands the existing database before
upgrading. For builds predating `crusades_admin`, stop the old service cleanly
and preserve its complete state directory before the first migration.
Opening an older database with the new tool/server performs migration
even for a valid inspection command; offline health is not a raw read-only SQLite
utility. Retain the old backup/build for rollback and restore into a new path,
never lower `PRAGMA user_version` or delete new tables in production. Database
migration does not change multiplayer protocol versions or replay byte layouts.

## Network/resource review

| Boundary | Enforcement and verification |
| --- | --- |
| Identity/authority | SCRAM proofs bind fresh challenges; replayed proofs and trailing/malformed handshake fields fail. Campaign handlers derive canonical accounts from authenticated connections; foreign status IDs remain private. |
| Results/replay | Server referee creates results; duplicate tickets/room identities/result writes cannot repeat credit. Client forged winner/state packets have no authority. Retained replay access stays enrollment-gated and bounded. |
| Reconnect | Shared account/IP budgets survive socket churn. Resume requires the original participant and current one-use rotated token; frozen/faulted/terminal campaign rooms reject rejoin. |
| Restart | Exclusive service lease, durable schema and audited orphan cancellation before admission; tested completed records and backup recovery remain readable. |
| Version/malformed messages | Exact current protocol/payload parsing rejects incompatible versions, truncated data, wrong flags/enums/counts/UTF-8 and trailing bytes without publishing partial state. No wire-version increment is needed for these compatible checks. |
| Campaign work | Existing ordinary read limit 32/s and replay limit 64/s per connection remain. Shared work budgets add 64 units/s per connection/account, 256/IP and 1,024 globally; replay work has separate budgets. Catalog pages cost up to 8 units according to requested size. Mutating allegiance/issuance requests also pass the shared gate. Excess replies are bounded instead of amplifying floods. |
| Authentication work | IP budget 64 and global 256 authentication messages/s, existing failure/account lockouts, fixed 30-second preauthentication lifetime independent of Ping. Retained login-address admission keys are bounded. |
| Connections/queues | At most 256 connections, 64 pending logins and 64 per address; 32 accepts per pass, 1,024 messages per connection/pass, 4 MiB queued output per peer and 64 MiB aggregate. Existing receive/frame caps remain. Slow peers cannot grow an unlimited backlog. |
| Limiter storage | Shared identity tables have at most 4,096 keys. Campaign keys expire after one quiet minute; login work keys retain the existing one-hour failure horizon. Exhaustion refuses new work rather than growing memory. |

These limits bound abusive clients and service work; they are not a promise of
protection against link saturation or every distributed flood. SCRAM provides
account authentication; the existing TCP protocol does not encrypt game traffic
or provide a MAC for each post-login message. This milestone does not add TLS or
claim a new transport-security protocol. Local filesystem administrators remain
trusted; immutable application triggers are not a defense against an OS owner
deliberately replacing the database.

## Acceptance and validation

Implementation tests cover audited mutation rollback, migrations 1–8, schema and
semantic tampering, backup restore, cancellation conflicts, terminal/duplicate
protection and process-lease exclusion/crash release. The real-server hardening
gate covers authentication/version malformations, socket-churn and multi-address
limits, framing/admission/deadlines, resume ownership/rotation, interrupted
battles and a backed-up service restored into a new directory. Existing referee,
history, matchmaking and actual SDL UI tests remain part of validation.

The acceptance checks map to these executable gates:

| Acceptance | Verification |
| --- | --- |
| Clean restart preserves campaign | Existing authenticated network/history/referee gates, plus M13 bootstrap audit checks. |
| Backup recovers a usable service | `crusades_hardening_network`: stop, SQLite backup, restore into a new directory, start a fresh authenticated server, read allegiance/result/replay and issue a new battle. |
| Duplicate and replay paths reject changes | Existing result/battle/referee tests and M13 replayed SCRAM proof, rotated resume token and interrupted-battle capability tests. |
| Unsupported versions fail safely | Protocol/parser/store tests and live incompatible Hello/campaign-payload checks. |
| Administration is auditable | 161 store/admin checks, 105 CLI checks, 15 lease/process checks, transactional rollback, immutable audit and startup recovery tests. |

All administration/store code and the pinned SQLite amalgamation also passed
ASan/UBSan with leak detection enabled on Linux (161 checks, no diagnostics).
Windows API compilation was checked with MinGW headers; native Windows/macOS
execution is covered by the repository's CI jobs. Packaged Release executables
were checked for unchanged system-only dynamic dependencies, and native CMake
installation includes `crusades_admin`.

Final integration, 2026-09-30: **all Release and Debug targets rebuilt**;
**153/153 Release CTests and 161/161 Debug CTests passed**, including all seven
authenticated live-server gates, actual campaign UI tests, referee verification,
transport/combat and retail pathfinding regressions. This completes M13.

Source boundaries:
[`CampaignStore`](../../../src/server/crusades/store.h),
[`CampaignServiceLease`](../../../src/server/crusades/servicelease.h),
[`takserver`](../../../src/server/server.cpp),
[`admin CLI`](../../../tools/crusades_admin.cpp).
