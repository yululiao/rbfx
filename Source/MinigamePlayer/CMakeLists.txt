#
# Copyright (c) 2026 the rbfx project.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#

# Standalone host for minigame platforms (Douyin/WeChat). Mirrors LuaGamePlayer's boot flow
# (Game.json -> scene -> Lua entry scripts) but produces an Emscripten module instead of an
# HTML page, because the vendor runtime has no DOM:
#
#   * MODULARIZE - the module is a factory (createRbfxModule) instantiated by the JavaScript
#     bootstrap (game.js) that ships in the minigame package. There is no shell file and no
#     generated HTML entry point.
#   * no --preload-file / -lidbfs.js - files are served by the vendor file system and mounted
#     into the engine VFS by the JavaScript bootstrap; IDBFS has no backing storage there.
#
# The remaining link flags (heap sizing, source map, export list) are platform-shaped and
# live in minigame_link_options() below so the two minigame variants (standard and legacy)
# can diverge without touching the host target.
#
# This target is only created for minigame builds (URHO3D_MINIGAME=ON implies WEB and
# URHO3D_LUA=ON; both are enforced in CMake/Modules/UrhoOptions.cmake).

function (minigame_link_options TARGET)
    # The runtime owns the event loop and never exits; the module must stay alive between
    # frames.
    target_link_options (${TARGET} PRIVATE -sNO_EXIT_RUNTIME=1 -sASSERTIONS=0)

    # The vendor runtimes are web-like JavaScript environments (the bootstrap installs the DOM
    # surface), so the glue needs the web path only. Left at the default the glue also carries
    # its Node branches, whose require("node:fs") / require("node:child_process") the vendor's
    # JavaScript compiler refuses to resolve while processing the package.
    target_link_options (${TARGET} PRIVATE -sENVIRONMENT=web)

    # Neither eval() nor new Function() may be used: the vendor runtime disables code generation
    # from strings, and a construct that depends on it breaks at startup - embind generates its
    # function invokers with new Function() by default. DYNAMIC_EXECUTION=0 switches embind (and
    # the rest of the glue) to its static fallbacks.
    target_link_options (${TARGET} PRIVATE -sDYNAMIC_EXECUTION=0)

    # Factory instantiation driven by the JavaScript bootstrap.
    target_link_options (${TARGET} PRIVATE
        -sMODULARIZE=1
        -sEXPORT_NAME=createRbfxModule
    )

    # File reads are synchronous by construction. On the vendor runtimes the only file API
    # that can reach the package directory is readFileSync, and CDN-hosted files are fetched
    # up front by the JavaScript loading pipeline (manifest-driven prefetch into the user
    # data directory) before the engine ever asks for them. Nothing worth reading is ever
    # pending on the network, so Asyncify stays off: it would instrument the entire engine
    # call graph to support a suspension path that cannot happen, paying in binary size and
    # execution speed for nothing.
    # The extra stack headroom covers the deep record/XML/Lua call chains of the boot
    # sequence and of scripted game code.
    target_link_options (${TARGET} PRIVATE
        -sSTACK_SIZE=1048576
        -sALLOW_MEMORY_GROWTH=1
    )

    # Heap policy. The standard variant takes the production-validated 768MB up front; the
    # legacy-compatible variant reserves more (960MB) because the old WebViews it targets
    # fail to grow the heap reliably, and additionally avoids the WebAssembly BigInt
    # integration those engines lack. Growth stays enabled in both variants.
    if (URHO3D_MINIGAME_LEGACY)
        target_link_options (${TARGET} PRIVATE -sINITIAL_MEMORY=960MB -sWASM_BIGINT=0)
    else ()
        target_link_options (${TARGET} PRIVATE -sINITIAL_MEMORY=768MB -sWASM_BIGINT=1)
    endif ()

    # Source map for every variant: a crashed session has to stay debuggable. The build
    # tree keeps the .map beside the subpackage; the package assembler never copies it into
    # the package, where it would be dead weight for every player.
    target_link_options (${TARGET} PRIVATE -gsource-map)

    # Platform layer injected into the module glue: canvas handoff for the vendor DOM mock,
    # the WebAssembly/WebGL/crypto shims, and the vendor wasm loader (Module.instantiateWasm).
    target_link_options (${TARGET} PRIVATE
        "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/pre-js/rbfx_pre.js"
    )

    # The C API of libdatachannel-wasm (rtc*) is authored in JavaScript and has to be handed
    # to the linker alongside the static library; shared helper with web_executable().
    link_datachannel_wasm (${TARGET})
endfunction ()

add_executable (MinigamePlayer MinigamePlayer.cpp)
set (MINIGAME_PLAYER_TARGET MinigamePlayer)

target_link_libraries (MinigamePlayer PRIVATE Urho3D RbfxLuaScript)
minigame_link_options (MinigamePlayer)

set_property (TARGET MinigamePlayer PROPERTY FOLDER "Applications")
install (TARGETS MinigamePlayer
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR})

# Package layout produced by this target (the package assembler maps it verbatim into the
# minigame package):
#
#   ${CMAKE_INSTALL_BINDIR}/minigame/...                main package payload: game.js entry,
#                                                       environment, adapter, loading screen
#   ${CMAKE_INSTALL_BINDIR}/${MINIGAME_SUBPACKAGE}/...  wasm subpackage: glue, payload, and
#                                                       the subpackage game.js entry
#
# The standard and legacy-compatible variants coexist in a shipped package, one per build
# tree, each under its own subpackage name.
if (URHO3D_MINIGAME_LEGACY)
    set (MINIGAME_SUBPACKAGE "package_wasm_compatible")
else ()
    set (MINIGAME_SUBPACKAGE "package_wasm")
endif ()

# The main-package JavaScript bootstrap. Kept in one list so the install tree and the build
# tree staging below always carry the same set.
set (MINIGAME_BOOTSTRAP_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/js/game.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/js/rbfx_env.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/js/rbfx_game_config.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/js/weapp-adapter.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/js/minigame_runtime.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/js/minigame_files.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/js/minigame_sdk.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/js/loading_config.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/js/loading.js"
)

install (FILES ${MINIGAME_BOOTSTRAP_FILES}
    DESTINATION ${CMAKE_INSTALL_BINDIR}/minigame)

install (FILES "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/MinigamePlayer.js"
    DESTINATION ${CMAKE_INSTALL_BINDIR}/${MINIGAME_SUBPACKAGE})
install (FILES "${CMAKE_CURRENT_SOURCE_DIR}/js/subpackage_game.js"
    DESTINATION ${CMAKE_INSTALL_BINDIR}/${MINIGAME_SUBPACKAGE}
    RENAME game.js)
if (EMSCRIPTEN_WASM)
    install (FILES "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/MinigamePlayer.wasm"
        DESTINATION ${CMAKE_INSTALL_BINDIR}/${MINIGAME_SUBPACKAGE})
endif ()

# Building the player also stages everything above beside it, so a build tree can be
# assembled into a package (or served for local testing) without going through
# `cmake --install` first.
add_custom_command (TARGET MinigamePlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MinigamePlayer>/minigame"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MINIGAME_BOOTSTRAP_FILES}
        "$<TARGET_FILE_DIR:MinigamePlayer>/minigame"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MinigamePlayer>/${MINIGAME_SUBPACKAGE}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/js/subpackage_game.js"
        "$<TARGET_FILE_DIR:MinigamePlayer>/${MINIGAME_SUBPACKAGE}/game.js"
    COMMENT "Staging the minigame bootstrap scripts")

group_sources ()
