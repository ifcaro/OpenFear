#include "openfear/platform/Platform.h"
#include "Ps2Audio.h"
#include "Ps2Boot.h"
#include "Ps2Renderer.h"
#include "SDL3Input.h"

#include <cstdio>
#include <malloc.h>

namespace openfear {

void platform_bootstrap() {
    ps2::ResetIop();
    ps2::LoadDriversAndFilesystems();
}

bool platform_prompt_retry_assets_missing(Platform& platform) {
    return ps2::PromptAssetsMissing(platform);
}

void platform_log_memory_stats(const char* tag, IRenderer& renderer) {
    struct mallinfo mi = mallinfo();
    // `ordblks` is the number of free chunks: rising over a match while
    // `fordblks` holds steady means free space is fragmenting into pieces
    // too small to reuse for the next big texture, even though "total
    // free" alone would look fine.
    std::fprintf(stderr,
                 "[MEM] %-14s arena=%uKB used=%uKB free=%uKB free_chunks=%u avg_free_chunk=%uB "
                 "textures=%zu\n",
                 tag, (unsigned)(mi.arena / 1024), (unsigned)(mi.uordblks / 1024),
                 (unsigned)(mi.fordblks / 1024), (unsigned)mi.ordblks,
                 mi.ordblks ? (unsigned)(mi.fordblks / mi.ordblks) : 0, renderer.textureCount());
}

std::unique_ptr<Platform> create_platform() {
    auto p = std::make_unique<Platform>();
    p->renderer = std::make_unique<sdl3::Ps2Renderer>();
    p->audio = std::make_unique<sdl3::Ps2Audio>();
    p->input = std::make_unique<sdl3::SDL3Input>();
    return p;
}

}  // namespace openfear
