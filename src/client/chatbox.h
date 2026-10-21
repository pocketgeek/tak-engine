#pragma once

// In-game chat box: the pure layout rules (word wrap, size clamp, placement), kept
// apart from GameView so they can be tested without a renderer. GameView's
// drawGameChat() measures text with the HUD font and draws what these return.
// Display only: nothing here touches the simulation or the network.

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace tak::chatbox {

// Options: CHAT SCALE (on top of UI SCALE) and CHAT POSITION.
inline constexpr float kMinScale = 0.5f, kMaxScale = 3.0f;
enum Align : int { Left = 0, Centered = 1 };

// How long a received line stays up while not typing, then how long it fades.
inline constexpr float kHoldSeconds = 12.0f, kFadeSeconds = 2.0f;

// Opacity of a line `age` seconds old (1 = fully shown, 0 = gone).
inline float lineAlpha(float age) {
    if (age <= kHoldSeconds) return 1.0f;
    return std::clamp(1.0f - (age - kHoldSeconds) / kFadeSeconds, 0.0f, 1.0f);
}

// Word-wrap `text` to `maxW` pixels. The first row has `firstW` pixels instead (the
// sender's name sits in front of it). A word longer than a whole row is broken
// between characters, never between the bytes of one UTF-8 character. Always returns
// at least one row (possibly empty).
inline std::vector<std::string> wrap(const std::string& text, float firstW, float maxW,
                                     const std::function<float(const std::string&)>& width) {
    std::vector<std::string> rows(1);
    auto limit = [&] { return rows.size() == 1 ? firstW : maxW; };
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ' ') {                        // a space: keep it unless the row is empty
            if (!rows.back().empty()) rows.back() += ' ';
            ++i;
            continue;
        }
        size_t e = text.find(' ', i);
        if (e == std::string::npos) e = text.size();
        std::string word = text.substr(i, e - i);
        i = e;
        if (width(rows.back() + word) <= limit()) { rows.back() += word; continue; }
        // Does not fit on this row: start a new one (dropping the trailing space)...
        if (!rows.back().empty()) {
            while (rows.back().back() == ' ') rows.back().pop_back();   // never only spaces
            rows.emplace_back();
        }
        if (width(word) <= limit()) { rows.back() = word; continue; }
        // ...and break a word that is wider than a whole row.
        for (size_t c = 0; c < word.size();) {
            size_t n = 1;
            while (c + n < word.size() && (static_cast<unsigned char>(word[c + n]) & 0xC0) == 0x80) ++n;
            std::string ch = word.substr(c, n);
            if (!rows.back().empty() && width(rows.back() + ch) > limit()) rows.emplace_back();
            rows.back() += ch;
            c += n;
        }
    }
    while (!rows.back().empty() && rows.back().back() == ' ') rows.back().pop_back();
    return rows;
}

// Where the box goes. `areaW` is the width of the play area (left of the command
// panel), `bottom` the y the box must stay above (the information bar's top), `top`
// the highest it may reach. `avoid` (w > 0) is a HUD strip to stay clear of -- the
// conjure/build row -- which the box moves up over when they would overlap.
struct Rect { float x = 0, y = 0, w = 0, h = 0; };
inline Rect place(int align, float boxW, float boxH, float areaW, float bottom, float top,
                  float margin, Rect avoid) {
    Rect r;
    r.w = std::min(boxW, std::max(0.0f, areaW - 2 * margin));
    r.h = boxH;
    r.x = align == Centered ? (areaW - r.w) / 2 : margin;
    r.x = std::max(r.x, margin);
    float b = bottom - margin;
    if (avoid.w > 0 && r.x < avoid.x + avoid.w && avoid.x < r.x + r.w && b > avoid.y - margin)
        b = avoid.y - margin;
    r.y = std::max(top, b - r.h);
    return r;
}

}  // namespace tak::chatbox
