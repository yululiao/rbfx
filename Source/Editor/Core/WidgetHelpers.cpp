//
// Copyright (c) 2017-2020 the rbfx project.
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

#include "../Core/WidgetHelpers.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>

#include <IconFontCppHeaders/IconsFontAwesome6.h>
#include <nfd.h>

namespace Urho3D
{

ea::optional<ea::string> PickNativePath(bool pickDirectory, const ea::string& filter,
    const ea::string& initialDir)
{
    // A value that points nowhere is left alone instead: NFD picks its own default place rather
    // than erroring on a path that does not exist.
    const char* const startAt = initialDir.empty() ? nullptr : initialDir.c_str();

    // The blocking OS dialog runs straight off the click, exactly like the resource picker of
    // the inspector does; a popup of our own would only add a frame of delay.
    nfdresult_t result = NFD_ERROR;
    ea::string chosen;
    if (!pickDirectory)
    {
        // An empty spec lists every file rather than filtering by extension: the form to use
        // when any executable counts as a valid tool path, say.
        nfdu8filteritem_t filterItem{};
        const nfdu8filteritem_t* filters = nullptr;
        nfdfiltersize_t filterCount = 0;
        if (!filter.empty())
        {
            filterItem.name = "Files";
            filterItem.spec = filter.c_str();
            filters = &filterItem;
            filterCount = 1;
        }
        nfdu8char_t* outPath = nullptr;
        result = NFD_OpenDialogU8(&outPath, filters, filterCount, startAt);
        if (result == NFD_OKAY)
        {
            chosen = ResolvePath(outPath);
            NFD_FreePathU8(outPath);
        }
    }
    else
    {
        nfdchar_t* outPath = nullptr;
        result = NFD_PickFolder(&outPath, startAt);
        if (result == NFD_OKAY)
        {
            chosen = ResolvePath(outPath);
            NFD_FreePath(outPath);
        }
    }
    if (result == NFD_ERROR)
    {
        URHO3D_LOGERROR("Browse dialog failed: {}", NFD_GetError());
        return ea::nullopt;
    }
    return result == NFD_OKAY ? ea::optional<ea::string>(chosen) : ea::nullopt;
}

bool BrowseButton(const char* label, ea::string& path, const char* filter)
{
    ui::SameLine();
    ui::PushID((ea::string(label) + "BrowseButton").c_str());
    bool clicked = ui::Button(ICON_FA_FOLDER_OPEN);
    ui::PopID();
    if (!clicked)
    {
        if (ui::IsItemHovered())
            ui::SetTooltip("Browse...");
        return false;
    }

    // Opening in the current value means fixing a typo starts where the typo left off. A value
    // that points nowhere is left alone instead: NFD picks its own default place rather than
    // erroring on a path that does not exist.
    auto* fs = Context::GetInstance()->GetSubsystem<FileSystem>();
    ea::string initialDir;
    if (fs && !path.empty())
    {
        if (fs->DirExists(path))
            initialDir = ResolvePath(path);
        else if (fs->FileExists(path))
            initialDir = ResolvePath(GetPath(path));
    }

    const auto picked = PickNativePath(filter == nullptr, filter ? filter : "", initialDir);
    if (picked)
    {
        path = *picked;
        return true;
    }
    return false;
}

bool PathField(const char* label, ea::string& path, PathFieldKind kind,
    const char* filter, const char* tooltip)
{
    bool changed = ui::InputText(label, &path);
    if (tooltip && ui::IsItemHovered())
        ui::SetTooltip("%s", tooltip);

    // A directory picker takes no extension filter; a file picker without one lists every file.
    if (BrowseButton(label,path, kind == PathFieldKind::File ? (filter ? filter : "") : nullptr))
        changed = true;

    // The reveal button opens the OS file manager at the value: selecting a file, or opening a
    // directory. A value that is not on disk has nothing to show, so the button waits until it is.
    auto* fs = Context::GetInstance()->GetSubsystem<FileSystem>();
    ui::BeginDisabled(!fs || path.empty() || (!fs->FileExists(path) && !fs->DirExists(path)));
    ui::SameLine();
    ui::PushID((ea::string(label) + "RaVealButton").c_str());
    bool clicked = ui::Button(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE);
    ui::PopID();
    if (clicked)
    {
        if (!fs->Reveal(path))
            URHO3D_LOGERROR("Could not reveal '{}' in the OS file browser", path);
    }
    ui::EndDisabled();
    if (ui::IsItemHovered())
        ui::SetTooltip("Show in OS file browser");

    return changed;
}

}
