# Darien Crusades research

Status: the plan's **first actionable task** is complete. The full historical
archaeology milestone remains open; there is no campaign implementation here.

- [Sources and fingerprints](sources.md)
- [Reusable provenance template](source-template.md)
- [Installer inventory and extraction](installer-inventory.md)
- [Crusades versus the smaller 3.0 update](crusades-vs-3.0.md)
- [Local Iron Plague-era installation comparison](crusades-vs-iron-plague.md)
- [Evidence matrix](evidence-matrix.md)
- [Territory definition and companion tables](territory-format.md)
- [Updater manifest](updater-manifest.md)
- [Binary trace notes](binary-notes.md)
- [Campaign rules recovered from shipped help](campaign-rules-evidence.md)
- [Machine-readable inventories](inventories/)

All committed artifacts are metadata or original research notes. Original
installers, extracted files, scripts, artwork and game data stay outside Git.
The research does not launch installers or the game. Nothing was added to
production networking, campaign storage, balance rules, or `World::stateHash()`.

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

## Next research steps

1. Decode installer conditions and updater precedence. The MMZ updater-manifest
   role is established, but it does not prove which files an installer writes.
2. Recover Darien definition semantics, particularly the borders/parcel data;
   field names alone do not prove battle eligibility or a territory graph.
3. Trace the patched engine and Boneyards DLL paths using their recorded hashes.
4. Recover fatigue/support/toughness arithmetic and result protocol. Shipped
   help now documents server calculations, momentum history and deferred
   results, but supplies neither exact formulas nor the wire contract.
5. Complete the per-field Crusades Balance audit independently of the metagame.

## Validation

All 146 Python research tests passed, including 13 inventory/comparison
cases and 10 territory-parser cases. These cover hashes, changed/added/removed files, case-only renames,
case-fold collisions, archive identity, filter disclosure, symlinks, missing
sources, invalid manifests, output/input isolation, malformed HPI listings and
an actual synthetic HPI pack/read round trip. The checked-in inventories
reproduce the checked-in difference report exactly. The territory parser also
validated the fingerprinted definition and reproduced the aggregate report.
Its synthetic tests cover multiline and CP1252 text, omitted prose, malformed
fields, types, counts, dimensions, duplicate identities and trailing garbage.

No engine rebuild or gameplay sweep was required: only research Python and
Markdown/metadata changed.
