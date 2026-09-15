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
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

# Standalone host for Lua-driven games. Brings up the LuaScript subsystem,
# reads the startup scene from Data/Game.json, loads it and executes the entry
# scripts referenced by the scene's LuaGameScript components.
#
# It links the shared RbfxLuaScript module (which also provides the serializable
# LuaGameScript type) so scenes authored in the editor deserialize here. The
# pure C++ Player application is intentionally left separate and untouched.

if (NOT WIN32)
    set (PLATFORM)
else ()
    if (NOT URHO3D_WIN32_CONSOLE)
        set (PLATFORM WIN32)
    endif ()
endif ()

# Android cannot start an executable: SDL loads every library it finds in the apk and the one that
# owns the application runs. The target is therefore a module, named with the "lib" spelled by hand
# and with no prefix, exactly like the C++ Player does - the activity derives the load order from
# the file name after stripping a leading "lib", which only resolves to "LuaGamePlayer" for a file
# called libLuaGamePlayer.so.
if (ANDROID)
    add_library (libLuaGamePlayer MODULE LuaGamePlayer.cpp)
    set_target_properties (libLuaGamePlayer PROPERTIES PREFIX "")
    set (LUA_GAME_PLAYER_TARGET libLuaGamePlayer)
elseif (WEB)
    # The web host is a full game host, unlike the C++ Player: a Lua game loads its scene and
    # scripts from the mounted resource directories, so there is user code to run - it just
    # lives in Data/ instead of the binary. The executable becomes one emscripten HTML page.
    add_executable (LuaGamePlayer LuaGamePlayer.cpp)
    set (LUA_GAME_PLAYER_TARGET LuaGamePlayer)
    web_executable (LuaGamePlayer)
    # The preloaded resources are the engine's own Data/CoreData paks (a project build regenerates
    # the pair with its own data; the loader only cares about the file names). Samples normally
    # create these paks, but a build without samples still needs them to link against.
    create_pak("${rbfx_SOURCE_DIR}/bin/Data"     "${CMAKE_BINARY_DIR}/bin/Data.pak")
    create_pak("${rbfx_SOURCE_DIR}/bin/CoreData" "${CMAKE_BINARY_DIR}/bin/CoreData.pak")
    # Link the resource loader (preRun hook that fetches Resources.js before main).
    package_resources_web(
        FILES        "${CMAKE_BINARY_DIR}/bin/Data.pak"
                     "${CMAKE_BINARY_DIR}/bin/CoreData.pak"
        RELATIVE_DIR "${CMAKE_BINARY_DIR}/bin"
        OUTPUT       "Resources.js"
        INSTALL_TO   "${CMAKE_INSTALL_BINDIR}"
    )
    web_link_resources(LuaGamePlayer "Resources.js")
    target_link_libraries(LuaGamePlayer PRIVATE "--shell-file ${CMAKE_SOURCE_DIR}/bin/shell.html")
else ()
    add_executable (LuaGamePlayer ${PLATFORM} LuaGamePlayer.cpp)
    set (LUA_GAME_PLAYER_TARGET LuaGamePlayer)
endif ()

if (LUA_GAME_PLAYER_TARGET)
    target_link_libraries (${LUA_GAME_PLAYER_TARGET} PRIVATE Urho3D RbfxLuaScript)
    set_property (TARGET ${LUA_GAME_PLAYER_TARGET} PROPERTY FOLDER "Applications")
    install (TARGETS ${LUA_GAME_PLAYER_TARGET}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR})
    if (WEB)
        # The html "executable" drags the engine side of the page along: the script glue and, for
        # a wasm build, the binary itself.
        install (FILES "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/LuaGamePlayer.js"
            DESTINATION ${CMAKE_INSTALL_BINDIR})
        if (EMSCRIPTEN_WASM)
            install (FILES "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/LuaGamePlayer.wasm"
                DESTINATION ${CMAKE_INSTALL_BINDIR})
        endif ()
    endif ()
endif ()

group_sources()
