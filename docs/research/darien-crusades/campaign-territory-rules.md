# Milestone 7: territory rules and evidence boundaries

M7 isolates campaign policy from simulation and persistence. The historical
policy remains the default and does not invent territory calculations. A
separately identified synthetic fixture exercises actual state transitions.
Implementation and validation completed 2026-09-30. The original Boneyards
territory arithmetic remains unrecovered.

## What the primary sources support

The [shipped help](campaign-rules-evidence.md) documents momentum from recent
battles, neighboring ownership effects, time-based fatigue, rank-sensitive
victory points and deferred orphan results. Its version caveats matter: one
page explicitly describes the December 1999 beta, and another discusses the
then-upcoming Iron Plague expansion.

The [recon templates and native reader](territory-parameters.md) establish
side-specific toughness, support and battle victory points plus shared fatigue.
The template explains capture using momentum and combined points exceeding
required points. That is a confirmed presentation explanation, not a recovered
server comparison instruction. The client receives these values rather than
calculating them. README-3 also mentions battle-free capture, without resolving
how its momentum requirement interacts with the template explanation.

| Behavior | Evidence classification | M7 treatment |
| --- | --- | --- |
| Count uppercase H/T in a supplied momentum string | Reconstructed native client display loop | Pure display helper; stops at NUL, other bytes ignored; no twenty-entry truncation |
| Latest twenty battles affect momentum | Confirmed beta-era FAQ statement | No invented authoritative window ordering, tie rule or side selection |
| Combined fatigue/support/battle points exceed required points with momentum | Confirmed template explanation | No automatic historical ownership change |
| Resistance, rank weights, time fatigue and neighbor support | Named/documented concepts; arithmetic unknown | Preserve supplied metrics; do not generate values |
| Orphan results contribute when contested again | Confirmed FAQ description | Keep result evidence; do not fabricate entrenchment value, lifetime or reset |
| Fixture cumulative wins cause capture | Authored synthetic test policy | May execute only under its distinct persisted fixture identity |

Even an evaluator supplied all displayed metrics would be interpreting prose,
not reproducing the authoritative server. Equality, rounding, simultaneous
qualification, momentum ties, application order and resets remain unknown.
The separate `inspectHistoricalCapture` advisory exposes that textual
interpretation with those limitations; it is not called by the historical
mutation path. It requires all seven finite supplied metrics and an explicit
momentum side. Missing values, equality, arithmetic overflow or both sides
exceeding their requirements return `Indeterminate`. Otherwise it reports
`Exceeds` or `DoesNotExceed` using explicitly modern binary64 arithmetic, with
`ConfirmedPresentation` evidence. None of these advisory results captures a
territory or asserts original server numeric precision.
The native momentum loop terminates at the first zero byte: EXE-3 tests the
initial byte at `0x4603f5`/`0x4603f7` and subsequent bytes at
`0x460409`/`0x46040b`. Bytes after NUL are not history entries.
Zero-valued HTML placeholders are not historical starting values. Geometric
border contact is not proof of server adjacency.

## Pure policy interface

`src/server/crusades/rules.{h,cpp}` provides a pure evaluator with explicit
policy, campaign definition/state and a trusted winning allegiance. It has no
clock, randomness, network or simulation access. Decisions contain policy ID,
evidence classification, disposition, explanatory reason and resulting state.

- `historical-darien-v1` is the default. Eligible battles return `UnknownRules`
  and leave state unchanged. No eligible winner returns `Ineligible`.
- `fixture-capture-N-v1` is explicitly authored test behavior: one cumulative
  point per eligible win for that side, saturated at the configured threshold
  (1–1,000,000), and capture of the target at that threshold. No neighbor
  propagation, fatigue/support generation, rank weighting or capture reset is
  implied. Integer fixture counts are validated before incrementing.
- `modern-reserved-v1` reserves a separate identity and reports unsupported
  rules. It does not fall back to fixture or historical arithmetic.

Policy identifiers are canonical and versioned; malformed identifiers and
foreign fixture parameters are rejected. The historical label is not a claim
that unknown calculations have been implemented. The fixture's use of the
battle-points field is test state, not a recovered Cavedog unit conversion.

The M6 rule that changed campaign/allegiance revisions invalidate modern duel
eligibility is distinct from historical orphan-result behavior. Retaining a
verified result is useful evidence, but does not itself implement the FAQ's
later entrenchment credit. Neither this page nor M7 reclassifies that modern
safety policy as retail behavior.

## Bounded additional evidence search

On September 30, 2026, a targeted follow-up searched for Darien Crusades,
momentum, orphan battles, support, toughness and victory-point rules. Indexed
results supplied no applicable additional primary arithmetic. The preserved
[Cavedog Kingdoms news page](https://zx.net.nz/mirror/www.cavedog.com/ta-kingdoms/news.html)
links the original `totalannihilation.com/boneyards/by_tak/dcrusades.html` page.
Candidate routes for that Boneyards page in the same mirror returned HTTP 404;
a Wayback CDX request for the original page returned HTTP 503. This is a bounded
retrieval result, not proof that no archive exists. No unrelated game's rules
were adopted. Existing fingerprinted patch help/templates remain the strongest
available evidence.

An original server/configuration backup or captured authoritative territory
updates spanning known battles could change this conclusion. Another client
screenshot, map image or display string alone cannot recover the missing
numeric update function.

## Persistence and validation

Schema version 5 adds immutable campaign policy, issued-battle policy and
per-result decision records. Creation defaults to Historical Darien. Migration
from versions 1–4 assigns that explicit historical identity to existing
campaigns and issued battles; it does not invent decisions for old results.

Later migrations add the participant projection in schema 6 and account-wide
reservation index in schema 7. [Milestone 11](campaign-history.md) adds the
immutable verified territory-history projection introduced in schema 8.
Migration backfills that projection from existing verified results without
manufacturing results or rules decisions; supported older schemas upgrade
atomically.

The result, rules decision, optional campaign snapshot/revision and terminal
battle status commit in one SQLite transaction. The winning allegiance comes
from the eligible verified result and its pinned roster, not from a client rule
request. Policy identity must match the immutable issued-battle binding. Stored
decisions retain before/after revisions, evidence, disposition, explanation and
resulting snapshot. An unchanged historical decision is still audited without
advancing the campaign state revision.

The verified-result rules path alters state only under an explicitly enabled
fixture policy and an eligible result.
No-credit outcomes cannot earn fixture points. Duplicate result protection also
prevents duplicate rules application. The store defaults to `allowFixtureRules=false`; fixture creation/loading
requires explicit trusted test opt-in, which the shipped server never enables.
The reserved modern policy is rejected. Thus the live historical duel service
cannot accidentally offer fixture behavior.

The separate trusted `CampaignStore::commit` API accepts validated,
caller-authored state under any permitted campaign policy. It is not directly
exposed by a client request and rejects reserved issued-battle result identities.
The fixture-only restriction above applies to verified-result rule evaluation,
not to that authored-state persistence API. See [the store contract](campaign-store.md)
and the [M12 reconstruction audit](../../darien-crusades-reconstruction.md).

All six focused suites pass ASan/UBSan with leak detection and instrumented
SQLite: pure rules 91, rules store 30, results 135, battle issuance 167,
allegiance 160 and baseline store 284 checks. All six compile and link with
strict warnings under MinGW; this is not a Windows runtime test. These cover
strict policy identifiers, unchanged historical state, advisory unknown cases,
synthetic capture thresholds, native NUL termination, fixture opt-in, atomic
rollback/restart, and schema migration preserving existing records.
Both full builds passed, with **137/137 Release** and **142/142 Debug** tests.
The real authenticated server's victory, resignation and no-credit cases also
verify a historical campaign policy, its immutable battle binding and exactly
one atomic rules decision. Historical territory state and revision remain
unchanged. Final Windows server syntax checking passed; Windows and macOS
runtime behavior was not tested locally. No tactical simulation or protocol
change was needed for M7; its protocol was 207. [M8](campaign-network.md)
subsequently introduces campaign networking in protocol 208 and schema 6.

## Milestone 13 operations

The current store schema is **9**, adding immutable administrative events.
[Public-server operations](campaign-operations.md) describes transactional
migrations from versions 1–8, audited start/reset/cancellation, interrupted-battle
recovery, offline inspection and tested backup restoration. These are modern
service policies; historical territory rules remain unchanged.
