#pragma once

// Minimal immediate-mode widgets for Cartographer's modal dialogs, drawn with
// the 5x7 bitmap font. The modal state + apply logic lives in the editor; these
// are just draw + hit-test helpers so each dialog stays a few lines.

#include "cartographer/font5x7.h"

#include <SDL.h>

#include <string>

namespace cart {

inline bool pointIn(int mx, int my, const SDL_Rect& r) {
    return mx >= r.x && my >= r.y && mx < r.x + r.w && my < r.y + r.h;
}

// A labelled text box. Draws the label above and the (optionally focused) value
// box; returns the value box rect so the caller can hit-test focus clicks.
inline SDL_Rect drawField(SDL_Renderer* ren, int x, int y, int w,
                          const std::string& label, const std::string& text,
                          bool focused) {
    drawText(ren, label, x, y, 1, 190, 195, 205);
    SDL_Rect box{x, y + 10, w, 16};
    SDL_SetRenderDrawColor(ren, focused ? 40 : 24, focused ? 44 : 26, focused ? 60 : 34, 255);
    SDL_RenderFillRect(ren, &box);
    SDL_SetRenderDrawColor(ren, focused ? 255 : 90, focused ? 210 : 92, focused ? 90 : 104, 255);
    SDL_RenderDrawRect(ren, &box);
    // Clip the text to the box; show the tail (caret end) when it overflows.
    SDL_RenderSetClipRect(ren, &box);
    int tw = textWidth(text, 1);
    int tx = tw + 6 <= w ? x + 4 : x + 4 - (tw + 6 - w);
    drawText(ren, text, tx, y + 14, 1, 230, 235, 245);
    if (focused) {   // caret
        int cx = tx + tw + 1;
        SDL_SetRenderDrawColor(ren, 255, 220, 120, 255);
        SDL_Rect c{cx, y + 13, 1, 9};
        SDL_RenderFillRect(ren, &c);
    }
    SDL_RenderSetClipRect(ren, nullptr);
    return box;
}

// A labelled dropdown box: draws the label above and a value box with a "v"
// marker on the right. `open` highlights it (its list is showing). Returns the
// box rect so the caller can hit-test the click that opens/closes the list.
inline SDL_Rect drawChoice(SDL_Renderer* ren, int x, int y, int w,
                           const std::string& label, const std::string& value,
                           bool open) {
    drawText(ren, label, x, y, 1, 190, 195, 205);
    SDL_Rect box{x, y + 10, w, 16};
    SDL_SetRenderDrawColor(ren, open ? 40 : 24, open ? 44 : 26, open ? 60 : 34, 255);
    SDL_RenderFillRect(ren, &box);
    SDL_SetRenderDrawColor(ren, open ? 255 : 90, open ? 210 : 92, open ? 90 : 104, 255);
    SDL_RenderDrawRect(ren, &box);
    drawText(ren, value, x + 4, y + 14, 1, 230, 235, 245);
    // Dropdown marker: a filled down-pointing triangle at the right edge, drawn as
    // stacked narrowing rows (SDL has no filled-triangle primitive).
    SDL_SetRenderDrawColor(ren, 210, 215, 225, 255);
    int ax = x + w - 12, ay = y + 15;
    for (int r = 0; r < 4; ++r) {
        SDL_Rect row{ax + r, ay + r, 8 - 2 * r, 1};
        SDL_RenderFillRect(ren, &row);
    }
    return box;
}

// A button; draws it and returns its rect (caller hit-tests clicks).
inline SDL_Rect drawButton(SDL_Renderer* ren, int x, int y, int w, int h,
                           const std::string& label, bool primary) {
    SDL_Rect r{x, y, w, h};
    SDL_SetRenderDrawColor(ren, primary ? 70 : 52, primary ? 90 : 54, primary ? 60 : 66, 255);
    SDL_RenderFillRect(ren, &r);
    SDL_SetRenderDrawColor(ren, primary ? 150 : 96, primary ? 200 : 98, primary ? 120 : 112, 255);
    SDL_RenderDrawRect(ren, &r);
    int lw = textWidth(label, 1);
    drawText(ren, label, x + (w - lw) / 2, y + (h - 7) / 2, 1,
             primary ? 255 : 210, primary ? 235 : 210, primary ? 200 : 220);
    return r;
}

// A modal panel: dim the screen, draw a titled box centred at (cx,cy). Returns
// the panel's content origin (top-left inside the border, below the title).
inline SDL_Rect drawPanel(SDL_Renderer* ren, int winW, int winH, int pw, int ph,
                          const std::string& title) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 150);
    SDL_Rect dim{0, 0, winW, winH};
    SDL_RenderFillRect(ren, &dim);
    SDL_Rect p{(winW - pw) / 2, (winH - ph) / 2, pw, ph};
    SDL_SetRenderDrawColor(ren, 40, 42, 52, 255);
    SDL_RenderFillRect(ren, &p);
    SDL_SetRenderDrawColor(ren, 120, 124, 140, 255);
    SDL_RenderDrawRect(ren, &p);
    SDL_Rect tb{p.x, p.y, p.w, 20};
    SDL_SetRenderDrawColor(ren, 60, 64, 80, 255);
    SDL_RenderFillRect(ren, &tb);
    drawText(ren, title, p.x + 8, p.y + 7, 1, 235, 235, 245);
    return SDL_Rect{p.x + 14, p.y + 30, p.w - 28, p.h - 40};
}

} // namespace cart
