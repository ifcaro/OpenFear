# Emscripten-specific build helpers, included once by the root CMakeLists.txt.

# Configures Emscripten-specific link flags/output for <target>: .html
# output, growable heap, Asyncify (lets the game's blocking-style loops
# suspend/resume instead of stalling the browser thread), and the JS
# asset-provider pre-js glue. OpenFear's own small assets (e.g.
# data/fonts/basicfont.bmp) are compiled in via openfear_embed_binary_file()
# instead of a virtual-FS --embed-file, so they need no special handling
# here; the real CD game data never gets embedded either way — the user
# supplies that via the browser file picker at runtime.
function(openfear_configure_emscripten_output target pre_js_file)
    if(NOT EMSCRIPTEN)
        return()
    endif()

    set_target_properties(${target} PROPERTIES SUFFIX ".html")

    set(_shell_file "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
    set(_flags
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=0
        -sASYNCIFY=1
        -sASYNCIFY_STACK_SIZE=1048576
        "--pre-js=${pre_js_file}"
        "--shell-file=${_shell_file}"
    )
    target_link_options(${target} PRIVATE ${_flags})
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${_shell_file}" "${pre_js_file}")
endfunction()
