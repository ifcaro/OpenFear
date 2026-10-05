#include "test_framework.h"
#include "BoardGameScreenTestAccess.h"
#include "fakes/FakeRenderer.h"
#include "fakes/FakeAudio.h"
#include "fakes/FakeVideoPlayer.h"
#include "fakes/FakeInput.h"
#include "fakes/FakeAssetProvider.h"
#include "fakes/RegisterBoardLoadDefaults.h"
#include "fakes/SyntheticMapFile.h"
#include "fakes/SyntheticMapMove.h"
#include "fakes/SyntheticRectFile.h"
#include "openfear/game/BoardInitializer.h"
#include "openfear/game/GameState.h"
#include "openfear/game/PlayerInitializer.h"
#include "openfear/game/PlayerObject.h"
#include <array>
#include <algorithm>

using namespace openfear;
using namespace openfear::game;
using namespace openfear::testing;

namespace {

// Shared setup for every headless BoardGameScreen test: real GameState +
// BoardInitializer (both pure in-memory, no file I/O), fake platform, real
// BoardGameScreen wired to the fakes instead of SDL3/real assets.
struct HeadlessBoard {
    FakeRenderer renderer;
    FakeAudio audio;
    FakeVideoPlayer video;
    FakeAssetProvider provider;
    AssetPaths paths{};
    assets::AssetManager assets{"test_assets", renderer, provider};
    BoardGameScreen board{assets, renderer, audio, video};

    explicit HeadlessBoard(int player_count = 1) {
        auto* gs = GameStateManager::Instance().Get();
        gs->type_id = 0x618d;
        gs->total_players = static_cast<uint8_t>(player_count);
        for (int i = 0; i < 5; ++i) gs->ambient_trigger_armed[i] = (i == 0) ? 0 : 1;
        BoardInitializer::Init(60, 30);
        std::array<int, 6> fears = {0, -1, -1, -1, -1, -1};
        board.Configure(0, 25, fears.data(), player_count);
        // Minimal valid defaults for everything load() hard-requires; tests
        // that care override a specific one with their own SetFile() before
        // calling Load().
        RegisterBoardLoadDefaults(provider);
    }

    bool Load() { return board.load(paths); }
};

bool AnyContains(const std::vector<std::string>& haystack, const std::string& needle) {
    return std::any_of(haystack.begin(), haystack.end(),
                       [&](const std::string& s) { return s.find(needle) != std::string::npos; });
}

// One plain (type 0) stone in province 0, force enc_type, and run the real
// arrival dispatch — shared by the per-enc_type tests below.
void force_plain_landing(HeadlessBoard& h, int enc_type) {
    SyntheticStone plain;
    plain.type = 0;
    const std::string mapfile_path = "MAPFILE";
    h.provider.SetFile(mapfile_path, BuildSyntheticMapFile(0, {plain}));
    h.Load();
    h.board.SetForceEncounterType(enc_type);
    h.board.DebugForceStoneArrival(0);
}

// Real (non-degenerate) MAPMOVE.BIN geometry, 3 non-overlapping rectangles,
// on-disk order LEFT/RIGHT/CENTER — for tests that need an actual
// point-in-polygon hit rather than RegisterBoardLoadDefaults' 1-point
// placeholder zones.
std::vector<uint8_t> BuildMapMoveFileWithRealZones() {
    std::vector<uint8_t> out;
    AppendMapMovePolygon(out, {{0, 0}, {100, 0}, {100, 100}, {0, 100}});      // LEFT
    AppendMapMovePolygon(out, {{200, 0}, {300, 0}, {300, 100}, {200, 100}});  // RIGHT
    AppendMapMovePolygon(out, {{120, 0}, {180, 0}, {180, 100}, {120, 100}});  // CENTER
    return out;
}

}  // namespace

void RegisterBoardGameScreenHeadlessTests(Tests& tests) {

    tests.AddCase("Headless: BoardGameScreen::load() succeeds against an all-fake platform", []() {
        HeadlessBoard h;
        ASSERT_TRUE(h.Load());
    });

    tests.AddCase("Headless: forced enc_type 7 (Doors) requests one of the 3 real door bitmaps",
                  []() {
                      HeadlessBoard h;
                      ASSERT_TRUE(h.Load());
                      h.board.SetForceEncounterType(7);
                      BoardGameScreenTestAccess::DispatchEncounter(h.board, 0);
                      h.board.render();
                      const bool requested_a_door_bmp =
                          AnyContains(h.renderer.loaded_bitmaps_, "DOORS3.BMP") ||
                          AnyContains(h.renderer.loaded_bitmaps_, "DOORS6.BMP") ||
                          AnyContains(h.renderer.loaded_bitmaps_, "DOORS10.BMP");
                      ASSERT_TRUE(requested_a_door_bmp);
                  });

    tests.AddCase("Headless: forced enc_type 7 (Doors) plays an entry WAV on its first update",
                  []() {
                      HeadlessBoard h;
                      ASSERT_TRUE(h.Load());
                      h.board.SetForceEncounterType(7);
                      BoardGameScreenTestAccess::DispatchEncounter(h.board, 0);
                      FakeInput input;
                      h.board.update(input);
                      // 6-clip pool, all under W2\ or W6\ — don't assert the exact clip
                      // (RNG-picked, not seeded from tests), just that something played.
                      ASSERT_FALSE(h.audio.played_.empty());
                  });

    tests.AddCase("Headless: MinimapScreen never calls stop() with handle 0 when its open-WAV "
                  "fails to load (regression: stop(0) halts every audio channel, not a no-op)",
                  []() {
                      HeadlessBoard h;
                      ASSERT_TRUE(h.Load());
                      h.audio.FailAllWaves();
                      auto& minimap = BoardGameScreenTestAccess::Minimap(h.board);
                      minimap.Enter(BoardGameState::Moving);
                      // A click on any position closes the minimap unconditionally (no
                      // rect/zone dependency) — real MinimapScreen::update() behavior.
                      FakeInput input;
                      input.ClickAt(100, 100);
                      minimap.update(input);
                      const bool called_global_halt =
                          std::find(h.audio.stopped_.begin(), h.audio.stopped_.end(),
                                    SoundHandle{0}) != h.audio.stopped_.end();
                      ASSERT_FALSE(called_global_halt);
                  });

    tests.AddCase("Headless: with a synthetic MAPFILE, a real stone-type-0 landing reaches "
                  "TriggerEncounter -> DispatchEncounter, not a direct test-only call",
                  []() {
                      HeadlessBoard h;
                      // One plain (type 0) stone in province 0 — stone.type==0 falls through
                      // ResolveStepArrival's key/bhole/FateCard/FortuneCard special cases
                      // straight into TriggerEncounter(), the real "roll an encounter" path.
                      SyntheticStone plain;
                      plain.type = 0;
                      const std::string mapfile_path = "MAPFILE";
                      h.provider.SetFile(mapfile_path, BuildSyntheticMapFile(0, {plain}));

                      ASSERT_TRUE(h.Load());
                      h.board.SetForceEncounterType(
                          7);  // Doors — deterministic instead of the real weighted roll
                      h.board.DebugForceStoneArrival(
                          0);  // finds the type-0 stone, runs the real arrival dispatch
                      h.board.render();

                      const bool requested_a_door_bmp =
                          AnyContains(h.renderer.loaded_bitmaps_, "DOORS3.BMP") ||
                          AnyContains(h.renderer.loaded_bitmaps_, "DOORS6.BMP") ||
                          AnyContains(h.renderer.loaded_bitmaps_, "DOORS10.BMP");
                      ASSERT_TRUE(requested_a_door_bmp);
                  });

    // Rules paraphrased from the user manual (not quoted — the manual itself
    // is copyrighted and never copied into this repo).

    tests.AddCase("Headless: a key stone you already hold that key for does not re-grant it, "
                  "falls through to a normal encounter roll instead",
                  []() {
                      // Manual: each color key can only be held once.
                      HeadlessBoard h;
                      SyntheticStone key_stone;
                      key_stone.type = 1;
                      const std::string mapfile_path = "MAPFILE";
                      h.provider.SetFile(mapfile_path, BuildSyntheticMapFile(0, {key_stone}));

                      ASSERT_TRUE(h.Load());
                      h.board.DebugSetPlayerKeys(0, 0x01);  // already holds province 0's key
                      h.board.SetForceEncounterType(
                          7);  // Doors — proves it reached TriggerEncounter
                      h.board.DebugForceStoneArrival(1);

                      ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                                  BoardGameState::DoorsGame);
                      ASSERT_FALSE(AnyContains(h.video.opened_, "KEY0"));
                  });

    tests.AddCase("Headless: landing on a key stone with the province's key pool exhausted "
                  "opens the NOKEY variant and does not grant a key",
                  []() {
                      // Manual: a key is only awarded if not every player has already claimed
                      // every copy of it (a per-province shared pool, not infinite supply).
                      HeadlessBoard h;
                      SyntheticStone key_stone;
                      key_stone.type = 1;
                      const std::string mapfile_path = "MAPFILE";
                      h.provider.SetFile(mapfile_path, BuildSyntheticMapFile(0, {key_stone}));

                      ASSERT_TRUE(h.Load());
                      h.board.DebugSetProvinceKeysRemaining(0, 0);
                      h.board.DebugForceStoneArrival(1);

                      ASSERT_TRUE(AnyContains(h.video.opened_, "NOKEY0"));
                      ASSERT_FALSE(BoardGameScreenTestAccess::RenderState(h.board)[0].keys[0]);
                  });

    tests.AddCase(
        "Headless: full key set (black-hole-immunity power included) skips the black "
        "hole entirely and falls through to a normal encounter roll",
        []() {
            // Manual: one specific key power prevents ever falling into a black hole.
            HeadlessBoard h;
            SyntheticStone bhole_stone;
            bhole_stone.type = 2;
            const std::string mapfile_path = "MAPFILE";
            h.provider.SetFile(mapfile_path, BuildSyntheticMapFile(0, {bhole_stone}));

            ASSERT_TRUE(h.Load());
            h.board.DebugSetPlayerKeys(0, 0x3F);  // all 6 keys -> every key power active
            h.board.SetForceEncounterType(7);     // Doors — proves it reached TriggerEncounter
            h.board.DebugForceStoneArrival(2);

            ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                        BoardGameState::DoorsGame);
            ASSERT_EQ(0,
                      BoardGameScreenTestAccess::RenderState(h.board)[0].black_hole_retry_counter);
            ASSERT_FALSE(AnyContains(h.video.opened_, "BHOLE0"));
        });

    tests.AddCase(
        "Headless: without black-hole immunity, landing on a black hole opens its AVI "
        "and arms the retry flag",
        []() {
            HeadlessBoard h;
            SyntheticStone bhole_stone;
            bhole_stone.type = 2;
            const std::string mapfile_path = "MAPFILE";
            h.provider.SetFile(mapfile_path, BuildSyntheticMapFile(0, {bhole_stone}));

            ASSERT_TRUE(h.Load());
            h.board.DebugSetPlayerKeys(0, 0x00);  // no keys -> no immunity
            h.board.DebugForceStoneArrival(2);

            ASSERT_TRUE(AnyContains(h.video.opened_, "BHOLE0"));
            ASSERT_EQ(1,
                      BoardGameScreenTestAccess::RenderState(h.board)[0].black_hole_retry_counter);
        });

    // ---------------------------------------------------------------------
    // One test per enc_type (1-16), via a plain (type 0) stone + forced
    // enc_type + a real DebugForceStoneArrival landing — exercises the same
    // ResolveStepArrival -> TriggerEncounter -> DispatchEncounter chain as
    // real gameplay for every type, not just Doors.
    // ---------------------------------------------------------------------

    tests.AddCase("Headless: enc_type 1 (insult-only) plays audio and reaches BlackFlash, no "
                  "render_state side effect",
                  []() {
                      HeadlessBoard h;
                      force_plain_landing(h, 1);
                      ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                                  BoardGameState::BlackFlash);
                      ASSERT_FALSE(h.audio.played_.empty());
                  });

    tests.AddCase("Headless: enc_type 2 (OneDie skip-turn) sets skip_turn_counter", []() {
        HeadlessBoard h;
        force_plain_landing(h, 2);
        ASSERT_EQ(1, BoardGameScreenTestAccess::RenderState(h.board)[0].skip_turn_counter);
    });

    tests.AddCase(
        "Headless: enc_type 3 (GateOpen) arms the black-hole retry flag "
        "unconditionally, even with no reachable black-hole stone",
        []() {
            HeadlessBoard h;
            force_plain_landing(h, 3);
            ASSERT_EQ(1,
                      BoardGameScreenTestAccess::RenderState(h.board)[0].black_hole_retry_counter);
        });

    tests.AddCase(
        "Headless: enc_type 4 (1-die restriction) sets dice_restricted and "
        "dice_restriction_turns",
        []() {
            HeadlessBoard h;
            force_plain_landing(h, 4);
            ASSERT_EQ(1, BoardGameScreenTestAccess::RenderState(h.board)[0].dice_restricted);
            ASSERT_EQ(1, BoardGameScreenTestAccess::RenderState(h.board)[0].dice_restriction_turns);
        });

    tests.AddCase("Headless: Roll a Six (enc type 5) enables the even-die restriction", []() {
        HeadlessBoard h;
        force_plain_landing(h, 5);
        ASSERT_EQ(1, BoardGameScreenTestAccess::RenderState(h.board)[0].even_dice);
    });

    tests.AddCase("Headless: enc_type 6 (extra turn) completes the full cascade without "
                  "crashing",
                  []() {
                      HeadlessBoard h(2);
                      force_plain_landing(h, 6);
                      ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                                  BoardGameState::BlackFlash);
                  });

    tests.AddCase("Headless: enc_type 8 (DiceKeys) transitions straight to DiceKeysGame", []() {
        HeadlessBoard h;
        force_plain_landing(h, 8);
        ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                    BoardGameState::DiceKeysGame);
    });

    tests.AddCase("Headless: enc_type 9 (OneDie minigame) is skipped with only 1 active player "
                  "(manual: multiplayer-only mechanic) and ends the turn instead",
                  []() {
                      HeadlessBoard h(1);
                      force_plain_landing(h, 9);
                      ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                                  BoardGameState::TurnEnd);
                  });

    tests.AddCase("Headless: enc_type 9 (OneDie minigame) fires with 2+ active players", []() {
        HeadlessBoard h(2);
        force_plain_landing(h, 9);
        ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) == BoardGameState::OneDieGame);
    });

    tests.AddCase("Headless: enc_type 10 (FortuneDoor) transitions straight to FortuneDoorGame",
                  []() {
                      HeadlessBoard h;
                      force_plain_landing(h, 10);
                      ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                                  BoardGameState::FortuneDoorGame);
                  });

    tests.AddCase("Headless: enc_type 11 (CoffinChoice) transitions straight to "
                  "CoffinChoiceGame",
                  []() {
                      HeadlessBoard h;
                      force_plain_landing(h, 11);
                      ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                                  BoardGameState::CoffinChoiceGame);
                  });

    tests.AddCase("Headless: enc_type 12 (Bribe) is skipped with no held key (needs a key to "
                  "offer collateral) and ends the turn instead",
                  []() {
                      HeadlessBoard h;
                      force_plain_landing(h, 12);  // no DebugSetPlayerKeys call -> holds no key
                      ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                                  BoardGameState::TurnEnd);
                  });

    tests.AddCase("Headless: enc_type 12 (Bribe) fires once the player holds at least one key",
                  []() {
                      HeadlessBoard h;
                      SyntheticStone plain;
                      plain.type = 0;
                      const std::string mapfile_path = "MAPFILE";
                      h.provider.SetFile(mapfile_path, BuildSyntheticMapFile(0, {plain}));
                      ASSERT_TRUE(h.Load());
                      h.board.DebugSetPlayerKeys(0, 0x01);
                      h.board.SetForceEncounterType(12);
                      h.board.DebugForceStoneArrival(0);
                      ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                                  BoardGameState::BribeGame);
                  });

    tests.AddCase("Headless: enc_type 13 (Blackjack) transitions straight to BlackjackGame", []() {
        HeadlessBoard h;
        force_plain_landing(h, 13);
        ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                    BoardGameState::BlackjackGame);
    });

    tests.AddCase("Headless: Blackjack stake table with every category empty (1 player, power<=9, "
                  "0 keys held) falls back to kStakeTurn instead of reading past the entries array",
                  []() {
                      HeadlessBoard h(1);  // 1 total player -> no turn-pass category either
                      SyntheticStone plain;
                      plain.type = 0;
                      h.provider.SetFile("MAPFILE", BuildSyntheticMapFile(0, {plain}));
                      h.Load();
                      h.board.DebugSetPlayerPower(0, 0);  // load() resets it back to 25
                      h.board.SetForceEncounterType(13);
                      h.board.DebugForceStoneArrival(0);
                      ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                                  BoardGameState::BlackjackGame);
                      auto& bj = BoardGameScreenTestAccess::Blackjack(h.board);
                      ASSERT_EQ(BoardGameScreenTestAccess::kBlackjackStakeTurn,
                                BoardGameScreenTestAccess::BlackjackStakeId(bj));
                  });

    tests.AddCase("Headless: enc_type 14 (SlotMachine) transitions straight to "
                  "SlotMachineGame",
                  []() {
                      HeadlessBoard h;
                      force_plain_landing(h, 14);
                      ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                                  BoardGameState::SlotMachineGame);
                  });

    tests.AddCase("Headless: enc_type 15 (KeyAward) is skipped once a player already holds "
                  "all 6 keys and ends the turn instead",
                  []() {
                      HeadlessBoard h;
                      SyntheticStone plain;
                      plain.type = 0;
                      const std::string mapfile_path = "MAPFILE";
                      h.provider.SetFile(mapfile_path, BuildSyntheticMapFile(0, {plain}));
                      ASSERT_TRUE(h.Load());
                      h.board.DebugSetPlayerKeys(0, 0x3F);  // all 6 keys already held
                      h.board.SetForceEncounterType(15);
                      h.board.DebugForceStoneArrival(0);
                      ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                                  BoardGameState::TurnEnd);
                  });

    tests.AddCase("Headless: enc_type 15 (KeyAward) fires while keys remain unheld, via the "
                  "GKLOOP intro state",
                  []() {
                      HeadlessBoard h;
                      force_plain_landing(h, 15);
                      ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                                  BoardGameState::GkLoopAVI);
                  });

    tests.AddCase("Headless: enc_type 16 (ambient fallback EYELOOP) plays the fixed insult "
                  "clip and reaches BlackFlash",
                  []() {
                      HeadlessBoard h;
                      force_plain_landing(h, 16);
                      ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                                  BoardGameState::BlackFlash);
                      ASSERT_TRUE(AnyContains(h.audio.played_, "AT_0454.WAV"));
                  });

    tests.AddCase(
        "Headless: Bathory key redirect audio starts after the AVI and holds its final frame",
        []() {
            HeadlessBoard h(4);
            for (uint8_t i = 0; i < 4; ++i) {
                auto* player = PlayerInitializer::GetPlayer(i);
                player->character_id = i;
                player->human_flag = (i == 0) ? 1 : 0;
            }
            std::array<int, 6> fears = {0, -1, -1, -1, -1, -1};
            h.board.Configure(0, 25, fears.data(), 1);
            ASSERT_TRUE(h.Load());
            h.board.DebugSetPlayerCurse(0, 6);

            BoardGameScreenTestAccess::ResolveKeyStoneArrival(h.board, 4);
            ASSERT_TRUE(BoardGameScreenTestAccess::BathoryKeyRedirectWav(h.board) == 0);
            ASSERT_TRUE(!AnyContains(h.audio.played_, "AT_0601.WAV"));
            auto* players = BoardGameScreenTestAccess::RenderState(h.board);
            ASSERT_TRUE(players[0].keys[4] == 0);
            ASSERT_TRUE(players[3].keys[4] != 0);

            BoardGameScreenTestAccess::ResetTurnStartTime(h.board);
            FakeInput input;
            h.video.SetPlaying();
            h.board.update(input);
            ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                        BoardGameState::StoneArrivalAVI);
            ASSERT_TRUE(BoardGameScreenTestAccess::BathoryKeyRedirectWav(h.board) == 0);
            ASSERT_TRUE(!AnyContains(h.audio.played_, "AT_0601.WAV"));

            h.video.FinishPlaying();
            h.board.update(input);
            ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                        BoardGameState::StoneArrivalAVI);
            const SoundHandle redirect_wav =
                BoardGameScreenTestAccess::BathoryKeyRedirectWav(h.board);
            ASSERT_TRUE(redirect_wav != 0);
            ASSERT_TRUE(AnyContains(h.audio.played_, "AT_0601.WAV"));
            h.audio.SetPlaying(redirect_wav);
            const int render_calls_before = h.video.render_calls_;
            h.board.render();
            ASSERT_TRUE(h.video.render_calls_ > render_calls_before);

            h.audio.FinishPlaying(redirect_wav);
            h.board.update(input);
            ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                        BoardGameState::TurnEnd);
            h.board.update(input);
            ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                        BoardGameState::BlackFlash);
            ASSERT_TRUE(BoardGameScreenTestAccess::BathoryKeyRedirectWav(h.board) == 0);
        });

    tests.AddCase(
        "Headless: BathoryKeyRedirectAudioBusy starts the chime once pending, holds while "
        "it plays, clears once done",
        []() {
            HeadlessBoard h(4);
            for (uint8_t i = 0; i < 4; ++i) {
                auto* player = PlayerInitializer::GetPlayer(i);
                player->character_id = i;
                player->human_flag = (i == 0) ? 1 : 0;
            }
            ASSERT_TRUE(h.Load());
            h.board.DebugSetPlayerCurse(0, 6);

            // Nothing pending yet: not busy.
            ASSERT_TRUE(!BoardGameScreenTestAccess::BathoryBusy(h.board));

            BoardGameScreenTestAccess::GainKey(h.board, 0, 2);  // redirects to Bathory (player 3)
            auto* players = BoardGameScreenTestAccess::RenderState(h.board);
            ASSERT_TRUE(players[0].keys[2] == 0);
            ASSERT_TRUE(players[3].keys[2] != 0);

            // First call after a redirect: starts the chime, reports busy.
            ASSERT_TRUE(BoardGameScreenTestAccess::BathoryBusy(h.board));
            const SoundHandle redirect_wav =
                BoardGameScreenTestAccess::BathoryKeyRedirectWav(h.board);
            ASSERT_TRUE(redirect_wav != 0);
            ASSERT_TRUE(AnyContains(h.audio.played_, "AT_0601.WAV"));

            // Still playing: stays busy, no second chime started.
            h.audio.SetPlaying(redirect_wav);
            ASSERT_TRUE(BoardGameScreenTestAccess::BathoryBusy(h.board));
            ASSERT_TRUE(BoardGameScreenTestAccess::BathoryKeyRedirectWav(h.board) == redirect_wav);

            // Done: no longer busy.
            h.audio.FinishPlaying(redirect_wav);
            ASSERT_TRUE(!BoardGameScreenTestAccess::BathoryBusy(h.board));
        });

    tests.AddCase(
        "Headless: DuelStealOrCurse holds on screen through the Bathory chime instead of "
        "cutting to TurnEnd/BlackFlash while it's still playing",
        []() {
            HeadlessBoard h(4);
            for (uint8_t i = 0; i < 4; ++i) {
                auto* player = PlayerInitializer::GetPlayer(i);
                player->character_id = i;
                player->human_flag = (i == 0) ? 1 : 0;
            }
            ASSERT_TRUE(h.Load());
            h.board.DebugSetPlayerCurse(0, 6);    // duel winner (player 0) is cursed
            h.board.DebugSetPlayerKeys(1, 0x04);  // loser (player 1) holds province 2's key

            auto& duel = BoardGameScreenTestAccess::Duel(h.board);
            duel.winner = 0;
            duel.loser = 1;
            BoardGameScreenTestAccess::RenderState(h.board)[0].is_ai = false;
            BoardGameScreenTestAccess::CurrentState(h.board) = BoardGameState::DuelStealOrCurse;
            BoardGameScreenTestAccess::SetStateActionExecuted(h.board, false);
            BoardGameScreenTestAccess::ResetTurnStartTime(h.board);

            FakeInput input;
            h.board.update(input);  // EnterStealOrCurse + arms the 2000ms hold timer
            ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                        BoardGameState::DuelStealOrCurse);

            // Resolve the winner's choice directly (steal province 2) instead of
            // simulating the real click/zone-hit-test — that's not what this test
            // is about. Triggers GainKey(0, 2), which redirects to Bathory.
            BoardGameScreenTestAccess::ResolveDuelZone(
                BoardGameScreenTestAccess::DuelStealOrCurseScreen(h.board), 2);
            auto* players = BoardGameScreenTestAccess::RenderState(h.board);
            ASSERT_TRUE(players[0].keys[2] == 0);
            ASSERT_TRUE(players[3].keys[2] != 0);  // Bathory got it instead

            // Clear the real 2000ms post-choice hold-pause without a wall-clock wait.
            BoardGameScreenTestAccess::AgeStateStartTime(h.board, 2100);

            // The hold-pause has elapsed, but the chime hasn't played yet — must
            // start it and keep waiting, NOT cut to TurnEnd/BlackFlash early (the
            // exact bug this test guards against).
            h.board.update(input);
            ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                        BoardGameState::DuelStealOrCurse);
            const SoundHandle redirect_wav =
                BoardGameScreenTestAccess::BathoryKeyRedirectWav(h.board);
            ASSERT_TRUE(redirect_wav != 0);
            ASSERT_TRUE(AnyContains(h.audio.played_, "AT_0601.WAV"));

            // Still playing: still holds.
            h.audio.SetPlaying(redirect_wav);
            h.board.update(input);
            ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                        BoardGameState::DuelStealOrCurse);

            // Chime done: now it's allowed to leave.
            h.audio.FinishPlaying(redirect_wav);
            h.board.update(input);
            ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                        BoardGameState::TurnEnd);
        });

    // ---------------------------------------------------------------------
    // Duel trio: DuelAttackGauge / DuelChooseOpponent / DuelStealOrCurse. The
    // real trigger is a click on the invisible portrait-corner zone
    // (533,365)-(639,479) during ShowingPortrait, own turn only, 2+ active
    // players, no skip-turn/black-hole-retry/curse block active — no
    // MAPFILE/movement involved at all.
    // ---------------------------------------------------------------------

    tests.AddCase("Headless: portrait-corner click with an eligible opponent opens "
                  "DuelChooseOpponent",
                  []() {
                      HeadlessBoard h(2);
                      ASSERT_TRUE(h.Load());
                      BoardGameScreenTestAccess::RenderState(h.board)[0].is_ai =
                          false;  // must be human to see the duel trigger
                      FakeInput input;
                      input.ClickAt(600, 400);  // inside the real (533,365)-(639,479) corner zone
                      h.board.update(input);
                      ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                                  BoardGameState::DuelChooseOpponent);
                  });

    tests.AddCase("Headless: portrait-corner click is a no-op solo (needs 2+ active players)",
                  []() {
                      HeadlessBoard h(1);
                      ASSERT_TRUE(h.Load());
                      BoardGameScreenTestAccess::RenderState(h.board)[0].is_ai = false;
                      FakeInput input;
                      input.ClickAt(600, 400);
                      h.board.update(input);
                      ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                                  BoardGameState::ShowingPortrait);
                  });

    tests.AddCase(
        "Headless: picking a valid opponent in DuelChooseOpponent starts "
        "DuelAttackGauge",
        []() {
            HeadlessBoard h(2);
            assets::RectEntry zones[6] = {};
            zones[1] = {100, 100, 150, 150};  // char_id 1 == player 1's icon cell
            const auto choose_path = AssetPaths::data("DATA", "choose", "BIN").string();
            h.provider.SetFile(choose_path, BuildRectFileBytes({zones[0], zones[1], zones[2],
                                                                zones[3], zones[4], zones[5]}));
            ASSERT_TRUE(h.Load());
            BoardGameScreenTestAccess::RenderState(h.board)[0].is_ai = false;

            FakeInput corner_click;
            corner_click.ClickAt(600, 400);
            h.board.update(corner_click);
            ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                        BoardGameState::DuelChooseOpponent);

            FakeInput pick_click;
            pick_click.ClickAt(120, 120);  // inside player 1's cell
            h.board.update(pick_click);    // registers the pick, "breaks the glass"
            h.board.update(pick_click);    // glass audio already "done" (FakeAudio) -> resolves
            ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                        BoardGameState::DuelAttackGauge);
        });

    tests.AddCase("Headless: picking your own portrait cancels the duel (self-target) instead "
                  "of starting a fight",
                  []() {
                      HeadlessBoard h(2);
                      assets::RectEntry zones[6] = {};
                      zones[0] = {10, 10, 60, 60};  // char_id 0 == the attacker's OWN cell
                      const auto choose_path = AssetPaths::data("DATA", "choose", "BIN").string();
                      h.provider.SetFile(choose_path,
                                         BuildRectFileBytes({zones[0], zones[1], zones[2], zones[3],
                                                             zones[4], zones[5]}));
                      ASSERT_TRUE(h.Load());
                      BoardGameScreenTestAccess::RenderState(h.board)[0].is_ai = false;

                      FakeInput corner_click;
                      corner_click.ClickAt(600, 400);
                      h.board.update(corner_click);
                      ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                                  BoardGameState::DuelChooseOpponent);

                      FakeInput self_click;
                      self_click.ClickAt(30, 30);  // inside the attacker's own cell
                      h.board.update(self_click);

                      ASSERT_TRUE(BoardGameScreenTestAccess::CurrentState(h.board) ==
                                  BoardGameState::TurnEnd);
                  });

    // ---------------------------------------------------------------------
    // MAPMOVE.BIN zone routing: move_zone_polys_'s storage order matches
    // the real on-disk order (LEFT, RIGHT, CENTER) — GetMouseZone() returns
    // 0 for LEFT, 1 for RIGHT, 2 for CENTER.
    // ---------------------------------------------------------------------

    tests.AddCase("Headless: Moving-state LEFT zone click rotates direction CCW at a junction",
                  []() {
                      HeadlessBoard h(1);
                      SyntheticStone junction;
                      junction.connections = 4;
                      h.provider.SetFile("MAPFILE", BuildSyntheticMapFile(0, {junction}));
                      h.provider.SetFile("DATA/MAPMOVE.BIN", BuildMapMoveFileWithRealZones());
                      ASSERT_TRUE(h.Load());
                      BoardGameScreenTestAccess::CurrentState(h.board) = BoardGameState::Moving;
                      BoardGameScreenTestAccess::MapfileDirection(h.board) = 0;
                      BoardGameScreenTestAccess::ResetTurnStartTime(h.board);
                      FakeInput noop;
                      h.board.update(noop);  // runs UpdateMoving's state-entry init
                      BoardGameScreenTestAccess::ExpireMovementCooldown(h.board);

                      FakeInput input;
                      input.ClickAt(50, 50);  // inside the LEFT rectangle
                      h.board.update(input);

                      ASSERT_TRUE(BoardGameScreenTestAccess::MapfileDirection(h.board) == 3);
                  });

    tests.AddCase("Headless: Moving-state RIGHT zone click rotates direction CW at a junction",
                  []() {
                      HeadlessBoard h(1);
                      SyntheticStone junction;
                      junction.connections = 4;
                      h.provider.SetFile("MAPFILE", BuildSyntheticMapFile(0, {junction}));
                      h.provider.SetFile("DATA/MAPMOVE.BIN", BuildMapMoveFileWithRealZones());
                      ASSERT_TRUE(h.Load());
                      BoardGameScreenTestAccess::CurrentState(h.board) = BoardGameState::Moving;
                      BoardGameScreenTestAccess::MapfileDirection(h.board) = 0;
                      BoardGameScreenTestAccess::ResetTurnStartTime(h.board);
                      FakeInput noop;
                      h.board.update(noop);  // runs UpdateMoving's state-entry init
                      BoardGameScreenTestAccess::ExpireMovementCooldown(h.board);

                      FakeInput input;
                      input.ClickAt(250, 50);  // inside the RIGHT rectangle
                      h.board.update(input);

                      ASSERT_TRUE(BoardGameScreenTestAccess::MapfileDirection(h.board) == 1);
                  });

    tests.AddCase("Headless: Moving-state CENTER zone click never rotates (advance-only zone)",
                  []() {
                      HeadlessBoard h(1);
                      SyntheticStone junction;
                      junction.connections = 4;
                      h.provider.SetFile("MAPFILE", BuildSyntheticMapFile(0, {junction}));
                      h.provider.SetFile("DATA/MAPMOVE.BIN", BuildMapMoveFileWithRealZones());
                      ASSERT_TRUE(h.Load());
                      BoardGameScreenTestAccess::CurrentState(h.board) = BoardGameState::Moving;
                      BoardGameScreenTestAccess::MapfileDirection(h.board) = 0;
                      BoardGameScreenTestAccess::ResetTurnStartTime(h.board);
                      FakeInput noop;
                      h.board.update(noop);  // runs UpdateMoving's state-entry init
                      BoardGameScreenTestAccess::ExpireMovementCooldown(h.board);

                      FakeInput input;
                      input.ClickAt(150, 50);  // inside the CENTER rectangle
                      h.board.update(input);

                      ASSERT_TRUE(BoardGameScreenTestAccess::MapfileDirection(h.board) == 0);
                  });

    tests.AddCase("Headless: RollingDice-state LEFT zone click selects 1-die", []() {
        HeadlessBoard h(1);
        h.provider.SetFile("DATA/MAPMOVE.BIN", BuildMapMoveFileWithRealZones());
        ASSERT_TRUE(h.Load());
        BoardGameScreenTestAccess::CurrentState(h.board) = BoardGameState::RollingDice;
        BoardGameScreenTestAccess::ResetTurnStartTime(h.board);

        FakeInput input;
        input.ClickAt(50, 50);  // inside the LEFT rectangle
        h.board.update(input);

        ASSERT_TRUE(BoardGameScreenTestAccess::Dice(h.board).selected_option == 1);
    });

    tests.AddCase(
        "Headless: RollingDice-state RIGHT zone click selects 2-dice (unrestricted)", []() {
            HeadlessBoard h(1);
            h.provider.SetFile("DATA/MAPMOVE.BIN", BuildMapMoveFileWithRealZones());
            ASSERT_TRUE(h.Load());
            BoardGameScreenTestAccess::RenderState(h.board)[0].dice_restricted = 2;
            BoardGameScreenTestAccess::CurrentState(h.board) = BoardGameState::RollingDice;
            BoardGameScreenTestAccess::ResetTurnStartTime(h.board);

            FakeInput input;
            input.ClickAt(250, 50);  // inside the RIGHT rectangle
            h.board.update(input);

            ASSERT_TRUE(BoardGameScreenTestAccess::Dice(h.board).selected_option == 2);
        });

    tests.AddCase("Headless: RollingDice-state CENTER zone click is a no-op", []() {
        HeadlessBoard h(1);
        h.provider.SetFile("DATA/MAPMOVE.BIN", BuildMapMoveFileWithRealZones());
        ASSERT_TRUE(h.Load());
        BoardGameScreenTestAccess::CurrentState(h.board) = BoardGameState::RollingDice;
        BoardGameScreenTestAccess::ResetTurnStartTime(h.board);

        FakeInput input;
        input.ClickAt(150, 50);  // inside the CENTER rectangle
        h.board.update(input);

        ASSERT_TRUE(BoardGameScreenTestAccess::Dice(h.board).selected_option == 0);
    });
}
