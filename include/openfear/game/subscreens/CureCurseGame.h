#pragma once

#include "openfear/game/Subscreen.h"
#include <chrono>

namespace openfear::game {

class BoardGameScreen;

// The Gatekeeper's periodic curse-cure offer (pay 25% power to clear the
// curse). AI: instant, free, silent cure. Human: fixed offer line over
// GKLOOP AVI -> 4000ms 2-zone accept/decline window -> random result taunt
// over GKLOOP AVI -> TurnEnd.
class CureCurseGame : public Subscreen {
public:
    CureCurseGame(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
                  BoardGameScreen& board)
        : Subscreen(assets, renderer, audio), board_(board) {}

    // Loads DATA/CURECURS.BIN's 2 rects (accept/decline). Called once from
    // BoardGameScreen::load().
    bool LoadZoneRects();

    // `curse_type` is the player's current curse type (1-6), used to pick
    // the offer audio clip.
    void Enter(int player, uint16_t curse_type);

    void update(const IInput& input, float dt);
    void render();

    // Frees the lazy-loaded offer/taunt audio + CURSICON.BMP texture.
    // Called from BoardGameScreen::FreeLazyMinigameAssets().
    void FreeAssets() override;

private:
    BoardGameScreen& board_;

    int player_ = 0;
    int phase_ = 0;             // 0=offer audio (GKLOOP), 1=2-zone window, 2=result taunt (GKLOOP)
    bool focus_accept_ = true;  // gamepad discrete-focus: true=accept(left), false=decline(right)
    std::chrono::steady_clock::time_point
        focus_change_time_;  // reset whenever focus_accept_ changes, anchors the render-side
                             // pulse to peak brightness right on switch
    SoundHandle active_audio_ = 0;
    SoundHandle offer_wav_ = 0;         // W8/AT_0690,0687,0689,0688,0685,0686 - only the one
                                        // for the current curse_type (1-6) loads
    TextureHandle bg_tex_ = 0;          // M1/CURSICON.BMP
    SpriteInfo bg_sprite_ = {};         // bg_tex_ wrapped for DrawFocusTint
    assets::RectEntry accept_rect_{};   // DATA/CURECURS.BIN rect 0
    assets::RectEntry decline_rect_{};  // DATA/CURECURS.BIN rect 1
};

}  // namespace openfear::game
