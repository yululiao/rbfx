//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaBindings.h"

// Dear ImGui is merged into Urho3D.dll and exported (IMGUI_EXPORTS on the Urho3D build);
// consumers compile with IMGUI_IMPORTS (a PUBLIC definition of the ImGui target) so the
// symbols below are imported from Urho3D.dll. RbfxLuaScript links Urho3D PUBLIC, which
// brings both the include directory and that definition transitively.
#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4244)
#endif
#include <imgui.h>
// Engine-side image widgets (Texture2D-aware) and Graphics/Texture2D.h come along with it.
#include "../Urho3D/SystemUI/Widgets.h"
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

#include <cfloat>
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
void RegisterImGuiBindings(sol::state& lua)
{
    sol::table imgui = lua.create_named_table("imgui");

    // ------------------------------------------------------------------ Text
    imgui.set_function("Text", [](const char* s) { ImGui::TextUnformatted(s); });
    imgui.set_function("TextWrapped", [](const char* s) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(s);
        ImGui::PopTextWrapPos();
    });
    imgui.set_function("TextDisabled", [](const char* s) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(s);
        ImGui::PopStyleColor();
    });
    imgui.set_function("TextColored", [](float r, float g, float b, float a, const char* s) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(r, g, b, a));
        ImGui::TextUnformatted(s);
        ImGui::PopStyleColor();
    });
    imgui.set_function("Bullet", []() { ImGui::Bullet(); });
    imgui.set_function("BulletText", [](const char* s) { ImGui::BulletText("%s", s); });

    // -------------------------------------------------------------- Windows
    imgui.set_function("Begin", [](const char* name, sol::optional<int> flags) {
        return ImGui::Begin(name, nullptr, flags.value_or(0));
    });
    imgui.set_function("End", []() { ImGui::End(); });
    imgui.set_function("BeginChild", [](const char* id, sol::optional<float> w, sol::optional<float> h,
                             sol::optional<bool> border, sol::optional<int> flags) {
        return ImGui::BeginChild(id, ImVec2(w.value_or(0.0f), h.value_or(0.0f)), border.value_or(false), flags.value_or(0));
    });
    imgui.set_function("EndChild", []() { ImGui::EndChild(); });

    // -------------------------------------------------------------- Buttons
    imgui.set_function("Button", [](const char* label, sol::optional<float> w, sol::optional<float> h) {
        return ImGui::Button(label, ImVec2(w.value_or(0.0f), h.value_or(0.0f)));
    });
    imgui.set_function("SmallButton", [](const char* label) { return ImGui::SmallButton(label); });
    imgui.set_function("ArrowButton", [](const char* id, int dir) {
        return ImGui::ArrowButton(id, static_cast<ImGuiDir>(dir));
    });
    imgui.set_function("InvisibleButton", [](const char* id, float w, float h, sol::optional<int> flags) {
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

    imgui.set_function("Image",
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
    imgui.set_function("ImageItem",
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

    imgui.set_function("ImageButton",
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
    imgui.set_function("Checkbox", [](const char* label, bool value) {
        bool v = value;
        const bool changed = ImGui::Checkbox(label, &v);
        return std::make_tuple(changed, v);
    });
    imgui.set_function("RadioButton", [](const char* label, bool active) {
        return ImGui::RadioButton(label, active);
    });
    imgui.set_function("ProgressBar", [](float fraction, sol::optional<float> w, sol::optional<float> h,
                             sol::optional<const char*> overlay) {
        ImGui::ProgressBar(fraction, ImVec2(w.value_or(-FLT_MIN), h.value_or(0.0f)), overlay.value_or(nullptr));
    });
    imgui.set_function("Combo", [](const char* label, int current, sol::table items) {
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
    imgui.set_function("DragFloat", [](const char* label, float v, sol::optional<float> speed,
                            sol::optional<float> vmin, sol::optional<float> vmax) {
        float val = v;
        const bool changed = ImGui::DragFloat(label, &val, speed.value_or(1.0f), vmin.value_or(0.0f), vmax.value_or(0.0f));
        return std::make_tuple(changed, val);
    });
    imgui.set_function("DragInt", [](const char* label, int v, sol::optional<float> speed,
                            sol::optional<int> vmin, sol::optional<int> vmax) {
        int val = v;
        const bool changed = ImGui::DragInt(label, &val, speed.value_or(1.0f), vmin.value_or(0), vmax.value_or(0));
        return std::make_tuple(changed, val);
    });
    imgui.set_function("SliderFloat", [](const char* label, float v, float vmin, float vmax,
                             sol::optional<const char*> format) {
        float val = v;
        const bool changed = ImGui::SliderFloat(label, &val, vmin, vmax, format.value_or("%.3f"));
        return std::make_tuple(changed, val);
    });
    imgui.set_function("SliderInt", [](const char* label, int v, int vmin, int vmax,
                            sol::optional<const char*> format) {
        int val = v;
        const bool changed = ImGui::SliderInt(label, &val, vmin, vmax, format.value_or("%d"));
        return std::make_tuple(changed, val);
    });
    imgui.set_function("InputFloat", [](const char* label, float v) {
        float val = v;
        const bool changed = ImGui::InputFloat(label, &val);
        return std::make_tuple(changed, val);
    });
    imgui.set_function("InputInt", [](const char* label, int v) {
        int val = v;
        const bool changed = ImGui::InputInt(label, &val);
        return std::make_tuple(changed, val);
    });
    imgui.set_function("InputText", [](const char* label, const std::string& text, sol::optional<int> flags) {
        std::vector<char> buf(text.begin(), text.end());
        buf.push_back('\0');
        buf.resize(buf.size() + 256, '\0'); // slack so freshly typed text is not clipped on the same frame
        const bool changed = ImGui::InputText(label, buf.data(), buf.size(), flags.value_or(0));
        return std::make_tuple(changed, std::string(buf.data()));
    });

    // ---------------------------------------------------------------- Color
    imgui.set_function("ColorEdit4", [](const char* label, float r, float g, float b, float a,
                             sol::optional<int> flags) {
        float col[4] = {r, g, b, a};
        const bool changed = ImGui::ColorEdit4(label, col, flags.value_or(0));
        return std::make_tuple(changed, col[0], col[1], col[2], col[3]);
    });
    imgui.set_function("ColorButton", [](const char* id, float r, float g, float b, float a,
                              sol::optional<int> flags, sol::optional<float> w, sol::optional<float> h) {
        ImGui::ColorButton(id, ImVec4(r, g, b, a), flags.value_or(0), ImVec2(w.value_or(0.0f), h.value_or(0.0f)));
    });

    // ------------------------------------------- Hierarchy (tree / header / selectable / list)
    imgui.set_function("TreeNode", [](const char* label) { return ImGui::TreeNode(label); });
    imgui.set_function("TreeNodeEx", [](const char* label, sol::optional<int> flags) {
        return ImGui::TreeNodeEx(label, flags.value_or(0));
    });
    imgui.set_function("TreePop", []() { ImGui::TreePop(); });
    imgui.set_function("CollapsingHeader", [](const char* label, sol::optional<int> flags) {
        return ImGui::CollapsingHeader(label, flags.value_or(0));
    });
    imgui.set_function("Selectable", [](const char* label, sol::optional<bool> selected, sol::optional<int> flags) {
        bool sel = selected.value_or(false);
        const bool clicked = ImGui::Selectable(label, &sel, flags.value_or(0));
        return std::make_tuple(clicked, sel);
    });
    imgui.set_function("BeginListBox", [](const char* label, sol::optional<float> w, sol::optional<float> h) {
        return ImGui::BeginListBox(label, ImVec2(w.value_or(0.0f), h.value_or(0.0f)));
    });
    imgui.set_function("EndListBox", []() { ImGui::EndListBox(); });
    // No ListBoxItem() in this ImGui build; draw rows with Selectable() between Begin/EndListBox.

    // ------------------------------------------------------- Layout helpers
    imgui.set_function("Separator", []() { ImGui::Separator(); });
    imgui.set_function("SameLine", [](sol::optional<float> offset, sol::optional<float> spacing) {
        ImGui::SameLine(offset.value_or(-1.0f), spacing.value_or(-1.0f));
    });
    imgui.set_function("NewLine", []() { ImGui::NewLine(); });
    imgui.set_function("Spacing", []() { ImGui::Spacing(); });
    imgui.set_function("Dummy", [](float w, float h) { ImGui::Dummy(ImVec2(w, h)); });
    imgui.set_function("Indent", [](sol::optional<float> w) { ImGui::Indent(w.value_or(0.0f)); });
    imgui.set_function("Unindent", [](sol::optional<float> w) { ImGui::Unindent(w.value_or(0.0f)); });
    imgui.set_function("BeginGroup", []() { ImGui::BeginGroup(); });
    imgui.set_function("EndGroup", []() { ImGui::EndGroup(); });
    imgui.set_function("SetNextItemWidth", [](float w) { ImGui::SetNextItemWidth(w); });

    // --------------------------------------------------- Cursor / region info
    imgui.set_function("GetContentRegionAvail", []() {
        const ImVec2 v = ImGui::GetContentRegionAvail();
        return std::make_tuple(v.x, v.y);
    });
    imgui.set_function("GetCursorPos", []() {
        const ImVec2 v = ImGui::GetCursorPos();
        return std::make_tuple(v.x, v.y);
    });
    imgui.set_function("GetCursorScreenPos", []() {
        const ImVec2 v = ImGui::GetCursorScreenPos();
        return std::make_tuple(v.x, v.y);
    });
    imgui.set_function("SetCursorPos", [](float x, float y) { ImGui::SetCursorPos(ImVec2(x, y)); });
    imgui.set_function("SetCursorScreenPos", [](float x, float y) { ImGui::SetCursorScreenPos(ImVec2(x, y)); });
    imgui.set_function("SetCursorPosX", [](float x) { ImGui::SetCursorPosX(x); });
    imgui.set_function("SetCursorPosY", [](float y) { ImGui::SetCursorPosY(y); });
    imgui.set_function("GetWindowPos", []() {
        const ImVec2 v = ImGui::GetWindowPos();
        return std::make_tuple(v.x, v.y);
    });
    imgui.set_function("GetWindowSize", []() {
        const ImVec2 v = ImGui::GetWindowSize();
        return std::make_tuple(v.x, v.y);
    });
    imgui.set_function("GetFrameHeight", []() { return ImGui::GetFrameHeight(); });
    imgui.set_function("GetTextLineHeight", []() { return ImGui::GetTextLineHeight(); });
    imgui.set_function("SetNextWindowPos", [](float x, float y, sol::optional<int> cond) {
        ImGui::SetNextWindowPos(ImVec2(x, y), cond.value_or(0));
    });
    imgui.set_function("SetNextWindowSize", [](float w, float h, sol::optional<int> cond) {
        ImGui::SetNextWindowSize(ImVec2(w, h), cond.value_or(0));
    });
    imgui.set_function("SetNextWindowBgAlpha", [](float a) { ImGui::SetNextWindowBgAlpha(a); });
    imgui.set_function("SetNextItemOpen", [](bool open, sol::optional<int> cond) {
        ImGui::SetNextItemOpen(open, cond.value_or(0));
    });

    // -------------------------------------------------------------- Tooltips
    imgui.set_function("SetTooltip", [](const char* s) { ImGui::SetTooltip("%s", s); });
    imgui.set_function("BeginTooltip", []() { ImGui::BeginTooltip(); });
    imgui.set_function("EndTooltip", []() { ImGui::EndTooltip(); });

    // ------------------------------------------------------------------ Menu
    imgui.set_function("BeginMainMenuBar", []() { return ImGui::BeginMainMenuBar(); });
    imgui.set_function("EndMainMenuBar", []() { ImGui::EndMainMenuBar(); });
    imgui.set_function("BeginMenuBar", []() { return ImGui::BeginMenuBar(); });
    imgui.set_function("EndMenuBar", []() { ImGui::EndMenuBar(); });
    imgui.set_function("BeginMenu", [](const char* label) { return ImGui::BeginMenu(label); });
    imgui.set_function("EndMenu", []() { ImGui::EndMenu(); });
    imgui.set_function("MenuItem", [](const char* label, sol::optional<const char*> shortcut, sol::optional<bool> selected) {
        return ImGui::MenuItem(label, shortcut.value_or(nullptr), selected.value_or(false));
    });

    // ---------------------------------------------------------------- Popups
    imgui.set_function("OpenPopup", [](const char* id) { ImGui::OpenPopup(id); });
    // This ImGui build exposes no per-id ClosePopup(); use CloseCurrentPopup().
    imgui.set_function("IsPopupOpen", [](const char* id) { return ImGui::IsPopupOpen(id); });
    imgui.set_function("BeginPopup", [](const char* id, sol::optional<int> flags) {
        return ImGui::BeginPopup(id, flags.value_or(0));
    });
    imgui.set_function("EndPopup", []() { ImGui::EndPopup(); });
    imgui.set_function("BeginPopupModal", [](const char* name, sol::optional<int> flags) {
        return ImGui::BeginPopupModal(name, nullptr, flags.value_or(0));
    });
    imgui.set_function("BeginPopupContextItem", [](sol::optional<const char*> id, sol::optional<int> flags) {
        return ImGui::BeginPopupContextItem(id.value_or(nullptr), flags.value_or(1));
    });
    imgui.set_function("CloseCurrentPopup", []() { ImGui::CloseCurrentPopup(); });

    // ------------------------------------------------------------------ Tabs
    imgui.set_function("BeginTabBar", [](const char* id, sol::optional<int> flags) {
        return ImGui::BeginTabBar(id, flags.value_or(0));
    });
    imgui.set_function("EndTabBar", []() { ImGui::EndTabBar(); });
    imgui.set_function("BeginTabItem", [](const char* label, sol::optional<int> flags) {
        return ImGui::BeginTabItem(label, nullptr, flags.value_or(0));
    });
    imgui.set_function("EndTabItem", []() { ImGui::EndTabItem(); });
    imgui.set_function("SetTabItemClosed", [](const char* label) { ImGui::SetTabItemClosed(label); });

    // ---------------------------------------------------------- Style / scope
    imgui.set_function("PushStyleColor", [](int idx, float r, float g, float b, float a) {
        ImGui::PushStyleColor(idx, ImVec4(r, g, b, a));
    });
    imgui.set_function("PopStyleColor", [](sol::optional<int> count) { ImGui::PopStyleColor(count.value_or(1)); });
    imgui.set_function("PushTextWrapPos", [](sol::optional<float> wrapLocalPosX) {
        ImGui::PushTextWrapPos(wrapLocalPosX.value_or(0.0f));
    });
    imgui.set_function("PopTextWrapPos", []() { ImGui::PopTextWrapPos(); });
    imgui.set_function("PushID", [](const char* id) { ImGui::PushID(id); });
    imgui.set_function("PopID", []() { ImGui::PopID(); });
    imgui.set_function("PushItemWidth", [](float w) { ImGui::PushItemWidth(w); });
    imgui.set_function("PopItemWidth", []() { ImGui::PopItemWidth(); });

    // ------------------------------------------------------- Query (item / mouse / key)
    imgui.set_function("IsItemHovered", []() { return ImGui::IsItemHovered(); });
    imgui.set_function("IsItemClicked", [](sol::optional<int> button) { return ImGui::IsItemClicked(button.value_or(0)); });
    imgui.set_function("IsItemFocused", []() { return ImGui::IsItemFocused(); });
    imgui.set_function("IsItemActive", []() { return ImGui::IsItemActive(); });
    imgui.set_function("IsItemEdited", []() { return ImGui::IsItemEdited(); });
    imgui.set_function("IsItemToggledOpen", []() { return ImGui::IsItemToggledOpen(); });
    imgui.set_function("IsMouseClicked", [](sol::optional<int> button) { return ImGui::IsMouseClicked(button.value_or(0)); });
    imgui.set_function("IsMouseDown", [](sol::optional<int> button) { return ImGui::IsMouseDown(button.value_or(0)); });
    imgui.set_function("IsMouseDoubleClicked", [](sol::optional<int> button) {
        return ImGui::IsMouseDoubleClicked(button.value_or(0));
    });
    imgui.set_function("GetMousePos", []() {
        const ImVec2 v = ImGui::GetMousePos();
        return std::make_tuple(v.x, v.y);
    });
    imgui.set_function("IsKeyPressed", [](int key, sol::optional<bool> repeat) {
        return ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), repeat.value_or(true));
    });
    imgui.set_function("IsKeyDown", [](int key) { return ImGui::IsKeyDown(static_cast<ImGuiKey>(key)); });
    imgui.set_function("SetMouseCursor", [](int cursor) { ImGui::SetMouseCursor(cursor); });
    imgui.set_function("GetFrameCount", []() { return static_cast<int>(ImGui::GetFrameCount()); });

    // ------------------------------------------------------------ Drag & drop
    imgui.set_function("BeginDragDropSource", [](sol::optional<int> flags) {
        return ImGui::BeginDragDropSource(flags.value_or(0));
    });
    imgui.set_function("EndDragDropSource", []() { ImGui::EndDragDropSource(); });
    imgui.set_function("SetDragDropPayload", [](const char* type, const std::string& data) {
        ImGui::SetDragDropPayload(type, data.data(), data.size());
    });
    imgui.set_function("BeginDragDropTarget", []() { return ImGui::BeginDragDropTarget(); });
    imgui.set_function("EndDragDropTarget", []() { ImGui::EndDragDropTarget(); });

    // ------------------------------------------------------------- Debug tools
    // ImGui::ShowDemoWindow is declared in imgui.h but not compiled into rbfx (imgui_demo.cpp
    // is excluded), so it is intentionally not bound here.
    imgui.set_function("getVersion", []() -> const char* { return ImGui::GetVersion(); });

    // ------------------------------------------------- Constants (flags / enums)
    sol::table windowFlags = lua.create_table();
    windowFlags["None"] = static_cast<int>(ImGuiWindowFlags_None);
    windowFlags["NoTitleBar"] = static_cast<int>(ImGuiWindowFlags_NoTitleBar);
    windowFlags["NoResize"] = static_cast<int>(ImGuiWindowFlags_NoResize);
    windowFlags["NoMove"] = static_cast<int>(ImGuiWindowFlags_NoMove);
    windowFlags["NoScrollbar"] = static_cast<int>(ImGuiWindowFlags_NoScrollbar);
    windowFlags["NoCollapse"] = static_cast<int>(ImGuiWindowFlags_NoCollapse);
    windowFlags["AlwaysAutoResize"] = static_cast<int>(ImGuiWindowFlags_AlwaysAutoResize);
    windowFlags["NoBackground"] = static_cast<int>(ImGuiWindowFlags_NoBackground);
    windowFlags["MenuBar"] = static_cast<int>(ImGuiWindowFlags_MenuBar);
    windowFlags["NoBringToFrontOnFocus"] = static_cast<int>(ImGuiWindowFlags_NoBringToFrontOnFocus);
    imgui["WindowFlags"] = windowFlags;

    sol::table cond = lua.create_table();
    cond["None"] = static_cast<int>(ImGuiCond_None);
    cond["Always"] = static_cast<int>(ImGuiCond_Always);
    cond["Once"] = static_cast<int>(ImGuiCond_Once);
    cond["FirstUseEver"] = static_cast<int>(ImGuiCond_FirstUseEver);
    imgui["Cond"] = cond;

    sol::table col = lua.create_table();
    col["Text"] = static_cast<int>(ImGuiCol_Text);
    col["TextDisabled"] = static_cast<int>(ImGuiCol_TextDisabled);
    col["WindowBg"] = static_cast<int>(ImGuiCol_WindowBg);
    col["ChildBg"] = static_cast<int>(ImGuiCol_ChildBg);
    col["PopupBg"] = static_cast<int>(ImGuiCol_PopupBg);
    col["Border"] = static_cast<int>(ImGuiCol_Border);
    col["FrameBg"] = static_cast<int>(ImGuiCol_FrameBg);
    col["FrameBgHovered"] = static_cast<int>(ImGuiCol_FrameBgHovered);
    col["FrameBgActive"] = static_cast<int>(ImGuiCol_FrameBgActive);
    col["TitleBg"] = static_cast<int>(ImGuiCol_TitleBg);
    col["TitleBgActive"] = static_cast<int>(ImGuiCol_TitleBgActive);
    col["MenuBarBg"] = static_cast<int>(ImGuiCol_MenuBarBg);
    col["ScrollbarBg"] = static_cast<int>(ImGuiCol_ScrollbarBg);
    col["Separator"] = static_cast<int>(ImGuiCol_Separator);
    col["CheckMark"] = static_cast<int>(ImGuiCol_CheckMark);
    col["SliderGrab"] = static_cast<int>(ImGuiCol_SliderGrab);
    col["Button"] = static_cast<int>(ImGuiCol_Button);
    col["ButtonHovered"] = static_cast<int>(ImGuiCol_ButtonHovered);
    col["ButtonActive"] = static_cast<int>(ImGuiCol_ButtonActive);
    col["Header"] = static_cast<int>(ImGuiCol_Header);
    col["HeaderHovered"] = static_cast<int>(ImGuiCol_HeaderHovered);
    col["HeaderActive"] = static_cast<int>(ImGuiCol_HeaderActive);
    col["TabSelected"] = static_cast<int>(ImGuiCol_TabSelected);
    col["ModalWindowDimBg"] = static_cast<int>(ImGuiCol_ModalWindowDimBg);
    imgui["Col"] = col;

    sol::table inputTextFlags = lua.create_table();
    inputTextFlags["None"] = static_cast<int>(ImGuiInputTextFlags_None);
    inputTextFlags["CharsDecimal"] = static_cast<int>(ImGuiInputTextFlags_CharsDecimal);
    inputTextFlags["CharsUppercase"] = static_cast<int>(ImGuiInputTextFlags_CharsUppercase);
    inputTextFlags["ReadOnly"] = static_cast<int>(ImGuiInputTextFlags_ReadOnly);
    inputTextFlags["Password"] = static_cast<int>(ImGuiInputTextFlags_Password);
    inputTextFlags["AllowTabInput"] = static_cast<int>(ImGuiInputTextFlags_AllowTabInput);
    inputTextFlags["EnterReturnsTrue"] = static_cast<int>(ImGuiInputTextFlags_EnterReturnsTrue);
    inputTextFlags["NoHorizontalScroll"] = static_cast<int>(ImGuiInputTextFlags_NoHorizontalScroll);
    inputTextFlags["CtrlEnterForNewLine"] = static_cast<int>(ImGuiInputTextFlags_CtrlEnterForNewLine);
    inputTextFlags["CallbackAlways"] = static_cast<int>(ImGuiInputTextFlags_CallbackAlways);
    imgui["InputTextFlags"] = inputTextFlags;

    sol::table treeNodeFlags = lua.create_table();
    treeNodeFlags["None"] = static_cast<int>(ImGuiTreeNodeFlags_None);
    treeNodeFlags["Selected"] = static_cast<int>(ImGuiTreeNodeFlags_Selected);
    treeNodeFlags["Framed"] = static_cast<int>(ImGuiTreeNodeFlags_Framed);
    treeNodeFlags["DefaultOpen"] = static_cast<int>(ImGuiTreeNodeFlags_DefaultOpen);
    treeNodeFlags["OpenOnArrow"] = static_cast<int>(ImGuiTreeNodeFlags_OpenOnArrow);
    treeNodeFlags["Leaf"] = static_cast<int>(ImGuiTreeNodeFlags_Leaf);
    treeNodeFlags["Bullet"] = static_cast<int>(ImGuiTreeNodeFlags_Bullet);
    treeNodeFlags["SpanAvailWidth"] = static_cast<int>(ImGuiTreeNodeFlags_SpanAvailWidth);
    treeNodeFlags["CollapsingHeader"] = static_cast<int>(ImGuiTreeNodeFlags_CollapsingHeader);
    imgui["TreeNodeFlags"] = treeNodeFlags;

    sol::table selectableFlags = lua.create_table();
    selectableFlags["None"] = static_cast<int>(ImGuiSelectableFlags_None);
    selectableFlags["NoAutoClosePopup"] = static_cast<int>(ImGuiSelectableFlags_NoAutoClosePopups);
    selectableFlags["SpanAllColumns"] = static_cast<int>(ImGuiSelectableFlags_SpanAllColumns);
    selectableFlags["AllowOverlap"] = static_cast<int>(ImGuiSelectableFlags_AllowOverlap);
    imgui["SelectableFlags"] = selectableFlags;

    sol::table dir = lua.create_table();
    dir["None"] = static_cast<int>(ImGuiDir_None);
    dir["Left"] = static_cast<int>(ImGuiDir_Left);
    dir["Right"] = static_cast<int>(ImGuiDir_Right);
    dir["Up"] = static_cast<int>(ImGuiDir_Up);
    dir["Down"] = static_cast<int>(ImGuiDir_Down);
    imgui["Dir"] = dir;

    sol::table key = lua.create_table();
    key["Tab"] = static_cast<int>(ImGuiKey_Tab);
    key["LeftArrow"] = static_cast<int>(ImGuiKey_LeftArrow);
    key["RightArrow"] = static_cast<int>(ImGuiKey_RightArrow);
    key["UpArrow"] = static_cast<int>(ImGuiKey_UpArrow);
    key["DownArrow"] = static_cast<int>(ImGuiKey_DownArrow);
    key["Enter"] = static_cast<int>(ImGuiKey_Enter);
    key["KeypadEnter"] = static_cast<int>(ImGuiKey_KeypadEnter);
    key["Escape"] = static_cast<int>(ImGuiKey_Escape);
    key["Space"] = static_cast<int>(ImGuiKey_Space);
    key["Backspace"] = static_cast<int>(ImGuiKey_Backspace);
    key["Delete"] = static_cast<int>(ImGuiKey_Delete);
    key["Home"] = static_cast<int>(ImGuiKey_Home);
    key["End"] = static_cast<int>(ImGuiKey_End);
    key["PageUp"] = static_cast<int>(ImGuiKey_PageUp);
    key["PageDown"] = static_cast<int>(ImGuiKey_PageDown);
    imgui["Key"] = key;

    sol::table mouseButton = lua.create_table();
    mouseButton["Left"] = static_cast<int>(ImGuiMouseButton_Left);
    mouseButton["Right"] = static_cast<int>(ImGuiMouseButton_Right);
    mouseButton["Middle"] = static_cast<int>(ImGuiMouseButton_Middle);
    imgui["MouseButton"] = mouseButton;
}

} // namespace Urho3D
