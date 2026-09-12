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

add_executable(LuaGamePlayer ${PLATFORM} LuaGamePlayer.cpp)
target_link_libraries(LuaGamePlayer PRIVATE Urho3D RbfxLuaScript)
set_property(TARGET LuaGamePlayer PROPERTY FOLDER "Applications")

group_sources()

install(TARGETS LuaGamePlayer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
