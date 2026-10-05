#include "Ps2Renderer.h"
#include <cstdio>
#include <gsKit.h>

namespace openfear::sdl3 {

bool Ps2Renderer::init(const char* title) {
    // Ask for the game's native 640x480 instead of accepting the backend's
    // hardcoded 640x448 default. Height is region-dependent: NTSC honors
    // an explicit 480 request literally instead of self-clamping, so it
    // needs the smaller 448 (the known-safe NTSC height); PAL is safe at
    // the full 480.
    const bool is_ntsc = (gsKit_check_rom() == GS_MODE_NTSC);
    SDL_SetHint(SDL_HINT_PS2_GS_WIDTH, "640");
    SDL_SetHint(SDL_HINT_PS2_GS_HEIGHT, is_ntsc ? "448" : "480");
    if (!SDL3Renderer::init(title)) return false;

    gap_y_ = is_ntsc ? 32 : 0;
    setSafeAreaAnchor(SafeAreaAnchor::Center);

    std::fprintf(stderr, "[Ps2Renderer] region=%s gap_y_=%d crop_offset_y_=%d\n",
                 is_ntsc ? "NTSC" : "PAL", gap_y_, crop_offset_y_);
    return true;
}

void Ps2Renderer::setSafeAreaAnchor(SafeAreaAnchor anchor) {
    switch (anchor) {
        case SafeAreaAnchor::Bottom:
            // Full gap at the top (dst_y - gap_y_) - bottom edge lands
            // exactly on the last visible row, fully protected.
            crop_offset_y_ = gap_y_;
            break;
        case SafeAreaAnchor::Top:
            // Mirror of Bottom - no shift, so the gap falls at the bottom
            // instead.
            crop_offset_y_ = 0;
            break;
        case SafeAreaAnchor::Center:
        default:
            // Split evenly (rounds down, so an odd gap_y_ loses the extra
            // pixel off the top) - never happens today, gap_y_ is always 0
            // or 32, but keeps this correct if that changes.
            crop_offset_y_ = gap_y_ / 2;
            break;
    }
}

// A negative-Y viewport corrupts the scissor register and blacks out the
// screen on this backend, so every draw below subtracts crop_offset_y_
// from its own Y instead - same crop, without touching viewport/scissor.
void Ps2Renderer::drawBitmap(TextureHandle tex, int dst_x, int dst_y, int src_x, int src_y, int w,
                             int h) {
    SDL3Renderer::drawBitmap(tex, dst_x, dst_y - crop_offset_y_, src_x, src_y, w, h);
}

void Ps2Renderer::drawBitmapScaled(TextureHandle tex, int dst_x, int dst_y, int dst_w, int dst_h,
                                   int src_x, int src_y, int src_w, int src_h) {
    SDL3Renderer::drawBitmapScaled(tex, dst_x, dst_y - crop_offset_y_, dst_w, dst_h, src_x, src_y,
                                   src_w, src_h);
}

void Ps2Renderer::drawSpriteTinted(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x,
                                   int src_y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    SDL_Texture* t = get(sprite.handle);
    if (!t) return;

    SDL_BlendMode previous_blend = SDL_BLENDMODE_BLEND;
    SDL_GetTextureBlendMode(t, &previous_blend);
    SDL_SetTextureBlendMode(t,
                            hasRealAlpha(sprite.handle) ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL3Renderer::drawSpriteTinted(sprite, dst_x, dst_y - crop_offset_y_, src_x, src_y, w, h, r, g,
                                   b);
    SDL_SetTextureBlendMode(t, previous_blend);
}

void Ps2Renderer::drawSpriteAdditive(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x,
                                     int src_y, int w, int h, uint8_t alpha) {
    // This backend hardcodes BLENDMODE_ADD's alpha source to a fixed
    // 0x80, not the vertex alpha - the alpha parameter never reaches the
    // blend formula, at any value. Redirect the intended fade into the
    // source COLOR instead: Cs is what the ADD formula actually uses
    // (Cv = Cs*1.0 + Cd), and color channels are plain 0-255 magnitudes,
    // not a GS blend ratio, so no further rescale needed here.
    SDL_Texture* t = get(sprite.handle);
    if (t) SDL_SetTextureColorMod(t, alpha, alpha, alpha);
    SDL3Renderer::drawSpriteAdditive(sprite, dst_x, dst_y - crop_offset_y_, src_x, src_y, w, h,
                                     255);
    if (t) SDL_SetTextureColorMod(t, 255, 255, 255);
}

void Ps2Renderer::drawSpriteAlpha(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x,
                                  int src_y, int w, int h, uint8_t alpha) {
    SDL3Renderer::drawSpriteAlpha(sprite, dst_x, dst_y - crop_offset_y_, src_x, src_y, w, h, alpha);
}

void Ps2Renderer::fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    SDL3Renderer::fillRect(x, y - crop_offset_y_, w, h, r, g, b);
}

void Ps2Renderer::fillRectAdditive(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b,
                                   uint8_t a) {
    // This backend hardcodes BLENDMODE_ADD's alpha to a fixed 0x80 - fold
    // the intended fade into the source color instead.
    const uint8_t sr = static_cast<uint8_t>((static_cast<int>(r) * a) / 255);
    const uint8_t sg = static_cast<uint8_t>((static_cast<int>(g) * a) / 255);
    const uint8_t sb = static_cast<uint8_t>((static_cast<int>(b) * a) / 255);
    SDL3Renderer::fillRectAdditive(x, y - crop_offset_y_, w, h, sr, sg, sb, 255);
}

void Ps2Renderer::drawLine(int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b) {
    SDL3Renderer::drawLine(x1, y1 - crop_offset_y_, x2, y2 - crop_offset_y_, r, g, b);
}

void Ps2Renderer::drawCursor(int x, int y) {
    SDL3Renderer::drawCursor(x, y - crop_offset_y_);
}

}  // namespace openfear::sdl3
