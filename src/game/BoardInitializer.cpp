#include "openfear/game/BoardInitializer.h"
#include "openfear/game/GameState.h"
#include <cstdio>

namespace openfear::game {

bool BoardInitializer::initialized_ = false;

bool BoardInitializer::Init(int duration_minutes, int turn_seconds) {

    GameState* gs = GameStateManager::Instance().Get();
    if (!gs || !gs->IsValid()) {
        std::fprintf(stderr, "[BoardInit] ERROR: GameState invalid or not initialized\n");
        return false;
    }

    gs->SetGameDurationMinutes(duration_minutes);
    gs->SetTurnTimeSeconds(static_cast<uint16_t>(turn_seconds));

    initialized_ = true;
    return true;
}

bool BoardInitializer::IsInitialized() {
    return initialized_;
}

}  // namespace openfear::game
