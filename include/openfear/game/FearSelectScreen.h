#pragma once
#include "GameScreen.h"
#include "FocusIndicator.h"
#include <array>
#include <chrono>

namespace openfear::game {

// Greatest Fear Selection Screen — each player picks which of 6 fears
// they're most afraid of.
//
// Audio state machine (4 phases):
//   Init          — screen load; no audio yet
//   OpeningPrompt — random opening voice; accept clicks
//   Selection     — user clicked fear; glass plays, then confirm prompt
//   Done          — confirm audio done; ready to advance
class FearSelectScreen : public GameScreen {
public:
    FearSelectScreen(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio)
        : GameScreen(assets, renderer, audio) {}

    bool load(const AssetPaths& paths, int player_index, const std::array<bool, 6>& taken = {});
    bool update(const IInput& input);
    void render();
    // Frees bg_/bg_sel_ — call after the last player's iteration (load()
    // itself already frees between iterations).
    void unload();

    int selected_fear() const { return selected_; }

private:
    enum class Phase { Init, OpeningPrompt, Selection, Done };

    TextureHandle bg_{};                   // GFEARS_2.BMP background
    TextureHandle bg_sel_{};               // GFEARS_0.BMP selection overlay
    std::array<Rect, 6> cell_rects_{};     // hit-test rectangles from GREATFEA.BIN
    std::array<Point, 6> cell_centers_{};  // cell_rects_ centers, for D-pad directional focus
    std::array<bool, 6> taken_{};  // availability (true = already selected by previous player)
    int selected_ = -1;            // selected fear index (0-5), or -1 if none
    int player_index_ = -1;
    int focus_index_ = 0;  // gamepad discrete-focus cell
    std::chrono::steady_clock::time_point
        focus_change_time_;         // reset whenever focus_index_ changes, anchors the render-side
                                    // pulse to peak brightness right on switch
    StickRepeater stick_repeater_;  // stick as D-pad alternative
    std::chrono::steady_clock::time_point stick_tick_time_;  // for stick_repeater_'s own dt

    Phase phase_ = Phase::Init;
    SoundHandle opening_wav_ = 0;  // currently playing opening prompt
    SoundHandle glass_wav_ = 0;    // glass click sound (precached in load, played on click)
    SoundHandle confirm_wav_ = 0;  // currently playing confirm prompt
};

}  // namespace openfear::game
