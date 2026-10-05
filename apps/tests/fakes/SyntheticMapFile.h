#pragma once

// Builds valid MAPFILE bytes from scratch — own synthetic fixture data, not
// the real game's map (copyrighted, not in this repo). Format documented in
// include/openfear/game/MapFile.h and already covered by
// apps/tests/test_mapfile.cpp: no global header, MapFile::kProvinceCount
// provinces, each a uint16 stone_count followed by stone_count 19-byte
// records (type, connections, 4x[int16 dest + uint16 frame], int8
// nearest_bh).

#include "openfear/game/MapFile.h"
#include <cstdint>
#include <cstring>
#include <vector>

namespace openfear::testing {

struct SyntheticStone {
    uint8_t type = 0;
    uint8_t connections = 0;
    int16_t dest[4] = {game::kDestNone, game::kDestNone, game::kDestNone, game::kDestNone};
    uint16_t frame[4] = {game::kFrameInvalid, game::kFrameInvalid, game::kFrameInvalid,
                         game::kFrameInvalid};
    int8_t nearest_bh = -1;
};

inline void AppendStone(std::vector<uint8_t>& out, const SyntheticStone& st) {
    out.push_back(st.type);
    out.push_back(st.connections);
    for (int dir = 0; dir < 4; ++dir) {
        uint8_t d[2], f[2];
        std::memcpy(d, &st.dest[dir], 2);
        std::memcpy(f, &st.frame[dir], 2);
        out.push_back(d[0]);
        out.push_back(d[1]);
        out.push_back(f[0]);
        out.push_back(f[1]);
    }
    out.push_back(static_cast<uint8_t>(st.nearest_bh));
}

// Builds a full MAPFILE with the given stones placed in `province` (0-7),
// every other province empty (stone_count=0 — valid, just uninteresting).
inline std::vector<uint8_t> BuildSyntheticMapFile(int province,
                                                  const std::vector<SyntheticStone>& stones) {
    std::vector<uint8_t> out;
    for (int p = 0; p < game::MapFile::kProvinceCount; ++p) {
        const uint16_t count = (p == province) ? static_cast<uint16_t>(stones.size()) : 0;
        uint8_t c[2];
        std::memcpy(c, &count, 2);
        out.push_back(c[0]);
        out.push_back(c[1]);
        if (p == province) {
            for (const auto& st : stones) AppendStone(out, st);
        }
    }
    return out;
}

}  // namespace openfear::testing
