#pragma once
#include "openfear/assets/IAssetProvider.h"
#include <cstdint>
#include <vector>
#include <string_view>
#include <optional>

namespace openfear::assets {

// .PNT files — minimap stone pixel coordinates, one file per province.
//
// Layout:
//   uint16   stone_count
//   struct { uint16 x; uint16 y; } points[stone_count]
//
// Pixel coordinates on the minimap image. Files: DATA/MAP0.PNT ..
// DATA/MAP7.PNT (provinces 0-7); points[K] is stone K's position.

struct PointEntry {
    uint16_t x, y;
};

std::optional<std::vector<PointEntry>> load_pnt_file(IAssetProvider& provider,
                                                     std::string_view path);

}  // namespace openfear::assets
