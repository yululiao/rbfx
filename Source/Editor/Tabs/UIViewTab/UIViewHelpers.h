//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "UIViewDocument.h"

#include "../../Core/WidgetHelpers.h"
#include "../../Project/Project.h"
#include "../../Project/ProjectRequest.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/SystemUI/SystemUI.h>

#include <IconFontCppHeaders/IconsFontAwesome6.h>

#include <RmlUi/Core/Types.h>

namespace Urho3D
{

/// Helpers shared by UIViewTab, UIViewHierarchy and UIViewInspector: viewport
/// conversions, resource-path resolution and picking, node drag payload
/// plumbing and panel constants. They live beside the units that use them
/// instead of inside any one translation unit.

inline Vector2 V2(const Rml::Vector2f& v) { return Vector2{v.x, v.y}; }
inline Vector2 V2(const ImVec2& v) { return Vector2{v.x, v.y}; }
inline ImVec2 IV2(const Vector2& v) { return ImVec2{v.x_, v.y_}; }

// Normalize native path separators to '/' (resource names are '/'-delimited).
inline void NormalizePath(ea::string& s)
{
    for (char& c : s)
    {
        if (c == '\\')
            c = '/';
    }
}

// ASCII-lowercased copy, for case-insensitive path prefix / suffix tests on Windows.
inline ea::string LowerCopy(const ea::string& s)
{
    ea::string r = s;
    for (char& c : r)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

// Resolve an RmlUi href against the document's resource path, mirroring how
// RmlUi's SystemInterface::JoinPath resolves document-relative references
// (the same rule RmlFile uses when loading <link>/<template> targets).
inline ea::string ResolveRelativeResourcePath(const ea::string& basePath, const ea::string& href)
{
    ea::string clean = Trim(href);
    const size_t extra = clean.find_first_of("?#"); // Rml::URL strips query/anchor
    if (extra != ea::string::npos)
        clean.resize(extra);
    if (clean.empty())
        return ea::string();
    if (clean.front() == '/') // absolute in RmlUi terms
        return clean.substr(1);

    ea::vector<ea::string> segments;
    const size_t slash = basePath.find_last_of('/');
    const ea::string dir = slash == ea::string::npos ? ea::string() : basePath.substr(0, slash);
    size_t begin = 0;
    while (begin < dir.length())
    {
        const size_t end = dir.find('/', begin);
        const ea::string seg = dir.substr(begin, (end == ea::string::npos ? dir.length() : end) - begin);
        if (!seg.empty())
            segments.push_back(seg);
        if (end == ea::string::npos)
            break;
        begin = end + 1;
    }

    begin = 0;
    while (begin <= clean.length())
    {
        const size_t end = clean.find('/', begin);
        const ea::string seg = clean.substr(begin, (end == ea::string::npos ? clean.length() : end) - begin);
        if (seg == ".." )
        {
            if (!segments.empty())
                segments.pop_back();
        }
        else if (!seg.empty() && seg != ".")
            segments.push_back(seg);
        if (end == ea::string::npos)
            break;
        begin = end + 1;
    }

    ea::string out;
    for (const ea::string& seg : segments)
    {
        if (!out.empty())
            out += '/';
        out += seg;
    }
    return out;
}

// Whether a resource name can be read right now (registered in the cache, or
// present under the project's Data folder). Cache lookup is in-memory; the
// filesystem probe is one stat per open template link per frame.
inline bool ResourceExists(Context* context, const ea::string& resourceName)
{
    auto* cache = context->GetSubsystem<ResourceCache>();
    if (cache && !cache->GetResourceFileName(resourceName).empty())
        return true;
    auto* project = context->GetSubsystem<Project>();
    auto* fs = context->GetSubsystem<FileSystem>();
    return project && fs && fs->FileExists(project->GetDataPath() + resourceName);
}

// Pick a file under the project's Data folder and hand it back as a '/'-rooted
// resource path ("/Textures/foo.png") - the one spelling that resolves the same
// however far the editing document sits from the target (ResolveRelativeResource
// Path treats a leading '/' as Data-rooted, exactly like RmlUi). Cancelling, or
// choosing something outside Data, yields nullopt: RmlUi can only load files
// that live under Data, so writing an outside path would plant a reference that
// silently never resolves. 'current' seeds the starting folder from the field's
// present value so fixing a reference opens where it already points.
inline ea::optional<ea::string> PickDataResource(Context* context, const ea::string& current,
    const char* filter)
{
    auto* project = context->GetSubsystem<Project>();
    auto* fs = context->GetSubsystem<FileSystem>();
    if (!project || !fs)
        return ea::nullopt;

    ea::string dataDir = project->GetDataPath().c_str();
    NormalizePath(dataDir);
    if (!dataDir.empty() && dataDir.back() != '/')
        dataDir += '/';

    ea::string seed = dataDir;
    if (!current.empty())
    {
        ea::string rel = current;
        if (rel.front() == '/')
            rel = rel.substr(1);
        const ea::string abs = dataDir + rel;
        if (fs->FileExists(abs) || fs->DirExists(abs))
            seed = abs; // PickNativePath opens the folder that holds it
    }

    const auto picked = PickNativePath(false, filter ? ea::string(filter) : ea::string(), seed);
    if (!picked)
        return ea::nullopt; // cancelled, or the dialog failed (already logged)

    ea::string chosen = *picked;
    NormalizePath(chosen);
    if (dataDir.empty())
        return ea::nullopt;
    const ea::string lowerChosen = LowerCopy(chosen);
    const ea::string lowerData = LowerCopy(dataDir);
    if (lowerChosen.compare(0, lowerData.size(), lowerData) != 0)
    {
        URHO3D_LOGWARNING("UIViewTab: '{}' is outside the project Data folder '{}'; RmlUi cannot load it.",
            chosen.c_str(), dataDir.c_str());
        return ea::nullopt;
    }
    const ea::string rel = chosen.substr(dataDir.length());
    if (rel.empty())
        return ea::nullopt;
    return ea::optional<ea::string>(ea::string("/") + rel);
}

// A folder-open button parked on the line after a resource-path field. On a
// pick it returns the new '/'-rooted path; a cancel returns nullopt, so the
// caller can tell "nothing happened" from "repoint at this file". 'id' keeps
// the button's ImGui identity distinct from its field.
inline ea::optional<ea::string> ResourceBrowseWidget(const char* id, Context* context,
    const ea::string& current, const char* filter)
{
    ui::SameLine();
    ui::PushID(id);
    const bool clicked = ui::SmallButton(ICON_FA_FOLDER_OPEN);
    ui::PopID();
    if (ui::IsItemHovered())
        ui::SetTooltip("Browse...");
    if (!clicked)
        return ea::nullopt;
    return PickDataResource(context, current, filter);
}

// Open the nested document referenced by \a href (or, when revealOnly is set,
// just locate and highlight it in the Resource Browser). ProcessRequest
// defers the handling to the frame loop, so calling this from inside ImGui
// rendering is safe.
inline void OpenNestedDocumentFile(Context* context, UIViewDocument* doc, const ea::string& href, bool revealOnly)
{
    const ea::string resPath = ResolveRelativeResourcePath(doc->GetSourcePath(), href);
    if (resPath.empty())
        return;
    auto* project = context->GetSubsystem<Project>();
    if (project)
        project->ProcessRequest(MakeShared<OpenResourceRequest>(context, resPath, revealOnly).Get());
}

// Drag-drop payload type carrying the source row's child-index path.
const char* const kUiNodeDragType = "UIEDITOR_NODE_PATH";

// Build the kUiNodeDragType payload: the source document's instance id
// followed by the dragged node's child-index path. The id lets a target on
// another document (another tab's canvas or hierarchy) reject the payload
// instead of resolving the path against the wrong model.
inline ea::vector<unsigned> MakeNodeDragData(const UIViewDocument* doc, const ea::vector<unsigned>& path)
{
    ea::vector<unsigned> data;
    data.reserve(path.size() + 1);
    data.push_back(doc->GetInstanceId());
    data.insert(data.end(), path.begin(), path.end());
    return data;
}

// Parse a kUiNodeDragType payload for \a doc. False for payloads from
// another document and for malformed payloads; on success \a outPath gets
// the dragged node's child-index path.
inline bool ParseNodeDragData(const ImGuiPayload* payload, const UIViewDocument* doc, ea::vector<unsigned>& outPath)
{
    const unsigned count = static_cast<unsigned>(payload->DataSize) / sizeof(unsigned);
    if (!doc || count == 0)
        return false;
    const unsigned* data = static_cast<const unsigned*>(payload->Data);
    if (data[0] != doc->GetInstanceId())
        return false;
    outPath.assign(data + 1, data + count);
    return true;
}

// Row-scoped context menu popup (shared by every hierarchy row; the request
// is recorded by RenderNode and opened at the end of RenderContent, see the
// comments there).
const char* const kUiNodePopupId = "##uiNodeCtx";
// Hover highlight of a hierarchy row (RGBA ImU32).
constexpr ImU32 kHoverColor = IM_COL32(90, 170, 255, 200);

}

