//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Precompiled.h"

#include "../LuaScript/LuaBindings.h"

#include "../Core/Context.h"
#include "../IO/Log.h"
#include "../UI/BorderImage.h"
#include "../UI/Button.h"
#include "../UI/CheckBox.h"
#include "../UI/Cursor.h"
#include "../UI/DropDownList.h"
#include "../UI/Font.h"
#include "../UI/LineEdit.h"
#include "../UI/ListView.h"
#include "../UI/Slider.h"
#include "../UI/Sprite.h"
#include "../UI/Text.h"
#include "../UI/ToolTip.h"
#include "../UI/UI.h"
#include "../UI/UIComponent.h"
#include "../SystemUI/Console.h"
#include "../Scene/ValueAnimation.h"
#include "../Scene/Animatable.h"
#include "../UI/UIElement.h"
#include "../UI/Window.h"

#include <sol/sol.hpp>

namespace sol
{

template <> struct is_automagical<Urho3D::Console> : std::false_type {};

template <> struct is_automagical<Urho3D::UIElement> : std::false_type {};
template <> struct is_automagical<Urho3D::Font> : std::false_type {};
template <> struct is_automagical<Urho3D::Text> : std::false_type {};
template <> struct is_automagical<Urho3D::BorderImage> : std::false_type {};
template <> struct is_automagical<Urho3D::Sprite> : std::false_type {};
template <> struct is_automagical<Urho3D::Button> : std::false_type {};
template <> struct is_automagical<Urho3D::Window> : std::false_type {};
template <> struct is_automagical<Urho3D::CheckBox> : std::false_type {};
template <> struct is_automagical<Urho3D::Slider> : std::false_type {};
template <> struct is_automagical<Urho3D::DropDownList> : std::false_type {};
template <> struct is_automagical<Urho3D::ListView> : std::false_type {};
template <> struct is_automagical<Urho3D::ToolTip> : std::false_type {};
template <> struct is_automagical<Urho3D::UIComponent> : std::false_type {};
template <> struct is_automagical<Urho3D::LineEdit> : std::false_type {};
template <> struct is_automagical<Urho3D::Cursor> : std::false_type {};
template <> struct is_automagical<Urho3D::UI> : std::false_type {};

} // namespace sol

namespace Urho3D
{

void RegisterUIBindings(sol::state& lua, Context* context)
{
    // Root widget base: transforms, alignment, layout, children. Widget types
    // without an explicit registration (LineEdit, CheckBox, ...) are created
    // through UIElement::CreateChild and stay usable via attribute reflection.
    lua.new_usertype<UIElement>("UIElement",
        sol::no_constructor,
        sol::base_classes, sol::bases<Serializable, Object>(),
        // sol3 resolves base-class members only one level deep, so widgets
        // derived from BorderImage must list the full chain to also reach
        // UIElement's methods (02_HelloGUI SetMinWidth, 16_Chat SetStyleAuto).
        "SetName", [](UIElement* element, const char* name) { if (element) element->SetName(name); },
        "GetName", [](UIElement* element) -> std::string { return element ? element->GetName().c_str() : ""; },
        "SetPosition", sol::overload(
            static_cast<void (UIElement::*)(const IntVector2&)>(&UIElement::SetPosition),
            static_cast<void (UIElement::*)(int, int)>(&UIElement::SetPosition),
            // Lua arithmetic (e.g. width / 2) yields floats; accept them here
            [](UIElement* element, double x, double y) {
                if (element)
                    element->SetPosition(static_cast<int>(x), static_cast<int>(y));
            }),
        "GetPosition", &UIElement::GetPosition,
        "SetSize", sol::overload(
            static_cast<void (UIElement::*)(const IntVector2&)>(&UIElement::SetSize),
            static_cast<void (UIElement::*)(int, int)>(&UIElement::SetSize),
            [](UIElement* element, double w, double h) {
                if (element)
                    element->SetSize(static_cast<int>(w), static_cast<int>(h));
            }),
        "GetSize", &UIElement::GetSize,
        "SetWidth", &UIElement::SetWidth,
        "SetHeight", &UIElement::SetHeight,
        "SetFixedWidth", &UIElement::SetFixedWidth,
        "SetFixedHeight", &UIElement::SetFixedHeight,
        "SetFixedSize", sol::overload(
            static_cast<void (UIElement::*)(const IntVector2&)>(&UIElement::SetFixedSize),
            static_cast<void (UIElement::*)(int, int)>(&UIElement::SetFixedSize),
            [](UIElement* element, double w, double h) {
                if (element)
                    element->SetFixedSize(static_cast<int>(w), static_cast<int>(h));
            }),
        "GetWidth", &UIElement::GetWidth,
        "GetHeight", &UIElement::GetHeight,
        "SetMinSize", sol::overload(
            static_cast<void (UIElement::*)(const IntVector2&)>(&UIElement::SetMinSize),
            static_cast<void (UIElement::*)(int, int)>(&UIElement::SetMinSize),
            [](UIElement* element, double w, double h) {
                if (element)
                    element->SetMinSize(static_cast<int>(w), static_cast<int>(h));
            }),
        "SetMaxSize", sol::overload(
            static_cast<void (UIElement::*)(const IntVector2&)>(&UIElement::SetMaxSize),
            static_cast<void (UIElement::*)(int, int)>(&UIElement::SetMaxSize),
            [](UIElement* element, double w, double h) {
                if (element)
                    element->SetMaxSize(static_cast<int>(w), static_cast<int>(h));
            }),
        // Lua arithmetic on GetRowWidth() results yields floats; accept them.
        "SetMinWidth", [](UIElement* element, double width) {
            if (element)
                element->SetMinWidth(static_cast<int>(width));
        },
        "SetMinHeight", [](UIElement* element, double height) {
            if (element)
                element->SetMinHeight(static_cast<int>(height));
        },
        "SetMaxWidth", [](UIElement* element, double width) {
            if (element)
                element->SetMaxWidth(static_cast<int>(width));
        },
        "SetMaxHeight", [](UIElement* element, double height) {
            if (element)
                element->SetMaxHeight(static_cast<int>(height));
        },
        "SetDefaultStyle", &UIElement::SetDefaultStyle,
        "SetAlignment", [](UIElement* element, int hAlign, int vAlign) {
            if (element)
                element->SetAlignment(static_cast<HorizontalAlignment>(hAlign), static_cast<VerticalAlignment>(vAlign));
        },
        "SetHorizontalAlignment", [](UIElement* element, int align) {
            if (element)
                element->SetHorizontalAlignment(static_cast<HorizontalAlignment>(align));
        },
        "SetVerticalAlignment", [](UIElement* element, int align) {
            if (element)
                element->SetVerticalAlignment(static_cast<VerticalAlignment>(align));
        },
        "SetColor", [](UIElement* element, const Color& color) {
            if (element)
                element->SetColor(color);
        },
        "SetOpacity", &UIElement::SetOpacity,
        "SetUseDerivedOpacity", &UIElement::SetUseDerivedOpacity,
        "SetVisible", &UIElement::SetVisible,
        "IsVisible", &UIElement::IsVisible,
        "SetEnabled", &UIElement::SetEnabled,
        "SetFocus", &UIElement::SetFocus,
        "SetBringToFront", &UIElement::SetBringToFront,
        "SetLayout", [](UIElement* element, int mode, sol::optional<int> spacing, sol::optional<IntRect> border) {
            if (element)
                element->SetLayout(static_cast<LayoutMode>(mode), spacing.value_or(0), border.value_or(IntRect::ZERO));
        },
        "SetLayoutSpacing", &UIElement::SetLayoutSpacing,
        "SetLayoutBorder", &UIElement::SetLayoutBorder,
        "SetLayoutMode", [](UIElement* element, int mode) {
            if (element)
                element->SetLayoutMode(static_cast<LayoutMode>(mode));
        },
        "SetStyle", [](UIElement* element, const char* styleName) -> bool {
            return element && element->SetStyle(styleName);
        },
        "SetStyleAuto", [](UIElement* element) -> bool {
            return element && element->SetStyleAuto();
        },
        "CreateChild", [](UIElement* element, const char* typeName, sol::optional<const char*> name,
            sol::this_state s) -> sol::object {
            if (!element)
                return sol::lua_nil;
            UIElement* child = element->CreateChild(StringHash(typeName), name ? *name : "", M_MAX_UNSIGNED);
            if (!child)
            {
                URHO3D_LOGERROR("UIElement type '{}' is not registered", typeName);
                return sol::lua_nil;
            }
            return WrapLuaObject(sol::state_view(s), child);
        },
        "AddChild", &UIElement::AddChild,
        "RemoveChild", [](UIElement* parent, UIElement* child) {
            if (parent && child)
                parent->RemoveChild(child);
        },
        "RemoveAllChildren", &UIElement::RemoveAllChildren,
        "GetChild", [](UIElement* element, const char* name, sol::optional<bool> recursive,
            sol::this_state s) -> sol::object {
            if (!element)
                return sol::lua_nil;
            return WrapLuaObject(sol::state_view(s), element->GetChild(name, recursive.value_or(false)));
        },
        "GetChildren", [](UIElement* element, sol::this_state s) -> sol::table {
            sol::state_view lua(s);
            sol::table result = lua.create_table();
            if (element)
            {
                unsigned index = 1;
                for (const SharedPtr<UIElement>& child : element->GetChildren())
                    result[index++] = child.Get();
            }
            return result;
        },
        "GetParent", &UIElement::GetParent,
        "SetVar", [](UIElement* element, const char* key, sol::object value, sol::this_state s) {
            if (element)
                element->SetVar(key, LuaToVariant(sol::state_view(s), value));
        },
        "GetVar", [](UIElement* element, const char* key, sol::this_state s) -> sol::object {
            return element ? VariantToLua(sol::state_view(s), element->GetVar(key)) : sol::lua_nil;
        },
        // Manual layout refresh and z-order control (37_UIDrag).
        "BringToFront", &UIElement::BringToFront,
        "UpdateLayout", &UIElement::UpdateLayout,
        "HasFocus", &UIElement::HasFocus,
        "GetMinWidth", &UIElement::GetMinWidth,
        // Tags for grouping elements (37_UIDrag drag sources).
        "AddTag", [](UIElement* element, const char* tag) {
            if (element)
                element->AddTag(tag);
        },
        "HasTag", &UIElement::HasTag,
        "GetChildrenWithTag", [](UIElement* element, const char* tag, sol::optional<bool> recursive,
            sol::this_state s) -> sol::table {
            sol::state_view lua(s);
            sol::table result = lua.create_table();
            if (element)
            {
                unsigned index = 1;
                for (UIElement* child : element->GetChildrenWithTag(tag, recursive.value_or(false)))
                    result[index++] = WrapLuaObject(lua, child);
            }
            return result;
        },
        // Keyboard focus behavior (49/50 Sample2D UI buttons).
        "SetFocusMode", [](UIElement* element, int mode) {
            if (element)
                element->SetFocusMode(static_cast<FocusMode>(mode));
        },
        // Render order within the parent (37_UIDrag windows above sprites).
        "SetPriority", &UIElement::SetPriority,
        // Object lifetime hints.
        "SetTemporary", &UIElement::SetTemporary,
        // Attribute animation, e.g. animating Text's "Text" attribute
        // (30_LightAnimation).
        "SetAttributeAnimation", [](UIElement* element, const char* name,
            ValueAnimation* animation, sol::optional<int> wrapMode, sol::optional<float> speed) {
            if (element)
                element->SetAttributeAnimation(name, animation,
                    static_cast<WrapMode>(wrapMode.value_or(WM_LOOP)), speed.value_or(1.0f));
        }
    );
    RegisterLuaObjectWrapper<UIElement>();

    // Font resource: only a type marker, consumed by Text:SetFont.
    lua.new_usertype<Font>("Font",
        sol::no_constructor,
        sol::base_classes, sol::bases<Resource, Object>()
    );
    RegisterLuaObjectWrapper<Font>();

    // Text label. Constructible standalone so it can be filled into
    // ListView/DropDownList item slots (47_Typography, 54_WindowSettings).
    lua.new_usertype<Text>("Text",
        sol::call_constructor, sol::factories([context]() {
            return SharedPtr<Text>(new Text(context));
        }),
        sol::base_classes, sol::bases<UIElement, Serializable, Object>(),
        "SetText", [](Text* text, const char* value) {
            if (text)
                text->SetText(value);
        },
        "SetFont", sol::overload(
            [](Text* text, Font* font) -> bool { return text && text->SetFont(font); },
            [](Text* text, Font* font, float size) -> bool { return text && text->SetFont(font, size); },
            [](Text* text, const char* fontName, float size) -> bool { return text && text->SetFont(fontName, size); }),
        "SetFontSize", &Text::SetFontSize,
        "SetTextAlignment", [](Text* text, int align) {
            if (text)
                text->SetTextAlignment(static_cast<HorizontalAlignment>(align));
        },
        "SetRowSpacing", &Text::SetRowSpacing,
        "SetWordwrap", &Text::SetWordwrap,
        "GetNumRows", &Text::GetNumRows,
        "GetRowHeight", &Text::GetRowHeight,
        "GetRowWidth", &Text::GetRowWidth,
        // Text effects (35_SignedDistanceFieldText).
        "SetTextEffect", [](Text* text, int effect) {
            if (text)
                text->SetTextEffect(static_cast<TextEffect>(effect));
        },
        "SetEffectColor", &Text::SetEffectColor,
        "SetEffectDepthBias", &Text::SetEffectDepthBias,
        // Localization of the displayed string (40_Localization).
        "SetAutoLocalizable", &Text::SetAutoLocalizable
    );
    RegisterLuaObjectWrapper<Text>();

    // Text effect constants.
    sol::table te = lua.create_named_table("TE");
    te["NONE"] = TE_NONE;
    te["SHADOW"] = TE_SHADOW;
    te["STROKE"] = TE_STROKE;

    // LineEdit: single-line text input with TextFinished event (16_Chat).
    lua.new_usertype<LineEdit>("LineEdit",
        sol::no_constructor,
        sol::base_classes, sol::bases<UIElement, Serializable, Object>(),
        "SetText", [](LineEdit* edit, const char* value) {
            if (edit)
                edit->SetText(value);
        },
        "GetText", [](LineEdit* edit) -> const char* { return edit ? edit->GetText().c_str() : ""; }
    );
    RegisterLuaObjectWrapper<LineEdit>();

    // BorderImage: textured widget base.
    lua.new_usertype<BorderImage>("BorderImage",
        sol::no_constructor,
        sol::base_classes, sol::bases<UIElement, Serializable, Object>(),
        "SetTexture", &BorderImage::SetTexture,
        "GetTexture", &BorderImage::GetTexture,
        "SetBlendMode", [](BorderImage* image, int mode) {
            if (image)
                image->SetBlendMode(static_cast<BlendMode>(mode));
        },
        // Sub-rectangle of the texture to display (49/50 UI backgrounds).
        "SetImageRect", &BorderImage::SetImageRect,
        "SetFullImageRect", &BorderImage::SetFullImageRect
    );
    RegisterLuaObjectWrapper<BorderImage>();

    // Button: styled through SetStyle("Button") + released/pressed events.
    lua.new_usertype<Button>("Button",
        sol::no_constructor,
        sol::base_classes, sol::bases<BorderImage, UIElement, Serializable, Object>()
    );
    RegisterLuaObjectWrapper<Button>();

    // Window: draggable container with optional modality.
    lua.new_usertype<Window>("Window",
        sol::no_constructor,
        sol::base_classes, sol::bases<BorderImage, UIElement, Serializable, Object>(),
        "SetModal", &Window::SetModal,
        "SetMovable", &Window::SetMovable
    );
    RegisterLuaObjectWrapper<Window>();

    // CheckBox: two-state toggle widget (14_SoundEffects).
    lua.new_usertype<CheckBox>("CheckBox",
        sol::no_constructor,
        sol::base_classes, sol::bases<BorderImage, UIElement, Serializable, Object>(),
        "SetChecked", &CheckBox::SetChecked,
        "IsChecked", &CheckBox::IsChecked
    );
    RegisterLuaObjectWrapper<CheckBox>();

    // Slider: float value in a range, fires SliderChanged events.
    lua.new_usertype<Slider>("Slider",
        sol::no_constructor,
        sol::base_classes, sol::bases<BorderImage, UIElement, Serializable, Object>(),
        "SetRange", &Slider::SetRange,
        "SetValue", &Slider::SetValue,
        "GetValue", &Slider::GetValue
    );
    RegisterLuaObjectWrapper<Slider>();

    // DropDownList: item picker with a popup list (14_SoundEffects).
    lua.new_usertype<DropDownList>("DropDownList",
        sol::no_constructor,
        sol::base_classes, sol::bases<BorderImage, UIElement, Serializable, Object>(),
        "AddItem", &DropDownList::AddItem,
        "RemoveAllItems", &DropDownList::RemoveAllItems,
        "SetSelection", [](DropDownList* list, double index) {
            if (list)
                list->SetSelection(static_cast<unsigned>(index));
        },
        "GetSelection", &DropDownList::GetSelection,
        "GetNumItems", &DropDownList::GetNumItems,
        "GetItem", &DropDownList::GetItem,
        "GetSelectedItem", &DropDownList::GetSelectedItem
    );
    RegisterLuaObjectWrapper<DropDownList>();

    // ListView: scrollable item list (47_Typography, 54_WindowSettings).
    lua.new_usertype<ListView>("ListView",
        sol::no_constructor,
        sol::base_classes, sol::bases<UIElement, Serializable, Object>(),
        "AddItem", &ListView::AddItem,
        "RemoveAllItems", &ListView::RemoveAllItems,
        "SetSelection", [](ListView* list, double index) {
            if (list)
                list->SetSelection(static_cast<unsigned>(index));
        },
        "GetSelection", &ListView::GetSelection,
        "GetNumItems", &ListView::GetNumItems,
        "GetItem", &ListView::GetItem,
        "SetHighlightMode", [](ListView* list, int mode) {
            if (list)
                list->SetHighlightMode(static_cast<HighlightMode>(mode));
        },
        "SetSelectOnClickEnd", &ListView::SetSelectOnClickEnd
    );
    RegisterLuaObjectWrapper<ListView>();

    // ToolTip: hover help container attached to a widget (48_Hello3DUI).
    lua.new_usertype<ToolTip>("ToolTip",
        sol::no_constructor,
        sol::base_classes, sol::bases<UIElement, Serializable, Object>()
    );
    RegisterLuaObjectWrapper<ToolTip>();

    // Sprite: textured quad for 2D overlay work (18_Urho2DSprite).
    lua.new_usertype<Sprite>("Sprite",
        sol::no_constructor,
        sol::base_classes, sol::bases<UIElement, Serializable, Object>(),
        "SetTexture", &Sprite::SetTexture,
        "SetImageRect", &Sprite::SetImageRect,
        "SetBlendMode", [](Sprite* sprite, int mode) {
            if (sprite)
                sprite->SetBlendMode(static_cast<BlendMode>(mode));
        },
        "SetHotSpot", sol::overload(
            static_cast<void (Sprite::*)(const IntVector2&)>(&Sprite::SetHotSpot),
            static_cast<void (Sprite::*)(int, int)>(&Sprite::SetHotSpot),
            [](Sprite* sprite, double x, double y) {
                if (sprite)
                    sprite->SetHotSpot(static_cast<int>(x), static_cast<int>(y));
            }),
        "SetScale", sol::overload(
            static_cast<void (Sprite::*)(const Vector2&)>(&Sprite::SetScale),
            static_cast<void (Sprite::*)(float, float)>(&Sprite::SetScale),
            static_cast<void (Sprite::*)(float)>(&Sprite::SetScale)),
        "SetRotation", &Sprite::SetRotation,
        "GetRotation", &Sprite::GetRotation
    );
    RegisterLuaObjectWrapper<Sprite>();

    // Cursor: OS/software cursor element (08_Decals and friends).
    // Constructed explicitly and installed through UI:SetCursor when the
    // sample needs to toggle cursor visibility (17_SceneReplication).
    lua.new_usertype<Cursor>("Cursor",
        sol::call_constructor, sol::factories([context]() {
            return SharedPtr<Cursor>(new Cursor(context));
        }),
        sol::base_classes, sol::bases<UIElement, Serializable, Object>()
    );
    RegisterLuaObjectWrapper<Cursor>();

    // UI subsystem: root element access and global scale.
    lua.new_usertype<UI>("UI",
        sol::no_constructor,
        sol::base_classes, sol::bases<Object>(),
        "GetRoot", &UI::GetRoot,
        "GetRootModalElement", &UI::GetRootModalElement,
        "GetFocusElement", &UI::GetFocusElement,
        "GetCursor", &UI::GetCursor,
        "SetCursor", &UI::SetCursor,
        "GetUICursorPosition", &UI::GetUICursorPosition,
        "GetElementAt", [](UI* ui, const IntVector2& position, sol::optional<bool> enabledOnly) {
            return ui ? ui->GetElementAt(position, enabledOnly.value_or(true)) : nullptr;
        },
        "ConvertUIToSystem", &UI::ConvertUIToSystem,
        "SetScale", &UI::SetScale,
        "SetUseSystemClipboard", &UI::SetUseSystemClipboard,
        // Font rendering configuration (47_Typography).
        "SetForceAutoHint", &UI::SetForceAutoHint,
        "GetForceAutoHint", &UI::GetForceAutoHint,
        "SetFontHintLevel", [](UI* ui, int level) {
            if (ui)
                ui->SetFontHintLevel(static_cast<FontHintLevel>(level));
        },
        "GetFontHintLevel", [](UI* ui) {
            return ui ? static_cast<int>(ui->GetFontHintLevel()) : 0;
        },
        "SetFontSubpixelThreshold", &UI::SetFontSubpixelThreshold,
        "GetFontSubpixelThreshold", &UI::GetFontSubpixelThreshold,
        "SetFontOversampling", &UI::SetFontOversampling,
        "GetFontOversampling", &UI::GetFontOversampling,
        // Immediate-mode debug outline of the element hierarchy
        // (48_Hello3DUI).
        "DebugDraw", &UI::DebugDraw,
        // Instantiate a widget tree from a layout XML file (38_SceneAndUILoad).
        "LoadLayout", [](UI* ui, XMLFile* file, sol::optional<XMLFile*> styleFile,
            sol::this_state s) -> sol::object {
            if (!ui || !file)
                return sol::lua_nil;
            SharedPtr<UIElement> root = ui->LoadLayout(file, styleFile.value_or(nullptr));
            return root ? WrapLuaObject(sol::state_view(s), root.Get()) : sol::lua_nil;
        }
    );
    RegisterLuaObjectWrapper<UI>();

    // Console subsystem (SystemUI): command interpreter + visibility
    // (26_ConsoleInput). Console command events arrive with the "Command"
    // parameter name.
    lua.new_usertype<Console>("Console",
        sol::no_constructor,
        sol::base_classes, sol::bases<Object>(),
        "SetVisible", &Console::SetVisible,
        "Toggle", &Console::Toggle,
        "IsVisible", &Console::IsVisible,
        "SetCommandInterpreter", [](Console* console, const char* name) {
            if (console)
                console->SetCommandInterpreter(name);
        },
        "SetAutoVisibleOnError", &Console::SetAutoVisibleOnError
    );
    RegisterLuaObjectWrapper<Console>();

    // UIComponent: renders a UI subtree into 3D space on a material
    // (48_Hello3DUI).
    lua.new_usertype<UIComponent>("UIComponent",
        sol::no_constructor,
        sol::base_classes, sol::bases<Component, Serializable, Object>(),
        "GetRoot", &UIComponent::GetRoot,
        "GetMaterial", &UIComponent::GetMaterial
    );
    RegisterLuaObjectWrapper<UIComponent>();

    // Keyboard focus modes for UIElement:SetFocusMode.
    sol::table fm = lua.create_named_table("FM");
    fm["NOTFOCUSABLE"] = FM_NOTFOCUSABLE;
    fm["RESETFOCUS"] = FM_RESETFOCUS;
    fm["FOCUSABLE"] = FM_FOCUSABLE;
    fm["FOCUSABLE_DEFOCUSABLE"] = FM_FOCUSABLE_DEFOCUSABLE;

    // ListView highlight modes (48_Hello3DUI).
    sol::table hm = lua.create_named_table("HM");
    hm["NEVER"] = HM_NEVER;
    hm["FOCUS"] = HM_FOCUS;
    hm["ALWAYS"] = HM_ALWAYS;

    // FreeType hinting levels for UI:SetFontHintLevel (47_Typography).
    sol::table fhl = lua.create_named_table("FHL");
    fhl["NONE"] = FONT_HINT_LEVEL_NONE;
    fhl["LIGHT"] = FONT_HINT_LEVEL_LIGHT;
    fhl["NORMAL"] = FONT_HINT_LEVEL_NORMAL;

    // Alignment and layout constants used by the setters above.
    sol::table ha = lua.create_named_table("HA");
    ha["LEFT"] = HA_LEFT;
    ha["CENTER"] = HA_CENTER;
    ha["RIGHT"] = HA_RIGHT;

    sol::table va = lua.create_named_table("VA");
    va["TOP"] = VA_TOP;
    va["CENTER"] = VA_CENTER;
    va["BOTTOM"] = VA_BOTTOM;

    sol::table lm = lua.create_named_table("LM");
    lm["FREE"] = LM_FREE;
    lm["HORIZONTAL"] = LM_HORIZONTAL;
    lm["VERTICAL"] = LM_VERTICAL;

    // Global UI root accessor mirroring the tolua-era LuaSamples helper of
    // the same name; most samples' CreateGUI uses it.
    lua.set_function("GetUIRoot", [context](sol::this_state s) -> sol::object {
        auto* ui = context->GetSubsystem<UI>();
        if (!ui || !ui->GetRoot())
            return sol::lua_nil;
        return WrapLuaObject(sol::state_view(s), ui->GetRoot());
    });

    // Blend modes for BorderImage and Sprite widgets (RenderAPIDefs.h).
    sol::table blend = lua.create_named_table("BLEND");
    blend["REPLACE"] = BLEND_REPLACE;
    blend["ADD"] = BLEND_ADD;
    blend["MULTIPLY"] = BLEND_MULTIPLY;
    blend["ALPHA"] = BLEND_ALPHA;
    blend["ADDALPHA"] = BLEND_ADDALPHA;
    blend["PREMULALPHA"] = BLEND_PREMULALPHA;
    blend["INVDESTALPHA"] = BLEND_INVDESTALPHA;
    blend["SUBTRACT"] = BLEND_SUBTRACT;
    blend["SUBTRACTALPHA"] = BLEND_SUBTRACTALPHA;
    blend["DEFERRED_DECAL"] = BLEND_DEFERRED_DECAL;
}

} // namespace Urho3D
