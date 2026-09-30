# Iron Plague and later packaged data provenance

Investigation date: 2026-09-30. These are read-only extraction and comparison
results. No installer or retail game was executed. Original files remain under
ignored `assets/research/darien-crusades/`; only observations and inventories
are committed.

## Original media acquisition

The [Internet Archive Thailand release item](https://archive.org/details/tak-iron-plague-thai-release)
identifies an English-language disc distributed in Thailand by B.M. Media.
Its `Images/IRON_PLAGUE.bin` was downloaded, measured and matched against the
item's published SHA-1. The uploader's regional attribution is not independently
authenticated. The disc itself declares volume `IRON_PLAGUE`, volume set
`Iron Plague`, publisher `Cavedog Entertainment`, application `BUZZSAW 1`.
The included CUE declares one MODE1/2352 track starting at sector zero.

| Observed artifact | Bytes | SHA-256 |
|---|---:|---|
| Downloaded `IRON_PLAGUE.bin` | 715374912 | `e5c4f6504e5151992f977f44a18788b47e85daedbd83f6ff0a935cb7b5abc82e` |
| Derived 2048-byte-sector ISO | 622911488 | `1bf9ff9d186401adcf51b34973a811db20efaf2dd55bfabe7f6a06a3f65d26ef` |

The BIN SHA-1 is `900bc863716c04c6247c898543342c6b70482553`. Extracting
bytes 16 through 2063 of every 2352-byte sector produces an ISO with SHA-1
`64f34b0c50522fb7f0bb82d5c6e65eee53ec1df1`. That exactly matches the independently
listed ISO's published SHA-1 in the [second Archive item](https://archive.org/details/total-annihilation-kingdoms-iron-plague).
This corroborates identical filesystem-image bytes across two preservation
listings; it is not a publisher signature or two independently examined discs.

A [US original-edition listing](https://archive.org/details/total-annihilation-kingdoms-iron-plague-us-2000-win-3.0)
also links Redump disc 51458 and identifies version 3.0, but its 1.99 GB dump
bundle was not downloaded. No equality with that US dump is claimed.

7-Zip 26.02 extracted the ISO filesystem. The expansion payload is an
InstallShield 5 cabinet, not a Microsoft CAB; 7-Zip could not unpack it.
[Unshield](https://github.com/twogood/unshield) source commit
`cbed3d19ef9cdf4e5fb86b9d65e48f1bf8d539db` was built in `/tmp` and successfully
extracted `data1.cab` using `unshield -d OUTPUT x data1.cab`. The locally built
executable SHA-256 is `f7c207dd22aad213e687c3678927cde60ba29d35136af3f5e500264035b98d88`.
No new engine dependency was added.

The [disc inventory](inventories/iron-plague-cd.json) records 82 filesystem files
and 9885 decoded HPI members. The [cabinet inventory](inventories/iron-plague-cab.json)
records 320 extracted payload files and 2561 decoded HPI members. Paths retain
the cabinet's file-group names. Embedded installer support cabinets are
fingerprinted, but their installer-runtime contents are not recursively decoded.

## Provenance of the existing gameplay data

The user-owned offline installer
`setup_total_annihilation_kingdoms_2.0.0.22.exe` was found in the local Downloads
directory. It is 959342944 bytes, SHA-256
`6d40e338887dc94d036c7858ce683637ada307a36fad0bc21c80729bf7bd570e`.
Its original acquisition date and independent download digest are unknown.
Innoextract 1.9 recognizes its product as Total Annihilation Kingdoms and format
as Inno Setup 5.5.0 Unicode. Selected archives, binaries, metagame files and
readmes were extracted without running the installer. The
[selected inventory](inventories/gog-selected.json) explicitly represents this
selection, not the entire installer.

All 40 selected extracted files are byte-identical to the corresponding files
in `assets/game`. In particular this explains a package source for its
`KINGDOMS.icd` hash and its later `V3Rocket.hpi`; their difference from the
standalone 3.0 patch does not by itself demonstrate a local modification.
This is a packaged GOG-era comparison, not a claim that the entire live
installation is untouched.

The original disc independently corroborates the following archive bytes in
both GOG and the local installation:

| CD source | GOG/local archive | SHA-256 |
|---|---|---|
| filesystem root | data.hpi | `3a489d9169af350db2153262421f503b9dfb89fded260e30e7e06eb72427c0d6` |
| filesystem root | english.hpi | `d6812403790948981f034a03e3c25a5d8b0789a8ffda0bdb68e705c81787c6b5` |
| Required_Install_Files | IPData.hpi | `4c7fbe0a71c6ed8c4169681a5690e23f6129fe02ca663fa978acb3aa901a48d4` |
| Required_Install_Files | IPEnglish.hpi | `5c7242d783cace7f1ad6ad45fdc3323f9f0a9d2aa45903fb2df23de43331f168` |

Thus the main base and expansion unit data now have original-media byte
corroboration. The later package must still be distinguished from the original
CD for executable and Boneyards research.

## Changes between disc and later packages

The CD `Data/V3ROCKET.HPI` is 2735598 bytes, SHA-256
`f7b46d5bd1cc6e76abc0c74af137828666da34b681880e95b44255bedeb506ca`.
The GOG/local archive is 2735599 bytes, SHA-256
`2c071a66e2388a9c93ea859b6c635b8508d6d2c59e7b85815cd4a2261e3bc7a7`.
Comparing all 1141 decoded member paths case-insensitively finds 1140 identical
member hashes. The only changed content is `translate/crusades.tdf`, including
leading spaces in map-name localization keys; its stored filename case also
changes. No unit or build-menu member changes between these two archives.

The CD's `Kingdoms.icd` is 2244725 bytes, SHA-256
`60a65c1ef99eb360929591873463af4e87df270ff10039954db8e0014714b014`.
The CD `Rover.dll` is 196608 bytes, SHA-256
`90e459fb39e7b8976a2b023aa8b0b48da7a923e56350bbb9da1ada01432b950e`.
Neither equals the standalone 3.0 patch or GOG executable/Rover pair.
`bymaia.dll` and `meta.hpi` do match the later package. Do not transfer static
addresses across these executable versions.

The CD `Darien.def` is 171052 bytes, SHA-256
`114254a9fbad1d4f602d268ddfe365ba6459233b9b00b19df350249d0605c45a`.
The existing strict territory parser accepts it. Compared with the patch's
171051-byte version, the world header, 313 territory IDs, native races and
terrain fields agree. Twenty-seven parcels change: 37 numeric text/fire anchor
fields, four descriptions (colon encoding), and one display name. These are
presentation changes; neither version supplies an adjacency edge list or
server ownership state. The one-byte size difference is **not** just a newline.

The CD cabinet has no loose `Borders.png`, `MetaMask.png`, `HonorMap.png`,
`TerrorMap.png` or `ContestedMap.png` in its metagame directory. GOG ships these
and all match the current local installation. Their GOG-package origin is now
verified; this does not yet establish the original Boneyards download route.

## Installation scope and reproducible audit roots

The CD readme's installation section requires an existing Kingdoms installation.
The extracted cabinet separates `Required_Install_Files` from `Crusades_Files`.
These names and extracted contents establish the groups' existence, not the
installer's conditional selection or exact final installation order.
The standalone Wise patch script contains a Kingdoms App Paths registry lookup
and an installation-not-detected message. Raw strings alone do not establish
all control-flow conditions, file overwrite policies or successful installation.

For data comparisons, `iron-plague-audit-root/` is deliberately assembled by
copying root HPIs from the CD filesystem, then `Data/`, then the cabinet's
`Required_Install_Files/` and `Crusades_Files/`, preserving file timestamps.
`gog-2.0.0.22/app/` is the selected GOG extraction. Both live under the ignored
research directory. They are **data audit roots**, not claims that an original
installer has been run or a historical online update reproduced.

The unresolved provenance boundary is now narrower: exact installer conditions,
historical update order, and the strategic PNGs' original delivery path remain
unverified. Original CD base/IP archives, standalone standard/Crusades patches,
and the later GOG payload can be compared reproducibly without guessing those
conditions.

## Bounded installer-condition investigation

Additional inspection on 2026-09-30 establishes the following without running
an installer:

- `unshield c data1.cab` lists five components: `Required Install Files\\Data Files`,
  `Stupid Fake Component`, `CD Only Files`, `Required Install Files`, and
  `Darien Crusades Files`. `unshield g` lists three file groups: `Crusades Files`,
  `CD Only Files`, and `Required Install Files`. These are decoded cabinet
  metadata, not guesses based on output directory names.
- CD `setup.ins` is a compiled 59520-byte InstallShield script (SHA-256
  `f2deba3b22cf493f145bc7314d00c4b8503f2c5988391e7f4f9a8030c5b8357e`).
  Its raw string offsets identify the existing-game registry key at `0xf81`,
  executable filenames near `0x1260`, V2Rocket/V3Rocket/Jersey near `0x1332`,
  and named Crusades/required component operations near `0x14ab–0x15aa`.
  Strings establish referenced targets, not branch outcomes or execution order.
- The preserved printed manual (MANUAL-IP in sources) confirms that Crusades
  uses an additional installation allocation. Its printed page 1/PDF page 2
  gives 95 MB for the expansion plus 35 MB for Crusades; the troubleshooting
  section separately distinguishes installing Crusades. The page image was
  visually checked, not inferred solely from OCR. This corroborates a distinct
  Crusades installation choice, without proving its default checkbox state.
- WiseUnpacker's `-i -o=SCRATCH Crusades.exe` reports script inflated size 36938
  bytes, deflated size 9248, and overlay flags `0x12caa`. It prints overlay
  metadata but exposes no textual control-flow decompilation mode.
- REWise 0.3.1 source revision `c3d3b68903a90ec53ff7b0a4ae704adc6302814b`
  rejects this executable's overlay (`Advertised .dib size looks insane`).
  A scratch driver invoking its existing Wise-script debug parser directly on
  the already extracted `WiseScript.bin` also misinterprets the header: it
  labels part of the installation-log path as opaque header bytes and reports
  a truncated font as the URL. Its decoded conditions are therefore **not
  valid evidence** and were not used. No original script or failed dump is
  committed, and no new installer implementation was written.

This bounded pass recovers component boundaries, payload contents, existing-
installation requirements and separate Crusades allocation. It does **not**
recover a trustworthy complete interpreter/decompilation of either installer,
its checkbox defaults, overwrite/version comparisons or a user's historical
installation sequence. Those remain explicitly UNKNOWN. Exact source-data
comparisons use declared archive sets and fingerprinted members, so they do not
silently depend on any of those missing installer behaviors.
