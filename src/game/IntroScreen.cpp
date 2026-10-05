#include "openfear/game/IntroScreen.h"
#include "openfear/assets/AssetManager.h"
#include "openfear/platform/IRenderer.h"
#include "openfear/platform/IInput.h"
#include <cstdio>

namespace openfear::game {

bool IntroScreen::load(const AssetPaths&) {
    setScreenName("Intro");

    const auto intro_path = AssetPaths::avi("AV2", "INTRO");
    if (video_.open(intro_path.string())) {
        // Native size (480x98), centered — not scaled to fill the screen.
        video_.setRenderRect(80, 191, 480, 98);
        video_.play(true);
        video_loaded_ = true;
        return true;
    }

    // Missing AVI is non-fatal — continues straight to the next screen
    // instead of failing the whole launch over one optional clip.
    std::fprintf(stderr, "[Intro] WARNING: INTRO.AVI not found, proceeding without video\n");
    return true;
}

bool IntroScreen::update(const IInput& input) {
    if (!video_loaded_) {
        return true;
    }

    if (video_.status() != VideoStatus::Playing) {
        return true;
    }

    // Skip on button press (either mouse button, or any gamepad button —
    // no zone/focus concept on a fullscreen video, so any press is the
    // natural equivalent of a click here).
    const bool gp_pressed =
        input.activeDevice() == InputDevice::Gamepad && AnyPressed(input.gamepad());
    if (input.mouse().clicked[0] || input.mouse().clicked[1] || gp_pressed) {
        return true;
    }

    return false;
}

void IntroScreen::render() {
    if (video_loaded_) {
        video_.renderFrame();
    }
}

void IntroScreen::unload() {
    if (video_loaded_) {
        video_.close();
        video_loaded_ = false;
    }
}

}  // namespace openfear::game
