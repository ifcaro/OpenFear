#pragma once
#include "openfear/assets/CdLayout.h"
#include "openfear/assets/IAssetProvider.h"
#include <cstdio>
#include <filesystem>

namespace openfear {

// Real-filesystem IAssetProvider, for any platform where assets live in a
// real directory on disk. Paths are normalized relative to root
// (case/separator differences) so lookups work the same across
// case-sensitive and case-insensitive filesystems. Also transparently
// reclassifies a raw, un-installed CD layout (see CdPathRemap) - built
// once at construction from a real directory listing.
class NativeAssetProvider : public IAssetProvider {
public:
    explicit NativeAssetProvider(std::filesystem::path root);

    bool Exists(const std::string& path) override;
    std::vector<uint8_t> ReadFile(const std::string& path) override;

    AssetHandle Open(const std::string& path) override;
    size_t Read(AssetHandle handle, void* buf, size_t n) override;
    bool Seek(AssetHandle handle, int64_t offset, AssetSeekOrigin origin) override;
    int64_t Tell(AssetHandle handle) override;
    void Close(AssetHandle handle) override;

protected:
    // build_remap=false skips the recursive std::filesystem walk below -
    // for a subclass that builds remap_ itself from its own directory
    // listing and would otherwise pay for two full walks of root for
    // nothing, the first one always discarded.
    NativeAssetProvider(std::filesystem::path root, bool build_remap);

    std::filesystem::path root_;
    assets::CdPathRemap remap_;

    std::filesystem::path Resolve(const std::string& path) const;
};

}  // namespace openfear
