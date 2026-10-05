#include "openfear/game/subscreens/DiceKeysGame.h"
#include "openfear/game/BoardGameScreen.h"
#include <chrono>
#include <cmath>

namespace openfear::game {

bool DiceKeysGame::LoadZoneRects() {
    return LoadRectZoneOrFail("nums", 10, zone_rects_.data(),
                              "NUMS.BIN not loaded/wrong size — number-picker zones would "
                              "never respond to clicks");
}

void DiceKeysGame::LoadAssets() {
    bg_tex_ = assets_.texture("G1", "DOMIN08");
}

void DiceKeysGame::FreeAssets() {
    if (bg_tex_ == 0) return;
    ReleaseTexture(bg_tex_);
    ReleaseTexture(result_tex_);
}

void DiceKeysGame::Enter(int player) {
    // Flat 25% pick across all 4 outcomes (+10/+20/+30 power, +1 key) — no
    // key/power/time gating at all.
    LoadAssets();
    player_ = player;
    clicked_ = false;
    target_ = RollIndex(board_.RngNext(), 8) + 1;  // 1-8, cosmetic
    selected_ = -1;
    {
        const int ids[4] = {0, 1, 2, 3};  // +10, +20, +30, key
        BoardGameScreen::WeightedPickEntry entries[4];
        int n = 0;
        BoardGameScreen::AppendWeightedCategory(ids, 4, 0.0f, 4, entries, n);
        reward_id_ = board_.PickWeightedEntry(entries, n);
    }
    tolerance_ = (reward_id_ == 3) ? 0 : 1;
    entry_wav_ = 0;
    entry_audio_done_time_ = std::chrono::steady_clock::time_point::min();
    timed_out_ = false;
    timeout_wav_ = 0;
    result_wav_ = 0;
    tick_time_ = std::chrono::steady_clock::now();
    board_.video_.close();
    board_.video_loaded_ = false;
    phase_ = Phase::Guessing;
    board_.current_state_ = BoardGameState::DiceKeysGame;
    board_.state_start_time_ = std::chrono::steady_clock::now();
    board_.state_action_executed_ = false;
}

void DiceKeysGame::update(const IInput& input, float dt) {
    if (phase_ == Phase::ShowingResult) {
        // Advances to TurnEnd once result_wav_ finishes, so the wait matches
        // each result clip's real duration.
        const bool result_audio_done = result_wav_ == 0 || !audio_.isPlaying(result_wav_);
        if (result_audio_done && !board_.BathoryKeyRedirectAudioBusy()) {
            board_.current_state_ = BoardGameState::TurnEnd;
            board_.state_start_time_ = std::chrono::steady_clock::now();
            board_.state_action_executed_ = false;
        }
        return;
    }

    using Clock = std::chrono::steady_clock;

    // Outside tolerance_ is a plain loss (no effect). A 4s timeout doesn't resolve
    // at all — no reward, no penalty, straight to TurnEnd.
    if (!board_.state_action_executed_) {
        board_.state_action_executed_ = true;
        entry_audio_done_time_ = Clock::time_point::min();
        // Entry audio plays unconditionally at setup.
        static const char* kDiceKeysEntryWav[4] = {"W2\\AT_0164.WAV", "W2\\AT_0165.WAV",
                                                   "W2\\AT_0166.WAV", "W6\\AT_0618.WAV"};
        entry_wav_ = playWav(kDiceKeysEntryWav[RollIndex(board_.RngNext(), 4)], AudioFlags::Async);
    }

    // The real 4000ms timer is armed only once the entry audio actually finishes —
    // not at setup.
    if (entry_audio_done_time_ == Clock::time_point::min() &&
        (entry_wav_ == 0 || !audio_.isPlaying(entry_wav_))) {
        entry_audio_done_time_ = Clock::now();
        tick_time_ = entry_audio_done_time_;
    }
    const bool dicekeys_timer_armed = entry_audio_done_time_ != Clock::time_point::min();

    const auto elapsed_ms =
        dicekeys_timer_armed
            ? std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - tick_time_)
                  .count()
            : 0;

    // Click hit-test: 10 real scattered zones from DATA/NUMS.BIN (numbers 1-10).
    const bool any_click = input.mouse().clicked[static_cast<int>(MouseButton::Left)] ||
                           input.mouse().clicked[static_cast<int>(MouseButton::Right)];
    // Gamepad focus nav: the 10 zones are scattered with no grid structure and painted
    // directly into the background (no separable sprite to tint), so this uses
    // FindNextFocus + DrawFocusCursor (real cursor snapped to the zone's center).
    // D-pad moves focus, A confirms.
    if (board_.gamepad_mode_ && !clicked_) {
        const auto& gp = input.gamepad();
        int dx = 0, dy = 0;
        if (gp.pressed[static_cast<size_t>(GamepadButton::DpadLeft)]) dx = -1;
        if (gp.pressed[static_cast<size_t>(GamepadButton::DpadRight)]) dx = 1;
        if (gp.pressed[static_cast<size_t>(GamepadButton::DpadUp)]) dy = -1;
        if (gp.pressed[static_cast<size_t>(GamepadButton::DpadDown)]) dy = 1;
        if (dx == 0 && dy == 0) board_.stick_repeater_.Poll(gp.left_x, gp.left_y, dt, dx, dy);
        if (dx != 0 || dy != 0) {
            std::vector<Point> centers;
            centers.reserve(10);
            for (int n = 0; n < 10; ++n) {
                const auto& z = zone_rects_[n];
                centers.push_back({static_cast<int16_t>((z.x1 + z.x2) / 2),
                                   static_cast<int16_t>((z.y1 + z.y2) / 2)});
            }
            focus_ = FindNextFocus(centers, focus_, dx, dy);
        }
        if (gp.pressed[static_cast<size_t>(GamepadButton::A)] && !timed_out_) {
            selected_ = focus_ + 1;  // 1-10
            clicked_ = true;
        }
    }

    // Once timed_out_ is set, the timeout has already locked in the no-effect
    // result; no further click should be accepted (otherwise a click landing just after
    // timeout, while the timeout's own audio is still playing, would wrongly override it).
    if (!clicked_ && !timed_out_ && any_click) {
        const auto& ms = input.mouse();
        for (int n = 0; n < 10; ++n) {
            const auto& z = zone_rects_[n];
            if (PointInRect(ms.x, ms.y, z)) {
                selected_ = n + 1;  // 1-10
                clicked_ = true;
                break;
            }
        }
    }

    // A real click cuts the entry audio if it's still playing.
    if (clicked_ && entry_wav_ != 0 && audio_.isPlaying(entry_wav_)) {
        audio_.stop(entry_wav_);
    }

    // 4s timeout with no click -> no reward/penalty, but it DOES play audio.
    if (!clicked_ && !timed_out_ && elapsed_ms >= 4000) {
        timed_out_ = true;
        static const char* kDiceKeysTimeoutWav[2] = {"W1\\AT_0063.WAV", "W5\\AT_0537.WAV"};
        timeout_wav_ =
            playWav(kDiceKeysTimeoutWav[RollIndex(board_.RngNext(), 2)], AudioFlags::Async);
    }
    if (timed_out_ && (timeout_wav_ == 0 || !audio_.isPlaying(timeout_wav_))) {
        board_.current_state_ = BoardGameState::TurnEnd;
        board_.state_start_time_ = Clock::now();
        board_.state_action_executed_ = false;
        return;
    }

    if (clicked_) {
        // The real comparison uses the RAW 0-indexed zone_index against the 1-indexed
        // target with NO adjustment — the displayed button "10" sends zone_index=9
        // internally, so it's compared as if it were "9", not "10". selected_ is
        // 1-indexed for display, so convert back to the raw index to match.
        const int diff = std::abs((selected_ - 1) - target_);
        if (diff <= tolerance_) {
            result_ = 0;
            if (reward_id_ == 3) {
                const int prov = board_.FindUnheldProvince(player_);
                if (prov >= 0) board_.GainKey(player_, prov);
                key_province_ = prov;  // remember for the result bitmap
                static const char* kKeyWin[2] = {"W6\\AT_0591.WAV", "W6\\AT_0592.WAV"};
                result_wav_ = playWav(kKeyWin[RollIndex(board_.RngNext(), 2)], AudioFlags::Async);
            } else {
                static const int kPowerAmt[3] = {10, 20, 30};
                board_.AddPower(player_, static_cast<uint16_t>(kPowerAmt[reward_id_]));
                static const char* kPowerWin[2] = {"W2\\AT_0184.WAV", "W1\\AT_0100.WAV"};
                result_wav_ = playWav(kPowerWin[RollIndex(board_.RngNext(), 2)], AudioFlags::Async);
            }
        } else {
            result_ = 1;
            static const char* kWrong[2] = {"W1\\AT_0101.WAV", "W2\\AT_0224.WAV"};
            result_wav_ = playWav(kWrong[RollIndex(board_.RngNext(), 2)], AudioFlags::Async);
        }
        phase_ = Phase::ShowingResult;
        board_.state_start_time_ = Clock::now();
        board_.state_action_executed_ = false;
    }
}

void DiceKeysGame::render() {
    // G1/DOMIN08.BMP (full 640x480) is the guess-phase background; DOMIN09-17 are
    // the 9 win-outcome reveal backgrounds (by reward/province), not a die-roll
    // animation. On loss the background stays DOMIN08 — only a "wrong guess" audio
    // cue plays, no visual change. Real click zones are invisible hit-boxes over
    // DOMIN08's own baked-in number art.
    TextureHandle dicekeys_result_bg = 0;
    if (phase_ == Phase::ShowingResult && result_ == 0) {
        static const int kDiceKeysPowerResultIdx[3] = {0, 2, 1};  // reward 0/1/2 -> DOMIN09/11/10
        static const int kDiceKeysKeyResultIdx[6] = {
            4, 5, 6, 3, 7, 8};  // province 0-5 -> DOMIN13/14/15/12/16/17
        int result_index = -1;
        if (reward_id_ == 3) {
            if (key_province_ >= 0 && key_province_ < 6) {
                result_index = kDiceKeysKeyResultIdx[key_province_];
            }
        } else if (reward_id_ >= 0 && reward_id_ < 3) {
            result_index = kDiceKeysPowerResultIdx[reward_id_];
        }
        if (result_index >= 0) {
            if (result_tex_ == 0) {
                char filename[9];
                std::snprintf(filename, sizeof(filename), "DOMIN%02d", result_index + 9);
                result_tex_ = assets_.texture("G1", filename);
            }
            dicekeys_result_bg = result_tex_;
        }
    }
    if (dicekeys_result_bg) {
        renderer_.drawBitmap(dicekeys_result_bg, 0, 0, 0, 0, 640, 480);
    } else if (bg_tex_) {
        renderer_.drawBitmap(bg_tex_, 0, 0, 0, 0, 640, 480);
    } else {
        renderer_.fillRect(0, 0, 640, 480, 10, 8, 20);  // dark background
    }

    // Gamepad focus indicator: real cursor snapped to the focused zone's center.
    if (board_.gamepad_mode_ && phase_ == Phase::Guessing) {
        const auto& z = zone_rects_[focus_];
        DrawFocusCursor(renderer_, (z.x1 + z.x2) / 2, (z.y1 + z.y2) / 2);
    }
}

}  // namespace openfear::game
