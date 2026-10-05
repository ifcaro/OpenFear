# Shared build helpers, included once by the root CMakeLists.txt so every
# add_subdirectory() target can use them without its own per-toolchain logic.

# Links SDL3 onto <target>. Uses Emscripten's built-in SDL3 port
# (compiler/linker flags, no find_package) under emcmake, libsdl-org/SDL
# fetched+built from source as a regular subdirectory target under the
# ps2dev.cmake toolchain (see root CMakeLists.txt's FetchContent block,
# right after libcinepak/libindeo3 — that comment covers why it's pinned),
# or vcpkg/CONFIG/pkg-config on native builds. PS2 needs its own
# from-source build rather than a prebuilt package because PS2SDK's ports
# tree only carries SDL2 (no SDL3 at all). No SDL3_mixer — audio is our
# own mixer built on SDL3 core (SDL_LoadWAV_IO + SDL_AudioStream +
# SDL_OpenAudioDevice, see SDL3Audio.cpp), and no SDL3_image — every image
# asset is a BMP, decoded by our own decoder (SDL3Renderer.cpp's
# decode_bmp()).
function(openfear_link_sdl3 target)
    if(EMSCRIPTEN)
        set(_sdl3_flags
            -sUSE_SDL=3
        )
        target_compile_options(${target} PUBLIC ${_sdl3_flags})
        target_link_options(${target} PUBLIC ${_sdl3_flags})
        return()
    endif()

    # ps2dev.cmake requires PS2SDK to be set (it hard-errors otherwise), so
    # its presence at configure time reliably identifies a PS2 cross-build -
    # there's no CMake-native EMSCRIPTEN-style variable for it.
    if(DEFINED ENV{PS2SDK})
        set(_ps2sdk $ENV{PS2SDK})
        set(_ps2dev $ENV{PS2DEV})

        # SDL3 built from source as a FetchContent subdirectory (root
        # CMakeLists.txt, right after libcinepak/libindeo3) - no prebuilt
        # install step needed, it's a normal target in this same build graph.
        target_link_directories(${target} PUBLIC "${_ps2dev}/gsKit/lib")
        target_link_libraries(${target} PUBLIC
            $<IF:$<TARGET_EXISTS:SDL3::SDL3>,SDL3::SDL3,SDL3::SDL3-static>
            # mtap/pad/audsrv: SDL3's own PS2 audio/joystick backends call
            # mtapInit()/mtapPortOpen()/padInit()/audsrv_init() directly -
            # needed to resolve those symbols even though nothing in this
            # project's own code calls those libraries itself (Ps2Boot.cpp's
            # init_audio_driver()/init_joystick_driver() do, on SDL's behalf).
            gskit dmakit fileXio mc pad mtap audsrv
            # eedebug: Ps2Boot.cpp's EE exception handler (ee_dbg_install()/
            # ee_dbg_set_level1_handler()) for crash diagnostics via
            # scr_printf().
            eedebug
            # This toolchain's libgcc has no native __atomic_fetch_add_4 for
            # the EE, and our own code (e.g. Ps2AssetProvider.cpp's
            # std::atomic<uint32_t> call counter) needs it resolved from
            # libatomic directly.
            atomic
            # SDL3's PS2 entry wrapper (SDL_sysmain_runapp.c: prepare_IOP())
            # calls sbv_patch_*() to patch the IOP's syscall table for
            # compatibility - without this it's an undefined-reference link
            # error the moment anything actually pulls that object in.
            patches
            # Full path, not the bare name: "debug" is a target_link_libraries
            # keyword (DEBUG/OPTIMIZED/GENERAL config selector) - a plain
            # "debug" entry gets parsed as that keyword instead of a library.
            "${_ps2sdk}/ee/lib/libdebug.a"
        )
        # eedebug's callstack.c (linked above) declares its own loose
        # reference to main() for stack-unwinding purposes, which never
        # matches our real main() signature under LTO's stricter type
        # checking - harmless (it only needs main's address, never calls
        # it), but noisy on every link. Not our declaration to fix.
        target_link_options(${target} PUBLIC -Wno-lto-type-mismatch)
        return()
    endif()

    find_package(SDL3 QUIET CONFIG)
    if(NOT SDL3_FOUND)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL3 REQUIRED sdl3)
    endif()
    if(NOT TARGET SDL3::SDL3 AND NOT TARGET SDL3::SDL3-static)
        add_library(SDL3::SDL3 IMPORTED INTERFACE)
        set_target_properties(SDL3::SDL3 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${SDL3_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES      "${SDL3_LIBRARIES}")
    endif()

    # Some vcpkg triplets (e.g. static-only ones like arm64-android) only
    # register the "-static" suffixed target, not the plain name - pick
    # whichever actually exists instead of assuming the plain one always
    # does (silently linking against nothing otherwise, no configure error).
    target_link_libraries(${target} PUBLIC
        $<IF:$<TARGET_EXISTS:SDL3::SDL3>,SDL3::SDL3,SDL3::SDL3-static>
    )

    # SDL3 folds SDL_main linkage into the SDL3 target itself via
    # SDL_main.h's macro shim - no separate SDL3main target to link (unlike
    # SDL2's SDL2::SDL2main).
endfunction()

# Compiles <input_file> into <target> as a byte array (extern "C" const
# unsigned char <symbol_name>[] + <symbol_name>_len) via EmbedBinaryFile.cmake
# — for OpenFear's own small assets (e.g. data/fonts/basicfont.bmp), which
# aren't part of the user-supplied CD asset root and so have no reliable
# runtime path on every target (PS2 asset_root is a "cdfs:"-style prefix
# with no real parent directory; native builds never copied data/ next to
# the exe either). Embedding removes the path-resolution problem entirely,
# uniformly across every platform this project builds for.
function(openfear_embed_binary_file target input_file symbol_name)
    get_filename_component(_name ${input_file} NAME_WE)
    set(_out "${CMAKE_CURRENT_BINARY_DIR}/embedded_${_name}.cpp")
    add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT_FILE=${input_file}"
            "-DOUTPUT_FILE=${_out}"
            "-DSYMBOL_NAME=${symbol_name}"
            -P "${PROJECT_SOURCE_DIR}/cmake/EmbedBinaryFile.cmake"
        DEPENDS "${input_file}" "${PROJECT_SOURCE_DIR}/cmake/EmbedBinaryFile.cmake"
        COMMENT "Embedding ${input_file} -> ${symbol_name}"
    )
    target_sources(${target} PRIVATE "${_out}")
endfunction()

# Embeds icon/icon.ico into <target>'s executable via icon/icon.rc. No-op
# outside Windows native builds.
function(openfear_add_icon target)
    if(WIN32 AND NOT EMSCRIPTEN)
        target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/icon/icon.rc")
    endif()
endfunction()
