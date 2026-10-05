#include "openfear/game/subscreens/CoffinChoiceGame.h"
#include "openfear/game/BoardGameScreen.h"
#include "openfear/game/GameState.h"
#include <chrono>
#include <cstdio>
#include <vector>

namespace openfear::game {

void CoffinChoiceGame::LoadPolygon() {
    poly_base_ = {{163, 64}, {277, 64}, {305, 168}, {261, 415}, {168, 415}, {130, 168}};
    constexpr int16_t kCoffinXOffset = 218;
    poly_translated_.reserve(poly_base_.size());
    for (const auto& v : poly_base_) {
        poly_translated_.push_back({static_cast<int16_t>(v.x + kCoffinXOffset), v.y});
    }
}

bool CoffinChoiceGame::LoadZoneRects() {
    auto rects = assets_.rect_file("powcoff");
    if (!rects || rects->size() < 2) {
        std::fprintf(stderr, "[CoffinChoice] ERROR: Failed to load POWCOFF.BIN or insufficient "
                             "rects\n");
        return false;
    }
    known_zone_ = (*rects)[0];
    coffin_zone_ = (*rects)[1];
    return true;
}

void CoffinChoiceGame::FreeAssets() {
    ReleaseTexture(tex_);
}

void CoffinChoiceGame::Enter(int player) {
    // Unlike OneDieGame/DiceKeysGame, the weighted-table roll is NOT precomputed
    // here — it happens later, in update()'s resolved-branch, at click/timeout time.
    player_ = player;
    clicked_ = false;
    reveal_phase_ = false;
    chose_known_ = false;
    picked_id_ = 0;
    timeout_wav_ = 0;
    awaiting_timeout_wav_ = false;
    // tex_/focus_known_ set directly here, not in update()'s init block —
    // render() runs before that block on this first frame and would draw the
    // empty fallback / stale focus side otherwise.
    focus_known_ = true;
    ReleaseTexture(tex_);
    tex_ = assets_.texture("G4", "COF_19");
    entry_wav_ = 0;
    board_.video_.close();
    board_.video_loaded_ = false;
    board_.current_state_ = BoardGameState::CoffinChoiceGame;
    board_.state_start_time_ = std::chrono::steady_clock::now();
    board_.state_action_executed_ = false;
}

void CoffinChoiceGame::update(const IInput& input, float dt) {
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();
    const auto& mouse = input.mouse();
    const bool left_click = mouse.clicked[static_cast<int>(MouseButton::Left)];
    const bool right_click = mouse.clicked[static_cast<int>(MouseButton::Right)];

    if (!board_.state_action_executed_) {
        board_.state_action_executed_ = true;
        // Entry audio: a 3-clip pool, independent of the resolve-time (post-click)
        // outcome audio.
        static const char* kCoffinEntryWav[3] = {"W5\\AT_0531.WAV", "W5\\AT_0532.WAV",
                                                 "W6\\AT_0617.WAV"};
        entry_wav_ = playWav(kCoffinEntryWav[RollIndex(board_.RngNext(), 3)], AudioFlags::Async);
        entry_audio_done_time_ = std::chrono::steady_clock::time_point::min();
    }

    if (!reveal_phase_) {
        // Timeout: a 3000ms grace timer starts once ENTRY audio finishes on its
        // own, not from when the coffin opened.
        const bool entry_audio_playing = entry_wav_ != 0 && audio_.isPlaying(entry_wav_);
        if (!entry_audio_playing &&
            entry_audio_done_time_ == std::chrono::steady_clock::time_point::min()) {
            entry_audio_done_time_ = now;
        }
        bool timed_out = false;
        if (entry_audio_done_time_ != std::chrono::steady_clock::time_point::min()) {
            const auto grace_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - entry_audio_done_time_)
                    .count();
            timed_out = (grace_ms >= 3000);
        }
        // Gamepad: D-pad/stick toggles focus_known_ (default known/left), A
        // confirms. Polygon outline instead of a rect/tint since the real zones
        // are invisible hotspots, no button art to align to.
        bool gamepad_known = false, gamepad_coffin = false;
        if (board_.gamepad_mode_) {
            const auto& gamepad = input.gamepad();
            int dx = 0;
            if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadLeft)]) dx = -1;
            if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadRight)]) dx = 1;
            if (dx == 0)
                board_.stick_repeater_.PollHorizontal(gamepad.left_x, gamepad.left_y, dt, dx);
            if (dx != 0) focus_known_ = (dx < 0);
            if (gamepad.pressed[static_cast<size_t>(GamepadButton::A)]) {
                gamepad_known = focus_known_;
                gamepad_coffin = !focus_known_;
            }
        }
        const bool clicked =
            (left_click || right_click || gamepad_known || gamepad_coffin) && !clicked_;
        if (clicked) clicked_ = true;
        if (!left_click && !right_click && !gamepad_known && !gamepad_coffin) clicked_ = false;

        bool resolved = false;
        bool did_click = false;  // a real click happened (either zone)
        if (awaiting_timeout_wav_) {
            // Resolution withheld (coffin stays closed) until the timeout
            // announcement finishes playing.
            const bool timeout_wav_done = (timeout_wav_ == 0) || !audio_.isPlaying(timeout_wav_);
            if (timeout_wav_done) {
                awaiting_timeout_wav_ = false;
                did_click = false;
                resolved = true;
            }
        } else if (timed_out) {
            if (entry_wav_ != 0) {
                audio_.stop(entry_wav_);
                entry_wav_ = 0;
            }
            chose_known_ = false;
            static const char* kCoffinTimeoutWav[2] = {"W1\\AT_0063.WAV", "W5\\AT_0537.WAV"};
            timeout_wav_ =
                playWav(kCoffinTimeoutWav[RollIndex(board_.RngNext(), 2)], AudioFlags::Async);
            awaiting_timeout_wav_ = true;
        } else if (clicked) {
            chose_known_ =
                gamepad_coffin
                    ? false
                    : (gamepad_known ? true : PointInRect(mouse.x, mouse.y, known_zone_));
            did_click = true;
            resolved = true;
            // The guaranteed +10 applies immediately, BEFORE the table roll, only
            // for the "known" zone.
            if (chose_known_) board_.AddPower(player_, 10);
        }

        if (resolved) {
            if (entry_wav_ != 0) {
                audio_.stop(entry_wav_);
                entry_wav_ = 0;
            }
            // Flat weighted pick per category (key/power/slot), except the non-catchup
            // power category (literal ratio, see kCoffinPowerCatchupWeights below).
            // Solo games drop the slot category's skip-turn entry — capacity is sized
            // per call to match, not fixed to a nominal max.
            const int held_keys = board_.CountHeldKeys(player_);
            GameState* game_state = GameStateManager::Instance().Get();
            const long long total_duration_s =
                game_state ? static_cast<long long>(game_state->game_duration_s_) : 0;
            const long long elapsed_s =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - board_.game_start_time_)
                    .count() /
                1000;
            const long long remaining_s = total_duration_s - elapsed_s;
            const int slot_count = (board_.total_players_ == 1) ? 1 : 2;

            BoardGameScreen::WeightedPickEntry entries[8];
            int n = 0;

            auto add_flat_cat = [&](const int ids[], int count, int capacity) {
                BoardGameScreen::AppendWeightedCategory(ids, count, 0.0f, capacity, entries, n);
            };
            auto add_slot_cat_filtered = [&](const int ids_in[], const int specials[], int count,
                                             int capacity) {
                int ids[2];
                int m = 0;
                for (int i = 0; i < count; ++i) {
                    if (specials[i] == kSlotSpecialSkipTurn && board_.total_players_ == 1) continue;
                    ids[m++] = ids_in[i];
                }
                if (m > 0)
                    BoardGameScreen::AppendWeightedCategory(ids, m, 0.0f, capacity, entries, n);
            };

            if (!did_click) {
                // TIMEOUT branch: kCoffinKeyLoss, kCoffinPowerLoss10, kCoffinPower1,
                // kCoffinGateOpen, kCoffinSkipTurn (skip-turn dropped solo).
                const int capacity = 3 + slot_count;
                const int key_ids[1] = {kCoffinKeyLoss};
                add_flat_cat(key_ids, 1, capacity);
                const int power_ids[2] = {kCoffinPowerLoss10, kCoffinPower1};
                add_flat_cat(power_ids, 2, capacity);
                const int slot_ids[2] = {kCoffinGateOpen, kCoffinSkipTurn};
                const int slot_specials[2] = {kSlotSpecialGateOpen, kSlotSpecialSkipTurn};
                add_slot_cat_filtered(slot_ids, slot_specials, 2, capacity);
            } else {
                // Forces the key stake once real remaining time drops below 10
                // minutes, same as Blackjack's own catch-up check — but without
                // Blackjack's extra held-keys-in-[1,5] gate around it.
                const bool catchup = remaining_s < 600 && held_keys < 3;

                if (catchup) {
                    // 3-entry table, capacity=3: kCoffinKeyGain, kCoffinPower1,
                    // kCoffinPower30.
                    const int key_ids[1] = {kCoffinKeyGain};
                    add_flat_cat(key_ids, 1, 3);
                    const int power_ids[2] = {kCoffinPower1, kCoffinPower30};
                    add_flat_cat(power_ids, 2, 3);
                } else {
                    // 8-entry table: kCoffinKeyLoss, kCoffinKeyGain, kCoffinPowerLoss10,
                    // kCoffinPower1, kCoffinPower30, kCoffinPower50, kCoffinGateOpen,
                    // kCoffinSkipTurn (skip-turn dropped solo). The power category's
                    // real weights are the literal ratio [10,10,10,5], not flat.
                    const int capacity = 6 + slot_count;
                    const int key_ids[2] = {kCoffinKeyLoss, kCoffinKeyGain};
                    add_flat_cat(key_ids, 2, capacity);
                    const int power_ids[4] = {kCoffinPowerLoss10, kCoffinPower1, kCoffinPower30,
                                              kCoffinPower50};
                    const float kCoffinPowerRichTableWeights[4] = {10.0f, 10.0f, 10.0f, 5.0f};
                    BoardGameScreen::AppendLiteralWeightCategory(
                        power_ids, kCoffinPowerRichTableWeights, 4, capacity, entries, n);
                    const int slot_ids[2] = {kCoffinGateOpen, kCoffinSkipTurn};
                    const int slot_specials[2] = {kSlotSpecialGateOpen, kSlotSpecialSkipTurn};
                    add_slot_cat_filtered(slot_ids, slot_specials, 2, capacity);
                }
            }

            picked_id_ = (n > 0) ? board_.PickWeightedEntry(entries, n) : 0;

            // Province shown in the bitmap uses the SAME rotation-order lookup as the
            // real GainKey/LoseKey call — NOT the player_'s current board province
            // (the key can be for ANY unheld/held province in rotation order).
            int coffin_key_prov = -1;
            if (chose_known_) {
                // NEVER applies the table entry's mechanical effect here (GainKey/
                // LoseKey/AddPower/skip-turn/GateOpen) — the rotation lookup only
                // picks which key-color bitmap to show for what was dodged.
                if (picked_id_ == kCoffinKeyGain) {
                    const int prov = board_.FindUnheldProvince(player_);
                    if (prov >= 0) coffin_key_prov = prov;
                } else if (picked_id_ == kCoffinKeyLoss) {
                    const int prov = board_.FindHeldProvinceByRotation(player_);
                    if (prov >= 0) coffin_key_prov = prov;
                }
                // Result audio depends on the full hidden roll, ids 1-8 (not just
                // kCoffinKeyGain/kCoffinKeyLoss like the lookup above): ids 1-3 (all good
                // outcomes) taunt you for playing it safe; ids 4-8 (minor/negative) play the
                // mirror-opposite taunt instead.
                if (picked_id_ < kCoffinPower1) {
                    static const char* kCoffinKnownLowWav[3] = {
                        "W8\\AT_0708.WAV", "W8\\AT_0709.WAV", "W8\\AT_0710.WAV"};
                    result_wav_ = playWav(kCoffinKnownLowWav[RollIndex(board_.RngNext(), 3)],
                                          AudioFlags::Async);
                } else {
                    static const char* kCoffinKnownHighWav[2] = {"W8\\AT_0705.WAV",
                                                                 "W8\\AT_0707.WAV"};
                    result_wav_ = playWav(kCoffinKnownHighWav[RollIndex(board_.RngNext(), 2)],
                                          AudioFlags::Async);
                }
            } else {
                static const char* kCoffinPowerWav[3] = {"W1\\AT_0100.WAV", "W2\\AT_0184.WAV",
                                                         "W2\\AT_0225.WAV"};
                static const char* kCoffinMinorWav[3] = {"W1\\AT_0101.WAV", "W2\\AT_0224.WAV",
                                                         "W2\\AT_0326.WAV"};
                static const char* kCoffinSkipTurnWav[6] = {"W2\\AT_0271.WAV", "W5\\AT_0570.WAV",
                                                            "W5\\AT_0572.WAV", "W5\\AT_0573.WAV",
                                                            "W5\\AT_0574.WAV", "W5\\AT_0578.WAV"};
                static const char* kCoffinGateOpenWav[7] = {
                    "W5\\AT_0553.WAV", "W5\\AT_0554.WAV", "W5\\AT_0556.WAV", "W5\\AT_0557.WAV",
                    "W5\\AT_0559.WAV", "W5\\AT_0560.WAV", "W2\\AT_0303.WAV"};
                switch (picked_id_) {
                    case kCoffinPower30:
                    case kCoffinPower50:
                        result_wav_ = playWav(kCoffinPowerWav[RollIndex(board_.RngNext(), 3)],
                                              AudioFlags::Async);
                        break;
                    case kCoffinKeyGain:
                        result_wav_ = playWav("W6\\AT_0591.WAV", AudioFlags::Async);
                        break;
                    case kCoffinPower1:
                    case kCoffinPowerLoss10:
                        result_wav_ = playWav(kCoffinMinorWav[RollIndex(board_.RngNext(), 3)],
                                              AudioFlags::Async);
                        break;
                    case kCoffinSkipTurn:
                        result_wav_ = playWav(kCoffinSkipTurnWav[RollIndex(board_.RngNext(), 6)],
                                              AudioFlags::Async);
                        break;
                    case kCoffinKeyLoss:
                        result_wav_ = playWav("W6\\AT_0593.WAV", AudioFlags::Async);
                        break;
                    case kCoffinGateOpen:
                        result_wav_ = playWav(kCoffinGateOpenWav[RollIndex(board_.RngNext(), 7)],
                                              AudioFlags::Async);
                        break;
                }
                // Mechanical effect of the picked id — unlike the audio switch
                // above, this one runs unconditionally.
                switch (picked_id_) {
                    case kCoffinPower30:
                        board_.AddPower(player_, 30);
                        break;
                    case kCoffinPower50:
                        board_.AddPower(player_, 50);
                        break;
                    case kCoffinKeyGain: {
                        const int prov = board_.FindUnheldProvince(player_);
                        if (prov >= 0) {
                            board_.GainKey(player_, prov);
                            coffin_key_prov = prov;
                        }
                        break;
                    }
                    case kCoffinPower1:
                        board_.AddPower(player_, 1);
                        break;
                    case kCoffinKeyLoss: {
                        const int prov = board_.FindHeldProvinceByRotation(player_);
                        if (prov >= 0) {
                            board_.LoseKey(player_, prov);
                            coffin_key_prov = prov;
                        }
                        break;
                    }
                    case kCoffinPowerLoss10:
                        board_.SubtractPower(player_, 10);
                        break;
                    case kCoffinSkipTurn:
                        board_.render_state_[player_].skip_turn_counter = 1;
                        break;
                    case kCoffinGateOpen:
                        // Same mechanism as ResolveFateCard's own GateOpen variant
                        // and DispatchBlackHoleTeleport (enc type 3).
                        board_.TeleportToNearestBlackHole(player_);
                        break;
                    default:
                        break;
                }
            }

            // 8-way jump on picked_id_, independent of which zone was chosen
            // (known/coffin).
            {
                const int key_prov = (coffin_key_prov >= 0) ? coffin_key_prov : 0;
                int cof_num = 0;  // G4/COF_%02d.BMP (cof_num), 1-indexed
                switch (picked_id_) {
                    case kCoffinPower30:
                        cof_num = 1;
                        break;
                    case kCoffinPower50:
                        cof_num = 2;
                        break;
                    case kCoffinKeyGain:
                        cof_num = 3 + key_prov;
                        break;
                    case kCoffinPower1:
                        cof_num = 9;
                        break;
                    case kCoffinSkipTurn:
                        cof_num = 10;
                        break;
                    case kCoffinKeyLoss:
                        cof_num = 11 + key_prov;
                        break;
                    case kCoffinPowerLoss10:
                        cof_num = 17;
                        break;
                    case kCoffinGateOpen:
                        cof_num = 18;
                        break;
                }
                ReleaseTexture(tex_);
                if (cof_num > 0) {
                    char cof_name[8];
                    std::snprintf(cof_name, sizeof(cof_name), "COF_%02d", cof_num);
                    tex_ = assets_.texture("G4", cof_name);
                }
            }
            reveal_phase_ = true;
            board_.state_start_time_ = now;
        }
    } else {
        // Real reveal-end trigger is audio-done, not a fixed timer — same pattern
        // as the FateCard/FortuneCard overlay teardown.
        const bool audio_done = (result_wav_ == 0) || !audio_.isPlaying(result_wav_);
        if (audio_done && !board_.BathoryKeyRedirectAudioBusy()) {
            board_.current_state_ = BoardGameState::TurnEnd;
            board_.state_start_time_ = Clock::now();
            board_.state_action_executed_ = false;
        }
    }
}

void CoffinChoiceGame::DrawCoffinFocus(const std::vector<Point>& verts) {
    DrawFocusPolygonFill(renderer_, verts);
    int cx = 0, cy = 0;
    for (const auto& v : verts) {
        cx += v.x;
        cy += v.y;
    }
    DrawFocusCursor(renderer_, cx / static_cast<int>(verts.size()),
                    cy / static_cast<int>(verts.size()));
}

void CoffinChoiceGame::render() {
    if (tex_) {
        renderer_.drawBitmap(tex_, 0, 0, 0, 0, 640, 480);
    } else {
        renderer_.fillRect(0, 0, 640, 480, 5, 5, 15);
    }
    // Gamepad focus indicator. Both coffins are the SAME hexagon, just translated
    // 218px in x (real coffin silhouette: flat head edge, 2 angled sides down to
    // the shoulder points, 2 angled sides down to a flat foot edge — 6 verts).
    // Only shown before a choice resolves.
    if (board_.gamepad_mode_ && !reveal_phase_ && !clicked_ && !poly_base_.empty()) {
        if (focus_known_) {
            DrawCoffinFocus(poly_base_);
        } else {
            DrawCoffinFocus(poly_translated_);
        }
    }
}

}  // namespace openfear::game
