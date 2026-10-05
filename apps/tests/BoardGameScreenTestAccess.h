#pragma once

#include "openfear/game/BoardGameScreen.h"
#include "openfear/game/subscreens/DuelStealOrCurse.h"
#include <chrono>

namespace openfear::game {

// Test-only seam (see the `friend struct BoardGameScreenTestAccess;` in
// BoardGameScreen.h). Exposes exactly what headless tests need to reach
// without adding any of this to the real public API — minigame Enter()
// calls are normally only reachable via real movement through a real
// MAPFILE, which tests can't practically fake.
struct BoardGameScreenTestAccess {
    static void DispatchEncounter(BoardGameScreen& b, int player) { b.DispatchEncounter(player); }

    static MinimapScreen& Minimap(BoardGameScreen& b) { return b.minimap_screen_; }

    static BoardGameState& CurrentState(BoardGameScreen& b) { return b.current_state_; }

    static void SetStateActionExecuted(BoardGameScreen& b, bool value) {
        b.state_action_executed_ = value;
    }

    static SoundHandle BathoryKeyRedirectWav(BoardGameScreen& b) {
        return b.bathory_key_redirect_wav_;
    }

    static void GainKey(BoardGameScreen& b, int player, int province) {
        b.GainKey(player, province);
    }

    static bool BathoryBusy(BoardGameScreen& b) { return b.BathoryKeyRedirectAudioBusy(); }

    static DuelState& Duel(BoardGameScreen& b) { return b.duel_; }

    static DuelStealOrCurse& DuelStealOrCurseScreen(BoardGameScreen& b) {
        return b.duel_steal_or_curse_screen_;
    }

    static void ResolveDuelZone(DuelStealOrCurse& s, int zone) { s.ResolveZone(zone); }

    // Ages state_start_time_ backward so a state's own hold-pause/timeout
    // check (measured from it) reads as already elapsed, without a real
    // wall-clock wait.
    static void AgeStateStartTime(BoardGameScreen& b, int ms) {
        b.state_start_time_ -= std::chrono::milliseconds(ms);
    }

    static void ResolveKeyStoneArrival(BoardGameScreen& b, uint16_t province) {
        b.ResolveKeyStoneArrival(province);
    }

    static PlayerRenderState* RenderState(BoardGameScreen& b) { return b.render_state_; }

    static BoardGameScreen::DiceRollState& Dice(BoardGameScreen& b) { return b.dice_; }

    static uint8_t& MapfileDirection(BoardGameScreen& b) { return b.mapfile_direction_; }

    // Real turn-timeout check (update()'s top) reads this against
    // turn_time_limit_ms_ before any state's own click handling runs —
    // tests that jump current_state_ directly (skipping the real
    // RollingDice entry that normally sets this) must reset it too, or
    // the very first update() call reverts/ends the turn instead.
    static void ResetTurnStartTime(BoardGameScreen& b) {
        b.turn_start_time_ = std::chrono::steady_clock::now();
    }

    // MovementCooldownElapsed()'s gate: UpdateMoving's own state-entry init
    // sets this to "now" on the same call that processes the first click,
    // so a test needs to age it artificially to get past the cooldown.
    static void ExpireMovementCooldown(BoardGameScreen& b) {
        b.last_move_time_ = std::chrono::steady_clock::now() - std::chrono::seconds(5);
    }

    static BlackjackGame& Blackjack(BoardGameScreen& b) { return b.blackjack_screen_; }

    static int BlackjackStakeId(BlackjackGame& s) { return s.stake_id_; }

    static constexpr int kBlackjackStakeTurn = BlackjackGame::kStakeTurn;

    using WeightedPickEntry = BoardGameScreen::WeightedPickEntry;

    static void AppendLiteralWeightCategory(const int ids[], const float weights[], int count,
                                            int capacity, WeightedPickEntry* out, int& out_count) {
        BoardGameScreen::AppendLiteralWeightCategory(ids, weights, count, capacity, out, out_count);
    }
};

}  // namespace openfear::game
