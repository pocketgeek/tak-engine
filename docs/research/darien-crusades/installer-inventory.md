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
| TNT terrain | `tnttool` | All 181 added TNT files parsed |
| OTA/FBI/TDF/TSF | `tdftool`, `tnttool` | All added OTA, one TDF and two TSF parsed; three OTA byte round trips differ |
| Boneyards GUI | Existing `src/gui/gui.cpp` numeric parser | All 20 added GUI files parsed to EOF; [numeric GUI validation](numeric-gui.md) |
| GAF sprite banks | `gaftool` (requires palette) | All 31 added GAF files loaded with frame decoding |
| COB / 3DO | `cobtool` / `modeltool` | None added in the standard/Crusades package difference |
| BIK movie chunks | `biktool` | Five chunks assemble one 1,110-frame movie; all joined frames decoded |
| Darien `.def`, `.jje`, `.mmz` | Research parser / reader trace / ZIP inspection | Definition parsed; companion integer/byte tables traced; MMZ updater manifest inspected |
| HTML/PNG/PCX/text | Text inspection / Pillow | All 60 added PNG/PCX decoded; no proprietary content committed |

`Darien.def` is not assumed to be ordinary TDF merely because of its extension.
Its parcel declarations use a different tagged representation. The strict
[territory parser](territory-format.md) validates its complete observed schema.
The later [added-asset sweep](asset-parse-sweep.md) supersedes the initial parser
coverage and explains the existing numeric GUI parser and movie assembly.

## Older FTP packages

The expanded [source survey](sources.md#expanded-cavedog-ftp-survey) covers 36
selected artifacts. The [metadata inventory](inventories/cavedog-ftp-survey.json)
separates complete loose payload extraction from archive member decoding:

- Fourteen additional Wise packages extracted with the pinned WiseUnpacker.
- Eleven ZIPs and two ZIP SFX executables extracted without running them.
  `update.EXE` and `clash.exe` are complete embedded ZIP containers according
  to 7-Zip, unlike the partial ZIP found inside the Wise standard patch.
- Four loose files hashed as downloaded.
- Two previously inventoried standard/Crusades patches referenced by their
  existing inventories instead of duplicating those entries.
- Three RTPatch executables inspected as PE sections/resources only:
  `BYKMaia20.exe`, `v1x-345by.exe`, and `non-intelupdate.exe`. Their delta output
  files were not reconstructed. Successful 7-Zip resource extraction does not
  make them complete patch inventories.

All selected packages have source hashes. The new extraction inventory contains
3,270 loose/resource/member records, excluding the two previous inventories.
All Kingdoms HPI/UFO/KMP members encountered in this pass were read and hashed.
Eighteen original TA v1 HPI/UFO containers were rejected by the existing TAK
v2 archive reader; each failure is recorded under `archive_checks`, and those
containers are hashed only as loose files. They comprise the two art/audio HPI
files in `update.EXE` and four unit UFOs in each of four original TA Boneyards
packages. No parser success or content review is claimed for these containers.

`complete_loose_payload_inventory` refers to extractor output, not installed
state, execution coverage or successful decoding of every nested archive.
The thirteen MMZ members were read with Python `zipfile`; metadata records
their decoded sizes, hashes, package declarations, locators, bundle names and
resource paths. Original manifest/help prose stays outside Git.
