#include "openfear/game/subscreens/PauseScreen.h"
#include "openfear/game/BoardGameScreen.h"
#include "openfear/game/GameState.h"
#include <chrono>

namespace openfear::game {

void PauseScreen::Enter() {
    // Pausing stops the portrait theme immediately.
    if (board_.portrait_wav_) audio_.stop(board_.portrait_wav_);
    start_time_ = std::chrono::steady_clock::now();
    insult_wav_ = 0;
    insult_waiting_ = false;
    board_.current_state_ = BoardGameState::PauseScreen;
    board_.state_start_time_ = std::chrono::steady_clock::now();
    board_.state_action_executed_ = false;
}

bool PauseScreen::update(const IInput& input, float dt) {
    // Continue resumes + compensates game_duration_s_ for elapsed pause
    // time; Exit closes the app directly, no credits. Insult-loop pool
    // split documented on insult_wav_ in the header.
    static const char* kPauseInsultPoolA[12] = {
        "WAV\\AT_0471E.WAV", "WAV\\AT_0472C.WAV", "WAV\\AT_0474A.WAV", "WAV\\AT_0474B.WAV",
        "WAV\\AT_0474C.WAV", "WAV\\AT_0474D.WAV", "WAV\\AT_0474E.WAV", "WAV\\AT_0474F.WAV",
        "WAV\\AT_0474G.WAV", "WAV\\AT_0475A.WAV", "WAV\\AT_0475B.WAV", "WAV\\AT_0475C.WAV",
    };
    static const char* kPauseInsultPoolB[5] = {
        "WAV\\AT_0426.WAV",  "WAV\\AT_0514A.WAV", "WAV\\AT_0514B.WAV",
        "WAV\\AT_0514C.WAV", "WAV\\AT_0514E.WAV",
    };
    // A silent 5s gap precedes the first clip too, not just between clips
    // — one wait machine covers both the initial entry (insult_wav_==0)
    // and inter-clip gaps.
    if (insult_wav_ == 0 || !audio_.isPlaying(insult_wav_)) {
        const auto now = std::chrono::steady_clock::now();
        if (!insult_waiting_) {
            insult_waiting_ = true;
            insult_wait_start_ = now;
        } else if (std::chrono::duration_cast<std::chrono::milliseconds>(now - insult_wait_start_)
                       .count() >= 5000) {
            insult_waiting_ = false;
            const bool pick_pool_a = RollIndex(board_.RngNext(), 2) == 0;
            insult_wav_ =
                pick_pool_a
                    ? playWav(kPauseInsultPoolA[RollIndex(board_.RngNext(), 12)], AudioFlags::Async)
                    : playWav(kPauseInsultPoolB[RollIndex(board_.RngNext(), 5)], AudioFlags::Async);
        }
    }

    // Gamepad focus nav: 2 real rectangular button panels within PAUSE.BMP.
    // D-pad up/down toggles focus, A confirms.
    static const Point kPauseCenters[2] = {{318, 217}, {318, 321}};  // Continue, Exit
    bool confirm_continue = false, confirm_exit = false;
    if (board_.gamepad_mode_) {
        const auto& gp = input.gamepad();
        int dx = 0, dy = 0;
        if (gp.pressed[static_cast<size_t>(GamepadButton::DpadUp)]) dy = -1;
        if (gp.pressed[static_cast<size_t>(GamepadButton::DpadDown)]) dy = 1;
        if (dy == 0) board_.stick_repeater_.Poll(gp.left_x, gp.left_y, dt, dx, dy);
        if (dy != 0) {
            std::vector<Point> centers(kPauseCenters, kPauseCenters + 2);
            focus_ = FindNextFocus(centers, focus_, dx, dy);
        }
        if (gp.pressed[static_cast<size_t>(GamepadButton::A)]) {
            confirm_continue = (focus_ == 0);
            confirm_exit = (focus_ == 1);
        }
    }

    const auto& pm = input.mouse();
    if (pm.clicked[static_cast<int>(MouseButton::Left)]) {
        confirm_continue = (pm.x >= 76 && pm.x < 560 && pm.y >= 178 && pm.y < 256);
        confirm_exit = (pm.x >= 76 && pm.x < 560 && pm.y >= 282 && pm.y < 360);
    }

    if (confirm_continue) {
        GameState* game_state = GameStateManager::Instance().Get();
        if (game_state && game_state->IsValid()) {
            const long long paused_s = std::chrono::duration_cast<std::chrono::seconds>(
                                           std::chrono::steady_clock::now() - start_time_)
                                           .count();
            game_state->game_duration_s_ += static_cast<uint32_t>(paused_s);
        }
        // The real game explicitly tears down the pause-insult loop on
        // resume, right before the resume message.
        if (insult_wav_ != 0) {
            audio_.stop(insult_wav_);
            insult_wav_ = 0;
        }
        // The real game restarts the portrait theme from scratch on every
        // redisplay, pause or not — let ShowingPortrait's own setup block rerun.
        board_.current_state_ = BoardGameState::ShowingPortrait;
        board_.state_action_executed_ = false;
        board_.state_start_time_ = std::chrono::steady_clock::now();
    } else if (confirm_exit) {
        quit_requested_ = true;
        return true;
    }
    return false;
}

void PauseScreen::render() {
    if (!tex_) {
        tex_ = renderer_.loadBitmap(AssetPaths::data("M2", "PAUSE", "BMP").string());
    }
    if (tex_) {
        renderer_.drawBitmap(tex_, 0, 0, 0, 0, 640, 480);
    } else {
        // Fallback: black background + labelled button rects (byte-exact, see update())
        renderer_.fillRect(0, 0, 640, 480, 0, 0, 0);
        renderer_.fillRect(76, 178, 484, 78, 40, 40, 80);  // Continue
        renderer_.fillRect(76, 282, 484, 78, 80, 40, 40);  // Exit
    }

    // Gamepad focus indicator: additive glow pulse over the focused button's
    // rect, plus the gamepad cursor centered on it.
    if (board_.gamepad_mode_ && tex_) {
        static constexpr int kZoneX1[2] = {76, 76}, kZoneY1[2] = {178, 282};
        static constexpr int kZoneW = 484, kZoneH = 78;
        const int i = (focus_ == 1) ? 1 : 0;
        DrawFocusPulse(renderer_, SpriteInfo{}, kZoneX1[i], kZoneY1[i], kZoneX1[i], kZoneY1[i],
                       kZoneW, kZoneH);
        DrawFocusCursor(renderer_, kZoneX1[i] + kZoneW / 2, kZoneY1[i] + kZoneH / 2);
    }
}

void PauseScreen::unload() {
    ReleaseTexture(tex_);
}

}  // namespace openfear::game
