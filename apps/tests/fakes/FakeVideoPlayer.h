#pragma once

#include "openfear/platform/IVideoPlayer.h"
#include <string>
#include <vector>

namespace openfear::testing {

// Headless IVideoPlayer: open() always succeeds (records the path) so
// BoardGameScreen::load()'s hard FULLMAP.AVI dependency doesn't need a real
// AVI file. status() defaults to Stopped so "wait until not Playing" loops
// never hang a test.
class FakeVideoPlayer : public IVideoPlayer {
public:
    bool init() override { return true; }
    void shutdown() override {}

    bool open(std::string_view path) override {
        opened_.emplace_back(path);
        status_ = VideoStatus::Stopped;
        return true;
    }
    // Finishes immediately (Stopped, not Playing) - no test currently needs
    // to observe a mid-playback state.
    void play(bool) override { status_ = VideoStatus::Stopped; }
    void resume() override { status_ = VideoStatus::Playing; }
    void pause() override { status_ = VideoStatus::Paused; }
    void stop() override { status_ = VideoStatus::Stopped; }
    void close() override { status_ = VideoStatus::Idle; }
    void SetPlaying() { status_ = VideoStatus::Playing; }
    void FinishPlaying() { status_ = VideoStatus::Stopped; }

    VideoStatus status() const override { return status_; }
    void renderFrame() override { ++render_calls_; }
    void seekFrame(int) override {}
    void setRenderRect(int, int, int, int) override {}

    std::vector<std::string> opened_;
    int render_calls_ = 0;

private:
    VideoStatus status_ = VideoStatus::Idle;
};

}  // namespace openfear::testing
