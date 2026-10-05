#include "openfear/platform/Platform.h"
#include <kernel.h>
#include <delaythread.h>

namespace openfear {

// std::this_thread::sleep_for doesn't cooperate with ps2sdk kernel threads
// (SDL's audio-feeder among them) - one can go unscheduled for seconds.
// DelayThread() is ps2sdk's own primitive, scheduled correctly instead.
void platform_pace_frame(std::chrono::steady_clock::time_point frame_begin,
                         std::chrono::microseconds frame_target) {
    const auto elapsed = std::chrono::steady_clock::now() - frame_begin;
    if (elapsed < frame_target) {
        DelayThread(static_cast<u32>(
            std::chrono::duration_cast<std::chrono::microseconds>(frame_target - elapsed).count()));
        return;
    }
    // SDL#14835 ("[PS2] Audio Stutter"): a loop that never yields starves
    // the audio-feeder thread, glitching playback. Upstream's fix: delay
    // briefly even over budget - common here, since a WAV decode alone can
    // take hundreds of ms on PS2's weak EE.
    DelayThread(1000);  // 1ms
}

// 15ms, not pace_frame()'s 1ms. Runs every frame, not gated on
// over-budget: endFrame() blocks on vsync (SDL_SetRenderVSync in
// SDL3Renderer::init(), which Ps2Renderer::init() calls as its base), so
// real frame time is the console's refresh period, not frame_target
// (16667us, ScreenLoop.h). PAL refreshes at 50Hz (~20ms/frame, always
// over frame_target); NTSC at ~59.94Hz (~16.68ms/frame, within 0.01ms of
// frame_target).
void platform_pre_vsync_yield() {
    DelayThread(15000);
}

}  // namespace openfear
