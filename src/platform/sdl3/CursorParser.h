#pragma once
#include <cstdint>
#include <SDL3/SDL.h>

namespace openfear::sdl3 {

// Converts a raw Win16 RT_CURSOR resource (as returned by
// ExeResourceParser) into an SDL_Surface.
class CursorParser {
public:
    // Returns nullptr on malformed/unsupported cursor data.
    static SDL_Surface* ParseWin16Cursor(const uint8_t* data, uint32_t size);

private:
    static bool ExtractCursorDimensions(const uint8_t* data, uint32_t size, int& out_width,
                                        int& out_height);
};

}  // namespace openfear::sdl3
