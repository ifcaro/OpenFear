#pragma once

#include "openfear/platform/IInput.h"

namespace openfear::testing {

// Scriptable IInput: a test sets mouse/gamepad state directly, then calls
// BoardGameScreen::update(fake_input) once per synthetic "frame".
class FakeInput : public IInput {
public:
    bool init() override { return true; }
    void shutdown() override {}
    bool poll() override {
        if (should_quit_) return false;
        if (polls_remaining_ >= 0) {
            if (polls_remaining_ == 0) return false;
            --polls_remaining_;
        }
        return true;
    }

    // Test control: makes poll() return false from here on, the same signal
    // a real window-close sends — for cleanly ending an open-ended blocking
    // loop (e.g. RunPostGameSequence's credits roll) instead of letting a
    // test hang or sleep through many real seconds of slide timers.
    void SetQuit(bool quit = true) { should_quit_ = quit; }

    // Lets exactly `n` more poll() calls succeed, then every call after
    // that returns false — for letting a click resolve (1 successful poll)
    // before quitting the NEXT open-ended loop (e.g. credits) a test
    // doesn't want to sit through in real time.
    void AllowPolls(int n) { polls_remaining_ = n; }

    const MouseState& mouse() const override { return mouse_; }
    const KeyboardState& keyboard() const override { return keyboard_; }
    const GamepadState& gamepad() const override { return gamepad_; }
    InputDevice activeDevice() const override { return active_device_; }

    // Sets mouse position and marks left-click as pressed-this-frame.
    void ClickAt(int16_t x, int16_t y, int button = 0) {
        mouse_.x = mouse_.native_x = x;
        mouse_.y = mouse_.native_y = y;
        mouse_.clicked[button] = true;
        mouse_.buttons[button] = true;
    }

    MouseState mouse_{};
    KeyboardState keyboard_{};
    GamepadState gamepad_{};
    InputDevice active_device_ = InputDevice::Mouse;

private:
    bool should_quit_ = false;
    int polls_remaining_ = -1;  // -1 = unlimited
};

}  // namespace openfear::testing
