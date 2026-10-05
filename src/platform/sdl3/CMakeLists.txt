# openfear_link_sdl3()/openfear_embed_binary_file() come from
# cmake/OpenFearPlatform.cmake; openfear_embed_ps2_irx_modules() comes from
# cmake/OpenFearPS2.cmake (both included once by the root CMakeLists.txt).

add_library(openfear_platform_sdl3 STATIC
    ../BitmapFont.cpp
    ../Cursor.cpp
    CursorParser.cpp
    PlatformFactory.cpp
    SDL3Audio.cpp
    SDL3Input.cpp
    SDL3Renderer.cpp
    SDL3VideoPlayer.cpp
)
target_include_directories(openfear_platform_sdl3 PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(openfear_platform_sdl3 PUBLIC
    openfear_game
    cinepak
    indeo3
)
openfear_link_sdl3(openfear_platform_sdl3)

# Basic ASCII bitmap font (BitmapFont.h) — OpenFear's own asset, not part
# of the CD data, embedded the same way as data/fonts/digitfont.bmp.
openfear_embed_binary_file(openfear_platform_sdl3
    "${PROJECT_SOURCE_DIR}/data/fonts/basicfont.bmp"
    g_basicfont_bmp
)

if(EMSCRIPTEN)
    target_sources(openfear_platform_sdl3 PRIVATE
        PlatformFactory_bootstrap_generic.cpp PlatformFactory_web.cpp)
elseif(DEFINED ENV{PS2SDK})
    target_sources(openfear_platform_sdl3 PRIVATE
        Ps2Boot.cpp Ps2Audio.cpp Ps2Renderer.cpp
        PlatformFactory_bootstrap_ps2.cpp PlatformFactory_ps2.cpp)
    openfear_embed_ps2_irx_modules(openfear_platform_sdl3)
else()
    target_sources(openfear_platform_sdl3 PRIVATE
        ErrorDialog.cpp PlatformFactory_bootstrap_generic.cpp PlatformFactory_generic.cpp)
endif()

# Game/app code links ONLY against this alias, never a backend-specific
# target name — swapping OPENFEAR_PLATFORM_BACKEND to a future non-SDL3
# backend never requires touching game code.
add_library(openfear_platform ALIAS openfear_platform_sdl3)
