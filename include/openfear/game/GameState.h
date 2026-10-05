#pragma once

#include <cstdint>

namespace openfear::game {

// Central game runtime object - a process-lifetime singleton shared across
// setup screens and BoardGameScreen for the handful of values that need to
// survive screen transitions (current position, timers, player count).
struct GameState {
    uint16_t type_id = 0;  // mandatory tag, must be 0x618D

    uint16_t total_players = 0;  // total player count (1-6)

    uint16_t player_idx = 0;  // active player's current province (0-7)
    uint16_t sub_idx = 0;     // stone index within province

    // Per-minute armed flags for ambient encounter type 16 (last 5 min of
    // the game). GLOBAL, shared across every player - not per-player.
    uint16_t ambient_trigger_armed[5] = {};

    uint32_t game_duration_s_ = 0;     // game duration x 60, set by MasterControlScreen
    uint16_t turn_time_limit_ms_ = 0;  // time per turn x 1000; reset every turn

    bool IsValid() const { return type_id == 0x618D; }

    void SetGameDurationMinutes(uint32_t minutes) { game_duration_s_ = minutes * 60; }
    void SetTurnTimeSeconds(uint16_t seconds) { turn_time_limit_ms_ = seconds * 1000; }

    void ResetPerTurnFields() { turn_time_limit_ms_ = 0; }
};

// ---- Singleton manager ----
class GameStateManager {
public:
    static GameStateManager& Instance() {
        static GameStateManager instance;
        return instance;
    }
    GameState* Get() { return &gs_; }

private:
    GameStateManager() = default;
    GameStateManager(const GameStateManager&) = delete;
    GameStateManager& operator=(const GameStateManager&) = delete;
    GameState gs_;
};

}  // namespace openfear::game
