#include "openfear/assets/IsoAssetProvider.h"
#include "openfear/assets/PathNormalize.h"

namespace openfear {

namespace {

struct IsoHandle {
    const Iso9660Reader::Entry* entry;
    uint64_t pos;
};

}  // namespace

IsoAssetProvider::IsoAssetProvider(const std::filesystem::path& iso_path)
    : IsoAssetProvider(std::make_unique<IsoFileSectorSource>(iso_path)) {}

IsoAssetProvider::IsoAssetProvider(std::unique_ptr<Iso9660SectorSource> source)
    : source_(std::move(source)), iso_(*source_) {
    mounted_ = iso_.Mount();
    if (mounted_) {
        std::vector<std::string> paths;
        paths.reserve(iso_.entries().size());
        for (const auto& e : iso_.entries()) paths.push_back(e.path);
        remap_.Build(paths);
    }
}

std::string IsoAssetProvider::NormalizeAndRemap(const std::string& path) const {
    return remap_.Resolve(NormalizeVirtualPath(path));
}

bool IsoAssetProvider::Exists(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    return iso_.Find(NormalizeAndRemap(path)) != nullptr;
}

std::vector<uint8_t> IsoAssetProvider::ReadFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    const Iso9660Reader::Entry* e = iso_.Find(NormalizeAndRemap(path));
    if (!e) return {};
    std::vector<uint8_t> data(e->size);
    const size_t got = iso_.ReadAt(*e, 0, data.data(), data.size());
    data.resize(got);
    return data;
}

AssetHandle IsoAssetProvider::Open(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    const Iso9660Reader::Entry* e = iso_.Find(NormalizeAndRemap(path));
    if (!e) return 0;
    return reinterpret_cast<AssetHandle>(new IsoHandle{e, 0});
}

size_t IsoAssetProvider::Read(AssetHandle handle, void* buf, size_t n) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* h = reinterpret_cast<IsoHandle*>(handle);
    const size_t got = iso_.ReadAt(*h->entry, h->pos, buf, n);
    h->pos += got;
    return got;
}

bool IsoAssetProvider::Seek(AssetHandle handle, int64_t offset, AssetSeekOrigin origin) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* h = reinterpret_cast<IsoHandle*>(handle);
    int64_t base = 0;
    switch (origin) {
        case AssetSeekOrigin::Set:
            base = 0;
            break;
        case AssetSeekOrigin::Cur:
            base = static_cast<int64_t>(h->pos);
            break;
        case AssetSeekOrigin::End:
            base = static_cast<int64_t>(h->entry->size);
            break;
    }
    const int64_t target = base + offset;
    if (target < 0) return false;
    h->pos = static_cast<uint64_t>(target);
    return true;
}

int64_t IsoAssetProvider::Tell(AssetHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int64_t>(reinterpret_cast<IsoHandle*>(handle)->pos);
}

void IsoAssetProvider::Close(AssetHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    delete reinterpret_cast<IsoHandle*>(handle);
}

}  // namespace openfear
