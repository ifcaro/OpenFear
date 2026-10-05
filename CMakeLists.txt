cmake_minimum_required(VERSION 3.20)
project(OpenFear VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

include_directories(include)

include(${PROJECT_SOURCE_DIR}/cmake/OpenFearPlatform.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/OpenFearPS2.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/OpenFearEmscripten.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/OpenFearFormat.cmake)

# Selects which concrete backend the openfear_platform alias (below) points
# to. Game/app code links only against openfear_platform — never a
# backend-specific target name — so adding a new backend never requires
# touching game code, only this option and its own src/platform/<name>/ dir.
set(OPENFEAR_PLATFORM_BACKEND "SDL3" CACHE STRING "Rendering/audio/input backend")
set_property(CACHE OPENFEAR_PLATFORM_BACKEND PROPERTY STRINGS SDL3)

# ---------------------------------------------------------------------------
# Third-party video decoders
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(libcinepak
    GIT_REPOSITORY https://github.com/ifcaro/libcinepak
    GIT_TAG        main)
FetchContent_MakeAvailable(libcinepak)

FetchContent_Declare(libindeo3
    GIT_REPOSITORY https://github.com/ifcaro/libindeo3
    GIT_TAG        main)
FetchContent_MakeAvailable(libindeo3)

# PS2 needs SDL3 built from source under the ps2dev.cmake toolchain (PS2SDK's
# ports tree only carries SDL2) - fetched the same way as libcinepak/
# libindeo3 above, added_subdirectory under the SAME toolchain already
# active for this whole configure, so it cross-compiles automatically with
# zero extra flags (confirmed matches the known-working manual build: just
# -DCMAKE_TOOLCHAIN_FILE=.../ps2dev.cmake, nothing else). Native/Emscripten
# get SDL3 from vcpkg/the Emscripten port instead (see
# openfear_link_sdl3() in OpenFearPlatform.cmake) - this fetch is PS2-only.
if(DEFINED ENV{PS2SDK})
    # SDL3's PS2 backend unconditionally links "ps2_drivers", the GPLv3
    # ps2sdk/ports package - not used here (Ps2Boot.cpp provides those 4
    # symbols against plain PS2SDK instead). PATCH_COMMAND strips that one
    # link line from SDL3's CMakeLists.txt right after fetching it.
    #
    # GIT_TAG is a pinned commit, not "main", so an unrelated upstream change
    # can't silently alter or break the PS2 build. This SHA includes the 4
    # PS2 fixes the project depends on: #16413/#16424 (audio PlayDevice
    # tolerance), #16425 (texel/pixel-center alignment), #16427 (blank both
    # framebuffers on init). Bump only after re-verifying PS2 still works.
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL
        GIT_TAG        ea7a2dabfd1ab6b0a5720568ffd207f5825e47f1
        PATCH_COMMAND ${CMAKE_COMMAND} -P "${PROJECT_SOURCE_DIR}/cmake/PatchRemovePs2Drivers.cmake")
    FetchContent_MakeAvailable(SDL3)
endif()

# ---------------------------------------------------------------------------
# Subsystems
# ---------------------------------------------------------------------------
add_subdirectory(src/assets)
add_subdirectory(src/game)

if(OPENFEAR_PLATFORM_BACKEND STREQUAL "SDL3")
    add_subdirectory(src/platform/sdl3)
else()
    message(FATAL_ERROR "Unknown OPENFEAR_PLATFORM_BACKEND: ${OPENFEAR_PLATFORM_BACKEND}")
endif()

add_subdirectory(apps/openfear)
add_subdirectory(apps/tests)

# apps/tools (CD installer, AVI player, frame-diff) gets added here once
# ported — not part of the base skeleton yet.
