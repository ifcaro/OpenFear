#include "openfear/game/subscreens/OneDieGame.h"
#include "openfear/game/BoardGameScreen.h"
#include "openfear/game/GameState.h"
#include <chrono>

namespace openfear::game {

void OneDieGame::LoadAssets() {
    bg_tex_ = assets_.texture("G1", "DIEBA");
    diceblit_tex_ = assets_.texture("G1", "DICEBLIT");
    static const char* kDieValueWavs[6] = {"W8\\AT_0711.WAV", "W8\\AT_0712.WAV", "W8\\AT_0713.WAV",
                                           "W8\\AT_0714.WAV", "W8\\AT_0715.WAV", "W8\\AT_0716.WAV"};
    for (int i = 0; i < 6; ++i) value_wavs_[i] = preloadWav(kDieValueWavs[i]);
}

void OneDieGame::FreeAssets() {
    if (bg_tex_ == 0) return;
    renderer_.destroyTexture(bg_tex_);
    bg_tex_ = 0;
    renderer_.destroyTexture(diceblit_tex_);
    diceblit_tex_ = 0;
    ReleaseTexture(prize_tex_);
    for (auto& h : value_wavs_) {
        audio_.unload(h);
        h = 0;
    }
}

void OneDieGame::Enter(int player) {
    // Die face and prize are two independent rolls; prize comes from a time-tiered
    // table, not a power-weighted bucket. Skipped if only 1 active player.
    if (board_.CountActivePlayers() <= 1) {
        return;
    }
    LoadAssets();
    player_ = player;
    result_ = -1;
    anim_ = 0;
    clicked_ = false;
    // Outcome codes: 0=+10, 1=+30, 2=+50 power, 3=DecrementCurrentPlayerCyclic (next
    // player), 4=key gain. Flat weight per outcome within 3 time regions, picked via
    // 2 independent remaining-time reads against the same threshold (real double clock
    // read, not cached): region A (first third elapsed) power+turn-pass (>1 players
    // only); region B (rest of game) power only; region C — only when both reads tie
    // exactly at the threshold — adds key-gain when held keys < 6.
    {
        GameState* game_state = GameStateManager::Instance().Get();
        const long long duration =
            game_state ? static_cast<long long>(game_state->game_duration_s_) : 0;
        using namespace std::chrono;
        const long long threshold = (duration / 3) * 2;
        auto remaining_now = [&]() {
            return duration -
                   duration_cast<milliseconds>(steady_clock::now() - board_.game_start_time_)
                           .count() /
                       1000;
        };
        const long long remaining1 = remaining_now();

        BoardGameScreen::WeightedPickEntry entries[3];
        int entry_count = 0;

        if (remaining1 > threshold) {
            const bool turn_pass = board_.total_players_ > 1;
            const int capacity = turn_pass ? 3 : 2;
            const int power_ids[2] = {0, 1};  // +10, +30
            BoardGameScreen::AppendWeightedCategory(power_ids, 2, 0.0f, capacity, entries,
                                                    entry_count);
            if (turn_pass) {
                const int slot_ids[1] = {3};  // turn-pass
                BoardGameScreen::AppendWeightedCategory(slot_ids, 1, 0.0f, capacity, entries,
                                                        entry_count);
            }
        } else if (remaining_now() < threshold) {
            const int power_ids[3] = {0, 1, 2};  // +10, +30, +50
            BoardGameScreen::AppendWeightedCategory(power_ids, 3, 0.0f, 3, entries, entry_count);
        } else {
            const bool key_eligible = board_.CountHeldKeys(player) < 6;
            const int capacity = key_eligible ? 3 : 2;
            if (key_eligible) {
                const int key_ids[1] = {4};
                BoardGameScreen::AppendWeightedCategory(key_ids, 1, 0.0f, capacity, entries,
                                                        entry_count);
            }
            const int power_ids[2] = {1, 2};  // +30, +50
            BoardGameScreen::AppendWeightedCategory(power_ids, 2, 0.0f, capacity, entries,
                                                    entry_count);
        }

        outcome_ = board_.PickWeightedEntry(entries, entry_count);
    }
    // The one prize-preview background matching outcome_ loads, on demand. Key-gain's
    // bitmap depends on which province's key is unheld (same selection GainKey() uses
    // at grant time) — a literal per-province table, not a clean formula (same pattern
    // as FortuneCard's kGainKeyFolder/kGainKeyPlaque).
    ReleaseTexture(prize_tex_);
    switch (outcome_) {
        case 0:
            prize_tex_ = assets_.texture("G1", "DIEBAC12");  // +10
            break;
        case 1:
            prize_tex_ = assets_.texture("G1", "DIEBAC13");  // +30
            break;
        case 2:
            prize_tex_ = assets_.texture("G1", "DIEBAC14");  // +50
            break;
        case 3:
            prize_tex_ = assets_.texture("G1", "DIEBAC15");  // turn-pass
            break;
        case 4: {
            static const char* kDieKeyPrizeBg[6] = {"DIEBAC10", "DIEBACK9", "DIEBACK8",
                                                    "DIEBAC11", "DIEBACK7", "DIEBACK6"};
            const int prov = board_.FindUnheldProvince(player_);
            prize_tex_ = assets_.texture("G1", kDieKeyPrizeBg[(prov >= 0 && prov < 6) ? prov : 0]);
            break;
        }
        default:
            break;
    }
    board_.video_.close();
    board_.video_loaded_ = false;
    phase_ = Phase::Rolling;
    board_.current_state_ = BoardGameState::OneDieGame;
    board_.state_start_time_ = std::chrono::steady_clock::now();
    board_.state_action_executed_ = false;
}

void OneDieGame::update(const IInput& input) {
    if (phase_ == Phase::ShowingResult) {
        // Teardown/advance is gated on the win/miss taunt audio finishing
        // naturally, not a fixed timer — input isn't read at all in this phase,
        // so a click can't skip it either.
        const bool advance_result = taunt_wav_ == 0 || !audio_.isPlaying(taunt_wav_);
        if (advance_result) {
            // Reset here (not on animation completion) — kept valid through
            // the result display so the prize-preview background stays on screen too.
            outcome_ = -1;
            rolling_started_ = false;
            taunt_wav_ = 0;
            board_.current_state_ = BoardGameState::TurnEnd;
            board_.state_start_time_ = std::chrono::steady_clock::now();
            board_.state_action_executed_ = false;
        }
        return;
    }

    // Animation counter: 0->25 at ~80ms/tick (~2s total); click skips to 25 if anim >= 2.
    // The 2-phase waiting/rolling flow is documented on voice_face_/result_ in the
    // header. At animation end, the prize only grants if the two rolls MATCH — most
    // rolls (~5/6) are a miss.
    if (!board_.state_action_executed_) {
        board_.state_action_executed_ = true;
        anim_ = 0;
        clicked_ = false;
        result_ = -1;

        rolling_started_ = false;
        voice_face_ = RollIndex(board_.RngNext(), 6);
        if (voice_face_ >= 0 && voice_face_ < 6 && value_wavs_[voice_face_] != 0) {
            audio_.playLoaded(value_wavs_[voice_face_], AudioFlags::Async);
        }
    }

    // Waiting phase. A real click is NOT required — voice_done going true on its
    // own feeds the same condition as start_click below. Click just skips the wait.
    if (!rolling_started_) {
        const bool gp_start =
            board_.gamepad_mode_ && input.gamepad().pressed[static_cast<size_t>(GamepadButton::A)];
        const bool start_click = input.mouse().clicked[static_cast<int>(MouseButton::Left)] ||
                                 input.mouse().clicked[static_cast<int>(MouseButton::Right)] ||
                                 gp_start;
        const bool voice_done =
            (voice_face_ < 0 || voice_face_ >= 6 || value_wavs_[voice_face_] == 0 ||
             !audio_.isPlaying(value_wavs_[voice_face_]));
        if (start_click || voice_done) {
            rolling_started_ = true;
            ReleaseTexture(prize_tex_);  // never shown again past this point
            result_ = RollIndex(board_.RngNext(), 6);
            anim_ = 0;
            clicked_ = false;
            tick_time_ = std::chrono::steady_clock::now();
            if (board_.rolling_wav_) audio_.stop(board_.rolling_wav_);
            if (board_.rolling_wav_ != 0) {
                audio_.playLoaded(board_.rolling_wav_, AudioFlags::Async | AudioFlags::Loop);
            }
        }
        return;
    }

    const bool clicked_now =
        input.mouse().clicked[static_cast<int>(MouseButton::Left)] ||
        input.mouse().clicked[static_cast<int>(MouseButton::Right)] ||
        (board_.gamepad_mode_ && input.gamepad().pressed[static_cast<size_t>(GamepadButton::A)]);
    if (clicked_now && !clicked_) {
        clicked_ = true;
        if (anim_ >= 2) {
            anim_ = 25;
        }
    }

    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();
    const long long elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - tick_time_).count();
    if (elapsed_ms >= 80 || anim_ == 25) {
        tick_time_ = now;
        ++anim_;

        if (anim_ > 25) {
            if (board_.rolling_wav_) audio_.stop(board_.rolling_wav_);
            if (board_.rolled_wav_ != 0) {
                audio_.playLoaded(board_.rolled_wav_, AudioFlags::Async);
            }
            const bool matched = (result_ == voice_face_);
            if (matched) {
                // Turn-pass (id 3) gets a single fixed clip. Power ids (0/1/2) share the
                // 5-clip pool; key-gain (id 4) reuses that same pool — its own real
                // audio-pool selection wasn't pinned down this session.
                static const char* kDiePowerWinWav[5] = {"W3\\AT_0428.WAV", "W4\\AT_0505.WAV",
                                                         "W2\\AT_0319.WAV", "W2\\AT_0322.WAV",
                                                         "W4\\AT_0480.WAV"};
                switch (outcome_) {
                    case 0:
                        board_.AddPower(player_, 10);
                        taunt_wav_ = playWav(kDiePowerWinWav[RollIndex(board_.RngNext(), 5)],
                                             AudioFlags::Async);
                        break;
                    case 1:
                        board_.AddPower(player_, 30);
                        taunt_wav_ = playWav(kDiePowerWinWav[RollIndex(board_.RngNext(), 5)],
                                             AudioFlags::Async);
                        break;
                    case 2:
                        board_.AddPower(player_, 50);
                        taunt_wav_ = playWav(kDiePowerWinWav[RollIndex(board_.RngNext(), 5)],
                                             AudioFlags::Async);
                        break;
                    case 4: {
                        const int prov = board_.FindUnheldProvince(player_);
                        if (prov >= 0) board_.GainKey(player_, prov);
                        taunt_wav_ = playWav(kDiePowerWinWav[RollIndex(board_.RngNext(), 5)],
                                             AudioFlags::Async);
                        break;
                    }
                    default:
                        board_.DecrementCurrentPlayerCyclic();
                        taunt_wav_ = playWav("W5\\AT_0575.WAV", AudioFlags::Async);
                        break;
                }
            } else {
                static const char* kDieMissWav[4] = {"W3\\AT_0459.WAV", "W9\\AT_1000.WAV",
                                                     "W2\\AT_0333.WAV", "W3\\AT_0420.WAV"};
                taunt_wav_ =
                    playWav(kDieMissWav[RollIndex(board_.RngNext(), 4)], AudioFlags::Async);
            }

            phase_ = Phase::ShowingResult;
            board_.state_start_time_ = std::chrono::steady_clock::now();
            board_.state_action_executed_ = false;
        }
    }
}

void OneDieGame::render() {
    // Background swaps in the prize-preview art only during the waiting
    // phase (before rolling starts), then back to plain DIEBA the instant
    // rolling_started_ flips true in update().
    TextureHandle bg = bg_tex_;
    if (!rolling_started_ && prize_tex_ != 0) {
        bg = prize_tex_;
    }
    if (bg) {
        renderer_.drawBitmap(bg, 0, 0, 0, 0, 640, 480);
    } else {
        renderer_.fillRect(0, 0, 640, 480, 20, 10, 30);
    }

    // Die face: cycle blurred faces during animation; show sharp result face at rest.
    const int die_x = 263;
    const int die_y = 184;
    // DICEBLIT.BMP layout: 6 columns x 2 rows, 128px stride per cell.
    constexpr int kDieCellStride = 128;
    constexpr int kDieCellOffX = 4;
    constexpr int kDieCellOffY = 6;
    constexpr int kDieCellW = 120;
    constexpr int kDieCellH = 118;
    int col = 0, row = 0;
    if (phase_ == Phase::Rolling) {
        col = anim_ % 6;
        row = 1;
    } else {
        col = (result_ >= 0 && result_ < 6) ? result_ : 0;
        row = 0;
    }
    // Waiting phase (prize+voice shown, not rolling yet): no die on screen at all.
    if (diceblit_tex_ && rolling_started_) {
        renderer_.drawBitmap(diceblit_tex_, die_x, die_y, col * kDieCellStride + kDieCellOffX,
                             row * kDieCellStride + kDieCellOffY, kDieCellW, kDieCellH);
    }
}

}  // namespace openfear::game
