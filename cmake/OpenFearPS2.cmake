# PS2-specific build helpers, included once by the root CMakeLists.txt.

# Embeds the PS2 IOP modules (.irx) needed before any filesystem exists to
# load them from a path - iomanX+fileXio, bdm+bdmfs_fatfs+usbd+usbmass_bd
# (mass: USB), libsd+audsrv, sio2man+mtapman+padman. No cdfs - never used
# for a real CD read, see Iso9660Reader/Ps2AssetProvider. Audio/pad modules
# load on demand from Ps2Boot.cpp instead, not here at boot. Each module
# becomes a byte array via PS2Dev's bin2c, loaded with
# SifExecModuleBuffer() (no path yet for SifLoadModule). No-op outside the
# PS2 toolchain.
function(openfear_embed_ps2_irx_modules target)
    if(NOT DEFINED ENV{PS2SDK})
        return()
    endif()
    set(_bin2c "$ENV{PS2SDK}/bin/bin2c")
    set(_irx_dir "$ENV{PS2SDK}/iop/irx")
    set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}/ps2_irx")
    file(MAKE_DIRECTORY "${_out_dir}")

    set(_modules iomanX fileXio bdm bdmfs_fatfs usbd usbmass_bd
                 libsd audsrv sio2man mtapman padman)
    foreach(_mod ${_modules})
        set(_in "${_irx_dir}/${_mod}.irx")
        set(_out "${_out_dir}/${_mod}_irx.cpp")
        add_custom_command(
            OUTPUT "${_out}"
            COMMAND "${_bin2c}" "${_in}" "${_out}" "${_mod}_irx"
            DEPENDS "${_in}"
            COMMENT "bin2c ${_mod}.irx"
        )
        target_sources(${target} PRIVATE "${_out}")
    endforeach()
endfunction()

# Strips debug symbols and LZMA-packs <target>'s PS2 ELF right after it
# links, as 2 extra artifacts next to the main one:
# <name>-stripped.elf (debug symbols removed) and <name>-packed.elf (also
# ps2-packer-compressed). Real hardware transfer over ps2link was failing on
# the unstripped ELF (~17.8MB, of which debug symbols alone were ~13.7MB) -
# ps2-packer hardcodes a "./<name>-packer.so" relative lookup for its packer
# module, so the command below runs from that module's directory. No-op
# outside the PS2 toolchain.
function(openfear_package_ps2_elf target)
    if(NOT DEFINED ENV{PS2SDK})
        return()
    endif()
    set(_ps2dev $ENV{PS2DEV})
    set(_strip "${_ps2dev}/ee/bin/mips64r5900el-ps2-elf-strip")
    set(_packer "${_ps2dev}/bin/ps2-packer")
    set(_packer_module_dir "${_ps2dev}/share/ps2-packer/module")
    set(_packer_stub "${_ps2dev}/share/ps2-packer/stub/lzma-1d00-stub")
    set(_stripped "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>-stripped.elf")
    set(_packed "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>-packed.elf")
    # Guard against the GPLv3 ps2_drivers package silently creeping back
    # into the link if the SDL3 pin is ever bumped without re-checking —
    # see CheckNoPs2Drivers.cmake for why this check is needed at all.
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} "-DLINK_FILE=CMakeFiles/${target}.dir/link.txt"
                -P "${PROJECT_SOURCE_DIR}/cmake/CheckNoPs2Drivers.cmake"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${target}>" "${_stripped}"
        COMMAND "${_strip}" -s "${_stripped}"
        COMMAND ${CMAKE_COMMAND} -E chdir "${_packer_module_dir}"
                "${_packer}" -s "${_packer_stub}" "${_stripped}" "${_packed}"
        COMMENT "PS2: stripping + LZMA-packing ${target} for ps2link transfer"
        VERBATIM
    )
endfunction()
