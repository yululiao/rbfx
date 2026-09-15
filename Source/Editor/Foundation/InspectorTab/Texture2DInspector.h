//
// Copyright (c) 2022-2022 the rbfx project.
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

#include "../../Assets/TextureImportSettings.h"
#include "../../Foundation/InspectorTab.h"
#include "../../Foundation/Shared/InspectorWithPreview.h"

#include <EASTL/functional.h>

namespace Urho3D
{

void Foundation_Texture2DInspector(Context* context, InspectorTab* inspectorTab);

/// Scene hierarchy provider for hierarchy browser tab.
class Texture2DInspector : public InspectorWithPreview
{
    URHO3D_OBJECT(Texture2DInspector, InspectorWithPreview)

public:
    explicit Texture2DInspector(Project* project);

protected:
    StringHash GetResourceType() const override;
    SharedPtr<ResourceInspectorWidget> MakeInspectorWidget(const ResourceVector& resources) override;
    SharedPtr<BaseWidget> MakePreviewWidget(Resource* resource) override;
    void RenderExtraInspectorContent() override;

private:
    /// Read the import params from the metadata file of the given (cache-relative) resource.
    /// Defaults when the file is absent.
    TextureImporterParams LoadParamsForResource(const ea::string& resourceName) const;
    /// Apply a field edit to the metadata file of every selected project-owned resource, then
    /// reload each texture so the preview shows the new settings immediately. The engine reads
    /// the very same file while loading, so there is nothing else to keep in sync.
    void SaveParamsForSelection(const ea::function<void(TextureImporterParams&)>& applyField);

    /// Resource names the cached params were loaded for.
    StringVector cachedResourceNames_;
    /// Import params currently shown (taken from the first selected resource).
    TextureImporterParams cachedParams_;
};

} // namespace Urho3D

