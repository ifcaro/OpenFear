#include "openfear/assets/NativeAssetProvider.h"
#include "openfear/assets/PathNormalize.h"
#include <filesystem>
#include <fstream>
#include <vector>

namespace openfear {

namespace fs = std::filesystem;

NativeAssetProvider::NativeAssetProvider(fs::path root)
    : NativeAssetProvider(std::move(root), true) {}

NativeAssetProvider::NativeAssetProvider(fs::path root, bool build_remap) : root_(std::move(root)) {
    if (!build_remap) return;

    // Every file under root, relative/uppercase/'/'-separated - lets
    // CdPathRemap detect and translate a raw, un-installed CD layout. A
    // no-op (empty map) if root already matches the installed layout.
    std::vector<std::string> paths;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(root_, ec)) {
        if (!entry.is_regular_file()) continue;
        const fs::path rel = fs::relative(entry.path(), root_, ec);
        if (!ec) paths.push_back(NormalizeVirtualPath(rel.string()));
    }
    remap_.Build(paths);
}

fs::path NativeAssetProvider::Resolve(const std::string& path) const {
    return root_ / fs::path(remap_.Resolve(NormalizeVirtualPath(path)));
}

bool NativeAssetProvider::Exists(const std::string& path) {
    // error_code overload, not the throwing one - some platforms' device
    // layer can return an errno the throwing fs::exists() doesn't expect
    // for a plain missing file, turning that into a crash instead of a
    // false. Kept as a cheap stat() here since Exists() is called
    // throughout gameplay for lazy asset loading on every platform;
    // Ps2AssetProvider overrides this method entirely for its own reason.
    std::error_code ec;
    return fs::exists(Resolve(path), ec);
}

std::vector<uint8_t> NativeAssetProvider::ReadFile(const std::string& path) {
    std::ifstream f(Resolve(path), std::ios::binary | std::ios::ate);
    if (!f) return {};
    const auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> data(sz);
    if (sz > 0 && !f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(sz)))
        return {};
    return data;
}

AssetHandle NativeAssetProvider::Open(const std::string& path) {
    FILE* fp = std::fopen(Resolve(path).string().c_str(), "rb");
    return reinterpret_cast<AssetHandle>(fp);
}

size_t NativeAssetProvider::Read(AssetHandle handle, void* buf, size_t n) {
    if (!handle) return 0;
    return std::fread(buf, 1, n, reinterpret_cast<FILE*>(handle));
}

bool NativeAssetProvider::Seek(AssetHandle handle, int64_t offset, AssetSeekOrigin origin) {
    if (!handle) return false;
    int whence = (origin == AssetSeekOrigin::Set)   ? SEEK_SET
                 : (origin == AssetSeekOrigin::Cur) ? SEEK_CUR
                                                    : SEEK_END;
#if defined(_WIN32)
    return _fseeki64(reinterpret_cast<FILE*>(handle), offset, whence) == 0;
#else
    return std::fseek(reinterpret_cast<FILE*>(handle), static_cast<long>(offset), whence) == 0;
#endif
}

int64_t NativeAssetProvider::Tell(AssetHandle handle) {
    if (!handle) return -1;
#if defined(_WIN32)
    return _ftelli64(reinterpret_cast<FILE*>(handle));
#else
    return std::ftell(reinterpret_cast<FILE*>(handle));
#endif
}

void NativeAssetProvider::Close(AssetHandle handle) {
    if (handle) std::fclose(reinterpret_cast<FILE*>(handle));
}

}  // namespace openfear
