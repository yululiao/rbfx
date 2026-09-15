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

#pragma once

#include "Urho3D/Resource/ResourceCache.h"

namespace Urho3D
{

/// Resource router that transparently redirects texture source requests to platform-specific cooked products.
/// Materials keep referencing the original source name (e.g. "Textures/foo.png"). At runtime, if a cooked
/// product for the current platform exists next to it (e.g. "Textures/foo.dds" on desktop or ".ktx" on mobile),
/// the request is rewritten to that product. When no cooked product exists (e.g. in the editor, where only the
/// source is present), the identifier is left untouched and the source is loaded as usual.
class URHO3D_API CookedTextureRouter : public ResourceRouter
{
    URHO3D_OBJECT(CookedTextureRouter, ResourceRouter);

public:
    /// Construct.
    explicit CookedTextureRouter(Context* context);

    /// Redirect a texture source request to its cooked product when one exists for the current platform.
    void Route(FileIdentifier& name) override;
};

}
