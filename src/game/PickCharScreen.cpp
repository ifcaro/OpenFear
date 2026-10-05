#include "openfear/game/PickCharScreen.h"
#include "openfear/game/PlayerInitializer.h"
#include "openfear/assets/RectFile.h"
#include <algorithm>
#include <numeric>
#include <vector>
#include <cstdio>

namespace openfear::game {

// Voice intro (played at screen load) — varies by player index.
static const char* kVoiceIntroWav[2] = {
    "W1\\AT_0014.WAV",  // 1st player
    "W3\\AT_0394.WAV",  // 2+P
};

// CINTRO_{n}.WAV — narrator introduces the chosen character. Plays after
// click, while the portrait is displayed.
static const char* kCintroWav[6] = {
    "W10\\CINTRO_0.WAV", "W10\\CINTRO_1.WAV", "W10\\CINTRO_2.WAV",
    "W10\\CINTRO_3.WAV", "W10\\CINTRO_4.WAV", "W10\\CINTRO_5.WAV",
};

bool PickCharScreen::load(const AssetPaths&, int player_index, int n_players,
                          const std::array<bool, 6>& taken) {
    setScreenName("PickChar");

    // load()ed once PER PLAYER in a multiplayer game, reusing the same
    // object — free the previous player's textures first or they leak for
    // the rest of the match. voice_intro_wav_/cintro_wav_ are transient
    // playWav() handles (not preloaded), no release needed for those;
    // glass_wav_ already has its own free at click time.
    ReleaseTexture(bg_);
    ReleaseTexture(bg_sel_);
    ReleaseTexture(portrait_);

    taken_ = taken;
    selected_ = -1;
    voice_intro_wav_ = 0;
    glass_wav_ = 0;
    cintro_wav_ = 0;
    wait_phase_ = WaitPhase::None;
    stick_repeater_ = StickRepeater{};

    bg_ = assets_.texture("M1", "HEXAGON");
    bg_sel_ = assets_.texture("M1", "HEXAGONB");
    if (!bg_ || !bg_sel_) {
        std::fprintf(stderr, "[PickChar] ERROR: Failed to load HEXAGON or HEXAGONB\n");
        return false;
    }

    auto rects = assets_.rect_file("pkchar");
    if (!rects || rects->size() < 6) {
        std::fprintf(stderr, "[PickChar] ERROR: Failed to load PKCHAR.BIN or insufficient rects\n");
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

    // Character order randomization only happens on the FIRST player;
    // subsequent players keep the same positions.
    if (player_index == 0) {
        std::iota(char_order_.begin(), char_order_.end(), 0);

        // Not a textbook Fisher-Yates: re-rolls a fresh threshold (3-12)
        // on every iteration check instead of picking a count up front,
        // swapping adjacent pairs while the running count stays below it
        // - a random walk capped at 12, not a fixed pick. 2 RNG calls per
        // swap plus 1 final failing call; must stay exact, it affects
        // every RNG roll downstream.
        int num_swaps = 0;
        for (;;) {
            const uint16_t r_threshold = PlayerInitializer::GetRandomNumber();
            const int threshold = (r_threshold * 10) / 32768 + 3;
            if (threshold <= num_swaps) break;
            const uint16_t r_idx = PlayerInitializer::GetRandomNumber();
            const int swap_idx = (r_idx * 5) / 32768;  // 0..4
            std::swap(char_order_[swap_idx], char_order_[swap_idx + 1]);
            ++num_swaps;
        }

    } else {
    }

    stick_tick_time_ = std::chrono::steady_clock::now();

    // Gamepad discrete-focus: start on the topmost, then leftmost
    // available (untaken) cell. PKCHAR.BIN's rect order doesn't match
    // visual reading order (index 0 happens to sit bottom-right on
    // screen), so pick by actual (y, then x) position instead of raw
    // array order, so the starting focus is always visually top-left.
    focus_index_ = 0;
    bool focus_set = false;
    for (int i = 0; i < 6; ++i) {
        if (taken_[char_order_[i]]) continue;
        if (!focus_set || cell_centers_[i].y < cell_centers_[focus_index_].y ||
            (cell_centers_[i].y == cell_centers_[focus_index_].y &&
             cell_centers_[i].x < cell_centers_[focus_index_].x)) {
            focus_index_ = i;
            focus_set = true;
        }
    }
    focus_change_time_ = std::chrono::steady_clock::now();

    const int voice_idx = (player_index == 0) ? 0 : 1;
    voice_intro_wav_ = playWav(kVoiceIntroWav[voice_idx], AudioFlags::Async);

    // Precache the glass WAV now; it plays later, on click (see update()).
    glass_wav_ = preloadWav("W9\\AT_GLASS.WAV");

    // State machine: VoiceIntro -> Glass (on click) -> Cintro
    wait_phase_ = WaitPhase::VoiceIntro;

    return true;
}

bool PickCharScreen::update(const IInput& input) {
    RefreshGamepadMode(input);

    // Phase 1: voice intro plays at screen load. A real character-zone
    // click stops the voice immediately and advances right away, without
    // waiting for the voice to finish.
    if (wait_phase_ == WaitPhase::VoiceIntro) {
        const auto& gp = input.gamepad();
        const auto now_pc = std::chrono::steady_clock::now();
        const float dt_pc = std::chrono::duration<float>(now_pc - stick_tick_time_).count();
        stick_tick_time_ = now_pc;

        if (selected_ < 0 && gamepad_mode_) {
            // Discrete D-pad navigation between the 6 hexagon cells,
            // filtered to available (untaken) cells only.
            const int focus_before = focus_index_;
            int dx = 0, dy = 0;
            if (gp.pressed[static_cast<size_t>(GamepadButton::DpadLeft)]) dx = -1;
            if (gp.pressed[static_cast<size_t>(GamepadButton::DpadRight)]) dx = 1;
            if (gp.pressed[static_cast<size_t>(GamepadButton::DpadUp)]) dy = -1;
            if (gp.pressed[static_cast<size_t>(GamepadButton::DpadDown)]) dy = 1;
            if (dx == 0 && dy == 0) stick_repeater_.Poll(gp.left_x, gp.left_y, dt_pc, dx, dy);
            if (dx != 0 || dy != 0) {
                std::vector<Point> avail_centers;
                std::vector<int> avail_indices;
                int current_avail = 0;
                for (int i = 0; i < 6; ++i) {
                    if (taken_[char_order_[i]]) continue;
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

        int hit_slot = -1;
        if (selected_ < 0 && clicked(input)) {
            const auto& m = input.mouse();
            for (int i = 0; i < 6; ++i) {
                const int char_id = char_order_[i];
                const bool is_taken = taken_[char_id];
                const auto& r = cell_rects_[i];
                const bool hit = !is_taken && PointInRect(m.x, m.y, r);
                if (hit) {
                    hit_slot = i;
                    break;
                }
            }
        } else if (selected_ < 0 && gamepad_mode_ &&
                   gp.pressed[static_cast<size_t>(GamepadButton::A)]) {
            if (!taken_[char_order_[focus_index_]]) hit_slot = focus_index_;
        }

        if (hit_slot >= 0) {
            selected_ = hit_slot;
        }

        if (selected_ < 0) return false;

        if (voice_intro_wav_ != 0) audio_.stop(voice_intro_wav_);
        if (glass_wav_ != 0) {
            audio_.playLoaded(glass_wav_, AudioFlags::None);
        }
        wait_phase_ = WaitPhase::Glass;
        return false;
    }

    // Phase 2: glass click sound plays (after user selects character)
    if (wait_phase_ == WaitPhase::Glass) {
        if (glass_wav_ != 0 && audio_.isPlaying(glass_wav_)) return false;
        // Glass will never play again this pick (at most 1 click per
        // player); free it now instead of holding it resident. load()
        // reloads it fresh if this screen is revisited.
        stopSound(glass_wav_);
        ReleaseAudio(glass_wav_);
        const int char_id = char_order_[selected_];
        portrait_ = assets_.portrait(char_id);
        cintro_wav_ = playWav(kCintroWav[char_id], AudioFlags::Async);
        wait_phase_ = WaitPhase::Cintro;
        return false;
    }

    // Phase 3: character intro (CINTRO) plays while portrait displayed
    if (wait_phase_ == WaitPhase::Cintro) {
        // Any button skips (no zone/focus concept on a fullscreen
        // portrait, so any press is the natural equivalent of a click).
        const bool gp_pressed = gamepad_mode_ && AnyPressed(input.gamepad());
        if (clicked(input) || gp_pressed) {
            stopSound(cintro_wav_);
            return true;
        }
        if (cintro_wav_ == 0 || !audio_.isPlaying(cintro_wav_)) {
            return true;
        }
        return false;
    }

    return false;
}

void PickCharScreen::render() {
    // Same portrait bitmap family as BoardGameScreen's ShowingPortrait
    // (INFO{n}_0.BMP) — same bottom-aligned key/icon row to protect. The
    // selection grid itself has no such bottom-critical UI, stays Center.
    renderer_.setSafeAreaAnchor(wait_phase_ == WaitPhase::Cintro
                                    ? IRenderer::SafeAreaAnchor::Bottom
                                    : IRenderer::SafeAreaAnchor::Center);

    if (wait_phase_ == WaitPhase::Cintro && portrait_) {
        renderer_.drawBitmap(portrait_, 0, 0, 0, 0, 640, 480);
        return;
    }

    // The base grid is always a single, unmodified full-bitmap blit — the
    // per-cell shuffled crop is only used for already-taken cells and the
    // just-clicked one, never for plain available cells.
    if (bg_) renderer_.drawBitmap(bg_, 0, 0, 0, 0, 640, 480);

    for (int i = 0; i < 6; ++i) {
        const int char_id = char_order_[i];
        const bool use_sel = taken_[char_id] || (i == selected_);
        const auto& src = cell_rects_[char_id];  // character's position in the BMP
        const auto& dst = cell_rects_[i];        // cell position on screen
        // Size from src (home cell), not dst — PKCHAR.BIN's columns vary
        // by 1px, so sizing from dst would shift/crop the highlight
        // whenever home and destination columns differ.
        const int w = src.x2 - src.x1;
        const int h = src.y2 - src.y1;

        // Gamepad focus indicator: brightness-pulse the focused,
        // still-available cell instead of a plain blit.
        if (gamepad_mode_ && selected_ < 0 && i == focus_index_ && !taken_[char_id]) {
            const auto anchor_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       focus_change_time_.time_since_epoch())
                                       .count();
            DrawFocusPulse(renderer_, SpriteInfo{}, dst.x1, dst.y1, src.x1, src.y1, w, h,
                           anchor_ms);
            DrawFocusCursor(renderer_, dst.x1 + w / 2, dst.y1 + h / 2);
        } else if (use_sel && bg_sel_) {
            renderer_.drawBitmap(bg_sel_, dst.x1, dst.y1, src.x1, src.y1, w, h);
        }
    }
}

void PickCharScreen::unload() {
    ReleaseTexture(bg_);
    ReleaseTexture(bg_sel_);
    ReleaseTexture(portrait_);
}

}  // namespace openfear::game
