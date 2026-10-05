#include "openfear/game/subscreens/CureCurseGame.h"
#include "openfear/game/BoardGameScreen.h"
#include <chrono>
#include <cstdio>

namespace openfear::game {

// Result-taunt pool of 8 gatekeeper voice clips. Only the RNG-picked one is
// loaded, via playWav() rather than preloaded up front.
static const char* kCureCurseTauntWavs[8] = {
    "W3\\AT_0429.WAV", "W3\\AT_0437.WAV", "W3\\AT_0438.WAV", "W4\\AT_0502.WAV",
    "W4\\AT_0503.WAV", "W4\\AT_0508.WAV", "W4\\AT_0509.WAV", "W4\\AT_0512.WAV"};

bool CureCurseGame::LoadZoneRects() {
    auto rects = assets_.rect_file("curecurs");
    if (!rects || rects->size() < 2) {
        std::fprintf(stderr, "[CureCurse] ERROR: Failed to load CURECURS.BIN or insufficient "
                             "rects\n");
        return false;
    }
    accept_rect_ = (*rects)[0];
    decline_rect_ = (*rects)[1];
    return true;
}

void CureCurseGame::Enter(int player, uint16_t curse_type) {
    // Human path: picks the fixed offer line for the player's curse_type and opens
    // GKLOOP. update() then drives phase 0 (offer audio), phase 1 (4000ms 2-zone
    // accept/decline window over CURSICON.BMP), and phase 2 (result taunt over
    // GKLOOP again) before TurnEnd.
    if (board_.render_state_[player].is_ai) {
        // AI's own cure path is an instant, free, silent cure: no UI, no power cost.
        board_.render_state_[player].curse_type = 0;
        board_.render_state_[player].curse_step_counter = 0;
        board_.render_state_[player].lost_key_to_vampire = false;
        board_.current_state_ = BoardGameState::TurnEnd;
        board_.state_start_time_ = std::chrono::steady_clock::now();
        board_.state_action_executed_ = false;
        return;
    }
    // bg_tex_ (the 2-zone accept/decline UI) loads once on first use and
    // stays loaded until FreeAssets() tears it down at TurnEnd.
    if (bg_tex_ == 0) {
        bg_tex_ = assets_.texture("M1", "CURSICON");
        bg_sprite_ = renderer_.createSprite(bg_tex_);
    }
    static const char* kCureCurseOfferWavsLazy[6] = {"W8\\AT_0690.WAV", "W8\\AT_0687.WAV",
                                                     "W8\\AT_0689.WAV", "W8\\AT_0688.WAV",
                                                     "W8\\AT_0685.WAV", "W8\\AT_0686.WAV"};
    if (offer_wav_) audio_.unload(offer_wav_);
    offer_wav_ = preloadWav(kCureCurseOfferWavsLazy[curse_type - 1]);
    focus_accept_ = true;
    focus_change_time_ = std::chrono::steady_clock::now();
    player_ = player;
    phase_ = 0;
    active_audio_ = offer_wav_;
    if (active_audio_) audio_.playLoaded(active_audio_, AudioFlags::Async);
    const int gkloop_variant = RollIndex(board_.RngNext(), 3) + 1;
    board_.gkloop_avi_path_ =
        AssetPaths::avi("AVI", std::string("GKLOOP") + std::to_string(gkloop_variant)).string();
    board_.video_.close();
    board_.video_loaded_ = false;
    board_.video_.setRenderRect(BoardGameScreen::kBoardAreaX, BoardGameScreen::kBoardAreaY,
                                BoardGameScreen::kBoardAreaW, BoardGameScreen::kBoardAreaH);
    if (board_.video_.open(board_.gkloop_avi_path_)) board_.video_.play(true);
    board_.current_state_ = BoardGameState::CureCurseGame;
    board_.state_start_time_ = std::chrono::steady_clock::now();
    board_.state_action_executed_ = false;
}

void CureCurseGame::update(const IInput& input, float dt) {
    using Clock = std::chrono::steady_clock;

    if (phase_ == 0 || phase_ == 2) {
        // Loop the GKLOOP video until this phase's audio finishes - play()
        // alone rewinds, no need to close/reopen the same stream.
        if (board_.video_.status() != VideoStatus::Playing) {
            board_.video_.play(true);
        }
        if (active_audio_ == 0 || !audio_.isPlaying(active_audio_)) {
            if (phase_ == 0) {
                // Pause (not close) freezes the eyes on their last frame under the
                // accept/decline buttons, then starts phase 1's 4000ms window.
                board_.video_.pause();
                phase_ = 1;
                board_.state_start_time_ = Clock::now();
                board_.state_action_executed_ = false;
            } else {
                board_.video_.close();
                board_.video_loaded_ = false;
                board_.FreeLazyMinigameAssets();
                board_.current_state_ = BoardGameState::TurnEnd;
                board_.state_start_time_ = Clock::now();
                board_.state_action_executed_ = false;
            }
        }
        return;
    }

    // Phase 1: interactive 4000ms window, 2 rects (8 bytes each). The GKLOOP
    // eyes stay frozen on their last decoded frame for the whole phase.
    const long elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                Clock::now() - board_.state_start_time_)
                                .count();
    const auto& mouse = input.mouse();
    const bool click = mouse.clicked[static_cast<int>(MouseButton::Left)] ||
                       mouse.clicked[static_cast<int>(MouseButton::Right)];
    // Gamepad: D-pad/stick left-right toggles focus between accept/decline, A confirms.
    bool gamepad_accept = false, gamepad_decline = false;
    if (board_.gamepad_mode_) {
        const auto& gamepad = input.gamepad();
        int dx = 0;
        if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadLeft)]) dx = -1;
        if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadRight)]) dx = 1;
        if (dx == 0) board_.stick_repeater_.PollHorizontal(gamepad.left_x, gamepad.left_y, dt, dx);
        if (dx != 0) {
            const bool focus_accept_before = focus_accept_;
            focus_accept_ = (dx < 0);
            if (focus_accept_ != focus_accept_before) {
                focus_change_time_ = std::chrono::steady_clock::now();
            }
        }
        if (gamepad.pressed[static_cast<size_t>(GamepadButton::A)]) {
            if (focus_accept_)
                gamepad_accept = true;
            else
                gamepad_decline = true;
        }
    }
    bool accepted = false;
    bool resolved = false;
    if (click) {
        const bool in_accept = PointInRect(mouse.x, mouse.y, accept_rect_);
        const bool in_decline = PointInRect(mouse.x, mouse.y, decline_rect_);
        if (in_accept || in_decline) {
            resolved = true;
            accepted = in_accept;
        }
    } else if (gamepad_accept || gamepad_decline) {
        resolved = true;
        accepted = gamepad_accept;
    } else if (elapsed_ms >= 4000) {
        resolved = true;  // timeout -> decline
    }

    if (resolved) {
        // Decline/timeout: no effect — curse persists, offer recurs on the same
        // curse_step_counter/threshold cadence. Accept's lost_key_to_vampire clear
        // undoes the Bathory reoffer-cadence boost from GainKey's key redirect.
        if (accepted) {
            board_.render_state_[player_].curse_type = 0;
            board_.render_state_[player_].curse_step_counter = 0;
            board_.render_state_[player_].lost_key_to_vampire = false;
            board_.SubtractPower(player_, board_.render_state_[player_].power / 4);
        }
        phase_ = 2;
        const int taunt_idx = RollIndex(board_.RngNext(), 8);
        active_audio_ = playWav(kCureCurseTauntWavs[taunt_idx], AudioFlags::Async);
        const int gkloop_variant = RollIndex(board_.RngNext(), 3) + 1;
        board_.gkloop_avi_path_ =
            AssetPaths::avi("AVI", std::string("GKLOOP") + std::to_string(gkloop_variant)).string();
        board_.video_.close();
        board_.video_loaded_ = false;
        board_.video_.setRenderRect(BoardGameScreen::kBoardAreaX, BoardGameScreen::kBoardAreaY,
                                    BoardGameScreen::kBoardAreaW, BoardGameScreen::kBoardAreaH);
        if (board_.video_.open(board_.gkloop_avi_path_)) board_.video_.play(true);
        board_.state_start_time_ = Clock::now();
    }
}

void CureCurseGame::render() {
    // Phases 0/2 (offer/result audio): just the GKLOOP video.
    // Phase 1 (interactive window): M1/CURSICON.BMP background over the
    // paused video's last frame — the one real asset for this UI.
    if (phase_ == 1) {
        // CURSICON.BMP is a real 490x86 strip, drawn at its native size
        // spanning accept's left edge to decline's right edge, not
        // stretched. The last GKLOOP frame (phase 0) deliberately stays
        // visible behind it — keep rendering that instead of clearing to
        // black/color.
        board_.video_.renderFrame();
        if (bg_tex_) {
            renderer_.drawBitmap(bg_tex_, 75, 380, 0, 0, 490, 86);
        }
        // Both buttons are baked into the same 490x86 strip — accept crops
        // its left portion, decline its right, both from bg_sprite_.
        if (board_.gamepad_mode_ && bg_tex_) {
            const auto anchor_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       focus_change_time_.time_since_epoch())
                                       .count();
            if (focus_accept_) {
                DrawFocusTint(renderer_, bg_sprite_, 75, 380, 0, 0, 109, 85, anchor_ms);
                DrawFocusCursor(renderer_, 75 + 109 / 2, 380 + 85 / 2);
            } else {
                DrawFocusTint(renderer_, bg_sprite_, 456, 380, 381, 0, 109, 85, anchor_ms);
                DrawFocusCursor(renderer_, 456 + 109 / 2, 380 + 85 / 2);
            }
        }
    } else {
        board_.video_.renderFrame();
    }
}

void CureCurseGame::FreeAssets() {
    if (bg_tex_ != 0) {
        audio_.unload(offer_wav_);
        offer_wav_ = 0;
        // active_audio_ may be the RNG-picked taunt (a playWav() transient, auto-reaped -
        // unload() here is just a defensive no-op if still mid-playback) or offer_wav_
        // above (already unloaded, second unload is a no-op).
        audio_.unload(active_audio_);
        active_audio_ = 0;
        ReleaseTexture(bg_tex_);
    }
}

}  // namespace openfear::game
