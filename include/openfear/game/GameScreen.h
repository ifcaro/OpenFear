#pragma once
#include "../AssetLocator.h"
#include "../assets/AssetManager.h"
#include "../platform/IRenderer.h"
#include "../platform/IAudio.h"
#include "../platform/IInput.h"
#include <cstdio>

namespace openfear::game {

// Base class for all setup/game screens.
//
// Centralizes patterns every screen repeats, including:
//   - Constructor injection of assets/renderer/audio.
//   - Click detection: clicked(input) -> m.released[0]/[1].
//   - WAV playback: playWav("W2\\AT_0205.WAV") takes a bare virtual path
//     and calls audio_.playWave(); stopSound() delegates to IAudio.
//
// Usage:
//   class MyScreen : public GameScreen {
//   public:
//       MyScreen(assets::AssetManager& a, IRenderer& r, IAudio& au)
//           : GameScreen(a, r, au) {}
//       bool update(const IInput& input) {
//           if (!clicked(input)) return false; ...
//       }
//   };
class GameScreen {
protected:
    GameScreen(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio)
        : assets_(assets), renderer_(renderer), audio_(audio) {}

    void setScreenName(const char* name) { screen_name_ = name; }

    // Commits on button press (WM_LBUTTONDOWN/WM_RBUTTONDOWN), not release —
    // matches the original's shared zone-dispatch engine.
    static bool clicked(const IInput& input) {
        return input.mouse().clicked[0] || input.mouse().clicked[1];
    }

    // Convert a raw RngNext() roll (0..0x7FFF) into a uniform index in
    // [0, count) — same scaling as the real ASM's (roll * count) / 0x8000.
    static int RollIndex(uint16_t raw_roll, int count) {
        return static_cast<int>(raw_roll * static_cast<unsigned>(count) / 0x8000u);
    }

    // Point-in-rect test (x1<=x<x2, y1<=y<y2) — exclusive bottom/right,
    // matching the real engine's GDI-based zone hit-test (CreatePolygonRgn +
    // PtInRegion scan-conversion excludes bottom/right edges, unlike a plain
    // rect-inclusive check). Templated: both assets::RectEntry and the
    // platform's own Rect share the x1/y1/x2/y2 layout but are distinct,
    // unrelated types.
    template <typename RectT> static bool PointInRect(int x, int y, const RectT& r) {
        return x >= r.x1 && x < r.x2 && y >= r.y1 && y < r.y2;
    }

    // Refreshes gamepad_mode_ from the device active this frame. Call once
    // per frame, in update() — render() (a separate call) reads the member
    // to decide whether to draw gamepad focus UI.
    bool RefreshGamepadMode(const IInput& input) {
        gamepad_mode_ = (input.activeDevice() == InputDevice::Gamepad);
        return gamepad_mode_;
    }

    // True if any gamepad button is pressed this frame — the "any button
    // skips/commits" idiom used on fullscreen video/portrait states that
    // have no zone/focus concept (so there's nothing more specific to check).
    static bool AnyPressed(const GamepadState& gp) {
        for (bool b : gp.pressed) {
            if (b) return true;
        }
        return false;
    }

    // Play a WAV file by path relative to the asset root - a bare virtual
    // path. The asset root is only ever known to whichever IAssetProvider
    // the audio backend was given (setAssetProvider(), wired once at
    // platform bootstrap); gameplay code doesn't need to know or care what
    // the underlying storage actually is.
    // rel may use either slash: "W2\\AT_0205.WAV" or "W2/AT_0205.WAV".
    // Returns 0 on playback failure.
    SoundHandle playWav(const std::string& rel, AudioFlags flags = AudioFlags::Async) {
        std::fprintf(stderr, "[WAV] %s (%s)\n", rel.c_str(), screen_name_);
        auto h = audio_.playWave(rel, flags);
        if (h == 0) std::fprintf(stderr, "[WAV]   ^ FAILED (file not found?)\n");
        return h;
    }

    // Preload a WAV file, for cases where it must be loaded before playback
    // (e.g. a click sound played from an input-poll callback).
    SoundHandle preloadWav(const std::string& rel) {
        std::fprintf(stderr, "[WAV-PRELOAD] %s (%s)\n", rel.c_str(), screen_name_);
        auto h = audio_.preloadWave(rel);
        if (h == 0) std::fprintf(stderr, "[WAV-PRELOAD]   ^ FAILED (file not found?)\n");
        return h;
    }

    void stopSound(SoundHandle handle) { audio_.stop(handle); }

    // Destroy tex (if any) via renderer_ and reset the handle to 0. Every
    // screen repeats this guard per owned texture on unload/free.
    void ReleaseTexture(TextureHandle& tex) {
        if (tex) {
            renderer_.destroyTexture(tex);
            tex = 0;
        }
    }

    // Unload handle (if any) via audio_ and reset it to 0. Same guard
    // repeated per owned sound on unload/free.
    void ReleaseAudio(SoundHandle& handle) {
        if (handle) {
            audio_.unload(handle);
            handle = 0;
        }
    }

    // Loads `count` rect entries from assets_.rect_file(lookup_name) into
    // dst; logs fail_message and returns false if missing/wrong size.
    // Shared by every load()-time hard-dependency rect file (click/focus
    // zones that would otherwise go permanently unresponsive if missing).
    bool LoadRectZoneOrFail(const char* lookup_name, int count, assets::RectEntry* dst,
                            const char* fail_message) {
        auto rects = assets_.rect_file(lookup_name);
        if (rects && static_cast<int>(rects->size()) == count) {
            for (int i = 0; i < count; ++i) dst[i] = (*rects)[i];
            return true;
        }
        std::fprintf(stderr, "[LoadRectZone] ERROR: %s\n", fail_message);
        return false;
    }

    assets::AssetManager& assets_;
    IRenderer& renderer_;
    IAudio& audio_;
    const char* screen_name_ = "?";
    bool gamepad_mode_ = false;  // see RefreshGamepadMode()
};

}  // namespace openfear::game
