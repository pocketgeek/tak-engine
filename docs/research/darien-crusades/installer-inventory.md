# Installer extraction and inventories

Observed on 2026-09-30. Source identities are in [sources.md](sources.md).

Both downloads are 16-bit NE Windows executables containing Wise payloads.
They were **not executed**. 7-Zip 26.02 could not open Crusades as a complete
archive and found only an embedded ZIP in the smaller update. Do not mistake that
partial ZIP for the entire package. REWise at
`c3d3b68903a90ec53ff7b0a4ae704adc6302814b` rejected both with an advertised-DIB-size
error. Neither failure was treated as a successful inventory.

WiseUnpacker successfully extracted both packages to new, separate temporary
directories. See the pinned binary hash in the source ledger; this external
research dependency is not linked into or shipped with TAK-Engine.

```sh
WiseUnpacker -x -o=/tmp/tak-darien-research/standard /tmp/tak-darien-research/downloads/TAK30BBupdate.exe
WiseUnpacker -x -o=/tmp/tak-darien-research/crusades /tmp/tak-darien-research/downloads/Crusades.exe
```

The extractor names destination placeholders such as `MAINDIR`, `INST`, and
`TEMP`; it also emits installer helper files and `WiseScript.bin`. Inventories
retain these names instead of pretending that the installer was run. File data
was read successfully through the extractor and `hpitool`; installer branch
coverage and the effects of applying either patch are **not** established.

| Inventory | Loose payload files | Expanded archive members | Total records |
|---|---:|---:|---:|
| Standard 3.0 update | 147 | 1,263 | 1,410 |
| Crusades update | 329 | 2,102 | 2,431 |
| Selected local mixed install | 194 | 2,770 | 2,964 |

No symlinks were skipped in these runs. The local inventory has a narrower,
explicit filter scope, so it is not directly comparable as a complete distribution.

The Crusades payload contains 181 `.kmp` archives. Each successfully lists and
reads four members: CRT, OTA, TNT and TXT. This differs from the preserved patch
page's advertised 182 maps. The discrepancy remains unresolved; no missing map
was invented. `meta.hpi` contains 115 members. All five HPI files present in both
patches are byte-identical, as are their 1,263 decoded members.

## Existing parser coverage

| Observed format | Existing tooling | This pass |
|---|---|---|
| HPI/KMP containers | `hpitool` | Every member in both patch inventories read and hashed |
| TNT terrain | `tnttool` | Candidate; representative added map checked |
| OTA/FBI/TDF | `tdftool` | 181 added OTA and one TDF passed |
| Boneyards GUI | `tdftool` candidate rejected | All 20 added numeric-text GUI files failed; separate format needed |
| GAF sprite banks | `gaftool` (requires palette) | Candidate; no new image exports committed |
| COB / 3DO | `cobtool` / `modeltool` | Candidate mapping; no execution needed for inventory |
| BIK movies | `biktool` | Candidate; no movie playback required |
| Darien `.def`, `.jje`, `.tsf`, `.mmz` | No confirmed dedicated parser | Preserve hashes; semantics need research |
| HTML/PNG/PCX/text | General inspection / existing asset loaders | No proprietary content committed |

`Darien.def` is not assumed to be ordinary TDF merely because of its extension.
Its parcel declarations use a different tagged representation. Counts in the
evidence matrix come from direct token inspection, not a complete parser.
