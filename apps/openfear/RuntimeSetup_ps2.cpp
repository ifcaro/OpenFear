#include "RuntimeSetup.h"

#include "openfear/AssetLocator.h"
#include "openfear/assets/Ps2AssetProvider.h"
#include "openfear/platform/Platform.h"

#include <cstdio>

namespace openfear {

std::optional<RuntimeContext> InitializeRuntime(int argc, char* argv[]) {
    RuntimeContext context;
    platform_bootstrap();

    context.platform = create_platform();
    if (!context.platform->init("Atmosfear")) {
        std::fputs("Platform init failed\n", stderr);
        return std::nullopt;
    }

    context.platform->renderer->beginFrame();
    context.platform->renderer->clear(0, 0, 0);
    context.platform->renderer->endFrame();

    std::optional<std::filesystem::path> discovered_root;
    for (;;) {
        discovered_root = AssetLocator::discover(argc, argv);
        if (discovered_root) break;
        if (!platform_prompt_retry_assets_missing(*context.platform)) break;
    }
    if (!discovered_root) {
        // The retry loop above only exits on SDL_EVENT_QUIT, and already
        // showed its own "insert CD/USB" message - no separate one needed.
        std::fputs("Atmosfear assets not found, quit requested.\n", stderr);
        context.platform->shutdown();
        return std::nullopt;
    }
    context.asset_root = *discovered_root;
    context.asset_provider = MakeAssetProvider<Ps2AssetProvider>(context.asset_root);
    context.platform->attachAssetProvider(context.asset_provider.get());
    return context;
}

}  // namespace openfear
