#include "openfear/game/MapFile.h"
#include <cstdio>
#include <cstring>

namespace openfear::game {

bool MapFile::Load(IAssetProvider& provider, const std::string& path) {
    loaded_ = false;

    std::vector<uint8_t> raw = provider.ReadFile(path);
    if (raw.empty()) {
        return false;
    }

    size_t pos = 0;
    auto read_bytes = [&](void* dst, size_t n) -> bool {
        if (pos + n > raw.size()) return false;
        std::memcpy(dst, raw.data() + pos, n);
        pos += n;
        return true;
    };

    for (int prov = 0; prov < kProvinceCount; ++prov) {
        uint16_t stone_count = 0;
        if (!read_bytes(&stone_count, 2)) {
            return false;
        }
        if (stone_count > 400) {
            return false;
        }

        provinces_[prov].resize(stone_count);

        for (uint16_t s = 0; s < stone_count; ++s) {
            MapStone& st = provinces_[prov][s];

            uint8_t type_byte = 0, conn_byte = 0;
            if (!read_bytes(&type_byte, 1) || !read_bytes(&conn_byte, 1)) {
                return false;
            }
            st.type = type_byte;
            st.connections = conn_byte;

            // Always exactly 4 direction pairs — connections is informational,
            // not the real loop limit.
            for (int dir = 0; dir < 4; ++dir) {
                int16_t dest_val = 0;
                uint16_t frame_val = 0;
                if (!read_bytes(&dest_val, 2) || !read_bytes(&frame_val, 2)) {
                    return false;
                }
                st.dest[dir] = dest_val;
                st.frame[dir] = frame_val;
            }

            int8_t nbh_byte = 0;
            if (!read_bytes(&nbh_byte, 1)) {
                return false;
            }
            st.nearest_bh = static_cast<int16_t>(nbh_byte);  // sign-extend
        }
    }

    // Baked-in map patch, not derivable from the raw file — the original
    // hardcodes these 2 province-2 dest overrides to WALL once every
    // province has loaded.
    if (provinces_[2].size() > 13) {
        provinces_[2][0].dest[2] = kDestWall;   // stone 0, direction S
        provinces_[2][13].dest[0] = kDestWall;  // stone 13, direction N
    }

    loaded_ = true;
    return true;
}

uint16_t MapFile::GetFrame(uint16_t province, uint16_t stone_id, uint8_t direction) const {
    if (!loaded_) return kFrameInvalid;
    if (province >= kProvinceCount) return kFrameInvalid;
    const auto& prov = provinces_[province];
    if (stone_id >= prov.size()) return kFrameInvalid;
    if (direction >= 4) return kFrameInvalid;
    return prov[stone_id].frame[direction];
}

int16_t MapFile::GetDest(uint16_t province, uint16_t stone_id, uint8_t direction) const {
    if (!loaded_) return kDestNone;
    if (province >= kProvinceCount) return kDestNone;
    const auto& prov = provinces_[province];
    if (stone_id >= prov.size()) return kDestNone;
    if (direction >= 4) return kDestNone;
    return prov[stone_id].dest[direction];
}

const MapStone* MapFile::GetStone(uint16_t province, uint16_t stone_id) const {
    if (!loaded_) return nullptr;
    if (province >= kProvinceCount) return nullptr;
    const auto& prov = provinces_[province];
    if (stone_id >= prov.size()) return nullptr;
    return &prov[stone_id];
}

uint16_t MapFile::GetStoneCount(uint16_t province) const {
    if (province >= kProvinceCount) return 0;
    return static_cast<uint16_t>(provinces_[province].size());
}

}  // namespace openfear::game
