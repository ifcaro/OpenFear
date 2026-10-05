# Script-mode CMake file (run via `cmake -P`, not include()'d) - removes the
# literal word "ps2_drivers" from SDL3's own CMakeLists.txt right after
# fetching it. See root CMakeLists.txt's SDL3 FetchContent_Declare comment
# for why. Pure CMake script, no sed/bash dependency - PS2SDK can exist on
# Windows without either being present. Runs with cwd = SDL3's source dir
# (FetchContent's PATCH_COMMAND convention), so "CMakeLists.txt" resolves
# there directly.

set(_file "CMakeLists.txt")
file(READ "${_file}" _contents)
string(REPLACE "ps2_drivers" "" _contents "${_contents}")
file(WRITE "${_file}" "${_contents}")
