#pragma once

#include "openfear/game/Subscreen.h"
#include "openfear/game/BoardGameState.h"
#include "openfear/assets/PntFile.h"
#include <chrono>
#include <vector>

namespace openfear::game {

class BoardGameScreen;

// Province minimap overlay — MAP0{province}.BMP (640x480) with a blinking
// player position marker. Dismissed by a left click (gamepad: B).
class MinimapScreen : public Subscreen {
public:
    MinimapScreen(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
                  BoardGameScreen& board)
        : Subscreen(assets, renderer, audio), board_(board) {}

    // Loads DATA/MAP{0-7}.PNT — stone pixel coordinates per province, tiny
    // binary tables, fine to keep all 8 resident. Called once from
    // BoardGameScreen::load().
    void LoadProvincePoints();

    // Loads MISC/MAP0{province}.BMP for whichever province the current
    // player is on right now. `return_state` is restored on dismiss.
    void Enter(BoardGameState return_state);

    bool update(const IInput& input);
    void render();
    void unload();
    void FreeAssets() override;

private:
    BoardGameScreen& board_;

    std::vector<assets::PointEntry> province_pnt_[8];  // stone pixel coords per province
    TextureHandle tex_ = 0;     // MISC/MAP0{current province}.BMP, owned; ONE reusable slot
    SoundHandle open_wav_ = 0;  // W9/AT_0739 or AT_0740, tracked so repeated opens don't stack
    BoardGameState return_state_ = BoardGameState::Moving;  // state to return to on dismiss
    std::chrono::steady_clock::time_point
        saved_start_time_;                // saved state_start_time_ (restored on dismiss)
    bool saved_action_executed_ = false;  // saved state_action_executed_ (restored on dismiss)
};

}  // namespace openfear::game
