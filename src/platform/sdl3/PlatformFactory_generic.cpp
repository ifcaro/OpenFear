#include "openfear/platform/Platform.h"
#include <thread>

namespace openfear {

void platform_pace_frame(std::chrono::steady_clock::time_point frame_begin,
                         std::chrono::microseconds frame_target) {
    const auto elapsed = std::chrono::steady_clock::now() - frame_begin;
    if (elapsed < frame_target) {
        std::this_thread::sleep_for(frame_target - elapsed);
    }
}

void platform_pre_vsync_yield() {}

}  // namespace openfear
