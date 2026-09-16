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

#include "../../Tabs/InspectorTab/FbxAssetInspector.h"

#include "../../Assets/FbxImport.h"
#include "../../Project/Project.h"

#include <IconFontCppHeaders/IconsFontAwesome6.h>

namespace Urho3D
{

void Tabs_FbxAssetInspector(Context* context, InspectorTab* inspectorTab)
{
    inspectorTab->RegisterAddon<FbxAssetInspector>(inspectorTab->GetProject());
}

FbxAssetInspector::FbxAssetInspector(Project* project)
    : Object(project->GetContext())
    , project_(project)
{
    project_->OnRequest.Subscribe(this, &FbxAssetInspector::OnProjectRequest);
}

void FbxAssetInspector::OnProjectRequest(ProjectRequest* request)
{
    auto inspectResourceRequest = dynamic_cast<InspectResourceRequest*>(request);
    if (!inspectResourceRequest || inspectResourceRequest->GetResources().empty())
        return;

    const auto& resources = inspectResourceRequest->GetResources();
    if (resources.size() != 1)
        return;

    const ResourceFileDescriptor& desc = resources.front();
    if (desc.isDirectory_ || desc.isAutomatic_ || !desc.HasExtension(".fbx"))
        return;

    const ea::string fileName = desc.fileName_;
    const ea::string resourceName = desc.resourceName_;
    request->QueueProcessCallback([=]()
    {
        fbxFileName_ = fileName;
        fbxResourceName_ = resourceName;
        OnActivated(this);
    });
}

void FbxAssetInspector::RenderContent()
{
    if (!fbxFileName_)
        return;

    if (ui::Button(ICON_FA_FILE_IMPORT " Import FBX"))
        ImportFbxFile(project_.Get(), *fbxFileName_);
    if (ui::IsItemHovered())
    {
        ui::SetTooltip("Import this FBX file in-process using AssetImporter library.\n"
            "Files with '@' in name are imported as Animation (.ani),\n"
            "others as Model (.mdl). Output is placed next to the source file.");
    }

    ui::TextWrapped("%s", fbxResourceName_->c_str());
}

void FbxAssetInspector::RenderContextMenuItems()
{
}

void FbxAssetInspector::RenderMenu()
{
}

void FbxAssetInspector::ApplyHotkeys(HotkeyManager* hotkeyManager)
{
}

}
