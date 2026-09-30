# Crusades versus the local Iron Plague-era installation

A clean, independently fingerprinted Iron Plague distribution has not been
compared. The existing `assets/game` tree includes IPData/IPMissions and later
or modified files, and is labeled **LOCAL-MIXED**, not an original CD baseline.

The selected inventory contains 194 loose files and 2,770 archive members.
Its filter scope is recorded in [local-install.json](inventories/local-install.json).
It includes Boneyards files, relevant archives and executables; it excludes user
maps, runtime caches and unrelated installation directories.

Direct byte comparisons against PATCH-CRUSADES show:

| Local file | Compared patch payload |
|---|---|
| `KINGDOMS.icd` | Different SHA-256, same size; cause not established |
| `bymaia.dll` | Identical |
| `Rover.dll` | Identical |
| `Boneyards/Metagame/Darien.def` | Identical |

This establishes that useful primary metagame material survives locally. It does
not establish the differences between original Iron Plague media and Crusades,
nor authorize using the local executable's offsets as official-patch offsets.
Compare a separately sourced original Iron Plague payload in a later pass using
the same inventory tool and a completed provenance record.
