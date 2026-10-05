#pragma once

#include "openfear/assets/IAssetProvider.h"
#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace openfear::game {

// MAPFILE binary format — board topology + FULLMAP.AVI frame lookup.
//
// 8 provinces, no global header. Each province: uint16 stone_count +
// stone_count x 19-byte stone records. Total 963 stones, 18313 bytes of
// real stone data; the file is 23025 bytes total. The real loader closes
// the file right after province 7 without reading further, so the
// remaining 4712 bytes are confirmed dead from the game's perspective —
// content unknown, never read by either engine.
//
// On-disk stone record (19 bytes, interleaved):
//   +0  uint8  type          0=path, 1-4=encounter, 5=gatekeeper lair, 6=junction, 7=portal
//   +1  uint8  connections   informational only; loop always reads all 4 directions
//   +2  int16  N_dest        destination stone North (-3=gate, -2=wall, -1=none)
//   +4  uint16 N_frame       FULLMAP.AVI frame facing North (0xFFFF=invalid)
//   +6  int16  E_dest / +8  uint16 E_frame
//   +10 int16  S_dest / +12 uint16 S_frame
//   +14 int16  W_dest / +16 uint16 W_frame
//   +18 int8   nearest_bh    signed, nearest black-hole stone index
//
// In-memory layout (deinterleaved): dest[0..3]/frame[0..3] = N/E/S/W.
//
// Province frame ranges (province: stones, frames):
//   0: 144, 0-337       4: 134, 1302-1611
//   1: 138, 338-633     5: 138, 1612-1915
//   2: 146, 634-971     6:  43, 1916-2015
//   3: 148, 972-1301    7:  72, 2016-2171
// Total: exactly 2172 frames = FULLMAP.AVI frame count.

enum class MapDirection : uint8_t { North = 0, East = 1, South = 2, West = 3 };

static constexpr int16_t kDestGate = -3;           // 0xFFFD — portal gate crossing (stone.type==7)
static constexpr int16_t kDestWall = -2;           // 0xFFFE — movement blocked
static constexpr int16_t kDestNone = -1;           // 0xFFFF — direction unavailable
static constexpr uint16_t kFrameInvalid = 0xFFFF;  // invalid frame, do not seek

// In-memory stone record (deinterleaved from the 19-byte on-disk format).
struct MapStone {
    uint8_t type;  // tile type (0=path, 1-4=encounter, 5=gatekeeper lair, 6=junction, 7=portal)
    uint8_t connections;  // count of valid directions — informational, NOT a loop limit
    int16_t dest[4];      // destination stone indices: [0]=N, [1]=E, [2]=S, [3]=W
    uint16_t frame[4];    // FULLMAP.AVI frame numbers: [0]=N, [1]=E, [2]=S, [3]=W
    int16_t nearest_bh;   // nearest black-hole stone index (signed, sign-extended from 1 byte)
};

// MAPFILE parser and FULLMAP.AVI frame lookup table.
//
// Usage:
//   MapFile mapfile;
//   if (mapfile.Load(provider, "MAPFILE")) {
//       // province = gs->player_idx, stone_id = gs->sub_idx (see GameState.h)
//       uint16_t frame = mapfile.GetFrame(province, stone_id, direction);
//   }
class MapFile {
public:
    static constexpr int kProvinceCount = 8;

    bool Load(IAssetProvider& provider, const std::string& path);

    // Frame number (0-2171), or kFrameInvalid if direction unavailable.
    uint16_t GetFrame(uint16_t province, uint16_t stone_id, uint8_t direction) const;

    // Destination stone index, or kDestGate/kDestWall/kDestNone for a
    // gate crossing/blocked direction/unavailable direction.
    int16_t GetDest(uint16_t province, uint16_t stone_id, uint8_t direction) const;

    const MapStone* GetStone(uint16_t province, uint16_t stone_id) const;

    uint16_t GetStoneCount(uint16_t province) const;

private:
    std::array<std::vector<MapStone>, kProvinceCount> provinces_;
    bool loaded_ = false;
};

}  // namespace openfear::game
