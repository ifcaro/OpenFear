add_executable(openfear main.cpp PostGameSequence.cpp GameFlow.cpp)

if(DEFINED ENV{PS2SDK})
    target_sources(openfear PRIVATE RuntimeDiagnostics_ps2.cpp)
else()
    target_sources(openfear PRIVATE RuntimeDiagnostics_generic.cpp)
endif()

if(EMSCRIPTEN)
    target_sources(openfear PRIVATE RuntimeSetup_emscripten.cpp)
elseif(DEFINED ENV{PS2SDK})
    target_sources(openfear PRIVATE RuntimeSetup_ps2.cpp)
else()
    # --install (copies CD/ISO/folder assets into a local "assets" folder,
    # see InstallFlow.h) needs writable storage distinct from the asset
    # source
    target_sources(openfear PRIVATE RuntimeSetup_native.cpp InstallFlow.cpp)
endif()
target_link_libraries(openfear PRIVATE openfear_platform openfear_game)

# Manual-QA quickstarts/cheats (--test-*, --cheat-*) — Debug only. Excluded
# from the target entirely in Release so no test-only code ships, not just
# runtime-disabled; DebugQuickstart.h/.cpp also self-gate on #ifndef NDEBUG
# as a second line of defense for multi-config generators.
target_sources(openfear PRIVATE $<$<CONFIG:Debug>:DebugQuickstart.cpp>)

openfear_add_icon(openfear)

# GUI subsystem (no console window) on native Windows only.
if(WIN32 AND NOT EMSCRIPTEN)
    set_target_properties(openfear PROPERTIES
        WIN32_EXECUTABLE TRUE
        LINK_FLAGS "/ENTRY:mainCRTStartup"
    )
endif()

# PS2 ELF loaders filter by *.elf - the ps2dev.cmake toolchain doesn't set
# this itself, output is a bare extensionless ELF otherwise.
if(DEFINED ENV{PS2SDK})
    set_target_properties(openfear PROPERTIES SUFFIX ".elf")
    openfear_package_ps2_elf(openfear)
endif()

openfear_configure_emscripten_output(openfear
    "${CMAKE_CURRENT_SOURCE_DIR}/web/asset_provider.js")

if(EMSCRIPTEN)
    add_custom_command(TARGET openfear POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/icon/icon-32.png"
            "$<TARGET_FILE_DIR:openfear>/favicon.png"
    )
endif()
