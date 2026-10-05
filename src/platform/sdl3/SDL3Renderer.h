#pragma once
#include "openfear/platform/IRenderer.h"
#include <SDL3/SDL.h>
#include <unordered_map>
#include <vector>

namespace openfear::sdl3 {

// Not final: Ps2Renderer subclasses this for GS-hardware-specific fixes.
class SDL3Renderer : public IRenderer {
public:
    ~SDL3Renderer() override;

    bool init(const char* title) override;
    void shutdown() override;

    void setCursor(platform::Cursor* cursor) override { cursor_ = cursor; }
    void setAssetProvider(IAssetProvider* provider) override { asset_provider_ = provider; }

    void beginFrame() override;
    void endFrame() override;
    void clear(uint8_t r, uint8_t g, uint8_t b) override;

    TextureHandle loadBitmap(std::string_view path) override;
    TextureHandle loadBitmapFromMemory(const uint8_t* data, size_t size) override;
    void drawBitmap(TextureHandle tex, int dst_x, int dst_y, int src_x, int src_y, int w,
                    int h) override;
    void drawBitmapScaled(TextureHandle tex, int dst_x, int dst_y, int dst_w, int dst_h, int src_x,
                          int src_y, int src_w, int src_h) override;

    TextureHandle createStreamingTexture(int width, int height) override;
    void updateTexture(TextureHandle tex, const uint8_t* rgba, int stride) override;

    SpriteInfo createSprite(TextureHandle src) override;
    void drawSprite(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x, int src_y, int w,
                    int h) override;

    void drawSpriteTinted(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x, int src_y,
                          int w, int h, uint8_t r, uint8_t g, uint8_t b) override;

    void drawSpriteAdditive(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x, int src_y,
                            int w, int h, uint8_t alpha) override;

    void drawSpriteAlpha(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x, int src_y,
                         int w, int h, uint8_t alpha) override;

    void destroyTexture(TextureHandle tex) override;
    size_t textureCount() const override { return textures_.size(); }

    void fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) override;
    void fillRectAdditive(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b,
                          uint8_t a) override;
    void drawLine(int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b) override;
    void drawCursor(int x, int y) override;
    // Same as drawCursor(), but scales the sprite's own pixel size by
    // `scale` - used for the mouse-driven native-window draw (endFrame()),
    // which has no automatic scaling applied.
    void drawCursorScaled(int x, int y, float scale);
    void setGameCursorState(int set, int frame) override;

    bool loadGameCursors(assets::AssetManager* asset_mgr) override;

    void setGamepadCursorPosition(int x, int y) override;

    void setCursorVisible(bool visible) override;

    // Exposed so SDL3Input can toggle fullscreen on the same window.
    SDL_Window* window() const { return window_; }

    // Aspect-preserving centered rect game_target_ blits into. SDL3Input
    // uses this to convert real mouse coordinates into 640x480 game space.
    SDL_Rect getLetterboxRect() const;

    // False only in the SDL_SetRenderLogicalPresentation fallback path -
    // SDL3Input needs to know, since events arrive pre-transformed there
    // (SDL#1397) unlike SDL_GetMouseState().
    bool usesRenderTarget() const { return use_render_target_; }

protected:
    // Lets a subclass inspect/adjust a texture's blend mode without
    // duplicating the texture map.
    SDL_Texture* get(TextureHandle h) const;

    // True if this texture was decoded from a genuinely-alpha source (32bpp
    // BMP, e.g. data/fonts/digitfont.bmp) rather than an opaque CD asset
    // (1/8/24bpp, alpha=255 baked in but meaningless) - lets a subclass
    // know whether a draw can safely force BLENDMODE_NONE (opaque asset,
    // straight copy) or must preserve real per-pixel alpha instead.
    bool hasRealAlpha(TextureHandle h) const;

    // False on backends whose SDL_SetRenderTarget() doesn't actually work
    // (see Ps2Renderer's override for why) - init() then skips the
    // render-to-texture path entirely instead of drawing into a texture
    // nothing ever presents. True (the render-to-texture path works) by
    // default.
    virtual bool supportsRenderTargetTexture() const { return true; }

private:
    // Shared tail of drawSpriteAdditive()/drawSpriteAlpha() - identical
    // save/set/draw/restore sequence, differing only in the blend mode.
    void drawSpriteBlended(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x, int src_y,
                           int w, int h, uint8_t alpha, SDL_BlendMode mode);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* game_target_ = nullptr;  // fixed 640x480 render target, see init()
    bool use_render_target_ = true;       // false if creation failed or was skipped (see init())
    platform::Cursor* cursor_ = nullptr;
    IAssetProvider* asset_provider_ = nullptr;

    TextureHandle next_handle_ = 1;
    std::unordered_map<TextureHandle, SDL_Texture*> textures_;
    std::unordered_map<TextureHandle, bool> has_real_alpha_;

    // Reused across updateTexture() calls when the destination texture's
    // real format isn't RGBA32 - avoids a per-frame allocation for video.
    std::vector<uint8_t> convert_scratch_;

    // Game cursors: 3 sets (0=turn, 1=advance, 2=idle/default) x 3
    // animation frames each.
    SDL_Texture* game_cursors_[3][3] = {};
    bool cursors_loaded_ = false;
    int game_cursor_set_ = 0;
    int game_cursor_frame_ = 2;
    // True after setCursorVisible(true) - the real OS pointer is in use instead of the game's
    // own cursor sprite (or its yellow-crosshair fallback), so endFrame() must not draw either.
    bool os_cursor_visible_ = false;

    SDL_Texture* CreateTextureAndFreeSurface(SDL_Surface* surface);
    void drawCursorImpl(int x, int y, float scale);
};

}  // namespace openfear::sdl3
