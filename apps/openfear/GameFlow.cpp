#include "GameFlow.h"
#include "PostGameSequence.h"
#include "RuntimeDiagnostics.h"
#include "ScreenLoop.h"
#ifndef NDEBUG
#  include "DebugQuickstart.h"
#endif

#include "openfear/game/BoardGameScreen.h"
#include "openfear/game/BoardInitializer.h"
#include "openfear/game/ChooseLevelScreen.h"
#include "openfear/game/FearSelectScreen.h"
#include "openfear/game/GameState.h"
#include "openfear/game/IntroScreen.h"
#include "openfear/game/MasterControlScreen.h"
#include "openfear/game/PickCharScreen.h"
#include "openfear/game/PickPlayersScreen.h"
#include "openfear/game/PlayerInitializer.h"
#include "openfear/platform/BitmapFont.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <string>

namespace openfear {
namespace {

// 10 difficulty levels (thermometer maps 0-90 -> level 0-9). The real
// game's output field is TOTAL players (human + AI); this table's
// ai_count is AI-only (total - 1).
struct DifficultyConfig {
    int initial_keys;
    int ai_count;
    uint16_t ai_power;
};
constexpr DifficultyConfig kDifficultyTable[10] = {
    {6, 0, 0},  {4, 0, 0},  {2, 0, 0},  {0, 0, 0},  {4, 1, 10},
    {2, 1, 20}, {0, 1, 25}, {2, 2, 10}, {2, 2, 20}, {0, 2, 20},
};

}  // namespace

int RunGameFlow(int argc, char* argv[], Platform* platform, assets::AssetManager& assets,
                const AssetPaths& paths) {
    // Path relative to the asset root, regardless of how assets are
    // loaded - IAssetProvider resolves it against the real root.
    // assets.asset_root() below is only for the log line, never joined
    // into a path.
    std::string exe_path = "ATMOS.EXE";
    LOG("Loading EXE from: %s (asset root: %s)\n", exe_path.c_str(),
        assets.asset_root().string().c_str());
    if (!assets.Init(exe_path)) {
        std::fprintf(stderr, "[MAIN] ERROR: Failed to load ATMOS.EXE (asset root: %s)\n",
                     assets.asset_root().string().c_str());
        platform->shutdown();
        return 1;
    }
    LOG("EXE parsed successfully\n");

    auto* gs = game::GameStateManager::Instance().Get();
    if (gs) {
        gs->type_id = 0x618d;  // mandatory type-validation marker
        // Index 0 (current minute, if <1 min left) starts unarmed;
        // indices 1-4 start armed.
        for (int i = 0; i < 5; ++i) gs->ambient_trigger_armed[i] = (i == 0) ? 0 : 1;
    }

    if (!assets.LoadCursors()) {
        std::fprintf(stderr, "[MAIN] Warning: Failed to load all cursor resources\n");
    }
    platform->renderer->loadGameCursors(&assets);

    game::PlayerInitializer::InitializeSeed();

#ifndef NDEBUG
    if (auto exit_code = debugqs::RunIfRequested(argc, argv, *platform, assets, paths)) {
        return *exit_code;
    }
#endif

    bool play_again = true;
    while (play_again) {
        play_again = false;

        LOG("--- ENTER Intro ---\n");
        game::IntroScreen intro(assets, *platform->renderer, *platform->audio, *platform->video);
        if (intro.load(paths)) {
            bool intro_ok = RunScreenLoop(intro, *platform);
            intro.unload();
            if (!intro_ok) {
                platform->shutdown();
                return 0;
            }
        }
        LOG("--- EXIT Intro ---\n");

        LOG("--- ENTER PickPlayers ---\n");
        game::PickPlayersScreen pick_players(assets, *platform->renderer, *platform->audio,
                                             *platform->video);
        if (!pick_players.load(paths)) {
            std::fputs("Failed to load DATA/PKPLYRS.BIN\n", stderr);
            platform->shutdown();
            return 1;
        }
        bool pick_players_ok = RunScreenLoop(pick_players, *platform);
        pick_players.unload();
        if (!pick_players_ok) {
            platform->shutdown();
            return 0;
        }
        const int human_player_count = pick_players.player_count();
        LOG("--- EXIT PickPlayers: player_count=%d ---\n", human_player_count);

        std::array<bool, 6> taken_chars{};
        std::array<bool, 6> taken_fears{};
        std::array<int, 6> player_fears{};
        player_fears.fill(-1);

        game::PickCharScreen pick_char(assets, *platform->renderer, *platform->audio);
        game::FearSelectScreen fear_screen(assets, *platform->renderer, *platform->audio);

        for (int player = 0; player < human_player_count; ++player) {
            LOG("=== Player loop: player=%d/%d ===\n", player + 1, human_player_count);
            {
                char tag[32];
                std::snprintf(tag, sizeof(tag), "player%d-start", player + 1);
                LogMemoryStats(tag, *platform->renderer);
            }

            if (!pick_char.load(paths, player, human_player_count, taken_chars)) {
                std::fputs("Failed to load DATA/PKCHAR.BIN\n", stderr);
                platform->shutdown();
                return 1;
            }
#ifndef NDEBUG
            const int test_char = debugqs::ParseTestChar(argc, argv);
            if (test_char >= 0 && test_char < 6 && !taken_chars[test_char]) {
                pick_char.DebugForceCharacter(test_char);
            }
#endif
            if (!RunScreenLoop(pick_char, *platform)) {
                platform->shutdown();
                return 0;
            }
            const int char_id = pick_char.selected_char();
            if (char_id >= 0) {
                taken_chars[char_id] = true;
                game::PlayerInitializer::MarkPlayerAsHuman(static_cast<uint8_t>(player),
                                                           static_cast<uint16_t>(char_id));
            }
            LOG("--- PickChar: player=%d char_id=%d ---\n", player + 1, char_id);
            {
                char tag[32];
                std::snprintf(tag, sizeof(tag), "player%d-pickchar-done", player + 1);
                LogMemoryStats(tag, *platform->renderer);
            }

            if (!fear_screen.load(paths, player, taken_fears)) {
                std::fprintf(stderr, "[FLOW] FearSelectScreen load failed for player %d\n",
                             player + 1);
                platform->shutdown();
                return 1;
            }
            if (!RunScreenLoop(fear_screen, *platform)) {
                platform->shutdown();
                return 0;
            }
            const int fear_id = fear_screen.selected_fear();
            if (fear_id >= 0) {
                taken_fears[fear_id] = true;
                player_fears[player] = fear_id;
                game::PlayerInitializer::MarkPlayerFearAndHuman(static_cast<uint8_t>(player),
                                                                static_cast<uint16_t>(fear_id));
            }
            LOG("--- FearSelect: player=%d fear_id=%d ---\n", player + 1, fear_id);
        }
        pick_char.unload();
        fear_screen.unload();

        int total_players = human_player_count;
        int initial_keys = 0;
        uint16_t ai_power = 0;

        if (human_player_count == 1) {
            LOG("--- ENTER ChooseLevel (1P) ---\n");
            game::ChooseLevelScreen choose_level(assets, *platform->renderer, *platform->audio);
            if (choose_level.load(paths)) {
                bool choose_level_ok = RunScreenLoop(choose_level, *platform);
                if (!choose_level_ok) {
                    choose_level.unload();
                    platform->shutdown();
                    return 0;
                }
                int difficulty = std::clamp(choose_level.selected_level(), 0, 9);
                const auto& difficulty_cfg = kDifficultyTable[difficulty];
                total_players = 1 + difficulty_cfg.ai_count;
                initial_keys = difficulty_cfg.initial_keys;
                ai_power = difficulty_cfg.ai_power;
                LOG("--- EXIT ChooseLevel: level=%d keys=%d AI=%d ai_power=%u total_players=%d "
                    "---\n",
                    difficulty, initial_keys, difficulty_cfg.ai_count, ai_power, total_players);
                choose_level.unload();
            }
        }

        if (human_player_count == 1 && total_players != human_player_count && gs && gs->IsValid()) {
            gs->total_players = static_cast<uint8_t>(total_players);
        }

        LOG("--- ENTER MasterControl (total_players=%d) ---\n", total_players);
        game::MasterControlScreen master_ctrl(assets, *platform->renderer, *platform->audio);
        if (!master_ctrl.load(paths, total_players)) {
            std::fputs("Failed to load MasterControlScreen\n", stderr);
            platform->shutdown();
            return 1;
        }
        if (!RunScreenLoop(master_ctrl, *platform)) {
            master_ctrl.unload();
            platform->shutdown();
            return 0;
        }
        LOG("--- EXIT MasterControl: game_duration=%d turn_time=%d ---\n",
            master_ctrl.gameDurationMinutes(), master_ctrl.turnTimeSeconds());
        master_ctrl.unload();
        platform->audio->releaseSetupPhaseCache();

        // AI-character assignment only runs for 1-player games, gated on
        // human_player_count rather than gs->total_players — the latter has
        // already been promoted to 1+ai_count above, so it can no longer
        // tell "1P with 1 AI" apart from a real 2P all-human game.
        LOG("--- AIAssign ---\n");
        if (human_player_count == 1 && gs && gs->IsValid()) {
            const int num_players = std::clamp<int>(gs->total_players, 1, 6);
            LOG("AIAssign: calling AssignAICharacters for %d players\n", num_players);
            game::PlayerInitializer::AssignAICharacters(static_cast<uint8_t>(num_players));
        } else {
            LOG("AIAssign: skipped — human_player_count=%d, not 1P mode\n", human_player_count);
        }

        LOG("--- ENTER Board Initialization ---\n");
        if (!game::BoardInitializer::Init(master_ctrl.gameDurationMinutes(),
                                          master_ctrl.turnTimeSeconds())) {
            std::fputs("Board initialization failed\n", stderr);
            platform->shutdown();
            return 1;
        }
        LOG("--- EXIT Board Initialization ---\n");

        for (int i = human_player_count; i < total_players && i < 6; ++i) player_fears[i] = 0;

        LOG("--- ENTER BoardGameLoop ---\n");
        game::BoardGameScreen board_game(assets, *platform->renderer, *platform->audio,
                                         *platform->video);
        board_game.Configure(initial_keys, ai_power, player_fears.data(), total_players);
#ifndef NDEBUG
        const auto cheats = debugqs::ParseCheatFlags(argc, argv);
        if (cheats.easy_win) {
            board_game.SetCheatEasyWin(true);
            LOG("--- CHEAT: --cheat-easy-win active ---\n");
        }
#endif
        {
            auto load_t0 = std::chrono::steady_clock::now();
            bool load_ok = board_game.load(paths);
            float load_ms =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - load_t0)
                    .count();
            LOG("--- BoardGameLoop: load() took %.1fms ---\n", load_ms);
            if (!load_ok) {
                // stderr alone is invisible on real hardware with no
                // console attached - draw the error on screen too before
                // stopping, instead of exiting silently.
                std::fprintf(stderr, "[MAIN] FATAL: board game failed to load (asset root: %s)\n",
                             assets.asset_root().string().c_str());
                platform::BitmapFont font;
                if (font.Load(*platform->renderer)) {
                    platform->renderer->beginFrame();
                    platform->renderer->clear(0, 0, 0);
                    font.DrawText(*platform->renderer, 40, 200, "FATAL: board game failed to load.",
                                  255, 80, 80);
                    // generic_string(), not string() - the bitmap font has no '\' glyph. Wrapped
                    // since an asset root can be long enough to run off the 640px screen; the
                    // line below is pushed down far enough to clear 2 wrapped lines.
                    font.DrawTextWrapped(*platform->renderer, 40, 220,
                                         assets.asset_root().generic_string().c_str(),
                                         640 - 40 - 40, 200, 200, 200);
                    font.DrawText(*platform->renderer, 40, 270,
                                  "Board data missing or corrupt - cannot continue.", 255, 255,
                                  255);
                    platform->renderer->endFrame();
                    WaitForDismissOrTimeout(*platform);
                }
                platform->shutdown();
                return 1;
            }
        }
#ifndef NDEBUG
        {
            if (cheats.all_keys) {
                for (int i = 0; i < total_players && i < 6; ++i)
                    board_game.DebugSetPlayerKeys(i, 0x3F);
                LOG("--- CHEAT: --cheat-all-keys active ---\n");
            }
            if (cheats.gatekeeper_lose) {
                board_game.SetForceGatekeeperLose(true);
                LOG("--- CHEAT: --cheat-gatekeeper-lose active ---\n");
            }
        }
#endif
        if (!RunScreenLoop(board_game, *platform)) {
            board_game.unload();
            platform->shutdown();
            return 0;
        }
        LOG("--- EXIT BoardGameLoop ---\n");

        if (board_game.pause_quit_requested()) {
            LOG("--- Pause menu Exit: quitting without post-game sequence ---\n");
            board_game.unload();
            platform->shutdown();
            return 0;
        }

        play_again = RunPostGameSequence(board_game, platform);
        board_game.unload();
    }

    LOG("=== GAME FINISHED ===\n");
    platform->shutdown();
    return 0;
}

}  // namespace openfear
