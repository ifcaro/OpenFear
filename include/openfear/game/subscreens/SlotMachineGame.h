#pragma once

#include "openfear/game/Subscreen.h"
#include <chrono>

namespace openfear::game {

class BoardGameScreen;

// SlotMachineGame (enc type 14): 3-reel slot machine. 3-match on own
// char_id -> +50 power (jackpot); any other 3-match -> +20 power; no
// match -> -5 power.
class SlotMachineGame : public Subscreen {
public:
    SlotMachineGame(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
                    BoardGameScreen& board)
        : Subscreen(assets, renderer, audio), board_(board) {}

    void Enter(int player);
    void update(const IInput& input, float dt);
    void render();

    // Frees the lazy-loaded reel/background textures + entry audio.
    // Called from BoardGameScreen::FreeLazyMinigameAssets().
    void FreeAssets() override;

private:
    BoardGameScreen& board_;

    int player_ = 0;
    int reels_[3] = {};  // CURRENTLY DISPLAYED reel values 0-5 (idle, mid-spin, or final)
    int result_ = 0;     // 0=+20(small win), 1=+50(jackpot), 2=-5(loss)
    bool spun_ = false;
    bool done_ = false;
    bool clicked_ = false;
    bool focus_spin_ = true;  // gamepad discrete-focus: true=spin(left), false=cancel(right)
    // Teardown (both the cancel-decline path and the post-spin result
    // path) is gated on exit_wav_'s own audio-done completion, not fired
    // immediately on click.
    bool awaiting_exit_ = false;
    SoundHandle exit_wav_ = 0;  // reused for the cancel-decline and spin-result clips
    // Idle roll at screen-open (Enter(), BEFORE any click): reel0 =
    // RngNext()*2/0x8000 (0-1), reel1 = RngNext()*2/0x8000+2 (2-3), reel2
    // = RngNext()*2/0x8000+4 (4-5) — 3 RNG calls, each reel restricted to
    // its own 2-symbol range.
    // Spin animation, driven every 45ms, tick 0-49 (~2250ms total). The
    // final pre-determined reels (power-tiered) are computed upfront at
    // spin start (see do_spin in update()), not inside the tick loop. For
    // tick<41: each reel just increments by 1, wrapping 5->0 —
    // deterministic round-robin, NOT random. For tick>=41: each reel
    // snaps directly to its final target value.
    bool spinning_ = false;     // true while ticking through the 50-tick roll
    int target_reels_[3] = {};  // pre-determined final values (set at tick 1)
    int spin_tick_ = 0;         // 0-49
    // Loops W11/SPIN.WAV — replayed each time it finishes, for as long as ticks are still rolling.
    SoundHandle spin_tick_wav_ = 0;
    std::chrono::steady_clock::time_point anim_tick_time_;  // last 45ms tick timestamp
    TextureHandle bg_tex_ = 0;                              // G2/POKMACH.BMP
    SpriteInfo bg_sprite_{};                                // bg_tex_ wrapped for DrawFocusTint
    // Reel symbols — a single 558x253 sprite sheet, 6 real symbols in row
    // 0 (crop formula in render()).
    TextureHandle reel_tex_ = 0;  // G2/POKBLIT.BMP
    SoundHandle entry_wav_ = 0;   // RNG-picked 1 of W9/AT_0745/0764/0765/0766
};

}  // namespace openfear::game
