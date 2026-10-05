#pragma once

#include "openfear/platform/Platform.h"
#include <chrono>
#include <cstdio>

// Shared by every apps/openfear/*.cpp with a screen loop or a frame-paced
// wait (real flow, quickstarts, post-game, install) - paces frames and
// logs the same way everywhere instead of each call site repeating it.

#define LOG(...) std::fprintf(stderr, "[FLOW] " __VA_ARGS__)

namespace openfear {

inline constexpr auto kFrameTarget = std::chrono::microseconds(16667);  // ~60 Hz

// Returns false if the user quit (window closed / SDL_QUIT) mid-screen —
// callers MUST check this and unwind instead of falling through to the next
// screen with whatever state the current one was left in.
template <typename ScreenType> bool RunScreenLoop(ScreenType& screen, Platform& platform) {
    auto frame_start = std::chrono::steady_clock::now();
    while (true) {
        auto frame_begin = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(frame_begin - frame_start).count();
        frame_start = frame_begin;

        if (!platform.poll(dt)) return false;
        if (screen.update(*platform.input)) return true;
        platform.renderer->beginFrame();
        screen.render();
        platform_pre_vsync_yield();
        platform.renderer->endFrame();

        platform_pace_frame(frame_begin, kFrameTarget);
    }
}

// Keeps the caller's last drawn frame on screen until the user closes the
// window or max_seconds elapses - needed before platform.shutdown(),
// which destroys the window on desktop and would otherwise hide a
// just-drawn fatal message before anyone could read it.
inline void WaitForDismissOrTimeout(Platform& platform, float max_seconds = 15.0f) {
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        const auto frame_begin = std::chrono::steady_clock::now();
        if (std::chrono::duration<float>(frame_begin - start).count() >= max_seconds) return;
        if (!platform.poll(0.016f)) return;
        platform_pace_frame(frame_begin, kFrameTarget);
    }
}

}  // namespace openfear
