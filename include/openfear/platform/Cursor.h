#pragma once

#include <cstdint>

namespace openfear::platform {

// Virtual cursor shared across mouse/gamepad-focus input (touch arrives as
// synthesized mouse motion, see the backend), so game code reads one
// logical position regardless of input device. Every screen's gamepad UX
// is discrete D-pad focus navigation (FocusIndicator); SetGamepadFocus()
// repositions this same cursor over the focused element's center each
// frame — the 2 continuous-control screens (ChooseLevelScreen,
// DuelAttackGauge) never call it, they use their own local stick reader
// instead.
class Cursor {
public:
    struct CursorState {
        int32_t x = 0;  // 640x480 logical screen space
        int32_t y = 0;
        // False by default: stays hidden until Platform::poll() confirms a
        // real mouse is active, so no stray crosshair shows before that.
        bool is_visible = false;
        // True (default): drawn in logical space like any game sprite —
        // covers gamepad focus. False: a real mouse event drove this
        // update, so native_x/native_y hold the raw window pixel position,
        // letting the renderer reach letterbox/pillarbox bars directly.
        bool in_game_area = true;
        int32_t native_x = 0;
        int32_t native_y = 0;
    };

    Cursor();
    ~Cursor() = default;

    void Initialize(int32_t screen_width, int32_t screen_height);

    // Call only on a frame with a real mouse-position event; the caller
    // gates that (see Platform::poll()). mouse_native_x/y: real
    // window-pixel mouse position, see CursorState::native_x.
    void Update(int32_t mouse_x, int32_t mouse_y, int32_t mouse_native_x = 0,
                int32_t mouse_native_y = 0);

    const CursorState& GetState() const { return state_; }
    CursorState& GetState() { return state_; }

    int32_t GetX() const { return state_.x; }
    int32_t GetY() const { return state_.y; }

    void SetPosition(int32_t x, int32_t y);

    // Repositions the cursor over a gamepad-focused element's center, forces
    // the logical-space draw path, and makes it visible. Call once per frame
    // while a focus target is active; stop calling it and Platform::poll()'s
    // visibility gate hides it again next frame.
    void SetGamepadFocus(int32_t x, int32_t y) {
        state_.x = x;
        state_.y = y;
        state_.in_game_area = true;
        state_.is_visible = true;
    }

    void SetVisible(bool visible) { state_.is_visible = visible; }
    bool IsVisible() const { return state_.is_visible; }

private:
    CursorState state_;
};

}  // namespace openfear::platform
