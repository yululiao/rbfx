#
# LuaScript as an independent shared library.
# Provides sol3-based Lua bindings for the Urho3D engine.
#

project(RbfxLuaScript C CXX)

file(GLOB SOURCE_FILES *.cpp *.h)

# Emscripten has no dynamic libraries in the plain sense: a SHARED target would be linked as a
# side module and fail to resolve the engine symbols it needs. The web host links Lua statically,
# exactly like every other statically linked engine component.
if (WEB)
    add_library(RbfxLuaScript STATIC ${SOURCE_FILES})
else ()
    add_library(RbfxLuaScript SHARED ${SOURCE_FILES})
endif ()

target_link_libraries(RbfxLuaScript PUBLIC Urho3D)
# Lua and sol3 must be linked directly since Urho3D links them privately.
target_link_libraries(RbfxLuaScript PRIVATE Lua sol3)
target_include_directories(RbfxLuaScript PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

# sol3 usertype registrations cause heavy template instantiation
if (MSVC)
    set_source_files_properties(${SOURCE_FILES} PROPERTIES COMPILE_OPTIONS "/bigobj")
endif ()

# The Windows DLL export macros are only meaningful for the shared library; the static web build
# must not define RBFXLUA_SHARED or the engine-side headers would look for a dllimport that is not
# there.
if (NOT WEB)
    target_compile_definitions(RbfxLuaScript PRIVATE RBFXLUA_EXPORTS)
    target_compile_definitions(RbfxLuaScript PUBLIC RBFXLUA_SHARED)
endif ()

# Content key of packaged Lua scripts. The compiled-in default is the development key; a release
# build passes its own with -DRBFX_LUA_SCRIPT_KEY_HEX=<64 hex digits> so that the key inside the
# shipped binary is not the one in the repository. The environment variable
# RBFX_LUA_SCRIPT_KEY overrides both at run time, which is how a development build opens a
# shipping package. Only the library and the bundler that links it need the value.
if (RBFX_LUA_SCRIPT_KEY_HEX)
    target_compile_definitions(RbfxLuaScript PRIVATE RBFX_LUA_SCRIPT_KEY_HEX="${RBFX_LUA_SCRIPT_KEY_HEX}")
endif ()

# Ensure import library (.lib) and DLL (.dll) go to the same output directories as Urho3D.
set_target_properties(RbfxLuaScript PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
)

# Install public headers
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/LuaScript.h
              ${CMAKE_CURRENT_SOURCE_DIR}/LuaGameScript.h
              ${CMAKE_CURRENT_SOURCE_DIR}/LuaBindings.h
              ${CMAKE_CURRENT_SOURCE_DIR}/LuaNodeBindings.h
              ${CMAKE_CURRENT_SOURCE_DIR}/EditorLuaScript.h
              ${CMAKE_CURRENT_SOURCE_DIR}/EditorLuaHooks.h
              ${CMAKE_CURRENT_SOURCE_DIR}/LuaFile.h
              ${CMAKE_CURRENT_SOURCE_DIR}/LuaPackageLoader.h
              ${CMAKE_CURRENT_SOURCE_DIR}/LuaScriptContainer.h
              ${CMAKE_CURRENT_SOURCE_DIR}/Export.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/LuaScript
)

# Install the shared library
install(TARGETS RbfxLuaScript
    EXPORT Urho3D
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

group_sources()
