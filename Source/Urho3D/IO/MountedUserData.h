// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include "Urho3D/Core/Object.h"
#include "Urho3D/IO/AbstractFile.h"
#include "Urho3D/IO/MountPoint.h"

namespace Urho3D
{

/// Read-write mount point backed by the host's persistent storage, registered as "user".
///
/// The host runtimes hand the game a private persistent directory, and their file APIs can
/// reach it synchronously. A file opened here is buffered in memory for the lifetime of the
/// handle and written back through the host storage when the handle is closed or dropped,
/// which keeps the whole write path in the engine synchronous as well. The volumes are
/// small - save games, settings, fetched manifests - so whole-file buffering is the right
/// model and no partial-write protocol is needed.
///
/// Outside a web build the host storage does not exist: the mount point reports no files
/// and refuses to open any.
class URHO3D_API MountedUserData : public MountPoint
{
    URHO3D_OBJECT(MountedUserData, MountPoint)

public:
    /// Construct. The mount point answers the "user" scheme.
    explicit MountedUserData(Context* context);
    /// Destruct.
    ~MountedUserData() override;

    /// Return whether the host storage is available. False on desktop and in web builds
    /// hosted without the JavaScript bootstrap.
    /// @property
    bool IsAvailable() const;

    /// Delete a file from the host storage. Return true if the file existed and was removed.
    bool Delete(const FileIdentifier& fileName);

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
    /// Human-readable name of the mount point and the accepted scheme.
    ea::string name_{"user"};
};

} // namespace Urho3D
