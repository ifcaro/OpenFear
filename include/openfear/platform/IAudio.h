#pragma once
#include <cstdint>
#include <functional>
#include <string_view>

namespace openfear {

class IAssetProvider;

enum class AudioFlags : uint32_t {
    None = 0,
    Async = 1 << 0,  // play without blocking
    Loop = 1 << 1,   // loop until stopped
};
inline AudioFlags operator|(AudioFlags a, AudioFlags b) {
    return static_cast<AudioFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool hasFlag(AudioFlags flags, AudioFlags flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

using SoundHandle = uint32_t;

// Abstract audio backend. Concrete implementations live under
// src/platform/<backend>/.
class IAudio {
public:
    virtual ~IAudio() = default;

    virtual bool init() = 0;
    virtual void shutdown() = 0;

    // Sample rate this backend actually mixes/outputs at (device format).
    // IVideoPlayer implementations need this to convert AVI audio (whatever
    // rate the file itself declares) to the SAME rate before mixing it in -
    // a mismatch here plays video audio at the wrong speed/pitch.
    virtual int sampleRate() const = 0;

    // Source of file bytes for preloadWave()/playWave(). Must be set
    // before any load call.
    virtual void setAssetProvider(IAssetProvider* provider) {}

    // Blocking: reads the setup-phase WAVs up front into an in-memory
    // raw-bytes cache (still decoded fresh on each later play), so
    // preloadWave()/playWave() skip the disc read for them. No-op on
    // backends that don't override it. Call once after init(), before
    // showing anything interactive — wrap it in a loading screen.
    // on_progress (optional): called after each file as (done, total), on
    // backends that override this to actually do real work; no-op
    // backends never call it — never assume it fires.
    virtual void preloadSetupPhase(std::function<void(int, int)> on_progress = nullptr) {
        (void)on_progress;
    }

    // Frees the cache built by preloadSetupPhase(). Call once the setup
    // phase (screens 1-8) is over and none of those screens will be shown
    // again this run, to give the memory back before the board game loads
    // its own assets. No-op on backends that don't override
    // preloadSetupPhase().
    virtual void releaseSetupPhaseCache() {}

    // Loads a WAV without playing it. Returns 0 on failure.
    virtual SoundHandle preloadWave(std::string_view path) = 0;

    // Plays a previously preloaded sound. Returns the handle on success, 0
    // on failure.
    virtual SoundHandle playLoaded(SoundHandle handle, AudioFlags flags) = 0;

    // Loads and plays a WAV in one call. Returns 0 on failure.
    virtual SoundHandle playWave(std::string_view path, AudioFlags flags) = 0;

    // Stops a specific sound, or all sounds if handle is 0.
    virtual void stop(SoundHandle handle = 0) = 0;

    // Stops (if playing) and frees the decoded audio data, unlike stop()
    // which keeps it resident for cheap replay. No-op if already unloaded.
    virtual void unload(SoundHandle handle) = 0;

    virtual bool isPlaying(SoundHandle handle) = 0;

    // True once the handle's decode has actually finished (whether or not
    // it's playing) - false while still pending. Unlike isPlaying() (which
    // also counts pending as "not done yet", for "wait until this finishes"
    // polling loops), this is for the opposite case: a caller that wants to
    // start something else (a video) in the same frame the sound becomes
    // playable, instead of leaving decode latency exposed as a visible
    // desync between the two.
    virtual bool isReady(SoundHandle handle) = 0;

    virtual void setVolume(SoundHandle handle, float volume) = 0;  // 0.0-1.0
};

}  // namespace openfear
