#pragma once

#include <cstdint>

namespace openfear::game {

struct PlayerObject;

// Player bookkeeping (setup-screen human selection + AI character
// assignment) and the game's own RNG (a 32-bit LCG, seeded once at startup
// from wall-clock time). Human/AI differentiation is via
// PlayerObject::human_flag; slot 0 is always the first human player.
// Purely static — no per-instance state, every slot/seed is process-global.
class PlayerInitializer {
public:
    static PlayerObject* GetPlayer(uint8_t player_idx);

    // PickCharScreen: user selects a character for this player slot.
    static void MarkPlayerAsHuman(uint8_t player_idx, uint16_t character_id);

    // FearSelectScreen: marks the slot human (the fear_id itself is threaded
    // separately by the caller, not stored on PlayerObject).
    static void MarkPlayerFearAndHuman(uint8_t player_idx, uint16_t fear_id);

    // Fills AI character IDs for slots 1..total_players-1 (slot 0 is
    // always the human player, never touched here). AI players never get a
    // fear assigned, matching the original — they stay fear_index=0
    // (unset/immune to the gatekeeper's fear check).
    static void AssignAICharacters(uint8_t total_players);

    // Exposed for testing the RNG/seed.
    static uint32_t GetCurrentRandomSeed();

    // Core LCG step: seed = seed * 0x015A4E35 + 1 (mod 2^32); returns the
    // upper 16 bits masked to 15 (0-32767). The single RNG used throughout
    // the original game.
    static uint16_t GetRandomNumber();

    // Seeds the RNG from wall-clock time. Call once at program startup,
    // before any RNG use.
    static void InitializeSeed();

private:
    // Random character ID (0-5), regenerated until it doesn't collide with
    // any already-processed player slot (0..current_player_index-1).
    static uint16_t GenerateRandomCharacterWithDedup(uint8_t current_player_index);
};

}  // namespace openfear::game
