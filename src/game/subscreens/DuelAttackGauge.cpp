#include "openfear/game/subscreens/DuelAttackGauge.h"
#include "openfear/game/BoardGameScreen.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace openfear::game {

bool DuelAttackGauge::LoadZoneRects() {
    auto rects1 = assets_.rect_file("gauge1");
    auto rects2 = assets_.rect_file("gauge2");
    if (!rects1 || rects1->empty() || !rects2 || rects2->empty()) {
        std::fprintf(stderr, "[DuelAttackGauge] ERROR: Failed to load GAUGE1.BIN/GAUGE2.BIN\n");
        return false;
    }
    gauge_atk_zone_ = (*rects1)[0];
    gauge_def_zone_ = (*rects2)[0];
    return true;
}

void DuelAttackGauge::Enter(int attacker, int defender) {
    // Lazy-load — DuelChooseOpponent calls this from 2 sites (normal pick and the
    // Protection bypass), human or AI alike; assets load only once an actual duel
    // is starting, never at board init.
    if (board_.duel_.bg_tex == 0) {
        board_.duel_.bg_tex = assets_.texture("A1", "ATTACKB");
        board_.duel_.getkeys_tex = assets_.texture("A1", "ATTACKL");
        board_.duel_.key_highlight_tex = assets_.texture("A1", "ATTACKC");
        board_.duel_.key_highlight_sprite =
            renderer_.createSprite(board_.duel_.key_highlight_tex);  // for DrawFocusTint
        board_.duel_.sigil_tex = assets_.texture("A1", "ATTACK2C");
        board_.duel_.gauge_atk_tex = assets_.texture("A1", "G1");
        board_.duel_.gauge_def_tex = assets_.texture("A1", "G2");
        atk_sprite_ = renderer_.createSprite(board_.duel_.gauge_atk_tex);  // for DrawFocusTint
        def_sprite_ = renderer_.createSprite(board_.duel_.gauge_def_tex);
    }
    // announce_wav_/duel_.choose_glass_wav/duel_.choose_prompt_wav each only (re)create
    // behind `if (handle==0)` and only free in the screen's full unload() — without this
    // reset, a 2nd+ duel would see a stale-nonzero handle and skip its announce entirely.
    // duel_.lose_taunt_wav has no such guard (reassigned unconditionally at its own use
    // site), so releasing it here is just hygiene.
    ReleaseAudio(announce_wav_);
    ReleaseAudio(board_.duel_.lose_taunt_wav);
    ReleaseAudio(board_.duel_.choose_glass_wav);
    ReleaseAudio(board_.duel_.choose_prompt_wav);
    insult_played_[0] = false;
    insult_played_[1] = false;
    board_.duel_.attacker = attacker;
    board_.duel_.defender = defender;
    board_.duel_.winner = -1;
    board_.duel_.loser = -1;
    board_.duel_.gauge_atk = 0.0f;
    board_.duel_.gauge_def = 0.0f;
    duel_rate_atk_ = 0.5f;
    duel_rate_def_ = 0.5f;
    board_.current_state_ = BoardGameState::DuelAttackGauge;
    board_.state_start_time_ = std::chrono::steady_clock::now();
    board_.state_action_executed_ = false;
}

void DuelAttackGauge::update(const IInput& input) {
    // Duel gauge battle: both tubes drain toward empty on a shared 100ms
    // tick, started once the entry announce finishes. Setup picks each
    // participant's `cur_init` fill level (human: mouse Y / stick; AI:
    // RNG*40/32768+60); ComputeGaugeDeadlines() turns that plus char power
    // into a `deadline` tick count (25% bonus via key_power_flags
    // bit3/bit5); TickGaugeBattle() advances each tube until one crosses
    // its own deadline — see that function for the real tick formula used.
    //
    // Protection does NOT skip the gauge system entirely — both normal
    // duel and Protection funnel into the identical object-construction
    // chain. Ported: fortune-card overlay + 1-of-4 taunt audio before the
    // steal screen, both gauges forced hollow/empty (see
    // protection_card_pending_'s header doc comment) — only the LOSER's
    // side then gets the broken-glass look, via the already-wired
    // ATTACKL.BMP rect7/8 crop drawn in DuelStealOrCurse; the winner's
    // tube just reads as a normal empty gauge.
    if (!board_.state_action_executed_) {
        EnterAttackGauge();
    }

    // Setup sub-phase: each participant picks (human, mouse-Y) or
    // rolls (AI, RNG) their own cur_init BEFORE the automatic
    // tick-battle below starts (each human's own taunt plays during
    // their own step here — the duel-announce entry audio below is
    // separate, and only starts once BOTH participants commit).
    if (setup_step_ < 2) {
        UpdateParticipantSetup(input);
        return;
    }

    // The duel-announce entry audio plays AFTER both participants
    // finish picking. Play once, then wait for it to finish before
    // starting the battle clock — board_.state_start_time_ only
    // resets once this finishes, and TickGaugeBattle() derives its
    // tick count from that reset point every frame after. Gauges
    // stay frozen at their picked height while it plays.
    if (!PlayDuelAnnounce()) {
        return;  // still playing — render's !setup_committed_ branch shows cur_init
    }

    if (!setup_committed_) {
        ComputeGaugeDeadlines();
    }

    // If last frame already detected drain, do the actual state
    // transition NOW (one frame later) — last frame's render() call
    // (which already ran with current_state_ still DuelAttackGauge)
    // is what finally showed the final tick's values on screen once
    // before DuelStealOrCurse freezes on them.
    if (drain_pending_) {
        drain_pending_ = false;
        board_.current_state_ = BoardGameState::DuelStealOrCurse;
        board_.state_start_time_ = std::chrono::steady_clock::now();
        board_.state_action_executed_ = false;
        return;
    }

    TickGaugeBattle();
}

void DuelAttackGauge::EnterAttackGauge() {
    board_.state_action_executed_ = true;
    setup_step_ = 0;
    setup_committed_ = false;
    step_start_time_ = std::chrono::steady_clock::now();
    // Gauges start EMPTY (percent arg 0 at initial draw,
    // cur=max reads as EMPTY). Real behavior for mouse/touch.
    // Gamepad only: start at the middle instead, since there's
    // no mouse position to anchor to and starting fully empty
    // means several full-speed stick pushes just to reach a usable range.
    const float gauge_start = board_.gamepad_mode_ ? 138.0f : 276.0f;
    cur_init_atk_ = gauge_start;
    cur_init_def_ = gauge_start;
    board_.duel_.gauge_atk = 0.0f;
    board_.duel_.gauge_def = 0.0f;
    insult_played_[0] = false;
    insult_played_[1] = false;
    stick_tick_time_ = std::chrono::steady_clock::now();
}

void DuelAttackGauge::UpdateParticipantSetup(const IInput& input) {
    {
        const int participant = (setup_step_ == 0) ? board_.duel_.attacker : board_.duel_.defender;
        float& cur_init = (setup_step_ == 0) ? cur_init_atk_ : cur_init_def_;
        // Real widget rects:
        // GAUGE1.BIN (attacker) rect#2 = (133,38)-(156,313)
        // GAUGE2.BIN (defender) rect#2 = (482,37)-(505,312)
        // both height 276 (max_px).
        const int gtop = (setup_step_ == 0) ? 38 : 37;

        if (board_.render_state_[participant].is_ai) {
            // No modal wait for AI.
            cur_init = board_.RngNext() * 40.0f / 32768.0f + 60.0f;
            setup_step_++;
            setup_committed_ = false;
            step_start_time_ = std::chrono::steady_clock::now();
        } else {
            // Human: mouse Y tracks cur_init live (cur = clamp(mouseY -
            // gauge_top_y, 0, 275)). Commit is keyboard-only in the real
            // game (any key; mouse clicks are no-ops) — mouse click ALSO
            // commits here as a port-only convenience.
            // Per-participant taunt (kDuelAttackerWav/kDuelDefenderWav
            // below) plays once per human, right when their step starts.
            if (!insult_played_[setup_step_]) {
                insult_played_[setup_step_] = true;
                static const char* kDuelAttackerWav[6] = {
                    "W7\\AT_0648.WAV", "W7\\AT_0649.WAV", "W7\\AT_0650.WAV",
                    "W7\\AT_0651.WAV", "W7\\AT_0652.WAV", "W7\\AT_0653.WAV",
                };
                static const char* kDuelDefenderWav[7] = {
                    "W7\\AT_0654.WAV", "W7\\AT_0655.WAV", "W7\\AT_0656.WAV", "W7\\AT_0657.WAV",
                    "W7\\AT_0658.WAV", "W7\\AT_0659.WAV", "W7\\AT_0660.WAV",
                };
                // Real hardware has a single audio stream — the
                // defender's taunt implicitly cuts the attacker's
                // if it was still playing.
                audio_.stop();
                if (setup_step_ == 0) {
                    playWav(kDuelAttackerWav[RollIndex(board_.RngNext(), 6)], AudioFlags::Async);
                } else {
                    playWav(kDuelDefenderWav[RollIndex(board_.RngNext(), 7)], AudioFlags::Async);
                }
            }

            // The gauge only tracks mouse-move within its own screen half
            // (DATA/GAUGE1.BIN / DATA/GAUGE2.BIN rect 0) - outside that
            // half the mouse-move is silently ignored.
            const auto& ms_gauge = input.mouse();
            const bool in_gauge_half = PointInRect(
                ms_gauge.x, ms_gauge.y, (setup_step_ == 0) ? gauge_atk_zone_ : gauge_def_zone_);
            if (!board_.gamepad_mode_ && in_gauge_half) {
                cur_init = std::clamp(static_cast<float>(ms_gauge.y - gtop), 0.0f, 275.0f);
            }
            // Gamepad: left stick Y drives cur_init continuously
            // — the one legitimate free-cursor/continuous-value
            // case, same model as ChooseLevelScreen's difficulty
            // gauge. Port-only UX addition, no ASM equivalent.
            if (board_.gamepad_mode_) {
                const auto now_gauge = std::chrono::steady_clock::now();
                const float dt_gauge =
                    std::chrono::duration<float>(now_gauge - stick_tick_time_).count();
                stick_tick_time_ = now_gauge;
                constexpr float kGaugeStickSpeed = 300.0f;  // px/sec, matches ChooseLevelScreen
                // Wide deadzone (0.3) + quantized ±1 direction, not analog-scaled speed —
                // kGaugeStickSpeed is a flat rate already, and a narrow deadzone would read
                // a resting controller's nonzero drift as a constant push for the whole duel.
                // D-pad (already ±1, digital) drives it the same way.
                constexpr float kStickDeadzone = 0.3f;
                const float raw_left_y = input.gamepad().left_y;
                float stick_dir = 0.0f;
                if (raw_left_y > kStickDeadzone)
                    stick_dir = 1.0f;
                else if (raw_left_y < -kStickDeadzone)
                    stick_dir = -1.0f;
                const float ly = stick_dir + static_cast<float>(input.gamepad().dpad_y);
                cur_init = std::clamp(cur_init + ly * kGaugeStickSpeed * dt_gauge, 0.0f, 275.0f);
            }
            const auto& kb_gauge = input.keyboard();
            bool any_key_gauge = false;
            for (bool p : kb_gauge.pressed) {
                if (p) {
                    any_key_gauge = true;
                    break;
                }
            }
            // Click-to-commit is a port-only convenience (real GAUGEWINPROC's mouse-down
            // handlers are no-ops) — gated by the same active-half check as mouse-move
            // above, so a click can't commit a stale cur_init from outside the zone.
            // Keyboard commit stays unrestricted, matching the real handler.
            const bool click_gauge =
                in_gauge_half && (ms_gauge.clicked[static_cast<int>(MouseButton::Left)] ||
                                  ms_gauge.clicked[static_cast<int>(MouseButton::Right)]);
            // Gamepad A also commits, same additive-convenience
            // pattern as the mouse-click addition (real ASM is keyboard-only).
            const bool gamepad_commit_gauge =
                board_.gamepad_mode_ &&
                input.gamepad().pressed[static_cast<size_t>(GamepadButton::A)];
            // Ignore commit input for the first 600ms of this
            // participant's own step, so the next participant's picking
            // doesn't instantly register a click/key/button still
            // "in flight" from the previous one's own commit the moment before.
            const long long step_elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - step_start_time_)
                    .count();
            if ((any_key_gauge || click_gauge || gamepad_commit_gauge) && step_elapsed_ms >= 600) {
                setup_step_++;
                setup_committed_ = false;
                step_start_time_ = std::chrono::steady_clock::now();
            }
        }
    }
}

bool DuelAttackGauge::PlayDuelAnnounce() {
    if (announce_wav_ == 0) {
        // Same real-hardware single-stream cutoff as the
        // attacker/defender taunt pair.
        audio_.stop();
        static const char* kDuelAnnounceWav[4] = {
            "W7\\AT_0661.WAV",
            "W7\\AT_0662.WAV",
            "W7\\AT_0663.WAV",
            "W7\\AT_0664.WAV",
        };
        announce_wav_ =
            playWav(kDuelAnnounceWav[RollIndex(board_.RngNext(), 4)], AudioFlags::Async);
    }
    return !audio_.isPlaying(announce_wav_);
}

void DuelAttackGauge::ComputeGaugeDeadlines() {
    // First update() call after the announce finishes: computes
    // `deadline` from the chosen cur_init values. Reuses
    // setup_committed_ as a one-shot latch.
    setup_committed_ = true;
    const float pow_atk = static_cast<float>(board_.render_state_[board_.duel_.attacker].power);
    const float pow_def = static_cast<float>(board_.render_state_[board_.duel_.defender].power);
    const float max100_atk =
        (board_.render_state_[board_.duel_.attacker].key_power_flags & 0x08) ? 125.0f : 100.0f;
    const float max100_def =
        (board_.render_state_[board_.duel_.defender].key_power_flags & 0x20) ? 125.0f : 100.0f;
    constexpr float kGaugeMaxPxInit = 276.0f;
    auto fillpx = [&](float cur, float power, float max100) -> float {
        if (cur < 0.0f) return power * max100 / 100.0f;
        if (cur >= kGaugeMaxPxInit) return 0.0f;
        return (kGaugeMaxPxInit - cur) * power * max100 / (kGaugeMaxPxInit * 100.0f);
    };
    duel_rate_atk_ = fillpx(cur_init_atk_, pow_atk, max100_atk);
    duel_rate_def_ = fillpx(cur_init_def_, pow_def, max100_def);
    board_.state_start_time_ = std::chrono::steady_clock::now();  // tick clock starts now
}

void DuelAttackGauge::TickGaugeBattle() {
    constexpr float kGaugeMaxPx = 276.0f;
    const float pow_atk = static_cast<float>(board_.render_state_[board_.duel_.attacker].power);
    const float pow_def = static_cast<float>(board_.render_state_[board_.duel_.defender].power);
    using ClockAG = std::chrono::steady_clock;
    // The real redraw only happens on each 100ms timer fire — 10
    // discrete updates/sec, not per-frame interpolation.
    // Quantize to whole ticks so cur only changes once every
    // 100ms, holding steady between.
    const float elapsed_ticks =
        std::floor(std::chrono::duration<float>(ClockAG::now() - board_.state_start_time_).count() *
                   10.0f);  // 100ms/tick, quantized

    // Linear interpolation from cur_init to kGaugeMaxPx over `deadline`
    // ticks — matches the real drain duration exactly, even though a
    // byte-matched per-tick formula couldn't be reconciled with starting
    // anywhere but cur_init=max. Uses kGaugeMaxPx (not kGaugeMaxPx-1) as
    // the drain endpoint — the real ASM's `cur` lands EXACTLY at 276 on
    // its final tick, no one-pixel short overshoot.
    auto tick_cur = [&](float deadline, float power, float cur_init) -> float {
        (void)power;
        if (deadline <= 0.0f) return kGaugeMaxPx;
        const float t_frac = std::clamp(elapsed_ticks / deadline, 0.0f, 1.0f);
        const float new_cur = cur_init + (kGaugeMaxPx - cur_init) * t_frac;
        return std::clamp(new_cur, 0.0f, kGaugeMaxPx);
    };
    const float cur_atk = tick_cur(duel_rate_atk_, pow_atk, cur_init_atk_);
    const float cur_def = tick_cur(duel_rate_def_, pow_def, cur_init_def_);
    board_.duel_.gauge_atk = cur_atk / kGaugeMaxPx;
    board_.duel_.gauge_def = cur_def / kGaugeMaxPx;

    // Compares elapsed_ticks directly against each deadline, not the derived
    // cur_atk/cur_def against kGaugeMaxPx. cur_* is reached via a subtract-
    // then-add chain that can miss kGaugeMaxPx by a float ULP when cur_init
    // isn't round (e.g. AI's RNG-rolled ~92.4f), leaving that participant
    // stuck undrained. elapsed_ticks vs deadline has no such chain — 2
    // independently-computed values, direct comparison, nothing to round short.
    const bool atk_drained = elapsed_ticks >= duel_rate_atk_;
    const bool def_drained = elapsed_ticks >= duel_rate_def_;
    if (atk_drained || def_drained) {
        if (atk_drained != def_drained) {
            board_.duel_.winner = atk_drained ? board_.duel_.defender : board_.duel_.attacker;
            board_.duel_.loser = atk_drained ? board_.duel_.attacker : board_.duel_.defender;
        } else {
            // Both drained the same frame (float tie, sub-tick
            // granularity). The real timer checks the defender
            // first — on a genuine tie the defender is recorded
            // as "reached max" first, so the attacker wins ties.
            board_.duel_.winner = board_.duel_.attacker;
            board_.duel_.loser = board_.duel_.defender;
        }
        // Duel-loss taunt — 12-clip pool keyed by the LOSER's
        // char_id (2 lines per character, scrambled string-table
        // order, not sequential).
        static const char* kDuelLoseWav[12] = {
            "W7\\AT_0665.WAV", "W7\\AT_0671.WAV", "W7\\AT_0667.WAV", "W7\\AT_0673.WAV",
            "W7\\AT_0666.WAV", "W7\\AT_0672.WAV", "W7\\AT_0670.WAV", "W7\\AT_0676.WAV",
            "W7\\AT_0669.WAV", "W7\\AT_0675.WAV", "W7\\AT_0668.WAV", "W7\\AT_0674.WAV",
        };
        const int loser_char_id = board_.render_state_[board_.duel_.loser].char_id;
        const int lose_idx = std::clamp(loser_char_id, 0, 5) * 2 + RollIndex(board_.RngNext(), 2);
        board_.duel_.lose_taunt_wav = playWav(kDuelLoseWav[lose_idx], AudioFlags::Async);

        // Winner chooses steal-vs-curse. Zone 6 (curse) is ALWAYS
        // a valid choice, even when the loser holds no stealable key.
        board_.duel_.steal_or_curse_zone = -1;
        board_.duel_.choice_ai_timer = 0.0f;
        // Don't transition current_state_ here — winner/loser and
        // the taunt audio are decided now; only the actual screen
        // switch waits one more frame so this frame's render()
        // still shows the final tick at DuelAttackGauge.
        drain_pending_ = true;
    }
}

void DuelAttackGauge::render() {
    if (board_.duel_.bg_tex) {
        renderer_.drawBitmap(board_.duel_.bg_tex, 0, 0, 0, 0, 640, 480);
    } else {
        renderer_.fillRect(0, 0, 640, 480, 20, 5, 35);
    }
    // Combatant sigils — real dest positions, sourced from
    // DATA/ATTACK.BIN. Persists visually across
    // DuelAttackGauge/DuelStealOrCurse (never cleared between states) —
    // drawn in both.
    if (board_.duel_.sigil_tex && board_.duel_.attacker >= 0 && board_.duel_.defender >= 0) {
        const int atk_cid = board_.render_state_[board_.duel_.attacker].char_id;
        const int def_cid = board_.render_state_[board_.duel_.defender].char_id;
        renderer_.drawBitmap(board_.duel_.sigil_tex, 57, 225, atk_cid * 53, 0, 52, 68);
        renderer_.drawBitmap(board_.duel_.sigil_tex, 529, 225, def_cid * 53, 0, 52, 68);
    }
    // Vertical thermometers — fill drawn top-to-bottom.
    constexpr int kGaugeXA = 133, kGaugeXA2 = 156, kGaugeYA = 38;
    constexpr int kGaugeXD = 482, kGaugeXD2 = 505, kGaugeYD = 37;
    constexpr int kGaugeMaxPxRender = 276;

    // `fill_px` is where the fill STARTS (distance from the tube's top),
    // extending down to the bottom — 0 = full tube, kGaugeMaxPxRender =
    // empty. `tint_sprite`, if set, draws the currently-picking
    // participant's fill via DrawFocusTint instead of a plain blit.
    auto draw_gauge = [&](TextureHandle tex, int x1, int x2, int y_top, int fill_px, int fb_r,
                          int fb_g, int fb_b, const SpriteInfo* tint_sprite = nullptr) {
        fill_px = std::clamp(fill_px, 0, kGaugeMaxPxRender);
        const int w = x2 - x1;
        const int h = kGaugeMaxPxRender - fill_px;
        if (tex) {
            if (h > 0) {
                if (tint_sprite) {
                    DrawFocusTint(renderer_, *tint_sprite, x1, y_top + fill_px, 0, fill_px, w, h);
                } else {
                    renderer_.drawBitmap(tex, x1, y_top + fill_px, 0, fill_px, w, h);
                }
            }
        } else {
            renderer_.fillRect(x1, y_top, w, kGaugeMaxPxRender, fb_r / 3, fb_g / 3, fb_b / 3);
            if (h > 0) renderer_.fillRect(x1, y_top + fill_px, w, h, fb_r, fb_g, fb_b);
        }
    };

    if (!setup_committed_) {
        // Setup sub-phase AND the announce-wait right after — both
        // thermometers frozen at their current cur_init, fed straight into
        // draw_gauge as `fill_px`: mouse near the top (small cur_init) ->
        // nearly FULL preview; mouse near the bottom (cur_init near 276) ->
        // nearly EMPTY.
        const int fill_atk = static_cast<int>(std::clamp(cur_init_atk_, 0.0f, 276.0f));
        const int fill_def = static_cast<int>(std::clamp(cur_init_def_, 0.0f, 276.0f));
        // Tint only the CURRENTLY-PICKING participant's tube — the
        // other side is inert during this step, not
        // gamepad-controllable, so it draws plain.
        const bool tint_atk = board_.gamepad_mode_ && setup_step_ == 0 &&
                              !board_.render_state_[board_.duel_.attacker].is_ai;
        const bool tint_def = board_.gamepad_mode_ && setup_step_ == 1 &&
                              !board_.render_state_[board_.duel_.defender].is_ai;
        draw_gauge(board_.duel_.gauge_atk_tex, kGaugeXA, kGaugeXA2, kGaugeYA, fill_atk, 220, 30, 30,
                   tint_atk ? &atk_sprite_ : nullptr);
        draw_gauge(board_.duel_.gauge_def_tex, kGaugeXD, kGaugeXD2, kGaugeYD, fill_def, 30, 30, 220,
                   tint_def ? &def_sprite_ : nullptr);
        return;
    }

    // Auto-tick battle phase. duel_.gauge_atk/def_ (normalized 0..1) feed
    // draw_gauge as fill_px directly, no inversion: the fill always
    // extends from fill_px down to the tube's bottom (see draw_gauge
    // above), so as a participant nears losing (gauge->1, fill_px->max)
    // that extent shrinks toward zero height, bottom edge pinned in place.
    const int bar_atk = static_cast<int>(board_.duel_.gauge_atk * kGaugeMaxPxRender);
    draw_gauge(board_.duel_.gauge_atk_tex, kGaugeXA, kGaugeXA2, kGaugeYA, bar_atk, 220, 30, 30);
    const int bar_def = static_cast<int>(board_.duel_.gauge_def * kGaugeMaxPxRender);
    draw_gauge(board_.duel_.gauge_def_tex, kGaugeXD, kGaugeXD2, kGaugeYD, bar_def, 30, 30, 220);
}

}  // namespace openfear::game
