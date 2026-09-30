# Numeric campaign GUI parsing

The 20 added GUI files in PATCH-CRUSADES `meta.hpi` all parse to end of input
with the existing `tak::gui::parse` in `src/gui/gui.cpp`. The initial choice of
`tdftool` was wrong: these are length-prefixed numeric widget streams, not TDF.
The rejected first-pass checks remain historical observations, superseded by
this format-appropriate check on 2026-09-30.

The check yields **374 gadget records**, with **961 image-reference entries**,
**961 text entries**, and **961 state entries**. Empty strings/placeholders
count as entries; these counts are not counts of distinct artwork or labels.
Every input hash was rechecked against the patch inventory. Per-file record
counts and numeric type histograms are in
[inventories/parser-fullchecks.json](inventories/parser-fullchecks.json).
No proprietary text, rectangles or asset bytes are included in that output.

## Observed structure and parser scope

The existing parser consumes a flat sequence of gadget records to EOF:

| Component | Parsed structure |
|---|---|
| Record header | Numeric gadget type and version |
| Type-specific prefix | Numeric style fields; edit boxes additionally contain a length-prefixed string |
| Common gadget | Marker 2, eight integers including rectangle/flags, marker 3 and four color components |
| Cursor/font/name | Marker-delimited length-prefixed strings |
| Image list | Counted entries with resource/sequence strings and numeric frame/flags |
| Text list | Counted entries with alignment and length-prefixed text |
| State list | Counted length-prefixed strings |
| Command/end | Command metadata/string and trailing identifier |

String length refers to bytes, allowing embedded whitespace. The existing
reader tolerates an under-counted length by extending to the next whitespace;
its generic style-prefix reader searches for the common-gadget signature.
This pass establishes that the surviving files can be structurally consumed
by the engine's existing parser. It does **not** prove strict malformed-input
rejection, every style-field meaning, rendering fidelity or a full native UI
implementation. No changes to that production parser were needed.

## Native resource path

Fingerprint EXE-3 from [sources.md](sources.md) ties the campaign resource to
native dialog loading: `0x43db6a` supplies `BYMetaBattleHostDialogue.gui` to
constructor `0x4ab500`; that constructor calls `0x4ab690` at `0x4ab56c`.
The latter obtains its stream through `0x4aafb0`, consumes a leading value with
`0x435f90`, and invokes `0x5708a0` at `0x4ab6df`. That routine consumes another
value with `0x435f90`, then enters `0x573e80`. These are native reader entry
anchors, not a claim that each virtual gadget reader has been reimplemented or
that all engine parser tolerance exactly matches native malformed-input behavior.

## Reproduction

Extract the 20 added `guis/*.gui` members with `hpitool cat` into a scratch
directory, preserving filename case. The wrapper below calls the existing
parser and reports only counts and type histograms; it contains no new grammar.

```sh
c++ -std=c++17 -Wall -Wextra -Werror -Isrc \
  tools/re/check_crusades_gui.cpp src/gui/gui.cpp -o /tmp/check-crusades-gui
/tmp/check-crusades-gui /path/to/extracted/guis/*.gui /path/to/extracted/guis/*.GUI
```

The uppercase glob matters: `BYHOUSE0.GUI` is one of the 20 files. Build and
full-input parsing passed; no game or original UI was launched.
