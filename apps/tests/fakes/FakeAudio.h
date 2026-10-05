#pragma once

#include "openfear/platform/IAudio.h"
#include <string>
#include <vector>

namespace openfear::testing {

// Headless IAudio: never opens a real audio device. Records every
// play/stop call so tests can assert which WAV the game requested, and
// (critically) can assert stop() was never called with handle==0 by
// accident — stop(0) halts every channel in the real backend, see
// PostGameSequence.cpp's guard comments for the bug class this catches.
class FakeAudio : public IAudio {
public:
    bool init() override { return true; }
    void shutdown() override {}
    int sampleRate() const override { return 44100; }

    SoundHandle preloadWave(std::string_view) override {
        if (fail_all_) return 0;
        return next_handle_++;
    }

    SoundHandle playLoaded(SoundHandle handle, AudioFlags) override { return handle; }

    SoundHandle playWave(std::string_view path, AudioFlags) override {
        played_.emplace_back(path);
        if (fail_all_) return 0;
        return next_handle_++;
    }

    void stop(SoundHandle handle) override { stopped_.push_back(handle); }
    void unload(SoundHandle) override {}
    // Sounds finish immediately unless a test explicitly holds a handle open.
    bool isPlaying(SoundHandle handle) override { return handle != 0 && handle == playing_handle_; }
    // No real async decode in this fake - anything with a valid handle is
    // "ready" the instant it's returned.
    bool isReady(SoundHandle) override { return true; }
    void setVolume(SoundHandle, float) override {}

    void SetPlaying(SoundHandle handle) { playing_handle_ = handle; }
    void FinishPlaying(SoundHandle handle) {
        if (playing_handle_ == handle) playing_handle_ = 0;
    }

    // Test control: make every preload/play call "fail" (handle 0) — for
    // regression tests where the exact RNG-picked path doesn't matter.
    void FailAllWaves() { fail_all_ = true; }

    std::vector<std::string> played_;
    std::vector<SoundHandle> stopped_;

private:
    SoundHandle next_handle_ = 1;
    SoundHandle playing_handle_ = 0;
    bool fail_all_ = false;
};

}  // namespace openfear::testing
