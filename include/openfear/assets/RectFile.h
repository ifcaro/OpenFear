#pragma once
#include "openfear/assets/IAssetProvider.h"
#include <cstdint>
#include <vector>
#include <string_view>
#include <optional>

namespace openfear::assets {

// .BIN files: arrays of (x1, y1, x2, y2) as little-endian uint16, used for
// hotspot rectangles, player slot rects, etc.

struct RectEntry {
    uint16_t x1, y1, x2, y2;
};

// Loads rect entries from a .BIN file. Trailing bytes past the last whole
// entry are ignored (PKCHAR.BIN carries 60 such bytes). Returns nullopt if
// the file can't be read or is too short for even one entry.
std::optional<std::vector<RectEntry>> load_rect_file(IAssetProvider& provider,
                                                     std::string_view path);

}  // namespace openfear::assets
