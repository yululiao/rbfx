#
# Copyright (c) 2026 the rbfx project.
# This work is licensed under the terms of the MIT license.
# For a copy, see <https://opensource.org/licenses/MIT>.
#

# Offline bundler for Lua scripts. Produces the .luc containers that LuaFile loads at run time,
# so that a release build carries no plain Lua source. Safe to run from CI: it exits non-zero
# unless every container it wrote can be read back with the key in use.

return_if_not_tool(LuaCompiler)

file (GLOB SOURCE_FILES *.cpp *.h)
add_executable (LuaCompiler ${SOURCE_FILES})

# The container format and the bytecode compiler both live in RbfxLuaScript, and the tool links
# against it rather than duplicating them: one definition of the layout means the build tool and
# the engine cannot drift apart.
target_link_libraries (LuaCompiler Urho3D RbfxLuaScript)
install_tools(LuaCompiler)

# Install runtime dependencies
install_target_runtime_deps(TARGET LuaCompiler COMPONENT Tools)
