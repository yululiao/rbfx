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
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../Tabs/GameViewTab.h"

#include "../Core/IniHelpers.h"

#include <Urho3D/Core/WorkQueue.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/Engine/StateManager.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/GraphicsEvents.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/Texture2D.h>
#include <Urho3D/Input/Input.h>
#include "../Tabs/SceneViewTab.h"
#ifdef URHO3D_LUA
#include "../Project/LuaGameRunner.h"
#endif
#include <Urho3D/Plugins/PluginManager.h>
#include <Urho3D/RenderAPI/RenderContext.h>
#include <Urho3D/RenderAPI/RenderDevice.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/XMLFile.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/SystemUI/DebugHud.h>
#include <Urho3D/SystemUI/Widgets.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/Utility/PackedSceneData.h>

#if URHO3D_RMLUI
    #include <Urho3D/RmlUI/RmlUI.h>
    #include <RmlUi/Core/DataModelHandle.h>
#endif

#include <IconFontCppHeaders/IconsFontAwesome6.h>

namespace Urho3D
{

namespace
{

const auto Hotkey_ReleaseInput = EditorHotkey{"GameViewTab.ReleaseInput"}.Shift().Press(KEY_ESCAPE);

}

void Tabs_GameViewTab(Context* context, Project* project)
{
    project->AddTab(MakeShared<GameViewTab>(context));
}

class GameViewTab::PlayState : public Object
{
    URHO3D_OBJECT(PlayState, Object);

public:
    PlayState(Context* context, CustomBackbufferTexture* backbuffer, Scene* editorScene,
        const ea::string& uiPreviewDocument)
        : Object(context)
        , engine_(GetSubsystem<Engine>())
        , renderer_(GetSubsystem<Renderer>())
        , pluginManager_(GetSubsystem<PluginManager>())
        , input_(GetSubsystem<Input>())
        , legacyUI_(GetSubsystem<UI>())
        , systemUI_(GetSubsystem<SystemUI>())
#if URHO3D_RMLUI
        , rmlUI_(GetSubsystem<RmlUI>())
#endif
        , stateManager_(GetSubsystem<StateManager>())
        , project_(GetSubsystem<Project>())
        , backbuffer_(backbuffer)
        , editorScene_(editorScene)
        , uiPreviewDocument_(uiPreviewDocument)
    {
        engine_->SetParameter(Param_IsRunningInEditor, true);

        // Snapshot the editor scene before the game touches it.
        // The snapshot is restored in the destructor so Play mode changes
        // (created/removed/modified nodes and components) never leak into
        // the editing session — same pattern as SimulateSceneAction.
        if (auto* scene = editorScene_.Get())
            sceneSnapshot_ = PackedSceneData::FromScene(scene);

        UpdateRenderSurface();

        legacyUI_->GetRoot()->RemoveAllChildren();
        legacyUI_->GetRootModalElement()->RemoveAllChildren();

        legacyUI_->SetRenderTarget(backbuffer_->GetTexture());
        backbuffer_->SetActive(true);
        GrabInput();

#ifdef URHO3D_LUA
        // Lua play mode: the Lua script is the game entry point, so plugins
        // (e.g. Builtin.SceneViewer) must NOT be started — they would load a
        // duplicate scene and overwrite the Lua viewport on the Renderer.
        luaRunner_ = MakeShared<LuaGameRunner>(context_);
        const bool luaPlayMode = luaRunner_->Start(editorScene, project_);
        if (!luaPlayMode)
            luaRunner_ = nullptr;
        pluginsSkipped_ = luaPlayMode;
#endif

#ifndef URHO3D_LUA
        pluginManager_->StartApplication();
#else
        if (!pluginsSkipped_)
            pluginManager_->StartApplication();
#endif
        UpdatePreferredMouseSetup();

        // Load the editor-requested document last: the game's own UI is in
        // place by now, so the preview document sits on top of it.
        if (!uiPreviewDocument_.empty())
            LoadUiPreviewDocument();
    }

    void GrabInput()
    {
        if (inputGrabbed_)
            return;

        input_->SetMouseVisible(preferredMouseVisible_);
        input_->SetMouseMode(preferredMouseMode_);
        input_->SetEnabled(true);
        systemUI_->SetPassThroughEvents(true);
        project_->SetGlobalHotkeysEnabled(false);
        project_->SetHighlightEnabled(true);

        inputGrabbed_ = true;
    }

    void ReleaseInput()
    {
        if (!inputGrabbed_)
            return;

        UpdatePreferredMouseSetup();
        input_->SetMouseVisible(true);
        input_->SetMouseMode(MM_ABSOLUTE);
        input_->SetEnabled(false);
        systemUI_->SetPassThroughEvents(false);
        project_->SetGlobalHotkeysEnabled(true);
        project_->SetHighlightEnabled(false);

        inputGrabbed_ = false;
    }

    void Update(const IntRect& windowRect)
    {
        input_->SetExplicitWindowRect(windowRect);
        UpdateRenderSurface();

        // Force cursor visible and absolute mode every frame so the user can always
        // click the Stop button or click outside the game view to release input,
        // even when the game sets MM_RELATIVE / hides the cursor.
        if (!input_->IsMouseVisible())
            input_->SetMouseVisible(true);
        if (input_->GetMouseMode() != MM_ABSOLUTE)
            input_->SetMouseMode(MM_ABSOLUTE);
    }

    bool IsInputGrabbed() const { return inputGrabbed_; }

    ~PlayState()
    {
        ReleaseInput();

#ifdef URHO3D_LUA
        // Stop the Lua session (globals reset -> Lua state reinit) before
        // plugins or scene state are touched.
        if (luaRunner_)
            luaRunner_->Stop();
#endif

        // Restore the editor scene from the Play-time snapshot after the Lua
        // state is torn down, so no stale Lua callbacks fire on removed nodes.
        if (auto scene = editorScene_.Get())
        {
            scene->SetUpdateEnabled(false);
            if (sceneSnapshot_.HasSceneData())
                sceneSnapshot_.ToScene(scene);
        }

#ifdef URHO3D_LUA
        if (!pluginsSkipped_)
            pluginManager_->StopApplication();
#else
        pluginManager_->StopApplication();
#endif
        backbuffer_->SetActive(false);

        legacyUI_->SetRenderTarget(nullptr);
        legacyUI_->SetCustomSize({0, 0});
        legacyUI_->GetRoot()->RemoveAllChildren();
        legacyUI_->GetRootModalElement()->RemoveAllChildren();

#if URHO3D_RMLUI
        // Close the editor-loaded document while its render target is still
        // attached: nothing this session loaded may leak into the editor frame.
        CloseUiPreviewDocument();
        rmlUI_->SetRenderTarget(nullptr);
#endif

        input_->ResetExplicitWindowRect();

        renderer_->SetBackbufferRenderSurface(nullptr);
        renderer_->SetNumViewports(0);

#ifdef URHO3D_LUA
        luaRunner_ = nullptr;
#endif

        stateManager_->Reset();

        engine_->SetParameter(Param_IsRunningInEditor, Variant::EMPTY);
    }

private:
    void UpdatePreferredMouseSetup()
    {
        preferredMouseVisible_ = input_->IsMouseVisible();
        preferredMouseMode_ = input_->GetMouseMode();
    }

    void UpdateRenderSurface()
    {
        RenderSurface* backbufferSurface = backbuffer_->GetTexture()->GetRenderSurface();
        if (backbufferSurface_ != backbufferSurface)
        {
            backbufferSurface_ = backbufferSurface;
            renderer_->SetBackbufferRenderSurface(backbufferSurface_);
            legacyUI_->SetCustomSize(backbufferSurface->GetSize());
#if URHO3D_RMLUI
            rmlUI_->SetRenderTarget(backbufferSurface_);
#endif
        }

#ifdef URHO3D_LUA
        // Sync the Lua game viewport to the backbuffer render surface.
        if (luaRunner_)
            luaRunner_->Update(backbuffer_);
#endif
    }

    /// Load the editor-requested .rml into the master RmlUI context. A load
    /// failure only degrades the session (warning), never interrupts the game.
    void LoadUiPreviewDocument()
    {
#if URHO3D_RMLUI
        Rml::Context* context = rmlUI_ ? rmlUI_->GetRmlContext() : nullptr;
        if (!context)
            return;

        // Same-source dedup: when the game loaded this document already, keep
        // its instance - a second copy of the same document must not appear.
        for (int i = 0; i < context->GetNumDocuments(); ++i)
        {
            Rml::ElementDocument* document = context->GetDocument(i);
            if (document && uiPreviewDocument_ == document->GetSourceURL())
                return;
        }

        // Documents bound to the design-time token "{{__data_model_id}}" need
        // an empty placeholder model under the name LoadDocument substitutes
        // for the token. Mirrors the editor preview (UIViewDocument) so the
        // bound regions resolve instead of erroring. An empty constructor
        // from GetDataModel means the name is free; a leftover model (the
        // allocator recycled our address) is reused as-is.
        const ea::string modelName = Format("{}", static_cast<void*>(this));
        Rml::DataModelConstructor existingModel = context->GetDataModel(modelName);
        if (!existingModel)
        {
            Rml::DataModelConstructor modelConstructor = context->CreateDataModel(modelName, nullptr);
            (void)modelConstructor.GetModelHandle();
        }

        Rml::ElementDocument* document = rmlUI_->LoadDocument(uiPreviewDocument_, this);
        if (!document)
        {
            URHO3D_LOGWARNING("UI preview document '{}' could not be loaded", uiPreviewDocument_);
            return;
        }
        document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
        uiPreviewLoaded_ = true;
#endif
    }

    /// Remove the document this session loaded, matched by source URL: the
    /// auto-reload on file change replaces the instance mid-session, so a
    /// stored document pointer would go stale. Documents of the game itself
    /// are managed by the game and are left alone.
    void CloseUiPreviewDocument()
    {
#if URHO3D_RMLUI
        if (!uiPreviewLoaded_)
            return;
        uiPreviewLoaded_ = false;

        Rml::Context* context = rmlUI_ ? rmlUI_->GetRmlContext() : nullptr;
        if (!context)
            return;

        ea::vector<Rml::ElementDocument*> documentsToClose;
        for (int i = 0; i < context->GetNumDocuments(); ++i)
        {
            Rml::ElementDocument* document = context->GetDocument(i);
            if (document && uiPreviewDocument_ == document->GetSourceURL())
                documentsToClose.push_back(document);
        }
        for (Rml::ElementDocument* document : documentsToClose)
            document->Close();
#endif
    }

    Engine* engine_{};
    Renderer* renderer_{};
    PluginManager* pluginManager_{};
    Input* input_{};
    UI* legacyUI_{};
    SystemUI* systemUI_{};
#if URHO3D_RMLUI
    RmlUI* rmlUI_{};
#endif
    StateManager* stateManager_{};
    Project* project_{};

    CustomBackbufferTexture* backbuffer_{};
    WeakPtr<RenderSurface> backbufferSurface_;
    WeakPtr<Scene> editorScene_;
    /// Scene state as it was before Play started; restored on Stop.
    PackedSceneData sceneSnapshot_;
    /// Editor-requested .rml document of this session (empty when none).
    ea::string uiPreviewDocument_;
    /// Whether that document (or its auto-reloaded replacement) is loaded.
    bool uiPreviewLoaded_{};
#ifdef URHO3D_LUA
    /// Lua play session; null when the plugin play mode is used.
    SharedPtr<LuaGameRunner> luaRunner_;
    /// True when plugin application was not started because Lua play mode is active.
    bool pluginsSkipped_{};
#endif

    bool inputGrabbed_{};

    bool preferredMouseVisible_{true};
    MouseMode preferredMouseMode_{MM_FREE};

    IntVector2 oldRootElementSize_{};
};

GameViewTab::GameViewTab(Context* context)
    : EditorTab(context, "Game", "212a6577-8a2a-42d6-aaed-042d226c724c",
          EditorTabFlag::NoContentPadding | EditorTabFlag::OpenByDefault, EditorTabPlacement::DockCenter)
    , backbuffer_(MakeShared<CustomBackbufferTexture>(context_))
{
    BindHotkey(Hotkey_ReleaseInput, &GameViewTab::ReleaseInput);

    auto pluginManager = GetSubsystem<PluginManager>();
    pluginManager->SetQuitApplicationCallback([this] { QuitApplication(); });
}

GameViewTab::~GameViewTab()
{
    auto pluginManager = GetSubsystem<PluginManager>();
    pluginManager->SetQuitApplicationCallback([] {});
}

bool GameViewTab::IsInputGrabbed() const
{
    return state_ && state_->IsInputGrabbed();
}

void GameViewTab::Play()
{
    if (state_)
        Stop();

    // One-shot payload: a UI preview request belongs to exactly one session.
    const ea::string uiPreviewDocument = pendingUiPreview_;
    pendingUiPreview_.clear();

    // Use the editor's current scene for the play session.
    auto* sceneViewTab = GetProject()->FindTab<SceneViewTab>();
    Scene* editorScene = sceneViewTab ? sceneViewTab->GetActivePage()->scene_.Get() : nullptr;
    if (!editorScene)
        return;

    // Flush unsaved scene changes to disk before the play session touches the
    // scene. The in-memory snapshot restores state on Stop, but this guards
    // against an editor crash mid-play losing recent edits. Only dirty scenes
    // are written (SaveResource skips unchanged resources), mirroring Unity's
    // "Auto Save Scenes" on entering play mode.
    if (autoSaveOnPlay_)
        sceneViewTab->SaveResource(sceneViewTab->GetActiveResourceName());

    editorScene->SetUpdateEnabled(true);
    state_ = ea::make_unique<PlayState>(context_, backbuffer_, editorScene, uiPreviewDocument);
    activeUiPreview_ = uiPreviewDocument;
    OnSimulationStarted(this);
}

void GameViewTab::Stop()
{
    if (!state_)
        return;

    state_ = nullptr;
    activeUiPreview_.clear();
    OnSimulationStopped(this);
}

void GameViewTab::TogglePlayed()
{
    if (IsPlaying())
        Stop();
    else
        Play();
}

void GameViewTab::ReleaseInput()
{
    if (state_)
        state_->ReleaseInput();
}

void GameViewTab::RenderToolbar()
{
    if (IsPlaying())
    {
        if (Widgets::ToolbarButton(ICON_FA_STOP, "Stop"))
            Stop();
        Widgets::ToolbarSeparator();
    }

    if (Widgets::ToolbarButton(ICON_FA_BUG, "Toggle Debug HUD", hudVisible_))
        hudVisible_ = !hudVisible_;

    Widgets::ToolbarSeparator();
}

void GameViewTab::PreRenderUpdate()
{
}

void GameViewTab::RenderContent()
{
    const IntVector2 contentSize = GetContentSize();
    if (contentSize.x_ == 0 || contentSize.y_ == 0)
        return;

    backbuffer_->SetTextureSize(contentSize);
    backbuffer_->Update();

    if (state_)
    {
        const auto& io = ui::GetIO();
        Texture2D* sceneTexture = backbuffer_->GetTexture();
        Widgets::ImageItem(sceneTexture, ToImGui(sceneTexture->GetSize()));

        IntVector2 origin = IntVector2::ZERO;
        if(!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
        {
            auto graphics = GetSubsystem<Graphics>();
            origin = graphics->GetWindowPosition();
        }

        const IntVector2 windowMin = origin + ToIntVector2(ui::GetItemRectMin());
        const IntVector2 windowMax = origin + ToIntVector2(ui::GetItemRectMax());
        state_->Update({windowMin, windowMax});
    }

    if (state_)
    {
        const bool wasGrabbed = state_->IsInputGrabbed();
        const bool needGrab = ui::IsItemHovered() && ui::IsMouseClicked(MOUSEB_ANY);
        const bool needRelease = !ui::IsItemHovered() && ui::IsMouseClicked(MOUSEB_ANY);
        if (!wasGrabbed && needGrab)
            state_->GrabInput();
        else if (wasGrabbed && needRelease)
            state_->ReleaseInput();
    }

    auto hud = GetSubsystem<DebugHud>();
    if (hud && hudVisible_)
    {
        const IntVector2 position = GetContentPosition();
        ui::SetCursorScreenPos(ToImGui(position.ToVector2()));
        hud->RenderUI(DEBUGHUD_SHOW_ALL);
    }
}

void GameViewTab::RenderContextMenuItems()
{
    if (ui::MenuItem("Auto Save Scene On Play", nullptr, autoSaveOnPlay_))
        autoSaveOnPlay_ = !autoSaveOnPlay_;
}

void GameViewTab::WriteIniSettings(ImGuiTextBuffer& output)
{
    WriteIntToIni(output, "IsHudVisible", hudVisible_ ? 1 : 0);
    WriteIntToIni(output, "AutoSaveOnPlay", autoSaveOnPlay_ ? 1 : 0);
}

void GameViewTab::ReadIniSettings(const char* line)
{
    if (const auto isHudVisible = ReadIntFromIni(line, "IsHudVisible"))
        hudVisible_ = *isHudVisible != 0;
    if (const auto autoSaveOnPlay = ReadIntFromIni(line, "AutoSaveOnPlay"))
        autoSaveOnPlay_ = *autoSaveOnPlay != 0;
}

void GameViewTab::QuitApplication()
{
    auto workQueue = GetSubsystem<WorkQueue>();
    if (IsPlaying())
        workQueue->PostDelayedTaskForMainThread([this] { Stop(); });
    else
        URHO3D_LOGWARNING("Application quit was requested when nothing is played");
}

} // namespace Urho3D
