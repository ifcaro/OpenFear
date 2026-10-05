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
// with BoardGameScreenEncounters.cpp's DispatchBlackHoleTeleport), given
// real (external) linkage there so both translation units can use it
// without duplicating the table.
extern const char* kGateOpenWav[7];

// Fortune-card plaque table - real definition lives in BoardGameScreen.cpp
// (shared with subscreens/DuelChooseOpponent.cpp's Protection overlay).
extern const char* kFortuneCard[6];

// Key-rotation table (province offsets by char_id) - real definition lives in
// BoardGameScreen.cpp (shared with BoardGameScreenUpdate.cpp's KeyAward target roll).
extern const int kKeyTable[6];

// ---------------------------------------------------------------------------
// Pure formulas, extracted for unit testing (no BoardGameScreen instance needed). Behavior
// must stay byte-identical to the inline logic at their call sites.
// ---------------------------------------------------------------------------

bool BoardGameScreen::ComputeKeyAwardRestricted(long long remaining_seconds, int held) {
    return remaining_seconds < 600 && held < 3;
}

int BoardGameScreen::ComputeKeyAwardTarget(bool restricted, int held, uint32_t rng_raw) {
    return restricted ? static_cast<int>(rng_raw * static_cast<uint32_t>(6 - held) / 0x8000u) + 1
                      : 1;
}

int BoardGameScreen::ComputeBlackHoleRoll(uint32_t rng_raw) {
    return static_cast<int>(rng_raw * 6u / 0x8000u) + 1;
}

bool BoardGameScreen::ComputeBlackHoleResolves(uint16_t retry_flag, int roll) {
    return retry_flag == 4 || retry_flag > static_cast<uint16_t>(roll);
}

int BoardGameScreen::ComputeFirstUnheldInRotation(int char_id, const uint16_t keys[6]) {
    for (int i = 0; i < 6; ++i) {
        const int prov = (kKeyTable[i] + char_id) % 6;
        if (!keys[prov]) return prov;
    }
    return -1;
}

int BoardGameScreen::ComputeLastHeldInRotation(int char_id, const uint16_t keys[6]) {
    int result = -1;
    for (int i = 0; i < 6; ++i) {
        const int prov = (kKeyTable[i] + char_id) % 6;
        if (keys[prov]) result = prov;
    }
    return result;
}

int BoardGameScreen::ComputeKeyPoolTokenCount(int total_players) {
    static constexpr int kTokensByPlayerCount[6] = {6, 8, 12, 14, 20, 24};
    const int players = std::clamp(total_players, 1, 6);
    return kTokensByPlayerCount[players - 1];
}

void BoardGameScreen::SeedKeyPool(uint16_t pool[6], int total_players, int start_province) {
    std::fill(pool, pool + 6, 0);
    int province = ((start_province % 6) + 6) % 6;
    const int tokens = ComputeKeyPoolTokenCount(total_players);
    for (int i = 0; i < tokens; ++i) {
        ++pool[province];
        province = (province + 1) % 6;
    }
}

void BoardGameScreen::Configure(int initial_keys, uint16_t ai_power, const int* player_fears,
                                int num_fears) {
    initial_keys_count_ = std::clamp(initial_keys, 0, 6);
    ai_initial_power_ = ai_power;

    // Seeds the shared key pool from a random starting province, scaled by player count
    // (num_fears is total_players — GameFlow.cpp passes total_players here).
    const int start_province = static_cast<int>(RngNext() * 6u / 0x8000u);
    SeedKeyPool(province_keys_remaining_, num_fears, start_province);

    // Store per-player fear indices (1-6, 1-indexed; 0 = unset/immune), used by gatekeeper
    // elimination. FearSelectScreen returns 0-5 (0-indexed), stored here as 1-indexed (fear+1).
    if (player_fears && num_fears > 0) {
        for (int i = 0; i < std::min(num_fears, 6); ++i) {
            render_state_[i].fear_index =
                (player_fears[i] >= 0)
                    ? static_cast<uint16_t>(player_fears[i] + 1)  // 0-indexed -> 1-indexed
                    : 0u;                                         // -1 = unset
        }
    }
}

void BoardGameScreen::AddPower(int player, uint16_t amount) {
    if (player < 0 || player >= 6) return;
    // No cap — wraps at 0xFFFF.
    render_state_[player].power += amount;
}

void BoardGameScreen::SubtractPower(int player, uint16_t amount) {
    if (player < 0 || player >= 6) return;
    if (render_state_[player].power < amount) {
        render_state_[player].power = 0;
    } else {
        render_state_[player].power -= amount;
    }
}

// Key-power permutation table, extracted directly from the original's data segment — a
// fixed 36-byte table, one bit per (char_id, province) pair (PlayerRenderState.h's
// key_power_flags). Real (external) linkage, not an anonymous namespace —
// BoardGameScreenDebug.cpp's DebugSetPlayerKeys() needs it too, with its own matching
// `extern` declaration (kept an internal implementation detail, not exposed via
// BoardGameScreen.h).
extern const uint8_t kKeyPowerBitmask[6][6] = {
    {0x01, 0x20, 0x08, 0x10, 0x02, 0x04}, {0x08, 0x01, 0x20, 0x02, 0x04, 0x10},
    {0x20, 0x08, 0x01, 0x04, 0x10, 0x02}, {0x04, 0x02, 0x10, 0x01, 0x08, 0x20},
    {0x02, 0x10, 0x04, 0x20, 0x01, 0x08}, {0x10, 0x04, 0x02, 0x08, 0x20, 0x01},
};

void BoardGameScreen::TriggerCurseIndicatorIfKhufu() {
    // Plays W6/AT_0597.WAV, gated internally on curse_step_counter == 0 so the line only
    // plays the very first time this player is cursed, not on every subsequent roll. Fires
    // once per roll, matching the dice-sprite->counter swap (same kDiceGraceMs threshold as
    // render()'s Layer 1 bitmap substitution), NOT at arrival — this function only handles
    // the one-shot audio line, not the K_CURSE.BMP bitmap itself (shown separately).
    if (render_state_[current_player_].curse_type != 3) return;
    if (render_state_[current_player_].curse_step_counter != 0) return;
    if (portrait_.curse_blindness_wav) {
        // Real Win16 waveOutOpen is a single stream, so starting this line implicitly cuts
        // whatever else was still playing at that moment. This port's multi-channel audio
        // model would otherwise let them audibly overlap.
        audio_.stop();
        audio_.playLoaded(portrait_.curse_blindness_wav, AudioFlags::Async);
        // Mirrors the real side effect of incrementing this field right after starting
        // playback — without arming this gate, the line would replay on every call until
        // ResolveStepArrival()'s own separate curse_step_counter++ happened to fire (which a
        // tunnel-entry transition never reaches, since it bypasses normal step arrival).
        render_state_[current_player_].curse_step_counter++;
    }
}

void BoardGameScreen::GainKey(int player, int province) {
    if (player < 0 || player >= 6 || province < 0 || province >= 6) return;

    // Bathory curse (curse_type==6): any key the cursed player would gain is redirected
    // to whichever player has char_id==3 (Bathory), unless she already holds it. Sets
    // lost_key_to_vampire=1 on the victim — read by TriggerEncounter's mode-6 curse-cure
    // reoffer threshold (BoardGameScreenEncounters.cpp).
    if (render_state_[player].curse_type == 6) {
        int bathory = -1;
        for (int i = 0; i < total_players_; ++i) {
            if (render_state_[i].char_id == 3) {
                bathory = i;
                break;
            }
        }
        if (bathory >= 0 && bathory != player && !render_state_[bathory].keys[province]) {
            bathory_key_redirect_audio_pending_ = true;
            render_state_[player].lost_key_to_vampire = true;
            GainKey(bathory, province);
            return;
        }
    }

    render_state_[player].keys[province] = 1;
    // ORs in kKeyPowerBitmask[char_id][province] into the multi-purpose flags byte.
    const uint8_t gained_bitmask = kKeyPowerBitmask[render_state_[player].char_id][province];
    render_state_[player].key_power_flags |= gained_bitmask;

    // Also clears black_hole_retry_counter when the key just gained is SPECIFICALLY the
    // black-hole-immunity one (bitmask exactly 0x10, not just that bit set among others) — an
    // early escape from the retry loop for whoever holds it.
    if (gained_bitmask == 0x10) {
        render_state_[player].black_hole_retry_counter = 0;
    }
}

void BoardGameScreen::StartBathoryKeyRedirectAudio() {
    if (!bathory_key_redirect_audio_pending_) return;
    bathory_key_redirect_audio_pending_ = false;
    bathory_key_redirect_wav_ = playWav("W6\\AT_0601.WAV", AudioFlags::Async);
}

bool BoardGameScreen::BathoryKeyRedirectAudioBusy() {
    if (bathory_key_redirect_audio_pending_) {
        StartBathoryKeyRedirectAudio();
        return true;
    }
    return bathory_key_redirect_wav_ != 0 && audio_.isPlaying(bathory_key_redirect_wav_);
}

void BoardGameScreen::LoseKey(int player, int province) {
    if (player < 0 || player >= 6 || province < 0 || province >= 6) return;
    render_state_[player].keys[province] = 0;
    // Clears the same bit back out.
    render_state_[player].key_power_flags &=
        static_cast<uint8_t>(~kKeyPowerBitmask[render_state_[player].char_id][province]);
    // Restores the key to the shared pool unconditionally, regardless of why it was lost (pure
    // penalty or duel-steal alike) — a duel-steal's LoseKey()+GainKey() pair therefore still
    // bumps the pool by one, since GainKey() never takes a copy back out of it.
    ++province_keys_remaining_[province];
}

int BoardGameScreen::FindUnheldProvince(int player) const {
    // Uses the same kKeyTable rotation as the initial key distribution in
    // BoardGameScreen::load(), not a plain (char_id+i)%6 walk.
    if (player < 0 || player >= 6) return -1;
    return ComputeFirstUnheldInRotation(render_state_[player].char_id, render_state_[player].keys);
}

int BoardGameScreen::FindHeldProvinceByRotation(int player) const {
    // Full loop over all 6 kKeyTable slots (does NOT stop early) — keeps overwriting the
    // result, so it ends up as the LAST held province in rotation order (the opposite end of
    // the same table FindUnheldProvince stops at first-empty).
    if (player < 0 || player >= 6) return -1;
    return ComputeLastHeldInRotation(render_state_[player].char_id, render_state_[player].keys);
}

void BoardGameScreen::DecrementCurrentPlayerCyclic() {
    // Rotates current_player_idx backward by one, wrapping at 0. Used for the
    // "extra turn" bonus (DispatchExtraTurn, FortuneCard result==1, OneDie's own prize
    // case 3, Blackjack's BJI1 win) and for DuelChooseOpponent's self-targeted duel cancel.
    //
    // EndTurn()'s own end-of-turn position save uses current_player_ as its target — correct
    // for the ordinary case, but THIS function decrements current_player_ directly well
    // before EndTurn() ever runs, from inside encounter dispatch. If EndTurn()'s save ran
    // afterward, it would copy the real mover's live GameState position into the WRONG
    // player's render_state (the one now sitting at the decremented index). Do that same
    // 3-field save HERE instead, at the one point current_player_ still correctly identifies
    // the real mover, and tell EndTurn() to skip its own copy this time.
    GameState* gs_soc = GameStateManager::Instance().Get();
    if (gs_soc) {
        SaveCurrentPlayerPositionFromGameState(gs_soc);
    }
    render_state_[current_player_].direction = mapfile_direction_;
    skip_position_save_ = true;
    if (current_player_ == 0) {
        current_player_ = static_cast<uint8_t>(total_players_ - 1);
    } else {
        current_player_ -= 1;
    }
}

int BoardGameScreen::FindNextProtectionTarget(int player, bool commit) {
    // Iterates player SLOTS 0..total_players_-1 (not char_id-rotation), looking for the
    // first other player whose char_id isn't yet flagged in `player`'s own
    // fortune_card_targeted[].
    if (player < 0 || player >= total_players_) return -1;
    for (int i = 0; i < total_players_; ++i) {
        const int target_char_id = render_state_[i].char_id;
        if (render_state_[player].char_id == target_char_id) continue;
        if (render_state_[player].fortune_card_targeted[target_char_id] == 0) {
            if (commit) render_state_[player].fortune_card_targeted[target_char_id] = 1;
            return target_char_id;
        }
    }
    return -1;
}

void BoardGameScreen::ShowCard(const char* folder, const char* name) {
    if (card_tex_) {
        renderer_.destroyTexture(card_tex_);
        card_tex_ = 0;
    }
    card_tex_ = assets_.texture(folder, name);
    if (card_tex_) {
        card_active_ = true;
    }
}

void BoardGameScreen::ResolveFateCard(int p) {
    // Variant selector:
    const int variant_roll = RollIndex(RngNext(), 10);
    int variant;
    if (variant_roll < 7 && render_state_[p].power >= 2) {
        variant = 0;  // SubtractPower penalty (~70%)
    } else if (variant_roll == 9 && !(render_state_[p].key_power_flags & 0x10)) {
        variant = 3;  // GateOpen
    } else {
        variant = (CountActivePlayers() == 1) ? 0 : 1;  // skip-turn (or power penalty if last left)
    }

    const char* plaque_folder = "FF1";
    std::string plaque_name;

    if (variant == 0) {
        // pen = (min(roll1,roll2)+1)*2, 3 power tiers.
        const uint16_t pw = render_state_[p].power;
        int r1, r2;
        if (pw < 25) {
            r1 = RollIndex(RngNext(), 3);
            r2 = RollIndex(RngNext(), 3);
        } else if (pw < 50) {
            r1 = RollIndex(RngNext(), 6);
            r2 = RollIndex(RngNext(), 6);
        } else {
            r1 = RollIndex(RngNext(), 3) + 3;
            r2 = RollIndex(RngNext(), 3) + 3;
        }
        const uint16_t pen = static_cast<uint16_t>((std::min(r1, r2) + 1) * 2);
        SubtractPower(p, pen);
        // string id = province*6 + pen/2 + base -> FF1/FATE{prov}_{pen:02}
        char nbuf[16];
        std::snprintf(nbuf, sizeof(nbuf), "FATE%u_%02u", render_state_[p].province, pen);
        plaque_name = nbuf;
        static const char* kFatePenaltyWav[4] = {"W3\\AT_0471D.WAV", "W3\\AT_0471E.WAV",
                                                 "W3\\AT_0473A.WAV", "W3\\AT_0473B.WAV"};
        card_wav_ = RollAudio(kFatePenaltyWav, 4);
    } else if (variant == 1) {
        render_state_[p].skip_turn_counter = 1;
        plaque_name = "FATE_S02";  // fixed string
        static const char* kSkipTurnWav[6] = {"W2\\AT_0271.WAV", "W5\\AT_0570.WAV",
                                              "W5\\AT_0572.WAV", "W5\\AT_0573.WAV",
                                              "W5\\AT_0574.WAV", "W5\\AT_0578.WAV"};
        card_wav_ = RollAudio(kSkipTurnWav, 6);
    } else {  // variant == 3: GateOpen
        TeleportToNearestBlackHole(p);
        plaque_name = "FATE_S01";  // fixed string
        card_wav_ = RollAudio(kGateOpenWav, 7);
    }

    // Always shows a bitmap for whichever variant fired, 3s.
    ShowCard(plaque_folder, plaque_name.c_str());
}

void BoardGameScreen::ResolveFortuneCard(int p) {
    const int fq_val = RollIndex(RngNext(), 100);
    const int kc = CountHeldKeys(p);
    int result;
    int target = -1;
    if (fq_val < 98 || kc > 5) {
        target = FindNextProtectionTarget(p, false);
        if (fq_val < 89 || target == -1) {
            result = (fq_val < 74 || CountActivePlayers() == 1) ? 0 : 1;
        } else {
            result = 4;
        }
    } else {
        result = 2;
    }

    const char* plaque_folder = "FF2";
    std::string plaque_name;

    if (result == 0) {
        // bonus = max(roll1,roll2)*2, 3 power tiers.
        const uint16_t pw = render_state_[p].power;
        int r1, r2;
        if (pw < 25) {
            r1 = RollIndex(RngNext(), 2) + 5;
            r2 = RollIndex(RngNext(), 2) + 5;
        } else if (pw < 50) {
            r1 = RollIndex(RngNext(), 6) + 1;
            r2 = RollIndex(RngNext(), 6) + 1;
        } else {
            r1 = RollIndex(RngNext(), 3) + 1;
            r2 = RollIndex(RngNext(), 3) + 1;
        }
        const uint16_t bonus = static_cast<uint16_t>(std::max(r1, r2) * 2);
        AddPower(p, bonus);
        // Real RT_STRING table: 34 of 36 entries read "ff2\fort{prov}_{amount:02}"; exactly
        // province 0 with amount 2 or 4 read "ff1\fort0_02"/"ff1\fort0_04" instead — a real,
        // deliberate exception in the original's own data (verified by dumping all 36
        // entries directly), not a misplaced CD file.
        plaque_folder =
            (render_state_[p].province == 0 && (bonus == 2 || bonus == 4)) ? "FF1" : "FF2";
        char nbuf[16];
        std::snprintf(nbuf, sizeof(nbuf), "FORT%u_%02u", render_state_[p].province, bonus);
        plaque_name = nbuf;
        static const char* kFortunePowerWav[11] = {
            "W4\\AT_0505.WAV",  "W2\\AT_0319.WAV",  "W2\\AT_0322A.WAV", "W4\\AT_0477.WAV",
            "W4\\AT_0511.WAV",  "W2\\AT_0323A.WAV", "W4\\AT_0514.WAV",  "W2\\AT_0325A.WAV",
            "W2\\AT_0325B.WAV", "W3\\AT_0472A.WAV", "W3\\AT_0472B.WAV"};
        card_wav_ = RollAudio(kFortunePowerWav, 11);
    } else if (result == 1) {
        DecrementCurrentPlayerCyclic();
        plaque_name = "FORT_S01";  // fixed string
        static const char* kExtraTurnWav[2] = {"W5\\AT_0575.WAV", "W5\\AT_0576.WAV"};
        card_wav_ = RollAudio(kExtraTurnWav, 2);
    } else if (result == 2) {
        const int prov = FindUnheldProvince(p);
        if (prov >= 0) GainKey(p, prov);
        // Literal RT_STRING table (province-indexed, NOT a clean per-province formula —
        // folder/numbering genuinely mismatched in the original asset table, confirmed via
        // direct string dump, not a transcription error).
        static const char* kGainKeyFolder[6] = {"FF3", "FF2", "FF2", "FF2", "FF2", "FF2"};
        static const char* kGainKeyPlaque[6] = {"FORT_S07", "FORT_S04", "FORT_S03",
                                                "FORT_S02", "FORT_S05", "FORT_S06"};
        const int idx = (prov >= 0 && prov < 6) ? prov : 0;
        plaque_folder = kGainKeyFolder[idx];
        plaque_name = kGainKeyPlaque[idx];
        static const char* kGainKeyWav[3] = {"W6\\AT_0591.WAV", "W6\\AT_0592.WAV",
                                             "W4\\AT_0476.WAV"};
        card_wav_ = RollAudio(kGainKeyWav, 3);
    } else {  // result == 4: Protection — names one character who'll auto-lose
              // if they later duel-attack you (fortune_card_targeted)
        target = FindNextProtectionTarget(p, true);
        const int target_id = (target >= 0 && target < 6) ? target : 0;
        plaque_folder = "FF3";
        plaque_name = kFortuneCard[target_id];
        static const char* kNameOppWavA[6] = {"W6\\AT_0636.WAV", "W6\\AT_0641.WAV",
                                              "W6\\AT_0637.WAV", "W6\\AT_0639.WAV",
                                              "W6\\AT_0638.WAV", "W6\\AT_0640.WAV"};
        static const char* kNameOppWavB[6] = {"W7\\AT_0642.WAV", "W7\\AT_0643.WAV",
                                              "W7\\AT_0644.WAV", "W7\\AT_0646.WAV",
                                              "W7\\AT_0645.WAV", "W7\\AT_0647.WAV"};
        const bool pick_b = RollIndex(RngNext(), 2) != 0;
        card_wav_ =
            playWav(pick_b ? kNameOppWavB[target_id] : kNameOppWavA[target_id], AudioFlags::Async);
    }

    // Always shows a bitmap for whichever result fired, 3s. plaque_folder is decided up front
    // from the real, fully-verified 36-entry string table (see the result==0 branch above),
    // not inferred reactively from whether a guessed path happens to load.
    ShowCard(plaque_folder, plaque_name.c_str());
}

void BoardGameScreen::ApplyCurse(int loser, int winner_char_id) {
    if (loser < 0 || loser >= 6 || winner_char_id < 0 || winner_char_id >= 6) return;
    // The curse branch: victim->curse_type = kCurseTable[winner_char_id],
    // victim->curse_step_counter = 0. kCurseTable structurally confirmed (mode 6's hardcoded
    // char_id-3 target matches table[3]=6 self-consistently).
    static constexpr uint16_t kCurseTable[6] = {1, 3, 4, 6, 2, 5};
    render_state_[loser].curse_type = kCurseTable[winner_char_id];
    render_state_[loser].curse_step_counter = 0;
}

}  // namespace openfear::game
