#include "openfear/game/subscreens/KeyAwardGame.h"
#include "openfear/game/BoardGameScreen.h"
#include "openfear/game/GameState.h"
#include <chrono>

namespace openfear::game {

void KeyAwardGame::Enter(int player) {
    const int held_keys = board_.CountHeldKeys(player);
    if (held_keys >= 6) {
        return;
    }

    GameState* gs = GameStateManager::Instance().Get();
    const long long total_duration_s = gs ? static_cast<long long>(gs->game_duration_s_) : 0;
    const long long elapsed_s = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - board_.game_start_time_)
                                    .count() /
                                1000;
    const long long remaining_s = total_duration_s - elapsed_s;
    const bool restricted = board_.ComputeKeyAwardRestricted(remaining_s, held_keys);
    // RngNext() must only be called when restricted (matches the real short-circuit
    // ternary — an unconditional call here would consume an extra RNG value on the
    // unrestricted path and desync the RNG stream).
    const int raw_target = restricted
                               ? board_.ComputeKeyAwardTarget(true, held_keys, board_.RngNext())
                               : board_.ComputeKeyAwardTarget(false, held_keys, 0);

    player_ = player;
    // result_ rolls in [0,5]; store target as [0,5] too (raw_target is 1-6).
    target_ = raw_target - 1;
    result_ = -1;
    anim_ = 0;
    clicked_ = false;
    keys_to_win_ = raw_target;
    phase_ = Phase::Rolling;
    // Intro voice line, 1 of 6 clips selected by raw_target.
    static const char* kKeyAwardIntroWav[6] = {"W2\\AT_0201.WAV", "W2\\AT_0202.WAV",
                                               "W2\\AT_0203.WAV", "W2\\AT_0204.WAV",
                                               "W2\\AT_0205.WAV", "W2\\AT_0206.WAV"};
    const int gkloop_variant = RollIndex(board_.RngNext(), 3) + 1;
    board_.gkloop_avi_path_ =
        AssetPaths::avi("AVI", std::string("GKLOOP") + std::to_string(gkloop_variant)).string();
    board_.gkloop_enc_audio_ = playWav(kKeyAwardIntroWav[raw_target - 1], AudioFlags::Async);
    board_.gkloop_post_state_ = BoardGameState::KeyAwardGame;
    board_.video_.close();
    board_.video_loaded_ = false;
    board_.video_.setRenderRect(BoardGameScreen::kBoardAreaX, BoardGameScreen::kBoardAreaY,
                                BoardGameScreen::kBoardAreaW, BoardGameScreen::kBoardAreaH);
    if (board_.video_.open(board_.gkloop_avi_path_)) board_.video_.play(true);
    board_.current_state_ = BoardGameState::GkLoopAVI;
    board_.state_start_time_ = std::chrono::steady_clock::now();
    board_.state_action_executed_ = true;
}

void KeyAwardGame::update(const IInput& input) {
    if (phase_ == Phase::ShowingResult) {
        // Gated on ROLLED.WAV finishing naturally — resolves the instant the handle
        // signals done, no fixed wait.
        const bool advance_result =
            board_.rolled_wav_ == 0 || !audio_.isPlaying(board_.rolled_wav_);
        if (advance_result && !board_.BathoryKeyRedirectAudioBusy()) {
            board_.current_state_ = BoardGameState::TurnEnd;
            board_.state_start_time_ = std::chrono::steady_clock::now();
            board_.state_action_executed_ = false;
        }
        return;
    }

    // One-die roll interactive game. Animation counter: 0->25 at ~80ms/tick (~2s
    // total). Player click: if anim >= 2, skip to 25 immediately.
    if (!board_.state_action_executed_) {
        board_.state_action_executed_ = true;
        anim_ = 0;
        clicked_ = false;
        result_ = -1;

        // Freeze the intro GKLOOP on whatever frame it's on — one-shot, only runs
        // here at state entry, not every render() frame.
        if (board_.video_.status() == VideoStatus::Idle) {
            board_.video_.setRenderRect(BoardGameScreen::kBoardAreaX, BoardGameScreen::kBoardAreaY,
                                        BoardGameScreen::kBoardAreaW, BoardGameScreen::kBoardAreaH);
            if (board_.video_.open(board_.gkloop_avi_path_)) {
                board_.video_.play(true);
            }
        }
        board_.video_.pause();
        tick_time_ = std::chrono::steady_clock::now();
        if (board_.rolling_wav_) audio_.stop(board_.rolling_wav_);
        if (board_.rolling_wav_ != 0) {
            audio_.playLoaded(board_.rolling_wav_, AudioFlags::Async | AudioFlags::Loop);
        }
        result_ = RollIndex(board_.RngNext(), 6);
    }

    const bool skip_click =
        input.mouse().clicked[static_cast<int>(MouseButton::Left)] ||
        input.mouse().clicked[static_cast<int>(MouseButton::Right)] ||
        (board_.gamepad_mode_ && input.gamepad().pressed[static_cast<size_t>(GamepadButton::A)]);
    if (skip_click && !clicked_) {
        clicked_ = true;
        if (anim_ >= 2) {
            anim_ = 25;
        }
    }

    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();
    const long long elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - tick_time_).count();
    if (elapsed_ms >= 80 || anim_ == 25) {
        tick_time_ = now;
        ++anim_;

        if (anim_ > 25) {
            if (board_.rolling_wav_) audio_.stop(board_.rolling_wav_);
            if (board_.rolled_wav_ != 0) {
                audio_.playLoaded(board_.rolled_wav_, AudioFlags::Async);
            }
            const int player_idx = player_;
            const bool win = (result_ == target_);
            if (win) {
                // Win: grant keys_to_win_ keys at once.
                for (int k = 0; k < keys_to_win_; ++k) {
                    const int province = board_.FindUnheldProvince(player_idx);
                    if (province >= 0) board_.GainKey(player_idx, province);
                }
            }

            phase_ = Phase::ShowingResult;
            board_.state_start_time_ = std::chrono::steady_clock::now();
            board_.state_action_executed_ = false;
        }
    }
}

void KeyAwardGame::render() {
    // Draws the same MVDICE.BMP die as a normal 1-die movement roll (45,403), over
    // the frozen GKLOOP frame already on screen. Kept separate from movement's
    // dice_visible logic on purpose — reusing it risks touching validated rendering.
    auto face_src_x = [](int val) -> int {
        int v = (val >= 1 && val <= 6) ? val : 6;
        return v * 100 - 99;
    };
    if (phase_ == Phase::Rolling) {
        // Spinning: bottom row, cycle faces using the existing anim_ (0-25) counter.
        int face_index = anim_ % 6;
        renderer_.drawBitmap(board_.dice_tex_, 45, 403, face_index * 100 + 1, 76, 99, 74);
    } else {
        // ShowingResult: stopped, top row, show the actual rolled face
        // (result_ is 0-5; MVDICE face values are 1-6).
        renderer_.drawBitmap(board_.dice_tex_, 45, 403, face_src_x(result_ + 1), 0, 99, 75);
    }
}

}  // namespace openfear::game
