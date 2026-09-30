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
from EXE-3. The same executable bytes were subsequently recovered from the user-owned
GOG offline installer (GOG-2.0.0.22 below), establishing a packaged source for
the difference without attributing it to a local modification. Local
`bymaia.dll`, `Rover.dll`, and `Boneyards/Metagame/Darien.def` match the patch
payload bytes. Original Iron Plague CD data have now also been acquired and
compared; see the [provenance investigation](distribution-provenance.md).

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

## Shipped Crusades help (reviewed 2026-09-30)

These files occur byte-identically in both extracted official patch payloads,
under `MAINDIR/Boneyards/Help/`. Container provenance is PATCH-STANDARD/PATCH-CRUSADES
as identified above; consult the inventory paths if using a different checkout.
No HTML, screenshots or original page prose is committed.

| ID | File | Bytes | SHA-256 |
|---|---|---:|---|
| HELP-144 | Help144.htm | 5447 | `7853c86cf670e6d927020b95ec9008df8129e6af6bb8dab42b33b525edab1d06` |
| HELP-146 | Help146.htm | 3780 | `b0402b71681c32a1429b452b1f39a540ee0f4ad2f1cc8e46ea7cb113f23145e1` |
| HELP-147 | Help147.htm | 17753 | `f3297d825e040d405da60b64f5e9ec0100ac62e5aef63465d072c815abac96df` |

HELP-144 is an overview explicitly framed as the 1999-12-08 open beta.
HELP-146 is the quickstart. HELP-147 is a numbered FAQ, useful for stable
section references even when HTML line endings differ. They contain promises
of future features and are not evidence that every described rule was running
unchanged on the final service. See the [rules analysis](campaign-rules-evidence.md).
An internet search for the momentum/orphan-battle text found no useful
independent corroboration; unrelated search results were not used as evidence.


## Original Iron Plague CD and GOG corroboration

**IRON-PLAGUE-CD** — [English Thailand-release preservation item](https://archive.org/details/tak-iron-plague-thai-release),
retrieved 2026-09-30 from its `Images/IRON_PLAGUE.bin` download. The uploader's
regional attribution is unverified; the disc volume identifies Cavedog as
publisher. Size 715374912 bytes; SHA-256
`e5c4f6504e5151992f977f44a18788b47e85daedbd83f6ff0a935cb7b5abc82e`.
Observed SHA-1 `900bc863716c04c6247c898543342c6b70482553` matches the published
Archive file metadata. Its decoded MODE1 ISO SHA-1 also matches the independently
listed [Iron Plague ISO](https://archive.org/details/total-annihilation-kingdoms-iron-plague).
This is cross-listing byte corroboration, not publisher authentication.
Original BIN and extracted files remain ignored. Full measurements, extraction
method, tool revision, CD/CAB inventories and differences are recorded in
[distribution-provenance.md](distribution-provenance.md).

**GOG-2.0.0.22** — user-owned offline
`setup_total_annihilation_kingdoms_2.0.0.22.exe`, 959342944 bytes; SHA-256
`6d40e338887dc94d036c7858ce683637ada307a36fad0bc21c80729bf7bd570e`.
Found in local Downloads and inspected 2026-09-30; original download date and
independently published digest unknown. Innoextract 1.9 extracted a selected
40-file research scope without running the installer. All 40 files match the
corresponding current `assets/game` files. Main base/IP archives also match
original CD bytes. This does not reclassify the entire live tree as pristine.

**WEB-NEWS** — [Preserved Cavedog Kingdoms news page](https://zx.net.nz/mirror/www.cavedog.com/ta-kingdoms/news.html),
retrieved 2026-09-30; 6956 bytes; SHA-256
`d5a8a389ae51bdfbf7e56dfde66ebd1687edb53dec92374fedf5e7530ec31263`.
Publisher-authored promotional text preserved by a third party identifies Honor
and Terror and battles contributing to Darien's outcome. It corroborates the
campaign framing; it supplies no numeric campaign rule formulas. Snapshot date
is unknown. The page contains mirror-era script additions, so its digest pins
retrieved bytes rather than an authenticated original publisher file.

**RECON-HISTORY** — `MAINDIR/Boneyards/Profile/TAK_reconhistory.htm`, 6907
bytes; SHA-256 `c89738d776303f29ef40965a5d3feee168806891ca7344dc421a9bccc336b948`.
**RECON-0** — sibling `Tak_rec0.htm`, 6906 bytes; SHA-256
`4b9b49e05c3f5d109a71f94c71cd84363a3303b9d6a31e351b07f02e23ca4314`.
Both are byte-identical between PATCH-STANDARD and PATCH-CRUSADES. Active
native template references and their limits are described in
[territory-parameters.md](territory-parameters.md).

## Manual, contemporary press and public-source search coverage

**MANUAL-IP** — [Preserved Iron Plague manual](https://archive.org/details/total-annihilation-kingdoms-iron-plague-manual),
`Total_Annihilation_Kingdoms_-_Iron_Plague_manual_big_text.pdf`, retrieved
2026-09-30; 5327133 bytes; SHA-256
`70802a0b8d9e8fcdb22118c4eab1dfcf908aca40fc07f1ae62daee0e41dbf026`.
Publisher-authored printed manual preserved as a third-party scan/OCR derivative,
not a publisher-authenticated digital original. Printed page 1 (PDF page 2)
was visually inspected: original Kingdoms is required, and Crusades requires
additional disk space beyond the expansion. OCR was used for searching, not
for exact spelling or layout claims. The document supplies installation/support
information, not the missing server formulas. Original PDF remains ignored.

**PRESS-GAMEOVER** — Dick Ritchie's [Iron Plague review](https://www.game-over.net/review/march2000/plague/),
March 2000 URL; retrieved HTML 15602 bytes, SHA-256
`4a0d0d26f295ea3d348901145bc9c37073dc25acba2c2ba7b4313a48a3b2612d`.
The exact publication day is unavailable because its surviving HTML contains
unexpanded date-server directives. It corroborates inclusion of the online
Crusades mode, not precise metagame mechanics. Its three linked screenshots
`Screen0.JPG`, `Screen1.JPG`, and `Screen2.JPG` were retrieved and visually
inspected: all show ordinary tactical battles, **not** the strategic map.
They cannot establish campaign labels, rank displays or battle-selection flow.

The Milestone 1.7 search covered the requested source categories as follows:

| Source category | Outcome / limit |
|---|---|
| Cavedog/support/download pages | WEB-PATCH and WEB-NEWS preserved publisher text; downloaded update payloads fingerprinted |
| Boneyards pages and FAQs | Original shipped Help144/146/147 and recon templates provide primary offline copies; linked online `totalannihilation.com/boneyards/by_tak/dcrusades.html` mirror candidates returned 404 |
| Manuals, release/patch notes | MANUAL-IP, CD readme, README-3 and extracted updater manifest inspected |
| Gaming press/contemporary reviews | PRESS-GAMEOVER inspected; contemporary reporting corroborates availability, not numeric rules |
| Old clan/fan pages | Exact-name searches with clan/Boneyards terms yielded later encyclopedia repetitions, not verified contemporary rule evidence |
| Usenet | Exact-name searches restricted to `groups.google.com` found no useful inspected Crusades thread; this is search failure, not proof none existed |
| Archived forums | [2008 CodeWeavers discussion](https://www.codeweavers.com/compatibility/crossover/forum/total-annihilation-kingdoms?msg=43479) identifies the two patch filenames; [2007 player recollection](https://forum.quartertothree.com/t/caveat-emptor-most-regretted-game-purchases/38482?page=7) recalls the metagame, but neither establishes its rules |
| Screenshots | PRESS-GAMEOVER's tactical shots inspected and excluded from metagame-layout evidence; packaged strategic art/templates remain the direct available evidence |
| Videos | Exact-name video/YouTube searches found no verified recording of a live original Crusades service; unrelated tactical gameplay is not a substitute |
| Download mirrors | FTP-MIRROR, HASH-CROSSCHECK and IRON-PLAGUE-CD provide actual retrieved and hashed artifacts |

Queries included `"Darien Crusades"` combined with `screenshot`, `review`,
`clan`, `forum`, `video`, and domain restrictions for Google Groups, YouTube
and TA Universe. A search hit was not treated as an inspected original rule.
No campaign arithmetic, adjacency list, server source or authoritative campaign
snapshot was recovered through this public-material pass. These are bounded
search results as of 2026-09-30, not a claim that every surviving archive has
been exhausted.
