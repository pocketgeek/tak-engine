#pragma once

// Bitmap font loaded from a GAF sequence (one frame per glyph), drawn with
// per-glyph vertical offsets. Extracted from client/main.cpp; kept at global
// scope so its existing unqualified use sites there are unchanged.

#include <SDL.h>

#include <string>

namespace tak::hpi { class Vfs; }

class Font {
public:
    Font() = default;
    Font(SDL_Renderer* ren, const tak::hpi::Vfs& vfs, const std::string& gafPath);
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;
    Font(Font&&) noexcept;
    Font& operator=(Font&&) noexcept;

    bool ok() const { return ok_; }
    void setLetterSpacing(float pixels) { letterSpacing_ = pixels; }

    int width(const std::string& text, float scale = 1) const;

    // Tallest glyph cell, for sizing a backing panel behind a line of text.
    int height(float scale = 1) const;

    // Where `text` actually renders vertically, relative to the `y` passed to draw():
    // its pixels occupy [y + topOff, y + topOff + h]. draw() lifts each glyph by its
    // yoff, so topOff is usually NEGATIVE (the text sits ABOVE y). Used to draw a
    // backing box that truly wraps the text instead of sitting below it.
    void vbounds(const std::string& text, float scale, float& topOff, float& h) const;

    // Free the glyph textures (gpuvram-accounted). Called at session teardown so
    // the VRAM budget doesn't leak across menu->game->menu loops; the font is
    // unusable afterwards until reconstructed. Also called by the destructor;
    // explicit cleanup is safe and must precede destroying the SDL renderer.
    void destroyGlyphs();

    void draw(SDL_Renderer* ren, const std::string& text, float x, float y,
              float scale = 1, SDL_Color tint = {255, 255, 255, 255}) const;

    // draw() in one flat colour: every opaque pixel of the glyph becomes exactly
    // `color`. The retail glyph art is shaded tan, so a tint through draw() comes out
    // a mottled, dark-edged version of the colour that is hard to read over terrain
    // (the under-unit squad marker); this keeps the same shapes, size and placement.
    void drawSolid(SDL_Renderer* ren, const std::string& text, float x, float y,
                   float scale, SDL_Color color) const;

private:
    struct Glyph {
        SDL_Texture* tex = nullptr;
        SDL_Texture* mask = nullptr;   // the glyph's coverage in white, for drawSolid()
        int w = 0, h = 0, yoff = 0;
    };
    float advance(const Glyph& g) const;
    void drawWith(SDL_Renderer* ren, const std::string& text, float x, float y,
                  float scale, SDL_Color tint, bool solid) const;
    float letterSpacing_ = 2;
    Glyph glyphs_[256] = {};
    bool ok_ = false;
};
