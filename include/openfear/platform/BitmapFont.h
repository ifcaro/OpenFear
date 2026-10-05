#pragma once
#include "IRenderer.h"

namespace openfear::platform {

// Fixed-width bitmap font, embedded via bin2c (data/fonts/basicfont.bmp).
// Port-only utility for screens with no game assets loaded yet (fatal
// errors, the installer's progress screen) — not a general in-game text
// system; every real game screen renders text via baked plaque/portrait
// art, matching the original.
//
// Uppercase only, plus digits/punctuation and a Western-European accented
// set (Spanish/French/Portuguese/German/Italian), kept deliberately small.
// Glyphs are Spleen 8x16 (github.com/fcambus/spleen, BSD 2-Clause).
// DrawText() takes UTF-8; characters outside this font's charset render as
// a blank cell.
class BitmapFont {
public:
    static constexpr int kGlyphWidth = 8;
    static constexpr int kGlyphHeight = 16;
    // Extra gap drawn between characters - Spleen's glyphs fill their full
    // cell width edge to edge (by design, for a terminal grid), which
    // reads as too cramped/touching for prose text at this size. Purely a
    // draw-time advance, not baked into the atlas.
    static constexpr int kCharSpacing = 2;

    // Decodes the embedded font strip into a texture. Call once; safe to
    // call again after Unload().
    bool Load(IRenderer& renderer);
    void Unload(IRenderer& renderer);

    // Draws left-aligned UTF-8 text starting at (x, y). No line-wrapping —
    // split multi-line callers across separate calls.
    void DrawText(IRenderer& renderer, int x, int y, const char* text, uint8_t r, uint8_t g,
                  uint8_t b) const;

    // Like DrawText, but hard-splits into multiple lines by codepoint count
    // so `text` never draws past x+max_width - for status/error screens
    // that interpolate an arbitrary path of unknown length. Not a real
    // word-wrap.
    void DrawTextWrapped(IRenderer& renderer, int x, int y, const char* text, int max_width,
                         uint8_t r, uint8_t g, uint8_t b) const;

    // Pixel width of `text` (counted in decoded UTF-8 codepoints, not
    // bytes) at this font's fixed glyph width — for centering a line
    // before drawing it.
    int TextWidth(const char* text) const;

private:
    SpriteInfo sprite_{};
    TextureHandle tex_ = 0;
};

}  // namespace openfear::platform
