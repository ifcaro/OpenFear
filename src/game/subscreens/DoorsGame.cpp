#include "openfear/game/subscreens/DoorsGame.h"
#include "openfear/game/BoardGameScreen.h"
#include <chrono>

namespace openfear::game {

bool DoorsGame::LoadZoneRects() {
    if (!LoadRectZoneOrFail("door3", 3, rects3_.data(),
                            "DOOR3.BIN not loaded/wrong size — door zones would never respond "
                            "to clicks")) {
        return false;
    }
    if (!LoadRectZoneOrFail("door6", 6, rects6_.data(),
                            "DOOR6.BIN not loaded/wrong size — door zones would never respond "
                            "to clicks")) {
        return false;
    }
    return LoadRectZoneOrFail("door10", 10, rects10_.data(),
                              "DOOR10.BIN not loaded/wrong size — door zones would never "
                              "respond to clicks");
}

void DoorsGame::Enter(int player) {
    // No GKLOOP/EYELOOP. The door click zone has a 4000ms timer:
    //   - REAL CLICK within 4s -> 8-entry MIXED table: power delta one of
    //     -12/-10/-8/-6/+6/+8/+10/+12, tapered by powerFactor (not a flat 50/50).
    //   - TIMEOUT (no click) -> 4-entry ALL-LOSS table: power delta one of
    //     -12/-10/-8/-6 (guaranteed loss, smaller magnitude range than a bad click),
    //     same taper.
    // door_type {3,6,10} only selects the door BMP, not the outcome table. Magnitude/
    // sign resolved at click/timeout time in update(), not here.
    player_ = player;
    static const int kDoorTypes[3] = {3, 6, 10};
    type_ = kDoorTypes[RollIndex(board_.RngNext(), 3)];
    ReleaseTexture(door_tex_);
    const std::string door_bmp_name = "DOORS" + std::to_string(type_);
    door_tex_ = assets_.texture("G1", door_bmp_name.c_str());

    zone_rects_ = (type_ == 3) ? rects3_.data() : (type_ == 6) ? rects6_.data() : rects10_.data();
    zone_count_ = type_;
    board_.video_.close();
    board_.video_loaded_ = false;
    board_.current_state_ = BoardGameState::DoorsGame;
    board_.state_start_time_ = std::chrono::steady_clock::now();
    board_.state_action_executed_ = false;
}

void DoorsGame::update(const IInput& input, float dt) {
    using Clock = std::chrono::steady_clock;

    // 3 real audio clips + a final wait-and-transition step:
    //   1. Setup (Enter): entry clip plays immediately. The door creak WAV is
    //      loaded but not played yet.
    //   2. phase_==0: on a REAL CLICK, stop the entry clip if still
    //      playing, then play the door creak. On TIMEOUT, this step is skipped —
    //      go straight to step 3, no creak.
    //   3. phase_==1: once the creak finishes, applies the power delta and
    //      plays ONE of 3 separate result pools depending on outcome —
    //      click+gain (3 clips), click+loss (2 clips), or timeout (2 clips, a
    //      genuinely separate pool from click+loss).
    //   4. phase_==2: wait for that result audio to finish, then TurnEnd.
    // The 4s auto-timeout only starts counting once the entry audio
    // is confirmed done.
    if (!board_.state_action_executed_) {
        board_.state_action_executed_ = true;
        phase_ = 0;
        focus_ = 0;
        entry_audio_done_time_ = Clock::time_point::min();
        static const char* kDoorsEntryWav[6] = {"W2\\AT_0182.WAV", "W6\\AT_0611.WAV",
                                                "W6\\AT_0612.WAV", "W6\\AT_0613.WAV",
                                                "W6\\AT_0614.WAV", "W6\\AT_0615.WAV"};
        entry_wav_ = playWav(kDoorsEntryWav[RollIndex(board_.RngNext(), 6)], AudioFlags::Async);
    }

    if (phase_ == 0) {
        bool any_click = false;
        if (input.mouse().clicked[static_cast<int>(MouseButton::Left)] ||
            input.mouse().clicked[static_cast<int>(MouseButton::Right)]) {
            const auto& mouse = input.mouse();
            for (int i = 0; i < zone_count_; ++i) {
                const auto& z = zone_rects_[i];
                if (PointInRect(mouse.x, mouse.y, z)) {
                    any_click = true;
                    break;
                }
            }
        }
        // Gamepad: D-pad/stick navigates between the real doors (positional
        // choice, matches mouse — the RNG outcome doesn't depend on which door,
        // but mouse players still get to pick one visually). A presses
        // whichever door is focused.
        if (board_.gamepad_mode_ && zone_count_ > 0) {
            const auto& gamepad = input.gamepad();
            int dx = 0, dy = 0;
            if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadLeft)]) dx = -1;
            if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadRight)]) dx = 1;
            if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadUp)]) dy = -1;
            if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadDown)]) dy = 1;
            if (dx == 0 && dy == 0)
                board_.stick_repeater_.Poll(gamepad.left_x, gamepad.left_y, dt, dx, dy);
            if (dx != 0 || dy != 0) {
                std::vector<Point> door_centers;
                for (int i = 0; i < zone_count_; ++i) {
                    const auto& z = zone_rects_[i];
                    door_centers.push_back({static_cast<int16_t>((z.x1 + z.x2) / 2),
                                            static_cast<int16_t>((z.y1 + z.y2) / 2)});
                }
                focus_ = FindNextFocus(door_centers, focus_, dx, dy);
            }
            if (gamepad.pressed[static_cast<size_t>(GamepadButton::A)]) {
                any_click = true;
            }
        }

        if (entry_audio_done_time_ == Clock::time_point::min() &&
            (entry_wav_ == 0 || !audio_.isPlaying(entry_wav_))) {
            entry_audio_done_time_ = Clock::now();
        }
        const bool entry_done = entry_audio_done_time_ != Clock::time_point::min();
        const bool timed_out = entry_done && std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 Clock::now() - entry_audio_done_time_)
                                                     .count() >= 4000;

        if (any_click || timed_out) {
            if (any_click) {
                if (entry_wav_) audio_.stop(entry_wav_);
                creak_wav_ = playWav("W10\\DR_OPEN.WAV", AudioFlags::Async);
                phase_ = 1;
            } else {
                // Flat weighted pick, equal weight across all 4 loss magnitudes.
                const int kDoorsTimeoutIds[4] = {-12, -10, -8, -6};
                BoardGameScreen::WeightedPickEntry doors_entries[4];
                int doors_n = 0;
                BoardGameScreen::AppendWeightedCategory(kDoorsTimeoutIds, 4, 0.0f, 4, doors_entries,
                                                        doors_n);
                const int loss = -board_.PickWeightedEntry(doors_entries, doors_n);
                board_.SubtractPower(player_, loss);
                char doors_plaque_name[16];
                std::snprintf(doors_plaque_name, sizeof(doors_plaque_name), "FATE%u_%02d",
                              board_.render_state_[player_].province, loss);
                ReleaseTexture(plaque_tex_);
                plaque_tex_ = assets_.texture("FF1", doors_plaque_name);
                static const char* kDoorsTimeoutWav[2] = {"W1\\AT_0063.WAV", "W5\\AT_0537.WAV"};
                result_wav_ =
                    playWav(kDoorsTimeoutWav[RollIndex(board_.RngNext(), 2)], AudioFlags::Async);
                phase_ = 2;
            }
        }
    } else if (phase_ == 1) {
        if (creak_wav_ == 0 || !audio_.isPlaying(creak_wav_)) {
            // 8-entry weighted table, flat weight across magnitude {12,10,8,6}
            // either sign — reached only via a real click, never a timeout.
            const int kDoorsClickIds[8] = {-12, -10, -8, -6, 6, 8, 10, 12};
            BoardGameScreen::WeightedPickEntry doors_click_entries[8];
            int doors_click_n = 0;
            BoardGameScreen::AppendWeightedCategory(kDoorsClickIds, 8, 0.0f, 8, doors_click_entries,
                                                    doors_click_n);
            const int delta = board_.PickWeightedEntry(doors_click_entries, doors_click_n);
            if (delta < 0)
                board_.SubtractPower(player_, -delta);
            else
                board_.AddPower(player_, delta);
            const uint16_t doors_prov = board_.render_state_[player_].province;
            const int mag = delta < 0 ? -delta : delta;
            char doors_plaque_name[16];
            std::snprintf(doors_plaque_name, sizeof(doors_plaque_name),
                          delta > 0 ? "FORT%u_%02d" : "FATE%u_%02d", doors_prov, mag);
            ReleaseTexture(plaque_tex_);
            plaque_tex_ = assets_.texture(delta > 0 ? "FF2" : "FF1", doors_plaque_name);
            if (delta > 0) {
                static const char* kDoorsGainWav[3] = {"W1\\AT_0100.WAV", "W2\\AT_0183.WAV",
                                                       "W2\\AT_0184.WAV"};
                result_wav_ =
                    playWav(kDoorsGainWav[RollIndex(board_.RngNext(), 3)], AudioFlags::Async);
            } else {
                static const char* kDoorsLossWav[2] = {"W2\\AT_0185.WAV", "W2\\AT_0186.WAV"};
                result_wav_ =
                    playWav(kDoorsLossWav[RollIndex(board_.RngNext(), 2)], AudioFlags::Async);
            }
            phase_ = 2;
        }
    } else if (phase_ == 2) {
        if (result_wav_ == 0 || !audio_.isPlaying(result_wav_)) {
            board_.current_state_ = BoardGameState::TurnEnd;
            board_.state_start_time_ = Clock::now();
            board_.state_action_executed_ = false;
        }
    }
}

void DoorsGame::render() {
    // Show G1/DOORS{3,6,10}.BMP full-screen; the result plaque below overlays
    // it once resolved.
    if (door_tex_) {
        renderer_.drawBitmap(door_tex_, 0, 0, 0, 0, 640, 480);
    } else {
        renderer_.fillRect(0, 0, 640, 480, 10, 8, 20);  // dark background
    }

    // Result overlay — real FF1/FF2 province+amount plaque, same asset family as
    // stone types 3/4. Appears only once the outcome has resolved (phase_==2),
    // not during the preceding door-creak wait.
    if (phase_ == 2 && plaque_tex_) {
        renderer_.drawBitmap(plaque_tex_, 0, 0, 0, 0, 640, 480);
    }

    // Gamepad hint: any door opens the same (which door was hit is never stored, the
    // outcome is pure RNG regardless of pick) — zones carry no visible cue otherwise,
    // so a discrete focus cursor marks ONE door at a time (focus_, D-pad-navigable),
    // matching mouse's positional pick.
    if (board_.gamepad_mode_ && phase_ == 0 && focus_ >= 0 && focus_ < zone_count_) {
        const auto& zf = zone_rects_[focus_];
        DrawFocusCursor(renderer_, (zf.x1 + zf.x2) / 2, (zf.y1 + zf.y2) / 2);
    }
}

void DoorsGame::FreeAssets() {
    ReleaseTexture(door_tex_);
    ReleaseTexture(plaque_tex_);
}

}  // namespace openfear::game
