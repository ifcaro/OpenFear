#pragma once

#include "openfear/game/Subscreen.h"

namespace openfear::game {

class BoardGameScreen;

// BribeGame (enc type 12): pay 10 power to keep a held key, or let a 3s
// grace timer expire and lose it.
class BribeGame : public Subscreen {
public:
    BribeGame(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
              BoardGameScreen& board)
        : Subscreen(assets, renderer, audio), board_(board) {}

    // Skips entirely (no state/bitmap/audio change) if the player holds no
    // key or has <10 power. Loads the single BRIBES{province}.BMP for the
    // key at stake + timeout WAV; freed again at this encounter's own
    // conclusion, not deferred to board teardown.
    void Enter(int player);

    void update(const IInput& input, float dt);
    void render();

    // Frees the lazy-loaded offer/accept/timeout audio + BRIBES{province}.BMP
    // texture. Called from BoardGameScreen::FreeLazyMinigameAssets().
    void FreeAssets() override;

private:
    BoardGameScreen& board_;

    int player_ = 0;
    int province_ = -1;  // key at stake, from FindHeldProvinceByRotation
    int phase_ = 0;      // 0=offer audio, 1=grace window, 2=pay audio, 3=lose audio
    bool focus_accept_ =
        true;  // gamepad discrete-focus: true=accept(-10, right), false=decline(key, left)
    SoundHandle active_audio_ = 0;  // whichever of offer/accept/timeout is currently playing
    SoundHandle timeout_wav_ = 0;   // W6/AT_0593 (lose-key path) - always used once triggered
    TextureHandle tex_ = 0;         // M1/BRIBES{province_}.BMP, the one key at stake
};

}  // namespace openfear::game
