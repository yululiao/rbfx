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

# Host executable for the Lua rewrite of the built-in samples. Sample scripts
# are copied next to the binary so the engine finds them through the regular
# resource directory mechanism ("CoreData;Data;LuaSamples").

if (NOT WIN32)
    set (PLATFORM)
else ()
    if (NOT URHO3D_WIN32_CONSOLE)
        set (PLATFORM WIN32)
    endif ()
endif ()

add_executable(LuaSamplesRunner ${PLATFORM} LuaSamplesRunner.cpp)
target_link_libraries(LuaSamplesRunner PRIVATE Urho3D)
set_property(TARGET LuaSamplesRunner PROPERTY FOLDER "Sample Projects")

# Keep the Lua sources beside the executable in sync on every build.
add_custom_command(TARGET LuaSamplesRunner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR} $<TARGET_FILE_DIR:LuaSamplesRunner>/LuaSamples
    COMMAND ${CMAKE_COMMAND} -E remove
        $<TARGET_FILE_DIR:LuaSamplesRunner>/LuaSamples/LuaSamplesRunner.cpp
        $<TARGET_FILE_DIR:LuaSamplesRunner>/LuaSamples/CMakeLists.txt
    COMMENT "Copying Lua sample scripts"
)

group_sources()

install(TARGETS LuaSamplesRunner RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        FILES_MATCHING PATTERN "*.lua")
