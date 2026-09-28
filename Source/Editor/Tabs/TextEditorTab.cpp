//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "TextEditorTab.h"

#include "../Project/Project.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/SystemUI/SystemUI.h>

#include <string.h>

namespace Urho3D
{

namespace
{
/// Slack kept past the text end so typing into the buffer never overruns
/// within a frame (the buffer is re-sized after every committed edit).
constexpr size_t EditSlack = 4096;
}

/// One undoable whole-buffer edit. Consecutive edits of the same file merge
/// into a single step (type a sentence, press Ctrl+Z once), mirroring the
/// document snapshot actions of the UI editor.
class TextEditAction : public EditorAction
{
public:
    TextEditAction(TextEditorTab* tab, ea::string resourceName,
        const ea::string& before, const ea::string& after)
        : tab_(tab)
        , resourceName_(ea::move(resourceName))
        , before_(before)
        , after_(after)
    {
    }

    /// Implement EditorAction.
    /// @{
    bool CanUndoRedo() const override { return tab_ && tab_->IsResourceOpen(resourceName_); }
    void Undo() const override
    {
        if (tab_)
            tab_->ReplaceBufferText(resourceName_, before_);
    }
    void Redo() const override
    {
        if (tab_)
            tab_->ReplaceBufferText(resourceName_, after_);
    }
    bool MergeWith(const EditorAction& other) override
    {
        const auto otherEdit = dynamic_cast<const TextEditAction*>(&other);
        if (!otherEdit || tab_ != otherEdit->tab_ || resourceName_ != otherEdit->resourceName_)
            return false;
        after_ = otherEdit->after_;
        return true;
    }
    /// @}

private:
    WeakPtr<TextEditorTab> tab_;
    ea::string resourceName_;
    ea::string before_;
    ea::string after_; ///< mutable in effect: MergeWith rewrites the redo target
};

void Tabs_TextEditorTab(Context* context, Project* project)
{
    project->AddTab(MakeShared<TextEditorTab>(context));
}

TextEditorTab::TextEditorTab(Context* context)
    : ResourceEditorTab(context, "", "d4c8e2f0-1a67-4b5a-9f30-textedit0",
        EditorTabFlags{}, EditorTabPlacement::DockCenter)
{
    title_ = GetProject()->GetUniqTabName(this->GetTypeName(), "Text");
    guid_ = Format("d4c8e2f0-1a67-4b5a-9f30-textedit{}", title_);
    uniqueId_ = Format("{}###{}", title_, guid_);
}

TextEditorTab::~TextEditorTab() = default;

void TextEditorTab::OpenAtLine(Project* project, const ea::string& resourceName, int line)
{
    if (!project || resourceName.empty())
        return;
    // One tab instance hosts every open text file (SupportMultipleResources).
    // Find it - or spawn it - then open and scroll synchronously: OpenResource
    // runs OnResourceLoaded inline, so the buffer exists on return.
    TextEditorTab* host = nullptr;
    for (const SharedPtr<EditorTab>& tab : project->GetTabs())
    {
        if (auto* textTab = dynamic_cast<TextEditorTab*>(tab.Get()))
        {
            host = textTab;
            if (textTab->IsResourceOpen(resourceName))
                break;
        }
    }
    if (!host)
    {
        host = MakeShared<TextEditorTab>(project->GetContext());
        project->AddTab(SharedPtr<EditorTab>(host));
    }
    host->OpenResource(resourceName);
    host->Focus();
    if (FileBuffer* fb = host->Buffer(resourceName))
        fb->gotoLine_ = line;
}

bool TextEditorTab::CanOpenResource(const ResourceFileDescriptor& desc)
{
    return !desc.isDirectory_ && desc.HasExtension(".rcss");
}

ea::string TextEditorTab::GetResourceTitle()
{
    return GetActiveResourceName();
}

TextEditorTab::FileBuffer* TextEditorTab::Buffer(const ea::string& resourceName)
{
    const auto iter = buffers_.find(resourceName);
    return iter != buffers_.end() ? &iter->second : nullptr;
}

void TextEditorTab::ReplaceBufferText(const ea::string& resourceName, const ea::string& text)
{
    FileBuffer* fb = Buffer(resourceName);
    if (!fb)
        return;
    fb->edit_.resize(text.size() + EditSlack);
    if (!text.empty())
        memcpy(fb->edit_.data(), text.data(), text.size());
    fb->edit_[text.size()] = '\0';
}

ea::string TextEditorTab::ReadResourceFile(const ea::string& resourceName) const
{
    auto* cache = GetSubsystem<ResourceCache>();
    ea::string abs = cache->GetResourceFileName(resourceName);
    if (abs.empty())
    {
        auto* project = GetProject();
        abs = project ? project->GetDataPath() + resourceName : resourceName;
    }
    File file(context_, abs, FILE_READ);
    if (!file.IsOpen())
        return ea::string();
    return file.ReadText();
}

bool TextEditorTab::WriteResourceFile(const ea::string& resourceName, const ea::string& text)
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* fs = GetSubsystem<FileSystem>();
    ea::string abs = cache->GetResourceFileName(resourceName);
    if (abs.empty())
    {
        auto* project = GetProject();
        if (!project)
            return false;
        abs = project->GetDataPath() + resourceName;
        const size_t sep = abs.find_last_of("/\\");
        if (sep != ea::string::npos)
            fs->CreateDirsRecursive(abs.substr(0, sep));
    }

    File file(GetContext(), abs, FILE_WRITE);
    if (!file.IsOpen())
    {
        URHO3D_LOGERROR("TextEditorTab: cannot open '{}' for writing.", abs.c_str());
        return false;
    }
    file.Write(text.data(), text.size());

    // Drop any cached File so a later load sees the new bytes.
    cache->ReleaseResource(resourceName, true);
    return true;
}

void TextEditorTab::OnResourceLoaded(const ea::string& resourceName)
{
    const ea::string contents = ReadResourceFile(resourceName);
    if (contents.empty())
    {
        URHO3D_LOGERROR("TextEditorTab: failed to read '{}'", resourceName.c_str());
        return;
    }

    FileBuffer& fb = buffers_[resourceName];
    fb.disk_ = contents;
    ReplaceBufferText(resourceName, contents);
}

void TextEditorTab::OnResourceUnloaded(const ea::string& resourceName)
{
    buffers_.erase(resourceName);
    if (pendingExternalOverwrite_ == resourceName)
    {
        pendingExternalOverwrite_.clear();
        overwriteApproved_.clear();
    }
}

void TextEditorTab::OnActiveResourceChanged(const ea::string& oldResourceName,
    const ea::string& newResourceName)
{
    // Buffers are keyed by resource name and rendered by active name, so an
    // activation switch needs no extra work here.
    (void)oldResourceName;
    (void)newResourceName;
}

void TextEditorTab::OnResourceSaved(const ea::string& resourceName)
{
    FileBuffer* fb = Buffer(resourceName);
    if (!fb)
        return;
    const ea::string text(fb->edit_.data());
    if (WriteResourceFile(resourceName, text))
        fb->disk_ = text; // new save-guard baseline
}

void TextEditorTab::OnResourceShallowSaved(const ea::string& resourceName)
{
    (void)resourceName; // no shallow data distinct from the text itself
}

bool TextEditorTab::CanSaveResource(const ea::string& resourceName)
{
    // One-shot approval from the overwrite modal; consume it either way so a
    // stale approval can never auto-pass a later save.
    const bool approved = !overwriteApproved_.empty() && overwriteApproved_ == resourceName;
    overwriteApproved_.clear();
    if (approved)
        return true;

    const FileBuffer* fb = Buffer(resourceName);
    if (!fb)
        return true; // nothing loaded through this tab: no baseline to guard

    // Bytes on disk differing from what this tab last loaded or wrote mean
    // someone else touched the file while it was open. Never clobber
    // silently: record the decision and let the modal ask.
    if (ReadResourceFile(resourceName) == fb->disk_)
        return true;
    pendingExternalOverwrite_ = resourceName;
    return false;
}

void TextEditorTab::RenderContent()
{
    const ea::string& active = GetActiveResourceName();
    if (active.empty())
    {
        ui::TextDisabled("Open a text resource (an .rcss stylesheet, for example) to edit it here.\n"
            "The UI editor's \"Matched styles\" lines open their source files here.");
        return;
    }
    FileBuffer* fb = Buffer(active);
    if (!fb)
        return;

    // Save goes through the tab context menu (right-click the tab header);
    // say so once - a text editor without a visible save path is a trap.
    ui::TextDisabled("%s   (save: tab context menu / Ctrl+S via the editor's save hotkey)",
        active.c_str());

    const ea::string before(fb->edit_.data());
    const bool edited = ui::InputTextMultiline("##text", fb->edit_.data(), fb->edit_.size(),
        ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_AllowTabInput);
    if (edited)
    {
        const ea::string after(fb->edit_.data());
        // Grow the slack for the next keystrokes; the undo step snapshots the
        // committed strings, not the raw buffer.
        if (fb->edit_.size() < after.size() + EditSlack)
            ReplaceBufferText(active, after);
        PushAction(MakeShared<TextEditAction>(this, active, before, after));
    }

    // A pending goto line (clicked from the inspector) scrolls the multiline
    // to the rule's declaration.
    if (fb->gotoLine_ > 0)
    {
        const float lineHeight = ui::GetFont()->FontSize + ui::GetStyle().ItemSpacing.y;
        ui::SetScrollY(static_cast<float>(fb->gotoLine_ - 1) * lineHeight);
        fb->gotoLine_ = -1;
    }

    // External-overwrite modal (raised by CanSaveResource declining a save).
    if (!pendingExternalOverwrite_.empty())
    {
        ui::OpenPopup("##textOverwrite");
        if (ui::BeginPopupModal("##textOverwrite", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ui::Text("'%s' was changed on disk after it was loaded here.",
                pendingExternalOverwrite_.c_str());
            ui::TextDisabled("Overwriting loses the external changes; cancel and reload instead.");
            const ea::string resource = pendingExternalOverwrite_;
            if (ui::Button("Overwrite"))
            {
                overwriteApproved_ = resource;
                pendingExternalOverwrite_.clear();
                ui::CloseCurrentPopup();
                SaveResource(resource);
            }
            ui::SameLine();
            if (ui::Button("Cancel"))
            {
                pendingExternalOverwrite_.clear();
                ui::CloseCurrentPopup();
            }
            ui::EndPopup();
        }
    }
}

}
