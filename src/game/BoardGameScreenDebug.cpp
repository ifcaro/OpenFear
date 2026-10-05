#include "openfear/game/BoardGameScreen.h"
#include "openfear/game/GameState.h"
#include <chrono>
#include <cstdint>

// Manual-QA / debug-only helpers: every method here is reachable exclusively through
// apps/openfear/DebugQuickstart.h's --test-*/--cheat-* flags (excluded from Release builds)
// or the headless test suite's deterministic hooks (SetForceEncounterType etc.) - kept in its
// own translation unit, separate from BoardGameScreen.cpp's real gameplay logic.

namespace openfear::game {

// Inter-province portal table's entry type - real definition (kPortalTable) lives in
// BoardGameScreen.cpp, given real (external) linkage there specifically so this file can
// share it instead of duplicating the 24-entry table. Kept out of BoardGameScreen.h since
// it is not part of the class's public surface, just an implementation detail both .cpp
// files happen to need.
struct PortalEntry {
    uint16_t src_stone, src_prov, dst_stone, dst_prov;
};
extern const PortalEntry kPortalTable[24];

// Key-power permutation table — real definition (kKeyPowerBitmask) lives in
// BoardGameScreenDuelAndKeys.cpp, given real (external) linkage there so this file can
// share it instead of duplicating the 36-byte table.
extern const uint8_t kKeyPowerBitmask[6][6];

void BoardGameScreen::SetForceEncounterType(int enc_type) {
    force_encounter_type_ = (enc_type >= 1 && enc_type <= 16) ? enc_type : 0;
}

void BoardGameScreen::SetForceEncounterTypeOnce(bool once) {
    force_encounter_type_once_ = once;
}

void BoardGameScreen::DebugEnterMoving(int steps) {
    render_state_[current_player_].steps_remaining = static_cast<uint16_t>(steps);
    render_state_[current_player_].steps_total_this_roll = static_cast<uint16_t>(steps);
    dice_.move_distance = steps;
    current_state_ = BoardGameState::Moving;
    state_start_time_ = std::chrono::steady_clock::now();
    state_action_executed_ = false;
    // turn_start_time_ normally gets set once at real turn-start (RollingDice entry) — these
    // debug shortcuts jump straight into Moving without ever passing through there, so it
    // must be set explicitly or the global turn-timeout check (which reads turn_start_time_,
    // not state_start_time_) sees a huge bogus elapsed time and immediately reverts the
    // player back to their starting position.
    turn_start_time_ = std::chrono::steady_clock::now();
}

void BoardGameScreen::DebugForceStoneArrival(int stone_type, int repeat) {
    for (uint16_t prov = 0; prov < MapFile::kProvinceCount; ++prov) {
        const uint16_t count = mapfile_.GetStoneCount(prov);
        for (uint16_t s = 0; s < count; ++s) {
            const MapStone* st = mapfile_.GetStone(prov, s);
            if (st && st->type == stone_type) {
                render_state_[current_player_].province = prov;
                render_state_[current_player_].stone_id = s;
                GameState* gsw = GameStateManager::Instance().Get();
                if (gsw) {
                    gsw->player_idx = prov;
                    gsw->sub_idx = s;
                }
                ResolveStepArrival();
                if (repeat > 1 && (stone_type == 3 || stone_type == 4)) {
                    debug_stone_repeat_remaining_ = repeat - 1;
                    debug_stone_repeat_type_ = stone_type;
                }
                return;
            }
        }
    }
}

void BoardGameScreen::DebugShowPortrait() {
    current_state_ = BoardGameState::ShowingPortrait;
    state_start_time_ = std::chrono::steady_clock::now();
    state_action_executed_ = false;
}

void BoardGameScreen::DebugSetupTobogganTest() {
    render_state_[current_player_].province = 5;
    render_state_[current_player_].stone_id = 63;
    GameState* gsw = GameStateManager::Instance().Get();
    if (gsw) {
        gsw->player_idx = 5;
        gsw->sub_idx = 63;
    }
    mapfile_direction_ = 3;  // West — the one direction on stone 63 whose dest==64
    DebugEnterMoving(1);
}

void BoardGameScreen::DebugSetupGateArrivalTest(int door_index) {
    // Takes the real door index (0-23) and looks up the real portal table — lets every one of
    // the 24 real gate crossings be spot-checked with the same harness. Grants all 6 keys
    // unconditionally: harmless for entries that don't require them, needed for the ones that do.
    if (door_index < 0 || door_index > 23) door_index = 0;
    const int province = kPortalTable[door_index].src_prov;
    const int stone_id = kPortalTable[door_index].src_stone;
    render_state_[current_player_].province = static_cast<uint16_t>(province);
    render_state_[current_player_].stone_id = static_cast<uint16_t>(stone_id);
    for (int k = 0; k < 6; ++k) render_state_[current_player_].keys[k] = 1;
    GameState* gsw = GameStateManager::Instance().Get();
    if (gsw) {
        gsw->player_idx = static_cast<uint16_t>(province);
        gsw->sub_idx = static_cast<uint16_t>(stone_id);
    }
    // Auto-detect the gate-trigger direction: the one facing where dest==kDestGate.
    uint8_t gate_dir = 0;
    for (uint8_t d = 0; d < 4; ++d) {
        if (mapfile_.GetDest(static_cast<uint16_t>(province), static_cast<uint16_t>(stone_id), d) ==
            kDestGate) {
            gate_dir = d;
            break;
        }
    }
    mapfile_direction_ = gate_dir;
    DebugEnterMoving(1);
}

void BoardGameScreen::DebugSetupTunnelEntryTest(int province) {
    // Spot-checks tunnel entry for all 6 source provinces. Stone 0 is the tunnel-entry stone
    // in every province, including province 2 (its stone 0 has no wall direction in the raw
    // MAPFILE data, but MapFile::Load()'s baked-in patch adds one at South).
    render_state_[current_player_].province = static_cast<uint16_t>(province);
    render_state_[current_player_].stone_id = 0;
    render_state_[current_player_].key_power_flags |=
        0x01;  // own home-province key — entry eligibility
    GameState* gsw = GameStateManager::Instance().Get();
    if (gsw) {
        gsw->player_idx = static_cast<uint16_t>(province);
        gsw->sub_idx = 0;
    }
    uint8_t wall_dir = 0;
    for (uint8_t d = 0; d < 4; ++d) {
        if (mapfile_.GetDest(static_cast<uint16_t>(province), 0, d) == kDestWall) {
            wall_dir = d;
            break;
        }
    }
    mapfile_direction_ = wall_dir;
    DebugEnterMoving(1);
}

void BoardGameScreen::DebugSetupTunnelExitTest(int origin_province) {
    // Spot-checks tunnel exit for all 6 origin provinces. Teleports into province 7 at
    // kTunnelEntryStoneTable[origin_province], facing the confirmed wall direction (North for
    // every one of the 6 exit stones, MAPFILE-verified).
    static constexpr uint16_t kTunnelEntryStoneTable[6] = {25, 49, 0, 13, 61, 37};
    const uint16_t stone_id = kTunnelEntryStoneTable[origin_province];
    render_state_[current_player_].province = 7;
    render_state_[current_player_].stone_id = stone_id;
    GameState* gsw = GameStateManager::Instance().Get();
    if (gsw) {
        gsw->player_idx = 7;
        gsw->sub_idx = stone_id;
    }
    uint8_t wall_dir = 0;
    for (uint8_t d = 0; d < 4; ++d) {
        if (mapfile_.GetDest(7, stone_id, d) == kDestWall) {
            wall_dir = d;
            break;
        }
    }
    mapfile_direction_ = wall_dir;
    DebugEnterMoving(1);
}

void BoardGameScreen::DebugSetupFlightTest() {
    for (uint16_t prov = 0; prov < 6; ++prov) {
        const uint16_t count = mapfile_.GetStoneCount(prov);
        for (uint16_t s = 0; s < count; ++s) {
            const MapStone* st = mapfile_.GetStone(prov, s);
            if (!st || st->type != 6) continue;
            // Pick any direction with a real connection (not wall/no-connection) so the
            // Moving state's own rendering has something coherent to show.
            int dir = -1;
            for (int d = 0; d < 4; ++d) {
                if (st->dest[d] >= 0) {
                    dir = d;
                    break;
                }
            }
            if (dir < 0) continue;  // shouldn't happen for a real junction stone

            render_state_[current_player_].province = prov;
            render_state_[current_player_].stone_id = s;
            // Must use DebugSetPlayerKeys, not a raw keys[]=1 loop — the Flight gate checks
            // key_power_flags bit2, which only DebugSetPlayerKeys mirrors from GainKey's real
            // side effect (kKeyPowerBitmask). A raw keys[] write leaves key_power_flags
            // untouched.
            DebugSetPlayerKeys(current_player_, 0x3F);
            GameState* gsw = GameStateManager::Instance().Get();
            if (gsw) {
                gsw->player_idx = prov;
                gsw->sub_idx = s;
            }
            mapfile_direction_ = dir;
            DebugEnterMoving(2);
            return;
        }
    }
}

void BoardGameScreen::DebugForceGatekeeperChallenge() {
    render_state_[current_player_].province = 6;
    render_state_[current_player_].stone_id = 15;
    for (int k = 0; k < 6; ++k) render_state_[current_player_].keys[k] = 1;
    GameState* gsw = GameStateManager::Instance().Get();
    if (gsw) {
        gsw->player_idx = 6;
        gsw->sub_idx = 15;
    }
    mapfile_direction_ = 1;  // East — the one direction on stone 15 whose dest==16
    DebugEnterMoving(1);
}

void BoardGameScreen::DebugSetPlayerKeys(int player_idx, uint8_t key_mask) {
    if (player_idx < 0 || player_idx >= total_players_) return;
    for (int k = 0; k < 6; ++k) {
        render_state_[player_idx].keys[k] = (key_mask >> k) & 1;
        // Mirror GainKey/LoseKey's real side effect: OR in the (char_id, province) bit when
        // the key is held, AND it back out when it isn't — so a 2nd call with a smaller mask
        // (e.g. resetting to 0x00 to override a previous grant) actually clears the derived
        // flags too, not just the raw keys[] array.
        const uint8_t bit = kKeyPowerBitmask[render_state_[player_idx].char_id][k];
        if ((key_mask >> k) & 1) {
            render_state_[player_idx].key_power_flags |= bit;
        } else {
            render_state_[player_idx].key_power_flags &= static_cast<uint8_t>(~bit);
        }
    }
}

void BoardGameScreen::DebugSetPlayerCurse(int player_idx, int curse_mode) {
    if (player_idx < 0 || player_idx >= total_players_) return;
    if (curse_mode < 0 || curse_mode > 6) return;
    render_state_[player_idx].curse_type = static_cast<uint16_t>(curse_mode);
}

void BoardGameScreen::DebugSetPlayerPower(int player_idx, uint16_t power) {
    if (player_idx < 0 || player_idx >= total_players_) return;
    render_state_[player_idx].power = power;
}

void BoardGameScreen::DebugSetDiceRestricted(int player_idx, bool restricted) {
    if (player_idx < 0 || player_idx >= total_players_) return;
    render_state_[player_idx].dice_restricted = restricted ? 1 : 2;
    render_state_[player_idx].dice_restriction_turns = restricted ? 1 : 0;
}

void BoardGameScreen::DebugSetProvinceKeysRemaining(int province, uint16_t remaining) {
    if (province < 0 || province >= 6) return;
    province_keys_remaining_[province] = remaining;
}

void BoardGameScreen::DebugForceDoubleSixRoll() {
    force_double_six_roll_ = true;
}

void BoardGameScreen::DebugSetFortuneCardTargeted(int target_player_idx, int attacker_char_id) {
    if (target_player_idx < 0 || target_player_idx >= total_players_) return;
    if (attacker_char_id < 0 || attacker_char_id >= 6) return;
    render_state_[target_player_idx].fortune_card_targeted[attacker_char_id] = 1;
}

}  // namespace openfear::game
