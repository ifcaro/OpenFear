#pragma once

#include "openfear/game/Subscreen.h"
#include <chrono>

namespace openfear::game {

class BoardGameScreen;

// OneDie (enc type 9): roll a die, win if it matches a secretly rolled
// face.
class OneDieGame : public Subscreen {
public:
    OneDieGame(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
               BoardGameScreen& board)
        : Subscreen(assets, renderer, audio), board_(board) {}

    // Loads DIEBA/DICEBLIT + the 6 die-value voice clips. The prize-preview
    // background loads separately, in Enter(), once outcome_ is known.
    void LoadAssets();
    // Frees everything LoadAssets() loaded. Called from
    // BoardGameScreen::FreeLazyMinigameAssets().
    void FreeAssets() override;

    // No-op if only 1 active player.
    void Enter(int player);

    void update(const IInput& input);

    // Full-screen render.
    void render();

private:
    BoardGameScreen& board_;

    enum class Phase : uint8_t { Rolling, ShowingResult };
    Phase phase_ = Phase::Rolling;

    int player_ = 0;        // player index in the dice game
    int result_ = -1;       // rolled result 0-5, set at anim start. This is the SECOND
                            // independent roll — see voice_face_.
    int anim_ = 0;          // animation counter 0->25
    bool clicked_ = false;  // player clicked to skip animation
    // OneDie has TWO independent die rolls, not one.
    // voice_face_ is the first roll — rolled at encounter setup,
    // drives ONLY the pre-roll flavor voice and is announced
    // BEFORE the player can even start the actual roll. result_ (above)
    // becomes the SECOND, fully independent roll once the player clicks
    // to start rolling. The real prize (outcome_) is only actually
    // granted if the two rolls match — most rolls (~5/6) grant nothing,
    // just a "miss" taunt.
    int voice_face_ = -1;
    // Pre-computed prize id: -1=off, 0=+10pwr, 1=+30pwr, 2=+50pwr,
    // 3=DecrementCurrentPlayerCyclic
    int outcome_ = -1;
    // True once the player has clicked past the prize+voice announcement
    // and the real roll animation has started (result_ is only valid once
    // this is true).
    bool rolling_started_ = false;
    // Handle of the win/miss taunt started when the animation completes.
    // Teardown out of the result display is gated on THIS audio finishing,
    // not a fixed timer — update() doesn't read input at all while it plays.
    SoundHandle taunt_wav_ = 0;
    std::chrono::steady_clock::time_point tick_time_;  // last animation tick timestamp

    TextureHandle bg_tex_ = 0;  // G1/DIEBA.BMP background
    // G1/DICEBLIT.BMP die-face sprite sheet: 765x253, a 6x2 grid, 128px
    // stride per cell — row 0 = static faces 1-6, row 1 = blurred/rotated
    // faces 1-6 (rolling animation).
    TextureHandle diceblit_tex_ = 0;
    // Prize preview matching outcome_: G1/DIEBAC12-15.BMP for +10/+30/+50/turn-pass,
    // one of 6 G1/DIEBAC10-11+DIEBACK6-9.BMP key-color bitmaps for key-gain (picked
    // by province). Loaded once outcome_ is known, freed again the instant rolling
    // starts.
    TextureHandle prize_tex_ = 0;
    SoundHandle value_wavs_[6] = {};  // W8/AT_0711-AT_0716.WAV die voices (precached)
};

}  // namespace openfear::game
