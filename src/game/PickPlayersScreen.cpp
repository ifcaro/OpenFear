#include "openfear/game/PickPlayersScreen.h"
#include "openfear/game/GameState.h"
#include "openfear/assets/RectFile.h"
#include <algorithm>
#include <vector>
#include <cstdio>

namespace openfear::game {

const char* PickPlayersScreen::kCountWavs[6] = {
    "W8\\AT_0691.WAV",  // 1 player
    "W1\\AT_0009.WAV",  // 2 players
    "W1\\AT_0010.WAV",  // 3 players
    "W1\\AT_0011.WAV",  // 4 players
    "W1\\AT_0012.WAV",  // 5 players
    "W1\\AT_0013.WAV",  // 6 players
};

bool PickPlayersScreen::load(const AssetPaths&) {
    setScreenName("PickPlayers");
    phase_ = Phase::ClickSelect;

    bg_ = assets_.texture("M2", "PKPLYRS");
    if (!bg_) {
        std::fprintf(stderr, "[PickPlayers] ERROR: Failed to load PKPLYRS\n");
        return false;
    }
    bg_sprite_ = renderer_.createSprite(bg_);
    voice_ = playWav("W1\\AT_0007.WAV");

    auto rects = assets_.rect_file("pkplyrs");
    if (!rects || rects->size() < 6) {
        std::fprintf(stderr,
                     "[PickPlayers] ERROR: Failed to load PKPLYRS.BIN or insufficient rects\n");
        return false;
    }
    for (int i = 0; i < 6; ++i) {
        const auto& r = (*rects)[i];
        cell_rects_[i] = {
            static_cast<int16_t>(std::min(r.x1, r.x2)), static_cast<int16_t>(std::min(r.y1, r.y2)),
            static_cast<int16_t>(std::max(r.x1, r.x2)), static_cast<int16_t>(std::max(r.y1, r.y2))};
        cell_centers_[i] = {static_cast<int16_t>((cell_rects_[i].x1 + cell_rects_[i].x2) / 2),
                            static_cast<int16_t>((cell_rects_[i].y1 + cell_rects_[i].y2) / 2)};
    }
    focus_index_ = 0;
    focus_change_time_ = std::chrono::steady_clock::now();
    stick_tick_time_ = std::chrono::steady_clock::now();
    return true;
}

void PickPlayersScreen::enterCountAudioPhase() {
    // open() is synchronous, but playWav()'s decode runs on a background
    // thread and returns immediately - neither call waits on the other
    // here. play() itself is deferred to update(), which polls isReady()
    // per frame instead of blocking the whole game loop until decode
    // finishes.
    const auto gkloop_path = AssetPaths::avi("AVI", "gkloop1");
    video_open_ok_ = video_.open(gkloop_path.string());
    if (!video_open_ok_) {
        std::fprintf(
            stderr,
            "[PickPlayers] Phase 2: WARNING GKLOOP1.AVI not found, proceeding without video\n");
    }

    const int count_idx = (player_count_ > 0 && player_count_ <= 6) ? (player_count_ - 1) : 0;
    count_wav_ = playWav(kCountWavs[count_idx], AudioFlags::Async);

    phase_ = Phase::CountAudio;
}

bool PickPlayersScreen::update(const IInput& input) {
    RefreshGamepadMode(input);

    if (phase_ == Phase::ClickSelect) {
        // Click stops the intro voice immediately and advances right away
        // — does not wait for the voice to finish naturally.
        const auto& gp = input.gamepad();
        const auto now_pp = std::chrono::steady_clock::now();
        const float dt_pp = std::chrono::duration<float>(now_pp - stick_tick_time_).count();
        stick_tick_time_ = now_pp;

        if (gamepad_mode_) {
            // Gamepad discrete-focus over the 6 player-count columns
            // (port-only UX, no ASM basis).
            const int focus_before = focus_index_;
            int dx = 0, dy = 0;
            if (gp.pressed[static_cast<size_t>(GamepadButton::DpadLeft)]) dx = -1;
            if (gp.pressed[static_cast<size_t>(GamepadButton::DpadRight)]) dx = 1;
            if (gp.pressed[static_cast<size_t>(GamepadButton::DpadUp)]) dy = -1;
            if (gp.pressed[static_cast<size_t>(GamepadButton::DpadDown)]) dy = 1;
            if (dx == 0 && dy == 0) stick_repeater_.Poll(gp.left_x, gp.left_y, dt_pp, dx, dy);
            if (dx != 0 || dy != 0) {
                std::vector<Point> centers(cell_centers_.begin(), cell_centers_.end());
                focus_index_ = FindNextFocus(centers, focus_index_, dx, dy);
            }
            if (focus_index_ != focus_before) {
                focus_change_time_ = std::chrono::steady_clock::now();
            }
        }

        int hit_index = -1;
        if (clicked(input)) {
            const auto& m = input.mouse();
            for (int i = 0; i < 6; ++i) {
                const auto& r = cell_rects_[i];
                const bool hit = PointInRect(m.x, m.y, r);
                if (hit) {
                    hit_index = i;
                    break;
                }
            }
            if (hit_index < 0) std::fprintf(stderr, "[PickPlayers]   no rect hit\n");
        } else if (gamepad_mode_ && gp.pressed[static_cast<size_t>(GamepadButton::A)]) {
            hit_index = focus_index_;
        }

        if (hit_index < 0) return false;

        player_count_ = hit_index + 1;

        GameState* gs = GameStateManager::Instance().Get();
        if (gs && gs->IsValid()) {
            gs->total_players = player_count_;
        } else {
            std::fprintf(stderr, "[PickPlayers] ERROR: GameState invalid or null\n");
        }

        if (voice_ != 0) audio_.stop(voice_);
        enterCountAudioPhase();
        return false;
    }

    // Phase 2: play() GKLOOP1.AVI (already open()ed) once the count audio
    // is ready to play (not just requested) - both start on the same frame
    // instead of the video looping for a moment before the voice catches up.
    if (!video_loaded_ && video_open_ok_ && (count_wav_ == 0 || audio_.isReady(count_wav_))) {
        // Same values as BoardGameScreen's private kBoardAreaX/Y/W/H
        // (its GkLoopAVI state uses the same rect) — hardcoded again here
        // since that constant isn't exposed outside BoardGameScreen.
        video_.setRenderRect(0, 77, 640, 326);
        video_.play(true);  // Loop
        video_loaded_ = true;
    }

    // While count audio plays. Click (or gamepad A) skips; otherwise waits
    // for the audio to finish naturally.
    if (count_wav_ != 0 && audio_.isPlaying(count_wav_)) {
        const bool gp_skip =
            gamepad_mode_ && input.gamepad().pressed[static_cast<size_t>(GamepadButton::A)];
        if (clicked(input) || gp_skip) {
            stopSound(count_wav_);
            count_wav_ = 0;
            return true;
        }
        return false;
    }

    if (count_wav_ != 0) {
        stopSound(count_wav_);
        count_wav_ = 0;
    }
    return true;
}

void PickPlayersScreen::render() {
    if (phase_ == Phase::ClickSelect) {
        if (bg_) renderer_.drawBitmap(bg_, 0, 0, 0, 0, 640, 480);
        // DrawFocusTint (darkening bmp tint), not DrawFocusPulse (rect
        // overlay) — deliberate per-screen choice, this screen's own look.
        if (gamepad_mode_ && bg_) {
            const auto& r = cell_rects_[focus_index_];
            const auto anchor_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       focus_change_time_.time_since_epoch())
                                       .count();
            DrawFocusTint(renderer_, bg_sprite_, r.x1, r.y1, r.x1, r.y1, r.x2 - r.x1, r.y2 - r.y1,
                          anchor_ms);
            DrawFocusCursor(renderer_, (r.x1 + r.x2) / 2, (r.y1 + r.y2) / 2);
        }
        return;
    }
    // Phase 2: black background (beginFrame() clears to black) + GKLOOP1.AVI
    // on top — the original doesn't render PKPLYRS.BMP during this phase.
    if (video_loaded_) video_.renderFrame();
}

void PickPlayersScreen::unload() {
    if (video_loaded_) {
        video_.close();
        video_loaded_ = false;
    }
    ReleaseTexture(bg_);
}

}  // namespace openfear::game
