#include "openfear/game/subscreens/DuelChooseOpponent.h"
#include "openfear/game/BoardGameScreen.h"
#include "openfear/game/FocusIndicator.h"
#include <chrono>
#include <cstdio>
#include <vector>

namespace openfear::game {

// Fortune-card plaque table — real definition lives in BoardGameScreen.cpp
// (shared with BoardGameScreenDuelAndKeys.cpp's ResolveFortuneCard).
extern const char* kFortuneCard[6];

bool DuelChooseOpponent::LoadZoneRects() {
    return LoadRectZoneOrFail("choose", 6, choose_zone_rects_.data(),
                              "CHOOSE.BIN not loaded/wrong size — duel opponent picker would "
                              "never respond to clicks");
}

void DuelChooseOpponent::FreeProtectionCardTexture() {
    ReleaseTexture(protection_card_tex_);
}

bool DuelChooseOpponent::update(const IInput& input, float dt) {
    // Duel opponent picker: draws all players' portraits. Human clicks a
    // cell in choose_zone_rects_; AI auto-resolves via duel_.choose_ai_target
    // below instead, but still shows this screen (glass+announce play either way).
    if (!board_.state_action_executed_) {
        EnterChooseOpponent();
    }
    if (UpdateProtectionCardWait()) return false;
    // Clicks are inert until the pick-your-opponent announcement
    // finishes — the real interactive picker's own click-handling
    // subclass is only installed once that audio's done event arrives.
    if (board_.duel_.choose_prompt_wav != 0 && audio_.isPlaying(board_.duel_.choose_prompt_wav)) {
        return false;
    }
    if (UpdateAiAutoResolve(dt)) return false;
    if (UpdatePostPickWait()) return false;
    UpdateClickResolution(input, dt);
    return false;
}

void DuelChooseOpponent::EnterChooseOpponent() {
    board_.state_action_executed_ = true;
    picked_char_ = -1;
    picked_target_slot_ = -1;
    // Gamepad discrete-focus: start on the topmost-then-leftmost cell
    // (choose_zone_rects_' array order isn't visual order). All 6
    // cells are always valid here, so no availability filter needed.
    focus_ = 0;
    for (int zc = 0; zc < 6; ++zc) {
        const auto& z = choose_zone_rects_[zc];
        const auto& best = choose_zone_rects_[focus_];
        if (z.y1 < best.y1 || (z.y1 == best.y1 && z.x1 < best.x1)) focus_ = zc;
    }
}

bool DuelChooseOpponent::UpdateProtectionCardWait() {
    // Protection card+audio overlay wait — armed mid-screen (right after a
    // click resolves), holds here until the card's taunt audio finishes
    // before opening the duel screen.
    if (!protection_card_pending_) return false;
    const bool card_audio_done =
        (protection_card_wav_ == 0) || !audio_.isPlaying(protection_card_wav_);
    if (!card_audio_done) return true;
    protection_card_active_ = false;
    protection_card_pending_ = false;
    duel_attack_gauge_.Enter(protection_attacker_, protection_target_);
    board_.duel_.winner = protection_target_;
    board_.duel_.loser = protection_attacker_;
    // norm01=1 draws both tubes hollow/empty — no separate broken-gauge
    // asset exists; the broken look is the ATTACKL.BMP rect7/8 crop in
    // DuelStealOrCurse, same as any gauge-battle loser/winner pair.
    board_.duel_.gauge_atk = 1.0f;
    board_.duel_.gauge_def = 1.0f;
    board_.duel_.steal_or_curse_zone = -1;
    board_.duel_.choice_ai_timer = 0.0f;
    board_.current_state_ = BoardGameState::DuelStealOrCurse;
    board_.state_start_time_ = std::chrono::steady_clock::now();
    board_.state_action_executed_ = false;
    return true;
}

bool DuelChooseOpponent::UpdateAiAutoResolve(float dt) {
    // AI auto-resolve. The real AI vtable slot resolves the
    // target instantly (no modal wait), but a short delay avoids
    // the pick reading as an instant, jarring cut the moment the announce ends.
    if (board_.duel_.choose_ai_target == -1 || picked_char_ != -1) return false;
    board_.duel_.choice_ai_timer += dt;
    if (board_.duel_.choice_ai_timer >= 0.5f) {
        const int target_slot = board_.duel_.choose_ai_target;
        const int zone_char = board_.render_state_[target_slot].char_id;
        board_.duel_.choose_ai_target = -1;
        picked_char_ = zone_char;
        picked_target_slot_ = target_slot;
        audio_.playLoaded(board_.duel_.choose_glass_wav, AudioFlags::None);
    }
    return true;
}

bool DuelChooseOpponent::UpdatePostPickWait() {
    // Post-pick wait: a valid target was already clicked, broken
    // glass is showing over that cell, and glass audio is
    // playing. The real dispatcher only proceeds once THAT audio
    // finishes, not on the click itself.
    if (picked_char_ == -1) return false;
    if (audio_.isPlaying(board_.duel_.choose_glass_wav)) return true;
    const int target_slot = picked_target_slot_;
    picked_char_ = -1;
    picked_target_slot_ = -1;
    // Protection: if the target's fortune_card_targeted[attacker_char] is
    // set (they drew a FortuneCard naming THIS attacker), consume it and
    // auto-resolve role-reversed — target wins, attacker loses — skipping
    // the interactive gauge duel but still landing on DuelStealOrCurse.
    const int attacker_char = board_.render_state_[board_.current_player_].char_id;
    if (attacker_char >= 0 && attacker_char < 6 &&
        board_.render_state_[target_slot].fortune_card_targeted[attacker_char] != 0) {
        board_.render_state_[target_slot].fortune_card_targeted[attacker_char] = 0;
        // Shows the attacker's fortune-card overlay (same table as
        // ResolveFortuneCard's grant-side reveal) + 1-of-4 taunt audio
        // before the duel screen appears; protection_card_pending_ waits
        // for that audio, then jumps straight to DuelStealOrCurse with
        // the gauges preset to the broken/empty visual.
        static const char* kProtectionWav[4] = {"W6\\AT_0632.WAV", "W6\\AT_0633.WAV",
                                                "W6\\AT_0634.WAV", "W6\\AT_0635.WAV"};
        ReleaseTexture(protection_card_tex_);
        protection_card_tex_ = assets_.texture("FF3", kFortuneCard[attacker_char]);
        protection_card_active_ = (protection_card_tex_ != 0);
        protection_card_wav_ =
            playWav(kProtectionWav[RollIndex(board_.RngNext(), 4)], AudioFlags::Async);
        protection_attacker_ = board_.current_player_;
        protection_target_ = target_slot;
        protection_card_pending_ = true;
        return true;
    }
    duel_attack_gauge_.Enter(board_.current_player_, target_slot);
    return true;
}

void DuelChooseOpponent::UpdateClickResolution(const IInput& input, float dt) {
    // The clicked zone yields a CHAR_ID (0-5), resolved to a target PLAYER
    // by scanning for whoever currently holds it. char_id==attacker's own,
    // or no active player holding it, both hit the same cancel path.
    // Gamepad: D-pad focus nav over the 6 cells, A selects the focused one
    // (all 6 clickable, even self/invalid).
    int gp_zone_char = -1;
    if (board_.gamepad_mode_) {
        const auto& gamepad = input.gamepad();
        int dx = 0, dy = 0;
        if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadLeft)]) dx = -1;
        if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadRight)]) dx = 1;
        if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadUp)]) dy = -1;
        if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadDown)]) dy = 1;
        if (dx == 0 && dy == 0)
            board_.stick_repeater_.Poll(gamepad.left_x, gamepad.left_y, dt, dx, dy);
        if (dx != 0 || dy != 0) {
            std::vector<Point> focus_centers;
            for (const auto& z : choose_zone_rects_)
                focus_centers.push_back({static_cast<int16_t>((z.x1 + z.x2) / 2),
                                         static_cast<int16_t>((z.y1 + z.y2) / 2)});
            focus_ = FindNextFocus(focus_centers, focus_, dx, dy);
        }
        if (gamepad.pressed[static_cast<size_t>(GamepadButton::A)]) {
            gp_zone_char = focus_;
        }
    }

    const auto& mouse = input.mouse();
    if (mouse.clicked[static_cast<int>(MouseButton::Left)] || gp_zone_char >= 0) {
        for (int zone_char = 0; zone_char < 6; ++zone_char) {
            const auto& z = choose_zone_rects_[zone_char];
            const bool hit =
                gp_zone_char >= 0 ? (zone_char == gp_zone_char) : PointInRect(mouse.x, mouse.y, z);
            if (hit) {
                int target_slot = -1;
                if (zone_char != board_.render_state_[board_.current_player_].char_id) {
                    for (int i = 0; i < board_.total_players_; ++i) {
                        if (i == board_.current_player_ || board_.render_state_[i].eliminated)
                            continue;
                        if (board_.render_state_[i].char_id == zone_char) {
                            target_slot = i;
                            break;
                        }
                    }
                }
                if (target_slot == -1) {
                    // Cancel duel — self OR no active player has
                    // this char_id: resolves to "self" (a blank
                    // window) -> DecrementCurrentPlayerCyclic + teardown,
                    // no duel. Must NOT sound the precached glass
                    // on a self/invalid click.
                    if (board_.duel_.choose_glass_wav) audio_.stop(board_.duel_.choose_glass_wav);
                    board_.DecrementCurrentPlayerCyclic();
                    board_.current_state_ = BoardGameState::TurnEnd;
                    board_.state_start_time_ = std::chrono::steady_clock::now();
                    board_.state_action_executed_ = false;
                    return;
                }
                // Valid target found: draw broken glass over the
                // picked cell + play glass audio, then WAIT — both
                // fire together right at the click.
                picked_char_ = zone_char;
                picked_target_slot_ = target_slot;
                audio_.playLoaded(board_.duel_.choose_glass_wav, AudioFlags::None);
                return;
            }
        }
    }
}

void DuelChooseOpponent::render() {
    // Protection card overlay, drawn full-screen before anything else in this block.
    if (protection_card_active_ && protection_card_tex_) {
        renderer_.drawBitmap(protection_card_tex_, 0, 0, 0, 0, 640, 480);
        return;
    }
    bool char_active[6] = {};
    for (int i = 0; i < board_.total_players_; ++i) {
        if (i == board_.current_player_ || board_.render_state_[i].eliminated) continue;
        const int cid = board_.render_state_[i].char_id;
        if (cid >= 0 && cid < 6) char_active[cid] = true;
    }
    const int self_char = board_.render_state_[board_.current_player_].char_id;

    if (board_.duel_.choose_icons_tex) {
        renderer_.drawBitmap(board_.duel_.choose_icons_tex, 0, 0, 0, 0, 640, 480);
    } else {
        renderer_.fillRect(0, 0, 640, 480, 5, 5, 15);
    }
    for (int zone_char = 0; zone_char < 6; ++zone_char) {
        if (!char_active[zone_char] || zone_char == self_char) {
            const auto& z = choose_zone_rects_[zone_char];
            if (board_.duel_.choose_broken_tex) {
                renderer_.drawBitmap(board_.duel_.choose_broken_tex, z.x1, z.y1, z.x1, z.y1,
                                     z.x2 - z.x1, z.y2 - z.y1);
            } else {
                renderer_.fillRect(z.x1, z.y1, z.x2 - z.x1, z.y2 - z.y1, 0, 0, 0);
            }
        }
    }
    // Shatter the just-picked cell — the valid-target branch stamps broken glass over the
    // CHOSEN cell too (separate from the "ineligible" stamping above, same sprite),
    // visible while glass audio plays.
    if (picked_char_ >= 0 && picked_char_ < 6) {
        const auto& z = choose_zone_rects_[picked_char_];
        if (board_.duel_.choose_broken_tex) {
            renderer_.drawBitmap(board_.duel_.choose_broken_tex, z.x1, z.y1, z.x1, z.y1,
                                 z.x2 - z.x1, z.y2 - z.y1);
        } else {
            renderer_.fillRect(z.x1, z.y1, z.x2 - z.x1, z.y2 - z.y1, 0, 0, 0);
        }
    }
    const bool duel_choose_interactive =
        picked_char_ < 0 && board_.duel_.choose_ai_target == -1 &&
        !(board_.duel_.choose_prompt_wav != 0 && audio_.isPlaying(board_.duel_.choose_prompt_wav));
    if (board_.gamepad_mode_ && duel_choose_interactive && focus_ >= 0 && focus_ < 6) {
        const auto& z = choose_zone_rects_[focus_];
        DrawFocusPulse(renderer_, SpriteInfo{}, z.x1, z.y1, z.x1, z.y1, z.x2 - z.x1, z.y2 - z.y1);
        DrawFocusCursor(renderer_, (z.x1 + z.x2) / 2, (z.y1 + z.y2) / 2);
    }
}

}  // namespace openfear::game
