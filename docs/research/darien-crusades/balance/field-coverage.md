# Exhaustive changed-field coverage

`field-coverage.tsv` contains one row for every distinct field path appearing in
`gog.tsv`, `official30.tsv`, or `iron-plague-cd.tsv`: **177 paths**. Its three
count columns count report rows, including added/removed menu definitions;
they are not counts of independently loaded unit types. The union and per-scope
counts were mechanically recomputed from the TSVs. The classifications were
then reviewed against `tools/crusades_registry_test.cpp`, production readers,
and [native key review](../balance-field-review.md).

The table deliberately distinguishes:

- `gameplay-checked`: a source-to-registry contract exists in the retail-backed
  test for the loaded value/default or effective derived result. This includes
  roster fallback, complete menu ordering, inherited movement properties,
  weapon dispatch and category damage against actual target types.
- `presentation-checked`: the registry test checks the source reference/value,
  but does not establish pixel-perfect rendering, sound output, or asset identity.
- `presentation-consumer-checked`: an authored weapon name is loaded correctly
  and its engine status/visual consumers are considered. A name is **not**
  automatically cosmetic just because the public report redacts its prose.
- `presentation-only`: unit display text and authored button-image references;
  no simulation behavior depends on these changed fields. The current HUD
  chooses weapon icons from names rather than reading these button-image keys;
  this audit does not claim native equivalence of all UI artwork.
- `authored-key-ignored`: an apparent source change has no native runtime
  meaning in that scope/spelling. A simultaneous omission of the correctly
  spelled key can still change its effective default: the two deer switch from
  standard water multiplier0.81 to Crusades1.0, rather than using the typo.
  Native reader evidence is required; absence
  from a text search alone is insufficient. See the cited review for the four
  keys, including the corrected `watermultipliser` compatibility behavior.

This is coverage of the **effective loaded rules**, not a claim every textual
FBI file defines a separate unit. The test verifies canonical roster IDs and
aliases, reads the winning definition for each mode, and validates the complete
active weapon roster. A noncanonical duplicate and a damage-zero inactive
weapon do not create an extra active game rule merely because their text differs.
Original field rows remain visible in the reports, including those shadowed
source declarations; the report never discards them to obtain coverage.

## Names and downstream behavior

All fifteen changed name rows in the GOG report were inspected from their
winning original source members, without publishing the prose. None introduces
a name-only paralyzer status. The Priest's second/third weapons swap stone
status through the explicit subtype, and the registry test checks that dispatch.
The one changed unit name (`verscout`) is display text.

There is one name-dependent visual classification: Crusades `vercen` weapon 1
becomes the engine's `Fire` visual family, whereas its standard counterpart is
`Arrow`. Removing the name from the current inference would leave the Crusades
classification at the default `Arrow`. The test explicitly records this
consumer; it is **not** claimed as native-proven rendering logic. Both authored
weapon data and renderer flow were checked: Crusades specifies model `araarrow2`,
and the model branch draws that mesh then skips the fallback particle drawing.
No authored `lightmap` enables the earlier family-tinted ground-light branch.
The changed name therefore does not turn this authored arrow into fallback flame
particles in the current renderer.

## Recheck the inventory boundary

From the repository root, the following metadata-only check verifies that no
field or occurrence disappeared from this coverage table:

```sh
python3 - <<'PY'
import csv
from collections import Counter
from pathlib import Path
root = Path('docs/research/darien-crusades/balance')
coverage = list(csv.DictReader((root/'field-coverage.tsv').open(), delimiter='\t'))
assert len(coverage) == len({r['field'] for r in coverage})
expected_union = set()
for scope, column in [('gog','gog_rows'), ('official30','official30_rows'),
                      ('iron-plague-cd','iron_plague_cd_rows')]:
    expected = Counter(r['field'] for r in csv.DictReader(
        (root/(scope+'.tsv')).open(), delimiter='\t'))
    expected_union.update(expected)
    assert {r['field']: int(r[column]) for r in coverage if int(r[column])} == dict(expected)
assert {r['field'] for r in coverage} == expected_union
assert not any(r['classification'] == 'missing-coverage' for r in coverage)
print(len(coverage), 'field paths accounted for')
PY
```

This check establishes table completeness only. Actual registry test runs and
native experiments are the evidence for the classifications; regenerating a
manifest or seeing green metadata checks does not prove gameplay correctness.
