#pragma once
#include <cstdint>

namespace openfear::game {

// Per-player runtime state, semantically mirroring the original's
// render_state struct. Offsets in comments refer to the original object's
// layout, kept for reference — this port uses semantic equivalence, not
// exact binary layout/padding.
struct PlayerRenderState {
    uint16_t char_id = 0;  // +0x02  character ID 0-5

    // 0=normal; 1-6=active curse, CAST onto a duel loser by ApplyCurse —
    // derived from the WINNER's char_id (not the winner's own mode) — when
    // the winner picks the "curse" zone, or the loser has no stealable key.
    // char_id<->mode: 0(Gevaudan)=1, 1(Khufu)=3, 2(Samedi)=4, 3(Bathory)=6,
    // 4(Anne)=2, 5(Hellin)=5.
    uint16_t curse_type = 0;  // +0x04

    uint16_t keys[6] = {};  // +0x66..+0x70  province key held per province (0=no, 1=yes)

    // Per-opponent flag, indexed by that opponent's char_id. Set when a
    // Fortune Card names an opponent as a target. Read by DuelChooseOpponent
    // when that target is later duel-attacked: if set, clears it (one-shot)
    // and skips the gauge duel entirely — the named player automatically
    // wins, the attacker automatically loses. The "Protection" effect.
    uint8_t fortune_card_targeted[6] = {};  // +0x16..+0x20 (word-stride array on disk)

    uint16_t home_avi_played = 0;  // +0x84  non-zero after HOME{n}.AVI has played once
    uint16_t eliminated = 0;       // +0xAE  non-zero = skip player in turn rotation
    uint16_t power = 0;  // +0xBC  power pool; human starts 25, AI starts ai_power, floor 0
    uint16_t dice_restricted = 0;  // +0xBE  1=forced 1-die, 2=normal
    uint16_t province = 0;         // +0xC0  current province 0-7
    uint16_t stone_id = 0;         // +0xC2  current stone index within province
    uint16_t direction = 0;        // +0xC4  MAPFILE direction: 0=N,1=E,2=S,3=W
    uint16_t win_flag = 0;         // +0xDE  set to 1 when player holds all 6 province keys
    // "The Chosen One" — set permanently on a double-sixes roll. Raises
    // ComputeEncounterSlotWeights()'s slot-1 and slot-3 weights, and makes
    // enc types 2/4/6 play a fixed insult instead of rolling from the
    // random pool.
    uint16_t chosen_one_flag = 0;  // +0xE0

    // "Lose a turn" flag (0 or 1) — the player's next turn is skipped
    // entirely, then this clears back to 0. Set by DispatchSkipNextTurn,
    // CoffinChoiceGame, a lost BlackjackGame turn-stake bet, or a
    // ResolveFateCard skip-turn variant.
    uint16_t skip_turn_counter = 0;  // +0xE2

    uint16_t fear_index = 0;  // +0xE6  player fear index (1-6, 1-indexed; 0=unset/immune)
                              //   gatekeeper roll: eliminated if RNG*6/0x8000 == fear_index-1

    // Turns elapsed under the 1-die restriction. Set to 1 alongside
    // dice_restricted by DispatchOneDieRestriction; +1 per completed
    // arrival while nonzero. Clears probabilistically (via
    // RollExtraTurnOrDiceEncounter, which also fires a skip-turn as a side
    // effect) or guaranteed once >6 (fixed audio, no skip-turn).
    uint16_t dice_restriction_turns = 0;  // +0xE8

    uint16_t even_dice = 0;  // +0xEA  1 = next roll must yield even result {2,4,6}
                             //   formula: RNG*3/0x8000*2+2; set by DispatchEvenDiceOnly

    // Black-hole escape-attempt counter. 0 = not stuck. Landing on a
    // black-hole stone sets it to 1 and plays the BHOLE AVI; each failed
    // escape attempt adds 1 and replays that AVI again instead of giving
    // the player a turn (ResolveBlackHoleRetry). Escape succeeds (resets
    // to 0) once it reaches 4, or earlier on a lucky roll. If the player
    // already holds the black-hole-immunity key (kKeyPowerBitmask bit
    // 0x10) at the moment they land, nothing happens at all — treated as
    // a normal stone, this counter never gets set. While already stuck,
    // another player can still challenge them to a duel on THEIR OWN
    // turn — target eligibility only checks the attacker's own counter,
    // never the target's. If the stuck player wins and the stolen key
    // happens to be that exact immunity key, GainKey clears the counter
    // immediately as an early escape.
    uint16_t black_hole_retry_counter = 0;  // +0xEC

    // Multi-purpose flags byte, one bit set/cleared per (char_id,
    // province) pair whenever that key is gained/lost, via a hardcoded
    // 36-byte permutation table (kKeyPowerBitmask, BoardGameScreenDuelAndKeys.cpp).
    //   bit0 (0x01) — holding your own starting province's key; gates
    //     province-7 hub tunnel entry (HandleWallDestination).
    //   bit1 (0x02) — "stop at any stone" key power: Space ends movement
    //     early (once >=1 step taken this roll), before the dice roll is
    //     exhausted.
    //   bit2 (0x04) — Flight key: an F-key press opens the province-teleport
    //     map mid-roll while standing on a type-6 (junction) stone.
    //   bit3 (0x08) / bit5 (0x20) — duel gauge max 125 vs 100 for attacker/defender.
    //   bit4 (0x10) — black-hole immunity: a black-hole landing does nothing (normal
    //     stone). ResolveFateCard's GateOpen penalty can't roll either — it's replaced
    //     by a skip-turn/power-penalty result instead.
    uint8_t key_power_flags = 0;  // +0x64

    // ── Port-only additions, no corresponding offset in the original ──

    bool is_ai = false;  // derived from PlayerObject::human_flag (is_ai = human_flag != 1)

    // Set when Bathory's curse (curse_type==6) redirects ANY GainKey call
    // away from the cursed player (unless Bathory already holds that
    // province's key). Makes the mode-6 cure offer reappear sooner
    // (TriggerEncounter's threshold drops from 10 to 0) until cured
    // (CureCurseGame::update's accept branch).
    bool lost_key_to_vampire = false;

    // Board-walk countdown during the Moving state: steps left to take this
    // roll, decremented by 1 per step until 0. Only current_player_ ever
    // moves.
    uint16_t steps_remaining = 0;

    // Frozen snapshot of steps_remaining taken at Moving-state entry.
    // (steps_total_this_roll - steps_remaining) = steps already taken this
    // roll. Two real uses: gates the "stop at any stone" key power (bit1
    // above), and gates the 180-degree turn on a dead-end/straight stone
    // (only allowed at 0 steps taken — a reversal is only possible right
    // after rolling, not mid-walk).
    uint16_t steps_total_this_roll = 0;

    // Turn-start position snapshot, reverted to on a genuine turn timeout.
    uint16_t province_backup = 0;
    uint16_t stone_backup = 0;
    uint16_t direction_backup = 0;

    // Curse-duration counter: while curse_type!=0, +1 per completed
    // movement step; resets to 0 on a cure (CureCurseGame) or whenever
    // ApplyCurse fires again (losing a duel while already cursed restarts
    // the counter). Gevaudan (mode 1) reads it capped at 3 for a portrait
    // animation stage; TriggerEncounter checks it against a per-mode
    // threshold to decide when to offer CureCurseGame instead of a normal
    // encounter roll.
    uint16_t curse_step_counter = 0;  // +0xF4
};

}  // namespace openfear::game
