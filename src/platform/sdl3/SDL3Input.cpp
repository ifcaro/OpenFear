#include "SDL3Input.h"
#include <cstring>
#include <cmath>
#include <cstdio>

namespace openfear::sdl3 {

// A gamepad axis must move past this (of 1.0 full deflection) before it
// counts as real gamepad use for active-device switching. Separate from
// any in-game stick deadzone - this only gates mouse<->gamepad mode
// switching, not cursor or gauge movement.
constexpr float kGamepadActivityThreshold = 0.3f;

bool SDL3Input::init() {
    if (!SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "[input] SDL_Init FAILED: %s\n", SDL_GetError());
        return false;
    }
    mouse_.in_window = true;  // window has focus on init
    OpenFirstController();
    return true;
}

void SDL3Input::shutdown() {
    if (controller_) {
        SDL_CloseGamepad(controller_);
        controller_ = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD);
}

void SDL3Input::OpenFirstController() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids) {
        for (int i = 0; i < count; ++i) {
            controller_ = SDL_OpenGamepad(ids[i]);
            gp_.connected = (controller_ != nullptr);
            if (controller_) break;
        }
        SDL_free(ids);
    }
    if (!controller_) std::fprintf(stderr, "[input] no gamepad found\n");
}

void SDL3Input::WindowToLogical(int native_x, int native_y, int& logical_x, int& logical_y) const {
    // Manual inverse of SDL3Renderer::getLetterboxRect() - the same rect
    // the renderer actually blits game_target_ into, so this can never
    // disagree with what's on screen.
    const SDL_Rect r = renderer_->getLetterboxRect();
    if (r.w <= 0 || r.h <= 0) {
        logical_x = native_x;
        logical_y = native_y;
        return;
    }
    logical_x = (native_x - r.x) * 640 / r.w;
    logical_y = (native_y - r.y) * 480 / r.h;
}

void SDL3Input::UpdateDpadFromHeld() {
    gp_.dpad_x = 0;
    gp_.dpad_y = 0;
    if (gp_.held[static_cast<size_t>(GamepadButton::DpadLeft)]) gp_.dpad_x = -1;
    if (gp_.held[static_cast<size_t>(GamepadButton::DpadRight)]) gp_.dpad_x = 1;
    if (gp_.held[static_cast<size_t>(GamepadButton::DpadUp)]) gp_.dpad_y = -1;
    if (gp_.held[static_cast<size_t>(GamepadButton::DpadDown)]) gp_.dpad_y = 1;
}

bool SDL3Input::poll() {
    std::memset(mouse_.clicked, 0, sizeof(mouse_.clicked));
    std::memset(mouse_.released, 0, sizeof(mouse_.released));
    std::memset(kb_.pressed, 0, sizeof(kb_.pressed));
    std::memset(gp_.pressed, 0, sizeof(gp_.pressed));
    mouse_.moved = false;

    // SDL_GetMouseState returns real window pixels (float in SDL3) -
    // convert to the fixed 640x480 logical space using the live
    // SDL3Renderer's getLetterboxRect() (set via setRenderer()).
    float mouse_xf = 0, mouse_yf = 0;
    SDL_GetMouseState(&mouse_xf, &mouse_yf);
    int mouse_x = static_cast<int>(mouse_xf), mouse_y = static_cast<int>(mouse_yf);
    mouse_.native_x = static_cast<int16_t>(mouse_x);
    mouse_.native_y = static_cast<int16_t>(mouse_y);
    if (renderer_) {
        WindowToLogical(mouse_x, mouse_y, mouse_x, mouse_y);
    }
    mouse_.x = static_cast<int16_t>(mouse_x);
    mouse_.y = static_cast<int16_t>(mouse_y);

    SDL_Window* focused = SDL_GetMouseFocus();
    mouse_.in_window = (focused != nullptr);

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_EVENT_QUIT:
                quit_ = true;
                break;

            case SDL_EVENT_MOUSE_MOTION: {
                mouse_.native_x = static_cast<int16_t>(ev.motion.x);
                mouse_.native_y = static_cast<int16_t>(ev.motion.y);
                int mx = static_cast<int>(ev.motion.x), my = static_cast<int>(ev.motion.y);
                // Only convert in the render-target path. In the fallback path
                // (the only place SDL_SetRenderLogicalPresentation is active),
                // a still-open SDL issue (libsdl-org/SDL#1397) means event
                // coordinates may already arrive pre-transformed into logical
                // space - converting again would double-apply the scale.
                if (renderer_ && renderer_->usesRenderTarget()) {
                    WindowToLogical(mx, my, mx, my);
                }
                mouse_.x = static_cast<int16_t>(mx);
                mouse_.y = static_cast<int16_t>(my);
                mouse_.moved = true;
                active_device_ = InputDevice::Mouse;
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                int idx = (ev.button.button == SDL_BUTTON_LEFT) ? 0 : 1;
                bool down = (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                mouse_.native_x = static_cast<int16_t>(ev.button.x);
                mouse_.native_y = static_cast<int16_t>(ev.button.y);
                int bx = static_cast<int>(ev.button.x), by = static_cast<int>(ev.button.y);
                // A still-open SDL issue (libsdl-org/SDL#1397) means event
                // coordinates may already arrive pre-transformed in the
                // fallback path (the only place
                // SDL_SetRenderLogicalPresentation is active) - converting
                // again would double-apply the scale.
                if (renderer_ && renderer_->usesRenderTarget()) {
                    WindowToLogical(bx, by, bx, by);
                }
                mouse_.x = static_cast<int16_t>(bx);
                mouse_.y = static_cast<int16_t>(by);
                mouse_.moved = true;
                active_device_ = InputDevice::Mouse;
                mouse_.in_window =
                    (mouse_.x >= 0 && mouse_.x < 640 && mouse_.y >= 0 && mouse_.y < 480);
                mouse_.buttons[idx] = down;
                if (down) {
                    mouse_.clicked[idx] = true;
                } else {
                    mouse_.released[idx] = true;
                }
                break;
            }

            case SDL_EVENT_KEY_DOWN: {
                // Fullscreen toggle - port-only UX, no ASM equivalent. Alt+Enter
                // only (the dominant PC convention), deliberately not F11 (that
                // would cost one of the real F1-F24 Flight-key range, all bound
                // undifferentiated - see SdlKeycodeToKey below).
                if (ev.key.key == SDLK_RETURN && (ev.key.mod & (SDL_KMOD_LALT | SDL_KMOD_RALT))) {
                    if (renderer_) {
                        SDL_Window* win = renderer_->window();
                        const bool is_fullscreen =
                            (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
                        SDL_SetWindowFullscreen(win, !is_fullscreen);
                    }
                    break;
                }
                Key k = SdlKeycodeToKey(ev.key.key);
                if (k != Key::Unknown) {
                    kb_.held[static_cast<uint8_t>(k)] = true;
                    kb_.pressed[static_cast<uint8_t>(k)] = true;
                }
                break;
            }
            case SDL_EVENT_KEY_UP: {
                Key k = SdlKeycodeToKey(ev.key.key);
                if (k != Key::Unknown) kb_.held[static_cast<uint8_t>(k)] = false;
                break;
            }

            case SDL_EVENT_GAMEPAD_ADDED:
                if (!controller_) {
                    controller_ = SDL_OpenGamepad(ev.gdevice.which);
                    gp_.connected = (controller_ != nullptr);
                }
                break;

            case SDL_EVENT_GAMEPAD_REMOVED:
                if (controller_) {
                    SDL_Joystick* joy = SDL_GetGamepadJoystick(controller_);
                    if (SDL_GetJoystickID(joy) == ev.gdevice.which) {
                        SDL_CloseGamepad(controller_);
                        controller_ = nullptr;
                        gp_ = GamepadState{};
                        OpenFirstController();  // fall back to another pad, if any
                    }
                }
                break;

            case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
                float v = static_cast<float>(ev.gaxis.value) / 32767.0f;
                if (v > 1.0f) v = 1.0f;
                if (v < -1.0f) v = -1.0f;
                if (ev.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX)
                    gp_.left_x = v;
                else if (ev.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY)
                    gp_.left_y = v;
                if (std::fabs(v) > kGamepadActivityThreshold) active_device_ = InputDevice::Gamepad;
                break;
            }

            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP: {
                GamepadButton b;
                if (SdlButtonToGamepadButton(static_cast<SDL_GamepadButton>(ev.gbutton.button),
                                             b)) {
                    bool down = (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
                    size_t idx = static_cast<size_t>(b);
                    gp_.held[idx] = down;
                    if (down) {
                        gp_.pressed[idx] = true;
                        active_device_ = InputDevice::Gamepad;
                    }
                }
                break;
            }

            default:
                break;
        }
    }
    UpdateDpadFromHeld();
    return !quit_;
}

Key SDL3Input::SdlKeycodeToKey(SDL_Keycode kc) {
    switch (kc) {
        case SDLK_RETURN:
            return Key::Enter;
        case SDLK_ESCAPE:
            return Key::Escape;
        case SDLK_SPACE:
            return Key::Space;
        case SDLK_LEFT:
            return Key::Left;
        case SDLK_RIGHT:
            return Key::Right;
        case SDLK_UP:
            return Key::Up;
        case SDLK_DOWN:
            return Key::Down;
        // The original accepts the full VK_F1-VK_F24 range undifferentiated -
        // match the same range for fidelity, even though F1-F12 covers
        // virtually every real keyboard.
        case SDLK_F1:
        case SDLK_F2:
        case SDLK_F3:
        case SDLK_F4:
        case SDLK_F5:
        case SDLK_F6:
        case SDLK_F7:
        case SDLK_F8:
        case SDLK_F9:
        case SDLK_F10:
        case SDLK_F11:
        case SDLK_F12:
        case SDLK_F13:
        case SDLK_F14:
        case SDLK_F15:
        case SDLK_F16:
        case SDLK_F17:
        case SDLK_F18:
        case SDLK_F19:
        case SDLK_F20:
        case SDLK_F21:
        case SDLK_F22:
        case SDLK_F23:
        case SDLK_F24:
            return Key::Function;
        default:
            return Key::Unknown;
    }
}

bool SDL3Input::SdlButtonToGamepadButton(SDL_GamepadButton b, GamepadButton& out) {
    // SDL3 renamed the face buttons from Xbox-label names (A/B/X/Y) to
    // physical-position names (SOUTH/EAST/WEST/NORTH) - same Xbox-layout
    // mapping as before, just renamed.
    switch (b) {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            out = GamepadButton::A;
            return true;
        case SDL_GAMEPAD_BUTTON_EAST:
            out = GamepadButton::B;
            return true;
        case SDL_GAMEPAD_BUTTON_WEST:
            out = GamepadButton::X;
            return true;
        case SDL_GAMEPAD_BUTTON_NORTH:
            out = GamepadButton::Y;
            return true;
        case SDL_GAMEPAD_BUTTON_START:
            out = GamepadButton::Start;
            return true;
        case SDL_GAMEPAD_BUTTON_BACK:
            out = GamepadButton::Back;
            return true;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            out = GamepadButton::LeftShoulder;
            return true;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            out = GamepadButton::RightShoulder;
            return true;
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            out = GamepadButton::DpadUp;
            return true;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            out = GamepadButton::DpadDown;
            return true;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            out = GamepadButton::DpadLeft;
            return true;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            out = GamepadButton::DpadRight;
            return true;
        default:
            return false;
    }
}

}  // namespace openfear::sdl3
