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

// GateOpen taunt pool - real definition lives in BoardGameScreen.cpp (shared
// with BoardGameScreenDuelAndKeys.cpp's ResolveFateCard GateOpen variant),
// given real (external) linkage there so both translation units can use it
// without duplicating the table.
extern const char* kGateOpenWav[7];

void BoardGameScreen::TriggerEncounter() {
    // Encounter trigger — dispatches based on MapFile stone type.
    GameState* gs = GameStateManager::Instance().Get();
    if (!gs || !gs->IsValid()) {
        return;
    }

    // Khufu curse blindness was only needed for the Moving/Encounter-roll render - freed here,
    // before whatever this dispatches into (a minigame needs the VRAM/heap room back, not
    // after it already tried to load its own assets).
    ReleaseTexture(portrait_.curse_indicator_tex);
    ReleaseAudio(portrait_.curse_blindness_wav);

    const int p = current_player_;

    // Guaranteed dice-restriction expiry: once dice_restriction_turns (arrivals since
    // DispatchOneDieRestriction restricted the player, incremented in BeginStepArrival())
    // exceeds 6 and no black hole retry is pending, resets dice_restricted unconditionally —
    // distinct from the probabilistic early-clear in RollExtraTurnOrDiceEncounter(). Like
    // CureCurse below, replaces the enc type roll entirely for this turn.
    if (force_encounter_type_ == 0 && render_state_[p].black_hole_retry_counter == 0 &&
        render_state_[p].dice_restriction_turns > 6) {
        render_state_[p].dice_restriction_turns = 0;
        render_state_[p].dice_restricted = 2;
        QueueGkLoopTaunt(playWav("W5\\AT_0551.WAV", AudioFlags::Async));
        return;
    }

    // CureCurse gate: offers a curse cure when the player is cursed (curse_type!=0) and
    // curse_step_counter is due. Reached only when black_hole_retry_counter==0 (a black hole
    // retry is resolved separately, earlier, in ShowingPortrait) and the dice-restriction-
    // expiry branch above did not fire. REPLACES the enc type roll below entirely for this
    // turn — no encounter dispatch happens.
    if (force_encounter_type_ == 0 && render_state_[p].black_hole_retry_counter == 0 &&
        render_state_[p].dice_restriction_turns <= 6 && render_state_[p].curse_type != 0) {
        // Due at cure_threshold, then every 2 turns after (curse_step_counter only ever
        // increments by 1 or resets to 0 on cure). Bathory (curse_type 6): threshold drops to
        // 0 once GainKey has already redirected a key away from this player
        // (lost_key_to_vampire), reoffering far more aggressively than the normal 10.
        const uint16_t curse_type = render_state_[p].curse_type;
        const int cure_threshold =
            (curse_type == 1)
                ? 7
                : (curse_type == 6 ? (render_state_[p].lost_key_to_vampire ? 0 : 10) : 6);
        const int step_count = render_state_[p].curse_step_counter;
        const bool due = (step_count == cure_threshold) ||
                         (step_count > cure_threshold && (step_count % 2) == 0);
        if (due) {
            curecurse_screen_.Enter(p, curse_type);
            return;
        }
    }

    // AI turns never reach the real 1-16 encounter roll at all: the real dispatcher only
    // proceeds to the encounter roll when the turn-state field is EXACTLY 8 — the AI-tag stub
    // sets that field to 9, never 8 — so an AI turn always takes the generic-message/no-roll
    // branch here, every time (in the common case where neither of the two blocks above
    // fired).
    if (render_state_[p].is_ai && force_encounter_type_ == 0) {
        current_state_ = BoardGameState::TurnEnd;
        state_start_time_ = std::chrono::steady_clock::now();
        state_action_executed_ = false;
        return;
    }

    const uint16_t province = render_state_[p].province;

    // The real encounter dispatcher has ZERO stone-type awareness — its only gate is
    // `player->current_province != 6 && != 7` (immediate no-roll return otherwise), regardless
    // of what's on the stone. Test hook: force_encounter_type_ != 0 skips this gate (fires on
    // any stone/province).
    if (force_encounter_type_ == 0 && province >= 6) {
        current_state_ = BoardGameState::TurnEnd;
        state_start_time_ = std::chrono::steady_clock::now();
        state_action_executed_ = false;
        return;
    }

    // Regular province encounter — provinces 0-5 only.
    DispatchEncounter(p);
}

float BoardGameScreen::ComputeKeyFactor(int player, long long remaining_seconds,
                                        long long total_seconds) const {
    const int held_keys = CountHeldKeys(player);
    const long long time_slice =
        (remaining_seconds * 6) / (total_seconds != 0 ? total_seconds : 1) + 1;
    const float time_elapsed_frac = 1.0f - (static_cast<float>(time_slice) / 6.0f);
    const float keys_held_frac = static_cast<float>(held_keys) / 6.0f;
    return keys_held_frac - time_elapsed_frac;
}

float BoardGameScreen::ComputePowerFactor(int player) const {
    int sum_power = 0;
    for (int i = 0; i < total_players_; ++i) sum_power += render_state_[i].power;
    if (sum_power == 0) return 0.0f;
    const float avg_power =
        total_players_ > 0 ? static_cast<float>(sum_power) / total_players_ : 0.0f;
    return ((static_cast<float>(render_state_[player].power) - avg_power) * total_players_) /
           static_cast<float>(sum_power);
}

void BoardGameScreen::AppendWeightedCategory(const int ids[], int count, float rate, int capacity,
                                             WeightedPickEntry* out, int& out_count) {
    auto mult_at = [&](int i) {
        return (rate >= 0.0f) ? (static_cast<float>(i) * rate + 1.0f)
                              : (1.0f - static_cast<float>(count - i - 1) * rate);
    };
    float sum_mult = 0.0f;
    for (int i = 0; i < count; ++i) sum_mult += mult_at(i);
    if (sum_mult == 0.0f) sum_mult = 1.0f;
    for (int i = 0; i < count; ++i) {
        out[out_count++] = {ids[i], mult_at(i) * 100.0f * static_cast<float>(count) /
                                        (static_cast<float>(capacity) * sum_mult)};
    }
}

void BoardGameScreen::AppendLiteralWeightCategory(const int ids[], const float weights[], int count,
                                                  int capacity, WeightedPickEntry* out,
                                                  int& out_count) {
    float sum_weights = 0.0f;
    for (int i = 0; i < count; ++i) sum_weights += weights[i];
    if (sum_weights == 0.0f) sum_weights = 1.0f;
    for (int i = 0; i < count; ++i) {
        out[out_count++] = {ids[i], weights[i] * 100.0f * static_cast<float>(count) /
                                        (static_cast<float>(capacity) * sum_weights)};
    }
}

int BoardGameScreen::PickWeightedEntry(const WeightedPickEntry entries[], int count) {
    const float roll = (static_cast<float>(RngNext()) / 32768.0f) * 100.0f;
    float cum = 0.0f;
    int picked = entries[count - 1].id;
    for (int i = 0; i < count; ++i) {
        cum += entries[i].weight;
        if (roll < cum) {
            picked = entries[i].id;
            break;
        }
    }
    return picked;
}

long long BoardGameScreen::RemainingGameSeconds() const {
    // game_duration_s_ (total game duration) is NOT decremented per turn — remaining is
    // always computed fresh here as (game_duration_s_ - elapsed), for display and every
    // time-gated check alike.
    GameState* gs = GameStateManager::Instance().Get();
    const long long total = gs ? static_cast<long long>(gs->game_duration_s_) : 0;
    const long long elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::steady_clock::now() - game_start_time_)
                                  .count();
    return total - elapsed;
}

void BoardGameScreen::ComputeEncounterSlotWeights(int p, float w[5]) const {
    GameState* gs_w = GameStateManager::Instance().Get();
    const long long total_w = gs_w ? static_cast<long long>(gs_w->game_duration_s_) : 0;
    const long long remaining_w = RemainingGameSeconds();

    const int held_keys_p = CountHeldKeys(p);
    const bool restricted = (remaining_w < 600 && held_keys_p < 3) || render_state_[p].power < 10;

    if (restricted) {
        w[0] = 0.0f;
        w[1] = 0.0f;
        w[2] = 100.0f;
        w[3] = 0.0f;
        w[4] = 0.0f;
        return;
    }

    const long long remaining_min = remaining_w / 60;
    const float time_bonus = (remaining_min >= 5) ? 10.0f : 0.0f;
    if (render_state_[p].chosen_one_flag != 0) {
        w[1] = 25.0f;
        w[3] = 20.0f;
    } else {
        w[1] = 5.0f;
        w[3] = 10.0f;
    }
    w[4] = w[3];

    const float keyFactor = ComputeKeyFactor(p, remaining_w, total_w);
    const float powerFactor = ComputePowerFactor(p);

    w[2] = keyFactor * 30.0f + 12.0f + powerFactor * 20.0f;
    w[0] = 100.0f - w[1] - time_bonus - w[2] - w[3];
}

int BoardGameScreen::RollEncounterSlot(const float w[5]) {
    // The real weighted-table roll reseeds its LCG from the system clock right before
    // this roll — deliberately NOT replicated: no port feature depends on matching the
    // original's exact roll sequence, and clock-reseeding risks identical back-to-back
    // rolls within the same second, a quirk of the original's 1-second clock resolution,
    // not worth reproducing. Continuous LCG used instead.
    const float target = RngNext() * 100.0f / 32768.0f;
    float cum = 0.0f;
    int slot = 4;
    for (int i = 0; i < 5; ++i) {
        cum += w[i];
        if (cum > target) {
            slot = i;
            break;
        }
    }
    return slot;
}

uint16_t BoardGameScreen::RollDiceEffectEncounter(int p) {
    // ~5% slot: 3-way sub-roll. case0=type2(OneDie), case1=type4(1-die
    // restriction), or type 5 (Roll a Six) — each gated independently.
    auto case0_one_die = [&]() -> uint16_t {
        const int active = CountActivePlayers();
        if (render_state_[p].skip_turn_counter == 0 && active > 1) {
            return 2;  // OneDie
        }
        return 0;
    };
    if (render_state_[p].curse_type == 2) {
        // Anne/frog curse: skips the 3-way roll entirely (consumes no RNG) and forces
        // sub-case 0 (OneDie check) directly.
        return case0_one_die();
    }
    const uint16_t roll = static_cast<uint16_t>(RollIndex(RngNext(), 3));
    if (roll == 0) {
        return case0_one_die();
    } else if (roll == 1) {
        // Blocked ONLY when forced to 1-die (==1), not on the normal value (==2).
        if (render_state_[p].dice_restricted != 1) {
            return 4;  // 1-die restriction
        }
    } else {
        if (!render_state_[p].even_dice) {
            return 5;  // Roll a Six
        }
    }
    return 0;
}

uint16_t BoardGameScreen::RollMinigameEncounter(int p) {
    // Always rolls first (9-way for Bathory's curse, else 10-way; same switch table — the
    // 9-way variant just can't land on index 9/enc type 15 this way), then unconditionally
    // checks the last-minute key limit. If met, a second roll OVERWRITES the result
    // regardless, consuming 2 RNG calls total instead of 1.
    static constexpr uint16_t kMinigameTypes[10] = {7, 8, 9, 8, 10, 11, 12, 13, 14, 15};
    const bool bathory = render_state_[p].curse_type == 6;
    const uint16_t denom = bathory ? 9u : 10u;
    const uint16_t idx = static_cast<uint16_t>(RollIndex(RngNext(), denom));
    uint16_t enc_type = kMinigameTypes[idx < 10 ? idx : 9];

    const long long remaining = RemainingGameSeconds();
    const int key_count = CountHeldKeys(p);

    if (remaining < 600 && key_count < 3) {
        static constexpr uint16_t kLastMinuteOverrideTypes[3] = {15, 11, 13};
        const uint16_t ov_roll = static_cast<uint16_t>(RollIndex(RngNext(), 3));
        enc_type = kLastMinuteOverrideTypes[ov_roll < 3 ? ov_roll : 2];
    }
    return enc_type;
}

uint16_t BoardGameScreen::RollExtraTurnOrDiceEncounter(int p) {
    // Never produces type 4/5. roll==0 -> type 6 (if active>1), roll!=0 -> type 2 (OneDie,
    // ONLY if the secondary dice-restriction flag is active — consumes it + resets
    // dice_restricted=2).
    const uint16_t roll = static_cast<uint16_t>(RollIndex(RngNext(), 4));
    if (roll == 0) {
        if (CountActivePlayers() > 1) {
            return 6;
        }
    } else if (render_state_[p].dice_restriction_turns) {
        render_state_[p].dice_restriction_turns = 0;
        render_state_[p].dice_restricted = 2;
        return 2;  // OneDie
    }
    return 0;
}

uint16_t BoardGameScreen::DetermineEncounterType(int p) {
    // Type selection: always rolls a 5-slot weighted table first, then a sub-roll within the
    // picked slot resolves the final 1-16 type — regardless of power. power < 10 then
    // unconditionally overrides the result to type 11 (fear-question penalty).
    uint16_t enc_type = 0;
    if (force_encounter_type_ != 0) {
        enc_type = static_cast<uint16_t>(force_encounter_type_);
        if (force_encounter_type_once_) {
            force_encounter_type_ = 0;
        }
    } else {
        float w[5];  // slot weights: 0,1,2,3,4 (3 and 4 share the same value)
        ComputeEncounterSlotWeights(p, w);
        const int slot = RollEncounterSlot(w);

        if (slot == 0) {
            // Residual weight: no encounter
        } else if (slot == 1) {
            enc_type = RollDiceEffectEncounter(p);
        } else if (slot == 2) {
            enc_type = RollMinigameEncounter(p);
        } else if (slot == 3) {
            enc_type = RollExtraTurnOrDiceEncounter(p);
        } else {
            // Slot 4 (shares weight w[3]): fixed enc type 1, DispatchTaunt below.
            enc_type = 1;
        }
    }

    // Power override (after the roll, before the ambient fallback) unconditionally
    // overwrites any result, even "no encounter." Ambient fallback only fires if power
    // override didn't already claim it. force_encounter_type_ (test hook) is exempt from both.
    if (force_encounter_type_ == 0 && render_state_[p].power < 10) {
        enc_type = 11;
    }

    // Fallback ambient trigger (type 16), only when nothing else fired above. Last 5 minutes,
    // one-shot per minute, via a GLOBAL flag table (GameState::ambient_trigger_armed, not
    // per-player) — whichever player checks first in a given minute consumes it for everyone.
    if (force_encounter_type_ == 0 && enc_type == 0) {
        GameState* gs_amb = GameStateManager::Instance().Get();
        const long long minutes_remaining = RemainingGameSeconds() / 60;
        if (gs_amb && minutes_remaining >= 0 && minutes_remaining < 5 &&
            gs_amb->ambient_trigger_armed[minutes_remaining]) {
            gs_amb->ambient_trigger_armed[minutes_remaining] = 0;
            enc_type = 16;
        }
    }

    return enc_type;
}

void BoardGameScreen::DispatchEncounter(int p) {
    const uint16_t enc_type = DetermineEncounterType(p);
    if (enc_type == 0) {
        return;
    }

    switch (enc_type) {
        case 1:
            DispatchTaunt();
            return;

        case 2:
            DispatchSkipNextTurn(p);
            return;

        case 3:
            DispatchBlackHoleTeleport(p);
            return;

        case 4:
            DispatchOneDieRestriction(p);
            return;

        case 5:
            DispatchEvenDiceOnly(p);
            return;

        case 6:
            DispatchExtraTurn(p);
            return;

        case 7:
            doors_screen_.Enter(p);
            return;

        case 8:
            dicekeys_screen_.Enter(p);
            return;

        case 9:
            onedie_screen_.Enter(p);
            return;

        case 10:
            fortune_door_screen_.Enter(p);
            return;

        case 11:
            coffin_screen_.Enter(p);
            return;

        case 12:
            bribe_screen_.Enter(p);
            return;

        case 13:
            blackjack_screen_.Enter(p);
            return;

        case 14:
            slot_screen_.Enter(p);
            return;

        case 15:
            keyaward_screen_.Enter(p);
            return;

        case 16:
            DispatchEyeloopTaunt();
            return;

        default:
            break;
    }
}

void BoardGameScreen::QueueGkLoopTaunt(SoundHandle audio) {
    const int gk = RollIndex(RngNext(), 3) + 1;
    gkloop_avi_path_ = AssetPaths::avi("AVI", std::string("GKLOOP") + std::to_string(gk)).string();
    gkloop_post_state_ = BoardGameState::TurnEnd;
    gkloop_enc_audio_ = audio;
    video_.close();
    video_loaded_ = false;
    video_.setRenderRect(kBoardAreaX, kBoardAreaY, kBoardAreaW, kBoardAreaH);
    if (video_.open(gkloop_avi_path_)) {
        video_.play(true);
    }
    black_flash_next_state_ = BoardGameState::GkLoopAVI;
    current_state_ = BoardGameState::BlackFlash;
    state_start_time_ = std::chrono::steady_clock::now();
    state_action_executed_ = false;
}

SoundHandle BoardGameScreen::RollAudio(const char* const* table, int count) {
    const int idx = static_cast<int>(RngNext() * static_cast<unsigned>(count) / 0x8000u);
    return playWav(table[idx], AudioFlags::Async);
}

SoundHandle BoardGameScreen::PlayEncounterAudio(int player, const char* fixed_wav,
                                                const char* const* table, int count) {
    if (render_state_[player].chosen_one_flag != 0) {
        return playWav(fixed_wav, AudioFlags::Async);
    }
    return RollAudio(table, count);
}

void BoardGameScreen::DispatchTaunt() {
    // Audio-only taunt, ZERO side effect. No render_state/GameState write anywhere
    // in this case — the key-steal mechanic sometimes assumed here belongs to the
    // portrait-corner DUEL system, entirely unrelated to this dispatcher.
    static const char* kTauntWav[13] = {"W3\\AT_0422.WAV",  "W3\\AT_0428.WAV",  "W3\\AT_0429.WAV",
                                        "W3\\AT_0473A.WAV", "W3\\AT_0473B.WAV", "W4\\AT_0477.WAV",
                                        "W4\\AT_0502.WAV",  "W4\\AT_0503.WAV",  "W4\\AT_0506.WAV",
                                        "W4\\AT_0507.WAV",  "W4\\AT_0509.WAV",  "W4\\AT_0510.WAV",
                                        "W4\\AT_0512.WAV"};
    QueueGkLoopTaunt(RollAudio(kTauntWav, 13));
}

void BoardGameScreen::DispatchSkipNextTurn(int p) {
    // OneDie — skip-turn counter, NOT an extra movement step. Player skips their
    // NEXT turn; nothing happens to their position now. If chosen_one_flag ("The Chosen One") is
    // set, uses a fixed audio clip with no RNG consumed for audio selection.
    static const char* kOneDieWav[6] = {"W2\\AT_0271.WAV", "W5\\AT_0570.WAV", "W5\\AT_0572.WAV",
                                        "W5\\AT_0573.WAV", "W5\\AT_0574.WAV", "W5\\AT_0578.WAV"};
    SoundHandle skip_turn_audio = PlayEncounterAudio(p, "W2\\AT_0106.WAV", kOneDieWav, 6);
    render_state_[p].skip_turn_counter = 1;
    QueueGkLoopTaunt(skip_turn_audio);
}

void BoardGameScreen::TeleportToNearestBlackHole(int p) {
    const uint16_t prov = render_state_[p].province;
    const MapStone* cur_stone = mapfile_.GetStone(prov, render_state_[p].stone_id);
    if (cur_stone && cur_stone->nearest_bh >= 0) {
        const uint16_t bh = static_cast<uint16_t>(cur_stone->nearest_bh);
        render_state_[p].stone_id = bh;
        if (p == current_player_) {
            GameStateManager::Instance().Get()->sub_idx = bh;
        }
        const MapStone* bh_stone = mapfile_.GetStone(prov, bh);
        if (bh_stone) {
            for (int dir = 0; dir < 4; ++dir) {
                if (bh_stone->dest[dir] >= 0) {
                    render_state_[p].direction = static_cast<uint16_t>(dir);
                    if (p == current_player_) mapfile_direction_ = dir;
                    break;
                }
            }
        }
    }
    render_state_[p].black_hole_retry_counter = 1;
}

void BoardGameScreen::DispatchBlackHoleTeleport(int p) {
    TeleportToNearestBlackHole(p);
    QueueGkLoopTaunt(RollAudio(kGateOpenWav, 7));
}

void BoardGameScreen::DispatchOneDieRestriction(int p) {
    // 1-die restriction. Player forced to roll 1 die for roughly the next 7 turns;
    // dice_restriction_turns counts completed turns toward that expiry.
    static const char* kDiceRestrictionWav[4] = {"W2\\AT_0308.WAV", "W2\\AT_0309.WAV",
                                                 "W2\\AT_0310.WAV", "W2\\AT_0311.WAV"};
    render_state_[p].dice_restricted = 1;
    render_state_[p].dice_restriction_turns = 1;
    QueueGkLoopTaunt(PlayEncounterAudio(p, "W6\\AT_0604.WAV", kDiceRestrictionWav, 4));
}

void BoardGameScreen::DispatchEvenDiceOnly(int p) {
    // Roll a Six (enc type 5) restricts single-die rolls to {2, 4, 6} until a 6 is rolled.
    static const char* kRollASixWavs[4] = {"W5\\AT_0546.WAV", "W5\\AT_0547.WAV", "W6\\AT_0606.WAV",
                                           "W6\\AT_0607.WAV"};
    render_state_[p].even_dice = 1;
    QueueGkLoopTaunt(RollAudio(kRollASixWavs, 4));
}

void BoardGameScreen::DispatchExtraTurn(int p) {
    // "Extra turn": DecrementCurrentPlayerCyclic() rotates current_player_idx backward by
    // one. GKLOOP audio-only, no minigame.
    DecrementCurrentPlayerCyclic();
    static const char* kExtraTurnWav[3] = {"W2\\AT_0189.WAV", "W5\\AT_0575.WAV", "W5\\AT_0576.WAV"};
    QueueGkLoopTaunt(PlayEncounterAudio(p, "W6\\AT_0605.WAV", kExtraTurnWav, 3));
}

void BoardGameScreen::DispatchEyeloopTaunt() {
    // Audio-only EYELOOP. GkLoopAVI loops GKLOOP{n}.AVI while audio plays -> TurnEnd.
    QueueGkLoopTaunt(playWav("W3\\AT_0454.WAV", AudioFlags::Async));
}

void BoardGameScreen::FreeLazyMinigameAssets() {
    platform_log_memory_stats("turn-peak", renderer_);
    // Existing subscreens' lazy-loaded assets, freed uniformly via Subscreen::FreeAssets() —
    // one array instead of scattered calls (still manual: a new subscreen must be added
    // here too).
    Subscreen* lazy_subscreens[] = {
        &bribe_screen_,  &fortune_door_screen_, &slot_screen_,      &blackjack_screen_,
        &onedie_screen_, &dicekeys_screen_,     &curecurse_screen_, &doors_screen_,
        &coffin_screen_, &flight_map_screen_,   &minimap_screen_,
    };
    for (Subscreen* s : lazy_subscreens) s->FreeAssets();

    // ChooseOpponent duel target-picker (province-6-independent Duel)
    if (duel_.choose_icons_tex != 0) {
        renderer_.destroyTexture(duel_.choose_icons_tex);
        duel_.choose_icons_tex = 0;
        renderer_.destroyTexture(duel_.choose_broken_tex);
        duel_.choose_broken_tex = 0;
    }
    duel_choose_screen_.FreeProtectionCardTexture();
    // Khufu curse blindness (needed through Moving/Encounter, released once the whole turn
    // — including any minigame it dispatched into — is actually done).
    ReleaseTexture(portrait_.curse_indicator_tex);
    ReleaseAudio(portrait_.curse_blindness_wav);
    // Duel gauge battle + KeyTransfer
    if (duel_.bg_tex != 0) {
        renderer_.destroyTexture(duel_.bg_tex);
        duel_.bg_tex = 0;
        renderer_.destroyTexture(duel_.getkeys_tex);
        duel_.getkeys_tex = 0;
        renderer_.destroyTexture(duel_.key_highlight_tex);
        duel_.key_highlight_tex = 0;
        renderer_.destroyTexture(duel_.sigil_tex);
        duel_.sigil_tex = 0;
        renderer_.destroyTexture(duel_.gauge_atk_tex);
        duel_.gauge_atk_tex = 0;
        renderer_.destroyTexture(duel_.gauge_def_tex);
        duel_.gauge_def_tex = 0;
    }
    platform_log_memory_stats("turn-cleaned", renderer_);
}

bool BoardGameScreen::CheckGameTimerExpiry() {
    // The real end-of-game check only runs at discrete turn-advance points, never
    // continuously — it can never interrupt an active screen. near_end_flag_ (set
    // elsewhere, every frame, the instant remaining<15s) can sit unconsumed for a while: the
    // actual game-over action only fires at the NEXT turn boundary, so the real game can end
    // anywhere from ~14s down to negative remaining, never exactly at 0.
    if (!(game_timer_started_ && !timer_expired_ && near_end_flag_)) {
        return false;
    }
    timer_expired_ = true;

    // The original's audio is a single/limited-stream device: starting new playback
    // implicitly cuts whatever was using that stream, no separate stop call needed. This
    // port's multi-channel audio model lets sounds overlap in ways the original never
    // could — so this needs an EXPLICIT stop to match original behavior.
    audio_.stop();

    GameState* gs = GameStateManager::Instance().Get();
    gs->sub_idx = 1;

    // EYELOOP animation: GKLOOP{n} loops while a random insult plays.
    static const char* kTimerWavs[6] = {
        "W1\\AT_0062.WAV",  "W3\\AT_0349A.WAV", "W3\\AT_0350A.WAV",
        "W3\\AT_0351A.WAV", "W3\\AT_0352A.WAV", "W3\\AT_0353A.WAV",
    };
    const int wav_idx = RollIndex(RngNext(), 6);
    const int gk_fq = RollIndex(RngNext(), 3) + 1;
    gkloop_avi_path_ =
        AssetPaths::avi("AVI", std::string("GKLOOP") + std::to_string(gk_fq)).string();
    gkloop_enc_audio_ = playWav(kTimerWavs[wav_idx], AudioFlags::Async);
    gkloop_post_state_ = BoardGameState::GameOver;
    video_.close();
    video_loaded_ = false;
    video_.setRenderRect(kBoardAreaX, kBoardAreaY, kBoardAreaW, kBoardAreaH);
    if (video_.open(gkloop_avi_path_)) {
        video_.play(true);
    }
    black_flash_next_state_ = BoardGameState::GkLoopAVI;
    current_state_ = BoardGameState::BlackFlash;
    state_start_time_ = std::chrono::steady_clock::now();
    state_action_executed_ = false;
    return true;
}

bool BoardGameScreen::ResolveWinFlagGatekeeperRoll() {
    // Any player with all 6 keys crosses the portal and faces the Gatekeeper roll (win_flag
    // was set on crossing; this roll only decides what happens after). Survive ends the
    // whole match. Lose ends it too, but only in 1P+AI mode (HasAiPlayer()) — in pure
    // multiplayer it just eliminates this player, others keep playing. Elimination side
    // effects apply either way.
    if (!render_state_[current_player_].win_flag) {
        return false;
    }
    render_state_[current_player_].win_flag = 0;  // consumed by this roll, don't re-trigger
    const uint16_t fear_index = render_state_[current_player_].fear_index;
    uint16_t fear_roll = static_cast<uint16_t>(RollIndex(RngNext(), 6));
    if (force_gatekeeper_lose_ && fear_index > 0) {
        fear_roll = static_cast<uint16_t>(fear_index - 1);  // manual-QA determinism only
    }
    const bool survived = (fear_index == 0) || (static_cast<uint16_t>(fear_index - 1) != fear_roll);
    const bool ends_match = survived || HasAiPlayer();
    if (ends_match) {
        winner_char_id_ = render_state_[current_player_].char_id;
        winner_gatekeeper_survived_ = survived;
    } else {
        render_state_[current_player_].eliminated = 1;
    }
    // Play AV2/FEAR{roll}.AVI then the survive/eliminate result taunt (GKLOOP1 + insult
    // pool) via StartGatekeeperResultTaunt(). No teleport: dest stone = current stone.
    teleport_.path =
        AssetPaths::avi("AV2", std::string("FEAR") + std::to_string(fear_roll)).string();
    teleport_.dest_stone = render_state_[current_player_].stone_id;
    teleport_.dest_prov = -1;
    teleport_.kind = TeleportAviState::Kind::GatekeeperChallenge;
    gatekeeper_taunt_survived_ = survived;
    // Set teleport_.post_state here, not gkloop_post_state_ — when the FEAR AVI finishes,
    // StartGatekeeperResultTaunt() copies teleport_.post_state into gkloop_post_state_
    // (then resets teleport_.post_state to Moving). Writing gkloop_post_state_ directly
    // here would just get overwritten by that copy.
    teleport_.post_state = ends_match ? BoardGameState::GameOver : BoardGameState::TurnEnd;
    // TurnEnd's handler unconditionally stomps state_action_executed_=true right after
    // EndTurn() returns — if current_state_ were set to TeleportAVI directly here, that
    // stomp would still happen, making TeleportAVI skip its own one-time entry setup.
    // BlackFlash's own handler is unconditional (immune to the stomp) and re-arms
    // state_action_executed_=false itself — same indirection as the game-timer-expiry path
    // above.
    black_flash_next_state_ = BoardGameState::TeleportAVI;
    current_state_ = BoardGameState::BlackFlash;
    state_start_time_ = std::chrono::steady_clock::now();
    state_action_executed_ = false;
    return true;
}

void BoardGameScreen::SaveCurrentPlayerPositionFromGameState(const GameState* gs) {
    render_state_[current_player_].province = gs->player_idx;
    render_state_[current_player_].stone_id = gs->sub_idx;
}

void BoardGameScreen::AdvanceToNextActivePlayer(GameState* gs) {
    // Save current player's board position — skipped when DecrementCurrentPlayerCyclic() has
    // already decremented current_player_ as part of its own "repeat the same player's turn"
    // mechanism: it does this exact same save itself, at the point where current_player_
    // still correctly identifies the real mover (before its own decrement) —
    // skip_position_save_ tells this block not to redo it against the now-wrong index.
    if (!skip_position_save_) {
        SaveCurrentPlayerPositionFromGameState(gs);
        render_state_[current_player_].direction = mapfile_direction_;
    }
    skip_position_save_ = false;

    // Advance to next player, skipping eliminated players AND consuming pending skip-turns.
    // After landing on the next active player, checks skip_turn_counter; while nonzero,
    // decrements it and loops to the next player again, never running turn setup for the
    // skipped player at all.
    int next_player = current_player_;
    int guard = 0;
    const int guard_max = total_players_ * 4 + 4;  // generous cap against pathological loops
    do {
        next_player = (next_player + 1) % total_players_;
        ++guard;
    } while (render_state_[next_player].eliminated && guard < guard_max);

    while (render_state_[next_player].skip_turn_counter > 0 && guard < guard_max) {
        --render_state_[next_player].skip_turn_counter;
        do {
            next_player = (next_player + 1) % total_players_;
            ++guard;
        } while (render_state_[next_player].eliminated && guard < guard_max);
    }

    current_player_ = next_player;

    // Load next player's board position into GameState
    gs->player_idx = render_state_[current_player_].province;
    gs->sub_idx = render_state_[current_player_].stone_id;
    mapfile_direction_ = static_cast<uint8_t>(render_state_[current_player_].direction);
    // Port-only safety net: a player's stored .direction can lack a frame CONTINUING that
    // way (only guaranteed for the direction they arrived FROM). Harmless in Moving, but
    // ShowingPortrait's entry always calls ReopenFullmapAvi() (resets last_video_frame_=-1)
    // — with no valid frame, the seek block never fires, leaving the video undecoded: a
    // black screen, no teleport/gate needed to trigger it.
    AutoOrientDirection(gs->player_idx, gs->sub_idx);

    // Only 2 paths exist here — time-exhaustion end-of-game, or continue normal rotation —
    // never a check on how many players are still active. A sole survivor keeps playing
    // alone until they win (6 keys) or the timer runs out.
}

void BoardGameScreen::EndTurn() {
    // Advances to the next active player, checking the game timer and win-flag gatekeeper
    // roll first.
    GameState* gs = GameStateManager::Instance().Get();
    if (!gs || !gs->IsValid()) {
        return;
    }

    if (CheckGameTimerExpiry()) return;

    // The ambient-nag channel has no literal per-turn object lifecycle to hook a destructor
    // into, so stop it explicitly here at the equivalent teardown point. This is as far as
    // EndTurn's own audio handling goes — it only stops, never plays a new sound.
    // WAV\TURN{n}.WAV plays at the START of the next player's turn instead, via
    // ShowingPortrait.
    if (ambient_nag_wav_ != 0) {
        audio_.stop(ambient_nag_wav_);
        ambient_nag_wav_ = 0;
    }

    gs->ResetPerTurnFields();

    if (ResolveWinFlagGatekeeperRoll()) return;

    // ALL PLAYERS ELIMINATED — port-only safety net, no real equivalent. The real
    // turn-rotation function is unconditional self-recursion with no base case for this,
    // genuinely hanging the 1995 game. Without this check, the rotation loop below would
    // exhaust guard_max and land on an eliminated player anyway, letting them keep taking
    // turns. winner_char_id_/timer_expired_ stay untouched so GatekeeperMiss() returns
    // false and RunPostGameSequence falls into its own graceful loss branch.
    if (CountActivePlayers() == 0) {
        current_state_ = BoardGameState::GameOver;
        return;
    }

    AdvanceToNextActivePlayer(gs);
}

}  // namespace openfear::game
