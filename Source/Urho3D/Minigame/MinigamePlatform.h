// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include "Urho3D/Core/Object.h"

namespace Urho3D
{

/// Platform layer for minigame hosts (Douyin/WeChat style runtimes).
///
/// The host runtime owns the application lifecycle: there is no window, no OS event queue
/// and nothing reaches the engine unless the JavaScript bootstrap forwards it. This
/// subsystem owns the C++ side of that bridge:
///
///  * installs the native hooks the bootstrap calls into (InstallNativeHooks) and turns
///    host lifecycle notifications - show/hide, memory warning, WebGL context loss - into
///    ordinary engine events, so game code subscribes the same way on every platform;
///  * exposes the host-controlled user data directory, the host "exit game" call and the
///    background file prefetch request.
///
/// Outside a host (desktop, editor, tests) the class is inert: the hooks report unavailable
/// and host calls are no-ops with a log line, keeping the subsystem surface uniform
/// everywhere so game code and Lua bindings do not need platform checks.
class URHO3D_API MinigamePlatform : public Object
{
    URHO3D_OBJECT(MinigamePlatform, Object)

public:
    /// Construct. The subsystem does nothing until InstallNativeHooks() is called.
    explicit MinigamePlatform(Context* context);
    /// Destruct.
    ~MinigamePlatform() override;

    /// Install the native hooks the JavaScript bootstrap calls into and detect whether a
    /// host runtime is attached. Call once after registering the subsystem, before the
    /// first frame. Returns true if the hooks were installed.
    bool InstallNativeHooks();

    /// The host resumed the game. Sends MinigameShow. Normally invoked through the
    /// installed hooks; public so a custom JavaScript host can drive it directly.
    void NotifyShow();
    /// The host sent the game to the background. Sends MinigameHide.
    void NotifyHide();
    /// The host reported memory pressure at the given vendor level. Sends
    /// MinigameMemoryWarning.
    void NotifyMemoryWarning(int level);
    /// The rendering context was lost. Sends MinigameContextLost.
    void NotifyContextLost();
    /// A background prefetch finished. Sends MinigamePrefetched.
    void NotifyPrefetchFinished(int readyFiles, int failedFiles);

    /// Return the writable directory the host assigns to this game (a vendor path, not an
    /// engine URI). Empty when no host is attached.
    ea::string GetUserDataPath() const;

    /// Ask the host to close the game. The minigame runtimes forbid terminating the
    /// process directly, so this is the only supported way to quit.
    void ExitGame(int code = 0) const;

    /// Queue background download/population of the given package-relative file names. The
    /// call returns immediately; completion is reported with MinigamePrefetched. Files
    /// that are already local complete instantly. Prefetching is a hint, not a
    /// requirement: synchronous reads keep working for everything already on disk.
    void PrefetchFiles(const StringVector& fileNames);

    /// Return whether the host hooks are installed.
    /// @property
    bool IsHostAvailable() const { return hostAvailable_; }

    /// Return whether the host currently displays the game.
    /// @property
    bool IsForeground() const { return isForeground_; }

private:
    /// Whether the JavaScript bootstrap accepted the native hooks.
    bool hostAvailable_{};
    /// Whether the host currently shows the game.
    bool isForeground_{true};
};

} // namespace Urho3D
