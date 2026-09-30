# Darien Crusades Reconstruction Plan

## Purpose

This document defines a research and implementation plan for reconstructing the
**Darien Crusades** online metagame from *Total Annihilation: Kingdoms* and
integrating a historically grounded recreation into TAK-Engine.

The work has two goals:

1. Recover and document how the original Darien Crusades actually worked.
2. Implement a modern TAK-Engine version without presenting guesses or modern
   additions as original Cavedog behavior.

Historical reconstruction comes first. Implementation must not invent missing
rules merely to complete the feature.

---

## Guiding principles

### Preserve the distinction between evidence and reconstruction

Every recovered behavior must be classified as one of:

- **CONFIRMED** — directly supported by original Cavedog material, shipped data,
  executable behavior, or other primary evidence.
- **RECONSTRUCTED** — derived from reverse engineering original binaries or data.
- **INFERRED** — strongly suggested by surviving evidence, but not directly proven.
- **MODERN** — intentionally introduced by TAK-Engine.
- **UNKNOWN** — evidence is currently insufficient.

Do not silently promote inferred behavior into historical fact.

### Keep proprietary assets out of the repository

Do not commit original Cavedog game assets, installers, executables, artwork,
maps, sounds, or other copyrighted game data.

The repository may contain:

- hashes;
- filenames;
- sizes;
- offsets;
- file-format descriptions;
- clean-room structures;
- derived metadata;
- reverse-engineering notes;
- tests that operate on a user's legally obtained installation.

### Keep the metagame outside the deterministic RTS simulation

The Darien Crusades strategic layer is server-side campaign state.

It must not become part of `World::stateHash()` or otherwise alter deterministic
RTS state except through the normal match configuration and result paths.

Conceptually:

```text
Darien campaign state
        |
        v
battle assignment
        |
        v
takserver GameRoom
        |
        v
deterministic TAK match
        |
        v
authoritative result
        |
        v
campaign-state update
```

### Separate game mode from balance mode

The following are distinct concepts:

```text
GameMode::Normal
GameMode::Crusades
```

and:

```text
BalanceMode::Retail
BalanceMode::Crusades
```

Historical Crusades play may require Crusades Balance, but code must not conflate
the two concepts.

---

# Milestone 1 — Historical archaeology

## Objective

Determine exactly what the Darien Crusades package added, how it interacted with
TA:K, and which responsibilities belonged to the TA:K executable, the Boneyards
client, and Cavedog's servers.

No production networking or campaign implementation belongs in this milestone.

## 1.1 Acquire and fingerprint original distributions

Locate preserved copies of:

- the Darien Crusades-enabled TA:K 3.0 distribution;
- the ordinary TA:K 3.0 update without Crusades;
- relevant Iron Plague distributions;
- any surviving Boneyards client components;
- any original Crusades documentation or help files.

For each artifact record:

- original filename;
- byte size;
- SHA-256;
- other historically published hashes if useful;
- source/provenance;
- retrieval date;
- whether the artifact was independently verified.

Suggested document:

```text
docs/research/darien-crusades/sources.md
```

## 1.2 Unpack and inventory

Unpack each installer without modifying the source artifact.

Produce inventories containing:

- relative path;
- size;
- hash;
- file type;
- archive membership where applicable.

Compare:

```text
TAK 3.0 standard
        vs
TAK 3.0 Crusades
        vs
Iron Plague
```

Identify files unique to Crusades and files changed by Crusades.

Suggested outputs:

```text
docs/research/darien-crusades/
    installer-inventory.md
    crusades-vs-3.0.md
    crusades-vs-iron-plague.md
```

## 1.3 Parse all recognizable game data

Use existing TAK-Engine tooling wherever possible:

```text
hpitool
gaftool
tnttool
modeltool
cobtool
tdftool
missiontool
biktool
```

Add a small research-only parser only when existing tools cannot identify a
format.

Search especially for:

- territory definitions;
- territory names;
- strategic-map data;
- map-to-territory relationships;
- alliance definitions;
- Order of Honour references;
- Council of Terror references;
- fatigue;
- support;
- toughness;
- battle-state constants;
- Boneyards hostnames and URLs;
- result-reporting messages;
- player-ranking fields;
- campaign identifiers;
- Crusades-specific GUI resources;
- command-line or IPC parameters.

Produce a machine-readable inventory where practical.

## 1.4 Perform string and resource analysis

Extract strings and resources from Crusades-specific binaries.

Search for anchors including:

```text
Crusade
Crusades
Darien
Fatigue
Support
Toughness
Boneyards
Order
Honour
Council
Terror
Territory
Battle
Victory
Defeat
Rank
Ladder
```

Record:

- string;
- source file;
- offset/address;
- cross-references if known;
- likely subsystem;
- confidence classification.

## 1.5 Reverse engineer Crusades-specific code paths

Starting from confirmed strings and resources, identify code paths for:

```text
territory selection
        |
        v
Boneyards interaction
        |
        v
match setup
        |
        v
match start
        |
        v
match completion
        |
        v
result construction
        |
        v
result submission
```

Do not attempt to reverse engineer unrelated code merely because it is nearby.

Useful questions:

- Does TA:K know the campaign map state?
- Does TA:K know the territory adjacency graph?
- Does TA:K calculate fatigue/support/toughness?
- Does TA:K report only match results?
- Does Boneyards launch TA:K with battle parameters?
- Is an opaque battle/session token passed to the game?
- Does the game validate that it was launched for a Crusades battle?
- Which side owns the authoritative campaign state?

## 1.6 Recover Crusades Balance changes

TAK-Engine already supports Crusades Balance.

Verify that support against original shipped data.

Generate an automated difference between standard and Crusades rules/data and
document every changed value that affects gameplay.

Suggested output:

```text
docs/crusades-balance-reference.md
```

Tests should prove that TAK-Engine loads or reproduces each known original
difference correctly.

## 1.7 Search archived public material

Search historical sources for:

- Cavedog pages;
- Boneyards pages;
- TA:K support pages;
- archived FAQs;
- online manuals;
- release notes;
- patch notes;
- gaming press;
- contemporary reviews;
- old clan pages;
- fan sites;
- Usenet;
- archived forum discussions;
- screenshots;
- videos;
- download mirrors.

Prefer contemporary primary or near-primary sources over later recollection.

Preserve citations and archive URLs where possible.

Screenshots are especially valuable for reconstructing:

- strategic-map layout;
- labels;
- territory-state presentation;
- activity indicators;
- rank displays;
- battle-selection flow;
- alliance presentation.

## 1.8 Produce an evidence matrix

Create:

```text
docs/research/darien-crusades/evidence-matrix.md
```

Example:

| Behavior | Classification | Evidence | Notes |
|---|---|---|---|
| Territories have toughness | CONFIRMED | Original patch documentation | Exact range still unknown |
| Fatigue influences territorial collapse | CONFIRMED | Original documentation | Formula not yet recovered |
| Support influences territorial collapse | CONFIRMED | Original documentation | Formula not yet recovered |
| Exact capture formula | UNKNOWN | — | Must not be guessed |
| Territory adjacency limits attacks | UNKNOWN | — | Research required |

## Milestone 1 acceptance criteria

Milestone 1 is complete when the project can answer, with citations or
reverse-engineering evidence:

1. What files and code were added by the Darien Crusades package?
2. What did the TA:K executable itself know about the metagame?
3. What did Boneyards appear to control?
4. What data describes territories and battles, if any survives locally?
5. What are the known meanings of fatigue, support, and toughness?
6. Which Crusades rules are confirmed, reconstructed, inferred, or unknown?
7. What exact gameplay changes comprise Crusades Balance?

No historical behavior may be marked complete merely because a plausible
implementation exists.

---

# Milestone 2 — Clean-room campaign model

Completed 2026-09-30. Implementation, format and acceptance evidence:
[campaign model](research/darien-crusades/campaign-model.md).
This server-side module is separate from the existing single-player campaign
controller. It does not yet expose a playable online Crusades mode.

## Objective

Define a modern internal representation capable of expressing the recovered
Darien Crusades rules without hard-coding assumptions into the RTS simulation.

Suggested location:

```text
src/server/crusades/
```

Possible components:

```text
campaign.h
campaign.cpp
territory.h
territory.cpp
rules.h
rules.cpp
battle.h
battle.cpp
```

## Core concepts

A territory model may eventually contain fields such as:

```text
id
display name
map identifier
neighbors
owner
toughness
fatigue
support
activity
```

Only fields supported by the recovered rules should be treated as historical.

Do not copy placeholder fields into production merely because they seem useful.

## Campaign data should be data-driven

Avoid embedding the original Darien campaign directly into network or simulation
code.

A campaign definition should be loadable independently of live campaign state.

This leaves room for:

- historical Darien Crusades;
- test campaigns;
- future custom campaigns;
- smaller modern campaigns.

## Milestone 2 acceptance criteria

- Campaign definitions load deterministically.
- Territory references validate.
- Duplicate territory IDs fail validation.
- Invalid adjacency fails validation.
- Unknown maps fail validation where required.
- Historical data and live mutable state are separate.
- No Crusades campaign state enters `World::stateHash()`.

---

# Milestone 3 — Persistent campaign store

Completed 2026-09-30. Implementation and acceptance evidence:
[campaign store](research/darien-crusades/campaign-store.md).
This storage library is separate from player authentication and live game-room
integration in subsequent milestones.

## Objective

Persist campaign state safely and transactionally.

The existing account credential store should remain logically separate from
campaign state.

Recommended persistence candidates:

- SQLite for production;
- an in-memory implementation for tests.

Potential schema:

```text
campaigns
alliances
territories
players
player_allegiances
battles
battle_participants
battle_results
campaign_events
```

## Requirements

Campaign mutations must be atomic.

A server crash must not leave:

- partially applied territory ownership;
- a battle marked complete without its result;
- duplicate result application;
- inconsistent activity/support/fatigue values.

Maintain an append-only or auditable event history sufficient to explain how the
current campaign state was reached.

Example events:

```text
BattleCreated
BattleStarted
BattleCompleted
TerritorySupportChanged
TerritoryFatigueChanged
TerritoryOwnerChanged
PlayerJoinedAlliance
CampaignStarted
CampaignEnded
```

## Milestone 3 acceptance criteria

- Restart preserves complete campaign state.
- Interrupted transaction does not partially apply.
- Duplicate result application is rejected.
- Campaign history can explain every ownership change.
- Tests can reconstruct a known state from persisted data.

---

# Milestone 4 — Authenticated Crusades allegiance

Completed 2026-09-30. Implementation and acceptance evidence:
[authenticated allegiance](research/darien-crusades/campaign-allegiance.md).
This establishes server-side participation. Battle issuance is covered by
Milestone 5; a player-facing campaign UI remains later work.

## Objective

Associate an authenticated TAK-Engine player with campaign participation.

Existing account authentication remains authoritative for identity.

Campaign data may record:

```text
account
campaign
alliance
joined time
campaign statistics
```

Do not place password material or authentication secrets in the campaign store.

## Historical behavior

If original Crusades allegiance restrictions are recovered, implement them in
historical mode.

Examples requiring evidence before adoption:

- permanent allegiance for a campaign;
- cooldown before switching;
- one global allegiance per account;
- faction restrictions based on alliance.

## Milestone 4 acceptance criteria

- Anonymous clients cannot alter persistent campaign state.
- Allegiance is tied to authenticated identity.
- Invalid allegiance changes are rejected.
- Campaign state never contains credentials.

---

# Milestone 5 — Battle issuance

Completed 2026-09-30. Design, security boundaries
and acceptance evidence: [campaign battles](research/darien-crusades/campaign-battles.md).
The initial modern server duel issues and binds battles but does not apply
campaign credit. Authoritative result processing remains Milestone 6 below.

## Objective

The campaign service creates an authoritative battle definition before a match
can affect the metagame.

A battle record may include:

```text
battle ID
campaign revision
territory
map
attacking alliance
defending alliance
eligible participants
game rules
balance mode
creation time
single-use token/nonce
```

Exact fields depend on recovered historical behavior.

## Security model

Clients must not be able to invent a campaign-valid battle by starting a normal
multiplayer match.

The campaign server owns the transition:

```text
campaign state
    -> battle issued
    -> GameRoom created/associated
```

## Milestone 5 acceptance criteria

- Battle IDs are unique.
- Expired/cancelled/completed battles cannot be reused.
- Wrong-map matches cannot report against a battle.
- Wrong participants cannot report against a battle.
- A normal multiplayer match cannot mutate campaign state.

---

# Milestone 6 — Authoritative match-result processing

Completed 2026-09-30. See [the result contract, modern outcome policy and
validation evidence](research/darien-crusades/campaign-results.md).

## Objective

Use `takserver` as the authority for Crusades results.

Clients must never be trusted to submit statements equivalent to:

```text
I won this territory.
```

The server already knows or can verify:

- authenticated participants;
- map;
- teams;
- factions;
- balance mode;
- match duration;
- defeats;
- forfeits;
- authoritative command sequence;
- referee outcome;
- final simulation state/hash where applicable.

Define a server-generated result object, for example conceptually:

```text
VerifiedMatchResult
```

The campaign service consumes only verified server results.

## Required result handling

Explicitly define behavior for:

- normal victory;
- resignation;
- disconnect;
- timeout;
- server abort;
- draw if historically valid;
- referee failure;
- desync;
- invalid client;
- participant substitution if ever supported.

Historical mode should follow recovered original rules where known.

## Milestone 6 acceptance criteria

- A client cannot forge a campaign win.
- Duplicate result submission is rejected.
- Aborted/referee-invalid matches do not mutate campaign state.
- Result processing is atomic with battle completion.
- Replays can be associated with the authoritative battle result.

---

# Milestone 7 — Territory rules

Completed 2026-09-30. See [policy separation, evidence boundaries and validation](research/darien-crusades/campaign-territory-rules.md).
Historical capture arithmetic remains unknown; automatic historical ownership
changes remain blocked. Fixture transitions are explicitly nonhistorical.

## Objective

Implement the historical territory-update rules only after sufficient evidence
exists.

Known concepts may include:

- support;
- fatigue;
- toughness;
- territory ownership;
- activity.

Do not assume a formula such as:

```text
support + fatigue >= toughness
```

unless direct evidence or reverse engineering establishes it.

## Rule implementation structure

Rules should be isolated behind a campaign rules interface so that:

- Historical Darien can reproduce original behavior.
- Tests can run simplified fixture rules.
- A future modern mode can use tuned rules without changing historical mode.

## Milestone 7 acceptance criteria

For every implemented historical rule:

- evidence classification is documented;
- tests reproduce known examples or recovered behavior;
- unknown original behavior remains marked unknown;
- no modern balancing change is silently applied to historical mode.

---

# Milestone 8 — Crusades network protocol

## Objective

Extend TAK-Engine networking with campaign-specific messages after the server
model is stable.

Potential concepts:

```text
CampaignList
CampaignSnapshot
CampaignDelta
PlayerCampaignStatus
BattleOffer
BattleAccepted
BattleCancelled
BattleStarted
BattleCompleted
CampaignError
```

Prefer explicit versioned structures over overloaded generic messages.

## Campaign revision

Snapshots and updates should carry a campaign revision or equivalent monotonic
version so clients can detect stale state.

## Milestone 8 acceptance criteria

- Client reconnect obtains a complete valid campaign snapshot.
- Stale updates are rejected or corrected.
- Campaign protocol changes are versioned.
- Campaign traffic cannot alter the deterministic match command stream.

---

# Milestone 9 — Strategic map UI

## Objective

Recreate the Darien Crusades campaign interface as faithfully as evidence allows.

Potential flow:

```text
MULTIPLAYER
    |
    +-- Normal Game
    |
    +-- Darien Crusades
            |
            +-- strategic map
            +-- territory details
            +-- available/active battles
            +-- alliance state
            +-- player campaign status
```

## Historical presentation

Use recovered screenshots, resources, terminology, and behavior where legally
and technically appropriate.

Do not commit copyrighted original artwork.

If original assets must be read from the user's installation, use the normal
TAK-Engine asset-loading model.

## Data flow

The client renders campaign state received from the server.

The client must not calculate authoritative territory ownership locally.

## Milestone 9 acceptance criteria

- Strategic map displays all current territories.
- Ownership and activity reflect authoritative server state.
- Client can inspect territory details.
- Client can enter an eligible battle.
- Disconnect/reconnect restores state.
- UI distinguishes historical facts from TAK-Engine-only additions where needed.

---

# Milestone 10 — Crusades matchmaking and active battles

## Objective

Provide the server-side workflow that turns campaign pressure into playable
matches.

Exact matchmaking rules depend on recovered historical behavior.

Potential server responsibilities:

- identify eligible territories;
- advertise available battles;
- enforce alliances;
- select map/rules;
- create GameRooms;
- associate players with battles;
- prevent duplicate participation where required.

## Milestone 10 acceptance criteria

- Every campaign-affecting match is linked to one authoritative battle.
- Every authoritative battle links to at most one completed result.
- Invalid lobby configuration cannot mutate campaign state.
- Cancelling an unplayed battle leaves territory state unchanged.

---

# Milestone 11 — History and replay integration

## Objective

Connect existing TAK-Engine replay infrastructure to campaign history.

Potential territory history:

```text
Territory: Cairbray
Owner: Order of Honour

Recent battles:
2026-10-04  PlayerA defeated PlayerB
2026-10-03  PlayerC defeated PlayerD
```

Where retained and allowed, a completed campaign battle may reference its replay.

Potential UI action:

```text
Watch Replay
```

## Milestone 11 acceptance criteria

- Completed battles can be listed by territory.
- Replay linkage cannot change campaign outcome.
- Missing/deleted replay does not corrupt campaign history.
- Historical result metadata remains available independently of replay files.

---

# Milestone 12 — Historical validation

## Objective

Compare the completed historical mode against all recovered original behavior.

Audit:

- alliance rules;
- territory graph;
- territory starting ownership;
- fatigue;
- support;
- toughness;
- battle eligibility;
- result handling;
- Crusades Balance requirement;
- UI terminology;
- activity representation;
- campaign transitions.

Produce:

```text
docs/darien-crusades-reconstruction.md
```

This document should explicitly list:

```text
CONFIRMED
RECONSTRUCTED
INFERRED
MODERN
UNKNOWN
```

for every significant behavior.

## Milestone 12 acceptance criteria

A contributor should be able to answer:

> Why does TAK-Engine's Darien Crusades behave this way?

and point to either:

- primary historical evidence;
- reverse-engineering evidence;
- an explicitly documented modern decision.

---

# Milestone 13 — Public-server hardening

## Objective

Make a Crusades service suitable for operation on an Internet-facing `takserver`.

Required review areas:

- authentication;
- authorization;
- replay attacks;
- duplicate battle submission;
- database integrity;
- campaign-state backup;
- rate limits;
- malformed campaign messages;
- reconnect behavior;
- server restart during a battle;
- version incompatibility;
- denial-of-service surfaces.

## Operational requirements

Provide:

- backup instructions;
- restore test;
- database migration policy;
- campaign reset/start tooling;
- admin inspection tooling;
- safe battle cancellation;
- campaign health/status command.

## Milestone 13 acceptance criteria

- Campaign survives clean server restart.
- Campaign survives recovery from tested backup.
- Duplicate and replayed result paths are tested.
- Unsupported protocol versions fail safely.
- Administrative operations are auditable.

---

# Optional future milestone — Modern Crusades mode

Historical reconstruction and modern balancing must remain separate.

A modern mode may eventually alter rules to suit today's player population.

Possible modern features include:

- lower territorial thresholds;
- campaign seasons;
- scheduled campaign resets;
- inactivity handling;
- smaller active fronts;
- AI-assisted battles;
- custom campaigns;
- alternate victory conditions.

Every such rule is classified **MODERN** and must not modify Historical Darien
behavior.

---

# Proposed developer tooling

## `crusadestool`

A dedicated research/admin utility would make both reconstruction and future
maintenance reproducible.

Potential commands:

```text
crusadestool inventory <path>
crusadestool compare <standard-path> <crusades-path>
crusadestool strings <path>
crusadestool balance <standard-path> <crusades-path>
crusadestool territories <path>
crusadestool verify <campaign-definition>
crusadestool dump-campaign <database>
crusadestool history <database>
```

Research commands must not require proprietary data to be committed.

---

# Test strategy

## Unit tests

Cover:

- campaign-definition parsing;
- territory validation;
- adjacency validation;
- allegiance rules;
- battle eligibility;
- territory update rules;
- persistence;
- campaign revisions;
- duplicate-result rejection;
- invalid token rejection.

## Integration tests

Representative flow:

```text
authenticated player A
authenticated player B
        |
        v
join opposing alliances
        |
        v
campaign issues battle
        |
        v
takserver starts associated GameRoom
        |
        v
match completes deterministically
        |
        v
server creates VerifiedMatchResult
        |
        v
campaign transaction applies result
        |
        v
territory state changes
        |
        v
both clients receive new revision
```

## Adversarial tests

Include:

- forged client result;
- duplicate result;
- replayed battle token;
- wrong map;
- wrong campaign;
- wrong territory;
- wrong participant;
- unauthorized account;
- disconnect before result;
- crash during transaction;
- stale campaign revision;
- malformed campaign packet;
- desynchronized or referee-invalid game;
- modified client.

---

# Suggested documentation layout

```text
docs/
    darien-crusades-reconstruction.md
    crusades-balance-reference.md

    research/
        darien-crusades/
            sources.md
            installer-inventory.md
            crusades-vs-3.0.md
            crusades-vs-iron-plague.md
            strings.md
            binary-notes.md
            archived-material.md
            evidence-matrix.md
```

---

# Recommended implementation order

Do not start with the strategic map UI.

Recommended order:

```text
M1   Historical archaeology
M2   Clean-room campaign model
M3   Persistent campaign store
M4   Authenticated allegiance
M5   Battle issuance
M6   Authoritative result processing
M7   Territory rules
M8   Crusades network protocol
M9   Strategic map UI
M10  Matchmaking / active battles
M11  History + replay integration
M12  Historical validation
M13  Public-server hardening
```

The important dependency chain is:

```text
evidence
   -> model
   -> persistence
   -> authoritative battles/results
   -> rules
   -> protocol
   -> UI
```

Do not reverse this order.

---

# First actionable task

Completed first pass on 2026-09-30: [research tooling, inventories, sources and
evidence matrix](research/darien-crusades/README.md). This does not mark the full
Milestone 1 historical/binary audit complete.

The first implementation task for an agent should be research tooling and
documentation only:

1. Add the `docs/research/darien-crusades/` structure.
2. Define the source/provenance template.
3. Add a local-only comparison script or `crusadestool inventory/compare`
   skeleton that accepts user-supplied Crusades and standard 3.0 installations.
4. Produce hash/file inventories.
5. Produce a changed-file report.
6. Identify which changed files can already be parsed by TAK-Engine tools.
7. Produce an initial evidence matrix.
8. Do **not** add campaign protocol or persistent campaign behavior yet.

Completion of this task should leave the repository with reproducible evidence,
not speculative gameplay code.

---

# Definition of success

The project is successful when TAK-Engine can run a persistent Darien Crusades
campaign whose historical behavior is traceable to documented evidence, whose
matches are authoritative and deterministic, and whose modern additions are
clearly distinguished from the original Cavedog design.

The final result should make it possible to preserve not merely the name
"Darien Crusades", but the actual online metagame to the greatest extent the
surviving evidence permits.
