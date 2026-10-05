#include "SDL3Renderer.h"
#include "CursorParser.h"
#include "openfear/platform/Cursor.h"
#include "openfear/assets/AssetManager.h"
#include "openfear/assets/IAssetProvider.h"
#include <cassert>
#include <string>
#include <vector>
#include <fstream>
#include <cstdio>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace openfear::sdl3 {

// SZDD (Microsoft COMPRESS.EXE) decompressor - used by CHARS/*.BMP.
// Header: "SZDD\x88\xF0\x27\x33" + 1 byte type + 1 byte ext + 4 byte orig
// size. Algorithm: LZSS, 4096-byte window pre-filled with spaces (0x20),
// window pointer starts at 4096-16.
static bool szdd_decompress(const uint8_t* src, size_t src_len, std::vector<uint8_t>& out) {
    static const uint8_t MAGIC[8] = {0x53, 0x5A, 0x44, 0x44, 0x88, 0xF0, 0x27, 0x33};
    if (src_len < 14) return false;
    for (int i = 0; i < 8; ++i)
        if (src[i] != MAGIC[i]) return false;

    uint32_t orig_size = static_cast<uint32_t>(src[10]) | (static_cast<uint32_t>(src[11]) << 8) |
                         (static_cast<uint32_t>(src[12]) << 16) |
                         (static_cast<uint32_t>(src[13]) << 24);
    out.reserve(orig_size);

    uint8_t window[4096];
    std::fill(window, window + 4096, uint8_t(0x20));
    int wpos = 4096 - 16;

    size_t pos = 14;
    while (pos < src_len) {
        uint8_t ctrl = src[pos++];
        for (int bit = 0; bit < 8 && pos < src_len; ++bit) {
            if (ctrl & (1u << bit)) {
                uint8_t c = src[pos++];
                out.push_back(c);
                window[wpos] = c;
                wpos = (wpos + 1) & 0xFFF;
            } else {
                if (pos + 1 >= src_len) break;
                uint8_t b0 = src[pos++], b1 = src[pos++];
                int mpos = b0 | ((b1 & 0xF0) << 4);
                int mlen = (b1 & 0x0F) + 3;
                for (int k = 0; k < mlen; ++k) {
                    uint8_t c = window[mpos & 0xFFF];
                    out.push_back(c);
                    window[wpos] = c;
                    mpos = (mpos + 1) & 0xFFF;
                    wpos = (wpos + 1) & 0xFFF;
                }
            }
        }
    }
    return true;
}

// Own BMP decoder - the only image format this project loads, no
// SDL2_image dependency. Covers exactly what the asset set needs: the
// original CD art is 1bpp/8bpp uncompressed, 8bpp RLE8, or 24bpp
// uncompressed; this project's own data/fonts/digitfont.bmp is 32bpp
// uncompressed with a real alpha channel (anti-aliased digit edges).
static SDL_Surface* decode_bmp(const uint8_t* data, size_t size) {
    if (size < 54 || data[0] != 'B' || data[1] != 'M') return nullptr;

    auto u16 = [&](size_t off) -> uint16_t {
        uint16_t v;
        std::memcpy(&v, data + off, 2);
        return v;
    };
    auto u32 = [&](size_t off) -> uint32_t {
        uint32_t v;
        std::memcpy(&v, data + off, 4);
        return v;
    };
    auto i32 = [&](size_t off) -> int32_t {
        int32_t v;
        std::memcpy(&v, data + off, 4);
        return v;
    };

    const uint32_t data_offset = u32(10);
    const uint32_t header_size = u32(14);
    if (header_size < 40 || 14ull + header_size > size) return nullptr;

    const int32_t width = i32(18);
    const int32_t raw_height = i32(22);
    const uint16_t bit_count = u16(28);
    const uint32_t compression = u32(30);
    const uint32_t colors_used = u32(46);

    if (width <= 0 || raw_height == 0) return nullptr;
    if (bit_count != 1 && bit_count != 8 && bit_count != 24 && bit_count != 32) return nullptr;
    if (compression != 0 && !(bit_count == 8 && compression == 1)) return nullptr;
    if (data_offset >= size) return nullptr;

    const bool top_down = raw_height < 0;
    const int height = top_down ? -raw_height : raw_height;

    const uint8_t* palette = nullptr;
    uint32_t palette_count = 0;
    if (bit_count <= 8) {
        palette_count = colors_used ? colors_used : (1u << bit_count);
        const uint64_t palette_offset = 14ull + header_size;
        if (palette_offset + static_cast<uint64_t>(palette_count) * 4 > size) return nullptr;
        palette = data + palette_offset;
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 0);
    auto put_pixel = [&](int x, int stream_row, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF) {
        if (x < 0 || x >= width || stream_row < 0 || stream_row >= height) return;
        const int row = top_down ? stream_row : (height - 1 - stream_row);
        uint8_t* p = &pixels[(static_cast<size_t>(row) * width + x) * 4];
        p[0] = r;
        p[1] = g;
        p[2] = b;
        p[3] = a;
    };
    auto palette_rgb = [&](uint32_t idx, uint8_t& r, uint8_t& g, uint8_t& b) {
        if (idx >= palette_count) {
            r = g = b = 0;
            return;
        }
        const uint8_t* e = palette + static_cast<size_t>(idx) * 4;
        b = e[0];
        g = e[1];
        r = e[2];
    };

    if (compression == 1) {
        // BI_RLE8 (Microsoft spec): pairs of (count, index) runs, or
        // (0, escape) - 0=end of line, 1=end of bitmap, 2=delta(dx,dy),
        // >=3=absolute mode (that many literal indices, word-padded).
        size_t pos = data_offset;
        int x = 0, row = 0;
        while (pos + 1 < size) {
            const uint8_t b0 = data[pos], b1 = data[pos + 1];
            pos += 2;
            if (b0 != 0) {
                uint8_t r, g, b;
                palette_rgb(b1, r, g, b);
                for (uint8_t k = 0; k < b0; ++k) put_pixel(x++, row, r, g, b);
            } else if (b1 == 0) {
                x = 0;
                ++row;
            } else if (b1 == 1) {
                break;
            } else if (b1 == 2) {
                if (pos + 1 >= size) break;
                x += data[pos];
                row += data[pos + 1];
                pos += 2;
            } else {
                const uint8_t count = b1;
                for (uint8_t k = 0; k < count; ++k) {
                    if (pos >= size) break;
                    uint8_t r, g, b;
                    palette_rgb(data[pos++], r, g, b);
                    put_pixel(x++, row, r, g, b);
                }
                if (count & 1) ++pos;
            }
        }
    } else if (bit_count == 1) {
        const size_t stride = ((static_cast<size_t>(width) + 31) / 32) * 4;
        for (int row = 0; row < height; ++row) {
            const size_t row_off = data_offset + static_cast<size_t>(row) * stride;
            if (row_off + stride > size) break;
            for (int x = 0; x < width; ++x) {
                const uint8_t byte = data[row_off + x / 8];
                const uint32_t idx = (byte >> (7 - (x % 8))) & 1;
                uint8_t r, g, b;
                palette_rgb(idx, r, g, b);
                put_pixel(x, row, r, g, b);
            }
        }
    } else if (bit_count == 8) {
        const size_t stride = ((static_cast<size_t>(width) + 3) / 4) * 4;
        for (int row = 0; row < height; ++row) {
            const size_t row_off = data_offset + static_cast<size_t>(row) * stride;
            if (row_off + stride > size) break;
            for (int x = 0; x < width; ++x) {
                uint8_t r, g, b;
                palette_rgb(data[row_off + x], r, g, b);
                put_pixel(x, row, r, g, b);
            }
        }
    } else if (bit_count == 24) {
        const size_t stride = ((static_cast<size_t>(width) * 3 + 3) / 4) * 4;
        for (int row = 0; row < height; ++row) {
            const size_t row_off = data_offset + static_cast<size_t>(row) * stride;
            if (row_off + stride > size) break;
            for (int x = 0; x < width; ++x) {
                const uint8_t* px = data + row_off + static_cast<size_t>(x) * 3;
                put_pixel(x, row, px[2], px[1], px[0]);
            }
        }
    } else {  // 32bpp, BGRA - already 4-byte aligned, no row padding.
        const size_t stride = static_cast<size_t>(width) * 4;
        for (int row = 0; row < height; ++row) {
            const size_t row_off = data_offset + static_cast<size_t>(row) * stride;
            if (row_off + stride > size) break;
            for (int x = 0; x < width; ++x) {
                const uint8_t* px = data + row_off + static_cast<size_t>(x) * 4;
                put_pixel(x, row, px[2], px[1], px[0], px[3]);
            }
        }
    }

    SDL_Surface* surf = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return nullptr;
    for (int row = 0; row < height; ++row) {
        std::memcpy(static_cast<uint8_t*>(surf->pixels) + static_cast<size_t>(row) * surf->pitch,
                    pixels.data() + static_cast<size_t>(row) * width * 4,
                    static_cast<size_t>(width) * 4);
    }
    if (bit_count != 32) {
        // Every non-32bpp asset is fully opaque (no real alpha channel in
        // the source data) - keep blend mode NONE like a straight-copy BMP,
        // not the BLEND that a format with an alpha channel gets by default.
        SDL_SetSurfaceBlendMode(surf, SDL_BLENDMODE_NONE);
    }
    return surf;
}

static constexpr int GAME_W = 640;
static constexpr int GAME_H = 480;

SDL3Renderer::~SDL3Renderer() {
    shutdown();
}

bool SDL3Renderer::init(const char* title) {
    if (!SDL_Init(SDL_INIT_VIDEO)) return false;

    // Pass click-to-focus events through so the window's first click isn't
    // consumed by the OS focus change (Windows default behavior).
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

    // SDL3 windows are shown by default and no longer take a position in
    // SDL_CreateWindow() - centered explicitly right after.
    window_ = SDL_CreateWindow(title, GAME_W, GAME_H, SDL_WINDOW_RESIZABLE);
    if (!window_) return false;
    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    // Acceleration is the default (no SDL_RENDERER_ACCELERATED flag in SDL3
    // - driver selection is by name, NULL picks the first that supports a
    // window); vsync is set explicitly below instead of a creation flag.
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) return false;
    SDL_SetRenderVSync(renderer_, 1);

    // Game content draws into a fixed 640x480 texture, blitted at end of
    // frame (see endFrame()) - needed because SDL_SetRenderLogicalPresentation
    // clips to the letterboxed rect, so a cursor drawn normally could never
    // reach the letterbox bars. supportsRenderTargetTexture() gates backends
    // that can't support this (see Ps2Renderer's override);
    // OPENFEAR_FORCE_NO_RENDER_TARGET=1 forces the same fallback for testing.
    const bool force_no_target = !supportsRenderTargetTexture() ||
                                 (SDL_getenv("OPENFEAR_FORCE_NO_RENDER_TARGET") != nullptr);
    game_target_ = force_no_target ? nullptr
                                   : SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                                       SDL_TEXTUREACCESS_TARGET, GAME_W, GAME_H);

    // Fallback for backends without render-to-texture support: the older
    // SDL_RenderSetLogicalSize path. Cursor can't reach the letterbox bars
    // there, but positioning is otherwise correct. SDL3Input::poll() skips
    // its own logical-space conversion for event coordinates in this mode
    // - SDL's event watcher already pre-transforms them here (SDL#1397),
    // converting twice would double-apply the scale.
    use_render_target_ = (game_target_ != nullptr);
    if (!use_render_target_) {
        // Always the fixed GAME_H, every backend - real PS2 cropping is
        // done entirely by Ps2Renderer's own per-draw Y offset, not here.
        SDL_SetRenderLogicalPresentation(renderer_, GAME_W, GAME_H,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
        std::fprintf(
            stderr,
            "[SDL3Renderer] Render-target unavailable, using fallback direct-render path\n");
    }

    // Hide OS cursor - the game draws its own.
    SDL_HideCursor();

    return true;
}

void SDL3Renderer::shutdown() {
    for (auto& [h, tex] : textures_) SDL_DestroyTexture(tex);
    textures_.clear();

    if (game_target_) {
        SDL_DestroyTexture(game_target_);
        game_target_ = nullptr;
    }
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

SDL_Rect SDL3Renderer::getLetterboxRect() const {
    int ww = GAME_W, wh = GAME_H;
    SDL_GetRenderOutputSize(renderer_, &ww, &wh);
    // Aspect-preserving centered fit of GAME_W x GAME_H inside ww x wh.
    const float scale = std::min(ww / static_cast<float>(GAME_W), wh / static_cast<float>(GAME_H));
    const int out_w = static_cast<int>(GAME_W * scale);
    const int out_h = static_cast<int>(GAME_H * scale);
    return SDL_Rect{(ww - out_w) / 2, (wh - out_h) / 2, out_w, out_h};
}

void SDL3Renderer::beginFrame() {
    if (use_render_target_) {
        SDL_SetRenderTarget(renderer_, game_target_);
    }
    clear(0, 0, 0);
}

void SDL3Renderer::endFrame() {
    const bool has_cursor = !os_cursor_visible_ && cursor_ && cursor_->GetState().is_visible;
    const auto* state = has_cursor ? &cursor_->GetState() : nullptr;

    if (!use_render_target_) {
        // No letterbox-bar cursor visibility possible in this path -
        // SDL_SetRenderLogicalPresentation clips the viewport to the
        // letterboxed rect. Draw through the ordinary logical-scaled
        // pipeline for both input types.
        if (has_cursor) drawCursor(state->x, state->y);
        SDL_RenderPresent(renderer_);
        return;
    }

    // A gamepad/D-pad-driven cursor has no real screen pixel to be native
    // to - draw it into the game content itself (scales with it), like
    // any other game sprite, before the blit.
    if (has_cursor && state->in_game_area) {
        drawCursor(state->x, state->y);
    }

    // Composite game_target_ onto the window, letterboxed/pillarboxed -
    // the only place any scaling happens.
    SDL_SetRenderTarget(renderer_, nullptr);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_Rect dst = getLetterboxRect();
    SDL_FRect dst_f{static_cast<float>(dst.x), static_cast<float>(dst.y), static_cast<float>(dst.w),
                    static_cast<float>(dst.h)};
    SDL_RenderTexture(renderer_, game_target_, nullptr, &dst_f);

    // A real mouse cursor draws after the blit, directly onto the window's
    // full surface - native pixels, outside the letterbox clip. Its
    // sprite is scaled to match the content's own scale so it grows/
    // shrinks with the window; position stays exact (unscaled).
    if (has_cursor && !state->in_game_area) {
        const float cursor_scale = dst.w / static_cast<float>(GAME_W);
        drawCursorScaled(state->native_x, state->native_y, cursor_scale);
    }

    SDL_RenderPresent(renderer_);
}

void SDL3Renderer::clear(uint8_t r, uint8_t g, uint8_t b) {
    SDL_SetRenderDrawColor(renderer_, r, g, b, 255);
    SDL_RenderClear(renderer_);
}

TextureHandle SDL3Renderer::loadBitmap(std::string_view path) {
    if (!asset_provider_) return 0;
    std::vector<uint8_t> raw = asset_provider_->ReadFile(std::string(path));
    const auto sz = raw.size();
    if (sz == 0) {
        std::fprintf(stderr, "[loadBitmap] %.*s: ReadFile returned 0 bytes\n", (int)path.size(),
                     path.data());
        return 0;
    }

    const uint8_t* bmp_data = raw.data();
    size_t bmp_size = sz;
    std::vector<uint8_t> decompressed;
    if (sz >= 8 && raw[0] == 0x53 && raw[1] == 0x5A) {
        // SZDD-compressed - always BMP after decompression.
        if (!szdd_decompress(raw.data(), sz, decompressed)) {
            std::fprintf(stderr, "[loadBitmap] %.*s: SZDD decompress failed\n", (int)path.size(),
                         path.data());
            return 0;
        }
        bmp_data = decompressed.data();
        bmp_size = decompressed.size();
    }

    TextureHandle h = loadBitmapFromMemory(bmp_data, bmp_size);
    if (!h) {
        std::fprintf(stderr, "[loadBitmap] %.*s: loadBitmapFromMemory failed (%zu bytes read)\n",
                     (int)path.size(), path.data(), sz);
    }
    return h;
}

TextureHandle SDL3Renderer::loadBitmapFromMemory(const uint8_t* data, size_t size) {
    SDL_Surface* surf = decode_bmp(data, size);
    if (!surf) {
        std::fprintf(stderr, "[loadBitmap] decode_bmp failed (%zu bytes)\n", size);
        return 0;
    }
    // decode_bmp() sets the surface's blend mode to flag whether the
    // source had a real alpha channel - captured here for hasRealAlpha()
    // below. It does NOT carry onto the texture: SDL_CreateTextureFromSurface()
    // always starts a texture at BLEND when its format has alpha, which
    // every texture here does (decode_bmp() always builds RGBA32).
    SDL_BlendMode surf_blend = SDL_BLENDMODE_NONE;
    SDL_GetSurfaceBlendMode(surf, &surf_blend);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    SDL_DestroySurface(surf);
    if (!tex) {
        std::fprintf(stderr, "[loadBitmap] SDL_CreateTextureFromSurface failed: %s\n",
                     SDL_GetError());
        return 0;
    }
    // Every asset here is pixel art meant to display 1:1 (or crop-blitted
    // at native size) - never smoothed. Explicit, not relying on SDL's
    // own default (SDL3 renderers default to LINEAR, which would blur
    // every sharp edge).
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

    TextureHandle h = next_handle_++;
    textures_[h] = tex;
    has_real_alpha_[h] = (surf_blend == SDL_BLENDMODE_BLEND);
    return h;
}

bool SDL3Renderer::hasRealAlpha(TextureHandle h) const {
    auto it = has_real_alpha_.find(h);
    return it != has_real_alpha_.end() && it->second;
}

SDL_Texture* SDL3Renderer::get(TextureHandle h) const {
    auto it = textures_.find(h);
    return (it != textures_.end()) ? it->second : nullptr;
}

SDL_Texture* SDL3Renderer::CreateTextureAndFreeSurface(SDL_Surface* surface) {
    if (!surface) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_DestroySurface(surface);
    return tex;
}

bool SDL3Renderer::loadGameCursors(assets::AssetManager* asset_mgr) {
    if (!asset_mgr || cursors_loaded_) return cursors_loaded_;

    int loaded = 0;
    for (int state = 0; state < 3; ++state) {
        for (int dir = 0; dir < 3; ++dir) {
            const uint8_t* data = asset_mgr->GetCursor(state, dir);
            const uint32_t size = asset_mgr->GetCursorSize(state, dir);
            if (!data || size == 0) continue;

            SDL_Surface* surf = CursorParser::ParseWin16Cursor(data, size);
            if (!surf) continue;

            SDL_Texture* tex = CreateTextureAndFreeSurface(surf);
            if (!tex) continue;

            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            game_cursors_[state][dir] = tex;
            ++loaded;
        }
    }

    cursors_loaded_ = (loaded > 0);
    std::fprintf(stderr, "[SDL3Renderer] Cursors loaded: %d/9\n", loaded);
    return cursors_loaded_;
}

void SDL3Renderer::drawBitmap(TextureHandle tex, int dst_x, int dst_y, int src_x, int src_y, int w,
                              int h) {
    SDL_Texture* t = get(tex);
    if (!t) return;

    // Borrow BLENDMODE_NONE for this draw only, then restore - every
    // texture here starts at BLEND by default (its format always has
    // alpha), and blending isn't always a harmless no-op for opaque
    // pixels depending on the backend's blend formula. Same idiom as
    // drawSpriteAdditive()/Alpha().
    SDL_BlendMode prev = SDL_BLENDMODE_NONE;
    SDL_GetTextureBlendMode(t, &prev);
    if (prev != SDL_BLENDMODE_NONE) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);

    SDL_FRect src{static_cast<float>(src_x), static_cast<float>(src_y), static_cast<float>(w),
                  static_cast<float>(h)};
    SDL_FRect dst{static_cast<float>(dst_x), static_cast<float>(dst_y), static_cast<float>(w),
                  static_cast<float>(h)};
    SDL_RenderTexture(renderer_, t, &src, &dst);

    if (prev != SDL_BLENDMODE_NONE) SDL_SetTextureBlendMode(t, prev);
}

void SDL3Renderer::drawBitmapScaled(TextureHandle tex, int dst_x, int dst_y, int dst_w, int dst_h,
                                    int src_x, int src_y, int src_w, int src_h) {
    SDL_Texture* t = get(tex);
    if (!t) return;

    // Borrow BLENDMODE_NONE for this draw only, then restore
    SDL_BlendMode prev = SDL_BLENDMODE_NONE;
    SDL_GetTextureBlendMode(t, &prev);
    if (prev != SDL_BLENDMODE_NONE) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);

    SDL_FRect src{static_cast<float>(src_x), static_cast<float>(src_y), static_cast<float>(src_w),
                  static_cast<float>(src_h)};
    SDL_FRect dst{static_cast<float>(dst_x), static_cast<float>(dst_y), static_cast<float>(dst_w),
                  static_cast<float>(dst_h)};
    // Logs the first failure only, to avoid flooding stderr if it starts
    // failing every frame.
    static bool logged_copy_fail = false;
    if (!SDL_RenderTexture(renderer_, t, &src, &dst) && !logged_copy_fail) {
        logged_copy_fail = true;
        std::fprintf(stderr, "[SDL3Renderer] drawBitmapScaled SDL_RenderTexture failed: %s\n",
                     SDL_GetError());
    }

    if (prev != SDL_BLENDMODE_NONE) SDL_SetTextureBlendMode(t, prev);
}

TextureHandle SDL3Renderer::createStreamingTexture(int width, int height) {
    // Ask the backend for its own preferred texture format instead of
    // assuming one (e.g. RGBA32) is supported - that list genuinely
    // differs by backend. updateTexture() converts our fixed RGBA32
    // source into whatever format this turns out to be. The list itself
    // comes from a renderer property
    // (SDL_PROP_RENDERER_TEXTURE_FORMATS_POINTER), SDL_PIXELFORMAT_UNKNOWN-
    // terminated, not a counted array.
    SDL_PixelFormat format = SDL_PIXELFORMAT_RGBA32;
    SDL_PropertiesID props = SDL_GetRendererProperties(renderer_);
    const auto* formats = static_cast<const SDL_PixelFormat*>(
        SDL_GetPointerProperty(props, SDL_PROP_RENDERER_TEXTURE_FORMATS_POINTER, nullptr));
    if (formats && formats[0] != SDL_PIXELFORMAT_UNKNOWN) {
        format = formats[0];
    }

    SDL_Texture* tex =
        SDL_CreateTexture(renderer_, format, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!tex) {
        std::fprintf(stderr, "[SDL3Renderer] createStreamingTexture(%dx%d) failed: %s\n", width,
                     height, SDL_GetError());
        return 0;
    }
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

    TextureHandle h = next_handle_++;
    textures_[h] = tex;
    has_real_alpha_[h] = false;  // decoded video frames are always fully opaque
    return h;
}

void SDL3Renderer::updateTexture(TextureHandle tex, const uint8_t* rgba, int stride) {
    // Logs the first failure of each kind once, to catch a mid-playback
    // failure without flooding stderr at ~15-30 calls/sec if it starts
    // failing every frame.
    static bool logged_convert_fail = false;
    static bool logged_update_fail = false;

    SDL_Texture* t = get(tex);
    if (!t || !rgba) return;

    // SDL3 removed SDL_QueryTexture() - format/w/h are public read-only
    // fields on SDL_Texture itself now.
    SDL_PixelFormat format = t->format;
    int w = t->w, h = t->h;

    if (format == SDL_PIXELFORMAT_RGBA32) {
        if (!SDL_UpdateTexture(t, nullptr, rgba, stride) && !logged_update_fail) {
            logged_update_fail = true;
            std::fprintf(stderr, "[SDL3Renderer] updateTexture SDL_UpdateTexture failed: %s\n",
                         SDL_GetError());
        }
        return;
    }

    const int dst_stride = w * SDL_BYTESPERPIXEL(format);
    const size_t needed = (size_t)dst_stride * (size_t)h;
    if (convert_scratch_.size() < needed) convert_scratch_.resize(needed);

    if (!SDL_ConvertPixels(w, h, SDL_PIXELFORMAT_RGBA32, rgba, stride, format,
                           convert_scratch_.data(), dst_stride) &&
        !logged_convert_fail) {
        logged_convert_fail = true;
        std::fprintf(stderr, "[SDL3Renderer] updateTexture SDL_ConvertPixels failed: %s\n",
                     SDL_GetError());
    }
    if (!SDL_UpdateTexture(t, nullptr, convert_scratch_.data(), dst_stride) &&
        !logged_update_fail) {
        logged_update_fail = true;
        std::fprintf(stderr, "[SDL3Renderer] updateTexture SDL_UpdateTexture failed: %s\n",
                     SDL_GetError());
    }
}

SpriteInfo SDL3Renderer::createSprite(TextureHandle src) {
    SDL_Texture* t = get(src);
    if (!t) return {0, 0, 0};

    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return {src, t->w, t->h};
}

void SDL3Renderer::drawSprite(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x, int src_y,
                              int w, int h) {
    drawBitmap(sprite.handle, dst_x, dst_y, src_x, src_y, w, h);
}

void SDL3Renderer::drawSpriteTinted(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x,
                                    int src_y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    SDL_Texture* t = get(sprite.handle);
    if (!t) return;
    SDL_SetTextureColorMod(t, r, g, b);
    SDL_FRect src{static_cast<float>(src_x), static_cast<float>(src_y), static_cast<float>(w),
                  static_cast<float>(h)};
    SDL_FRect dst{static_cast<float>(dst_x), static_cast<float>(dst_y), static_cast<float>(w),
                  static_cast<float>(h)};
    SDL_RenderTexture(renderer_, t, &src, &dst);
    SDL_SetTextureColorMod(t, 255, 255, 255);
}

void SDL3Renderer::drawSpriteBlended(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x,
                                     int src_y, int w, int h, uint8_t alpha, SDL_BlendMode mode) {
    SDL_Texture* t = get(sprite.handle);
    if (!t) return;
    SDL_BlendMode previous_blend = SDL_BLENDMODE_BLEND;
    uint8_t previous_alpha = 255;
    SDL_GetTextureBlendMode(t, &previous_blend);
    SDL_GetTextureAlphaMod(t, &previous_alpha);
    SDL_FRect src{static_cast<float>(src_x), static_cast<float>(src_y), static_cast<float>(w),
                  static_cast<float>(h)};
    SDL_FRect dst{static_cast<float>(dst_x), static_cast<float>(dst_y), static_cast<float>(w),
                  static_cast<float>(h)};
    // Explicit mode + alpha, not assumed: this draw needs a specific
    // combination (e.g. ADD plus a pulsing alpha, for a glow effect) that
    // differs per caller (drawSpriteAdditive/Alpha() share this), restored
    // below so later ordinary draws of the same texture see it back in
    // its normal resting state (BLEND, full alpha, from createSprite()).
    SDL_SetTextureBlendMode(t, mode);
    SDL_SetTextureAlphaMod(t, alpha);
    SDL_RenderTexture(renderer_, t, &src, &dst);
    SDL_SetTextureAlphaMod(t, previous_alpha);
    SDL_SetTextureBlendMode(t, previous_blend);
}

void SDL3Renderer::drawSpriteAdditive(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x,
                                      int src_y, int w, int h, uint8_t alpha) {
    drawSpriteBlended(sprite, dst_x, dst_y, src_x, src_y, w, h, alpha, SDL_BLENDMODE_ADD);
}

void SDL3Renderer::drawSpriteAlpha(const SpriteInfo& sprite, int dst_x, int dst_y, int src_x,
                                   int src_y, int w, int h, uint8_t alpha) {
    drawSpriteBlended(sprite, dst_x, dst_y, src_x, src_y, w, h, alpha, SDL_BLENDMODE_BLEND);
}

void SDL3Renderer::fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    SDL_SetRenderDrawColor(renderer_, r, g, b, 255);
    SDL_FRect rect{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                   static_cast<float>(h)};
    SDL_RenderFillRect(renderer_, &rect);
}

void SDL3Renderer::fillRectAdditive(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b,
                                    uint8_t a) {
    SDL_BlendMode previous_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(renderer_, &previous_blend);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);
    SDL_FRect rect{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                   static_cast<float>(h)};
    SDL_RenderFillRect(renderer_, &rect);
    SDL_SetRenderDrawBlendMode(renderer_, previous_blend);
}

void SDL3Renderer::drawLine(int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b) {
    SDL_SetRenderDrawColor(renderer_, r, g, b, 255);
    SDL_RenderLine(renderer_, static_cast<float>(x1), static_cast<float>(y1),
                   static_cast<float>(x2), static_cast<float>(y2));
}

void SDL3Renderer::destroyTexture(TextureHandle tex) {
    auto it = textures_.find(tex);
    if (it == textures_.end()) return;
    SDL_DestroyTexture(it->second);
    textures_.erase(it);
    has_real_alpha_.erase(tex);
}

void SDL3Renderer::setGamepadCursorPosition(int x, int y) {
    if (cursor_) cursor_->SetGamepadFocus(x, y);
}

void SDL3Renderer::setCursorVisible(bool visible) {
    if (visible)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
    os_cursor_visible_ = visible;
}

void SDL3Renderer::setGameCursorState(int set, int frame) {
    game_cursor_set_ = (set >= 0 && set < 3) ? set : 2;
    game_cursor_frame_ = (frame >= 0 && frame < 3) ? frame : 0;
}

void SDL3Renderer::drawCursor(int x, int y) {
    drawCursorImpl(x, y, 1.0f);
}

void SDL3Renderer::drawCursorScaled(int x, int y, float scale) {
    drawCursorImpl(x, y, scale);
}

void SDL3Renderer::drawCursorImpl(int x, int y, float scale) {
    if (cursors_loaded_ && cursor_) {
        SDL_Texture* cursor_tex = game_cursors_[game_cursor_set_][game_cursor_frame_];
        if (cursor_tex) {
            const int w = static_cast<int>(cursor_tex->w * scale);
            const int h = static_cast<int>(cursor_tex->h * scale);

            SDL_FRect dst{static_cast<float>(x - w / 2), static_cast<float>(y - h / 2),
                          static_cast<float>(w), static_cast<float>(h)};

            // Win16 cursor bitmaps are Y-inverted (bottom-up DIB rows).
            SDL_RenderTextureRotated(renderer_, cursor_tex, nullptr, &dst, 0.0, nullptr,
                                     SDL_FLIP_VERTICAL);
            return;
        }
    }

    // Fallback: placeholder crosshair.
    const float fx = static_cast<float>(x), fy = static_cast<float>(y);
    const float size = 5.0f * scale;
    SDL_SetRenderDrawColor(renderer_, 255, 255, 0, 255);
    SDL_RenderLine(renderer_, fx, fy - size, fx, fy + size);
    SDL_RenderLine(renderer_, fx - size, fy, fx + size, fy);
    SDL_RenderPoint(renderer_, fx, fy);
}

}  // namespace openfear::sdl3
