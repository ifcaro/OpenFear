#pragma once

#include "openfear/AssetLocator.h"
#include "openfear/assets/AssetManager.h"
#include "openfear/platform/Platform.h"

namespace openfear {

int RunGameFlow(int argc, char* argv[], Platform* platform, assets::AssetManager& assets,
                const AssetPaths& paths);

}  // namespace openfear
