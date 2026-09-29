// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include "Urho3D/IO/MountedRemoteFiles.h"

#include "Urho3D/IO/Log.h"

#include <cstdlib>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>

// JavaScript library functions used by the EM_JS blocks below.
EM_JS_DEPS(rbfx_remote_files_deps, "$UTF8ToString,$stringToUTF8,$lengthBytesUTF8");

// The three calls below are the entire JavaScript contract of the file layer
// (minigame_files.js implements globalThis.rbfx.files):
//
//   size(path)       -> byte size, or -1 when the file does not exist;
//   readBytes(path)  -> Uint8Array | -1 (missing) | -2 (known but not local yet; the
//                       layer schedules a background fetch and answers the next time);
//   list(dir)        -> array of package-relative file names (recursive).
//
// The layer never touches the wasm heap: the copy is done by the wrappers here, which run
// inside the module and always have HEAPU8.

EM_JS(int, rbfx_files_size, (const char* path), {
    const files = globalThis.rbfx && globalThis.rbfx.files;
    if (!files || typeof files.size !== "function")
        return -1;
    try {
        return files.size(UTF8ToString(path)) | 0;
    } catch (error) {
        console.error("[rbfx] files.size failed", error);
        return -1;
    }
});

EM_JS(int, rbfx_files_read_into, (const char* path, unsigned char* dest, int maxSize), {
    const files = globalThis.rbfx && globalThis.rbfx.files;
    if (!files || typeof files.readBytes !== "function")
        return -1;
    const pathStr = UTF8ToString(path);
    let result;
    try {
        result = files.readBytes(pathStr, maxSize | 0);
    } catch (error) {
        console.error("[rbfx] files.readBytes failed for " + pathStr, error);
        return -1;
    }
    if (typeof result === "number")
        return result | 0;
    if (!result)
        return -1;
    if (result.length > (maxSize | 0)) {
        console.error("[rbfx] file grew past its manifest size: " + pathStr);
        return -3;
    }
    HEAPU8.set(result, dest);
    return result.length | 0;
});

EM_JS(char*, rbfx_files_list, (const char* path), {
    const files = globalThis.rbfx && globalThis.rbfx.files;
    const dir = UTF8ToString(path);
    let entries = null;
    if (files && typeof files.list === "function") {
        try {
            entries = files.list(dir);
        } catch (error) {
            console.error("[rbfx] files.list failed", error);
        }
    }
    const text = (entries && entries.length !== 0) ? entries.join("\n") : "";
    const lengthBytes = lengthBytesUTF8(text) + 1;
    const stringOnWasmHeap = _malloc(lengthBytes);
    stringToUTF8(text, stringOnWasmHeap, lengthBytes);
    return stringOnWasmHeap;
});

#else // defined(__EMSCRIPTEN__)

namespace
{

int rbfx_files_size(const char*) { return -1; }
int rbfx_files_read_into(const char*, unsigned char*, int) { return -1; }
char* rbfx_files_list(const char*) { return nullptr; }

}

#endif // defined(__EMSCRIPTEN__)

namespace Urho3D
{

MountedRemoteFiles::MountedRemoteFiles(Context* context, const ea::string& packageRoot)
    : MountPoint(context)
    , packageRoot_(packageRoot)
{
    // Tolerate trailing slashes; every name passed in later is relative to the root.
    while (!packageRoot_.empty() && (packageRoot_.back() == '/' || packageRoot_.back() == '\\'))
        packageRoot_.pop_back();

    name_ = packageRoot_.empty() ? ea::string{"MinigamePackage"} : ea::string{"MinigamePackage:"} + packageRoot_;
}

MountedRemoteFiles::~MountedRemoteFiles() = default;

void MountedRemoteFiles::ClearCache()
{
    cache_.clear();
    cacheSize_ = 0;
}

bool MountedRemoteFiles::AcceptsScheme(const ea::string& scheme) const
{
    // Package files are addressed by relative paths, i.e. the empty scheme.
    return scheme.empty();
}

bool MountedRemoteFiles::Exists(const FileIdentifier& fileName) const
{
    if (!AcceptsScheme(fileName.scheme_))
        return false;

    const ea::string name = ExpandName(fileName.fileName_);
    if (cache_.contains(name))
        return true;

    return rbfx_files_size(name.c_str()) >= 0;
}

AbstractFilePtr MountedRemoteFiles::OpenFile(const FileIdentifier& fileName, FileMode mode)
{
    // The package is shipped read-only; writable storage is a separate concern (user data).
    if (mode != FILE_READ)
        return nullptr;

    if (!AcceptsScheme(fileName.scheme_))
        return nullptr;

    if (const SharedPtr<OwnedMemoryBlock> block = GetOrLoadFile(ExpandName(fileName.fileName_)))
    {
        // The file name is part of the open contract, same as in MountedDirectory: consumers
        // such as the shader include resolver rebuild dependent names from GetPath(GetName()),
        // so an unnamed stream turns every include into a schemeless bare name that no mount
        // point can resolve.
        const auto view = MakeShared<MemoryBlockView>(block);
        view->SetName(fileName.ToUri());
        return view;
    }

    return nullptr;
}

const ea::string& MountedRemoteFiles::GetName() const
{
    return name_;
}

void MountedRemoteFiles::Scan(
    ea::vector<ea::string>& result, const ea::string& pathName, const ea::string& filter, ScanFlags flags) const
{
    if (!flags.Test(SCAN_APPEND))
        result.clear();

    char* rawList = rbfx_files_list(ExpandName(pathName).c_str());
    if (!rawList)
        return;

    const ea::string list{rawList};
    std::free(rawList);

    const bool recursive = flags.Test(SCAN_RECURSIVE);
    const ea::string filterExtension = GetExtensionFromFilter(filter);

    const StringVector files = list.split('\n');
    for (const ea::string& entry : files)
    {
        // The JavaScript layer answers with package-relative names; fold the package root
        // away so the result stays relative to this mount point.
        if (entry.empty() || (!packageRoot_.empty() && !entry.starts_with(packageRoot_)))
            continue;

        const ea::string name = packageRoot_.empty() ? entry : TrimPathPrefix(entry, packageRoot_);
        if (MatchFileName(name, pathName, filterExtension, recursive))
            result.push_back(TrimPathPrefix(name, pathName));
    }
}

ea::string MountedRemoteFiles::ExpandName(const ea::string& fileName) const
{
    if (packageRoot_.empty())
        return fileName;
    if (fileName.empty())
        return packageRoot_;
    return packageRoot_ + "/" + fileName;
}

SharedPtr<MountedRemoteFiles::OwnedMemoryBlock> MountedRemoteFiles::GetOrLoadFile(const ea::string& fileName)
{
    const auto cached = cache_.find(fileName);
    if (cached != cache_.end())
    {
        cached->second.lastUse_ = ++useCounter_;
        return cached->second.block_;
    }

    const int size = rbfx_files_size(fileName.c_str());
    if (size < 0)
        return nullptr;

    unsigned char* data = static_cast<unsigned char*>(std::malloc(size > 0 ? static_cast<unsigned>(size) : 1));
    if (!data)
        return nullptr;

    const int read = rbfx_files_read_into(fileName.c_str(), data, size);
    if (read < 0)
    {
        std::free(data);
        if (read == -2)
        {
            // Known to the manifest but not local yet: the JavaScript layer has scheduled
            // a background fetch and reports the outcome through MinigamePrefetched. A
            // failed open is not cached by the resource system, so the game simply issues
            // the load again once the file is reported ready.
            URHO3D_LOGWARNING("MountedRemoteFiles: '{}' is not local yet, a background fetch was scheduled", fileName);
        }
        return nullptr;
    }

    const SharedPtr<OwnedMemoryBlock> block = MakeShared<OwnedMemoryBlock>(data, static_cast<unsigned>(read));
    InsertCache(fileName, block);
    return block;
}

void MountedRemoteFiles::InsertCache(const ea::string& fileName, const SharedPtr<OwnedMemoryBlock>& block)
{
    const unsigned size = block->Size();

    // A single file larger than the whole budget is served but never retained.
    if (size > cacheBudget_)
        return;

    while (cacheSize_ + size > cacheBudget_ && !cache_.empty())
    {
        auto victim = cache_.begin();
        for (auto it = cache_.begin(); it != cache_.end(); ++it)
        {
            if (it->second.lastUse_ < victim->second.lastUse_)
                victim = it;
        }
        cacheSize_ -= victim->second.block_->Size();
        cache_.erase(victim);
    }

    cacheSize_ += size;
    CachedFile& entry = cache_[fileName];
    entry.block_ = block;
    entry.lastUse_ = ++useCounter_;
}

} // namespace Urho3D
