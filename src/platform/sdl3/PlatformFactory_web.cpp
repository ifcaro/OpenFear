#include "openfear/platform/Platform.h"
#include <emscripten.h>

namespace openfear {

// emscripten_sleep() is the Asyncify-aware primitive that yields to the
// browser event loop under Asyncify - plain sleep_for does not, and would
// freeze the tab in a blocking while(true) screen loop instead of
// cooperating with it.
void platform_pace_frame(std::chrono::steady_clock::time_point frame_begin,
                         std::chrono::microseconds frame_target) {
    const auto elapsed = std::chrono::steady_clock::now() - frame_begin;
    if (elapsed < frame_target) {
        emscripten_sleep(static_cast<unsigned int>(
            std::chrono::round<std::chrono::milliseconds>(frame_target - elapsed).count()));
    }
}

void platform_pre_vsync_yield() {}

}  // namespace openfear
