#include "openfear/game/subscreens/BlackjackGame.h"
#include "openfear/game/BoardGameScreen.h"
#include "openfear/game/GameState.h"
#include "openfear/game/FocusIndicator.h"
#include <chrono>
#include <cstdio>
#include <vector>

namespace openfear::game {

int BlackjackGame::ComputeHandValue(const int* cards, int count) {
    int hard = 0, soft = 0, aces = 0;
    for (int i = 0; i < count; ++i) {
        if (!cards[i]) continue;
        int v;
        if (cards[i] == 1) {
            v = 1;
            aces++;
        } else if (cards[i] >= 11) {
            v = 10;
        } else {
            v = cards[i];
        }
        hard += v;
        soft += (cards[i] == 1) ? 11 : v;
    }
    return (aces > 0 && soft <= 21) ? soft : hard;
}

int BlackjackGame::RollUniqueRank(const int* hand, int filled_count) {
    int rank;
    bool dup;
    do {
        rank = RollIndex(board_.RngNext(), 13) + 1;
        dup = false;
        for (int i = 0; i < filled_count; ++i) {
            if (hand[i] == rank) {
                dup = true;
                break;
            }
        }
    } while (dup);
    return rank;
}

int BlackjackGame::PlayerScore() const {
    int active_cards[5];
    int active_count = 0;
    for (int i = 0; i < 5; ++i) {
        if (i != 0 && !player_confirmed_[i]) continue;
        active_cards[active_count++] = player_cards_[i];
    }
    return ComputeHandValue(active_cards, active_count);
}

bool BlackjackGame::LoadZoneRects() {
    if (!LoadRectZoneOrFail("bjack2", 4, zone_rects_.data(),
                            "BJACK2.BIN not loaded/wrong size — hit/stand zones would never "
                            "respond to clicks")) {
        return false;
    }
    if (!LoadRectZoneOrFail("bjack1", 2, stake_zone_rects_.data(),
                            "BJACK1.BIN not loaded/wrong size — stake-choice zones would never "
                            "respond to clicks")) {
        return false;
    }
    return true;
}

void BlackjackGame::FreeAssets() {
    ReleaseTexture(table_tex_);
    ReleaseTexture(announce_tex_);
    ReleaseTexture(stake_icon_tex_);
    for (auto& t : suit_tex_) ReleaseTexture(t);
}

void BlackjackGame::Enter(int player) {
    // Stake is picked via a weighted table before the screen shows; the
    // player only confirms or declines it (StakeAnnounce phase). Catch-up
    // (held keys in [1,5] + last-10-min key limit active) guarantees the
    // key stake outright. Otherwise a flat weighted pick per category:
    // kStakeTurn if total_players>1; kStakePower20/kStakePower30 (equal
    // weight) if power>9; kStakeKey if held keys in [1,5] — no key/power
    // tapering at all.
    if (table_tex_ == 0) {
        table_tex_ = assets_.texture("G3", "CARDTBLE");
        announce_tex_ = assets_.texture("G3", "BJTABLEH");
        announce_sprite_ = renderer_.createSprite(announce_tex_);
        stake_icon_tex_ = assets_.texture("G3", "BJBLIT");
    }
    const int held_keys = board_.CountHeldKeys(player);
    const bool keys_1_to_5 = held_keys >= 1 && held_keys <= 5;
    const bool power_gt9 = board_.render_state_[player].power > 9;

    GameState* game_state = GameStateManager::Instance().Get();
    const long long total_duration_s =
        game_state ? static_cast<long long>(game_state->game_duration_s_) : 0;
    using namespace std::chrono;
    const long long elapsed_s =
        duration_cast<milliseconds>(steady_clock::now() - board_.game_start_time_).count() / 1000;
    const long long remaining_s = total_duration_s - elapsed_s;

    // Forces the key stake once real remaining time drops below 10 minutes.
    const bool catchup = keys_1_to_5 && remaining_s < 600 && held_keys < 3;

    if (catchup) {
        stake_id_ = kStakeKey;  // guaranteed
    } else {
        // Capacity tracks the actual entry count this table ends up with (0-4
        // depending on which categories are present), matching how the real
        // picker always normalizes against its actual count, not a nominal max.
        const int capacity =
            (board_.total_players_ > 1 ? 1 : 0) + (power_gt9 ? 2 : 0) + (keys_1_to_5 ? 1 : 0);

        BoardGameScreen::WeightedPickEntry entries[4];
        int n = 0;

        if (board_.total_players_ > 1) {
            const int slot_ids[1] = {kStakeTurn};
            BoardGameScreen::AppendWeightedCategory(slot_ids, 1, 0.0f, capacity, entries, n);
        }
        if (power_gt9) {
            const int power_ids[2] = {kStakePower20, kStakePower30};  // real creation order
            BoardGameScreen::AppendWeightedCategory(power_ids, 2, 0.0f, capacity, entries, n);
        }
        if (keys_1_to_5) {
            const int key_ids[1] = {kStakeKey};
            BoardGameScreen::AppendWeightedCategory(key_ids, 1, 0.0f, capacity, entries, n);
        }

        // n==0 is reachable (1 total player, power<=9, held keys outside [1,5] — e.g.
        // turn 1 of a solo game) and PickWeightedEntry has no entry to fall back to in
        // that case — default to kStakeTurn, the one stake the real table always tries
        // to include regardless of standing.
        stake_id_ = (n > 0) ? board_.PickWeightedEntry(entries, n) : kStakeTurn;
    }

    player_ = player;
    phase_ = BjPhase::StakeAnnounce;
    player_count_ = 0;
    dealer_count_ = 0;
    dealer_hidden_ = true;
    player_won_ = false;
    clicked_ = false;
    for (int i = 0; i < 5; ++i) {
        player_cards_[i] = 0;
        dealer_cards_[i] = 0;
        player_confirmed_[i] = false;
    }
    phase_time_ = std::chrono::steady_clock::now();
    board_.video_.close();
    board_.video_loaded_ = false;
    board_.current_state_ = BoardGameState::BlackjackGame;
    board_.state_start_time_ = std::chrono::steady_clock::now();
    board_.state_action_executed_ = false;
}

void BlackjackGame::update(const IInput& input, float dt) {
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();
    const auto phase_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - phase_time_).count();

    const auto& mouse = input.mouse();
    const bool left_click = mouse.clicked[static_cast<int>(MouseButton::Left)];
    const bool right_click = mouse.clicked[static_cast<int>(MouseButton::Right)];
    const int mouse_x = mouse.x, mouse_y = mouse.y;

    switch (phase_) {
        case BjPhase::StakeAnnounce: {
            // stake_id_ was already picked in Enter(); announce it via
            // audio. 2 real zones during this phase: left = decline (no
            // stake effect), right = confirm (advance now instead of
            // waiting out the fallback timer).
            if (!board_.state_action_executed_) {
                board_.state_action_executed_ = true;
                stake_clicked_ = false;
                stake_focus_confirm_ = true;
                stake_audio_done_time_ = std::chrono::steady_clock::time_point::min();
                // tag0(key)->BJI4, tag1(+30)->BJI3, tag2(+20)->BJI2, tag3(turn)->BJI1.
                static const char* kBjStakeWav[4] = {"W10\\BJI4.WAV", "W10\\BJI3.WAV",
                                                     "W10\\BJI2.WAV", "W10\\BJI1.WAV"};
                stake_audio_ = playWav(kBjStakeWav[stake_id_], AudioFlags::Async);
            }

            bool decline = false, confirm_now = false;
            if (!stake_clicked_ && (left_click || right_click)) {
                // Any raw mouse-down stops the stake announce
                // audio, before zone dispatch — not gated on
                // which zone was hit.
                if (stake_audio_) audio_.stop(stake_audio_);
                // LEFT plaque shows a checkmark, RIGHT shows an X.
                const auto& cz = stake_zone_rects_[0];
                const auto& dz = stake_zone_rects_[1];
                if (PointInRect(mouse_x, mouse_y, cz)) {
                    stake_clicked_ = true;
                    confirm_now = true;
                } else if (PointInRect(mouse_x, mouse_y, dz)) {
                    stake_clicked_ = true;
                    decline = true;
                }
            }
            // Gamepad: D-pad/stick left-right toggles focus
            // between the 2 real zones, A confirms whichever's focused.
            if (!stake_clicked_ && board_.gamepad_mode_) {
                const auto& gamepad = input.gamepad();
                int dx = 0;
                if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadLeft)]) dx = -1;
                if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadRight)]) dx = 1;
                if (dx == 0)
                    board_.stick_repeater_.PollHorizontal(gamepad.left_x, gamepad.left_y, dt, dx);
                if (dx != 0) stake_focus_confirm_ = (dx < 0);
                if (gamepad.pressed[static_cast<size_t>(GamepadButton::A)]) {
                    if (stake_audio_) audio_.stop(stake_audio_);
                    stake_clicked_ = true;
                    if (stake_focus_confirm_)
                        confirm_now = true;
                    else
                        decline = true;
                }
            }

            // Fallback timing when no click happens: once the
            // announce audio finishes, a 4000ms grace window
            // starts; if it expires with no click, the encounter
            // CANCELS (same as clicking decline), it does NOT auto-confirm.
            bool timeout_decline = false;
            if (!decline && !confirm_now) {
                const bool audio_playing = stake_audio_ && audio_.isPlaying(stake_audio_);
                if (!audio_playing) {
                    if (stake_audio_done_time_ == std::chrono::steady_clock::time_point::min()) {
                        stake_audio_done_time_ = now;
                    }
                    const auto grace_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                              now - stake_audio_done_time_)
                                              .count();
                    if (grace_ms >= 4000) timeout_decline = true;
                }
            }

            if (decline || timeout_decline) {
                // No stake resolution at all.
                board_.current_state_ = BoardGameState::TurnEnd;
                board_.state_start_time_ = std::chrono::steady_clock::now();
                board_.state_action_executed_ = false;
            } else if (confirm_now) {
                phase_ = BjPhase::DealCards;
                phase_time_ = now;
                board_.state_action_executed_ = false;
            }
            break;
        }

        case BjPhase::DealCards: {
            // 2 cards each dealt simultaneously, then check
            // natural-21 edge cases before any play.
            if (!board_.state_action_executed_) {
                board_.state_action_executed_ = true;

                // Pre-roll all 5 slots per side (per-hand dedup).
                for (int i = 0; i < 5; ++i) {
                    player_cards_[i] = RollUniqueRank(player_cards_, i);
                }
                for (int i = 0; i < 5; ++i) {
                    dealer_cards_[i] = RollUniqueRank(dealer_cards_, i);
                }
                // Both initial cards (slot0/1) are marked
                // confirmed-for-scoring together, right here at
                // deal time — independent of when they're
                // actually DRAWN (cards_revealed_, ~3s later).
                // Slots 2-4 (the 3 real hit zones) start unconfirmed.
                player_confirmed_[0] = true;
                player_confirmed_[1] = true;
                player_confirmed_[2] = false;
                player_confirmed_[3] = false;
                player_confirmed_[4] = false;
                player_count_ = 2;
                dealer_count_ = 2;
                dealer_hidden_ = true;
                cards_revealed_ = false;

                // 2 distinct random suit icons, decorative only.
                for (int i = 0; i < 2; ++i) {
                    ReleaseTexture(suit_tex_[i]);
                }
                int suit_a = RollIndex(board_.RngNext(), 4);
                int suit_b;
                do {
                    suit_b = RollIndex(board_.RngNext(), 4);
                } while (suit_b == suit_a);
                char suit_name[8];
                std::snprintf(suit_name, sizeof(suit_name), "SUIT%d", suit_a + 1);
                suit_tex_[0] = assets_.texture("G3", suit_name);
                std::snprintf(suit_name, sizeof(suit_name), "SUIT%d", suit_b + 1);
                suit_tex_[1] = assets_.texture("G3", suit_name);

                const int ps = PlayerScore();
                const int ds = ComputeHandValue(dealer_cards_, dealer_count_);
                const bool p_nat = (ps == 21);
                const bool d_nat = (ds == 21);
                if (p_nat || d_nat) {
                    // dealer_hidden_ stays true (revealed in Comparing,
                    // not here). cards_revealed_ only turns true when
                    // the player alone has the natural — dealer-natural
                    // and both-natural skip it entirely.
                    cards_revealed_ = (p_nat && !d_nat);
                    // No push/tie exists — compare is
                    // `dealer>=player -> LOSS`, so an exact tie
                    // (including both-natural) resolves as a dealer win.
                    player_won_ = (p_nat && !d_nat);
                    phase_ = BjPhase::Comparing;
                    phase_time_ = now;
                    board_.state_action_executed_ = false;
                } else {
                    // Neither natural — dealer plays its ENTIRE hand
                    // automatically first; player only gets a turn once
                    // the dealer stands under 21 (DealerTurn below).
                    static const char* kBjNoNaturalWav[3] = {"W10\\BJI5.WAV", "W10\\BJI6.WAV",
                                                             "W10\\BJI7.WAV"};
                    dealer_audio_ =
                        playWav(kBjNoNaturalWav[RollIndex(board_.RngNext(), 3)], AudioFlags::Async);
                    phase_ = BjPhase::DealerTurn;
                    phase_time_ = now;
                }
            }
            break;
        }

        case BjPhase::PlayerTurn: {
            // The dealer's stand/your-turn line (BJI9) must finish playing
            // (~3s) before the player's cards are actually shown. Nothing
            // else in this phase starts until then.
            if (!cards_revealed_) {
                if (dealer_audio_ != 0 && audio_.isPlaying(dealer_audio_)) {
                    break;
                }
                cards_revealed_ = true;
                dealer_audio_ = 0;
                phase_time_ = now;
                break;
            }
            // 5-card charlie cap — forced resolution on bust or
            // reaching 5 cards without busting.
            bool advance = false;
            {
                // rects[0..2] = 3 hit zones, each tied to a FIXED card
                // slot (zone0->slot2, zone1->slot3, zone2->slot4), not
                // sequential — rects[3] = stand (no card).
                // Gamepad: D-pad focus nav over the 4 zones (3 hit
                // + 1 stand), A selects — port-only UX, no ASM basis.
                if (board_.gamepad_mode_) {
                    const auto& gamepad = input.gamepad();
                    const int focus_before = focus_index_;
                    int dx = 0, dy = 0;
                    if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadLeft)]) dx = -1;
                    if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadRight)]) dx = 1;
                    if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadUp)]) dy = -1;
                    if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadDown)]) dy = 1;
                    if (dx == 0 && dy == 0)
                        board_.stick_repeater_.Poll(gamepad.left_x, gamepad.left_y, dt, dx, dy);
                    if (dx != 0 || dy != 0) {
                        std::vector<Point> focus_centers;
                        for (const auto& z : zone_rects_)
                            focus_centers.push_back({static_cast<int16_t>((z.x1 + z.x2) / 2),
                                                     static_cast<int16_t>((z.y1 + z.y2) / 2)});
                        focus_index_ = FindNextFocus(focus_centers, focus_index_, dx, dy);
                    }
                    if (focus_index_ != focus_before) {
                        focus_change_time_ = std::chrono::steady_clock::now();
                    }
                }
                const bool gamepad_select =
                    board_.gamepad_mode_ &&
                    input.gamepad().pressed[static_cast<size_t>(GamepadButton::A)];
                const bool any_click = left_click || right_click || gamepad_select;
                if (any_click && !clicked_) {
                    clicked_ = true;
                    int clicked_zone = -1;  // 0-2 = hit zones, 3 = stand
                    if (gamepad_select && !left_click && !right_click) {
                        clicked_zone = focus_index_;
                    } else {
                        for (int zi = 0; zi < 4; ++zi) {
                            const auto& z = zone_rects_[zi];
                            if (PointInRect(mouse_x, mouse_y, z)) {
                                clicked_zone = zi;
                                break;
                            }
                        }
                    }
                    // Neither hit nor stand plays a click sound in
                    // the real game.
                    if (clicked_zone >= 0 && clicked_zone <= 2) {
                        const int slot =
                            clicked_zone + 2;  // zone0->slot2, zone1->slot3, zone2->slot4
                        if (!player_confirmed_[slot]) {
                            // A slot already revealed can't be
                            // re-hit by re-clicking its zone.
                            player_confirmed_[slot] = true;
                            ++player_count_;
                            const int ps = PlayerScore();
                            if (ps > 21 || (player_confirmed_[2] && player_confirmed_[3] &&
                                            player_confirmed_[4]))
                                advance = true;  // bust OR 5-card charlie
                            else
                                phase_time_ = now;  // re-arm the 25000ms decision timer
                        }
                    } else if (clicked_zone == 3) {
                        advance = true;
                    }
                }
                if (!left_click && !right_click && !gamepad_select) clicked_ = false;

                // Decision timeout: a 25000ms window is armed
                // after every action; if it fires while still
                // interactive, plays the time's-up line then
                // resolves as an automatic loss.
                if (!advance && phase_ms >= 25000) {
                    playWav("W1\\AT_0063.WAV", AudioFlags::Async);
                    dealer_hidden_ = false;
                    player_won_ = false;
                    phase_time_ = now;
                    phase_ = BjPhase::Comparing;
                    board_.state_action_executed_ = false;
                    break;
                }
            }
            if (advance) {
                const int ps = PlayerScore();
                phase_time_ = now;
                // dealer_hidden_ stays true here — the
                // dealer's hole card is only ever drawn at actual
                // result-resolve time, not when the player
                // finishes acting.
                if (ps > 21) {
                    // Bust resolves immediately — dealer never gets a turn.
                    player_won_ = false;
                    phase_ = BjPhase::Comparing;
                    board_.state_action_executed_ = false;
                } else if (player_count_ == 5) {
                    // 5-card charlie: reaching a 5th card without
                    // busting is an automatic win regardless of
                    // the dealer's already-resolved hand.
                    player_won_ = true;
                    playWav("W10\\BJI10.WAV", AudioFlags::Async);
                    phase_ = BjPhase::Comparing;
                    board_.state_action_executed_ = false;
                } else {
                    // The dealer already played its FULL hand
                    // before this turn even started — compare
                    // directly against the already-final dealer score.
                    const int ds_final = ComputeHandValue(dealer_cards_, dealer_count_);
                    // No push/tie in the real game — the dealer wins ties.
                    player_won_ = (ps > ds_final);
                    phase_ = BjPhase::Comparing;
                    board_.state_action_executed_ = false;
                }
            }
            break;
        }

        case BjPhase::DealerTurn: {
            // Runs BEFORE PlayerTurn: dealer plays its ENTIRE hand
            // automatically first, only handing off if it stops under
            // 21 — busting or hitting 21 resolves immediately, player
            // never gets a turn. Hits while player!=21 AND (dealer<16
            // OR dealer<player) — more aggressive than "always hit
            // <17"; `ps` here is still the player's original 2-card
            // score. Waits for the dealer's going-first line to finish
            // before the hit-loop's own timer starts counting.
            if (dealer_audio_ != 0) {
                if (audio_.isPlaying(dealer_audio_)) {
                    break;
                }
                dealer_audio_ = 0;
                phase_time_ = now;
                break;
            }
            if (phase_ms >= 1000) {
                const int ps = PlayerScore();
                const int ds = ComputeHandValue(dealer_cards_, dealer_count_);
                const bool should_hit = (ps != 21) && (ds < 16 || ds < ps) && dealer_count_ < 5;
                if (should_hit) {
                    ++dealer_count_;  // reveal next pre-rolled card
                    phase_time_ = now;
                } else {
                    const int ds_final = ComputeHandValue(dealer_cards_, dealer_count_);
                    // dealer_hidden_ stays true here — revealed only
                    // once Comparing actually runs its resolve.
                    phase_time_ = now;
                    if (ds_final > 21) {
                        // Resolves immediately — player never
                        // acts. Comparing must wait for this
                        // audio's own done-callback before starting.
                        dealer_audio_ = playWav("W10\\BJI8.WAV", AudioFlags::Async);
                        // cards_revealed_ stays false the whole
                        // encounter — player never gets a turn, so
                        // its hand is never shown at all.
                        player_won_ = true;
                        phase_ = BjPhase::Comparing;
                        board_.state_action_executed_ = true;
                    } else if (ds_final == 21) {
                        // Dealer reaches 21 via hits (not an opening
                        // natural) — no intro line, just Comparing's
                        // generic-loss audio pool. cards_revealed_
                        // stays false here too; player cards never drawn.
                        player_won_ = false;
                        phase_ = BjPhase::Comparing;
                        phase_time_ = now;
                        board_.state_action_executed_ = false;
                    } else {
                        // Dealer stands under 21 — its stand/your-turn
                        // line (BJI9) hands off to the player's own
                        // interactive hit/stand turn. No win/tie decided yet.
                        // Cards stay hidden until this audio itself
                        // finishes (~3s later).
                        dealer_audio_ = playWav("W10\\BJI9.WAV", AudioFlags::Async);
                        cards_revealed_ = false;
                        phase_ = BjPhase::PlayerTurn;
                        phase_time_ = now;
                        clicked_ = false;
                    }
                }
            }
            break;
        }

        case BjPhase::Comparing: {
            // player_won_ is already fully resolved by the
            // transitioning phase — this phase only applies the stake
            // consequence (no separate tie/push outcome exists). Gates
            // on DealerTurn's bust-line audio (BJI8) if it stashed one;
            // the hits-21 path never sets dealer_audio_, so it skips
            // straight to the resolve below.
            if (dealer_audio_ != 0) {
                if (audio_.isPlaying(dealer_audio_)) break;
                dealer_audio_ = 0;
                board_.state_action_executed_ = false;
            }
            if (!board_.state_action_executed_) {
                board_.state_action_executed_ = true;
                result_audio_ = 0;
                // Dealer hole-card reveal happens here, at
                // result-resolve entry, as the very first action
                // — both the WIN and LOSS resolve paths draw the
                // dealer's 2 card slots at this exact point, never earlier.
                dealer_hidden_ = false;
                {
                    switch (stake_id_) {
                        case kStakeKey: {  // announce audio: BJI4
                            // Province-indexed win/loss voice line.
                            static const char* kBjKeyWinWav[6] = {
                                "W10\\BJI11.WAV", "W10\\BJI12.WAV", "W10\\BJI15.WAV",
                                "W10\\BJI16.WAV", "W10\\BJI14.WAV", "W10\\BJI13.WAV"};
                            static const char* kBjKeyLoseWav[6] = {
                                "W10\\BJI17.WAV", "W10\\BJI19.WAV", "W10\\BJI21.WAV",
                                "W10\\BJI22.WAV", "W10\\BJI20.WAV", "W10\\BJI18.WAV"};
                            if (player_won_) {
                                const int prov = board_.FindUnheldProvince(player_);
                                if (prov >= 0) {
                                    board_.GainKey(player_, prov);
                                    result_audio_ = playWav(kBjKeyWinWav[prov], AudioFlags::Async);
                                }
                            } else {
                                const int held = board_.FindHeldProvinceByRotation(player_);
                                if (held >= 0) {
                                    board_.LoseKey(player_, held);
                                    result_audio_ = playWav(kBjKeyLoseWav[held], AudioFlags::Async);
                                }
                            }
                            break;
                        }
                        case kStakePower30:  // announce audio BJI3: win +30, lose -10
                            if (player_won_)
                                board_.AddPower(player_, 30);
                            else
                                board_.SubtractPower(player_, 10);
                            break;
                        case kStakePower20:  // announce audio BJI2: win +20, lose -10
                            if (player_won_)
                                board_.AddPower(player_, 20);
                            else
                                board_.SubtractPower(player_, 10);
                            break;
                        case kStakeTurn:  // announce audio BJI1:
                                          // win -> extra turn (same mechanism as enc_type 6);
                                          // lose -> skip a turn.
                            if (player_won_) {
                                board_.DecrementCurrentPlayerCyclic();
                            } else {
                                board_.render_state_[player_].skip_turn_counter = 1;
                            }
                            break;
                    }
                }
                // The key stake (kStakeKey) already applied its own
                // province effect in the switch above and is excluded
                // here — this generic pool is for turn/+20/+30 only.
                if (stake_id_ != kStakeKey) {
                    if (player_won_) {
                        static const char* kBjWin[5] = {"W3\\AT_0428.WAV", "W4\\AT_0505.WAV",
                                                        "W2\\AT_0319.WAV", "W2\\AT_0322.WAV",
                                                        "W4\\AT_0480.WAV"};
                        result_audio_ =
                            playWav(kBjWin[RollIndex(board_.RngNext(), 5)], AudioFlags::Async);
                    } else {
                        // Generic loss pool, also covers the dealer-hits-21 case.
                        static const char* kBjLoss[4] = {"W3\\AT_0459.WAV", "W9\\AT_1000.WAV",
                                                         "W2\\AT_0333.WAV", "W3\\AT_0420.WAV"};
                        result_audio_ =
                            playWav(kBjLoss[RollIndex(board_.RngNext(), 4)], AudioFlags::Async);
                    }
                }
            }
            // Teardown fires in the same millisecond as the result
            // audio's own done-event — no extra hold. Go straight to
            // TurnEnd once the result audio finishes.
            const bool compare_done =
                result_audio_ != 0 ? !audio_.isPlaying(result_audio_) : phase_ms >= 500;
            if (compare_done && !board_.BathoryKeyRedirectAudioBusy()) {
                board_.current_state_ = BoardGameState::TurnEnd;
                board_.state_start_time_ = Clock::now();
                board_.state_action_executed_ = false;
            }
            break;
        }
    }
}

void BlackjackGame::render() {
    const bool announcing =
        phase_ == BjPhase::StakeAnnounce && board_.current_state_ == BoardGameState::BlackjackGame;
    if (announcing) {
        if (announce_tex_) {
            renderer_.drawBitmap(announce_tex_, 0, 0, 0, 0, 640, 480);
        } else {
            renderer_.fillRect(0, 0, 640, 480, 10, 8, 20);
        }
        if (stake_icon_tex_) {
            // BJBLIT.BMP (471x405) packs all 4 stake icons; each crop below
            // fits within those dimensions.
            struct BjStakeCrop {
                int sx, sy, sw, sh, dx, dy;
            };
            static const BjStakeCrop kBjStakeCrops[4] = {
                {2, 10, 326, 116, 168, 130},    // kStakeKey (BJI4)
                {180, 128, 178, 275, 255, 55},  // kStakePower30 (BJI3)
                {1, 128, 178, 275, 255, 55},    // kStakePower20 (BJI2)
                {329, 2, 125, 125, 257, 111},   // kStakeTurn, dice icon (BJI1)
            };
            const auto& c = kBjStakeCrops[stake_id_ >= 0 && stake_id_ < 4 ? stake_id_ : kStakeKey];
            renderer_.drawBitmap(stake_icon_tex_, c.dx, c.dy, c.sx, c.sy, c.sw, c.sh);
        } else {
            renderer_.fillRect(120, 200, 400, 80, 40, 40, 90);
        }
        // Gamepad focus indicator. Zones are the real BJACK1.BIN rects
        // (left=confirm, right=decline); the decline zone's visual
        // indicator is nudged 13px right to match the rendered art
        // (cosmetic-only, real hit-test rect stays exactly the BIN's
        // own data).
        if (board_.gamepad_mode_ && !stake_clicked_) {
            const auto& z = stake_zone_rects_[stake_focus_confirm_ ? 0 : 1];
            const int visual_dx = stake_focus_confirm_ ? 0 : 13;
            DrawFocusTint(renderer_, announce_sprite_, z.x1 + visual_dx, z.y1, z.x1 + visual_dx,
                          z.y1, z.x2 - z.x1, z.y2 - z.y1);
            DrawFocusCursor(renderer_, z.x1 + visual_dx + (z.x2 - z.x1) / 2,
                            z.y1 + (z.y2 - z.y1) / 2);
        }
    } else {
        if (table_tex_) {
            renderer_.drawBitmap(table_tex_, 0, 0, 0, 0, 640, 480);
        } else {
            renderer_.fillRect(0, 0, 640, 480, 10, 8, 20);
        }

        // Per-card blits — crops a 100x150 cell from the SUIT{1-4}.BMP
        // sheet (7 ranks per row, 2 rows). Dest x=65/168/271/371/474 per
        // slot 0-4; y=57 (dealer) / 273 (player).
        if (suit_tex_[0] || suit_tex_[1]) {
            static constexpr int kBjCardX[5] = {65, 168, 271, 371, 474};
            auto DrawBjCard = [&](int rank1to13, int slot, bool dealer) {
                if (rank1to13 < 1 || rank1to13 > 13 || slot < 0 || slot >= 5) return;
                const TextureHandle deck = dealer ? suit_tex_[1] : suit_tex_[0];
                if (!deck) return;
                const int rank0 = rank1to13 - 1;
                const int sx = (rank0 <= 6 ? rank0 : rank0 - 7) * 100;
                const int sy = (rank0 <= 6 ? 0 : 150);
                const int dx = kBjCardX[slot];
                const int dy = dealer ? 57 : 273;
                renderer_.drawBitmap(deck, dx, dy, sx, sy, 100, 150);
            };
            if (cards_revealed_) {
                // Per-slot, not contiguous — a hit zone can reveal slot 4 before slot 2 if
                // clicked out of order (each zone maps to a fixed slot).
                for (int i = 0; i < 5; ++i) {
                    if (player_confirmed_[i]) DrawBjCard(player_cards_[i], i, false);
                }
            }
            // Each dealer HIT card is drawn the instant it's dealt, at the
            // just-incremented slot index (first hit -> slot 2, the center). Only the
            // initial 2 cards (slots 0/1) stay hidden until Comparing.
            for (int i = 0; i < dealer_count_; ++i) {
                const bool hidden_slot = dealer_hidden_ && i < 2;
                if (!hidden_slot) DrawBjCard(dealer_cards_[i], i, true);
            }
        }

        // Gamepad focus indicator over the focused Hit/Stand zone. Pulse rather than
        // tint: these 4 zones are where cards land, tint would double-draw over any
        // already-revealed card there.
        if (board_.gamepad_mode_ && phase_ == BjPhase::PlayerTurn && cards_revealed_ &&
            focus_index_ >= 0 && focus_index_ < 4) {
            const auto& z = zone_rects_[focus_index_];
            const auto anchor_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       focus_change_time_.time_since_epoch())
                                       .count();
            DrawFocusPulse(renderer_, SpriteInfo{}, z.x1, z.y1, 0, 0, z.x2 - z.x1, z.y2 - z.y1,
                           anchor_ms, 2.2f);
            DrawFocusCursor(renderer_, (z.x1 + z.x2) / 2, (z.y1 + z.y2) / 2);
        }
    }
}

}  // namespace openfear::game
