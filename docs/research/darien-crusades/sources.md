# Sources and fingerprints

Survey/retrieval date: **2026-09-30 UTC**. No original assets are included here.
Use [the template](source-template.md) when adding sources.

## Web provenance

**WEB-PATCH** — [Preserved Cavedog TA:K patch page](https://zx.net.nz/mirror/www.cavedog.com/ta-kingdoms/patch.html).
Publisher-authored page preserved by a third-party mirror, not a currently
publisher-operated site. It links `TAK30BBupdate.exe` as the update without
Darien Crusades and `Crusades.exe` as the version including it; the page advertises
182 maps. Retrieved HTML: 14,540 bytes; SHA-256
`0a9e9914958722a7d53e2440b423dbcac3ecb4f625d4ee70353b558de4b858d9`.
Original publication date and exact mirror snapshot date remain unverified.
The Wayback URL found through historical references was inaccessible during this
pass; it was not used as inspected evidence.

**FTP-MIRROR** — [Preserved ftp.cavedog.com directory](https://ftp.zx.net.nz/pub/archive/ftp.cavedog.com/cavedog/).
Direct download URLs and observed fingerprints follow. Its displayed 2005 file
timestamps are mirror metadata, not claimed original release dates.

**HASH-CROSSCHECK** — [ModDB Crusades download listing](https://www.moddb.com/games/total-annihilation-kingdoms-the-iron-plague/downloads/ta-kingdoms-v30-patch-darien-crusades).
Third-party listing added 2016-09-14. Its advertised byte size and MD5 for
`Crusades.exe` match our FTP-mirror download. This corroborates byte identity
across listings, not a publisher signature. No independent published standard-
update digest was verified in this pass.

## Downloaded distributions

| ID | Artifact / direct source | Bytes | Observed SHA-256 |
|---|---|---:|---|
| PATCH-CRUSADES | [Crusades.exe](https://ftp.zx.net.nz/pub/archive/ftp.cavedog.com/cavedog/Crusades.exe) | 32,096,428 | `895de2d8a869a1c4941d9325bed5521c8e5836075bf5a027af749e20b44b030e` |
| PATCH-STANDARD | [TAK30BBupdate.exe](https://ftp.zx.net.nz/pub/archive/ftp.cavedog.com/cavedog/TAK30BBupdate.exe) | 6,073,053 | `a0ad5f5d8cec9b2ce340c60498d20e308e0eeb02679351488ea18dd73c0bd5b0` |

Observed MD5s: Crusades `71e17a18fc41ae745cabf77e9bcd03c6` (independently
matched); standard `9e93a1956c52e8086c52b71383576070` (local measurement only).
No publisher cryptographic signature was authenticated.

Raw downloads and extracted payloads are retained locally under the ignored
`assets/research/darien-crusades/` directory, in `downloads/`, `standard/`, and
`crusades/`. Extraction logs and scratch output are in `/tmp/tak-darien-research/`.
No original payload is tracked in Git; the URLs and fingerprints allow retrieval.

## Extracted primary evidence

These paths are relative to each extractor output. Unless noted, both patch
packages contain identical bytes. Full hashes and membership are in the
[inventories](inventories/).

| ID | Path | Bytes | SHA-256 |
|---|---|---:|---|
| EXE-3 | `MAINDIR/KINGDOMS.icd` | 2,277,500 | `6ddc7fae0cbf3ee13d530614b64608a3aeefca7deb73719310a4f68fe1f22e96` |
| DARIEN-DEF | `MAINDIR/Boneyards/Metagame/Darien.def` | 171,051 | `6241e8c2fd18c01112c4f1ff177554d5fdffac3acec32bbf7abe10a6d4aa97b1` |
| BYMAIA | `MAINDIR/bymaia.dll` | 741,376 | `a4741b677c22b33e29c0226c09a604f9037f3d8252c0eb472c4b31cf72390427` |
| ROVER | `MAINDIR/Rover.dll` | 200,750 | `ffedddf9b615e7a60303231c98541ab0147d55fd8d2e1de510272dff0481ffd6` |
| README-3 | `MAINDIR/v3readme.txt` | 7,113 | `5a3cfd8f20937e19ad265ed9a6255e9a8a581cb15300af926c555c65faa8d43a` |

`README-3` lines 5–7 discuss Crusades activity, territorial fatigue/support/
toughness, and the balance selector. Its descriptive statements are primary
historical evidence, but not a recovered implementation of the rule arithmetic.

## Local installation

**LOCAL-MIXED** — existing user-supplied `assets/game`, containing Iron Plague,
Boneyards, and modified/runtime material. Acquisition date and exact package
lineage are UNKNOWN. The selected local inventory records its explicit scope.
It is not evidence for an untouched Iron Plague CD or standard-3.0 installation.

Its `KINGDOMS.icd` is 2,277,500 bytes but hashes to
`1144a394889811ae6d113f8fe470dac5de0b0a7aae3c9ee063f74b8d20b730db`, distinct
from EXE-3. This pass does not identify the cause of the difference. Local
`bymaia.dll`, `Rover.dll`, and `Boneyards/Metagame/Darien.def` match the patch
payload bytes. Original Iron Plague media comparisons remain outstanding.

## Extraction tooling

[WiseUnpacker](https://github.com/mnadareski/WiseUnpacker), rolling-release
Linux x64 .NET 10 standalone archive, fetched 2026-09-30. Rolling tag resolved to
`3608eca0b9c105e01b60be2f45788395a2117265`; rolling tags can move, so pin the
hashes when reproducing:

- `WiseUnpacker_net10.0_linux-x64_release.zip`: SHA-256
  `753c260838dd2ec3665f2c9ae0025c891c95f914d8b7792e7860d386bc71ad8a`.
- Extractor executable: SHA-256
  `ccaa2b0eed917f194f2f991c3c0e5ed75dd32690b14a3223d1e8234d7880c0a5`.

`hpitool`/`tdftool`/`tnttool` were the existing 0.7.17 local builds. Inventory and
comparison use `tools/re/crusades_inventory.py` in this research commit. No
extractor or proprietary payload is vendored or added to the engine build.
