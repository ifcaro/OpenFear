#include "openfear/game/subscreens/FortuneDoorGame.h"
#include "openfear/game/BoardGameScreen.h"
#include <chrono>

namespace openfear::game {

// RNG-picked variant pools — only ONE of each ever plays per Enter(), so only
// that one is loaded (playWav(), same pattern as FearSelectScreen's
// kOpeningWavs) instead of preloading all 13 candidates up front.
static const char* kFortuneDoorVarWavs[5] = {
    "W2\\AT_0140.WAV", "W2\\AT_0141.WAV", "W6\\AT_0608.WAV", "W6\\AT_0609.WAV", "W6\\AT_0610.WAV"};
static const char* kFortuneDoorTauntWavs[8] = {
    "W3\\AT_0437.WAV", "W3\\AT_0438.WAV", "W3\\AT_0429.WAV", "W4\\AT_0502.WAV",
    "W4\\AT_0503.WAV", "W4\\AT_0508.WAV", "W4\\AT_0509.WAV", "W4\\AT_0512.WAV"};

bool FortuneDoorGame::LoadZoneRects() {
    return LoadRectZoneOrFail("gateopen", 2, zone_rects_.data(),
                              "GATEOPEN.BIN not loaded/wrong size — OK/X zones would never "
                              "respond to clicks");
}

void FortuneDoorGame::Enter(int player) {
    // Shows the closed-door bitmap immediately, preps the open-door one, and plays the
    // entry gatekeeper-voice variant. The 4s auto-resolve timer only starts once that
    // entry audio finishes — never at setup.
    player_ = player;
    clicked_ = false;
    ok_click_ = false;
    awaiting_creak_ = false;
    taunt_wav_ = 0;
    entry_audio_done_time_ = std::chrono::steady_clock::time_point::min();

    // The 50/50 FortuneCard-vs-FateCard roll isn't precomputed here — it happens
    // once the door creak finishes, in update()'s awaiting_creak_ block.
    if (tex_ == 0) {
        tex_ = assets_.texture("G1", "GATEOPN2");
        bg_tex_ = assets_.texture("G1", "GATEOPEN");
        bg_sprite_ = renderer_.createSprite(bg_tex_);
        focus_ok_ = true;
        result_wav_ = preloadWav("W10\\DR_OPEN.WAV");
    }
    // DR_OPEN.WAV loads into slot 1 here (load only); the gatekeeper-voice variant
    // loads into slot 0 and plays immediately — 2 real simultaneous channels. Slot 1
    // stays loaded-but-silent until OK/timeout explicitly plays it; X never plays it.
    {
        const int var_idx = RollIndex(board_.RngNext(), 5);
        result_var_wav_ = playWav(kFortuneDoorVarWavs[var_idx], AudioFlags::Async);
    }
    board_.video_.close();
    board_.video_loaded_ = false;
    board_.current_state_ = BoardGameState::FortuneDoorGame;
    board_.state_start_time_ = std::chrono::steady_clock::now();
    board_.state_action_executed_ = false;
}

void FortuneDoorGame::update(const IInput& input, float dt) {
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();

    // The 4s auto-timer only starts once the entry variant stops playing —
    // isPlaying(result_wav_) is trivially false here, since it's still just
    // loaded-silent (never played) at this point.
    if (entry_audio_done_time_ == Clock::time_point::min()) {
        bool entry_audio_playing =
            audio_.isPlaying(result_wav_) || audio_.isPlaying(result_var_wav_);
        if (!entry_audio_playing) {
            entry_audio_done_time_ = now;
        }
    }
    const bool entry_audio_done = entry_audio_done_time_ != Clock::time_point::min();
    const long elapsed_since_audio_done_ms =
        entry_audio_done
            ? std::chrono::duration_cast<std::chrono::milliseconds>(now - entry_audio_done_time_)
                  .count()
            : 0;

    // 4s auto-timer -> OK path.
    if (!clicked_ && entry_audio_done && elapsed_since_audio_done_ms >= 4000) {
        clicked_ = true;
        ok_click_ = true;
    }

    // Human input — real zones loaded from GATEOPEN.BIN: zone_rects_[1] is the OK
    // checkmark, zone_rects_[0] is the X (cancel).
    if (!clicked_) {
        const auto& mouse = input.mouse();
        const bool left_click = mouse.clicked[static_cast<int>(MouseButton::Left)];
        const bool right_click = mouse.clicked[static_cast<int>(MouseButton::Right)];
        // Gamepad: D-pad/stick left-right toggles focus between the 2 real zones, A
        // confirms whichever is focused.
        bool gamepad_ok = false, gamepad_x = false;
        if (board_.gamepad_mode_) {
            const auto& gamepad = input.gamepad();
            int dx = 0;
            if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadLeft)]) dx = -1;
            if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadRight)]) dx = 1;
            if (dx == 0)
                board_.stick_repeater_.PollHorizontal(gamepad.left_x, gamepad.left_y, dt, dx);
            if (dx != 0) focus_ok_ = (dx < 0);
            if (gamepad.pressed[static_cast<size_t>(GamepadButton::A)]) {
                gamepad_ok = focus_ok_;
                gamepad_x = !focus_ok_;
            }
        }
        if (left_click || right_click || gamepad_ok || gamepad_x) {
            const bool in_ok = gamepad_ok || PointInRect(mouse.x, mouse.y, zone_rects_[1]);
            const bool in_x = gamepad_x || PointInRect(mouse.x, mouse.y, zone_rects_[0]);
            if (in_ok || in_x) {
                clicked_ = true;
                ok_click_ = in_ok;
            }
        }
    }

    if (clicked_ && !board_.state_action_executed_) {
        board_.state_action_executed_ = true;
        // Resolving (click or 4s timeout) stops the entry/variant audio first,
        // before either branch below does anything else.
        if (result_wav_) audio_.stop(result_wav_);
        if (result_var_wav_) audio_.stop(result_var_wav_);
        if (ok_click_) {
            // render()'s clicked_&&ok_click_ check already swaps in the OPEN bitmap (X
            // never satisfies it, stays closed) — this just starts the door creak.
            if (result_wav_) audio_.playLoaded(result_wav_, AudioFlags::Async);
            awaiting_creak_ = true;
        } else {
            // X path: play taunt audio. Teardown only once THIS audio finishes,
            // not a flat 2s wait.
            const int taunt_idx = RollIndex(board_.RngNext(), 8);
            taunt_wav_ = playWav(kFortuneDoorTauntWavs[taunt_idx], AudioFlags::Async);
        }
    }

    // OK path: door stays visibly open while the creak plays — only once it finishes
    // does the real 50/50 roll + card dispatch happen. current_state_ stays
    // FortuneDoorGame throughout this wait.
    if (awaiting_creak_) {
        const bool creak_done = (result_wav_ == 0) || !audio_.isPlaying(result_wav_);
        if (creak_done) {
            awaiting_creak_ = false;
            const bool use_card = RollIndex(board_.RngNext(), 2) == 0;
            if (use_card) {
                board_.ResolveFortuneCard(player_);
            } else {
                board_.ResolveFateCard(player_);
            }
            board_.current_state_ = BoardGameState::TurnEnd;
            board_.state_start_time_ = Clock::now();
            board_.state_action_executed_ = false;
        }
    }

    // X path only reaches here — wait for the taunt audio to finish, then TurnEnd.
    if (clicked_ && !ok_click_ && board_.state_action_executed_ &&
        board_.current_state_ == BoardGameState::FortuneDoorGame) {
        const bool taunt_done = (taunt_wav_ == 0) || !audio_.isPlaying(taunt_wav_);
        if (taunt_done) {
            board_.FreeLazyMinigameAssets();
            board_.current_state_ = BoardGameState::TurnEnd;
            board_.state_start_time_ = Clock::now();
            board_.state_action_executed_ = false;
        }
    }
}

void FortuneDoorGame::render() {
    const TextureHandle door_tex = (clicked_ && ok_click_) ? tex_ : bg_tex_;
    if (door_tex) {
        renderer_.drawBitmap(door_tex, 0, 0, 0, 0, 640, 480);
    } else {
        renderer_.fillRect(0, 0, 640, 480, 40, 20, 5);
    }
    // Gamepad focus indicator. Real click zones (zone_rects_, loaded from GATEOPEN.BIN
    // for hit-testing) are padded well past the painted gold plaque for click
    // forgiveness — hand-measured the actual plaque bounds for the visual tint only.
    if (board_.gamepad_mode_ && !clicked_ && bg_tex_) {
        static constexpr int kOkPlaqueX1 = 108, kOkPlaqueY1 = 262, kOkPlaqueX2 = 191,
                             kOkPlaqueY2 = 326;
        static constexpr int kXPlaqueX1 = 455, kXPlaqueY1 = 262, kXPlaqueX2 = 538, kXPlaqueY2 = 326;
        const int fx1 = focus_ok_ ? kOkPlaqueX1 : kXPlaqueX1;
        const int fy1 = focus_ok_ ? kOkPlaqueY1 : kXPlaqueY1;
        const int fx2 = focus_ok_ ? kOkPlaqueX2 : kXPlaqueX2;
        const int fy2 = focus_ok_ ? kOkPlaqueY2 : kXPlaqueY2;
        DrawFocusTint(renderer_, bg_sprite_, fx1, fy1, fx1, fy1, fx2 - fx1, fy2 - fy1);
        DrawFocusCursor(renderer_, (fx1 + fx2) / 2, (fy1 + fy2) / 2);
    }
}

void FortuneDoorGame::FreeAssets() {
    if (tex_ != 0) {
        renderer_.destroyTexture(tex_);
        tex_ = 0;
        renderer_.destroyTexture(bg_tex_);
        bg_tex_ = 0;
        audio_.unload(result_wav_);
        result_wav_ = 0;
        // result_var_wav_/taunt_wav_ are playWav() transients, auto-reaped once
        // finished — unload() here is just a defensive no-op if one's still
        // mid-playback when the turn ends.
        audio_.unload(result_var_wav_);
        result_var_wav_ = 0;
        audio_.unload(taunt_wav_);
        taunt_wav_ = 0;
    }
}

}  // namespace openfear::game
