//
// Copyright (c) 2024-2024 the rbfx project.
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
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../Precompiled.h"

#include "CookedTextureRouter.h"

#include "Urho3D/Engine/ApplicationFlavor.h"
#include "Urho3D/IO/FileSystem.h"
#include "Urho3D/IO/VirtualFileSystem.h"

#include "../DebugNew.h"

namespace Urho3D
{

CookedTextureRouter::CookedTextureRouter(Context* context)
    : ResourceRouter(context)
{
}

void CookedTextureRouter::Route(FileIdentifier& name)
{
    // Only texture source files are candidates for redirection. Everything else is left untouched.
    static const char* const textureSourceExtensions[] = {".png", ".jpg", ".jpeg", ".bmp", ".tga"};
    const ea::string extension = GetExtension(name.fileName_);

    bool isTextureSource = false;
    for (const char* const candidateExtension : textureSourceExtensions)
    {
        if (extension == candidateExtension)
        {
            isTextureSource = true;
            break;
        }
    }
    if (!isTextureSource)
        return;

    auto* vfs = GetSubsystem<VirtualFileSystem>();
    if (!vfs)
        return;

    // Cooked containers in probe order, most preferred first for the current platform. The platform is
    // fixed for the process lifetime, so it is resolved once. Mobile and web share the KTX-first
    // order: ETC2 inside KTX is the one compressed combination WebGL2 guarantees and mobile GPUs
    // read natively, which is also what the build profiles of both platforms cook.
    static const bool prefersKtx =
        ApplicationFlavor::Platform.Matches(ApplicationFlavorPattern{"platform=mobile"}).has_value()
        || ApplicationFlavor::Platform.Matches(ApplicationFlavorPattern{"platform=web"}).has_value();
    static const char* const desktopContainers[] = {".dds", ".ktx", ".pvr"};
    static const char* const ktxFirstContainers[] = {".ktx", ".pvr", ".dds"};

    const char* const* containers = prefersKtx ? ktxFirstContainers : desktopContainers;
    const unsigned numContainers = sizeof(desktopContainers) / sizeof(desktopContainers[0]);

    for (unsigned i = 0; i < numContainers; ++i)
    {
        FileIdentifier candidate{name.scheme_, ReplaceExtension(name.fileName_, containers[i])};
        if (vfs->Exists(candidate))
        {
            name = candidate;
            return;
        }
    }

    // No cooked product found: keep the original source identifier (e.g. editor with only sources present).
}

}
