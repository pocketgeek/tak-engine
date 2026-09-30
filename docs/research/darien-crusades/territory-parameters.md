# Territory parameters, geometry and missing server data

This Milestone 1 investigation distinguishes values the shipped client can
present from rules it computes. It adds no campaign behavior. Binary addresses
refer exclusively to EXE-3, SHA-256
`6ddc7fae0cbf3ee13d530614b64608a3aeefca7deb73719310a4f68fe1f22e96`.
Source/package provenance is in [sources.md](sources.md).

## Surviving parameter schema

Two recon templates contain a victory-point breakdown and an explanation of
capture. They occur byte-identically in the extracted standard and Crusades
3.0 patch payloads, under `MAINDIR/Boneyards/Profile/`:

| File | Bytes | SHA-256 |
|---|---:|---|
| `TAK_reconhistory.htm` | 6,907 | `c89738d776303f29ef40965a5d3feee168806891ca7344dc421a9bccc336b948` |
| `Tak_rec0.htm` | 6,906 | `4b9b49e05c3f5d109a71f94c71cd84363a3303b9d6a31e351b07f02e23ca4314` |

The bindings occupy the same lines in both files:

| Lines | Binding / intended meaning |
|---|---|
| 14–18 | `battles_total`, `time_cont_s`, `hvictory_pnts`, `tvictory_pnts`: battle count, time contested and side-specific battle victory points |
| 49–52 | `htoughness`, `ttoughness`: required victory points for Honor/Terror |
| 56–59 | `fatigue`: fatigue victory points, used in both side columns |
| 63–66 | `hinfluence`, `tinfluence`: support victory points for Honor/Terror |
| 70–73 | `hvictory_pnts`, `tvictory_pnts`: battle victory points |
| 79 | `momentum_s`: text describing which side has momentum |
| 88–91 | Capture explanation: momentum plus combined fatigue, support and battle victory points exceeding the required victory points |

These are **CONFIRMED file observations**. The default `0` in placeholders is
presentation fallback, not a supplied campaign value or server default. The
capture explanation documents an intended predicate but not exact execution
order, numeric representation, rounding, tie handling or reset semantics.
README-3's battle-free capture note does not explain how momentum is treated in
that case. Do not resolve that gap by inventing a special rule.

`TAK_reconhistory.htm` is not merely an unused filename: EXE-3 references its
path at `0x60a928` and supplies it to the recon-view constructor at `0x457da7`,
`0x459061` and `0x460d8c`. The last path tests the territory's state at offset
`0x49` and selects this template when the value is zero. The alternate branch
chooses a different recon template. These observations establish a shipped
client use, without claiming a successful live Boneyards session.

## Where displayed values come from

The recon callback does both named reads and generic property forwarding:

| EXE-3 location | Reconstructed operation |
|---|---|
| `0x45fdff`, `0x45fe26`, `0x45fe4e` | Reads `traffic`, unsuffixed `influence`, and `victory_pnts` into territory object fields |
| `0x4603ce`–`0x460410` | Counts H/T in received `momentum`; the loop has no twenty-entry cap |
| `0x4606b9` | Starts iteration over the incoming property table |
| `0x4606fb`, `0x460701` | Obtains each property's name and type |
| `0x46072f`, `0x460789` | Retrieves a string value and forwards it under that name to the recon view's property table |
| `0x4607f1`–`0x460861`, `0x4608a9`–`0x460919` | Retrieves the two numeric property kinds, formats decimal text and forwards it under the original name |
| `0x46094f` | Advances to the next property |

This forwarding explains how template parameters can be populated without
literal `fatigue`, `htoughness` or `hinfluence` strings in EXE-3. It is evidence
for a display boundary, **not** evidence that the client calculates these values.
The source of server parameter arithmetic remains UNKNOWN.

The bare name `influence` is ambiguous across views. At `0x45ee30`–`0x45efb5`,
the recon-settings path reads territory state `+0x49` and writes the corresponding
Terror/Honor/contested ownership label as the `influence` display value. This
must not be confused with the numeric side-specific support fields above.

The `time_cont` value is read at `0x45cba4`, stored at territory offset `0x66`,
and formatted at `0x45cd7c`–`0x45cdc6`. A second display path at
`0x45ce26`–`0x45ce73` performs the same conversion. The input is treated as
minutes: days = input / 1440, hours = (input % 1440) / 60, minutes = input % 60.
This establishes its **display unit**, not a fatigue growth rate. An isolated
Unicorn check executed the second native conversion with 1,012 values,
including 0, 59, 60, 1439, 1440 and the unsigned 32-bit endpoints; all matched.
No entry point, game UI or network connection was run.

## Local parcels versus live map assignments

[DARIEN-DEF's strict parse](territory-format.md) recovers 313 parcels but no
neighbor IDs, battle-map assignment, fatigue, toughness or support values.
The world header's 871-border count is only a count.

The live-map loader callback at `0x459a00` compares the incoming `command` with
`parcel` at `0x459a4c`–`0x459a70`. Other commands, including `world_attribs`,
bypass parcel construction. It reads the parcel name, description, terrain,
chat-area ID and anchor coordinates. It initializes the map-name and map-script
strings empty at `0x459cfc`–`0x459d07` and `0x459d38`–`0x459d43`; the
header's dimensions and border count are not used in this callback.

Subsequent property callbacks use `terr_id` to find a parcel and then populate
its battle association:

| Location | Field / destination |
|---|---|
| `0x45cb0d`–`0x45cb24` | Reads `terr_id`, resolves the parcel through `0x459720` |
| `0x45cbd6` | Reads `map_name` into parcel `+0x1d3` |
| `0x45cbf4` | Reads `map_type` into parcel `+0x1e3` |
| `0x45e98e`–`0x45e9a8` | Separate settings callback resolves `terr_id` |
| `0x45e9d2`, `0x45e9ef` | Reads `map_script` into `+0x1f3` and `map_name` into `+0x1d3` |

Therefore the local definition is not a complete historical map-assignment
table. The supplied values are tied to a territory at runtime. A complete
mapping requires a captured response, authoritative server table or equivalent
primary data; matching territory names to packaged map names is insufficient.
The separate [battle contract](battle-contract.md) covers settings and battle
entry. `map_script` has not been proved to be an executable script or assigned a
script language merely because of its name.

## How the client obtains clickable territory shapes

The traced live strategic-map rendering path uses `Borders.png`, the three
side/state map images and fire anchors. It does not construct a neighbor list:

1. `0x459dcf` loads `Borders.png`. On initial setup, `0x459de5`–`0x459df7`
   takes dimensions from the decoded image, rather than Darien.def's header.
2. `0x459e66`–`0x459ec4` allocates a reduced territory lookup buffer and
   initializes its populated range with the sentinel `0x7fff`.
3. `0x459ed0`–`0x459f2e` converts border pixels into the working 16-bit image.
4. Territory loops choose Contested/Honor/Terror artwork according to state
   and invoke `0x45b040` from their fire anchors (`0x459fbd`, `0x45a0f8`,
   `0x45a26d`). One specially handled anchor receives extra fill seeds.
5. `0x45b040` scans connected `0xfc1f` pixels, paints them from the selected
   artwork and recursively visits matching spans in the row above/below.
   At `0x45b0cb`–`0x45b0de` it writes the parcel **vector index**, not the
   chat-area ID, into the half-resolution lookup buffer. Selection paths,
   including `0x458d60`, consume that buffer.

An isolated native experiment used an 8×8 synthetic border image with separated
chambers: all 18 pixels connected to the seed were painted, the other chamber
remained unchanged, and the reduced lookup received the supplied index 42.
This verifies flood-fill and lookup behavior; it does not derive campaign
adjacency or claim the synthetic image is a retail asset.

The inspected local `assets/game/Boneyards/Metagame/Borders.png` is 89,820 bytes,
SHA-256 `601879fb99bce2408b7451a0e7aa6564e59b7d6bea5e92c22094a23e82c07444`,
1672×1083, with three used palette entries. Neither patch inventory contains
that path or the companion Honor/Terror/Contested map PNGs. The later
GOG-2.0.0.22 package independently matches these local images; their original
patch/CD delivery route remains unresolved. See [distribution provenance](distribution-provenance.md). Even with authentic artwork,
flood-filled visual contact is not proof of server adjacency: islands,
non-geometric links and border weighting require independent evidence.

## Investigated limits and conclusions

The parameter search examined the two inventoried patch payloads' territory
files and recon/help pages, EXE-3's campaign loaders/callbacks/rendering paths,
and the decoded `boneyards.hpi`, `boneyards2.hpi` and `meta.hpi` members. A
case-insensitive text scan of the Crusades payload's loose text plus those
three decoded archives covered 99 `.gui`, `.tdf`, `.def`, `.jje`, `.txt`,
`.htm` and `.ini` files. It located the recon parameter schema above rather
than an authoritative parameter table. It is not a claim to have examined every
historical installation or server backup.

| Question | Classification / bounded answer |
|---|---|
| What is toughness? | CONFIRMED presentation meaning: side-specific required victory points; actual values and calculation UNKNOWN |
| What is fatigue? | CONFIRMED presentation meaning: shared fatigue victory points; time erosion is documented, but rate, bounds, reset and traffic scaling UNKNOWN |
| What is support? | CONFIRMED presentation meaning: side-specific influence victory points; neighbor effects documented, graph and weights UNKNOWN |
| How is capture explained? | CONFIRMED shipped explanation: momentum plus combined fatigue/support/battle VPs exceeding the threshold; exact authoritative server predicate UNKNOWN |
| Is there a usable territory dataset? | CONFIRMED 313 named/id-bearing parcels and anchors, with local rendering reconstruction; no complete server parameter or adjacency dataset recovered |
| Is map-to-territory assignment fixed locally? | RECONSTRUCTED runtime property assignment; a historical complete table is UNKNOWN |
| Can missing rules be filled from client rendering? | No. Hit testing and text formatting do not recover server campaign arithmetic |

The next evidence that would change these conclusions is a server/configuration
backup, an authenticated historic response or replay carrying the missing
fields, or another original distribution with those tables. Do not substitute
an invented graph, default zero values, or the plan's illustrative formulas.
