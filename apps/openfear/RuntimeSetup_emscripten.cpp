#include "RuntimeSetup.h"

#include "openfear/assets/EmscriptenAssetProvider.h"
#include "openfear/platform/Platform.h"

#include <cstdio>

namespace openfear {

std::optional<RuntimeContext> InitializeRuntime(int, char*[]) {
    RuntimeContext context;
    platform_bootstrap();

    auto provider = std::make_unique<EmscriptenAssetProvider>();
    // Re-shows the "Click to start" picker on its own if the user cancels -
    // no reason to make them reload the whole page for that.
    while (!provider->StartGate()) {
        std::fputs("No assets folder selected, asking again.\n", stderr);
    }
    context.asset_provider = std::move(provider);
    context.asset_root = "";

    context.platform = create_platform();
    if (!context.platform->init("Atmosfear")) {
        std::fputs("Platform init failed\n", stderr);
        return std::nullopt;
    }
    context.platform->attachAssetProvider(context.asset_provider.get());
    return context;
}

}  // namespace openfear
