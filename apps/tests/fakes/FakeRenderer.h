#pragma once

#include "openfear/platform/IRenderer.h"
#include <string>
#include <vector>

namespace openfear::testing {

// Headless IRenderer: never touches a real window/GPU. Records every
// requested bitmap path (and never fails to "load" one) so tests can assert
// which asset the game asked for, without needing the real copyrighted BMP
// files on disk.
class FakeRenderer : public IRenderer {
public:
    bool init(const char*) override { return true; }
    void shutdown() override {}
    void setCursor(platform::Cursor*) override {}

    void beginFrame() override {}
    void endFrame() override {}
    void clear(uint8_t, uint8_t, uint8_t) override {}

    TextureHandle loadBitmap(std::string_view path) override {
        loaded_bitmaps_.emplace_back(path);
        return next_handle_++;
    }

    TextureHandle loadBitmapFromMemory(const uint8_t*, size_t) override { return next_handle_++; }

    void drawBitmap(TextureHandle, int, int, int, int, int, int) override {}

    SpriteInfo createSprite(TextureHandle src) override { return SpriteInfo{src, 0, 0}; }
    void drawSprite(const SpriteInfo&, int, int, int, int, int, int) override {}

    void destroyTexture(TextureHandle) override {}

    void fillRect(int, int, int, int, uint8_t, uint8_t, uint8_t) override {}
    void drawLine(int, int, int, int, uint8_t, uint8_t, uint8_t) override {}
    void drawCursor(int, int) override {}

    std::vector<std::string> loaded_bitmaps_;

private:
    TextureHandle next_handle_ = 1;
};

}  // namespace openfear::testing
