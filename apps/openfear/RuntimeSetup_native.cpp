#include "RuntimeSetup.h"

#include "openfear/AssetLocator.h"
#include "openfear/assets/NativeAssetProvider.h"
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
        std::fputs("Atmosfear assets not found.\n"
                   "Options:\n"
                   "  1. Run: openfear --install (copies your CD/game files into place)\n"
                   "  2. Run: openfear --assets <path>\n"
                   "  3. Set: OPENFEAR_ASSETS=<path>\n"
                   "  4. Place an \"assets\" folder in the directory you run openfear.exe from\n",
                   stderr);
        show_error_dialog(
            "OpenFear",
            "Could not find your Atmosfear game files.\n\n"
            "Run \"openfear --install\" with your original CD or game files to set things up, "
            "then restart.");
        context.platform->shutdown();
        return std::nullopt;
    }
    context.asset_root = *discovered_root;
    context.asset_provider = MakeAssetProvider<NativeAssetProvider>(context.asset_root);
    context.platform->attachAssetProvider(context.asset_provider.get());
    return context;
}

}  // namespace openfear
