//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaBindings.h"
#include "LuaBindHelpers.h"
#include "LuaBindMacros.h"

#include "../Urho3D/Core/Context.h"
#include "../Urho3D/IO/Log.h"
#include "../Urho3D/UI/BorderImage.h"
#include "../Urho3D/UI/Button.h"
#include "../Urho3D/UI/CheckBox.h"
#include "../Urho3D/UI/Cursor.h"
#include "../Urho3D/UI/DropDownList.h"
#include "../Urho3D/UI/Font.h"
#include "../Urho3D/UI/LineEdit.h"
#include "../Urho3D/UI/ListView.h"
#include "../Urho3D/UI/Slider.h"
#include "../Urho3D/UI/Sprite.h"
#include "../Urho3D/UI/Text.h"
#include "../Urho3D/UI/ToolTip.h"
#include "../Urho3D/UI/UI.h"
#include "../Urho3D/UI/UIComponent.h"
#include "../Urho3D/SystemUI/Console.h"
#include "../Urho3D/Scene/ValueAnimation.h"
#include "../Urho3D/Scene/Animatable.h"
#include "../Urho3D/UI/UIElement.h"
#include "../Urho3D/UI/Window.h"

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
    {
        using LUA_THIS = UIElement;
        LUA_CLASS(UIElement,
            sol::no_constructor
            LUA_BASES(Serializable, Object)
            // sol3 resolves base-class members only one level deep, so widgets
            // derived from BorderImage must list the full chain to also reach
            // UIElement's methods (02_HelloGUI SetMinWidth, 16_Chat SetStyleAuto).
            LUA_MEMBER_FUNC_RAW(SetName, [](UIElement* element, const char* name) { if (element) element->SetName(name); })
            LUA_MEMBER_FUNC_RAW(GetName, [](UIElement* element) -> std::string { return element ? element->GetName().c_str() : ""; })
            LUA_MEMBER_FUNC_OVERLOAD(SetPosition,
                LUA_CAST(SetPosition, void, const IntVector2&),
                LUA_CAST(SetPosition, void, int, int),
                // Lua arithmetic (e.g. width / 2) yields floats; accept them here
                [](UIElement* element, double x, double y) {
                    if (element)
                        element->SetPosition(static_cast<int>(x), static_cast<int>(y));
                })
            LUA_MEMBER_FUNC(GetPosition)
            LUA_MEMBER_FUNC_OVERLOAD(SetSize,
                LUA_CAST(SetSize, void, const IntVector2&),
                LUA_CAST(SetSize, void, int, int),
                [](UIElement* element, double w, double h) {
                    if (element)
                        element->SetSize(static_cast<int>(w), static_cast<int>(h));
                })
            LUA_MEMBER_FUNC(GetSize)
            LUA_MEMBER_FUNC(SetWidth)
            LUA_MEMBER_FUNC(SetHeight)
            LUA_MEMBER_FUNC(SetFixedWidth)
            LUA_MEMBER_FUNC(SetFixedHeight)
            LUA_MEMBER_FUNC_OVERLOAD(SetFixedSize,
                LUA_CAST(SetFixedSize, void, const IntVector2&),
                LUA_CAST(SetFixedSize, void, int, int),
                [](UIElement* element, double w, double h) {
                    if (element)
                        element->SetFixedSize(static_cast<int>(w), static_cast<int>(h));
                })
            LUA_MEMBER_FUNC(GetWidth)
            LUA_MEMBER_FUNC(GetHeight)
            LUA_MEMBER_FUNC_OVERLOAD(SetMinSize,
                LUA_CAST(SetMinSize, void, const IntVector2&),
                LUA_CAST(SetMinSize, void, int, int),
                [](UIElement* element, double w, double h) {
                    if (element)
                        element->SetMinSize(static_cast<int>(w), static_cast<int>(h));
                })
            LUA_MEMBER_FUNC_OVERLOAD(SetMaxSize,
                LUA_CAST(SetMaxSize, void, const IntVector2&),
                LUA_CAST(SetMaxSize, void, int, int),
                [](UIElement* element, double w, double h) {
                    if (element)
                        element->SetMaxSize(static_cast<int>(w), static_cast<int>(h));
                })
            // Lua arithmetic on GetRowWidth() results yields floats; accept them.
            LUA_MEMBER_FUNC_RAW(SetMinWidth, [](UIElement* element, double width) {
                if (element)
                    element->SetMinWidth(static_cast<int>(width));
            })
            LUA_MEMBER_FUNC_RAW(SetMinHeight, [](UIElement* element, double height) {
                if (element)
                    element->SetMinHeight(static_cast<int>(height));
            })
            LUA_MEMBER_FUNC_RAW(SetMaxWidth, [](UIElement* element, double width) {
                if (element)
                    element->SetMaxWidth(static_cast<int>(width));
            })
            LUA_MEMBER_FUNC_RAW(SetMaxHeight, [](UIElement* element, double height) {
                if (element)
                    element->SetMaxHeight(static_cast<int>(height));
            })
            LUA_MEMBER_FUNC(SetDefaultStyle)
            LUA_MEMBER_FUNC_RAW(SetAlignment, [](UIElement* element, int hAlign, int vAlign) {
                if (element)
                    element->SetAlignment(static_cast<HorizontalAlignment>(hAlign), static_cast<VerticalAlignment>(vAlign));
            })
            LUA_MEMBER_FUNC_RAW(SetHorizontalAlignment, [](UIElement* element, int align) {
                if (element)
                    element->SetHorizontalAlignment(static_cast<HorizontalAlignment>(align));
            })
            LUA_MEMBER_FUNC_RAW(SetVerticalAlignment, [](UIElement* element, int align) {
                if (element)
                    element->SetVerticalAlignment(static_cast<VerticalAlignment>(align));
            })
            LUA_MEMBER_FUNC_RAW(SetColor, [](UIElement* element, const Color& color) {
                if (element)
                    element->SetColor(color);
            })
            LUA_MEMBER_FUNC(SetOpacity)
            LUA_MEMBER_FUNC(SetUseDerivedOpacity)
            LUA_MEMBER_FUNC(SetVisible)
            LUA_MEMBER_FUNC(IsVisible)
            LUA_MEMBER_FUNC(SetEnabled)
            LUA_MEMBER_FUNC(SetFocus)
            LUA_MEMBER_FUNC(SetBringToFront)
            LUA_MEMBER_FUNC_RAW(SetLayout, [](UIElement* element, int mode, sol::optional<int> spacing, sol::optional<IntRect> border) {
                if (element)
                    element->SetLayout(static_cast<LayoutMode>(mode), spacing.value_or(0), border.value_or(IntRect::ZERO));
            })
            LUA_MEMBER_FUNC(SetLayoutSpacing)
            LUA_MEMBER_FUNC(SetLayoutBorder)
            LUA_MEMBER_FUNC_RAW(SetLayoutMode, [](UIElement* element, int mode) {
                if (element)
                    element->SetLayoutMode(static_cast<LayoutMode>(mode));
            })
            LUA_MEMBER_FUNC_RAW(SetStyle, [](UIElement* element, const char* styleName) -> bool {
                return element && element->SetStyle(styleName);
            })
            LUA_MEMBER_FUNC_RAW(SetStyleAuto, [](UIElement* element) -> bool {
                return element && element->SetStyleAuto();
            })
            LUA_MEMBER_FUNC_RAW(CreateChild, [](UIElement* element, const char* typeName, sol::optional<const char*> name,
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
            })
            LUA_MEMBER_FUNC(AddChild)
            LUA_MEMBER_FUNC_RAW(RemoveChild, [](UIElement* parent, UIElement* child) {
                if (parent && child)
                    parent->RemoveChild(child);
            })
            LUA_MEMBER_FUNC(RemoveAllChildren)
            LUA_MEMBER_FUNC_RAW(GetChild, [](UIElement* element, const char* name, sol::optional<bool> recursive,
                sol::this_state s) -> sol::object {
                if (!element)
                    return sol::lua_nil;
                return WrapLuaObject(sol::state_view(s), element->GetChild(name, recursive.value_or(false)));
            })
            LUA_MEMBER_FUNC_RAW(GetChildren, [](UIElement* element, sol::this_state s) -> sol::table {
                sol::state_view lua(s);
                sol::table result = lua.create_table();
                if (element)
                {
                    unsigned index = 1;
                    for (const SharedPtr<UIElement>& child : element->GetChildren())
                        result[index++] = child.Get();
                }
                return result;
            })
            LUA_MEMBER_FUNC(GetParent)
            LUA_MEMBER_FUNC_RAW(SetVar, [](UIElement* element, const char* key, sol::object value, sol::this_state s) {
                if (element)
                    element->SetVar(key, LuaToVariant(sol::state_view(s), value));
            })
            LUA_MEMBER_FUNC_RAW(GetVar, [](UIElement* element, const char* key, sol::this_state s) -> sol::object {
                return element ? VariantToLua(sol::state_view(s), element->GetVar(key)) : sol::lua_nil;
            })
            // Manual layout refresh and z-order control (37_UIDrag).
            LUA_MEMBER_FUNC(BringToFront)
            LUA_MEMBER_FUNC(UpdateLayout)
            LUA_MEMBER_FUNC(HasFocus)
            LUA_MEMBER_FUNC(GetMinWidth)
            // Tags for grouping elements (37_UIDrag drag sources).
            LUA_MEMBER_FUNC_RAW(AddTag, [](UIElement* element, const char* tag) {
                if (element)
                    element->AddTag(tag);
            })
            LUA_MEMBER_FUNC(HasTag)
            LUA_MEMBER_FUNC_RAW(GetChildrenWithTag, [](UIElement* element, const char* tag, sol::optional<bool> recursive,
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
            })
            // Keyboard focus behavior (49/50 Sample2D UI buttons).
            LUA_MEMBER_FUNC_RAW(SetFocusMode, [](UIElement* element, int mode) {
                if (element)
                    element->SetFocusMode(static_cast<FocusMode>(mode));
            })
            // Render order within the parent (37_UIDrag windows above sprites).
            LUA_MEMBER_FUNC(SetPriority)
            // Object lifetime hints.
            LUA_MEMBER_FUNC(SetTemporary)
            // Attribute animation, e.g. animating Text's "Text" attribute
            // (30_LightAnimation).
            LUA_MEMBER_FUNC_RAW(SetAttributeAnimation, [](UIElement* element, const char* name,
                ValueAnimation* animation, sol::optional<int> wrapMode, sol::optional<float> speed) {
                if (element)
                    element->SetAttributeAnimation(name, animation,
                        static_cast<WrapMode>(wrapMode.value_or(WM_LOOP)), speed.value_or(1.0f));
            })
        );
    }
    RegisterLuaObjectWrapper<UIElement>();

    // Font resource: only a type marker, consumed by Text:SetFont.
    {
        using LUA_THIS = Font;
        LUA_CLASS(Font,
            sol::no_constructor
            LUA_BASES(Resource, Object)
        );
    }
    RegisterLuaObjectWrapper<Font>();

    // Text label. Constructible standalone so it can be filled into
    // ListView/DropDownList item slots (47_Typography, 54_WindowSettings).
    {
        using LUA_THIS = Text;
        LUA_CLASS(Text,
            sol::call_constructor, sol::factories([context]() {
                return SharedPtr<Text>(new Text(context));
            })
            LUA_BASES(UIElement, Serializable, Object)
            LUA_MEMBER_FUNC_RAW(SetText, [](Text* text, const char* value) {
                if (text)
                    text->SetText(value);
            })
            LUA_MEMBER_FUNC_OVERLOAD(SetFont,
                [](Text* text, Font* font) -> bool { return text && text->SetFont(font); },
                [](Text* text, Font* font, float size) -> bool { return text && text->SetFont(font, size); },
                [](Text* text, const char* fontName, float size) -> bool { return text && text->SetFont(fontName, size); })
            LUA_MEMBER_FUNC(SetFontSize)
            LUA_MEMBER_FUNC_RAW(SetTextAlignment, [](Text* text, int align) {
                if (text)
                    text->SetTextAlignment(static_cast<HorizontalAlignment>(align));
            })
            LUA_MEMBER_FUNC(SetRowSpacing)
            LUA_MEMBER_FUNC(SetWordwrap)
            LUA_MEMBER_FUNC(GetNumRows)
            LUA_MEMBER_FUNC(GetRowHeight)
            LUA_MEMBER_FUNC(GetRowWidth)
            // Text effects (35_SignedDistanceFieldText).
            LUA_MEMBER_FUNC_RAW(SetTextEffect, [](Text* text, int effect) {
                if (text)
                    text->SetTextEffect(static_cast<TextEffect>(effect));
            })
            LUA_MEMBER_FUNC(SetEffectColor)
            LUA_MEMBER_FUNC(SetEffectDepthBias)
            // Localization of the displayed string (40_Localization).
            LUA_MEMBER_FUNC(SetAutoLocalizable)
        );
    }
    RegisterLuaObjectWrapper<Text>();

    // Text effect constants.
    LUA_ENUM_TABLE(TE, "NONE", TE_NONE, "SHADOW", TE_SHADOW, "STROKE", TE_STROKE);

    // BorderImage: textured widget base.
    {
        using LUA_THIS = BorderImage;
        LUA_CLASS(BorderImage,
            sol::no_constructor
            LUA_BASES(UIElement, Serializable, Object)
            LUA_MEMBER_FUNC(SetTexture)
            LUA_MEMBER_FUNC(GetTexture)
            LUA_MEMBER_FUNC_RAW(SetBlendMode, [](BorderImage* image, int mode) {
                if (image)
                    image->SetBlendMode(static_cast<BlendMode>(mode));
            })
            // Sub-rectangle of the texture to display (49/50 UI backgrounds).
            LUA_MEMBER_FUNC(SetImageRect)
            LUA_MEMBER_FUNC(SetFullImageRect)
        );
    }
    RegisterLuaObjectWrapper<BorderImage>();

    // Button: styled through SetStyle("Button") + released/pressed events.
    {
        using LUA_THIS = Button;
        LUA_CLASS(Button,
            sol::no_constructor
            LUA_BASES(BorderImage, UIElement, Serializable, Object)
        );
    }
    RegisterLuaObjectWrapper<Button>();

    // LineEdit: single-line text input with TextFinished event (16_Chat).
    // Registered after BorderImage, which its LuaBases chain requires.
    {
        using LUA_THIS = LineEdit;
        LUA_CLASS(LineEdit,
            sol::no_constructor
            LUA_BASES(BorderImage, UIElement, Serializable, Object)
            LUA_MEMBER_FUNC_RAW(SetText, [](LineEdit* edit, const char* value) {
                if (edit)
                    edit->SetText(value);
            })
            LUA_MEMBER_FUNC_RAW(GetText, [](LineEdit* edit) -> const char* { return edit ? edit->GetText().c_str() : ""; })
        );
    }
    RegisterLuaObjectWrapper<LineEdit>();

    // Window: draggable container with optional modality.
    {
        using LUA_THIS = Window;
        LUA_CLASS(Window,
            sol::no_constructor
            LUA_BASES(BorderImage, UIElement, Serializable, Object)
            LUA_MEMBER_FUNC(SetModal)
            LUA_MEMBER_FUNC(SetMovable)
        );
    }
    RegisterLuaObjectWrapper<Window>();

    // CheckBox: two-state toggle widget (14_SoundEffects).
    {
        using LUA_THIS = CheckBox;
        LUA_CLASS(CheckBox,
            sol::no_constructor
            LUA_BASES(BorderImage, UIElement, Serializable, Object)
            LUA_MEMBER_FUNC(SetChecked)
            LUA_MEMBER_FUNC(IsChecked)
        );
    }
    RegisterLuaObjectWrapper<CheckBox>();

    // Slider: float value in a range, fires SliderChanged events.
    {
        using LUA_THIS = Slider;
        LUA_CLASS(Slider,
            sol::no_constructor
            LUA_BASES(BorderImage, UIElement, Serializable, Object)
            LUA_MEMBER_FUNC(SetRange)
            LUA_MEMBER_FUNC(SetValue)
            LUA_MEMBER_FUNC(GetValue)
        );
    }
    RegisterLuaObjectWrapper<Slider>();

    // DropDownList: item picker with a popup list (14_SoundEffects).
    {
        using LUA_THIS = DropDownList;
        LUA_CLASS(DropDownList,
            sol::no_constructor
            LUA_BASES(Button, BorderImage, UIElement, Serializable, Object)
            LUA_MEMBER_FUNC(AddItem)
            LUA_MEMBER_FUNC(RemoveAllItems)
            LUA_MEMBER_FUNC_RAW(SetSelection, [](DropDownList* list, double index) {
                if (list)
                    list->SetSelection(static_cast<unsigned>(index));
            })
            LUA_MEMBER_FUNC(GetSelection)
            LUA_MEMBER_FUNC(GetNumItems)
            LUA_MEMBER_FUNC(GetItem)
            LUA_MEMBER_FUNC(GetSelectedItem)
        );
    }
    RegisterLuaObjectWrapper<DropDownList>();

    // ListView: scrollable item list (47_Typography, 54_WindowSettings).
    {
        using LUA_THIS = ListView;
        LUA_CLASS(ListView,
            sol::no_constructor
            LUA_BASES(UIElement, Serializable, Object)
            LUA_MEMBER_FUNC(AddItem)
            LUA_MEMBER_FUNC(RemoveAllItems)
            LUA_MEMBER_FUNC_RAW(SetSelection, [](ListView* list, double index) {
                if (list)
                    list->SetSelection(static_cast<unsigned>(index));
            })
            LUA_MEMBER_FUNC(GetSelection)
            LUA_MEMBER_FUNC(GetNumItems)
            LUA_MEMBER_FUNC(GetItem)
            LUA_MEMBER_FUNC_RAW(SetHighlightMode, [](ListView* list, int mode) {
                if (list)
                    list->SetHighlightMode(static_cast<HighlightMode>(mode));
            })
            LUA_MEMBER_FUNC(SetSelectOnClickEnd)
        );
    }
    RegisterLuaObjectWrapper<ListView>();

    // ToolTip: hover help container attached to a widget (48_Hello3DUI).
    {
        using LUA_THIS = ToolTip;
        LUA_CLASS(ToolTip,
            sol::no_constructor
            LUA_BASES(UIElement, Serializable, Object)
        );
    }
    RegisterLuaObjectWrapper<ToolTip>();

    // Sprite: textured quad for 2D overlay work (18_Urho2DSprite).
    {
        using LUA_THIS = Sprite;
        LUA_CLASS(Sprite,
            sol::no_constructor
            LUA_BASES(UIElement, Serializable, Object)
            LUA_MEMBER_FUNC(SetTexture)
            LUA_MEMBER_FUNC(SetImageRect)
            LUA_MEMBER_FUNC_RAW(SetBlendMode, [](Sprite* sprite, int mode) {
                if (sprite)
                    sprite->SetBlendMode(static_cast<BlendMode>(mode));
            })
            LUA_MEMBER_FUNC_OVERLOAD(SetHotSpot,
                LUA_CAST(SetHotSpot, void, const IntVector2&),
                LUA_CAST(SetHotSpot, void, int, int),
                [](Sprite* sprite, double x, double y) {
                    if (sprite)
                        sprite->SetHotSpot(static_cast<int>(x), static_cast<int>(y));
                })
            LUA_MEMBER_FUNC_OVERLOAD(SetScale,
                LUA_CAST(SetScale, void, const Vector2&),
                LUA_CAST(SetScale, void, float, float),
                LUA_CAST(SetScale, void, float))
            LUA_MEMBER_FUNC(SetRotation)
            LUA_MEMBER_FUNC(GetRotation)
        );
    }
    RegisterLuaObjectWrapper<Sprite>();

    // Cursor: OS/software cursor element (08_Decals and friends).
    // Constructed explicitly and installed through UI:SetCursor when the
    // sample needs to toggle cursor visibility (17_SceneReplication).
    {
        using LUA_THIS = Cursor;
        LUA_CLASS(Cursor,
            sol::call_constructor, sol::factories([context]() {
                return SharedPtr<Cursor>(new Cursor(context));
            })
            LUA_BASES(BorderImage, UIElement, Serializable, Object)
        );
    }
    RegisterLuaObjectWrapper<Cursor>();

    // UI subsystem: root element access and global scale.
    {
        using LUA_THIS = UI;
        LUA_CLASS(UI,
            sol::no_constructor
            LUA_BASES(Object)
            LUA_MEMBER_FUNC(GetRoot)
            LUA_MEMBER_FUNC(GetRootModalElement)
            LUA_MEMBER_FUNC(GetFocusElement)
            LUA_MEMBER_FUNC(GetCursor)
            LUA_MEMBER_FUNC(SetCursor)
            LUA_MEMBER_FUNC(GetUICursorPosition)
            LUA_MEMBER_FUNC_RAW(GetElementAt, [](UI* ui, const IntVector2& position, sol::optional<bool> enabledOnly) {
                return ui ? ui->GetElementAt(position, enabledOnly.value_or(true)) : nullptr;
            })
            LUA_MEMBER_FUNC(ConvertUIToSystem)
            LUA_MEMBER_FUNC(SetScale)
            LUA_MEMBER_FUNC(SetUseSystemClipboard)
            // Font rendering configuration (47_Typography).
            LUA_MEMBER_FUNC(SetForceAutoHint)
            LUA_MEMBER_FUNC(GetForceAutoHint)
            LUA_MEMBER_FUNC_RAW(SetFontHintLevel, [](UI* ui, int level) {
                if (ui)
                    ui->SetFontHintLevel(static_cast<FontHintLevel>(level));
            })
            LUA_MEMBER_FUNC_RAW(GetFontHintLevel, [](UI* ui) {
                return ui ? static_cast<int>(ui->GetFontHintLevel()) : 0;
            })
            LUA_MEMBER_FUNC(SetFontSubpixelThreshold)
            LUA_MEMBER_FUNC(GetFontSubpixelThreshold)
            LUA_MEMBER_FUNC(SetFontOversampling)
            LUA_MEMBER_FUNC(GetFontOversampling)
            // Immediate-mode debug outline of the element hierarchy
            // (48_Hello3DUI).
            LUA_MEMBER_FUNC(DebugDraw)
            // Instantiate a widget tree from a layout XML file (38_SceneAndUILoad).
            LUA_MEMBER_FUNC_RAW(LoadLayout, [](UI* ui, XMLFile* file, sol::optional<XMLFile*> styleFile,
                sol::this_state s) -> sol::object {
                if (!ui || !file)
                    return sol::lua_nil;
                SharedPtr<UIElement> root = ui->LoadLayout(file, styleFile.value_or(nullptr));
                return root ? WrapLuaObject(sol::state_view(s), root.Get()) : sol::lua_nil;
            })
        );
    }
    RegisterLuaObjectWrapper<UI>();

    // Console subsystem (SystemUI): command interpreter + visibility
    // (26_ConsoleInput). Console command events arrive with the "Command"
    // parameter name.
    {
        using LUA_THIS = Console;
        LUA_CLASS(Console,
            sol::no_constructor
            LUA_BASES(Object)
            LUA_MEMBER_FUNC(SetVisible)
            LUA_MEMBER_FUNC(Toggle)
            LUA_MEMBER_FUNC(IsVisible)
            LUA_MEMBER_FUNC_RAW(SetCommandInterpreter, [](Console* console, const char* name) {
                if (console)
                    console->SetCommandInterpreter(name);
            })
            LUA_MEMBER_FUNC(SetAutoVisibleOnError)
        );
    }
    RegisterLuaObjectWrapper<Console>();

    // UIComponent: renders a UI subtree into 3D space on a material
    // (48_Hello3DUI).
    {
        using LUA_THIS = UIComponent;
        LUA_CLASS(UIComponent,
            sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(GetRoot)
            LUA_MEMBER_FUNC(GetMaterial)
        );
    }
    RegisterLuaObjectWrapper<UIComponent>();

    // Keyboard focus modes for UIElement:SetFocusMode.
    LUA_ENUM_TABLE(FM, "NOTFOCUSABLE", FM_NOTFOCUSABLE, "RESETFOCUS", FM_RESETFOCUS, "FOCUSABLE",
        FM_FOCUSABLE, "FOCUSABLE_DEFOCUSABLE", FM_FOCUSABLE_DEFOCUSABLE);

    // ListView highlight modes (48_Hello3DUI).
    LUA_ENUM_TABLE(HM, "NEVER", HM_NEVER, "FOCUS", HM_FOCUS, "ALWAYS", HM_ALWAYS);

    // FreeType hinting levels for UI:SetFontHintLevel (47_Typography).
    LUA_ENUM_TABLE(FHL, "NONE", FONT_HINT_LEVEL_NONE, "LIGHT", FONT_HINT_LEVEL_LIGHT, "NORMAL",
        FONT_HINT_LEVEL_NORMAL);

    // Alignment and layout constants used by the setters above.
    LUA_ENUM_TABLE(HA, "LEFT", HA_LEFT, "CENTER", HA_CENTER, "RIGHT", HA_RIGHT);

    LUA_ENUM_TABLE(VA, "TOP", VA_TOP, "CENTER", VA_CENTER, "BOTTOM", VA_BOTTOM);

    LUA_ENUM_TABLE(LM, "FREE", LM_FREE, "HORIZONTAL", LM_HORIZONTAL, "VERTICAL", LM_VERTICAL);

    // Global UI root accessor mirroring the tolua-era LuaSamples helper of
    // the same name; most samples' CreateGUI uses it.
    LUA_GLOBAL_FUNC(GetUIRoot, [context](sol::this_state s) -> sol::object {
        auto* ui = context->GetSubsystem<UI>();
        if (!ui || !ui->GetRoot())
            return sol::lua_nil;
        return WrapLuaObject(sol::state_view(s), ui->GetRoot());
    });

    // Blend modes for BorderImage and Sprite widgets (RenderAPIDefs.h).
    LUA_ENUM_TABLE(BLEND, "REPLACE", BLEND_REPLACE, "ADD", BLEND_ADD, "MULTIPLY", BLEND_MULTIPLY,
        "ALPHA", BLEND_ALPHA, "ADDALPHA", BLEND_ADDALPHA, "PREMULALPHA", BLEND_PREMULALPHA,
        "INVDESTALPHA", BLEND_INVDESTALPHA, "SUBTRACT", BLEND_SUBTRACT, "SUBTRACTALPHA",
        BLEND_SUBTRACTALPHA, "DEFERRED_DECAL", BLEND_DEFERRED_DECAL);
}

} // namespace Urho3D
