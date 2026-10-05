# Script-mode CMake file (run via `cmake -P`, not include()'d) - checks that
# the real GPLv3 ps2_drivers package never ends up in a PS2 link command.
# See OpenFearPlatform.cmake's openfear_package_ps2_elf() for why this
# exists and why a duplicate-symbol link error can't be relied on to catch
# this on its own (static-archive linking only pulls object files that are
# actually needed - if every symbol ps2_drivers would provide is already
# resolved by openfear_platform_sdl3.a earlier on the link line, the linker
# never touches ps2_drivers.a's contents at all, even if "-lps2_drivers"
# is sitting right there in the command). Pure CMake script, no shell/bash
# dependency - PS2SDK can exist on Windows without bash being present.
#
# Usage: cmake -DLINK_FILE=<path> -P CheckNoPs2Drivers.cmake

if(NOT DEFINED LINK_FILE)
    message(FATAL_ERROR "CheckNoPs2Drivers.cmake: LINK_FILE not set")
endif()

if(NOT EXISTS "${LINK_FILE}")
    # Matches the old bash version's behavior: a missing link.txt means
    # there's nothing to check, not a reason to fail the build.
    return()
endif()

file(READ "${LINK_FILE}" _link_contents)
string(FIND "${_link_contents}" "ps2_drivers" _pos)
if(NOT _pos EQUAL -1)
    message(FATAL_ERROR
        "ps2_drivers (GPLv3) leaked into the link (${LINK_FILE}) - see "
        "OpenFearPlatform.cmake's openfear_package_ps2_elf() and root "
        "CMakeLists.txt's SDL3 FetchContent_Declare comment.")
endif()
