# Crusades versus Iron Plague distributions

An original Iron Plague CD payload and a user-owned GOG offline installer have
now been extracted and fingerprinted. See [sources](sources.md) and the
[detailed provenance investigation](distribution-provenance.md) for acquisition,
confidence, exact hashes and reproduction steps. Neither installer was run.

| Comparison | Observed result |
|---|---|
| CD base data/English vs GOG/current local | Byte-identical archives |
| CD IPData/IPEnglish vs GOG/current local | Byte-identical archives |
| CD V3Rocket vs GOG/current local | 1140 of 1141 decoded members identical; only Crusades map localization changes |
| CD KINGDOMS.icd vs official 3.0 patch/GOG | Different executables; static addresses must remain version-specific |
| CD Rover vs official 3.0 patch/GOG | Different DLL bytes and sizes |
| CD bymaia/meta.hpi vs later package | Identical |
| CD Darien.def vs official 3.0 patch | Same world header/IDs/factions/terrain; 27 parcels have presentation edits |
| Selected GOG payload vs current local | All 40 selected files identical |

The [CD filesystem inventory](inventories/iron-plague-cd.json) and
[decoded installer payload inventory](inventories/iron-plague-cab.json) provide
82 + 320 loose-file fingerprints and all decoded HPI member fingerprints.
The [selected GOG inventory](inventories/gog-selected.json) records its explicit
40-file scope. These supplement the earlier mixed local survey rather than
relabeling that entire tree as pristine.

The CD cabinet's `Crusades_Files` group establishes that metagame assets,
Darien.def, help and Boneyards components shipped on the expansion media.
It does not prove which optional groups a user selected, the installer's full
conditional logic, or the order of subsequent online patches. Campaign
implementation research continues to use the explicitly fingerprinted standalone
3.0 patch executable and Rover, not offsets borrowed from the CD or GOG versions.
