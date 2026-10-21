# Chat-box font

Noto Sans Bold 2.015 (SIL OFL 1.1; notice and licence text in `CHAT-FONT-LICENSE.txt` at
the repository root), subset from the upstream file with fontTools:

    pyftsubset NotoSans-Bold.ttf \
      --unicodes="U+0020-007E,U+00A0-00FF,U+2013-2014,U+2018-201E,U+2022,U+2026,U+20AC" \
      --layout-features=kern --no-hinting --desubroutinize \
      --output-file=NotoSans-Bold.ttf

`cmake/EmbedFiles.cmake` writes it into `takclient` as a byte array (no runtime data
file), and `src/client/chatfont.cpp` rasterizes it with stb_truetype (`src/vendor/stb`)
at the exact pixel size drawn. The character set is mirrored in `chatfont.cpp`
(`kExtras`); anything outside it draws as `?`.
