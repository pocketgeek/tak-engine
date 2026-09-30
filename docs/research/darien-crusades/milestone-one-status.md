# Milestone 1 acceptance audit

Goal activated 2026-09-30. **In progress**, not complete. This tracks the seven
acceptance criteria in [the plan](../../darien-crusades-plan.md).

| Criterion | Current evidence | Outstanding work |
|---|---|---|
| 1. Package additions | Fingerprinted standard/Crusades installers, extracted payload inventories and member diff | Installer conditions and original Iron Plague baseline provenance |
| 2. Engine metagame knowledge | Territory definition/replay loaders, recon fields, battle settings, race restrictions, balance selection and reporting calls | Connect territory selection to battle entry; finish launch/result event gating |
| 3. Boneyards responsibilities | Shipped FAQ assigns campaign calculations to servers; Rover message construction, queueing and socket path traced | Incoming callbacks, acceptance/finalization and explicit limits of recoverable server authority |
| 4. Territory/battle data | 313 validated parcels; map/settings properties; score-report schema | Adjacency source and map-to-territory data provenance; no invented graph |
| 5. Fatigue/support/toughness | Primary documentation for side resistance, neighbor influence, time erosion, traffic and momentum | Audit surviving parameters/code; retain exact arithmetic as UNKNOWN unless recovered |
| 6. Evidence classification | Maintained evidence matrix with primary source IDs, binary fingerprints and offsets | Consolidated final answers and unresolved historical limits |
| 7. Exact Crusades Balance | Existing loader selects unitscb/canbuildcb before ordinary definitions; retail branch corroborated | Exhaustive effective-data diff, per-field loader coverage and meaningful regression tests |

## Current work order

1. Finish documenting the transport trace and distinguish local send from server
   acceptance. Isolated native codec checks already pass.
2. Audit effective base/Crusades data through retail VFS precedence, generate the
   complete gameplay difference, and verify the engine's interpretation. Use
   existing HPI/TDF tools and parsers; do not commit original data.
3. Close the remaining campaign-flow, provenance and parameter investigations.
4. Review all seven answers against the evidence matrix. Unknown historical
   server formulas may remain explicitly UNKNOWN after investigation; a guessed
   formula is never an acceptance substitute. The balance audit may not be
   waved through as an unknown without examining the surviving shipped data.

No production campaign networking, persistence, or campaign database belongs in
this goal. Any necessary fix to existing balance loading must retain normal
simulation validation and rebuild requirements.
