#pragma once

#include "openfear/game/Subscreen.h"
#include "openfear/assets/RectFile.h"
#include <array>
#include <chrono>

namespace openfear::game {

class BoardGameScreen;

// Flight Key teleport: province map + 6 clickable zones. Trigger (any
// F-key, mid-roll, on a type-6 stone — each province's single central
// stone, always stone 0 — with key_power_flags bit2) stays in
// BoardGameScreen's Moving case, since it's a movement-input
// gate, not this screen's own logic.
class FlightMap : public Subscreen {
public:
    FlightMap(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
              BoardGameScreen& board)
        : Subscreen(assets, renderer, audio), board_(board) {}

    // Lazy-loads the province-picker background + 6 click zones (fixed pixel
    // rects, no file read). Idempotent — FlightMap returns to Moving, not
    // TurnEnd, so this can be re-entered more than once per roll.
    void LoadAssets();
    void FreeAssets() override;

    // Resets focus_/selected_ and transitions current_state_. Does NOT
    // decrement steps_remaining (that stays in the Moving-case trigger,
    // alongside the eligibility gate).
    void Enter();

    void update(const IInput& input);
    void render();

private:
    // D-pad ring-neighbor nav + stick absolute-direction pick; moves focus_,
    // and sets selected_ on an A press.
    void UpdateGamepadFocus(const IInput& input);

    BoardGameScreen& board_;

    std::array<assets::RectEntry, 6> zones_{};
    int focus_ = 0;  // gamepad discrete-focus index into zones_
    std::chrono::steady_clock::time_point
        focus_change_time_;  // reset whenever focus_ changes, anchors the render-side pulse to peak
                             // brightness right on switch
    int selected_ = -1;      // -1 = still choosing; 0-5 = province chosen, AVI/teleport in progress
    SoundHandle open_wav_ = 0;  // W6/AT_0589 or AT_0590, stopped on province click
    TextureHandle bg_tex_ = 0;  // M1/FLIGHT.BMP — province-picker background
};

}  // namespace openfear::game
