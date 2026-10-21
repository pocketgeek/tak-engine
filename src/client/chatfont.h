#pragma once

// The in-game chat box's face (GameView::drawGameChat): Noto Sans Bold, embedded in
// takclient (res/chat-font, SIL OFL 1.1 -- CHAT-FONT-LICENSE.txt) and rasterized by
// stb_truetype (src/vendor/stb) at the exact pixel size drawn. It replaced the retail
// bold Times GAF, which players found hard to read.
//
// It offers the slice of Font's interface drawGameChat uses -- width(), vbounds() and
// drawSolid(), with the same meaning of `scale` (the face is set 16 px tall, ascent to
// descent, at scale 1) and `y` (the top of the line; the baseline sits ascent below).
//
// Pixels. The client draws in renderer OUTPUT pixels everywhere (client/hidpi.h): on a
// macOS Retina window the UI SCALE already carries the backing scale (1 or 2, per
// display), and on Linux/Windows output pixels are window pixels. So `scale` here is
// always output pixels per design pixel, and the glyph sheet is rasterized at the real
// backing size and drawn 1:1 (nearest, whole-pixel positions): sharp at 100%, at UI
// SCALE 200% and on Retina alike. Pen positions keep their fraction through three
// horizontal subpixel phases at text sizes under 32 px, so spacing is exact too.
//
// Text is UTF-8 (what SDL text input produces); a byte that does not start a valid
// sequence stands for itself as Latin-1, so legacy 8-bit text still shows. The face
// carries printable ASCII, Latin-1 and the typographic punctuation an OS keyboard or
// paste produces; any other character (CJK, emoji, control codes) draws as '?', one
// glyph per code point, so a line's measured and drawn widths always agree.
//
// The glyph sheet. One texture, for one pixel size: built on the first draw, rebuilt
// only when the drawn size changes (CHAT SCALE, UI SCALE, or a window small enough to
// shrink the box), never per frame. destroyGlyphs() frees it (renderer reset / art
// reload, like the other fonts); the next draw rebuilds it. Display only: nothing here
// touches the simulation.

#include <SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct stbtt_fontinfo;

namespace tak {

// Decode UTF-8 to code points. Only well-formed sequences decode (no overlongs,
// surrogates or values past U+10FFFF); any other byte stands for itself (Latin-1).
std::vector<uint32_t> decodeUtf8Lenient(const std::string& s);

class ChatFont {
public:
    // The embedded Noto Sans Bold.
    ChatFont();
    // Any TrueType face; `data` must outlive the font. `basePx` is the pixel height
    // (ascent - descent) at scale 1.
    ChatFont(const unsigned char* data, size_t size, float basePx);
    ~ChatFont();
    ChatFont(const ChatFont&) = delete;
    ChatFont& operator=(const ChatFont&) = delete;

    bool ok() const { return info_ != nullptr; }
    float width(const std::string& text, float scale = 1) const;
    // Ink extent of `text` below the line top: offset of its highest ink and its height.
    void vbounds(const std::string& text, float scale, float& topOff, float& h) const;
    void drawSolid(SDL_Renderer* ren, const std::string& text, float x, float y,
                   float scale, SDL_Color color) const;
    // Free the glyph sheet (gpuvram-accounted); the next draw rebuilds it.
    void destroyGlyphs();

    // The code point drawn for `cp`: itself when the face carries it, else '?'.
    static uint32_t mapped(uint32_t cp);
    // The pixel size `scale` is rasterized at (quarter-pixel steps).
    float pixelSize(float scale) const;
    // Diagnostics for tests: sheets built so far, and the current sheet's size.
    int sheetBuilds() const { return builds_; }
    int sheetW() const { return sheetW_; }
    int sheetH() const { return sheetH_; }

private:
    struct Metrics { int glyph = 0, advance = 0, y0 = 0, y1 = 0; bool box = false; };
    struct Glyph { int sx = 0, sy = 0, w = 0, h = 0, xoff = 0, yoff = 0; };
    static constexpr int kMaxPhases = 3;
    static int slot(uint32_t mappedCp);         // index into metrics_ (mapped code points)
    float sc(float px) const;                   // stbtt scale for a pixel height
    float kern(int a, int b) const;             // font units between two slots
    bool ensureSheet(SDL_Renderer* ren, float px) const;

    stbtt_fontinfo* info_ = nullptr;   // owned (incomplete here, so not a unique_ptr)
    float basePx_ = 16;
    int ascent_ = 0, descent_ = 0;
    std::vector<Metrics> metrics_;     // per slot
    mutable std::unordered_map<uint32_t, int16_t> kern_;   // slot pair -> font units

    // The one glyph sheet.
    mutable SDL_Texture* tex_ = nullptr;
    mutable SDL_Renderer* texRen_ = nullptr;
    mutable int key_ = -1;             // pixel size * 4 the sheet holds
    mutable int phases_ = 1;
    mutable std::vector<std::array<Glyph, kMaxPhases>> glyphs_;   // per slot
    mutable int builds_ = 0, sheetW_ = 0, sheetH_ = 0;
};

}  // namespace tak
