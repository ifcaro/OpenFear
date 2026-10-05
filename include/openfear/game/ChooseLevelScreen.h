#pragma once
#include "GameScreen.h"
#include "FocusIndicator.h"
#include <array>
#include <chrono>
#include <cstdint>

namespace openfear::game {

// Choose Level Screen — 1-player mode ONLY. Converts a continuous gauge
// (raw pixel offset within the thermometer bar) to a discrete difficulty
// level 0-9, which GameFlow.cpp's kDifficultyTable[10] expands into initial
// keys, AI count, and AI power.
//
// This screen does NOT write GameState.game_duration_s_ (game duration) —
// that's exclusively MasterControlScreen's job.
class ChooseLevelScreen : public GameScreen {
public:
    ChooseLevelScreen(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio)
        : GameScreen(assets, renderer, audio) {}

    bool load(const AssetPaths& paths);
    bool update(const IInput& input);
    void render();
    void unload();

    int selected_level() const { return selected_level_; }

private:
    enum class Phase { Init, Intro, Tracking, DurationVoicePlaying, Done };

    TextureHandle bg_{};           // 1PLYLEV.BMP — background
    TextureHandle sprite_{};       // 1PLYLBLT.BMP — gauge indicator
    SpriteInfo sprite_info_{};     // sprite_ wrapped for DrawFocusTint (gamepad focus)
    std::array<Rect, 2> zones_{};  // CHOOSLEV.BIN hit-test zones: [0]=screen bounds, [1]=bar

    int gauge_cur_ = 0;        // 0..(bar_height-1) raw pixel offset within the bar
    int selected_level_ = -1;  // final selection: difficulty 0-9, or -1 if not selected
    int32_t grace_period_remaining_ = 500;  // ms — discard early clicks (signed!)

    Phase phase_ = Phase::Init;
    SoundHandle intro_wav_ = 0;
    SoundHandle duration_wav_ = 0;  // one of 3 variants, picked by gauge position

    std::chrono::steady_clock::time_point last_frame_time_{};  // for update()'s own dt
};

}  // namespace openfear::game
