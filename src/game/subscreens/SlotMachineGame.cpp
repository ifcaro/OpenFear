#include "openfear/game/subscreens/SlotMachineGame.h"
#include "openfear/game/BoardGameScreen.h"
#include <chrono>

namespace openfear::game {

static const char* kSlotEntryWavs[4] = {"W9\\AT_0745.WAV", "W9\\AT_0764.WAV", "W9\\AT_0765.WAV",
                                        "W9\\AT_0766.WAV"};

void SlotMachineGame::Enter(int player) {
    // Spin/Cancel zone rects documented in update(); either mouse button works in both.
    if (bg_tex_ == 0) {
        bg_tex_ = assets_.texture("G2", "POKMACH");
        bg_sprite_ = renderer_.createSprite(bg_tex_);
        reel_tex_ = assets_.texture("G2", "POKBLIT");
    }
    player_ = player;
    spun_ = false;
    done_ = false;
    clicked_ = false;
    spinning_ = false;
    spin_tick_ = 0;
    awaiting_exit_ = false;
    // Rolled here (not in the state's own init block) so render() never draws one frame
    // of the zero-initialized default (symbol 0 on all 3 reels) before the real
    // per-reel roll lands — current_state_ changes mid-update(), a frame before the
    // state's own init block would otherwise run.
    reels_[0] = RollIndex(board_.RngNext(), 2);
    reels_[1] = RollIndex(board_.RngNext(), 2) + 2;
    reels_[2] = RollIndex(board_.RngNext(), 2) + 4;
    {
        const int entry_idx = RollIndex(board_.RngNext(), 4);
        entry_wav_ = playWav(kSlotEntryWavs[entry_idx], AudioFlags::Async);
    }
    board_.video_.close();
    board_.video_loaded_ = false;
    board_.current_state_ = BoardGameState::SlotMachineGame;
    board_.state_start_time_ = std::chrono::steady_clock::now();
    board_.state_action_executed_ = false;
}

void SlotMachineGame::update(const IInput& input, float dt) {
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();
    const auto& mouse = input.mouse();
    const bool left_click = mouse.clicked[static_cast<int>(MouseButton::Left)];
    const bool right_click = mouse.clicked[static_cast<int>(MouseButton::Right)];

    if (!board_.state_action_executed_) {
        board_.state_action_executed_ = true;
        focus_spin_ = true;
    }

    // Spin roll animation mechanics documented on spinning_ in the header.
    if (spinning_) {
        if (spin_tick_wav_ == 0 || !audio_.isPlaying(spin_tick_wav_)) {
            spin_tick_wav_ = playWav("W11\\SPIN.WAV", AudioFlags::Async);
        }
        const auto tick_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - anim_tick_time_).count();
        if (tick_ms >= 45) {
            anim_tick_time_ = now;
            if (spin_tick_ < 41) {
                for (int r = 0; r < 3; ++r) {
                    reels_[r] = (reels_[r] == 5) ? 0 : reels_[r] + 1;
                }
            } else {
                reels_[0] = target_reels_[0];
                reels_[1] = target_reels_[1];
                reels_[2] = target_reels_[2];
            }
            ++spin_tick_;
            if (spin_tick_ >= 50) {
                spinning_ = false;
                spun_ = true;
            }
        }
    }

    if (!spun_ && !done_ && !spinning_ && !awaiting_exit_) {
        // Gamepad: D-pad/stick left-right toggles focus between the real
        // spin/cancel zones, A confirms whichever's focused.
        bool gp_spin = false, gp_cancel = false;
        if (board_.gamepad_mode_) {
            const auto& gp = input.gamepad();
            int dx = 0;
            if (gp.pressed[static_cast<size_t>(GamepadButton::DpadLeft)]) dx = -1;
            if (gp.pressed[static_cast<size_t>(GamepadButton::DpadRight)]) dx = 1;
            if (dx == 0) board_.stick_repeater_.PollHorizontal(gp.left_x, gp.left_y, dt, dx);
            if (dx != 0) focus_spin_ = (dx < 0);
            if (gp.pressed[static_cast<size_t>(GamepadButton::A)]) {
                if (focus_spin_)
                    gp_spin = true;
                else
                    gp_cancel = true;
            }
        }
        const bool any_click = (left_click || right_click || gp_spin || gp_cancel) && !clicked_;
        if (any_click) clicked_ = true;
        if (!left_click && !right_click && !gp_spin && !gp_cancel) clicked_ = false;

        bool do_spin = false;
        // Real zone rects: LEFT (x=15-95) arms the spin timer — the real SPIN
        // action. RIGHT (x=540-622) stops audio with no spin timer — the real
        // CANCEL/decline action.
        static constexpr int kSlotSpinX1 = 15, kSlotSpinY1 = 206, kSlotSpinX2 = 95,
                             kSlotSpinY2 = 269;
        static constexpr int kSlotCancelX1 = 540, kSlotCancelY1 = 206, kSlotCancelX2 = 622,
                             kSlotCancelY2 = 269;
        if (any_click) {
            const bool in_cancel =
                gp_cancel || (mouse.x >= kSlotCancelX1 && mouse.x <= kSlotCancelX2 &&
                              mouse.y >= kSlotCancelY1 && mouse.y <= kSlotCancelY2);
            const bool in_spin = gp_spin || (mouse.x >= kSlotSpinX1 && mouse.x <= kSlotSpinX2 &&
                                             mouse.y >= kSlotSpinY1 && mouse.y <= kSlotSpinY2);
            if (in_cancel) {
                // Cancel — wait for audio (real 7-clip pool) before tearing down.
                if (entry_wav_) audio_.stop(entry_wav_);
                static const char* kSlotCancel[7] = {
                    "W3\\AT_0437.WAV", "W3\\AT_0438.WAV", "W3\\AT_0429.WAV", "W4\\AT_0502.WAV",
                    "W4\\AT_0503.WAV", "W4\\AT_0508.WAV", "W4\\AT_0509.WAV",
                };
                exit_wav_ = playWav(kSlotCancel[RollIndex(board_.RngNext(), 7)], AudioFlags::Async);
                awaiting_exit_ = true;
                return;
            } else if (in_spin) {
                do_spin = true;
            }
        }
        if (do_spin) {
            // Stop any still-playing audio (same as the Cancel path) before
            // arming the spin timer.
            if (entry_wav_) audio_.stop(entry_wav_);
            // Outcome is pre-determined by a power-tiered formula, not 3 independent
            // rolls compared after — lower power means a more likely guaranteed win
            // (and better jackpot odds). The animation just reveals it over ~2.25s.
            const int char_id = board_.render_state_[player_].char_id;
            const uint16_t power = board_.render_state_[player_].power;
            const float tier_mult = (power > 40)    ? 0.3f
                                    : (power >= 24) ? 0.9f
                                    : (power >= 16) ? 1.5f
                                                    : 2.0f;
            const int roll = RollIndex(board_.RngNext(), 100);
            const int scaled = static_cast<int>(roll * tier_mult);
            bool all_match;
            if (scaled >= 106) {
                target_reels_[0] = target_reels_[1] = target_reels_[2] = char_id;
                all_match = true;
            } else if (scaled >= 61) {
                const int sym = RollIndex(board_.RngNext(), 6);
                target_reels_[0] = target_reels_[1] = target_reels_[2] = sym;
                all_match = true;
            } else {
                target_reels_[0] = RollIndex(board_.RngNext(), 6);
                target_reels_[1] = RollIndex(board_.RngNext(), 6);
                target_reels_[2] = RollIndex(board_.RngNext(), 6);
                all_match = (target_reels_[0] == target_reels_[1]) &&
                            (target_reels_[1] == target_reels_[2]);
            }
            if (all_match && target_reels_[0] == char_id) {
                result_ = 1;  // big win: +50 power
            } else if (all_match) {
                result_ = 0;  // small win: +20 power
            } else {
                result_ = 2;  // loss: -5 power
            }
            spinning_ = true;
            spin_tick_ = 0;
            anim_tick_time_ = now;
        }
    } else if (spun_ && !done_) {
        // The outcome resolves and the result WAV starts the instant the tick
        // counter hits 50, same timer tick that ends the spin, no extra wait.
        // The reels are already frozen since tick 41 (~405ms earlier).
        {
            done_ = true;
            // own-face jackpot: AddPower(50), a single fixed clip, no RNG call —
            // not a random pool.
            // 3-match non-jackpot: AddPower(20), a 12-entry pool.
            // no match: SubtractPower(5), a 23-entry pool.
            static const char* kSlotBigWin = "W9\\AT_0749.WAV";
            static const char* kSlotSmallWin[12] = {
                "W4\\AT_0480.WAV",  "W3\\AT_0472B.WAV", "W3\\AT_0472A.WAV", "W2\\AT_0325B.WAV",
                "W2\\AT_0325A.WAV", "W4\\AT_0514.WAV",  "W2\\AT_0323A.WAV", "W4\\AT_0511.WAV",
                "W4\\AT_0477.WAV",  "W2\\AT_0322A.WAV", "W2\\AT_0319.WAV",  "W4\\AT_0505.WAV",
            };
            static const char* kSlotLoss[23] = {
                "W3\\AT_0457.WAV",  "W3\\AT_0420.WAV",  "W4\\AT_0507.WAV",  "W4\\AT_0485.WAV",
                "W4\\AT_0484.WAV",  "W3\\AT_0459.WAV",  "W2\\AT_0333.WAV",  "W3\\AT_0436.WAV",
                "W4\\AT_0510.WAV",  "W4\\AT_0509.WAV",  "W4\\AT_0506.WAV",  "W4\\AT_0503.WAV",
                "W4\\AT_0502.WAV",  "W4\\AT_0481.WAV",  "W4\\AT_0483.WAV",  "W4\\AT_0482.WAV",
                "W3\\AT_0473B.WAV", "W3\\AT_0473A.WAV", "W3\\AT_0472B.WAV", "W3\\AT_0472A.WAV",
                "W3\\AT_0471E.WAV", "W3\\AT_0471D.WAV", "W3\\AT_0422.WAV",
            };
            if (entry_wav_) audio_.stop(entry_wav_);
            switch (result_) {
                case 0:
                    board_.AddPower(player_, 20);
                    exit_wav_ =
                        playWav(kSlotSmallWin[RollIndex(board_.RngNext(), 12)], AudioFlags::Async);
                    break;
                case 1:
                    board_.AddPower(player_, 50);
                    exit_wav_ = playWav(kSlotBigWin, AudioFlags::Async);
                    break;
                case 2:
                    board_.SubtractPower(player_, 5);
                    exit_wav_ =
                        playWav(kSlotLoss[RollIndex(board_.RngNext(), 23)], AudioFlags::Async);
                    break;
            }
            awaiting_exit_ = true;
        }
    } else if (awaiting_exit_) {
        // Any click skips the result audio, but only if done_ is true.
        const bool click_skip = done_ && (left_click || right_click);
        if (click_skip && exit_wav_ != 0) {
            audio_.stop(exit_wav_);
        }
        if (exit_wav_ == 0 || !audio_.isPlaying(exit_wav_)) {
            awaiting_exit_ = false;
            board_.FreeLazyMinigameAssets();
            board_.current_state_ = BoardGameState::TurnEnd;
            board_.state_start_time_ = now;
            board_.state_action_executed_ = false;
        }
    }
}

void SlotMachineGame::render() {
    if (bg_tex_) {
        renderer_.drawBitmap(bg_tex_, 0, 0, 0, 0, 640, 480);
    } else {
        renderer_.fillRect(0, 0, 640, 480, 5, 5, 15);
    }
    // reels_ is always a valid 0-5 symbol (idle roll from screen-open, mid-spin
    // animation, or final result), so there's no "not spun yet" placeholder
    // case — always draw the real texture crop.
    static const int kReelX[3] = {173, 272, 372};
    static const int kReelY = 228, kReelW = 88, kReelH = 118;
    for (int r = 0; r < 3; ++r) {
        const int symbol = reels_[r];
        if (reel_tex_) {
            renderer_.drawBitmap(reel_tex_, kReelX[r], kReelY, symbol * 89 + 1, 1, kReelW, kReelH);
        }
    }
    // Result label sprite: a small sprite from the same POKBLIT atlas drawn over the
    // "20"/"50"/"-5" arch already baked into POKMACH.BMP.
    if (spun_ && reel_tex_) {
        struct {
            int sx, sy, sw, sh, dx, dy;
        } r;
        switch (result_) {
            case 0:
                r = {6, 120, 80, 56, 189, 125};
                break;  // "20"
            case 1:
                r = {3, 178, 82, 55, 279, 85};
                break;  // "50"
            default:
                r = {99, 123, 67, 51, 366, 126};
                break;  // "-5"
        }
        renderer_.drawBitmap(reel_tex_, r.dx, r.dy, r.sx, r.sy, r.sw, r.sh);
    }
    // Gamepad focus indicator. Zones are the real spin(left)/cancel(right) action rects.
    if (board_.gamepad_mode_ && !spun_ && !done_ && !spinning_ && !awaiting_exit_) {
        static constexpr int kSlotSpinX1 = 15, kSlotSpinY1 = 206, kSlotSpinX2 = 95,
                             kSlotSpinY2 = 269;
        static constexpr int kSlotCancelX1 = 540, kSlotCancelY1 = 206, kSlotCancelX2 = 622,
                             kSlotCancelY2 = 269;
        const int fx1 = focus_spin_ ? kSlotSpinX1 : kSlotCancelX1;
        const int fy1 = focus_spin_ ? kSlotSpinY1 : kSlotCancelY1;
        const int fx2 = focus_spin_ ? kSlotSpinX2 : kSlotCancelX2;
        const int fy2 = focus_spin_ ? kSlotSpinY2 : kSlotCancelY2;
        DrawFocusTint(renderer_, bg_sprite_, fx1, fy1, fx1, fy1, fx2 - fx1, fy2 - fy1);
        DrawFocusCursor(renderer_, (fx1 + fx2) / 2, (fy1 + fy2) / 2);
    }
}

void SlotMachineGame::FreeAssets() {
    if (bg_tex_ != 0) {
        renderer_.destroyTexture(bg_tex_);
        bg_tex_ = 0;
        renderer_.destroyTexture(reel_tex_);
        reel_tex_ = 0;
        audio_.unload(entry_wav_);
        entry_wav_ = 0;
    }
}

}  // namespace openfear::game
