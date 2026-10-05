#pragma once

#include "openfear/assets/IsoAssetProvider.h"
#include "openfear/platform/Platform.h"
#include <filesystem>
#include <memory>
#include <optional>

namespace openfear {

struct RuntimeContext {
    std::unique_ptr<IAssetProvider> asset_provider;
    std::filesystem::path asset_root;
    std::unique_ptr<Platform> platform;
};

std::optional<RuntimeContext> InitializeRuntime(int argc, char* argv[]);

// discover() can resolve to a loose .iso file instead of a folder - a
// regular file there means that case. Every platform's InitializeRuntime()
// needs this same distinction, just with a different folder-based
// provider type.
template <typename FolderAssetProvider>
std::unique_ptr<IAssetProvider> MakeAssetProvider(const std::filesystem::path& asset_root) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(asset_root, ec))
        return std::make_unique<IsoAssetProvider>(asset_root);
    return std::make_unique<FolderAssetProvider>(asset_root);
}

}  // namespace openfear
