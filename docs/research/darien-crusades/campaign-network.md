# Milestone 8: campaign network snapshots

M8 adds a modern, engine-owned campaign read protocol and `MpClient` integration.
It is separate from the historical Boneyards protocol and from tactical command
bundles. Completed September 30, 2026. The strategic map UI remains Milestone 9.

## Messages and authority

The new request/response families are campaign catalog, full campaign snapshot,
own player campaign status, battle status and structured error. Their codecs
live in `src/net/crusades.{h,cpp}`. Each payload begins with a little-endian
version (`u16`, currently 1) and request ID (`u32`); network protocol version
208 gates the new message families. Its outer network message
identifies the family. Zero request ID is reserved for server notifications.

The catalog is paginated. Full snapshots contain campaign identity, display name,
revision, persisted rules policy and territories with optional ownership, map
assignment, adjacency and recon metrics. Missing values remain missing. Own
player status contains allegiance and bounded recent battle references. Battle
status describes the issued battle lifecycle and, when present, the authoritative
outcome. Launch secrets, room tokens, credentials and private persistence payloads
are not part of these DTOs.

Requests do not supply an acting account. The authenticated server connection
selects the player identity; the service validates access. Existing allegiance
and battle-issuance commands retain their separate mutation contracts. Clients
cannot upload territory state or verified results through these messages.

## Server subscriptions and lifecycle

The most recently requested full campaign snapshot selects the connection's
implicit subscription. The server checks subscribed revisions once per second,
using a cheap revision read, and sends a full replacement only when needed.
Outgoing-buffer backpressure prevents refresh traffic from accumulating behind a
slow connection. Explicit reads are limited to 32 per second per connection:
the first excess request receives a correlated `Unavailable` error and further
excess requests in that window are dropped. The client's 15-second pending-read
expiry releases slots for unanswered requests without disconnecting the game.

Authenticated battle participants receive lifecycle notifications for issuance,
start, cancellation, expiry and durable result completion. A restarted server
can report an old orphan `Started` record, but its room ID is zero; this reports
a durable historical record and does not offer live reattachment or reconstruct
a lost referee. No room token or launch capability is exposed.

Schema version 6 adds an indexed participant-to-battle lookup for bounded own
history reads. Migration preserves prior records and policy bindings. This
index does not grant access to another account's history, and notifications do
not change the persisted Historical Darien policy. Native historical arithmetic
remains unresolved and blocked; no read protocol invents territory credit.

## Strict decoding and replicas

Encoders and decoders validate complete messages, not prefixes. Version, count,
string length, UTF-8, optional flags, enum and numeric checks apply before an
object is published. Payloads are capped at 240 KiB; catalog pages at 64 entries,
snapshots at 1024 territories, neighbor lists at 256 and recent battle lists at
32. Oversized snapshots fail explicitly instead of being truncated into a
plausible partial state.

`Replica` applies full snapshots atomically. Older revisions and conflicting
content at the same revision are rejected without changing cached state.
Equivalent canonical content at the same revision is unchanged, even if request
IDs or input ordering differ. This is not a delta protocol; refresh requests
retrieve another full snapshot.

## MpClient behavior

`MpClient` exposes `listCampaigns`, `getCampaignSnapshot`,
`getPlayerCampaignStatus` and `getCampaignBattleStatus`. Each returns a nonzero
request ID, or zero when unavailable/rejected locally. Outstanding reads are
bounded to 128 and responses are correlated with request family and identity.
Malformed or mismatched responses release their request slot. Pending reads
expire after 15 seconds with a campaign-specific error, without disconnecting
the tactical session.
Server notifications use request ID zero. Snapshot/player notifications are
accepted for the selected campaign; battle notifications can include a newly
invited campaign and are cached with a 64-battle bound.

`subscribeCampaign` remembers the selected campaign across reconnects. All
server-specific campaign caches and outstanding requests clear on connection
replacement or disconnection. After authenticated Welcome the client requests
the catalog and, if subscribed, a full snapshot and own status. A new server can
therefore supply revision zero without being rejected against an old server's
higher revision. Anonymous connections neither request nor accept campaign data.

A stale selected snapshot triggers at most one automatic full refresh until a
successful update or explicit resubscription. Equal-revision conflicts surface
an error. Malformed responses preserve existing state, surface a campaign error
and do not trigger recursive refresh or disconnect the tactical session.
Allegiance revisions, immutable battle identity and terminal result payloads
are protected against regression or conflicting replacement. Late own-status
responses for a previous subscription cannot replace the current selection.

Existing battle-issuance responses/invitations are exposed to the caller and
automatically trigger the new detailed battle-status query. Existing allegiance
responses trigger a current player-status query. These helpers do not select a
territory, join an invitation or display a new screen automatically.

Campaign reads and notifications do not alter `MpClient`'s tactical state,
command/event bundles, recorded replay bytes or hash log. They never access a
`World`. Rules authority remains in the server/store; the historical policy
still has [unrecovered territory arithmetic](campaign-territory-rules.md).

## Validation

`tools/crusades_client_network_test.cpp` drives the real `MpClient` over loopback
with a scripted server and real SCRAM mutual authentication, requiring no game
assets. Its 93 checks pass: automatic reads, catalog/snapshot/status
caches, stale/conflicting/malformed replies, bounded refresh, invitations and
lifecycle notifications, tactical bundle/replay isolation, reconnect cache
clearing and fresh subscriptions, and anonymous rejection. It also checks long map identifiers, conflicting
terminal hashes and subscription-switch races. The new client and codec paths also pass ASan/UBSan with leak detection.
Supporting existing framework libraries in that focused run were not instrumented.
The final `client.cpp` and loopback test both pass strict MinGW compilation
(`-Wall -Wextra -Werror`); this is not a Windows runtime test.
The shared codec passes 711 checks, including every truncated response prefix,
invalid UTF-8, flags/enums, received NaN values, duplicate territory IDs,
adjacency errors, bounded payloads and atomic replica updates. The authenticated
service passes 54 checks covering authorization, pagination, unknown-versus-zero
metrics, stale/equal/ahead snapshots, private battle data, durable results,
indexed newest-first history and schema-5 migration/rollback. All five existing
persistence suites also pass. Codec and service/persistence checks pass strict
GCC, ASan/UBSan with leak detection, and strict MinGW compilation/linking. No
Windows or macOS runtime run was performed locally; CI includes the new targets.

All 141 Release and 146 Debug CTests pass after full builds of both configurations.
`crusades_protocol_network_test.py` exercises real SCRAM connections, exact
version errors, unauthenticated/disabled queries, live revision subscriptions,
stale/ahead correction, participant isolation, issued/started/completed
notifications and durable snapshot/result recovery after restarting the server.
It also verifies that the authoritative replay still contains tactical bundles
and its terminal hash. The existing result network sweep now additionally tests
forged campaign snapshots: they receive no credit and cannot change territory
state. GCC and Clang at O0/O2/O3 agree on the deterministic math golden hash;
the local ARM cross-build legs lack the required target headers and were skipped.

| M8 acceptance criterion | Evidence |
|---|---|
| Reconnect obtains a complete valid snapshot | Real `MpClient` authenticated reconnect and real server restart tests |
| Stale updates rejected or corrected | Replica atomicity/conflict tests, live stale/ahead reads and persisted subscription refresh |
| Protocol changes versioned | Network version 208, payload version 1, malformed/version tests |
| Campaign traffic cannot alter tactical commands | Client bundle/replay byte preservation, real referee replay verification and forged-snapshot no-credit test |

## M9 activity extension

Network version 209 and campaign payload version 2 append optional offered/active
battle counts to each territory. These are current server-room observations,
independent of the stored ownership revision and historical battle-point metrics.
Unknown activity remains distinct from zero. Ordered activity-only snapshots may
refresh the client at the same campaign revision; conflicting persistent fields
still fail validation. Full legacy clients must update to the matching protocol.

## M10 matchmaking extension

Network version 210 and campaign payload version 3 add the separate authoritative
matchmaking board and Find/Cancel requests. Schema version 7 indexes account-wide
battle reservations. See [the matchmaking contract and live checks](campaign-matchmaking.md).
