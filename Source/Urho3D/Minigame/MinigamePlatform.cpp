// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include "Urho3D/Minigame/MinigamePlatform.h"

#include "Urho3D/IO/Log.h"
#include "Urho3D/Minigame/MinigameEvents.h"

#include <cstdlib>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#if defined(__EMSCRIPTEN__)

// JavaScript library functions used by the EM_JS blocks below.
EM_JS_DEPS(rbfx_minigame_platform_deps, "$UTF8ToString,$stringToUTF8,$lengthBytesUTF8");

// Installed into globalThis.rbfx.native. The JavaScript bootstrap is expected to call these
// when the host reports the corresponding lifecycle change; each closure forwards into an
// exported C entry point. The "_" prefix on the callees is the emscripten binding name of
// the C symbols below (asmjs_mangle convention), not part of the C names.
EM_JS(int, rbfx_install_platform_hooks, (), {
    const g = globalThis;
    if (!g.rbfx) {
        if (typeof console !== "undefined")
            console.warn("[rbfx] no JavaScript bootstrap found, host hooks are not installed");
        return 0;
    }
    const natives = g.rbfx.native = g.rbfx.native || {};
    natives.on_show = function () { _rbfx_minigame_on_show(); };
    natives.on_hide = function () { _rbfx_minigame_on_hide(); };
    natives.on_memory_warning = function (level) { _rbfx_minigame_on_memory_warning(level | 0); };
    natives.on_context_lost = function () { _rbfx_minigame_on_context_lost(); };
    natives.on_prefetch_done = function (ready, failed) { _rbfx_minigame_on_prefetch_done(ready | 0, failed | 0); };
    return 1;
});

// The vendor path of the user data directory, or an empty string. Allocated on the wasm
// heap; the caller frees it.
EM_JS(char*, rbfx_minigame_user_data_path, (), {
    const runtime = globalThis.rbfx && globalThis.rbfx.runtime;
    let path = "";
    if (runtime && typeof runtime.userDataPath === "function") {
        try {
            path = String(runtime.userDataPath());
        } catch (error) {
            path = "";
        }
    }
    const lengthBytes = lengthBytesUTF8(path) + 1;
    const stringOnWasmHeap = _malloc(lengthBytes);
    stringToUTF8(path, stringOnWasmHeap, lengthBytes);
    return stringOnWasmHeap;
});

// Ask the host to close the game. Returns 1 when the runtime accepted the request.
EM_JS(int, rbfx_minigame_exit, (int code), {
    const runtime = globalThis.rbfx && globalThis.rbfx.runtime;
    if (!runtime || typeof runtime.exit !== "function")
        return 0;
    try {
        runtime.exit(code | 0);
    } catch (error) {
        console.error("[rbfx] runtime exit failed", error);
        return 0;
    }
    return 1;
});

// Hand a batch of file names (newline-separated) to the JavaScript file layer for
// background population. Returns 1 when the file layer accepted the request.
EM_JS(int, rbfx_minigame_prefetch, (const char* paths), {
    const files = globalThis.rbfx && globalThis.rbfx.files;
    if (!files || typeof files.prefetch !== "function")
        return 0;
    const entries = UTF8ToString(paths).split("\n").filter(function (entry) { return entry.length !== 0; });
    files.prefetch(entries);
    return 1;
});

#endif // defined(__EMSCRIPTEN__)

namespace Urho3D
{

namespace
{

/// The subsystem instance the native hooks forward into. The module owns a single
/// Context, so a plain pointer is enough; it is cleared by the destructor.
MinigamePlatform* platform = nullptr;

}

// Native entry points the hooks forward into. extern "C" keeps the names exact (the
// JavaScript side calls the "_"-prefixed binding name); the keepalive attribute keeps the
// functions in the wasm export table even though only JavaScript references them.
extern "C"
{

#if defined(__EMSCRIPTEN__)
#define RBFX_MINIGAME_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define RBFX_MINIGAME_KEEPALIVE
#endif

void RBFX_MINIGAME_KEEPALIVE rbfx_minigame_on_show()
{
    if (platform)
        platform->NotifyShow();
}

void RBFX_MINIGAME_KEEPALIVE rbfx_minigame_on_hide()
{
    if (platform)
        platform->NotifyHide();
}

void RBFX_MINIGAME_KEEPALIVE rbfx_minigame_on_memory_warning(int level)
{
    if (platform)
        platform->NotifyMemoryWarning(level);
}

void RBFX_MINIGAME_KEEPALIVE rbfx_minigame_on_context_lost()
{
    if (platform)
        platform->NotifyContextLost();
}

void RBFX_MINIGAME_KEEPALIVE rbfx_minigame_on_prefetch_done(int readyFiles, int failedFiles)
{
    if (platform)
        platform->NotifyPrefetchFinished(readyFiles, failedFiles);
}

#undef RBFX_MINIGAME_KEEPALIVE

} // extern "C"

MinigamePlatform::MinigamePlatform(Context* context)
    : Object(context)
{
}

MinigamePlatform::~MinigamePlatform()
{
    if (platform == this)
        platform = nullptr;
}

bool MinigamePlatform::InstallNativeHooks()
{
    platform = this;

#if defined(__EMSCRIPTEN__)
    hostAvailable_ = rbfx_install_platform_hooks() != 0;
#else
    hostAvailable_ = false;
#endif

    if (!hostAvailable_)
        URHO3D_LOGINFO("MinigamePlatform: no host runtime detected, platform calls are inert");

    return hostAvailable_;
}

void MinigamePlatform::NotifyShow()
{
    isForeground_ = true;
    SendEvent(E_MINIGAMESHOW);
}

void MinigamePlatform::NotifyHide()
{
    isForeground_ = false;
    SendEvent(E_MINIGAMEHIDE);
}

void MinigamePlatform::NotifyMemoryWarning(int level)
{
    using namespace MinigameMemoryWarning;

    VariantMap& eventData = GetEventDataMap();
    eventData[P_LEVEL] = level;
    SendEvent(E_MINIGAMEMEMORYWARNING, eventData);
}

void MinigamePlatform::NotifyContextLost()
{
    SendEvent(E_MINIGAMECONTEXTLOST);
    URHO3D_LOGERROR("MinigamePlatform: the rendering context was lost");
}

void MinigamePlatform::NotifyPrefetchFinished(int readyFiles, int failedFiles)
{
    if (failedFiles > 0)
        URHO3D_LOGWARNING("MinigamePlatform: prefetch finished, {} ready, {} failed", readyFiles, failedFiles);

    using namespace MinigamePrefetched;

    VariantMap& eventData = GetEventDataMap();
    eventData[P_READY] = readyFiles;
    eventData[P_FAILED] = failedFiles;
    SendEvent(E_MINIGAMEPREFETCHED, eventData);
}

ea::string MinigamePlatform::GetUserDataPath() const
{
#if defined(__EMSCRIPTEN__)
    char* rawPath = rbfx_minigame_user_data_path();
    if (!rawPath)
        return EMPTY_STRING;

    const ea::string result{rawPath};
    std::free(rawPath);
    return result;
#else
    return EMPTY_STRING;
#endif
}

void MinigamePlatform::ExitGame(int code) const
{
#if defined(__EMSCRIPTEN__)
    if (rbfx_minigame_exit(code) == 0)
        URHO3D_LOGWARNING("MinigamePlatform: no host exit path is available");
#else
    (void)code;
#endif
}

void MinigamePlatform::PrefetchFiles(const StringVector& fileNames)
{
    if (fileNames.empty())
        return;

#if defined(__EMSCRIPTEN__)
    ea::string joined;
    for (const ea::string& fileName : fileNames)
    {
        if (fileName.empty())
            continue;
        joined += fileName;
        joined += '\n';
    }

    if (joined.empty())
        return;

    if (rbfx_minigame_prefetch(joined.c_str()) == 0)
        URHO3D_LOGWARNING("MinigamePlatform: the file bridge is not available, prefetch ignored");
#else
    (void)fileNames;
#endif
}

} // namespace Urho3D
