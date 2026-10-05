#include "test_framework.h"
#include "BoardGameScreenTestAccess.h"
#include "openfear/game/BoardGameScreen.h"
#include <cmath>
#include <cstdint>

using namespace openfear::game;

// Pure static formulas on BoardGameScreen — no instance/assets needed. Real
// gameplay logic (KeyAward (enc type 15) target roll, black-hole retry,
// key-rotation lookups), not data tables, so these are worth unit-testing
// per the project's own "test logic, not static data" convention.
void RegisterBoardGameFormulaTests(Tests& tests) {

    tests.AddCase("BoardGameFormulas: KeyAwardRestricted true under 10min with <3 keys", []() {
        ASSERT_TRUE(BoardGameScreen::ComputeKeyAwardRestricted(599, 2));
        ASSERT_TRUE(BoardGameScreen::ComputeKeyAwardRestricted(0, 0));
    });

    tests.AddCase("BoardGameFormulas: KeyAwardRestricted false at exactly 600s (not <600)",
                  []() { ASSERT_FALSE(BoardGameScreen::ComputeKeyAwardRestricted(600, 0)); });

    tests.AddCase("BoardGameFormulas: KeyAwardRestricted false with 3+ keys held",
                  []() { ASSERT_FALSE(BoardGameScreen::ComputeKeyAwardRestricted(0, 3)); });

    tests.AddCase("BoardGameFormulas: KeyAwardRestricted false with plenty of time left",
                  []() { ASSERT_FALSE(BoardGameScreen::ComputeKeyAwardRestricted(1000, 0)); });

    tests.AddCase("BoardGameFormulas: KeyAwardTarget always 1 when not restricted", []() {
        ASSERT_EQ(1, BoardGameScreen::ComputeKeyAwardTarget(false, 0, 0));
        ASSERT_EQ(1, BoardGameScreen::ComputeKeyAwardTarget(false, 5, 0x7fff));
    });

    tests.AddCase("BoardGameFormulas: KeyAwardTarget restricted range is 1..(6-held)", []() {
        // rng_raw=0 -> the low end of the range.
        ASSERT_EQ(1, BoardGameScreen::ComputeKeyAwardTarget(true, 0, 0));
        ASSERT_EQ(1, BoardGameScreen::ComputeKeyAwardTarget(true, 5, 0));
        // rng_raw=0x7FFF (max) -> the high end, never past 6-held.
        ASSERT_EQ(6, BoardGameScreen::ComputeKeyAwardTarget(true, 0, 0x7fff));
        ASSERT_EQ(3, BoardGameScreen::ComputeKeyAwardTarget(true, 3, 0x7fff));
        ASSERT_EQ(1, BoardGameScreen::ComputeKeyAwardTarget(true, 5, 0x7fff));
    });

    tests.AddCase("BoardGameFormulas: BlackHoleRoll stays in range 1-6", []() {
        ASSERT_EQ(1, BoardGameScreen::ComputeBlackHoleRoll(0));
        ASSERT_EQ(6, BoardGameScreen::ComputeBlackHoleRoll(0x7fff));
        for (uint32_t raw = 0; raw <= 0x7fff; raw += 137) {
            const int roll = BoardGameScreen::ComputeBlackHoleRoll(raw);
            ASSERT_GE(roll, 1);
            ASSERT_LE(roll, 6);
        }
    });

    tests.AddCase("BoardGameFormulas: BlackHoleResolves true when retry_flag==4 (guaranteed cap)",
                  []() {
                      ASSERT_TRUE(BoardGameScreen::ComputeBlackHoleResolves(4, 6));
                      ASSERT_TRUE(BoardGameScreen::ComputeBlackHoleResolves(4, 1));
                  });

    tests.AddCase("BoardGameFormulas: BlackHoleResolves true when retry_flag>roll, false otherwise",
                  []() {
                      ASSERT_TRUE(BoardGameScreen::ComputeBlackHoleResolves(3, 2));
                      ASSERT_FALSE(BoardGameScreen::ComputeBlackHoleResolves(3, 3));
                      ASSERT_FALSE(BoardGameScreen::ComputeBlackHoleResolves(1, 1));
                  });

    tests.AddCase("BoardGameFormulas: FirstUnheldInRotation returns -1 when all 6 keys held", []() {
        uint16_t keys[6] = {1, 1, 1, 1, 1, 1};
        ASSERT_EQ(-1, BoardGameScreen::ComputeFirstUnheldInRotation(0, keys));
    });

    tests.AddCase(
        "BoardGameFormulas: FirstUnheldInRotation follows kKeyTable order, not raw province order",
        []() {
            // kKeyTable = {0,5,1,4,2,3}; char_id=0 rotation visits province 0
            // first, then 5 — with only province 5 unheld, that's the 2nd
            // province checked, not the 6th (a naive 0..5 scan would find it last).
            uint16_t keys[6] = {1, 1, 1, 1, 1, 0};
            ASSERT_EQ(5, BoardGameScreen::ComputeFirstUnheldInRotation(0, keys));
        });

    tests.AddCase("BoardGameFormulas: FirstUnheldInRotation rotation shifts with char_id", []() {
        uint16_t keys[6] = {0, 0, 0, 0, 0, 0};  // none held — first slot in rotation wins
        ASSERT_EQ(0, BoardGameScreen::ComputeFirstUnheldInRotation(0, keys));
        ASSERT_EQ(1, BoardGameScreen::ComputeFirstUnheldInRotation(1, keys));
    });

    tests.AddCase("BoardGameFormulas: LastHeldInRotation returns -1 when none held", []() {
        uint16_t keys[6] = {0, 0, 0, 0, 0, 0};
        ASSERT_EQ(-1, BoardGameScreen::ComputeLastHeldInRotation(0, keys));
    });

    tests.AddCase("BoardGameFormulas: LastHeldInRotation keeps overwriting to the LAST held slot "
                  "in rotation order",
                  []() {
                      // char_id=0 rotation order is province 0,5,1,4,2,3. Provinces 0 and
                      // 2 held - province 2 comes later in that order, so it wins over
                      // province 0 despite having the lower raw index.
                      uint16_t keys[6] = {1, 0, 1, 0, 0, 0};
                      ASSERT_EQ(2, BoardGameScreen::ComputeLastHeldInRotation(0, keys));
                  });

    tests.AddCase(
        "BoardGameFormulas: AppendLiteralWeightCategory normalizes non-uniform literal ratios",
        []() {
            const int ids[4] = {10, 11, 12, 13};
            const float weights[4] = {10.0f, 10.0f, 10.0f, 5.0f};
            BoardGameScreenTestAccess::WeightedPickEntry entries[4];
            int n = 0;
            BoardGameScreenTestAccess::AppendLiteralWeightCategory(ids, weights, 4, 8, entries, n);
            ASSERT_EQ(4, n);
            ASSERT_TRUE(std::fabs(entries[0].weight - 14.2857f) < 0.01f);
            ASSERT_TRUE(std::fabs(entries[1].weight - 14.2857f) < 0.01f);
            ASSERT_TRUE(std::fabs(entries[2].weight - 14.2857f) < 0.01f);
            ASSERT_TRUE(std::fabs(entries[3].weight - 7.1429f) < 0.01f);
        });

    tests.AddCase(
        "BoardGameFormulas: AppendLiteralWeightCategory with equal weights matches a flat split",
        []() {
            const int ids[2] = {0, 1};
            const float weights[2] = {1.0f, 1.0f};
            BoardGameScreenTestAccess::WeightedPickEntry entries[2];
            int n = 0;
            BoardGameScreenTestAccess::AppendLiteralWeightCategory(ids, weights, 2, 4, entries, n);
            ASSERT_TRUE(std::fabs(entries[0].weight - 25.0f) < 0.01f);
            ASSERT_TRUE(std::fabs(entries[1].weight - 25.0f) < 0.01f);
        });

    tests.AddCase(
        "BoardGameFormulas: LastHeldInRotation with all held returns the rotation's last slot",
        []() {
            uint16_t keys[6] = {1, 1, 1, 1, 1, 1};
            // char_id=0 rotation order 0,5,1,4,2,3 - last visited is province 3.
            ASSERT_EQ(3, BoardGameScreen::ComputeLastHeldInRotation(0, keys));
        });

    tests.AddCase("BoardGameFormulas: KeyPoolTokenCount scales with player count", []() {
        ASSERT_EQ(6, BoardGameScreen::ComputeKeyPoolTokenCount(1));
        ASSERT_EQ(8, BoardGameScreen::ComputeKeyPoolTokenCount(2));
        ASSERT_EQ(12, BoardGameScreen::ComputeKeyPoolTokenCount(3));
        ASSERT_EQ(14, BoardGameScreen::ComputeKeyPoolTokenCount(4));
        ASSERT_EQ(20, BoardGameScreen::ComputeKeyPoolTokenCount(5));
        ASSERT_EQ(24, BoardGameScreen::ComputeKeyPoolTokenCount(6));
    });

    tests.AddCase("BoardGameFormulas: SeedKeyPool with 1 player splits evenly (6/6)", []() {
        uint16_t pool[6];
        BoardGameScreen::SeedKeyPool(pool, 1, /*start_province=*/0);
        for (int p = 0; p < 6; ++p) ASSERT_EQ(1, pool[p]);
    });

    tests.AddCase("BoardGameFormulas: SeedKeyPool with 6 players splits evenly (24/6)", []() {
        uint16_t pool[6];
        BoardGameScreen::SeedKeyPool(pool, 6, /*start_province=*/2);
        for (int p = 0; p < 6; ++p) ASSERT_EQ(4, pool[p]);
    });

    tests.AddCase(
        "BoardGameFormulas: SeedKeyPool with 2 players splits unevenly, extra tokens start at "
        "start_province",
        []() {
            // 8 tokens / 6 provinces: provinces {3,4} (start_province=3, then wrapping) get 2,
            // the other 4 get 1.
            uint16_t pool[6];
            BoardGameScreen::SeedKeyPool(pool, 2, /*start_province=*/3);
            int total = 0;
            int extra_count = 0;
            for (int p = 0; p < 6; ++p) {
                ASSERT_TRUE(pool[p] == 1 || pool[p] == 2);
                if (pool[p] == 2) ++extra_count;
                total += pool[p];
            }
            ASSERT_EQ(8, total);
            ASSERT_EQ(2, extra_count);
            ASSERT_EQ(2, pool[3]);
            ASSERT_EQ(2, pool[4]);
        });

    tests.AddCase("BoardGameFormulas: SeedKeyPool normalizes an out-of-range start_province", []() {
        uint16_t pool[6];
        BoardGameScreen::SeedKeyPool(pool, 1, /*start_province=*/-6);
        for (int p = 0; p < 6; ++p) ASSERT_EQ(1, pool[p]);
    });
}
