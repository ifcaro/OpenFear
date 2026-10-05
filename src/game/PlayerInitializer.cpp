#include "openfear/game/PlayerInitializer.h"
#include "openfear/game/GameState.h"
#include "openfear/game/PlayerObject.h"
#include <cstdio>
#include <chrono>

namespace openfear::game {

static PlayerObject g_PlayerObjects[6];

// Seeded once at startup from wall-clock time — the original seeds from a
// single 16-bit DOS epoch-seconds value with the high word forced to 0;
// this port uses host wall-clock seconds masked to 16 bits instead of
// replicating the exact DOS date/time math, since nothing depends on
// reproducing the original's specific seed value.
static uint32_t g_RandomSeed = 0;

void PlayerInitializer::InitializeSeed() {
    const auto now = std::chrono::system_clock::now();
    const uint64_t epoch_seconds = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
    const uint16_t seed_low = static_cast<uint16_t>(epoch_seconds & 0xffff);

    g_RandomSeed = static_cast<uint32_t>(seed_low);  // high word forced to 0
}

PlayerObject* PlayerInitializer::GetPlayer(uint8_t player_idx) {
    if (player_idx >= 6) {
        std::fprintf(stderr, "[PlayerInit] ERROR: Invalid player index %d\n", player_idx);
        return nullptr;
    }
    return &g_PlayerObjects[player_idx];
}

void PlayerInitializer::MarkPlayerAsHuman(uint8_t player_idx, uint16_t character_id) {
    PlayerObject* player = GetPlayer(player_idx);
    if (!player) return;

    player->character_id = character_id;
}

void PlayerInitializer::MarkPlayerFearAndHuman(uint8_t player_idx, uint16_t fear_id) {
    PlayerObject* player = GetPlayer(player_idx);
    if (!player) return;

    player->human_flag = 0x1;
}

void PlayerInitializer::AssignAICharacters(uint8_t total_players) {

    GameState* gs = GameStateManager::Instance().Get();
    if (gs && gs->IsValid()) {
        gs->total_players = total_players;
    }

    // Slot 0 = first human player, always skipped.
    for (uint8_t i = 1; i < total_players && i < 6; i++) {
        PlayerObject* player = GetPlayer(i);
        if (!player) continue;

        uint16_t ai_character = GenerateRandomCharacterWithDedup(i);
        player->character_id = ai_character;
    }
}

uint16_t PlayerInitializer::GenerateRandomCharacterWithDedup(uint8_t current_player_index) {
    const uint16_t NUM_CHARACTERS = 6;
    // Divides by 0x8000 (32768), not the 15-bit max 0x7fff — matches the
    // original's divisor exactly.
    const uint16_t RANDOM_MAX = 0x8000;

    uint16_t character_id;

    do {
        uint16_t random_value = GetRandomNumber();
        character_id = (random_value * NUM_CHARACTERS) / RANDOM_MAX;

        bool is_duplicate = false;
        for (uint8_t j = 0; j < current_player_index; j++) {
            PlayerObject* existing_player = GetPlayer(j);
            if (existing_player && existing_player->character_id == character_id) {
                is_duplicate = true;
                break;
            }
        }

        if (!is_duplicate) {
            break;
        }

    } while (true);

    return character_id;
}

uint32_t PlayerInitializer::GetCurrentRandomSeed() {
    return g_RandomSeed;
}

uint16_t PlayerInitializer::GetRandomNumber() {
    static const uint32_t MULTIPLIER = 0x015A4E35;
    static const uint32_t INCREMENT = 1;

    g_RandomSeed = (g_RandomSeed * MULTIPLIER) + INCREMENT;

    return static_cast<uint16_t>((g_RandomSeed >> 16) & 0x7fff);
}

}  // namespace openfear::game
