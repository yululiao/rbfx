//
// Copyright (c) 2017-2026 the rbfx project.
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

#include "../Assets/FbxImportSettings.h"

#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Math/MathDefs.h>
#include <Urho3D/Resource/XMLFile.h>

namespace Urho3D
{

namespace
{

// Sane bounds for the ratio attribute: (0, 1) excluding both ends, where simplification is
// either meaningless or destructive. Values outside are clamped, not rejected, so a hand-edited
// file cannot wedge the import.
constexpr float MinSimplifyRatio = 0.05f;
constexpr float MaxSimplifyRatio = 0.95f;

} // namespace

bool FbxImportSettings::IsDefault() const
{
    return !simplify_;
}

ea::string GetFbxMetaFileName(const ea::string& sourceFileName)
{
    return sourceFileName + ".fbxmeta";
}

bool LoadFbxImportSettings(Context* context, const ea::string& sourceFileName, FbxImportSettings& params)
{
    params = {};

    auto* fs = context->GetSubsystem<FileSystem>();
    if (!fs)
        return false;
    const ea::string metaFileName = GetFbxMetaFileName(sourceFileName);
    if (!fs->FileExists(metaFileName))
        return false;

    XMLFile xmlFile(context);
    if (!xmlFile.LoadFile(metaFileName))
    {
        URHO3D_LOGERROR("Could not parse the FBX metadata file '{}'.", metaFileName);
        return false;
    }

    const XMLElement rootElem = xmlFile.GetRoot("fbx");
    if (rootElem.IsNull())
        return false;

    if (rootElem.HasAttribute("simplify"))
        params.simplify_ = rootElem.GetBool("simplify");
    if (rootElem.HasAttribute("simplifyRatio"))
    {
        params.simplifyRatio_ = Clamp(rootElem.GetFloat("simplifyRatio"), MinSimplifyRatio, MaxSimplifyRatio);
    }

    return true;
}

bool SaveFbxImportSettings(Context* context, const ea::string& sourceFileName, const FbxImportSettings& params)
{
    auto* fs = context->GetSubsystem<FileSystem>();
    if (!fs)
        return false;
    const ea::string metaFileName = GetFbxMetaFileName(sourceFileName);

    // Default params need no metadata file; one left over from earlier edits is removed again.
    if (params.IsDefault())
        return !fs->FileExists(metaFileName) || fs->Delete(metaFileName);

    // Keep what is already there: the file is the user's, and elements this panel does not
    // manage (future import parameters) survive a round-trip untouched.
    XMLFile xmlFile(context);
    XMLElement rootElem;
    if (fs->FileExists(metaFileName))
    {
        if (!xmlFile.LoadFile(metaFileName))
            return false;
        rootElem = xmlFile.GetRoot();
    }
    if (rootElem.IsNull())
        rootElem = xmlFile.CreateRoot("fbx");

    rootElem.SetBool("simplify", params.simplify_);
    rootElem.SetFloat("simplifyRatio", Clamp(params.simplifyRatio_, MinSimplifyRatio, MaxSimplifyRatio));

    return xmlFile.SaveFile(metaFileName);
}

} // namespace Urho3D
