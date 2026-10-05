#pragma once
#include "GameScreen.h"
#include "FocusIndicator.h"
#include <chrono>
#include <functional>

namespace openfear::game {

// Master Control Screen — sequential 2-counter adjustment for game
// parameters. Users adjust counters one at a time (Counter #1 -> Counter #2).
//
// Counter #1: game duration, 5-998 minutes (3-digit on-screen display).
//   - Initial value depends on total_players (1P=60, 2P=40, 3P=50, 4P=60, 5P=70, 6P=80)
//   - Stored to GameState.game_duration_s_ as seconds (minutes * 60)
// Counter #2: time per turn, 10-60 seconds, 25 is a special boundary.
//   - Always initializes to 25
//   - Stored to GameState.turn_time_limit_ms_ as milliseconds (seconds * 1000)
//
// Confirm-time audio (GameDuration vs difficulty-table default, TurnTime vs
// fixed 25) plays once when Confirm is clicked, not on every +/- click.
//
// Bitmaps: M2\MCONTROL.BMP (background), M2\MCTLBLIT.BMP (counter digits).
class MasterControlScreen : public GameScreen {
public:
    MasterControlScreen(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio)
        : GameScreen(assets, renderer, audio) {}

    // total_players: human + AI opponents from ChooseLevelScreen's difficulty
    // (1P mode) or just the human count (2+P, no AI in multiplayer).
    bool load(const AssetPaths& paths, int total_players);

    bool update(const IInput& input);
    void render();
    void unload();

    int gameDurationMinutes() const { return game_duration_value_; }
    int turnTimeSeconds() const { return turn_time_value_; }

private:
    // WaitingGameDurationConfirmAudio/WaitingTurnTimeConfirmAudio: each
    // counter's Confirm only advances once its own feedback audio finishes
    // playing — Counter #1 holds on screen until GameDuration's audio ends,
    // same as Counter #2 holds until TurnTime's.
    enum class Phase : int {
        Init = 0,
        Intro = 1,
        GameDuration = 2,
        TurnTime = 3,
        Done = 4,
        WaitingTurnTimeConfirmAudio = 5,
        WaitingGameDurationConfirmAudio = 6
    };

    int game_duration_value_ = 0;  // current value, in minutes
    int game_duration_min_ = 5;    // real floor is 5 minutes, not 0
    int game_duration_max_ = 998;
    int game_duration_default_ = 0;  // difficulty-table default, for Confirm-time audio compare

    int turn_time_value_ = 25;  // current value, starts at 25
    int turn_time_min_ = 10;
    int turn_time_max_ = 60;

    Phase phase_ = Phase::Init;

    // Button hit-test zones (hardcoded in the original at a fixed offset,
    // not from any .BIN resource). Index 0 = Plus, 1 = Minus, 2 = Confirm.
    static constexpr int kNumButtonZones = 3;
    Rect button_zones_[kNumButtonZones] = {
        {176, 217, 317, 254},  // [0] Plus/Increment button
        {318, 217, 460, 254},  // [1] Minus/Decrement button
        {263, 295, 372, 377},  // [2] Confirm button
    };
    // Gamepad: no focus grid — direct Up/Down/A bind (same style as
    // Moving/RollingDice), simpler UX than aiming at 3 small buttons.
    StickRepeater stick_repeater_;  // stick as D-pad alternative
    // Separate instance: D-pad held[] fed through it as a fully-deflected
    // stick, repeat rate deliberately slower than stick_repeater_'s own default.
    StickRepeater dpad_repeater_;
    std::chrono::steady_clock::time_point stick_tick_time_;  // for both repeaters' own dt

    TextureHandle bg_{};
    TextureHandle bg_blit_{};
    SoundHandle intro_wav_ = 0;     // intro voice handle
    SoundHandle audio_handle_ = 0;  // current audio playback handle

    int hit_test_buttons(int mouse_x, int mouse_y) const;  // returns zone index (0-2) or -1
    // A click on any of the 3 real zones, or any gamepad button, silences
    // the pending confirm audio immediately instead of waiting it out.
    bool skip_requested(const IInput& input) const;
    // Shared Plus/Minus/Confirm handling for both GameDuration and TurnTime
    // phases: mouse hit-test or gamepad Up/Down/A, clamped +/-1 on
    // value, on_confirm invoked once on the Confirm zone.
    void AdjustCounter(const IInput& input, int& value, int min_value, int max_value,
                       const std::function<void()>& on_confirm);
    void play_game_duration_feedback(int final_value, int default_value);
    void play_turn_time_feedback(int final_value);
    void store_game_duration_to_gamestate();
    void store_turn_time_to_gamestate();
};

}  // namespace openfear::game
