#include "openfear/game/subscreens/DuelStealOrCurse.h"
#include "openfear/game/BoardGameScreen.h"
#include "openfear/game/FocusIndicator.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace openfear::game {

extern const int kKeyTable[6];

bool DuelStealOrCurse::LoadZoneRects() {
    return LoadRectZoneOrFail("getkeys", 9, getkeys_zone_rects_.data(),
                              "GETKEYS.BIN not loaded/wrong size — duel opponent picker would "
                              "never respond to clicks");
}

void DuelStealOrCurse::update(const IInput& input, float dt) {
    // Winner's post-gauge choice: steal a stealable key (zones
    // 0-5) or curse the loser (zone 6, always valid).
    //
    // This whole screen doesn't even mount until the loser-taunt
    // from DuelAttackGauge finishes — block the whole init (including
    // our own announce) on it.
    if (board_.duel_.lose_taunt_wav != 0 && audio_.isPlaying(board_.duel_.lose_taunt_wav)) {
        return;
    }
    if (!board_.state_action_executed_) {
        EnterStealOrCurse();
    }

    // Clicks/AI-decide are inert until the announce finishes — the
    // interactive timer only starts once that audio is done, not from
    // screen-open, so the 2000ms/1000ms countdown can't expire before
    // the player even gets a chance to see the screen.
    if (announce_wav_ != 0 && audio_.isPlaying(announce_wav_)) {
        return;
    }
    if (!timer_armed_) {
        timer_armed_ = true;
        board_.state_start_time_ =
            std::chrono::steady_clock::now();  // 2000ms window starts NOW, not at screen-open
    }

    if (UpdatePendingTurnEnd()) return;

    hover_mouse_x_ = input.mouse().x;
    hover_mouse_y_ = input.mouse().y;
    UpdateWinnerChoice(input, dt);
}

void DuelStealOrCurse::EnterStealOrCurse() {
    board_.state_action_executed_ = true;
    board_.duel_.steal_or_curse_zone = -1;
    board_.duel_.choice_ai_timer = 0.0f;
    timer_armed_ = false;
    pending_turnend_ = false;
    // Scans all 6 provinces for a stealable key (loser holds
    // it, winner doesn't) to pick the announce: stealable
    // exists (fixed clip), none (random 1-of-2).
    bool has_stealable = false;
    focus_ = 6;  // gamepad: default focus on the always-valid curse zone
    for (int k = 0; k < 6; ++k) {
        if (board_.render_state_[board_.duel_.loser].keys[k] &&
            !board_.render_state_[board_.duel_.winner].keys[k]) {
            has_stealable = true;
            focus_ = k;
            break;
        }
    }
    static const char* kNoStealWav[2] = {"W6\\AT_0627.WAV", "W6\\AT_0629.WAV"};
    const char* announce_wav =
        has_stealable ? "W6\\AT_0630.WAV" : kNoStealWav[RollIndex(board_.RngNext(), 2)];
    announce_wav_ = playWav(announce_wav, AudioFlags::Async);
}

void DuelStealOrCurse::ResolveZone(int zone) {
    // The chosen zone's highlight shows even for the AI's own
    // auto-resolved pick, not just a human hover.
    board_.duel_.steal_or_curse_zone = zone;
    if (zone == 6) {
        board_.ApplyCurse(board_.duel_.loser, board_.render_state_[board_.duel_.winner].char_id);
        // A switch on the just-cursed loser's OWN curse_type:
        // 1->W6/AT_0596, 2->W1/AT_0092, 4->W6/AT_0599,
        // 6->random(W6/AT_0600, W1/AT_0094); modes 3/5
        // (Khufu/Hellin) get no audio here.
        switch (board_.render_state_[board_.duel_.loser].curse_type) {
            case 1:
                curse_taunt_wav_ = playWav("W6\\AT_0596.WAV", AudioFlags::Async);
                break;
            case 2:
                curse_taunt_wav_ = playWav("W1\\AT_0092.WAV", AudioFlags::Async);
                break;
            case 4:
                curse_taunt_wav_ = playWav("W6\\AT_0599.WAV", AudioFlags::Async);
                break;
            case 6:
                curse_taunt_wav_ = playWav(RollIndex(board_.RngNext(), 2) == 0 ? "W6\\AT_0600.WAV"
                                                                               : "W1\\AT_0094.WAV",
                                           AudioFlags::Async);
                break;
            default:
                break;
        }
    } else {
        board_.LoseKey(board_.duel_.loser, zone);
        board_.GainKey(board_.duel_.winner, zone);
    }
    // Don't transition to TurnEnd in the same call that
    // started the curse taunt; defer it to the check below,
    // which waits for that audio (if any) to finish first.
    pending_turnend_ = true;
    board_.state_start_time_ = std::chrono::steady_clock::now();
}

bool DuelStealOrCurse::UpdatePendingTurnEnd() {
    if (!pending_turnend_) return false;
    if (curse_taunt_wav_ != 0) {
        if (audio_.isPlaying(curse_taunt_wav_)) return true;
    } else {
        // The real 2000ms hold-result pause applies whenever
        // no taunt audio was queued (steal, or a no-audio
        // curse mode) — not a decision-deadline, a
        // post-choice pause.
        const long long elapsed_hold =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                  board_.state_start_time_)
                .count();
        if (elapsed_hold < 2000) return true;
    }
    if (board_.BathoryKeyRedirectAudioBusy()) return true;
    pending_turnend_ = false;
    board_.current_state_ = BoardGameState::TurnEnd;
    board_.state_start_time_ = std::chrono::steady_clock::now();
    board_.state_action_executed_ = false;
    return true;
}

void DuelStealOrCurse::UpdateWinnerChoice(const IInput& input, float dt) {
    if (board_.render_state_[board_.duel_.winner].is_ai) {
        // AI decision: steal the first stealable key in kKeyTable rotation order
        // starting from the LOSER's own char_id (same rotation the key-pool
        // distribution/unheld-province lookups already use elsewhere), else curse.
        board_.duel_.choice_ai_timer += dt;
        if (board_.duel_.choice_ai_timer >= 1.0f) {
            const int loser_char_id = board_.render_state_[board_.duel_.loser].char_id;
            int steal_prov = -1;
            for (int i = 0; i < 6; ++i) {
                const int prov = (kKeyTable[i] + loser_char_id) % 6;
                if (board_.render_state_[board_.duel_.loser].keys[prov] &&
                    !board_.render_state_[board_.duel_.winner].keys[prov]) {
                    steal_prov = prov;
                    break;
                }
            }
            ResolveZone(steal_prov >= 0 ? steal_prov : 6);
        }
        return;
    }
    const auto& mouse = input.mouse();
    // Gamepad: D-pad focus nav over the 7 zones (6 key + 1
    // curse), A selects the focused zone if it's currently
    // valid (curse always is; a key zone only if stealable).
    // Explicit gate on both real blocking audios here, same
    // as render()'s own gate.
    bool gamepad_select = false;
    if (board_.gamepad_mode_ &&
        !(board_.duel_.lose_taunt_wav != 0 && audio_.isPlaying(board_.duel_.lose_taunt_wav)) &&
        !(announce_wav_ != 0 && audio_.isPlaying(announce_wav_))) {
        const auto& gamepad = input.gamepad();
        int dx = 0, dy = 0;
        if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadLeft)]) dx = -1;
        if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadRight)]) dx = 1;
        if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadUp)]) dy = -1;
        if (gamepad.pressed[static_cast<size_t>(GamepadButton::DpadDown)]) dy = 1;
        if (dx == 0 && dy == 0)
            board_.stick_repeater_.Poll(gamepad.left_x, gamepad.left_y, dt, dx, dy);
        // Filter to valid zones only (stealable key provinces
        // + the always-valid curse zone 6), so the D-pad
        // can't land the focus on a non-stealable province.
        if (dx != 0 || dy != 0) {
            std::vector<Point> focus_centers;
            std::vector<int> focus_provinces;
            int focus_current = 0;
            for (int prov = 0; prov < 7; ++prov) {
                const bool valid =
                    (prov == 6) || (board_.render_state_[board_.duel_.loser].keys[prov] &&
                                    !board_.render_state_[board_.duel_.winner].keys[prov]);
                if (!valid) continue;
                if (prov == focus_) focus_current = static_cast<int>(focus_provinces.size());
                const auto& z = getkeys_zone_rects_[prov];
                focus_provinces.push_back(prov);
                focus_centers.push_back({static_cast<int16_t>((z.x1 + z.x2) / 2),
                                         static_cast<int16_t>((z.y1 + z.y2) / 2)});
            }
            if (!focus_provinces.empty()) {
                const int focus_next = FindNextFocus(focus_centers, focus_current, dx, dy);
                focus_ = focus_provinces[focus_next];
            }
        }
        gamepad_select = gamepad.pressed[static_cast<size_t>(GamepadButton::A)];
    }

    if (mouse.clicked[static_cast<int>(MouseButton::Left)]) {
        // Real zones are the 3x2 key grid + curse-card region,
        // not evenly-spaced bands.
        const auto& curse_z = getkeys_zone_rects_[6];
        if (PointInRect(mouse.x, mouse.y, curse_z)) {
            ResolveZone(6);  // curse zone — always valid
        } else {
            for (int prov = 0; prov < 6; ++prov) {
                const auto& z = getkeys_zone_rects_[prov];
                if (PointInRect(mouse.x, mouse.y, z) &&
                    board_.render_state_[board_.duel_.loser].keys[prov] &&
                    !board_.render_state_[board_.duel_.winner].keys[prov]) {
                    ResolveZone(prov);
                    break;
                }
            }
        }
    } else if (gamepad_select) {
        if (focus_ == 6) {
            ResolveZone(6);
        } else if (focus_ >= 0 && focus_ < 6 &&
                   board_.render_state_[board_.duel_.loser].keys[focus_] &&
                   !board_.render_state_[board_.duel_.winner].keys[focus_]) {
            ResolveZone(focus_);
        }
    }
}

void DuelStealOrCurse::render() {
    // Real screen just crops getkeys_zone_rects_[7]/[8] (display-only rects, rect 7
    // if the ATTACKER lost else 8 — repositions the same art's broken element to
    // the losing side) onto the gauge screen's leftover surface. This renderer
    // clears every frame instead, so the port redraws the full background first.
    if (board_.duel_.bg_tex) {
        renderer_.drawBitmap(board_.duel_.bg_tex, 0, 0, 0, 0, 640, 480);
    } else {
        renderer_.fillRect(0, 0, 640, 480, 20, 5, 35);
    }
    if (board_.duel_.getkeys_tex) {
        const bool attacker_lost = (board_.duel_.loser == board_.duel_.attacker);
        const auto& bg = getkeys_zone_rects_[attacker_lost ? 7 : 8];
        renderer_.drawBitmap(board_.duel_.getkeys_tex, bg.x1, 0, bg.x1, 0, bg.x2 - bg.x1,
                             bg.y2 - bg.y1);
    }
    if (board_.duel_.sigil_tex && board_.duel_.attacker >= 0 && board_.duel_.defender >= 0) {
        const int atk_cid = board_.render_state_[board_.duel_.attacker].char_id;
        const int def_cid = board_.render_state_[board_.duel_.defender].char_id;
        renderer_.drawBitmap(board_.duel_.sigil_tex, 57, 225, atk_cid * 53, 0, 52, 68);
        renderer_.drawBitmap(board_.duel_.sigil_tex, 529, 225, def_cid * 53, 0, 52, 68);
    }
    // The gauges themselves persist into this screen too, frozen at
    // their last value from DuelAttackGauge — nothing resets them until
    // the next DuelAttackGauge::Enter().
    {
        constexpr int kGaugeXA = 133, kGaugeXA2 = 156, kGaugeYA = 38;
        constexpr int kGaugeXD = 482, kGaugeXD2 = 505, kGaugeYD = 37;
        constexpr int kGaugeMaxPxRender = 276;
        auto draw_gauge_frozen = [&](TextureHandle tex, int x1, int x2, int y_top, float norm01) {
            const int fill_px =
                std::clamp(static_cast<int>(norm01 * kGaugeMaxPxRender), 0, kGaugeMaxPxRender);
            const int h = kGaugeMaxPxRender - fill_px;
            if (tex && h > 0) {
                renderer_.drawBitmap(tex, x1, y_top + fill_px, 0, fill_px, x2 - x1, h);
            }
        };
        draw_gauge_frozen(board_.duel_.gauge_atk_tex, kGaugeXA, kGaugeXA2, kGaugeYA,
                          board_.duel_.gauge_atk);
        draw_gauge_frozen(board_.duel_.gauge_def_tex, kGaugeXD, kGaugeXD2, kGaugeYD,
                          board_.duel_.gauge_def);
    }
    if (board_.duel_.loser >= 0 && board_.duel_.loser < 6 && board_.duel_.winner >= 0 &&
        board_.duel_.winner < 6) {
        const auto& origin = getkeys_zone_rects_[0];
        // A stolen key needs no extra highlight code to drop it: once LoseKey/GainKey
        // run (in ResolveZone, before this render), loser.keys[k] is already false
        // for that province, so this loop naturally stops matching it next frame.
        for (int k = 0; k < 6; ++k) {
            if (!(board_.render_state_[board_.duel_.loser].keys[k] &&
                  !board_.render_state_[board_.duel_.winner].keys[k]))
                continue;
            const auto& z = getkeys_zone_rects_[k];
            const int w = z.x2 - z.x1, h = z.y2 - z.y1;
            if (board_.duel_.key_highlight_tex) {
                renderer_.drawBitmap(board_.duel_.key_highlight_tex, z.x1, z.y1, z.x1 - origin.x1,
                                     z.y1 - origin.y1, w, h);
            } else {
                renderer_.fillRect(z.x1, z.y1, w, h, 200, 180, 30);
            }
        }

        // Curse zone tracks a live hover highlight, human winners only —
        // the real AI decision is instant, so its 1s delay has nothing to
        // preview, just the final pick. hover_mouse_x_/y_ cached from
        // update() since render() has no IInput access.
        const bool winner_is_human = board_.duel_.winner >= 0 && board_.duel_.winner < 6 &&
                                     !board_.render_state_[board_.duel_.winner].is_ai;
        const auto& curse_hz = getkeys_zone_rects_[6];
        const bool curse_zone_hovered =
            winner_is_human && PointInRect(hover_mouse_x_, hover_mouse_y_, curse_hz);
        if (curse_zone_hovered || board_.duel_.steal_or_curse_zone == 6) {
            const int w = curse_hz.x2 - curse_hz.x1, h = curse_hz.y2 - curse_hz.y1;
            if (board_.duel_.key_highlight_tex) {
                renderer_.drawBitmap(board_.duel_.key_highlight_tex, curse_hz.x1, curse_hz.y1,
                                     curse_hz.x1 - origin.x1, curse_hz.y1 - origin.y1, w, h);
            } else {
                renderer_.fillRect(curse_hz.x1, curse_hz.y1, w, h, 200, 180, 30);
            }
        }

        // Gamepad focus: keys just get the cursor at zone center (their crop
        // cell is bigger than the icon, so a fill would light up empty margin);
        // curse also gets an alpha-reveal pulse (its crop tightly bounds the
        // art). Same interactive gate as update()'s own input handling.
        const bool duel_steal_or_curse_interactive =
            board_.state_action_executed_ && !pending_turnend_ &&
            !(board_.duel_.lose_taunt_wav != 0 && audio_.isPlaying(board_.duel_.lose_taunt_wav)) &&
            !(announce_wav_ != 0 && audio_.isPlaying(announce_wav_));
        if (board_.gamepad_mode_ && duel_steal_or_curse_interactive &&
            !board_.render_state_[board_.duel_.winner].is_ai && focus_ >= 0 && focus_ < 7) {
            if (focus_ == 6) {
                const auto now = std::chrono::steady_clock::now();
                const auto elapsed_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
                        .count();
                const float phase =
                    (static_cast<float>(elapsed_ms % 1000) / 1000.0f) * 2.0f * 3.14159265f;
                const uint8_t alpha =
                    static_cast<uint8_t>(140.0f + (0.5f + 0.5f * std::sin(phase)) * 115.0f);
                renderer_.drawSpriteAlpha(board_.duel_.key_highlight_sprite, curse_hz.x1,
                                          curse_hz.y1, curse_hz.x1 - origin.x1,
                                          curse_hz.y1 - origin.y1, curse_hz.x2 - curse_hz.x1,
                                          curse_hz.y2 - curse_hz.y1, alpha);
                DrawFocusCursor(renderer_, (curse_hz.x1 + curse_hz.x2) / 2,
                                (curse_hz.y1 + curse_hz.y2) / 2);
            } else {
                const auto& z = getkeys_zone_rects_[focus_];
                DrawFocusCursor(renderer_, (z.x1 + z.x2) / 2, (z.y1 + z.y2) / 2);
            }
        }
    }
}

}  // namespace openfear::game
