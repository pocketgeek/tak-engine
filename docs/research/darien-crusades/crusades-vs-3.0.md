# Crusades versus standard 3.0 patch payload

This comparison is between the downloaded **patch payloads**, not two clean
installed game trees. [Sources](sources.md) identify both inputs; reproduce with
[the inventory tool](README.md). Case-insensitive container/member identity is
used, and both sides expand `.hpi` and `.kmp` archives.

**1,401 records match**, **3 changed**, **1,027 were added**, and **6 were removed**
on the Crusades side. Counts include loose containers and their decoded members;
they must not be described as counts of installed files.
See the [complete generated difference report](patch-diff.md).

## Changed records

| Path | Scope of observation |
|---|---|
| `CustomDialogSet_DISPLAY-Select Destination Directory` | Extracted installer dialog differs |
| `WiseScript.bin` | Extracted installer script differs; branches not interpreted |
| `MAINDIR/kingdoms.mmz` | Payload bytes differ; format/function not established here |

`KINGDOMS.icd`, `Kingdoms.exe`, `bymaia.dll`, `Rover.dll`, `Jersey.hpi`,
`V2Rocket.hpi`, `V3Rocket.hpi`, `boneyards.hpi`, `boneyards2.hpi`, and
`Boneyards/Metagame/Darien.def` are byte-identical between these extracted
packages. Thus the smaller download is not a pristine pre-Crusades engine
baseline. Presence in the larger package alone cannot establish when a code
path was first added.

## Added and removed groups

The Crusades payload adds:

- 181 KMP map containers and 724 decoded CRT/OTA/TNT/TXT members.
- `meta.hpi` and its 115 decoded members: 31 GAF, 31 PCX, 29 PNG, 20 GUI,
  two TSF, one BAT and one TDF.
- Five BIK movie files.
- One additional HTML file.

It omits six standalone unit readmes under `MAINDIR/Docs`:
`AFlyBuild.txt`, `ASiege.txt`, `TKamRat.txt`, `TRictus.txt`, `VLightHs.txt`,
and `ZSWolf.txt`. This does not prove that applying the patch deletes previously
installed readmes.

## Parser checks

All archive members were decoded and hashed using existing `hpitool`.
A follow-up pass checked 203 representative/all-added format candidates:
**181 OTA + one TDF + one TNT passed**. **All 20 added GUI files were rejected
by `tdftool`**, starting with its expected-section check. The observed Boneyards
GUI format is numeric text, unlike the TDF GUI syntax; these require separate
format research. Results are recorded in [parser-checks.json](inventories/parser-checks.json).
No parser failure was silently classified as successful coverage.

The patch page's advertised 182 maps versus 181 extracted map containers remains
an open discrepancy. We have not inferred an extra territory or map to close it.
