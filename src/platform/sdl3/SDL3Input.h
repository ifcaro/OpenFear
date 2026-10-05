#pragma once
#include "openfear/platform/IInput.h"
#include "SDL3Renderer.h"
#include <SDL3/SDL.h>

namespace openfear::sdl3 {

class SDL3Input final : public IInput {
public:
    ~SDL3Input() override = default;

    bool init() override;
    void shutdown() override;
    bool poll() override;

    // SDL mouse coordinates are real window pixels; every position-
    // dependent game read assumes the fixed 640x480 logical space instead.
    // Converting needs the live SDL3Renderer's getLetterboxRect(). Call
    // setRenderer() once, right after both renderer->init() and
    // input->init() have run.
    void setRenderer(SDL3Renderer* renderer) { renderer_ = renderer; }

    const MouseState& mouse() const override { return mouse_; }
    const KeyboardState& keyboard() const override { return kb_; }
    const GamepadState& gamepad() const override { return gp_; }
    InputDevice activeDevice() const override { return active_device_; }

private:
    MouseState mouse_{};
    KeyboardState kb_{};
    GamepadState gp_{};
    // Defaults to Gamepad, not Mouse: mouse_.in_window starts true (window
    // has focus on init), so a Mouse default would show the cursor from
    // frame 0 with zero actual input. Flips to Mouse on the first real
    // mouse motion/click, so mouse play is unaffected.
    InputDevice active_device_ = InputDevice::Gamepad;
    SDL_Gamepad* controller_ = nullptr;
    bool quit_ = false;
    SDL3Renderer* renderer_ = nullptr;  // for window->logical conversion, see setRenderer()

    static Key SdlKeycodeToKey(SDL_Keycode kc);
    static bool SdlButtonToGamepadButton(SDL_GamepadButton b, GamepadButton& out);
    void OpenFirstController();
    void UpdateDpadFromHeld();
    // Converts real window pixels into the fixed 640x480 game-logical
    // space, using the same letterbox rect SDL3Renderer::endFrame() blits
    // game content into.
    void WindowToLogical(int native_x, int native_y, int& logical_x, int& logical_y) const;
};

}  // namespace openfear::sdl3
