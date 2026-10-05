/*
 * OpenFear — Atmosfear PC reimplementation
 *
 * This entry point owns process and runtime setup (logging, asset
 * discovery, platform init). The real screen flow lives in GameFlow.cpp.
 */

#include "GameFlow.h"
#include "RuntimeDiagnostics.h"
#include "RuntimeSetup.h"
#include "ScreenLoop.h"
#if !defined(__PS2__) && !defined(__EMSCRIPTEN__)
#  include "InstallFlow.h"
#endif

#include "openfear/assets/AssetManager.h"
#include "openfear/platform/BitmapFont.h"

#ifdef _WIN32
#  include <io.h>
#else
#  include <unistd.h>
#endif

#include <cstdio>
#include <filesystem>
#include <new>
#include <string>

using namespace openfear;

namespace {

void RedirectDesktopLog(const char* executable_path) {
#if !defined(__EMSCRIPTEN__) && !defined(__PS2__)
    std::filesystem::path log_path =
        std::filesystem::path(executable_path).parent_path() / "openfear.log";
    FILE* log_file = std::fopen(log_path.string().c_str(), "w");
    if (!log_file) return;
#  ifdef _WIN32
    _dup2(_fileno(log_file), _fileno(stderr));
#  else
    dup2(fileno(log_file), fileno(stderr));
#  endif
    std::fclose(log_file);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
#else
    (void)executable_path;
#endif
}

void ShowLoadingScreen(Platform& platform) {
    platform::BitmapFont loading_font;
    const bool font_ok = loading_font.Load(*platform.renderer);
    constexpr int kBarW = 300, kBarH = 20;
    const int bar_x = (640 - kBarW) / 2, bar_y = 240;
    auto draw_loading = [&](int percent) {
        platform.renderer->beginFrame();
        platform.renderer->clear(0, 0, 0);
        if (font_ok) {
            const char* kMsg = "Loading...";
            const int x = (640 - loading_font.TextWidth(kMsg)) / 2;
            loading_font.DrawText(*platform.renderer, x, 200, kMsg, 255, 255, 255);
        }
        platform.renderer->fillRect(bar_x - 2, bar_y - 2, kBarW + 4, kBarH + 4, 90, 90, 90);
        platform.renderer->fillRect(bar_x, bar_y, kBarW, kBarH, 20, 20, 20);
        const int fill_w = (kBarW * percent) / 100;
        if (fill_w > 0) {
            platform.renderer->fillRect(bar_x, bar_y, fill_w, kBarH, 200, 60, 60);
        }
        platform.renderer->endFrame();
    };

    platform.poll();
    if (platform.cursor) platform.cursor->SetVisible(false);
    platform.audio->preloadSetupPhase(
        [&](int done, int total) { draw_loading(total > 0 ? (done * 100) / total : 100); });
    if (font_ok) loading_font.Unload(*platform.renderer);
}

}  // namespace

int main(int argc, char* argv[]) {
    InstallRuntimeDiagnostics();
    RedirectDesktopLog(argv[0]);
    LOG("=== OpenFear startup ===\n");

#if !defined(__PS2__) && !defined(__EMSCRIPTEN__)
    if (auto exit_code = RunInstallIfRequested(argc, argv)) {
        return *exit_code;
    }
#endif

    auto runtime = InitializeRuntime(argc, argv);
    if (!runtime) return 1;
    auto platform = std::move(runtime->platform);
    auto asset_provider = std::move(runtime->asset_provider);
    const auto asset_root = std::move(runtime->asset_root);
    const AssetPaths paths{};
    LOG("asset root: %s\n", asset_root.string().c_str());

    ShowLoadingScreen(*platform);
    LogMemoryStats("after-preload-wavs", *platform->renderer);

    assets::AssetManager assets(asset_root, *platform->renderer, *asset_provider);
    try {
        return RunGameFlow(argc, argv, platform.get(), assets, paths);
    } catch (const std::bad_alloc&) {
        // Uncaught, this would reach std::terminate() with nothing visible
        // on hardware with no console attached - show the player something
        // instead of a silent hang/reset.
        std::fputs("[MAIN] FATAL: out of memory\n", stderr);
        platform::BitmapFont font;
        if (font.Load(*platform->renderer)) {
            platform->renderer->beginFrame();
            platform->renderer->clear(0, 0, 0);
            font.DrawText(*platform->renderer, 40, 200, "FATAL: out of memory.", 255, 80, 80);
            const std::string mem_stats = FormatMemoryStats(*platform->renderer);
            int mem_y = 220;
            for (size_t start = 0; start < mem_stats.size();) {
                const size_t end = mem_stats.find('\n', start);
                const std::string line = mem_stats.substr(start, end - start);
                font.DrawText(*platform->renderer, 40, mem_y, line.c_str(), 200, 200, 200);
                mem_y += 20;
                if (end == std::string::npos) break;
                start = end + 1;
            }
            font.DrawText(*platform->renderer, 40, mem_y + 10, "Please restart and report this.",
                          255, 255, 255);
            platform->renderer->endFrame();
            WaitForDismissOrTimeout(*platform);
        }
        platform->shutdown();
        return 1;
    }
}
