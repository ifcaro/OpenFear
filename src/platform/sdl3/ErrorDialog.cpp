#include "openfear/platform/Platform.h"
#include <SDL3/SDL.h>

namespace openfear {

void show_error_dialog(const char* title, const char* message) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, nullptr);
}

}  // namespace openfear
