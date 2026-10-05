#pragma once

#include "openfear/game/Subscreen.h"
#include "openfear/assets/RectFile.h"
#include <array>
#include <chrono>

namespace openfear::game {

class BoardGameScreen;

// DiceKeysGame (enc type 8): guess a number 1-10 against a secret target.
// Reward is pre-picked (power tapered by powerFactor; key-gain untapered)
// BEFORE the guess; guessing within tolerance of the (cosmetic) secret
// target 1-8 wins that pre-picked reward.
class DiceKeysGame : public Subscreen {
public:
    DiceKeysGame(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
                 BoardGameScreen& board)
        : Subscreen(assets, renderer, audio), board_(board) {}

    // Loads DATA/NUMS.BIN's 10 click zones. Called once from
    // BoardGameScreen::load().
    bool LoadZoneRects();

    // Loads the DOMIN08 guessing-phase background. Called from Enter().
    // The DOMIN09-17 result backgrounds load separately, one at a time,
    // in render().
    void LoadAssets();
    // Frees the DOMIN08 background and any loaded DOMIN09-17 result
    // textures. Called from BoardGameScreen::FreeLazyMinigameAssets().
    void FreeAssets() override;

    void Enter(int player);
    void update(const IInput& input, float dt);
    void render();

private:
    BoardGameScreen& board_;

    // Internal sub-phase — current_state_ stays BoardGameState::DiceKeysGame
    // throughout both; nothing outside this class needs to tell them apart.
    enum class Phase : uint8_t { Guessing, ShowingResult };
    Phase phase_ = Phase::Guessing;

    int player_ = 0;                                   // player index in this encounter
    bool clicked_ = false;                             // player has made a selection (debounce)
    std::chrono::steady_clock::time_point tick_time_;  // state entry / timeout reference
    int result_ = 0;                                   // 0=win, 1=loss
    int target_ = 1;         // secret target 1-8, cosmetic — the reward is pre-picked independently
    int selected_ = -1;      // number player clicked, -1=none
    int reward_id_ = 0;      // pre-picked reward: 0=+10 power,1=+20 power,2=+30 power,3=+1 key
    int tolerance_ = 1;      // 0 if reward_id_==3 (key, exact match required), else 1
    int key_province_ = -1;  // province granted on a reward_id_==3 win (-1=n/a), for result bitmap
    SoundHandle entry_wav_ = 0;  // entry clip, 4 variants
    std::chrono::steady_clock::time_point
        entry_audio_done_time_{};  // when entry audio finished — the real 4000ms timeout is only
                                   // armed once this happens; time_point::min() = not yet done
    bool timed_out_ =
        false;  // true once the 4s no-click timeout has fired (guards single audio play)
    SoundHandle timeout_wav_ = 0;  // timeout audio, 2 variants. No reward/penalty on timeout,
                                   // audio only.
    SoundHandle result_wav_ = 0;   // win/loss result clip handle — the result-wait phase waits for
                                   // this to finish instead of a flat 2s

    std::array<assets::RectEntry, 10> zone_rects_{};
    int focus_ = 0;  // 0-9, index into zone_rects_

    TextureHandle bg_tex_ = 0;  // G1/DOMIN08.BMP (picking-phase background, full 640x480)
    // G1/DOMIN09-17.BMP, 1 of 9 result screens; the index-to-filename mapping
    // is not monotonic with reward_id_/province.
    TextureHandle result_tex_ = 0;
};

}  // namespace openfear::game
