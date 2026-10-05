#include "openfear/platform/Cursor.h"

namespace openfear::platform {

Cursor::Cursor() = default;

void Cursor::Initialize(int32_t screen_width, int32_t screen_height) {
    state_.x = screen_width / 2;
    state_.y = screen_height / 2;
    // Explicit reset: Initialize() can run again after something else set
    // is_visible to true, and CursorState's member default only applies at
    // construction, not on this later call.
    state_.is_visible = false;
}

void Cursor::Update(int32_t mouse_x, int32_t mouse_y, int32_t mouse_native_x,
                    int32_t mouse_native_y) {
    SetPosition(mouse_x, mouse_y);
    state_.in_game_area = false;
    state_.native_x = mouse_native_x;
    state_.native_y = mouse_native_y;
}

void Cursor::SetPosition(int32_t x, int32_t y) {
    // Deliberately unclamped: a real mouse can legitimately sit outside
    // [0,640)x[0,480) in a letterbox bar. Safe because no game logic reads
    // Cursor's position — screens read IInput's MouseState directly.
    state_.x = x;
    state_.y = y;
}

}  // namespace openfear::platform
