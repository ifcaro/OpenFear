#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace openfear {

namespace platform {
class Cursor;
}
namespace assets {
class AssetManager;
}
class IAssetProvider;

struct Rect {
    int16_t x1, y1, x2, y2;
};
struct Point {
    int16_t x, y;
};

// Opaque handle for a loaded texture. 0 is always invalid.
using TextureHandle = uint32_t;

struct SpriteInfo {
    TextureHandle handle;
    int width;
    int height;
};

// Abstract 2D rendering backend. All coordinates use a fixed 640x480
// logical space. Concrete implementations (SDL3, or any future backend)
// live under src/platform/<backend>/ and are selected at CMake configure
// time — game code depends only on this interface.
class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual bool init(const char* title) = 0;
    virtual void shutdown() = 0;

    virtual void setCursor(platform::Cursor* cursor) = 0;

    // Source of file bytes for loadBitmap(). Must be set before any
    // loadBitmap() call. No-op default for backends that load bitmaps some
    // other way.
    virtual void setAssetProvider(IAssetProvider* provider) {}

    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;
    virtual void clear(uint8_t r = 0, uint8_t g = 0, uint8_t b = 0) = 0;

    // Loads a BMP file, returns 0 on failure.
    virtual TextureHandle loadBitmap(std::string_view path) = 0;

    // Decodes a BMP already in memory (e.g. compiled-in via bin2c) — for
    // OpenFear's own small assets, which don't live under the user-supplied
    // CD asset root and so can't go through loadBitmap()/IAssetProvider.
    // Returns 0 on failure.
    virtual TextureHandle loadBitmapFromMemory(const uint8_t* data, size_t size) = 0;

    // Blits a sub-region of a texture.
    virtual void drawBitmap(TextureHandle tex, int dst_x, int dst_y, int src_x, int src_y, int w,
                            int h) = 0;

    // Blits a sub-region of a texture scaled to an arbitrary destination
    // size — the only draw call in this interface where dst size can differ
    // from src size. Used for video playback (arbitrary AVI resolutions
    // blitted into game-defined rects); every other asset in the game is
    // pre-sized pixel art drawn 1:1 via drawBitmap. Default falls back to an
    // unscaled drawBitmap (ignores dst_w/dst_h) for backends that don't
    // implement it.
    virtual void drawBitmapScaled(TextureHandle tex, int dst_x, int dst_y, int dst_w, int dst_h,
                                  int src_x, int src_y, int src_w, int src_h) {
        drawBitmap(tex, dst_x, dst_y, src_x, src_y, src_w, src_h);
    }

    // Creates a texture meant for per-frame CPU pixel uploads (video
    // playback) rather than a one-time asset load. Returns 0 on failure or
    // if unsupported. Default: unsupported.
    virtual TextureHandle createStreamingTexture(int /*width*/, int /*height*/) { return 0; }

    // Uploads a full frame of pixel data to a texture created by
    // createStreamingTexture(). `rgba` is always RGBA32 byte order (R,G,B,A
    // per pixel in memory) regardless of backend — implementations convert
    // internally as needed. `stride` is bytes per row. No-op by default.
    virtual void updateTexture(TextureHandle /*tex*/, const uint8_t* /*rgba*/, int /*stride*/) {}

    // Wraps a texture as a sprite with alpha transparency.
    virtual SpriteInfo createSprite(TextureHandle src) = 0;

    // Blits a sprite with transparency.
    virtual void drawSprite(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x, int src_y,
                            int w, int h) = 0;

    // Blits a sprite with its RGB multiplied by a tint color (source should
    // be white/near-white for this to read as a plain recolor). Default
    // falls back to drawSprite (ignores the tint).
    virtual void drawSpriteTinted(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x,
                                  int src_y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
        drawSprite(sprite, dst_x, dst_y, src_x, src_y, w, h);
    }

    // Blits a sprite additively (dst += sprite * alpha/255, clamped) — can
    // brighten past the sprite's own color, unlike drawSpriteTinted which
    // can only darken. Default falls back to drawSprite (ignores alpha).
    virtual void drawSpriteAdditive(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x,
                                    int src_y, int w, int h, uint8_t alpha) {
        drawSprite(sprite, dst_x, dst_y, src_x, src_y, w, h);
    }

    // Blits a sprite with plain alpha blending (dst = sprite*alpha +
    // dst*(1-alpha)). Default falls back to drawSprite (ignores alpha).
    virtual void drawSpriteAlpha(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x,
                                 int src_y, int w, int h, uint8_t alpha) {
        drawSprite(sprite, dst_x, dst_y, src_x, src_y, w, h);
    }

    virtual void destroyTexture(TextureHandle tex) = 0;

    // Number of currently-live textures (leak-hunting diagnostic). Default
    // 0 for backends that don't track this.
    virtual size_t textureCount() const { return 0; }

    virtual void fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) = 0;

    // Fills a rect additively (dst += color*alpha, clamped — glows instead
    // of washing toward the fill color). Default falls back to fillRect
    // (ignores alpha, fully opaque).
    virtual void fillRectAdditive(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b,
                                  uint8_t a) {
        fillRect(x, y, w, h, r, g, b);
    }

    virtual void drawLine(int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b) = 0;

    // Fallback cursor draw (small crosshair) for backends without a real
    // cursor sprite loaded.
    virtual void drawCursor(int x, int y) = 0;

    // Sets which cursor sprite/frame to draw. Persists until changed; call
    // once per frame before endFrame().
    virtual void setGameCursorState(int set, int frame) {}

    // Loads the Win16 cursor resources (ATMOS.EXE) into drawable sprites.
    // No-op (returns false) by default.
    virtual bool loadGameCursors(assets::AssetManager* asset_mgr) { return false; }

    // Shows/hides the OS cursor. Backends that always hide it in favor of
    // their own drawn cursor (see drawCursor()) can override this for
    // screens with no game cursor loaded and no gameplay fidelity to
    // preserve, e.g. InstallFlow. No-op by default.
    virtual void setCursorVisible(bool visible) {}

    // Positions the shared cursor sprite over a gamepad-focused UI element's
    // center and makes it visible for this frame, drawn through the
    // logical/game-space path (not the native-window-pixel path a real
    // mouse uses — see endFrame()'s in_game_area branch). Call once per
    // frame, after render()'s own draw calls, from any screen using
    // discrete D-pad focus nav (FocusIndicator::DrawFocusCursor). No-op on
    // backends with no cursor wired (e.g. no platform::Cursor set).
    virtual void setGamepadCursorPosition(int x, int y) {}

    // Where to bias content within the logical 640x480 canvas when a
    // backend can't display all of it (e.g. PS2 NTSC overscan, which can't
    // show the full canvas safely — see Ps2Renderer). Bottom keeps the
    // bottom edge fully visible (loses from the top instead) for screens
    // whose critical UI sits there (e.g. the board's key/step-counter
    // row); Top is the mirror case; Center splits the loss evenly, the
    // default. No-op on backends with nothing to crop.
    enum class SafeAreaAnchor { Center, Top, Bottom };
    virtual void setSafeAreaAnchor(SafeAreaAnchor) {}
};

}  // namespace openfear
