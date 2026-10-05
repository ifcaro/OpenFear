#include "openfear/assets/RectFile.h"
#include <cstring>

namespace openfear::assets {

std::optional<std::vector<RectEntry>> load_rect_file(IAssetProvider& provider,
                                                     std::string_view path) {
    std::vector<uint8_t> raw = provider.ReadFile(std::string(path));
    const size_t count = raw.size() / sizeof(RectEntry);
    if (count == 0) return std::nullopt;

    std::vector<RectEntry> entries(count);
    std::memcpy(entries.data(), raw.data(), count * sizeof(RectEntry));
    return entries;
}

}  // namespace openfear::assets
