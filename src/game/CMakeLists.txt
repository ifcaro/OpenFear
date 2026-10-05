add_library(openfear_game STATIC
    BoardGameScreen.cpp
    BoardGameScreenUpdate.cpp
    BoardGameScreenRender.cpp
    BoardGameScreenMovement.cpp
    BoardGameScreenEncounters.cpp
    BoardGameScreenDuelAndKeys.cpp
    BoardInitializer.cpp
    ChooseLevelScreen.cpp
    FearSelectScreen.cpp
    FocusIndicator.cpp
    MasterControlScreen.cpp
    IntroScreen.cpp
    MapFile.cpp
    PickCharScreen.cpp
    PickPlayersScreen.cpp
    PlayerInitializer.cpp
    subscreens/PauseScreen.cpp
    subscreens/MinimapScreen.cpp
    subscreens/BribeGame.cpp
    subscreens/CureCurseGame.cpp
    subscreens/FortuneDoorGame.cpp
    subscreens/FlightMap.cpp
    subscreens/CoffinChoiceGame.cpp
    subscreens/SlotMachineGame.cpp
    subscreens/DoorsGame.cpp
    subscreens/DiceKeysGame.cpp
    subscreens/OneDieGame.cpp
    subscreens/KeyAwardGame.cpp
    subscreens/DuelAttackGauge.cpp
    subscreens/DuelChooseOpponent.cpp
    subscreens/DuelStealOrCurse.cpp
    subscreens/BlackjackGame.cpp
)
target_include_directories(openfear_game PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(openfear_game PUBLIC
    openfear_assets
)

# BoardGameScreen's manual-QA-only methods (--test-*/--cheat-* hooks called
# from apps/openfear/DebugQuickstart.cpp, and by the headless test suite) —
# Debug-config only, same pattern as DebugQuickstart.cpp itself, so a Release
# openfear.exe never links them. openfear-tests is a dev-only tool, always
# built Debug in practice (no CI/workflow builds it in Release) — simpler to
# gate this once here than duplicate the source across every consumer target.
target_sources(openfear_game PRIVATE $<$<CONFIG:Debug>:BoardGameScreenDebug.cpp>)

# Digit font strip (steps/time/power displays) — OpenFear's own asset, not
# part of the CD data; embedded directly instead of loaded from a runtime
# path (see openfear_embed_binary_file()'s doc comment for why).
openfear_embed_binary_file(openfear_game
    "${PROJECT_SOURCE_DIR}/data/fonts/digitfont.bmp"
    g_digitfont_bmp
)
