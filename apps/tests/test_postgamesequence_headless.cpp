#include "test_framework.h"
#include "PostGameSequence.h"
#include "fakes/FakeRenderer.h"
#include "fakes/FakeAudio.h"
#include "fakes/FakeVideoPlayer.h"
#include "fakes/FakeInput.h"
#include "fakes/FakeAssetProvider.h"
#include "fakes/RegisterBoardLoadDefaults.h"
#include "openfear/game/BoardGameScreen.h"
#include "openfear/game/BoardInitializer.h"
#include "openfear/game/GameState.h"
#include <algorithm>
#include <array>
#include <memory>
#include <string>

using namespace openfear;
using namespace openfear::game;
using namespace openfear::testing;

namespace {

// Builds Platform directly, skipping init() - it static_casts to the real
// SDL3 types and would crash against fakes. poll() (called internally by
// RunPostGameSequence) stays safe: it no-ops without a cursor, which only
// init() would have constructed.
struct HeadlessPostGame {
    static Platform MakePlatform() {
        Platform p;
        p.renderer = std::make_unique<FakeRenderer>();
        p.audio = std::make_unique<FakeAudio>();
        p.input = std::make_unique<FakeInput>();
        p.video = std::make_unique<FakeVideoPlayer>();
        return p;
    }

    Platform platform = MakePlatform();
    FakeAssetProvider provider;
    AssetPaths paths{};
    assets::AssetManager assets{"test_assets", *platform.renderer, provider};
    BoardGameScreen board{assets, *platform.renderer, *platform.audio, *platform.video};

    explicit HeadlessPostGame(int player_count = 1) {
        auto* gs = GameStateManager::Instance().Get();
        gs->type_id = 0x618d;
        gs->total_players = static_cast<uint8_t>(player_count);
        for (int i = 0; i < 5; ++i) gs->ambient_trigger_armed[i] = (i == 0) ? 0 : 1;
        BoardInitializer::Init(60, 30);
        std::array<int, 6> fears = {0, -1, -1, -1, -1, -1};
        board.Configure(0, 25, fears.data(), player_count);
        RegisterBoardLoadDefaults(provider);
    }

    bool Load() { return board.load(paths); }

    FakeVideoPlayer& video() { return static_cast<FakeVideoPlayer&>(*platform.video); }
    FakeInput& input() { return static_cast<FakeInput&>(*platform.input); }
};

bool AnyContains(const std::vector<std::string>& haystack, const std::string& needle) {
    return std::any_of(haystack.begin(), haystack.end(),
                       [&](const std::string& s) { return s.find(needle) != std::string::npos; });
}

}  // namespace

void RegisterPostGameSequenceHeadlessTests(Tests& tests) {

    tests.AddCase("Headless PostGame: a fresh board (no winner, timer not expired) still "
                  "plays ENDVID (Ruta C) before the PLAYAGN prompt - it's just the logo "
                  "bumper, not a win-specific clip",
                  []() {
                      HeadlessPostGame h;
                      ASSERT_TRUE(h.Load());
                      h.input().ClickAt(300, 300);  // inside hit_yes (258,274)-(372,358)
                      const bool play_again = RunPostGameSequence(h.board, &h.platform);
                      ASSERT_TRUE(AnyContains(h.video().opened_, "ENDVID"));
                  });

    tests.AddCase("Headless PostGame: clicking Yes resets GameState for a new game and "
                  "reports play_again=true",
                  []() {
                      HeadlessPostGame h;
                      ASSERT_TRUE(h.Load());
                      h.input().ClickAt(300, 300);  // inside hit_yes (258,274)-(372,358)
                      const bool play_again = RunPostGameSequence(h.board, &h.platform);
                      ASSERT_TRUE(play_again);
                      auto* gs = GameStateManager::Instance().Get();
                      ASSERT_EQ(0u, gs->total_players);
                  });

    tests.AddCase("Headless PostGame: clicking No proceeds into the credits roll and returns "
                  "cleanly on a quit signal, instead of hanging on real slide timers",
                  []() {
                      HeadlessPostGame h;
                      ASSERT_TRUE(h.Load());
                      h.input().ClickAt(300, 200);  // inside hit_no (258,162)-(372,248)
                      h.input().AllowPolls(
                          1);  // let the No click resolve, then quit before credits waits
                      const bool play_again = RunPostGameSequence(h.board, &h.platform);
                      ASSERT_FALSE(play_again);
                  });

    tests.AddCase("Headless PostGame: an immediate quit signal (window close) during PLAYAGN "
                  "returns without crashing",
                  []() {
                      HeadlessPostGame h;
                      ASSERT_TRUE(h.Load());
                      h.input().SetQuit(true);
                      const bool play_again = RunPostGameSequence(h.board, &h.platform);
                      ASSERT_FALSE(play_again);
                  });
}
