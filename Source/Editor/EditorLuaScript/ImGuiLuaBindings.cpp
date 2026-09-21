//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "EditorLuaBindings.h"

// Dear ImGui is merged into Urho3D.dll and exported (IMGUI_EXPORTS on the Urho3D build);
// consumers compile with IMGUI_IMPORTS (a PUBLIC definition of the ImGui target) so the
// symbols below are imported from Urho3D.dll. EditorLibrary links Urho3D PUBLIC, which
// brings both the include directory and that definition transitively.
#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4244)
#endif
#include <imgui.h>
// Engine-side image widgets (Texture2D-aware) and Graphics/Texture2D.h come along with it.
#include <Urho3D/SystemUI/Widgets.h>
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

#include "../Core/WidgetHelpers.h"

#include <LuaScript/LuaBindMacros.h>

#include <Urho3D/Core/Context.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>

#include <IconFontCppHeaders/IconsFontAwesome6.h>

#include <cfloat>
#include <cstring>
#include <sol/sol.hpp>
#include <string>
#include <tuple>
#include <vector>

namespace Urho3D
{

// Registers an "imgui" global table exposing a lean, editor-oriented subset of the
// Dear ImGui immediate-mode API to Lua. This runs inside the editor's active ImGui
// frame (from Editor.addTab draw callbacks and Editor.addMenuItem click callbacks),
// so it deliberately does NOT create/destroy a context or feed input -- the engine's
// SystemUI subsystem already owns those. Only draw/layout/query calls are exposed.
void RegisterImGuiLuaBindings(sol::state& lua)
{
    sol::table imgui = lua.create_named_table("imgui");

    // ------------------------------------------------------------------ Text
    LUA_TABLE_FUNC(imgui, Text, [](const char* s) { ImGui::TextUnformatted(s); });
    LUA_TABLE_FUNC(imgui, TextWrapped, [](const char* s) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(s);
        ImGui::PopTextWrapPos();
    });
    LUA_TABLE_FUNC(imgui, TextDisabled, [](const char* s) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(s);
        ImGui::PopStyleColor();
    });
    LUA_TABLE_FUNC(imgui, TextColored, [](float r, float g, float b, float a, const char* s) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(r, g, b, a));
        ImGui::TextUnformatted(s);
        ImGui::PopStyleColor();
    });
    LUA_TABLE_FUNC(imgui, Bullet, []() { ImGui::Bullet(); });
    LUA_TABLE_FUNC(imgui, BulletText, [](const char* s) { ImGui::BulletText("%s", s); });

    // -------------------------------------------------------------- Windows
    LUA_TABLE_FUNC(imgui, Begin, [](const char* name, sol::optional<int> flags) {
        return ImGui::Begin(name, nullptr, flags.value_or(0));
    });
    LUA_TABLE_FUNC(imgui, End, []() { ImGui::End(); });
    LUA_TABLE_FUNC(imgui, BeginChild, [](const char* id, sol::optional<float> w, sol::optional<float> h,
                             sol::optional<bool> border, sol::optional<int> flags) {
        return ImGui::BeginChild(id, ImVec2(w.value_or(0.0f), h.value_or(0.0f)), border.value_or(false), flags.value_or(0));
    });
    LUA_TABLE_FUNC(imgui, EndChild, []() { ImGui::EndChild(); });

    // -------------------------------------------------------------- Buttons
    LUA_TABLE_FUNC(imgui, Button, [](const char* label, sol::optional<float> w, sol::optional<float> h) {
        return ImGui::Button(label, ImVec2(w.value_or(0.0f), h.value_or(0.0f)));
    });
    LUA_TABLE_FUNC(imgui, SmallButton, [](const char* label) { return ImGui::SmallButton(label); });
    LUA_TABLE_FUNC(imgui, ArrowButton, [](const char* id, int dir) {
        return ImGui::ArrowButton(id, static_cast<ImGuiDir>(dir));
    });
    LUA_TABLE_FUNC(imgui, InvisibleButton, [](const char* id, float w, float h, sol::optional<int> flags) {
        return ImGui::InvisibleButton(id, ImVec2(w, h), flags.value_or(0));
    });

    // -------------------------------------------------------- Images / textures
    // These forward to the engine's Widgets:: helpers, which convert a Texture2D into an
    // ImTextureID *and* keep it alive for the frame (SystemUI::ReferenceTexture), so plugins
    // never have to handle raw texture handles. Every argument after the texture is optional:
    // omit w/h to use the texture's own pixel size, and uv/tint/border fall back to ImGui
    // defaults. Reusing the same texture for several buttons needs an enclosing
    // imgui.PushID/imgui.PopID because the widget derives its id from the texture pointer.
    auto imageSize = [](Texture2D* texture, const sol::optional<float>& w, const sol::optional<float>& h)
    {
        return ImVec2(w.value_or(static_cast<float>(texture->GetWidth())),
            h.value_or(static_cast<float>(texture->GetHeight())));
    };

    LUA_TABLE_FUNC(imgui, Image,
        [imageSize](sol::optional<Texture2D*> texture, sol::optional<float> w, sol::optional<float> h,
            sol::optional<float> u0, sol::optional<float> v0, sol::optional<float> u1, sol::optional<float> v1,
            sol::optional<float> tr, sol::optional<float> tg, sol::optional<float> tb, sol::optional<float> ta,
            sol::optional<float> br, sol::optional<float> bg, sol::optional<float> bb, sol::optional<float> ba)
        {
            Texture2D* tex = texture.value_or(nullptr);
            if (!tex)
                return;
            Widgets::Image(tex, imageSize(tex, w, h),
                ImVec2(u0.value_or(0.0f), v0.value_or(0.0f)),
                ImVec2(u1.value_or(1.0f), v1.value_or(1.0f)),
                ImVec4(tr.value_or(1.0f), tg.value_or(1.0f), tb.value_or(1.0f), ta.value_or(1.0f)),
                ImVec4(br.value_or(0.0f), bg.value_or(0.0f), bb.value_or(0.0f), ba.value_or(0.0f)));
        });

    // Same as Image but registers an item, so IsItemHovered/IsItemClicked and tooltips work.
    LUA_TABLE_FUNC(imgui, ImageItem,
        [imageSize](sol::optional<Texture2D*> texture, sol::optional<float> w, sol::optional<float> h,
            sol::optional<float> u0, sol::optional<float> v0, sol::optional<float> u1, sol::optional<float> v1,
            sol::optional<float> tr, sol::optional<float> tg, sol::optional<float> tb, sol::optional<float> ta,
            sol::optional<float> br, sol::optional<float> bg, sol::optional<float> bb, sol::optional<float> ba)
        {
            Texture2D* tex = texture.value_or(nullptr);
            if (!tex)
                return;
            Widgets::ImageItem(tex, imageSize(tex, w, h),
                ImVec2(u0.value_or(0.0f), v0.value_or(0.0f)),
                ImVec2(u1.value_or(1.0f), v1.value_or(1.0f)),
                ImVec4(tr.value_or(1.0f), tg.value_or(1.0f), tb.value_or(1.0f), ta.value_or(1.0f)),
                ImVec4(br.value_or(0.0f), bg.value_or(0.0f), bb.value_or(0.0f), ba.value_or(0.0f)));
        });

    LUA_TABLE_FUNC(imgui, ImageButton,
        [imageSize](sol::optional<Texture2D*> texture, sol::optional<float> w, sol::optional<float> h,
            sol::optional<float> u0, sol::optional<float> v0, sol::optional<float> u1, sol::optional<float> v1,
            sol::optional<int> framePadding,
            sol::optional<float> bgr, sol::optional<float> bgg, sol::optional<float> bgb, sol::optional<float> bga,
            sol::optional<float> tr, sol::optional<float> tg, sol::optional<float> tb, sol::optional<float> ta)
        -> bool
        {
            Texture2D* tex = texture.value_or(nullptr);
            if (!tex)
                return false;
            return Widgets::ImageButton(tex, imageSize(tex, w, h),
                ImVec2(u0.value_or(0.0f), v0.value_or(0.0f)),
                ImVec2(u1.value_or(1.0f), v1.value_or(1.0f)),
                framePadding.value_or(-1),
                ImVec4(bgr.value_or(0.0f), bgg.value_or(0.0f), bgb.value_or(0.0f), bga.value_or(0.0f)),
                ImVec4(tr.value_or(1.0f), tg.value_or(1.0f), tb.value_or(1.0f), ta.value_or(1.0f)));
        });

    // ------------------------------------------------------ Toggle / choice
    LUA_TABLE_FUNC(imgui, Checkbox, [](const char* label, bool value) {
        bool v = value;
        const bool changed = ImGui::Checkbox(label, &v);
        return std::make_tuple(changed, v);
    });
    LUA_TABLE_FUNC(imgui, RadioButton, [](const char* label, bool active) {
        return ImGui::RadioButton(label, active);
    });
    LUA_TABLE_FUNC(imgui, ProgressBar, [](float fraction, sol::optional<float> w, sol::optional<float> h,
                             sol::optional<const char*> overlay) {
        ImGui::ProgressBar(fraction, ImVec2(w.value_or(-FLT_MIN), h.value_or(0.0f)), overlay.value_or(nullptr));
    });
    LUA_TABLE_FUNC(imgui, Combo, [](const char* label, int current, sol::table items) {
        std::string joined;
        const int count = static_cast<int>(items.size());
        for (int i = 1; i <= count; ++i)
        {
            const std::string item = items[i];
            joined += item;
            joined.push_back('\0');
        }
        joined.push_back('\0');
        int cur = current;
        const bool changed = ImGui::Combo(label, &cur, joined.c_str());
        return std::make_tuple(changed, cur);
    });

    // ------------------------------------------------------- Numeric widgets
    LUA_TABLE_FUNC(imgui, DragFloat, [](const char* label, float v, sol::optional<float> speed,
                            sol::optional<float> vmin, sol::optional<float> vmax) {
        float val = v;
        const bool changed = ImGui::DragFloat(label, &val, speed.value_or(1.0f), vmin.value_or(0.0f), vmax.value_or(0.0f));
        return std::make_tuple(changed, val);
    });
    LUA_TABLE_FUNC(imgui, DragInt, [](const char* label, int v, sol::optional<float> speed,
                            sol::optional<int> vmin, sol::optional<int> vmax) {
        int val = v;
        const bool changed = ImGui::DragInt(label, &val, speed.value_or(1.0f), vmin.value_or(0), vmax.value_or(0));
        return std::make_tuple(changed, val);
    });
    LUA_TABLE_FUNC(imgui, SliderFloat, [](const char* label, float v, float vmin, float vmax,
                             sol::optional<const char*> format) {
        float val = v;
        const bool changed = ImGui::SliderFloat(label, &val, vmin, vmax, format.value_or("%.3f"));
        return std::make_tuple(changed, val);
    });
    LUA_TABLE_FUNC(imgui, SliderInt, [](const char* label, int v, int vmin, int vmax,
                            sol::optional<const char*> format) {
        int val = v;
        const bool changed = ImGui::SliderInt(label, &val, vmin, vmax, format.value_or("%d"));
        return std::make_tuple(changed, val);
    });
    LUA_TABLE_FUNC(imgui, InputFloat, [](const char* label, float v) {
        float val = v;
        const bool changed = ImGui::InputFloat(label, &val);
        return std::make_tuple(changed, val);
    });
    LUA_TABLE_FUNC(imgui, InputInt, [](const char* label, int v) {
        int val = v;
        const bool changed = ImGui::InputInt(label, &val);
        return std::make_tuple(changed, val);
    });
    LUA_TABLE_FUNC(imgui, InputText, [](const char* label, const std::string& text, sol::optional<int> flags) {
        std::vector<char> buf(text.begin(), text.end());
        buf.push_back('\0');
        buf.resize(buf.size() + 256, '\0'); // slack so freshly typed text is not clipped on the same frame
        const bool changed = ImGui::InputText(label, buf.data(), buf.size(), flags.value_or(0));
        return std::make_tuple(changed, std::string(buf.data()));
    });

    // --------------------------------------------------------------- Path fields
    // Combined path field and standalone picker, mirroring the editor's C++ WidgetHelpers.
    // The native dialog is opened through PickNativePath, which is exactly why these bindings
    // live in the editor: nfd only exists in desktop editor builds.
    const auto pickNativePath = [](bool pickDirectory, const char* filter, const char* initialDir)
        -> sol::optional<std::string>
    {
        const auto chosen = PickNativePath(pickDirectory, filter ? filter : "", initialDir ? initialDir : "");
        return chosen ? sol::optional<std::string>(chosen->c_str()) : sol::nullopt;
    };

    // Opens the native picker alone and returns the chosen path, or nil when cancelled. kind is
    // "dir" to pick a folder (anything else, the default "file", picks a file); filter is an
    // extension spec ("png,jpg") for file picking; initialDir is where the dialog opens.
    LUA_TABLE_FUNC(imgui, PickPath,
        [pickNativePath](sol::optional<const char*> kind, sol::optional<const char*> filter,
            sol::optional<const char*> initialDir) -> sol::optional<std::string>
        {
            const bool pickDirectory = kind && strcmp(*kind, "dir") == 0;
            return pickNativePath(pickDirectory, filter.value_or(nullptr), initialDir.value_or(nullptr));
        });

    // A whole path field in one call: text input + native browse button + reveal button that
    // opens the OS file manager at the value (disabled while the value points nowhere). kind and
    // filter are PickPath's; the browse dialog starts where the current value points.
    LUA_TABLE_FUNC(imgui, InputPath,
        [pickNativePath](const char* label, const std::string& text, sol::optional<const char*> kind,
            sol::optional<const char*> filter) -> std::tuple<bool, std::string>
        {
            const bool pickDirectory = kind && strcmp(*kind, "dir") == 0;

            // Text half, same buffering scheme as InputText above.
            std::vector<char> buf(text.begin(), text.end());
            buf.push_back('\0');
            buf.resize(buf.size() + 256, '\0');
            bool changed = ImGui::InputText(label, buf.data(), buf.size());
            std::string value(buf.data());

            auto* fs = Context::GetInstance()->GetSubsystem<FileSystem>();

            // Both buttons live in one ID scope derived from the label: several path fields in
            // the same window would otherwise collide on the bare icon strings.
            ImGui::PushID(label);

            // Browse half: fixing a typo starts where the current value points; a value that
            // points nowhere falls back to the picker's own default place.
            std::string initialDir;
            if (fs && !value.empty())
            {
                if (fs->DirExists(value.c_str()))
                    initialDir = ResolvePath(value.c_str()).c_str();
                else if (fs->FileExists(value.c_str()))
                    initialDir = ResolvePath(GetPath(value.c_str())).c_str();
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_FOLDER_OPEN))
            {
                const auto picked = pickNativePath(pickDirectory, filter.value_or(nullptr),
                    initialDir.empty() ? nullptr : initialDir.c_str());
                if (picked)
                {
                    value = *picked;
                    changed = true;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Browse...");

            // Reveal half: opens the OS file manager at the value. A path that is not on disk
            // has nothing to show, so the button waits until it is.
            const bool canReveal = fs && !value.empty()
                && (fs->FileExists(value.c_str()) || fs->DirExists(value.c_str()));
            ImGui::SameLine();
            ImGui::BeginDisabled(!canReveal);
            if (ImGui::Button(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE) && canReveal)
            {
                if (!fs->Reveal(value.c_str()))
                    URHO3D_LOGERROR("Could not reveal '{}' in the OS file browser", value.c_str());
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Show in OS file browser");

            ImGui::PopID();

            return std::make_tuple(changed, value);
        });

    // ---------------------------------------------------------------- Color
    LUA_TABLE_FUNC(imgui, ColorEdit4, [](const char* label, float r, float g, float b, float a,
                             sol::optional<int> flags) {
        float col[4] = {r, g, b, a};
        const bool changed = ImGui::ColorEdit4(label, col, flags.value_or(0));
        return std::make_tuple(changed, col[0], col[1], col[2], col[3]);
    });
    LUA_TABLE_FUNC(imgui, ColorButton, [](const char* id, float r, float g, float b, float a,
                              sol::optional<int> flags, sol::optional<float> w, sol::optional<float> h) {
        ImGui::ColorButton(id, ImVec4(r, g, b, a), flags.value_or(0), ImVec2(w.value_or(0.0f), h.value_or(0.0f)));
    });

    // ------------------------------------------- Hierarchy (tree / header / selectable / list)
    LUA_TABLE_FUNC(imgui, TreeNode, [](const char* label) { return ImGui::TreeNode(label); });
    LUA_TABLE_FUNC(imgui, TreeNodeEx, [](const char* label, sol::optional<int> flags) {
        return ImGui::TreeNodeEx(label, flags.value_or(0));
    });
    LUA_TABLE_FUNC(imgui, TreePop, []() { ImGui::TreePop(); });
    LUA_TABLE_FUNC(imgui, CollapsingHeader, [](const char* label, sol::optional<int> flags) {
        return ImGui::CollapsingHeader(label, flags.value_or(0));
    });
    LUA_TABLE_FUNC(imgui, Selectable, [](const char* label, sol::optional<bool> selected, sol::optional<int> flags) {
        bool sel = selected.value_or(false);
        const bool clicked = ImGui::Selectable(label, &sel, flags.value_or(0));
        return std::make_tuple(clicked, sel);
    });
    LUA_TABLE_FUNC(imgui, BeginListBox, [](const char* label, sol::optional<float> w, sol::optional<float> h) {
        return ImGui::BeginListBox(label, ImVec2(w.value_or(0.0f), h.value_or(0.0f)));
    });
    LUA_TABLE_FUNC(imgui, EndListBox, []() { ImGui::EndListBox(); });
    // No ListBoxItem() in this ImGui build; draw rows with Selectable() between Begin/EndListBox.

    // ------------------------------------------------------- Layout helpers
    LUA_TABLE_FUNC(imgui, Separator, []() { ImGui::Separator(); });
    LUA_TABLE_FUNC(imgui, SameLine, [](sol::optional<float> offset, sol::optional<float> spacing) {
        ImGui::SameLine(offset.value_or(-1.0f), spacing.value_or(-1.0f));
    });
    LUA_TABLE_FUNC(imgui, NewLine, []() { ImGui::NewLine(); });
    LUA_TABLE_FUNC(imgui, Spacing, []() { ImGui::Spacing(); });
    LUA_TABLE_FUNC(imgui, Dummy, [](float w, float h) { ImGui::Dummy(ImVec2(w, h)); });
    LUA_TABLE_FUNC(imgui, Indent, [](sol::optional<float> w) { ImGui::Indent(w.value_or(0.0f)); });
    LUA_TABLE_FUNC(imgui, Unindent, [](sol::optional<float> w) { ImGui::Unindent(w.value_or(0.0f)); });
    LUA_TABLE_FUNC(imgui, BeginGroup, []() { ImGui::BeginGroup(); });
    LUA_TABLE_FUNC(imgui, EndGroup, []() { ImGui::EndGroup(); });
    LUA_TABLE_FUNC(imgui, SetNextItemWidth, [](float w) { ImGui::SetNextItemWidth(w); });

    // --------------------------------------------------- Cursor / region info
    LUA_TABLE_FUNC(imgui, GetContentRegionAvail, []() {
        const ImVec2 v = ImGui::GetContentRegionAvail();
        return std::make_tuple(v.x, v.y);
    });
    LUA_TABLE_FUNC(imgui, GetCursorPos, []() {
        const ImVec2 v = ImGui::GetCursorPos();
        return std::make_tuple(v.x, v.y);
    });
    LUA_TABLE_FUNC(imgui, GetCursorScreenPos, []() {
        const ImVec2 v = ImGui::GetCursorScreenPos();
        return std::make_tuple(v.x, v.y);
    });
    LUA_TABLE_FUNC(imgui, SetCursorPos, [](float x, float y) { ImGui::SetCursorPos(ImVec2(x, y)); });
    LUA_TABLE_FUNC(imgui, SetCursorScreenPos, [](float x, float y) { ImGui::SetCursorScreenPos(ImVec2(x, y)); });
    LUA_TABLE_FUNC(imgui, SetCursorPosX, [](float x) { ImGui::SetCursorPosX(x); });
    LUA_TABLE_FUNC(imgui, SetCursorPosY, [](float y) { ImGui::SetCursorPosY(y); });
    LUA_TABLE_FUNC(imgui, GetWindowPos, []() {
        const ImVec2 v = ImGui::GetWindowPos();
        return std::make_tuple(v.x, v.y);
    });
    LUA_TABLE_FUNC(imgui, GetWindowSize, []() {
        const ImVec2 v = ImGui::GetWindowSize();
        return std::make_tuple(v.x, v.y);
    });
    LUA_TABLE_FUNC(imgui, GetFrameHeight, []() { return ImGui::GetFrameHeight(); });
    LUA_TABLE_FUNC(imgui, GetTextLineHeight, []() { return ImGui::GetTextLineHeight(); });
    LUA_TABLE_FUNC(imgui, SetNextWindowPos, [](float x, float y, sol::optional<int> cond) {
        ImGui::SetNextWindowPos(ImVec2(x, y), cond.value_or(0));
    });
    LUA_TABLE_FUNC(imgui, SetNextWindowSize, [](float w, float h, sol::optional<int> cond) {
        ImGui::SetNextWindowSize(ImVec2(w, h), cond.value_or(0));
    });
    LUA_TABLE_FUNC(imgui, SetNextWindowBgAlpha, [](float a) { ImGui::SetNextWindowBgAlpha(a); });
    LUA_TABLE_FUNC(imgui, SetNextItemOpen, [](bool open, sol::optional<int> cond) {
        ImGui::SetNextItemOpen(open, cond.value_or(0));
    });

    // -------------------------------------------------------------- Tooltips
    LUA_TABLE_FUNC(imgui, SetTooltip, [](const char* s) { ImGui::SetTooltip("%s", s); });
    LUA_TABLE_FUNC(imgui, BeginTooltip, []() { ImGui::BeginTooltip(); });
    LUA_TABLE_FUNC(imgui, EndTooltip, []() { ImGui::EndTooltip(); });

    // ------------------------------------------------------------------ Menu
    LUA_TABLE_FUNC(imgui, BeginMainMenuBar, []() { return ImGui::BeginMainMenuBar(); });
    LUA_TABLE_FUNC(imgui, EndMainMenuBar, []() { ImGui::EndMainMenuBar(); });
    LUA_TABLE_FUNC(imgui, BeginMenuBar, []() { return ImGui::BeginMenuBar(); });
    LUA_TABLE_FUNC(imgui, EndMenuBar, []() { ImGui::EndMenuBar(); });
    LUA_TABLE_FUNC(imgui, BeginMenu, [](const char* label) { return ImGui::BeginMenu(label); });
    LUA_TABLE_FUNC(imgui, EndMenu, []() { ImGui::EndMenu(); });
    LUA_TABLE_FUNC(imgui, MenuItem, [](const char* label, sol::optional<const char*> shortcut, sol::optional<bool> selected) {
        return ImGui::MenuItem(label, shortcut.value_or(nullptr), selected.value_or(false));
    });

    // ---------------------------------------------------------------- Popups
    LUA_TABLE_FUNC(imgui, OpenPopup, [](const char* id) { ImGui::OpenPopup(id); });
    // This ImGui build exposes no per-id ClosePopup(); use CloseCurrentPopup().
    LUA_TABLE_FUNC(imgui, IsPopupOpen, [](const char* id) { return ImGui::IsPopupOpen(id); });
    LUA_TABLE_FUNC(imgui, BeginPopup, [](const char* id, sol::optional<int> flags) {
        return ImGui::BeginPopup(id, flags.value_or(0));
    });
    LUA_TABLE_FUNC(imgui, EndPopup, []() { ImGui::EndPopup(); });
    LUA_TABLE_FUNC(imgui, BeginPopupModal, [](const char* name, sol::optional<int> flags) {
        return ImGui::BeginPopupModal(name, nullptr, flags.value_or(0));
    });
    LUA_TABLE_FUNC(imgui, BeginPopupContextItem, [](sol::optional<const char*> id, sol::optional<int> flags) {
        return ImGui::BeginPopupContextItem(id.value_or(nullptr), flags.value_or(1));
    });
    LUA_TABLE_FUNC(imgui, CloseCurrentPopup, []() { ImGui::CloseCurrentPopup(); });

    // ------------------------------------------------------------------ Tabs
    LUA_TABLE_FUNC(imgui, BeginTabBar, [](const char* id, sol::optional<int> flags) {
        return ImGui::BeginTabBar(id, flags.value_or(0));
    });
    LUA_TABLE_FUNC(imgui, EndTabBar, []() { ImGui::EndTabBar(); });
    LUA_TABLE_FUNC(imgui, BeginTabItem, [](const char* label, sol::optional<int> flags) {
        return ImGui::BeginTabItem(label, nullptr, flags.value_or(0));
    });
    LUA_TABLE_FUNC(imgui, EndTabItem, []() { ImGui::EndTabItem(); });
    LUA_TABLE_FUNC(imgui, SetTabItemClosed, [](const char* label) { ImGui::SetTabItemClosed(label); });

    // ---------------------------------------------------------- Style / scope
    LUA_TABLE_FUNC(imgui, PushStyleColor, [](int idx, float r, float g, float b, float a) {
        ImGui::PushStyleColor(idx, ImVec4(r, g, b, a));
    });
    LUA_TABLE_FUNC(imgui, PopStyleColor, [](sol::optional<int> count) { ImGui::PopStyleColor(count.value_or(1)); });
    LUA_TABLE_FUNC(imgui, PushTextWrapPos, [](sol::optional<float> wrapLocalPosX) {
        ImGui::PushTextWrapPos(wrapLocalPosX.value_or(0.0f));
    });
    LUA_TABLE_FUNC(imgui, PopTextWrapPos, []() { ImGui::PopTextWrapPos(); });
    LUA_TABLE_FUNC(imgui, PushID, [](const char* id) { ImGui::PushID(id); });
    LUA_TABLE_FUNC(imgui, PopID, []() { ImGui::PopID(); });
    LUA_TABLE_FUNC(imgui, PushItemWidth, [](float w) { ImGui::PushItemWidth(w); });
    LUA_TABLE_FUNC(imgui, PopItemWidth, []() { ImGui::PopItemWidth(); });

    // ------------------------------------------------------- Query (item / mouse / key)
    LUA_TABLE_FUNC(imgui, IsItemHovered, []() { return ImGui::IsItemHovered(); });
    LUA_TABLE_FUNC(imgui, IsItemClicked, [](sol::optional<int> button) { return ImGui::IsItemClicked(button.value_or(0)); });
    LUA_TABLE_FUNC(imgui, IsItemFocused, []() { return ImGui::IsItemFocused(); });
    LUA_TABLE_FUNC(imgui, IsItemActive, []() { return ImGui::IsItemActive(); });
    LUA_TABLE_FUNC(imgui, IsItemEdited, []() { return ImGui::IsItemEdited(); });
    LUA_TABLE_FUNC(imgui, IsItemToggledOpen, []() { return ImGui::IsItemToggledOpen(); });
    LUA_TABLE_FUNC(imgui, IsMouseClicked, [](sol::optional<int> button) { return ImGui::IsMouseClicked(button.value_or(0)); });
    LUA_TABLE_FUNC(imgui, IsMouseDown, [](sol::optional<int> button) { return ImGui::IsMouseDown(button.value_or(0)); });
    LUA_TABLE_FUNC(imgui, IsMouseDoubleClicked, [](sol::optional<int> button) {
        return ImGui::IsMouseDoubleClicked(button.value_or(0));
    });
    LUA_TABLE_FUNC(imgui, GetMousePos, []() {
        const ImVec2 v = ImGui::GetMousePos();
        return std::make_tuple(v.x, v.y);
    });
    LUA_TABLE_FUNC(imgui, IsKeyPressed, [](int key, sol::optional<bool> repeat) {
        return ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), repeat.value_or(true));
    });
    LUA_TABLE_FUNC(imgui, IsKeyDown, [](int key) { return ImGui::IsKeyDown(static_cast<ImGuiKey>(key)); });
    LUA_TABLE_FUNC(imgui, SetMouseCursor, [](int cursor) { ImGui::SetMouseCursor(cursor); });
    LUA_TABLE_FUNC(imgui, GetFrameCount, []() { return static_cast<int>(ImGui::GetFrameCount()); });

    // ------------------------------------------------------------ Drag & drop
    LUA_TABLE_FUNC(imgui, BeginDragDropSource, [](sol::optional<int> flags) {
        return ImGui::BeginDragDropSource(flags.value_or(0));
    });
    LUA_TABLE_FUNC(imgui, EndDragDropSource, []() { ImGui::EndDragDropSource(); });
    LUA_TABLE_FUNC(imgui, SetDragDropPayload, [](const char* type, const std::string& data) {
        ImGui::SetDragDropPayload(type, data.data(), data.size());
    });
    LUA_TABLE_FUNC(imgui, BeginDragDropTarget, []() { return ImGui::BeginDragDropTarget(); });
    LUA_TABLE_FUNC(imgui, EndDragDropTarget, []() { ImGui::EndDragDropTarget(); });

    // ------------------------------------------------------------- Debug tools
    // ImGui::ShowDemoWindow is declared in imgui.h but not compiled into rbfx (imgui_demo.cpp
    // is excluded), so it is intentionally not bound here.
    LUA_TABLE_FUNC(imgui, getVersion, []() -> const char* { return ImGui::GetVersion(); });

    // ------------------------------------------------- Constants (flags / enums)
    sol::table windowFlags = lua.create_table();
    LUA_TABLE_ENUM(windowFlags, None, static_cast<int>(ImGuiWindowFlags_None));
    LUA_TABLE_ENUM(windowFlags, NoTitleBar, static_cast<int>(ImGuiWindowFlags_NoTitleBar));
    LUA_TABLE_ENUM(windowFlags, NoResize, static_cast<int>(ImGuiWindowFlags_NoResize));
    LUA_TABLE_ENUM(windowFlags, NoMove, static_cast<int>(ImGuiWindowFlags_NoMove));
    LUA_TABLE_ENUM(windowFlags, NoScrollbar, static_cast<int>(ImGuiWindowFlags_NoScrollbar));
    LUA_TABLE_ENUM(windowFlags, NoCollapse, static_cast<int>(ImGuiWindowFlags_NoCollapse));
    LUA_TABLE_ENUM(windowFlags, AlwaysAutoResize, static_cast<int>(ImGuiWindowFlags_AlwaysAutoResize));
    LUA_TABLE_ENUM(windowFlags, NoBackground, static_cast<int>(ImGuiWindowFlags_NoBackground));
    LUA_TABLE_ENUM(windowFlags, MenuBar, static_cast<int>(ImGuiWindowFlags_MenuBar));
    LUA_TABLE_ENUM(windowFlags, NoBringToFrontOnFocus, static_cast<int>(ImGuiWindowFlags_NoBringToFrontOnFocus));
    imgui["WindowFlags"] = windowFlags;

    sol::table cond = lua.create_table();
    LUA_TABLE_ENUM(cond, None, static_cast<int>(ImGuiCond_None));
    LUA_TABLE_ENUM(cond, Always, static_cast<int>(ImGuiCond_Always));
    LUA_TABLE_ENUM(cond, Once, static_cast<int>(ImGuiCond_Once));
    LUA_TABLE_ENUM(cond, FirstUseEver, static_cast<int>(ImGuiCond_FirstUseEver));
    imgui["Cond"] = cond;

    sol::table col = lua.create_table();
    LUA_TABLE_ENUM(col, Text, static_cast<int>(ImGuiCol_Text));
    LUA_TABLE_ENUM(col, TextDisabled, static_cast<int>(ImGuiCol_TextDisabled));
    LUA_TABLE_ENUM(col, WindowBg, static_cast<int>(ImGuiCol_WindowBg));
    LUA_TABLE_ENUM(col, ChildBg, static_cast<int>(ImGuiCol_ChildBg));
    LUA_TABLE_ENUM(col, PopupBg, static_cast<int>(ImGuiCol_PopupBg));
    LUA_TABLE_ENUM(col, Border, static_cast<int>(ImGuiCol_Border));
    LUA_TABLE_ENUM(col, FrameBg, static_cast<int>(ImGuiCol_FrameBg));
    LUA_TABLE_ENUM(col, FrameBgHovered, static_cast<int>(ImGuiCol_FrameBgHovered));
    LUA_TABLE_ENUM(col, FrameBgActive, static_cast<int>(ImGuiCol_FrameBgActive));
    LUA_TABLE_ENUM(col, TitleBg, static_cast<int>(ImGuiCol_TitleBg));
    LUA_TABLE_ENUM(col, TitleBgActive, static_cast<int>(ImGuiCol_TitleBgActive));
    LUA_TABLE_ENUM(col, MenuBarBg, static_cast<int>(ImGuiCol_MenuBarBg));
    LUA_TABLE_ENUM(col, ScrollbarBg, static_cast<int>(ImGuiCol_ScrollbarBg));
    LUA_TABLE_ENUM(col, Separator, static_cast<int>(ImGuiCol_Separator));
    LUA_TABLE_ENUM(col, CheckMark, static_cast<int>(ImGuiCol_CheckMark));
    LUA_TABLE_ENUM(col, SliderGrab, static_cast<int>(ImGuiCol_SliderGrab));
    LUA_TABLE_ENUM(col, Button, static_cast<int>(ImGuiCol_Button));
    LUA_TABLE_ENUM(col, ButtonHovered, static_cast<int>(ImGuiCol_ButtonHovered));
    LUA_TABLE_ENUM(col, ButtonActive, static_cast<int>(ImGuiCol_ButtonActive));
    LUA_TABLE_ENUM(col, Header, static_cast<int>(ImGuiCol_Header));
    LUA_TABLE_ENUM(col, HeaderHovered, static_cast<int>(ImGuiCol_HeaderHovered));
    LUA_TABLE_ENUM(col, HeaderActive, static_cast<int>(ImGuiCol_HeaderActive));
    LUA_TABLE_ENUM(col, TabSelected, static_cast<int>(ImGuiCol_TabSelected));
    LUA_TABLE_ENUM(col, ModalWindowDimBg, static_cast<int>(ImGuiCol_ModalWindowDimBg));
    imgui["Col"] = col;

    sol::table inputTextFlags = lua.create_table();
    LUA_TABLE_ENUM(inputTextFlags, None, static_cast<int>(ImGuiInputTextFlags_None));
    LUA_TABLE_ENUM(inputTextFlags, CharsDecimal, static_cast<int>(ImGuiInputTextFlags_CharsDecimal));
    LUA_TABLE_ENUM(inputTextFlags, CharsUppercase, static_cast<int>(ImGuiInputTextFlags_CharsUppercase));
    LUA_TABLE_ENUM(inputTextFlags, ReadOnly, static_cast<int>(ImGuiInputTextFlags_ReadOnly));
    LUA_TABLE_ENUM(inputTextFlags, Password, static_cast<int>(ImGuiInputTextFlags_Password));
    LUA_TABLE_ENUM(inputTextFlags, AllowTabInput, static_cast<int>(ImGuiInputTextFlags_AllowTabInput));
    LUA_TABLE_ENUM(inputTextFlags, EnterReturnsTrue, static_cast<int>(ImGuiInputTextFlags_EnterReturnsTrue));
    LUA_TABLE_ENUM(inputTextFlags, NoHorizontalScroll, static_cast<int>(ImGuiInputTextFlags_NoHorizontalScroll));
    LUA_TABLE_ENUM(inputTextFlags, CtrlEnterForNewLine, static_cast<int>(ImGuiInputTextFlags_CtrlEnterForNewLine));
    LUA_TABLE_ENUM(inputTextFlags, CallbackAlways, static_cast<int>(ImGuiInputTextFlags_CallbackAlways));
    imgui["InputTextFlags"] = inputTextFlags;

    sol::table treeNodeFlags = lua.create_table();
    LUA_TABLE_ENUM(treeNodeFlags, None, static_cast<int>(ImGuiTreeNodeFlags_None));
    LUA_TABLE_ENUM(treeNodeFlags, Selected, static_cast<int>(ImGuiTreeNodeFlags_Selected));
    LUA_TABLE_ENUM(treeNodeFlags, Framed, static_cast<int>(ImGuiTreeNodeFlags_Framed));
    LUA_TABLE_ENUM(treeNodeFlags, DefaultOpen, static_cast<int>(ImGuiTreeNodeFlags_DefaultOpen));
    LUA_TABLE_ENUM(treeNodeFlags, OpenOnArrow, static_cast<int>(ImGuiTreeNodeFlags_OpenOnArrow));
    LUA_TABLE_ENUM(treeNodeFlags, Leaf, static_cast<int>(ImGuiTreeNodeFlags_Leaf));
    LUA_TABLE_ENUM(treeNodeFlags, Bullet, static_cast<int>(ImGuiTreeNodeFlags_Bullet));
    LUA_TABLE_ENUM(treeNodeFlags, SpanAvailWidth, static_cast<int>(ImGuiTreeNodeFlags_SpanAvailWidth));
    LUA_TABLE_ENUM(treeNodeFlags, CollapsingHeader, static_cast<int>(ImGuiTreeNodeFlags_CollapsingHeader));
    imgui["TreeNodeFlags"] = treeNodeFlags;

    sol::table selectableFlags = lua.create_table();
    LUA_TABLE_ENUM(selectableFlags, None, static_cast<int>(ImGuiSelectableFlags_None));
    LUA_TABLE_ENUM(selectableFlags, NoAutoClosePopup, static_cast<int>(ImGuiSelectableFlags_NoAutoClosePopups));
    LUA_TABLE_ENUM(selectableFlags, SpanAllColumns, static_cast<int>(ImGuiSelectableFlags_SpanAllColumns));
    LUA_TABLE_ENUM(selectableFlags, AllowOverlap, static_cast<int>(ImGuiSelectableFlags_AllowOverlap));
    imgui["SelectableFlags"] = selectableFlags;

    sol::table dir = lua.create_table();
    LUA_TABLE_ENUM(dir, None, static_cast<int>(ImGuiDir_None));
    LUA_TABLE_ENUM(dir, Left, static_cast<int>(ImGuiDir_Left));
    LUA_TABLE_ENUM(dir, Right, static_cast<int>(ImGuiDir_Right));
    LUA_TABLE_ENUM(dir, Up, static_cast<int>(ImGuiDir_Up));
    LUA_TABLE_ENUM(dir, Down, static_cast<int>(ImGuiDir_Down));
    imgui["Dir"] = dir;

    sol::table key = lua.create_table();
    LUA_TABLE_ENUM(key, Tab, static_cast<int>(ImGuiKey_Tab));
    LUA_TABLE_ENUM(key, LeftArrow, static_cast<int>(ImGuiKey_LeftArrow));
    LUA_TABLE_ENUM(key, RightArrow, static_cast<int>(ImGuiKey_RightArrow));
    LUA_TABLE_ENUM(key, UpArrow, static_cast<int>(ImGuiKey_UpArrow));
    LUA_TABLE_ENUM(key, DownArrow, static_cast<int>(ImGuiKey_DownArrow));
    LUA_TABLE_ENUM(key, Enter, static_cast<int>(ImGuiKey_Enter));
    LUA_TABLE_ENUM(key, KeypadEnter, static_cast<int>(ImGuiKey_KeypadEnter));
    LUA_TABLE_ENUM(key, Escape, static_cast<int>(ImGuiKey_Escape));
    LUA_TABLE_ENUM(key, Space, static_cast<int>(ImGuiKey_Space));
    LUA_TABLE_ENUM(key, Backspace, static_cast<int>(ImGuiKey_Backspace));
    LUA_TABLE_ENUM(key, Delete, static_cast<int>(ImGuiKey_Delete));
    LUA_TABLE_ENUM(key, Home, static_cast<int>(ImGuiKey_Home));
    LUA_TABLE_ENUM(key, End, static_cast<int>(ImGuiKey_End));
    LUA_TABLE_ENUM(key, PageUp, static_cast<int>(ImGuiKey_PageUp));
    LUA_TABLE_ENUM(key, PageDown, static_cast<int>(ImGuiKey_PageDown));
    imgui["Key"] = key;

    sol::table mouseButton = lua.create_table();
    LUA_TABLE_ENUM(mouseButton, Left, static_cast<int>(ImGuiMouseButton_Left));
    LUA_TABLE_ENUM(mouseButton, Right, static_cast<int>(ImGuiMouseButton_Right));
    LUA_TABLE_ENUM(mouseButton, Middle, static_cast<int>(ImGuiMouseButton_Middle));
    imgui["MouseButton"] = mouseButton;
}

} // namespace Urho3D
