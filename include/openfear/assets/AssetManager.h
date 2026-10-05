#pragma once

#include "ExeResourceParser.h"
#include "RectFile.h"
#include "IAssetProvider.h"
#include "../platform/IRenderer.h"
#include "../AssetLocator.h"

#include <filesystem>
#include <memory>
#include <string>
#include <optional>
#include <vector>

namespace openfear {
class IRenderer;
}  // namespace openfear

namespace openfear::assets {

// Loads and caches assets sourced from the original game files: EXE
// resources (cursors, custom data), and filesystem assets (textures,
// portraits, rect/hotspot data).
class AssetManager {
public:
    AssetManager(std::filesystem::path root, IRenderer& renderer, IAssetProvider& provider)
        : root_(std::move(root)), asset_provider_(&provider), renderer_(&renderer) {}
    ~AssetManager() = default;

    IAssetProvider& provider() const { return *asset_provider_; }

    // exe_path: path to ATMOS.EXE.
    bool Init(const std::string& exe_path);

    bool LoadCursors();

    // state: 0-2. direction: 0-2 (Up, Down, Horizontal).
    const uint8_t* GetCursor(int state, int direction) const;
    uint32_t GetCursorSize(int state, int direction) const;

    // category/name.BMP, e.g. texture("M1", "HEXAGON"). Returns 0 if not found.
    TextureHandle texture(const char* category, const char* name);

    // char_id 0-5 -> INFO{id}_0.BMP. Returns 0 if not found.
    TextureHandle portrait(int char_id);

    // e.g. portrait("INFO0_0"). Returns 0 if not found.
    TextureHandle portrait(const char* name);

    TextureHandle misc_texture(const char* name);

    // name without extension, e.g. rect_file("pkchar").
    std::optional<std::vector<RectEntry>> rect_file(const char* name);

private:
    ExeResourceParser parser_;
    // Anchoring/diagnostics only, exposed via asset_root() - never used to
    // resolve an asset; real file resolution goes through asset_provider_
    // with bare virtual paths (AssetPaths::data()/avi()), which never
    // touch this.
    std::filesystem::path root_;

public:
    // Path to the original game asset root (e.g. .../assets/). Anchoring/
    // diagnostics only - asset resolution always goes through
    // asset_provider_ with bare virtual paths, never through this.
    const std::filesystem::path& asset_root() const { return root_; }

private:
    IAssetProvider* asset_provider_ = nullptr;
    IRenderer* renderer_ = nullptr;

    const uint8_t* cursor_cache_[3][3] = {};  // [state][direction]
    uint32_t cursor_size_cache_[3][3] = {};
    bool cursors_loaded_ = false;
};

}  // namespace openfear::assets
