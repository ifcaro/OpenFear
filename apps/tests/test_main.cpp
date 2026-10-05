#include "test_framework.h"
#include <iostream>

void RegisterMapfileTests(Tests& tests);
void RegisterPlayerInitTests(Tests& tests);
void RegisterBoardGameFormulaTests(Tests& tests);
void RegisterBoardGameScreenHeadlessTests(Tests& tests);
void RegisterBlackjackFormulaTests(Tests& tests);
void RegisterPostGameSequenceHeadlessTests(Tests& tests);
void RegisterNativeAssetProviderTests(Tests& tests);

int main() {
    std::cout << "\n";
    std::cout << std::string(80, '=') << "\n";
    std::cout << "  OpenFear Test Suite\n";
    std::cout << std::string(80, '=') << "\n";
    std::cout << "\n";
    std::cout << "NOTE: Run from project root (assets/MAPFILE must be accessible)\n";
    std::cout << "\n";

    Tests tests;

    RegisterMapfileTests(tests);
    RegisterPlayerInitTests(tests);
    RegisterBoardGameFormulaTests(tests);
    RegisterBoardGameScreenHeadlessTests(tests);
    RegisterBlackjackFormulaTests(tests);
    RegisterPostGameSequenceHeadlessTests(tests);
    RegisterNativeAssetProviderTests(tests);

    int failed = tests.RunAll();
    return (failed == 0) ? 0 : 1;
}
