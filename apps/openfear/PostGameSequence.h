#pragma once

#include "openfear/game/BoardGameScreen.h"
#include "openfear/platform/Platform.h"

namespace openfear {

// Post-game sequence: ENDVID.AVI -> M2/PLAYAGN.BMP (Yes/No) -> credits roll.
// Returns true if the user picked "Yes", telling the caller to restart from
// IntroScreen.
bool RunPostGameSequence(game::BoardGameScreen& board_game, Platform* platform);

}  // namespace openfear
