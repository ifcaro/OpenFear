#pragma once

#include <cstdint>

namespace openfear::game {

// Per-player scratch storage written during setup (PickCharScreen/
// FearSelectScreen via PlayerInitializer), read once by BoardGameScreen
// when it allocates its own render_state_ array.
struct PlayerObject {
    uint16_t character_id = 0xffff;  // selected character (0-5), 0xffff = undefined
    uint16_t human_flag = 0;         // 0 = AI slot, 1 = human (set by FearSelectScreen)
};

}  // namespace openfear::game
