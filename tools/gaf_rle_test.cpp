// gaf_rle_test — the RLE (encoding 1) frame decoder on synthetic GAF buffers: valid
// transparent/repeat/literal runs decode to exact pixels, and every malformed run is
// rejected with the parser's runtime_error before any byte outside its scanline (or the
// buffer) is read. Feature GAFs arrive in peers' map packages, so this is untrusted input.

#include "gaf/gaf.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint8_t kTransparent = 9;

void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

// One-entry, one-frame GAF whose RLE data (dataPtr 88) is `rle`, with nothing after it
// unless the caller appends bytes.
std::vector<uint8_t> rleGaf(uint16_t w, uint16_t h, const std::vector<uint8_t>& rle) {
    std::vector<uint8_t> d(88);
    auto u16 = [&](size_t at, uint16_t v) { d[at] = uint8_t(v); d[at + 1] = uint8_t(v >> 8); };
    auto u32 = [&](size_t at, uint32_t v) { u16(at, uint16_t(v)); u16(at + 2, uint16_t(v >> 16)); };
    u32(0, 0x00010100); u32(4, 1); u32(12, 16);      // version, 1 entry at 16
    u16(16, 1); std::memcpy(&d[24], "rle", 3);       // 1 frame, name
    u32(56, 64); u32(60, 2);                         // frame record at 64, delay 2
    u16(64, w); u16(66, h);
    d[72] = kTransparent; d[73] = 1;                 // transparency index, RLE encoding
    u32(80, 88);                                     // dataPtr
    d.insert(d.end(), rle.begin(), rle.end());
    return {d.begin(), d.end()};  // exact capacity, so ASan sees any read past the end
}

tak::gaf::Palette palette() {
    std::vector<uint8_t> raw(1024);
    for (int i = 0; i < 256; ++i) {
        raw[size_t(i) * 4] = uint8_t(i);
        raw[size_t(i) * 4 + 1] = uint8_t(255 - i);
        raw[size_t(i) * 4 + 2] = uint8_t(i ^ 0x5A);
    }
    return tak::gaf::Palette::fromBytes(raw);
}

void expectRejected(const std::vector<uint8_t>& gaf, const char* what) {
    try {
        tak::gaf::load(gaf, palette(), -1, what);
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error(std::string("malformed RLE accepted: ") + what);
}

void validRuns() {
    // 8x4 frame. Codes: odd = skip code>>1; bit 1 = repeat (code>>2)+1 of the next byte;
    // else literal (code>>2)+1 bytes. Runs past the width are clipped, not rejected.
    const std::vector<uint8_t> rle = {
        6, 0,  0x05, 0x0A, 7, 0x04, 1, kTransparent,  // skip 2, 3x colour 7, literal {1, transparent}
        5, 0,  0x00, 200, 0x06, kTransparent, 0x0B,   // literal {200}, 2x transparent, skip 5
        0, 0,                                         // empty line
        7, 0,  0x0B, 0x01, 0x0C, 30, 31, 32, 33,      // skip 5, skip 0, literal 4 from x=5 (1 clipped)
    };
    const int expected[4][8] = {
        {-1, -1, 7, 7, 7, 1, -1, -1},
        {200, -1, -1, -1, -1, -1, -1, -1},
        {-1, -1, -1, -1, -1, -1, -1, -1},
        {-1, -1, -1, -1, -1, 30, 31, 32},
    };
    const auto pal = palette();
    const auto seqs = tak::gaf::load(rleGaf(8, 4, rle), pal, -1, "valid");
    check(seqs.size() == 1 && seqs[0].frames.size() == 1, "valid: one sequence, one frame");
    const auto& fr = seqs[0].frames[0];
    check(fr.width == 8 && fr.height == 4 && fr.rgba.size() == 8 * 4 * 4, "valid: frame size");
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 8; ++x) {
            const uint8_t* px = &fr.rgba[(size_t(y) * 8 + x) * 4];
            const int idx = expected[y][x];
            const uint8_t want[4] = {idx < 0 ? uint8_t(0) : pal.rgba[idx][0],
                                     idx < 0 ? uint8_t(0) : pal.rgba[idx][1],
                                     idx < 0 ? uint8_t(0) : pal.rgba[idx][2],
                                     uint8_t(idx < 0 ? 0 : 255)};
            check(std::memcmp(px, want, 4) == 0,
                  "valid: pixel (" + std::to_string(x) + "," + std::to_string(y) + ") differs");
        }

    // A run ending exactly on the scanline's last byte with bytes after it (next line).
    const auto twoLines = tak::gaf::load(rleGaf(2, 2, {2, 0, 0x06, 4, 2, 0, 0x06, 5, 0xEE}), pal);
    const auto& t = twoLines[0].frames[0].rgba;
    check(t[0] == 4 && t[4] == 4 && t[8] == 5 && t[12] == 5 && t[15] == 255,
          "valid: repeat runs that end on their scanline boundary");
}

void malformedRuns() {
    // Operands missing at the very end of the buffer (the scanline is declared whole).
    expectRejected(rleGaf(4, 1, {1, 0, 0x02}), "repeat without its colour byte");
    expectRejected(rleGaf(4, 1, {3, 0, 0x0C, 1, 2}), "literal of 4 with 2 bytes");
    expectRejected(rleGaf(4, 1, {2, 0, 0x01, 0x00}), "literal of 1 without its byte");

    // The operand bytes exist in the file but belong to the next scanline.
    expectRejected(rleGaf(4, 2, {1, 0, 0x02, 2, 0, 0x02, 3}), "repeat crossing its scanline");
    expectRejected(rleGaf(4, 2, {2, 0, 0x08, 5, 2, 0, 0x02, 3}), "literal crossing its scanline");
    expectRejected(rleGaf(4, 1, {2, 0, 0x08, 5, 6, 7, 8, 9}), "literal crossing the last scanline");

    // Truncated scanline headers and declared scanlines.
    expectRejected(rleGaf(4, 2, {2, 0, 0x02, 3, 4}), "half a scanline header");
    expectRejected(rleGaf(4, 2, {2, 0, 0x02, 3}), "missing scanline header");
    expectRejected(rleGaf(4, 1, {10, 0, 0x02, 3}), "declared scanline longer than the file");
    expectRejected(rleGaf(4, 1, {0xFF, 0xFF}), "65535-byte scanline in a short file");
}

} // namespace

int main() try {
    validRuns();
    malformedRuns();
    std::cout << "gaf_rle: ok\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "gaf_rle: FAIL: " << e.what() << "\n";
    return 1;
}
