#pragma once

// Builds .BIN click-zone rect files from scratch — own synthetic fixture
// data (arbitrary coordinates), not the real game's zone tables. Format:
// N x 8-byte RectEntry {x1,y1,x2,y2} as native uint16 (see RectFile.h/.cpp
// - a plain memcpy of the raw bytes, no header).

#include "openfear/assets/RectFile.h"
#include <cstring>
#include <vector>

namespace openfear::testing {

inline std::vector<uint8_t> BuildRectFileBytes(const std::vector<assets::RectEntry>& entries) {
    std::vector<uint8_t> out(entries.size() * sizeof(assets::RectEntry));
    if (!entries.empty()) std::memcpy(out.data(), entries.data(), out.size());
    return out;
}

}  // namespace openfear::testing
