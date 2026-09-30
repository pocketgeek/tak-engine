# Territory definition and companion tables

This continues Milestone 1; it does not introduce campaign rules. Source IDs and
package provenance are in [sources.md](sources.md). The aggregate report is
[territory-summary.json](inventories/territory-summary.json).

## Darien.def: CONFIRMED file observations

DARIEN-DEF is 171,051 bytes, SHA-256
`6241e8c2fd18c01112c4f1ff177554d5fdffac3acec32bbf7abe10a6d4aa97b1`.
Both extracted patch payloads contain identical bytes.

The text uses tagged DarienMap records, with string and integer fields. Parsing
as CP1252 preserves the observed accented text. This is an observed encoding,
not a claim that every Boneyards file uses it. One description spans two physical
lines, so a line-at-a-time record parser would lose data.

There is one world header, then 313 parcel records. Names and `chatareaid` values
are unique. IDs range from 8193 to 8506 and are not a contiguous 313-element
range. The header declares 313 parcels and 871 borders, but **there are no edge
records or neighbor lists in this file**. The border count is not sufficient to
construct an adjacency graph.

| Native race label | Parcels |
|---|---:|
| Aramon | 93 |
| Taros | 67 |
| Veruna | 49 |
| Zhon | 72 |
| Neutral | 32 |

These are native-race labels, not current ownership. Every observed `nativeicon`
is zero. The seven terrain labels describe 93 Coastal, 47 Forest, 18 Hills,
44 Islands, 47 Mountains, 40 Plains and 24 Urban parcels.

The header literally says width 1083 and height 1672. Fire anchors range up to
x=1649 and y=1073. The local mixed installation's strategic PNGs are 1672 pixels
wide by 1083 high. Thus blindly treating header width/height as ordinary image
axes would reject valid anchors. The report retains the original labels; the
later [native geometry trace](territory-parameters.md) follows seed-based image
fill and hit-test lookup without treating these labels as an adjacency schema.
The strategic PNGs now match the independently fingerprinted GOG distribution;
their original patch/CD delivery route remains unresolved.

## Companion files: CONFIRMED bytes, RECONSTRUCTED reader behavior

Both patch payloads contain these identical files:

| File | Bytes | SHA-256 |
|---|---:|---|
| PreInit.jje | 313 | `147426400dd3d01f51f9108b678715d092ff9bb590f6d02916bf590bf5b1a8b8` |
| wdhit.jje | 257 | `4776955ff63017de2e88f15698682a29f7b0fc456debe3a686ea5b75ddc31222` |

PreInit contains 119 H, 165 T and 29 C bytes, with no whitespace. The traced
reader consumes one byte per ordered territory entry and maps C/H/T to 0/1/2.
The callback keys that tree by `chatareaid`, with unsigned ascending comparison.
Consequently file position follows ascending IDs, not necessarily definition
record order. This is a reconstruction from static code, not a live-server
experiment. It does **not** establish that these are the authoritative starting
owners of every historical campaign. The path appears in replay/movie-related
code; the exact live-campaign relationship remains UNKNOWN.

wdhit contains 80 whitespace-separated decimal integers (50 ones, 30 twos).
Its traced reader scans decimal values into a vector near crest sprite setup.
It is not evidence of a fatigue/support script. The precise meaning of each
entry is still UNKNOWN. See [binary notes](binary-notes.md) for trace addresses.

## Reproduce

```sh
python3 tools/re/crusades_territories.py /path/to/Boneyards/Metagame/Darien.def
python3 -m unittest discover -s tools/re -p 'crusades_territories_test.py' -v
```

The research parser validates the observed field sets and types, unique IDs and
names, header position and parcel count. Unknown fields/commands fail explicitly
so a new file variant cannot silently masquerade as this format. Output contains
aggregate metadata and a source hash, never descriptions or a territory list.
The tests use synthetic data. This is not an engine asset loader.
