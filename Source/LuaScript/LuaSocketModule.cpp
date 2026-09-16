// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include "LuaSocketModule.h"

#ifdef URHO3D_LUASOCKET

#include <cstring>

extern "C" {
#include "luasocket.h"
#include "mime.h"
}

namespace Urho3D
{

namespace
{

// socket.lua from LuaSocket 3.1.0 (Diego Nehab, MIT -- see ThirdParty/LuaSocket/LICENSE),
// the Lua-level wrapper that rounds out socket.core with connect/bind/sink/source helpers.
// It is embedded rather than shipped as a resource so require("socket") resolves before
// any project data is mounted, which is exactly when a debugger wants to attach.
const char* const kSocketLuaSource = R"SOCKETLUA(
-----------------------------------------------------------------------------
-- LuaSocket helper module
-- Author: Diego Nehab
-----------------------------------------------------------------------------

-----------------------------------------------------------------------------
-- Declare module and import dependencies
-----------------------------------------------------------------------------
local base = _G
local string = require("string")
local math = require("math")
local socket = require("socket.core")

local _M = socket

-----------------------------------------------------------------------------
-- Exported auxiliar functions
-----------------------------------------------------------------------------
function _M.connect4(address, port, laddress, lport)
    return socket.connect(address, port, laddress, lport, "inet")
end

function _M.connect6(address, port, laddress, lport)
    return socket.connect(address, port, laddress, lport, "inet6")
end

function _M.bind(host, port, backlog)
    if host == "*" then host = "0.0.0.0" end
    local addrinfo, err = socket.dns.getaddrinfo(host);
    if not addrinfo then return nil, err end
    local sock, res
    err = "no info on address"
    for i, alt in base.ipairs(addrinfo) do
        if alt.family == "inet" then
            sock, err = socket.tcp4()
        else
            sock, err = socket.tcp6()
        end
        if not sock then return nil, err end
        sock:setoption("reuseaddr", true)
        res, err = sock:bind(alt.addr, port)
        if not res then
            sock:close()
        else
            res, err = sock:listen(backlog)
            if not res then
                sock:close()
            else
                return sock
            end
        end
    end
    return nil, err
end

_M.try = _M.newtry()

function _M.choose(table)
    return function(name, opt1, opt2)
        if base.type(name) ~= "string" then
            name, opt1, opt2 = "default", name, opt1
        end
        local f = table[name or "nil"]
        if not f then base.error("unknown key (".. base.tostring(name) ..")", 3)
        else return f(opt1, opt2) end
    end
end

-----------------------------------------------------------------------------
-- Socket sources and sinks, conforming to LTN12
-----------------------------------------------------------------------------
-- create namespaces inside LuaSocket namespace
local sourcet, sinkt = {}, {}
_M.sourcet = sourcet
_M.sinkt = sinkt

_M.BLOCKSIZE = 2048

sinkt["close-when-done"] = function(sock)
    return base.setmetatable({
        getfd = function() return sock:getfd() end,
        dirty = function() return sock:dirty() end
    }, {
        __call = function(self, chunk, err)
            if not chunk then
                sock:close()
                return 1
            else return sock:send(chunk) end
        end
    })
end

sinkt["keep-open"] = function(sock)
    return base.setmetatable({
        getfd = function() return sock:getfd() end,
        dirty = function() return sock:dirty() end
    }, {
        __call = function(self, chunk, err)
            if chunk then return sock:send(chunk)
            else return 1 end
        end
    })
end

sinkt["default"] = sinkt["keep-open"]

_M.sink = _M.choose(sinkt)

sourcet["by-length"] = function(sock, length)
    return base.setmetatable({
        getfd = function() return sock:getfd() end,
        dirty = function() return sock:dirty() end
    }, {
        __call = function()
            if length <= 0 then return nil end
            local size = math.min(socket.BLOCKSIZE, length)
            local chunk, err = sock:receive(size)
            if err then return nil, err end
            length = length - string.len(chunk)
            return chunk
        end
    })
end

sourcet["until-closed"] = function(sock)
    local done
    return base.setmetatable({
        getfd = function() return sock:getfd() end,
        dirty = function() return sock:dirty() end
    }, {
        __call = function()
            if done then return nil end
            local chunk, err, partial = sock:receive(socket.BLOCKSIZE)
            if not err then return chunk
            elseif err == "closed" then
                sock:close()
                done = 1
                return partial
            else return nil, err end
        end
    })
end


sourcet["default"] = sourcet["until-closed"]

_M.source = _M.choose(sourcet)

return _M
)SOCKETLUA";

/// Load the embedded socket.lua wrapper. luaL_requiref expects a lua_CFunction that
/// pushes the module table; the wrapper chunk returns exactly that.
int OpenSocketWrapper(lua_State* L)
{
    if (luaL_loadbuffer(L, kSocketLuaSource, std::strlen(kSocketLuaSource), "socket.lua") != LUA_OK)
        return lua_error(L);
    lua_call(L, 0, 1);
    return 1;
}

}

void RegisterLuaSocketModules(lua_State* luaState)
{
    // The C cores land in package.loaded first, so the wrapper's own require("socket.core")
    // resolves from there and never consults the package searchers (the VFS included).
    luaL_requiref(luaState, "socket.core", luaopen_socket_core, 0);
    lua_pop(luaState, 1);
    luaL_requiref(luaState, "mime.core", luaopen_mime_core, 0);
    lua_pop(luaState, 1);
    luaL_requiref(luaState, "socket", OpenSocketWrapper, 0);
    lua_pop(luaState, 1);
}

}

#else

namespace Urho3D
{

void RegisterLuaSocketModules(lua_State* luaState)
{
    // Built without URHO3D_LUASOCKET: no socket modules to preload.
    (void)luaState;
}

}

#endif
