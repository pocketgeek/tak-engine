# Darien Crusades balance research

This directory records where the Crusades balance data came from and how it
was checked: the archaeology of the Crusades patch, the native-reader checks
and the source-scoped balance audit behind the lobby's CRUSADES option. The
engine's current Crusades rules, and the corrections these findings led to, are
in the [balance reference](../../crusades-balance-reference.md).

The Darien Crusades meta game (the strategic map, campaigns, allegiance,
matchmaking and the campaign service) was removed in 0.9.4. Its design and
research notes were removed with it; release notes and history entries still
mention them.

- [Crusades Balance audit](../../crusades-balance-reference.md)
- [Native balance field findings](balance-field-review.md)
- [Balance source reports and effective registries](balance/)
- [Crusades versus the smaller 3.0 update](crusades-vs-3.0.md)
- [Local Iron Plague-era installation comparison](crusades-vs-iron-plague.md)
- [Patch difference report](patch-diff.md)
- [Original CD and GOG distribution provenance](distribution-provenance.md)
- [Sources and fingerprints](sources.md)
- [Reusable provenance template](source-template.md)
- [Installer inventory and extraction](installer-inventory.md)
- [Machine-readable inventories](inventories/)

All committed artifacts are metadata or original research notes. Original
installers, extracted files, scripts, artwork and game data stay outside Git.
The research does not launch installers or the retail game. Native blocks are
checked in isolated emulation. Proven balance-reader and combat mismatches were
corrected in the engine; these changes and their validation are recorded in the
[balance reference](../../crusades-balance-reference.md).

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

## Validation

The inventory tests cover hashes, changed/added/removed files, case-only renames,
case-fold collisions, archive identity, filter disclosure, symlinks, missing
sources, invalid manifests, output/input isolation, malformed HPI listings and
an actual synthetic HPI pack/read round trip. The checked-in inventories
reproduce the checked-in difference report exactly.
