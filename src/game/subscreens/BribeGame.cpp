#include "openfear/game/subscreens/BribeGame.h"
#include "openfear/game/BoardGameScreen.h"
#include <chrono>

namespace openfear::game {

static const char* kBribeOfferWavs[4] = {"W9\\AT_0760.WAV", "W9\\AT_0761.WAV", "W9\\AT_0762.WAV",
                                         "W9\\AT_0763.WAV"};
static const char* kBribeAcceptWavs[2] = {"W3\\AT_0471D.WAV", "W3\\AT_0471E.WAV"};

void BribeGame::Enter(int player) {
    // BribeGame (enc type 12). Verifies eligibility (held key + power>=10) below,
    // then starts the offer audio. Click at any point during the offer or the
    // following 3s grace window pays 10 power and keeps the key; letting the
    // grace window expire loses the key.
    const int held_province = board_.FindHeldProvinceByRotation(player);
    if (held_province < 0 || board_.render_state_[player].power < 10) {
        return;
    }
    // offer/accept each pick and play their one RNG-chosen variant on demand,
    // not a preloaded array of every WAV.
    if (timeout_wav_ == 0) {
        timeout_wav_ = preloadWav("W6\\AT_0593.WAV");
    }
    ReleaseTexture(tex_);
    char bribe_name[12];
    std::snprintf(bribe_name, sizeof(bribe_name), "BRIBES%d", held_province);
    tex_ = assets_.texture("M1", bribe_name);
    player_ = player;
    province_ = held_province;
    phase_ = 0;
    const int offer_idx = RollIndex(board_.RngNext(), 4);
    active_audio_ = playWav(kBribeOfferWavs[offer_idx], AudioFlags::Async);
    board_.video_.close();
    board_.video_loaded_ = false;
    board_.current_state_ = BoardGameState::BribeGame;
    board_.state_start_time_ = std::chrono::steady_clock::now();
    board_.state_action_executed_ = false;
}

void BribeGame::update(const IInput& input, float dt) {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - board_.state_start_time_)
            .count();
    const auto& mouse = input.mouse();
    // Real accept-click zone is the right half of the screen only; declining is
    // genuinely passive (do nothing for 3s), which reads as a dead screen on
    // gamepad. D-pad/stick left-right toggles focus between the key art (left,
    // "decline now" — a UX-only shortcut to the SAME outcome the real 3s
    // timeout already produces) and the real -10 zone (right, accept), A
    // confirms whichever's focused.
    bool gamepad_accept_click = false, gp_decline_now = false;
    if (board_.gamepad_mode_) {
        const auto& gamepad = input.gamepad();
        int dx = 0;
        if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadLeft)]) dx = -1;
        if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadRight)]) dx = 1;
        if (dx == 0) board_.stick_repeater_.PollHorizontal(gamepad.left_x, gamepad.left_y, dt, dx);
        if (dx != 0) focus_accept_ = (dx > 0);
        if (gamepad.pressed[static_cast<size_t>(GamepadButton::A)]) {
            if (focus_accept_)
                gamepad_accept_click = true;
            else
                gp_decline_now = true;
        }
    }
    const bool click = ((mouse.clicked[static_cast<int>(MouseButton::Left)] ||
                         mouse.clicked[static_cast<int>(MouseButton::Right)]) &&
                        mouse.x >= 320) ||
                       gamepad_accept_click;

    if (!board_.state_action_executed_) {
        board_.state_action_executed_ = true;
        focus_accept_ = true;
    }

    if (gp_decline_now && (phase_ == 0 || phase_ == 1)) {
        if (active_audio_) audio_.stop(active_audio_);
        phase_ = 3;
        active_audio_ = timeout_wav_ ? timeout_wav_ : 0;
        if (timeout_wav_) audio_.playLoaded(timeout_wav_, AudioFlags::Async);
    }

    if ((phase_ == 0 || phase_ == 1) && click) {
        // Accept the bribe: cancel whatever's playing, play the accept laugh;
        // phase 2 below pays the 10 power once that audio finishes.
        if (active_audio_) audio_.stop(active_audio_);
        phase_ = 2;
        const int accept_idx = RollIndex(board_.RngNext(), 2);
        active_audio_ = playWav(kBribeAcceptWavs[accept_idx], AudioFlags::Async);
        board_.state_start_time_ = std::chrono::steady_clock::now();
    } else if (phase_ == 0) {
        // Offer audio playing — once it finishes, the grace window starts.
        if (active_audio_ == 0 || !audio_.isPlaying(active_audio_)) {
            phase_ = 1;
            board_.state_start_time_ = std::chrono::steady_clock::now();
        }
    } else if (phase_ == 1) {
        if (elapsed_ms >= 3000) {
            phase_ = 3;
            active_audio_ = timeout_wav_ ? timeout_wav_ : 0;
            if (timeout_wav_) audio_.playLoaded(timeout_wav_, AudioFlags::Async);
        }
    } else if (phase_ == 2) {
        if (active_audio_ == 0 || !audio_.isPlaying(active_audio_)) {
            board_.SubtractPower(player_, 10);
            board_.FreeLazyMinigameAssets();
            board_.current_state_ = BoardGameState::TurnEnd;
            board_.state_start_time_ = std::chrono::steady_clock::now();
            board_.state_action_executed_ = false;
        }
    } else if (phase_ == 3) {
        if (active_audio_ == 0 || !audio_.isPlaying(active_audio_)) {
            if (province_ >= 0) board_.LoseKey(player_, province_);
            board_.FreeLazyMinigameAssets();
            board_.current_state_ = BoardGameState::TurnEnd;
            board_.state_start_time_ = std::chrono::steady_clock::now();
            board_.state_action_executed_ = false;
        }
    }
}

void BribeGame::render() {
    if (tex_) {
        renderer_.drawBitmap(tex_, 0, 0, 0, 0, 640, 480);
    } else {
        renderer_.fillRect(0, 0, 640, 480, 5, 5, 15);
    }
    // Gamepad focus indicator: cursor parks at the center of each zone. Zone
    // extents, hand-measured against the real M1/BRIBES0.BMP art (irregular
    // key/gauge shapes), are each half-screen (0-320 / 320-640) inset 20px
    // on the inner edge, 80px on the screen edge.
    if (board_.gamepad_mode_ && phase_ <= 1) {
        if (focus_accept_) {
            DrawFocusCursor(renderer_, 340 + 110, 20 + 220);
        } else {
            DrawFocusCursor(renderer_, 80 + 110, 20 + 220);
        }
    }
}

void BribeGame::FreeAssets() {
    ReleaseAudio(timeout_wav_);
    // active_audio_ may still hold the RNG-picked offer/accept playWav() transient,
    // or alias timeout_wav_ on the lose-key path (already unloaded above — erase()
    // on an already-gone handle is a safe no-op). Either way this just stops it if
    // still playing, harmless since this encounter is already done by now.
    ReleaseAudio(active_audio_);
    ReleaseTexture(tex_);
}

}  // namespace openfear::game
