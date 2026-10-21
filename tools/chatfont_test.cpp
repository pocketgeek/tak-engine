// chatfont_test -- the chat box's embedded face (client/chatfont.h).
//
// The face loads with sane metrics; widths add up (and kern); text wraps to a width
// with it (client/chatbox.h); UTF-8 decodes strictly, invalid bytes stand for
// themselves as Latin-1, and characters the face lacks draw as '?' one per code
// point; and the glyph sheet is built once per pixel size -- not per draw, not per
// colour -- rebuilt after destroyGlyphs(), and actually puts ink on the target.

#include "client/chatbox.h"
#include "client/chatfont.h"

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0;
static void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++g_fail;
}

static bool same(const std::vector<uint32_t>& a, std::initializer_list<uint32_t> b) {
    return a == std::vector<uint32_t>(b);
}

int main() {
    std::printf("chatfont_test\n");
    tak::ChatFont f;
    check(f.ok(), "the embedded Noto Sans Bold loads");
    if (!f.ok()) return 1;

    // ---- metrics ----
    float top = 0, h = 0;
    f.vbounds("Hdgjpqy", 1, top, h);
    check(h > 10.5f && h < 13 && top > 2 && top < 5, "ink of Hdgjpqy in a 16 px line: ~11.8 tall, ~3.6 down");
    float capTop = 0, capH = 0;
    f.vbounds("H", 1, capTop, capH);
    check(capH > 7.5f && capH < 9.5f && capTop > top, "cap height ~8.4 px, below the ascender line");
    const float wH = f.width("H"), wi = f.width("i"), wSp = f.width(" ");
    check(wH > wi && wi > 0 && wSp > 0, "proportional: H wider than i, space has width");
    check(f.width("") == 0, "the empty string is 0 wide");

    // ---- widths ----
    const std::string hello = "Hello, world!";
    const float w1 = f.width(hello);
    check(w1 > 65 && w1 < 82, "\"Hello, world!\" in a 16 px line is ~74 px wide");
    check(std::fabs(f.width(hello, 2) - 2 * w1) < 2, "twice the scale, twice the width");
    check(std::fabs(f.width("AV") - (f.width("A") + f.width("V"))) > 0.1f, "kerning: AV is not A + V");
    float sum = 0;
    for (char c : std::string("xxxx")) sum += f.width(std::string(1, c));
    check(std::fabs(f.width("xxxx") - sum) < 0.01f, "no kerning pair: widths add up");
    check(f.pixelSize(1) == 16 && f.pixelSize(1.3f) == 20.75f && f.pixelSize(0.01f) == 1,
          "pixel size: 16 px at 1, quarter-pixel steps, never below 1");

    // ---- wrapping with the face ----
    auto width = [&](const std::string& t) { return f.width(t); };
    const std::string para = "the quick brown fox jumps over the lazy dog again and again";
    const float wrapW = 150;
    const auto rows = tak::chatbox::wrap(para, wrapW, wrapW, width);
    bool fits = rows.size() >= 3;
    std::string joined;
    for (const auto& r : rows) {
        fits = fits && f.width(r) <= wrapW && !r.empty();
        joined += (joined.empty() ? "" : " ") + r;
    }
    check(fits, "a sentence wraps to rows that each fit 150 px");
    check(joined == para, "and the rows rejoin to the sentence");
    const auto broken = tak::chatbox::wrap(std::string(60, 'W'), 100, 100, width);
    bool bfits = broken.size() > 1;
    for (const auto& r : broken) bfits = bfits && f.width(r) <= 100;
    check(bfits, "an unbroken word is split between characters to fit");
    const auto u = tak::chatbox::wrap("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 20, 20, width);
    bool whole = u.size() > 1;
    for (const auto& r : u) whole = whole && r.size() % 2 == 0;
    check(whole, "a split never cuts a UTF-8 character");

    // ---- UTF-8 ----
    using tak::decodeUtf8Lenient;
    check(same(decodeUtf8Lenient("A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"), {'A', 0xE9, 0x20AC, 0x1F600}),
          "1-, 2-, 3- and 4-byte sequences decode");
    check(same(decodeUtf8Lenient("caf\xE9"), {'c', 'a', 'f', 0xE9}), "a Latin-1 byte stands for itself");
    check(same(decodeUtf8Lenient("\xC3"), {0xC3}) && same(decodeUtf8Lenient("\xE2\x82"), {0xE2, 0x82}),
          "a truncated sequence: its bytes, one each");
    check(same(decodeUtf8Lenient("\xC0\xAF"), {0xC0, 0xAF}) && same(decodeUtf8Lenient("\xE0\x81\x81"), {0xE0, 0x81, 0x81}),
          "overlong forms are not decoded");
    check(same(decodeUtf8Lenient("\xED\xA0\x80"), {0xED, 0xA0, 0x80}) &&
              same(decodeUtf8Lenient("\xF4\x90\x80\x80"), {0xF4, 0x90, 0x80, 0x80}),
          "surrogates and values past U+10FFFF are not decoded");
    check(same(decodeUtf8Lenient("\xC3x"), {0xC3, 'x'}), "a lead byte before a non-continuation: both kept");
    check(tak::ChatFont::mapped(0x1F600) == '?' && tak::ChatFont::mapped(0x4E2D) == '?' &&
              tak::ChatFont::mapped(7) == '?' && tak::ChatFont::mapped(0x85) == '?',
          "emoji, CJK and control codes map to '?'");
    check(tak::ChatFont::mapped(0xE9) == 0xE9 && tak::ChatFont::mapped(0x20AC) == 0x20AC &&
              tak::ChatFont::mapped(0x2019) == 0x2019,
          "Latin-1 and the typographic extras are carried");
    check(std::fabs(f.width("\xE4\xB8\xAD\xF0\x9F\x98\x80") - f.width("??")) < 0.01f,
          "two missing characters measure as two '?'");
    check(std::fabs(f.width("a\xFF\xFE") - f.width("a\xC3\xBF\xC3\xBE")) < 0.01f,
          "invalid bytes measure as their Latin-1 letters");

    // ---- the glyph sheet ----
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, 400, 120, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* ren = surf ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    check(ren != nullptr, "a software renderer for the sheet");
    if (ren) {
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        check(f.sheetBuilds() == 0, "no sheet before the first draw");
        for (int i = 0; i < 5; ++i)
            f.drawSolid(ren, hello, 10, 10, 1, {255, 255, 255, 255});
        f.drawSolid(ren, hello, 11, 12, 1, {0, 0, 0, 128});
        f.width(hello, 3);   // measuring at another size builds nothing
        check(f.sheetBuilds() == 1, "one sheet for many draws, colours and positions");
        check(f.sheetW() >= 256 && f.sheetH() > 0 && f.sheetH() <= f.sheetW() * 2, "the sheet is roughly square");
        f.drawSolid(ren, hello, 10, 40, 1.5f, {255, 255, 255, 255});
        f.drawSolid(ren, hello, 10, 40, 1.5f, {255, 255, 255, 255});
        check(f.sheetBuilds() == 2, "a new pixel size rebuilds it once");
        f.drawSolid(ren, hello, 10, 40, 1.5f + 0.001f, {255, 255, 255, 255});
        check(f.sheetBuilds() == 2, "a size within the same quarter pixel does not");
        f.destroyGlyphs();
        f.drawSolid(ren, hello, 10, 40, 1.5f, {255, 255, 255, 255});
        check(f.sheetBuilds() == 3, "destroyGlyphs (renderer reset): the next draw rebuilds");
        f.drawSolid(ren, "\xE4\xB8\xAD\xFF\x01", 10, 80, 1, {255, 255, 255, 255});
        check(f.sheetBuilds() == 4, "odd text draws without trouble");
        f.drawSolid(ren, hello, 10, 10, 6, {255, 255, 255, 255});   // 300% x UI 200%
        check(f.sheetW() <= 4096 && f.sheetH() <= 4096, "the 96 px sheet stays within 4096 x 4096");
        f.destroyGlyphs();

        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        f.drawSolid(ren, "Hello", 10, 10, 1, {255, 255, 255, 255});
        SDL_RenderPresent(ren);
        int inkMinX = 999, inkMaxX = -1, inkMinY = 999, inkMaxY = -1;
        SDL_LockSurface(surf);
        for (int y = 0; y < surf->h; ++y)
            for (int x = 0; x < surf->w; ++x) {
                const uint8_t* p = static_cast<const uint8_t*>(surf->pixels) + y * surf->pitch + x * 4;
                if (p[0] > 128) {
                    inkMinX = std::min(inkMinX, x); inkMaxX = std::max(inkMaxX, x);
                    inkMinY = std::min(inkMinY, y); inkMaxY = std::max(inkMaxY, y);
                }
            }
        SDL_UnlockSurface(surf);
        float hTop = 0, hH = 0;
        f.vbounds("Hello", 1, hTop, hH);
        check(inkMaxX > inkMinX && inkMinX >= 10 && inkMinX <= 12 && float(inkMaxX - 10) <= f.width("Hello") + 1,
              "ink starts at x and ends within the measured width");
        check(std::fabs(float(inkMinY) - (10 + hTop)) <= 1 && std::fabs(float(inkMaxY - inkMinY + 1) - hH) <= 1.5f,
              "ink rows match vbounds");
        f.destroyGlyphs();
        SDL_DestroyRenderer(ren);
    }
    if (surf) SDL_FreeSurface(surf);

    std::printf(g_fail ? "chatfont_test: %d FAILED\n" : "chatfont_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
