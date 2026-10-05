#include "openfear/game/MasterControlScreen.h"
#include "openfear/game/GameState.h"
#include "openfear/game/PlayerInitializer.h"
#include <algorithm>
#include <cstdio>

namespace openfear::game {

// GameDuration counter's initial value lookup table based on total_players, in MINUTES
// (GameState::SetGameDurationMinutes() converts to seconds for game_duration_s_).
static const int kGameDurationInitTable[] = {
    60,  // 1P (difficulty 0-3): no AI
    40,  // Reached either way: 1P difficulty 4-6 (+1 AI), or a real 2-human game
    50,  // 1P difficulty 7-9 (+2 AI), or a real 3-human game
    60,  // Real 4-human game (1P-mode difficulty table never reaches this)
    70,  // Real 5-human game
    80,  // Real 6-human game
};

bool MasterControlScreen::load(const AssetPaths&, int total_players) {
    setScreenName("MasterControl");

    if (total_players < 1 || total_players > 6) {
        std::fprintf(stderr,
                     "[MasterControl] WARNING: invalid total_players=%d, clamping to [1,6]\n",
                     total_players);
        total_players = std::clamp(total_players, 1, 6);
    }

    game_duration_value_ = kGameDurationInitTable[total_players - 1];
    game_duration_default_ = game_duration_value_;  // Confirm-time audio compares final vs this

    phase_ = Phase::Init;
    audio_handle_ = 0;
    stick_tick_time_ = std::chrono::steady_clock::now();

    bg_ = assets_.texture("M2", "MCONTROL");
    bg_blit_ = assets_.texture("M2", "MCTLBLIT");

    if (!bg_) {
        std::fprintf(stderr, "[MasterControl] WARNING: Failed to load MCONTROL.BMP\n");
    }
    if (!bg_blit_) {
        std::fprintf(stderr, "[MasterControl] WARNING: Failed to load MCTLBLIT.BMP\n");
    }

    // Play random intro voice, unconditionally, right at screen-open.
    uint16_t random_val = PlayerInitializer::GetRandomNumber();
    const bool use_variant_b = (static_cast<uint32_t>(random_val) * 2 / 0x8000) != 0;
    intro_wav_ = playWav(use_variant_b ? "W8\\AT_0693.WAV" : "W8\\AT_0692.WAV", AudioFlags::Async);

    phase_ = Phase::Intro;
    return true;
}

bool MasterControlScreen::update(const IInput& input) {
    RefreshGamepadMode(input);

    // Phase 1: intro voice plays, but the screen transitions to GameDuration
    // immediately without waiting for it.
    if (phase_ == Phase::Intro) {
        phase_ = Phase::GameDuration;
        return false;
    }

    // Phase 2: GameDuration counter adjustment. Button zones: 0=Plus, 1=Minus, 2=Confirm.
    if (phase_ == Phase::GameDuration) {
        AdjustCounter(input, game_duration_value_, game_duration_min_, game_duration_max_, [this] {
            // Feedback voice depends on the FINAL value vs the
            // difficulty-table default. The GameDuration counter stays on
            // screen until that audio finishes (not an immediate blank-and-advance).
            play_game_duration_feedback(game_duration_value_, game_duration_default_);
            store_game_duration_to_gamestate();
            phase_ = Phase::WaitingGameDurationConfirmAudio;
        });
        return false;
    }

    // Phase 2b: hold the screen until the GameDuration confirm feedback
    // audio finishes, then advance to the TurnTime counter. A click/press
    // silences it immediately instead of waiting it out.
    if (phase_ == Phase::WaitingGameDurationConfirmAudio) {
        if (audio_handle_ && audio_.isPlaying(audio_handle_) && skip_requested(input)) {
            audio_.stop(audio_handle_);
        }
        if (!audio_handle_ || !audio_.isPlaying(audio_handle_)) {
            phase_ = Phase::TurnTime;
        }
        return false;
    }

    // Phase 3: TurnTime counter adjustment. Button zones: 0=Plus, 1=Minus, 2=Confirm.
    if (phase_ == Phase::TurnTime) {
        AdjustCounter(input, turn_time_value_, turn_time_min_, turn_time_max_, [this] {
            // Feedback voice depends on the FINAL value vs fixed 25.
            // Screen stays visible until that audio finishes (not an
            // immediate blank-and-advance).
            play_turn_time_feedback(turn_time_value_);
            store_turn_time_to_gamestate();
            phase_ = Phase::WaitingTurnTimeConfirmAudio;
        });
        return false;
    }

    // Phase 3b: hold the screen until the TurnTime confirm feedback audio
    // finishes. A click/press silences it immediately instead of waiting it out.
    if (phase_ == Phase::WaitingTurnTimeConfirmAudio) {
        if (audio_handle_ && audio_.isPlaying(audio_handle_) && skip_requested(input)) {
            audio_.stop(audio_handle_);
        }
        if (!audio_handle_ || !audio_.isPlaying(audio_handle_)) {
            phase_ = Phase::Done;
        }
        return false;
    }

    // Phase 4: Done — MasterControlScreen complete, advance to AI character assignment.
    if (phase_ == Phase::Done) {
        return true;
    }

    return false;
}

void MasterControlScreen::AdjustCounter(const IInput& input, int& value, int min_value,
                                        int max_value, const std::function<void()>& on_confirm) {
    const auto& mouse = input.mouse();
    const auto& gp = input.gamepad();
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - stick_tick_time_).count();
    stick_tick_time_ = now;

    // Gamepad: direct Up/Down/A bind, no focus grid — mouse/touch
    // still use the real 3-zone Plus/Minus/Confirm hit-test unchanged.
    int zone = -1;
    if (mouse.clicked[static_cast<int>(MouseButton::Left)] ||
        mouse.clicked[static_cast<int>(MouseButton::Right)]) {
        zone = hit_test_buttons(mouse.x, mouse.y);
    } else if (gamepad_mode_) {
        int dy = 0;
        // D-pad held[] fed through a separate, slower repeater than the stick.
        const float dpad_y = gp.held[static_cast<size_t>(GamepadButton::DpadUp)]     ? -1.0f
                             : gp.held[static_cast<size_t>(GamepadButton::DpadDown)] ? 1.0f
                                                                                     : 0.0f;
        dpad_repeater_.PollVertical(0.0f, dpad_y, dt, dy, StickRepeater::kDpadRepeatRate);
        if (dy == 0) stick_repeater_.PollVertical(gp.left_x, gp.left_y, dt, dy);
        if (dy < 0)
            zone = 0;  // Up   -> Plus/Increment
        else if (dy > 0)
            zone = 1;  // Down -> Minus/Decrement
        else if (gp.pressed[static_cast<size_t>(GamepadButton::A)])
            zone = 2;  // A -> Confirm
    }

    if (zone < 0) return;

    // Stops any currently-playing MasterControl audio on every zone
    // click before dispatching the click itself.
    if (intro_wav_ && audio_.isPlaying(intro_wav_)) audio_.stop(intro_wav_);
    if (audio_handle_ && audio_.isPlaying(audio_handle_)) audio_.stop(audio_handle_);

    if (zone == 0) {
        if (value < max_value) value++;
    } else if (zone == 1) {
        if (value > min_value) value--;
    } else if (zone == 2) {
        on_confirm();
    }
}

void MasterControlScreen::play_game_duration_feedback(int final_value, int default_value) {
    const char* wav_path = nullptr;

    if (final_value > default_value) {
        wav_path = "W8\\AT_0695.WAV";  // rose above default
    } else if (final_value < default_value) {
        wav_path = "W8\\AT_0697.WAV";  // fell below default
    } else {
        wav_path = "W9\\AT_0759.WAV";  // unchanged from default
    }

    audio_handle_ = playWav(wav_path, AudioFlags::Async);
}

void MasterControlScreen::play_turn_time_feedback(int final_value) {
    const char* wav_path = nullptr;

    if (final_value == 25) {
        wav_path = "W8\\AT_0699.WAV";  // landed exactly on 25
    } else if (final_value > 25) {
        wav_path = "W8\\AT_0698.WAV";  // ended up above 25
    } else {
        wav_path = "W8\\AT_0700.WAV";  // ended up below 25
    }

    audio_handle_ = playWav(wav_path, AudioFlags::Async);
}

void MasterControlScreen::store_game_duration_to_gamestate() {
    GameState* gs = GameStateManager::Instance().Get();
    if (!gs || !gs->IsValid()) {
        std::fprintf(stderr, "[MasterControl] ERROR: GameState not initialized\n");
        return;
    }

    gs->SetGameDurationMinutes(game_duration_value_);
}

void MasterControlScreen::store_turn_time_to_gamestate() {
    GameState* gs = GameStateManager::Instance().Get();
    if (!gs || !gs->IsValid()) {
        std::fprintf(stderr, "[MasterControl] ERROR: GameState not initialized\n");
        return;
    }

    gs->SetTurnTimeSeconds(turn_time_value_);
}

int MasterControlScreen::hit_test_buttons(int mouse_x, int mouse_y) const {
    for (int i = 0; i < kNumButtonZones; ++i) {
        if (PointInRect(mouse_x, mouse_y, button_zones_[i])) {
            const char* zone_name[] = {"Plus", "Minus", "Confirm"};
            return i;
        }
    }
    return -1;
}

bool MasterControlScreen::skip_requested(const IInput& input) const {
    const auto& mouse = input.mouse();
    if (mouse.clicked[static_cast<int>(MouseButton::Left)] ||
        mouse.clicked[static_cast<int>(MouseButton::Right)]) {
        if (hit_test_buttons(mouse.x, mouse.y) >= 0) return true;
    }
    return gamepad_mode_ && AnyPressed(input.gamepad());
}

void MasterControlScreen::render() {
    if (phase_ == Phase::Done) {
        return;
    }

    if (bg_) {
        renderer_.drawBitmap(bg_, 0, 0, 0, 0, 640, 480);
    }

    // Counter values render as individual digit sprites from MCTLBLIT.BMP:
    // 10 digit sprites (0-9) side by side, 91x103 each, digit[i] at
    // source_x = i*91.
    if (bg_blit_) {
        const bool showing_game_duration =
            phase_ == Phase::GameDuration || phase_ == Phase::WaitingGameDurationConfirmAudio;
        int counter_value = showing_game_duration ? game_duration_value_ : turn_time_value_;

        int ones = counter_value % 10;
        int tens = (counter_value / 10) % 10;
        int hundreds = (counter_value / 100) % 10;

        const int sprite_width = 91;
        const int sprite_height = 103;
        const int sprite_y = 0;

        // GameDuration counter (top): 3 digits. Also drawn during
        // WaitingGameDurationConfirmAudio, since the screen stays visible
        // with the confirmed value until the audio ends.
        if (showing_game_duration) {
            const int base_y = 90 + 19;
            renderer_.drawBitmap(bg_blit_, 176, base_y, hundreds * sprite_width, sprite_y,
                                 sprite_width, sprite_height);
            renderer_.drawBitmap(bg_blit_, 272, base_y, tens * sprite_width, sprite_y, sprite_width,
                                 sprite_height);
            renderer_.drawBitmap(bg_blit_, 369, base_y, ones * sprite_width, sprite_y, sprite_width,
                                 sprite_height);
        }
        // TurnTime counter (bottom): 2 digits, same X positions as the
        // GameDuration counter's tens/ones. Also drawn during
        // WaitingTurnTimeConfirmAudio, since the screen stays visible with
        // the confirmed value until the audio ends.
        else if (phase_ == Phase::TurnTime || phase_ == Phase::WaitingTurnTimeConfirmAudio) {
            const int base_y = 90 + 19;
            const int tens_x = 272;
            const int ones_x = 369;

            renderer_.drawBitmap(bg_blit_, tens_x, base_y, tens * sprite_width, sprite_y,
                                 sprite_width, sprite_height);
            renderer_.drawBitmap(bg_blit_, ones_x, base_y, ones * sprite_width, sprite_y,
                                 sprite_width, sprite_height);
        }
    }
}

void MasterControlScreen::unload() {
    ReleaseTexture(bg_);
    ReleaseTexture(bg_blit_);
}

}  // namespace openfear::game
