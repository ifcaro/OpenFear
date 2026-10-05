#pragma once
#include "openfear/assets/CdLayout.h"
#include "openfear/assets/IAssetProvider.h"
#include "openfear/assets/Iso9660Reader.h"
#include "openfear/assets/IsoFileSectorSource.h"
#include <filesystem>
#include <memory>
#include <mutex>

namespace openfear {

// IAssetProvider backed by a plain .iso image - a real file on disk, via
// IsoFileSectorSource. Synchronous reads only, no prefetch thread needed:
// a regular file read is already fast enough to stream AVI smoothly
// without one.
//
// Thread safety: SDL3Audio's decode thread and the main thread both read
// through this provider concurrently, sharing one unsynced reader/cache -
// mutex_ serializes every access, including per-handle state (Seek/Tell/
// Close), so a handle can safely be used from a different thread than the
// one that opened it.
class IsoAssetProvider : public IAssetProvider {
public:
    explicit IsoAssetProvider(const std::filesystem::path& iso_path);
    explicit IsoAssetProvider(std::unique_ptr<Iso9660SectorSource> source);

    // False if the file couldn't be opened or isn't a valid ISO9660 image -
    // callers should fall back rather than use a provider that will never
    // find anything.
    bool is_open() const { return mounted_; }

    bool Exists(const std::string& path) override;
    std::vector<uint8_t> ReadFile(const std::string& path) override;
    AssetHandle Open(const std::string& path) override;
    size_t Read(AssetHandle handle, void* buf, size_t n) override;
    bool Seek(AssetHandle handle, int64_t offset, AssetSeekOrigin origin) override;
    int64_t Tell(AssetHandle handle) override;
    void Close(AssetHandle handle) override;

private:
    std::string NormalizeAndRemap(const std::string& path) const;

    std::mutex mutex_;
    std::unique_ptr<Iso9660SectorSource> source_;
    Iso9660Reader iso_;
    assets::CdPathRemap remap_;
    bool mounted_ = false;
};

}  // namespace openfear
