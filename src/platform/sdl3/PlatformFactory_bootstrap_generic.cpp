#include "openfear/platform/Platform.h"
#include "SDL3Renderer.h"
#include "SDL3Audio.h"
#include "SDL3Input.h"

#ifdef _WIN32
#  include <windows.h>
#endif
#ifdef __EMSCRIPTEN__
#  include "EmscriptenAudio.h"
#endif

namespace openfear {

void platform_bootstrap() {
#ifdef _WIN32
    // Without this, opening a file on a CD/DVD drive with no disc inserted
    // (AssetLocator::from_cd_scan()'s drive-letter sweep) blocks behind the
    // OS's own "Please insert a disk" dialog instead of just failing.
    SetErrorMode(SEM_FAILCRITICALERRORS);
#endif
}

bool platform_prompt_retry_assets_missing(Platform&) {
    return false;
}

void platform_log_memory_stats(const char*, IRenderer&) {}

std::unique_ptr<Platform> create_platform() {
    auto p = std::make_unique<Platform>();
    p->renderer = std::make_unique<sdl3::SDL3Renderer>();
#ifdef __EMSCRIPTEN__
    p->audio = std::make_unique<sdl3::EmscriptenAudio>();
#else
    p->audio = std::make_unique<sdl3::SDL3Audio>();
#endif
    p->input = std::make_unique<sdl3::SDL3Input>();
    return p;
}

}  // namespace openfear
