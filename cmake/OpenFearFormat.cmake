# Optional style-check helpers, never wired into the default build (no
# target depends on `format`/`format-check`, and clang-format is not
# required to configure or build the project). Deliberately clang-format
# only, no clang-tidy — a default clang-tidy ruleset would flag hundreds of
# intentional project patterns (zero `virtual`, public struct-like access
# instead of friends, LOG/ASSERT_* macros) and needs real per-project rule
# tuning before it'd be usable, not just a CMake flag.
find_program(CLANG_FORMAT_EXE NAMES clang-format)

if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE OPENFEAR_FORMAT_SOURCES
        ${PROJECT_SOURCE_DIR}/apps/*.cpp ${PROJECT_SOURCE_DIR}/apps/*.h
        ${PROJECT_SOURCE_DIR}/include/*.h
        ${PROJECT_SOURCE_DIR}/src/*.cpp ${PROJECT_SOURCE_DIR}/src/*.h
    )

    # Rewrites files in place.
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i ${OPENFEAR_FORMAT_SOURCES}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Formatting sources with clang-format"
    )

    # Fails (non-zero exit) if any file needs reformatting, without touching
    # them - for a CI check, not for local iteration.
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${OPENFEAR_FORMAT_SOURCES}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Checking clang-format compliance"
    )
else()
    message(STATUS "clang-format not found - `format`/`format-check` targets unavailable (optional, build unaffected)")
endif()
