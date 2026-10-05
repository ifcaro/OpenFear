#include "openfear/assets/AssetManager.h"
#include "openfear/platform/IRenderer.h"

#include <cstdio>
#include <cstring>

namespace openfear::assets {

namespace {
uint16_t read_u16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
}  // namespace

bool AssetManager::Init(const std::string& exe_path) {
    if (!asset_provider_) {
        return false;
    }
    if (!parser_.Parse(*asset_provider_, exe_path)) {
        return false;
    }

    return true;
}

bool AssetManager::LoadCursors() {
    if (cursors_loaded_) return true;

    // Groups are RT_GROUP_CURSOR ordinals 1-9 (0x8001-0x8009), in
    // state-major, direction-minor order — the only lookup that actually
    // resolves on any known Atmosfear build (see ExeResourceParser).
    const uint16_t RT_CURSOR = 0x8001;
    const uint16_t RT_GROUP_CURSOR = 0x800C;

    int loaded_count = 0;

    for (int state = 0; state < 3; ++state) {
        for (int dir = 0; dir < 3; ++dir) {
            const uint16_t group_id = 0x8000 | (state * 3 + dir + 1);
            if (!parser_.LoadResource(RT_GROUP_CURSOR, group_id)) continue;

            const auto* group_entry = parser_.GetResourceById(RT_GROUP_CURSOR, group_id);
            if (!group_entry || !group_entry->data || group_entry->size < 6u + 14u) continue;

            // RT_GROUP_CURSOR: WORD reserved, WORD type, WORD count, then
            // per entry WORD width/height/planes/bitcount, DWORD bytesInRes,
            // WORD id (plain ordinal, not 0x8000-prefixed).
            const uint8_t* g = group_entry->data;
            const uint16_t count = read_u16(g + 4);
            if (count < 1) continue;

            const uint16_t component_id = read_u16(g + 6 + 12);
            const uint16_t cursor_id = 0x8000 | component_id;

            if (!parser_.LoadResource(RT_CURSOR, cursor_id)) continue;

            const auto* entry = parser_.GetResourceById(RT_CURSOR, cursor_id);
            if (entry && entry->data) {
                cursor_cache_[state][dir] = entry->data;
                cursor_size_cache_[state][dir] = entry->size;
                ++loaded_count;
            }
        }
    }

    cursors_loaded_ = (loaded_count == 9);
    return cursors_loaded_;
}

const uint8_t* AssetManager::GetCursor(int state, int direction) const {
    if (state < 0 || state >= 3 || direction < 0 || direction >= 3) return nullptr;
    return cursor_cache_[state][direction];
}

uint32_t AssetManager::GetCursorSize(int state, int direction) const {
    if (state < 0 || state >= 3 || direction < 0 || direction >= 3) return 0;
    return cursor_size_cache_[state][direction];
}

TextureHandle AssetManager::texture(const char* category, const char* name) {
    if (!renderer_) return 0;
    return renderer_->loadBitmap(AssetPaths::data(category, name, "BMP").string());
}

TextureHandle AssetManager::portrait(int char_id) {
    if (char_id < 0 || char_id > 5) return 0;
    char filename[32];
    std::snprintf(filename, sizeof(filename), "INFO%d_0", char_id);
    return portrait(filename);
}

TextureHandle AssetManager::portrait(const char* name) {
    if (!renderer_) return 0;
    return renderer_->loadBitmap(AssetPaths::data("CHARS", name, "BMP").string());
}

TextureHandle AssetManager::misc_texture(const char* name) {
    if (!renderer_) return 0;
    return renderer_->loadBitmap(AssetPaths::data("MISC", name, "BMP").string());
}

std::optional<std::vector<RectEntry>> AssetManager::rect_file(const char* name) {
    if (!asset_provider_) return std::nullopt;
    return load_rect_file(*asset_provider_, AssetPaths::data("DATA", name, "BIN").string());
}

}  // namespace openfear::assets
