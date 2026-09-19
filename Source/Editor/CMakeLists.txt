#
# Copyright (c) 2017-2022 the rbfx project.
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

if (NOT URHO3D_SYSTEMUI OR NOT DESKTOP)
    return ()
endif ()

set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME Tools)

file (GLOB_RECURSE SOURCE_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.cpp *.h *.hpp)
list (REMOVE_ITEM SOURCE_FILES Editor.cpp)

#
# Groups sources so that they're displayed in IDE matching their layout in the filesystem
#
group_sources()

# Always export Editor without entry point as EditorLibrary.
# TODO: Support SHARED EditorLibrary for external Editor plugins.
add_library (EditorLibrary STATIC ${SOURCE_FILES})

target_include_directories(EditorLibrary PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/Tools>
)
target_link_libraries (EditorLibrary PUBLIC Urho3D)
if (MSVC)
    # Some tabs contain UTF-8 (non-ASCII) string literals; without an explicit
    # source/execution charset MSVC falls back to the system ANSI codepage and
    # mis-parses multi-byte sequences. /utf-8 keeps both source and narrow
    # literals UTF-8, which is what the immediate-mode UI expects at runtime.
    target_compile_options (EditorLibrary PRIVATE /utf-8)
endif ()
if (URHO3D_LUA)
    # The editor registers the imgui table and the editor-capability half of the "Editor" Lua
    # table straight into the subsystem's state (EditorLuaBindings / ImGuiLuaBindings), which
    # needs the sol3 headers and Lua symbols. Both arrive transitively: RbfxLuaScript links
    # them PUBLIC because its own public headers expose sol types.
    target_link_libraries(EditorLibrary PUBLIC RbfxLuaScript)
endif ()
# FBX import is performed in-process through the AssetImporter dynamic library
# (built as a tool, so it requires URHO3D_TOOLS to be enabled).
# Linked into executables instead of EditorLibrary to keep the SDK export set clean.
set (EDITOR_ASSETIMPORTER_LINK AssetImporter)

# Unless forbidden by the user, add Editor executable(s).
set (EDITOR_TARGETS "")
if (NOT URHO3D_NO_EDITOR_PLAYER_EXE)
    # Always add native application
    if (NOT URHO3D_WIN32_CONSOLE)
        set (TARGET_TYPE WIN32)
    endif ()

    add_executable (Editor ${TARGET_TYPE} Editor.cpp)
    target_link_libraries (Editor PUBLIC EditorLibrary)
    target_link_libraries (Editor PRIVATE ${EDITOR_ASSETIMPORTER_LINK})
    list (APPEND EDITOR_TARGETS Editor)

endif ()

foreach (EDITOR_TARGET ${EDITOR_TARGETS})
    target_link_plugins (${EDITOR_TARGET} "${URHO3D_PLUGIN_LIST}")
endforeach ()

# SDK installation
install_tools(${EDITOR_TARGETS})
foreach (EDITOR_TARGET ${EDITOR_TARGETS})
    install_target_runtime_deps(TARGET ${EDITOR_TARGET} COMPONENT Tools)
endforeach ()
install (TARGETS EditorLibrary EXPORT Urho3D ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install (DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Tools
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)
