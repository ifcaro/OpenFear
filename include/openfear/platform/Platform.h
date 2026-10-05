#pragma once
#include "IRenderer.h"
#include "IAudio.h"
#include "IInput.h"
#include "IVideoPlayer.h"
#include "Cursor.h"
#include <chrono>
#include <memory>

namespace openfear {

class IAssetProvider;

// Bundles one concrete implementation of each subsystem interface,
// produced by whichever backend is selected at build time.
struct Platform {
    std::unique_ptr<IRenderer> renderer;
    std::unique_ptr<IAudio> audio;
    std::unique_ptr<IInput> input;
    std::unique_ptr<IVideoPlayer> video;
    std::unique_ptr<platform::Cursor> cursor;

    // Source of bytes for every asset load (bitmaps/WAVs/AVIs) across the
    // platform layer. Unset until attachAssetProvider() runs — renderer/
    // audio/input all come up fine without one (nothing loads an asset
    // that early); video is the one subsystem that needs it to exist at
    // all, so it's not constructed until attachAssetProvider() runs.
    IAssetProvider* asset_provider = nullptr;

    // Brings up window/renderer/cursor + audio + gamepad input. On PS2
    // this needs platform_bootstrap() to have already loaded the pad/audio
    // IOP modules — call after it.
    bool init(const char* window_title = "Atmosfear");

    // Wires a real asset provider into renderer/audio and constructs video
    // (deferred — SDL3VideoPlayer needs one at construction). Call once,
    // right after asset discovery succeeds. Split from init() so
    // input/audio/renderer can come up before the asset root is known.
    void attachAssetProvider(IAssetProvider* provider);

    void shutdown();

    // Polls input and updates the cursor. Returns false on quit.
    bool poll(float dt = 0.016f);  // default: ~60 FPS

    InputDevice activeDevice() const { return input->activeDevice(); }
};

// Implemented once per backend (e.g. src/platform/sdl3/PlatformFactory.cpp)
// — only the backend selected via OPENFEAR_PLATFORM_BACKEND is built. No
// asset provider yet — see Platform::attachAssetProvider().
std::unique_ptr<Platform> create_platform();

// Backend-specific bring-up (e.g. PS2: IOP reset, then filesystem driver
// modules and the CD/USB filesystem layers — see Ps2Boot.h's ResetIop()/
// LoadDriversAndFilesystems()). No-op on every other backend.
void platform_bootstrap();

// Called when AssetLocator::discover() fails, before giving up. Returns
// true to retry discovery (PS2: shows a message via the built-in bitmap
// font, blocks for a button press) or false to fall through to the
// generic console message + exit (desktop/Web, where a log is enough).
bool platform_prompt_retry_assets_missing(Platform& platform);

// Sleeps off whatever's left of a frame's budget (frame_target - elapsed
// since frame_begin), using whichever primitive this backend's threading
// model actually needs — plain sleep_for on most backends, but see the PS2
// and Emscripten implementations' own comments for why they differ. Call
// once per frame, after endFrame(). No-op if the frame already ran over
// budget on desktop/Web; PS2 differs here too, see its own comment.
void platform_pace_frame(std::chrono::steady_clock::time_point frame_begin,
                         std::chrono::microseconds frame_target);

// Yields once per frame, before endFrame()/vsync. No-op on every backend
// except PS2 — see PlatformFactory_ps2.cpp's own comment for why.
void platform_pre_vsync_yield();

// Logs real allocator heap state (arena/used/free/free-chunk-count) plus
// renderer.textureCount() under `tag`, for hunting heap fragmentation on a
// hardware playtest. No-op on every backend except PS2, where the EE heap
// doesn't compact — see PlatformFactory_bootstrap_ps2.cpp. Stderr only,
// read from the log after the run.
void platform_log_memory_stats(const char* tag, IRenderer& renderer);

// Native OS error dialog — works standalone, before any Platform/window
// exists. For fatal startup failures (e.g. assets not found) that would
// otherwise only print to a console the user never sees (the desktop build
// is a windowed, console-less executable).
void show_error_dialog(const char* title, const char* message);

}  // namespace openfear
