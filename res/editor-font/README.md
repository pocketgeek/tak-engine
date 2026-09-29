# Editor font source

`NotoSansMono-Regular.ttf` is Noto Sans Mono version 2.014, copyright 2022
The Noto Project Authors, from https://github.com/notofonts/latin-greek-cyrillic.
It is distributed under SIL OFL 1.1; see `EDITOR-FONT-LICENSE.txt` at the
repository root. This is an open font, not a retail game asset.

`tools/build-editor-font.py` rasterizes its mapped codepoints into the embedded
`src/cartographer/fontdata.inc` alpha masks. Regeneration requires Python,
Pillow and fontTools; ordinary builds and deployed binaries require none of
those tools or font libraries. The generated header records the source hash.
The renderer uses fixed-width cells to preserve caret/hit-test geometry.

The embedded coverage includes Latin, Greek and Cyrillic. Missing glyphs use
a replacement symbol. Complex-script shaping, bidi layout and CJK fallback
are not supplied by this font backend.
