#pragma once
#include "SDL3Renderer.h"

namespace openfear::sdl3 {

// PS2-specific renderer. Wraps SDL3Renderer, which stays a plain,
// generic SDL3 binding — home for GS-hardware-only rendering fixes.
class Ps2Renderer final : public SDL3Renderer {
public:
    bool init(const char* title) override;

    void drawBitmap(TextureHandle tex, int dst_x, int dst_y, int src_x, int src_y, int w,
                    int h) override;
    void drawBitmapScaled(TextureHandle tex, int dst_x, int dst_y, int dst_w, int dst_h, int src_x,
                          int src_y, int src_w, int src_h) override;
    void drawSpriteTinted(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x, int src_y,
                          int w, int h, uint8_t r, uint8_t g, uint8_t b) override;
    void drawSpriteAdditive(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x, int src_y,
                            int w, int h, uint8_t alpha) override;
    void drawSpriteAlpha(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x, int src_y,
                         int w, int h, uint8_t alpha) override;
    void fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) override;
    void fillRectAdditive(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b,
                          uint8_t a) override;
    void drawLine(int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b) override;
    void drawCursor(int x, int y) override;

    // See IRenderer::SafeAreaAnchor. Recomputes crop_offset_y_ from the
    // fixed gap_y_ and the new anchor - cheap, no hardware/viewport touched.
    void setSafeAreaAnchor(SafeAreaAnchor anchor) override;

protected:
    // PS2_CreateTexture() (SDL_render_ps2.c) never checks the access mode,
    // so TEXTUREACCESS_TARGET always succeeds - but PS2_SetRenderTarget()
    // is a literal `{ return true; }` stub, every draw lands on the real
    // framebuffer regardless. A render-to-texture game_target_ would get
    // nothing, and blitting it (empty/black) would cover up everything
    // that rendered correctly underneath.
    bool supportsRenderTargetTexture() const override { return false; }

private:
    // Pixels lost to cropping - fixed per region (0 PAL/480, 32 NTSC/448),
    // set once in init(). Not measured after the fact - the real
    // post-init height isn't reliably readable back on this hardware.
    int gap_y_ = 32;

    // Y shift applied before every draw reaches SDL3Renderer - derived
    // from gap_y_ and the current SafeAreaAnchor (default Center, split
    // evenly). Bottom anchor protects the bottom fully but looks
    // unbalanced without bottom-critical UI, so screens opt in explicitly
    // (e.g. BoardGameScreen's bottom key/step row). 32 here is just the
    // pre-init() default.
    int crop_offset_y_ = 32;
};

}  // namespace openfear::sdl3
