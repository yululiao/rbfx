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

return_if_not_tool(AssetImporter)

if (MSVC)
    # Third-party warnings are irrelevant to us
    ucm_replace_flag("/W[0-3]" "/W0" REGEX)
endif ()

# Build ufbx as the static dependency library of the tool (the only FBX backend)
add_subdirectory (../../ThirdParty/ufbx ${CMAKE_CURRENT_BINARY_DIR}/../ThirdParty/ufbx)

file (GLOB SOURCE_FILES *.cpp *.h)
# AssetImporter is built as a dynamic library so the Editor can import FBX in-process.
# Public C API is defined in AssetImporterLibrary.h (ASSETIMPORTER_API export macro).
add_library (AssetImporter SHARED ${SOURCE_FILES})
target_compile_definitions (AssetImporter PRIVATE ASSETIMPORTER_LIBRARY_BUILD)
target_include_directories (AssetImporter SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../ThirdParty/ufbx
)
target_link_libraries (AssetImporter Urho3D ufbx)
# Install next to the Editor executable. Not part of the SDK export set
# because the third-party dependency (ufbx) is not exported.
install (TARGETS AssetImporter
    COMPONENT Tools
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR})

# Install runtime dependencies
install_target_runtime_deps(TARGET AssetImporter COMPONENT Tools)
