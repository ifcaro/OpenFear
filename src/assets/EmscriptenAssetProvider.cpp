#include "openfear/assets/EmscriptenAssetProvider.h"
#include "openfear/assets/CdLayout.h"
#include "openfear/assets/PathNormalize.h"
#include <emscripten.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace openfear {

// clang-format off
EM_ASYNC_JS(int, ofw_start_gate_js, (), {
    return await Module['ofwStartGate']();
});

EM_JS(int, ofw_exists_js, (const char* path), {
    return Module['ofwExists'](UTF8ToString(path));
});

EM_JS(double, ofw_size_js, (const char* path), {
    return Module['ofwGetSize'](UTF8ToString(path));
});

EM_ASYNC_JS(int, ofw_read_slice_js, (const char* path, double offset, double length, uint8_t* dest), {
    return await Module['ofwReadSlice'](UTF8ToString(path), offset, length, dest);
});

EM_JS(char*, ofw_list_paths_js, (), {
    return stringToNewUTF8(Module['ofwListPaths']());
});
// clang-format on

bool EmscriptenAssetProvider::StartGate() {
    if (ofw_start_gate_js() <= 0) return false;

    char* raw_list = ofw_list_paths_js();
    std::vector<std::string> paths;
    {
        std::istringstream iss(raw_list);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty()) paths.push_back(line);
        }
    }
    std::free(raw_list);

    remap_.Build(paths);
    return true;
}

// remap_'s keys are JS-normalized paths (see CdPathRemap::Build), matching what
// NormalizeVirtualPath produces (backslash->slash, uppercase, strip leading slashes).
// virtual_path may arrive in whatever case/separator the C++ call site happens to
// construct, so it needs that same normalization before the lookup.
std::string EmscriptenAssetProvider::ResolveRaw(const std::string& virtual_path) const {
    return remap_.Resolve(NormalizeVirtualPath(virtual_path));
}

bool EmscriptenAssetProvider::Exists(const std::string& path) {
    return ofw_exists_js(ResolveRaw(path).c_str()) != 0;
}

std::vector<uint8_t> EmscriptenAssetProvider::ReadFile(const std::string& path) {
    const std::string raw_path = ResolveRaw(path);
    const double size = ofw_size_js(raw_path.c_str());
    if (size < 0) {
        return {};
    }
    if (size == 0) return {};
    std::vector<uint8_t> out(static_cast<size_t>(size));
    const int got = ofw_read_slice_js(raw_path.c_str(), 0.0, size, out.data());
    if (got < 0 || static_cast<size_t>(got) != out.size()) {
        out.resize(static_cast<size_t>(std::max(got, 0)));
    }
    return out;
}

AssetHandle EmscriptenAssetProvider::Open(const std::string& path) {
    const std::string raw_path = ResolveRaw(path);
    const double size = ofw_size_js(raw_path.c_str());
    if (size < 0) {
        return 0;
    }
    auto h = std::make_unique<StreamHandle>();
    h->path = raw_path;
    h->pos = 0;
    if (size > 0) {
        h->data.resize(static_cast<size_t>(size));
        const int got = ofw_read_slice_js(raw_path.c_str(), 0.0, size, h->data.data());
        if (got < 0 || static_cast<size_t>(got) != h->data.size())
            h->data.resize(static_cast<size_t>(std::max(got, 0)));
    }
    StreamHandle* raw = h.get();
    handles_[raw] = std::move(h);
    return reinterpret_cast<AssetHandle>(raw);
}

size_t EmscriptenAssetProvider::Read(AssetHandle handle, void* buf, size_t n) {
    auto* h = reinterpret_cast<StreamHandle*>(handle);
    if (!h || handles_.find(h) == handles_.end()) return 0;
    if (static_cast<size_t>(h->pos) >= h->data.size()) return 0;
    const size_t avail = h->data.size() - static_cast<size_t>(h->pos);
    const size_t got = std::min(n, avail);
    std::memcpy(buf, h->data.data() + h->pos, got);
    h->pos += static_cast<int64_t>(got);
    return got;
}

bool EmscriptenAssetProvider::Seek(AssetHandle handle, int64_t offset, AssetSeekOrigin origin) {
    auto* h = reinterpret_cast<StreamHandle*>(handle);
    if (!h || handles_.find(h) == handles_.end()) return false;
    const int64_t base = (origin == AssetSeekOrigin::Set)   ? 0
                         : (origin == AssetSeekOrigin::Cur) ? h->pos
                                                            : static_cast<int64_t>(h->data.size());
    const int64_t new_pos = base + offset;
    if (new_pos < 0) return false;
    h->pos = new_pos;
    return true;
}

int64_t EmscriptenAssetProvider::Tell(AssetHandle handle) {
    auto* h = reinterpret_cast<StreamHandle*>(handle);
    if (!h || handles_.find(h) == handles_.end()) return -1;
    return h->pos;
}

void EmscriptenAssetProvider::Close(AssetHandle handle) {
    auto* h = reinterpret_cast<StreamHandle*>(handle);
    if (h) handles_.erase(h);
}

}  // namespace openfear
