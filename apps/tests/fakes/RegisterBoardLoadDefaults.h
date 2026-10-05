#pragma once

// Registers minimal valid defaults for every asset BoardGameScreen::load()
// hard-requires - shared by every headless fixture that constructs a real
// BoardGameScreen, so load() succeeds by default. Tests that care about a
// specific file's content override it with their own SetFile() after
// calling this.

#include "FakeAssetProvider.h"
#include "SyntheticMapFile.h"
#include "SyntheticMapMove.h"
#include "SyntheticRectFile.h"

namespace openfear::testing {

inline void RegisterBoardLoadDefaults(FakeAssetProvider& provider) {
    provider.SetFile("MAPFILE", BuildSyntheticMapFile(0, {}));
    provider.SetFile("DATA/MAPMOVE.BIN", BuildSyntheticMapMoveFile());
    provider.SetFile("DATA/CHOOSE.BIN", BuildRectFileBytes(std::vector<assets::RectEntry>(6)));
    provider.SetFile("DATA/GETKEYS.BIN", BuildRectFileBytes(std::vector<assets::RectEntry>(9)));
    provider.SetFile("DATA/BJACK1.BIN", BuildRectFileBytes(std::vector<assets::RectEntry>(2)));
    provider.SetFile("DATA/BJACK2.BIN", BuildRectFileBytes(std::vector<assets::RectEntry>(4)));
    provider.SetFile("DATA/NUMS.BIN", BuildRectFileBytes(std::vector<assets::RectEntry>(10)));
    provider.SetFile("DATA/DOOR3.BIN", BuildRectFileBytes(std::vector<assets::RectEntry>(3)));
    provider.SetFile("DATA/DOOR6.BIN", BuildRectFileBytes(std::vector<assets::RectEntry>(6)));
    provider.SetFile("DATA/DOOR10.BIN", BuildRectFileBytes(std::vector<assets::RectEntry>(10)));
    provider.SetFile("DATA/GATEOPEN.BIN", BuildRectFileBytes(std::vector<assets::RectEntry>(2)));
    provider.SetFile("DATA/POWCOFF.BIN", BuildRectFileBytes(std::vector<assets::RectEntry>(2)));
    provider.SetFile("DATA/CURECURS.BIN", BuildRectFileBytes(std::vector<assets::RectEntry>(2)));
    provider.SetFile("DATA/GAUGE1.BIN", BuildRectFileBytes(std::vector<assets::RectEntry>(1)));
    provider.SetFile("DATA/GAUGE2.BIN", BuildRectFileBytes(std::vector<assets::RectEntry>(1)));
}

}  // namespace openfear::testing
