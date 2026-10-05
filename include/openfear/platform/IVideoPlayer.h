#pragma once
#include <cstdint>
#include <string_view>

namespace openfear {

enum class VideoStatus {
    Idle,     // Not open — no frame to draw
    Playing,  // Active playback
    Paused,   // pause() was called — frame frozen, resume() continues it
    Stopped,  // Reached the end, or stop() was called — last frame still
              // drawable until close()
};

// Abstract AVI video playback backend. Concrete implementations live
// under src/platform/<backend>/.
class IVideoPlayer {
public:
    virtual ~IVideoPlayer() = default;

    virtual bool init() = 0;
    virtual void shutdown() = 0;

    // Opens an AVI file. Returns false on failure.
    virtual bool open(std::string_view path) = 0;

    // Plays from the beginning. fullscreen_overlay renders over the
    // entire 640x480 surface.
    virtual void play(bool fullscreen_overlay = true) = 0;

    // Resumes from Paused without resetting position.
    virtual void resume() = 0;

    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void close() = 0;

    virtual VideoStatus status() const = 0;

    // Renders the current frame into the active render target. Call once
    // per game frame. Only advances to a new decoded frame while status()
    // == Playing; in any other state it just keeps redrawing whatever
    // frame is already there.
    virtual void renderFrame() = 0;

    // Seeks to a 0-indexed frame number without changing playing status.
    virtual void seekFrame(int frame_number) = 0;

    // Sets the destination rect for rendering (default: centered).
    virtual void setRenderRect(int x, int y, int width, int height) = 0;
};

}  // namespace openfear
