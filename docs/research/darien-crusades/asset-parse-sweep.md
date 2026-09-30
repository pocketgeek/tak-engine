# Added campaign asset parse sweep

On 2026-09-30, every case-insensitively added entry in PATCH-CRUSADES relative
to PATCH-STANDARD was checked against its recorded SHA-256, then inspected with
existing tools according to its format. The scope is 1,027 records, counting
containers and decoded members separately. This is an asset-validation pass,
not execution of the campaign or proof of game-rule semantics. The per-file
results are [parser-fullchecks.json](inventories/parser-fullchecks.json).

| Added material | Existing tool / check | Result |
|---|---|---|
| 181 KMP and one HPI | `hpitool list/cat`, source inventories | All members read and hashed |
| 181 TNT | `tnttool info` | All parsed |
| 181 OTA | `tnttool ota`; `tdftool check` for differences | All parsed; 178 byte-identical round trips, three canonical writes differ |
| 181 CRT | `tnttool crt` | All passed its semantic/stable-write checks |
| 31 GAF | `gaftool list` | All loaded, including frame decoding |
| 31 PCX, 29 PNG | Pillow image load | All decoded |
| Two TSF and one TDF | `tdftool check` | All parsed; TSF files use sprite-frame/layer declarations |
| Five `.bik` files | Native assembly trace, then `biktool` | Five chunks of one movie; full joined movie decodes all 1,110 frames |
| 20 Boneyards GUI | Existing `tak::gui::parse` via research driver | All parsed to EOF: 374 gadgets; initial TDF rejection was a wrong-format tool choice |
| 181 TXT, one HTM, one BAT | CP1252 text decode and role inspection | Text; no code or batch file executed |

The three OTA byte differences are `Lobster Manor`, `The Vault`, and
`Waterworks`. Their scenario readers report dimensions and start positions,
then return nonzero because the writer's result differs from the original
text. The ordinary TDF check also accepts all three. These are **not** evidence
that the input maps fail to load; the sweep does not claim that their canonical
writer preserves every original text field or formatting choice.

`gaftool list` calls the full GAF loader before reporting sequences, so this
checks frame decoding as well as headers. The palette came from the already
original-media-corroborated `data.hpi`, member `palettes/guipal.pal`, SHA-256
`4213f545e311c75413c04de4d847f16daee558b566d90da985164ea14d8649f4`.
This validates decoding, not exact rendered palette selection for each UI.
No added COB, 3DO or single-player campaign mission scripts occur in this
package difference; a `missiontool` tactical mission run would not validate
Darien's server-side campaign rules.

The numeric GUI files are accepted by the existing engine GUI parser, which
consumes gadget records, length-prefixed resource names, and counted UI lists.
The [numeric GUI check](numeric-gui.md) records its structure, native reader
entry anchors and per-file counts. No new parser or production UI was added.
The initial `tdftool` rejections are preserved as first-pass history, not left
as evidence that the format cannot be parsed.

## One movie, five pieces

The files `FTUIMovie1.bik` through `FTUIMovie4.bik` each contain 1,048,576 bytes;
`FTUIMovie5.bik` contains 624,064 bytes. Only the first starts with a Bink header.
That header declares a total stream size of 4,818,368 bytes and 1,110 frames.
Decoding the first piece alone stops after 239 frames; a zero exit status from
`biktool` is therefore not sufficient to claim the movie is complete.

EXE-3 routine `0x485a10` checks for an existing `FTUIMovie.bik`. If absent it
creates the output in binary-write mode and copies each numbered input in
ascending order, using 1,024-byte read/write blocks. Input opens occur at
`0x485a5c`, `0x485aca`, `0x485b38`, `0x485ba6` and `0x485c10`. The copy loops
end on an empty read; the error branches delete the incomplete output. This is
**RECONSTRUCTED native assembly behavior**, not guessed installer concatenation.

Joining the five original pieces in that order in scratch space gives:

| Property | Verified result |
|---|---|
| Bytes | 4,818,368 |
| SHA-256 | `6d734b6efd28fab1a125feea65d2fe90a1cddbfdd136e3477ec45b36a16ec0e4` |
| Dimensions | 640×280 |
| Frame rate | 30 fps |
| Header / decoded frames | 1,110 / 1,110 |

No movie, extracted frames, original palette or disassembly was committed.
The original five files remain separately represented in the distribution
inventories; their filenames do not imply five distinct videos.

## Reproduction

Use `crusades_inventory.py` to produce the two inventories and compare keys by
case-insensitive `(archive, path)`. For each added member, `hpitool cat` obtains
the bytes in scratch space; verify its inventory hash before running the tool
listed above. For loose files, inspect the original extracted path directly.
Run `gaftool list FILE PALETTE`, `tnttool info TNT`, `tnttool ota OTA`,
`tnttool crt CRT`, and `tdftool check FILE`. For GUI resources, use the
[existing-parser driver](numeric-gui.md#reproduction). Keep tool prose and asset exports
outside Git. For the movie, concatenate the five numbered pieces, compare the
joined header's size/frame count and run `biktool JOINED` to completion.

The earlier [parser-checks.json](inventories/parser-checks.json) remains a record
of the narrower first pass; this report supersedes its coverage summary without
rewriting historical tool results.
