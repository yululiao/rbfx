-- LuaSamples/55_LuaSocket/main.lua
-- Smoke test for the engine-embedded LuaSocket modules: require("socket") must resolve
-- from package.loaded (no filesystem searcher), the Lua-level wrapper from socket.lua
-- must be fully expanded, and a TCP loopback round-trip must prove the C core is live.

local app = Sample:new()

function app:OnStart()
    -- 1. The preload itself: this is the line the LuaPanda debugger needs to work.
    local socket = require("socket")
    assert(socket, "require(\"socket\") returned nil")

    -- 2. The C core reports its version through the preloaded table.
    LogInfo("socket._VERSION = " .. tostring(socket._VERSION))

    -- 3. The embedded socket.lua wrapper must be expanded on top of the core:
    --    these are the functions that only exist in the Lua-level module.
    local wrapperFunctions = { "connect4", "connect6", "bind", "sink", "source", "try", "choose" }
    for _, name in ipairs(wrapperFunctions) do
        assert(type(socket[name]) == "function", "socket." .. name .. " is missing (wrapper not loaded)")
    end
    assert(type(socket.BLOCKSIZE) == "number", "socket.BLOCKSIZE is missing (wrapper not loaded)")

    -- 4. The mime C core is preloaded as well.
    local mimeCore = require("mime.core")
    assert(type(mimeCore) == "table", "require(\"mime.core\") failed")

    -- 5. A real TCP loopback round-trip: proves wsocket.c and WinSock are linked in.
    local server = assert(socket.tcp())
    assert(server:bind("127.0.0.1", 0), "server:bind failed")
    assert(server:listen(1), "server:listen failed")
    local _, port = assert(server:getsockname())
    assert(type(port) == "number", "server:getsockname returned no port")

    local client = assert(socket.tcp())
    assert(client:connect("127.0.0.1", port), "client:connect failed")
    local peer = assert(server:accept(), "server:accept failed")

    assert(peer:send("ping") == 4, "peer:send failed")
    assert(client:settimeout(2), "client:settimeout failed")
    local data = assert(client:receive(4), "client:receive failed")
    assert(data == "ping", "round-trip data mismatch: " .. tostring(data))

    peer:close()
    client:close()
    server:close()

    LogInfo("LuaSocket smoke test PASSED: require, wrapper, mime.core and TCP loopback all OK")

    GetSubsystem("Engine"):Exit()
end

app:Run()
