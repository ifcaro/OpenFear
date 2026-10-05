#pragma once

#include "openfear/game/Subscreen.h"
#include <chrono>

namespace openfear::game {

class BoardGameScreen;

// Duel battle screen: two power gauges drained by a shared 100ms tick.
// board_.duel_ (attacker/defender/winner/loser, gauge fill ratios) is
// genuinely 3-way shared with DuelChooseOpponent (Protection bypass) and
// DuelStealOrCurse (reads winner/loser, re-renders the frozen fill).
class DuelAttackGauge : public Subscreen {
public:
    DuelAttackGauge(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
                    BoardGameScreen& board)
        : Subscreen(assets, renderer, audio), board_(board) {}

    // Loads DATA/GAUGE1.BIN and DATA/GAUGE2.BIN's rect 0 (each gauge's own
    // mouse-tracking half). Called once from BoardGameScreen::load().
    bool LoadZoneRects();

    // Initializes the Duel (attack) state machine. Called only from
    // DuelChooseOpponent once a target is picked (including the
    // Protection bypass path, which overrides the winner/loser/gauge
    // fields right after this returns).
    void Enter(int attacker, int defender);

    // current_state_ == DuelAttackGauge.
    void update(const IInput& input);
    void render();

private:
    // update()'s sub-steps, split out for readability — same side
    // effects/order as when this was inline.
    void EnterAttackGauge();
    // Handles the interactive attacker/defender cur_init picking phase
    // (setup_step_ < 2). Caller must `return` right after — matches the
    // original's own early return for this whole phase.
    void UpdateParticipantSetup(const IInput& input);
    // Starts (if needed) and polls the duel-announce entry audio. Returns
    // true once it's finished playing — caller must `return` if false.
    bool PlayDuelAnnounce();
    // One-shot: computes duel_rate_atk_/def_ from the committed cur_init
    // values, right after the announce finishes.
    void ComputeGaugeDeadlines();
    // Advances the tick-battle clock, updates the visible gauge fill, and
    // detects/resolves a drain (sets drain_pending_, picks winner/loser).
    void TickGaugeBattle();

    BoardGameScreen& board_;

    float duel_rate_atk_ = 0.0f;  // attacker's tick-count threshold to fully drain, set once
                                  // by ComputeGaugeDeadlines()
    float duel_rate_def_ = 0.0f;  // defender's tick-count threshold to fully drain (same)
    // Gauge setup sub-phase: each participant picks their gauge's initial
    // value BEFORE the automatic tick-battle. Human: polled every frame,
    // mouse Y sets the gauge's cur_init live (clamp(mouseY - gtop, 0, 275))
    // until they commit (click or any key). AI: skips straight to the RNG
    // formula. Sequential, not simultaneous: attacker
    // picks first, then defender (hotseat-style if both are human).
    int setup_step_ =
        0;  // 0=attacker picking, 1=defender picking, 2=done, proceed to auto-tick battle
    // A short grace period at the start of each human participant's own
    // gauge-picking step, so player 2's setup doesn't register player 1's
    // tail-end click/keypress as an instant accidental commit. Port-only
    // UX addition.
    std::chrono::steady_clock::time_point step_start_time_;
    float cur_init_atk_ = 0.0f;     // attacker's chosen/rolled cur_init (0..275)
    float cur_init_def_ = 0.0f;     // defender's chosen/rolled cur_init (0..275)
    bool setup_committed_ = false;  // current step's participant has clicked/confirmed
    // Set the frame TickGaugeBattle() detects a drain; the actual
    // current_state_ switch to DuelStealOrCurse happens one frame later
    // (next update() call), so render() shows the final tick's values at
    // least once before freezing.
    bool drain_pending_ = false;
    // True once each step's setup-announce insult has played this duel —
    // AI's equivalent never plays it, so only human participants hear it.
    // [0]=attacker, [1]=defender.
    bool insult_played_[2] = {false, false};
    // Last-frame timestamp for the gamepad stick's continuous cur_init
    // drive (genuinely continuous, needs its own dt source).
    std::chrono::steady_clock::time_point stick_tick_time_;
    SoundHandle announce_wav_ = 0;        // Duel entry announce handle — plays only after
                                          // both participants finish picking; the tick
                                          // battle then waits for THIS to finish (4
                                          // distinct sequential phases: P1 selection, P2
                                          // selection, entry audio, duel).
    SpriteInfo atk_sprite_{};             // board_.duel_.gauge_atk_tex wrapped for DrawFocusTint
    SpriteInfo def_sprite_{};             // board_.duel_.gauge_def_tex wrapped for DrawFocusTint
    assets::RectEntry gauge_atk_zone_{};  // DATA/GAUGE1.BIN rect 0
    assets::RectEntry gauge_def_zone_{};  // DATA/GAUGE2.BIN rect 0
};

}  // namespace openfear::game
