#pragma once

// Manual-QA-only quickstarts and cheat flags. Deliberately NOT compiled into
// Release builds: excluded from the target entirely in Release config (see
// apps/openfear/CMakeLists.txt's `$<$<CONFIG:Debug>:...>` on
// DebugQuickstart.cpp) and this header's own content is gated behind
// `#ifndef NDEBUG` so a stray Release include is a silent no-op rather than
// a link error against a missing translation unit.
#ifndef NDEBUG

#  include "openfear/platform/Platform.h"
#  include "openfear/assets/AssetManager.h"
#  include "openfear/AssetLocator.h"
#  include <optional>

namespace openfear::debugqs {

// Parses every --test-* quickstart flag; if one matches, runs that
// quickstart to completion (including ENDVID/PLAYAGN/credits where
// applicable) and returns the process exit code. Returns nullopt if no
// quickstart flag is present - the caller should proceed with the real
// GameFlow.cpp flow instead.
std::optional<int> RunIfRequested(int argc, char* argv[], Platform& platform,
                                  assets::AssetManager& assets, const AssetPaths& paths);

// --test-char N (char_id 0-5), for the real flow's own PickCharScreen to
// skip the random click for a specific character. -1 if not present.
int ParseTestChar(int argc, char* argv[]);

// Manual-QA cheats applicable to the REAL (non-quickstart) BoardGameScreen
// entry point, not just a quickstart.
struct CheatFlags {
    bool easy_win = false;         // --cheat-easy-win
    bool all_keys = false;         // --cheat-all-keys
    bool gatekeeper_lose = false;  // --cheat-gatekeeper-lose
};
CheatFlags ParseCheatFlags(int argc, char* argv[]);

}  // namespace openfear::debugqs

#endif  // NDEBUG
