# macOS .app bundle packaging. No-op elsewhere.

# Builds <target> as a real .app: generates .icns from icon.png via
# sips/iconutil, Info.plist from icon/Info.plist.in (CMake substitutes its
# ${MACOSX_BUNDLE_*} placeholders itself, no configure_file() needed). Needed
# for double-click launch at all - a loose Unix binary opens inside
# Terminal.app with cwd=$HOME, breaking asset lookup (see
# AssetLocator::from_local()).
function(openfear_package_macos_bundle target)
    if(NOT APPLE OR EMSCRIPTEN)
        return()
    endif()

    set(_icns_filename "${target}.icns")
    set(_iconset_dir "${CMAKE_CURRENT_BINARY_DIR}/${target}.iconset")
    set(_icns_file "${CMAKE_CURRENT_BINARY_DIR}/${_icns_filename}")
    set(_source_png "${PROJECT_SOURCE_DIR}/icon/icon.png")

    add_custom_command(
        OUTPUT "${_icns_file}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_iconset_dir}"
        COMMAND sips -z 16 16     "${_source_png}" --out "${_iconset_dir}/icon_16x16.png"
        COMMAND sips -z 32 32     "${_source_png}" --out "${_iconset_dir}/icon_16x16@2x.png"
        COMMAND sips -z 32 32     "${_source_png}" --out "${_iconset_dir}/icon_32x32.png"
        COMMAND sips -z 64 64     "${_source_png}" --out "${_iconset_dir}/icon_32x32@2x.png"
        COMMAND sips -z 128 128   "${_source_png}" --out "${_iconset_dir}/icon_128x128.png"
        COMMAND sips -z 256 256   "${_source_png}" --out "${_iconset_dir}/icon_128x128@2x.png"
        COMMAND sips -z 256 256   "${_source_png}" --out "${_iconset_dir}/icon_256x256.png"
        COMMAND sips -z 512 512   "${_source_png}" --out "${_iconset_dir}/icon_256x256@2x.png"
        COMMAND sips -z 512 512   "${_source_png}" --out "${_iconset_dir}/icon_512x512.png"
        COMMAND sips -z 1024 1024 "${_source_png}" --out "${_iconset_dir}/icon_512x512@2x.png"
        COMMAND iconutil -c icns "${_iconset_dir}" -o "${_icns_file}"
        DEPENDS "${_source_png}"
        COMMENT "Generating ${_icns_filename} from icon.png")

    set_target_properties(${target} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/icon/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "OpenFear"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.openfear.openfear"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright OpenFear contributors. MIT License."
        MACOSX_BUNDLE_ICON_FILE "${_icns_filename}")

    target_sources(${target} PRIVATE "${_icns_file}")
    set_source_files_properties("${_icns_file}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
        GENERATED TRUE)
endfunction()

# Compiles a real Finder drop target ("droplet", Apple's own term) next to
# <target>'s .app bundle, so non-expert users can drag a folder/.iso onto it
# to install - a plain script can't receive a Finder drop, only a bundle
# can. See apps/openfear/launchers/InstallDroplet.applescript for behavior.
function(openfear_add_macos_install_launcher target)
    if(NOT APPLE OR EMSCRIPTEN)
        return()
    endif()

    set(_droplet_dir "$<TARGET_BUNDLE_DIR:${target}>/../Install OpenFear.app")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND osacompile -o "${_droplet_dir}"
            "${PROJECT_SOURCE_DIR}/apps/openfear/launchers/InstallDroplet.applescript"
        # osacompile ships its own generic script icon - swap in the real
        # one (same .icns already generated above), setting the plist key
        # ourselves instead of relying on whatever default filename
        # osacompile happens to use.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/${target}.icns"
            "${_droplet_dir}/Contents/Resources/${target}.icns"
        COMMAND /usr/libexec/PlistBuddy -c "Set :CFBundleIconFile ${target}.icns"
            "${_droplet_dir}/Contents/Info.plist"
    )
endfunction()

# Compiles a double-click-only companion app next to <target>'s .app
# bundle, same visual treatment as the installer (real icon, no generic
# script icon) for consistency - even though --uninstall takes no path
# and never receives a Finder drag like the installer does.
# See apps/openfear/launchers/UninstallApplet.applescript for behavior.
function(openfear_add_macos_uninstall_launcher target)
    if(NOT APPLE OR EMSCRIPTEN)
        return()
    endif()

    set(_uninstall_dir "$<TARGET_BUNDLE_DIR:${target}>/../Uninstall OpenFear.app")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND osacompile -o "${_uninstall_dir}"
            "${PROJECT_SOURCE_DIR}/apps/openfear/launchers/UninstallApplet.applescript"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/${target}.icns"
            "${_uninstall_dir}/Contents/Resources/${target}.icns"
        COMMAND /usr/libexec/PlistBuddy -c "Set :CFBundleIconFile ${target}.icns"
            "${_uninstall_dir}/Contents/Info.plist"
    )
endfunction()
