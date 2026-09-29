// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include "Urho3D/Core/Object.h"
#include "Urho3D/IO/AbstractFile.h"
#include "Urho3D/IO/MemoryBuffer.h"
#include "Urho3D/IO/MountPoint.h"

#include <EASTL/unordered_map.h>

#include <cstdlib>

namespace Urho3D
{

/// Read-only mount point serving the minigame package through the JavaScript file layer.
///
/// On the vendor runtimes the game files live in the package (main package plus downloaded
/// subpackages), and the only file API that can reach them is the synchronous
/// readFileSync. This mount point is the engine-side end of that channel: paths resolve in
/// the empty scheme (the same relative names the package was assembled with), reads copy
/// the bytes into the wasm heap, and everything above the IO layer - ResourceCache, pak
/// files, scene loading - stays fully synchronous.
///
/// Files larger than the cache budget are served once and not retained; everything else is
/// kept in an LRU of whole files, because a resource system re-opens the same headers many
/// times and a per-open readFileSync round trip is the one thing worth avoiding.
///
/// One mount point serves one package namespace: each resource path the host was configured
/// with ("CoreData", "Data", ...) gets a mount point rooted at that directory inside the
/// package, exactly like MountedDirectory does for real folders. Resource names resolve the
/// same way they do on desktop, and later mounts take priority as usual.
///
/// Outside a web build the mount point reports no files: there is no JavaScript layer to
/// ask and no package to read.
class URHO3D_API MountedRemoteFiles : public MountPoint
{
    URHO3D_OBJECT(MountedRemoteFiles, MountPoint)

public:
    /// Construct with the given context, rooted at the given directory inside the package.
    /// An empty root makes the mount point cover the whole package.
    explicit MountedRemoteFiles(Context* context, const ea::string& packageRoot = EMPTY_STRING);
    /// Destruct.
    ~MountedRemoteFiles() override;

    /// Set the whole-file cache budget in bytes. Default 64 MiB.
    /// @property
    void SetCacheBudget(unsigned bytes) { cacheBudget_ = bytes; }
    /// Return the cache budget in bytes.
    /// @property
    unsigned GetCacheBudget() const { return cacheBudget_; }
    /// Return the number of bytes currently held by the cache.
    /// @property
    unsigned GetCacheSize() const { return cacheSize_; }
    /// Drop all cached files.
    void ClearCache();

    /// Implement MountPoint.
    /// @{
    bool AcceptsScheme(const ea::string& scheme) const override;
    bool Exists(const FileIdentifier& fileName) const override;
    AbstractFilePtr OpenFile(const FileIdentifier& fileName, FileMode mode) override;

    const ea::string& GetName() const override;

    void Scan(ea::vector<ea::string>& result, const ea::string& pathName, const ea::string& filter,
        ScanFlags flags) const override;
    /// @}

private:
    /// Reference-counted block of wasm heap memory, freed with the system allocator.
    /// Shared by every open handle of a cached file, so eviction from the cache cannot
    /// invalidate a file that is still open.
    class OwnedMemoryBlock : public RefCounted
    {
    public:
        OwnedMemoryBlock(unsigned char* data, unsigned size)
            : data_(data)
            , size_(size)
        {
        }

        ~OwnedMemoryBlock() override { std::free(data_); }

        unsigned char* Data() const { return data_; }
        unsigned Size() const { return size_; }

    private:
        unsigned char* data_{};
        unsigned size_{};
    };

    /// A per-open view over a shared block: fresh stream position, shared bytes.
    class MemoryBlockView : public RefCounted, public MemoryBuffer
    {
    public:
        explicit MemoryBlockView(const SharedPtr<OwnedMemoryBlock>& block)
            : block_(block)
            , MemoryBuffer(block->Data(), block->Size())
        {
        }

    private:
        SharedPtr<OwnedMemoryBlock> block_;
    };

    /// One cached file: the shared byte block and its LRU stamp.
    struct CachedFile
    {
        SharedPtr<OwnedMemoryBlock> block_;
        unsigned lastUse_{};
    };

    /// Map a mount-point-relative name onto its package-relative name.
    ea::string ExpandName(const ea::string& fileName) const;
    /// Return the file from the cache, or read it into the wasm heap and cache it.
    /// Takes a package-relative name.
    SharedPtr<OwnedMemoryBlock> GetOrLoadFile(const ea::string& fileName);
    /// Account a freshly loaded file and evict least recently used entries as needed.
    void InsertCache(const ea::string& fileName, const SharedPtr<OwnedMemoryBlock>& block);

    /// Cached files, keyed by package-relative name.
    ea::unordered_map<ea::string, CachedFile> cache_;
    /// Monotonic LRU stamp source.
    unsigned useCounter_{};
    /// Whole-file cache budget in bytes.
    unsigned cacheBudget_{64u * 1024u * 1024u};
    /// Bytes currently held by the cache.
    unsigned cacheSize_{};
    /// Directory inside the package this mount point is rooted at, if any.
    ea::string packageRoot_;
    /// Human-readable name of the mount point.
    ea::string name_{"MinigamePackage"};
};

} // namespace Urho3D
