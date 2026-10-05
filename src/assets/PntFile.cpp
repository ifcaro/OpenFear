#include "openfear/assets/PntFile.h"
#include <cstring>

namespace openfear::assets {

std::optional<std::vector<PointEntry>> load_pnt_file(IAssetProvider& provider,
                                                     std::string_view path) {
    std::vector<uint8_t> raw = provider.ReadFile(std::string(path));
    if (raw.size() < sizeof(uint16_t)) return std::nullopt;

    // PNT files begin with a uint16 point count, followed by count × 4-byte
    // (x, y) pairs. The count matches the stone_count in the mapfile group.
    uint16_t count = 0;
    std::memcpy(&count, raw.data(), sizeof(count));
    if (count == 0 || raw.size() < sizeof(count) + count * sizeof(PointEntry)) return std::nullopt;

    std::vector<PointEntry> entries(count);
    std::memcpy(entries.data(), raw.data() + sizeof(count), count * sizeof(PointEntry));
    return entries;
}

}  // namespace openfear::assets
