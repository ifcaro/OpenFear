#pragma once

#include <cstdint>

namespace openfear::game {

// Board initialization flow (GameState setup, turn-timer defaults) run once
// after MasterControlScreen confirms game duration/turn-timer settings,
// before entering the board game loop.
class BoardInitializer {
public:
    static bool Init(int duration_minutes, int turn_seconds);

    static bool IsInitialized();

private:
    static bool initialized_;
};

}  // namespace openfear::game
