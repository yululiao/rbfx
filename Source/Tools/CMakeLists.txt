#
# Copyright (c) 2008-2022 the Urho3D project.
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

if (NOT DESKTOP)
    install(FILES PackageTool/PackageTool.py DESTINATION ${CMAKE_INSTALL_BINDIR_BASE})
    return ()
endif ()

set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME Tools)

add_subdirectory(PackageTool)
add_subdirectory(RampGenerator)
add_subdirectory(SpritePacker)
add_subdirectory(ScriptPlayer)
add_subdirectory(AssetImporter)
add_subdirectory(LuaCompiler)

# PVRTexToolCLI is a prebuilt binary rather than a target, so it is deployed by hand. The editor's
# texture-compression stage finds it with FindTool(), which looks next to the running editor, so it has
# to sit in the build-tree bin for development and in the install bin for a packaged SDK. Windows only:
# that is the only platform this executable is provided for, and compressing for any target (Android
# included) happens on the host that runs the editor.
if (WIN32)
    set(PVRTEXTOOL_CLI ${CMAKE_CURRENT_SOURCE_DIR}/PVRTexTool/PVRTexToolCLI.exe)
    add_custom_target(PVRTexTool ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PVRTEXTOOL_CLI} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/PVRTexToolCLI.exe
        DEPENDS ${PVRTEXTOOL_CLI}
        COMMENT "Deploying PVRTexToolCLI"
        VERBATIM)
    install(PROGRAMS ${PVRTEXTOOL_CLI} DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Tools)
endif ()

vs_group_subdirectory_targets(${CMAKE_CURRENT_SOURCE_DIR} Tools)
