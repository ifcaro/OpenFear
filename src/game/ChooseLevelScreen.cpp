#include "openfear/game/ChooseLevelScreen.h"
#include "openfear/assets/RectFile.h"
#include <algorithm>
#include <cstdio>

namespace openfear::game {

// Duration voices: 3 variants, picked from a gauge-position formula with a
// -3 bias (see the click handler below).
static const char* kDurationWavs[3] = {
    "W8\\AT_0702.WAV",  // gauge near top
    "W8\\AT_0703.WAV",  // gauge in middle
    "W8\\AT_0704.WAV",  // gauge near bottom
};

bool ChooseLevelScreen::load(const AssetPaths&) {
    setScreenName("ChooseLevel");

    gauge_cur_ = 0;
    selected_level_ = -1;
    grace_period_remaining_ = 500;  // ms — click debounce
    phase_ = Phase::Init;
    intro_wav_ = 0;
    duration_wav_ = 0;
    last_frame_time_ = std::chrono::steady_clock::now();

    bg_ = assets_.texture("M1", "1PLYLEV");
    sprite_ = assets_.texture("M1", "1PLYLBLT");
    if (!bg_ || !sprite_) {
        std::fprintf(stderr, "[ChooseLevel] ERROR: Failed to load 1PLYLEV or 1PLYLBLT\n");
        return false;
    }
    sprite_info_ = renderer_.createSprite(sprite_);

    auto rects = assets_.rect_file("chooslev");
    if (!rects || rects->size() < 2) {
        std::fprintf(stderr,
                     "[ChooseLevel] ERROR: Failed to load CHOOSLEV.BIN or insufficient rects\n");
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        const auto& r = (*rects)[i];
        zones_[i] = {
            static_cast<int16_t>(std::min(r.x1, r.x2)), static_cast<int16_t>(std::min(r.y1, r.y2)),
            static_cast<int16_t>(std::max(r.x1, r.x2)), static_cast<int16_t>(std::max(r.y1, r.y2))};
    }

    intro_wav_ = playWav("W8\\AT_0701.WAV", AudioFlags::Async);
    phase_ = Phase::Intro;

    return true;
}

bool ChooseLevelScreen::update(const IInput& input) {
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - last_frame_time_).count();
    const uint32_t delta_ms = static_cast<uint32_t>(dt * 1000.0f);
    last_frame_time_ = now;

    RefreshGamepadMode(input);

    if (grace_period_remaining_ > 0) {
        grace_period_remaining_ -= delta_ms;
    }

    if (phase_ == Phase::Intro) {
        // Don't wait for the intro voice — move to tracking immediately.
        phase_ = Phase::Tracking;
    }

    if (phase_ == Phase::Tracking) {
        int bar_y1 = zones_[1].y1;
        int bar_y2 = zones_[1].y2;
        // range = zone_y2-zone_y1+1 (inclusive pixel count) - same value
        // render() uses for its bar_height, computed once in the original
        // and reused for both the fill bar and this percentage math.
        int bar_height = bar_y2 - bar_y1 + 1;

        // Gamepad: genuinely continuous, unlike most screens' discrete-focus
        // model (DuelAttackGauge is the other legitimate free-cursor case).
        // Left stick Y and D-pad up/down are additive and both move the
        // gauge directly at a fixed speed instead of snapping to a position.
        if (gamepad_mode_ && bar_height > 0) {
            constexpr float kGaugeSpeed = 300.0f;  // px/sec
            // Wide deadzone (0.3), quantized to ±1 - same gauge pattern as
            // DuelAttackGauge: the analog magnitude is never used (kGaugeSpeed
            // is a flat rate), so a stick resting off-center would drive the
            // gauge on its own with no deadzone.
            constexpr float kStickDeadzone = 0.3f;
            const float raw_left_y = input.gamepad().left_y;
            float stick_dir = 0.0f;
            if (raw_left_y > kStickDeadzone)
                stick_dir = 1.0f;
            else if (raw_left_y < -kStickDeadzone)
                stick_dir = -1.0f;
            const float ly = stick_dir + static_cast<float>(input.gamepad().dpad_y);
            gauge_cur_ =
                std::clamp(gauge_cur_ + static_cast<int>(ly * kGaugeSpeed * dt), 0, bar_height - 1);
        } else if (bar_height > 0) {
            int mouse_y = input.mouse().y;
            gauge_cur_ = std::clamp(mouse_y - bar_y1, 0, bar_height - 1);
        } else {
            gauge_cur_ = 0;
        }

        bool is_clicked =
            clicked(input) ||
            (gamepad_mode_ && input.gamepad().pressed[static_cast<size_t>(GamepadButton::A)]);

        if (is_clicked && grace_period_remaining_ <= 0) {
            // gauge_cur = 0 (top) -> temperature 99 (hottest/hardest)
            // gauge_cur = bar_height-1 (bottom) -> temperature ~0 (easiest)
            int bar_h = (bar_height > 0) ? bar_height : 235;
            int temperature = (bar_h - gauge_cur_) * 100 / bar_h;
            temperature = std::clamp(temperature, 0, 99);

            // Difficulty level 0-9 maps to 10-degree bands.
            selected_level_ = std::clamp(temperature / 10, 0, 9);

            int audio_index = std::clamp(((bar_h - gauge_cur_) * 3 - 3) / bar_h, 0, 2);

            stopSound(intro_wav_);
            intro_wav_ = 0;

            duration_wav_ = playWav(kDurationWavs[audio_index], AudioFlags::Async);
            phase_ = Phase::DurationVoicePlaying;
            return false;
        }

        return false;
    }

    if (phase_ == Phase::DurationVoicePlaying) {
        // A click during this phase stops the duration voice and advances
        // immediately, same as it finishing naturally.
        const bool is_clicked =
            clicked(input) ||
            (gamepad_mode_ && input.gamepad().pressed[static_cast<size_t>(GamepadButton::A)]);
        if (is_clicked) {
            stopSound(duration_wav_);
            duration_wav_ = 0;
            phase_ = Phase::Done;
            return true;
        }
        if (duration_wav_ != 0 && audio_.isPlaying(duration_wav_)) {
            return false;
        }
        phase_ = Phase::Done;
        return true;
    }

    return false;
}

void ChooseLevelScreen::render() {
    if (bg_) {
        renderer_.drawBitmap(bg_, 0, 0, 0, 0, 640, 480);
    }

    if (sprite_) {
        int bar_x1 = zones_[1].x1;
        int bar_x2 = zones_[1].x2;
        int bar_y1 = zones_[1].y1;
        int bar_y2 = zones_[1].y2;
        int bar_width = bar_x2 - bar_x1 + 1;
        int bar_height = bar_y2 - bar_y1 + 1;

        // gauge_cur = 0 (top) -> full height; gauge_cur = bar_height-1 (bottom, its
        // max) -> 1px.
        int sprite_height = bar_height - gauge_cur_;
        sprite_height = std::clamp(sprite_height, 0, bar_height);

        // Sprite stretches UP from the bottom of the bar.
        int sprite_y = bar_y2 - sprite_height;
        int sprite_x = bar_x1;
        int sprite_width = bar_width;

        // Draw from the bottom of the sprite source, not the top.
        int src_y = bar_height - sprite_height;

        // Tint the gauge's own fill sprite while still selectable — hide
        // once locked in, so it doesn't keep pulsing on a frozen gauge.
        if (gamepad_mode_ && phase_ == Phase::Tracking) {
            DrawFocusTint(renderer_, sprite_info_, sprite_x, sprite_y, 0, src_y, sprite_width,
                          sprite_height);
        } else {
            renderer_.drawBitmap(sprite_, sprite_x, sprite_y, 0, src_y, sprite_width,
                                 sprite_height);
        }
    }
}

void ChooseLevelScreen::unload() {
    ReleaseTexture(bg_);
    ReleaseTexture(sprite_);
}

}  // namespace openfear::game
