#pragma once
#include "GameScreen.h"
#include "FocusIndicator.h"
#include "openfear/platform/IVideoPlayer.h"
#include <array>
#include <chrono>

namespace openfear::game {

// Phase 1 (ClickSelect): static PKPLYRS.BMP background + 6 click zones +
//   intro voice. Click stops the voice immediately and advances to Phase 2.
// Phase 2 (CountAudio): GKLOOP1.AVI fullscreen + count confirmation audio,
//   starting only after Phase 1's click, never concurrent with the 6 zones.
class PickPlayersScreen : public GameScreen {
public:
    PickPlayersScreen(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
                      IVideoPlayer& video)
        : GameScreen(assets, renderer, audio), video_(video) {}

    bool load(const AssetPaths& paths);
    bool update(const IInput& input);
    void render();
    void unload();

    int player_count() const { return player_count_; }

private:
    enum class Phase { ClickSelect, CountAudio };

    void enterCountAudioPhase();

    // Count audio resources — one per player count (index = count-1).
    static const char* kCountWavs[6];

    IVideoPlayer& video_;
    Phase phase_ = Phase::ClickSelect;

    // Phase 1 state (click select)
    TextureHandle bg_{};
    SpriteInfo bg_sprite_{};  // bg_, wrapped for drawSpriteTinted (gamepad focus pulse)
    std::array<Rect, 6> cell_rects_{};
    std::array<Point, 6> cell_centers_{};  // cell_rects_ centers, for D-pad directional focus
    SoundHandle voice_ = 0;
    int player_count_ = 0;
    int focus_index_ = 0;  // gamepad discrete-focus cell
    std::chrono::steady_clock::time_point
        focus_change_time_;         // reset whenever focus_index_ changes, anchors the render-side
                                    // pulse to peak brightness right on switch
    StickRepeater stick_repeater_;  // stick as D-pad alternative
    std::chrono::steady_clock::time_point stick_tick_time_;  // for stick_repeater_'s own dt

    // Phase 2 state (GKLOOP + count audio)
    bool video_loaded_ = false;   // true once play() has been called - safe to renderFrame()
    bool video_open_ok_ = false;  // true once open() succeeded - safe to play() when ready
    SoundHandle count_wav_ = 0;
};

}  // namespace openfear::game
