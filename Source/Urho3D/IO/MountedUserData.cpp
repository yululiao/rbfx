// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include "Urho3D/IO/MountedUserData.h"

#include "Urho3D/Container/ByteVector.h"
#include "Urho3D/IO/Log.h"

#include <cstdlib>
#include <cstring>
#include <utility>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>

// JavaScript library functions used by the EM_JS blocks below.
EM_JS_DEPS(rbfx_user_data_deps, "$UTF8ToString,$stringToUTF8,$lengthBytesUTF8");

// The JavaScript side of the user storage (minigame_files.js) implements
// globalThis.rbfx.files.userAvailable / userSize / userRead / userWrite / userRemove /
// userList. All calls are synchronous: userRead returns a Uint8Array, userWrite receives a
// Uint8Array view over the wasm heap which it must copy before returning.
//
// The wrappers here are the only code that touches the wasm heap; the JavaScript layer
// never does.

EM_JS(int, rbfx_user_available, (), {
    const files = globalThis.rbfx && globalThis.rbfx.files;
    return (files && typeof files.userAvailable === "function" && files.userAvailable()) ? 1 : 0;
});

EM_JS(int, rbfx_user_size, (const char* path), {
    const files = globalThis.rbfx && globalThis.rbfx.files;
    if (!files || typeof files.userSize !== "function")
        return -1;
    try {
        return files.userSize(UTF8ToString(path)) | 0;
    } catch (error) {
        console.error("[rbfx] user storage size failed", error);
        return -1;
    }
});

EM_JS(int, rbfx_user_read_into, (const char* path, unsigned char* dest, int maxSize), {
    const files = globalThis.rbfx && globalThis.rbfx.files;
    if (!files || typeof files.userRead !== "function")
        return -1;
    const pathStr = UTF8ToString(path);
    let result;
    try {
        result = files.userRead(pathStr, maxSize | 0);
    } catch (error) {
        console.error("[rbfx] user storage read failed for " + pathStr, error);
        return -2;
    }
    if (typeof result === "number")
        return result | 0;
    if (!result)
        return -1;
    if (result.length > (maxSize | 0)) {
        console.error("[rbfx] user storage file grew past its reported size: " + pathStr);
        return -2;
    }
    HEAPU8.set(result, dest);
    return result.length | 0;
});

EM_JS(int, rbfx_user_write, (const char* path, const unsigned char* src, int size), {
    const files = globalThis.rbfx && globalThis.rbfx.files;
    if (!files || typeof files.userWrite !== "function")
        return -1;
    const pathStr = UTF8ToString(path);
    try {
        return files.userWrite(pathStr, HEAPU8.subarray(src, src + (size | 0))) ? 0 : -1;
    } catch (error) {
        console.error("[rbfx] user storage write failed for " + pathStr, error);
        return -1;
    }
});

EM_JS(int, rbfx_user_remove, (const char* path), {
    const files = globalThis.rbfx && globalThis.rbfx.files;
    if (!files || typeof files.userRemove !== "function")
        return -1;
    try {
        return files.userRemove(UTF8ToString(path)) ? 0 : -1;
    } catch (error) {
        console.error("[rbfx] user storage remove failed", error);
        return -1;
    }
});

EM_JS(char*, rbfx_user_list, (), {
    const files = globalThis.rbfx && globalThis.rbfx.files;
    let entries = null;
    if (files && typeof files.userList === "function") {
        try {
            entries = files.userList();
        } catch (error) {
            console.error("[rbfx] user storage list failed", error);
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

int rbfx_user_available() { return 0; }
int rbfx_user_size(const char*) { return -1; }
int rbfx_user_read_into(const char*, unsigned char*, int) { return -1; }
int rbfx_user_write(const char*, const unsigned char*, int) { return -1; }
int rbfx_user_remove(const char*) { return -1; }
char* rbfx_user_list() { return nullptr; }

}

#endif // defined(__EMSCRIPTEN__)

namespace Urho3D
{

namespace
{

/// A file buffered in memory and flushed to the host storage on close or destruction.
///
/// The host file APIs are synchronous, so Flush is a straight marshalling call. A failed
/// flush leaves the host copy unchanged and logs the error; nothing is queued.
class VendorStorageFile : public RefCounted, public AbstractFile
{
public:
    VendorStorageFile(ea::string path, ByteVector data, bool dirty)
        : path_(std::move(path))
        , data_(std::move(data))
        , dirty_(dirty)
    {
        size_ = static_cast<unsigned>(data_.size());
    }

    ~VendorStorageFile() override
    {
        Flush();
    }

    /// Read bytes. Return number of bytes actually read.
    unsigned Read(void* dest, unsigned size) override
    {
        if (position_ >= size_)
            return 0;

        const unsigned available = size_ - position_;
        const unsigned readSize = size < available ? size : available;
        std::memcpy(dest, data_.data() + position_, readSize);
        position_ += readSize;
        return readSize;
    }

    /// Set position from the beginning of the file. Return actual new position.
    unsigned Seek(unsigned position) override
    {
        position_ = position < size_ ? position : size_;
        return position_;
    }

    /// Write bytes to the buffer. The host storage is updated when the file is flushed.
    unsigned Write(const void* data, unsigned size) override
    {
        const unsigned end = position_ + size;
        if (end > static_cast<unsigned>(data_.size()))
            data_.resize(end);
        std::memcpy(data_.data() + position_, data, size);
        position_ = end;
        size_ = static_cast<unsigned>(data_.size());
        dirty_ = true;
        return size;
    }

    /// Write the buffer back to the host storage if it was modified.
    void Flush()
    {
        if (!dirty_)
            return;

        dirty_ = false;
        if (rbfx_user_write(path_.c_str(), data_.data(), static_cast<int>(data_.size())) != 0)
            URHO3D_LOGERROR("MountedUserData: failed to write '{}'", path_);
        else
            URHO3D_LOGDEBUG("MountedUserData: wrote '{}' ({} bytes)", path_, data_.size());
    }

    /// Implement AbstractFile.
    /// @{
    bool IsOpen() const override { return open_; }
    void Close() override
    {
        Flush();
        open_ = false;
    }
    /// @}

private:
    /// Path within the user data directory.
    ea::string path_;
    /// File contents.
    ByteVector data_;
    /// Whether the buffer differs from the host storage.
    bool dirty_{};
    /// Whether the file is still open.
    bool open_{true};
};

} // namespace

MountedUserData::MountedUserData(Context* context)
    : MountPoint(context)
{
}

MountedUserData::~MountedUserData() = default;

bool MountedUserData::IsAvailable() const
{
    return rbfx_user_available() != 0;
}

bool MountedUserData::Delete(const FileIdentifier& fileName)
{
    if (!AcceptsScheme(fileName.scheme_))
        return false;

    return rbfx_user_remove(fileName.fileName_.c_str()) == 0;
}

bool MountedUserData::AcceptsScheme(const ea::string& scheme) const
{
    return scheme == name_;
}

bool MountedUserData::Exists(const FileIdentifier& fileName) const
{
    if (!AcceptsScheme(fileName.scheme_))
        return false;

    return rbfx_user_size(fileName.fileName_.c_str()) >= 0;
}

AbstractFilePtr MountedUserData::OpenFile(const FileIdentifier& fileName, FileMode mode)
{
    if (!AcceptsScheme(fileName.scheme_))
        return nullptr;

    ByteVector data;

    // Opening for writing truncates, matching a regular file system; the (possibly empty)
    // buffer is written through on close. Other modes load the existing contents: read
    // requires the file to exist, read-write starts from empty otherwise.
    if (mode != FILE_WRITE)
    {
        const int size = rbfx_user_size(fileName.fileName_.c_str());
        if (size < 0)
        {
            if (mode == FILE_READ)
                return nullptr;
        }
        else
        {
            data.resize(static_cast<unsigned>(size));
            if (size > 0)
            {
                const int read = rbfx_user_read_into(fileName.fileName_.c_str(), data.data(), size);
                if (read < 0)
                {
                    URHO3D_LOGWARNING("MountedUserData: failed to read '{}'", fileName.fileName_);
                    return nullptr;
                }
                data.resize(static_cast<unsigned>(read));
            }
        }
    }

    return MakeShared<VendorStorageFile>(fileName.fileName_, std::move(data), mode == FILE_WRITE);
}

const ea::string& MountedUserData::GetName() const
{
    return name_;
}

void MountedUserData::Scan(
    ea::vector<ea::string>& result, const ea::string& pathName, const ea::string& filter, ScanFlags flags) const
{
    if (!flags.Test(SCAN_APPEND))
        result.clear();

    char* rawList = rbfx_user_list();
    if (!rawList)
        return;

    const ea::string list{rawList};
    std::free(rawList);

    const bool recursive = flags.Test(SCAN_RECURSIVE);
    const ea::string filterExtension = GetExtensionFromFilter(filter);

    const StringVector files = list.split('\n');
    for (const ea::string& name : files)
    {
        if (name.empty())
            continue;
        if (MatchFileName(name, pathName, filterExtension, recursive))
            result.push_back(TrimPathPrefix(name, pathName));
    }
}

} // namespace Urho3D
