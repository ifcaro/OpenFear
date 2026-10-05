#include "test_framework.h"
#include "openfear/game/PlayerInitializer.h"
#include <cstdint>

using namespace openfear::game;

// LCG constants — must stay bit-exact with PlayerInitializer::GetRandomNumber():
//   seed_new = (seed * 0x015A4E35 + 1) & 0xFFFFFFFF
//   output   = (seed_new >> 16) & 0x7FFF
static constexpr uint32_t kMultiplier = 0x015A4E35;
static constexpr uint32_t kIncrement = 1;
static constexpr uint16_t kMask15 = 0x7fff;

void RegisterPlayerInitTests(Tests& tests) {

    tests.AddCase("PlayerInit: GetRandomNumber() never returns > 0x7FFF (15-bit max)", []() {
        PlayerInitializer::InitializeSeed();
        for (int i = 0; i < 1000; ++i) {
            uint16_t val = PlayerInitializer::GetRandomNumber();
            ASSERT_TRUE((val & ~kMask15) == 0);
        }
    });

    tests.AddCase("PlayerInit: LCG formula matches spec (seed_new = seed * 0x015A4E35 + 1)", []() {
        PlayerInitializer::InitializeSeed();

        uint32_t seed_before = PlayerInitializer::GetCurrentRandomSeed();
        uint16_t output = PlayerInitializer::GetRandomNumber();
        uint32_t seed_after = PlayerInitializer::GetCurrentRandomSeed();

        uint32_t expected_seed = (seed_before * kMultiplier + kIncrement) & 0xffffffff;
        ASSERT_EQ(static_cast<int>(expected_seed), static_cast<int>(seed_after));

        uint16_t expected_output = static_cast<uint16_t>((seed_after >> 16) & kMask15);
        ASSERT_EQ(static_cast<int>(expected_output), static_cast<int>(output));
    });

    tests.AddCase("PlayerInit: 10 consecutive seeds are all different", []() {
        PlayerInitializer::InitializeSeed();

        uint32_t seeds[10];
        for (int i = 0; i < 10; ++i) {
            PlayerInitializer::GetRandomNumber();
            seeds[i] = PlayerInitializer::GetCurrentRandomSeed();
        }
        for (int i = 0; i < 9; ++i) {
            ASSERT_NE(seeds[i], seeds[i + 1]);
        }
    });
}
