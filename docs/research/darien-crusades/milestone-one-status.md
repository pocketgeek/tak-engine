# Milestone 1 acceptance audit

Goal activated and completed 2026-09-30. **Milestone 1 complete.** This tracks the seven
acceptance criteria and the work sections in [the plan](../../darien-crusades-plan.md).
A documented historical unknown is different from surviving data that has not
yet been checked. No production campaign service is required by this milestone.

## Seven acceptance answers and their present limits

| Criterion | Evidence-backed answer | Remaining completion work / limit |
|---|---|---|
| 1. Package additions | Standard/Crusades payload inventories and member diff identify shared executable/Boneyards bytes and added maps, UI/art, and five chunks of one movie. Original Iron Plague disc and later GOG comparisons now have independent byte evidence. [Package diff](patch-diff.md), [distribution provenance](distribution-provenance.md), sources PATCH-STANDARD/PATCH-CRUSADES/IRON-PLAGUE-CD/GOG-2.0.0.22 | Final comparisons and source-scoped roots are reconciled. Extracted payloads do not prove installer branch selection or historical online update order; 181 packaged maps versus advertised 182 remains explicitly unresolved |
| 2. Engine metagame knowledge | The client knows parcel IDs/presentation, ownership/status displays, runtime map settings, side permissions and campaign balance. Selection enters Boneyards areas; host/join consumes a typed launch callback, then reports tactical player results. [Campaign flow](campaign-flow.md), [battle contract](battle-contract.md), EXE-3/ROVER | Static transition now reaches session create/join and shared tactical staging; session/password consumption is documented. No live historical connection or first-combat-tick claim |
| 3. Boneyards responsibilities | FAQ assigns campaign calculations to servers; Rover constructs requests, receives launch settings, queues/encodes/sends score reports and dispatches matching incoming report notifications. [Flow](campaign-flow.md), [transport](report-transport.md), HELP-147 | The local success boundary is known. Authoritative score acceptance, deduplication, dispute resolution and durable campaign mutation remain UNKNOWN; these are not recovered by a client enqueue/send result |
| 4. Territory/battle data | Both original territory definitions describe 313 parcels; runtime properties associate maps/settings with IDs. Image flood fill supplies rendering/hit testing, not a neighbor graph. [Territory format](territory-format.md), [parameters/geometry](territory-parameters.md), [provenance](distribution-provenance.md) | No authoritative adjacency or complete historical map-assignment table was found in the audited sources. Do not invent one from names, artwork or the header's border count |
| 5. Fatigue/support/toughness | Active recon templates identify fatigue VPs, side-specific support VPs and required VPs. They explain momentum plus combined fatigue/support/battle VPs exceeding the required amount. [Rules](campaign-rules-evidence.md), [parameter trace](territory-parameters.md), RECON-HISTORY/RECON-0/README-3 | Meanings and documented predicate are recovered; parameter generation, rounding, capture ties and battle-free momentum behavior remain UNKNOWN at the server boundary |
| 6. Evidence classification | [Evidence matrix](evidence-matrix.md) distinguishes file facts, native code reconstruction, documentation claims, modern proposals and unknown rules, with fingerprints/addresses | Final consistency and local-link review passed; earlier progress observations are labeled and linked to subsequent findings |
| 7. Exact Crusades Balance | Three source-scoped raw/effective reports, all 177 changed field paths classified, native corrections and complete registry/menu coverage are verified. [Balance reference](../../crusades-balance-reference.md) | Complete within the changed-rule scope: wind, damage and water-key corrections passed full builds, 127 CTests, native probes and final registry checks; see [validation](milestone-one-validation.md) |

## Work-section audit

| Plan section | Evidence inspected | Status |
|---|---|---|
| 1.1 Acquire/fingerprint | Source ledger, standard/Crusades downloads, original Iron Plague BIN/derived ISO, cabinet and selected GOG inventory | Core artifacts obtained and fingerprinted; each source's verification limits are explicit |
| 1.2 Unpack/inventory/compare | Standard, Crusades, CD, cabinet, local and selected GOG inventories; package and member comparisons | Original-media and later-package comparisons reconciled; selection/order limits remain explicit |
| 1.3 Parse/search data | Strict territory parser; complete added-asset [parse sweep](asset-parse-sweep.md); source-specific balance audits; template and companion-table inspection | All added maps/sprites/images/movie and all 20 numeric GUI files checked with their format-appropriate readers; GUI records parse to EOF. Full native UI rendering semantics are outside this structural parse claim |
| 1.4 Strings/resources | Fingerprinted EXE/ROVER addresses in binary, parameter, battle, transport and flow notes; primary HTML bindings | Campaign anchors traced rather than used as standalone rule evidence; no original disassembly committed |
| 1.5 Campaign code paths | Selection → area → host/join → launch callback/settings → result event gates → score construction → queue/serialization/socket send | Session create/join continuation traced; server policy and untested live lifecycle explicitly bounded |
| 1.6 Exact balance | Generated source field/menu reports and effective registry checks | Complete: raw/effective reports, field classifications, corrected readers/lookup and regression validation |
| 1.7 Archived public material | Cavedog patch/news pages, preserved original downloads, shipped FAQ/quickstart/readmes/templates, preserved media and supplementary contemporary material in sources.md | Primary sources provide cited behavior. Failed or irrelevant searches are not evidence; beta documentation is not silently promoted to final-service implementation |
| 1.8 Evidence matrix | E01–E47 with sources, code locations, parse/decode checks and limitations | Final source, address, document-link and classification cross-check passed |

## Completion evidence

The [validation report](milestone-one-validation.md) records successful full
Release/Debug builds, all 127 CTests, 151 Python research tests, the expanded
registry runs on all three source scopes, native experiments, repeated
client/server smoke checks and byte-for-byte regeneration of all nine balance
reports. Every explicit work section and all seven acceptance answers above
were reviewed against the actual files, not inferred from test counts alone.

No accessible Milestone 1 investigation or required correction remains open.
The following historical limits are findings of the archaeology, not completed
implementations of unavailable server behavior.

## Bounded historical unknowns

The original server's adjacency graph, authoritative map-assignment tables,
fatigue/support/toughness parameter generation, rank weights, result validation,
orphan-result persistence and simultaneous-capture ordering have not been
recovered. Neither a zero HTML fallback nor a plausible modern formula resolves
these gaps. They remain UNKNOWN with an identified evidence boundary, rather
than an instruction to implement a replacement service during Milestone 1.

The full native GUI rendering semantics, installer conditions, original strategic-PNG delivery
route and the advertised-map-count discrepancy also remain limited by the
present source analysis. Those limits must stay visible; no claim is made that
every original installer action or UI serialization detail has been reproduced.

No production campaign networking, persistence or campaign database belongs in
this goal. Milestone 1 completion does not authorize or imply implementation of those later milestones.
