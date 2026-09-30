# Clean-room campaign model (Milestone 2)

**Milestone 2 complete — 2026-09-30.** All seven acceptance criteria in the
[plan](../../darien-crusades-plan.md) are covered below.

The model in `src/server/crusades/` defines a campaign independently of its live
state. It is distinct from `src/campaign/`, which handles the existing
single-player mission campaigns. This library adds no player-facing Darien mode,
network messages, persistence, lobby flow, or campaign data to the RTS simulation
or `World::stateHash()`. Later layers add [transactional persistence](campaign-store.md)
and [authenticated allegiance requests](campaign-allegiance.md), while keeping
this model separate from tactical simulation.

## Evidence and design boundaries

[Milestone 1](milestone-one-status.md) recovered territory IDs and presentation,
native factions/terrain, server-supplied ownership and map settings, and the
client's battle/report lifecycle. It did **not** recover an authoritative Darien
adjacency graph, complete map assignment, starting live ownership, or exact
fatigue/support/toughness formulas.

Consequently, this model does not manufacture any of those historical values.
Native faction is descriptive and never becomes an owner. Missing adjacency
means unknown, while an explicitly empty neighbor list means isolated. Missing
map and owner remain unknown. The owner vocabulary is Contested/Honor/Terror;
Neutral as a native-faction label does not create another campaign allegiance.
Optional live recon metrics preserve the recovered meanings of fatigue victory
points and each side's required, support and battle victory points. Their values
remain unknown until supplied; there are no placeholder defaults or capture
formulas.

The versioned text format, graph validation rules and API are modern engine
interfaces, not a claim to the original `Darien.def` serialization or server
protocol. A supplied graph is an explicitly authored **undirected** graph;
its edges must be reciprocal. This does not assert that historical Boneyards
used these same validation rules.

## Definition format, version 1

The format is UTF-8, one directive per line. Blank lines and full-line `#`
comments are allowed. Quoted strings must be nonempty and contain no control
characters; only escaped quote and backslash are recognized. All IDs are
positive unsigned 32-bit integers. Input is limited to 8 MiB.

```text
campaign 1 "example" "Example campaign"
territory 1 "Northern territory"
territory 2 "Southern territory"
native 1 "Aramon"
terrain 1 "forest"
map 1 "maps/example-north.ota"
map 2 "maps/example-south.ota"
neighbors 1 2
neighbors 2 1
```

This is an invented demonstration, not Darien territory/map data.

| Directive | Meaning |
|---|---|
| `campaign 1 "id" "name"` | Exactly one versioned campaign identity |
| `territory id "name"` | Territory definition; duplicate IDs are rejected |
| `native id "label"` | Optional descriptive native faction |
| `terrain id "label"` | Optional descriptive terrain label |
| `map id "identifier"` | Optional authored map preference |
| `neighbors id [neighbor-id ...]` | Optional complete neighbor list; no following IDs explicitly means isolated |

Directive order does not determine the resulting model. References may precede
their territory declaration. Territories are stored in numeric ID order and
neighbors are normalized into numeric order. Unknown directives, extra tokens,
duplicate properties, undeclared references, repeated neighbor IDs, self-edges
and asymmetric edges are errors. A partially supplied graph must still resolve
every declared edge reciprocally; unknown does not imply an inferred reverse
edge. Definitions must contain at least one territory.

## API and validation policy

The API is declared in `src/server/crusades/campaign.h`, in namespace
`tak::srv::crusades`:

- `loadDefinitionText` parses an in-memory definition with an optional diagnostic
  origin; `loadDefinition` reads a filesystem path. They return a validated
  `CampaignDefinition`, or throw `std::runtime_error` without returning a partial
  model.
- `CampaignDefinition` exposes const identity, territory lookup and the sorted
  territory collection. Callers do not mutate loaded definition fields through
  this interface.
- `DefinitionLoadOptions::mapExists` is the caller's map resolver. When supplied,
  every authored map is checked even when map assignment is optional.
- `requireMaps` requires a resolver and a map for every territory.
  `requireAdjacency` requires an explicitly supplied neighbor list for every
  territory. Neither policy invents missing historical data.
- `makeInitialState` creates a separate `CampaignState`, with the same campaign
  ID and one state entry per defined territory. Owners and runtime map
  assignments start unknown; an authored map preference is not a live
  assignment.
- `TerritoryState::recon` holds `ReconMetrics`: optional
  `fatigueVictoryPoints`, plus `honor` and `terror` `SideReconMetrics`, each with
  optional `requiredVictoryPoints`, `supportVictoryPoints` and
  `battleVictoryPoints`. Missing values remain distinct from zero. These are
  modern finite `double` values, not a claim about historical server numeric
  precision, bounds, rounding or update arithmetic. No sign constraint is
  imposed without evidence.
- `validateState` checks campaign identity, exact territory membership, valid
  owner values, supplied runtime maps and finite recon metrics. `requireMaps`
  also requires every runtime map assignment; `requireAdjacency` applies only
  when loading the definition, because live state has no graph.

Deterministic output assumes the same input and the same resolver answers.
The library does not access the game's map catalog implicitly, contact a server,
or look up additional historical data while parsing.

## Acceptance and validation

The dedicated `crusades_campaign_test` uses independently authored synthetic
campaigns, without retail assets. It checks definition ordering, territory and
graph validation, map resolution policy, definition/state separation, malformed
text rejection and file loading. Run it with:

```sh
cmake --build build --target crusades_campaign_test
ctest --test-dir build -R '^crusades_campaign$' --output-on-failure
```

The `tak-crusades` static library has no dependency on `tak-formats`, SDL or
third-party parsers. Its test therefore also builds directly from the two C++
sources, independently of the rest of the engine.

| Plan acceptance criterion | Model boundary / check |
|---|---|
| Deterministic definition loading | Sorted territory IDs and neighbor lists; declaration-order equivalence |
| Territory references validate | Every property and neighbor resolves to a declared territory |
| Duplicate IDs fail | Duplicate declarations are rejected rather than overwritten |
| Invalid adjacency fails | Self, duplicate, unknown and nonreciprocal edges are rejected |
| Unknown maps fail where required | Explicit resolver policy validates supplied maps; required-map policy rejects incomplete definitions |
| Definition and live state remain separate | Const definition API and independent state instances; owner/map mutation cannot rewrite definition |
| No campaign state enters `World::stateHash()` | Model is a separate library with no RTS simulation integration |

Validation on 2026-09-30:

- Full Release and Debug builds succeeded; all **128 Release** and **133 Debug**
  CTests passed, including the new model test's **98 checks**.
- Standalone GCC and Clang builds passed with `-Wall -Wextra -Werror`; the Clang
  run also passed AddressSanitizer and UndefinedBehaviorSanitizer.
- A separate libFuzzer/ASan/UBSan run exercised **1,871,385 inputs** in 41 seconds
  with no finding. This is bounded robustness coverage, not proof of all inputs.
- MinGW cross-compiled the model and test for Windows with warnings treated as
  errors. This is compile coverage; the Windows executable was not run here.
- The model and test link only to the new static library and the C++ runtime.
  No changes were made to simulation, AI, network protocol or tactical hashes.

This milestone supplies an in-memory model only. Saving campaign state,
authoritative campaign operations, accounts, battle credit and integration into
a running game remain separate milestones.
