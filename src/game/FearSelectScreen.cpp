#include "openfear/game/FearSelectScreen.h"
#include "openfear/game/GameState.h"
#include "openfear/game/PlayerInitializer.h"
#include "openfear/assets/RectFile.h"
#include <vector>
#include <cstdio>

namespace openfear::game {

// Opening prompts: 5 variants, picked via idx = (random * 5) / 32768.
static const char* kOpeningWavs[5] = {
    "W4\\AT_0490.WAV", "W4\\AT_0491.WAV", "W4\\AT_0492.WAV", "W4\\AT_0493.WAV", "W4\\AT_0494.WAV",
};

// Confirm prompts: 6 variants, picked via idx = (random * 6) / 32768.
static const char* kConfirmWavs[6] = {
    "W3\\AT_0400.WAV",  "W3\\AT_0401.WAV",  "W3\\AT_0471D.WAV",
    "W3\\AT_0471E.WAV", "W3\\AT_0473A.WAV", "W3\\AT_0473B.WAV",
};

bool FearSelectScreen::load(const AssetPaths&, int player_index, const std::array<bool, 6>& taken) {
    setScreenName("FearSelect");

    // load()ed once PER PLAYER, reusing the same object — free the
    // previous player's textures first or they leak for the rest of the match.
    ReleaseTexture(bg_);
    ReleaseTexture(bg_sel_);

    player_index_ = player_index;
    taken_ = taken;
    selected_ = -1;
    phase_ = Phase::Init;
    opening_wav_ = 0;
    glass_wav_ = 0;
    confirm_wav_ = 0;
    stick_repeater_ = StickRepeater{};

    GameState* gs = GameStateManager::Instance().Get();
    if (!gs || !gs->IsValid()) {
        std::fprintf(stderr, "[FearSelect] ERROR: GameState not initialized\n");
        return false;
    }

    bg_ = assets_.misc_texture("GFEARS_2");
    bg_sel_ = assets_.misc_texture("GFEARS_0");
    if (!bg_ || !bg_sel_) {
        std::fprintf(stderr, "[FearSelect] ERROR: Failed to load GFEARS bitmaps\n");
        return false;
    }

    auto rects = assets_.rect_file("greatfea");
    if (!rects || rects->size() < 6) {
        std::fprintf(stderr,
                     "[FearSelect] ERROR: Failed to load GREATFEA.BIN or insufficient rects\n");
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

    stick_tick_time_ = std::chrono::steady_clock::now();

    // Gamepad discrete-focus: start on the topmost, then leftmost
    // available (untaken) cell — GREATFEA.BIN's rect order doesn't
    // necessarily match visual reading order, so pick by actual (y, then
    // x) position instead of array index.
    focus_index_ = 0;
    bool focus_set = false;
    for (int i = 0; i < 6; ++i) {
        if (taken_[i]) continue;
        if (!focus_set || cell_centers_[i].y < cell_centers_[focus_index_].y ||
            (cell_centers_[i].y == cell_centers_[focus_index_].y &&
             cell_centers_[i].x < cell_centers_[focus_index_].x)) {
            focus_index_ = i;
            focus_set = true;
        }
    }
    focus_change_time_ = std::chrono::steady_clock::now();

    // Precache glass now for zero-latency; it plays later, on click.
    glass_wav_ = preloadWav("W9\\AT_GLASS.WAV");

    // Random opening prompt — uses the same LCG (PlayerInitializer) as the
    // rest of gameplay's randomness.
    uint16_t random_val = PlayerInitializer::GetRandomNumber();
    const int opening_idx = (random_val * 5) / 32768;
    opening_wav_ = playWav(kOpeningWavs[opening_idx], AudioFlags::Async);
    phase_ = Phase::OpeningPrompt;

    return true;
}

bool FearSelectScreen::update(const IInput& input) {
    RefreshGamepadMode(input);

    // Phase: OpeningPrompt. Accept clicks on fear cells; opening voice
    // stops immediately on click and advances right away, without waiting
    // for it to finish.
    if (phase_ == Phase::OpeningPrompt) {
        const auto& gp = input.gamepad();
        const auto now_fs = std::chrono::steady_clock::now();
        const float dt_fs = std::chrono::duration<float>(now_fs - stick_tick_time_).count();
        stick_tick_time_ = now_fs;

        if (selected_ < 0 && gamepad_mode_) {
            // Discrete D-pad navigation, filtered to available (untaken)
            // cells so the focus can't land on an already-picked fear.
            const int focus_before = focus_index_;
            int dx = 0, dy = 0;
            if (gp.pressed[static_cast<size_t>(GamepadButton::DpadLeft)]) dx = -1;
            if (gp.pressed[static_cast<size_t>(GamepadButton::DpadRight)]) dx = 1;
            if (gp.pressed[static_cast<size_t>(GamepadButton::DpadUp)]) dy = -1;
            if (gp.pressed[static_cast<size_t>(GamepadButton::DpadDown)]) dy = 1;
            if (dx == 0 && dy == 0) stick_repeater_.Poll(gp.left_x, gp.left_y, dt_fs, dx, dy);
            if (dx != 0 || dy != 0) {
                std::vector<Point> avail_centers;
                std::vector<int> avail_indices;
                int current_avail = 0;
                for (int i = 0; i < 6; ++i) {
                    if (taken_[i]) continue;
                    if (i == focus_index_) current_avail = static_cast<int>(avail_indices.size());
                    avail_indices.push_back(i);
                    avail_centers.push_back(cell_centers_[i]);
                }
                if (!avail_indices.empty()) {
                    const int next_avail = FindNextFocus(avail_centers, current_avail, dx, dy);
                    focus_index_ = avail_indices[next_avail];
                }
            }
            if (focus_index_ != focus_before) {
                focus_change_time_ = std::chrono::steady_clock::now();
            }
        }

        if (selected_ < 0 && clicked(input)) {
            const auto& m = input.mouse();

            for (int i = 0; i < 6; ++i) {
                const bool is_taken = taken_[i];
                const auto& r = cell_rects_[i];
                const bool hit = !is_taken && PointInRect(m.x, m.y, r);

                if (hit) {
                    selected_ = i;
                    break;
                }
            }
            if (selected_ < 0) std::fprintf(stderr, "[FearSelect]   no rect hit\n");
        } else if (selected_ < 0 && gamepad_mode_ &&
                   gp.pressed[static_cast<size_t>(GamepadButton::A)]) {
            if (!taken_[focus_index_]) {
                selected_ = focus_index_;
            }
        }

        if (selected_ < 0) return false;

        if (opening_wav_ != 0) audio_.stop(opening_wav_);
        opening_wav_ = 0;

        if (glass_wav_ != 0) {
            audio_.playLoaded(glass_wav_, AudioFlags::None);
        }

        phase_ = Phase::Selection;
        return false;
    }

    // Phase: Selection. Glass plays; after it finishes, play a random
    // confirm prompt. User can skip by clicking, or wait for it to finish.
    if (phase_ == Phase::Selection) {
        if (glass_wav_ != 0 && audio_.isPlaying(glass_wav_)) {
            return false;
        }

        // Glass will never play again this selection (at most 1 click);
        // free it now instead of holding it resident.
        ReleaseAudio(glass_wav_);

        if (confirm_wav_ == 0) {
            uint16_t random_val = PlayerInitializer::GetRandomNumber();
            const int confirm_idx = (random_val * 6) / 32768;
            confirm_wav_ = playWav(kConfirmWavs[confirm_idx], AudioFlags::Async);
            return false;
        }

        const bool is_playing = audio_.isPlaying(confirm_wav_);

        const bool gp_skip =
            gamepad_mode_ && input.gamepad().pressed[static_cast<size_t>(GamepadButton::A)];
        if (clicked(input) || gp_skip) {
            stopSound(confirm_wav_);
            phase_ = Phase::Done;
            return false;
        }

        if (!is_playing) {
            phase_ = Phase::Done;
            return false;
        }

        return false;
    }

    if (phase_ == Phase::Done) {
        return true;
    }

    return false;
}

void FearSelectScreen::render() {
    if (bg_) {
        renderer_.drawBitmap(bg_, 0, 0, 0, 0, 640, 480);
    }

    if (bg_sel_) {
        for (int i = 0; i < 6; ++i) {
            const bool show_overlay = taken_[i] || (i == selected_);
            if (!show_overlay) continue;

            const auto& r = cell_rects_[i];
            const int w = r.x2 - r.x1;
            const int h = r.y2 - r.y1;
            renderer_.drawBitmap(bg_sel_, r.x1, r.y1, r.x1, r.y1, w, h);
        }
    }

    if (gamepad_mode_ && selected_ < 0 && bg_ && !taken_[focus_index_]) {
        const auto& r = cell_rects_[focus_index_];
        const auto anchor_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   focus_change_time_.time_since_epoch())
                                   .count();
        DrawFocusPulse(renderer_, SpriteInfo{}, r.x1, r.y1, r.x1, r.y1, r.x2 - r.x1, r.y2 - r.y1,
                       anchor_ms);
        DrawFocusCursor(renderer_, (r.x1 + r.x2) / 2, (r.y1 + r.y2) / 2);
    }
}

void FearSelectScreen::unload() {
    ReleaseTexture(bg_);
    ReleaseTexture(bg_sel_);
}

}  // namespace openfear::game
