//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "ResourceEditorTab.h"

#include <EASTL/map.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace Urho3D
{

/// Plugin entry: registers the singleton text editor tab.
void Tabs_TextEditorTab(Context* context, Project* project);

/// Plain multi-line text editor for hand-authored resource files (.rcss
/// stylesheets for now). No syntax highlighting and no structure awareness:
/// it exists so the UIView inspector's "Matched styles" section has a
/// click-through - open the originating file, land on the line, edit, save.
/// Edits are undoable whole-buffer text snapshots; the dirty tracking,
/// unsaved-on-close prompt and external-overwrite guard are the standard
/// ResourceEditorTab plumbing.
class TextEditorTab : public ResourceEditorTab
{
    URHO3D_OBJECT(TextEditorTab, ResourceEditorTab);

public:
    TextEditorTab(Context* context);
    ~TextEditorTab() override;

    /// Ask the editor to open \a resourceName in a text editor tab and scroll
    /// to the 1-based \a line (any already-open instance takes it).
    static void OpenAtLine(Project* project, const ea::string& resourceName, int line);

    /// EditorTab / ResourceEditorTab implementation
    /// @{
    void RenderContent() override;
    ea::string GetResourceTitle() override;
    bool CanOpenResource(const ResourceFileDescriptor& desc) override;
    bool SupportMultipleResources() override { return true; }
    bool CanSaveResource(const ea::string& resourceName) override;
    /// @}

protected:
    /// ResourceEditorTab implementation
    /// @{
    void OnResourceLoaded(const ea::string& resourceName) override;
    void OnResourceUnloaded(const ea::string& resourceName) override;
    void OnActiveResourceChanged(const ea::string& oldResourceName,
        const ea::string& newResourceName) override;
    void OnResourceSaved(const ea::string& resourceName) override;
    void OnResourceShallowSaved(const ea::string& resourceName) override;
    /// @}

private:
    struct FileBuffer
    {
        ea::vector<char> edit_; ///< ImGui-facing buffer: null-terminated, with slack to type into
        ea::string disk_;       ///< save-guard baseline: the bytes this tab last loaded or wrote
        int gotoLine_ = -1;     ///< pending 1-based scroll target, consumed by the next render
    };

    FileBuffer* Buffer(const ea::string& resourceName);
    /// Swap the buffer content from an undo/redo action (records no new step).
    void ReplaceBufferText(const ea::string& resourceName, const ea::string& text);

    ea::string ReadResourceFile(const ea::string& resourceName) const;
    bool WriteResourceFile(const ea::string& resourceName, const ea::string& text);

    friend class TextEditAction;

    ea::map<ea::string, FileBuffer> buffers_;
    ea::string pendingExternalOverwrite_; ///< set by CanSaveResource, asked by the modal
    ea::string overwriteApproved_;        ///< one-shot approval consumed by CanSaveResource
};

}
