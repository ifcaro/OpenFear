#pragma once

#include "GameScreen.h"

namespace openfear::game {

// Base for the 15 subscreens BoardGameScreen delegates to: 8 enc_type-roll
// minigames, 4 triggered by other mechanisms, FlightMap, PauseScreen, and
// MinimapScreen. Adds nothing beyond GameScreen except a virtual
// FreeAssets(), so BoardGameScreen::FreeLazyMinigameAssets() can sweep
// every subscreen's lazy-loaded assets through one shared array instead
// of scattered per-screen calls — still hand-maintained, not foolproof; a
// new subscreen must be added there too.
class Subscreen : public GameScreen {
public:
    virtual ~Subscreen() = default;

    virtual void FreeAssets() {}

protected:
    using GameScreen::GameScreen;
};

}  // namespace openfear::game
