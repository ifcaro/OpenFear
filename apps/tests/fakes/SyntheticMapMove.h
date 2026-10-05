#pragma once

// Builds valid DATA/MAPMOVE.BIN bytes from scratch — own synthetic fixture
// data, not the real game's file (copyrighted, not in this repo). Format:
// 3 polygons (on-disk order LEFT/RIGHT/CENTER), each a uint16 point count
// followed by that many (int16 x, int16 y) pairs. An optional trailing
// 8-byte counter_box is omitted here — BoardGameScreen::load() already
// tolerates its absence, only the 3 polygons are a hard dependency.

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace openfear::testing {

inline void AppendMapMovePolygon(std::vector<uint8_t>& out,
                                 const std::vector<std::pair<int16_t, int16_t>>& points) {
    const uint16_t count = static_cast<uint16_t>(points.size());
    uint8_t c[2];
    std::memcpy(c, &count, 2);
    out.push_back(c[0]);
    out.push_back(c[1]);
    for (const auto& [x, y] : points) {
        uint8_t xb[2], yb[2];
        std::memcpy(xb, &x, 2);
        std::memcpy(yb, &y, 2);
        out.push_back(xb[0]);
        out.push_back(xb[1]);
        out.push_back(yb[0]);
        out.push_back(yb[1]);
    }
}

// One minimal (1-point) polygon per zone - enough for load()'s "all 3
// non-empty" check, not meant to exercise real click-zone geometry.
inline std::vector<uint8_t> BuildSyntheticMapMoveFile() {
    std::vector<uint8_t> out;
    AppendMapMovePolygon(out, {{0, 0}});  // LEFT
    AppendMapMovePolygon(out, {{0, 0}});  // RIGHT
    AppendMapMovePolygon(out, {{0, 0}});  // CENTER
    return out;
}

}  // namespace openfear::testing
