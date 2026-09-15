#
# LuaScript as a static library.
# Provides sol3-based Lua bindings for the Urho3D engine.
#

project(RbfxLuaScript C CXX)

file(GLOB SOURCE_FILES *.cpp *.h)

# Static on every platform: the web target has no dynamic libraries in the plain sense, and a
# single static form removes the Lua-version pairing constraint between the engine and its
# consumers (editor, game player, Lua compiler tool) -- they all link the same code in.
add_library(RbfxLuaScript STATIC ${SOURCE_FILES})

# Lua and sol3 are PUBLIC: the public headers (LuaScript.h, EditorLuaScript.h, LuaBindings.h)
# expose sol types in their signatures, so every consumer of this library needs both the
# headers and the Lua symbols. Urho3D links them privately and no longer re-exports them.
target_link_libraries(RbfxLuaScript PUBLIC Urho3D Lua sol3)
target_include_directories(RbfxLuaScript PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

# sol3 usertype registrations cause heavy template instantiation
if (MSVC)
    set_source_files_properties(${SOURCE_FILES} PROPERTIES COMPILE_OPTIONS "/bigobj")
endif ()

# The library is static, so the RBFXLUA_API export/import macros in Export.h are permanently in
# their empty branch: neither RBFXLUA_SHARED nor RBFXLUA_EXPORTS is defined here.

# Content key of packaged Lua scripts. The compiled-in default is the development key; a release
# build passes its own with -DRBFX_LUA_SCRIPT_KEY_HEX=<64 hex digits> so that the key inside the
# shipped binary is not the one in the repository. The environment variable
# RBFX_LUA_SCRIPT_KEY overrides both at run time, which is how a development build opens a
# shipping package. Only the library and the bundler that links it need the value.
if (RBFX_LUA_SCRIPT_KEY_HEX)
    target_compile_definitions(RbfxLuaScript PRIVATE RBFX_LUA_SCRIPT_KEY_HEX="${RBFX_LUA_SCRIPT_KEY_HEX}")
endif ()

# Ensure the static library goes to the same output directory as Urho3D.
set_target_properties(RbfxLuaScript PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
)

# Install public headers
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/LuaScript.h
              ${CMAKE_CURRENT_SOURCE_DIR}/LuaGameScript.h
              ${CMAKE_CURRENT_SOURCE_DIR}/LuaBindings.h
              ${CMAKE_CURRENT_SOURCE_DIR}/LuaNodeBindings.h
              ${CMAKE_CURRENT_SOURCE_DIR}/EditorLuaScript.h
              ${CMAKE_CURRENT_SOURCE_DIR}/LuaFile.h
              ${CMAKE_CURRENT_SOURCE_DIR}/LuaPackageLoader.h
              ${CMAKE_CURRENT_SOURCE_DIR}/LuaScriptContainer.h
              ${CMAKE_CURRENT_SOURCE_DIR}/Export.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/LuaScript
)

# Install the library
install(TARGETS RbfxLuaScript
    EXPORT Urho3D
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

group_sources()
