#include "CursorParser.h"
#include <cstring>
#include <cstdio>

namespace openfear::sdl3 {

namespace {
uint16_t read_u16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
int32_t read_i32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
}  // namespace

bool CursorParser::ExtractCursorDimensions(const uint8_t* data, uint32_t size, int& out_width,
                                           int& out_height) {
    if (size < 0x44) return false;

    // The height includes both the XOR and AND masks stacked vertically.
    int32_t width = read_i32(data + 8);
    int32_t height = read_i32(data + 12);

    if (height < 0) height = -height;
    height /= 2;

    if (width <= 0 || width > 256 || height <= 0 || height > 256) {
        std::fprintf(stderr, "[CursorParser] Bad dimensions: w=%d h=%d\n", width, height);
        return false;
    }

    out_width = width;
    out_height = height;
    return true;
}

SDL_Surface* CursorParser::ParseWin16Cursor(const uint8_t* data, uint32_t size) {
    if (!data || size < 0x34) {
        std::fprintf(stderr, "[CursorParser] Invalid cursor data\n");
        return nullptr;
    }

    int width = 0, height = 0;
    if (!ExtractCursorDimensions(data, size, width, height)) {
        return nullptr;
    }

    // Win16 cursor layout:
    // +00-03:  hotspot_x, hotspot_y
    // +04-43:  BITMAPINFOHEADER (40 bytes)
    // +44-51:  color table: 2 × RGBQUAD (1-bit monochrome)
    // +52+:    XOR mask (height rows), then AND mask (height rows)
    const uint32_t color0 = read_u16(data + 44) | (read_u16(data + 46) << 16);
    const uint32_t color1 = read_u16(data + 48) | (read_u16(data + 50) << 16);

    // RGBQUAD is B,G,R,reserved — pack into SDL's RGBA32 with full alpha.
    auto rgbquad_to_rgba = [](uint32_t quad) -> uint32_t {
        uint8_t b = (quad >> 0) & 0xFF;
        uint8_t g = (quad >> 8) & 0xFF;
        uint8_t r = (quad >> 16) & 0xFF;
        return (0xFFu << 24) | (r << 16) | (g << 8) | b;
    };
    const uint32_t px_color0 = rgbquad_to_rgba(color0);
    const uint32_t px_color1 = rgbquad_to_rgba(color1);

    const int row_bytes = ((width + 31) / 32) * 4;
    const uint8_t* xor_mask = data + 52;
    const uint8_t* and_mask = xor_mask + (row_bytes * height);

    // Byte order 0x000000FF/0x0000FF00/0x00FF0000/0xFF000000 (R,G,B,A masks)
    // is exactly SDL_PIXELFORMAT_RGBA32 - SDL3 removed the mask-based
    // SDL_CreateRGBSurface() in favor of naming the format directly.
    SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        std::fprintf(stderr, "[CursorParser] SDL_CreateSurface failed\n");
        return nullptr;
    }

    uint32_t* pixels = static_cast<uint32_t*>(surface->pixels);
    const uint32_t pitch = surface->pitch / 4;

    // Win16 cursor compositing: AND=1 -> transparent, AND=0 -> opaque,
    // XOR selects which of the 2 colors when opaque.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int byte_idx = x / 8;
            const int bit_idx = 7 - (x % 8);

            const uint8_t and_bit = (and_mask[y * row_bytes + byte_idx] >> bit_idx) & 1;
            const uint8_t xor_bit = (xor_mask[y * row_bytes + byte_idx] >> bit_idx) & 1;

            pixels[y * pitch + x] =
                (and_bit == 1) ? 0x00000000 : (xor_bit == 0 ? px_color0 : px_color1);
        }
    }

    return surface;
}

}  // namespace openfear::sdl3
