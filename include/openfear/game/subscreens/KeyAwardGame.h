#pragma once

#include "openfear/game/Subscreen.h"
#include <chrono>

namespace openfear::game {

class BoardGameScreen;

// KeyAward (enc type 15): a single die roll against a precomputed 1-6
// target, entered via a GkLoopAVI intro voice line.
class KeyAwardGame : public Subscreen {
public:
    KeyAwardGame(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
                 BoardGameScreen& board)
        : Subscreen(assets, renderer, audio), board_(board) {}

    // No-op if the player already holds all 6 keys. Otherwise sets
    // current_state_ to GkLoopAVI, not directly to KeyAwardGame — the real
    // transition happens once the intro audio finishes.
    void Enter(int player);

    void update(const IInput& input);

    // Real dice overlay, drawn over the frozen GKLOOP intro frame, not the board.
    void render();

private:
    BoardGameScreen& board_;

    enum class Phase : uint8_t { Rolling, ShowingResult };
    Phase phase_ = Phase::Rolling;

    int player_ = 0;                                   // player index in the dice game
    int target_ = 0;                                   // precomputed target value 0-5
    int result_ = -1;                                  // rolled result 0-5, set at anim start
    int anim_ = 0;                                     // animation counter 0->25
    bool clicked_ = false;                             // player clicked to skip animation
    int keys_to_win_ = 1;                              // keys to award on win, variable 1-6
    std::chrono::steady_clock::time_point tick_time_;  // last animation tick timestamp
};

}  // namespace openfear::game
