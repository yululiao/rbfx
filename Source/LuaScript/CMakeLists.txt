#
# LuaScript as an independent shared library.
# Provides sol3-based Lua bindings for the Urho3D engine.
#

project(RbfxLuaScript C CXX)

file(GLOB SOURCE_FILES *.cpp *.h)

add_library(RbfxLuaScript SHARED ${SOURCE_FILES})

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

target_compile_definitions(RbfxLuaScript PRIVATE RBFXLUA_EXPORTS)
target_compile_definitions(RbfxLuaScript PUBLIC RBFXLUA_SHARED)

# Ensure import library (.lib) and DLL (.dll) go to the same output directories as Urho3D.
set_target_properties(RbfxLuaScript PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
)

# Install public headers
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/LuaScript.h
              ${CMAKE_CURRENT_SOURCE_DIR}/LuaBindings.h
              ${CMAKE_CURRENT_SOURCE_DIR}/LuaNodeBindings.h
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
