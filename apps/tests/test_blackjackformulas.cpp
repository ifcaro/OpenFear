#include "test_framework.h"
#include "openfear/game/subscreens/BlackjackGame.h"

using namespace openfear::game;

// BlackjackGame::ComputeHandValue — extracted from 2 duplicated inline
// lambdas (player/dealer scoring) into a single pure static formula so it's
// unit-testable without a live game/RNG. Rank encoding: 1=Ace, 2-10=face
// value, 11+=face card (10), 0=empty slot.
void RegisterBlackjackFormulaTests(Tests& tests) {

    tests.AddCase("BlackjackFormulas: simple hand sums face values", []() {
        const int cards[] = {5, 4};
        ASSERT_EQ(9, BlackjackGame::ComputeHandValue(cards, 2));
    });

    tests.AddCase("BlackjackFormulas: face cards (rank 11+) all count as 10", []() {
        const int cards[] = {13, 12, 11};  // King, Queen, Jack
        ASSERT_EQ(30, BlackjackGame::ComputeHandValue(cards, 3));
    });

    tests.AddCase("BlackjackFormulas: a lone ace counts as 11 (soft hand)", []() {
        const int cards[] = {1, 5};
        ASSERT_EQ(16, BlackjackGame::ComputeHandValue(cards, 2));
    });

    tests.AddCase("BlackjackFormulas: an ace reduces from 11 to 1 to avoid busting "
                  "(ace + king + 9 = 20, not 30)",
                  []() {
                      const int cards[] = {1, 13, 9};
                      ASSERT_EQ(20, BlackjackGame::ComputeHandValue(cards, 3));
                  });

    tests.AddCase("BlackjackFormulas: natural blackjack — ace + face card = 21", []() {
        const int cards[] = {1, 12};
        ASSERT_EQ(21, BlackjackGame::ComputeHandValue(cards, 2));
    });

    tests.AddCase("BlackjackFormulas: two aces never mix (ace+ace+9 = 11, the all-11 "
                  "total of 31 busts so it falls back to the all-1 total, not a real "
                  "blackjack table's optimal 21)",
                  []() {
                      const int cards[] = {1, 1, 9};
                      ASSERT_EQ(11, BlackjackGame::ComputeHandValue(cards, 3));
                  });

    tests.AddCase("BlackjackFormulas: two aces fall back to all-1 when the all-11 total "
                  "busts too (ace+ace+king+king+king = 32)",
                  []() {
                      const int cards[] = {1, 1, 13, 13, 13};
                      ASSERT_EQ(32, BlackjackGame::ComputeHandValue(cards, 5));
                  });

    tests.AddCase("BlackjackFormulas: a hard hand over 21 with no ace to reduce is a bust "
                  "as-is",
                  []() {
                      const int cards[] = {13, 9, 5};  // 10+9+5 = 24
                      ASSERT_EQ(24, BlackjackGame::ComputeHandValue(cards, 3));
                  });

    tests.AddCase("BlackjackFormulas: empty slots (rank 0) contribute nothing", []() {
        const int cards[] = {7, 0, 0, 0, 0};
        ASSERT_EQ(7, BlackjackGame::ComputeHandValue(cards, 5));
    });

    tests.AddCase("BlackjackFormulas: zero cards scores 0",
                  []() { ASSERT_EQ(0, BlackjackGame::ComputeHandValue(nullptr, 0)); });
}
