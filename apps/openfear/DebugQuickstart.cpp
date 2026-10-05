#ifndef NDEBUG
#  include "DebugQuickstart.h"
#  include "PostGameSequence.h"
#  include "ScreenLoop.h"
#  include "openfear/game/PlayerInitializer.h"
#  include "openfear/game/BoardInitializer.h"
#  include "openfear/game/BoardGameScreen.h"
#  include "openfear/game/GameState.h"
#  include <algorithm>
#  include <array>
#  include <cstdlib>
#  include <random>
#  include <string_view>
#  include <vector>

namespace openfear::debugqs {

namespace {

int ParseInt(int argc, char* argv[], const char* flag, int fallback) {
    for (int i = 1; i < argc - 1; ++i)
        if (std::string_view(argv[i]) == flag) return std::atoi(argv[i + 1]);
    return fallback;
}

bool ParseFlag(int argc, char* argv[], const char* flag) {
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == flag) return true;
    return false;
}

// Scans argv for `flag`. If found, reads+clamps the next arg (or clamps
// `value_if_missing_arg` if there's no next arg). If `flag` isn't found at
// all, returns `value_if_flag_absent` as-is (NOT clamped — callers pick
// a value already in the range they want, e.g. 0 as a "not requested"
// sentinel, or a pre-clamped default).
int ParseClamped(int argc, char* argv[], const char* flag, int value_if_missing_arg,
                 int value_if_flag_absent, int lo, int hi) {
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == flag) {
            int n = value_if_missing_arg;
            if (i + 1 < argc) n = std::atoi(argv[i + 1]);
            return std::clamp(n, lo, hi);
        }
    }
    return value_if_flag_absent;
}

// Returns 0 if `flag` is absent (a sentinel meaning "not requested" -
// callers using this only ever have valid ranges that exclude 0).
int ParseClampedOr0(int argc, char* argv[], const char* flag, int fallback, int lo, int hi) {
    return ParseClamped(argc, argv, flag, fallback, 0, lo, hi);
}

// Returns clamp(default_value) if `flag` is absent - for flags that are
// always meaningful (no "not requested" sentinel).
int ParseClampedOrDefault(int argc, char* argv[], const char* flag, int default_value, int lo,
                          int hi) {
    return ParseClamped(argc, argv, flag, default_value, std::clamp(default_value, lo, hi), lo, hi);
}

// --test-duel-keys <hex,hex,...>: one hex byte per player (bit i = province
// i key held), applied via DebugSetPlayerKeys. Missing trailing players
// default to 0.
std::vector<uint8_t> ParseTestDuelKeys(int argc, char* argv[]) {
    std::vector<uint8_t> masks;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--test-duel-keys" && i + 1 < argc) {
            std::string_view csv(argv[i + 1]);
            size_t pos = 0;
            while (pos < csv.size()) {
                size_t comma = csv.find(',', pos);
                std::string_view tok = csv.substr(
                    pos, comma == std::string_view::npos ? std::string_view::npos : comma - pos);
                masks.push_back(
                    static_cast<uint8_t>(std::strtoul(std::string(tok).c_str(), nullptr, 16)));
                if (comma == std::string_view::npos) break;
                pos = comma + 1;
            }
            break;
        }
    }
    return masks;
}

// A shuffled {0..5} char_id assignment so --test-duel/--test-duel-ai don't
// always show the same characters - real, non-deterministic std::random,
// not the game's own RNG (CLI-only test setup, not gameplay).
std::array<int, 6> ShuffledCharIds() {
    std::array<int, 6> ids = {0, 1, 2, 3, 4, 5};
    std::random_device rd;
    std::mt19937 rng(rd());
    std::shuffle(ids.begin(), ids.end(), rng);
    return ids;
}

void ApplyDuelKeys(game::BoardGameScreen& board, const std::vector<uint8_t>& masks) {
    for (size_t i = 0; i < masks.size() && i < 6; ++i)
        board.DebugSetPlayerKeys(static_cast<int>(i), masks[i]);
}

void AssignBathoryTestRoster(int player_count) {
    for (int i = 0; i < player_count && i < 6; ++i) {
        game::PlayerInitializer::MarkPlayerAsHuman(static_cast<uint8_t>(i),
                                                   static_cast<uint16_t>(i));
    }
}

bool ApplyTestCurse(game::BoardGameScreen& board, int curse_mode, int victim, int player_count) {
    if (curse_mode == 0) return true;
    if (victim < 0 || victim >= player_count) {
        LOG("ERROR: --test-curse-victim %d is outside the %d-player quickstart\n", victim,
            player_count);
        return false;
    }
    if (curse_mode == 6 && player_count < 4) {
        LOG("ERROR: Bathory key-redirection tests require --test-players 4 or more\n");
        return false;
    }
    board.DebugSetPlayerCurse(victim, curse_mode);
    return true;
}

// Shared skeleton bracketing every quickstart block below: board-level
// init before BoardGameScreen construction, its load() after Configure().
// Each returns false (after already calling platform.shutdown()) on
// failure, so callers just need `if (!...) return 1;`.
bool InitQuickstartBoard(Platform& platform, int test_duration) {
    if (game::BoardInitializer::Init(test_duration, 30)) return true;
    std::fputs("Board initialization failed\n", stderr);
    platform.shutdown();
    return false;
}

bool LoadQuickstartBoard(game::BoardGameScreen& board, const AssetPaths& paths,
                         Platform& platform) {
    if (board.load(paths)) return true;
    std::fputs("Board game screen initialization failed\n", stderr);
    platform.shutdown();
    return false;
}

// Common tail of every quickstart block: unload() before shutdown()
// (board_qs can hold live SDL handles) then the done log.
void FinishQuickstart(game::BoardGameScreen& board, Platform& platform) {
    LOG("=== QUICKSTART done ===\n");
    board.unload();
    platform.shutdown();
}

}  // namespace

int ParseTestChar(int argc, char* argv[]) {
    return ParseInt(argc, argv, "--test-char", -1);
}

CheatFlags ParseCheatFlags(int argc, char* argv[]) {
    CheatFlags f;
    f.easy_win = ParseFlag(argc, argv, "--cheat-easy-win");
    f.all_keys = ParseFlag(argc, argv, "--cheat-all-keys");
    f.gatekeeper_lose = ParseFlag(argc, argv, "--cheat-gatekeeper-lose");
    return f;
}

std::optional<int> RunIfRequested(int argc, char* argv[], Platform& platform,
                                  assets::AssetManager& assets, const AssetPaths& paths) {
    const int test_encounter = ParseInt(argc, argv, "--test-encounter", 0);
    const int test_gate_arrival = ParseInt(argc, argv, "--test-gate-arrival", -1);
    const int test_tunnel_entry = ParseInt(argc, argv, "--test-tunnel-entry", -1);
    const int test_tunnel_exit = ParseInt(argc, argv, "--test-tunnel-exit", -1);
    const bool test_no_keys_left = ParseFlag(argc, argv, "--test-no-keys-left");
    const int test_power = ParseInt(argc, argv, "--test-power", -1);
    const int test_stone = ParseInt(argc, argv, "--test-stone", -1);
    const int test_stone_repeat = ParseInt(argc, argv, "--test-stone-repeat", 1);
    const bool test_dice_restricted = ParseFlag(argc, argv, "--test-dice-restricted");
    const bool test_board = ParseFlag(argc, argv, "--test-board");
    const bool test_toboggan = ParseFlag(argc, argv, "--test-toboggan");
    const bool test_gatekeeper = ParseFlag(argc, argv, "--test-gatekeeper");
    const bool test_gatekeeper_lose = ParseFlag(argc, argv, "--test-gatekeeper-lose");
    const bool test_flight = ParseFlag(argc, argv, "--test-flight");
    const bool test_chosen_one = ParseFlag(argc, argv, "--test-chosen-one");
    const int test_portrait = ParseInt(argc, argv, "--test-portrait", -1);
    const int test_duration_raw = ParseInt(argc, argv, "--test-duration", -1);
    const int test_duration = (test_duration_raw > 0) ? test_duration_raw : 90;
    const bool test_encounter_once = ParseFlag(argc, argv, "--test-encounter-once");
    const bool test_no_ai_duel = ParseFlag(argc, argv, "--test-no-ai-duel");
    // --test-players N (default 1, clamped 1-6): filler AI players for
    // quickstarts, so RNG branches that need "remaining players" reach
    // their real trigger condition instead of being forced.
    const int test_players = ParseClampedOrDefault(argc, argv, "--test-players", 1, 1, 6);
    const int test_duel = ParseClampedOr0(argc, argv, "--test-duel", 3, 2, 6);
    const int test_duel_ai = ParseClampedOr0(argc, argv, "--test-duel-ai", 3, 2, 6);
    const std::vector<uint8_t> test_duel_keys = ParseTestDuelKeys(argc, argv);
    const bool test_duel_counter = ParseFlag(argc, argv, "--test-duel-counter");
    const int test_curse_mode = ParseClampedOr0(argc, argv, "--test-curse-mode", 1, 1, 6);
    const int test_curse_victim = ParseInt(argc, argv, "--test-curse-victim", 0);

    // ---------------------------------------------------------------
    // --test-encounter N: 1-human-player board, enc_type N forced on
    // every landing.
    // ---------------------------------------------------------------
    if (test_encounter >= 1 && test_encounter <= 16) {
        LOG("=== QUICKSTART: --test-encounter %d (players=%d) ===\n", test_encounter, test_players);
        auto* gs_qs = game::GameStateManager::Instance().Get();
        if (gs_qs && gs_qs->IsValid()) gs_qs->total_players = test_players;

        game::PlayerInitializer::MarkPlayerAsHuman(0, 0);
        game::PlayerInitializer::MarkPlayerFearAndHuman(0, 0);
        if (test_curse_mode == 6) AssignBathoryTestRoster(test_players);

        if (!InitQuickstartBoard(platform, test_duration)) return 1;
        game::BoardGameScreen board_qs(assets, *platform.renderer, *platform.audio,
                                       *platform.video);
        std::array<int, 6> fears_qs = {0, -1, -1, -1, -1, -1};
        board_qs.Configure(0, 25, fears_qs.data(), 1);
        board_qs.SetForceEncounterType(test_encounter);
        if (test_encounter_once) board_qs.SetForceEncounterTypeOnce(true);
        if (test_no_ai_duel) board_qs.SetDisableAiDuel(true);

        if (!LoadQuickstartBoard(board_qs, paths, platform)) return 1;
        if (!ApplyTestCurse(board_qs, test_curse_mode, test_curse_victim, test_players)) {
            FinishQuickstart(board_qs, platform);
            return 1;
        }
        if (test_power >= 0) board_qs.DebugSetPlayerPower(0, static_cast<uint16_t>(test_power));
        ApplyDuelKeys(board_qs,
                      test_duel_keys);  // reused for enc types that gate on held keys (e.g. Bribe)
        RunScreenLoop(board_qs, platform);

        FinishQuickstart(board_qs, platform);
        return 0;
    }

    // ---------------------------------------------------------------
    // --test-duel N (default 3, all human): skips setup, lands on
    // ShowingPortrait with N hotseat players - click the duel corner to
    // walk DuelChooseOpponent -> DuelAttackGauge -> DuelStealOrCurse.
    // ---------------------------------------------------------------
    if (test_duel >= 2) {
        if (test_curse_mode == 6 && (test_duel < 4 || test_curse_victim == 3)) {
            LOG("ERROR: Bathory duel tests need at least 4 players and a non-Bathory victim\n");
            return 1;
        }
        LOG("=== QUICKSTART: --test-duel %d (all human) ===\n", test_duel);
        auto* gs_qs = game::GameStateManager::Instance().Get();
        if (gs_qs && gs_qs->IsValid()) gs_qs->total_players = test_duel;

        std::array<int, 6> fears_qs = {-1, -1, -1, -1, -1, -1};
        const std::array<int, 6> shuffled_chars =
            (test_curse_mode == 6) ? std::array<int, 6>{0, 1, 2, 3, 4, 5} : ShuffledCharIds();
        for (int i = 0; i < test_duel; ++i) {
            game::PlayerInitializer::MarkPlayerAsHuman(static_cast<uint8_t>(i),
                                                       static_cast<uint16_t>(shuffled_chars[i]));
            game::PlayerInitializer::MarkPlayerFearAndHuman(static_cast<uint8_t>(i), 0);
            fears_qs[i] = 0;
        }

        if (!InitQuickstartBoard(platform, test_duration)) return 1;
        game::BoardGameScreen board_qs(assets, *platform.renderer, *platform.audio,
                                       *platform.video);
        board_qs.Configure(0, 25, fears_qs.data(), test_duel);
        if (!LoadQuickstartBoard(board_qs, paths, platform)) return 1;
        if (!ApplyTestCurse(board_qs, test_curse_mode, test_curse_victim, test_duel)) {
            FinishQuickstart(board_qs, platform);
            return 1;
        }
        ApplyDuelKeys(board_qs, test_duel_keys);
        if (test_duel_counter) {
            // Every player already named every other player's char_id in a
            // prior FortuneCard - Protection fires on the first duel
            // regardless of who initiates.
            for (int a = 0; a < test_duel; ++a)
                for (int b = 0; b < test_duel; ++b)
                    if (a != b) board_qs.DebugSetFortuneCardTargeted(a, shuffled_chars[b]);
        }
        RunScreenLoop(board_qs, platform);

        FinishQuickstart(board_qs, platform);
        return 0;
    }

    // ---------------------------------------------------------------
    // --test-duel-ai N (default 3): player 0 human, rest AI filler - for
    // human-vs-AI and AI-initiated duels.
    // ---------------------------------------------------------------
    if (test_duel_ai >= 2) {
        LOG("=== QUICKSTART: --test-duel-ai %d (player 0 human, rest AI) ===\n", test_duel_ai);
        auto* gs_qs = game::GameStateManager::Instance().Get();
        if (gs_qs && gs_qs->IsValid()) gs_qs->total_players = test_duel_ai;

        const std::array<int, 6> shuffled_chars_ai = ShuffledCharIds();
        game::PlayerInitializer::MarkPlayerAsHuman(0, static_cast<uint16_t>(shuffled_chars_ai[0]));
        game::PlayerInitializer::MarkPlayerFearAndHuman(0, 0);
        for (int i = 1; i < test_duel_ai; ++i) {
            // Character only - no MarkPlayerFearAndHuman, so these stay AI
            // (default human_flag=0) with just a randomized character.
            game::PlayerInitializer::MarkPlayerAsHuman(static_cast<uint8_t>(i),
                                                       static_cast<uint16_t>(shuffled_chars_ai[i]));
        }

        if (!InitQuickstartBoard(platform, test_duration)) return 1;
        game::BoardGameScreen board_qs(assets, *platform.renderer, *platform.audio,
                                       *platform.video);
        std::array<int, 6> fears_qs = {0, -1, -1, -1, -1, -1};
        board_qs.Configure(0, 25, fears_qs.data(), 1);
        if (!LoadQuickstartBoard(board_qs, paths, platform)) return 1;
        ApplyDuelKeys(board_qs, test_duel_keys);
        if (test_duel_counter) {
            for (int a = 0; a < test_duel_ai; ++a)
                for (int b = 0; b < test_duel_ai; ++b)
                    if (a != b) board_qs.DebugSetFortuneCardTargeted(a, shuffled_chars_ai[b]);
        }
        RunScreenLoop(board_qs, platform);

        FinishQuickstart(board_qs, platform);
        return 0;
    }

    // ---------------------------------------------------------------
    // --test-stone N and every teleport-style quickstart (dice-restricted,
    // board, toboggan, gatekeeper[-lose], flight, chosen-one, no-keys-left,
    // gate-arrival, tunnel-entry/-exit, portrait): same 1-human-player
    // board setup as --test-encounter, but teleports/forces state instead
    // of forcing an encounter roll.
    // ---------------------------------------------------------------
    if ((test_stone >= 0 && test_stone <= 7) || test_dice_restricted || test_board ||
        test_toboggan || test_gatekeeper || test_gatekeeper_lose || test_flight ||
        test_chosen_one || test_no_keys_left ||
        (test_gate_arrival >= 0 && test_gate_arrival <= 23) ||
        (test_tunnel_entry >= 0 && test_tunnel_entry <= 5) ||
        (test_tunnel_exit >= 0 && test_tunnel_exit <= 5) ||
        (test_portrait >= 0 && test_portrait <= 5)) {
        LOG("=== QUICKSTART: --test-stone %d dice_restricted=%d toboggan=%d gatekeeper=%d "
            "gatekeeper_lose=%d flight=%d chosen_one=%d portrait=%d (players=%d) ===\n",
            test_stone, (int)test_dice_restricted, (int)test_toboggan, (int)test_gatekeeper,
            (int)test_gatekeeper_lose, (int)test_flight, (int)test_chosen_one, test_portrait,
            test_players);

        auto* gs_qs = game::GameStateManager::Instance().Get();
        if (gs_qs && gs_qs->IsValid()) gs_qs->total_players = test_players;

        // --test-toboggan uses char_id 5 (Hellin) for name-flavor only - the
        // real trigger has no char_id gate.
        int quickstart_char_id = 0;
        if (test_portrait >= 0 && test_portrait <= 5)
            quickstart_char_id = test_portrait;
        else if (test_toboggan)
            quickstart_char_id = 5;
        else if (test_gate_arrival >= 0)
            quickstart_char_id = 1;  // Khufu
        else if (test_tunnel_entry >= 0)
            quickstart_char_id = test_tunnel_entry;  // char_id==province
        else if (test_tunnel_exit >= 0)
            quickstart_char_id = test_tunnel_exit;
        game::PlayerInitializer::MarkPlayerAsHuman(0, quickstart_char_id);
        // fear_index==0 means "unset/immune" - always survives the
        // gatekeeper roll, which would make --test-gatekeeper-lose's
        // eliminated branch unreachable.
        game::PlayerInitializer::MarkPlayerFearAndHuman(0, test_gatekeeper_lose ? 1 : 0);
        if (test_curse_mode == 6 && test_stone == 4 && quickstart_char_id == 0)
            AssignBathoryTestRoster(test_players);

        if (!InitQuickstartBoard(platform, test_duration)) return 1;
        game::BoardGameScreen board_qs(assets, *platform.renderer, *platform.audio,
                                       *platform.video);
        std::array<int, 6> fears_qs = {test_gatekeeper_lose ? 1 : 0, -1, -1, -1, -1, -1};
        board_qs.Configure(0, 25, fears_qs.data(), 1);
        if (!LoadQuickstartBoard(board_qs, paths, platform)) return 1;
        if (!ApplyTestCurse(board_qs, test_curse_mode, test_curse_victim, test_players)) {
            FinishQuickstart(board_qs, platform);
            return 1;
        }
        // Applied before any forced-arrival call: DebugForceStoneArrival
        // resolves the stone's effect synchronously inside itself, so
        // pre-arrival key state must already be set.
        ApplyDuelKeys(board_qs, test_duel_keys);
        if (test_no_keys_left) board_qs.DebugSetProvinceKeysRemaining(0, 0);
        if (test_stone >= 0 && test_stone <= 7) {
            board_qs.DebugForceStoneArrival(test_stone, test_stone_repeat);
            // The Flight power only fires mid-roll on a type-6 stone with
            // the right province's key held - grant all 6 so it's always
            // active regardless of which province this quickstart landed on.
            if (test_stone == 6) board_qs.DebugSetPlayerKeys(0, 0x3F);
        }
        if (test_dice_restricted) board_qs.DebugSetDiceRestricted(0, true);
        if (test_toboggan) board_qs.DebugSetupTobogganTest();
        if (test_gate_arrival >= 0 && test_gate_arrival <= 23)
            board_qs.DebugSetupGateArrivalTest(test_gate_arrival);
        if (test_tunnel_entry >= 0 && test_tunnel_entry <= 5)
            board_qs.DebugSetupTunnelEntryTest(test_tunnel_entry);
        if (test_tunnel_exit >= 0 && test_tunnel_exit <= 5)
            board_qs.DebugSetupTunnelExitTest(test_tunnel_exit);
        if (test_gatekeeper || test_gatekeeper_lose) {
            board_qs.DebugForceGatekeeperChallenge();
            if (test_gatekeeper_lose) board_qs.SetForceGatekeeperLose(true);
        }
        if (test_flight) board_qs.DebugSetupFlightTest();
        if (test_chosen_one) board_qs.DebugForceDoubleSixRoll();
        if (test_portrait >= 0 && test_portrait <= 5) {
            board_qs.DebugSetPlayerKeys(0, 0x3F);
            board_qs.DebugShowPortrait();
        }
        // Applied last so it can override any auto-grant above (e.g.
        // --test-stone 6 / --test-flight both auto-grant 0x3F) - needed for
        // the Flight-key negative cases.
        ApplyDuelKeys(board_qs, test_duel_keys);
        const bool quit_during_play = !RunScreenLoop(board_qs, platform);

        // The win check (EndTurn()'s "WIN FLAG CHECK") already rolls the
        // Gatekeeper + plays FEAR{roll}.AVI + the result taunt inside
        // RunScreenLoop above - this only covers what's left: ENDVID,
        // PLAYAGN result audio, credits. Skipped on quit - without this
        // check, a quit during RunScreenLoop was silently ignored and
        // RunPostGameSequence ran anyway, needing a second close click to
        // actually exit.
        if (!quit_during_play && (test_gatekeeper || test_gatekeeper_lose)) {
            RunPostGameSequence(board_qs, &platform);
        }

        FinishQuickstart(board_qs, platform);
        return 0;
    }

    // ---------------------------------------------------------------
    // --test-curse-mode N [--test-curse-victim P]: same 1-human-player
    // board setup, but starts player P (default 0) already cursed - just
    // play turns, CureCurseGame's real trigger fires on its own.
    // ---------------------------------------------------------------
    if (test_curse_mode >= 1 && test_curse_mode <= 6) {
        LOG("=== QUICKSTART: --test-curse-mode %d victim=player%d (players=%d) ===\n",
            test_curse_mode, test_curse_victim, test_players);
        auto* gs_qs = game::GameStateManager::Instance().Get();
        if (gs_qs && gs_qs->IsValid()) gs_qs->total_players = test_players;

        // Mode N is cast BY this char_id on a duel win, so it can never be
        // the CURSED victim in real play (self-duel is impossible). Only
        // mode 1's caster (char_id 0) collides with player 0's usual
        // char_id 0 default; bump to char_id 1 in that one case.
        static const int kCurseCasterChar[7] = {-1, 0, 4, 1, 2, 5, 3};  // index by mode 1-6
        int human_char_id = 0;
        if (test_curse_victim == 0 && kCurseCasterChar[test_curse_mode] == 0) human_char_id = 1;
        game::PlayerInitializer::MarkPlayerAsHuman(0, human_char_id);
        game::PlayerInitializer::MarkPlayerFearAndHuman(0, 0);
        if (test_curse_mode == 6) AssignBathoryTestRoster(test_players);

        if (!InitQuickstartBoard(platform, test_duration)) return 1;
        game::BoardGameScreen board_qs(assets, *platform.renderer, *platform.audio,
                                       *platform.video);
        std::array<int, 6> fears_qs = {0, -1, -1, -1, -1, -1};
        board_qs.Configure(0, 25, fears_qs.data(), 1);
        if (!LoadQuickstartBoard(board_qs, paths, platform)) return 1;
        board_qs.DebugSetPlayerCurse(test_curse_victim, test_curse_mode);
        RunScreenLoop(board_qs, platform);

        FinishQuickstart(board_qs, platform);
        return 0;
    }

    return std::nullopt;
}

}  // namespace openfear::debugqs
#endif  // NDEBUG
