#include "openfear/game/BoardGameScreen.h"
#include "openfear/game/GameState.h"
#include "openfear/game/BoardInitializer.h"
#include "openfear/game/PlayerInitializer.h"
#include "openfear/game/PlayerObject.h"
#include "openfear/assets/AssetManager.h"
#include "openfear/assets/IAssetProvider.h"
#include "openfear/platform/BitmapFont.h"
#include "openfear/platform/Platform.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <random>
#include <vector>

namespace openfear::game {

// Key-rotation table (province offsets by char_id) - real definition lives in
// BoardGameScreen.cpp (shared with BoardGameScreenDuelAndKeys.cpp's
// FindUnheldProvince/FindHeldProvinceByRotation).
extern const int kKeyTable[6];

void BoardGameScreen::CheckGameTimerNearEnd() {
    if (!game_timer_started_ || timer_expired_ || near_end_flag_) return;

    GameState* gs_tick = GameStateManager::Instance().Get();
    if (!gs_tick || gs_tick->game_duration_s_ <= 0) return;

    using namespace std::chrono;
    const long long elapsed_s =
        duration_cast<seconds>(steady_clock::now() - game_start_time_).count();
    const long long remaining_s = static_cast<long long>(gs_tick->game_duration_s_) - elapsed_s;
    if (remaining_s < 15) {
        near_end_flag_ = true;
    }
}

void BoardGameScreen::CheckTurnTimeout() {
    if (turn_time_limit_ms_ == 0 || turn_timeout_triggered_) return;
    if (current_state_ != BoardGameState::RollingDice && current_state_ != BoardGameState::Moving &&
        current_state_ != BoardGameState::MinimapScreen &&
        current_state_ != BoardGameState::Encounter &&
        current_state_ != BoardGameState::OneDieGame &&
        current_state_ != BoardGameState::KeyAwardGame) {
        return;
    }

    using namespace std::chrono;
    const long long elapsed_ms =
        duration_cast<milliseconds>(steady_clock::now() - turn_start_time_).count();
    if (elapsed_ms < static_cast<long long>(turn_time_limit_ms_)) return;

    turn_timeout_triggered_ = true;

    // steps_remaining==0 at timeout -> normal completion, no revert/timeout audio.
    // Excluded: Encounter/OneDieGame/KeyAwardGame (already mid-arrival at 0, this
    // shortcut would re-trigger it) and RollingDice (steps_remaining carries over at
    // 0 from last turn, untouched until a die is picked — without this exclusion, a
    // pre-roll timeout would skip stopping rolling_wav_ and loop it forever).
    if (render_state_[current_player_].steps_remaining == 0 &&
        current_state_ != BoardGameState::RollingDice &&
        current_state_ != BoardGameState::Encounter &&
        current_state_ != BoardGameState::OneDieGame &&
        current_state_ != BoardGameState::KeyAwardGame) {
        ResolveStepArrival();
        return;
    }

    // Revert to turn-start position. The backup fields
    // (province_backup/stone_backup/direction_backup) are captured at
    // RollingDice entry.
    {
        auto& rs_revert = render_state_[current_player_];
        rs_revert.province = rs_revert.province_backup;
        rs_revert.stone_id = rs_revert.stone_backup;
        rs_revert.direction = rs_revert.direction_backup;
        GameState* gs_revert = GameStateManager::Instance().Get();
        if (gs_revert) {
            gs_revert->player_idx = rs_revert.province;
            gs_revert->sub_idx = rs_revert.stone_id;
        }
        // render()'s video-frame seek reads mapfile_direction_ only, never
        // render_state_'s own direction copy — restoring just rs_revert.direction
        // above isn't enough. A mid-roll direction change (junction rotate, portal
        // exit) followed by a timeout would leave the screen seeking with the
        // stale, pre-revert direction unless this is restored too.
        mapfile_direction_ = rs_revert.direction;
    }
    if (rolling_wav_) audio_.stop(rolling_wav_);
    if (timeout_wav_) audio_.playLoaded(timeout_wav_, AudioFlags::Async);
    current_state_ = BoardGameState::TurnTimeoutWait;
    state_start_time_ = std::chrono::steady_clock::now();
    state_action_executed_ = false;
}

bool BoardGameScreen::update(const IInput& input) {
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - cursor_state_.last_frame_time).count();
    cursor_state_.last_frame_time = now;

    UpdateCursorAnimation(dt);

    RefreshGamepadMode(input);

    const auto& keyboard = input.keyboard();
    if (keyboard.pressed[static_cast<int>(Key::Escape)]) {
        return true;
    }

    CheckGameTimerNearEnd();
    CheckTurnTimeout();

    // =========================================================================
    // STATE MACHINE
    // =========================================================================

    switch (current_state_) {
        case BoardGameState::ShowingPortrait:
            UpdateShowingPortrait(input);
            break;

        case BoardGameState::WaitingForPlayer: {
            if (!state_action_executed_) {
                state_action_executed_ = true;
                turn_timeout_triggered_ = false;  // reset for this new turn
                // Pre-initialize so the global timeout check never sees
                // an uninitialized value; overwritten precisely at RollingDice entry.
                turn_start_time_ = std::chrono::steady_clock::now();
            }

            // Brief pause before dice start spinning
            auto wait_elapsed =
                std::chrono::duration<float>(std::chrono::steady_clock::now() - state_start_time_)
                    .count();
            if (wait_elapsed >= 0.5f) {
                current_state_ = BoardGameState::RollingDice;
                state_start_time_ = std::chrono::steady_clock::now();
                state_action_executed_ = false;
                // Reset here, at the transition itself, so a stale render in between
                // never observes the previous roll's dice-option/anim-counter values
                // with the new state already set.
                dice_.selected_option = 0;
                dice_.anim_counter = 0;
            }
            break;
        }

        case BoardGameState::RollingDice:
            UpdateRollingDice(input, dt);
            break;

        case BoardGameState::Moving:
            UpdateMoving(input, dt);
            break;

        case BoardGameState::MinimapScreen:
            minimap_screen_.update(input);
            break;

        case BoardGameState::FlightMap:
            flight_map_screen_.update(input);
            break;

        case BoardGameState::HomeAVI: {
            // Same board-area rect as FULLMAP.AVI.
            if (!state_action_executed_) {
                const int char_id = render_state_[current_player_].char_id;
                const std::string home_avi =
                    AssetPaths::avi("AV2", std::string("HOME") + std::to_string(char_id)).string();
                video_.close();
                video_loaded_ = false;
                video_.setRenderRect(kBoardAreaX, kBoardAreaY, kBoardAreaW, kBoardAreaH);
                if (!video_.open(home_avi)) {
                    render_state_[current_player_].home_avi_played = 1;
                    ReopenFullmapAvi();
                    current_state_ = BoardGameState::WaitingForPlayer;
                    state_start_time_ = std::chrono::steady_clock::now();
                    state_action_executed_ = false;
                    break;
                }
                video_.play(true);
                state_action_executed_ = true;
            }

            // Skip on click. Gamepad: any button skips too.
            bool gp_skip_home = gamepad_mode_ && AnyPressed(input.gamepad());
            if (input.mouse().clicked[static_cast<int>(MouseButton::Left)] || gp_skip_home) {
                video_.close();
                video_loaded_ = false;
                render_state_[current_player_].home_avi_played = 1;
                ReopenFullmapAvi();
                current_state_ = BoardGameState::WaitingForPlayer;
                state_start_time_ = std::chrono::steady_clock::now();
                state_action_executed_ = false;
                break;
            }

            if (video_.status() != VideoStatus::Playing) {
                render_state_[current_player_].home_avi_played = 1;
                ReopenFullmapAvi();
                current_state_ = BoardGameState::WaitingForPlayer;
                state_start_time_ = std::chrono::steady_clock::now();
                state_action_executed_ = false;
            }
            break;
        }

        case BoardGameState::TurnTimeoutWait:
            // Waits for the timeout audio to finish before advancing.
            if (timeout_wav_ && audio_.isPlaying(timeout_wav_)) {
                // still playing — wait
            } else {
                current_state_ = BoardGameState::TurnEnd;
                state_start_time_ = std::chrono::steady_clock::now();
                state_action_executed_ = false;
            }
            break;

        case BoardGameState::TurnEnd: {
            // A FateCard/FortuneCard overlay, once shown, structurally blocks EndTurn()
            // until its own audio finishes — checked here in update() (not render()) so
            // the flag is already correct before this same frame's gate below runs,
            // avoiding a stray extra frame.
            if (card_active_ && card_tex_) {
                // If playWav() ever failed, card_wav_ is 0 and this is immediately true —
                // no separate timer/safety-cap needed on top of it.
                const bool card_audio_done = (card_wav_ == 0) || !audio_.isPlaying(card_wav_);
                // Keep the card on screen through the Bathory chime too — it fires
                // (if pending) only once card_audio_done, same gate as every other
                // GainKey() caller, so the card/overlay never disappears before it.
                if (card_audio_done && !BathoryKeyRedirectAudioBusy()) {
                    renderer_.destroyTexture(card_tex_);
                    card_tex_ = 0;
                    card_active_ = false;
                    // --test-stone-repeat: fire the next queued card now that the previous
                    // one's audio actually finished, instead of all of them in one frame.
                    if (debug_stone_repeat_remaining_ > 0) {
                        --debug_stone_repeat_remaining_;
                        if (debug_stone_repeat_type_ == 3)
                            ResolveFateCard(current_player_);
                        else
                            ResolveFortuneCard(current_player_);
                    }
                }
            }
            const bool bathory_audio_done =
                bathory_key_redirect_wav_ == 0 || !audio_.isPlaying(bathory_key_redirect_wav_);
            if (bathory_audio_done) bathory_key_redirect_wav_ = 0;
            if (!state_action_executed_ && !card_active_ && bathory_audio_done) {
                FreeLazyMinigameAssets();
                EndTurn();
                state_action_executed_ = true;

                if (current_state_ == BoardGameState::GameOver) {
                    break;  // skip timeout logic, go directly to GameOver handling
                }

                // Transition to BlackFlash immediately, same frame as EndTurn() — the
                // real game shows no pause with the board still visible between
                // turn-end cleanup and the next-turn audio.
                if (current_state_ == BoardGameState::TurnEnd) {
                    black_flash_next_state_ = BoardGameState::ShowingPortrait;
                    current_state_ = BoardGameState::BlackFlash;
                    state_start_time_ = std::chrono::steady_clock::now();
                    state_action_executed_ = false;
                }
            }
        } break;

        case BoardGameState::BlackFlash:
            // The black frame itself already rendered last frame — this just advances
            // past it on entry, one frame later, instead of lingering here.
            current_state_ = black_flash_next_state_;
            state_start_time_ = std::chrono::steady_clock::now();
            state_action_executed_ = false;
            break;

        case BoardGameState::GameOver:
            return true;

        case BoardGameState::PauseScreen:
            return pause_screen_.update(input, dt);

        // Board stays visible (falls through to the normal Moving-style draw, video_
        // untouched) for exactly one tick, then hands off to TeleportAVI's own
        // open+play path — whose blocking video_.open() call is what actually keeps
        // this frame on screen while the asset loads.
        case BoardGameState::TeleportVideoPreload:
            current_state_ = BoardGameState::TeleportAVI;
            state_start_time_ = std::chrono::steady_clock::now();
            state_action_executed_ = false;
            break;

        case BoardGameState::TeleportAVI:
            UpdateTeleportAVI();
            break;

        case BoardGameState::StoneArrivalAVI: {
            if (!state_action_executed_) {
                video_.close();
                video_loaded_ = false;
                video_.setRenderRect(kBoardAreaX, kBoardAreaY, kBoardAreaW, kBoardAreaH);
                if (!video_.open(stone_arrival_avi_path_)) {
                    ReopenFullmapAvi();
                    current_state_ = BoardGameState::TurnEnd;
                    state_start_time_ = std::chrono::steady_clock::now();
                    state_action_executed_ = false;
                    break;
                }
                video_.play(true);
                state_action_executed_ = true;
            }

            if (video_.status() != VideoStatus::Playing) {
                if (bathory_key_redirect_audio_pending_) {
                    StartBathoryKeyRedirectAudio();
                    break;
                }
                const bool bathory_audio_busy =
                    bathory_key_redirect_wav_ != 0 && audio_.isPlaying(bathory_key_redirect_wav_);
                if (!bathory_audio_busy) {
                    ReopenFullmapAvi();
                    current_state_ = BoardGameState::TurnEnd;
                    state_start_time_ = std::chrono::steady_clock::now();
                    state_action_executed_ = false;
                }
            }
            break;
        }

        case BoardGameState::GkLoopAVI: {
            // AVI already opened by whichever caller transitioned here (DispatchEncounter's
            // taunt helpers, black-hole retry resolution, the Chosen One roll, or KeyAward's
            // intro). Exit: when the audio is no longer playing.
            if (!state_action_executed_) {
                state_action_executed_ = true;
            }

            // Loop: if video has finished its one pass, restart it - play()
            // alone rewinds, no need to close/reopen the same stream.
            if (video_.status() != VideoStatus::Playing) {
                video_.play(true);
            }

            if (gkloop_enc_audio_ == 0 || !audio_.isPlaying(gkloop_enc_audio_)) {
                // KeyAward (gkloop_post_state_==KeyAwardGame): the real screen just
                // stays frozen on the eyeloop's last frame through the whole roll —
                // nothing reopens/redraws the fullmap background during it. Skip the
                // close+reopen for this case (and for GameOver, the game-timer-expiry
                // exit path — no point reopening the board right before the
                // post-game sequence takes over).
                if (gkloop_post_state_ != BoardGameState::KeyAwardGame &&
                    gkloop_post_state_ != BoardGameState::GameOver) {
                    video_.close();
                    video_loaded_ = false;
                    ReopenFullmapAvi();
                }
                current_state_ = gkloop_post_state_;
                state_start_time_ = std::chrono::steady_clock::now();
                state_action_executed_ = false;
            }
            break;
        }

        case BoardGameState::OneDieGame:
            onedie_screen_.update(input);
            break;

        case BoardGameState::KeyAwardGame:
            keyaward_screen_.update(input);
            break;

        case BoardGameState::DoorsGame:
            doors_screen_.update(input, dt);
            break;

        case BoardGameState::DiceKeysGame:
            dicekeys_screen_.update(input, dt);
            break;

        case BoardGameState::BlackjackGame:
            blackjack_screen_.update(input, dt);
            break;

        case BoardGameState::CoffinChoiceGame:
            coffin_screen_.update(input, dt);
            break;

        case BoardGameState::BribeGame:
            bribe_screen_.update(input, dt);
            break;

        case BoardGameState::CureCurseGame:
            curecurse_screen_.update(input, dt);
            break;

        case BoardGameState::SlotMachineGame:
            slot_screen_.update(input, dt);
            break;

        case BoardGameState::FortuneDoorGame:
            fortune_door_screen_.update(input, dt);
            break;

        case BoardGameState::DuelAttackGauge:
            duel_attack_gauge_.update(input);
            break;

        case BoardGameState::DuelChooseOpponent:
            return duel_choose_screen_.update(input, dt);

        case BoardGameState::DuelStealOrCurse:
            duel_steal_or_curse_screen_.update(input, dt);
            break;
    }

    return false;
}

void BoardGameScreen::UpdateShowingPortrait(const IInput& input) {
    // Portrait: INFO{char_id}_0.BMP. Audio: character theme WAV.
    // Duration: portrait stays open until the theme WAV finishes.
    // Clicking does NOT skip the portrait.
    if (!state_action_executed_) {
        EnterShowingPortrait();
    } else if (!portrait_.deferred_loaded) {
        LoadDeferredPortraitResources();
        portrait_.deferred_loaded = true;
    }

    const bool audio_done = ComputePortraitAudioDone();

    if (HandleDuelCornerClick(input)) return;
    if (HandlePauseCornerClick(input)) return;

    if (audio_done) {
        if (ResolveBlackHoleRetry()) return;
        DispatchTurnStartAfterPortrait();
    }
    return;
}

void BoardGameScreen::EnterShowingPortrait() {
    portrait_.deferred_loaded = false;

    ReleaseTexture(portrait_.tex);
    {
        const auto& portrait_rs = render_state_[current_player_];
        if (portrait_rs.curse_type == 1 && portrait_rs.char_id >= 0 && portrait_rs.char_id < 6) {
            // Gevaudan curse: portrait bitmap swaps to a 0-3 degradation
            // stage, capped at 3, driven by curse_step_counter.
            const int stage = std::min<int>(portrait_rs.curse_step_counter, 3);
            // folder_idx (0-23, char_id*4+stage) splits the 24 stage portraits across two
            // folders: the first 15 live in C1, the rest in C2.
            const int folder_idx = portrait_rs.char_id * 4 + stage;
            const char* folder = (folder_idx < 15) ? "C1" : "C2";
            char name[16];
            std::snprintf(name, sizeof(name), "MG%d_%d", portrait_rs.char_id, stage);
            portrait_.tex = assets_.texture(folder, name);
        } else if (portrait_rs.curse_type == 5 && portrait_rs.char_id >= 0 &&
                   portrait_rs.char_id < 6) {
            // Hellin curse: permanent alternate portrait, no animation stages.
            char broken_name[16];
            std::snprintf(broken_name, sizeof(broken_name), "BROKEN_%d", portrait_rs.char_id);
            portrait_.tex = assets_.texture("C1", broken_name);
        } else {
            portrait_.tex = assets_.portrait(portrait_rs.char_id);
        }
    }

    // Key-icon sheet — loads only the current player's own sheet, not all 6 up front.
    ReleaseTexture(portrait_.key_tex);
    {
        char key_name[16];
        std::snprintf(key_name, sizeof(key_name), "INFO%d_1",
                      render_state_[current_player_].char_id);
        portrait_.key_tex = assets_.portrait(key_name);
    }

    // Taken before the theme WAV preload below, matching SetupPortraitWindow's order
    // (DrawTimeRemainingDisplay before PlayPortraitAudio) — the WAV decode shouldn't count
    // against the displayed duration.
    if (!game_timer_started_) {
        game_start_time_ = std::chrono::steady_clock::now();
        game_timer_started_ = true;
    }

    {
        GameState* gs_p = GameStateManager::Instance().Get();
        long long total_s = gs_p ? static_cast<long long>(gs_p->game_duration_s_) : 0;
        using namespace std::chrono;
        long long elapsed_s =
            duration_cast<seconds>(steady_clock::now() - game_start_time_).count();
        long long remaining = total_s - elapsed_s;
        if (remaining < 1) remaining = 1;
        if (remaining >= 6000)
            std::snprintf(portrait_time_str_, sizeof(portrait_time_str_), "%02lld", remaining / 60);
        else
            std::snprintf(portrait_time_str_, sizeof(portrait_time_str_), "%02lld:%02lld",
                          remaining / 60, remaining % 60);
        std::snprintf(portrait_power_str_, sizeof(portrait_power_str_), "%04u",
                      render_state_[current_player_].power);
    }

    {
        static const char* kPortraitThemeWav[6] = {
            "WAV\\HOWL.WAV", "WAV\\BJUJU2.WAV", "WAV\\EGYPT.WAV",
            "WAV\\BATS.WAV", "WAV\\WITCH.WAV",  "WAV\\HELEN.WAV",
        };
        const int cid = render_state_[current_player_].char_id;
        ReleaseAudio(portrait_.theme_wav);
        if (cid >= 0 && cid < 6) {
            portrait_.theme_wav = preloadWav(kPortraitThemeWav[cid]);
        }
    }
    portrait_wav_ = portrait_.theme_wav;

    // One-time special audio on first display only (curse_step_counter==0) - never written
    // here, it advances once per turn elsewhere (BeginStepArrival/is_ai branch below).
    ReleaseAudio(portrait_.curse_hellin_wav);
    const bool hellin_first_display = render_state_[current_player_].curse_type == 5 &&
                                      render_state_[current_player_].curse_step_counter == 0;
    if (hellin_first_display) {
        portrait_.curse_hellin_wav = preloadWav("W1\\AT_0082.WAV");
    }
    if (hellin_first_display && portrait_.curse_hellin_wav) {
        portrait_wav_ = portrait_.curse_hellin_wav;
    }

    if (portrait_wav_) {
        audio_.playLoaded(portrait_wav_, AudioFlags::Async);
    }

    if (render_state_[current_player_].is_ai) {
        render_state_[current_player_].power =
            static_cast<uint16_t>(render_state_[current_player_].power + 2);
        // If this AI is cursed (curse_type!=0), curse_step_counter advances here
        // instead — AI never walks, so it never reaches BeginStepArrival()'s
        // per-step increment. This is the only place a cursed AI's
        // curse_step_counter moves.
        if (render_state_[current_player_].curse_type != 0) {
            render_state_[current_player_].curse_step_counter++;
        }
    }

    state_action_executed_ = true;
}

void BoardGameScreen::LoadDeferredPortraitResources() {
    // ShowingPortrait's render() draws only the portrait bitmap, never video_, so the
    // board can reopen here, behind the already-playing theme WAV, instead of at TurnEnd.
    ReopenFullmapAvi();

    ReleaseTexture(portrait_.curse_indicator_tex);
    ReleaseAudio(portrait_.curse_blindness_wav);
    if (render_state_[current_player_].curse_type == 3) {
        portrait_.curse_indicator_tex = assets_.texture("C1", "K_CURSE");
        if (!portrait_.curse_indicator_tex) {
            std::fprintf(stderr, "[BoardGameScreen] WARNING: K_CURSE.BMP failed to load — Khufu "
                                 "blindness will show a blank screen\n");
        }
        if (render_state_[current_player_].curse_step_counter == 0) {
            portrait_.curse_blindness_wav = preloadWav("W6\\AT_0597.WAV");
        }
    }

    const int cid = render_state_[current_player_].char_id;
    ReleaseAudio(portrait_.turn_wav);
    ReleaseAudio(portrait_.move_wav);
    if (cid >= 0 && cid < 6) {
        char turn_path[32];
        std::snprintf(turn_path, sizeof(turn_path), "WAV\\TURN%d.WAV", cid);
        portrait_.turn_wav = preloadWav(turn_path);
        char move_path[32];
        if (render_state_[current_player_].curse_type == 2) {
            std::snprintf(move_path, sizeof(move_path), "WAV\\RIBBIT1.WAV");
        } else {
            std::snprintf(move_path, sizeof(move_path), "WAV\\MOVE%d.WAV", cid);
        }
        portrait_.move_wav = preloadWav(move_path);
    }
}

bool BoardGameScreen::ComputePortraitAudioDone() const {
    auto portrait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - state_start_time_)
                           .count();
    if (portrait_ms < 300) {
        return false;
    } else if (portrait_wav_) {
        return !audio_.isPlaying(portrait_wav_);
    } else {
        return (portrait_ms >= 2000);
    }
}

bool BoardGameScreen::HandleDuelCornerClick(const IInput& input) {
    // Duel trigger: bottom-right corner click, own turn only, opens the opponent
    // picker. Rect byte-exact from the real zone-setup call. Disabled when is_ai,
    // skip_turn_counter != 0, black_hole_retry_counter != 0, or total_players==1 —
    // plus a curse gate: Hellin always blocks; Gevaudan blocks on ANY
    // curse_step_counter progress (a lower threshold than the >=2/>=3 this curse
    // uses elsewhere to suppress the portrait's time/power text and key icons).
    const bool duel_blocked_by_curse = (render_state_[current_player_].curse_type == 5) ||
                                       (render_state_[current_player_].curse_type == 1 &&
                                        render_state_[current_player_].curse_step_counter != 0);
    if (!render_state_[current_player_].is_ai &&
        render_state_[current_player_].skip_turn_counter == 0 &&
        render_state_[current_player_].black_hole_retry_counter == 0 && !duel_blocked_by_curse &&
        total_players_ > 1) {
        const auto& portrait_mouse = input.mouse();
        // Mouse/touch keep the original's invisible zero-affordance corner exactly as
        // shipped; gamepad gets a direct Y bind instead of steering a cursor into an
        // invisible corner.
        const bool gp_duel =
            gamepad_mode_ && input.gamepad().pressed[static_cast<size_t>(GamepadButton::Y)];
        if ((portrait_mouse.clicked[static_cast<int>(MouseButton::Left)] &&
             portrait_mouse.x >= 533 && portrait_mouse.x < 639 && portrait_mouse.y >= 365 &&
             portrait_mouse.y < 479) ||
            gp_duel) {
            // Glass is precached here (loaded, not played) — it only actually sounds
            // later, gated on a valid target pick in DuelChooseOpponent's own
            // click-resolution. Only the opponent-pick prompt plays unconditionally
            // at screen-open.
            if (PrepareDuelChooseOpponent() > 0) {
                if (portrait_wav_) audio_.stop(portrait_wav_);
                current_state_ = BoardGameState::DuelChooseOpponent;
                state_start_time_ = std::chrono::steady_clock::now();
                state_action_executed_ = false;
                duel_.choose_ai_target =
                    -1;  // human-initiated: real interactive pick, not AI auto-resolve
                return true;
            }
        }
    }
    return false;
}

int BoardGameScreen::PrepareDuelChooseOpponent() {
    int candidate_count = 0;
    for (int i = 0; i < total_players_; ++i) {
        if (i == current_player_ || render_state_[i].eliminated) continue;
        ++candidate_count;
    }
    duel_.choose_num_candidates = candidate_count;
    if (candidate_count > 0) {
        if (duel_.choose_icons_tex == 0) {
            duel_.choose_icons_tex = assets_.texture("MISC", "ATTKCHSE");
            duel_.choose_broken_tex = assets_.texture("MISC", "ATTKEMPT");
        }
        if (duel_.choose_glass_wav == 0) {
            duel_.choose_glass_wav = preloadWav("W9\\AT_GLASS.WAV");
            duel_.choose_prompt_wav = preloadWav("WAV\\AT_0631.WAV");
        }
        audio_.playLoaded(duel_.choose_prompt_wav, AudioFlags::None);
    }
    return candidate_count;
}

bool BoardGameScreen::HandlePauseCornerClick(const IInput& input) {
    // Pause trigger: click bottom-left corner of portrait screen (where time/power
    // are shown), own turn only. Sibling zone of the duel-corner trigger, byte-exact
    // rect (7,391)-(110,479).
    if (!render_state_[current_player_].is_ai) {
        const auto& pause_mouse = input.mouse();
        const bool gp_pause =
            gamepad_mode_ && input.gamepad().pressed[static_cast<size_t>(GamepadButton::Start)];
        if ((pause_mouse.clicked[static_cast<int>(MouseButton::Left)] && pause_mouse.x >= 7 &&
             pause_mouse.x < 110 && pause_mouse.y >= 391 && pause_mouse.y < 479) ||
            gp_pause) {
            pause_screen_.Enter();
            return true;
        }
    }
    return false;
}

bool BoardGameScreen::ResolveBlackHoleRetry() {
    // Black-hole retry: while black_hole_retry_counter is nonzero, this player gets
    // no dice/movement — the bhole AVI replays and the counter increments. Resolves
    // when counter==4 or counter > a 1-6 roll, guaranteeing release by the 4th
    // attempt (matches the manual's up-to-3-lost-turns).
    auto& bh_rs = render_state_[current_player_];
    if (bh_rs.black_hole_retry_counter != 0) {
        const int bh_roll = ComputeBlackHoleRoll(RngNext());
        const bool bh_resolves = ComputeBlackHoleResolves(bh_rs.black_hole_retry_counter, bh_roll);
        if (bh_resolves) {
            bh_rs.black_hole_retry_counter = 0;
            // Generic closing EYELOOP.
            static const char* kBholeCloseWav[6] = {"W2\\AT_0307.WAV", "W5\\AT_0564.WAV",
                                                    "W5\\AT_0565.WAV", "W5\\AT_0566.WAV",
                                                    "W5\\AT_0567.WAV", "W5\\AT_0569.WAV"};
            const int bh_insult = RollIndex(RngNext(), 6);
            const int bh_gk = RollIndex(RngNext(), 3) + 1;
            gkloop_avi_path_ =
                AssetPaths::avi("AVI", std::string("GKLOOP") + std::to_string(bh_gk)).string();
            gkloop_post_state_ =
                bh_rs.is_ai ? BoardGameState::TurnEnd : BoardGameState::WaitingForPlayer;
            gkloop_enc_audio_ = playWav(kBholeCloseWav[bh_insult], AudioFlags::Async);
            video_.close();
            video_loaded_ = false;
            video_.setRenderRect(kBoardAreaX, kBoardAreaY, kBoardAreaW, kBoardAreaH);
            if (video_.open(gkloop_avi_path_)) {
                video_.play(true);
            }
            black_flash_next_state_ = BoardGameState::GkLoopAVI;
            current_state_ = BoardGameState::BlackFlash;
        } else {
            bh_rs.black_hole_retry_counter++;
            stone_arrival_avi_path_ =
                AssetPaths::avi("AV1", std::string("BHOLE") + std::to_string(bh_rs.province))
                    .string();
            current_state_ = BoardGameState::StoneArrivalAVI;
            // Open the real BHOLE clip here immediately (not deferred to
            // StoneArrivalAVI's own state-entry block) so no frame in between
            // shows the stale FULLMAP.AVI.
            video_.close();
            video_loaded_ = false;
            video_.setRenderRect(kBoardAreaX, kBoardAreaY, kBoardAreaW, kBoardAreaH);
            if (video_.open(stone_arrival_avi_path_)) video_.play(true);
        }
        state_start_time_ = std::chrono::steady_clock::now();
        state_action_executed_ = (current_state_ == BoardGameState::StoneArrivalAVI);
        ReleaseTexture(portrait_.tex);
        ReleaseTexture(portrait_.key_tex);
        ReleaseTexture(portrait_.curse_indicator_tex);
        ReleaseAudio(portrait_.curse_blindness_wav);
        ReleaseAudio(portrait_.curse_hellin_wav);
        return true;
    }
    return false;
}

void BoardGameScreen::DispatchTurnStartAfterPortrait() {
    // AI never rolls dice or moves on the board — its position is set once at
    // game start and never updates. This is the real design: AI is a stationary
    // encounter target, not a real board player.
    if (render_state_[current_player_].is_ai) {
        // AI-initiated duel eligibility: attacker (this AI) must not be Hellin-cursed
        // (can't duel at all) or heavily Gevaudan-cursed, must have power >=
        // target's power*1.25 (target no more than 80% of attacker's power), target
        // must not be mid-black-hole-retry, and a stealable key must exist OR the
        // target isn't already cursed (the AI won't challenge someone it can neither
        // steal from nor curse).
        bool ai_duel_fired = false;
        if (total_players_ > 1 && !debug_disable_ai_duel_) {
            int human_target = -1;
            for (int i = 0; i < total_players_; ++i) {
                if (!render_state_[i].is_ai && !render_state_[i].eliminated) {
                    human_target = i;
                    break;
                }
            }
            if (human_target >= 0) {
                const auto& target_rs = render_state_[human_target];
                const auto& attacker_rs = render_state_[current_player_];
                auto has_stealable_key = [](const PlayerRenderState& atk,
                                            const PlayerRenderState& tgt) {
                    for (int i = 0; i < 6; ++i) {
                        const int prov = (kKeyTable[i] + tgt.char_id) % 6;
                        if (tgt.keys[prov] != 0 && atk.keys[prov] == 0) return true;
                    }
                    return false;
                };
                const bool ineligible =
                    attacker_rs.curse_type == 5 ||
                    (attacker_rs.curse_type == 1 && attacker_rs.curse_step_counter > 1) ||
                    (attacker_rs.power * 80 / 100 < target_rs.power) ||
                    target_rs.black_hole_retry_counter != 0 ||
                    !(has_stealable_key(attacker_rs, target_rs) || target_rs.curse_type == 0);
                if (!ineligible) {
                    // Opens the SAME real interactive picker screen the human's
                    // corner-click does (glass + opponent-pick prompt) — setting
                    // choose_ai_target here just makes it auto-resolve instead of
                    // waiting for a real click.
                    PrepareDuelChooseOpponent();
                    current_state_ = BoardGameState::DuelChooseOpponent;
                    state_start_time_ = std::chrono::steady_clock::now();
                    state_action_executed_ = false;
                    duel_.choose_ai_target = human_target;
                    duel_.choice_ai_timer = 0.0f;
                    ai_duel_fired = true;
                }
            }
        }

        if (!ai_duel_fired) {
            // AI players never get a random encounter (or any minigame) rolled on
            // their own turn — full stop. Their turn is exactly: a +2 power tick ->
            // duel-eligibility check -> straight to TurnEnd.
            current_state_ = BoardGameState::TurnEnd;
            state_start_time_ = std::chrono::steady_clock::now();
            state_action_executed_ = false;
        }
    } else {
        // First turn per player -> play HOME{char_id}.AVI before dice.
        if (!render_state_[current_player_].home_avi_played) {
            current_state_ = BoardGameState::HomeAVI;
        } else {
            current_state_ = BoardGameState::WaitingForPlayer;
        }
    }
    state_start_time_ = std::chrono::steady_clock::now();
    state_action_executed_ = false;
    ReleaseTexture(portrait_.tex);
    ReleaseTexture(portrait_.key_tex);
    ReleaseAudio(portrait_.curse_hellin_wav);
}

void BoardGameScreen::UpdateRollingDice(const IInput& input, float dt) {
    // Never rolls all 3 dice up front — only die1 (1-die) or die2+die3 (2-dice),
    // whichever the player picks. The spinning animation is purely cosmetic (a
    // local frame counter); it doesn't consume game RNG.
    if (!state_action_executed_) {
        state_action_executed_ = true;
        dice_.anim_counter = 0;
        dice_.result_locked = false;
        dice_.selected_option = 0;
        dice_.move_distance = 0;
        cursor_state_.is_animating = false;

        turn_start_time_ = std::chrono::steady_clock::now();

        {
            auto& rs_snap = render_state_[current_player_];
            rs_snap.province_backup = rs_snap.province;
            rs_snap.stone_backup = rs_snap.stone_id;
            rs_snap.direction_backup = rs_snap.direction;
        }

        if (rolling_wav_ != 0) {
            audio_.playLoaded(rolling_wav_, AudioFlags::Async | AudioFlags::Loop);
        }
    }

    // Animation speed: ~4 frames/face at ~60fps.
    dice_.anim_counter++;

    // Dice-count choice uses the same 3 real MAPMOVE.BIN screen-third zones as
    // movement direction (GetMouseZone()), not small boxes around the die sprites.
    const auto& mouse = input.mouse();
    bool clicked = mouse.clicked[static_cast<int>(MouseButton::Left)];

    // 2-dice eligibility: requires dice_restricted==2 (normal), even_dice==0, and
    // curse_type!=2 (Anne/frog curse forces 1-die only). When the guard fails, the real game
    // does nothing at all — the click is silently ignored, not redirected to 1-die.
    const auto& rs = render_state_[current_player_];
    const bool two_dice_blocked =
        rs.dice_restricted != 2 || rs.even_dice != 0 || rs.curse_type == 2;

    // Gamepad direct-select: D-pad left/right moves focus
    // between the 1-die/2-dice zones, A confirms.
    bool gp_confirm = false;
    if (gamepad_mode_) {
        const auto& gp = input.gamepad();
        // dice_.focus persists across turns — clamp back every
        // frame if this turn is now restricted, not just
        // block new rightward moves.
        if (two_dice_blocked) dice_.focus = 0;
        int stick_dx = 0;
        if (gp.pressed[static_cast<size_t>(GamepadButton::DpadLeft)])
            dice_.focus = 0;
        else if (gp.pressed[static_cast<size_t>(GamepadButton::DpadRight)] && !two_dice_blocked)
            dice_.focus = 1;
        else {
            stick_repeater_.PollHorizontal(gp.left_x, gp.left_y, dt, stick_dx);
            if (stick_dx < 0)
                dice_.focus = 0;
            else if (stick_dx > 0 && !two_dice_blocked)
                dice_.focus = 1;
        }
        gp_confirm = gp.pressed[static_cast<size_t>(GamepadButton::A)];
    }

    // Die click: only accept while result is not yet locked
    if ((clicked || gp_confirm) && !dice_.result_locked) {
        const uint8_t zone =
            clicked ? GetMouseZone(mouse.x, mouse.y) : static_cast<uint8_t>(dice_.focus);
        bool on_die1 = (zone == 0);   // LEFT  -> 1-die
        bool on_die23 = (zone == 1);  // RIGHT -> 2-dice
        // zone == 2 (CENTER) -> no-op.

        if (two_dice_blocked && on_die23) {
            on_die23 = false;
        }

        if (on_die1 || on_die23) {
            const char* choice;
            if (on_die1) {
                dice_.selected_option = 1;
                RollDiceForOption(1);
                choice = "1-die";
                // Clicking the die itself never resets a dice_restricted persist in
                // the original — only a later RollExtraTurnOrDiceEncounter roll
                // (nonzero sub-roll, with dice_restriction_turns already set)
                // clears it.
            } else {
                dice_.selected_option = 2;
                RollDiceForOption(2);
                choice = "2-dice";
            }
            dice_.anim_counter = 10000;  // lock animation at final face

            // Stop ROLLING, play ROLLED — result face stays
            // visible until this audio finishes.
            if (rolling_wav_) audio_.stop(rolling_wav_);
            if (rolled_wav_ != 0) {
                audio_.playLoaded(rolled_wav_, AudioFlags::Async);
            }

            dice_.result_locked = true;
        }
    }

    // Wait for the dice-landed sound to finish before transitioning to Moving —
    // matches the real gate, which waits for whatever's playing to finish before
    // dispatching the outcome.
    if (dice_.result_locked) {
        const bool rolled_done = (rolled_wav_ == 0) || !audio_.isPlaying(rolled_wav_);
        if (rolled_done) {
            dice_.result_locked = false;

            // An even-only-restricted (enc type 5) 1-die roll that ISN'T
            // 6 skips the walking phase ENTIRELY: no movement, arrival is
            // dispatched immediately at the CURRENT stone, restriction NOT cleared
            // (persists to next turn). The generic enc type 1-16 roll still fires
            // normally (only the type-1..4 special-effect switch is skipped).
            if (dice_.selected_option == 1 && render_state_[current_player_].even_dice != 0) {
                render_state_[current_player_].steps_remaining = 0;
                ResolveStepArrival(/*skip_stone_type_effects=*/true);
                return;
            }

            // "The Chosen One": every 2-dice roll summing to 12 fires a dedicated
            // EYELOOP, always GKLOOP1 (never the usual random 1-3 pick), with a
            // fixed insult clip (W1/AT_0102.WAV). Returns immediately, resumes via
            // the audio-done chain into TurnEnd (no move after it).
            if (dice_.move_distance == 12) {
                gkloop_avi_path_ = AssetPaths::avi("AVI", "GKLOOP1").string();
                gkloop_post_state_ = BoardGameState::TurnEnd;
                gkloop_enc_audio_ = playWav("W1\\AT_0102.WAV", AudioFlags::Async);
                video_.close();
                video_loaded_ = false;
                video_.setRenderRect(kBoardAreaX, kBoardAreaY, kBoardAreaW, kBoardAreaH);
                if (video_.open(gkloop_avi_path_)) {
                    video_.play(true);
                }
                current_state_ = BoardGameState::GkLoopAVI;
                state_start_time_ = std::chrono::steady_clock::now();
                state_action_executed_ = false;
                return;
            }

            current_state_ = BoardGameState::Moving;
            state_start_time_ = std::chrono::steady_clock::now();
            state_action_executed_ = false;
        }
    }
}

void BoardGameScreen::UpdateMoving(const IInput& input, float dt) {
    if (!state_action_executed_) {
        render_state_[current_player_].steps_remaining = static_cast<uint16_t>(dice_.move_distance);
        // Frozen snapshot used by the "stop at any stone" key power.
        render_state_[current_player_].steps_total_this_roll =
            render_state_[current_player_].steps_remaining;
        cursor_state_.is_animating = true;  // cursor animates during movement
        // Fresh repeater state every roll — otherwise a stick still held from the
        // PREVIOUS roll starts this one already mid-repeat/accelerated, firing
        // several real steps at once instead of one.
        move_stick_repeater_ = StickRepeater{};
        state_action_executed_ = true;
        last_move_time_ = std::chrono::steady_clock::now();
        timeout_warn_played_ = false;
        portrait_.curse_move_audio_played = false;
    }

    // TriggerCurseIndicatorIfKhufu() is called once per roll, edge-triggered when
    // ms_in_moving crosses kDiceGraceMs — whether its audio actually plays is
    // gated inside that function, not here. The bitmap swap itself is
    // unconditional on this same threshold in render()'s Layer 1.
    if (!portrait_.curse_move_audio_played) {
        const long long ms_in_moving_now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::steady_clock::now() - state_start_time_)
                                               .count();
        if (ms_in_moving_now >= kDiceGraceMs) {
            portrait_.curse_move_audio_played = true;
            TriggerCurseIndicatorIfKhufu();
        }
    }

    // Khufu curse (mode 3): once the blindness audio starts
    // (first trigger only, curse_step_counter==0 gate), it blocks ALL
    // input until it finishes — same for the portal/tunnel-
    // entry-blocked STOP.WAV, not just movement zone clicks.
    // One-shot line (gated to play exactly once per curse, never again this same curse) —
    // freed the instant it naturally finishes, rather than held until TurnEnd/TriggerEncounter.
    if (portrait_.curse_blindness_wav && !audio_.isPlaying(portrait_.curse_blindness_wav)) {
        ReleaseAudio(portrait_.curse_blindness_wav);
    }
    const bool movement_available =
        !(portrait_.curse_blindness_wav && audio_.isPlaying(portrait_.curse_blindness_wav)) &&
        !audio_.isPlaying(portal_blocked_wav_);
    if (!movement_available) {
        return;
    }

    const auto& mouse = input.mouse();
    bool clicked = mouse.clicked[static_cast<int>(MouseButton::Left)];

    // Tracked every frame regardless of click — a later rotate click uses this
    // zone to update mapfile_direction_.
    uint8_t zone = GetMouseZone(mouse.x, mouse.y);
    current_mouse_zone_ = zone;

    // "Stop at any stone" (key_power_flags bit1) — Space ends the move early,
    // mid-roll, at whatever stone the player currently occupies. Requires
    // steps_remaining>0 AND at least 1 step already taken this roll AND the bit set;
    // routes to the same arrival dispatch as a normal arrival.
    const auto& keyboard_mv = input.keyboard();
    // Gamepad: A confirms here too (port-only UX — without it, a gamepad-only
    // player would be stuck pressing Space on every single move).
    const bool gp_confirm_mv =
        gamepad_mode_ && input.gamepad().pressed[static_cast<size_t>(GamepadButton::A)];
    if ((keyboard_mv.pressed[static_cast<int>(Key::Space)] || gp_confirm_mv) &&
        (debug_cheat_easy_win_ || (render_state_[current_player_].key_power_flags & 0x02)) &&
        render_state_[current_player_].steps_remaining > 0 &&
        render_state_[current_player_].steps_remaining <
            render_state_[current_player_].steps_total_this_roll) {
        render_state_[current_player_].steps_remaining = 0;
        ResolveStepArrival();
        return;
    }

    // The OTHER disjunct of the same real Space branch: steps_remaining==0 alone,
    // unconditional, no key-power bit needed — the normal end-of-roll
    // confirmation once all steps are walked.
    if ((keyboard_mv.pressed[static_cast<int>(Key::Space)] || gp_confirm_mv) &&
        render_state_[current_player_].steps_remaining == 0 &&
        render_state_[current_player_].steps_total_this_roll > 0) {
        ResolveStepArrival();
        return;
    }

    // Flight Key (key_power_flags bit2) — any F-key opens the province-teleport map,
    // mid-roll, while standing on a type-6 stone (each province's single central
    // stone, always stone 0). Landing on the stone via normal movement never reaches
    // this — it's keyboard-only in the original. Gamepad: Y/Triangle. Using Flight
    // decrements steps_remaining by 1.
    {
        const MapStone* flight_st = mapfile_.GetStone(render_state_[current_player_].province,
                                                      render_state_[current_player_].stone_id);
        const bool flight_key =
            keyboard_mv.pressed[static_cast<int>(Key::Function)] ||
            (gamepad_mode_ && input.gamepad().pressed[static_cast<size_t>(GamepadButton::Y)]);
        if (flight_key && (render_state_[current_player_].key_power_flags & 0x04) &&
            render_state_[current_player_].steps_remaining > 0 && flight_st &&
            flight_st->type == 6) {
            render_state_[current_player_].steps_remaining -= 1;
            flight_map_screen_.Enter();
            return;
        }
    }

    if (clicked) {
        // Step counter box (COUNTER.BMP) opens the province minimap —
        // counter_box_{x,y,w,h}_ comes from DATA/MAPMOVE.BIN's trailing 8
        // bytes, defaulting to the known-good real values if missing/short.
        if (mouse.x >= counter_box_x_ && mouse.x < counter_box_x_ + counter_box_w_ &&
            mouse.y >= counter_box_y_ && mouse.y < counter_box_y_ + counter_box_h_) {
            minimap_screen_.Enter(BoardGameState::Moving);
            return;
        } else if (zone == kNoZone) {
        } else if (!MovementCooldownElapsed()) {
        } else {
            HandleMovementInput(zone);
        }
    }

    // Gamepad direct-bind: D-pad left/right/up map straight
    // to the same LEFT/RIGHT/CENTER zones a mouse click on
    // the 3 MAPMOVE.BIN polygons would hit; X opens the
    // minimap (replaces the invisible step-counter hotspot).
    if (gamepad_mode_ && MovementCooldownElapsed()) {
        const auto& gp = input.gamepad();
        int gp_zone = -1;
        if (gp.pressed[static_cast<size_t>(GamepadButton::DpadLeft)])
            gp_zone = 0;
        else if (gp.pressed[static_cast<size_t>(GamepadButton::DpadRight)])
            gp_zone = 1;
        else if (gp.pressed[static_cast<size_t>(GamepadButton::DpadUp)])
            gp_zone = 2;
        else {
            int stick_dx = 0, stick_dy = 0;
            move_stick_repeater_.Poll(gp.left_x, gp.left_y, dt, stick_dx, stick_dy,
                                      StickRepeater::kStickRepeatRate, false);
            if (stick_dx < 0)
                gp_zone = 0;
            else if (stick_dx > 0)
                gp_zone = 1;
            else if (stick_dy < 0)
                gp_zone = 2;
        }
        if (gp_zone >= 0) {
            HandleMovementInput(static_cast<uint8_t>(gp_zone));
        } else if (gp.pressed[static_cast<size_t>(GamepadButton::X)]) {
            minimap_screen_.Enter(BoardGameState::Moving);
            return;
        }
    }

    // Keyboard arrows: Left/Right/Up handled literally as board movement input
    // (Left=rotate CCW, Right=rotate CW, Up=step) — independent of any
    // mouse/gamepad path. Down is not handled in the original.
    if (MovementCooldownElapsed()) {
        int kb_zone = -1;
        if (keyboard_mv.pressed[static_cast<int>(Key::Left)])
            kb_zone = 0;
        else if (keyboard_mv.pressed[static_cast<int>(Key::Right)])
            kb_zone = 1;
        else if (keyboard_mv.pressed[static_cast<int>(Key::Up)])
            kb_zone = 2;
        if (kb_zone >= 0) {
            HandleMovementInput(static_cast<uint8_t>(kb_zone));
        }
    }
}

void BoardGameScreen::UpdateTeleportAVI() {
    // AVI plays fully, then player teleports to teleport_.dest_stone.
    if (!state_action_executed_) {
        // Stops the ambient nag WAV immediately — tunnel/hub entry fires on a direct
        // wall click rather than a normal step arrival, so this is the one shared
        // entry point where every TeleportAVI trigger still needs it silenced before
        // its own video opens.
        if (ambient_nag_wav_ != 0) {
            audio_.stop(ambient_nag_wav_);
            ambient_nag_wav_ = 0;
        }
        video_.close();
        video_loaded_ = false;
        video_.setRenderRect(kBoardAreaX, kBoardAreaY, kBoardAreaW, kBoardAreaH);
        if (!video_.open(teleport_.path)) {
            GameState* gs2 = GameStateManager::Instance().Get();
            if (gs2) {
                gs2->sub_idx = teleport_.dest_stone;
                if (teleport_.dest_direction >= 0) {
                    mapfile_direction_ = static_cast<uint8_t>(teleport_.dest_direction);
                    render_state_[current_player_].direction = mapfile_direction_;
                    teleport_.dest_direction = -1;
                }
                AutoOrientDirection(gs2->player_idx, gs2->sub_idx);
            }
            ReopenFullmapAvi();
            if (teleport_.kind == TeleportAviState::Kind::GatekeeperChallenge) {
                StartGatekeeperResultTaunt();
            } else {
                current_state_ = teleport_.post_state;
                teleport_.post_state = BoardGameState::Moving;  // reset to default
                state_start_time_ = std::chrono::steady_clock::now();
                state_action_executed_ = false;
            }
            return;
        }
        video_.play(true);
        state_action_executed_ = true;
    }

    if (video_.status() != VideoStatus::Playing) {
        GameState* gs2 = GameStateManager::Instance().Get();
        if (gs2) {
            if (teleport_.dest_prov >= 0) {
                gs2->player_idx = static_cast<uint16_t>(teleport_.dest_prov);
                render_state_[current_player_].province =
                    static_cast<uint16_t>(teleport_.dest_prov);
            }
            gs2->sub_idx = teleport_.dest_stone;
            render_state_[current_player_].stone_id = teleport_.dest_stone;
            if (teleport_.dest_direction >= 0) {
                mapfile_direction_ = static_cast<uint8_t>(teleport_.dest_direction);
                render_state_[current_player_].direction = mapfile_direction_;
                teleport_.dest_direction = -1;
            }
            AutoOrientDirection(gs2->player_idx, gs2->sub_idx);
        }
        teleport_.dest_prov = -1;
        teleport_.shows_counter = false;
        // The real game never returns to board display once the
        // win flag is set — skip the FULLMAP.AVI reopen in that
        // case (win_flag is set in exactly one place, the
        // central-portal win check, so this can't false-positive
        // on a normal teleport's reopen).
        if (!render_state_[current_player_].win_flag) {
            ReopenFullmapAvi();
        }
        const TeleportAviState::Kind finished_kind = teleport_.kind;
        teleport_.kind = TeleportAviState::Kind::RollEnding;  // reset to default
        if (finished_kind == TeleportAviState::Kind::MidRollContinue) {
            current_state_ = BoardGameState::Moving;
            state_action_executed_ = true;
            state_start_time_ = std::chrono::steady_clock::now();
        } else if (finished_kind == TeleportAviState::Kind::GatekeeperChallenge) {
            StartGatekeeperResultTaunt();
        } else {
            current_state_ = teleport_.post_state;
            teleport_.post_state = BoardGameState::Moving;  // reset to default
            state_start_time_ = std::chrono::steady_clock::now();
            state_action_executed_ = false;
        }
    }
    return;
}

}  // namespace openfear::game
