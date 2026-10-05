#pragma once
#include <cstddef>
#include <cstdint>

namespace openfear {

enum class MouseButton : uint8_t { Left = 0, Right = 1 };

struct MouseState {
    int16_t x, y;  // Cursor position in the game's 640x480 logical space —
                   // used for zone/click hit-tests against logical rects.
    // Real window pixel position, unconverted — only needed to draw the cursor
    // sprite directly onto the window (so it can reach letterbox/pillarbox
    // bars a logical-space draw would be clipped out of). Equal to x/y when
    // the window happens to be exactly 640x480.
    int16_t native_x, native_y;
    bool buttons[2];   // Left, Right — currently held
    bool clicked[2];   // True for one frame after button-down
    bool released[2];  // True for one frame after button-up
    bool moved;        // True if the mouse moved this frame
    bool in_window;    // True if the mouse is inside the window
};

enum class Key : uint8_t {
    Unknown = 0,
    Enter,
    Escape,
    Space,
    Left,
    Right,
    Up,
    Down,
    Function,  // Any F1-F12, treated as one logical key
    Count
};

struct KeyboardState {
    bool held[static_cast<size_t>(Key::Count)];     // Indexed by Key enum
    bool pressed[static_cast<size_t>(Key::Count)];  // True for one frame after key-down
};

// No triggers/right stick — not used by any current input mapping.
enum class GamepadButton : uint8_t {
    A = 0,
    B,
    X,
    Y,
    Start,
    Back,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Count
};

struct GamepadState {
    bool connected = false;
    float left_x = 0.0f, left_y = 0.0f;  // Left stick, -1..1, no deadzone applied
    int8_t dpad_x = 0, dpad_y = 0;       // Discrete D-pad, -1/0/1 per axis
    bool held[static_cast<size_t>(GamepadButton::Count)]{};
    bool pressed[static_cast<size_t>(
        GamepadButton::Count)]{};  // True for one frame after button-down
};

// Last input device that produced real activity (mouse move/click, or a
// gamepad axis/button past its own activity threshold). Touch is
// synthesized as mouse motion by the platform layer, so it counts as
// Mouse here.
enum class InputDevice : uint8_t { Mouse = 0, Gamepad = 1 };

// Abstract input backend. Concrete implementations live under
// src/platform/<backend>/.
class IInput {
public:
    virtual ~IInput() = default;

    virtual bool init() = 0;
    virtual void shutdown() = 0;

    // Call once per frame before querying state. Returns false when the
    // user requests quit.
    virtual bool poll() = 0;

    virtual const MouseState& mouse() const = 0;
    virtual const KeyboardState& keyboard() const = 0;
    virtual const GamepadState& gamepad() const = 0;
    virtual InputDevice activeDevice() const = 0;
};

}  // namespace openfear
