# Darien Crusades research

This directory records the Milestone 1 archaeology, native-reader verification
and source-scoped balance audit, plus the Milestone 2 campaign model and
Milestone 3 transactional store, Milestone 4 authenticated allegiance,
Milestone 5 battle issuance and Milestone 6 authoritative results. See the
[Milestone 1 acceptance audit](milestone-one-status.md) and
[Milestone 2 model](campaign-model.md), [Milestone 3 store](campaign-store.md)
and [Milestone 4 allegiance](campaign-allegiance.md)
for completion status and validation.
No historical campaign service has been implemented.

- [Milestone 1 acceptance audit](milestone-one-status.md)
- [Milestone 2 clean-room campaign model](campaign-model.md)
- [Milestone 3 transactional campaign store](campaign-store.md)
- [Milestone 4 authenticated campaign allegiance](campaign-allegiance.md)
- [Milestone 5 authoritative battle issuance](campaign-battles.md)
- [Milestone 6 authoritative match results](campaign-results.md)
- [Milestone 7 territory rules and evidence boundaries](campaign-territory-rules.md)
- [Crusades Balance audit](../../crusades-balance-reference.md)
- [Sources and fingerprints](sources.md)
- [Reusable provenance template](source-template.md)
- [Installer inventory and extraction](installer-inventory.md)
- [Original CD and GOG distribution provenance](distribution-provenance.md)
- [Added-asset parser sweep](asset-parse-sweep.md)
- [Numeric GUI parser validation](numeric-gui.md)
- [Crusades versus the smaller 3.0 update](crusades-vs-3.0.md)
- [Local Iron Plague-era installation comparison](crusades-vs-iron-plague.md)
- [Evidence matrix](evidence-matrix.md)
- [Territory definition and companion tables](territory-format.md)
- [Territory parameters and geometry](territory-parameters.md)
- [Updater manifest](updater-manifest.md)
- [Binary trace notes](binary-notes.md)
- [Campaign rules recovered from shipped help](campaign-rules-evidence.md)
- [Territory selection, battle entry and result lifecycle](campaign-flow.md)
- [Battle settings and score-report boundary](battle-contract.md)
- [Report queue, encoding and incoming reports](report-transport.md)
- [Native balance field findings](balance-field-review.md)
- [Machine-readable inventories](inventories/)

All committed artifacts are metadata or original research notes. Original
installers, extracted files, scripts, artwork and game data stay outside Git.
The research does not launch installers or the retail game. Native blocks are
checked in isolated emulation. Proven balance-reader and combat mismatches were
corrected in the engine; these changes and their validation are recorded in the
[balance reference](../../crusades-balance-reference.md). The later milestones add modern campaign storage and authenticated service
boundaries; they do not claim to reproduce the historical campaign-server protocol.

## Reproduce an inventory

Python 3.10+ and the existing `hpitool` are sufficient after extraction. This is
research tooling, not a new engine runtime dependency.

```sh
python3 tools/re/crusades_inventory.py inventory /path/to/standard-payload \
  --label 'Standard 3.0 patch payload' --hpitool build/hpitool \
  --archive-members '*.hpi' --archive-members '*.kmp' \
  --output /tmp/standard.json
python3 tools/re/crusades_inventory.py inventory /path/to/crusades-payload \
  --label 'Crusades patch payload' --hpitool build/hpitool \
  --archive-members '*.hpi' --archive-members '*.kmp' \
  --output /tmp/crusades.json
python3 tools/re/crusades_inventory.py compare /tmp/standard.json /tmp/crusades.json \
  --output /tmp/crusades-diff.json --markdown /tmp/crusades-diff.md
python3 -m unittest discover -s tools/re -p 'crusades_inventory_test.py' -v
```

An inventory accepts a single file (including an installer) or a directory.
Repeat `--include GLOB` to restrict a directory survey. Globs match whole relative
paths case-insensitively; `*` also spans `/`. No filter means every regular file.
The manifest records filters so comparisons can disclose different scopes.
Archive-member expansion is opt-in; matching archives are read with `hpitool
list/cat`, without writing their contents to disk. Container and member paths
remain separate. Raw archives and their decoded members have independent hashes.

The tool rejects ambiguous case-folded names, malformed member listings, changed
source files and outputs inside the source tree. It reports skipped symlinks;
it never follows them. Comparison preserves case-only renames separately from
byte changes. A parser candidate means a tool recognizes that format, not that
this particular asset has passed a semantic parse. These inventories do not
resolve mount precedence or prove the contents of a completed installation.

## Recovered boundaries and remaining historical unknowns

The package/member comparisons now include the original Iron Plague CD and a
fingerprinted GOG distribution. The added assets have a format-by-format parse
sweep. Territory selection, host/join, launch settings, tactical staging, report
serialization and result-event gates have native traces. Balance differences
have complete source reports and effective registry checks.

The surviving client does not establish historical server capture/ranking
formulas, authoritative result acceptance, deduplication or durable campaign
mutation. Territory hit-test imagery is not an adjacency graph. Extracted
installer payloads and updater manifests do not establish every installation
branch or the order of later online updates. These boundaries remain explicit
in the linked evidence; they are not requests to invent missing rules.

## Validation

The initial inventory/parser pass recorded 146 passing Python research tests,
including 13 inventory/comparison
cases and 10 territory-parser cases. These cover hashes, changed/added/removed files, case-only renames,
case-fold collisions, archive identity, filter disclosure, symlinks, missing
sources, invalid manifests, output/input isolation, malformed HPI listings and
an actual synthetic HPI pack/read round trip. The checked-in inventories
reproduce the checked-in difference report exactly. The territory parser also
validated the fingerprinted definition and reproduced the aggregate report.
Its synthetic tests cover multiline and CP1252 text, omitted prose, malformed
fields, types, counts, dimensions, duplicate identities and trailing garbage.

That initial pass changed only research tooling and metadata. Later native
balance findings required engine changes, rebuilds and gameplay validation;
consult the [current acceptance audit](milestone-one-status.md) and
[balance reference](../../crusades-balance-reference.md) for the final results.
