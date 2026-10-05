#pragma once

#include "openfear/game/Subscreen.h"
#include <chrono>

namespace openfear::game {

class BoardGameScreen;

// M2/PAUSE.BMP modal, 2 buttons (Continue/Exit).
class PauseScreen : public Subscreen {
public:
    PauseScreen(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
                BoardGameScreen& board)
        : Subscreen(assets, renderer, audio), board_(board) {}

    // Captures start_time_, resets the insult-loop state, stops the
    // portrait theme, transitions current_state_ to PauseScreen.
    void Enter();

    // Returns true if the player chose Exit (BoardGameScreen::update()
    // must propagate this up as its own return value).
    bool update(const IInput& input, float dt);
    void render();
    void unload();

    // Exit clicked — GameFlow.cpp must skip post-game when this is true
    // (BoardGameScreen::pause_quit_requested() delegates here).
    bool quitRequested() const { return quit_requested_; }

private:
    BoardGameScreen& board_;

    std::chrono::steady_clock::time_point start_time_;  // captured when PauseScreen entered
    bool quit_requested_ = false;
    // Repeating random-insult loop while paused (50% from a 12-clip pool,
    // 50% from a 5-clip pool), re-armed on each clip's own audio-done, for
    // as long as PauseScreen stays open.
    SoundHandle insult_wav_ = 0;
    // Real 5-second wait after each clip finishes before the next fires.
    bool insult_waiting_ = false;
    std::chrono::steady_clock::time_point insult_wait_start_;

    TextureHandle tex_ = 0;  // M2/PAUSE.BMP, cached
    int focus_ = 0;          // 0=Continue, 1=Exit
};

}  // namespace openfear::game
