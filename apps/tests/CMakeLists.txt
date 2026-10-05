# Headless logic tests target the dev machine (Windows/Linux/Mac), never run
# on PS2 hardware/PCSX2 - skip this whole directory on PS2 cross-builds
# rather than shipping an ELF nobody runs.
if(DEFINED ENV{PS2SDK})
    return()
endif()

add_executable(openfear-tests
    test_main.cpp
    test_mapfile.cpp
    test_playerinit.cpp
    test_boardgameformulas.cpp
    test_boardgamescreen_headless.cpp
    test_blackjackformulas.cpp
    test_postgamesequence_headless.cpp
    test_nativeassetprovider.cpp
    ${PROJECT_SOURCE_DIR}/apps/openfear/PostGameSequence.cpp
)

target_include_directories(openfear-tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/apps/openfear
)

target_link_libraries(openfear-tests PRIVATE
    openfear_game
    openfear_assets
    # Platform::poll()/init()/shutdown() (Platform.h's non-virtual methods)
    # are implemented once per backend in PlatformFactory.cpp - needed to
    # link RunPostGameSequence's Platform* usage. Tests never call init(),
    # so the real SDL3 device is never touched.
    openfear_platform
)

set_target_properties(openfear-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

enable_testing()
add_test(
    NAME OpenFearUnitTests
    COMMAND openfear-tests
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
