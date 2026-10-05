#pragma once
#include "GameScreen.h"
#include "FocusIndicator.h"
#include <array>
#include <chrono>

namespace openfear::game {

// Character selection screen. Shown once per player; previously-chosen
// characters are pre-marked as taken.
//
// Init: character order is randomized once, on the first player, and kept
//   for subsequent players. char_order_[cell] = char_id.
// Render: HEXAGON base; HEXAGONB region blitted over each taken/selected cell.
// On click: glass WAV plays + portrait (CHARS\INFO{n}_0.BMP) shown, then
//   CINTRO_{n}.WAV plays while portrait is displayed; done when it ends
//   (click again to skip).
class PickCharScreen : public GameScreen {
public:
    PickCharScreen(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio)
        : GameScreen(assets, renderer, audio) {}

    bool load(const AssetPaths& paths, int player_index, int n_players,
              const std::array<bool, 6>& taken);
    bool update(const IInput& input);
    void render();
    // Frees bg_/bg_sel_/portrait_ — call after the last player's iteration
    // (load() itself already frees between iterations).
    void unload();

    // Returns the char_id (0..5) of the selected character, or -1 if none.
    int selected_char() const { return (selected_ >= 0) ? char_order_[selected_] : -1; }

#ifndef NDEBUG
    // Debug-only, not from the original: skips the click and pre-selects a
    // specific char_id (0-5). Call right after load(), before the first
    // update() — the normal click->glass->CINTRO flow still runs from
    // there. No-op if char_id isn't in this screen's shuffled char_order_
    // (already taken).
    void DebugForceCharacter(int char_id) {
        for (int i = 0; i < 6; ++i) {
            if (char_order_[i] == char_id) {
                selected_ = i;
                return;
            }
        }
    }
#endif

private:
    TextureHandle bg_{};        // HEXAGON  — all cells available
    TextureHandle bg_sel_{};    // HEXAGONB — taken/selected cell state
    TextureHandle portrait_{};  // CHARS\INFO{char}_0.BMP (shown after click)
    std::array<Rect, 6> cell_rects_{};
    std::array<Point, 6> cell_centers_{};  // cell_rects_ centers, for D-pad directional focus
    std::array<bool, 6> taken_{};
    std::array<int, 6> char_order_{};  // shuffled: char_order_[cell] = char_id
    int selected_ = -1;                // clicked cell index
    int focus_index_ = 0;              // gamepad discrete-focus cell
    std::chrono::steady_clock::time_point
        focus_change_time_;         // reset whenever focus_index_ changes, anchors the
                                    // render-side pulse to peak brightness right on switch
    StickRepeater stick_repeater_;  // stick as D-pad alternative
    std::chrono::steady_clock::time_point stick_tick_time_;  // for stick_repeater_'s own dt
    SoundHandle voice_intro_wav_ = 0;                        // voice intro, plays in load()
    SoundHandle glass_wav_ = 0;   // glass click sound, precached then played on click
    SoundHandle cintro_wav_ = 0;  // character intro, played after click
    enum class WaitPhase { None, VoiceIntro, Glass, Cintro };
    WaitPhase wait_phase_ = WaitPhase::None;
};

}  // namespace openfear::game
