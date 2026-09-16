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

# LuaSocket 3.1.0 vendored from https://github.com/lunarmodules/luasocket (see LICENSE).
# Only the core C modules are built: socket.core and mime.core. The optional
# Unix-domain (unix*.c) and serial (serial.c) modules stay out -- they have no
# consumer here and only widen the platform matrix. The Lua-level wrapper
# (socket.lua, kept next to the sources for reference) is embedded by
# Source/LuaScript/LuaSocketModule.cpp instead of being shipped as a resource,
# so require("socket") works before any project data is mounted.

set(LUASOCKET_SOURCE_FILES
    auxiliar.c
    buffer.c
    compat.c
    except.c
    inet.c
    io.c
    luasocket.c
    mime.c
    options.c
    select.c
    tcp.c
    timeout.c
    udp.c
)

# One socket backend per platform: WinSock on Windows, BSD sockets elsewhere.
if (WIN32)
    list(APPEND LUASOCKET_SOURCE_FILES wsocket.c)
else ()
    list(APPEND LUASOCKET_SOURCE_FILES usocket.c)
endif ()

add_library(luasocket STATIC ${LUASOCKET_SOURCE_FILES})

target_include_directories(luasocket
    INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    PRIVATE   $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# Lua is PUBLIC because the public header luasocket.h includes lua.h/lauxlib.h.
target_link_libraries(luasocket PUBLIC Lua)

if (WIN32)
    target_link_libraries(luasocket PRIVATE ws2_32)
endif ()

install_third_party_libs(luasocket)
