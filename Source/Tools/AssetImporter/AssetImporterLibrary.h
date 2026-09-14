//
// Copyright (c) 2017-2025 the rbfx project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#pragma once

// Public C API of the AssetImporter dynamic library.
// The Editor links this library and imports FBX in-process,
// which makes import failures directly debuggable in the Editor debugger session.
// Only POD types are exchanged across the library boundary.

#if defined(_WIN32)
    #ifdef ASSETIMPORTER_LIBRARY_BUILD
        #define ASSETIMPORTER_API __declspec(dllexport)
    #else
        #define ASSETIMPORTER_API __declspec(dllimport)
    #endif
#else
    #define ASSETIMPORTER_API __attribute__((visibility("default")))
#endif

extern "C"
{

/// Content classification of an FBX, detected by parsing its ufbx scene rather than by any
/// file-name convention. Reported as a bitmask after an "import" run through
/// AssetImporterGetLastImportContent(), so the host can build its resource mapping (for example
/// the Cache satellite layout) from the actual content: a file may carry only a mesh, only
/// animations, or both.
enum AssetImporterContentType
{
    ASSET_IMPORTER_CONTENT_MODEL     = 1 << 0, // FBX has at least one mesh (geometry)
    ASSET_IMPORTER_CONTENT_ANIMATION = 1 << 1, // FBX has at least one animation stack
};

/// Run AssetImporter in-process.
/// Arguments correspond to the tool command line without the executable name,
/// e.g. {"import", "input.fbx", "path/to/asset.fbx.d/", "-nm", "-nt"}.
/// Calls are not concurrent-safe and must be serialized by the caller.
/// The host engine Context is reused if present (rbfx Context is a process-wide
/// singleton), otherwise a private one is created on first use.
/// Returns exit code: 0 on success, non-zero on failure.
ASSETIMPORTER_API int AssetImporterRun(int numArguments, const char** arguments);

/// Return message of the last error that caused AssetImporterRun to fail.
/// The returned pointer is valid until the next AssetImporterRun call. Never null.
ASSETIMPORTER_API const char* AssetImporterGetLastError();

/// Bitmask of AssetImporterContentType detected by the most recent successful "import" run.
/// Returns 0 if the last run was not an auto-detected import or produced nothing.
ASSETIMPORTER_API unsigned AssetImporterGetLastImportContent();

}
